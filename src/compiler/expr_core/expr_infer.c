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
 * double > float > ulong > long > int，decimal 只与整数合。曾按 then 臂
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
        local_var_t *l = local_find(locals, e->ident.name);
        if (l) return l->type;
        if (g->current_type_sym) {
            zan_symbol_t *fs = get_field_sym(g->current_type_sym, e->ident.name);
            /* `item` (implicit `this */
            if (fs) return concretize(g, fs->type);
        }
        return NULL;
    }
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
            zan_symbol_t *cs = zan_binder_lookup(g->binder,
                                                 e->member.object->ident.name);
            if (cs && (cs->kind == SYM_CLASS || cs->kind == SYM_STRUCT)) {
                zan_symbol_t *fs = get_field_sym(cs, e->member.name);
                if (fs) return fs->type;
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
        if (is_span_type(ot) && e->member.name.len == 6 &&
            memcmp(e->member.name.str, "Length", 6) == 0)
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
                if (aot && aot->kind == TYPE_STRING)
                    return zan_binder_make_readonly_span_type(g->binder, g->binder->type_char);

                if (is_span_type(aot)) return aot;
            }
            if (mm.len == 5 && memcmp(mm.str, "Slice", 5) == 0) {
                zan_type_t *sot = infer_expr_type(g, callee->member.object, locals);
                if (is_span_type(sot)) return sot;
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
                if (eot && (eot->kind == TYPE_OBJECT || eot->kind == TYPE_INTERFACE ||
                            eot->kind == TYPE_CLASS)) {
                    zan_symbol_t *osym = eot->sym;
                    bool declared = osym && resolve_overload(osym, mm, 0, 0) != NULL;
                    if (eot->kind == TYPE_INTERFACE && osym)
                        declared = resolve_iface_overload(osym, mm, 0, 0) != NULL;
                    if (!declared) return g->binder->type_int;
                }
            }
            /* string 实例方法启发式——char/Char 的静态类（char.ToUpper 等，
             * A525）成员名与之相撞（ToUpper/ToLower），先跳过让它落到
             * 下面的静态内建分支按 Char 表解析。 */
            int recv_is_char_cls = callee->member.object->kind == AST_IDENTIFIER &&
                callee->member.object->ident.name.len == 4 &&
                (memcmp(callee->member.object->ident.name.str, "char", 4) == 0 ||
                 memcmp(callee->member.object->ident.name.str, "Char", 4) == 0);
            if (!recv_is_char_cls &&
                ((mm.len == 9 && memcmp(mm.str, "Substring", 9) == 0) ||
                (mm.len == 8 && memcmp(mm.str, "ToString", 8) == 0) ||
                (mm.len == 4 && memcmp(mm.str, "Trim", 4) == 0) ||
                (mm.len == 7 && memcmp(mm.str, "Replace", 7) == 0) ||
                (mm.len == 7 && memcmp(mm.str, "ToUpper", 7) == 0) ||
                (mm.len == 7 && memcmp(mm.str, "ToLower", 7) == 0)))
                return g->binder->type_string;
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
            }
        }
        if (callee->kind == AST_IDENTIFIER) {
            if (g->current_type_sym) {
            zan_symbol_t *m = resolve_overload_typed(g, g->current_type_sym,
                                                     callee->ident.name, e, locals);
            if (!m) m = get_method_sym(g->current_type_sym, callee->ident.name);
                if (m) return method_ret_type_at(g, m, e, NULL, locals);
            }
            zan_symbol_t *gf = zan_binder_lookup(g->binder, callee->ident.name);
            if (gf && gf->kind == SYM_METHOD)
                return method_ret_type_at(g, gf, e, NULL, locals);
            return NULL;
        }
        if (callee->kind == AST_MEMBER_ACCESS) {
            zan_ast_node_t *obj = callee->member.object;
            /* static: ClassName */
            if (obj->kind == AST_IDENTIFIER && !local_find(locals, obj->ident.name)) {
                zan_symbol_t *ts = zan_binder_lookup(g->binder, obj->ident.name);
                if (ts && (ts->kind == SYM_CLASS || ts->kind == SYM_STRUCT)) {
                    zan_symbol_t *m = resolve_overload_typed(g, ts, callee->member.name, e, locals);
                    if (!m) m = get_method_sym(ts, callee->member.name);
                    if (m) {
                        zan_type_t *gr = method_ret_type_at(g, m, e, NULL, locals);
                        return gr ? gr : m->type;
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
                    if (res) {
                        if (strcmp(res, "string") == 0) return g->binder->type_string;
                        if (strcmp(res, "int") == 0) return g->binder->type_int;
                        if (strcmp(res, "long") == 0) return g->binder->type_long;
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
        return infer_expr_type(g, e->unary.operand, locals);
    case AST_BINARY:
        if (e->binary.op == TK_PLUS && is_string_expr(g, e, locals))
            return g->binder->type_string;
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
    case AST_SWITCH_EXPR:
        /* `x switch { */
        if (e->switch_expr.arms.count == 0) return NULL;
        return infer_expr_type(g, e->switch_expr.arms.items[0]->switch_arm.result,
                               locals);
    case AST_WITH_EXPR:
        return infer_expr_type(g, e->with_expr.expr, locals);
    default:
        return NULL;
    }
}
