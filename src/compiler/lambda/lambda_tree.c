/* ---------- B-ID65：只读表达式树（显式 ExprNode / Expr<T> 型 lambda） ----------
 * 形参显式标注树类型（`(ExprNode x) => x.age > 18`）的 lambda 在发射口
 * 降级为 ExprNode 构造调用链，SQL 翻译在库侧 ExprSql.Build（Zan.Data）；
 * dbgen 生成器废除（de208fe98）后由编译器原生承接 ExprNode.zan 承诺的
 * 降级契约。挂点在 emit_lambda_typed 顶部：ORM 门面链式调用
 * （db.Select<T>().Where(…)）的接收者在 checker 侧落 error 类型、由 irgen
 * 健壮成员路径兜底（既有架构），重载定型点对树实参不可达，唯一稳定的
 * 会合点就是 lambda 自身的发射。
 * 入树支持面（v1，字面量入树、不捕获外层变量）：
 *   成员访问 x.F、int/double/string/bool/char/null 字面量、
 *   二元 == != < > <= >= + - * / % && ||、一元 ! 与字面量负号、
 *   字符串 StartsWith/EndsWith/Contains（实参须为常量，访问者译 LIKE）。
 * 其余形状一律 E3103 fail-closed——静默降级会把 ORM 谓词悄悄变成
 * 无条件全表（ExprSql 旧版对未知调用回 1=1，正是审计要堵的口）。 */

static zan_istr_t irgen_tree_istr(zan_irgen_t *g, const char *s) {
    size_t n = strlen(s);
    zan_istr_t out;
    out.str = zan_arena_strdup(g->arena, s, n);
    out.len = (uint32_t)n;
    return out;
}

static zan_ast_node_t *irgen_tree_id(zan_irgen_t *g, const char *name,
                                     zan_loc_t loc) {
    zan_ast_node_t *n = zan_ast_new(g->arena, AST_IDENTIFIER, loc);
    n->ident.name = irgen_tree_istr(g, name);
    n->ident.inst_type_ref = NULL;
    return n;
}

/* 造 ExprNode.Fn(args…) 普通静态调用节点（ExprNode 静态工厂）。 */
static zan_ast_node_t *irgen_tree_call(zan_irgen_t *g, const char *fn,
                                       zan_ast_node_t **args, int argc,
                                       zan_loc_t loc) {
    zan_ast_node_t *mc = zan_ast_new(g->arena, AST_MEMBER_ACCESS, loc);
    mc->member.object = irgen_tree_id(g, "ExprNode", loc);
    mc->member.name = irgen_tree_istr(g, fn);
    mc->member.null_cond = 0;
    zan_ast_node_t *call = zan_ast_new(g->arena, AST_CALL, loc);
    call->call.callee = mc;
    zan_ast_list_init(&call->call.args);
    for (int i = 0; i < argc; i++)
        if (args[i]) zan_ast_list_push(&call->call.args, args[i], g->arena);
    zan_ast_list_init(&call->call.type_args);
    return call;
}

static zan_ast_node_t *irgen_tree_str(zan_irgen_t *g, zan_istr_t s,
                                      zan_loc_t loc) {
    zan_ast_node_t *n = zan_ast_new(g->arena, AST_STRING_LITERAL, loc);
    n->str_val = s;
    return n;
}

/* Expr<T>.From(tree)：泛型静态直调（Meta.For<E> 同款发射路径）。 */
static zan_ast_node_t *irgen_tree_from(zan_irgen_t *g, zan_type_t *wt,
                                       zan_ast_node_t *tree, zan_loc_t loc) {
    zan_ast_node_t *mc = zan_ast_new(g->arena, AST_MEMBER_ACCESS, loc);
    mc->member.object = irgen_tree_id(g, "Expr", loc);
    mc->member.name = irgen_tree_istr(g, "From");
    mc->member.null_cond = 0;
    zan_ast_node_t *call = zan_ast_new(g->arena, AST_CALL, loc);
    call->call.callee = mc;
    zan_ast_list_init(&call->call.args);
    zan_ast_list_push(&call->call.args, tree, g->arena);
    zan_ast_list_init(&call->call.type_args);
    zan_ast_node_t *tr = zan_ast_new(g->arena, AST_TYPE_REF, loc);
    tr->type_ref.name = wt->sym->name;
    tr->type_ref.is_nullable = false;
    tr->type_ref.is_array = false;
    zan_ast_list_init(&tr->type_ref.type_args);
    zan_ast_list_push(&call->call.type_args, tr, g->arena);
    return call;
}

/* 树目标类型（B-ID65）：System.Linq.ExprNode 或 System.Linq.Expr<T>。
 * irgen_expr_core.c（同编译单元、先于本文件包含）的过载打分也要判——
 * 前置声明在那边。 */
bool irgen_type_is_tree_target(zan_type_t *t) {
    if (!t || t->kind != TYPE_CLASS || !t->sym) return false;
    if (t->sym->name.len == 8 &&
        memcmp(t->sym->name.str, "ExprNode", 8) == 0)
        return true;
    return t->sym->name.len == 4 &&
           memcmp(t->sym->name.str, "Expr", 4) == 0 &&
           t->type_arg_count == 1 && t->type_args && t->type_args[0];
}

static bool irgen_tree_target_type(zan_irgen_t *g, zan_ast_node_t *ptype) {
    if (!ptype) return false;
    return irgen_type_is_tree_target(resolve_type_ctx(g, ptype));
}
bool irgen_is_tree_lambda(zan_irgen_t *g, zan_ast_node_t *expr) {
    if (!expr || expr->kind != AST_LAMBDA || expr->lambda.params.count != 1)
        return false;
    return irgen_tree_target_type(g, expr->lambda.params.items[0]->param.type);
}

/* 把 lambda 体改写为 ExprNode 树；不可译形状发 E3103 并置 *ok=false。
 * 成员名不在发射口校验（无实体类型可用）——列名合法性由库侧
 * Select.WExpr 按 Meta 校验（fail-closed），checker 流够得着的调用形状
 * （局部变量接收者）另有预检跳过、打分放行两道。 */
static zan_ast_node_t *irgen_build_expr_tree(zan_irgen_t *g,
                                             zan_ast_node_t *e,
                                             zan_istr_t pn, bool *ok) {
    if (!e) { *ok = false; return NULL; }
    switch (e->kind) {
    case AST_INT_LITERAL: {
        zan_ast_node_t *a = zan_ast_new(g->arena, AST_INT_LITERAL, e->loc);
        a->int_val = e->int_val;
        return irgen_tree_call(g, "Const", &a, 1, e->loc);
    }
    case AST_FLOAT_LITERAL: {
        zan_ast_node_t *a = zan_ast_new(g->arena, AST_FLOAT_LITERAL, e->loc);
        a->float_val = e->float_val;
        return irgen_tree_call(g, "Const", &a, 1, e->loc);
    }
    case AST_STRING_LITERAL:
        return irgen_tree_call(g, "Const", &e, 1, e->loc);
    case AST_BOOL_LITERAL: {
        zan_ast_node_t *a = zan_ast_new(g->arena, AST_BOOL_LITERAL, e->loc);
        a->bool_val = e->bool_val;
        return irgen_tree_call(g, "Const", &a, 1, e->loc);
    }
    case AST_CHAR_LITERAL: {
        /* 字符按 1 字符串入树（SQL 文本列比较口径，char→int 会比错列型）。 */
        char b[2] = { (char)e->int_val, 0 };
        zan_ast_node_t *a = irgen_tree_str(g, irgen_tree_istr(g, b), e->loc);
        return irgen_tree_call(g, "Const", &a, 1, e->loc);
    }
    case AST_NULL_LITERAL:
        return irgen_tree_call(g, "NullConst", NULL, 0, e->loc);
    case AST_DECIMAL_LITERAL:
        zan_diag_emit_code(g->diag, DIAG_ERROR, e->loc, 3103,
            "expression tree lambda: decimal literals have no ExprNode form");
        *ok = false;
        return NULL;
    case AST_UNARY: {
        zan_ast_node_t *o = e->unary.operand;
        if (e->unary.op == TK_BANG) {
            zan_ast_node_t *a = irgen_build_expr_tree(g, o, pn, ok);
            if (!*ok) return NULL;
            return irgen_tree_call(g, "Not", &a, 1, e->loc);
        }
        if (e->unary.op == TK_MINUS && o && o->kind == AST_INT_LITERAL) {
            zan_ast_node_t *a =
                zan_ast_new(g->arena, AST_INT_LITERAL, e->loc);
            a->int_val = -o->int_val;
            return irgen_tree_call(g, "Const", &a, 1, e->loc);
        }
        if (e->unary.op == TK_MINUS && o && o->kind == AST_FLOAT_LITERAL) {
            zan_ast_node_t *a =
                zan_ast_new(g->arena, AST_FLOAT_LITERAL, e->loc);
            a->float_val = -o->float_val;
            return irgen_tree_call(g, "Const", &a, 1, e->loc);
        }
        if (e->unary.op == TK_PLUS)
            return irgen_build_expr_tree(g, o, pn, ok);
        zan_diag_emit_code(g->diag, DIAG_ERROR, e->loc, 3103,
            "expression tree lambda: unary operator '%s' has no ExprNode form",
            zan_token_kind_name(e->unary.op));
        *ok = false;
        return NULL;
    }
    case AST_BINARY: {
        const char *op = NULL;
        switch (e->binary.op) {
        case TK_EQ_EQ: op = "=="; break;
        case TK_BANG_EQ: op = "!="; break;
        case TK_LESS: op = "<"; break;
        case TK_GREATER: op = ">"; break;
        case TK_LESS_EQ: op = "<="; break;
        case TK_GREATER_EQ: op = ">="; break;
        case TK_PLUS: op = "+"; break;
        case TK_MINUS: op = "-"; break;
        case TK_STAR: op = "*"; break;
        case TK_SLASH: op = "/"; break;
        case TK_PERCENT: op = "%"; break;
        case TK_AMP_AMP: op = "&&"; break;
        case TK_PIPE_PIPE: op = "||"; break;
        default: break;
        }
        if (!op) {
            zan_diag_emit_code(g->diag, DIAG_ERROR, e->loc, 3103,
                "expression tree lambda: operator '%s' has no ExprNode form",
                zan_token_kind_name(e->binary.op));
            *ok = false;
            return NULL;
        }
        zan_ast_node_t *l = irgen_build_expr_tree(g, e->binary.left, pn, ok);
        if (!*ok) return NULL;
        zan_ast_node_t *r = irgen_build_expr_tree(g, e->binary.right, pn, ok);
        if (!*ok) return NULL;
        zan_ast_node_t *as[3];
        as[0] = irgen_tree_str(g, irgen_tree_istr(g, op), e->loc);
        as[1] = l;
        as[2] = r;
        return irgen_tree_call(g, "Binary", as, 3, e->loc);
    }
    case AST_IDENTIFIER:
        if (e->ident.name.len == pn.len &&
            memcmp(e->ident.name.str, pn.str, pn.len) == 0)
            zan_diag_emit_code(g->diag, DIAG_ERROR, e->loc, 3103,
                "expression tree lambda: cannot reference the lambda parameter itself; access a member (x.F) instead");
        else
            zan_diag_emit_code(g->diag, DIAG_ERROR, e->loc, 3103,
                "expression tree lambda: outer variables cannot be captured (v1 translates literals only)");
        *ok = false;
        return NULL;
    case AST_MEMBER_ACCESS: {
        zan_ast_node_t *obj = e->member.object;
        if (!obj || obj->kind != AST_IDENTIFIER ||
            obj->ident.name.len != pn.len ||
            memcmp(obj->ident.name.str, pn.str, pn.len) != 0) {
            zan_diag_emit_code(g->diag, DIAG_ERROR, e->loc, 3103,
                "expression tree lambda: only direct member access on the lambda parameter (x.F) is supported");
            *ok = false;
            return NULL;
        }
        zan_ast_node_t *nm = irgen_tree_str(g, e->member.name, e->loc);
        return irgen_tree_call(g, "Member", &nm, 1, e->loc);
    }
    case AST_CALL: {
        zan_ast_node_t *cal = e->call.callee;
        zan_ast_node_t *robj = (cal && cal->kind == AST_MEMBER_ACCESS)
            ? cal->member.object : NULL;
        /* 接收者只认 `x.F`（lambda 参数的直接成员）——字符串列方法
         * `x.name.StartsWith("al")` 的本质形态。参数自身作接收者
         * （`x.Method(…)`）没有对应 SQL 列，v1 拒绝。 */
        zan_ast_node_t *fld = NULL;
        if (cal && cal->kind == AST_MEMBER_ACCESS && robj &&
            robj->kind == AST_MEMBER_ACCESS && robj->member.object &&
            robj->member.object->kind == AST_IDENTIFIER &&
            robj->member.object->ident.name.len == pn.len &&
            memcmp(robj->member.object->ident.name.str, pn.str,
                   pn.len) == 0)
            fld = robj;
        if (!fld) {
            zan_diag_emit_code(g->diag, DIAG_ERROR, e->loc, 3103,
                "expression tree lambda: only x.F.Method(arg) calls on a lambda-parameter member are supported");
            *ok = false;
            return NULL;
        }
        const char *fn = NULL;
        zan_istr_t mn = cal->member.name;
        if (mn.len == 10 && memcmp(mn.str, "StartsWith", 10) == 0)
            fn = "StartsWith";
        else if (mn.len == 8 && memcmp(mn.str, "EndsWith", 8) == 0)
            fn = "EndsWith";
        else if (mn.len == 8 && memcmp(mn.str, "Contains", 8) == 0)
            fn = "Contains";
        if (!fn || e->call.args.count != 1) {
            zan_diag_emit_code(g->diag, DIAG_ERROR, e->loc, 3103,
                "expression tree lambda: method '%.*s' has no ExprNode form (v1 supports StartsWith/EndsWith/Contains with a constant argument)",
                (int)mn.len, mn.str);
            *ok = false;
            return NULL;
        }
        /* 接收方树 = Member("F")（列引用，访问者译 t.F），不是方法名。 */
        zan_ast_node_t *nm = irgen_tree_str(g, fld->member.name, e->loc);
        zan_ast_node_t *recv = irgen_tree_call(g, "Member", &nm, 1, e->loc);
        zan_ast_node_t *arg = irgen_build_expr_tree(g, e->call.args.items[0],
                                                    pn, ok);
        if (!*ok) return NULL;
        /* LIKE 模式在访问者侧按常量展开：实参必须是 Const/NullConst。 */
        zan_istr_t cn;
        cn.str = NULL;
        cn.len = 0;
        if (arg->kind == AST_CALL && arg->call.callee &&
            arg->call.callee->kind == AST_MEMBER_ACCESS)
            cn = arg->call.callee->member.name;
        bool is_const = (cn.len == 5 && memcmp(cn.str, "Const", 5) == 0) ||
                        (cn.len == 9 && memcmp(cn.str, "NullConst", 9) == 0);
        if (!is_const) {
            zan_diag_emit_code(g->diag, DIAG_ERROR, e->loc, 3103,
                "expression tree lambda: the argument to '%s' must be a constant (LIKE patterns expand at the visitor)", fn);
            *ok = false;
            return NULL;
        }
        /* 经 Call 工厂：ExprNode.Call("StartsWith", recv, arg)——
         * 方法名是节点负载（Name），不是 ExprNode 的成员方法
         * （ExprNode.StartsWith 不存在，E3063/E3065 实录）。 */
        zan_ast_node_t *fs = irgen_tree_str(g, cal->member.name, e->loc);
        zan_ast_node_t *as[3];
        as[0] = fs;
        as[1] = recv;
        as[2] = arg;
        return irgen_tree_call(g, "Call", as, 3, e->loc);
    }
    default:
        zan_diag_emit_code(g->diag, DIAG_ERROR, e->loc, 3103,
            "expression tree lambda: this expression form cannot be translated (v1: members, literals, comparison/logic/arithmetic, !, StartsWith/EndsWith/Contains)");
        *ok = false;
        return NULL;
    }
}
LLVMValueRef emit_lambda_typed(zan_irgen_t *g, zan_ast_node_t *expr,
                                      zan_type_t *expected, local_scope_t *locals) {
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    int pc = expr->lambda.params.count;
    bool exp_delegate = expected && expected->kind == TYPE_DELEGATE;

    /* B-ID163①：lambda 不是协程（体内 await 本身就是 E3032），同步 lambda
     * 落到 async 委托后，调用侧按协程帧步进发射、结果槽永远留默认值——
     * string 形即静默空串（TASKS B-ID163 探针实录）。lambda→delegate 的
     * 全部转换面（实参/var decl/字段初始化/赋值/对象初始化式）都汇聚于
     * 本函数，这里是唯一全覆盖收口点；方法组路径的孪生检查是 E3047
     * （check_delegate_async_match）。报错即收敛为 null，防级联噪声
     * （E3103 同款写法）。 */
    if (exp_delegate && expected->delegate_is_async) {
        zan_diag_emit_code(g->diag, DIAG_ERROR, expr->loc, 3104,
            "cannot convert a lambda to async delegate '%.*s': a lambda is not a coroutine; assign an async method group instead",
            expected->name.len, expected->name.str ? expected->name.str : "?");
        return LLVMConstNull(LLVMPointerType(i64, 0));
    }

    /* B-ID65：显式树类型形参的 lambda 在发射口降级为 ExprNode 构造树。
     * 目标为委托时不降级——树 lambda 不隐式转委托（C# Expression<T> 同
     * 规），响亮拒绝。 */
    if (irgen_is_tree_lambda(g, expr) && expr->lambda.body) {
        if (exp_delegate) {
            zan_diag_emit_code(g->diag, DIAG_ERROR, expr->loc, 3103,
                "expression tree lambda cannot convert to delegate type '%s': bind it to an ExprNode/Expr<T> parameter",
                expected->name.str ? expected->name.str : "?");
            /* E3103 已发（error，本轮编译必停）：立即收敛为 null，
             * 不直落常规 lambda 发射——那会把体按 ExprNode 形参再查
             * 一遍，喷出 E3009 级联噪声（p2 探针实录）。 */
            return LLVMConstNull(LLVMPointerType(i64, 0));
        } else if (expr->lambda.body->kind != AST_BLOCK) {
            bool ok = true;
            zan_ast_node_t *tree = irgen_build_expr_tree(
                g, expr->lambda.body,
                expr->lambda.params.items[0]->param.name, &ok);
            if (ok && tree) {
                zan_type_t *p0t = resolve_type_ctx(
                    g, expr->lambda.params.items[0]->param.type);
                if (p0t && p0t->type_arg_count == 1 && p0t->sym)
                    tree = irgen_tree_from(g, p0t, tree, expr->loc);
                return emit_expr(g, tree, locals);
            }
            /* E3103 已发（error，本轮编译必停）；收敛为 null，
             * 不把毒树喂给发射。 */
            return LLVMConstNull(LLVMPointerType(i64, 0));
        }
    }

    zan_type_t **ptypes = (zan_type_t **)calloc((size_t)(pc > 0 ? pc : 1), sizeof(zan_type_t *));
    LLVMTypeRef *param_types = (LLVMTypeRef *)calloc((size_t)(pc > 0 ? pc : 1), sizeof(LLVMTypeRef));
    for (int k = 0; k < pc; k++) {
        zan_ast_node_t *param = expr->lambda.params.items[k];
        zan_type_t *pt = NULL;
        if (param->param.type) {
            pt = resolve_type_ctx(g, param->param.type);
        }
        if (!pt && exp_delegate && k < expected->delegate_param_count) {
            pt = expected->delegate_param_types[k];
        }
        ptypes[k] = pt;
        param_types[k] = pt ? map_type(g, pt) : i64;
    }
    zan_type_t *rett = exp_delegate ? expected->delegate_ret_type : NULL;
    LLVMTypeRef ret_type = rett ? map_type(g, rett) : i64;
    bool ret_void = rett && rett->kind == TYPE_VOID;
    if (ret_void) ret_type = LLVMVoidTypeInContext(g->ctx);

    /* 收集 Lambda 函数体内引用的外层局部变量与 this 接收者作为捕获集 */
    capture_scan_t cs;
    memset(&cs, 0, sizeof(cs));
    cs.g = g;
    cs.outer = locals;
    for (int k = 0; k < pc; k++)
        cap_shadow(&cs, expr->lambda.params.items[k]->param.name);
    cap_scan(&cs, expr->lambda.body);
    if (cs.needs_this && !g->current_this) cs.needs_this = 0;
    if (cs.overflow) {
        zan_diag_emit_code(g->diag, DIAG_ERROR, expr->loc, 3046,
            "out of memory collecting the variables this lambda captures");
        cs.count = 0;
        cs.needs_this = 0;
    }
    int capc = cs.count;
    int has_this = cs.needs_this ? 1 : 0;
    /* wasm32 平台下 Lambda 统一按带环境闭包形式发射 */
    bool is_closure = (capc + has_this) > 0 || target_is_wasm32(g);

    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef *all_params = (LLVMTypeRef *)calloc((size_t)pc + 2, sizeof(LLVMTypeRef));
    unsigned apc = 0;
    if (is_closure) all_params[apc++] = i8ptr;
    for (int k = 0; k < pc; k++) all_params[apc++] = param_types[k];
    LLVMTypeRef fn_type = LLVMFunctionType(ret_type, all_params, apc, 0);
    free(all_params);

    char lname[64];
    static int lambda_id = 0;
    snprintf(lname, sizeof(lname), "lambda_%d",
             (int)__atomic_fetch_add(&lambda_id, 1, __ATOMIC_SEQ_CST));
    LLVMValueRef lambda_fn = LLVMAddFunction(g->mod, lname, fn_type);
    /* 全程序静态编译：Lambda 设为 internal 链接属性 */
    zan_set_module_local(lambda_fn);

    /* 闭包记录内存布局：{ 函数指针, 析构指针, 目标指针, 捕获槽..., [this] } */
    LLVMTypeRef rec_ty = NULL;
    if (is_closure) {
        LLVMTypeRef *fields = (LLVMTypeRef *)calloc(
            (size_t)(ZAN_CLOSURE_HDR_FIELDS + capc + 1), sizeof(LLVMTypeRef));
        if (!fields) {
            cap_scan_free(&cs);
            return LLVMConstNull(i8ptr);
        }
        for (int i = 0; i < ZAN_CLOSURE_HDR_FIELDS; i++) fields[i] = i8ptr;
        for (int i = 0; i < capc; i++)
            fields[ZAN_CLOSURE_HDR_FIELDS + i] = cs.caps[i].llvm;
        if (has_this) fields[ZAN_CLOSURE_HDR_FIELDS + capc] = i8ptr;
        char rname[80];
        snprintf(rname, sizeof(rname), "%s$clo", lname);
        rec_ty = LLVMStructCreateNamed(g->ctx, rname);
        LLVMStructSetBody(rec_ty, fields,
                          (unsigned)(ZAN_CLOSURE_HDR_FIELDS + capc + has_this), 0);
        free(fields);
    }

    LLVMBasicBlockRef saved_bb = LLVMGetInsertBlock(g->builder);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, lambda_fn, "entry");
    LLVMPositionBuilderAtEnd(g->builder, entry);

    /* 调试模式 (-g)：Lambda 发射独立 LLVM 函数以生成精确作用域调试信息 */
    LLVMMetadataRef saved_dl = NULL;
    uint32_t saved_di_line = g->di_cur_line;
    uint32_t saved_di_file = g->di_cur_file;
    if (g->emit_debug) {
        saved_dl = LLVMGetCurrentDebugLocation2(g->builder);
        /* 先清空 builder 继承自外层函数的 !dbg，防止 lambda 头部捕获展开指令
         * 泄漏外层函数的 !DISubprogram 导致 LLVM verification 失败（B-ID162） */
        LLVMSetCurrentDebugLocation2(g->builder, NULL);
    }

    /* Lambda 函数体在独立的函数上下文中生成控制流基本块 */
    LLVMValueRef saved_fn = g->current_fn;
    /* 嵌套 Lambda 就地挂起当前生成函数并切换上下文 */
    bool saved_fn_is_main = g->current_fn_is_main;
    g->current_fn_is_main = false;
    LLVMTypeRef saved_fn_ret = g->current_fn_ret_type;
    zan_type_t *saved_fn_zan_ret = g->current_fn_zan_ret_type;
    LLVMValueRef saved_this = g->current_this;
    LLVMValueRef saved_async_frame = g->current_async_frame;
    zan_ast_node_t *saved_fn_body = g->current_fn_body;
    g->current_fn_body = expr->lambda.body;
    g->current_fn = lambda_fn;
    g->current_fn_ret_type = ret_type;
    g->current_fn_zan_ret_type = rett;

    /* 切换到 lambda_fn 之后，再为 lambda 函数体建立归属于自身的独立调试作用域 */
    if (g->emit_debug) {
        if (expr->loc.line > 0) {
            di_set_loc(g, expr->loc);
        } else {
            di_clear(g);
        }
    }
    int saved_throw_base = g->throw_locals_base;
    int saved_catch_cc = g->catch_cleanup_count;
    int saved_throw_cb = g->throw_catch_base;
    int saved_fin_c = g->finally_count;
    int saved_fin_lb = g->finally_loop_base;
    zan_irgen_pending_context_t saved_pending = g->pending;
    LLVMBasicBlockRef saved_break = g->break_target;
    LLVMBasicBlockRef saved_continue = g->continue_target;
    int saved_loop_base = g->loop_locals_base;
    int saved_break_base = g->break_locals_base;
    int saved_loop_cbase = g->loop_catch_base;
    zan_irgen_finally_entry_t *saved_finallys = NULL;
    zan_irgen_catch_cleanup_t *saved_catches = NULL;
    if (saved_fin_c) {
        size_t bytes = sizeof(g->finallys[0]) * (size_t)saved_fin_c;
        saved_finallys = zan_arena_alloc(g->arena, bytes);
        memcpy(saved_finallys, g->finallys, bytes);
    }
    if (saved_catch_cc) {
        size_t bytes = sizeof(g->catch_cleanups[0]) * (size_t)saved_catch_cc;
        saved_catches = zan_arena_alloc(g->arena, bytes);
        memcpy(saved_catches, g->catch_cleanups, bytes);
    }
    /* lpad 栈同理必须切换：宿主方法的方法级 lpad entry（含已注册的宿主
     * ARC/boxed 局部清单）若滞留栈上，lambda 体内的异常传播兜底
     * （emit_eh_propagate_tail_lpad 对 lpad_stack[0] 的清理，见
     * stmt_helpers.c）就会在 lambda 函数的 builder 里 load/store 宿主
     * 函数的 alloca——跨函数 IR 引用，LLVM verification 直接拒绝
     * （E3090：LspSession.Start 的 Thread.Start lambda 即此因）。 */
    int saved_lpad_depth = g->lpad_depth;
    zan_lpad_entry_t *saved_lpad_entries = NULL;
    if (saved_lpad_depth > 0) {
        size_t lbytes = sizeof(g->lpad_stack[0]) * (size_t)saved_lpad_depth;
        saved_lpad_entries = zan_arena_alloc(g->arena, lbytes);
        memcpy(saved_lpad_entries, g->lpad_stack, lbytes);
    }
    g->lpad_depth = 0;
    int saved_eh_c = g->eh_armed_count;
    int saved_eh_b = g->eh_armed_base;
    int saved_eh_lb = g->eh_armed_loop_base;
    g->throw_locals_base = 0;
    g->catch_cleanup_count = 0;
    g->throw_catch_base = 0;
    g->finally_count = 0;
    g->finally_loop_base = 0;
    g->pending = (zan_irgen_pending_context_t){0};
    g->break_target = NULL;
    g->continue_target = NULL;
    g->loop_locals_base = 0;
    g->break_locals_base = 0;
    g->loop_catch_base = 0;
    g->eh_armed_base = g->eh_armed_count;
    g->eh_armed_loop_base = g->eh_armed_count;
    g->current_this = NULL;
    /* 外层方法有协程帧 = 外层方法是 async：这个 lambda 里的 await 没有
     * 自己的帧可挂，只能挂到外层协程的块上（跨函数的 br，非法 IR）。
     * 同步方法里的 lambda 没有外层协程，根驱动等得到，不算错。 */
    bool saved_lambda_in_async = g->lambda_in_async_method;
    g->lambda_in_async_method = saved_lambda_in_async || (saved_async_frame != NULL);
    g->current_async_frame = NULL;
    g->lambda_depth++;

    local_scope_t lambda_locals;
    local_scope_init(&lambda_locals, g->arena);
    /* 捕获变量排布于闭包首部，进入函数体时从环境记录中重载为局部变量 */
    if (is_closure) {
        LLVMValueRef rec = LLVMGetParam(lambda_fn, 0);
        for (int i = 0; i < capc; i++) {
            LLVMValueRef p = LLVMBuildStructGEP2(g->builder, rec_ty, rec,
                (unsigned)(ZAN_CLOSURE_HDR_FIELDS + i), "cap.p");
            LLVMValueRef v = LLVMBuildLoad2(g->builder, cs.caps[i].llvm, p, "cap");
            if (cs.caps[i].boxed) {
                LLVMTypeRef payload = map_type(g, cs.caps[i].type);
                local_add(&lambda_locals, cs.caps[i].name,
                          box_value_ptr(g, v, payload), cs.caps[i].type);
                /* 闭包持有被捕获引用的所有权，调用方借用访问 */
                lambda_locals.vars[lambda_locals.count - 1].box_cell = v;
                if (is_rc_managed_type(cs.caps[i].type))
                    lambda_locals.vars[lambda_locals.count - 1].arc_owned = 1;
                if (type_contains_collection_rc(g, cs.caps[i].type, 0))
                    lambda_locals.vars[lambda_locals.count - 1].struct_rc = 1;
                continue;
            }
            LLVMValueRef alloc = LLVMBuildAlloca(g->builder, cs.caps[i].llvm, "cap.slot");
            LLVMBuildStore(g->builder, v, alloc);
            local_add(&lambda_locals, cs.caps[i].name, alloc, cs.caps[i].type);
        }
        if (has_this) {
            LLVMValueRef p = LLVMBuildStructGEP2(g->builder, rec_ty, rec,
                (unsigned)(ZAN_CLOSURE_HDR_FIELDS + capc), "cap.thisp");
            LLVMValueRef v = LLVMBuildLoad2(g->builder, i8ptr, p, "cap.this");
            LLVMValueRef alloc = LLVMBuildAlloca(g->builder, i8ptr, "this.slot");
            LLVMBuildStore(g->builder, v, alloc);
            g->current_this = alloc;
        }
    }
    for (int k = 0; k < pc; k++) {
        zan_ast_node_t *param = expr->lambda.params.items[k];
        LLVMTypeRef lt = param_types[k];
        LLVMValueRef pv = LLVMGetParam(lambda_fn,
            (unsigned)(is_closure ? k + 1 : k));
        LLVMValueRef alloc = LLVMBuildAlloca(g->builder, lt, "lp");
        zan_store_fit(g, pv, alloc);
        zan_type_t *pt = ptypes[k] ? ptypes[k] : g->binder->type_int;
        local_add(&lambda_locals, param->param.name, alloc, pt);
        box_captured_parameter(g, &lambda_locals, param, pt, lt, pv,
                               expr->lambda.body);
    }

    if (expr->lambda.body && expr->lambda.body->kind == AST_BLOCK) {
        emit_stmt(g, expr->lambda.body, &lambda_locals);
        if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder))) {
            if (ret_void) LLVMBuildRetVoid(g->builder);
            else LLVMBuildRet(g->builder, LLVMConstNull(ret_type));
        }
    } else if (expr->lambda.body) {
        zan_type_t *bty = infer_expr_type(g, expr->lambda.body, &lambda_locals);
        check_implicit_narrowing(g, rett, bty, expr->lambda.body, "return");
        /* 委托返回类型与 lambda 体类型必须兼容：没有装箱设施时，返回一个
         * 值类型给引用类型返回位的委托，运行期只会造出野指针。 */
        check_no_boxing_conversion(g, rett, bty, expr->lambda.body,
                                   "lambda return");
        LLVMValueRef result = emit_expr(g, expr->lambda.body, &lambda_locals);
        if (ret_void) {
            LLVMBuildRetVoid(g->builder);
        } else if (rett) {
            /* 表达式主体 Lambda 返回托管引用时按规则保留引用计数 */
            if (is_rc_managed_type(rett) &&
                !expr_yields_owned_rc_value(g, expr->lambda.body, &lambda_locals)) {
                emit_rc_retain_for_type(g, rett, result);
            }
            /* 表达式体 Lambda 返回结构体：表达式求值得到的是结构体对象的
             * 地址，与「按值返回」的签名不符（LLVM 校验失败：Function return
             * type does not match operand type of return inst）。这里按值取出。
             * 可空聚合体（zan.nullable.*）不是这种形态，跳过。 */
            if (LLVMGetTypeKind(ret_type) == LLVMStructTypeKind &&
                !llvm_is_nullable(ret_type)) {
                result = coerce_struct_value_arg(g, result, ret_type);
            }
            result = emit_boundary_coerce(g, result, ret_type);
            LLVMBuildRet(g->builder, result);
        } else {
            /* 未传期望类型的 lambda（作为实参经 emit_expr 发射，expected=NULL）
             * 返回契约仍然是「向调用方移交 +1 强引用」：调用点按拥有临时对待
             * 委托返回值（用完即释放），这里不补 retain 的话，共享捕获的
             * 字符串/集合会被过度释放——引用计数打穿到不朽标记 -1 掩蔽成
             * 静默腐败，后续堆块复用后读侧崩溃（for_loopvar_capture 的
             * 0xC0000005 teardown 即此因）。ret_type 已知时上面的分支已覆盖。 */
            if (is_rc_managed_type(bty) &&
                !expr_yields_owned_rc_value(g, expr->lambda.body, &lambda_locals)) {
                emit_rc_retain_for_type(g, bty, result);
            }
            result = coerce_to_i64(g, result);
            LLVMBuildRet(g->builder, result);
        }
    } else {
        if (ret_void) LLVMBuildRetVoid(g->builder);
        else LLVMBuildRet(g->builder, LLVMConstNull(ret_type));
    }

    g->current_fn = saved_fn;
    g->current_fn_is_main = saved_fn_is_main;
    g->current_fn_ret_type = saved_fn_ret;
    g->current_fn_zan_ret_type = saved_fn_zan_ret;
    g->throw_locals_base = saved_throw_base;
    g->catch_cleanup_count = saved_catch_cc;
    g->throw_catch_base = saved_throw_cb;
    g->finally_count = saved_fin_c;
    g->finally_loop_base = saved_fin_lb;
    g->pending = saved_pending;
    g->break_target = saved_break;
    g->continue_target = saved_continue;
    g->loop_locals_base = saved_loop_base;
    g->break_locals_base = saved_break_base;
    g->loop_catch_base = saved_loop_cbase;
    if (saved_fin_c && saved_finallys) {
        memcpy(g->finallys, saved_finallys,
               sizeof(g->finallys[0]) * (size_t)saved_fin_c);
    }
    if (saved_catch_cc && saved_catches) {
        memcpy(g->catch_cleanups, saved_catches,
               sizeof(g->catch_cleanups[0]) * (size_t)saved_catch_cc);
    }
    if (saved_lpad_entries) {
        memcpy(g->lpad_stack, saved_lpad_entries,
               sizeof(g->lpad_stack[0]) * (size_t)saved_lpad_depth);
    }
    g->lpad_depth = saved_lpad_depth;
    g->eh_armed_count = saved_eh_c;
    g->eh_armed_base = saved_eh_b;
    g->eh_armed_loop_base = saved_eh_lb;
    g->current_this = saved_this;
    g->current_async_frame = saved_async_frame;
    g->current_fn_body = saved_fn_body;
    g->lambda_in_async_method = saved_lambda_in_async;
    g->lambda_depth--;
    LLVMPositionBuilderAtEnd(g->builder, saved_bb);
    /* 返回外层函数上下文：恢复外层调试位置 */
    if (g->emit_debug) {
        LLVMSetCurrentDebugLocation2(g->builder, saved_dl);
        g->di_cur_line = saved_di_line;
        g->di_cur_file = saved_di_file;
        g->di_last_loc = saved_dl;
        g->di_last_loc_fn = saved_fn;
    }
    free(param_types);
    free(ptypes);

    if (!is_closure) {
        cap_scan_free(&cs);
        return lambda_fn;
    }

    LLVMValueRef self = has_this
        ? LLVMBuildLoad2(g->builder, LLVMGetAllocatedType(saved_this),
                         saved_this, "cap.self")
        : NULL;
    LLVMValueRef clo = emit_closure_record(g, expr->loc, lname, rec_ty,
                                           lambda_fn, NULL, cs.caps, capc,
                                           has_this, self, true);
    cap_scan_free(&cs);
    return clo;
}
zan_type_t *method_param_type(zan_irgen_t *g, zan_symbol_t *msym, int idx) {
    if (!msym || !msym->decl || msym->decl->kind != AST_METHOD_DECL) return NULL;
    zan_ast_list_t *params = &msym->decl->method_decl.params;
    if (idx < 0 || idx >= params->count) return NULL;
    zan_ast_node_t *p = params->items[idx];
    if (!p || !p->param.type) return NULL;
    /* 优先使用绑定器在声明作用域内绑定的形参符号 */
    for (int i = 0; i < msym->member_count; i++) {
        zan_symbol_t *ps = msym->members[i];
        if (ps && ps->kind == SYM_PARAM && ps->decl == p) return ps->type;
    }
    return zan_binder_resolve_type(g->binder, p->param.type);
}
