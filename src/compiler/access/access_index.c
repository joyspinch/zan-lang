/* 临时容器对象访问完成后及时释放引用 */
static LLVMValueRef finish_index_of_temp(zan_irgen_t *g, zan_ast_node_t *expr,
                                         local_scope_t *locals,
                                         zan_type_t *container_type,
                                         zan_type_t *elem_type,
                                         LLVMValueRef container,
                                         LLVMValueRef elem) {
    if (!container || !container_type ||
        !expr_yields_owned_rc_value(g, expr->index.object, locals) ||
        expr_is_local_ident(expr->index.object, locals) ||
        !is_rc_managed_type(container_type))
        return elem;
    if (elem_type && is_rc_managed_type(elem_type) &&
        LLVMGetTypeKind(LLVMTypeOf(elem)) == LLVMPointerTypeKind)
        emit_rc_retain_for_type(g, elem_type, elem);
    emit_rc_release_for_type(g, container_type, container);
    return elem;
}
LLVMValueRef emit_expr_index(zan_irgen_t *g, zan_ast_node_t *expr,
        local_scope_t *locals) {
        /* 实例索引器 obj[i] 降解为 op_index 方法调用 */
        {
            zan_type_t *oit = infer_expr_type(g, expr->index.object, locals);
            if (oit && (oit->kind == TYPE_CLASS || oit->kind == TYPE_STRUCT) &&
                oit->sym) {
                zan_istr_t op_istr = {(char *)"op_index", 8};
                zan_ast_node_t *op_call = zan_ast_new(g->arena, AST_CALL, expr->loc);
                op_call->call.callee = NULL;
                zan_ast_list_init(&op_call->call.args);
                zan_ast_list_init(&op_call->call.type_args);
                zan_ast_list_push(&op_call->call.args, expr->index.index, g->arena);
                zan_symbol_t *op_sym = resolve_op_overload(g, oit->sym,
                                                           op_istr, op_call, locals);
                if (op_sym) {
                    for (int fi = irgen_find_function(g, op_sym); fi >= 0; fi = -1) {
                        if (g->functions[fi].sym == op_sym) {
                            LLVMValueRef recv_val = emit_expr(g, expr->index.object, locals);
                            LLVMValueRef *call_args = (LLVMValueRef *)calloc(2, sizeof(LLVMValueRef));
                            call_args[0] = recv_val;
                            if (LLVMGetTypeKind(LLVMTypeOf(recv_val)) == LLVMStructTypeKind) {
                                LLVMValueRef rslot = emit_entry_alloca(g,
                                    LLVMTypeOf(recv_val), "opx.recv");
                                LLVMBuildStore(g->builder, recv_val, rslot);
                                call_args[0] = rslot;
                            }
                            int recv_eh_pushed = 0;
                            if (oit->kind == TYPE_CLASS &&
                                !expr_is_local_ident(expr->index.object, locals) &&
                                expr_yields_owned_rc_value(g, expr->index.object, locals) &&
                                LLVMGetTypeKind(LLVMTypeOf(recv_val)) == LLVMPointerTypeKind) {
                                emit_eh_tmp_push(g, recv_val);
                                recv_eh_pushed = 1;
                            }
                            call_args[1] = emit_arg_typed(g, expr->index.index,
                                method_param_type_at(g, op_sym,
                                    op_index_param_offset(op_sym), NULL,
                                    expr->index.object, locals), locals);
                            LLVMTypeRef mft = g->functions[fi].fn_type;
                            LLVMValueRef mfn = route_generic_method(g, oit,
                                op_sym, g->functions[fi].fn, mft, &mft);
                            const char *cn = (LLVMGetTypeKind(LLVMGetReturnType(mft)) == LLVMVoidTypeKind) ? "" : "opx";
                            LLVMValueRef result = emit_dispatch_call(g,
                                oit->sym, op_sym, mfn, mft, call_args, 2, cn);
                            result = coerce_generic_result(g, result, op_sym, oit);
                            if (recv_eh_pushed) emit_eh_tmp_pop(g);
                            emit_release_owned_call_temp(g, expr->index.object,
                                recv_val, locals);
                            emit_release_owned_call_temp(g, expr->index.index,
                                call_args[1], locals);
                            free(call_args);
                            return result;
                        }
                    }
                }
            }
        }
        {
            zan_type_t *sot = infer_expr_type(g, expr->index.object, locals);
            if (is_span_type(sot)) {
                zan_type_t *et = container_elem_type(sot);
                LLVMTypeRef elem_llvm = span_elem_storage_type(g, et);
                LLVMValueRef span_val = emit_expr(g, expr->index.object, locals);
                LLVMValueRef base = LLVMBuildExtractValue(g->builder, span_val, 0, "spx.base");
                LLVMValueRef typed = LLVMBuildBitCast(g->builder, base,
                    LLVMPointerType(elem_llvm, 0), "spx.p");
                LLVMValueRef idx = emit_expr(g, expr->index.index, locals);
                LLVMValueRef span_len = LLVMBuildExtractValue(g->builder, span_val,
                    1, "spx.len");
                emit_index_bounds_check(g, idx, span_len, expr->loc, "span");
                if (LLVMGetTypeKind(LLVMTypeOf(idx)) == LLVMIntegerTypeKind &&
                    LLVMGetIntTypeWidth(LLVMTypeOf(idx)) < 64)
                    idx = LLVMBuildSExt(g->builder, idx,
                        LLVMInt64TypeInContext(g->ctx), "spx.ix");
                idx = emit_index_safe_bounds(g, idx, span_len, expr->loc, "span");
                LLVMValueRef ep = LLVMBuildGEP2(g->builder, elem_llvm, typed, &idx, 1, "spx.ep");
                LLVMValueRef ld = LLVMBuildLoad2(g->builder, elem_llvm, ep, "spx.elem");
                LLVMSetAlignment(ld, 1);
                return promote_loaded(g, ld, et);
            }
        }
        LLVMValueRef arr_ptr = NULL;
        zan_type_t *arr_type = NULL;
        int is_list = 0;
        if (expr->index.object->kind == AST_IDENTIFIER) {
            local_var_t *local = local_find(locals, expr->index.object->ident.name);
            if (local) {
                arr_ptr = LLVMBuildLoad2(g->builder, LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0),
                    local->alloca, "arrload");
                arr_type = local->type;
                if (arr_type && type_named(arr_type, "List", 4)) {
                    is_list = 1;
                }
            } else if (g->current_type_sym) {
                zan_symbol_t *fsym = get_field_sym(g->current_type_sym, expr->index.object->ident.name);
                if (fsym) {
                    arr_type = fsym->type;
                    arr_ptr = emit_expr(g, expr->index.object, locals);
                    if (arr_type && type_named(arr_type, "List", 4)) {
                        is_list = 1;
                    }
                }
            }
        } else if (expr->index.object->kind == AST_MEMBER_ACCESS) {
            /* 字段数组索引访问：加载字段指针后进行数组寻址 */
            arr_type = member_access_field_type(g, locals, expr->index.object);
            if (!arr_type)
                arr_type = infer_expr_type(g, expr->index.object, locals);
            if (arr_type) {
                arr_ptr = emit_expr(g, expr->index.object, locals);
                if (type_named(arr_type, "List", 4)) {
                    is_list = 1;
                }
            }
        } else {
            /* 通用索引寻址：支持任意产生集合/数组的表达式 */
            arr_type = infer_expr_type(g, expr->index.object, locals);
            if (arr_type) {
                arr_ptr = emit_expr(g, expr->index.object, locals);
                if (type_named(arr_type, "List", 4)) {
                    is_list = 1;
                }
            }
        }
        arr_type = concretize(g, arr_type);
        if (arr_ptr && is_list) {
            LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
            LLVMValueRef list_ptr = LLVMBuildBitCast(g->builder, arr_ptr,
                LLVMPointerType(g->list_struct_type, 0), "lptr");
            LLVMValueRef data_field = LLVMBuildStructGEP2(g->builder, g->list_struct_type, list_ptr, 2, "df");
            LLVMValueRef data = LLVMBuildLoad2(g->builder, LLVMPointerType(i64, 0), data_field, "data");
            LLVMValueRef idx = emit_expr(g, expr->index.index, locals);
            LLVMValueRef count_field = LLVMBuildStructGEP2(g->builder,
                g->list_struct_type, list_ptr, 0, "countf");
            LLVMValueRef count = LLVMBuildLoad2(g->builder, i64, count_field,
                "count");
            emit_index_bounds_check(g, idx, count, expr->loc, "list");
            if (LLVMGetTypeKind(LLVMTypeOf(idx)) == LLVMIntegerTypeKind &&
                LLVMGetIntTypeWidth(LLVMTypeOf(idx)) < 64) {
                idx = LLVMBuildSExt(g->builder, idx, i64, "ext");
            }
            idx = emit_index_safe_bounds(g, idx, count, expr->loc, "list");
            zan_type_t *et = container_elem_type(arr_type);
            LLVMValueRef widx = slot_word_index(g, idx, elem_slot_words(g, et));
            LLVMValueRef elem_ptr = LLVMBuildGEP2(g->builder, i64, data, &widx, 1, "ep");
            LLVMTypeRef em = et ? map_type(g, et) : NULL;
            if (em && LLVMGetTypeKind(em) == LLVMStructTypeKind)
                return finish_index_of_temp(g, expr, locals, arr_type, et,
                    arr_ptr, load_struct_from_slot(g, elem_ptr, em));
            LLVMValueRef raw = LLVMBuildLoad2(g->builder, i64, elem_ptr, "elem");
            /* 集合物理 i64 槽与非整型元素间的重解释转换 */
            LLVMValueRef out = raw;
            if (em) {
                LLVMTypeKind mk = LLVMGetTypeKind(em);
                if (mk == LLVMPointerTypeKind)
                    out = LLVMBuildIntToPtr(g->builder, raw, em, "elp");
                else if (mk == LLVMDoubleTypeKind)
                    out = LLVMBuildBitCast(g->builder, raw, em, "elf");
                else if (mk == LLVMFloatTypeKind) {
                    /* 浮点槽位 32 位位模式在 64 位槽内的保持 */
                    LLVMValueRef narrow = LLVMBuildTrunc(g->builder, raw,
                        LLVMInt32TypeInContext(g->ctx), "elf32.t");
                    out = LLVMBuildBitCast(g->builder, narrow, em, "elf32.f");
                }
            }
            return finish_index_of_temp(g, expr, locals, arr_type, et,
                                        arr_ptr, out);
        }
        /* Dict 索引读取：哈希探测查找条目并提取值字段 */
        if (arr_ptr && arr_type && type_named(arr_type, "Dict", 4)) {
            LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
            LLVMValueRef dp = LLVMBuildBitCast(g->builder, arr_ptr,
                LLVMPointerType(g->dict_struct_type, 0), "dp");
            LLVMValueRef vp = LLVMBuildStructGEP2(g->builder, g->dict_struct_type, dp, 3, "vp");
            LLVMValueRef vs = LLVMBuildLoad2(g->builder, LLVMPointerType(i64, 0), vp, "vs");
            LLVMValueRef search = coerce_dict_key(g, emit_expr(g, expr->index.index, locals), dict_key_type(g, arr_type));
            zan_type_t *dvt = dict_value_type(arr_type);
            LLVMTypeRef value_llvm = dvt ? map_type(g, dvt) : i64;
            LLVMValueRef res = emit_entry_alloca(g, value_llvm, "dres");
            zan_store_fit(g, LLVMConstNull(value_llvm), res);
            LLVMValueRef found = emit_dict_find(g, arr_type, arr_ptr, search);
            LLVMValueRef hit = zan_icmp(g->builder, LLVMIntSGE, found,
                LLVMConstInt(i64, 0, 0), "dihit");
            LLVMBasicBlockRef hit_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "di.hit");
            LLVMBasicBlockRef miss_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "di.miss");
            LLVMBasicBlockRef done_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "di.done");
            LLVMBuildCondBr(g->builder, hit, hit_bb, miss_bb);
            /* 缺 key 读取不能静默返回默认值：至少走 soft runtime error 通道
             * （C# 语义是抛 KeyNotFoundException，见 TASKS.md A508）。 */
            LLVMPositionBuilderAtEnd(g->builder, miss_bb);
            emit_runtime_check(g, LLVMConstInt(LLVMInt1TypeInContext(g->ctx), 1, 0),
                expr->loc, "Dict key not found in indexer read");
            LLVMBuildBr(g->builder, done_bb);
            LLVMPositionBuilderAtEnd(g->builder, hit_bb);
            LLVMValueRef word = zan_mul(g->builder, found,
                load_dict_value_words(g, arr_ptr), "di.word");
            LLVMValueRef vslot = LLVMBuildGEP2(g->builder, i64, vs,
                &word, 1, "vsl");
            zan_store_fit(g, load_collection_slot_value(g, dvt, vslot), res);
            LLVMBuildBr(g->builder, done_bb);
            LLVMPositionBuilderAtEnd(g->builder, done_bb);
            emit_release_owned_call_temp(g, expr->index.index, search, locals);
            LLVMValueRef dout = LLVMBuildLoad2(g->builder, value_llvm, res, "dval");
            return finish_index_of_temp(g, expr, locals, arr_type, dvt,
                                        arr_ptr, dout);
        }
        /* 字符串索引访问：读取单字节 (i8) 并零扩展为字符码 */
        if (arr_ptr && arr_type && arr_type->kind == TYPE_STRING) {
            LLVMValueRef idx = emit_expr(g, expr->index.index, locals);
            if (expr_has_reliable_string_bounds(expr->index.object, locals)) {
                arr_ptr = emit_string_base_guard(g, arr_ptr, expr->loc);
                LLVMValueRef str_len = emit_string_buffer_len(g, arr_ptr, expr->loc);
                emit_index_bounds_check(g, idx, str_len, expr->loc, "string");
                idx = emit_index_safe_bounds(g, idx, str_len, expr->loc, "string");
            } else {
                idx = emit_string_elem_guard(g, arr_ptr, idx, expr->loc, &arr_ptr);
            }
            if (LLVMGetTypeKind(LLVMTypeOf(idx)) == LLVMIntegerTypeKind &&
                LLVMGetIntTypeWidth(LLVMTypeOf(idx)) != 64) {
                idx = LLVMBuildSExt(g->builder, idx,
                    LLVMInt64TypeInContext(g->ctx), "idxext");
            }
            LLVMTypeRef i8t = LLVMInt8TypeInContext(g->ctx);
            LLVMValueRef ch_ptr = LLVMBuildGEP2(g->builder, i8t, arr_ptr, &idx, 1, "chp");
            LLVMValueRef ch = LLVMBuildLoad2(g->builder, i8t, ch_ptr, "ch");
            LLVMValueRef chz = LLVMBuildZExt(g->builder, ch,
                LLVMInt64TypeInContext(g->ctx), "chz");
            return finish_index_of_temp(g, expr, locals, arr_type, NULL,
                                        arr_ptr, chz);
        }
        if (arr_ptr && arr_type) {
            LLVMTypeRef elem_llvm = LLVMInt64TypeInContext(g->ctx);
            if (arr_type->element_type) {
                elem_llvm = map_type(g, arr_type->element_type);
            }
            LLVMValueRef elem_ptr;
            if (arr_type->array_rank > 1) {
                /* B-ID183：多维同款 null 守卫（emit_mdarray_elem_ptr 读维度
                 * 头，null 直接崩），口径与一维一致。 */
                LLVMValueRef isnull = LLVMBuildIsNull(g->builder, arr_ptr,
                    "mdarridx.null");
                emit_runtime_check(g, isnull, expr->loc,
                    "null reference where a string/byte buffer is required (element access)");
                arr_ptr = emit_soft_base_select(g, arr_ptr, isnull, expr->loc);
                elem_ptr = emit_mdarray_elem_ptr(g, arr_ptr, expr,
                                                 elem_llvm, locals, NULL);
            } else {
                /* B-ID183：null 数组索引此前无守卫——zan_array_len 解引用
                 * null 头直接段错误（探针 _scratch/arrprobe：try 内也崩）。
                 * 对齐字符串分支（emit_string_base_guard）与 C# 语义：try 内
                 * 抛 NullReferenceException，try 外 soft 报告后落 scratch 页
                 * （len 读 0 → 越界钳 0 → 读 0，定义行为）。 */
                LLVMValueRef isnull = LLVMBuildIsNull(g->builder, arr_ptr,
                    "arridx.null");
                emit_runtime_check(g, isnull, expr->loc,
                    "null reference where a string/byte buffer is required (element access)");
                arr_ptr = emit_soft_base_select(g, arr_ptr, isnull, expr->loc);
                LLVMValueRef idx = emit_expr(g, expr->index.index, locals);
                LLVMValueRef arr_len = zan_array_len(g, arr_ptr);
                emit_index_bounds_check(g, idx, arr_len, expr->loc, "array");
                idx = emit_index_safe_bounds(g, idx, arr_len, expr->loc, "array");
                LLVMValueRef typed_arr = LLVMBuildBitCast(g->builder, arr_ptr,
                    LLVMPointerType(elem_llvm, 0), "arrp");
                elem_ptr = LLVMBuildGEP2(g->builder, elem_llvm, typed_arr, &idx, 1, "eidx");
            }
            return promote_loaded(g,
                LLVMBuildLoad2(g->builder, elem_llvm, elem_ptr, "elem"),
                arr_type->element_type);
        }
        return LLVMConstInt(LLVMInt64TypeInContext(g->ctx), 0, 0);
    return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
}
