#include "irgen_internal.h"

/* Composite Dispatcher Submodules */
#include "stmt/stmt_helpers.c"
#include "stmt/stmt_borrow.c"

static void emit_stmt_impl(zan_irgen_t *g, zan_ast_node_t *stmt, local_scope_t *locals);

void emit_stmt(zan_irgen_t *g, zan_ast_node_t *stmt, local_scope_t *locals) {
    void *saved_locals = g->active_cleanup_locals;
    LLVMValueRef saved_fn = g->active_cleanup_fn;
    if (locals) {
        g->active_cleanup_locals = locals;
        LLVMBasicBlockRef bb = LLVMGetInsertBlock(g->builder);
        g->active_cleanup_fn = bb ? LLVMGetBasicBlockParent(bb) : NULL;
    }
    emit_stmt_impl(g, stmt, locals);
    g->active_cleanup_locals = saved_locals;
    g->active_cleanup_fn = saved_fn;
}

static void emit_stmt_impl(zan_irgen_t *g, zan_ast_node_t *stmt, local_scope_t *locals) {
    if (!stmt) return;
    if (locals) g->current_locals = locals;

    LLVMBasicBlockRef cur_bb = LLVMGetInsertBlock(g->builder);
    if (cur_bb && LLVMGetBasicBlockTerminator(cur_bb)) {
        LLVMBasicBlockRef dead_bb = LLVMAppendBasicBlockInContext(
            g->ctx, LLVMGetBasicBlockParent(cur_bb), "dead");
        LLVMPositionBuilderAtEnd(g->builder, dead_bb);
    }

    di_set_loc(g, stmt->loc);

    int arc_nested = (stmt->kind == AST_IF_STMT || stmt->kind == AST_WHILE_STMT ||
                      stmt->kind == AST_DO_WHILE_STMT || stmt->kind == AST_FOR_STMT ||
                      stmt->kind == AST_FOREACH_STMT || stmt->kind == AST_SWITCH_STMT ||
                      stmt->kind == AST_TRY_STMT);
    if (arc_nested) g->arc_stmt_depth++;

    switch (stmt->kind) {
#include "stmt/stmt_decl.c"
#include "stmt/stmt_flow.c"
#include "stmt/stmt_try.c"
#include "stmt/stmt_loop.c"
#include "stmt/stmt_jump.c"
    default:
        break;
    }

    if (arc_nested) g->arc_stmt_depth--;
}

void emit_release_static_rc_fields(zan_irgen_t *g, zan_ast_node_t *unit) {
    (void)unit;
    if (!g->static_init_cleanup) {
        LLVMTypeRef ty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), NULL, 0, 0);
        g->static_init_cleanup = LLVMAddFunction(g->mod, "__zan_static_cleanup", ty);
        zan_set_module_local(g->static_init_cleanup);
    }
    zan_call2(g->builder, LLVMGlobalGetValueType(g->static_init_cleanup),
              g->static_init_cleanup, NULL, 0, "");
    /* 只有主函数退出路径会释放静态字段：此后的退出报告按严格口径计数（S13） */
    if (g->check_leaks)
        LLVMBuildStore(g->builder, LLVMConstInt(LLVMInt8TypeInContext(g->ctx), 1, 0),
                       zan_leak_main_done_global(g));
}

/* Main and the terminate handler are emitted before the reachability closure.
 * Fill one shared cleanup afterwards, including fields found by late cctors and
 * reflection constructors, without letting cleanup reads activate their types. */
void finalize_static_rc_cleanup(zan_irgen_t *g) {
    if (!g->static_init_cleanup || !LLVMIsDeclaration(g->static_init_cleanup)) return;
    LLVMValueRef saved_fn = g->current_fn;
    LLVMBasicBlockRef saved_bb = LLVMGetInsertBlock(g->builder);
    g->current_fn = g->static_init_cleanup;
    LLVMPositionBuilderAtEnd(g->builder, LLVMAppendBasicBlockInContext(
        g->ctx, g->static_init_cleanup, "entry"));
    di_clear(g);
    for (int i = 0; i < g->static_field_count; i++) {
        zan_type_t *ft = g->static_fields[i].type;
        if (!ft) continue;
        bool active = false;
        for (int t = 0; t < g->static_init_type_count; t++) {
            if (g->static_init_types[t].sym != g->static_fields[i].owner) continue;
            zan_type_t *inst = g->static_init_types[t].inst;
            zan_type_t *field_inst = g->static_fields[i].inst;
            if ((!inst && !field_inst) ||
                (inst && field_inst && types_equal(inst, field_inst))) {
                active = true;
                break;
            }
        }
        if (!active) continue;
        LLVMValueRef gv = g->static_fields[i].gv;
        LLVMTypeRef lt = map_type(g, ft);
        LLVMValueRef old = LLVMBuildLoad2(g->builder, lt, gv, "sf.rel");
        emit_rc_release_for_type(g, ft, old);
        zan_store_fit(g, LLVMConstNull(lt), gv);
    }
    LLVMBuildRetVoid(g->builder);
    g->current_fn = saved_fn;
    if (saved_bb) LLVMPositionBuilderAtEnd(g->builder, saved_bb);
    else LLVMClearInsertionPosition(g->builder);
}
