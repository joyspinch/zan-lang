#include "irgen_internal.h"
/* irgen_generics */
void get_eh_globals(zan_irgen_t *g, LLVMValueRef *top, LLVMValueRef *bufs, LLVMValueRef *exc);
LLVMValueRef get_eh_exc_owned_global(zan_irgen_t *g);
LLVMValueRef get_eh_exc_tid_global(zan_irgen_t *g);
LLVMValueRef get_class_tid_global(zan_irgen_t *g, zan_symbol_t *sym);
LLVMValueRef emit_eh_buf_ptr(zan_irgen_t *g, LLVMValueRef idx);
void emit_eh_unwind_to_handler(zan_irgen_t *g, LLVMValueRef top);
void emit_eh_longjmp(zan_irgen_t *g, LLVMValueRef bufp);
void emit_finallys_left_by_throw(zan_irgen_t *g, local_scope_t *locals);
LLVMValueRef emit_string_literal_rc(zan_irgen_t *g, zan_istr_t text);
void emit_eh_tmp_push_slot(zan_irgen_t *g, LLVMValueRef slot, int kind);
bool sym_declares_extern(zan_symbol_t *sym);
static LLVMValueRef emit_soft_scratch_cached(zan_irgen_t *g);
static zan_symbol_t *extern_check_callee_sym(zan_irgen_t *g,
                                             zan_ast_node_t *call);
zan_type_t *method_ret_type_at(zan_irgen_t *g, zan_symbol_t *msym,
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
void collect_inst_expr(zan_irgen_t *g, zan_ast_node_t *e) {
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
    case AST_CALL: {
        for (int i = 0; i < e->call.type_args.count; i++)
            collect_inst_typeref(g, e->call.type_args.items[i]);
        collect_inst_expr(g, e->call.callee);
        for (int i = 0; i < e->call.args.count; i++)
            collect_inst_expr(g, e->call.args.items[i]);

        if (e->call.type_args.count > 0 && e->call.callee) {
            bool all_concrete = true;
            for (int i = 0; i < e->call.type_args.count; i++) {
                zan_type_t *ta = resolve_type_ctx(g, e->call.type_args.items[i]);
                if (!ta || !type_is_concrete(ta)) {
                    all_concrete = false;
                    break;
                }
            }
            if (all_concrete) {
                zan_istr_t mname = {0};
                zan_ast_node_t *recv_obj = NULL;
                if (e->call.callee->kind == AST_IDENTIFIER) {
                    mname = e->call.callee->ident.name;
                } else if (e->call.callee->kind == AST_MEMBER_ACCESS) {
                    mname = e->call.callee->member.name;
                    recv_obj = e->call.callee->member.object;
                }
                if (mname.str && mname.len > 0) {
                    if (g->binder && g->binder->ext_methods) {
                        for (int i = 0; i < g->binder->ext_method_count; i++) {
                            zan_symbol_t *m = g->binder->ext_methods[i].method;
                            if (!m || !m->decl || m->decl->kind != AST_METHOD_DECL) continue;
                            if (m->name.len == mname.len &&
                                memcmp(m->name.str, mname.str, (size_t)mname.len) == 0 &&
                                m->decl->method_decl.type_params.count == e->call.type_args.count) {
                                zan_type_t *ret = method_ret_type_at(g, m, e, recv_obj, NULL);
                                if (ret) collect_inst_type(g, ret);
                            }
                        }
                    }
                    if (recv_obj && recv_obj->kind == AST_IDENTIFIER && g->binder) {
                        zan_symbol_t *ts = zan_binder_lookup(g->binder, recv_obj->ident.name);
                        if (ts && (ts->kind == SYM_CLASS || ts->kind == SYM_STRUCT)) {
                            zan_symbol_t *m = get_method_sym(ts, mname);
                            if (m && m->decl && m->decl->kind == AST_METHOD_DECL &&
                                m->decl->method_decl.type_params.count == e->call.type_args.count) {
                                zan_type_t *ret = method_ret_type_at(g, m, e, recv_obj, NULL);
                                if (ret) collect_inst_type(g, ret);
                            }
                        }
                    }
                }
            }
        }
        break;
    }
    case AST_MEMBER_ACCESS:
        collect_inst_expr(g, e->member.object);
        break;
    case AST_TYPEOF_EXPR:
        collect_inst_typeref(g, e->cast.type);
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
        collect_inst_expr(g, member->field_decl.initializer);
    }
}
void discover_generic_insts(zan_irgen_t *g, zan_ast_node_t *unit) {
    if (!unit || unit->kind != AST_COMPILATION_UNIT) return;
    int prev = -1;
    int guard = 0;
    const int limit = 64;
    while (g->generic_inst_count != prev) {
        if (guard >= limit) {
            zan_diag_emit_code(g->diag, DIAG_ERROR, unit->loc, 3100,
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
    for (int i = 0; i < g->generic_inst_count; i++) {
        zan_symbol_t *ts = g->generic_insts[i].type_sym;
        zan_type_t *in = g->generic_insts[i].inst;
        zan_compile_trace("  inst %d: %.*s<%d args>", i,
                          ts ? (int)ts->name.len : 2, ts ? ts->name.str : "?",
                          in ? in->type_arg_count : -1);
    }
}
zan_type_t *ident_inst_type(zan_irgen_t *g, zan_ast_node_t *e) {
    if (!e || e->kind != AST_IDENTIFIER || !e->ident.inst_type_ref) return NULL;
    return zan_binder_resolve_type(g->binder, e->ident.inst_type_ref);
}
LLVMValueRef coerce_generic_result(zan_irgen_t *g, LLVMValueRef result,
                                          zan_symbol_t *method_sym,
                                          zan_type_t *recv) {
    if (!result || !method_sym || !recv) return result;
    zan_type_t *rt = subst_type_param(method_sym->type, recv);
    if (rt && rt != method_sym->type)
        return emit_boundary_coerce(g, result, map_type(g, rt));
    return result;
}
LLVMValueRef route_generic_method(zan_irgen_t *g, zan_type_t *recv_ty,
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
LLVMValueRef emit_dispatch_call(zan_irgen_t *g, zan_symbol_t *static_sym,
        zan_symbol_t *method_sym, LLVMValueRef static_fn, LLVMTypeRef fn_type,
        LLVMValueRef *call_args, int argc, const char *cn) {
    coerce_args_to_params(g, fn_type, call_args, argc);
    if (static_sym && method_sym &&
        (method_sym->modifiers & (MOD_VIRTUAL | MOD_OVERRIDE | MOD_ABSTRACT)) &&
        class_has_virtual_methods(static_sym) && argc >= 1 && call_args[0] &&
        LLVMGetTypeKind(LLVMTypeOf(call_args[0])) == LLVMPointerTypeKind) {

        bool devirt = ((static_sym->modifiers & MOD_SEALED) != 0) ||
                      ((method_sym->modifiers & MOD_SEALED) != 0);
        /* B-ID95：导出面（emit_lib/emit_shared）是开放世界——消费者可对导出类
         * 源码子类化并经导出 API 传入派生对象，「本模块无派生/无重写」不再成立，
         * 封闭世界 devirt 会把虚调用钉死到基类实现。仅 sealed 可安全直调。 */
        if (!g->emit_lib) {
            devirt = devirt || !class_has_derived_in_module(g, static_sym) ||
                     !hierarchy_has_subclass_override(g, static_sym, method_sym);
        }

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
                return zan_call_or_invoke(g, b, fn_type, fnp, call_args, (unsigned)argc, cn);
            }
        }
    }
    return zan_call_or_invoke(g, g->builder, fn_type, static_fn, call_args, (unsigned)argc, cn);
}
