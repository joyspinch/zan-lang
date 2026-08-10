/* irgen_generics.c -- generic instantiation discovery (the monomorphization worklist).
 *
 * Part of the irgen translation unit: this file is #include'd by irgen.c
 * (in a fixed order) and must not be compiled standalone. Splitting keeps
 * the single-TU static linkage while keeping each concern in its own file.
 */

/* ---- generic instantiation discovery (monomorphization worklist seed) ----
 * Walk the whole compilation unit resolving every declared / constructed type,
 * recording each fully concrete instantiation of a user generic class. Types
 * mentioning a still-erased type parameter (e.g. List<T> inside a generic body)
 * resolve non-concrete and are ignored. */
static void collect_inst_type(zan_irgen_t *g, zan_type_t *t) {
    if (!t) return;
    /* Inside a generic class's body a type written with the class's own
     * parameters (`Inner<T>` as a field of `Box<T>`) is not concrete, so on its
     * own it records no instantiation and calls into it land in the erased copy
     * -- where T is not RC-managed, so a returned element comes back unretained
     * while the caller releases it. Substituting the enclosing instantiation
     * being scanned turns it into `Inner<Opt>`, which is a real instantiation. */
    if (g->collect_inst_ctx && !type_is_concrete(t))
        t = subst_type_param_deep(g, t, g->collect_inst_ctx);
    if (!t) return;
    add_generic_inst(g, t);
    if (t->kind == TYPE_ARRAY || t->kind == TYPE_NULLABLE)
        collect_inst_type(g, t->element_type);
    for (int i = 0; i < t->type_arg_count; i++)
        collect_inst_type(g, t->type_args[i]);
}

static void collect_inst_typeref(zan_irgen_t *g, zan_ast_node_t *tref) {
    if (!tref || tref->kind != AST_TYPE_REF) return;
    collect_inst_type(g, zan_binder_resolve_type(g->binder, tref));
}

static void collect_inst_stmt(zan_irgen_t *g, zan_ast_node_t *st);

static void collect_inst_expr(zan_irgen_t *g, zan_ast_node_t *e) {
    if (!e) return;
    switch (e->kind) {
    case AST_BINARY:
    case AST_ASSIGNMENT:
        collect_inst_expr(g, e->binary.left);
        collect_inst_expr(g, e->binary.right);
        break;
    case AST_UNARY:
    case AST_POSTFIX_UNARY:
        collect_inst_expr(g, e->unary.operand);
        break;
    case AST_AWAIT_EXPR:
        collect_inst_expr(g, e->await_expr.expr);
        break;
    case AST_CALL:
        collect_inst_expr(g, e->call.callee);
        for (int i = 0; i < e->call.args.count; i++)
            collect_inst_expr(g, e->call.args.items[i]);
        break;
    case AST_MEMBER_ACCESS:
        collect_inst_expr(g, e->member.object);
        break;
    case AST_IDENTIFIER:
        /* `Box<int>` naming a constructed type in expression position */
        collect_inst_typeref(g, e->inst_type_ref);
        break;
    case AST_INDEX:
        collect_inst_expr(g, e->index.object);
        collect_inst_expr(g, e->index.index);
        break;
    case AST_CONDITIONAL:
        collect_inst_expr(g, e->conditional.cond);
        collect_inst_expr(g, e->conditional.then_expr);
        collect_inst_expr(g, e->conditional.else_expr);
        break;
    case AST_NEW_EXPR:
        collect_inst_typeref(g, e->new_expr.type);
        for (int i = 0; i < e->new_expr.args.count; i++)
            collect_inst_expr(g, e->new_expr.args.items[i]);
        break;
    case AST_CAST_EXPR:
        collect_inst_typeref(g, e->cast.type);
        collect_inst_expr(g, e->cast.expr);
        break;
    case AST_IS_EXPR:
    case AST_AS_EXPR:
        collect_inst_typeref(g, e->type_test.type);
        collect_inst_expr(g, e->type_test.expr);
        break;
    default:
        break;
    }
}

static void collect_inst_stmt(zan_irgen_t *g, zan_ast_node_t *st) {
    if (!st) return;
    switch (st->kind) {
    case AST_BLOCK:
        for (int i = 0; i < st->block.stmts.count; i++)
            collect_inst_stmt(g, st->block.stmts.items[i]);
        break;
    case AST_VAR_DECL:
        collect_inst_typeref(g, st->var_decl.type);
        collect_inst_expr(g, st->var_decl.initializer);
        break;
    case AST_EXPR_STMT:
        collect_inst_expr(g, st->expr_stmt.expr);
        break;
    case AST_RETURN_STMT:
        collect_inst_expr(g, st->ret.value);
        break;
    case AST_IF_STMT:
        collect_inst_expr(g, st->if_stmt.cond);
        collect_inst_stmt(g, st->if_stmt.then_body);
        collect_inst_stmt(g, st->if_stmt.else_body);
        break;
    case AST_WHILE_STMT:
    case AST_DO_WHILE_STMT:
        collect_inst_expr(g, st->while_stmt.cond);
        collect_inst_stmt(g, st->while_stmt.body);
        break;
    case AST_FOR_STMT:
        collect_inst_stmt(g, st->for_stmt.init);
        collect_inst_expr(g, st->for_stmt.cond);
        collect_inst_expr(g, st->for_stmt.step);
        collect_inst_stmt(g, st->for_stmt.body);
        break;
    case AST_FOREACH_STMT:
        collect_inst_typeref(g, st->foreach_stmt.var_type);
        collect_inst_expr(g, st->foreach_stmt.collection);
        collect_inst_stmt(g, st->foreach_stmt.body);
        break;
    case AST_THROW_STMT:
        collect_inst_expr(g, st->throw_stmt.value);
        break;
    case AST_TRY_STMT:
        collect_inst_stmt(g, st->try_stmt.try_body);
        for (int i = 0; i < st->try_stmt.catches.count; i++)
            collect_inst_stmt(g, st->try_stmt.catches.items[i]->catch_clause.body);
        collect_inst_stmt(g, st->try_stmt.finally_body);
        break;
    case AST_SWITCH_STMT:
        collect_inst_expr(g, st->switch_stmt.expr);
        for (int i = 0; i < st->switch_stmt.cases.count; i++)
            collect_inst_stmt(g, st->switch_stmt.cases.items[i]->switch_case.body);
        break;
    default:
        break;
    }
}

static void collect_inst_member(zan_irgen_t *g, zan_ast_node_t *member) {
    if (!member) return;
    if (member->kind == AST_METHOD_DECL) {
        for (int k = 0; k < member->method_decl.params.count; k++)
            collect_inst_typeref(g, member->method_decl.params.items[k]->param.type);
        collect_inst_typeref(g, member->method_decl.return_type);
        collect_inst_stmt(g, member->method_decl.body);
    } else if (member->kind == AST_CONSTRUCTOR_DECL) {
        for (int k = 0; k < member->method_decl.params.count; k++)
            collect_inst_typeref(g, member->method_decl.params.items[k]->param.type);
        collect_inst_stmt(g, member->method_decl.body);
    } else if (member->kind == AST_FIELD_DECL) {
        collect_inst_typeref(g, member->field_decl.type);
    }
}

/* Repeatedly scan until no new instantiation appears, so a concrete generic
 * used only inside another specialized generic's body (transitive) is found.
 * If the scan keeps adding instantiations past the resource limit, the type
 * graph is either recursively dependent or too large; report an error instead
 * of silently emitting an incomplete program. */
static void discover_generic_insts(zan_irgen_t *g, zan_ast_node_t *unit) {
    if (!unit || unit->kind != AST_COMPILATION_UNIT) return;
    int prev = -1;
    int guard = 0;
    const int limit = 64;
    while (g->generic_inst_count != prev) {
        if (guard >= limit) {
            zan_diag_emit(g->diag, DIAG_ERROR, unit->loc,
                "generic instantiation did not reach a fixed point within "
                "%d iterations; the type graph may be recursively dependent "
                "or too large", limit);
            break;
        }
        prev = g->generic_inst_count;
        for (int i = 0; i < unit->comp_unit.decls.count; i++) {
            zan_ast_node_t *decl = unit->comp_unit.decls.items[i];
            if (decl->kind != AST_CLASS_DECL && decl->kind != AST_STRUCT_DECL &&
                decl->kind != AST_INTERFACE_DECL)
                continue;
            g->collect_inst_ctx = NULL;
            for (int j = 0; j < decl->type_decl.members.count; j++)
                collect_inst_member(g, decl->type_decl.members.items[j]);
            /* Then once per known instantiation of this generic class, so its
             * body's open types (`Inner<T>`) are discovered as the concrete
             * instantiations the specialized copies will call into. */
            zan_istr_t dname = decl->type_decl.name;
            int snapshot = g->generic_inst_count;
            for (int k = 0; k < snapshot; k++) {
                zan_symbol_t *isym = g->generic_insts[k].type_sym;
                if (!isym || isym->name.len != dname.len ||
                    memcmp(isym->name.str, dname.str, (size_t)dname.len) != 0)
                    continue;
                g->collect_inst_ctx = g->generic_insts[k].inst;
                for (int j = 0; j < decl->type_decl.members.count; j++)
                    collect_inst_member(g, decl->type_decl.members.items[j]);
            }
            g->collect_inst_ctx = NULL;
        }
        guard++;
    }
}

/* The instantiation an identifier names when it is a constructed generic type
 * in expression position (`Box<int>.Create(x)`); NULL for a plain identifier. */
static zan_type_t *ident_inst_type(zan_irgen_t *g, zan_ast_node_t *e) {
    if (!e || e->kind != AST_IDENTIFIER || !e->inst_type_ref) return NULL;
    return zan_binder_resolve_type(g->binder, e->inst_type_ref);
}

/* Coerce a call result from the erased opaque pointer back to the concrete
 * type when the callee's declared return type is a generic type parameter of
 * the receiver's instantiated class. No-op otherwise. */
static LLVMValueRef coerce_generic_result(zan_irgen_t *g, LLVMValueRef result,
                                          zan_symbol_t *method_sym,
                                          zan_type_t *recv) {
    if (!result || !method_sym || !recv) return result;
    zan_type_t *rt = subst_type_param(method_sym->type, recv);
    if (rt && rt != method_sym->type)
        return emit_boundary_coerce(g, result, map_type(g, rt));
    return result;
}

/* When the receiver's static type is a concrete instantiation of a user generic
 * class that has a registered specialized method, return the specialized
 * function (and its type via *out_ty); otherwise return the erased function
 * unchanged. Signatures are identical, so this is a pure symbol swap. */
static LLVMValueRef route_generic_method(zan_irgen_t *g, zan_type_t *recv_ty,
                                         zan_symbol_t *method_sym,
                                         LLVMValueRef erased_fn,
                                         LLVMTypeRef erased_ty,
                                         LLVMTypeRef *out_ty) {
    if (out_ty) *out_ty = erased_ty;
    if (!recv_ty || !recv_ty->sym) return erased_fn;
    if (!is_user_generic_sym(recv_ty->sym)) return erased_fn;
    zan_type_t **args = recv_ty->type_args;
    int argc = recv_ty->type_arg_count;
    /* `this.Helper()` inside a specialized body: the receiver's static type is
     * still the open form (Queue<T>), so the plain lookup misses and the call
     * lands in the ERASED copy -- whose T is not RC-managed, so a returned
     * element is handed out unretained and the caller's release frees it while
     * the collection still holds it. Route through the instantiation being
     * emitted instead. */
    if (g->cur_inst && g->cur_inst->sym == recv_ty->sym) {
        bool concrete = argc > 0;
        for (int i = 0; i < argc && concrete; i++)
            concrete = type_is_concrete(args[i]);
        if (!concrete) {
            args = g->cur_inst->type_args;
            argc = g->cur_inst->type_arg_count;
        }
    } else if (g->cur_inst && !type_is_concrete(recv_ty)) {
        /* A *different* generic held by the instantiation being emitted
         * (`ListView<T> list` inside `Dropdown<T>`): substitute this body's
         * concrete arguments so the call reaches ListView<Opt>'s specialized
         * copy, which retains what it returns. */
        zan_type_t *sub = subst_type_param_deep(g, recv_ty, g->cur_inst);
        if (sub && sub->type_arg_count > 0 && type_is_concrete(sub)) {
            args = sub->type_args;
            argc = sub->type_arg_count;
        }
    }
    if (argc <= 0) return erased_fn;
    LLVMTypeRef st = NULL;
    LLVMValueRef sfn = find_generic_fn(g, method_sym, args, argc, &st);
    if (sfn) {
        if (out_ty) *out_ty = st;
        return sfn;
    }
    return erased_fn;
}

/* Emit a call that dispatches through the object's vtable when the target is a
 * virtual/override method invoked on a class instance; otherwise a plain
 * static call. `static_sym` is the receiver's *declared* type. */
static LLVMValueRef emit_dispatch_call(zan_irgen_t *g, zan_symbol_t *static_sym,
        zan_symbol_t *method_sym, LLVMValueRef static_fn, LLVMTypeRef fn_type,
        LLVMValueRef *call_args, int argc, const char *cn) {
    coerce_args_to_params(g, fn_type, call_args, argc);
    if (static_sym && method_sym &&
        (method_sym->modifiers & (MOD_VIRTUAL | MOD_OVERRIDE)) &&
        class_has_virtual_methods(static_sym) && argc >= 1 && call_args[0] &&
        LLVMGetTypeKind(LLVMTypeOf(call_args[0])) == LLVMPointerTypeKind) {
        int slot = get_virtual_method_index(static_sym, method_sym->name);
        LLVMTypeRef st = get_struct_llvm_type(g, static_sym);
        if (slot >= 0 && st) {
            LLVMBuilderRef b = g->builder;
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
            LLVMValueRef thisp = LLVMBuildBitCast(b, call_args[0], LLVMPointerType(st, 0), "vthis");
            LLVMValueRef vpf = LLVMBuildStructGEP2(b, st, thisp, 0, "vpf");
            LLVMValueRef vt8 = LLVMBuildLoad2(b, i8ptr, vpf, "vt8");
            LLVMValueRef vt = LLVMBuildBitCast(b, vt8, LLVMPointerType(i8ptr, 0), "vt");
            LLVMValueRef sidx = LLVMConstInt(i64, (unsigned long long)slot, 0);
            LLVMValueRef sp = LLVMBuildGEP2(b, i8ptr, vt, &sidx, 1, "vsp");
            LLVMValueRef fn8 = LLVMBuildLoad2(b, i8ptr, sp, "vfn8");
            LLVMValueRef fnp = LLVMBuildBitCast(b, fn8, LLVMPointerType(fn_type, 0), "vfnp");
            return zan_call2(b, fn_type, fnp, call_args, (unsigned)argc, cn);
        }
    }
    return zan_call2(g->builder, fn_type, static_fn, call_args, (unsigned)argc, cn);
}

/* A value already carrying an owned (+1) reference we may take over as-is;
 * anything else is a borrowed load that must be retained on capture. */
static int expr_yields_owned_ref(zan_ast_node_t *e) {
    return e && (e->kind == AST_NEW_EXPR || e->kind == AST_CALL ||
                 e->kind == AST_QUERY_EXPR);
}

static int expr_is_arc_object(zan_irgen_t *g, zan_ast_node_t *e, local_scope_t *locals) {
    zan_type_t *t = infer_expr_type(g, e, locals);
    return is_rc_managed_type(t);
}

/* Indexing a temporary container -- Split(",")[0], Keys[i] -- has to release
 * that container, so the element is handed out retained (see emit_expr_index)
 * and the index expression owns its result like a call would. */
static int expr_index_of_owned_temp(zan_irgen_t *g, zan_ast_node_t *e,
                                    local_scope_t *locals);
/* Reading a field off a temporary object -- Make().name, Get().Inner -- has to
 * release that object, so the field is handed out retained (see the general
 * field-access path) and the member expression owns its result like a call. */
static int expr_member_of_owned_temp(zan_irgen_t *g, zan_ast_node_t *e,
                                     local_scope_t *locals);

/* An expression that materializes a delegate value on the spot: a lambda, or
 * an instance method group (`obj.M` not called). Both may allocate a closure
 * record, so the value is freshly owned. */
static int expr_yields_delegate_value(zan_irgen_t *g, zan_ast_node_t *e,
                                      local_scope_t *locals) {
    if (!e) return 0;
    if (e->kind == AST_LAMBDA) return 1;
    if (e->kind != AST_MEMBER_ACCESS) return 0;
    zan_symbol_t *cls = expr_class_sym(g, e->member.object, locals);
    zan_symbol_t *ms = cls ? get_method_sym(cls, e->member.name) : NULL;
    return ms && ms->decl && ms->decl->kind == AST_METHOD_DECL &&
           (ms->decl->method_decl.modifiers & MOD_STATIC) == 0;
}

static int expr_yields_owned_rc_value(zan_irgen_t *g, zan_ast_node_t *e,
                                      local_scope_t *locals) {
    if (!e) return 0;
    if (e->kind == AST_INDEX) {
        /* A user-defined op_index lowers to a method call, so an RC-managed
         * return is owned (+1) exactly like an AST_CALL result. This applies
         * even when the indexed receiver is a local; treating it as a borrowed
         * container element leaked every PyObject returned by PyObject.op_index. */
        zan_type_t *ot = infer_expr_type(g, e->index.object, locals);
        if (ot && (ot->kind == TYPE_CLASS || ot->kind == TYPE_STRUCT) && ot->sym) {
            zan_istr_t op_istr = {(char *)"op_index", 8};
            if (get_method_sym(ot->sym, op_istr)) return 1;
        }
        if (expr_index_of_owned_temp(g, e, locals)) return 1;
    }
    if (e->kind == AST_MEMBER_ACCESS && expr_member_of_owned_temp(g, e, locals))
        return 1;
    if (e->kind == AST_NEW_EXPR || e->kind == AST_CALL ||
        e->kind == AST_QUERY_EXPR) return 1;
    /* A switch expression yields the owned (+1) value that its matching arm
     * stored into the hidden result local (emit_expr_switch_expr): the store
     * moves an owned arm or retains a borrowed one, leaving the slot with +1,
     * and the hidden local is removed from scope so nothing releases it. The
     * receiving local must therefore treat it as owned and take that +1, or
     * the reference leaks at exit (leakcheck_cs_b06_switch_expr). */
    if (e->kind == AST_SWITCH_EXPR) return 1;
    /* A capturing lambda, and an instance method group used as a value, each
     * allocate a fresh closure record (+1): whoever receives it owns that
     * count and must not retain again. Non-capturing lambdas and static
     * method groups are bare function pointers, where retain is a no-op. */
    if (expr_yields_delegate_value(g, e, locals)) return 1;
    /* A conditional is owned when either branch yields an owned value. The IR
     * emitter retains the borrowed branch in that mixed-ownership case so the
     * PHI has one consistent (+1) contract regardless of the path taken. */
    if (e->kind == AST_CONDITIONAL) {
        return expr_yields_owned_rc_value(g, e->conditional.then_expr, locals) ||
               expr_yields_owned_rc_value(g, e->conditional.else_expr, locals);
    }
    /* An awaited async call yields a freshly owned (+1) reference: the callee's
     * async `return` always retains the result before completing (see the
     * AST_RETURN_STMT async path), so the awaiter receives +1 and must NOT
     * retain it again. Treating it as owned lets the receiving local release it
     * on scope exit -- otherwise every awaited RC result leaks per await. */
    if (e->kind == AST_AWAIT_EXPR) return 1;
    /* An ANF await temp ($awN, generated by anf_hoist_await) is a declared local
     * of the async body with the awaited method's type, so it owns the +1 it
     * received and releases it at scope exit: reading it is a borrowed load like
     * any other local, and the reader retains. (It used to be typed `int` and
     * therefore unowned, which made reads a move.) */
    if (e->kind == AST_STRING_INTERP) return 1;
    /* Dict.Keys / Dict.Values build a fresh owned List snapshot. */
    if (e->kind == AST_MEMBER_ACCESS &&
        ((e->member.name.len == 4 && memcmp(e->member.name.str, "Keys", 4) == 0) ||
         (e->member.name.len == 6 && memcmp(e->member.name.str, "Values", 6) == 0))) {
        zan_type_t *ot = infer_expr_type(g, e->member.object, locals);
        if (ot && type_named(ot, "Dict", 4))
            return 1;
    }
    if (e->kind == AST_BINARY) {
        if (e->binary.op == TK_PLUS && is_string_expr(g, e, locals)) {
            return 1;
        }
        /* A binary op on a user class/struct lowers to a static op_add/op_sub/...
         * method call, which returns an owned (+1) value like any other call.
         * Reporting it as owned prevents the assignment target from retaining a
         * second reference (which would leak, e.g. `event += handler`). */
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

static int expr_is_local_ident(zan_ast_node_t *e, local_scope_t *locals) {
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

/* The field type a member access yields, with the receiver's type arguments
 * substituted, or NULL when the receiver has no such field. */
static zan_type_t *member_owned_field_type(zan_irgen_t *g, zan_ast_node_t *e,
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

static void emit_release_owned_call_temp(zan_irgen_t *g, zan_ast_node_t *arg,
                                         LLVMValueRef val, local_scope_t *locals) {
    if (!arg || !locals || !val) return;
    if (LLVMGetTypeKind(LLVMTypeOf(val)) != LLVMPointerTypeKind) return;
    if (expr_is_local_ident(arg, locals)) return;
    /* A delegate argument written in place -- `Apply((a) => a * k, 5)`,
     * `Sub(s.OnE)` -- is a temporary the call site owns. Its static type is
     * only known from the parameter, so release it by shape: the closure
     * release tests the tag and ignores a bare function pointer. */
    if (expr_yields_delegate_value(g, arg, locals)) {
        emit_closure_release(g, val);
        return;
    }
    zan_type_t *t = infer_expr_type(g, arg, locals);
    if (!t || !is_rc_managed_type(t)) return;
    if (!expr_yields_owned_rc_value(g, arg, locals)) return;
    emit_rc_release_for_type(g, t, val);
}

static int call_consumes_free_arg(LLVMValueRef callee) {
    if (!callee) return 0;
    size_t name_len = 0;
    const char *name = LLVMGetValueName2(callee, &name_len);
    return name && name_len == 4 && memcmp(name, "free", 4) == 0;
}

static void emit_invalidate_freed_string(zan_irgen_t *g, zan_ast_node_t *arg,
                                         local_scope_t *locals);

static LLVMValueRef emit_delegate_call(zan_irgen_t *g,
                                       zan_type_t *delegate_type,
                                       LLVMValueRef fn_ptr,
                                       zan_ast_node_t *call,
                                       local_scope_t *locals) {
    int pc = delegate_type->delegate_param_count;
    LLVMTypeRef *param_types = (LLVMTypeRef *)calloc(
        (size_t)(pc > 0 ? pc : 1), sizeof(LLVMTypeRef));
    for (int k = 0; k < pc; k++) {
        param_types[k] = map_type(
            g, delegate_type->delegate_param_types[k]);
    }
    /* async delegate: invocation returns an i8* task handle (see map_type). */
    LLVMTypeRef ret = delegate_type->delegate_is_async
        ? LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0)
        : (delegate_type->delegate_ret_type
            ? map_type(g, delegate_type->delegate_ret_type)
            : LLVMVoidTypeInContext(g->ctx));
    LLVMTypeRef fn_type = LLVMFunctionType(
        ret, param_types, (unsigned)pc, 0);
    int argc = call->call.args.count;
    LLVMValueRef *call_args = (LLVMValueRef *)calloc(
        (size_t)(argc > 0 ? argc : 1), sizeof(LLVMValueRef));
    for (int k = 0; k < argc; k++) {
        call_args[k] = emit_expr(
            g, call->call.args.items[k], locals);
        if (k < pc) {
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
    free(call_args);
    free(param_types);
    return result;
}

static void emit_leak_report_support(zan_irgen_t *g) {
    if (!g->check_leaks || g->fn_report_leaks) return;
    /* void __zan_report_leaks(void): at program exit, if any ARC object is
     * still live, print a summary line and then a per-allocation-site
     * breakdown ("file:line:col"). Scheduled via atexit when --check-leaks. */
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
    /* The report also goes to a file: a GUI program is linked for the windows
     * subsystem and has no stdout, so printf alone loses the report exactly
     * where it is needed (the IDE). `log_*` blocks write the same text to
     * zan_leaks.log next to the working directory when the file opens. */
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
    LLVMValueRef msg = LLVMBuildGlobalStringPtr(b,
        "zan: memory leak detected: %lld object(s) still reachable at exit\n", "leak_fmt");
    LLVMValueRef pargs[] = { msg, live };
    zan_call2(b, g->printf_type, g->fn_printf, pargs, 2, "");
    LLVMValueRef log_path = LLVMBuildGlobalStringPtr(b, "zan_leaks.log", "leak_log_path");
    LLVMValueRef log_mode = LLVMBuildGlobalStringPtr(b, "ab", "leak_log_mode");
    LLVMValueRef fo_args[2] = { log_path, log_mode };
    LLVMValueRef log_fh = zan_call2(b, fopen_type, fn_fopen, fo_args, 2, "leaklog");
    LLVMValueRef have_log = LLVMBuildIsNotNull(b, log_fh, "havelog");
    LLVMBuildCondBr(b, have_log, log_sum_bb, head_bb);

    LLVMPositionBuilderAtEnd(b, log_sum_bb);
    LLVMValueRef fsum_args[3] = { log_fh, msg, live };
    zan_call2(b, fprintf_type, fn_fprintf, fsum_args, 3, "");
    LLVMBuildBr(b, head_bb);

    /* iterate the site buckets, printing those with a positive live count */
    LLVMPositionBuilderAtEnd(b, head_bb);
    LLVMValueRef idx = LLVMBuildPhi(b, i64, "i");
    LLVMValueRef in_range = zan_icmp(b, LLVMIntSLT, idx,
        LLVMConstInt(i64, ZAN_MAX_LEAK_SITES, 0), "inrange");
    LLVMBuildCondBr(b, in_range, body_bb, close_bb);

    LLVMPositionBuilderAtEnd(b, body_bb);
    LLVMValueRef z32 = LLVMConstInt(i32t, 0, 0);
    LLVMValueRef cidx[2] = { z32, idx };
    LLVMValueRef sc_ptr = LLVMBuildGEP2(b, g->site_live_type, g->g_site_live, cidx, 2, "scptr");
    LLVMValueRef sc = LLVMBuildLoad2(b, i64, sc_ptr, "sc");
    LLVMValueRef has = zan_icmp(b, LLVMIntSGT, sc, LLVMConstInt(i64, 0, 0), "has");
    LLVMBuildCondBr(b, has, print_bb, next_bb);

    LLVMPositionBuilderAtEnd(b, print_bb);
    LLVMValueRef nm_ptr = LLVMBuildGEP2(b, g->site_names_type, g->g_site_names, cidx, 2, "nmptr");
    LLVMValueRef nm = LLVMBuildLoad2(b, i8p, nm_ptr, "nm");
    LLVMValueRef dmsg = LLVMBuildGlobalStringPtr(b,
        "  %lld object(s) leaked, allocated at %s\n", "leak_site_fmt");
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

/* defined in later-included parts of this translation unit */
static void emit_eh_tmp_push_slot(zan_irgen_t *g, LLVMValueRef slot, int kind);
static void emit_eh_tmp_pop(zan_irgen_t *g);
static void emit_eh_tmp_drop(zan_irgen_t *g, LLVMValueRef obj);

/* True when a local's slot holds an owning class heap reference (excludes
 * borrowed params, stack-struct classes and non-class types), so assigning to
 * it must release the previous occupant and retain the new one. */
static int local_slot_owns_rc(local_var_t *v) {
    if (!v || v->arc_owned != 1 || !v->type || !is_rc_managed_type(v->type)) return 0;
    /* A `ref`/`out` parameter writes through the caller's owning slot, whose
     * storage is a pointer parameter rather than an alloca instruction. */
    if (v->byref_slot) return 1;
    /* A boxed local (A33-2b) keeps its value in a heap cell; `alloca` points
     * into that cell rather than being an alloca instruction. */
    if (v->box_cell) return 1;
    return LLVMGetTypeKind(LLVMGetAllocatedType(v->alloca)) == LLVMPointerTypeKind;
}

/* True when a local is an `object` whose ownership is tracked at runtime (see
 * the object-local comment below). An async body's locals live in the heap
 * frame, which the stack-allocated flag would not survive, so one is only
 * tracked there if a flag already exists. */
static int local_is_dyn_obj(zan_irgen_t *g, local_var_t *v) {
    if (!v || !v->type || v->type->kind != TYPE_OBJECT) return 0;
    if (v->box_cell || g->current_async_frame) return v->obj_rc_flag != NULL;
    return LLVMGetTypeKind(local_slot_type(g, v)) == LLVMPointerTypeKind;
}

/* True when scope exit must release the reference in the local's slot. A boxed
 * local is released as a whole cell instead (its destructor releases the value
 * inside), so releasing the slot here as well would double-release. */
static int local_owns_arc(local_var_t *v) {
    return local_slot_owns_rc(v) && !v->box_cell && !v->byref_slot;
}

/* Boxed locals are released as a whole cell: the last reference out (this
 * scope or a closure still holding the variable) runs the cell's destructor,
 * which releases an rc-managed value inside it. */
static void emit_closure_record_release(zan_irgen_t *g, LLVMValueRef rec);
static void release_boxed_local(zan_irgen_t *g, local_var_t *v) {
    if (v && v->box_cell && v->box_owned) emit_closure_record_release(g, v->box_cell);
}

/* Register the most recently declared local's slot with the unwinder and mark
 * it owning. A longjmp skips the scope-exit release of every frame it unwinds
 * through, so the slot is what lets the throw site release them (A8-12).
 *
 * Async bodies are excluded: their locals live in the heap frame, which
 * __zan_async_unwind releases per suspended frame, and the throw site releases
 * the current one (A8-3). */
static void arc_own_local(zan_irgen_t *g, local_scope_t *locals) {
    if (!locals || locals->count == 0) return;
    local_var_t *v = &locals->vars[locals->count - 1];
    v->arc_owned = 1;
    if (v->eh_slot || g->current_async_frame || !local_owns_arc(v)) return;
    emit_eh_tmp_push_slot(g, v->alloca, eh_slot_kind_of(v->type));
    v->eh_slot = 1;
}

/* Drop a released local's unwind-stack entry. Entries are pushed in
 * declaration order and every release pass walks a suffix of the scope, so
 * popping once per released registered local restores the depth the scope was
 * entered at. */
static void pop_eh_slot(zan_irgen_t *g, local_var_t *v) {
    if (v && v->eh_slot) emit_eh_tmp_pop(g);
}

/* Release owned locals in [start, count) without dropping them from the
 * scope: used on `break`/`continue` edges, where the same locals remain in
 * scope on the fall-through path and are released there separately. */
/* Release every owning local at function exit, except `keep` (when non-null).
 *
 * A bare `new T[n]` array has no rc header, so a function that returns one
 * hands over the only reference: its elements must not be released on the way
 * out, or the caller receives an array full of dangling pointers. Rc-managed
 * returns (including Dict, see A8-2) don't need the exclusion -- `return`
 * retains them before the release pass. */
static void emit_release_owned_locals_except(zan_irgen_t *g, local_scope_t *locals,
                                             local_var_t *keep) {
    if (!locals) return;
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    for (int i = 0; i < locals->count; i++) {
        pop_eh_slot(g, &locals->vars[i]);
        if (keep && &locals->vars[i] == keep) continue;
        if (locals->vars[i].obj_rc_flag) {
            emit_release_obj_local(g, &locals->vars[i]);
        } else if (locals->vars[i].box_cell) {
            release_boxed_local(g, &locals->vars[i]);
        } else if (local_owns_arc(&locals->vars[i])) {
            LLVMValueRef cur = LLVMBuildLoad2(g->builder, i8ptr,
                                              locals->vars[i].alloca, "arc.rel");
            emit_rc_release_for_type(g, locals->vars[i].type, cur);
        } else if (locals->vars[i].arr_len && locals->vars[i].type) {
            LLVMValueRef a = LLVMBuildLoad2(g->builder, i8ptr,
                                            locals->vars[i].alloca, "arr.rel");
            LLVMValueRef nn = LLVMBuildLoad2(g->builder,
                LLVMInt64TypeInContext(g->ctx), locals->vars[i].arr_len, "arr.n");
            emit_array_release_elems(g, locals->vars[i].type->element_type, a, nn);
        }
    }
}

static void emit_release_owned_locals(zan_irgen_t *g, local_scope_t *locals) {
    emit_release_owned_locals_except(g, locals, NULL);
}

static void emit_release_owned_locals_range(zan_irgen_t *g, local_scope_t *locals, int start) {
    if (!locals) return;
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    for (int i = locals->count - 1; i >= start; i--) {
        pop_eh_slot(g, &locals->vars[i]);
        if (locals->vars[i].obj_rc_flag) {
            emit_release_obj_local(g, &locals->vars[i]);
        } else if (locals->vars[i].box_cell) {
            release_boxed_local(g, &locals->vars[i]);
        } else if (local_owns_arc(&locals->vars[i])) {
            LLVMValueRef cur = LLVMBuildLoad2(g->builder, i8ptr,
                                              locals->vars[i].alloca, "arc.rel");
            emit_rc_release_for_type(g, locals->vars[i].type, cur);
        } else if (locals->vars[i].arr_len && locals->vars[i].type) {
            LLVMValueRef a = LLVMBuildLoad2(g->builder, i8ptr,
                                            locals->vars[i].alloca, "arr.rel");
            LLVMValueRef nn = LLVMBuildLoad2(g->builder,
                LLVMInt64TypeInContext(g->ctx), locals->vars[i].arr_len, "arr.n");
            emit_array_release_elems(g, locals->vars[i].type->element_type, a, nn);
        }
    }
}

/* Release the exceptions owned by the catch bodies in [base, count): the
 * handler entry stacked each one as an EH temp and the try epilogue would pop
 * and release it, but `return`/`break`/`continue`/`throw` leave the handler
 * without reaching that epilogue. Innermost first, mirroring the epilogue. */
static void emit_release_active_catch_excs(zan_irgen_t *g, int base) {
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
        emit_eh_tmp_drop(g, ev);
        zan_call2(g->builder,
            LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), &i8ptr, 1, 0),
            g->rt_release_dyn, &ev, 1, "");
        /* leave __zan_eh_exc alone: on a `throw` out of the handler it already
         * carries the new in-flight exception */
        LLVMBuildStore(g->builder, LLVMConstInt(i32, 0, 0), owned_slot);
        LLVMBuildBr(g->builder, cont_bb);
        LLVMPositionBuilderAtEnd(g->builder, cont_bb);
    }
}

/* Null the slots of the owning locals in [start, count) after they have been
 * released. An async body's locals are frame-resident and every path that
 * abandons the frame -- the coroutine's exception landing pad, the frame
 * cleanup -- releases them again from those slots, so a throw that releases
 * them itself (A8-3) must leave null behind instead of dangling pointers. */
static void emit_clear_owned_locals_range(zan_irgen_t *g, local_scope_t *locals, int start) {
    if (!locals) return;
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    for (int i = locals->count - 1; i >= start; i--) {
        if (local_owns_arc(&locals->vars[i])) {
            LLVMBuildStore(g->builder, LLVMConstNull(i8ptr),
                           locals->vars[i].alloca);
        } else if (locals->vars[i].arr_len && locals->vars[i].type) {
            LLVMBuildStore(g->builder,
                           LLVMConstInt(LLVMInt64TypeInContext(g->ctx), 0, 0),
                           locals->vars[i].arr_len);
        }
    }
}

static void emit_release_owned_locals_from(zan_irgen_t *g, local_scope_t *locals, int start) {
    if (!locals || start >= locals->count) return;
    /* If the current block already ends in a terminator (e.g. the scope exited
     * via `return`/`break`/`continue`), emitting release calls here would append
     * dead instructions *after* the terminator — invalid IR ("basic block does
     * not have terminator"). A `return` has already released the owning locals
     * on its own path, so only the bookkeeping (drop ownership tracking, shrink
     * the scope) is needed in that case. */
    bool terminated =
        LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder)) != NULL;
    for (int i = locals->count - 1; i >= start; i--) {
        if (!terminated) pop_eh_slot(g, &locals->vars[i]);
        if (!terminated && locals->vars[i].obj_rc_flag) {
            emit_release_obj_local(g, &locals->vars[i]);
        } else if (!terminated && locals->vars[i].box_cell) {
            release_boxed_local(g, &locals->vars[i]);
        } else if (!terminated && local_owns_arc(&locals->vars[i])) {
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMValueRef cur = LLVMBuildLoad2(g->builder, i8ptr,
                                              locals->vars[i].alloca, "arc.rel");
            emit_rc_release_for_type(g, locals->vars[i].type, cur);
        } else if (!terminated && locals->vars[i].arr_len && locals->vars[i].type) {
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMValueRef a = LLVMBuildLoad2(g->builder, i8ptr,
                                            locals->vars[i].alloca, "arr.rel");
            LLVMValueRef nn = LLVMBuildLoad2(g->builder,
                LLVMInt64TypeInContext(g->ctx), locals->vars[i].arr_len, "arr.n");
            emit_array_release_elems(g, locals->vars[i].type->element_type, a, nn);
        }
        locals->vars[i].arr_len = NULL;
        locals->vars[i].arc_owned = 0;
    }
    locals->count = start;
}

/* ---- `object` locals ------------------------------------------------------
 * An object-typed slot is pointer-wide and untyped: `object o = new Dog()`
 * stores a heap reference, `object o = "hi"` a string literal in static
 * storage, `object o = 5` the integer itself. Releasing on the static type
 * would therefore either leak the first or corrupt the others, so an object
 * local carries an i1 flag saying whether its current occupant is an owned
 * heap reference. Only a value whose static type is an ARC-managed class sets
 * it, and only a set flag releases -- an unrecognised occupant is left alone.
 *
 * A local that never receives a managed value has no flag and costs nothing. */
static LLVMValueRef obj_rc_flag_slot(zan_irgen_t *g, local_var_t *v) {
    if (!v->obj_rc_flag) {
        LLVMTypeRef i1 = LLVMInt1TypeInContext(g->ctx);
        LLVMValueRef here = LLVMGetInsertBlock(g->builder);
        v->obj_rc_flag = emit_entry_alloca(g, i1, "obj.owns");
        LLVMPositionBuilderAtEnd(g->builder, here);
        LLVMBuildStore(g->builder, LLVMConstInt(i1, 0, 0), v->obj_rc_flag);
    }
    return v->obj_rc_flag;
}

/* Release the occupant of an object local when it owns one. */
static void emit_release_obj_local(zan_irgen_t *g, local_var_t *v) {
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
    emit_arc_release_typed(g, NULL, cur);
    LLVMBuildStore(g->builder, LLVMConstInt(i1, 0, 0), v->obj_rc_flag);
    LLVMBuildStore(g->builder, LLVMConstNull(i8ptr), v->alloca);
    LLVMBuildBr(g->builder, cont);
    LLVMPositionBuilderAtEnd(g->builder, cont);
}

/* True when a value of static type `t` stored into an object slot is a heap
 * reference the slot can own. Strings and delegates have their own release
 * functions and are left borrowed rather than tracked with a second flag. */
static int obj_slot_owns_value(zan_type_t *t) {
    return t && is_arc_managed_type(t);
}

/* Store `v` (from `rhs`, static type `vtype`) into an object local, keeping the
 * ownership flag in step: release what the slot owned, take a reference to the
 * new occupant when it is a heap object the expression does not already hand
 * over, and record whether the slot now owns anything. */
static void emit_obj_local_store(zan_irgen_t *g, local_var_t *v, LLVMValueRef val,
                                 zan_type_t *vtype, zan_ast_node_t *rhs,
                                 local_scope_t *locals) {
    LLVMTypeRef i1 = LLVMInt1TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    int owns = obj_slot_owns_value(vtype);
    if (owns || v->obj_rc_flag) obj_rc_flag_slot(g, v);
    emit_release_obj_local(g, v);
    if (owns) {
        if (LLVMGetTypeKind(LLVMTypeOf(val)) == LLVMPointerTypeKind &&
            LLVMTypeOf(val) != i8ptr)
            val = LLVMBuildBitCast(g->builder, val, i8ptr, "obj.bc");
        if (!expr_yields_owned_rc_value(g, rhs, locals))
            emit_arc_retain(g, val);
    }
    zan_store_fit(g, val, v->alloca);
    if (v->obj_rc_flag)
        LLVMBuildStore(g->builder, LLVMConstInt(i1, owns ? 1 : 0, 0),
                       v->obj_rc_flag);
}

/* Capture value `v` (from `rhs`) into an owning reference slot `slot_alloca`:
 * release the previous occupant, retain a borrowed reference, then store. The
 * slot must be null-initialised before its first capture so the release is a
 * no-op. */
static void emit_rc_capture_local(zan_irgen_t *g, zan_type_t *type,
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

/* Retain a borrowed value stored into an owning field, releasing the previous
 * occupant (fields are zero-initialised at object allocation). */
static void emit_rc_store_field(zan_irgen_t *g, zan_type_t *type,
                                LLVMValueRef field_ptr, LLVMValueRef v,
                                zan_ast_node_t *rhs, local_scope_t *locals,
                                int is_weak) {
    LLVMTypeRef vt = LLVMTypeOf(v);
    /* The slot's element type and the value's type may be different pointer
     * representations of the same object (e.g. `null` arrives as i8*, a
     * constructor result as %struct.T*); bitcast so the store verifies under
     * typed-pointer LLVM builds. */
    LLVMTypeRef slot_t = LLVMTypeOf(field_ptr);
    LLVMTypeRef elem_t = vt;
    /* Opaque-pointer LLVM builds (15+) have no pointer element type — and
     * every pointer already inter-stores freely there, so keep vt. */
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
    /* weak field: non-owning store, no retain of new / release of old, so a
     * parent<->child back-reference does not form an ARC-uncollectable cycle. */
    if (is_weak || LLVMGetTypeKind(vt) != LLVMPointerTypeKind) {
        LLVMBuildStore(g->builder, v, field_ptr);
        return;
    }
    LLVMValueRef old = LLVMBuildLoad2(g->builder, elem_t, field_ptr, "arc.fold");
    if (!expr_yields_owned_rc_value(g, rhs, locals)) emit_rc_retain_for_type(g, type, v);
    LLVMBuildStore(g->builder, v, field_ptr);
    emit_rc_release_for_type(g, type, old);
}

/* Emit: when `cond` (an i1) is true at runtime, print `msg` and exit(1), then
 * continue in a fresh block. Shared by fopen and every other I/O return-value
 * guard so a failed syscall never falls through with an invalid result. */
static void emit_io_abort_if(zan_irgen_t *g, LLVMValueRef cond, const char *msg) {
    if (!g->current_fn) return;
    LLVMBasicBlockRef fail_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "io.fail");
    LLVMBasicBlockRef cont_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "io.cont");
    LLVMBuildCondBr(g->builder, cond, fail_bb, cont_bb);
    LLVMPositionBuilderAtEnd(g->builder, fail_bb);
    LLVMValueRef text = LLVMBuildGlobalStringPtr(g->builder, msg, "ioerr");
    LLVMValueRef fmt = LLVMBuildGlobalStringPtr(g->builder, "%s", "ioerr_fmt");
    LLVMValueRef pargs[] = { fmt, text };
    zan_call2(g->builder, g->printf_type, g->fn_printf, pargs, 2, "");
    LLVMValueRef code = LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 1, 0);
    zan_call2(g->builder, g->exit_type, g->fn_exit, &code, 1, "");
    LLVMBuildUnreachable(g->builder);
    LLVMPositionBuilderAtEnd(g->builder, cont_bb);
}

static void emit_fopen_check(zan_irgen_t *g, LLVMValueRef fp, const char *msg) {
    if (!g->current_fn) return;
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMValueRef isnull = zan_icmp(g->builder, LLVMIntEQ, fp,
        LLVMConstPointerNull(i8ptr), "fp.isnull");
    emit_io_abort_if(g, isnull, msg);
}

/* Emit a runtime guard: when `is_error` is true at runtime, print
 * "<file>:<line>:<col>: runtime error: <msg>" and exit(70). Execution
 * continues (in a fresh block) on the non-error path. No-op when runtime
 * checks are disabled. Must be called with the builder inside `current_fn`. */
static void emit_runtime_check(zan_irgen_t *g, LLVMValueRef is_error,
                               zan_loc_t loc, const char *msg) {
    if (!g->runtime_checks || !g->current_fn) return;

    LLVMBasicBlockRef panic_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "rt.panic");
    LLVMBasicBlockRef cont_bb  = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "rt.cont");
    LLVMBuildCondBr(g->builder, is_error, panic_bb, cont_bb);

    LLVMPositionBuilderAtEnd(g->builder, panic_bb);
    char buf[640];
    const char *file = loc_site_file(g, loc);
    snprintf(buf, sizeof(buf), "%s:%u:%u: runtime error: %s\n",
             file, loc.line, loc.col, msg);
    LLVMValueRef text = LLVMBuildGlobalStringPtr(g->builder, buf, "rterr");
    LLVMValueRef fmt = LLVMBuildGlobalStringPtr(g->builder, "%s", "rterr_fmt");
    LLVMValueRef pargs[] = { fmt, text };
    zan_call2(g->builder, g->printf_type, g->fn_printf, pargs, 2, "");
    LLVMValueRef code = LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 70, 0);
    zan_call2(g->builder, g->exit_type, g->fn_exit, &code, 1, "");
    LLVMBuildUnreachable(g->builder);

    LLVMPositionBuilderAtEnd(g->builder, cont_bb);
}

static LLVMValueRef emit_index_i64(zan_irgen_t *g, LLVMValueRef value,
                                   const char *name) {
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeKind kind = LLVMGetTypeKind(LLVMTypeOf(value));
    if (kind != LLVMIntegerTypeKind) return value;
    unsigned width = LLVMGetIntTypeWidth(LLVMTypeOf(value));
    if (width < 64) return LLVMBuildSExt(g->builder, value, i64, name);
    if (width > 64) return LLVMBuildTrunc(g->builder, value, i64, name);
    return value;
}

static void emit_index_range_check(zan_irgen_t *g, LLVMValueRef index,
                                   LLVMValueRef length, bool allow_end,
                                   zan_loc_t loc, const char *kind) {
    if (!g->runtime_checks || !g->current_fn) return;
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    index = emit_index_i64(g, index, "idx.i64");
    length = emit_index_i64(g, length, "len.i64");
    LLVMValueRef negative = zan_icmp(g->builder, LLVMIntSLT, index,
                                     LLVMConstInt(i64, 0, 0), "idx.neg");
    LLVMIntPredicate upper_pred = allow_end ? LLVMIntSGT : LLVMIntSGE;
    LLVMValueRef outside = zan_icmp(g->builder, upper_pred, index, length,
                                    "idx.high");
    LLVMValueRef bad = LLVMBuildOr(g->builder, negative, outside, "idx.bad");
    char msg[96];
    snprintf(msg, sizeof(msg), "%s index out of bounds", kind ? kind : "array");
    emit_runtime_check(g, bad, loc, msg);
}

static void emit_index_bounds_check(zan_irgen_t *g, LLVMValueRef index,
                                    LLVMValueRef length, zan_loc_t loc,
                                    const char *kind) {
    emit_index_range_check(g, index, length, false, loc, kind);
}

/* Length of a `string`-typed payload for bounds checking. Strings, byte[] and
 * bare C pointers all share the payload pointer ABI, and the second header
 * word says which one this is: ZAN_ARRAY_MAGIC marks a byte buffer, whose
 * carried element count is used so embedded NUL bytes do not truncate
 * indexing bounds. Everything else -- a managed string, or a pointer an extern
 * returned, which has no Zan header at all -- is NUL-terminated and measured
 * with strlen, the same definition `string.Length` uses. Never read the count
 * word for those: for a foreign pointer it is whatever the C allocator left in
 * front of the buffer, which used to reject every valid index. */
static LLVMValueRef emit_string_buffer_len(zan_irgen_t *g, LLVMValueRef payload) {
    LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMValueRef back = LLVMConstInt(i64, (uint64_t)ZAN_OBJ_RC_OFF, 1);
    LLVMValueRef header = LLVMBuildGEP2(g->builder, i8, payload, &back, 1,
                                        "strbuf.header");
    LLVMValueRef count = LLVMBuildLoad2(g->builder, i64,
        LLVMBuildBitCast(g->builder, header, LLVMPointerType(i64, 0),
                         "strbuf.countp"), "strbuf.count");
    LLVMValueRef magic_ptr = LLVMBuildGEP2(g->builder, i8, payload,
        &(LLVMValueRef){ LLVMConstInt(i64, (uint64_t)ZAN_OBJ_SITE_OFF, 1) }, 1,
        "strbuf.magicp");
    LLVMValueRef magic = LLVMBuildLoad2(g->builder, i64,
        LLVMBuildBitCast(g->builder, magic_ptr, LLVMPointerType(i64, 0),
                         "strbuf.magicc"), "strbuf.magic");
    LLVMValueRef is_array = zan_icmp(g->builder, LLVMIntEQ, magic,
        LLVMConstInt(i64, ZAN_ARRAY_MAGIC, 0), "strbuf.isarr");
    LLVMTypeRef strlen_ty = LLVMFunctionType(i64,
        (LLVMTypeRef[]){ LLVMPointerType(i8, 0) }, 1, 0);
    LLVMValueRef string_len = zan_call2(g->builder, strlen_ty, g->fn_strlen,
                                        &payload, 1, "strbuf.strlen");
    return LLVMBuildSelect(g->builder, is_array, count, string_len,
                           "strbuf.len");
}

static void emit_span_window_check(zan_irgen_t *g, LLVMValueRef start,
                                   LLVMValueRef window, LLVMValueRef length,
                                   zan_loc_t loc) {
    if (!g->runtime_checks || !g->current_fn) return;
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    start = emit_index_i64(g, start, "span.start");
    window = emit_index_i64(g, window, "span.count");
    length = emit_index_i64(g, length, "span.length");
    LLVMValueRef bad_start = zan_icmp(g->builder, LLVMIntSLT, start,
                                      LLVMConstInt(i64, 0, 0), "span.start.neg");
    LLVMValueRef bad_window = zan_icmp(g->builder, LLVMIntSLT, window,
                                      LLVMConstInt(i64, 0, 0), "span.count.neg");
    LLVMValueRef after_end = zan_icmp(g->builder, LLVMIntSGT, start, length,
                                      "span.start.high");
    LLVMValueRef remaining = LLVMBuildSub(g->builder, length, start,
                                          "span.remaining");
    LLVMValueRef too_long = zan_icmp(g->builder, LLVMIntSGT, window, remaining,
                                     "span.count.high");
    LLVMValueRef bad = LLVMBuildOr(g->builder, bad_start, bad_window, "span.bad0");
    bad = LLVMBuildOr(g->builder, bad, after_end, "span.bad1");
    bad = LLVMBuildOr(g->builder, bad, too_long, "span.bad2");
    emit_runtime_check(g, bad, loc, "span range out of bounds");
}

/* Resolve the base pointer to a struct/class instance held by a local.
 * A class local may be stored either inline (alloca of the struct itself,
 * a value-like representation) or as a pointer (alloca of a pointer to a
 * heap object). Inspect the allocated type and load through the pointer
 * only in the latter case, so field GEPs work for both representations. */
static LLVMValueRef struct_base_ptr(zan_irgen_t *g, local_var_t *local, LLVMTypeRef st) {
    LLVMValueRef base = local->alloca;
    if (LLVMIsAAllocaInst(base)) {
        LLVMTypeRef alloc_t = LLVMGetAllocatedType(base);
        if (LLVMGetTypeKind(alloc_t) == LLVMPointerTypeKind) {
            base = LLVMBuildLoad2(g->builder, LLVMPointerType(st, 0), base, "objld");
        }
    }
    return base;
}

/* Coerce two integer operands to their wider common width via sign extension.
 * No-op unless both operands are integers of differing widths (floats, pointers
 * and equal-width integers pass through). This lets mixed-width integer
 * expressions -- e.g. an i32 `int` loop variable compared against an i64
 * `List.Count`/`string.Length` -- generate valid IR. */
static void coerce_int_pair(zan_irgen_t *g, LLVMValueRef *a, LLVMValueRef *b) {
    LLVMTypeRef ta = LLVMTypeOf(*a), tb = LLVMTypeOf(*b);
    /* `float op double` computes in double, as in C#; only float op float
     * stays 32-bit. */
    if (LLVMGetTypeKind(ta) == LLVMFloatTypeKind &&
        LLVMGetTypeKind(tb) == LLVMDoubleTypeKind) {
        *a = LLVMBuildFPExt(g->builder, *a, tb, "fpair.ext");
        return;
    }
    if (LLVMGetTypeKind(tb) == LLVMFloatTypeKind &&
        LLVMGetTypeKind(ta) == LLVMDoubleTypeKind) {
        *b = LLVMBuildFPExt(g->builder, *b, ta, "fpair.ext");
        return;
    }
    /* `d * 2` computes in floating point, as in C#: the integer side is
     * converted. Without this the two operands had different LLVM types and the
     * binary operator failed verification ("Both operands to a binary operator
     * are not of the same type"). i1 stays out of it -- a bool is not a number
     * here. */
    if ((LLVMGetTypeKind(ta) == LLVMDoubleTypeKind ||
         LLVMGetTypeKind(ta) == LLVMFloatTypeKind) &&
        LLVMGetTypeKind(tb) == LLVMIntegerTypeKind &&
        LLVMGetIntTypeWidth(tb) > 1) {
        *b = LLVMBuildSIToFP(g->builder, *b, ta, "fpair.sitofp");
        return;
    }
    if ((LLVMGetTypeKind(tb) == LLVMDoubleTypeKind ||
         LLVMGetTypeKind(tb) == LLVMFloatTypeKind) &&
        LLVMGetTypeKind(ta) == LLVMIntegerTypeKind &&
        LLVMGetIntTypeWidth(ta) > 1) {
        *a = LLVMBuildSIToFP(g->builder, *a, tb, "fpair.sitofp");
        return;
    }
    if (LLVMGetTypeKind(ta) != LLVMIntegerTypeKind ||
        LLVMGetTypeKind(tb) != LLVMIntegerTypeKind) return;
    unsigned wa = LLVMGetIntTypeWidth(ta), wb = LLVMGetIntTypeWidth(tb);
    if (wa == wb) return;
    if (wa < wb) *a = zan_iwiden(g->builder, *a, tb);
    else         *b = zan_iwiden(g->builder, *b, ta);
}

/* Normalize an integer value to a target integer type before storing it into a
 * slot of that type, so a width mismatch (e.g. an i32-producing helper feeding
 * an i64 `int` local) never produces invalid/undefined IR. Non-integer or
 * matching-width values, and non-integer targets, pass through unchanged. */
static LLVMValueRef coerce_int_to(zan_irgen_t *g, LLVMValueRef v, LLVMTypeRef target) {
    LLVMTypeRef vt = LLVMTypeOf(v);
    /* A payload value meeting a nullable slot is wrapped, and the null literal
     * (a null pointer) becomes the none value: `int? v = 5;` and `v = null;`
     * both go through here, as does every argument, return and field store. */
    if (llvm_is_nullable(target) && vt != target) {
        if (LLVMGetTypeKind(vt) == LLVMPointerTypeKind)
            return LLVMIsNull(v) ? nullable_none(target) : v;
        if (llvm_is_nullable(vt)) {
            /* One nullable form meeting another: `a + 1` on an `int?` computes
             * its payload in the i64 register form, so only the payload is
             * converted -- the has-value flag carries over as it is. */
            LLVMTypeRef pl = nullable_payload_type(target);
            LLVMValueRef fit = coerce_int_to(g, nullable_get_payload(g, v), pl);
            if (LLVMTypeOf(fit) != pl) return v;
            LLVMValueRef agg = LLVMBuildInsertValue(g->builder,
                LLVMGetUndef(target), fit, 0, "nv.val");
            return LLVMBuildInsertValue(g->builder, agg,
                nullable_has_value(g, v), 1, "nv.fit");
        }
        LLVMValueRef wrapped = nullable_some(g, target, v);
        return wrapped ? wrapped : v;
    }
    /* `float` slots hold f32 while every literal and arithmetic result is a
     * double, so a value is rounded on the way in and widened on the way out;
     * storing the f64 bits raw wrote a denormal (which printed as 0). */
    if (LLVMGetTypeKind(target) == LLVMFloatTypeKind &&
        LLVMGetTypeKind(vt) == LLVMDoubleTypeKind)
        return LLVMBuildFPTrunc(g->builder, v, target, "fit.fptrunc");
    if (LLVMGetTypeKind(target) == LLVMDoubleTypeKind &&
        LLVMGetTypeKind(vt) == LLVMFloatTypeKind)
        return LLVMBuildFPExt(g->builder, v, target, "fit.fpext");
    /* An integer meeting a floating-point slot is converted, not reinterpreted:
     * `double d = 1;`, `a.v = 3;` and `sum = sum + 1` used to store the integer
     * bit pattern into the slot, which read back as a denormal (1 printed as
     * 4.94066e-324) and compared unequal to 1.0. i1 is excluded -- a bool never
     * belongs in a float slot, and taking it here would hide that mistake. */
    if ((LLVMGetTypeKind(target) == LLVMDoubleTypeKind ||
         LLVMGetTypeKind(target) == LLVMFloatTypeKind) &&
        LLVMGetTypeKind(vt) == LLVMIntegerTypeKind &&
        LLVMGetIntTypeWidth(vt) > 1)
        return LLVMBuildSIToFP(g->builder, v, target, "fit.sitofp");
    if (LLVMGetTypeKind(vt) != LLVMIntegerTypeKind ||
        LLVMGetTypeKind(target) != LLVMIntegerTypeKind) return v;
    unsigned wv = LLVMGetIntTypeWidth(vt), wt = LLVMGetIntTypeWidth(target);
    if (wv == wt) return v;
    if (wv < wt) return zan_iwiden(g->builder, v, target);
    return LLVMBuildTrunc(g->builder, v, target, "trunc");
}

/* Store an integer into a slot whose type is statically known (an alloca, a
 * global, or a struct field GEP), fitting the value's width to the slot's
 * first. Opaque pointers make a width mismatch invisible to the verifier, so
 * an i64 stored into an i32 field silently writes four bytes past it; this
 * keeps every such store in range. Non-integer values and slots whose type
 * cannot be recovered (byte-offset GEPs, casts) pass through untouched. */
static LLVMValueRef zan_store_fit(zan_irgen_t *g, LLVMValueRef val, LLVMValueRef ptr) {
    LLVMTypeRef target = NULL;
    if (LLVMIsAAllocaInst(ptr)) {
        target = LLVMGetAllocatedType(ptr);
    } else if (LLVMIsAGlobalVariable(ptr)) {
        target = LLVMGlobalGetValueType(ptr);
    } else if (LLVMIsAGetElementPtrInst(ptr)) {
        LLVMTypeRef src = LLVMGetGEPSourceElementType(ptr);
        if (src && LLVMGetTypeKind(src) == LLVMStructTypeKind &&
            LLVMGetNumOperands(ptr) == 3) {
            LLVMValueRef idx = LLVMGetOperand(ptr, 2);
            if (LLVMIsAConstantInt(idx))
                target = LLVMStructGetTypeAtIndex(
                    src, (unsigned)LLVMConstIntGetZExtValue(idx));
        } else if (src && (LLVMGetTypeKind(src) == LLVMFloatTypeKind ||
                           LLVMGetTypeKind(src) == LLVMDoubleTypeKind)) {
            /* An array element GEP walks the element type, so a `float[]` /
             * `double[]` slot is recoverable here. Only the floating-point cases
             * are taken: a raw byte view (Span, string buffers) is an i8 GEP
             * too, and narrowing the value to its element type there would
             * truncate. */
            target = src;
        }
    }
    /* A nullable slot takes the wrapped form of whatever arrives, including the
     * null literal -- which must not be treated as a struct behind a pointer
     * (the copy below would dereference it). */
    if (target && llvm_is_nullable(target) && LLVMTypeOf(val) != target)
        return LLVMBuildStore(g->builder, coerce_int_to(g, val, target), ptr);
    /* A value struct that arrives behind a pointer (`S s = new S();`, a byref
     * receiver) is copied into the slot. Storing the pointer itself writes
     * eight bytes into a slot that is only as wide as the struct, which
     * overruns anything smaller than a pointer -- a struct holding a single
     * i32 field had its stack neighbour clobbered. */
    if (target && LLVMGetTypeKind(target) == LLVMStructTypeKind &&
        LLVMGetTypeKind(LLVMTypeOf(val)) == LLVMPointerTypeKind)
        val = LLVMBuildLoad2(g->builder, target, val, "sv.copy");
    if (target) val = coerce_int_to(g, val, target);
    /* An erased generic value (an opaque pointer) landing in a slot the
     * specialized body typed concretely (i32 for Box<int>) must be narrowed,
     * or the store writes eight bytes into a four-byte slot and clobbers its
     * stack neighbour. */
    if (target && LLVMTypeOf(val) != target &&
        LLVMGetTypeKind(LLVMTypeOf(val)) == LLVMPointerTypeKind &&
        (LLVMGetTypeKind(target) == LLVMIntegerTypeKind ||
         LLVMGetTypeKind(target) == LLVMFloatTypeKind ||
         LLVMGetTypeKind(target) == LLVMDoubleTypeKind))
        val = emit_boundary_coerce(g, val, target);
    return LLVMBuildStore(g->builder, val, ptr);
}

/* Widen a narrow integer to i64 for numeric formatting/printing: the narrow
 * widths are the unsigned ones, so `true` formats as 1 and a byte of 255 as
 * 255 rather than as -1. */
static LLVMValueRef emit_widen_i64_for_print(zan_irgen_t *g, LLVMValueRef v) {
    LLVMTypeRef vt = LLVMTypeOf(v);
    if (LLVMGetTypeKind(vt) != LLVMIntegerTypeKind) return v;
    unsigned w = LLVMGetIntTypeWidth(vt);
    if (w >= 64) return v;
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    return zan_iwiden(g->builder, v, i64);
}

static LLVMValueRef emit_string_alloc_rc(zan_irgen_t *g, LLVMValueRef payload_size) {
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMValueRef thirty_one = LLVMConstInt(i64, 31, 0);
    LLVMValueRef padded = zan_add(g->builder, payload_size, thirty_one, "str.pad");
    LLVMValueRef aligned = zan_and(g->builder, padded,
        LLVMConstInt(i64, ~UINT64_C(31), 0), "str.align");
    LLVMValueRef min_payload = LLVMConstInt(i64, 32, 0);
    LLVMValueRef payload = LLVMBuildSelect(g->builder,
        zan_icmp(g->builder, LLVMIntULT, aligned, min_payload, "str.small"),
        min_payload, aligned, "str.payload");
    LLVMValueRef user_ptr = zan_call2(g->builder,
        LLVMFunctionType(LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0),
                         (LLVMTypeRef[]){ i64 }, 1, 0),
        g->rt_str_alloc, &payload, 1, "str.raw");
    return user_ptr;
}

static LLVMValueRef emit_widen_i64_for_print(zan_irgen_t *g, LLVMValueRef v);
static LLVMValueRef emit_string_literal_rc(zan_irgen_t *g, zan_istr_t text);

/* Convert a scalar (int/float/bool/char) to a NUL-terminated C string in a
 * stack buffer so it can feed byte-level string machinery (StringBuilder
 * append, etc.). Pointer (string) values are returned unchanged. */
static LLVMValueRef emit_value_as_cstr(zan_irgen_t *g, LLVMValueRef v) {
    LLVMTypeRef vt = LLVMTypeOf(v);
    if (LLVMGetTypeKind(vt) == LLVMPointerTypeKind) return v;
    LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    if (LLVMGetTypeKind(vt) == LLVMIntegerTypeKind && LLVMGetIntTypeWidth(vt) == 1) {
        zan_istr_t t = { "true", 4 }, f = { "false", 5 };
        return LLVMBuildSelect(g->builder, v,
            emit_string_literal_rc(g, t), emit_string_literal_rc(g, f), "b2s");
    }
    LLVMValueRef buf = LLVMBuildArrayAlloca(g->builder, i8,
        LLVMConstInt(i64, 40, 0), "v2s.buf");
    const char *fmt;
    LLVMValueRef arg;
    if (LLVMGetTypeKind(vt) == LLVMDoubleTypeKind || LLVMGetTypeKind(vt) == LLVMFloatTypeKind) {
        fmt = "%g";
        arg = (LLVMGetTypeKind(vt) == LLVMFloatTypeKind)
            ? LLVMBuildFPExt(g->builder, v, LLVMDoubleTypeInContext(g->ctx), "f2d") : v;
    } else {
        fmt = "%lld";
        arg = emit_widen_i64_for_print(g, v);
    }
    zan_istr_t fs = { fmt, (int)strlen(fmt) };
    LLVMValueRef fmt_ptr = emit_string_literal_rc(g, fs);
    LLVMTypeRef snp_ty = LLVMFunctionType(LLVMInt32TypeInContext(g->ctx),
        (LLVMTypeRef[]){ i8ptr, i64, i8ptr }, 3, 1);
    zan_call2(g->builder, snp_ty, g->fn_snprintf,
        (LLVMValueRef[]){ buf, LLVMConstInt(i64, 40, 0), fmt_ptr, arg }, 4, "");
    return buf;
}

/* Append slen bytes from s to the StringBuilder pointed to by sbp (typed
 * pointer to g->sb_struct_type), growing the backing buffer as needed. */
static void emit_sb_append_bytes(zan_irgen_t *g, LLVMValueRef sbp,
                                 LLVMValueRef s, LLVMValueRef slen) {
    LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMValueRef cptr = LLVMBuildStructGEP2(g->builder, g->sb_struct_type, sbp, 0, "sbcp");
    LLVMValueRef count = LLVMBuildLoad2(g->builder, i64, cptr, "sbcv");
    LLVMValueRef capptr = LLVMBuildStructGEP2(g->builder, g->sb_struct_type, sbp, 1, "sbcapp");
    LLVMValueRef cap = LLVMBuildLoad2(g->builder, i64, capptr, "sbcapv");
    LLVMValueRef need = zan_add(g->builder, count, slen, "sbneed");
    LLVMValueRef full = zan_icmp(g->builder, LLVMIntSGT, need, cap, "sbfull");
    LLVMBasicBlockRef grow_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "sb.grow");
    LLVMBasicBlockRef st_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "sb.st");
    LLVMBuildCondBr(g->builder, full, grow_bb, st_bb);
    /* grow: newcap = max(cap*2, need); realloc data */
    LLVMPositionBuilderAtEnd(g->builder, grow_bb);
    LLVMValueRef nc0 = zan_mul(g->builder, cap, LLVMConstInt(i64, 2, 0), "sbnc0");
    LLVMValueRef small = zan_icmp(g->builder, LLVMIntSLT, nc0, need, "sbsm");
    LLVMValueRef nc = LLVMBuildSelect(g->builder, small, need, nc0, "sbnc");
    LLVMValueRef dptr = LLVMBuildStructGEP2(g->builder, g->sb_struct_type, sbp, 2, "sbdp");
    LLVMValueRef olddata = LLVMBuildLoad2(g->builder, i8ptr, dptr, "sbod");
    LLVMValueRef newdata = zan_call2(g->builder,
        LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i64 }, 2, 0),
        g->fn_realloc, (LLVMValueRef[]){ olddata, nc }, 2, "sbnd");
    LLVMBuildStore(g->builder, newdata, dptr);
    LLVMBuildStore(g->builder, nc, capptr);
    LLVMBuildBr(g->builder, st_bb);
    /* store: memcpy(data+count, s, slen); count += slen */
    LLVMPositionBuilderAtEnd(g->builder, st_bb);
    LLVMValueRef dptr2 = LLVMBuildStructGEP2(g->builder, g->sb_struct_type, sbp, 2, "sbdp2");
    LLVMValueRef data = LLVMBuildLoad2(g->builder, i8ptr, dptr2, "sbdv");
    LLVMValueRef dest = LLVMBuildGEP2(g->builder, i8, data, &count, 1, "sbdest");
    LLVMValueRef memcpy_fn = LLVMGetNamedFunction(g->mod, "memcpy");
    LLVMTypeRef memcpy_ty = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr, i64 }, 3, 0);
    if (!memcpy_fn) memcpy_fn = LLVMAddFunction(g->mod, "memcpy", memcpy_ty);
    zan_call2(g->builder, memcpy_ty, memcpy_fn,
        (LLVMValueRef[]){ dest, s, slen }, 3, "");
    LLVMValueRef ncount = zan_add(g->builder, count, slen, "sbncount");
    LLVMBuildStore(g->builder, ncount, cptr);
}

static LLVMValueRef emit_string_literal_rc(zan_irgen_t *g, zan_istr_t text) {
    for (int i = 0; i < g->string_literal_count; i++) {
        if ((size_t)g->string_literals[i].text.len == (size_t)text.len &&
            memcmp(g->string_literals[i].text.str, text.str, (size_t)text.len) == 0) {
            LLVMValueRef global = g->string_literals[i].value;
            LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
            LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
            LLVMTypeRef lit_ty = LLVMArrayType(i8, (unsigned)(16u + (size_t)text.len + 1u));
            LLVMValueRef idxs[] = { LLVMConstInt(i64, 0, 0), LLVMConstInt(i64, 16, 0) };
            return LLVMBuildGEP2(g->builder, lit_ty, global, idxs, 2, "str.lit.ptr");
        }
    }

    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
    size_t total = 16u + (size_t)text.len + 1u;
    LLVMTypeRef lit_ty = LLVMArrayType(i8, (unsigned)total);
    char name[64];
    snprintf(name, sizeof(name), "__zan.strlit.%d", g->string_literal_count);
    LLVMValueRef global = LLVMAddGlobal(g->mod, lit_ty, name);
    LLVMSetLinkage(global, LLVMPrivateLinkage);
    LLVMSetUnnamedAddr(global, LLVMGlobalUnnamedAddr);
    unsigned char *blob = (unsigned char *)calloc(total, 1);
    if (!blob) return LLVMConstNull(LLVMPointerType(i8, 0));
    memcpy(blob + 0, &((uint64_t){ ZAN_STRING_SENTINEL_RC }), 8);
    memcpy(blob + 8, &((uint64_t){ ZAN_STRING_MAGIC }), 8);
    if (text.len > 0) memcpy(blob + 16, text.str, (size_t)text.len);
    blob[16 + (size_t)text.len] = 0;
    /* --publish: XOR-scramble the text bytes in the image and record the
     * global so the startup constructor can un-scramble it. Only when there is
     * room to record (else the byte would never be restored) -- overflow just
     * leaves that literal in plain text, never garbled at runtime. The header
     * (RC sentinel + magic) and NUL stay intact. The global must be writable
     * for the constructor to patch it, so it is not marked constant here. */
    bool obf = g->obfuscate_strings && text.len > 0
        && g->obf_literal_count <
           (int)(sizeof(g->obf_literals) / sizeof(g->obf_literals[0]));
    if (obf) {
        for (size_t j = 0; j < (size_t)text.len; j++) {
            unsigned char ks = g->obf_key[j & 15]
                ^ (unsigned char)((unsigned)text.len * 31u + (unsigned)j * 89u);
            blob[16 + j] = (unsigned char)(blob[16 + j] ^ ks);
        }
        g->obf_literals[g->obf_literal_count].global = global;
        g->obf_literals[g->obf_literal_count].len = (uint32_t)text.len;
        g->obf_literal_count++;
    }
    LLVMSetGlobalConstant(global, obf ? 0 : 1);
    LLVMValueRef *init_elems = (LLVMValueRef *)calloc(total, sizeof(LLVMValueRef));
    if (!init_elems) {
        free(blob);
        return LLVMConstNull(LLVMPointerType(i8, 0));
    }
    for (size_t i = 0; i < total; i++) {
        init_elems[i] = LLVMConstInt(i8, blob[i], 0);
    }
    LLVMValueRef init = LLVMConstArray(i8, init_elems, (unsigned)total);
    LLVMSetInitializer(global, init);
    free(init_elems);
    free(blob);

    LLVMValueRef idxs[] = { LLVMConstInt(i64, 0, 0), LLVMConstInt(i64, 16, 0) };
    LLVMValueRef user_ptr = LLVMBuildGEP2(g->builder, lit_ty, global, idxs, 2, "str.lit.ptr");

    if (g->string_literal_count < (int)(sizeof(g->string_literals) / sizeof(g->string_literals[0]))) {
        g->string_literals[g->string_literal_count].text = text;
        g->string_literals[g->string_literal_count].value = global;
        g->string_literal_count++;
    }
    return user_ptr;
}

/* A string operand may be NULL at the IR level (an unassigned string local
 * reads as NULL); C# semantics treat it as "" for comparisons, ordering and
 * concatenation, so strcmp/strlen must never receive a NULL pointer. Coerce
 * a NULL string to the empty literal before it reaches the runtime. */
static LLVMValueRef emit_str_nonnull(zan_irgen_t *g, LLVMValueRef v) {
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMValueRef empty = emit_string_literal_rc(g, (zan_istr_t){ "", 0 });
    LLVMValueRef isnull = zan_icmp(g->builder, LLVMIntEQ, v,
        LLVMConstNull(i8ptr), "s.null");
    return LLVMBuildSelect(g->builder, isnull, empty, v, "s.nn");
}

void zan_irgen_emit_string_deobf(zan_irgen_t *g) {
    if (!g->obfuscate_strings || g->obf_literal_count <= 0) return;
    int n = g->obf_literal_count;
    LLVMTypeRef i8  = LLVMInt8TypeInContext(g->ctx);
    LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef ptr = LLVMPointerType(i8, 0);

    /* key pad -> [16 x i8] constant */
    LLVMValueRef keyb[16];
    for (int i = 0; i < 16; i++) keyb[i] = LLVMConstInt(i8, g->obf_key[i], 0);
    LLVMTypeRef keyty = LLVMArrayType(i8, 16);
    LLVMValueRef keyg = LLVMAddGlobal(g->mod, keyty, "__zan.obf.key");
    LLVMSetLinkage(keyg, LLVMPrivateLinkage);
    LLVMSetGlobalConstant(keyg, 1);
    LLVMSetUnnamedAddr(keyg, LLVMGlobalUnnamedAddr);
    LLVMSetInitializer(keyg, LLVMConstArray(i8, keyb, 16));

    /* parallel tables: ptrs[n] (each -> the literal's text at offset 16) and
     * lens[n]; kept as constants (they only hold addresses + lengths). */
    LLVMValueRef *pinit = (LLVMValueRef *)calloc((size_t)n, sizeof(LLVMValueRef));
    LLVMValueRef *linit = (LLVMValueRef *)calloc((size_t)n, sizeof(LLVMValueRef));
    if (!pinit || !linit) { free(pinit); free(linit); return; }
    for (int i = 0; i < n; i++) {
        uint32_t len = g->obf_literals[i].len;
        size_t total = 16u + (size_t)len + 1u;
        LLVMTypeRef lit_ty = LLVMArrayType(i8, (unsigned)total);
        LLVMValueRef idx[] = { LLVMConstInt(i64, 0, 0), LLVMConstInt(i64, 16, 0) };
        pinit[i] = LLVMConstGEP2(lit_ty, g->obf_literals[i].global, idx, 2);
        linit[i] = LLVMConstInt(i64, len, 0);
    }
    LLVMTypeRef ptab_ty = LLVMArrayType(ptr, (unsigned)n);
    LLVMTypeRef ltab_ty = LLVMArrayType(i64, (unsigned)n);
    LLVMValueRef ptab = LLVMAddGlobal(g->mod, ptab_ty, "__zan.obf.ptrs");
    LLVMValueRef ltab = LLVMAddGlobal(g->mod, ltab_ty, "__zan.obf.lens");
    LLVMSetLinkage(ptab, LLVMPrivateLinkage); LLVMSetGlobalConstant(ptab, 1);
    LLVMSetLinkage(ltab, LLVMPrivateLinkage); LLVMSetGlobalConstant(ltab, 1);
    LLVMSetInitializer(ptab, LLVMConstArray(ptr, pinit, (unsigned)n));
    LLVMSetInitializer(ltab, LLVMConstArray(i64, linit, (unsigned)n));
    free(pinit); free(linit);

    /* void __zan.deobf(void): for each literal, XOR its bytes back. */
    LLVMTypeRef fnty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), NULL, 0, 0);
    LLVMValueRef fn = LLVMAddFunction(g->mod, "__zan.deobf", fnty);
    LLVMSetLinkage(fn, LLVMInternalLinkage);
    LLVMBuilderRef b = LLVMCreateBuilderInContext(g->ctx);

    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
    LLVMBasicBlockRef ih    = LLVMAppendBasicBlockInContext(g->ctx, fn, "ih");
    LLVMBasicBlockRef ib    = LLVMAppendBasicBlockInContext(g->ctx, fn, "ib");
    LLVMBasicBlockRef jh    = LLVMAppendBasicBlockInContext(g->ctx, fn, "jh");
    LLVMBasicBlockRef jb    = LLVMAppendBasicBlockInContext(g->ctx, fn, "jb");
    LLVMBasicBlockRef inext = LLVMAppendBasicBlockInContext(g->ctx, fn, "inext");
    LLVMBasicBlockRef done  = LLVMAppendBasicBlockInContext(g->ctx, fn, "done");

    LLVMPositionBuilderAtEnd(b, entry);
    LLVMValueRef ia = LLVMBuildAlloca(b, i64, "i");
    LLVMValueRef ja = LLVMBuildAlloca(b, i64, "j");
    LLVMValueRef pa = LLVMBuildAlloca(b, ptr, "p");
    LLVMValueRef la = LLVMBuildAlloca(b, i64, "L");
    LLVMBuildStore(b, LLVMConstInt(i64, 0, 0), ia);
    LLVMBuildBr(b, ih);

    LLVMValueRef nconst = LLVMConstInt(i64, (unsigned long long)n, 0);
    LLVMPositionBuilderAtEnd(b, ih);
    LLVMValueRef iv = LLVMBuildLoad2(b, i64, ia, "iv");
    LLVMValueRef icmp = LLVMBuildICmp(b, LLVMIntSLT, iv, nconst, "icmp");
    LLVMBuildCondBr(b, icmp, ib, done);

    LLVMPositionBuilderAtEnd(b, ib);
    iv = LLVMBuildLoad2(b, i64, ia, "iv2");
    LLVMValueRef pidx[] = { LLVMConstInt(i64, 0, 0), iv };
    LLVMValueRef pslot = LLVMBuildGEP2(b, ptab_ty, ptab, pidx, 2, "pslot");
    LLVMValueRef pv = LLVMBuildLoad2(b, ptr, pslot, "pv");
    LLVMBuildStore(b, pv, pa);
    LLVMValueRef lslot = LLVMBuildGEP2(b, ltab_ty, ltab, pidx, 2, "lslot");
    LLVMValueRef lv = LLVMBuildLoad2(b, i64, lslot, "lv");
    LLVMBuildStore(b, lv, la);
    LLVMBuildStore(b, LLVMConstInt(i64, 0, 0), ja);
    LLVMBuildBr(b, jh);

    LLVMPositionBuilderAtEnd(b, jh);
    LLVMValueRef jv = LLVMBuildLoad2(b, i64, ja, "jv");
    LLVMValueRef Lv = LLVMBuildLoad2(b, i64, la, "Lv");
    LLVMValueRef jcmp = LLVMBuildICmp(b, LLVMIntSLT, jv, Lv, "jcmp");
    LLVMBuildCondBr(b, jcmp, jb, inext);

    LLVMPositionBuilderAtEnd(b, jb);
    jv = LLVMBuildLoad2(b, i64, ja, "jv2");
    Lv = LLVMBuildLoad2(b, i64, la, "Lv2");
    LLVMValueRef pcur = LLVMBuildLoad2(b, ptr, pa, "pcur");
    LLVMValueRef bp = LLVMBuildGEP2(b, i8, pcur, &jv, 1, "bp");
    LLVMValueRef cur = LLVMBuildLoad2(b, i8, bp, "cur");
    /* ks = key[j & 15] ^ (i8)(L*31 + j*89) */
    LLVMValueRef ki = LLVMBuildAnd(b, jv, LLVMConstInt(i64, 15, 0), "ki");
    LLVMValueRef kidx[] = { LLVMConstInt(i64, 0, 0), ki };
    LLVMValueRef kslot = LLVMBuildGEP2(b, keyty, keyg, kidx, 2, "kslot");
    LLVMValueRef kb = LLVMBuildLoad2(b, i8, kslot, "kb");
    LLVMValueRef m1 = LLVMBuildMul(b, Lv, LLVMConstInt(i64, 31, 0), "m1");
    LLVMValueRef m2 = LLVMBuildMul(b, jv, LLVMConstInt(i64, 89, 0), "m2");
    LLVMValueRef ms = LLVMBuildAdd(b, m1, m2, "ms");
    LLVMValueRef mt = LLVMBuildTrunc(b, ms, i8, "mt");
    LLVMValueRef ks = LLVMBuildXor(b, kb, mt, "ks");
    LLVMValueRef nb = LLVMBuildXor(b, cur, ks, "nb");
    LLVMBuildStore(b, nb, bp);
    LLVMValueRef jn = LLVMBuildAdd(b, jv, LLVMConstInt(i64, 1, 0), "jn");
    LLVMBuildStore(b, jn, ja);
    LLVMBuildBr(b, jh);

    LLVMPositionBuilderAtEnd(b, inext);
    iv = LLVMBuildLoad2(b, i64, ia, "iv3");
    LLVMValueRef in = LLVMBuildAdd(b, iv, LLVMConstInt(i64, 1, 0), "in");
    LLVMBuildStore(b, in, ia);
    LLVMBuildBr(b, ih);

    LLVMPositionBuilderAtEnd(b, done);
    LLVMBuildRetVoid(b);
    LLVMDisposeBuilder(b);

    /* register in llvm.global_ctors: [{ i32 priority, void()* fn, i8* data }] */
    LLVMValueRef fields[] = { LLVMConstInt(i32, 65535, 0), fn, LLVMConstNull(ptr) };
    LLVMValueRef entryc = LLVMConstStruct(fields, 3, 0);
    LLVMTypeRef arrty = LLVMArrayType(LLVMTypeOf(entryc), 1);
    LLVMValueRef gc = LLVMAddGlobal(g->mod, arrty, "llvm.global_ctors");
    LLVMSetLinkage(gc, LLVMAppendingLinkage);
    LLVMSetInitializer(gc, LLVMConstArray(LLVMTypeOf(entryc), &entryc, 1));
}

/* Coerce a value to an i8* C string. Pointers pass through unchanged; integer
 * and floating operands are formatted into a fresh heap buffer via snprintf.
 * Lets `+` concatenate strings with numbers (e.g. "%t" + counter). */
static LLVMValueRef emit_to_cstr(zan_irgen_t *g, LLVMValueRef val) {
    LLVMTypeRef vt = LLVMTypeOf(val);
    LLVMTypeKind vtk = LLVMGetTypeKind(vt);
    if (vtk == LLVMPointerTypeKind) return val;

    /* A nullable formats as its value, or as the empty string when it holds
     * none (C#). The payload is formatted on its own branch so that a none
     * value never allocates a string it would then have to drop. */
    if (llvm_is_nullable(vt)) {
        LLVMTypeRef nv_i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
        LLVMValueRef has = nullable_has_value(g, val);
        LLVMValueRef payload = nullable_get_payload(g, val);
        LLVMValueRef fn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder));
        LLVMBasicBlockRef some_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "nv.str.some");
        LLVMBasicBlockRef none_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "nv.str.none");
        LLVMBasicBlockRef end_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "nv.str.end");
        LLVMBuildCondBr(g->builder, has, some_bb, none_bb);

        LLVMPositionBuilderAtEnd(g->builder, some_bb);
        LLVMValueRef sv = emit_to_cstr(g, payload);
        LLVMBasicBlockRef some_end = LLVMGetInsertBlock(g->builder);
        LLVMBuildBr(g->builder, end_bb);

        LLVMPositionBuilderAtEnd(g->builder, none_bb);
        LLVMValueRef empty = emit_string_alloc_rc(g,
            LLVMConstInt(LLVMInt64TypeInContext(g->ctx), 1, 0));
        LLVMBuildStore(g->builder, LLVMConstInt(LLVMInt8TypeInContext(g->ctx), 0, 0),
                       empty);
        LLVMBasicBlockRef none_end = LLVMGetInsertBlock(g->builder);
        LLVMBuildBr(g->builder, end_bb);

        LLVMPositionBuilderAtEnd(g->builder, end_bb);
        LLVMValueRef phi = LLVMBuildPhi(g->builder, nv_i8ptr, "nv.str");
        LLVMValueRef vals[] = { sv, empty };
        LLVMBasicBlockRef bbs[] = { some_end, none_end };
        LLVMAddIncoming(phi, vals, bbs, 2);
        return phi;
    }

    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
    LLVMTypeRef snprintf_type = LLVMFunctionType(
        LLVMInt32TypeInContext(g->ctx), (LLVMTypeRef[]){ i8ptr, i64, i8ptr }, 3, 1);
    LLVMValueRef fmt;
    LLVMValueRef arg;
    if (vtk == LLVMDoubleTypeKind || vtk == LLVMFloatTypeKind) {
        fmt = LLVMBuildGlobalStringPtr(g->builder, "%g", "dfmt");
        arg = val;
    } else {
        fmt = LLVMBuildGlobalStringPtr(g->builder, "%lld", "ifmt");
        arg = emit_widen_i64_for_print(g, val);
    }
    LLVMValueRef tmp_sz = LLVMConstInt(i64, 1024, 0);
    LLVMValueRef tmp = LLVMBuildArrayAlloca(g->builder, i8, tmp_sz, "fmt.tmp");
    LLVMValueRef a1[] = { tmp, tmp_sz, fmt, arg };
    zan_call2(g->builder, snprintf_type, g->fn_snprintf, a1, 4, "");
    LLVMValueRef needed = zan_call2(g->builder,
        LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr }, 1, 0),
        LLVMGetNamedFunction(g->mod, "strlen"), &tmp, 1, "needed");
    LLVMValueRef bsz = zan_add(g->builder, needed, LLVMConstInt(i64, 1, 0), "bsz");
    LLVMValueRef buf = emit_string_alloc_rc(g, bsz);
    zan_call2(g->builder,
        LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0),
        LLVMGetNamedFunction(g->mod, "strcpy"),
        (LLVMValueRef[]){ buf, tmp }, 2, "");
    return buf;
}

/* Format a `char` as text the way C# does: the character itself, UTF-8 encoded,
 * not its numeric code. The encoded bytes are built as a little-endian packed
 * i64 (unused high bytes stay zero), so one store plus a length-sized memcpy
 * covers all four UTF-8 lengths without branching. */
static LLVMValueRef emit_char_to_cstr(zan_irgen_t *g, LLVMValueRef val) {
    LLVMTypeRef i8t = LLVMInt8TypeInContext(g->ctx);
    LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(i8t, 0);
    LLVMBuilderRef bd = g->builder;

    LLVMValueRef cp = emit_widen_i64_for_print(g, val);
    cp = LLVMBuildAnd(bd, cp, LLVMConstInt(i64t, 0x1FFFFF, 0), "ch.cp");

    LLVMValueRef low6 = LLVMBuildAnd(bd, cp, LLVMConstInt(i64t, 0x3F, 0), "ch.l6");
    LLVMValueRef sh6 = LLVMBuildLShr(bd, cp, LLVMConstInt(i64t, 6, 0), "ch.s6");
    LLVMValueRef sh12 = LLVMBuildLShr(bd, cp, LLVMConstInt(i64t, 12, 0), "ch.s12");
    LLVMValueRef sh18 = LLVMBuildLShr(bd, cp, LLVMConstInt(i64t, 18, 0), "ch.s18");
    LLVMValueRef c6 = LLVMBuildAnd(bd, sh6, LLVMConstInt(i64t, 0x3F, 0), "ch.c6");
    LLVMValueRef c12 = LLVMBuildAnd(bd, sh12, LLVMConstInt(i64t, 0x3F, 0), "ch.c12");

    /* byte(n) = payload | tag, packed at 8*n bits. */
    LLVMValueRef b2 = LLVMBuildOr(bd,
        LLVMBuildOr(bd, LLVMConstInt(i64t, 0xC0, 0), sh6, "ch.b2a"),
        LLVMBuildShl(bd, LLVMBuildOr(bd, LLVMConstInt(i64t, 0x80, 0), low6, "ch.b2b"),
                     LLVMConstInt(i64t, 8, 0), "ch.b2c"), "ch.b2");
    LLVMValueRef b3 = LLVMBuildOr(bd,
        LLVMBuildOr(bd, LLVMConstInt(i64t, 0xE0, 0), sh12, "ch.b3a"),
        LLVMBuildOr(bd,
            LLVMBuildShl(bd, LLVMBuildOr(bd, LLVMConstInt(i64t, 0x80, 0), c6, "ch.b3b"),
                         LLVMConstInt(i64t, 8, 0), "ch.b3c"),
            LLVMBuildShl(bd, LLVMBuildOr(bd, LLVMConstInt(i64t, 0x80, 0), low6, "ch.b3d"),
                         LLVMConstInt(i64t, 16, 0), "ch.b3e"), "ch.b3f"), "ch.b3");
    LLVMValueRef b4 = LLVMBuildOr(bd,
        LLVMBuildOr(bd, LLVMConstInt(i64t, 0xF0, 0), sh18, "ch.b4a"),
        LLVMBuildOr(bd,
            LLVMBuildShl(bd, LLVMBuildOr(bd, LLVMConstInt(i64t, 0x80, 0), c12, "ch.b4b"),
                         LLVMConstInt(i64t, 8, 0), "ch.b4c"),
            LLVMBuildOr(bd,
                LLVMBuildShl(bd, LLVMBuildOr(bd, LLVMConstInt(i64t, 0x80, 0), c6, "ch.b4d"),
                             LLVMConstInt(i64t, 16, 0), "ch.b4e"),
                LLVMBuildShl(bd, LLVMBuildOr(bd, LLVMConstInt(i64t, 0x80, 0), low6, "ch.b4f"),
                             LLVMConstInt(i64t, 24, 0), "ch.b4g"), "ch.b4h"), "ch.b4i"), "ch.b4");

    LLVMValueRef lt80 = LLVMBuildICmp(bd, LLVMIntULT, cp, LLVMConstInt(i64t, 0x80, 0), "ch.lt80");
    LLVMValueRef lt800 = LLVMBuildICmp(bd, LLVMIntULT, cp, LLVMConstInt(i64t, 0x800, 0), "ch.lt800");
    LLVMValueRef lt10000 = LLVMBuildICmp(bd, LLVMIntULT, cp, LLVMConstInt(i64t, 0x10000, 0), "ch.lt1k");
    LLVMValueRef packed = LLVMBuildSelect(bd, lt80, cp,
        LLVMBuildSelect(bd, lt800, b2,
            LLVMBuildSelect(bd, lt10000, b3, b4, "ch.p3"), "ch.p2"), "ch.packed");
    LLVMValueRef len = LLVMBuildSelect(bd, lt80, LLVMConstInt(i64t, 1, 0),
        LLVMBuildSelect(bd, lt800, LLVMConstInt(i64t, 2, 0),
            LLVMBuildSelect(bd, lt10000, LLVMConstInt(i64t, 3, 0),
                            LLVMConstInt(i64t, 4, 0), "ch.n3"), "ch.n2"), "ch.len");
    /* char 0 still yields the empty string, matching a NUL-terminated C string. */

    LLVMValueRef tmp = LLVMBuildAlloca(g->builder, i64t, "ch.tmp");
    LLVMBuildStore(bd, packed, tmp);
    LLVMValueRef buf = emit_string_alloc_rc(g, zan_add(bd, len, LLVMConstInt(i64t, 1, 0), "ch.sz"));
    LLVMTypeRef memcpy_type = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr, i64t }, 3, 0);
    LLVMValueRef memcpy_fn = LLVMGetNamedFunction(g->mod, "memcpy");
    if (!memcpy_fn) memcpy_fn = LLVMAddFunction(g->mod, "memcpy", memcpy_type);
    zan_call2(bd, memcpy_type, memcpy_fn, (LLVMValueRef[]){ buf, tmp, len }, 3, "");
    LLVMValueRef endp = LLVMBuildGEP2(bd, i8t, buf, &len, 1, "ch.end");
    LLVMBuildStore(bd, LLVMConstInt(i8t, 0, 0), endp);
    return buf;
}

/* emit_to_cstr for an operand whose AST is known: `char` formats as the
 * character, every other type keeps the numeric/string formatting. */
static LLVMValueRef emit_to_cstr_of(zan_irgen_t *g, LLVMValueRef val,
                                    zan_ast_node_t *ast, local_scope_t *locals) {
    /* A `T` value read out of a generic instance arrives in the erased pointer
     * slot, so `item + "x"` with T bound to a value type would otherwise run
     * strlen over the bit pattern. Recover the concrete representation first. */
    if (ast && val && LLVMGetTypeKind(LLVMTypeOf(val)) == LLVMPointerTypeKind) {
        zan_type_t *t = infer_expr_type(g, ast, locals);
        LLVMTypeRef want = t ? map_type(g, t) : NULL;
        if (want) {
            LLVMTypeKind wk = LLVMGetTypeKind(want);
            if (wk == LLVMIntegerTypeKind || wk == LLVMFloatTypeKind ||
                wk == LLVMDoubleTypeKind)
                val = emit_boundary_coerce(g, val, want);
        }
    }
    if (ast && LLVMGetTypeKind(LLVMTypeOf(val)) == LLVMIntegerTypeKind &&
        expr_is_char(g, ast, locals)) {
        return emit_char_to_cstr(g, val);
    }
    return emit_to_cstr(g, val);
}

/* Emit `a + b` for two string (i8*) operands as a heap-allocated concatenation:
 * malloc(strlen(a)+strlen(b)+1); memcpy left, memcpy right, NUL terminate.
 * A NULL operand concatenates as "" (C#), so neither strlen may see a NULL. */
static LLVMValueRef emit_str_concat(zan_irgen_t *g, LLVMValueRef a, LLVMValueRef b) {
    LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    a = emit_str_nonnull(g, a);
    b = emit_str_nonnull(g, b);
    LLVMTypeRef strlen_type = LLVMFunctionType(i64t, (LLVMTypeRef[]){ i8ptr }, 1, 0);
    LLVMValueRef la = zan_call2(g->builder, strlen_type, g->fn_strlen, &a, 1, "cla");
    LLVMValueRef lb = zan_call2(g->builder, strlen_type, g->fn_strlen, &b, 1, "clb");
    LLVMValueRef tot = zan_add(g->builder, la, lb, "ct");
    tot = zan_add(g->builder, tot, LLVMConstInt(i64t, 1, 0), "ct1");
    LLVMValueRef buf = emit_string_alloc_rc(g, tot);
    LLVMTypeRef memcpy_type = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr, i64t }, 3, 0);
    LLVMValueRef memcpy_fn = LLVMGetNamedFunction(g->mod, "memcpy");
    if (!memcpy_fn) memcpy_fn = LLVMAddFunction(g->mod, "memcpy", memcpy_type);
    zan_call2(g->builder, memcpy_type, memcpy_fn,
        (LLVMValueRef[]){ buf, a, la }, 3, "");
    LLVMValueRef dst_b = LLVMBuildGEP2(g->builder, LLVMInt8TypeInContext(g->ctx), buf, &la, 1, "dst.b");
    zan_call2(g->builder, memcpy_type, memcpy_fn,
        (LLVMValueRef[]){ dst_b, b, lb }, 3, "");
    LLVMValueRef end_off = zan_add(g->builder, la, lb, "slen");
    LLVMValueRef endp = LLVMBuildGEP2(g->builder, LLVMInt8TypeInContext(g->ctx), buf, &end_off, 1, "end");
    LLVMBuildStore(g->builder, LLVMConstInt(LLVMInt8TypeInContext(g->ctx), 0, 0), endp);
    return buf;
}

/* True when `e` is a string-yielding `+` node (either operand is a string),
 * i.e. a link of a concatenation chain. */
static bool is_str_concat_node(zan_irgen_t *g, zan_ast_node_t *e, local_scope_t *locals) {
    return e && e->kind == AST_BINARY && e->binary.op == TK_PLUS &&
        (is_string_expr(g, e->binary.left, locals) ||
         is_string_expr(g, e->binary.right, locals));
}

/* Collect the leaves of a `+` concatenation chain in evaluation order.
 * Stops descending once `ops` is nearly full; an overflowing sub-chain is
 * kept as a single leaf and flattened again when it is itself emitted. */
static void collect_concat_ops(zan_irgen_t *g, zan_ast_node_t *e, local_scope_t *locals,
                               zan_ast_node_t **ops, int *n, int max) {
    if (*n < max - 1 && is_str_concat_node(g, e, locals)) {
        /* Reserve one slot for the right subtree while descending left, so a
         * long left spine can never fill the array before the pending right
         * leaves are stored (each pending right needs at least one slot). */
        collect_concat_ops(g, e->binary.left, locals, ops, n, max - 1);
        collect_concat_ops(g, e->binary.right, locals, ops, n, max);
    } else {
        ops[(*n)++] = e;
    }
}

/* Emit an n-way string concatenation as a single allocation: sum the operand
 * lengths, allocate once, memcpy each part in order and NUL-terminate.
 * Replaces the O(n^2) copying of emitting a chain pairwise. */
static LLVMValueRef emit_str_concat_n(zan_irgen_t *g, zan_ast_node_t *expr,
                                      local_scope_t *locals) {
    enum { MAXOPS = 48 };
    zan_ast_node_t *ops[MAXOPS];
    int n = 0;
    collect_concat_ops(g, expr, locals, ops, &n, MAXOPS);

    LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i8t = LLVMInt8TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(i8t, 0);
    LLVMTypeRef strlen_type = LLVMFunctionType(i64t, (LLVMTypeRef[]){ i8ptr }, 1, 0);
    LLVMTypeRef memcpy_type = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr, i64t }, 3, 0);
    LLVMValueRef memcpy_fn = LLVMGetNamedFunction(g->mod, "memcpy");
    if (!memcpy_fn) memcpy_fn = LLVMAddFunction(g->mod, "memcpy", memcpy_type);

    LLVMValueRef vals[MAXOPS];
    LLVMValueRef lens[MAXOPS];
    bool owned[MAXOPS];
    LLVMValueRef total = LLVMConstInt(i64t, 1, 0); /* NUL */
    for (int i = 0; i < n; i++) {
        LLVMValueRef v = emit_expr(g, ops[i], locals);
        LLVMValueRef s = emit_to_cstr_of(g, v, ops[i], locals);
        s = emit_str_nonnull(g, s);
        vals[i] = s;
        owned[i] = !is_string_expr(g, ops[i], locals) ||
                   expr_yields_owned_rc_value(g, ops[i], locals);
        lens[i] = zan_call2(g->builder, strlen_type, g->fn_strlen, &s, 1, "cl");
        total = zan_add(g->builder, total, lens[i], "ct");
    }
    LLVMValueRef buf = emit_string_alloc_rc(g, total);
    LLVMValueRef off = LLVMConstInt(i64t, 0, 0);
    for (int i = 0; i < n; i++) {
        LLVMValueRef dst = LLVMBuildGEP2(g->builder, i8t, buf, &off, 1, "dst");
        zan_call2(g->builder, memcpy_type, memcpy_fn,
            (LLVMValueRef[]){ dst, vals[i], lens[i] }, 3, "");
        off = zan_add(g->builder, off, lens[i], "off");
    }
    LLVMValueRef endp = LLVMBuildGEP2(g->builder, i8t, buf, &off, 1, "end");
    LLVMBuildStore(g->builder, LLVMConstInt(i8t, 0, 0), endp);
    for (int i = 0; i < n; i++) {
        if (owned[i]) emit_string_release(g, vals[i]);
    }
    return buf;
}

/* Return the internal `__zan_co_reap` step function, creating it once per
 * module. It matches zan_co_step_t (void(i8*)) and simply frees the frame it is
 * handed. A detached (Task.Spawn) coroutine has no awaiter to hand its frame
 * back to, so it installs this as its own awaiter step: on completion
 * emit_async_complete re-enqueues (frame, __zan_co_reap), and the driver later
 * frees the frame -- otherwise every spawned coroutine leaks its heap frame,
 * the residual per-connection leak in long-running socket servers. */
static LLVMValueRef get_co_reap_fn(zan_irgen_t *g) {
    LLVMValueRef reap = LLVMGetNamedFunction(g->mod, "__zan_co_reap");
    if (reap) return reap;
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef reap_ty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), &i8ptr, 1, 0);
    reap = LLVMAddFunction(g->mod, "__zan_co_reap", reap_ty);
    LLVMSetLinkage(reap, LLVMInternalLinkage);
    LLVMBasicBlockRef saved = LLVMGetInsertBlock(g->builder);
    LLVMTypeRef hdr = g->co_header_type;
    LLVMValueRef arg = LLVMGetParam(reap, 0);

    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, reap, "entry");
    LLVMBasicBlockRef rel_bb = LLVMAppendBasicBlockInContext(g->ctx, reap, "release");
    LLVMBasicBlockRef done = LLVMAppendBasicBlockInContext(g->ctx, reap, "done");
    LLVMPositionBuilderAtEnd(g->builder, entry);
    /* A detached (spawned) coroutine that completed by throwing parks its
     * exception in the frame header; no awaiter will ever take ownership of
     * it, so drop the +1 reference the frame holds before freeing the frame.
     * Class objects go through the RTTI-dispatch releaser, strings through
     * the string releaser (their headers differ). */
    LLVMValueRef exc_p = LLVMBuildStructGEP2(g->builder, hdr, arg,
        ASYNC_FRAME_EXC, "r.exc.p");
    LLVMValueRef exc = LLVMBuildLoad2(g->builder, i8ptr, exc_p, "r.exc");
    LLVMValueRef own_p = LLVMBuildStructGEP2(g->builder, hdr, arg,
        ASYNC_FRAME_EXC_OWNED, "r.own.p");
    LLVMValueRef own = LLVMBuildLoad2(g->builder, i32, own_p, "r.own");
    LLVMValueRef nonnull = zan_icmp(g->builder, LLVMIntNE, exc,
        LLVMConstNull(i8ptr), "r.nonnull");
    LLVMValueRef owns = zan_icmp(g->builder, LLVMIntNE, own,
        LLVMConstInt(i32, 0, 0), "r.owns");
    LLVMBuildCondBr(g->builder, zan_and(g->builder, nonnull, owns, "r.rel"),
        rel_bb, done);

    LLVMPositionBuilderAtEnd(g->builder, rel_bb);
    {
        LLVMValueRef tid_p = LLVMBuildStructGEP2(g->builder, hdr, arg,
            ASYNC_FRAME_EXC_TID, "r.tid.p");
        LLVMValueRef tid = LLVMBuildLoad2(g->builder, i8ptr, tid_p, "r.tid");
        LLVMValueRef is_obj = zan_icmp(g->builder, LLVMIntNE, tid,
            LLVMConstNull(i8ptr), "r.isobj");
        LLVMBasicBlockRef rel_obj = LLVMAppendBasicBlockInContext(g->ctx, reap, "rel.obj");
        LLVMBasicBlockRef rel_str = LLVMAppendBasicBlockInContext(g->ctx, reap, "rel.str");
        LLVMBasicBlockRef rel_done = LLVMAppendBasicBlockInContext(g->ctx, reap, "rel.done");
        LLVMBuildCondBr(g->builder, is_obj, rel_obj, rel_str);

        LLVMPositionBuilderAtEnd(g->builder, rel_obj);
        zan_call2(g->builder, LLVMGlobalGetValueType(g->rt_release_dyn),
            g->rt_release_dyn, &exc, 1, "");
        LLVMBuildBr(g->builder, rel_done);

        LLVMPositionBuilderAtEnd(g->builder, rel_str);
        zan_call2(g->builder, LLVMGlobalGetValueType(g->rt_str_release),
            g->rt_str_release, &exc, 1, "");
        LLVMBuildBr(g->builder, rel_done);

        LLVMPositionBuilderAtEnd(g->builder, rel_done);
        LLVMBuildBr(g->builder, done);
    }

    LLVMPositionBuilderAtEnd(g->builder, done);
    /* drop the frame from the live-handle registry first: a Task.Spawn handle
     * the program kept must stop naming this frame before it is freed */
    LLVMValueRef untrack = get_co_untrack_fn(g);
    zan_call2(g->builder, LLVMGlobalGetValueType(untrack), untrack, &arg, 1, "");
    zan_call2(g->builder, LLVMGlobalGetValueType(g->fn_free), g->fn_free, &arg, 1, "");
    LLVMBuildRetVoid(g->builder);
    if (saved) LLVMPositionBuilderAtEnd(g->builder, saved);
    return reap;
}

/* Return the internal `__zan_async_unwind(i8* self)` helper, creating it once
 * per module. An exception thrown in coroutine `self` longjmps straight to the
 * target handler, skipping every suspended CPS frame in between; those frames
 * (and every rc value they own) would leak. Starting at `self`, walk the
 * awaiter chain calling each frame's $cleanup (releases owned slots + frees
 * the frame), stopping at the first frame with an armed try handler
 * (hcount > 0) -- that frame and everything above it stay live because the
 * handler resumes there. Called right before the throw-site longjmp. */
static LLVMValueRef get_async_unwind_fn(zan_irgen_t *g) {
    LLVMValueRef f = LLVMGetNamedFunction(g->mod, "__zan_async_unwind");
    if (f) return f;
    LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef fty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), &i8ptr, 1, 0);
    f = LLVMAddFunction(g->mod, "__zan_async_unwind", fty);
    LLVMSetLinkage(f, LLVMInternalLinkage);
    LLVMBasicBlockRef saved = LLVMGetInsertBlock(g->builder);
    LLVMTypeRef hdr = g->co_header_type;
    LLVMTypeRef hdr_ptr = LLVMPointerType(hdr, 0);

    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, f, "entry");
    LLVMBasicBlockRef head = LLVMAppendBasicBlockInContext(g->ctx, f, "head");
    LLVMBasicBlockRef body = LLVMAppendBasicBlockInContext(g->ctx, f, "body");
    LLVMBasicBlockRef clean = LLVMAppendBasicBlockInContext(g->ctx, f, "clean");
    LLVMBasicBlockRef call_bb = LLVMAppendBasicBlockInContext(g->ctx, f, "call");
    LLVMBasicBlockRef next_bb = LLVMAppendBasicBlockInContext(g->ctx, f, "next");
    LLVMBasicBlockRef done = LLVMAppendBasicBlockInContext(g->ctx, f, "done");

    LLVMPositionBuilderAtEnd(g->builder, entry);
    LLVMValueRef cur_a = LLVMBuildAlloca(g->builder, i8ptr, "cur.a");
    LLVMValueRef next_a = LLVMBuildAlloca(g->builder, i8ptr, "next.a");
    LLVMBuildStore(g->builder, LLVMGetParam(f, 0), cur_a);
    LLVMBuildBr(g->builder, head);

    LLVMPositionBuilderAtEnd(g->builder, head);
    LLVMValueRef cur = LLVMBuildLoad2(g->builder, i8ptr, cur_a, "cur");
    LLVMValueRef nonnull = zan_icmp(g->builder, LLVMIntNE, cur,
        LLVMConstNull(i8ptr), "cur.nn");
    LLVMBuildCondBr(g->builder, nonnull, body, done);

    LLVMPositionBuilderAtEnd(g->builder, body);
    cur = LLVMBuildLoad2(g->builder, i8ptr, cur_a, "cur");
    LLVMValueRef hf = LLVMBuildBitCast(g->builder, cur, hdr_ptr, "hf");
    LLVMValueRef hc = LLVMBuildLoad2(g->builder, i32,
        LLVMBuildStructGEP2(g->builder, hdr, hf, ASYNC_FRAME_HCOUNT, "hc.p"), "hc");
    LLVMValueRef armed = zan_icmp(g->builder, LLVMIntSGT, hc,
        LLVMConstInt(i32, 0, 0), "hc.armed");
    LLVMBuildCondBr(g->builder, armed, done, clean);

    LLVMPositionBuilderAtEnd(g->builder, clean);
    LLVMValueRef aw = LLVMBuildLoad2(g->builder, i8ptr,
        LLVMBuildStructGEP2(g->builder, hdr, hf, ASYNC_FRAME_AWAITER, "aw.p"), "aw");
    /* a detached (Task.Spawn) coroutine marks itself as its own awaiter */
    LLVMValueRef det = zan_icmp(g->builder, LLVMIntEQ, aw, cur, "aw.det");
    LLVMValueRef nxt = LLVMBuildSelect(g->builder, det,
        LLVMConstNull(i8ptr), aw, "aw.next");
    LLVMBuildStore(g->builder, nxt, next_a);
    LLVMValueRef cl = LLVMBuildLoad2(g->builder, g->co_step_ptr,
        LLVMBuildStructGEP2(g->builder, hdr, hf, ASYNC_FRAME_CLEANUP, "cl.p"), "cl");
    LLVMValueRef has_cl = zan_icmp(g->builder, LLVMIntNE, cl,
        LLVMConstNull(g->co_step_ptr), "cl.nn");
    LLVMBuildCondBr(g->builder, has_cl, call_bb, next_bb);

    LLVMPositionBuilderAtEnd(g->builder, call_bb);
    zan_call2(g->builder, g->co_step_type, cl, &cur, 1, "");
    LLVMBuildBr(g->builder, next_bb);

    LLVMPositionBuilderAtEnd(g->builder, next_bb);
    LLVMValueRef nv = LLVMBuildLoad2(g->builder, i8ptr, next_a, "next");
    LLVMBuildStore(g->builder, nv, cur_a);
    LLVMBuildBr(g->builder, head);

    LLVMPositionBuilderAtEnd(g->builder, done);
    LLVMBuildRetVoid(g->builder);
    if (saved) LLVMPositionBuilderAtEnd(g->builder, saved);
    return f;
}
