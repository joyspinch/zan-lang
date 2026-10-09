/* 内部辅助实现 */

static void emit_release_static_rc_fields(zan_irgen_t *g, zan_ast_node_t *unit);
/* 内部辅助实现 */
static bool async_slot_type_compatible(zan_type_t *a, zan_type_t *b) {
    if (a == b) return true;
    if (!a || !b) return false;
    int depth = 0;
    while (a && b && depth < 32) {
        if (a == b) return true;
        if (a->kind != b->kind) return false;
        if (a->name.len != b->name.len ||
            (a->name.len && memcmp(a->name.str, b->name.str,
                                   (size_t)a->name.len) != 0))
            return false;
        if (a->type_arg_count != b->type_arg_count) return false;
        for (int i = 0; i < a->type_arg_count; i++)
            if (!async_slot_type_compatible(a->type_args[i], b->type_args[i]))
                return false;
        a = a->element_type;
        b = b->element_type;
        depth++;
    }
    return a == b;
}

static bool stmt_contains_label(zan_ast_node_t *st, zan_istr_t name) {
    if (!st) return false;
    switch (st->kind) {
    case AST_LABEL_STMT:
        return st->ident.name.len == name.len &&
            memcmp(st->ident.name.str, name.str, (size_t)name.len) == 0;
    case AST_BLOCK:
        for (int i = 0; i < st->block.stmts.count; i++)
            if (stmt_contains_label(st->block.stmts.items[i], name)) return true;
        break;
    case AST_IF_STMT:
        return stmt_contains_label(st->if_stmt.then_body, name) ||
            stmt_contains_label(st->if_stmt.else_body, name);
    case AST_WHILE_STMT: case AST_DO_WHILE_STMT:
        return stmt_contains_label(st->while_stmt.body, name);
    case AST_FOR_STMT:
        return stmt_contains_label(st->for_stmt.body, name);
    case AST_FOREACH_STMT:
        return stmt_contains_label(st->foreach_stmt.body, name);
    case AST_LOCK_STMT:
        return stmt_contains_label(st->lock_stmt.body, name);
    case AST_CHECKED_STMT:
        return stmt_contains_label(st->checked_stmt.body, name);
    case AST_SWITCH_STMT:
        for (int i = 0; i < st->switch_stmt.cases.count; i++)
            if (stmt_contains_label(st->switch_stmt.cases.items[i]->switch_case.body, name))
                return true;
        break;
    case AST_TRY_STMT:
        if (stmt_contains_label(st->try_stmt.try_body, name) ||
            stmt_contains_label(st->try_stmt.finally_body, name)) return true;
        for (int i = 0; i < st->try_stmt.catches.count; i++)
            if (stmt_contains_label(st->try_stmt.catches.items[i]->catch_clause.body, name))
                return true;
        break;
    default: break;
    }
    return false;
}

static zan_irgen_pending_scope_t *goto_label_owner(zan_irgen_t *g, zan_istr_t name) {
    for (zan_irgen_pending_scope_t *scope = g->pending.scope; scope; scope = scope->parent)
        if (stmt_contains_label(scope->body, name)) return scope;
    return NULL;
}

/* Find or create the label record for (current function, body copy, name) */
static int irgen_goto_label_idx(zan_irgen_t *g, zan_istr_t name) {
    LLVMValueRef fn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder));
    zan_irgen_pending_scope_t *owner = goto_label_owner(g, name);
    for (int i = 0; i < g->goto_label_count; i++) {
        if (g->goto_labels[i].fn == fn && g->goto_labels[i].label_owner == owner &&
            g->goto_labels[i].name.len == name.len &&
            memcmp(g->goto_labels[i].name.str, name.str,
                   (size_t)name.len) == 0)
            return i;
    }
    if (!ZAN_TAB_ENSURE(g->goto_labels, g->goto_label_count,
                        g->goto_label_cap, 64)) return -1;
    LLVMBasicBlockRef bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "label");
    memset(&g->goto_labels[g->goto_label_count], 0,
           sizeof(g->goto_labels[0]));
    g->goto_labels[g->goto_label_count].label_owner = owner;
    g->goto_labels[g->goto_label_count].name = name;
    g->goto_labels[g->goto_label_count].fn = fn;
    g->goto_labels[g->goto_label_count].bb = bb;
    g->goto_label_count++;
    return g->goto_label_count - 1;
}

/* 内部辅助实现 */
static int irgen_goto_count_owned_locals(local_scope_t *locals) {
    int owned = 0;
    for (int i = 0; i < locals->count; i++) {
        local_var_t *v = &locals->vars[i];
        if (v->arc_owned || v->struct_rc || v->box_cell || v->obj_rc_flag)
            owned++;
    }
    return owned;
}

/* 内部辅助逻辑 */
static LLVMValueRef get_eh_hook_fn(zan_irgen_t *g, const char *name) {
    LLVMValueRef f = LLVMGetNamedFunction(g->mod, name);
    if (f) return f;
    LLVMTypeRef fty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), NULL, 0, 0);
    f = LLVMAddFunction(g->mod, name, fty);
    /* 内部辅助逻辑 */
    LLVMSetLinkage(f, LLVMInternalLinkage);
    LLVMBasicBlockRef saved = LLVMGetInsertBlock(g->builder);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, f, "entry");
    LLVMPositionBuilderAtEnd(g->builder, entry);
    LLVMBuildRetVoid(g->builder);
    if (saved) LLVMPositionBuilderAtEnd(g->builder, saved);
    return f;
}

/* Calls one of the debugger hooks above. */
static void emit_eh_hook_call(zan_irgen_t *g, const char *name) {
    if (!g->emit_debug) return;
    LLVMValueRef f = get_eh_hook_fn(g, name);
    LLVMTypeRef fty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), NULL, 0, 0);
    zan_call2(g->builder, fty, f, NULL, 0, "");
}

static void emit_stmt(zan_irgen_t *g, zan_ast_node_t *stmt, local_scope_t *locals);

/* Bind a switch case's pattern variable (`case T x:`) to the discriminant */
static void emit_switch_pattern_bind(zan_irgen_t *g, local_scope_t *locals,
                                     zan_ast_node_t *sc, LLVMValueRef switch_val) {
    if (!sc->switch_case.type_pattern || sc->switch_case.var_name.len == 0) return;
    zan_type_t *pt = resolve_type_ctx(g, sc->switch_case.type_pattern);
    if (!pt) return;
    LLVMTypeRef ptll = map_type(g, pt);
    LLVMValueRef cv = switch_val;
    if (LLVMTypeOf(cv) != ptll &&
        LLVMGetTypeKind(ptll) == LLVMPointerTypeKind &&
        LLVMGetTypeKind(LLVMTypeOf(cv)) == LLVMPointerTypeKind)
        cv = LLVMBuildBitCast(g->builder, cv, ptll, "cpat.cast");

    int captured = local_is_lambda_captured(g, locals, g->current_fn_body, sc);
    if (captured && (pt->kind == TYPE_OBJECT || pt->kind == TYPE_TYPE_PARAM)) {
        zan_diag_emit(g->diag, DIAG_ERROR, sc->loc,
            "cannot capture pattern variable '%.*s' of type '%.*s': "
            "its runtime ownership is not representable yet",
            (int)sc->switch_case.var_name.len, sc->switch_case.var_name.str,
            (int)pt->name.len, pt->name.str ? pt->name.str : "");
        return;
    }

    if (captured) {
        pattern_binding_t *pb = local_find_pattern_binding(locals, sc);
        if (!pb || !pb->cell) {
            pb = pb ? pb : local_add_pattern_binding(locals, sc);
            if (!pb) return;
            pb->type = pt;
            pb->payload = ptll;
            int aggregate_rc = pt->kind == TYPE_STRUCT &&
                type_contains_collection_rc(g, pt, 0);
            if (aggregate_rc)
                emit_collection_value_retain(g, pt, cv, 0);
            else if (is_rc_managed_type(pt) &&
                     LLVMGetTypeKind(ptll) == LLVMPointerTypeKind)
                emit_rc_retain_for_type(g, pt, cv);
            pb->cell = emit_box_cell(g, sc->loc, ptll, pt, cv);
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
            pb->owner_slot = emit_entry_alloca(g, i8ptr, "cpat.owner");
            LLVMValueRef tagged = LLVMBuildIntToPtr(g->builder,
                LLVMBuildOr(g->builder,
                    LLVMBuildPtrToInt(g->builder, pb->cell, i64, "cpat.i"),
                    LLVMConstInt(i64, ZAN_CLOSURE_TAG, 0), "cpat.tag"),
                i8ptr, "cpat.owner.v");
            LLVMBuildStore(g->builder, tagged, pb->owner_slot);
            if (!g->current_async_frame) {
                emit_eh_tmp_push_slot(g, pb->owner_slot, ZAN_EH_SLOT_DLG);
                pb->eh_slot = 1;
            }
        }
        local_var_t *prior = local_find_binding_decl(locals, sc);
        LLVMValueRef slot = box_value_ptr(g, pb->cell, ptll);
        local_add(locals, sc->switch_case.var_name, slot, pt);
        local_var_t *v = &locals->vars[locals->count - 1];
        v->binding_decl = sc;
        v->box_cell = pb->cell;
        v->box_owner_slot = pb->owner_slot;
        v->box_owned = prior == NULL;
        v->eh_slot = v->box_owned ? pb->eh_slot : 0;
        if (is_rc_managed_type(pt)) v->arc_owned = 1;
        if (pt->kind == TYPE_STRUCT && type_contains_collection_rc(g, pt, 0))
            v->struct_rc = 1;
        return;
    }

    LLVMValueRef slot = emit_entry_alloca(g, ptll, "cpat");
    zan_store_fit(g, LLVMConstNull(ptll), slot);
    zan_store_fit(g, cv, slot);
    local_add(locals, sc->switch_case.var_name, slot, pt);
    locals->vars[locals->count - 1].binding_decl = sc;
}

/* Release a captured pattern cell on a failed guard */
static void emit_switch_pattern_fail_release(zan_irgen_t *g,
                                             local_scope_t *locals,
                                             zan_ast_node_t *sc) {
    local_var_t *v = local_find_binding_decl(locals, sc);
    if (!v || !v->box_cell || !v->box_owned) return;
    /* 内部辅助逻辑 */
    release_boxed_local(g, v);
}

/* 内部辅助实现 */
/* Release the monitor a `lock (obj)` took */
static void emit_monitor_exit(zan_irgen_t *g, LLVMValueRef obj_slot) {
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef mon_ty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx),
                                          &i8ptr, 1, 0);
    LLVMValueRef exit_fn = LLVMGetNamedFunction(g->mod, "zan_monitor_exit");
    if (!exit_fn) exit_fn = LLVMAddFunction(g->mod, "zan_monitor_exit", mon_ty);
    LLVMValueRef obj = LLVMBuildLoad2(g->builder, i8ptr, obj_slot, "lock.obj");
    zan_call2(g->builder, mon_ty, exit_fn, &obj, 1, "");
}

/* 内部辅助实现 */
static void emit_eh_disarm_from(zan_irgen_t *g, int base) {
    if (base < g->eh_armed_base) base = g->eh_armed_base;
    if (g->eh_armed_count <= base) return;
    LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
    LLVMValueRef top_g, bufs_g, exc_g;
    get_eh_globals(g, &top_g, &bufs_g, &exc_g);
    if (g->current_async_frame) {
        int hc = base - g->eh_armed_base;
        LLVMValueRef entry = LLVMBuildLoad2(g->builder, i32t,
            g->current_async_eh_entry, "eh.exit.entry");
        zan_store_fit(g, LLVMBuildAdd(g->builder, entry,
            LLVMConstInt(i32t, (unsigned)hc + 1, 0), "eh.exit.top"), top_g);
        zan_store_fit(g, LLVMConstInt(i32t, (unsigned)hc, 0),
            LLVMBuildStructGEP2(g->builder, g->current_async_frame_type,
                g->current_async_frame, ASYNC_FRAME_HCOUNT, "eh.exit.hc"));
    } else {
        LLVMValueRef ot = LLVMBuildLoad2(g->builder, i32t,
            g->eh_armed[base].old_top_slot, "eh.old.exit");
        zan_store_fit(g, ot, top_g);
    }
}

/* 内部辅助逻辑 */
static zan_irgen_pending_scope_t *pending_scope_common(
        zan_irgen_pending_scope_t *a, zan_irgen_pending_scope_t *b) {
    for (zan_irgen_pending_scope_t *p = a; p; p = p->parent)
        for (zan_irgen_pending_scope_t *q = b; q; q = q->parent)
            if (p == q) return p;
    return NULL;
}

static void emit_pending_scope_exit(zan_irgen_t *g,
                                    zan_irgen_pending_scope_t *target,
                                    bool preserve_return) {
    if (!g->current_async_frame) return;
    zan_irgen_pending_scope_t *scope = g->pending.scope;
    zan_irgen_pending_scope_t *common = pending_scope_common(scope, target);
    if (scope == common) return;
    while (scope->parent != common) scope = scope->parent;
    LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef ft = g->current_async_frame_type;
    LLVMTypeRef hp_type = LLVMStructGetTypeAtIndex(ft, ASYNC_FRAME_HPENDING);
    LLVMValueRef indices[] = { LLVMConstInt(i32, 0, 0),
        LLVMConstInt(i32, (unsigned)scope->handler_id, 0) };
    LLVMValueRef depth = LLVMBuildLoad2(g->builder, i32,
        LLVMBuildGEP2(g->builder, hp_type,
            LLVMBuildStructGEP2(g->builder, ft, g->current_async_frame,
                ASYNC_FRAME_HPENDING, "pending.scope.p"), indices, 2,
            "pending.scope.slot"), "pending.scope.depth");
    if (preserve_return)
        emit_async_pending_discard_preserving(g, depth);
    else
        emit_async_pending_discard(g, g->current_async_frame, ft, depth);
}

static void emit_finally_body(zan_irgen_t *g, local_scope_t *locals,
                              const zan_irgen_finally_entry_t *entry) {
    zan_irgen_pending_scope_t *saved = g->pending.scope;
    zan_irgen_pending_scope_t *scope = NULL;
    if (g->current_async_frame) {
        scope = zan_arena_alloc(g->arena, sizeof(*scope));
        *scope = *entry->pending_scope;
        scope->body = entry->body;
    }
    g->pending.scope = scope;
    emit_stmt(g, entry->body, locals);
    g->pending.scope = saved;
}

typedef struct zan_irgen_finally_shared {
    struct zan_irgen_finally_shared *next;
    LLVMBasicBlockRef body_bb;
    LLVMValueRef dispatch;
    LLVMBasicBlockRef break_target, continue_target;
    unsigned continuation_count;
    int context[10];
    int local_count, pattern_count, catch_count, armed_count, outer_count;
    local_var_t *vars;
    pattern_binding_t *patterns;
    zan_irgen_catch_cleanup_t *catches;
    LLVMValueRef *armed;
    zan_irgen_finally_entry_t *outers;
    zan_irgen_pending_context_t pending;
} zan_irgen_finally_shared_t;

static void finally_shared_context(zan_irgen_t *g, int context[10]) {
    context[0] = g->throw_locals_base;
    context[1] = g->throw_catch_base;
    context[2] = g->loop_locals_base;
    context[3] = g->loop_catch_base;
    context[4] = g->finally_loop_base;
    context[5] = g->eh_armed_loop_base;
    context[6] = g->eh_armed_base;
    context[7] = g->irgen_checked_depth;
    context[8] = g->finally_count;
    context[9] = g->catch_cleanup_count;
}

static bool finally_shared_matches(zan_irgen_t *g, local_scope_t *locals,
                                    zan_irgen_finally_shared_t *s) {
    int context[10];
    finally_shared_context(g, context);
    if (memcmp(context, s->context, sizeof(context)) ||
        locals->count != s->local_count ||
        locals->pattern_count != s->pattern_count ||
        g->catch_cleanup_count != s->catch_count ||
        g->eh_armed_count != s->armed_count ||
        g->finally_count != s->outer_count ||
        g->break_target != s->break_target ||
        g->continue_target != s->continue_target ||
        g->pending.scope != s->pending.scope ||
        g->pending.break_scope != s->pending.break_scope ||
        g->pending.continue_scope != s->pending.continue_scope) return false;
    /* 内部辅助逻辑 */
    if (s->local_count && memcmp(locals->vars, s->vars,
            sizeof(*s->vars) * (size_t)s->local_count)) return false;
    if (s->pattern_count && memcmp(locals->patterns, s->patterns,
            sizeof(*s->patterns) * (size_t)s->pattern_count)) return false;
    if (s->catch_count && memcmp(g->catch_cleanups, s->catches,
            sizeof(*s->catches) * (size_t)s->catch_count)) return false;
    for (int i = 0; i < s->armed_count; i++)
        if (g->eh_armed[i].old_top_slot != s->armed[i]) return false;
    for (int i = 0; i < s->outer_count; i++) {
        if (g->finallys[i].body != s->outers[i].body ||
            g->finallys[i].monitor_obj != s->outers[i].monitor_obj ||
            g->finallys[i].outer_armed_depth != s->outers[i].outer_armed_depth ||
            g->finallys[i].outer_throw_locals_base != s->outers[i].outer_throw_locals_base ||
            g->finallys[i].outer_throw_catch_base != s->outers[i].outer_throw_catch_base ||
            g->finallys[i].pending_parent != s->outers[i].pending_parent ||
            g->finallys[i].pending_scope != s->outers[i].pending_scope ||
            g->finallys[i].in_try_body != s->outers[i].in_try_body)
            return false;
    }
    return true;
}

static void *finally_shared_copy(zan_irgen_t *g, const void *p, size_t bytes) {
    if (!bytes) return NULL;
    void *copy = zan_arena_alloc(g->arena, bytes);
    memcpy(copy, p, bytes);
    return copy;
}

static void emit_shared_pending_finally(zan_irgen_t *g, local_scope_t *locals,
                                        int fin_idx) {
    zan_irgen_finally_entry_t entry = g->finallys[fin_idx];
    zan_irgen_finally_shared_t *s = entry.shared;
    while (s && !finally_shared_matches(g, locals, s)) s = s->next;
    LLVMValueRef fn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder));
    LLVMBasicBlockRef origin = LLVMGetInsertBlock(g->builder);
    LLVMBasicBlockRef cont = LLVMAppendBasicBlockInContext(g->ctx, fn, "fin.cont");
    if (!s) {
        s = zan_arena_alloc(g->arena, sizeof(*s));
        s->next = entry.shared;
        entry.shared = s;
        finally_shared_context(g, s->context);
        s->local_count = locals->count;
        s->pattern_count = locals->pattern_count;
        s->catch_count = g->catch_cleanup_count;
        s->armed_count = g->eh_armed_count;
        s->outer_count = g->finally_count;
        s->break_target = g->break_target;
        s->continue_target = g->continue_target;
        s->pending = g->pending;
        s->vars = finally_shared_copy(g, locals->vars,
            sizeof(*s->vars) * (size_t)s->local_count);
        s->patterns = finally_shared_copy(g, locals->patterns,
            sizeof(*s->patterns) * (size_t)s->pattern_count);
        s->catches = finally_shared_copy(g, g->catch_cleanups,
            sizeof(*s->catches) * (size_t)s->catch_count);
        s->armed = zan_arena_alloc(g->arena,
            sizeof(*s->armed) * (size_t)s->armed_count);
        for (int i = 0; i < s->armed_count; i++)
            s->armed[i] = g->eh_armed[i].old_top_slot;
        s->outers = finally_shared_copy(g, g->finallys,
            sizeof(*s->outers) * (size_t)s->outer_count);
        s->body_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "fin.shared");
        LLVMPositionBuilderAtEnd(g->builder, s->body_bb);
        emit_finally_body(g, locals, &entry);
        locals->count = s->local_count;
        /* A nested try in this body reuses fin_idx */
        g->finallys[fin_idx] = entry;
        if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder))) {
            LLVMBasicBlockRef tail = LLVMGetInsertBlock(g->builder);
            LLVMBasicBlockRef invalid = LLVMAppendBasicBlockInContext(g->ctx, fn,
                "fin.invalid");
            LLVMPositionBuilderAtEnd(g->builder, invalid);
            LLVMBuildUnreachable(g->builder);
            LLVMPositionBuilderAtEnd(g->builder, tail);
            s->dispatch = LLVMBuildSwitch(g->builder,
                LLVMBuildLoad2(g->builder, LLVMInt32TypeInContext(g->ctx),
                    entry.continuation_slot, "fin.next"), invalid, 0);
        }
        LLVMPositionBuilderAtEnd(g->builder, origin);
    }
    if (!s->dispatch) {
        LLVMBuildBr(g->builder, s->body_bb);
        LLVMPositionBuilderAtEnd(g->builder, cont);
        LLVMBuildUnreachable(g->builder);
        return;
    }
    LLVMValueRef id = LLVMConstInt(LLVMInt32TypeInContext(g->ctx),
                                  ++s->continuation_count, 0);
    zan_store_fit(g, id, entry.continuation_slot);
    LLVMBuildBr(g->builder, s->body_bb);
    LLVMAddCase(s->dispatch, id, cont);
    LLVMPositionBuilderAtEnd(g->builder, cont);
}

static void emit_pending_finallys(zan_irgen_t *g, local_scope_t *locals, int base,
                                  bool preserve_return) {
    if (base < 0) base = 0;
    int saved = g->finally_count;
    int saved_armed = g->eh_armed_count;
    int saved_throw_base = g->throw_locals_base;
    int saved_throw_cbase = g->throw_catch_base;
    void *armed = finally_shared_copy(g, g->eh_armed,
        sizeof(g->eh_armed[0]) * (size_t)saved_armed);
    for (int i = saved - 1; i >= base; i--) {
        if (g->current_async_frame) {
            zan_irgen_finally_entry_t *entry = &g->finallys[i];
            emit_eh_disarm_from(g, entry->outer_armed_depth);
            g->eh_armed_count = entry->outer_armed_depth;
            g->throw_locals_base = entry->outer_throw_locals_base;
            g->throw_catch_base = entry->outer_throw_catch_base;
            emit_pending_scope_exit(g, entry->pending_parent, preserve_return);
        }
        if (g->finallys[i].monitor_obj) {
            emit_monitor_exit(g, g->finallys[i].monitor_obj);
            continue;
        }
        if (!g->finallys[i].body) continue;
        int saved_locals = locals->count;
        int hidden_count = saved - i - 1;
        zan_irgen_finally_entry_t *hidden = finally_shared_copy(g,
            &g->finallys[i + 1], sizeof(*hidden) * (size_t)hidden_count);
        g->finally_count = i;
        if (g->current_async_frame && g->finallys[i].continuation_slot) {
            emit_shared_pending_finally(g, locals, i);
        } else {
            zan_irgen_finally_entry_t entry = g->finallys[i];
            emit_finally_body(g, locals, &entry);
            g->finallys[i] = entry;
        }
        if (hidden_count)
            memcpy(&g->finallys[i + 1], hidden,
                   sizeof(*hidden) * (size_t)hidden_count);
        locals->count = saved_locals;
        if (LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder))) break;
    }
    g->finally_count = saved;
    if (saved_armed) memcpy(g->eh_armed, armed,
        sizeof(g->eh_armed[0]) * (size_t)saved_armed);
    g->eh_armed_count = saved_armed;
    g->throw_locals_base = saved_throw_base;
    g->throw_catch_base = saved_throw_cbase;
}

/* 内部辅助逻辑 */
static void emit_finally_on_exception_path(zan_irgen_t *g, local_scope_t *locals,
                                           int fin_idx) {
    if (fin_idx < 0) return;
    if (g->finallys[fin_idx].monitor_obj) {
        /* no exception state to preserve: the exit call runs no Zan code */
        emit_monitor_exit(g, g->finallys[fin_idx].monitor_obj);
        return;
    }
    if (!g->finallys[fin_idx].body) return;
    emit_pending_scope_exit(g, g->finallys[fin_idx].pending_parent, false);
    LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMValueRef top_g, bufs_g, exc_g;
    get_eh_globals(g, &top_g, &bufs_g, &exc_g);
    LLVMValueRef own_g = get_eh_exc_owned_global(g);
    LLVMValueRef tid_g = get_eh_exc_tid_global(g);
    LLVMValueRef s_exc = NULL, s_own = NULL, s_tid = NULL;
    if (g->current_async_frame) {
        LLVMValueRef owns = LLVMBuildLoad2(g->builder, i32t, own_g, "fin.owned");
        LLVMValueRef kind = LLVMBuildSelect(g->builder,
            LLVMBuildICmp(g->builder, LLVMIntNE, owns, LLVMConstInt(i32t, 0, 0),
                          "fin.isowned"),
            LLVMConstInt(i32t, ASYNC_PENDING_EXCEPTION, 0),
            LLVMConstInt(i32t, ASYNC_PENDING_BORROWED_EXCEPTION, 0), "fin.kind");
        emit_async_pending_push(g,
            LLVMBuildPtrToInt(g->builder,
                LLVMBuildLoad2(g->builder, i8ptr, exc_g, "fin.exc.v"),
                LLVMInt64TypeInContext(g->ctx), "fin.exc.bits"),
            LLVMBuildLoad2(g->builder, i8ptr, tid_g, "fin.tid.v"), kind);
        zan_store_fit(g, LLVMConstNull(i8ptr), exc_g);
        zan_store_fit(g, LLVMConstInt(i32t, 0, 0), own_g);
        zan_store_fit(g, LLVMConstNull(i8ptr), tid_g);
    } else {
        s_exc = emit_entry_alloca(g, i8ptr, "fin.exc");
        s_own = emit_entry_alloca(g, i32t, "fin.own");
        s_tid = emit_entry_alloca(g, i8ptr, "fin.tid");
        zan_store_fit(g, LLVMBuildLoad2(g->builder, i8ptr, exc_g, "fin.exc.v"), s_exc);
        zan_store_fit(g, LLVMBuildLoad2(g->builder, i32t, own_g, "fin.own.v"), s_own);
        zan_store_fit(g, LLVMBuildLoad2(g->builder, i8ptr, tid_g, "fin.tid.v"), s_tid);
    }
    int saved_count = g->finally_count;
    int saved_locals = locals->count;
    int hidden_count = saved_count > fin_idx ? saved_count - fin_idx : 1;
    zan_irgen_finally_entry_t *hidden = finally_shared_copy(g,
        &g->finallys[fin_idx], sizeof(*hidden) * (size_t)hidden_count);
    g->finally_count = fin_idx;   /* the body must not re-run itself */
    emit_finally_body(g, locals, &hidden[0]);
    memcpy(&g->finallys[fin_idx], hidden,
           sizeof(*hidden) * (size_t)hidden_count);
    locals->count = saved_locals;
    g->finally_count = saved_count;
    if (LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder))) return;
    if (g->current_async_frame) {
        LLVMTypeRef ft = g->current_async_frame_type;
        LLVMValueRef record = emit_async_pending_top(g);
        LLVMValueRef value = LLVMBuildLoad2(g->builder, LLVMInt64TypeInContext(g->ctx),
            async_pending_field(g, ft, record, 0), "fin.exc.bits");
        zan_store_fit(g, LLVMBuildIntToPtr(g->builder, value, i8ptr, "fin.exc.r"), exc_g);
        LLVMValueRef kind = LLVMBuildLoad2(g->builder, i32t,
            async_pending_field(g, ft, record, 2), "fin.kind");
        zan_store_fit(g, LLVMBuildZExt(g->builder,
            LLVMBuildICmp(g->builder, LLVMIntEQ, kind,
                LLVMConstInt(i32t, ASYNC_PENDING_EXCEPTION, 0), "fin.owns"),
            i32t, "fin.owned"), own_g);
        zan_store_fit(g, LLVMBuildLoad2(g->builder, i8ptr,
            async_pending_field(g, ft, record, 1), "fin.tid.r"), tid_g);
        emit_async_pending_pop(g, record);
    } else {
        zan_store_fit(g, LLVMBuildLoad2(g->builder, i8ptr, s_exc, "fin.exc.r"), exc_g);
        zan_store_fit(g, LLVMBuildLoad2(g->builder, i32t, s_own, "fin.own.r"), own_g);
        zan_store_fit(g, LLVMBuildLoad2(g->builder, i8ptr, s_tid, "fin.tid.r"), tid_g);
    }
}

/* 内部辅助实现 */
static void emit_finallys_left_by_throw(zan_irgen_t *g, local_scope_t *locals) {
    int base = g->finally_count;
    while (base > 0 && !g->finallys[base - 1].in_try_body) base--;
    for (int i = g->finally_count - 1; i >= base; i--) {
        emit_finally_on_exception_path(g, locals, i);
        if (LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder))) return;
    }
}

/* 内部辅助实现 */
static void emit_finallys_below_for_propagate(zan_irgen_t *g,
                                              local_scope_t *locals,
                                              int fin_idx) {
    if (LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder))) return;
    for (int i = fin_idx - 1; i >= 0; i--) {
        if (g->finallys[i].in_try_body) break;
        emit_finally_on_exception_path(g, locals, i);
        if (LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder))) return;
    }
}

/* 内部辅助逻辑 */
static void emit_eh_propagate_tail(zan_irgen_t *g) {
    LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMValueRef top_g, bufs_g, exc_g;
    get_eh_globals(g, &top_g, &bufs_g, &exc_g);
    LLVMValueRef fn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder));
    LLVMValueRef rtop = LLVMBuildLoad2(g->builder, i32t, top_g, "reh.top");
    LLVMValueRef rhas = zan_icmp(g->builder, LLVMIntSGE, rtop,
        LLVMConstInt(i32t, 0, 0), "reh.has");
    LLVMBasicBlockRef rjmp_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "reh.jmp");
    LLVMBasicBlockRef rdie_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "reh.die");
    /* 内部辅助逻辑 */
    if (g->target_is_wasm) {
        LLVMBuildBr(g->builder, rdie_bb);
        LLVMPositionBuilderAtEnd(g->builder, rjmp_bb);
        LLVMBuildUnreachable(g->builder);
        LLVMPositionBuilderAtEnd(g->builder, rdie_bb);
    } else {
        LLVMBuildCondBr(g->builder, rhas, rjmp_bb, rdie_bb);
        LLVMPositionBuilderAtEnd(g->builder, rjmp_bb);
        /* 内部辅助逻辑 */
        emit_eh_longjmp(g, emit_eh_buf_ptr(g, rtop));
        LLVMBuildUnreachable(g->builder);
        LLVMPositionBuilderAtEnd(g->builder, rdie_bb);
    }
    emit_eh_hook_call(g, "__zan_eh_unhandled");
    LLVMValueRef printf_fn = LLVMGetNamedFunction(g->mod, "printf");
    if (printf_fn) {
        LLVMTypeRef printf_ty = LLVMFunctionType(i32t, &i8ptr, 1, 1);
        /* 内部辅助实现 */
        LLVMValueRef exc = LLVMBuildLoad2(g->builder, i8ptr, exc_g, "reh.exc");
        LLVMValueRef tid = LLVMBuildLoad2(g->builder, i8ptr,
            get_eh_exc_tid_global(g), "reh.tid");
        LLVMValueRef hasExc = zan_icmp(g->builder, LLVMIntNE, exc,
            LLVMConstNull(i8ptr), "reh.has");
        LLVMValueRef isStr = zan_icmp(g->builder, LLVMIntEQ, tid,
            LLVMConstNull(i8ptr), "reh.isstr");
        LLVMBasicBlockRef reh_check_bb =
            LLVMAppendBasicBlockInContext(g->ctx, fn, "reh.check");
        LLVMBasicBlockRef reh_str_bb =
            LLVMAppendBasicBlockInContext(g->ctx, fn, "reh.str");
        LLVMBasicBlockRef reh_cls_bb =
            LLVMAppendBasicBlockInContext(g->ctx, fn, "reh.cls");
        LLVMBasicBlockRef reh_none_bb =
            LLVMAppendBasicBlockInContext(g->ctx, fn, "reh.none");
        LLVMBasicBlockRef reh_cont_bb =
            LLVMAppendBasicBlockInContext(g->ctx, fn, "reh.cont");
        LLVMBuildCondBr(g->builder, hasExc, reh_check_bb, reh_none_bb);
        LLVMPositionBuilderAtEnd(g->builder, reh_check_bb);
        LLVMBuildCondBr(g->builder, isStr, reh_str_bb, reh_cls_bb);
        /* string throw: print the message itself */
        LLVMPositionBuilderAtEnd(g->builder, reh_str_bb);
        {
            LLVMValueRef sfmt = zan_irgen_intern_string(g,
                "Unhandled exception: %s\n");
            LLVMValueRef sargs[2] = { sfmt, exc };
            zan_call2(g->builder, printf_ty, printf_fn, sargs, 2, "");
        }
        LLVMBuildBr(g->builder, reh_cont_bb);
        /* 内部辅助逻辑 */
        LLVMPositionBuilderAtEnd(g->builder, reh_cls_bb);
        {
            LLVMValueRef name_fn = get_eh_tid_name_fn(g);
            LLVMValueRef cname = zan_call2(g->builder,
                LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr }, 1, 0),
                name_fn, (LLVMValueRef[]){ tid }, 1, "reh.cname");
            LLVMValueRef found = zan_icmp(g->builder, LLVMIntNE, cname,
                LLVMConstNull(i8ptr), "reh.cfound");
            LLVMBasicBlockRef named_bb =
                LLVMAppendBasicBlockInContext(g->ctx, fn, "reh.named");
            LLVMBasicBlockRef anon_bb =
                LLVMAppendBasicBlockInContext(g->ctx, fn, "reh.anon");
            LLVMBuildCondBr(g->builder, found, named_bb, anon_bb);
            LLVMPositionBuilderAtEnd(g->builder, named_bb);
            {
                LLVMValueRef cfmt2 = zan_irgen_intern_string(g,
                    "Unhandled exception: %s\n");
                LLVMValueRef cargs[2] = { cfmt2, cname };
                zan_call2(g->builder, printf_ty, printf_fn, cargs, 2, "");
            }
            LLVMBuildBr(g->builder, reh_cont_bb);
            LLVMPositionBuilderAtEnd(g->builder, anon_bb);
            LLVMValueRef cfmt = zan_irgen_intern_string(g,
                "Unhandled exception (class object)\n");
            zan_call2(g->builder, printf_ty, printf_fn, &cfmt, 1, "");
            LLVMBuildBr(g->builder, reh_cont_bb);
        }
        /* no exception object in flight (internal rethrow miss) */
        LLVMPositionBuilderAtEnd(g->builder, reh_none_bb);
        {
            LLVMValueRef nfmt = zan_irgen_intern_string(g,
                "Unhandled exception\n");
            zan_call2(g->builder, printf_ty, printf_fn, &nfmt, 1, "");
        }
        LLVMBuildBr(g->builder, reh_cont_bb);
        LLVMPositionBuilderAtEnd(g->builder, reh_cont_bb);
    }
    LLVMTypeRef exit_ty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), &i32t, 1, 0);
    LLVMValueRef exit_fn = get_libc_fn(g, "exit", exit_ty);
    LLVMValueRef one = LLVMConstInt(i32t, 1, 0);
    zan_call2(g->builder, exit_ty, exit_fn, &one, 1, "");
    LLVMBuildUnreachable(g->builder);
}

/* True when `expr` is a call to an extern/DllImport function */
static int call_targets_extern(zan_irgen_t *g, zan_ast_node_t *expr) {
    if (!expr || expr->kind != AST_CALL || !expr->call.callee) return 0;
    zan_symbol_t *sym = NULL;
    if (expr->call.callee->kind == AST_IDENTIFIER) {
        /* A static method of the enclosing class (`calloc( */
        if (g->current_type_sym)
            sym = get_method_sym(g->current_type_sym,
                                 expr->call.callee->ident.name);
        if (!sym)
            sym = zan_binder_lookup(g->binder, expr->call.callee->ident.name);
    } else if (expr->call.callee->kind == AST_MEMBER_ACCESS &&
               expr->call.callee->member.object->kind == AST_IDENTIFIER) {
        /* ClassName.ExternMethod(...) */
        zan_symbol_t *cls = zan_binder_lookup(g->binder,
            expr->call.callee->member.object->ident.name);
        if (cls) sym = get_method_sym(cls, expr->call.callee->member.name);
    }
    if (!sym || !sym->decl || sym->decl->kind != AST_METHOD_DECL) return 0;
    return zan_ast_method_extern_lib(sym->decl).str != NULL ||
           (sym->decl->method_decl.modifiers & MOD_EXTERN) != 0;
}

/* 内部辅助实现 */
static int emit_boxed_var_decl(zan_irgen_t *g, zan_ast_node_t *stmt,
                               local_scope_t *locals) {
    zan_ast_node_t *body = g->current_async_frame
        ? g->current_async_body : g->current_fn_body;
    if (!body) return 0;
    zan_type_t *type = stmt->var_decl.type
        ? resolve_type_ctx(g, stmt->var_decl.type)
        : (stmt->var_decl.initializer
               ? infer_expr_type(g, stmt->var_decl.initializer, locals)
               : NULL);
    if (!type) return 0;
    LLVMTypeRef payload = map_type(g, type);
    LLVMTypeKind k = LLVMGetTypeKind(payload);
    int rc = is_rc_managed_type(type) && k == LLVMPointerTypeKind;
    int aggregate_rc = type->kind == TYPE_STRUCT &&
        type_contains_collection_rc(g, type, 0);
    if (!local_is_lambda_captured(g, locals, body, stmt)) return 0;
    if (type->kind == TYPE_OBJECT || type->kind == TYPE_TYPE_PARAM) {
        zan_diag_emit(g->diag, DIAG_ERROR, stmt->loc,
            "cannot capture local '%.*s' of type '%.*s': its runtime ownership is not representable yet",
            (int)stmt->var_decl.name.len, stmt->var_decl.name.str,
            (int)type->name.len, type->name.str ? type->name.str : "");
        return 0;
    }
    if (!rc && k != LLVMIntegerTypeKind && k != LLVMFloatTypeKind &&
        k != LLVMDoubleTypeKind && k != LLVMStructTypeKind) {
        zan_diag_emit(g->diag, DIAG_ERROR, stmt->loc,
            "cannot capture local '%.*s': unsupported value representation",
            (int)stmt->var_decl.name.len, stmt->var_decl.name.str);
        return 0;
    }

    LLVMValueRef slot;
    if (g->current_async_frame) {
        local_var_t *pre = local_find_async_decl(locals, stmt);
        /* Only locals planned as cells may borrow a frame owner here. */
        if (!pre || !pre->box_cell) return 0;
        int pre_idx = (int)(pre - locals->vars);
        slot = pre->alloca;
        LLVMValueRef cell = pre->box_cell;
        LLVMValueRef owner = pre->box_owner_slot;
        local_add(locals, stmt->var_decl.name, slot, type);
        local_var_t *v = &locals->vars[locals->count - 1];
        v->box_cell = cell;
        v->box_owner_slot = owner;
        v->frame_owner = pre_idx;
        /* This lexical binding borrows the cell */
    } else {
        /* A null payload makes the destructor safe if initialization throws */
        LLVMValueRef cell = emit_box_cell(g, stmt->loc, payload, type, NULL);
        slot = box_value_ptr(g, cell, payload);
        local_add(locals, stmt->var_decl.name, slot, type);
        own_box_cell(g, &locals->vars[locals->count - 1], cell);
    }
    if (rc) locals->vars[locals->count - 1].arc_owned = 1;
    if (aggregate_rc) locals->vars[locals->count - 1].struct_rc = 1;
    if (type && type->kind == TYPE_STRING && stmt->var_decl.initializer &&
        call_targets_extern(g, stmt->var_decl.initializer))
        locals->vars[locals->count - 1].opaque_string = 1;
    if (stmt->var_decl.initializer) {
        LLVMValueRef init =
            (type->kind == TYPE_DELEGATE &&
             stmt->var_decl.initializer->kind == AST_LAMBDA)
                ? emit_lambda_typed(g, stmt->var_decl.initializer, type, locals)
                : emit_expr(g, stmt->var_decl.initializer, locals);
        if (rc)
            emit_rc_capture_local(g, type, slot, init,
                                  stmt->var_decl.initializer, locals);
        else if (aggregate_rc)
            emit_struct_local_capture(g, type, slot, init,
                                      stmt->var_decl.initializer, locals);
        else
            zan_store_fit(g, init, slot);
    }
    return 1;
}

/* 发射a 0-arg method call on a receiver value already in hand */
static LLVMValueRef emit_foreach_call0(zan_irgen_t *g, zan_type_t *recv_ty,
                                       zan_symbol_t *m, LLVMValueRef recv) {
    if (!m) return LLVMConstInt(LLVMInt64TypeInContext(g->ctx), 0, 0);
    bool is_static = (m->modifiers & MOD_STATIC) != 0;
    for (int fi = irgen_find_function(g, m); fi >= 0; fi = -1) {
        if (g->functions[fi].sym != m) continue;
        LLVMTypeRef mft = g->functions[fi].fn_type;
        LLVMValueRef mfn = route_generic_method(g, recv_ty, m,
            g->functions[fi].fn, mft, &mft);
        LLVMValueRef args[1];
        unsigned na = 0;
        if (!is_static) args[na++] = recv;
        LLVMValueRef r = emit_dispatch_call(g, is_static ? NULL : recv_ty->sym,
                                            m, mfn, mft, args, (int)na, "fe.m");
        return coerce_generic_result(g, r, m, recv_ty);
    }
    return LLVMConstInt(LLVMInt64TypeInContext(g->ctx), 0, 0);
}

static void emit_stmt(zan_irgen_t *g, zan_ast_node_t *stmt, local_scope_t *locals) {
    if (!stmt) return;

    /* 内部辅助实现 */
    LLVMBasicBlockRef cur_bb = LLVMGetInsertBlock(g->builder);
    if (cur_bb && LLVMGetBasicBlockTerminator(cur_bb)) {
        LLVMBasicBlockRef dead_bb = LLVMAppendBasicBlockInContext(
            g->ctx, LLVMGetBasicBlockParent(cur_bb), "dead");
        LLVMPositionBuilderAtEnd(g->builder, dead_bb);
    }

    /* 内部辅助逻辑 */
    di_set_loc(g, stmt->loc);

    /* 内部辅助实现 */
    int arc_nested = (stmt->kind == AST_IF_STMT || stmt->kind == AST_WHILE_STMT ||
                      stmt->kind == AST_DO_WHILE_STMT || stmt->kind == AST_FOR_STMT ||
                      stmt->kind == AST_FOREACH_STMT || stmt->kind == AST_SWITCH_STMT ||
                      stmt->kind == AST_TRY_STMT);
    if (arc_nested) g->arc_stmt_depth++;

    switch (stmt->kind) {
    case AST_BLOCK: {
        int block_start = locals->count;
        for (int i = 0; i < stmt->block.stmts.count; i++) {
            zan_ast_node_t *bs = stmt->block.stmts.items[i];
            emit_stmt(g, bs, locals);
        }
        if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder))) {
            emit_release_owned_locals_from(g, locals, block_start);
        }
        break;
    }

    case AST_VAR_DECL: {
        /* 内部辅助逻辑 */
        if (stmt->var_decl.type && stmt->var_decl.initializer) {
            check_implicit_narrowing(g, resolve_type_ctx(g, stmt->var_decl.type),
                           infer_expr_type(g, stmt->var_decl.initializer, locals),
                           stmt->var_decl.initializer, "initializer");
        }
        /* 内部辅助逻辑 */
        if (emit_boxed_var_decl(g, stmt, locals)) return;
        /* 内部辅助实现 */
        if (g->current_async_frame && g->current_async_slot_count > 0) {
            /* 内部辅助逻辑 */
            local_var_t *pre = local_find_async_decl(locals, stmt);
            if (pre) {
                int pre_idx = (int)(pre - locals->vars);
                /* 内部辅助逻辑 */
                zan_type_t *type = stmt->var_decl.type
                    ? resolve_type_ctx(g, stmt->var_decl.type)
                    : NULL;
                if (!type && stmt->var_decl.initializer)
                    type = infer_expr_type(g, stmt->var_decl.initializer, locals);
                if (!type) type = pre->type;
                for (int i = 0; i < g->current_async_slot_count; i++) {
                    /* 内部辅助实现 */
                    if (g->current_async_slots[i].slot_alloca == pre->alloca &&
                        !async_slot_type_compatible(type, pre->type))
                        continue;
                    if (g->current_async_slots[i].slot_alloca == pre->alloca) {
                        /* 内部辅助实现 */
                        int arc_own = (type && is_rc_managed_type(type) &&
                                       LLVMGetTypeKind(g->current_async_slots[i].llvm) == LLVMPointerTypeKind);
                        /* 内部辅助逻辑 */
                        if (stmt->var_decl.initializer) {
                            LLVMValueRef iv = (type->kind == TYPE_DELEGATE &&
                                stmt->var_decl.initializer->kind == AST_LAMBDA)
                                ? emit_lambda_typed(g, stmt->var_decl.initializer, type, locals)
                                : emit_expr(g, stmt->var_decl.initializer, locals);
                            LLVMTypeRef slot_ty = g->current_async_slots[i].llvm;
                            if (arc_own) {
                                emit_rc_capture_local(g, type, pre->alloca, iv,
                                    stmt->var_decl.initializer, locals);
                            } else if (pre->struct_rc) {
                                emit_struct_local_capture(g, type, pre->alloca, iv,
                                    stmt->var_decl.initializer, locals);
                            } else {
                                iv = coerce_int_to(g, iv, slot_ty);
                                /* 内部辅助逻辑 */
                                if (LLVMGetTypeKind(slot_ty) == LLVMPointerTypeKind &&
                                    LLVMGetTypeKind(LLVMTypeOf(iv)) == LLVMPointerTypeKind &&
                                    LLVMTypeOf(iv) != slot_ty) {
                                    iv = LLVMBuildBitCast(g->builder, iv, slot_ty, "fl.bc");
                                }
                                zan_store_fit(g, iv, pre->alloca);
                            }
                        }
                        if (arc_own) pre->arc_owned = 1;
                        /* 内部辅助逻辑 */
                        if (type && type->kind == TYPE_STRING &&
                            stmt->var_decl.initializer &&
                            call_targets_extern(g, stmt->var_decl.initializer))
                            pre->opaque_string = 1;
                        /* 内部辅助实现 */
                        local_add(locals, stmt->var_decl.name,
                                  locals->vars[pre_idx].alloca, type);
                        locals->vars[locals->count - 1].frame_owner = pre_idx;
                        return;
                    }
                }
            }
        }
        zan_type_t *type = g->binder->type_int; /* default */
        if (stmt->var_decl.type) {
            type = resolve_type_ctx(g, stmt->var_decl.type);
        } else if (stmt->var_decl.initializer) {
            zan_ast_node_t *init = stmt->var_decl.initializer;

            if (init->kind == AST_NEW_EXPR && init->new_expr.is_array) {
                LLVMValueRef arr_val = emit_expr(g, init, locals);
                LLVMTypeRef ptr_type = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                LLVMValueRef alloca = emit_entry_alloca(g, ptr_type, "arr");
                zan_store_fit(g, arr_val, alloca);

                /* 内部辅助逻辑 */
                zan_type_t *arr_type = resolve_type_ctx(g, init->new_expr.type);
                if (!arr_type || arr_type->kind != TYPE_ARRAY) {
                    zan_type_t *elem_type = arr_type ? arr_type : g->binder->type_int;
                    arr_type = (zan_type_t *)zan_arena_alloc(g->arena, sizeof(zan_type_t));
                    memset(arr_type, 0, sizeof(zan_type_t));
                    arr_type->kind = TYPE_ARRAY;
                    arr_type->element_type = elem_type;
                    arr_type->array_rank = 1;
                }
                local_add(locals, stmt->var_decl.name, alloca, arr_type);
                /* 内部辅助逻辑 */
                arc_own_local(g, locals);
                return;
            }

            if (init->kind == AST_NEW_EXPR && init->new_expr.type &&
                init->new_expr.type->kind == AST_TYPE_REF) {
                zan_istr_t tname = init->new_expr.type->type_ref.name;
                if (tname.len == 4 && memcmp(tname.str, "List", 4) == 0) {
                    LLVMValueRef list_val = emit_expr(g, init, locals);
                    LLVMTypeRef ptr_type = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                    LLVMValueRef alloca = emit_entry_alloca(g, ptr_type, "list");
                    zan_store_fit(g, list_val, alloca);
                    zan_type_t *list_type = resolve_type_ctx(g, init->new_expr.type);
                    local_add(locals, stmt->var_decl.name, alloca, list_type);
                    /* 内部辅助逻辑 */
                    if (list_type && is_rc_managed_type(list_type))
                        arc_own_local(g, locals);
                    return;
                }
                if (tname.len == 13 && memcmp(tname.str, "StringBuilder", 13) == 0) {
                    LLVMValueRef sb_val = emit_expr(g, init, locals);
                    LLVMTypeRef ptr_type = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                    LLVMValueRef alloca = emit_entry_alloca(g, ptr_type, "sb");
                    zan_store_fit(g, sb_val, alloca);
                    zan_type_t *sb_type = resolve_type_ctx(g, init->new_expr.type);
                    local_add(locals, stmt->var_decl.name, alloca, sb_type);
                    /* 内部辅助逻辑 */
                    if (sb_type && is_rc_managed_type(sb_type))
                        arc_own_local(g, locals);
                    return;
                }
                if ((tname.len == 4 && memcmp(tname.str, "Dict", 4) == 0) ||
                    (tname.len == 10 && memcmp(tname.str, "Dictionary", 10) == 0)) {
                    LLVMValueRef dict_val = emit_expr(g, init, locals);
                    LLVMTypeRef ptr_type = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                    LLVMValueRef alloca = emit_entry_alloca(g, ptr_type, "dict");
                    zan_store_fit(g, dict_val, alloca);
                    zan_type_t *dict_type = resolve_type_ctx(g, init->new_expr.type);
                    local_add(locals, stmt->var_decl.name, alloca, dict_type);
                    /* 内部辅助逻辑 */
                    arc_own_local(g, locals);
                    return;
                }
            }

            if (init->kind == AST_NEW_EXPR && init->new_expr.type) {
                zan_istr_t type_name = {NULL, 0};
                if (init->new_expr.type->kind == AST_IDENTIFIER) {
                    type_name = init->new_expr.type->ident.name;
                } else if (init->new_expr.type->kind == AST_TYPE_REF) {
                    type_name = init->new_expr.type->type_ref.name;
                }
                if (type_name.str) {
                    zan_symbol_t *sym = zan_binder_lookup(g->binder, type_name);
                    if (sym && sym->type && sym->type->kind == TYPE_STRUCT) {
                        type = sym->type;
                        LLVMTypeRef st = get_struct_llvm_type(g, sym);
                        if (st) {
                            LLVMValueRef alloca = emit_entry_alloca(g, st, "var");
                            zan_store_fit(g, LLVMConstNull(st), alloca);

                            bool ctor_called = false;
                            zan_type_t *new_inst = resolve_type_ctx(
                                g, init->new_expr.type);
                            /* 内部辅助实现 */
                            int init_start = init->new_expr.args.count;
                            while (init_start > 0) {
                                zan_ast_node_t *a =
                                    init->new_expr.args.items[init_start - 1];
                                if (a->kind == AST_COLL_INIT) {
                                    if (get_field_index(sym, a->coll_init.name) < 0)
                                        break;
                                    init_start--;
                                    continue;
                                }
                                if (a->kind != AST_ASSIGNMENT ||
                                    a->binary.left->kind != AST_IDENTIFIER ||
                                    get_field_index(sym,
                                        a->binary.left->ident.name) < 0)
                                    break;
                                init_start--;
                            }
                            zan_ast_list_t ctor_args = init->new_expr.args;
                            ctor_args.count = init_start;
                            struct zan_ctor_entry *ctor = find_ctor(
                                g, sym, &ctor_args, locals, NULL);
                            if (!ctor) {
                                zan_ast_list_t filled;
                                if (fill_ctor_default_args(g, sym, &ctor_args,
                                                           &filled)) {
                                    struct zan_ctor_entry *dc = find_ctor(
                                        g, sym, &filled, locals, NULL);
                                    if (dc) {
                                        ctor = dc;
                                        ctor_args = filled;
                                    }
                                }
                            }
                            if (ctor) {
                                int argc = ctor_args.count + 1;
                                LLVMValueRef *call_args = (LLVMValueRef *)calloc(
                                    (size_t)argc, sizeof(LLVMValueRef));
                                call_args[0] = alloca;
                                for (int k = 0; k < ctor_args.count; k++) {
                                    zan_ast_node_t *param =
                                        ctor->decl->method_decl.params.items[k];
                                    zan_type_t *pt = zan_binder_resolve_type(
                                        g->binder, param->param.type);
                                    call_args[k + 1] = emit_arg_typed(
                                        g, ctor_args.items[k], pt, locals);
                                }
                                coerce_args_to_params(g, ctor->fn_type, call_args, argc);
                                zan_call2(g->builder, ctor->fn_type, ctor->fn,
                                    call_args, (unsigned)argc, "");
                                for (int k = 0; k < ctor_args.count; k++)
                                    emit_release_owned_call_temp(g,
                                        ctor_args.items[k],
                                        call_args[k + 1], locals);
                                free(call_args);
                                ctor_called = true;
                            }

                            /* 内部辅助逻辑 */
                            if (!ctor_called) {
                                emit_implicit_field_initializers(
                                    g, sym, new_inst ? new_inst : sym->type,
                                    alloca, locals);
                            }
                            /* 内部辅助逻辑 */
                            zan_ast_list_t object_inits;
                            zan_ast_list_init(&object_inits);
                            for (int oi = init_start;
                                 oi < init->new_expr.args.count; oi++) {
                                zan_ast_list_push(&object_inits,
                                    init->new_expr.args.items[oi], g->arena);
                            }
                            for (int oi = 0;
                                 oi < init->new_expr.arg_inits.count; oi++) {
                                zan_ast_list_push(&object_inits,
                                    init->new_expr.arg_inits.items[oi], g->arena);
                            }
                            for (int i = 0; i < object_inits.count; i++) {
                                zan_ast_node_t *arg = object_inits.items[i];
                                /* `Members = { a, b }`: same synthetic `member */
                                if (arg->kind == AST_COLL_INIT) {
                                    zan_istr_t cname = arg->coll_init.name;
                                    zan_symbol_t *msym = get_field_sym(sym, cname);
                                    if (!msym || !msym->type) continue;
                                    int getter_owned = 0;
                                    zan_ast_node_t *recv = zan_ast_new(g->arena,
                                        AST_IDENTIFIER, arg->loc);
                                    recv->ident.name = cname;
                                    LLVMTypeRef coll_lt = map_type(g, msym->type);
                                    LLVMValueRef cslot = emit_entry_alloca(g,
                                        coll_lt, "cinit.slot");
                                    zan_store_fit(g, LLVMConstNull(coll_lt), cslot);
                                    int cfi = get_field_index(sym, cname);
                                    zan_symbol_t *getter =
                                        property_getter_sym(g, msym);
                                    if (getter) {
                                        LLVMValueRef v = emit_property_getter_call(
                                            g, getter,
                                            new_inst ? new_inst : sym->type,
                                            alloca, arg, locals);
                                        zan_store_fit(g, v, cslot);
                                        /* 内部辅助实现 */
                                        getter_owned = 1;
                                    } else if (cfi >= 0) {
                                        LLVMValueRef cptr = emit_field_ptr(g, sym,
                                            st, alloca, cfi, "cinit.ptr");
                                        LLVMValueRef v = LLVMBuildLoad2(g->builder,
                                            coll_lt, cptr, "cinit.load");
                                        zan_store_fit(g, v, cslot);
                                    } else {
                                        continue;
                                    }
                                    local_add(locals, cname, cslot, msym->type);
                                    for (int k = 0;
                                         k < arg->coll_init.items.count; k++) {
                                        zan_ast_node_t *item =
                                            arg->coll_init.items.items[k];
                                        zan_ast_node_t *madd = zan_ast_new(g->arena,
                                            AST_MEMBER_ACCESS, item->loc);
                                        madd->member.object = recv;
                                        madd->member.name = (zan_istr_t){"Add", 3};
                                        madd->member.null_cond = 0;
                                        zan_ast_node_t *addcall = zan_ast_new(
                                            g->arena, AST_CALL, item->loc);
                                        addcall->call.callee = madd;
                                        zan_ast_list_init(&addcall->call.args);
                                        zan_ast_list_init(&addcall->call.type_args);
                                        zan_ast_list_push(&addcall->call.args,
                                                          item, g->arena);
                                        if (msym->type &&
                                            type_named(msym->type, "Dict", 4) &&
                                            k + 1 < arg->coll_init.items.count) {
                                            zan_ast_list_push(&addcall->call.args,
                                                arg->coll_init.items.items[++k],
                                                g->arena);
                                        }
                                        emit_expr(g, addcall, locals);
                                    }
                                    for (int lv = locals->count - 1; lv >= 0; lv--) {
                                        if (locals->vars[lv].name.len == cname.len &&
                                            memcmp(locals->vars[lv].name.str,
                                                   cname.str, (size_t)cname.len) == 0) {
                                            for (int sh = lv; sh < locals->count - 1; sh++)
                                                locals->vars[sh] = locals->vars[sh + 1];
                                            locals->count--;
                                            break;
                                        }
                                    }
                                    if (getter_owned) {
                                        LLVMValueRef cv = LLVMBuildLoad2(g->builder,
                                            coll_lt, cslot, "cinit.done");
                                        emit_rc_release_for_type(g, msym->type, cv);
                                    }
                                    continue;
                                }
                                if (arg->kind == AST_ASSIGNMENT && arg->binary.left->kind == AST_IDENTIFIER) {
                                    zan_symbol_t *fsym = get_field_sym(sym, arg->binary.left->ident.name);
                                    /* 内部辅助逻辑 */
                                    zan_symbol_t *setter = property_setter_sym(g, fsym);
                                    if (setter) {
                                        emit_property_setter_call(g, setter,
                                            new_inst ? new_inst : sym->type,
                                            alloca, emit_expr(g, arg->binary.right, locals),
                                            arg->binary.left, arg->binary.right, locals);
                                        continue;
                                    }
                                    int fi = get_field_index(sym, arg->binary.left->ident.name);
                                    if (fi >= 0) {
                                        LLVMValueRef fptr = emit_field_ptr(g, sym, st, alloca, fi, "finit");
                                        LLVMValueRef fval = emit_expr(g, arg->binary.right, locals);
                                        zan_type_t *fst = field_store_type(
                                            g, fsym, new_inst ? new_inst : sym->type);
                                        if (fsym && fsym->type) {
                                            LLVMTypeRef target_t = map_type(g, fsym->type);
                                            LLVMTypeRef val_t = LLVMTypeOf(fval);
                                            if (LLVMGetTypeKind(target_t) == LLVMFloatTypeKind &&
                                                LLVMGetTypeKind(val_t) == LLVMDoubleTypeKind) {
                                                fval = LLVMBuildFPTrunc(g->builder, fval, target_t, "trunc");
                                            } else if (LLVMGetTypeKind(target_t) == LLVMDoubleTypeKind &&
                                                       LLVMGetTypeKind(val_t) == LLVMFloatTypeKind) {
                                                fval = LLVMBuildFPExt(g->builder, fval, target_t, "ext");
                                            }
                                        }
                                        /* 内部辅助实现 */
                                        if (fst && (is_rc_managed_type(fst) || fst->kind == TYPE_OBJECT)) {
                                            emit_rc_store_field(g, fst, fptr, fval,
                                                arg->binary.right, locals,
                                                (fsym->modifiers & MOD_WEAK) ? 1 : 0);
                                        } else if (fst && fst->kind == TYPE_STRUCT &&
                                                   type_contains_collection_rc(g, fst, 0)) {
                                            emit_struct_field_capture(g, fst, fptr, fval,
                                                arg->binary.right, locals);
                                        } else {
                                            zan_store_fit(g, fval, fptr);
                                        }
                                    }
                                }
                            }
                            local_add(locals, stmt->var_decl.name, alloca, type);
                            /* 内部辅助逻辑 */
                            if (type && type->kind == TYPE_STRUCT &&
                                type_contains_collection_rc(g, type, 0))
                                locals->vars[locals->count - 1].struct_rc = 1;
                            return;
                        }
                    }
                }
            }

            /* tuple initializer: `var t = (a, b, */
            if (init->kind == AST_TUPLE_EXPR) {
                zan_type_t *ttype = infer_expr_type(g, init, locals);
                if (ttype && ttype->kind == TYPE_STRUCT && ttype->sym) {
                    /* 内部辅助逻辑 */
                    LLVMTypeRef st = get_struct_llvm_type(g, ttype->sym);
                    if (!st) st = map_type(g, ttype);
                    if (st) {
                        LLVMValueRef alloca = emit_entry_alloca(g, st, "tup");
                        zan_store_fit(g, LLVMConstNull(st), alloca);
                        LLVMValueRef tup = emit_expr(g, init, locals);
                        LLVMValueRef tupval =
                            LLVMGetTypeKind(LLVMTypeOf(tup)) == LLVMPointerTypeKind
                                ? LLVMBuildLoad2(g->builder, st, tup, "tup.load")
                                : tup;
                        zan_store_fit(g, tupval, alloca);
                        local_add(locals, stmt->var_decl.name, alloca, ttype);
                        /* 内部辅助逻辑 */
                        if (type_contains_collection_rc(g, ttype, 0))
                            locals->vars[locals->count - 1].struct_rc = 1;
                        return;
                    }
                }
            }

            /* regular type inference from initializer */
            type = infer_expr_type(g, init, locals);
            LLVMValueRef init_val = emit_expr(g, init, locals);
            LLVMTypeRef init_type = LLVMTypeOf(init_val);
            LLVMValueRef alloca = emit_entry_alloca(g, init_type, "var");
            zan_store_fit(g, init_val, alloca);
            if (type) {
                /* reject a static type that cannot actually describe this value (e */
                LLVMTypeRef mt = map_type(g, type);
                bool shape_ok =
                    LLVMGetTypeKind(mt) == LLVMGetTypeKind(init_type);
                if (LLVMGetTypeKind(init_type) == LLVMPointerTypeKind)
                    shape_ok = LLVMGetTypeKind(mt) == LLVMPointerTypeKind ||
                        type->kind == TYPE_NULLABLE;
                if (!shape_ok) type = NULL;
            }
            if (!type) {
                if (LLVMGetTypeKind(init_type) == LLVMDoubleTypeKind) {
                    type = g->binder->type_double;
                } else if (LLVMGetTypeKind(init_type) == LLVMFloatTypeKind) {
                    type = g->binder->type_float;
                } else if (LLVMGetTypeKind(init_type) == LLVMPointerTypeKind) {
                    type = g->binder->type_string;
                } else if (LLVMGetTypeKind(init_type) == LLVMIntegerTypeKind) {
                    unsigned bits = LLVMGetIntTypeWidth(init_type);
                    if (bits <= 32) type = g->binder->type_int;
                    else type = g->binder->type_long;
                } else if (llvm_is_nullable(init_type)) {
                    /* `var v = maybe;` keeps the nullable type, so `v */
                    zan_type_t *it = infer_expr_type(g, init, locals);
                    type = (it && it->kind == TYPE_NULLABLE) ? it : g->binder->type_int;
                } else {
                    type = g->binder->type_int;
                }
            }
            if (type && type->kind == TYPE_NULLABLE &&
                !llvm_is_nullable(init_type)) {
                /* 内部辅助逻辑 */
                type = NULL;
            }
            if (type && type->kind == TYPE_NULLABLE) {
                /* keep the nullable type: `var v = maybe;` still answers v */
                LLVMTypeRef nst = map_type(g, type);
                LLVMValueRef nslot = emit_entry_alloca(g, nst, "var");
                zan_store_fit(g, init_val, nslot);
                local_add(locals, stmt->var_decl.name, nslot, type);
                return;
            }
            bool type_is_string = type && type->kind == TYPE_STRING;
            bool type_is_ptr_class = type &&
                (type->kind == TYPE_CLASS || type->kind == TYPE_INTERFACE ||
                 ((type->kind == TYPE_STRUCT) && type->sym &&
                  get_struct_llvm_type(g, type->sym) &&
                  LLVMGetTypeKind(get_struct_llvm_type(g, type->sym)) ==
                      LLVMPointerTypeKind));
            if (type_is_ptr_class || (type_is_string && init_val &&
                LLVMGetTypeKind(init_type) != LLVMPointerTypeKind)) {
                /* 内部辅助逻辑 */
                LLVMTypeRef llvm_t = map_type(g, type);
                LLVMValueRef slot = emit_entry_alloca(g, llvm_t, "var");
                zan_store_fit(g, LLVMConstNull(llvm_t), slot);
                emit_rc_capture_local(g, type, slot, init_val, init, locals);
                local_add(locals, stmt->var_decl.name, slot, type);
                arc_own_local(g, locals);
                return;
            }
            if (type_is_string) {
                LLVMTypeRef llvm_string = map_type(g, type);
                LLVMValueRef slot = emit_entry_alloca(g, llvm_string, "var");
                zan_store_fit(g, LLVMConstNull(llvm_string), slot);
                emit_rc_capture_local(g, type, slot, init_val, init, locals);
                local_add(locals, stmt->var_decl.name, slot, type);
                if (call_targets_extern(g, init))
                    locals->vars[locals->count - 1].opaque_string = 1;
                arc_own_local(g, locals);
                return;
            }
            if (type && is_rc_managed_type(type) &&
                LLVMGetTypeKind(init_type) == LLVMPointerTypeKind &&
                !type_is_ptr_class) {
                /* 内部辅助逻辑 */
                LLVMTypeRef llvm_t = map_type(g, type);
                LLVMValueRef slot = emit_entry_alloca(g, llvm_t, "var");
                zan_store_fit(g, LLVMConstNull(llvm_t), slot);
                emit_rc_capture_local(g, type, slot, init_val, init, locals);
                local_add(locals, stmt->var_decl.name, slot, type);
                arc_own_local(g, locals);
                return;
            }
            if (type && (type->kind == TYPE_ARRAY)) {
                /* 内部辅助逻辑 */
                LLVMTypeRef llvm_t = map_type(g, type);
                LLVMValueRef slot = emit_entry_alloca(g, llvm_t, "var");
                zan_store_fit(g, init_val, slot);
                local_add(locals, stmt->var_decl.name, slot, type);
                arc_own_local(g, locals);
                return;
            }
            local_add(locals, stmt->var_decl.name, alloca, type);
            return;
        }

        LLVMTypeRef llvm_type = map_type(g, type);
        LLVMValueRef alloca = (type && type->kind == TYPE_STRING)
            ? emit_entry_alloca(g, llvm_type, "var")
            : emit_entry_alloca(g, llvm_type, "var");

        /* ARC: a class-typed local holds an owning heap reference */
        int arc_own = (type && is_rc_managed_type(type) &&
                       LLVMGetTypeKind(llvm_type) == LLVMPointerTypeKind);
        int struct_own = (type && type->kind == TYPE_STRUCT &&
                          LLVMGetTypeKind(llvm_type) == LLVMStructTypeKind &&
                          type_contains_collection_rc(g, type, 0));
        /* Field replacement releases the old value even on its first write */
        if (arc_own || struct_own)
            zan_store_fit(g, LLVMConstNull(llvm_type), alloca);
        /* 内部辅助逻辑 */
        if (type && type->kind == TYPE_STRING && !stmt->var_decl.initializer) {
            zan_istr_t empty = { (char *)"", 0 };
            zan_store_fit(g, emit_string_literal_rc(g, empty), alloca);
        }
        /* 内部辅助逻辑 */
        local_var_t obj_slot;
        memset(&obj_slot, 0, sizeof(obj_slot));
        obj_slot.alloca = alloca;
        obj_slot.type = type;
        int obj_own = local_is_dyn_obj(g, &obj_slot);
        if (obj_own) zan_store_fit(g, LLVMConstNull(llvm_type), alloca);

        if (stmt->var_decl.initializer) {
            check_value_type_mismatch(g, type,
                infer_expr_type(g, stmt->var_decl.initializer, locals),
                stmt->var_decl.initializer, "initializer");
            check_generic_invariance(g, type,
                infer_expr_type(g, stmt->var_decl.initializer, locals),
                stmt->var_decl.initializer, "initializer");
            if (type && type->kind == TYPE_DELEGATE &&
                stmt->var_decl.initializer->kind != AST_LAMBDA)
                check_delegate_async_match(g, stmt->var_decl.initializer, type, locals);
            /* 内部辅助逻辑 */
            LLVMValueRef init_val = NULL;
            int init_owned = 0;
            if (type && type_is_binding(type) &&
                !type_is_binding(infer_expr_type(g, stmt->var_decl.initializer, locals))) {
                init_val = emit_binding_value(g, type,
                                              stmt->var_decl.initializer, locals);
                if (init_val) init_owned = 1;
            }
            if (!init_val)
                init_val =
                    (type && type->kind == TYPE_DELEGATE &&
                     stmt->var_decl.initializer->kind == AST_LAMBDA)
                        ? emit_lambda_typed(g, stmt->var_decl.initializer, type, locals)
                        : emit_expr(g, stmt->var_decl.initializer, locals);
            /* 内部辅助逻辑 */
            zan_ast_node_t *init_src = stmt->var_decl.initializer;
            bool conv_owned = false;
            /* 内部辅助逻辑 */
            bool bind_owned = init_owned;
            if (type && !bind_owned) {
                zan_type_t *ity = infer_expr_type(g, init_src, locals);
                if (ity) {
                    conv_owned =
                        (find_user_conversion(g, ity, type, "op_implicit") != NULL) &&
                        is_rc_managed_type(type);
                    init_val = emit_user_conversion(g, ity, type, "op_implicit",
                                                    init_val, init_src, locals);
                }
            }
            if (arc_own) {
                emit_rc_capture_local(g, type, alloca, init_val,
                    (conv_owned || bind_owned)
                        ? owned_rhs_marker(g, init_src->loc) : init_src,
                    locals);
            } else if (obj_own) {
                emit_obj_local_store(g, &obj_slot, init_val,
                    infer_expr_type(g, stmt->var_decl.initializer, locals),
                    stmt->var_decl.initializer, locals);
            } else if (type && type->kind == TYPE_STRUCT &&
                       type_contains_collection_rc(g, type, 0)) {
                /* A struct initializer is a field-wise copy */
                init_val = coerce_int_to(g, init_val, llvm_type);
                zan_store_fit(g, init_val, alloca);
                if (!expr_yields_owned_rc_value(g, init_src, locals) &&
                    !conv_owned)
                    emit_struct_local_retain(g, type, alloca);
            } else {
                init_val = coerce_int_to(g, init_val, llvm_type);
                zan_store_fit(g, init_val, alloca);
            }
        }

        /* An array variable initialized from a List (`string[] p = s */
        if (type && type->kind == TYPE_ARRAY && stmt->var_decl.initializer) {
            zan_type_t *it = infer_expr_type(g, stmt->var_decl.initializer, locals);
            /* 内部辅助逻辑 */
            if (it && it->kind != TYPE_ARRAY && it->name.str &&
                ((type_named(it, "List", 4)) ||
                 (type_named(it, "Dictionary", 10)) ||
                 (type_named(it, "Dict", 4))))
                zan_diag_emit(g->diag, DIAG_ERROR, stmt->loc,
                    "cannot initialize an array from '%.*s': declare the "
                    "variable as '%.*s' instead",
                    (int)it->name.len, it->name.str,
                    (int)it->name.len, it->name.str);
        }

        local_add(locals, stmt->var_decl.name, alloca, type);
        /* `string buf = calloc( */
        if (type && type->kind == TYPE_STRING && stmt->var_decl.initializer &&
            call_targets_extern(g, stmt->var_decl.initializer))
            locals->vars[locals->count - 1].opaque_string = 1;
        /* 内部辅助逻辑 */
        if (struct_own)
            locals->vars[locals->count - 1].struct_rc = 1;
        if (arc_own) arc_own_local(g, locals);
        if (obj_own)
            locals->vars[locals->count - 1].obj_rc_flag = obj_slot.obj_rc_flag;
        /* 内部辅助逻辑 */
        if (type &&
            ((type_named(type, "Dict", 4)) ||
             (type_named(type, "Dictionary", 10))) &&
            stmt->var_decl.initializer &&
            stmt->var_decl.initializer->kind == AST_NEW_EXPR)
            arc_own_local(g, locals);
        /* any `new T[n]` initializer: capture the element count so `a */
        if (type && type->kind == TYPE_ARRAY &&
            stmt->var_decl.initializer &&
            stmt->var_decl.initializer->kind == AST_NEW_EXPR &&
            stmt->var_decl.initializer->new_expr.is_array &&
            stmt->var_decl.initializer->new_expr.args.count > 0) {
            if (stmt->var_decl.initializer->new_expr.array_init) {
                /* new T[] { ... }: the length is the element count. */
                LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
                LLVMValueRef lslot = emit_entry_alloca(g, i64t, "arr.lenslot3");
                zan_store_fit(g, LLVMConstInt(i64t,
                    (unsigned long long)stmt->var_decl.initializer->new_expr.args.count, 0),
                    lslot);
                locals->vars[locals->count - 1].arr_len_slot = lslot;
                break;
            }
            zan_ast_node_t *sz = stmt->var_decl.initializer->new_expr.args.items[0];
            if (sz->kind == AST_INT_LITERAL || sz->kind == AST_IDENTIFIER) {
                LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
                LLVMValueRef szv = emit_expr(g, sz, locals);
                if (LLVMGetTypeKind(LLVMTypeOf(szv)) == LLVMIntegerTypeKind &&
                    LLVMGetIntTypeWidth(LLVMTypeOf(szv)) < 64)
                    szv = LLVMBuildSExt(g->builder, szv, i64t, "arr.len");
                LLVMValueRef lslot = emit_entry_alloca(g, i64t, "arr.lenslot2");
                zan_store_fit(g, szv, lslot);
                locals->vars[locals->count - 1].arr_len_slot = lslot;
            }
        }
        break;
    }

    case AST_TUPLE_DECON: {
        /* 内部辅助逻辑 */
        zan_ast_node_t *init = stmt->tuple_decon.initializer;
        zan_type_t *ttype = infer_expr_type(g, init, locals);
        if (!ttype || ttype->kind != TYPE_STRUCT || !ttype->sym) {
            zan_diag_emit(g->diag, DIAG_ERROR, stmt->loc,
                          "right-hand side of a deconstruction is not a tuple");
            break;
        }
        LLVMTypeRef st = get_struct_llvm_type(g, ttype->sym);
        if (!st) st = map_type(g, ttype); /* lazily register synthesized struct */
        LLVMValueRef tmp = NULL;
        if (st) {
            tmp = emit_entry_alloca(g, st, "dtmp");
            zan_store_fit(g, LLVMConstNull(st), tmp);
            if (init->kind == AST_TUPLE_EXPR) {
                /* 内部辅助逻辑 */
                int n = init->tuple_expr.items.count;
                for (int i = 0; i < n; i++) {
                    zan_ast_node_t *item = init->tuple_expr.items.items[i];
                    char fname[16];
                    snprintf(fname, sizeof fname, "Item%d", i + 1);
                    zan_istr_t f_istr = { fname, (uint32_t)strlen(fname) };
                    zan_symbol_t *fsym = get_field_sym(ttype->sym, f_istr);
                    if (!fsym) continue;
                    int fi = get_field_index(ttype->sym, f_istr);
                    if (fi < 0) fi = i;
                    LLVMValueRef fptr = emit_field_ptr(g, ttype->sym, st, tmp,
                                                       fi, "df");
                    LLVMValueRef fval = emit_arg_typed(g, item, fsym->type,
                                                       locals);
                    if (fsym->type && is_rc_managed_type(fsym->type) &&
                        LLVMGetTypeKind(LLVMTypeOf(fval)) ==
                            LLVMPointerTypeKind &&
                        !expr_yields_owned_rc_value(g, item, locals)) {
                        emit_rc_retain_for_type(g, fsym->type, fval);
                    } else if (fsym->type &&
                               fsym->type->kind == TYPE_STRUCT &&
                               type_contains_collection_rc(g, fsym->type, 0) &&
                               !expr_yields_owned_rc_value(g, item, locals)) {
                        emit_collection_value_retain(g, fsym->type, fval, 0);
                    }
                    zan_store_fit(g, fval, fptr);
                }
            } else {
                LLVMValueRef tv = emit_expr(g, init, locals);
                LLVMValueRef tvv =
                    LLVMGetTypeKind(LLVMTypeOf(tv)) == LLVMPointerTypeKind
                        ? LLVMBuildLoad2(g->builder, st, tv, "dtmp.load")
                        : tv;
                zan_store_fit(g, tvv, tmp);
            }
        }
        /* 内部辅助实现 */
        bool de_consumed[64];
        int de_fields = 0;
        while (de_fields < 64) {
            char dfn[16];
            snprintf(dfn, sizeof dfn, "Item%d", de_fields + 1);
            zan_istr_t di = { dfn, (uint32_t)strlen(dfn) };
            if (!get_field_sym(ttype->sym, di)) break;
            de_fields++;
        }
        for (int i = 0; i < 64; i++) de_consumed[i] = false;
        /* 内部辅助实现 */
        bool de_src_owns = expr_yields_owned_rc_value(g, init, locals);
        for (int i = 0; i < stmt->tuple_decon.names.count; i++) {
            zan_ast_node_t *nm = stmt->tuple_decon.names.items[i];
            zan_ast_node_t *ty = stmt->tuple_decon.types.items[i];
            if (!nm || nm->kind != AST_IDENTIFIER) continue;
            zan_type_t *et = ty ? resolve_type_ctx(g, ty) : NULL;
            int fi = -1;
            if (!et) {
                char fname[16];
                snprintf(fname, sizeof fname, "Item%d", i + 1);
                zan_istr_t f_istr = { fname, (uint32_t)strlen(fname) };
                zan_symbol_t *fsym = ttype->sym
                    ? get_field_sym(ttype->sym, f_istr) : NULL;
                et = fsym ? fsym->type : g->binder->type_int;
            }
            {
                char fname[16];
                snprintf(fname, sizeof fname, "Item%d", i + 1);
                zan_istr_t f_istr = { fname, (uint32_t)strlen(fname) };
                fi = get_field_index(ttype->sym, f_istr);
                if (fi < 0) fi = i;
            }
            LLVMTypeRef elt = map_type(g, et);
            LLVMValueRef slot = emit_entry_alloca(g, elt, "d");
            zan_store_fit(g, LLVMConstNull(elt), slot);
            LLVMValueRef de_fval = NULL;
            if (st && tmp) {
                LLVMValueRef fptr = emit_field_ptr(g, ttype->sym, st, tmp,
                                                   fi, "de");
                de_fval = LLVMBuildLoad2(g->builder, elt, fptr, "de.load");
                zan_store_fit(g, de_fval, slot);
            }
            local_add(locals, nm->ident.name, slot, et);
            /* 内部辅助逻辑 */
            if (et && is_rc_managed_type(et)) {
                if (!de_src_owns && de_fval &&
                    LLVMGetTypeKind(elt) == LLVMPointerTypeKind)
                    emit_rc_retain_for_type(g, et, de_fval);
                arc_own_local(g, locals);
            } else if (et && et->kind == TYPE_STRUCT &&
                       type_contains_collection_rc(g, et, 0)) {
                if (!de_src_owns && de_fval)
                    emit_collection_value_retain(g, et, de_fval, 0);
                locals->vars[locals->count - 1].struct_rc = 1;
            }
            if (fi >= 0 && fi < 64) de_consumed[fi] = true;
        }
        /* drop the +1s of fields no target consumed (owning sources only) */
        if (de_src_owns) {
            for (int i = 0; i < de_fields; i++) {
                if (de_consumed[i]) continue;
                char fname[16];
                snprintf(fname, sizeof fname, "Item%d", i + 1);
                zan_istr_t f_istr = { fname, (uint32_t)strlen(fname) };
                zan_symbol_t *fsym = ttype->sym
                    ? get_field_sym(ttype->sym, f_istr) : NULL;
                if (!fsym || !fsym->type) continue;
                if (!st || !tmp) break;
                int fi = get_field_index(ttype->sym, f_istr);
                if (fi < 0) fi = i;
                LLVMTypeRef felt = map_type(g, fsym->type);
                LLVMValueRef fptr = emit_field_ptr(g, ttype->sym, st, tmp,
                                                   fi, "dx");
                LLVMValueRef fval = LLVMBuildLoad2(g->builder, felt, fptr,
                                                   "dx.load");
                if (is_rc_managed_type(fsym->type) &&
                    LLVMGetTypeKind(felt) == LLVMPointerTypeKind)
                    emit_rc_release_for_type(g, fsym->type, fval);
                else if (fsym->type->kind == TYPE_STRUCT &&
                         type_contains_collection_rc(g, fsym->type, 0))
                    emit_collection_value_release(g, fsym->type, fval, 0);
            }
        }
        break;
    }

    case AST_EXPR_STMT: {
        zan_ast_node_t *e = stmt->expr_stmt.expr;
        LLVMValueRef ev = emit_expr(g, e, locals);
        /* 内部辅助实现 */
        if (ev && e->kind == AST_CALL && emit_detach_async_call(g, ev, false))
            break;
        /* 内部辅助逻辑 */
        if (ev && expr_yields_owned_rc_value(g, e, locals)) {
            zan_type_t *et = infer_expr_type(g, e, locals);
            if (et && is_rc_managed_type(et)) {
                LLVMTypeKind evk = LLVMGetTypeKind(LLVMTypeOf(ev));
                if (evk == LLVMPointerTypeKind) {
                    emit_rc_release_for_type(g, et, ev);
                } else if (evk == LLVMIntegerTypeKind &&
                           e->kind == AST_AWAIT_EXPR) {
                    LLVMValueRef p = emit_boundary_coerce(g, ev,
                        LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0));
                    emit_rc_release_for_type(g, et, p);
                }
            } else if (et && et->kind == TYPE_STRUCT &&
                       type_contains_collection_rc(g, et, 0)) {
                emit_collection_value_release(g, et, ev, 0);
            }
        }
        break;
    }

    case AST_RETURN_STMT:
        if (stmt->ret.value) {
            check_implicit_narrowing(g, g->current_fn_zan_ret_type,
                infer_expr_type(g, stmt->ret.value, locals),
                stmt->ret.value, "return");
        }
        /* 内部辅助实现 */
        if (g->current_async_frame) {
            LLVMValueRef ri = NULL, rv = NULL;
            zan_type_t *ret_type = g->current_async_ret_type;
            if (stmt->ret.value) {
                rv = emit_expr(g, stmt->ret.value, locals);
                zan_type_t *value_type = concretize(g,
                    infer_expr_type(g, stmt->ret.value, locals));
                if (!ret_type) ret_type = value_type;
                if (type_contains_collection_rc(g, value_type, 0) &&
                    !expr_yields_owned_rc_value(g, stmt->ret.value, locals))
                    emit_collection_value_retain(g, value_type, rv, 0);
                rv = coerce_async_ret(g, rv);
            }
            if (g->finally_count > 0) {
                if (rv) emit_async_pending_return(g, rv);
                emit_pending_finallys(g, locals, 0, rv != NULL);
                if (LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder)))
                    break;
                if (rv) ri = emit_async_pending_take_return(g);
            } else if (rv) {
                ri = coerce_to_frame_result(g, rv, ret_type);
            }
            emit_release_active_catch_excs(g, 0);
            emit_async_complete(g, locals, ri);
            break;
        }
        if (stmt->ret.value) {
            LLVMValueRef val = emit_expr(g, stmt->ret.value, locals);
            /* 内部辅助逻辑 */
            bool conv_ret = false;
            if (g->current_fn_zan_ret_type) {
                zan_type_t *vty = infer_expr_type(g, stmt->ret.value, locals);
                if (find_user_conversion(g, vty, g->current_fn_zan_ret_type,
                                         "op_implicit")) {
                    val = emit_user_conversion(g, vty, g->current_fn_zan_ret_type,
                                               "op_implicit", val,
                                               stmt->ret.value, locals);
                    conv_ret = true;
                }
            }
            /* ARC 返回契约：向调用方移交 +1 强引用所有权，随后释放本作用域局部变量 */
            zan_type_t *ret_type = concretize(g,
                infer_expr_type(g, stmt->ret.value, locals));
            if (conv_ret) ret_type = g->current_fn_zan_ret_type;
            if (!ret_type && g->current_fn_zan_ret_type)
                ret_type = g->current_fn_zan_ret_type;
            if (is_rc_managed_type(ret_type) &&
                !expr_yields_owned_rc_value(g, stmt->ret.value, locals) &&
                !conv_ret) {
                emit_rc_retain_for_type(g, ret_type, val);
            } else if (ret_type && ret_type->kind == TYPE_STRUCT &&
                       type_contains_collection_rc(g, ret_type, 0) &&
                       !expr_yields_owned_rc_value(g, stmt->ret.value, locals) &&
                       !conv_ret) {
                /* 内部辅助实现 */
                emit_collection_value_retain(g, ret_type, val, 0);
            }
            if (g->finally_count > 0) {
                LLVMValueRef v_slot =
                    emit_entry_alloca(g, LLVMTypeOf(val), "ret.fin.slot");
                zan_store_fit(g, val, v_slot);
                emit_pending_finallys(g, locals, 0, false);
                if (LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder)))
                    break;
                val = LLVMBuildLoad2(g->builder, LLVMTypeOf(val), v_slot,
                                     "ret.fin");
            }
            /* 内部辅助逻辑 */
            emit_release_owned_locals(g, locals);
            emit_release_active_catch_excs(g, 0);
            LLVMTypeRef fn_ret = g->current_fn_ret_type;
            LLVMTypeRef val_t = LLVMTypeOf(val);
            if (val_t != fn_ret) {
                if (llvm_is_nullable(fn_ret)) {
                    /* `return 5;` / `return null;` from a `T?` method. */
                    val = coerce_int_to(g, val, fn_ret);
                } else if (LLVMGetTypeKind(fn_ret) == LLVMFloatTypeKind &&
                    LLVMGetTypeKind(val_t) == LLVMDoubleTypeKind) {
                    val = LLVMBuildFPTrunc(g->builder, val, fn_ret, "rettrunc");
                } else if (LLVMGetTypeKind(fn_ret) == LLVMDoubleTypeKind &&
                           LLVMGetTypeKind(val_t) == LLVMFloatTypeKind) {
                    val = LLVMBuildFPExt(g->builder, val, fn_ret, "retext");
                } else if ((LLVMGetTypeKind(fn_ret) == LLVMDoubleTypeKind ||
                            LLVMGetTypeKind(fn_ret) == LLVMFloatTypeKind) &&
                           LLVMGetTypeKind(val_t) == LLVMIntegerTypeKind &&
                           LLVMGetIntTypeWidth(val_t) > 1) {
                    /* 内部辅助逻辑 */
                    val = LLVMBuildSIToFP(g->builder, val, fn_ret, "retsitofp");
                } else if (LLVMGetTypeKind(fn_ret) == LLVMIntegerTypeKind &&
                           LLVMGetTypeKind(val_t) == LLVMIntegerTypeKind) {
                    unsigned fn_bits = LLVMGetIntTypeWidth(fn_ret);
                    unsigned val_bits = LLVMGetIntTypeWidth(val_t);
                    if (fn_bits > val_bits) {
                        /* 内部辅助实现 */
                        val = val_bits <= 8
                            ? LLVMBuildZExt(g->builder, val, fn_ret, "retzext")
                            : LLVMBuildSExt(g->builder, val, fn_ret, "retext");
                    } else if (fn_bits < val_bits) {
                        val = LLVMBuildTrunc(g->builder, val, fn_ret, "rettrunc");
                    }
                } else if (LLVMGetTypeKind(fn_ret) == LLVMStructTypeKind &&
                           LLVMGetTypeKind(val_t) == LLVMPointerTypeKind) {
                    /* 内部辅助实现 */
                    val = LLVMBuildLoad2(g->builder, fn_ret, val, "ret.struct");
                } else if (LLVMGetTypeKind(fn_ret) == LLVMPointerTypeKind &&
                           LLVMGetTypeKind(val_t) == LLVMPointerTypeKind) {
                    /* e */
                    val = LLVMBuildBitCast(g->builder, val, fn_ret, "retcast");
                } else if (LLVMGetTypeKind(fn_ret) == LLVMPointerTypeKind ||
                           LLVMGetTypeKind(val_t) == LLVMPointerTypeKind) {
                    /* 内部辅助实现 */
                    val = emit_boundary_coerce(g, val, fn_ret);
                }
            }
            /* 内部辅助逻辑 */
            if (g->current_fn_is_main) emit_release_static_rc_fields(g, NULL);
            emit_eh_disarm_from(g, 0);
            LLVMBuildRet(g->builder, val);
        } else {
            emit_pending_finallys(g, locals, 0, false);
            if (LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder)))
                break;
            emit_release_owned_locals(g, locals);
            emit_release_active_catch_excs(g, 0);
            /* A bare `return;` normally maps to `ret void` */
            LLVMTypeRef fn_ret = g->current_fn_ret_type;
            if (g->current_fn_is_main) emit_release_static_rc_fields(g, NULL);
            emit_eh_disarm_from(g, 0);
            if (fn_ret && LLVMGetTypeKind(fn_ret) != LLVMVoidTypeKind) {
                LLVMBuildRet(g->builder, LLVMConstNull(fn_ret));
            } else {
                LLVMBuildRetVoid(g->builder);
            }
        }
        break;

    case AST_IF_STMT: {
        int then_start = locals->count;
        LLVMValueRef cond = emit_expr(g, stmt->if_stmt.cond, locals);
        cond = zan_tobool(g->builder, cond, "tobool");

        LLVMBasicBlockRef then_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "then");
        LLVMBasicBlockRef else_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "else");
        LLVMBasicBlockRef merge_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "merge");

        LLVMBuildCondBr(g->builder, cond, then_bb, stmt->if_stmt.else_body ? else_bb : merge_bb);

        LLVMPositionBuilderAtEnd(g->builder, then_bb);
        emit_stmt(g, stmt->if_stmt.then_body, locals);
        if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder))) {
            emit_release_owned_locals_from(g, locals, then_start);
            LLVMBuildBr(g->builder, merge_bb);
        } else {
            emit_release_owned_locals_from(g, locals, then_start);
        }

        int else_start = locals->count;
        LLVMPositionBuilderAtEnd(g->builder, else_bb);
        if (stmt->if_stmt.else_body) {
            emit_stmt(g, stmt->if_stmt.else_body, locals);
        }
        if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder))) {
            emit_release_owned_locals_from(g, locals, else_start);
            LLVMBuildBr(g->builder, merge_bb);
        } else {
            emit_release_owned_locals_from(g, locals, else_start);
        }

        LLVMPositionBuilderAtEnd(g->builder, merge_bb);
        break;
    }

    case AST_WHILE_STMT: {
        int body_start = locals->count;
        LLVMBasicBlockRef cond_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "while.cond");
        LLVMBasicBlockRef body_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "while.body");
        LLVMBasicBlockRef end_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "while.end");

        zan_irgen_pending_context_t saved_pending = g->pending;
        LLVMBasicBlockRef saved_break = g->break_target;
        LLVMBasicBlockRef saved_cont = g->continue_target;
        int saved_loop_base = g->loop_locals_base;
        int saved_loop_cbase = g->loop_catch_base;
        int saved_loop_fbase = g->finally_loop_base;
        g->pending.break_scope = g->pending.scope;
        g->pending.continue_scope = g->pending.scope;
        g->break_target = end_bb;
        g->continue_target = cond_bb;
        g->loop_locals_base = body_start;
        g->loop_catch_base = g->catch_cleanup_count;
        g->finally_loop_base = g->finally_count;
        int saved_loop_ehbase = g->eh_armed_loop_base;
        g->eh_armed_loop_base = g->eh_armed_count;

        LLVMBuildBr(g->builder, cond_bb);
        LLVMPositionBuilderAtEnd(g->builder, cond_bb);
        LLVMValueRef cond = emit_expr(g, stmt->while_stmt.cond, locals);
        cond = zan_tobool(g->builder, cond, "tobool");
        LLVMBuildCondBr(g->builder, cond, body_bb, end_bb);

        LLVMPositionBuilderAtEnd(g->builder, body_bb);
        emit_stmt(g, stmt->while_stmt.body, locals);
        if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder))) {
            emit_release_owned_locals_from(g, locals, body_start);
            /* 内部辅助逻辑 */
            if (!emit_async_preempt_site(g, cond_bb))
                LLVMBuildBr(g->builder, cond_bb);
        } else {
            emit_release_owned_locals_from(g, locals, body_start);
        }

        g->pending.break_scope = saved_pending.break_scope;
        g->pending.continue_scope = saved_pending.continue_scope;
        g->break_target = saved_break;
        g->continue_target = saved_cont;
        g->loop_locals_base = saved_loop_base;
        g->loop_catch_base = saved_loop_cbase;
        g->finally_loop_base = saved_loop_fbase;
        g->eh_armed_loop_base = saved_loop_ehbase;

        LLVMPositionBuilderAtEnd(g->builder, end_bb);
        break;
    }

    case AST_FOR_STMT: {
        int for_start = locals->count;
        if (stmt->for_stmt.init) emit_stmt(g, stmt->for_stmt.init, locals);
        /* Variables declared in the init clause capture per iteration */
        for (int i = for_start; i < locals->count; i++)
            locals->vars[i].per_iteration = 1;
        /* 内部辅助实现 */
        int for_body_start = locals->count;

        LLVMBasicBlockRef cond_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "for.cond");
        LLVMBasicBlockRef body_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "for.body");
        LLVMBasicBlockRef step_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "for.step");
        LLVMBasicBlockRef end_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "for.end");

        zan_irgen_pending_context_t saved_pending = g->pending;
        LLVMBasicBlockRef saved_break = g->break_target;
        LLVMBasicBlockRef saved_cont = g->continue_target;
        int saved_loop_base = g->loop_locals_base;
        int saved_loop_cbase = g->loop_catch_base;
        int saved_loop_fbase = g->finally_loop_base;
        g->pending.break_scope = g->pending.scope;
        g->pending.continue_scope = g->pending.scope;
        g->break_target = end_bb;
        g->continue_target = step_bb;
        g->loop_locals_base = for_body_start;
        g->loop_catch_base = g->catch_cleanup_count;
        g->finally_loop_base = g->finally_count;
        int saved_loop_ehbase = g->eh_armed_loop_base;
        g->eh_armed_loop_base = g->eh_armed_count;

        LLVMBuildBr(g->builder, cond_bb);
        LLVMPositionBuilderAtEnd(g->builder, cond_bb);
        if (stmt->for_stmt.cond) {
            LLVMValueRef cond = emit_expr(g, stmt->for_stmt.cond, locals);
            cond = zan_tobool(g->builder, cond, "tobool");
            LLVMBuildCondBr(g->builder, cond, body_bb, end_bb);
        } else {
            LLVMBuildBr(g->builder, body_bb);
        }

        LLVMPositionBuilderAtEnd(g->builder, body_bb);
        emit_stmt(g, stmt->for_stmt.body, locals);
        if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder))) {
            emit_release_owned_locals_from(g, locals, for_body_start);
            LLVMBuildBr(g->builder, step_bb);
        } else {
            emit_release_owned_locals_from(g, locals, for_body_start);
        }

        LLVMPositionBuilderAtEnd(g->builder, step_bb);
        if (stmt->for_stmt.step) {
            if (stmt->for_stmt.step->kind == AST_BLOCK ||
                stmt->for_stmt.step->kind == AST_EXPR_STMT) {
                emit_stmt(g, stmt->for_stmt.step, locals);
            } else {
                emit_expr(g, stmt->for_stmt.step, locals);
            }
        }
        /* 内部辅助逻辑 */
        if (!emit_async_preempt_site(g, cond_bb))
            LLVMBuildBr(g->builder, cond_bb);

        g->pending.break_scope = saved_pending.break_scope;
        g->pending.continue_scope = saved_pending.continue_scope;
        g->break_target = saved_break;
        g->continue_target = saved_cont;
        g->loop_locals_base = saved_loop_base;
        g->loop_catch_base = saved_loop_cbase;
        g->finally_loop_base = saved_loop_fbase;
        g->eh_armed_loop_base = saved_loop_ehbase;

        LLVMPositionBuilderAtEnd(g->builder, end_bb);
        /* 内部辅助逻辑 */
        emit_release_owned_locals_from(g, locals, for_start);
        break;
    }

    case AST_BREAK_STMT:
        if (g->break_target) {
            /* leaving every try entered inside this loop runs their finallys */
            emit_pending_finallys(g, locals, g->finally_loop_base, false);
            if (LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder)))
                break;
            emit_release_owned_locals_range(g, locals, g->loop_locals_base);
            emit_release_active_catch_excs(g, g->loop_catch_base);
            emit_eh_disarm_from(g, g->eh_armed_loop_base);
            emit_pending_scope_exit(g, g->pending.break_scope, false);
            LLVMBuildBr(g->builder, g->break_target);
        }
        break;

    case AST_CONTINUE_STMT:
        if (g->continue_target) {
            emit_pending_finallys(g, locals, g->finally_loop_base, false);
            if (LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder)))
                break;
            emit_release_owned_locals_range(g, locals, g->loop_locals_base);
            emit_release_active_catch_excs(g, g->loop_catch_base);
            emit_eh_disarm_from(g, g->eh_armed_loop_base);
            emit_pending_scope_exit(g, g->pending.continue_scope, false);
            LLVMBuildBr(g->builder, g->continue_target);
        }
        break;

    case AST_SWITCH_STMT: {
        int switch_start = locals->count;
        LLVMValueRef switch_val = emit_expr(g, stmt->switch_stmt.expr, locals);
        LLVMBasicBlockRef end_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "sw.end");

        /* count non-default cases */
        int num_cases = 0;
        zan_ast_node_t *default_case = NULL;
        for (int i = 0; i < stmt->switch_stmt.cases.count; i++) {
            zan_ast_node_t *sc = stmt->switch_stmt.cases.items[i];
            if (sc->switch_case.pattern || sc->switch_case.type_pattern ||
                sc->switch_case.when_cond || sc->switch_case.var_name.len > 0) {
                num_cases++;
            } else {
                default_case = sc;
            }
        }

        LLVMBasicBlockRef default_bb = default_case
            ? LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "sw.default")
            : end_bb;

        /* 内部辅助逻辑 */
        zan_irgen_pending_scope_t *sw_saved_pending_break = g->pending.break_scope;
        g->pending.break_scope = g->pending.scope;
        LLVMBasicBlockRef sw_saved_break = g->break_target;
        g->break_target = end_bb;
        /* 内部辅助逻辑 */
        int sw_saved_loop_base = g->loop_locals_base;
        g->loop_locals_base = switch_start;

        /* Pattern cases — `case T x:`, `case null:`, `case */
        bool has_patterns = false;
        for (int i = 0; i < stmt->switch_stmt.cases.count && !has_patterns; i++) {
            zan_ast_node_t *sc = stmt->switch_stmt.cases.items[i];
            if (sc->switch_case.type_pattern || sc->switch_case.when_cond ||
                sc->switch_case.var_name.len > 0 ||
                (sc->switch_case.pattern &&
                 sc->switch_case.pattern->kind == AST_NULL_LITERAL))
                has_patterns = true;
        }
        if (has_patterns) {
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
            LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
            bool sw_string = is_string_expr(g, stmt->switch_stmt.expr, locals);
            LLVMValueRef strcmp_fn = NULL;
            LLVMTypeRef strcmp_ty = NULL;
            if (sw_string) {
                strcmp_fn = LLVMGetNamedFunction(g->mod, "strcmp");
                strcmp_ty = LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0);
                if (!strcmp_fn) strcmp_fn = LLVMAddFunction(g->mod, "strcmp", strcmp_ty);
            }

            int nc = stmt->switch_stmt.cases.count;
            LLVMBasicBlockRef *pmatch = (LLVMBasicBlockRef *)calloc(
                (size_t)(nc > 0 ? nc : 1), sizeof(LLVMBasicBlockRef));
            int *psrc = (int *)calloc((size_t)(nc > 0 ? nc : 1), sizeof(int));
            int ci = 0;

            LLVMBasicBlockRef test_bb = LLVMAppendBasicBlockInContext(
                g->ctx, g->current_fn, "sw.ptest");
            LLVMBuildBr(g->builder, test_bb);

            for (int i = 0; i < nc; i++) {
                zan_ast_node_t *sc = stmt->switch_stmt.cases.items[i];
                if (!sc->switch_case.pattern && !sc->switch_case.type_pattern)
                    continue; /* default handled after the chain */
                LLVMPositionBuilderAtEnd(g->builder, test_bb);
                LLVMBasicBlockRef match_bb = LLVMAppendBasicBlockInContext(
                    g->ctx, g->current_fn, "sw.pmatch");
                /* 内部辅助逻辑 */
                LLVMBasicBlockRef fail_bb = LLVMAppendBasicBlockInContext(
                    g->ctx, g->current_fn, "sw.pnext");

                /* pattern variable: bind before the guard so `when` can read it */
                emit_switch_pattern_bind(g, locals, sc, switch_val);

                LLVMValueRef cond;
                if (sc->switch_case.type_pattern) {
                    zan_type_t *pt = resolve_type_ctx(g, sc->switch_case.type_pattern);
                    if (pt && (pt->kind == TYPE_CLASS || pt->kind == TYPE_STRING ||
                               pt->kind == TYPE_OBJECT || pt->kind == TYPE_INTERFACE))
                        cond = emit_runtime_is_check_name(g, switch_val, pt);
                    else {
                        /* 内部辅助实现 */
                        zan_type_t *dt = infer_expr_type(g, stmt->switch_stmt.expr,
                                                         locals);
                        cond = LLVMConstInt(LLVMInt1TypeInContext(g->ctx),
                            (pt && dt && types_equal(dt, pt)) ? 1 : 0, 0);
                    }
                } else if (sc->switch_case.pattern->kind == AST_NULL_LITERAL) {
                    cond = zan_icmp(g->builder, LLVMIntEQ, switch_val,
                        LLVMConstNull(LLVMTypeOf(switch_val)), "sw.null");
                } else if (sw_string) {
                    LLVMValueRef case_val = emit_expr(g, sc->switch_case.pattern, locals);
                    LLVMValueRef cmp = zan_call2(g->builder, strcmp_ty, strcmp_fn,
                        (LLVMValueRef[]){ switch_val, case_val }, 2, "swcmp");
                    cond = zan_icmp(g->builder, LLVMIntEQ, cmp,
                        LLVMConstInt(i32, 0, 0), "sweq");
                } else {
                    LLVMValueRef case_val = emit_expr(g, sc->switch_case.pattern, locals);
                    cond = zan_icmp(g->builder, LLVMIntEQ, switch_val, case_val,
                                    "sw.eq");
                }
                /* `when` guard: evaluated only after the pattern matched */
                if (sc->switch_case.when_cond) {
                    LLVMBasicBlockRef guard_bb = LLVMAppendBasicBlockInContext(
                        g->ctx, g->current_fn, "sw.pguard");
                    LLVMBuildCondBr(g->builder, cond, guard_bb, fail_bb);
                    LLVMPositionBuilderAtEnd(g->builder, guard_bb);
                    LLVMValueRef gv = emit_expr(g, sc->switch_case.when_cond, locals);
                    if (LLVMGetTypeKind(LLVMTypeOf(gv)) != LLVMIntegerTypeKind ||
                        LLVMGetIntTypeWidth(LLVMTypeOf(gv)) != 1)
                        gv = LLVMBuildICmp(g->builder, LLVMIntNE, gv,
                            LLVMConstInt(LLVMTypeOf(gv), 0, 0), "sw.guard");
                    LLVMBuildCondBr(g->builder, gv, match_bb, fail_bb);
                } else {
                    LLVMBuildCondBr(g->builder, cond, match_bb, fail_bb);
                }
                /* A failed type/guard match never enters the case body */
                LLVMPositionBuilderAtEnd(g->builder, fail_bb);
                emit_switch_pattern_fail_release(g, locals, sc);
                pmatch[ci] = match_bb;
                psrc[ci] = i;
                ci++;
                /* 内部辅助逻辑 */
                test_bb = LLVMGetInsertBlock(g->builder);
            }

            /* the last test's failure lands in default / end */
            LLVMPositionBuilderAtEnd(g->builder, test_bb);
            if (default_case)
                LLVMBuildBr(g->builder, default_bb);
            else
                LLVMBuildBr(g->builder, end_bb);

            /* case bodies */
            for (int k = 0; k < ci; k++) {
                int i = psrc[k];
                zan_ast_node_t *sc = stmt->switch_stmt.cases.items[i];
                LLVMPositionBuilderAtEnd(g->builder, pmatch[k]);
                bool case_empty = (sc->switch_case.body &&
                                   sc->switch_case.body->block.stmts.count == 0);
                if (case_empty) {
                    LLVMBasicBlockRef fall = NULL;
                    for (int k2 = k + 1; k2 < ci; k2++) {
                        zan_ast_node_t *nxt = stmt->switch_stmt.cases.items[psrc[k2]];
                        if (nxt->switch_case.body &&
                            nxt->switch_case.body->block.stmts.count > 0) {
                            fall = pmatch[k2];
                            break;
                        }
                    }
                    if (!fall) fall = default_bb;
                    LLVMBuildBr(g->builder, fall);
                    continue;
                }
                /* 内部辅助逻辑 */
                emit_switch_pattern_bind(g, locals, sc, switch_val);
                emit_stmt(g, sc->switch_case.body, locals);
                if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder))) {
                    emit_release_owned_locals_from(g, locals, switch_start);
                    LLVMBuildBr(g->builder, end_bb);
                } else {
                    emit_release_owned_locals_from(g, locals, switch_start);
                }
            }
            free(pmatch);
            free(psrc);

            if (default_case) {
                LLVMPositionBuilderAtEnd(g->builder, default_bb);
                emit_stmt(g, default_case->switch_case.body, locals);
                if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder))) {
                    emit_release_owned_locals_from(g, locals, switch_start);
                    LLVMBuildBr(g->builder, end_bb);
                } else {
                    emit_release_owned_locals_from(g, locals, switch_start);
                }
            }

            g->pending.break_scope = sw_saved_pending_break;
            g->break_target = sw_saved_break;
            g->loop_locals_base = sw_saved_loop_base;
            LLVMPositionBuilderAtEnd(g->builder, end_bb);
            break;
        }

        if (is_string_expr(g, stmt->switch_stmt.expr, locals)) {
            /* string switch: strcmp chain (LLVMBuildSwitch requires integers) */
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
            LLVMValueRef strcmp_fn = LLVMGetNamedFunction(g->mod, "strcmp");
            LLVMTypeRef strcmp_ty = LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0);
            if (!strcmp_fn) strcmp_fn = LLVMAddFunction(g->mod, "strcmp", strcmp_ty);

            LLVMBasicBlockRef *case_bbs = (LLVMBasicBlockRef *)calloc(
                (size_t)(num_cases > 0 ? num_cases : 1), sizeof(LLVMBasicBlockRef));
            int *case_src = (int *)calloc((size_t)(num_cases > 0 ? num_cases : 1),
                                          sizeof(int));
            int ci = 0;
            for (int i = 0; i < stmt->switch_stmt.cases.count; i++) {
                zan_ast_node_t *sc = stmt->switch_stmt.cases.items[i];
                if (!sc->switch_case.pattern) continue;
                LLVMValueRef case_val = emit_expr(g, sc->switch_case.pattern, locals);
                LLVMBasicBlockRef case_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "sw.case");
                LLVMBasicBlockRef next_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "sw.next");
                LLVMValueRef cmp = zan_call2(g->builder, strcmp_ty, strcmp_fn,
                    (LLVMValueRef[]){ switch_val, case_val }, 2, "swcmp");
                LLVMValueRef eq = zan_icmp(g->builder, LLVMIntEQ, cmp,
                    LLVMConstInt(i32, 0, 0), "sweq");
                LLVMBuildCondBr(g->builder, eq, case_bb, next_bb);
                LLVMPositionBuilderAtEnd(g->builder, next_bb);
                case_bbs[ci] = case_bb;
                case_src[ci] = i;
                ci++;
            }
            LLVMBuildBr(g->builder, default_bb);

            for (int k = 0; k < ci; k++) {
                int i = case_src[k];
                zan_ast_node_t *sc = stmt->switch_stmt.cases.items[i];
                LLVMPositionBuilderAtEnd(g->builder, case_bbs[k]);
                /* 内部辅助逻辑 */
                bool case_empty = (sc->switch_case.body &&
                                   sc->switch_case.body->block.stmts.count == 0);
                if (case_empty) {
                    LLVMBasicBlockRef fall_bb = NULL;
                    for (int k2 = k + 1; k2 < ci; k2++) {
                        zan_ast_node_t *nxt =
                            stmt->switch_stmt.cases.items[case_src[k2]];
                        if (nxt->switch_case.body &&
                            nxt->switch_case.body->block.stmts.count > 0) {
                            fall_bb = case_bbs[k2];
                            break;
                        }
                    }
                    if (!fall_bb) fall_bb = default_bb;
                    LLVMBuildBr(g->builder, fall_bb);
                    continue;
                }
                emit_stmt(g, sc->switch_case.body, locals);
                if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder))) {
                    emit_release_owned_locals_from(g, locals, switch_start);
                    LLVMBuildBr(g->builder, end_bb);
                } else {
                    emit_release_owned_locals_from(g, locals, switch_start);
                }
            }
            free(case_bbs);
            free(case_src);

            if (default_case) {
                LLVMPositionBuilderAtEnd(g->builder, default_bb);
                emit_stmt(g, default_case->switch_case.body, locals);
                if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder))) {
                    emit_release_owned_locals_from(g, locals, switch_start);
                    LLVMBuildBr(g->builder, end_bb);
                } else {
                    emit_release_owned_locals_from(g, locals, switch_start);
                }
            }

            g->pending.break_scope = sw_saved_pending_break;
            g->break_target = sw_saved_break;
            g->loop_locals_base = sw_saved_loop_base;
            LLVMPositionBuilderAtEnd(g->builder, end_bb);
            break;
        }

        LLVMValueRef sw = LLVMBuildSwitch(g->builder, switch_val, default_bb, (unsigned)num_cases);

        /* 内部辅助实现 */
        LLVMBasicBlockRef *case_bbs = (LLVMBasicBlockRef *)calloc(
            (size_t)(num_cases > 0 ? num_cases : 1), sizeof(LLVMBasicBlockRef));
        /* 内部辅助逻辑 */
        int *case_src = (int *)calloc((size_t)(num_cases > 0 ? num_cases : 1),
                                      sizeof(int));
        int nci = 0;
        /* pass 1: create a block for every case, add it to the switch */
        for (int i = 0; i < stmt->switch_stmt.cases.count; i++) {
            zan_ast_node_t *sc = stmt->switch_stmt.cases.items[i];
            if (!sc->switch_case.pattern) continue; /* default handled separately */

            LLVMBasicBlockRef case_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "sw.case");
            LLVMBasicBlockRef entry_bb = LLVMGetInsertBlock(g->builder);
            LLVMValueRef case_val = emit_expr(g, sc->switch_case.pattern, locals);
            /* 内部辅助实现 */
            if (!case_val || !LLVMIsAConstantInt(case_val)) {
                zan_diag_emit(g->diag, DIAG_ERROR, sc->loc,
                              "case label must be a compile-time constant "
                              "in an integer switch");
                LLVMPositionBuilderAtEnd(g->builder, case_bb);
                emit_release_owned_locals_from(g, locals, switch_start);
                LLVMBuildBr(g->builder, end_bb);
                LLVMPositionBuilderAtEnd(g->builder, entry_bb);
                continue;
            }
            /* 内部辅助逻辑 */
            LLVMTypeRef swty = LLVMTypeOf(switch_val);
            if (LLVMGetTypeKind(LLVMTypeOf(case_val)) == LLVMIntegerTypeKind &&
                LLVMGetTypeKind(swty) == LLVMIntegerTypeKind &&
                LLVMTypeOf(case_val) != swty) {
                long long cv = LLVMConstIntGetSExtValue(case_val);
                case_val = LLVMConstInt(swty, (unsigned long long)cv, 1);
            }
            LLVMAddCase(sw, case_val, case_bb);
            case_bbs[nci] = case_bb;
            case_src[nci] = i;
            nci++;
        }

        /* 内部辅助逻辑 */
        for (int k = 0; k < nci; k++) {
            int i = case_src[k];
            zan_ast_node_t *sc = stmt->switch_stmt.cases.items[i];
            LLVMBasicBlockRef case_bb = case_bbs[k];
            bool case_empty = (sc->switch_case.body &&
                               sc->switch_case.body->block.stmts.count == 0);
            /* 内部辅助逻辑 */
            LLVMBasicBlockRef fall_bb = NULL;
            if (case_empty) {
                for (int k2 = k + 1; k2 < nci; k2++) {
                    zan_ast_node_t *nxt =
                        stmt->switch_stmt.cases.items[case_src[k2]];
                    if (nxt->switch_case.body &&
                        nxt->switch_case.body->block.stmts.count > 0) {
                        fall_bb = case_bbs[k2];
                        break;
                    }
                }
                if (!fall_bb) fall_bb = default_bb;
            }

            LLVMPositionBuilderAtEnd(g->builder, case_bb);
            if (case_empty) {
                LLVMBuildBr(g->builder, fall_bb);
                continue;
            }
            emit_stmt(g, sc->switch_case.body, locals);
            if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder))) {
                emit_release_owned_locals_from(g, locals, switch_start);
                LLVMBuildBr(g->builder, end_bb);
            } else {
                emit_release_owned_locals_from(g, locals, switch_start);
            }
        }
        free(case_bbs);
        free(case_src);

        if (default_case) {
            LLVMPositionBuilderAtEnd(g->builder, default_bb);
            emit_stmt(g, default_case->switch_case.body, locals);
            if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder))) {
                emit_release_owned_locals_from(g, locals, switch_start);
                LLVMBuildBr(g->builder, end_bb);
            } else {
                emit_release_owned_locals_from(g, locals, switch_start);
            }
        }

        g->pending.break_scope = sw_saved_pending_break;
            g->break_target = sw_saved_break;
        g->loop_locals_base = sw_saved_loop_base;
        LLVMPositionBuilderAtEnd(g->builder, end_bb);
        break;
    }

    case AST_TRY_STMT: {
        /* 内部辅助实现 */
        LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
        LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
        LLVMValueRef top_g, bufs_g, exc_g;
        get_eh_globals(g, &top_g, &bufs_g, &exc_g);
        LLVMValueRef fn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder));
        LLVMBasicBlockRef try_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "try.body");
        LLVMBasicBlockRef catch_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "try.catch");
        LLVMBasicBlockRef end_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "try.end");

        LLVMValueRef eh_tmptop_g = get_eh_tmp_top_global(g);
        /* 内部辅助逻辑 */
        int async_hid = -1;
        LLVMValueRef tmp_mark = LLVMBuildLoad2(g->builder, i32t, eh_tmptop_g, "eh.tmpmark");
        LLVMValueRef old_top = LLVMBuildLoad2(g->builder, i32t, top_g, "eh.old");
        /* 内部辅助逻辑 */
        LLVMValueRef tmp_mark_slot = emit_entry_alloca(g, i32t, "eh.tmpmark.slot");
        zan_store_fit(g, tmp_mark, tmp_mark_slot);
        LLVMValueRef old_top_slot = emit_entry_alloca(g, i32t, "eh.old.slot");
        zan_store_fit(g, old_top, old_top_slot);
        LLVMValueRef new_top = zan_add(g->builder, old_top,
            LLVMConstInt(i32t, 1, 0), "eh.new");
        zan_store_fit(g, new_top, top_g);
        /* 内部辅助逻辑 */
        zan_store_fit(g, tmp_mark, emit_eh_mark_ptr(g, new_top));
        /* An async frame records how many try handlers it currently has armed (frame */
        if (g->current_async_frame) {
            LLVMValueRef hc_ptr = LLVMBuildStructGEP2(g->builder,
                g->current_async_frame_type, g->current_async_frame,
                ASYNC_FRAME_HCOUNT, "eh.hc");
            LLVMValueRef hc = LLVMBuildLoad2(g->builder, i32t, hc_ptr, "eh.hcv");
            zan_store_fit(g, zan_add(g->builder, hc,
                LLVMConstInt(i32t, 1, 0), "eh.hc1"), hc_ptr);
            /* 内部辅助实现 */
            async_hid = g->current_async_handler_next++;
            if (LLVMGetArrayLength(LLVMStructGetTypeAtIndex(
                    g->current_async_frame_type, ASYNC_FRAME_PENDING))) {
                LLVMTypeRef hp_type = LLVMStructGetTypeAtIndex(
                    g->current_async_frame_type, ASYNC_FRAME_HPENDING);
                LLVMValueRef hp_indices[] = { LLVMConstInt(i32t, 0, 0),
                    LLVMConstInt(i32t, (unsigned)async_hid, 0) };
                LLVMValueRef hp = LLVMBuildGEP2(g->builder, hp_type,
                    LLVMBuildStructGEP2(g->builder, g->current_async_frame_type,
                        g->current_async_frame, ASYNC_FRAME_HPENDING, "eh.pending.p"),
                    hp_indices, 2, "eh.pending.slot");
                LLVMBuildStore(g->builder, LLVMBuildLoad2(g->builder, i32t,
                    async_pending_count_ptr(g, g->current_async_frame,
                        g->current_async_frame_type), "eh.pending.depth"), hp);
            }
            if (g->current_async_rearm_switch) {
                int hid = async_hid;
                LLVMValueRef hs = LLVMBuildStructGEP2(g->builder,
                    g->current_async_frame_type, g->current_async_frame,
                    ASYNC_FRAME_HSTACK, "eh.hs");
                LLVMValueRef hs_idx[2] = { LLVMConstInt(i32t, 0, 0), hc };
                LLVMValueRef hs_slot = LLVMBuildGEP2(g->builder,
                    LLVMArrayType(i32t, (unsigned)g->current_async_handler_cap),
                    hs, hs_idx, 2, "eh.hs.slot");
                zan_store_fit(g, LLVMConstInt(i32t, (unsigned)hid, 0), hs_slot);

                LLVMBasicBlockRef here = LLVMGetInsertBlock(g->builder);
                struct { const char *nm; LLVMValueRef sw; LLVMBasicBlockRef dst; } re[2] = {
                    { "try.rearm.init", g->current_async_rearm_init_switch,
                      g->current_async_rearm_next_bb },
                    { "try.rearm", g->current_async_rearm_switch, catch_bb }
                };
                for (int ri = 0; ri < 2; ri++) {
                    if (!re[ri].sw) continue;
                    LLVMBasicBlockRef re_bb = LLVMAppendBasicBlockInContext(g->ctx, fn,
                        re[ri].nm);
                    LLVMPositionBuilderAtEnd(g->builder, re_bb);
                    /* 内部辅助逻辑 */
                    LLVMValueRef rtop = LLVMBuildLoad2(g->builder, i32t, top_g, "eh.rtop");
                    zan_store_fit(g, zan_sub(g->builder, rtop,
                        LLVMConstInt(i32t, 1, 0), "eh.rtop0"), old_top_slot);
                    zan_store_fit(g,
                        LLVMBuildLoad2(g->builder, i32t, eh_tmptop_g, "eh.rtmp"),
                        tmp_mark_slot);
                    LLVMBuildBr(g->builder, re[ri].dst);
                    LLVMAddCase(re[ri].sw, LLVMConstInt(i32t, (unsigned)hid, 0), re_bb);
                }
                LLVMPositionBuilderAtEnd(g->builder, here);
            }

        }
        LLVMValueRef zero = LLVMConstInt(i32t, 0, 0);
        LLVMValueRef bufp = emit_eh_buf_ptr(g, new_top);
        /* 内部辅助实现 */
        bool wasm_try = false;
        LLVMBasicBlockRef saved_lpad = NULL;
        int saved_try_depth = 0;
        if (!g->target_is_wasm) {
            LLVMValueRef r = emit_eh_setjmp(g, bufp);
            LLVMValueRef took = zan_icmp(g->builder, LLVMIntEQ, r, zero, "eh.took");
            LLVMBuildCondBr(g->builder, took, try_bb, catch_bb);
        } else {
            LLVMValueRef r = LLVMBuildLoad2(g->builder, i32t, old_top_slot,
                                            "eh.old.nowasm");
            LLVMBuildCondBr(g->builder,
                zan_icmp(g->builder, LLVMIntEQ, r, r, "eh.always"), try_bb,
                catch_bb);
        }
        (void)bufp;

        LLVMPositionBuilderAtEnd(g->builder, try_bb);
        int try_start = locals->count;
        /* 内部辅助逻辑 */
        int saved_throw_base = g->throw_locals_base;
        int saved_throw_cbase = g->throw_catch_base;
        g->throw_locals_base = try_start;
        g->throw_catch_base = g->catch_cleanup_count;
        /* 内部辅助逻辑 */
        int fin_idx = -1;
        if (stmt->try_stmt.finally_body &&
            g->finally_count < ZAN_MAX_FINALLY_DEPTH) {
            fin_idx = g->finally_count++;
            g->finallys[fin_idx].body = stmt->try_stmt.finally_body;
            g->finallys[fin_idx].monitor_obj = NULL;
            g->finallys[fin_idx].continuation_slot = NULL;
            g->finallys[fin_idx].shared = NULL;
            g->finallys[fin_idx].pending_parent = g->pending.scope;
            g->finallys[fin_idx].pending_scope = NULL;
            if (g->current_async_frame) {
                zan_irgen_pending_scope_t *scope = zan_arena_alloc(g->arena, sizeof(*scope));
                scope->parent = g->pending.scope;
                scope->handler_id = async_hid;
                g->finallys[fin_idx].pending_scope = scope;
            }
            g->finallys[fin_idx].outer_armed_depth = g->eh_armed_count;
            g->finallys[fin_idx].outer_throw_locals_base = saved_throw_base;
            g->finallys[fin_idx].outer_throw_catch_base = saved_throw_cbase;
            if (g->current_async_frame) {
                local_var_t *selector = local_find_async_decl(locals, stmt);
                if (selector)
                    g->finallys[fin_idx].continuation_slot = selector->alloca;
            }
            g->finallys[fin_idx].in_try_body = true;
        } else if (stmt->try_stmt.finally_body) {
            zan_diag_emit(g->diag, DIAG_ERROR, stmt->loc,
                "too many nested try/finally blocks in one function body");
        }
        int armed_idx = -1;
        if (g->eh_armed_count < ZAN_MAX_ARMED_TRY) {
            armed_idx = g->eh_armed_count++;
            g->eh_armed[armed_idx].old_top_slot = old_top_slot;
        }
        emit_stmt(g, stmt->try_stmt.try_body, locals);
        /* 内部辅助逻辑 */
        if (armed_idx >= 0) g->eh_armed_count = armed_idx;
        if (fin_idx >= 0) g->finallys[fin_idx].in_try_body = false;
        g->throw_locals_base = saved_throw_base;
        g->throw_catch_base = saved_throw_cbase;
        if (wasm_try) g->wasm_try_depth = saved_try_depth;
        locals->count = try_start;
        if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder))) {
            LLVMValueRef ot = LLVMBuildLoad2(g->builder, i32t, old_top_slot, "eh.old.re");
            zan_store_fit(g, ot, top_g);
            if (g->current_async_frame) {
                LLVMValueRef hc_ptr = LLVMBuildStructGEP2(g->builder,
                    g->current_async_frame_type, g->current_async_frame,
                    ASYNC_FRAME_HCOUNT, "eh.hc");
                LLVMValueRef hc = LLVMBuildLoad2(g->builder, i32t, hc_ptr, "eh.hcv");
                zan_store_fit(g, zan_sub(g->builder, hc,
                    LLVMConstInt(i32t, 1, 0), "eh.hc0"), hc_ptr);
            }
            LLVMBuildBr(g->builder, end_bb);
        }

        LLVMPositionBuilderAtEnd(g->builder, catch_bb);
        {
            LLVMValueRef ot = LLVMBuildLoad2(g->builder, i32t, old_top_slot, "eh.old.re");
            zan_store_fit(g, ot, top_g);
            if (g->current_async_frame) {
                LLVMValueRef hc_ptr = LLVMBuildStructGEP2(g->builder,
                    g->current_async_frame_type, g->current_async_frame,
                    ASYNC_FRAME_HCOUNT, "eh.hc");
                LLVMValueRef hc = LLVMBuildLoad2(g->builder, i32t, hc_ptr, "eh.hcv");
                zan_store_fit(g, zan_sub(g->builder, hc,
                    LLVMConstInt(i32t, 1, 0), "eh.hc0"), hc_ptr);
                /* 内部辅助实现 */
            }
        }
        /* 内部辅助实现 */
        {
            LLVMTypeRef uwty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), &i32t, 1, 0);
            LLVMValueRef t1 = zan_add(g->builder,
                LLVMBuildLoad2(g->builder, i32t, old_top_slot, "eh.old.re2"),
                LLVMConstInt(i32t, 1, 0), "eh.buf1");
            LLVMValueRef tm = LLVMBuildLoad2(g->builder, i32t,
                emit_eh_mark_ptr(g, t1), "eh.bufmark");
            zan_call2(g->builder, uwty, get_eh_tmp_unwind_fn(g), &tm, 1, "");
        }
        if (async_hid >= 0 && LLVMGetArrayLength(LLVMStructGetTypeAtIndex(
                g->current_async_frame_type, ASYNC_FRAME_PENDING))) {
            LLVMTypeRef ft = g->current_async_frame_type;
            LLVMTypeRef hp_type = LLVMStructGetTypeAtIndex(ft, ASYNC_FRAME_HPENDING);
            LLVMValueRef indices[] = { LLVMConstInt(i32t, 0, 0),
                LLVMConstInt(i32t, (unsigned)async_hid, 0) };
            LLVMValueRef hp = LLVMBuildGEP2(g->builder, hp_type,
                LLVMBuildStructGEP2(g->builder, ft, g->current_async_frame,
                    ASYNC_FRAME_HPENDING, "eh.pending.p"), indices, 2, "eh.pending.slot");
            emit_async_pending_discard(g, g->current_async_frame, ft,
                LLVMBuildLoad2(g->builder, i32t, hp, "eh.pending.depth"));
        }
        LLVMValueRef exc_val = LLVMBuildLoad2(g->builder, i8ptr, exc_g, "exc");
        /* 内部辅助实现 */
        LLVMValueRef exc_slot, exc_owned_slot, exc_tid_slot;
        if (g->current_async_frame && async_hid >= 0 &&
            async_hid < g->current_async_handler_cap) {
            /* 内部辅助实现 */
            LLVMValueRef cidx[2] = { LLVMConstInt(i32t, 0, 0),
                                     LLVMConstInt(i32t, (unsigned)async_hid, 0) };
            LLVMBasicBlockRef cur_bb = LLVMGetInsertBlock(g->builder);
            LLVMBasicBlockRef entry_bb = LLVMGetEntryBasicBlock(
                LLVMGetBasicBlockParent(cur_bb));
            LLVMValueRef entry_term = LLVMGetBasicBlockTerminator(entry_bb);
            if (entry_term) LLVMPositionBuilderBefore(g->builder, entry_term);
            else LLVMPositionBuilderAtEnd(g->builder, entry_bb);
            exc_slot = LLVMBuildGEP2(g->builder,
                LLVMArrayType(i8ptr, (unsigned)g->current_async_handler_cap),
                LLVMBuildStructGEP2(g->builder, g->current_async_frame_type,
                    g->current_async_frame, ASYNC_FRAME_CEXC, "eh.cexc"),
                cidx, 2, "eh.exc.slot");
            exc_owned_slot = LLVMBuildGEP2(g->builder,
                LLVMArrayType(i32t, (unsigned)g->current_async_handler_cap),
                LLVMBuildStructGEP2(g->builder, g->current_async_frame_type,
                    g->current_async_frame, ASYNC_FRAME_CEXC_OWNED, "eh.cexcown"),
                cidx, 2, "eh.excown.slot");
            exc_tid_slot = LLVMBuildGEP2(g->builder,
                LLVMArrayType(i8ptr, (unsigned)g->current_async_handler_cap),
                LLVMBuildStructGEP2(g->builder, g->current_async_frame_type,
                    g->current_async_frame, ASYNC_FRAME_CEXC_TID, "eh.cexctid"),
                cidx, 2, "eh.exctid.slot");
            LLVMPositionBuilderAtEnd(g->builder, cur_bb);
        } else {
            exc_slot = emit_entry_alloca(g, i8ptr, "eh.exc.slot");
            /* 内部辅助逻辑 */
            exc_owned_slot = emit_entry_alloca(g, i32t, "eh.excown.slot");
            /* 内部辅助逻辑 */
            exc_tid_slot = emit_entry_alloca(g, i8ptr, "eh.exctid.slot");
        }
        zan_store_fit(g, exc_val, exc_slot);
        zan_store_fit(g, LLVMConstInt(i32t, 0, 0), exc_owned_slot);
        zan_store_fit(g, LLVMConstNull(i8ptr), exc_tid_slot);
        int catch_start = locals->count;
        int ncatch = stmt->try_stmt.catches.count;
        if (ncatch > 0) {
            /* 内部辅助逻辑 */
            LLVMValueRef tid_g = get_eh_exc_tid_global(g);
            LLVMValueRef thrown_tid = LLVMBuildLoad2(g->builder, i8ptr, tid_g, "exc.tid");
            zan_store_fit(g, thrown_tid, exc_tid_slot);
            LLVMValueRef match_fn = get_eh_tid_match_fn(g);
            LLVMTypeRef match_ty = LLVMFunctionType(LLVMInt1TypeInContext(g->ctx),
                (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0);
            LLVMBasicBlockRef done_bb =
                LLVMAppendBasicBlockInContext(g->ctx, fn, "catch.done");
            LLVMBasicBlockRef rethrow_bb =
                LLVMAppendBasicBlockInContext(g->ctx, fn, "catch.rethrow");

            for (int ci = 0; ci < ncatch; ci++) {
                zan_ast_node_t *cc = stmt->try_stmt.catches.items[ci];
                zan_type_t *et = cc->catch_clause.type
                    ? resolve_type_ctx(g, cc->catch_clause.type)
                    : NULL;
                LLVMBasicBlockRef body_bb =
                    LLVMAppendBasicBlockInContext(g->ctx, fn, "catch.body");
                LLVMBasicBlockRef miss_bb = (ci + 1 < ncatch)
                    ? LLVMAppendBasicBlockInContext(g->ctx, fn, "catch.test")
                    : rethrow_bb;
                if (et && et->kind == TYPE_CLASS && et->sym) {
                    LLVMValueRef want = LLVMBuildBitCast(g->builder,
                        get_class_tid_global(g, et->sym), i8ptr, "want.tid");
                    LLVMValueRef hits = zan_call2(g->builder, match_ty, match_fn,
                        (LLVMValueRef[]){ thrown_tid, want }, 2, "tid.hit");
                    LLVMBuildCondBr(g->builder, hits, body_bb, miss_bb);
                } else {
                    /* untyped / non-class clause: catches everything */
                    LLVMBuildBr(g->builder, body_bb);
                    if (miss_bb != rethrow_bb) {
                        /* unreachable later tests still need a terminator */
                        LLVMPositionBuilderAtEnd(g->builder, miss_bb);
                        LLVMBuildBr(g->builder, rethrow_bb);
                    }
                    miss_bb = NULL;
                }

                LLVMPositionBuilderAtEnd(g->builder, body_bb);
                if (cc->catch_clause.var_name.len > 0) {
                    LLVMValueRef ev = LLVMBuildLoad2(g->builder, i8ptr, exc_slot, "exc.re");
                    /* 内部辅助实现 */
                    LLVMValueRef ea = NULL;
                    if (g->current_async_frame) {
                        /* 内部辅助逻辑 */
                        local_var_t *fv = local_find_async_decl(locals, cc);
                        if (fv && !fv->type &&
                            LLVMGetTypeKind(local_slot_type(g, fv)) == LLVMPointerTypeKind)
                            ea = fv->alloca;
                    }
                    if (!ea) ea = emit_entry_alloca(g, i8ptr, "exc.var");
                    zan_store_fit(g, ev, ea);
                    local_add(locals, cc->catch_clause.var_name, ea, et);
                }
                /* 内部辅助实现 */
                {
                    LLVMValueRef owned_g2 = get_eh_exc_owned_global(g);
                    LLVMValueRef ofl = LLVMBuildLoad2(g->builder, i32t, owned_g2, "exc.ofl");
                    zan_store_fit(g, ofl, exc_owned_slot);
                    zan_store_fit(g, LLVMConstInt(i32t, 0, 0), owned_g2);
                    LLVMValueRef isown = zan_icmp(g->builder, LLVMIntNE, ofl,
                        LLVMConstInt(i32t, 0, 0), "exc.isown");
                    LLVMBasicBlockRef push_bb =
                        LLVMAppendBasicBlockInContext(g->ctx, fn, "exc.push");
                    LLVMBasicBlockRef pcont_bb =
                        LLVMAppendBasicBlockInContext(g->ctx, fn, "exc.pcont");
                    LLVMBuildCondBr(g->builder, isown, push_bb, pcont_bb);
                    LLVMPositionBuilderAtEnd(g->builder, push_bb);
                    LLVMValueRef ev2 = LLVMBuildLoad2(g->builder, i8ptr, exc_slot, "exc.re");
                    emit_eh_tmp_push(g, ev2);
                    LLVMBuildBr(g->builder, pcont_bb);
                    LLVMPositionBuilderAtEnd(g->builder, pcont_bb);
                }
                /* 内部辅助逻辑 */
                if (ZAN_TAB_ENSURE(g->catch_cleanups, g->catch_cleanup_count,
                                   g->catch_cleanup_cap, 16)) {
                    g->catch_cleanups[g->catch_cleanup_count].exc_slot = exc_slot;
                    g->catch_cleanups[g->catch_cleanup_count].owned_slot =
                        exc_owned_slot;
                    g->catch_cleanups[g->catch_cleanup_count].tid_slot =
                        exc_tid_slot;
                    g->catch_cleanup_count++;
                } else {
                    zan_diag_emit(g->diag, DIAG_ERROR, stmt->loc,
                        "out of memory tracking nested catch handlers");
                }
                int saved_cc = g->catch_cleanup_count;
                emit_stmt(g, cc->catch_clause.body, locals);
                g->catch_cleanup_count = saved_cc - 1;
                locals->count = catch_start;
                if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder)))
                    LLVMBuildBr(g->builder, done_bb);

                if (!miss_bb) break;   /* catch-all consumed the rest */
                LLVMPositionBuilderAtEnd(g->builder, miss_bb);
            }
            /* 内部辅助逻辑 */
            if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder)) &&
                LLVMGetInsertBlock(g->builder) != rethrow_bb)
                LLVMBuildBr(g->builder, rethrow_bb);

            /* 内部辅助实现 */
            LLVMPositionBuilderAtEnd(g->builder, rethrow_bb);
            emit_finally_on_exception_path(g, locals, fin_idx);
            /* 内部辅助逻辑 */
            emit_finallys_below_for_propagate(g, locals, fin_idx);
            if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder)))
                emit_eh_propagate_tail(g);

            LLVMPositionBuilderAtEnd(g->builder, done_bb);
        } else {
            /* 内部辅助逻辑 */
            emit_finally_on_exception_path(g, locals, fin_idx);
            emit_finallys_below_for_propagate(g, locals, fin_idx);
            if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder)))
                emit_eh_propagate_tail(g);
        }
        locals->count = catch_start;
        if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder))) {
            /* 内部辅助实现 */
            LLVMValueRef cfn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder));
            LLVMValueRef hofl = LLVMBuildLoad2(g->builder, i32t, exc_owned_slot,
                "exc.hown");
            LLVMValueRef h_owned = zan_icmp(g->builder, LLVMIntNE, hofl,
                LLVMConstInt(i32t, 0, 0), "exc.hisown");
            LLVMBasicBlockRef hrel_bb = LLVMAppendBasicBlockInContext(g->ctx, cfn, "exc.hrel");
            LLVMBasicBlockRef hcont_bb = LLVMAppendBasicBlockInContext(g->ctx, cfn, "exc.hcont");
            LLVMBuildCondBr(g->builder, h_owned, hrel_bb, hcont_bb);
            LLVMPositionBuilderAtEnd(g->builder, hrel_bb);
            emit_eh_tmp_pop(g);
            LLVMValueRef hev = LLVMBuildLoad2(g->builder, i8ptr, exc_slot, "exc.hre");
            zan_store_fit(g, LLVMConstInt(i32t, 0, 0), exc_owned_slot);
            zan_store_fit(g, LLVMConstNull(i8ptr), exc_slot);
            zan_call2(g->builder,
                LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), &i8ptr, 1, 0),
                g->rt_release_dyn, &hev, 1, "");
            zan_store_fit(g, LLVMConstNull(i8ptr), exc_g);
            LLVMBuildBr(g->builder, hcont_bb);
            LLVMPositionBuilderAtEnd(g->builder, hcont_bb);
            LLVMBuildBr(g->builder, end_bb);
        }

        /* 内部辅助逻辑 */
        if (fin_idx >= 0) g->finally_count = fin_idx;
        LLVMPositionBuilderAtEnd(g->builder, end_bb);
        if (stmt->try_stmt.finally_body) {
            zan_irgen_finally_entry_t entry = g->finallys[fin_idx];
            emit_finally_body(g, locals, &entry);
        }
        break;
    }

    case AST_DO_WHILE_STMT: {
        int body_start = locals->count;
        LLVMValueRef fn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder));
        LLVMBasicBlockRef body_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "do.body");
        LLVMBasicBlockRef cond_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "do.cond");
        LLVMBasicBlockRef end_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "do.end");
        zan_irgen_pending_context_t saved_pending = g->pending;
        LLVMBasicBlockRef saved_break = g->break_target;
        LLVMBasicBlockRef saved_cont = g->continue_target;
        int saved_loop_base = g->loop_locals_base;
        int saved_loop_cbase = g->loop_catch_base;
        int saved_loop_fbase = g->finally_loop_base;
        int saved_loop_ehbase = g->eh_armed_loop_base;
        /* 内部辅助实现 */
        g->pending.break_scope = g->pending.scope;
        g->pending.continue_scope = g->pending.scope;
        g->break_target = end_bb;
        g->continue_target = cond_bb;
        g->loop_locals_base = body_start;
        g->loop_catch_base = g->catch_cleanup_count;
        g->finally_loop_base = g->finally_count;
        g->eh_armed_loop_base = g->eh_armed_count;
        LLVMBuildBr(g->builder, body_bb);

        LLVMPositionBuilderAtEnd(g->builder, body_bb);
        emit_stmt(g, stmt->while_stmt.body, locals);
        if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder))) {
            emit_release_owned_locals_from(g, locals, body_start);
            /* 内部辅助逻辑 */
            if (!emit_async_preempt_site(g, cond_bb))
                LLVMBuildBr(g->builder, cond_bb);
        } else {
            emit_release_owned_locals_from(g, locals, body_start);
        }

        LLVMPositionBuilderAtEnd(g->builder, cond_bb);
        LLVMValueRef cond = NULL;
        if (stmt->while_stmt.cond && stmt->while_stmt.cond->kind == AST_BLOCK) {
            int block_start = locals->count;
            zan_ast_list_t *stmts = &stmt->while_stmt.cond->block.stmts;
            for (int i = 0; i < stmts->count; i++) {
                if (i == stmts->count - 1 && stmts->items[i]->kind == AST_EXPR_STMT) {
                    cond = emit_expr(g, stmts->items[i]->expr_stmt.expr, locals);
                } else {
                    emit_stmt(g, stmts->items[i], locals);
                    if (LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder)))
                        break;
                }
            }
            if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder)))
                emit_release_owned_locals_from(g, locals, block_start);
            locals->count = block_start;
        } else {
            cond = emit_expr(g, stmt->while_stmt.cond, locals);
        }
        cond = zan_tobool(g->builder, cond, "dcond");
        LLVMBuildCondBr(g->builder, cond, body_bb, end_bb);

        g->pending.break_scope = saved_pending.break_scope;
        g->pending.continue_scope = saved_pending.continue_scope;
        g->break_target = saved_break;
        g->continue_target = saved_cont;
        g->loop_locals_base = saved_loop_base;
        g->loop_catch_base = saved_loop_cbase;
        g->finally_loop_base = saved_loop_fbase;
        g->eh_armed_loop_base = saved_loop_ehbase;

        LLVMPositionBuilderAtEnd(g->builder, end_bb);
        break;
    }

    case AST_FOREACH_STMT: {
        LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
        LLVMValueRef fn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder));

        LLVMValueRef collection = emit_expr(g, stmt->foreach_stmt.collection, locals);

        /* 内部辅助实现 */
        zan_type_t *col_type0 = infer_expr_type(g, stmt->foreach_stmt.collection,
                                                locals);
        zan_type_t *fe_enum_ty = foreach_proto_enum_type(g, col_type0);
        zan_type_t *fe_cur_ty = foreach_proto_current_type(g, col_type0);
        if (!fe_enum_ty && col_type0 && col_type0->kind == TYPE_CLASS &&
            col_type0->sym) {
            /* 内部辅助实现 */
            zan_istr_t gi = { (char *)"GetEnumerator", 13 };
            zan_symbol_t *gm = resolve_overload(col_type0->sym, gi, 0, 0);
            if (gm && gm->decl && gm->decl->kind == AST_METHOD_DECL &&
                gm->decl->method_decl.return_type) {
                zan_type_t *rt = resolve_type_ctx(g,
                    gm->decl->method_decl.return_type);
                if (rt && rt->kind == TYPE_INTERFACE)
                    zan_diag_emit(g->diag, DIAG_ERROR, stmt->loc,
                        "foreach: GetEnumerator() returning interface '%.*s' "
                        "is not supported yet; return the concrete enumerator "
                        "type so the loop can call MoveNext/Current directly",
                        (int)rt->name.len, rt->name.str);
            }
        }
        zan_symbol_t *fe_get_m = NULL;
        zan_symbol_t *fe_next_m = NULL, *fe_cur_m = NULL, *fe_cur_getter = NULL;
        if (fe_enum_ty) {
            zan_istr_t gi = { (char *)"GetEnumerator", 13 };
            zan_istr_t ni = { (char *)"MoveNext", 8 };
            zan_istr_t ci = { (char *)"Current", 7 };
            fe_get_m = resolve_overload(col_type0->sym, gi, 0, 0);
            fe_next_m = resolve_overload(fe_enum_ty->sym, ni, 0, 0);
            fe_cur_m = resolve_overload(fe_enum_ty->sym, ci, 0, 0);
            if (!fe_cur_m) {
                for (int i = 0; i < fe_enum_ty->sym->member_count &&
                                 !fe_cur_getter; i++) {
                    zan_symbol_t *m = fe_enum_ty->sym->members[i];
                    if (m && m->kind == SYM_PROPERTY && member_name_is(m, ci))
                        fe_cur_getter = property_getter_sym(g, m);
                }
            }
        }

        /* element type: declared loop-var type, else inferred from collection */
        zan_type_t *elem_type = NULL;
        if (stmt->foreach_stmt.var_type)
            elem_type = resolve_type_ctx(g, stmt->foreach_stmt.var_type);
        if ((!elem_type || elem_type->kind == TYPE_ERROR) && fe_cur_ty)
            elem_type = fe_cur_ty;
        if (!elem_type || elem_type->kind == TYPE_ERROR)
            elem_type = container_elem_type(col_type0);
        if (!elem_type) elem_type = g->binder->type_int;
        LLVMTypeRef elem_llvm = map_type(g, elem_type);

        /* 内部辅助逻辑 */
        zan_type_t *col_type = col_type0;
        bool fe_array = col_type && col_type->kind == TYPE_ARRAY;
        bool fe_string = col_type && col_type->kind == TYPE_STRING;
        LLVMValueRef fe_len = NULL;
        if (fe_array) {
            fe_len = zan_array_len(g, collection);
        } else if (fe_string) {
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMValueRef sp = LLVMBuildBitCast(g->builder, collection, i8ptr, "fe.sp");
            fe_len = emit_string_length(g, sp, stmt->loc);
        }

        /* 内部辅助实现 */
        LLVMValueRef col_slot = NULL, idx_alloc = NULL, iter_alloc = NULL;
        LLVMValueRef enum_slot = NULL, enum_alloc = NULL;
        if (g->current_async_frame) {
            /* Scan and emission can visit a finally body different numbers of times */
            if (fe_enum_ty) {
                local_var_t *ev2 = local_find_async_role(locals, stmt,
                    ASYNC_FOREACH_ENUMERATOR);
                if (ev2) enum_slot = ev2->alloca;
                if (!enum_slot)
                    zan_diag_emit(g->diag, DIAG_ERROR, stmt->loc,
                                  "foreach protocol enumerator missing an "
                                  "async frame slot");
            }
            local_var_t *cv = local_find_async_role(locals, stmt,
                ASYNC_FOREACH_COLLECTION);
            if (cv) col_slot = cv->alloca;
            local_var_t *iv = local_find_async_role(locals, stmt,
                ASYNC_FOREACH_INDEX);
            if (iv) idx_alloc = iv->alloca;
            /* 内部辅助实现 */
            local_var_t *ev = local_find_async_decl(locals, stmt);
            if (ev && !ev->type && LLVMGetTypeKind(elem_llvm) ==
                    LLVMGetTypeKind(local_slot_type(g, ev)))
                iter_alloc = ev->alloca;
        }
        if (col_slot)
            zan_store_fit(g,
                LLVMBuildBitCast(g->builder, collection,
                    LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0), "fe.colp"),
                col_slot);

        /* protocol: materialize the enumerator once, before the loop */
        LLVMTypeRef fe_enum_ll = fe_enum_ty ? map_type(g, fe_enum_ty) : NULL;
        if (fe_enum_ty) {
            if (!enum_slot) enum_alloc = emit_entry_alloca(g, fe_enum_ll, "fe.enum");
            LLVMValueRef ev0 = emit_foreach_call0(g, col_type0, fe_get_m,
                                                  collection);
            zan_store_fit(g, ev0, enum_slot ? enum_slot : enum_alloc);
        }

        /* 内部辅助逻辑 */
        bool fe_coll_registered = false;
        {
            zan_type_t *coll_t = infer_expr_type(g,
                stmt->foreach_stmt.collection, locals);
            if (collection && coll_t && is_rc_managed_type(coll_t) &&
                LLVMGetTypeKind(LLVMTypeOf(collection)) ==
                    LLVMPointerTypeKind &&
                !expr_is_local_ident(stmt->foreach_stmt.collection, locals) &&
                expr_yields_owned_rc_value(g, stmt->foreach_stmt.collection,
                                           locals)) {
                LLVMValueRef cslot = col_slot;
                if (!cslot) {
                    cslot = emit_entry_alloca(g, LLVMTypeOf(collection),
                                              "fe.coll");
                    zan_store_fit(g, collection, cslot);
                }
                char cn2[32];
                snprintf(cn2, sizeof(cn2), "$fe.coll");
                zan_istr_t cni = { cn2, (uint32_t)strlen(cn2) };
                local_add(locals, cni, cslot, coll_t);
                arc_own_local(g, locals);
                fe_coll_registered = true;
            }
            if (fe_enum_ty) {
                char enm2[32];
                snprintf(enm2, sizeof(enm2), "$fe.enum");
                zan_istr_t eni = { enm2, (uint32_t)strlen(enm2) };
                local_add(locals, eni, enum_slot ? enum_slot : enum_alloc,
                          fe_enum_ty);
                arc_own_local(g, locals);
            }
        }

        int fe_start = locals->count;
        if (!idx_alloc) idx_alloc = emit_entry_alloca(g, i64, "fi");
        zan_store_fit(g, LLVMConstInt(i64, 0, 0), idx_alloc);

        if (!iter_alloc) iter_alloc = emit_entry_alloca(g, elem_llvm, "fv");
        local_add(locals, stmt->foreach_stmt.var_name, iter_alloc, elem_type);

        LLVMTypeRef list_ptr_ty = LLVMTypeOf(collection);
        LLVMBasicBlockRef cond_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "fe.cond");
        LLVMBasicBlockRef body_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "fe.body");
        /* 内部辅助逻辑 */
        LLVMBasicBlockRef step_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "fe.step");
        LLVMBasicBlockRef end_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "fe.end");

        zan_irgen_pending_context_t fe_saved_pending = g->pending;
        LLVMBasicBlockRef fe_saved_break = g->break_target;
        LLVMBasicBlockRef fe_saved_cont = g->continue_target;
        int fe_saved_loop_base = g->loop_locals_base;
        int fe_saved_loop_cbase = g->loop_catch_base;
        int fe_saved_loop_fbase = g->finally_loop_base;
        int fe_saved_loop_ehbase = g->eh_armed_loop_base;
        g->pending.break_scope = g->pending.scope;
        g->pending.continue_scope = g->pending.scope;
        g->break_target = end_bb;
        g->continue_target = step_bb;
        g->loop_locals_base = fe_start;
        g->loop_catch_base = g->catch_cleanup_count;
        g->finally_loop_base = g->finally_count;
        g->eh_armed_loop_base = g->eh_armed_count;

        LLVMBuildBr(g->builder, cond_bb);
        LLVMPositionBuilderAtEnd(g->builder, cond_bb);

        /* protocol path: the condition is enumerator */
        if (fe_enum_ty) {
            LLVMValueRef fev = enum_slot
                ? LLVMBuildBitCast(g->builder,
                      LLVMBuildLoad2(g->builder,
                          LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0),
                          enum_slot, "fe.ens"),
                      fe_enum_ll, "fe.env")
                : LLVMBuildLoad2(g->builder, fe_enum_ll, enum_alloc, "fe.env");
            LLVMValueRef mr = emit_foreach_call0(g, fe_enum_ty, fe_next_m, fev);
            LLVMTypeRef mrt = LLVMTypeOf(mr);
            LLVMValueRef more =
                (LLVMGetTypeKind(mrt) == LLVMIntegerTypeKind &&
                 LLVMGetIntTypeWidth(mrt) == 1)
                    ? mr
                    : zan_icmp(g->builder, LLVMIntNE, mr,
                               LLVMConstNull(mrt), "fe.more");
            LLVMBuildCondBr(g->builder, more, body_bb, end_bb);

            LLVMPositionBuilderAtEnd(g->builder, body_bb);
            fev = enum_slot
                ? LLVMBuildBitCast(g->builder,
                      LLVMBuildLoad2(g->builder,
                          LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0),
                          enum_slot, "fe.bes"),
                      fe_enum_ll, "fe.bev")
                : LLVMBuildLoad2(g->builder, fe_enum_ll, enum_alloc, "fe.bev");
            LLVMValueRef cur;
            if (fe_cur_m)
                cur = emit_foreach_call0(g, fe_enum_ty, fe_cur_m, fev);
            else
                cur = emit_property_getter_call(g, fe_cur_getter, fe_enum_ty,
                                                fev, NULL, locals);
            /* 内部辅助实现 */
            if (cur && elem_type &&
                LLVMGetTypeKind(LLVMTypeOf(cur)) == LLVMIntegerTypeKind &&
                LLVMGetIntTypeWidth(LLVMTypeOf(cur)) == 64 &&
                LLVMGetTypeKind(elem_llvm) != LLVMIntegerTypeKind) {
                LLVMValueRef cslot = emit_entry_alloca(g,
                    LLVMTypeOf(cur), "fe.curs");
                zan_store_fit(g, cur, cslot);
                cur = load_collection_slot_value(g, elem_type, cslot);
            }
            zan_store_fit(g, cur, iter_alloc);

            emit_stmt(g, stmt->foreach_stmt.body, locals);
            emit_release_owned_locals_from(g, locals, fe_start);
            if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder)))
                LLVMBuildBr(g->builder, step_bb);

            LLVMPositionBuilderAtEnd(g->builder, step_bb);
            LLVMBuildBr(g->builder, cond_bb);

            g->pending.break_scope = fe_saved_pending.break_scope;
            g->pending.continue_scope = fe_saved_pending.continue_scope;
            g->break_target = fe_saved_break;
            g->continue_target = fe_saved_cont;
            g->loop_locals_base = fe_saved_loop_base;
            g->loop_catch_base = fe_saved_loop_cbase;
            g->finally_loop_base = fe_saved_loop_fbase;
            g->eh_armed_loop_base = fe_saved_loop_ehbase;
            LLVMPositionBuilderAtEnd(g->builder, end_bb);
            /* 内部辅助实现 */
            emit_release_owned_locals_from(g, locals, fe_start);
            if (!fe_coll_registered)
                emit_release_owned_call_temp(g, stmt->foreach_stmt.collection,
                                             collection, locals);
            break;
        }

        /* count: field 0 of the List struct, re-read each iteration */
        LLVMValueRef col_cond = col_slot
            ? LLVMBuildBitCast(g->builder,
                  LLVMBuildLoad2(g->builder,
                      LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0),
                      col_slot, "fe.col"),
                  list_ptr_ty, "fe.colc")
            : collection;
        LLVMValueRef count;
        if (fe_array || fe_string) {
            /* 内部辅助实现 */
            if (col_slot && fe_array) {
                count = zan_array_len(g, col_cond);
            } else if (col_slot) {
                LLVMTypeRef i8p = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                LLVMValueRef sp2 = LLVMBuildBitCast(g->builder, col_cond, i8p, "fe.sp2");
                count = emit_string_length(g, sp2, stmt->loc);
            } else {
                count = fe_len;
            }
        } else {
            LLVMValueRef cnt_ptr = LLVMBuildStructGEP2(g->builder, g->list_struct_type,
                col_cond, 0, "cnt_ptr");
            count = LLVMBuildLoad2(g->builder, i64, cnt_ptr, "cnt");
        }
        LLVMValueRef idx_val = LLVMBuildLoad2(g->builder, i64, idx_alloc, "i");
        LLVMValueRef cmp = zan_icmp(g->builder, LLVMIntSLT, idx_val, count, "fcmp");
        LLVMBuildCondBr(g->builder, cmp, body_bb, end_bb);

        LLVMPositionBuilderAtEnd(g->builder, body_bb);
        /* data pointer: field 2 of the List struct, likewise re-read here */
        LLVMValueRef col_body = col_slot
            ? LLVMBuildBitCast(g->builder,
                  LLVMBuildLoad2(g->builder,
                      LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0),
                      col_slot, "fe.col"),
                  list_ptr_ty, "fe.colb")
            : collection;
        if (fe_array || fe_string) {
            LLVMValueRef ai = LLVMBuildLoad2(g->builder, i64, idx_alloc, "ib");
            LLVMTypeRef slot_ty = fe_string
                ? LLVMInt8TypeInContext(g->ctx) : elem_llvm;
            LLVMValueRef ep = LLVMBuildGEP2(g->builder, slot_ty, col_body, &ai, 1, "aep");
            LLVMValueRef av = LLVMBuildLoad2(g->builder, slot_ty, ep, "aelem");
            if (fe_string && slot_ty != elem_llvm)
                av = zan_iwiden(g->builder, av, elem_llvm);
            zan_store_fit(g, av, iter_alloc);

            emit_stmt(g, stmt->foreach_stmt.body, locals);
            emit_release_owned_locals_from(g, locals, fe_start);
            if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder)))
                LLVMBuildBr(g->builder, step_bb);

            LLVMPositionBuilderAtEnd(g->builder, step_bb);
            zan_store_fit(g, zan_add(g->builder,
                LLVMBuildLoad2(g->builder, i64, idx_alloc, "i2"),
                LLVMConstInt(i64, 1, 0), "next"), idx_alloc);
            LLVMBuildBr(g->builder, cond_bb);

            g->pending.break_scope = fe_saved_pending.break_scope;
            g->pending.continue_scope = fe_saved_pending.continue_scope;
            g->break_target = fe_saved_break;
            g->continue_target = fe_saved_cont;
            g->loop_locals_base = fe_saved_loop_base;
            g->loop_catch_base = fe_saved_loop_cbase;
            g->finally_loop_base = fe_saved_loop_fbase;
            g->eh_armed_loop_base = fe_saved_loop_ehbase;
            LLVMPositionBuilderAtEnd(g->builder, end_bb);
            emit_release_owned_locals_from(g, locals, fe_start);
            if (!fe_coll_registered) {
                LLVMValueRef col_end = col_slot
                    ? LLVMBuildBitCast(g->builder,
                          LLVMBuildLoad2(g->builder,
                              LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0),
                              col_slot, "fe.col"),
                          list_ptr_ty, "fe.cole")
                    : collection;
                emit_release_owned_call_temp(g, stmt->foreach_stmt.collection,
                                             col_end, locals);
            }
            break;
        }
        LLVMValueRef data_ptr = LLVMBuildStructGEP2(g->builder, g->list_struct_type,
            col_body, 2, "data_ptr");
        LLVMValueRef data = LLVMBuildLoad2(g->builder, LLVMPointerType(i64, 0),
            data_ptr, "data");
        LLVMValueRef idx_body = LLVMBuildLoad2(g->builder, i64, idx_alloc, "ib");
        /* 内部辅助逻辑 */
        LLVMValueRef fe_widx = slot_word_index(g, idx_body,
            elem_slot_words(g, elem_type));
        LLVMValueRef elem_ptr = LLVMBuildGEP2(g->builder, i64, data, &fe_widx, 1, "ep");
        LLVMTypeKind ek = LLVMGetTypeKind(elem_llvm);
        LLVMValueRef elem = (ek == LLVMStructTypeKind)
            ? load_struct_from_slot(g, elem_ptr, elem_llvm)
            : LLVMBuildLoad2(g->builder, i64, elem_ptr, "elem");
        if (ek == LLVMPointerTypeKind)
            elem = LLVMBuildIntToPtr(g->builder, elem, elem_llvm, "elp");
        else if (ek == LLVMDoubleTypeKind)
            elem = LLVMBuildBitCast(g->builder, elem, elem_llvm, "elf");
        else if (ek == LLVMFloatTypeKind) {
            /* 内部辅助逻辑 */
            LLVMValueRef nb = LLVMBuildTrunc(g->builder, elem,
                LLVMInt32TypeInContext(g->ctx), "elf32");
            elem = LLVMBuildBitCast(g->builder, nb, elem_llvm, "elf32b");
        }
        else if (ek == LLVMIntegerTypeKind && LLVMGetIntTypeWidth(elem_llvm) < 64)
            elem = LLVMBuildTrunc(g->builder, elem, elem_llvm, "elt");
        zan_store_fit(g, elem, iter_alloc);

        emit_stmt(g, stmt->foreach_stmt.body, locals);

        if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder))) {
            emit_release_owned_locals_from(g, locals, fe_start);
            LLVMBuildBr(g->builder, step_bb);
        } else {
            emit_release_owned_locals_from(g, locals, fe_start);
        }

        LLVMPositionBuilderAtEnd(g->builder, step_bb);
        LLVMValueRef next = zan_add(g->builder,
            LLVMBuildLoad2(g->builder, i64, idx_alloc, "i2"),
            LLVMConstInt(i64, 1, 0), "next");
        zan_store_fit(g, next, idx_alloc);
        LLVMBuildBr(g->builder, cond_bb);

        g->pending.break_scope = fe_saved_pending.break_scope;
        g->pending.continue_scope = fe_saved_pending.continue_scope;
        g->break_target = fe_saved_break;
        g->continue_target = fe_saved_cont;
        g->loop_locals_base = fe_saved_loop_base;
        g->loop_catch_base = fe_saved_loop_cbase;
        g->finally_loop_base = fe_saved_loop_fbase;
        g->eh_armed_loop_base = fe_saved_loop_ehbase;

        LLVMPositionBuilderAtEnd(g->builder, end_bb);
        /* an owned temporary collection (e */
        emit_release_owned_locals_from(g, locals, fe_start);
        if (!fe_coll_registered) {
            LLVMValueRef col_end = col_slot
                ? LLVMBuildBitCast(g->builder,
                      LLVMBuildLoad2(g->builder,
                          LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0),
                          col_slot, "fe.col"),
                      list_ptr_ty, "fe.cole")
                : collection;
            emit_release_owned_call_temp(g, stmt->foreach_stmt.collection,
                                         col_end, locals);
        }
        break;
    }

    case AST_CHECKED_STMT: {
        /* 内部辅助实现 */
        int saved = g->irgen_checked_depth;
        g->irgen_checked_depth = stmt->checked_stmt.checked ? saved + 1
                                                            : saved - 1;
        emit_stmt(g, stmt->checked_stmt.body, locals);
        g->irgen_checked_depth = saved;
        break;
    }

    case AST_LOCK_STMT: {
        /* lock (expr) body — enter/exit the runtime monitor around the body */
        g->uses_sync_runtime = true;
        LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
        LLVMTypeRef mon_ty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx),
                                              &i8ptr, 1, 0);
        LLVMValueRef enter_fn = LLVMGetNamedFunction(g->mod, "zan_monitor_enter");
        if (!enter_fn)
            enter_fn = LLVMAddFunction(g->mod, "zan_monitor_enter", mon_ty);
        LLVMValueRef exit_fn = LLVMGetNamedFunction(g->mod, "zan_monitor_exit");
        if (!exit_fn)
            exit_fn = LLVMAddFunction(g->mod, "zan_monitor_exit", mon_ty);

        LLVMValueRef obj = emit_expr(g, stmt->lock_stmt.expr, locals);
        LLVMTypeRef ot = LLVMTypeOf(obj);
        if (LLVMGetTypeKind(ot) == LLVMIntegerTypeKind)
            obj = LLVMBuildIntToPtr(g->builder, obj, i8ptr, "lockp");
        else if (LLVMGetTypeKind(ot) == LLVMPointerTypeKind && ot != i8ptr)
            obj = LLVMBuildBitCast(g->builder, obj, i8ptr, "lockp");
        /* 内部辅助逻辑 */
        LLVMValueRef obj_slot = emit_entry_alloca(g, i8ptr, "lock.slot");
        zan_store_fit(g, obj, obj_slot);
        zan_call2(g->builder, mon_ty, enter_fn, &obj, 1, "");
        int lock_fin = -1;
        if (g->finally_count < ZAN_MAX_FINALLY_DEPTH) {
            lock_fin = g->finally_count++;
            g->finallys[lock_fin].body = NULL;
            g->finallys[lock_fin].monitor_obj = obj_slot;
            g->finallys[lock_fin].pending_parent = g->pending.scope;
            g->finallys[lock_fin].pending_scope = NULL;
            g->finallys[lock_fin].continuation_slot = NULL;
            g->finallys[lock_fin].shared = NULL;
            g->finallys[lock_fin].outer_armed_depth = g->eh_armed_count;
            g->finallys[lock_fin].outer_throw_locals_base = g->throw_locals_base;
            g->finallys[lock_fin].outer_throw_catch_base = g->throw_catch_base;
            /* 内部辅助逻辑 */
            g->finallys[lock_fin].in_try_body = false;
        } else {
            zan_diag_emit(g->diag, DIAG_ERROR, stmt->loc,
                "too many nested lock/try-finally blocks in one function body");
        }
        emit_stmt(g, stmt->lock_stmt.body, locals);
        if (lock_fin >= 0) g->finally_count = lock_fin;
        if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder))) {
            (void)exit_fn;
            emit_monitor_exit(g, obj_slot);
        }
        break;
    }

    case AST_LABEL_STMT: {
        int li = irgen_goto_label_idx(g, stmt->ident.name);
        if (li < 0) break;
        if (g->goto_labels[li].defined) {
            zan_diag_emit(g->diag, DIAG_ERROR, stmt->loc,
                "duplicate label '%.*s' in this function",
                (int)stmt->ident.name.len, stmt->ident.name.str);
        } else {
            /* 内部辅助实现 */
            g->goto_labels[li].defined = 1;
            g->goto_labels[li].pending_scope = g->pending.scope;
            g->goto_labels[li].fin_depth = g->finally_count;
            g->goto_labels[li].eh_armed_base = g->eh_armed_count;
            g->goto_labels[li].catch_base = g->catch_cleanup_count;
            g->goto_labels[li].locals_base = locals->count;
            g->goto_labels[li].locals_owned =
                irgen_goto_count_owned_locals(locals);
            LLVMValueRef lfn = g->goto_labels[li].fn;
            int lab_fin = g->goto_labels[li].fin_depth;
            int lab_eh = g->goto_labels[li].eh_armed_base;
            int lab_catch = g->goto_labels[li].catch_base;
            LLVMBasicBlockRef resume_bb = LLVMGetInsertBlock(g->builder);
            for (int fi = 0; fi < g->goto_fixup_count; fi++) {
                struct zan_goto_fixup *f = &g->goto_fixups[fi];
                if (f->resolved || f->fn != lfn ||
                    f->label_owner != g->goto_labels[li].label_owner ||
                    f->name.len != stmt->ident.name.len ||
                    memcmp(f->name.str, stmt->ident.name.str,
                           (size_t)stmt->ident.name.len) != 0)
                    continue;
                f->resolved = 1;
                /* Nested cleanup emission can grow and relocate goto_fixups. */
                struct zan_goto_fixup source_fixup = *f;
                f = &source_fixup;
                if (pending_scope_common(f->pending.scope, g->pending.scope) !=
                        g->pending.scope ||
                    f->fin_depth < lab_fin || f->eh_armed_base < lab_eh ||
                    f->catch_base < lab_catch) {
                    zan_diag_emit(g->diag, DIAG_ERROR, f->loc,
                        "goto '%.*s' jumps into a try/catch/finally or lock "
                        "block; C# forbids jumping into one -- restructure "
                        "with break/return",
                        (int)stmt->ident.name.len, stmt->ident.name.str);
                } else if (f->locals_base < locals->count ||
                           f->locals_owned !=
                               g->goto_labels[li].locals_owned) {
                    zan_diag_emit(g->diag, DIAG_ERROR, f->loc,
                        "goto '%.*s' leaves (or enters) a scope with owning "
                        "locals; the abandoned references would leak -- "
                        "restructure with break/return",
                        (int)stmt->ident.name.len, stmt->ident.name.str);
                } else if (f->fin_depth > lab_fin || f->catch_base > lab_catch ||
                           f->eh_armed_base > lab_eh ||
                           f->pending.scope != g->pending.scope) {
                    /* 内部辅助实现 */
                    zan_irgen_pending_context_t saved_pending = g->pending;
                    LLVMBasicBlockRef saved_break = g->break_target;
                    LLVMBasicBlockRef saved_continue = g->continue_target;
                    int saved_throw_base = g->throw_locals_base;
                    int saved_throw_cbase = g->throw_catch_base;
                    int saved_loop_base = g->loop_locals_base;
                    int saved_loop_cbase = g->loop_catch_base;
                    int saved_fin_lb = g->finally_loop_base;
                    int saved_eh_lb = g->eh_armed_loop_base;
                    int saved_checked = g->irgen_checked_depth;
                    void *saved_armed = finally_shared_copy(g, g->eh_armed,
                        sizeof(g->eh_armed[0]) * (size_t)lab_eh);
                    g->pending = f->pending;
                    g->break_target = f->break_target;
                    g->continue_target = f->continue_target;
                    g->throw_locals_base = f->throw_locals_base;
                    g->throw_catch_base = f->throw_catch_base;
                    g->loop_locals_base = f->loop_locals_base;
                    g->loop_catch_base = f->loop_catch_base;
                    g->finally_loop_base = f->finally_loop_base;
                    g->eh_armed_loop_base = f->eh_armed_loop_base;
                    g->irgen_checked_depth = f->checked_depth;
                    void *saved_catches = finally_shared_copy(g, g->catch_cleanups,
                        sizeof(g->catch_cleanups[0]) * (size_t)lab_catch);
                    if (f->catch_base) memcpy(g->catch_cleanups, f->catch_snap,
                        sizeof(g->catch_cleanups[0]) * (size_t)f->catch_base);
                    g->catch_cleanup_count = f->catch_base;
                    g->eh_armed_count = f->eh_armed_base;
                    for (int ai = 0; ai < f->eh_armed_base; ai++)
                        g->eh_armed[ai].old_top_slot = f->armed_snap[ai];
                    LLVMPositionBuilderAtEnd(g->builder, f->from_bb);
                    if (LLVMGetBasicBlockTerminator(f->from_bb))
                        LLVMInstructionEraseFromParent(
                            LLVMGetBasicBlockTerminator(f->from_bb));
                    if (f->fin_depth > lab_fin && f->finally_snap &&
                        f->finally_snap_n >= f->fin_depth) {
                        memcpy(g->finallys + lab_fin,
                               f->finally_snap + lab_fin,
                               sizeof(g->finallys[0]) *
                                   (size_t)(f->fin_depth - lab_fin));
                        g->finally_count = f->fin_depth;
                        emit_pending_finallys(g, locals, lab_fin, false);
                        g->finally_count = lab_fin;
                    } else if (f->fin_depth > lab_fin) {
                        zan_diag_emit(g->diag, DIAG_ERROR, f->loc,
                            "goto '%.*s' leaves %d finally/lock region(s) "
                            "whose exits cannot be run -- restructure with "
                            "break/return",
                            (int)stmt->ident.name.len, stmt->ident.name.str,
                            f->fin_depth - lab_fin);
                    }
                    if (!LLVMGetBasicBlockTerminator(
                            LLVMGetInsertBlock(g->builder)) &&
                        f->catch_base > lab_catch) {
                        if (f->catch_snap && f->catch_snap_n >= f->catch_base) {
                            memcpy(g->catch_cleanups + lab_catch,
                                   f->catch_snap + lab_catch,
                                   sizeof(g->catch_cleanups[0]) *
                                       (size_t)(f->catch_base - lab_catch));
                            g->catch_cleanup_count = f->catch_base;
                            emit_release_active_catch_excs(g, lab_catch);
                            g->catch_cleanup_count = lab_catch;
                        }
                    }
                    if (!LLVMGetBasicBlockTerminator(
                            LLVMGetInsertBlock(g->builder)) &&
                        f->eh_armed_base > lab_eh) {
                        g->eh_armed_count = f->eh_armed_base;
                        emit_eh_disarm_from(g, lab_eh);
                        g->eh_armed_count = lab_eh;
                    }
                    if (!LLVMGetBasicBlockTerminator(
                            LLVMGetInsertBlock(g->builder))) {
                        emit_pending_scope_exit(g, saved_pending.scope, false);
                        LLVMBuildBr(g->builder, g->goto_labels[li].bb);
                    }
                    if (lab_eh) memcpy(g->eh_armed, saved_armed,
                        sizeof(g->eh_armed[0]) * (size_t)lab_eh);
                    g->eh_armed_count = lab_eh;
                    if (lab_catch) memcpy(g->catch_cleanups, saved_catches,
                        sizeof(g->catch_cleanups[0]) * (size_t)lab_catch);
                    g->catch_cleanup_count = lab_catch;
                    g->pending = saved_pending;
                    g->break_target = saved_break;
                    g->continue_target = saved_continue;
                    g->throw_locals_base = saved_throw_base;
                    g->throw_catch_base = saved_throw_cbase;
                    g->loop_locals_base = saved_loop_base;
                    g->loop_catch_base = saved_loop_cbase;
                    g->finally_loop_base = saved_fin_lb;
                    g->eh_armed_loop_base = saved_eh_lb;
                    g->irgen_checked_depth = saved_checked;
                }
            }
            LLVMPositionBuilderAtEnd(g->builder, resume_bb);
        }
        LLVMBasicBlockRef bb = g->goto_labels[li].bb;
        if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder)))
            LLVMBuildBr(g->builder, bb);
        LLVMPositionBuilderAtEnd(g->builder, bb);
        break;
    }

    case AST_GOTO_STMT: {
        LLVMValueRef gfn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder));
        int li = irgen_goto_label_idx(g, stmt->ident.name);
        if (li < 0) break;
        LLVMBasicBlockRef bb = g->goto_labels[li].bb;
        if (g->goto_labels[li].defined) {
            if (pending_scope_common(g->pending.scope,
                    g->goto_labels[li].pending_scope) != g->goto_labels[li].pending_scope) {
                zan_diag_emit(g->diag, DIAG_ERROR, stmt->loc,
                    "goto '%.*s' jumps into a finally body",
                    (int)stmt->ident.name.len, stmt->ident.name.str);
                break;
            }
            /* 内部辅助逻辑 */
            emit_pending_finallys(g, locals, g->goto_labels[li].fin_depth, false);
            if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder))) {
                emit_release_owned_locals_range(
                    g, locals, g->goto_labels[li].locals_base);
                emit_release_active_catch_excs(
                    g, g->goto_labels[li].catch_base);
                emit_eh_disarm_from(g, g->goto_labels[li].eh_armed_base);
                emit_pending_scope_exit(g, g->goto_labels[li].pending_scope, false);
                LLVMBuildBr(g->builder, bb);
            }
        } else {
            /* 内部辅助逻辑 */
            if (ZAN_TAB_ENSURE(g->goto_fixups, g->goto_fixup_count,
                               g->goto_fixup_cap, 8)) {
                struct zan_goto_fixup *f =
                    &g->goto_fixups[g->goto_fixup_count++];
                memset(f, 0, sizeof(*f));
                f->name = stmt->ident.name;
                f->fn = gfn;
                f->loc = stmt->loc;
                f->label_owner = g->goto_labels[li].label_owner;
                f->pending = g->pending;
                f->break_target = g->break_target;
                f->continue_target = g->continue_target;
                f->throw_locals_base = g->throw_locals_base;
                f->throw_catch_base = g->throw_catch_base;
                f->loop_locals_base = g->loop_locals_base;
                f->loop_catch_base = g->loop_catch_base;
                f->finally_loop_base = g->finally_loop_base;
                f->eh_armed_loop_base = g->eh_armed_loop_base;
                f->checked_depth = g->irgen_checked_depth;
                f->armed_snap = zan_arena_alloc(g->arena,
                    sizeof(*f->armed_snap) * (size_t)g->eh_armed_count);
                for (int ai = 0; ai < g->eh_armed_count; ai++)
                    f->armed_snap[ai] = g->eh_armed[ai].old_top_slot;
                f->fin_depth = g->finally_count;
                f->eh_armed_base = g->eh_armed_count;
                f->catch_base = g->catch_cleanup_count;
                f->locals_base = locals->count;
                f->locals_owned = irgen_goto_count_owned_locals(locals);
                f->from_bb = LLVMGetInsertBlock(g->builder);
                f->finally_snap_n = g->finally_count;
                if (f->finally_snap_n > 0) {
                    f->finally_snap = zan_arena_alloc(g->arena,
                        sizeof(*f->finally_snap) * (size_t)f->finally_snap_n);
                    if (f->finally_snap)
                        memcpy(f->finally_snap, g->finallys,
                               sizeof(*f->finally_snap) *
                                   (size_t)f->finally_snap_n);
                }
                f->catch_snap_n = g->catch_cleanup_count;
                if (f->catch_snap_n > 0) {
                    f->catch_snap = zan_arena_alloc(g->arena,
                        sizeof(*f->catch_snap) * (size_t)f->catch_snap_n);
                    if (f->catch_snap)
                        memcpy(f->catch_snap, g->catch_cleanups,
                               sizeof(*f->catch_snap) * (size_t)f->catch_snap_n);
                }
            }
            LLVMBuildBr(g->builder, bb);
        }
        /* 内部辅助逻辑 */
        LLVMBasicBlockRef cont =
            LLVMAppendBasicBlockInContext(g->ctx, gfn, "goto.cont");
        LLVMPositionBuilderAtEnd(g->builder, cont);
        break;
    }

    case AST_THROW_STMT: {
        /* 内部辅助逻辑 */
        bool rethrow = stmt->throw_stmt.value == NULL;
        if (rethrow) {
            /* 内部辅助逻辑 */
            LLVMValueRef hslot = g->catch_cleanup_count > 0
                ? g->catch_cleanups[g->catch_cleanup_count - 1].exc_slot : NULL;
            LLVMBasicBlockRef hbb = hslot ? LLVMGetInstructionParent(hslot) : NULL;
            if (!hbb || LLVMGetBasicBlockParent(hbb) !=
                    LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder))) {
                zan_diag_emit(g->diag, DIAG_ERROR, stmt->loc,
                    "a rethrow (`throw;`) is only valid inside a catch block");
                break;
            }
        }
        emit_eh_hook_call(g, "__zan_eh_throw");
        LLVMValueRef val = rethrow
            ? NULL : emit_expr(g, stmt->throw_stmt.value, locals);
        LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
        {
            LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
            LLVMValueRef top_g, bufs_g, exc_g;
            get_eh_globals(g, &top_g, &bufs_g, &exc_g);
            if (rethrow) {
                /* 内部辅助实现 */
                int ci = g->catch_cleanup_count - 1;
                LLVMValueRef exc_slot = g->catch_cleanups[ci].exc_slot;
                LLVMValueRef own_slot = g->catch_cleanups[ci].owned_slot;
                LLVMValueRef tid_slot = g->catch_cleanups[ci].tid_slot;
                LLVMValueRef ev = LLVMBuildLoad2(g->builder, i8ptr, exc_slot, "reth.exc");
                LLVMValueRef ofl = LLVMBuildLoad2(g->builder, i32t, own_slot, "reth.own");
                zan_store_fit(g, ev, exc_g);
                zan_store_fit(g, ofl, get_eh_exc_owned_global(g));
                zan_store_fit(g,
                    LLVMBuildLoad2(g->builder, i8ptr, tid_slot, "reth.tid"),
                    get_eh_exc_tid_global(g));
                LLVMValueRef rfn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder));
                LLVMValueRef isown = zan_icmp(g->builder, LLVMIntNE, ofl,
                    LLVMConstInt(i32t, 0, 0), "reth.isown");
                LLVMBasicBlockRef drop_bb =
                    LLVMAppendBasicBlockInContext(g->ctx, rfn, "reth.drop");
                LLVMBasicBlockRef dcont_bb =
                    LLVMAppendBasicBlockInContext(g->ctx, rfn, "reth.dcont");
                LLVMBuildCondBr(g->builder, isown, drop_bb, dcont_bb);
                LLVMPositionBuilderAtEnd(g->builder, drop_bb);
                emit_eh_tmp_drop(g, ev);
                LLVMBuildBr(g->builder, dcont_bb);
                LLVMPositionBuilderAtEnd(g->builder, dcont_bb);
                zan_store_fit(g, LLVMConstInt(i32t, 0, 0), own_slot);
                goto throw_unwind;
            }
            LLVMValueRef vail = val;
            if (LLVMGetTypeKind(LLVMTypeOf(vail)) != LLVMPointerTypeKind)
                vail = LLVMConstNull(i8ptr);
            else if (LLVMTypeOf(vail) != i8ptr)
                vail = LLVMBuildBitCast(g->builder, vail, i8ptr, "exc.bc");
            zan_store_fit(g, vail, exc_g);
            /* 内部辅助逻辑 */
            {
                LLVMValueRef owned_g = get_eh_exc_owned_global(g);
                LLVMValueRef tid_g = get_eh_exc_tid_global(g);
                zan_type_t *tt = infer_expr_type(g, stmt->throw_stmt.value, locals);
                if (tt && is_rc_managed_type(tt) &&
                    LLVMGetTypeKind(LLVMTypeOf(val)) == LLVMPointerTypeKind) {
                    /* 内部辅助实现 */
                    if (!expr_yields_owned_rc_value(g, stmt->throw_stmt.value, locals))
                        /* 内部辅助实现 */
                        emit_rc_retain_for_type(g, tt, vail);
                    zan_store_fit(g, LLVMConstInt(i32t, 1, 0), owned_g);
                    /* 记录抛出异常类的类型描述符，供 catch 子句按动态类型分发 */
                    if (tt->kind == TYPE_CLASS && tt->sym) {
                        LLVMValueRef tid = get_class_tid_global(g, tt->sym);
                        zan_store_fit(g,
                            LLVMBuildBitCast(g->builder, tid, i8ptr, "tid.bc"),
                            tid_g);
                    } else {
                        zan_store_fit(g, LLVMConstNull(i8ptr), tid_g);
                    }
                } else {
                    zan_store_fit(g, LLVMConstInt(i32t, 0, 0), owned_g);
                    zan_store_fit(g, LLVMConstNull(i8ptr), tid_g);
                }
            }
throw_unwind:
            /* 内部辅助逻辑 */
            emit_finallys_left_by_throw(g, locals);
            if (LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder))) break;
            /* longjmp skips every scope-exit release between here and the handler */
            if (g->current_async_frame) {
                emit_release_owned_locals_range(g, locals, g->throw_locals_base);
                emit_clear_owned_locals_range(g, locals, g->throw_locals_base);
            }
            /* 内部辅助逻辑 */
            emit_release_active_catch_excs(g, g->throw_catch_base);
            LLVMValueRef top = LLVMBuildLoad2(g->builder, i32t, top_g, "eh.top");
            (void)top;
            LLVMValueRef fn2 = LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder));
            LLVMBasicBlockRef jmp_bb = LLVMAppendBasicBlockInContext(g->ctx, fn2, "throw.jmp");
            LLVMBasicBlockRef die_bb = LLVMAppendBasicBlockInContext(g->ctx, fn2, "throw.die");
            /* 内部辅助实现 */
            if (g->target_is_wasm) {
                LLVMBuildBr(g->builder, die_bb);
                LLVMPositionBuilderAtEnd(g->builder, jmp_bb);
                LLVMBuildUnreachable(g->builder);
                LLVMPositionBuilderAtEnd(g->builder, die_bb);
            } else {
                LLVMValueRef has = zan_icmp(g->builder, LLVMIntSGE, top,
                    LLVMConstInt(i32t, 0, 0), "eh.has");
                LLVMBuildCondBr(g->builder, has, jmp_bb, die_bb);
                LLVMPositionBuilderAtEnd(g->builder, jmp_bb);
                /* 内部辅助逻辑 */
                if (g->current_async_frame) {
                    emit_eh_longjmp(g, emit_eh_buf_ptr(g, top));
                    LLVMBuildUnreachable(g->builder);
                } else {
                    emit_eh_unwind_to_handler(g, top);
                    emit_eh_longjmp(g, emit_eh_buf_ptr(g, top));
                    LLVMBuildUnreachable(g->builder);
                }
                LLVMPositionBuilderAtEnd(g->builder, die_bb);
            }
            emit_eh_hook_call(g, "__zan_eh_unhandled");
            LLVMValueRef printf_fn = LLVMGetNamedFunction(g->mod, "printf");
            if (printf_fn) {
                LLVMTypeRef printf_ty = LLVMFunctionType(LLVMInt32TypeInContext(g->ctx),
                    &i8ptr, 1, 1);
                /* A string throw prints its message; a class throw prints a type note */
                LLVMValueRef dexc = LLVMBuildLoad2(g->builder, i8ptr, exc_g,
                    "die.exc");
                LLVMValueRef dtid = LLVMBuildLoad2(g->builder, i8ptr,
                    get_eh_exc_tid_global(g), "die.tid");
                LLVMValueRef dhas = zan_icmp(g->builder, LLVMIntNE, dexc,
                    LLVMConstNull(i8ptr), "die.has");
                LLVMValueRef dstr = zan_icmp(g->builder, LLVMIntEQ, dtid,
                    LLVMConstNull(i8ptr), "die.str");
                LLVMBasicBlockRef die_check_bb =
                    LLVMAppendBasicBlockInContext(g->ctx, fn2, "die.check");
                LLVMBasicBlockRef die_str_bb =
                    LLVMAppendBasicBlockInContext(g->ctx, fn2, "die.str");
                LLVMBasicBlockRef die_cls_bb =
                    LLVMAppendBasicBlockInContext(g->ctx, fn2, "die.cls");
                LLVMBasicBlockRef die_none_bb =
                    LLVMAppendBasicBlockInContext(g->ctx, fn2, "die.none");
                LLVMBasicBlockRef die_cont_bb =
                    LLVMAppendBasicBlockInContext(g->ctx, fn2, "die.cont");
                LLVMBuildCondBr(g->builder, dhas, die_check_bb, die_none_bb);
                LLVMPositionBuilderAtEnd(g->builder, die_check_bb);
                LLVMBuildCondBr(g->builder, dstr, die_str_bb, die_cls_bb);
                LLVMPositionBuilderAtEnd(g->builder, die_str_bb);
                {
                    LLVMValueRef fmt = zan_irgen_intern_string(g,
                        "Unhandled exception: %s\n");
                    LLVMValueRef args[] = { fmt, dexc };
                    zan_call2(g->builder, printf_ty, printf_fn, args, 2, "");
                }
                LLVMBuildBr(g->builder, die_cont_bb);
                /* 内部辅助逻辑 */
                LLVMPositionBuilderAtEnd(g->builder, die_cls_bb);
                {
                    LLVMValueRef name_fn = get_eh_tid_name_fn(g);
                    LLVMValueRef cname = zan_call2(g->builder,
                        LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr }, 1, 0),
                        name_fn, (LLVMValueRef[]){ dtid }, 1, "die.cname");
                    LLVMValueRef found = zan_icmp(g->builder, LLVMIntNE, cname,
                        LLVMConstNull(i8ptr), "die.cfound");
                    LLVMBasicBlockRef named_bb =
                        LLVMAppendBasicBlockInContext(g->ctx, fn2, "die.named");
                    LLVMBasicBlockRef anon_bb =
                        LLVMAppendBasicBlockInContext(g->ctx, fn2, "die.anon");
                    LLVMBuildCondBr(g->builder, found, named_bb, anon_bb);
                    LLVMPositionBuilderAtEnd(g->builder, named_bb);
                    {
                        LLVMValueRef fmt2 = zan_irgen_intern_string(g,
                            "Unhandled exception: %s\n");
                        LLVMValueRef args2[] = { fmt2, cname };
                        zan_call2(g->builder, printf_ty, printf_fn, args2, 2, "");
                    }
                    LLVMBuildBr(g->builder, die_cont_bb);
                    LLVMPositionBuilderAtEnd(g->builder, anon_bb);
                    LLVMValueRef fmt = zan_irgen_intern_string(g,
                        "Unhandled exception (class object)\n");
                    LLVMValueRef args[] = { fmt };
                    zan_call2(g->builder, printf_ty, printf_fn, args, 1, "");
                    LLVMBuildBr(g->builder, die_cont_bb);
                }
                LLVMPositionBuilderAtEnd(g->builder, die_none_bb);
                {
                    LLVMValueRef fmt = zan_irgen_intern_string(g,
                        "Unhandled exception\n");
                    LLVMValueRef args[] = { fmt };
                    zan_call2(g->builder, printf_ty, printf_fn, args, 1, "");
                }
                LLVMBuildBr(g->builder, die_cont_bb);
                LLVMPositionBuilderAtEnd(g->builder, die_cont_bb);
            }
        }
        release_all_arc_locals(g, locals);
        LLVMTypeRef exit_args[] = { LLVMInt32TypeInContext(g->ctx) };
        LLVMTypeRef exit_type = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx),
            exit_args, 1, 0);
        LLVMValueRef exit_fn = LLVMGetNamedFunction(g->mod, "exit");
        if (!exit_fn) {
            exit_fn = LLVMAddFunction(g->mod, "exit", exit_type);
        }
        LLVMValueRef exit_arg = LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 1, 0);
        zan_call2(g->builder, exit_type, exit_fn, &exit_arg, 1, "");
        LLVMBuildUnreachable(g->builder);
        break;
    }

    default:
        break;
    }

    if (arc_nested) g->arc_stmt_depth--;
}

/* 内部辅助实现 */
static void emit_release_static_rc_fields(zan_irgen_t *g, zan_ast_node_t *unit) {
    (void)unit;
    for (int i = 0; i < g->static_field_count; i++) {
        zan_type_t *ft = g->static_fields[i].type;
        if (!ft) continue;
        LLVMValueRef gv = g->static_fields[i].gv;
        LLVMTypeRef lt = map_type(g, ft);
        LLVMValueRef old = LLVMBuildLoad2(g->builder, lt, gv, "sf.rel");
        emit_rc_release_for_type(g, ft, old);
        zan_store_fit(g, LLVMConstNull(lt), gv);
    }
}
