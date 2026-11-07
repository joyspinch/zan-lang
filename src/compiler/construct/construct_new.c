zan_ast_node_t *owned_rhs_marker(zan_irgen_t *g, zan_loc_t loc);

/* B-ID163②：new T() 擦除体兜底出口。类型实参未定时旧实现静默返回零值
 * （调用方拿 null 继续跑）；此处把失败发射挪进独立块再硬失败
 * （emit_erased_new_hard_fail，exit 70 带文案），当前块正常 br 出去——
 * 调用方还要在"死块"里继续发射（LLVM 块中块 terminator 非法，探针实录
 * E3090）。单态化路径（实参已替换，见 conformance generic_new_constraint）
 * actual_t 不是 TYPE_TYPE_PARAM，不受影响。 */
static LLVMValueRef new_erased_fallback(zan_irgen_t *g, zan_type_t *actual_t) {
    if (actual_t && actual_t->kind == TYPE_TYPE_PARAM && g->current_fn &&
        LLVMGetInsertBlock(g->builder)) {
        LLVMValueRef cfn = LLVMGetBasicBlockParent(
            LLVMGetInsertBlock(g->builder));
        LLVMBasicBlockRef fail_bb = LLVMAppendBasicBlockInContext(
            g->ctx, cfn, "newt.fail");
        LLVMBasicBlockRef dead_bb = LLVMAppendBasicBlockInContext(
            g->ctx, cfn, "newt.dead");
        LLVMBuildBr(g->builder, fail_bb);
        LLVMPositionBuilderAtEnd(g->builder, fail_bb);
        emit_erased_new_hard_fail(g);
        if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder)))
            LLVMBuildUnreachable(g->builder);
        LLVMPositionBuilderAtEnd(g->builder, dead_bb);
    }
    return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
}

LLVMValueRef emit_expr_new_expr(zan_irgen_t *g, zan_ast_node_t *expr,
        local_scope_t *locals) {
        /* 工厂调用接对象初始化式 */
        if (!expr->new_expr.type && expr->new_expr.call_init) {
            LLVMValueRef obj = emit_expr(g, expr->new_expr.call_init, locals);
            zan_type_t *otype = infer_expr_type(g, expr->new_expr.call_init,
                                                locals);
            if (!otype || !otype->sym ||
                (otype->sym->kind != SYM_CLASS &&
                 otype->sym->kind != SYM_STRUCT)) {
                zan_diag_emit_code(g->diag, DIAG_ERROR, expr->loc, 3022,
                    "object initializer can only continue a call that "
                    "returns a class instance");
                return obj;
            }
            zan_symbol_t *sym = otype->sym;
            LLVMTypeRef st = get_struct_llvm_type(g, sym);
            if (!st) return obj;
            for (int i = 0; i < expr->new_expr.arg_inits.count; i++) {
                zan_ast_node_t *arg = expr->new_expr.arg_inits.items[i];
                if (arg->kind != AST_COLL_INIT && arg->kind != AST_ASSIGNMENT)
                    continue;
                zan_istr_t mname = (arg->kind == AST_COLL_INIT)
                    ? arg->coll_init.name
                    : arg->binary.left->ident.name;
                zan_symbol_t *msym = get_field_sym(sym, mname);
                if (!msym) continue;
                if (arg->kind == AST_COLL_INIT) {
                    int getter_owned = 0;
                    zan_ast_node_t *recv = zan_ast_new(g->arena,
                        AST_IDENTIFIER, arg->loc);
                    recv->ident.name = mname;
                    LLVMTypeRef coll_lt = map_type(g, msym->type);
                    LLVMValueRef cslot = emit_entry_alloca(g, coll_lt,
                                                           "cinit.slot");
                    zan_store_fit(g, LLVMConstNull(coll_lt), cslot);
                    zan_symbol_t *getter = property_getter_sym(g, msym);
                    if (getter) {
                        LLVMValueRef v = emit_property_getter_call(g, getter,
                            otype, obj, arg, locals);
                        zan_store_fit(g, v, cslot);
                        getter_owned = 1;
                    } else {
                        int cfi = get_field_index(sym, mname);
                        if (cfi < 0) continue;
                        LLVMValueRef cptr = emit_field_ptr(g, sym, st, obj,
                                                           cfi, "cinit.ptr");
                        zan_store_fit(g, LLVMBuildLoad2(g->builder, coll_lt,
                            cptr, "cinit.load"), cslot);
                    }
                    local_add(locals, mname, cslot, msym->type);
                    for (int k = 0; k < arg->coll_init.items.count; k++) {
                        zan_ast_node_t *item = arg->coll_init.items.items[k];
                        zan_ast_node_t *madd = zan_ast_new(g->arena,
                            AST_MEMBER_ACCESS, item->loc);
                        madd->member.object = recv;
                        madd->member.name = (zan_istr_t){"Add", 3};
                        madd->member.null_cond = 0;
                        zan_ast_node_t *addcall = zan_ast_new(g->arena,
                            AST_CALL, item->loc);
                        addcall->call.callee = madd;
                        zan_ast_list_init(&addcall->call.args);
                        zan_ast_list_init(&addcall->call.type_args);
                        zan_ast_list_push(&addcall->call.args, item, g->arena);
                        if (msym->type && type_named(msym->type, "Dict", 4) &&
                            k + 1 < arg->coll_init.items.count) {
                            zan_ast_list_push(&addcall->call.args,
                                arg->coll_init.items.items[++k], g->arena);
                        }
                        /* 支持流式 API 的 Add 方法处理 */
                        LLVMValueRef addres = emit_expr(g, addcall, locals);
                        if (addres && expr_yields_owned_rc_value(g, addcall,
                                                      locals)) {
                            zan_type_t *rt = infer_expr_type(g, addcall,
                                                             locals);
                            if (rt && is_rc_managed_type(rt) &&
                                LLVMGetTypeKind(LLVMTypeOf(addres))
                                    == LLVMPointerTypeKind) {
                                emit_rc_release_for_type(g, rt, addres);
                            }
                        }
                    }
                    for (int lv = locals->count - 1; lv >= 0; lv--) {
                        if (locals->vars[lv].name.len == mname.len &&
                            memcmp(locals->vars[lv].name.str, mname.str,
                                   (size_t)mname.len) == 0) {
                            for (int sh = lv; sh < locals->count - 1; sh++)
                                locals->vars[sh] = locals->vars[sh + 1];
                            locals->count--;
                            break;
                        }
                    }
                    if (getter_owned) {
                        LLVMValueRef cv = LLVMBuildLoad2(g->builder, coll_lt,
                            cslot, "cinit.done");
                        emit_rc_release_for_type(g, msym->type, cv);
                    }
                    continue;
                }
                /* 普通字段/属性写入：通过合成访问器 */
                zan_ast_node_t *mref = zan_ast_new(g->arena,
                    AST_MEMBER_ACCESS, arg->loc);
                mref->member.object = expr->new_expr.call_init;
                mref->member.name = mname;
                mref->member.null_cond = 0;
                zan_ast_node_t *setn = zan_ast_new(g->arena,
                    AST_ASSIGNMENT, arg->loc);
                setn->binary.op = TK_EQ;
                setn->binary.left = mref;
                setn->binary.right = arg->binary.right;
                emit_expr(g, setn, locals);
            }
            return obj;
        }
        /* 匿名对象字面量 new { ... } 降解：
         * 依次计算各成员表达式（保留求值副作用），返回 null 对象引用作为轻量占位 */
        if (!expr->new_expr.type) {
            for (int i = 0; i < expr->new_expr.args.count; i++) {
                emit_expr(g, expr->new_expr.args.items[i], locals);
            }
            return LLVMConstNull(LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0));
        }
        if (expr->new_expr.type && expr->new_expr.type->kind == AST_TYPE_REF &&
            !expr->new_expr.is_array) {
            zan_istr_t spn = expr->new_expr.type->type_ref.name;
            if (spn.len == 4 && memcmp(spn.str, "Span", 4) == 0 &&
                expr->new_expr.args.count == 2) {
                LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
                LLVMValueRef base = emit_expr(g, expr->new_expr.args.items[0], locals);
                if (LLVMGetTypeKind(LLVMTypeOf(base)) == LLVMIntegerTypeKind)
                    base = LLVMBuildIntToPtr(g->builder, base, i8ptr, "span.b2p");
                else
                    base = LLVMBuildBitCast(g->builder, base, i8ptr, "span.bbc");
                LLVMValueRef len = coerce_int_to(g,
                    emit_expr(g, expr->new_expr.args.items[1], locals), i64t);
                LLVMValueRef negative_len = zan_icmp(g->builder, LLVMIntSLT, len,
                    LLVMConstInt(i64t, 0, 0), "span.len.neg");
                emit_runtime_check(g, negative_len, expr->new_expr.args.items[1]->loc,
                                   "negative span length");
                LLVMValueRef v = LLVMGetUndef(g->span_struct_type);
                v = LLVMBuildInsertValue(g->builder, v, base, 0, "span.n0");
                v = LLVMBuildInsertValue(g->builder, v, len, 1, "span.n1");
                return v;
            }
        }
        /* new List<T>() 内建动态列表实例分配 */
        if (expr->new_expr.type && expr->new_expr.type->kind == AST_TYPE_REF &&
            !expr->new_expr.is_array) {
            zan_istr_t tname = expr->new_expr.type->type_ref.name;
            if (tname.len == 4 && memcmp(tname.str, "List", 4) == 0) {
                LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                zan_type_t *lelem = NULL;
                {
                    zan_type_t *lt = resolve_type_ctx(g, expr->new_expr.type);
                    if (lt) lelem = container_elem_type(lt);
                }
                LLVMValueRef list_ptr = emit_alloc_rc_collection(g, expr, 24, 1, lelem);
                /* cast to List* */
                LLVMValueRef typed_ptr = LLVMBuildBitCast(g->builder, list_ptr,
                    LLVMPointerType(g->list_struct_type, 0), "lptr");
                /* new List<T>(src) 拷贝构造函数降解 */
                if (expr->new_expr.list_copy && expr->new_expr.args.count == 1) {
                    LLVMValueRef src_raw =
                        emit_expr(g, expr->new_expr.args.items[0], locals);
                    if (LLVMGetTypeKind(LLVMTypeOf(src_raw)) != LLVMPointerTypeKind) {
                        src_raw = LLVMBuildIntToPtr(g->builder, src_raw, i8ptr,
                                                    "cpy.src.ip");
                    }
                    LLVMValueRef src_ptr = LLVMBuildBitCast(g->builder, src_raw,
                        LLVMPointerType(g->list_struct_type, 0), "cpy.src");
                    LLVMValueRef src_cnt = LLVMBuildLoad2(g->builder, i64,
                        LLVMBuildStructGEP2(g->builder, g->list_struct_type,
                            src_ptr, 0, "src.cnt"), "src.n");
                    LLVMValueRef src_data = LLVMBuildLoad2(g->builder,
                        LLVMPointerType(i64, 0),
                        LLVMBuildStructGEP2(g->builder, g->list_struct_type,
                            src_ptr, 2, "src.df"), "src.d");

                    LLVMValueRef eight = LLVMConstInt(i64, 8, 0);
                    LLVMValueRef cap = LLVMBuildSelect(g->builder,
                        zan_icmp(g->builder, LLVMIntSGT, src_cnt, eight, "cpy.gt8"),
                        src_cnt, eight, "cpy.cap");
                    zan_store_fit(g, src_cnt, LLVMBuildStructGEP2(g->builder,
                        g->list_struct_type, typed_ptr, 0, "cnt"));
                    zan_store_fit(g, cap, LLVMBuildStructGEP2(g->builder,
                        g->list_struct_type, typed_ptr, 1, "cap"));
                    unsigned lwords = elem_slot_words(g, lelem);
                    LLVMValueRef bytes = LLVMBuildMul(g->builder, cap,
                        LLVMConstInt(i64, (unsigned long long)(8 * lwords), 0),
                        "cpy.bytes");
                    LLVMValueRef data_ptr = zan_call2(g->builder,
                        LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i64, i64 }, 2, 0),
                        get_calloc_fn(g),
                        (LLVMValueRef[]){ LLVMConstInt(i64, 1, 0), bytes }, 2,
                        "cpy.data");
                    zan_irgen_emit_oom_check(g, g->current_fn, data_ptr);
                    LLVMValueRef dst_data = LLVMBuildBitCast(g->builder, data_ptr,
                        LLVMPointerType(i64, 0), "cpy.dp");
                    zan_store_fit(g, dst_data, LLVMBuildStructGEP2(g->builder,
                        g->list_struct_type, typed_ptr, 2, "df"));

                    LLVMValueRef cfn = LLVMGetBasicBlockParent(
                        LLVMGetInsertBlock(g->builder));
                    LLVMBasicBlockRef saved_bb =
                        LLVMGetInsertBlock(g->builder);
                    LLVMBasicBlockRef cond_bb = LLVMAppendBasicBlockInContext(
                        g->ctx, cfn, "cpy.cond");
                    LLVMBasicBlockRef body_bb = LLVMAppendBasicBlockInContext(
                        g->ctx, cfn, "cpy.body");
                    LLVMBasicBlockRef end_bb = LLVMAppendBasicBlockInContext(
                        g->ctx, cfn, "cpy.end");
                    LLVMValueRef ip = LLVMBuildAlloca(g->builder, i64, "cpy.i");
                    LLVMBuildStore(g->builder, LLVMConstInt(i64, 0, 0), ip);
                    LLVMBuildBr(g->builder, cond_bb);
                    LLVMPositionBuilderAtEnd(g->builder, cond_bb);
                    LLVMValueRef iv = LLVMBuildLoad2(g->builder, i64, ip, "cpy.iv");
                    LLVMBuildCondBr(g->builder,
                        zan_icmp(g->builder, LLVMIntSLT, iv, src_cnt, "cpy.more"),
                        body_bb, end_bb);
                    LLVMPositionBuilderAtEnd(g->builder, body_bb);
                    iv = LLVMBuildLoad2(g->builder, i64, ip, "cpy.iv2");
                    LLVMValueRef off = LLVMBuildMul(g->builder, iv,
                        LLVMConstInt(i64, lwords, 0), "cpy.off");
                    LLVMValueRef s_slot = LLVMBuildGEP2(g->builder, i64,
                        src_data, &off, 1, "cpy.ss");
                    LLVMValueRef d_slot = LLVMBuildGEP2(g->builder, i64,
                        dst_data, &off, 1, "cpy.ds");
                    LLVMTypeRef elem_llvm = lelem ? map_type(g, lelem) : i64;
                    LLVMValueRef v;
                    if (LLVMGetTypeKind(elem_llvm) == LLVMStructTypeKind) {
                        v = load_struct_from_slot(g, s_slot, elem_llvm);
                    } else {
                        v = LLVMBuildLoad2(g->builder, i64, s_slot, "cpy.sv");
                        if (LLVMGetTypeKind(elem_llvm) == LLVMPointerTypeKind) {
                            v = LLVMBuildIntToPtr(g->builder, v, elem_llvm, "cpy.pv");
                        } else if (LLVMGetTypeKind(elem_llvm) == LLVMDoubleTypeKind) {
                            v = LLVMBuildBitCast(g->builder, v, elem_llvm, "cpy.dv");
                        }
                    }
                    emit_collection_slot_store(g, lelem, i64, d_slot, v,
                                               NULL, locals, 0);
                    LLVMValueRef nxt = LLVMBuildAdd(g->builder, iv,
                        LLVMConstInt(i64, 1, 0), "cpy.nx");
                    LLVMBuildStore(g->builder, nxt, ip);
                    LLVMBuildBr(g->builder, cond_bb);
                    LLVMPositionBuilderAtEnd(g->builder, end_bb);
                    return LLVMBuildBitCast(g->builder, typed_ptr, i8ptr, "listv");
                }
                /* new List<T>(array) 数组拷贝构造：数组不是 List，落进下面
                 * 的初始化式路径会把整个数组对象当成 1 个元素 Add 进去
                 * （count=1、读元素得到假扮字符串的数组头、下标 1 越界，
                 * 全程静默——Process 参数列表全是垃圾，实测踩过）。这里
                 * 按数组布局（长度在 ptr-16，元素从 ptr+0 紧凑排布）逐
                 * 元素拷进 List 的字槽。 */
                if (!expr->new_expr.list_copy && expr->new_expr.args.count == 1
                        && expr->new_expr.arg_inits.count == 0) {
                    zan_ast_node_t *asrc = expr->new_expr.args.items[0];
                    zan_type_t *at = asrc
                        ? infer_expr_type(g, asrc, locals) : NULL;
                    if (at && at->kind == TYPE_ARRAY && at->array_rank == 1
                            && at->element_type) {
                        LLVMValueRef arr = emit_expr(g, asrc, locals);
                        LLVMTypeRef aetl = map_type(g, at->element_type);
                        LLVMTypeKind aek = LLVMGetTypeKind(aetl);
                        unsigned alwords = elem_slot_words(g, lelem);
                        LLVMValueRef alen = zan_array_len(g, arr);
                        LLVMValueRef aeight = LLVMConstInt(i64, 8, 0);
                        LLVMValueRef acap = LLVMBuildSelect(g->builder,
                            zan_icmp(g->builder, LLVMIntSGT, alen, aeight,
                                     "acpy.gt8"),
                            alen, aeight, "acpy.cap");
                        zan_store_fit(g, alen, LLVMBuildStructGEP2(g->builder,
                            g->list_struct_type, typed_ptr, 0, "acnt"));
                        zan_store_fit(g, acap, LLVMBuildStructGEP2(g->builder,
                            g->list_struct_type, typed_ptr, 1, "acap"));
                        LLVMValueRef abytes = LLVMBuildMul(g->builder, acap,
                            LLVMConstInt(i64,
                                (unsigned long long)(8 * alwords), 0),
                            "acpy.bytes");
                        LLVMValueRef adata = zan_call2(g->builder,
                            LLVMFunctionType(i8ptr,
                                (LLVMTypeRef[]){ i64, i64 }, 2, 0),
                            get_calloc_fn(g),
                            (LLVMValueRef[]){
                                LLVMConstInt(i64, 1, 0), abytes }, 2,
                            "acpy.data");
                        zan_irgen_emit_oom_check(g, g->current_fn, adata);
                        LLVMValueRef adp = LLVMBuildBitCast(g->builder, adata,
                            LLVMPointerType(i64, 0), "acpy.dp");
                        zan_store_fit(g, adp, LLVMBuildStructGEP2(g->builder,
                            g->list_struct_type, typed_ptr, 2, "adf"));
                        LLVMValueRef acfn = LLVMGetBasicBlockParent(
                            LLVMGetInsertBlock(g->builder));
                        LLVMBasicBlockRef acond = LLVMAppendBasicBlockInContext(
                            g->ctx, acfn, "acpy.cond");
                        LLVMBasicBlockRef abody = LLVMAppendBasicBlockInContext(
                            g->ctx, acfn, "acpy.body");
                        LLVMBasicBlockRef aend = LLVMAppendBasicBlockInContext(
                            g->ctx, acfn, "acpy.end");
                        LLVMValueRef aip = LLVMBuildAlloca(g->builder, i64,
                                                           "acpy.i");
                        LLVMBuildStore(g->builder, LLVMConstInt(i64, 0, 0),
                                       aip);
                        LLVMBuildBr(g->builder, acond);
                        LLVMPositionBuilderAtEnd(g->builder, acond);
                        LLVMValueRef aiv = LLVMBuildLoad2(g->builder, i64,
                                                          aip, "acpy.iv");
                        LLVMBuildCondBr(g->builder,
                            zan_icmp(g->builder, LLVMIntSLT, aiv, alen,
                                     "acpy.more"),
                            abody, aend);
                        LLVMPositionBuilderAtEnd(g->builder, abody);
                        aiv = LLVMBuildLoad2(g->builder, i64, aip, "acpy.iv2");
                        LLVMValueRef aslot;
                        if (aek == LLVMStructTypeKind) {
                            aslot = LLVMBuildGEP2(g->builder, aetl, arr,
                                                  &aiv, 1, "acpy.ss");
                        } else {
                            LLVMValueRef aptr = LLVMBuildBitCast(g->builder,
                                arr, LLVMPointerType(aetl, 0), "acpy.ab");
                            aslot = LLVMBuildGEP2(g->builder, aetl, aptr,
                                                  &aiv, 1, "acpy.ss");
                        }
                        LLVMValueRef av;
                        if (aek == LLVMStructTypeKind) {
                            av = load_struct_from_slot(g, aslot, aetl);
                        } else {
                            av = LLVMBuildLoad2(g->builder, aetl, aslot,
                                                "acpy.sv");
                            if (aek == LLVMIntegerTypeKind) {
                                unsigned aw = LLVMGetIntTypeWidth(aetl);
                                if (aw < 64) {
                                    av = LLVMBuildSExt(g->builder, av, i64,
                                                       "acpy.sw");
                                } else if (aw > 64) {
                                    av = LLVMBuildTrunc(g->builder, av, i64,
                                                        "acpy.tw");
                                }
                            }
                        }
                        LLVMValueRef awpos = slot_word_index(g, aiv,
                                                             alwords);
                        LLVMValueRef adst = LLVMBuildGEP2(g->builder, i64,
                            adp, &awpos, 1, "acpy.ds");
                        emit_collection_slot_store(g, lelem, i64, adst, av,
                                                   NULL, locals, 0);
                        LLVMValueRef anx = LLVMBuildAdd(g->builder, aiv,
                            LLVMConstInt(i64, 1, 0), "acpy.nx");
                        LLVMBuildStore(g->builder, anx, aip);
                        LLVMBuildBr(g->builder, acond);
                        LLVMPositionBuilderAtEnd(g->builder, aend);
                        return LLVMBuildBitCast(g->builder, typed_ptr, i8ptr,
                                                "listv");
                    }
                }
                /* 集合初始化式项处理 (new List<T>{ a, b, c })：发射逐项 Add 调用 */
                int ninit = expr->new_expr.args.count +
                            expr->new_expr.arg_inits.count;
                if (ninit == 0) {
                    LLVMValueRef count_ptr = LLVMBuildStructGEP2(g->builder, g->list_struct_type, typed_ptr, 0, "cnt");
                    zan_store_fit(g, LLVMConstInt(i64, 0, 0), count_ptr);
                    LLVMValueRef cap_ptr = LLVMBuildStructGEP2(g->builder, g->list_struct_type, typed_ptr, 1, "cap");
                    zan_store_fit(g, LLVMConstInt(i64, 0, 0), cap_ptr);
                    LLVMValueRef data_field = LLVMBuildStructGEP2(g->builder, g->list_struct_type, typed_ptr, 2, "df");
                    zan_store_fit(g, LLVMConstNull(LLVMPointerType(i64, 0)), data_field);
                    return LLVMBuildBitCast(g->builder, typed_ptr, i8ptr, "listv");
                }
                long long initcap = ninit > 8 ? (long long)ninit : 8;
                /* count = ninit */
                LLVMValueRef count_ptr = LLVMBuildStructGEP2(g->builder, g->list_struct_type, typed_ptr, 0, "cnt");
                zan_store_fit(g, LLVMConstInt(i64, (unsigned long long)ninit, 0), count_ptr);
                LLVMValueRef cap_ptr = LLVMBuildStructGEP2(g->builder, g->list_struct_type, typed_ptr, 1, "cap");
                zan_store_fit(g, LLVMConstInt(i64, (unsigned long long)initcap, 0), cap_ptr);
                unsigned lwords = elem_slot_words(g, lelem);
                LLVMValueRef data_size = LLVMConstInt(i64,
                    (unsigned long long)(initcap * 8 * lwords), 0);
                LLVMValueRef data_ptr = zan_call2(g->builder,
                    LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i64, i64 }, 2, 0),
                    get_calloc_fn(g), (LLVMValueRef[]){ LLVMConstInt(i64, 1, 0), data_size }, 2, "data");
                zan_irgen_emit_oom_check(g, g->current_fn, data_ptr);
                LLVMValueRef data_typed = LLVMBuildBitCast(g->builder, data_ptr,
                    LLVMPointerType(i64, 0), "dptr");
                LLVMValueRef data_field = LLVMBuildStructGEP2(g->builder, g->list_struct_type, typed_ptr, 2, "df");
                zan_store_fit(g, data_typed, data_field);
                for (int ii = 0; ii < ninit; ii++) {
                    zan_ast_node_t *item = ii < expr->new_expr.args.count
                        ? expr->new_expr.args.items[ii]
                        : expr->new_expr.arg_inits.items[
                              ii - expr->new_expr.args.count];
                    LLVMValueRef idxk = LLVMConstInt(i64,
                        (unsigned long long)ii * lwords, 0);
                    LLVMValueRef slot = LLVMBuildGEP2(g->builder, i64, data_typed, &idxk, 1, "iis");
                    LLVMValueRef ival = emit_expr(g, item, locals);
                    emit_collection_slot_store(g, lelem, i64, slot, ival, item, locals, 0);
                }
                return LLVMBuildBitCast(g->builder, typed_ptr, i8ptr, "listv");
            }
        }

        if (expr->new_expr.type && expr->new_expr.type->kind == AST_TYPE_REF &&
            !expr->new_expr.is_array) {
            zan_istr_t sbname = expr->new_expr.type->type_ref.name;
            if (sbname.len == 13 && memcmp(sbname.str, "StringBuilder", 13) == 0) {
                LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                LLVMValueRef sb_raw = emit_alloc_rc_collection(g, expr, 24, 2, NULL);
                LLVMValueRef sb_ptr = LLVMBuildBitCast(g->builder, sb_raw,
                    LLVMPointerType(g->sb_struct_type, 0), "sbp");
                LLVMValueRef cnt_p = LLVMBuildStructGEP2(g->builder, g->sb_struct_type, sb_ptr, 0, "sbc");
                zan_store_fit(g, LLVMConstInt(i64, 0, 0), cnt_p);
                LLVMValueRef cap_p = LLVMBuildStructGEP2(g->builder, g->sb_struct_type, sb_ptr, 1, "sbcap");
                zan_store_fit(g, LLVMConstInt(i64, 0, 0), cap_p);
                LLVMValueRef data_p = LLVMBuildStructGEP2(g->builder, g->sb_struct_type, sb_ptr, 2, "sbd");
                zan_store_fit(g, LLVMConstNull(i8ptr), data_p);
                return LLVMBuildBitCast(g->builder, sb_ptr, i8ptr, "sbv");
            }
        }

        if (expr->new_expr.type && expr->new_expr.type->kind == AST_TYPE_REF &&
            !expr->new_expr.is_array) {
            zan_istr_t tname2 = expr->new_expr.type->type_ref.name;
            if ((tname2.len == 4 && memcmp(tname2.str, "Dict", 4) == 0) ||
                (tname2.len == 10 && memcmp(tname2.str, "Dictionary", 10) == 0)) {
                LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                LLVMValueRef dict_raw = emit_alloc_rc_collection(g, expr, 64, 3,
                    resolve_type_ctx(g, expr->new_expr.type));
                LLVMValueRef typed_ptr = LLVMBuildBitCast(g->builder, dict_raw,
                    LLVMPointerType(g->dict_struct_type, 0), "dptr");
                for (unsigned zf = 4; zf < 7; zf++) {
                    LLVMValueRef zp = LLVMBuildStructGEP2(g->builder,
                        g->dict_struct_type, typed_ptr, zf, "dz");
                    zan_store_fit(g,
                        (zf == 4) ? (LLVMValueRef)LLVMConstNull(LLVMPointerType(i64, 0))
                                  : (LLVMValueRef)LLVMConstInt(i64, 0, 0), zp);
                }
                zan_type_t *dict_type = resolve_type_ctx(g, expr->new_expr.type);
                zan_type_t *value_type = dict_value_type(dict_type);
                unsigned value_words = elem_slot_words(g, value_type);
                LLVMValueRef value_words_const = LLVMConstInt(i64, value_words, 0);
                zan_store_fit(g, value_words_const,
                    LLVMBuildStructGEP2(g->builder, g->dict_struct_type, typed_ptr, 7, "vwp"));
                /* 字典初始化式条目处理 (new Dict<K,V>{ {k, v}, ... })：发射逐项索引器写入 */
                zan_ast_list_t dict_inits;
                zan_ast_list_init(&dict_inits);
                for (int di = 0; di < expr->new_expr.args.count; di++)
                    zan_ast_list_push(&dict_inits,
                        expr->new_expr.args.items[di], g->arena);
                for (int di = 0; di < expr->new_expr.arg_inits.count; di++)
                    zan_ast_list_push(&dict_inits,
                        expr->new_expr.arg_inits.items[di], g->arena);
                int ninit = dict_inits.count;
                if (ninit == 0) {
                    /* count = 0 */
                    LLVMValueRef cnt_p = LLVMBuildStructGEP2(g->builder, g->dict_struct_type, typed_ptr, 0, "cnt");
                    zan_store_fit(g, LLVMConstInt(i64, 0, 0), cnt_p);
                    /* capacity = 0 (惰性分配，避免空字典产生键值数组微碎片) */
                    LLVMValueRef cap_p = LLVMBuildStructGEP2(g->builder, g->dict_struct_type, typed_ptr, 1, "cap");
                    zan_store_fit(g, LLVMConstInt(i64, 0, 0), cap_p);
                    /* keys = NULL */
                    LLVMValueRef kf = LLVMBuildStructGEP2(g->builder, g->dict_struct_type, typed_ptr, 2, "kf");
                    zan_store_fit(g, LLVMConstNull(LLVMPointerType(i8ptr, 0)), kf);
                    /* values = NULL */
                    LLVMValueRef vf = LLVMBuildStructGEP2(g->builder, g->dict_struct_type, typed_ptr, 3, "vf");
                    zan_store_fit(g, LLVMConstNull(LLVMPointerType(i64, 0)), vf);
                    return LLVMBuildBitCast(g->builder, typed_ptr, i8ptr, "dictv");
                }
                /* count = 0 */
                LLVMValueRef cnt_p = LLVMBuildStructGEP2(g->builder, g->dict_struct_type, typed_ptr, 0, "cnt");
                zan_store_fit(g, LLVMConstInt(i64, 0, 0), cnt_p);
                /* capacity = 16 */
                LLVMValueRef cap_p = LLVMBuildStructGEP2(g->builder, g->dict_struct_type, typed_ptr, 1, "cap");
                zan_store_fit(g, LLVMConstInt(i64, 16, 0), cap_p);
                /* keys = malloc(16 * 8) */
                LLVMValueRef keys_sz = LLVMConstInt(i64, 128, 0);
                LLVMValueRef keys_raw = zan_call2(g->builder,
                    LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i64, i64 }, 2, 0),
                    get_calloc_fn(g), (LLVMValueRef[]){ LLVMConstInt(i64, 1, 0), keys_sz }, 2, "keys");
                zan_irgen_emit_oom_check(g, g->current_fn, keys_raw);
                LLVMValueRef keys_typed = LLVMBuildBitCast(g->builder, keys_raw,
                    LLVMPointerType(i8ptr, 0), "kptr");
                LLVMValueRef kf = LLVMBuildStructGEP2(g->builder, g->dict_struct_type, typed_ptr, 2, "kf");
                zan_store_fit(g, keys_typed, kf);
                /* values = malloc(16 * 8) */
                LLVMValueRef vals_sz = LLVMConstInt(i64, 128ULL * value_words, 0);
                LLVMValueRef vals_raw = zan_call2(g->builder,
                    LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i64, i64 }, 2, 0),
                    get_calloc_fn(g), (LLVMValueRef[]){ LLVMConstInt(i64, 1, 0), vals_sz }, 2, "vals");
                zan_irgen_emit_oom_check(g, g->current_fn, vals_raw);
                LLVMValueRef vals_typed = LLVMBuildBitCast(g->builder, vals_raw,
                    LLVMPointerType(i64, 0), "vptr");
                LLVMValueRef vf = LLVMBuildStructGEP2(g->builder, g->dict_struct_type, typed_ptr, 3, "vf");
                zan_store_fit(g, vals_typed, vf);
                if (ninit >= 2) {
                    zan_type_t *kt = dict_key_type(g, dict_type);
                    for (int ii = 0; ii + 1 < ninit; ii += 2) {
                        zan_ast_node_t *kexpr = dict_inits.items[ii];
                        zan_ast_node_t *vexpr = dict_inits.items[ii + 1];
                        LLVMValueRef key = emit_expr(g, kexpr, locals);
                        if (LLVMGetTypeKind(LLVMTypeOf(key)) == LLVMIntegerTypeKind) {
                            if (LLVMGetIntTypeWidth(LLVMTypeOf(key)) < 64)
                                key = LLVMBuildSExt(g->builder, key, i64, "k.sx");
                            key = LLVMBuildIntToPtr(g->builder, key, i8ptr, "k.ip");
                        } else if (LLVMTypeOf(key) != i8ptr) {
                            key = LLVMBuildBitCast(g->builder, key, i8ptr, "k.bc");
                        }
                        LLVMValueRef val = emit_expr(g, vexpr, locals);
                        emit_dict_value_set(g, dict_type, dict_raw, key, val,
                            kexpr, vexpr, locals);
                    }
                }
                return LLVMBuildBitCast(g->builder, typed_ptr, i8ptr, "dictv");
            }
        }

        /* 数组初始化式 (new Type[] { a, b, c })：分配连续内存并填充初始元素 */
        if (expr->new_expr.is_array && expr->new_expr.array_init) {
            zan_type_t *elem_type = resolve_type_ctx(g, expr->new_expr.type);
            if (elem_type && elem_type->kind == TYPE_ARRAY && elem_type->element_type)
                elem_type = elem_type->element_type;
            if (!elem_type) elem_type = g->binder->type_int;
            LLVMTypeRef elem_llvm = map_type(g, elem_type);
            LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            int n = expr->new_expr.args.count;
            LLVMValueRef total = zan_mul(g->builder,
                LLVMConstInt(i64t, (unsigned long long)n, 0),
                LLVMSizeOf(elem_llvm), "total");
            LLVMValueRef arr = zan_array_alloc_typed(g, total,
                LLVMConstInt(i64t, (unsigned long long)n, 0), elem_type);
            for (int k = 0; k < n; k++) {
                LLVMValueRef v = emit_arg_typed(g, expr->new_expr.args.items[k],
                                                elem_type, locals);
                /* 数组缓冲区持有元素所有权：ARC 引用类型元素保留 +1 计数 */
                if (elem_type && is_rc_managed_type(elem_type) &&
                    !expr_yields_owned_rc_value(g, expr->new_expr.args.items[k],
                                                locals)) {
                    emit_rc_retain_for_type(g, elem_type, v);
                }
                LLVMValueRef idx = LLVMConstInt(i64t, (unsigned long long)k, 0);
                LLVMValueRef slot = LLVMBuildGEP2(g->builder, elem_llvm, arr,
                                                  &idx, 1, "aep");
                LLVMBuildStore(g->builder, emit_boundary_coerce(g, v, elem_llvm), slot);
            }
            return arr;
        }

        /* 数组分配 (new Type[d1, d2, ...])：多维数组分配形状描述符与平铺缓冲区 */
        if (expr->new_expr.is_array && expr->new_expr.args.count > 0) {
            zan_type_t *alloc_type = resolve_type_ctx(g, expr->new_expr.type);
            if (!alloc_type) alloc_type = g->binder->type_int;
            int rank = expr->new_expr.array_rank > 0
                ? expr->new_expr.array_rank : 1;
            zan_type_t *elem_type = alloc_type;
            if (elem_type->kind == TYPE_ARRAY && elem_type->element_type)
                elem_type = elem_type->element_type;
            LLVMTypeRef elem_llvm = map_type(g, elem_type);
            LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
            int ndims = rank > 16 ? 16 : rank;
            LLVMValueRef dims[16];
            for (int d = 0; d < ndims; d++) {
                LLVMValueRef dv = emit_expr(g, expr->new_expr.args.items[d], locals);
                dv = emit_index_i64(g, dv, "arr.dim");
                LLVMValueRef negative = zan_icmp(g->builder, LLVMIntSLT, dv,
                    LLVMConstInt(i64t, 0, 0), "arr.negative");
                emit_runtime_check(g, negative,
                    expr->new_expr.args.items[d]->loc, "negative array length");
                /* 数组维度负数检查与越界防护 */
                dims[d] = LLVMBuildSelect(g->builder, negative,
                    LLVMConstInt(i64t, 0, 0), dv, "arr.dim.safe");
            }
            LLVMValueRef arr;
            if (rank <= 1) {
                LLVMValueRef total = zan_mul(g->builder, dims[0],
                    LLVMSizeOf(elem_llvm), "total");
                arr = zan_array_alloc_typed(g, total, dims[0], elem_type);
            } else {
                arr = zan_mdarray_alloc(g, dims, rank, elem_llvm);
            }
            int ninit = expr->new_expr.args.count - ndims;
            if (ninit > 0) {
                LLVMValueRef data = arr;
                if (rank > 1) {
                    LLVMValueRef data_off = LLVMConstInt(i64t,
                        (unsigned long long)rank * 8, 0);
                    data = LLVMBuildGEP2(g->builder, LLVMInt8TypeInContext(g->ctx),
                        arr, &data_off, 1, "md.data");
                }
                LLVMValueRef typed = LLVMBuildBitCast(g->builder, data,
                    LLVMPointerType(elem_llvm, 0), "aip");
                for (int k = 0; k < ninit; k++) {
                    LLVMValueRef v = emit_arg_typed(g,
                        expr->new_expr.args.items[ndims + k], elem_type, locals);
                    if (elem_type && is_rc_managed_type(elem_type) &&
                        !expr_yields_owned_rc_value(g,
                            expr->new_expr.args.items[ndims + k], locals)) {
                        emit_rc_retain_for_type(g, elem_type, v);
                    }
                    LLVMValueRef idx = LLVMConstInt(i64t, (unsigned long long)k, 0);
                    LLVMValueRef slot = LLVMBuildGEP2(g->builder, elem_llvm,
                        typed, &idx, 1, "aep");
                    LLVMBuildStore(g->builder,
                        emit_boundary_coerce(g, v, elem_llvm), slot);
                }
            }
            return arr;
        }

        zan_istr_t type_name = {NULL, 0};
        if (expr->new_expr.type) {
            if (expr->new_expr.type->kind == AST_IDENTIFIER) {
                type_name = expr->new_expr.type->ident.name;
            } else if (expr->new_expr.type->kind == AST_TYPE_REF) {
                type_name = expr->new_expr.type->type_ref.name;
            }
        }
        if (type_name.str && type_name.len == 6 && memcmp(type_name.str, "object", 6) == 0) {
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
            LLVMValueRef sz = LLVMConstInt(i64, 8, 0);
            LLVMValueRef site_name = LLVMConstNull(i8ptr);
            int site_idx = reserve_arc_site(g, NULL, NULL, 0, NULL);
            LLVMValueRef site_val = arc_site_arg(g, site_idx);
            if (g->check_leaks) {
                char site_buf[600];
                const char *sfile = loc_site_file(g, expr->loc);
                snprintf(site_buf, sizeof(site_buf), "%s:%u:%u",
                         sfile, expr->loc.line, expr->loc.col);
                site_name = zan_irgen_intern_string(g, site_buf);
            }
            LLVMTypeRef alloc_fn_type = LLVMFunctionType(i8ptr,
                (LLVMTypeRef[]){ i64, i64, i8ptr }, 3, 0);
            LLVMValueRef alloc_args3[] = { sz, site_val, site_name };
            LLVMValueRef raw = zan_call2(g->builder, alloc_fn_type, g->rt_alloc, alloc_args3, 3, "newobj");
            LLVMValueRef zero64 = LLVMConstInt(i64, 0, 0);
            LLVMValueRef p64 = LLVMBuildBitCast(g->builder, raw, LLVMPointerType(i64, 0), "objp");
            zan_store_fit(g, zero64, p64);
            return raw;
        }
        zan_type_t *actual_t = resolve_type_ctx(g, expr->new_expr.type);
        if (actual_t && (actual_t->kind == TYPE_INT || actual_t->kind == TYPE_UINT ||
                         actual_t->kind == TYPE_LONG || actual_t->kind == TYPE_ULONG ||
                         actual_t->kind == TYPE_SHORT || actual_t->kind == TYPE_USHORT ||
                         actual_t->kind == TYPE_BYTE || actual_t->kind == TYPE_SBYTE ||
                         actual_t->kind == TYPE_BOOL || actual_t->kind == TYPE_FLOAT ||
                         actual_t->kind == TYPE_DOUBLE || actual_t->kind == TYPE_CHAR ||
                         actual_t->kind == TYPE_NINT)) {
            LLVMTypeRef lt = map_type(g, actual_t);
            return LLVMConstNull(lt);
        }
        zan_symbol_t *sym = NULL;
        if (actual_t && actual_t->sym &&
            (actual_t->sym->type->kind == TYPE_STRUCT || actual_t->sym->type->kind == TYPE_CLASS)) {
            sym = actual_t->sym;
        } else if (type_name.str) {
            sym = zan_binder_lookup(g->binder, type_name);
        }
        if (sym) {
            if (sym->type && (sym->type->kind == TYPE_STRUCT || sym->type->kind == TYPE_CLASS)) {
                LLVMTypeRef st = get_struct_llvm_type(g, sym);
                if (st) {
                    LLVMValueRef alloca;
                    if (sym->type->kind == TYPE_CLASS) {
                        LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                        LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                        LLVMValueRef sz = LLVMSizeOf(st);
                        LLVMValueRef site_name = LLVMConstNull(i8ptr);
                        LLVMValueRef site_val;
                        {
                            /* 分配固定分配点索引用于内存剖析与诊断 */
                            zan_type_t *site_inst =
                                resolve_type_ctx(g, expr->new_expr.type);
                            int site_idx = reserve_arc_site(
                                g, sym, site_inst, 0, NULL);
                            site_val = arc_site_arg(g, site_idx);
                            if (g->check_leaks) {
                                char site_buf[600];
                                const char *sfile = loc_site_file(g, expr->loc);
                                snprintf(site_buf, sizeof(site_buf), "%s:%u:%u",
                                         sfile, expr->loc.line, expr->loc.col);
                                site_name = zan_irgen_intern_string(g, site_buf);
                            }
                        }
                        LLVMTypeRef alloc_fn_type = LLVMFunctionType(i8ptr,
                            (LLVMTypeRef[]){ i64, i64, i8ptr }, 3, 0);
                        LLVMValueRef alloc_args3[] = { sz, site_val, site_name };
                        LLVMValueRef raw = zan_call2(g->builder, alloc_fn_type, g->rt_alloc, alloc_args3, 3, "newobj");
                        alloca = LLVMBuildBitCast(g->builder, raw, LLVMPointerType(st, 0), "objp");
                    } else {
                        alloca = emit_entry_alloca(g, st, "new");
                    }
                    zan_store_fit(g, LLVMConstNull(st), alloca);

                    /* 查找并解析类的实例构造函数 */
                    zan_type_t *new_inst = resolve_type_ctx(g, expr->new_expr.type);

                    if (sym->type->kind == TYPE_CLASS && class_has_virtual_methods(sym)) {
                        LLVMTypeRef i8ptr_vt = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                        /* 泛型类**每个实例化一张自己的 vtable**：整类共用一张表时，
                         * 表里的槽位指向擦除版方法体，而擦除版拿不到类型实参，
                         * 方法体里的 `m is T` 恒为 false——经基类引用做虚调用时
                         * 判定恒错（静默）。实例化自己的表指向特化版方法体。 */
                        LLVMValueRef vtg = get_vtable_global_inst(g, sym, new_inst);
                        LLVMValueRef vpf0 = LLVMBuildStructGEP2(g->builder, st, alloca, 0, "vpf.init");
                        zan_store_fit(g,
                            LLVMBuildBitCast(g->builder, vtg, i8ptr_vt, "vt.i8"), vpf0);
                    }

                    /* 对象初始化式字段赋值发射 */
                    int init_start = expr->new_expr.args.count;
                    while (init_start > 0) {
                        zan_ast_node_t *a =
                            expr->new_expr.args.items[init_start - 1];
                        if (a->kind == AST_COLL_INIT) {
                            if (get_field_index(sym, a->coll_init.name) < 0)
                                break;
                            init_start--;
                            continue;
                        }
                        if (a->kind != AST_ASSIGNMENT ||
                            a->binary.left->kind != AST_IDENTIFIER ||
                            get_field_index(sym, a->binary.left->ident.name) < 0)
                            break;
                        init_start--;
                    }
                    zan_ast_list_t ctor_arg_list = expr->new_expr.args;
                    ctor_arg_list.count = init_start;
                    struct zan_ctor_entry *ctor = find_ctor(
                        g, sym, &ctor_arg_list, locals, NULL);
                    if (!ctor) {
                        zan_ast_list_t filled;
                        if (fill_ctor_default_args(g, sym, &ctor_arg_list,
                                                   &filled)) {
                            struct zan_ctor_entry *dc = find_ctor(
                                g, sym, &filled, locals, NULL);
                            if (dc) {
                                ctor = dc;
                                ctor_arg_list = filled;
                            }
                        }
                    }
                    LLVMValueRef ctor_fn = NULL;
                    LLVMTypeRef ctor_ft = NULL;
                    if (ctor && new_inst && new_inst->type_arg_count > 0)
                        ctor_fn = find_generic_ctor(g, sym, ctor->decl,
                                                    new_inst->type_args,
                                                    new_inst->type_arg_count,
                                                    &ctor_ft);
                    if (!ctor_fn && ctor) {
                        ctor_fn = ctor->fn;
                        ctor_ft = ctor->fn_type;
                    }
                    if (ctor && ctor->decl) {
                        /* 构造函数命名参数对齐与重排序 */
                        reorder_named_args_impl(g, &ctor_arg_list,
                                                expr->loc, ctor->decl);
                    }
                    if (ctor_fn) {
                        int argc = ctor_arg_list.count + 1;
                        LLVMValueRef *call_args = (LLVMValueRef *)calloc((size_t)argc, sizeof(LLVMValueRef));
                        call_args[0] = alloca;
                        int obj_eh_pushed = 0;
                        if (sym->type->kind == TYPE_CLASS) {
                            emit_eh_tmp_push(g, alloca);
                            obj_eh_pushed = 1;
                        }
                        /* 实参槽：构造函数抛异常时由展开器释放实参里的拥有型
                         * 临时量（与普通方法调用路径同源判定）。 */
                        int *arg_eh_pushed = (int *)calloc((size_t)argc, sizeof(int));
                        for (int k = 0; k < ctor_arg_list.count; k++) {
                            zan_ast_node_t *param =
                                ctor->decl->method_decl.params.items[k];
                            zan_type_t *pt = zan_binder_resolve_type(
                                g->binder, param->param.type);
                            /* 泛型类构造函数签名特化与参数映射 */
                            if (new_inst && new_inst->type_arg_count > 0)
                                pt = subst_delegate_sig(g, pt, new_inst);
                            call_args[k + 1] = emit_arg_typed(
                                g, ctor_arg_list.items[k], pt, locals);
                            /* 实参里的拥有型临时量（new 出来的对象/集合）归
                             * 调用方所有：正常返回后由下面的
                             * emit_release_owned_call_temp 释放；构造函数
                             * 抛异常时必须先进 EH 临时栈交给展开器，否则
                             * 这些临时量没人释放（泄漏）。普通方法调用路径
                             * 一直这么做，构造路径此前漏了。 */
                            arg_eh_pushed[k + 1] = emit_call_arg_eh_push(
                                g, ctor_arg_list.items[k], call_args[k + 1],
                                locals);
                        }
                        coerce_args_to_params(g, ctor_ft, call_args, argc);
                        zan_call_or_invoke(g, g->builder, ctor_ft, ctor_fn, call_args, (unsigned)argc, "");
                        /* 逆序弹出：实参槽先进后进（后进先出），对象槽最后弹 */
                        for (int k = argc - 1; k >= 0; k--)
                            if (arg_eh_pushed[k]) emit_eh_tmp_pop(g);
                        if (obj_eh_pushed) emit_eh_tmp_pop(g);
                        for (int k = 0; k < ctor_arg_list.count; k++) {
                            emit_release_owned_call_temp(g, ctor_arg_list.items[k],
                                call_args[k + 1], locals);
                        }
                        free(arg_eh_pushed);
                        free(call_args);
                    } else {
                        if (sym->type->kind == TYPE_CLASS &&
                            ctor_arg_list.count > 0)
                            zan_diag_emit_code(g->diag, DIAG_ERROR, expr->loc, 3024,
                                "no constructor of '%.*s' accepts %d argument%s",
                                (int)sym->name.len, sym->name.str,
                                ctor_arg_list.count,
                                ctor_arg_list.count == 1 ? "" : "s");
                        int fields_eh_pushed = 0;
                        if (sym->type->kind == TYPE_CLASS) {
                            emit_eh_tmp_push(g, alloca);
                            fields_eh_pushed = 1;
                        }

                        /* 派生类未声明构造函数时，沿继承链寻找最近的基类构造函数调用 */
                        struct zan_ctor_entry *bctor = NULL;
                        zan_symbol_t *base_sym = (sym->type && sym->type->base_type)
                            ? sym->type->base_type->sym : NULL;
                        zan_ast_list_t empty_args;
                        zan_ast_list_init(&empty_args);
                        zan_ast_list_t filled_args;
                        zan_ast_list_init(&filled_args);
                        zan_ast_list_t *actual_bargs = &empty_args;

                        while (base_sym) {
                            bctor = find_ctor(g, base_sym, &empty_args, locals, NULL);
                            if (!bctor) {
                                if (fill_ctor_default_args(g, base_sym, &empty_args, &filled_args)) {
                                    bctor = find_ctor(g, base_sym, &filled_args, locals, NULL);
                                    if (bctor) actual_bargs = &filled_args;
                                }
                            }
                            if (bctor) break;
                            if (has_declared_ctor(g, base_sym)) {
                                zan_diag_emit_code(g->diag, DIAG_ERROR, expr->loc, 3024,
                                    "base class '%.*s' of '%.*s' has no constructor accepting 0 arguments",
                                    (int)base_sym->name.len, base_sym->name.str,
                                    (int)sym->name.len, sym->name.str);
                                break;
                            }
                            base_sym = (base_sym->type && base_sym->type->base_type)
                                ? base_sym->type->base_type->sym : NULL;
                        }

                        if (bctor && bctor->fn) {
                            /* A516 顺序：派生类与所有中间类字段初始化器 -> 基类构造链（含基类字段初始化与基类构造体） */
                            zan_symbol_t *cur_init = sym;
                            while (cur_init && cur_init != bctor->type_sym) {
                                emit_decl_field_initializers(
                                    g, cur_init,
                                    cur_init == sym && new_inst ? new_inst : cur_init->type,
                                    alloca, locals);
                                cur_init = (cur_init->type && cur_init->type->base_type)
                                    ? cur_init->type->base_type->sym : NULL;
                            }

                            LLVMTypeRef bst = get_struct_llvm_type(g, bctor->type_sym);
                            LLVMValueRef base_this = alloca;
                            if (bst) {
                                base_this = LLVMBuildBitCast(
                                    g->builder, alloca,
                                    LLVMPointerType(bst, 0), "base.this");
                            }
                            int b_argc = actual_bargs->count + 1;
                            LLVMValueRef *bcall_args = (LLVMValueRef *)calloc(
                                (size_t)b_argc, sizeof(LLVMValueRef));
                            bcall_args[0] = base_this;
                            for (int k = 0; k < actual_bargs->count; k++) {
                                zan_ast_node_t *param =
                                    bctor->decl->method_decl.params.items[k];
                                zan_type_t *pt = zan_binder_resolve_type(
                                    g->binder, param->param.type);
                                bcall_args[k + 1] = emit_arg_typed(
                                    g, actual_bargs->items[k], pt, locals);
                            }
                            coerce_args_to_params(g, bctor->fn_type, bcall_args, b_argc);
                            zan_call_or_invoke(g, g->builder, bctor->fn_type, bctor->fn,
                                      bcall_args, (unsigned)b_argc, "");
                            for (int k = 0; k < actual_bargs->count; k++) {
                                emit_release_owned_call_temp(
                                    g, actual_bargs->items[k], bcall_args[k + 1], locals);
                            }
                            free(bcall_args);
                        } else {
                            /* 基类链上无显式声明的构造函数时，兜底递归发射全继承链字段初始化器 */
                            emit_implicit_field_initializers(
                                g, sym, new_inst ? new_inst : sym->type,
                                alloca, locals);
                        }

                        if (fields_eh_pushed) emit_eh_tmp_pop(g);
                    }

                    {
                        /* 构造函数参数后置的常规对象初始化式 */
                        zan_ast_list_t object_inits;
                        zan_ast_list_init(&object_inits);
                        for (int oi = init_start;
                             oi < expr->new_expr.args.count; oi++) {
                            zan_ast_list_push(&object_inits,
                                expr->new_expr.args.items[oi], g->arena);
                        }
                        for (int oi = 0;
                             oi < expr->new_expr.arg_inits.count; oi++) {
                            zan_ast_list_push(&object_inits,
                                expr->new_expr.arg_inits.items[oi], g->arena);
                        }
                        for (int i = 0; i < object_inits.count; i++) {
                            zan_ast_node_t *arg = object_inits.items[i];
                            /* 嵌套集合初始化式 Members = { a, b } 降解 */
                            if (arg->kind == AST_COLL_INIT) {
                                zan_istr_t cname = arg->coll_init.name;
                                zan_symbol_t *msym = get_field_sym(sym, cname);
                                if (!msym || !msym->type) continue;
                                int getter_owned = 0;
                                zan_ast_node_t *recv = zan_ast_new(g->arena,
                                    AST_IDENTIFIER, arg->loc);
                                recv->ident.name = cname;
                                /* 对象初始化式中隐式读取新分配实例的 this 指针 */
                                LLVMTypeRef coll_lt = map_type(g, msym->type);
                                LLVMValueRef cslot = emit_entry_alloca(g,
                                    coll_lt, "cinit.slot");
                                zan_store_fit(g, LLVMConstNull(coll_lt), cslot);
                                int cfi = get_field_index(sym, cname);
                                zan_symbol_t *getter =
                                    property_getter_sym(g, msym);
                                if (getter) {
                                    LLVMValueRef v = emit_property_getter_call(
                                        g, getter,
                                        new_inst ? new_inst : sym->type,
                                        alloca, arg, locals);
                                    zan_store_fit(g, v, cslot);
                                    /* getter 返回值持有 +1 引用处理 */
                                    getter_owned = 1;
                                } else if (cfi >= 0) {
                                    LLVMValueRef cptr = emit_field_ptr(g, sym,
                                        st, alloca, cfi, "cinit.ptr");
                                    LLVMValueRef v = LLVMBuildLoad2(g->builder,
                                        coll_lt, cptr, "cinit.load");
                                    zan_store_fit(g, v, cslot);
                                } else {
                                    continue;
                                }
                                local_add(locals, cname, cslot, msym->type);
                                /* 类类型快照借用加载语义 */
                                for (int k = 0;
                                     k < arg->coll_init.items.count; k++) {
                                    zan_ast_node_t *item =
                                        arg->coll_init.items.items[k];
                                    zan_ast_node_t *madd = zan_ast_new(g->arena,
                                        AST_MEMBER_ACCESS, item->loc);
                                    madd->member.object = recv;
                                    madd->member.name = (zan_istr_t){"Add", 3};
                                    madd->member.null_cond = 0;
                                    zan_ast_node_t *addcall = zan_ast_new(
                                        g->arena, AST_CALL, item->loc);
                                    addcall->call.callee = madd;
                                    zan_ast_list_init(&addcall->call.args);
                                    zan_ast_list_init(&addcall->call.type_args);
                                    zan_ast_list_push(&addcall->call.args, item,
                                                      g->arena);
                                    if (msym->type &&
                                        type_named(msym->type, "Dict", 4) &&
                                        k + 1 < arg->coll_init.items.count) {
                                        zan_ast_list_push(&addcall->call.args,
                                            arg->coll_init.items.items[++k],
                                            g->arena);
                                    }
                                    emit_expr(g, addcall, locals);
                                }
                                for (int lv = locals->count - 1; lv >= 0; lv--) {
                                    if (locals->vars[lv].name.len == cname.len &&
                                        memcmp(locals->vars[lv].name.str,
                                               cname.str, (size_t)cname.len) == 0) {
                                        for (int sh = lv; sh < locals->count - 1; sh++)
                                            locals->vars[sh] = locals->vars[sh + 1];
                                        locals->count--;
                                        break;
                                    }
                                }
                                if (getter_owned) {
                                    LLVMValueRef cv = LLVMBuildLoad2(g->builder,
                                        coll_lt, cslot, "cinit.done");
                                    emit_rc_release_for_type(g, msym->type, cv);
                                }
                                continue;
                            }
                            if (arg->kind == AST_ASSIGNMENT && arg->binary.left->kind == AST_IDENTIFIER) {
                                zan_symbol_t *fsym = get_field_sym(sym, arg->binary.left->ident.name);
                                zan_symbol_t *setter = property_setter_sym(g, fsym);
                                if (setter) {
                                    emit_property_setter_call(g, setter,
                                        new_inst ? new_inst : sym->type,
                                        alloca, emit_expr(g, arg->binary.right, locals),
                                        arg->binary.left, arg->binary.right, locals);
                                    continue;
                                }
                                int fi = get_field_index(sym, arg->binary.left->ident.name);
                                if (fi >= 0) {
                                    LLVMValueRef fptr = emit_field_ptr(g, sym, st, alloca, fi, "finit");
                                    zan_symbol_t *fsym = get_field_sym(sym, arg->binary.left->ident.name);
                                    int fval_owned = 0;
                                    LLVMValueRef fval = NULL;
                                    if (fsym && fsym->type && type_is_binding(fsym->type) &&
                                        !type_is_binding(infer_expr_type(g, arg->binary.right, locals))) {
                                        fval = emit_binding_value(g, fsym->type,
                                                                  arg->binary.right, locals);
                                        if (fval) fval_owned = 1;
                                    }
                                    if (!fval)
                                        fval =
                                        (fsym && fsym->type && fsym->type->kind == TYPE_DELEGATE &&
                                         arg->binary.right->kind == AST_LAMBDA)
                                            ? emit_lambda_typed(g, arg->binary.right, fsym->type, locals)
                                            : emit_expr(g, arg->binary.right, locals);
                                    if (fsym && fsym->type) {
                                        LLVMTypeRef target_t = map_type(g, fsym->type);
                                        LLVMTypeRef val_t = LLVMTypeOf(fval);
                                        if (LLVMGetTypeKind(target_t) == LLVMFloatTypeKind &&
                                            LLVMGetTypeKind(val_t) == LLVMDoubleTypeKind) {
                                            fval = LLVMBuildFPTrunc(g->builder, fval, target_t, "trunc");
                                        }
                                    }
                                    /* ARC 字段存储契约：旧值释放与新值保留 */
                                    if (fsym && fsym->type && fval) {
                                        zan_type_t *fsty = field_store_type(
                                            g, fsym, new_inst ? new_inst : sym->type);
                                        if (fsty && (is_rc_managed_type(fsty) || fsty->kind == TYPE_OBJECT) &&
                                            !(fsym->modifiers & MOD_WEAK)) {
                                            /* 绑定值发射结果处理 */
                                            emit_rc_store_field(g, fsty, fptr, fval,
                                                fval_owned
                                                    ? owned_rhs_marker(g, arg->binary.right->loc)
                                                    : arg->binary.right,
                                                locals, 0);
                                        } else if (fsty && fsty->kind == TYPE_STRUCT &&
                                                   type_contains_collection_rc(g, fsty, 0)) {
                                            emit_struct_field_capture(g, fsty, fptr, fval,
                                                fval_owned
                                                    ? owned_rhs_marker(g, arg->binary.right->loc)
                                                    : arg->binary.right, locals);
                                        } else {
                                            zan_store_fit(g, fval, fptr);
                                        }
                                    }
                                }
                            }
                        }
                    }
                    return alloca;
                }
            }
        }
        return new_erased_fallback(g, actual_t);
    return new_erased_fallback(g, actual_t);
}

