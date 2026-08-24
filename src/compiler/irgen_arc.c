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

static bool types_equal(zan_type_t *a, zan_type_t *b);
static bool type_is_concrete(zan_type_t *t);
static zan_type_t *subst_type_param_deep(zan_irgen_t *g, zan_type_t *t,
                                         zan_type_t *recv);

static void emit_arc_retain(zan_irgen_t *g, LLVMValueRef v) {
    if (g->current_fn_no_runtime) return;   /* [NoRuntime]: no ARC helpers */
    if (!v || LLVMGetTypeKind(LLVMTypeOf(v)) != LLVMPointerTypeKind) return;
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    if (LLVMTypeOf(v) != i8ptr) v = LLVMBuildBitCast(g->builder, v, i8ptr, "arc.rt");
    zan_call2(g->builder,
        LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), &i8ptr, 1, 0),
        g->rt_retain, &v, 1, "");
}

static void emit_arc_release(zan_irgen_t *g, LLVMValueRef v) {
    if (g->current_fn_no_runtime) return;   /* [NoRuntime]: no ARC helpers */
    if (!v || LLVMGetTypeKind(LLVMTypeOf(v)) != LLVMPointerTypeKind) return;
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    if (LLVMTypeOf(v) != i8ptr) v = LLVMBuildBitCast(g->builder, v, i8ptr, "arc.rl");
    zan_call2(g->builder,
        LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), &i8ptr, 1, 0),
        g->rt_release, &v, 1, "");
}

static void emit_string_retain(zan_irgen_t *g, LLVMValueRef v) {
    if (g->current_fn_no_runtime) return;   /* [NoRuntime]: no ARC helpers */
    if (!v || LLVMGetTypeKind(LLVMTypeOf(v)) != LLVMPointerTypeKind) return;
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    if (LLVMTypeOf(v) != i8ptr) v = LLVMBuildBitCast(g->builder, v, i8ptr, "str.rt");
    zan_call2(g->builder,
        LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), &i8ptr, 1, 0),
        g->rt_str_retain, &v, 1, "");
}

static void emit_string_release(zan_irgen_t *g, LLVMValueRef v) {
    if (g->current_fn_no_runtime) return;   /* [NoRuntime]: no ARC helpers */
    if (!v || LLVMGetTypeKind(LLVMTypeOf(v)) != LLVMPointerTypeKind) return;
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    if (LLVMTypeOf(v) != i8ptr) v = LLVMBuildBitCast(g->builder, v, i8ptr, "str.rl");
    zan_call2(g->builder,
        LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), &i8ptr, 1, 0),
        g->rt_str_release, &v, 1, "");
}

/* ---- delegate closures (A33-2) -------------------------------------------
 * A delegate value is one pointer with two shapes, told apart by bit 0:
 *   even -- a bare function pointer: a static method or a non-capturing
 *           lambda, so it can still be handed to C as a plain callback;
 *   odd  -- a tagged pointer to a heap closure record
 *           { ptr fn, ptr dtor, ptr target, <captured values> }, allocated with
 *           the object allocator (so it carries an rc header) and invoked as
 *           fn(record, args...). `target` is the bound receiver of an instance
 *           method group and null for a lambda; it gives `==` (and therefore
 *           `event -= obj.Handler`) C#'s target+method identity.
 * Retain/release therefore test the tag at run time: on a bare function
 * pointer both are no-ops, on a closure they drive the record's refcount and,
 * at zero, its dtor (which releases the captured rc values).
 * The shape itself (ZAN_CLOSURE_*) lives in ../common/zan_abi.h: runtime code
 * that keeps a delegate alive across calls has to read the same layout. */

static LLVMValueRef emit_closure_is_tagged(zan_irgen_t *g, LLVMValueRef v) {
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMValueRef iv = LLVMBuildPtrToInt(g->builder, v, i64, "clo.iv");
    LLVMValueRef bit = zan_and(g->builder, iv,
        LLVMConstInt(i64, ZAN_CLOSURE_TAG, 0), "clo.bit");
    return zan_icmp(g->builder, LLVMIntNE, bit, LLVMConstInt(i64, 0, 0), "clo.is");
}

static LLVMValueRef emit_closure_untag(zan_irgen_t *g, LLVMValueRef v) {
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMValueRef iv = LLVMBuildPtrToInt(g->builder, v, i64, "clo.iv");
    LLVMValueRef cl = zan_and(g->builder, iv,
        LLVMConstInt(i64, ~(uint64_t)ZAN_CLOSURE_TAG, 0), "clo.clr");
    return LLVMBuildIntToPtr(g->builder, cl, i8ptr, "clo.rec");
}

/* { ptr fn, ptr dtor, ptr target }: the prefix every closure record starts
 * with; captured values follow it. */
#define ZAN_CLOSURE_HDR_FIELDS 3
static LLVMTypeRef closure_header_type(zan_irgen_t *g) {
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef fields[ZAN_CLOSURE_HDR_FIELDS] = { i8ptr, i8ptr, i8ptr };
    return LLVMStructTypeInContext(g->ctx, fields, ZAN_CLOSURE_HDR_FIELDS, 0);
}

/* Delegate equality, as C# defines it: the same function and the same bound
 * receiver. Bare function pointers compare directly; two closure records are
 * equal when they wrap the same method-group target. Separately created
 * capturing lambdas have a null target and so stay distinct, matching C#. */
static LLVMValueRef emit_delegate_equals(zan_irgen_t *g, LLVMValueRef a,
                                         LLVMValueRef b) {
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i1 = LLVMInt1TypeInContext(g->ctx);
    LLVMValueRef ai = LLVMBuildPtrToInt(g->builder, a, i64, "deq.ai");
    LLVMValueRef bi = LLVMBuildPtrToInt(g->builder, b, i64, "deq.bi");
    LLVMValueRef same = zan_icmp(g->builder, LLVMIntEQ, ai, bi, "deq.same");
    LLVMValueRef both = LLVMBuildAnd(g->builder,
        emit_closure_is_tagged(g, a), emit_closure_is_tagged(g, b), "deq.both");
    LLVMValueRef fn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder));
    LLVMBasicBlockRef cmp_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "deq.cmp");
    LLVMBasicBlockRef end_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "deq.end");
    LLVMValueRef go = LLVMBuildAnd(g->builder, both,
        LLVMBuildNot(g->builder, same, "deq.nsame"), "deq.go");
    LLVMBasicBlockRef entry_bb = LLVMGetInsertBlock(g->builder);
    LLVMBuildCondBr(g->builder, go, cmp_bb, end_bb);

    LLVMPositionBuilderAtEnd(g->builder, cmp_bb);
    LLVMTypeRef hdr = closure_header_type(g);
    LLVMValueRef ra = emit_closure_untag(g, a), rb = emit_closure_untag(g, b);
    LLVMValueRef fa = LLVMBuildLoad2(g->builder, i8ptr,
        LLVMBuildStructGEP2(g->builder, hdr, ra, 0, "deq.fap"), "deq.fa");
    LLVMValueRef fb = LLVMBuildLoad2(g->builder, i8ptr,
        LLVMBuildStructGEP2(g->builder, hdr, rb, 0, "deq.fbp"), "deq.fb");
    LLVMValueRef ta = LLVMBuildLoad2(g->builder, i8ptr,
        LLVMBuildStructGEP2(g->builder, hdr, ra, 2, "deq.tap"), "deq.ta");
    LLVMValueRef tb = LLVMBuildLoad2(g->builder, i8ptr,
        LLVMBuildStructGEP2(g->builder, hdr, rb, 2, "deq.tbp"), "deq.tb");
    LLVMValueRef tai = LLVMBuildPtrToInt(g->builder, ta, i64, "deq.tai");
    LLVMValueRef eq = LLVMBuildAnd(g->builder,
        zan_icmp(g->builder, LLVMIntEQ,
                 LLVMBuildPtrToInt(g->builder, fa, i64, "deq.fai"),
                 LLVMBuildPtrToInt(g->builder, fb, i64, "deq.fbi"), "deq.feq"),
        LLVMBuildAnd(g->builder,
            zan_icmp(g->builder, LLVMIntEQ, tai,
                     LLVMBuildPtrToInt(g->builder, tb, i64, "deq.tbi"), "deq.teq"),
            zan_icmp(g->builder, LLVMIntNE, tai, LLVMConstInt(i64, 0, 0),
                     "deq.tnn"), "deq.tok"), "deq.eq");
    LLVMBuildBr(g->builder, end_bb);
    LLVMBasicBlockRef cmp_end = LLVMGetInsertBlock(g->builder);

    LLVMPositionBuilderAtEnd(g->builder, end_bb);
    LLVMValueRef phi = LLVMBuildPhi(g->builder, i1, "deq");
    LLVMValueRef vals[2] = { same, eq };
    LLVMBasicBlockRef blks[2] = { entry_bb, cmp_end };
    LLVMAddIncoming(phi, vals, blks, 2);
    return phi;
}

static void emit_closure_retain(zan_irgen_t *g, LLVMValueRef v) {
    if (g->current_fn_no_runtime) return;
    if (!v || LLVMGetTypeKind(LLVMTypeOf(v)) != LLVMPointerTypeKind) return;
    LLVMValueRef fn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder));
    LLVMBasicBlockRef doit = LLVMAppendBasicBlockInContext(g->ctx, fn, "clo.rt");
    LLVMBasicBlockRef done = LLVMAppendBasicBlockInContext(g->ctx, fn, "clo.rt.end");
    LLVMBuildCondBr(g->builder, emit_closure_is_tagged(g, v), doit, done);
    LLVMPositionBuilderAtEnd(g->builder, doit);
    emit_arc_retain(g, emit_closure_untag(g, v));
    LLVMBuildBr(g->builder, done);
    LLVMPositionBuilderAtEnd(g->builder, done);
}

/* Drop one reference to an (untagged) closure-shaped record through the
 * destructor it carries: it releases what the record owns when the count
 * reaches zero, then frees the record. Boxed locals (A33-2b) use the same
 * shape and therefore the same release. */
static void emit_closure_record_release(zan_irgen_t *g, LLVMValueRef rec) {
    if (g->current_fn_no_runtime) return;
    if (!rec || LLVMGetTypeKind(LLVMTypeOf(rec)) != LLVMPointerTypeKind) return;
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    if (LLVMTypeOf(rec) != i8ptr)
        rec = LLVMBuildBitCast(g->builder, rec, i8ptr, "clo.rec8");
    LLVMTypeRef hdr = closure_header_type(g);
    LLVMValueRef dp = LLVMBuildStructGEP2(g->builder, hdr, rec, 1, "clo.dtorp");
    LLVMValueRef dtor = LLVMBuildLoad2(g->builder, i8ptr, dp, "clo.dtor");
    zan_call2(g->builder,
        LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), &i8ptr, 1, 0),
        dtor, &rec, 1, "");
}

static void emit_closure_release(zan_irgen_t *g, LLVMValueRef v) {
    if (g->current_fn_no_runtime) return;
    if (!v || LLVMGetTypeKind(LLVMTypeOf(v)) != LLVMPointerTypeKind) return;
    LLVMValueRef fn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder));
    LLVMBasicBlockRef doit = LLVMAppendBasicBlockInContext(g->ctx, fn, "clo.rl");
    LLVMBasicBlockRef done = LLVMAppendBasicBlockInContext(g->ctx, fn, "clo.rl.end");
    LLVMBuildCondBr(g->builder, emit_closure_is_tagged(g, v), doit, done);
    LLVMPositionBuilderAtEnd(g->builder, doit);
    emit_closure_record_release(g, emit_closure_untag(g, v));
    LLVMBuildBr(g->builder, done);
    LLVMPositionBuilderAtEnd(g->builder, done);
}

/* Invoke a delegate value: a bare function pointer is called directly, a
 * closure through the record's function with the record as leading argument.
 * Both shapes are possible in the same value, so the tag test is emitted at
 * every call site. */
static LLVMValueRef emit_delegate_invoke(zan_irgen_t *g, LLVMValueRef dv,
                                         LLVMTypeRef fn_type, LLVMTypeRef ret,
                                         LLVMValueRef *args, int argc,
                                         const char *name) {
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    bool is_void = LLVMGetTypeKind(ret) == LLVMVoidTypeKind;
    LLVMValueRef fn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder));
    LLVMBasicBlockRef clo_bb  = LLVMAppendBasicBlockInContext(g->ctx, fn, "dlg.clo");
    LLVMBasicBlockRef bare_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "dlg.bare");
    LLVMBasicBlockRef join_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "dlg.join");
    LLVMBuildCondBr(g->builder, emit_closure_is_tagged(g, dv), clo_bb, bare_bb);

    LLVMPositionBuilderAtEnd(g->builder, clo_bb);
    LLVMValueRef rec = emit_closure_untag(g, dv);
    LLVMValueRef cfnp = LLVMBuildStructGEP2(g->builder, closure_header_type(g),
                                            rec, 0, "dlg.fnp");
    LLVMValueRef cfn = LLVMBuildLoad2(g->builder, i8ptr, cfnp, "dlg.fn");
    unsigned nparams = LLVMCountParamTypes(fn_type);
    LLVMTypeRef *ptypes = (LLVMTypeRef *)calloc((size_t)nparams + 1, sizeof(LLVMTypeRef));
    ptypes[0] = i8ptr;
    if (nparams) LLVMGetParamTypes(fn_type, ptypes + 1);
    LLVMTypeRef clo_fn_type = LLVMFunctionType(ret, ptypes, nparams + 1, 0);
    free(ptypes);
    LLVMValueRef *cargs = (LLVMValueRef *)calloc((size_t)argc + 1, sizeof(LLVMValueRef));
    cargs[0] = rec;
    for (int i = 0; i < argc; i++) cargs[i + 1] = args[i];
    LLVMValueRef rclo = zan_call2(g->builder, clo_fn_type, cfn, cargs,
                                  (unsigned)argc + 1, is_void ? "" : "dlg.rc");
    free(cargs);
    LLVMBasicBlockRef clo_end = LLVMGetInsertBlock(g->builder);
    LLVMBuildBr(g->builder, join_bb);

    LLVMPositionBuilderAtEnd(g->builder, bare_bb);
    LLVMValueRef rbare = zan_call2(g->builder, fn_type, dv, args,
                                   (unsigned)argc, is_void ? "" : "dlg.rb");
    LLVMBasicBlockRef bare_end = LLVMGetInsertBlock(g->builder);
    LLVMBuildBr(g->builder, join_bb);

    LLVMPositionBuilderAtEnd(g->builder, join_bb);
    if (is_void) return NULL;
    LLVMValueRef phi = LLVMBuildPhi(g->builder, ret, name && *name ? name : "dlg.r");
    LLVMValueRef vals[2] = { rclo, rbare };
    LLVMBasicBlockRef blks[2] = { clo_end, bare_end };
    LLVMAddIncoming(phi, vals, blks, 2);
    return phi;
}

static void emit_array_retain(zan_irgen_t *g, LLVMValueRef v);
static void emit_array_release(zan_irgen_t *g, zan_type_t *type, LLVMValueRef v);

static void emit_rc_retain_for_type(zan_irgen_t *g, zan_type_t *type, LLVMValueRef v) {
    if (!type) return;
    if (type->kind == TYPE_STRING) {
        emit_string_retain(g, v);
    } else if (type->kind == TYPE_DELEGATE) {
        emit_closure_retain(g, v);
    } else if (type->kind == TYPE_ARRAY) {
        emit_array_retain(g, v);
    } else if (is_arc_managed_type(type)) {
        emit_arc_retain(g, v);
    }
}

static void emit_rc_release_for_type(zan_irgen_t *g, zan_type_t *type, LLVMValueRef v) {
    if (!type) return;
    if (type->kind == TYPE_STRING) {
        emit_string_release(g, v);
    } else if (type->kind == TYPE_DELEGATE) {
        emit_closure_release(g, v);
    } else if (type->kind == TYPE_ARRAY) {
        emit_array_release(g, type, v);
    } else if (is_arc_managed_type(type)) {
        emit_arc_release_typed(g, type, v);
    }
}

static int type_contains_collection_rc(zan_irgen_t *g, zan_type_t *type,
                                       unsigned depth) {
    if (!type || depth > 32) return 0;
    type = concretize(g, type);
    if (!type) return 0;
    if (is_rc_managed_type(type)) return 1;
    if (type->kind != TYPE_STRUCT || !type->sym) return 0;
    for (int i = 0; i < type->sym->member_count; i++) {
        zan_symbol_t *m = type->sym->members[i];
        if ((m->kind != SYM_FIELD && m->kind != SYM_PROPERTY) ||
            field_member_is_static(m) || (m->modifiers & MOD_WEAK)) continue;
        zan_type_t *ft = subst_type_param_deep(g, m->type, type);
        if (type_contains_collection_rc(g, ft, depth + 1)) return 1;
    }
    return 0;
}

static void emit_collection_value_retain(zan_irgen_t *g, zan_type_t *type,
                                         LLVMValueRef value, unsigned depth) {
    if (!type || !value || depth > 32) return;
    type = concretize(g, type);
    if (!type) return;
    if (is_rc_managed_type(type)) {
        emit_rc_retain_for_type(g, type, value);
        return;
    }
    if (type->kind != TYPE_STRUCT || !type->sym ||
        LLVMGetTypeKind(LLVMTypeOf(value)) != LLVMStructTypeKind) return;
    unsigned fi = (unsigned)class_vptr_offset(type->sym);
    for (int i = 0; i < type->sym->member_count; i++) {
        zan_symbol_t *m = type->sym->members[i];
        if ((m->kind != SYM_FIELD && m->kind != SYM_PROPERTY) ||
            field_member_is_static(m)) continue;
        LLVMValueRef field = LLVMBuildExtractValue(g->builder, value, fi++, "arc.vf");
        if (m->modifiers & MOD_WEAK) continue;
        zan_type_t *ft = subst_type_param_deep(g, m->type, type);
        emit_collection_value_retain(g, ft, field, depth + 1);
    }
}

static void emit_collection_value_release(zan_irgen_t *g, zan_type_t *type,
                                          LLVMValueRef value, unsigned depth) {
    if (!type || !value || depth > 32) return;
    type = concretize(g, type);
    if (!type) return;
    if (is_rc_managed_type(type)) {
        emit_rc_release_for_type(g, type, value);
        return;
    }
    if (type->kind != TYPE_STRUCT || !type->sym ||
        LLVMGetTypeKind(LLVMTypeOf(value)) != LLVMStructTypeKind) return;
    unsigned fi = (unsigned)class_vptr_offset(type->sym);
    for (int i = 0; i < type->sym->member_count; i++) {
        zan_symbol_t *m = type->sym->members[i];
        if ((m->kind != SYM_FIELD && m->kind != SYM_PROPERTY) ||
            field_member_is_static(m)) continue;
        LLVMValueRef field = LLVMBuildExtractValue(g->builder, value, fi++, "arc.vf");
        if (m->modifiers & MOD_WEAK) continue;
        zan_type_t *ft = subst_type_param_deep(g, m->type, type);
        emit_collection_value_release(g, ft, field, depth + 1);
    }
}

static LLVMValueRef load_dict_value_words(zan_irgen_t *g, LLVMValueRef raw) {
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMValueRef dict = LLVMBuildBitCast(g->builder, raw,
        LLVMPointerType(g->dict_struct_type, 0), "dvw.dp");
    LLVMValueRef words_ptr = LLVMBuildStructGEP2(g->builder,
        g->dict_struct_type, dict, 7, "dvw.p");
    return LLVMBuildLoad2(g->builder, i64, words_ptr, "dvw");
}

static void emit_collection_slot_store(zan_irgen_t *g, zan_type_t *elem_type,
                                       LLVMTypeRef slot_ty, LLVMValueRef slot_ptr,
                                       LLVMValueRef value,
                                       zan_ast_node_t *rhs, local_scope_t *locals,
                                       int overwrite_old) {
    LLVMTypeRef elem_llvm = elem_type ? map_type(g, elem_type) : slot_ty;
    LLVMTypeKind elem_kind = LLVMGetTypeKind(elem_llvm);
    LLVMValueRef old = NULL;
    if (overwrite_old) {
        old = elem_kind == LLVMStructTypeKind
            ? load_struct_from_slot(g, slot_ptr, elem_llvm)
            : LLVMBuildLoad2(g->builder, slot_ty, slot_ptr, "arc.old");
    }
    if (!expr_yields_owned_rc_value(g, rhs, locals))
        emit_collection_value_retain(g, elem_type, value, 0);

    LLVMValueRef stored = value;
    LLVMTypeKind slot_kind = LLVMGetTypeKind(slot_ty);
    LLVMTypeKind value_kind = LLVMGetTypeKind(LLVMTypeOf(stored));
    /* An integer landing in a `double` element is converted before it is
     * reinterpreted into the i64 slot: `List<double> l; l.Add(1);` used to store
     * the integer bit pattern, and the reader (which bitcasts the slot back to
     * double) saw a denormal. */
    if (elem_kind == LLVMDoubleTypeKind && value_kind == LLVMIntegerTypeKind &&
        LLVMGetIntTypeWidth(LLVMTypeOf(stored)) > 1) {
        stored = LLVMBuildSIToFP(g->builder, stored, elem_llvm, "slot.sitofp");
        value_kind = LLVMGetTypeKind(LLVMTypeOf(stored));
    }
    if (value_kind == LLVMStructTypeKind) {
        store_struct_in_slot(g, stored, slot_ptr, rhs);
    } else {
        if (slot_kind == LLVMPointerTypeKind) {
            if (value_kind == LLVMIntegerTypeKind) {
                LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                if (elem_llvm && LLVMGetTypeKind(elem_llvm) == LLVMIntegerTypeKind &&
                    LLVMGetIntTypeWidth(elem_llvm) < LLVMGetIntTypeWidth(LLVMTypeOf(stored)))
                    stored = LLVMBuildTrunc(g->builder, stored, elem_llvm, "slot.knw");
                if (LLVMGetIntTypeWidth(LLVMTypeOf(stored)) < 64)
                    stored = extend_int_for_slot(g, stored, elem_type, i64);
                stored = LLVMBuildIntToPtr(g->builder, stored, slot_ty, "slot.ip");
            } else if (value_kind == LLVMPointerTypeKind && LLVMTypeOf(stored) != slot_ty) {
                stored = LLVMBuildBitCast(g->builder, stored, slot_ty, "slot.bc");
            }
        } else if (slot_kind == LLVMIntegerTypeKind) {
            if (value_kind == LLVMPointerTypeKind) {
                stored = LLVMBuildPtrToInt(g->builder, stored, slot_ty, "slot.pi");
            } else if (value_kind == LLVMIntegerTypeKind) {
                if (elem_llvm && LLVMGetTypeKind(elem_llvm) == LLVMIntegerTypeKind &&
                    LLVMGetIntTypeWidth(elem_llvm) < LLVMGetIntTypeWidth(LLVMTypeOf(stored)))
                    stored = LLVMBuildTrunc(g->builder, stored, elem_llvm, "slot.elw");
                if (LLVMGetIntTypeWidth(LLVMTypeOf(stored)) < LLVMGetIntTypeWidth(slot_ty))
                    stored = extend_int_for_slot(g, stored, elem_type, slot_ty);
            } else if (value_kind == LLVMDoubleTypeKind) {
                stored = LLVMBuildBitCast(g->builder, stored, slot_ty, "slot.fb");
            }
        }
        LLVMBuildStore(g->builder, stored, slot_ptr);
    }
    if (overwrite_old) {
        LLVMValueRef old_value = old;
        if (elem_kind == LLVMPointerTypeKind &&
            LLVMGetTypeKind(LLVMTypeOf(old)) == LLVMIntegerTypeKind)
            old_value = LLVMBuildIntToPtr(g->builder, old, elem_llvm, "slot.old");
        emit_collection_value_release(g, elem_type, old_value, 0);
    }
}

static void emit_typed_out_store(zan_irgen_t *g, zan_type_t *value_type,
                                 LLVMValueRef out_ptr, LLVMValueRef value) {
    if (!value_type || !out_ptr || !value) return;
    LLVMTypeRef value_llvm = map_type(g, value_type);
    LLVMValueRef old = LLVMBuildLoad2(g->builder, value_llvm, out_ptr,
                                      "out.old");
    /* The dictionary slot is borrowed. Retain the incoming value before
     * replacing the caller's owner, then release the overwritten value. */
    emit_collection_value_retain(g, value_type, value, 0);
    LLVMValueRef stored = value;
    if (LLVMGetTypeKind(LLVMTypeOf(stored)) == LLVMPointerTypeKind &&
        LLVMGetTypeKind(value_llvm) == LLVMPointerTypeKind &&
        LLVMTypeOf(stored) != value_llvm)
        stored = LLVMBuildBitCast(g->builder, stored, value_llvm, "out.bc");
    else if (LLVMGetTypeKind(LLVMTypeOf(stored)) == LLVMIntegerTypeKind &&
             LLVMGetTypeKind(value_llvm) == LLVMIntegerTypeKind &&
             LLVMTypeOf(stored) != value_llvm)
        stored = coerce_int_to(g, stored, value_llvm);
    LLVMBuildStore(g->builder, stored, out_ptr);
    emit_collection_value_release(g, value_type, old, 0);
}

static void emit_collection_release_raw_slot(zan_irgen_t *g, zan_type_t *elem_type,
                                             LLVMValueRef raw, LLVMTypeRef slot_ty) {
    if (!elem_type || !type_contains_collection_rc(g, elem_type, 0)) return;
    LLVMTypeRef elem_llvm = map_type(g, elem_type);
    LLVMValueRef value = raw;
    if (LLVMGetTypeKind(elem_llvm) == LLVMPointerTypeKind &&
        LLVMGetTypeKind(LLVMTypeOf(raw)) == LLVMIntegerTypeKind)
        value = LLVMBuildIntToPtr(g->builder, raw, elem_llvm, "slot.old");
    else if (LLVMGetTypeKind(slot_ty) == LLVMPointerTypeKind &&
             LLVMTypeOf(raw) != elem_llvm &&
             LLVMGetTypeKind(LLVMTypeOf(raw)) == LLVMPointerTypeKind)
        value = LLVMBuildBitCast(g->builder, raw, elem_llvm, "slot.old");
    emit_collection_value_release(g, elem_type, value, 0);
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

/* One destructor per (class, instantiation): a field declared `T` holds a
 * different type in Acc<Node> than in Acc<int>, so one shared destructor keyed
 * by the class symbol alone skipped every type-parameter field and leaked
 * whatever it held. `inst` is NULL for non-generic classes. */
static LLVMValueRef get_class_release_decl(zan_irgen_t *g, zan_symbol_t *sym,
                                          zan_type_t *inst) {
    if (!sym) return NULL;
    if (inst && (!inst->type_arg_count || !type_is_concrete(inst))) inst = NULL;
    for (int i = 0; i < g->class_release_count; i++) {
        if (g->class_release[i].sym != sym) continue;
        zan_type_t *ci = g->class_release[i].inst;
        if (ci == inst) return g->class_release[i].fn;
        if (ci && inst && types_equal(ci, inst)) return g->class_release[i].fn;
    }
    /* only classes with a registered struct layout can be walked */
    if (!get_struct_llvm_type(g, sym)) return NULL;
    g->class_release = irgen_grow(g->class_release, &g->class_release_cap,
                                  g->class_release_count + 1,
                                  sizeof(*g->class_release));
    char name[320];
    snprintf(name, sizeof(name), "__zan_release_%.*s_%d",
             (int)sym->name.len, sym->name.str, g->class_release_count);
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef ft = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), &i8ptr, 1, 0);
    LLVMValueRef fn = LLVMAddFunction(g->mod, name, ft);
    LLVMSetLinkage(fn, LLVMInternalLinkage);
    g->class_release[g->class_release_count].sym = sym;
    g->class_release[g->class_release_count].inst = inst;
    g->class_release[g->class_release_count].fn = fn;
    g->class_release_count++;
    return fn;
}

/* Release the RC-managed elements held by a List<T> value `col` (an i8* to the
 * bare List struct { i64 count, i64 cap, i64* data }). No-op for null lists or
 * non-RC element types. Only the tracked elements are released; the header-less
 * List struct/buffer are not rc-counted and are left as-is. */
static void emit_list_release_elems(zan_irgen_t *g, zan_type_t *elem_type, LLVMValueRef col) {
    if (!elem_type || !type_contains_collection_rc(g, elem_type, 0)) return;
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
    LLVMValueRef isn = zan_icmp(b, LLVMIntEQ, c8, LLVMConstNull(i8ptr), "lc.isn");
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
    LLVMValueRef lt = zan_icmp(b, LLVMIntSLT, iphi, cnt, "lt");
    LLVMBuildCondBr(b, lt, body, done);
    LLVMPositionBuilderAtEnd(b, body);
    LLVMValueRef word = slot_word_index(g, iphi, elem_slot_words(g, elem_type));
    LLVMValueRef slot = LLVMBuildGEP2(b, i64, data, &word, 1, "slot");
    LLVMTypeRef elem_llvm = map_type(g, elem_type);
    LLVMValueRef raw = LLVMGetTypeKind(elem_llvm) == LLVMStructTypeKind
        ? load_struct_from_slot(g, slot, elem_llvm)
        : LLVMBuildLoad2(b, i64, slot, "raw");
    emit_collection_release_raw_slot(g, elem_type, raw, i64);
    LLVMValueRef inext = zan_add(b, iphi, LLVMConstInt(i64, 1, 0), "inext");
    LLVMBasicBlockRef body_end = LLVMGetInsertBlock(b);
    LLVMBuildBr(b, head);
    LLVMValueRef vals[2] = { LLVMConstInt(i64, 0, 0), inext };
    LLVMBasicBlockRef blks[2] = { chk, body_end };
    LLVMAddIncoming(iphi, vals, blks, 2);
    LLVMPositionBuilderAtEnd(b, done);
}

/* Release the rc-managed keys/values held by a Dict value `col` (an i8* to
 * the bare Dict struct { i64 count, i64 cap, i8** keys, i64* vals }). The
 * header-less Dict struct/buffers are calloc'd and not rc-counted; only the
 * tracked occupants are released. No-op for a null dict. */
static void emit_dict_release_elems(zan_irgen_t *g, zan_type_t *dict_type, LLVMValueRef col) {
    zan_type_t *kt = dict_key_type(g, dict_type);
    zan_type_t *vt = dict_value_type(dict_type);
    bool krc = kt && is_rc_managed_type(kt);
    bool vrc = vt && type_contains_collection_rc(g, vt, 0);
    if (!krc && !vrc) return;
    if (!col || LLVMGetTypeKind(LLVMTypeOf(col)) != LLVMPointerTypeKind) return;
    LLVMContextRef c = g->ctx;
    LLVMBuilderRef b = g->builder;
    LLVMTypeRef i64 = LLVMInt64TypeInContext(c);
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(c), 0);
    LLVMValueRef fn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(b));
    LLVMBasicBlockRef chk  = LLVMAppendBasicBlockInContext(c, fn, "dc.chk");
    LLVMBasicBlockRef head = LLVMAppendBasicBlockInContext(c, fn, "dc.head");
    LLVMBasicBlockRef body = LLVMAppendBasicBlockInContext(c, fn, "dc.body");
    LLVMBasicBlockRef done = LLVMAppendBasicBlockInContext(c, fn, "dc.done");
    LLVMValueRef c8 = (LLVMTypeOf(col) == i8ptr) ? col
                      : LLVMBuildBitCast(b, col, i8ptr, "dc.c8");
    LLVMValueRef isn = zan_icmp(b, LLVMIntEQ, c8, LLVMConstNull(i8ptr), "dc.isn");
    LLVMBuildCondBr(b, isn, done, chk);
    LLVMPositionBuilderAtEnd(b, chk);
    LLVMValueRef dp = LLVMBuildBitCast(b, c8, LLVMPointerType(g->dict_struct_type, 0), "dp");
    LLVMValueRef cnt = LLVMBuildLoad2(b, i64,
        LLVMBuildStructGEP2(b, g->dict_struct_type, dp, 0, "cntp"), "cnt");
    LLVMValueRef ks = LLVMBuildLoad2(b, LLVMPointerType(i8ptr, 0),
        LLVMBuildStructGEP2(b, g->dict_struct_type, dp, 2, "kp"), "ks");
    LLVMValueRef vs = LLVMBuildLoad2(b, LLVMPointerType(i64, 0),
        LLVMBuildStructGEP2(b, g->dict_struct_type, dp, 3, "vp"), "vs");
    LLVMValueRef value_words = LLVMBuildLoad2(b, i64,
        LLVMBuildStructGEP2(b, g->dict_struct_type, dp, 7, "vwp"), "vw");
    LLVMBuildBr(b, head);
    LLVMPositionBuilderAtEnd(b, head);
    LLVMValueRef iphi = LLVMBuildPhi(b, i64, "i");
    LLVMValueRef lt = zan_icmp(b, LLVMIntSLT, iphi, cnt, "lt");
    LLVMBuildCondBr(b, lt, body, done);
    LLVMPositionBuilderAtEnd(b, body);
    if (krc) {
        LLVMValueRef kslot = LLVMBuildGEP2(b, i8ptr, ks, &iphi, 1, "kslot");
        LLVMValueRef kv = LLVMBuildLoad2(b, i8ptr, kslot, "kv");
        emit_collection_release_raw_slot(g, kt, kv, i8ptr);
    }
    if (vrc) {
        LLVMValueRef word = zan_mul(b, iphi, value_words, "dv.word");
        LLVMValueRef vslot = LLVMBuildGEP2(b, i64, vs, &word, 1, "vslot");
        LLVMTypeRef value_llvm = map_type(g, vt);
        LLVMValueRef vv = LLVMGetTypeKind(value_llvm) == LLVMStructTypeKind
            ? load_struct_from_slot(g, vslot, value_llvm)
            : LLVMBuildLoad2(b, i64, vslot, "vv");
        emit_collection_release_raw_slot(g, vt, vv, i64);
    }
    LLVMValueRef inext = zan_add(b, iphi, LLVMConstInt(i64, 1, 0), "inext");
    LLVMBasicBlockRef body_end = LLVMGetInsertBlock(b);
    LLVMBuildBr(b, head);
    LLVMValueRef vals[2] = { LLVMConstInt(i64, 0, 0), inext };
    LLVMBasicBlockRef blks[2] = { chk, body_end };
    LLVMAddIncoming(iphi, vals, blks, 2);
    LLVMPositionBuilderAtEnd(b, done);
}

/* Release the rc-managed elements of an array payload. The count is threaded
 * in explicitly so the same walk serves a plain array (count at arr-16, data
 * at arr) and a rectangular one (data behind the shape). Null is a no-op. */
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
    LLVMValueRef isn = zan_icmp(b, LLVMIntEQ, a8, LLVMConstNull(i8ptr), "ac.isn");
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
    LLVMValueRef lt = zan_icmp(b, LLVMIntSLT, iphi, n, "ac.lt");
    LLVMBuildCondBr(b, lt, body, done);
    LLVMPositionBuilderAtEnd(b, body);
    LLVMValueRef slot = LLVMBuildGEP2(b, elem_llvm, typed, &iphi, 1, "ac.slot");
    LLVMValueRef elem = LLVMBuildLoad2(b, elem_llvm, slot, "ac.elem");
    emit_rc_release_for_type(g, elem_type, elem);
    LLVMValueRef inext = zan_add(b, iphi, LLVMConstInt(i64, 1, 0), "ac.inext");
    LLVMBasicBlockRef body_end = LLVMGetInsertBlock(b);
    LLVMBuildBr(b, head);
    LLVMValueRef vals[2] = { LLVMConstInt(i64, 0, 0), inext };
    LLVMBasicBlockRef blks[2] = { chk, body_end };
    LLVMAddIncoming(iphi, vals, blks, 2);
    LLVMPositionBuilderAtEnd(b, done);
}

/* Retain of an array value: the tolerant runtime helper only touches a buffer
 * carrying the array rc guard, so a `T[]`-typed value that never came from
 * `new T[n]` (an extern's buffer, a span base) costs a load and a compare. */
static void emit_array_retain(zan_irgen_t *g, LLVMValueRef v) {
    if (!v || LLVMGetTypeKind(LLVMTypeOf(v)) != LLVMPointerTypeKind) return;
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMValueRef a = (LLVMTypeOf(v) == i8ptr) ? v
        : LLVMBuildBitCast(g->builder, v, i8ptr, "arr.rt8");
    zan_call2(g->builder,
        LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), &i8ptr, 1, 0),
        g->rt_arr_retain, &a, 1, "");
}

/* Per-element-type array destructor __zan_arr_release_<T>: release the
 * elements when this release is the one that takes the count to zero, then let
 * zan_rt_arr_release do the decrement and the free. Emitted only for element
 * types that own something; a `byte[]`/`int[]` release is the plain helper. */
static void mangle_type_token(char *buf, size_t n, size_t *off, zan_type_t *t);

static LLVMValueRef get_array_release_decl(zan_irgen_t *g, zan_type_t *elem_type,
                                           int rect) {
    char tok[192];
    size_t off = 0;
    tok[0] = '\0';
    mangle_type_token(tok, sizeof(tok), &off, elem_type);
    char name[256];
    snprintf(name, sizeof(name), "__zan_arr_release_%s%s", tok, rect ? "_md" : "");
    LLVMValueRef existing = LLVMGetNamedFunction(g->mod, name);
    if (existing) return existing;

    LLVMContextRef c = g->ctx;
    LLVMTypeRef i8 = LLVMInt8TypeInContext(c);
    LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(c);
    LLVMValueRef fn = LLVMAddFunction(g->mod, name,
        LLVMFunctionType(LLVMVoidTypeInContext(c), &i8ptr, 1, 0));
    LLVMSetLinkage(fn, LLVMInternalLinkage);

    LLVMBasicBlockRef saved_bb = LLVMGetInsertBlock(g->builder);
    di_clear(g); /* synthetic fn: don't inherit a user fn's DISubprogram scope */
    LLVMBuilderRef b = g->builder;
    LLVMValueRef arr = LLVMGetParam(fn, 0);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(c, fn, "entry");
    LLVMBasicBlockRef guard = LLVMAppendBasicBlockInContext(c, fn, "guard");
    LLVMBasicBlockRef relel = LLVMAppendBasicBlockInContext(c, fn, "relel");
    LLVMBasicBlockRef dorel = LLVMAppendBasicBlockInContext(c, fn, "dorel");
    LLVMBasicBlockRef ret   = LLVMAppendBasicBlockInContext(c, fn, "ret");
    LLVMPositionBuilderAtEnd(b, entry);
    LLVMBuildCondBr(b, zan_icmp(b, LLVMIntEQ, arr, LLVMConstNull(i8ptr), "isnull"),
                    ret, guard);
    LLVMPositionBuilderAtEnd(b, guard);
    /* only a buffer with the rc guard has a count word to walk */
    LLVMValueRef gmoff = LLVMConstInt(i64, (unsigned long long)ZAN_ARR_RC_MAGIC_OFF, 1);
    LLVMValueRef gmp = LLVMBuildGEP2(b, i8, arr, &gmoff, 1, "magicp");
    emit_header_read_guard(g, fn, gmp, ret);
    LLVMValueRef magic = LLVMBuildLoad2(b, i64,
        LLVMBuildBitCast(b, gmp, LLVMPointerType(i64, 0), "magicip"), "magic");
    LLVMValueRef has_magic = zan_icmp(b, LLVMIntEQ, magic,
        LLVMConstInt(i64, ZAN_ARRAY_RC_MAGIC, 0), "hasmagic");
    LLVMBasicBlockRef peek = LLVMAppendBasicBlockInContext(c, fn, "peek");
    LLVMBuildCondBr(b, has_magic, peek, ret);
    LLVMPositionBuilderAtEnd(b, peek);
    LLVMValueRef rcoff = LLVMConstInt(i64, (unsigned long long)ZAN_ARR_RC_OFF, 1);
    LLVMValueRef rc = LLVMBuildLoad2(b, i64,
        LLVMBuildBitCast(b, LLVMBuildGEP2(b, i8, arr, &rcoff, 1, "rcp"),
                         LLVMPointerType(i64, 0), "rcip"), "rc");
    LLVMBuildCondBr(b, zan_icmp(b, LLVMIntEQ, rc, LLVMConstInt(i64, 1, 0), "is1"),
                    relel, dorel);
    LLVMPositionBuilderAtEnd(b, relel);
    LLVMValueRef data = arr;
    if (rect) {
        /* a rectangular array keeps its shape between the count word and the
         * payload: rank at arr-8, dims at arr+0, elements at arr + 8*rank
         * (see zan_mdarray_alloc) */
        LLVMValueRef rkoff = LLVMConstInt(i64, (unsigned long long)ZAN_OBJ_SITE_OFF, 1);
        LLVMValueRef rank = LLVMBuildLoad2(b, i64,
            LLVMBuildBitCast(b, LLVMBuildGEP2(b, i8, arr, &rkoff, 1, "rankp"),
                             LLVMPointerType(i64, 0), "rankip"), "rank");
        LLVMValueRef shape = LLVMBuildMul(b, rank, LLVMConstInt(i64, 8, 0), "shape");
        data = LLVMBuildGEP2(b, i8, arr, &shape, 1, "md.data");
    }
    emit_array_release_elems(g, elem_type, data, zan_array_len(g, arr));
    LLVMBuildBr(b, dorel);
    LLVMPositionBuilderAtEnd(b, dorel);
    zan_call2(b, LLVMFunctionType(LLVMVoidTypeInContext(c), &i8ptr, 1, 0),
              g->rt_arr_release, &arr, 1, "");
    LLVMBuildBr(b, ret);
    LLVMPositionBuilderAtEnd(b, ret);
    LLVMBuildRetVoid(b);
    if (saved_bb) LLVMPositionBuilderAtEnd(b, saved_bb);
    return fn;
}

/* Release of an array value: an element type that owns something goes through
 * the per-element-type destructor (rectangular arrays get the shape-aware
 * variant), anything else through the plain runtime helper. */
static void emit_array_release(zan_irgen_t *g, zan_type_t *type, LLVMValueRef v) {
    if (!v || LLVMGetTypeKind(LLVMTypeOf(v)) != LLVMPointerTypeKind) return;
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMValueRef a = (LLVMTypeOf(v) == i8ptr) ? v
        : LLVMBuildBitCast(g->builder, v, i8ptr, "arr.rl8");
    zan_type_t *et = type ? type->element_type : NULL;
    LLVMValueRef fn = g->rt_arr_release;
    if (et && is_rc_managed_type(et))
        fn = get_array_release_decl(g, et, type->array_rank > 1);
    zan_call2(g->builder,
        LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), &i8ptr, 1, 0),
        fn, &a, 1, "");
}

/* Emit the body of __zan_release_<T>: null-guard, peek the refcount, release the
 * RC-managed fields when it is about to hit zero, then hand off to
 * zan_rt_release for the decrement + free. */
static void build_class_release_body(zan_irgen_t *g, zan_symbol_t *sym,
                                    zan_type_t *inst, LLVMValueRef fn) {
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
    LLVMValueRef isnull = zan_icmp(b, LLVMIntEQ, obj, LLVMConstNull(i8ptr), "isnull");
    LLVMBuildCondBr(b, isnull, ret, cont);
    LLVMPositionBuilderAtEnd(b, cont);
    LLVMValueRef neg16 = LLVMConstInt(i64, (unsigned long long)ZAN_OBJ_RC_OFF, 1);
    LLVMValueRef rcp = LLVMBuildGEP2(b, LLVMInt8TypeInContext(c), obj, &neg16, 1, "rcp");
    LLVMValueRef rcip = LLVMBuildBitCast(b, rcp, LLVMPointerType(i64, 0), "rcip");
    LLVMValueRef rc = LLVMBuildLoad2(b, i64, rcip, "rc");
    LLVMValueRef is1 = zan_icmp(b, LLVMIntEQ, rc, LLVMConstInt(i64, 1, 0), "is1");
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
        /* resolve `T` / `List<T>` fields against the instantiation being freed */
        if (inst) ft = subst_type_param_deep(g, ft, inst);
        if (!ft || ft->kind == TYPE_TYPE_PARAM) continue;
        /* weak fields are non-owning back-references: unregister the slot
         * before the containing object is freed, but never release its value. */
        if ((m->modifiers & MOD_WEAK) && is_arc_managed_type(ft) &&
            (ft->kind == TYPE_INTERFACE ||
             (ft->kind == TYPE_CLASS && ft->sym != NULL))) {
            if (LLVMGetTypeKind(map_type(g, ft)) == LLVMPointerTypeKind) {
                LLVMValueRef fp = LLVMBuildStructGEP2(b, structT, self,
                    (unsigned)idx, "weak.fp");
                emit_weak_store(g, fp,
                    LLVMConstPointerNull(LLVMPointerType(
                        LLVMInt8TypeInContext(c), 0)));
            }
            continue;
        }
        if (ft->kind == TYPE_STRING) {
            LLVMValueRef fp = LLVMBuildStructGEP2(b, structT, self, (unsigned)idx, "fp");
            LLVMValueRef s = LLVMBuildLoad2(b, i8ptr, fp, "fs");
            emit_string_release(g, s);
        } else if (ft->kind == TYPE_DELEGATE) {
            /* A delegate field may hold a closure record (captured values or
             * a bound receiver); on a bare function pointer the release is a
             * no-op. The tag test splits the block, which the remaining field
             * releases tolerate: `self` dominates every successor. */
            LLVMValueRef fp = LLVMBuildStructGEP2(g->builder, structT, self,
                                                  (unsigned)idx, "fp");
            LLVMValueRef dv = LLVMBuildLoad2(g->builder, i8ptr, fp, "fd");
            emit_closure_release(g, dv);
        } else if (ft->kind == TYPE_ARRAY) {
            /* An array field holds the same +1 a local would (field stores
             * retain), so the owner drops it here; a field that was never
             * given a `new T[n]` buffer -- an extern's pointer -- lacks the
             * rc prefix and the release is a no-op. */
            LLVMValueRef fp = LLVMBuildStructGEP2(b, structT, self, (unsigned)idx, "fp");
            LLVMValueRef av = LLVMBuildLoad2(b, i8ptr, fp, "fa");
            emit_array_release(g, ft, av);
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
    LLVMValueRef isnull = zan_icmp(b, LLVMIntEQ, obj, LLVMConstNull(i8ptr), "isnull");
    LLVMBuildCondBr(b, isnull, ret, cont);
    LLVMPositionBuilderAtEnd(b, cont);
    LLVMValueRef neg16 = LLVMConstInt(i64, (unsigned long long)ZAN_OBJ_RC_OFF, 1);
    LLVMValueRef rcp = LLVMBuildGEP2(b, LLVMInt8TypeInContext(c), obj, &neg16, 1, "rcp");
    LLVMValueRef rcip = LLVMBuildBitCast(b, rcp, LLVMPointerType(i64, 0), "rcip");
    LLVMValueRef rc = LLVMBuildLoad2(b, i64, rcip, "rc");
    LLVMValueRef is1 = zan_icmp(b, LLVMIntEQ, rc, LLVMConstInt(i64, 1, 0), "is1");
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
    } else if (coll_kind == 3) {
        /* Dict: release rc-managed keys/values, then free the keys, values and
         * hash-index buffers. `elem_type` carries the dict type itself so the
         * key/value types stay recoverable here. */
        LLVMValueRef dp = LLVMBuildBitCast(b, obj, LLVMPointerType(g->dict_struct_type, 0), "dp");
        LLVMValueRef ks = LLVMBuildLoad2(b, i8ptr,
            LLVMBuildStructGEP2(b, g->dict_struct_type, dp, 2, "kp"), "ks8");
        LLVMValueRef vs = LLVMBuildLoad2(b, i8ptr,
            LLVMBuildStructGEP2(b, g->dict_struct_type, dp, 3, "vp"), "vs8");
        LLVMValueRef ix = LLVMBuildLoad2(b, i8ptr,
            LLVMBuildStructGEP2(b, g->dict_struct_type, dp, 4, "ip"), "ix8");
        emit_dict_release_elems(g, elem_type, obj);
        zan_call2(b, free_ty, g->fn_free, &ks, 1, "");
        zan_call2(b, free_ty, g->fn_free, &vs, 1, "");
        zan_call2(b, free_ty, g->fn_free, &ix, 1, "");
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
        LLVMValueRef fn = get_class_release_decl(g, sym, NULL);
        if (fn) build_class_release_body(g, sym, NULL, fn);
    }
    /* plus one per generic instantiation that was actually allocated, so its
     * type-parameter fields are released with their concrete types */
    for (int i = 0; g->site_inst && i < g->leak_site_count; i++) {
        zan_symbol_t *sym = g->site_syms ? g->site_syms[i] : NULL;
        zan_type_t *inst = g->site_inst[i];
        if (!sym || !inst || !inst->type_arg_count || !type_is_concrete(inst)) continue;
        int seen = 0;
        for (int j = 0; j < g->class_release_count && !seen; j++)
            if (g->class_release[j].sym == sym && g->class_release[j].inst &&
                types_equal(g->class_release[j].inst, inst))
                seen = 1;
        if (seen) continue;
        LLVMValueRef fn = get_class_release_decl(g, sym, inst);
        if (fn) build_class_release_body(g, sym, inst, fn);
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
            LLVMValueRef fn = get_class_release_decl(g, g->site_syms[i],
                                  g->site_inst ? g->site_inst[i] : NULL);
            if (fn) e = LLVMConstBitCast(fn, i8ptr);
        }
        elems[i] = e;
    }
    LLVMSetInitializer(g->g_site_dtors, LLVMConstArray(i8ptr, elems, (unsigned)n));
    free(elems);
}

/* Fill __zan_site_tynames[site] with a pointer to the site class's ancestor
 * name list: the class itself plus every base class, most-derived first, with
 * a trailing null. `x is T` (T a strict base of the static type) walks it to
 * test the object's runtime class against the target. Collections contribute
 * their intrinsic name (List/StringBuilder/Dict). Runs at finalize, after all
 * sites are registered. */
static void emit_site_tyname_table(zan_irgen_t *g) {
    if (!g->site_syms) return;
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    int n = ZAN_MAX_LEAK_SITES;
    LLVMValueRef *elems = (LLVMValueRef *)calloc((size_t)n, sizeof(LLVMValueRef));
    for (int i = 0; i < n; i++) {
        elems[i] = LLVMConstNull(i8ptr);
        if (i >= g->leak_site_count) continue;
        const char *names[64];
        int k = 0;
        if (g->site_coll && g->site_coll[i]) {
            int ck = g->site_coll[i];
            names[k++] = ck == 1 ? "List" : (ck == 2 ? "StringBuilder" : "Dict");
        } else if (g->site_syms[i]) {
            zan_symbol_t *cur = g->site_syms[i];
            while (cur && k < 63) {
                char buf[320];
                int len = (int)cur->name.len;
                if (len > 319) len = 319;
                memcpy(buf, cur->name.str, (size_t)len);
                buf[len] = 0;
                names[k++] = zan_arena_strdup(g->arena, buf, (size_t)len);
                cur = (cur->type && cur->type->base_type)
                          ? cur->type->base_type->sym : NULL;
            }
        }
        if (k == 0) continue;
        LLVMValueRef *nptr = (LLVMValueRef *)calloc((size_t)k + 1, sizeof(LLVMValueRef));
        for (int j = 0; j < k; j++)
            nptr[j] = LLVMBuildGlobalStringPtr(g->builder, names[j], "tn");
        nptr[k] = LLVMConstNull(i8ptr);
        LLVMTypeRef at = LLVMArrayType(i8ptr, (unsigned)k + 1);
        char gname[64];
        snprintf(gname, sizeof(gname), "__zan_tynames_%d", i);
        LLVMValueRef gv = LLVMAddGlobal(g->mod, at, gname);
        LLVMSetInitializer(gv, LLVMConstArray(i8ptr, nptr, (unsigned)k + 1));
        LLVMSetLinkage(gv, LLVMInternalLinkage);
        LLVMSetGlobalConstant(gv, 1);
        LLVMSetUnnamedAddr(gv, LLVMGlobalUnnamedAddr);
        free(nptr);
        LLVMValueRef idxs[] = { LLVMConstInt(i64, 0, 0), LLVMConstInt(i64, 0, 0) };
        elems[i] = LLVMBuildGEP2(g->builder, at, gv, idxs, 2, "tn.ptr");
    }
    LLVMSetInitializer(g->g_site_tynames, LLVMConstArray(i8ptr, elems, (unsigned)n));
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
    int i = irgen_find_function(g, msym);
    return i >= 0 ? g->functions[i].fn : NULL;
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
        /* The slot-defining decl list is as large as the slot count; a fixed
         * 128-entry stack array silently truncated deep hierarchies, leaving
         * the tail slots null and any virtual call through them a null call. */
        zan_symbol_t **decls = (zan_symbol_t **)calloc((size_t)n,
                                                       sizeof(zan_symbol_t *));
        int nd = 0;
        collect_vslot_decls(sym, decls, &nd, n);
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
        free(decls);
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
                v = zan_iwiden(g->builder, v, i64);
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
/* Like subst_type_param, but reaches inside type arguments as well, so a field
 * declared List<T> in Box<Square> reads back as List<Square> rather than
 * leaving T unresolved for whatever indexes it. */
static zan_type_t *subst_type_param_deep(zan_irgen_t *g, zan_type_t *t,
                                         zan_type_t *recv) {
    if (!t || !recv || !recv->sym || !recv->sym->decl) return t;
    zan_ast_list_t *tps = &recv->sym->decl->type_decl.type_params;
    if (tps->count == 0 || recv->type_arg_count < tps->count) return t;
    return zan_binder_subst_named(g->binder, t, tps, recv->type_args);
}

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
        return resolve_type_ctx(g, call->call.type_args.items[which]);
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
    if (a->kind == TYPE_ARRAY || a->kind == TYPE_NULLABLE) {
        if (a->kind == TYPE_ARRAY && a->array_rank != b->array_rank)
            return false; /* int[,] is not int[] */
        return types_equal(a->element_type, b->element_type);
    }
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
                             zan_ast_node_t *decl, zan_type_t **args, int argc,
                             int param_count,
                             LLVMValueRef fn, LLVMTypeRef fn_type) {
    if (g->generic_ctor_count >= g->generic_ctor_cap) {
        int ncap = g->generic_ctor_cap ? g->generic_ctor_cap * 2 : 64;
        g->generic_ctors = realloc(g->generic_ctors,
                                   (size_t)ncap * sizeof(*g->generic_ctors));
        g->generic_ctor_cap = ncap;
    }
    g->generic_ctors[g->generic_ctor_count].type_sym = type_sym;
    g->generic_ctors[g->generic_ctor_count].decl = decl;
    g->generic_ctors[g->generic_ctor_count].args = args;
    g->generic_ctors[g->generic_ctor_count].argc = argc;
    g->generic_ctors[g->generic_ctor_count].param_count = param_count;
    g->generic_ctors[g->generic_ctor_count].fn = fn;
    g->generic_ctors[g->generic_ctor_count].fn_type = fn_type;
    g->generic_ctor_count++;
}

static LLVMValueRef find_generic_ctor(zan_irgen_t *g, zan_symbol_t *type_sym,
                                      zan_ast_node_t *decl,
                                      zan_type_t **args, int argc,
                                      LLVMTypeRef *out_fn_type) {
    if (!type_sym || argc <= 0 || !args) return NULL;
    for (int i = 0; i < g->generic_ctor_count; i++) {
        if (g->generic_ctors[i].type_sym == type_sym &&
            g->generic_ctors[i].decl == decl &&
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
