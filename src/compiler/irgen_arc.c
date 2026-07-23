/* irgen_arc.c -- ARC (automatic reference counting) for heap class objects, the
 * object-graph release destructors and virtual dispatch (vtables).
 *
 * Part of the irgen translation unit: this file is #include'd by irgen.c
 * (in a fixed order) and must not be compiled standalone. Splitting keeps
 * the single-TU static linkage while keeping each concern in its own file.
 */

/* ===== ARC (automatic reference counting) for heap class objects ===========
 * Only TYPE_CLASS instances are heap-allocated with the 16-byte rc header
 * (zan_rt_alloc); struct values live on the stack and List/Dict/StringBuilder
 * are raw-malloc'd without a header, so retain/release apply strictly to class
 * pointers. Ownership model (classifier-based, no temp pool):
 *   - `new C(...)` and calls yield an already-owned (+1) reference;
 *   - identifier / member / index loads yield a borrowed reference.
 * A borrowed reference is retained when captured into an owning class slot
 * (local or field) or returned; owning class locals are released when
 * overwritten and at every function exit. A class local passed as a call
 * argument is conservatively treated as escaped (ownership handed off) and not
 * released, which keeps collections/stores that capture it use-after-free
 * safe. Strings, async frames and collection element release are follow-ups. */

static void emit_arc_retain(zan_irgen_t *g, LLVMValueRef v) {
    if (!v || LLVMGetTypeKind(LLVMTypeOf(v)) != LLVMPointerTypeKind) return;
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    if (LLVMTypeOf(v) != i8ptr) v = LLVMBuildBitCast(g->builder, v, i8ptr, "arc.rt");
    zan_call2(g->builder,
        LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), &i8ptr, 1, 0),
        g->rt_retain, &v, 1, "");
}

static void emit_arc_release(zan_irgen_t *g, LLVMValueRef v) {
    if (!v || LLVMGetTypeKind(LLVMTypeOf(v)) != LLVMPointerTypeKind) return;
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    if (LLVMTypeOf(v) != i8ptr) v = LLVMBuildBitCast(g->builder, v, i8ptr, "arc.rl");
    zan_call2(g->builder,
        LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), &i8ptr, 1, 0),
        g->rt_release, &v, 1, "");
}

static void emit_string_retain(zan_irgen_t *g, LLVMValueRef v) {
    if (!v || LLVMGetTypeKind(LLVMTypeOf(v)) != LLVMPointerTypeKind) return;
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    if (LLVMTypeOf(v) != i8ptr) v = LLVMBuildBitCast(g->builder, v, i8ptr, "str.rt");
    zan_call2(g->builder,
        LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), &i8ptr, 1, 0),
        g->rt_str_retain, &v, 1, "");
}

static void emit_string_release(zan_irgen_t *g, LLVMValueRef v) {
    if (!v || LLVMGetTypeKind(LLVMTypeOf(v)) != LLVMPointerTypeKind) return;
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    if (LLVMTypeOf(v) != i8ptr) v = LLVMBuildBitCast(g->builder, v, i8ptr, "str.rl");
    zan_call2(g->builder,
        LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), &i8ptr, 1, 0),
        g->rt_str_release, &v, 1, "");
}

static void emit_rc_retain_for_type(zan_irgen_t *g, zan_type_t *type, LLVMValueRef v) {
    if (!type) return;
    if (type->kind == TYPE_STRING) {
        emit_string_retain(g, v);
    } else if (is_arc_managed_type(type)) {
        emit_arc_retain(g, v);
    }
}

static void emit_rc_release_for_type(zan_irgen_t *g, zan_type_t *type, LLVMValueRef v) {
    if (!type) return;
    if (type->kind == TYPE_STRING) {
        emit_string_release(g, v);
    } else if (is_arc_managed_type(type)) {
        emit_arc_release_typed(g, type, v);
    }
}

/* ---- object-graph release (per-class destructors) ------------------------
 * User class instances carry a 16-byte rc header; their RC-managed fields
 * (strings, other class instances, and the elements held by a List field) are
 * retained on capture but were never released when the owning object died,
 * leaking the whole object graph. For each class we synthesise
 *   void __zan_release_<T>(i8* obj):
 *     if (obj == null) return;
 *     if (*refcount == 1)          // this release drops the last reference
 *         <release each RC-managed field>;
 *     zan_rt_release(obj);         // decrement + free (+ leak bookkeeping)
 * Peeking the refcount keeps field release aliasing-safe: fields are dropped
 * exactly once, on the release that brings the object to zero. */

static LLVMValueRef get_class_release_decl(zan_irgen_t *g, zan_symbol_t *sym) {
    if (!sym) return NULL;
    for (int i = 0; i < g->class_release_count; i++)
        if (g->class_release[i].sym == sym) return g->class_release[i].fn;
    if (g->class_release_count >= 256) return NULL;
    /* only classes with a registered struct layout can be walked */
    if (!get_struct_llvm_type(g, sym)) return NULL;
    char name[320];
    snprintf(name, sizeof(name), "__zan_release_%.*s_%d",
             (int)sym->name.len, sym->name.str, g->class_release_count);
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef ft = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), &i8ptr, 1, 0);
    LLVMValueRef fn = LLVMAddFunction(g->mod, name, ft);
    LLVMSetLinkage(fn, LLVMInternalLinkage);
    g->class_release[g->class_release_count].sym = sym;
    g->class_release[g->class_release_count].fn = fn;
    g->class_release_count++;
    return fn;
}

/* Release the RC-managed elements held by a List<T> value `col` (an i8* to the
 * bare List struct { i64 count, i64 cap, i64* data }). No-op for null lists or
 * non-RC element types. Only the tracked elements are released; the header-less
 * List struct/buffer are not rc-counted and are left as-is. */
static void emit_list_release_elems(zan_irgen_t *g, zan_type_t *elem_type, LLVMValueRef col) {
    if (!elem_type || !is_rc_managed_type(elem_type)) return;
    if (!col || LLVMGetTypeKind(LLVMTypeOf(col)) != LLVMPointerTypeKind) return;
    LLVMContextRef c = g->ctx;
    LLVMBuilderRef b = g->builder;
    LLVMTypeRef i64 = LLVMInt64TypeInContext(c);
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(c), 0);
    LLVMValueRef fn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(b));
    LLVMBasicBlockRef chk  = LLVMAppendBasicBlockInContext(c, fn, "lc.chk");
    LLVMBasicBlockRef head = LLVMAppendBasicBlockInContext(c, fn, "lc.head");
    LLVMBasicBlockRef body = LLVMAppendBasicBlockInContext(c, fn, "lc.body");
    LLVMBasicBlockRef done = LLVMAppendBasicBlockInContext(c, fn, "lc.done");
    LLVMValueRef c8 = (LLVMTypeOf(col) == i8ptr) ? col
                      : LLVMBuildBitCast(b, col, i8ptr, "lc.c8");
    LLVMValueRef isn = LLVMBuildICmp(b, LLVMIntEQ, c8, LLVMConstNull(i8ptr), "lc.isn");
    LLVMBuildCondBr(b, isn, done, chk);
    LLVMPositionBuilderAtEnd(b, chk);
    LLVMValueRef lp = LLVMBuildBitCast(b, c8, LLVMPointerType(g->list_struct_type, 0), "lp");
    LLVMValueRef cntp = LLVMBuildStructGEP2(b, g->list_struct_type, lp, 0, "cntp");
    LLVMValueRef cnt = LLVMBuildLoad2(b, i64, cntp, "cnt");
    LLVMValueRef datap = LLVMBuildStructGEP2(b, g->list_struct_type, lp, 2, "datap");
    LLVMValueRef data = LLVMBuildLoad2(b, LLVMPointerType(i64, 0), datap, "data");
    LLVMBuildBr(b, head);
    LLVMPositionBuilderAtEnd(b, head);
    LLVMValueRef iphi = LLVMBuildPhi(b, i64, "i");
    LLVMValueRef lt = LLVMBuildICmp(b, LLVMIntSLT, iphi, cnt, "lt");
    LLVMBuildCondBr(b, lt, body, done);
    LLVMPositionBuilderAtEnd(b, body);
    LLVMValueRef slot = LLVMBuildGEP2(b, i64, data, &iphi, 1, "slot");
    LLVMValueRef raw = LLVMBuildLoad2(b, i64, slot, "raw");
    emit_collection_release_raw_slot(g, elem_type, raw, i64);
    LLVMValueRef inext = LLVMBuildAdd(b, iphi, LLVMConstInt(i64, 1, 0), "inext");
    LLVMBasicBlockRef body_end = LLVMGetInsertBlock(b);
    LLVMBuildBr(b, head);
    LLVMValueRef vals[2] = { LLVMConstInt(i64, 0, 0), inext };
    LLVMBasicBlockRef blks[2] = { chk, body_end };
    LLVMAddIncoming(iphi, vals, blks, 2);
    LLVMPositionBuilderAtEnd(b, done);
}

/* Release the rc-managed elements of a bare `new T[n]` array buffer. Arrays
 * carry no length header, so the count is threaded in explicitly (captured at
 * the declaration). Null buffer is a no-op. */
static void emit_array_release_elems(zan_irgen_t *g, zan_type_t *elem_type,
                                     LLVMValueRef arr, LLVMValueRef len) {
    if (!elem_type || !is_rc_managed_type(elem_type)) return;
    if (!arr || LLVMGetTypeKind(LLVMTypeOf(arr)) != LLVMPointerTypeKind) return;
    if (!len) return;
    LLVMContextRef c = g->ctx;
    LLVMBuilderRef b = g->builder;
    LLVMTypeRef i64 = LLVMInt64TypeInContext(c);
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(c), 0);
    LLVMTypeRef elem_llvm = map_type(g, elem_type);
    LLVMValueRef fn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(b));
    LLVMBasicBlockRef chk  = LLVMAppendBasicBlockInContext(c, fn, "ac.chk");
    LLVMBasicBlockRef head = LLVMAppendBasicBlockInContext(c, fn, "ac.head");
    LLVMBasicBlockRef body = LLVMAppendBasicBlockInContext(c, fn, "ac.body");
    LLVMBasicBlockRef done = LLVMAppendBasicBlockInContext(c, fn, "ac.done");
    LLVMValueRef a8 = (LLVMTypeOf(arr) == i8ptr) ? arr
                      : LLVMBuildBitCast(b, arr, i8ptr, "ac.a8");
    LLVMValueRef isn = LLVMBuildICmp(b, LLVMIntEQ, a8, LLVMConstNull(i8ptr), "ac.isn");
    LLVMBuildCondBr(b, isn, done, chk);
    LLVMPositionBuilderAtEnd(b, chk);
    LLVMValueRef typed = LLVMBuildBitCast(b, a8, LLVMPointerType(elem_llvm, 0), "ac.tp");
    LLVMValueRef n = len;
    if (LLVMGetTypeKind(LLVMTypeOf(n)) == LLVMIntegerTypeKind &&
        LLVMGetIntTypeWidth(LLVMTypeOf(n)) < 64)
        n = LLVMBuildSExt(b, n, i64, "ac.n");
    LLVMBuildBr(b, head);
    LLVMPositionBuilderAtEnd(b, head);
    LLVMValueRef iphi = LLVMBuildPhi(b, i64, "i");
    LLVMValueRef lt = LLVMBuildICmp(b, LLVMIntSLT, iphi, n, "ac.lt");
    LLVMBuildCondBr(b, lt, body, done);
    LLVMPositionBuilderAtEnd(b, body);
    LLVMValueRef slot = LLVMBuildGEP2(b, elem_llvm, typed, &iphi, 1, "ac.slot");
    LLVMValueRef elem = LLVMBuildLoad2(b, elem_llvm, slot, "ac.elem");
    emit_rc_release_for_type(g, elem_type, elem);
    LLVMValueRef inext = LLVMBuildAdd(b, iphi, LLVMConstInt(i64, 1, 0), "ac.inext");
    LLVMBasicBlockRef body_end = LLVMGetInsertBlock(b);
    LLVMBuildBr(b, head);
    LLVMValueRef vals[2] = { LLVMConstInt(i64, 0, 0), inext };
    LLVMBasicBlockRef blks[2] = { chk, body_end };
    LLVMAddIncoming(iphi, vals, blks, 2);
    LLVMPositionBuilderAtEnd(b, done);
}

/* Emit the body of __zan_release_<T>: null-guard, peek the refcount, release the
 * RC-managed fields when it is about to hit zero, then hand off to
 * zan_rt_release for the decrement + free. */
static void build_class_release_body(zan_irgen_t *g, zan_symbol_t *sym, LLVMValueRef fn) {
    di_clear(g); /* synthetic fn: don't inherit a user fn's DISubprogram scope */
    LLVMContextRef c = g->ctx;
    LLVMBuilderRef b = g->builder;
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(c), 0);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(c);
    LLVMTypeRef structT = get_struct_llvm_type(g, sym);
    LLVMValueRef obj = LLVMGetParam(fn, 0);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(c, fn, "entry");
    LLVMBasicBlockRef cont  = LLVMAppendBasicBlockInContext(c, fn, "cont");
    LLVMBasicBlockRef relf  = LLVMAppendBasicBlockInContext(c, fn, "relf");
    LLVMBasicBlockRef dorel = LLVMAppendBasicBlockInContext(c, fn, "dorel");
    LLVMBasicBlockRef ret   = LLVMAppendBasicBlockInContext(c, fn, "ret");
    LLVMPositionBuilderAtEnd(b, entry);
    LLVMValueRef isnull = LLVMBuildICmp(b, LLVMIntEQ, obj, LLVMConstNull(i8ptr), "isnull");
    LLVMBuildCondBr(b, isnull, ret, cont);
    LLVMPositionBuilderAtEnd(b, cont);
    LLVMValueRef neg16 = LLVMConstInt(i64, (unsigned long long)-16, 1);
    LLVMValueRef rcp = LLVMBuildGEP2(b, LLVMInt8TypeInContext(c), obj, &neg16, 1, "rcp");
    LLVMValueRef rcip = LLVMBuildBitCast(b, rcp, LLVMPointerType(i64, 0), "rcip");
    LLVMValueRef rc = LLVMBuildLoad2(b, i64, rcip, "rc");
    LLVMValueRef is1 = LLVMBuildICmp(b, LLVMIntEQ, rc, LLVMConstInt(i64, 1, 0), "is1");
    LLVMBuildCondBr(b, is1, relf, dorel);
    LLVMPositionBuilderAtEnd(b, relf);
    LLVMValueRef self = LLVMBuildBitCast(b, obj, LLVMPointerType(structT, 0), "self");
    int fi = class_vptr_offset(sym);
    for (int i = 0; i < sym->member_count; i++) {
        zan_symbol_t *m = sym->members[i];
        if (m->kind != SYM_FIELD && m->kind != SYM_PROPERTY) continue;
        if (field_member_is_static(m)) continue;
        int idx = fi++;
        zan_type_t *ft = m->type;
        if (!ft) continue;
        /* weak fields are non-owning back-references: never released here. */
        if (m->modifiers & MOD_WEAK) continue;
        if (ft->kind == TYPE_STRING) {
            LLVMValueRef fp = LLVMBuildStructGEP2(b, structT, self, (unsigned)idx, "fp");
            LLVMValueRef s = LLVMBuildLoad2(b, i8ptr, fp, "fs");
            emit_string_release(g, s);
        } else if (is_arc_managed_type(ft)) {
            /* User class instances and the refcounted collections List/
             * StringBuilder: release via the recorded site destructor (which
             * releases elements and frees the backing buffer + struct). */
            LLVMValueRef fp = LLVMBuildStructGEP2(b, structT, self, (unsigned)idx, "fp");
            LLVMValueRef cv = LLVMBuildLoad2(b, map_type(g, ft), fp, "fc");
            emit_arc_release_typed(g, ft, cv);
        }
    }
    LLVMBuildBr(b, dorel);
    LLVMPositionBuilderAtEnd(b, dorel);
    zan_call2(b, LLVMFunctionType(LLVMVoidTypeInContext(c), &i8ptr, 1, 0),
                   g->rt_release, &obj, 1, "");
    LLVMBuildBr(b, ret);
    LLVMPositionBuilderAtEnd(b, ret);
    LLVMBuildRetVoid(b);
}

/* Emit the body of a per-site collection destructor (List/StringBuilder):
 * null-guard, peek the refcount, and when this release brings it to zero
 * release the RC-managed elements (List) and free the separately-malloc'd
 * backing buffer, then hand off to zan_rt_release for the struct decrement +
 * free. Peeking the refcount keeps buffer release aliasing-safe. */
static void build_collection_release_body(zan_irgen_t *g, int coll_kind,
                                          zan_type_t *elem_type, LLVMValueRef fn) {
    di_clear(g); /* synthetic fn: don't inherit a user fn's DISubprogram scope */
    LLVMContextRef c = g->ctx;
    LLVMBuilderRef b = g->builder;
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(c), 0);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(c);
    LLVMValueRef obj = LLVMGetParam(fn, 0);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(c, fn, "entry");
    LLVMBasicBlockRef cont  = LLVMAppendBasicBlockInContext(c, fn, "cont");
    LLVMBasicBlockRef relf  = LLVMAppendBasicBlockInContext(c, fn, "relf");
    LLVMBasicBlockRef dorel = LLVMAppendBasicBlockInContext(c, fn, "dorel");
    LLVMBasicBlockRef ret   = LLVMAppendBasicBlockInContext(c, fn, "ret");
    LLVMPositionBuilderAtEnd(b, entry);
    LLVMValueRef isnull = LLVMBuildICmp(b, LLVMIntEQ, obj, LLVMConstNull(i8ptr), "isnull");
    LLVMBuildCondBr(b, isnull, ret, cont);
    LLVMPositionBuilderAtEnd(b, cont);
    LLVMValueRef neg16 = LLVMConstInt(i64, (unsigned long long)-16, 1);
    LLVMValueRef rcp = LLVMBuildGEP2(b, LLVMInt8TypeInContext(c), obj, &neg16, 1, "rcp");
    LLVMValueRef rcip = LLVMBuildBitCast(b, rcp, LLVMPointerType(i64, 0), "rcip");
    LLVMValueRef rc = LLVMBuildLoad2(b, i64, rcip, "rc");
    LLVMValueRef is1 = LLVMBuildICmp(b, LLVMIntEQ, rc, LLVMConstInt(i64, 1, 0), "is1");
    LLVMBuildCondBr(b, is1, relf, dorel);
    LLVMPositionBuilderAtEnd(b, relf);
    LLVMTypeRef free_ty = LLVMFunctionType(LLVMVoidTypeInContext(c), &i8ptr, 1, 0);
    if (coll_kind == 1) {
        /* List: release RC-managed elements, then free the i64* data buffer.
         * emit_list_release_elems may split the block and leaves the builder at
         * its own terminator block; capture the data pointer beforehand. */
        LLVMValueRef lp = LLVMBuildBitCast(b, obj, LLVMPointerType(g->list_struct_type, 0), "lp");
        LLVMValueRef dp = LLVMBuildStructGEP2(b, g->list_struct_type, lp, 2, "dp");
        LLVMValueRef data = LLVMBuildLoad2(b, LLVMPointerType(i64, 0), dp, "data");
        emit_list_release_elems(g, elem_type, obj);
        LLVMValueRef d8 = LLVMBuildBitCast(b, data, i8ptr, "d8");
        zan_call2(b, free_ty, g->fn_free, &d8, 1, "");
    } else if (coll_kind == 2) {
        /* StringBuilder: free the i8* data buffer (free tolerates null). */
        LLVMValueRef sp = LLVMBuildBitCast(b, obj, LLVMPointerType(g->sb_struct_type, 0), "sp");
        LLVMValueRef dp = LLVMBuildStructGEP2(b, g->sb_struct_type, sp, 2, "dp");
        LLVMValueRef data = LLVMBuildLoad2(b, i8ptr, dp, "data");
        zan_call2(b, free_ty, g->fn_free, &data, 1, "");
    }
    LLVMBuildBr(b, dorel);
    LLVMPositionBuilderAtEnd(b, dorel);
    zan_call2(b, LLVMFunctionType(LLVMVoidTypeInContext(c), &i8ptr, 1, 0),
                   g->rt_release, &obj, 1, "");
    LLVMBuildBr(b, ret);
    LLVMPositionBuilderAtEnd(b, ret);
    LLVMBuildRetVoid(b);
}

/* Get (creating on first use) the per-site collection destructor for a List/
 * StringBuilder allocation site, building its body immediately. */
static LLVMValueRef get_collection_release_decl(zan_irgen_t *g, int site) {
    char name[64];
    snprintf(name, sizeof(name), "__zan_release_coll_%d", site);
    LLVMValueRef existing = LLVMGetNamedFunction(g->mod, name);
    if (existing) return existing;
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef ft = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), &i8ptr, 1, 0);
    LLVMValueRef fn = LLVMAddFunction(g->mod, name, ft);
    LLVMSetLinkage(fn, LLVMInternalLinkage);
    build_collection_release_body(g, g->site_coll[site], g->site_coll_elem[site], fn);
    return fn;
}

/* Release a class value `v` of static type `type` via its synthesised
 * __zan_release_<T> (which releases RC fields then frees). Falls back to the
 * plain rc decrement when no per-class function exists (unregistered type). */
static void emit_arc_release_typed(zan_irgen_t *g, zan_type_t *type, LLVMValueRef v) {
    (void)type;
    if (!v || LLVMGetTypeKind(LLVMTypeOf(v)) != LLVMPointerTypeKind) return;
    /* Dispatch on the object's recorded allocation site (its concrete type)
     * rather than the static type, so a base-typed reference still frees the
     * derived instance's fields. zan_rt_release_dyn falls back to a plain
     * decrement when the site has no per-class destructor. */
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    if (LLVMTypeOf(v) != i8ptr) v = LLVMBuildBitCast(g->builder, v, i8ptr, "arc.rlt");
    zan_call2(g->builder, LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), &i8ptr, 1, 0),
                   g->rt_release_dyn, &v, 1, "");
}

/* Build the bodies of every class release function. Called at finalize, once
 * all class types are registered. Iterating the registered struct types covers
 * every function get_class_release_decl may lazily create for nested fields. */
static void emit_all_class_releases(zan_irgen_t *g) {
    for (int i = 0; i < g->struct_type_count; i++) {
        zan_symbol_t *sym = g->struct_types[i].sym;
        if (!sym || !sym->type || !is_arc_managed_type(sym->type)) continue;
        LLVMValueRef fn = get_class_release_decl(g, sym);
        if (fn) build_class_release_body(g, sym, fn);
    }
}

/* Fill __zan_site_dtors[site] with the concrete per-class destructor recorded
 * for that allocation site, so zan_rt_release_dyn can dispatch on runtime
 * type. Must run after emit_all_class_releases (all destructors declared). */
static void emit_site_dtor_table(zan_irgen_t *g) {
    if (!g->site_syms) return;
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    int n = ZAN_MAX_LEAK_SITES;
    LLVMValueRef *elems = (LLVMValueRef *)calloc((size_t)n, sizeof(LLVMValueRef));
    for (int i = 0; i < n; i++) {
        LLVMValueRef e = LLVMConstNull(i8ptr);
        if (i < g->leak_site_count && g->site_coll && g->site_coll[i]) {
            LLVMValueRef fn = get_collection_release_decl(g, i);
            if (fn) e = LLVMConstBitCast(fn, i8ptr);
        } else if (i < g->leak_site_count && g->site_syms[i]) {
            LLVMValueRef fn = get_class_release_decl(g, g->site_syms[i]);
            if (fn) e = LLVMConstBitCast(fn, i8ptr);
        }
        elems[i] = e;
    }
    LLVMSetInitializer(g->g_site_dtors, LLVMConstArray(i8ptr, elems, (unsigned)n));
    free(elems);
}

/* ---- virtual dispatch: vtable globals + dynamic call ---------------------
 * Each class with virtual/override methods gets an internal global
 * __zan_vtable_<Class> : [N x i8*], one slot per virtual method (base-first
 * ordering shared across the hierarchy). Objects store &vtable[0] in field 0
 * at construction; a virtual call loads the slot and calls through it, so the
 * runtime (most-derived) implementation runs even via a base-typed reference
 * or a List<Base> element. */

static LLVMValueRef find_fn_for_sym(zan_irgen_t *g, zan_symbol_t *msym) {
    if (!msym) return NULL;
    for (int i = 0; i < g->function_count; i++)
        if (g->functions[i].sym == msym) return g->functions[i].fn;
    return NULL;
}

/* Enumerate the virtual *slot-defining* methods (MOD_VIRTUAL and not an
 * override) of a class hierarchy, base classes first, matching the slot
 * numbering used by get_virtual_method_index/count_virtual_methods. */
static void collect_vslot_decls(zan_symbol_t *sym, zan_symbol_t **out, int *n, int cap) {
    if (sym->type && sym->type->base_type && sym->type->base_type->sym)
        collect_vslot_decls(sym->type->base_type->sym, out, n, cap);
    for (int i = 0; i < sym->member_count; i++) {
        zan_symbol_t *m = sym->members[i];
        if (m->kind == SYM_METHOD &&
            (m->modifiers & MOD_VIRTUAL) && !(m->modifiers & MOD_OVERRIDE)) {
            if (*n < cap) out[(*n)++] = m;
        }
    }
}

static LLVMValueRef get_vtable_global(zan_irgen_t *g, zan_symbol_t *sym) {
    char name[320];
    snprintf(name, sizeof(name), "__zan_vtable_%.*s",
             (int)sym->name.len, sym->name.str);
    LLVMValueRef gv = LLVMGetNamedGlobal(g->mod, name);
    if (gv) return gv;
    int n = count_virtual_methods(sym);
    if (n < 1) n = 1;
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef at = LLVMArrayType(i8ptr, (unsigned)n);
    gv = LLVMAddGlobal(g->mod, at, name);
    LLVMSetInitializer(gv, LLVMConstNull(at));
    LLVMSetLinkage(gv, LLVMInternalLinkage);
    return gv;
}

/* Populate every instantiable class's vtable with the most-derived function
 * for each slot. Runs at finalize, after all methods are registered. */
static void emit_vtables(zan_irgen_t *g) {
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    for (int i = 0; i < g->struct_type_count; i++) {
        zan_symbol_t *sym = g->struct_types[i].sym;
        if (!sym || !class_has_virtual_methods(sym)) continue;
        int n = count_virtual_methods(sym);
        if (n < 1) continue;
        zan_symbol_t *decls[128];
        int nd = 0;
        collect_vslot_decls(sym, decls, &nd, 128);
        LLVMValueRef *elems = (LLVMValueRef *)calloc((size_t)n, sizeof(LLVMValueRef));
        for (int s = 0; s < n; s++) {
            LLVMValueRef e = LLVMConstNull(i8ptr);
            if (s < nd) {
                zan_symbol_t *decl = decls[s];
                int arity = decl->decl ? decl->decl->method_decl.params.count : 0;
                zan_symbol_t *impl = resolve_overload(sym, decl->name, arity);
                LLVMValueRef fn = find_fn_for_sym(g, impl);
                if (fn) e = LLVMConstBitCast(fn, i8ptr);
            }
            elems[s] = e;
        }
        LLVMValueRef gv = get_vtable_global(g, sym);
        LLVMSetInitializer(gv, LLVMConstArray(i8ptr, elems, (unsigned)n));
        free(elems);
    }
}

/* Reinterpret/convert a value to a target LLVM type across the generic
 * erased-pointer boundary. A generic type parameter T lowers to an opaque
 * pointer, so a value-type argument (i64/double/i1) passed where T is expected
 * — and the reverse, a T-typed field/return consumed as a concrete value —
 * must be bit-reinterpreted (mirrors the List<T> slot store/load). Matching
 * types pass through unchanged, so non-generic code is never affected. */
static LLVMValueRef coerce_int_to(zan_irgen_t *g, LLVMValueRef v, LLVMTypeRef target);

static LLVMValueRef emit_boundary_coerce(zan_irgen_t *g, LLVMValueRef v,
                                         LLVMTypeRef target) {
    if (!v || !target) return v;
    LLVMTypeRef vt = LLVMTypeOf(v);
    if (vt == target) return v;
    LLVMTypeKind vk = LLVMGetTypeKind(vt);
    LLVMTypeKind tk = LLVMGetTypeKind(target);
    LLVMContextRef c = g->ctx;
    LLVMTypeRef i32 = LLVMInt32TypeInContext(c);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(c);

    if (tk == LLVMPointerTypeKind) {
        if (vk == LLVMPointerTypeKind)
            return LLVMBuildBitCast(g->builder, v, target, "bc.pp");
        if (vk == LLVMIntegerTypeKind) {
            if (LLVMGetIntTypeWidth(vt) < 64)
                v = LLVMBuildSExt(g->builder, v, i64, "bc.ext");
            return LLVMBuildIntToPtr(g->builder, v, target, "bc.ip");
        }
        if (vk == LLVMFloatTypeKind) {
            LLVMValueRef iv = LLVMBuildBitCast(g->builder, v, i32, "bc.fi");
            return LLVMBuildIntToPtr(g->builder, iv, target, "bc.ip");
        }
        if (vk == LLVMDoubleTypeKind) {
            LLVMValueRef iv = LLVMBuildBitCast(g->builder, v, i64, "bc.di");
            return LLVMBuildIntToPtr(g->builder, iv, target, "bc.ip");
        }
    } else if (tk == LLVMIntegerTypeKind) {
        if (vk == LLVMPointerTypeKind)
            return LLVMBuildPtrToInt(g->builder, v, target, "bc.pi");
        if (vk == LLVMIntegerTypeKind)
            return coerce_int_to(g, v, target);
    } else if (tk == LLVMFloatTypeKind) {
        if (vk == LLVMPointerTypeKind) {
            LLVMValueRef iv = LLVMBuildPtrToInt(g->builder, v, i32, "bc.pi");
            return LLVMBuildBitCast(g->builder, iv, target, "bc.if");
        }
        if (vk == LLVMDoubleTypeKind)
            return LLVMBuildFPTrunc(g->builder, v, target, "bc.fptr");
    } else if (tk == LLVMDoubleTypeKind) {
        if (vk == LLVMPointerTypeKind) {
            LLVMValueRef iv = LLVMBuildPtrToInt(g->builder, v, i64, "bc.pi");
            return LLVMBuildBitCast(g->builder, iv, target, "bc.id");
        }
        if (vk == LLVMFloatTypeKind)
            return LLVMBuildFPExt(g->builder, v, target, "bc.fpext");
    }
    return v;
}

/* Coerce each argument to the callee's declared parameter type (generic
 * erased-pointer boundary). No-op when types already agree. */
static void coerce_args_to_params(zan_irgen_t *g, LLVMTypeRef fn_type,
                                  LLVMValueRef *call_args, int argc) {
    unsigned npt = LLVMCountParamTypes(fn_type);
    if (npt == 0 || argc <= 0) return;
    LLVMTypeRef *pts = (LLVMTypeRef *)calloc((size_t)npt, sizeof(LLVMTypeRef));
    LLVMGetParamTypes(fn_type, pts);
    int n = (argc < (int)npt) ? argc : (int)npt;
    for (int i = 0; i < n; i++)
        call_args[i] = emit_boundary_coerce(g, call_args[i], pts[i]);
    free(pts);
}

/* If `t` is a generic type parameter of `recv`'s instantiated class, resolve it
 * to the corresponding concrete type argument (e.g. T -> int for Box<int>);
 * otherwise return `t` unchanged. */
static zan_type_t *subst_type_param(zan_type_t *t, zan_type_t *recv) {
    if (!t || t->kind != TYPE_TYPE_PARAM || !recv || !recv->sym) return t;
    zan_ast_node_t *decl = recv->sym->decl;
    if (!decl) return t;
    zan_ast_list_t *tps = &decl->type_decl.type_params;
    for (int i = 0; i < tps->count && i < recv->type_arg_count; i++) {
        zan_ast_node_t *tp = tps->items[i];
        if (tp->kind != AST_IDENTIFIER) continue;
        if (tp->ident.name.len == t->name.len &&
            memcmp(tp->ident.name.str, t->name.str, (size_t)t->name.len) == 0)
            return recv->type_args[i];
    }
    return t;
}

/* For a call to a generic method (one declaring its own <T,...>), determine the
 * concrete return type at this call site when the declared return type is one
 * of those type parameters. Uses explicit type arguments (f<int>(...)) when
 * present, otherwise infers from the argument bound to that type parameter.
 * Returns NULL when the method is non-generic or its return type is not a bare
 * type parameter (e.g. bool / List<T>), in which case no boundary coercion of
 * the erased-pointer result is required. */
static zan_type_t *generic_method_ret(zan_irgen_t *g, zan_symbol_t *msym,
                                      zan_ast_node_t *call, local_scope_t *locals) {
    if (!msym || !msym->decl || msym->decl->kind != AST_METHOD_DECL) return NULL;
    if (!call || call->kind != AST_CALL) return NULL;
    zan_ast_list_t *tps = &msym->decl->method_decl.type_params;
    if (tps->count == 0) return NULL;
    zan_ast_node_t *ret_ref = msym->decl->method_decl.return_type;
    if (!ret_ref || ret_ref->kind != AST_TYPE_REF) return NULL;
    zan_istr_t rn = ret_ref->type_ref.name;
    int which = -1;
    for (int i = 0; i < tps->count; i++) {
        zan_istr_t tn = tps->items[i]->ident.name;
        if (tn.len == rn.len && memcmp(tn.str, rn.str, (size_t)rn.len) == 0) {
            which = i;
            break;
        }
    }
    if (which < 0) return NULL;
    if (call->call.type_args.count > which)
        return zan_binder_resolve_type(g->binder, call->call.type_args.items[which]);
    zan_ast_list_t *params = &msym->decl->method_decl.params;
    for (int j = 0; j < params->count && j < call->call.args.count; j++) {
        zan_ast_node_t *pref = params->items[j]->param.type;
        if (pref && pref->kind == AST_TYPE_REF &&
            pref->type_ref.name.len == rn.len &&
            memcmp(pref->type_ref.name.str, rn.str, (size_t)rn.len) == 0)
            return infer_expr_type(g, call->call.args.items[j], locals);
    }
    return NULL;
}

/* Structural equality of two resolved types, used to match a call site's
 * concrete type arguments against a discovered instantiation. Compares kind +
 * simple name + (recursively) generic type arguments; arrays/nullable compare
 * their element type. Good enough for the closed set of types that appear as
 * generic arguments (builtins, user classes/structs, nested generics). */
static bool types_equal(zan_type_t *a, zan_type_t *b) {
    if (a == b) return true;
    if (!a || !b) return false;
    if (a->kind != b->kind) return false;
    if (a->name.len != b->name.len ||
        (a->name.len && memcmp(a->name.str, b->name.str, (size_t)a->name.len) != 0))
        return false;
    if (a->kind == TYPE_ARRAY || a->kind == TYPE_NULLABLE)
        return types_equal(a->element_type, b->element_type);
    if (a->type_arg_count != b->type_arg_count) return false;
    for (int i = 0; i < a->type_arg_count; i++)
        if (!types_equal(a->type_args[i], b->type_args[i])) return false;
    return true;
}

static bool type_arglists_equal(zan_type_t **a, int an, zan_type_t **b, int bn) {
    if (an != bn) return false;
    for (int i = 0; i < an; i++)
        if (!types_equal(a[i], b[i])) return false;
    return true;
}

/* True when `t` mentions no unresolved generic type parameter (directly or in a
 * type argument / element type) — i.e. it is a fully concrete instantiation. */
static bool type_is_concrete(zan_type_t *t) {
    if (!t) return false;
    if (t->kind == TYPE_TYPE_PARAM || t->kind == TYPE_ERROR) return false;
    if (t->kind == TYPE_ARRAY || t->kind == TYPE_NULLABLE)
        return type_is_concrete(t->element_type);
    for (int i = 0; i < t->type_arg_count; i++)
        if (!type_is_concrete(t->type_args[i])) return false;
    return true;
}

/* Substitute a type parameter to its concrete argument using the instantiation
 * currently being specialized (`g->cur_inst`); no-op when not specializing or
 * when `t` is not one of this instantiation's parameters. */
static zan_type_t *concretize(zan_irgen_t *g, zan_type_t *t) {
    if (!g->cur_inst) return t;
    return subst_type_param(t, g->cur_inst);
}

/* A user generic class/struct is one declaring at least one type parameter and
 * not a built-in intrinsic (List/Dict/StringBuilder handled elsewhere). */
static bool is_user_generic_sym(zan_symbol_t *sym) {
    if (!sym || !sym->decl) return false;
    zan_ast_node_t *d = sym->decl;
    if (d->kind != AST_CLASS_DECL && d->kind != AST_STRUCT_DECL) return false;
    return d->type_decl.type_params.count > 0;
}

static void add_generic_fn(zan_irgen_t *g, zan_symbol_t *msym,
                           zan_type_t **args, int argc,
                           LLVMValueRef fn, LLVMTypeRef fn_type) {
    if (g->generic_fn_count >= g->generic_fn_cap) {
        int ncap = g->generic_fn_cap ? g->generic_fn_cap * 2 : 64;
        g->generic_fns = realloc(g->generic_fns,
                                 (size_t)ncap * sizeof(*g->generic_fns));
        g->generic_fn_cap = ncap;
    }
    g->generic_fns[g->generic_fn_count].msym = msym;
    g->generic_fns[g->generic_fn_count].args = args;
    g->generic_fns[g->generic_fn_count].argc = argc;
    g->generic_fns[g->generic_fn_count].fn = fn;
    g->generic_fns[g->generic_fn_count].fn_type = fn_type;
    g->generic_fn_count++;
}

/* Find the specialized function for `msym` at the instantiation carrying
 * `args`; NULL when none was emitted (caller falls back to the erased fn). */
static LLVMValueRef find_generic_fn(zan_irgen_t *g, zan_symbol_t *msym,
                                    zan_type_t **args, int argc,
                                    LLVMTypeRef *out_fn_type) {
    if (!msym || argc <= 0 || !args) return NULL;
    for (int i = 0; i < g->generic_fn_count; i++) {
        if (g->generic_fns[i].msym == msym &&
            type_arglists_equal(g->generic_fns[i].args, g->generic_fns[i].argc,
                                args, argc)) {
            if (out_fn_type) *out_fn_type = g->generic_fns[i].fn_type;
            return g->generic_fns[i].fn;
        }
    }
    return NULL;
}

static void add_generic_ctor(zan_irgen_t *g, zan_symbol_t *type_sym,
                             zan_type_t **args, int argc, int param_count,
                             LLVMValueRef fn, LLVMTypeRef fn_type) {
    if (g->generic_ctor_count >= g->generic_ctor_cap) {
        int ncap = g->generic_ctor_cap ? g->generic_ctor_cap * 2 : 64;
        g->generic_ctors = realloc(g->generic_ctors,
                                   (size_t)ncap * sizeof(*g->generic_ctors));
        g->generic_ctor_cap = ncap;
    }
    g->generic_ctors[g->generic_ctor_count].type_sym = type_sym;
    g->generic_ctors[g->generic_ctor_count].args = args;
    g->generic_ctors[g->generic_ctor_count].argc = argc;
    g->generic_ctors[g->generic_ctor_count].param_count = param_count;
    g->generic_ctors[g->generic_ctor_count].fn = fn;
    g->generic_ctors[g->generic_ctor_count].fn_type = fn_type;
    g->generic_ctor_count++;
}

static LLVMValueRef find_generic_ctor(zan_irgen_t *g, zan_symbol_t *type_sym,
                                      zan_type_t **args, int argc,
                                      int param_count, LLVMTypeRef *out_fn_type) {
    if (!type_sym || argc <= 0 || !args) return NULL;
    for (int i = 0; i < g->generic_ctor_count; i++) {
        if (g->generic_ctors[i].type_sym == type_sym &&
            g->generic_ctors[i].param_count == param_count &&
            type_arglists_equal(g->generic_ctors[i].args, g->generic_ctors[i].argc,
                                args, argc)) {
            if (out_fn_type) *out_fn_type = g->generic_ctors[i].fn_type;
            return g->generic_ctors[i].fn;
        }
    }
    return NULL;
}

/* Record a distinct concrete instantiation of a user generic class. */
static void add_generic_inst(zan_irgen_t *g, zan_type_t *inst) {
    if (!inst || !inst->sym || inst->type_arg_count <= 0) return;
    if (!is_user_generic_sym(inst->sym)) return;
    if (!type_is_concrete(inst)) return;
    for (int i = 0; i < g->generic_inst_count; i++)
        if (g->generic_insts[i].type_sym == inst->sym &&
            type_arglists_equal(g->generic_insts[i].inst->type_args,
                                g->generic_insts[i].inst->type_arg_count,
                                inst->type_args, inst->type_arg_count))
            return; /* already recorded */
    if (g->generic_inst_count >= g->generic_inst_cap) {
        int ncap = g->generic_inst_cap ? g->generic_inst_cap * 2 : 32;
        g->generic_insts = realloc(g->generic_insts,
                                   (size_t)ncap * sizeof(*g->generic_insts));
        g->generic_inst_cap = ncap;
    }
    g->generic_insts[g->generic_inst_count].type_sym = inst->sym;
    g->generic_insts[g->generic_inst_count].inst = inst;
    g->generic_inst_count++;
}

/* Append a readable, LLVM-symbol-safe token for a concrete type argument
 * (e.g. `string`, `int`, or `List_string` for a nested generic). */
static void mangle_type_token(char *buf, size_t n, size_t *off, zan_type_t *t) {
    if (!t) return;
    if (t->kind == TYPE_ARRAY || t->kind == TYPE_NULLABLE) {
        mangle_type_token(buf, n, off, t->element_type);
        int w = snprintf(buf + (*off < n ? *off : n), (*off < n) ? n - *off : 0, "A");
        if (w > 0) *off += (size_t)w;
        return;
    }
    int w = snprintf(buf + (*off < n ? *off : n), (*off < n) ? n - *off : 0,
                     "%.*s", (int)t->name.len, t->name.str);
    if (w > 0) *off += (size_t)w;
    for (int i = 0; i < t->type_arg_count; i++) {
        w = snprintf(buf + (*off < n ? *off : n), (*off < n) ? n - *off : 0, "_");
        if (w > 0) *off += (size_t)w;
        mangle_type_token(buf, n, off, t->type_args[i]);
    }
}

/* Build the function-name suffix for an instantiation, e.g. HashSet<string>
 * yields "$string" and Pair<int,string> yields "$int$string". */
static void mangle_inst_suffix(char *buf, size_t n, zan_type_t *inst) {
    size_t off = 0;
    buf[0] = '\0';
    for (int i = 0; i < inst->type_arg_count; i++) {
        int w = snprintf(buf + (off < n ? off : n), (off < n) ? n - off : 0, "$");
        if (w > 0) off += (size_t)w;
        mangle_type_token(buf, n, &off, inst->type_args[i]);
    }
    if (off < n) buf[off] = '\0'; else buf[n - 1] = '\0';
}
