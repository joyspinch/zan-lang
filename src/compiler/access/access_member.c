LLVMValueRef emit_expr_member_access(zan_irgen_t *g, zan_ast_node_t *expr,
        local_scope_t *locals) {
        /* 读取控制台当前前景色与背景色属性 */
        if (expr->member.object->kind == AST_IDENTIFIER &&
            expr->member.object->ident.name.len == 7 &&
            memcmp(expr->member.object->ident.name.str, "Console", 7) == 0 &&
            !local_find(locals, expr->member.object->ident.name) &&
            expr->member.name.len == 15) {
            if (memcmp(expr->member.name.str, "ForegroundColor", 15) == 0)
                return emit_console_color_get(g, 0);
            if (memcmp(expr->member.name.str, "BackgroundColor", 15) == 0)
                return emit_console_color_get(g, 1);
        }
        {
            bool is_cpu_obj = false;
            if (expr->member.object->kind == AST_IDENTIFIER &&
                expr->member.object->ident.name.len == 3 &&
                memcmp(expr->member.object->ident.name.str, "Cpu", 3) == 0 &&
                !local_find(locals, expr->member.object->ident.name)) {
                is_cpu_obj = true;
            } else if (expr->member.object->kind == AST_MEMBER_ACCESS &&
                       expr->member.object->member.name.len == 3 &&
                       memcmp(expr->member.object->member.name.str, "Cpu", 3) == 0) {
                is_cpu_obj = true;
            }
            if (is_cpu_obj) {
                int feat_id = 0;
                zan_istr_t mn = expr->member.name;
                if (mn.len == 9 && memcmp(mn.str, "HasPopcnt", 9) == 0) feat_id = 1;
                else if (mn.len == 8 && memcmp(mn.str, "HasLzcnt", 8) == 0) feat_id = 2;
                else if (mn.len == 8 && memcmp(mn.str, "HasSse42", 8) == 0) feat_id = 3;
                else if (mn.len == 7 && memcmp(mn.str, "HasAvx2", 7) == 0) feat_id = 4;
                else if (mn.len == 8 && memcmp(mn.str, "HasAesNi", 8) == 0) feat_id = 5;
                else if (mn.len == 7 && memcmp(mn.str, "HasNeon", 7) == 0) feat_id = 6;
                if (feat_id > 0) {
                    LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
                    LLVMValueRef id = LLVMConstInt(i32t, feat_id, 0);
                    LLVMValueRef fn = cpu_feature_fn(g);
                    LLVMTypeRef fnty = LLVMFunctionType(i32t, (LLVMTypeRef[]){ i32t }, 1, 0);
                    LLVMValueRef val = zan_call2(g->builder, fnty, fn, &id, 1, "cpu_feat");
                    return zan_icmp(g->builder, LLVMIntNE, val, LLVMConstInt(i32t, 0, 0), "has_feat");
                }
            }
        }
        /* Vector128.Zero / Vector128.AllBitsSet */
        {
            if (is_target_class_obj(expr->member.object, "Vector128", 9, locals)) {
                zan_istr_t mn = expr->member.name;
                LLVMTypeRef v16i8 = LLVMVectorType(LLVMInt8TypeInContext(g->ctx), 16);
                if (mn.len == 4 && memcmp(mn.str, "Zero", 4) == 0) {
                    return v16i8_to_vec128(g, LLVMConstNull(v16i8));
                }
                if (mn.len == 10 && memcmp(mn.str, "AllBitsSet", 10) == 0) {
                    return v16i8_to_vec128(g, LLVMConstAllOnes(v16i8));
                }
            }
        }
        {
            zan_type_t *tit = infer_expr_type(g, expr->member.object, locals);
            if (zan_refl_is_typeinfo(tit)) {
                LLVMValueRef rv = NULL;
                if (refl_emit_typeinfo_member(g,
                        emit_guarded_member_object(g, expr, locals),
                        expr->member.name, NULL, 0, locals, &rv))
                    return rv;
            }
        }
        /* 可空值类型 HasValue 与 Value 属性读取 */
        {
            zan_type_t *nt = infer_expr_type(g, expr->member.object, locals);
            bool is_has = expr->member.name.len == 8 &&
                memcmp(expr->member.name.str, "HasValue", 8) == 0;
            bool is_val = expr->member.name.len == 5 &&
                memcmp(expr->member.name.str, "Value", 5) == 0;
            if (nt && nt->kind == TYPE_NULLABLE && (is_has || is_val)) {
                LLVMValueRef nv = emit_guarded_member_object(g, expr, locals);
                if (llvm_is_nullable(LLVMTypeOf(nv))) {
                    if (is_has) return nullable_has_value(g, nv);
                    emit_runtime_check(g,
                        LLVMBuildNot(g->builder, nullable_has_value(g, nv), "nv.novalue"),
                        expr->loc, "Nullable object must have a value");
                    return nullable_get_payload(g, nv);
                }
            }
        }
        /* Task 属性读取：读取帧结果或完成标志 */
        {
            zan_type_t *ot = infer_expr_type(g, expr->member.object, locals);
            if (ot && ot->kind == TYPE_TASK) {
                bool is_res = expr->member.name.len == 6 &&
                    memcmp(expr->member.name.str, "Result", 6) == 0;
                bool is_comp = expr->member.name.len == 11 &&
                    memcmp(expr->member.name.str, "IsCompleted", 11) == 0;
                if (is_res || is_comp) {
                    LLVMTypeRef ti64 = LLVMInt64TypeInContext(g->ctx);
                    if (is_res && ot->type_arg_count != 1) {
                        zan_diag_emit_code(g->diag, DIAG_ERROR, expr->loc, 3012,
                            "Task.Result requires a Task<T> value");
                        return LLVMConstInt(ti64, 0, 0);
                    }
                    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                    LLVMValueRef h = emit_guarded_member_object(g, expr, locals);
                    if (LLVMGetTypeKind(LLVMTypeOf(h)) == LLVMPointerTypeKind)
                        h = LLVMBuildPtrToInt(g->builder, h, ti64, "task.h");
                    else if (LLVMGetIntTypeWidth(LLVMTypeOf(h)) < 64)
                        h = zan_iwiden(g->builder, h, ti64);
                    LLVMValueRef hp = LLVMBuildIntToPtr(g->builder, h, i8ptr, "task.fp");
                    int mode = is_comp ? 2 : 1;
                    return emit_task_member(g, hp,
                        is_res ? ot->type_args[0] : NULL, mode);
                }
            }
        }
        /* 标量类型成员访问防护与拦截 */
        {
            bool is_prim_const = false;
            if (expr->member.object->kind == AST_IDENTIFIER) {
                zan_istr_t on = expr->member.object->ident.name;
                zan_istr_t mn = expr->member.name;
                bool is_scalar_name = (on.len == 3 && memcmp(on.str, "int", 3) == 0) ||
                    (on.len == 4 && (memcmp(on.str, "uint", 4) == 0 ||
                                     memcmp(on.str, "long", 4) == 0 ||
                                     memcmp(on.str, "byte", 4) == 0 ||
                                     memcmp(on.str, "char", 4) == 0)) ||
                    (on.len == 5 && (memcmp(on.str, "ulong", 5) == 0 ||
                                     memcmp(on.str, "short", 5) == 0 ||
                                     memcmp(on.str, "sbyte", 5) == 0 ||
                                     memcmp(on.str, "float", 5) == 0)) ||
                    (on.len == 6 && (memcmp(on.str, "ushort", 6) == 0 ||
                                     memcmp(on.str, "double", 6) == 0));
                is_prim_const = is_scalar_name && (
                    (mn.len == 8 && (memcmp(mn.str, "MaxValue", 8) == 0 ||
                                     memcmp(mn.str, "MinValue", 8) == 0)) ||
                    (mn.len == 3 && memcmp(mn.str, "NaN", 3) == 0) ||
                    (mn.len == 16 && (memcmp(mn.str, "PositiveInfinity", 16) == 0 ||
                                      memcmp(mn.str, "NegativeInfinity", 16) == 0)) ||
                    (mn.len == 7 && memcmp(mn.str, "Epsilon", 7) == 0));
            }
            zan_type_t *mt = infer_expr_type(g, expr->member.object, locals);
            if (mt && !is_prim_const && mt->kind != TYPE_CLASS &&
                mt->kind != TYPE_STRUCT &&
                mt->kind != TYPE_INTERFACE && mt->kind != TYPE_ENUM &&
                mt->kind != TYPE_ARRAY && mt->kind != TYPE_NULLABLE &&
                mt->kind != TYPE_DELEGATE && mt->kind != TYPE_TYPE_PARAM &&
                mt->kind != TYPE_OBJECT && mt->kind != TYPE_ERROR &&
                mt->kind != TYPE_VOID && !is_span_type(mt)) {
                bool is_len = expr->member.name.len == 6 &&
                    memcmp(expr->member.name.str, "Length", 6) == 0;
                if (!(mt->kind == TYPE_STRING && is_len)) {
                    zan_diag_emit_code(g->diag, DIAG_ERROR, expr->loc, 3013,
                        "type '%s' has no member '%.*s'",
                        mt->name.str ? mt->name.str : "?",
                        (int)expr->member.name.len, expr->member.name.str);
                    return LLVMConstInt(LLVMInt64TypeInContext(g->ctx), 0, 0);
                }
            }
        }
        /* Math.PI → constant */
        if (expr->member.object->kind == AST_IDENTIFIER) {
            zan_istr_t obj = expr->member.object->ident.name;
            if (obj.len == 4 && memcmp(obj.str, "Math", 4) == 0) {
                if (expr->member.name.len == 2 && memcmp(expr->member.name.str, "PI", 2) == 0) {
                    return LLVMConstReal(LLVMDoubleTypeInContext(g->ctx), 3.14159265358979323846);
                }
                if (expr->member.name.len == 4 && memcmp(expr->member.name.str, "Sqrt", 4) == 0) {
                    return LLVMConstInt(LLVMInt64TypeInContext(g->ctx), 0, 0);
                }
            }
        }

        /* 内置标量类型常量（int.MaxValue、double.NaN 等）发射编译期常量 */
        if (expr->member.object->kind == AST_IDENTIFIER) {
            zan_istr_t obj = expr->member.object->ident.name;
            LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
            LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
            bool is_int   = obj.len == 3 && memcmp(obj.str, "int", 3) == 0;
            bool is_uint  = obj.len == 4 && memcmp(obj.str, "uint", 4) == 0;
            bool is_long  = obj.len == 4 && memcmp(obj.str, "long", 4) == 0;
            bool is_ulong = obj.len == 5 && memcmp(obj.str, "ulong", 5) == 0;
            bool is_short = obj.len == 5 && memcmp(obj.str, "short", 5) == 0;
            bool is_ushort= obj.len == 6 && memcmp(obj.str, "ushort", 6) == 0;
            bool is_byte  = obj.len == 4 && memcmp(obj.str, "byte", 4) == 0;
            bool is_sbyte = obj.len == 5 && memcmp(obj.str, "sbyte", 5) == 0;
            bool is_float = obj.len == 5 && memcmp(obj.str, "float", 5) == 0;
            bool is_double= obj.len == 6 && memcmp(obj.str, "double", 6) == 0;
            bool is_char  = obj.len == 4 && memcmp(obj.str, "char", 4) == 0;
            if (is_int || is_uint || is_long || is_ulong || is_short ||
                is_ushort || is_byte || is_sbyte || is_float || is_double ||
                is_char) {
                zan_istr_t m = expr->member.name;
                if (m.len == 8 && memcmp(m.str, "MaxValue", 8) == 0) {
                    if (is_int)    return LLVMConstInt(i32, INT32_MAX, 0);
                    if (is_uint)   return LLVMConstInt(i32, UINT32_MAX, 0);
                    if (is_long)   return LLVMConstInt(i64, INT64_MAX, 0);
                    if (is_ulong)  return LLVMConstInt(i64, UINT64_MAX, 0);
                    if (is_short)  return LLVMConstInt(i32, INT16_MAX, 0);
                    if (is_ushort) return LLVMConstInt(i32, UINT16_MAX, 0);
                    if (is_byte)   return LLVMConstInt(i32, UINT8_MAX, 0);
                    if (is_sbyte)  return LLVMConstInt(i32, INT8_MAX, 0);
                    if (is_char)   return LLVMConstInt(i64, 0xFFFF, 0);
                    if (is_float)  return LLVMConstReal(LLVMFloatTypeInContext(g->ctx), 3.40282346638528859812e+38);
                    if (is_double) return LLVMConstReal(LLVMDoubleTypeInContext(g->ctx), 1.7976931348623157e+308);
                }
                if (m.len == 8 && memcmp(m.str, "MinValue", 8) == 0) {
                    if (is_int)   return LLVMConstInt(i32, (unsigned long long)INT32_MIN, 1);
                    if (is_long)  return LLVMConstInt(i64, (unsigned long long)INT64_MIN, 1);
                    if (is_short) return LLVMConstInt(i32, (unsigned long long)INT16_MIN, 1);
                    if (is_sbyte) return LLVMConstInt(i32, (unsigned long long)INT8_MIN, 1);
                    if (is_float) return LLVMConstReal(LLVMFloatTypeInContext(g->ctx), -3.40282346638528859812e+38);
                    if (is_double) return LLVMConstReal(LLVMDoubleTypeInContext(g->ctx), -1.7976931348623157e+308);
                }
                if (is_double || is_float) {
                    /* 浮点特殊常量位精确表示（如正负无穷与 NaN） */
                    if (m.len == 3 && memcmp(m.str, "NaN", 3) == 0) {
                        if (is_float)
                            return LLVMConstBitCast(
                                LLVMConstInt(i32, 0x7FC00000U, 0),
                                LLVMFloatTypeInContext(g->ctx));
                        return LLVMConstBitCast(
                            LLVMConstInt(i64, 0x7FF8000000000000ULL, 0),
                            LLVMDoubleTypeInContext(g->ctx));
                    }
                    if (m.len == 16 && memcmp(m.str, "PositiveInfinity", 16) == 0) {
                        if (is_float)
                            return LLVMConstBitCast(
                                LLVMConstInt(i32, 0x7F800000U, 0),
                                LLVMFloatTypeInContext(g->ctx));
                        return LLVMConstBitCast(
                            LLVMConstInt(i64, 0x7FF0000000000000ULL, 0),
                            LLVMDoubleTypeInContext(g->ctx));
                    }
                    if (m.len == 16 && memcmp(m.str, "NegativeInfinity", 16) == 0) {
                        if (is_float)
                            return LLVMConstBitCast(
                                LLVMConstInt(i32, 0xFF800000U, 0),
                                LLVMFloatTypeInContext(g->ctx));
                        return LLVMConstBitCast(
                            LLVMConstInt(i64, 0xFFF0000000000000ULL, 0),
                            LLVMDoubleTypeInContext(g->ctx));
                    }
                }
                if (is_float && m.len == 7 && memcmp(m.str, "Epsilon", 7) == 0)
                    return LLVMConstReal(LLVMFloatTypeInContext(g->ctx),
                        1.4012984643248170709e-45);
            }
        }

        if (expr->member.name.len == 6 &&
            memcmp(expr->member.name.str, "Length", 6) == 0) {
            zan_type_t *st = infer_expr_type(g, expr->member.object, locals);
            if (is_span_type(st)) {
                LLVMValueRef span_val = emit_guarded_member_object(g, expr, locals);
                LLVMValueRef len = LLVMBuildExtractValue(g->builder, span_val, 1, "sp.len");
                return LLVMBuildTrunc(g->builder, len,
                    LLVMInt32TypeInContext(g->ctx), "sp.len32");
            }
        }

        if (expr->member.name.len == 5 && memcmp(expr->member.name.str, "Count", 5) == 0) {
            zan_type_t *lt = infer_expr_type(g, expr->member.object, locals);
            if (lt && type_named(lt, "List", 4)) {
                LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                LLVMValueRef raw_ptr = emit_guarded_member_object(g, expr, locals);
                LLVMValueRef list_ptr = LLVMBuildBitCast(g->builder, raw_ptr,
                    LLVMPointerType(g->list_struct_type, 0), "lptr");
                LLVMValueRef count_ptr = LLVMBuildStructGEP2(g->builder, g->list_struct_type, list_ptr, 0, "cntp");
                LLVMValueRef count = LLVMBuildLoad2(g->builder, i64, count_ptr, "cnt");
                emit_release_owned_call_temp(g, expr->member.object, raw_ptr, locals);
                return count;
            }
        }

        /* Dict.Count：读取字典当前项数 */
        if (expr->member.name.len == 5 && memcmp(expr->member.name.str, "Count", 5) == 0) {
            zan_type_t *dt = infer_expr_type(g, expr->member.object, locals);
            if (dt && type_named(dt, "Dict", 4)) {
                LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                LLVMValueRef raw = emit_guarded_member_object(g, expr, locals);
                LLVMValueRef dp = LLVMBuildBitCast(g->builder, raw,
                    LLVMPointerType(g->dict_struct_type, 0), "dp");
                LLVMValueRef cntp = LLVMBuildStructGEP2(g->builder, g->dict_struct_type, dp, 0, "cntp");
                LLVMValueRef cnt = LLVMBuildLoad2(g->builder, i64, cntp, "dcnt");
                emit_release_owned_call_temp(g, expr->member.object, raw, locals);
                return cnt;
            }
        }

        /* Dict.Keys 与 Dict.Values：将键/值集合拷贝至独立 List 实例 */
        if ((expr->member.name.len == 4 && memcmp(expr->member.name.str, "Keys", 4) == 0) ||
            (expr->member.name.len == 6 && memcmp(expr->member.name.str, "Values", 6) == 0)) {
            zan_type_t *dt = infer_expr_type(g, expr->member.object, locals);
            if (dt && type_named(dt, "Dict", 4)) {
                bool want_keys = (expr->member.name.len == 4);
                zan_type_t *elem = want_keys ? dict_key_type(g, dt) : dict_value_type(dt);
                LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                LLVMValueRef raw = emit_guarded_member_object(g, expr, locals);
                LLVMValueRef dp = LLVMBuildBitCast(g->builder, raw,
                    LLVMPointerType(g->dict_struct_type, 0), "dp");
                LLVMValueRef dict_value_words = load_dict_value_words(g, raw);
                LLVMValueRef cntp = LLVMBuildStructGEP2(g->builder, g->dict_struct_type, dp, 0, "cntp");
                LLVMValueRef cnt = LLVMBuildLoad2(g->builder, i64, cntp, "dcnt");
                LLVMValueRef srcp = LLVMBuildStructGEP2(g->builder, g->dict_struct_type, dp,
                    want_keys ? 2 : 3, want_keys ? "kp" : "vp");
                LLVMTypeRef src_slot_ty = want_keys ? i8ptr : i64;
                LLVMValueRef src = LLVMBuildLoad2(g->builder,
                    LLVMPointerType(src_slot_ty, 0), srcp, "dsrc");
                LLVMValueRef list_raw = emit_alloc_rc_collection(g, expr, 24, 1, elem);
                LLVMValueRef lp = LLVMBuildBitCast(g->builder, list_raw,
                    LLVMPointerType(g->list_struct_type, 0), "lp");
                zan_store_fit(g, cnt,
                    LLVMBuildStructGEP2(g->builder, g->list_struct_type, lp, 0, "lcnt"));
                LLVMValueRef cap = zan_add(g->builder, cnt,
                    LLVMConstInt(i64, 8, 0), "lcap");
                zan_store_fit(g, cap,
                    LLVMBuildStructGEP2(g->builder, g->list_struct_type, lp, 1, "lcapp"));
                unsigned elem_words = elem_slot_words(g, elem);
                LLVMValueRef data_words = zan_mul(g->builder, cap,
                    LLVMConstInt(i64, elem_words, 0), "lwords");
                LLVMValueRef data_size = zan_mul(g->builder, data_words,
                    LLVMConstInt(i64, 8, 0), "lsz");
                LLVMValueRef data = zan_call2(g->builder,
                    LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i64, i64 }, 2, 0),
                    get_calloc_fn(g),
                    (LLVMValueRef[]){ LLVMConstInt(i64, 1, 0), data_size }, 2, "ldata");
                zan_irgen_emit_oom_check(g, g->current_fn, data);
                LLVMValueRef data_typed = LLVMBuildBitCast(g->builder, data,
                    LLVMPointerType(i64, 0), "ldp");
                zan_store_fit(g, data_typed,
                    LLVMBuildStructGEP2(g->builder, g->list_struct_type, lp, 2, "ldf"));
                LLVMValueRef idx_a = emit_entry_alloca(g, i64, "dk.i");
                zan_store_fit(g, LLVMConstInt(i64, 0, 0), idx_a);
                LLVMBasicBlockRef cond_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "dk.cond");
                LLVMBasicBlockRef body_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "dk.body");
                LLVMBasicBlockRef done_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "dk.done");
                LLVMBuildBr(g->builder, cond_bb);
                LLVMPositionBuilderAtEnd(g->builder, cond_bb);
                LLVMValueRef ci = LLVMBuildLoad2(g->builder, i64, idx_a, "ci");
                LLVMBuildCondBr(g->builder,
                    zan_icmp(g->builder, LLVMIntUGE, ci, cnt, "cdone"),
                    done_bb, body_bb);
                LLVMPositionBuilderAtEnd(g->builder, body_bb);
                LLVMValueRef src_index = want_keys ? ci
                    : zan_mul(g->builder, ci, dict_value_words, "dv.word");
                LLVMValueRef sslot = LLVMBuildGEP2(g->builder, src_slot_ty,
                    src, &src_index, 1, "ssl");
                LLVMValueRef sval = want_keys
                    ? LLVMBuildLoad2(g->builder, src_slot_ty, sslot, "sv")
                    : load_collection_slot_value(g, elem, sslot);
                LLVMValueRef dst_index = slot_word_index(g, ci, elem_words);
                LLVMValueRef dslot = LLVMBuildGEP2(g->builder, i64,
                    data_typed, &dst_index, 1, "dsl");
                emit_collection_slot_store(g, elem, i64, dslot, sval, NULL, locals, 0);
                zan_store_fit(g,
                    zan_add(g->builder, ci, LLVMConstInt(i64, 1, 0), "ni"), idx_a);
                LLVMBuildBr(g->builder, cond_bb);
                LLVMPositionBuilderAtEnd(g->builder, done_bb);
                emit_release_owned_call_temp(g, expr->member.object, raw, locals);
                return LLVMBuildBitCast(g->builder, lp, i8ptr, "keysv");
            }
        }

        /* array.Length：从数组头部读取元素个数，空引用返回 0 */
        if (expr->member.name.len == 5 &&
            memcmp(expr->member.name.str, "Count", 5) == 0) {
            zan_type_t *at = infer_expr_type(g, expr->member.object, locals);
            if (at && at->kind == TYPE_ARRAY) {
                LLVMValueRef arr = emit_guarded_member_object(g, expr, locals);
                if (LLVMGetTypeKind(LLVMTypeOf(arr)) == LLVMPointerTypeKind) {
                    LLVMValueRef isnull = zan_icmp(g->builder, LLVMIntEQ, arr,
                        LLVMConstNull(LLVMTypeOf(arr)), "arr.null");
                    emit_runtime_check(g, isnull, expr->member.object->loc,
                        "null reference where an array is required (.Count)");
                    arr = emit_soft_base_select(g, arr, isnull,
                                                expr->member.object->loc);
                }
                LLVMValueRef len = zan_array_len(g, arr);
                emit_release_owned_call_temp(g, expr->member.object, arr, locals);
                return len;
            }
        }
        if (expr->member.name.len == 6 &&
            memcmp(expr->member.name.str, "Length", 6) == 0) {
            zan_type_t *at = infer_expr_type(g, expr->member.object, locals);
            if (at && at->kind == TYPE_ARRAY) {
                LLVMValueRef arr = emit_guarded_member_object(g, expr, locals);
                if (LLVMGetTypeKind(LLVMTypeOf(arr)) == LLVMPointerTypeKind) {
                    LLVMValueRef isnull = zan_icmp(g->builder, LLVMIntEQ, arr,
                        LLVMConstNull(LLVMTypeOf(arr)), "arr.null");
                    emit_runtime_check(g, isnull, expr->member.object->loc,
                        "null reference where an array is required (.Length)");
                    arr = emit_soft_base_select(g, arr, isnull,
                                                expr->member.object->loc);
                }
                LLVMValueRef len = zan_array_len(g, arr);
                emit_release_owned_call_temp(g, expr->member.object, arr, locals);
                return len;
            }
        }

        if (expr->member.name.len == 6 && memcmp(expr->member.name.str, "Length", 6) == 0) {
            zan_type_t *sbt = infer_expr_type(g, expr->member.object, locals);
            if (sbt && type_named(sbt, "StringBuilder", 13)) {
                LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                LLVMValueRef raw = emit_guarded_member_object(g, expr, locals);
                LLVMValueRef sbp = LLVMBuildBitCast(g->builder, raw,
                    LLVMPointerType(g->sb_struct_type, 0), "sbp");
                LLVMValueRef cptr = LLVMBuildStructGEP2(g->builder, g->sb_struct_type, sbp, 0, "sbcp");
                LLVMValueRef sblen = LLVMBuildLoad2(g->builder, i64, cptr, "sblen");
                emit_release_owned_call_temp(g, expr->member.object, raw, locals);
                return sblen;
            }
        }

        /* String.Length 属性：读取头部缓存长度，必要时回退至 strlen 扫描 */
        if (expr->member.name.len == 6 && memcmp(expr->member.name.str, "Length", 6) == 0 &&
            recv_is_stringlike(g, expr->member.object, locals)) {
            LLVMValueRef obj_val = emit_guarded_member_object(g, expr, locals);
            if (LLVMGetTypeKind(LLVMTypeOf(obj_val)) == LLVMPointerTypeKind) {
                LLVMValueRef len = emit_string_length(g, obj_val, expr->loc);
                emit_release_owned_call_temp(g, expr->member.object, obj_val, locals);
                return len;
            }
        }

        /* ConsoleColor 枚举成员读取：映射至 0..15 固定序数 */
        if (expr->member.object->kind == AST_IDENTIFIER &&
            expr->member.object->ident.name.len == 12 &&
            memcmp(expr->member.object->ident.name.str, "ConsoleColor", 12) == 0) {
            static const struct { const char *n; int l; } cc[16] = {
                {"Black",5},{"DarkBlue",8},{"DarkGreen",9},{"DarkCyan",8},
                {"DarkRed",7},{"DarkMagenta",11},{"DarkYellow",10},{"Gray",4},
                {"DarkGray",8},{"Blue",4},{"Green",5},{"Cyan",4},
                {"Red",3},{"Magenta",7},{"Yellow",6},{"White",5} };
            for (int i = 0; i < 16; i++) {
                if (expr->member.name.len == cc[i].l &&
                    memcmp(expr->member.name.str, cc[i].n, (size_t)cc[i].l) == 0)
                    return LLVMConstInt(LLVMInt64TypeInContext(g->ctx),
                                        (uint64_t)i, 0);
            }
        }

        /* 枚举成员访问：EnumType.MemberName 降解为整型常量 */
        if (expr->member.object->kind == AST_IDENTIFIER) {
            zan_symbol_t *enum_sym = zan_binder_lookup(g->binder, expr->member.object->ident.name);
            if (enum_sym && enum_sym->kind == SYM_ENUM) {
                int64_t enum_val = 0;
                for (int ei = 0; ei < enum_sym->member_count; ei++) {
                    if (enum_sym->members[ei]->kind == SYM_ENUM_MEMBER) {
                        if (enum_sym->members[ei]->name.len == expr->member.name.len &&
                            memcmp(enum_sym->members[ei]->name.str, expr->member.name.str,
                                   (size_t)expr->member.name.len) == 0) {
                            zan_ast_node_t *em_decl = enum_sym->members[ei]->decl;
                            if (em_decl && em_decl->kind == AST_ENUM_MEMBER &&
                                em_decl->enum_member.value &&
                                em_decl->enum_member.value->kind == AST_INT_LITERAL) {
                                enum_val = em_decl->enum_member.value->int_val;
                            }
                            return LLVMConstInt(LLVMInt64TypeInContext(g->ctx),
                                               (uint64_t)(uint32_t)enum_val, 0);
                        }
                        zan_ast_node_t *em_decl = enum_sym->members[ei]->decl;
                        if (em_decl && em_decl->kind == AST_ENUM_MEMBER &&
                            em_decl->enum_member.value &&
                            em_decl->enum_member.value->kind == AST_INT_LITERAL) {
                            enum_val = em_decl->enum_member.value->int_val + 1;
                        } else {
                            enum_val++;
                        }
                    }
                }
                return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
            }
        }

        /* 静态字段访问：ClassName.StaticField 直接读取全局变量 */
        if (expr->member.object->kind == AST_IDENTIFIER &&
            !local_find(locals, expr->member.object->ident.name)) {
            zan_symbol_t *cs = zan_binder_lookup(g->binder, expr->member.object->ident.name);
            if (cs && (cs->kind == SYM_CLASS || cs->kind == SYM_STRUCT)) {
                zan_symbol_t *fs = get_field_sym(cs, expr->member.name);
                zan_symbol_t *getter = property_getter_sym(g, fs);
                if (getter) {
                    return emit_property_getter_call(g, getter, cs->type, NULL,
                        expr->member.object, locals);
                }
                LLVMValueRef gv = get_static_field_global(g, cs, fs,
                    static_access_inst(g, expr->member.object));
                if (gv) {
                    LLVMTypeRef ft = fs->type ? map_type(g, fs->type)
                                              : LLVMInt64TypeInContext(g->ctx);
                    return promote_loaded(g,
                        LLVMBuildLoad2(g->builder, ft, gv, "sfld"), fs->type);
                }
            }
        }

        /* 静态方法引用作为委托传递：生成静态方法组记录 */
        if (expr->member.object->kind == AST_IDENTIFIER &&
            !local_find(locals, expr->member.object->ident.name)) {
            zan_symbol_t *cs = zan_binder_lookup(g->binder, expr->member.object->ident.name);
            if (cs && (cs->kind == SYM_CLASS || cs->kind == SYM_STRUCT)) {
                zan_symbol_t *ms = get_method_sym(cs, expr->member.name);
                if (ms) {
                    if (target_is_wasm32(g)) {
                        LLVMValueRef clo = emit_method_group_closure(g, ms, NULL,
                                                                     expr->loc);
                        if (clo) return clo;
                    }
                    for (int fi = irgen_find_function(g, ms); fi >= 0; fi = -1) {
                        if (g->functions[fi].sym == ms) {
                            return g->functions[fi].fn;
                        }
                    }
                }
            }
        }

        if (expr->member.object->kind == AST_IDENTIFIER) {
            local_var_t *local = local_find(locals, expr->member.object->ident.name);
            if (local && local->type && (local->type->kind == TYPE_STRUCT || local->type->kind == TYPE_CLASS)) {
                zan_symbol_t *type_sym = local->type->sym;
                if (type_sym) {
                    int fi = get_field_index(type_sym, expr->member.name);
                    if (fi >= 0) {
                        zan_symbol_t *psym = get_field_sym(type_sym, expr->member.name);
                        zan_symbol_t *getter = property_getter_sym(g, psym);
                        if (getter) {
                            LLVMTypeRef st = get_struct_llvm_type(g, type_sym);
                            LLVMValueRef rval = struct_base_ptr(g, local, st);
                            return emit_property_getter_call(g, getter,
                                local->type, rval, expr->member.object, locals);
                        }
                        LLVMTypeRef st = get_struct_llvm_type(g, type_sym);
                        if (st) {
                            /* 结构体字段访问：获取指针并通过 GEP 寻址目标字段 */
                            LLVMValueRef struct_ptr = struct_base_ptr(g, local, st);
                            if (local->type->kind == TYPE_CLASS &&
                                LLVMGetTypeKind(LLVMTypeOf(struct_ptr)) == LLVMPointerTypeKind) {
                                LLVMValueRef isnull = zan_icmp(g->builder,
                                    LLVMIntEQ, struct_ptr,
                                    LLVMConstNull(LLVMTypeOf(struct_ptr)),
                                    "recv.null");
                                char recv_msg[256];
                                snprintf(recv_msg, sizeof(recv_msg),
                                    "null reference: receiver '%.*s' is null",
                                    (int)expr->member.object->ident.name.len,
                                    expr->member.object->ident.name.str);
                                emit_runtime_check(g, isnull,
                                    expr->member.object->loc, recv_msg);
                                struct_ptr = emit_soft_base_select(g, struct_ptr,
                                    isnull, expr->member.object->loc);
                            }
                            LLVMValueRef field_ptr = emit_field_ptr(g, type_sym, st, struct_ptr, fi, "fld");
                            zan_symbol_t *fsym = get_field_sym(type_sym, expr->member.name);
                            zan_type_t *fty = fsym
                                ? field_type_at(g, fsym, local->type)
                                : NULL;
                            LLVMTypeRef field_type = fty ? map_type(g, fty)
                                : LLVMInt64TypeInContext(g->ctx);
                            LLVMValueRef fv0 =
                                (fsym && (fsym->modifiers & MOD_WEAK))
                                ? emit_weak_field_load(g, field_ptr, field_type)
                                : LLVMBuildLoad2(g->builder, field_type, field_ptr, "fval");
                            LLVMValueRef fv = promote_loaded(g, fv0, fty);
                            return fv;
                        }
                    }
                }
            }
        }

        {
            zan_symbol_t *cls = expr_class_sym(g, expr->member.object, locals);
            if (cls) {
                int fi = get_field_index(cls, expr->member.name);
                if (fi >= 0) {
                    /* 索引结构体元素字段读取 (l[i].field) */
                    if (expr->member.object->kind == AST_INDEX &&
                        cls->kind == SYM_STRUCT) {
                        zan_type_t *et = infer_expr_type(g,
                            expr->member.object, locals);
                        LLVMTypeRef st = get_struct_llvm_type(g, cls);
                        LLVMValueRef ep = (st && !type_is_binding(et))
                            ? emit_struct_elem_ptr(g, expr->member.object, locals)
                            : NULL;
                        if (ep) {
                            LLVMValueRef sptr = LLVMBuildBitCast(g->builder, ep,
                                LLVMPointerType(st, 0), "ep.s");
                            zan_symbol_t *gpsym = get_field_sym(cls,
                                expr->member.name);
                            zan_symbol_t *getter = property_getter_sym(g, gpsym);
                            if (getter) {
                                return emit_property_getter_call(g, getter, et,
                                    sptr, expr->member.object, locals);
                            }
                            LLVMValueRef field_ptr = emit_field_ptr(g, cls, st,
                                sptr, fi, "gfld");
                            zan_type_t *fty = gpsym
                                ? field_type_at(g, gpsym, et) : NULL;
                            LLVMTypeRef ft = fty ? map_type(g, fty)
                                : LLVMInt64TypeInContext(g->ctx);
                            LLVMValueRef gfv0 =
                                (gpsym && (gpsym->modifiers & MOD_WEAK))
                                ? emit_weak_field_load(g, field_ptr, ft)
                                : LLVMBuildLoad2(g->builder, ft, field_ptr, "gfval");
                            return promote_loaded(g, gfv0, fty);
                        }
                    }
                    zan_symbol_t *psym = get_field_sym(cls, expr->member.name);
                    zan_symbol_t *getter = property_getter_sym(g, psym);
                    if (getter) {
                        zan_type_t *rct = infer_expr_type(g, expr->member.object, locals);
                        LLVMValueRef rval = emit_guarded_member_object(g, expr, locals);
                        /* base.Prop 属性读取：直接调用基类 getter，绕过虚方法分发 */
                        if (expr->member.object->kind == AST_BASE_EXPR)
                            return emit_property_getter_call(g, getter, NULL,
                                rval, expr->member.object, locals);
                        return emit_property_getter_call(g, getter, rct, rval,
                                                         expr->member.object,
                                                         locals);
                    }
                    LLVMTypeRef st = get_struct_llvm_type(g, cls);
                    LLVMValueRef obj_val = emit_guarded_member_object(g, expr, locals);
                    if (LLVMGetTypeKind(LLVMTypeOf(obj_val)) == LLVMStructTypeKind) {
                        LLVMValueRef sfv = LLVMBuildExtractValue(g->builder,
                            obj_val, (unsigned)fi, "sfval");
                        zan_symbol_t *fsym = get_field_sym(cls, expr->member.name);
                        zan_type_t *rct = infer_expr_type(g, expr->member.object, locals);
                        if (fsym) {
                            zan_type_t *ct = field_type_at(g, fsym, rct);
                            if (ct != fsym->type)
                                sfv = emit_boundary_coerce(g, sfv, map_type(g, ct));
                        }
                        return sfv;
                    }
                    if (st && LLVMGetTypeKind(LLVMTypeOf(obj_val)) == LLVMPointerTypeKind) {
                        LLVMValueRef field_ptr = emit_field_ptr(g, cls, st, obj_val, fi, "gfld");
                        zan_symbol_t *fsym = get_field_sym(cls, expr->member.name);
                        zan_type_t *rct = infer_expr_type(g, expr->member.object, locals);
                        zan_type_t *fty = fsym
                            ? field_type_at(g, fsym, rct) : NULL;
                        LLVMTypeRef ft = fty ? map_type(g, fty)
                                             : LLVMInt64TypeInContext(g->ctx);
                        LLVMValueRef gfv0 =
                            (fsym && (fsym->modifiers & MOD_WEAK))
                            ? emit_weak_field_load(g, field_ptr, ft)
                            : LLVMBuildLoad2(g->builder, ft, field_ptr, "gfval");
                        LLVMValueRef gfv = promote_loaded(g, gfv0, fty);
                        return finish_member_of_temp(g, expr, locals, rct,
                                                     obj_val, gfv);
                    }
                }
            }
        }
        /* 实例方法组作为委托传递：绑定接收者并保留引用 */
        {
            zan_symbol_t *cls = expr_class_sym(g, expr->member.object, locals);
            if (cls) {
                zan_symbol_t *ms = get_method_sym(cls, expr->member.name);
                if (ms && ms->decl && ms->decl->kind == AST_METHOD_DECL &&
                    (ms->decl->method_decl.modifiers & MOD_STATIC) == 0) {
                    LLVMValueRef recv = emit_guarded_member_object(g, expr, locals);
                    LLVMValueRef clo = recv
                        ? emit_method_group_closure(g, ms, recv, expr->loc)
                        : NULL;
                    if (clo) {
                        /* 闭包捕获接收者并维持 +1 强引用 */
                        zan_type_t *rct = infer_expr_type(g, expr->member.object, locals);
                        if (receiver_is_owned_temp(g, expr->member.object, locals,
                                                   rct, recv))
                            emit_rc_release_for_type(g, rct, recv);
                        return clo;
                    }
                    zan_diag_emit_code(g->diag, DIAG_ERROR, expr->loc, 3014,
                        "instance method group '%.*s' cannot be used as a value",
                        (int)expr->member.name.len, expr->member.name.str);
                    return LLVMConstNull(
                        LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0));
                }
            }
        }
        /* 健壮性：未定义字段读取安全报错与诊断 */
        {
            zan_symbol_t *rcls = expr_class_sym(g, expr->member.object, locals);
            if (!rcls && expr->member.object->kind == AST_IDENTIFIER &&
                !local_find(locals, expr->member.object->ident.name)) {
                zan_symbol_t *ts = zan_binder_lookup(g->binder,
                                                     expr->member.object->ident.name);
                if (ts && (ts->kind == SYM_CLASS || ts->kind == SYM_STRUCT))
                    rcls = ts;
            }
            if (rcls && (rcls->kind == SYM_CLASS || rcls->kind == SYM_STRUCT) &&
                !type_declares_member(rcls, expr->member.name)) {
                zan_diag_emit_code(g->diag, DIAG_ERROR, expr->loc, 3009,
                    "'%.*s' has no member '%.*s'",
                    (int)rcls->name.len, rcls->name.str,
                    (int)expr->member.name.len, expr->member.name.str);
            } else if (!rcls) {
                zan_type_t *rt = infer_expr_type(g, expr->member.object, locals);
                if (rt && is_builtin_collection_type(rt))
                    zan_diag_emit_code(g->diag, DIAG_ERROR, expr->loc, 3009,
                        "'%.*s' has no member '%.*s'",
                        (int)rt->name.len, rt->name.str,
                        (int)expr->member.name.len, expr->member.name.str);
                /* 链式调用未定义成员安全处理 */
                zan_ast_node_t *ro = expr->member.object;
                if (!rt && ro->kind == AST_CALL && ro->call.callee &&
                    ro->call.callee->kind == AST_MEMBER_ACCESS) {
                    zan_ast_node_t *inner = ro->call.callee;
                    zan_symbol_t *icls = expr_class_sym(g, inner->member.object,
                                                        locals);
                    if (icls &&
                        (icls->kind == SYM_CLASS || icls->kind == SYM_STRUCT) &&
                        !type_declares_member(icls, inner->member.name))
                        zan_diag_emit_code(g->diag, DIAG_ERROR, inner->loc, 3009,
                            "'%.*s' has no member '%.*s'",
                            (int)icls->name.len, icls->name.str,
                            (int)inner->member.name.len,
                            inner->member.name.str);
                }
            }
        }
        if (g->diag->error_count == 0) {
            zan_symbol_t *rcls = expr_class_sym(g, expr->member.object, locals);
            if (rcls) {
                zan_diag_emit_code(g->diag, DIAG_ERROR, expr->loc, 3009,
                    "'%.*s' has no member '%.*s'",
                    (int)rcls->name.len, rcls->name.str,
                    (int)expr->member.name.len, expr->member.name.str);
            } else {
                zan_diag_emit_code(g->diag, DIAG_ERROR, expr->loc, 3015,
                    "cannot resolve member '%.*s'",
                    (int)expr->member.name.len, expr->member.name.str);
            }
        }
        return LLVMConstInt(LLVMInt64TypeInContext(g->ctx), 0, 0);
    return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
}

