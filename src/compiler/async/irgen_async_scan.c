/* FRAGMENT（拼接片段，非独立 TU）：由 irgen_async.c composite dispatcher 拼入。
 * 域：协程帧布局扫描——局部角色归类、存储槽登记、await 转移计数、
 * 类型驻帧判定。
 * 文件级 static（async_scan_expr、async_scan_add_local 等与
 * async_type_is_scalar、async_type_is_frame_resident、async_synth_name）
 * 为本片段私有。 */
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

/* 为局部变量分配帧内槽位，确保其在挂起点跨调用存活 */
static bool async_type_is_frame_resident(LLVMTypeRef t) {
    return async_type_is_scalar(t) || LLVMGetTypeKind(t) == LLVMPointerTypeKind ||
           LLVMGetTypeKind(t) == LLVMStructTypeKind;
}

static void async_scan_expr(async_scan_t *s, zan_ast_node_t *e);
void async_scan_stmt(async_scan_t *s, zan_ast_node_t *st);

static int async_scan_add_local_role(async_scan_t *s, zan_istr_t name, LLVMTypeRef llvm,
                                      zan_type_t *zt, zan_ast_node_t *decl, int role) {
    for (int i = 0; i < s->local_count; i++) {
        if (decl) {
            /* 按声明 AST 节点索引：同名遮蔽变量分配独立帧槽位 */
            if (s->locals[i].decl == decl && s->locals[i].role == role) return i;
        } else if (s->locals[i].name.len == name.len &&
                   memcmp(s->locals[i].name.str, name.str, name.len) == 0) {
            return i;
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
    bool boxed = decl && decl->kind == AST_VAR_DECL && zt &&
        zt->kind != TYPE_OBJECT && zt->kind != TYPE_TYPE_PARAM &&
        (async_type_is_scalar(llvm) || LLVMGetTypeKind(llvm) == LLVMStructTypeKind ||
         (is_rc_managed_type(zt) &&
          LLVMGetTypeKind(llvm) == LLVMPointerTypeKind)) &&
        local_is_lambda_captured(s->g, s->scope, s->body, decl);
    s->locals[s->local_count].name = name;
    s->locals[s->local_count].llvm = boxed
        ? LLVMPointerType(LLVMInt8TypeInContext(s->g->ctx), 0) : llvm;
    s->locals[s->local_count].ztype = zt;
    s->locals[s->local_count].frame_index = -1;
    s->locals[s->local_count].no_arc = false;
    s->locals[s->local_count].boxed = boxed;
    s->locals[s->local_count].decl = decl;
    s->locals[s->local_count].role = role;
    return s->local_count++;
}

static int async_scan_add_local(async_scan_t *s, zan_istr_t name, LLVMTypeRef llvm,
                                zan_type_t *zt, zan_ast_node_t *decl) {
    return async_scan_add_local_role(s, name, llvm, zt, decl, ASYNC_LOCAL_VALUE);
}

/* 分配以 $ 开头的编译器内部帧临时槽，避免与用户变量冲突 */
static zan_istr_t async_synth_name(zan_irgen_t *g, const char *prefix, int n) {
    char buf[32];
    int len = snprintf(buf, sizeof(buf), "$%s%d", prefix, n);
    char *p = (char *)zan_arena_alloc(g->arena, (size_t)len + 1);
    memcpy(p, buf, (size_t)len + 1);
    zan_istr_t s = { p, (uint32_t)len };
    return s;
}

static void async_scan_add_storage_role(async_scan_t *s, zan_istr_t name,
                                        LLVMTypeRef llvm, zan_ast_node_t *decl,
                                        int role) {
    int index = async_scan_add_local_role(s, name, llvm, NULL, decl, role);
    s->locals[index].no_arc = true;
}

static void async_scan_add_storage_local(async_scan_t *s, zan_istr_t name,
                                         LLVMTypeRef llvm, zan_ast_node_t *decl) {
    async_scan_add_storage_role(s, name, llvm, decl, ASYNC_LOCAL_VALUE);
}

static void async_scan_note_type(async_scan_t *s, zan_istr_t name, zan_type_t *t) {
    if (s->scope && t) local_add(s->scope, name, NULL, t);
}

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
        for (int i = 0; i < e->new_expr.arg_inits.count; i++)
            async_scan_expr(s, e->new_expr.arg_inits.items[i]);
        break;
    case AST_COLL_INIT:
        for (int i = 0; i < e->coll_init.items.count; i++)
            async_scan_expr(s, e->coll_init.items.items[i]);
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

/* 统计提前离开 try 块的跳转语句数（用于估算内联 finally 体数量） */
int async_count_transfers(zan_ast_node_t *st) {
    if (!st) return 0;
    switch (st->kind) {
    case AST_RETURN_STMT:
    case AST_BREAK_STMT:
    case AST_CONTINUE_STMT:
    case AST_THROW_STMT:
        return 1;
    case AST_BLOCK: {
        int n = 0;
        for (int i = 0; i < st->block.stmts.count; i++)
            n += async_count_transfers(st->block.stmts.items[i]);
        return n;
    }
    case AST_IF_STMT:
        return async_count_transfers(st->if_stmt.then_body) +
               async_count_transfers(st->if_stmt.else_body);
    case AST_WHILE_STMT:
    case AST_DO_WHILE_STMT:
        return async_count_transfers(st->while_stmt.body);
    case AST_FOR_STMT:
        return async_count_transfers(st->for_stmt.body);
    case AST_FOREACH_STMT:
        return async_count_transfers(st->foreach_stmt.body);
    case AST_LOCK_STMT:
        return async_count_transfers(st->lock_stmt.body);
    case AST_CHECKED_STMT:
        return async_count_transfers(st->checked_stmt.body);
    case AST_SWITCH_STMT: {
        int n = 0;
        for (int i = 0; i < st->switch_stmt.cases.count; i++)
            n += async_count_transfers(st->switch_stmt.cases.items[i]->switch_case.body);
        return n;
    }
    case AST_TRY_STMT: {
        int n = async_count_transfers(st->try_stmt.try_body) +
                async_count_transfers(st->try_stmt.finally_body);
        for (int i = 0; i < st->try_stmt.catches.count; i++)
            n += async_count_transfers(st->try_stmt.catches.items[i]->catch_clause.body);
        return n;
    }
    default:
        return 0;
    }
}

/* foreach 迭代协议接口判断（GetEnumerator / MoveNext / Current）。
 * 枚举器返回类型允许具体类，也允许接口；集合本身也允许是接口
 * （A381 lazy iterators：IEnumerable<T> 型集合/方法调用结果直接 foreach）。 */
zan_type_t *foreach_proto_enum_type(zan_irgen_t *g, zan_type_t *ct) {
    if (!ct || !ct->sym ||
        (ct->kind != TYPE_CLASS && ct->kind != TYPE_INTERFACE)) return NULL;
    zan_istr_t gi = { (char *)"GetEnumerator", 13 };
    zan_symbol_t *gm = ct->kind == TYPE_INTERFACE
        ? resolve_iface_overload(ct->sym, gi, 0, 0)
        : resolve_overload(ct->sym, gi, 0, 0);
    zan_type_t *et = NULL;
    if (gm && gm->decl && gm->decl->kind == AST_METHOD_DECL &&
        gm->decl->method_decl.return_type)
        et = resolve_type_ctx(g, gm->decl->method_decl.return_type);
    if (!et) return NULL;
    if (ct) et = subst_type_param_deep(g, et, ct);
    if ((et->kind != TYPE_CLASS && et->kind != TYPE_INTERFACE) || !et->sym)
        return NULL;
    zan_istr_t ni = { (char *)"MoveNext", 8 };
    zan_istr_t ci = { (char *)"Current", 7 };
    zan_symbol_t *next_m = et->kind == TYPE_INTERFACE
        ? resolve_iface_overload(et->sym, ni, 0, 0)
        : resolve_overload(et->sym, ni, 0, 0);
    if (!next_m) return NULL;
    zan_symbol_t *cur_m = et->kind == TYPE_INTERFACE
        ? resolve_iface_overload(et->sym, ci, 0, 0)
        : resolve_overload(et->sym, ci, 0, 0);
    if (cur_m) return et;
    for (int i = 0; i < et->sym->member_count; i++) {
        zan_symbol_t *m = et->sym->members[i];
        if (m && m->kind == SYM_PROPERTY && member_name_is(m, ci) &&
            property_getter_sym(g, m))
            return et;
    }
    return NULL;
}
zan_type_t *foreach_proto_current_type(zan_irgen_t *g, zan_type_t *ct) {
    zan_type_t *et = foreach_proto_enum_type(g, ct);
    if (!et) return NULL;
    zan_istr_t ci = { (char *)"Current", 7 };
    zan_symbol_t *cm = et->kind == TYPE_INTERFACE
        ? resolve_iface_overload(et->sym, ci, 0, 0)
        : resolve_overload(et->sym, ci, 0, 0);
    if (cm && cm->decl && cm->decl->kind == AST_METHOD_DECL &&
        cm->decl->method_decl.return_type) {
        zan_type_t *cur = resolve_type_ctx(g, cm->decl->method_decl.return_type);
        if (cur && et) cur = subst_type_param_deep(g, cur, et);
        if (cur && ct) cur = subst_type_param_deep(g, cur, ct);
        return cur;
    }
    for (int i = 0; i < et->sym->member_count; i++) {
        zan_symbol_t *m = et->sym->members[i];
        if (m && m->kind == SYM_PROPERTY && member_name_is(m, ci)) {
            zan_symbol_t *getter = property_getter_sym(g, m);
            if (getter && getter->decl && getter->decl->kind == AST_METHOD_DECL &&
                getter->decl->method_decl.return_type) {
                zan_type_t *cur = resolve_type_ctx(g,
                    getter->decl->method_decl.return_type);
                if (cur && et) cur = subst_type_param_deep(g, cur, et);
                if (cur && ct) cur = subst_type_param_deep(g, cur, ct);
                return cur;
            }
        }
    }
    return NULL;
}
void async_scan_stmt(async_scan_t *s, zan_ast_node_t *st) {
    if (!st) return;
    switch (st->kind) {
    case AST_BLOCK:
        for (int i = 0; i < st->block.stmts.count; i++)
            async_scan_stmt(s, st->block.stmts.items[i]);
        break;
    case AST_VAR_DECL: {
        /* 推断 var 声明变量类型并登记至帧槽位 */
        zan_type_t *t = st->var_decl.type
            ? resolve_type_ctx(s->g, st->var_decl.type)
            : NULL;
        if ((!t || t->kind == TYPE_ERROR) && st->var_decl.initializer)
            t = infer_expr_type(s->g, st->var_decl.initializer, s->scope);
        if (t && t->kind != TYPE_ERROR) {
            LLVMTypeRef lt = map_type(s->g, t);
            if (async_type_is_frame_resident(lt))
                async_scan_add_local(s, st->var_decl.name, lt, t, st);
            async_scan_note_type(s, st->var_decl.name, t);
        }
        async_scan_expr(s, st->var_decl.initializer);
        break;
    }
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
        async_scan_expr(s, st->while_stmt.cond);
        async_scan_stmt(s, st->while_stmt.body);
        break;
    case AST_DO_WHILE_STMT:
        if (st->while_stmt.cond && st->while_stmt.cond->kind == AST_BLOCK)
            async_scan_stmt(s, st->while_stmt.cond);
        else
            async_scan_expr(s, st->while_stmt.cond);
        async_scan_stmt(s, st->while_stmt.body);
        break;
    case AST_FOR_STMT:
        async_scan_stmt(s, st->for_stmt.init);
        async_scan_expr(s, st->for_stmt.cond);
        if (st->for_stmt.step && st->for_stmt.step->kind == AST_BLOCK)
            async_scan_stmt(s, st->for_stmt.step);
        else
            async_scan_expr(s, st->for_stmt.step);
        async_scan_stmt(s, st->for_stmt.body);
        break;
    case AST_FOREACH_STMT: {
        async_scan_expr(s, st->foreach_stmt.collection);
        /* foreach 循环变量与迭代状态跨挂起点分配帧槽位 */
        int fe_id = s->foreach_next++;
        zan_type_t *et = st->foreach_stmt.var_type
            ? resolve_type_ctx(s->g, st->foreach_stmt.var_type)
            : NULL;
        if (!et || et->kind == TYPE_ERROR)
            et = foreach_proto_current_type(s->g,
                infer_expr_type(s->g, st->foreach_stmt.collection, s->scope));
        if (!et || et->kind == TYPE_ERROR)
            et = container_elem_type(
                infer_expr_type(s->g, st->foreach_stmt.collection, s->scope));
        if (et && et->kind != TYPE_ERROR) {
            LLVMTypeRef lt = map_type(s->g, et);
            if (async_type_is_frame_resident(lt))
                async_scan_add_storage_local(s, st->foreach_stmt.var_name, lt, st);
            async_scan_note_type(s, st->foreach_stmt.var_name, et);
        }
        async_scan_add_storage_role(s, async_synth_name(s->g, "fe.i", fe_id),
            LLVMInt64TypeInContext(s->g->ctx), st, ASYNC_FOREACH_INDEX);
        async_scan_add_storage_role(s, async_synth_name(s->g, "fe.c", fe_id),
            LLVMPointerType(LLVMInt8TypeInContext(s->g->ctx), 0), st,
            ASYNC_FOREACH_COLLECTION);
        async_scan_add_storage_role(s, async_synth_name(s->g, "fe.s", fe_id),
            LLVMInt64TypeInContext(s->g->ctx), st, ASYNC_FOREACH_SNAP);
        if (foreach_proto_enum_type(s->g,
                infer_expr_type(s->g, st->foreach_stmt.collection, s->scope)))
            /* 枚举器对象在整个循环期间驻留帧槽位 */
            async_scan_add_storage_role(s,
                async_synth_name(s->g, "fe.e", fe_id),
                LLVMPointerType(LLVMInt8TypeInContext(s->g->ctx), 0), st,
                ASYNC_FOREACH_ENUMERATOR);
        async_scan_stmt(s, st->foreach_stmt.body);
        break;
    }
    case AST_THROW_STMT:
        async_scan_expr(s, st->throw_stmt.value);
        break;
    case AST_LOCK_STMT:
        async_scan_expr(s, st->lock_stmt.expr);
        s->fin_depth++;
        if (s->fin_depth > s->fin_depth_max) s->fin_depth_max = s->fin_depth;
        async_scan_stmt(s, st->lock_stmt.body);
        s->fin_depth--;
        break;
    case AST_CHECKED_STMT:
        async_scan_stmt(s, st->checked_stmt.body);
        break;
    case AST_TRY_STMT:
        s->try_count++;
        if (st->try_stmt.finally_body) {
            async_scan_add_storage_local(s,
                async_synth_name(s->g, "fin.cont", s->try_count),
                LLVMInt32TypeInContext(s->g->ctx), st);
            s->fin_depth++;
            if (s->fin_depth > s->fin_depth_max) s->fin_depth_max = s->fin_depth;
        }
        async_scan_stmt(s, st->try_stmt.try_body);
        for (int i = 0; i < st->try_stmt.catches.count; i++) {
            zan_ast_node_t *cc = st->try_stmt.catches.items[i];
            /* catch 异常绑定变量跨 await 驻留帧槽位（借用所有权） */
            if (cc->catch_clause.var_name.len > 0) {
                async_scan_add_storage_local(s, cc->catch_clause.var_name,
                    LLVMPointerType(LLVMInt8TypeInContext(s->g->ctx), 0), cc);
                if (cc->catch_clause.type)
                    async_scan_note_type(s, cc->catch_clause.var_name,
                        resolve_type_ctx(s->g, cc->catch_clause.type));
            }
            async_scan_stmt(s, cc->catch_clause.body);
        }
        /* 扫描 finally 块中每个退出分支对应副本的 await 槽位需求 */
        if (st->try_stmt.finally_body) {
            int copies = 2 + async_count_transfers(st->try_stmt.try_body);
            for (int i = 0; i < st->try_stmt.catches.count; i++)
                copies += async_count_transfers(
                    st->try_stmt.catches.items[i]->catch_clause.body);
            for (int c = 0; c < copies; c++)
                async_scan_stmt(s, st->try_stmt.finally_body);
        }
        if (st->try_stmt.finally_body) s->fin_depth--;
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
