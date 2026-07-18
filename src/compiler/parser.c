/* parser.c -- Recursive descent parser for the Zan language.
 *
 * Parses the subset needed for M1:
 *   - using / namespace
 *   - class / struct declarations
 *   - method / constructor / destructor declarations
 *   - field declarations
 *   - variable declarations (var, let, typed)
 *   - control flow (if, while, for, foreach, do-while, return, break, continue)
 *   - expressions (binary, unary, call, member access, indexing, new, cast, literals)
 *   - basic type references
 */

#include "parser.h"
#include "arena.h"
#include "diag.h"
#include <string.h>
#include <stdio.h>

/* ---- helpers ---- */

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

/* Consume the '>' that closes a generic argument list.
 *
 * The lexer greedily forms '>>' / '>>=' tokens, but inside nested generics
 * (e.g. `Box<Box<int>>`) each level needs its own closing '>'. When the
 * current token is such a compound token, split off a single '>' and rewrite
 * the current token to hold the remainder so the enclosing level can consume
 * it in turn, without advancing the underlying lexer. */
static void parser_expect_gt(zan_parser_t *p) {
    switch (p->current.kind) {
    case TK_GREATER:
        parser_advance(p);
        return;
    case TK_GREATER_GREATER:
        p->current.kind = TK_GREATER;
        p->current.loc.col += 1;
        return;
    case TK_GREATER_GREATER_EQ:
        p->current.kind = TK_GREATER_EQ;
        p->current.loc.col += 1;
        return;
    default:
        zan_diag_emit(p->diag, DIAG_ERROR, p->current.loc,
                      "expected '%s', got '%s'",
                      zan_token_kind_name(TK_GREATER),
                      zan_token_kind_name(p->current.kind));
    }
}

/* forward declarations */
static zan_ast_node_t *parse_expression(zan_parser_t *p);
static zan_ast_node_t *parse_statement(zan_parser_t *p);
static zan_ast_node_t *parse_block(zan_parser_t *p);
static zan_ast_node_t *parse_type_ref(zan_parser_t *p);
static zan_ast_node_t *parse_type_decl(zan_parser_t *p, uint32_t modifiers);
static void gen_record_class(zan_ast_node_t *unit, zan_istr_t rname,
                             zan_ast_list_t *params, zan_arena_t *arena,
                             zan_diag_t *diag);
static zan_ast_node_t *parse_unary(zan_parser_t *p);

/* ---- qualified name: a.b.c ---- */

static zan_ast_node_t *parse_qualified_name(zan_parser_t *p) {
    zan_loc_t loc = p->current.loc;
    zan_ast_node_t *node = zan_ast_new(p->arena, AST_QUALIFIED_NAME, loc);
    zan_ast_list_init(&node->qualified_name.parts);

    /* first identifier */
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

/* ---- type references ---- */

static zan_ast_node_t *parse_type_ref(zan_parser_t *p) {
    zan_loc_t loc = p->current.loc;

    /* handle built-in type keywords */
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
    } else {
        zan_diag_emit(p->diag, DIAG_ERROR, loc, "expected type");
        return parser_error_node(p);
    }

    zan_ast_node_t *type_node = zan_ast_new(p->arena, AST_TYPE_REF, loc);
    type_node->type_ref.name = name;
    type_node->type_ref.is_nullable = false;
    type_node->type_ref.is_array = false;
    zan_ast_list_init(&type_node->type_ref.type_args);

    /* generic type args: <T, U> */
    if (parser_check(p, TK_LESS)) {
        parser_advance(p);
        while (!parser_check(p, TK_GREATER) && !parser_check(p, TK_EOF)) {
            zan_ast_node_t *arg = parse_type_ref(p);
            zan_ast_list_push(&type_node->type_ref.type_args, arg, p->arena);
            if (!parser_match(p, TK_COMMA)) break;
        }
        parser_expect_gt(p);
    }

    /* array: T[] */
    if (parser_check(p, TK_LBRACKET) &&
        p->lex->pos < p->lex->source_len &&
        p->lex->source[p->lex->pos] == ']') {
        /* peek: next char after [ is ] */
        parser_advance(p); /* [ */
        parser_advance(p); /* ] */
        type_node->type_ref.is_array = true;
    }

    /* nullable: T? */
    if (parser_match(p, TK_QUESTION)) {
        type_node->type_ref.is_nullable = true;
    }

    return type_node;
}

/* ---- modifiers ---- */

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
        case TK_EXTERN:    parser_advance(p); mods |= MOD_EXTERN;    break;
        case TK_ASYNC:     parser_advance(p); mods |= MOD_ASYNC;     break;
        case TK_UNSAFE:    parser_advance(p); mods |= MOD_UNSAFE;    break;
        case TK_WEAK:      parser_advance(p); mods |= MOD_WEAK;      break;
        default: return mods;
        }
    }
}

/* ---- expressions (Pratt-style precedence climbing) ---- */

/* Lookahead used to disambiguate a parenthesized group from a lambda
 * parameter list. With p->current sitting on '(', returns true when the
 * matching ')' is immediately followed by '=>'. Token-level only: it saves
 * and restores the lexer + current/previous tokens and allocates no AST,
 * so it emits no diagnostics. */
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

/* A single lambda parameter: either an untyped bare identifier (when the
 * identifier is directly followed by ',' or ')') or a typed `Type name`. */
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

/* Parse a parenthesized lambda: `( params ) => body`, with p->current on '('.
 * The body is a block when it starts with '{', otherwise a single expression. */
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

static zan_ast_node_t *parse_primary(zan_parser_t *p) {
    zan_loc_t loc = p->current.loc;

    switch (p->current.kind) {
    case TK_INT_LIT: {
        parser_advance(p);
        zan_ast_node_t *n = zan_ast_new(p->arena, AST_INT_LITERAL, loc);
        n->int_val = p->previous.int_val;
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
        /* $"text {expr} text {expr} text" */
        zan_ast_node_t *n = zan_ast_new(p->arena, AST_STRING_INTERP, loc);
        zan_ast_list_init(&n->string_interp.parts);
        parser_advance(p); /* consume INTERP_START */
        /* add leading text segment */
        zan_ast_node_t *seg = zan_ast_new(p->arena, AST_STRING_LITERAL, loc);
        seg->str_val = p->previous.str_val;
        zan_ast_list_push(&n->string_interp.parts, seg, p->arena);
        /* parse expr + mid/end pairs */
        while (true) {
            zan_ast_node_t *expr = parse_expression(p);
            zan_ast_list_push(&n->string_interp.parts, expr, p->arena);
            if (p->current.kind == TK_INTERP_MID) {
                parser_advance(p);
                zan_ast_node_t *mid = zan_ast_new(p->arena, AST_STRING_LITERAL, p->previous.loc);
                mid->str_val = p->previous.str_val;
                zan_ast_list_push(&n->string_interp.parts, mid, p->arena);
                /* loop for next expression */
            } else if (p->current.kind == TK_INTERP_END) {
                parser_advance(p);
                zan_ast_node_t *end = zan_ast_new(p->arena, AST_STRING_LITERAL, p->previous.loc);
                end->str_val = p->previous.str_val;
                zan_ast_list_push(&n->string_interp.parts, end, p->arena);
                break;
            } else {
                /* no more interpolation — string had no closing text */
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
        /* contextual keyword: nameof(expr) — folds to a string literal */
        if (p->current.str_val.len == 6 &&
            memcmp(p->current.str_val.str, "nameof", 6) == 0 &&
            zan_lexer_peek(p->lex).kind == TK_LPAREN) {
            parser_advance(p); /* nameof */
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
        /* check for lambda: x => expr */
        zan_token_t peek = zan_lexer_peek(p->lex);
        if (peek.kind == TK_ARROW) {
            parser_advance(p); /* ident */
            zan_istr_t param_name = p->previous.str_val;
            parser_advance(p); /* => */
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
        /* Lambda with a parenthesized parameter list: () =>, (a, b) =>,
         * (int x) => ... — detected by a matching ')' followed by '=>'. Must
         * be checked before the cast rule so typed params like (int x) work. */
        if (paren_is_lambda(p)) {
            return parse_lambda_paren(p, loc);
        }
        /* C-style cast: (PrimitiveType) unary — primitive keywords cannot
         * begin a parenthesized expression, so this is unambiguous. */
        switch (zan_lexer_peek(p->lex).kind) {
        case TK_INT: case TK_LONG: case TK_SHORT: case TK_BYTE:
        case TK_UINT: case TK_ULONG: case TK_USHORT: case TK_SBYTE:
        case TK_DOUBLE: case TK_FLOAT: case TK_DECIMAL:
        case TK_BOOL: case TK_CHAR: {
            parser_advance(p); /* ( */
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
        /* could be grouped expression or lambda: (params) => expr */
        /* save lexer state to try lambda parse */
        parser_advance(p); /* ( */
        zan_ast_node_t *expr = parse_expression(p);
        parser_expect(p, TK_RPAREN);
        /* check for lambda arrow after ) */
        if (parser_check(p, TK_ARROW)) {
            parser_advance(p); /* => */
            zan_ast_node_t *body = parse_expression(p);
            zan_ast_node_t *n = zan_ast_new(p->arena, AST_LAMBDA, loc);
            zan_ast_list_init(&n->lambda.params);
            /* treat the parenthesized expr as single param */
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
    case TK_NEW: {
        parser_advance(p); /* new */
        zan_ast_node_t *type = parse_type_ref(p);
        zan_ast_node_t *n = zan_ast_new(p->arena, AST_NEW_EXPR, loc);
        n->new_expr.type = type;
        zan_ast_list_init(&n->new_expr.args);
        n->new_expr.is_array = false;

        /* array creation: new Type[size] */
        if (parser_check(p, TK_LBRACKET) && !type->type_ref.is_array) {
            parser_advance(p); /* [ */
            zan_ast_node_t *size = parse_expression(p);
            parser_expect(p, TK_RBRACKET);
            zan_ast_list_push(&n->new_expr.args, size, p->arena);
            n->new_expr.is_array = true;
        } else if (parser_match(p, TK_LPAREN)) {
            while (!parser_check(p, TK_RPAREN) && !parser_check(p, TK_EOF)) {
                zan_ast_node_t *arg = parse_expression(p);
                zan_ast_list_push(&n->new_expr.args, arg, p->arena);
                if (!parser_match(p, TK_COMMA)) break;
            }
            parser_expect(p, TK_RPAREN);
        }
        /* collection/object initializer: { items } */
        if (parser_match(p, TK_LBRACE)) {
            while (!parser_check(p, TK_RBRACE) && !parser_check(p, TK_EOF)) {
                zan_ast_node_t *item = parse_expression(p);
                zan_ast_list_push(&n->new_expr.args, item, p->arena);
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

/* A call argument: an expression, or a by-reference `ref x` / `out x` /
 * `out T x` argument (the latter declares a fresh local at the call site). */
static zan_ast_node_t *parse_call_arg(zan_parser_t *p) {
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
        /* `out Foo x`: the expression parsed was actually the type name */
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

/* Disambiguate `name<...>(` (an explicit generic call) from a `<` comparison.
 * Speculatively scan the tokens after `<`, allowing only type-argument shapes
 * (identifiers, builtin type keywords, nested `<>`, `,` `.` `[` `]` `?`). A
 * generic call is confirmed only when the matching `>` is immediately followed
 * by `(`. Lexer state is restored so the caller re-parses from the `<`. */
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
        } else if (k == TK_GREATER || k == TK_GREATER_GREATER) {
            depth -= (k == TK_GREATER_GREATER) ? 2 : 1;
            if (depth <= 0) {
                ok = (zan_lexer_peek(p->lex).kind == TK_LPAREN);
                break;
            }
        } else if (k == TK_IDENT || k == TK_COMMA || k == TK_DOT ||
                   k == TK_LBRACKET || k == TK_RBRACKET || k == TK_QUESTION ||
                   is_type_kw(k)) {
            /* still plausibly a type-argument list */
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

/* postfix: call, member access, index, ++, --, object initializer */
static zan_ast_node_t *parse_postfix(zan_parser_t *p) {
    zan_ast_node_t *expr = parse_primary(p);

    /* object initializer: Identifier { field = val, ... }
     * Only when preceding expression is a simple identifier or member access */
    if (parser_check(p, TK_LBRACE) &&
        (expr->kind == AST_IDENTIFIER || expr->kind == AST_MEMBER_ACCESS)) {
        zan_loc_t loc = p->current.loc;
        parser_advance(p); /* { */

        /* re-interpret: the identifier becomes the type in a new-expression */
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

        /* parse field = value pairs as assignment expressions */
        while (!parser_check(p, TK_RBRACE) && !parser_check(p, TK_EOF)) {
            zan_ast_node_t *field_init = parse_expression(p);
            zan_ast_list_push(&n->new_expr.args, field_init, p->arena);
            if (!parser_match(p, TK_COMMA)) break;
        }
        parser_expect(p, TK_RBRACE);
        expr = n;
    }

    for (;;) {
        zan_loc_t loc = p->current.loc;

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
            expr = n;
        } else if (parser_match(p, TK_LPAREN)) {
            /* function call */
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
            /* explicit generic call: callee<T, ...>(args) */
            zan_ast_node_t *n = zan_ast_new(p->arena, AST_CALL, loc);
            n->call.callee = expr;
            zan_ast_list_init(&n->call.args);
            zan_ast_list_init(&n->call.type_args);
            parser_advance(p); /* < */
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
        } else if (parser_match(p, TK_LBRACKET)) {
            /* indexing */
            zan_ast_node_t *idx = parse_expression(p);
            parser_expect(p, TK_RBRACKET);
            zan_ast_node_t *n = zan_ast_new(p->arena, AST_INDEX, loc);
            n->index.object = expr;
            n->index.index = idx;
            expr = n;
        } else if (parser_match(p, TK_PLUS_PLUS) || p->previous.kind == TK_MINUS_MINUS) {
            /* defer: check if we just matched */
            if (p->previous.kind == TK_PLUS_PLUS || p->previous.kind == TK_MINUS_MINUS) {
                zan_ast_node_t *n = zan_ast_new(p->arena, AST_POSTFIX_UNARY, loc);
                n->unary.op = p->previous.kind;
                n->unary.operand = expr;
                expr = n;
            }
            break;
        } else if (parser_check(p, TK_MINUS_MINUS)) {
            parser_advance(p);
            zan_ast_node_t *n = zan_ast_new(p->arena, AST_POSTFIX_UNARY, loc);
            n->unary.op = TK_MINUS_MINUS;
            n->unary.operand = expr;
            expr = n;
        } else {
            break;
        }
    }

    return expr;
}

/* unary: !x, -x, ~x, ++x, --x */
static zan_ast_node_t *parse_unary(zan_parser_t *p) {
    zan_loc_t loc = p->current.loc;

    /* await expression: await expr */
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

/* binary precedence levels */
static int get_precedence(zan_token_kind_t kind) {
    switch (kind) {
    case TK_STAR: case TK_SLASH: case TK_PERCENT: return 12;
    case TK_PLUS: case TK_MINUS: return 11;
    case TK_LESS_LESS: case TK_GREATER_GREATER: return 10;
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

static zan_ast_node_t *parse_binary(zan_parser_t *p, int min_prec) {
    zan_ast_node_t *left = parse_unary(p);

    while (is_binary_op(p->current.kind)) {
        int prec = get_precedence(p->current.kind);
        if (prec < min_prec) break;

        zan_token_kind_t op = p->current.kind;
        zan_loc_t loc = p->current.loc;
        parser_advance(p);

        /* handle `is` and `as` with type argument */
        if (op == TK_IS || op == TK_AS) {
            zan_ast_node_t *type = parse_type_ref(p);
            zan_ast_node_t *n = zan_ast_new(p->arena,
                op == TK_IS ? AST_IS_EXPR : AST_AS_EXPR, loc);
            n->type_test.expr = left;
            n->type_test.type = type;
            left = n;
            continue;
        }

        zan_ast_node_t *right = parse_binary(p, prec + 1);
        zan_ast_node_t *n = zan_ast_new(p->arena, AST_BINARY, loc);
        n->binary.op = op;
        n->binary.left = left;
        n->binary.right = right;
        left = n;
    }

    return left;
}

/* conditional: expr ? expr : expr */
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

/* assignment */
static bool is_assign_op(zan_token_kind_t kind) {
    switch (kind) {
    case TK_EQ: case TK_PLUS_EQ: case TK_MINUS_EQ: case TK_STAR_EQ:
    case TK_SLASH_EQ: case TK_PERCENT_EQ: case TK_AMP_EQ: case TK_PIPE_EQ:
    case TK_CARET_EQ: case TK_LESS_LESS_EQ: case TK_GREATER_GREATER_EQ:
        return true;
    default:
        return false;
    }
}

static zan_ast_node_t *parse_expression(zan_parser_t *p) {
    zan_ast_node_t *expr = parse_conditional(p);

    if (is_assign_op(p->current.kind)) {
        zan_token_kind_t op = p->current.kind;
        zan_loc_t loc = p->current.loc;
        parser_advance(p);
        zan_ast_node_t *right = parse_expression(p);

        /* Desugar compound assignment `lhs OP= rhs` into `lhs = lhs OP rhs`.
         * irgen has no dedicated compound-assign path, so lowering here fixes
         * +=/-=/... for scalars and strings and, because `+` already dispatches
         * to a class's op_add, lets operator-overloaded types drive `+=`
         * (e.g. `btn.Click += handler`). */
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
        n->binary.left = expr;
        n->binary.right = right;
        return n;
    }

    return expr;
}

/* ---- statements ---- */

static zan_ast_node_t *parse_block(zan_parser_t *p) {
    zan_loc_t loc = p->current.loc;
    parser_expect(p, TK_LBRACE);

    zan_ast_node_t *block = zan_ast_new(p->arena, AST_BLOCK, loc);
    zan_ast_list_init(&block->block.stmts);

    while (!parser_check(p, TK_RBRACE) && !parser_check(p, TK_EOF)) {
        uint32_t before = p->current.loc.offset;
        zan_ast_node_t *stmt = parse_statement(p);
        if (stmt) {
            zan_ast_list_push(&block->block.stmts, stmt, p->arena);
        }
        /* guarantee forward progress: if a malformed statement was not
         * consumed, skip a token so error recovery can't spin forever
         * (previously this looped, allocating until OOM). */
        if (p->current.loc.offset == before && !parser_check(p, TK_EOF)) {
            parser_advance(p);
        }
    }

    parser_expect(p, TK_RBRACE);
    return block;
}

/* check if current position looks like a variable declaration:
 * type ident [= ...]
 * var ident [= ...]
 * let ident [= ...]
 * const type ident [= ...]
 */
static bool looks_like_var_decl(zan_parser_t *p) {
    if (parser_check(p, TK_VAR) || parser_check(p, TK_LET) || parser_check(p, TK_CONST)) {
        return true;
    }
    /* type keyword followed by identifier */
    switch (p->current.kind) {
    case TK_INT: case TK_LONG: case TK_SHORT: case TK_BYTE:
    case TK_UINT: case TK_ULONG: case TK_USHORT: case TK_SBYTE:
    case TK_FLOAT: case TK_DOUBLE: case TK_DECIMAL: case TK_BOOL: case TK_CHAR:
    case TK_STRING: case TK_VOID: case TK_OBJECT: case TK_NINT:
        return true;
    case TK_IDENT: {
        /* could be type or expression; peek for identifier after */
        zan_token_t peek = zan_lexer_peek(p->lex);
        /* save lexer state is handled by peek */
        /* heuristic: ident followed by ident or `<` is likely a type declaration */
        if (peek.kind == TK_IDENT || peek.kind == TK_LESS) {
            return true;
        }
        /* `Ident[] name` (array-typed decl) vs `arr[i]` (index expression):
         * only a declaration when the brackets are empty. Scan the raw source
         * after the current identifier for a `[` immediately followed by `]`. */
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

    /* if type was 'var' or 'let', type is NULL (inferred) */
    if (type && type->kind == AST_TYPE_REF &&
        type->type_ref.name.len == 3 &&
        memcmp(type->type_ref.name.str, "var", 3) == 0) {
        type = NULL;
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

    parser_expect(p, TK_SEMICOLON);

    zan_ast_node_t *decl = zan_ast_new(p->arena, AST_VAR_DECL, loc);
    decl->var_decl.name = name;
    decl->var_decl.type = type;
    decl->var_decl.initializer = init;
    decl->var_decl.is_const = is_const;
    decl->var_decl.is_let = is_let;
    return decl;
}

static zan_ast_node_t *parse_if_stmt(zan_parser_t *p) {
    zan_loc_t loc = p->current.loc;
    parser_expect(p, TK_IF);
    parser_expect(p, TK_LPAREN);
    zan_ast_node_t *cond = parse_expression(p);
    parser_expect(p, TK_RPAREN);
    zan_ast_node_t *then_body = parse_block(p);

    zan_ast_node_t *else_body = NULL;
    if (parser_match(p, TK_ELSE)) {
        if (parser_check(p, TK_IF)) {
            else_body = parse_if_stmt(p);
        } else {
            else_body = parse_block(p);
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
    zan_ast_node_t *body = parse_block(p);

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
    if (!parser_check(p, TK_SEMICOLON)) {
        if (looks_like_var_decl(p)) {
            /* var decl without trailing ; — parse_var_decl handles it */
            init = parse_var_decl(p);
            /* parse_var_decl already consumed ; */
            goto parse_cond;
        } else {
            init = parse_expression(p);
            parser_expect(p, TK_SEMICOLON);
        }
    } else {
        parser_advance(p); /* ; */
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

    zan_ast_node_t *body = parse_block(p);

    zan_ast_node_t *n = zan_ast_new(p->arena, AST_FOR_STMT, loc);
    n->for_stmt.init = init;
    n->for_stmt.cond = cond;
    n->for_stmt.step = step;
    n->for_stmt.body = body;
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
        parser_advance(p); /* var */
    }

    zan_istr_t var_name = {0};
    if (parser_check(p, TK_IDENT)) {
        parser_advance(p);
        var_name = p->previous.str_val;
    }

    parser_expect(p, TK_IN);
    zan_ast_node_t *collection = parse_expression(p);
    parser_expect(p, TK_RPAREN);
    zan_ast_node_t *body = parse_block(p);

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

        if (parser_check(p, TK_CASE)) {
            parser_advance(p); /* case */
            pattern = parse_expression(p);
            parser_expect(p, TK_COLON);
        } else if (parser_check(p, TK_DEFAULT)) {
            parser_advance(p); /* default */
            parser_expect(p, TK_COLON);
            /* pattern stays NULL for default */
        } else {
            break;
        }

        /* collect statements until next case/default/} */
        zan_ast_node_t *body_block = zan_ast_new(p->arena, AST_BLOCK, case_loc);
        zan_ast_list_init(&body_block->block.stmts);
        while (!parser_check(p, TK_CASE) && !parser_check(p, TK_DEFAULT) &&
               !parser_check(p, TK_RBRACE) && !parser_check(p, TK_EOF)) {
            uint32_t before = p->current.loc.offset;
            zan_ast_node_t *stmt = parse_statement(p);
            zan_ast_list_push(&body_block->block.stmts, stmt, p->arena);
            if (p->current.loc.offset == before && !parser_check(p, TK_EOF)) {
                parser_advance(p);
            }
        }

        zan_ast_node_t *sc = zan_ast_new(p->arena, AST_SWITCH_CASE, case_loc);
        sc->switch_case.pattern = pattern;
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
        parser_advance(p); /* catch */

        zan_ast_node_t *catch_type = NULL;
        zan_istr_t catch_var = {0};

        if (parser_check(p, TK_LPAREN)) {
            parser_advance(p); /* ( */
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

static zan_ast_node_t *parse_statement(zan_parser_t *p) {
    /* `yield` is contextual: only `yield return expr;` / `yield break;` are
     * statements, so identifiers named `yield` keep working elsewhere. */
    if (p->current.kind == TK_IDENT && p->current.str_val.len == 5 &&
        memcmp(p->current.str_val.str, "yield", 5) == 0) {
        zan_token_kind_t nk = zan_lexer_peek(p->lex).kind;
        if (nk == TK_RETURN || nk == TK_BREAK) {
            zan_loc_t loc = p->current.loc;
            parser_advance(p); /* yield */
            parser_advance(p); /* return / break */
            zan_ast_node_t *n = zan_ast_new(p->arena, AST_YIELD_STMT, loc);
            n->yield_stmt.value =
                (nk == TK_RETURN) ? parse_expression(p) : NULL;
            parser_expect(p, TK_SEMICOLON);
            return n;
        }
    }

    /* `name:` at statement position is a goto label */
    if (p->current.kind == TK_IDENT && zan_lexer_peek(p->lex).kind == TK_COLON) {
        zan_loc_t loc = p->current.loc;
        parser_advance(p); /* name */
        zan_istr_t lname = p->previous.str_val;
        parser_advance(p); /* : */
        zan_ast_node_t *n = zan_ast_new(p->arena, AST_LABEL_STMT, loc);
        n->ident.name = lname;
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
        zan_ast_node_t *value = parse_expression(p);
        parser_expect(p, TK_SEMICOLON);
        zan_ast_node_t *n = zan_ast_new(p->arena, AST_THROW_STMT, loc);
        n->throw_stmt.value = value;
        return n;
    }
    case TK_SWITCH:
        return parse_switch_stmt(p);
    case TK_TRY:
        return parse_try_stmt(p);
    case TK_LOCK: {
        /* lock (expr) body */
        zan_loc_t loc = p->current.loc;
        parser_advance(p);
        parser_expect(p, TK_LPAREN);
        zan_ast_node_t *expr = parse_expression(p);
        parser_expect(p, TK_RPAREN);
        zan_ast_node_t *n = zan_ast_new(p->arena, AST_LOCK_STMT, loc);
        n->lock_stmt.expr = expr;
        n->lock_stmt.body = parse_statement(p);
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
        /* unsafe { ... } — everything is "unsafe" here; just run the block */
        parser_advance(p);
        return parse_block(p);
    case TK_FIXED: {
        /* fixed (T name = expr) body — no GC to pin against, so this is a
         * scoped alias: { T name = expr; body } */
        zan_loc_t loc = p->current.loc;
        parser_advance(p);
        parser_expect(p, TK_LPAREN);
        zan_ast_node_t *type = parse_type_ref(p);
        parser_match(p, TK_STAR); /* tolerate pointer-style `T*` */
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
        zan_ast_node_t *body = parse_statement(p);
        zan_ast_node_t *blk = zan_ast_new(p->arena, AST_BLOCK, loc);
        zan_ast_list_init(&blk->block.stmts);
        zan_ast_list_push(&blk->block.stmts, decl, p->arena);
        zan_ast_list_push(&blk->block.stmts, body, p->arena);
        return blk;
    }
    case TK_DO: {
        zan_loc_t loc = p->current.loc;
        parser_advance(p);
        zan_ast_node_t *body = parse_block(p);
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

    /* variable declaration or expression statement */
    if (looks_like_var_decl(p)) {
        return parse_var_decl(p);
    }

    /* expression statement */
    zan_loc_t loc = p->current.loc;
    zan_ast_node_t *expr = parse_expression(p);
    parser_expect(p, TK_SEMICOLON);
    zan_ast_node_t *n = zan_ast_new(p->arena, AST_EXPR_STMT, loc);
    n->expr_stmt.expr = expr;
    return n;
}

/* ---- class / struct members ---- */

/* `where T : C1, C2 ...` generic constraint clauses. Reference/value/ctor
 * constraints (`class`, `struct`, `new()`) are accepted but not recorded;
 * named type constraints are collected for the checker to enforce. */
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

    /* `this` modifier on the first parameter marks an extension method:
     * `static R M(this T recv, ...)` is callable as `recv.M(...)`. */
    int is_this = 0;
    if (parser_check(p, TK_THIS)) {
        parser_advance(p);
        is_this = 1;
    }

    /* contextual `params` modifier: `params T[] rest`. The variadic bundle is
     * lowered as List<T> so the callee has Count and normal indexing. */
    int is_params = 0;
    if (parser_check(p, TK_IDENT) && p->current.str_val.len == 6 &&
        memcmp(p->current.str_val.str, "params", 6) == 0) {
        parser_advance(p);
        is_params = 1;
    }

    /* `ref` / `out` by-reference parameter */
    int by_ref = 0;
    if (parser_check(p, TK_REF)) { parser_advance(p); by_ref = 1; }
    else if (parser_check(p, TK_OUT)) { parser_advance(p); by_ref = 2; }

    zan_ast_node_t *type = parse_type_ref(p);
    if (is_params && type && type->kind == AST_TYPE_REF && type->type_ref.is_array) {
        zan_ast_node_t *elem = zan_ast_new(p->arena, AST_TYPE_REF, loc);
        elem->type_ref.name = type->type_ref.name;
        zan_ast_list_init(&elem->type_ref.type_args);
        zan_ast_node_t *lst = zan_ast_new(p->arena, AST_TYPE_REF, loc);
        lst->type_ref.name.str = "List";
        lst->type_ref.name.len = 4;
        zan_ast_list_init(&lst->type_ref.type_args);
        zan_ast_list_push(&lst->type_ref.type_args, elem, p->arena);
        type = lst;
    }

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

/* ---- yield desugaring ----
 *
 * Iterator methods are lowered eagerly: a method containing `yield` gets a
 * hidden `List<T> __yield` accumulator, each `yield return e` appends to it,
 * `yield break` returns it early, and the method returns the finished list
 * (its `IEnumerable<T>` return type is rewritten to `List<T>`, which foreach
 * already iterates). */

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

static zan_ast_node_t *parse_member_decl(zan_parser_t *p) {
    /* parse [DllImport("lib")] attribute if present */
    zan_istr_t dll_import_lib = {NULL, 0};
    zan_istr_t dll_entry_point = {NULL, 0};
    while (parser_check(p, TK_LBRACKET)) {
        parser_advance(p); /* consume [ */
        if (parser_check(p, TK_IDENT) &&
            p->current.str_val.len == 9 &&
            memcmp(p->current.str_val.str, "DllImport", 9) == 0) {
            parser_advance(p); /* consume DllImport */
            if (parser_match(p, TK_LPAREN)) {
                if (parser_check(p, TK_STRING_LIT)) {
                    dll_import_lib = p->current.str_val;
                    parser_advance(p);
                }
                /* optional: EntryPoint = "name" */
                if (parser_match(p, TK_COMMA)) {
                    /* skip EntryPoint = "name" for now, just consume to ) */
                    while (!parser_check(p, TK_RPAREN) && !parser_check(p, TK_EOF)) {
                        if (parser_check(p, TK_IDENT) &&
                            p->current.str_val.len == 10 &&
                            memcmp(p->current.str_val.str, "EntryPoint", 10) == 0) {
                            parser_advance(p); /* EntryPoint */
                            parser_expect(p, TK_EQ); /* = */
                            if (parser_check(p, TK_STRING_LIT)) {
                                dll_entry_point = p->current.str_val;
                                parser_advance(p);
                            }
                        } else {
                            parser_advance(p);
                        }
                        if (!parser_match(p, TK_COMMA)) break;
                    }
                }
                parser_expect(p, TK_RPAREN);
            }
        } else {
            /* skip unknown attributes */
            while (!parser_check(p, TK_RBRACKET) && !parser_check(p, TK_EOF)) {
                parser_advance(p);
            }
        }
        parser_expect(p, TK_RBRACKET);
    }

    uint32_t mods = parse_modifiers(p);
    if (dll_import_lib.str) mods |= MOD_EXTERN;
    /* `event` is contextual: only a modifier when introducing an event field
     * (`event D E;`), so identifiers named `event` keep working elsewhere. */
    if (parser_check(p, TK_IDENT) && p->current.str_val.len == 5 &&
        memcmp(p->current.str_val.str, "event", 5) == 0 &&
        zan_lexer_peek(p->lex).kind == TK_IDENT) {
        parser_advance(p);
        mods |= MOD_EVENT;
    }
    zan_loc_t loc = p->current.loc;

    /* destructor: ~ClassName() { } */
    if (parser_check(p, TK_TILDE)) {
        parser_advance(p);
        parser_expect(p, TK_IDENT); /* class name */
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

    /* type or identifier */
    zan_ast_node_t *type = parse_type_ref(p);

    /* constructor: ClassName(params) [: base(args)] { } */
    if (parser_check(p, TK_LPAREN)) {
        zan_istr_t name = type->type_ref.name;
        zan_ast_list_t params = parse_param_list(p);

        /* optional base/this initializer: : base(...) or : this(...) */
        if (parser_match(p, TK_COLON)) {
            /* skip base(...) or this(...) for now */
            if (parser_check(p, TK_BASE) || parser_check(p, TK_THIS)) {
                parser_advance(p);
                if (parser_match(p, TK_LPAREN)) {
                    while (!parser_check(p, TK_RPAREN) && !parser_check(p, TK_EOF)) {
                        parse_expression(p);
                        if (!parser_match(p, TK_COMMA)) break;
                    }
                    parser_expect(p, TK_RPAREN);
                }
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
        zan_ast_list_init(&n->method_decl.type_params);
        return n;
    }

    /* operator overloading: static ReturnType operator+(params) { } */
    if (parser_check(p, TK_OPERATOR)) {
        parser_advance(p); /* consume 'operator' */
        /* next token is the operator symbol: +, -, *, /, ==, !=, <, >, etc. */
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
        parser_advance(p); /* consume operator token */
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
        n->method_decl.extern_lib = (zan_istr_t){NULL, 0};
        n->method_decl.entry_point = (zan_istr_t){NULL, 0};
        return n;
    }

    /* method or field: need name next */
    if (!parser_check(p, TK_IDENT)) {
        zan_diag_emit(p->diag, DIAG_ERROR, p->current.loc, "expected member name");
        return parser_error_node(p);
    }
    parser_advance(p);
    zan_istr_t name = p->previous.str_val;

    /* expression-bodied property: type Name => expr; */
    if (parser_check(p, TK_ARROW)) {
        parser_advance(p); /* => */
        zan_ast_node_t *expr = parse_expression(p);
        parser_expect(p, TK_SEMICOLON);

        /* treat as a get-only property → method returning the type */
        zan_ast_node_t *body = zan_ast_new(p->arena, AST_BLOCK, expr->loc);
        zan_ast_list_init(&body->block.stmts);
        zan_ast_node_t *ret = zan_ast_new(p->arena, AST_RETURN_STMT, expr->loc);
        ret->ret.value = expr;
        zan_ast_list_push(&body->block.stmts, ret, p->arena);

        zan_ast_node_t *n = zan_ast_new(p->arena, AST_METHOD_DECL, loc);
        n->method_decl.name = name;
        n->method_decl.return_type = type;
        zan_ast_list_init(&n->method_decl.params);
        zan_ast_list_init(&n->method_decl.type_params);
        n->method_decl.body = body;
        n->method_decl.modifiers = mods;
        return n;
    }

    /* method: name(params) { body } or name(params) => expr; */
    if (parser_check(p, TK_LPAREN) || parser_check(p, TK_LESS)) {
        /* optional type params */
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
            /* expression body: => expr; */
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
        n->method_decl.extern_lib = dll_import_lib;
        n->method_decl.entry_point = dll_entry_point;
        n->method_decl.where_clauses = wheres;
        desugar_yield_method(p, n);
        return n;
    }

    /* property: type Name { get; set; } or type Name { get { ... } set { ... } } */
    if (parser_check(p, TK_LBRACE)) {
        /* peek inside: if it starts with get/set, it's a property */
        zan_token_t peek = zan_lexer_peek(p->lex);
        if (peek.kind == TK_GET || peek.kind == TK_SET) {
            parser_advance(p); /* { */
            /* skip property body for now */
            int depth = 1;
            while (depth > 0 && !parser_check(p, TK_EOF)) {
                if (parser_check(p, TK_LBRACE)) depth++;
                if (parser_check(p, TK_RBRACE)) depth--;
                if (depth > 0) parser_advance(p);
            }
            if (parser_check(p, TK_RBRACE)) parser_advance(p);

            /* optional default value: = value; */
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
            return n;
        }
    }

    /* field: type name [= initializer]; */
    zan_ast_node_t *init = NULL;
    if (parser_match(p, TK_EQ)) {
        init = parse_expression(p);
    }
    parser_expect(p, TK_SEMICOLON);

    zan_ast_node_t *n = zan_ast_new(p->arena, AST_FIELD_DECL, loc);
    n->field_decl.name = name;
    n->field_decl.type = type;
    n->field_decl.initializer = init;
    n->field_decl.modifiers = mods;
    return n;
}

/* ---- type declarations ---- */

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

    /* name */
    zan_istr_t name = {0};
    if (parser_check(p, TK_IDENT)) {
        parser_advance(p);
        name = p->previous.str_val;
    } else {
        zan_diag_emit(p->diag, DIAG_ERROR, p->current.loc, "expected type name");
    }

    /* type parameters */
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

    /* base types: : Type1, Type2 */
    zan_ast_list_t bases;
    zan_ast_list_init(&bases);
    if (parser_match(p, TK_COLON)) {
        do {
            zan_ast_node_t *base = parse_type_ref(p);
            zan_ast_list_push(&bases, base, p->arena);
        } while (parser_match(p, TK_COMMA));
    }

    /* generic constraints: where T : C1, C2 */
    zan_ast_list_t wheres;
    zan_ast_list_init(&wheres);
    parse_where_clauses(p, &wheres);

    /* body */
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
            /* forward-progress guard: a member that fails to parse without
             * consuming its offending token must not spin the loop. */
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
    n->type_decl.where_clauses = wheres;
    return n;
}

/* ---- top level ---- */

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

/* ---- main entry ---- */

void zan_parser_init(zan_parser_t *p, zan_lexer_t *lex, zan_arena_t *arena,
                     zan_diag_t *diag) {
    memset(p, 0, sizeof(*p));
    p->lex = lex;
    p->arena = arena;
    p->diag = diag;
    parser_advance(p); /* prime first token */
}

zan_ast_node_t *zan_parser_parse(zan_parser_t *p) {
    zan_loc_t loc = p->current.loc;
    zan_ast_node_t *unit = zan_ast_new(p->arena, AST_COMPILATION_UNIT, loc);
    zan_ast_list_init(&unit->comp_unit.usings);
    zan_ast_list_init(&unit->comp_unit.decls);
    unit->comp_unit.ns = NULL;

    /* using declarations */
    while (parser_check(p, TK_USING)) {
        zan_ast_node_t *u = parse_using_decl(p);
        zan_ast_list_push(&unit->comp_unit.usings, u, p->arena);
    }

    /* optional namespace */
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
    }

    /* type declarations */
    while (!parser_check(p, TK_EOF) && !parser_check(p, TK_RBRACE)) {
        /* parse attributes: [StructLayout(...)] */
        bool has_c_layout = false;
        while (parser_check(p, TK_LBRACKET)) {
            parser_advance(p);
            if (parser_check(p, TK_IDENT) &&
                p->current.str_val.len == 12 &&
                memcmp(p->current.str_val.str, "StructLayout", 12) == 0) {
                has_c_layout = true;
            }
            while (!parser_check(p, TK_RBRACKET) && !parser_check(p, TK_EOF)) {
                parser_advance(p);
            }
            parser_expect(p, TK_RBRACKET);
        }

        uint32_t mods = parse_modifiers(p);

        /* `partial` is contextual: only a modifier right before a type kw */
        if (parser_check(p, TK_IDENT) && p->current.str_val.len == 7 &&
            memcmp(p->current.str_val.str, "partial", 7) == 0) {
            zan_token_kind_t nk = zan_lexer_peek(p->lex).kind;
            if (nk == TK_CLASS || nk == TK_STRUCT || nk == TK_INTERFACE) {
                parser_advance(p);
                mods |= MOD_PARTIAL;
            }
        }

        /* `record Name(T a, ...);` is contextual and lowers to a class */
        if (parser_check(p, TK_IDENT) && p->current.str_val.len == 6 &&
            memcmp(p->current.str_val.str, "record", 6) == 0 &&
            zan_lexer_peek(p->lex).kind == TK_IDENT) {
            parser_advance(p); /* record */
            parser_expect(p, TK_IDENT);
            zan_istr_t rname = p->previous.str_val;
            zan_ast_list_t rparams = parse_param_list(p);
            parser_expect(p, TK_SEMICOLON);
            gen_record_class(unit, rname, &rparams, p->arena, p->diag);
            continue;
        }

        if (parser_check(p, TK_CLASS) || parser_check(p, TK_STRUCT) ||
            parser_check(p, TK_INTERFACE) || parser_check(p, TK_ENUM)) {
            zan_ast_node_t *decl = parse_type_decl(p, mods);
            decl->type_decl.is_c_layout = has_c_layout;
            zan_ast_list_push(&unit->comp_unit.decls, decl, p->arena);
        } else if (parser_check(p, TK_DELEGATE)) {
            /* delegate ReturnType Name(params); */
            parser_advance(p); /* consume 'delegate' */
            zan_loc_t dloc = p->current.loc;
            zan_ast_node_t *ret_type = parse_type_ref(p);
            parser_expect(p, TK_IDENT);
            zan_istr_t dname = p->previous.str_val;
            /* optional generic type params: delegate R Name<T, R>(params); */
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
            ddecl->method_decl.extern_lib = (zan_istr_t){NULL, 0};
            ddecl->method_decl.entry_point = (zan_istr_t){NULL, 0};
            zan_ast_list_push(&unit->comp_unit.decls, ddecl, p->arena);
        } else {
            zan_diag_emit(p->diag, DIAG_ERROR, p->current.loc,
                          "expected type declaration (class, struct, interface, enum, or delegate)");
            parser_advance(p); /* skip to recover */
        }
    }

    /* close file-scoped namespace's brace if applicable */
    if (unit->comp_unit.ns && !unit->comp_unit.ns->namespace_decl.is_file_scoped) {
        parser_expect(p, TK_RBRACE);
    }

    return unit;
}

/* ---- event desugaring ----
 *
 * `event D E;` fields are lowered to a generated multicast holder class
 * `__Event_D` backed by a `List<D>`. Its `op_add`/`op_sub` operators drive
 * the compound-assignment desugar (`E += h` becomes `E = E + h`), so event
 * subscription needs no dedicated codegen; `E.Invoke(...)` raises the event
 * and `E.Count()` reports the handler count. The holder is materialized
 * lazily inside op_add, matching C#'s null-until-subscribed field events. */

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

static int tref_write(char *buf, int cap, zan_ast_node_t *t) {
    int n = 0;
    if (!t || t->kind != AST_TYPE_REF) return 0;
    n += snprintf(buf + n, (size_t)(cap - n), "%.*s",
                  (int)t->type_ref.name.len, t->type_ref.name.str);
    if (t->type_ref.type_args.count > 0) {
        n += snprintf(buf + n, (size_t)(cap - n), "<");
        for (int i = 0; i < t->type_ref.type_args.count; i++) {
            if (i > 0) n += snprintf(buf + n, (size_t)(cap - n), ", ");
            n += tref_write(buf + n, cap - n, t->type_ref.type_args.items[i]);
        }
        n += snprintf(buf + n, (size_t)(cap - n), ">");
    }
    if (t->type_ref.is_array) n += snprintf(buf + n, (size_t)(cap - n), "[]");
    if (t->type_ref.is_nullable) n += snprintf(buf + n, (size_t)(cap - n), "?");
    return n;
}

static void gen_event_holder(zan_ast_node_t *unit, zan_ast_node_t *ddecl,
                             const char *dname, const char *hname,
                             zan_arena_t *arena, zan_diag_t *diag) {
    char src[8192];
    int n = 0;
    n += snprintf(src + n, sizeof(src) - (size_t)n,
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
    for (int i = 0; i < ddecl->method_decl.params.count; i++) {
        zan_ast_node_t *pp = ddecl->method_decl.params.items[i];
        if (i > 0) n += snprintf(src + n, sizeof(src) - (size_t)n, ", ");
        n += tref_write(src + n, (int)sizeof(src) - n, pp->param.type);
        n += snprintf(src + n, sizeof(src) - (size_t)n, " a%d", i);
    }
    n += snprintf(src + n, sizeof(src) - (size_t)n,
        ") {\n"
        "        int i = 0;\n"
        "        while (i < hs.Count) {\n"
        "            %s d = hs[i];\n"
        "            d(", dname);
    for (int i = 0; i < ddecl->method_decl.params.count; i++) {
        n += snprintf(src + n, sizeof(src) - (size_t)n, "%sa%d",
                      i > 0 ? ", " : "", i);
    }
    n += snprintf(src + n, sizeof(src) - (size_t)n,
        ");\n"
        "            i = i + 1;\n"
        "        }\n"
        "    }\n"
        "}\n");

    char *gsrc = zan_arena_strdup(arena, src, (size_t)n);
    zan_lexer_t lex;
    zan_lexer_init(&lex, gsrc, (size_t)n, 0, arena, diag);
    zan_parser_t gp;
    zan_parser_init(&gp, &lex, arena, diag);
    zan_ast_node_t *gu = zan_parser_parse(&gp);
    for (int i = 0; i < gu->comp_unit.decls.count; i++)
        zan_ast_list_push(&unit->comp_unit.decls, gu->comp_unit.decls.items[i],
                          arena);
}

/* Lower `record Name(T1 A, T2 B);` into a class with public fields, a
 * positional constructor, field-wise op_eq/op_neq value equality, and a
 * C#-style ToString. */
static void gen_record_class(zan_ast_node_t *unit, zan_istr_t rname,
                             zan_ast_list_t *params, zan_arena_t *arena,
                             zan_diag_t *diag) {
    char src[8192];
    char nm[256];
    int n = 0;
    snprintf(nm, sizeof(nm), "%.*s", (int)rname.len, rname.str);
    n += snprintf(src + n, sizeof(src) - (size_t)n, "class %s {\n", nm);
    for (int i = 0; i < params->count; i++) {
        zan_ast_node_t *pp = params->items[i];
        n += snprintf(src + n, sizeof(src) - (size_t)n, "    public ");
        n += tref_write(src + n, (int)sizeof(src) - n, pp->param.type);
        n += snprintf(src + n, sizeof(src) - (size_t)n, " %.*s;\n",
                      (int)pp->param.name.len, pp->param.name.str);
    }
    n += snprintf(src + n, sizeof(src) - (size_t)n, "    public %s(", nm);
    for (int i = 0; i < params->count; i++) {
        zan_ast_node_t *pp = params->items[i];
        if (i > 0) n += snprintf(src + n, sizeof(src) - (size_t)n, ", ");
        n += tref_write(src + n, (int)sizeof(src) - n, pp->param.type);
        n += snprintf(src + n, sizeof(src) - (size_t)n, " %.*s",
                      (int)pp->param.name.len, pp->param.name.str);
    }
    n += snprintf(src + n, sizeof(src) - (size_t)n, ") {\n");
    for (int i = 0; i < params->count; i++) {
        zan_ast_node_t *pp = params->items[i];
        n += snprintf(src + n, sizeof(src) - (size_t)n,
                      "        this.%.*s = %.*s;\n",
                      (int)pp->param.name.len, pp->param.name.str,
                      (int)pp->param.name.len, pp->param.name.str);
    }
    n += snprintf(src + n, sizeof(src) - (size_t)n,
        "    }\n"
        "    static bool op_eq(%s l, %s r) {\n"
        "        if (l == null) { return r == null; }\n"
        "        if (r == null) { return false; }\n"
        "        return true", nm, nm);
    for (int i = 0; i < params->count; i++) {
        zan_ast_node_t *pp = params->items[i];
        n += snprintf(src + n, sizeof(src) - (size_t)n,
                      " && l.%.*s == r.%.*s",
                      (int)pp->param.name.len, pp->param.name.str,
                      (int)pp->param.name.len, pp->param.name.str);
    }
    n += snprintf(src + n, sizeof(src) - (size_t)n,
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
        n += snprintf(src + n, sizeof(src) - (size_t)n,
                      " + \"%s %.*s = \" + %s%.*s%s",
                      i > 0 ? "," : "",
                      (int)pp->param.name.len, pp->param.name.str,
                      is_str ? "" : "Convert.ToString(",
                      (int)pp->param.name.len, pp->param.name.str,
                      is_str ? "" : ")");
    }
    n += snprintf(src + n, sizeof(src) - (size_t)n,
        " + \" }\";\n"
        "    }\n"
        "}\n");

    char *gsrc = zan_arena_strdup(arena, src, (size_t)n);
    zan_lexer_t lex;
    zan_lexer_init(&lex, gsrc, (size_t)n, 0, arena, diag);
    zan_parser_t gp;
    zan_parser_init(&gp, &lex, arena, diag);
    zan_ast_node_t *gu = zan_parser_parse(&gp);
    for (int i = 0; i < gu->comp_unit.decls.count; i++)
        zan_ast_list_push(&unit->comp_unit.decls, gu->comp_unit.decls.items[i],
                          arena);
}

/* Merge `partial` type declarations: members and bases of later parts are
 * appended to the first part of the same name/kind, and later parts are
 * dropped from the unit. Runs before binding, so the rest of the pipeline
 * only ever sees one declaration per type. */
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
            for (int wi = 0; wi < d->type_decl.where_clauses.count; wi++)
                zan_ast_list_push(&first->type_decl.where_clauses,
                                  d->type_decl.where_clauses.items[wi], arena);
        } else {
            decls->items[out++] = d;
        }
    }
    decls->count = out;
    (void)diag;
}

void zan_parser_desugar_events(zan_ast_node_t *unit, zan_arena_t *arena,
                               zan_diag_t *diag) {
    if (!unit || unit->kind != AST_COMPILATION_UNIT) return;
    const char *generated[64];
    int generated_count = 0;
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
            if (!already && generated_count < 64) {
                gen_event_holder(unit, ddecl, dname, hname, arena, diag);
                generated[generated_count++] =
                    zan_arena_strdup(arena, hname, strlen(hname));
            }
            char *hn = zan_arena_strdup(arena, hname, strlen(hname));
            tr->type_ref.name = (zan_istr_t){hn, (uint32_t)strlen(hname)};
        }
    }
}
