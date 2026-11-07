static zan_sym_kind_t ast_kind_to_sym_kind(zan_ast_kind_t kind) {
    switch (kind) {
    case AST_CLASS_DECL:     return SYM_CLASS;
    case AST_STRUCT_DECL:    return SYM_STRUCT;
    case AST_INTERFACE_DECL: return SYM_INTERFACE;
    case AST_ENUM_DECL:      return SYM_ENUM;
    case AST_DELEGATE_DECL:  return SYM_DELEGATE;
    default:                 return SYM_CLASS;
    }
}

static zan_type_kind_t ast_kind_to_type_kind(zan_ast_kind_t kind) {
    switch (kind) {
    case AST_CLASS_DECL:     return TYPE_CLASS;
    case AST_STRUCT_DECL:    return TYPE_STRUCT;
    case AST_INTERFACE_DECL: return TYPE_INTERFACE;
    case AST_ENUM_DECL:      return TYPE_ENUM;
    case AST_DELEGATE_DECL:  return TYPE_DELEGATE;
    default:                 return TYPE_CLASS;
    }
}

/* A551：resolve_type 里这些名字**硬编码短路**到内建类型，永远轮不到 scope
 * 查找——用户声明同名类型会被静默顶掉，成员解析落空报"has no member"把人
 * 引向无关方向。在声明处直接报重名。名单必须与 resolve_type 的内建短路表
 * 保持一致；例外：Func/Action 允许被同名 delegate 遮蔽（resolve_type 优先
 * 取用户声明，C# 语义），delegate 声明不算碰撞。 */
static bool builtin_type_name(zan_istr_t name) {
    if (istr_eq(name, "void", 4) || istr_eq(name, "bool", 4) ||
        istr_eq(name, "byte", 4) || istr_eq(name, "short", 5) ||
        istr_eq(name, "int", 3) || istr_eq(name, "long", 4) ||
        istr_eq(name, "sbyte", 5) || istr_eq(name, "ushort", 6) ||
        istr_eq(name, "uint", 4) || istr_eq(name, "ulong", 5) ||
        istr_eq(name, "float", 5) || istr_eq(name, "double", 6) ||
        istr_eq(name, "decimal", 7) || istr_eq(name, "char", 4) ||
        istr_eq(name, "string", 6) || istr_eq(name, "object", 6) ||
        istr_eq(name, "nint", 4) || istr_eq(name, "List", 4) ||
        istr_eq(name, "Dict", 4) || istr_eq(name, "Dictionary", 10) ||
        istr_eq(name, "StringBuilder", 13) || istr_eq(name, "Span", 4) ||
        istr_eq(name, "ReadOnlySpan", 12) || istr_eq(name, "TypeInfo", 8) ||
        istr_eq(name, "Task", 4) || istr_eq(name, "Func", 4) ||
        istr_eq(name, "Action", 6))
        return true;
    return false;
}

static void bind_type_decls(zan_binder_t *b, zan_ast_list_t *decls) {
    zan_symbol_t **syms = (zan_symbol_t **)zan_arena_alloc(
        b->arena, sizeof(zan_symbol_t *) *
                      (size_t)(decls->count > 0 ? decls->count : 1));
    for (int i = 0; i < decls->count; i++) {
        syms[i] = NULL;
        zan_ast_node_t *node = decls->items[i];

        if (node->kind == AST_DELEGATE_DECL) {
            zan_istr_t name = node->method_decl.name;
            /* Func/Action 可被同名 delegate 遮蔽（resolve_type 会优先取
             * 用户声明，C# 语义）；其余内建名一律拦。 */
            bool shadowable_delegate = istr_eq(name, "Func", 4) ||
                                       istr_eq(name, "Action", 6);
            if (builtin_type_name(name) && !shadowable_delegate) {
                zan_diag_emit_code(b->diag, DIAG_ERROR, node->loc, 1009,
                              "type declaration '%.*s' collides with a built-in type name",
                              name.len, name.str);
                continue;
            }
            zan_symbol_t *existing = scope_find(b->current_scope, name);
            if (existing) {
                zan_diag_emit_code(b->diag, DIAG_ERROR, node->loc, 1010,
                              "duplicate type declaration '%.*s'", name.len, name.str);
                continue;
            }
            zan_type_t *type = make_type(b->arena, TYPE_DELEGATE, name.str, name.len);
            zan_symbol_t *sym = make_symbol(b->arena, SYM_DELEGATE,
                name, type, node, node->method_decl.modifiers);
            type->sym = sym;
            scope_add(b->arena, b->current_scope, sym);
            continue;
        }

        if (node->kind == AST_CLASS_DECL || node->kind == AST_STRUCT_DECL ||
            node->kind == AST_INTERFACE_DECL || node->kind == AST_ENUM_DECL) {

            zan_istr_t name = node->type_decl.name;

            if (builtin_type_name(name)) {
                zan_diag_emit_code(b->diag, DIAG_ERROR, node->loc, 1009,
                              "type declaration '%.*s' collides with a built-in type name",
                              name.len, name.str);
                continue;
            }

            zan_symbol_t *existing = scope_find(b->current_scope, name);
            if (existing) {
                zan_diag_emit_code(b->diag, DIAG_ERROR, node->loc, 1010,
                              "duplicate type declaration '%.*s'", name.len, name.str);
                continue;
            }

            zan_type_t *type = make_type(b->arena,
                ast_kind_to_type_kind(node->kind), name.str, name.len);
            zan_symbol_t *sym = make_symbol(b->arena,
                ast_kind_to_sym_kind(node->kind),
                name, type, node, node->type_decl.modifiers);
            type->sym = sym;
            type->base_type = NULL;

            scope_add(b->arena, b->current_scope, sym);
            syms[i] = sym;
        }
    }

    for (int i = 0; i < decls->count; i++) {
        zan_ast_node_t *node = decls->items[i];
        if (node->kind != AST_CLASS_DECL && node->kind != AST_STRUCT_DECL &&
            node->kind != AST_INTERFACE_DECL && node->kind != AST_ENUM_DECL)
            continue;
        zan_ast_node_t *host = node->type_decl.nested_host;
        if (!host) continue;
        zan_symbol_t *sym = syms[i];
        zan_symbol_t *host_sym = NULL;
        for (int j = 0; j < decls->count; j++) {
            if (decls->items[j] == host) { host_sym = syms[j]; break; }
        }
        if (sym && host_sym) symbol_add_member(b->arena, host_sym, sym);
    }

    for (int i = 0; i < decls->count; i++) {
        zan_ast_node_t *node = decls->items[i];
        if (node->kind != AST_DELEGATE_DECL) continue;

        zan_symbol_t *sym = scope_find(b->current_scope, node->method_decl.name);
        if (!sym || sym->decl != node) continue;
        zan_type_t *type = sym->type;

        register_type_param_list(b, &node->method_decl.type_params);
        type->delegate_is_async = (node->method_decl.modifiers & MOD_ASYNC) != 0;
        type->delegate_ret_type = node->method_decl.return_type
            ? zan_binder_resolve_type(b, node->method_decl.return_type)
            : b->type_void;
        int pc = node->method_decl.params.count;
        type->delegate_param_count = pc;
        if (pc > 0) {
            type->delegate_param_types = (zan_type_t **)zan_arena_alloc(
                b->arena, sizeof(zan_type_t *) * (size_t)pc);
            for (int j = 0; j < pc; j++) {
                zan_ast_node_t *param = node->method_decl.params.items[j];
                type->delegate_param_types[j] =
                    zan_binder_resolve_type(b, param->param.type);
            }
        } else {
            type->delegate_param_types = NULL;
        }
    }
}

static bool base_args_resolvable(zan_binder_t *b, zan_ast_node_t *type_ref) {
    if (!type_ref || type_ref->kind != AST_TYPE_REF) return false;
    zan_ast_list_t *args = &type_ref->type_ref.type_args;
    for (int i = 0; i < args->count; i++) {
        zan_ast_node_t *arg = args->items[i];
        if (!arg || arg->kind != AST_TYPE_REF) return false;
        zan_istr_t n = arg->type_ref.name;
        if (istr_eq(n, "void", 4) || istr_eq(n, "bool", 4) ||
            istr_eq(n, "byte", 4) || istr_eq(n, "short", 5) ||
            istr_eq(n, "int", 3) || istr_eq(n, "long", 4) ||
            istr_eq(n, "sbyte", 5) || istr_eq(n, "ushort", 6) ||
            istr_eq(n, "uint", 4) || istr_eq(n, "ulong", 5) ||
            istr_eq(n, "float", 5) || istr_eq(n, "double", 6) ||
            istr_eq(n, "decimal", 7) || istr_eq(n, "char", 4) ||
            istr_eq(n, "string", 6) || istr_eq(n, "object", 6) ||
            istr_eq(n, "nint", 4))
            continue;
        if (!scope_find(b->current_scope, n)) return false;
        if (!base_args_resolvable(b, arg)) return false;
    }
    return true;
}

static void resolve_bases(zan_binder_t *b, zan_ast_node_t *type_node) {
    zan_symbol_t *type_sym = scope_find(b->current_scope, type_node->type_decl.name);
    if (!type_sym || !type_sym->type || type_sym->decl != type_node) return;
    if (type_sym->type->bases_resolved == 2) return;   /* done */
    if (type_sym->type->bases_resolved == 1) {
        zan_diag_emit_code(b->diag, DIAG_ERROR, type_node->loc, 1011,
                      "cyclic inheritance involving '%.*s'",
                      (int)type_node->type_decl.name.len,
                      type_node->type_decl.name.str);
        return;
    }
    type_sym->type->bases_resolved = 1;

    if (type_node->type_decl.bases.count > 0) {
        int nbases = type_node->type_decl.bases.count;
        zan_type_t **ifaces = (zan_type_t **)zan_arena_alloc(
            b->arena, sizeof(zan_type_t *) * (size_t)nbases);
        int nif = 0;
        for (int bx = 0; bx < nbases; bx++) {
            zan_ast_node_t *base_ref = type_node->type_decl.bases.items[bx];
            if (base_ref->kind != AST_TYPE_REF) {
                zan_diag_emit_code(b->diag, DIAG_ERROR, base_ref->loc, 1012,
                              "invalid base type");
                continue;
            }
            zan_istr_t base_name = base_ref->type_ref.name;
            zan_symbol_t *base_sym = scope_find(b->current_scope, base_name);
            if (!base_sym) {
                zan_diag_emit_code(b->diag, DIAG_ERROR, base_ref->loc, 1013,
                              "undefined base type '%.*s'",
                              (int)base_name.len, base_name.str);
                continue;
            }
            if ((base_sym->kind == SYM_CLASS || base_sym->kind == SYM_STRUCT) &&
                !type_sym->type->base_type) {
                if ((base_sym->modifiers & MOD_SEALED) != 0) {
                    zan_diag_emit_code(b->diag, DIAG_ERROR, base_ref->loc, 1014,
                                  "cannot derive from sealed type '%.*s'",
                                  (int)base_name.len, base_name.str);
                    continue;
                }
                if (base_sym->decl &&
                    (base_sym->decl->kind == AST_CLASS_DECL ||
                     base_sym->decl->kind == AST_STRUCT_DECL)) {
                    resolve_bases(b, base_sym->decl);
                }
                if (base_sym->type->bases_resolved != 2) continue;
                if (base_args_resolvable(b, base_ref)) {
                    zan_type_t *inst = zan_binder_resolve_type(b, base_ref);
                    if (inst && (inst->kind == TYPE_CLASS || inst->kind == TYPE_STRUCT) &&
                        inst != b->type_error) {
                        type_sym->type->base_type = inst;
                    } else {
                        type_sym->type->base_type = base_sym->type;
                    }
                } else {
                    type_sym->type->base_type = base_sym->type;
                }
                int ninherit = 0;
                for (int bi = 0; bi < base_sym->member_count; bi++) {
                    if (base_sym->members[bi]->kind == SYM_FIELD ||
                        base_sym->members[bi]->kind == SYM_PROPERTY)
                        ninherit++;
                }
                if (ninherit > 0) {
                    int total = ninherit + type_sym->member_count;
                    zan_symbol_t **merged = (zan_symbol_t **)zan_arena_alloc(
                        b->arena, sizeof(zan_symbol_t *) * (size_t)total);
                    int mi = 0;
                    for (int bi = 0; bi < base_sym->member_count; bi++) {
                        zan_symbol_t *bm = base_sym->members[bi];
                        if (bm->kind != SYM_FIELD && bm->kind != SYM_PROPERTY)
                            continue;
                        /* 泛型基类的字段类型要按实参替换：Base<string>.value
                         * 是 string，不是基类声明里的 T。否则派生类里
                         * this.value 的类型是个悬空的类型参数，方法体里的
                         * 成员访问解析不出来，整个方法退化成空桩。 */
                        zan_symbol_t *copy = bm;
                        zan_type_t *bt = type_sym->type->base_type;
                        if (bt && bt->type_arg_count > 0 && bm->type &&
                            bm->type->kind == TYPE_TYPE_PARAM) {
                            zan_type_t *st = binder_subst_type_param(bm->type, bt);
                            if (st && st != bm->type) {
                                copy = make_symbol(b->arena, bm->kind, bm->name,
                                                   st, bm->decl, bm->modifiers);
                                copy->parent = type_sym;
                            }
                        }
                        merged[mi++] = copy;
                    }
                    for (int oi = 0; oi < type_sym->member_count; oi++) {
                        merged[mi++] = type_sym->members[oi];
                    }
                    type_sym->members = merged;
                    type_sym->member_count = total;
                    type_sym->member_cap = total;
                }
            } else if (base_sym->kind == SYM_INTERFACE) {
                if (base_sym->decl) resolve_bases(b, base_sym->decl);
                if (base_sym->type->bases_resolved != 2) continue;
                if (base_args_resolvable(b, base_ref)) {
                    zan_type_t *inst = zan_binder_resolve_type(b, base_ref);
                    if (inst && inst->kind == TYPE_INTERFACE &&
                        inst != b->type_error) {
                        ifaces[nif++] = inst;
                        continue;
                    }
                }
                ifaces[nif++] = base_sym->type;
            }
        }
        if (nif > 0) {
            type_sym->type->interfaces = ifaces;
            type_sym->type->interface_count = nif;
        }
    }
    type_sym->type->bases_resolved = 2;
}

static bool binder_type_equal(zan_type_t *a, zan_type_t *b, int depth) {
    if (!a || !b) return a == b;
    if (a == b) return true;
    if (depth > 64 || a->kind != b->kind) return false;
    if (a->kind == TYPE_ARRAY || a->kind == TYPE_NULLABLE) {
        if (a->kind == TYPE_ARRAY && a->array_rank != b->array_rank)
            return false;
        return binder_type_equal(a->element_type, b->element_type, depth + 1);
    }
    if (a->kind == TYPE_DELEGATE) {
        if (a->delegate_is_async != b->delegate_is_async ||
            a->delegate_param_count != b->delegate_param_count ||
            !binder_type_equal(a->delegate_ret_type, b->delegate_ret_type, depth + 1))
            return false;
        for (int i = 0; i < a->delegate_param_count; i++)
            if (!binder_type_equal(a->delegate_param_types[i],
                                   b->delegate_param_types[i], depth + 1))
                return false;
        return true;
    }
    if (a->type_arg_count != b->type_arg_count) return false;
    if (a->sym && b->sym && a->sym != b->sym) return false;
    if ((!a->sym || !b->sym) &&
        (a->name.len != b->name.len ||
         memcmp(a->name.str, b->name.str, (size_t)a->name.len) != 0))
        return false;
    for (int i = 0; i < a->type_arg_count; i++)
        if (!binder_type_equal(a->type_args[i], b->type_args[i], depth + 1))
            return false;
    return true;
}

/* 类型兼容性判定：t 继承自 sup 或实现了 sup 接口 */
static bool binder_type_derives(zan_type_t *t, zan_type_t *sup, int depth) {
    if (!t || !sup || depth > 64) return false;
    if (t == sup) return true;
    if (t->sym && sup->sym && t->sym == sup->sym)
        return binder_type_equal(t, sup, 0);
    for (int i = 0; i < t->interface_count; i++)
        if (binder_type_derives(t->interfaces[i], sup, depth + 1))
            return true;
    if (t->base_type && t->base_type != t)
        return binder_type_derives(t->base_type, sup, depth + 1);
    return false;
}

static bool method_signature_equal(zan_binder_t *b, zan_ast_node_t *a,
                                   zan_ast_node_t *other,
                                   zan_ast_list_t *bind_tps,
                                   zan_type_t **bind_args) {
    if (!a || !other || a->kind != AST_METHOD_DECL ||
        other->kind != AST_METHOD_DECL ||
        a->method_decl.name.len != other->method_decl.name.len ||
        memcmp(a->method_decl.name.str, other->method_decl.name.str,
               (size_t)a->method_decl.name.len) != 0)
        return false;
    if (((a->method_decl.modifiers ^ other->method_decl.modifiers) &
         (MOD_STATIC | MOD_ASYNC)) != 0)
        return false;
    if (a->method_decl.params.count != other->method_decl.params.count)
        return false;
    zan_type_t *ar = a->method_decl.return_type
        ? zan_binder_resolve_type(b, a->method_decl.return_type) : b->type_void;
    zan_type_t *br = other->method_decl.return_type
        ? zan_binder_resolve_type(b, other->method_decl.return_type) : b->type_void;
    bool ret_subst = false;
    if (bind_tps && bind_tps->count > 0 && bind_args) {
        zan_type_t *sr = zan_binder_subst_named(b, br, bind_tps, bind_args);
        if (sr && sr != br) { br = sr; ret_subst = true; }
    }
    bool a_tp = ar && ar->kind == TYPE_TYPE_PARAM;
    bool b_tp = br && br->kind == TYPE_TYPE_PARAM && !ret_subst;
    if (!(a_tp || b_tp) && !binder_type_equal(ar, br, 0) &&
        !binder_type_derives(ar, br, 0))
        return false;
    for (int i = 0; i < a->method_decl.params.count; i++) {
        zan_ast_node_t *ap = a->method_decl.params.items[i];
        zan_ast_node_t *bp = other->method_decl.params.items[i];
        if (!ap || !bp || ap->kind != AST_PARAM || bp->kind != AST_PARAM ||
            ap->param.by_ref != bp->param.by_ref ||
            ap->param.is_params != bp->param.is_params ||
            ap->param.is_this != bp->param.is_this)
            return false;
        zan_type_t *at = zan_binder_resolve_type(b, ap->param.type);
        zan_type_t *bt = zan_binder_resolve_type(b, bp->param.type);
        bool param_subst = false;
        if (bind_tps && bind_tps->count > 0 && bind_args) {
            zan_type_t *st = zan_binder_subst_named(b, bt, bind_tps, bind_args);
            if (st && st != bt) { bt = st; param_subst = true; }
        }
        bool at_tp = at && at->kind == TYPE_TYPE_PARAM;
        bool bt_tp = bt && bt->kind == TYPE_TYPE_PARAM && !param_subst;
        if (!(at_tp || bt_tp) && !binder_type_equal(at, bt, 0)) return false;
    }
    return true;
}

static bool type_declares_method(zan_binder_t *b, zan_ast_node_t *type_node,
                                 zan_ast_node_t *wanted) {
    for (int i = 0; i < type_node->type_decl.members.count; i++) {
        zan_ast_node_t *m = type_node->type_decl.members.items[i];
        if (method_signature_equal(b, m, wanted, NULL, NULL)) return true;
    }
    return false;
}

/* 把类型参数替换成实例的类型实参（Base<string> 里的 T → string）。 */
static zan_type_t *binder_subst_type_param(zan_type_t *t, zan_type_t *recv) {
    if (!t || t->kind != TYPE_TYPE_PARAM || !recv || !recv->sym) return t;
    zan_ast_node_t *decl = recv->sym->decl;
    if (!decl) return t;
    zan_ast_list_t *tps = &decl->type_decl.type_params;
    for (int i = 0; i < tps->count && i < recv->type_arg_count; i++) {
        zan_ast_node_t *tp = tps->items[i];
        if (tp->kind != AST_IDENTIFIER) continue;
        if (tp->ident.name.len == t->name.len &&
            memcmp(tp->ident.name.str, t->name.str, (size_t)t->name.len) == 0)
            return recv->type_args[i];
    }
    return t;
}

static void inherit_default_methods(zan_binder_t *b, zan_ast_node_t *type_node,
                                    zan_ast_node_t *iface_node, int depth) {
    if (!iface_node || iface_node->kind != AST_INTERFACE_DECL || depth > 8) return;
    for (int i = 0; i < iface_node->type_decl.members.count; i++) {
        zan_ast_node_t *m = iface_node->type_decl.members.items[i];
        if (m->kind != AST_METHOD_DECL || !m->method_decl.body) continue;
        if ((m->method_decl.modifiers & MOD_STATIC) != 0) continue;
        if (type_declares_method(b, type_node, m)) continue;
        zan_ast_node_t *copy = zan_ast_new(b->arena, AST_METHOD_DECL, m->loc);
        *copy = *m;
        zan_ast_list_push(&type_node->type_decl.members, copy, b->arena);
    }
    for (int bx = 0; bx < iface_node->type_decl.bases.count; bx++) {
        zan_ast_node_t *bref = iface_node->type_decl.bases.items[bx];
        if (bref->kind != AST_TYPE_REF) continue;
        zan_symbol_t *bs = scope_find(b->current_scope, bref->type_ref.name);
        if (bs && bs->kind == SYM_INTERFACE && bs->decl)
            inherit_default_methods(b, type_node, bs->decl, depth + 1);
    }
}

static void bind_default_interface_methods(zan_binder_t *b, zan_ast_list_t *decls) {
    for (int i = 0; i < decls->count; i++) {
        zan_ast_node_t *decl = decls->items[i];
        if (decl->kind != AST_CLASS_DECL && decl->kind != AST_STRUCT_DECL) continue;
        for (int bx = 0; bx < decl->type_decl.bases.count; bx++) {
            zan_ast_node_t *bref = decl->type_decl.bases.items[bx];
            if (bref->kind != AST_TYPE_REF) continue;
            zan_symbol_t *bs = scope_find(b->current_scope, bref->type_ref.name);
            if (bs && bs->kind == SYM_INTERFACE && bs->decl)
                inherit_default_methods(b, decl, bs->decl, 0);
        }
    }
}

static bool class_has_interface_method(zan_binder_t *b, zan_symbol_t *cls,
                                       zan_ast_node_t *wanted,
                                       zan_ast_list_t *bind_tps,
                                       zan_type_t **bind_args) {
    for (zan_symbol_t *s = cls; s; s = (s->type && s->type->base_type)
             ? s->type->base_type->sym : NULL) {
        for (int i = 0; i < s->member_count; i++) {
            zan_symbol_t *m = s->members[i];
            if (m && m->kind == SYM_METHOD && m->decl &&
                method_signature_equal(b, m->decl, wanted, bind_tps,
                                       bind_args))
                return true;
        }
    }
    return false;
}

static void validate_interface_contract(zan_binder_t *b, zan_symbol_t *cls,
                                        zan_ast_node_t *iface,
                                        zan_type_t *inst, int depth) {
    zan_ast_list_t *bind_tps = NULL;
    zan_type_t **bind_args = NULL;
    if (inst && inst->type_arg_count > 0 &&
        inst->type_arg_count == iface->type_decl.type_params.count) {
        bind_tps = &iface->type_decl.type_params;
        bind_args = inst->type_args;
    }
    if (!iface || depth > 64) return;
    zan_scope_t *saved = b->current_scope;
    int tp_count = iface->type_decl.type_params.count;
    if (tp_count > 0) {
        b->current_scope = scope_new(b->arena, saved);
        register_type_param_list(b, &iface->type_decl.type_params);
    }
    for (int i = 0; i < iface->type_decl.members.count; i++) {
        zan_ast_node_t *im = iface->type_decl.members.items[i];
        if (im->kind != AST_METHOD_DECL ||
            (im->method_decl.modifiers & MOD_STATIC) != 0 ||
            im->method_decl.body)
            continue;
        if (!class_has_interface_method(b, cls, im, bind_tps, bind_args)) {
            zan_diag_emit_code(b->diag, DIAG_ERROR, im->loc, 1015,
                          "type '%.*s' does not implement interface method '%.*s'",
                          (int)cls->name.len, cls->name.str,
                          (int)im->method_decl.name.len,
                          im->method_decl.name.str);
        }
    }
    for (int i = 0; i < iface->type_decl.bases.count; i++) {
        zan_ast_node_t *bref = iface->type_decl.bases.items[i];
        if (bref->kind != AST_TYPE_REF) continue;
        zan_symbol_t *base = scope_find(b->current_scope, bref->type_ref.name);
        if (base && base->kind == SYM_INTERFACE && base->decl) {
            zan_type_t *base_inst = NULL;
            if (bref->type_ref.type_args.count > 0) {
                base_inst = make_type(b->arena, TYPE_CLASS, bref->type_ref.name.str, bref->type_ref.name.len);
                base_inst->sym = base;
                base_inst->type_arg_count = bref->type_ref.type_args.count;
                base_inst->type_args = (zan_type_t **)zan_arena_alloc(
                    b->arena, sizeof(zan_type_t *) * (size_t)base_inst->type_arg_count);
                for (int k = 0; k < base_inst->type_arg_count; k++) {
                    zan_ast_node_t *ta_node = bref->type_ref.type_args.items[k];
                    zan_type_t *resolved_arg = zan_binder_resolve_type(b, ta_node);
                    if (bind_tps && bind_tps->count > 0 && bind_args) {
                        resolved_arg = zan_binder_subst_named(b, resolved_arg, bind_tps, bind_args);
                    }
                    base_inst->type_args[k] = resolved_arg;
                }
            }
            validate_interface_contract(b, cls, base->decl, base_inst, depth + 1);
        }
    }
    b->current_scope = saved;
}

static void validate_interface_contracts(zan_binder_t *b, zan_ast_list_t *decls) {
    for (int i = 0; i < decls->count; i++) {
        zan_ast_node_t *decl = decls->items[i];
        if (decl->kind != AST_CLASS_DECL && decl->kind != AST_STRUCT_DECL) continue;
        zan_symbol_t *cls = scope_find(b->current_scope, decl->type_decl.name);
        if (!cls || !cls->type) continue;
        for (int j = 0; j < cls->type->interface_count; j++) {
            zan_type_t *iface = cls->type->interfaces[j];
            if (iface && iface->sym && iface->sym->decl)
                validate_interface_contract(b, cls, iface->sym->decl, iface, 0);
        }
    }
}

static bool fold_enum_member_value(zan_binder_t *b, zan_symbol_t *type_sym,
                                   zan_ast_node_t *e, int64_t *out) {
    if (!e) return false;
    switch (e->kind) {
    case AST_INT_LITERAL:
        *out = e->int_val;
        return true;
    case AST_UNARY: {
        int64_t v;
        if (!fold_enum_member_value(b, type_sym, e->unary.operand, &v))
            return false;
        uint64_t uv = (uint64_t)v;
        switch (e->unary.op) {
        case TK_MINUS: *out = (int64_t)(0ULL - uv); return true;
        case TK_PLUS:  *out = v; return true;
        case TK_TILDE: *out = ~v; return true;
        default: return false;
        }
    }
    case AST_BINARY: {
        int64_t l, r;
        if (!fold_enum_member_value(b, type_sym, e->binary.left, &l))
            return false;
        if (!fold_enum_member_value(b, type_sym, e->binary.right, &r))
            return false;
        uint64_t ul = (uint64_t)l, ur = (uint64_t)r;
        switch (e->binary.op) {
        case TK_PLUS:  *out = (int64_t)(ul + ur); return true;
        case TK_MINUS: *out = (int64_t)(ul - ur); return true;
        case TK_STAR:  *out = (int64_t)(ul * ur); return true;
        case TK_SLASH:
            if (!r || (l == INT64_MIN && r == -1)) return false;
            *out = l / r; return true;
        case TK_LESS_LESS: *out = (int64_t)((uint64_t)l << (r & 63)); return true;
        case TK_AMP:   *out = l & r; return true;
        case TK_PIPE:  *out = l | r; return true;
        default: return false;
        }
    }
    case AST_IDENTIFIER: {
        for (int i = 0; i < type_sym->member_count; i++) {
            zan_symbol_t *m = type_sym->members[i];
            if (m->kind != SYM_ENUM_MEMBER) continue;
            if (m->name.len != e->ident.name.len ||
                memcmp(m->name.str, e->ident.name.str, m->name.len))
                continue;
            zan_ast_node_t *md = m->decl;
            if (!md || md->kind != AST_ENUM_MEMBER ||
                !md->enum_member.value ||
                md->enum_member.value->kind != AST_INT_LITERAL)
                return false;
            *out = md->enum_member.value->int_val;
            return true;
        }
        return false;
    }
    default:
        return false;
    }
}

static bool enum_value_fits_int(int64_t v) {
    return v >= INT32_MIN && v <= INT32_MAX;
}

/* 扩展方法登记：static + 首参带 `this` 的方法。
 *
 * 登记时机在 bind_members 里——那时方法自己的类型参还在作用域内，
 * 首参类型（`this List<T>` 里的 List<T>）能解析成正确的形状；等
 * checker/irgen 阶段再回头解析，类型参早就出作用域了。 */
static void binder_add_ext_method(zan_binder_t *b, zan_symbol_t *m,
                                  zan_type_t *recv_type) {
    if (b->ext_method_count >= b->ext_method_cap) {
        int ncap = b->ext_method_cap ? b->ext_method_cap * 2 : 32;
        zan_ext_method_t *nm = (zan_ext_method_t *)zan_arena_alloc(
            b->arena, sizeof(zan_ext_method_t) * (size_t)ncap);
        for (int i = 0; i < b->ext_method_count; i++) nm[i] = b->ext_methods[i];
        b->ext_methods = nm;
        b->ext_method_cap = ncap;
    }
    b->ext_methods[b->ext_method_count].method = m;
    b->ext_methods[b->ext_method_count].recv_type = recv_type;
    b->ext_method_count++;
}

static void bind_members(zan_binder_t *b, zan_ast_node_t *type_node) {
    zan_istr_t type_name = type_node->type_decl.name;
    zan_symbol_t *type_sym = scope_find(b->current_scope, type_name);
    if (!type_sym) return;

    zan_scope_t *saved = b->current_scope;
    b->current_scope = scope_new(b->arena, saved);

    for (int i = 0; i < type_node->type_decl.type_params.count; i++) {
        zan_ast_node_t *tp = type_node->type_decl.type_params.items[i];
        zan_type_t *tp_type = make_type(b->arena, TYPE_TYPE_PARAM,
                                        tp->ident.name.str, tp->ident.name.len);
        zan_symbol_t *tp_sym = make_symbol(b->arena, SYM_TYPE_PARAM,
                                           tp->ident.name, tp_type, tp, 0);
        scope_add(b->arena, b->current_scope, tp_sym);
        symbol_add_member(b->arena, type_sym, tp_sym);
    }

    for (int i = 0; i < type_node->type_decl.members.count; i++) {
        zan_ast_node_t *member = type_node->type_decl.members.items[i];

        switch (member->kind) {
        case AST_FIELD_DECL:
        case AST_PROPERTY_DECL: {
            zan_type_t *field_type = zan_binder_resolve_type(b, member->field_decl.type);
            zan_symbol_t *field_sym = make_symbol(b->arena,
                member->kind == AST_PROPERTY_DECL ? SYM_PROPERTY : SYM_FIELD,
                member->field_decl.name, field_type, member,
                member->field_decl.modifiers);
            scope_add(b->arena, b->current_scope, field_sym);
            symbol_add_member(b->arena, type_sym, field_sym);
            check_member_name_clash(b, type_sym, field_sym);
            break;
        }
        case AST_METHOD_DECL: {
            register_type_param_list(b, &member->method_decl.type_params);
            zan_type_t *ret_type = zan_binder_resolve_type(b, member->method_decl.return_type);
            zan_symbol_t *method_sym = make_symbol(b->arena, SYM_METHOD,
                member->method_decl.name, ret_type, member,
                member->method_decl.modifiers);

            zan_type_t *first_param_type = NULL;
            for (int j = 0; j < member->method_decl.params.count; j++) {
                zan_ast_node_t *param = member->method_decl.params.items[j];
                zan_type_t *param_type = zan_binder_resolve_type(b, param->param.type);
                if (j == 0) first_param_type = param_type;
                zan_symbol_t *param_sym = make_symbol(b->arena, SYM_PARAM,
                    param->param.name, param_type, param, 0);
                symbol_add_member(b->arena, method_sym, param_sym);
            }
            scope_add(b->arena, b->current_scope, method_sym);
            symbol_add_member(b->arena, type_sym, method_sym);
            check_member_name_clash(b, type_sym, method_sym);
            /* 扩展方法（首参带 `this`）登记到 binder 的扩展方法表，
             * 供 checker 判定"调用目标方法不存在"时使用。登记口径与
             * irgen 的 find_extension_method 一致：只看首参是不是 `this`
             * 标记，不额外要求 static——两边口径不同就会一边能发、
             * 一边判成"没有这个方法"。 */
            if (member->method_decl.params.count > 0 && first_param_type) {
                zan_ast_node_t *p0 = member->method_decl.params.items[0];
                if (p0 && p0->kind == AST_PARAM && p0->param.is_this)
                    binder_add_ext_method(b, method_sym, first_param_type);
            }
            break;
        }
        case AST_CONSTRUCTOR_DECL: {
            zan_symbol_t *ctor_sym = make_symbol(b->arena, SYM_CONSTRUCTOR,
                member->method_decl.name, type_sym->type, member,
                member->method_decl.modifiers);

            for (int j = 0; j < member->method_decl.params.count; j++) {
                zan_ast_node_t *param = member->method_decl.params.items[j];
                zan_type_t *param_type = zan_binder_resolve_type(b, param->param.type);
                zan_symbol_t *param_sym = make_symbol(b->arena, SYM_PARAM,
                    param->param.name, param_type, param, 0);
                symbol_add_member(b->arena, ctor_sym, param_sym);
            }
            scope_add(b->arena, b->current_scope, ctor_sym);
            symbol_add_member(b->arena, type_sym, ctor_sym);
            check_member_name_clash(b, type_sym, ctor_sym);
            break;
        }
        case AST_DESTRUCTOR_DECL:
            /* A517：`~X()` 曾是死语法——解析/检查全过、从不发射、从不调用，
             * 依赖确定性清理的迁移代码静默失效。ARC 释放链上没有析构钩子，
             * 实现是独立战线；在那之前当场拒绝，不能停在"静默吞掉"。
             * 确定性清理用 using / 显式 Close()/Dispose() 形状改写。 */
            zan_diag_emit_code(b->diag, DIAG_ERROR, member->loc, 1016,
                "destructors ('~%.*s()') are not supported: the ARC runtime "
                "has no destructor hook on release -- do deterministic "
                "cleanup explicitly (using statement or a Close/Dispose "
                "method) instead of relying on '~%.*s()'",
                member->method_decl.name.len, member->method_decl.name.str,
                member->method_decl.name.len, member->method_decl.name.str);
            break;
        case AST_ENUM_MEMBER: {
            if (member->enum_member.value) {
                int64_t v;
                if (fold_enum_member_value(b, type_sym,
                                           member->enum_member.value, &v)) {
                    if (!enum_value_fits_int(v)) {
                        zan_diag_emit_code(b->diag, DIAG_ERROR, member->loc, 1017,
                            "enum member '%.*s' value %lld does not fit in "
                            "'int' (enum members are 32-bit)",
                            member->enum_member.name.len,
                            member->enum_member.name.str, (long long)v);
                    }
                    zan_ast_node_t *lit = zan_ast_new(b->arena,
                        AST_INT_LITERAL, member->loc);
                    lit->int_val = v;
                    member->enum_member.value = lit;
                } else {
                    zan_diag_emit_code(b->diag, DIAG_ERROR, member->loc, 1018,
                        "enum member '%.*s' initializer must be an integer "
                        "constant expression",
                        member->enum_member.name.len,
                        member->enum_member.name.str);
                    member->enum_member.value = NULL;
                }
            }
            zan_symbol_t *em_sym = make_symbol(b->arena, SYM_ENUM_MEMBER,
                member->enum_member.name, type_sym->type, member, MOD_PUBLIC);
            scope_add(b->arena, b->current_scope, em_sym);
            symbol_add_member(b->arena, type_sym, em_sym);
            check_member_name_clash(b, type_sym, em_sym);
            break;
        }
        default:
            break;
        }
    }

    b->current_scope = saved;
}

void zan_binder_bind(zan_binder_t *b, zan_ast_node_t *unit) {
    if (!unit || unit->kind != AST_COMPILATION_UNIT) return;

    bind_type_decls(b, &unit->comp_unit.decls);

    register_type_params(b, &unit->comp_unit.decls);

    /* pass 1 */
    bind_default_interface_methods(b, &unit->comp_unit.decls);

    for (int i = 0; i < unit->comp_unit.decls.count; i++) {
        zan_ast_node_t *decl = unit->comp_unit.decls.items[i];
        if (decl->kind == AST_CLASS_DECL || decl->kind == AST_STRUCT_DECL ||
            decl->kind == AST_INTERFACE_DECL || decl->kind == AST_ENUM_DECL) {
            bind_members(b, decl);
        }
    }

    for (int i = 0; i < unit->comp_unit.decls.count; i++) {
        zan_ast_node_t *decl = unit->comp_unit.decls.items[i];
        if (decl->kind == AST_CLASS_DECL || decl->kind == AST_STRUCT_DECL ||
            decl->kind == AST_INTERFACE_DECL) {
            resolve_bases(b, decl);
        }
    }

    validate_interface_contracts(b, &unit->comp_unit.decls);

    b->binding_done = true;
}

/* ---------- 扩展方法查询（A476） ---------- */

static bool binder_class_implements_iface_depth(zan_symbol_t *cls,
                                                zan_symbol_t *iface, int depth) {
    if (!cls || !cls->type || !iface || depth > 512) return false;
    for (int i = 0; i < cls->type->interface_count; i++) {
        zan_type_t *it = cls->type->interfaces[i];
        if (!it || !it->sym) continue;
        if (it->sym == iface) return true;
        if (it->sym != cls &&
            binder_class_implements_iface_depth(it->sym, iface, depth + 1))
            return true;
    }
    if (cls->type->base_type && cls->type->base_type->sym &&
        cls->type->base_type->sym != cls)
        return binder_class_implements_iface_depth(cls->type->base_type->sym,
                                                   iface, depth + 1);
    return false;
}

bool zan_binder_class_implements_iface(zan_symbol_t *cls, zan_symbol_t *iface) {
    return binder_class_implements_iface_depth(cls, iface, 0);
}

/* 扩展方法的接收者是接口、接收者实现了它 → 适用。
 * 口径与 irgen 的 ext_receiver_interface_match 一致。 */
static bool ext_receiver_interface_match(zan_type_t *pt, zan_type_t *recv_ty) {
    return pt && recv_ty && pt->kind == TYPE_INTERFACE &&
           pt->type_arg_count == 0 &&
           (recv_ty->kind == TYPE_CLASS || recv_ty->kind == TYPE_INTERFACE) &&
           zan_binder_class_implements_iface(recv_ty->sym, pt->sym);
}

/* 扩展方法能不能作用于这个接收者类型。口径与 irgen 的
 * find_extension_method 一致（src/compiler/irgen_expr_core.c）：
 * 类型参接收者（`this T`）适用任意类型；接口接收者看实现关系；
 * 其余按 kind + 符号相等判定。 */
static bool ext_receiver_match(zan_type_t *pt, zan_type_t *recv_ty) {
    if (!pt || !recv_ty) return false;
    if (pt->kind == TYPE_TYPE_PARAM) return true;
    if (ext_receiver_interface_match(pt, recv_ty)) return true;
    if (pt->kind != recv_ty->kind) return false;
    if ((pt->kind == TYPE_CLASS || pt->kind == TYPE_STRUCT ||
         pt->kind == TYPE_INTERFACE || pt->kind == TYPE_ENUM) &&
        pt->sym != recv_ty->sym)
        return false;
    return true;
}

bool zan_binder_has_extension_method(zan_binder_t *b, zan_type_t *recv_ty,
                                     zan_istr_t name) {
    if (!b || !recv_ty || !name.str || name.len == 0) return false;
    for (int i = 0; i < b->ext_method_count; i++) {
        zan_symbol_t *m = b->ext_methods[i].method;
        if (!m || m->name.len != name.len ||
            memcmp(m->name.str, name.str, (size_t)name.len) != 0)
            continue;
        if (!ext_receiver_match(b->ext_methods[i].recv_type, recv_ty)) continue;
        return true;
    }
    return false;
}
