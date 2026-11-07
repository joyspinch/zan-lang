        /* 裸名函数调用：优先查找当前类实例/静态方法，未匹配则查找全局函数 */
        if (expr->call.callee && expr->call.callee->kind == AST_IDENTIFIER) {
            zan_istr_t fn_name = expr->call.callee->ident.name;

            if (g->current_type_sym) {
                zan_symbol_t *method_sym = resolve_overload_typed(
            g, g->current_type_sym, fn_name, expr, locals);
                    if (method_sym) fill_default_args(g, expr, method_sym);
                    if (method_sym) pack_params_args(g, expr, method_sym, locals);
                if (method_sym) {
                    int spec = try_method_spec(g, method_sym, expr, NULL, locals);
                    if (spec >= 0)
                        return emit_method_spec_call(g, spec, expr, NULL, locals);
                    for (int fi = irgen_find_function(g, method_sym); fi >= 0; fi = -1) {
                        if (g->functions[fi].sym == method_sym) {
                            int argc = expr->call.args.count;
                            bool is_static = (method_sym->modifiers & MOD_STATIC) != 0;
                            int extra = is_static ? 0 : 1;
                            int n_eh_args = 0;
                            LLVMValueRef *call_args = (LLVMValueRef *)calloc(
                                (size_t)(argc + extra > 0 ? argc + extra : 1), sizeof(LLVMValueRef));
                            if (!is_static && g->current_this) {
                                LLVMTypeRef this_ty = LLVMGetAllocatedType(g->current_this);
                                LLVMValueRef this_val = LLVMBuildLoad2(g->builder,
                                    this_ty, g->current_this, "this");
                                unsigned np = LLVMCountParamTypes(g->functions[fi].fn_type);
                                if (np > 0) {
                                    LLVMTypeRef *pts = (LLVMTypeRef *)calloc(np, sizeof(LLVMTypeRef));
                                    LLVMGetParamTypes(g->functions[fi].fn_type, pts);
                                    if (LLVMTypeOf(this_val) != pts[0]) {
                                        this_val = LLVMBuildBitCast(g->builder, this_val, pts[0], "thisc");
                                    }
                                    free(pts);
                                }
                                call_args[0] = this_val;
                            }
                            for (int k = 0; k < argc; k++) {
                                call_args[k + extra] = emit_arg_typed(g,
                                    expr->call.args.items[k],
                                    method_param_type_at(g, method_sym, k, expr,
                                                         NULL, locals),
                                    locals);
                                n_eh_args += emit_call_arg_eh_push(g,
                                    expr->call.args.items[k],
                                    call_args[k + extra], locals);
                            }
                            LLVMTypeRef mft = g->functions[fi].fn_type;
                            LLVMValueRef mfn = route_generic_method(g, g->cur_inst,
                                method_sym, g->functions[fi].fn, mft, &mft);
                            if (g->runtime_checks && g->current_fn &&
                                sym_declares_extern(method_sym)) {
                                for (int k = 0; k < argc; k++) {
                                    if (LLVMGetTypeKind(LLVMTypeOf(call_args[k + extra]))
                                            != LLVMPointerTypeKind) continue;
                                    if (!is_string_expr(g,
                                            expr->call.args.items[k], locals))
                                        continue;
                                    LLVMValueRef isnull = zan_icmp(g->builder,
                                        LLVMIntEQ, call_args[k + extra],
                                        LLVMConstNull(LLVMTypeOf(call_args[k + extra])),
                                        "extarg.null");
                                    emit_runtime_check(g, isnull, expr->loc,
                                        "null string passed to an extern function");
                                    call_args[k + extra] = LLVMBuildSelect(g->builder,
                                        isnull,
                                        zan_irgen_intern_string(g, ""),
                                        call_args[k + extra], "extarg.safe");
                                }
                            }
                            const char *cn = (LLVMGetTypeKind(LLVMGetReturnType(mft)) == LLVMVoidTypeKind) ? "" : "bcall";
                            LLVMValueRef result = emit_dispatch_call(g,
                                is_static ? NULL : g->current_type_sym, method_sym,
                                mfn, mft, call_args, argc + extra, cn);
                            int consumes_free_arg =
                                argc == 1 && call_consumes_free_arg(g->functions[fi].fn);
                            if (consumes_free_arg) {
                                emit_invalidate_freed_string(g,
                                    expr->call.args.items[0], locals);
                            }
                            emit_extern_call_len_invalidate(g,
                                g->functions[fi].fn, call_args, argc + extra,
                                expr, locals);
                            for (int k = 0; k < n_eh_args; k++)
                                emit_eh_tmp_pop(g);
                            for (int k = 0; k < argc; k++) {
                                if (!consumes_free_arg || k != 0) {
                                    emit_release_owned_call_temp(g, expr->call.args.items[k],
                                        call_args[k + extra], locals);
                                }
                            }
                            free(call_args);
                            return result;
                        }
                    }
                }
            }

            char name_buf[256];
            int nlen = fn_name.len < 255 ? fn_name.len : 255;
            memcpy(name_buf, fn_name.str, (size_t)nlen);
            name_buf[nlen] = '\0';
            LLVMValueRef global_fn = LLVMGetNamedFunction(g->mod, name_buf);
            if (global_fn) {
                int argc = expr->call.args.count;
                int n_eh_args = 0;
                LLVMValueRef *call_args = (LLVMValueRef *)calloc(
                    (size_t)(argc > 0 ? argc : 1), sizeof(LLVMValueRef));
                LLVMTypeRef fn_type = LLVMGlobalGetValueType(global_fn);
                unsigned nparams = LLVMCountParamTypes(fn_type);
                LLVMTypeRef *ptypes = nparams > 0
                    ? (LLVMTypeRef *)calloc(nparams, sizeof(LLVMTypeRef))
                    : NULL;
                if (!ptypes) nparams = 0;
                else LLVMGetParamTypes(fn_type, ptypes);
                for (int k = 0; k < argc; k++) {
                    call_args[k] = emit_expr(g, expr->call.args.items[k], locals);
                    n_eh_args += emit_call_arg_eh_push(g, expr->call.args.items[k],
                                          call_args[k], locals);
                    if ((unsigned)k >= nparams) { continue; }
                    LLVMTypeRef at = LLVMTypeOf(call_args[k]);
                    LLVMTypeRef pt = ptypes[k];
                    if (at == pt) { continue; }
                    if (LLVMGetTypeKind(at) == LLVMIntegerTypeKind
                        && LLVMGetTypeKind(pt) == LLVMIntegerTypeKind) {
                        unsigned aw = LLVMGetIntTypeWidth(at);
                        unsigned pw = LLVMGetIntTypeWidth(pt);
                        if (aw < pw) {
                            call_args[k] = (aw == 1)
                                ? LLVMBuildZExt(g->builder, call_args[k], pt,
                                                "arg.zext")
                                : LLVMBuildSExt(g->builder, call_args[k], pt,
                                                "arg.sext");
                        } else if (aw > pw) {
                            call_args[k] = LLVMBuildTrunc(g->builder,
                                call_args[k], pt, "arg.trunc");
                        }
                    } else if (LLVMGetTypeKind(at) == LLVMPointerTypeKind
                        && LLVMGetTypeKind(pt) == LLVMPointerTypeKind) {
                        call_args[k] = LLVMBuildBitCast(g->builder,
                            call_args[k], pt, "arg.ptrc");
                    }
                }
                if (LLVMIsFunctionVarArg(fn_type)) {
                    for (int k = (int)nparams; k < argc; k++) {
                        LLVMTypeRef at = LLVMTypeOf(call_args[k]);
                        if (LLVMGetTypeKind(at) == LLVMIntegerTypeKind &&
                            LLVMGetIntTypeWidth(at) < 32)
                            call_args[k] =
                                (LLVMGetIntTypeWidth(at) == 1)
                                ? LLVMBuildZExt(g->builder, call_args[k],
                                    LLVMInt32TypeInContext(g->ctx), "va.prom")
                                : LLVMBuildSExt(g->builder, call_args[k],
                                    LLVMInt32TypeInContext(g->ctx), "va.prom");
                        else if (LLVMGetTypeKind(at) == LLVMFloatTypeKind)
                            call_args[k] = LLVMBuildFPExt(g->builder,
                                call_args[k],
                                LLVMDoubleTypeInContext(g->ctx), "va.prom");
                    }
                }
                if (g->runtime_checks && g->current_fn) {
                    for (int k = 0; k < argc; k++) {
                        if (LLVMGetTypeKind(LLVMTypeOf(call_args[k]))
                                != LLVMPointerTypeKind) continue;
                        if (!is_string_expr(g, expr->call.args.items[k], locals))
                            continue;
                        LLVMValueRef isnull = zan_icmp(g->builder, LLVMIntEQ,
                            call_args[k], LLVMConstNull(LLVMTypeOf(call_args[k])),
                            "extarg.null");
                        emit_runtime_check(g, isnull, expr->loc,
                            "null string passed to an extern function");
                        call_args[k] = LLVMBuildSelect(g->builder, isnull,
                            zan_irgen_intern_string(g, ""),
                            call_args[k], "extarg.safe");
                    }
                }
                const char *gcn = (LLVMGetTypeKind(LLVMGetReturnType(fn_type)) == LLVMVoidTypeKind) ? "" : "gcall";
                LLVMValueRef result = zan_call_or_invoke(g, g->builder, fn_type,
                    global_fn, call_args, (unsigned)argc, gcn);
                int consumes_free_arg =
                    argc == 1 && call_consumes_free_arg(global_fn);
                if (consumes_free_arg) {
                    emit_invalidate_freed_string(g,
                        expr->call.args.items[0], locals);
                }
                emit_extern_call_len_invalidate(g, global_fn, call_args, argc,
                                               expr, locals);
                for (int k = 0; k < n_eh_args; k++)
                    emit_eh_tmp_pop(g);
                for (int k = 0; k < argc; k++) {
                    if (!consumes_free_arg || k != 0) {
                        emit_release_owned_call_temp(g, expr->call.args.items[k],
                            call_args[k], locals);
                    }
                }
                free(ptypes);
                free(call_args);
                return result;
            }

            if (g->current_type_sym &&
                (g->current_type_sym->kind == SYM_CLASS ||
                 g->current_type_sym->kind == SYM_STRUCT)) {
                zan_symbol_t *cur = g->current_type_sym;
                int member_found = 0;
                while (cur && !member_found) {
                    for (int mi = 0; mi < cur->member_count; mi++) {
                        zan_symbol_t *m = cur->members[mi];
                        if (m && m->name.len == fn_name.len &&
                            memcmp(m->name.str, fn_name.str,
                                   (size_t)fn_name.len) == 0) {
                            member_found = 1;
                            break;
                        }
                    }
                    zan_symbol_t *base = (cur->type && cur->type->base_type)
                        ? cur->type->base_type->sym : NULL;
                    cur = (base && base != cur) ? base : NULL;
                }
                if (!member_found && !local_find(locals, expr->call.callee->ident.name)) {
                    zan_diag_emit_code(g->diag, DIAG_ERROR, expr->loc, 3009,
                        "'%.*s' has no member '%.*s'",
                        (int)g->current_type_sym->name.len,
                        g->current_type_sym->name.str,
                        (int)fn_name.len, fn_name.str);
                }
            }
        }

        /* Robustness: a call `obj */
        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS) {
            zan_ast_node_t *callee = expr->call.callee;
            zan_symbol_t *recv_cls = expr_class_sym(g, callee->member.object, locals);
            if (!recv_cls && callee->member.object->kind == AST_IDENTIFIER &&
                !local_find(locals, callee->member.object->ident.name)) {
                zan_symbol_t *ts = zan_binder_lookup(g->binder, callee->member.object->ident.name);
                if (ts && (ts->kind == SYM_CLASS || ts->kind == SYM_STRUCT)) recv_cls = ts;
            }
            if (recv_cls && (recv_cls->kind == SYM_CLASS || recv_cls->kind == SYM_STRUCT)) {
                zan_istr_t mn = callee->member.name;
                int found = 0;
                zan_symbol_t *cur = recv_cls;
                while (cur && !found) {
                    for (int mi = 0; mi < cur->member_count; mi++) {
                        zan_symbol_t *m = cur->members[mi];
                        if (m && m->name.len == mn.len &&
                            memcmp(m->name.str, mn.str, mn.len) == 0) { found = 1; break; }
                    }
                    zan_symbol_t *base = (cur->type && cur->type->base_type)
                        ? cur->type->base_type->sym : NULL;
                    cur = (base && base != cur) ? base : NULL;
                }
                if (!found) {
                    zan_istr_t ns = recv_cls->decl
                        ? zan_ast_ns_name(recv_cls->decl) : (zan_istr_t){0};
                    if (ns.len)
                        zan_diag_emit_code(g->diag, DIAG_ERROR, expr->loc, 3063,
                            "type '%.*s.%.*s' has no member '%.*s'",
                            (int)ns.len, ns.str,
                            (int)recv_cls->name.len, recv_cls->name.str,
                            (int)mn.len, mn.str);
                    else
                        zan_diag_emit_code(g->diag, DIAG_ERROR, expr->loc, 3009,
                            "'%.*s' has no member '%.*s'",
                            (int)recv_cls->name.len, recv_cls->name.str,
                            (int)mn.len, mn.str);
                }
            }
        }

        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS &&
            is_name_path(expr->call.callee->member.object)) {
            zan_ast_node_t *obj = expr->call.callee->member.object;
            zan_ast_node_t *head = name_path_head(obj);
            if (head && !local_find(locals, head->ident.name)) {
                zan_istr_t on = (obj->kind == AST_IDENTIFIER)
                    ? obj->ident.name : obj->member.name;
                zan_istr_t mn = expr->call.callee->member.name;
                zan_symbol_t *osym = zan_binder_lookup(g->binder, on);
                /* 限定名路径与类型命名空间对不上（例如把 System.Csv.Csv 写成
                 * System.Text.Csv）：当未解析处理，别按最后一段猜一个类。 */
                if (osym && obj->kind == AST_MEMBER_ACCESS &&
                    !qpath_ns_ok(g, obj, osym))
                    osym = NULL;
                char path[256];
                int plen = format_name_path(expr->call.callee, path,
                                            (int)sizeof(path));
                bool enum_const_recv = false;
                if (!osym && obj->kind == AST_MEMBER_ACCESS &&
                    obj->member.object->kind == AST_IDENTIFIER) {
                    zan_symbol_t *es = zan_binder_lookup(g->binder,
                        obj->member.object->ident.name);
                    if (es && es->kind == SYM_ENUM) {
                        for (int ei = 0; ei < es->member_count; ei++) {
                            zan_symbol_t *em = es->members[ei];
                            if (em && em->kind == SYM_ENUM_MEMBER &&
                                em->name.len == obj->member.name.len &&
                                memcmp(em->name.str, obj->member.name.str,
                                       (size_t)obj->member.name.len) == 0) {
                                enum_const_recv =
                                    (mn.len == 8 &&
                                     memcmp(mn.str, "ToString", 8) == 0) ||
                                    (mn.len == 8 &&
                                     memcmp(mn.str, "TryParse", 8) == 0);
                                break;
                            }
                        }
                    }
                }
                if (!osym && !enum_const_recv) {
                    zan_diag_emit_code(g->diag, DIAG_ERROR, expr->loc, 3064,
                        "unresolved call '%.*s': '%.*s' is not a known variable, "
                        "type, or namespace (is the class imported and registered "
                        "in the stdlib map?)",
                        plen, path, (int)on.len, on.str);
                }
                else if (osym && (osym->kind == SYM_CLASS || osym->kind == SYM_STRUCT) &&
                         !get_method_sym(osym, mn)) {
                    zan_diag_emit_code(g->diag, DIAG_ERROR, expr->loc, 3065,
                        "unresolved call '%.*s': type '%.*s' has no method "
                        "'%.*s'",
                        plen, path, (int)on.len, on.str, (int)mn.len, mn.str);
                }
            }
        }

        /* Robustness: `recv */
        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS) {
            zan_ast_node_t *bcallee = expr->call.callee;
            zan_istr_t bmn = bcallee->member.name;
            zan_type_t *brt = infer_expr_type(g, bcallee->member.object, locals);
            const char *brecv = NULL;
            if (brt && brt->kind == TYPE_STRING) brecv = "string";
            else if (brt && type_named(brt, "List", 4)) brecv = "List";
            else if (brt && type_named(brt, "Dict", 4)) brecv = "Dict";
            else if (brt && type_named(brt, "StringBuilder", 13))
                brecv = "StringBuilder";
            if (brecv && !zan_builtin_has_member(brecv, bmn.str, (int)bmn.len)) {
                const zan_builtin_type_t *bt = zan_builtin_find(brecv);
                zan_diag_emit_code(g->diag, DIAG_ERROR, expr->loc, 3066,
                    "'%s' has no member '%.*s'",
                    bt ? bt->display : brecv, (int)bmn.len, bmn.str);
            }
            else if (brecv &&
                     zan_builtin_member_kind(brecv, bmn.str, (int)bmn.len) == 'P') {
                const zan_builtin_type_t *bt = zan_builtin_find(brecv);
                zan_diag_emit_code(g->diag, DIAG_ERROR, expr->loc, 3067,
                    "'%s.%.*s' is a property, not a method: drop the '()'",
                    bt ? bt->display : brecv, (int)bmn.len, bmn.str);
            }
        }

        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS)
            diagnose_unresolved_static_chain(g, expr->call.callee->member.object);

        if (!zan_diag_has_errors(g->diag)) {
            if (!call_receiver_is_open_generic(g, expr, locals))
                zan_diag_emit_code(g->diag, DIAG_ERROR, expr->loc, 3068,
                              "this call could not be resolved to any callable "
                              "(no method, delegate, operator or builtin matches)");
            else if (g->runtime_checks && g->current_fn) {
                /* A494：这个调用点只在"直接实例化开放泛型模板"时才会执行
                 * （真身由派生类特化补出，模板体没有运行期实现）。原行为
                 * 是静默返回默认值——写库 0 行、查表 null，比编译失败更
                 * 危险。这里响亮报告：软诊断口径（每位点一次，
                 * --strict-runtime exit 70），返回值仍走同型默认值，
                 * 特化出的真身不经过这里。 */
                zan_istr_t cname = { NULL, 0 };
                if (expr->call.callee) {
                    if (expr->call.callee->kind == AST_MEMBER_ACCESS)
                        cname = expr->call.callee->member.name;
                    else if (expr->call.callee->kind == AST_IDENTIFIER)
                        cname = expr->call.callee->ident.name;
                }
                char msg[280];
                if (cname.str && cname.len > 0)
                    snprintf(msg, sizeof(msg),
                             "call '%.*s' has no runtime body: resolvable "
                             "only after generic specialization; instantiate "
                             "the template via a derived class "
                             "(default value returned)",
                             (int)cname.len, cname.str);
                else
                    snprintf(msg, sizeof(msg),
                             "call has no runtime body: resolvable only "
                             "after generic specialization; instantiate the "
                             "template via a derived class (default value "
                             "returned)");
                if (!g->rt_guard_split) {
                    char buf[420];
                    const char *file0 = loc_site_file(g, expr->loc);
                    if (g->hide_diagnostics)
                        snprintf(buf, sizeof(buf), "%s: runtime error: %s\n",
                                 file0, msg);
                    else
                        snprintf(buf, sizeof(buf),
                                 "%s:%u:%u: runtime error: %s\n",
                                 file0, expr->loc.line, expr->loc.col, msg);
                    emit_guard_report_merged(g,
                        zan_irgen_intern_string(g, buf));
                } else {
                    LLVMValueRef file_gv = zan_irgen_intern_string(g,
                        loc_site_file(g, expr->loc));
                    char mbuf[300];
                    snprintf(mbuf, sizeof(mbuf), "%s\n", msg);
                    LLVMValueRef msg_gv = zan_irgen_intern_string(g, mbuf);
                    emit_guard_report3(g, file_gv,
                        g->hide_diagnostics ? 0 : (uint32_t)expr->loc.line,
                        g->hide_diagnostics ? 0 : (uint32_t)expr->loc.col,
                        msg_gv);
                }
            }
        }
        return emit_unresolved_call_result(g, expr, locals);
    return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
