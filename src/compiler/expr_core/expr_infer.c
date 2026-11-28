void infer_cache_invalidate(void) { g_infer_ctx.live = false; }

static zan_type_t *null_cond_result_type(zan_irgen_t *g, zan_ast_node_t *e,
                                         zan_type_t *t) {
    zan_ast_node_t *m = NULL;
    if (e->kind == AST_MEMBER_ACCESS) m = e;
    else if (e->kind == AST_CALL && e->call.callee) m = e->call.callee;
    if (!m || m->kind != AST_MEMBER_ACCESS || !m->member.null_cond) return t;
    return zan_binder_make_nullable_type(g->binder, t);
}

static zan_type_t *infer_expr_type_uncached(zan_irgen_t *g, zan_ast_node_t *e,
                                            local_scope_t *locals) {
    zan_type_t *t = null_cond_result_type(g, e, infer_expr_type_raw(g, e, locals));
    if (!t || !g->cur_inst || type_is_concrete(t)) return t;
    if (t->kind == TYPE_DELEGATE) return t;
    if (t->kind == TYPE_TYPE_PARAM) return t;
    return subst_type_param_deep(g, t, g->cur_inst);
}

static int g_infer_depth;
static bool g_infer_bailed;
zan_type_t *infer_expr_type(zan_irgen_t *g, zan_ast_node_t *e,
                                   local_scope_t *locals) {
    if (!e) return NULL;
    if (e->in_infer & 1) return NULL;
    infer_cache_slot_t *slot = infer_cache_slot(g, e, locals);
    if (slot && slot->node == e) return slot->type;
    if (g_infer_depth >= ZAN_MAX_INFER_DEPTH) {
        if (!g_infer_bailed) {
            g_infer_bailed = true;
            zan_diag_emit_code(g->diag, DIAG_ERROR, e->loc, 2104,
                          "expression is nested too deeply to type "
                          "(inference recursed %d levels); split it into "
                          "statements", ZAN_MAX_INFER_DEPTH);
        }
        return NULL;
    }
    e->in_infer |= 1;
    g_infer_depth++;
    zan_type_t *t = infer_expr_type_uncached(g, e, locals);
    g_infer_depth--;
    e->in_infer &= ~1;
    slot = infer_cache_slot(g, e, locals);
    if (slot) {
        slot->node = e;
        slot->type = t;
    }
    return t;
}

/* 两支数值提升（infer 侧）：与 checker 的 promote_numeric 同口径——
 * double > float > ulong > long > nint > int，decimal 只与整数合。曾按 then 臂
 * 返回类型，(t?1:2.0)/2 被推成 int 除法得 0（C# 0.5，TASKS.md A514）。 */
static zan_type_t *irgen_promote_pair(zan_irgen_t *g, zan_type_t *a,
                                      zan_type_t *b) {
    if (!a) return b;
    if (!b) return a;
    if (a->kind == TYPE_DECIMAL) return (b->kind == TYPE_FLOAT ||
        b->kind == TYPE_DOUBLE) ? NULL : g->binder->type_decimal;
    if (b->kind == TYPE_DECIMAL) return (a->kind == TYPE_FLOAT ||
        a->kind == TYPE_DOUBLE) ? NULL : g->binder->type_decimal;
    if (a->kind == TYPE_DOUBLE || b->kind == TYPE_DOUBLE) return g->binder->type_double;
    if (a->kind == TYPE_FLOAT  || b->kind == TYPE_FLOAT)  return g->binder->type_float;
    if (a->kind == TYPE_ULONG  || b->kind == TYPE_ULONG)  return g->binder->type_ulong;
    if (a->kind == TYPE_LONG   || b->kind == TYPE_LONG)   return g->binder->type_long;
    if (a->kind == TYPE_NINT   || b->kind == TYPE_NINT)   return g->binder->type_nint;
    if (a->kind == TYPE_UINT   || b->kind == TYPE_UINT)   return g->binder->type_uint;
    int fa = type_family(a);
    int fb = type_family(b);
    if ((fa == FAM_INT || fa == FAM_FLOAT || fa == FAM_DECIMAL) &&
        (fb == FAM_INT || fb == FAM_FLOAT || fb == FAM_DECIMAL)) {
        return g->binder ? g->binder->type_int : a;
    }
    return a;
}

static zan_type_t *infer_expr_type_raw(zan_irgen_t *g, zan_ast_node_t *e,
                                       local_scope_t *locals) {
    if (!e) return NULL;
    zan_type_t *method_group_target = zan_binder_method_group_target(e);
    if (method_group_target) return concretize(g, method_group_target);
    switch (e->kind) {
    case AST_STRING_LITERAL:
        return g->binder ? g->binder->type_string : NULL;
    case AST_INT_LITERAL:
        if (!g->binder) return NULL;
        switch (e->lit_suffix) {
        case 1:
            return g->binder->type_long;
        case 2:
            if (e->int_val < 0 || e->int_val > 4294967295LL)
                return g->binder->type_ulong;
            return g->binder->type_uint;
        case 3:
            return g->binder->type_ulong;
        default:
            if ((e->lit_radix == 2 || e->lit_radix == 8
                 || e->lit_radix == 16)
                && e->int_val >= 0 && e->int_val <= 4294967295LL) {
                return g->binder->type_int;
            }
            if (e->int_val < -2147483648LL || e->int_val > 2147483647LL)
                return g->binder->type_long;
            return g->binder->type_int;
        }
    case AST_FLOAT_LITERAL:
        /* 与 checker 一致:f 后缀字面量推断为 float */
        if (g->binder && e->lit_suffix) { return g->binder->type_float; }
        return g->binder ? g->binder->type_double : NULL;
    case AST_DECIMAL_LITERAL:
        return g->binder ? g->binder->type_decimal : NULL;
    case AST_CHAR_LITERAL:
        return g->binder ? g->binder->type_char : NULL;
    case AST_BOOL_LITERAL:
        return g->binder ? g->binder->type_bool : NULL;
    case AST_IDENTIFIER: {
        if (e->ident.inst_type_ref)
            return resolve_type_ctx(g, e->ident.inst_type_ref);
        if (e->ident.is_type_receiver) {
            zan_symbol_t *ts = zan_binder_type_receiver(g->binder, e);
            return ts ? ts->type : NULL;
        }
        local_var_t *l = local_find(locals, e->ident.name);
        if (l) return l->type;
        if (resolve_const_param_val(g, e->ident.name, NULL))
            return g->binder ? g->binder->type_int : NULL;
        if (g->current_type_sym) {
            zan_symbol_t *fs = get_field_sym(g->current_type_sym, e->ident.name);
            /* `item` (implicit `this */
            if (fs) return concretize(g, fs->type);
        }
        return NULL;
    }
    case AST_IS_EXPR:
        /* `o is T` 是 bool（checker 同口径）：缺这一支时推断落到默认，
         * Console.WriteLine(o is T) 按整数打印 1/0，而存进 bool 变量
         * 再打印是 true/false——同一个值两种输出。 */
        return g->binder->type_bool;
    case AST_THIS_EXPR:
        return g->current_type_sym ? g->current_type_sym->type : NULL;
    case AST_BASE_EXPR:
        /* `base */
        if (g->current_type_sym && g->current_type_sym->type
            && g->current_type_sym->type->base_type) {
            return g->current_type_sym->type->base_type;
        }
        return g->current_type_sym ? g->current_type_sym->type : NULL;
    case AST_AWAIT_EXPR:
        return infer_expr_type(g, e->await_expr.expr, locals);
    case AST_REF_ARG:
        return infer_expr_type(g, e->ref_arg.expr, locals);
    case AST_LAMBDA:
        if (irgen_is_tree_lambda(g, e) && e->lambda.params.count == 1 &&
            e->lambda.params.items[0]->param.type) {
            return resolve_type_ctx(g, e->lambda.params.items[0]->param.type);
        }
        return NULL;
    case AST_QUERY_EXPR: {
        zan_type_t *src_ty = infer_expr_type(g, e->query.source, locals);
        zan_type_t *elem = container_elem_type(src_ty);
        if (!elem) elem = g->binder->type_int;
        int mark = locals->count;
        local_add(locals, e->query.var, NULL, elem);
        for (int ci = 0; ci < e->query.clauses.count; ci++) {
            zan_ast_node_t *cl = e->query.clauses.items[ci];
            switch (cl->kind) {
            case AST_QUERY_LET: {
                zan_type_t *lt =
                    infer_expr_type(g, cl->query_clause.expr, locals);
                if (!lt) lt = g->binder->type_int;
                local_add(locals, cl->query_clause.name, NULL, lt);
                break;
            }
            case AST_QUERY_JOIN: {
                zan_type_t *jty =
                    infer_expr_type(g, cl->query_clause.source, locals);
                zan_type_t *je = container_elem_type(jty);
                if (!je) je = g->binder->type_int;
                if (cl->query_clause.into.len > 0)
                    local_add(locals, cl->query_clause.into, NULL,
                              zan_binder_make_list_type(g->binder, je));
                else
                    local_add(locals, cl->query_clause.name, NULL, je);
                break;
            }
            default:
                break;
            }
        }
        if (e->query.group_expr) {
            zan_type_t *ge = infer_expr_type(g, e->query.group_expr, locals);
            if (!ge) ge = elem;
            zan_type_t *grp = zan_binder_make_grouping_type(g->binder, ge);
            if (e->query.group_into.len > 0) {
                int mark2 = locals->count;
                local_add(locals, e->query.group_into, NULL, grp);
                zan_type_t *sel = infer_expr_type(g, e->query.select, locals);
                locals->count = mark2;
                locals->count = mark;
                if (!sel) sel = ge;
                return zan_binder_make_list_type(g->binder, sel);
            }
            locals->count = mark;
            return zan_binder_make_list_type(g->binder, grp);
        }
        zan_type_t *sel = infer_expr_type(g, e->query.select, locals);
        locals->count = mark;
        if (!sel) sel = elem;
        return zan_binder_make_list_type(g->binder, sel);
    }
    case AST_MEMBER_ACCESS: {
        {
            zan_type_t *rt = infer_expr_type(g, e->member.object, locals);
            if (zan_refl_is_typeinfo(rt)) {
                zan_type_t *mt = zan_refl_member_type(g->binder, rt,
                                                      e->member.name);
                if (mt) return mt;
            }
        }
        if (e->member.object->kind == AST_IDENTIFIER &&
            !local_find(locals, e->member.object->ident.name)) {
            zan_type_t *tp_ct = resolve_tp_ident_type(g, e->member.object);
            if (tp_ct) {
                if (tp_ct->kind == TYPE_CLASS || tp_ct->kind == TYPE_STRUCT) {
                    zan_symbol_t *fs = tp_ct->sym
                        ? get_field_sym(tp_ct->sym, e->member.name) : NULL;
                    if (fs) {
                        zan_type_t *sinst = tp_ct->type_arg_count > 0 ? tp_ct : NULL;
                        zan_type_t *ft = field_store_type(g, fs, sinst);
                        return ft ? ft : fs->type;
                    }
                } else if ((e->member.name.len == 4 &&
                            memcmp(e->member.name.str, "Zero", 4) == 0) ||
                           (e->member.name.len == 3 &&
                            memcmp(e->member.name.str, "One", 3) == 0)) {
                    return tp_ct;
                }
            }
            zan_symbol_t *cs = zan_binder_lookup(g->binder,
                                                 e->member.object->ident.name);
            if (cs && (cs->kind == SYM_CLASS || cs->kind == SYM_STRUCT)) {
                zan_symbol_t *fs = get_field_sym(cs, e->member.name);
                if (fs) {
                    zan_type_t *sinst = static_access_inst(g, e->member.object);
                    zan_type_t *ft = field_store_type(g, fs, sinst);
                    return ft ? ft : fs->type;
                }
            }
        }
        if (e->member.object->kind == AST_IDENTIFIER &&
            !local_find(locals, e->member.object->ident.name)) {
            zan_symbol_t *es = zan_binder_lookup(g->binder,
                                                 e->member.object->ident.name);
            if (es && es->kind == SYM_ENUM) {
                for (int ei = 0; ei < es->member_count; ei++) {
                    zan_symbol_t *em = es->members[ei];
                    if (em && em->kind == SYM_ENUM_MEMBER &&
                        em->name.len == e->member.name.len &&
                        memcmp(em->name.str, e->member.name.str,
                               (size_t)e->member.name.len) == 0)
                        return es->type;
                }
            }
        }
        if (e->member.object->kind == AST_IDENTIFIER &&
            !local_find(locals, e->member.object->ident.name)) {
            zan_istr_t on = e->member.object->ident.name;
            zan_istr_t mn = e->member.name;
            bool is_scalar = (on.len == 3 && memcmp(on.str, "int", 3) == 0) ||
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
            if (is_scalar) {
                bool is_const = (mn.len == 8 &&
                                 (memcmp(mn.str, "MaxValue", 8) == 0 ||
                                  memcmp(mn.str, "MinValue", 8) == 0)) ||
                    (mn.len == 3 && memcmp(mn.str, "NaN", 3) == 0) ||
                    (mn.len == 16 &&
                     (memcmp(mn.str, "PositiveInfinity", 16) == 0 ||
                      memcmp(mn.str, "NegativeInfinity", 16) == 0)) ||
                    (mn.len == 7 && memcmp(mn.str, "Epsilon", 7) == 0);
                if (is_const) {
                    if (on.len == 3 && memcmp(on.str, "int", 3) == 0)
                        return g->binder->type_int;
                    if (on.len == 4 && memcmp(on.str, "uint", 4) == 0)
                        return g->binder->type_uint;
                    if (on.len == 4 && memcmp(on.str, "long", 4) == 0)
                        return g->binder->type_long;
                    if (on.len == 5 && memcmp(on.str, "ulong", 5) == 0)
                        return g->binder->type_ulong;
                    if (on.len == 5 && memcmp(on.str, "short", 5) == 0)
                        return g->binder->type_short;
                    if (on.len == 6 && memcmp(on.str, "ushort", 6) == 0)
                        return g->binder->type_ushort;
                    if (on.len == 4 && memcmp(on.str, "byte", 4) == 0)
                        return g->binder->type_byte;
                    if (on.len == 5 && memcmp(on.str, "sbyte", 5) == 0)
                        return g->binder->type_sbyte;
                    if (on.len == 4 && memcmp(on.str, "char", 4) == 0)
                        return g->binder->type_char;
                    if (on.len == 5 && memcmp(on.str, "float", 5) == 0)
                        return g->binder->type_float;
                    if (on.len == 6 && memcmp(on.str, "double", 6) == 0)
                        return g->binder->type_double;
                }
            }
        }
        zan_type_t *ot = infer_expr_type(g, e->member.object, locals);
        /* Dict */
        if (ot && type_named(ot, "Dict", 4)) {
            if (e->member.name.len == 4 &&
                memcmp(e->member.name.str, "Keys", 4) == 0)
                return zan_binder_make_list_type(g->binder, dict_key_type(g, ot));
            if (e->member.name.len == 6 &&
                memcmp(e->member.name.str, "Values", 6) == 0) {
                zan_type_t *vt = dict_value_type(ot);
                if (vt) return zan_binder_make_list_type(g->binder, vt);
            }
        }
        if ((is_span_type(ot) || is_inline_array_type(ot) || is_vec_type(ot)) &&
            ((e->member.name.len == 6 &&
              memcmp(e->member.name.str, "Length", 6) == 0) ||
             (e->member.name.len == 5 &&
              memcmp(e->member.name.str, "Count", 5) == 0)))
            return g->binder->type_int;
        if (ot && ot->kind == TYPE_NULLABLE) {
            if (e->member.name.len == 8 &&
                memcmp(e->member.name.str, "HasValue", 8) == 0)
                return g->binder->type_bool;
            if (e->member.name.len == 5 &&
                memcmp(e->member.name.str, "Value", 5) == 0)
                return ot->element_type;
        }
        if (ot && ot->sym) {
            zan_symbol_t *fs = get_field_sym(ot->sym, e->member.name);
            if (fs) {
                zan_type_t *ft = subst_type_param_deep(g, fs->type, ot);
                if (ft && ft->kind == TYPE_TYPE_PARAM) ft = concretize(g, ft);
                return ft;
            }
        }
        if (ot) {
            const char *bt = NULL;
            if (ot->kind == TYPE_STRING) bt = "string";
            else if (type_named(ot, "List", 4)) bt = "List";
            else if (type_named(ot, "Dict", 4)) bt = "Dict";
            else if (type_named(ot, "StringBuilder", 13)) bt = "StringBuilder";
            if (bt && zan_builtin_member_kind(bt, e->member.name.str,
                                              (int)e->member.name.len) == 'P') {
                const char *res = zan_builtin_member_result(
                    bt, e->member.name.str, (int)e->member.name.len);
                if (res) {
                    if (strcmp(res, "int") == 0) return g->binder->type_int;
                    if (strcmp(res, "long") == 0) return g->binder->type_long;
                    if (strcmp(res, "bool") == 0) return g->binder->type_bool;
                    if (strcmp(res, "double") == 0) return g->binder->type_double;
                    if (strcmp(res, "string") == 0) return g->binder->type_string;
                }
            }
            if (ot->kind == TYPE_ARRAY && e->member.name.len == 6 &&
                memcmp(e->member.name.str, "Length", 6) == 0)
                return g->binder->type_int;
            if (ot->kind == TYPE_ARRAY && e->member.name.len == 5 &&
                memcmp(e->member.name.str, "Count", 5) == 0)
                return g->binder->type_int;
        }
        return NULL;
    }
    case AST_INDEX: {
        zan_type_t *ot = infer_expr_type(g, e->index.object, locals);
        if (ot && (ot->kind == TYPE_CLASS || ot->kind == TYPE_STRUCT) && ot->sym) {
            zan_istr_t op_istr = {(char *)"op_index", 8};
            zan_ast_node_t *op_call = zan_ast_new(g->arena, AST_CALL, e->loc);
            op_call->call.callee = NULL;
            zan_ast_list_init(&op_call->call.args);
            zan_ast_list_init(&op_call->call.type_args);
            zan_ast_list_push(&op_call->call.args, e->index.index, g->arena);
            zan_symbol_t *op = resolve_op_overload(g, ot->sym,
                                                   op_istr, op_call, locals);
            if (op) return op->type;
        }
        if (ot && type_named(ot, "Dict", 4) &&
            ot->type_arg_count == 2)
            return ot->type_args[1];
        if (ot && ot->kind == TYPE_STRING) return g->binder->type_char;
        if (is_inline_array_type(ot)) return inline_array_elem(ot);
        if (is_vec_type(ot)) return vec_elem(ot);
        return container_elem_type(ot);
    }
    case AST_CALL: {
        zan_ast_node_t *callee = e->call.callee;
        if (!callee) return NULL;
        /* EnumType */
        if (callee->kind == AST_MEMBER_ACCESS &&
            callee->member.object->kind == AST_IDENTIFIER &&
            callee->member.name.len == 8 &&
            memcmp(callee->member.name.str, "TryParse", 8) == 0) {
            zan_symbol_t *es = zan_binder_lookup(g->binder,
                callee->member.object->ident.name);
            if (es && es->kind == SYM_ENUM) {
                return g->binder->type_bool;
            }
        }
        if (callee->kind == AST_MEMBER_ACCESS) {
            zan_type_t *rt = infer_expr_type(g, callee->member.object, locals);
            if (zan_refl_is_typeinfo(rt) ||
                (zan_refl_is_reflectable(rt) &&
                 (!rt->sym || !get_method_sym(rt->sym, callee->member.name)))) {
                zan_type_t *mt = zan_refl_member_type(g->binder, rt,
                                                      callee->member.name);
                if (mt) return mt;
            }
        }
        if (callee->kind == AST_IDENTIFIER &&
            callee->ident.name.len == 8 &&
            memcmp(callee->ident.name.str, "comptime", 8) == 0 &&
            e->call.args.count == 1 &&
            !local_find(locals, callee->ident.name)) {
            return infer_expr_type(g, e->call.args.items[0], locals);
        }
        if (e->call.args.count == 1) {
            if (callee->kind == AST_IDENTIFIER &&
                callee->ident.name.len == 13 &&
                memcmp(callee->ident.name.str, "static_assert", 13) == 0 &&
                !local_find(locals, callee->ident.name))
                return g->binder->type_void;
            if (callee->kind == AST_IDENTIFIER &&
                ((callee->ident.name.len == 6 &&
                  memcmp(callee->ident.name.str, "likely", 6) == 0) ||
                 (callee->ident.name.len == 8 &&
                  memcmp(callee->ident.name.str, "unlikely", 8) == 0)) &&
                !local_find(locals, callee->ident.name))
                return g->binder->type_bool;
            if (callee->kind == AST_MEMBER_ACCESS &&
                callee->member.object &&
                callee->member.object->kind == AST_IDENTIFIER &&
                callee->member.object->ident.name.len == 4 &&
                memcmp(callee->member.object->ident.name.str, "Hint", 4) == 0 &&
                ((callee->member.name.len == 6 &&
                  memcmp(callee->member.name.str, "Likely", 6) == 0) ||
                 (callee->member.name.len == 8 &&
                  memcmp(callee->member.name.str, "Unlikely", 8) == 0)) &&
                !local_find(locals, callee->member.object->ident.name))
                return g->binder->type_bool;
        }
        if (callee->kind == AST_MEMBER_ACCESS && callee->member.object &&
            callee->member.object->kind == AST_IDENTIFIER &&
            !local_find(locals, callee->member.object->ident.name)) {
            zan_istr_t on = callee->member.object->ident.name;
            zan_istr_t mn = callee->member.name;
            if (on.len == 4 && memcmp(on.str, "Hint", 4) == 0) {
                if ((mn.len == 6 && memcmp(mn.str, "Assume", 6) == 0) ||
                    (mn.len == 11 && memcmp(mn.str, "Unreachable", 11) == 0) ||
                    (mn.len == 4 && memcmp(mn.str, "Trap", 4) == 0))
                    return g->binder->type_void;
                if (mn.len == 8 && memcmp(mn.str, "BlackBox", 8) == 0) {
                    if (e->call.type_args.count == 1)
                        return resolve_type_ctx(g, e->call.type_args.items[0]);
                    if (e->call.args.count == 1)
                        return infer_expr_type(g, e->call.args.items[0], locals);
                }
            }
            if (on.len == 9 && memcmp(on.str, "InlineAsm", 9) == 0 &&
                ((mn.len == 4 && memcmp(mn.str, "Emit", 4) == 0) ||
                 (mn.len == 9 && memcmp(mn.str, "EmitIntel", 9) == 0))) {
                if (e->call.type_args.count == 1)
                    return resolve_type_ctx(g, e->call.type_args.items[0]);
                return g->binder->type_void;
            }
            if (on.len == 12 && memcmp(on.str, "NativeMemory", 12) == 0) {
                if ((mn.len == 7 && memcmp(mn.str, "Realloc", 7) == 0) ||
                    (mn.len == 12 && memcmp(mn.str, "AlignedAlloc", 12) == 0) ||
                    (mn.len == 9 && memcmp(mn.str, "AddressOf", 9) == 0) ||
                    (mn.len == 15 && memcmp(mn.str, "FunctionPointer", 15) == 0) ||
                    (mn.len == 13 && memcmp(mn.str, "ReturnAddress", 13) == 0) ||
                    (mn.len == 12 && memcmp(mn.str, "FrameAddress", 12) == 0) ||
                    (mn.len == 3 && memcmp(mn.str, "Add", 3) == 0))
                    return g->binder->type_nint;
                if (mn.len == 10 && memcmp(mn.str, "ByteOffset", 10) == 0)
                    return g->binder->type_long;
                if (mn.len == 12 && memcmp(mn.str, "IncludeBytes", 12) == 0)
                    return zan_binder_make_span_type(g->binder, g->binder->type_byte);
                if ((mn.len == 11 && memcmp(mn.str, "AlignedFree", 11) == 0) ||
                    (mn.len == 10 && memcmp(mn.str, "InvokeVoid", 10) == 0) ||
                    (mn.len == 16 && memcmp(mn.str, "InvokeVoidWithCC", 16) == 0) ||
                    (mn.len == 18 && memcmp(mn.str, "InvokeVariadicVoid", 18) == 0) ||
                    (mn.len == 8 && memcmp(mn.str, "ZeroInit", 8) == 0) ||
                    (mn.len == 5 && memcmp(mn.str, "Write", 5) == 0) ||
                    (mn.len == 14 && memcmp(mn.str, "WriteUnaligned", 14) == 0) ||
                    (mn.len == 11 && memcmp(mn.str, "StreamStore", 11) == 0))
                    return g->binder->type_void;
                if (e->call.type_args.count == 2 &&
                    mn.len == 7 && memcmp(mn.str, "BitCast", 7) == 0)
                    return resolve_type_ctx(g, e->call.type_args.items[1]);
                if (e->call.type_args.count == 1) {
                    zan_type_t *et = resolve_type_ctx(g, e->call.type_args.items[0]);
                    if ((mn.len == 4 && memcmp(mn.str, "Read", 4) == 0) ||
                        (mn.len == 13 && memcmp(mn.str, "ReadUnaligned", 13) == 0) ||
                        (mn.len == 10 && memcmp(mn.str, "StreamLoad", 10) == 0) ||
                        (mn.len == 5 && memcmp(mn.str, "AsRef", 5) == 0) ||
                        (mn.len == 2 && memcmp(mn.str, "At", 2) == 0) ||
                        (mn.len == 6 && memcmp(mn.str, "Invoke", 6) == 0) ||
                        (mn.len == 12 && memcmp(mn.str, "InvokeWithCC", 12) == 0) ||
                        (mn.len == 14 && memcmp(mn.str, "InvokeVariadic", 14) == 0))
                        return et;
                    if (mn.len == 6 && memcmp(mn.str, "AsSpan", 6) == 0 && et)
                        return zan_binder_make_span_type(g->binder, et);
                }
            }
            bool is_vol = (on.len == 8 && memcmp(on.str, "Volatile", 8) == 0);
            bool is_atm = (on.len == 6 && memcmp(on.str, "Atomic", 6) == 0);
            if (is_vol || is_atm) {
                if (is_vol &&
                    ((mn.len == 9 && memcmp(mn.str, "CopyBlock", 9) == 0) ||
                     (mn.len == 9 && memcmp(mn.str, "InitBlock", 9) == 0)))
                    return g->binder->type_void;
                if (is_atm && mn.len == 20 &&
                    memcmp(mn.str, "CompareExchange128At", 20) == 0)
                    return g->binder->type_bool;
                bool is_at = (mn.len > 2 && memcmp(mn.str + mn.len - 2, "At", 2) == 0);
                zan_istr_t base_mn = mn;
                if (is_at) base_mn.len -= 2;
                if (is_atm && base_mn.len == 19 &&
                    memcmp(base_mn.str, "CompareExchangeBool", 19) == 0)
                    return g->binder->type_bool;
                if ((is_vol && base_mn.len == 5 && memcmp(base_mn.str, "Write", 5) == 0) ||
                    (is_atm && ((base_mn.len == 5 && memcmp(base_mn.str, "Store", 5) == 0) ||
                                (base_mn.len == 5 && memcmp(base_mn.str, "Fence", 5) == 0) ||
                                (base_mn.len == 5 && memcmp(base_mn.str, "Pause", 5) == 0) ||
                                (base_mn.len == 13 && memcmp(base_mn.str, "CompilerFence", 13) == 0))))
                    return g->binder->type_void;
                if (is_at && e->call.type_args.count == 1)
                    return resolve_type_ctx(g, e->call.type_args.items[0]);
                if (e->call.args.count >= 1 && e->call.args.items[0] &&
                    e->call.args.items[0]->kind == AST_REF_ARG &&
                    e->call.args.items[0]->ref_arg.expr) {
                    zan_type_t *lt = infer_expr_type(g, e->call.args.items[0]->ref_arg.expr, locals);
                    if (lt) return lt;
                }
            }
        }
        {
            zan_type_t *ct = infer_expr_type_raw(g, callee, locals);
            if (ct && (ct->kind == TYPE_CLASS || ct->kind == TYPE_STRUCT) && ct->sym) {
                zan_istr_t op_istr = {(char *)"op_call", 7};
                zan_symbol_t *op = resolve_op_overload(g, ct->sym,
                                                       op_istr, e, locals);
                if (op) return op->type;
            }
        }
        if (callee->kind == AST_IDENTIFIER || callee->kind == AST_MEMBER_ACCESS) {
            zan_type_t *dt = infer_expr_type_raw(g, callee, locals);
            if (dt && dt->kind == TYPE_DELEGATE)
                return dt->delegate_ret_type;
        }
        if (callee->kind == AST_MEMBER_ACCESS) {
            zan_istr_t mm = callee->member.name;
            if ((mm.len == 6 && memcmp(mm.str, "AsSpan", 6) == 0) ||
                (mm.len == 14 && memcmp(mm.str, "AsReadOnlySpan", 14) == 0)) {
                bool is_ro = (mm.len == 14);
                zan_type_t *aot = infer_expr_type(g, callee->member.object, locals);
                if (aot && aot->kind == TYPE_ARRAY && aot->element_type)
                    return is_ro
                        ? zan_binder_make_readonly_span_type(g->binder, aot->element_type)
                        : zan_binder_make_span_type(g->binder, aot->element_type);
                if (is_inline_array_type(aot) && inline_array_elem(aot))
                    return is_ro
                        ? zan_binder_make_readonly_span_type(g->binder, inline_array_elem(aot))
                        : zan_binder_make_span_type(g->binder, inline_array_elem(aot));
                if (aot && aot->kind == TYPE_STRING)
                    return zan_binder_make_readonly_span_type(g->binder, g->binder->type_char);

                if (is_span_type(aot)) return aot;
            }
            if (mm.len == 5 && memcmp(mm.str, "Slice", 5) == 0) {
                zan_type_t *sot = infer_expr_type(g, callee->member.object, locals);
                if (is_span_type(sot)) return sot;
            }
            {
                zan_type_t *vot = infer_expr_type(g, callee->member.object, locals);
                if (is_vec_type(vot)) {
                    if ((mm.len == 3 && memcmp(mm.str, "Sum", 3) == 0) ||
                        (mm.len == 3 && memcmp(mm.str, "Dot", 3) == 0) ||
                        (mm.len == 4 && memcmp(mm.str, "HMin", 4) == 0) ||
                        (mm.len == 4 && memcmp(mm.str, "HMax", 4) == 0) ||
                        (e->call.args.count == 0 &&
                         ((mm.len == 3 && memcmp(mm.str, "Min", 3) == 0) ||
                          (mm.len == 3 && memcmp(mm.str, "Max", 3) == 0))))
                        return vec_elem(vot);
                    if (mm.len == 5 && memcmp(mm.str, "Store", 5) == 0)
                        return g->binder->type_void;
                    return vot;
                }
            }
            if (mm.len == 7 && memcmp(mm.str, "ToBytes", 7) == 0) {
                zan_type_t *bot = infer_expr_type(g, callee->member.object, locals);
                if (bot && bot->kind == TYPE_STRING)
                    return zan_binder_make_array_type(g->binder, g->binder->type_byte);
            }
            if (mm.len == 17 && memcmp(mm.str, "GetValueOrDefault", 17) == 0) {
                zan_type_t *nvt = infer_expr_type(g, callee->member.object, locals);
                if (nvt && nvt->kind == TYPE_NULLABLE) return nvt->element_type;
            }
            if (mm.len == 5 && memcmp(mm.str, "ToStr", 5) == 0) {
                zan_type_t *bot = infer_expr_type(g, callee->member.object, locals);
                if (bot && bot->kind == TYPE_ARRAY) return g->binder->type_string;
            }
            /* A558：Object 方法族推断——接收者是 object/接口/类且该类型
             * 自身（含基类链/接口）未声明同名成员时：Equals(1)→bool、
             * GetHashCode(0)→int。ToString(0) 由下方通用启发式覆盖
             * （返回 string）。缺 Equals 覆写的声明时也不劫持：让普通
             * 推断路径先走。 */
            if (mm.len == 6 && memcmp(mm.str, "Equals", 6) == 0 &&
                e->call.args.count == 1) {
                zan_type_t *eot = infer_expr_type(g, callee->member.object, locals);
                if (eot && (eot->kind == TYPE_OBJECT || eot->kind == TYPE_INTERFACE ||
                            eot->kind == TYPE_CLASS)) {
                    zan_symbol_t *osym = eot->sym;
                    bool declared = osym && resolve_overload(osym, mm, 1, 0) != NULL;
                    if (eot->kind == TYPE_INTERFACE && osym)
                        declared = resolve_iface_overload(osym, mm, 1, 0) != NULL;
                    if (!declared) return g->binder->type_bool;
                }
            }
            if (mm.len == 11 && memcmp(mm.str, "GetHashCode", 11) == 0 &&
                e->call.args.count == 0) {
                zan_type_t *eot = infer_expr_type(g, callee->member.object, locals);
                if (eot && irgen_type_is_hashable_prim(eot)) {
                    return g->binder->type_int;
                }
                if (eot && (eot->kind == TYPE_OBJECT || eot->kind == TYPE_INTERFACE ||
                            eot->kind == TYPE_CLASS)) {
                    zan_symbol_t *osym = eot->sym;
                    bool declared = osym && resolve_overload(osym, mm, 0, 0) != NULL;
                    if (eot->kind == TYPE_INTERFACE && osym)
                        declared = resolve_iface_overload(osym, mm, 0, 0) != NULL;
                    if (!declared) return g->binder->type_int;
                }
            }
            /* A string member name alone is not a return-type contract:
             * user methods such as async bool Roles.Replace must reach their
             * declarations before async frame ownership is classified. */
            if ((mm.len == 9 && memcmp(mm.str, "Substring", 9) == 0) ||
                (mm.len == 4 && memcmp(mm.str, "Trim", 4) == 0) ||
                (mm.len == 7 && memcmp(mm.str, "Replace", 7) == 0) ||
                (mm.len == 7 && memcmp(mm.str, "ToUpper", 7) == 0) ||
                (mm.len == 7 && memcmp(mm.str, "ToLower", 7) == 0)) {
                zan_type_t *srt = infer_expr_type(g, callee->member.object, locals);
                if (srt && srt->kind == TYPE_STRING)
                    return g->binder->type_string;
            }
            if (mm.len == 5 && memcmp(mm.str, "Split", 5) == 0) {
                zan_type_t *ot = infer_expr_type(g, callee->member.object, locals);
                if (ot && ot->kind == TYPE_STRING)
                    return zan_binder_make_list_type(g->binder, g->binder->type_string);
            }
            /* string */
            if (callee->member.object->kind == AST_IDENTIFIER &&
                !local_find(locals, callee->member.object->ident.name)) {
                zan_istr_t on = callee->member.object->ident.name;
                int sr = on.len == 6 && (memcmp(on.str, "string", 6) == 0 ||
                                         memcmp(on.str, "String", 6) == 0);
                if (sr && ((mm.len == 4 && memcmp(mm.str, "Join", 4) == 0) ||
                           (mm.len == 6 && memcmp(mm.str, "Format", 6) == 0)))
                    return g->binder->type_string;
                int cr = on.len == 4 && (memcmp(on.str, "char", 4) == 0 ||
                                         memcmp(on.str, "Char", 4) == 0);
                if (cr) {
                    if ((mm.len == 7 && memcmp(mm.str, "ToUpper", 7) == 0) ||
                        (mm.len == 7 && memcmp(mm.str, "ToLower", 7) == 0))
                        return g->binder->type_char;
                    if ((mm.len == 7 && memcmp(mm.str, "IsDigit", 7) == 0) ||
                        (mm.len == 8 && memcmp(mm.str, "IsLetter", 8) == 0) ||
                        (mm.len == 15 && memcmp(mm.str, "IsLetterOrDigit", 15) == 0) ||
                        (mm.len == 12 && memcmp(mm.str, "IsWhiteSpace", 12) == 0) ||
                        (mm.len == 7 && memcmp(mm.str, "IsUpper", 7) == 0) ||
                        (mm.len == 7 && memcmp(mm.str, "IsLower", 7) == 0))
                        return g->binder->type_bool;
                }
            }
        }
        if (callee->kind == AST_IDENTIFIER) {
            if (g->current_type_sym) {
            zan_symbol_t *m = resolve_overload_typed(g, g->current_type_sym,
                                                     callee->ident.name, e, locals);
            if (!m) m = get_method_sym(g->current_type_sym, callee->ident.name);
                if (m) {
                    zan_type_t *mt = method_ret_type_at(g, m, e, NULL, locals);
                    if (mt && g->cur_mtps)
                        mt = subst_method_tp(g, mt, g->cur_mtps, g->cur_mbind);
                    if (mt && mt->kind == TYPE_TYPE_PARAM) mt = concretize(g, mt);
                    return mt;
                }
            }
            zan_symbol_t *gf = zan_binder_lookup(g->binder, callee->ident.name);
            if (gf && gf->kind == SYM_METHOD) {
                zan_type_t *mt = method_ret_type_at(g, gf, e, NULL, locals);
                if (mt && g->cur_mtps)
                    mt = subst_method_tp(g, mt, g->cur_mtps, g->cur_mbind);
                if (mt && mt->kind == TYPE_TYPE_PARAM) mt = concretize(g, mt);
                return mt;
            }
            return NULL;
        }
        if (callee->kind == AST_MEMBER_ACCESS) {
            zan_ast_node_t *obj = callee->member.object;
            /* static: ClassName */
            if (obj->kind == AST_IDENTIFIER && !local_find(locals, obj->ident.name)) {
                zan_type_t *tp_ct = resolve_tp_ident_type(g, obj);
                zan_symbol_t *ts = (tp_ct && tp_ct->sym &&
                    (tp_ct->kind == TYPE_CLASS || tp_ct->kind == TYPE_STRUCT))
                    ? tp_ct->sym : zan_binder_lookup(g->binder, obj->ident.name);
                if (ts && (ts->kind == SYM_CLASS || ts->kind == SYM_STRUCT)) {
                    zan_symbol_t *m = resolve_overload_typed(g, ts, callee->member.name, e, locals);
                    if (!m) m = get_method_sym(ts, callee->member.name);
                    if (m) {
                        zan_type_t *gr = method_ret_type_at(g, m, e, NULL, locals);
                        if (!gr) gr = m->type;
                        zan_type_t *sinst = static_access_inst(g, obj);
                        if (!sinst)
                            sinst = infer_generic_static_recv_inst(g, ts, m, e, locals);
                        if (gr && sinst) gr = subst_type_param_deep(g, gr, sinst);
                        if (gr && g->cur_mtps)
                            gr = subst_method_tp(g, gr, g->cur_mtps, g->cur_mbind);
                        if (gr && gr->kind == TYPE_TYPE_PARAM) gr = concretize(g, gr);
                        return gr;
                    }
                }
            }
            if (obj->kind == AST_IDENTIFIER && !local_find(locals, obj->ident.name)) {
                char cls[64];
                int cn = (int)obj->ident.name.len;
                if (cn > 0 && cn < (int)sizeof(cls)) {
                    memcpy(cls, obj->ident.name.str, (size_t)cn);
                    cls[cn] = 0;
                    const zan_builtin_type_t *bt = zan_builtin_find(cls);
                    const char *bcls = cls;
                    if (bt && !bt->is_static) {
                        /* `string.IsNullOrEmpty(x)` is a member of the static
                         * String class, not of the instance string type */
                        const char *alias = builtin_static_class_for(cls);
                        if (alias) {
                            const zan_builtin_type_t *abt = zan_builtin_find(alias);
                            if (abt && abt->is_static) { bt = abt; bcls = alias; }
                        }
                    }
                    const char *res = bt && bt->is_static
                        ? zan_builtin_member_result(bcls, callee->member.name.str,
                                                    (int)callee->member.name.len)
                        : NULL;
                    if (bt && strcmp(bcls, "Math") == 0 &&
                        callee->member.name.len == 3 && e->call.args.count == 2 &&
                        (memcmp(callee->member.name.str, "Min", 3) == 0 ||
                         memcmp(callee->member.name.str, "Max", 3) == 0)) {
                        res = zan_builtin_minmax_result(bcls,
                            callee->member.name.str, 3,
                            infer_expr_type(g, e->call.args.items[0], locals),
                            infer_expr_type(g, e->call.args.items[1], locals));
                    }
                    if (bt && strcmp(bcls, "Math") == 0 &&
                        callee->member.name.len == 3 && e->call.args.count == 1 &&
                        memcmp(callee->member.name.str, "Abs", 3) == 0) {
                        zan_type_t *at0 = infer_expr_type(g, e->call.args.items[0], locals);
                        if (at0 && at0->kind == TYPE_DOUBLE) return g->binder->type_double;
                        if (at0 && at0->kind == TYPE_FLOAT) return g->binder->type_float;
                        if (at0 && at0->kind == TYPE_LONG) return g->binder->type_long;
                        if (at0 && at0->kind == TYPE_INT) return g->binder->type_int;
                    }
                    if (bt && strcmp(bcls, "Math") == 0 &&
                        callee->member.name.len == 5 && e->call.args.count == 3 &&
                        memcmp(callee->member.name.str, "Clamp", 5) == 0) {
                        zan_type_t *at0 = infer_expr_type(g, e->call.args.items[0], locals);
                        zan_type_t *at1 = infer_expr_type(g, e->call.args.items[1], locals);
                        zan_type_t *at2 = infer_expr_type(g, e->call.args.items[2], locals);
                        const char *r1 = zan_builtin_minmax_result("Math", "Max", 3, at0, at1);
                        if (r1) {
                            zan_type_t *mid = strcmp(r1, "float") == 0 ? g->binder->type_float
                                            : strcmp(r1, "double") == 0 ? g->binder->type_double
                                            : strcmp(r1, "long") == 0 ? g->binder->type_long
                                            : strcmp(r1, "ulong") == 0 ? g->binder->type_ulong
                                            : strcmp(r1, "uint") == 0 ? g->binder->type_uint
                                            : g->binder->type_int;
                            res = zan_builtin_minmax_result("Math", "Min", 3, mid, at2);
                        }
                    }
                    if (bt && strcmp(bcls, "Math") == 0 &&
                        callee->member.name.len == 16 && e->call.args.count == 3 &&
                        memcmp(callee->member.name.str, "FusedMultiplyAdd", 16) == 0) {
                        zan_type_t *at0 = infer_expr_type(g, e->call.args.items[0], locals);
                        zan_type_t *at1 = infer_expr_type(g, e->call.args.items[1], locals);
                        zan_type_t *at2 = infer_expr_type(g, e->call.args.items[2], locals);
                        if (at0 && at1 && at2 && at0->kind == TYPE_FLOAT &&
                            at1->kind == TYPE_FLOAT && at2->kind == TYPE_FLOAT)
                            return g->binder->type_float;
                        return g->binder->type_double;
                    }
                    if (bt && strcmp(bcls, "Parallel") == 0) {
                        return g->binder->type_void;
                    }
                    if (bt && strcmp(bcls, "MathF") == 0) {
                        return g->binder->type_float;
                    }
                    if (res) {
                        if (strcmp(res, "string") == 0) return g->binder->type_string;
                        if (strcmp(res, "int") == 0) return g->binder->type_int;
                        if (strcmp(res, "long") == 0) return g->binder->type_long;
                        if (strcmp(res, "uint") == 0) return g->binder->type_uint;
                        if (strcmp(res, "ulong") == 0) return g->binder->type_ulong;
                        if (strcmp(res, "nint") == 0) return g->binder->type_nint;
                        if (strcmp(res, "float") == 0) return g->binder->type_float;
                        if (strcmp(res, "double") == 0) return g->binder->type_double;
                        if (strcmp(res, "bool") == 0) return g->binder->type_bool;
                        if (strcmp(res, "char") == 0) return g->binder->type_char;
                        if (strcmp(res, "List<string>") == 0)
                            return zan_binder_make_list_type(g->binder,
                                                             g->binder->type_string);
                    }
                    /* A529：标量实例类型上的 native 静态成员（int.Parse 等）
                     * 曾推断为 NULL——`int.Parse("9").ToString()` 断在
                     * "could not be resolved"（var x = int.Parse(...) 与
                     * 比较运算不吃 infer 才侥幸能用）。返回类型就是标量
                     * 本身，与 irgen_call.c 的原生分派点同口径。 */
                    if (bt && !bt->is_static &&
                        zan_builtin_has_native_member(cls, cn,
                            callee->member.name.str,
                            (int)callee->member.name.len)) {
                        if (strcmp(cls, "int") == 0) return g->binder->type_int;
                        if (strcmp(cls, "long") == 0) return g->binder->type_long;
                        if (strcmp(cls, "double") == 0) return g->binder->type_double;
                        if (strcmp(cls, "float") == 0) return g->binder->type_float;
                    }
                }
            }
            /* static: Namespace */
            if (obj->kind == AST_MEMBER_ACCESS && is_name_path(obj)) {
                zan_ast_node_t *head = name_path_head(obj);
                if (head && !local_find(locals, head->ident.name)) {
                    zan_symbol_t *ts = zan_binder_lookup(g->binder, obj->member.name);
                    if (ts && (ts->kind == SYM_CLASS || ts->kind == SYM_STRUCT) &&
                        qpath_ns_ok(g, obj, ts)) {
                        zan_symbol_t *m = get_method_sym(ts, callee->member.name);
                        if (m) {
                            zan_type_t *gr = method_ret_type_at(g, m, e, NULL, locals);
                            return gr ? gr : m->type;
                        }
                    }
                }
            }
            /* instance: <expr> */
            zan_type_t *rt = infer_expr_type(g, obj, locals);
            if (rt && rt->sym) {
                zan_symbol_t *m = resolve_overload_typed(g, rt->sym,
                    callee->member.name, e, locals);
                if (!m) m = get_method_sym(rt->sym, callee->member.name);
                if (m) {
                    zan_type_t *mt = method_ret_type_at(g, m, e, obj, locals);
                    if (!mt) mt = m->type;
                    mt = subst_type_param_deep(g, mt, rt);
                    if (mt && mt->kind == TYPE_TYPE_PARAM) mt = concretize(g, mt);
                    return mt;
                }
            }
            /* a built-in type (List<T>, string, Dictionary) has no symbol:
             * its members' result types come from the built-in table */
            if (rt) {
                zan_type_t *bt = builtin_member_result_type(g, rt,
                    callee->member.name.str, (int)callee->member.name.len);
                if (bt) return bt;
            }
            zan_symbol_t *xm = find_extension_method(g, rt, callee->member.name,
                                                     e->call.args.count,
                                                     e, obj, locals);
            if (xm) return method_ret_type_at(g, xm, e, obj, locals);
            /* Object/scalar ToString remains available without a declaration;
             * declared static/instance methods above always determine their
             * own return type, including non-string user ToString overloads. */
            if (rt && callee->member.name.len == 8 &&
                memcmp(callee->member.name.str, "ToString", 8) == 0)
                return g->binder->type_string;
        }
        return NULL;
    }
    case AST_NEW_EXPR:
        if (e->new_expr.is_array) {
            if (!e->new_expr.type) return NULL;
            zan_type_t *at = resolve_type_ctx(g, e->new_expr.type);
            if (at && at->kind == TYPE_ARRAY) return at;
            return NULL;
        }
        /* `FactoryCall( */
        if (!e->new_expr.type && e->new_expr.call_init)
            return infer_expr_type(g, e->new_expr.call_init, locals);
        return resolve_type_ctx(g, e->new_expr.type);
    case AST_COLL_INIT:
        return NULL;
    case AST_UNARY:
        if (e->unary.op == TK_BANG)
            return g->binder ? g->binder->type_bool : NULL;
        return infer_expr_type(g, e->unary.operand, locals);
    case AST_POSTFIX_UNARY:
        if (e->unary.op == TK_QUESTION) {
            zan_type_t *ot = concretize(g, infer_expr_type(g, e->unary.operand, locals));
            if (ot && ot->type_arg_count >= 1)
                return ot->type_args[0];
        }
        return infer_expr_type(g, e->unary.operand, locals);
    case AST_BINARY:
        if (e->binary.op == TK_PLUS && is_string_expr(g, e, locals))
            return g->binder->type_string;
        {
            zan_type_t *vlt = infer_expr_type(g, e->binary.left, locals);
            if (is_vec_type(vlt)) return vlt;
        }
        /* 运算符重载：先解析重载方法并返回它的返回类型。
         *
         * 不做这一步的话，`a * s + b`（重载结果再参与下一次运算）这种
         * 表达式的类型推不出来——左操作数是"结构体 × 标量"，左右类型
         * 不同 kinds，下面的数值分支会返回 NULL；上层拿不到类型就跳过
         * 重载分派，代码生成退化成整数加法，LLVM 校验直接失败
         * （"Integer arithmetic operators only work with integral types"）。 */
        {
            const char *op_name = NULL;
            switch (e->binary.op) {
            case TK_PLUS:       op_name = "op_add"; break;
            case TK_MINUS:      op_name = "op_sub"; break;
            case TK_STAR:       op_name = "op_mul"; break;
            case TK_SLASH:      op_name = "op_div"; break;
            case TK_PERCENT:    op_name = "op_mod"; break;
            case TK_EQ_EQ:      op_name = "op_eq"; break;
            case TK_BANG_EQ:    op_name = "op_neq"; break;
            case TK_LESS:       op_name = "op_lt"; break;
            case TK_GREATER:    op_name = "op_gt"; break;
            case TK_LESS_EQ:    op_name = "op_le"; break;
            case TK_GREATER_EQ: op_name = "op_ge"; break;
            default: break;
            }
            if (op_name) {
                zan_type_t *lt = infer_expr_type(g, e->binary.left, locals);
                if (lt && lt->sym &&
                    (lt->kind == TYPE_CLASS || lt->kind == TYPE_STRUCT)) {
                    zan_istr_t op_istr = { (char *)op_name, (int)strlen(op_name) };
                    zan_ast_node_t *op_probe = zan_ast_new(g->arena, AST_CALL,
                                                           e->loc);
                    op_probe->call.callee = NULL;
                    zan_ast_list_init(&op_probe->call.args);
                    zan_ast_list_init(&op_probe->call.type_args);
                    zan_ast_list_push(&op_probe->call.args, e->binary.right,
                                      g->arena);
                    zan_symbol_t *op = resolve_op_overload(g, lt->sym, op_istr,
                                                           op_probe, locals);
                    if (op) return op->type;
                }
            }
        }
        switch (e->binary.op) {
        case TK_PLUS: case TK_MINUS: case TK_STAR: case TK_SLASH:
        case TK_PERCENT: case TK_AMP: case TK_PIPE: case TK_CARET:
        case TK_LESS_LESS: case TK_GREATER_GREATER:
        case TK_GREATER_GREATER_GREATER: {
            zan_type_t *lt = infer_expr_type(g, e->binary.left, locals);
            zan_type_t *rt = infer_expr_type(g, e->binary.right, locals);
            if (lt && rt && (lt->kind == TYPE_NULLABLE || rt->kind == TYPE_NULLABLE)) {
                zan_type_t *lb = lt->kind == TYPE_NULLABLE ? lt->element_type : lt;
                zan_type_t *rb = rt->kind == TYPE_NULLABLE ? rt->element_type : rt;
                if (lb && rb && (lb->kind == TYPE_DECIMAL || rb->kind == TYPE_DECIMAL)) {
                    zan_type_t *promoted = irgen_promote_pair(g, lb, rb);
                    if (promoted) return zan_binder_make_nullable_type(g->binder, promoted);
                }
            }
            if (lt && lt->kind == TYPE_NULLABLE) return lt;
            if (rt && rt->kind == TYPE_NULLABLE) return rt;
            if ((lt && lt->kind == TYPE_ULONG) || (rt && rt->kind == TYPE_ULONG))
                return g->binder->type_ulong;
            if (lt && rt && lt->kind == rt->kind) return lt;
            if (lt && rt) {
                /* A510 收窄的前置：混合位宽数值要按 C# 提升定型
                 * （long+int → long）。曾返回 NULL——发射端 coerce_int_pair
                 * 会把臂对齐到 64 位所以数值碰巧没错，但定型为 NULL 后外层
                 * 表达式（如 `(long子式)/int`）会取另一臂的 int 当整个表达
                 * 式的类型；A510 的 32 位收窄按这个错误定型截断 long 臂，
                 * 2147483648 被截回 -2147483648（numdig/Encoding.zan 一线）。
                 * 用 A514 同一条 irgen_promote_pair。 */
                zan_type_t *m = irgen_promote_pair(g, lt, rt);
                if (m) return m;
            }
            if (lt && !rt) return lt;
            if (rt && !lt) return rt;
            return NULL;
        }
        case TK_EQ_EQ: case TK_BANG_EQ: case TK_LESS:
        case TK_LESS_EQ: case TK_GREATER: case TK_GREATER_EQ:
        case TK_AMP_AMP: case TK_PIPE_PIPE:
            return g->binder ? g->binder->type_bool : NULL;
        case TK_QUESTION_QUESTION: {
            zan_type_t *lt = infer_expr_type(g, e->binary.left, locals);
            zan_type_t *rt = infer_expr_type(g, e->binary.right, locals);
            if (lt && lt->kind == TYPE_NULLABLE && lt->element_type) {
                if (rt && rt->kind == TYPE_NULLABLE) return rt;
                return lt->element_type;
            }
            if (!lt) return rt;
            if (!rt) return lt;
            if (lt == rt) return lt;
            zan_type_t *m = irgen_promote_pair(g, lt, rt);
            if (m) return m;
            return lt;
        }
        default:
            return NULL;
        }
    case AST_TYPEOF_EXPR:
        return g->binder->type_typeinfo;
    case AST_STRING_INTERP:
        return g->binder->type_string;
    case AST_CAST_EXPR:
        return resolve_type_ctx(g, e->cast.type);
    case AST_STACKALLOC_EXPR: {
        zan_type_t *et = resolve_type_ctx(g, e->cast.type);
        return et ? zan_binder_make_span_type(g->binder, et) : NULL;
    }
    case AST_CONDITIONAL: {
        /* 两支提升合一定型（C# ?: 取公共提升型），不是 then 臂——
         * infer 与家族合并（expr_family）同口径，TASKS.md A514。 */
        zan_type_t *lt = infer_expr_type(g, e->conditional.then_expr, locals);
        zan_type_t *rt = infer_expr_type(g, e->conditional.else_expr, locals);
        if (!lt) return rt;
        if (!rt) return lt;
        if (lt == rt) return lt;
        zan_type_t *m = irgen_promote_pair(g, lt, rt);
        if (m) return m;
        return lt; /* 非数值混合维持旧行为（then 臂） */
    }
    case AST_TUPLE_EXPR: {
        /* (a, b, */
        if (!g->binder) return NULL;
        int n = e->tuple_expr.items.count;
        zan_type_t **elems = (zan_type_t **)zan_arena_alloc(
            g->arena, sizeof(zan_type_t *) * (size_t)(n > 0 ? n : 1));
        for (int i = 0; i < n; i++) {
            elems[i] = infer_expr_type(g, e->tuple_expr.items.items[i], locals);
            if (!elems[i]) return NULL;
        }
        return zan_binder_make_tuple_type(g->binder, elems, n);
    }
    case AST_SWITCH_EXPR: {
        if (e->switch_expr.arms.count == 0) return NULL;
        zan_ast_node_t *arm = e->switch_expr.arms.items[0];
        local_scope_t empty;
        if (!locals) {
            local_scope_init(&empty, g->arena);
            locals = &empty;
        }
        int mark = locals->count;
        if (arm->switch_arm.type_pattern && arm->switch_arm.var_name.len) {
            zan_type_t *pattern = resolve_type_ctx(g, arm->switch_arm.type_pattern);
            local_add(locals, arm->switch_arm.var_name, NULL, pattern);
        } else {
            zan_type_t *dty = concretize(g, infer_expr_type(g, e->switch_expr.expr, locals));
            if (dty && dty->kind == TYPE_STRUCT && dty->sym && dty->sym->decl &&
                dty->sym->decl->kind == AST_STRUCT_DECL &&
                dty->sym->decl->meta && dty->sym->decl->meta->adt_info) {
                zan_adt_info_t *ainfo = dty->sym->decl->meta->adt_info;
                zan_istr_t vname = {0};
                zan_ast_list_t *binds = NULL;
                if (arm->switch_arm.adt_pat) {
                    vname = arm->switch_arm.adt_pat->variant;
                    binds = &arm->switch_arm.adt_pat->bindings;
                } else if (arm->switch_arm.pattern &&
                           arm->switch_arm.pattern->kind == AST_CALL) {
                    zan_ast_node_t *cal = arm->switch_arm.pattern->call.callee;
                    if (cal && cal->kind == AST_MEMBER_ACCESS)
                        vname = cal->member.name;
                    else if (cal && cal->kind == AST_IDENTIFIER)
                        vname = cal->ident.name;
                    binds = &arm->switch_arm.pattern->call.args;
                }
                if (vname.len > 0 && binds) {
                    for (int vi = 0; vi < ainfo->variant_count; vi++) {
                        zan_adt_variant_t *av = &ainfo->variants[vi];
                        if (av->name.len == vname.len &&
                            memcmp(av->name.str, vname.str, (size_t)vname.len) == 0) {
                            for (int bi = 0; bi < binds->count && bi < av->params.count; bi++) {
                                zan_ast_node_t *bn = binds->items[bi];
                                if (!bn || bn->kind != AST_IDENTIFIER) continue;
                                if (bn->ident.name.len == 1 && bn->ident.name.str[0] == '_')
                                    continue;
                                zan_symbol_t *fs = get_field_sym(dty->sym, av->struct_field_names[bi]);
                                zan_type_t *fty = fs ? field_type_at(g, fs, dty) : NULL;
                                if (fty) local_add(locals, bn->ident.name, NULL, fty);
                            }
                            break;
                        }
                    }
                }
            }
        }
        zan_type_t *result = infer_expr_type(g, arm->switch_arm.result, locals);
        locals->count = mark;
        return result;
    }
    case AST_WITH_EXPR:
        return infer_expr_type(g, e->with_expr.expr, locals);
    default:
        return NULL;
    }
}
