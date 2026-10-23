/* irgen_generics */

static void emit_eh_tmp_push_slot(zan_irgen_t *g, LLVMValueRef slot, int kind);

static bool sym_declares_extern(zan_symbol_t *sym);
static LLVMValueRef emit_soft_scratch_cached(zan_irgen_t *g);
static zan_symbol_t *extern_check_callee_sym(zan_irgen_t *g,
                                             zan_ast_node_t *call);
static zan_type_t *method_ret_type_at(zan_irgen_t *g, zan_symbol_t *msym,
                                      zan_ast_node_t *call,
                                      zan_ast_node_t *recv_expr,
                                      local_scope_t *locals);

static void collect_inst_type_depth(zan_irgen_t *g, zan_type_t *t, int depth) {
    if (!t || depth > 16) return;
    if (g->collect_inst_ctx && !type_is_concrete(t))
        t = subst_type_param_deep(g, t, g->collect_inst_ctx);
    if (!t) return;
    add_generic_inst(g, t);
    /* 泛型类的**基类实例**也要注册：class Validator<T> : Plan<T> 实例化成
     * Validator<Signup> 时，基类实例是 Plan<Signup>。不注册就没有
     * Plan<Signup> 的方法变体，调用退化成擦除版——基类方法里读
     * `this.properties` 拿到的是擦除槽位的指针，与派生类看到的字段不是
     * 同一个东西（实测运行期 0xC0000005）。 */
    if (t->base_type && t->base_type->sym && t->base_type->sym != t->sym) {
        zan_type_t *binst = subst_type_param_deep(g, t->base_type, t);
        if (binst && binst != t->base_type)
            collect_inst_type_depth(g, binst, depth + 1);
    }
    if (t->kind == TYPE_ARRAY || t->kind == TYPE_NULLABLE)
        collect_inst_type_depth(g, t->element_type, depth + 1);
    for (int i = 0; i < t->type_arg_count; i++)
        collect_inst_type_depth(g, t->type_args[i], depth + 1);
}

static void collect_inst_type(zan_irgen_t *g, zan_type_t *t) {
    collect_inst_type_depth(g, t, 0);
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
        collect_inst_typeref(g, e->ident.inst_type_ref);
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
        for (int i = 0; i < e->new_expr.arg_inits.count; i++)
            collect_inst_expr(g, e->new_expr.arg_inits.items[i]);
        break;
    case AST_COLL_INIT:
        for (int i = 0; i < e->coll_init.items.count; i++)
            collect_inst_expr(g, e->coll_init.items.items[i]);
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
            for (int j = 0; j < decl->type_decl.bases.count; j++)
                collect_inst_typeref(g, decl->type_decl.bases.items[j]);
            for (int j = 0; j < decl->type_decl.members.count; j++)
                collect_inst_member(g, decl->type_decl.members.items[j]);
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
    zan_compile_trace("generic insts: %d after %d round(s)",
                      g->generic_inst_count, guard);
}

static zan_type_t *ident_inst_type(zan_irgen_t *g, zan_ast_node_t *e) {
    if (!e || e->kind != AST_IDENTIFIER || !e->ident.inst_type_ref) return NULL;
    return zan_binder_resolve_type(g->binder, e->ident.inst_type_ref);
}

static LLVMValueRef coerce_generic_result(zan_irgen_t *g, LLVMValueRef result,
                                          zan_symbol_t *method_sym,
                                          zan_type_t *recv) {
    if (!result || !method_sym || !recv) return result;
    zan_type_t *rt = subst_type_param(method_sym->type, recv);
    if (rt && rt != method_sym->type)
        return emit_boundary_coerce(g, result, map_type(g, rt));
    return result;
}

static LLVMValueRef route_generic_method(zan_irgen_t *g, zan_type_t *recv_ty,
                                         zan_symbol_t *method_sym,
                                         LLVMValueRef erased_fn,
                                         LLVMTypeRef erased_ty,
                                         LLVMTypeRef *out_ty) {
    if (out_ty) *out_ty = erased_ty;
    if (!recv_ty || !recv_ty->sym) return erased_fn;
    if (!is_user_generic_sym(recv_ty->sym)) {
        for (zan_type_t *bt = recv_ty->base_type; bt; bt = bt->base_type) {
            if (bt->sym && is_user_generic_sym(bt->sym)) {
                if (method_sym && method_sym->parent &&
                    (method_sym->parent == bt->sym ||
                     (method_sym->parent->name.len == bt->sym->name.len &&
                      memcmp(method_sym->parent->name.str, bt->sym->name.str,
                             (size_t)bt->sym->name.len) == 0))) {
                    recv_ty = bt;
                    break;
                }
            }
        }
    }
    if (!is_user_generic_sym(recv_ty->sym)) return erased_fn;
    zan_type_t **args = recv_ty->type_args;
    int argc = recv_ty->type_arg_count;
    if (g->cur_inst && g->cur_inst->sym == recv_ty->sym) {
        bool concrete = argc > 0;
        for (int i = 0; i < argc && concrete; i++)
            concrete = type_is_concrete(args[i]);
        if (!concrete) {
            args = g->cur_inst->type_args;
            argc = g->cur_inst->type_arg_count;
        }
    } else if (g->cur_inst && !type_is_concrete(recv_ty)) {
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
    zan_compile_trace("route miss: %.*s.%.*s argc=%d arg0type=%d concrete=%d",
                      (int)recv_ty->sym->name.len, recv_ty->sym->name.str,
                      (int)method_sym->name.len, method_sym->name.str,
                      argc, args && args[0] ? (int)args[0]->kind : -1,
                      type_is_concrete(recv_ty));
    return erased_fn;
}

static bool class_has_derived_in_module(zan_irgen_t *g, zan_symbol_t *target) {
    if (!g || !target) return false;
    for (int i = 0; i < g->struct_type_count; i++) {
        zan_symbol_t *cur = g->struct_types[i].sym;
        if (!cur || cur == target) continue;
        for (zan_type_t *bt = (cur->type ? cur->type->base_type : NULL); bt; bt = bt->base_type) {
            if (bt->sym == target) return true;
        }
    }
    return false;
}

/* 检查if any derived class of static_sym actually overrides method_sym */
static bool hierarchy_has_subclass_override(zan_irgen_t *g, zan_symbol_t *static_sym, zan_symbol_t *method_sym) {
    if (!g || !static_sym || !method_sym) return false;
    int want_params = method_declared_param_count(method_sym);
    for (int i = 0; i < g->struct_type_count; i++) {
        zan_symbol_t *cur = g->struct_types[i].sym;
        if (!cur || cur == static_sym) continue;
        bool is_sub = false;
        for (zan_type_t *bt = (cur->type ? cur->type->base_type : NULL); bt; bt = bt->base_type) {
            if (bt->sym == static_sym) { is_sub = true; break; }
        }
        if (!is_sub) continue;
        for (int m = 0; m < cur->member_count; m++) {
            zan_symbol_t *mem = cur->members[m];
            if (mem && mem->kind == SYM_METHOD && (mem->modifiers & MOD_OVERRIDE)) {
                if (member_name_is(mem, method_sym->name) &&
                    method_declared_param_count(mem) == want_params) {
                    return true;
                }
            }
        }
    }
    return false;
}

static LLVMValueRef emit_dispatch_call(zan_irgen_t *g, zan_symbol_t *static_sym,
        zan_symbol_t *method_sym, LLVMValueRef static_fn, LLVMTypeRef fn_type,
        LLVMValueRef *call_args, int argc, const char *cn) {
    coerce_args_to_params(g, fn_type, call_args, argc);
    if (static_sym && method_sym &&
        (method_sym->modifiers & (MOD_VIRTUAL | MOD_OVERRIDE | MOD_ABSTRACT)) &&
        class_has_virtual_methods(static_sym) && argc >= 1 && call_args[0] &&
        LLVMGetTypeKind(LLVMTypeOf(call_args[0])) == LLVMPointerTypeKind) {

        bool devirt = ((static_sym->modifiers & MOD_SEALED) != 0) ||
                      ((method_sym->modifiers & MOD_SEALED) != 0) ||
                      !class_has_derived_in_module(g, static_sym) ||
                      !hierarchy_has_subclass_override(g, static_sym, method_sym);

        if (!devirt) {
            int slot = get_virtual_method_index(static_sym, method_sym);
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
    }
    return zan_call2(g->builder, fn_type, static_fn, call_args, (unsigned)argc, cn);
}

static int expr_is_arc_object(zan_irgen_t *g, zan_ast_node_t *e, local_scope_t *locals) {
    zan_type_t *t = infer_expr_type(g, e, locals);
    return is_rc_managed_type(t);
}

static int expr_index_of_owned_temp(zan_irgen_t *g, zan_ast_node_t *e,
                                    local_scope_t *locals);
static int expr_member_of_owned_temp(zan_irgen_t *g, zan_ast_node_t *e,
                                     local_scope_t *locals);

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
    if (e->kind == AST_CAST_EXPR)
        return expr_yields_owned_rc_value(g, e->cast.expr, locals);
    if (e->kind == AST_POSTFIX_UNARY && e->unary.op == TK_BANG)
        return expr_yields_owned_rc_value(g, e->unary.operand, locals);
    if (e->kind == AST_TUPLE_EXPR) return 1;
    if (e->kind == AST_IDENTIFIER && g->current_type_sym && g->current_this &&
        !(locals && local_find(locals, e->ident.name))) {
        zan_symbol_t *bfs = get_field_sym(g->current_type_sym, e->ident.name);
        if (bfs && bfs->kind == SYM_FIELD &&
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

/* 检查是否e */
static int member_field_is_weak(zan_irgen_t *g, zan_ast_node_t *e,
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
static int call_arg_is_owned_temp(zan_irgen_t *g, zan_ast_node_t *arg,
                                  zan_type_t *at, local_scope_t *locals) {
    if (!arg) return 0;
    if (expr_is_local_ident(arg, locals)) return 0;
    if (expr_yields_delegate_value(g, arg, locals)) return 1;
    if (!at || !is_rc_managed_type(at)) return 0;
    return expr_yields_owned_rc_value(g, arg, locals);
}

static void emit_release_owned_call_temp(zan_irgen_t *g, zan_ast_node_t *arg,
                                         LLVMValueRef val, local_scope_t *locals) {
    if (!arg || !locals || !val) return;
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
    free(call_args);
    free(param_types);
    return result;
}

static void emit_leak_report_support(zan_irgen_t *g) {
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

static void emit_eh_tmp_push_slot(zan_irgen_t *g, LLVMValueRef slot, int kind);
static void emit_eh_tmp_push(zan_irgen_t *g, LLVMValueRef obj);
static void emit_eh_tmp_pop(zan_irgen_t *g);
static void emit_eh_tmp_drop(zan_irgen_t *g, LLVMValueRef obj);
static zan_type_t *method_ret_type_at(zan_irgen_t *g, zan_symbol_t *msym,
                                      zan_ast_node_t *call,
                                      zan_ast_node_t *recv_expr,
                                      local_scope_t *locals);

static int local_slot_owns_rc(local_var_t *v) {
    if (!v || v->arc_owned != 1 || !v->type || !is_rc_managed_type(v->type)) return 0;
    if (v->byref_slot) return 1;
    if (v->box_cell) return 1;
    return LLVMGetTypeKind(LLVMGetAllocatedType(v->alloca)) == LLVMPointerTypeKind;
}

static int local_is_dyn_obj(zan_irgen_t *g, local_var_t *v) {
    if (!v || !v->type || v->type->kind != TYPE_OBJECT) return 0;
    if (v->box_cell || g->current_async_frame) return v->obj_rc_flag != NULL;
    return LLVMGetTypeKind(local_slot_type(g, v)) == LLVMPointerTypeKind;
}

static int local_owns_arc(local_var_t *v) {
    return local_slot_owns_rc(v) && !v->box_cell && !v->byref_slot;
}

static void emit_closure_record_release(zan_irgen_t *g, LLVMValueRef rec);
static void release_boxed_local(zan_irgen_t *g, local_var_t *v) {
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

static void arc_own_local(zan_irgen_t *g, local_scope_t *locals) {
    if (!locals || locals->count == 0) return;
    local_var_t *v = &locals->vars[locals->count - 1];
    v->arc_owned = 1;
    if (v->eh_slot || g->current_async_frame || !local_owns_arc(v)) return;
    emit_eh_tmp_push_slot(g, v->alloca, eh_slot_kind_of(v->type));
    v->eh_slot = 1;
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
        LLVMBuildStore(g->builder, LLVMConstInt(i32, 0, 0), owned_slot);
        LLVMBuildBr(g->builder, cont_bb);
        LLVMPositionBuilderAtEnd(g->builder, cont_bb);
    }
}

static void emit_clear_owned_locals_range(zan_irgen_t *g, local_scope_t *locals, int start) {
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

static void emit_release_owned_locals_from(zan_irgen_t *g, local_scope_t *locals, int start) {
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
    }
    return v->obj_rc_flag;
}

static void emit_release_obj_value(zan_irgen_t *g, LLVMValueRef cur) {
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i8t = LLVMInt8TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(i8t, 0);
    if (LLVMTypeOf(cur) != i8ptr)
        cur = LLVMBuildBitCast(g->builder, cur, i8ptr, "obj.rbc");
    LLVMValueRef fn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder));
    LLVMBasicBlockRef probe = LLVMAppendBasicBlockInContext(g->ctx, fn, "obj.probe");
    LLVMBasicBlockRef sbb = LLVMAppendBasicBlockInContext(g->ctx, fn, "obj.srel");
    LLVMBasicBlockRef obb = LLVMAppendBasicBlockInContext(g->ctx, fn, "obj.orel");
    LLVMBasicBlockRef cbb = LLVMAppendBasicBlockInContext(g->ctx, fn, "obj.rdone");
    LLVMBuildCondBr(g->builder,
        zan_icmp(g->builder, LLVMIntEQ, cur, LLVMConstNull(i8ptr), "obj.rnull"),
        cbb, probe);
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
    emit_release_obj_value(g, cur);
    LLVMBuildStore(g->builder, LLVMConstInt(i1, 0, 0), v->obj_rc_flag);
    LLVMBuildStore(g->builder, LLVMConstNull(i8ptr), v->alloca);
    LLVMBuildBr(g->builder, cont);
    LLVMPositionBuilderAtEnd(g->builder, cont);
}

static int obj_slot_owns_value(zan_type_t *t) {
    return t && is_arc_managed_type(t);
}

static int obj_slot_owns_owned_rhs(zan_irgen_t *g, zan_ast_node_t *rhs,
                                   LLVMValueRef val, local_scope_t *locals) {
    if (!rhs || !val || !locals) return 0;
    if (LLVMGetTypeKind(LLVMTypeOf(val)) != LLVMPointerTypeKind) return 0;
    zan_type_t *rt = infer_expr_type(g, rhs, locals);
    if (!rt || (rt->kind != TYPE_OBJECT && rt->kind != TYPE_STRING)) return 0;
    return expr_yields_owned_rc_value(g, rhs, locals);
}

static void emit_obj_local_store(zan_irgen_t *g, local_var_t *v, LLVMValueRef val,
                                 zan_type_t *vtype, zan_ast_node_t *rhs,
                                 local_scope_t *locals) {
    LLVMTypeRef i1 = LLVMInt1TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    int owns = obj_slot_owns_value(vtype) ||
               obj_slot_owns_owned_rhs(g, rhs, val, locals);
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

static void emit_rc_store_field(zan_irgen_t *g, zan_type_t *type,
                                LLVMValueRef field_ptr, LLVMValueRef v,
                                zan_ast_node_t *rhs, local_scope_t *locals,
                                int is_weak) {
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
        if (LLVMGetTypeKind(vt) == LLVMPointerTypeKind)
            emit_weak_store(g, field_ptr, v);
        else
            LLVMBuildStore(g->builder, v, field_ptr);
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
        emit_eh_tmp_push_slot(g, field_ptr, eh_slot_kind_of(type));
    }
}

#define ZAN_RT_FAULT_MESSAGE 0xE0A2C010u

static void emit_fatal_report(zan_irgen_t *g, LLVMValueRef text, int exit_code) {
    LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i8p = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMValueRef fmt = zan_irgen_intern_string(g, "%s");
    LLVMValueRef pargs[] = { fmt, text };
    zan_call2(g->builder, g->printf_type, g->fn_printf, pargs, 2, "");

    if (g->target_is_windows) {
        LLVMTypeRef fl_ty = LLVMFunctionType(i32t, &i8p, 1, 0);
        LLVMValueRef fl = LLVMGetNamedFunction(g->mod, "fflush");
        if (!fl) fl = LLVMAddFunction(g->mod, "fflush", fl_ty);
        LLVMValueRef nullp = LLVMConstPointerNull(i8p);
        zan_call2(g->builder, fl_ty, fl, &nullp, 1, "");

        LLVMTypeRef re_args[] = { i32t, i32t, i32t, LLVMPointerType(i64t, 0) };
        LLVMTypeRef re_ty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx),
                                             re_args, 4, 0);
        LLVMValueRef re = LLVMGetNamedFunction(g->mod, "RaiseException");
        if (!re) re = LLVMAddFunction(g->mod, "RaiseException", re_ty);
        LLVMValueRef slots = LLVMBuildArrayAlloca(g->builder, i64t,
            LLVMConstInt(i32t, 2, 0), "fatalargs");
        LLVMValueRef vals[2] = {
            LLVMBuildPtrToInt(g->builder, text, i64t, "fatalmsg"),
            LLVMConstInt(i64t, (unsigned long long)exit_code, 0)
        };
        for (int i = 0; i < 2; i++) {
            LLVMValueRef idx = LLVMConstInt(i32t, (unsigned long long)i, 0);
            LLVMValueRef slot = LLVMBuildGEP2(g->builder, i64t, slots, &idx, 1,
                                              "fatalslot");
            LLVMBuildStore(g->builder, vals[i], slot);
        }
        LLVMValueRef cargs[] = { LLVMConstInt(i32t, ZAN_RT_FAULT_MESSAGE, 0),
                                 LLVMConstInt(i32t, 0, 0),
                                 LLVMConstInt(i32t, 2, 0), slots };
        zan_call2(g->builder, re_ty, re, cargs, 4, "");
    }
    LLVMValueRef code = LLVMConstInt(i32t, (unsigned long long)exit_code, 0);
    zan_call2(g->builder, g->exit_type, g->fn_exit, &code, 1, "");
    LLVMBuildUnreachable(g->builder);
}


static void emit_guard_hard(zan_irgen_t *g, LLVMValueRef text, int exit_code) {
    LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i8p = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMValueRef fmt = zan_irgen_intern_string(g, "%s");
    LLVMValueRef pargs[] = { fmt, text };
    zan_call2(g->builder, g->printf_type, g->fn_printf, pargs, 2, "");

    if (g->target_is_windows) {
        LLVMTypeRef fl_ty = LLVMFunctionType(i32t, &i8p, 1, 0);
        LLVMValueRef fl = LLVMGetNamedFunction(g->mod, "fflush");
        if (!fl) fl = LLVMAddFunction(g->mod, "fflush", fl_ty);
        LLVMValueRef nullp = LLVMConstPointerNull(i8p);
        zan_call2(g->builder, fl_ty, fl, &nullp, 1, "");

        LLVMTypeRef re_args[] = { i32t, i32t, i32t, LLVMPointerType(i64t, 0) };
        LLVMTypeRef re_ty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx),
                                             re_args, 4, 0);
        LLVMValueRef re = LLVMGetNamedFunction(g->mod, "RaiseException");
        if (!re) re = LLVMAddFunction(g->mod, "RaiseException", re_ty);
        LLVMValueRef slots = LLVMBuildArrayAlloca(g->builder, i64t,
            LLVMConstInt(i32t, 2, 0), "fatalargs");
        LLVMValueRef vals[2] = {
            LLVMBuildPtrToInt(g->builder, text, i64t, "fatalmsg"),
            LLVMConstInt(i64t, (unsigned long long)exit_code, 0)
        };
        for (int i = 0; i < 2; i++) {
            LLVMValueRef idx = LLVMConstInt(i32t, (unsigned long long)i, 0);
            LLVMValueRef slot = LLVMBuildGEP2(g->builder, i64t, slots, &idx, 1,
                                              "fatalslot");
            LLVMBuildStore(g->builder, vals[i], slot);
        }
        LLVMValueRef cargs[] = { LLVMConstInt(i32t, ZAN_RT_FAULT_MESSAGE, 0),
                                 LLVMConstInt(i32t, 0, 0),
                                 LLVMConstInt(i32t, 2, 0), slots };
        zan_call2(g->builder, re_ty, re, cargs, 4, "");
    }
    LLVMValueRef code = LLVMConstInt(i32t, (unsigned long long)exit_code, 0);
    zan_call2(g->builder, g->exit_type, g->fn_exit, &code, 1, "");
    LLVMBuildUnreachable(g->builder);
}

static void emit_guard_report3(zan_irgen_t *g, LLVMValueRef file_gv,
                               unsigned line, unsigned col,
                               LLVMValueRef msg_gv);
static void emit_guard_report_merged(zan_irgen_t *g, LLVMValueRef text);
static void emit_guard_report3(zan_irgen_t *g, LLVMValueRef file_gv,
                               unsigned line, unsigned col,
                               LLVMValueRef msg_gv) {
    LLVMTypeRef i8p = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef fail_ty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx),
                                           (LLVMTypeRef[]){ i8p, i32t, i32t,
                                                            i8p }, 4, 0);
    LLVMValueRef fail_fn = LLVMGetNamedFunction(g->mod, "zan_rt_guard_fail3");
    if (!fail_fn) fail_fn = LLVMAddFunction(g->mod, "zan_rt_guard_fail3",
                                            fail_ty);
    LLVMValueRef fargs[] = {
        file_gv,
        LLVMConstInt(i32t, line, 0),
        LLVMConstInt(i32t, col, 0),
        msg_gv
    };
    zan_call2(g->builder, fail_ty, fail_fn, fargs, 4, "");
}

static void emit_guard_report_merged(zan_irgen_t *g, LLVMValueRef text) {
    LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i8p = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);

    LLVMTypeRef ishard_ty = LLVMFunctionType(i32t, NULL, 0, 0);
    LLVMValueRef ishard_fn = LLVMGetNamedFunction(g->mod, "zan_rt_soft_is_hard");
    if (!ishard_fn) ishard_fn = LLVMAddFunction(g->mod, "zan_rt_soft_is_hard",
                                                ishard_ty);
    LLVMValueRef hard = zan_call2(g->builder, ishard_ty, ishard_fn, NULL, 0,
                                  "rt.hard");

    LLVMTypeRef note_ty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx),
                                           (LLVMTypeRef[]){ i8p }, 1, 0);
    LLVMValueRef note_fn = LLVMGetNamedFunction(g->mod, "zan_rt_soft_note");
    if (!note_fn) note_fn = LLVMAddFunction(g->mod, "zan_rt_soft_note", note_ty);

    LLVMBasicBlockRef hard_bb = LLVMAppendBasicBlockInContext(g->ctx,
        g->current_fn, "rt.hardpath");
    LLVMBasicBlockRef soft_cont_bb = LLVMAppendBasicBlockInContext(g->ctx,
        g->current_fn, "rt.softcont");
    LLVMBuildCondBr(g->builder,
                    zan_icmp(g->builder, LLVMIntNE, hard,
                             LLVMConstInt(i32t, 0, 0), "rt.hardcmp"),
                    hard_bb, soft_cont_bb);

    LLVMPositionBuilderAtEnd(g->builder, hard_bb);
    emit_guard_hard(g, text, 70);

    LLVMPositionBuilderAtEnd(g->builder, soft_cont_bb);
    zan_call2(g->builder, note_ty, note_fn, &text, 1, "");
}

static void emit_io_abort_if(zan_irgen_t *g, LLVMValueRef cond, const char *msg) {
    if (!g->current_fn) return;
    LLVMBasicBlockRef fail_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "io.fail");
    LLVMBasicBlockRef cont_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "io.cont");
    LLVMBuildCondBr(g->builder, cond, fail_bb, cont_bb);
    LLVMPositionBuilderAtEnd(g->builder, fail_bb);
    LLVMValueRef text = zan_irgen_intern_string(g, msg);
    emit_fatal_report(g, text, 1);
    LLVMPositionBuilderAtEnd(g->builder, cont_bb);
}

static void emit_fopen_check(zan_irgen_t *g, LLVMValueRef fp, const char *msg) {
    if (!g->current_fn) return;
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMValueRef isnull = zan_icmp(g->builder, LLVMIntEQ, fp,
        LLVMConstPointerNull(i8ptr), "fp.isnull");
    emit_io_abort_if(g, isnull, msg);
}

static LLVMValueRef emit_expect_false(zan_irgen_t *g, LLVMValueRef cond) {
    if (!cond) return cond;
    if (LLVMGetTypeKind(LLVMTypeOf(cond)) != LLVMIntegerTypeKind) return cond;
    if (LLVMGetIntTypeWidth(LLVMTypeOf(cond)) != 1) return cond;
    if (!g->expect_false_fn) {
        LLVMTypeRef i1 = LLVMInt1TypeInContext(g->ctx);
        LLVMTypeRef ty = LLVMFunctionType(i1, (LLVMTypeRef[]){ i1, i1 }, 2, 0);
        g->expect_false_fn = LLVMAddFunction(g->mod, "llvm.expect.i1", ty);
    }
    LLVMValueRef args[] = { cond, LLVMConstInt(LLVMInt1TypeInContext(g->ctx), 0, 0) };
    return LLVMBuildCall2(g->builder,
                          LLVMGlobalGetValueType(g->expect_false_fn),
                          g->expect_false_fn, args, 2, "guard.expect");
}

static void emit_runtime_check(zan_irgen_t *g, LLVMValueRef is_error,
                               zan_loc_t loc, const char *msg) {
    if (!g->runtime_checks || !g->current_fn) return;
    is_error = emit_expect_false(g, is_error);

    if (!g->rt_guard_split) {
        LLVMBasicBlockRef bad_bb  = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "rt.bad");
        LLVMBasicBlockRef cont_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "rt.cont");
        LLVMBuildCondBr(g->builder, is_error, bad_bb, cont_bb);
        LLVMPositionBuilderAtEnd(g->builder, bad_bb);
        char buf[640];
        const char *file0 = loc_site_file(g, loc);
        if (g->hide_diagnostics) {
            snprintf(buf, sizeof(buf), "%s: runtime error: %s\n",
                     file0, msg);
        } else {
            snprintf(buf, sizeof(buf), "%s:%u:%u: runtime error: %s\n",
                     file0, loc.line, loc.col, msg);
        }
        LLVMValueRef text0 = zan_irgen_intern_string(g, buf);
        emit_guard_report_merged(g, text0);
        LLVMBuildBr(g->builder, cont_bb);
        LLVMPositionBuilderAtEnd(g->builder, cont_bb);
        return;
    }

    LLVMBasicBlockRef bad_bb  = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "rt.bad");
    LLVMBasicBlockRef cont_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "rt.cont");
    LLVMBuildCondBr(g->builder, is_error, bad_bb, cont_bb);

    LLVMPositionBuilderAtEnd(g->builder, bad_bb);
    LLVMValueRef file_gv = zan_irgen_intern_string(g, loc_site_file(g, loc));
    char mbuf[320];
    snprintf(mbuf, sizeof(mbuf), "%s\n", msg);
    LLVMValueRef msg_gv = zan_irgen_intern_string(g, mbuf);
    uint32_t line_arg = g->hide_diagnostics ? 0 : loc.line;
    uint32_t col_arg = g->hide_diagnostics ? 0 : loc.col;
    emit_guard_report3(g, file_gv, line_arg, col_arg, msg_gv);
    LLVMBuildBr(g->builder, cont_bb);

    LLVMPositionBuilderAtEnd(g->builder, cont_bb);
}

static void emit_string_null_report(zan_irgen_t *g, LLVMValueRef payload,
                                    zan_loc_t loc) {
    if (!g->rt_guard_split) {
        char buf[160];
        const char *file0 = loc_site_file(g, loc);
        if (g->hide_diagnostics) {
            snprintf(buf, sizeof(buf),
                     "%s: runtime error: null reference where a string/byte "
                     "buffer is required (length probe)\n",
                     file0);
        } else {
            snprintf(buf, sizeof(buf),
                     "%s:%u:%u: runtime error: null reference where a string/byte "
                     "buffer is required (length probe)\n",
                     file0, loc.line, loc.col);
        }
        emit_guard_report_merged(g, zan_irgen_intern_string(g, buf));
        return;
    }
    const char *file = loc_site_file(g, loc);
    LLVMValueRef file_gv = zan_irgen_intern_string(g, file);
    LLVMValueRef msg_gv = zan_irgen_intern_string(g,
        "null reference where a string/byte buffer is required (length probe)\n");
    uint32_t line_arg = g->hide_diagnostics ? 0 : loc.line;
    uint32_t col_arg = g->hide_diagnostics ? 0 : loc.col;
    emit_guard_report3(g, file_gv, line_arg, col_arg, msg_gv);
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

/* A507 越界写保护（TASKS.md）：与 emit_index_range_check 同一谓词，但把
 * "坏"标志返回给调用方——store 位点用它做 cond_br 跳过写入。曾
 * `a[9]=99` 诊断照发后仍钳位落进槽 0（静默覆盖 a[0]）。 */
static LLVMValueRef emit_index_oob_pred(zan_irgen_t *g, LLVMValueRef index,
                                        LLVMValueRef length) {
    LLVMTypeRef i1 = LLVMInt1TypeInContext(g->ctx);
    if (!g->runtime_checks || !g->current_fn)
        return LLVMConstInt(i1, 0, 0);
    if (LLVMGetTypeKind(LLVMTypeOf(index)) != LLVMIntegerTypeKind ||
        LLVMGetTypeKind(LLVMTypeOf(length)) != LLVMIntegerTypeKind)
        return LLVMConstInt(i1, 0, 0);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    index = emit_index_i64(g, index, "oob.idx");
    length = emit_index_i64(g, length, "oob.len");
    LLVMValueRef neg = zan_icmp(g->builder, LLVMIntSLT, index,
                                LLVMConstInt(i64, 0, 0), "oob.neg");
    LLVMValueRef high = zan_icmp(g->builder, LLVMIntSGE, index, length,
                                 "oob.high");
    return LLVMBuildOr(g->builder, neg, high, "oob.bad");
}

/* 越界写位点协议：split 在诊断之后切控制流（oob → end，否则 → do 块），
 * 调用方把 store（连着配套的 retain/release）发射在 do 块里，finish 收口。
 * end_bb 为 NULL 表示谓词恒假/检查关闭，store 照旧直落。 */
static void emit_oob_store_split(zan_irgen_t *g, LLVMValueRef oob,
                                 LLVMBasicBlockRef *do_bb,
                                 LLVMBasicBlockRef *end_bb) {
    *do_bb = NULL;
    *end_bb = NULL;
    if (!g->runtime_checks || !g->current_fn) return;
    if (LLVMIsConstant(oob)) return; /* 恒假：不切块 */
    LLVMValueRef fn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder));
    *do_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "sto.do");
    *end_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "sto.end");
    LLVMBuildCondBr(g->builder, oob, *end_bb, *do_bb);
    LLVMPositionBuilderAtEnd(g->builder, *do_bb);
}

static void emit_oob_store_finish(zan_irgen_t *g, LLVMBasicBlockRef do_bb,
                                  LLVMBasicBlockRef end_bb) {
    if (!end_bb) return;
    (void)do_bb;
    LLVMBuildBr(g->builder, end_bb);
    LLVMPositionBuilderAtEnd(g->builder, end_bb);
}

static LLVMValueRef emit_index_safe_check(zan_irgen_t *g, LLVMValueRef index,
                                          LLVMValueRef length, bool allow_end,
                                          zan_loc_t loc, const char *kind) {
    if (!g->runtime_checks || !g->current_fn) return index;
    emit_index_range_check(g, index, length, allow_end, loc, kind);
    if (LLVMGetTypeKind(LLVMTypeOf(index)) != LLVMIntegerTypeKind)
        return index;
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMValueRef idx = emit_index_i64(g, index, "safeidx.i64");
    LLVMValueRef len = emit_index_i64(g, length, "safeidx.len");
    LLVMValueRef negative = zan_icmp(g->builder, LLVMIntSLT, idx,
                                     LLVMConstInt(i64, 0, 0), "safeidx.neg");
    LLVMIntPredicate upper_pred = allow_end ? LLVMIntSGT : LLVMIntSGE;
    LLVMValueRef outside = zan_icmp(g->builder, upper_pred, idx, len,
                                    "safeidx.high");
    LLVMValueRef unsafe = LLVMBuildOr(g->builder, negative, outside,
                                      "safeidx.bad");
    return LLVMBuildSelect(g->builder, unsafe,
                           LLVMConstInt(LLVMTypeOf(index), 0, 0), index,
                           "safeidx.ok");
}

static LLVMValueRef emit_index_safe_bounds(zan_irgen_t *g, LLVMValueRef index,
                                           LLVMValueRef length, zan_loc_t loc,
                                           const char *kind) {
    return emit_index_safe_check(g, index, length, false, loc, kind);
}

static LLVMValueRef emit_string_base_guard(zan_irgen_t *g, LLVMValueRef payload,
                                           zan_loc_t loc) {
    if (!g->runtime_checks || !g->current_fn) return payload;
    if (LLVMGetTypeKind(LLVMTypeOf(payload)) != LLVMPointerTypeKind) return payload;
    LLVMValueRef fn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder));
    if (!fn) return payload;
    LLVMValueRef isnull = zan_icmp(g->builder, LLVMIntEQ, payload,
        LLVMConstNull(LLVMTypeOf(payload)), "strbase.null");
    LLVMBasicBlockRef fast_bb = LLVMGetInsertBlock(g->builder);
    LLVMBasicBlockRef slow_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "strbase.null");
    LLVMBasicBlockRef done_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "strbase.done");
    LLVMBuildCondBr(g->builder, isnull, slow_bb, done_bb);
    LLVMPositionBuilderAtEnd(g->builder, slow_bb);
    emit_runtime_check(g, isnull, loc,
        "null reference where a string/byte buffer is required (element access)");
    LLVMValueRef scratch = emit_soft_scratch_cached(g);
    scratch = LLVMBuildBitCast(g->builder, scratch, LLVMTypeOf(payload),
                               "soft.scratch.bc");
    LLVMBasicBlockRef slow_end = LLVMGetInsertBlock(g->builder);
    LLVMBuildBr(g->builder, done_bb);
    LLVMPositionBuilderAtEnd(g->builder, done_bb);
    LLVMValueRef phi = LLVMBuildPhi(g->builder, LLVMTypeOf(payload), "strbase.phi");
    LLVMValueRef vals[2] = { payload, scratch };
    LLVMBasicBlockRef bbs[2] = { fast_bb, slow_end };
    LLVMAddIncoming(phi, vals, bbs, 2);
    return phi;
}

static LLVMValueRef emit_soft_scratch_cached(zan_irgen_t *g) {
    LLVMBasicBlockRef cur = LLVMGetInsertBlock(g->builder);
    LLVMValueRef fn = LLVMGetBasicBlockParent(cur);
    LLVMTypeRef i8p = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    if (!g->soft_scratch_slot || g->soft_scratch_fn != fn) {
        g->soft_scratch_slot = emit_entry_alloca(g, i8p, "soft.scratch.slot");
        LLVMBasicBlockRef saved = LLVMGetInsertBlock(g->builder);
        LLVMBasicBlockRef entry = LLVMGetEntryBasicBlock(fn);
        LLVMValueRef term = LLVMGetBasicBlockTerminator(entry);
        if (term) LLVMPositionBuilderBefore(g->builder, term);
        else LLVMPositionBuilderAtEnd(g->builder, entry);
        LLVMTypeRef fn_ty = LLVMFunctionType(i8p, NULL, 0, 0);
        LLVMValueRef sf = LLVMGetNamedFunction(g->mod, "zan_rt_soft_scratch");
        if (!sf) sf = LLVMAddFunction(g->mod, "zan_rt_soft_scratch", fn_ty);
        LLVMValueRef scratch = zan_call2(g->builder, fn_ty, sf, NULL, 0,
                                         "soft.scratch.init");
        LLVMBuildStore(g->builder, scratch, g->soft_scratch_slot);
        LLVMPositionBuilderAtEnd(g->builder, saved);
        g->soft_scratch_fn = fn;
    }
    return LLVMBuildLoad2(g->builder, i8p, g->soft_scratch_slot, "soft.scratch");
}

static LLVMValueRef emit_soft_base_select(zan_irgen_t *g, LLVMValueRef base,
                                          LLVMValueRef isnull, zan_loc_t loc);
static LLVMValueRef emit_string_elem_guard(zan_irgen_t *g, LLVMValueRef payload,
                                           LLVMValueRef index, zan_loc_t loc,
                                           LLVMValueRef *arr_ptr_out) {
    if (!g->runtime_checks || !g->current_fn) return index;
    LLVMValueRef isnull = zan_icmp(g->builder, LLVMIntEQ, payload,
        LLVMConstNull(LLVMTypeOf(payload)), "stridx.null");
    emit_runtime_check(g, isnull, loc,
        "null reference where a string/byte buffer is required (element access)");
    payload = emit_soft_base_select(g, payload, isnull, loc);
    if (arr_ptr_out) *arr_ptr_out = payload;
    LLVMValueRef unsafe = isnull;
    if (LLVMGetTypeKind(LLVMTypeOf(index)) != LLVMIntegerTypeKind) return index;
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMValueRef idx = emit_index_i64(g, index, "stridx.i64");
    LLVMValueRef neg = zan_icmp(g->builder, LLVMIntSLT, idx,
                                LLVMConstInt(i64, 0, 0), "stridx.neg");
    emit_runtime_check(g, neg, loc, "string index out of bounds");
    unsafe = LLVMBuildOr(g->builder, unsafe, neg, "stridx.unsafe");
    return LLVMBuildSelect(g->builder, unsafe,
                           LLVMConstInt(LLVMTypeOf(idx), 0, 0), idx,
                           "stridx.safe");
}

static LLVMValueRef emit_soft_base_select(zan_irgen_t *g, LLVMValueRef base,
                                          LLVMValueRef isnull, zan_loc_t loc) {
    if (!g->runtime_checks || !g->current_fn) return base;
    if (LLVMGetTypeKind(LLVMTypeOf(base)) != LLVMPointerTypeKind) return base;
    LLVMValueRef scratch = emit_soft_scratch_cached(g);
    scratch = LLVMBuildBitCast(g->builder, scratch, LLVMTypeOf(base),
                               "soft.scratch.bc");
    return LLVMBuildSelect(g->builder, isnull, scratch, base, "soft.base");
}

static LLVMValueRef emit_string_len_ex(zan_irgen_t *g, LLVMValueRef payload,
                                      int array_count, zan_loc_t loc) {
    LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
    LLVMTypeRef i8p = LLVMPointerType(i8, 0);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i64p = LLVMPointerType(i64, 0);
    LLVMTypeRef strlen_ty = LLVMFunctionType(i64, (LLVMTypeRef[]){ i8p }, 1, 0);
    if (LLVMGetTypeKind(LLVMTypeOf(payload)) != LLVMPointerTypeKind)
        return zan_call2(g->builder, strlen_ty, g->fn_strlen, &payload, 1,
                         "strbuf.strlen");

    LLVMBasicBlockRef entry_bb = LLVMGetInsertBlock(g->builder);
    LLVMValueRef fn = LLVMGetBasicBlockParent(entry_bb);
    LLVMBasicBlockRef probe_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "strbuf.probe");
    LLVMBasicBlockRef arr_bb = array_count
        ? LLVMAppendBasicBlockInContext(g->ctx, fn, "strbuf.arr") : NULL;
    LLVMBasicBlockRef slow_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "strbuf.slow");
    LLVMBasicBlockRef stamp_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "strbuf.stamp");
    LLVMBasicBlockRef stamp_do_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "strbuf.stampdo");
    LLVMBasicBlockRef slow_end_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "strbuf.slowend");
    LLVMBasicBlockRef raw_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "strbuf.raw");
    LLVMBasicBlockRef done_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "strbuf.done");

    LLVMValueRef hdr_ptr = LLVMBuildGEP2(g->builder, i8, payload,
        &(LLVMValueRef){ LLVMConstInt(i64, (uint64_t)ZAN_OBJ_SITE_OFF, 1) }, 1,
        "strbuf.hdrp");
    LLVMValueRef read_ok = zan_hdr_read_ok(g, payload);
    LLVMValueRef probe_ok = zan_icmp(g->builder, LLVMIntNE, payload,
        LLVMConstNull(LLVMTypeOf(payload)), "strbuf.nonnull");
    if (read_ok) probe_ok = zan_and(g->builder, probe_ok, read_ok, "strbuf.ok");
    LLVMBuildCondBr(g->builder, probe_ok, probe_bb, raw_bb);

    LLVMPositionBuilderAtEnd(g->builder, probe_bb);
    LLVMValueRef hdr_iptr = LLVMBuildBitCast(g->builder, hdr_ptr, i64p,
                                             "strbuf.hdrip");
    LLVMValueRef word = LLVMBuildLoad2(g->builder, i64, hdr_iptr, "strbuf.hdr");
    LLVMValueRef is_str = zan_hdr_is_string(g, word, "strbuf.isstr");
    LLVMValueRef cached = LLVMBuildAnd(g->builder, word,
        LLVMConstInt(i64, ZAN_STR_LEN_MASK, 0), "strbuf.cached");
    LLVMValueRef measured = zan_icmp(g->builder, LLVMIntNE, cached,
        LLVMConstInt(i64, ZAN_STR_LEN_UNKNOWN, 0), "strbuf.measured");
    LLVMValueRef known = zan_and(g->builder, is_str, measured, "strbuf.known");
    LLVMBuildCondBr(g->builder, known, done_bb,
                    arr_bb ? arr_bb : slow_bb);

    LLVMValueRef count = NULL;
    LLVMValueRef phi_raw = NULL;
    LLVMBasicBlockRef phi_raw_pred = NULL;
    if (arr_bb) {
        LLVMBasicBlockRef arr_test_bb = arr_bb;
        arr_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "strbuf.arrcount");
        LLVMPositionBuilderAtEnd(g->builder, arr_test_bb);
        LLVMValueRef is_array = zan_icmp(g->builder, LLVMIntEQ, word,
            LLVMConstInt(i64, ZAN_ARRAY_MAGIC, 0), "strbuf.isarr");
        LLVMBuildCondBr(g->builder, is_array, arr_bb, slow_bb);
        LLVMPositionBuilderAtEnd(g->builder, arr_bb);
        LLVMValueRef cnt_ptr = LLVMBuildGEP2(g->builder, i8, payload,
            &(LLVMValueRef){ LLVMConstInt(i64, (uint64_t)ZAN_OBJ_RC_OFF, 1) }, 1,
            "strbuf.countp");
        count = LLVMBuildLoad2(g->builder,
            i64, LLVMBuildBitCast(g->builder, cnt_ptr, i64p, "strbuf.countip"),
            "strbuf.count");
        LLVMBuildBr(g->builder, done_bb);
    }

    LLVMPositionBuilderAtEnd(g->builder, slow_bb);
    LLVMValueRef string_len = zan_call2(g->builder, strlen_ty, g->fn_strlen,
                                        &payload, 1, "strbuf.strlen");
    LLVMValueRef fits = zan_and(g->builder,
        zan_icmp(g->builder, LLVMIntNE, string_len,
                 LLVMConstInt(i64, ZAN_STR_LEN_UNKNOWN, 0), "strbuf.notunk"),
        zan_icmp(g->builder, LLVMIntULE, string_len,
                 LLVMConstInt(i64, ZAN_STR_LEN_MASK, 0), "strbuf.fits32"),
        "strbuf.fits");
    LLVMBuildCondBr(g->builder, zan_and(g->builder, is_str, fits,
                                        "strbuf.stampable"),
                    stamp_bb, slow_end_bb);

    LLVMPositionBuilderAtEnd(g->builder, stamp_bb);
    LLVMValueRef rc_ptr = LLVMBuildGEP2(g->builder, i8, payload,
        &(LLVMValueRef){ LLVMConstInt(i64, (uint64_t)ZAN_OBJ_RC_OFF, 1) }, 1,
        "strbuf.rcp");
    LLVMValueRef rc = LLVMBuildLoad2(g->builder, i64,
        LLVMBuildBitCast(g->builder, rc_ptr, i64p, "strbuf.rcip"), "strbuf.rc");
    LLVMBuildCondBr(g->builder,
        zan_icmp(g->builder, LLVMIntNE, rc,
                 LLVMConstInt(i64, ZAN_STRING_SENTINEL_RC, 0),
                 "strbuf.writable"),
        stamp_do_bb, slow_end_bb);

    LLVMPositionBuilderAtEnd(g->builder, stamp_do_bb);
    LLVMBuildStore(g->builder, LLVMBuildOr(g->builder,
            LLVMConstInt(i64, ZAN_STRING_TAG << 32, 0), string_len,
            "strbuf.newhdr"),
        hdr_iptr);
    LLVMBuildBr(g->builder, slow_end_bb);

    LLVMPositionBuilderAtEnd(g->builder, slow_end_bb);
    LLVMBuildBr(g->builder, done_bb);

    LLVMPositionBuilderAtEnd(g->builder, raw_bb);
    if (g->runtime_checks) {
        emit_string_null_report(g, payload, loc);
        LLVMBuildBr(g->builder, done_bb);
        phi_raw = LLVMConstInt(i64, 0, 0);
        phi_raw_pred = LLVMGetInsertBlock(g->builder);
    } else {
        phi_raw = zan_call2(g->builder, strlen_ty, g->fn_strlen,
                            &payload, 1, "strbuf.rawlen");
        LLVMBuildBr(g->builder, done_bb);
        phi_raw_pred = raw_bb;
    }
    LLVMPositionBuilderAtEnd(g->builder, done_bb);
    LLVMValueRef phi = LLVMBuildPhi(g->builder, i64, "strbuf.len");
    LLVMAddIncoming(phi, (LLVMValueRef[]){ cached },
                    (LLVMBasicBlockRef[]){ probe_bb }, 1);
    if (arr_bb)
        LLVMAddIncoming(phi, (LLVMValueRef[]){ count },
                        (LLVMBasicBlockRef[]){ arr_bb }, 1);
    LLVMAddIncoming(phi, (LLVMValueRef[]){ string_len },
                    (LLVMBasicBlockRef[]){ slow_end_bb }, 1);
    if (phi_raw)
        LLVMAddIncoming(phi, (LLVMValueRef[]){ phi_raw },
                        (LLVMBasicBlockRef[]){ phi_raw_pred }, 1);
    return phi;
}

static void emit_string_len_invalidate(zan_irgen_t *g, LLVMValueRef payload) {
    if (LLVMGetTypeKind(LLVMTypeOf(payload)) != LLVMPointerTypeKind) return;
    LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i64p = LLVMPointerType(i64, 0);
    LLVMBasicBlockRef entry_bb = LLVMGetInsertBlock(g->builder);
    LLVMValueRef fn = LLVMGetBasicBlockParent(entry_bb);
    LLVMBasicBlockRef probe_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "strinv.probe");
    LLVMBasicBlockRef clear_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "strinv.clear");
    LLVMBasicBlockRef done_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "strinv.done");
    LLVMValueRef hdr_ptr = LLVMBuildGEP2(g->builder, i8, payload,
        &(LLVMValueRef){ LLVMConstInt(i64, (uint64_t)ZAN_OBJ_SITE_OFF, 1) }, 1,
        "strinv.hdrp");
    LLVMValueRef read_ok = zan_hdr_read_ok(g, payload);
    LLVMValueRef nonnull = zan_icmp(g->builder, LLVMIntNE, payload,
        LLVMConstNull(LLVMTypeOf(payload)), "strinv.nonnull");
    if (read_ok) nonnull = zan_and(g->builder, nonnull, read_ok, "strinv.ok");
    LLVMBuildCondBr(g->builder, nonnull, probe_bb, done_bb);

    LLVMPositionBuilderAtEnd(g->builder, probe_bb);
    LLVMValueRef hdr_iptr = LLVMBuildBitCast(g->builder, hdr_ptr, i64p,
                                             "strinv.hdrip");
    LLVMValueRef word = LLVMBuildLoad2(g->builder, i64, hdr_iptr, "strinv.hdr");
    LLVMValueRef stale = zan_and(g->builder,
        zan_hdr_is_string(g, word, "strinv.isstr"),
        zan_icmp(g->builder, LLVMIntNE,
                 LLVMBuildAnd(g->builder, word,
                     LLVMConstInt(i64, ZAN_STR_LEN_MASK, 0), "strinv.cached"),
                 LLVMConstInt(i64, ZAN_STR_LEN_UNKNOWN, 0), "strinv.measured"),
        "strinv.stale");
    LLVMBuildCondBr(g->builder, stale, clear_bb, done_bb);

    LLVMPositionBuilderAtEnd(g->builder, clear_bb);
    LLVMValueRef rc_ptr = LLVMBuildGEP2(g->builder, i8, payload,
        &(LLVMValueRef){ LLVMConstInt(i64, (uint64_t)ZAN_OBJ_RC_OFF, 1) }, 1,
        "strinv.rcp");
    LLVMValueRef rc = LLVMBuildLoad2(g->builder, i64,
        LLVMBuildBitCast(g->builder, rc_ptr, i64p, "strinv.rcip"), "strinv.rc");
    LLVMBasicBlockRef store_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "strinv.store");
    LLVMBuildCondBr(g->builder,
        zan_icmp(g->builder, LLVMIntNE, rc,
                 LLVMConstInt(i64, ZAN_STRING_SENTINEL_RC, 0),
                 "strinv.writable"),
        store_bb, done_bb);
    LLVMPositionBuilderAtEnd(g->builder, store_bb);
    LLVMBuildStore(g->builder,
                   LLVMConstInt(i64, ZAN_STR_HDR_WORD(ZAN_STR_LEN_UNKNOWN), 0),
                   hdr_iptr);
    LLVMBuildBr(g->builder, done_bb);

    LLVMPositionBuilderAtEnd(g->builder, done_bb);
}

static void emit_string_len_set(zan_irgen_t *g, LLVMValueRef payload,
                                LLVMValueRef len) {
    LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i64p = LLVMPointerType(i64, 0);
    LLVMValueRef fits = zan_and(g->builder,
        zan_icmp(g->builder, LLVMIntULE, len,
                 LLVMConstInt(i64, ZAN_STR_LEN_MASK, 0), "strset.fits32"),
        zan_icmp(g->builder, LLVMIntNE, len,
                 LLVMConstInt(i64, ZAN_STR_LEN_UNKNOWN, 0), "strset.notunk"),
        "strset.ok");
    LLVMValueRef half = LLVMBuildSelect(g->builder, fits, len,
        LLVMConstInt(i64, ZAN_STR_LEN_UNKNOWN, 0), "strset.half");
    LLVMValueRef word = LLVMBuildOr(g->builder,
        LLVMConstInt(i64, ZAN_STRING_TAG << 32, 0), half, "strset.hdr");
    LLVMValueRef hdr_ptr = LLVMBuildGEP2(g->builder, i8, payload,
        &(LLVMValueRef){ LLVMConstInt(i64, (uint64_t)ZAN_OBJ_SITE_OFF, 1) }, 1,
        "strset.hdrp");
    LLVMBuildStore(g->builder, word,
        LLVMBuildBitCast(g->builder, hdr_ptr, i64p, "strset.hdrip"));
}

static bool sym_declares_extern(zan_symbol_t *sym) {
    if (!sym || !sym->decl || sym->decl->kind != AST_METHOD_DECL) return false;
    return zan_ast_method_extern_lib(sym->decl).str != NULL ||
           (sym->decl->method_decl.modifiers & MOD_EXTERN) != 0;
}

static zan_symbol_t *extern_check_callee_sym(zan_irgen_t *g,
                                             zan_ast_node_t *call) {
    zan_ast_node_t *callee = call->call.callee;
    if (!callee) return NULL;
    if (callee->kind == AST_IDENTIFIER) {
        if (g->current_type_sym)
            return get_method_sym(g->current_type_sym,
                                  callee->ident.name);
        return zan_binder_lookup(g->binder, callee->ident.name);
    }
    if (callee->kind == AST_MEMBER_ACCESS &&
        callee->member.object->kind == AST_IDENTIFIER) {
        zan_symbol_t *cls = zan_binder_lookup(g->binder,
            callee->member.object->ident.name);
        if (cls) return get_method_sym(cls, callee->member.name);
    }
    return NULL;
}

static void emit_extern_call_len_invalidate(zan_irgen_t *g, LLVMValueRef fn,
                                            LLVMValueRef *args, int argc,
                                            zan_ast_node_t *call,
                                            local_scope_t *locals) {
    if (!fn || !args || argc <= 0 || !call) return;
    if (!LLVMIsAFunction(fn)) return;
    zan_symbol_t *cs = extern_check_callee_sym(g, call);
    if (!sym_declares_extern(cs)) return;
    int m = call->call.args.count;
    if (m <= 0 || m > argc) return;
    for (int i = 0; i < m; i++) {
        LLVMValueRef v = args[argc - m + i];
        if (!v || LLVMGetTypeKind(LLVMTypeOf(v)) != LLVMPointerTypeKind)
            continue;
        if (!is_string_expr(g, call->call.args.items[i], locals)) continue;
        emit_string_len_invalidate(g, v);
    }
}

static LLVMValueRef emit_string_buffer_len(zan_irgen_t *g, LLVMValueRef payload,
                                           zan_loc_t loc) {
    return emit_string_len_ex(g, payload, 1, loc);
}

/* `string */
static LLVMValueRef emit_string_length(zan_irgen_t *g, LLVMValueRef payload,
                                       zan_loc_t loc) {
    return emit_string_len_ex(g, payload, 1, loc);
}

static LLVMValueRef get_str_ordinal_cmp_fn(zan_irgen_t *g, zan_loc_t loc) {
    LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "__zan_str_ocmp");
    if (fn) return fn;
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef fnty = LLVMFunctionType(i32t, (LLVMTypeRef[]){ i8ptr, i8ptr },
                                        2, 0);
    fn = LLVMAddFunction(g->mod, "__zan_str_ocmp", fnty);
    LLVMSetLinkage(fn, LLVMInternalLinkage);
    LLVMTypeRef memcmp_ty = LLVMFunctionType(i32t,
        (LLVMTypeRef[]){ i8ptr, i8ptr, i64 }, 3, 0);
    LLVMValueRef memcmp_fn = get_libc_fn(g, "memcmp", memcmp_ty);

    LLVMBasicBlockRef saved_bb = LLVMGetInsertBlock(g->builder);
    LLVMValueRef saved_fn = g->current_fn;
    g->current_fn = fn;
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
    LLVMBasicBlockRef ret0 = LLVMAppendBasicBlockInContext(g->ctx, fn, "same");
    LLVMBasicBlockRef nullbb = LLVMAppendBasicBlockInContext(g->ctx, fn, "null");
    LLVMBasicBlockRef retneg = LLVMAppendBasicBlockInContext(g->ctx, fn, "neq");
    LLVMBasicBlockRef body = LLVMAppendBasicBlockInContext(g->ctx, fn, "body");
    LLVMValueRef a = LLVMGetParam(fn, 0);
    LLVMValueRef b2 = LLVMGetParam(fn, 1);

    LLVMPositionBuilderAtEnd(g->builder, entry);
    LLVMBuildCondBr(g->builder,
        zan_icmp(g->builder, LLVMIntEQ, a, b2, "same"),
        ret0, nullbb);
    LLVMPositionBuilderAtEnd(g->builder, ret0);
    LLVMBuildRet(g->builder, LLVMConstInt(i32t, 0, 0));
    LLVMPositionBuilderAtEnd(g->builder, nullbb);
    LLVMValueRef an = zan_icmp(g->builder, LLVMIntEQ, a,
        LLVMConstNull(i8ptr), "an");
    LLVMValueRef bn = zan_icmp(g->builder, LLVMIntEQ, b2,
        LLVMConstNull(i8ptr), "bn");
    LLVMBuildCondBr(g->builder, zan_or(g->builder, an, bn, "en"),
        retneg, body);
    LLVMPositionBuilderAtEnd(g->builder, retneg);
    LLVMBuildRet(g->builder, LLVMConstInt(i32t, (uint64_t)-1, 1));
    LLVMPositionBuilderAtEnd(g->builder, body);
    LLVMValueRef la = emit_string_len_ex(g, a, 1, loc);
    LLVMValueRef lb = emit_string_len_ex(g, b2, 1, loc);
    LLVMValueRef llt = zan_icmp(g->builder, LLVMIntSLT, la, lb, "llt");
    LLVMValueRef m = LLVMBuildSelect(g->builder, llt, la, lb, "minlen");
    LLVMValueRef mr = zan_call2(g->builder, memcmp_ty, memcmp_fn,
        (LLVMValueRef[]){ a, b2, m }, 3, "ocmp.memcmp");
    LLVMValueRef tail = LLVMBuildSelect(g->builder, llt,
        LLVMConstInt(i32t, (uint64_t)-1, 1), LLVMConstInt(i32t, 1, 0),
        "tail");
    LLVMValueRef inner = LLVMBuildSelect(g->builder,
        zan_icmp(g->builder, LLVMIntNE, mr, LLVMConstInt(i32t, 0, 0), "nz"),
        mr, tail, "body");
    LLVMValueRef r = LLVMBuildSelect(g->builder,
        zan_icmp(g->builder, LLVMIntEQ, la, lb, "leq"), mr, inner, "ocmp");
    LLVMBuildRet(g->builder, r);

    g->current_fn = saved_fn;
    if (saved_bb) LLVMPositionBuilderAtEnd(g->builder, saved_bb);
    return fn;
}

static void emit_span_window_check(zan_irgen_t *g, LLVMValueRef start,
                                   LLVMValueRef window, LLVMValueRef length,
                                   zan_loc_t loc, const char *kind) {
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
    char msg[96];
    snprintf(msg, sizeof(msg), "%s range out of bounds", kind ? kind : "span");
    emit_runtime_check(g, bad, loc, msg);
}

static void emit_span_safe_window(zan_irgen_t *g, LLVMValueRef *start_out,
                                  LLVMValueRef *window_out,
                                  LLVMValueRef length, zan_loc_t loc,
                                  const char *kind) {
    if (!g->runtime_checks || !g->current_fn) return;
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMValueRef start = emit_index_i64(g, *start_out, "safe.start");
    LLVMValueRef window = emit_index_i64(g, *window_out, "safe.count");
    length = emit_index_i64(g, length, "safe.length");
    LLVMValueRef bad_start = zan_icmp(g->builder, LLVMIntSLT, start,
                                      LLVMConstInt(i64, 0, 0), "safe.start.neg");
    LLVMValueRef bad_window = zan_icmp(g->builder, LLVMIntSLT, window,
                                      LLVMConstInt(i64, 0, 0), "safe.count.neg");
    LLVMValueRef after_end = zan_icmp(g->builder, LLVMIntSGT, start, length,
                                      "safe.start.high");
    LLVMValueRef remaining = LLVMBuildSub(g->builder, length, start,
                                          "safe.remaining");
    LLVMValueRef too_long = zan_icmp(g->builder, LLVMIntSGT, window, remaining,
                                     "safe.count.high");
    LLVMValueRef bad = LLVMBuildOr(g->builder, bad_start, bad_window, "safe.bad0");
    bad = LLVMBuildOr(g->builder, bad, after_end, "safe.bad1");
    bad = LLVMBuildOr(g->builder, bad, too_long, "safe.bad2");
    *start_out = LLVMBuildSelect(g->builder, bad,
                                 LLVMConstInt(i64, 0, 0), start, "safe.start.ok");
    *window_out = LLVMBuildSelect(g->builder, bad,
                                  LLVMConstInt(i64, 0, 0), window, "safe.count.ok");
}

static LLVMValueRef struct_base_ptr(zan_irgen_t *g, local_var_t *local, LLVMTypeRef st) {
    LLVMValueRef base = local->alloca;
    if (local->box_cell && local->type && local->type->kind == TYPE_CLASS) {
        base = LLVMBuildLoad2(g->builder, LLVMPointerType(st, 0), base, "objld");
    } else if (LLVMIsAAllocaInst(base)) {
        LLVMTypeRef alloc_t = LLVMGetAllocatedType(base);
        if (LLVMGetTypeKind(alloc_t) == LLVMPointerTypeKind) {
            base = LLVMBuildLoad2(g->builder, LLVMPointerType(st, 0), base, "objld");
        }
    } else if (local->byref_slot && local->type &&
               local->type->kind == TYPE_CLASS) {
        base = LLVMBuildLoad2(g->builder, LLVMPointerType(st, 0), base, "objld");
    }
    return base;
}

static void coerce_int_pair(zan_irgen_t *g, LLVMValueRef *a, LLVMValueRef *b) {
    LLVMTypeRef ta = LLVMTypeOf(*a), tb = LLVMTypeOf(*b);
    /* 浮点二元运算提升规则：float 与 double 运算提升至 double 精度计算 */
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
    /* 混合类型乘法隐式提升：整数端隐式转换为浮点精度参与计算 */
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

static LLVMValueRef coerce_int_to(zan_irgen_t *g, LLVMValueRef v, LLVMTypeRef target) {
    if (!v || !target) return v;
    LLVMTypeRef vt = LLVMTypeOf(v);
    if (vt == target) return v;
    if (llvm_is_nullable(target)) {
        if (LLVMGetTypeKind(vt) == LLVMPointerTypeKind)
            return LLVMIsNull(v) ? nullable_none(target) : v;
        if (llvm_is_nullable(vt)) {
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
    if (LLVMIsNull(v)) return LLVMConstNull(target);
    if (LLVMGetTypeKind(vt) == LLVMPointerTypeKind &&
        LLVMGetTypeKind(target) == LLVMIntegerTypeKind)
        return LLVMBuildPtrToInt(g->builder, v, target, "p2i");
    if (LLVMGetTypeKind(vt) == LLVMIntegerTypeKind &&
        LLVMGetTypeKind(target) == LLVMPointerTypeKind)
        return LLVMBuildIntToPtr(g->builder, v, target, "i2p");
    if (LLVMGetTypeKind(target) == LLVMFloatTypeKind &&
        LLVMGetTypeKind(vt) == LLVMDoubleTypeKind)
        return LLVMBuildFPTrunc(g->builder, v, target, "fit.fptrunc");
    if (LLVMGetTypeKind(target) == LLVMDoubleTypeKind &&
        LLVMGetTypeKind(vt) == LLVMFloatTypeKind)
        return LLVMBuildFPExt(g->builder, v, target, "fit.fpext");
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
            target = src;
        }
    }
    if (target && llvm_is_nullable(target) && LLVMTypeOf(val) != target)
        return LLVMBuildStore(g->builder, coerce_int_to(g, val, target), ptr);
    if (target && LLVMGetTypeKind(target) == LLVMStructTypeKind &&
        LLVMGetTypeKind(LLVMTypeOf(val)) == LLVMPointerTypeKind)
        val = LLVMBuildLoad2(g->builder, target, val, "sv.copy");
    if (target) val = coerce_int_to(g, val, target);
    if (target && LLVMTypeOf(val) != target &&
        LLVMGetTypeKind(LLVMTypeOf(val)) == LLVMPointerTypeKind &&
        (LLVMGetTypeKind(target) == LLVMIntegerTypeKind ||
         LLVMGetTypeKind(target) == LLVMFloatTypeKind ||
         LLVMGetTypeKind(target) == LLVMDoubleTypeKind))
        val = emit_boundary_coerce(g, val, target);
    return LLVMBuildStore(g->builder, val, ptr);
}

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

static LLVMValueRef emit_entry_scratch(zan_irgen_t *g, unsigned size,
                                       const char *name) {
    LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef arr = LLVMArrayType(i8, size);
    LLVMValueRef slot = emit_entry_alloca(g, arr, name);
    LLVMValueRef idxs[] = { LLVMConstInt(i64, 0, 0), LLVMConstInt(i64, 0, 0) };
    return LLVMBuildInBoundsGEP2(g->builder, arr, slot, idxs, 2, name);
}

/* bool -> 托管串："true" / "false"。
   所有把值转成字符串的路径都要走这一条：i1 落进 itoa 会渲染成 "1"/"0"，
   同一个 bool 在不同上下文里长得不一样（`Convert.ToString(b)` -> "1"，
   `"x" + b` -> "x1"，而 `b.ToString()` -> "true"）。C# 的口径是 true/false。 */
LLVMValueRef emit_bool_to_cstr(zan_irgen_t *g, LLVMValueRef v) {
    zan_istr_t t = { "true", 4 }, f = { "false", 5 };
    return LLVMBuildSelect(g->builder, v,
        emit_string_literal_rc(g, t), emit_string_literal_rc(g, f), "b2s");
}

/* bool 的**语义类型**优先于 LLVM 值的宽度。泛型实例化的方法返回值
   （List<T>.Contains 这类）被提升成 i64，按宽度判就掉进 itoa，渲染成 "1"——
   同一个 bool 在不同上下文里长得不一样（插值 -> "1"，而 b.ToString() -> "true"）。
   C# 的口径是 true/false，所有取串路径都要按语义类型判。
   返回 i1 条件（非零为真）；这个值不是 bool 就返回 NULL。 */
static LLVMValueRef emit_bool_cond(zan_irgen_t *g, LLVMValueRef val,
                                    zan_ast_node_t *ast, local_scope_t *locals) {
    if (!ast || !val) return NULL;
    zan_type_t *bt = infer_expr_type(g, ast, locals);
    if (!bt || bt->kind != TYPE_BOOL) return NULL;
    LLVMTypeRef vk = LLVMTypeOf(val);
    if (LLVMGetTypeKind(vk) != LLVMIntegerTypeKind) return NULL;
    if (LLVMGetIntTypeWidth(vk) == 1) return val;
    return LLVMBuildICmp(g->builder, LLVMIntNE, val,
                         LLVMConstInt(vk, 0, 0), "bcond");
}

static LLVMValueRef emit_value_as_cstr(zan_irgen_t *g, LLVMValueRef v) {
    LLVMTypeRef vt = LLVMTypeOf(v);
    if (LLVMGetTypeKind(vt) == LLVMPointerTypeKind) return v;
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    if (LLVMGetTypeKind(vt) == LLVMIntegerTypeKind && LLVMGetIntTypeWidth(vt) == 1) {
        return emit_bool_to_cstr(g, v);
    }
    LLVMValueRef buf = emit_entry_scratch(g, 40, "v2s.buf");
    if (LLVMGetTypeKind(vt) == LLVMDoubleTypeKind || LLVMGetTypeKind(vt) == LLVMFloatTypeKind) {
        emit_dbl_str(g, buf, LLVMConstInt(i64, 40, 0), v);
        return buf;
    }
    emit_itoa_into(g, buf, emit_widen_i64_for_print(g, v), 0);
    return buf;
}

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
    LLVMPositionBuilderAtEnd(g->builder, grow_bb);
    LLVMValueRef nc0 = zan_mul(g->builder, cap, LLVMConstInt(i64, 2, 0), "sbnc0");
    LLVMValueRef small = zan_icmp(g->builder, LLVMIntSLT, nc0, need, "sbsm");
    LLVMValueRef nc = LLVMBuildSelect(g->builder, small, need, nc0, "sbnc");
    LLVMValueRef tiny = zan_icmp(g->builder, LLVMIntSLT, nc,
                                 LLVMConstInt(i64, 64, 0), "sbtiny");
    nc = LLVMBuildSelect(g->builder, tiny, LLVMConstInt(i64, 64, 0), nc, "sbnc2");
    LLVMValueRef dptr = LLVMBuildStructGEP2(g->builder, g->sb_struct_type, sbp, 2, "sbdp");
    LLVMValueRef olddata = LLVMBuildLoad2(g->builder, i8ptr, dptr, "sbod");
    LLVMValueRef newdata = zan_call2(g->builder,
        LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i64 }, 2, 0),
        g->fn_realloc, (LLVMValueRef[]){ olddata, nc }, 2, "sbnd");
    zan_irgen_emit_oom_check(g, g->current_fn, newdata);
    LLVMBuildStore(g->builder, newdata, dptr);
    LLVMBuildStore(g->builder, nc, capptr);
    LLVMBuildBr(g->builder, st_bb);
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
    memcpy(blob + 8, &((uint64_t){ ZAN_STR_HDR_WORD(text.len) }), 8);
    if (text.len > 0) memcpy(blob + 16, text.str, (size_t)text.len);
    blob[16 + (size_t)text.len] = 0;
    bool obf = g->obfuscate_strings && text.len > 0;
    if (obf && !ZAN_TAB_ENSURE(g->obf_literals, g->obf_literal_count,
                               g->obf_literal_cap, 256)) {
        zan_loc_t oloc = {0};
        zan_diag_emit(g->diag, DIAG_ERROR, oloc,
                      "out of memory recording obfuscated string literals "
                      "(%d recorded)", g->obf_literal_count);
        obf = false;
    }
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

    if (ZAN_TAB_ENSURE(g->string_literals, g->string_literal_count,
                       g->string_literal_cap, 512)) {
        g->string_literals[g->string_literal_count].text = text;
        g->string_literals[g->string_literal_count].value = global;
        g->string_literal_count++;
    }
    return user_ptr;
}

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

    LLVMValueRef keyb[16];
    for (int i = 0; i < 16; i++) keyb[i] = LLVMConstInt(i8, g->obf_key[i], 0);
    LLVMTypeRef keyty = LLVMArrayType(i8, 16);
    LLVMValueRef keyg = LLVMAddGlobal(g->mod, keyty, "__zan.obf.key");
    LLVMSetLinkage(keyg, LLVMPrivateLinkage);
    LLVMSetGlobalConstant(keyg, 1);
    LLVMSetUnnamedAddr(keyg, LLVMGlobalUnnamedAddr);
    LLVMSetInitializer(keyg, LLVMConstArray(i8, keyb, 16));

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

    /* void __zan */
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

    bool is_elf = !g->target_is_windows && !g->target_is_macos && !g->target_is_ios
        && strncmp(g->target_triple, "wasm", 4) != 0;
    if (is_elf) {
        LLVMValueRef ent = LLVMAddGlobal(g->mod, ptr, "__zan.init_array.deobf");
        LLVMSetLinkage(ent, LLVMInternalLinkage);
        LLVMSetSection(ent, ".init_array");
        LLVMSetInitializer(ent, fn);
        LLVMTypeRef usedty = LLVMArrayType(ptr, 1);
        LLVMValueRef usedg = LLVMAddGlobal(g->mod, usedty, "llvm.used");
        LLVMSetLinkage(usedg, LLVMAppendingLinkage);
        LLVMSetSection(usedg, "llvm.metadata");
        LLVMSetInitializer(usedg, LLVMConstArray(ptr, &ent, 1));
    } else {
        /* register in llvm */
        LLVMValueRef fields[] = { LLVMConstInt(i32, 65535, 0), fn, LLVMConstNull(ptr) };
        LLVMValueRef entryc = LLVMConstStruct(fields, 3, 0);
        LLVMTypeRef arrty = LLVMArrayType(LLVMTypeOf(entryc), 1);
        LLVMValueRef gc = LLVMAddGlobal(g->mod, arrty, "llvm.global_ctors");
        LLVMSetLinkage(gc, LLVMAppendingLinkage);
        LLVMSetInitializer(gc, LLVMConstArray(LLVMTypeOf(entryc), &entryc, 1));
    }
}

/* Coerce a value to an i8* C string */
static LLVMValueRef emit_to_cstr_u(zan_irgen_t *g, LLVMValueRef val,
                                   bool is_unsigned) {
    LLVMTypeRef vt = LLVMTypeOf(val);
    LLVMTypeKind vtk = LLVMGetTypeKind(vt);
    if (vtk == LLVMPointerTypeKind) return val;
    /* bool 是 i1：统一渲染成 "true"/"false"（否则掉进 itoa 变成 "1"） */
    if (vtk == LLVMIntegerTypeKind && LLVMGetIntTypeWidth(vt) == 1) {
        return emit_bool_to_cstr(g, val);
    }

    /* 可空类型字符串格式化：有值输出值字符串，无值输出空字符串 "" */
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
        LLVMValueRef sv = emit_to_cstr_u(g, payload, is_unsigned);
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
    if (vt == get_decimal_type(g)) {
        LLVMValueRef dslot = emit_entry_alloca(g, vt, "dec.str.slot");
        LLVMBuildStore(g->builder, val, dslot);
        LLVMValueRef buf = emit_string_alloc_rc(g, LLVMConstInt(i64, 48, 0));
        LLVMTypeRef to_str_fn_ty = LLVMFunctionType(
            LLVMInt32TypeInContext(g->ctx),
            (LLVMTypeRef[]){ LLVMPointerType(vt, 0), i8ptr, i64 },
            3, 0);
        LLVMValueRef to_str_fn = get_libc_fn(g, "__zan_decimal_to_string", to_str_fn_ty);
        LLVMValueRef len = zan_call2(g->builder, to_str_fn_ty, to_str_fn,
            (LLVMValueRef[]){ dslot, buf, LLVMConstInt(i64, 48, 0) }, 3, "dec.slen");
        LLVMValueRef len64 = LLVMBuildZExt(g->builder, len, i64, "dec.slen64");
        emit_string_len_set(g, buf, len64);
        return buf;
    }
    if (vtk == LLVMDoubleTypeKind || vtk == LLVMFloatTypeKind) {
        LLVMValueRef buf = emit_string_alloc_rc(g, LLVMConstInt(i64, 40, 0));
        emit_dbl_str(g, buf, LLVMConstInt(i64, 40, 0), val);
        return buf;
    }
    LLVMValueRef ibuf = emit_string_alloc_rc(g, LLVMConstInt(i64, 24, 0));
    emit_itoa_into(g, ibuf, emit_widen_i64_for_print(g, val),
                   is_unsigned ? 1 : 0);
    return ibuf;
}

static LLVMValueRef emit_to_cstr(zan_irgen_t *g, LLVMValueRef val) {
    return emit_to_cstr_u(g, val, false);
}

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

    LLVMValueRef tmp = LLVMBuildAlloca(g->builder, i64t, "ch.tmp");
    LLVMBuildStore(bd, packed, tmp);
    LLVMValueRef buf = emit_string_alloc_rc(g, zan_add(bd, len, LLVMConstInt(i64t, 1, 0), "ch.sz"));
    LLVMTypeRef memcpy_type = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr, i64t }, 3, 0);
    LLVMValueRef memcpy_fn = LLVMGetNamedFunction(g->mod, "memcpy");
    if (!memcpy_fn) memcpy_fn = LLVMAddFunction(g->mod, "memcpy", memcpy_type);
    zan_call2(bd, memcpy_type, memcpy_fn, (LLVMValueRef[]){ buf, tmp, len }, 3, "");
    LLVMValueRef endp = LLVMBuildGEP2(bd, i8t, buf, &len, 1, "ch.end");
    LLVMBuildStore(bd, LLVMConstInt(i8t, 0, 0), endp);
    emit_string_len_set(g, buf,
        LLVMBuildSelect(bd, LLVMBuildICmp(bd, LLVMIntEQ, cp,
            LLVMConstInt(i64t, 0, 0), "ch.z"),
            LLVMConstInt(i64t, 0, 0), len, "ch.stamp"));
    return buf;
}

static LLVMValueRef emit_to_cstr_of(zan_irgen_t *g, LLVMValueRef val,
                                    zan_ast_node_t *ast, local_scope_t *locals) {
    if (ast && val) {
        LLVMTypeRef vt = LLVMTypeOf(val);
        if (LLVMGetTypeKind(vt) == LLVMStructTypeKind) {
            const char *sn = LLVMGetStructName(vt);
            if (sn && strncmp(sn, "zan.nullable.", 13) == 0) {
                LLVMTypeRef payty = LLVMStructGetTypeAtIndex(vt, 0);
                LLVMTypeKind pk = LLVMGetTypeKind(payty);
                if (pk == LLVMIntegerTypeKind || pk == LLVMFloatTypeKind ||
                    pk == LLVMDoubleTypeKind) {
                    zan_type_t *st = infer_expr_type(g, ast, locals);
                    bool uns = st && st->element_type &&
                               (st->element_type->kind == TYPE_UINT ||
                                st->element_type->kind == TYPE_ULONG);
                    LLVMValueRef pay = LLVMBuildExtractValue(g->builder, val, 0, "nl.pay");
                    LLVMValueRef has = LLVMBuildExtractValue(g->builder, val, 1, "nl.has");
                    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                    LLVMBasicBlockRef has_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "nl.has");
                    LLVMBasicBlockRef no_bb  = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "nl.no");
                    LLVMBasicBlockRef end_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "nl.end");
                    LLVMBuildCondBr(g->builder, has, has_bb, no_bb);
                    LLVMPositionBuilderAtEnd(g->builder, has_bb);
                    LLVMValueRef ps = emit_to_cstr_u(g, pay, uns);
                    LLVMBuildBr(g->builder, end_bb);
                    LLVMPositionBuilderAtEnd(g->builder, no_bb);
                    LLVMBuildBr(g->builder, end_bb);
                    LLVMPositionBuilderAtEnd(g->builder, end_bb);
                    LLVMValueRef phi = LLVMBuildPhi(g->builder, i8ptr, "nl.str");
                    LLVMValueRef vals[2] = { ps, LLVMConstNull(i8ptr) };
                    LLVMBasicBlockRef bbs[2] = { has_bb, no_bb };
                    LLVMAddIncoming(phi, vals, bbs, 2);
                    return phi;
                }
            }
        }
    }
    /* bool 的语义类型优先于 LLVM 值的宽度。泛型实例化的方法返回值
       （List<Big>.Contains 这类）被提升成 i64，按宽度判就掉进 itoa，
       渲染成 "1"——同一个 bool 在不同上下文里长得不一样（"x" + b -> "x1"，
       而 b.ToString() -> "true"）。C# 的口径是 true/false，两边都得是它。 */
    if (ast && val) {
        LLVMValueRef bc = emit_bool_cond(g, val, ast, locals);
        if (bc) return emit_bool_to_cstr(g, bc);
    }
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
    zan_type_t *st = ast ? infer_expr_type(g, ast, locals) : NULL;
    return emit_to_cstr_u(g, val, st && st->kind == TYPE_ULONG);
}

static LLVMValueRef emit_cstr_len_of(zan_irgen_t *g, LLVMValueRef s,
                                     zan_ast_node_t *ast) {
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    if (ast && ast->kind == AST_STRING_LITERAL)
        return LLVMConstInt(i64, (uint64_t)ast->str_val.len, 0);
    return emit_string_length(g, s, ast ? ast->loc : (zan_loc_t){0});
}

static LLVMValueRef emit_str_concat(zan_irgen_t *g, LLVMValueRef a, LLVMValueRef b) {
    LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    a = emit_str_nonnull(g, a);
    b = emit_str_nonnull(g, b);
    LLVMValueRef la = emit_string_length(g, a, (zan_loc_t){0});
    LLVMValueRef lb = emit_string_length(g, b, (zan_loc_t){0});
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
    emit_string_len_set(g, buf, end_off);
    return buf;
}

/* 检查二元 + 表达式任一操作数是否为字符串（判定字符串拼接） */
static bool is_str_concat_node(zan_irgen_t *g, zan_ast_node_t *e, local_scope_t *locals) {
    return e && e->kind == AST_BINARY && e->binary.op == TK_PLUS &&
        (is_string_expr(g, e->binary.left, locals) ||
         is_string_expr(g, e->binary.right, locals));
}

static void collect_concat_ops(zan_irgen_t *g, zan_ast_node_t *e, local_scope_t *locals,
                               zan_ast_node_t **ops, int *n, int max) {
    if (*n < max - 1 && is_str_concat_node(g, e, locals)) {
        collect_concat_ops(g, e->binary.left, locals, ops, n, max - 1);
        collect_concat_ops(g, e->binary.right, locals, ops, n, max);
    } else {
        ops[(*n)++] = e;
    }
}

static LLVMValueRef emit_str_concat_n(zan_irgen_t *g, zan_ast_node_t *expr,
                                      local_scope_t *locals) {
    enum { MAXOPS = 48 };
    zan_ast_node_t *ops[MAXOPS];
    int n = 0;
    collect_concat_ops(g, expr, locals, ops, &n, MAXOPS);

    LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i8t = LLVMInt8TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(i8t, 0);
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
        if (owned[i]) {
            /* s 永远是托管字符串的数据指针（整数/字符操作数已由
             * emit_to_cstr_of 转成 itoa 缓冲），所以 EH 槽位种类必须是
             * ZAN_EH_SLOT_STR。若按源操作数的类型取种类，整数操作数会
             * 落到 ZAN_EH_SLOT_OBJ，异常展开时 zan_rt_release_dyn 会把
             * 字符串头字当成 site 下标去查析构表——读坏内存。 */
            LLVMValueRef slot = emit_entry_alloca(g, LLVMTypeOf(s), "ct.eh");
            LLVMBuildStore(g->builder, s, slot);
            emit_eh_tmp_push_slot(g, slot, ZAN_EH_SLOT_STR);
        }
        lens[i] = emit_cstr_len_of(g, s, ops[i]);
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
    emit_string_len_set(g, buf, off);
    for (int i = 0; i < n; i++) {
        if (owned[i]) {
            emit_eh_tmp_pop(g);
            emit_string_release(g, vals[i]);
        }
    }
    return buf;
}

/* 返回the internal `__zan_co_reap` step function, creating it once per module */
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
    LLVMValueRef untrack = get_co_untrack_fn(g);
    zan_call2(g->builder, LLVMGlobalGetValueType(untrack), untrack, &arg, 1, "");
    emit_co_cancel_delay(g, arg);
    zan_emit_frame_free(g, arg);
    LLVMBuildRetVoid(g->builder);
    if (saved) LLVMPositionBuilderAtEnd(g->builder, saved);
    return reap;
}

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
    LLVMTypeRef ptr_int_ty = g->target_is_wasm ? i32 : LLVMInt64TypeInContext(g->ctx);
    LLVMValueRef cur_int = LLVMBuildPtrToInt(g->builder, cur, ptr_int_ty, "cur.int");
    LLVMValueRef valid = zan_icmp(g->builder, LLVMIntUGT, cur_int,
        LLVMConstInt(ptr_int_ty, 1, 0), "cur.valid");
    LLVMBuildCondBr(g->builder, valid, body, done);

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
    /* a detached (Task */
    LLVMValueRef aw_int = LLVMBuildPtrToInt(g->builder, aw, ptr_int_ty, "aw.int");
    LLVMValueRef aw_valid = zan_icmp(g->builder, LLVMIntUGT, aw_int,
        LLVMConstInt(ptr_int_ty, 1, 0), "aw.valid");
    LLVMValueRef not_det = zan_icmp(g->builder, LLVMIntNE, aw, cur, "aw.ndet");
    LLVMValueRef has_nxt = zan_and(g->builder, aw_valid, not_det, "has.nxt");
    LLVMValueRef nxt = LLVMBuildSelect(g->builder, has_nxt, aw,
        LLVMConstNull(i8ptr), "aw.next");
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
