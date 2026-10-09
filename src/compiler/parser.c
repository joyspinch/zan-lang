/* 模块核心语义抽象与接口调用契约 */

#include "parser.h"
#include "arena.h"
#include "diag.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

/* 模块核心语义抽象与接口调用契约 */
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
    zan_diag_emit(p->diag, DIAG_ERROR, p->current.loc,
                  "expected '%s', got '%s'",
                  zan_token_kind_name(kind),
                  zan_token_kind_name(p->current.kind));
}

static zan_ast_node_t *parser_error_node(zan_parser_t *p) {
    return zan_ast_new(p->arena, AST_INT_LITERAL, p->current.loc);
}

/* 底层系统交互与数据协议契约 */
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
        zan_diag_emit(p->diag, DIAG_ERROR, p->current.loc,
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

/* 内部辅助逻辑 */
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

/* 内部辅助逻辑 */
static zan_ast_node_t *parse_delegate_decl(zan_parser_t *p, uint32_t mods) {
    parser_advance(p);
    zan_loc_t dloc = p->current.loc;
    zan_ast_node_t *ret_type = parse_type_ref(p);
    parser_expect(p, TK_IDENT);
    zan_istr_t dname = p->previous.str_val;
    zan_ast_list_t dtype_params;
    zan_ast_list_init(&dtype_params);
    if (parser_match(p, TK_LESS)) {
        while (!parser_check(p, TK_GREATER) && !parser_check(p, TK_EOF)) {
            if (parser_check(p, TK_IDENT)) {
                parser_advance(p);
                zan_ast_node_t *tp = zan_ast_new(p->arena, AST_IDENTIFIER, p->previous.loc);
                tp->ident.name = p->previous.str_val;
                zan_ast_list_push(&dtype_params, tp, p->arena);
            }
            if (!parser_match(p, TK_COMMA)) break;
        }
        parser_expect(p, TK_GREATER);
    }
    zan_ast_list_t dparams = parse_param_list(p);
    parser_expect(p, TK_SEMICOLON);
    zan_ast_node_t *ddecl = zan_ast_new(p->arena, AST_DELEGATE_DECL, dloc);
    ddecl->method_decl.name = dname;
    ddecl->method_decl.return_type = ret_type;
    ddecl->method_decl.params = dparams;
    ddecl->method_decl.type_params = dtype_params;
    ddecl->method_decl.body = NULL;
    ddecl->method_decl.modifiers = mods;
    return ddecl;
}

/* 内部辅助实现 */
static bool parse_top_level_decl(zan_parser_t *p, zan_ast_node_t *unit) {
    /* 解析类型特性：保留特性元数据，[StructLayout] 触发 C 内存布局对齐 */
    bool has_c_layout = false;
    bool has_explicit_layout = false;
    zan_ast_list_t type_attrs;
    zan_ast_list_init(&type_attrs);
    parse_attr_usages(p, &type_attrs, NULL, NULL, NULL);
    for (int _ai = 0; _ai < type_attrs.count; _ai++) {
        zan_istr_t _n = type_attrs.items[_ai]->attribute.name->ident.name;
        if (_n.str && _n.len == 12 && memcmp(_n.str, "StructLayout", 12) == 0) {
            has_c_layout = true;
            /* `LayoutKind */
            zan_ast_list_t *_args = &type_attrs.items[_ai]->attribute.args;
            for (int _aj = 0; _aj < _args->count; _aj++) {
                zan_ast_node_t *_a = _args->items[_aj];
                zan_istr_t _k = {NULL, 0};
                if (_a->kind == AST_MEMBER_ACCESS) _k = _a->member.name;
                else if (_a->kind == AST_IDENTIFIER) _k = _a->ident.name;
                if (_k.str && _k.len == 8 && memcmp(_k.str, "Explicit", 8) == 0)
                    has_explicit_layout = true;
            }
        }
    }

    uint32_t mods = parse_modifiers(p);

    /* 底层系统交互与数据协议契约 */
    if (parser_check(p, TK_IDENT) && p->current.str_val.len == 7 &&
        memcmp(p->current.str_val.str, "partial", 7) == 0) {
        zan_token_kind_t nk = zan_lexer_peek(p->lex).kind;
        if (nk == TK_CLASS || nk == TK_STRUCT || nk == TK_INTERFACE) {
            parser_advance(p);
            mods |= MOD_PARTIAL;
        }
    }

    /* 底层系统交互与数据协议契约 */
    if (parser_check(p, TK_IDENT) && p->current.str_val.len == 6 &&
        memcmp(p->current.str_val.str, "record", 6) == 0 &&
        zan_lexer_peek(p->lex).kind == TK_IDENT) {
        parser_advance(p);
        parser_expect(p, TK_IDENT);
        zan_istr_t rname = p->previous.str_val;
        zan_ast_list_t rparams = parse_param_list(p);
        parser_expect(p, TK_SEMICOLON);
        gen_record_class(unit, rname, &rparams, p->arena, p->diag);
        return true;
    }

    if (parser_check(p, TK_CLASS) || parser_check(p, TK_STRUCT) ||
        parser_check(p, TK_INTERFACE) || parser_check(p, TK_ENUM)) {
        zan_ast_node_t *decl = parse_type_decl(p, mods);
        decl->type_decl.is_c_layout = has_c_layout;
        decl->type_decl.is_explicit_layout = has_explicit_layout;
        if (type_attrs.count > 0)
            zan_ast_ensure_decl_meta(decl, p->arena)->attributes = type_attrs;
        zan_ast_list_push(&unit->comp_unit.decls, decl, p->arena);
        return true;
    }
    if (parser_check(p, TK_DELEGATE)) {
        zan_ast_node_t *ddecl = parse_delegate_decl(p, mods);
        zan_ast_list_push(&unit->comp_unit.decls, ddecl, p->arena);
        return true;
    }
    zan_diag_emit(p->diag, DIAG_ERROR, p->current.loc,
                  "expected type declaration (class, struct, interface, enum, or delegate)");
    return false;
}

static zan_ast_node_t *parse_qualified_name(zan_parser_t *p) {
    zan_loc_t loc = p->current.loc;
    zan_ast_node_t *node = zan_ast_new(p->arena, AST_QUALIFIED_NAME, loc);
    zan_ast_list_init(&node->qualified_name.parts);

    parser_expect(p, TK_IDENT);
    zan_ast_node_t *part = zan_ast_new(p->arena, AST_IDENTIFIER, p->previous.loc);
    part->ident.name = p->previous.str_val;
    zan_ast_list_push(&node->qualified_name.parts, part, p->arena);

    while (parser_match(p, TK_DOT)) {
        if (!parser_check(p, TK_IDENT)) break;
        parser_advance(p);
        part = zan_ast_new(p->arena, AST_IDENTIFIER, p->previous.loc);
        part->ident.name = p->previous.str_val;
        zan_ast_list_push(&node->qualified_name.parts, part, p->arena);
    }

    return node;
}

/* 内部辅助逻辑 */
static int array_suffix_rank(zan_parser_t *p) {
    zan_lexer_t *lx = p->lex;
    const char *s = lx->source;
    size_t i = lx->pos;
    size_t n = lx->source_len;
    while (i < n && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' ||
                     s[i] == '\n'))
        i++;
    if (i >= n) return 0;
    if (s[i] == ']') return 1;
    if (s[i] != ',') return 0;
    int rank = 1;
    while (i < n) {
        if (s[i] == ',') { rank++; i++; continue; }
        if (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n') {
            i++; continue;
        }
        if (s[i] == ']') return rank;
        return 0;
    }
    return 0;
}

static zan_ast_node_t *parse_type_ref(zan_parser_t *p) {
    zan_loc_t loc = p->current.loc;

    /* 内部辅助逻辑 */
    int nn_top = p->type_no_nullable;
    p->type_no_nullable = 0;

    /* 内部辅助逻辑 */
    if (++p->type_depth > ZAN_PARSER_MAX_TYPE_DEPTH) {
        zan_diag_emit(p->diag, DIAG_ERROR, loc,
                      "type nesting too deep (max %d)",
                      ZAN_PARSER_MAX_TYPE_DEPTH);
        p->type_depth--;
        return parser_error_node(p);
    }

    /* 内部辅助逻辑 */
    if (parser_check(p, TK_LPAREN)) {
        parser_advance(p);
        zan_ast_node_t *tn = zan_ast_new(p->arena, AST_TUPLE_TYPE, loc);
        zan_ast_list_init(&tn->tuple_type.elems);
        while (!parser_check(p, TK_RPAREN) && !parser_check(p, TK_EOF)) {
            zan_ast_node_t *et = parse_type_ref(p);
            zan_ast_list_push(&tn->tuple_type.elems, et, p->arena);
            /* 模块核心语义抽象与接口调用契约 */
            if (p->current.kind == TK_IDENT &&
                !(et->kind == AST_TYPE_REF && et->type_ref.is_array)) {
                /* 内部辅助逻辑 */
                parser_advance(p);
            }
            if (!parser_match(p, TK_COMMA)) break;
        }
        parser_expect(p, TK_RPAREN);
        p->type_depth--;
        return tn;
    }

    zan_istr_t name = {0};
    bool is_builtin = true;
    switch (p->current.kind) {
    case TK_INT:    name.str = "int";    name.len = 3; break;
    case TK_LONG:   name.str = "long";   name.len = 4; break;
    case TK_SHORT:  name.str = "short";  name.len = 5; break;
    case TK_BYTE:   name.str = "byte";   name.len = 4; break;
    case TK_UINT:   name.str = "uint";   name.len = 4; break;
    case TK_ULONG:  name.str = "ulong";  name.len = 5; break;
    case TK_USHORT: name.str = "ushort"; name.len = 6; break;
    case TK_SBYTE:  name.str = "sbyte";  name.len = 5; break;
    case TK_FLOAT:  name.str = "float";  name.len = 5; break;
    case TK_DOUBLE: name.str = "double"; name.len = 6; break;
    case TK_DECIMAL: name.str = "decimal"; name.len = 7; break;
    case TK_BOOL:   name.str = "bool";   name.len = 4; break;
    case TK_CHAR:   name.str = "char";   name.len = 4; break;
    case TK_STRING: name.str = "string"; name.len = 6; break;
    case TK_VOID:   name.str = "void";   name.len = 4; break;
    case TK_OBJECT: name.str = "object"; name.len = 6; break;
    case TK_NINT:   name.str = "nint";   name.len = 4; break;
    case TK_VAR:    name.str = "var";    name.len = 3; break;
    default: is_builtin = false; break;
    }

    if (is_builtin) {
        parser_advance(p);
    } else if (parser_check(p, TK_IDENT)) {
        parser_advance(p);
        name = p->previous.str_val;
        /* 核心系统底层抽象与内存语义契约 */
        if (parser_check(p, TK_DOT) &&
            zan_lexer_peek(p->lex).kind == TK_IDENT) {
            char qbuf[512];
            size_t qn = 0;
            for (uint32_t qi = 0; qi < name.len && qn < sizeof qbuf; qi++)
                qbuf[qn++] = name.str[qi];
            while (parser_check(p, TK_DOT) &&
                   zan_lexer_peek(p->lex).kind == TK_IDENT) {
                parser_advance(p);
                parser_advance(p);
                if (qn < sizeof qbuf - 1) qbuf[qn++] = '.';
                for (uint32_t qi = 0; qi < p->previous.str_val.len &&
                     qn < sizeof qbuf; qi++)
                    qbuf[qn++] = p->previous.str_val.str[qi];
            }
            name.str = zan_arena_strdup(p->arena, qbuf, qn);
            name.len = (uint32_t)qn;
        }
    } else {
        zan_diag_emit(p->diag, DIAG_ERROR, loc, "expected type");
        p->type_depth--;
        return parser_error_node(p);
    }

    zan_ast_node_t *type_node = zan_ast_new(p->arena, AST_TYPE_REF, loc);
    type_node->type_ref.name = name;
    type_node->type_ref.is_nullable = false;
    type_node->type_ref.is_array = false;
    zan_ast_list_init(&type_node->type_ref.type_args);

    if (parser_check(p, TK_LESS)) {
        parser_advance(p);
        while (!parser_check(p, TK_GREATER) && !parser_check(p, TK_EOF)) {
            zan_ast_node_t *arg = parse_type_ref(p);
            zan_ast_list_push(&type_node->type_ref.type_args, arg, p->arena);
            if (!parser_match(p, TK_COMMA)) break;
        }
        parser_expect_gt(p);
    }

    /* 核心系统底层抽象与内存语义契约 */
    int ranks[16];
    int nranks = 0;
    bool seen_array = false;
    for (;;) {
        if (nn_top && parser_check(p, TK_QUESTION)) {
            /* 内部辅助逻辑 */
            break;
        }
        if (parser_match(p, TK_QUESTION)) {
            if (seen_array) {
                /* 核心系统底层抽象与内存语义契约 */
            } else {
                type_node->type_ref.is_nullable = true;
            }
        } else if (parser_check(p, TK_LBRACKET)) {
            int rank = array_suffix_rank(p);
            if (rank <= 0) break;
            parser_advance(p);
            for (int c = 1; c < rank; c++) parser_advance(p);
            parser_advance(p);
            if (rank <= 16 && nranks < 16) {
                ranks[nranks++] = rank;
            } else {
                /* 内部辅助逻辑 */
                zan_diag_emit(p->diag, DIAG_ERROR, loc,
                              "array rank specifier is too deep (max 16)");
            }
            seen_array = true;
        } else {
            break;
        }
    }
    if (nranks > 0) {
        zan_ast_node_t *cur = type_node;
        for (int i = nranks - 1; i >= 0; i--) {
            zan_ast_node_t *w = zan_ast_new(p->arena, AST_TYPE_REF, loc);
            w->type_ref.name = cur->type_ref.name;
            w->type_ref.is_array = true;
            w->type_ref.array_rank = ranks[i];
            w->type_ref.array_element = cur;
            zan_ast_list_init(&w->type_ref.type_args);
            cur = w;
        }
        p->type_depth--;
        return cur;
    }

    p->type_depth--;
    return type_node;
}

/* 内部辅助逻辑 */
static bool is_init_accessor_kw(zan_parser_t *p) {
    return p->current.kind == TK_IDENT && p->current.str_val.len == 4 &&
           memcmp(p->current.str_val.str, "init", 4) == 0;
}

/* 内部辅助逻辑 */
static bool is_checked_use(zan_parser_t *p) {
    if (p->current.kind != TK_IDENT) return false;
    const char *s = p->current.str_val.str;
    int n = p->current.str_val.len;
    bool kw = (n == 7 && memcmp(s, "checked", 7) == 0) ||
              (n == 9 && memcmp(s, "unchecked", 9) == 0);
    if (!kw) return false;
    zan_token_kind_t nxt = zan_lexer_peek(p->lex).kind;
    return nxt == TK_LPAREN || nxt == TK_LBRACE;
}

static uint32_t parse_modifiers(zan_parser_t *p) {
    uint32_t mods = 0;
    for (;;) {
        switch (p->current.kind) {
        case TK_PUBLIC:    parser_advance(p); mods |= MOD_PUBLIC;    break;
        case TK_PRIVATE:   parser_advance(p); mods |= MOD_PRIVATE;   break;
        case TK_PROTECTED: parser_advance(p); mods |= MOD_PROTECTED; break;
        case TK_INTERNAL:  parser_advance(p); mods |= MOD_INTERNAL;  break;
        case TK_STATIC:    parser_advance(p); mods |= MOD_STATIC;    break;
        case TK_VIRTUAL:   parser_advance(p); mods |= MOD_VIRTUAL;   break;
        case TK_OVERRIDE:  parser_advance(p); mods |= MOD_OVERRIDE;  break;
        case TK_ABSTRACT:  parser_advance(p); mods |= MOD_ABSTRACT;  break;
        case TK_SEALED:    parser_advance(p); mods |= MOD_SEALED;    break;
        case TK_READONLY:  parser_advance(p); mods |= MOD_READONLY;  break;
        /* 内部辅助逻辑 */
        case TK_CONST:     parser_advance(p);
                           mods |= MOD_STATIC | MOD_READONLY; break;
        case TK_EXTERN:    parser_advance(p); mods |= MOD_EXTERN;    break;
        case TK_ASYNC:     parser_advance(p); mods |= MOD_ASYNC;     break;
        case TK_UNSAFE:    parser_advance(p); mods |= MOD_UNSAFE;    break;
        case TK_WEAK:      parser_advance(p); mods |= MOD_WEAK;      break;
        /* 核心系统底层抽象与内存语义契约 */
        case TK_REF:       parser_advance(p); mods |= MOD_REF;       break;
        default: return mods;
        }
    }
}

/* 内部辅助逻辑 */
static bool paren_is_lambda(zan_parser_t *p) {
    zan_lexer_t saved_lex = *p->lex;
    zan_token_t saved_cur = p->current;
    zan_token_t saved_prev = p->previous;
    int depth = 0;
    bool result = false;
    while (p->current.kind != TK_EOF) {
        if (p->current.kind == TK_LPAREN) {
            depth++;
        } else if (p->current.kind == TK_RPAREN) {
            depth--;
            if (depth == 0) {
                result = (zan_lexer_peek(p->lex).kind == TK_ARROW);
                break;
            }
        }
        parser_advance(p);
    }
    *p->lex = saved_lex;
    p->current = saved_cur;
    p->previous = saved_prev;
    return result;
}

/* (Name)operand 显式类型转换表达式语法消歧 */
static bool paren_is_named_cast(zan_parser_t *p) {
    if (zan_lexer_peek(p->lex).kind != TK_IDENT) return false;
    zan_lexer_t saved_lex = *p->lex;
    zan_token_t saved_cur = p->current;
    zan_token_t saved_prev = p->previous;
    bool result = false;
    parser_advance(p);
    parser_advance(p);
    if (p->current.kind == TK_RPAREN) {
        switch (zan_lexer_peek(p->lex).kind) {
        case TK_IDENT: case TK_INT_LIT: case TK_FLOAT_LIT:
        case TK_STRING_LIT: case TK_CHAR_LIT:
        case TK_THIS: case TK_NEW: case TK_LPAREN:
            result = true;
            break;
        default:
            break;
        }
    }
    *p->lex = saved_lex;
    p->current = saved_cur;
    p->previous = saved_prev;
    return result;
}

/* 内部辅助逻辑 */
static zan_ast_node_t *parse_lambda_param(zan_parser_t *p) {
    zan_loc_t loc = p->current.loc;
    if (parser_check(p, TK_IDENT)) {
        zan_token_kind_t after = zan_lexer_peek(p->lex).kind;
        if (after == TK_COMMA || after == TK_RPAREN) {
            parser_advance(p);
            zan_ast_node_t *pn = zan_ast_new(p->arena, AST_PARAM, loc);
            pn->param.name = p->previous.str_val;
            pn->param.type = NULL;
            pn->param.default_val = NULL;
            return pn;
        }
    }
    zan_ast_node_t *type = parse_type_ref(p);
    zan_istr_t name = {0};
    if (parser_check(p, TK_IDENT)) {
        parser_advance(p);
        name = p->previous.str_val;
    }
    zan_ast_node_t *pn = zan_ast_new(p->arena, AST_PARAM, loc);
    pn->param.name = name;
    pn->param.type = type;
    pn->param.default_val = NULL;
    return pn;
}

/* 底层系统交互与数据协议契约 */
static zan_ast_node_t *parse_lambda_paren(zan_parser_t *p, zan_loc_t loc) {
    zan_ast_node_t *n = zan_ast_new(p->arena, AST_LAMBDA, loc);
    zan_ast_list_init(&n->lambda.params);
    parser_expect(p, TK_LPAREN);
    while (!parser_check(p, TK_RPAREN) && !parser_check(p, TK_EOF)) {
        zan_ast_node_t *param = parse_lambda_param(p);
        zan_ast_list_push(&n->lambda.params, param, p->arena);
        if (!parser_match(p, TK_COMMA)) break;
    }
    parser_expect(p, TK_RPAREN);
    parser_expect(p, TK_ARROW);
    if (parser_check(p, TK_LBRACE)) {
        n->lambda.body = parse_block(p);
    } else {
        n->lambda.body = parse_expression(p);
    }
    return n;
}

static bool is_type_kw(zan_token_kind_t k);

/* 内部辅助逻辑 */
static zan_ast_node_t *parse_call_arg(zan_parser_t *p);

static zan_ast_node_t *parse_primary(zan_parser_t *p) {
    zan_loc_t loc = p->current.loc;

    /* checked( */
    if (is_checked_use(p)) {
        bool is_checked = p->current.str_val.len == 7;
        zan_loc_t cloc = p->current.loc;
        parser_advance(p);
        if (is_checked) p->checked_depth++; else p->unchecked_depth++;
        if (parser_check(p, TK_LPAREN)) {
            parser_advance(p);
            zan_ast_node_t *inner = parse_expression(p);
            parser_expect(p, TK_RPAREN);
            if (is_checked) p->checked_depth--; else p->unchecked_depth--;
            zan_ast_node_t *n = zan_ast_new(p->arena, AST_CHECKED_STMT, cloc);
            n->checked_stmt.checked = is_checked;
            n->checked_stmt.body = inner;
            return n;
        }
        /* checked { */
        zan_ast_node_t *blk = parse_block(p);
        if (is_checked) p->checked_depth--; else p->unchecked_depth--;
        zan_ast_node_t *n = zan_ast_new(p->arena, AST_CHECKED_STMT, cloc);
        n->checked_stmt.checked = is_checked;
        n->checked_stmt.body = blk;
        return n;
    }

    /* 核心系统底层抽象与内存语义契约 */
    if (is_type_kw(p->current.kind) && zan_lexer_peek(p->lex).kind == TK_DOT) {
        const char *nm = zan_token_kind_name(p->current.kind);
        parser_advance(p);
        zan_ast_node_t *n = zan_ast_new(p->arena, AST_IDENTIFIER, loc);
        n->ident.name = (zan_istr_t){ nm, (int)strlen(nm) };
        return n;
    }

    /* 核心系统底层抽象与内存语义契约 */
    if (p->current.kind == TK_IDENT && p->current.str_val.len == 4 &&
        memcmp(p->current.str_val.str, "from", 4) == 0 &&
        zan_lexer_peek(p->lex).kind == TK_IDENT) {
        zan_lexer_t saved_lex = *p->lex;
        zan_token_t saved_cur = p->current;
        zan_token_t saved_prev = p->previous;
        parser_advance(p);
        parser_advance(p);
        if (p->current.kind == TK_IN) {
            zan_istr_t qvar = p->previous.str_val;
            parser_advance(p);
            zan_ast_node_t *n = zan_ast_new(p->arena, AST_QUERY_EXPR, loc);
            n->query.var = qvar;
            n->query.source = parse_expression(p);
            zan_ast_list_init(&n->query.clauses);
            n->query.group_expr = NULL;
            n->query.group_key = NULL;
            n->query.group_into = (zan_istr_t){ NULL, 0 };
            n->query.select = NULL;
            /* 内部辅助逻辑 */
            for (;;) {
                if (parser_match(p, TK_WHERE)) {
                    zan_ast_node_t *wc = zan_ast_new(p->arena, AST_QUERY_WHERE,
                                                     p->previous.loc);
                    wc->query_clause.expr = parse_expression(p);
                    zan_ast_list_push(&n->query.clauses, wc, p->arena);
                    continue;
                }
                if (p->current.kind == TK_LET) {
                    parser_advance(p);
                    zan_ast_node_t *lc = zan_ast_new(p->arena, AST_QUERY_LET,
                                                     p->previous.loc);
                    lc->query_clause.name = p->current.str_val;
                    parser_expect(p, TK_IDENT);
                    parser_expect(p, TK_EQ);
                    lc->query_clause.expr = parse_expression(p);
                    zan_ast_list_push(&n->query.clauses, lc, p->arena);
                    continue;
                }
                if (p->current.kind == TK_IDENT &&
                    p->current.str_val.len == 7 &&
                    memcmp(p->current.str_val.str, "orderby", 7) == 0) {
                    parser_advance(p);
                    for (;;) {
                        zan_ast_node_t *oc = zan_ast_new(
                            p->arena, AST_QUERY_ORDERBY, p->previous.loc);
                        oc->query_clause.expr = parse_expression(p);
                        oc->query_clause.descending = 0;
                        if (p->current.kind == TK_IDENT &&
                            p->current.str_val.len == 10 &&
                            memcmp(p->current.str_val.str, "descending", 10)
                                == 0) {
                            parser_advance(p);
                            oc->query_clause.descending = 1;
                        } else if (p->current.kind == TK_IDENT &&
                                   p->current.str_val.len == 9 &&
                                   memcmp(p->current.str_val.str,
                                          "ascending", 9) == 0) {
                            parser_advance(p);
                        }
                        zan_ast_list_push(&n->query.clauses, oc, p->arena);
                        if (!parser_match(p, TK_COMMA)) break;
                    }
                    continue;
                }
                if (p->current.kind == TK_IDENT &&
                    p->current.str_val.len == 4 &&
                    memcmp(p->current.str_val.str, "join", 4) == 0) {
                    parser_advance(p);
                    zan_ast_node_t *jc = zan_ast_new(p->arena, AST_QUERY_JOIN,
                                                     p->previous.loc);
                    jc->query_clause.name = p->current.str_val;
                    parser_expect(p, TK_IDENT);
                    parser_expect(p, TK_IN);
                    jc->query_clause.source = parse_expression(p);
                    if (!(p->current.kind == TK_IDENT &&
                          p->current.str_val.len == 2 &&
                          memcmp(p->current.str_val.str, "on", 2) == 0)) {
                        zan_diag_emit(p->diag, DIAG_ERROR, p->current.loc,
                                      "expected 'on' in join clause");
                    } else {
                        parser_advance(p);
                    }
                    jc->query_clause.left_key = parse_expression(p);
                    if (!(p->current.kind == TK_IDENT &&
                          p->current.str_val.len == 6 &&
                          memcmp(p->current.str_val.str, "equals", 6) == 0)) {
                        zan_diag_emit(p->diag, DIAG_ERROR, p->current.loc,
                                      "expected 'equals' in join clause");
                    } else {
                        parser_advance(p);
                    }
                    jc->query_clause.right_key = parse_expression(p);
                    jc->query_clause.into = (zan_istr_t){ NULL, 0 };
                    if (p->current.kind == TK_IDENT &&
                        p->current.str_val.len == 4 &&
                        memcmp(p->current.str_val.str, "into", 4) == 0) {
                        parser_advance(p);
                        jc->query_clause.into = p->current.str_val;
                        parser_expect(p, TK_IDENT);
                    }
                    zan_ast_list_push(&n->query.clauses, jc, p->arena);
                    continue;
                }
                break;
            }
            /* 内部辅助逻辑 */
            if (p->current.kind == TK_IDENT &&
                p->current.str_val.len == 5 &&
                memcmp(p->current.str_val.str, "group", 5) == 0) {
                parser_advance(p);
                n->query.group_expr = parse_expression(p);
                if (!(p->current.kind == TK_IDENT &&
                      p->current.str_val.len == 2 &&
                      memcmp(p->current.str_val.str, "by", 2) == 0)) {
                    zan_diag_emit(p->diag, DIAG_ERROR, p->current.loc,
                                  "expected 'by' in group clause");
                } else {
                    parser_advance(p);
                }
                n->query.group_key = parse_expression(p);
                if (p->current.kind == TK_IDENT &&
                    p->current.str_val.len == 4 &&
                    memcmp(p->current.str_val.str, "into", 4) == 0) {
                    parser_advance(p);
                    n->query.group_into = p->current.str_val;
                    parser_expect(p, TK_IDENT);
                }
            }
            if (p->current.kind == TK_IDENT && p->current.str_val.len == 6 &&
                memcmp(p->current.str_val.str, "select", 6) == 0) {
                if (n->query.group_expr && n->query.group_into.len == 0) {
                    /* `group e by k` without `into` is terminal. */
                    zan_diag_emit(p->diag, DIAG_ERROR, p->current.loc,
                                  "a group clause without 'into' must be the "
                                  "final clause of the query");
                } else {
                    parser_advance(p);
                    n->query.select = parse_expression(p);
                }
            } else if (!n->query.group_expr) {
                zan_diag_emit(p->diag, DIAG_ERROR, p->current.loc,
                              "expected 'select' in query expression");
                n->query.select = parser_error_node(p);
            }
            return n;
        }
        *p->lex = saved_lex;
        p->current = saved_cur;
        p->previous = saved_prev;
    }

    switch (p->current.kind) {
    case TK_INT_LIT: {
        parser_advance(p);
        zan_ast_node_t *n = zan_ast_new(p->arena, AST_INT_LITERAL, loc);
        n->int_val = p->previous.int_val;
        n->lit_suffix = p->previous.lit_suffix;
        n->lit_radix = p->previous.lit_radix;
        return n;
    }
    case TK_FLOAT_LIT: {
        parser_advance(p);
        zan_ast_node_t *n = zan_ast_new(p->arena, AST_FLOAT_LITERAL, loc);
        n->float_val = p->previous.float_val;
        return n;
    }
    case TK_STRING_LIT: {
        parser_advance(p);
        zan_ast_node_t *n = zan_ast_new(p->arena, AST_STRING_LITERAL, loc);
        n->str_val = p->previous.str_val;
        return n;
    }
    case TK_INTERP_START: {
        /* 内部辅助逻辑 */
        zan_ast_node_t *n = zan_ast_new(p->arena, AST_STRING_INTERP, loc);
        zan_ast_list_init(&n->string_interp.parts);
        zan_ast_list_init(&n->string_interp.formats);
        parser_advance(p);
        zan_ast_node_t *seg = zan_ast_new(p->arena, AST_STRING_LITERAL, loc);
        seg->str_val = p->previous.str_val;
        zan_ast_list_push(&n->string_interp.parts, seg, p->arena);
        while (true) {
            zan_ast_node_t *expr = parse_expression(p);
            zan_ast_list_push(&n->string_interp.parts, expr, p->arena);
            zan_ast_node_t *fmt = NULL;
            if (p->current.kind == TK_INTERP_FMT) {
                parser_advance(p);
                fmt = zan_ast_new(p->arena, AST_STRING_LITERAL, p->previous.loc);
                fmt->str_val = p->previous.str_val;
            }
            zan_ast_list_push(&n->string_interp.formats, fmt, p->arena);
            if (p->current.kind == TK_INTERP_MID) {
                parser_advance(p);
                zan_ast_node_t *mid = zan_ast_new(p->arena, AST_STRING_LITERAL, p->previous.loc);
                mid->str_val = p->previous.str_val;
                zan_ast_list_push(&n->string_interp.parts, mid, p->arena);
            } else if (p->current.kind == TK_INTERP_END) {
                parser_advance(p);
                zan_ast_node_t *end = zan_ast_new(p->arena, AST_STRING_LITERAL, p->previous.loc);
                end->str_val = p->previous.str_val;
                zan_ast_list_push(&n->string_interp.parts, end, p->arena);
                break;
            } else {
                break;
            }
        }
        return n;
    }
    case TK_CHAR_LIT: {
        parser_advance(p);
        zan_ast_node_t *n = zan_ast_new(p->arena, AST_CHAR_LITERAL, loc);
        n->int_val = p->previous.int_val;
        return n;
    }
    case TK_TRUE: {
        parser_advance(p);
        zan_ast_node_t *n = zan_ast_new(p->arena, AST_BOOL_LITERAL, loc);
        n->bool_val = true;
        return n;
    }
    case TK_FALSE: {
        parser_advance(p);
        zan_ast_node_t *n = zan_ast_new(p->arena, AST_BOOL_LITERAL, loc);
        n->bool_val = false;
        return n;
    }
    case TK_NULL: {
        parser_advance(p);
        return zan_ast_new(p->arena, AST_NULL_LITERAL, loc);
    }
    case TK_THIS: {
        parser_advance(p);
        return zan_ast_new(p->arena, AST_THIS_EXPR, loc);
    }
    case TK_BASE: {
        parser_advance(p);
        return zan_ast_new(p->arena, AST_BASE_EXPR, loc);
    }
    case TK_IDENT: {
        /* 底层系统交互与数据协议契约 */
        if (p->current.str_val.len == 6 &&
            memcmp(p->current.str_val.str, "nameof", 6) == 0 &&
            zan_lexer_peek(p->lex).kind == TK_LPAREN) {
            parser_advance(p);
            parser_expect(p, TK_LPAREN);
            zan_ast_node_t *arg = parse_expression(p);
            parser_expect(p, TK_RPAREN);
            zan_istr_t name = {NULL, 0};
            if (arg->kind == AST_IDENTIFIER) {
                name = arg->ident.name;
            } else if (arg->kind == AST_MEMBER_ACCESS) {
                name = arg->member.name;
            }
            if (!name.str) {
                zan_diag_emit(p->diag, DIAG_ERROR, loc,
                              "nameof argument must be an identifier or member access");
                name.str = "";
                name.len = 0;
            }
            zan_ast_node_t *n = zan_ast_new(p->arena, AST_STRING_LITERAL, loc);
            n->str_val = name;
            return n;
        }
        /* 核心系统底层抽象与内存语义契约 */
        zan_token_t peek = zan_lexer_peek(p->lex);
        if (peek.kind == TK_ARROW) {
            parser_advance(p);
            zan_istr_t param_name = p->previous.str_val;
            parser_advance(p);
            zan_ast_node_t *body = parser_check(p, TK_LBRACE)
                ? parse_block(p) : parse_expression(p);
            zan_ast_node_t *n = zan_ast_new(p->arena, AST_LAMBDA, loc);
            zan_ast_list_init(&n->lambda.params);
            zan_ast_node_t *param = zan_ast_new(p->arena, AST_PARAM, loc);
            param->param.name = param_name;
            param->param.type = NULL;
            param->param.default_val = NULL;
            zan_ast_list_push(&n->lambda.params, param, p->arena);
            n->lambda.body = body;
            return n;
        }
        parser_advance(p);
        zan_ast_node_t *n = zan_ast_new(p->arena, AST_IDENTIFIER, loc);
        n->ident.name = p->previous.str_val;
        return n;
    }
    case TK_LPAREN: {
        /* 底层系统交互与数据协议契约 */
        if (paren_is_lambda(p)) {
            return parse_lambda_paren(p, loc);
        }
        /* 内部辅助逻辑 */
        switch (zan_lexer_peek(p->lex).kind) {
        case TK_INT: case TK_LONG: case TK_SHORT: case TK_BYTE:
        case TK_UINT: case TK_ULONG: case TK_USHORT: case TK_SBYTE:
        case TK_DOUBLE: case TK_FLOAT: case TK_DECIMAL:
        case TK_BOOL: case TK_CHAR: case TK_NINT:
        case TK_STRING: case TK_OBJECT: {
            parser_advance(p);
            zan_ast_node_t *ctype = parse_type_ref(p);
            parser_expect(p, TK_RPAREN);
            zan_ast_node_t *coperand = parse_unary(p);
            zan_ast_node_t *cn = zan_ast_new(p->arena, AST_CAST_EXPR, loc);
            cn->cast.type = ctype;
            cn->cast.expr = coperand;
            return cn;
        }
        default: break;
        }
        if (paren_is_named_cast(p)) {
            parser_advance(p);
            zan_ast_node_t *ctype = parse_type_ref(p);
            parser_expect(p, TK_RPAREN);
            zan_ast_node_t *coperand = parse_unary(p);
            zan_ast_node_t *cn = zan_ast_new(p->arena, AST_CAST_EXPR, loc);
            cn->cast.type = ctype;
            cn->cast.expr = coperand;
            return cn;
        }
        parser_advance(p);
        zan_ast_node_t *expr = parse_expression(p);
        /* 内部辅助逻辑 */
        if (parser_check(p, TK_COMMA)) {
            zan_ast_node_t *tup = zan_ast_new(p->arena, AST_TUPLE_EXPR, loc);
            zan_ast_list_init(&tup->tuple_expr.items);
            zan_ast_list_push(&tup->tuple_expr.items, expr, p->arena);
            while (parser_match(p, TK_COMMA)) {
                if (parser_check(p, TK_RPAREN) || parser_check(p, TK_EOF)) break;
                /* 模块核心语义抽象与接口调用契约 */
                if (p->current.kind == TK_IDENT &&
                    zan_lexer_peek(p->lex).kind == TK_COLON) {
                    parser_advance(p);
                    parser_advance(p);
                }
                zan_ast_node_t *item = parse_expression(p);
                zan_ast_list_push(&tup->tuple_expr.items, item, p->arena);
            }
            parser_expect(p, TK_RPAREN);
            return tup;
        }
        parser_expect(p, TK_RPAREN);
        if (parser_check(p, TK_ARROW)) {
            parser_advance(p);
            zan_ast_node_t *body = parse_expression(p);
            zan_ast_node_t *n = zan_ast_new(p->arena, AST_LAMBDA, loc);
            zan_ast_list_init(&n->lambda.params);
            if (expr->kind == AST_IDENTIFIER) {
                zan_ast_node_t *param = zan_ast_new(p->arena, AST_PARAM, loc);
                param->param.name = expr->ident.name;
                param->param.type = NULL;
                param->param.default_val = NULL;
                zan_ast_list_push(&n->lambda.params, param, p->arena);
            }
            n->lambda.body = body;
            return n;
        }
        return expr;
    }
    case TK_DELEGATE: {
        /* 核心系统底层抽象与内存语义契约 */
        zan_loc_t dloc = p->current.loc;
        parser_advance(p);
        zan_ast_node_t *n = zan_ast_new(p->arena, AST_LAMBDA, dloc);
        zan_ast_list_init(&n->lambda.params);
        if (parser_match(p, TK_LPAREN)) {
            while (!parser_check(p, TK_RPAREN) && !parser_check(p, TK_EOF)) {
                zan_ast_node_t *param = parse_parameter(p);
                zan_ast_list_push(&n->lambda.params, param, p->arena);
                if (!parser_match(p, TK_COMMA)) break;
            }
            parser_expect(p, TK_RPAREN);
        }
        n->lambda.body = parse_block(p);
        return n;
    }
    case TK_NEW: {
        parser_advance(p);
        zan_loc_t newloc = p->previous.loc;
        zan_ast_node_t *n = NULL;

        /* 核心系统底层抽象与内存语义契约 */
        if (parser_check(p, TK_LBRACE)) {
            parser_advance(p);
            n = zan_ast_new(p->arena, AST_NEW_EXPR, newloc);
            n->new_expr.type = NULL;
            zan_ast_list_init(&n->new_expr.args);
            n->new_expr.is_array = false;
            n->new_expr.array_init = false;
            int m = 0;
            while (!parser_check(p, TK_RBRACE) && !parser_check(p, TK_EOF)) {
                zan_ast_node_t *arg = parse_expression(p);
                zan_ast_list_push(&n->new_expr.args, arg, p->arena);
                (void)m;
                m++;
                if (!parser_match(p, TK_COMMA)) break;
            }
            parser_expect(p, TK_RBRACE);
            return n;
        }

        zan_ast_node_t *type = parse_type_ref(p);
        n = zan_ast_new(p->arena, AST_NEW_EXPR, loc);
        n->new_expr.type = type;
        zan_ast_list_init(&n->new_expr.args);
        n->new_expr.is_array = false;
        n->new_expr.array_init = false;

        /* 核心系统底层抽象与内存语义契约 */
        if (parser_check(p, TK_LBRACKET) && !type->type_ref.is_array) {
            parser_advance(p);
            n->new_expr.is_array = true;
            while (!parser_check(p, TK_RBRACKET) && !parser_check(p, TK_EOF)) {
                if (n->new_expr.array_rank >= 16) {
                    /* 多维数组维度上限校验 (最大支持 16 维) */
                    zan_diag_emit(p->diag, DIAG_ERROR, p->current.loc,
                                  "array rank specifier is too deep (max 16)");
                }
                zan_ast_node_t *dim = parse_expression(p);
                zan_ast_list_push(&n->new_expr.args, dim, p->arena);
                n->new_expr.array_rank++;
                if (!parser_match(p, TK_COMMA)) break;
            }
            parser_expect(p, TK_RBRACKET);
            /* 编译器代码生成与运行时系统底层调用契约 */
            while (parser_check(p, TK_LBRACKET)) {
                int rank = array_suffix_rank(p);
                if (rank <= 0) break;
                parser_advance(p);
                for (int c = 1; c < rank; c++) parser_advance(p);
                parser_advance(p);
                if (rank > 16) {
                    /* 编译器代码生成与运行时系统底层调用契约 */
                    zan_diag_emit(p->diag, DIAG_ERROR, loc,
                                  "array rank specifier is too deep (max 16)");
                    continue;
                }
                zan_ast_node_t *w = zan_ast_new(p->arena, AST_TYPE_REF, loc);
                w->type_ref.name = type->type_ref.name;
                w->type_ref.is_array = true;
                w->type_ref.array_rank = rank;
                w->type_ref.array_element = type;
                zan_ast_list_init(&w->type_ref.type_args);
                type = w;
            }
            /* 模块核心语义抽象与接口调用契约 */
            if (n->new_expr.array_rank > 0) {
                zan_ast_node_t *w = zan_ast_new(p->arena, AST_TYPE_REF, loc);
                w->type_ref.name = type->type_ref.name;
                w->type_ref.is_array = true;
                w->type_ref.array_rank = n->new_expr.array_rank;
                w->type_ref.array_element = type;
                zan_ast_list_init(&w->type_ref.type_args);
                type = w;
            }
            n->new_expr.type = type;
        } else if (parser_match(p, TK_LPAREN)) {
            while (!parser_check(p, TK_RPAREN) && !parser_check(p, TK_EOF)) {
                zan_ast_node_t *arg = parse_call_arg(p);
                zan_ast_list_push(&n->new_expr.args, arg, p->arena);
                if (!parser_match(p, TK_COMMA)) break;
            }
            parser_expect(p, TK_RPAREN);
        }
        /* 内部辅助实现 */
        if (parser_check(p, TK_LBRACE) && type->kind == AST_TYPE_REF &&
            type->type_ref.is_array && !n->new_expr.is_array) {
            /* new T[] { */
            n->new_expr.is_array = true;
            n->new_expr.array_init = true;
        }
        if (parser_match(p, TK_LBRACE)) {
            while (!parser_check(p, TK_RBRACE) && !parser_check(p, TK_EOF)) {
                if (parser_match(p, TK_LBRACE)) {
                    while (!parser_check(p, TK_RBRACE) && !parser_check(p, TK_EOF)) {
                        zan_ast_node_t *sub = parse_expression(p);
                        zan_ast_list_push(&n->new_expr.args, sub, p->arena);
                        if (!parser_match(p, TK_COMMA)) break;
                    }
                    parser_expect(p, TK_RBRACE);
                } else {
                    zan_ast_node_t *item = parse_expression(p);
                    zan_ast_list_push(&n->new_expr.args, item, p->arena);
                }
                if (!parser_match(p, TK_COMMA)) break;
            }
            parser_expect(p, TK_RBRACE);
        }
        return n;
    }
    case TK_TYPEOF: {
        parser_advance(p);
        parser_expect(p, TK_LPAREN);
        zan_ast_node_t *type = parse_type_ref(p);
        parser_expect(p, TK_RPAREN);
        zan_ast_node_t *n = zan_ast_new(p->arena, AST_TYPEOF_EXPR, loc);
        n->cast.type = type;
        return n;
    }
    case TK_SIZEOF: {
        parser_advance(p);
        parser_expect(p, TK_LPAREN);
        zan_ast_node_t *type = parse_type_ref(p);
        parser_expect(p, TK_RPAREN);
        zan_ast_node_t *n = zan_ast_new(p->arena, AST_SIZEOF_EXPR, loc);
        n->cast.type = type;
        return n;
    }
    default:
        zan_diag_emit(p->diag, DIAG_ERROR, loc,
                      "unexpected token '%s' in expression",
                      zan_token_kind_name(p->current.kind));
        parser_advance(p);
        return parser_error_node(p);
    }
}

static bool is_type_kw(zan_token_kind_t k);

/* 内部辅助逻辑 */
static zan_ast_node_t *parse_call_arg(zan_parser_t *p) {
    /* 核心系统底层抽象与内存语义契约 */
    if (parser_check(p, TK_IDENT)) {
        zan_token_t peek = zan_lexer_peek(p->lex);
        if (peek.kind == TK_COLON) {
            zan_loc_t loc = p->current.loc;
            zan_istr_t name = p->current.str_val;
            parser_advance(p);
            parser_advance(p);
            zan_ast_node_t *expr = parse_expression(p);
            zan_ast_node_t *n = zan_ast_new(p->arena, AST_NAMED_ARG, loc);
            n->named_arg.name = name;
            n->named_arg.expr = expr;
            return n;
        }
    }
    if (!parser_check(p, TK_REF) && !parser_check(p, TK_OUT)) {
        return parse_expression(p);
    }
    zan_loc_t loc = p->current.loc;
    int is_out = parser_check(p, TK_OUT);
    parser_advance(p);
    zan_ast_node_t *n = zan_ast_new(p->arena, AST_REF_ARG, loc);
    n->ref_arg.is_out = is_out;
    n->ref_arg.decl_type = NULL;
    if (is_out && is_type_kw(p->current.kind)) {
        n->ref_arg.decl_type = parse_type_ref(p);
        zan_ast_node_t *id = zan_ast_new(p->arena, AST_IDENTIFIER, p->current.loc);
        id->ident.name = p->current.str_val;
        parser_expect(p, TK_IDENT);
        n->ref_arg.expr = id;
        return n;
    }
    zan_ast_node_t *e = parse_expression(p);
    if (is_out && e->kind == AST_IDENTIFIER && parser_check(p, TK_IDENT)) {
        /* 编译器代码生成与运行时系统底层调用契约 */
        zan_ast_node_t *ty = zan_ast_new(p->arena, AST_TYPE_REF, e->loc);
        ty->type_ref.name = e->ident.name;
        zan_ast_list_init(&ty->type_ref.type_args);
        n->ref_arg.decl_type = ty;
        zan_ast_node_t *id = zan_ast_new(p->arena, AST_IDENTIFIER, p->current.loc);
        id->ident.name = p->current.str_val;
        parser_advance(p);
        e = id;
    }
    n->ref_arg.expr = e;
    return n;
}

static bool is_type_kw(zan_token_kind_t k) {
    switch (k) {
    case TK_INT: case TK_LONG: case TK_SHORT: case TK_BYTE:
    case TK_UINT: case TK_ULONG: case TK_USHORT: case TK_SBYTE:
    case TK_FLOAT: case TK_DOUBLE: case TK_DECIMAL: case TK_BOOL: case TK_CHAR:
    case TK_STRING: case TK_VOID: case TK_OBJECT: case TK_NINT:
        return true;
    default:
        return false;
    }
}

/* Disambiguate `name< */
static bool looks_like_call_type_args(zan_parser_t *p) {
    zan_lexer_t saved_lex = *p->lex;
    zan_token_t saved_cur = p->current;
    zan_token_t saved_prev = p->previous;
    bool ok = false;
    int depth = 0;
    while (p->current.kind != TK_EOF) {
        zan_token_kind_t k = p->current.kind;
        if (k == TK_LESS) {
            depth++;
        } else if (k == TK_GREATER || k == TK_GREATER_GREATER ||
                   k == TK_GREATER_GREATER_GREATER) {
            depth -= (k == TK_GREATER_GREATER_GREATER) ? 3
                   : (k == TK_GREATER_GREATER) ? 2 : 1;
            if (depth <= 0) {
                ok = (zan_lexer_peek(p->lex).kind == TK_LPAREN);
                break;
            }
        } else if (k == TK_IDENT || k == TK_COMMA || k == TK_DOT ||
                   k == TK_LBRACKET || k == TK_RBRACKET || k == TK_QUESTION ||
                   is_type_kw(k)) {
            /* 核心系统底层抽象与内存语义契约 */
        } else {
            break;
        }
        parser_advance(p);
    }
    *p->lex = saved_lex;
    p->current = saved_cur;
    p->previous = saved_prev;
    return ok;
}

/* Disambiguate `Name< */
static bool looks_like_type_args_before_dot(zan_parser_t *p) {
    zan_lexer_t saved_lex = *p->lex;
    zan_token_t saved_cur = p->current;
    zan_token_t saved_prev = p->previous;
    bool ok = false;
    int depth = 0;
    while (p->current.kind != TK_EOF) {
        zan_token_kind_t k = p->current.kind;
        if (k == TK_LESS) {
            depth++;
        } else if (k == TK_GREATER || k == TK_GREATER_GREATER ||
                   k == TK_GREATER_GREATER_GREATER) {
            depth -= (k == TK_GREATER_GREATER_GREATER) ? 3
                   : (k == TK_GREATER_GREATER) ? 2 : 1;
            if (depth <= 0) {
                ok = (zan_lexer_peek(p->lex).kind == TK_DOT);
                break;
            }
        } else if (k == TK_IDENT || k == TK_COMMA || k == TK_DOT ||
                   k == TK_LBRACKET || k == TK_RBRACKET || k == TK_QUESTION ||
                   is_type_kw(k)) {
            /* 核心系统底层抽象与内存语义契约 */
        } else {
            break;
        }
        parser_advance(p);
    }
    *p->lex = saved_lex;
    p->current = saved_cur;
    p->previous = saved_prev;
    return ok;
}

/* Disambiguate `Name< */
static bool looks_like_type_args_before_brace(zan_parser_t *p) {
    zan_lexer_t saved_lex = *p->lex;
    zan_token_t saved_cur = p->current;
    zan_token_t saved_prev = p->previous;
    bool ok = false;
    int depth = 0;
    while (p->current.kind != TK_EOF) {
        zan_token_kind_t k = p->current.kind;
        if (k == TK_LESS) {
            depth++;
        } else if (k == TK_GREATER || k == TK_GREATER_GREATER ||
                   k == TK_GREATER_GREATER_GREATER) {
            depth -= (k == TK_GREATER_GREATER_GREATER) ? 3
                   : (k == TK_GREATER_GREATER) ? 2 : 1;
            if (depth <= 0) {
                ok = (zan_lexer_peek(p->lex).kind == TK_LBRACE);
                break;
            }
        } else if (k == TK_IDENT || k == TK_COMMA || k == TK_DOT ||
                   k == TK_LBRACKET || k == TK_RBRACKET || k == TK_QUESTION ||
                   is_type_kw(k)) {
            /* 核心系统底层抽象与内存语义契约 */
        } else {
            break;
        }
        parser_advance(p);
    }
    *p->lex = saved_lex;
    p->current = saved_cur;
    p->previous = saved_prev;
    return ok;
}

/* 内部辅助实现 */
static void desugar_task_join(zan_ast_node_t *member) {
    zan_istr_t obj_name;
    zan_istr_t m = member->member.name;
    if (!member->member.object ||
        member->member.object->kind != AST_IDENTIFIER) return;
    obj_name = member->member.object->ident.name;
    if (obj_name.len != 4 || memcmp(obj_name.str, "Task", 4) != 0) return;
    if (!((m.len == 7 && memcmp(m.str, "WhenAll", 7) == 0) ||
          (m.len == 7 && memcmp(m.str, "WhenAny", 7) == 0))) return;
    member->member.object->ident.name = (zan_istr_t){"TaskJoin", 8};
}

static bool is_case_type_pattern(zan_parser_t *p);

static zan_ast_node_t *parse_postfix(zan_parser_t *p) {
    zan_ast_node_t *expr = parse_primary(p);

    for (;;) {
        zan_loc_t loc = p->current.loc;

        /* 核心系统底层抽象与内存语义契约 */
        if (parser_check(p, TK_LBRACE) &&
            (expr->kind == AST_IDENTIFIER || expr->kind == AST_MEMBER_ACCESS ||
             expr->kind == AST_CALL)) {
            /* 内部辅助逻辑 */
            if (expr->kind == AST_IDENTIFIER && expr->ident.inst_type_ref) {
                zan_ast_node_t *n = zan_ast_new(p->arena, AST_NEW_EXPR, loc);
                n->new_expr.type = expr->ident.inst_type_ref;
                zan_ast_list_init(&n->new_expr.args);
                zan_ast_list_init(&n->new_expr.arg_inits);
                parser_advance(p);
                while (!parser_check(p, TK_RBRACE) && !parser_check(p, TK_EOF)) {
                    zan_ast_node_t *field_init;
                    if (parser_check(p, TK_IDENT) &&
                        zan_lexer_peek(p->lex).kind == TK_EQ) {
                        zan_loc_t nloc = p->current.loc;
                        zan_istr_t name = p->current.str_val;
                        parser_advance(p);
                        parser_advance(p);
                        if (parser_match(p, TK_LBRACE)) {
                            field_init = zan_ast_new(p->arena, AST_COLL_INIT, nloc);
                            field_init->coll_init.name = name;
                            zan_ast_list_init(&field_init->coll_init.items);
                            while (!parser_check(p, TK_RBRACE) &&
                                   !parser_check(p, TK_EOF)) {
                                zan_ast_node_t *item = parse_expression(p);
                                zan_ast_list_push(&field_init->coll_init.items,
                                                  item, p->arena);
                                if (!parser_match(p, TK_COMMA)) break;
                            }
                            parser_expect(p, TK_RBRACE);
                        } else {
                            field_init = zan_ast_new(p->arena, AST_ASSIGNMENT, nloc);
                            zan_ast_node_t *lhs =
                                zan_ast_new(p->arena, AST_IDENTIFIER, nloc);
                            lhs->ident.name = name;
                            field_init->binary.op = TK_EQ;
                            field_init->binary.left = lhs;
                            field_init->binary.right = parse_expression(p);
                        }
                    } else {
                        field_init = parse_expression(p);
                    }
                    zan_ast_list_push(&n->new_expr.arg_inits, field_init,
                                      p->arena);
                    if (!parser_match(p, TK_COMMA)) break;
                }
                parser_expect(p, TK_RBRACE);
                expr = n;
                continue;
            }

            /* 核心系统底层抽象与内存语义契约 */
            if (expr->kind == AST_CALL) {
                zan_ast_node_t *n = zan_ast_new(p->arena, AST_NEW_EXPR, loc);
                n->new_expr.call_init = expr;
                zan_ast_list_init(&n->new_expr.args);
                zan_ast_list_init(&n->new_expr.arg_inits);
                parser_advance(p);
                while (!parser_check(p, TK_RBRACE) && !parser_check(p, TK_EOF)) {
                    zan_ast_node_t *field_init;
                    if (parser_check(p, TK_IDENT) &&
                        zan_lexer_peek(p->lex).kind == TK_EQ) {
                        zan_loc_t nloc = p->current.loc;
                        zan_istr_t name = p->current.str_val;
                        parser_advance(p);
                        parser_advance(p);
                        if (parser_match(p, TK_LBRACE)) {
                            field_init = zan_ast_new(p->arena, AST_COLL_INIT, nloc);
                            field_init->coll_init.name = name;
                            zan_ast_list_init(&field_init->coll_init.items);
                            while (!parser_check(p, TK_RBRACE) &&
                                   !parser_check(p, TK_EOF)) {
                                zan_ast_node_t *item = parse_expression(p);
                                zan_ast_list_push(&field_init->coll_init.items,
                                                  item, p->arena);
                                if (!parser_match(p, TK_COMMA)) break;
                            }
                            parser_expect(p, TK_RBRACE);
                        } else {
                            field_init = zan_ast_new(p->arena, AST_ASSIGNMENT, nloc);
                            zan_ast_node_t *lhs =
                                zan_ast_new(p->arena, AST_IDENTIFIER, nloc);
                            lhs->ident.name = name;
                            field_init->binary.op = TK_EQ;
                            field_init->binary.left = lhs;
                            field_init->binary.right = parse_expression(p);
                        }
                    } else {
                        field_init = parse_expression(p);
                    }
                    zan_ast_list_push(&n->new_expr.arg_inits, field_init,
                                      p->arena);
                    if (!parser_match(p, TK_COMMA)) break;
                }
                parser_expect(p, TK_RBRACE);
                expr = n;
                continue;
            }
            /* 内部辅助逻辑 */
            zan_ast_node_t *type = zan_ast_new(p->arena, AST_TYPE_REF, expr->loc);
            if (expr->kind == AST_IDENTIFIER) {
                type->type_ref.name = expr->ident.name;
            } else {
                type->type_ref.name = expr->member.name;
            }
            zan_ast_list_init(&type->type_ref.type_args);

            zan_ast_node_t *n = zan_ast_new(p->arena, AST_NEW_EXPR, loc);
            n->new_expr.type = type;
            zan_ast_list_init(&n->new_expr.args);

            /* 内部辅助逻辑 */
            while (!parser_check(p, TK_RBRACE) && !parser_check(p, TK_EOF)) {
                zan_ast_node_t *field_init;
                if (parser_check(p, TK_IDENT) &&
                    zan_lexer_peek(p->lex).kind == TK_EQ) {
                    zan_loc_t nloc = p->current.loc;
                    zan_istr_t name = p->current.str_val;
                    parser_advance(p);
                    parser_advance(p);
                    if (parser_match(p, TK_LBRACE)) {
                        field_init = zan_ast_new(p->arena, AST_COLL_INIT, nloc);
                        field_init->coll_init.name = name;
                        zan_ast_list_init(&field_init->coll_init.items);
                        while (!parser_check(p, TK_RBRACE) && !parser_check(p, TK_EOF)) {
                            zan_ast_node_t *item = parse_expression(p);
                            zan_ast_list_push(&field_init->coll_init.items, item,
                                              p->arena);
                            if (!parser_match(p, TK_COMMA)) break;
                        }
                        parser_expect(p, TK_RBRACE);
                    } else {
                        field_init = zan_ast_new(p->arena, AST_ASSIGNMENT, nloc);
                        zan_ast_node_t *lhs =
                            zan_ast_new(p->arena, AST_IDENTIFIER, nloc);
                        lhs->ident.name = name;
                        field_init->binary.op = TK_EQ;
                        field_init->binary.left = lhs;
                        field_init->binary.right = parse_expression(p);
                    }
                } else {
                    field_init = parse_expression(p);
                }
                zan_ast_list_push(&n->new_expr.args, field_init, p->arena);
                if (!parser_match(p, TK_COMMA)) break;
            }
            parser_expect(p, TK_RBRACE);
            expr = n;
            continue;
        }

        if (parser_check(p, TK_DOT) || parser_check(p, TK_QUESTION_DOT)) {
            int null_cond = parser_check(p, TK_QUESTION_DOT);
            parser_advance(p);
            if (!parser_check(p, TK_IDENT)) {
                zan_diag_emit(p->diag, DIAG_ERROR, loc, "expected member name after '.'");
                break;
            }
            parser_advance(p);
            zan_ast_node_t *n = zan_ast_new(p->arena, AST_MEMBER_ACCESS, loc);
            n->member.object = expr;
            n->member.name = p->previous.str_val;
            n->member.null_cond = null_cond;
            desugar_task_join(n);
            expr = n;
        } else if (parser_match(p, TK_LPAREN)) {
            zan_ast_node_t *n = zan_ast_new(p->arena, AST_CALL, loc);
            n->call.callee = expr;
            zan_ast_list_init(&n->call.args);
            zan_ast_list_init(&n->call.type_args);
            while (!parser_check(p, TK_RPAREN) && !parser_check(p, TK_EOF)) {
                zan_ast_node_t *arg = parse_call_arg(p);
                zan_ast_list_push(&n->call.args, arg, p->arena);
                if (!parser_match(p, TK_COMMA)) break;
            }
            parser_expect(p, TK_RPAREN);
            expr = n;
        } else if (parser_check(p, TK_LESS) &&
                   (expr->kind == AST_IDENTIFIER || expr->kind == AST_MEMBER_ACCESS) &&
                   looks_like_call_type_args(p)) {
            zan_ast_node_t *n = zan_ast_new(p->arena, AST_CALL, loc);
            n->call.callee = expr;
            zan_ast_list_init(&n->call.args);
            zan_ast_list_init(&n->call.type_args);
            parser_advance(p);
            while (!parser_check(p, TK_GREATER) && !parser_check(p, TK_EOF)) {
                zan_ast_node_t *ta = parse_type_ref(p);
                zan_ast_list_push(&n->call.type_args, ta, p->arena);
                if (!parser_match(p, TK_COMMA)) break;
            }
            parser_expect_gt(p);
            parser_expect(p, TK_LPAREN);
            while (!parser_check(p, TK_RPAREN) && !parser_check(p, TK_EOF)) {
                zan_ast_node_t *arg = parse_call_arg(p);
                zan_ast_list_push(&n->call.args, arg, p->arena);
                if (!parser_match(p, TK_COMMA)) break;
            }
            parser_expect(p, TK_RPAREN);
            expr = n;
        } else if (parser_check(p, TK_LESS) && expr->kind == AST_IDENTIFIER &&
                   !expr->ident.inst_type_ref &&
                   (looks_like_type_args_before_dot(p) ||
                    looks_like_type_args_before_brace(p))) {
            /* 内部辅助逻辑 */
            zan_ast_node_t *tref = zan_ast_new(p->arena, AST_TYPE_REF, expr->loc);
            tref->type_ref.name = expr->ident.name;
            tref->type_ref.is_nullable = false;
            tref->type_ref.is_array = false;
            zan_ast_list_init(&tref->type_ref.type_args);
            parser_advance(p);
            while (!parser_check(p, TK_GREATER) && !parser_check(p, TK_EOF)) {
                zan_ast_node_t *ta = parse_type_ref(p);
                zan_ast_list_push(&tref->type_ref.type_args, ta, p->arena);
                if (!parser_match(p, TK_COMMA)) break;
            }
            parser_expect_gt(p);
            expr->ident.inst_type_ref = tref;
        } else if (parser_match(p, TK_LBRACKET)) {
            /* 底层系统交互与数据协议契约 */
            zan_ast_node_t *idx = parse_expression(p);
            zan_ast_node_t *n = zan_ast_new(p->arena, AST_INDEX, loc);
            n->index.object = expr;
            n->index.index = idx;
            zan_ast_list_init(&n->index.extra);
            while (parser_match(p, TK_COMMA))
                zan_ast_list_push(&n->index.extra, parse_expression(p),
                                  p->arena);
            parser_expect(p, TK_RBRACKET);
            expr = n;
        } else if (parser_check(p, TK_SWITCH)) {
            /* 内部辅助逻辑 */
            parser_advance(p);
            parser_expect(p, TK_LBRACE);
            zan_ast_node_t *n = zan_ast_new(p->arena, AST_SWITCH_EXPR, loc);
            n->switch_expr.expr = expr;
            zan_ast_list_init(&n->switch_expr.arms);
            while (!parser_check(p, TK_RBRACE) && !parser_check(p, TK_EOF)) {
                zan_loc_t arm_loc = p->current.loc;
                zan_ast_node_t *arm = zan_ast_new(p->arena, AST_SWITCH_ARM, arm_loc);
                zan_ast_node_t *pattern = NULL;
                zan_ast_node_t *type_pattern = NULL;
                zan_ast_node_t *when_cond = NULL;
                zan_istr_t var_name = {0};
                bool is_default = false;

                /* 内部辅助逻辑 */
                if (parser_check(p, TK_DEFAULT) ||
                    (parser_check(p, TK_IDENT) && p->current.str_val.len == 1 &&
                     p->current.str_val.str[0] == '_')) {
                    parser_advance(p);
                    is_default = true;
                } else if (is_case_type_pattern(p)) {
                    type_pattern = parse_type_ref(p);
                    if (parser_check(p, TK_IDENT)) {
                        parser_advance(p);
                        var_name = p->previous.str_val;
                    }
                } else {
                    /* 核心系统底层抽象与内存语义契约 */
                    pattern = parse_expression(p);
                }
                if (parser_match(p, TK_WHEN)) {
                    when_cond = parse_expression(p);
                }
                parser_expect(p, TK_ARROW);
                zan_ast_node_t *result = parse_expression(p);

                arm->switch_arm.pattern = pattern;
                arm->switch_arm.type_pattern = type_pattern;
                arm->switch_arm.when_cond = when_cond;
                arm->switch_arm.result = result;
                arm->switch_arm.var_name = var_name;
                arm->switch_arm.is_default = is_default;
                zan_ast_list_push(&n->switch_expr.arms, arm, p->arena);
                if (!parser_match(p, TK_COMMA)) break;
            }
            parser_expect(p, TK_RBRACE);
            expr = n;
        } else if (parser_check(p, TK_IDENT) &&
                   p->current.str_val.len == 4 &&
                   memcmp(p->current.str_val.str, "with", 4) == 0 &&
                   zan_lexer_peek(p->lex).kind == TK_LBRACE) {
            /* 底层系统交互与数据协议契约 */
            parser_advance(p);
            parser_expect(p, TK_LBRACE);
            zan_ast_node_t *n = zan_ast_new(p->arena, AST_WITH_EXPR, loc);
            n->with_expr.expr = expr;
            zan_ast_list_init(&n->with_expr.assigns);
            while (!parser_check(p, TK_RBRACE) && !parser_check(p, TK_EOF)) {
                parser_expect(p, TK_IDENT);
                zan_istr_t fname = p->previous.str_val;
                parser_expect(p, TK_EQ);
                zan_ast_node_t *asg =
                    zan_ast_new(p->arena, AST_ASSIGNMENT, loc);
                asg->binary.op = TK_EQ;
                zan_ast_node_t *lhs =
                    zan_ast_new(p->arena, AST_IDENTIFIER, loc);
                lhs->ident.name = fname;
                asg->binary.left = lhs;
                asg->binary.right = parse_expression(p);
                zan_ast_list_push(&n->with_expr.assigns, asg, p->arena);
                if (!parser_match(p, TK_COMMA)) break;
            }
            parser_expect(p, TK_RBRACE);
            expr = n;
        } else if (parser_check(p, TK_BANG)) {
            /* 底层系统交互与数据协议契约 */
            parser_advance(p);
            zan_ast_node_t *n = zan_ast_new(p->arena, AST_POSTFIX_UNARY, loc);
            n->unary.op = TK_BANG;
            n->unary.operand = expr;
            expr = n;
            continue;
        } else if (parser_check(p, TK_PLUS_PLUS) || parser_check(p, TK_MINUS_MINUS)) {
            /* 内部辅助逻辑 */
            zan_ast_node_t *n = zan_ast_new(p->arena, AST_POSTFIX_UNARY, loc);
            n->unary.op = p->current.kind;
            parser_advance(p);
            n->unary.operand = expr;
            expr = n;
            break;
        } else {
            break;
        }
    }

    return expr;
}

/* 内部辅助逻辑 */
#define ZAN_PARSER_MAX_EXPR_DEPTH 512

static zan_ast_node_t *parse_unary_inner(zan_parser_t *p);

/* 内部辅助逻辑 */
static zan_ast_node_t *parser_expr_too_deep(zan_parser_t *p) {
    if (!p->expr_depth_reported) {
        p->expr_depth_reported = true;
        zan_diag_emit(p->diag, DIAG_ERROR, p->current.loc,
                      "expression nesting too deep (max %d)",
                      ZAN_PARSER_MAX_EXPR_DEPTH);
    }
    return parser_error_node(p);
}

/* 核心系统底层抽象与内存语义契约 */
static zan_ast_node_t *parse_unary(zan_parser_t *p) {
    if (p->expr_depth >= ZAN_PARSER_MAX_EXPR_DEPTH)
        return parser_expr_too_deep(p);
    p->expr_depth++;
    zan_ast_node_t *n = parse_unary_inner(p);
    p->expr_depth--;
    return n;
}

static zan_ast_node_t *parse_unary_inner(zan_parser_t *p) {
    zan_loc_t loc = p->current.loc;

    if (parser_check(p, TK_AWAIT)) {
        parser_advance(p);
        zan_ast_node_t *expr = parse_unary(p);
        zan_ast_node_t *n = zan_ast_new(p->arena, AST_AWAIT_EXPR, loc);
        n->await_expr.expr = expr;
        return n;
    }

    if (parser_check(p, TK_BANG) || parser_check(p, TK_MINUS) ||
        parser_check(p, TK_TILDE) || parser_check(p, TK_PLUS_PLUS) ||
        parser_check(p, TK_MINUS_MINUS)) {
        zan_token_kind_t op = p->current.kind;
        parser_advance(p);
        zan_ast_node_t *operand = parse_unary(p);
        zan_ast_node_t *n = zan_ast_new(p->arena, AST_UNARY, loc);
        n->unary.op = op;
        n->unary.operand = operand;
        return n;
    }

    return parse_postfix(p);
}

static int get_precedence(zan_token_kind_t kind) {
    switch (kind) {
    case TK_STAR: case TK_SLASH: case TK_PERCENT: return 12;
    case TK_PLUS: case TK_MINUS: return 11;
    case TK_LESS_LESS: case TK_GREATER_GREATER: case TK_GREATER_GREATER_GREATER: return 10;
    case TK_LESS: case TK_GREATER: case TK_LESS_EQ: case TK_GREATER_EQ:
    case TK_IS: case TK_AS: return 9;
    case TK_EQ_EQ: case TK_BANG_EQ: return 8;
    case TK_AMP: return 7;
    case TK_CARET: return 6;
    case TK_PIPE: return 5;
    case TK_AMP_AMP: return 4;
    case TK_PIPE_PIPE: return 3;
    case TK_QUESTION_QUESTION: return 2;
    default: return 0;
    }
}

static bool is_binary_op(zan_token_kind_t kind) {
    return get_precedence(kind) > 0;
}

/* 底层系统交互与数据协议契约 */
static bool token_starts_expr(zan_token_kind_t k) {
    switch (k) {
    case TK_IDENT: case TK_INT_LIT: case TK_FLOAT_LIT: case TK_STRING_LIT:
    case TK_CHAR_LIT: case TK_INTERP_START: case TK_LPAREN: case TK_MINUS:
    case TK_PLUS: case TK_BANG: case TK_TILDE: case TK_THIS: case TK_BASE:
    case TK_NEW: case TK_NULL: case TK_TRUE: case TK_FALSE:
        return true;
    default:
        return false;
    }
}

/* 内部辅助逻辑 */
static bool is_question_is_conditional(zan_parser_t *p) {
    zan_lexer_t saved_lex = *p->lex;
    zan_token_t saved_cur = p->current;
    zan_token_t saved_prev = p->previous;
    int depth = 0; /* 核心系统底层抽象与内存语义契约 */
    bool in_arm = false; /* 底层系统交互与数据协议契约 */
    bool result = false;
    while (p->current.kind != TK_EOF) {
        zan_token_kind_t k = p->current.kind;
        if (k == TK_SEMICOLON || k == TK_COMMA || k == TK_LBRACE
            || k == TK_RBRACE || k == TK_ARROW) {
            break;
        }
        if (k == TK_LPAREN || k == TK_LBRACKET
            || (!in_arm && k == TK_LESS)) {
            depth++;
        } else if (k == TK_RPAREN || k == TK_RBRACKET || k == TK_GREATER) {
            if (depth == 0) { break; }
            depth--;
        } else if (k == TK_GREATER_GREATER) {
            if (depth == 0) { break; }
            depth -= depth >= 2 ? 2 : depth;
        } else if (k == TK_GREATER_GREATER_GREATER) {
            if (depth == 0) { break; }
            depth -= depth >= 3 ? 3 : depth;
        } else if (!in_arm && k == TK_QUESTION && depth == 0) {
            if (!token_starts_expr(zan_lexer_peek(p->lex).kind)) {
                break; /* 底层系统交互与数据协议契约 */
            }
            in_arm = true;
        } else if (in_arm && k == TK_COLON && depth == 0) {
            result = true;
            break;
        }
        parser_advance(p);
    }
    *p->lex = saved_lex;
    p->current = saved_cur;
    p->previous = saved_prev;
    return result;
}

/* 核心系统底层抽象与内存语义契约 */
#define ZAN_PARSER_MAX_BINOP_CHAIN 16384

static zan_ast_node_t *parse_binary(zan_parser_t *p, int min_prec) {
    zan_ast_node_t *left = parse_unary(p);
    int chain = 0;

    while (is_binary_op(p->current.kind)) {
        int prec = get_precedence(p->current.kind);
        if (prec < min_prec) break;

        if (++chain > ZAN_PARSER_MAX_BINOP_CHAIN) {
            if (!p->chain_cap_reported) {
                zan_diag_emit(p->diag, DIAG_ERROR, p->current.loc,
                              "expression chain too long (max %d operators); "
                              "split it into statements",
                              ZAN_PARSER_MAX_BINOP_CHAIN);
                p->chain_cap_reported = true;
            }
            break;
        }

        zan_token_kind_t op = p->current.kind;
        zan_loc_t loc = p->current.loc;
        parser_advance(p);

        if (op == TK_IS || op == TK_AS) {
            zan_ast_node_t *n = zan_ast_new(p->arena,
                op == TK_IS ? AST_IS_EXPR : AST_AS_EXPR, loc);
            n->type_test.expr = left;
            n->type_test.type = NULL;
            n->type_test.var_name = (zan_istr_t){NULL, 0};
            n->type_test.is_not = false;
            if (op == TK_AS) {
                /* 内部辅助逻辑 */
                p->type_no_nullable =
                    is_question_is_conditional(p) ? 1 : 0;
                n->type_test.type = parse_type_ref(p);
                p->type_no_nullable = 0;
                left = n;
                continue;
            }
            /* 核心系统底层抽象与内存语义契约 */
            if (parser_match(p, TK_NOT)) {
                n->type_test.is_not = true;
            }
            if (parser_check(p, TK_NULL)) {
                /* 核心系统底层抽象与内存语义契约 */
                parser_advance(p);
            } else {
                p->type_no_nullable =
                    is_question_is_conditional(p) ? 1 : 0;
                n->type_test.type = parse_type_ref(p);
                p->type_no_nullable = 0;
                /* 底层系统交互与数据协议契约 */
                if (parser_check(p, TK_IDENT)) {
                    zan_token_t after = zan_lexer_peek(p->lex);
                    if (after.kind != TK_IS && after.kind != TK_AS) {
                        parser_advance(p);
                        n->type_test.var_name = p->previous.str_val;
                    }
                }
            }
            left = n;
            continue;
        }

        zan_ast_node_t *right = parse_binary(p, prec + 1);
        zan_ast_node_t *n = zan_ast_new(p->arena, AST_BINARY, loc);
        n->binary.op = op;
        /* 内部辅助逻辑 */
        if (op == TK_PLUS || op == TK_MINUS || op == TK_STAR) {
            if (p->checked_depth > 0) n->binary.checked = 1;
            else if (p->unchecked_depth > 0) n->binary.checked = -1;
        }
        n->binary.left = left;
        n->binary.right = right;
        left = n;
    }

    return left;
}

static zan_ast_node_t *parse_conditional(zan_parser_t *p) {
    zan_ast_node_t *expr = parse_binary(p, 1);

    if (parser_match(p, TK_QUESTION)) {
        zan_loc_t loc = p->previous.loc;
        zan_ast_node_t *then_expr = parse_expression(p);
        parser_expect(p, TK_COLON);
        zan_ast_node_t *else_expr = parse_expression(p);
        zan_ast_node_t *n = zan_ast_new(p->arena, AST_CONDITIONAL, loc);
        n->conditional.cond = expr;
        n->conditional.then_expr = then_expr;
        n->conditional.else_expr = else_expr;
        return n;
    }

    return expr;
}

static bool is_assign_op(zan_token_kind_t kind) {
    switch (kind) {
    case TK_EQ: case TK_PLUS_EQ: case TK_MINUS_EQ: case TK_STAR_EQ:
    case TK_SLASH_EQ: case TK_PERCENT_EQ: case TK_AMP_EQ: case TK_PIPE_EQ:
    case TK_CARET_EQ: case TK_LESS_LESS_EQ: case TK_GREATER_GREATER_EQ:
    case TK_GREATER_GREATER_GREATER_EQ:
        return true;
    default:
        return false;
    }
}

static zan_ast_node_t *parse_expression_inner(zan_parser_t *p);

static zan_ast_node_t *parse_expression(zan_parser_t *p) {
    /* 底层系统交互与数据协议契约 */
    if (p->expr_tail_depth >= ZAN_PARSER_MAX_EXPR_DEPTH)
        return parser_expr_too_deep(p);
    p->expr_tail_depth++;
    zan_ast_node_t *n = parse_expression_inner(p);
    p->expr_tail_depth--;
    return n;
}

static zan_ast_node_t *parse_expression_inner(zan_parser_t *p) {
    zan_ast_node_t *expr = parse_conditional(p);

    if (is_assign_op(p->current.kind)) {
        zan_token_kind_t op = p->current.kind;
        zan_loc_t loc = p->current.loc;
        parser_advance(p);

        /* 核心系统底层抽象与内存语义契约 */
        if (op == TK_EQ && parser_check(p, TK_LBRACE) &&
            (expr->kind == AST_IDENTIFIER || expr->kind == AST_MEMBER_ACCESS)) {
            zan_istr_t name = (expr->kind == AST_IDENTIFIER)
                                  ? expr->ident.name
                                  : expr->member.name;
            zan_ast_node_t *n = zan_ast_new(p->arena, AST_COLL_INIT, loc);
            n->coll_init.name = name;
            zan_ast_list_init(&n->coll_init.items);
            parser_advance(p);
            while (!parser_check(p, TK_RBRACE) && !parser_check(p, TK_EOF)) {
                zan_ast_node_t *item = parse_expression(p);
                zan_ast_list_push(&n->coll_init.items, item, p->arena);
                if (!parser_match(p, TK_COMMA)) break;
            }
            parser_expect(p, TK_RBRACE);
            return n;
        }

        zan_ast_node_t *right = parse_expression(p);

        /* 内部辅助逻辑 */
        zan_token_kind_t base = TK_EOF;
        switch (op) {
        case TK_PLUS_EQ:            base = TK_PLUS; break;
        case TK_MINUS_EQ:           base = TK_MINUS; break;
        case TK_STAR_EQ:            base = TK_STAR; break;
        case TK_SLASH_EQ:           base = TK_SLASH; break;
        case TK_PERCENT_EQ:         base = TK_PERCENT; break;
        case TK_AMP_EQ:             base = TK_AMP; break;
        case TK_PIPE_EQ:            base = TK_PIPE; break;
        case TK_CARET_EQ:           base = TK_CARET; break;
        case TK_LESS_LESS_EQ:       base = TK_LESS_LESS; break;
        case TK_GREATER_GREATER_EQ: base = TK_GREATER_GREATER; break;
        case TK_GREATER_GREATER_GREATER_EQ: base = TK_GREATER_GREATER_GREATER; break;
        default: break;
        }
        if (base != TK_EOF) {
            zan_ast_node_t *bin = zan_ast_new(p->arena, AST_BINARY, loc);
            bin->binary.op = base;
            bin->binary.left = expr;
            bin->binary.right = right;
            right = bin;
            op = TK_EQ;
        }

        zan_ast_node_t *n = zan_ast_new(p->arena, AST_ASSIGNMENT, loc);
        n->binary.op = op;
        /* 内部辅助逻辑 */
        n->binary.compound_base = base;
        n->binary.left = expr;
        n->binary.right = right;
        return n;
    }

    return expr;
}

/* 内部辅助逻辑 */
#define ZAN_PARSER_MAX_STMT_DEPTH 4096

static void parse_block_stmts(zan_parser_t *p, zan_ast_list_t *stmts_list) {
    while (!parser_check(p, TK_RBRACE) && !parser_check(p, TK_EOF)) {
        if (parser_check(p, TK_DEFER)) {
            zan_loc_t defer_loc = p->current.loc;
            parser_advance(p);
            /* 内部辅助逻辑 */
            if (p->stmt_depth >= ZAN_PARSER_MAX_STMT_DEPTH) {
                zan_diag_emit(p->diag, DIAG_ERROR, defer_loc,
                              "statement nesting too deep (max %d)",
                              ZAN_PARSER_MAX_STMT_DEPTH);
                if (!parser_check(p, TK_RBRACE) && !parser_check(p, TK_EOF))
                    parser_advance(p);
                break;
            }
            p->stmt_depth++;
            zan_ast_node_t *defer_stmt = parse_statement(p);
            p->stmt_depth--;
            zan_ast_node_t *fin_body = defer_stmt;
            if (!fin_body || fin_body->kind != AST_BLOCK) {
                zan_ast_node_t *fb = zan_ast_new(p->arena, AST_BLOCK, defer_loc);
                zan_ast_list_init(&fb->block.stmts);
                if (defer_stmt) {
                    zan_ast_list_push(&fb->block.stmts, defer_stmt, p->arena);
                    splice_pending_stmts(p, &fb->block.stmts);
                }
                fin_body = fb;
            }

            /* 模块核心语义抽象与接口调用契约 */
            zan_ast_node_t *tail_block = zan_ast_new(p->arena, AST_BLOCK, defer_loc);
            zan_ast_list_init(&tail_block->block.stmts);
            parse_block_stmts(p, &tail_block->block.stmts);

            zan_ast_node_t *try_node = zan_ast_new(p->arena, AST_TRY_STMT, defer_loc);
            try_node->try_stmt.try_body = tail_block;
            zan_ast_list_init(&try_node->try_stmt.catches);
            try_node->try_stmt.finally_body = fin_body;

            zan_ast_list_push(stmts_list, try_node, p->arena);
            break;
        }

        uint32_t before = p->current.loc.offset;
        zan_ast_node_t *stmt = parse_statement(p);
        if (stmt) {
            zan_ast_list_push(stmts_list, stmt, p->arena);
            splice_pending_stmts(p, stmts_list);
        }
        /* 内部辅助逻辑 */
        if (p->current.loc.offset == before && !parser_check(p, TK_EOF)) {
            parser_advance(p);
        }
    }
}

static zan_ast_node_t *parse_block(zan_parser_t *p) {
    if (p->stmt_depth >= ZAN_PARSER_MAX_STMT_DEPTH) {
        zan_diag_emit(p->diag, DIAG_ERROR, p->current.loc,
                      "statement nesting too deep (max %d)",
                      ZAN_PARSER_MAX_STMT_DEPTH);
        /* 模块核心语义抽象与接口调用契约 */
        while (!parser_check(p, TK_RBRACE) && !parser_check(p, TK_EOF))
            parser_advance(p);
        zan_ast_node_t *empty = zan_ast_new(p->arena, AST_BLOCK,
                                            p->current.loc);
        zan_ast_list_init(&empty->block.stmts);
        return empty;
    }
    p->stmt_depth++;
    zan_loc_t loc = p->current.loc;
    parser_expect(p, TK_LBRACE);

    zan_ast_node_t *block = zan_ast_new(p->arena, AST_BLOCK, loc);
    zan_ast_list_init(&block->block.stmts);

    parse_block_stmts(p, &block->block.stmts);

    p->stmt_depth--;
    parser_expect(p, TK_RBRACE);
    return block;
}

/* 内部辅助实现 */

/* 内部辅助逻辑 */
static zan_token_t lexer_peek_n(zan_parser_t *p, int k) {
    size_t pos = p->lex->pos;
    uint32_t line = p->lex->line;
    uint32_t col = p->lex->col;
    int idepth = p->lex->interp_depth;
    int dcount = p->lex->define_count;
    int nsave = idepth < ZAN_MAX_INTERP_DEPTH ? idepth : ZAN_MAX_INTERP_DEPTH;
    zan_interp_level_t istack[ZAN_MAX_INTERP_DEPTH];
    if (nsave > 0)
        memcpy(istack, p->lex->interp_stack, sizeof(istack[0]) * (size_t)nsave);
    /* 内部辅助逻辑 */
    int cdep = p->lex->cond_depth;
    int cover = p->lex->cond_overflow;
    int cstack[ZAN_PP_MAX_COND_DEPTH];
    int cseen[ZAN_PP_MAX_COND_DEPTH];
    int csave = cdep < ZAN_PP_MAX_COND_DEPTH ? cdep : ZAN_PP_MAX_COND_DEPTH;
    if (csave > 0) {
        memcpy(cstack, p->lex->cond_stack, sizeof(cstack[0]) * (size_t)csave);
        memcpy(cseen, p->lex->cond_seen_true, sizeof(cseen[0]) * (size_t)csave);
    }
    zan_token_t tok = {0};
    for (int i = 0; i < k; i++) tok = zan_lexer_next(p->lex);
    p->lex->pos = pos;
    p->lex->line = line;
    p->lex->col = col;
    p->lex->interp_depth = idepth;
    p->lex->define_count = dcount;
    if (nsave > 0)
        memcpy(p->lex->interp_stack, istack, sizeof(istack[0]) * (size_t)nsave);
    p->lex->cond_depth = cdep;
    p->lex->cond_overflow = cover;
    if (csave > 0) {
        memcpy(p->lex->cond_stack, cstack, sizeof(cstack[0]) * (size_t)csave);
        memcpy(p->lex->cond_seen_true, cseen, sizeof(cseen[0]) * (size_t)csave);
    }
    return tok;
}

static bool looks_like_decon_decl(zan_parser_t *p) {
    if (!parser_check(p, TK_LPAREN)) return false;
    zan_token_t a = lexer_peek_n(p, 1);
    /* 内部辅助逻辑 */
    bool type_kw = a.kind == TK_INT || a.kind == TK_LONG || a.kind == TK_SHORT ||
                   a.kind == TK_BYTE || a.kind == TK_UINT || a.kind == TK_ULONG ||
                   a.kind == TK_USHORT || a.kind == TK_SBYTE || a.kind == TK_FLOAT ||
                   a.kind == TK_DOUBLE || a.kind == TK_DECIMAL || a.kind == TK_BOOL ||
                   a.kind == TK_CHAR || a.kind == TK_STRING || a.kind == TK_OBJECT ||
                   a.kind == TK_NINT || a.kind == TK_VAR;
    if (a.kind != TK_IDENT && !type_kw) return false;
    /* 编译器代码生成与运行时系统底层调用契约 */
    zan_token_t b = lexer_peek_n(p, 2);
    if (b.kind != TK_IDENT) return false;
    zan_token_t c = lexer_peek_n(p, 3);
    if (c.kind != TK_COMMA && c.kind != TK_RPAREN) return false;

    /* 内部辅助逻辑 */
    int depth = 0;
    for (int k = 1; k < 64; k++) {
        zan_token_t tk = lexer_peek_n(p, k);
        if (tk.kind == TK_LPAREN) {
            depth++;
        } else if (tk.kind == TK_RPAREN) {
            if (depth == 0) {
                zan_token_t after = lexer_peek_n(p, k + 1);
                return after.kind == TK_EQ;
            }
            depth--;
        } else if (tk.kind == TK_SEMICOLON || tk.kind == TK_EOF) {
            return false;
        }
    }
    return false;
}

/* 内部辅助逻辑 */
static zan_ast_node_t *parse_tuple_decon_body(zan_parser_t *p, zan_loc_t loc,
                                              zan_ast_node_t *type_prefix) {
    zan_ast_node_t *n = zan_ast_new(p->arena, AST_TUPLE_DECON, loc);
    zan_ast_list_init(&n->tuple_decon.names);
    zan_ast_list_init(&n->tuple_decon.types);

    /* 编译器代码生成与运行时系统底层调用契约 */
    if (type_prefix) {
        bool is_var = type_prefix->kind == AST_TYPE_REF &&
                      type_prefix->type_ref.name.len == 3 &&
                      memcmp(type_prefix->type_ref.name.str, "var", 3) == 0;
        if (!is_var) {
            /* 内部辅助逻辑 */
            zan_diag_emit(p->diag, DIAG_ERROR, loc,
                          "unsupported tuple deconstruction form");
            return n;
        }
    }

    if (parser_check(p, TK_LPAREN)) parser_advance(p);

    while (!parser_check(p, TK_RPAREN) && !parser_check(p, TK_EOF)) {
        /* 核心系统底层抽象与内存语义契约 */
        zan_ast_node_t *elem_type = NULL;
        bool is_kw_type =
            parser_check(p, TK_INT) || parser_check(p, TK_LONG) ||
            parser_check(p, TK_SHORT) || parser_check(p, TK_BYTE) ||
            parser_check(p, TK_UINT) || parser_check(p, TK_ULONG) ||
            parser_check(p, TK_USHORT) || parser_check(p, TK_SBYTE) ||
            parser_check(p, TK_FLOAT) || parser_check(p, TK_DOUBLE) ||
            parser_check(p, TK_DECIMAL) || parser_check(p, TK_BOOL) ||
            parser_check(p, TK_CHAR) || parser_check(p, TK_STRING) ||
            parser_check(p, TK_OBJECT) || parser_check(p, TK_NINT) ||
            parser_check(p, TK_VAR);
        if (is_kw_type) {
            elem_type = parse_type_ref(p);
        } else if (parser_check(p, TK_IDENT)) {
            zan_token_t nxt = zan_lexer_peek(p->lex);
            if (nxt.kind != TK_COMMA && nxt.kind != TK_RPAREN) {
                elem_type = parse_type_ref(p);
            }
        }
        zan_ast_node_t *name = zan_ast_new(p->arena, AST_IDENTIFIER, p->current.loc);
        if (parser_check(p, TK_IDENT)) {
            parser_advance(p);
            name->ident.name = p->previous.str_val;
        } else {
            zan_diag_emit(p->diag, DIAG_ERROR, p->current.loc,
                          "expected variable name in deconstruction");
            name->ident.name = (zan_istr_t){ (char *)"__bad", 5 };
        }
        zan_ast_list_push(&n->tuple_decon.names, name, p->arena);
        zan_ast_list_push(&n->tuple_decon.types, elem_type, p->arena);
        if (!parser_match(p, TK_COMMA)) break;
    }
    parser_expect(p, TK_RPAREN);
    parser_expect(p, TK_EQ);
    n->tuple_decon.initializer = parse_expression(p);
    parser_expect(p, TK_SEMICOLON);
    return n;
}

/* 底层系统交互与数据协议契约 */
static zan_ast_node_t *parse_embedded_stmt(zan_parser_t *p) {
    if (parser_check(p, TK_LBRACE)) {
        return parse_block(p);
    }

    /* 内部辅助逻辑 */
    if (p->stmt_depth >= ZAN_PARSER_MAX_STMT_DEPTH) {
        zan_diag_emit(p->diag, DIAG_ERROR, p->current.loc,
                      "statement nesting too deep (max %d)",
                      ZAN_PARSER_MAX_STMT_DEPTH);
        parser_advance(p); /* 底层系统交互与数据协议契约 */
        zan_ast_node_t *empty = zan_ast_new(p->arena, AST_BLOCK,
                                            p->current.loc);
        zan_ast_list_init(&empty->block.stmts);
        return empty;
    }
    p->stmt_depth++;

    zan_loc_t loc = p->current.loc;
    if (looks_like_var_decl(p)) {
        zan_diag_emit(p->diag, DIAG_ERROR, loc,
                      "a declaration cannot be used as a single-statement body; "
                      "enclose it in braces");
    }

    zan_ast_node_t *block = zan_ast_new(p->arena, AST_BLOCK, loc);
    zan_ast_list_init(&block->block.stmts);
    uint32_t before = p->current.loc.offset;
    zan_ast_node_t *stmt = parse_statement(p);
    if (stmt) {
        zan_ast_list_push(&block->block.stmts, stmt, p->arena);
        splice_pending_stmts(p, &block->block.stmts);
    }
    if (p->current.loc.offset == before && !parser_check(p, TK_EOF)) {
        parser_advance(p);
    }
    p->stmt_depth--;
    return block;
}

/* 内部辅助逻辑 */
static bool looks_like_var_decl(zan_parser_t *p) {
    if (parser_check(p, TK_VAR) || parser_check(p, TK_LET) || parser_check(p, TK_CONST)) {
        return true;
    }
    switch (p->current.kind) {
    case TK_INT: case TK_LONG: case TK_SHORT: case TK_BYTE:
    case TK_UINT: case TK_ULONG: case TK_USHORT: case TK_SBYTE:
    case TK_FLOAT: case TK_DOUBLE: case TK_DECIMAL: case TK_BOOL: case TK_CHAR:
    case TK_STRING: case TK_VOID: case TK_OBJECT: case TK_NINT: {
        /* `int x = 3;` declares, but `int */
        if (p->current.kind == TK_VOID) return true;
        const char *s = p->lex->source;
        size_t q = p->lex->pos, n = p->lex->source_len;
        #define ZAN_TKW_WS(ch) ((ch)==' '||(ch)=='\t'||(ch)=='\r'||(ch)=='\n')
        while (q < n && ZAN_TKW_WS(s[q])) q++;
        while (q < n && s[q] == '[') {
            size_t r = q + 1;
            /* 数组秩修饰符解析：方括号间仅允许逗号与空白 */
            while (r < n && (ZAN_TKW_WS(s[r]) || s[r] == ',')) r++;
            if (r >= n || s[r] != ']') return false;
            q = r + 1;
            while (q < n && ZAN_TKW_WS(s[q])) q++;
        }
        if (q < n && s[q] == '.') return false;
        #undef ZAN_TKW_WS
        return true;
    }
    case TK_IDENT: {
        zan_token_t peek = zan_lexer_peek(p->lex);
        if (peek.kind == TK_IDENT) {
            return true;
        }
        /* 底层系统交互与数据协议契约 */
        if (peek.kind == TK_LESS) {
            const char *s = p->lex->source;
            size_t q = p->lex->pos, n = p->lex->source_len;
            #define ZAN_GA_WS(ch) ((ch)==' '||(ch)=='\t'||(ch)=='\r'||(ch)=='\n')
            #define ZAN_GA_IDSTART(ch) (((ch)>='a'&&(ch)<='z')||((ch)>='A'&&(ch)<='Z')||(ch)=='_')
            while (q < n && ZAN_GA_WS(s[q])) q++;
            if (q >= n || s[q] != '<') return false;
            int gd = 0;
            while (q < n) {
                char gc = s[q];
                if (gc == '<') { gd++; }
                else if (gc == '>') { gd--; if (gd == 0) { q++; break; } }
                else if (gc == ';' || gc == '(' || gc == ')' ||
                         gc == '{' || gc == '}' || gc == '=') break;
                q++;
            }
            if (gd != 0) return false;
            while (q < n && ZAN_GA_WS(s[q])) q++;
            while (q < n && s[q] == '[') {
                size_t r = q + 1;
                while (r < n && ZAN_GA_WS(s[r])) r++;
                if (r >= n || s[r] != ']') return false;
                q = r + 1;
                while (q < n && ZAN_GA_WS(s[q])) q++;
            }
            return q < n && ZAN_GA_IDSTART(s[q]);
            #undef ZAN_GA_WS
            #undef ZAN_GA_IDSTART
        }
        /* 核心系统底层抽象与内存语义契约 */
        if (peek.kind == TK_DOT) {
            const char *s = p->lex->source;
            size_t q = p->lex->pos, n = p->lex->source_len;
            #define ZAN_WS(ch) ((ch)==' '||(ch)=='\t'||(ch)=='\r'||(ch)=='\n')
            #define ZAN_IDSTART(ch) (((ch)>='a'&&(ch)<='z')||((ch)>='A'&&(ch)<='Z')||(ch)=='_')
            #define ZAN_IDCONT(ch) (ZAN_IDSTART(ch)||((ch)>='0'&&(ch)<='9'))
            for (;;) {
                while (q < n && ZAN_WS(s[q])) q++;
                if (q >= n || s[q] != '.') break;
                q++;
                while (q < n && ZAN_WS(s[q])) q++;
                if (q >= n || !ZAN_IDSTART(s[q])) return false;
                while (q < n && ZAN_IDCONT(s[q])) q++;
            }
            while (q < n && ZAN_WS(s[q])) q++;
            if (q < n && s[q] == '<') {
                /* `A */
                int gd = 0; size_t r = q;
                while (r < n) {
                    char gc = s[r];
                    if (gc == '<') gd++;
                    else if (gc == '>') { gd--; if (gd == 0) { r++; break; } }
                    else if (gc == ';' || gc == '(' || gc == ')' ||
                             gc == '{' || gc == '}') break;
                    r++;
                }
                if (gd != 0) return false;
                while (r < n && ZAN_WS(s[r])) r++;
                return r < n && ZAN_IDSTART(s[r]);
            }
            if (q < n && s[q] == '[') {
                size_t r = q + 1;
                while (r < n && ZAN_WS(s[r])) r++;
                if (r < n && s[r] == ']') return true;
                return false;
            }
            if (q < n && ZAN_IDSTART(s[q])) {
                char w[8]; size_t wl = 0; size_t r = q;
                while (r < n && ZAN_IDCONT(s[r]) && wl < sizeof w - 1) w[wl++] = s[r++];
                w[wl] = 0;
                if ((wl==2 && (w[0]=='i'&&w[1]=='s')) ||
                    (wl==2 && (w[0]=='a'&&w[1]=='s')) ||
                    (wl==2 && (w[0]=='i'&&w[1]=='n')))
                    return false;
                return true;
            }
            #undef ZAN_WS
            #undef ZAN_IDSTART
            #undef ZAN_IDCONT
            return false;
        }
        /* 内部辅助逻辑 */
        if (peek.kind == TK_QUESTION) {
            const char *s = p->lex->source;
            size_t q = p->lex->pos, n = p->lex->source_len;
            #define ZAN_NQ_WS(ch) ((ch)==' '||(ch)=='\t'||(ch)=='\r'||(ch)=='\n')
            #define ZAN_NQ_IDSTART(ch) (((ch)>='a'&&(ch)<='z')||((ch)>='A'&&(ch)<='Z')||(ch)=='_')
            #define ZAN_NQ_IDCONT(ch) (ZAN_NQ_IDSTART(ch)||((ch)>='0'&&(ch)<='9'))
            bool is_decl = false;
            while (q < n && ZAN_NQ_WS(s[q])) q++;
            if (q < n && s[q] == '?') {
                q++;
                /* 核心系统底层抽象与内存语义契约 */
                if (q < n && s[q] != '?' && s[q] != '.' && s[q] != '[') {
                    while (q < n && ZAN_NQ_WS(s[q])) q++;
                    if (q < n && ZAN_NQ_IDSTART(s[q])) {
                        while (q < n && ZAN_NQ_IDCONT(s[q])) q++;
                        while (q < n && ZAN_NQ_WS(s[q])) q++;
                        is_decl = q < n && (s[q] == ';' ||
                            (s[q] == '=' && (q + 1 >= n || s[q + 1] != '=')));
                    }
                }
            }
            #undef ZAN_NQ_WS
            #undef ZAN_NQ_IDSTART
            #undef ZAN_NQ_IDCONT
            return is_decl;
        }
        /* 内部辅助逻辑 */
        if (peek.kind == TK_LBRACKET) {
            const char *s = p->lex->source;
            size_t q = p->lex->pos, n = p->lex->source_len;
            while (q < n && (s[q] == ' ' || s[q] == '\t' ||
                             s[q] == '\r' || s[q] == '\n')) q++;
            if (q < n && s[q] == '[') {
                q++;
                while (q < n && (s[q] == ' ' || s[q] == '\t' ||
                                 s[q] == '\r' || s[q] == '\n')) q++;
                if (q < n && s[q] == ']') return true;
            }
        }
        return false;
    }
    case TK_LPAREN: {
        /* 内部辅助逻辑 */
        int depth = 0;
        for (int k = 1; k < 64; k++) {
            zan_token_t tk = lexer_peek_n(p, k);
            if (tk.kind == TK_LPAREN) {
                depth++;
            } else if (tk.kind == TK_RPAREN) {
                if (depth == 0) {
                    zan_token_t nm = lexer_peek_n(p, k + 1);
                    return nm.kind == TK_IDENT;
                }
                depth--;
            } else if (tk.kind == TK_SEMICOLON || tk.kind == TK_EOF ||
                       tk.kind == TK_EQ_EQ) {
                return false;
            }
        }
        return false;
    }
    default:
        return false;
    }
}

static zan_ast_node_t *parse_var_decl(zan_parser_t *p) {
    zan_loc_t loc = p->current.loc;
    bool is_const = parser_match(p, TK_CONST);
    bool is_let = parser_match(p, TK_LET);

    zan_ast_node_t *type = NULL;
    if (!is_let || parser_check(p, TK_IDENT)) {
        type = parse_type_ref(p);
    }

    /* 底层系统交互与数据协议契约 */
    if (type && type->kind == AST_TYPE_REF &&
        type->type_ref.name.len == 3 &&
        memcmp(type->type_ref.name.str, "var", 3) == 0) {
        type = NULL;
    }

    /* 核心系统底层抽象与内存语义契约 */
    if (type == NULL && parser_check(p, TK_LPAREN) && !is_const) {
        return parse_tuple_decon_body(p, loc, type);
    }

    zan_istr_t name = {0};
    if (parser_check(p, TK_IDENT)) {
        parser_advance(p);
        name = p->previous.str_val;
    } else {
        zan_diag_emit(p->diag, DIAG_ERROR, p->current.loc, "expected variable name");
    }

    zan_ast_node_t *init = NULL;
    if (parser_match(p, TK_EQ)) {
        init = parse_expression(p);
    }

    zan_ast_node_t *decl = zan_ast_new(p->arena, AST_VAR_DECL, loc);
    decl->var_decl.name = name;
    decl->var_decl.type = type;
    decl->var_decl.initializer = init;
    decl->var_decl.is_const = is_const;
    decl->var_decl.is_let = is_let;

    /* 内部辅助实现 */
    zan_ast_list_init(&p->pending_stmts);
    while (parser_match(p, TK_COMMA)) {
        zan_istr_t more = {0};
        if (parser_check(p, TK_IDENT)) {
            parser_advance(p);
            more = p->previous.str_val;
        } else {
            zan_diag_emit(p->diag, DIAG_ERROR, p->current.loc,
                          "expected variable name after ','");
            break;
        }
        zan_ast_node_t *more_init = NULL;
        if (parser_match(p, TK_EQ)) {
            more_init = parse_expression(p);
        }
        zan_ast_node_t *md = zan_ast_new(p->arena, AST_VAR_DECL, loc);
        md->var_decl.name = more;
        md->var_decl.type = type;
        md->var_decl.initializer = more_init;
        md->var_decl.is_const = is_const;
        md->var_decl.is_let = is_let;
        zan_ast_list_push(&p->pending_stmts, md, p->arena);
    }

    parser_expect(p, TK_SEMICOLON);
    return decl;
}

static zan_ast_node_t *parse_if_stmt(zan_parser_t *p) {
    zan_loc_t loc = p->current.loc;
    parser_expect(p, TK_IF);
    parser_expect(p, TK_LPAREN);
    zan_ast_node_t *cond = parse_expression(p);
    parser_expect(p, TK_RPAREN);
    zan_ast_node_t *then_body = parse_embedded_stmt(p);

    zan_ast_node_t *else_body = NULL;
    if (parser_match(p, TK_ELSE)) {
        if (parser_check(p, TK_IF)) {
            /* 内部辅助逻辑 */
            if (p->stmt_depth >= ZAN_PARSER_MAX_STMT_DEPTH) {
                zan_diag_emit(p->diag, DIAG_ERROR, p->current.loc,
                              "statement nesting too deep (max %d)",
                              ZAN_PARSER_MAX_STMT_DEPTH);
                parser_advance(p); /* 核心系统底层抽象与内存语义契约 */
            } else {
                p->stmt_depth++;
                else_body = parse_if_stmt(p);
                p->stmt_depth--;
            }
        } else {
            else_body = parse_embedded_stmt(p);
        }
    }

    zan_ast_node_t *n = zan_ast_new(p->arena, AST_IF_STMT, loc);
    n->if_stmt.cond = cond;
    n->if_stmt.then_body = then_body;
    n->if_stmt.else_body = else_body;
    return n;
}

static zan_ast_node_t *parse_while_stmt(zan_parser_t *p) {
    zan_loc_t loc = p->current.loc;
    parser_expect(p, TK_WHILE);
    parser_expect(p, TK_LPAREN);
    zan_ast_node_t *cond = parse_expression(p);
    parser_expect(p, TK_RPAREN);
    zan_ast_node_t *body = parse_embedded_stmt(p);

    zan_ast_node_t *n = zan_ast_new(p->arena, AST_WHILE_STMT, loc);
    n->while_stmt.cond = cond;
    n->while_stmt.body = body;
    return n;
}

static zan_ast_node_t *parse_for_stmt(zan_parser_t *p) {
    zan_loc_t loc = p->current.loc;
    parser_expect(p, TK_FOR);
    parser_expect(p, TK_LPAREN);

    zan_ast_node_t *init = NULL;
    /* 底层系统交互与数据协议契约 */
    zan_ast_list_t head_decls;
    zan_ast_list_init(&head_decls);
    int head_count = 0;
    if (!parser_check(p, TK_SEMICOLON)) {
        if (looks_like_var_decl(p)) {
            bool is_const = parser_match(p, TK_CONST);
            bool is_let = parser_match(p, TK_LET);

            zan_ast_node_t *dtype = NULL;
            if (!is_let || parser_check(p, TK_IDENT)) {
                dtype = parse_type_ref(p);
            }
            if (dtype && dtype->kind == AST_TYPE_REF &&
                dtype->type_ref.name.len == 3 &&
                memcmp(dtype->type_ref.name.str, "var", 3) == 0) {
                dtype = NULL;
            }

            if (dtype == NULL && parser_check(p, TK_LPAREN) && !is_const) {
                /* 核心系统底层抽象与内存语义契约 */
                init = parse_tuple_decon_body(p, loc, dtype);
            } else {
                for (;;) {
                    zan_istr_t name = {0};
                    if (parser_check(p, TK_IDENT)) {
                        parser_advance(p);
                        name = p->previous.str_val;
                    } else {
                        zan_diag_emit(p->diag, DIAG_ERROR, p->current.loc,
                                      "expected variable name");
                    }
                    zan_ast_node_t *dinit = NULL;
                    if (parser_match(p, TK_EQ)) {
                        dinit = parse_expression(p);
                    }
                    zan_ast_node_t *decl = zan_ast_new(p->arena,
                        AST_VAR_DECL, loc);
                    decl->var_decl.name = name;
                    decl->var_decl.type = dtype;
                    decl->var_decl.initializer = dinit;
                    decl->var_decl.is_const = is_const;
                    decl->var_decl.is_let = is_let;
                    zan_ast_list_push(&head_decls, decl, p->arena);
                    head_count++;
                    if (!parser_match(p, TK_COMMA)) break;
                }
                parser_expect(p, TK_SEMICOLON);
            }
            goto parse_cond;
        } else {
            /* `for (r = 0; */
            zan_loc_t eloc = p->current.loc;
            zan_ast_node_t *expr = parse_expression(p);
            parser_expect(p, TK_SEMICOLON);
            init = zan_ast_new(p->arena, AST_EXPR_STMT, eloc);
            init->expr_stmt.expr = expr;
        }
    } else {
        parser_advance(p);
    }

parse_cond:;
    zan_ast_node_t *cond = NULL;
    if (!parser_check(p, TK_SEMICOLON)) {
        cond = parse_expression(p);
    }
    parser_expect(p, TK_SEMICOLON);

    zan_ast_node_t *step = NULL;
    if (!parser_check(p, TK_RPAREN)) {
        step = parse_expression(p);
    }
    parser_expect(p, TK_RPAREN);

    zan_ast_node_t *body = parse_embedded_stmt(p);

    zan_ast_node_t *n = zan_ast_new(p->arena, AST_FOR_STMT, loc);
    n->for_stmt.init = init;
    n->for_stmt.cond = cond;
    n->for_stmt.step = step;
    n->for_stmt.body = body;
    if (head_count == 1) {
        n->for_stmt.init = head_decls.items[0];
    } else if (head_count > 1) {
        zan_ast_node_t *wrap = zan_ast_new(p->arena, AST_BLOCK, loc);
        zan_ast_list_init(&wrap->block.stmts);
        for (int i = 0; i < head_decls.count; i++) {
            zan_ast_list_push(&wrap->block.stmts, head_decls.items[i],
                              p->arena);
        }
        zan_ast_list_push(&wrap->block.stmts, n, p->arena);
        return wrap;
    }
    return n;
}

static zan_ast_node_t *parse_foreach_stmt(zan_parser_t *p) {
    zan_loc_t loc = p->current.loc;
    parser_expect(p, TK_FOREACH);
    parser_expect(p, TK_LPAREN);

    zan_ast_node_t *var_type = NULL;
    if (!parser_check(p, TK_VAR)) {
        var_type = parse_type_ref(p);
    } else {
        parser_advance(p);
    }

    zan_istr_t var_name = {0};
    if (parser_check(p, TK_IDENT)) {
        parser_advance(p);
        var_name = p->previous.str_val;
    }

    parser_expect(p, TK_IN);
    zan_ast_node_t *collection = parse_expression(p);
    parser_expect(p, TK_RPAREN);
    zan_ast_node_t *body = parse_embedded_stmt(p);

    zan_ast_node_t *n = zan_ast_new(p->arena, AST_FOREACH_STMT, loc);
    n->foreach_stmt.var_name = var_name;
    n->foreach_stmt.var_type = var_type;
    n->foreach_stmt.collection = collection;
    n->foreach_stmt.body = body;
    return n;
}

static zan_ast_node_t *parse_return_stmt(zan_parser_t *p) {
    zan_loc_t loc = p->current.loc;
    parser_expect(p, TK_RETURN);

    zan_ast_node_t *value = NULL;
    if (!parser_check(p, TK_SEMICOLON)) {
        value = parse_expression(p);
    }
    parser_expect(p, TK_SEMICOLON);

    zan_ast_node_t *n = zan_ast_new(p->arena, AST_RETURN_STMT, loc);
    n->ret.value = value;
    return n;
}

/* 内部辅助实现 */
static bool is_case_type_pattern(zan_parser_t *p) {
    switch (p->current.kind) {
    case TK_INT: case TK_LONG: case TK_SHORT: case TK_BYTE:
    case TK_UINT: case TK_ULONG: case TK_USHORT: case TK_SBYTE:
    case TK_FLOAT: case TK_DOUBLE: case TK_DECIMAL: case TK_BOOL: case TK_CHAR:
    case TK_STRING: case TK_OBJECT: case TK_NINT:
        return true;
    case TK_IDENT: {
        zan_token_t nxt = zan_lexer_peek(p->lex);
        return nxt.kind == TK_IDENT || nxt.kind == TK_LESS ||
               nxt.kind == TK_QUESTION || nxt.kind == TK_LBRACKET;
    }
    default:
        return false;
    }
}

static zan_ast_node_t *parse_switch_stmt(zan_parser_t *p) {
    zan_loc_t loc = p->current.loc;
    parser_expect(p, TK_SWITCH);
    parser_expect(p, TK_LPAREN);
    zan_ast_node_t *expr = parse_expression(p);
    parser_expect(p, TK_RPAREN);
    parser_expect(p, TK_LBRACE);

    zan_ast_node_t *n = zan_ast_new(p->arena, AST_SWITCH_STMT, loc);
    n->switch_stmt.expr = expr;
    zan_ast_list_init(&n->switch_stmt.cases);

    while (!parser_check(p, TK_RBRACE) && !parser_check(p, TK_EOF)) {
        zan_loc_t case_loc = p->current.loc;
        zan_ast_node_t *pattern = NULL;
        zan_ast_node_t *type_pattern = NULL;
        zan_ast_node_t *when_cond = NULL;
        zan_istr_t var_name = {0};

        if (parser_check(p, TK_CASE)) {
            parser_advance(p);
            if (is_case_type_pattern(p)) {
                type_pattern = parse_type_ref(p);
                if (parser_check(p, TK_IDENT)) {
                    parser_advance(p);
                    var_name = p->previous.str_val;
                }
            } else {
                /* 底层系统交互与数据协议契约 */
                pattern = parse_expression(p);
            }
            /* `case */
            if (parser_match(p, TK_WHEN)) {
                when_cond = parse_expression(p);
            }
            parser_expect(p, TK_COLON);
        } else if (parser_check(p, TK_DEFAULT)) {
            parser_advance(p);
            if (parser_match(p, TK_WHEN)) {
                when_cond = parse_expression(p);
            }
            parser_expect(p, TK_COLON);
        } else {
            break;
        }

        zan_ast_node_t *body_block = zan_ast_new(p->arena, AST_BLOCK, case_loc);
        zan_ast_list_init(&body_block->block.stmts);
        while (!parser_check(p, TK_CASE) && !parser_check(p, TK_DEFAULT) &&
               !parser_check(p, TK_RBRACE) && !parser_check(p, TK_EOF)) {
            uint32_t before = p->current.loc.offset;
            zan_ast_node_t *stmt = parse_statement(p);
            zan_ast_list_push(&body_block->block.stmts, stmt, p->arena);
            splice_pending_stmts(p, &body_block->block.stmts);
            if (p->current.loc.offset == before && !parser_check(p, TK_EOF)) {
                parser_advance(p);
            }
        }

        zan_ast_node_t *sc = zan_ast_new(p->arena, AST_SWITCH_CASE, case_loc);
        sc->switch_case.pattern = pattern;
        sc->switch_case.type_pattern = type_pattern;
        sc->switch_case.when_cond = when_cond;
        sc->switch_case.var_name = var_name;
        sc->switch_case.body = body_block;
        zan_ast_list_push(&n->switch_stmt.cases, sc, p->arena);
    }

    parser_expect(p, TK_RBRACE);
    return n;
}

static zan_ast_node_t *parse_try_stmt(zan_parser_t *p) {
    zan_loc_t loc = p->current.loc;
    parser_expect(p, TK_TRY);
    zan_ast_node_t *try_body = parse_block(p);

    zan_ast_node_t *n = zan_ast_new(p->arena, AST_TRY_STMT, loc);
    n->try_stmt.try_body = try_body;
    zan_ast_list_init(&n->try_stmt.catches);
    n->try_stmt.finally_body = NULL;

    while (parser_check(p, TK_CATCH)) {
        zan_loc_t catch_loc = p->current.loc;
        parser_advance(p);

        zan_ast_node_t *catch_type = NULL;
        zan_istr_t catch_var = {0};

        if (parser_check(p, TK_LPAREN)) {
            parser_advance(p);
            catch_type = parse_type_ref(p);
            if (parser_check(p, TK_IDENT)) {
                parser_advance(p);
                catch_var = p->previous.str_val;
            }
            parser_expect(p, TK_RPAREN);
        }

        zan_ast_node_t *catch_body = parse_block(p);

        zan_ast_node_t *cc = zan_ast_new(p->arena, AST_CATCH_CLAUSE, catch_loc);
        cc->catch_clause.type = catch_type;
        cc->catch_clause.var_name = catch_var;
        cc->catch_clause.body = catch_body;
        zan_ast_list_push(&n->try_stmt.catches, cc, p->arena);
    }

    if (parser_check(p, TK_FINALLY)) {
        parser_advance(p);
        n->try_stmt.finally_body = parse_block(p);
    }

    return n;
}

/* Skip a balanced `< */
static bool skip_angle_group(zan_parser_t *p) {
    int depth = 0;
    for (;;) {
        zan_token_t gt = zan_lexer_next(p->lex);
        if (gt.kind == TK_LESS) depth++;
        else if (gt.kind == TK_GREATER) depth--;
        else if (gt.kind == TK_GREATER_GREATER) depth -= 2;
        else if (gt.kind == TK_GREATER_GREATER_GREATER) depth -= 3;
        else if (gt.kind == TK_EOF || gt.kind == TK_SEMICOLON) return false;
        if (depth <= 0) return true;
    }
}

/* 内部辅助逻辑 */
static bool looks_like_local_func(zan_parser_t *p) {
    zan_lexer_t saved = *p->lex;

    /* 内部辅助逻辑 */
    zan_token_t t = p->current;

    switch (t.kind) {
    case TK_INT: case TK_LONG: case TK_SHORT: case TK_BYTE:
    case TK_UINT: case TK_ULONG: case TK_USHORT: case TK_SBYTE:
    case TK_FLOAT: case TK_DOUBLE: case TK_DECIMAL: case TK_BOOL: case TK_CHAR:
    case TK_STRING: case TK_VOID: case TK_OBJECT: case TK_NINT:
        break;
    case TK_IDENT: {
        for (;;) {
            zan_token_t after = zan_lexer_peek(p->lex);
            if (after.kind == TK_DOT) {
                zan_lexer_next(p->lex);
                if (zan_lexer_next(p->lex).kind != TK_IDENT) {
                    *p->lex = saved; return false;
                }
                continue;
            }
            if (after.kind == TK_LESS) {
                if (!skip_angle_group(p)) { *p->lex = saved; return false; }
                continue;
            }
            break;
        }
        break;
    }
    default:
        return false;
    }

    /* 底层系统交互与数据协议契约 */
    for (;;) {
        zan_token_t after = zan_lexer_peek(p->lex);
        if (after.kind != TK_LBRACKET) break;
        int depth = 0;
        for (;;) {
            zan_token_t bt = zan_lexer_next(p->lex);
            if (bt.kind == TK_LBRACKET) depth++;
            else if (bt.kind == TK_RBRACKET) depth--;
            else if (bt.kind == TK_EOF || bt.kind == TK_SEMICOLON) {
                *p->lex = saved; return false;
            }
            if (depth <= 0) break;
        }
    }

    if (zan_lexer_next(p->lex).kind != TK_IDENT) { *p->lex = saved; return false; }

    /* 底层系统交互与数据协议契约 */
    if (zan_lexer_peek(p->lex).kind == TK_LESS) {
        if (!skip_angle_group(p)) { *p->lex = saved; return false; }
    }

    if (zan_lexer_next(p->lex).kind != TK_LPAREN) { *p->lex = saved; return false; }

    /* 内部辅助逻辑 */
    {
        int depth = 1;
        while (depth > 0) {
            zan_token_t pt = zan_lexer_next(p->lex);
            if (pt.kind == TK_LPAREN || pt.kind == TK_LBRACKET) depth++;
            else if (pt.kind == TK_RPAREN || pt.kind == TK_RBRACKET) depth--;
            else if (pt.kind == TK_SEMICOLON || pt.kind == TK_EOF) {
                *p->lex = saved; return false;
            }
        }
    }

    zan_token_t tail = zan_lexer_next(p->lex);
    *p->lex = saved;
    return tail.kind == TK_LBRACE || tail.kind == TK_ARROW;
}

/* 内部辅助逻辑 */
static zan_ast_node_t *parse_local_func(zan_parser_t *p) {
    zan_loc_t loc = p->current.loc;

    zan_ast_node_t *ret_type = parse_type_ref(p);

    zan_istr_t name = {0};
    if (parser_check(p, TK_IDENT)) {
        parser_advance(p);
        name = p->previous.str_val;
    } else {
        zan_diag_emit(p->diag, DIAG_ERROR, p->current.loc,
                      "expected local function name");
        return parser_error_node(p);
    }

    /* 核心系统底层抽象与内存语义契约 */
    zan_ast_list_t type_params;
    zan_ast_list_init(&type_params);
    if (parser_match(p, TK_LESS)) {
        while (!parser_check(p, TK_GREATER) && !parser_check(p, TK_EOF)) {
            if (parser_check(p, TK_IDENT)) {
                parser_advance(p);
                zan_ast_node_t *tp = zan_ast_new(p->arena, AST_IDENTIFIER, p->previous.loc);
                tp->ident.name = p->previous.str_val;
                zan_ast_list_push(&type_params, tp, p->arena);
            }
            if (!parser_match(p, TK_COMMA)) break;
        }
        parser_expect(p, TK_GREATER);
    }

    zan_ast_list_t params;
    zan_ast_list_init(&params);
    parser_expect(p, TK_LPAREN);
    if (!parser_check(p, TK_RPAREN)) {
        for (;;) {
            zan_ast_node_t *param = parse_parameter(p);
            zan_ast_list_push(&params, param, p->arena);
            if (!parser_match(p, TK_COMMA)) break;
        }
    }
    parser_expect(p, TK_RPAREN);

    zan_ast_node_t *body = NULL;
    if (parser_check(p, TK_LBRACE)) {
        body = parse_block(p);
    } else if (parser_match(p, TK_ARROW)) {
        zan_ast_node_t *expr = parse_expression(p);
        parser_expect(p, TK_SEMICOLON);
        body = zan_ast_new(p->arena, AST_BLOCK, expr->loc);
        zan_ast_list_init(&body->block.stmts);
        zan_ast_node_t *ret = zan_ast_new(p->arena, AST_RETURN_STMT, expr->loc);
        ret->ret.value = expr;
        zan_ast_list_push(&body->block.stmts, ret, p->arena);
    } else {
        zan_diag_emit(p->diag, DIAG_ERROR, p->current.loc,
                      "expected local function body");
        return parser_error_node(p);
    }

    zan_ast_node_t *n = zan_ast_new(p->arena, AST_METHOD_DECL, loc);
    n->method_decl.name = name;
    n->method_decl.return_type = ret_type;
    n->method_decl.params = params;
    n->method_decl.type_params = type_params;
    n->method_decl.body = body;
    n->method_decl.modifiers = MOD_PRIVATE | MOD_STATIC;
    n->method_decl.has_base_init = false;
    n->method_decl.has_this_init = false;
    zan_ast_list_push(&p->pending_members, n, p->arena);

    /* 底层系统交互与数据协议契约 */
    zan_ast_node_t *noop = zan_ast_new(p->arena, AST_BLOCK, loc);
    zan_ast_list_init(&noop->block.stmts);
    return noop;
}

static zan_ast_node_t *parse_statement(zan_parser_t *p) {
    /* 内部辅助逻辑 */
    if (p->current.kind == TK_IDENT && p->current.str_val.len == 5 &&
        memcmp(p->current.str_val.str, "yield", 5) == 0) {
        zan_token_kind_t nk = zan_lexer_peek(p->lex).kind;
        if (nk == TK_RETURN || nk == TK_BREAK) {
            zan_loc_t loc = p->current.loc;
            parser_advance(p);
            parser_advance(p);
            zan_ast_node_t *n = zan_ast_new(p->arena, AST_YIELD_STMT, loc);
            n->yield_stmt.value =
                (nk == TK_RETURN) ? parse_expression(p) : NULL;
            parser_expect(p, TK_SEMICOLON);
            return n;
        }
    }

    /* 核心系统底层抽象与内存语义契约 */
    if (p->current.kind == TK_IDENT && zan_lexer_peek(p->lex).kind == TK_COLON) {
        zan_loc_t loc = p->current.loc;
        parser_advance(p);
        zan_istr_t lname = p->previous.str_val;
        parser_advance(p);
        zan_ast_node_t *n = zan_ast_new(p->arena, AST_LABEL_STMT, loc);
        n->ident.name = lname;
        return n;
    }

    /* `checked { */
    if (is_checked_use(p) && zan_lexer_peek(p->lex).kind == TK_LBRACE) {
        zan_loc_t loc = p->current.loc;
        bool is_checked = p->current.str_val.len == 7;
        parser_advance(p);
        if (is_checked) p->checked_depth++; else p->unchecked_depth++;
        zan_ast_node_t *blk = parse_block(p);
        if (is_checked) p->checked_depth--; else p->unchecked_depth--;
        /* 内部辅助逻辑 */
        zan_ast_node_t *n = zan_ast_new(p->arena, AST_CHECKED_STMT, loc);
        n->checked_stmt.checked = is_checked;
        n->checked_stmt.body = blk;
        return n;
    }

    switch (p->current.kind) {
    case TK_LBRACE:
        return parse_block(p);
    case TK_IF:
        return parse_if_stmt(p);
    case TK_WHILE:
        return parse_while_stmt(p);
    case TK_FOR:
        return parse_for_stmt(p);
    case TK_FOREACH:
        return parse_foreach_stmt(p);
    case TK_RETURN:
        return parse_return_stmt(p);
    case TK_BREAK: {
        zan_loc_t loc = p->current.loc;
        parser_advance(p);
        parser_expect(p, TK_SEMICOLON);
        return zan_ast_new(p->arena, AST_BREAK_STMT, loc);
    }
    case TK_CONTINUE: {
        zan_loc_t loc = p->current.loc;
        parser_advance(p);
        parser_expect(p, TK_SEMICOLON);
        return zan_ast_new(p->arena, AST_CONTINUE_STMT, loc);
    }
    case TK_THROW: {
        zan_loc_t loc = p->current.loc;
        parser_advance(p);
        /* 内部辅助逻辑 */
        zan_ast_node_t *value = NULL;
        if (p->current.kind != TK_SEMICOLON)
            value = parse_expression(p);
        parser_expect(p, TK_SEMICOLON);
        zan_ast_node_t *n = zan_ast_new(p->arena, AST_THROW_STMT, loc);
        n->throw_stmt.value = value;
        return n;
    }
    case TK_SWITCH:
        return parse_switch_stmt(p);
    case TK_TRY:
        return parse_try_stmt(p);
    case TK_DEFER: {
        zan_loc_t loc = p->current.loc;
        /* 核心系统底层抽象与内存语义契约 */
        if (p->stmt_depth >= ZAN_PARSER_MAX_STMT_DEPTH) {
            zan_diag_emit(p->diag, DIAG_ERROR, loc,
                          "statement nesting too deep (max %d)",
                          ZAN_PARSER_MAX_STMT_DEPTH);
            parser_advance(p); /* 核心系统底层抽象与内存语义契约 */
            zan_ast_node_t *fb = zan_ast_new(p->arena, AST_BLOCK, loc);
            zan_ast_list_init(&fb->block.stmts);
            return fb;
        }
        p->stmt_depth++;
        parser_advance(p);
        zan_ast_node_t *stmt = parse_statement(p);
        p->stmt_depth--;
        zan_ast_node_t *n = zan_ast_new(p->arena, AST_BLOCK, loc);
        zan_ast_list_init(&n->block.stmts);
        zan_ast_node_t *try_node = zan_ast_new(p->arena, AST_TRY_STMT, loc);
        zan_ast_node_t *empty_try = zan_ast_new(p->arena, AST_BLOCK, loc);
        zan_ast_list_init(&empty_try->block.stmts);
        try_node->try_stmt.try_body = empty_try;
        zan_ast_list_init(&try_node->try_stmt.catches);
        zan_ast_node_t *fin = stmt;
        if (!fin || fin->kind != AST_BLOCK) {
            zan_ast_node_t *fb = zan_ast_new(p->arena, AST_BLOCK, loc);
            zan_ast_list_init(&fb->block.stmts);
            if (stmt) zan_ast_list_push(&fb->block.stmts, stmt, p->arena);
            fin = fb;
        }
        try_node->try_stmt.finally_body = fin;
        zan_ast_list_push(&n->block.stmts, try_node, p->arena);
        return n;
    }
    case TK_USING: {
        /* 内部辅助逻辑 */
        zan_loc_t loc = p->current.loc;
        parser_advance(p);
        parser_expect(p, TK_LPAREN);

        zan_istr_t name = {NULL, 0};
        zan_ast_node_t *type = NULL;
        zan_ast_node_t *init = NULL;

        if (looks_like_var_decl(p)) {
            /* 内部辅助逻辑 */
            zan_ast_node_t *tref = parse_type_ref(p);
            type = tref;
            if (parser_check(p, TK_IDENT)) {
                parser_advance(p);
                name = p->previous.str_val;
            } else {
                zan_diag_emit(p->diag, DIAG_ERROR, p->current.loc,
                              "expected variable name in using declaration");
            }
            if (parser_match(p, TK_EQ)) {
                init = parse_expression(p);
            }
        } else {
            /* 底层系统交互与数据协议契约 */
            init = parse_expression(p);
            char buf[32];
            snprintf(buf, sizeof buf, "__using%d", p->synth_counter++);
            name = (zan_istr_t){ zan_arena_strdup(p->arena, buf, (int)strlen(buf)),
                                 (uint32_t)strlen(buf) };
        }
        parser_expect(p, TK_RPAREN);
        zan_ast_node_t *body = parse_embedded_stmt(p);

        /* 底层系统交互与数据协议契约 */
        zan_ast_node_t *outer = zan_ast_new(p->arena, AST_BLOCK, loc);
        zan_ast_list_init(&outer->block.stmts);

        if (type || !init) {
            /* 内部辅助逻辑 */
            zan_ast_node_t *decl = zan_ast_new(p->arena, AST_VAR_DECL, loc);
            decl->var_decl.name = name;
            decl->var_decl.type = type; /* 核心系统底层抽象与内存语义契约 */
            decl->var_decl.initializer = init;
            decl->var_decl.is_const = false;
            decl->var_decl.is_let = false;
            zan_ast_list_push(&outer->block.stmts, decl, p->arena);
        } else if (init) {
            /* 底层系统交互与数据协议契约 */
            zan_ast_node_t *decl = zan_ast_new(p->arena, AST_VAR_DECL, loc);
            decl->var_decl.name = name;
            decl->var_decl.type = NULL; /* var (inferred) */
            decl->var_decl.initializer = init;
            decl->var_decl.is_const = false;
            decl->var_decl.is_let = false;
            zan_ast_list_push(&outer->block.stmts, decl, p->arena);
        }

        zan_ast_node_t *try_node = zan_ast_new(p->arena, AST_TRY_STMT, loc);
        try_node->try_stmt.try_body = body;
        zan_ast_list_init(&try_node->try_stmt.catches);
        try_node->try_stmt.finally_body = NULL;

        /* finally { name.Dispose(); } */
        zan_ast_node_t *fin = zan_ast_new(p->arena, AST_BLOCK, loc);
        zan_ast_list_init(&fin->block.stmts);
        zan_ast_node_t *callee = zan_ast_new(p->arena, AST_MEMBER_ACCESS, loc);
        zan_ast_node_t *res_ref = zan_ast_new(p->arena, AST_IDENTIFIER, loc);
        res_ref->ident.name = name;
        callee->member.object = res_ref;
        callee->member.name = (zan_istr_t){ (char *)"Dispose", 7 };
        callee->member.null_cond = false;
        zan_ast_node_t *dispose_call = zan_ast_new(p->arena, AST_CALL, loc);
        dispose_call->call.callee = callee;
        zan_ast_list_init(&dispose_call->call.args);
        zan_ast_list_init(&dispose_call->call.type_args);
        zan_ast_node_t *estmt = zan_ast_new(p->arena, AST_EXPR_STMT, loc);
        estmt->expr_stmt.expr = dispose_call;
        zan_ast_list_push(&fin->block.stmts, estmt, p->arena);
        try_node->try_stmt.finally_body = fin;

        zan_ast_list_push(&outer->block.stmts, try_node, p->arena);
        return outer;
    }
    case TK_LOCK: {
        zan_loc_t loc = p->current.loc;
        parser_advance(p);
        parser_expect(p, TK_LPAREN);
        zan_ast_node_t *expr = parse_expression(p);
        parser_expect(p, TK_RPAREN);
        zan_ast_node_t *n = zan_ast_new(p->arena, AST_LOCK_STMT, loc);
        n->lock_stmt.expr = expr;
        /* 模块核心语义抽象与接口调用契约 */
        n->lock_stmt.body = parse_embedded_stmt(p);
        return n;
    }
    case TK_GOTO: {
        zan_loc_t loc = p->current.loc;
        parser_advance(p);
        parser_expect(p, TK_IDENT);
        zan_ast_node_t *n = zan_ast_new(p->arena, AST_GOTO_STMT, loc);
        n->ident.name = p->previous.str_val;
        parser_expect(p, TK_SEMICOLON);
        return n;
    }
    case TK_UNSAFE:
        /* unsafe { */
        parser_advance(p);
        return parse_block(p);
    case TK_FIXED: {
        /* 内部辅助逻辑 */
        zan_loc_t loc = p->current.loc;
        parser_advance(p);
        parser_expect(p, TK_LPAREN);
        zan_ast_node_t *type = parse_type_ref(p);
        parser_match(p, TK_STAR); /* 核心系统底层抽象与内存语义契约 */
        parser_expect(p, TK_IDENT);
        zan_istr_t vname = p->previous.str_val;
        parser_expect(p, TK_EQ);
        zan_ast_node_t *init = parse_expression(p);
        parser_expect(p, TK_RPAREN);
        zan_ast_node_t *decl = zan_ast_new(p->arena, AST_VAR_DECL, loc);
        decl->var_decl.name = vname;
        decl->var_decl.type = type;
        decl->var_decl.initializer = init;
        decl->var_decl.is_const = false;
        decl->var_decl.is_let = false;
        /* 底层系统交互与数据协议契约 */
        zan_ast_node_t *body = parse_embedded_stmt(p);
        zan_ast_node_t *blk = zan_ast_new(p->arena, AST_BLOCK, loc);
        zan_ast_list_init(&blk->block.stmts);
        zan_ast_list_push(&blk->block.stmts, decl, p->arena);
        zan_ast_list_push(&blk->block.stmts, body, p->arena);
        return blk;
    }
    case TK_DO: {
        zan_loc_t loc = p->current.loc;
        parser_advance(p);
        zan_ast_node_t *body = parse_embedded_stmt(p);
        parser_expect(p, TK_WHILE);
        parser_expect(p, TK_LPAREN);
        zan_ast_node_t *cond = parse_expression(p);
        parser_expect(p, TK_RPAREN);
        parser_expect(p, TK_SEMICOLON);
        zan_ast_node_t *n = zan_ast_new(p->arena, AST_DO_WHILE_STMT, loc);
        n->while_stmt.cond = cond;
        n->while_stmt.body = body;
        return n;
    }
    default:
        break;
    }

    /* 内部辅助实现 */
    if (looks_like_local_func(p)) {
        return parse_local_func(p);
    }

    /* 核心系统底层抽象与内存语义契约 */
    if (looks_like_decon_decl(p)) {
        zan_loc_t dloc = p->current.loc;
        return parse_tuple_decon_body(p, dloc, NULL);
    }

    if (looks_like_var_decl(p)) {
        return parse_var_decl(p);
    }

    zan_loc_t loc = p->current.loc;
    zan_ast_node_t *expr = parse_expression(p);
    parser_expect(p, TK_SEMICOLON);
    zan_ast_node_t *n = zan_ast_new(p->arena, AST_EXPR_STMT, loc);
    n->expr_stmt.expr = expr;
    return n;
}

/* `where T : C1, C2 */
static void parse_where_clauses(zan_parser_t *p, zan_ast_list_t *out) {
    while (parser_check(p, TK_WHERE)) {
        parser_advance(p);
        zan_loc_t loc = p->current.loc;
        zan_istr_t pname = {NULL, 0};
        if (parser_check(p, TK_IDENT)) {
            parser_advance(p);
            pname = p->previous.str_val;
        } else {
            zan_diag_emit(p->diag, DIAG_ERROR, loc,
                          "expected type parameter name after 'where'");
        }
        parser_expect(p, TK_COLON);
        zan_ast_node_t *n = zan_ast_new(p->arena, AST_WHERE_CLAUSE, loc);
        n->where_clause.param_name = pname;
        zan_ast_list_init(&n->where_clause.constraints);
        do {
            if (parser_check(p, TK_CLASS) || parser_check(p, TK_STRUCT)) {
                parser_advance(p);
                continue;
            }
            if (parser_check(p, TK_NEW)) {
                parser_advance(p);
                parser_expect(p, TK_LPAREN);
                parser_expect(p, TK_RPAREN);
                continue;
            }
            zan_ast_node_t *cons = parse_type_ref(p);
            zan_ast_list_push(&n->where_clause.constraints, cons, p->arena);
        } while (parser_match(p, TK_COMMA));
        zan_ast_list_push(out, n, p->arena);
    }
}

static zan_ast_node_t *parse_parameter(zan_parser_t *p) {
    zan_loc_t loc = p->current.loc;

    /* 内部辅助逻辑 */
    int is_this = 0;
    if (parser_check(p, TK_THIS)) {
        parser_advance(p);
        is_this = 1;
    }

    /* Contextual `params` modifier: `params T[] rest` */
    int is_params = 0;
    if (parser_check(p, TK_IDENT) && p->current.str_val.len == 6 &&
        memcmp(p->current.str_val.str, "params", 6) == 0) {
        parser_advance(p);
        is_params = 1;
    }

    int by_ref = 0;
    if (parser_check(p, TK_REF)) { parser_advance(p); by_ref = 1; }
    else if (parser_check(p, TK_OUT)) { parser_advance(p); by_ref = 2; }

    zan_ast_node_t *type = parse_type_ref(p);

    zan_istr_t name = {0};
    if (parser_check(p, TK_IDENT)) {
        parser_advance(p);
        name = p->previous.str_val;
    }

    zan_ast_node_t *default_val = NULL;
    if (parser_match(p, TK_EQ)) {
        default_val = parse_expression(p);
    }

    zan_ast_node_t *param = zan_ast_new(p->arena, AST_PARAM, loc);
    param->param.name = name;
    param->param.type = type;
    param->param.default_val = default_val;
    param->param.is_params = is_params;
    param->param.by_ref = by_ref;
    param->param.is_this = is_this;
    return param;
}

static zan_ast_list_t parse_param_list(zan_parser_t *p) {
    zan_ast_list_t params;
    zan_ast_list_init(&params);

    parser_expect(p, TK_LPAREN);
    while (!parser_check(p, TK_RPAREN) && !parser_check(p, TK_EOF)) {
        zan_ast_node_t *param = parse_parameter(p);
        zan_ast_list_push(&params, param, p->arena);
        if (!parser_match(p, TK_COMMA)) break;
    }
    parser_expect(p, TK_RPAREN);

    return params;
}

/* 内部辅助实现 */

static bool stmt_contains_yield(zan_ast_node_t *s);

static bool list_contains_yield(zan_ast_list_t *l) {
    for (int i = 0; i < l->count; i++)
        if (stmt_contains_yield(l->items[i])) return true;
    return false;
}

static bool stmt_contains_yield(zan_ast_node_t *s) {
    if (!s) return false;
    switch (s->kind) {
    case AST_YIELD_STMT:   return true;
    case AST_BLOCK:        return list_contains_yield(&s->block.stmts);
    case AST_IF_STMT:      return stmt_contains_yield(s->if_stmt.then_body) ||
                                  stmt_contains_yield(s->if_stmt.else_body);
    case AST_WHILE_STMT:
    case AST_DO_WHILE_STMT: return stmt_contains_yield(s->while_stmt.body);
    case AST_FOR_STMT:     return stmt_contains_yield(s->for_stmt.body);
    case AST_FOREACH_STMT: return stmt_contains_yield(s->foreach_stmt.body);
    case AST_TRY_STMT:
        if (stmt_contains_yield(s->try_stmt.try_body)) return true;
        for (int i = 0; i < s->try_stmt.catches.count; i++)
            if (stmt_contains_yield(s->try_stmt.catches.items[i]->catch_clause.body))
                return true;
        return stmt_contains_yield(s->try_stmt.finally_body);
    case AST_SWITCH_STMT:
        for (int i = 0; i < s->switch_stmt.cases.count; i++)
            if (stmt_contains_yield(s->switch_stmt.cases.items[i]->switch_case.body))
                return true;
        return false;
    default: return false;
    }
}

static zan_ast_node_t *make_yield_ident(zan_parser_t *p, zan_loc_t loc) {
    zan_ast_node_t *id = zan_ast_new(p->arena, AST_IDENTIFIER, loc);
    id->ident.name = (zan_istr_t){"__yield", 7};
    return id;
}

static void rewrite_yield_stmt(zan_parser_t *p, zan_ast_node_t **slot) {
    zan_ast_node_t *s = *slot;
    if (!s) return;
    switch (s->kind) {
    case AST_YIELD_STMT: {
        if (s->yield_stmt.value) {
            /* __yield.Add(value); */
            zan_ast_node_t *mem = zan_ast_new(p->arena, AST_MEMBER_ACCESS, s->loc);
            mem->member.object = make_yield_ident(p, s->loc);
            mem->member.name = (zan_istr_t){"Add", 3};
            mem->member.null_cond = 0;
            zan_ast_node_t *call = zan_ast_new(p->arena, AST_CALL, s->loc);
            call->call.callee = mem;
            zan_ast_list_init(&call->call.args);
            zan_ast_list_init(&call->call.type_args);
            zan_ast_list_push(&call->call.args, s->yield_stmt.value, p->arena);
            zan_ast_node_t *es = zan_ast_new(p->arena, AST_EXPR_STMT, s->loc);
            es->expr_stmt.expr = call;
            *slot = es;
        } else {
            /* return __yield; */
            zan_ast_node_t *ret = zan_ast_new(p->arena, AST_RETURN_STMT, s->loc);
            ret->ret.value = make_yield_ident(p, s->loc);
            *slot = ret;
        }
        return;
    }
    case AST_BLOCK:
        for (int i = 0; i < s->block.stmts.count; i++)
            rewrite_yield_stmt(p, &s->block.stmts.items[i]);
        return;
    case AST_IF_STMT:
        rewrite_yield_stmt(p, &s->if_stmt.then_body);
        rewrite_yield_stmt(p, &s->if_stmt.else_body);
        return;
    case AST_WHILE_STMT:
    case AST_DO_WHILE_STMT:
        rewrite_yield_stmt(p, &s->while_stmt.body);
        return;
    case AST_FOR_STMT:
        rewrite_yield_stmt(p, &s->for_stmt.body);
        return;
    case AST_FOREACH_STMT:
        rewrite_yield_stmt(p, &s->foreach_stmt.body);
        return;
    case AST_TRY_STMT:
        rewrite_yield_stmt(p, &s->try_stmt.try_body);
        for (int i = 0; i < s->try_stmt.catches.count; i++)
            rewrite_yield_stmt(p, &s->try_stmt.catches.items[i]->catch_clause.body);
        rewrite_yield_stmt(p, &s->try_stmt.finally_body);
        return;
    case AST_SWITCH_STMT:
        for (int i = 0; i < s->switch_stmt.cases.count; i++)
            rewrite_yield_stmt(p, &s->switch_stmt.cases.items[i]->switch_case.body);
        return;
    default: return;
    }
}

static void desugar_yield_method(zan_parser_t *p, zan_ast_node_t *m) {
    zan_ast_node_t *body = m->method_decl.body;
    if (!body || !stmt_contains_yield(body)) return;
    zan_ast_node_t *rt = m->method_decl.return_type;
    if (!rt || rt->kind != AST_TYPE_REF || rt->type_ref.type_args.count != 1) {
        zan_diag_emit(p->diag, DIAG_ERROR, m->loc,
                      "iterator method '%.*s' must return IEnumerable<T> or List<T>",
                      (int)m->method_decl.name.len, m->method_decl.name.str);
        return;
    }
    rt->type_ref.name = (zan_istr_t){"List", 4};

    zan_ast_node_t *list_type = zan_ast_new(p->arena, AST_TYPE_REF, m->loc);
    list_type->type_ref.name = (zan_istr_t){"List", 4};
    list_type->type_ref.type_args = rt->type_ref.type_args;
    list_type->type_ref.is_nullable = false;
    list_type->type_ref.is_array = false;

    zan_ast_node_t *nw = zan_ast_new(p->arena, AST_NEW_EXPR, m->loc);
    nw->new_expr.type = list_type;
    zan_ast_list_init(&nw->new_expr.args);
    nw->new_expr.is_array = false;

    zan_ast_node_t *decl = zan_ast_new(p->arena, AST_VAR_DECL, m->loc);
    decl->var_decl.name = (zan_istr_t){"__yield", 7};
    decl->var_decl.type = NULL;
    decl->var_decl.initializer = nw;
    decl->var_decl.is_const = false;
    decl->var_decl.is_let = false;

    rewrite_yield_stmt(p, &m->method_decl.body);
    body = m->method_decl.body;

    zan_ast_node_t *ret = zan_ast_new(p->arena, AST_RETURN_STMT, m->loc);
    ret->ret.value = make_yield_ident(p, m->loc);

    zan_ast_list_t old = body->block.stmts;
    zan_ast_list_init(&body->block.stmts);
    zan_ast_list_push(&body->block.stmts, decl, p->arena);
    for (int i = 0; i < old.count; i++)
        zan_ast_list_push(&body->block.stmts, old.items[i], p->arena);
    zan_ast_list_push(&body->block.stmts, ret, p->arena);
}

static void desugar_async_task_method(zan_parser_t *p, zan_ast_node_t *m) {
    if (!m || m->kind != AST_METHOD_DECL) return;
    zan_ast_node_t *rt = m->method_decl.return_type;
    if (!rt || rt->kind != AST_TYPE_REF) return;
    bool is_task = (rt->type_ref.name.len == 4 && memcmp(rt->type_ref.name.str, "Task", 4) == 0);
    bool is_valuetask = (rt->type_ref.name.len == 9 && memcmp(rt->type_ref.name.str, "ValueTask", 9) == 0);
    if (!is_task && !is_valuetask) return;

    /* 底层系统交互与数据协议契约 */
    if ((m->method_decl.modifiers & MOD_ASYNC) != 0 || m->method_decl.body == NULL) {
        if ((m->method_decl.modifiers & MOD_ASYNC) == 0) {
            m->method_decl.modifiers |= MOD_ASYNC;
        }
        m->method_decl.is_task_return = true;
        if (rt->type_ref.type_args.count == 1) {
            m->method_decl.return_type = rt->type_ref.type_args.items[0];
        } else if (rt->type_ref.type_args.count == 0) {
            zan_ast_node_t *v = zan_ast_new(p->arena, AST_TYPE_REF, rt->loc);
            v->type_ref.name = (zan_istr_t){"void", 4};
            v->type_ref.is_nullable = false;
            v->type_ref.is_array = false;
            m->method_decl.return_type = v;
        }
    }
}

static void parse_attr_usages(zan_parser_t *p, zan_ast_list_t *out,
                              zan_istr_t *out_lib, zan_istr_t *out_entry,
                              bool *out_variadic) {
    /* 核心系统底层抽象与内存语义契约 */
    while (parser_check(p, TK_LBRACKET)) {
        parser_advance(p);
        for (;;) {
            zan_loc_t aloc = p->current.loc;
            zan_istr_t aname = {NULL, 0};
            if (parser_check(p, TK_IDENT)) {
                aname = p->current.str_val;
                parser_advance(p);
                while (parser_check(p, TK_DOT)) {
                    parser_advance(p);
                    if (parser_check(p, TK_IDENT)) { aname = p->current.str_val; parser_advance(p); }
                    else break;
                }
            }
            zan_ast_node_t *attr = zan_ast_new(p->arena, AST_ATTRIBUTE, aloc);
            zan_ast_node_t *nameNode = zan_ast_new(p->arena, AST_IDENTIFIER, aloc);
            nameNode->ident.name = aname;
            attr->attribute.name = nameNode;
            zan_ast_list_init(&attr->attribute.args);
            bool is_dll = (aname.str && aname.len == 9 &&
                           memcmp(aname.str, "DllImport", 9) == 0);
            if (parser_match(p, TK_LPAREN)) {
                while (!parser_check(p, TK_RPAREN) && !parser_check(p, TK_EOF)) {
                    if (parser_check(p, TK_IDENT) && zan_lexer_peek(p->lex).kind == TK_EQ) {
                        zan_loc_t nloc = p->current.loc;
                        zan_istr_t argname = p->current.str_val;
                        parser_advance(p);
                        parser_advance(p);
                        zan_ast_node_t *val = parse_expression(p);
                        zan_ast_node_t *asn = zan_ast_new(p->arena, AST_ASSIGNMENT, nloc);
                        zan_ast_node_t *lhs = zan_ast_new(p->arena, AST_IDENTIFIER, nloc);
                        lhs->ident.name = argname;
                        asn->binary.op = TK_EQ;
                        asn->binary.left = lhs;
                        asn->binary.right = val;
                        zan_ast_list_push(&attr->attribute.args, asn, p->arena);
                        if (is_dll && out_entry && argname.str && argname.len == 10 &&
                            memcmp(argname.str, "EntryPoint", 10) == 0 &&
                            val->kind == AST_STRING_LITERAL) {
                            *out_entry = val->str_val;
                        }
                        if (is_dll && out_variadic && argname.str &&
                            argname.len == 8 &&
                            memcmp(argname.str, "Variadic", 8) == 0 &&
                            val->kind == AST_BOOL_LITERAL && val->bool_val) {
                            *out_variadic = true;
                        }
                    } else {
                        zan_ast_node_t *val = parse_expression(p);
                        zan_ast_list_push(&attr->attribute.args, val, p->arena);
                        if (is_dll && out_lib && !out_lib->str &&
                            val->kind == AST_STRING_LITERAL) {
                            *out_lib = val->str_val;
                        }
                    }
                    if (!parser_match(p, TK_COMMA)) break;
                }
                parser_expect(p, TK_RPAREN);
            }
            if (out) zan_ast_list_push(out, attr, p->arena);
            if (!parser_match(p, TK_COMMA)) break;
        }
        parser_expect(p, TK_RBRACKET);
    }
}

static zan_ast_node_t *parse_member_decl_inner(zan_parser_t *p,
                                               zan_istr_t dll_import_lib,
                                               zan_istr_t dll_entry_point,
                                               bool dll_variadic);

/* 内部辅助逻辑 */
static zan_ast_node_t *synth_property_accessor(zan_parser_t *p, zan_istr_t name,
                                               zan_ast_node_t *ret_type,
                                               zan_ast_node_t *value_type,
                                               zan_ast_node_t *body,
                                               uint32_t mods, zan_loc_t loc) {
    zan_ast_node_t *n = zan_ast_new(p->arena, AST_METHOD_DECL, loc);
    n->method_decl.name = name;
    n->method_decl.return_type = ret_type;
    zan_ast_list_init(&n->method_decl.params);
    zan_ast_list_init(&n->method_decl.type_params);
    if (value_type) {
        zan_ast_node_t *param = zan_ast_new(p->arena, AST_PARAM, loc);
        /* 底层系统交互与数据协议契约 */
        zan_istr_t vn = {(char *)"value", 5};
        param->param.name = vn;
        param->param.type = value_type;
        param->param.is_this = false;
        zan_ast_list_push(&n->method_decl.params, param, p->arena);
    }
    n->method_decl.body = body;
    n->method_decl.modifiers = mods;
    return n;
}

static zan_ast_node_t *parse_member_decl(zan_parser_t *p) {
    zan_ast_list_t attrs;
    zan_ast_list_init(&attrs);
    zan_istr_t dll_import_lib = {NULL, 0};
    zan_istr_t dll_entry_point = {NULL, 0};
    bool dll_variadic = false;
    parse_attr_usages(p, &attrs, &dll_import_lib, &dll_entry_point, &dll_variadic);
    zan_ast_node_t *n = parse_member_decl_inner(p, dll_import_lib, dll_entry_point,
                                                dll_variadic);
    if (n && attrs.count > 0)
        zan_ast_ensure_decl_meta(n, p->arena)->attributes = attrs;
    return n;
}

static zan_ast_node_t *parse_member_decl_inner(zan_parser_t *p,
                                               zan_istr_t dll_import_lib,
                                               zan_istr_t dll_entry_point,
                                               bool dll_variadic) {
    uint32_t mods = parse_modifiers(p);
    if (dll_import_lib.str) mods |= MOD_EXTERN;
    /* 内部辅助逻辑 */
    if (parser_check(p, TK_IDENT) && p->current.str_val.len == 5 &&
        memcmp(p->current.str_val.str, "event", 5) == 0 &&
        zan_lexer_peek(p->lex).kind == TK_IDENT) {
        parser_advance(p);
        mods |= MOD_EVENT;
    }

    /* 内部辅助逻辑 */
    if (parser_check(p, TK_IDENT) && p->current.str_val.len == 7 &&
        memcmp(p->current.str_val.str, "partial", 7) == 0) {
        zan_token_kind_t nk = zan_lexer_peek(p->lex).kind;
        if (nk == TK_CLASS || nk == TK_STRUCT || nk == TK_INTERFACE) {
            parser_advance(p);
            mods |= MOD_PARTIAL;
        }
    }

    /* 模块核心语义抽象与接口调用契约 */
    if (parser_check(p, TK_CLASS) || parser_check(p, TK_STRUCT) ||
        parser_check(p, TK_INTERFACE) || parser_check(p, TK_ENUM)) {
        return parse_type_decl(p, mods);
    }

    /* 内部辅助逻辑 */
    if (parser_check(p, TK_DELEGATE)) {
        return parse_delegate_decl(p, mods);
    }

    zan_loc_t loc = p->current.loc;

    /* destructor: ~ClassName() { } */
    if (parser_check(p, TK_TILDE)) {
        parser_advance(p);
        parser_expect(p, TK_IDENT);
        zan_istr_t name = p->previous.str_val;
        parser_expect(p, TK_LPAREN);
        parser_expect(p, TK_RPAREN);
        zan_ast_node_t *body = parse_block(p);

        zan_ast_node_t *n = zan_ast_new(p->arena, AST_DESTRUCTOR_DECL, loc);
        n->method_decl.name = name;
        n->method_decl.body = body;
        n->method_decl.modifiers = mods;
        n->method_decl.return_type = NULL;
        zan_ast_list_init(&n->method_decl.params);
        zan_ast_list_init(&n->method_decl.type_params);
        return n;
    }

    /* 底层系统交互与数据协议契约 */
    if (parser_check(p, TK_IDENT) &&
        zan_lexer_peek(p->lex).kind == TK_OPERATOR) {
        zan_istr_t kw = p->current.str_val;
        bool is_explicit;
        if (kw.len == 8 && memcmp(kw.str, "implicit", 8) == 0)
            is_explicit = false;
        else if (kw.len == 8 && memcmp(kw.str, "explicit", 8) == 0)
            is_explicit = true;
        else
            goto ordinary_member;
        parser_advance(p);
        parser_advance(p);
        zan_ast_node_t *conv_ret = parse_type_ref(p);
        zan_ast_list_t conv_params = parse_param_list(p);
        zan_ast_node_t *conv_body = NULL;
        if (parser_check(p, TK_LBRACE)) {
            conv_body = parse_block(p);
        } else {
            parser_expect(p, TK_SEMICOLON);
        }
        const char *conv_name = is_explicit ? "op_explicit" : "op_implicit";
        zan_ast_node_t *cn = zan_ast_new(p->arena, AST_METHOD_DECL, loc);
        cn->method_decl.name =
            (zan_istr_t){ (char *)conv_name, 11 }; /* 核心系统底层抽象与内存语义契约 */
        cn->method_decl.return_type = conv_ret;
        cn->method_decl.params = conv_params;
        zan_ast_list_init(&cn->method_decl.type_params);
        cn->method_decl.body = conv_body;
        cn->method_decl.modifiers = mods | MOD_STATIC;
        return cn;
    }
ordinary_member:
    /* 底层系统交互与数据协议契约 */
    ;
    zan_ast_node_t *type = parse_type_ref(p);

    /* constructor: ClassName(params) [: base(args)] { } */
    if (parser_check(p, TK_LPAREN)) {
        zan_istr_t name = type->type_ref.name;
        zan_ast_list_t params = parse_param_list(p);

        /* 底层系统交互与数据协议契约 */
        zan_ast_list_t base_args;
        zan_ast_list_init(&base_args);
        bool has_base_init = false;
        bool has_this_init = false;
        if (parser_match(p, TK_COLON)) {
            if (parser_check(p, TK_BASE) || parser_check(p, TK_THIS)) {
                bool is_base = parser_check(p, TK_BASE);
                parser_advance(p);
                if (parser_match(p, TK_LPAREN)) {
                    while (!parser_check(p, TK_RPAREN) && !parser_check(p, TK_EOF)) {
                        zan_ast_node_t *arg = parse_expression(p);
                        if (arg)
                            zan_ast_list_push(&base_args, arg, p->arena);
                        if (!parser_match(p, TK_COMMA)) break;
                    }
                    parser_expect(p, TK_RPAREN);
                }
                has_base_init = is_base;
                has_this_init = !is_base;
            }
        }

        zan_ast_node_t *body = NULL;
        if (parser_check(p, TK_LBRACE)) {
            body = parse_block(p);
        } else {
            parser_expect(p, TK_SEMICOLON);
        }

        zan_ast_node_t *n = zan_ast_new(p->arena, AST_CONSTRUCTOR_DECL, loc);
        n->method_decl.name = name;
        n->method_decl.params = params;
        n->method_decl.body = body;
        n->method_decl.modifiers = mods;
        n->method_decl.return_type = NULL;
        if (base_args.count > 0) {
            zan_ast_ensure_method_ext(n, p->arena)->base_args = base_args;
        }
        n->method_decl.has_base_init = has_base_init;
        n->method_decl.has_this_init = has_this_init;
        zan_ast_list_init(&n->method_decl.type_params);
        return n;
    }

    /* 底层系统交互与数据协议契约 */
    if (parser_check(p, TK_OPERATOR)) {
        parser_advance(p);
        char op_name[32];
        switch (p->current.kind) {
        case TK_PLUS:    snprintf(op_name, sizeof(op_name), "op_add"); break;
        case TK_MINUS:   snprintf(op_name, sizeof(op_name), "op_sub"); break;
        case TK_STAR:    snprintf(op_name, sizeof(op_name), "op_mul"); break;
        case TK_SLASH:   snprintf(op_name, sizeof(op_name), "op_div"); break;
        case TK_PERCENT: snprintf(op_name, sizeof(op_name), "op_mod"); break;
        case TK_EQ_EQ:   snprintf(op_name, sizeof(op_name), "op_eq"); break;
        case TK_BANG_EQ: snprintf(op_name, sizeof(op_name), "op_neq"); break;
        case TK_LESS:    snprintf(op_name, sizeof(op_name), "op_lt"); break;
        case TK_GREATER: snprintf(op_name, sizeof(op_name), "op_gt"); break;
        case TK_LESS_EQ: snprintf(op_name, sizeof(op_name), "op_le"); break;
        case TK_GREATER_EQ: snprintf(op_name, sizeof(op_name), "op_ge"); break;
        default:         snprintf(op_name, sizeof(op_name), "op_unknown"); break;
        }
        parser_advance(p);
        zan_ast_list_t params = parse_param_list(p);
        zan_ast_node_t *body = NULL;
        if (parser_check(p, TK_LBRACE)) {
            body = parse_block(p);
        } else {
            parser_expect(p, TK_SEMICOLON);
        }
        zan_ast_node_t *n = zan_ast_new(p->arena, AST_METHOD_DECL, loc);
        size_t op_len = strlen(op_name);
        char *op_str = zan_arena_strdup(p->arena, op_name, op_len);
        zan_istr_t iname = { op_str, (int)op_len };
        n->method_decl.name = iname;
        n->method_decl.return_type = type;
        n->method_decl.params = params;
        zan_ast_list_init(&n->method_decl.type_params);
        n->method_decl.body = body;
        n->method_decl.modifiers = mods | MOD_STATIC;
        return n;
    }

    /* 核心系统底层抽象与内存语义契约 */
    if (parser_check(p, TK_THIS)) {
        parser_advance(p);
        parser_expect(p, TK_LBRACKET);
        zan_ast_list_t idx_params;
        zan_ast_list_init(&idx_params);
        while (!parser_check(p, TK_RBRACKET) && !parser_check(p, TK_EOF)) {
            zan_ast_node_t *param = parse_parameter(p);
            zan_ast_list_push(&idx_params, param, p->arena);
            if (!parser_match(p, TK_COMMA)) break;
        }
        parser_expect(p, TK_RBRACKET);

        zan_ast_node_t *getter_body = NULL;
        zan_ast_node_t *setter_body = NULL;
        bool has_getter = false;
        bool has_setter = false;
        bool has_init = false;

        if (parser_check(p, TK_ARROW)) {
            parser_advance(p);
            zan_ast_node_t *expr = parse_expression(p);
            parser_expect(p, TK_SEMICOLON);
            getter_body = zan_ast_new(p->arena, AST_BLOCK, expr->loc);
            zan_ast_list_init(&getter_body->block.stmts);
            zan_ast_node_t *ret = zan_ast_new(p->arena, AST_RETURN_STMT, expr->loc);
            ret->ret.value = expr;
            zan_ast_list_push(&getter_body->block.stmts, ret, p->arena);
            has_getter = true;
        } else if (parser_check(p, TK_LBRACE)) {
            parser_advance(p);
            while (!parser_check(p, TK_RBRACE) && !parser_check(p, TK_EOF)) {
                if (parser_match(p, TK_GET)) {
                    has_getter = true;
                    if (parser_match(p, TK_SEMICOLON)) {
                        getter_body = NULL; /* automatic */
                    } else if (parser_check(p, TK_LBRACE)) {
                        getter_body = parse_block(p);
                    } else {
                        parser_expect(p, TK_SEMICOLON);
                    }
                } else if (parser_match(p, TK_SET)) {
                    has_setter = true;
                    if (parser_match(p, TK_SEMICOLON)) {
                        setter_body = NULL; /* automatic */
                    } else if (parser_check(p, TK_LBRACE)) {
                        setter_body = parse_block(p);
                    } else {
                        parser_expect(p, TK_SEMICOLON);
                    }
                } else if (is_init_accessor_kw(p)) {
                    parser_advance(p); /* init */
                    has_init = true;
                    if (parser_match(p, TK_SEMICOLON)) {
                        setter_body = NULL; /* automatic */
                    } else if (parser_check(p, TK_LBRACE)) {
                        setter_body = parse_block(p);
                    } else {
                        parser_expect(p, TK_SEMICOLON);
                    }
                } else {
                    parser_advance(p); /* 核心系统底层抽象与内存语义契约 */
                }
            }
            parser_expect(p, TK_RBRACE);
        } else {
            parser_expect(p, TK_LBRACE);
        }

        zan_ast_node_t *n = zan_ast_new(p->arena, AST_PROPERTY_DECL, loc);
        zan_istr_t iname = {(char *)"Item", 4}; /* 核心系统底层抽象与内存语义契约 */
        n->field_decl.name = iname;
        n->field_decl.type = type;
        n->field_decl.initializer = NULL;
        n->field_decl.modifiers = mods;
        n->field_decl.getter_body = getter_body;
        n->field_decl.setter_body = setter_body;
        n->field_decl.has_getter = has_getter;
        n->field_decl.has_setter = has_setter;
        n->field_decl.has_init = has_init;
        /* 内部辅助实现 */
        zan_ast_list_t *iparams =
            (zan_ast_list_t *)zan_arena_alloc(p->arena, sizeof(zan_ast_list_t));
        *iparams = idx_params;
        n->field_decl.indexer_params = iparams;

        /* 核心系统底层抽象与内存语义契约 */
        if (getter_body) {
            zan_ast_node_t *g = zan_ast_new(p->arena, AST_METHOD_DECL, loc);
            zan_istr_t gistr = {(char *)"op_index", 8};
            g->method_decl.name = gistr;
            g->method_decl.return_type = type;
            g->method_decl.params = idx_params;
            zan_ast_list_init(&g->method_decl.type_params);
            g->method_decl.body = getter_body;
            g->method_decl.modifiers = mods; /* instance, receiver is `this` */
            zan_ast_list_push(&p->pending_members, g, p->arena);
        }
        if (setter_body) {
            zan_ast_node_t *s = zan_ast_new(p->arena, AST_METHOD_DECL, loc);
            zan_istr_t sistr = {(char *)"op_index_set", 12};
            s->method_decl.name = sistr;
            s->method_decl.return_type = NULL; /* void */
            s->method_decl.params = idx_params;
            zan_ast_list_init(&s->method_decl.type_params);
            zan_ast_node_t *vp = zan_ast_new(p->arena, AST_PARAM, loc);
            zan_istr_t vn = {(char *)"value", 5};
            vp->param.name = vn;
            vp->param.type = type;
            vp->param.is_this = false;
            zan_ast_list_push(&s->method_decl.params, vp, p->arena);
            s->method_decl.body = setter_body;
            s->method_decl.modifiers = mods;
            zan_ast_list_push(&p->pending_members, s, p->arena);
        }
        return n;
    }

    if (!parser_check(p, TK_IDENT)) {
        zan_diag_emit(p->diag, DIAG_ERROR, p->current.loc, "expected member name");
        return parser_error_node(p);
    }
    parser_advance(p);
    zan_istr_t name = p->previous.str_val;

    /* 模块核心语义抽象与接口调用契约 */
    if (parser_check(p, TK_ARROW)) {
        parser_advance(p);
        zan_ast_node_t *expr = parse_expression(p);
        parser_expect(p, TK_SEMICOLON);

        zan_ast_node_t *body = zan_ast_new(p->arena, AST_BLOCK, expr->loc);
        zan_ast_list_init(&body->block.stmts);
        zan_ast_node_t *ret = zan_ast_new(p->arena, AST_RETURN_STMT, expr->loc);
        ret->ret.value = expr;
        zan_ast_list_push(&body->block.stmts, ret, p->arena);

        size_t gn = 4;
        char *gname = (char *)zan_arena_alloc(p->arena, gn + (size_t)name.len + 1);
        memcpy(gname, "get_", gn);
        memcpy(gname + gn, name.str, name.len);
        gname[gn + name.len] = '\0';
        zan_istr_t gistr = { gname, (uint32_t)(gn + name.len) };

        zan_ast_node_t *n = zan_ast_new(p->arena, AST_PROPERTY_DECL, loc);
        n->field_decl.name = name;
        n->field_decl.type = type;
        n->field_decl.initializer = NULL;
        n->field_decl.modifiers = mods;
        n->field_decl.getter_body = body;
        n->field_decl.setter_body = NULL;
        zan_ast_list_push(&p->pending_members,
            synth_property_accessor(p, gistr, type, NULL, body, mods, loc),
            p->arena);
        return n;
    }

    /* 底层系统交互与数据协议契约 */
    if (parser_check(p, TK_LPAREN) || parser_check(p, TK_LESS)) {
        zan_ast_list_t type_params;
        zan_ast_list_init(&type_params);
        if (parser_match(p, TK_LESS)) {
            while (!parser_check(p, TK_GREATER) && !parser_check(p, TK_EOF)) {
                if (parser_check(p, TK_IDENT)) {
                    parser_advance(p);
                    zan_ast_node_t *tp = zan_ast_new(p->arena, AST_IDENTIFIER, p->previous.loc);
                    tp->ident.name = p->previous.str_val;
                    zan_ast_list_push(&type_params, tp, p->arena);
                }
                if (!parser_match(p, TK_COMMA)) break;
            }
            parser_expect(p, TK_GREATER);
        }

        zan_ast_list_t params = parse_param_list(p);

        zan_ast_list_t wheres;
        zan_ast_list_init(&wheres);
        parse_where_clauses(p, &wheres);

        zan_ast_node_t *body = NULL;
        if (parser_check(p, TK_LBRACE)) {
            body = parse_block(p);
        } else if (parser_match(p, TK_ARROW)) {
            zan_ast_node_t *expr = parse_expression(p);
            parser_expect(p, TK_SEMICOLON);
            body = zan_ast_new(p->arena, AST_BLOCK, expr->loc);
            zan_ast_list_init(&body->block.stmts);
            zan_ast_node_t *ret = zan_ast_new(p->arena, AST_RETURN_STMT, expr->loc);
            ret->ret.value = expr;
            zan_ast_list_push(&body->block.stmts, ret, p->arena);
        } else {
            parser_expect(p, TK_SEMICOLON); /* abstract / extern */
        }

        zan_ast_node_t *n = zan_ast_new(p->arena, AST_METHOD_DECL, loc);
        n->method_decl.name = name;
        n->method_decl.return_type = type;
        n->method_decl.params = params;
        n->method_decl.type_params = type_params;
        n->method_decl.body = body;
        n->method_decl.modifiers = mods;
        n->method_decl.is_variadic = dll_variadic;
        if (dll_import_lib.str || dll_entry_point.str || wheres.count > 0) {
            zan_method_ext_t *ext = zan_ast_ensure_method_ext(n, p->arena);
            ext->extern_lib = dll_import_lib;
            if (dll_entry_point.str) {
                ext->entry_point = (zan_istr_t *)zan_arena_alloc(
                    p->arena, sizeof(zan_istr_t));
                *ext->entry_point = dll_entry_point;
            }
            ext->where_clauses = wheres;
        }
        desugar_yield_method(p, n);
        desugar_async_task_method(p, n);
        return n;
    }

    /* 模块核心语义抽象与接口调用契约 */
    if (parser_check(p, TK_LBRACE)) {
        zan_token_t peek = zan_lexer_peek(p->lex);
        if (peek.kind == TK_GET || peek.kind == TK_SET) {
            parser_advance(p);
            zan_ast_node_t *getter_body = NULL;
            zan_ast_node_t *setter_body = NULL;
            bool has_getter = false;
            bool has_setter = false;
            bool has_init = false;
            while (!parser_check(p, TK_RBRACE) && !parser_check(p, TK_EOF)) {
                if (parser_match(p, TK_GET)) {
                    has_getter = true;
                    if (parser_match(p, TK_SEMICOLON)) {
                        getter_body = NULL; /* automatic */
                    } else if (parser_check(p, TK_LBRACE)) {
                        getter_body = parse_block(p);
                    } else {
                        parser_expect(p, TK_SEMICOLON);
                    }
                } else if (parser_match(p, TK_SET)) {
                    has_setter = true;
                    if (parser_match(p, TK_SEMICOLON)) {
                        setter_body = NULL; /* automatic */
                    } else if (parser_check(p, TK_LBRACE)) {
                        setter_body = parse_block(p);
                    } else {
                        parser_expect(p, TK_SEMICOLON);
                    }
                } else if (is_init_accessor_kw(p)) {
                    parser_advance(p); /* init */
                    has_init = true;
                    if (parser_match(p, TK_SEMICOLON)) {
                        setter_body = NULL; /* automatic */
                    } else if (parser_check(p, TK_LBRACE)) {
                        setter_body = parse_block(p);
                    } else {
                        parser_expect(p, TK_SEMICOLON);
                    }
                } else {
                    parser_advance(p); /* 核心系统底层抽象与内存语义契约 */
                }
            }
            parser_expect(p, TK_RBRACE);

            /* 核心系统底层抽象与内存语义契约 */
            zan_ast_node_t *init = NULL;
            if (parser_match(p, TK_EQ)) {
                init = parse_expression(p);
                parser_expect(p, TK_SEMICOLON);
            }

            zan_ast_node_t *n = zan_ast_new(p->arena, AST_PROPERTY_DECL, loc);
            n->field_decl.name = name;
            n->field_decl.type = type;
            n->field_decl.initializer = init;
            n->field_decl.modifiers = mods;
            n->field_decl.getter_body = getter_body;
            n->field_decl.setter_body = setter_body;
            n->field_decl.has_getter = has_getter;
            n->field_decl.has_setter = has_setter;
            n->field_decl.has_init = has_init;

            /* 模块核心语义抽象与接口调用契约 */
            if (getter_body) {
                size_t gn = 4;
                char *gname = (char *)zan_arena_alloc(p->arena,
                    gn + (size_t)name.len + 1);
                memcpy(gname, "get_", gn);
                memcpy(gname + gn, name.str, name.len);
                gname[gn + name.len] = '\0';
                zan_istr_t gistr = { gname, (uint32_t)(gn + name.len) };
                zan_ast_list_push(&p->pending_members,
                    synth_property_accessor(p, gistr, type, NULL, getter_body,
                                            mods, loc),
                    p->arena);
            }
            if (setter_body) {
                size_t sn = 4;
                char *sname = (char *)zan_arena_alloc(p->arena,
                    sn + (size_t)name.len + 1);
                memcpy(sname, "set_", sn);
                memcpy(sname + sn, name.str, name.len);
                sname[sn + name.len] = '\0';
                zan_istr_t sistr = { sname, (uint32_t)(sn + name.len) };
                zan_ast_list_push(&p->pending_members,
                    synth_property_accessor(p, sistr, NULL, type, setter_body,
                                            mods, loc),
                    p->arena);
            }
            return n;
        }
    }

    /* 内部辅助逻辑 */
    zan_ast_node_t *init = NULL;
    if (parser_match(p, TK_EQ)) {
        init = parse_expression(p);
    }

    zan_ast_node_t *n = zan_ast_new(p->arena, AST_FIELD_DECL, loc);
    n->field_decl.name = name;
    n->field_decl.type = type;
    n->field_decl.initializer = init;
    n->field_decl.modifiers = mods;
    while (parser_match(p, TK_COMMA)) {
        if (!parser_check(p, TK_IDENT)) {
            zan_diag_emit(p->diag, DIAG_ERROR, p->current.loc,
                          "expected member name");
            return parser_error_node(p);
        }
        parser_advance(p);
        zan_istr_t extra = p->previous.str_val;
        zan_ast_node_t *extra_init = NULL;
        if (parser_match(p, TK_EQ)) {
            extra_init = parse_expression(p);
        }
        zan_ast_node_t *e = zan_ast_new(p->arena, AST_FIELD_DECL, loc);
        e->field_decl.name = extra;
        e->field_decl.type = type;
        e->field_decl.initializer = extra_init;
        e->field_decl.modifiers = mods;
        zan_ast_list_push(&p->pending_members, e, p->arena);
    }
    parser_expect(p, TK_SEMICOLON);
    return n;
}

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
        zan_diag_emit(p->diag, DIAG_ERROR, loc, "expected class, struct, interface, or enum");
        return parser_error_node(p);
    }

    zan_istr_t name = {0};
    if (parser_check(p, TK_IDENT)) {
        parser_advance(p);
        name = p->previous.str_val;
    } else {
        zan_diag_emit(p->diag, DIAG_ERROR, p->current.loc, "expected type name");
    }

    zan_ast_list_t type_params;
    zan_ast_list_init(&type_params);
    if (parser_match(p, TK_LESS)) {
        while (!parser_check(p, TK_GREATER) && !parser_check(p, TK_EOF)) {
            /* 内部辅助逻辑 */
            if (parser_check(p, TK_OUT) || parser_check(p, TK_IN)) {
                parser_advance(p);
            }
            if (parser_check(p, TK_IDENT)) {
                parser_advance(p);
                zan_ast_node_t *tp = zan_ast_new(p->arena, AST_IDENTIFIER, p->previous.loc);
                tp->ident.name = p->previous.str_val;
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
            /* 内部辅助逻辑 */
            for (int pi = 0; pi < p->pending_members.count; pi++) {
                zan_ast_list_push(&members, p->pending_members.items[pi],
                                  p->arena);
            }
            p->pending_members.count = 0;
            /* 内部辅助逻辑 */
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
    parser_advance(p); /* 核心系统底层抽象与内存语义契约 */
}

zan_ast_node_t *zan_parser_parse(zan_parser_t *p) {
    zan_loc_t loc = p->current.loc;
    zan_ast_node_t *unit = zan_ast_new(p->arena, AST_COMPILATION_UNIT, loc);
    zan_ast_list_init(&unit->comp_unit.usings);
    zan_ast_list_init(&unit->comp_unit.decls);
    unit->comp_unit.ns = NULL;

    while (parser_check(p, TK_USING)) {
        zan_ast_node_t *u = parse_using_decl(p);
        zan_ast_list_push(&unit->comp_unit.usings, u, p->arena);
    }

    if (parser_check(p, TK_NAMESPACE)) {
        zan_loc_t ns_loc = p->current.loc;
        parser_advance(p);
        zan_ast_node_t *ns_name = parse_qualified_name(p);

        zan_ast_node_t *ns = zan_ast_new(p->arena, AST_NAMESPACE_DECL, ns_loc);
        ns->namespace_decl.name = ns_name;
        zan_ast_list_init(&ns->namespace_decl.members);

        if (parser_match(p, TK_SEMICOLON)) {
            ns->namespace_decl.is_file_scoped = true;
        } else {
            parser_expect(p, TK_LBRACE);
            ns->namespace_decl.is_file_scoped = false;
        }

        unit->comp_unit.ns = ns;

        /* Block-scoped `namespace X { */
        if (!ns->namespace_decl.is_file_scoped) {
            for (;;) {
                if (parser_match(p, TK_RBRACE)) break;
                if (parser_check(p, TK_EOF)) {
                    zan_diag_emit(p->diag, DIAG_ERROR, p->current.loc,
                                  "unexpected end of file inside namespace block; missing '}'");
                    break;
                }
                if (!parse_top_level_decl(p, unit))
                    parser_advance(p); /* skip to recover */
            }
        }
    }

    while (!parser_check(p, TK_EOF) && !parser_check(p, TK_RBRACE)) {
        if (!parse_top_level_decl(p, unit))
            parser_advance(p); /* skip to recover */
        if (parser_check(p, TK_RBRACE)) {
            /* 顶层游离右大括号容错：报告语法错误并跳过以继续解析后续声明 */
            zan_diag_emit(p->diag, DIAG_ERROR, p->current.loc,
                          "unexpected '}' at top level");
            parser_advance(p);
        }
    }

    return unit;
}

/* 内部辅助实现 */

static zan_ast_node_t *find_delegate_decl(zan_ast_node_t *unit, zan_istr_t name) {
    for (int i = 0; i < unit->comp_unit.decls.count; i++) {
        zan_ast_node_t *d = unit->comp_unit.decls.items[i];
        if (d->kind == AST_DELEGATE_DECL &&
            d->method_decl.name.len == name.len &&
            memcmp(d->method_decl.name.str, name.str, (size_t)name.len) == 0)
            return d;
    }
    return NULL;
}

/* 底层系统交互与数据协议契约 */
/* 内部辅助逻辑 */
#define ZAN_GEN_SRC_CAP (256 * 1024)

static void zsrc_append(char *buf, int cap, int *off, const char *fmt, ...) {
    if (*off >= cap) return;
    va_list ap;
    va_start(ap, fmt);
    int w = vsnprintf(buf + *off, (size_t)(cap - *off), fmt, ap);
    va_end(ap);
    if (w < 0) return;
    *off += w;
    if (*off > cap) *off = cap;
}

static int tref_write(char *buf, int cap, zan_ast_node_t *t) {
    int n = 0;
    if (!t || t->kind != AST_TYPE_REF || cap <= 0) return 0;
    zsrc_append(buf, cap, &n, "%.*s",
                     (int)t->type_ref.name.len, t->type_ref.name.str);
    if (t->type_ref.type_args.count > 0) {
        zsrc_append(buf, cap, &n, "<");
        for (int i = 0; i < t->type_ref.type_args.count; i++) {
            if (i > 0) zsrc_append(buf, cap, &n, ", ");
            n += tref_write(buf + n, cap - n, t->type_ref.type_args.items[i]);
        }
        zsrc_append(buf, cap, &n, ">");
    }
    if (t->type_ref.is_array) zsrc_append(buf, cap, &n, "[]");
    if (t->type_ref.is_nullable) zsrc_append(buf, cap, &n, "?");
    return n;
}

static void gen_event_holder(zan_ast_node_t *unit, zan_ast_node_t *ddecl,
                             const char *dname, const char *hname,
                             zan_arena_t *arena, zan_diag_t *diag) {
    char *src = (char *)malloc(ZAN_GEN_SRC_CAP);
    if (!src) return;
    const int cap = ZAN_GEN_SRC_CAP;
    int n = 0;
    zsrc_append(src, cap, &n,
        "class %s {\n"
        "    List<%s> hs;\n"
        "    static %s op_add(%s self, %s h) {\n"
        "        %s r = self;\n"
        "        if (r == null) { r = new %s(); r.hs = new List<%s>(); }\n"
        "        r.hs.Add(h);\n"
        "        return r;\n"
        "    }\n"
        "    static %s op_sub(%s self, %s h) {\n"
        "        if (self == null) { return self; }\n"
        "        int i = self.hs.Count - 1;\n"
        "        while (i >= 0) {\n"
        "            if (self.hs[i] == h) { self.hs.RemoveAt(i); return self; }\n"
        "            i = i - 1;\n"
        "        }\n"
        "        return self;\n"
        "    }\n"
        "    int Count() { return hs.Count; }\n"
        "    void Invoke(",
        hname, dname, hname, hname, dname, hname, hname, dname,
        hname, hname, dname);
    /* 模块核心语义抽象与接口调用契约 */
    int invoke_params_at = n;
    for (int i = 0; i < ddecl->method_decl.params.count; i++) {
        zan_ast_node_t *pp = ddecl->method_decl.params.items[i];
        if (i > 0) zsrc_append(src, cap, &n, ", ");
        n += tref_write(src + n, cap - n, pp->param.type);
        zsrc_append(src, cap, &n, " a%d", i);
    }
    int invoke_params_end = n;
    zsrc_append(src, cap, &n,
        ") {\n"
        "        int i = 0;\n"
        "        while (i < hs.Count) {\n"
        "            %s d = hs[i];\n"
        "            d(", dname);
    for (int i = 0; i < ddecl->method_decl.params.count; i++) {
        zsrc_append(src, cap, &n, "%sa%d",
                         i > 0 ? ", " : "", i);
    }
    zsrc_append(src, cap, &n,
        ");\n"
        "            i = i + 1;\n"
        "        }\n"
        "    }\n");

    /* 内部辅助逻辑 */
    int params_len = invoke_params_end - invoke_params_at;
    char *params = (char *)malloc((size_t)params_len + 1);
    if (!params) { free(src); return; }
    memcpy(params, src + invoke_params_at, (size_t)params_len);
    params[params_len] = '\0';
    zsrc_append(src, cap, &n,
        "    static void op_call(%s self%s%s) {\n"
        "        if (self == null) { return; }\n"
        "        self.Invoke(",
        hname, ddecl->method_decl.params.count ? ", " : "", params);
    for (int i = 0; i < ddecl->method_decl.params.count; i++)
        zsrc_append(src, cap, &n, "%sa%d", i > 0 ? ", " : "", i);
    zsrc_append(src, cap, &n,
        ");\n"
        "    }\n"
        "}\n");
    free(params);

    if (n >= cap) {
        zan_loc_t loc = {0};
        zan_diag_emit(diag, DIAG_ERROR, loc,
                      "declaration too large to lower: generated source "
                      "exceeds %d bytes", cap);
        free(src);
        return;
    }
    char *gsrc = zan_arena_strdup(arena, src, (size_t)n);
    free(src);
    zan_lexer_t lex;
    zan_lexer_init(&lex, gsrc, (size_t)n, 0, arena, diag);
    zan_parser_t gp;
    zan_parser_init(&gp, &lex, arena, diag);
    zan_ast_node_t *gu = zan_parser_parse(&gp);
    for (int i = 0; i < gu->comp_unit.decls.count; i++)
        zan_ast_list_push(&unit->comp_unit.decls, gu->comp_unit.decls.items[i],
                          arena);
}

/* 内部辅助逻辑 */
static void gen_record_class(zan_ast_node_t *unit, zan_istr_t rname,
                             zan_ast_list_t *params, zan_arena_t *arena,
                             zan_diag_t *diag) {
    char *src = (char *)malloc(ZAN_GEN_SRC_CAP);
    if (!src) return;
    const int cap = ZAN_GEN_SRC_CAP;
    char nm[256];
    int n = 0;
    snprintf(nm, sizeof(nm), "%.*s", (int)rname.len, rname.str);
    zsrc_append(src, cap, &n, "class %s {\n", nm);
    for (int i = 0; i < params->count; i++) {
        zan_ast_node_t *pp = params->items[i];
        zsrc_append(src, cap, &n, "    public ");
        n += tref_write(src + n, cap - n, pp->param.type);
        zsrc_append(src, cap, &n, " %.*s;\n",
                         (int)pp->param.name.len, pp->param.name.str);
    }
    zsrc_append(src, cap, &n, "    public %s(", nm);
    for (int i = 0; i < params->count; i++) {
        zan_ast_node_t *pp = params->items[i];
        if (i > 0) zsrc_append(src, cap, &n, ", ");
        n += tref_write(src + n, cap - n, pp->param.type);
        zsrc_append(src, cap, &n, " %.*s",
                         (int)pp->param.name.len, pp->param.name.str);
    }
    zsrc_append(src, cap, &n, ") {\n");
    for (int i = 0; i < params->count; i++) {
        zan_ast_node_t *pp = params->items[i];
        zsrc_append(src, cap, &n,
                         "        this.%.*s = %.*s;\n",
                         (int)pp->param.name.len, pp->param.name.str,
                         (int)pp->param.name.len, pp->param.name.str);
    }
    zsrc_append(src, cap, &n, "    }\n");
    /* 模块核心语义抽象与接口调用契约 */
    zsrc_append(src, cap, &n, "    public %s __CloneWith(", nm);
    for (int i = 0; i < params->count; i++) {
        zan_ast_node_t *pp = params->items[i];
        if (i > 0) zsrc_append(src, cap, &n, ", ");
        n += tref_write(src + n, cap - n, pp->param.type);
        zsrc_append(src, cap, &n, " %.*s",
                         (int)pp->param.name.len, pp->param.name.str);
    }
    zsrc_append(src, cap, &n, ") {\n        return new %s(", nm);
    for (int i = 0; i < params->count; i++) {
        zan_ast_node_t *pp = params->items[i];
        if (i > 0) zsrc_append(src, cap, &n, ", ");
        zsrc_append(src, cap, &n, "%.*s",
                         (int)pp->param.name.len, pp->param.name.str);
    }
    zsrc_append(src, cap, &n, ");\n    }\n");
    zsrc_append(src, cap, &n,
        "    static bool op_eq(%s l, %s r) {\n"
        "        if (l == null) { return r == null; }\n"
        "        if (r == null) { return false; }\n"
        "        return true", nm, nm);
    for (int i = 0; i < params->count; i++) {
        zan_ast_node_t *pp = params->items[i];
        zsrc_append(src, cap, &n,
                         " && l.%.*s == r.%.*s",
                         (int)pp->param.name.len, pp->param.name.str,
                         (int)pp->param.name.len, pp->param.name.str);
    }
    zsrc_append(src, cap, &n,
        ";\n"
        "    }\n"
        "    static bool op_neq(%s l, %s r) { return !(l == r); }\n"
        "    public string ToString() {\n"
        "        return \"%s {\"", nm, nm, nm);
    for (int i = 0; i < params->count; i++) {
        zan_ast_node_t *pp = params->items[i];
        zan_ast_node_t *pt = pp->param.type;
        int is_str = pt && pt->kind == AST_TYPE_REF && !pt->type_ref.is_array &&
                     pt->type_ref.name.len == 6 &&
                     memcmp(pt->type_ref.name.str, "string", 6) == 0;
        zsrc_append(src, cap, &n,
                         " + \"%s %.*s = \" + %s%.*s%s",
                         i > 0 ? "," : "",
                         (int)pp->param.name.len, pp->param.name.str,
                         is_str ? "" : "Convert.ToString(",
                         (int)pp->param.name.len, pp->param.name.str,
                         is_str ? "" : ")");
    }
    zsrc_append(src, cap, &n,
        " + \" }\";\n"
        "    }\n"
        "}\n");

    if (n >= cap) {
        zan_loc_t loc = {0};
        zan_diag_emit(diag, DIAG_ERROR, loc,
                      "declaration too large to lower: generated source "
                      "exceeds %d bytes", cap);
        free(src);
        return;
    }
    char *gsrc = zan_arena_strdup(arena, src, (size_t)n);
    free(src);
    zan_lexer_t lex;
    zan_lexer_init(&lex, gsrc, (size_t)n, 0, arena, diag);
    zan_parser_t gp;
    zan_parser_init(&gp, &lex, arena, diag);
    zan_ast_node_t *gu = zan_parser_parse(&gp);
    for (int i = 0; i < gu->comp_unit.decls.count; i++)
        zan_ast_list_push(&unit->comp_unit.decls, gu->comp_unit.decls.items[i],
                          arena);
}

/* 内部辅助实现 */
void zan_parser_merge_partials(zan_ast_node_t *unit, zan_arena_t *arena,
                               zan_diag_t *diag) {
    if (!unit || unit->kind != AST_COMPILATION_UNIT) return;
    zan_ast_list_t *decls = &unit->comp_unit.decls;
    int out = 0;
    for (int di = 0; di < decls->count; di++) {
        zan_ast_node_t *d = decls->items[di];
        zan_ast_node_t *first = NULL;
        if ((d->kind == AST_CLASS_DECL || d->kind == AST_STRUCT_DECL ||
             d->kind == AST_INTERFACE_DECL) &&
            (d->type_decl.modifiers & MOD_PARTIAL)) {
            for (int fi = 0; fi < out; fi++) {
                zan_ast_node_t *e = decls->items[fi];
                if (e->kind == d->kind &&
                    (e->type_decl.modifiers & MOD_PARTIAL) &&
                    e->type_decl.name.len == d->type_decl.name.len &&
                    memcmp(e->type_decl.name.str, d->type_decl.name.str,
                           (size_t)d->type_decl.name.len) == 0) {
                    first = e;
                    break;
                }
            }
        }
        if (first) {
            for (int mi = 0; mi < d->type_decl.members.count; mi++)
                zan_ast_list_push(&first->type_decl.members,
                                  d->type_decl.members.items[mi], arena);
            for (int bi = 0; bi < d->type_decl.bases.count; bi++)
                zan_ast_list_push(&first->type_decl.bases,
                                  d->type_decl.bases.items[bi], arena);
            if (d->type_decl.where_clauses && d->type_decl.where_clauses->count > 0) {
                if (!first->type_decl.where_clauses) {
                    first->type_decl.where_clauses = (zan_ast_list_t *)zan_arena_alloc(
                        arena, sizeof(zan_ast_list_t));
                    zan_ast_list_init(first->type_decl.where_clauses);
                }
                for (int wi = 0; wi < d->type_decl.where_clauses->count; wi++)
                    zan_ast_list_push(first->type_decl.where_clauses,
                                      d->type_decl.where_clauses->items[wi], arena);
            }
        } else {
            decls->items[out++] = d;
        }
    }
    decls->count = out;
    (void)diag;
}

/* 内部辅助逻辑 */
static int hoist_nested_types(zan_ast_node_t *unit, zan_ast_node_t *type_node,
                              zan_ast_list_t *decls, zan_arena_t *arena) {
    int hoisted = 0;
    zan_ast_list_t *members = &type_node->type_decl.members;
    int m = members->count;
    int mi = 0;
    while (mi < m) {
        zan_ast_node_t *mem = members->items[mi];
        if (mem->kind == AST_DELEGATE_DECL) {
            zan_ast_list_push(decls, mem, arena);
            hoisted++;
            /* 核心系统底层抽象与内存语义契约 */
            for (int k = mi; k < m - 1; k++) members->items[k] = members->items[k + 1];
            members->count--;
            m--;
        } else if (mem->kind == AST_CLASS_DECL || mem->kind == AST_STRUCT_DECL ||
                   mem->kind == AST_INTERFACE_DECL || mem->kind == AST_ENUM_DECL) {
            hoisted += hoist_nested_types(unit, mem, decls, arena);
            /* 编译器代码生成与运行时系统底层调用契约 */
            mem->type_decl.nested_host = type_node;
            zan_ast_list_push(decls, mem, arena);
            hoisted++;
            /* 核心系统底层抽象与内存语义契约 */
            for (int k = mi; k < m - 1; k++) members->items[k] = members->items[k + 1];
            members->count--;
            m--;
        } else {
            mi++;
        }
    }
    return hoisted;
}

void zan_parser_flatten_nested_types(zan_ast_node_t *unit, zan_arena_t *arena,
                                     zan_diag_t *diag) {
    if (!unit || unit->kind != AST_COMPILATION_UNIT) return;
    zan_ast_list_t *decls = &unit->comp_unit.decls;
    int n = decls->count;
    for (int di = 0; di < n; di++) {
        zan_ast_node_t *d = decls->items[di];
        if (d->kind != AST_CLASS_DECL && d->kind != AST_STRUCT_DECL &&
            d->kind != AST_INTERFACE_DECL && d->kind != AST_ENUM_DECL) continue;
        hoist_nested_types(unit, d, decls, arena);
    }
    (void)diag;
}

void zan_parser_desugar_events(zan_ast_node_t *unit, zan_arena_t *arena,
                               zan_diag_t *diag) {
    if (!unit || unit->kind != AST_COMPILATION_UNIT) return;
    const char **generated = NULL;
    int generated_count = 0;
    int generated_cap = 0;
    int decl_count = unit->comp_unit.decls.count;
    for (int di = 0; di < decl_count; di++) {
        zan_ast_node_t *d = unit->comp_unit.decls.items[di];
        if (d->kind != AST_CLASS_DECL && d->kind != AST_STRUCT_DECL) continue;
        for (int mi = 0; mi < d->type_decl.members.count; mi++) {
            zan_ast_node_t *m = d->type_decl.members.items[mi];
            if (m->kind != AST_FIELD_DECL || !(m->field_decl.modifiers & MOD_EVENT))
                continue;
            zan_ast_node_t *tr = m->field_decl.type;
            if (!tr || tr->kind != AST_TYPE_REF || tr->type_ref.type_args.count) {
                zan_diag_emit(diag, DIAG_ERROR, m->loc,
                              "event field requires a non-generic delegate type");
                continue;
            }
            zan_ast_node_t *ddecl = find_delegate_decl(unit, tr->type_ref.name);
            if (!ddecl) {
                zan_diag_emit(diag, DIAG_ERROR, m->loc,
                              "event type '%.*s' is not a declared delegate",
                              (int)tr->type_ref.name.len, tr->type_ref.name.str);
                continue;
            }
            char dname[256], hname[280];
            snprintf(dname, sizeof(dname), "%.*s",
                     (int)tr->type_ref.name.len, tr->type_ref.name.str);
            snprintf(hname, sizeof(hname), "__Event_%s", dname);
            int already = 0;
            for (int gi = 0; gi < generated_count; gi++)
                if (strcmp(generated[gi], hname) == 0) { already = 1; break; }
            if (!already) {
                if (generated_count == generated_cap) {
                    int ncap = generated_cap ? generated_cap * 2 : 16;
                    const char **ng = (const char **)realloc(
                        (void *)generated, (size_t)ncap * sizeof(*generated));
                    if (!ng) {
                        free((void *)generated);
                        return;
                    }
                    generated = ng;
                    generated_cap = ncap;
                }
                gen_event_holder(unit, ddecl, dname, hname, arena, diag);
                generated[generated_count++] =
                    zan_arena_strdup(arena, hname, strlen(hname));
            }
            char *hn = zan_arena_strdup(arena, hname, strlen(hname));
            tr->type_ref.name = (zan_istr_t){hn, (uint32_t)strlen(hname)};
        }
    }
    free((void *)generated);
}

typedef struct {
    zan_ast_list_t *tparams;
    zan_ast_list_t *targs;
    zan_arena_t *arena;
} type_subst_ctx_t;

static zan_ast_node_t *clone_ast_subst(zan_ast_node_t *node, type_subst_ctx_t *ctx) {
    if (!node) return NULL;
    switch (node->kind) {
    case AST_TYPE_REF: {
        for (int i = 0; i < ctx->tparams->count && i < ctx->targs->count; i++) {
            zan_ast_node_t *tp = ctx->tparams->items[i];
            if (tp && tp->kind == AST_IDENTIFIER &&
                tp->ident.name.len == node->type_ref.name.len &&
                memcmp(tp->ident.name.str, node->type_ref.name.str, (size_t)node->type_ref.name.len) == 0) {
                zan_ast_node_t *ta = ctx->targs->items[i];
                zan_ast_node_t *res = zan_ast_new(ctx->arena, AST_TYPE_REF, node->loc);
                *res = *ta;
                res->type_ref.is_array = node->type_ref.is_array || ta->type_ref.is_array;
                res->type_ref.is_nullable = node->type_ref.is_nullable || ta->type_ref.is_nullable;
                return res;
            }
        }
        zan_ast_node_t *res = zan_ast_new(ctx->arena, AST_TYPE_REF, node->loc);
        *res = *node;
        zan_ast_list_init(&res->type_ref.type_args);
        for (int i = 0; i < node->type_ref.type_args.count; i++) {
            zan_ast_list_push(&res->type_ref.type_args,
                              clone_ast_subst(node->type_ref.type_args.items[i], ctx),
                              ctx->arena);
        }
        return res;
    }
    case AST_IDENTIFIER: {
        for (int i = 0; i < ctx->tparams->count && i < ctx->targs->count; i++) {
            zan_ast_node_t *tp = ctx->tparams->items[i];
            if (tp && tp->kind == AST_IDENTIFIER &&
                tp->ident.name.len == node->ident.name.len &&
                memcmp(tp->ident.name.str, node->ident.name.str, (size_t)node->ident.name.len) == 0) {
                zan_ast_node_t *ta = ctx->targs->items[i];
                zan_ast_node_t *res = zan_ast_new(ctx->arena, AST_IDENTIFIER, node->loc);
                *res = *node;
                res->ident.name = ta->type_ref.name;
                return res;
            }
        }
        zan_ast_node_t *res = zan_ast_new(ctx->arena, AST_IDENTIFIER, node->loc);
        *res = *node;
        return res;
    }
    case AST_CALL: {
        zan_ast_node_t *res = zan_ast_new(ctx->arena, AST_CALL, node->loc);
        *res = *node;
        res->call.callee = clone_ast_subst(node->call.callee, ctx);
        zan_ast_list_init(&res->call.args);
        for (int i = 0; i < node->call.args.count; i++)
            zan_ast_list_push(&res->call.args, clone_ast_subst(node->call.args.items[i], ctx), ctx->arena);
        zan_ast_list_init(&res->call.type_args);
        for (int i = 0; i < node->call.type_args.count; i++)
            zan_ast_list_push(&res->call.type_args, clone_ast_subst(node->call.type_args.items[i], ctx), ctx->arena);
        return res;
    }
    case AST_BLOCK: {
        zan_ast_node_t *res = zan_ast_new(ctx->arena, AST_BLOCK, node->loc);
        *res = *node;
        zan_ast_list_init(&res->block.stmts);
        for (int i = 0; i < node->block.stmts.count; i++)
            zan_ast_list_push(&res->block.stmts, clone_ast_subst(node->block.stmts.items[i], ctx), ctx->arena);
        return res;
    }
    case AST_RETURN_STMT: {
        zan_ast_node_t *res = zan_ast_new(ctx->arena, AST_RETURN_STMT, node->loc);
        *res = *node;
        res->ret.value = clone_ast_subst(node->ret.value, ctx);
        return res;
    }
    case AST_EXPR_STMT: {
        zan_ast_node_t *res = zan_ast_new(ctx->arena, AST_EXPR_STMT, node->loc);
        *res = *node;
        res->expr_stmt.expr = clone_ast_subst(node->expr_stmt.expr, ctx);
        return res;
    }
    case AST_MEMBER_ACCESS: {
        zan_ast_node_t *res = zan_ast_new(ctx->arena, AST_MEMBER_ACCESS, node->loc);
        *res = *node;
        res->member.object = clone_ast_subst(node->member.object, ctx);
        return res;
    }
    case AST_BINARY: {
        zan_ast_node_t *res = zan_ast_new(ctx->arena, AST_BINARY, node->loc);
        *res = *node;
        res->binary.left = clone_ast_subst(node->binary.left, ctx);
        res->binary.right = clone_ast_subst(node->binary.right, ctx);
        return res;
    }
    case AST_UNARY:
    case AST_POSTFIX_UNARY: {
        zan_ast_node_t *res = zan_ast_new(ctx->arena, node->kind, node->loc);
        *res = *node;
        res->unary.operand = clone_ast_subst(node->unary.operand, ctx);
        return res;
    }
    case AST_ASSIGNMENT: {
        zan_ast_node_t *res = zan_ast_new(ctx->arena, AST_ASSIGNMENT, node->loc);
        *res = *node;
        res->binary.left = clone_ast_subst(node->binary.left, ctx);
        res->binary.right = clone_ast_subst(node->binary.right, ctx);
        return res;
    }
    case AST_IF_STMT: {
        zan_ast_node_t *res = zan_ast_new(ctx->arena, AST_IF_STMT, node->loc);
        *res = *node;
        res->if_stmt.cond = clone_ast_subst(node->if_stmt.cond, ctx);
        res->if_stmt.then_body = clone_ast_subst(node->if_stmt.then_body, ctx);
        res->if_stmt.else_body = clone_ast_subst(node->if_stmt.else_body, ctx);
        return res;
    }
    case AST_WHILE_STMT:
    case AST_DO_WHILE_STMT: {
        zan_ast_node_t *res = zan_ast_new(ctx->arena, node->kind, node->loc);
        *res = *node;
        res->while_stmt.cond = clone_ast_subst(node->while_stmt.cond, ctx);
        res->while_stmt.body = clone_ast_subst(node->while_stmt.body, ctx);
        return res;
    }
    case AST_FOR_STMT: {
        zan_ast_node_t *res = zan_ast_new(ctx->arena, AST_FOR_STMT, node->loc);
        *res = *node;
        res->for_stmt.init = clone_ast_subst(node->for_stmt.init, ctx);
        res->for_stmt.cond = clone_ast_subst(node->for_stmt.cond, ctx);
        res->for_stmt.step = clone_ast_subst(node->for_stmt.step, ctx);
        res->for_stmt.body = clone_ast_subst(node->for_stmt.body, ctx);
        return res;
    }
    case AST_FOREACH_STMT: {
        zan_ast_node_t *res = zan_ast_new(ctx->arena, AST_FOREACH_STMT, node->loc);
        *res = *node;
        res->foreach_stmt.var_type = clone_ast_subst(node->foreach_stmt.var_type, ctx);
        res->foreach_stmt.collection = clone_ast_subst(node->foreach_stmt.collection, ctx);
        res->foreach_stmt.body = clone_ast_subst(node->foreach_stmt.body, ctx);
        return res;
    }
    case AST_VAR_DECL: {
        zan_ast_node_t *res = zan_ast_new(ctx->arena, AST_VAR_DECL, node->loc);
        *res = *node;
        res->var_decl.type = clone_ast_subst(node->var_decl.type, ctx);
        res->var_decl.initializer = clone_ast_subst(node->var_decl.initializer, ctx);
        return res;
    }
    case AST_NEW_EXPR: {
        zan_ast_node_t *res = zan_ast_new(ctx->arena, AST_NEW_EXPR, node->loc);
        *res = *node;
        res->new_expr.type = clone_ast_subst(node->new_expr.type, ctx);
        zan_ast_list_init(&res->new_expr.args);
        for (int i = 0; i < node->new_expr.args.count; i++)
            zan_ast_list_push(&res->new_expr.args, clone_ast_subst(node->new_expr.args.items[i], ctx), ctx->arena);
        zan_ast_list_init(&res->new_expr.arg_inits);
        for (int i = 0; i < node->new_expr.arg_inits.count; i++)
            zan_ast_list_push(&res->new_expr.arg_inits, clone_ast_subst(node->new_expr.arg_inits.items[i], ctx), ctx->arena);
        return res;
    }
    case AST_CAST_EXPR:
    case AST_TYPEOF_EXPR:
    case AST_SIZEOF_EXPR: {
        zan_ast_node_t *res = zan_ast_new(ctx->arena, node->kind, node->loc);
        *res = *node;
        res->cast.type = clone_ast_subst(node->cast.type, ctx);
        res->cast.expr = clone_ast_subst(node->cast.expr, ctx);
        return res;
    }
    case AST_IS_EXPR:
    case AST_AS_EXPR: {
        zan_ast_node_t *res = zan_ast_new(ctx->arena, node->kind, node->loc);
        *res = *node;
        res->type_test.expr = clone_ast_subst(node->type_test.expr, ctx);
        res->type_test.type = clone_ast_subst(node->type_test.type, ctx);
        return res;
    }
    case AST_AWAIT_EXPR: {
        zan_ast_node_t *res = zan_ast_new(ctx->arena, AST_AWAIT_EXPR, node->loc);
        *res = *node;
        res->await_expr.expr = clone_ast_subst(node->await_expr.expr, ctx);
        return res;
    }
    case AST_PARAM: {
        zan_ast_node_t *res = zan_ast_new(ctx->arena, AST_PARAM, node->loc);
        *res = *node;
        res->param.type = clone_ast_subst(node->param.type, ctx);
        res->param.default_val = clone_ast_subst(node->param.default_val, ctx);
        return res;
    }
    case AST_INDEX: {
        zan_ast_node_t *res = zan_ast_new(ctx->arena, AST_INDEX, node->loc);
        *res = *node;
        res->index.object = clone_ast_subst(node->index.object, ctx);
        res->index.index = clone_ast_subst(node->index.index, ctx);
        zan_ast_list_init(&res->index.extra);
        for (int i = 0; i < node->index.extra.count; i++)
            zan_ast_list_push(&res->index.extra, clone_ast_subst(node->index.extra.items[i], ctx), ctx->arena);
        return res;
    }
    case AST_CONDITIONAL: {
        zan_ast_node_t *res = zan_ast_new(ctx->arena, AST_CONDITIONAL, node->loc);
        *res = *node;
        res->if_stmt.cond = clone_ast_subst(node->if_stmt.cond, ctx);
        res->if_stmt.then_body = clone_ast_subst(node->if_stmt.then_body, ctx);
        res->if_stmt.else_body = clone_ast_subst(node->if_stmt.else_body, ctx);
        return res;
    }
    case AST_THROW_STMT: {
        zan_ast_node_t *res = zan_ast_new(ctx->arena, AST_THROW_STMT, node->loc);
        *res = *node;
        res->throw_stmt.value = clone_ast_subst(node->throw_stmt.value, ctx);
        return res;
    }
    case AST_TRY_STMT: {
        zan_ast_node_t *res = zan_ast_new(ctx->arena, AST_TRY_STMT, node->loc);
        *res = *node;
        res->try_stmt.try_body = clone_ast_subst(node->try_stmt.try_body, ctx);
        res->try_stmt.finally_body = clone_ast_subst(node->try_stmt.finally_body, ctx);
        zan_ast_list_init(&res->try_stmt.catches);
        for (int i = 0; i < node->try_stmt.catches.count; i++)
            zan_ast_list_push(&res->try_stmt.catches, clone_ast_subst(node->try_stmt.catches.items[i], ctx), ctx->arena);
        return res;
    }
    case AST_CATCH_CLAUSE: {
        zan_ast_node_t *res = zan_ast_new(ctx->arena, AST_CATCH_CLAUSE, node->loc);
        *res = *node;
        res->catch_clause.type = clone_ast_subst(node->catch_clause.type, ctx);
        res->catch_clause.body = clone_ast_subst(node->catch_clause.body, ctx);
        return res;
    }
    default: {
        zan_ast_node_t *res = zan_ast_new(ctx->arena, node->kind, node->loc);
        *res = *node;
        return res;
    }
    }
}

static zan_ast_node_t *find_type_decl_by_name(zan_ast_node_t *unit, zan_istr_t name) {
    if (!unit || unit->kind != AST_COMPILATION_UNIT) return NULL;
    for (int i = 0; i < unit->comp_unit.decls.count; i++) {
        zan_ast_node_t *d = unit->comp_unit.decls.items[i];
        if (d->kind != AST_CLASS_DECL && d->kind != AST_STRUCT_DECL) continue;
        if (d->type_decl.name.len == name.len &&
            memcmp(d->type_decl.name.str, name.str, (size_t)name.len) == 0)
            return d;
        zan_istr_t orig = zan_ast_orig_name(d);
        if (orig.len == name.len &&
            memcmp(orig.str, name.str, (size_t)name.len) == 0)
            return d;
        if (d->type_decl.name.len > name.len + 1 &&
            d->type_decl.name.str[d->type_decl.name.len - name.len - 1] == '_' &&
            memcmp(d->type_decl.name.str + (d->type_decl.name.len - name.len),
                   name.str, (size_t)name.len) == 0)
            return d;
    }
    return NULL;
}

static bool class_has_method_name_and_param_count(zan_ast_node_t *cls, zan_istr_t name, int argc) {
    for (int i = 0; i < cls->type_decl.members.count; i++) {
        zan_ast_node_t *m = cls->type_decl.members.items[i];
        if (m->kind == AST_METHOD_DECL &&
            m->method_decl.name.len == name.len &&
            memcmp(m->method_decl.name.str, name.str, (size_t)name.len) == 0 &&
            m->method_decl.params.count == argc)
            return true;
    }
    return false;
}

static void specialize_type_from_base(zan_ast_node_t *derived, zan_ast_node_t *base_ref,
                                      zan_ast_node_t *unit, zan_arena_t *arena, int depth) {
    if (!derived || !base_ref || !unit || depth > 8) return;
    if (base_ref->kind != AST_TYPE_REF || base_ref->type_ref.type_args.count == 0) return;

    zan_ast_node_t *base_decl = find_type_decl_by_name(unit, base_ref->type_ref.name);
    if (!base_decl || (base_decl->kind != AST_CLASS_DECL && base_decl->kind != AST_STRUCT_DECL))
        return;
    if (base_decl->type_decl.type_params.count != base_ref->type_ref.type_args.count)
        return;

    type_subst_ctx_t ctx;
    ctx.tparams = &base_decl->type_decl.type_params;
    ctx.targs = &base_ref->type_ref.type_args;
    ctx.arena = arena;

    for (int i = 0; i < base_decl->type_decl.members.count; i++) {
        zan_ast_node_t *m = base_decl->type_decl.members.items[i];
        if (m->kind != AST_METHOD_DECL || !m->method_decl.body) continue;
        if (m->method_decl.modifiers & MOD_STATIC) continue;
        if (m->method_decl.name.len == base_decl->type_decl.name.len &&
            memcmp(m->method_decl.name.str, base_decl->type_decl.name.str, (size_t)m->method_decl.name.len) == 0)
            continue;
        if (class_has_method_name_and_param_count(derived, m->method_decl.name, m->method_decl.params.count))
            continue;

        zan_ast_node_t *spec = zan_ast_new(arena, AST_METHOD_DECL, m->loc);
        *spec = *m;
        spec->method_decl.return_type = clone_ast_subst(m->method_decl.return_type, &ctx);
        zan_ast_list_init(&spec->method_decl.params);
        for (int p = 0; p < m->method_decl.params.count; p++) {
            zan_ast_list_push(&spec->method_decl.params,
                              clone_ast_subst(m->method_decl.params.items[p], &ctx),
                              arena);
        }
        spec->method_decl.body = clone_ast_subst(m->method_decl.body, &ctx);
        zan_ast_list_push(&derived->type_decl.members, spec, arena);
    }

    for (int b = 0; b < base_decl->type_decl.bases.count; b++) {
        specialize_type_from_base(derived, base_decl->type_decl.bases.items[b], unit, arena, depth + 1);
    }
}

void zan_parser_specialize_generic_bases(zan_ast_node_t *unit, zan_arena_t *arena,
                                         zan_diag_t *diag) {
    if (!unit || unit->kind != AST_COMPILATION_UNIT) return;
    int decl_count = unit->comp_unit.decls.count;
    for (int i = 0; i < decl_count; i++) {
        zan_ast_node_t *d = unit->comp_unit.decls.items[i];
        if (d->kind != AST_CLASS_DECL && d->kind != AST_STRUCT_DECL) continue;
        for (int b = 0; b < d->type_decl.bases.count; b++) {
            specialize_type_from_base(d, d->type_decl.bases.items[b], unit, arena, 0);
        }
    }
    (void)diag;
}
