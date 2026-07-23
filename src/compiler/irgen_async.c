/* irgen_async.c -- async/await CPS lowering, await A-normal-form normalization and
 * the rc-element array escape analysis.
 *
 * Part of the irgen translation unit: this file is #include'd by irgen.c
 * (in a fixed order) and must not be compiled standalone. Splitting keeps
 * the single-TU static linkage while keeping each concern in its own file.
 */

/* ---- async/await CPS lowering helpers ---- */

/* Coerce an arbitrary scalar value to the i64 used by the frame result slot. */
static LLVMValueRef coerce_to_i64(zan_irgen_t *g, LLVMValueRef v) {
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef t = LLVMTypeOf(v);
    switch (LLVMGetTypeKind(t)) {
    case LLVMIntegerTypeKind: {
        unsigned bits = LLVMGetIntTypeWidth(t);
        if (bits < 64) return LLVMBuildSExt(g->builder, v, i64, "res.i64");
        if (bits > 64) return LLVMBuildTrunc(g->builder, v, i64, "res.i64");
        return v;
    }
    case LLVMPointerTypeKind:
        return LLVMBuildPtrToInt(g->builder, v, i64, "res.i64");
    case LLVMDoubleTypeKind:
        return LLVMBuildBitCast(g->builder, v, i64, "res.i64");
    case LLVMFloatTypeKind: {
        LLVMValueRef d = LLVMBuildFPExt(g->builder, v,
            LLVMDoubleTypeInContext(g->ctx), "res.f64");
        return LLVMBuildBitCast(g->builder, d, i64, "res.i64");
    }
    default:
        return LLVMConstInt(i64, 0, 0);
    }
}

/* Emit the completion epilogue of an async $resume body: store the result,
 * mark the frame done, wake a waiting awaiter (if any), then `ret void`.
 * `result_i64` may be NULL for a void async fn. Assumes the builder is
 * positioned at a block without a terminator.
 *
 * The awaiter-wake handshake schedules the frame that awaited us: when a
 * caller `await`s this task it stores itself + its own $resume into our
 * awaiter/awaiter_step header slots (see the await protocol). On completion we
 * re-enqueue that awaiter via zan_co_ready so the cooperative driver re-steps
 * it and it can read our result. A root (non-async) driver leaves awaiter null
 * and instead polls the result after zan_co_sched_run drains. */
static void emit_async_complete(zan_irgen_t *g, local_scope_t *locals, LLVMValueRef result_i64) {
    LLVMValueRef frame = g->current_async_frame;
    LLVMTypeRef ft = g->current_async_frame_type;
    LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);

    LLVMValueRef res_ptr = LLVMBuildStructGEP2(g->builder, ft, frame,
        ASYNC_FRAME_RESULT, "fr.result");
    LLVMBuildStore(g->builder, result_i64 ? result_i64 : LLVMConstInt(i64, 0, 0),
        res_ptr);

    LLVMValueRef done_ptr = LLVMBuildStructGEP2(g->builder, ft, frame,
        ASYNC_FRAME_DONE, "fr.done");
    LLVMBuildStore(g->builder, LLVMConstInt(i32, 1, 0), done_ptr);
    LLVMBuildStore(g->builder, LLVMConstInt(i32, -1, 1),
        LLVMBuildStructGEP2(g->builder, ft, frame, ASYNC_FRAME_STATE, "fr.state"));
    /* a `return` inside a try leaves that try's armed-handler count behind;
     * a completed frame has no live handlers, so reset it for the unwinder */
    LLVMBuildStore(g->builder, LLVMConstInt(i32, 0, 0),
        LLVMBuildStructGEP2(g->builder, ft, frame, ASYNC_FRAME_HCOUNT, "fr.hc"));

    emit_release_owned_locals(g, locals);

    /* if (awaiter != null) zan_co_ready(awaiter, awaiter_step); */
    LLVMValueRef aw_ptr = LLVMBuildStructGEP2(g->builder, ft, frame,
        ASYNC_FRAME_AWAITER, "fr.awaiter");
    LLVMValueRef awaiter = LLVMBuildLoad2(g->builder, i8ptr, aw_ptr, "awaiter");
    LLVMValueRef has_awaiter = LLVMBuildICmp(g->builder, LLVMIntNE, awaiter,
        LLVMConstNull(i8ptr), "has.awaiter");
    LLVMValueRef fn = g->current_fn;
    LLVMBasicBlockRef wake_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "co.wake");
    LLVMBasicBlockRef ret_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "co.ret");
    LLVMBuildCondBr(g->builder, has_awaiter, wake_bb, ret_bb);

    LLVMPositionBuilderAtEnd(g->builder, wake_bb);
    LLVMValueRef aws_ptr = LLVMBuildStructGEP2(g->builder, ft, frame,
        ASYNC_FRAME_AWAITER_STEP, "fr.awaiter.step");
    LLVMValueRef aw_step = LLVMBuildLoad2(g->builder, g->co_step_ptr, aws_ptr, "awaiter.step");
    LLVMValueRef wake_args[] = { awaiter, aw_step };
    zan_call2(g->builder, g->rt_co_ready_type, g->rt_co_ready, wake_args, 2, "");
    LLVMBuildBr(g->builder, ret_bb);

    LLVMPositionBuilderAtEnd(g->builder, ret_bb);
    LLVMBuildRetVoid(g->builder);
}

/* Save all frame-resident slots (params + named scalar locals) from their
 * stack allocas back into the heap frame. Emitted right before a suspend so
 * live values survive across the `ret void`. */
static void emit_async_save_slots(zan_irgen_t *g) {
    LLVMTypeRef ft = g->current_async_frame_type;
    LLVMValueRef frame = g->current_async_frame;
    for (int i = 0; i < g->current_async_slot_count; i++) {
        LLVMValueRef v = LLVMBuildLoad2(g->builder, g->current_async_slots[i].llvm,
            g->current_async_slots[i].slot_alloca, "sv");
        LLVMValueRef slot = LLVMBuildStructGEP2(g->builder, ft, frame,
            (unsigned)g->current_async_slots[i].frame_index, "sv.slot");
        LLVMBuildStore(g->builder, v, slot);
    }
}

/* Reload all frame-resident slots from the heap frame into their stack allocas.
 * Emitted at the top of each state block (co.start and every resume-k) so the
 * body reads live values that survived a suspension. */
static void emit_async_reload_slots(zan_irgen_t *g) {
    LLVMTypeRef ft = g->current_async_frame_type;
    LLVMValueRef frame = g->current_async_frame;
    for (int i = 0; i < g->current_async_slot_count; i++) {
        LLVMValueRef slot = LLVMBuildStructGEP2(g->builder, ft, frame,
            (unsigned)g->current_async_slots[i].frame_index, "rl.slot");
        LLVMValueRef v = LLVMBuildLoad2(g->builder, g->current_async_slots[i].llvm,
            slot, "rl");
        LLVMBuildStore(g->builder, v, g->current_async_slots[i].slot_alloca);
    }
}

/* ---- await A-normal-form (ANF) normalization ----
 *
 * S3 keeps a value alive across a suspension only when it is a named scalar
 * local (those live in the heap frame and are reloaded at every state block).
 * An intermediate SSA temp produced *before* an await and consumed *after* it
 * does not survive: the resume-k block is entered from the entry switch, so a
 * value computed in the pre-suspend block does not dominate it and LLVM rejects
 * the module ("instruction does not dominate all uses"). This shows up for
 * compound / multiple awaits, e.g. `c + await f()`, `await a() + await b()`,
 * or `h(await a(), await b())`.
 *
 * This pass rewrites each async body into A-normal form for awaits: every
 * `await E` in a linearly-evaluated position becomes its own preceding
 * statement `int $awN = await E;` and the original occurrence is replaced by a
 * reference to `$awN`. Because `$awN` is a named scalar local it is made
 * frame-resident by async_scan and reloaded after the suspension, so no value
 * crosses a suspend point in a register. Awaits inside short-circuit (`&&`,
 * `||`) and conditional (`?:`) operands, and inside loop conditions/steps, are
 * left in place (their existing control-flow lowering handles them and hoisting
 * would change evaluation semantics). */

typedef struct {
    zan_irgen_t    *g;
    zan_ast_list_t *out;  /* hoisted statements, appended in evaluation order */
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
        return false;
    }
    case AST_CAST_EXPR: return anf_expr_contains_await(e->cast.expr);
    case AST_IS_EXPR:
    case AST_AS_EXPR:  return anf_expr_contains_await(e->type_test.expr);
    default: return false;
    }
}

/* A side-effecting operand (a call, assignment, or ++/--) evaluated *before* an
 * await in the same operand list would be reordered to run *after* the await if
 * we only hoist the await (it stays in the residual). Detect that to reject it
 * with a clear message instead of silently changing evaluation order. Awaits
 * themselves are hoisted in order, so they are not counted here. */
static bool anf_expr_has_side_effect(zan_ast_node_t *e) {
    if (!e) return false;
    switch (e->kind) {
    case AST_AWAIT_EXPR: return false; /* hoisted separately, order preserved */
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

/* If operand `before` (evaluated first) has side effects and a later operand
 * `after` contains an await, hoisting only the await reorders them. Flag it. */
static void anf_check_order(anf_ctx_t *c, zan_ast_node_t *before, zan_ast_node_t *after) {
    if (after && before && anf_expr_contains_await(after) &&
        anf_expr_has_side_effect(before)) {
        zan_diag_emit(c->g->diag, DIAG_ERROR, before->loc,
            "async: an expression with side effects is evaluated before an "
            "await in the same expression; assign it to a local first");
    }
}

/* Hoist `await E` into `int $awN = await E;` and return a reference to $awN. */
static zan_ast_node_t *anf_hoist_await(anf_ctx_t *c, zan_ast_node_t *aw) {
    /* normalize any nested awaits inside the awaited expression first */
    aw->await_expr.expr = anf_expr(c, aw->await_expr.expr);

    char buf[32];
    int len = snprintf(buf, sizeof buf, "$aw%d", (*c->counter)++);
    char *nm = (char *)zan_arena_alloc(c->g->arena, (size_t)len + 1);
    memcpy(nm, buf, (size_t)len + 1);
    zan_istr_t name = { nm, (uint32_t)len };

    zan_ast_node_t *tref = zan_ast_new(c->g->arena, AST_TYPE_REF, aw->loc);
    static const char intname[] = "int";
    tref->type_ref.name = (zan_istr_t){ intname, 3 };

    zan_ast_node_t *vd = zan_ast_new(c->g->arena, AST_VAR_DECL, aw->loc);
    vd->var_decl.name = name;
    vd->var_decl.type = tref;
    vd->var_decl.initializer = aw;
    zan_ast_list_push(c->out, vd, c->g->arena);

    zan_ast_node_t *id = zan_ast_new(c->g->arena, AST_IDENTIFIER, aw->loc);
    id->ident.name = name;
    return id;
}

/* Recursively lift awaits out of a linearly-evaluated expression, appending
 * hoisted `$awN` declarations to c->out in evaluation order and returning the
 * residual expression (which no longer contains any hoistable await). */
static zan_ast_node_t *anf_expr(anf_ctx_t *c, zan_ast_node_t *e) {
    if (!e) return e;
    switch (e->kind) {
    case AST_AWAIT_EXPR:
        return anf_hoist_await(c, e);
    case AST_BINARY:
    case AST_ASSIGNMENT:
        if (e->binary.op == TK_AMP_AMP || e->binary.op == TK_PIPE_PIPE) {
            /* short-circuit: only the left operand is unconditional. */
            e->binary.left = anf_expr(c, e->binary.left);
            return e;
        }
        anf_check_order(c, e->binary.left, e->binary.right);
        e->binary.left  = anf_expr(c, e->binary.left);
        e->binary.right = anf_expr(c, e->binary.right);
        return e;
    case AST_UNARY:
    case AST_POSTFIX_UNARY:
        e->unary.operand = anf_expr(c, e->unary.operand);
        return e;
    case AST_CALL:
        for (int i = 0; i < e->call.args.count; i++) {
            for (int j = i + 1; j < e->call.args.count; j++)
                anf_check_order(c, e->call.args.items[i], e->call.args.items[j]);
        }
        e->call.callee = anf_expr(c, e->call.callee);
        for (int i = 0; i < e->call.args.count; i++)
            e->call.args.items[i] = anf_expr(c, e->call.args.items[i]);
        return e;
    case AST_MEMBER_ACCESS:
        e->member.object = anf_expr(c, e->member.object);
        return e;
    case AST_INDEX:
        anf_check_order(c, e->index.object, e->index.index);
        e->index.object = anf_expr(c, e->index.object);
        e->index.index  = anf_expr(c, e->index.index);
        return e;
    case AST_CONDITIONAL:
        /* only the guard is unconditional; leave branch awaits in place. */
        e->conditional.cond = anf_expr(c, e->conditional.cond);
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
        return e;
    case AST_IS_EXPR:
    case AST_AS_EXPR:
        e->type_test.expr = anf_expr(c, e->type_test.expr);
        return e;
    default:
        return e;
    }
}

static void anf_normalize_block(zan_irgen_t *g, zan_ast_node_t *block, int *counter);

/* Does a statement subtree contain any await? Used to decide whether a
 * single-statement body must be wrapped in a block for hoisting. */
static bool anf_stmt_contains_await(zan_ast_node_t *st) {
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
    case AST_BLOCK: {
        for (int i = 0; i < st->block.stmts.count; i++)
            if (anf_stmt_contains_await(st->block.stmts.items[i])) return true;
        return false;
    }
    default: return false;
    }
}

/* Ensure `*slot` is an AST_BLOCK so hoisted statements can be inserted before
 * the awaits it contains, then normalize it. Used for single-statement bodies
 * (e.g. `if (c) return await f();`). */
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

/* Normalize one statement, appending any hoisted declarations to `dst` (in
 * evaluation order) *before* the statement is pushed by the caller. Nested
 * statement bodies are normalized recursively. */
static void anf_normalize_stmt(zan_irgen_t *g, zan_ast_node_t *st,
                               zan_ast_list_t *dst, int *counter) {
    if (!st) return;
    anf_ctx_t c = { g, dst, counter };
    switch (st->kind) {
    case AST_VAR_DECL:
        st->var_decl.initializer = anf_expr(&c, st->var_decl.initializer);
        break;
    case AST_EXPR_STMT:
        /* A bare `await E;` is already at statement position, so keep the await
         * in place (only normalize awaits nested inside E) instead of hoisting
         * it into an `int $awN` temp. Hoisting would erase the real result type
         * (the temp is always typed `int`), so a discarded owned rc result
         * (string/object) would never be released and would leak. Emitted
         * directly, the expr-statement discard path releases it by its true
         * type. */
        if (st->expr_stmt.expr && st->expr_stmt.expr->kind == AST_AWAIT_EXPR) {
            st->expr_stmt.expr->await_expr.expr =
                anf_expr(&c, st->expr_stmt.expr->await_expr.expr);
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
        /* condition is evaluated once → safe to hoist */
        st->if_stmt.cond = anf_expr(&c, st->if_stmt.cond);
        anf_normalize_body(g, &st->if_stmt.then_body, counter);
        anf_normalize_body(g, &st->if_stmt.else_body, counter);
        break;
    case AST_WHILE_STMT:
    case AST_DO_WHILE_STMT:
        /* condition is re-evaluated each iteration → must NOT hoist it out */
        anf_normalize_body(g, &st->while_stmt.body, counter);
        break;
    case AST_FOR_STMT:
        anf_normalize_body(g, &st->for_stmt.body, counter);
        break;
    case AST_FOREACH_STMT:
        anf_normalize_body(g, &st->foreach_stmt.body, counter);
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

/* Rebuild a block's statement list, inserting hoisted `$awN` declarations
 * before each statement that needed them. */
static void anf_normalize_block(zan_irgen_t *g, zan_ast_node_t *block, int *counter) {
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

/* A named scalar local of an async method that must live in the heap frame so
 * its value survives across suspensions. `frame_index` is assigned when the
 * frame struct is laid out (params first, then these locals). */
typedef struct {
    zan_istr_t   name;
    LLVMTypeRef  llvm;        /* slot element type */
    zan_type_t  *ztype;      /* zan type (for identifier load typing) */
    int          frame_index;
} async_local_t;

typedef struct {
    zan_irgen_t   *g;
    int            await_count;
    async_local_t *locals;
    int            local_count;
    int            local_cap;
} async_scan_t;

static bool async_type_is_scalar(LLVMTypeRef t) {
    switch (LLVMGetTypeKind(t)) {
    case LLVMIntegerTypeKind:
    case LLVMFloatTypeKind:
    case LLVMDoubleTypeKind:
        return true;
    default:
        return false;
    }
}

/* A local must be frame-resident (saved to / reloaded from the heap frame
 * around each suspension) if it can be live across an await. Scalars and any
 * pointer-shaped value (string, array, List/Dict, class instances — all lowered
 * to a pointer here) qualify; the save/reload just relocates the bits and never
 * touches a refcount, so ARC ownership (release on throw-cleanup / scope exit,
 * driven off the reloaded alloca) is unaffected. Aggregates passed by value are
 * left alone (they do not cross suspends in the current lowering). */
static bool async_type_is_frame_resident(LLVMTypeRef t) {
    return async_type_is_scalar(t) || LLVMGetTypeKind(t) == LLVMPointerTypeKind;
}

static void async_scan_expr(async_scan_t *s, zan_ast_node_t *e);
static void async_scan_stmt(async_scan_t *s, zan_ast_node_t *st);

static void async_scan_add_local(async_scan_t *s, zan_istr_t name, LLVMTypeRef llvm, zan_type_t *zt) {
    for (int i = 0; i < s->local_count; i++) {
        if (s->locals[i].name.len == name.len &&
            memcmp(s->locals[i].name.str, name.str, name.len) == 0) {
            return; /* dedup by name (flat scope) */
        }
    }
    if (s->local_count >= s->local_cap) {
        int nc = s->local_cap ? s->local_cap * 2 : 8;
        async_local_t *g2 = (async_local_t *)zan_arena_alloc(s->g->arena,
            sizeof(async_local_t) * (size_t)nc);
        if (s->local_count > 0) memcpy(g2, s->locals, sizeof(async_local_t) * (size_t)s->local_count);
        s->locals = g2;
        s->local_cap = nc;
    }
    s->locals[s->local_count].name = name;
    s->locals[s->local_count].llvm = llvm;
    s->locals[s->local_count].ztype = zt;
    s->locals[s->local_count].frame_index = -1;
    s->local_count++;
}

/* Walk expressions to count await points (state-machine transitions). */
static void async_scan_expr(async_scan_t *s, zan_ast_node_t *e) {
    if (!e) return;
    switch (e->kind) {
    case AST_AWAIT_EXPR:
        s->await_count++;
        async_scan_expr(s, e->await_expr.expr);
        break;
    case AST_BINARY:
    case AST_ASSIGNMENT:
        async_scan_expr(s, e->binary.left);
        async_scan_expr(s, e->binary.right);
        break;
    case AST_UNARY:
    case AST_POSTFIX_UNARY:
        async_scan_expr(s, e->unary.operand);
        break;
    case AST_CALL:
        async_scan_expr(s, e->call.callee);
        for (int i = 0; i < e->call.args.count; i++)
            async_scan_expr(s, e->call.args.items[i]);
        break;
    case AST_MEMBER_ACCESS:
        async_scan_expr(s, e->member.object);
        break;
    case AST_INDEX:
        async_scan_expr(s, e->index.object);
        async_scan_expr(s, e->index.index);
        break;
    case AST_CONDITIONAL:
        async_scan_expr(s, e->conditional.cond);
        async_scan_expr(s, e->conditional.then_expr);
        async_scan_expr(s, e->conditional.else_expr);
        break;
    case AST_NEW_EXPR:
        for (int i = 0; i < e->new_expr.args.count; i++)
            async_scan_expr(s, e->new_expr.args.items[i]);
        break;
    case AST_CAST_EXPR:
        async_scan_expr(s, e->cast.expr);
        break;
    case AST_IS_EXPR:
    case AST_AS_EXPR:
        async_scan_expr(s, e->type_test.expr);
        break;
    default:
        break;
    }
}

/* Walk statements to collect named scalar locals (which must live in the frame)
 * and count await points anywhere in the body. */
static void async_scan_stmt(async_scan_t *s, zan_ast_node_t *st) {
    if (!st) return;
    switch (st->kind) {
    case AST_BLOCK:
        for (int i = 0; i < st->block.stmts.count; i++)
            async_scan_stmt(s, st->block.stmts.items[i]);
        break;
    case AST_VAR_DECL:
        if (st->var_decl.type) {
            zan_type_t *t = zan_binder_resolve_type(s->g->binder, st->var_decl.type);
            if (t) {
                LLVMTypeRef lt = map_type(s->g, t);
                if (async_type_is_frame_resident(lt))
                    async_scan_add_local(s, st->var_decl.name, lt, t);
            }
        }
        async_scan_expr(s, st->var_decl.initializer);
        break;
    case AST_EXPR_STMT:
        async_scan_expr(s, st->expr_stmt.expr);
        break;
    case AST_RETURN_STMT:
        async_scan_expr(s, st->ret.value);
        break;
    case AST_IF_STMT:
        async_scan_expr(s, st->if_stmt.cond);
        async_scan_stmt(s, st->if_stmt.then_body);
        async_scan_stmt(s, st->if_stmt.else_body);
        break;
    case AST_WHILE_STMT:
    case AST_DO_WHILE_STMT:
        async_scan_expr(s, st->while_stmt.cond);
        async_scan_stmt(s, st->while_stmt.body);
        break;
    case AST_FOR_STMT:
        async_scan_stmt(s, st->for_stmt.init);
        async_scan_expr(s, st->for_stmt.cond);
        async_scan_expr(s, st->for_stmt.step);
        async_scan_stmt(s, st->for_stmt.body);
        break;
    case AST_FOREACH_STMT:
        async_scan_expr(s, st->foreach_stmt.collection);
        async_scan_stmt(s, st->foreach_stmt.body);
        break;
    case AST_THROW_STMT:
        async_scan_expr(s, st->throw_stmt.value);
        break;
    case AST_TRY_STMT:
        async_scan_stmt(s, st->try_stmt.try_body);
        for (int i = 0; i < st->try_stmt.catches.count; i++)
            async_scan_stmt(s, st->try_stmt.catches.items[i]->catch_clause.body);
        async_scan_stmt(s, st->try_stmt.finally_body);
        break;
    case AST_SWITCH_STMT:
        async_scan_expr(s, st->switch_stmt.expr);
        for (int i = 0; i < st->switch_stmt.cases.count; i++)
            async_scan_stmt(s, st->switch_stmt.cases.items[i]->switch_case.body);
        break;
    default:
        break;
    }
}

/* ---- rc-element array escape analysis ----
 * A `new T[n]` array of rc elements is released element-by-element at scope
 * exit. That is only sound when the buffer does not outlive the scope, i.e. it
 * never escapes: used only as an index base `a[i]` or a borrowed member read
 * `a.Length`. Any other use of the bare identifier (return, assignment RHS,
 * call argument, collection/field store, ...) shares the pointer, so we skip
 * the release for it (leak rather than risk a double-free). */
static int arr_ident_named(zan_ast_node_t *e, zan_istr_t nm) {
    return e && e->kind == AST_IDENTIFIER &&
           e->ident.name.len == nm.len &&
           (nm.len == 0 || memcmp(e->ident.name.str, nm.str, (size_t)nm.len) == 0);
}
static void arr_escape_stmt(zan_istr_t nm, zan_ast_node_t *st, int *esc);
static void arr_escape_expr(zan_istr_t nm, zan_ast_node_t *e, int *esc) {
    if (!e || *esc) return;
    switch (e->kind) {
    case AST_IDENTIFIER:
        if (arr_ident_named(e, nm)) *esc = 1;
        break;
    case AST_INDEX:
        if (!arr_ident_named(e->index.object, nm))
            arr_escape_expr(nm, e->index.object, esc);
        arr_escape_expr(nm, e->index.index, esc);
        break;
    case AST_MEMBER_ACCESS:
        if (!arr_ident_named(e->member.object, nm))
            arr_escape_expr(nm, e->member.object, esc);
        break;
    case AST_BINARY:
    case AST_ASSIGNMENT:
        arr_escape_expr(nm, e->binary.left, esc);
        arr_escape_expr(nm, e->binary.right, esc);
        break;
    case AST_UNARY:
    case AST_POSTFIX_UNARY:
        arr_escape_expr(nm, e->unary.operand, esc);
        break;
    case AST_CALL:
        arr_escape_expr(nm, e->call.callee, esc);
        for (int i = 0; i < e->call.args.count; i++)
            arr_escape_expr(nm, e->call.args.items[i], esc);
        break;
    case AST_CONDITIONAL:
        arr_escape_expr(nm, e->conditional.cond, esc);
        arr_escape_expr(nm, e->conditional.then_expr, esc);
        arr_escape_expr(nm, e->conditional.else_expr, esc);
        break;
    case AST_NEW_EXPR:
        for (int i = 0; i < e->new_expr.args.count; i++)
            arr_escape_expr(nm, e->new_expr.args.items[i], esc);
        break;
    case AST_CAST_EXPR:
        arr_escape_expr(nm, e->cast.expr, esc);
        break;
    case AST_IS_EXPR:
    case AST_AS_EXPR:
        arr_escape_expr(nm, e->type_test.expr, esc);
        break;
    case AST_AWAIT_EXPR:
        arr_escape_expr(nm, e->await_expr.expr, esc);
        break;
    default:
        break;
    }
}
static void arr_escape_stmt(zan_istr_t nm, zan_ast_node_t *st, int *esc) {
    if (!st || *esc) return;
    switch (st->kind) {
    case AST_BLOCK:
        for (int i = 0; i < st->block.stmts.count; i++)
            arr_escape_stmt(nm, st->block.stmts.items[i], esc);
        break;
    case AST_VAR_DECL:
        arr_escape_expr(nm, st->var_decl.initializer, esc);
        break;
    case AST_EXPR_STMT:
        arr_escape_expr(nm, st->expr_stmt.expr, esc);
        break;
    case AST_RETURN_STMT:
        arr_escape_expr(nm, st->ret.value, esc);
        break;
    case AST_IF_STMT:
        arr_escape_expr(nm, st->if_stmt.cond, esc);
        arr_escape_stmt(nm, st->if_stmt.then_body, esc);
        arr_escape_stmt(nm, st->if_stmt.else_body, esc);
        break;
    case AST_WHILE_STMT:
    case AST_DO_WHILE_STMT:
        arr_escape_expr(nm, st->while_stmt.cond, esc);
        arr_escape_stmt(nm, st->while_stmt.body, esc);
        break;
    case AST_FOR_STMT:
        arr_escape_stmt(nm, st->for_stmt.init, esc);
        arr_escape_expr(nm, st->for_stmt.cond, esc);
        arr_escape_expr(nm, st->for_stmt.step, esc);
        arr_escape_stmt(nm, st->for_stmt.body, esc);
        break;
    case AST_FOREACH_STMT:
        arr_escape_expr(nm, st->foreach_stmt.collection, esc);
        arr_escape_stmt(nm, st->foreach_stmt.body, esc);
        break;
    case AST_THROW_STMT:
        arr_escape_expr(nm, st->throw_stmt.value, esc);
        break;
    case AST_TRY_STMT:
        arr_escape_stmt(nm, st->try_stmt.try_body, esc);
        for (int i = 0; i < st->try_stmt.catches.count; i++)
            arr_escape_stmt(nm, st->try_stmt.catches.items[i]->catch_clause.body, esc);
        arr_escape_stmt(nm, st->try_stmt.finally_body, esc);
        break;
    case AST_SWITCH_STMT:
        arr_escape_expr(nm, st->switch_stmt.expr, esc);
        for (int i = 0; i < st->switch_stmt.cases.count; i++)
            arr_escape_stmt(nm, st->switch_stmt.cases.items[i]->switch_case.body, esc);
        break;
    default:
        break;
    }
}
static int rc_array_local_escapes(zan_irgen_t *g, zan_istr_t nm) {
    int esc = 0;
    if (g->current_fn_body) arr_escape_stmt(nm, g->current_fn_body, &esc);
    else esc = 1; /* unknown body: conservative */
    return esc;
}
