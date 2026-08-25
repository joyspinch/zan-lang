/* irgen_abi.c -- C calling-convention classification for extern (FFI) calls.
 *
 * LLVM leaves aggregate classification to the frontend: an argument typed
 * `%struct.P` in IR is *not* what a C compiler passes for `struct P`. Win64
 * puts an 8-byte struct in one integer register and hands anything else over
 * as a pointer to a caller-made copy; SysV splits the first 16 bytes into
 * eightbytes classified INTEGER or SSE; AArch64 has homogeneous float
 * aggregates. Getting this wrong reads arguments out of the wrong registers.
 *
 * Every extern whose signature mentions a struct therefore gets two functions:
 * the real symbol, declared with the register/memory shape the platform C
 * compiler uses, and an internal thunk carrying the Zan-level signature that
 * converts between the two. Call sites keep calling the Zan-level signature.
 */

typedef enum {
    ABI_TARGET_WIN64,
    ABI_TARGET_SYSV64,
    ABI_TARGET_AARCH64,
    ABI_TARGET_UNSUPPORTED
} abi_target_t;

typedef enum {
    ABI_SLOT_DIRECT,    /* passed as declared */
    ABI_SLOT_COERCE,    /* passed as 1-2 register-shaped values */
    ABI_SLOT_INDIRECT,  /* passed as a pointer to a copy */
    ABI_SLOT_SRET       /* returned through a caller-provided pointer */
} abi_slot_kind_t;

typedef struct {
    abi_slot_kind_t kind;
    LLVMTypeRef ty;        /* the Zan-level (declared) type */
    LLVMTypeRef parts[2];  /* register shapes for ABI_SLOT_COERCE */
    int nparts;
    bool byval;            /* SysV memory arguments carry byval(ty) */
    unsigned stack_align;  /* AArch64 HFA arguments carry alignstack(8) */
} abi_slot_t;

static abi_target_t abi_target_of(zan_irgen_t *g) {
    const char *t = g->target_triple;
    if (t && t[0]) {
        if (strncmp(t, "x86_64", 6) == 0 || strncmp(t, "amd64", 5) == 0)
            return strstr(t, "windows") ? ABI_TARGET_WIN64 : ABI_TARGET_SYSV64;
        if (strncmp(t, "aarch64", 7) == 0 || strncmp(t, "arm64", 5) == 0)
            return ABI_TARGET_AARCH64;
        return ABI_TARGET_UNSUPPORTED;
    }
#if defined(_WIN64) || defined(_M_X64)
    return ABI_TARGET_WIN64;
#elif defined(__aarch64__)
    return ABI_TARGET_AARCH64;
#elif defined(__x86_64__)
    return ABI_TARGET_SYSV64;
#else
    return ABI_TARGET_UNSUPPORTED;
#endif
}

static bool abi_target_is_apple(zan_irgen_t *g) {
    const char *t = g->target_triple;
    if (t && t[0]) return strstr(t, "apple") != NULL;
#if defined(__APPLE__)
    return true;
#else
    return false;
#endif
}

static unsigned long abi_size_of(LLVMTypeRef t);

static unsigned long abi_align_of(LLVMTypeRef t) {
    switch (LLVMGetTypeKind(t)) {
    case LLVMIntegerTypeKind: {
        unsigned long b = (LLVMGetIntTypeWidth(t) + 7) / 8;
        return b > 8 ? 8 : (b == 0 ? 1 : b);
    }
    case LLVMFloatTypeKind:   return 4;
    case LLVMDoubleTypeKind:  return 8;
    case LLVMPointerTypeKind: return 8;
    case LLVMArrayTypeKind:   return abi_align_of(LLVMGetElementType(t));
    case LLVMStructTypeKind: {
        if (LLVMIsPackedStruct(t)) return 1;
        unsigned n = LLVMCountStructElementTypes(t);
        unsigned long maxa = 1;
        for (unsigned i = 0; i < n; i++) {
            unsigned long a = abi_align_of(LLVMStructGetTypeAtIndex(t, i));
            if (a > maxa) maxa = a;
        }
        return maxa;
    }
    default: return 8;
    }
}

static unsigned long abi_size_of(LLVMTypeRef t) {
    switch (LLVMGetTypeKind(t)) {
    case LLVMIntegerTypeKind: return (LLVMGetIntTypeWidth(t) + 7) / 8;
    case LLVMFloatTypeKind:   return 4;
    case LLVMDoubleTypeKind:  return 8;
    case LLVMPointerTypeKind: return 8;
    case LLVMArrayTypeKind:
        return (unsigned long)LLVMGetArrayLength(t) *
               abi_size_of(LLVMGetElementType(t));
    case LLVMStructTypeKind: {
        bool packed = LLVMIsPackedStruct(t);
        unsigned n = LLVMCountStructElementTypes(t);
        unsigned long off = 0, maxa = 1;
        for (unsigned i = 0; i < n; i++) {
            LLVMTypeRef f = LLVMStructGetTypeAtIndex(t, i);
            unsigned long a = packed ? 1 : abi_align_of(f);
            if (a > maxa) maxa = a;
            off = (off + a - 1) & ~(a - 1);
            off += abi_size_of(f);
        }
        return (off + maxa - 1) & ~(maxa - 1);
    }
    default: return 8;
    }
}

static bool abi_is_aggregate(LLVMTypeRef t) {
    LLVMTypeKind k = LLVMGetTypeKind(t);
    return k == LLVMStructTypeKind || k == LLVMArrayTypeKind;
}

/* ---- SysV x86-64 eightbyte classification ---- */

typedef enum { SYSV_NONE = 0, SYSV_SSE, SYSV_INTEGER, SYSV_MEMORY } sysv_class_t;

typedef struct {
    sysv_class_t cls[2];
    bool has_double[2];      /* an SSE eightbyte holding a double, not floats */
    unsigned long used[2];   /* bytes of the eightbyte the fields reach into */
    unsigned long size;
    bool memory;
} sysv_info_t;

static void sysv_merge(sysv_info_t *in, unsigned long off, unsigned long width,
                       sysv_class_t c, bool is_double) {
    int eb = (int)(off / 8);
    if (eb < 0 || eb > 1) { in->memory = true; return; }
    if (c == SYSV_INTEGER) in->cls[eb] = SYSV_INTEGER;
    else if (in->cls[eb] == SYSV_NONE) in->cls[eb] = SYSV_SSE;
    if (c == SYSV_SSE && is_double) in->has_double[eb] = true;
    unsigned long end = off % 8 + width;
    if (end > 8) end = 8;
    if (end > in->used[eb]) in->used[eb] = end;
}

static void sysv_walk(LLVMTypeRef t, unsigned long off, sysv_info_t *in) {
    switch (LLVMGetTypeKind(t)) {
    case LLVMIntegerTypeKind:
    case LLVMPointerTypeKind: {
        unsigned long w = abi_size_of(t);
        sysv_merge(in, off, w > 8 ? 8 : w, SYSV_INTEGER, false);
        if (w > 8) sysv_merge(in, off + 8, w - 8, SYSV_INTEGER, false);
        break;
    }
    case LLVMFloatTypeKind:
        sysv_merge(in, off, 4, SYSV_SSE, false);
        break;
    case LLVMDoubleTypeKind:
        sysv_merge(in, off, 8, SYSV_SSE, true);
        break;
    case LLVMArrayTypeKind: {
        LLVMTypeRef el = LLVMGetElementType(t);
        unsigned long es = abi_size_of(el);
        for (unsigned i = 0; i < LLVMGetArrayLength(t); i++)
            sysv_walk(el, off + i * es, in);
        break;
    }
    case LLVMStructTypeKind: {
        bool packed = LLVMIsPackedStruct(t);
        unsigned n = LLVMCountStructElementTypes(t);
        unsigned long fo = off;
        for (unsigned i = 0; i < n; i++) {
            LLVMTypeRef f = LLVMStructGetTypeAtIndex(t, i);
            unsigned long a = packed ? 1 : abi_align_of(f);
            fo = (fo + a - 1) & ~(a - 1);
            sysv_walk(f, fo, in);
            fo += abi_size_of(f);
        }
        break;
    }
    default:
        in->memory = true;
        break;
    }
}

static void sysv_classify(LLVMTypeRef t, sysv_info_t *in) {
    memset(in, 0, sizeof(*in));
    in->size = abi_size_of(t);
    if (in->size > 16 || in->size == 0) { in->memory = true; return; }
    sysv_walk(t, 0, in);
}

/* Register shape of eightbyte `eb`: an INTEGER eightbyte becomes an integer
 * exactly as wide as the bytes it holds (i64 / i32 / i24 ...), an SSE eightbyte
 * becomes double, <2 x float> or float. */
static LLVMTypeRef sysv_part_type(zan_irgen_t *g, sysv_info_t *in, int eb) {
    unsigned long rest = in->size - (unsigned long)eb * 8;
    unsigned long bytes = rest > 8 ? 8 : rest;
    /* trailing padding is not part of the register: {int, long} passes its
     * first eightbyte as i32, the way a C compiler does */
    if (in->used[eb] && in->used[eb] < bytes) bytes = in->used[eb];
    if (in->cls[eb] == SYSV_INTEGER)
        return LLVMIntTypeInContext(g->ctx, (unsigned)(bytes * 8));
    if (in->has_double[eb]) return LLVMDoubleTypeInContext(g->ctx);
    if (bytes > 4)
        return LLVMVectorType(LLVMFloatTypeInContext(g->ctx), 2);
    return LLVMFloatTypeInContext(g->ctx);
}

/* ---- AArch64 homogeneous float aggregates ---- */

static bool aarch64_hfa(LLVMTypeRef t, LLVMTypeRef *base, int *count) {
    switch (LLVMGetTypeKind(t)) {
    case LLVMFloatTypeKind:
    case LLVMDoubleTypeKind:
        if (*base && *base != t) return false;
        *base = t;
        (*count)++;
        return *count <= 4;
    case LLVMArrayTypeKind: {
        LLVMTypeRef el = LLVMGetElementType(t);
        for (unsigned i = 0; i < LLVMGetArrayLength(t); i++)
            if (!aarch64_hfa(el, base, count)) return false;
        return true;
    }
    case LLVMStructTypeKind: {
        unsigned n = LLVMCountStructElementTypes(t);
        for (unsigned i = 0; i < n; i++)
            if (!aarch64_hfa(LLVMStructGetTypeAtIndex(t, i), base, count))
                return false;
        return true;
    }
    default:
        return false;
    }
}

/* ---- classification entry points ---- */

static void abi_classify(zan_irgen_t *g, abi_target_t tgt, LLVMTypeRef ty,
                         bool is_return, abi_slot_t *slot) {
    memset(slot, 0, sizeof(*slot));
    slot->ty = ty;
    slot->kind = ABI_SLOT_DIRECT;
    if (!abi_is_aggregate(ty)) return;

    unsigned long size = abi_size_of(ty);

    if (tgt == ABI_TARGET_WIN64) {
        if (size == 1 || size == 2 || size == 4 || size == 8) {
            slot->kind = ABI_SLOT_COERCE;
            slot->parts[0] = LLVMIntTypeInContext(g->ctx, (unsigned)(size * 8));
            slot->nparts = 1;
        } else {
            /* the caller owns the copy; Win64 has no byval */
            slot->kind = is_return ? ABI_SLOT_SRET : ABI_SLOT_INDIRECT;
        }
        return;
    }

    if (tgt == ABI_TARGET_SYSV64) {
        sysv_info_t in;
        sysv_classify(ty, &in);
        if (in.memory) {
            slot->kind = is_return ? ABI_SLOT_SRET : ABI_SLOT_INDIRECT;
            slot->byval = !is_return;
            return;
        }
        slot->kind = ABI_SLOT_COERCE;
        slot->parts[0] = sysv_part_type(g, &in, 0);
        slot->nparts = 1;
        if (in.size > 8) {
            slot->parts[1] = sysv_part_type(g, &in, 1);
            slot->nparts = 2;
        }
        return;
    }

    if (tgt == ABI_TARGET_AARCH64) {
        LLVMTypeRef base = NULL;
        int count = 0;
        if (aarch64_hfa(ty, &base, &count) && base && count > 0 && count <= 4) {
            if (is_return) return;   /* returned in v0-v3 as the struct itself */
            slot->kind = ABI_SLOT_COERCE;
            slot->parts[0] = LLVMArrayType(base, (unsigned)count);
            slot->nparts = 1;
            if (!abi_target_is_apple(g)) slot->stack_align = 8;
            return;
        }
        if (size > 16) {
            slot->kind = is_return ? ABI_SLOT_SRET : ABI_SLOT_INDIRECT;
            return;
        }
        slot->kind = ABI_SLOT_COERCE;
        slot->nparts = 1;
        if (size <= 8) {
            slot->parts[0] = is_return
                ? LLVMIntTypeInContext(g->ctx, (unsigned)(size * 8))
                : LLVMInt64TypeInContext(g->ctx);
        } else {
            slot->parts[0] = LLVMArrayType(LLVMInt64TypeInContext(g->ctx), 2);
        }
        return;
    }

    /* Unknown target: leave the declaration alone and let the caller report it. */
}

static void abi_add_type_attr(zan_irgen_t *g, LLVMValueRef fn, LLVMValueRef call,
                              unsigned idx, const char *name, LLVMTypeRef ty) {
    unsigned kind = LLVMGetEnumAttributeKindForName(name, strlen(name));
    if (!kind) return;
    if (fn)
        LLVMAddAttributeAtIndex(fn, idx, LLVMCreateTypeAttribute(g->ctx, kind, ty));
    if (call)
        LLVMAddCallSiteAttribute(call, idx,
                                 LLVMCreateTypeAttribute(g->ctx, kind, ty));
}

static void abi_add_int_attr(zan_irgen_t *g, LLVMValueRef fn, LLVMValueRef call,
                             unsigned idx, const char *name, uint64_t val) {
    unsigned kind = LLVMGetEnumAttributeKindForName(name, strlen(name));
    if (!kind) return;
    if (fn)
        LLVMAddAttributeAtIndex(fn, idx, LLVMCreateEnumAttribute(g->ctx, kind, val));
    if (call)
        LLVMAddCallSiteAttribute(call, idx,
                                 LLVMCreateEnumAttribute(g->ctx, kind, val));
}

/* The C ABI promotes anything narrower than `int` at the boundary, and every
 * supported target makes that the *caller's* job: AAPCS64 and the RISC-V ABI
 * let the callee read the whole register, so an unextended i1/i8/i16 argument
 * arrives with garbage above its own width. `char`/enum are 64-bit here and
 * need no promotion. */
static const char *abi_int_ext_attr(zan_type_t *t) {
    if (!t) return NULL;
    switch (t->kind) {
    case TYPE_BOOL:
    case TYPE_BYTE:
    case TYPE_USHORT: return "zeroext";
    case TYPE_SBYTE:
    case TYPE_SHORT:  return "signext";
    default:          return NULL;
    }
}

/* Attach the promotion attributes for one extern declaration. `ptypes` holds
 * the resolved parameter types in declaration order. */
static void abi_add_int_ext_attrs(zan_irgen_t *g, LLVMValueRef fn,
                                  zan_type_t *ret, zan_type_t **ptypes,
                                  int pc) {
    const char *r = abi_int_ext_attr(ret);
    if (r) abi_add_int_attr(g, fn, NULL, (unsigned)LLVMAttributeReturnIndex, r, 0);
    for (int i = 0; i < pc; i++) {
        const char *a = abi_int_ext_attr(ptypes[i]);
        if (a) abi_add_int_attr(g, fn, NULL, (unsigned)(i + 1), a, 0);
    }
}

static LLVMValueRef abi_byte_ptr(zan_irgen_t *g, LLVMValueRef base,
                                 unsigned long off) {
    if (off == 0) return base;
    LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
    LLVMValueRef idx = LLVMConstInt(LLVMInt64TypeInContext(g->ctx), off, 0);
    return LLVMBuildInBoundsGEP2(g->builder, i8, base, &idx, 1, "abi.off");
}

/* Declare `name` with the platform C signature and wrap it in an internal
 * thunk that keeps the Zan-level signature `zan_ft`. Returns NULL when no
 * struct crosses the boundary (the plain declaration is then correct). */
static LLVMValueRef abi_extern_thunk(zan_irgen_t *g, const char *name,
                                     LLVMTypeRef zan_ft) {
    unsigned pc = LLVMCountParamTypes(zan_ft);
    /* Sized by the actual parameter count: a fixed cap here used to bail out
     * for wide signatures, and the caller then declared the extern with the
     * Zan-level signature -- passing structs by the wrong ABI instead of
     * reporting anything. */
    LLVMTypeRef *zan_params =
        (LLVMTypeRef *)calloc(pc ? pc : 1, sizeof(LLVMTypeRef));
    if (!zan_params) return NULL;
    LLVMGetParamTypes(zan_ft, zan_params);
    LLVMTypeRef zan_ret = LLVMGetReturnType(zan_ft);

    bool any = abi_is_aggregate(zan_ret);
    for (unsigned i = 0; i < pc && !any; i++) any = abi_is_aggregate(zan_params[i]);
    if (!any) { free(zan_params); return NULL; }

    abi_target_t tgt = abi_target_of(g);
    if (tgt == ABI_TARGET_UNSUPPORTED) {
        zan_diag_emit(g->diag, DIAG_ERROR, zan_loc(0, 0, 0, 0),
                      "extern '%s' passes a struct by value, which has no C ABI "
                      "classification for target '%s'", name,
                      g->target_triple[0] ? g->target_triple : "host");
        free(zan_params);
        return NULL;
    }

    abi_slot_t ret_slot;
    abi_slot_t *arg_slots =
        (abi_slot_t *)calloc(pc ? pc : 1, sizeof(abi_slot_t));
    LLVMTypeRef *c_params =
        (LLVMTypeRef *)calloc((size_t)pc * 2 + 1, sizeof(LLVMTypeRef));
    LLVMValueRef *c_args =
        (LLVMValueRef *)calloc((size_t)pc * 2 + 1, sizeof(LLVMValueRef));
    if (!arg_slots || !c_params || !c_args) {
        free(arg_slots); free(c_params); free(c_args); free(zan_params);
        return NULL;
    }
    abi_classify(g, tgt, zan_ret, true, &ret_slot);
    for (unsigned i = 0; i < pc; i++)
        abi_classify(g, tgt, zan_params[i], false, &arg_slots[i]);

    /* --- the real C signature --- */
    unsigned cn = 0;
    bool sret = (ret_slot.kind == ABI_SLOT_SRET);
    LLVMTypeRef ptr_ty = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    if (sret) c_params[cn++] = ptr_ty;
    for (unsigned i = 0; i < pc; i++) {
        abi_slot_t *s = &arg_slots[i];
        if (s->kind == ABI_SLOT_COERCE)
            for (int k = 0; k < s->nparts; k++) c_params[cn++] = s->parts[k];
        else if (s->kind == ABI_SLOT_INDIRECT)
            c_params[cn++] = ptr_ty;
        else
            c_params[cn++] = zan_params[i];
    }
    LLVMTypeRef c_ret = LLVMVoidTypeInContext(g->ctx);
    if (!sret) {
        if (ret_slot.kind == ABI_SLOT_COERCE) {
            c_ret = ret_slot.nparts == 1
                ? ret_slot.parts[0]
                : LLVMStructTypeInContext(g->ctx, ret_slot.parts, 2, 0);
        } else {
            c_ret = zan_ret;
        }
    }
    LLVMTypeRef c_ft = LLVMFunctionType(c_ret, c_params, cn, 0);

    LLVMValueRef c_fn = LLVMGetNamedFunction(g->mod, name);
    if (!c_fn) {
        c_fn = LLVMAddFunction(g->mod, name, c_ft);
        if (sret) {
            abi_add_type_attr(g, c_fn, NULL, 1, "sret", zan_ret);
            abi_add_int_attr(g, c_fn, NULL, 1, "align",
                             abi_align_of(zan_ret));
        }
        unsigned ai = sret ? 2 : 1;
        for (unsigned i = 0; i < pc; i++) {
            abi_slot_t *s = &arg_slots[i];
            if (s->kind == ABI_SLOT_COERCE) {
                if (s->stack_align)
                    abi_add_int_attr(g, c_fn, NULL, ai, "alignstack",
                                     s->stack_align);
                ai += (unsigned)s->nparts;
                continue;
            }
            if (s->kind == ABI_SLOT_INDIRECT && s->byval) {
                abi_add_type_attr(g, c_fn, NULL, ai, "byval", zan_params[i]);
                abi_add_int_attr(g, c_fn, NULL, ai, "align",
                                 abi_align_of(zan_params[i]) < 8
                                     ? 8 : abi_align_of(zan_params[i]));
            }
            if (s->stack_align)
                abi_add_int_attr(g, c_fn, NULL, ai, "alignstack", s->stack_align);
            ai++;
        }
    }

    /* --- the thunk --- */
    char tname[320];
    snprintf(tname, sizeof(tname), "zan.abi.%s", name);
    LLVMValueRef thunk = LLVMGetNamedFunction(g->mod, tname);
    if (thunk) {
        free(arg_slots); free(c_params); free(c_args); free(zan_params);
        return thunk;
    }
    thunk = LLVMAddFunction(g->mod, tname, zan_ft);
    LLVMSetLinkage(thunk, LLVMInternalLinkage);

    LLVMBasicBlockRef saved = LLVMGetInsertBlock(g->builder);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, thunk, "entry");
    LLVMPositionBuilderAtEnd(g->builder, entry);

    unsigned an = 0;
    LLVMValueRef ret_tmp = NULL;
    if (sret) {
        ret_tmp = LLVMBuildAlloca(g->builder, zan_ret, "abi.sret");
        LLVMSetAlignment(ret_tmp, (unsigned)abi_align_of(zan_ret));
        c_args[an++] = ret_tmp;
    }
    for (unsigned i = 0; i < pc; i++) {
        abi_slot_t *s = &arg_slots[i];
        LLVMValueRef v = LLVMGetParam(thunk, i);
        if (s->kind == ABI_SLOT_DIRECT) { c_args[an++] = v; continue; }
        LLVMValueRef tmp = LLVMBuildAlloca(g->builder, zan_params[i], "abi.arg");
        unsigned al = (unsigned)abi_align_of(zan_params[i]);
        LLVMSetAlignment(tmp, al < 8 ? 8 : al);
        LLVMBuildStore(g->builder, v, tmp);
        if (s->kind == ABI_SLOT_INDIRECT) { c_args[an++] = tmp; continue; }
        for (int k = 0; k < s->nparts; k++) {
            LLVMValueRef p = abi_byte_ptr(g, tmp, (unsigned long)k * 8);
            c_args[an++] = LLVMBuildLoad2(g->builder, s->parts[k], p, "abi.part");
        }
    }

    LLVMValueRef call = LLVMBuildCall2(g->builder, c_ft, c_fn, c_args, an,
                                       LLVMGetTypeKind(c_ret) == LLVMVoidTypeKind
                                           ? "" : "abi.call");
    if (sret) {
        abi_add_type_attr(g, NULL, call, 1, "sret", zan_ret);
        abi_add_int_attr(g, NULL, call, 1, "align", abi_align_of(zan_ret));
    }
    {
        unsigned ai = sret ? 2 : 1;
        for (unsigned i = 0; i < pc; i++) {
            abi_slot_t *s = &arg_slots[i];
            if (s->kind == ABI_SLOT_COERCE) {
                if (s->stack_align)
                    abi_add_int_attr(g, NULL, call, ai, "alignstack",
                                     s->stack_align);
                ai += (unsigned)s->nparts;
                continue;
            }
            if (s->kind == ABI_SLOT_INDIRECT && s->byval) {
                abi_add_type_attr(g, NULL, call, ai, "byval", zan_params[i]);
                abi_add_int_attr(g, NULL, call, ai, "align",
                                 abi_align_of(zan_params[i]) < 8
                                     ? 8 : abi_align_of(zan_params[i]));
            }
            if (s->stack_align)
                abi_add_int_attr(g, NULL, call, ai, "alignstack", s->stack_align);
            ai++;
        }
    }

    if (LLVMGetTypeKind(zan_ret) == LLVMVoidTypeKind) {
        LLVMBuildRetVoid(g->builder);
    } else if (sret) {
        LLVMBuildRet(g->builder,
                     LLVMBuildLoad2(g->builder, zan_ret, ret_tmp, "abi.ret"));
    } else if (ret_slot.kind == ABI_SLOT_COERCE) {
        LLVMValueRef tmp = LLVMBuildAlloca(g->builder, zan_ret, "abi.rettmp");
        unsigned al = (unsigned)abi_align_of(zan_ret);
        LLVMSetAlignment(tmp, al < 8 ? 8 : al);
        for (int k = 0; k < ret_slot.nparts; k++) {
            LLVMValueRef part = ret_slot.nparts == 1
                ? call
                : LLVMBuildExtractValue(g->builder, call, (unsigned)k, "abi.rp");
            LLVMBuildStore(g->builder, part,
                           abi_byte_ptr(g, tmp, (unsigned long)k * 8));
        }
        LLVMBuildRet(g->builder,
                     LLVMBuildLoad2(g->builder, zan_ret, tmp, "abi.ret"));
    } else {
        LLVMBuildRet(g->builder, call);
    }

    if (saved) LLVMPositionBuilderAtEnd(g->builder, saved);
    free(arg_slots); free(c_params); free(c_args); free(zan_params);
    return thunk;
}
