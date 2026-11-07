#include "irgen_internal.h"

/* Composite Dispatcher Submodules */
#include "stmt/stmt_helpers.c"

void emit_stmt(zan_irgen_t *g, zan_ast_node_t *stmt, local_scope_t *locals) {
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
