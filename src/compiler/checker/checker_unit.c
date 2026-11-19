static void check_method_body(zan_checker_t *c, zan_ast_node_t *method) {
    if (!method->method_decl.body) return;

    bool saved_ctor = c->in_ctor;
    c->in_ctor = method->kind == AST_CONSTRUCTOR_DECL;
    if (c->in_ctor && !(method->method_decl.modifiers & MOD_STATIC) &&
        !method->method_decl.has_this_init && !method->method_decl.has_base_init) {
        zan_type_t *bt = c->current_type_sym && c->current_type_sym->type
            ? c->current_type_sym->type->base_type : NULL;
        if (bt && bt->sym && bt->kind == TYPE_CLASS) {
            if (!checker_has_default_ctor(c, bt->sym)) {
                zan_diag_emit_code(c->diag, DIAG_ERROR, method->loc, 3088,
                    "'%.*s' does not contain a constructor that takes 0 arguments",
                    (int)bt->sym->name.len, bt->sym->name.str);
            }
        }
    }
    zan_scope_t *saved_scope = zan_binder_push_type_params(
        c->binder, &method->method_decl.type_params);
    zan_ast_node_t *saved_method = c->current_method_decl;
    c->current_method_decl = method;
    bool saved_nrt = c->in_no_runtime;
    c->in_no_runtime = zan_ast_has_attr(method, "NoRuntime");
    zan_type_t *saved = c->current_return_type;
    c->current_return_type = zan_binder_resolve_type(c->binder,
                                                      method->method_decl.return_type);
    check_generic_constraints(c, c->current_return_type, method->loc);
    struct checker_local *saved_locals = c->locals;
    struct checker_byref_parameter *saved_byref = c->byref_parameters;
    int saved_lambda_depth = c->lambda_depth;
    c->locals = NULL;
    c->byref_parameters = NULL;
    c->lambda_depth = 0;
    for (int j = 0; j < method->method_decl.params.count; j++) {
        zan_ast_node_t *p = method->method_decl.params.items[j];
        if (p && p->kind == AST_PARAM) {
            zan_type_t *pt = zan_binder_resolve_type(c->binder, p->param.type);
            checker_add_local(c, p->param.name, pt);
            checker_bind_byref_parameter(c, p);
        }
    }
    zan_ast_node_t *saved_body = c->current_body;
    c->current_body = method->method_decl.body;
    if (method->kind == AST_CONSTRUCTOR_DECL) {
        /* Initializer arguments are stored outside the body. Check each once
         * in parameter scope, with the same argument context as a normal call. */
        zan_ast_list_t *args = zan_ast_method_base_args(method);
        int saved_in_call_args = c->in_call_args;
        c->in_call_args = 1;
        for (int j = 0; j < args->count; j++)
            zan_checker_check_expr(c, args->items[j]);
        c->in_call_args = saved_in_call_args;
    }
    zan_checker_check_stmt(c, method->method_decl.body);
    zan_definite_check(c->diag, method);
    c->current_body = saved_body;
    c->locals = saved_locals;
    c->byref_parameters = saved_byref;
    c->lambda_depth = saved_lambda_depth;
    c->current_return_type = saved;
    c->in_no_runtime = saved_nrt;
    c->current_method_decl = saved_method;
    c->binder->current_scope = saved_scope;
    c->in_ctor = saved_ctor;
}

typedef struct {
    zan_ast_node_t **slots;
    int cap;                     /* power of two, 0 = unset */
    int count;
} zan_struct_visited_t;

static size_t struct_visit_hash(zan_ast_node_t *n) {
    uintptr_t h = (uintptr_t)n;
    h ^= h >> 33;
    h *= (uintptr_t)0xff51afd7ed558ccdULL;
    h ^= h >> 29;
    return (size_t)h;
}

static bool struct_visit_seen(const zan_struct_visited_t *v,
                              zan_ast_node_t *n) {
    if (!v->cap) return false;
    size_t mask = (size_t)v->cap - 1;
    for (size_t i = struct_visit_hash(n) & mask; v->slots[i];
         i = (i + 1) & mask) {
        if (v->slots[i] == n) return true;
    }
    return false;
}

static void struct_visit_add(zan_checker_t *c, zan_struct_visited_t *v,
                             zan_ast_node_t *n) {
    if (v->count * 2 + 1 >= v->cap) {
        int ncap = v->cap ? v->cap * 2 : 64;
        zan_ast_node_t **nslots = (zan_ast_node_t **)zan_arena_alloc(
            c->arena, sizeof(zan_ast_node_t *) * (size_t)ncap);
        memset(nslots, 0, sizeof(zan_ast_node_t *) * (size_t)ncap);
        int ocap = v->cap;
        zan_ast_node_t **oslots = v->slots;
        v->slots = nslots;
        v->cap = ncap;
        v->count = 0;
        for (int i = 0; i < ocap; i++) {
            if (!oslots[i]) continue;
            size_t mask = (size_t)ncap - 1;
            size_t j = struct_visit_hash(oslots[i]) & mask;
            while (nslots[j]) j = (j + 1) & mask;
            nslots[j] = oslots[i];
            v->count++;
        }
    }
    size_t mask = (size_t)v->cap - 1;
    size_t i = struct_visit_hash(n) & mask;
    while (v->slots[i]) {
        if (v->slots[i] == n) return;
        i = (i + 1) & mask;
    }
    v->slots[i] = n;
    v->count++;
}

static bool check_struct_cycle_dfs(zan_checker_t *c, zan_ast_node_t *struct_decl,
                                   zan_ast_node_t **stack, int depth,
                                   zan_struct_visited_t *visited) {
    if (!struct_decl || struct_decl->kind != AST_STRUCT_DECL) return false;
    if (depth >= 512) {
        zan_diag_emit_code(c->diag, DIAG_ERROR, struct_decl->loc, 2091,
                      "struct '%.*s' exceeds maximum nesting depth",
                      (int)struct_decl->type_decl.name.len,
                      struct_decl->type_decl.name.str);
        return false;
    }
    if (struct_visit_seen(visited, struct_decl)) return false;
    stack[depth] = struct_decl;

    for (int j = 0; j < struct_decl->type_decl.members.count; j++) {
        zan_ast_node_t *member = struct_decl->type_decl.members.items[j];
        if (!member) continue;
        if (member->kind != AST_FIELD_DECL && member->kind != AST_PROPERTY_DECL) continue;
        if (member->field_decl.modifiers & MOD_STATIC) continue;
        if (member->kind == AST_PROPERTY_DECL &&
            (member->field_decl.getter_body || member->field_decl.setter_body)) continue;

        zan_type_t *ftype = zan_binder_resolve_type(c->binder, member->field_decl.type);
        if (!ftype || ftype->kind != TYPE_STRUCT) continue;
        zan_symbol_t *fsym = ftype->sym;
        if (!fsym && c->binder) fsym = zan_binder_lookup(c->binder, ftype->name);
        if (fsym && fsym->decl && fsym->decl->kind == AST_STRUCT_DECL) {
            for (int k = 0; k <= depth; k++) {
                if (stack[k] == fsym->decl) {
                    zan_diag_emit_code(c->diag, DIAG_ERROR, member->loc, 2092,
                                  "struct member '%.*s' of type '%.*s' causes a cycle in the struct layout (recursive struct has infinite size)",
                                  (int)member->field_decl.name.len,
                                  member->field_decl.name.str,
                                  (int)fsym->decl->type_decl.name.len,
                                  fsym->decl->type_decl.name.str);
                    return true;
                }
            }
            if (check_struct_cycle_dfs(c, fsym->decl, stack, depth + 1,
                                       visited)) {
                return true;
            }
        }
    }
    struct_visit_add(c, visited, struct_decl);
    return false;
}

static void check_all_struct_cycles(zan_checker_t *c, zan_ast_node_t *unit) {
    if (!unit || unit->kind != AST_COMPILATION_UNIT) return;
    zan_struct_visited_t visited = { NULL, 0, 0 };
    for (int i = 0; i < unit->comp_unit.decls.count; i++) {
        zan_ast_node_t *decl = unit->comp_unit.decls.items[i];
        if (decl && decl->kind == AST_STRUCT_DECL) {
            zan_ast_node_t *stack[512];
            check_struct_cycle_dfs(c, decl, stack, 0, &visited);
        }
    }
}


static int checker_method_param_count(zan_symbol_t *m) {
    if (!m || !m->decl || m->decl->kind != AST_METHOD_DECL) return -1;
    return m->decl->method_decl.params.count;
}

static bool checker_base_has_virtual_slot(zan_symbol_t *base_sym,
                                          zan_istr_t name, int arity,
                                          int depth) {
    if (!base_sym || depth > CHECKER_DERIVES_MAX_DEPTH) return false;
    for (int i = 0; i < base_sym->member_count; i++) {
        zan_symbol_t *m = base_sym->members[i];
        if (m && m->kind == SYM_METHOD &&
            (m->modifiers & (MOD_VIRTUAL | MOD_OVERRIDE | MOD_ABSTRACT)) &&
            m->name.len == name.len &&
            memcmp(m->name.str, name.str, (size_t)name.len) == 0 &&
            checker_method_param_count(m) == arity)
            return true;
    }
    return checker_base_has_virtual_slot(
        (base_sym->type && base_sym->type->base_type)
            ? base_sym->type->base_type->sym : NULL,
        name, arity, depth + 1);
}

/* 抽象类判定：声明带 abstract，或含抽象方法。
 * 抽象类不能被实例化——它的抽象方法没有实现，new 出来的对象调下去
 * 只会撞上编译器发的「未被覆盖」致命桩。 */
static bool checker_class_is_abstract(zan_symbol_t *sym) {
    if (!sym) return false;
    if (sym->decl && sym->decl->kind == AST_CLASS_DECL &&
        (sym->decl->type_decl.modifiers & MOD_ABSTRACT))
        return true;
    for (int i = 0; i < sym->member_count; i++) {
        zan_symbol_t *m = sym->members[i];
        if (m && m->kind == SYM_METHOD && (m->modifiers & MOD_ABSTRACT))
            return true;
    }
    return false;
}

typedef struct signature_owner {
    zan_type_t *base;
    const struct signature_owner *derived;
} signature_owner_t;

typedef struct {
    const signature_owner_t *owner;
    const zan_ast_list_t *method_params;
} signature_scope_t;

static bool signature_name_equal(zan_istr_t a, zan_istr_t b) {
    return a.len == b.len && (!a.len || memcmp(a.str, b.str, a.len) == 0);
}

static int signature_param_index(const zan_ast_list_t *params, zan_istr_t name) {
    for (int i = 0; params && i < params->count; i++) {
        if (signature_name_equal(params->items[i]->ident.name, name)) return i;
    }
    return -1;
}

/* Each base edge has type arguments expressed in the derived owner's scope.
 * Method type parameters shadow the owner and compare by their position. */
static zan_type_t *signature_subst(zan_type_t *type, signature_scope_t *scope,
                                   int *method_param) {
    *method_param = -1;
    if (type && type->kind == TYPE_TYPE_PARAM)
        *method_param = signature_param_index(scope->method_params, type->name);
    while (type && type->kind == TYPE_TYPE_PARAM && *method_param < 0 && scope->owner) {
        zan_type_t *base = scope->owner->base;
        int i = signature_param_index(&base->sym->decl->type_decl.type_params, type->name);
        if (i < 0 || i >= base->type_arg_count) break;
        type = base->type_args[i];
        scope->owner = scope->owner->derived;
        scope->method_params = NULL;
    }
    return type;
}

static bool signature_is_tuple(zan_type_t *t) {
    return t->kind == TYPE_STRUCT && t->sym && !t->sym->decl &&
           t->name.len >= 7 && memcmp(t->name.str, "__tuple", 7) == 0;
}

static bool signature_type_equal(zan_type_t *a, signature_scope_t as,
                                 zan_type_t *b, signature_scope_t bs, int depth) {
    if (depth > 64) return false;
    int ap, bp;
    a = signature_subst(a, &as, &ap);
    b = signature_subst(b, &bs, &bp);
    if (ap >= 0 || bp >= 0) return ap == bp;
    if (!a || !b) return a == b;
    if (a->kind != b->kind) return false;
    if (a->kind == TYPE_TYPE_PARAM)
        return as.owner == bs.owner && signature_name_equal(a->name, b->name);
    if (a == b && as.owner == bs.owner && as.method_params == bs.method_params)
        return true;
    if (a->kind == TYPE_ARRAY || a->kind == TYPE_NULLABLE) {
        return (a->kind != TYPE_ARRAY || a->array_rank == b->array_rank) &&
            signature_type_equal(a->element_type, as, b->element_type, bs, depth + 1);
    }
    if (signature_is_tuple(a) && signature_is_tuple(b)) {
        if (a->sym->member_count != b->sym->member_count) return false;
        for (int i = 0; i < a->sym->member_count; i++) {
            if (!signature_type_equal(a->sym->members[i]->type, as,
                                      b->sym->members[i]->type, bs, depth + 1))
                return false;
        }
        return true;
    }
    if (a->sym && b->sym ? a->sym != b->sym : !signature_name_equal(a->name, b->name))
        return false;
    if (a->type_arg_count != b->type_arg_count) return false;
    for (int i = 0; i < a->type_arg_count; i++) {
        if (!signature_type_equal(a->type_args[i], as, b->type_args[i], bs, depth + 1))
            return false;
    }
    return true;
}

static zan_type_t *signature_base(zan_symbol_t *owner) {
    return owner && owner->type ? owner->type->base_type : NULL;
}

static bool signature_compare(zan_symbol_t *derived, zan_symbol_t *base,
                              const signature_owner_t *owner, bool compare_return,
                              int depth) {
    zan_symbol_t *current = owner ? owner->base->sym : derived->parent;
    if (current == base->parent) {
        signature_scope_t ds = {NULL, &derived->decl->method_decl.type_params};
        signature_scope_t bs = {owner, &base->decl->method_decl.type_params};
        if (compare_return)
            return signature_type_equal(derived->type, ds, base->type, bs, 0);
        for (int i = 0; i < derived->member_count; i++) {
            zan_symbol_t *a = derived->members[i], *b = base->members[i];
            if (!a || !b || a->kind != SYM_PARAM || b->kind != SYM_PARAM ||
                !a->decl || !b->decl || a->decl->param.by_ref != b->decl->param.by_ref ||
                !signature_type_equal(a->type, ds, b->type, bs, 0)) return false;
        }
        return true;
    }
    zan_type_t *next = signature_base(current);
    if (!next || !next->sym || !next->sym->decl || depth >= CHECKER_DERIVES_MAX_DEPTH)
        return false;
    signature_owner_t edge = {next, owner};
    return signature_compare(derived, base, &edge, compare_return, depth + 1);
}

bool zan_checker_virtual_signature_equal(zan_symbol_t *a, zan_symbol_t *b) {
    if (a == b) return a != NULL;
    if (checker_method_param_count(a) < 0 ||
        checker_method_param_count(a) != checker_method_param_count(b) ||
        !signature_name_equal(a->name, b->name) ||
        ((a->modifiers ^ b->modifiers) & (MOD_STATIC | MOD_ASYNC)) ||
        a->decl->method_decl.type_params.count != b->decl->method_decl.type_params.count ||
        a->member_count != b->member_count) return false;
    return signature_compare(a, b, NULL, false, 0) ||
           signature_compare(b, a, NULL, false, 0);
}

/* Match the same nearest override that supplies the virtual slot. */
static zan_symbol_t *checker_find_abstract_override(zan_symbol_t *cls,
                                                    zan_symbol_t *required) {
    for (zan_symbol_t *owner = cls; owner && owner != required->parent; ) {
        for (int i = 0; i < owner->member_count; i++) {
            zan_symbol_t *candidate = owner->members[i];
            if (candidate && candidate->kind == SYM_METHOD &&
                (candidate->modifiers & MOD_OVERRIDE) &&
                zan_checker_virtual_signature_equal(candidate, required)) return candidate;
        }
        zan_type_t *base = signature_base(owner);
        owner = base ? base->sym : NULL;
    }
    return NULL;
}

static void check_abstract_implementations(zan_checker_t *c, zan_symbol_t *cls) {
    for (zan_type_t *base = signature_base(cls); base && base->sym;
         base = signature_base(base->sym)) {
        zan_symbol_t *owner = base->sym;
        for (int i = 0; i < owner->member_count; i++) {
            zan_symbol_t *required = owner->members[i];
            if (!required || required->kind != SYM_METHOD ||
                !(required->modifiers & MOD_ABSTRACT)) continue;
            zan_symbol_t *found = checker_find_abstract_override(cls, required);
            if (found && signature_compare(found, required, NULL, true, 0) &&
                ((found->modifiers & MOD_ABSTRACT) || found->decl->method_decl.body))
                continue; /* A reabstracted slot is checked at its new declaration. */
            zan_diag_emit_code(c->diag, DIAG_ERROR, cls->decl->loc, 2093,
                "non-abstract class '%.*s' does not implement abstract method "
                "'%.*s' inherited from '%.*s'",
                (int)cls->name.len, cls->name.str,
                (int)required->name.len, required->name.str,
                (int)owner->name.len, owner->name.str);
        }
    }
}

static void check_abstract_members(zan_checker_t *c, zan_ast_node_t *decl) {
    zan_symbol_t *ts = c->current_type_sym;
    if (!ts || ts->decl != decl) return;
    bool is_class = decl->kind == AST_CLASS_DECL;
    bool is_interface = decl->kind == AST_INTERFACE_DECL;
    uint32_t mods = decl->type_decl.modifiers;
    if ((mods & MOD_ABSTRACT) && ((!is_class && !is_interface) ||
        (is_class && (mods & (MOD_STATIC | MOD_SEALED))))) {
        zan_diag_emit_code(c->diag, DIAG_ERROR, decl->loc, 2110,
            "type '%.*s' cannot be declared abstract with its kind or modifiers",
            (int)ts->name.len, ts->name.str);
    }
    bool has_abstract_method = false;
    for (int j = 0; j < decl->type_decl.members.count; j++) {
        zan_ast_node_t *member = decl->type_decl.members.items[j];
        if (member->kind != AST_METHOD_DECL) {
            if (((member->kind == AST_FIELD_DECL || member->kind == AST_PROPERTY_DECL) &&
                 (member->field_decl.modifiers & MOD_ABSTRACT)) ||
                ((member->kind == AST_CONSTRUCTOR_DECL || member->kind == AST_DESTRUCTOR_DECL) &&
                 (member->method_decl.modifiers & MOD_ABSTRACT))) {
                zan_diag_emit_code(c->diag, DIAG_ERROR, member->loc, 2111,
                    "only methods can be declared abstract");
            }
            continue;
        }
        uint32_t mm = member->method_decl.modifiers;
        if (!(mm & MOD_ABSTRACT)) continue;
        has_abstract_method = true;
        zan_istr_t name = member->method_decl.name;
        if (!is_class && !is_interface) {
            zan_diag_emit_code(c->diag, DIAG_ERROR, member->loc, 2111,
                "abstract method '%.*s' must be declared in a class or interface",
                (int)name.len, name.str);
        }
        if (mm & MOD_STATIC) {
            zan_diag_emit_code(c->diag, DIAG_ERROR, member->loc, 2094,
                "an abstract method cannot be static: '%.*s'", (int)name.len, name.str);
        }
        if (mm & MOD_ASYNC) {
            zan_diag_emit_code(c->diag, DIAG_ERROR, member->loc, 2095,
                "an abstract method cannot be async: '%.*s'", (int)name.len, name.str);
        }
        if (mm & (MOD_PRIVATE | MOD_SEALED | MOD_EXTERN)) {
            zan_diag_emit_code(c->diag, DIAG_ERROR, member->loc, 2112,
                "an abstract method cannot be private, sealed, or extern: '%.*s'",
                (int)name.len, name.str);
        }
        if (member->method_decl.body) {
            zan_diag_emit_code(c->diag, DIAG_ERROR, member->loc, 2113,
                "an abstract method cannot have a body: '%.*s'", (int)name.len, name.str);
        }
    }
    if (is_class && !(mods & MOD_ABSTRACT)) {
        if (has_abstract_method) {
            zan_diag_emit_code(c->diag, DIAG_ERROR, decl->loc, 2096,
                "class '%.*s' declares an abstract method and must be declared "
                "'abstract'", (int)ts->name.len, ts->name.str);
        }
        check_abstract_implementations(c, ts);
    }
}

static void check_virtual_shadows_base(zan_checker_t *c, zan_ast_node_t *decl) {
    zan_symbol_t *ts = c->current_type_sym;
    if (!ts || !ts->type || !ts->type->base_type || !ts->type->base_type->sym)
        return;
    for (int j = 0; j < decl->type_decl.members.count; j++) {
        zan_ast_node_t *member = decl->type_decl.members.items[j];
        if (member->kind != AST_METHOD_DECL) continue;
        int want = member->method_decl.params.count;
        zan_symbol_t *sym = NULL;
        for (int i = 0; i < ts->member_count; i++) {
            zan_symbol_t *m = ts->members[i];
            if (m && m->kind == SYM_METHOD &&
                m->name.len == member->method_decl.name.len &&
                memcmp(m->name.str, member->method_decl.name.str,
                       (size_t)member->method_decl.name.len) == 0 &&
                checker_method_param_count(m) == want)
                sym = m;
        }
        if (!sym || !(sym->modifiers & MOD_VIRTUAL) ||
            (sym->modifiers & MOD_OVERRIDE))
            continue;
        if (checker_base_has_virtual_slot(ts->type->base_type->sym,
                                          sym->name, want, 0)) {
            zan_diag_emit_code(c->diag, DIAG_WARNING, member->loc, 2097,
                "'%.*s' is declared 'virtual' but hides an inherited virtual "
                "with the same name and arity; calls through base-class "
                "references will run the base implementation -- "
                "use 'override' to replace it",
                (int)member->method_decl.name.len,
                member->method_decl.name.str);
        }
    }
}

static void check_unit(zan_checker_t *c, zan_ast_node_t *unit, int64_t only_file_id) {
    if (!unit || unit->kind != AST_COMPILATION_UNIT) return;

    zan_mvc_collect_controllers(&c->mvc_routes, unit, c->arena);
    check_all_struct_cycles(c, unit);

    for (int i = 0; i < unit->comp_unit.decls.count; i++) {
        zan_ast_node_t *decl = unit->comp_unit.decls.items[i];
        if (decl->kind != AST_CLASS_DECL && decl->kind != AST_STRUCT_DECL &&
            decl->kind != AST_INTERFACE_DECL && decl->kind != AST_ENUM_DECL) {
            continue;
        }

        c->current_type_sym = zan_binder_lookup(c->binder, decl->type_decl.name);
        zan_scope_t *saved_scope = zan_binder_push_type_params(
            c->binder, &decl->type_decl.type_params);
        if (decl->kind == AST_CLASS_DECL) check_virtual_shadows_base(c, decl);
        check_abstract_members(c, decl);
        for (int j = 0; j < decl->type_decl.members.count; j++) {
            zan_ast_node_t *member = decl->type_decl.members.items[j];
            if (only_file_id >= 0 && member->loc.file_id != only_file_id) continue;
            check_weak_member(c, decl, member);
            check_extern_static(c, member);
            if ((member->kind == AST_FIELD_DECL || member->kind == AST_PROPERTY_DECL) &&
                member->field_decl.initializer) {
                zan_type_t *target = zan_binder_resolve_type(
                    c->binder, member->field_decl.type);
                if (target && target->kind == TYPE_DELEGATE) {
                    int saved_assignable = c->in_assignable_ctx;
                    c->in_assignable_ctx = 1;
                    zan_type_t *value = zan_checker_check_expr(
                        c, member->field_decl.initializer);
                    checker_check_assignable(c, target, value,
                        member->field_decl.initializer, member->loc, "field initializer");
                    c->in_assignable_ctx = saved_assignable;
                }
            }
            if (member->kind == AST_METHOD_DECL ||
                member->kind == AST_CONSTRUCTOR_DECL ||
                member->kind == AST_DESTRUCTOR_DECL) {
                zan_compile_trace("check %.*s.%.*s",
                                (int)decl->type_decl.name.len,
                                decl->type_decl.name.str,
                                (int)member->method_decl.name.len,
                                member->method_decl.name.str);
                check_method_body(c, member);
            }
        }
        c->binder->current_scope = saved_scope;
    }
}

void zan_checker_check(zan_checker_t *c, zan_ast_node_t *unit) {
    check_unit(c, unit, -1);
}

void zan_checker_check_file(zan_checker_t *c, zan_ast_node_t *unit, uint32_t file_id) {
    check_unit(c, unit, file_id);
}
