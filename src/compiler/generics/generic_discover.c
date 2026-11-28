#include "irgen_internal.h"
/* irgen_generics */
void get_eh_globals(zan_irgen_t *g, LLVMValueRef *top, LLVMValueRef *bufs,
                    LLVMValueRef *exc);
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
                               zan_ast_node_t *call, zan_ast_node_t *recv_expr,
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
    zan_type_t *owner_inst;
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

static void disc_queue_spec(zan_symbol_t *m, zan_type_t *owner_inst,
                            zan_type_t **bind, int n) {
    if (!m || !m->decl || m->decl->kind != AST_METHOD_DECL) return;
    if (!m->decl->method_decl.body) return;
    if (m->parent && is_user_generic_sym(m->parent) &&
        (!owner_inst || !type_is_concrete(owner_inst)))
        return;
    for (int i = 0; i < n; i++)
        if (!bind[i] || disc_type_depth(bind[i], 0) > 6) return;  /* 防递归膨胀 */
    if (owner_inst && disc_type_depth(owner_inst, 0) > 6) return;
    for (int k = 0; k < disc_spec_n; k++) {
        if (disc_specs[k].m != m) continue;
        if (disc_specs[k].owner_inst != owner_inst &&
            (!disc_specs[k].owner_inst || !owner_inst ||
             !types_equal(disc_specs[k].owner_inst, owner_inst)))
            continue;
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
    sp->owner_inst = owner_inst;
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
  if (!tref || tref->kind != AST_TYPE_REF)
    return;
  collect_inst_type(g, zan_binder_resolve_type(g->binder, tref));
}


/* 泛型方法调用（显式或推导类型实参）：推绑定 → 登记代换后的返回类型 →
 * 排队按绑定重扫方法体。候选与发射端口径一致：扩展方法（接收者占第 0 形参，
 * 且接收者类型须与第 0 形参同一泛型定义）、类型名接收者的静态方法、实例
 * 接收者的方法、当前类里的裸名调用。 */
static void disc_try_method(zan_irgen_t *g, zan_ast_node_t *call,
                            zan_symbol_t *m, zan_ast_node_t *recv_for_infer,
                            zan_type_t *owner_inst) {
    if (!m || !m->decl || m->decl->kind != AST_METHOD_DECL) return;
    zan_ast_list_t *tps = &m->decl->method_decl.type_params;
    int n = tps->count;
    if (n == 0 || n > 8) return;
    if (call->call.type_args.count != 0 && call->call.type_args.count != n) return;
    zan_type_t *bind[8] = { NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL };
    zan_type_t *saved_cur = g->cur_inst;
    g->cur_inst = owner_inst ? owner_inst : g->collect_inst_ctx;
    infer_method_tp_bindings(g, m, call, recv_for_infer, disc_scope, bind);
    g->cur_inst = saved_cur;
    for (int i = 0; i < n; i++) {
        bind[i] = disc_subst(g, bind[i]);
        if (!bind[i] || !type_is_concrete(bind[i])) return;
    }
    zan_type_t *ret = m->type ? subst_method_tp(g, m->type, tps, bind) : NULL;
    if (ret && owner_inst && !type_is_concrete(ret))
        ret = subst_type_param_deep(g, ret, owner_inst);
    if (ret && type_is_concrete(ret)) collect_inst_type(g, ret);
    disc_queue_spec(m, owner_inst, bind, n);
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
            disc_try_method(g, e, m, NULL, g->collect_inst_ctx);
        }
        return;
    }
    zan_symbol_t *ts = NULL;
    zan_type_t *tp_sinst = NULL;
    if (recv_obj->kind == AST_IDENTIFIER && !local_find(disc_scope, recv_obj->ident.name)) {
        zan_istr_t rn = recv_obj->ident.name;
        zan_type_t *tp_ct = NULL;
        if (disc_tps && disc_bind) {
            for (int ti = 0; ti < disc_tps->count; ti++) {
                zan_ast_node_t *tp = disc_tps->items[ti];
                if (tp && tp->kind == AST_IDENTIFIER &&
                    tp->ident.name.len == rn.len &&
                    memcmp(tp->ident.name.str, rn.str, (size_t)rn.len) == 0) {
                    tp_ct = disc_bind[ti];
                    break;
                }
            }
        }
        if (!tp_ct && g->collect_inst_ctx && g->collect_inst_ctx->sym &&
            g->collect_inst_ctx->sym->decl) {
            zan_ast_list_t *ctps = &g->collect_inst_ctx->sym->decl->type_decl.type_params;
            for (int ti = 0; ti < ctps->count && ti < g->collect_inst_ctx->type_arg_count; ti++) {
                zan_ast_node_t *tp = ctps->items[ti];
                if (tp && tp->kind == AST_IDENTIFIER &&
                    tp->ident.name.len == rn.len &&
                    memcmp(tp->ident.name.str, rn.str, (size_t)rn.len) == 0) {
                    tp_ct = g->collect_inst_ctx->type_args[ti];
                    break;
                }
            }
        }
        if (tp_ct && tp_ct->sym &&
            (tp_ct->kind == TYPE_CLASS || tp_ct->kind == TYPE_STRUCT)) {
            ts = tp_ct->sym;
            if (tp_ct->type_arg_count > 0) tp_sinst = tp_ct;
        } else {
            ts = zan_binder_lookup(g->binder, rn);
            if (ts && ts->kind != SYM_CLASS && ts->kind != SYM_STRUCT) ts = NULL;
        }
    }
    if (ts) {
        zan_type_t *sinst = tp_sinst ? tp_sinst
            : (recv_obj->ident.inst_type_ref
                ? disc_subst(g, zan_binder_resolve_type(g->binder, recv_obj->ident.inst_type_ref))
                : NULL);
        disc_try_method(g, e, get_method_sym(ts, mname), NULL, sinst);
        return;
    }
    zan_type_t *rt = disc_subst(g, infer_expr_type(g, recv_obj, disc_scope));
    if (!rt || !type_is_concrete(rt)) return;
    if (rt->sym) {
        zan_symbol_t *im = get_method_sym(rt->sym, mname);
        if (im && im->decl && im->decl->kind == AST_METHOD_DECL &&
            im->decl->method_decl.type_params.count > 0) {
            disc_try_method(g, e, im, recv_obj, rt->type_arg_count > 0 ? rt : NULL);
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
        zan_type_t *p0 = g->binder->ext_methods[i].recv_type;
        if (!p0) p0 = method_param_type(g, m, 0);
        if (!p0) continue;
        /* 接收者须与 this 形参同一定义（List<T> 对 List<int>），或实现
         * this 接口形参（SqliteConnection 对 IDbExecutor，与 expr_overload.c
         * 的 ext_receiver_interface_match 口径一致），否则同名重载（Dict 版
         * Where 等）会被错配出虚假实例。 */
        if (p0->kind != TYPE_TYPE_PARAM) {
            bool iface_match =
                (p0->kind == TYPE_INTERFACE && p0->type_arg_count == 0 &&
                 (rt->kind == TYPE_CLASS || rt->kind == TYPE_STRUCT ||
                  rt->kind == TYPE_INTERFACE) &&
                 rt->sym && p0->sym &&
                 zan_binder_class_implements_iface(rt->sym, p0->sym));
            if (!iface_match) {
                if (p0->kind != rt->kind) continue;
                if (p0->name.len != rt->name.len ||
                    (p0->name.len && memcmp(p0->name.str, rt->name.str,
                                            (size_t)p0->name.len) != 0)) continue;
            }
        }
        disc_try_method(g, e, m, recv_obj, NULL);
    }
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
        disc_collect_call(g, e);
        if (e->call.type_args.count == 0 && e->call.callee &&
            e->call.callee->kind == AST_MEMBER_ACCESS) {
            zan_ast_node_t *recv_obj = e->call.callee->member.object;
            if (recv_obj && recv_obj->kind == AST_IDENTIFIER &&
                !recv_obj->ident.inst_type_ref && g->binder &&
                (!disc_scope || !local_find(disc_scope, recv_obj->ident.name))) {
                zan_symbol_t *ts =
                    recv_obj->ident.is_type_receiver
                        ? zan_binder_type_receiver(g->binder, recv_obj)
                        : zan_binder_lookup(g->binder, recv_obj->ident.name);
                if (ts && is_user_generic_sym(ts)) {
                    local_scope_t empty_locals = {0};
                    local_scope_t *eff_locals = disc_scope ? disc_scope : &empty_locals;
                    zan_symbol_t *m = resolve_overload_typed(
                        g, ts, e->call.callee->member.name, e, eff_locals);
                    if (!m)
                        m = get_method_sym(ts, e->call.callee->member.name);
                    zan_type_t *inferred =
                        infer_generic_static_recv_inst(g, ts, m, e, eff_locals);
                    if (inferred)
                        collect_inst_type(g, inferred);
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
    case AST_LAMBDA:
        for (int i = 0; i < e->lambda.params.count; i++)
            collect_inst_typeref(g, e->lambda.params.items[i]->param.type);
        collect_inst_stmt(g, e->lambda.body);
        collect_inst_expr(g, e->lambda.body);
        break;
    case AST_TUPLE_EXPR:
        for (int i = 0; i < e->tuple_expr.items.count; i++)
            collect_inst_expr(g, e->tuple_expr.items.items[i]);
        break;
    case AST_SWITCH_EXPR:
        collect_inst_expr(g, e->switch_expr.expr);
        for (int i = 0; i < e->switch_expr.arms.count; i++) {
            zan_ast_node_t *arm = e->switch_expr.arms.items[i];
            collect_inst_typeref(g, arm->switch_arm.type_pattern);
            collect_inst_expr(g, arm->switch_arm.pattern);
            collect_inst_expr(g, arm->switch_arm.when_cond);
            collect_inst_expr(g, arm->switch_arm.result);
        }
        break;
    case AST_WITH_EXPR:
        collect_inst_expr(g, e->with_expr.expr);
        for (int i = 0; i < e->with_expr.assigns.count; i++)
            collect_inst_expr(g, e->with_expr.assigns.items[i]);
        break;
    default:
        break;
    }
}

static void collect_inst_stmt(zan_irgen_t *g, zan_ast_node_t *st) {
    if (!st) return;
    switch (st->kind) {
    case AST_BLOCK: {
        int mark = disc_scope ? disc_scope->count : 0;
        for (int i = 0; i < st->block.stmts.count; i++)
            collect_inst_stmt(g, st->block.stmts.items[i]);
        if (disc_scope)
            disc_scope->count = mark;
        break;
    }
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
    case AST_TUPLE_DECON:
        for (int i = 0; i < st->tuple_decon.types.count; i++)
            collect_inst_typeref(g, st->tuple_decon.types.items[i]);
        collect_inst_expr(g, st->tuple_decon.initializer);
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
    case AST_FOR_STMT: {
        int mark = disc_scope ? disc_scope->count : 0;
        collect_inst_stmt(g, st->for_stmt.init);
        collect_inst_expr(g, st->for_stmt.cond);
        collect_inst_expr(g, st->for_stmt.step);
        collect_inst_stmt(g, st->for_stmt.body);
        if (disc_scope)
            disc_scope->count = mark;
        break;
    }
    case AST_FOREACH_STMT: {
        int mark = disc_scope ? disc_scope->count : 0;
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
        if (disc_scope)
            disc_scope->count = mark;
        break;
    }
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
    } else if (member->kind == AST_FIELD_DECL ||
               member->kind == AST_PROPERTY_DECL) {
        collect_inst_typeref(g, member->field_decl.type);
        collect_inst_expr(g, member->field_decl.initializer);
        collect_inst_stmt(g, member->field_decl.getter_body);
        collect_inst_expr(g, member->field_decl.getter_body);
        collect_inst_stmt(g, member->field_decl.setter_body);
        collect_inst_expr(g, member->field_decl.setter_body);
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
            g->collect_inst_ctx = sp.owner_inst;
            g->current_type_sym = sp.m->parent;
            collect_inst_member(g, sp.m->decl);
            g->collect_inst_ctx = NULL;
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
  if (!e || e->kind != AST_IDENTIFIER || !e->ident.inst_type_ref)
    return NULL;
  return zan_binder_resolve_type(g->binder, e->ident.inst_type_ref);
}
LLVMValueRef coerce_generic_result(zan_irgen_t *g, LLVMValueRef result,
                                   zan_symbol_t *method_sym, zan_type_t *recv) {
  if (!result || !method_sym || !recv)
    return result;
  zan_type_t *rt = subst_type_param(method_sym->type, recv);
  if (rt && rt != method_sym->type)
    return emit_boundary_coerce(g, result, map_type(g, rt));
  return result;
}
static zan_type_t *resolve_generic_recv_inst(zan_irgen_t *g, zan_type_t *recv_ty,
                                             zan_symbol_t *method_sym,
                                             zan_type_t ***out_args,
                                             int *out_argc) {
  if (!recv_ty || !recv_ty->sym)
    return NULL;
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
  if (!is_user_generic_sym(recv_ty->sym))
    return NULL;
  zan_type_t **args = recv_ty->type_args;
  int argc = recv_ty->type_arg_count;
  zan_type_t *resolved = recv_ty;
  if (g->cur_inst && g->cur_inst->sym == recv_ty->sym) {
    bool concrete = argc > 0;
    for (int i = 0; i < argc && concrete; i++)
      concrete = type_is_concrete(args[i]);
    if (!concrete) {
      args = g->cur_inst->type_args;
      argc = g->cur_inst->type_arg_count;
      resolved = g->cur_inst;
    }
  } else if (g->cur_inst && !type_is_concrete(recv_ty)) {
    zan_type_t *sub = subst_type_param_deep(g, recv_ty, g->cur_inst);
    if (sub && sub->type_arg_count > 0 && type_is_concrete(sub)) {
      args = sub->type_args;
      argc = sub->type_arg_count;
      resolved = sub;
    }
  }
  if (argc <= 0)
    return NULL;
  if (out_args)
    *out_args = args;
  if (out_argc)
    *out_argc = argc;
  return resolved;
}

LLVMValueRef route_generic_method_gctx(zan_irgen_t *g, zan_type_t *recv_ty,
                                       zan_symbol_t *method_sym) {
  zan_type_t **args = NULL;
  int argc = 0;
  zan_type_t *resolved =
      resolve_generic_recv_inst(g, recv_ty, method_sym, &args, &argc);
  if (!resolved || !args || argc <= 0)
    return NULL;
  return resolve_gctx_arg_for_inst(g, false, resolved->sym, resolved, NULL, 0);
}

LLVMValueRef route_generic_method(zan_irgen_t *g, zan_type_t *recv_ty,
                                  zan_symbol_t *method_sym,
                                  LLVMValueRef erased_fn, LLVMTypeRef erased_ty,
                                  LLVMTypeRef *out_ty) {
  if (out_ty)
    *out_ty = erased_ty;
  zan_type_t **args = NULL;
  int argc = 0;
  zan_type_t *resolved =
      resolve_generic_recv_inst(g, recv_ty, method_sym, &args, &argc);
  if (!resolved)
    return erased_fn;
  LLVMTypeRef st = NULL;
  LLVMValueRef sfn = find_generic_fn(g, method_sym, args, argc, &st);
  if (sfn) {
    if (out_ty)
      *out_ty = st;
    return sfn;
  }
  zan_compile_trace("route miss: %.*s.%.*s argc=%d arg0type=%d concrete=%d",
                    (int)resolved->sym->name.len, resolved->sym->name.str,
                    (int)method_sym->name.len, method_sym->name.str, argc,
                    args && args[0] ? (int)args[0]->kind : -1,
                    type_is_concrete(resolved));
  return erased_fn;
}

static bool class_has_derived_in_module(zan_irgen_t *g, zan_symbol_t *target) {
  if (!g || !target)
    return false;
  for (int i = 0; i < g->struct_type_count; i++) {
    zan_symbol_t *cur = g->struct_types[i].sym;
    if (!cur || cur == target)
      continue;
    for (zan_type_t *bt = (cur->type ? cur->type->base_type : NULL); bt;
         bt = bt->base_type) {
      if (bt->sym == target)
        return true;
    }
  }
  return false;
}

/* 检查if any derived class of static_sym actually overrides method_sym */
static bool hierarchy_has_subclass_override(zan_irgen_t *g,
                                            zan_symbol_t *static_sym,
                                            zan_symbol_t *method_sym) {
  if (!g || !static_sym || !method_sym)
    return false;
  int want_params = method_declared_param_count(method_sym);
  for (int i = 0; i < g->struct_type_count; i++) {
    zan_symbol_t *cur = g->struct_types[i].sym;
    if (!cur || cur == static_sym)
      continue;
    bool is_sub = false;
    for (zan_type_t *bt = (cur->type ? cur->type->base_type : NULL); bt;
         bt = bt->base_type) {
      if (bt->sym == static_sym) {
        is_sub = true;
        break;
      }
    }
    if (!is_sub)
      continue;
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
LLVMValueRef emit_dispatch_call_gctx(zan_irgen_t *g, zan_symbol_t *static_sym,
                                     zan_symbol_t *method_sym,
                                     LLVMValueRef static_fn, LLVMTypeRef fn_type,
                                     LLVMValueRef *call_args, int argc,
                                     LLVMValueRef gctx_arg, const char *cn) {
  unsigned npar = fn_type ? LLVMCountParamTypes(fn_type) : (unsigned)argc;
  bool has_gctx = (npar == (unsigned)argc + 1 && gctx_arg != NULL);
  LLVMTypeRef vtable_fnty =
      has_gctx ? strip_last_param_fn_type(g, fn_type) : fn_type;
  coerce_args_to_params(g, vtable_fnty, call_args, argc);
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
        LLVMValueRef thisp =
            LLVMBuildBitCast(b, call_args[0], LLVMPointerType(st, 0), "vthis");
        LLVMValueRef vpf = LLVMBuildStructGEP2(b, st, thisp, 0, "vpf");
        unsigned inv_kind =
            LLVMGetMDKindIDInContext(g->ctx, "invariant.load", 14);
        LLVMValueRef inv_md = LLVMMDNodeInContext(g->ctx, NULL, 0);
        LLVMValueRef vt8 = LLVMBuildLoad2(b, i8ptr, vpf, "vt8");
        LLVMSetMetadata(vt8, inv_kind, inv_md);
        LLVMValueRef vt =
            LLVMBuildBitCast(b, vt8, LLVMPointerType(i8ptr, 0), "vt");
        LLVMValueRef sidx = LLVMConstInt(i64, (unsigned long long)slot, 0);
        LLVMValueRef sp = LLVMBuildInBoundsGEP2(b, i8ptr, vt, &sidx, 1, "vsp");
        LLVMValueRef fn8 = LLVMBuildLoad2(b, i8ptr, sp, "vfn8");
        LLVMSetMetadata(fn8, inv_kind, inv_md);
        LLVMValueRef fnp =
            LLVMBuildBitCast(b, fn8, LLVMPointerType(vtable_fnty, 0), "vfnp");
        LLVMValueRef call_res = zan_call_or_invoke(
            g, b, vtable_fnty, fnp, call_args, (unsigned)argc, cn);
        if (g->publish_instance_prune && !g->emit_lib &&
            !is_user_generic_sym(static_sym) &&
            (LLVMIsACallInst(call_res) || LLVMIsAInvokeInst(call_res))) {
          int st_idx = -1;
          for (int si = 0; si < g->struct_type_count; si++) {
            if (g->struct_types[si].sym == static_sym) {
              st_idx = si;
              break;
            }
          }
          if (st_idx >= 0) {
            LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
            LLVMMetadataRef md_elems[] = {
                LLVMValueAsMetadata(
                    LLVMConstInt(i64t, (unsigned long long)st_idx, 0)),
                LLVMValueAsMetadata(
                    LLVMConstInt(i64t, (unsigned long long)slot, 0))};
            unsigned md_kind =
                LLVMGetMDKindIDInContext(g->ctx, "zan.vcall", 9);
            LLVMSetMetadata(
                call_res, md_kind,
                LLVMMetadataAsValue(
                    g->ctx, LLVMMDNodeInContext2(g->ctx, md_elems, 2)));
          }
        }
        return call_res;
      }
    }
  }
  return zan_call_with_gctx(g, fn_type, static_fn, call_args, argc, gctx_arg,
                            cn);
}

LLVMValueRef emit_dispatch_call(zan_irgen_t *g, zan_symbol_t *static_sym,
                                zan_symbol_t *method_sym,
                                LLVMValueRef static_fn, LLVMTypeRef fn_type,
                                LLVMValueRef *call_args, int argc,
                                const char *cn) {
  return emit_dispatch_call_gctx(g, static_sym, method_sym, static_fn, fn_type,
                                 call_args, argc, NULL, cn);
}
