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

/* —— 推导出的泛型方法实例化 ——
 * 早先实例收集只认**显式**类型实参的泛型方法调用（Select<int,string>(...)）。
 * `nums.AsStream()` 这类推导调用拿不到返回类型 Stream<int>，类实例不登记，
 * Stream<int> 的全部方法在运行期走擦除版（每个 T 值都按 8 字节槽位转换），
 * LINQ 的 Stream 管线整条都是擦除代码。这里给收集过程配一个轻量词法作用域
 * （形参/局部的类型，只用于 infer_expr_type），借发射端同一套
 * infer_method_tp_bindings 推出方法类型实参：
 *   1. 返回类型按绑定代换后登记（Stream<int>）；
 *   2. 该泛型方法体按绑定再扫一遍（体内 new Dict<T,bool> 等嵌套实例），
 *      (方法, 绑定) 去重，随不动点循环推进。 */
zan_type_t *subst_method_tp(zan_irgen_t *g, zan_type_t *t,
                            zan_ast_list_t *tps, zan_type_t **bind);
static local_scope_t *disc_scope;
static zan_ast_list_t *disc_tps;
static zan_type_t **disc_bind;
typedef struct {
    zan_symbol_t *m;
    zan_type_t *bind[8];
} disc_mspec_t;
static disc_mspec_t *disc_specs;
static int disc_spec_n, disc_spec_cap, disc_spec_done;
#define DISC_SPEC_LIMIT 4096

static zan_type_t *disc_subst(zan_irgen_t *g, zan_type_t *t) {
    if (!t) return t;
    if (disc_tps && disc_bind && type_mentions_tp(t))
        t = subst_method_tp(g, t, disc_tps, disc_bind);
    if (t && g->collect_inst_ctx && !type_is_concrete(t))
        t = subst_type_param_deep(g, t, g->collect_inst_ctx);
    return t;
}

static int disc_type_depth(zan_type_t *t, int d) {
    if (!t || d > 16) return d;
    int m = d;
    if (t->element_type) {
        int e = disc_type_depth(t->element_type, d + 1);
        if (e > m) m = e;
    }
    for (int i = 0; i < t->type_arg_count; i++) {
        int e = disc_type_depth(t->type_args[i], d + 1);
        if (e > m) m = e;
    }
    return m;
}

static void disc_queue_spec(zan_symbol_t *m, zan_type_t **bind, int n) {
    if (!m || !m->decl || m->decl->kind != AST_METHOD_DECL) return;
    if (!m->decl->method_decl.body) return;
    /* 只排非泛型类里的泛型方法（Enumerable 这类静态工具类）：泛型类里的
     * 泛型方法还需叠加类实例上下文，交给原有的类实例扫描。 */
    if (m->parent && is_user_generic_sym(m->parent)) return;
    for (int i = 0; i < n; i++)
        if (!bind[i] || disc_type_depth(bind[i], 0) > 6) return;  /* 防递归膨胀 */
    for (int k = 0; k < disc_spec_n; k++) {
        if (disc_specs[k].m != m) continue;
        bool same = true;
        for (int i = 0; i < n && same; i++)
            same = types_equal(disc_specs[k].bind[i], bind[i]);
        if (same) return;
    }
    if (disc_spec_n >= DISC_SPEC_LIMIT) return;
    if (disc_spec_n == disc_spec_cap) {
        int nc = disc_spec_cap ? disc_spec_cap * 2 : 64;
        disc_mspec_t *grown = (disc_mspec_t *)realloc(disc_specs,
            (size_t)nc * sizeof(*grown));
        if (!grown) return;
        disc_specs = grown;
        disc_spec_cap = nc;
    }
    disc_mspec_t *sp = &disc_specs[disc_spec_n++];
    memset(sp, 0, sizeof(*sp));
    sp->m = m;
    for (int i = 0; i < n; i++) sp->bind[i] = bind[i];
}

static void collect_inst_type_depth(zan_irgen_t *g, zan_type_t *t, int depth) {
    if (!t || depth > 16) return;
    if (depth == 0 || !type_is_concrete(t)) t = disc_subst(g, t);
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


/* 泛型方法调用（显式或推导类型实参）：推绑定 → 登记代换后的返回类型 →
 * 排队按绑定重扫方法体。候选与发射端口径一致：扩展方法（接收者占第 0 形参，
 * 且接收者类型须与第 0 形参同一泛型定义）、类型名接收者的静态方法、实例
 * 接收者的方法、当前类里的裸名调用。 */
static void disc_try_method(zan_irgen_t *g, zan_ast_node_t *call,
                            zan_symbol_t *m, zan_ast_node_t *recv_for_infer) {
    if (!m || !m->decl || m->decl->kind != AST_METHOD_DECL) return;
    zan_ast_list_t *tps = &m->decl->method_decl.type_params;
    int n = tps->count;
    if (n == 0 || n > 8) return;
    if (call->call.type_args.count != 0 && call->call.type_args.count != n) return;
    zan_type_t *bind[8] = { NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL };
    zan_type_t *saved_cur = g->cur_inst;
    g->cur_inst = g->collect_inst_ctx;
    infer_method_tp_bindings(g, m, call, recv_for_infer, disc_scope, bind);
    g->cur_inst = saved_cur;
    for (int i = 0; i < n; i++) {
        bind[i] = disc_subst(g, bind[i]);
        if (!bind[i] || !type_is_concrete(bind[i])) return;
    }
    zan_type_t *ret = m->type ? subst_method_tp(g, m->type, tps, bind) : NULL;
    if (ret && type_is_concrete(ret)) collect_inst_type(g, ret);
    disc_queue_spec(m, bind, n);
}

static void disc_collect_call(zan_irgen_t *g, zan_ast_node_t *e) {
    if (!disc_scope || !e->call.callee || !g->binder) return;
    zan_istr_t mname = {0};
    zan_ast_node_t *recv_obj = NULL;
    if (e->call.callee->kind == AST_IDENTIFIER) {
        mname = e->call.callee->ident.name;
    } else if (e->call.callee->kind == AST_MEMBER_ACCESS) {
        mname = e->call.callee->member.name;
        recv_obj = e->call.callee->member.object;
    }
    if (!mname.str || mname.len == 0) return;
    if (!recv_obj) {
        if (g->current_type_sym && !local_find(disc_scope, mname)) {
            zan_symbol_t *m = get_method_sym(g->current_type_sym, mname);
            disc_try_method(g, e, m, NULL);
        }
        return;
    }
    zan_symbol_t *ts = NULL;
    if (recv_obj->kind == AST_IDENTIFIER && !local_find(disc_scope, recv_obj->ident.name)) {
        ts = zan_binder_lookup(g->binder, recv_obj->ident.name);
        if (ts && ts->kind != SYM_CLASS && ts->kind != SYM_STRUCT) ts = NULL;
    }
    if (ts) {
        disc_try_method(g, e, get_method_sym(ts, mname), NULL);
        return;
    }
    zan_type_t *rt = disc_subst(g, infer_expr_type(g, recv_obj, disc_scope));
    if (!rt || !type_is_concrete(rt)) return;
    if (rt->sym) {
        zan_symbol_t *im = get_method_sym(rt->sym, mname);
        if (im && im->decl && im->decl->kind == AST_METHOD_DECL &&
            im->decl->method_decl.type_params.count > 0) {
            disc_try_method(g, e, im, recv_obj);
            return;
        }
    }
    if (!g->binder->ext_methods) return;
    for (int i = 0; i < g->binder->ext_method_count; i++) {
        zan_symbol_t *m = g->binder->ext_methods[i].method;
        if (!m || !m->decl || m->decl->kind != AST_METHOD_DECL) continue;
        if (m->name.len != mname.len ||
            memcmp(m->name.str, mname.str, (size_t)mname.len) != 0) continue;
        if (m->decl->method_decl.type_params.count == 0) continue;
        zan_type_t *p0 = method_param_type(g, m, 0);
        if (!p0) continue;
        /* 接收者须与 this 形参同一定义（List<T> 对 List<int>），否则同名
         * 重载（Dict 版 Where 等）会被错配出虚假实例。 */
        if (p0->kind != TYPE_TYPE_PARAM) {
            if (p0->kind != rt->kind) continue;
            if (p0->name.len != rt->name.len ||
                (p0->name.len && memcmp(p0->name.str, rt->name.str,
                                        (size_t)p0->name.len) != 0)) continue;
        }
        disc_try_method(g, e, m, recv_obj);
    }
}
static void disc_collect_call(zan_irgen_t *g, zan_ast_node_t *e);
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
        disc_collect_call(g, e);
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
        if (disc_scope) {
            zan_type_t *vt = st->var_decl.type
                ? zan_binder_resolve_type(g->binder, st->var_decl.type)
                : (st->var_decl.initializer
                    ? infer_expr_type(g, st->var_decl.initializer, disc_scope)
                    : NULL);
            vt = disc_subst(g, vt);
            if (vt) local_add(disc_scope, st->var_decl.name, NULL, vt);
        }
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
        if (disc_scope) {
            zan_type_t *et = st->foreach_stmt.var_type
                ? zan_binder_resolve_type(g->binder, st->foreach_stmt.var_type)
                : container_elem_type(disc_subst(g,
                    infer_expr_type(g, st->foreach_stmt.collection, disc_scope)));
            et = disc_subst(g, et);
            if (et) local_add(disc_scope, st->foreach_stmt.var_name, NULL, et);
        }
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

static void collect_inst_member_body(zan_irgen_t *g, zan_ast_node_t *member);
static void collect_inst_member(zan_irgen_t *g, zan_ast_node_t *member) {
    if (!member) return;
    local_scope_t *saved = disc_scope;
    disc_scope = NULL;
    if (member->kind == AST_METHOD_DECL || member->kind == AST_CONSTRUCTOR_DECL) {
        disc_scope = local_scope_new(g->arena);
        for (int k = 0; k < member->method_decl.params.count; k++) {
            zan_ast_node_t *p = member->method_decl.params.items[k];
            zan_type_t *pt = p->param.type
                ? disc_subst(g, zan_binder_resolve_type(g->binder, p->param.type))
                : NULL;
            if (pt) local_add(disc_scope, p->param.name, NULL, pt);
        }
    }
    collect_inst_member_body(g, member);
    disc_scope = saved;
}
static void collect_inst_member_body(zan_irgen_t *g, zan_ast_node_t *member) {
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
    int prev_specs = -1;
    int guard = 0;
    const int limit = 64;
    zan_symbol_t *saved_type_sym = g->current_type_sym;
    disc_spec_n = disc_spec_done = 0;
    while (g->generic_inst_count != prev || disc_spec_n != prev_specs) {
        if (guard >= limit) {
            zan_diag_emit_code(g->diag, DIAG_ERROR, unit->loc, 3100,
                "generic instantiation did not reach a fixed point within "
                "%d iterations; the type graph may be recursively dependent "
                "or too large", limit);
            break;
        }
        prev = g->generic_inst_count;
        prev_specs = disc_spec_n;
        for (int i = 0; i < unit->comp_unit.decls.count; i++) {
            zan_ast_node_t *decl = unit->comp_unit.decls.items[i];
            if (decl->kind != AST_CLASS_DECL && decl->kind != AST_STRUCT_DECL &&
                decl->kind != AST_INTERFACE_DECL)
                continue;
            g->collect_inst_ctx = NULL;
            g->current_type_sym = zan_binder_lookup(g->binder, decl->type_decl.name);
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
        /* 推导出的泛型方法实例：按绑定重扫方法体（可能继续排入新的实例） */
        for (; disc_spec_done < disc_spec_n; disc_spec_done++) {
            disc_mspec_t sp = disc_specs[disc_spec_done];
            disc_tps = &sp.m->decl->method_decl.type_params;
            disc_bind = sp.bind;
            g->collect_inst_ctx = NULL;
            g->current_type_sym = sp.m->parent;
            collect_inst_member(g, sp.m->decl);
            disc_tps = NULL;
            disc_bind = NULL;
        }
        guard++;
    }
    g->current_type_sym = saved_type_sym;
    g->collect_inst_ctx = NULL;
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
                note_virtual_slot_demand(g, static_sym, slot);
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
