/* stmt_borrow.c（irgen_stmt.c 拼接片段）：局部别名借用判定
 *
 * `T q = p;`（p 为局部/参数）曾对每个这样的拷贝做一次原子 retain、出作用域
 * 一次 release。foreach 遍历里 `P q = p;` 让每个元素多付一对原子操作（Mac
 * 实测百万元素列表遍历：0.50 → 0.26 s 的差距全在这里）。只要 p 在 q 的整个
 * 生命期内既不改指向也不出作用域，q 借用 p 持有（或 p 自己借用）的那份引用
 * 即可——与 foreach 循环变量同一机制（arc_owned = 0；return、存入字段等需要
 * +1 的场合由既有路径照常补 retain）。
 *
 * 条件按名字在整个函数体里保守判定，任一不满足即退回原行为：
 *   - p、q 都从未被赋值（含元组赋值、++/--、ref/out 实参、元组解构）；
 *   - q 只声明这一次，p 至多声明一次（排除遮蔽/重名）；
 *   - 函数里没有 lambda / 局部函数 / LINQ 查询（可能捕获）、await（局部进协程
 *     帧）、yield（迭代器状态机）、goto（打乱作用域嵌套）；
 *   - p 是普通局部或参数（非装箱单元、非 byref、非协程帧槽、非结构体、非外来
 *     字符串），且与 q 同为类/接口/object 或同为 string。 */

typedef struct {
    zan_istr_t q, p;
    int q_decls, p_decls;
    bool bad;
} borrow_scan_t;

static bool borrow_name_eq(zan_istr_t a, zan_istr_t b) {
    return a.len == b.len && a.len > 0 && memcmp(a.str, b.str, a.len) == 0;
}

static bool borrow_names(const zan_ast_node_t *n, const borrow_scan_t *s) {
    return n && n->kind == AST_IDENTIFIER &&
           (borrow_name_eq(n->ident.name, s->q) || borrow_name_eq(n->ident.name, s->p));
}

static void borrow_note_decl(borrow_scan_t *s, zan_istr_t name) {
    if (borrow_name_eq(name, s->q)) s->q_decls++;
    if (borrow_name_eq(name, s->p)) s->p_decls++;
}

static zan_walk_action_t borrow_scan_visit(zan_ast_node_t *n, void *u) {
    borrow_scan_t *s = (borrow_scan_t *)u;
    switch (n->kind) {
    case AST_LAMBDA: case AST_YIELD_STMT: case AST_AWAIT_EXPR:
    case AST_QUERY_EXPR: case AST_GOTO_STMT: case AST_METHOD_DECL:
        s->bad = true;
        break;
    case AST_ASSIGNMENT: {
        zan_ast_node_t *l = n->binary.left;
        if (borrow_names(l, s)) s->bad = true;
        if (l && l->kind == AST_TUPLE_EXPR)
            for (int i = 0; i < l->tuple_expr.items.count; i++)
                if (borrow_names(l->tuple_expr.items.items[i], s)) s->bad = true;
        break;
    }
    case AST_UNARY: case AST_POSTFIX_UNARY:
        if (borrow_names(n->unary.operand, s)) s->bad = true;
        break;
    case AST_REF_ARG:
        if (borrow_names(n->ref_arg.expr, s)) s->bad = true;
        break;
    case AST_VAR_DECL:
        borrow_note_decl(s, n->var_decl.name);
        break;
    case AST_FOREACH_STMT:
        borrow_note_decl(s, n->foreach_stmt.var_name);
        break;
    case AST_CATCH_CLAUSE:
        borrow_note_decl(s, n->catch_clause.var_name);
        break;
    case AST_TUPLE_DECON:
        for (int i = 0; i < n->tuple_decon.names.count; i++)
            if (borrow_names(n->tuple_decon.names.items[i], s)) s->bad = true;
        break;
    default:
        break;
    }
    return s->bad ? ZAN_WALK_ABORT : ZAN_WALK_CONTINUE;
}

static bool local_init_can_borrow(zan_irgen_t *g, zan_ast_node_t *stmt,
                                  zan_type_t *type, local_scope_t *locals) {
    zan_ast_node_t *init = stmt->var_decl.initializer;
    if (!init || init->kind != AST_IDENTIFIER || !type) return false;
    if (!(is_arc_managed_type(type) || type->kind == TYPE_STRING)) return false;
    if (g->current_async_frame || !g->current_fn_body) return false;
    local_var_t *src = local_find(locals, init->ident.name);
    if (!src || !src->type || src->box_cell || src->byref_slot ||
        src->async_decl || src->frame_owner >= 0 || src->struct_rc ||
        src->obj_rc_flag || src->opaque_string)
        return false;
    zan_type_t *st = src->type;
    if (st->kind != type->kind ||
        !(is_arc_managed_type(st) || st->kind == TYPE_STRING))
        return false;
    borrow_scan_t scan;
    memset(&scan, 0, sizeof scan);
    scan.q = stmt->var_decl.name;
    scan.p = init->ident.name;
    zan_ast_walk(g->current_fn_body, borrow_scan_visit, NULL, &scan);
    return !scan.bad && scan.q_decls == 1 && scan.p_decls <= 1;
}

/* —— 方法调用接收者免钉住 ——
 * 调用点对局部/参数接收者曾一律 retain + EH 临时栈压入，调用后弹出 + release
 * （expr_pred.c emit_hold_call_receiver），理由是实参里的 ref 可能在派发前清空
 * 该槽。Mac 实测：对参数做虚调用/接口调用比 C++ 慢 ~10 倍、Queue 操作慢 11 倍，
 * 大头就是这对钉住。
 *
 * 只要接收者是**自己持有引用**的局部或参数（arc_owned，非 foreach/别名借用——
 * 被调者可能清空它所借的集合），且在整个函数里从不被重新赋值/ref-out/重名声明、
 * 函数无 lambda/await/yield/goto、也非装箱/byref/协程帧/结构体局部，则调用
 * 期间没有任何途径让它的那份引用消失，钉住纯属多余。判定复用上面的全函数
 * 扫描；按（函数体, 名字）缓存，避免大函数里每个调用点重扫。 */
typedef struct { zan_ast_node_t *body; zan_istr_t name; bool ok; } recv_cache_t;
static recv_cache_t g_recv_cache[32];
static int g_recv_cache_n;
static zan_ast_node_t *g_recv_cache_body;

bool zan_local_never_reassigned(zan_irgen_t *g, zan_istr_t name) {
    zan_ast_node_t *body = g->current_fn_body;
    if (!body || g->current_async_frame) return false;
    if (body != g_recv_cache_body) { g_recv_cache_body = body; g_recv_cache_n = 0; }
    for (int i = 0; i < g_recv_cache_n; i++)
        if (borrow_name_eq(g_recv_cache[i].name, name)) return g_recv_cache[i].ok;
    borrow_scan_t scan;
    memset(&scan, 0, sizeof scan);
    scan.q = name;
    scan.p = name;
    zan_ast_walk(body, borrow_scan_visit, NULL, &scan);
    bool ok = !scan.bad && scan.q_decls <= 1;
    if (g_recv_cache_n < 32) {
        g_recv_cache[g_recv_cache_n].body = body;
        g_recv_cache[g_recv_cache_n].name = name;
        g_recv_cache[g_recv_cache_n].ok = ok;
        g_recv_cache_n++;
    }
    return ok;
}

bool zan_receiver_needs_no_pin(zan_irgen_t *g, zan_ast_node_t *object,
                               local_scope_t *locals) {
    if (!object || object->kind != AST_IDENTIFIER || !locals) return false;
    local_var_t *v = local_find(locals, object->ident.name);
    if (!v || !v->arc_owned || v->box_cell || v->byref_slot || v->async_decl ||
        v->frame_owner >= 0 || v->struct_rc || v->obj_rc_flag)
        return false;
    return zan_local_never_reassigned(g, object->ident.name);
}

/* —— 数组元素接收者免钉住 ——
 * `arr[i].M(args)` 的接收者来自数组槽：钉住（retain + EH 临时栈 + release，
 * 两次原子操作）防的是"调用期间有人改写该槽、把 this 释放掉"。只有能写数组槽
 * 或运行任意用户代码的东西才做得到，所以满足下列全部条件时钉住多余：
 *   - arr 是局部或形参（借用的也行：整个钉住窗口里没有任何代码能释放对象，
 *     数组在读槽时活着，调用返回前就一直活着）；
 *   - 下标与全部实参只由局部读、字面量与原始数值/布尔运算组成（无调用、
 *     无属性 getter、无重载运算符）；
 *   - M 的每一个可能实现（静态类型的全部子类 / 接口实现类）都是"叶子体"：
 *     无调用、无 new、无 lambda/await/throw、无下标读写；运算、赋值、转换只作用
 *     于原始数值/布尔/字符/枚举；成员只读写 this 的字段（属性 getter 是用户代码）。
 * 任一判不出即保守钉住。另一线程无同步地并发写同一槽本就是数据竞争（读槽与
 * retain 之间同样不原子），不在钉住的保障范围内。 */
#define LEAF_MAX_LOCALS 32
typedef struct {
    zan_irgen_t *g;
    zan_symbol_t *cls;
    zan_ast_node_t *decl;
    zan_istr_t lname[LEAF_MAX_LOCALS];
    zan_type_t *ltype[LEAF_MAX_LOCALS];
    int nl;
    bool ok;
} leaf_scan_t;

static bool leaf_prim(const zan_type_t *t) {
    if (!t) return false;
    return (t->kind >= TYPE_BOOL && t->kind <= TYPE_DOUBLE) ||
           t->kind == TYPE_CHAR || t->kind == TYPE_NINT || t->kind == TYPE_ENUM;
}

static zan_type_t *leaf_field_type(zan_symbol_t *cls, zan_istr_t name) {
    for (int depth = 0; cls && depth < 64; depth++) {
        for (int i = 0; i < cls->member_count; i++) {
            zan_symbol_t *m = cls->members[i];
            if (!borrow_name_eq(m->name, name)) continue;
            /* 同名的非字段成员（属性/方法）：读它可能跑用户代码 */
            return m->kind == SYM_FIELD ? m->type : NULL;
        }
        if (!cls->type || !cls->type->base_type) break;
        cls = cls->type->base_type->sym;
    }
    return NULL;
}

static zan_type_t *leaf_name_type(leaf_scan_t *L, zan_istr_t name) {
    for (int i = L->nl - 1; i >= 0; i--)
        if (borrow_name_eq(L->lname[i], name)) return L->ltype[i];
    zan_ast_list_t *ps = &L->decl->method_decl.params;
    for (int i = 0; i < ps->count; i++) {
        zan_ast_node_t *p = ps->items[i];
        if (!p || !borrow_name_eq(p->param.name, name)) continue;
        if (p->param.by_ref || p->param.is_params) return NULL;
        return zan_binder_resolve_type(L->g->binder, p->param.type);
    }
    return leaf_field_type(L->cls, name);
}

static zan_type_t *leaf_expr(leaf_scan_t *L, zan_ast_node_t *n);

/* 可写的原始值位置：局部/形参/本类字段，或 this.字段 */
static zan_type_t *leaf_place(leaf_scan_t *L, zan_ast_node_t *n) {
    if (!n) return NULL;
    if (n->kind == AST_IDENTIFIER || n->kind == AST_MEMBER_ACCESS) {
        zan_type_t *t = leaf_expr(L, n);
        return leaf_prim(t) ? t : NULL;
    }
    return NULL;
}

static zan_type_t *leaf_expr(leaf_scan_t *L, zan_ast_node_t *n) {
    if (!n || !L->ok) { L->ok = false; return NULL; }
    zan_type_t *t = NULL;
    switch (n->kind) {
    case AST_INT_LITERAL: case AST_FLOAT_LITERAL:
    case AST_CHAR_LITERAL: case AST_BOOL_LITERAL:
        t = L->g->binder->type_int;
        if (n->kind == AST_BOOL_LITERAL) t = L->g->binder->type_bool;
        break;
    case AST_IDENTIFIER:
        t = leaf_name_type(L, n->ident.name);
        break;
    case AST_THIS_EXPR:
        t = L->cls ? L->cls->type : NULL;
        break;
    case AST_MEMBER_ACCESS:
        if (n->member.object && n->member.object->kind == AST_THIS_EXPR &&
            !n->member.null_cond)
            t = leaf_field_type(L->cls, n->member.name);
        break;
    case AST_BINARY: {
        zan_type_t *lt = leaf_expr(L, n->binary.left);
        zan_type_t *rt = leaf_expr(L, n->binary.right);
        if (leaf_prim(lt) && leaf_prim(rt)) {
            switch (n->binary.op) {
            case TK_EQ_EQ: case TK_BANG_EQ: case TK_LESS: case TK_GREATER:
            case TK_LESS_EQ: case TK_GREATER_EQ: case TK_AMP_AMP: case TK_PIPE_PIPE:
                t = L->g->binder->type_bool;
                break;
            default:
                t = lt;
                break;
            }
        }
        break;
    }
    case AST_UNARY: case AST_POSTFIX_UNARY: {
        zan_type_t *ot = (n->unary.op == TK_PLUS_PLUS || n->unary.op == TK_MINUS_MINUS)
            ? leaf_place(L, n->unary.operand) : leaf_expr(L, n->unary.operand);
        if (leaf_prim(ot)) t = ot;
        break;
    }
    case AST_ASSIGNMENT: {
        zan_type_t *lt = leaf_place(L, n->binary.left);
        zan_type_t *rt = leaf_expr(L, n->binary.right);
        if (leaf_prim(lt) && leaf_prim(rt)) t = lt;
        break;
    }
    case AST_CONDITIONAL: {
        zan_type_t *ct = leaf_expr(L, n->conditional.cond);
        zan_type_t *a = leaf_expr(L, n->conditional.then_expr);
        zan_type_t *b = leaf_expr(L, n->conditional.else_expr);
        if (leaf_prim(ct) && leaf_prim(a) && leaf_prim(b)) t = a;
        break;
    }
    case AST_CAST_EXPR: {
        zan_type_t *to = zan_binder_resolve_type(L->g->binder, n->cast.type);
        zan_type_t *from = leaf_expr(L, n->cast.expr);
        if (leaf_prim(to) && leaf_prim(from)) t = to;
        break;
    }
    default:
        break;
    }
    if (!t) L->ok = false;
    return t;
}

static void leaf_stmt(leaf_scan_t *L, zan_ast_node_t *n) {
    if (!L->ok) return;
    if (!n) return;
    switch (n->kind) {
    case AST_BLOCK:
        for (int i = 0; i < n->block.stmts.count && L->ok; i++)
            leaf_stmt(L, n->block.stmts.items[i]);
        return;
    case AST_RETURN_STMT:
        /* 返回字段/局部（任意类型）只是一次读取 + 返回所需的 retain */
        if (n->ret.value) leaf_expr(L, n->ret.value);
        return;
    case AST_EXPR_STMT:
        leaf_expr(L, n->expr_stmt.expr);
        return;
    case AST_VAR_DECL: {
        zan_type_t *t = n->var_decl.type
            ? zan_binder_resolve_type(L->g->binder, n->var_decl.type) : NULL;
        if (n->var_decl.initializer) {
            zan_type_t *it = leaf_expr(L, n->var_decl.initializer);
            if (!t) t = it;
            if (!leaf_prim(it)) L->ok = false;
        }
        if (!leaf_prim(t) || L->nl >= LEAF_MAX_LOCALS) { L->ok = false; return; }
        L->lname[L->nl] = n->var_decl.name;
        L->ltype[L->nl] = t;
        L->nl++;
        return;
    }
    case AST_IF_STMT:
        if (!leaf_prim(leaf_expr(L, n->if_stmt.cond))) { L->ok = false; return; }
        leaf_stmt(L, n->if_stmt.then_body);
        leaf_stmt(L, n->if_stmt.else_body);
        return;
    case AST_WHILE_STMT:
        if (!leaf_prim(leaf_expr(L, n->while_stmt.cond))) { L->ok = false; return; }
        leaf_stmt(L, n->while_stmt.body);
        return;
    case AST_FOR_STMT:
        if (n->for_stmt.init) {
            if (n->for_stmt.init->kind == AST_VAR_DECL || n->for_stmt.init->kind == AST_BLOCK)
                leaf_stmt(L, n->for_stmt.init);
            else leaf_expr(L, n->for_stmt.init);
        }
        if (n->for_stmt.cond && !leaf_prim(leaf_expr(L, n->for_stmt.cond))) {
            L->ok = false; return;
        }
        if (n->for_stmt.step) leaf_expr(L, n->for_stmt.step);
        leaf_stmt(L, n->for_stmt.body);
        return;
    case AST_BREAK_STMT: case AST_CONTINUE_STMT:
        return;
    default:
        L->ok = false;
        return;
    }
}

typedef struct { zan_symbol_t *m; bool ok; } leaf_cache_t;
static leaf_cache_t g_leaf_cache[128];
static int g_leaf_cache_n;

static bool method_is_leaf(zan_irgen_t *g, zan_symbol_t *m) {
    if (!m || m->kind != SYM_METHOD || !m->decl ||
        m->decl->kind != AST_METHOD_DECL || !m->decl->method_decl.body)
        return false;
    for (int i = 0; i < g_leaf_cache_n; i++)
        if (g_leaf_cache[i].m == m) return g_leaf_cache[i].ok;
    zan_ast_node_t *d = m->decl;
    bool ok = !(d->method_decl.modifiers & (MOD_ASYNC | MOD_EXTERN | MOD_STATIC)) &&
              d->method_decl.type_params.count == 0 && !d->method_decl.is_variadic &&
              !d->method_decl.ext;
    if (ok) {
        leaf_scan_t L;
        memset(&L, 0, sizeof L);
        L.g = g;
        L.cls = m->parent;
        L.decl = d;
        L.ok = true;
        zan_ast_node_t *body = d->method_decl.body;
        if (body->kind == AST_BLOCK) leaf_stmt(&L, body);
        else leaf_expr(&L, body);      /* expression-bodied member */
        ok = L.ok;
    }
    if (g_leaf_cache_n < 128) {
        g_leaf_cache[g_leaf_cache_n].m = m;
        g_leaf_cache[g_leaf_cache_n].ok = ok;
        g_leaf_cache_n++;
    }
    return ok;
}

/* 调用方一侧的实参/下标：只许局部读、字面量与原始数值运算 */
static bool caller_simple_expr(zan_irgen_t *g, zan_ast_node_t *n,
                               local_scope_t *locals) {
    if (!n) return false;
    switch (n->kind) {
    case AST_INT_LITERAL: case AST_FLOAT_LITERAL:
    case AST_CHAR_LITERAL: case AST_BOOL_LITERAL: case AST_NULL_LITERAL:
        return true;
    case AST_IDENTIFIER: {
        local_var_t *v = local_find(locals, n->ident.name);
        return v && !v->byref_slot;
    }
    case AST_BINARY:
        return n->binary.op != TK_QUESTION_QUESTION &&
               caller_simple_expr(g, n->binary.left, locals) &&
               caller_simple_expr(g, n->binary.right, locals) &&
               leaf_prim(infer_expr_type(g, n->binary.left, locals)) &&
               leaf_prim(infer_expr_type(g, n->binary.right, locals));
    case AST_UNARY:
        return n->unary.op != TK_PLUS_PLUS && n->unary.op != TK_MINUS_MINUS &&
               caller_simple_expr(g, n->unary.operand, locals) &&
               leaf_prim(infer_expr_type(g, n->unary.operand, locals));
    case AST_CONDITIONAL:
        return caller_simple_expr(g, n->conditional.cond, locals) &&
               caller_simple_expr(g, n->conditional.then_expr, locals) &&
               caller_simple_expr(g, n->conditional.else_expr, locals) &&
               leaf_prim(infer_expr_type(g, n, locals));
    case AST_CAST_EXPR:
        return caller_simple_expr(g, n->cast.expr, locals) &&
               leaf_prim(infer_expr_type(g, n->cast.expr, locals)) &&
               leaf_prim(infer_expr_type(g, n, locals));
    default:
        return false;
    }
}

static bool sym_derives_from(zan_symbol_t *cls, zan_symbol_t *base) {
    for (int depth = 0; cls && depth < 512; depth++) {
        if (cls == base) return true;
        if (!cls->type || !cls->type->base_type) return false;
        cls = cls->type->base_type->sym;
    }
    return false;
}

/* The array base is read without side effects: a local/parameter, or an
 * instance field of `this` (bare `items` or `this.items`). Reading a field
 * releases nothing and the caller keeps `this` alive; the leaf-callee rule
 * below guarantees nothing in the window can replace the field either.
 * (Generic containers keep their arrays in fields: `items[i].Score()` in a
 * Box<T> paid a full pin per element -- 34x C++.) */
static bool elem_base_is_stable_read(zan_irgen_t *g, zan_ast_node_t *base, local_scope_t *locals) {
    if (!base) return false;
    zan_istr_t fname;
    if (base->kind == AST_IDENTIFIER) {
        if (local_find(locals, base->ident.name)) return true;
        fname = base->ident.name;
    } else if (base->kind == AST_MEMBER_ACCESS && base->member.object &&
               base->member.object->kind == AST_THIS_EXPR && !base->member.null_cond) {
        fname = base->member.name;
    } else {
        return false;
    }
    if (!g->current_type_sym || !g->current_this) return false;
    zan_symbol_t *fs = get_field_sym(g->current_type_sym, fname);
    return fs && fs->kind == SYM_FIELD && !field_member_is_static(fs);
}

bool zan_elem_receiver_needs_no_pin(zan_irgen_t *g, zan_ast_node_t *object,
                                    zan_ast_node_t *call, local_scope_t *locals) {
    if (!object || object->kind != AST_INDEX || !call || call->kind != AST_CALL ||
        !call->call.callee || call->call.callee->kind != AST_MEMBER_ACCESS ||
        call->call.callee->member.object != object ||
        call->call.callee->member.null_cond || call->call.type_args.count > 0)
        return false;
    if (object->index.extra.count > 0) return false;
    /* The array only has to stay alive for the pinned window, and nothing
     * in that window (simple index/arguments, leaf callee) can release any
     * object: a plain local or parameter read is enough, borrowed or not. */
    zan_ast_node_t *base = object->index.object;
    if (!elem_base_is_stable_read(g, base, locals)) return false;
    zan_type_t *at = infer_expr_type(g, object->index.object, locals);
    if (!at || at->kind != TYPE_ARRAY) return false;
    if (!caller_simple_expr(g, object->index.index, locals) ||
        !leaf_prim(infer_expr_type(g, object->index.index, locals)))
        return false;
    int argc = call->call.args.count;
    for (int i = 0; i < argc; i++)
        if (!caller_simple_expr(g, call->call.args.items[i], locals)) return false;
    zan_type_t *st = infer_expr_type(g, object, locals);
    if (!st || !st->sym || (st->kind != TYPE_CLASS && st->kind != TYPE_INTERFACE))
        return false;
    zan_istr_t name = call->call.callee->member.name;
    int cands = 0;
    for (int pass = 0; pass < 2; pass++) {
        int n = pass == 0 ? g->struct_type_count : g->generic_inst_count;
        for (int i = 0; i < n; i++) {
            zan_symbol_t *cls = pass == 0 ? g->struct_types[i].sym
                                          : g->generic_insts[i].type_sym;
            if (!cls) continue;
            bool related = st->kind == TYPE_INTERFACE
                ? class_implements_iface(cls, st->sym)
                : sym_derives_from(cls, st->sym);
            if (!related) continue;
            zan_symbol_t *impl = resolve_overload(cls, name, argc, 0);
            if (!impl) {
                if (st->kind == TYPE_INTERFACE) continue;
                return false;
            }
            if (!method_is_leaf(g, impl)) return false;
            cands++;
        }
    }
    return cands > 0;
}
