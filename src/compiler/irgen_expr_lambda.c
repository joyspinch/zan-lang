/* Lambda 表达式变量捕获与闭包环境生成 */
typedef struct {
    zan_istr_t   name;
    LLVMValueRef slot;   
    zan_type_t  *type;
    LLVMTypeRef  llvm;
    /* 外层局部变量被闭包修改时装箱为共享 cell */
    int          boxed;
    /* for 循环迭代变量闭包捕获按轮独立装箱 */
    int          per_iter;
} lambda_capture_t;

typedef struct {
    zan_irgen_t     *g;
    local_scope_t   *outer;
    lambda_capture_t *caps;
    int              count;
    int              cap;
    int              needs_this;
    zan_istr_t      *shadow;
    int              shadow_count;
    int              shadow_cap;
    int              overflow;
    /* 写入扫描：分析局部变量是否存在写操作以决定是否装箱 */
    zan_istr_t       want_write;
    int              lam_depth;
    int              found_write;
    int              write_any;
    /* 共享捕获 cell 探测：识别被闭包写入的局部变量并提升为堆分配 cell */
    zan_istr_t       want_capture;
    zan_ast_node_t  *capture_decl;
    int              capture_active;
    int              capture_shadow_depth;
    int              found_capture;
} capture_scan_t;

static int istr_eq_c(zan_istr_t a, zan_istr_t b) {
    return a.len == b.len && a.len > 0 &&
           memcmp(a.str, b.str, (size_t)a.len) == 0;
}

static void cap_shadow(capture_scan_t *cs, zan_istr_t name) {
    if (!name.len) return;
    for (int i = 0; i < cs->shadow_count; i++)
        if (istr_eq_c(cs->shadow[i], name)) return;
    if (!ZAN_TAB_ENSURE(cs->shadow, cs->shadow_count, cs->shadow_cap, 64)) {
        cs->overflow = 1;
        return;
    }
    cs->shadow[cs->shadow_count++] = name;
}

static void cap_scan_free(capture_scan_t *cs) {
    free(cs->caps);
    cs->caps = NULL;
    cs->count = cs->cap = 0;
    free(cs->shadow);
    cs->shadow = NULL;
    cs->shadow_count = cs->shadow_cap = 0;
}

static int cap_is_shadowed(capture_scan_t *cs, zan_istr_t name) {
    for (int i = 0; i < cs->shadow_count; i++)
        if (istr_eq_c(cs->shadow[i], name)) return 1;
    return 0;
}

static int name_is_instance_member(zan_irgen_t *g, zan_istr_t name) {
    zan_symbol_t *ts = g->current_type_sym;
    if (!ts) return 0;
    for (zan_symbol_t *s = ts; s; s = (s->type && s->type->base_type)
                                        ? s->type->base_type->sym : NULL) {
        for (int i = 0; i < s->member_count; i++) {
            zan_symbol_t *m = s->members[i];
            if (!m || !istr_eq_c(m->name, name)) continue;
            if (m->kind != SYM_FIELD && m->kind != SYM_PROPERTY &&
                m->kind != SYM_METHOD) continue;
            return (m->modifiers & MOD_STATIC) ? 0 : 1;
        }
    }
    return 0;
}

static void cap_use(capture_scan_t *cs, zan_istr_t name) {
    if (!name.len || cap_is_shadowed(cs, name)) return;
    for (int i = 0; i < cs->count; i++)
        if (istr_eq_c(cs->caps[i].name, name)) return;
    local_var_t *lv = cs->outer ? local_find(cs->outer, name) : NULL;
    if (!lv) {
        if (name_is_instance_member(cs->g, name)) cs->needs_this = 1;
        return;
    }
    if (!ZAN_TAB_ENSURE(cs->caps, cs->count, cs->cap, 16)) {
        cs->overflow = 1;
        return;
    }
    lambda_capture_t *c = &cs->caps[cs->count++];
    c->name = name;
    c->type = lv->type;
    c->boxed = lv->box_cell ? 1 : 0;
    c->per_iter = lv->per_iteration;
    if (c->boxed) {
        c->slot = lv->box_cell;
        c->llvm = LLVMPointerType(LLVMInt8TypeInContext(cs->g->ctx), 0);
    } else {
        c->slot = lv->alloca;
        c->llvm = local_slot_type(cs->g, lv);
    }
}

static int node_writes_ident(zan_ast_node_t *n, zan_istr_t name) {
    zan_ast_node_t *t = NULL;
    if (n->kind == AST_ASSIGNMENT) t = n->binary.left;
    else if ((n->kind == AST_UNARY || n->kind == AST_POSTFIX_UNARY) &&
             (n->unary.op == TK_PLUS_PLUS || n->unary.op == TK_MINUS_MINUS))
        t = n->unary.operand;
    else if (n->kind == AST_REF_ARG) t = n->ref_arg.expr;
    return t && t->kind == AST_IDENTIFIER && istr_eq_c(t->ident.name, name);
}

/* 检查标识符是否作为写入目标（赋值、自增减、ref/out 实参） */
static zan_istr_t node_write_name(zan_ast_node_t *n) {
    zan_ast_node_t *t = NULL;
    if (n->kind == AST_ASSIGNMENT) t = n->binary.left;
    else if ((n->kind == AST_UNARY || n->kind == AST_POSTFIX_UNARY) &&
             (n->unary.op == TK_PLUS_PLUS || n->unary.op == TK_MINUS_MINUS))
        t = n->unary.operand;
    else if (n->kind == AST_REF_ARG) t = n->ref_arg.expr;
    return (t && t->kind == AST_IDENTIFIER) ? t->ident.name
                                            : (zan_istr_t){ NULL, 0 };
}

static void cap_scan(capture_scan_t *cs, zan_ast_node_t *n);

static void cap_scan_list(capture_scan_t *cs, zan_ast_list_t *l) {
    for (int i = 0; l && i < l->count; i++) cap_scan(cs, l->items[i]);
}

static void cap_scan(capture_scan_t *cs, zan_ast_node_t *n) {
    if (!n) return;
    if (cs->want_write.len) {
        if (cs->found_write) return;
        if ((cs->lam_depth > 0 || cs->write_any) &&
            node_writes_ident(n, cs->want_write)) {
            cs->found_write = 1;
            return;
        }
    }
    if (cs->want_capture.len && cs->found_capture) return;
    switch (n->kind) {
    case AST_IDENTIFIER:
        if (cs->want_capture.len && cs->capture_active && cs->lam_depth > 0 &&
            cs->capture_shadow_depth == 0 &&
            istr_eq_c(n->ident.name, cs->want_capture))
            cs->found_capture = 1;
        cap_use(cs, n->ident.name);
        return;
    case AST_THIS_EXPR:
    case AST_BASE_EXPR:       cs->needs_this = 1; return;
    case AST_BINARY:
    case AST_ASSIGNMENT:      cap_scan(cs, n->binary.left);
                              cap_scan(cs, n->binary.right); return;
    case AST_UNARY:
    case AST_POSTFIX_UNARY:   cap_scan(cs, n->unary.operand); return;
    case AST_CALL:            cap_scan(cs, n->call.callee);
                              cap_scan_list(cs, &n->call.args); return;
    case AST_MEMBER_ACCESS:   cap_scan(cs, n->member.object); return;
    case AST_INDEX:           cap_scan(cs, n->index.object);
                              cap_scan(cs, n->index.index); return;
    case AST_CONDITIONAL:     cap_scan(cs, n->conditional.cond);
                              cap_scan(cs, n->conditional.then_expr);
                              cap_scan(cs, n->conditional.else_expr); return;
    case AST_NEW_EXPR:        cap_scan_list(cs, &n->new_expr.args); return;
    case AST_COLL_INIT:       cap_scan_list(cs, &n->coll_init.items); return;
    case AST_CAST_EXPR:       cap_scan(cs, n->cast.expr); return;
    case AST_IS_EXPR:
    case AST_AS_EXPR:         cap_scan(cs, n->type_test.expr); return;
    case AST_AWAIT_EXPR:      cap_scan(cs, n->await_expr.expr); return;
    case AST_REF_ARG:         cap_scan(cs, n->ref_arg.expr); return;
    case AST_STRING_INTERP:   cap_scan_list(cs, &n->string_interp.parts); return;
    case AST_QUERY_EXPR: {
        int shadow_base = cs->shadow_count;
        int capture_shadow_base = cs->capture_shadow_depth;
        cap_scan(cs, n->query.source);
        cap_shadow(cs, n->query.var);
        if (cs->capture_active && istr_eq_c(n->query.var, cs->want_capture))
            cs->capture_shadow_depth++;
        for (int ci = 0; ci < n->query.clauses.count; ci++) {
            zan_ast_node_t *cl = n->query.clauses.items[ci];
            if (cl->kind == AST_QUERY_JOIN) {
                cap_scan(cs, cl->query_clause.source);
                cap_scan(cs, cl->query_clause.left_key);
                int join_shadow_base = cs->capture_shadow_depth;
                cap_shadow(cs, cl->query_clause.name);
                if (cs->capture_active &&
                    istr_eq_c(cl->query_clause.name, cs->want_capture))
                    cs->capture_shadow_depth++;
                cap_scan(cs, cl->query_clause.right_key);
                cs->capture_shadow_depth = join_shadow_base;
                if (cl->query_clause.into.len > 0) {
                    cap_shadow(cs, cl->query_clause.into);
                    if (cs->capture_active &&
                        istr_eq_c(cl->query_clause.into, cs->want_capture))
                        cs->capture_shadow_depth++;
                }
            } else {
                cap_scan(cs, cl->query_clause.expr);
                if (cl->kind == AST_QUERY_LET) {
                    cap_shadow(cs, cl->query_clause.name);
                    if (cs->capture_active &&
                        istr_eq_c(cl->query_clause.name, cs->want_capture))
                        cs->capture_shadow_depth++;
                }
            }
        }
        if (n->query.group_expr) {
            cap_scan(cs, n->query.group_expr);
            cap_scan(cs, n->query.group_key);
            if (n->query.group_into.len > 0) {
                cap_shadow(cs, n->query.group_into);
                if (cs->capture_active &&
                    istr_eq_c(n->query.group_into, cs->want_capture))
                    cs->capture_shadow_depth++;
            }
        }
        if (n->query.select) cap_scan(cs, n->query.select);
        cs->shadow_count = shadow_base;
        cs->capture_shadow_depth = capture_shadow_base;
        return;
    }
    case AST_SWITCH_EXPR:
        cap_scan(cs, n->switch_expr.expr);
        for (int ai = 0; ai < n->switch_expr.arms.count; ai++) {
            zan_ast_node_t *arm = n->switch_expr.arms.items[ai];
            int shadow_base = cs->shadow_count;
            int capture_shadow_base = cs->capture_shadow_depth;
            if (arm->switch_arm.pattern) cap_scan(cs, arm->switch_arm.pattern);
            if (arm->switch_arm.type_pattern && arm->switch_arm.var_name.len > 0) {
                cap_shadow(cs, arm->switch_arm.var_name);
                if (cs->capture_active &&
                    istr_eq_c(arm->switch_arm.var_name, cs->want_capture))
                    cs->capture_shadow_depth++;
            }
            if (arm->switch_arm.when_cond) cap_scan(cs, arm->switch_arm.when_cond);
            cap_scan(cs, arm->switch_arm.result);
            cs->shadow_count = shadow_base;
            cs->capture_shadow_depth = capture_shadow_base;
        }
        return;
    case AST_WITH_EXPR:
        cap_scan(cs, n->with_expr.expr);
        for (int ai = 0; ai < n->with_expr.assigns.count; ai++)
            cap_scan(cs, n->with_expr.assigns.items[ai]);
        return;
    case AST_LAMBDA: {
        /* Lambda 形参遮蔽与外层作用域符号恢复 */
        int shadow_base = cs->shadow_count;
        int capture_shadow_base = cs->capture_shadow_depth;
        for (int i = 0; i < n->lambda.params.count; i++) {
            zan_istr_t pn = n->lambda.params.items[i]->param.name;
            cap_shadow(cs, pn);
            if (cs->capture_active && istr_eq_c(pn, cs->want_capture))
                cs->capture_shadow_depth++;
        }
        cs->lam_depth++;
        cap_scan(cs, n->lambda.body);
        cs->lam_depth--;
        cs->shadow_count = shadow_base;
        cs->capture_shadow_depth = capture_shadow_base;
        return;
    }
    case AST_BLOCK: {
        int shadow_base = cs->shadow_count;
        int active_base = cs->capture_active;
        int capture_shadow_base = cs->capture_shadow_depth;
        cap_scan_list(cs, &n->block.stmts);
        cs->shadow_count = shadow_base;
        cs->capture_active = active_base;
        cs->capture_shadow_depth = capture_shadow_base;
        return;
    }
    case AST_VAR_DECL:
        cap_scan(cs, n->var_decl.initializer);
        if (cs->want_capture.len && n == cs->capture_decl) {
            cs->capture_active = 1;
            cs->capture_shadow_depth = 0;
        } else {
            cap_shadow(cs, n->var_decl.name);
            if (cs->capture_active &&
                istr_eq_c(n->var_decl.name, cs->want_capture))
                cs->capture_shadow_depth++;
        }
        return;
    case AST_EXPR_STMT:       cap_scan(cs, n->expr_stmt.expr); return;
    case AST_RETURN_STMT:     cap_scan(cs, n->ret.value); return;
    case AST_IF_STMT:         cap_scan(cs, n->if_stmt.cond);
                              cap_scan(cs, n->if_stmt.then_body);
                              cap_scan(cs, n->if_stmt.else_body); return;
    case AST_WHILE_STMT:
    case AST_DO_WHILE_STMT:   cap_scan(cs, n->while_stmt.cond);
                              cap_scan(cs, n->while_stmt.body); return;
    case AST_FOR_STMT: {
        int shadow_base = cs->shadow_count;
        int active_base = cs->capture_active;
        int capture_shadow_base = cs->capture_shadow_depth;
        cap_scan(cs, n->for_stmt.init);
        cap_scan(cs, n->for_stmt.cond);
        cap_scan(cs, n->for_stmt.step);
        cap_scan(cs, n->for_stmt.body);
        cs->shadow_count = shadow_base;
        cs->capture_active = active_base;
        cs->capture_shadow_depth = capture_shadow_base;
        return;
    }
    case AST_FOREACH_STMT: {
        int shadow_base = cs->shadow_count;
        int capture_shadow_base = cs->capture_shadow_depth;
        cap_scan(cs, n->foreach_stmt.collection);
        cap_shadow(cs, n->foreach_stmt.var_name);
        if (cs->capture_active &&
            istr_eq_c(n->foreach_stmt.var_name, cs->want_capture))
            cs->capture_shadow_depth++;
        cap_scan(cs, n->foreach_stmt.body);
        cs->shadow_count = shadow_base;
        cs->capture_shadow_depth = capture_shadow_base;
        return;
    }
    case AST_THROW_STMT:      cap_scan(cs, n->throw_stmt.value); return;
    case AST_TRY_STMT:        cap_scan(cs, n->try_stmt.try_body);
                              cap_scan_list(cs, &n->try_stmt.catches);
                              cap_scan(cs, n->try_stmt.finally_body); return;
    case AST_CATCH_CLAUSE: {
        int shadow_base = cs->shadow_count;
        int capture_shadow_base = cs->capture_shadow_depth;
        cap_shadow(cs, n->catch_clause.var_name);
        if (cs->capture_active &&
            istr_eq_c(n->catch_clause.var_name, cs->want_capture))
            cs->capture_shadow_depth++;
        cap_scan(cs, n->catch_clause.body);
        cs->shadow_count = shadow_base;
        cs->capture_shadow_depth = capture_shadow_base;
        return;
    }
    case AST_SWITCH_STMT:     cap_scan(cs, n->switch_stmt.expr);
                              cap_scan_list(cs, &n->switch_stmt.cases); return;
    case AST_SWITCH_CASE: {
        int shadow_base = cs->shadow_count;
        int active_base = cs->capture_active;
        int capture_shadow_base = cs->capture_shadow_depth;
        cap_scan(cs, n->switch_case.pattern);
        if (cs->want_capture.len && n == cs->capture_decl) {
            cs->capture_active = 1;
            cs->capture_shadow_depth = 0;
        } else if (n->switch_case.var_name.len > 0) {
            cap_shadow(cs, n->switch_case.var_name);
            if (cs->capture_active &&
                istr_eq_c(n->switch_case.var_name, cs->want_capture))
                cs->capture_shadow_depth++;
        }
        cap_scan(cs, n->switch_case.when_cond);
        cap_scan(cs, n->switch_case.body);
        cs->shadow_count = shadow_base;
        cs->capture_active = active_base;
        cs->capture_shadow_depth = capture_shadow_base;
        return;
    }
    case AST_LOCK_STMT:       cap_scan(cs, n->lock_stmt.expr);
                              cap_scan(cs, n->lock_stmt.body); return;
    case AST_CHECKED_STMT:    cap_scan(cs, n->checked_stmt.body); return;
    case AST_YIELD_STMT:      cap_scan(cs, n->yield_stmt.value); return;
    default: return;
    }
}

/* 闭包析构函数：释放捕获的环境变量与接收者强引用 */
static LLVMValueRef build_closure_dtor(zan_irgen_t *g, const char *lname,
                                       LLVMTypeRef rec_ty,
                                       lambda_capture_t *caps, int capc,
                                       int has_this, bool release_target) {
    LLVMContextRef c = g->ctx;
    LLVMTypeRef i64 = LLVMInt64TypeInContext(c);
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(c), 0);
    char name[160];
    snprintf(name, sizeof(name), "__zan_clo_dtor_%s", lname);
    LLVMValueRef existing = LLVMGetNamedFunction(g->mod, name);
    if (existing) return existing;
    LLVMValueRef fn = LLVMAddFunction(g->mod,
        name, LLVMFunctionType(LLVMVoidTypeInContext(c), &i8ptr, 1, 0));
    LLVMSetLinkage(fn, LLVMInternalLinkage);

    LLVMBuilderRef saved = g->builder;
    LLVMBuilderRef b = LLVMCreateBuilderInContext(c);
    g->builder = b;
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(c, fn, "entry");
    LLVMBasicBlockRef drop  = LLVMAppendBasicBlockInContext(c, fn, "drop");
    LLVMBasicBlockRef dec   = LLVMAppendBasicBlockInContext(c, fn, "dec");
    LLVMValueRef rec = LLVMGetParam(fn, 0);
    LLVMPositionBuilderAtEnd(b, entry);
    LLVMValueRef neg16 = LLVMConstInt(i64, (unsigned long long)ZAN_OBJ_RC_OFF, 1);
    LLVMValueRef rcp = LLVMBuildGEP2(b, LLVMInt8TypeInContext(c), rec, &neg16, 1, "rcp");
    LLVMValueRef rcip = LLVMBuildBitCast(b, rcp, LLVMPointerType(i64, 0), "rcip");
    /* 析构防重入：原子声明防止并发二次释放 */
    LLVMValueRef rc_old = LLVMBuildAtomicRMW(b, LLVMAtomicRMWBinOpSub, rcip,
        LLVMConstInt(i64, 1, 0), LLVMAtomicOrderingAcquireRelease, 0);
    LLVMBasicBlockRef last_bb = LLVMAppendBasicBlockInContext(c, fn, "last");
    LLVMBasicBlockRef freebb = LLVMAppendBasicBlockInContext(c, fn, "freebb");
    LLVMBasicBlockRef done_bb = LLVMAppendBasicBlockInContext(c, fn, "done");
    LLVMBuildCondBr(b, zan_icmp(b, LLVMIntSLE, rc_old, LLVMConstInt(i64, 0, 0), "over"),
                    dec, last_bb);
    LLVMPositionBuilderAtEnd(b, last_bb);
    LLVMBuildCondBr(b, zan_icmp(b, LLVMIntEQ, rc_old, LLVMConstInt(i64, 1, 0), "is1"),
                    drop, done_bb);
    LLVMPositionBuilderAtEnd(b, drop);
    /* 方法组绑定的接收者引用释放 */
    if (release_target) {
        LLVMValueRef p = LLVMBuildStructGEP2(b, rec_ty, rec, 2, "tgp");
        emit_arc_release_typed(g, NULL, LLVMBuildLoad2(b, i8ptr, p, "tgv"));
    }
    for (int i = 0; i < capc; i++) {
        int aggregate_rc = caps[i].type &&
            caps[i].type->kind == TYPE_STRUCT &&
            type_contains_collection_rc(g, caps[i].type, 0);
        if (!caps[i].boxed && !is_rc_managed_type(caps[i].type) &&
            !aggregate_rc) continue;
        LLVMValueRef p = LLVMBuildStructGEP2(g->builder, rec_ty, rec,
            (unsigned)(ZAN_CLOSURE_HDR_FIELDS + i), "cp");
        LLVMValueRef v = LLVMBuildLoad2(g->builder, caps[i].llvm, p, "cv");
        if (caps[i].boxed) emit_closure_record_release(g, v);
        else if (aggregate_rc)
            emit_collection_value_release(g, caps[i].type, v, 0);
        else emit_rc_release_for_type(g, caps[i].type, v);
    }
    if (has_this) {
        LLVMValueRef p = LLVMBuildStructGEP2(g->builder, rec_ty, rec,
            (unsigned)(ZAN_CLOSURE_HDR_FIELDS + capc), "tp");
        LLVMValueRef v = LLVMBuildLoad2(g->builder, i8ptr, p, "tv");
        emit_arc_release_typed(g, NULL, v);
    }
    LLVMBuildBr(g->builder, freebb);
    LLVMPositionBuilderAtEnd(g->builder, freebb);
    LLVMValueRef freefn = get_arc_free_decl(g);
    zan_call2(g->builder, LLVMGlobalGetValueType(freefn), freefn, &rec, 1, "");
    LLVMBuildBr(g->builder, done_bb);
    LLVMPositionBuilderAtEnd(g->builder, dec);
    zan_call2(g->builder, LLVMFunctionType(LLVMVoidTypeInContext(c), &i8ptr, 1, 0),
              g->rt_release, &rec, 1, "");
    LLVMBuildBr(g->builder, done_bb);
    LLVMPositionBuilderAtEnd(g->builder, done_bb);
    LLVMBuildRetVoid(g->builder);
    LLVMDisposeBuilder(b);
    g->builder = saved;
    return fn;
}

/* 在当前帧分配并初始化闭包记录 */
static LLVMValueRef emit_box_cell(zan_irgen_t *g, zan_loc_t loc,
                                  LLVMTypeRef payload, zan_type_t *vtype,
                                  LLVMValueRef init);
static LLVMValueRef box_value_ptr(zan_irgen_t *g, LLVMValueRef cell,
                                  LLVMTypeRef payload);
static LLVMValueRef emit_closure_record(zan_irgen_t *g, zan_loc_t loc,
                                        const char *lname, LLVMTypeRef rec_ty,
                                        LLVMValueRef fn_ptr, LLVMValueRef target,
                                        lambda_capture_t *caps, int capc,
                                        int has_this, LLVMValueRef self,
                                        bool retain_target) {
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMValueRef dtor = build_closure_dtor(g, lname, rec_ty, caps, capc,
                                           has_this, true);
    int site_idx = reserve_closure_site(g);
    LLVMValueRef site_name = LLVMConstNull(i8ptr);
    if (g->check_leaks) {
        char site_buf[600];
        snprintf(site_buf, sizeof(site_buf), "%s:%u:%u [closure]",
                 loc_site_file(g, loc), loc.line, loc.col);
        site_name = zan_irgen_intern_string(g, site_buf);
    }
    LLVMValueRef alloc_args[3] = {
        LLVMBuildPtrToInt(g->builder, LLVMSizeOf(rec_ty), i64, "clo.size"),
        arc_site_arg(g, site_idx), site_name };
    LLVMValueRef rec = zan_call2(g->builder,
        LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i64, i64, i8ptr }, 3, 0),
        g->rt_alloc, alloc_args, 3, "clo");
    LLVMBuildStore(g->builder,
        LLVMBuildBitCast(g->builder, fn_ptr, i8ptr, "clo.fn"),
        LLVMBuildStructGEP2(g->builder, rec_ty, rec, 0, "clo.fnp"));
    LLVMBuildStore(g->builder,
        LLVMBuildBitCast(g->builder, dtor, i8ptr, "clo.dt"),
        LLVMBuildStructGEP2(g->builder, rec_ty, rec, 1, "clo.dtp"));
    if (target) {
        if (LLVMTypeOf(target) != i8ptr)
            target = LLVMBuildBitCast(g->builder, target, i8ptr, "clo.tg8");
        if (retain_target) emit_arc_retain(g, target);
    }
    LLVMBuildStore(g->builder, target ? target : LLVMConstNull(i8ptr),
        LLVMBuildStructGEP2(g->builder, rec_ty, rec, 2, "clo.tgp"));
    for (int i = 0; i < capc; i++) {
        if (caps[i].boxed) {
            LLVMValueRef cell = caps[i].slot;
            if (caps[i].per_iter) {
                /* for 循环变量闭包按每次迭代独立捕获 */
                zan_type_t *pt = caps[i].type;
                LLVMTypeRef payload = map_type(g, pt);
                LLVMValueRef cur = LLVMBuildLoad2(g->builder, payload,
                    box_value_ptr(g, cell, payload), "loopvar.cap");
                if (pt->kind == TYPE_STRUCT &&
                    type_contains_collection_rc(g, pt, 0))
                    emit_collection_value_retain(g, pt, cur, 0);
                else if (is_rc_managed_type(pt) &&
                         LLVMGetTypeKind(payload) == LLVMPointerTypeKind)
                    emit_rc_retain_for_type(g, pt, cur);
                cell = emit_box_cell(g, loc, payload, pt, cur);
            } else {
                emit_arc_retain(g, cell);
            }
            LLVMBuildStore(g->builder, cell,
                LLVMBuildStructGEP2(g->builder, rec_ty, rec,
                                    (unsigned)(ZAN_CLOSURE_HDR_FIELDS + i), "cap.bp"));
            continue;
        }
        LLVMValueRef v = LLVMBuildLoad2(g->builder, caps[i].llvm, caps[i].slot, "cap.v");
        if (type_contains_collection_rc(g, caps[i].type, 0))
            emit_collection_value_retain(g, caps[i].type, v, 0);
        else
            emit_rc_retain_for_type(g, caps[i].type, v);
        LLVMBuildStore(g->builder, v,
            LLVMBuildStructGEP2(g->builder, rec_ty, rec,
                                (unsigned)(ZAN_CLOSURE_HDR_FIELDS + i), "cap.sp"));
    }
    if (has_this && self) {
        if (LLVMTypeOf(self) != i8ptr)
            self = LLVMBuildBitCast(g->builder, self, i8ptr, "cap.self8");
        emit_arc_retain(g, self);
        LLVMBuildStore(g->builder, self,
            LLVMBuildStructGEP2(g->builder, rec_ty, rec,
                                (unsigned)(ZAN_CLOSURE_HDR_FIELDS + capc), "cap.selfp"));
    }
    LLVMValueRef tagged = LLVMBuildOr(g->builder,
        LLVMBuildPtrToInt(g->builder, rec, i64, "clo.i"),
        LLVMConstInt(i64, ZAN_CLOSURE_TAG, 0), "clo.tag");
    return LLVMBuildIntToPtr(g->builder, tagged, i8ptr, "clo.v");
}

/* 装箱共享局部变量 (Boxed Locals) 支持 */
#define ZAN_BOX_VALUE_FIELD ZAN_CLOSURE_HDR_FIELDS

static LLVMTypeRef box_cell_type(zan_irgen_t *g, LLVMTypeRef payload) {
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef fields[ZAN_BOX_VALUE_FIELD + 1] = { i8ptr, i8ptr, i8ptr, payload };
    return LLVMStructTypeInContext(g->ctx, fields, ZAN_BOX_VALUE_FIELD + 1, 0);
}

/* 扫描方法体内是否存在对指定名称变量的赋值写入 */
static int body_writes_ident_scan(zan_irgen_t *g, zan_ast_node_t *body,
                                  zan_istr_t name);

/* 标识符字符串内容比对 */
static int memo_name_eq(zan_istr_t a, zan_istr_t b) {
    return a.len == b.len && a.len > 0 &&
           memcmp(a.str, b.str, (size_t)a.len) == 0;
}

static unsigned body_write_memo_hash(zan_ast_node_t *body, zan_istr_t name) {
    uint64_t h = 1469598103934665603ull;
    uintptr_t bp = (uintptr_t)body;
    for (unsigned i = 0; i < sizeof(bp); i++) {
        h ^= (unsigned char)(bp >> (i * 8));
        h *= 1099511628211ull;
    }
    for (int i = 0; i < name.len; i++) {
        h ^= (unsigned char)name.str[i];
        h *= 1099511628211ull;
    }
    return (unsigned)h;
}

static void body_write_collect(zan_irgen_t *g, zan_ast_node_t *n,
                               zan_ast_node_t *body, int lam_depth);

static void body_write_collect_list(zan_irgen_t *g, zan_ast_list_t *l,
                                    zan_ast_node_t *body, int lam_depth) {
    for (int i = 0; l && i < l->count; i++)
        body_write_collect(g, l->items[i], body, lam_depth);
}

static struct zan_body_write_entry *body_write_memo_slot(zan_irgen_t *g,
                                                         zan_ast_node_t *body,
                                                         zan_istr_t name) {
    if (g->body_write_memo_count * 2 >= g->body_write_memo_cap) {
        unsigned ncap = g->body_write_memo_cap ? g->body_write_memo_cap * 2 : 256;
        struct zan_body_write_entry *ns =
            calloc((size_t)ncap, sizeof(*ns));
        if (!ns) return NULL;
        /* 扩容并将存活条目拷贝至新符号哈希表 */
        for (unsigned i = 0; i < g->body_write_memo_cap; i++) {
            if (!g->body_write_memo[i].known) continue;
            unsigned mask = ncap - 1;
            unsigned j = body_write_memo_hash(g->body_write_memo[i].body,
                                              g->body_write_memo[i].name) & mask;
            while (ns[j].known) j = (j + 1) & mask;
            ns[j] = g->body_write_memo[i];
        }
        free(g->body_write_memo);
        g->body_write_memo = ns;
        g->body_write_memo_cap = ncap;
    }
    unsigned mask = g->body_write_memo_cap - 1;
    unsigned i = body_write_memo_hash(body, name) & mask;
    while (g->body_write_memo[i].known) {
        if (g->body_write_memo[i].body == body &&
            memo_name_eq(g->body_write_memo[i].name, name))
            return &g->body_write_memo[i];
        i = (i + 1) & mask;
    }
    return &g->body_write_memo[i];
}

/* 收集 AST 节点内所有被赋值的标识符至备忘表 */
static void body_write_collect(zan_irgen_t *g, zan_ast_node_t *n,
                               zan_ast_node_t *body, int lam_depth) {
    if (!n) return;
    if (lam_depth > 0 && n->kind == AST_IDENTIFIER) {
        struct zan_body_write_entry *e = body_write_memo_slot(g, body,
                                                              n->ident.name);
        if (e) {
            if (!e->known) {
                e->body = body;
                e->name = n->ident.name;
                e->known = 1;
                g->body_write_memo_count++;
            }
            e->lam_captured = 1;
        }
    }
    zan_istr_t w = node_write_name(n);
    if (w.str) {
        struct zan_body_write_entry *e = body_write_memo_slot(g, body, w);
        if (e) {
            if (!e->known) {
                e->body = body;
                e->name = w;
                e->written = 1;
                e->lam_written = (lam_depth > 0) ? 1 : 0;
                e->known = 1;
                g->body_write_memo_count++;
            } else if (lam_depth > 0) {
                e->lam_written = 1;
            }
        }
    }
    switch (n->kind) {
    case AST_LAMBDA: {
        /* 嵌套 Lambda 形参遮蔽外层变量：其写入不视为外层变量修改 */
        for (int i = 0; i < n->lambda.params.count; i++) {
            zan_istr_t pn = n->lambda.params.items[i]->param.name;
            struct zan_body_write_entry *e = body_write_memo_slot(g, body, pn);
            if (e && !e->known) {
                e->body = body;
                e->name = pn;
                e->written = 0;
                e->lam_written = 0;
                e->known = 1;
                g->body_write_memo_count++;
            }
        }
        body_write_collect(g, n->lambda.body, body, lam_depth + 1);
        return;
    }
    case AST_BINARY:
    case AST_ASSIGNMENT:      body_write_collect(g, n->binary.left, body, lam_depth);
                              body_write_collect(g, n->binary.right, body, lam_depth); return;
    case AST_UNARY:
    case AST_POSTFIX_UNARY:   body_write_collect(g, n->unary.operand, body, lam_depth); return;
    case AST_CALL:            body_write_collect(g, n->call.callee, body, lam_depth);
                              body_write_collect_list(g, &n->call.args, body, lam_depth); return;
    case AST_MEMBER_ACCESS:   body_write_collect(g, n->member.object, body, lam_depth); return;
    case AST_INDEX:           body_write_collect(g, n->index.object, body, lam_depth);
                              body_write_collect(g, n->index.index, body, lam_depth); return;
    case AST_CONDITIONAL:     body_write_collect(g, n->conditional.cond, body, lam_depth);
                              body_write_collect(g, n->conditional.then_expr, body, lam_depth);
                              body_write_collect(g, n->conditional.else_expr, body, lam_depth); return;
    case AST_NEW_EXPR:        body_write_collect_list(g, &n->new_expr.args, body, lam_depth); return;
    case AST_COLL_INIT:       body_write_collect_list(g, &n->coll_init.items, body, lam_depth); return;
    case AST_CAST_EXPR:       body_write_collect(g, n->cast.expr, body, lam_depth); return;
    case AST_IS_EXPR:
    case AST_AS_EXPR:         body_write_collect(g, n->type_test.expr, body, lam_depth); return;
    case AST_AWAIT_EXPR:      body_write_collect(g, n->await_expr.expr, body, lam_depth); return;
    case AST_REF_ARG:         body_write_collect(g, n->ref_arg.expr, body, lam_depth); return;
    case AST_STRING_INTERP:   body_write_collect_list(g, &n->string_interp.parts, body, lam_depth); return;
    case AST_QUERY_EXPR:      body_write_collect(g, n->query.source, body, lam_depth);
                              body_write_collect(g, n->query.group_expr, body, lam_depth);
                              body_write_collect(g, n->query.group_key, body, lam_depth);
                              body_write_collect(g, n->query.select, body, lam_depth);
                              body_write_collect_list(g, &n->query.clauses, body, lam_depth); return;
    case AST_QUERY_WHERE:     body_write_collect(g, n->query_clause.expr, body, lam_depth); return;
    case AST_QUERY_LET:       body_write_collect(g, n->query_clause.expr, body, lam_depth); return;
    case AST_QUERY_ORDERBY:   body_write_collect(g, n->query_clause.expr, body, lam_depth); return;
    case AST_QUERY_JOIN:      body_write_collect(g, n->query_clause.source, body, lam_depth);
                              body_write_collect(g, n->query_clause.left_key, body, lam_depth);
                              body_write_collect(g, n->query_clause.right_key, body, lam_depth); return;
    case AST_SWITCH_EXPR:     body_write_collect(g, n->switch_expr.expr, body, lam_depth);
                              body_write_collect_list(g, &n->switch_expr.arms, body, lam_depth); return;
    case AST_WITH_EXPR:       body_write_collect(g, n->with_expr.expr, body, lam_depth);
                              body_write_collect_list(g, &n->with_expr.assigns, body, lam_depth); return;
    case AST_SWITCH_ARM:      body_write_collect(g, n->switch_arm.pattern, body, lam_depth);
                              body_write_collect(g, n->switch_arm.when_cond, body, lam_depth);
                              body_write_collect(g, n->switch_arm.result, body, lam_depth); return;
    case AST_BLOCK:           body_write_collect_list(g, &n->block.stmts, body, lam_depth); return;
    case AST_VAR_DECL:        body_write_collect(g, n->var_decl.initializer, body, lam_depth);
                              {
                                  zan_istr_t dn = n->var_decl.name;
                                  if (dn.str) {
                                      struct zan_body_write_entry *e =
                                          body_write_memo_slot(g, body, dn);
                                      if (e && !e->known) {
                                          e->body = body;
                                          e->name = dn;
                                          e->written = 0;
                                          e->lam_written = 0;
                                          e->known = 1;
                                          g->body_write_memo_count++;
                                      }
                                  }
                              }
                              return;
    case AST_EXPR_STMT:       body_write_collect(g, n->expr_stmt.expr, body, lam_depth); return;
    case AST_RETURN_STMT:     body_write_collect(g, n->ret.value, body, lam_depth); return;
    case AST_IF_STMT:         body_write_collect(g, n->if_stmt.cond, body, lam_depth);
                              body_write_collect(g, n->if_stmt.then_body, body, lam_depth);
                              body_write_collect(g, n->if_stmt.else_body, body, lam_depth); return;
    case AST_WHILE_STMT:
    case AST_DO_WHILE_STMT:   body_write_collect(g, n->while_stmt.cond, body, lam_depth);
                              body_write_collect(g, n->while_stmt.body, body, lam_depth); return;
    case AST_FOR_STMT:        body_write_collect(g, n->for_stmt.init, body, lam_depth);
                              body_write_collect(g, n->for_stmt.cond, body, lam_depth);
                              body_write_collect(g, n->for_stmt.step, body, lam_depth);
                              body_write_collect(g, n->for_stmt.body, body, lam_depth); return;
    case AST_FOREACH_STMT:    body_write_collect(g, n->foreach_stmt.collection, body, lam_depth);
                              body_write_collect(g, n->foreach_stmt.body, body, lam_depth); return;
    case AST_THROW_STMT:      body_write_collect(g, n->throw_stmt.value, body, lam_depth); return;
    case AST_TRY_STMT:        body_write_collect(g, n->try_stmt.try_body, body, lam_depth);
                              body_write_collect_list(g, &n->try_stmt.catches, body, lam_depth);
                              body_write_collect(g, n->try_stmt.finally_body, body, lam_depth); return;
    case AST_CATCH_CLAUSE:    body_write_collect(g, n->catch_clause.body, body, lam_depth); return;
    case AST_SWITCH_STMT:     body_write_collect(g, n->switch_stmt.expr, body, lam_depth);
                              body_write_collect_list(g, &n->switch_stmt.cases, body, lam_depth); return;
    case AST_SWITCH_CASE:     body_write_collect(g, n->switch_case.pattern, body, lam_depth);
                              body_write_collect(g, n->switch_case.when_cond, body, lam_depth);
                              body_write_collect(g, n->switch_case.body, body, lam_depth); return;
    case AST_LOCK_STMT:       body_write_collect(g, n->lock_stmt.expr, body, lam_depth);
                              body_write_collect(g, n->lock_stmt.body, body, lam_depth); return;
    case AST_CHECKED_STMT:    body_write_collect(g, n->checked_stmt.body, body, lam_depth); return;
    case AST_YIELD_STMT:      body_write_collect(g, n->yield_stmt.value, body, lam_depth); return;
    default: return;
    }
}

/* 查询变量是否在方法体内被写入 */
static int body_writes_ident_memo(zan_irgen_t *g, zan_ast_node_t *body,
                                  zan_istr_t name) {
    if (!body || !name.len) return 0;
    if (!g->body_write_memo_cap) {
        g->body_write_memo = calloc(256, sizeof(*g->body_write_memo));
        if (!g->body_write_memo) return body_writes_ident_scan(g, body, name);
        g->body_write_memo_cap = 256;
        g->body_write_memo_count = 0;
    }
    if (g->body_write_scan_done != body) {
        body_write_collect(g, body, body, 0);
        g->body_write_scan_done = body;
    }
    unsigned mask = g->body_write_memo_cap - 1;
    unsigned i = body_write_memo_hash(body, name) & mask;
    while (g->body_write_memo[i].known) {
        if (g->body_write_memo[i].body == body &&
            memo_name_eq(g->body_write_memo[i].name, name))
            return g->body_write_memo[i].written;
        i = (i + 1) & mask;
    }
    return 0;
}

static int body_writes_ident_scan(zan_irgen_t *g, zan_ast_node_t *body,
                                  zan_istr_t name) {
    capture_scan_t cs;
    memset(&cs, 0, sizeof(cs));
    cs.g = g;
    cs.want_write = name;
    cs.write_any = 1;
    cap_scan(&cs, body);
    int found = cs.found_write;
    cap_scan_free(&cs);
    return found;
}

static int body_writes_ident(zan_irgen_t *g, zan_ast_node_t *body,
                             zan_istr_t name) {
    if (!body || !name.len) return 0;
    return body_writes_ident_memo(g, body, name);
}

/* 判断当前编译函数内的 Lambda 是否捕获了该声明变量 */
static int local_is_lambda_captured(zan_irgen_t *g, local_scope_t *locals,
                                    zan_ast_node_t *body,
                                    zan_ast_node_t *decl) {
    (void)locals;
    if (!body || !decl) return 0;
    zan_istr_t name;
    memset(&name, 0, sizeof(name));
    if (decl->kind == AST_PARAM)
        name = decl->param.name;
    else if (decl->kind == AST_VAR_DECL)
        name = decl->var_decl.name;
    else if (decl->kind == AST_SWITCH_CASE)
        name = decl->switch_case.var_name;
    else
        return 0;
    if (!name.len) return 0;
    body_writes_ident_memo(g, body, name);
    unsigned mask = g->body_write_memo_cap - 1;
    unsigned i = body_write_memo_hash(body, name) & mask;
    int candidate = 0;
    while (g->body_write_memo[i].known) {
        if (g->body_write_memo[i].body == body &&
            memo_name_eq(g->body_write_memo[i].name, name)) {
            candidate = g->body_write_memo[i].lam_captured;
            break;
        }
        i = (i + 1) & mask;
    }
    if (!candidate) return 0;

    capture_scan_t cs;
    memset(&cs, 0, sizeof(cs));
    cs.g = g;
    cs.want_capture = name;
    cs.capture_decl = decl;
    cs.capture_active = decl->kind == AST_PARAM ||
                          decl->kind == AST_SWITCH_CASE;
    cap_scan(&cs, body);
    int found = cs.found_capture;
    cap_scan_free(&cs);
    return found;
}

static LLVMValueRef emit_box_cell(zan_irgen_t *g, zan_loc_t loc,
                                  LLVMTypeRef payload, zan_type_t *vtype,
                                  LLVMValueRef init) {
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef rec_ty = box_cell_type(g, payload);
    static int box_id = 0;
    char bname[64];
    snprintf(bname, sizeof(bname), "box_%d",
             (int)__atomic_fetch_add(&box_id, 1, __ATOMIC_SEQ_CST));
    lambda_capture_t val = { .name = (zan_istr_t){ NULL, 0 }, .slot = NULL,
                             .type = vtype, .llvm = payload, .boxed = 0 };
    LLVMValueRef dtor = build_closure_dtor(g, bname, rec_ty, &val, 1, 0, true);
    int site_idx = reserve_closure_site(g);
    LLVMValueRef site_name = LLVMConstNull(i8ptr);
    if (g->check_leaks) {
        char site_buf[600];
        snprintf(site_buf, sizeof(site_buf), "%s:%u:%u [captured local]",
                 loc_site_file(g, loc), loc.line, loc.col);
        site_name = zan_irgen_intern_string(g, site_buf);
    }
    LLVMValueRef alloc_args[3] = {
        LLVMBuildPtrToInt(g->builder, LLVMSizeOf(rec_ty), i64, "box.size"),
        arc_site_arg(g, site_idx), site_name };
    LLVMValueRef cell = zan_call2(g->builder,
        LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i64, i64, i8ptr }, 3, 0),
        g->rt_alloc, alloc_args, 3, "box");
    LLVMBuildStore(g->builder, LLVMConstNull(i8ptr),
        LLVMBuildStructGEP2(g->builder, rec_ty, cell, 0, "box.fnp"));
    LLVMBuildStore(g->builder,
        LLVMBuildBitCast(g->builder, dtor, i8ptr, "box.dt"),
        LLVMBuildStructGEP2(g->builder, rec_ty, cell, 1, "box.dtp"));
    LLVMBuildStore(g->builder, LLVMConstNull(i8ptr),
        LLVMBuildStructGEP2(g->builder, rec_ty, cell, 2, "box.tgp"));
    LLVMValueRef vp = LLVMBuildStructGEP2(g->builder, rec_ty, cell,
                                          ZAN_BOX_VALUE_FIELD, "box.vp");
    LLVMBuildStore(g->builder, init ? init : LLVMConstNull(payload), vp);
    return cell;
}

/* 装箱局部变量存储槽：指针直达 cell 内部的值字段 */
static LLVMValueRef box_value_ptr(zan_irgen_t *g, LLVMValueRef cell,
                                  LLVMTypeRef payload) {
    return LLVMBuildStructGEP2(g->builder, box_cell_type(g, payload), cell,
                               ZAN_BOX_VALUE_FIELD, "box.v");
}

/* 作用域绑定接管共享 cell 所有权，异常展开时统一释放 */
static void own_box_cell(zan_irgen_t *g, local_var_t *v, LLVMValueRef cell) {
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMValueRef tagged_i = LLVMBuildOr(g->builder,
        LLVMBuildPtrToInt(g->builder, cell, i64, "box.i"),
        LLVMConstInt(i64, ZAN_CLOSURE_TAG, 0), "box.tag");
    LLVMValueRef tagged = LLVMBuildIntToPtr(g->builder, tagged_i, i8ptr,
                                            "box.owner.v");
    LLVMValueRef owner = emit_entry_alloca(g, i8ptr, "box.owner");
    LLVMBuildStore(g->builder, tagged, owner);
    v->box_cell = cell;
    v->box_owner_slot = owner;
    v->box_owned = 1;
    if (!g->current_async_frame) {
        emit_eh_tmp_push_slot(g, owner, ZAN_EH_SLOT_DLG);
        v->eh_slot = 1;
    }
}

/* 将按值传递的形参移动至闭包共享 cell */
static void box_captured_parameter(zan_irgen_t *g, local_scope_t *locals,
                                   zan_ast_node_t *param, zan_type_t *type,
                                   LLVMTypeRef payload, LLVMValueRef value,
                                   zan_ast_node_t *body) {
    if (!locals || locals->count == 0 || !param ||
        !local_is_lambda_captured(g, locals, body, param)) return;
    if (param->param.by_ref) {
        zan_diag_emit_code(g->diag, DIAG_ERROR, param->loc, 3043,
            "cannot capture a ref or out parameter in a lambda");
        return;
    }
    if (!type || type->kind == TYPE_OBJECT || type->kind == TYPE_TYPE_PARAM) {
        zan_diag_emit_code(g->diag, DIAG_ERROR, param->loc, 3044,
            "cannot capture parameter '%.*s' of type '%.*s': its runtime ownership is not representable yet",
            (int)param->param.name.len, param->param.name.str,
            type ? (int)type->name.len : 0,
            type && type->name.str ? type->name.str : "");
        return;
    }
    LLVMTypeKind k = LLVMGetTypeKind(payload);
    int rc = is_rc_managed_type(type) && k == LLVMPointerTypeKind;
    int aggregate_rc = type->kind == TYPE_STRUCT &&
        type_contains_collection_rc(g, type, 0);
    if (!rc && k != LLVMIntegerTypeKind && k != LLVMFloatTypeKind &&
        k != LLVMDoubleTypeKind && k != LLVMStructTypeKind) {
        zan_diag_emit_code(g->diag, DIAG_ERROR, param->loc, 3045,
            "cannot capture parameter '%.*s': unsupported value representation",
            (int)param->param.name.len, param->param.name.str);
        return;
    }
    if (aggregate_rc) emit_collection_value_retain(g, type, value, 0);
    else if (rc) emit_rc_retain_for_type(g, type, value);
    LLVMValueRef cell = emit_box_cell(g, param->loc, payload, type, value);
    local_var_t *v = &locals->vars[locals->count - 1];
    v->alloca = box_value_ptr(g, cell, payload);
    own_box_cell(g, v, cell);
    if (rc) v->arc_owned = 1;
    if (aggregate_rc) v->struct_rc = 1;
}

/* 实例方法组转委托：将接收者绑定入方法组记录 */
static LLVMValueRef emit_method_group_closure(zan_irgen_t *g, zan_symbol_t *msym,
                                              LLVMValueRef recv, zan_loc_t loc) {
    int fi = irgen_find_function(g, msym);
    if (fi < 0) return NULL;
    LLVMValueRef target = g->functions[fi].fn;
    LLVMTypeRef target_ty = g->functions[fi].fn_type;
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    bool is_static = (recv == NULL);
    unsigned np = LLVMCountParamTypes(target_ty);
    if (!is_static && np < 1) return NULL;
    LLVMTypeRef *tp = (LLVMTypeRef *)calloc((size_t)np, sizeof(LLVMTypeRef));
    LLVMGetParamTypes(target_ty, tp);
    LLVMTypeRef ret = LLVMGetReturnType(target_ty);

    /* 每个方法生成单例 thunk，支持委托判等与比较 */
    char lname[128];
    snprintf(lname, sizeof(lname), "mg_%s", LLVMGetValueName(target));

    LLVMTypeRef fields[ZAN_CLOSURE_HDR_FIELDS] = { i8ptr, i8ptr, i8ptr };
    char rname[128];
    snprintf(rname, sizeof(rname), "%s$clo", lname);
    LLVMTypeRef rec_ty = LLVMStructCreateNamed(g->ctx, rname);
    LLVMStructSetBody(rec_ty, fields, ZAN_CLOSURE_HDR_FIELDS, 0);

    LLVMTypeRef *thunk_params = (LLVMTypeRef *)calloc((size_t)np + 1, sizeof(LLVMTypeRef));
    unsigned tn;
    if (is_static) {
        thunk_params[0] = i8ptr;
        for (unsigned i = 0; i < np; i++) thunk_params[i + 1] = tp[i];
        tn = np + 1;
    } else {
        thunk_params[0] = i8ptr;
        for (unsigned i = 1; i < np; i++) thunk_params[i] = tp[i];
        tn = np;
    }
    LLVMTypeRef thunk_ty = LLVMFunctionType(ret, thunk_params, tn, 0);
    char tname[160];
    snprintf(tname, sizeof(tname), "__zan_%s", lname);
    LLVMValueRef thunk = LLVMGetNamedFunction(g->mod, tname);
    int thunk_is_new = thunk == NULL;
    if (thunk_is_new) {
        thunk = LLVMAddFunction(g->mod, tname, thunk_ty);
        LLVMSetLinkage(thunk, LLVMInternalLinkage);
    }

    LLVMBasicBlockRef saved_bb = LLVMGetInsertBlock(g->builder);
    if (!thunk_is_new) {
        free(thunk_params);
        free(tp);
        if (is_static) return build_static_mg_record(g, loc, lname, rec_ty, thunk);
        return emit_closure_record(g, loc, lname, rec_ty, thunk, recv,
                                   NULL, 0, 0, NULL, true);
    }
    LLVMPositionBuilderAtEnd(g->builder,
        LLVMAppendBasicBlockInContext(g->ctx, thunk, "entry"));
    LLVMValueRef rec = LLVMGetParam(thunk, 0);
    LLVMValueRef *cargs = (LLVMValueRef *)calloc((size_t)np + 1, sizeof(LLVMValueRef));
    unsigned ca = 0;
    LLVMValueRef self = NULL;
    if (!is_static) {
        LLVMValueRef sp = LLVMBuildStructGEP2(g->builder, rec_ty, rec, 2, "mg.selfp");
        self = LLVMBuildLoad2(g->builder, i8ptr, sp, "mg.self");
        cargs[ca++] = LLVMTypeOf(self) == tp[0]
            ? self : LLVMBuildBitCast(g->builder, self, tp[0], "mg.self.c");
        for (unsigned i = 1; i < tn; i++) cargs[ca++] = LLVMGetParam(thunk, i);
    } else {
        for (unsigned i = 1; i < tn; i++) cargs[ca++] = LLVMGetParam(thunk, i);
    }
    LLVMValueRef r = zan_call2(g->builder, target_ty, target, cargs, ca,
        LLVMGetTypeKind(ret) == LLVMVoidTypeKind ? "" : "mg.r");
    if (LLVMGetTypeKind(ret) == LLVMVoidTypeKind) LLVMBuildRetVoid(g->builder);
    else LLVMBuildRet(g->builder, r);
    free(cargs);
    free(thunk_params);
    free(tp);
    LLVMPositionBuilderAtEnd(g->builder, saved_bb);

    if (is_static) return build_static_mg_record(g, loc, lname, rec_ty, thunk);
    return emit_closure_record(g, loc, lname, rec_ty, thunk, recv, NULL, 0, 0, NULL, true);
}

/* 为静态方法组构建闭包记录 */
static LLVMValueRef build_static_mg_record(zan_irgen_t *g, zan_loc_t loc,
                                           const char *lname, LLVMTypeRef rec_ty,
                                           LLVMValueRef thunk) {
    build_closure_dtor(g, lname, rec_ty, NULL, 0, 0, false);
    return emit_closure_record(g, loc, lname, rec_ty, thunk, thunk,
                               NULL, 0, 0, NULL, false);
}

/* 合成双向数据绑定属性访问器 */
static LLVMValueRef emit_binding_acc_delegate(zan_irgen_t *g, LLVMValueRef acc,
                                              LLVMTypeRef acc_ty) {
    if (!target_is_wasm32(g)) return acc;
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef tp[2];
    unsigned np = LLVMCountParamTypes(acc_ty);
    if (np == 0 || np > 2) return acc;
    LLVMGetParamTypes(acc_ty, tp);
    LLVMTypeRef ret = LLVMGetReturnType(acc_ty);
    const char *accname = LLVMGetValueName(acc);
    char lname[512];
    snprintf(lname, sizeof(lname), "mg_%s", accname);
    char tname[560];
    snprintf(tname, sizeof(tname), "__zan_%s", lname);
    LLVMValueRef thunk = LLVMGetNamedFunction(g->mod, tname);
    if (!thunk) {
        LLVMTypeRef thunk_params[3] = { i8ptr, tp[0], tp[1 % 2] };
        LLVMTypeRef thunk_ty = LLVMFunctionType(ret, thunk_params, np + 1, 0);
        thunk = LLVMAddFunction(g->mod, tname, thunk_ty);
        LLVMSetLinkage(thunk, LLVMInternalLinkage);
        LLVMBasicBlockRef saved = LLVMGetInsertBlock(g->builder);
        LLVMPositionBuilderAtEnd(g->builder,
            LLVMAppendBasicBlockInContext(g->ctx, thunk, "entry"));
        LLVMValueRef cargs[2] = { LLVMGetParam(thunk, 1), LLVMGetParam(thunk, 2) };
        LLVMValueRef r = zan_call2(g->builder, acc_ty, acc, cargs, np,
            LLVMGetTypeKind(ret) == LLVMVoidTypeKind ? "" : "mg.r");
        if (LLVMGetTypeKind(ret) == LLVMVoidTypeKind) LLVMBuildRetVoid(g->builder);
        else LLVMBuildRet(g->builder, r);
        LLVMPositionBuilderAtEnd(g->builder, saved);
    }
    return build_static_mg_record(g, zan_loc(0, 0, 0, 0), lname,
                                  closure_header_type(g), thunk);
}

static LLVMValueRef emit_lambda_typed(zan_irgen_t *g, zan_ast_node_t *expr,
                                      zan_type_t *expected, local_scope_t *locals) {
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    int pc = expr->lambda.params.count;
    bool exp_delegate = expected && expected->kind == TYPE_DELEGATE;

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
        di_set_loc(g, expr->loc);
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

static zan_type_t *method_param_type(zan_irgen_t *g, zan_symbol_t *msym, int idx) {
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

static bool type_mentions_tp(zan_type_t *t) {
    if (!t) return false;
    if (t->kind == TYPE_TYPE_PARAM) return true;
    if (t->kind == TYPE_ARRAY || t->kind == TYPE_NULLABLE)
        return type_mentions_tp(t->element_type);
    if (t->kind == TYPE_DELEGATE) {
        if (type_mentions_tp(t->delegate_ret_type)) return true;
        for (int i = 0; i < t->delegate_param_count; i++)
            if (type_mentions_tp(t->delegate_param_types[i])) return true;
    }
    for (int i = 0; i < t->type_arg_count; i++)
        if (type_mentions_tp(t->type_args[i])) return true;
    return false;
}

static void unify_method_tp(zan_type_t *dp, zan_type_t *at,
                            zan_ast_list_t *tps, zan_type_t **bind) {
    if (!dp || !at) return;
    if (dp->kind == TYPE_TYPE_PARAM) {
        for (int i = 0; i < tps->count; i++) {
            zan_istr_t tn = tps->items[i]->ident.name;
            if (tn.len == dp->name.len &&
                memcmp(tn.str, dp->name.str, (size_t)dp->name.len) == 0) {
                if (!bind[i]) bind[i] = at;
                return;
            }
        }
        return;
    }
    if ((dp->kind == TYPE_ARRAY || dp->kind == TYPE_NULLABLE) &&
        dp->kind == at->kind) {
        unify_method_tp(dp->element_type, at->element_type, tps, bind);
        return;
    }
    for (int i = 0; i < dp->type_arg_count && i < at->type_arg_count; i++)
        unify_method_tp(dp->type_args[i], at->type_args[i], tps, bind);
}

static zan_type_t *subst_method_tp(zan_irgen_t *g, zan_type_t *t,
                                   zan_ast_list_t *tps, zan_type_t **bind) {
    if (!t || !type_mentions_tp(t)) return t;
    if (t->kind == TYPE_TYPE_PARAM) {
        for (int i = 0; i < tps->count; i++) {
            zan_istr_t tn = tps->items[i]->ident.name;
            if (bind[i] && tn.len == t->name.len &&
                memcmp(tn.str, t->name.str, (size_t)t->name.len) == 0)
                return bind[i];
        }
        return t;
    }
    zan_type_t *nt = (zan_type_t *)zan_arena_alloc(g->arena, sizeof(zan_type_t));
    *nt = *t;
    if (t->kind == TYPE_ARRAY || t->kind == TYPE_NULLABLE) {
        nt->element_type = subst_method_tp(g, t->element_type, tps, bind);
        return nt;
    }
    if (t->kind == TYPE_DELEGATE) {
        nt->delegate_ret_type = subst_method_tp(g, t->delegate_ret_type, tps, bind);
        if (t->delegate_param_count > 0) {
            nt->delegate_param_types = (zan_type_t **)zan_arena_alloc(
                g->arena, sizeof(zan_type_t *) * (size_t)t->delegate_param_count);
            for (int i = 0; i < t->delegate_param_count; i++)
                nt->delegate_param_types[i] =
                    subst_method_tp(g, t->delegate_param_types[i], tps, bind);
        }
    }
    if (t->type_arg_count > 0) {
        nt->type_args = (zan_type_t **)zan_arena_alloc(
            g->arena, sizeof(zan_type_t *) * (size_t)t->type_arg_count);
        for (int i = 0; i < t->type_arg_count; i++)
            nt->type_args[i] = subst_method_tp(g, t->type_args[i], tps, bind);
    }
    return nt;
}

/* 泛型方法调用点推导类型实参绑定 */
static void infer_method_tp_bindings(zan_irgen_t *g, zan_symbol_t *msym,
                                     zan_ast_node_t *call,
                                     zan_ast_node_t *recv_expr,
                                     local_scope_t *locals,
                                     zan_type_t **bind) {
    zan_ast_list_t *tps = &msym->decl->method_decl.type_params;
    for (int i = 0; i < tps->count && i < call->call.type_args.count; i++)
        bind[i] = resolve_type_ctx(g, call->call.type_args.items[i]);

    /* collect_inst_expr 的实例收集路径没有词法作用域（locals=NULL）：
     * 显式类型实参的绑定已在上面完成，其余两遍推导都要把实参表达式
     * 交给 infer_expr_type/local_find——local_find 对 NULL scope 直接
     * 解引用，`ParseScoped<T>(json, lambda)` 编译期段错误
     * （json_scoped_scratch/reflect_* 一线）。没有 locals 就到此为止。 */
    if (!locals) return;
    zan_ast_list_t *params = &msym->decl->method_decl.params;
    /* 形参与实参的映射口径必须和发射端一致（try_method_spec / emit_method_spec_call）：
     * 只有「静态方法经实例表达式调用」时接收者才占第 0 个实参；实例方法的接收者是
     * this，不是声明形参之一。早期这里写成 recv_expr ? 1 : 0，导致实例泛型方法
     * 拿接收者去合一形参类型，真正的实参没参与绑定 → bind 为空 → 退化成擦除版
     * → 形参里的 `is T` 被折成常量 false。 */
    bool m_static = (msym->decl->method_decl.modifiers & MOD_STATIC) != 0;
    int arg_base = (recv_expr && m_static) ? 1 : 0;
    for (int j = 0; j < params->count; j++) {
        zan_ast_node_t *aexpr = NULL;
        if (arg_base == 1 && j == 0) aexpr = recv_expr;
        else if (j - arg_base < call->call.args.count)
            aexpr = call->call.args.items[j - arg_base];
        if (!aexpr || aexpr->kind == AST_LAMBDA) continue;
        zan_type_t *dp = method_param_type(g, msym, j);
        if (!dp || !type_mentions_tp(dp)) continue;
        zan_type_t *at = infer_expr_type(g, aexpr, locals);
        zan_compile_trace("  tp param %d: dp_kind=%d dp_args=%d at_kind=%d at_args=%d",
                          j, (int)dp->kind, dp->type_arg_count,
                          at ? (int)at->kind : -1, at ? at->type_arg_count : -1);
        /* 具体化变体里，泛型类的类型实参已定：把实参类型里的类类型参数
         * 换成实参，否则「类类型参数 → 方法类型参数」的绑定推不出来，
         * 泛型方法会退化成擦除版（返回值是默认值，判定全错）。 */
        if (at && g->cur_inst) at = subst_type_param_deep(g, at, g->cur_inst);
        if (at && !type_mentions_tp(at)) unify_method_tp(dp, at, tps, bind);
    }

    /* 第二遍推导：根据委托形参签名推导 Lambda 参数与返回类型 */
    for (int j = 0; j < params->count; j++) {
        int ai = j - arg_base;
        if (ai < 0 || ai >= call->call.args.count) continue;
        zan_ast_node_t *a = call->call.args.items[ai];
        if (!a || a->kind != AST_LAMBDA) continue;
        zan_type_t *dp = method_param_type(g, msym, j);
        if (!dp || dp->kind != TYPE_DELEGATE || !type_mentions_tp(dp)) continue;
        if (a->lambda.params.count != dp->delegate_param_count) continue;
        zan_type_t *sdp = subst_method_tp(g, dp, tps, bind);
        int mark = locals->count;
        unsigned mark_gen = g_local_gen;
        bool params_known = true;
        for (int k = 0; k < a->lambda.params.count; k++) {
            zan_ast_node_t *lp = a->lambda.params.items[k];
            zan_type_t *lpt = lp->param.type
                ? zan_binder_resolve_type(g->binder, lp->param.type)
                : sdp->delegate_param_types[k];
            /* 外层泛型类的类型实参在这里已经定下来了：委托的形参类型
             * 可能正是类的类型参数（FuncT<T,V>），不替换的话它永远算
             * 「未定」，lambda 的返回类型就推不出来，方法类型参数绑不上。 */
            if (lpt && g->cur_inst)
                lpt = subst_type_param_deep(g, lpt, g->cur_inst);
            if (lpt && !type_mentions_tp(lpt)) {
                if (lp->param.type)
                    unify_method_tp(dp->delegate_param_types[k], lpt, tps, bind);
                local_add(locals, lp->param.name, NULL, lpt);
            } else {
                params_known = false;
            }
        }
        if (params_known &&
            type_mentions_tp(dp->delegate_ret_type) &&
            a->lambda.body && a->lambda.body->kind != AST_BLOCK) {
            zan_type_t *bt = infer_expr_type(g, a->lambda.body, locals);
            if (!bt || type_mentions_tp(bt)) {
                switch (expr_family(g, a->lambda.body, locals)) {
                case FAM_BOOL: bt = g->binder->type_bool; break;
                case FAM_INT: bt = g->binder->type_int; break;
                case FAM_FLOAT: bt = g->binder->type_double; break;
                case FAM_STRING: bt = g->binder->type_string; break;
                default: bt = NULL; break;
                }
            }
            if (bt && !type_mentions_tp(bt))
                unify_method_tp(dp->delegate_ret_type, bt, tps, bind);
        }
        locals->count = mark;
        g_local_gen = mark_gen;
    }
}

/* 泛型形参类型实参代换 */
static zan_type_t *method_param_type_at(zan_irgen_t *g, zan_symbol_t *msym,
                                        int idx, zan_ast_node_t *call,
                                        zan_ast_node_t *recv_expr,
                                        local_scope_t *locals) {
    zan_type_t *pt = method_param_type(g, msym, idx);
    if (!pt) return pt;
    /* 外层泛型类类型形参代换 */
    if (pt->kind == TYPE_DELEGATE) {
        zan_type_t *recv = g->cur_recv_type ? g->cur_recv_type
                         : (recv_expr ? infer_expr_type(g, recv_expr, locals)
                                      : g->cur_inst);
        if (recv && recv->type_arg_count > 0)
            pt = subst_delegate_sig(g, pt, recv);
    }
    if (!msym->decl || msym->decl->kind != AST_METHOD_DECL) return pt;
    zan_ast_list_t *tps = &msym->decl->method_decl.type_params;
    if (tps->count == 0 || tps->count > 8) return pt;
    if (!type_mentions_tp(pt)) return pt;
    if (!call || call->kind != AST_CALL) return pt;

    zan_type_t *bind[8] = { NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL };
    infer_method_tp_bindings(g, msym, call, recv_expr, locals, bind);
    return subst_method_tp(g, pt, tps, bind);
}

/* 判断方法返回类型是否为该方法自身声明的泛型类型形参 */
static bool method_ret_is_bare_tp(zan_symbol_t *msym) {
    if (!msym || !msym->decl || msym->decl->kind != AST_METHOD_DECL) return false;
    zan_ast_list_t *tps = &msym->decl->method_decl.type_params;
    if (tps->count == 0) return false;
    zan_ast_node_t *ret_ref = msym->decl->method_decl.return_type;
    if (!ret_ref || ret_ref->kind != AST_TYPE_REF) return false;
    if (ret_ref->type_ref.type_args.count > 0 || ret_ref->type_ref.is_array)
        return false;
    zan_istr_t rn = ret_ref->type_ref.name;
    for (int i = 0; i < tps->count; i++) {
        zan_istr_t tn = tps->items[i]->ident.name;
        if (tn.len == rn.len && memcmp(tn.str, rn.str, (size_t)rn.len) == 0)
            return true;
    }
    return false;
}

static zan_type_t *method_ret_type_at(zan_irgen_t *g, zan_symbol_t *msym,
                                      zan_ast_node_t *call,
                                      zan_ast_node_t *recv_expr,
                                      local_scope_t *locals) {
    zan_type_t *rt = msym ? msym->type : NULL;
    if (!rt || !msym->decl || msym->decl->kind != AST_METHOD_DECL) return rt;
    zan_ast_list_t *tps = &msym->decl->method_decl.type_params;
    if (tps->count == 0 || tps->count > 8) return rt;
    if (!type_mentions_tp(rt)) return rt;
    if (!call || call->kind != AST_CALL) return rt;
    zan_type_t *bind[8] = { NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL };
    infer_method_tp_bindings(g, msym, call, recv_expr, locals, bind);
    return subst_method_tp(g, rt, tps, bind);
}

/* async 委托调用：调用生成协程帧并在原地 await 挂起等待结果 */
static void check_delegate_async_match(zan_irgen_t *g, zan_ast_node_t *e,
                                       zan_type_t *dt, local_scope_t *locals) {
    if (!e || !dt || dt->kind != TYPE_DELEGATE) return;
    zan_symbol_t *m = NULL;
    if (e->kind == AST_IDENTIFIER) {
        if (local_find(locals, e->ident.name)) return;
        if (g->current_type_sym)
            m = get_method_sym(g->current_type_sym, e->ident.name);
    } else if (e->kind == AST_MEMBER_ACCESS && e->member.object &&
               e->member.object->kind == AST_IDENTIFIER &&
               !local_find(locals, e->member.object->ident.name)) {
        zan_symbol_t *cs =
            zan_binder_lookup(g->binder, e->member.object->ident.name);
        if (cs && (cs->kind == SYM_CLASS || cs->kind == SYM_STRUCT))
            m = get_method_sym(cs, e->member.name);
    }
    if (!m || !m->decl || m->decl->kind != AST_METHOD_DECL) return;
    int m_async = (m->decl->method_decl.modifiers & MOD_ASYNC) != 0;
    if (m_async == (dt->delegate_is_async != 0)) return;
    zan_diag_emit_code(g->diag, DIAG_ERROR, e->loc, 3047,
        "cannot convert %s method '%.*s' to %s delegate '%.*s'",
        m_async ? "async" : "non-async", m->name.len, m->name.str,
        dt->delegate_is_async ? "async" : "non-async",
        dt->name.len, dt->name.str);
}

static zan_ast_node_t *ast_type_from_zan_type(zan_irgen_t *g,
                                                zan_type_t *type,
                                                zan_loc_t loc) {
    if (!g || !type) return NULL;
    zan_ast_node_t *ref = zan_ast_new(g->arena, AST_TYPE_REF, loc);
    ref->type_ref.name = type->name;
    zan_ast_list_init(&ref->type_ref.type_args);
    ref->type_ref.is_nullable = false;
    ref->type_ref.is_array = false;
    for (int i = 0; i < type->type_arg_count; i++) {
        zan_ast_node_t *arg = ast_type_from_zan_type(g, type->type_args[i], loc);
        if (!arg) return NULL;
        zan_ast_list_push(&ref->type_ref.type_args, arg, g->arena);
    }
    return ref;
}

/* 原地改写构造实参以复用类构造管线 */
static bool wrap_implicit_ctor_arg(zan_irgen_t *g, zan_ast_node_t *arg,
                                   zan_type_t *ptype) {
    if (!g || !arg || !ptype) return false;
    zan_ast_node_t *type_ref = ast_type_from_zan_type(g, ptype, arg->loc);
    if (!type_ref) return false;
    zan_ast_node_t *original = (zan_ast_node_t *)zan_arena_alloc(
        g->arena, sizeof(zan_ast_node_t));
    if (!original) return false;
    *original = *arg;
    zan_ast_node_t replacement;
    memset(&replacement, 0, sizeof(replacement));
    replacement.kind = AST_NEW_EXPR;
    replacement.loc = arg->loc;
    replacement.new_expr.type = type_ref;
    replacement.new_expr.is_array = false;
    replacement.new_expr.array_init = false;
    zan_ast_list_init(&replacement.new_expr.args);
    zan_ast_list_push(&replacement.new_expr.args, original, g->arena);
    *arg = replacement;
    infer_cache_invalidate();
    return true;
}

static LLVMValueRef emit_arg_typed(zan_irgen_t *g, zan_ast_node_t *arg,
                                   zan_type_t *ptype, local_scope_t *locals) {
    zan_type_t *atype = infer_expr_type(g, arg, locals);
    check_implicit_narrowing(g, ptype, atype, arg, "argument");
    check_value_type_mismatch(g, ptype, atype, arg, "argument");
    check_generic_invariance(g, ptype, atype, arg, "argument");
    if (ptype && atype && ptype->kind == TYPE_CLASS &&
        is_implicit_ctor_source(atype)) {
        if (implicit_ctor_for_arg(g, ptype, atype, arg, locals)) {
            wrap_implicit_ctor_arg(g, arg, ptype);
            return emit_expr(g, arg, locals);
        }
        zan_diag_emit_code(g->diag, DIAG_ERROR, arg->loc, 3048,
            "cannot convert '%s' to '%s' in argument: no matching constructor",
            atype->name.str ? atype->name.str : "?",
            ptype->name.str ? ptype->name.str : "?");
        return LLVMConstNull(map_type(g, ptype));
    }
    if (arg && ptype && ptype->kind == TYPE_DELEGATE) {
        if (arg->kind == AST_LAMBDA)
            return emit_lambda_typed(g, arg, ptype, locals);
        check_delegate_async_match(g, arg, ptype, locals);
    }
    LLVMValueRef v = emit_expr(g, arg, locals);
    if (v && ptype && atype)
        v = emit_user_conversion(g, atype, ptype, "op_implicit", v, arg, locals);
    /* A556-1 隐式装箱：值形状实参流向 object/接口形参 */
    v = emit_implicit_box(g, ptype, v, arg, locals);
    if (v && ptype && (ptype->kind == TYPE_DOUBLE || ptype->kind == TYPE_FLOAT) &&
        LLVMGetTypeKind(LLVMTypeOf(v)) == LLVMIntegerTypeKind &&
        LLVMGetIntTypeWidth(LLVMTypeOf(v)) > 1)
        v = coerce_int_to(g, v, map_type(g, ptype));
    if (v && ptype && ptype->kind == TYPE_NULLABLE)
        v = coerce_int_to(g, v, map_type(g, ptype));
    return v;
}

/* ref / out 实参处理：传递局部变量或存储槽的内存地址 */
static LLVMValueRef emit_ref_lvalue_ptr(zan_irgen_t *g, zan_ast_node_t *tgt,
                                        local_scope_t *locals) {
    if (!tgt) return NULL;
    if (tgt->kind == AST_THIS_EXPR) {
        if (g->current_this && g->current_type_sym) {
            LLVMTypeRef st = get_struct_llvm_type(g, g->current_type_sym);
            if (st) {
                return LLVMBuildLoad2(g->builder, LLVMPointerType(st, 0),
                                      g->current_this, "ref.this");
            }
        }
        return NULL;
    }
    if (tgt->kind == AST_IDENTIFIER) {
        local_var_t *local = local_find(locals, tgt->ident.name);
        if (local) return local->alloca;
        if (g->current_type_sym) {
            zan_symbol_t *field = get_field_sym(g->current_type_sym, tgt->ident.name);
            LLVMValueRef global = field ? get_static_field_global(g,
                g->current_type_sym, field, NULL) : NULL;
            if (global) return global;
            int fi = get_field_index(g->current_type_sym, tgt->ident.name);
            LLVMTypeRef st = get_struct_llvm_type(g, g->current_type_sym);
            if (fi >= 0 && st && g->current_this) {
                LLVMValueRef self = LLVMBuildLoad2(g->builder,
                    LLVMPointerType(st, 0), g->current_this, "ref.this");
                return emit_field_ptr(g, g->current_type_sym, st, self, fi, "ref.fld");
            }
        }
        return NULL;
    }

    if (tgt->kind == AST_MEMBER_ACCESS && !tgt->member.null_cond) {
        zan_ast_node_t *obj_expr = tgt->member.object;
        if (obj_expr->kind == AST_IDENTIFIER &&
            !local_find(locals, obj_expr->ident.name)) {
            zan_symbol_t *cs = zan_binder_lookup(g->binder, obj_expr->ident.name);
            if (cs && (cs->kind == SYM_CLASS || cs->kind == SYM_STRUCT)) {
                zan_symbol_t *fs = get_field_sym(cs, tgt->member.name);
                LLVMValueRef gv = fs ? get_static_field_global(g, cs, fs,
                    static_access_inst(g, obj_expr)) : NULL;
                if (gv) return gv;
            }
        }
        zan_symbol_t *cls = expr_class_sym(g, obj_expr, locals);
        if (cls) {
            int fi = get_field_index(cls, tgt->member.name);
            LLVMTypeRef st = get_struct_llvm_type(g, cls);
            if (fi >= 0 && st) {
                LLVMValueRef obj_val = cls->kind == SYM_STRUCT
                    ? emit_ref_lvalue_ptr(g, obj_expr, locals)
                    : emit_guarded_member_object(g, tgt, locals);
                if (obj_val &&
                    LLVMGetTypeKind(LLVMTypeOf(obj_val)) == LLVMPointerTypeKind) {
                    if (LLVMTypeOf(obj_val) != LLVMPointerType(st, 0))
                        obj_val = LLVMBuildBitCast(g->builder, obj_val,
                            LLVMPointerType(st, 0), "ref.obj");
                    return emit_field_ptr(g, cls, st, obj_val, fi, "ref.fld");
                }
            }
        }
        return NULL;
    }

    /* 集合/数组/Span 元素取地址传参 */
    if (tgt->kind == AST_INDEX)
        return emit_struct_elem_ptr(g, tgt, locals);

    return NULL;
}

static LLVMValueRef emit_ref_arg(zan_irgen_t *g, zan_ast_node_t *arg,
                                 local_scope_t *locals) {
    zan_ast_node_t *tgt = arg->ref_arg.expr;
    if (arg->ref_arg.decl_type && tgt && tgt->kind == AST_IDENTIFIER) {
        zan_type_t *dt = resolve_type_ctx(g, arg->ref_arg.decl_type);
        LLVMTypeRef lt = map_type(g, dt);
        LLVMValueRef a = emit_entry_alloca(g, lt, "out");
        zan_store_fit(g, LLVMConstNull(lt), a);
        local_add(locals, tgt->ident.name, a, dt);
        return a;
    }
    if (tgt && tgt->kind == AST_IDENTIFIER) {
        local_var_t *l = local_find(locals, tgt->ident.name);
        if (l) return l->alloca;
        if (g->current_type_sym) {
            zan_symbol_t *fs = get_field_sym(g->current_type_sym,
                                             tgt->ident.name);
            LLVMValueRef gv = fs ? get_static_field_global(g,
                g->current_type_sym, fs, NULL) : NULL;
            if (gv) return gv;
            int fi = get_field_index(g->current_type_sym, tgt->ident.name);
            LLVMTypeRef st = get_struct_llvm_type(g, g->current_type_sym);
            if (fi >= 0 && st && g->current_this) {
                LLVMValueRef this_ptr = LLVMBuildLoad2(g->builder,
                    LLVMPointerType(st, 0), g->current_this, "this");
                return emit_field_ptr(g, g->current_type_sym, st, this_ptr,
                                      fi, "ref.this");
            }
        }
    }
    /* 位置表达式 (obj.field, arr[i]) 取地址传参 */
    LLVMValueRef place = emit_ref_lvalue_ptr(g, tgt, locals);
    if (place) {
        if (LLVMGetTypeKind(LLVMTypeOf(place)) != LLVMPointerTypeKind)
            return LLVMBuildIntToPtr(g->builder, place,
                LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0), "ref.ip");
        return place;
    }
    zan_diag_emit_code(g->diag, DIAG_ERROR, arg->loc, 3049,
        "ref/out argument must be a variable, a field, or an element");
    return emit_expr(g, tgt, locals);
}
