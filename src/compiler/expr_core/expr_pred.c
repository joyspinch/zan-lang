bool expr_is_ulong(zan_irgen_t *g, zan_ast_node_t *e, local_scope_t *locals) {
    zan_type_t *t = infer_expr_type(g, e, locals);
    return t && t->kind == TYPE_ULONG;
}
bool expr_is_uint(zan_irgen_t *g, zan_ast_node_t *e, local_scope_t *locals) {
    zan_type_t *t = infer_expr_type(g, e, locals);
    return t && t->kind == TYPE_UINT;
}
bool expr_is_longish(zan_irgen_t *g, zan_ast_node_t *e, local_scope_t *locals) {
    zan_type_t *t = infer_expr_type(g, e, locals);
    return t && (t->kind == TYPE_LONG || t->kind == TYPE_ULONG);
}
bool expr_is_int32(zan_irgen_t *g, zan_ast_node_t *e, local_scope_t *locals) {
    zan_type_t *t = infer_expr_type(g, e, locals);
    return t && t->kind == TYPE_INT;
}
bool expr_is_bool(zan_irgen_t *g, zan_ast_node_t *e, local_scope_t *locals) {
    zan_type_t *t = infer_expr_type(g, e, locals);
    return t && t->kind == TYPE_BOOL;
}
bool expr_is_unsigned_int(zan_irgen_t *g, zan_ast_node_t *e,
                                 local_scope_t *locals) {
    zan_type_t *t = infer_expr_type(g, e, locals);
    if (!t) return false;
    if (t->kind == TYPE_ULONG || t->kind == TYPE_UINT ||
        t->kind == TYPE_USHORT || t->kind == TYPE_BYTE)
        return true;
    return false;
}
bool expr_is_char(zan_irgen_t *g, zan_ast_node_t *e, local_scope_t *locals) {
    zan_type_t *t = infer_expr_type(g, e, locals);
    return t && t->kind == TYPE_CHAR;
}
zan_symbol_t *expr_class_sym(zan_irgen_t *g, zan_ast_node_t *e,
                                    local_scope_t *locals) {
    zan_type_t *t = infer_expr_type(g, e, locals);
    if (t && t->kind == TYPE_TYPE_PARAM) t = concretize(g, t);
    if (t && (t->kind == TYPE_CLASS || t->kind == TYPE_STRUCT)) return t->sym;
    return NULL;
}

/* 返回the field symbol when `e` is a read of a weak instance field */
static zan_symbol_t *weak_field_read_sym(zan_irgen_t *g, zan_ast_node_t *e,
                                         local_scope_t *locals) {
    zan_symbol_t *field = NULL;
    if (!e) return NULL;
    if (e->kind == AST_IDENTIFIER) {
        if (local_find(locals, e->ident.name) || !g->current_type_sym)
            return NULL;
        if (get_field_index(g->current_type_sym, e->ident.name) >= 0)
            field = get_field_sym(g->current_type_sym, e->ident.name);
        if (field && field_member_is_static(field)) return NULL;
    } else if (e->kind == AST_MEMBER_ACCESS && !e->member.null_cond) {
        zan_symbol_t *owner = expr_class_sym(g, e->member.object, locals);
        if (!owner) return NULL;
        if (get_field_index(owner, e->member.name) >= 0)
            field = get_field_sym(owner, e->member.name);
        if (field && field_member_is_static(field)) return NULL;
    }
    return field && (field->modifiers & MOD_WEAK) ? field : NULL;
}
void emit_weak_read_guard(zan_irgen_t *g, zan_ast_node_t *read_expr,
                                 LLVMValueRef value, zan_loc_t loc,
                                 local_scope_t *locals) {
    zan_type_t *rt = infer_expr_type(g, read_expr, locals);
    bool reflike = rt && (rt->kind == TYPE_OBJECT || rt->kind == TYPE_CLASS ||
                          rt->kind == TYPE_INTERFACE ||
                          rt->kind == TYPE_TYPE_PARAM ||
                          rt->kind == TYPE_ERROR);
    if (reflike && value &&
        LLVMGetTypeKind(LLVMTypeOf(value)) == LLVMPointerTypeKind) {
        LLVMValueRef is_null = zan_icmp(g->builder, LLVMIntEQ, value,
                                        LLVMConstNull(LLVMTypeOf(value)),
                                        "recv.null");
        char msg[256];
        if (read_expr && read_expr->kind == AST_IDENTIFIER)
            snprintf(msg, sizeof(msg),
                     "null reference: receiver '%.*s' is null",
                     (int)read_expr->ident.name.len, read_expr->ident.name.str);
        else
            snprintf(msg, sizeof(msg),
                     "null reference where an object is required (member access)");
        emit_runtime_check(g, is_null, loc, msg);
    }
    zan_symbol_t *field = weak_field_read_sym(g, read_expr, locals);
    if (!field || !value ||
        LLVMGetTypeKind(LLVMTypeOf(value)) != LLVMPointerTypeKind)
        return;
    LLVMTypeRef ptr_type = LLVMTypeOf(value);
    LLVMValueRef is_null = zan_icmp(g->builder, LLVMIntEQ, value,
                                    LLVMConstNull(ptr_type), "weak.null");
    char msg[256];
    snprintf(msg, sizeof(msg),
             "null reference: weak field '%.*s' was cleared",
             (int)field->name.len, field->name.str);
    emit_runtime_check(g, is_null, loc, msg);
}
LLVMValueRef emit_guarded_member_object_ex(zan_irgen_t *g,
                                                  zan_ast_node_t *member,
                                                  local_scope_t *locals,
                                                  bool async_gate) {
    LLVMValueRef value = emit_expr(g, member->member.object, locals);
    if (member && !member->member.null_cond)
        emit_weak_read_guard(g, member->member.object, value,
                             member->member.object->loc, locals);
    if (member && value &&
        LLVMGetTypeKind(LLVMTypeOf(value)) == LLVMPointerTypeKind) {
        zan_type_t *rt = infer_expr_type(g, member->member.object, locals);
        bool reflike = rt && (rt->kind == TYPE_OBJECT || rt->kind == TYPE_CLASS ||
                              rt->kind == TYPE_INTERFACE ||
                              rt->kind == TYPE_TYPE_PARAM ||
                              rt->kind == TYPE_ERROR);
        if (reflike && !member->member.null_cond) {
            LLVMValueRef isnull = zan_icmp(g->builder, LLVMIntEQ, value,
                                           LLVMConstNull(LLVMTypeOf(value)),
                                           "recv.null");
            /* A550：async 被调者 + null 接收者不能走软基址——scratch 会被
             * 当协程帧调度，协程序言的虚表派发/retain 读哨兵字节。走到这里
             * = 不在 try（在 try 的 emit_weak_read_guard 已 longjmp 转
             * NRE），async 调用没有有效续延，响亮中止；同步调用维持软基址
             * （成员访问触发软诊断，报告后继续）。 */
            if (async_gate) {
                LLVMValueRef gfn = LLVMGetBasicBlockParent(
                    LLVMGetInsertBlock(g->builder));
                LLVMBasicBlockRef live_bb = LLVMAppendBasicBlockInContext(
                    g->ctx, gfn, "recv.live");
                LLVMBasicBlockRef dead_bb = LLVMAppendBasicBlockInContext(
                    g->ctx, gfn, "recv.dead");
                LLVMBuildCondBr(g->builder, isnull, dead_bb, live_bb);
                LLVMPositionBuilderAtEnd(g->builder, dead_bb);
                LLVMTypeRef abty = LLVMFunctionType(
                    LLVMVoidTypeInContext(g->ctx), NULL, 0, 0);
                zan_call2(g->builder, abty,
                    get_libc_fn(g, "abort", abty), NULL, 0, "");
                LLVMBuildUnreachable(g->builder);
                LLVMPositionBuilderAtEnd(g->builder, live_bb);
                return value;
            }
            value = emit_soft_base_select(g, value, isnull,
                                          member->member.object->loc);
        }
    }
    return value;
}

/* 读路径（字段/下标/属性等）维持原口径：软基址替换，报告后继续。 */
LLVMValueRef emit_guarded_member_object(zan_irgen_t *g,
                                               zan_ast_node_t *member,
                                               local_scope_t *locals) {
    return emit_guarded_member_object_ex(g, member, locals, false);
}
bool class_implements_iface(zan_symbol_t *cls, zan_symbol_t *iface) {
    /* 与 checker 共用 binder 里的同一份判定：两边对"接口方法算不算这个
     * 类型的成员"必须口径一致，否则 checker 报"没有这个方法"而 irgen
     * 又能把同一个调用发出去。 */
    return zan_binder_class_implements_iface(cls, iface);
}
zan_symbol_t *get_method_sym(zan_symbol_t *cls, zan_istr_t name);
bool zan_type_defines(zan_irgen_t *g, const char *type_name,
                             const char *method) {
    if (!g || !g->binder) return false;
    zan_istr_t tn = { (char *)type_name, (uint32_t)strlen(type_name) };
    zan_symbol_t *sym = zan_binder_lookup(g->binder, tn);
    if (!sym || !sym->type) return false;
    if (sym->type->kind != TYPE_CLASS && sym->type->kind != TYPE_STRUCT)
        return false;
    zan_istr_t mn = { (char *)method, (uint32_t)strlen(method) };
    return get_method_sym(sym, mn) != NULL;
}

/* A443：Convert.ToString 的真重载进 stdlib 后，内建特判只该在「实参与
 * 某个 ToString 重载的形参**精确同型**」时让路——精确匹配的实参由静态
 * 解析（irgen_call.c 的 resolve_overload_typed 路径）发真方法；其余形状
 * （可空/object/枚举/数组/协变数值）继续走内建：内建的形状覆盖面
 * （可空 emit_to_cstr_u、object 指针拷贝、class/struct/array/delegate 的
 * A463 拒绝措辞）是 reject/golden 锁住的既有行为，真方法的参数表表达
 * 不了，宽容打分更会把可空的 box 指针当 i64 送进真方法（LLVM verify 炸）。 */
bool zan_convert_tostring_exact_decl(zan_irgen_t *g,
                                            zan_ast_node_t *arg,
                                            local_scope_t *locals) {
    if (!g || !g->binder || !arg) return false;
    zan_istr_t tn = { "Convert", 7 };
    zan_symbol_t *sym = zan_binder_lookup(g->binder, tn);
    if (!sym || !sym->type ||
        (sym->type->kind != TYPE_CLASS && sym->type->kind != TYPE_STRUCT))
        return false;
    zan_type_t *at = infer_expr_type(g, arg, locals);
    if (!at || type_mentions_tp(at)) return false;
    for (int i = 0; i < sym->member_count; i++) {
        zan_symbol_t *m = sym->members[i];
        if (m->kind != SYM_METHOD || !m->decl ||
            m->decl->kind != AST_METHOD_DECL) continue;
        if (m->name.len != 8 || memcmp(m->name.str, "ToString", 8) != 0)
            continue;
        if (m->decl->method_decl.params.count != 1) continue;
        zan_type_t *pt = method_param_type(g, m, 0);
        if (pt && types_concrete_equal(pt, at)) return true;
    }
    return false;
}
void emit_runtime_check(zan_irgen_t *g, LLVMValueRef is_error,
                               zan_loc_t loc, const char *msg);

static void zan_arr_hdr_store(zan_irgen_t *g, LLVMValueRef raw, int off,
                              LLVMValueRef val, const char *name) {
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
    LLVMValueRef o = LLVMConstInt(i64, (unsigned long long)off, 0);
    LLVMValueRef p = LLVMBuildGEP2(g->builder, i8, raw, &o, 1, name);
    LLVMBuildStore(g->builder, val,
        LLVMBuildBitCast(g->builder, p, LLVMPointerType(i64, 0), name));
}

static LLVMValueRef zan_array_alloc_impl(zan_irgen_t *g, LLVMValueRef total,
                                         LLVMValueRef count, zan_type_t *elem_type) {
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
    LLVMValueRef max_i64 = LLVMConstInt(i64, UINT64_MAX, 0);
    LLVMValueRef header = LLVMConstInt(i64, ZAN_ARR_HDR_SIZE, 0);
    LLVMValueRef max_total = LLVMBuildSub(g->builder, max_i64, header, "arr.max");
    LLVMValueRef overflow = zan_icmp(g->builder, LLVMIntUGT, total,
        max_total, "arr.overflow");
    emit_runtime_check(g, overflow, (zan_loc_t){0}, "array allocation overflow");
    total = LLVMBuildSelect(g->builder, overflow,
        LLVMConstInt(i64, 0, 0), total, "arr.total.safe");
    count = LLVMBuildSelect(g->builder, overflow,
        LLVMConstInt(i64, 0, 0), count, "arr.count.safe");
    LLVMValueRef bytes = LLVMBuildAdd(g->builder, total, header, "arr.bytes");
    LLVMValueRef raw = zan_call2(g->builder,
        LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i64, i64 }, 2, 0),
        get_calloc_fn(g), (LLVMValueRef[]){ bytes, LLVMConstInt(i64, 1, 0) }, 2, "arr.raw");
    LLVMValueRef null_raw = LLVMBuildIsNull(g->builder, raw, "arr.null");
    emit_runtime_check(g, null_raw, (zan_loc_t){0}, "array allocation failed");
    emit_rt_alloc_add(g, bytes);
    zan_arr_hdr_store(g, raw, ZAN_ARR_HDR_SIZE + ZAN_ARR_RC_OFF,
                      LLVMConstInt(i64, 1, 0), "arr.rc");
    zan_arr_hdr_store(g, raw, ZAN_ARR_HDR_SIZE + ZAN_ARR_RC_MAGIC_OFF,
                      LLVMConstInt(i64, ZAN_ARRAY_RC_MAGIC, 0), "arr.rcmagic");
    zan_arr_hdr_store(g, raw, ZAN_ARR_HDR_SIZE + ZAN_OBJ_RC_OFF, count, "arr.count");
    if (elem_type && is_rc_managed_type(elem_type)) {
        LLVMValueRef desc = get_array_desc(g, elem_type);
        LLVMValueRef desc_i64 = LLVMBuildPtrToInt(g->builder, desc, i64, "arr.desci");
        zan_arr_hdr_store(g, raw, ZAN_ARR_HDR_SIZE + ZAN_OBJ_SITE_OFF,
                          desc_i64, "arr.desc");
    } else {
        zan_arr_hdr_store(g, raw, ZAN_ARR_HDR_SIZE + ZAN_OBJ_SITE_OFF,
                          LLVMConstInt(i64, ZAN_ARRAY_MAGIC, 0), "arr.magic");
    }
    LLVMValueRef off = LLVMConstInt(i64, ZAN_ARR_HDR_SIZE, 0);
    LLVMValueRef arr_ptr = LLVMBuildGEP2(g->builder, i8, raw, &off, 1, "arr");
    if (g->rt_live_register) {
        zan_call2(g->builder, LLVMGlobalGetValueType(g->rt_live_register),
            g->rt_live_register, (LLVMValueRef[]){ arr_ptr, bytes }, 2, "");
    }
    return arr_ptr;
}
LLVMValueRef zan_array_alloc_typed(zan_irgen_t *g, LLVMValueRef total,
                                          LLVMValueRef count, zan_type_t *elem_type) {
    return zan_array_alloc_impl(g, total, count, elem_type);
}
LLVMValueRef zan_array_alloc(zan_irgen_t *g, LLVMValueRef total,
                                    LLVMValueRef count) {
    return zan_array_alloc_impl(g, total, count, NULL);
}
LLVMValueRef zan_array_len(zan_irgen_t *g, LLVMValueRef arr) {
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
    LLVMValueRef off = LLVMConstInt(i64, (unsigned long long)ZAN_OBJ_RC_OFF, 1);
    LLVMValueRef hdr = LLVMBuildGEP2(g->builder, i8, arr, &off, 1, "arr.hdrp");
    return LLVMBuildLoad2(g->builder, i64,
        LLVMBuildBitCast(g->builder, hdr, LLVMPointerType(i64, 0), "arr.hdrc"),
        "arr.len");
}
LLVMValueRef zan_mdarray_alloc(zan_irgen_t *g, LLVMValueRef *dims,
                                      int rank, LLVMTypeRef elem_llvm) {
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
    LLVMValueRef total = LLVMConstInt(i64, 1, 0);
    for (int d = 0; d < rank; d++)
        total = zan_mul(g->builder, total, dims[d], "md.total");
    LLVMValueRef neg = zan_icmp(g->builder, LLVMIntSLT, total,
        LLVMConstInt(i64, 0, 0), "md.neg");
    emit_runtime_check(g, neg, (zan_loc_t){0}, "array allocation overflow");
    total = LLVMBuildSelect(g->builder, neg,
        LLVMConstInt(i64, 0, 0), total, "md.total.safe");
    LLVMValueRef hdr = LLVMConstInt(i64,
        (unsigned long long)(ZAN_ARR_HDR_SIZE + 8 * rank), 0);
    LLVMValueRef bytes = zan_add(g->builder, hdr,
        zan_mul(g->builder, total, LLVMSizeOf(elem_llvm), "md.bytes"), "md.totalb");
    LLVMValueRef nbytes = zan_icmp(g->builder, LLVMIntSLT, bytes,
        LLVMConstInt(i64, 0, 0), "md.nb");
    emit_runtime_check(g, nbytes, (zan_loc_t){0}, "array allocation overflow");
    bytes = LLVMBuildSelect(g->builder, nbytes,
        LLVMConstInt(i64, ZAN_ARR_HDR_SIZE + 8 * rank, 0), bytes, "md.bytes.safe");
    LLVMValueRef raw = zan_call2(g->builder,
        LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i64, i64 }, 2, 0),
        get_calloc_fn(g), (LLVMValueRef[]){ bytes, LLVMConstInt(i64, 1, 0) }, 2, "md.raw");
    LLVMValueRef null_raw = LLVMBuildIsNull(g->builder, raw, "md.null");
    emit_runtime_check(g, null_raw, (zan_loc_t){0}, "array allocation failed");
    emit_rt_alloc_add(g, bytes);
    LLVMValueRef hdri64 = LLVMBuildBitCast(g->builder, raw,
        LLVMPointerType(i64, 0), "md.hdr");
    zan_arr_hdr_store(g, raw, ZAN_ARR_HDR_SIZE + ZAN_ARR_RC_OFF,
                      LLVMConstInt(i64, 1, 0), "md.rc");
    zan_arr_hdr_store(g, raw, ZAN_ARR_HDR_SIZE + ZAN_ARR_RC_MAGIC_OFF,
                      LLVMConstInt(i64, ZAN_ARRAY_RC_MAGIC, 0), "md.rcmagic");
    zan_arr_hdr_store(g, raw, ZAN_ARR_HDR_SIZE + ZAN_OBJ_RC_OFF, total, "md.count");
    zan_arr_hdr_store(g, raw, ZAN_ARR_HDR_SIZE + ZAN_OBJ_SITE_OFF,
                      LLVMConstInt(i64, rank, 0), "md.rank");
    for (int d = 0; d < rank; d++) {
        LLVMValueRef off = LLVMConstInt(i64,
            ZAN_ARR_HDR_SIZE / 8 + (unsigned long long)d, 0);
        LLVMBuildStore(g->builder, dims[d],
            LLVMBuildGEP2(g->builder, i64, hdri64, &off, 1, "md.dim"));
    }
    LLVMValueRef hdr_off = LLVMConstInt(i64, ZAN_ARR_HDR_SIZE, 0);
    LLVMValueRef arr_ptr = LLVMBuildGEP2(g->builder, i8, raw, &hdr_off, 1, "md.arr");
    if (g->rt_live_register) {
        zan_call2(g->builder, LLVMGlobalGetValueType(g->rt_live_register),
            g->rt_live_register, (LLVMValueRef[]){ arr_ptr, bytes }, 2, "");
    }
    return arr_ptr;
}

static LLVMValueRef zan_itoa_fn(zan_irgen_t *g) {
    LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "__zan_itoa");
    if (fn) return fn;
    LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
    LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef fnty = LLVMFunctionType(i64t,
        (LLVMTypeRef[]){ i8ptr, i64t, i32t }, 3, 0);
    fn = LLVMAddFunction(g->mod, "__zan_itoa", fnty);
    LLVMSetLinkage(fn, LLVMInternalLinkage);

    LLVMBasicBlockRef saved = LLVMGetInsertBlock(g->builder);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
    LLVMBasicBlockRef loop = LLVMAppendBasicBlockInContext(g->ctx, fn, "loop");
    LLVMBasicBlockRef sign = LLVMAppendBasicBlockInContext(g->ctx, fn, "sign");
    LLVMBasicBlockRef neg_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "neg");
    LLVMBasicBlockRef copy = LLVMAppendBasicBlockInContext(g->ctx, fn, "copy");

    LLVMPositionBuilderAtEnd(g->builder, entry);
    LLVMValueRef buf = LLVMGetParam(fn, 0);
    LLVMValueRef val = LLVMGetParam(fn, 1);
    LLVMValueRef uns = LLVMGetParam(fn, 2);
    LLVMTypeRef tmp_ty = LLVMArrayType(i8, 24);
    LLVMValueRef tmp = LLVMBuildAlloca(g->builder, tmp_ty, "itoa.tmp");
    LLVMValueRef zero64 = LLVMConstInt(i64t, 0, 0);
    LLVMValueRef ten = LLVMConstInt(i64t, 10, 0);
    LLVMValueRef is_signed = zan_icmp(g->builder, LLVMIntEQ, uns,
        LLVMConstInt(i32t, 0, 0), "itoa.signed");
    LLVMValueRef is_lt0 = zan_icmp(g->builder, LLVMIntSLT, val, zero64, "itoa.lt0");
    LLVMValueRef is_neg = LLVMBuildAnd(g->builder, is_signed, is_lt0, "itoa.neg");
    LLVMValueRef mag = LLVMBuildSelect(g->builder, is_neg,
        LLVMBuildNeg(g->builder, val, "itoa.abs"), val, "itoa.mag");
    LLVMBuildBr(g->builder, loop);

    LLVMPositionBuilderAtEnd(g->builder, loop);
    LLVMValueRef v = LLVMBuildPhi(g->builder, i64t, "itoa.v");
    LLVMValueRef pos = LLVMBuildPhi(g->builder, i64t, "itoa.pos");
    LLVMValueRef q = LLVMBuildUDiv(g->builder, v, ten, "itoa.q");
    LLVMValueRef r = LLVMBuildURem(g->builder, v, ten, "itoa.r");
    LLVMValueRef next_pos = LLVMBuildSub(g->builder, pos,
        LLVMConstInt(i64t, 1, 0), "itoa.pos1");
    LLVMValueRef digit = LLVMBuildTrunc(g->builder,
        LLVMBuildAdd(g->builder, r, LLVMConstInt(i64t, '0', 0), "itoa.ch"),
        i8, "itoa.ch8");
    LLVMValueRef gep_idx[] = { zero64, next_pos };
    LLVMBuildStore(g->builder, digit,
        LLVMBuildGEP2(g->builder, tmp_ty, tmp, gep_idx, 2, "itoa.dp"));
    LLVMValueRef more = zan_icmp(g->builder, LLVMIntNE, q, zero64, "itoa.more");
    LLVMBuildCondBr(g->builder, more, loop, sign);
    LLVMAddIncoming(v, (LLVMValueRef[]){ mag, q },
        (LLVMBasicBlockRef[]){ entry, loop }, 2);
    LLVMAddIncoming(pos, (LLVMValueRef[]){ LLVMConstInt(i64t, 24, 0), next_pos },
        (LLVMBasicBlockRef[]){ entry, loop }, 2);

    LLVMPositionBuilderAtEnd(g->builder, sign);
    LLVMBuildCondBr(g->builder, is_neg, neg_bb, copy);

    LLVMPositionBuilderAtEnd(g->builder, neg_bb);
    LLVMValueRef minus_pos = LLVMBuildSub(g->builder, next_pos,
        LLVMConstInt(i64t, 1, 0), "itoa.mpos");
    LLVMValueRef minus_idx[] = { zero64, minus_pos };
    LLVMBuildStore(g->builder, LLVMConstInt(i8, '-', 0),
        LLVMBuildGEP2(g->builder, tmp_ty, tmp, minus_idx, 2, "itoa.mp"));
    LLVMBuildBr(g->builder, copy);

    LLVMPositionBuilderAtEnd(g->builder, copy);
    LLVMValueRef start = LLVMBuildPhi(g->builder, i64t, "itoa.start");
    LLVMAddIncoming(start, (LLVMValueRef[]){ next_pos, minus_pos },
        (LLVMBasicBlockRef[]){ sign, neg_bb }, 2);
    LLVMValueRef len = LLVMBuildSub(g->builder, LLVMConstInt(i64t, 24, 0),
        start, "itoa.len");
    LLVMValueRef src_idx[] = { zero64, start };
    LLVMValueRef src = LLVMBuildGEP2(g->builder, tmp_ty, tmp, src_idx, 2, "itoa.src");
    LLVMTypeRef memcpy_ty = LLVMFunctionType(i8ptr,
        (LLVMTypeRef[]){ i8ptr, i8ptr, i64t }, 3, 0);
    zan_call2(g->builder, memcpy_ty, get_libc_fn(g, "memcpy", memcpy_ty),
        (LLVMValueRef[]){ buf, src, len }, 3, "");
    LLVMBuildStore(g->builder, LLVMConstInt(i8, 0, 0),
        LLVMBuildGEP2(g->builder, i8, buf, &len, 1, "itoa.end"));
    LLVMBuildRet(g->builder, len);

    if (saved) LLVMPositionBuilderAtEnd(g->builder, saved);
    return fn;
}

/* 发射`__zan_itoa(buf, val, is_unsigned)`; `val` must already be i64 */
static LLVMValueRef zan_emit_itoa(zan_irgen_t *g, LLVMValueRef buf,
                                 LLVMValueRef val, bool is_unsigned) {
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef fnty = LLVMFunctionType(i64t,
        (LLVMTypeRef[]){ i8ptr, i64t, i32t }, 3, 0);
    return zan_call2(g->builder, fnty, zan_itoa_fn(g),
        (LLVMValueRef[]){ buf, val, LLVMConstInt(i32t, is_unsigned ? 1 : 0, 0) },
        3, "itoa.n");
}
bool istr_matches_class(zan_istr_t name, const char *cls) {
    if (!name.str) return false;
    size_t len = strlen(cls);
    if (name.len == len && memcmp(name.str, cls, len) == 0) return true;
    if (name.len > len + 1 && name.str[name.len - len - 1] == '_' &&
        memcmp(name.str + (name.len - len), cls, len) == 0) {
        return true;
    }
    return false;
}
bool is_call_to(zan_ast_node_t *expr, const char *obj, const char *method) {
    if (expr->kind != AST_CALL) return false;
    if (expr->call.resolved_sym != NULL && !expr->call.is_intrinsic) return false;
    zan_ast_node_t *callee = expr->call.callee;
    if (callee->kind != AST_MEMBER_ACCESS) return false;

    zan_istr_t obj_name = {NULL, 0};
    if (callee->member.object->kind == AST_IDENTIFIER) {
        obj_name = callee->member.object->ident.name;
    } else if (callee->member.object->kind == AST_MEMBER_ACCESS) {
        obj_name = callee->member.object->member.name;
    } else {
        return false;
    }

    zan_istr_t method_name = callee->member.name;
    size_t method_len = strlen(method);

    bool obj_ok = istr_matches_class(obj_name, obj);
    if (!obj_ok && expr->call.resolved_sym && expr->call.resolved_sym->parent) {
        obj_ok = istr_matches_class(expr->call.resolved_sym->parent->name, obj);
    }
    if (!obj_ok) return false;

    if (method_name.len != method_len || memcmp(method_name.str, method, method_len) != 0)
        return false;
    return true;
}
