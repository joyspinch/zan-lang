/* irgen_arc */
bool types_equal(zan_type_t *a, zan_type_t *b);
bool type_is_concrete(zan_type_t *t);
int type_is_binding(zan_type_t *t);
zan_type_t *subst_type_param_deep(zan_irgen_t *g, zan_type_t *t,
                                         zan_type_t *recv);
/* 下面三个定义在文件后半段（泛型函数表 / 类型名编码 / 泛型类判定），
 * vtable 那几处要用，先声明。 */
LLVMValueRef find_generic_fn(zan_irgen_t *g, zan_symbol_t *msym,
                                    zan_type_t **args, int argc,
                                    LLVMTypeRef *out_fn_type);
void mangle_inst_suffix(char *buf, size_t n, zan_type_t *inst);
bool is_user_generic_sym(zan_symbol_t *sym);
void emit_arc_retain(zan_irgen_t *g, LLVMValueRef v) {
    if (g->current_fn_no_runtime) return;
    if (!v || LLVMGetTypeKind(LLVMTypeOf(v)) != LLVMPointerTypeKind) return;
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    if (LLVMTypeOf(v) != i8ptr) v = LLVMBuildBitCast(g->builder, v, i8ptr, "arc.rt");
    zan_call2(g->builder,
        LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), &i8ptr, 1, 0),
        g->rt_retain, &v, 1, "");
}

static void emit_arc_release(zan_irgen_t *g, LLVMValueRef v) {
    if (g->current_fn_no_runtime) return;
    if (!v || LLVMGetTypeKind(LLVMTypeOf(v)) != LLVMPointerTypeKind) return;
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    if (LLVMTypeOf(v) != i8ptr) v = LLVMBuildBitCast(g->builder, v, i8ptr, "arc.rl");
    zan_call2(g->builder,
        LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), &i8ptr, 1, 0),
        g->rt_release, &v, 1, "");
}
void emit_string_retain(zan_irgen_t *g, LLVMValueRef v) {
    if (g->current_fn_no_runtime) return;
    if (!v || LLVMGetTypeKind(LLVMTypeOf(v)) != LLVMPointerTypeKind) return;
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    if (LLVMTypeOf(v) != i8ptr) v = LLVMBuildBitCast(g->builder, v, i8ptr, "str.rt");
    zan_call2(g->builder,
        LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), &i8ptr, 1, 0),
        g->rt_str_retain, &v, 1, "");
}
void emit_string_release(zan_irgen_t *g, LLVMValueRef v) {
    if (g->current_fn_no_runtime) return;
    if (!v || LLVMGetTypeKind(LLVMTypeOf(v)) != LLVMPointerTypeKind) return;
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    if (LLVMTypeOf(v) != i8ptr) v = LLVMBuildBitCast(g->builder, v, i8ptr, "str.rl");
    zan_call2(g->builder,
        LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), &i8ptr, 1, 0),
        g->rt_str_release, &v, 1, "");
}


static LLVMValueRef emit_closure_is_tagged(zan_irgen_t *g, LLVMValueRef v) {
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMValueRef iv = LLVMBuildPtrToInt(g->builder, v, i64, "clo.iv");
    LLVMValueRef bit = zan_and(g->builder, iv,
        LLVMConstInt(i64, ZAN_CLOSURE_TAG, 0), "clo.bit");
    return zan_icmp(g->builder, LLVMIntNE, bit, LLVMConstInt(i64, 0, 0), "clo.is");
}
LLVMValueRef emit_closure_untag(zan_irgen_t *g, LLVMValueRef v) {
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMValueRef iv = LLVMBuildPtrToInt(g->builder, v, i64, "clo.iv");
    LLVMValueRef cl = zan_and(g->builder, iv,
        LLVMConstInt(i64, ~(uint64_t)ZAN_CLOSURE_TAG, 0), "clo.clr");
    return LLVMBuildIntToPtr(g->builder, cl, i8ptr, "clo.rec");
}

#define ZAN_CLOSURE_HDR_FIELDS 3
LLVMTypeRef closure_header_type(zan_irgen_t *g) {
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef fields[ZAN_CLOSURE_HDR_FIELDS] = { i8ptr, i8ptr, i8ptr };
    return LLVMStructTypeInContext(g->ctx, fields, ZAN_CLOSURE_HDR_FIELDS, 0);
}
LLVMValueRef emit_delegate_equals(zan_irgen_t *g, LLVMValueRef a,
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
void emit_closure_retain(zan_irgen_t *g, LLVMValueRef v) {
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
void emit_closure_record_release(zan_irgen_t *g, LLVMValueRef rec) {
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
void emit_closure_release(zan_irgen_t *g, LLVMValueRef v) {
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
LLVMValueRef emit_delegate_invoke(zan_irgen_t *g, LLVMValueRef dv,
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
    LLVMValueRef rclo = zan_call_or_invoke(g, g->builder, clo_fn_type, cfn, cargs,
                                  (unsigned)argc + 1, is_void ? "" : "dlg.rc");
    free(cargs);
    LLVMBasicBlockRef clo_end = LLVMGetInsertBlock(g->builder);
    LLVMBuildBr(g->builder, join_bb);

    LLVMPositionBuilderAtEnd(g->builder, bare_bb);
    LLVMValueRef rbare = zan_call_or_invoke(g, g->builder, fn_type, dv, args,
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
void emit_rc_retain_for_type(zan_irgen_t *g, zan_type_t *type, LLVMValueRef v) {
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
void emit_rc_release_for_type(zan_irgen_t *g, zan_type_t *type, LLVMValueRef v) {
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
int type_contains_collection_rc(zan_irgen_t *g, zan_type_t *type,
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
void emit_collection_value_retain(zan_irgen_t *g, zan_type_t *type,
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
void emit_collection_value_release(zan_irgen_t *g, zan_type_t *type,
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
void emit_struct_local_release(zan_irgen_t *g, zan_type_t *type,
                                      LLVMValueRef slot) {
    LLVMTypeRef st = LLVMIsAAllocaInst(slot)
        ? LLVMGetAllocatedType(slot) : LLVMTypeOf(slot);
    if (!st || LLVMGetTypeKind(st) != LLVMStructTypeKind) return;
    LLVMValueRef cur = LLVMBuildLoad2(g->builder, st, slot, "arc.srel");
    emit_collection_value_release(g, type, cur, 0);
}
void emit_struct_local_retain(zan_irgen_t *g, zan_type_t *type,
                                     LLVMValueRef slot) {
    LLVMTypeRef st = LLVMIsAAllocaInst(slot)
        ? LLVMGetAllocatedType(slot) : LLVMTypeOf(slot);
    if (!st || LLVMGetTypeKind(st) != LLVMStructTypeKind) return;
    LLVMValueRef cur = LLVMBuildLoad2(g->builder, st, slot, "arc.sret");
    emit_collection_value_retain(g, type, cur, 0);
}
void emit_struct_local_capture(zan_irgen_t *g, zan_type_t *type,
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
void emit_struct_field_capture(zan_irgen_t *g, zan_type_t *ftype,
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
LLVMValueRef load_dict_value_words(zan_irgen_t *g, LLVMValueRef raw) {
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMValueRef dict = LLVMBuildBitCast(g->builder, raw,
        LLVMPointerType(g->dict_struct_type, 0), "dvw.dp");
    LLVMValueRef words_ptr = LLVMBuildStructGEP2(g->builder,
        g->dict_struct_type, dict, 7, "dvw.p");
    return LLVMBuildLoad2(g->builder, i64, words_ptr, "dvw");
}

/* irgen_expr.c 定义（TU 靠后）：int→decimal 的运行时构造转换（A548） */
LLVMValueRef coerce_to_decimal(zan_irgen_t *g, LLVMValueRef val);

/* 结构体按值语义归整。
 *
 * 表达式若是构造临时对象（new T(...)），emit_expr 返回的是该临时对象的
 * 地址（指针）；而按值消费方（集合槽位、结构体字段、函数实参）需要的是
 * 结构体值。变量实参则已是按值取出的结构体——两种形态不一致会让按值
 * 位置存进指针、读出来是垃圾。这里按声明类型统一归整：声明类型是值结构体
 * 且实参是指针时，先按值取出。 */
LLVMValueRef coerce_struct_value(zan_irgen_t *g, LLVMValueRef v,
                                        zan_type_t *t) {
    if (!v || !t || t->kind != TYPE_STRUCT) return v;
    LLVMTypeRef st = map_type(g, t);
    if (!st || LLVMGetTypeKind(st) != LLVMStructTypeKind) return v;
    if (LLVMGetTypeKind(LLVMTypeOf(v)) != LLVMPointerTypeKind) return v;
    return LLVMBuildLoad2(g->builder, st, v, "struct.sval");
}
void emit_collection_slot_store(zan_irgen_t *g, zan_type_t *elem_type,
                                       LLVMTypeRef slot_ty, LLVMValueRef slot_ptr,
                                       LLVMValueRef value,
                                       zan_ast_node_t *rhs, local_scope_t *locals,
                                       int overwrite_old) {
    LLVMTypeRef elem_llvm = elem_type ? map_type(g, elem_type) : slot_ty;
    LLVMTypeKind elem_kind = LLVMGetTypeKind(elem_llvm);
    /* 结构体元素按值存槽：实参是构造临时对象时先按值取出 */
    value = coerce_struct_value(g, value, elem_type);
    /* decimal 元素槽 + 整数右值：先走运行时构造转成 decimal，
     * 否则 i64 落进兜底裸 store，元素读回是 0/垃圾（A548）。 */
    if (elem_type && elem_type->kind == TYPE_DECIMAL &&
        LLVMGetTypeKind(LLVMTypeOf(value)) == LLVMIntegerTypeKind)
        value = coerce_to_decimal(g, value);
    /* A556-1 隐式装箱：object/接口元素槽收值形状实参——List.Add/索引器/
     * Dict 的内联快路径不经过 emit_arg_typed，在此统一收口。箱 fresh
     * owned（rhs 节点进 boxed 集合），元素槽捕获协议不再 retain。 */
    value = emit_implicit_box(g, elem_type, value, rhs, locals);
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
                    LLVMValueRef f32 = LLVMBuildFPTrunc(g->builder, stored,
                        LLVMFloatTypeInContext(g->ctx), "slot.f32d");
                    LLVMValueRef bits = LLVMBuildBitCast(g->builder, f32,
                        LLVMInt32TypeInContext(g->ctx), "slot.f32b");
                    stored = LLVMBuildZExt(g->builder, bits, slot_ty, "slot.f32w");
                } else {
                    stored = LLVMBuildBitCast(g->builder, stored, slot_ty, "slot.fb");
                }
            } else if (value_kind == LLVMFloatTypeKind) {
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
void emit_typed_out_store(zan_irgen_t *g, zan_type_t *value_type,
                                 LLVMValueRef out_ptr, LLVMValueRef value) {
    if (!value_type || !out_ptr || !value) return;
    LLVMTypeRef value_llvm = map_type(g, value_type);
    LLVMValueRef old = LLVMBuildLoad2(g->builder, value_llvm, out_ptr,
                                      "out.old");
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
void emit_collection_release_raw_slot(zan_irgen_t *g, zan_type_t *elem_type,
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
LLVMValueRef get_class_release_decl(zan_irgen_t *g, zan_symbol_t *sym,
                                          zan_type_t *inst) {
    if (!sym) return NULL;
    if (inst && (!inst->type_arg_count || !type_is_concrete(inst))) inst = NULL;
    for (int i = 0; i < g->class_release_count; i++) {
        if (g->class_release[i].sym != sym) continue;
        zan_type_t *ci = g->class_release[i].inst;
        if (ci == inst) return g->class_release[i].fn;
        if (ci && inst && types_equal(ci, inst)) return g->class_release[i].fn;
    }
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
void emit_list_release_elems(zan_irgen_t *g, zan_type_t *elem_type, LLVMValueRef col) {
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
void emit_dict_release_elems(zan_irgen_t *g, zan_type_t *dict_type, LLVMValueRef col) {
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
void emit_array_release_elems(zan_irgen_t *g, zan_type_t *elem_type,
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

static void emit_array_retain(zan_irgen_t *g, LLVMValueRef v) {
    if (!v || LLVMGetTypeKind(LLVMTypeOf(v)) != LLVMPointerTypeKind) return;
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMValueRef a = (LLVMTypeOf(v) == i8ptr) ? v
        : LLVMBuildBitCast(g->builder, v, i8ptr, "arr.rt8");
    zan_call2(g->builder,
        LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), &i8ptr, 1, 0),
        g->rt_arr_retain, &a, 1, "");
}
void mangle_type_token(char *buf, size_t n, size_t *off, zan_type_t *t);

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
LLVMValueRef get_array_release_decl(zan_irgen_t *g, zan_type_t *elem_type,
                                           int rect) {
    char tok[192];
    size_t off = 0;
    tok[0] = '\0';
    mangle_type_token(tok, sizeof(tok), &off, elem_type);
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
    zan_builder_point_t pt = zan_builder_save(g);
    di_clear(g);
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
    zan_builder_restore(g, pt);
    return fn;
}
LLVMValueRef get_array_desc(zan_irgen_t *g, zan_type_t *elem_type) {
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
        (LLVMValueRef[]){ dtor_c, z, z, z }, 4));
    return dg;
}

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
LLVMValueRef get_arc_free_decl(zan_irgen_t *g) {
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
    di_clear(g);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(c, fn, "entry");
    LLVMPositionBuilderAtEnd(b, entry);
    g->current_fn = fn;
    LLVMValueRef obj = LLVMGetParam(fn, 0);
    LLVMValueRef neg8 = LLVMConstInt(i64, (uint64_t)ZAN_OBJ_SITE_OFF, 1);
    LLVMValueRef site_ptr = LLVMBuildGEP2(b, LLVMInt8TypeInContext(c), obj, &neg8, 1, "sptr");
    LLVMValueRef site_iptr = LLVMBuildBitCast(b, site_ptr, LLVMPointerType(i64, 0), "siptr");
    LLVMValueRef site = LLVMBuildLoad2(b, i64, site_iptr, "site");
    LLVMValueRef is_stack = zan_icmp(b, LLVMIntNE,
        zan_and(b, site, LLVMConstInt(i64, ZAN_OBJ_STACK_FLAG, 0), "stkbit"),
        LLVMConstInt(i64, 0, 0), "is_stack");
    LLVMBasicBlockRef do_free_bb = LLVMAppendBasicBlockInContext(c, fn, "do_free");
    LLVMBasicBlockRef ret_bb = LLVMAppendBasicBlockInContext(c, fn, "ret");
    LLVMBuildCondBr(b, is_stack, ret_bb, do_free_bb);

    LLVMPositionBuilderAtEnd(b, do_free_bb);
    if (g->check_leaks) {
        emit_leak_counter_add(g, g->g_live, -1);
        LLVMValueRef ltbl = LLVMBuildLoad2(b, LLVMPointerType(i64, 0),
            g->g_site_live, "ltbl");
        LLVMValueRef sc_ptr = LLVMBuildGEP2(b, i64, ltbl, &site, 1, "scptr");
        emit_leak_counter_add(g, sc_ptr, -1);
    }
    if (g->rt_live_unregister) {
        zan_call2(b, LLVMGlobalGetValueType(g->rt_live_unregister),
            g->rt_live_unregister, &obj, 1, "");
    }
    if (g->arc_guard) {
        LLVMValueRef neg16 = LLVMConstInt(i64, (unsigned long long)ZAN_OBJ_RC_OFF, 1);
        LLVMValueRef rcp = LLVMBuildGEP2(b, LLVMInt8TypeInContext(c), obj, &neg16, 1, "rcp");
        LLVMValueRef rcip = LLVMBuildBitCast(b, rcp, LLVMPointerType(i64, 0), "rcip");
        emit_arc_quarantine(g, obj, rcip);
    } else {
        LLVMValueRef neg16 = LLVMConstInt(i64, (unsigned long long)ZAN_OBJ_RC_OFF, 1);
        LLVMValueRef header_ptr = LLVMBuildGEP2(b, LLVMInt8TypeInContext(c), obj, &neg16, 1, "hdr");
        LLVMTypeRef free_fn_type = LLVMFunctionType(LLVMVoidTypeInContext(c), &i8ptr, 1, 0);
        zan_call2(b, free_fn_type, g->fn_free, &header_ptr, 1, "");
    }
    LLVMBuildBr(b, ret_bb);

    LLVMPositionBuilderAtEnd(b, ret_bb);
    LLVMBuildRetVoid(b);
    g->current_fn = saved_fn;
    if (saved_bb) LLVMPositionBuilderAtEnd(b, saved_bb);
    return fn;
