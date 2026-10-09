/* irgen_arc */

/* 内部辅助实现 */

static bool types_equal(zan_type_t *a, zan_type_t *b);
static bool type_is_concrete(zan_type_t *t);
static int type_is_binding(zan_type_t *t);
static zan_type_t *subst_type_param_deep(zan_irgen_t *g, zan_type_t *t,
                                         zan_type_t *recv);

static void emit_arc_retain(zan_irgen_t *g, LLVMValueRef v) {
    if (g->current_fn_no_runtime) return;   /* 核心系统底层抽象与内存语义契约 */
    if (!v || LLVMGetTypeKind(LLVMTypeOf(v)) != LLVMPointerTypeKind) return;
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    if (LLVMTypeOf(v) != i8ptr) v = LLVMBuildBitCast(g->builder, v, i8ptr, "arc.rt");
    zan_call2(g->builder,
        LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), &i8ptr, 1, 0),
        g->rt_retain, &v, 1, "");
}

static void emit_arc_release(zan_irgen_t *g, LLVMValueRef v) {
    if (g->current_fn_no_runtime) return;   /* 核心系统底层抽象与内存语义契约 */
    if (!v || LLVMGetTypeKind(LLVMTypeOf(v)) != LLVMPointerTypeKind) return;
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    if (LLVMTypeOf(v) != i8ptr) v = LLVMBuildBitCast(g->builder, v, i8ptr, "arc.rl");
    zan_call2(g->builder,
        LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), &i8ptr, 1, 0),
        g->rt_release, &v, 1, "");
}

static void emit_string_retain(zan_irgen_t *g, LLVMValueRef v) {
    if (g->current_fn_no_runtime) return;   /* 核心系统底层抽象与内存语义契约 */
    if (!v || LLVMGetTypeKind(LLVMTypeOf(v)) != LLVMPointerTypeKind) return;
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    if (LLVMTypeOf(v) != i8ptr) v = LLVMBuildBitCast(g->builder, v, i8ptr, "str.rt");
    zan_call2(g->builder,
        LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), &i8ptr, 1, 0),
        g->rt_str_retain, &v, 1, "");
}

static void emit_string_release(zan_irgen_t *g, LLVMValueRef v) {
    if (g->current_fn_no_runtime) return;   /* 核心系统底层抽象与内存语义契约 */
    if (!v || LLVMGetTypeKind(LLVMTypeOf(v)) != LLVMPointerTypeKind) return;
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    if (LLVMTypeOf(v) != i8ptr) v = LLVMBuildBitCast(g->builder, v, i8ptr, "str.rl");
    zan_call2(g->builder,
        LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), &i8ptr, 1, 0),
        g->rt_str_release, &v, 1, "");
}

/* 内部辅助实现 */

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

/* 内部辅助逻辑 */
#define ZAN_CLOSURE_HDR_FIELDS 3
static LLVMTypeRef closure_header_type(zan_irgen_t *g) {
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef fields[ZAN_CLOSURE_HDR_FIELDS] = { i8ptr, i8ptr, i8ptr };
    return LLVMStructTypeInContext(g->ctx, fields, ZAN_CLOSURE_HDR_FIELDS, 0);
}

/* 内部辅助逻辑 */
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

/* 内部辅助实现 */
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

/* 内部辅助实现 */
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
    } else if (type->kind == TYPE_OBJECT) {
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
    } else if (type->kind == TYPE_OBJECT) {
        emit_release_obj_value(g, v);
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

/* 内部辅助实现 */

static void emit_struct_local_release(zan_irgen_t *g, zan_type_t *type,
                                      LLVMValueRef slot) {
    LLVMTypeRef st = LLVMIsAAllocaInst(slot)
        ? LLVMGetAllocatedType(slot) : LLVMTypeOf(slot);
    if (!st || LLVMGetTypeKind(st) != LLVMStructTypeKind) return;
    LLVMValueRef cur = LLVMBuildLoad2(g->builder, st, slot, "arc.srel");
    emit_collection_value_release(g, type, cur, 0);
}

static void emit_struct_local_retain(zan_irgen_t *g, zan_type_t *type,
                                     LLVMValueRef slot) {
    LLVMTypeRef st = LLVMIsAAllocaInst(slot)
        ? LLVMGetAllocatedType(slot) : LLVMTypeOf(slot);
    if (!st || LLVMGetTypeKind(st) != LLVMStructTypeKind) return;
    LLVMValueRef cur = LLVMBuildLoad2(g->builder, st, slot, "arc.sret");
    emit_collection_value_retain(g, type, cur, 0);
}

/* 内部辅助逻辑 */
static void emit_struct_local_capture(zan_irgen_t *g, zan_type_t *type,
                                      LLVMValueRef slot_alloca,
                                      LLVMValueRef v, zan_ast_node_t *rhs,
                                      local_scope_t *locals) {
    LLVMTypeRef st = LLVMIsAAllocaInst(slot_alloca)
        ? LLVMGetAllocatedType(slot_alloca) : map_type(g, type);
    if (!st || LLVMGetTypeKind(st) != LLVMStructTypeKind) return;
    LLVMValueRef old = LLVMBuildLoad2(g->builder, st, slot_alloca, "arc.sold");
    if (!expr_yields_owned_rc_value(g, rhs, locals))
        emit_collection_value_retain(g, type, v, 0);
    zan_store_fit(g, v, slot_alloca);
    emit_collection_value_release(g, type, old, 0);
}

/* 核心系统底层抽象与内存语义契约 */
static void emit_struct_field_capture(zan_irgen_t *g, zan_type_t *ftype,
                                      LLVMValueRef field_ptr, LLVMValueRef v,
                                      zan_ast_node_t *rhs,
                                      local_scope_t *locals) {
    LLVMTypeRef st = map_type(g, ftype);
    if (!st || LLVMGetTypeKind(st) != LLVMStructTypeKind) return;
    if (LLVMGetTypeKind(LLVMTypeOf(v)) == LLVMPointerTypeKind)
        v = LLVMBuildLoad2(g->builder, st, v, "arc.sld");
    LLVMValueRef old = LLVMBuildLoad2(g->builder, st, field_ptr, "arc.sold");
    if (!expr_yields_owned_rc_value(g, rhs, locals))
        emit_collection_value_retain(g, ftype, v, 0);
    zan_store_fit(g, v, field_ptr);
    emit_collection_value_release(g, ftype, old, 0);
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
    /* 内部辅助逻辑 */
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
                if (elem_llvm && LLVMGetTypeKind(elem_llvm) == LLVMFloatTypeKind) {
                    /* 内部辅助实现 */
                    LLVMValueRef f32 = LLVMBuildFPTrunc(g->builder, stored,
                        LLVMFloatTypeInContext(g->ctx), "slot.f32d");
                    LLVMValueRef bits = LLVMBuildBitCast(g->builder, f32,
                        LLVMInt32TypeInContext(g->ctx), "slot.f32b");
                    stored = LLVMBuildZExt(g->builder, bits, slot_ty, "slot.f32w");
                } else {
                    stored = LLVMBuildBitCast(g->builder, stored, slot_ty, "slot.fb");
                }
            } else if (value_kind == LLVMFloatTypeKind) {
                /* 内部辅助逻辑 */
                LLVMValueRef bits = LLVMBuildBitCast(g->builder, stored,
                    LLVMInt32TypeInContext(g->ctx), "slot.f32b");
                stored = LLVMBuildZExt(g->builder, bits, slot_ty, "slot.f32w");
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
    /* 核心系统底层抽象与内存语义契约 */
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

/* 内部辅助实现 */

/* 内部辅助逻辑 */
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
    /* 模块核心语义抽象与接口调用契约 */
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

/* 内部辅助实现 */
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

/* 内部辅助实现 */
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

/* 底层系统交互与数据协议契约 */
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

/* 内部辅助逻辑 */
static void emit_array_retain(zan_irgen_t *g, LLVMValueRef v) {
    if (!v || LLVMGetTypeKind(LLVMTypeOf(v)) != LLVMPointerTypeKind) return;
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMValueRef a = (LLVMTypeOf(v) == i8ptr) ? v
        : LLVMBuildBitCast(g->builder, v, i8ptr, "arr.rt8");
    zan_call2(g->builder,
        LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), &i8ptr, 1, 0),
        g->rt_arr_retain, &a, 1, "");
}

/* 内部辅助实现 */
static void mangle_type_token(char *buf, size_t n, size_t *off, zan_type_t *t);

/* 内部辅助逻辑 */
static void arr_rel_hash_mix(uint64_t *h, const void *data, size_t len) {
    const unsigned char *p = (const unsigned char *)data;
    for (size_t i = 0; i < len; i++) {
        *h ^= (uint64_t)p[i];
        *h *= 0x100000001b3ULL;
    }
}

static void arr_rel_hash_type(uint64_t *h, zan_type_t *t) {
    if (!t) { unsigned char c = 'N'; arr_rel_hash_mix(h, &c, 1); return; }
    unsigned char kind = (unsigned char)t->kind;
    arr_rel_hash_mix(h, &kind, 1);
    unsigned short nlen = (unsigned short)t->name.len;
    arr_rel_hash_mix(h, &nlen, sizeof(nlen));
    if (t->name.str && t->name.len)
        arr_rel_hash_mix(h, t->name.str, t->name.len);
    for (int i = 0; i < t->type_arg_count; i++)
        arr_rel_hash_type(h, t->type_args[i]);
    if (t->kind == TYPE_ARRAY || t->kind == TYPE_NULLABLE) {
        unsigned char e = 'E';
        arr_rel_hash_mix(h, &e, 1);
        arr_rel_hash_type(h, t->element_type);
    }
    unsigned char end = ';';
    arr_rel_hash_mix(h, &end, 1);
}

static uint64_t arr_rel_type_key(zan_type_t *t) {
    uint64_t h = 0xcbf29ce484222325ULL;
    arr_rel_hash_type(&h, t);
    return h;
}

static LLVMValueRef get_array_release_decl(zan_irgen_t *g, zan_type_t *elem_type,
                                           int rect) {
    char tok[192];
    size_t off = 0;
    tok[0] = '\0';
    mangle_type_token(tok, sizeof(tok), &off, elem_type);
    /* 内部辅助实现 */
    char name[256];
    snprintf(name, sizeof(name), "__zan_arr_release_%s_%016llx%s",
             tok, (unsigned long long)arr_rel_type_key(elem_type),
             rect ? "_md" : "");
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
    di_clear(g); /* 底层系统交互与数据协议契约 */
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
    /* 模块核心语义抽象与接口调用契约 */
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
        /* 内部辅助逻辑 */
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

static LLVMValueRef get_array_desc(zan_irgen_t *g, zan_type_t *elem_type) {
    char tok[192];
    size_t off = 0;
    tok[0] = '\0';
    mangle_type_token(tok, sizeof(tok), &off, elem_type);
    char name[256];
    snprintf(name, sizeof(name), "__zan_arr_desc_%s_%016llx",
             tok, (unsigned long long)arr_rel_type_key(elem_type));
    LLVMValueRef eg = LLVMGetNamedGlobal(g->mod, name);
    if (eg) return eg;

    LLVMValueRef dtor = get_array_release_decl(g, elem_type, 0);
    LLVMTypeRef i8p = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef dt = arc_desc_type(g);
    LLVMValueRef dg = LLVMAddGlobal(g->mod, dt, name);
    LLVMSetLinkage(dg, LLVMInternalLinkage);
    LLVMSetGlobalConstant(dg, 1);
    LLVMValueRef z = LLVMConstNull(i8p);
    LLVMValueRef dtor_c = LLVMConstBitCast(dtor, i8p);
    LLVMSetInitializer(dg, LLVMConstNamedStruct(dt,
        (LLVMValueRef[]){ dtor_c, z, z, LLVMConstInt(i64t, 0, 0) }, 4));
    return dg;
}

/* 内部辅助逻辑 */
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

/* 内部辅助逻辑 */
static LLVMValueRef get_arc_free_decl(zan_irgen_t *g) {
    LLVMValueRef existing = LLVMGetNamedFunction(g->mod, "__zan_arc_free");
    if (existing) return existing;
    LLVMContextRef c = g->ctx;
    LLVMBuilderRef b = g->builder;
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(c), 0);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(c);
    LLVMTypeRef ft = LLVMFunctionType(LLVMVoidTypeInContext(c), &i8ptr, 1, 0);
    LLVMValueRef fn = LLVMAddFunction(g->mod, "__zan_arc_free", ft);
    LLVMSetLinkage(fn, LLVMInternalLinkage);
    LLVMBasicBlockRef saved_bb = LLVMGetInsertBlock(b);
    LLVMValueRef saved_fn = g->current_fn;
    di_clear(g); /* 底层系统交互与数据协议契约 */
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(c, fn, "entry");
    LLVMPositionBuilderAtEnd(b, entry);
    g->current_fn = fn;
    LLVMValueRef obj = LLVMGetParam(fn, 0);
    if (g->check_leaks) {
        /* 编译器代码生成与运行时系统底层调用契约 */
        LLVMValueRef neg8 = LLVMConstInt(i64, (uint64_t)ZAN_OBJ_SITE_OFF, 1);
        LLVMValueRef site_ptr = LLVMBuildGEP2(b, LLVMInt8TypeInContext(c), obj, &neg8, 1, "sptr");
        LLVMValueRef site_iptr = LLVMBuildBitCast(b, site_ptr, LLVMPointerType(i64, 0), "siptr");
        LLVMValueRef site = LLVMBuildLoad2(b, i64, site_iptr, "site");
        /* 编译器代码生成与运行时系统底层调用契约 */
        emit_leak_counter_add(g, g->g_live, -1);
        LLVMValueRef ltbl = LLVMBuildLoad2(b, LLVMPointerType(i64, 0),
            g->g_site_live, "ltbl");
        LLVMValueRef sc_ptr = LLVMBuildGEP2(b, i64, ltbl, &site, 1, "scptr");
        emit_leak_counter_add(g, sc_ptr, -1);
    }
    if (g->arc_guard) {
        LLVMValueRef neg16 = LLVMConstInt(i64, (unsigned long long)ZAN_OBJ_RC_OFF, 1);
        LLVMValueRef rcp = LLVMBuildGEP2(b, LLVMInt8TypeInContext(c), obj, &neg16, 1, "rcp");
        LLVMValueRef rcip = LLVMBuildBitCast(b, rcp, LLVMPointerType(i64, 0), "rcip");
        emit_arc_quarantine(g, obj, rcip);
    } else {
        /* 核心系统底层抽象与内存语义契约 */
        LLVMValueRef neg16 = LLVMConstInt(i64, (unsigned long long)ZAN_OBJ_RC_OFF, 1);
        LLVMValueRef header_ptr = LLVMBuildGEP2(b, LLVMInt8TypeInContext(c), obj, &neg16, 1, "hdr");
        LLVMTypeRef free_fn_type = LLVMFunctionType(LLVMVoidTypeInContext(c), &i8ptr, 1, 0);
        zan_call2(b, free_fn_type, g->fn_free, &header_ptr, 1, "");
    }
    LLVMBuildRetVoid(b);
    g->current_fn = saved_fn;
    if (saved_bb) LLVMPositionBuilderAtEnd(b, saved_bb);
    return fn;
}

/* 内部辅助实现 */
static void build_class_release_body(zan_irgen_t *g, zan_symbol_t *sym,
                                    zan_type_t *inst, LLVMValueRef fn) {
    di_clear(g); /* 底层系统交互与数据协议契约 */
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
    /* 核心系统底层抽象与内存语义契约 */
    LLVMValueRef rc_old = LLVMBuildAtomicRMW(b, LLVMAtomicRMWBinOpSub, rcip,
        LLVMConstInt(i64, 1, 0), LLVMAtomicOrderingAcquireRelease, 0);
    LLVMValueRef is1 = zan_icmp(b, LLVMIntEQ, rc_old, LLVMConstInt(i64, 1, 0), "is1");
    LLVMValueRef over = zan_icmp(b, LLVMIntSLE, rc_old, LLVMConstInt(i64, 0, 0), "over");
    LLVMBasicBlockRef last_bb = LLVMAppendBasicBlockInContext(c, fn, "last");
    LLVMBuildCondBr(b, over, dorel, last_bb);
    LLVMPositionBuilderAtEnd(b, last_bb);
    LLVMBasicBlockRef begin_bb = LLVMAppendBasicBlockInContext(c, fn, "begin");
    LLVMBuildCondBr(b, is1, begin_bb, ret);
    /* 内部辅助实现 */
    LLVMPositionBuilderAtEnd(b, begin_bb);
    LLVMValueRef weak_ok = zan_call2(b,
        LLVMGlobalGetValueType(g->rt_weak_destroy_begin),
        g->rt_weak_destroy_begin, &obj, 1, "weak.begin");
    LLVMBuildCondBr(b, weak_ok, relf, ret);
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
        /* 模块核心语义抽象与接口调用契约 */
        if (inst) ft = subst_type_param_deep(g, ft, inst);
        if (!ft || ft->kind == TYPE_TYPE_PARAM) continue;
        /* 模块核心语义抽象与接口调用契约 */
        if (type_is_binding(inst ? inst : sym->type) &&
            m->name.len == 6 && memcmp(m->name.str, "target", 6) == 0)
            continue;
        /* 内部辅助逻辑 */
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
            /* 内部辅助实现 */
            LLVMValueRef fp = LLVMBuildStructGEP2(g->builder, structT, self,
                                                  (unsigned)idx, "fp");
            LLVMValueRef dv = LLVMBuildLoad2(g->builder, i8ptr, fp, "fd");
            emit_closure_release(g, dv);
        } else if (ft->kind == TYPE_ARRAY) {
            /* 内部辅助实现 */
            LLVMValueRef fp = LLVMBuildStructGEP2(b, structT, self, (unsigned)idx, "fp");
            LLVMValueRef av = LLVMBuildLoad2(b, i8ptr, fp, "fa");
            emit_array_release(g, ft, av);
        } else if (is_arc_managed_type(ft)) {
            /* 内部辅助逻辑 */
            LLVMValueRef fp = LLVMBuildStructGEP2(b, structT, self, (unsigned)idx, "fp");
            LLVMValueRef cv = LLVMBuildLoad2(b, map_type(g, ft), fp, "fc");
            emit_arc_release_typed(g, ft, cv);
        } else if (ft->kind == TYPE_OBJECT) {
            LLVMValueRef fp = LLVMBuildStructGEP2(b, structT, self, (unsigned)idx, "fp");
            LLVMValueRef cv = LLVMBuildLoad2(b, i8ptr, fp, "fc");
            emit_release_obj_value(g, cv);
        }
    }
    /* 内部辅助逻辑 */
    LLVMBasicBlockRef freebb = LLVMAppendBasicBlockInContext(c, fn, "freebb");
    LLVMBuildBr(b, freebb);
    LLVMPositionBuilderAtEnd(b, freebb);
    LLVMValueRef freefn = get_arc_free_decl(g);
    zan_call2(b, LLVMGlobalGetValueType(freefn), freefn, &obj, 1, "");
    LLVMBuildBr(b, ret);
    LLVMPositionBuilderAtEnd(b, dorel);
    zan_call2(b, LLVMFunctionType(LLVMVoidTypeInContext(c), &i8ptr, 1, 0),
                   g->rt_release, &obj, 1, "");
    LLVMBuildBr(b, ret);
    LLVMPositionBuilderAtEnd(b, ret);
    LLVMBuildRetVoid(b);
}

/* 内部辅助实现 */
static void build_collection_release_body(zan_irgen_t *g, int coll_kind,
                                          zan_type_t *elem_type, LLVMValueRef fn) {
    di_clear(g); /* 底层系统交互与数据协议契约 */
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
    /* 核心系统底层抽象与内存语义契约 */
    LLVMValueRef rc_old = LLVMBuildAtomicRMW(b, LLVMAtomicRMWBinOpSub, rcip,
        LLVMConstInt(i64, 1, 0), LLVMAtomicOrderingAcquireRelease, 0);
    LLVMValueRef is1 = zan_icmp(b, LLVMIntEQ, rc_old, LLVMConstInt(i64, 1, 0), "is1");
    LLVMValueRef over = zan_icmp(b, LLVMIntSLE, rc_old, LLVMConstInt(i64, 0, 0), "over");
    LLVMBasicBlockRef last_bb = LLVMAppendBasicBlockInContext(c, fn, "last");
    LLVMBuildCondBr(b, over, dorel, last_bb);
    LLVMPositionBuilderAtEnd(b, last_bb);
    LLVMBuildCondBr(b, is1, relf, ret);
    LLVMPositionBuilderAtEnd(b, relf);
    LLVMTypeRef free_ty = LLVMFunctionType(LLVMVoidTypeInContext(c), &i8ptr, 1, 0);
    if (coll_kind == 1) {
        /* 模块核心语义抽象与接口调用契约 */
        LLVMValueRef lp = LLVMBuildBitCast(b, obj, LLVMPointerType(g->list_struct_type, 0), "lp");
        LLVMValueRef dp = LLVMBuildStructGEP2(b, g->list_struct_type, lp, 2, "dp");
        LLVMValueRef data = LLVMBuildLoad2(b, LLVMPointerType(i64, 0), dp, "data");
        emit_list_release_elems(g, elem_type, obj);
        LLVMValueRef d8 = LLVMBuildBitCast(b, data, i8ptr, "d8");
        zan_call2(b, free_ty, g->fn_free, &d8, 1, "");
    } else if (coll_kind == 3) {
        /* 内部辅助逻辑 */
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
        /* 模块核心语义抽象与接口调用契约 */
        LLVMValueRef sp = LLVMBuildBitCast(b, obj, LLVMPointerType(g->sb_struct_type, 0), "sp");
        LLVMValueRef dp = LLVMBuildStructGEP2(b, g->sb_struct_type, sp, 2, "dp");
        LLVMValueRef data = LLVMBuildLoad2(b, i8ptr, dp, "data");
        zan_call2(b, free_ty, g->fn_free, &data, 1, "");
    }
    LLVMBasicBlockRef freebb = LLVMAppendBasicBlockInContext(c, fn, "freebb");
    LLVMBuildBr(b, freebb);
    LLVMPositionBuilderAtEnd(b, freebb);
    LLVMValueRef freefn = get_arc_free_decl(g);
    zan_call2(b, LLVMGlobalGetValueType(freefn), freefn, &obj, 1, "");
    LLVMBuildBr(b, ret);
    LLVMPositionBuilderAtEnd(b, dorel);
    zan_call2(b, LLVMFunctionType(LLVMVoidTypeInContext(c), &i8ptr, 1, 0),
                   g->rt_release, &obj, 1, "");
    LLVMBuildBr(b, ret);
    LLVMPositionBuilderAtEnd(b, ret);
    LLVMBuildRetVoid(b);
}

/* 内部辅助逻辑 */
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

/* 内部辅助逻辑 */
static void emit_arc_release_typed(zan_irgen_t *g, zan_type_t *type, LLVMValueRef v) {
    (void)type;
    if (!v || LLVMGetTypeKind(LLVMTypeOf(v)) != LLVMPointerTypeKind) return;
    /* 内部辅助实现 */
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    if (LLVMTypeOf(v) != i8ptr) v = LLVMBuildBitCast(g->builder, v, i8ptr, "arc.rlt");
    zan_call2(g->builder, LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), &i8ptr, 1, 0),
                   g->rt_release_dyn, &v, 1, "");
}

/* 构建the bodies of every class release function */
static void emit_all_class_releases(zan_irgen_t *g) {
    for (int i = 0; i < g->struct_type_count; i++) {
        zan_symbol_t *sym = g->struct_types[i].sym;
        if (!sym || !sym->type || !is_arc_managed_type(sym->type)) continue;
        LLVMValueRef fn = get_class_release_decl(g, sym, NULL);
        if (fn) build_class_release_body(g, sym, NULL, fn);
    }
    /* 内部辅助逻辑 */
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

/* 内部辅助实现 */
static void emit_site_live_tables(zan_irgen_t *g) {
    if (g->desc_hdr || !g->g_site_live || !g->g_site_count) return;
    LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i8p = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    unsigned n = (unsigned)g->leak_site_count;
    LLVMTypeRef lt = LLVMArrayType(i64t, n);
    LLVMValueRef live = LLVMAddGlobal(g->mod, lt, "__zan_site_live_tbl");
    LLVMSetInitializer(live, LLVMConstNull(lt));
    LLVMSetLinkage(live, LLVMInternalLinkage);
    LLVMSetInitializer(g->g_site_live,
        LLVMConstBitCast(live, LLVMPointerType(i64t, 0)));
    LLVMTypeRef nt = LLVMArrayType(i8p, n);
    LLVMValueRef names = LLVMAddGlobal(g->mod, nt, "__zan_site_names_tbl");
    LLVMSetInitializer(names, LLVMConstNull(nt));
    LLVMSetLinkage(names, LLVMInternalLinkage);
    LLVMSetInitializer(g->g_site_names,
        LLVMConstBitCast(names, LLVMPointerType(i8p, 0)));
    LLVMSetInitializer(g->g_site_count, LLVMConstInt(i64t, n, 0));
}

/* 内部辅助实现 */
static void emit_site_dtor_table(zan_irgen_t *g) {
    if (!g->site_syms || g->desc_hdr) return;
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    int n = g->leak_site_count;
    if (n <= 0) return;
    LLVMValueRef *elems = (LLVMValueRef *)calloc((size_t)n, sizeof(LLVMValueRef));
    for (int i = 0; i < n; i++) {
        LLVMValueRef e = LLVMConstNull(i8ptr);
        if (g->site_coll && g->site_coll[i]) {
            LLVMValueRef fn = get_collection_release_decl(g, i);
            if (fn) e = LLVMConstBitCast(fn, i8ptr);
        } else if (g->site_syms[i]) {
            LLVMValueRef fn = get_class_release_decl(g, g->site_syms[i],
                                  g->site_inst ? g->site_inst[i] : NULL);
            if (fn) e = LLVMConstBitCast(fn, i8ptr);
        }
        elems[i] = e;
    }
    LLVMTypeRef at = LLVMArrayType(i8ptr, (unsigned)n);
    LLVMValueRef arr = LLVMAddGlobal(g->mod, at, "__zan_site_dtor_tbl");
    LLVMSetInitializer(arr, LLVMConstArray(i8ptr, elems, (unsigned)n));
    LLVMSetLinkage(arr, LLVMInternalLinkage);
    LLVMSetInitializer(g->g_site_dtors,
        LLVMConstBitCast(arr, LLVMPointerType(i8ptr, 0)));
    free(elems);
}

/* 内部辅助实现 */
static void emit_site_tyname_table(zan_irgen_t *g) {
    if (!g->site_syms || g->desc_hdr) return;
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    int n = g->leak_site_count;
    if (n <= 0) return;
    LLVMValueRef *elems = (LLVMValueRef *)calloc((size_t)n, sizeof(LLVMValueRef));
    for (int i = 0; i < n; i++) {
        elems[i] = LLVMConstNull(i8ptr);
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
            nptr[j] = zan_irgen_intern_string(g, names[j]);
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
    LLVMTypeRef at = LLVMArrayType(i8ptr, (unsigned)n);
    LLVMValueRef arr = LLVMAddGlobal(g->mod, at, "__zan_site_tyname_tbl");
    LLVMSetInitializer(arr, LLVMConstArray(i8ptr, elems, (unsigned)n));
    LLVMSetLinkage(arr, LLVMInternalLinkage);
    LLVMSetInitializer(g->g_site_tynames,
        LLVMConstBitCast(arr, LLVMPointerType(i8ptr, 0)));
    free(elems);
}

/* 内部辅助实现 */
void zan_irgen_emit_arc_desc_init(zan_irgen_t *g);

/* 内部辅助实现 */

static LLVMValueRef find_fn_for_sym(zan_irgen_t *g, zan_symbol_t *msym) {
    if (!msym) return NULL;
    int i = irgen_find_function(g, msym);
    return i >= 0 ? g->functions[i].fn : NULL;
}

/* 内部辅助逻辑 */
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

/* 内部辅助逻辑 */
static void emit_vtables(zan_irgen_t *g) {
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    for (int i = 0; i < g->struct_type_count; i++) {
        zan_symbol_t *sym = g->struct_types[i].sym;
        if (!sym || !class_has_virtual_methods(sym)) continue;
        int n = count_virtual_methods(sym);
        if (n < 1) continue;
        /* 内部辅助逻辑 */
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
                zan_symbol_t *impl = resolve_overload(sym, decl->name, arity, 0);
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

/* 内部辅助逻辑 */
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

/* 内部辅助逻辑 */
static void coerce_args_to_params(zan_irgen_t *g, LLVMTypeRef fn_type,
                                  LLVMValueRef *call_args, int argc) {
    unsigned npt = LLVMCountParamTypes(fn_type);
    bool va = LLVMIsFunctionVarArg(fn_type) != 0;
    if ((npt == 0 && !va) || argc <= 0) return;
    LLVMTypeRef *pts = (LLVMTypeRef *)calloc((size_t)(npt > 0 ? npt : 1),
                                             sizeof(LLVMTypeRef));
    if (npt > 0) LLVMGetParamTypes(fn_type, pts);
    int n = (argc < (int)npt) ? argc : (int)npt;
    for (int i = 0; i < n; i++)
        call_args[i] = emit_boundary_coerce(g, call_args[i], pts[i]);
    free(pts);
    /* 内部辅助实现 */
    for (int i = (int)npt; va && i < argc; i++) {
        LLVMValueRef v = call_args[i];
        if (!v) continue;
        LLVMTypeRef t = LLVMTypeOf(v);
        if (LLVMGetTypeKind(t) == LLVMIntegerTypeKind &&
            LLVMGetIntTypeWidth(t) < 32)
            call_args[i] = (LLVMGetIntTypeWidth(t) == 1)
                ? LLVMBuildZExt(g->builder, v,
                    LLVMInt32TypeInContext(g->ctx), "va.prom")
                : LLVMBuildSExt(g->builder, v,
                    LLVMInt32TypeInContext(g->ctx), "va.prom");
        else if (LLVMGetTypeKind(t) == LLVMFloatTypeKind)
            call_args[i] = LLVMBuildFPExt(g->builder, v,
                LLVMDoubleTypeInContext(g->ctx), "va.prom");
    }
}

/* 内部辅助实现 */
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

/* 模块核心语义抽象与接口调用契约 */
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

/* 内部辅助实现 */
static bool types_equal(zan_type_t *a, zan_type_t *b) {
    if (a == b) return true;
    if (!a || !b) return false;
    if (a->kind != b->kind) return false;
    if (a->name.len != b->name.len ||
        (a->name.len && memcmp(a->name.str, b->name.str, (size_t)a->name.len) != 0))
        return false;
    if (a->kind == TYPE_ARRAY || a->kind == TYPE_NULLABLE) {
        if (a->kind == TYPE_ARRAY && a->array_rank != b->array_rank)
            return false; /* 核心系统底层抽象与内存语义契约 */
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

/* 内部辅助逻辑 */
static bool type_is_concrete(zan_type_t *t) {
    if (!t) return false;
    if (t->kind == TYPE_TYPE_PARAM || t->kind == TYPE_ERROR) return false;
    if (t->kind == TYPE_ARRAY || t->kind == TYPE_NULLABLE)
        return type_is_concrete(t->element_type);
    for (int i = 0; i < t->type_arg_count; i++)
        if (!type_is_concrete(t->type_args[i])) return false;
    return true;
}

/* 内部辅助逻辑 */
static zan_type_t *concretize(zan_irgen_t *g, zan_type_t *t) {
    if (!g->cur_inst) return t;
    return subst_type_param(t, g->cur_inst);
}

/* 内部辅助逻辑 */
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

/* 内部辅助逻辑 */
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

/* 底层系统交互与数据协议契约 */
static void add_generic_inst(zan_irgen_t *g, zan_type_t *inst) {
    if (!inst || !inst->sym || inst->type_arg_count <= 0) return;
    if (!is_user_generic_sym(inst->sym)) return;
    if (!type_is_concrete(inst)) return;
    for (int i = 0; i < g->generic_inst_count; i++)
        if (g->generic_insts[i].type_sym == inst->sym &&
            type_arglists_equal(g->generic_insts[i].inst->type_args,
                                g->generic_insts[i].inst->type_arg_count,
                                inst->type_args, inst->type_arg_count))
            return; /* 核心系统底层抽象与内存语义契约 */
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

/* 编译器代码生成与运行时系统底层调用契约 */
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

/* 构建the function-name suffix for an instantiation, e */
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
