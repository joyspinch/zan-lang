#include "irgen_internal.h"
void emit_release_static_rc_fields(zan_irgen_t *g, zan_ast_node_t *unit);
static bool async_slot_type_compatible(zan_type_t *a, zan_type_t *b) {
    if (a == b) return true;
    if (!a || !b) return false;
    int depth = 0;
    while (a && b && depth < 32) {
        if (a == b) return true;
        /* 类型参数一侧放行：预接线槽冻结的是首趟发射的类型（开放泛型），
         * 固定点重发趟已具体化（E→string）——同一帧槽按 LLVM 布局同型，
         * 按开放实参拒接会让重发趟跌回未接线 alloca（mem2reg 提升后跨
         * 挂起丢值：尾声释放读幻影槽 = 泄漏，await 后再读 = NRE）。 */
        if (a->kind == TYPE_TYPE_PARAM || b->kind == TYPE_TYPE_PARAM) return true;
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

static int irgen_goto_count_owned_locals(local_scope_t *locals) {
    int owned = 0;
    for (int i = 0; i < locals->count; i++) {
        local_var_t *v = &locals->vars[i];
        /* Unnamed pending payloads have compiler-managed exit cleanup. */
        if (v->name.len &&
            (v->arc_owned || v->struct_rc || v->box_cell || v->obj_rc_flag))
            owned++;
    }
    return owned;
}

static LLVMValueRef get_eh_hook_fn(zan_irgen_t *g, const char *name) {
    LLVMValueRef f = LLVMGetNamedFunction(g->mod, name);
    if (f) return f;
    LLVMTypeRef fty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), NULL, 0, 0);
    f = LLVMAddFunction(g->mod, name, fty);
    LLVMSetLinkage(f, LLVMInternalLinkage);
    LLVMBasicBlockRef saved = LLVMGetInsertBlock(g->builder);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, f, "entry");
    LLVMPositionBuilderAtEnd(g->builder, entry);
    LLVMBuildRetVoid(g->builder);
    if (saved) LLVMPositionBuilderAtEnd(g->builder, saved);
    return f;
}
void emit_eh_hook_call(zan_irgen_t *g, const char *name) {
    if (!g->emit_debug) return;
    LLVMValueRef f = get_eh_hook_fn(g, name);
    LLVMTypeRef fty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), NULL, 0, 0);
    zan_call2(g->builder, fty, f, NULL, 0, "");
}
bool call_is_async_callee(zan_irgen_t *g, zan_ast_node_t *call, local_scope_t *locals) {
    if (!call || call->kind != AST_CALL) return false;
    if (call->call.resolved_sym && call->call.resolved_sym->decl &&
        call->call.resolved_sym->decl->kind == AST_METHOD_DECL) {
        return (call->call.resolved_sym->decl->method_decl.modifiers & MOD_ASYNC) != 0;
    }
    zan_ast_node_t *callee = call->call.callee;
    if (!callee) return false;

    if (callee->kind == AST_MEMBER_ACCESS) {
        zan_type_t *obj_ty = infer_expr_type(g, callee->member.object, locals);
        if (obj_ty && obj_ty->kind == TYPE_INTERFACE && obj_ty->sym) {
            zan_symbol_t *iface_m = resolve_iface_overload(obj_ty->sym,
                                        callee->member.name,
                                        call->call.args.count,
                                        call->call.type_args.count);
            if (iface_m && iface_m->decl && iface_m->decl->kind == AST_METHOD_DECL)
                return (iface_m->decl->method_decl.modifiers & MOD_ASYNC) != 0;
        }
        zan_symbol_t *ts = (obj_ty && obj_ty->sym) ? obj_ty->sym : expr_class_sym(g, callee->member.object, locals);
        if (ts) {
            zan_symbol_t *msym = resolve_overload_typed(g, ts, callee->member.name, call, locals);
            if (msym && msym->decl && msym->decl->kind == AST_METHOD_DECL)
                return (msym->decl->method_decl.modifiers & MOD_ASYNC) != 0;
        }
    } else if (callee->kind == AST_IDENTIFIER) {
        if (g->current_type_sym) {
            zan_symbol_t *msym = resolve_overload_typed(g, g->current_type_sym, callee->ident.name, call, locals);
            if (msym && msym->decl && msym->decl->kind == AST_METHOD_DECL)
                return (msym->decl->method_decl.modifiers & MOD_ASYNC) != 0;
        }
    }
    return false;
}
void emit_stmt(zan_irgen_t *g, zan_ast_node_t *stmt, local_scope_t *locals);

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
    /* object 模式变量捕获解禁（B-ID117，同 var 口径）：rc 指针走
     * 下方既有 retain/box/arc_owned 通路。开泛形参仍 fail-closed。 */
    if (captured && pt->kind == TYPE_TYPE_PARAM) {
        zan_diag_emit_code(g->diag, DIAG_ERROR, sc->loc, 3069,
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
        if (pt->kind == TYPE_STRUCT && type_contains_collection_rc(g, pt, 0)) {
            v->struct_rc = 1;
            lpad_register_local(g, v);
        }
        return;
    }

    LLVMValueRef slot = emit_entry_alloca(g, ptll, "cpat");
    zan_store_fit(g, LLVMConstNull(ptll), slot);
    zan_store_fit(g, cv, slot);
    local_add(locals, sc->switch_case.var_name, slot, pt);
    locals->vars[locals->count - 1].binding_decl = sc;
}

static void emit_switch_pattern_fail_release(zan_irgen_t *g,
                                             local_scope_t *locals,
                                             zan_ast_node_t *sc) {
    local_var_t *v = local_find_binding_decl(locals, sc);
    if (!v || !v->box_cell || !v->box_owned) return;
    release_boxed_local(g, v);
}

static void emit_monitor_exit(zan_irgen_t *g, LLVMValueRef obj_slot) {
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef mon_ty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx),
                                          &i8ptr, 1, 0);
    LLVMValueRef exit_fn = LLVMGetNamedFunction(g->mod, "zan_monitor_exit");
    if (!exit_fn) exit_fn = LLVMAddFunction(g->mod, "zan_monitor_exit", mon_ty);
    LLVMValueRef obj = LLVMBuildLoad2(g->builder, i8ptr, obj_slot, "lock.obj");
    zan_call2(g->builder, mon_ty, exit_fn, &obj, 1, "");
}
void emit_eh_disarm_from(zan_irgen_t *g, int base) {
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
        if (g->eh_armed[base].restore_tmps)
            zan_store_fit(g, ot, get_eh_tmp_top_global(g));
        else
            zan_store_fit(g, ot, top_g);
    }
}

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
void emit_finally_body(zan_irgen_t *g, local_scope_t *locals,
                              const zan_irgen_finally_entry_t *entry) {
    zan_irgen_pending_scope_t *saved = g->pending.scope;
    zan_irgen_pending_scope_t *scope = zan_arena_alloc(g->arena, sizeof(*scope));
    if (g->current_async_frame) {
        *scope = *entry->pending_scope;
    } else {
        memset(scope, 0, sizeof(*scope));
        scope->parent = saved;
    }
    /* Host finally bodies are cloned at each exit too: their local labels
     * need separate identities, while outward jumps retain the parent scope. */
    scope->body = entry->body;
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
    int saved_lpad_depth = g->lpad_depth;
    int saved_throw_base = g->throw_locals_base;
    int saved_throw_cbase = g->throw_catch_base;
    void *armed = finally_shared_copy(g, g->eh_armed,
        sizeof(g->eh_armed[0]) * (size_t)saved_armed);
    for (int i = saved - 1; i >= base; i--) {
        zan_irgen_finally_entry_t *entry = &g->finallys[i];
        emit_eh_disarm_from(g, entry->outer_armed_depth);
        g->eh_armed_count = entry->outer_armed_depth;
        g->throw_locals_base = entry->outer_throw_locals_base;
        g->throw_catch_base = entry->outer_throw_catch_base;
        emit_pending_scope_exit(g, entry->pending_parent, preserve_return);
        if (g->finallys[i].monitor_obj) {
            emit_monitor_exit(g, g->finallys[i].monitor_obj);
            continue;
        }
        if (!g->finallys[i].body) continue;
        /* A return has left this try before entering its finally. Invokes in
         * the finally must target an outer handler, never this try's lpad.
         * Nested try emission may reuse the hidden stack entries. */
        int outer_lpad = g->lpad_depth;
        LLVMValueRef fn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder));
        for (int d = g->lpad_depth - 1; d >= 0; d--) {
            if (g->lpad_stack[d].fn != fn) break;
            if (g->lpad_stack[d].fin_idx == i) {
                outer_lpad = d;
                break;
            }
        }
        int hidden_lp_count = saved_lpad_depth - outer_lpad;
        zan_lpad_entry_t *hidden_lp = finally_shared_copy(g,
            &g->lpad_stack[outer_lpad],
            sizeof(*hidden_lp) * (size_t)hidden_lp_count);
        g->lpad_depth = outer_lpad;
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
        if (hidden_lp_count)
            memcpy(&g->lpad_stack[outer_lpad], hidden_lp,
                sizeof(*hidden_lp) * (size_t)hidden_lp_count);
        g->lpad_depth = saved_lpad_depth;
        if (LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder))) break;
    }
    g->finally_count = saved;
    if (saved_armed) memcpy(g->eh_armed, armed,
        sizeof(g->eh_armed[0]) * (size_t)saved_armed);
    g->eh_armed_count = saved_armed;
    g->throw_locals_base = saved_throw_base;
    g->throw_catch_base = saved_throw_cbase;
}

static void emit_finally_on_exception_path(zan_irgen_t *g, local_scope_t *locals,
                                           int fin_idx) {
    if (fin_idx < 0) return;
    if (g->finallys[fin_idx].monitor_obj) {
        emit_monitor_exit(g, g->finallys[fin_idx].monitor_obj);
        return;
    }
    if (!g->finallys[fin_idx].body) return;
    emit_pending_scope_exit(g, g->finallys[fin_idx].pending_parent, false);
    int saved_lpad_depth = g->lpad_depth;
    int outer_lpad = saved_lpad_depth;
    LLVMValueRef fn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder));
    for (int d = saved_lpad_depth - 1; d >= 0; d--) {
        if (g->lpad_stack[d].fn != fn) break;
        if (g->lpad_stack[d].fin_idx >= fin_idx &&
            g->lpad_stack[d].fin_idx >= 0) outer_lpad = d;
    }
    int hidden_lp_count = saved_lpad_depth - outer_lpad;
    zan_lpad_entry_t *hidden_lp = finally_shared_copy(g,
        &g->lpad_stack[outer_lpad], sizeof(*hidden_lp) * (size_t)hidden_lp_count);
    g->lpad_depth = outer_lpad;
    int saved_armed = g->eh_armed_count;
    int saved_throw_base = g->throw_locals_base;
    int saved_throw_cbase = g->throw_catch_base;
    void *armed = finally_shared_copy(g, g->eh_armed,
        sizeof(g->eh_armed[0]) * (size_t)saved_armed);
    if (!g->current_async_frame) {
        zan_irgen_finally_entry_t *entry = &g->finallys[fin_idx];
        emit_eh_disarm_from(g, entry->outer_armed_depth);
        g->eh_armed_count = entry->outer_armed_depth;
        g->throw_locals_base = entry->outer_throw_locals_base;
        g->throw_catch_base = entry->outer_throw_catch_base;
    }
    LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMValueRef top_g, bufs_g, exc_g;
    get_eh_globals(g, &top_g, &bufs_g, &exc_g);
    LLVMValueRef own_g = get_eh_exc_owned_global(g);
    LLVMValueRef tid_g = get_eh_exc_tid_global(g);
    LLVMValueRef s_exc = NULL, s_own = NULL, s_tid = NULL;
    LLVMValueRef owner_flag = NULL;
    int saved_locals = locals->count;
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
        /* Suspend dispatch ownership in a scoped local, like a pending return.
         * Outer landing pads and outward transfers release it; catches created
         * inside this finally start above it and must leave it alive. */
        LLVMTypeRef i1 = LLVMInt1TypeInContext(g->ctx);
        owner_flag = emit_entry_alloca(g, i1, "fin.owns");
        zan_builder_point_t pt = zan_builder_save(g);
        LLVMBasicBlockRef entry = LLVMGetEntryBasicBlock(
            LLVMGetBasicBlockParent(pt.bb));
        LLVMValueRef term = LLVMGetBasicBlockTerminator(entry);
        if (term) LLVMPositionBuilderBefore(g->builder, term);
        else LLVMPositionBuilderAtEnd(g->builder, entry);
        LLVMBuildStore(g->builder, LLVMConstInt(i1, 0, 0), owner_flag);
        zan_builder_restore(g, pt);
        LLVMBuildStore(g->builder, LLVMBuildICmp(g->builder, LLVMIntNE,
            LLVMBuildLoad2(g->builder, i32t, s_own, "fin.own.v"),
            LLVMConstInt(i32t, 0, 0), "fin.isowned"), owner_flag);
        local_add(locals, (zan_istr_t){"", 0}, s_exc, g->binder->type_object);
        locals->vars[locals->count - 1].obj_rc_flag = owner_flag;
        arc_own_local(g, locals);
        zan_store_fit(g, LLVMConstNull(i8ptr), exc_g);
        zan_store_fit(g, LLVMConstInt(i32t, 0, 0), own_g);
        zan_store_fit(g, LLVMConstNull(i8ptr), tid_g);
    }
    int saved_count = g->finally_count;
    int hidden_count = saved_count > fin_idx ? saved_count - fin_idx : 1;
    zan_irgen_finally_entry_t *hidden = finally_shared_copy(g,
        &g->finallys[fin_idx], sizeof(*hidden) * (size_t)hidden_count);
    g->finally_count = fin_idx;
    emit_finally_body(g, locals, &hidden[0]);
    memcpy(&g->finallys[fin_idx], hidden,
           sizeof(*hidden) * (size_t)hidden_count);
    locals->count = saved_locals;
    g->finally_count = saved_count;
    if (hidden_lp_count)
        memcpy(&g->lpad_stack[outer_lpad], hidden_lp,
               sizeof(*hidden_lp) * (size_t)hidden_lp_count);
    g->lpad_depth = saved_lpad_depth;
    if (saved_armed) memcpy(g->eh_armed, armed,
        sizeof(g->eh_armed[0]) * (size_t)saved_armed);
    g->eh_armed_count = saved_armed;
    g->throw_locals_base = saved_throw_base;
    g->throw_catch_base = saved_throw_cbase;
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
        LLVMBuildStore(g->builder,
            LLVMConstInt(LLVMInt1TypeInContext(g->ctx), 0, 0), owner_flag);
        zan_store_fit(g, LLVMConstNull(i8ptr), s_exc);
    }
}
void emit_finallys_left_by_throw(zan_irgen_t *g, local_scope_t *locals) {
    int target_fin = 0;
    LLVMBasicBlockRef cur_bb = LLVMGetInsertBlock(g->builder);
    LLVMValueRef cur_fn = cur_bb ? LLVMGetBasicBlockParent(cur_bb) : NULL;
    for (int i = g->lpad_depth - 1; i >= 0; i--) {
        zan_lpad_entry_t *top = &g->lpad_stack[i];
        if (top->fn != cur_fn) break;
        if (!g->current_async_frame && top->lpad_bb) {
            /* Synchronous throws enter the same lexical landing as callees.
             * Only entries without a landing of their own (locks) run here. */
            target_fin = top->finally_depth;
            break;
        }
        if (top->catch_bb) {
            target_fin = (top->fin_idx >= 0) ? top->fin_idx + 1 : 0;
            break;
        }
    }
    for (int i = g->finally_count - 1; i >= target_fin; i--) {
        emit_finally_on_exception_path(g, locals, i);
        if (LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder))) return;
    }
}

static void emit_finallys_below_for_propagate(zan_irgen_t *g,
                                              local_scope_t *locals,
                                              int fin_idx) {
    if (LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder))) return;
    if (!g->current_async_frame) return; /* Outer landings own their finally. */
    for (int i = fin_idx - 1; i >= 0; i--) {
        if (g->finallys[i].in_try_body) break;
        emit_finally_on_exception_path(g, locals, i);
        if (LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder))) return;
    }
}

static void emit_eh_propagate_tail_lpad(zan_irgen_t *g,
                                        local_scope_t *locals,
                                        LLVMValueRef lp_slot,
                                        LLVMTypeRef lp_ret_ty,
                                        bool is_catch_rethrow) {
    if (g->current_async_frame) {
        /* This try's finally has run; enclosing active tries still own
         * their cleanup and must receive the exception in lexical order. */
        emit_raise_exception_through_cleanups(g);
        return;
    }
    LLVMBasicBlockRef cur_bb = LLVMGetInsertBlock(g->builder);
    LLVMValueRef cur_fn = cur_bb ? LLVMGetBasicBlockParent(cur_bb) : NULL;
    bool enclosing = false;
    for (int i = g->lpad_depth - 1; i >= 0; i--) {
        if (g->lpad_stack[i].fn != cur_fn) break;
        if (g->lpad_stack[i].lpad_bb) {
            enclosing = true;
            break;
        }
    }
    if (enclosing) {
        /* A branch straight to catch skipped intervening finally-only tries
         * and their cleanup ledgers. Re-enter only the next outer landing. */
        emit_raise_exception_through_cleanups(g);
    } else if (is_catch_rethrow) {
        emit_release_owned_locals(g, locals);
        emit_raise_exception(g);
    } else {
        if (g->lpad_depth > 0) {
            emit_lpad_cleanups(g, &g->lpad_stack[0]);
        }
        LLVMValueRef cur_lp = LLVMBuildLoad2(g->builder, lp_ret_ty, lp_slot, "lp.prop");
        /* A finally-only catch-all can be the selected native handler even
         * when no Zan catch remains. Resume is only valid during cleanup
         * unwinding; resuming a selected SEH handler targets this same frame
         * again and reruns its ARC cleanup/finally. Restart the search for a
         * handler outside this region while preserving the native payload. */
        LLVMValueRef selector = LLVMBuildExtractValue(g->builder, cur_lp, 1,
                                                       "lp.selector");
        LLVMBasicBlockRef rethrow = LLVMAppendBasicBlockInContext(g->ctx, cur_fn,
                                                                  "lp.rethrow");
        LLVMBasicBlockRef resume = LLVMAppendBasicBlockInContext(g->ctx, cur_fn,
                                                                 "lp.resume");
        LLVMBuildCondBr(g->builder, LLVMBuildICmp(g->builder, LLVMIntNE,
            selector, LLVMConstNull(LLVMTypeOf(selector)), "lp.handler"),
            rethrow, resume);
        LLVMPositionBuilderAtEnd(g->builder, rethrow);
        LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
        LLVMValueRef native_exc = LLVMBuildExtractValue(g->builder, cur_lp, 0,
                                                         "lp.exception");
        LLVMTypeRef raise_ty = LLVMFunctionType(LLVMInt32TypeInContext(g->ctx),
                                                &i8ptr, 1, 0);
        LLVMValueRef raise_fn = get_libc_fn(g, "_Unwind_RaiseException", raise_ty);
        zan_call2(g->builder, raise_ty, raise_fn, &native_exc, 1, "");
        /* Returning means propagation failed (END_OF_STACK or an unwind
         * error). Resume_or_Rethrow aborts on SEH; use the installed terminate
         * handler so the original Zan exception is diagnosed consistently. */
        LLVMTypeRef void_fn_ty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx),
                                                   NULL, 0, 0);
        LLVMValueRef terminate_fn = get_libc_fn(g, "_ZSt9terminatev", void_fn_ty);
        zan_call2(g->builder, void_fn_ty, terminate_fn, NULL, 0, "");
        LLVMBuildUnreachable(g->builder);
        LLVMPositionBuilderAtEnd(g->builder, resume);
        LLVMBuildResume(g->builder, cur_lp);
    }
}

static int call_targets_extern(zan_irgen_t *g, zan_ast_node_t *expr) {
    if (!expr || expr->kind != AST_CALL || !expr->call.callee) return 0;
    zan_symbol_t *sym = NULL;
    if (expr->call.callee->kind == AST_IDENTIFIER) {
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
    /* object 捕获解禁（B-ID117）：object 是 rc 管理的 i8*，与 CLASS 捕获
     * 走同一条 box+ARC 通路（dtor 按类型发 release）。只保留开泛形参的
     * fail-closed 拦截——擦除槽 i64 没有运行期类型信息，闭包若比调用帧
     * 长寿无法代为持有。 */
    if (type->kind == TYPE_TYPE_PARAM) {
        zan_diag_emit_code(g->diag, DIAG_ERROR, stmt->loc, 3070,
            "cannot capture local '%.*s' of type '%.*s': its runtime ownership is not representable yet",
            (int)stmt->var_decl.name.len, stmt->var_decl.name.str,
            (int)type->name.len, type->name.str ? type->name.str : "");
        return 0;
    }
    if (!rc && k != LLVMIntegerTypeKind && k != LLVMFloatTypeKind &&
        k != LLVMDoubleTypeKind && k != LLVMStructTypeKind) {
        zan_diag_emit_code(g->diag, DIAG_ERROR, stmt->loc, 3071,
            "cannot capture local '%.*s': unsupported value representation",
            (int)stmt->var_decl.name.len, stmt->var_decl.name.str);
        return 0;
    }

    LLVMValueRef slot;
    if (g->current_async_frame) {
        local_var_t *pre = local_find_async_decl(locals, stmt);
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
    } else {
        LLVMValueRef cell = emit_box_cell(g, stmt->loc, payload, type, NULL);
        slot = box_value_ptr(g, cell, payload);
        local_add(locals, stmt->var_decl.name, slot, type);
        own_box_cell(g, &locals->vars[locals->count - 1], cell);
    }
    if (rc) locals->vars[locals->count - 1].arc_owned = 1;
    if (aggregate_rc) {
        locals->vars[locals->count - 1].struct_rc = 1;
        lpad_register_local(g, &locals->vars[locals->count - 1]);
    }
    if (type && type->kind == TYPE_STRING && stmt->var_decl.initializer &&
        call_targets_extern(g, stmt->var_decl.initializer))
        locals->vars[locals->count - 1].opaque_string = 1;
    if (stmt->var_decl.initializer) {
        LLVMValueRef init =
            (type->kind == TYPE_DELEGATE &&
             stmt->var_decl.initializer->kind == AST_LAMBDA)
                ? emit_lambda_typed(g, stmt->var_decl.initializer, type, locals)
                : emit_expr(g, stmt->var_decl.initializer, locals);
        init = emit_implicit_box(g, type, init, stmt->var_decl.initializer, locals);
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

/* 经接口引用调用零参成员（foreach 协议的 GetEnumerator/MoveNext/Current，
 * A381 第一步：枚举器接口形态）。把接收者值存进临时槽并以接口类型注册为
 * 隐藏局部，合成成员调用走常规调用路径——tag 分发、空接收者守卫、ARC
 * 全部复用既有成员调用降级，不另写一份接口分发。 */
static LLVMValueRef emit_iface_call0(zan_irgen_t *g, zan_type_t *iface_ty,
                                     const char *mname, LLVMValueRef recv,
                                     local_scope_t *locals, zan_loc_t loc) {
    int saved = locals->count;
    LLVMTypeRef slot_ll = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMValueRef slot = emit_entry_alloca(g, slot_ll, "fe.ifc");
    zan_store_fit(g, LLVMBuildBitCast(g->builder, recv, slot_ll, "fe.ifc.b"),
                  slot);
    char nm[16];
    snprintf(nm, sizeof(nm), "$fe.ifc");
    zan_istr_t ni = { nm, (uint32_t)strlen(nm) };
    local_add(locals, ni, slot, iface_ty);
    zan_ast_node_t *id = zan_ast_new(g->arena, AST_IDENTIFIER, loc);
    id->ident.name = ni;
    zan_ast_node_t *mem = zan_ast_new(g->arena, AST_MEMBER_ACCESS, loc);
    mem->member.object = id;
    mem->member.name = (zan_istr_t){ (char *)mname, (uint32_t)strlen(mname) };
    mem->member.null_cond = 0;
    zan_ast_node_t *call = zan_ast_new(g->arena, AST_CALL, loc);
    call->call.callee = mem;
    zan_ast_list_init(&call->call.args);
    zan_ast_list_init(&call->call.type_args);
    LLVMValueRef r = emit_expr(g, call, locals);
    locals->count = saved;
    return r;
}
