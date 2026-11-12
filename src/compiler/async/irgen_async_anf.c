/* FRAGMENT（拼接片段，非独立 TU）：由 irgen_async.c composite dispatcher 拼入。
 * 域：await A-标准型（ANF）正规化——表达式提升、receiver spill、
 * 语句/循环条件守卫与块归一。
 * 文件级 static（anf_expr/anf_hoist_await/anf_spill_await_receiver 等）
 * 为本片段私有。 */
typedef struct {
    zan_irgen_t    *g;
    zan_ast_list_t *out;
    int            *counter;
} anf_ctx_t;

static bool anf_expr_contains_await(zan_ast_node_t *e) {
    if (!e) return false;
    switch (e->kind) {
    case AST_AWAIT_EXPR: return true;
    case AST_BINARY:
    case AST_ASSIGNMENT:
        return anf_expr_contains_await(e->binary.left) ||
               anf_expr_contains_await(e->binary.right);
    case AST_UNARY:
    case AST_POSTFIX_UNARY:
        return anf_expr_contains_await(e->unary.operand);
    case AST_CALL: {
        if (anf_expr_contains_await(e->call.callee)) return true;
        for (int i = 0; i < e->call.args.count; i++)
            if (anf_expr_contains_await(e->call.args.items[i])) return true;
        return false;
    }
    case AST_MEMBER_ACCESS: return anf_expr_contains_await(e->member.object);
    case AST_INDEX:
        return anf_expr_contains_await(e->index.object) ||
               anf_expr_contains_await(e->index.index);
    case AST_CONDITIONAL:
        return anf_expr_contains_await(e->conditional.cond) ||
               anf_expr_contains_await(e->conditional.then_expr) ||
               anf_expr_contains_await(e->conditional.else_expr);
    case AST_NEW_EXPR: {
        for (int i = 0; i < e->new_expr.args.count; i++)
            if (anf_expr_contains_await(e->new_expr.args.items[i])) return true;
        for (int i = 0; i < e->new_expr.arg_inits.count; i++)
            if (anf_expr_contains_await(e->new_expr.arg_inits.items[i]))
                return true;
        return false;
    }
    case AST_COLL_INIT: {
        for (int i = 0; i < e->coll_init.items.count; i++)
            if (anf_expr_contains_await(e->coll_init.items.items[i])) return true;
        return false;
    }
    case AST_CAST_EXPR: return anf_expr_contains_await(e->cast.expr);
    case AST_IS_EXPR:
    case AST_AS_EXPR:  return anf_expr_contains_await(e->type_test.expr);
    default: return false;
    }
}

/* 提升位于 await 之前且具副作用的操作数至临时变量，保证求值顺序保真 */
static bool anf_expr_has_side_effect(zan_ast_node_t *e) {
    if (!e) return false;
    switch (e->kind) {
    case AST_AWAIT_EXPR: return false;
    case AST_CALL: return true;
    case AST_ASSIGNMENT: return true;
    case AST_POSTFIX_UNARY: return true;
    case AST_UNARY:
        if (e->unary.op == TK_PLUS_PLUS || e->unary.op == TK_MINUS_MINUS) return true;
        return anf_expr_has_side_effect(e->unary.operand);
    case AST_BINARY:
        return anf_expr_has_side_effect(e->binary.left) ||
               anf_expr_has_side_effect(e->binary.right);
    case AST_MEMBER_ACCESS: return anf_expr_has_side_effect(e->member.object);
    case AST_INDEX:
        return anf_expr_has_side_effect(e->index.object) ||
               anf_expr_has_side_effect(e->index.index);
    case AST_CAST_EXPR: return anf_expr_has_side_effect(e->cast.expr);
    case AST_IS_EXPR:
    case AST_AS_EXPR:  return anf_expr_has_side_effect(e->type_test.expr);
    default: return false;
    }
}

static zan_ast_node_t *anf_expr(anf_ctx_t *c, zan_ast_node_t *e);
static void anf_spill_await_receiver(anf_ctx_t *c, zan_ast_node_t *aw);

/* 检查先求值的操作数是否具副作用且后续操作数含 await */
static void anf_check_order(anf_ctx_t *c, zan_ast_node_t *before, zan_ast_node_t *after) {
    if (after && before && anf_expr_contains_await(after) &&
        anf_expr_has_side_effect(before)) {
        zan_diag_emit_code(c->g->diag, DIAG_ERROR, before->loc, 3098,
            "async: an expression with side effects is evaluated before an "
            "await in the same expression; assign it to a local first");
    }
}

/* A frame-backed local also isolates lazy expressions without evaluating dead arms. */
static zan_ast_node_t *anf_hoist_value(anf_ctx_t *c, zan_ast_node_t *value) {
    char buf[32];
    int len = snprintf(buf, sizeof buf, "$aw%d", (*c->counter)++);
    char *nm = (char *)zan_arena_alloc(c->g->arena, (size_t)len + 1);
    memcpy(nm, buf, (size_t)len + 1);
    zan_istr_t name = { nm, (uint32_t)len };

    zan_ast_node_t *vd = zan_ast_new(c->g->arena, AST_VAR_DECL, value->loc);
    vd->var_decl.name = name;
    vd->var_decl.type = NULL;
    vd->var_decl.initializer = value;
    zan_ast_list_push(c->out, vd, c->g->arena);

    zan_ast_node_t *id = zan_ast_new(c->g->arena, AST_IDENTIFIER, value->loc);
    id->ident.name = name;
    return id;
}

/* 将 `await E` 提升为 `var $awN = await E;` 声明并返回该临时变量引用 */
static zan_ast_node_t *anf_hoist_await(anf_ctx_t *c, zan_ast_node_t *aw) {
    aw->await_expr.expr = anf_expr(c, aw->await_expr.expr);
    anf_spill_await_receiver(c, aw);
    return anf_hoist_value(c, aw);
}

/* 将被 await 调用的计算所得接收者提升为独立局部变量，避免跨挂起点丢失 SSA 临时值 */
static bool anf_receiver_needs_spill(zan_ast_node_t *obj) {
    if (!obj) return false;
    switch (obj->kind) {
    case AST_CALL:
    case AST_NEW_EXPR:
    case AST_INDEX:
    case AST_CONDITIONAL:
    case AST_CAST_EXPR:
        return true;
    case AST_MEMBER_ACCESS:
        return anf_receiver_needs_spill(obj->member.object);
    default:
        return false;
    }
}

static void anf_spill_await_receiver(anf_ctx_t *c, zan_ast_node_t *aw) {
    zan_ast_node_t *call = aw->await_expr.expr;
    if (!call || call->kind != AST_CALL) return;
    zan_ast_node_t *callee = call->call.callee;
    if (!callee || callee->kind != AST_MEMBER_ACCESS) return;
    zan_ast_node_t *obj = callee->member.object;
    if (!anf_receiver_needs_spill(obj)) return;

    char buf[32];
    int len = snprintf(buf, sizeof buf, "$rc%d", (*c->counter)++);
    char *nm = (char *)zan_arena_alloc(c->g->arena, (size_t)len + 1);
    memcpy(nm, buf, (size_t)len + 1);
    zan_istr_t name = { nm, (uint32_t)len };

    zan_ast_node_t *vd = zan_ast_new(c->g->arena, AST_VAR_DECL, obj->loc);
    vd->var_decl.name = name;
    vd->var_decl.type = NULL;
    vd->var_decl.initializer = obj;
    zan_ast_list_push(c->out, vd, c->g->arena);

    zan_ast_node_t *id = zan_ast_new(c->g->arena, AST_IDENTIFIER, obj->loc);
    id->ident.name = name;
    callee->member.object = id;
}

/* 递归提取表达式中的 await 并按求值顺序追加至 hoisted 声明列表 */
static zan_ast_node_t *anf_expr(anf_ctx_t *c, zan_ast_node_t *e) {
    if (!e) return e;
    switch (e->kind) {
    case AST_AWAIT_EXPR:
        return anf_hoist_await(c, e);
    case AST_BINARY:
    case AST_ASSIGNMENT:
        if (e->binary.op == TK_AMP_AMP || e->binary.op == TK_PIPE_PIPE) {
            e->binary.left = anf_expr(c, e->binary.left);
            if (anf_expr_contains_await(e->binary.right)) {
                /* Normalize the lazy arm inside its guard. Leaving raw awaits
                 * here lets operand SSA values escape across resume entries. */
                zan_ast_node_t *result = anf_hoist_value(c, e->binary.left);
                zan_ast_node_t *body = zan_ast_new(c->g->arena, AST_BLOCK, e->loc);
                zan_ast_list_init(&body->block.stmts);
                anf_ctx_t branch = { c->g, &body->block.stmts, c->counter };
                zan_ast_node_t *rhs = anf_expr(&branch, e->binary.right);
                zan_ast_node_t *assign = zan_ast_new(c->g->arena, AST_ASSIGNMENT, e->loc);
                assign->binary.op = TK_EQ;
                assign->binary.left = result;
                assign->binary.right = rhs;
                zan_ast_node_t *stmt = zan_ast_new(c->g->arena, AST_EXPR_STMT, e->loc);
                stmt->expr_stmt.expr = assign;
                zan_ast_list_push(&body->block.stmts, stmt, c->g->arena);
                zan_ast_node_t *guard = result;
                if (e->binary.op == TK_PIPE_PIPE) {
                    guard = zan_ast_new(c->g->arena, AST_UNARY, e->loc);
                    guard->unary.op = TK_BANG;
                    guard->unary.operand = result;
                }
                zan_ast_node_t *ifs = zan_ast_new(c->g->arena, AST_IF_STMT, e->loc);
                ifs->if_stmt.cond = guard;
                ifs->if_stmt.then_body = body;
                ifs->if_stmt.else_body = NULL;
                zan_ast_list_push(c->out, ifs, c->g->arena);
                return result;
            }
            return e;
        }
        anf_check_order(c, e->binary.left, e->binary.right);
        e->binary.left = anf_expr(c, e->binary.left);
        /* Snapshot a value before a later await can mutate its source. An
         * assignment's left side is a storage location, not a value snapshot. */
        if (e->kind == AST_BINARY && anf_expr_contains_await(e->binary.right))
            e->binary.left = anf_hoist_value(c, e->binary.left);
        e->binary.right = anf_expr(c, e->binary.right);
        return e;
    case AST_UNARY:
    case AST_POSTFIX_UNARY:
        e->unary.operand = anf_expr(c, e->unary.operand);
        return e;
    case AST_CALL: {
        bool args_await = false;
        for (int i = 0; i < e->call.args.count; i++)
            args_await |= anf_expr_contains_await(e->call.args.items[i]);
        e->call.callee = anf_expr(c, e->call.callee);
        /* Capture an instance receiver before any argument can suspend or mutate it.
         * Type-name receivers remain names; they are not runtime values. */
        zan_ast_node_t *callee = e->call.callee;
        if (args_await && callee && callee->kind == AST_MEMBER_ACCESS) {
            zan_ast_node_t *obj = callee->member.object;
            bool snapshot = anf_receiver_needs_spill(obj);
            if (obj && obj->kind == AST_IDENTIFIER) {
                zan_symbol_t *sym = zan_binder_lookup(c->g->binder, obj->ident.name);
                snapshot = !sym || (sym->kind != SYM_CLASS &&
                    sym->kind != SYM_STRUCT && sym->kind != SYM_NAMESPACE);
                /* Native static types need no binder declaration. A resolved
                 * instance call still snapshots a shadowing local receiver. */
                if (!sym && !e->call.resolved_sym) {
                    int count = 0;
                    const zan_builtin_type_t *types = zan_builtin_types(&count);
                    for (int i = 0; i < count; i++) {
                        if (types[i].is_static &&
                            strlen(types[i].type) == obj->ident.name.len &&
                            memcmp(types[i].type, obj->ident.name.str,
                                   obj->ident.name.len) == 0) {
                            snapshot = false;
                            break;
                        }
                    }
                }
            }
            if (snapshot) callee->member.object = anf_hoist_value(c, obj);
        }
        for (int i = 0; i < e->call.args.count; i++) {
            bool later_await = false;
            for (int j = i + 1; j < e->call.args.count; j++)
                later_await |= anf_expr_contains_await(e->call.args.items[j]);
            zan_ast_node_t *arg = anf_expr(c, e->call.args.items[i]);
            if (later_await && arg->kind != AST_LAMBDA &&
                arg->kind != AST_REF_ARG)
                arg = anf_hoist_value(c, arg);
            e->call.args.items[i] = arg;
        }
        return e;
    }
    case AST_MEMBER_ACCESS:
        e->member.object = anf_expr(c, e->member.object);
        return e;
    case AST_INDEX:
        anf_check_order(c, e->index.object, e->index.index);
        e->index.object = anf_expr(c, e->index.object);
        e->index.index  = anf_expr(c, e->index.index);
        return e;
    case AST_CONDITIONAL:
        e->conditional.cond = anf_expr(c, e->conditional.cond);
        if (anf_expr_contains_await(e->conditional.then_expr) ||
            anf_expr_contains_await(e->conditional.else_expr))
            return anf_hoist_value(c, e);
        return e;
    case AST_CAST_EXPR:
        e->cast.expr = anf_expr(c, e->cast.expr);
        return e;
    case AST_NEW_EXPR:
        for (int i = 0; i < e->new_expr.args.count; i++) {
            for (int j = i + 1; j < e->new_expr.args.count; j++)
                anf_check_order(c, e->new_expr.args.items[i], e->new_expr.args.items[j]);
        }
        for (int i = 0; i < e->new_expr.args.count; i++)
            e->new_expr.args.items[i] = anf_expr(c, e->new_expr.args.items[i]);
        for (int i = 0; i < e->new_expr.arg_inits.count; i++)
            e->new_expr.arg_inits.items[i] = anf_expr(c, e->new_expr.arg_inits.items[i]);
        return e;
    case AST_COLL_INIT:
        for (int i = 0; i < e->coll_init.items.count; i++) {
            for (int j = i + 1; j < e->coll_init.items.count; j++)
                anf_check_order(c, e->coll_init.items.items[i], e->coll_init.items.items[j]);
        }
        for (int i = 0; i < e->coll_init.items.count; i++)
            e->coll_init.items.items[i] = anf_expr(c, e->coll_init.items.items[i]);
        return e;
    case AST_IS_EXPR:
    case AST_AS_EXPR:
        e->type_test.expr = anf_expr(c, e->type_test.expr);
        return e;
    default:
        return e;
    }
}
void anf_normalize_block(zan_irgen_t *g, zan_ast_node_t *block, int *counter);

/* 递归判断语句子树中是否包含任何 await 节点 */
bool anf_stmt_contains_await(zan_ast_node_t *st) {
    if (!st) return false;
    switch (st->kind) {
    case AST_VAR_DECL:    return anf_expr_contains_await(st->var_decl.initializer);
    case AST_EXPR_STMT:   return anf_expr_contains_await(st->expr_stmt.expr);
    case AST_RETURN_STMT: return anf_expr_contains_await(st->ret.value);
    case AST_THROW_STMT:  return anf_expr_contains_await(st->throw_stmt.value);
    case AST_IF_STMT:
        return anf_expr_contains_await(st->if_stmt.cond) ||
               anf_stmt_contains_await(st->if_stmt.then_body) ||
               anf_stmt_contains_await(st->if_stmt.else_body);
    case AST_WHILE_STMT:
    case AST_DO_WHILE_STMT:
        return anf_expr_contains_await(st->while_stmt.cond) ||
               anf_stmt_contains_await(st->while_stmt.body);
    case AST_FOR_STMT:
        return anf_stmt_contains_await(st->for_stmt.init) ||
               anf_expr_contains_await(st->for_stmt.cond) ||
               anf_expr_contains_await(st->for_stmt.step) ||
               anf_stmt_contains_await(st->for_stmt.body);
    case AST_FOREACH_STMT:
        return anf_expr_contains_await(st->foreach_stmt.collection) ||
               anf_stmt_contains_await(st->foreach_stmt.body);
    case AST_TRY_STMT: {
        if (anf_stmt_contains_await(st->try_stmt.try_body)) return true;
        for (int i = 0; i < st->try_stmt.catches.count; i++)
            if (anf_stmt_contains_await(st->try_stmt.catches.items[i]))
                return true;
        return anf_stmt_contains_await(st->try_stmt.finally_body);
    }
    case AST_CATCH_CLAUSE:
        return anf_stmt_contains_await(st->catch_clause.body);
    case AST_SWITCH_STMT: {
        if (anf_expr_contains_await(st->switch_stmt.expr)) return true;
        for (int i = 0; i < st->switch_stmt.cases.count; i++)
            if (anf_stmt_contains_await(st->switch_stmt.cases.items[i]))
                return true;
        return false;
    }
    case AST_SWITCH_CASE:
        return anf_stmt_contains_await(st->switch_case.body);
    case AST_LOCK_STMT:
        return anf_expr_contains_await(st->lock_stmt.expr) ||
               anf_stmt_contains_await(st->lock_stmt.body);
    case AST_CHECKED_STMT:
        return anf_stmt_contains_await(st->checked_stmt.body);
    case AST_BLOCK: {
        for (int i = 0; i < st->block.stmts.count; i++)
            if (anf_stmt_contains_await(st->block.stmts.items[i])) return true;
        return false;
    }
    default: return false;
    }
}

/* 确保目标槽位为 AST_BLOCK 节点以容纳提升语句并进行正规化 */
static void anf_normalize_body(zan_irgen_t *g, zan_ast_node_t **slot, int *counter) {
    zan_ast_node_t *body = *slot;
    if (!body) return;
    if (body->kind != AST_BLOCK) {
        if (!anf_stmt_contains_await(body)) return;
        zan_ast_node_t *blk = zan_ast_new(g->arena, AST_BLOCK, body->loc);
        zan_ast_list_init(&blk->block.stmts);
        zan_ast_list_push(&blk->block.stmts, body, g->arena);
        *slot = blk;
        body = blk;
    }
    anf_normalize_block(g, body, counter);
}

/* 重写条件含 await 的循环表达式，使其在循环体首部作为语句求值 */
static zan_ast_node_t *anf_loop_cond_guard(zan_irgen_t *g, zan_ast_list_t *dst,
                                           int *counter, zan_ast_node_t **cond_slot,
                                           const zan_loc_t *loc) {
    anf_ctx_t c = { g, dst, counter };
    zan_ast_node_t *residual = anf_expr(&c, *cond_slot);
    zan_ast_node_t *neg = zan_ast_new(g->arena, AST_UNARY, *loc);
    neg->unary.op = TK_BANG;
    neg->unary.operand = residual;
    zan_ast_node_t *tb = zan_ast_new(g->arena, AST_BLOCK, *loc);
    zan_ast_list_init(&tb->block.stmts);
    zan_ast_list_push(&tb->block.stmts,
        zan_ast_new(g->arena, AST_BREAK_STMT, *loc), g->arena);
    zan_ast_node_t *ifs = zan_ast_new(g->arena, AST_IF_STMT, *loc);
    ifs->if_stmt.cond = neg;
    ifs->if_stmt.then_body = tb;
    ifs->if_stmt.else_body = NULL;
    zan_ast_list_push(dst, ifs, g->arena);
    zan_ast_node_t *tru = zan_ast_new(g->arena, AST_BOOL_LITERAL, *loc);
    tru->bool_val = true;
    return tru;
}

/* 单条语句正规化：将提升出的声明按求值顺序追加至目标列表 */
static void anf_normalize_stmt(zan_irgen_t *g, zan_ast_node_t *st,
                               zan_ast_list_t *dst, int *counter) {
    if (!st) return;
    anf_ctx_t c = { g, dst, counter };
    switch (st->kind) {
    case AST_VAR_DECL:
        /* `T x = await E;` 已位于语句位置，直接保留原初始化式避免类型信息丢失 */
        if (st->var_decl.initializer &&
            st->var_decl.initializer->kind == AST_AWAIT_EXPR) {
            st->var_decl.initializer->await_expr.expr =
                anf_expr(&c, st->var_decl.initializer->await_expr.expr);
            anf_spill_await_receiver(&c, st->var_decl.initializer);
        } else {
            st->var_decl.initializer = anf_expr(&c, st->var_decl.initializer);
        }
        break;
    case AST_EXPR_STMT:
        /* 顶层表达式语句 `await E;` 仅递归正规化其内部嵌套 await */
        if (st->expr_stmt.expr && st->expr_stmt.expr->kind == AST_AWAIT_EXPR) {
            st->expr_stmt.expr->await_expr.expr =
                anf_expr(&c, st->expr_stmt.expr->await_expr.expr);
            anf_spill_await_receiver(&c, st->expr_stmt.expr);
        } else {
            st->expr_stmt.expr = anf_expr(&c, st->expr_stmt.expr);
        }
        break;
    case AST_RETURN_STMT:
        st->ret.value = anf_expr(&c, st->ret.value);
        break;
    case AST_THROW_STMT:
        st->throw_stmt.value = anf_expr(&c, st->throw_stmt.value);
        break;
    case AST_IF_STMT:
        st->if_stmt.cond = anf_expr(&c, st->if_stmt.cond);
        anf_normalize_body(g, &st->if_stmt.then_body, counter);
        anf_normalize_body(g, &st->if_stmt.else_body, counter);
        break;
    case AST_WHILE_STMT:
        /* 循环条件每轮求值：改写为循环体内首部语句，防止跨挂起点丢失状态 */
        if (anf_expr_contains_await(st->while_stmt.cond)) {
            zan_ast_node_t *blk = zan_ast_new(g->arena, AST_BLOCK, st->loc);
            zan_ast_list_init(&blk->block.stmts);
            st->while_stmt.cond = anf_loop_cond_guard(g, &blk->block.stmts,
                counter, &st->while_stmt.cond, &st->loc);
            anf_normalize_body(g, &st->while_stmt.body, counter);
            zan_ast_list_push(&blk->block.stmts, st->while_stmt.body, g->arena);
            st->while_stmt.body = blk;
        } else {
            anf_normalize_body(g, &st->while_stmt.body, counter);
        }
        break;
    case AST_DO_WHILE_STMT:
        /* do-while 循环条件含 await 时的块包装处理 */
        if (anf_expr_contains_await(st->while_stmt.cond)) {
            zan_ast_node_t *cblk = zan_ast_new(g->arena, AST_BLOCK, st->loc);
            zan_ast_list_init(&cblk->block.stmts);
            anf_ctx_t cc = { g, &cblk->block.stmts, counter };
            zan_ast_node_t *c_residual = anf_expr(&cc, st->while_stmt.cond);
            zan_ast_node_t *es = zan_ast_new(g->arena, AST_EXPR_STMT, st->loc);
            es->expr_stmt.expr = c_residual;
            zan_ast_list_push(&cblk->block.stmts, es, g->arena);
            st->while_stmt.cond = cblk;
            anf_normalize_body(g, &st->while_stmt.body, counter);
        } else {
            anf_normalize_body(g, &st->while_stmt.body, counter);
        }
        break;
    case AST_FOR_STMT: {
        bool cond_awaits = anf_expr_contains_await(st->for_stmt.cond);
        bool step_awaits = anf_expr_contains_await(st->for_stmt.step);
        if (cond_awaits) {
            zan_ast_node_t *blk = zan_ast_new(g->arena, AST_BLOCK, st->loc);
            zan_ast_list_init(&blk->block.stmts);
            st->for_stmt.cond = anf_loop_cond_guard(g, &blk->block.stmts,
                counter, &st->for_stmt.cond, &st->loc);
            anf_normalize_body(g, &st->for_stmt.body, counter);
            zan_ast_list_push(&blk->block.stmts, st->for_stmt.body, g->arena);
            st->for_stmt.body = blk;
        } else {
            anf_normalize_body(g, &st->for_stmt.body, counter);
        }
        if (step_awaits) {
            /* 将 for 循环 step 中的提升语句打包进 step 块 */
            zan_ast_node_t *sblk = zan_ast_new(g->arena, AST_BLOCK, st->loc);
            zan_ast_list_init(&sblk->block.stmts);
            anf_ctx_t c2 = { g, &sblk->block.stmts, counter };
            zan_ast_node_t *step = anf_expr(&c2, st->for_stmt.step);
            zan_ast_node_t *es = zan_ast_new(g->arena, AST_EXPR_STMT, st->loc);
            es->expr_stmt.expr = step;
            zan_ast_list_push(&sblk->block.stmts, es, g->arena);
            st->for_stmt.step = sblk;
        }
        break;
    }
    case AST_FOREACH_STMT:
        anf_normalize_body(g, &st->foreach_stmt.body, counter);
        break;
    case AST_LOCK_STMT:
        st->lock_stmt.expr = anf_expr(&c, st->lock_stmt.expr);
        anf_normalize_body(g, &st->lock_stmt.body, counter);
        break;
    case AST_CHECKED_STMT:
        anf_normalize_body(g, &st->checked_stmt.body, counter);
        break;
    case AST_BLOCK:
        anf_normalize_block(g, st, counter);
        break;
    case AST_TRY_STMT:
        anf_normalize_body(g, &st->try_stmt.try_body, counter);
        for (int i = 0; i < st->try_stmt.catches.count; i++)
            anf_normalize_body(g, &st->try_stmt.catches.items[i]->catch_clause.body, counter);
        anf_normalize_body(g, &st->try_stmt.finally_body, counter);
        break;
    case AST_SWITCH_STMT:
        for (int i = 0; i < st->switch_stmt.cases.count; i++)
            anf_normalize_body(g, &st->switch_stmt.cases.items[i]->switch_case.body, counter);
        break;
    default:
        break;
    }
}

/* 重构块语句列表，将提升出的临时变量声明插入在所属语句之前 */
void anf_normalize_block(zan_irgen_t *g, zan_ast_node_t *block, int *counter) {
    if (!block || block->kind != AST_BLOCK) return;
    zan_ast_list_t nl;
    zan_ast_list_init(&nl);
    for (int i = 0; i < block->block.stmts.count; i++) {
        zan_ast_node_t *st = block->block.stmts.items[i];
        anf_normalize_stmt(g, st, &nl, counter);
        zan_ast_list_push(&nl, st, g->arena);
    }
    block->block.stmts = nl;
}

