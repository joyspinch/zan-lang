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
 * used only inside another specialized generic's body (transitive) is found. */
static void discover_generic_insts(zan_irgen_t *g, zan_ast_node_t *unit) {
    if (!unit || unit->kind != AST_COMPILATION_UNIT) return;
    int prev = -1;
    int guard = 0;
    while (g->generic_inst_count != prev && guard++ < 64) {
        prev = g->generic_inst_count;
        for (int i = 0; i < unit->comp_unit.decls.count; i++) {
            zan_ast_node_t *decl = unit->comp_unit.decls.items[i];
            if (decl->kind == AST_CLASS_DECL || decl->kind == AST_STRUCT_DECL ||
                decl->kind == AST_INTERFACE_DECL) {
                for (int j = 0; j < decl->type_decl.members.count; j++)
                    collect_inst_member(g, decl->type_decl.members.items[j]);
            }
        }
    }
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
    if (!recv_ty || recv_ty->type_arg_count <= 0 || !recv_ty->sym) return erased_fn;
    if (!is_user_generic_sym(recv_ty->sym)) return erased_fn;
    LLVMTypeRef st = NULL;
    LLVMValueRef sfn = find_generic_fn(g, method_sym, recv_ty->type_args,
                                       recv_ty->type_arg_count, &st);
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

static int expr_yields_owned_rc_value(zan_irgen_t *g, zan_ast_node_t *e,
                                      local_scope_t *locals) {
    if (!e) return 0;
    if (e->kind == AST_NEW_EXPR || e->kind == AST_CALL ||
        e->kind == AST_QUERY_EXPR) return 1;
    /* An awaited async call yields a freshly owned (+1) reference: the callee's
     * async `return` always retains the result before completing (see the
     * AST_RETURN_STMT async path), so the awaiter receives +1 and must NOT
     * retain it again. Treating it as owned lets the receiving local release it
     * on scope exit -- otherwise every awaited RC result leaks per await. */
    if (e->kind == AST_AWAIT_EXPR) return 1;
    /* An ANF await temp ($awN, generated by anf_hoist_await) holds the owned
     * (+1) result of an awaited async call. It is single-use, so copying it out
     * is a move: the destination takes the +1 and the temp is abandoned. Without
     * treating it as owned, `T x = $awN` retains a second reference and the
     * awaited result leaks. */
    if (e->kind == AST_IDENTIFIER && e->ident.name.len >= 3 &&
        e->ident.name.str[0] == '$' && e->ident.name.str[1] == 'a' &&
        e->ident.name.str[2] == 'w')
        return 1;
    if (e->kind == AST_STRING_INTERP) return 1;
    /* Dict.Keys / Dict.Values build a fresh owned List snapshot. */
    if (e->kind == AST_MEMBER_ACCESS &&
        ((e->member.name.len == 4 && memcmp(e->member.name.str, "Keys", 4) == 0) ||
         (e->member.name.len == 6 && memcmp(e->member.name.str, "Values", 6) == 0))) {
        zan_type_t *ot = infer_expr_type(g, e->member.object, locals);
        if (ot && ot->name.len == 4 && memcmp(ot->name.str, "Dict", 4) == 0)
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

static void emit_release_owned_call_temp(zan_irgen_t *g, zan_ast_node_t *arg,
                                         LLVMValueRef val, local_scope_t *locals) {
    if (!arg || !locals || !val) return;
    if (LLVMGetTypeKind(LLVMTypeOf(val)) != LLVMPointerTypeKind) return;
    if (expr_is_local_ident(arg, locals)) return;
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
    LLVMTypeRef ret = delegate_type->delegate_ret_type
        ? map_type(g, delegate_type->delegate_ret_type)
        : LLVMVoidTypeInContext(g->ctx);
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
    LLVMValueRef result = zan_call2(
        g->builder, fn_type, fn_ptr, call_args, (unsigned)argc, name);
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
    LLVMBasicBlockRef done_bb  = LLVMAppendBasicBlockInContext(g->ctx, g->fn_report_leaks, "done");

    LLVMPositionBuilderAtEnd(b, bb);
    LLVMValueRef live = LLVMBuildLoad2(b, i64, g->g_live, "live");
    LLVMValueRef leaked = LLVMBuildICmp(b, LLVMIntSGT, live,
        LLVMConstInt(i64, 0, 0), "leaked");
    LLVMBuildCondBr(b, leaked, leak_bb, done_bb);

    LLVMPositionBuilderAtEnd(b, leak_bb);
    LLVMValueRef msg = LLVMBuildGlobalStringPtr(b,
        "zan: memory leak detected: %lld object(s) still reachable at exit\n", "leak_fmt");
    LLVMValueRef pargs[] = { msg, live };
    zan_call2(b, g->printf_type, g->fn_printf, pargs, 2, "");
    LLVMBuildBr(b, head_bb);

    /* iterate the site buckets, printing those with a positive live count */
    LLVMPositionBuilderAtEnd(b, head_bb);
    LLVMValueRef idx = LLVMBuildPhi(b, i64, "i");
    LLVMValueRef in_range = LLVMBuildICmp(b, LLVMIntSLT, idx,
        LLVMConstInt(i64, ZAN_MAX_LEAK_SITES, 0), "inrange");
    LLVMBuildCondBr(b, in_range, body_bb, done_bb);

    LLVMPositionBuilderAtEnd(b, body_bb);
    LLVMValueRef z32 = LLVMConstInt(i32t, 0, 0);
    LLVMValueRef cidx[2] = { z32, idx };
    LLVMValueRef sc_ptr = LLVMBuildGEP2(b, g->site_live_type, g->g_site_live, cidx, 2, "scptr");
    LLVMValueRef sc = LLVMBuildLoad2(b, i64, sc_ptr, "sc");
    LLVMValueRef has = LLVMBuildICmp(b, LLVMIntSGT, sc, LLVMConstInt(i64, 0, 0), "has");
    LLVMBuildCondBr(b, has, print_bb, next_bb);

    LLVMPositionBuilderAtEnd(b, print_bb);
    LLVMValueRef nm_ptr = LLVMBuildGEP2(b, g->site_names_type, g->g_site_names, cidx, 2, "nmptr");
    LLVMValueRef nm = LLVMBuildLoad2(b, i8p, nm_ptr, "nm");
    LLVMValueRef dmsg = LLVMBuildGlobalStringPtr(b,
        "  %lld object(s) leaked, allocated at %s\n", "leak_site_fmt");
    LLVMValueRef dargs[] = { dmsg, sc, nm };
    zan_call2(b, g->printf_type, g->fn_printf, dargs, 3, "");
    LLVMBuildBr(b, next_bb);

    LLVMPositionBuilderAtEnd(b, next_bb);
    LLVMValueRef idx1 = LLVMBuildAdd(b, idx, LLVMConstInt(i64, 1, 0), "i.next");
    LLVMBuildBr(b, head_bb);

    LLVMValueRef phi_vals[2] = { LLVMConstInt(i64, 0, 0), idx1 };
    LLVMBasicBlockRef phi_bbs[2] = { leak_bb, next_bb };
    LLVMAddIncoming(idx, phi_vals, phi_bbs, 2);

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

/* True when a local owns a class heap reference held as a pointer (excludes
 * borrowed params, stack-struct classes and non-class types). */
static int local_owns_arc(local_var_t *v) {
    if (!v || v->arc_owned != 1 || !v->type || !is_rc_managed_type(v->type)) return 0;
    return LLVMGetTypeKind(LLVMGetAllocatedType(v->alloca)) == LLVMPointerTypeKind;
}

static int local_is_dict(local_var_t *v) {
    return v && v->arc_owned == 1 && v->type &&
           ((v->type->name.len == 4 && memcmp(v->type->name.str, "Dict", 4) == 0) ||
            (v->type->name.len == 10 && memcmp(v->type->name.str, "Dictionary", 10) == 0)) &&
           LLVMGetTypeKind(LLVMGetAllocatedType(v->alloca)) == LLVMPointerTypeKind;
}

static void emit_release_dict_local(zan_irgen_t *g, local_var_t *v) {
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMValueRef cur = LLVMBuildLoad2(g->builder, i8ptr, v->alloca, "dict.rel");
    emit_dict_release_elems(g, v->type, cur);
}

static void emit_release_owned_locals(zan_irgen_t *g, local_scope_t *locals) {
    if (!locals) return;
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    for (int i = 0; i < locals->count; i++) {
        if (local_owns_arc(&locals->vars[i])) {
            LLVMValueRef cur = LLVMBuildLoad2(g->builder, i8ptr,
                                              locals->vars[i].alloca, "arc.rel");
            emit_rc_release_for_type(g, locals->vars[i].type, cur);
        } else if (local_is_dict(&locals->vars[i])) {
            emit_release_dict_local(g, &locals->vars[i]);
        } else if (locals->vars[i].arr_len && locals->vars[i].type) {
            LLVMValueRef a = LLVMBuildLoad2(g->builder, i8ptr,
                                            locals->vars[i].alloca, "arr.rel");
            LLVMValueRef nn = LLVMBuildLoad2(g->builder,
                LLVMInt64TypeInContext(g->ctx), locals->vars[i].arr_len, "arr.n");
            emit_array_release_elems(g, locals->vars[i].type->element_type, a, nn);
        }
    }
}

/* Release owned locals in [start, count) without dropping them from the
 * scope: used on `break`/`continue` edges, where the same locals remain in
 * scope on the fall-through path and are released there separately. */
static void emit_release_owned_locals_range(zan_irgen_t *g, local_scope_t *locals, int start) {
    if (!locals) return;
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    for (int i = locals->count - 1; i >= start; i--) {
        if (local_owns_arc(&locals->vars[i])) {
            LLVMValueRef cur = LLVMBuildLoad2(g->builder, i8ptr,
                                              locals->vars[i].alloca, "arc.rel");
            emit_rc_release_for_type(g, locals->vars[i].type, cur);
        } else if (local_is_dict(&locals->vars[i])) {
            emit_release_dict_local(g, &locals->vars[i]);
        } else if (locals->vars[i].arr_len && locals->vars[i].type) {
            LLVMValueRef a = LLVMBuildLoad2(g->builder, i8ptr,
                                            locals->vars[i].alloca, "arr.rel");
            LLVMValueRef nn = LLVMBuildLoad2(g->builder,
                LLVMInt64TypeInContext(g->ctx), locals->vars[i].arr_len, "arr.n");
            emit_array_release_elems(g, locals->vars[i].type->element_type, a, nn);
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
        if (!terminated && local_owns_arc(&locals->vars[i])) {
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMValueRef cur = LLVMBuildLoad2(g->builder, i8ptr,
                                              locals->vars[i].alloca, "arc.rel");
            emit_rc_release_for_type(g, locals->vars[i].type, cur);
        } else if (!terminated && local_is_dict(&locals->vars[i])) {
            emit_release_dict_local(g, &locals->vars[i]);
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
    LLVMValueRef isnull = LLVMBuildICmp(g->builder, LLVMIntEQ, fp,
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
    const char *file = g->src_file ? g->src_file : "<unknown>";
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
    if (LLVMGetTypeKind(ta) != LLVMIntegerTypeKind ||
        LLVMGetTypeKind(tb) != LLVMIntegerTypeKind) return;
    unsigned wa = LLVMGetIntTypeWidth(ta), wb = LLVMGetIntTypeWidth(tb);
    if (wa == wb) return;
    if (wa < wb) *a = LLVMBuildSExt(g->builder, *a, tb, "iext");
    else         *b = LLVMBuildSExt(g->builder, *b, ta, "iext");
}

/* Normalize an integer value to a target integer type before storing it into a
 * slot of that type, so a width mismatch (e.g. an i32-producing helper feeding
 * an i64 `int` local) never produces invalid/undefined IR. Non-integer or
 * matching-width values, and non-integer targets, pass through unchanged. */
static LLVMValueRef coerce_int_to(zan_irgen_t *g, LLVMValueRef v, LLVMTypeRef target) {
    LLVMTypeRef vt = LLVMTypeOf(v);
    if (LLVMGetTypeKind(vt) != LLVMIntegerTypeKind ||
        LLVMGetTypeKind(target) != LLVMIntegerTypeKind) return v;
    unsigned wv = LLVMGetIntTypeWidth(vt), wt = LLVMGetIntTypeWidth(target);
    if (wv == wt) return v;
    if (wv < wt) return LLVMBuildSExt(g->builder, v, target, "sext");
    return LLVMBuildTrunc(g->builder, v, target, "trunc");
}

/* Widen a narrow integer to i64 for numeric formatting/printing. Signed
 * integers are sign-extended, but i1 (bool) is zero-extended so that `true`
 * formats as 1 rather than -1 (sext of an i1 set to 1 yields all-ones). */
static LLVMValueRef emit_widen_i64_for_print(zan_irgen_t *g, LLVMValueRef v) {
    LLVMTypeRef vt = LLVMTypeOf(v);
    if (LLVMGetTypeKind(vt) != LLVMIntegerTypeKind) return v;
    unsigned w = LLVMGetIntTypeWidth(vt);
    if (w >= 64) return v;
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    if (w == 1) return LLVMBuildZExt(g->builder, v, i64, "bext");
    return LLVMBuildSExt(g->builder, v, i64, "ext");
}

static LLVMValueRef emit_string_alloc_rc(zan_irgen_t *g, LLVMValueRef payload_size) {
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMValueRef thirty_one = LLVMConstInt(i64, 31, 0);
    LLVMValueRef padded = LLVMBuildAdd(g->builder, payload_size, thirty_one, "str.pad");
    LLVMValueRef aligned = LLVMBuildAnd(g->builder, padded,
        LLVMConstInt(i64, ~UINT64_C(31), 0), "str.align");
    LLVMValueRef min_payload = LLVMConstInt(i64, 32, 0);
    LLVMValueRef payload = LLVMBuildSelect(g->builder,
        LLVMBuildICmp(g->builder, LLVMIntULT, aligned, min_payload, "str.small"),
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
    LLVMValueRef need = LLVMBuildAdd(g->builder, count, slen, "sbneed");
    LLVMValueRef full = LLVMBuildICmp(g->builder, LLVMIntSGT, need, cap, "sbfull");
    LLVMBasicBlockRef grow_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "sb.grow");
    LLVMBasicBlockRef st_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "sb.st");
    LLVMBuildCondBr(g->builder, full, grow_bb, st_bb);
    /* grow: newcap = max(cap*2, need); realloc data */
    LLVMPositionBuilderAtEnd(g->builder, grow_bb);
    LLVMValueRef nc0 = LLVMBuildMul(g->builder, cap, LLVMConstInt(i64, 2, 0), "sbnc0");
    LLVMValueRef small = LLVMBuildICmp(g->builder, LLVMIntSLT, nc0, need, "sbsm");
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
    LLVMValueRef ncount = LLVMBuildAdd(g->builder, count, slen, "sbncount");
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
    LLVMSetGlobalConstant(global, 1);
    LLVMSetUnnamedAddr(global, LLVMGlobalUnnamedAddr);
    unsigned char *blob = (unsigned char *)calloc(total, 1);
    if (!blob) return LLVMConstNull(LLVMPointerType(i8, 0));
    memcpy(blob + 0, &((uint64_t){ ZAN_STRING_SENTINEL_RC }), 8);
    memcpy(blob + 8, &((uint64_t){ ZAN_STRING_MAGIC }), 8);
    if (text.len > 0) memcpy(blob + 16, text.str, (size_t)text.len);
    blob[16 + (size_t)text.len] = 0;
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

/* Coerce a value to an i8* C string. Pointers pass through unchanged; integer
 * and floating operands are formatted into a fresh heap buffer via snprintf.
 * Lets `+` concatenate strings with numbers (e.g. "%t" + counter). */
static LLVMValueRef emit_to_cstr(zan_irgen_t *g, LLVMValueRef val) {
    LLVMTypeRef vt = LLVMTypeOf(val);
    LLVMTypeKind vtk = LLVMGetTypeKind(vt);
    if (vtk == LLVMPointerTypeKind) return val;

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
    LLVMValueRef bsz = LLVMBuildAdd(g->builder, needed, LLVMConstInt(i64, 1, 0), "bsz");
    LLVMValueRef buf = emit_string_alloc_rc(g, bsz);
    zan_call2(g->builder,
        LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0),
        LLVMGetNamedFunction(g->mod, "strcpy"),
        (LLVMValueRef[]){ buf, tmp }, 2, "");
    return buf;
}

/* Emit `a + b` for two string (i8*) operands as a heap-allocated concatenation:
 * malloc(strlen(a)+strlen(b)+1); memcpy left, memcpy right, NUL terminate. */
static LLVMValueRef emit_str_concat(zan_irgen_t *g, LLVMValueRef a, LLVMValueRef b) {
    LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef strlen_type = LLVMFunctionType(i64t, (LLVMTypeRef[]){ i8ptr }, 1, 0);
    LLVMValueRef la = zan_call2(g->builder, strlen_type, g->fn_strlen, &a, 1, "cla");
    LLVMValueRef lb = zan_call2(g->builder, strlen_type, g->fn_strlen, &b, 1, "clb");
    LLVMValueRef tot = LLVMBuildAdd(g->builder, la, lb, "ct");
    tot = LLVMBuildAdd(g->builder, tot, LLVMConstInt(i64t, 1, 0), "ct1");
    LLVMValueRef buf = emit_string_alloc_rc(g, tot);
    LLVMTypeRef memcpy_type = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr, i64t }, 3, 0);
    LLVMValueRef memcpy_fn = LLVMGetNamedFunction(g->mod, "memcpy");
    if (!memcpy_fn) memcpy_fn = LLVMAddFunction(g->mod, "memcpy", memcpy_type);
    zan_call2(g->builder, memcpy_type, memcpy_fn,
        (LLVMValueRef[]){ buf, a, la }, 3, "");
    LLVMValueRef dst_b = LLVMBuildGEP2(g->builder, LLVMInt8TypeInContext(g->ctx), buf, &la, 1, "dst.b");
    zan_call2(g->builder, memcpy_type, memcpy_fn,
        (LLVMValueRef[]){ dst_b, b, lb }, 3, "");
    LLVMValueRef end_off = LLVMBuildAdd(g->builder, la, lb, "slen");
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
        collect_concat_ops(g, e->binary.left, locals, ops, n, max);
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
        LLVMValueRef s = emit_to_cstr(g, v);
        vals[i] = s;
        owned[i] = !is_string_expr(g, ops[i], locals) ||
                   expr_yields_owned_rc_value(g, ops[i], locals);
        lens[i] = zan_call2(g->builder, strlen_type, g->fn_strlen, &s, 1, "cl");
        total = LLVMBuildAdd(g->builder, total, lens[i], "ct");
    }
    LLVMValueRef buf = emit_string_alloc_rc(g, total);
    LLVMValueRef off = LLVMConstInt(i64t, 0, 0);
    for (int i = 0; i < n; i++) {
        LLVMValueRef dst = LLVMBuildGEP2(g->builder, i8t, buf, &off, 1, "dst");
        zan_call2(g->builder, memcpy_type, memcpy_fn,
            (LLVMValueRef[]){ dst, vals[i], lens[i] }, 3, "");
        off = LLVMBuildAdd(g->builder, off, lens[i], "off");
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
    LLVMTypeRef reap_ty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), &i8ptr, 1, 0);
    reap = LLVMAddFunction(g->mod, "__zan_co_reap", reap_ty);
    LLVMSetLinkage(reap, LLVMInternalLinkage);
    LLVMBasicBlockRef saved = LLVMGetInsertBlock(g->builder);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, reap, "entry");
    LLVMPositionBuilderAtEnd(g->builder, entry);
    LLVMValueRef arg = LLVMGetParam(reap, 0);
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
    LLVMValueRef nonnull = LLVMBuildICmp(g->builder, LLVMIntNE, cur,
        LLVMConstNull(i8ptr), "cur.nn");
    LLVMBuildCondBr(g->builder, nonnull, body, done);

    LLVMPositionBuilderAtEnd(g->builder, body);
    cur = LLVMBuildLoad2(g->builder, i8ptr, cur_a, "cur");
    LLVMValueRef hf = LLVMBuildBitCast(g->builder, cur, hdr_ptr, "hf");
    LLVMValueRef hc = LLVMBuildLoad2(g->builder, i32,
        LLVMBuildStructGEP2(g->builder, hdr, hf, ASYNC_FRAME_HCOUNT, "hc.p"), "hc");
    LLVMValueRef armed = LLVMBuildICmp(g->builder, LLVMIntSGT, hc,
        LLVMConstInt(i32, 0, 0), "hc.armed");
    LLVMBuildCondBr(g->builder, armed, done, clean);

    LLVMPositionBuilderAtEnd(g->builder, clean);
    LLVMValueRef aw = LLVMBuildLoad2(g->builder, i8ptr,
        LLVMBuildStructGEP2(g->builder, hdr, hf, ASYNC_FRAME_AWAITER, "aw.p"), "aw");
    /* a detached (Task.Spawn) coroutine marks itself as its own awaiter */
    LLVMValueRef det = LLVMBuildICmp(g->builder, LLVMIntEQ, aw, cur, "aw.det");
    LLVMValueRef nxt = LLVMBuildSelect(g->builder, det,
        LLVMConstNull(i8ptr), aw, "aw.next");
    LLVMBuildStore(g->builder, nxt, next_a);
    LLVMValueRef cl = LLVMBuildLoad2(g->builder, g->co_step_ptr,
        LLVMBuildStructGEP2(g->builder, hdr, hf, ASYNC_FRAME_CLEANUP, "cl.p"), "cl");
    LLVMValueRef has_cl = LLVMBuildICmp(g->builder, LLVMIntNE, cl,
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
