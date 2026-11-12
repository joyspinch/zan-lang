#include "irgen_internal.h"


/* ---- B-ID92: 静态初始化依赖拓扑排序 ----
 * Main 开头曾按 AST 声明序盲目跑各类型的静态字段初始化与 _cctor：A 的
 * 初始化器读 B 的静态字段而 B 声明在后，读到的就是未初始化零值/空引用，
 * 且随文件编译顺序漂移。这里扫描每个类型的静态初始化器 + 静态构造体，
 * 对其引用到的本编译单元其他类型建边，DFS 后序得到"被依赖者先初始化"
 * 的顺序；环路成员退回声明序（C# 对环同样不保证）。 */
static bool sinit_istr_eq(zan_istr_t a, zan_istr_t b) {
    return a.len == b.len && a.len > 0 && memcmp(a.str, b.str, (size_t)a.len) == 0;
}

typedef struct {
    zan_ast_node_t *unit;
    int n;            /* decls.count */
    uint8_t *dep;     /* n*n，dep[a*n+b]=1 表示 a 依赖 b */
} sinit_graph_t;

static int sinit_type_index(sinit_graph_t *gr, zan_istr_t name) {
    for (int i = 0; i < gr->n; i++) {
        zan_ast_node_t *d = gr->unit->comp_unit.decls.items[i];
        if (d->kind != AST_CLASS_DECL && d->kind != AST_STRUCT_DECL) continue;
        if (sinit_istr_eq(d->type_decl.name, name)) return i;
    }
    return -1;
}

static void sinit_scan_list(sinit_graph_t *gr, int self, zan_ast_list_t *l, int depth);

static void sinit_scan(sinit_graph_t *gr, int self, zan_ast_node_t *n, int depth) {
    if (!n || depth > 64) return;
    switch (n->kind) {
    case AST_IDENTIFIER: {
        int j = sinit_type_index(gr, n->ident.name);
        if (j >= 0 && j != self) gr->dep[self * gr->n + j] = 1;
        return;
    }
    case AST_TYPE_REF: {
        int j = sinit_type_index(gr, n->type_ref.name);
        if (j >= 0 && j != self) gr->dep[self * gr->n + j] = 1;
        sinit_scan_list(gr, self, &n->type_ref.type_args, depth + 1);
        return;
    }
    case AST_QUALIFIED_NAME:
        sinit_scan_list(gr, self, &n->qualified_name.parts, depth + 1);
        return;
    case AST_MEMBER_ACCESS:
        sinit_scan(gr, self, n->member.object, depth + 1);
        return;
    case AST_BINARY: case AST_ASSIGNMENT:
        sinit_scan(gr, self, n->binary.left, depth + 1);
        sinit_scan(gr, self, n->binary.right, depth + 1);
        return;
    case AST_UNARY: case AST_POSTFIX_UNARY:
        sinit_scan(gr, self, n->unary.operand, depth + 1);
        return;
    case AST_CALL:
        sinit_scan(gr, self, n->call.callee, depth + 1);
        sinit_scan_list(gr, self, &n->call.args, depth + 1);
        sinit_scan_list(gr, self, &n->call.type_args, depth + 1);
        return;
    case AST_INDEX:
        sinit_scan(gr, self, n->index.object, depth + 1);
        sinit_scan(gr, self, n->index.index, depth + 1);
        sinit_scan_list(gr, self, &n->index.extra, depth + 1);
        return;
    case AST_CONDITIONAL:
        sinit_scan(gr, self, n->conditional.cond, depth + 1);
        sinit_scan(gr, self, n->conditional.then_expr, depth + 1);
        sinit_scan(gr, self, n->conditional.else_expr, depth + 1);
        return;
    case AST_NEW_EXPR:
        sinit_scan(gr, self, n->new_expr.type, depth + 1);
        sinit_scan(gr, self, n->new_expr.call_init, depth + 1);
        sinit_scan_list(gr, self, &n->new_expr.args, depth + 1);
        sinit_scan_list(gr, self, &n->new_expr.arg_inits, depth + 1);
        return;
    case AST_CAST_EXPR:
        sinit_scan(gr, self, n->cast.type, depth + 1);
        sinit_scan(gr, self, n->cast.expr, depth + 1);
        return;
    case AST_IS_EXPR: case AST_AS_EXPR:
        sinit_scan(gr, self, n->type_test.expr, depth + 1);
        sinit_scan(gr, self, n->type_test.type, depth + 1);
        return;
    case AST_TYPEOF_EXPR: case AST_SIZEOF_EXPR:
        sinit_scan(gr, self, n->cast.type, depth + 1);
        return;
    case AST_AWAIT_EXPR:
        sinit_scan(gr, self, n->await_expr.expr, depth + 1);
        return;
    case AST_LAMBDA:
        sinit_scan(gr, self, n->lambda.body, depth + 1);
        return;
    case AST_STRING_INTERP:
        sinit_scan_list(gr, self, &n->string_interp.parts, depth + 1);
        return;
    case AST_TUPLE_EXPR:
        sinit_scan_list(gr, self, &n->tuple_expr.items, depth + 1);
        return;
    case AST_COLL_INIT:
        sinit_scan_list(gr, self, &n->coll_init.items, depth + 1);
        return;
    case AST_REF_ARG:
        sinit_scan(gr, self, n->ref_arg.expr, depth + 1);
        return;
    case AST_NAMED_ARG:
        sinit_scan(gr, self, n->named_arg.expr, depth + 1);
        return;
    case AST_BLOCK:
        sinit_scan_list(gr, self, &n->block.stmts, depth + 1);
        return;
    case AST_EXPR_STMT:
        sinit_scan(gr, self, n->expr_stmt.expr, depth + 1);
        return;
    case AST_VAR_DECL:
        sinit_scan(gr, self, n->var_decl.type, depth + 1);
        sinit_scan(gr, self, n->var_decl.initializer, depth + 1);
        return;
    case AST_RETURN_STMT:
        sinit_scan(gr, self, n->ret.value, depth + 1);
        return;
    case AST_THROW_STMT:
        sinit_scan(gr, self, n->throw_stmt.value, depth + 1);
        return;
    case AST_IF_STMT:
        sinit_scan(gr, self, n->if_stmt.cond, depth + 1);
        sinit_scan(gr, self, n->if_stmt.then_body, depth + 1);
        sinit_scan(gr, self, n->if_stmt.else_body, depth + 1);
        return;
    case AST_WHILE_STMT: case AST_DO_WHILE_STMT:
        sinit_scan(gr, self, n->while_stmt.cond, depth + 1);
        sinit_scan(gr, self, n->while_stmt.body, depth + 1);
        return;
    case AST_FOR_STMT:
        sinit_scan(gr, self, n->for_stmt.init, depth + 1);
        sinit_scan(gr, self, n->for_stmt.cond, depth + 1);
        sinit_scan(gr, self, n->for_stmt.step, depth + 1);
        sinit_scan(gr, self, n->for_stmt.body, depth + 1);
        return;
    case AST_FOREACH_STMT:
        sinit_scan(gr, self, n->foreach_stmt.var_type, depth + 1);
        sinit_scan(gr, self, n->foreach_stmt.collection, depth + 1);
        sinit_scan(gr, self, n->foreach_stmt.body, depth + 1);
        return;
    case AST_TRY_STMT:
        sinit_scan(gr, self, n->try_stmt.try_body, depth + 1);
        sinit_scan_list(gr, self, &n->try_stmt.catches, depth + 1);
        sinit_scan(gr, self, n->try_stmt.finally_body, depth + 1);
        return;
    case AST_CATCH_CLAUSE:
        sinit_scan(gr, self, n->catch_clause.type, depth + 1);
        sinit_scan(gr, self, n->catch_clause.body, depth + 1);
        return;
    case AST_SWITCH_STMT:
        sinit_scan(gr, self, n->switch_stmt.expr, depth + 1);
        sinit_scan_list(gr, self, &n->switch_stmt.cases, depth + 1);
        return;
    case AST_SWITCH_CASE:
        sinit_scan(gr, self, n->switch_case.pattern, depth + 1);
        sinit_scan(gr, self, n->switch_case.when_cond, depth + 1);
        sinit_scan(gr, self, n->switch_case.body, depth + 1);
        return;
    case AST_SWITCH_EXPR:
        sinit_scan(gr, self, n->switch_expr.expr, depth + 1);
        sinit_scan_list(gr, self, &n->switch_expr.arms, depth + 1);
        return;
    case AST_SWITCH_ARM:
        sinit_scan(gr, self, n->switch_arm.pattern, depth + 1);
        sinit_scan(gr, self, n->switch_arm.when_cond, depth + 1);
        sinit_scan(gr, self, n->switch_arm.result, depth + 1);
        return;
    case AST_LOCK_STMT:
        sinit_scan(gr, self, n->lock_stmt.expr, depth + 1);
        sinit_scan(gr, self, n->lock_stmt.body, depth + 1);
        return;
    case AST_CHECKED_STMT:
        sinit_scan(gr, self, n->checked_stmt.body, depth + 1);
        return;
    default:
        /* 其余节点（字面量、this/base、查询表达式等）不产生跨类型依赖，
         * 或极少出现在静态初始化器中；漏扫只退回声明序，不会更差。 */
        return;
    }
}

static void sinit_scan_list(sinit_graph_t *gr, int self, zan_ast_list_t *l, int depth) {
    if (!l) return;
    for (int i = 0; i < l->count; i++) sinit_scan(gr, self, l->items[i], depth);
}

static void sinit_visit(sinit_graph_t *gr, int i, uint8_t *color, int *order, int *pos) {
    if (color[i]) return;          /* 1=在栈上（环，跳过）/ 2=已完成 */
    color[i] = 1;
    for (int j = 0; j < gr->n; j++)
        if (gr->dep[i * gr->n + j] && color[j] != 1) sinit_visit(gr, j, color, order, pos);
    color[i] = 2;
    order[(*pos)++] = i;
}

/* 返回 decls 下标的排列（malloc，调用方 free）：被依赖的类型排在前面，
 * 其余保持声明序。 */
static int *emit_static_init_order(zan_ast_node_t *unit) {
    int n = unit->comp_unit.decls.count;
    int *order = (int *)malloc(sizeof(int) * (size_t)(n > 0 ? n : 1));
    if (n <= 0) return order;
    sinit_graph_t gr;
    gr.unit = unit;
    gr.n = n;
    gr.dep = (uint8_t *)calloc((size_t)n * (size_t)n, 1);
    uint8_t *color = (uint8_t *)calloc((size_t)n, 1);
    if (!gr.dep || !color) {
        free(gr.dep); free(color);
        for (int i = 0; i < n; i++) order[i] = i;
        return order;
    }
    for (int i = 0; i < n; i++) {
        zan_ast_node_t *d = unit->comp_unit.decls.items[i];
        if (d->kind != AST_CLASS_DECL && d->kind != AST_STRUCT_DECL) continue;
        for (int mi = 0; mi < d->type_decl.members.count; mi++) {
            zan_ast_node_t *m = d->type_decl.members.items[mi];
            if ((m->kind == AST_FIELD_DECL || m->kind == AST_PROPERTY_DECL) &&
                (m->field_decl.modifiers & MOD_STATIC) && m->field_decl.initializer)
                sinit_scan(&gr, i, m->field_decl.initializer, 0);
            else if (m->kind == AST_CONSTRUCTOR_DECL &&
                     (m->method_decl.modifiers & MOD_STATIC) && m->method_decl.body)
                sinit_scan(&gr, i, m->method_decl.body, 0);
        }
    }
    int pos = 0;
    for (int i = 0; i < n; i++) sinit_visit(&gr, i, color, order, &pos);
    for (int i = 0; i < n && pos < n; i++) { /* 理论上不会发生，兜底补齐 */
        bool seen = false;
        for (int k = 0; k < pos; k++) if (order[k] == i) { seen = true; break; }
        if (!seen) order[pos++] = i;
    }
    free(gr.dep);
    free(color);
    return order;
}

static void emit_eh_terminate_setup(zan_irgen_t *g, bool define_body) {
    if (g->target_is_wasm) return;
    LLVMTypeRef void_ty = LLVMVoidTypeInContext(g->ctx);
    LLVMTypeRef term_fn_ty = LLVMFunctionType(void_ty, NULL, 0, 0);
    LLVMValueRef term_fn = LLVMGetNamedFunction(g->mod, "__zan_eh_terminate");
    if (!term_fn)
        term_fn = LLVMAddFunction(g->mod, "__zan_eh_terminate", term_fn_ty);
    /* Main must finish discovering static fields before its terminal exit
     * can share the same ARC cleanup as the normal exit. */
    if (define_body && LLVMCountBasicBlocks(term_fn) == 0) {
        LLVMBasicBlockRef tbb = LLVMAppendBasicBlockInContext(g->ctx, term_fn, "entry");
        zan_builder_point_t pt = zan_builder_save(g);
        g->current_fn = term_fn;
        LLVMPositionBuilderAtEnd(g->builder, tbb);
        di_clear(g);
        emit_eh_hook_call(g, "__zan_eh_unhandled");
        LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
        LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
        LLVMValueRef top_g, bufs_g, exc_g;
        get_eh_globals(g, &top_g, &bufs_g, &exc_g);
        LLVMValueRef printf_fn = get_libc_fn(g, "printf", LLVMFunctionType(i32t, &i8ptr, 1, 1));
        LLVMValueRef exc = LLVMBuildLoad2(g->builder, i8ptr, exc_g, "term.exc");
        LLVMValueRef tid = LLVMBuildLoad2(g->builder, i8ptr, get_eh_exc_tid_global(g), "term.tid");
        LLVMValueRef hasExc = zan_icmp(g->builder, LLVMIntNE, exc, LLVMConstNull(i8ptr), "term.has");
        LLVMValueRef isStr = zan_icmp(g->builder, LLVMIntEQ, tid, LLVMConstNull(i8ptr), "term.isstr");
        LLVMBasicBlockRef fn_bb = LLVMGetInsertBlock(g->builder);
        LLVMValueRef cur_fn = LLVMGetBasicBlockParent(fn_bb);
        LLVMBasicBlockRef check_bb = LLVMAppendBasicBlockInContext(g->ctx, cur_fn, "term.check");
        LLVMBasicBlockRef str_bb = LLVMAppendBasicBlockInContext(g->ctx, cur_fn, "term.str");
        LLVMBasicBlockRef cls_bb = LLVMAppendBasicBlockInContext(g->ctx, cur_fn, "term.cls");
        LLVMBasicBlockRef none_bb = LLVMAppendBasicBlockInContext(g->ctx, cur_fn, "term.none");
        LLVMBasicBlockRef cont_bb = LLVMAppendBasicBlockInContext(g->ctx, cur_fn, "term.cont");
        LLVMBuildCondBr(g->builder, hasExc, check_bb, none_bb);
        LLVMPositionBuilderAtEnd(g->builder, check_bb);
        LLVMBuildCondBr(g->builder, isStr, str_bb, cls_bb);
        LLVMPositionBuilderAtEnd(g->builder, str_bb);
        {
            LLVMValueRef sfmt = zan_irgen_intern_string(g, "Unhandled exception: %s\n");
            zan_call2(g->builder, LLVMFunctionType(i32t, &i8ptr, 1, 1), printf_fn,
                (LLVMValueRef[]){ sfmt, exc }, 2, "");
            LLVMBuildBr(g->builder, cont_bb);
        }
        LLVMPositionBuilderAtEnd(g->builder, cls_bb);
        {
            LLVMValueRef name_fn = get_eh_tid_name_fn(g);
            LLVMValueRef cname = zan_call2(g->builder,
                LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr }, 1, 0),
                name_fn, (LLVMValueRef[]){ tid }, 1, "term.cname");
            LLVMValueRef found = zan_icmp(g->builder, LLVMIntNE, cname,
                LLVMConstNull(i8ptr), "term.cfound");
            LLVMBasicBlockRef named_bb = LLVMAppendBasicBlockInContext(g->ctx, cur_fn, "term.named");
            LLVMBasicBlockRef anon_bb = LLVMAppendBasicBlockInContext(g->ctx, cur_fn, "term.anon");
            LLVMBuildCondBr(g->builder, found, named_bb, anon_bb);
            LLVMPositionBuilderAtEnd(g->builder, named_bb);
            emit_unhandled_class_exc_print(g, cur_fn, LLVMFunctionType(i32t, &i8ptr, 1, 1),
                printf_fn, cname, exc, tid, cont_bb);
            LLVMPositionBuilderAtEnd(g->builder, anon_bb);
            LLVMValueRef afmt = zan_irgen_intern_string(g, "Unhandled exception: class instance\n");
            zan_call2(g->builder, LLVMFunctionType(i32t, &i8ptr, 1, 1), printf_fn,
                (LLVMValueRef[]){ afmt }, 1, "");
            LLVMBuildBr(g->builder, cont_bb);
        }
        LLVMPositionBuilderAtEnd(g->builder, none_bb);
        {
            LLVMValueRef nfmt = zan_irgen_intern_string(g, "Unhandled exception\n");
            zan_call2(g->builder, LLVMFunctionType(i32t, &i8ptr, 1, 1), printf_fn,
                (LLVMValueRef[]){ nfmt }, 1, "");
            LLVMBuildBr(g->builder, cont_bb);
        }
        LLVMPositionBuilderAtEnd(g->builder, cont_bb);
        /* The native unwind payload only transports this TLS owner. Release
         * the Zan value once, after its diagnostic, without treating a foreign
         * native exception as an ARC object. */
        LLVMValueRef owned_g = get_eh_exc_owned_global(g);
        LLVMValueRef owned = LLVMBuildLoad2(g->builder, i32t, owned_g, "term.owned");
        zan_store_fit(g, LLVMConstInt(i32t, 0, 0), owned_g);
        zan_store_fit(g, LLVMConstNull(i8ptr), exc_g);
        zan_store_fit(g, LLVMConstNull(i8ptr), get_eh_exc_tid_global(g));
        LLVMBasicBlockRef release_bb = LLVMAppendBasicBlockInContext(g->ctx, cur_fn, "term.release");
        LLVMBasicBlockRef release_obj = LLVMAppendBasicBlockInContext(g->ctx, cur_fn, "term.release.obj");
        LLVMBasicBlockRef release_str = LLVMAppendBasicBlockInContext(g->ctx, cur_fn, "term.release.str");
        LLVMBasicBlockRef cleanup_bb = LLVMAppendBasicBlockInContext(g->ctx, cur_fn, "term.cleanup");
        LLVMBuildCondBr(g->builder, zan_icmp(g->builder, LLVMIntNE, owned,
            LLVMConstInt(i32t, 0, 0), "term.isowned"), release_bb, cleanup_bb);
        LLVMPositionBuilderAtEnd(g->builder, release_bb);
        LLVMBuildCondBr(g->builder, isStr, release_str, release_obj);
        LLVMPositionBuilderAtEnd(g->builder, release_obj);
        zan_call2(g->builder, LLVMGlobalGetValueType(g->rt_release_dyn),
            g->rt_release_dyn, &exc, 1, "");
        LLVMBuildBr(g->builder, cleanup_bb);
        LLVMPositionBuilderAtEnd(g->builder, release_str);
        zan_call2(g->builder, LLVMGlobalGetValueType(g->rt_str_release),
            g->rt_str_release, &exc, 1, "");
        LLVMBuildBr(g->builder, cleanup_bb);
        LLVMPositionBuilderAtEnd(g->builder, cleanup_bb);
        emit_release_static_rc_fields(g, NULL);
        LLVMValueRef exit_fn = get_libc_fn(g, "exit", LLVMFunctionType(void_ty, &i32t, 1, 0));
        LLVMValueRef code1 = LLVMConstInt(i32t, 1, 0);
        zan_call2(g->builder, LLVMFunctionType(void_ty, &i32t, 1, 0), exit_fn, &code1, 1, "");
        LLVMBuildUnreachable(g->builder);
        zan_builder_restore(g, pt);
    }
    if (define_body) return;

    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef st_ty = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr }, 1, 0);
    LLVMValueRef set_term = LLVMGetNamedFunction(g->mod, "_ZSt13set_terminatePFvvE");
    if (!set_term) set_term = LLVMAddFunction(g->mod, "_ZSt13set_terminatePFvvE", st_ty);
    LLVMValueRef term_ptr = LLVMBuildBitCast(g->builder, term_fn, i8ptr, "term.fp");
    zan_call2(g->builder, st_ty, set_term, &term_ptr, 1, "");
}

static void emit_main_method(zan_irgen_t *g, zan_ast_node_t *method, zan_symbol_t *type_sym,
                             zan_ast_node_t *unit) {
    LLVMTypeRef i32ty = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef i8ptrptr = LLVMPointerType(i8ptr, 0);
    LLVMTypeRef main_params[] = { i32ty, i8ptrptr };
    LLVMTypeRef main_type = LLVMFunctionType(i32ty, main_params, 2, 0);
    LLVMValueRef main_fn = LLVMAddFunction(g->mod, "main", main_type);

    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, main_fn, "entry");
    LLVMPositionBuilderAtEnd(g->builder, entry);
    di_clear(g);
    emit_eh_terminate_setup(g, false);

    if (g->target_is_windows) {
        LLVMTypeRef uintt = LLVMInt32TypeInContext(g->ctx);
        LLVMTypeRef setcp_type = LLVMFunctionType(uintt, (LLVMTypeRef[]){ uintt }, 1, 0);
        LLVMValueRef fn_set_out = LLVMGetNamedFunction(g->mod, "SetConsoleOutputCP");
        if (!fn_set_out) fn_set_out = LLVMAddFunction(g->mod, "SetConsoleOutputCP", setcp_type);
        LLVMValueRef fn_set_in = LLVMGetNamedFunction(g->mod, "SetConsoleCP");
        if (!fn_set_in) fn_set_in = LLVMAddFunction(g->mod, "SetConsoleCP", setcp_type);
        LLVMValueRef cp_utf8 = LLVMConstInt(uintt, 65001, 0);
        zan_call2(g->builder, setcp_type, fn_set_out, &cp_utf8, 1, "");
        zan_call2(g->builder, setcp_type, fn_set_in, &cp_utf8, 1, "");
    }

    LLVMValueRef main_argc = LLVMGetParam(main_fn, 0);
    LLVMValueRef main_argv = LLVMGetParam(main_fn, 1);
    if (g->target_is_windows) {
        LLVMTypeRef i32ptr = LLVMPointerType(i32ty, 0);
        LLVMTypeRef i8ptrptrptr = LLVMPointerType(i8ptrptr, 0);
        LLVMTypeRef utf8_argv_type = LLVMFunctionType(i32ty,
            (LLVMTypeRef[]){ i32ptr, i8ptrptrptr }, 2, 0);
        LLVMValueRef utf8_argv_fn = LLVMGetNamedFunction(g->mod, "zan_utf8_argv");
        if (!utf8_argv_fn)
            utf8_argv_fn = LLVMAddFunction(g->mod, "zan_utf8_argv", utf8_argv_type);
        LLVMValueRef argc_slot = LLVMBuildAlloca(g->builder, i32ty, "utf8_argc");
        LLVMValueRef argv_slot = LLVMBuildAlloca(g->builder, i8ptrptr, "utf8_argv");
        LLVMBuildStore(g->builder, main_argc, argc_slot);
        LLVMBuildStore(g->builder, main_argv, argv_slot);
        LLVMValueRef utf8_argv_args[] = { argc_slot, argv_slot };
        zan_call2(g->builder, utf8_argv_type, utf8_argv_fn, utf8_argv_args, 2, "");
        main_argc = LLVMBuildLoad2(g->builder, i32ty, argc_slot, "utf8_argc.value");
        main_argv = LLVMBuildLoad2(g->builder, i8ptrptr, argv_slot, "utf8_argv.value");
    }

    LLVMValueRef g_argc = LLVMGetNamedGlobal(g->mod, "__zan_argc");
    if (!g_argc) {
        g_argc = LLVMAddGlobal(g->mod, i32ty, "__zan_argc");
        LLVMSetInitializer(g_argc, LLVMConstInt(i32ty, 0, 0));
    }
    LLVMValueRef g_argv = LLVMGetNamedGlobal(g->mod, "__zan_argv");
    if (!g_argv) {
        g_argv = LLVMAddGlobal(g->mod, i8ptrptr, "__zan_argv");
        LLVMSetInitializer(g_argv, LLVMConstNull(i8ptrptr));
    }
    LLVMBuildStore(g->builder, main_argc, g_argc);
    LLVMBuildStore(g->builder, main_argv, g_argv);

    {
        LLVMTypeRef i8p = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
        LLVMTypeRef svi32 = LLVMInt32TypeInContext(g->ctx);
        LLVMTypeRef svi64 = LLVMInt64TypeInContext(g->ctx);
        LLVMValueRef stdout_ptr;
        if (g->target_is_windows) {
            LLVMTypeRef iob_type = LLVMFunctionType(i8p, (LLVMTypeRef[]){ svi32 }, 1, 0);
            LLVMValueRef iobfn = LLVMGetNamedFunction(g->mod, "__acrt_iob_func");
            if (!iobfn) iobfn = LLVMAddFunction(g->mod, "__acrt_iob_func", iob_type);
            LLVMValueRef one = LLVMConstInt(svi32, 1, 0);
            stdout_ptr = zan_call2(g->builder, iob_type, iobfn, &one, 1, "stdout");
        } else {
            const char *soname = (g->target_is_macos || g->target_is_ios) ? "__stdoutp" : "stdout";
            LLVMValueRef sg = LLVMGetNamedGlobal(g->mod, soname);
            if (!sg) sg = LLVMAddGlobal(g->mod, i8p, soname);
            stdout_ptr = LLVMBuildLoad2(g->builder, i8p, sg, "stdout");
        }
        LLVMTypeRef sv_type = LLVMFunctionType(svi32,
            (LLVMTypeRef[]){ i8p, i8p, svi32, svi64 }, 4, 0);
        LLVMValueRef sv = LLVMGetNamedFunction(g->mod, "setvbuf");
        if (!sv) sv = LLVMAddFunction(g->mod, "setvbuf", sv_type);
        int sv_mode = g->target_is_windows ? 4 /* _IONBF */ : 1 /* _IOLBF */;
        LLVMValueRef sv_args[] = { stdout_ptr, LLVMConstNull(i8p),
            LLVMConstInt(svi32, sv_mode, 0),
            LLVMConstInt(svi64, g->target_is_windows ? 0 : 4096, 0) };
        zan_call2(g->builder, sv_type, sv, sv_args, 4, "");
    }

    if (g->target_is_windows) {
        LLVMValueRef set_out_cp = LLVMGetNamedFunction(g->mod, "SetConsoleOutputCP");
        LLVMTypeRef setcp_type = LLVMFunctionType(i32ty, (LLVMTypeRef[]){ i32ty }, 1, 0);
        if (!set_out_cp)
            set_out_cp = LLVMAddFunction(g->mod, "SetConsoleOutputCP", setcp_type);
        LLVMValueRef set_in_cp = LLVMGetNamedFunction(g->mod, "SetConsoleCP");
        if (!set_in_cp)
            set_in_cp = LLVMAddFunction(g->mod, "SetConsoleCP", setcp_type);
        LLVMValueRef utf8cp = LLVMConstInt(i32ty, 65001, 0);
        zan_call2(g->builder, setcp_type, set_out_cp, &utf8cp, 1, "");
        zan_call2(g->builder, setcp_type, set_in_cp, &utf8cp, 1, "");
    }

    g->current_fn = main_fn;
    g->current_fn_ret_type = LLVMInt32TypeInContext(g->ctx);
    if (g->strict_runtime) {
        LLVMTypeRef strict_ty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx),
                                                 NULL, 0, 0);
        LLVMValueRef strict_fn = LLVMGetNamedFunction(g->mod, "zan_rt_set_strict");
        if (!strict_fn)
            strict_fn = LLVMAddFunction(g->mod, "zan_rt_set_strict", strict_ty);
        zan_call2(g->builder, strict_ty, strict_fn, NULL, 0, "");
    }
    g->throw_locals_base = 0;
    g->catch_cleanup_count = 0;
    g->throw_catch_base = 0;
    g->finally_count = 0;
    g->pending = (zan_irgen_pending_context_t){0};
    g->finally_loop_base = 0;
    g->eh_armed_count = 0;
    g->eh_armed_base = 0;
    g->eh_armed_loop_base = 0;
    g->current_type_sym = type_sym;
    g->current_this = NULL;
    g->current_fn_body = method->method_decl.body;

    if (g->check_leaks) {
        emit_leak_report_support(g);
        zan_call2(g->builder, g->atexit_type, g->fn_atexit,
                       &g->fn_report_leaks, 1, "");
    }

    if (!g->desc_hdr && g->g_site_field_graphs) {
        LLVMTypeRef i8p = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
        LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
        LLVMTypeRef reg_args[] = { LLVMPointerType(i8p, 0), LLVMPointerType(i8p, 0), i64t };
        LLVMTypeRef reg_ty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), reg_args, 3, 0);
        LLVMValueRef reg_fn = LLVMGetNamedFunction(g->mod, "zan_rt_register_site_field_graphs");
        if (!reg_fn) reg_fn = LLVMAddFunction(g->mod, "zan_rt_register_site_field_graphs", reg_ty);
        LLVMValueRef tbl = LLVMBuildLoad2(g->builder, LLVMPointerType(i8p, 0), g->g_site_field_graphs, "fgtbl");
        LLVMValueRef tntbl = g->g_site_tynames ? LLVMBuildLoad2(g->builder, LLVMPointerType(i8p, 0), g->g_site_tynames, "tntbl")
                                               : LLVMConstNull(LLVMPointerType(i8p, 0));
        LLVMValueRef sc = LLVMConstInt(i64t, (uint64_t)g->leak_site_count, 0);
        zan_call2(g->builder, reg_ty, reg_fn, (LLVMValueRef[]){ tbl, tntbl, sc }, 3, "");
    }

    /* 协程工作窃取调度器 */
    zan_call2(g->builder, g->rt_co_sched_init_type, g->rt_co_sched_init, NULL, 0, "");

    di_set_loc(g, method->loc);

    if (unit && unit->kind == AST_COMPILATION_UNIT) {
        local_scope_t *sf_locals = local_scope_new(g->arena);
        zan_symbol_t *saved_type = g->current_type_sym;
        LLVMValueRef saved_this = g->current_this;
        g->current_this = NULL;
        /* B-ID92：按依赖拓扑序逐类型「字段初始化 → _cctor」成对发射（对齐 C# 每类型序），
         * 不再"全部字段初始化完再跑全部 cctor"且不再盲从声明序。 */
        int *init_order = emit_static_init_order(unit);
        for (int oi = 0; oi < unit->comp_unit.decls.count; oi++) {
            int di = init_order[oi];
            zan_ast_node_t *d = unit->comp_unit.decls.items[di];
            if (d->kind != AST_CLASS_DECL && d->kind != AST_STRUCT_DECL) continue;
            zan_symbol_t *csym = zan_binder_lookup(g->binder, d->type_decl.name);
            if (csym) {
            g->current_type_sym = csym;
            for (int mi = 0; mi < d->type_decl.members.count; mi++) {
                zan_ast_node_t *m = d->type_decl.members.items[mi];
                if (m->kind != AST_FIELD_DECL && m->kind != AST_PROPERTY_DECL) continue;
                if (!(m->field_decl.modifiers & MOD_STATIC)) continue;
                if (!m->field_decl.initializer) continue;
                if (m->kind == AST_PROPERTY_DECL &&
                    (m->field_decl.getter_body || m->field_decl.setter_body))
                    continue;
                zan_symbol_t *fs = get_field_sym(csym, m->field_decl.name);
                zan_type_t **insts = NULL;
                int ninst = 0, inst_cap = 0;
                if (d->type_decl.type_params.count > 0) {
                    for (int gi = 0; gi < g->generic_inst_count; gi++) {
                        if (g->generic_insts[gi].type_sym != csym) continue;
                        bool seen = false;
                        for (int k = 0; k < ninst && !seen; k++)
                            seen = types_equal(insts[k], g->generic_insts[gi].inst);
                        if (seen) continue;
                        if (ninst == inst_cap) {
                            inst_cap = inst_cap ? inst_cap * 2 : 8;
                            insts = (zan_type_t **)realloc(insts,
                                (size_t)inst_cap * sizeof(*insts));
                        }
                        insts[ninst++] = g->generic_insts[gi].inst;
                    }
                } else {
                    insts = (zan_type_t **)malloc(sizeof(*insts));
                    insts[ninst++] = NULL;
                }
                for (int ii = 0; ii < ninst; ii++) {
                zan_type_t *saved_field_inst = g->cur_inst;
                g->cur_inst = insts[ii];
                LLVMValueRef gv = get_static_field_global(g, csym, fs, insts[ii]);
                if (gv) {
                zan_type_t *source_type = infer_expr_type(
                    g, m->field_decl.initializer, sf_locals);
                check_implicit_narrowing(g, fs->type, source_type,
                    m->field_decl.initializer, "field initializer");
                check_value_type_mismatch(g, fs->type, source_type,
                    m->field_decl.initializer, "field initializer");
                LLVMValueRef v = fs->type && fs->type->kind == TYPE_DELEGATE &&
                                 m->field_decl.initializer->kind == AST_LAMBDA
                    ? emit_lambda_typed(g, m->field_decl.initializer,
                                        fs->type, sf_locals)
                    : emit_expr(g, m->field_decl.initializer, sf_locals);
                if (fs->type && (is_rc_managed_type(fs->type) || fs->type->kind == TYPE_OBJECT)) {
                    emit_rc_store_field(g, fs->type, gv, v, m->field_decl.initializer, sf_locals,
                                        (fs->modifiers & MOD_WEAK) ? 1 : 0);
                } else {
                    LLVMTypeRef ft = fs->type ? map_type(g, fs->type)
                                              : LLVMInt64TypeInContext(g->ctx);
                    LLVMBuildStore(g->builder, coerce_int_to(g, v, ft), gv);
                }
                }
                g->cur_inst = saved_field_inst;
                }
                free(insts);
            }
            } /* csym */
            char cctor_name[512];
            snprintf(cctor_name, sizeof(cctor_name), "%.*s_cctor",
                     (int)d->type_decl.name.len, d->type_decl.name.str);
            LLVMValueRef cctor = LLVMGetNamedFunction(g->mod, cctor_name);
            if (cctor)
                zan_call2(g->builder, LLVMGlobalGetValueType(cctor), cctor,
                          NULL, 0, "");
        }
        free(init_order);
        g->current_type_sym = saved_type;
        g->current_this = saved_this;
    }

    if ((method->method_decl.modifiers & MOD_ASYNC) &&
        method->method_decl.params.count == 0 && type_sym) {
        char ramp_name[512];
        snprintf(ramp_name, sizeof(ramp_name), "%.*s_Main",
                 (int)type_sym->name.len, type_sym->name.str);
        LLVMValueRef ramp = LLVMGetNamedFunction(g->mod, ramp_name);
        char res_name[520];
        snprintf(res_name, sizeof(res_name), "%s$resume", ramp_name);
        LLVMValueRef resume = LLVMGetNamedFunction(g->mod, res_name);
        if (ramp && resume) {
            LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
            LLVMTypeRef mi8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMValueRef aw_tmp_mark = LLVMBuildLoad2(g->builder,
                LLVMInt32TypeInContext(g->ctx), get_eh_tmp_top_global(g),
                "main.tmpmark");
            LLVMValueRef sub = zan_call2(g->builder, LLVMGlobalGetValueType(ramp),
                ramp, NULL, 0, "main.task");
            LLVMValueRef sub_i8 = LLVMBuildBitCast(g->builder, sub, mi8ptr, "main.task8");
            LLVMValueRef sched_args[] = { sub_i8, resume };
            zan_call2(g->builder, g->rt_co_ready_type, g->rt_co_ready, sched_args, 2, "");
            zan_call2(g->builder, g->rt_co_sched_run_type, g->rt_co_sched_run, NULL, 0, "");
            emit_async_check_sub_exc(g, sub_i8, aw_tmp_mark);
            LLVMValueRef rptr = LLVMBuildStructGEP2(g->builder, g->co_header_type,
                sub_i8, ASYNC_FRAME_RESULT, "main.res.p");
            LLVMValueRef res = LLVMBuildLoad2(g->builder, i64, rptr, "main.res");
            zan_emit_frame_free(g, sub_i8);
            emit_release_static_rc_fields(g, unit);
            LLVMBuildRet(g->builder, LLVMBuildTrunc(g->builder, res,
                LLVMInt32TypeInContext(g->ctx), "main.ret"));
            emit_eh_terminate_setup(g, true);
            g->current_type_sym = NULL;
            g->current_fn_body = NULL;
            return;
        }
    }

    local_scope_t *locals = local_scope_new(g->arena);

    if (method->method_decl.params.count == 1) {
        zan_ast_node_t *param = method->method_decl.params.items[0];
        zan_type_t *pt = zan_binder_resolve_type(g->binder, param->param.type);
        if (pt && pt->kind == TYPE_ARRAY && pt->element_type &&
            pt->element_type->kind == TYPE_STRING) {
            LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
            LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
            LLVMTypeRef i8ptrptr = LLVMPointerType(i8ptr, 0);
            LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
            LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
            LLVMValueRef g_argc = LLVMGetNamedGlobal(g->mod, "__zan_argc");
            if (!g_argc) {
                g_argc = LLVMAddGlobal(g->mod, i32, "__zan_argc");
                LLVMSetInitializer(g_argc, LLVMConstInt(i32, 0, 0));
            }
            LLVMValueRef g_argv = LLVMGetNamedGlobal(g->mod, "__zan_argv");
            if (!g_argv) {
                g_argv = LLVMAddGlobal(g->mod, i8ptrptr, "__zan_argv");
                LLVMSetInitializer(g_argv, LLVMConstNull(i8ptrptr));
            }
            LLVMValueRef argc = LLVMBuildLoad2(g->builder, i32, g_argc, "ma.argc");
            LLVMValueRef argc64 = LLVMBuildSExt(g->builder, argc, i64t, "ma.argc64");
            LLVMValueRef nneg = zan_icmp(g->builder, LLVMIntSGT, argc64,
                LLVMConstInt(i64t, 0, 0), "ma.nneg");
            LLVMValueRef n = LLVMBuildSelect(g->builder, nneg,
                zan_sub(g->builder, argc64, LLVMConstInt(i64t, 1, 0), "ma.n"),
                LLVMConstInt(i64t, 0, 0), "ma.count");
            LLVMValueRef total = zan_mul(g->builder, n,
                LLVMSizeOf(i8ptrptr), "ma.total");
            LLVMValueRef arr = zan_array_alloc_typed(g, total, n, g->binder->type_string);
            LLVMValueRef argv = LLVMBuildLoad2(g->builder, i8ptrptr, g_argv, "ma.argv");
            LLVMValueRef lp = emit_entry_alloca(g, i64t, "ma.i");
            zan_store_fit(g, LLVMConstInt(i64t, 0, 0), lp);
            LLVMValueRef ffn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder));
            LLVMBasicBlockRef cond_bb = LLVMAppendBasicBlockInContext(g->ctx, ffn, "ma.cond");
            LLVMBasicBlockRef body_bb = LLVMAppendBasicBlockInContext(g->ctx, ffn, "ma.fill");
            LLVMBasicBlockRef done_bb = LLVMAppendBasicBlockInContext(g->ctx, ffn, "ma.filled");
            LLVMBuildBr(g->builder, cond_bb);
            LLVMPositionBuilderAtEnd(g->builder, cond_bb);
            LLVMValueRef iv = LLVMBuildLoad2(g->builder, i64t, lp, "ma.iv");
            LLVMBuildCondBr(g->builder, zan_icmp(g->builder, LLVMIntSLT, iv, n, "ma.more"),
                body_bb, done_bb);
            LLVMPositionBuilderAtEnd(g->builder, body_bb);
            LLVMValueRef iv1 = zan_add(g->builder, iv, LLVMConstInt(i64t, 1, 0), "ma.i1");
            LLVMValueRef slot = LLVMBuildGEP2(g->builder, i8ptr, argv, &iv1, 1, "ma.slot");
            LLVMValueRef cstr = LLVMBuildLoad2(g->builder, i8ptr, slot, "ma.cstr");
            LLVMValueRef len = zan_call2(g->builder,
                LLVMFunctionType(i64t, (LLVMTypeRef[]){ i8ptr }, 1, 0),
                g->fn_strlen, &cstr, 1, "ma.len");
            LLVMValueRef cap = zan_add(g->builder, len, LLVMConstInt(i64t, 1, 0), "ma.cap");
            LLVMValueRef buf = emit_string_alloc_rc(g, cap);
            LLVMTypeRef memcpy_ty = LLVMFunctionType(i8ptr,
                (LLVMTypeRef[]){ i8ptr, i8ptr, i64t }, 3, 0);
            LLVMValueRef mc = get_libc_fn(g, "memcpy", memcpy_ty);
            zan_call2(g->builder, memcpy_ty, mc,
                (LLVMValueRef[]){ buf, cstr, len }, 3, "");
            LLVMValueRef endp = LLVMBuildGEP2(g->builder, i8, buf, &len, 1, "ma.ep");
            zan_store_fit(g, LLVMConstInt(i8, 0, 0), endp);
            LLVMTypeRef i64p = LLVMPointerType(i64t, 0);
            LLVMValueRef fits = zan_icmp(g->builder, LLVMIntULE, len,
                LLVMConstInt(i64t, ZAN_STR_LEN_MASK, 0), "ma.fits");
            LLVMValueRef half = LLVMBuildSelect(g->builder, fits, len,
                LLVMConstInt(i64t, ZAN_STR_LEN_UNKNOWN, 0), "ma.half");
            LLVMValueRef word = LLVMBuildOr(g->builder,
                LLVMConstInt(i64t, ZAN_STRING_TAG << 32, 0), half, "ma.hdr");
            LLVMValueRef hdr_ptr = LLVMBuildGEP2(g->builder, i8, buf,
                &(LLVMValueRef){ LLVMConstInt(i64t, (uint64_t)ZAN_OBJ_SITE_OFF, 1) }, 1,
                "ma.hdrp");
            LLVMBuildStore(g->builder, word,
                LLVMBuildBitCast(g->builder, hdr_ptr, i64p, "ma.hdrip"));
            LLVMValueRef elem = LLVMBuildGEP2(g->builder, i8ptr, arr, &iv, 1, "ma.ep.slot");
            zan_store_fit(g, buf, elem);
            zan_store_fit(g, iv1, lp);
            LLVMBuildBr(g->builder, cond_bb);
            LLVMPositionBuilderAtEnd(g->builder, done_bb);
            zan_type_t *args_type = zan_binder_make_array_type(g->binder,
                g->binder->type_string);
            LLVMValueRef slot_a = emit_entry_alloca(g, i8ptrptr, "ma.slot");
            zan_store_fit(g, arr, slot_a);
            local_add(locals, param->param.name, slot_a, args_type);
            box_captured_parameter(g, locals, param, args_type, i8ptrptr, arr,
                                   method->method_decl.body);
            if (locals->vars[locals->count - 1].box_cell) {
                emit_rc_release_for_type(g, args_type, arr);
            } else {
                arc_own_local(g, locals);
            }
        }
    }

    g->current_fn_is_main = true;
    if (method->method_decl.body) {
        emit_stmt(g, method->method_decl.body, locals);
    }
    g->current_fn_is_main = false;

    if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder))) {
        emit_release_owned_locals(g, locals);
        emit_release_static_rc_fields(g, unit);
        LLVMBuildRet(g->builder, LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0));
    }
    emit_eh_terminate_setup(g, true);
    g->current_type_sym = NULL;
    g->current_fn_body = NULL;
}


