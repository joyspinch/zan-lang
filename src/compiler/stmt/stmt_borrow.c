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
