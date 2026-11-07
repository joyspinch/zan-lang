#include "irgen_internal.h"
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
LLVMValueRef emit_box_cell(zan_irgen_t *g, zan_loc_t loc,
                                  LLVMTypeRef payload, zan_type_t *vtype,
                                  LLVMValueRef init);
LLVMValueRef box_value_ptr(zan_irgen_t *g, LLVMValueRef cell,
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
int body_writes_ident(zan_irgen_t *g, zan_ast_node_t *body,
                             zan_istr_t name) {
    if (!body || !name.len) return 0;
    return body_writes_ident_memo(g, body, name);
}

/* 判断当前编译函数内的 Lambda 是否捕获了该声明变量 */
int local_is_lambda_captured(zan_irgen_t *g, local_scope_t *locals,
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
LLVMValueRef emit_box_cell(zan_irgen_t *g, zan_loc_t loc,
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
LLVMValueRef box_value_ptr(zan_irgen_t *g, LLVMValueRef cell,
                                  LLVMTypeRef payload) {
    return LLVMBuildStructGEP2(g->builder, box_cell_type(g, payload), cell,
                               ZAN_BOX_VALUE_FIELD, "box.v");
}

/* 作用域绑定接管共享 cell 所有权，异常展开时统一释放 */
void own_box_cell(zan_irgen_t *g, local_var_t *v, LLVMValueRef cell) {
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
    lpad_register_local(g, v);
}

/* 将按值传递的形参移动至闭包共享 cell */
void box_captured_parameter(zan_irgen_t *g, local_scope_t *locals,
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
    /* object 形参捕获解禁（B-ID117，同 var 口径）：rc 指针走下方既有
     * box+ARC 通路。开泛形参（含 !type 的擦除形态）仍 fail-closed。 */
    if (!type || type->kind == TYPE_TYPE_PARAM) {
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
    lpad_register_local(g, v);
}

/* 实例方法组转委托：将接收者绑定入方法组记录 */
LLVMValueRef emit_method_group_closure(zan_irgen_t *g, zan_symbol_t *msym,
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
LLVMValueRef build_static_mg_record(zan_irgen_t *g, zan_loc_t loc,
                                           const char *lname, LLVMTypeRef rec_ty,
                                           LLVMValueRef thunk) {
    build_closure_dtor(g, lname, rec_ty, NULL, 0, 0, false);
    return emit_closure_record(g, loc, lname, rec_ty, thunk, thunk,
                               NULL, 0, 0, NULL, false);
}

/* 合成双向数据绑定属性访问器 */
LLVMValueRef emit_binding_acc_delegate(zan_irgen_t *g, LLVMValueRef acc,
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

