
static int expr_is_arc_object(zan_irgen_t *g, zan_ast_node_t *e, local_scope_t *locals) {
    zan_type_t *t = infer_expr_type(g, e, locals);
    return is_rc_managed_type(t);
}

static int expr_index_of_owned_temp(zan_irgen_t *g, zan_ast_node_t *e,
                                    local_scope_t *locals);
static int expr_member_of_owned_temp(zan_irgen_t *g, zan_ast_node_t *e,
                                     local_scope_t *locals);
int expr_yields_delegate_value(zan_irgen_t *g, zan_ast_node_t *e,
                                      local_scope_t *locals) {
    if (!e) return 0;
    if (e->kind == AST_LAMBDA) return !irgen_is_tree_lambda(g, e);
    if (e->kind != AST_MEMBER_ACCESS) return 0;
    zan_symbol_t *cls = expr_class_sym(g, e->member.object, locals);
    zan_symbol_t *ms = cls ? get_method_sym(cls, e->member.name) : NULL;
    return ms && ms->decl && ms->decl->kind == AST_METHOD_DECL &&
           (ms->decl->method_decl.modifiers & MOD_STATIC) == 0;
}

/* A556-1：登记/查询发生过隐式装箱的表达式节点（zan_irgen_t::boxed_exprs，
 * arena 分配——装箱本身就是低频路径，线性查表足够）。 */
void irgen_note_boxed_expr(zan_irgen_t *g, zan_ast_node_t *e) {
    if (!g || !e) return;
    if (g->boxed_expr_count >= g->boxed_expr_cap) {
        int ncap = g->boxed_expr_cap ? g->boxed_expr_cap * 2 : 16;
        zan_ast_node_t **nv = (zan_ast_node_t **)zan_arena_alloc(
            g->arena, (size_t)ncap * sizeof(*nv));
        for (int i = 0; i < g->boxed_expr_count; i++) nv[i] = g->boxed_exprs[i];
        g->boxed_exprs = nv;
        g->boxed_expr_cap = ncap;
    }
    g->boxed_exprs[g->boxed_expr_count++] = e;
}
int irgen_expr_was_boxed(zan_irgen_t *g, zan_ast_node_t *e) {
    if (!g || !e) return 0;
    for (int i = 0; i < g->boxed_expr_count; i++)
        if (g->boxed_exprs[i] == e) return 1;
    return 0;
}
int expr_yields_owned_rc_value(zan_irgen_t *g, zan_ast_node_t *e,
                                      local_scope_t *locals) {
    if (!e) return 0;
    /* A556-1：隐式装箱过的表达式产出 fresh owned 堆箱——消费方不得再
     * retain（与下面装箱 cast 的显式形态同一口径）。 */
    if (irgen_expr_was_boxed(g, e)) return 1;
    if (e->kind == AST_CAST_EXPR) {
        /* 装箱 cast（A454/A556-3）：`(object)structValue` 分配新堆箱，
         * 结果是 fresh owned 指针——消费方（object 变量/字段捕获）不得
         * 再对箱指针 retain（会顶成 rc=2，作用域释放只减到 1，箱泄漏）。
         * 装箱内部的字段级 retain 由 emit_box_struct 按操作数的 owned
         * 性单独判定，不走这里。 */
        zan_type_t *ct = infer_expr_type(g, e, locals);
        if (ct && ct->kind == TYPE_OBJECT) {
            zan_type_t *stc = infer_expr_type(g, e->cast.expr, locals);
            if (stc && stc->kind == TYPE_STRUCT) return 1;
        }
        return expr_yields_owned_rc_value(g, e->cast.expr, locals);
    }
    /* A successful reference `as` transfers its operand's owned reference;
     * the emitter releases that reference itself on a failed type check. */
    if (e->kind == AST_AS_EXPR)
        return expr_yields_owned_rc_value(g, e->type_test.expr, locals);
    if (e->kind == AST_POSTFIX_UNARY && e->unary.op == TK_BANG)
        return expr_yields_owned_rc_value(g, e->unary.operand, locals);
    if (e->kind == AST_TUPLE_EXPR) return 1;
    if (e->kind == AST_IDENTIFIER && g->current_type_sym &&
        !(locals && local_find(locals, e->ident.name))) {
        zan_symbol_t *bfs = get_field_sym(g->current_type_sym, e->ident.name);
        /* Bare custom properties emit getter calls just like this.Prop or
         * Type.Prop. Their results are owned; auto-properties/fields remain
         * borrowed, and a local with the same name takes precedence. */
        if (property_getter_sym(g, bfs)) return 1;
        if (g->current_this && bfs && bfs->kind == SYM_FIELD &&
            !(bfs->modifiers & MOD_STATIC) && (bfs->modifiers & MOD_WEAK))
            return 1;
    }
    if (e->kind == AST_INDEX) {
        zan_type_t *ot = infer_expr_type(g, e->index.object, locals);
        if (ot && (ot->kind == TYPE_CLASS || ot->kind == TYPE_STRUCT) && ot->sym) {
            zan_istr_t op_istr = {(char *)"op_index", 8};
            if (get_method_sym(ot->sym, op_istr)) return 1;
        }
        if (expr_index_of_owned_temp(g, e, locals)) return 1;
    }
    if (e->kind == AST_MEMBER_ACCESS && e->member.object &&
        e->member.object->kind == AST_IDENTIFIER &&
        !(locals && local_find(locals, e->member.object->ident.name))) {
        /* Match emit_expr_member_access: a type-qualified static load does not
         * evaluate an inherited field/property with the same receiver name.
         * Static fields are borrowed; custom getter results remain owned. */
        zan_symbol_t *cs = zan_binder_lookup(g->binder, e->member.object->ident.name);
        if (cs && (cs->kind == SYM_CLASS || cs->kind == SYM_STRUCT)) {
            zan_symbol_t *fs = get_field_sym(cs, e->member.name);
            if (fs && (fs->modifiers & MOD_STATIC))
                return property_getter_sym(g, fs) != NULL;
        }
    }
    if (e->kind == AST_MEMBER_ACCESS && expr_member_of_owned_temp(g, e, locals))
        return 1;
    if (e->kind == AST_MEMBER_ACCESS) {
        zan_ast_node_t *obj = e->member.object;
        /* weak 弱引用字段读取：尝试提升并向调用方返回 +1 强引用 */
        zan_type_t *ot0 = infer_expr_type(g, obj, locals);
        if (ot0 && ot0->sym) {
            zan_symbol_t *fs0 = get_field_sym(ot0->sym, e->member.name);
            if (fs0 && fs0->kind == SYM_FIELD && (fs0->modifiers & MOD_WEAK))
                return 1;
        }
        zan_type_t *ot = infer_expr_type(g, obj, locals);
        if (e->member.null_cond) ot = NULL;
        zan_symbol_t *tsym = ot ? ot->sym : NULL;
        if (ot && ot->kind == TYPE_TYPE_PARAM) {
            zan_type_t *ct = concretize(g, ot);
            tsym = ct ? ct->sym : NULL;
        }
        if (obj && obj->kind == AST_BASE_EXPR && g->current_type_sym &&
            g->current_type_sym->type &&
            g->current_type_sym->type->base_type)
            tsym = g->current_type_sym->type->base_type->sym;
        else if (obj && obj->kind == AST_THIS_EXPR)
            tsym = g->current_type_sym;
        if (!tsym && obj && obj->kind == AST_IDENTIFIER && locals &&
            !local_find(locals, obj->ident.name)) {
            zan_symbol_t *cs = zan_binder_lookup(g->binder, obj->ident.name);
            if (cs && (cs->kind == SYM_CLASS || cs->kind == SYM_STRUCT)) {
                tsym = cs;
            } else if (g->current_type_sym) {
                zan_symbol_t *fs = get_field_sym(g->current_type_sym,
                                                 obj->ident.name);
                if (fs && fs->type) tsym = fs->type->sym;
            }
        }
        if (tsym) {
            zan_symbol_t *fs = get_field_sym(tsym, e->member.name);
            if (fs && fs->kind == SYM_PROPERTY && fs->decl &&
                fs->decl->field_decl.getter_body)
                return 1;
        }
    }
    if (e->kind == AST_CALL) {
        if (e->call.callee && e->call.callee->kind == AST_MEMBER_ACCESS) {
            zan_ast_node_t *sc = e->call.callee;
            zan_type_t *st = infer_expr_type(g, sc->member.object, locals);
            if (st && type_named(st, "StringBuilder", 13)) {
                zan_istr_t nm = sc->member.name;
                if ((nm.len == 6 && memcmp(nm.str, "Append", 6) == 0) ||
                    (nm.len == 10 && memcmp(nm.str, "AppendLine", 10) == 0) ||
                    (nm.len == 5 && memcmp(nm.str, "Clear", 5) == 0)) {
                    return expr_yields_owned_rc_value(g, sc->member.object, locals);
                }
            }
        }
        /* NativeMemory */
        if (is_call_to(e, "NativeMemory", "GetString") && e->call.args.count == 3)
            return 1;
        if (is_call_to(e, "NativeMemory", "Sha256") && e->call.args.count == 2)
            return 1;
        if (is_call_to(e, "NativeMemory", "Sha1") && e->call.args.count == 2)
            return 1;
        if (is_call_to(e, "NativeMemory", "Sha512") && e->call.args.count == 2)
            return 1;
        if (is_call_to(e, "NativeMemory", "Sm3") && e->call.args.count == 2)
            return 1;
        if (is_call_to(e, "NativeMemory", "Base64Encode") && e->call.args.count == 2)
            return 1;
        zan_symbol_t *cs = extern_check_callee_sym(g, e);
        if (sym_declares_extern(cs)) {
            zan_type_t *rt = method_ret_type_at(g, cs, e, NULL, locals);
            if (!rt || rt->kind == TYPE_VOID || type_named(rt, "string", 6))
                return 0;
        }
        return 1;
    }
    if (e->kind == AST_NEW_EXPR || e->kind == AST_QUERY_EXPR) return 1;
    if (e->kind == AST_SWITCH_EXPR) return 1;
    if (e->kind == AST_WITH_EXPR) return 1;
    if (expr_yields_delegate_value(g, e, locals)) return 1;
    if (irgen_is_tree_lambda(g, e)) return 1;
    if (e->kind == AST_CONDITIONAL) {
        return expr_yields_owned_rc_value(g, e->conditional.then_expr, locals) ||
               expr_yields_owned_rc_value(g, e->conditional.else_expr, locals);
    }
    if (e->kind == AST_AWAIT_EXPR) return 1;
    if (e->kind == AST_STRING_INTERP) return 1;
    if (e->kind == AST_MEMBER_ACCESS &&
        ((e->member.name.len == 4 && memcmp(e->member.name.str, "Keys", 4) == 0) ||
         (e->member.name.len == 6 && memcmp(e->member.name.str, "Values", 6) == 0))) {
        zan_type_t *ot = infer_expr_type(g, e->member.object, locals);
        if (ot && type_named(ot, "Dict", 4))
            return 1;
    }
    if (e->kind == AST_UNARY && (e->unary.op == TK_MINUS || e->unary.op == TK_PLUS)) {
        /* 一元运算符重载的结果同样是"拥有型临时值"：调用方（如赋值）要按
           转移/释放处理，否则重载里 new 出来的对象会漏一条引用。 */
        zan_type_t *ut = infer_expr_type(g, e->unary.operand, locals);
        if (ut && (ut->kind == TYPE_CLASS || ut->kind == TYPE_STRUCT) && ut->sym) {
            const char *opn = (e->unary.op == TK_MINUS) ? "op_sub" : "op_add";
            zan_istr_t op_istr = { (char *)opn, (int)strlen(opn) };
            zan_symbol_t *ms = get_method_sym(ut->sym, op_istr);
            /* 一元重载：1 个形参（接收者即操作数）；2 个形参的是二元重载 */
            if (ms && ms->decl && ms->decl->kind == AST_METHOD_DECL &&
                ms->decl->method_decl.params.count == 1)
                return 1;
        }
    }
    if (e->kind == AST_BINARY) {
        if (e->binary.op == TK_QUESTION_QUESTION) {
            return expr_yields_owned_rc_value(g, e->binary.left, locals) ||
                   expr_yields_owned_rc_value(g, e->binary.right, locals);
        }
        if (e->binary.op == TK_PLUS && is_string_expr(g, e, locals)) {
            return 1;
        }
        const char *opn = NULL;
        switch (e->binary.op) {
        case TK_PLUS:    opn = "op_add"; break;
        case TK_MINUS:   opn = "op_sub"; break;
        case TK_STAR:    opn = "op_mul"; break;
        case TK_SLASH:   opn = "op_div"; break;
        case TK_PERCENT: opn = "op_mod"; break;
        default: break;
        }
        if (opn) {
            zan_type_t *lt = infer_expr_type(g, e->binary.left, locals);
            if (lt && (lt->kind == TYPE_CLASS || lt->kind == TYPE_STRUCT) && lt->sym) {
                zan_istr_t op_istr = { (char *)opn, (int)strlen(opn) };
                if (get_method_sym(lt->sym, op_istr)) {
                    return 1;
                }
            }
        }
    }
    return 0;
}
int expr_is_local_ident(zan_ast_node_t *e, local_scope_t *locals) {
    return e && e->kind == AST_IDENTIFIER && locals && local_find(locals, e->ident.name);
}

static int expr_index_of_owned_temp(zan_irgen_t *g, zan_ast_node_t *e,
                                    local_scope_t *locals) {
    if (!e || e->kind != AST_INDEX || !locals) return 0;
    zan_ast_node_t *obj = e->index.object;
    if (!obj || expr_is_local_ident(obj, locals)) return 0;
    zan_type_t *ct = infer_expr_type(g, obj, locals);
    if (!ct || !is_rc_managed_type(ct)) return 0;
    if (!expr_yields_owned_rc_value(g, obj, locals)) return 0;
    return is_rc_managed_type(container_elem_type(ct)) ||
           is_rc_managed_type(dict_value_type(ct));
}
zan_type_t *member_owned_field_type(zan_irgen_t *g, zan_ast_node_t *e,
                                           local_scope_t *locals) {
    zan_ast_node_t *obj = e->member.object;
    zan_type_t *ot = infer_expr_type(g, obj, locals);
    if (!ot || !ot->sym) return NULL;
    zan_symbol_t *fs = get_field_sym(ot->sym, e->member.name);
    if (!fs || !fs->type) return NULL;
    zan_type_t *ft = subst_type_param_deep(g, fs->type, ot);
    if (ft && ft->kind == TYPE_TYPE_PARAM) ft = concretize(g, ft);
    return ft;
}

/* 检查是否e */
int member_field_is_weak(zan_irgen_t *g, zan_ast_node_t *e,
                                local_scope_t *locals) {
    if (!e || e->kind != AST_MEMBER_ACCESS) return 0;
    zan_type_t *ot = infer_expr_type(g, e->member.object, locals);
    if (!ot || !ot->sym) return 0;
    zan_symbol_t *fs = get_field_sym(ot->sym, e->member.name);
    return (fs && (fs->modifiers & MOD_WEAK)) ? 1 : 0;
}

static int expr_member_of_owned_temp(zan_irgen_t *g, zan_ast_node_t *e,
                                     local_scope_t *locals) {
    if (!e || e->kind != AST_MEMBER_ACCESS || !locals) return 0;
    zan_ast_node_t *obj = e->member.object;
    if (!obj || expr_is_local_ident(obj, locals)) return 0;
    zan_type_t *ot = infer_expr_type(g, obj, locals);
    if (!ot || ot->kind != TYPE_CLASS || !is_rc_managed_type(ot)) return 0;
    if (!expr_yields_owned_rc_value(g, obj, locals)) return 0;
    zan_type_t *ft = member_owned_field_type(g, e, locals);
    return ft && is_rc_managed_type(ft);
}

/* 调用实参是不是"调用方拥有、用完要释放"的临时量。
 *
 * 正常路径在调用返回后由 emit_release_owned_call_temp 释放它；异常路径必须
 * 释放同一批临时量，否则调用抛异常时实参会整个留在堆上。lambda 实参尤其
 * 要紧：闭包连同它捕获的局部（堆 cell）一起漏，泄漏检查会把它报成
 * "captured local / closure"两个对象。
 *
 * 委托值不能拿 at（实参的推断类型）当前提：裸 lambda 没有自然类型，推断
 * 出来是空的，所以正常路径才改用 expr_yields_delegate_value 认它。这里跟
 * 正常路径同源判定，两边一致，异常路径才不会漏。 */
int call_arg_is_owned_temp(zan_irgen_t *g, zan_ast_node_t *arg,
                                  zan_type_t *at, local_scope_t *locals) {
    if (!arg) return 0;
    if (irgen_expr_was_boxed(g, arg)) return 1;
    if (expr_is_local_ident(arg, locals)) return 0;
    if (irgen_is_tree_lambda(g, arg)) return 1;
    if (expr_yields_delegate_value(g, arg, locals)) return 1;
    if (!at || !is_rc_managed_type(at)) return 0;
    return expr_yields_owned_rc_value(g, arg, locals);
}
void emit_release_owned_call_temp(zan_irgen_t *g, zan_ast_node_t *arg,
                                         LLVMValueRef val, local_scope_t *locals) {
    if (!arg || !locals || !val) return;
    /* A556-1：实参位置发生的隐式装箱——传出去的是 fresh 堆箱临时，
     * 被调方按借用协议不释放参数，调用方在调用后释放。必须先于
     * local-ident 早退（装箱的正是那个局部变量的临时箱）。 */
    if (irgen_expr_was_boxed(g, arg)) {
        emit_release_obj_value(g, val);
        return;
    }
    /* B-ID65：树 lambda 在发射口降级为 ExprNode 堆对象，作为实参临时量用完后释放。 */
    if (irgen_is_tree_lambda(g, arg)) {
        emit_release_obj_value(g, val);
        return;
    }
    if (LLVMGetTypeKind(LLVMTypeOf(val)) == LLVMStructTypeKind) {
        zan_type_t *st = infer_expr_type(g, arg, locals);
        if (st && st->kind == TYPE_STRUCT &&
            type_contains_collection_rc(g, st, 0) &&
            expr_yields_owned_rc_value(g, arg, locals))
            emit_collection_value_release(g, st, val, 0);
        return;
    }
    if (LLVMGetTypeKind(LLVMTypeOf(val)) != LLVMPointerTypeKind) return;
    if (expr_is_local_ident(arg, locals)) return;
    if (expr_yields_delegate_value(g, arg, locals)) {
        emit_closure_release(g, val);
        return;
    }
    zan_type_t *t = infer_expr_type(g, arg, locals);
    if (!t || !is_rc_managed_type(t)) return;
    if (!expr_yields_owned_rc_value(g, arg, locals)) return;
    emit_rc_release_for_type(g, t, val);
}
int call_consumes_free_arg(LLVMValueRef callee) {
    if (!callee) return 0;
    size_t name_len = 0;
    const char *name = LLVMGetValueName2(callee, &name_len);
    return name && name_len == 4 && memcmp(name, "free", 4) == 0;
}
void emit_invalidate_freed_string(zan_irgen_t *g, zan_ast_node_t *arg,
                                         local_scope_t *locals);
LLVMValueRef emit_delegate_call(zan_irgen_t *g,
                                       zan_type_t *delegate_type,
                                       LLVMValueRef fn_ptr,
                                       zan_ast_node_t *call,
                                       local_scope_t *locals) {
    /* 泛型实例里委托的形参/返回类型可能是类型参数（如泛型类的 `Func<TTarget>`
     * 字段）。调用点必须按当前实例把类型参数换成实参再发射：按擦除形态发射
     * 会与实际委托（lambda）的签名对不上——结构体返回会被读成野指针。 */
    zan_type_t *dt = delegate_type;
    if (g->cur_inst && delegate_type &&
        delegate_type->kind == TYPE_DELEGATE) {
        zan_type_t *subst = subst_type_param_deep(g, delegate_type, g->cur_inst);
        if (subst) dt = subst;
    }
    int pc = dt->delegate_param_count;
    LLVMTypeRef *param_types = (LLVMTypeRef *)calloc(
        (size_t)(pc > 0 ? pc : 1), sizeof(LLVMTypeRef));
    for (int k = 0; k < pc; k++) {
        param_types[k] = map_type(
            g, dt->delegate_param_types[k]);
    }
    LLVMTypeRef ret = dt->delegate_is_async
        ? LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0)
        : (dt->delegate_ret_type
            ? map_type(g, dt->delegate_ret_type)
            : LLVMVoidTypeInContext(g->ctx));
    LLVMTypeRef fn_type = LLVMFunctionType(
        ret, param_types, (unsigned)pc, 0);
    int argc = call->call.args.count;
    LLVMValueRef *call_args = (LLVMValueRef *)calloc(
        (size_t)(argc > 0 ? argc : 1), sizeof(LLVMValueRef));
    /* The callee is borrowed from its local/field slot. Argument evaluation
     * or the callback may overwrite that last owner, so keep the selected
     * delegate alive until the invocation and argument cleanup finish. */
    emit_closure_retain(g, fn_ptr);
    int eh_pushed = 0;
    if (!g->current_fn_no_runtime) {
        emit_eh_tmp_push_slot(g, fn_ptr, ZAN_EH_SLOT_DLG);
        eh_pushed = 1;
    }
    for (int k = 0; k < argc; k++) {
        call_args[k] = emit_expr(
            g, call->call.args.items[k], locals);
        if (k < pc) {
            call_args[k] = coerce_struct_value_arg(
                g, call_args[k], param_types[k]);
            call_args[k] = emit_boundary_coerce(
                g, call_args[k], param_types[k]);
        }
    }
    const char *name =
        LLVMGetTypeKind(ret) == LLVMVoidTypeKind ? "" : "dlgcall";
    LLVMValueRef result = emit_delegate_invoke(
        g, fn_ptr, fn_type, ret, call_args, argc, name);
    for (int k = 0; k < argc; k++) {
        emit_release_owned_call_temp(
            g, call->call.args.items[k], call_args[k], locals);
    }
    if (eh_pushed) emit_eh_tmp_pop(g);
    emit_closure_release(g, fn_ptr);
    free(call_args);
    free(param_types);
    return result;
}
void emit_leak_report_support(zan_irgen_t *g) {
    if (!g->check_leaks || g->fn_report_leaks) return;
    LLVMBuilderRef b = LLVMCreateBuilderInContext(g->ctx);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i8p  = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef rl_type = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), NULL, 0, 0);
    g->fn_report_leaks = LLVMAddFunction(g->mod, "__zan_report_leaks", rl_type);
    LLVMBasicBlockRef bb       = LLVMAppendBasicBlockInContext(g->ctx, g->fn_report_leaks, "entry");
    LLVMBasicBlockRef leak_bb  = LLVMAppendBasicBlockInContext(g->ctx, g->fn_report_leaks, "leak");
    LLVMBasicBlockRef head_bb  = LLVMAppendBasicBlockInContext(g->ctx, g->fn_report_leaks, "loop.head");
    LLVMBasicBlockRef body_bb  = LLVMAppendBasicBlockInContext(g->ctx, g->fn_report_leaks, "loop.body");
    LLVMBasicBlockRef print_bb = LLVMAppendBasicBlockInContext(g->ctx, g->fn_report_leaks, "loop.print");
    LLVMBasicBlockRef next_bb  = LLVMAppendBasicBlockInContext(g->ctx, g->fn_report_leaks, "loop.next");
    LLVMBasicBlockRef log_sum_bb  = LLVMAppendBasicBlockInContext(g->ctx, g->fn_report_leaks, "log.summary");
    LLVMBasicBlockRef log_site_bb = LLVMAppendBasicBlockInContext(g->ctx, g->fn_report_leaks, "log.site");
    LLVMBasicBlockRef close_bb = LLVMAppendBasicBlockInContext(g->ctx, g->fn_report_leaks, "log.close");
    LLVMBasicBlockRef done_bb  = LLVMAppendBasicBlockInContext(g->ctx, g->fn_report_leaks, "done");

    LLVMTypeRef fopen_args[2]  = { i8p, i8p };
    LLVMTypeRef fopen_type = LLVMFunctionType(i8p, fopen_args, 2, 0);
    LLVMValueRef fn_fopen = LLVMGetNamedFunction(g->mod, "fopen");
    if (!fn_fopen) fn_fopen = LLVMAddFunction(g->mod, "fopen", fopen_type);
    LLVMTypeRef fprintf_args[2] = { i8p, i8p };
    LLVMTypeRef fprintf_type = LLVMFunctionType(i32t, fprintf_args, 2, 1);
    LLVMValueRef fn_fprintf = LLVMGetNamedFunction(g->mod, "fprintf");
    if (!fn_fprintf) fn_fprintf = LLVMAddFunction(g->mod, "fprintf", fprintf_type);
    LLVMTypeRef fclose_args[1] = { i8p };
    LLVMTypeRef fclose_type = LLVMFunctionType(i32t, fclose_args, 1, 0);
    LLVMValueRef fn_fclose = LLVMGetNamedFunction(g->mod, "fclose");
    if (!fn_fclose) fn_fclose = LLVMAddFunction(g->mod, "fclose", fclose_type);

    LLVMPositionBuilderAtEnd(b, bb);
    LLVMValueRef live = LLVMBuildLoad2(b, i64, g->g_live, "live");
    LLVMValueRef leaked = zan_icmp(b, LLVMIntSGT, live,
        LLVMConstInt(i64, 0, 0), "leaked");
    LLVMBuildCondBr(b, leaked, leak_bb, done_bb);

    LLVMPositionBuilderAtEnd(b, leak_bb);
    LLVMValueRef msg = zan_irgen_intern_string(g,
        "zan: memory leak detected: %lld object(s) still reachable at exit\n");
    LLVMValueRef pargs[] = { msg, live };
    zan_call2(b, g->printf_type, g->fn_printf, pargs, 2, "");
    LLVMValueRef log_path = zan_irgen_intern_string(g, "zan_leaks.log");
    LLVMValueRef log_mode = zan_irgen_intern_string(g, "ab");
    LLVMValueRef fo_args[2] = { log_path, log_mode };
    LLVMValueRef log_fh = zan_call2(b, fopen_type, fn_fopen, fo_args, 2, "leaklog");
    LLVMValueRef have_log = LLVMBuildIsNotNull(b, log_fh, "havelog");
    LLVMBuildCondBr(b, have_log, log_sum_bb, head_bb);

    LLVMPositionBuilderAtEnd(b, log_sum_bb);
    LLVMValueRef fsum_args[3] = { log_fh, msg, live };
    zan_call2(b, fprintf_type, fn_fprintf, fsum_args, 3, "");
    LLVMBuildBr(b, head_bb);

    LLVMPositionBuilderAtEnd(b, head_bb);
    LLVMValueRef idx = LLVMBuildPhi(b, i64, "i");
    LLVMValueRef bound = LLVMBuildLoad2(b, i64, g->g_site_count, "bound");
    LLVMValueRef in_range = zan_icmp(b, LLVMIntSLT, idx, bound, "inrange");
    LLVMBuildCondBr(b, in_range, body_bb, close_bb);

    LLVMPositionBuilderAtEnd(b, body_bb);
    LLVMValueRef ltbl = LLVMBuildLoad2(b, LLVMPointerType(i64, 0),
        g->g_site_live, "ltbl");
    LLVMValueRef sc_ptr = LLVMBuildGEP2(b, i64, ltbl, &idx, 1, "scptr");
    LLVMValueRef sc = LLVMBuildLoad2(b, i64, sc_ptr, "sc");
    LLVMValueRef has = zan_icmp(b, LLVMIntSGT, sc, LLVMConstInt(i64, 0, 0), "has");
    LLVMBuildCondBr(b, has, print_bb, next_bb);

    LLVMPositionBuilderAtEnd(b, print_bb);
    LLVMValueRef ntbl = LLVMBuildLoad2(b, LLVMPointerType(i8p, 0),
        g->g_site_names, "ntbl");
    LLVMValueRef nm_ptr = LLVMBuildGEP2(b, i8p, ntbl, &idx, 1, "nmptr");
    LLVMValueRef nm = LLVMBuildLoad2(b, i8p, nm_ptr, "nm");
    LLVMValueRef dmsg = zan_irgen_intern_string(g,
        "  %lld object(s) leaked, allocated at %s\n");
    LLVMValueRef dargs[] = { dmsg, sc, nm };
    zan_call2(b, g->printf_type, g->fn_printf, dargs, 3, "");
    LLVMBuildCondBr(b, have_log, log_site_bb, next_bb);

    LLVMPositionBuilderAtEnd(b, log_site_bb);
    LLVMValueRef fsite_args[4] = { log_fh, dmsg, sc, nm };
    zan_call2(b, fprintf_type, fn_fprintf, fsite_args, 4, "");
    LLVMBuildBr(b, next_bb);

    LLVMPositionBuilderAtEnd(b, next_bb);
    LLVMValueRef idx1 = zan_add(b, idx, LLVMConstInt(i64, 1, 0), "i.next");
    LLVMBuildBr(b, head_bb);

    LLVMPositionBuilderAtEnd(b, close_bb);
    LLVMBasicBlockRef close_do_bb = LLVMAppendBasicBlockInContext(g->ctx, g->fn_report_leaks, "log.close.do");
    LLVMBuildCondBr(b, have_log, close_do_bb, done_bb);
    LLVMPositionBuilderAtEnd(b, close_do_bb);
    LLVMValueRef fc_args[1] = { log_fh };
    zan_call2(b, fclose_type, fn_fclose, fc_args, 1, "");
    LLVMBuildBr(b, done_bb);

    LLVMValueRef phi_vals[3] = { LLVMConstInt(i64, 0, 0), LLVMConstInt(i64, 0, 0), idx1 };
    LLVMBasicBlockRef phi_bbs[3] = { leak_bb, log_sum_bb, next_bb };
    LLVMAddIncoming(idx, phi_vals, phi_bbs, 3);

    LLVMPositionBuilderAtEnd(b, done_bb);
    LLVMBuildRetVoid(b);
    LLVMDisposeBuilder(b);

    if (!g->fn_atexit) {
        LLVMTypeRef void_fn_type = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), NULL, 0, 0);
        LLVMTypeRef void_fn_ptr = LLVMPointerType(void_fn_type, 0);
        LLVMTypeRef atexit_args[] = { void_fn_ptr };
        g->atexit_type = LLVMFunctionType(LLVMInt32TypeInContext(g->ctx), atexit_args, 1, 0);
        g->fn_atexit = LLVMAddFunction(g->mod, "atexit", g->atexit_type);
    }
}
void emit_eh_tmp_push_slot(zan_irgen_t *g, LLVMValueRef slot, int kind);
void emit_eh_tmp_push(zan_irgen_t *g, LLVMValueRef obj);
void emit_eh_tmp_pop(zan_irgen_t *g);
void emit_eh_tmp_drop(zan_irgen_t *g, LLVMValueRef obj);
zan_type_t *method_ret_type_at(zan_irgen_t *g, zan_symbol_t *msym,
                                      zan_ast_node_t *call,
                                      zan_ast_node_t *recv_expr,
                                      local_scope_t *locals);
int local_slot_owns_rc(local_var_t *v) {
    if (!v || v->arc_owned != 1 || !v->type || !is_rc_managed_type(v->type)) return 0;
    if (v->byref_slot) return 1;
    if (v->box_cell) return 1;
    return LLVMGetTypeKind(LLVMGetAllocatedType(v->alloca)) == LLVMPointerTypeKind;
}
int local_is_dyn_obj(zan_irgen_t *g, local_var_t *v) {
    if (!v || !v->type || v->type->kind != TYPE_OBJECT) return 0;
    if (v->box_cell || g->current_async_frame) return v->obj_rc_flag != NULL;
    return LLVMGetTypeKind(local_slot_type(g, v)) == LLVMPointerTypeKind;
}
int local_owns_arc(local_var_t *v) {
    return local_slot_owns_rc(v) && !v->box_cell && !v->byref_slot;
}
void emit_closure_record_release(zan_irgen_t *g, LLVMValueRef rec);
void release_boxed_local(zan_irgen_t *g, local_var_t *v) {
    if (!v || !v->box_cell || !v->box_owned) return;
    if (v->box_owner_slot) {
        LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
        LLVMValueRef tagged = LLVMBuildLoad2(g->builder, i8ptr,
                                             v->box_owner_slot, "box.owner");
        emit_closure_release(g, tagged);
        LLVMBuildStore(g->builder, LLVMConstNull(i8ptr), v->box_owner_slot);
        return;
    }
    emit_closure_record_release(g, v->box_cell);
}
void arc_own_local(zan_irgen_t *g, local_scope_t *locals) {
    if (!locals || locals->count == 0) return;
    local_var_t *v = &locals->vars[locals->count - 1];
    v->arc_owned = 1;
    lpad_register_local(g, v);
}

void lpad_register_local(zan_irgen_t *g, local_var_t *v) {
    if (!g || !v || !v->alloca || g->current_async_frame) return;
    if (g->lpad_depth <= 0) return;
    LLVMValueRef alloca_fn = NULL;
    if (LLVMIsAAllocaInst(v->alloca)) {
        LLVMBasicBlockRef abb = LLVMGetInstructionParent(v->alloca);
        if (abb) alloca_fn = LLVMGetBasicBlockParent(abb);
    }
    if (!alloca_fn) {
        alloca_fn = g->current_fn;
        if (!alloca_fn && LLVMGetInsertBlock(g->builder))
            alloca_fn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder));
    }
    if (!alloca_fn) return;
    for (int d = 0; d < g->lpad_depth; d++) {
        zan_lpad_entry_t *entry = &g->lpad_stack[d];
        if (entry->fn != alloca_fn) continue;
        int found = 0;
        for (int i = 0; i < entry->cleanup_count; i++) {
            if (entry->cleanups[i].alloca == v->alloca) {
                entry->cleanups[i] = *v;
                found = 1;
                break;
            }
        }
        if (!found) {
            if (ZAN_TAB_ENSURE(entry->cleanups, entry->cleanup_count, entry->cleanup_cap, 8)) {
                entry->cleanups[entry->cleanup_count++] = *v;
            }
        }
    }
}

void emit_lpad_cleanups(zan_irgen_t *g, zan_lpad_entry_t *entry) {
    if (!g || !entry || !entry->cleanups) return;
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    for (int i = entry->cleanup_count - 1; i >= 0; i--) {
        local_var_t *cl = &entry->cleanups[i];
        if (cl->obj_rc_flag) {
            emit_release_obj_local(g, cl);
        } else if (cl->box_cell) {
            release_boxed_local(g, cl);
        } else if (cl->struct_rc) {
            emit_struct_local_release(g, cl->type, cl->alloca);
            LLVMTypeRef st = LLVMIsAAllocaInst(cl->alloca)
                ? LLVMGetAllocatedType(cl->alloca) : LLVMTypeOf(cl->alloca);
            if (st && LLVMGetTypeKind(st) == LLVMStructTypeKind)
                LLVMBuildStore(g->builder, LLVMConstNull(st), cl->alloca);
        } else if (local_owns_arc(cl)) {
            LLVMValueRef cur = LLVMBuildLoad2(g->builder, i8ptr, cl->alloca, "arc.rel");
            emit_rc_release_for_type(g, cl->type, cur);
            LLVMTypeRef alloc_ty = LLVMIsAAllocaInst(cl->alloca)
                ? LLVMGetAllocatedType(cl->alloca) : i8ptr;
            LLVMBuildStore(g->builder, LLVMConstNull(alloc_ty), cl->alloca);
        }
    }
}

static void pop_eh_slot(zan_irgen_t *g, local_var_t *v) {
    if (v && v->eh_slot) emit_eh_tmp_pop(g);
}

static void emit_release_owned_locals_except(zan_irgen_t *g, local_scope_t *locals,
                                             local_var_t *keep) {
    if (!locals) return;
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    /* 逆序弹出：eh_slot 标记是压栈（arc_own_local）时入栈的，弹出必须按
     * 相反顺序，否则弹掉的是别的局部变量的标记，标记错位滞留栈上。 */
    for (int i = locals->count - 1; i >= 0; i--) {
        pop_eh_slot(g, &locals->vars[i]);
        if (keep && &locals->vars[i] == keep) continue;
        if (locals->vars[i].obj_rc_flag) {
            emit_release_obj_local(g, &locals->vars[i]);
        } else if (locals->vars[i].box_cell) {
            release_boxed_local(g, &locals->vars[i]);
        } else if (locals->vars[i].struct_rc) {
            emit_struct_local_release(g, locals->vars[i].type,
                                      locals->vars[i].alloca);
        } else if (local_owns_arc(&locals->vars[i])) {
            LLVMValueRef cur = LLVMBuildLoad2(g->builder, i8ptr,
                                              locals->vars[i].alloca, "arc.rel");
            emit_rc_release_for_type(g, locals->vars[i].type, cur);
        }
    }
}
void emit_release_owned_locals(zan_irgen_t *g, local_scope_t *locals) {
    emit_release_owned_locals_except(g, locals, NULL);
}
void emit_release_owned_locals_no_pop(zan_irgen_t *g, local_scope_t *locals) {
    if (!locals) return;
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    for (int i = locals->count - 1; i >= 0; i--) {
        if (locals->vars[i].obj_rc_flag) {
            emit_release_obj_local(g, &locals->vars[i]);
        } else if (locals->vars[i].box_cell) {
            release_boxed_local(g, &locals->vars[i]);
        } else if (locals->vars[i].struct_rc) {
            emit_struct_local_release(g, locals->vars[i].type,
                                      locals->vars[i].alloca);
        } else if (local_owns_arc(&locals->vars[i])) {
            LLVMValueRef cur = LLVMBuildLoad2(g->builder, i8ptr,
                                              locals->vars[i].alloca, "arc.rel");
            emit_rc_release_for_type(g, locals->vars[i].type, cur);
        }
    }
}
void emit_release_owned_locals_range(zan_irgen_t *g, local_scope_t *locals, int start) {
    if (!locals) return;
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    for (int i = locals->count - 1; i >= start; i--) {
        pop_eh_slot(g, &locals->vars[i]);
        if (locals->vars[i].obj_rc_flag) {
            emit_release_obj_local(g, &locals->vars[i]);
        } else if (locals->vars[i].box_cell) {
            release_boxed_local(g, &locals->vars[i]);
        } else if (locals->vars[i].struct_rc) {
            emit_struct_local_release(g, locals->vars[i].type,
                                      locals->vars[i].alloca);
        } else if (local_owns_arc(&locals->vars[i])) {
            LLVMValueRef cur = LLVMBuildLoad2(g->builder, i8ptr,
                                              locals->vars[i].alloca, "arc.rel");
            /* An enclosing landing pad may still visit this slot after a jump. */
            LLVMBuildStore(g->builder, LLVMConstNull(i8ptr), locals->vars[i].alloca);
            emit_rc_release_for_type(g, locals->vars[i].type, cur);
        }
    }
}
void emit_release_active_catch_excs(zan_irgen_t *g, int base) {
    LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    for (int i = g->catch_cleanup_count - 1; i >= base; i--) {
        LLVMValueRef owned_slot = g->catch_cleanups[i].owned_slot;
        LLVMValueRef exc_slot = g->catch_cleanups[i].exc_slot;
        if (!owned_slot || !exc_slot) continue;
        LLVMValueRef fn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder));
        LLVMValueRef ofl = LLVMBuildLoad2(g->builder, i32, owned_slot, "cex.own");
        LLVMValueRef is_own = zan_icmp(g->builder, LLVMIntNE, ofl,
            LLVMConstInt(i32, 0, 0), "cex.isown");
        LLVMBasicBlockRef rel_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "cex.rel");
        LLVMBasicBlockRef cont_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "cex.cont");
        LLVMBuildCondBr(g->builder, is_own, rel_bb, cont_bb);
        LLVMPositionBuilderAtEnd(g->builder, rel_bb);
        LLVMValueRef ev = LLVMBuildLoad2(g->builder, i8ptr, exc_slot, "cex.re");
        if (!g->current_async_frame) emit_eh_tmp_drop(g, ev);
        zan_call2(g->builder,
            LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), &i8ptr, 1, 0),
            g->rt_release_dyn, &ev, 1, "");
        LLVMBuildStore(g->builder, LLVMConstInt(i32, 0, 0), owned_slot);
        LLVMBuildBr(g->builder, cont_bb);
        LLVMPositionBuilderAtEnd(g->builder, cont_bb);
    }
}
void emit_clear_owned_locals_range(zan_irgen_t *g, local_scope_t *locals, int start) {
    if (!locals) return;
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    for (int i = locals->count - 1; i >= start; i--) {
        if (locals->vars[i].box_cell) continue;
        if (local_owns_arc(&locals->vars[i])) {
            LLVMBuildStore(g->builder, LLVMConstNull(i8ptr),
                           locals->vars[i].alloca);
        } else if (locals->vars[i].struct_rc) {
            LLVMBuildStore(g->builder,
                LLVMConstNull(LLVMGetAllocatedType(locals->vars[i].alloca)),
                locals->vars[i].alloca);
        }
    }
}
void emit_release_owned_locals_from(zan_irgen_t *g, local_scope_t *locals, int start) {
    if (!locals || start >= locals->count) return;
    bool terminated =
        LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder)) != NULL;
    for (int i = locals->count - 1; i >= start; i--) {
        if (!terminated) pop_eh_slot(g, &locals->vars[i]);
        if (!terminated && locals->vars[i].obj_rc_flag) {
            emit_release_obj_local(g, &locals->vars[i]);
        } else if (!terminated && locals->vars[i].box_cell) {
            release_boxed_local(g, &locals->vars[i]);
        } else if (!terminated && locals->vars[i].struct_rc) {
            emit_struct_local_release(g, locals->vars[i].type,
                                      locals->vars[i].alloca);
        } else if (!terminated && local_owns_arc(&locals->vars[i])) {
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMValueRef cur = LLVMBuildLoad2(g->builder, i8ptr,
                                              locals->vars[i].alloca, "arc.rel");
            emit_rc_release_for_type(g, locals->vars[i].type, cur);
            LLVMBuildStore(g->builder, LLVMConstNull(i8ptr), locals->vars[i].alloca);
        }
        locals->vars[i].arc_owned = 0;
    }
    locals->count = start;
}

static LLVMValueRef obj_rc_flag_slot(zan_irgen_t *g, local_var_t *v) {
    if (!v->obj_rc_flag) {
        LLVMTypeRef i1 = LLVMInt1TypeInContext(g->ctx);
        LLVMBasicBlockRef here = LLVMGetInsertBlock(g->builder);
        v->obj_rc_flag = emit_entry_alloca(g, i1, "obj.owns");
        LLVMPositionBuilderAtEnd(g->builder, here);
        LLVMBuildStore(g->builder, LLVMConstInt(i1, 0, 0), v->obj_rc_flag);
        lpad_register_local(g, v);
    }
    return v->obj_rc_flag;
}
void emit_release_obj_value(zan_irgen_t *g, LLVMValueRef cur) {
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i8t = LLVMInt8TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(i8t, 0);
    if (LLVMTypeOf(cur) != i8ptr)
        cur = LLVMBuildBitCast(g->builder, cur, i8ptr, "obj.rbc");
    LLVMValueRef fn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder));
    LLVMBasicBlockRef tag = LLVMAppendBasicBlockInContext(g->ctx, fn, "obj.tag");
    LLVMBasicBlockRef clo = LLVMAppendBasicBlockInContext(g->ctx, fn, "obj.crel");
    LLVMBasicBlockRef probe = LLVMAppendBasicBlockInContext(g->ctx, fn, "obj.probe");
    LLVMBasicBlockRef sbb = LLVMAppendBasicBlockInContext(g->ctx, fn, "obj.srel");
    LLVMBasicBlockRef obb = LLVMAppendBasicBlockInContext(g->ctx, fn, "obj.orel");
    LLVMBasicBlockRef cbb = LLVMAppendBasicBlockInContext(g->ctx, fn, "obj.rdone");
    LLVMBuildCondBr(g->builder,
        zan_icmp(g->builder, LLVMIntEQ, cur, LLVMConstNull(i8ptr), "obj.rnull"),
        cbb, tag);
    LLVMPositionBuilderAtEnd(g->builder, tag);
    LLVMBuildCondBr(g->builder, emit_closure_is_tagged(g, cur), clo, probe);
    LLVMPositionBuilderAtEnd(g->builder, clo);
    emit_closure_release(g, cur);
    LLVMBuildBr(g->builder, cbb);
    LLVMPositionBuilderAtEnd(g->builder, probe);
    LLVMValueRef neg8 = LLVMConstInt(i64, (uint64_t)ZAN_OBJ_SITE_OFF, 1);
    LLVMValueRef hp = LLVMBuildGEP2(g->builder, i8t, cur, &neg8, 1, "obj.hp");
    LLVMValueRef w = LLVMBuildLoad2(g->builder, i64,
        LLVMBuildBitCast(g->builder, hp, LLVMPointerType(i64, 0), "obj.hi"),
        "obj.hw");
    LLVMBuildCondBr(g->builder, zan_hdr_is_string(g, w, "obj.isstr"), sbb, obb);
    LLVMPositionBuilderAtEnd(g->builder, sbb);
    emit_string_release(g, cur);
    LLVMBuildBr(g->builder, cbb);
    LLVMPositionBuilderAtEnd(g->builder, obb);
    emit_arc_release_typed(g, NULL, cur);
    LLVMBuildBr(g->builder, cbb);
    LLVMPositionBuilderAtEnd(g->builder, cbb);
}
void emit_release_obj_local(zan_irgen_t *g, local_var_t *v) {
    if (!v || !v->obj_rc_flag) return;
    LLVMTypeRef i1 = LLVMInt1TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMValueRef fn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder));
    LLVMValueRef owns = LLVMBuildLoad2(g->builder, i1, v->obj_rc_flag, "obj.own");
    LLVMBasicBlockRef rel = LLVMAppendBasicBlockInContext(g->ctx, fn, "obj.rel");
    LLVMBasicBlockRef cont = LLVMAppendBasicBlockInContext(g->ctx, fn, "obj.cont");
    LLVMBuildCondBr(g->builder, owns, rel, cont);
    LLVMPositionBuilderAtEnd(g->builder, rel);
    LLVMValueRef cur = LLVMBuildLoad2(g->builder, i8ptr, v->alloca, "obj.cur");
    /* Disarm before destruction: enclosing cleanup may visit the same slot. */
    LLVMBuildStore(g->builder, LLVMConstInt(i1, 0, 0), v->obj_rc_flag);
    LLVMBuildStore(g->builder, LLVMConstNull(i8ptr), v->alloca);
    emit_release_obj_value(g, cur);
    LLVMBuildBr(g->builder, cont);
    LLVMPositionBuilderAtEnd(g->builder, cont);
}

static int obj_slot_owns_value(zan_type_t *t) {
    return t && (is_arc_managed_type(t) || t->kind == TYPE_OBJECT);
}

static int obj_slot_owns_owned_rhs(zan_irgen_t *g, zan_ast_node_t *rhs,
                                   LLVMValueRef val, local_scope_t *locals) {
    if (!rhs || !val || !locals) return 0;
    if (irgen_expr_was_boxed(g, rhs)) return 1;
    if (LLVMGetTypeKind(LLVMTypeOf(val)) != LLVMPointerTypeKind) return 0;
    zan_type_t *rt = infer_expr_type(g, rhs, locals);
    if (!rt || (rt->kind != TYPE_OBJECT && rt->kind != TYPE_STRING)) return 0;
    return expr_yields_owned_rc_value(g, rhs, locals);
}
void emit_obj_local_store(zan_irgen_t *g, local_var_t *v, LLVMValueRef val,
                                 zan_type_t *vtype, zan_ast_node_t *rhs,
                                 local_scope_t *locals) {
    LLVMTypeRef i1 = LLVMInt1TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    val = emit_implicit_box(g, v->type, val, rhs, locals);
    int owns = obj_slot_owns_value(vtype) ||
               obj_slot_owns_owned_rhs(g, rhs, val, locals);
    if (owns || v->obj_rc_flag) obj_rc_flag_slot(g, v);
    if (owns) {
        if (LLVMGetTypeKind(LLVMTypeOf(val)) == LLVMPointerTypeKind &&
            LLVMTypeOf(val) != i8ptr)
            val = LLVMBuildBitCast(g->builder, val, i8ptr, "obj.bc");
        if (!expr_yields_owned_rc_value(g, rhs, locals))
            emit_retain_obj_value(g, val);
    }
    /* Retain first: the source may alias the destination being replaced. */
    emit_release_obj_local(g, v);
    zan_store_fit(g, val, v->alloca);
    if (v->obj_rc_flag)
        LLVMBuildStore(g->builder, LLVMConstInt(i1, owns ? 1 : 0, 0),
                       v->obj_rc_flag);
}
void emit_rc_capture_local(zan_irgen_t *g, zan_type_t *type,
                                  LLVMValueRef slot_alloca, LLVMValueRef v,
                                  zan_ast_node_t *rhs, local_scope_t *locals) {
    LLVMTypeRef slot_ty = LLVMIsAAllocaInst(slot_alloca)
        ? LLVMGetAllocatedType(slot_alloca) : map_type(g, type);
    LLVMValueRef old = LLVMBuildLoad2(g->builder, slot_ty, slot_alloca, "arc.old");
    if (!expr_yields_owned_rc_value(g, rhs, locals)) emit_rc_retain_for_type(g, type, v);
    if (LLVMTypeOf(v) != slot_ty &&
        LLVMGetTypeKind(LLVMTypeOf(v)) == LLVMPointerTypeKind)
        v = LLVMBuildBitCast(g->builder, v, slot_ty, "arc.bc");
    LLVMBuildStore(g->builder, v, slot_alloca);
    emit_rc_release_for_type(g, type, old);
}
void emit_rc_store_field(zan_irgen_t *g, zan_type_t *type,
                                LLVMValueRef field_ptr, LLVMValueRef v,
                                zan_ast_node_t *rhs, local_scope_t *locals,
                                int is_weak) {
    /* A556-1 隐式装箱：object/接口字段收值形状右值。箱 fresh owned
     * （rhs 节点进 boxed 集合），字段捕获协议不再 retain。 */
    v = emit_implicit_box(g, type, v, rhs, locals);
    LLVMTypeRef vt = LLVMTypeOf(v);
    LLVMTypeRef slot_t = LLVMTypeOf(field_ptr);
    LLVMTypeRef elem_t = vt;
#if !defined(LLVM_VERSION_MAJOR) || LLVM_VERSION_MAJOR < 15
    if (LLVMGetTypeKind(slot_t) == LLVMPointerTypeKind)
        elem_t = LLVMGetElementType(slot_t);
#elif LLVM_VERSION_MAJOR < 17
    if (LLVMGetTypeKind(slot_t) == LLVMPointerTypeKind &&
        !LLVMPointerTypeIsOpaque(slot_t))
        elem_t = LLVMGetElementType(slot_t);
#else
    (void)slot_t;
#endif
    if (LLVMGetTypeKind(vt) == LLVMPointerTypeKind &&
        LLVMGetTypeKind(elem_t) == LLVMPointerTypeKind && vt != elem_t) {
        v = LLVMBuildBitCast(g->builder, v, elem_t, "fld.cast");
        vt = elem_t;
    }
    if (is_weak && is_arc_managed_type(type) &&
        (type->kind == TYPE_INTERFACE ||
         (type->kind == TYPE_CLASS && type->sym != NULL))) {
        if (LLVMGetTypeKind(vt) == LLVMPointerTypeKind) {
            emit_weak_store(g, field_ptr, v);
            /* 弱存储不接管所有权：右值若是 +1 临时（弱字段读取提升为 +1 强引用、
             * new、调用结果），存完必须在此释放，否则这一个 retain 永远还不掉——
             * LinkedList.Remove 的 `cur.next.prev = cur.prev` 曾因此每次泄漏一个节点。
             * 强路径（下方）靠"消费 owned 右值"吸收这个 +1，弱路径没有消费者。 */
            if (expr_yields_owned_rc_value(g, rhs, locals))
                emit_rc_release_for_type(g, type, v);
        } else {
            LLVMBuildStore(g->builder, v, field_ptr);
        }
        return;
    }
    if (LLVMGetTypeKind(vt) != LLVMPointerTypeKind) {
        LLVMBuildStore(g->builder, v, field_ptr);
        return;
    }
    LLVMValueRef old = LLVMBuildLoad2(g->builder, elem_t, field_ptr, "arc.fold");
    if (!expr_yields_owned_rc_value(g, rhs, locals)) emit_rc_retain_for_type(g, type, v);
    LLVMBuildStore(g->builder, v, field_ptr);
    emit_rc_release_for_type(g, type, old);
    /* A528：结构体构造函数体内写拥有型字段——把字段槽注册进 EH 临时栈，
     * 构造中途抛异常时展开器按槽地址取当前引用释放（字段重赋值安全：
     * 展开侧 load-and-null 使重复 push 无害）。类路径不需要：对象本身
     * 已注册，对象释放级联释放字段。正常返回由 ctor 入口的裸武装标记
     * 经 disarm 免释放回退（所有权移交调用方）。 */
    if (g->current_fn_is_ctor && !g->current_async_frame &&
        g->current_type_sym && g->current_type_sym->type &&
        g->current_type_sym->type->kind == TYPE_STRUCT && !is_weak) {
        emit_eh_tmp_push_slot(g, v, eh_slot_kind_of(type));
    }
}

int field_is_binding_target(zan_symbol_t *field) {
    return field && field->kind == SYM_FIELD && field->parent &&
        type_is_binding(field->parent->type) &&
        field->name.len == 6 && memcmp(field->name.str, "target", 6) == 0;
}
/* Binding.target is a borrowed slot in both synthesized and explicit bindings.
 * Its destructor deliberately skips target; retaining here would leak the owner.
 * Unlike tracked weak class fields this object slot does not auto-null on death. */
void emit_rc_store_member(zan_irgen_t *g, zan_symbol_t *field,
                          zan_type_t *type, LLVMValueRef field_ptr,
                          LLVMValueRef value, zan_ast_node_t *rhs,
                          local_scope_t *locals) {
    if (field_is_binding_target(field)) {
        if (expr_yields_owned_rc_value(g, rhs, locals)) {
            zan_diag_emit_code(g->diag, DIAG_ERROR,
                rhs ? rhs->loc : field->decl->loc, 3105,
                "Binding.target is non-owning and cannot reference an owned "
                "temporary; keep the target in an owning local or field");
            return;
        }
        /* Neither acquire the new borrow nor release the previous borrow. */
        zan_store_fit(g, value, field_ptr);
        return;
    }
    emit_rc_store_field(g, type, field_ptr, value, rhs, locals,
        field && (field->modifiers & MOD_WEAK) ? 1 : 0);
}
