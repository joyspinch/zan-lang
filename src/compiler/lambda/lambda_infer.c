bool type_mentions_tp(zan_type_t *t) {
    if (!t) return false;
    if (t->kind == TYPE_TYPE_PARAM) return true;
    if (t->kind == TYPE_ARRAY || t->kind == TYPE_NULLABLE)
        return type_mentions_tp(t->element_type);
    if (t->kind == TYPE_DELEGATE) {
        if (type_mentions_tp(t->delegate_ret_type)) return true;
        for (int i = 0; i < t->delegate_param_count; i++)
            if (type_mentions_tp(t->delegate_param_types[i])) return true;
    }
    for (int i = 0; i < t->type_arg_count; i++)
        if (type_mentions_tp(t->type_args[i])) return true;
    return false;
}

static void unify_method_tp(zan_type_t *dp, zan_type_t *at,
                            zan_ast_list_t *tps, zan_type_t **bind) {
    if (!dp || !at) return;
    if (dp->kind == TYPE_TYPE_PARAM) {
        for (int i = 0; i < tps->count; i++) {
            zan_istr_t tn = tps->items[i]->ident.name;
            if (tn.len == dp->name.len &&
                memcmp(tn.str, dp->name.str, (size_t)dp->name.len) == 0) {
                if (!bind[i]) bind[i] = at;
                return;
            }
        }
        return;
    }
    if ((dp->kind == TYPE_ARRAY || dp->kind == TYPE_NULLABLE) &&
        dp->kind == at->kind) {
        unify_method_tp(dp->element_type, at->element_type, tps, bind);
        return;
    }
    for (int i = 0; i < dp->type_arg_count && i < at->type_arg_count; i++)
        unify_method_tp(dp->type_args[i], at->type_args[i], tps, bind);
}
zan_type_t *subst_method_tp(zan_irgen_t *g, zan_type_t *t,
                                   zan_ast_list_t *tps, zan_type_t **bind) {
    if (!t || !type_mentions_tp(t)) return t;
    if (t->kind == TYPE_TYPE_PARAM) {
        for (int i = 0; i < tps->count; i++) {
            zan_istr_t tn = tps->items[i]->ident.name;
            if (bind[i] && tn.len == t->name.len &&
                memcmp(tn.str, t->name.str, (size_t)t->name.len) == 0)
                return bind[i];
        }
        return t;
    }
    zan_type_t *nt = (zan_type_t *)zan_arena_alloc(g->arena, sizeof(zan_type_t));
    *nt = *t;
    if (t->kind == TYPE_ARRAY || t->kind == TYPE_NULLABLE) {
        nt->element_type = subst_method_tp(g, t->element_type, tps, bind);
        return nt;
    }
    if (t->kind == TYPE_DELEGATE) {
        nt->delegate_ret_type = subst_method_tp(g, t->delegate_ret_type, tps, bind);
        if (t->delegate_param_count > 0) {
            nt->delegate_param_types = (zan_type_t **)zan_arena_alloc(
                g->arena, sizeof(zan_type_t *) * (size_t)t->delegate_param_count);
            for (int i = 0; i < t->delegate_param_count; i++)
                nt->delegate_param_types[i] =
                    subst_method_tp(g, t->delegate_param_types[i], tps, bind);
        }
    }
    if (t->type_arg_count > 0) {
        nt->type_args = (zan_type_t **)zan_arena_alloc(
            g->arena, sizeof(zan_type_t *) * (size_t)t->type_arg_count);
        for (int i = 0; i < t->type_arg_count; i++)
            nt->type_args[i] = subst_method_tp(g, t->type_args[i], tps, bind);
    }
    return nt;
}

/* 泛型方法调用点推导类型实参绑定 */
void infer_method_tp_bindings(zan_irgen_t *g, zan_symbol_t *msym,
                                     zan_ast_node_t *call,
                                     zan_ast_node_t *recv_expr,
                                     local_scope_t *locals,
                                     zan_type_t **bind) {
    zan_ast_list_t *tps = &msym->decl->method_decl.type_params;
    for (int i = 0; i < tps->count && i < call->call.type_args.count; i++)
        bind[i] = resolve_type_ctx(g, call->call.type_args.items[i]);

    /* collect_inst_expr 的实例收集路径没有词法作用域（locals=NULL）：
     * 显式类型实参的绑定已在上面完成，其余两遍推导都要把实参表达式
     * 交给 infer_expr_type/local_find——local_find 对 NULL scope 直接
     * 解引用，`ParseScoped<T>(json, lambda)` 编译期段错误
     * （json_scoped_scratch/reflect_* 一线）。没有 locals 就到此为止。 */
    if (!locals) return;
    zan_ast_list_t *params = &msym->decl->method_decl.params;
    /* 形参与实参的映射口径必须和发射端一致（try_method_spec / emit_method_spec_call）：
     * 只有「静态方法经实例表达式调用」时接收者才占第 0 个实参；实例方法的接收者是
     * this，不是声明形参之一。早期这里写成 recv_expr ? 1 : 0，导致实例泛型方法
     * 拿接收者去合一形参类型，真正的实参没参与绑定 → bind 为空 → 退化成擦除版
     * → 形参里的 `is T` 被折成常量 false。 */
    bool m_static = (msym->decl->method_decl.modifiers & MOD_STATIC) != 0;
    int arg_base = (recv_expr && m_static) ? 1 : 0;
    for (int j = 0; j < params->count; j++) {
        zan_ast_node_t *aexpr = NULL;
        if (arg_base == 1 && j == 0) aexpr = recv_expr;
        else if (j - arg_base < call->call.args.count)
            aexpr = call->call.args.items[j - arg_base];
        if (!aexpr || aexpr->kind == AST_LAMBDA) continue;
        zan_type_t *dp = method_param_type(g, msym, j);
        if (!dp || !type_mentions_tp(dp)) continue;
        zan_type_t *at = infer_expr_type(g, aexpr, locals);
        zan_compile_trace("  tp param %d: dp_kind=%d dp_args=%d at_kind=%d at_args=%d",
                          j, (int)dp->kind, dp->type_arg_count,
                          at ? (int)at->kind : -1, at ? at->type_arg_count : -1);
        /* 具体化变体里，泛型类的类型实参已定：把实参类型里的类类型参数
         * 换成实参，否则「类类型参数 → 方法类型参数」的绑定推不出来，
         * 泛型方法会退化成擦除版（返回值是默认值，判定全错）。 */
        if (at && g->cur_inst) at = subst_type_param_deep(g, at, g->cur_inst);
        if (at && !type_mentions_tp(at)) unify_method_tp(dp, at, tps, bind);
    }

    /* 第二遍推导：根据委托形参签名推导 Lambda 参数与返回类型 */
    for (int j = 0; j < params->count; j++) {
        int ai = j - arg_base;
        if (ai < 0 || ai >= call->call.args.count) continue;
        zan_ast_node_t *a = call->call.args.items[ai];
        if (!a || a->kind != AST_LAMBDA) continue;
        zan_type_t *dp = method_param_type(g, msym, j);
        if (!dp || dp->kind != TYPE_DELEGATE || !type_mentions_tp(dp)) continue;
        if (a->lambda.params.count != dp->delegate_param_count) continue;
        zan_type_t *sdp = subst_method_tp(g, dp, tps, bind);
        int mark = locals->count;
        unsigned mark_gen = g_local_gen;
        bool params_known = true;
        for (int k = 0; k < a->lambda.params.count; k++) {
            zan_ast_node_t *lp = a->lambda.params.items[k];
            zan_type_t *lpt = lp->param.type
                ? zan_binder_resolve_type(g->binder, lp->param.type)
                : sdp->delegate_param_types[k];
            /* 外层泛型类的类型实参在这里已经定下来了：委托的形参类型
             * 可能正是类的类型参数（Func<T,V>），不替换的话它永远算
             * 「未定」，lambda 的返回类型就推不出来，方法类型参数绑不上。 */
            if (lpt && g->cur_inst)
                lpt = subst_type_param_deep(g, lpt, g->cur_inst);
            if (lpt && !type_mentions_tp(lpt)) {
                if (lp->param.type)
                    unify_method_tp(dp->delegate_param_types[k], lpt, tps, bind);
                local_add(locals, lp->param.name, NULL, lpt);
            } else {
                params_known = false;
            }
        }
        if (params_known &&
            type_mentions_tp(dp->delegate_ret_type) &&
            a->lambda.body && a->lambda.body->kind != AST_BLOCK) {
            zan_type_t *bt = infer_expr_type(g, a->lambda.body, locals);
            if (!bt || type_mentions_tp(bt)) {
                switch (expr_family(g, a->lambda.body, locals)) {
                case FAM_BOOL: bt = g->binder->type_bool; break;
                case FAM_INT: bt = g->binder->type_int; break;
                case FAM_FLOAT: bt = g->binder->type_double; break;
                case FAM_STRING: bt = g->binder->type_string; break;
                default: bt = NULL; break;
                }
            }
            if (bt && !type_mentions_tp(bt))
                unify_method_tp(dp->delegate_ret_type, bt, tps, bind);
        }
        locals->count = mark;
        g_local_gen = mark_gen;
    }
}

/* 泛型形参类型实参代换 */
zan_type_t *method_param_type_at(zan_irgen_t *g, zan_symbol_t *msym,
                                        int idx, zan_ast_node_t *call,
                                        zan_ast_node_t *recv_expr,
                                        local_scope_t *locals) {
    zan_type_t *pt = method_param_type(g, msym, idx);
    if (!pt) return pt;
    /* 外层泛型类类型形参代换 */
    if (pt->kind == TYPE_DELEGATE) {
        zan_type_t *recv = g->cur_recv_type ? g->cur_recv_type
                         : (recv_expr ? infer_expr_type(g, recv_expr, locals)
                                      : g->cur_inst);
        if (recv && recv->type_arg_count > 0)
            pt = subst_delegate_sig(g, pt, recv);
    }
    if (!msym->decl || msym->decl->kind != AST_METHOD_DECL) return pt;
    zan_ast_list_t *tps = &msym->decl->method_decl.type_params;
    if (tps->count == 0 || tps->count > 8) return pt;
    if (!type_mentions_tp(pt)) return pt;
    if (!call || call->kind != AST_CALL) return pt;

    zan_type_t *bind[8] = { NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL };
    infer_method_tp_bindings(g, msym, call, recv_expr, locals, bind);
    return subst_method_tp(g, pt, tps, bind);
}

/* 判断方法返回类型是否为该方法自身声明的泛型类型形参 */
bool method_ret_is_bare_tp(zan_symbol_t *msym) {
    if (!msym || !msym->decl || msym->decl->kind != AST_METHOD_DECL) return false;
    zan_ast_list_t *tps = &msym->decl->method_decl.type_params;
    if (tps->count == 0) return false;
    zan_ast_node_t *ret_ref = msym->decl->method_decl.return_type;
    if (!ret_ref || ret_ref->kind != AST_TYPE_REF) return false;
    if (ret_ref->type_ref.type_args.count > 0 || ret_ref->type_ref.is_array)
        return false;
    zan_istr_t rn = ret_ref->type_ref.name;
    for (int i = 0; i < tps->count; i++) {
        zan_istr_t tn = tps->items[i]->ident.name;
        if (tn.len == rn.len && memcmp(tn.str, rn.str, (size_t)rn.len) == 0)
            return true;
    }
    return false;
}
zan_type_t *method_ret_type_at(zan_irgen_t *g, zan_symbol_t *msym,
                                      zan_ast_node_t *call,
                                      zan_ast_node_t *recv_expr,
                                      local_scope_t *locals) {
    zan_type_t *rt = msym ? msym->type : NULL;
    if (!rt || !msym->decl || msym->decl->kind != AST_METHOD_DECL) return rt;
    zan_ast_list_t *tps = &msym->decl->method_decl.type_params;
    if (tps->count == 0 || tps->count > 8) return rt;
    if (!type_mentions_tp(rt)) return rt;
    if (!call || call->kind != AST_CALL) return rt;
    zan_type_t *bind[8] = { NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL };
    infer_method_tp_bindings(g, msym, call, recv_expr, locals, bind);
    return subst_method_tp(g, rt, tps, bind);
}

/* async 委托调用：调用生成协程帧并在原地 await 挂起等待结果 */
void check_delegate_async_match(zan_irgen_t *g, zan_ast_node_t *e,
                                       zan_type_t *dt, local_scope_t *locals) {
    if (!e || !dt || dt->kind != TYPE_DELEGATE) return;
    zan_symbol_t *m = NULL;
    if (e->kind == AST_IDENTIFIER) {
        if (local_find(locals, e->ident.name)) return;
        if (g->current_type_sym)
            m = get_method_sym(g->current_type_sym, e->ident.name);
    } else if (e->kind == AST_MEMBER_ACCESS && e->member.object &&
               e->member.object->kind == AST_IDENTIFIER &&
               !local_find(locals, e->member.object->ident.name)) {
        zan_symbol_t *cs =
            zan_binder_lookup(g->binder, e->member.object->ident.name);
        if (cs && (cs->kind == SYM_CLASS || cs->kind == SYM_STRUCT))
            m = get_method_sym(cs, e->member.name);
    }
    if (!m || !m->decl || m->decl->kind != AST_METHOD_DECL) return;
    int m_async = (m->decl->method_decl.modifiers & MOD_ASYNC) != 0;
    if (m_async == (dt->delegate_is_async != 0)) return;
    zan_diag_emit_code(g->diag, DIAG_ERROR, e->loc, 3047,
        "cannot convert %s method '%.*s' to %s delegate '%.*s'",
        m_async ? "async" : "non-async", m->name.len, m->name.str,
        dt->delegate_is_async ? "async" : "non-async",
        dt->name.len, dt->name.str);
}

static zan_ast_node_t *ast_type_from_zan_type(zan_irgen_t *g,
                                                zan_type_t *type,
                                                zan_loc_t loc) {
    if (!g || !type) return NULL;
    zan_ast_node_t *ref = zan_ast_new(g->arena, AST_TYPE_REF, loc);
    ref->type_ref.name = type->name;
    zan_ast_list_init(&ref->type_ref.type_args);
    ref->type_ref.is_nullable = false;
    ref->type_ref.is_array = false;
    for (int i = 0; i < type->type_arg_count; i++) {
        zan_ast_node_t *arg = ast_type_from_zan_type(g, type->type_args[i], loc);
        if (!arg) return NULL;
        zan_ast_list_push(&ref->type_ref.type_args, arg, g->arena);
    }
    return ref;
}

/* 原地改写构造实参以复用类构造管线 */
static bool wrap_implicit_ctor_arg(zan_irgen_t *g, zan_ast_node_t *arg,
                                   zan_type_t *ptype) {
    if (!g || !arg || !ptype) return false;
    zan_ast_node_t *type_ref = ast_type_from_zan_type(g, ptype, arg->loc);
    if (!type_ref) return false;
    zan_ast_node_t *original = (zan_ast_node_t *)zan_arena_alloc(
        g->arena, sizeof(zan_ast_node_t));
    if (!original) return false;
    *original = *arg;
    zan_ast_node_t replacement;
    memset(&replacement, 0, sizeof(replacement));
    replacement.kind = AST_NEW_EXPR;
    replacement.loc = arg->loc;
    replacement.new_expr.type = type_ref;
    replacement.new_expr.is_array = false;
    replacement.new_expr.array_init = false;
    zan_ast_list_init(&replacement.new_expr.args);
    zan_ast_list_push(&replacement.new_expr.args, original, g->arena);
    *arg = replacement;
    infer_cache_invalidate();
    return true;
}
LLVMValueRef emit_arg_typed(zan_irgen_t *g, zan_ast_node_t *arg,
                                   zan_type_t *ptype, local_scope_t *locals) {
    zan_type_t *atype = infer_expr_type(g, arg, locals);
    check_implicit_narrowing(g, ptype, atype, arg, "argument");
    check_value_type_mismatch(g, ptype, atype, arg, "argument");
    check_generic_invariance(g, ptype, atype, arg, "argument");
    if (ptype && atype && ptype->kind == TYPE_CLASS &&
        is_implicit_ctor_source(atype)) {
        if (implicit_ctor_for_arg(g, ptype, atype, arg, locals)) {
            wrap_implicit_ctor_arg(g, arg, ptype);
            return emit_expr(g, arg, locals);
        }
        zan_diag_emit_code(g->diag, DIAG_ERROR, arg->loc, 3048,
            "cannot convert '%s' to '%s' in argument: no matching constructor",
            atype->name.str ? atype->name.str : "?",
            ptype->name.str ? ptype->name.str : "?");
        return LLVMConstNull(map_type(g, ptype));
    }
    if (arg && ptype && ptype->kind == TYPE_DELEGATE) {
        if (arg->kind == AST_LAMBDA)
            return emit_lambda_typed(g, arg, ptype, locals);
        check_delegate_async_match(g, arg, ptype, locals);
    }
    LLVMValueRef v = emit_expr(g, arg, locals);
    if (v && ptype && atype)
        v = emit_user_conversion(g, atype, ptype, "op_implicit", v, arg, locals);
    /* A556-1 隐式装箱：值形状实参流向 object/接口形参 */
    v = emit_implicit_box(g, ptype, v, arg, locals);
    if (v && ptype && (ptype->kind == TYPE_DOUBLE || ptype->kind == TYPE_FLOAT) &&
        LLVMGetTypeKind(LLVMTypeOf(v)) == LLVMIntegerTypeKind &&
        LLVMGetIntTypeWidth(LLVMTypeOf(v)) > 1)
        v = coerce_int_to(g, v, map_type(g, ptype));
    if (v && ptype && ptype->kind == TYPE_NULLABLE)
        v = coerce_int_to(g, v, map_type(g, ptype));
    return v;
}

/* ref / out 实参处理：传递局部变量或存储槽的内存地址 */
LLVMValueRef emit_ref_lvalue_ptr(zan_irgen_t *g, zan_ast_node_t *tgt,
                                        local_scope_t *locals) {
    if (!tgt) return NULL;
    if (tgt->kind == AST_THIS_EXPR) {
        if (g->current_this && g->current_type_sym) {
            LLVMTypeRef st = get_struct_llvm_type(g, g->current_type_sym);
            if (st) {
                return LLVMBuildLoad2(g->builder, LLVMPointerType(st, 0),
                                      g->current_this, "ref.this");
            }
        }
        return NULL;
    }
    if (tgt->kind == AST_IDENTIFIER) {
        local_var_t *local = local_find(locals, tgt->ident.name);
        if (local) return local->alloca;
        if (g->current_type_sym) {
            zan_symbol_t *field = get_field_sym(g->current_type_sym, tgt->ident.name);
            LLVMValueRef global = field ? get_static_field_global(g,
                g->current_type_sym, field, NULL) : NULL;
            if (global) return global;
            int fi = get_field_index(g->current_type_sym, tgt->ident.name);
            LLVMTypeRef st = get_struct_llvm_type(g, g->current_type_sym);
            if (fi >= 0 && st && g->current_this) {
                LLVMValueRef self = LLVMBuildLoad2(g->builder,
                    LLVMPointerType(st, 0), g->current_this, "ref.this");
                return emit_field_ptr(g, g->current_type_sym, st, self, fi, "ref.fld");
            }
        }
        return NULL;
    }

    if (tgt->kind == AST_MEMBER_ACCESS && !tgt->member.null_cond) {
        zan_ast_node_t *obj_expr = tgt->member.object;
        if (obj_expr->kind == AST_IDENTIFIER &&
            !local_find(locals, obj_expr->ident.name)) {
            zan_symbol_t *cs = zan_binder_lookup(g->binder, obj_expr->ident.name);
            if (cs && (cs->kind == SYM_CLASS || cs->kind == SYM_STRUCT)) {
                zan_symbol_t *fs = get_field_sym(cs, tgt->member.name);
                LLVMValueRef gv = fs ? get_static_field_global(g, cs, fs,
                    static_access_inst(g, obj_expr)) : NULL;
                if (gv) return gv;
            }
        }
        zan_symbol_t *cls = expr_class_sym(g, obj_expr, locals);
        if (cls) {
            int fi = get_field_index(cls, tgt->member.name);
            LLVMTypeRef st = get_struct_llvm_type(g, cls);
            if (fi >= 0 && st) {
                LLVMValueRef obj_val = cls->kind == SYM_STRUCT
                    ? emit_ref_lvalue_ptr(g, obj_expr, locals)
                    : emit_guarded_member_object(g, tgt, locals);
                if (obj_val &&
                    LLVMGetTypeKind(LLVMTypeOf(obj_val)) == LLVMPointerTypeKind) {
                    if (LLVMTypeOf(obj_val) != LLVMPointerType(st, 0))
                        obj_val = LLVMBuildBitCast(g->builder, obj_val,
                            LLVMPointerType(st, 0), "ref.obj");
                    return emit_field_ptr(g, cls, st, obj_val, fi, "ref.fld");
                }
            }
        }
        return NULL;
    }

    /* 集合/数组/Span 元素取地址传参 */
    if (tgt->kind == AST_INDEX)
        return emit_struct_elem_ptr(g, tgt, locals);

    return NULL;
}
LLVMValueRef emit_ref_arg(zan_irgen_t *g, zan_ast_node_t *arg,
                                 local_scope_t *locals) {
    zan_ast_node_t *tgt = arg->ref_arg.expr;
    if (arg->ref_arg.decl_type && tgt && tgt->kind == AST_IDENTIFIER) {
        zan_type_t *dt = resolve_type_ctx(g, arg->ref_arg.decl_type);
        LLVMTypeRef lt = map_type(g, dt);
        LLVMValueRef a = emit_entry_alloca(g, lt, "out");
        zan_store_fit(g, LLVMConstNull(lt), a);
        local_add(locals, tgt->ident.name, a, dt);
        return a;
    }
    if (tgt && tgt->kind == AST_IDENTIFIER) {
        local_var_t *l = local_find(locals, tgt->ident.name);
        if (l) return l->alloca;
        if (g->current_type_sym) {
            zan_symbol_t *fs = get_field_sym(g->current_type_sym,
                                             tgt->ident.name);
            LLVMValueRef gv = fs ? get_static_field_global(g,
                g->current_type_sym, fs, NULL) : NULL;
            if (gv) return gv;
            int fi = get_field_index(g->current_type_sym, tgt->ident.name);
            LLVMTypeRef st = get_struct_llvm_type(g, g->current_type_sym);
            if (fi >= 0 && st && g->current_this) {
                LLVMValueRef this_ptr = LLVMBuildLoad2(g->builder,
                    LLVMPointerType(st, 0), g->current_this, "this");
                return emit_field_ptr(g, g->current_type_sym, st, this_ptr,
                                      fi, "ref.this");
            }
        }
    }
    /* 位置表达式 (obj.field, arr[i]) 取地址传参 */
    LLVMValueRef place = emit_ref_lvalue_ptr(g, tgt, locals);
    if (place) {
        if (LLVMGetTypeKind(LLVMTypeOf(place)) != LLVMPointerTypeKind)
            return LLVMBuildIntToPtr(g->builder, place,
                LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0), "ref.ip");
        return place;
    }
    zan_diag_emit_code(g->diag, DIAG_ERROR, arg->loc, 3049,
        "ref/out argument must be a variable, a field, or an element");
    return emit_expr(g, tgt, locals);
}
