static zan_ast_node_t *parse_type_decl(zan_parser_t *p, uint32_t modifiers) {
    zan_loc_t loc = p->current.loc;
    zan_ast_kind_t kind = AST_CLASS_DECL;

    if (parser_match(p, TK_CLASS)) {
        kind = AST_CLASS_DECL;
    } else if (parser_match(p, TK_STRUCT)) {
        kind = AST_STRUCT_DECL;
    } else if (parser_match(p, TK_INTERFACE)) {
        kind = AST_INTERFACE_DECL;
    } else if (parser_match(p, TK_ENUM)) {
        kind = AST_ENUM_DECL;
    } else {
        zan_diag_emit_code(p->diag, DIAG_ERROR, loc, 53, "expected class, struct, interface, or enum");
        return parser_error_node(p);
    }

    zan_istr_t name = {0};
    if (parser_check(p, TK_IDENT)) {
        parser_advance(p);
        name = p->previous.str_val;
    } else if (!parser_take_reserved_word(p, &name)) {
        parser_diag_expected_name(p, p->current.loc, "expected type name");
    }

    zan_ast_list_t type_params;
    zan_ast_list_init(&type_params);
    if (parser_match(p, TK_LESS)) {
        while (!parser_check(p, TK_GREATER) && !parser_check(p, TK_EOF)) {
            int variance = 0;
            if (parser_match(p, TK_OUT)) {
                variance = 1;
            } else if (parser_match(p, TK_IN)) {
                variance = -1;
            }
            if (parser_check(p, TK_IDENT)) {
                parser_advance(p);
                zan_ast_node_t *tp = zan_ast_new(p->arena, AST_IDENTIFIER, p->previous.loc);
                tp->ident.name = p->previous.str_val;
                tp->ident.variance = variance;
                zan_ast_list_push(&type_params, tp, p->arena);
            }
            if (!parser_match(p, TK_COMMA)) break;
        }
        parser_expect(p, TK_GREATER);
    }

    zan_ast_list_t bases;
    zan_ast_list_init(&bases);
    if (parser_match(p, TK_COLON)) {
        do {
            zan_ast_node_t *base = parse_type_ref(p);
            zan_ast_list_push(&bases, base, p->arena);
        } while (parser_match(p, TK_COMMA));
    }

    zan_ast_list_t wheres;
    zan_ast_list_init(&wheres);
    parse_where_clauses(p, &wheres);

    zan_ast_list_t members;
    zan_ast_list_init(&members);

    if (kind == AST_ENUM_DECL) {
        parser_expect(p, TK_LBRACE);
        while (!parser_check(p, TK_RBRACE) && !parser_check(p, TK_EOF)) {
            zan_loc_t member_loc = p->current.loc;
            if (parser_check(p, TK_IDENT)) {
                parser_advance(p);
                zan_ast_node_t *em = zan_ast_new(p->arena, AST_ENUM_MEMBER, member_loc);
                em->enum_member.name = p->previous.str_val;
                em->enum_member.value = NULL;
                if (parser_match(p, TK_EQ)) {
                    em->enum_member.value = parse_expression(p);
                }
                zan_ast_list_push(&members, em, p->arena);
                parser_match(p, TK_COMMA);
            } else {
                parser_advance(p); /* skip */
            }
        }
        parser_expect(p, TK_RBRACE);
    } else {
        parser_expect(p, TK_LBRACE);
        while (!parser_check(p, TK_RBRACE) && !parser_check(p, TK_EOF)) {
            uint32_t before = p->current.loc.offset;
            zan_ast_node_t *member = parse_member_decl(p);
            if (member) {
                zan_ast_list_push(&members, member, p->arena);
            }
            for (int pi = 0; pi < p->pending_members.count; pi++) {
                zan_ast_list_push(&members, p->pending_members.items[pi],
                                  p->arena);
            }
            p->pending_members.count = 0;
            if (p->current.loc.offset == before && !parser_check(p, TK_EOF)) {
                parser_advance(p);
            }
        }
        parser_expect(p, TK_RBRACE);
    }

    zan_ast_node_t *n = zan_ast_new(p->arena, kind, loc);
    n->type_decl.name = name;
    n->type_decl.type_params = type_params;
    n->type_decl.bases = bases;
    n->type_decl.members = members;
    n->type_decl.modifiers = modifiers;
    if (wheres.count > 0) {
        n->type_decl.where_clauses = (zan_ast_list_t *)zan_arena_alloc(
            p->arena, sizeof(zan_ast_list_t));
        *n->type_decl.where_clauses = wheres;
    } else {
        n->type_decl.where_clauses = NULL;
    }
    return n;
}

static zan_ast_node_t *parse_using_decl(zan_parser_t *p) {
    zan_loc_t loc = p->current.loc;
    parser_expect(p, TK_USING);

    bool is_static = parser_match(p, TK_STATIC);
    zan_ast_node_t *name = parse_qualified_name(p);
    parser_expect(p, TK_SEMICOLON);

    zan_ast_node_t *n = zan_ast_new(p->arena, AST_USING_DECL, loc);
    n->using_decl.name = name;
    n->using_decl.is_static = is_static;
    return n;
}

void zan_parser_init(zan_parser_t *p, zan_lexer_t *lex, zan_arena_t *arena,
                     zan_diag_t *diag) {
    memset(p, 0, sizeof(*p));
    p->lex = lex;
    p->arena = arena;
    p->diag = diag;
    parser_advance(p);
}

/* ===== A381 lazy iterators：IEnumerable<T> 返回 + yield 的方法，parse 完成
 * 后合成顶层迭代器类（IEnumerator<T> 实现，goto/label 状态机）。此阶段
 * 外层类成员表齐全、comp_unit.decls 可挂合成类。手工探针钉过的形状：
 * MoveNext 里 if(__state==N){goto R_N;} 前导分派、yield return 槽位整体
 * 替换为 { __cur=v; __state=N; return true; R_N: } 块、标签后直接跟下一条
 * （Zan 无空语句）、goto 跳进循环体/普通块合法。===== */

static zan_ast_node_t *iter_type_ref(zan_parser_t *p, const char *name,
                                     zan_loc_t loc) {
    zan_ast_node_t *t = zan_ast_new(p->arena, AST_TYPE_REF, loc);
    t->type_ref.name = (zan_istr_t){ (char *)name, (uint32_t)strlen(name) };
    zan_ast_list_init(&t->type_ref.type_args);
    t->type_ref.is_nullable = false;
    t->type_ref.is_array = false;
    t->type_ref.array_rank = 0;
    t->type_ref.array_element = NULL;
    t->type_ref.rt_type = NULL;
    t->type_ref.rt_scope = NULL;
    return t;
}

static zan_istr_t iter_intern(zan_parser_t *p, const char *s);

static zan_ast_node_t *iter_ident(zan_parser_t *p, const char *name,
                                  zan_loc_t loc) {
    zan_ast_node_t *n = zan_ast_new(p->arena, AST_IDENTIFIER, loc);
    n->ident.name = iter_intern(p, name);
    return n;
}

static zan_ast_node_t *iter_bool(zan_parser_t *p, bool v, zan_loc_t loc) {
    zan_ast_node_t *n = zan_ast_new(p->arena, AST_BOOL_LITERAL, loc);
    n->bool_val = v;
    return n;
}

static zan_ast_node_t *iter_int_const(zan_parser_t *p, int64_t v,
                                      zan_loc_t loc) {
    zan_ast_node_t *n = zan_ast_new(p->arena, AST_INT_LITERAL, loc);
    n->int_val = v;
    n->lit_suffix = 0;
    n->lit_radix = 0;
    return n;
}

static zan_ast_node_t *iter_bin_eq_state(zan_parser_t *p, int st,
                                         zan_loc_t loc) {
    zan_ast_node_t *n = zan_ast_new(p->arena, AST_BINARY, loc);
    n->binary.op = TK_EQ_EQ;
    n->binary.left = iter_ident(p, "__state", loc);
    n->binary.right = iter_int_const(p, st, loc);
    return n;
}

static zan_ast_node_t *iter_assign(zan_parser_t *p, zan_ast_node_t *l,
                                   zan_ast_node_t *r, zan_loc_t loc) {
    zan_ast_node_t *n = zan_ast_new(p->arena, AST_ASSIGNMENT, loc);
    n->binary.op = TK_EQ;
    n->binary.left = l;
    n->binary.right = r;
    return n;
}

static zan_ast_node_t *iter_member(zan_parser_t *p, zan_ast_node_t *obj,
                                   const char *name, zan_loc_t loc) {
    zan_ast_node_t *n = zan_ast_new(p->arena, AST_MEMBER_ACCESS, loc);
    n->member.object = obj;
    n->member.name = iter_intern(p, name);
    n->member.null_cond = 0;
    return n;
}

static zan_ast_node_t *iter_istr_ident(zan_parser_t *p, zan_istr_t name,
                                       zan_loc_t loc) {
    zan_ast_node_t *n = zan_ast_new(p->arena, AST_IDENTIFIER, loc);
    n->ident.name = name;
    return n;
}
/* 合成名进 arena：栈缓冲（snprintf 的 clsbuf/lbuf、hoist 数组）在合成
 * 函数返回后即失效，istr 直指会变成悬垂名——binder 阶段读到垃圾
 * （E1010 duplicate type 乱码 / E1005 undefined type '' 的根源）。 */
static zan_ast_node_t *iter_type_ref_istr(zan_parser_t *p, zan_istr_t name,
                                          zan_loc_t loc) {
    zan_ast_node_t *t = iter_type_ref(p, "", loc);
    t->type_ref.name = name;
    return t;
}

static zan_istr_t iter_intern(zan_parser_t *p, const char *s) {
    uint32_t n = (uint32_t)strlen(s);
    char *buf = (char *)zan_arena_alloc(p->arena, (size_t)n + 1);
    memcpy(buf, s, (size_t)n + 1);
    return (zan_istr_t){ buf, n };
}

typedef struct {
    zan_parser_t *p;
    zan_ast_node_t *cls;      /* 合成中的迭代器类 */
    zan_ast_node_t *elem_ty;  /* 元素类型 AST_TYPE_REF（T） */
    int next_state;
    char hoist[32][64];
    int hoist_count;
    zan_istr_t enc_name;      /* 外层类名（限定改写用） */
    zan_ast_list_t *enc_members;
} iter_ctx;

static bool iter_is_hoisted(iter_ctx *c, zan_istr_t name) {
    for (int i = 0; i < c->hoist_count; i++) {
        if ((int)strlen(c->hoist[i]) == (int)name.len &&
            memcmp(c->hoist[i], name.str, (size_t)name.len) == 0)
            return true;
    }
    return false;
}

static void iter_add_field(iter_ctx *c, const char *fname,
                           zan_ast_node_t *ftype, zan_loc_t loc) {
    zan_ast_node_t *f = zan_ast_new(c->p->arena, AST_FIELD_DECL, loc);
    f->field_decl.name = iter_intern(c->p, fname);
    f->field_decl.type = ftype;
    f->field_decl.initializer = NULL;
    f->field_decl.modifiers = 0;
    f->field_decl.getter_body = NULL;
    f->field_decl.setter_body = NULL;
    f->field_decl.has_getter = false;
    f->field_decl.has_setter = false;
    f->field_decl.has_init = false;
    f->field_decl.indexer_params = NULL;
    zan_ast_list_push(&c->cls->type_decl.members, f, c->p->arena);
}

/* 体内 var_decl 一律提升为字段：声明槽位改写成初始化赋值（无初始化器
 * 则改写成空块）。跨 yield 存活的值全在字段里，恢复跳转才不会踩到
 * 每次 MoveNext 重开的局部。 */
static void iter_hoist_vardecl(zan_ast_node_t *s, iter_ctx *c) {
    zan_loc_t loc = s->loc;
    zan_istr_t name = s->var_decl.name;
    if (iter_is_hoisted(c, name)) {
        zan_diag_emit_code(c->p->diag, DIAG_ERROR, loc, 59,
            "duplicate local '%.*s' in iterator method -- names must be unique "
            "across the whole body (v1 hoists every local to a field)",
            (int)name.len, name.str);
    }
    if (!s->var_decl.type) {
        zan_diag_emit_code(c->p->diag, DIAG_ERROR, loc, 59,
            "iterator local '%.*s' must declare an explicit type ('var' is not "
            "supported in iterator methods in this release)",
            (int)name.len, name.str);
    }
    const char *fname = "?";
    if (c->hoist_count < 32) {
        int n = name.len < 63 ? (int)name.len : 63;
        memcpy(c->hoist[c->hoist_count], name.str, (size_t)n);
        c->hoist[c->hoist_count][n] = 0;
        fname = c->hoist[c->hoist_count];
        c->hoist_count++;
    }
    iter_add_field(c, fname, s->var_decl.type, loc);
    if (s->var_decl.initializer) {
        char ibuf[64];
        snprintf(ibuf, sizeof(ibuf), "%s", fname);
        zan_ast_node_t *as = iter_assign(c->p, iter_ident(c->p, ibuf, loc),
                                         s->var_decl.initializer, loc);
        s->kind = AST_EXPR_STMT;
        s->expr_stmt.expr = as;
    } else {
        s->kind = AST_BLOCK;
        zan_ast_list_init(&s->block.stmts);
    }
}

/* 残留 yield（catch 体内等不支持位置）报错，不静默。 */
static void iter_check_residual(zan_ast_node_t *s, zan_parser_t *p) {
    if (!s) return;
    if (s->kind == AST_YIELD_STMT) {
        zan_diag_emit_code(p->diag, DIAG_ERROR, s->loc, 59,
            "'yield' inside a catch body is not supported in iterator "
            "methods in this release");
        return;
    }
    switch (s->kind) {
    case AST_BLOCK: {
        for (int i = 0; i < s->block.stmts.count; i++)
            iter_check_residual(s->block.stmts.items[i], p);
        return;
    }
    case AST_IF_STMT:
        iter_check_residual(s->if_stmt.then_body, p);
        iter_check_residual(s->if_stmt.else_body, p);
        return;
    case AST_WHILE_STMT:
        iter_check_residual(s->while_stmt.body, p);
        return;
    case AST_DO_WHILE_STMT:
        iter_check_residual(s->while_stmt.body, p);
        return;
    case AST_FOR_STMT:
        iter_check_residual(s->for_stmt.body, p);
        return;
    case AST_TRY_STMT:
        iter_check_residual(s->try_stmt.try_body, p);
        if (s->try_stmt.finally_body)
            iter_check_residual(s->try_stmt.finally_body, p);
        for (int i = 0; i < s->try_stmt.catches.count; i++)
            iter_check_residual(s->try_stmt.catches.items[i], p);
        return;
    default:
        return;
    }
}

/* ===== 标识符限定改写（A381 路线 1）：体内无限定标识符若与外层类成员
 * 同名（形参/已提升局部除外），就地改成 Enclosing.member——合成类里没有
 * 外层作用域链（探针 y3：嵌套类 E3003）。注意这是词法级改写：体内局部
 * 恰好撞外层成员名会误伤（v1 记档限制）。 ===== */

static bool iter_enc_has_member(iter_ctx *c, zan_istr_t name) {
    if (!c->enc_members) return false;
    for (int i = 0; i < c->enc_members->count; i++) {
        zan_ast_node_t *mm = c->enc_members->items[i];
        if (!mm) continue;
        if (mm->kind == AST_METHOD_DECL) {
            if (mm->method_decl.name.len == name.len &&
                memcmp(mm->method_decl.name.str, name.str,
                       (size_t)name.len) == 0)
                return true;
        } else if (mm->kind == AST_FIELD_DECL ||
                   mm->kind == AST_PROPERTY_DECL) {
            if (mm->field_decl.name.len == name.len &&
                memcmp(mm->field_decl.name.str, name.str,
                       (size_t)name.len) == 0)
                return true;
        }
    }
    return false;
}

/* 表达式递归：命中外层成员名的标识符就地变异为 AST_MEMBER_ACCESS。
 * 一律先保存旧 name istr（union 重叠，member.object 会覆盖 ident.name）。 */
static void iter_fix_expr(zan_ast_node_t *e, iter_ctx *c);

static void iter_fix_exprs(zan_ast_list_t *l, iter_ctx *c) {
    for (int i = 0; i < l->count; i++)
        iter_fix_expr(l->items[i], c);
}

static void iter_fix_expr(zan_ast_node_t *e, iter_ctx *c) {
    if (!e) return;
    switch (e->kind) {
    case AST_IDENTIFIER:
        if (e->ident.name.len > 0 && !iter_is_hoisted(c, e->ident.name) &&
            iter_enc_has_member(c, e->ident.name)) {
            zan_istr_t nm = e->ident.name;
            zan_ast_node_t *obj = zan_ast_new(c->p->arena, AST_IDENTIFIER,
                                              e->loc);
            obj->ident.name = c->enc_name;
            e->kind = AST_MEMBER_ACCESS;
            e->member.object = obj;
            e->member.name = nm;
            e->member.null_cond = 0;
        }
        return;
    case AST_BINARY:
        iter_fix_expr(e->binary.left, c);
        iter_fix_expr(e->binary.right, c);
        return;
    case AST_UNARY:
    case AST_POSTFIX_UNARY:
        iter_fix_expr(e->unary.operand, c);
        return;
    case AST_CALL:
        iter_fix_expr(e->call.callee, c);
        iter_fix_exprs(&e->call.args, c);
        return;
    case AST_MEMBER_ACCESS:
        iter_fix_expr(e->member.object, c);
        return;
    case AST_INDEX:
        iter_fix_expr(e->index.object, c);
        iter_fix_expr(e->index.index, c);
        return;
    case AST_ASSIGNMENT:
        iter_fix_expr(e->binary.left, c);
        iter_fix_expr(e->binary.right, c);
        return;
    case AST_CONDITIONAL:
        iter_fix_expr(e->conditional.cond, c);
        iter_fix_expr(e->conditional.then_expr, c);
        iter_fix_expr(e->conditional.else_expr, c);
        return;
    case AST_NEW_EXPR:
        iter_fix_exprs(&e->new_expr.args, c);
        return;
    case AST_CAST_EXPR:
        iter_fix_expr(e->cast.expr, c);
        return;
    case AST_IS_EXPR:
    case AST_AS_EXPR:
        iter_fix_expr(e->type_test.expr, c);
        return;
    case AST_AWAIT_EXPR:
        iter_fix_expr(e->await_expr.expr, c);
        return;
    case AST_STRING_INTERP:
        iter_fix_exprs(&e->string_interp.parts, c);
        return;
    default:
        return;
    }
}

/* yield/foreach/lock 的 v1 位置限制在改写阶段一并查：foreach/lock 内
 * 的 yield 状态无法提升（foreach 局部、lock 作用域），报错不静默。 */
static void iter_check_stmt_pos(zan_ast_node_t *s, iter_ctx *c, int in_foreach,
                                int in_lock);

static void iter_check_stmts_pos(zan_ast_list_t *l, iter_ctx *c,
                                 int in_foreach, int in_lock) {
    for (int i = 0; i < l->count; i++)
        iter_check_stmt_pos(l->items[i], c, in_foreach, in_lock);
}

static void iter_check_stmt_pos(zan_ast_node_t *s, iter_ctx *c, int in_foreach,
                                int in_lock) {
    if (!s) return;
    if (s->kind == AST_YIELD_STMT && (in_foreach || in_lock)) {
        zan_diag_emit_code(c->p->diag, DIAG_ERROR, s->loc, 59,
            "'yield' inside %s is not supported in iterator methods in this "
            "release -- hoist the sequence into its own iterator method",
            in_foreach ? "foreach" : "lock");
        return;
    }
    switch (s->kind) {
    case AST_BLOCK:
        iter_check_stmts_pos(&s->block.stmts, c, in_foreach, in_lock);
        return;
    case AST_IF_STMT:
        iter_check_stmt_pos(s->if_stmt.then_body, c, in_foreach, in_lock);
        iter_check_stmt_pos(s->if_stmt.else_body, c, in_foreach, in_lock);
        return;
    case AST_WHILE_STMT:
        iter_check_stmt_pos(s->while_stmt.body, c, in_foreach, in_lock);
        return;
    case AST_DO_WHILE_STMT:
        iter_check_stmt_pos(s->while_stmt.body, c, in_foreach, in_lock);
        return;
    case AST_FOR_STMT:
        iter_check_stmt_pos(s->for_stmt.body, c, in_foreach, in_lock);
        return;
    case AST_FOREACH_STMT:
        iter_check_stmt_pos(s->foreach_stmt.body, c, 1, in_lock);
        return;
    case AST_LOCK_STMT:
        iter_check_stmt_pos(s->lock_stmt.body, c, in_foreach, 1);
        return;
    case AST_TRY_STMT:
        iter_check_stmt_pos(s->try_stmt.try_body, c, in_foreach, in_lock);
        if (s->try_stmt.finally_body)
            iter_check_stmt_pos(s->try_stmt.finally_body, c, in_foreach, 1);
        return;
    case AST_SWITCH_STMT:
        for (int i = 0; i < s->switch_stmt.cases.count; i++)
            iter_check_stmt_pos(s->switch_stmt.cases.items[i], c, in_foreach,
                                in_lock);
        return;
    case AST_SWITCH_CASE:
        iter_check_stmt_pos(s->switch_case.body, c, in_foreach, in_lock);
        return;
    default:
        return;
    }
}
/* yield return v 槽位 → 块 { __cur = v; __state = N; return true; R_N: }；
 * yield break → return false。 */
static void iter_rewrite_stmt(zan_ast_node_t *s, iter_ctx *c);

static void iter_rewrite_stmts(zan_ast_list_t *stmts, iter_ctx *c) {
    for (int i = 0; i < stmts->count; i++)
        iter_rewrite_stmt(stmts->items[i], c);
}

static void iter_rewrite_stmt(zan_ast_node_t *s, iter_ctx *c) {
    if (!s) return;
    switch (s->kind) {
    case AST_YIELD_STMT: {
        zan_loc_t loc = s->loc;
        if (s->yield_stmt.value) {
            iter_fix_expr(s->yield_stmt.value, c);
            c->next_state++;
            int st = c->next_state;
            char lbuf[16];
            snprintf(lbuf, sizeof(lbuf), "R%d", st);
            zan_ast_node_t *b = zan_ast_new(c->p->arena, AST_BLOCK, loc);
            zan_ast_list_init(&b->block.stmts);
            zan_ast_node_t *e1 = zan_ast_new(c->p->arena, AST_EXPR_STMT, loc);
            e1->expr_stmt.expr = iter_assign(c->p,
                iter_ident(c->p, "__cur", loc), s->yield_stmt.value, loc);
            zan_ast_node_t *e2 = zan_ast_new(c->p->arena, AST_EXPR_STMT, loc);
            e2->expr_stmt.expr = iter_assign(c->p,
                iter_ident(c->p, "__state", loc),
                iter_int_const(c->p, st, loc), loc);
            zan_ast_node_t *rt = zan_ast_new(c->p->arena, AST_RETURN_STMT, loc);
            rt->ret.value = iter_bool(c->p, true, loc);
            zan_ast_node_t *lb = zan_ast_new(c->p->arena, AST_LABEL_STMT, loc);
            lb->ident.name = iter_intern(c->p, lbuf);
            zan_ast_list_push(&b->block.stmts, e1, c->p->arena);
            zan_ast_list_push(&b->block.stmts, e2, c->p->arena);
            zan_ast_list_push(&b->block.stmts, rt, c->p->arena);
            zan_ast_list_push(&b->block.stmts, lb, c->p->arena);
            s->kind = AST_BLOCK;
            s->block.stmts = b->block.stmts;
        } else {
            s->kind = AST_RETURN_STMT;
            s->ret.value = iter_bool(c->p, false, loc);
        }
        return;
    }
    case AST_BLOCK:
        iter_rewrite_stmts(&s->block.stmts, c);
        return;
    case AST_EXPR_STMT:
        iter_fix_expr(s->expr_stmt.expr, c);
        return;
    case AST_RETURN_STMT:
        iter_fix_expr(s->ret.value, c);
        return;
    case AST_IF_STMT:
        iter_fix_expr(s->if_stmt.cond, c);
        iter_rewrite_stmt(s->if_stmt.then_body, c);
        iter_rewrite_stmt(s->if_stmt.else_body, c);
        return;
    case AST_WHILE_STMT:
        iter_fix_expr(s->while_stmt.cond, c);
        iter_rewrite_stmt(s->while_stmt.body, c);
        return;
    case AST_DO_WHILE_STMT:
        iter_fix_expr(s->while_stmt.cond, c);
        iter_rewrite_stmt(s->while_stmt.body, c);
        return;
    case AST_FOR_STMT:
        if (s->for_stmt.init) {
            if (s->for_stmt.init->kind == AST_VAR_DECL)
                iter_hoist_vardecl(s->for_stmt.init, c);
            else
                iter_fix_expr(s->for_stmt.init, c);
        }
        iter_fix_expr(s->for_stmt.cond, c);
        iter_fix_expr(s->for_stmt.step, c);
        iter_rewrite_stmt(s->for_stmt.body, c);
        return;
    case AST_FOREACH_STMT:
        iter_fix_expr(s->foreach_stmt.collection, c);
        iter_rewrite_stmt(s->foreach_stmt.body, c);
        return;
    case AST_SWITCH_STMT:
        iter_fix_expr(s->switch_stmt.expr, c);
        for (int i = 0; i < s->switch_stmt.cases.count; i++)
            iter_rewrite_stmt(s->switch_stmt.cases.items[i], c);
        return;
    case AST_SWITCH_CASE:
        iter_rewrite_stmts(&s->switch_case.body->block.stmts, c);
        return;
    case AST_THROW_STMT:
        iter_fix_expr(s->throw_stmt.value, c);
        return;
    case AST_LOCK_STMT:
        iter_fix_expr(s->lock_stmt.expr, c);
        iter_rewrite_stmt(s->lock_stmt.body, c);
        return;
    case AST_VAR_DECL:
        iter_fix_expr(s->var_decl.initializer, c);
        iter_hoist_vardecl(s, c);
        return;
    case AST_TRY_STMT:
        iter_rewrite_stmt(s->try_stmt.try_body, c);
        if (s->try_stmt.finally_body)
            iter_rewrite_stmt(s->try_stmt.finally_body, c);
        return;
    default:
        return;
    }
}

static void iter_desugar_method(zan_parser_t *p, zan_ast_node_t *enc,
                                zan_ast_node_t *m, zan_ast_node_t *unit) {
    zan_ast_node_t *rt = m->method_decl.return_type;
    if (!rt || rt->kind != AST_TYPE_REF || rt->type_ref.type_args.count != 1)
        return;
    if (!(rt->type_ref.name.len == 11 &&
          memcmp(rt->type_ref.name.str, "IEnumerable", 11) == 0))
        return;
    if (!m->method_decl.body || m->method_decl.body->kind != AST_BLOCK)
        return;
    if (!stmt_contains_yield(m->method_decl.body)) return;
    if (!(m->method_decl.modifiers & MOD_STATIC)) {
        zan_diag_emit_code(p->diag, DIAG_ERROR, m->loc, 60,
            "iterator method '%.*s' must be static in this release "
            "(instance iterators are not synthesized yet)",
            (int)m->method_decl.name.len, m->method_decl.name.str);
        return;
    }

    iter_ctx c;
    memset(&c, 0, sizeof(c));
    c.p = p;
    c.elem_ty = rt->type_ref.type_args.items[0];
    c.next_state = 0;
    c.enc_name = enc->type_decl.name;
    c.enc_members = &enc->type_decl.members;

    char clsbuf[96];
    snprintf(clsbuf, sizeof(clsbuf), "__Iter_%.*s_%d",
             (int)m->method_decl.name.len, m->method_decl.name.str,
             p->synth_counter++);

    zan_ast_node_t *cls = zan_ast_new(p->arena, AST_CLASS_DECL, m->loc);
    cls->type_decl.name = iter_intern(p, clsbuf);
    zan_ast_list_init(&cls->type_decl.type_params);
    zan_ast_list_init(&cls->type_decl.bases);
    zan_ast_list_init(&cls->type_decl.members);
    cls->type_decl.modifiers = 0;
    cls->type_decl.is_c_layout = false;
    cls->type_decl.is_explicit_layout = false;
    cls->type_decl.where_clauses = NULL;
    cls->type_decl.nested_host = NULL;
    c.cls = cls;

    zan_ast_node_t *ibase = iter_type_ref(p, "IEnumerator", m->loc);
    zan_ast_list_push(&ibase->type_ref.type_args, c.elem_ty, p->arena);
    zan_ast_list_push(&cls->type_decl.bases, ibase, p->arena);
    /* C# 同形：迭代器同时实现 IEnumerable<T>（GetEnumerator 返回 this），
     * 方法声明的 IEnumerable<T> 返回类型才有隐式转换可走。 */
    zan_ast_node_t *ebase = iter_type_ref(p, "IEnumerable", m->loc);
    zan_ast_list_push(&ebase->type_ref.type_args, c.elem_ty, p->arena);
    zan_ast_list_push(&cls->type_decl.bases, ebase, p->arena);

    iter_add_field(&c, "__state", iter_type_ref(p, "int", m->loc), m->loc);
    iter_add_field(&c, "__cur", c.elem_ty, m->loc);

    /* 形参提升为字段；ctor 逐个 this.X = X。 */
    zan_ast_list_t *mps = &m->method_decl.params;
    zan_ast_node_t *ctor = zan_ast_new(p->arena, AST_CONSTRUCTOR_DECL, m->loc);
    ctor->method_decl.name = cls->type_decl.name;
    zan_ast_list_init(&ctor->method_decl.params);
    zan_ast_node_t *cbody = zan_ast_new(p->arena, AST_BLOCK, m->loc);
    zan_ast_list_init(&cbody->block.stmts);
    ctor->method_decl.body = cbody;
    ctor->method_decl.modifiers = MOD_PUBLIC;
    for (int i = 0; i < mps->count; i++) {
        zan_ast_node_t *pm = mps->items[i];
        if (!pm || pm->kind != AST_PARAM) continue;
        char nbuf[64];
        int n = pm->param.name.len < 63 ? (int)pm->param.name.len : 63;
        memcpy(nbuf, pm->param.name.str, (size_t)n);
        nbuf[n] = 0;
        iter_add_field(&c, nbuf, pm->param.type, m->loc);
        zan_ast_node_t *np = zan_ast_new(p->arena, AST_PARAM, m->loc);
        np->param.name = pm->param.name;
        np->param.type = pm->param.type;
        np->param.default_val = NULL;
        np->param.is_params = 0;
        np->param.by_ref = 0;
        np->param.is_this = 0;
        zan_ast_list_push(&ctor->method_decl.params, np, p->arena);
        zan_ast_node_t *as = zan_ast_new(p->arena, AST_EXPR_STMT, m->loc);
        as->expr_stmt.expr = iter_assign(p,
            iter_member(p, zan_ast_new(p->arena, AST_THIS_EXPR, m->loc),
                        nbuf, m->loc),
            iter_ident(p, nbuf, m->loc), m->loc);
        zan_ast_list_push(&cbody->block.stmts, as, p->arena);
        if (c.hoist_count < 32) {
            snprintf(c.hoist[c.hoist_count], 64, "%s", nbuf);
            c.hoist_count++;
        }
    }
    zan_ast_list_push(&cls->type_decl.members, ctor, p->arena);

    /* MoveNext：原体改写（提升局部/改写 yield）+ 前导状态分派 + 尾部
     * return false。 */
    iter_check_stmt_pos(m->method_decl.body, &c, 0, 0);
    iter_rewrite_stmts(&m->method_decl.body->block.stmts, &c);
    zan_ast_node_t *mb = zan_ast_new(p->arena, AST_BLOCK, m->loc);
    zan_ast_list_init(&mb->block.stmts);
    for (int st = 1; st <= c.next_state; st++) {
        char lbuf[16];
        snprintf(lbuf, sizeof(lbuf), "R%d", st);
        zan_ast_node_t *ifs = zan_ast_new(p->arena, AST_IF_STMT, m->loc);
        ifs->if_stmt.cond = iter_bin_eq_state(p, st, m->loc);
        zan_ast_node_t *tb = zan_ast_new(p->arena, AST_BLOCK, m->loc);
        zan_ast_list_init(&tb->block.stmts);
        zan_ast_node_t *gt = zan_ast_new(p->arena, AST_GOTO_STMT, m->loc);
        gt->ident.name = iter_intern(p, lbuf);
        zan_ast_list_push(&tb->block.stmts, gt, p->arena);
        ifs->if_stmt.then_body = tb;
        ifs->if_stmt.else_body = NULL;
        zan_ast_list_push(&mb->block.stmts, ifs, p->arena);
    }
    for (int i = 0; i < m->method_decl.body->block.stmts.count; i++)
        zan_ast_list_push(&mb->block.stmts,
                          m->method_decl.body->block.stmts.items[i], p->arena);
    zan_ast_node_t *rf = zan_ast_new(p->arena, AST_RETURN_STMT, m->loc);
    rf->ret.value = iter_bool(p, false, m->loc);
    zan_ast_list_push(&mb->block.stmts, rf, p->arena);
    /* 残留 yield（catch 体内等不支持位置）：报错而非静默 */
    for (int i = 0; i < mb->block.stmts.count; i++)
        iter_check_residual(mb->block.stmts.items[i], p);

    zan_ast_node_t *mn = zan_ast_new(p->arena, AST_METHOD_DECL, m->loc);
    mn->method_decl.name = (zan_istr_t){ (char *)"MoveNext", 8 };
    mn->method_decl.return_type = iter_type_ref(p, "bool", m->loc);
    zan_ast_list_init(&mn->method_decl.params);
    zan_ast_list_init(&mn->method_decl.type_params);
    mn->method_decl.body = mb;
    mn->method_decl.modifiers = MOD_PUBLIC;
    mn->method_decl.is_variadic = 0;
    zan_ast_list_push(&cls->type_decl.members, mn, p->arena);

    zan_ast_node_t *cu = zan_ast_new(p->arena, AST_METHOD_DECL, m->loc);
    cu->method_decl.name = (zan_istr_t){ (char *)"Current", 7 };
    cu->method_decl.return_type = c.elem_ty;
    zan_ast_list_init(&cu->method_decl.params);
    zan_ast_list_init(&cu->method_decl.type_params);
    zan_ast_node_t *cb = zan_ast_new(p->arena, AST_BLOCK, m->loc);
    zan_ast_list_init(&cb->block.stmts);
    zan_ast_node_t *cr = zan_ast_new(p->arena, AST_RETURN_STMT, m->loc);
    cr->ret.value = iter_ident(p, "__cur", m->loc);
    zan_ast_list_push(&cb->block.stmts, cr, p->arena);
    cu->method_decl.body = cb;
    cu->method_decl.modifiers = MOD_PUBLIC;
    cu->method_decl.is_variadic = 0;
    zan_ast_list_push(&cls->type_decl.members, cu, p->arena);

    zan_ast_node_t *ge = zan_ast_new(p->arena, AST_METHOD_DECL, m->loc);
    ge->method_decl.name = (zan_istr_t){ (char *)"GetEnumerator", 13 };
    zan_ast_node_t *gret = iter_type_ref(p, "IEnumerator", m->loc);
    zan_ast_list_push(&gret->type_ref.type_args, c.elem_ty, p->arena);
    ge->method_decl.return_type = gret;
    zan_ast_list_init(&ge->method_decl.params);
    zan_ast_list_init(&ge->method_decl.type_params);
    zan_ast_node_t *gb = zan_ast_new(p->arena, AST_BLOCK, m->loc);
    zan_ast_list_init(&gb->block.stmts);
    zan_ast_node_t *gr = zan_ast_new(p->arena, AST_RETURN_STMT, m->loc);
    gr->ret.value = zan_ast_new(p->arena, AST_THIS_EXPR, m->loc);
    zan_ast_list_push(&gb->block.stmts, gr, p->arena);
    ge->method_decl.body = gb;
    ge->method_decl.modifiers = MOD_PUBLIC;
    ge->method_decl.is_variadic = 0;
    zan_ast_list_push(&cls->type_decl.members, ge, p->arena);

    /* 原方法体 → return new __Iter_F(实参...); 文件缺 using 时补一条。 */
    zan_ast_node_t *nb = zan_ast_new(p->arena, AST_BLOCK, m->loc);
    zan_ast_list_init(&nb->block.stmts);
    zan_ast_node_t *nw = zan_ast_new(p->arena, AST_NEW_EXPR, m->loc);
    nw->new_expr.type = iter_type_ref_istr(p, cls->type_decl.name, m->loc);
    zan_ast_list_init(&nw->new_expr.args);
    nw->new_expr.is_array = false;
    for (int i = 0; i < mps->count; i++) {
        zan_ast_node_t *pm = mps->items[i];
        if (pm && pm->kind == AST_PARAM)
            zan_ast_list_push(&nw->new_expr.args,
                              iter_istr_ident(p, pm->param.name, m->loc),
                              p->arena);
    }
    zan_ast_node_t *nret = zan_ast_new(p->arena, AST_RETURN_STMT, m->loc);
    nret->ret.value = nw;
    zan_ast_list_push(&nb->block.stmts, nret, p->arena);
    m->method_decl.body = nb;

    /* IEnumerator<T> 的 using 兜底：源文件没带就补一条。 */
    bool has_generic_using = false;
    for (int i = 0; i < unit->comp_unit.usings.count; i++) {
        zan_ast_node_t *u = unit->comp_unit.usings.items[i];
        if (u && u->kind == AST_USING_DECL && u->using_decl.name &&
            u->using_decl.name->kind == AST_QUALIFIED_NAME &&
            u->using_decl.name->qualified_name.parts.count == 3) {
            zan_ast_node_t *p0 =
                u->using_decl.name->qualified_name.parts.items[0];
            zan_ast_node_t *p2 =
                u->using_decl.name->qualified_name.parts.items[2];
            if (p0 && p2 && p0->kind == AST_IDENTIFIER &&
                p2->kind == AST_IDENTIFIER &&
                p0->ident.name.len == 6 &&
                memcmp(p0->ident.name.str, "System", 6) == 0 &&
                p2->ident.name.len == 7 &&
                memcmp(p2->ident.name.str, "Generic", 7) == 0)
                has_generic_using = true;
        }
    }
    if (!has_generic_using) {
        zan_ast_node_t *un = zan_ast_new(p->arena, AST_USING_DECL, m->loc);
        un->using_decl.is_static = false;
        zan_ast_node_t *qn = zan_ast_new(p->arena, AST_QUALIFIED_NAME, m->loc);
        zan_ast_list_init(&qn->qualified_name.parts);
        zan_ast_list_push(&qn->qualified_name.parts,
                          iter_ident(p, "System", m->loc), p->arena);
        zan_ast_list_push(&qn->qualified_name.parts,
                          iter_ident(p, "Collections", m->loc), p->arena);
        zan_ast_list_push(&qn->qualified_name.parts,
                          iter_ident(p, "Generic", m->loc), p->arena);
        un->using_decl.name = qn;
        zan_ast_list_push(&unit->comp_unit.usings, un, p->arena);
    }

    zan_ast_list_push(&unit->comp_unit.decls, cls, p->arena);
}

static void iter_desugar_unit(zan_parser_t *p, zan_ast_node_t *unit) {
    for (int i = 0; i < unit->comp_unit.decls.count; i++) {
        zan_ast_node_t *d = unit->comp_unit.decls.items[i];
        if (!d || (d->kind != AST_CLASS_DECL && d->kind != AST_STRUCT_DECL))
            continue;
        for (int k = 0; k < d->type_decl.members.count; k++) {
            zan_ast_node_t *mm = d->type_decl.members.items[k];
            if (mm && mm->kind == AST_METHOD_DECL)
                iter_desugar_method(p, d, mm, unit);
        }
    }
}

