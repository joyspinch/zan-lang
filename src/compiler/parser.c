
#include "parser.h"
#include "arena.h"
#include "diag.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

#define ZAN_PARSER_MAX_TYPE_DEPTH 4096

static void parser_advance(zan_parser_t *p) {
    p->previous = p->current;
    p->current = zan_lexer_next(p->lex);
}

static bool parser_check(zan_parser_t *p, zan_token_kind_t kind) {
    return p->current.kind == kind;
}

static bool parser_match(zan_parser_t *p, zan_token_kind_t kind) {
    if (p->current.kind == kind) {
        parser_advance(p);
        return true;
    }
    return false;
}

static void parser_expect(zan_parser_t *p, zan_token_kind_t kind) {
    if (p->current.kind == kind) {
        parser_advance(p);
        return;
    }
    zan_diag_emit_code(p->diag, DIAG_ERROR, p->current.loc, 32,
                  "expected '%s', got '%s'",
                  zan_token_kind_name(kind),
                  zan_token_kind_name(p->current.kind));
}

static zan_ast_node_t *parser_error_node(zan_parser_t *p) {
    /* 毒化节点而非 int 常量 0：后者会让 checker 把它当 int 校验，
     * 喷出 "cannot access member on int" 之类风马牛不相及的级联误报。 */
    return zan_ast_new(p->arena, AST_ERROR_EXPR, p->current.loc);
}

/* 保留字不能当名字用（base / out / value ... 都是关键字）。
 * 撞上时直接点名道姓，别只丢一句 "expected variable name"——
 * 那让人怀疑自己语法写错了，实际只是撞了保留字。
 * 坑出处：外部项目里 agent 连撞四次 base/out，134 条报错全在说别的。 */
static const char *const ZAN_RESERVED_WORD_MSG =
    "'%s' is a reserved word and cannot be used as a name here";

/* 名字槽上撞到保留字：点名 + 吃掉 token，词面留给调用方当占位名。
 * 吃掉的理由：留着的话外层会接着补一串级联报错（"expected ')'" /
 * "expected ';'" / "expected type"……），把真正的原因盖住——那正是
 * 这条要治的病。返回 false 表示当前 token 不是保留字，调用方走通用分支。 */
static bool parser_take_reserved_word(zan_parser_t *p, zan_istr_t *name_out) {
    const char *word = zan_lexer_keyword_text(p->current.kind);
    if (!word) return false;
    zan_diag_emit_code(p->diag, DIAG_ERROR, p->current.loc, 33, ZAN_RESERVED_WORD_MSG, word);
    parser_advance(p);
    if (name_out) {
        name_out->str = (char *)word;
        name_out->len = (uint32_t)strlen(word);
    }
    return true;
}

/* "这里应该是个名字"的位置统一走这里：
 * 撞上保留字就说清楚，否则退回原来的通用消息。 */
static void parser_diag_expected_name(zan_parser_t *p, zan_loc_t loc, const char *generic) {
    const char *word = zan_lexer_keyword_text(p->current.kind);
    if (word) {
        zan_diag_emit_code(p->diag, DIAG_ERROR, p->current.loc, 33, ZAN_RESERVED_WORD_MSG, word);
    } else {
        zan_diag_emit_code(p->diag, DIAG_ERROR, loc, 34, "%s", generic);
    }
}

static void parser_expect_gt(zan_parser_t *p) {
    switch (p->current.kind) {
    case TK_GREATER:
        parser_advance(p);
        return;
    case TK_GREATER_GREATER:
        p->current.kind = TK_GREATER;
        p->current.loc.col += 1;
        return;
    case TK_GREATER_GREATER_GREATER:
        p->current.kind = TK_GREATER_GREATER;
        p->current.loc.col += 1;
        return;
    case TK_GREATER_GREATER_EQ:
        p->current.kind = TK_GREATER_EQ;
        p->current.loc.col += 1;
        return;
    case TK_GREATER_GREATER_GREATER_EQ:
        p->current.kind = TK_GREATER_GREATER_EQ;
        p->current.loc.col += 1;
        return;
    default:
        zan_diag_emit_code(p->diag, DIAG_ERROR, p->current.loc, 32,
                      "expected '%s', got '%s'",
                      zan_token_kind_name(TK_GREATER),
                      zan_token_kind_name(p->current.kind));
    }
}

static zan_ast_node_t *parse_expression(zan_parser_t *p);
static zan_ast_node_t *parse_statement(zan_parser_t *p);
static zan_ast_node_t *parse_block(zan_parser_t *p);
static zan_ast_node_t *parse_embedded_stmt(zan_parser_t *p);
static bool looks_like_var_decl(zan_parser_t *p);
static zan_ast_node_t *parse_type_ref(zan_parser_t *p);
static zan_ast_node_t *parse_type_decl(zan_parser_t *p, uint32_t modifiers);

static void splice_pending_stmts(zan_parser_t *p, zan_ast_list_t *list) {
    for (int i = 0; i < p->pending_stmts.count; i++) {
        zan_ast_list_push(list, p->pending_stmts.items[i], p->arena);
    }
    p->pending_stmts.count = 0;
}
static void gen_record_class(zan_ast_node_t *unit, zan_istr_t rname,
                             zan_ast_list_t *params, zan_arena_t *arena,
                             zan_diag_t *diag);
static zan_ast_node_t *parse_unary(zan_parser_t *p);
static zan_ast_node_t *parse_parameter(zan_parser_t *p);
static uint32_t parse_modifiers(zan_parser_t *p);
static zan_ast_list_t parse_param_list(zan_parser_t *p);
static void parse_attr_usages(zan_parser_t *p, zan_ast_list_t *out,
                              zan_istr_t *out_lib, zan_istr_t *out_entry,
                              bool *out_variadic);
static bool parse_top_level_decl(zan_parser_t *p, zan_ast_node_t *unit);


/* Composite Dispatcher Submodules */
#include "parser_type.c"
#include "parser_expr_primary.c"
#include "parser_expr.c"
#include "parser_stmt_decl.c"
#include "parser_stmt.c"
#include "parser_member.c"
#include "parser_type_decl.c"
#include "parser_unit.c"
