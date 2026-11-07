/* 统一三元表达式分支值类型至 PHI 节点类型，适配整型位宽与指针类型 */
static LLVMValueRef coerce_ternary_value(zan_irgen_t *g, LLVMValueRef v,
                                         LLVMTypeRef target) {
    if (!v || !target) return v;
    int vk = LLVMGetTypeKind(LLVMTypeOf(v));
    int tk = LLVMGetTypeKind(target);
    if (vk == LLVMIntegerTypeKind &&
        (tk == LLVMFloatTypeKind || tk == LLVMDoubleTypeKind))
        return LLVMBuildSIToFP(g->builder, v, target, "tern.sitofp");
    if ((vk == LLVMFloatTypeKind || vk == LLVMDoubleTypeKind) &&
        tk == LLVMIntegerTypeKind)
        return emit_fptosi_guarded(g, v, target, (zan_loc_t){0}, NULL);
    return coerce_int_to(g, v, target);
}
LLVMValueRef emit_expr_conditional(zan_irgen_t *g, zan_ast_node_t *expr,
        local_scope_t *locals) {
        LLVMValueRef cond = emit_expr(g, expr->conditional.cond, locals);
        /* normalize to i1 */
        cond = zan_tobool(g->builder, cond, "cond");
        LLVMBasicBlockRef then_bb = LLVMAppendBasicBlockInContext(g->ctx,
            LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder)), "tern.then");
        LLVMBasicBlockRef else_bb = LLVMAppendBasicBlockInContext(g->ctx,
            LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder)), "tern.else");
        LLVMBasicBlockRef merge_bb = LLVMAppendBasicBlockInContext(g->ctx,
            LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder)), "tern.merge");
        LLVMBuildCondBr(g->builder, cond, then_bb, else_bb);

        LLVMPositionBuilderAtEnd(g->builder, then_bb);
        LLVMValueRef then_val = emit_expr(g, expr->conditional.then_expr, locals);
        LLVMBasicBlockRef then_end = LLVMGetInsertBlock(g->builder);

        LLVMPositionBuilderAtEnd(g->builder, else_bb);
        LLVMValueRef else_val = emit_expr(g, expr->conditional.else_expr, locals);
        LLVMBasicBlockRef else_end = LLVMGetInsertBlock(g->builder);

        /* A482：两臂都是 void 调用（语句位三元 `flag ? A() : B();`，最常见
         * 的三元写法之一）时不建 phi——phi void 是非法 IR（宿主与自举同病，
         * "Instruction has a name, but provides a void value!"），直接分支
         * 汇合返回哑值。 */
        if (LLVMGetTypeKind(LLVMTypeOf(then_val)) == LLVMVoidTypeKind &&
            LLVMGetTypeKind(LLVMTypeOf(else_val)) == LLVMVoidTypeKind) {
            LLVMPositionBuilderAtEnd(g->builder, then_end);
            LLVMBuildBr(g->builder, merge_bb);
            LLVMPositionBuilderAtEnd(g->builder, else_end);
            LLVMBuildBr(g->builder, merge_bb);
            LLVMPositionBuilderAtEnd(g->builder, merge_bb);
            return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
        }

        /* 分支字面量位宽对齐转换 */
        LLVMTypeRef tty = LLVMTypeOf(then_val);
        LLVMTypeRef ety = LLVMTypeOf(else_val);
        LLVMTypeRef phi_ty = tty;
        zan_type_t *conditional_type = infer_expr_type(g, expr, locals);
        if (conditional_type && is_rc_managed_type(conditional_type)) {
            int then_owned = expr_yields_owned_rc_value(
                g, expr->conditional.then_expr, locals);
            int else_owned = expr_yields_owned_rc_value(
                g, expr->conditional.else_expr, locals);
            /* 产生持有引用的条件表达式需规范化各分支所有权状态。
             * B-ID111：委托 retain 不是直线调用——值可能是裸函数指针或堆闭包，
             * irgen_arc 以运行期 clo.is 分流建 clo.rt/clo.rt.end 两块并把 builder
             * 留在后者；此处不重取臂末块，下方 Br(merge) 便落回旧块成双终结符、
             * clo.rt.end 悬空无终结符（`c ? (Op)(lambda) : null` 必炸 E3090）。 */
            if (then_owned != else_owned) {
                if (!then_owned) {
                    LLVMPositionBuilderAtEnd(g->builder, then_end);
                    emit_rc_retain_for_type(g, conditional_type, then_val);
                    then_end = LLVMGetInsertBlock(g->builder);
                } else {
                    LLVMPositionBuilderAtEnd(g->builder, else_end);
                    emit_rc_retain_for_type(g, conditional_type, else_val);
                    else_end = LLVMGetInsertBlock(g->builder);
                }
            }
        }
        if (tty != ety) {
            zan_type_t *st = conditional_type;
            if (st) {
                LLVMTypeRef want = map_type(g, st);
                if (want) phi_ty = want;
            }
            if (phi_ty == tty) {
                int tk = LLVMGetTypeKind(tty), ek = LLVMGetTypeKind(ety);
                if (tk == LLVMIntegerTypeKind && ek == LLVMIntegerTypeKind) {
                    if (LLVMGetIntTypeWidth(ety) > LLVMGetIntTypeWidth(tty))
                        phi_ty = ety;
                } else if ((tk == LLVMFloatTypeKind || tk == LLVMDoubleTypeKind) &&
                           (ek == LLVMFloatTypeKind || ek == LLVMDoubleTypeKind)) {
                    if ((ek == LLVMDoubleTypeKind) &&
                        !(tk == LLVMDoubleTypeKind)) phi_ty = ety;
                } else if (ek == LLVMFloatTypeKind || ek == LLVMDoubleTypeKind) {
                    phi_ty = ety;
                }
            }
            LLVMPositionBuilderAtEnd(g->builder, then_end);
            then_val = coerce_ternary_value(g, then_val, phi_ty);
            then_end = LLVMGetInsertBlock(g->builder);
            LLVMPositionBuilderAtEnd(g->builder, else_end);
            else_val = coerce_ternary_value(g, else_val, phi_ty);
            else_end = LLVMGetInsertBlock(g->builder);
        }

        LLVMPositionBuilderAtEnd(g->builder, then_end);
        LLVMBuildBr(g->builder, merge_bb);
        LLVMPositionBuilderAtEnd(g->builder, else_end);
        LLVMBuildBr(g->builder, merge_bb);

        LLVMPositionBuilderAtEnd(g->builder, merge_bb);
        LLVMValueRef phi = LLVMBuildPhi(g->builder, phi_ty, "tern");
        LLVMValueRef incoming_vals[] = { then_val, else_val };
        LLVMBasicBlockRef incoming_bbs[] = { then_end, else_end };
        LLVMAddIncoming(phi, incoming_vals, incoming_bbs, 2);
        return phi;
    return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
}

/* switch 表达式 (expr switch { pattern => result, ... }) 模式匹配降解 */
LLVMValueRef emit_expr_switch_expr(zan_irgen_t *g, zan_ast_node_t *expr,
                                          local_scope_t *locals) {
    zan_type_t *rty = infer_expr_type(g, expr, locals);
    if (!rty || rty->kind == TYPE_ERROR || rty->kind == TYPE_VOID)
        rty = g->binder->type_int;
    LLVMTypeRef rtll = map_type(g, rty);
    LLVMValueRef rslot = emit_entry_alloca(g, rtll, "swx.result");
    zan_store_fit(g, LLVMConstNull(rtll), rslot);

    static const char kSwx[] = { 1, 's', 'w', 'x' };
    zan_istr_t hname = { (char *)kSwx, 4 };
    int mark = locals->count;
    local_add(locals, hname, rslot, rty);

    zan_ast_node_t *syn = zan_ast_new(g->arena, AST_SWITCH_STMT, expr->loc);
    syn->switch_stmt.expr = expr->switch_expr.expr;
    zan_ast_list_init(&syn->switch_stmt.cases);

    int narms = expr->switch_expr.arms.count;
    bool has_default = false;
    for (int i = 0; i < narms; i++) {
        zan_ast_node_t *arm = expr->switch_expr.arms.items[i];
        if (arm->switch_arm.is_default) has_default = true;

        zan_ast_node_t *sc = zan_ast_new(g->arena, AST_SWITCH_CASE, arm->loc);
        sc->switch_case.pattern = arm->switch_arm.pattern;
        sc->switch_case.type_pattern = arm->switch_arm.type_pattern;
        sc->switch_case.when_cond = arm->switch_arm.when_cond;
        sc->switch_case.var_name = arm->switch_arm.var_name;

        /* body: { __swx = result; break; } */
        zan_ast_node_t *block = zan_ast_new(g->arena, AST_BLOCK, arm->loc);
        zan_ast_list_init(&block->block.stmts);
        zan_ast_node_t *assign = zan_ast_new(g->arena, AST_ASSIGNMENT, arm->loc);
        zan_ast_node_t *lhs = zan_ast_new(g->arena, AST_IDENTIFIER, arm->loc);
        lhs->ident.name = hname;
        assign->binary.op = TK_EQ;
        assign->binary.left = lhs;
        assign->binary.right = arm->switch_arm.result;
        zan_ast_node_t *es = zan_ast_new(g->arena, AST_EXPR_STMT, arm->loc);
        es->expr_stmt.expr = assign;
        zan_ast_list_push(&block->block.stmts, es, g->arena);
        zan_ast_node_t *brk = zan_ast_new(g->arena, AST_BREAK_STMT, arm->loc);
        zan_ast_list_push(&block->block.stmts, brk, g->arena);
        sc->switch_case.body = block;
        zan_ast_list_push(&syn->switch_stmt.cases, sc, g->arena);
    }
    if (!has_default) {
        zan_ast_node_t *sc = zan_ast_new(g->arena, AST_SWITCH_CASE, expr->loc);
        zan_ast_node_t *block = zan_ast_new(g->arena, AST_BLOCK, expr->loc);
        zan_ast_list_init(&block->block.stmts);
        zan_ast_node_t *brk = zan_ast_new(g->arena, AST_BREAK_STMT, expr->loc);
        zan_ast_list_push(&block->block.stmts, brk, g->arena);
        sc->switch_case.body = block;
        zan_ast_list_push(&syn->switch_stmt.cases, sc, g->arena);
    }

    emit_stmt(g, syn, locals);
    locals->count = mark;
    return LLVMBuildLoad2(g->builder, rtll, rslot, "swx.load");
}

/* record with 表达式：非破坏性浅拷贝并覆盖指定字段 */
LLVMValueRef emit_expr_with_expr(zan_irgen_t *g, zan_ast_node_t *expr,
                                        local_scope_t *locals) {
    zan_istr_t cm = { (char *)"__CloneWith", 11 };
    zan_type_t *rty = concretize(g, infer_expr_type(g, expr->with_expr.expr,
                                                    locals));
    if (!rty || !rty->sym ||
        !get_method_sym(rty->sym, cm)) {
        zan_diag_emit_code(g->diag, DIAG_ERROR, expr->loc, 3025,
            "'with' requires a record receiver with a synthesized clone");
        return LLVMConstNull(LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0));
    }
    static const char kWr[] = { 1, 'w', 'r' };
    zan_istr_t hname = { (char *)kWr, 3 };
    LLVMTypeRef rtll = map_type(g, rty);
    LLVMValueRef recv = emit_expr(g, expr->with_expr.expr, locals);
    LLVMValueRef rslot = emit_entry_alloca(g, rtll, "with.recv");
    zan_store_fit(g, recv, rslot);
    int mark = locals->count;
    local_add(locals, hname, rslot, rty);

    zan_ast_node_t *recv_id = zan_ast_new(g->arena, AST_IDENTIFIER, expr->loc);
    recv_id->ident.name = hname;
    zan_ast_node_t *callee = zan_ast_new(g->arena, AST_MEMBER_ACCESS, expr->loc);
    callee->member.object = recv_id;
    callee->member.name = cm;
    zan_ast_node_t *call = zan_ast_new(g->arena, AST_CALL, expr->loc);
    call->call.callee = callee;
    zan_ast_list_init(&call->call.args);
    zan_ast_list_init(&call->call.type_args);
    for (int fi = 0; fi < rty->sym->member_count; fi++) {
        zan_symbol_t *m = rty->sym->members[fi];
        if (!m || m->kind != SYM_FIELD) continue;
        zan_ast_node_t *arg = NULL;
        for (int ai = 0; ai < expr->with_expr.assigns.count; ai++) {
            zan_ast_node_t *asg = expr->with_expr.assigns.items[ai];
            if (asg && asg->kind == AST_ASSIGNMENT &&
                asg->binary.left->ident.name.len == m->name.len &&
                memcmp(asg->binary.left->ident.name.str, m->name.str,
                       (size_t)m->name.len) == 0) {
                arg = asg->binary.right;
                break;
            }
        }
        if (!arg) {
            arg = zan_ast_new(g->arena, AST_MEMBER_ACCESS, expr->loc);
            zan_ast_node_t *obj =
                zan_ast_new(g->arena, AST_IDENTIFIER, expr->loc);
            obj->ident.name = hname;
            arg->member.object = obj;
            arg->member.name = m->name;
        }
        zan_ast_list_push(&call->call.args, arg, g->arena);
    }

    LLVMValueRef result = emit_expr(g, call, locals);
    /* 接收者所有权转移与生命周期管理 */
    LLVMValueRef recv_now = LLVMBuildLoad2(g->builder, rtll, rslot,
                                           "with.recv.load");
    emit_release_owned_call_temp(g, expr->with_expr.expr, recv_now, locals);
    locals->count = mark;
    return result;
}

/* 协程帧结果槽为 64 位宽：await 统一加载 64 位槽值并进行反向类型转换 */
static LLVMValueRef coerce_await_result(zan_irgen_t *g, zan_ast_node_t *expr,
        LLVMValueRef res, local_scope_t *locals) {
    zan_type_t *rt = concretize(g, infer_expr_type(g, expr->await_expr.expr, locals));
    if (rt && rt->type_arg_count > 0) {
        if (rt->kind == TYPE_TASK ||
            (rt->name.str && ((rt->name.len >= 4 && memcmp(rt->name.str, "Task", 4) == 0) ||
                             (rt->name.len >= 9 && memcmp(rt->name.str, "ValueTask", 9) == 0))) ||
            (rt->sym &&
             ((rt->sym->name.len == 4 && memcmp(rt->sym->name.str, "Task", 4) == 0) ||
              (rt->sym->name.len == 9 && memcmp(rt->sym->name.str, "ValueTask", 9) == 0)))) {
            rt = concretize(g, rt->type_args[0]);
        }
    }
    return coerce_from_frame_result(g, res, rt);
}

static zan_symbol_t *direct_extern_method(zan_irgen_t *g, zan_ast_node_t *expr) {
    if (!expr || expr->kind != AST_CALL || !expr->call.callee) return NULL;
    zan_symbol_t *sym = NULL;
    if (expr->call.callee->kind == AST_IDENTIFIER) {
        if (g->current_type_sym)
            sym = get_method_sym(g->current_type_sym,
                                 expr->call.callee->ident.name);
        if (!sym)
            sym = zan_binder_lookup(g->binder, expr->call.callee->ident.name);
    } else if (expr->call.callee->kind == AST_MEMBER_ACCESS &&
               expr->call.callee->member.object->kind == AST_IDENTIFIER) {
        zan_symbol_t *cls = zan_binder_lookup(g->binder,
            expr->call.callee->member.object->ident.name);
        if (cls) sym = get_method_sym(cls, expr->call.callee->member.name);
    }
    if (!sym || !sym->decl || sym->decl->kind != AST_METHOD_DECL) return NULL;
    if (!zan_ast_method_extern_lib(sym->decl).str &&
        !(sym->decl->method_decl.modifiers & MOD_EXTERN))
        return NULL;
    return sym;
}

static bool blocking_integer_type(zan_type_t *t) {
    if (!t) return false;
    switch (t->kind) {
    case TYPE_BOOL:
    case TYPE_BYTE:
    case TYPE_SHORT:
    case TYPE_INT:
    case TYPE_LONG:
    case TYPE_SBYTE:
    case TYPE_USHORT:
    case TYPE_UINT:
    case TYPE_ULONG:
    case TYPE_CHAR:
    case TYPE_NINT:
    case TYPE_ENUM:
        return true;
    default:
        return false;
    }
}

static LLVMValueRef blocking_arg_i64(zan_irgen_t *g, LLVMValueRef v,
                                     zan_type_t *type) {
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    if (LLVMGetTypeKind(LLVMTypeOf(v)) != LLVMIntegerTypeKind)
        return LLVMBuildPtrToInt(g->builder, v, i64, "blocking.arg");
    if (LLVMGetIntTypeWidth(LLVMTypeOf(v)) == 64) return v;
    if (type && (type->kind == TYPE_BOOL || type->kind == TYPE_BYTE ||
                 type->kind == TYPE_USHORT || type->kind == TYPE_UINT ||
                 type->kind == TYPE_ULONG || type->kind == TYPE_CHAR))
        return LLVMBuildZExt(g->builder, v, i64, "blocking.arg");
    return LLVMBuildSExt(g->builder, v, i64, "blocking.arg");
}

/* 原生异步挂起原语发射 */
static LLVMValueRef emit_await_blocking_extern(zan_irgen_t *g,
        zan_ast_node_t *expr, local_scope_t *locals, zan_symbol_t *sym) {
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    zan_ast_node_t *call = expr->await_expr.expr;
    zan_ast_node_t *decl = sym->decl;
    int argc = call->call.args.count;
    zan_type_t *ret = decl->method_decl.return_type
        ? resolve_type_ctx(g, decl->method_decl.return_type)
        : g->binder->type_void;

    if (strstr(g->target_triple, "wasm") != NULL) {
        zan_diag_emit_code(g->diag, DIAG_ERROR, expr->loc, 3026,
            "await native extern calls are not supported on wasm32: "
            "the blocking runtime requires native worker threads");
        return LLVMConstInt(i64, 0, 0);
    }

    if (!g->current_async_frame || !g->current_async_switch) {
        zan_diag_emit_code(g->diag, DIAG_ERROR, expr->loc, 3027,
            "await native extern calls are only supported inside an async "
            "method");
        return LLVMConstInt(i64, 0, 0);
    }
    if (argc > 4) {
        zan_diag_emit_code(g->diag, DIAG_ERROR, expr->loc, 3028,
            "await native extern call has %d arguments; at most four scalar "
            "arguments are supported (prepare data before suspension and "
            "await a handle-only operation)", argc);
        return LLVMConstInt(i64, 0, 0);
    }
    if (ret->kind != TYPE_VOID && ret->kind != TYPE_INT &&
        ret->kind != TYPE_LONG && ret->kind != TYPE_NINT) {
        zan_diag_emit_code(g->diag, DIAG_ERROR, expr->loc, 3029,
            "await native extern call returns '%s', but only int, long, nint, "
            "or void can cross a suspension safely; return a scalar handle "
            "instead", ret->name.str ? ret->name.str : "unsupported");
        return LLVMConstInt(i64, 0, 0);
    }

    for (int k = 0; k < argc; k++) {
        zan_ast_node_t *p = decl->method_decl.params.items[k];
        zan_type_t *pt = (p && p->kind == AST_PARAM)
            ? resolve_type_ctx(g, p->param.type) : NULL;
        if (!blocking_integer_type(pt)) {
            zan_diag_emit_code(g->diag, DIAG_ERROR, call->call.args.items[k]->loc, 3030,
                "await native extern argument %d has type '%s'; only integer, "
                "nint, bool, and enum scalars are safe across suspension "
                "(prepare/bind managed data first, then await a handle-only "
                "operation)", k + 1, pt && pt->name.str ? pt->name.str : "unsupported");
            return LLVMConstInt(i64, 0, 0);
        }
    }

    int fi = irgen_find_function(g, sym);
    LLVMValueRef native = fi >= 0 ? g->functions[fi].fn : NULL;
    if (!native) {
        zan_diag_emit_code(g->diag, DIAG_ERROR, expr->loc, 3031,
            "cannot lower await native extern call: external function is not "
            "available in the generated module");
        return LLVMConstInt(i64, 0, 0);
    }

    LLVMValueRef args[4] = {
        LLVMConstInt(i64, 0, 0), LLVMConstInt(i64, 0, 0),
        LLVMConstInt(i64, 0, 0), LLVMConstInt(i64, 0, 0)
    };
    for (int k = 0; k < argc; k++) {
        zan_ast_node_t *p = decl->method_decl.params.items[k];
        zan_type_t *pt = resolve_type_ctx(g, p->param.type);
        args[k] = blocking_arg_i64(g,
            emit_arg_typed(g, call->call.args.items[k], pt, locals), pt);
    }

    g->uses_socket_async = true;
    int state = g->current_async_next_state++;
    LLVMValueRef frame = g->current_async_frame;
    LLVMTypeRef frame_type = g->current_async_frame_type;
    LLVMValueRef frame_i8 = LLVMBuildBitCast(g->builder, frame, i8ptr,
                                             "blocking.frame");
    LLVMValueRef out = NULL;
    if (ret->kind != TYPE_VOID) {
        out = get_async_result_ptr(g);
    } else {
        out = LLVMConstNull(LLVMPointerType(i64, 0));
    }
    LLVMValueRef fnptr = LLVMBuildBitCast(g->builder, native, i8ptr,
                                          "blocking.fn");
    zan_store_fit(g, LLVMConstInt(i32, (unsigned)state, 0), get_async_state_ptr(g));
    LLVMValueRef rt_args[] = {
        fnptr, LLVMConstInt(i32, (unsigned)argc, 0),
        args[0], args[1], args[2], args[3],
        frame_i8, g->current_async_resume_fn, out
    };
    zan_call2(g->builder, g->rt_blocking_co_type, g->rt_blocking_co,
              rt_args, 9, "");
    LLVMBuildBr(g->builder, get_async_suspend_ret_bb(g));

    LLVMBasicBlockRef resume = LLVMAppendBasicBlockInContext(g->ctx,
        g->current_async_resume_fn, "co.blocking.resume");
    LLVMAddCase(g->current_async_switch,
        LLVMConstInt(i32, (unsigned)state, 0), resume);
    LLVMPositionBuilderAtEnd(g->builder, resume);
    if (ret->kind == TYPE_VOID) return LLVMConstInt(i64, 0, 0);
    LLVMValueRef raw = LLVMBuildLoad2(g->builder, i64, get_async_result_ptr(g), "blocking.raw");
    return coerce_await_result(g, expr, raw, locals);
}

/* await 的目标是不是**异步**的。
 *
 * await 的目标分两类：
 *  - **异步目标**——它的值是"子协程帧"，结果要从帧里抽出来；
 *  - **普通目标**——它的值就是"普通值"，await 只是把它原样带过去。
 *
 * 所以要判断"这调用是不是 async"。判定有几种：
 *  - **Task / ValueTask 类型**——能直接接住；
 *  - **async 方法**——调用目标原型是"双反"；
 *  - **async 委托**——调用的对象（或被调者本身）是 async 委托。
 *
 * 判错的后果是读野指针：普通值往往也是 i8 指针（字符串就是），与子
 * 协程帧的指针同型，被当成帧去读就会读出面元指针——静默的垃圾值，
 * 不报错。
 */
static bool await_target_is_async(zan_irgen_t *g, zan_ast_node_t *expr,
                                  local_scope_t *locals) {
    if (!expr) return false;
    zan_type_t *ty = concretize(g, infer_expr_type(g, expr, locals));
    if (ty) {
        if (ty->kind == TYPE_TASK) return true;
        if (ty->name.str && ty->name.len >= 4 &&
            memcmp(ty->name.str, "Task", 4) == 0) return true;
        if (ty->name.str && ty->name.len >= 9 &&
            memcmp(ty->name.str, "ValueTask", 9) == 0) return true;
    }
    if (expr->kind != AST_CALL || !expr->call.callee) return false;
    if (expr->call.resolved_sym && expr->call.resolved_sym->decl &&
        expr->call.resolved_sym->decl->kind == AST_METHOD_DECL) {
        if ((expr->call.resolved_sym->decl->method_decl.modifiers & MOD_ASYNC) != 0)
            return true;
    }
    zan_ast_node_t *callee = expr->call.callee;
    /* 被调者本身就是委托（局部变量、字段、下标元素都算）——
     * `await shutdownHooks[i]()`。委托类型自带 async 标记。 */
    if (callee->kind != AST_MEMBER_ACCESS) {
        zan_type_t *callee_ty = infer_expr_type(g, callee, locals);
        if (callee_ty && callee_ty->kind == TYPE_DELEGATE)
            return callee_ty->delegate_is_async;
    }
    if (callee->kind == AST_MEMBER_ACCESS) {
        zan_type_t *obj_ty = infer_expr_type(g, callee->member.object, locals);
        /* async 委托：调用的对象是 async 委托。 */
        if (obj_ty && obj_ty->kind == TYPE_DELEGATE && obj_ty->delegate_is_async)
            return true;
        /* 字段本身是 async 委托：`await this.recv(buf, n)`。 */
        zan_type_t *field_ty = member_access_field_type(g, locals, callee);
        if (field_ty && field_ty->kind == TYPE_DELEGATE)
            return field_ty->delegate_is_async;
        if (obj_ty) {
            zan_symbol_t *ts = obj_ty->sym ? obj_ty->sym
                : expr_class_sym(g, callee->member.object, locals);
            if (ts) {
                /* 接口方法：签名在接口上声明，async 标记也在那儿。 */
                if (ts->kind == SYM_INTERFACE) {
                    zan_symbol_t *im = resolve_iface_overload(ts,
                        callee->member.name, expr->call.args.count,
                        expr->call.type_args.count);
                    if (im && im->decl && im->decl->kind == AST_METHOD_DECL)
                        return (im->decl->method_decl.modifiers & MOD_ASYNC) != 0;
                }
                zan_symbol_t *msym = resolve_overload_typed(g, ts,
                    callee->member.name, expr, locals);
                if (msym && msym->decl && msym->decl->kind == AST_METHOD_DECL)
                    return (msym->decl->method_decl.modifiers & MOD_ASYNC) != 0;
            }
        } else {
            /* 静态接收者写的是**类名**（`Socket.SendStringAsync(...)`）——
             * 类名不是值，infer 给不出类型，要按名字查符号。 */
            zan_symbol_t *cs = callee->member.object->kind == AST_IDENTIFIER
                ? zan_binder_lookup(g->binder, callee->member.object->ident.name)
                : NULL;
            if (cs && (cs->kind == SYM_CLASS || cs->kind == SYM_STRUCT)) {
                zan_symbol_t *msym = resolve_overload_typed(g, cs,
                    callee->member.name, expr, locals);
                if (msym && msym->decl && msym->decl->kind == AST_METHOD_DECL)
                    return (msym->decl->method_decl.modifiers & MOD_ASYNC) != 0;
            }
        }
    } else if (callee->kind == AST_IDENTIFIER) {
        /* 局部变量装的委托：`await driver(this, method, path, body)`。 */
        local_var_t *lv = local_find(locals, callee->ident.name);
        if (lv && lv->type && lv->type->kind == TYPE_DELEGATE)
            return lv->type->delegate_is_async;
        if (g->current_type_sym) {
            zan_symbol_t *msym = resolve_overload_typed(g, g->current_type_sym,
                callee->ident.name, expr, locals);
            if (msym && msym->decl && msym->decl->kind == AST_METHOD_DECL)
                return (msym->decl->method_decl.modifiers & MOD_ASYNC) != 0;
        }
    }
    return false;
}
LLVMValueRef emit_expr_await_expr(zan_irgen_t *g, zan_ast_node_t *expr,
        local_scope_t *locals) {
        /* await 落在 async 方法的 lambda 体内：lambda 不是协程，没有自己的帧可挂
         * ——硬降级会把外层协程的块当自己的用（跨函数的 br，LLVM 校验直接失败：
         * "Referring to a basic block in another function"）。这里报诊断，不落那条
         * 非法 IR。同步方法里的 lambda 没有外层协程，根驱动等得到，不算错。 */
        if (g->lambda_in_async_method) {
            zan_diag_emit_code(g->diag, DIAG_ERROR, expr->loc, 3032,
                          "'await' inside a lambda is not supported: "
                          "a lambda is not a coroutine");
            return LLVMConstInt(LLVMInt64TypeInContext(g->ctx), 0, 0);
        }

        /* await Task.Yield(): 协作式让出控制权并将当前协程帧重新入队 */
        if (is_call_to(expr->await_expr.expr, "Task", "Yield") &&
            expr->await_expr.expr->call.args.count == 0) {
            if (g->current_async_frame && g->current_async_switch) {
                int k = g->current_async_next_state++;
                LLVMTypeRef di32 = LLVMInt32TypeInContext(g->ctx);
                LLVMTypeRef di64 = LLVMInt64TypeInContext(g->ctx);
                zan_store_fit(g, LLVMConstInt(di32, (unsigned)k, 0), get_async_state_ptr(g));
                LLVMBuildBr(g->builder, get_async_requeue_bb(g));

                LLVMBasicBlockRef rk = LLVMAppendBasicBlockInContext(g->ctx,
                    g->current_async_resume_fn, "co.resume");
                LLVMAddCase(g->current_async_switch, LLVMConstInt(di32, (unsigned)k, 0), rk);
                LLVMPositionBuilderAtEnd(g->builder, rk);
                di_set_loc(g, expr->loc);
                return LLVMConstInt(di64, 0, 0);
            }
            return LLVMConstInt(LLVMInt64TypeInContext(g->ctx), 0, 0);
        }

        /* await Task.Delay(ms): 基于定时器的挂起（复用当前帧，不分配子帧） */
        if (is_call_to(expr->await_expr.expr, "Task", "Delay") &&
            expr->await_expr.expr->call.args.count == 1) {
            LLVMTypeRef di64 = LLVMInt64TypeInContext(g->ctx);
            LLVMTypeRef di32 = LLVMInt32TypeInContext(g->ctx);
            LLVMTypeRef di8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMValueRef ms = emit_expr(g, expr->await_expr.expr->call.args.items[0], locals);
            if (LLVMTypeOf(ms) != di64) {
                ms = LLVMBuildIntCast2(g->builder, ms, di64, 1, "ms64");
            }
            if (g->current_async_frame && g->current_async_switch) {
                int k = g->current_async_next_state++;
                LLVMValueRef self_i8 = get_async_self_i8(g);
                zan_store_fit(g, LLVMConstInt(di32, (unsigned)k, 0), get_async_state_ptr(g));
                zan_call2(g->builder, g->rt_co_delay_type, g->rt_co_delay,
                    (LLVMValueRef[]){ ms, self_i8, g->current_async_resume_fn }, 3, "");
                LLVMBuildBr(g->builder, get_async_suspend_ret_bb(g));

                LLVMBasicBlockRef rk = LLVMAppendBasicBlockInContext(g->ctx,
                    g->current_async_resume_fn, "co.resume");
                LLVMAddCase(g->current_async_switch, LLVMConstInt(di32, (unsigned)k, 0), rk);
                LLVMPositionBuilderAtEnd(g->builder, rk);
                di_set_loc(g, expr->loc);
                return LLVMConstInt(di64, 0, 0);
            }
            /* 顶层非异步上下文：同步休眠等待。
             *
             * 这里原来只**查**模块里的 "Sleep"（Windows）/ "poll"
             * （POSIX）：LLVMGetNamedFunction 找不到就返回 NULL，而这两个
             * 符号本来就不在当前模块里（Sleep 是 Win32 API、poll 是 libc
             * 符号，链接期才进来）——查不到就**整段等待被静默跳过**：
             * `await Task.Delay(500)` 编译成一条 ret，方法立刻返回，
             * 调用方以为等了 500ms，其实等了 0ms。
             *
             * 改法与 irgen.c 里 zan_io_pump_timeout 一致：**声明**符号
             * （LLVMAddFunction）而不是只查——声明进模块后链接期由系统库
             * 解析，符号一定找得到。
             *
             * 代价：同步休眠**占住调用线程**。这段代码在 async 协程内部被
             * 调用时，该 worker 线程上的其它协程要等休眠结束才能派发。
             *
             * 修法（A470）：不要用裸的 Sleep / poll——那会把整段时间
             * 变成"谁都不派发"。改成调运行时的 zan_co_sync_sleep：它把
             * 一次长睡拆成**按下一个定时器截止时间切片**的等待，每片醒来
             * 派发一次到期定时器并把被唤醒的协程跑起来。于是别的协程的
             * 定时器照样按时到期，而这次等待本身也照样等满。
             *
             * 实测（单 worker）：一个协程在 async 内部同步睡 500ms，另一
             * 个协程 200ms 的定时器从 503ms（改前）回到 204ms（改后）。
             * 注意必须**在 async 协程内部**才复现——放在 Main 里占的是
             * 主线程（不是 worker），worker 池照常派发，看不出差别。
             */
            LLVMValueRef ms32 = LLVMBuildTrunc(g->builder, ms, di32, "ms32");
            {
                LLVMTypeRef ss_args[] = { di64 };
                LLVMTypeRef ss_ty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx),
                    ss_args, 1, 0);
                LLVMValueRef fn_ss = LLVMGetNamedFunction(g->mod, "zan_co_sync_sleep");
                if (!fn_ss) fn_ss = LLVMAddFunction(g->mod, "zan_co_sync_sleep", ss_ty);
                zan_call2(g->builder, ss_ty, fn_ss,
                    (LLVMValueRef[]){ zan_iwiden(g->builder, ms32, di64) }, 1, "");
            }
            return LLVMConstInt(di64, 0, 0);
        }

        /* await Gate.Park(handle): 事件驱动协程等待原语 */
        if (is_call_to(expr->await_expr.expr, "Gate", "Park") &&
            expr->await_expr.expr->call.args.count == 1) {
            LLVMTypeRef di64 = LLVMInt64TypeInContext(g->ctx);
            LLVMTypeRef di32 = LLVMInt32TypeInContext(g->ctx);
            LLVMTypeRef di8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            if (!(g->current_async_frame && g->current_async_switch)) {
                zan_diag_emit_code(g->diag, DIAG_ERROR, expr->loc, 3033,
                    "await Gate.Park is only supported inside an async method");
                return LLVMConstInt(di64, 0, 0);
            }
            LLVMValueRef handle = emit_expr(g, expr->await_expr.expr->call.args.items[0], locals);
            if (LLVMTypeOf(handle) != di64) {
                handle = LLVMBuildIntCast2(g->builder, handle, di64, 1, "gate64");
            }
            g->uses_socket_async = true;
            LLVMTypeRef gate_park_type = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx),
                (LLVMTypeRef[]){ di64, di8ptr, g->co_step_ptr }, 3, 0);
            LLVMValueRef gate_park = LLVMGetNamedFunction(g->mod, "zan_gate_park");
            if (!gate_park) {
                gate_park = LLVMAddFunction(g->mod, "zan_gate_park", gate_park_type);
            }
            int k = g->current_async_next_state++;
            LLVMValueRef self_i8 = get_async_self_i8(g);
            zan_store_fit(g, LLVMConstInt(di32, (unsigned)k, 0), get_async_state_ptr(g));
            zan_call2(g->builder, gate_park_type, gate_park,
                (LLVMValueRef[]){ handle, self_i8, g->current_async_resume_fn }, 3, "");
            LLVMBuildBr(g->builder, get_async_suspend_ret_bb(g));

            LLVMBasicBlockRef rk = LLVMAppendBasicBlockInContext(g->ctx,
                g->current_async_resume_fn, "co.resume");
            LLVMAddCase(g->current_async_switch, LLVMConstInt(di32, (unsigned)k, 0), rk);
            LLVMPositionBuilderAtEnd(g->builder, rk);
            di_set_loc(g, expr->loc);
            return LLVMConstInt(di64, 0, 0);
        }

        /* await Task.JoinWait(entry): 事件驱动汇合挂起 */
        if (is_call_to(expr->await_expr.expr, "Task", "JoinWait") &&
            expr->await_expr.expr->call.args.count == 1) {
            LLVMTypeRef di64 = LLVMInt64TypeInContext(g->ctx);
            LLVMTypeRef di32 = LLVMInt32TypeInContext(g->ctx);
            LLVMTypeRef di8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMValueRef entry = emit_expr(g, expr->await_expr.expr->call.args.items[0], locals);
            if (LLVMTypeOf(entry) != di64)
                entry = LLVMBuildIntCast2(g->builder, entry, di64, 1, "join64");
            LLVMTypeRef jw_type = LLVMFunctionType(di32,
                (LLVMTypeRef[]){ di64, di8ptr, g->co_step_ptr }, 3, 0);
            LLVMValueRef jw = LLVMGetNamedFunction(g->mod, "zan_join_wait2");
            if (!jw) jw = LLVMAddFunction(g->mod, "zan_join_wait2", jw_type);
            if (g->current_async_frame && g->current_async_switch) {
                int k = g->current_async_next_state++;
                LLVMValueRef self_i8 = get_async_self_i8(g);
                LLVMValueRef r = zan_call2(g->builder, jw_type, jw,
                    (LLVMValueRef[]){ entry, self_i8, g->current_async_resume_fn },
                    3, "join.wait");
                LLVMBuildStore(g->builder,
                    LLVMBuildZExt(g->builder, r, di64, "join.r"),
                    get_async_result_ptr(g));
                LLVMValueRef must_suspend = zan_icmp(g->builder, LLVMIntEQ, r,
                    LLVMConstInt(di32, 0, 0), "join.susp");
                LLVMValueRef fn = g->current_fn;
                LLVMBasicBlockRef susp_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "join.park");
                LLVMBasicBlockRef rk = LLVMAppendBasicBlockInContext(g->ctx,
                    g->current_async_resume_fn, "co.resume");
                LLVMBuildCondBr(g->builder, must_suspend, susp_bb, rk);
                LLVMPositionBuilderAtEnd(g->builder, susp_bb);
                di_set_loc(g, expr->loc);
                zan_store_fit(g, LLVMConstInt(di32, (unsigned)k, 0), get_async_state_ptr(g));
                LLVMBuildBr(g->builder, get_async_suspend_ret_bb(g));
                LLVMAddCase(g->current_async_switch, LLVMConstInt(di32, (unsigned)k, 0), rk);
                LLVMPositionBuilderAtEnd(g->builder, rk);
                di_set_loc(g, expr->loc);
                return LLVMBuildLoad2(g->builder, di64, get_async_result_ptr(g), "join.r2");
            }
            LLVMValueRef r2 = zan_call2(g->builder, jw_type, jw,
                (LLVMValueRef[]){ entry, LLVMConstNull(di8ptr),
                                  LLVMConstNull(g->co_step_ptr) }, 3, "join.wait.root");
            return LLVMBuildZExt(g->builder, r2, di64, "join.r");
        }

        /* 套接字就绪态异步等待（Reactor 反应堆通知） */
        {
            bool is_read  = is_call_to(expr->await_expr.expr, "Socket", "ReadReady");
            bool is_write = is_call_to(expr->await_expr.expr, "Socket", "WriteReady");
            if ((is_read || is_write) && expr->await_expr.expr->call.args.count == 1) {
                LLVMTypeRef di64 = LLVMInt64TypeInContext(g->ctx);
                LLVMTypeRef di32 = LLVMInt32TypeInContext(g->ctx);
                LLVMTypeRef di8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                if (!(g->current_async_frame && g->current_async_switch)) {
                    zan_diag_emit_code(g->diag, DIAG_ERROR, expr->loc, 3034,
                        "await Socket.%s is only supported inside an async method",
                        is_read ? "ReadReady" : "WriteReady");
                    return LLVMConstInt(di64, 0, 0);
                }
                LLVMValueRef fd = emit_expr(g, expr->await_expr.expr->call.args.items[0], locals);
                if (LLVMTypeOf(fd) != di64) {
                    fd = LLVMBuildIntCast2(g->builder, fd, di64, 1, "fd64");
                }
                /* ZAN_IO_READ = 1, ZAN_IO_WRITE = 2 (see rt_io.h) */
                LLVMValueRef interest = LLVMConstInt(di32, is_read ? 1 : 2, 0);
                g->uses_socket_async = true;

                int k = g->current_async_next_state++;
                LLVMValueRef self_i8 = get_async_self_i8(g);
                zan_store_fit(g, LLVMConstInt(di32, (unsigned)k, 0), get_async_state_ptr(g));
                zan_call2(g->builder, g->rt_io_wait_co_type, g->rt_io_wait_co,
                    (LLVMValueRef[]){ fd, interest, self_i8, g->current_async_resume_fn }, 4, "");
                LLVMBuildBr(g->builder, get_async_suspend_ret_bb(g));

                LLVMBasicBlockRef rk = LLVMAppendBasicBlockInContext(g->ctx,
                    g->current_async_resume_fn, "co.resume");
                LLVMAddCase(g->current_async_switch, LLVMConstInt(di32, (unsigned)k, 0), rk);
                LLVMPositionBuilderAtEnd(g->builder, rk);
                di_set_loc(g, expr->loc);
                return LLVMConstInt(di64, 0, 0);
            }
        }

        /* Windows IOCP 重叠接收异步等待 */
        {
            if (is_call_to(expr->await_expr.expr, "Socket", "RecvOv") &&
                expr->await_expr.expr->call.args.count == 3) {
                LLVMTypeRef di64 = LLVMInt64TypeInContext(g->ctx);
                LLVMTypeRef di32 = LLVMInt32TypeInContext(g->ctx);
                LLVMTypeRef di8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                if (!(g->current_async_frame && g->current_async_switch)) {
                    zan_diag_emit_code(g->diag, DIAG_ERROR, expr->loc, 3035,
                        "await Socket.RecvOv is only supported inside an async method");
                    return LLVMConstInt(di64, 0, 0);
                }
                LLVMValueRef fd = emit_expr(g, expr->await_expr.expr->call.args.items[0], locals);
                if (LLVMTypeOf(fd) != di64)
                    fd = LLVMBuildIntCast2(g->builder, fd, di64, 1, "fd64");
                LLVMValueRef buf = emit_expr(g, expr->await_expr.expr->call.args.items[1], locals);
                if (LLVMTypeOf(buf) != di8ptr) {
                    if (LLVMGetTypeKind(LLVMTypeOf(buf)) == LLVMIntegerTypeKind)
                        buf = LLVMBuildIntToPtr(g->builder, buf, di8ptr, "recvbuf");
                    else
                        buf = LLVMBuildBitCast(g->builder, buf, di8ptr, "recvbuf");
                }
                LLVMValueRef len = emit_expr(g, expr->await_expr.expr->call.args.items[2], locals);
                if (LLVMTypeOf(len) != di32)
                    len = LLVMBuildIntCast2(g->builder, len, di32, 1, "len32");
                g->uses_socket_async = true;

                int k = g->current_async_next_state++;
                LLVMValueRef self_i8 = get_async_self_i8(g);
                LLVMValueRef out_n = get_async_result_ptr(g);
                zan_store_fit(g, LLVMConstInt(di32, (unsigned)k, 0), get_async_state_ptr(g));
                zan_call2(g->builder, g->rt_io_recv_co_type, g->rt_io_recv_co,
                    (LLVMValueRef[]){ fd, buf, len, self_i8,
                        g->current_async_resume_fn, out_n }, 6, "");
                LLVMBuildBr(g->builder, get_async_suspend_ret_bb(g));

                LLVMBasicBlockRef rk = LLVMAppendBasicBlockInContext(g->ctx,
                    g->current_async_resume_fn, "co.resume");
                LLVMAddCase(g->current_async_switch, LLVMConstInt(di32, (unsigned)k, 0), rk);
                LLVMPositionBuilderAtEnd(g->builder, rk);
                di_set_loc(g, expr->loc);
                return LLVMBuildLoad2(g->builder, di64, get_async_result_ptr(g), "recvn");
            }
        }

        /* 带超时的 Windows 套接字重叠接收挂起 */
        {
            if (is_call_to(expr->await_expr.expr, "Socket", "RecvToOv") &&
                expr->await_expr.expr->call.args.count == 4) {
                LLVMTypeRef di64 = LLVMInt64TypeInContext(g->ctx);
                LLVMTypeRef di32 = LLVMInt32TypeInContext(g->ctx);
                LLVMTypeRef di8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                if (!(g->current_async_frame && g->current_async_switch)) {
                    zan_diag_emit_code(g->diag, DIAG_ERROR, expr->loc, 3036,
                        "await Socket.RecvToOv is only supported inside an async method");
                    return LLVMConstInt(di64, 0, 0);
                }
                LLVMValueRef fd = emit_expr(g, expr->await_expr.expr->call.args.items[0], locals);
                if (LLVMTypeOf(fd) != di64)
                    fd = LLVMBuildIntCast2(g->builder, fd, di64, 1, "fd64");
                LLVMValueRef buf = emit_expr(g, expr->await_expr.expr->call.args.items[1], locals);
                if (LLVMTypeOf(buf) != di8ptr) {
                    if (LLVMGetTypeKind(LLVMTypeOf(buf)) == LLVMIntegerTypeKind)
                        buf = LLVMBuildIntToPtr(g->builder, buf, di8ptr, "recvbuf");
                    else
                        buf = LLVMBuildBitCast(g->builder, buf, di8ptr, "recvbuf");
                }
                LLVMValueRef len = emit_expr(g, expr->await_expr.expr->call.args.items[2], locals);
                if (LLVMTypeOf(len) != di32)
                    len = LLVMBuildIntCast2(g->builder, len, di32, 1, "len32");
                LLVMValueRef tmo = emit_expr(g, expr->await_expr.expr->call.args.items[3], locals);
                if (LLVMTypeOf(tmo) != di64)
                    tmo = LLVMBuildIntCast2(g->builder, tmo, di64, 1, "tmo64");
                g->uses_socket_async = true;

                int k = g->current_async_next_state++;
                LLVMValueRef self_i8 = get_async_self_i8(g);
                LLVMValueRef out_n = get_async_result_ptr(g);
                zan_store_fit(g, LLVMConstInt(di32, (unsigned)k, 0), get_async_state_ptr(g));
                zan_call2(g->builder, g->rt_io_recv_to_co_type, g->rt_io_recv_to_co,
                    (LLVMValueRef[]){ fd, buf, len, tmo, self_i8,
                        g->current_async_resume_fn, out_n }, 7, "");
                LLVMBuildBr(g->builder, get_async_suspend_ret_bb(g));

                LLVMBasicBlockRef rk = LLVMAppendBasicBlockInContext(g->ctx,
                    g->current_async_resume_fn, "co.resume");
                LLVMAddCase(g->current_async_switch, LLVMConstInt(di32, (unsigned)k, 0), rk);
                LLVMPositionBuilderAtEnd(g->builder, rk);
                di_set_loc(g, expr->loc);
                return LLVMBuildLoad2(g->builder, di64, get_async_result_ptr(g), "recvton");
            }
        }

        /* Windows AcceptEx 异步接收连接完成等待 */
        {
            if (is_call_to(expr->await_expr.expr, "Socket", "AcceptOv") &&
                expr->await_expr.expr->call.args.count == 1) {
                LLVMTypeRef di64 = LLVMInt64TypeInContext(g->ctx);
                LLVMTypeRef di32 = LLVMInt32TypeInContext(g->ctx);
                LLVMTypeRef di8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                if (!(g->current_async_frame && g->current_async_switch)) {
                    zan_diag_emit_code(g->diag, DIAG_ERROR, expr->loc, 3037,
                        "await Socket.AcceptOv is only supported inside an async method");
                    return LLVMConstInt(di64, 0, 0);
                }
                LLVMValueRef fd = emit_expr(g, expr->await_expr.expr->call.args.items[0], locals);
                if (LLVMTypeOf(fd) != di64)
                    fd = LLVMBuildIntCast2(g->builder, fd, di64, 1, "fd64");
                g->uses_socket_async = true;

                int k = g->current_async_next_state++;
                LLVMValueRef self_i8 = get_async_self_i8(g);
                LLVMValueRef out_fd = get_async_result_ptr(g);
                zan_store_fit(g, LLVMConstInt(di32, (unsigned)k, 0), get_async_state_ptr(g));
                zan_call2(g->builder, g->rt_io_accept_co_type,
                    g->rt_io_accept_co, (LLVMValueRef[]){ fd, self_i8,
                        g->current_async_resume_fn, out_fd }, 4, "");
                LLVMBuildBr(g->builder, get_async_suspend_ret_bb(g));

                LLVMBasicBlockRef rk = LLVMAppendBasicBlockInContext(g->ctx,
                    g->current_async_resume_fn, "co.resume");
                LLVMAddCase(g->current_async_switch,
                    LLVMConstInt(di32, (unsigned)k, 0), rk);
                LLVMPositionBuilderAtEnd(g->builder, rk);
                di_set_loc(g, expr->loc);
                return LLVMBuildLoad2(g->builder, di64, get_async_result_ptr(g), "acceptfd");
            }
        }

        /* 异步 DNS 解析等待 */
        {
            if (is_call_to(expr->await_expr.expr, "Socket", "ResolveAsync") &&
                expr->await_expr.expr->call.args.count == 1) {
                LLVMTypeRef di64 = LLVMInt64TypeInContext(g->ctx);
                LLVMTypeRef di32 = LLVMInt32TypeInContext(g->ctx);
                LLVMTypeRef di8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                if (!(g->current_async_frame && g->current_async_switch)) {
                    zan_diag_emit_code(g->diag, DIAG_ERROR, expr->loc, 3038,
                        "await Socket.ResolveAsync is only supported inside an async method");
                    return LLVMConstInt(di64, 0, 0);
                }
                LLVMValueRef host = emit_expr(g, expr->await_expr.expr->call.args.items[0], locals);
                if (LLVMTypeOf(host) != di8ptr)
                    host = LLVMBuildBitCast(g->builder, host, di8ptr, "dns.host");
                g->uses_socket_async = true;

                int k = g->current_async_next_state++;
                LLVMValueRef self_i8 = get_async_self_i8(g);
                /* 套接字结果槽位指针绑定至反应堆事件 */
                LLVMValueRef res_gep = get_async_result_ptr(g);
                LLVMValueRef out32 = LLVMBuildBitCast(g->builder, res_gep,
                    LLVMPointerType(di32, 0), "self.dnsout");
                zan_store_fit(g, LLVMConstInt(di32, (unsigned)k, 0), get_async_state_ptr(g));
                zan_call2(g->builder, g->rt_io_resolve_co_type,
                    g->rt_io_resolve_co, (LLVMValueRef[]){ host, self_i8,
                        g->current_async_resume_fn, out32 }, 4, "");
                LLVMBuildBr(g->builder, get_async_suspend_ret_bb(g));

                LLVMBasicBlockRef rk = LLVMAppendBasicBlockInContext(g->ctx,
                    g->current_async_resume_fn, "co.resume");
                LLVMAddCase(g->current_async_switch,
                    LLVMConstInt(di32, (unsigned)k, 0), rk);
                LLVMPositionBuilderAtEnd(g->builder, rk);
                di_set_loc(g, expr->loc);
                LLVMValueRef res_slot = LLVMBuildBitCast(g->builder,
                    get_async_result_ptr(g),
                    LLVMPointerType(di32, 0), "self.dnsout2");
                return LLVMBuildLoad2(g->builder, di32, res_slot, "dnsaddr");
            }
        }

        /* 异步套接字地址解析挂起 */
        {
            if (is_call_to(expr->await_expr.expr, "Socket", "ResolveSockAddr") &&
                expr->await_expr.expr->call.args.count == 4) {
                LLVMTypeRef di64 = LLVMInt64TypeInContext(g->ctx);
                LLVMTypeRef di32 = LLVMInt32TypeInContext(g->ctx);
                LLVMTypeRef di8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                if (!(g->current_async_frame && g->current_async_switch)) {
                    zan_diag_emit_code(g->diag, DIAG_ERROR, expr->loc, 3039,
                        "await Socket.ResolveSockAddr is only supported inside an async method");
                    return LLVMConstInt(di64, 0, 0);
                }
                LLVMValueRef name = emit_expr(g, expr->await_expr.expr->call.args.items[0], locals);
                if (LLVMTypeOf(name) != di8ptr)
                    name = LLVMBuildBitCast(g->builder, name, di8ptr, "sa.name");
                LLVMValueRef port = emit_expr(g, expr->await_expr.expr->call.args.items[1], locals);
                if (LLVMTypeOf(port) != di32)
                    port = LLVMBuildIntCast2(g->builder, port, di32, 1, "sa.port");
                LLVMValueRef buf = emit_expr(g, expr->await_expr.expr->call.args.items[2], locals);
                if (LLVMTypeOf(buf) != di8ptr)
                    buf = LLVMBuildBitCast(g->builder, buf, di8ptr, "sa.buf");
                LLVMValueRef cap = emit_expr(g, expr->await_expr.expr->call.args.items[3], locals);
                if (LLVMTypeOf(cap) != di32)
                    cap = LLVMBuildIntCast2(g->builder, cap, di32, 1, "sa.cap");
                g->uses_socket_async = true;

                int k = g->current_async_next_state++;
                LLVMValueRef self_i8 = get_async_self_i8(g);
                LLVMValueRef res_gep = get_async_result_ptr(g);
                LLVMValueRef out32 = LLVMBuildBitCast(g->builder, res_gep,
                    LLVMPointerType(di32, 0), "self.saout");
                zan_store_fit(g, LLVMConstInt(di32, (unsigned)k, 0), get_async_state_ptr(g));
                zan_call2(g->builder, g->rt_io_resolve_sa_co_type,
                    g->rt_io_resolve_sa_co, (LLVMValueRef[]){ name, port, buf,
                        cap, self_i8, g->current_async_resume_fn, out32 }, 7, "");
                LLVMBuildBr(g->builder, get_async_suspend_ret_bb(g));

                LLVMBasicBlockRef rk = LLVMAppendBasicBlockInContext(g->ctx,
                    g->current_async_resume_fn, "co.resume");
                LLVMAddCase(g->current_async_switch,
                    LLVMConstInt(di32, (unsigned)k, 0), rk);
                LLVMPositionBuilderAtEnd(g->builder, rk);
                di_set_loc(g, expr->loc);
                LLVMValueRef res_slot = LLVMBuildBitCast(g->builder,
                    get_async_result_ptr(g),
                    LLVMPointerType(di32, 0), "self.saout2");
                return LLVMBuildLoad2(g->builder, di32, res_slot, "salen");
            }
        }

        {
            zan_symbol_t *native = direct_extern_method(g, expr->await_expr.expr);
            if (native)
                return emit_await_blocking_extern(g, expr, locals, native);
        }

        /* await <call>: 等待异步方法子协程执行完成 */
        /* await 挂起点临时栈深度记录，供异常展开清理 */
        LLVMValueRef aw_tmp_mark = LLVMBuildLoad2(g->builder,
            LLVMInt32TypeInContext(g->ctx), get_eh_tmp_top_global(g),
            "aw.tmpmark");
        /* await 一个**非异步**的值：原样返回，不进协程帧那套。
         *
         * 例如 `await someSyncDelegate()`——同步委托的返回值是**非异步**的，
         * 它不是子协程帧，所以应认作普通值。判错就是读野指针：普通值
         * 往往也是 i8 指针（字符串就是），与子协程帧的指针同型，被当成
         * 帧去读就会读出面元指针——静默的垃圾值，不报错。 */
        if (!await_target_is_async(g, expr->await_expr.expr, locals)) {
            return emit_expr(g, expr->await_expr.expr, locals);
        }
        LLVMValueRef sub = emit_expr(g, expr->await_expr.expr, locals);
        LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
        LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
        LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
        LLVMTypeRef hdr = g->co_header_type;

        LLVMValueRef sub_resume = NULL;
        if (LLVMIsACallInst(sub)) {
            /* 直接异步调用：已知被调用函数入口，直连 ramp 与 resume 符号 */
            LLVMValueRef callee = LLVMGetCalledValue(sub);
            LLVMValueRef callee_fn = callee ? LLVMIsAFunction(callee) : NULL;
            if (callee_fn) {
                size_t nl = 0;
                const char *cn = LLVMGetValueName2(callee_fn, &nl);
                if (cn && nl > 0 && nl < 240) {
                    char rn[256];
                    memcpy(rn, cn, nl);
                    memcpy(rn + nl, "$resume", 8);
                    sub_resume = LLVMGetNamedFunction(g->mod, rn);
                }
            }
        }

        /* 间接异步调用：通过虚方法表或委托分发 resume 入口 */
        if (!sub_resume &&
            LLVMGetTypeKind(LLVMTypeOf(sub)) == LLVMPointerTypeKind) {
            LLVMValueRef sub_hdr = (LLVMTypeOf(sub) == i8ptr) ? sub :
                LLVMBuildBitCast(g->builder, sub, i8ptr, "sub.hdr");
            LLVMValueRef ssp = LLVMBuildStructGEP2(g->builder, hdr, sub_hdr,
                ASYNC_FRAME_SELF_STEP, "sub.selfstep.p");
            sub_resume = LLVMBuildLoad2(g->builder, g->co_step_ptr, ssp, "sub.selfstep");
        }

        if (sub_resume && LLVMGetTypeKind(LLVMTypeOf(sub)) == LLVMPointerTypeKind) {
            LLVMValueRef sub_i8 = (LLVMTypeOf(sub) == i8ptr) ? sub :
                LLVMBuildBitCast(g->builder, sub, i8ptr, "sub");

            if (g->current_async_frame && g->current_async_switch) {
                int k = g->current_async_next_state++;
                LLVMTypeRef ptr_int_ty = g->target_is_wasm ? i32 : i64;
                LLVMValueRef fn = g->current_async_resume_fn;

                LLVMBasicBlockRef prep_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "await.prep");
                LLVMBasicBlockRef suspend_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "await.suspend");
                LLVMBasicBlockRef rk = LLVMAppendBasicBlockInContext(g->ctx, fn, "co.resume");
                LLVMBasicBlockRef cont_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "await.cont");

                LLVMAddCase(g->current_async_switch, LLVMConstInt(i32, (unsigned)k, 0), rk);

                /* 快路径探测：子协程若已同步完成则直接内联提取结果，避免调度开销 */
                LLVMValueRef done_p = LLVMBuildStructGEP2(g->builder, hdr, sub_i8,
                    ASYNC_FRAME_DONE, "sub.done.p");
                LLVMValueRef is_done = LLVMBuildLoad2(g->builder, i32, done_p, "sub.is_done");
                LLVMSetOrdering(is_done, LLVMAtomicOrderingAcquire);
                LLVMValueRef fast_cond = zan_icmp(g->builder, LLVMIntNE, is_done,
                    LLVMConstInt(i32, 0, 0), "sub.already_done");
                LLVMBasicBlockRef probe_bb = LLVMGetInsertBlock(g->builder);
                LLVMBuildCondBr(g->builder, fast_cond, cont_bb, prep_bb);

                LLVMPositionBuilderAtEnd(g->builder, prep_bb);
                di_set_loc(g, expr->loc);
                zan_store_fit(g, g->current_async_resume_fn,
                    LLVMBuildStructGEP2(g->builder, hdr, sub_i8, ASYNC_FRAME_AWAITER_STEP, "sub.aws"));
                LLVMValueRef aw_ptr = LLVMBuildStructGEP2(g->builder, hdr, sub_i8,
                    ASYNC_FRAME_AWAITER, "sub.aw");
                LLVMTypeRef aw_target_pty = LLVMPointerType(ptr_int_ty, 0);
                LLVMValueRef aw_iptr = (LLVMTypeOf(aw_ptr) == aw_target_pty) ? aw_ptr :
                    LLVMBuildBitCast(g->builder, aw_ptr, aw_target_pty, "sub.aw.iptr");
                LLVMValueRef self_int = get_async_self_int(g, ptr_int_ty);
                LLVMValueRef cas_res = LLVMBuildAtomicCmpXchg(g->builder, aw_iptr,
                    LLVMConstInt(ptr_int_ty, 0, 0), self_int,
                    LLVMAtomicOrderingSequentiallyConsistent,
                    LLVMAtomicOrderingSequentiallyConsistent, 0);
                LLVMValueRef won = LLVMBuildExtractValue(g->builder, cas_res, 1, "cas.won");
                LLVMBuildCondBr(g->builder, won, suspend_bb, cont_bb);

                LLVMPositionBuilderAtEnd(g->builder, suspend_bb);
                di_set_loc(g, expr->loc);
                LLVMValueRef sub_slot = get_async_sub_slot_ptr(g);
                zan_store_fit(g, sub_i8, sub_slot);
                zan_store_fit(g, sub_i8, get_async_child_ptr(g));
                zan_store_fit(g, LLVMConstInt(i32, (unsigned)k, 0), get_async_state_ptr(g));
                LLVMValueRef sched_args[] = { sub_i8, sub_resume };
                zan_call2(g->builder, g->rt_co_ready_type, g->rt_co_ready, sched_args, 2, "");
                LLVMBuildBr(g->builder, get_async_suspend_ret_bb(g));

                LLVMPositionBuilderAtEnd(g->builder, rk);
                di_set_loc(g, expr->loc);
                LLVMValueRef sub_rl = LLVMBuildLoad2(g->builder, i8ptr, sub_slot, "sub.rl");
                LLVMBuildBr(g->builder, cont_bb);

                LLVMPositionBuilderAtEnd(g->builder, cont_bb);
                di_set_loc(g, expr->loc);
                LLVMValueRef completed_sub = LLVMBuildPhi(g->builder, i8ptr, "sub.completed");
                LLVMValueRef sub_vals[] = { sub_i8, sub_i8, sub_rl };
                LLVMBasicBlockRef sub_bbs[] = { probe_bb, prep_bb, rk };
                LLVMAddIncoming(completed_sub, sub_vals, sub_bbs, 3);
                emit_async_check_sub_exc(g, completed_sub, NULL);
                LLVMValueRef rptr = LLVMBuildStructGEP2(g->builder, hdr, completed_sub,
                    ASYNC_FRAME_RESULT, "sub.result");
                LLVMValueRef awres = LLVMBuildLoad2(g->builder, i64, rptr, "awres");
                LLVMValueRef val = coerce_await_result(g, expr, awres, locals);
                zan_emit_frame_free(g, completed_sub);
                if (LLVMGetTypeKind(LLVMTypeOf(val)) == LLVMVoidTypeKind)
                    return LLVMConstInt(i64, 0, 0);
                return val;
            }

            /* 根驱动调度循环（非异步调用方）：驱动调度器推进协程执行至完成 */
            LLVMValueRef done_p = LLVMBuildStructGEP2(g->builder, hdr, sub_i8,
                ASYNC_FRAME_DONE, "sub.done.p");
            LLVMValueRef is_done = LLVMBuildLoad2(g->builder, i32, done_p, "sub.is_done");
            LLVMSetOrdering(is_done, LLVMAtomicOrderingAcquire);
            LLVMValueRef need_pump = zan_icmp(g->builder, LLVMIntEQ, is_done,
                LLVMConstInt(i32, 0, 0), "sub.need_pump");
            LLVMValueRef fn = g->current_fn;
            LLVMBasicBlockRef pump_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "await.pump");
            LLVMBasicBlockRef fin_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "await.fin");
            LLVMBuildCondBr(g->builder, need_pump, pump_bb, fin_bb);

            LLVMPositionBuilderAtEnd(g->builder, pump_bb);
            di_set_loc(g, expr->loc);
            LLVMValueRef sched_args[] = { sub_i8, sub_resume };
            zan_call2(g->builder, g->rt_co_ready_type, g->rt_co_ready, sched_args, 2, "");
            /* 单协程调度循环：轮询驱动直至当前协程执行完成 */
            zan_call2(g->builder, g->rt_co_sched_run_until_type,
                g->rt_co_sched_run_until, (LLVMValueRef[]){ done_p }, 1, "");
            LLVMBuildBr(g->builder, fin_bb);

            LLVMPositionBuilderAtEnd(g->builder, fin_bb);
            di_set_loc(g, expr->loc);
            emit_async_check_sub_exc(g, sub_i8, aw_tmp_mark);
            LLVMValueRef rptr = LLVMBuildStructGEP2(g->builder, hdr, sub_i8,
                ASYNC_FRAME_RESULT, "sub.result");
            LLVMValueRef awres = LLVMBuildLoad2(g->builder, i64, rptr, "awres");
            LLVMValueRef val = coerce_await_result(g, expr, awres, locals);
            zan_emit_frame_free(g, sub_i8);
            return val;
        }

        if (LLVMGetTypeKind(LLVMTypeOf(sub)) == LLVMPointerTypeKind) {
            LLVMValueRef task_ptr = LLVMBuildBitCast(g->builder, sub,
                LLVMPointerType(g->task_struct_type, 0), "taskp");
            LLVMBasicBlockRef poll_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "aw.poll");
            LLVMBasicBlockRef done_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "aw.done");
            LLVMBuildBr(g->builder, poll_bb);
            LLVMPositionBuilderAtEnd(g->builder, poll_bb);
            di_set_loc(g, expr->loc);
            LLVMValueRef comp_ptr = LLVMBuildStructGEP2(g->builder, g->task_struct_type, task_ptr, 0, "comp");
            LLVMValueRef comp = LLVMBuildLoad2(g->builder, i64, comp_ptr, "cv");
            LLVMValueRef is_done = zan_icmp(g->builder, LLVMIntNE, comp, LLVMConstInt(i64, 0, 0), "done");
            LLVMBuildCondBr(g->builder, is_done, done_bb, poll_bb);
            LLVMPositionBuilderAtEnd(g->builder, done_bb);
            di_set_loc(g, expr->loc);
            LLVMValueRef res_ptr = LLVMBuildStructGEP2(g->builder, g->task_struct_type, task_ptr, 1, "resp");
            return coerce_await_result(g, expr,
                LLVMBuildLoad2(g->builder, i64, res_ptr, "awres"), locals);
        }
        return sub;
    return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
}

/* 用户自定义隐式/显式类型转换运算符降解 */
static zan_symbol_t *find_conversion_method(zan_irgen_t *g, zan_symbol_t *sym,
                                            zan_type_t *from_type,
                                            zan_type_t *to_type,
                                            const char *op_name) {
    if (!sym) return NULL;
    size_t olen = strlen(op_name);
    for (int i = 0; i < sym->member_count; i++) {
        zan_symbol_t *m = sym->members[i];
        if (!m || m->kind != SYM_METHOD || !(m->modifiers & MOD_STATIC)) continue;
        if (!m->decl || m->decl->kind != AST_METHOD_DECL) continue;
        if ((size_t)m->name.len != olen ||
            memcmp(m->name.str, op_name, olen) != 0) continue;
        zan_type_t *rt = zan_binder_resolve_type(g->binder,
            m->decl->method_decl.return_type);
        if (!rt || !types_concrete_equal(rt, to_type)) continue;
        zan_ast_list_t *ps = &m->decl->method_decl.params;
        if (ps->count != 1 || !ps->items[0] || ps->items[0]->kind != AST_PARAM)
            continue;
        zan_type_t *pt = zan_binder_resolve_type(g->binder,
            ps->items[0]->param.type);
        if (pt && types_concrete_equal(pt, from_type)) return m;
    }
    return NULL;
}

/* 合成 new 节点作为所有权标记 */
zan_ast_node_t *owned_rhs_marker(zan_irgen_t *g, zan_loc_t loc) {
    return zan_ast_new(g->arena, AST_NEW_EXPR, loc);
}
zan_symbol_t *find_user_conversion(zan_irgen_t *g, zan_type_t *from_type,
                                          zan_type_t *to_type,
                                          const char *op_name) {
    if (!from_type || !to_type) return NULL;
    if (from_type->kind == TYPE_CLASS || from_type->kind == TYPE_STRUCT) {
        zan_symbol_t *m = find_conversion_method(g, from_type->sym, from_type,
                                                 to_type, op_name);
        if (m) return m;
    }
    if (to_type->kind == TYPE_CLASS || to_type->kind == TYPE_STRUCT) {
        zan_symbol_t *m = find_conversion_method(g, to_type->sym, from_type,
                                                 to_type, op_name);
        if (m) return m;
    }
    return NULL;
}
