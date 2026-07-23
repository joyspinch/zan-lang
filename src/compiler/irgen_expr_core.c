/* irgen_expr_core.c -- expression codegen: emit_expr/emit_stmt forward decls' helpers --
 * lvalues, loads/stores, comparisons and small shared emit utilities.
 *
 * Part of the irgen translation unit: this file is #include'd by irgen.c
 * (in a fixed order) and must not be compiled standalone. Splitting keeps
 * the single-TU static linkage while keeping each concern in its own file.
 */

/* ---- expression codegen ---- */

static LLVMValueRef emit_expr(zan_irgen_t *g, zan_ast_node_t *expr, local_scope_t *locals);
static void emit_stmt(zan_irgen_t *g, zan_ast_node_t *stmt, local_scope_t *locals);
/* Emit a lambda literal, typing its parameters/return from `expected` (the
 * target delegate type) when the lambda omits annotations. Without this the
 * params default to `int`, so a class-typed parameter cannot resolve fields.
 * `expected` may be NULL/non-delegate, in which case i64/int defaults apply. */
static LLVMValueRef emit_lambda_typed(zan_irgen_t *g, zan_ast_node_t *expr,
                                      zan_type_t *expected, local_scope_t *locals);
/* Resolve the declared type of a method's idx-th parameter (NULL if unknown). */
static zan_type_t *method_param_type(zan_irgen_t *g, zan_symbol_t *msym, int idx);
static zan_type_t *method_param_type_at(zan_irgen_t *g, zan_symbol_t *msym,
                                        int idx, zan_ast_node_t *call,
                                        zan_ast_node_t *recv_expr,
                                        local_scope_t *locals);
static zan_type_t *method_ret_type_at(zan_irgen_t *g, zan_symbol_t *msym,
                                      zan_ast_node_t *call,
                                      zan_ast_node_t *recv_expr,
                                      local_scope_t *locals);
static bool method_ret_is_bare_tp(zan_symbol_t *msym);
/* Emit a call argument, typing a bare lambda from the target delegate param. */
static LLVMValueRef emit_arg_typed(zan_irgen_t *g, zan_ast_node_t *arg,
                                   zan_type_t *ptype, local_scope_t *locals);

/* async/await CPS helpers (defined below; forward-declared for use in the
 * AST_AWAIT_EXPR case of emit_expr). Frame header field indices are shared
 * across every async frame (see docs/ASYNC_CPS_DESIGN.md). */
enum {
    ASYNC_FRAME_STATE = 0,        /* i32: 0=start, k=resume-after-await-k, -1=done */
    ASYNC_FRAME_DONE = 1,         /* i32: 1 once result slot is valid */
    ASYNC_FRAME_AWAITER = 2,      /* i8*: frame waiting on this one (or null) */
    ASYNC_FRAME_AWAITER_STEP = 3, /* void(i8*)*: awaiter's resume fn (or null) */
    ASYNC_FRAME_RESULT = 4,       /* i64: return value (scalars are i64 here) */
    ASYNC_FRAME_FIRST_PARAM = 5
};
static LLVMValueRef coerce_to_i64(zan_irgen_t *g, LLVMValueRef v);
static void emit_async_save_slots(zan_irgen_t *g);
static void emit_async_reload_slots(zan_irgen_t *g);

/* A "name path" is a chain of identifiers joined by member access, e.g.
 * `Foo.Bar.Widget` — the syntactic form of a namespace-qualified type
 * reference. It contains no calls, indexes, `this`/`base`, etc. */
static bool is_name_path(zan_ast_node_t *node) {
    if (!node) return false;
    if (node->kind == AST_IDENTIFIER) return true;
    if (node->kind == AST_MEMBER_ACCESS) return is_name_path(node->member.object);
    return false;
}

/* Leftmost identifier of a name path (the outermost namespace segment). */
static zan_ast_node_t *name_path_head(zan_ast_node_t *node) {
    while (node && node->kind == AST_MEMBER_ACCESS) node = node->member.object;
    return (node && node->kind == AST_IDENTIFIER) ? node : NULL;
}

/* Resolve the declared type of an `obj.field` member access so that element
 * indexing on struct/class array fields (e.g. `b.data[i]`) can determine the
 * element LLVM type. Returns NULL when the field/type cannot be resolved. */
static zan_type_t *infer_expr_type(zan_irgen_t *g, zan_ast_node_t *e, local_scope_t *locals);

static zan_type_t *member_access_field_type(zan_irgen_t *g, local_scope_t *locals, zan_ast_node_t *member) {
    if (!member || member->kind != AST_MEMBER_ACCESS) return NULL;
    zan_ast_node_t *obj = member->member.object;
    if (obj->kind == AST_IDENTIFIER) {
        local_var_t *l = local_find(locals, obj->ident.name);
        if (l && l->type && l->type->sym) {
            zan_symbol_t *fsym = get_field_sym(l->type->sym, member->member.name);
            if (fsym) return fsym->type;
        }
        /* ClassName.StaticField: obj names a class (not a shadowing local) and
         * member is one of its static fields. */
        if (!l && g && g->binder) {
            zan_symbol_t *cs = zan_binder_lookup(g->binder, obj->ident.name);
            if (cs && (cs->kind == SYM_CLASS || cs->kind == SYM_STRUCT)) {
                zan_symbol_t *fsym = get_field_sym(cs, member->member.name);
                if (fsym) return fsym->type;
            }
        }
        /* not a local: could be an implicit `this` field whose own type is a
         * class, e.g. `field.subfield` inside a method. */
        if (g && g->current_type_sym) {
            zan_symbol_t *ofsym = get_field_sym(g->current_type_sym, obj->ident.name);
            if (ofsym && ofsym->type && ofsym->type->sym) {
                zan_symbol_t *fsym = get_field_sym(ofsym->type->sym, member->member.name);
                if (fsym) return fsym->type;
            }
        }
    }
    /* explicit `this.field` / `base.field` — resolve against the current type. */
    if ((obj->kind == AST_THIS_EXPR || obj->kind == AST_BASE_EXPR) &&
        g && g->current_type_sym) {
        zan_symbol_t *fsym = get_field_sym(g->current_type_sym, member->member.name);
        if (fsym) return fsym->type;
    }
    /* General case: the object is any expression whose static type is a
     * class/struct — e.g. `arr[i].field`, `a.b.field`, `make().field`. Infer
     * the object's type and look the field up on it. This is what lets chained
     * subscripts like `arr[i].values[j]` recover the element type (without it
     * the AST_INDEX codegen falls back to a zero constant). */
    {
        zan_type_t *ot = infer_expr_type(g, obj, locals);
        if (ot && ot->sym) {
            zan_symbol_t *fsym = get_field_sym(ot->sym, member->member.name);
            if (fsym) return fsym->type;
        }
    }
    return NULL;
}

static zan_type_t *infer_expr_type(zan_irgen_t *g, zan_ast_node_t *e, local_scope_t *locals);
static zan_type_t *container_elem_type(zan_type_t *t);
static zan_type_t *generic_method_ret(zan_irgen_t *g, zan_symbol_t *msym,
                                      zan_ast_node_t *call, local_scope_t *locals);

/* Render a type reference's display name (with generic args, [] and ?). */
static int render_type_ref_name(const zan_ast_node_t *t, char *buf, int cap) {
    int n = 0;
    if (t && t->kind == AST_TYPE_REF && cap > 1) {
        int len = (int)t->type_ref.name.len;
        if (len > cap - n - 1) len = cap - n - 1;
        memcpy(buf + n, t->type_ref.name.str, (size_t)len);
        n += len;
        if (t->type_ref.type_args.count > 0) {
            if (n < cap - 1) buf[n++] = '<';
            for (int i = 0; i < t->type_ref.type_args.count; i++) {
                if (i > 0) {
                    if (n < cap - 1) buf[n++] = ',';
                    if (n < cap - 1) buf[n++] = ' ';
                }
                n += render_type_ref_name(t->type_ref.type_args.items[i],
                                          buf + n, cap - n);
            }
            if (n < cap - 1) buf[n++] = '>';
        }
        if (t->type_ref.is_array) {
            if (n < cap - 1) buf[n++] = '[';
            if (n < cap - 1) buf[n++] = ']';
        }
        if (t->type_ref.is_nullable && n < cap - 1) buf[n++] = '?';
    }
    if (cap > 0) buf[n] = 0;
    return n;
}

/* Best-effort static test for whether an expression yields a `string` value.
 * Used to route `+` to concatenation and `==`/`!=` to strcmp rather than raw
 * pointer arithmetic/comparison. Reference (class) values are NOT strings. */
static bool is_string_expr(zan_irgen_t *g, zan_ast_node_t *e, local_scope_t *locals) {
    if (!e || !locals) return false;
    switch (e->kind) {
    case AST_STRING_LITERAL:
    case AST_STRING_INTERP:
    case AST_TYPEOF_EXPR:
        return true;
    case AST_IDENTIFIER: {
        local_var_t *l = local_find(locals, e->ident.name);
        if (l) return l->type && l->type->kind == TYPE_STRING;
        if (g->current_type_sym) {
            zan_symbol_t *fs = get_field_sym(g->current_type_sym, e->ident.name);
            if (fs) return fs->type && fs->type->kind == TYPE_STRING;
        }
        return false;
    }
    case AST_MEMBER_ACCESS: {
        zan_type_t *ft = member_access_field_type(g, locals, e);
        return ft && ft->kind == TYPE_STRING;
    }
    case AST_INDEX: {
        /* Indexing a List<string>/string[] yields a borrowed string element.
         * Recognising it keeps `a + list[i]` from releasing the element (which
         * is still owned by the container). */
        zan_type_t *et = container_elem_type(infer_expr_type(g, e->index.object, locals));
        return et && et->kind == TYPE_STRING;
    }
    case AST_BINARY:
        if (e->binary.op == TK_PLUS)
            return is_string_expr(g, e->binary.left, locals) ||
                   is_string_expr(g, e->binary.right, locals);
        return false;
    case AST_CALL: {
        zan_ast_node_t *callee = e->call.callee;
        if (callee && callee->kind == AST_MEMBER_ACCESS) {
            zan_istr_t m = callee->member.name;
            if ((m.len == 9 && memcmp(m.str, "Substring", 9) == 0) ||
                (m.len == 8 && memcmp(m.str, "ToString", 8) == 0) ||
                (m.len == 4 && memcmp(m.str, "Trim", 4) == 0) ||
                (m.len == 7 && memcmp(m.str, "Replace", 7) == 0) ||
                (m.len == 7 && memcmp(m.str, "ToUpper", 7) == 0) ||
                (m.len == 7 && memcmp(m.str, "ToLower", 7) == 0))
                return true;
        }
        /* user method returning string (bare, instance or static call) */
        {
            zan_type_t *rt = infer_expr_type(g, e, locals);
            return rt && rt->kind == TYPE_STRING;
        }
    }
    default:
        return false;
    }
}

/* Element type of a List<T>/array container type. */
static zan_type_t *container_elem_type(zan_type_t *t) {
    if (!t) return NULL;
    if (t->element_type) return t->element_type;
    if (t->type_args && t->type_arg_count > 0) return t->type_args[0];
    return NULL;
}

static zan_type_t *dict_key_type(zan_irgen_t *g, zan_type_t *t) {
    if (!t || !t->type_args || t->type_arg_count < 1) return g ? g->binder->type_string : NULL;
    return t->type_args[0];
}

static zan_type_t *dict_value_type(zan_type_t *t) {
    if (!t || !t->type_args || t->type_arg_count < 2) return NULL;
    return t->type_args[1];
}

/* Extension method lookup: a static method whose first parameter is declared
 * `this T` extends T; `recv.M(args)` resolves to it when no instance method
 * matches. Matches on method name, arity, and the receiver's static type. */
static zan_symbol_t *find_extension_method(zan_irgen_t *g, zan_type_t *recv_ty,
                                           zan_istr_t name, int argc) {
    if (!recv_ty) return NULL;
    for (int fi = 0; fi < g->function_count; fi++) {
        zan_symbol_t *m = g->functions[fi].sym;
        if (!m || m->kind != SYM_METHOD || !m->decl ||
            m->decl->kind != AST_METHOD_DECL)
            continue;
        if (m->name.len != name.len ||
            memcmp(m->name.str, name.str, (size_t)name.len) != 0)
            continue;
        zan_ast_list_t *ps = &m->decl->method_decl.params;
        if (ps->count != argc + 1) continue;
        zan_ast_node_t *p0 = ps->items[0];
        if (!p0 || p0->kind != AST_PARAM || !p0->param.is_this) continue;
        zan_type_t *pt = zan_binder_resolve_type(g->binder, p0->param.type);
        if (!pt || pt->kind != recv_ty->kind) continue;
        if ((pt->kind == TYPE_CLASS || pt->kind == TYPE_STRUCT ||
             pt->kind == TYPE_INTERFACE || pt->kind == TYPE_ENUM) &&
            pt->sym != recv_ty->sym)
            continue;
        return m;
    }
    return NULL;
}

/* Best-effort static inference of an expression's Zan type, composed over
 * identifiers, `this`, field access and element indexing so that patterns like
 * `list[i].field` or `a.b[i].c` resolve their class/struct symbol. */
static zan_type_t *infer_expr_type(zan_irgen_t *g, zan_ast_node_t *e,
                                   local_scope_t *locals) {
    if (!e) return NULL;
    switch (e->kind) {
    case AST_IDENTIFIER: {
        local_var_t *l = local_find(locals, e->ident.name);
        if (l) return l->type;
        if (g->current_type_sym) {
            zan_symbol_t *fs = get_field_sym(g->current_type_sym, e->ident.name);
            if (fs) return fs->type;
        }
        return NULL;
    }
    case AST_THIS_EXPR:
        return g->current_type_sym ? g->current_type_sym->type : NULL;
    case AST_AWAIT_EXPR:
        /* `await E` yields the (unwrapped) result type of the awaited async
         * call — i.e. the callee's declared return type. */
        return infer_expr_type(g, e->await_expr.expr, locals);
    case AST_QUERY_EXPR: {
        /* query yields List<select-type>; the range var is briefly registered
         * (type only) so the projection can be inferred through it */
        zan_type_t *src_ty = infer_expr_type(g, e->query.source, locals);
        zan_type_t *elem = container_elem_type(src_ty);
        if (!elem) elem = g->binder->type_int;
        int mark = locals->count;
        local_add(locals, e->query.var, NULL, elem);
        zan_type_t *sel = infer_expr_type(g, e->query.select, locals);
        locals->count = mark;
        if (!sel) sel = elem;
        return zan_binder_make_list_type(g->binder, sel);
    }
    case AST_MEMBER_ACCESS: {
        /* static field: ClassName.StaticField (class name, not a local). */
        if (e->member.object->kind == AST_IDENTIFIER &&
            !local_find(locals, e->member.object->ident.name)) {
            zan_symbol_t *cs = zan_binder_lookup(g->binder,
                                                 e->member.object->ident.name);
            if (cs && (cs->kind == SYM_CLASS || cs->kind == SYM_STRUCT)) {
                zan_symbol_t *fs = get_field_sym(cs, e->member.name);
                if (fs) return fs->type;
            }
        }
        zan_type_t *ot = infer_expr_type(g, e->member.object, locals);
        if (ot && ot->sym) {
            zan_symbol_t *fs = get_field_sym(ot->sym, e->member.name);
            if (fs) return fs->type;
        }
        return NULL;
    }
    case AST_INDEX:
        return container_elem_type(infer_expr_type(g, e->index.object, locals));
    case AST_CALL: {
        /* Resolve the static return type of a method/function call so that
         * chained member access (e.g. Next().field) can find the struct. */
        zan_ast_node_t *callee = e->call.callee;
        if (!callee) return NULL;
        /* Built-in string instance methods return string but have no symbol to
         * resolve through, so name-match them (mirrors is_string_expr) — lets
         * their owned result be released when passed straight into a call. */
        if (callee->kind == AST_MEMBER_ACCESS) {
            zan_istr_t mm = callee->member.name;
            if ((mm.len == 9 && memcmp(mm.str, "Substring", 9) == 0) ||
                (mm.len == 8 && memcmp(mm.str, "ToString", 8) == 0) ||
                (mm.len == 4 && memcmp(mm.str, "Trim", 4) == 0) ||
                (mm.len == 7 && memcmp(mm.str, "Replace", 7) == 0) ||
                (mm.len == 7 && memcmp(mm.str, "ToUpper", 7) == 0) ||
                (mm.len == 7 && memcmp(mm.str, "ToLower", 7) == 0))
                return g->binder->type_string;
            if (mm.len == 5 && memcmp(mm.str, "Split", 5) == 0) {
                zan_type_t *ot = infer_expr_type(g, callee->member.object, locals);
                if (ot && ot->kind == TYPE_STRING)
                    return zan_binder_make_list_type(g->binder, g->binder->type_string);
            }
        }
        if (callee->kind == AST_IDENTIFIER) {
            /* bare call: current class method, else global function */
            if (g->current_type_sym) {
                zan_symbol_t *m = get_method_sym(g->current_type_sym, callee->ident.name);
                if (m) return m->type;
            }
            zan_symbol_t *gf = zan_binder_lookup(g->binder, callee->ident.name);
            if (gf && gf->kind == SYM_METHOD) return gf->type;
            return NULL;
        }
        if (callee->kind == AST_MEMBER_ACCESS) {
            zan_ast_node_t *obj = callee->member.object;
            /* static: ClassName.Method() (class name, not a shadowing local) */
            if (obj->kind == AST_IDENTIFIER && !local_find(locals, obj->ident.name)) {
                zan_symbol_t *ts = zan_binder_lookup(g->binder, obj->ident.name);
                if (ts && (ts->kind == SYM_CLASS || ts->kind == SYM_STRUCT)) {
                    zan_symbol_t *m = get_method_sym(ts, callee->member.name);
                    if (m) {
                        zan_type_t *gr = generic_method_ret(g, m, e, locals);
                        return gr ? gr : m->type;
                    }
                }
            }
            /* static: Namespace.Path.ClassName.Method() -- the object is a
             * name path, so its rightmost segment is the type name. */
            if (obj->kind == AST_MEMBER_ACCESS && is_name_path(obj)) {
                zan_ast_node_t *head = name_path_head(obj);
                if (head && !local_find(locals, head->ident.name)) {
                    zan_symbol_t *ts = zan_binder_lookup(g->binder, obj->member.name);
                    if (ts && (ts->kind == SYM_CLASS || ts->kind == SYM_STRUCT)) {
                        zan_symbol_t *m = get_method_sym(ts, callee->member.name);
                        if (m) {
                            zan_type_t *gr = generic_method_ret(g, m, e, locals);
                            return gr ? gr : m->type;
                        }
                    }
                }
            }
            /* instance: <expr>.Method() -- resolve the receiver type
             * generally (local, this, field, index, or a nested call) so
             * that fluent chains a.M1().M2().M3() infer at any depth. */
            zan_type_t *rt = infer_expr_type(g, obj, locals);
            if (rt && rt->sym) {
                zan_symbol_t *m = get_method_sym(rt->sym, callee->member.name);
                if (m) return m->type;
            }
            /* extension method: recv.M(args) returns the static method's type
             * (with its generic type parameters substituted from this call
             * site, so fluent chains like list.Where(..).Select(..) keep a
             * concrete element type) */
            zan_symbol_t *xm = find_extension_method(g, rt, callee->member.name,
                                                     e->call.args.count);
            if (xm) return method_ret_type_at(g, xm, e, obj, locals);
        }
        return NULL;
    }
    case AST_NEW_EXPR:
        /* `new T(...)` yields a T; array `new T[n]` is not a single rc object. */
        if (e->new_expr.is_array) return NULL;
        return zan_binder_resolve_type(g->binder, e->new_expr.type);
    case AST_BINARY:
        /* string concatenation (`a + b`) yields a freshly heap-allocated,
         * owned string; other binary operators produce non-rc scalars. */
        if (e->binary.op == TK_PLUS && is_string_expr(g, e, locals))
            return g->binder->type_string;
        switch (e->binary.op) {
        case TK_PLUS: case TK_MINUS: case TK_STAR: case TK_SLASH:
        case TK_PERCENT: case TK_AMP: case TK_PIPE: case TK_CARET:
        case TK_LESS_LESS: case TK_GREATER_GREATER: {
            zan_type_t *lt = infer_expr_type(g, e->binary.left, locals);
            zan_type_t *rt = infer_expr_type(g, e->binary.right, locals);
            if ((lt && lt->kind == TYPE_ULONG) || (rt && rt->kind == TYPE_ULONG))
                return g->binder->type_ulong;
            return NULL;
        }
        default:
            return NULL;
        }
    case AST_TYPEOF_EXPR:
    case AST_STRING_INTERP:
        return g->binder->type_string;
    case AST_CAST_EXPR:
        return zan_binder_resolve_type(g->binder, e->cast.type);
    default:
        return NULL;
    }
}

/* True when an expression's static type is the unsigned 64-bit `ulong`,
 * which selects unsigned division/remainder/shift/compare and %llu output. */
static bool expr_is_ulong(zan_irgen_t *g, zan_ast_node_t *e, local_scope_t *locals) {
    zan_type_t *t = infer_expr_type(g, e, locals);
    return t && t->kind == TYPE_ULONG;
}

/* Class/struct symbol of an expression's static type, or NULL. */
static zan_symbol_t *expr_class_sym(zan_irgen_t *g, zan_ast_node_t *e,
                                    local_scope_t *locals) {
    zan_type_t *t = infer_expr_type(g, e, locals);
    if (t && (t->kind == TYPE_CLASS || t->kind == TYPE_STRUCT)) return t->sym;
    return NULL;
}

/* True when class `cls` (or one of its base classes) declares `iface` — or an
 * interface that itself extends `iface` — in its implements list. */
static bool class_implements_iface(zan_symbol_t *cls, zan_symbol_t *iface) {
    if (!cls || !cls->type || !iface) return false;
    for (int i = 0; i < cls->type->interface_count; i++) {
        zan_type_t *it = cls->type->interfaces[i];
        if (!it || !it->sym) continue;
        if (it->sym == iface) return true;
        if (it->sym != cls && class_implements_iface(it->sym, iface)) return true;
    }
    if (cls->type->base_type && cls->type->base_type->sym &&
        cls->type->base_type->sym != cls)
        return class_implements_iface(cls->type->base_type->sym, iface);
    return false;
}

static bool is_call_to(zan_ast_node_t *expr, const char *obj, const char *method) {
    if (expr->kind != AST_CALL) return false;
    zan_ast_node_t *callee = expr->call.callee;
    if (callee->kind != AST_MEMBER_ACCESS) return false;
    if (callee->member.object->kind != AST_IDENTIFIER) return false;

    zan_istr_t obj_name = callee->member.object->ident.name;
    zan_istr_t method_name = callee->member.name;

    return ((int)obj_name.len == (int)strlen(obj) &&
            memcmp(obj_name.str, obj, (size_t)obj_name.len) == 0 &&
            (int)method_name.len == (int)strlen(method) &&
            memcmp(method_name.str, method, (size_t)method_name.len) == 0);
}
