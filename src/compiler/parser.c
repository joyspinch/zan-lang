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
#include <stdlib.h>
#include <stdarg.h>

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
static zan_ast_node_t *parse_embedded_stmt(zan_parser_t *p);
static bool looks_like_var_decl(zan_parser_t *p);
static zan_ast_node_t *parse_type_ref(zan_parser_t *p);
static zan_ast_node_t *parse_type_decl(zan_parser_t *p, uint32_t modifiers);
static void gen_record_class(zan_ast_node_t *unit, zan_istr_t rname,
                             zan_ast_list_t *params, zan_arena_t *arena,
                             zan_diag_t *diag);
static zan_ast_node_t *parse_unary(zan_parser_t *p);
static zan_ast_node_t *parse_parameter(zan_parser_t *p);
static uint32_t parse_modifiers(zan_parser_t *p);
static zan_ast_list_t parse_param_list(zan_parser_t *p);
static void parse_attr_usages(zan_parser_t *p, zan_ast_list_t *out,
                              zan_istr_t *out_lib, zan_istr_t *out_entry);
static bool parse_top_level_decl(zan_parser_t *p, zan_ast_node_t *unit);

/* Parses one full top-level declaration -- attributes, modifiers, class/
 * struct/interface/enum/delegate, or a `record` lowering -- into
 * unit->comp_unit.decls. Shared verbatim by the compilation-unit decls loop
 * and the block-scoped `namespace X { ... }` member loop, so both spellings
 * treat a declaration identically (attributes, StructLayout detection,
 * partial, record). Returns false when no declaration could be parsed (an
 * error has been emitted); the caller skips one token to recover. */
static bool parse_top_level_decl(zan_parser_t *p, zan_ast_node_t *unit) {
    /* parse attributes: retained on the type; [StructLayout] also toggles C layout */
    bool has_c_layout = false;
    bool has_explicit_layout = false;
    zan_ast_list_t type_attrs;
    zan_ast_list_init(&type_attrs);
    parse_attr_usages(p, &type_attrs, NULL, NULL);
    for (int _ai = 0; _ai < type_attrs.count; _ai++) {
        zan_istr_t _n = type_attrs.items[_ai]->attribute.name->ident.name;
        if (_n.str && _n.len == 12 && memcmp(_n.str, "StructLayout", 12) == 0) {
            has_c_layout = true;
            /* `LayoutKind.Explicit` parses as a member access whose last
             * segment names the kind; anything else stays sequential. */
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
        return true;
    }

    if (parser_check(p, TK_CLASS) || parser_check(p, TK_STRUCT) ||
        parser_check(p, TK_INTERFACE) || parser_check(p, TK_ENUM)) {
        zan_ast_node_t *decl = parse_type_decl(p, mods);
        decl->type_decl.is_c_layout = has_c_layout;
        decl->type_decl.is_explicit_layout = has_explicit_layout;
        decl->attributes = type_attrs;
        zan_ast_list_push(&unit->comp_unit.decls, decl, p->arena);
        return true;
    }
    if (parser_check(p, TK_DELEGATE)) {
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
        return true;
    }
    zan_diag_emit(p->diag, DIAG_ERROR, p->current.loc,
                  "expected type declaration (class, struct, interface, enum, or delegate)");
    return false;
}

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

/* Rank of the bracket at the current token, when it is an array rank
 * specifier: `[]` -> 1, `[,]` -> 2, `[,,]` -> 3. Returns 0 when the bracket
 * holds a size expression (`int[3]` in a new-expression): that bracket is
 * not part of the type and is left for the caller. Whitespace inside the
 * bracket is tolerated. `p->lex->pos` points just past the `[`. */
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

    /* C# tuple type: `(int, string)`. Each element may itself be named
     * (`(int x, string y)`); the names are dropped -- Item1/Item2 fields back
     * the tuple regardless. */
    if (parser_check(p, TK_LPAREN)) {
        parser_advance(p); /* ( */
        zan_ast_node_t *tn = zan_ast_new(p->arena, AST_TUPLE_TYPE, loc);
        zan_ast_list_init(&tn->tuple_type.elems);
        while (!parser_check(p, TK_RPAREN) && !parser_check(p, TK_EOF)) {
            zan_ast_node_t *et = parse_type_ref(p);
            zan_ast_list_push(&tn->tuple_type.elems, et, p->arena);
            /* named element `int x`: consume the name, keep the type */
            if (p->current.kind == TK_IDENT &&
                !(et->kind == AST_TYPE_REF && et->type_ref.is_array)) {
                /* after a type ref, an identifier is the element name unless it
                 * is part of a qualified name already consumed by parse_type_ref
                 * (A.B binds the dot chain). Skip it. */
                parser_advance(p);
            }
            if (!parser_match(p, TK_COMMA)) break;
        }
        parser_expect(p, TK_RPAREN);
        return tn;
    }

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
        /* qualified type name: A.B.C  (dotted). Build the dotted spelling;
         * the namespace resolver (nsresolve.c) maps it to the real type.
         * Only enter when '.' is directly followed by an identifier, so
         * member access on values elsewhere is unaffected. */
        if (parser_check(p, TK_DOT) &&
            zan_lexer_peek(p->lex).kind == TK_IDENT) {
            char qbuf[512];
            size_t qn = 0;
            for (uint32_t qi = 0; qi < name.len && qn < sizeof qbuf; qi++)
                qbuf[qn++] = name.str[qi];
            while (parser_check(p, TK_DOT) &&
                   zan_lexer_peek(p->lex).kind == TK_IDENT) {
                parser_advance(p); /* '.' */
                parser_advance(p); /* IDENT */
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

    /* nullable/array suffixes, in either order:
     *   `int?[]`  -- array whose element type is `int?` (nullable value)
     *   `int[]?`  -- nullable array reference; for arrays this is a no-op
     *                (an array pointer already admits null), so the `?` is
     *                consumed and not recorded.
     *   `int[][]` / `int[,]` / `int[][,]` -- jagged and rectangular shapes.
     * A bracket is a rank specifier only when it holds nothing but commas
     * (`[]`, `[,]`); `int[3]` leaves the bracket for the new-expression's
     * dimension list. C# folds rank specifiers with the LEFTMOST outermost,
     * so `int[][,]` is a 1D array of `int[,]`: the collected ranks are
     * wrapped right-to-left, each wrapper's array_element pointing at the
     * inner declaration. */
    int ranks[16];
    int nranks = 0;
    bool seen_array = false;
    for (;;) {
        if (parser_match(p, TK_QUESTION)) {
            if (seen_array) {
                /* `int[]?` -- the array reference is nullable; nothing to do. */
            } else {
                type_node->type_ref.is_nullable = true;
            }
        } else if (parser_check(p, TK_LBRACKET)) {
            int rank = array_suffix_rank(p);
            if (rank <= 0) break; /* bracket holds a size: `int[3]` */
            parser_advance(p);    /* [ */
            for (int c = 1; c < rank; c++) parser_advance(p); /* commas */
            parser_advance(p);    /* ] */
            if (nranks < 16) ranks[nranks++] = rank;
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
        return cur;
    }

    return type_node;
}

/* ---- modifiers ---- */

/* `init` is a contextual keyword: it is only special as a property accessor
 * (`{ get; init; }`). The lexer must not reserve it, or ordinary identifiers
 * named `init` become impossible. */
static bool is_init_accessor_kw(zan_parser_t *p) {
    return p->current.kind == TK_IDENT && p->current.str_val.len == 4 &&
           memcmp(p->current.str_val.str, "init", 4) == 0;
}

/* `checked` / `unchecked` are contextual: only special when followed by `(`
 * (an expression) or `{` (a statement block). Identifiers with those names
 * (e.g. a parameter `bool checked`) keep working elsewhere. */
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
        /* C# `const` on a member is an implicitly static, never-assigned
         * field; the compiler has no separate constant storage class, so it
         * lowers to `static readonly`. */
        case TK_CONST:     parser_advance(p);
                           mods |= MOD_STATIC | MOD_READONLY; break;
        case TK_EXTERN:    parser_advance(p); mods |= MOD_EXTERN;    break;
        case TK_ASYNC:     parser_advance(p); mods |= MOD_ASYNC;     break;
        case TK_UNSAFE:    parser_advance(p); mods |= MOD_UNSAFE;    break;
        case TK_WEAK:      parser_advance(p); mods |= MOD_WEAK;      break;
        /* `ref struct` (stack-allocated struct). The modifier is recorded but
         * the type has no special stack-only semantics in this runtime. */
        case TK_REF:       parser_advance(p); mods |= MOD_REF;       break;
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

/* `(Name)operand` — a cast to a user-declared type (delegate, class, struct)
 * rather than a parenthesized expression. Only the unambiguous shape is
 * accepted: a single identifier inside the parentheses followed by a token
 * that can only start an operand (identifier, literal, `this`, `new` or a
 * nested parenthesis), so `(x) + y` and `(x)` keep parsing as grouping.
 * This is what makes `(WndProc)addr` — an address obtained at runtime turned
 * into a callable function pointer — expressible in the language. */
static bool paren_is_named_cast(zan_parser_t *p) {
    if (zan_lexer_peek(p->lex).kind != TK_IDENT) return false;
    zan_lexer_t saved_lex = *p->lex;
    zan_token_t saved_cur = p->current;
    zan_token_t saved_prev = p->previous;
    bool result = false;
    parser_advance(p); /* ( */
    parser_advance(p); /* Name */
    if (p->current.kind == TK_RPAREN) {
        switch (zan_lexer_peek(p->lex).kind) {
        case TK_IDENT: case TK_INT_LIT: case TK_FLOAT_LIT:
        case TK_STRING_LIT: case TK_CHAR_LIT:
        case TK_THIS: case TK_NEW:
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

static bool is_type_kw(zan_token_kind_t k);

/* A call argument: an expression, a named `name: expr`, or a by-reference
 * `ref x` / `out x` / `out T x` argument. Forward-declared because
 * `new Type(args)` (inside parse_primary) parses constructor arguments with
 * it before its definition below. */
static zan_ast_node_t *parse_call_arg(zan_parser_t *p);

static zan_ast_node_t *parse_primary(zan_parser_t *p) {
    zan_loc_t loc = p->current.loc;

    /* checked/unchecked: accepted for C# source compatibility. Overflow
     * checking is always off in this runtime (wrapping semantics), so both
     * forms lower to a plain expression — a no-op. Contextual: only when the
     * identifier is followed by `(` or `{`. */
    if (is_checked_use(p)) {
        bool is_checked = p->current.str_val.len == 7;
        parser_advance(p); /* checked | unchecked */
        if (parser_check(p, TK_LPAREN)) {
            parser_advance(p); /* ( */
            zan_ast_node_t *n = parse_expression(p);
            parser_expect(p, TK_RPAREN);
            return n;
        }
        return parse_block(p); /* checked { ... } statement block */
    }

    /* Type keyword as a static receiver: `int.Parse(...)`, `string.Join(...)`.
     * Lower the keyword to an identifier so member access parses normally. */
    if (is_type_kw(p->current.kind) && zan_lexer_peek(p->lex).kind == TK_DOT) {
        const char *nm = zan_token_kind_name(p->current.kind);
        parser_advance(p);
        zan_ast_node_t *n = zan_ast_new(p->arena, AST_IDENTIFIER, loc);
        n->ident.name = (zan_istr_t){ nm, (int)strlen(nm) };
        return n;
    }

    /* LINQ query: `from x in src [where c]... select e` — `from` is
     * contextual, committed only when `in` follows the range variable */
    if (p->current.kind == TK_IDENT && p->current.str_val.len == 4 &&
        memcmp(p->current.str_val.str, "from", 4) == 0 &&
        zan_lexer_peek(p->lex).kind == TK_IDENT) {
        zan_lexer_t saved_lex = *p->lex;
        zan_token_t saved_cur = p->current;
        zan_token_t saved_prev = p->previous;
        parser_advance(p); /* from */
        parser_advance(p); /* var */
        if (p->current.kind == TK_IN) {
            zan_istr_t qvar = p->previous.str_val;
            parser_advance(p); /* in */
            zan_ast_node_t *n = zan_ast_new(p->arena, AST_QUERY_EXPR, loc);
            n->query.var = qvar;
            n->query.source = parse_expression(p);
            zan_ast_list_init(&n->query.clauses);
            n->query.group_expr = NULL;
            n->query.group_key = NULL;
            n->query.group_into = (zan_istr_t){ NULL, 0 };
            n->query.select = NULL;
            /* sub-clauses: where / let / orderby / join may repeat in any
             * order (C# order); they land in one ordered list so later
             * passes can honour scope (a let is visible only to clauses
             * after it; a where after a join sees the join variable).
             * group is a single trailing clause. */
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
                    /* one or more comma-separated keys, each optionally
                     * followed by ascending/descending */
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
            /* trailing group clause: `group e by k [into g]` — a `group`
             * without `into` is terminal (the result is the grouping list
             * itself), so no select is required then. */
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
                    /* C#: `group e by k` without `into` is terminal — a
                     * select after it has no range variable to bind. */
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
        /* $"text {expr:fmt} text {expr} text" — a `:` after the expression is
         * the format specifier (C#: `{v:D4}`); ternaries inside a hole must be
         * parenthesized, so a top-level `:` always starts a format. */
        zan_ast_node_t *n = zan_ast_new(p->arena, AST_STRING_INTERP, loc);
        zan_ast_list_init(&n->string_interp.parts);
        zan_ast_list_init(&n->string_interp.formats);
        parser_advance(p); /* consume INTERP_START */
        /* add leading text segment */
        zan_ast_node_t *seg = zan_ast_new(p->arena, AST_STRING_LITERAL, loc);
        seg->str_val = p->previous.str_val;
        zan_ast_list_push(&n->string_interp.parts, seg, p->arena);
        /* parse expr + mid/end pairs */
        while (true) {
            zan_ast_node_t *expr = parse_expression(p);
            zan_ast_list_push(&n->string_interp.parts, expr, p->arena);
            /* optional format specifier after the expression */
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
        case TK_BOOL: case TK_CHAR: case TK_NINT: {
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
        /* Cast to a named type: (Delegate)addr, (Handle)value. */
        if (paren_is_named_cast(p)) {
            parser_advance(p); /* ( */
            zan_ast_node_t *ctype = parse_type_ref(p);
            parser_expect(p, TK_RPAREN);
            zan_ast_node_t *coperand = parse_unary(p);
            zan_ast_node_t *cn = zan_ast_new(p->arena, AST_CAST_EXPR, loc);
            cn->cast.type = ctype;
            cn->cast.expr = coperand;
            return cn;
        }
        /* could be grouped expression or lambda: (params) => expr */
        /* save lexer state to try lambda parse */
        parser_advance(p); /* ( */
        zan_ast_node_t *expr = parse_expression(p);
        /* C# tuple literal: `(e1, e2, ...)`. The first element is already
         * parsed; a comma means the parens group a tuple, not a parenthesised
         * single expression. Named elements (`(x: 1, y: 2)`) are lowered to
         * plain positional tuples here. */
        if (parser_check(p, TK_COMMA)) {
            zan_ast_node_t *tup = zan_ast_new(p->arena, AST_TUPLE_EXPR, loc);
            zan_ast_list_init(&tup->tuple_expr.items);
            zan_ast_list_push(&tup->tuple_expr.items, expr, p->arena);
            while (parser_match(p, TK_COMMA)) {
                if (parser_check(p, TK_RPAREN) || parser_check(p, TK_EOF)) break;
                /* named element `(a: 1)`: skip the name and the colon */
                if (p->current.kind == TK_IDENT &&
                    zan_lexer_peek(p->lex).kind == TK_COLON) {
                    parser_advance(p); /* name */
                    parser_advance(p); /* : */
                }
                zan_ast_node_t *item = parse_expression(p);
                zan_ast_list_push(&tup->tuple_expr.items, item, p->arena);
            }
            parser_expect(p, TK_RPAREN);
            return tup;
        }
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
    case TK_DELEGATE: {
        /* C# anonymous method: `delegate (int x) { return x * 2; }` (with or
         * without the parenthesised parameter list). Lowers to a lambda -- the
         * delegate target type (Func/Action/custom delegate) supplies the
         * parameter types via the existing lambda-to-delegate assignment path. */
        zan_loc_t dloc = p->current.loc;
        parser_advance(p); /* delegate */
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
        parser_advance(p); /* new */
        zan_loc_t newloc = p->previous.loc;
        zan_ast_node_t *n = NULL;

        /* C#-style anonymous object literal: `new { expr1, expr2, ... }`.
         * The members are plain expressions; each member's name is taken from
         * a `p.field` expression (the last segment) when available, else an
         * implicit `m0, m1, ...`. Lowered to a NEW_EXPR with type == NULL and
         * args holding the member expressions. */
        if (parser_check(p, TK_LBRACE)) {
            parser_advance(p); /* { */
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

        /* array creation: new Type[d1, d2, ...] with optional trailing rank
         * specifiers (`new int[3][]` -- a jagged outer array whose elements
         * are int[] rows; the sizes belong to the leftmost level). */
        if (parser_check(p, TK_LBRACKET) && !type->type_ref.is_array) {
            parser_advance(p); /* [ */
            n->new_expr.is_array = true;
            while (!parser_check(p, TK_RBRACKET) && !parser_check(p, TK_EOF)) {
                zan_ast_node_t *dim = parse_expression(p);
                zan_ast_list_push(&n->new_expr.args, dim, p->arena);
                n->new_expr.array_rank++;
                if (!parser_match(p, TK_COMMA)) break;
            }
            parser_expect(p, TK_RBRACKET);
            /* trailing rank-only brackets nest the element type below the
             * sized level: new int[3][] allocates a 3-row array of int[]. */
            while (parser_check(p, TK_LBRACKET)) {
                int rank = array_suffix_rank(p);
                if (rank <= 0) break;
                parser_advance(p);
                for (int c = 1; c < rank; c++) parser_advance(p);
                parser_advance(p);
                zan_ast_node_t *w = zan_ast_new(p->arena, AST_TYPE_REF, loc);
                w->type_ref.name = type->type_ref.name;
                w->type_ref.is_array = true;
                w->type_ref.array_rank = rank;
                w->type_ref.array_element = type;
                zan_ast_list_init(&w->type_ref.type_args);
                type = w;
            }
            /* the allocation type carries the sized level as its OUTERMOST
             * rank: new int[3][] is an int[][] whose outer array is sized */
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
        /* collection/object initializer: { items }
         * A plain element is a single expression (list initializer).
         * A braced element `{ k, v }` is a dictionary initializer entry; its
         * inner expressions are flattened into args as consecutive key/value
         * pairs and consumed pairwise by the Dict lowering. */
        if (parser_check(p, TK_LBRACE) && type->kind == AST_TYPE_REF &&
            type->type_ref.is_array && !n->new_expr.is_array) {
            /* new T[] { a, b, c }: the braces hold the elements and the
             * length is how many there are. */
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

/* A call argument: an expression, or a by-reference `ref x` / `out x` /
 * `out T x` argument (the latter declares a fresh local at the call site).
 * Also used by `new Type(args)` so named arguments work in constructors. */
static zan_ast_node_t *parse_call_arg(zan_parser_t *p) {
    /* named argument: `name: expr`. Only a bare identifier followed by `:`
     * is treated as a name -- a conditional `a ? b : c` has a full expression
     * before the colon, so it never reaches this branch. */
    if (parser_check(p, TK_IDENT)) {
        zan_token_t peek = zan_lexer_peek(p->lex);
        if (peek.kind == TK_COLON) {
            zan_loc_t loc = p->current.loc;
            zan_istr_t name = p->current.str_val;
            parser_advance(p); /* name */
            parser_advance(p); /* : */
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

/* Disambiguate `Name<...>.` (static access on a constructed generic type,
 * e.g. `Box<int>.Create(7)`) from a `<` comparison, the same way
 * looks_like_call_type_args does for `name<...>(` — here the matching `>`
 * must be followed by `.`. */
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
        } else if (k == TK_GREATER || k == TK_GREATER_GREATER) {
            depth -= (k == TK_GREATER_GREATER) ? 2 : 1;
            if (depth <= 0) {
                ok = (zan_lexer_peek(p->lex).kind == TK_DOT);
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

/* Disambiguate `Name<...>{` (object initializer on a constructed generic
 * type, e.g. `List<int> { 1, 2 }` spelled as a postfix on the type name)
 * from a `<` comparison: the matching `>` must be followed by `{`. */
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
        } else if (k == TK_GREATER || k == TK_GREATER_GREATER) {
            depth -= (k == TK_GREATER_GREATER) ? 2 : 1;
            if (depth <= 0) {
                ok = (zan_lexer_peek(p->lex).kind == TK_LBRACE);
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

/* `Task.WhenAll(handles)` / `Task.WhenAny(handles)` name the fan-out joins the
 * design docs promise, but the joins themselves are ordinary async standard
 * library code (System.Threading.TaskJoin) rather than compiler intrinsics:
 * they suspend and resume like any other awaited call, which is exactly what
 * the await lowering already does. Rewriting the receiver here -- before name
 * resolution, binding and irgen -- lets the promised spelling resolve to that
 * class, so `await Task.WhenAll(list)` needs no special case downstream.
 * Task's real intrinsics (Spawn/Run/Cancel/Delay/IsDone/IsCancellationRequested)
 * are untouched. */
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

    /* postfix: call, member access, index, ++, --, object initializer */
static bool is_case_type_pattern(zan_parser_t *p);

static zan_ast_node_t *parse_postfix(zan_parser_t *p) {
    zan_ast_node_t *expr = parse_primary(p);

    for (;;) {
        zan_loc_t loc = p->current.loc;

        /* object initializer: Identifier { field = val, ... } — when the
         * preceding expression is a simple identifier (optionally carrying a
         * constructed generic instantiation), a member access, or a call
         * (factory continuation: `Panel.Row() { ... }`). */
        if (parser_check(p, TK_LBRACE) &&
            (expr->kind == AST_IDENTIFIER || expr->kind == AST_MEMBER_ACCESS ||
             expr->kind == AST_CALL)) {
            /* object initializer on a constructed generic type
             * (`List<int> { 1, 2 }`): the instantiation was captured on the
             * identifier by the `<...>` branch below, so the new-expression's
             * type IS that instantiation and the braces hold member-writes. */
            if (expr->kind == AST_IDENTIFIER && expr->inst_type_ref) {
                zan_ast_node_t *n = zan_ast_new(p->arena, AST_NEW_EXPR, loc);
                n->new_expr.type = expr->inst_type_ref;
                zan_ast_list_init(&n->new_expr.args);
                zan_ast_list_init(&n->new_expr.arg_inits);
                parser_advance(p); /* { */
                while (!parser_check(p, TK_RBRACE) && !parser_check(p, TK_EOF)) {
                    zan_ast_node_t *field_init;
                    if (parser_check(p, TK_IDENT) &&
                        zan_lexer_peek(p->lex).kind == TK_EQ) {
                        zan_loc_t nloc = p->current.loc;
                        zan_istr_t name = p->current.str_val;
                        parser_advance(p); /* name */
                        parser_advance(p); /* = */
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

            /* factory-call continuation (`Panel.Row() { Children = {...} }`):
             * the braces continue the just-parsed call, so keep the callee in
             * call_init; the checker types it via the call and irgen lowers
             * the call before applying the member-writes. */
            if (expr->kind == AST_CALL) {
                zan_ast_node_t *n = zan_ast_new(p->arena, AST_NEW_EXPR, loc);
                n->new_expr.call_init = expr;
                zan_ast_list_init(&n->new_expr.args);
                zan_ast_list_init(&n->new_expr.arg_inits);
                parser_advance(p); /* { */
                while (!parser_check(p, TK_RBRACE) && !parser_check(p, TK_EOF)) {
                    zan_ast_node_t *field_init;
                    if (parser_check(p, TK_IDENT) &&
                        zan_lexer_peek(p->lex).kind == TK_EQ) {
                        zan_loc_t nloc = p->current.loc;
                        zan_istr_t name = p->current.str_val;
                        parser_advance(p); /* name */
                        parser_advance(p); /* = */
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
            /* plain identifier / member access: re-interpret it as the type
             * name of a new-expression */
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

            /* parse field = value pairs as assignment expressions.
             * `Name = { a, b }` is a member collection initializer (C#
             * semantics): it is kept as a dedicated AST_COLL_INIT node so irgen
             * lowers it to Add(item) per element on that member, instead of
             * reading the braces as a nested dictionary entry. */
            while (!parser_check(p, TK_RBRACE) && !parser_check(p, TK_EOF)) {
                zan_ast_node_t *field_init;
                if (parser_check(p, TK_IDENT) &&
                    zan_lexer_peek(p->lex).kind == TK_EQ) {
                    zan_loc_t nloc = p->current.loc;
                    zan_istr_t name = p->current.str_val;
                    parser_advance(p); /* name */
                    parser_advance(p); /* = */
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
        } else if (parser_check(p, TK_LESS) && expr->kind == AST_IDENTIFIER &&
                   !expr->inst_type_ref &&
                   (looks_like_type_args_before_dot(p) ||
                    looks_like_type_args_before_brace(p))) {
            /* static access on a constructed generic type: Box<int>.Create(7).
             * The identifier keeps its simple name and carries the
             * instantiation, so the following `.` or initializer brace is
             * handled by the ordinary postfix loop. */
            zan_ast_node_t *tref = zan_ast_new(p->arena, AST_TYPE_REF, expr->loc);
            tref->type_ref.name = expr->ident.name;
            tref->type_ref.is_nullable = false;
            tref->type_ref.is_array = false;
            zan_ast_list_init(&tref->type_ref.type_args);
            parser_advance(p); /* < */
            while (!parser_check(p, TK_GREATER) && !parser_check(p, TK_EOF)) {
                zan_ast_node_t *ta = parse_type_ref(p);
                zan_ast_list_push(&tref->type_ref.type_args, ta, p->arena);
                if (!parser_match(p, TK_COMMA)) break;
            }
            parser_expect_gt(p);
            expr->inst_type_ref = tref;
        } else if (parser_match(p, TK_LBRACKET)) {
            /* indexing; a rank-2+ array takes several indices: m[i, j] */
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
            /* switch expression: `expr switch { arm, arm, ... }` (B6). Each
             * arm is `pattern => result`, `pattern when g => result`, or
             * `_ => result`; arms are comma-separated with an optional
             * trailing comma. */
            parser_advance(p); /* switch */
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

                /* `_ => ...` discard and `default => ...` are the fallback
                 * arm; handle `_` before the type-pattern test (its next
                 * token is `=>`, not a name). */
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
                    /* constant pattern: `1 => ...`, `null => ...` */
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
        } else if (parser_check(p, TK_PLUS_PLUS) || parser_check(p, TK_MINUS_MINUS)) {
            /* postfix ++/--: consume exactly one operator and stop, so
             * `x++`/`x--` lower to a single AST_POSTFIX_UNARY. */
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

/* Cap on expression recursion depth. Every expression-parsing cycle (nested
 * parentheses, call/index arguments, prefix-operator chains, ternaries, ...)
 * passes through parse_unary, so bounding it here bounds the whole recursive
 * descent and turns pathological nesting (e.g. thousands of '(' or '!') into a
 * diagnostic instead of a stack overflow. The limit is far beyond any
 * hand-written or normally-generated expression. */
#define ZAN_PARSER_MAX_EXPR_DEPTH 256

static zan_ast_node_t *parse_unary_inner(zan_parser_t *p);

/* unary: !x, -x, ~x, ++x, --x (with a recursion-depth guard) */
static zan_ast_node_t *parse_unary(zan_parser_t *p) {
    if (p->expr_depth >= ZAN_PARSER_MAX_EXPR_DEPTH) {
        zan_diag_emit(p->diag, DIAG_ERROR, p->current.loc,
                      "expression nesting too deep (max %d)",
                      ZAN_PARSER_MAX_EXPR_DEPTH);
        return parser_error_node(p);
    }
    p->expr_depth++;
    zan_ast_node_t *n = parse_unary_inner(p);
    p->expr_depth--;
    return n;
}

static zan_ast_node_t *parse_unary_inner(zan_parser_t *p) {
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
            zan_ast_node_t *n = zan_ast_new(p->arena,
                op == TK_IS ? AST_IS_EXPR : AST_AS_EXPR, loc);
            n->type_test.expr = left;
            n->type_test.type = NULL;
            n->type_test.var_name = (zan_istr_t){NULL, 0};
            n->type_test.is_not = false;
            if (op == TK_AS) {
                n->type_test.type = parse_type_ref(p);
                left = n;
                continue;
            }
            /* `is` patterns: `is T`, `is T x`, `is null`, `is not T`,
             * `is not null`. */
            if (parser_match(p, TK_NOT)) {
                n->type_test.is_not = true;
            }
            if (parser_check(p, TK_NULL)) {
                /* `is null` / `is not null` — no type operand */
                parser_advance(p);
            } else {
                n->type_test.type = parse_type_ref(p);
                /* pattern variable: `is T x` — a bare name after the type
                 * (not `is` or `as`, which would be a chained test). */
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

        /* `member = { a, b, c }` is a member collection initializer (C#
         * semantics): inside an object initializer the named member receives
         * Add(item) per element. parse_primary has no brace expression, so
         * without this branch the `{` reached it and died as "unexpected
         * token". Statement-level `x = { ... }` was equally a parse error
         * before, so this cannot change the meaning of existing programs;
         * checker/irgen reject AST_COLL_INIT outside an object-initializer
         * tail. */
        if (op == TK_EQ && parser_check(p, TK_LBRACE) &&
            (expr->kind == AST_IDENTIFIER || expr->kind == AST_MEMBER_ACCESS)) {
            zan_istr_t name = (expr->kind == AST_IDENTIFIER)
                                  ? expr->ident.name
                                  : expr->member.name;
            zan_ast_node_t *n = zan_ast_new(p->arena, AST_COLL_INIT, loc);
            n->coll_init.name = name;
            zan_ast_list_init(&n->coll_init.items);
            parser_advance(p); /* { */
            while (!parser_check(p, TK_RBRACE) && !parser_check(p, TK_EOF)) {
                zan_ast_node_t *item = parse_expression(p);
                zan_ast_list_push(&n->coll_init.items, item, p->arena);
                if (!parser_match(p, TK_COMMA)) break;
            }
            parser_expect(p, TK_RBRACE);
            return n;
        }

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
        /* `expr` is now referenced twice (value side and store target);
         * compound_base tells irgen to rewrite the spine for single
         * evaluation before lowering. */
        n->binary.compound_base = base;
        n->binary.left = expr;
        n->binary.right = right;
        return n;
    }

    return expr;
}

/* ---- statements ---- */

/* Cap on statement/block nesting. Thousands of nested `{` recurse
 * parse_block -> parse_statement without bound and would exhaust the C stack
 * before any diagnostic fires; mirror the expression depth cap. */
#define ZAN_PARSER_MAX_STMT_DEPTH 128

static zan_ast_node_t *parse_block(zan_parser_t *p) {
    if (p->stmt_depth >= ZAN_PARSER_MAX_STMT_DEPTH) {
        zan_diag_emit(p->diag, DIAG_ERROR, p->current.loc,
                      "statement nesting too deep (max %d)",
                      ZAN_PARSER_MAX_STMT_DEPTH);
        /* drain to the matching close brace so parsing resumes sanely */
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

    p->stmt_depth--;
    parser_expect(p, TK_RBRACE);
    return block;
}

/* C# deconstruction declaration: `var (a, b) = rhs;` or `(int a, string b) =
 * rhs;`. Lowered to AST_TUPLE_DECON with per-element names/types; irgen reads
 * the initializer's Item1..ItemN fields into fresh locals.
 *
 * `looks_like_decon_decl` peeks whether `(T name, ...) = rhs` / `var (...)` is
 * coming. `(` starts both a grouped expression and a typed deconstruction; the
 * second element's presence (a `,` after `name`) is what makes it a decon. */

/* Non-consuming lookahead of the k-th token after the current one. zan_lexer_peek
 * only reaches one token ahead, so walk the lexer k times saving/restoring its
 * state (the token position plus every interpolation depth the lexer tracks). */
static zan_token_t lexer_peek_n(zan_parser_t *p, int k) {
    size_t pos = p->lex->pos;
    uint32_t line = p->lex->line;
    uint32_t col = p->lex->col;
    int idepth = p->lex->interp_depth;
    int nsave = idepth < ZAN_MAX_INTERP_DEPTH ? idepth : ZAN_MAX_INTERP_DEPTH;
    zan_interp_level_t istack[ZAN_MAX_INTERP_DEPTH];
    if (nsave > 0)
        memcpy(istack, p->lex->interp_stack, sizeof(istack[0]) * (size_t)nsave);
    zan_token_t tok = {0};
    for (int i = 0; i < k; i++) tok = zan_lexer_next(p->lex);
    p->lex->pos = pos;
    p->lex->line = line;
    p->lex->col = col;
    p->lex->interp_depth = idepth;
    if (nsave > 0)
        memcpy(p->lex->interp_stack, istack, sizeof(istack[0]) * (size_t)nsave);
    return tok;
}

static bool looks_like_decon_decl(zan_parser_t *p) {
    if (!parser_check(p, TK_LPAREN)) return false;
    zan_token_t a = lexer_peek_n(p, 1);
    /* `var (a, b)` is handled inside parse_var_decl; a paren-prefixed decon
     * needs `(` then a type keyword or a name. */
    bool type_kw = a.kind == TK_INT || a.kind == TK_LONG || a.kind == TK_SHORT ||
                   a.kind == TK_BYTE || a.kind == TK_UINT || a.kind == TK_ULONG ||
                   a.kind == TK_USHORT || a.kind == TK_SBYTE || a.kind == TK_FLOAT ||
                   a.kind == TK_DOUBLE || a.kind == TK_DECIMAL || a.kind == TK_BOOL ||
                   a.kind == TK_CHAR || a.kind == TK_STRING || a.kind == TK_OBJECT ||
                   a.kind == TK_NINT || a.kind == TK_VAR;
    if (a.kind != TK_IDENT && !type_kw) return false;
    /* `(Type name` / `(name` then a comma => more than one element => decon.
     * We must not consume tokens; peek two ahead is enough for the common
     * `(int a, ...)` / `(a, ...)` shapes. A single-element typed decon
     * `(int a) = rhs` is rare; the expression parser handles `(int)a` casts,
     * so leave it to the identifier path and require the comma. */
    zan_token_t b = lexer_peek_n(p, 2);
    if (b.kind != TK_IDENT) return false;
    zan_token_t c = lexer_peek_n(p, 3);
    return c.kind == TK_COMMA || c.kind == TK_RPAREN;
}

/* Parse the `(a, b) = rhs;` / `(int a, string b) = rhs;` statement body,
 * starting just after the `(`. Each element is either `name` (var/inferred)
 * or `Type name`. */
static zan_ast_node_t *parse_tuple_decon_body(zan_parser_t *p, zan_loc_t loc,
                                              zan_ast_node_t *type_prefix) {
    zan_ast_node_t *n = zan_ast_new(p->arena, AST_TUPLE_DECON, loc);
    zan_ast_list_init(&n->tuple_decon.names);
    zan_ast_list_init(&n->tuple_decon.types);

    /* `var (a, b)` reaches here with type_prefix being the "var" ref; skip it */
    if (type_prefix) {
        bool is_var = type_prefix->kind == AST_TYPE_REF &&
                      type_prefix->type_ref.name.len == 3 &&
                      memcmp(type_prefix->type_ref.name.str, "var", 3) == 0;
        if (!is_var) {
            /* `(int a` came through parse_var_decl after a plain type prefix —
             * not a shape this helper handles (a typed prefix belongs to a
             * regular var decl). */
            zan_diag_emit(p->diag, DIAG_ERROR, loc,
                          "unsupported tuple deconstruction form");
            return n;
        }
    }

    if (parser_check(p, TK_LPAREN)) parser_advance(p); /* ( */

    while (!parser_check(p, TK_RPAREN) && !parser_check(p, TK_EOF)) {
        /* element: `name` or `Type name`. A bare identifier is a type only
         * when something follows it that continues a type (`Type name`,
         * `T<...>`, `T[]`, `T?.x`); a `,` or `)` right after makes it a plain
         * variable name, as in `var (a, b)`. */
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

/* C# embedded statement: the body of if/else/while/for/foreach/do.
 * Either a block or a single statement; a single statement is wrapped in a
 * block so scoping and every consumer that assumes a block keep working.
 * As in C#, a declaration is not an embedded statement (CS1023) — it would
 * declare a variable nothing can reach. */
static zan_ast_node_t *parse_embedded_stmt(zan_parser_t *p) {
    if (parser_check(p, TK_LBRACE)) {
        return parse_block(p);
    }

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
    }
    if (p->current.loc.offset == before && !parser_check(p, TK_EOF)) {
        parser_advance(p);
    }
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
        /* heuristic: ident followed by ident is likely a type declaration */
        if (peek.kind == TK_IDENT) {
            return true;
        }
        /* `List<int> name` / `List<string>[] rows` declare a variable, but
         * `Stat<int>.s = 7;` and `Stat<int>.F();` are statements about a
         * generic instantiation. Treating every `Ident <` as a declaration
         * sent those to the declaration parser, which reported "expected
         * variable name" at the dot. Only a name after the matching `>` (past
         * any array rank) makes this a declaration. */
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
        /* qualified type decl: `A.B.C name` / `A.B<T> name` / `A.B[] name`.
         * Scan the raw source across the dotted chain, then check what
         * follows so `a.b.c()` / `a.b = x` stay expressions. */
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
                /* `A.B<T> name` declares a variable, but `db.Select<T>()` is
                 * a generic call: only a name after the matching `>` makes
                 * this a declaration. */
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
        /* `Ident? name` declares a nullable-typed variable, while `c ? a : b`
         * is a conditional expression. Only a name followed by `=` or `;`
         * after the `?` makes this a declaration -- a conditional's second
         * operand is followed by `:`. */
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
                /* `a ?? b`, `a?.b`, `a?[i]` are all expressions */
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
    case TK_LPAREN: {
        /* tuple-typed variable: `(int, int) t = rhs;` — scan tokens to the
         * matching `)` (the tuple type), then require an identifier (the
         * variable name). The scan is non-consuming (lexer_peek_n restores
         * state), so `(a, b) = rhs` / `(int a) = rhs` fall through. */
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

    /* if type was 'var' or 'let', type is NULL (inferred) */
    if (type && type->kind == AST_TYPE_REF &&
        type->type_ref.name.len == 3 &&
        memcmp(type->type_ref.name.str, "var", 3) == 0) {
        type = NULL;
    }

    /* `var (a, b) = rhs;` -- deconstruction declaration */
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
    zan_ast_node_t *then_body = parse_embedded_stmt(p);

    zan_ast_node_t *else_body = NULL;
    if (parser_match(p, TK_ELSE)) {
        if (parser_check(p, TK_IF)) {
            else_body = parse_if_stmt(p);
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

    zan_ast_node_t *body = parse_embedded_stmt(p);

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

/* True when the current token starts a switch type pattern: `case T x:` or
 * `case T:` — a type keyword, or a name followed by another name / `<` / `?` /
 * `[` (so `case Color.Red:` and `case SomeConst:` stay constant patterns).
 * Mirrors the variable-declaration heuristic in looks_like_var_decl. */
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
            parser_advance(p); /* case */
            /* `case T x:` / `case T:` — a type pattern starts with a type
             * keyword or a name followed by another name / `<` / `?` / `[`. */
            if (is_case_type_pattern(p)) {
                type_pattern = parse_type_ref(p);
                if (parser_check(p, TK_IDENT)) {
                    parser_advance(p);
                    var_name = p->previous.str_val;
                }
            } else {
                /* `case 5:` / `case Color.Red:` / `case null:` */
                pattern = parse_expression(p);
            }
            /* `case ... when guard:` — the guard runs after the pattern var
             * is bound, so it can reference it. */
            if (parser_match(p, TK_WHEN)) {
                when_cond = parse_expression(p);
            }
            parser_expect(p, TK_COLON);
        } else if (parser_check(p, TK_DEFAULT)) {
            parser_advance(p); /* default */
            if (parser_match(p, TK_WHEN)) {
                when_cond = parse_expression(p);
            }
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

/* Skip a balanced `<...>` generic-args group; p->lex is positioned just
 * before the `<` (the peek that detected it was non-consuming, so the first
 * token read here is the `<` itself). Returns false when the group runs off
 * the end of the statement (EOF or `;` first) -- e.g. a comparison `a < b` --
 * in which case the caller restores the lexer. */
static bool skip_angle_group(zan_parser_t *p) {
    int depth = 0;
    for (;;) {
        zan_token_t gt = zan_lexer_next(p->lex);
        if (gt.kind == TK_LESS) depth++;
        else if (gt.kind == TK_GREATER) depth--;
        else if (gt.kind == TK_EOF || gt.kind == TK_SEMICOLON) return false;
        if (depth <= 0) return true;
    }
}

/* Detect a local function declaration at statement position:
 * `R Name(params) { body }` or `R Name(params) => expr;`. The return type
 * may be a builtin keyword (`int`), a plain or dotted identifier (`Vec3`,
 * `A.B`), a generic (`List<int>`), or carry an array suffix (`int[]`); the
 * name may itself carry generic params (`T Identity<T>(T x)`). The decision
 * is structural: after the type and the name comes `(`, and the matching `)`
 * is followed by `{` or `=>` -- a call (`Foo(x);`, `a.b.C(x);`) or a
 * declaration with an initializer (`Foo x = ...;`) ends differently. The
 * lexer is restored on every path. */
static bool looks_like_local_func(zan_parser_t *p) {
    zan_lexer_t saved = *p->lex;

    /* The lexer sits past p->current, so the first type token IS p->current
     * and every later token comes from zan_lexer_next/peek. */
    zan_token_t t = p->current;

    /* the return type */
    switch (t.kind) {
    case TK_INT: case TK_LONG: case TK_SHORT: case TK_BYTE:
    case TK_UINT: case TK_ULONG: case TK_USHORT: case TK_SBYTE:
    case TK_FLOAT: case TK_DOUBLE: case TK_DECIMAL: case TK_BOOL: case TK_CHAR:
    case TK_STRING: case TK_VOID: case TK_OBJECT: case TK_NINT:
        /* type keyword: `t` is the type; the lexer's next token is the name */
        break;
    case TK_IDENT: {
        /* identifier type: `t` is the first ident; a dotted chain (`A.B`)
         * and/or generic args (`List<int>`) extend the type */
        for (;;) {
            zan_token_t after = zan_lexer_peek(p->lex);
            if (after.kind == TK_DOT) {
                zan_lexer_next(p->lex);      /* consume `.` */
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

    /* array suffixes on the return type: `int[] F(`, `Foo[][] F(`. The `[`
     * is still ahead (peeked, not consumed), so the first token read here is
     * the `[` itself. */
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

    /* the name */
    if (zan_lexer_next(p->lex).kind != TK_IDENT) { *p->lex = saved; return false; }

    /* the name's own generic params: `Identity<T>(` */
    if (zan_lexer_peek(p->lex).kind == TK_LESS) {
        if (!skip_angle_group(p)) { *p->lex = saved; return false; }
    }

    /* the parameter list must open with `(` */
    if (zan_lexer_next(p->lex).kind != TK_LPAREN) { *p->lex = saved; return false; }

    /* scan to the matching `)` of the parameter list; a `;` or EOF before it
     * means this is a call or a var decl, not a local function */
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

    /* after the parameter list must come a block body or `=> expr;` */
    zan_token_t tail = zan_lexer_next(p->lex);
    *p->lex = saved;
    return tail.kind == TK_LBRACE || tail.kind == TK_ARROW;
}

/* Parse a local function declaration and hoist it into the enclosing type as
 * a private static method. The statement itself produces nothing (a caller
 * cannot capture the definition site), but its name becomes a member so bare
 * `Name(args)` calls inside the enclosing method resolve to it. */
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

    /* optional generic type params: `T Identity<T>(T x)` */
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

    /* parameters */
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

    /* body: block, or `=> expr;` */
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
    zan_ast_list_init(&n->method_decl.where_clauses);
    n->method_decl.body = body;
    n->method_decl.modifiers = MOD_PRIVATE | MOD_STATIC;
    n->method_decl.extern_lib = (zan_istr_t){NULL, 0};
    n->method_decl.entry_point = (zan_istr_t){NULL, 0};
    n->method_decl.has_base_init = false;
    n->method_decl.has_this_init = false;
    zan_ast_list_push(&p->pending_members, n, p->arena);

    /* the declaration statement itself has no runtime effect */
    zan_ast_node_t *noop = zan_ast_new(p->arena, AST_BLOCK, loc);
    zan_ast_list_init(&noop->block.stmts);
    return noop;
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

    /* `checked { ... }` / `unchecked { ... }` statement block -- accepted
     * for C# compatibility, lowered to a plain block (no-op). Contextual, so
     * identifiers named `checked` keep working as variables/parameters. */
    if (is_checked_use(p) && zan_lexer_peek(p->lex).kind == TK_LBRACE) {
        zan_loc_t loc = p->current.loc;
        parser_advance(p); /* checked | unchecked */
        zan_ast_node_t *n = parse_block(p);
        n->loc = loc;
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
        /* `throw;` (C# rethrow): no operand -- the enclosing catch's exception
         * travels on unchanged, keeping its original dynamic type */
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
    case TK_USING: {
        /* `using (expr) body` / `using (Type name = expr) body` -- deterministic
         * disposal. Lowered to try/finally + a Dispose() call on the resource:
         *   using (R r = new R()) { body }  ->
         *     { R r = new R(); try { body } finally { r.Dispose(); } }
         * A bare expression gets a synthetic temp to hold the resource. */
        zan_loc_t loc = p->current.loc;
        parser_advance(p); /* using */
        parser_expect(p, TK_LPAREN);

        zan_istr_t name = {NULL, 0};
        zan_ast_node_t *type = NULL;
        zan_ast_node_t *init = NULL;

        if (looks_like_var_decl(p)) {
            /* using (Type name = expr) -- parse the declaration inline; the
             * closing `)` ends it, not a semicolon. */
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
            /* using (expr) — evaluate into a synthetic local */
            init = parse_expression(p);
            char buf[32];
            snprintf(buf, sizeof buf, "__using%d", p->synth_counter++);
            name = (zan_istr_t){ zan_arena_strdup(p->arena, buf, (int)strlen(buf)),
                                 (uint32_t)strlen(buf) };
        }
        parser_expect(p, TK_RPAREN);
        zan_ast_node_t *body = parse_embedded_stmt(p);

        /* build: { <decl>; try { body } finally { name.Dispose(); } } */
        zan_ast_node_t *outer = zan_ast_new(p->arena, AST_BLOCK, loc);
        zan_ast_list_init(&outer->block.stmts);

        if (type || !init) {
            /* `using (Type name = expr)`: type carries the declared type; if
             * the declared form had no initializer (unusual) the var decl is
             * still pushed so the name binds in the body scope. */
            zan_ast_node_t *decl = zan_ast_new(p->arena, AST_VAR_DECL, loc);
            decl->var_decl.name = name;
            decl->var_decl.type = type; /* NULL when `using (var x = ...)` */
            decl->var_decl.initializer = init;
            decl->var_decl.is_const = false;
            decl->var_decl.is_let = false;
            zan_ast_list_push(&outer->block.stmts, decl, p->arena);
        } else if (init) {
            /* bare `using (expr)`: temp var of inferred type */
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

    /* local function: `R Name(params) { body }` declared inside a method body.
     * It is hoisted to the enclosing type as a private static method (the
     * statement itself is a no-op), so a bare `Name(args)` call inside the
     * method resolves through the normal same-class member lookup. Detected
     * before the variable-declaration check because `int F(...)` and
     * `int x = ...` start the same way (type, name) -- the `(` after the name
     * is what makes it a function. */
    if (looks_like_local_func(p)) {
        return parse_local_func(p);
    }

    /* typed deconstruction: `(int a, string b) = rhs;` */
    if (looks_like_decon_decl(p)) {
        zan_loc_t dloc = p->current.loc;
        return parse_tuple_decon_body(p, dloc, NULL);
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

    /* contextual `params` modifier: `params T[] rest`. The declared type stays
     * `T[]` as written, like C#: the bundle is an array, the callee reads
     * `rest.Length` and can hand it on to any `T[]` parameter. (It used to be
     * rewritten to List<T> because array parameters carried no length; that
     * gap is gone, and the rewrite left every params callee reading `.Count`.) */
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

static void parse_attr_usages(zan_parser_t *p, zan_ast_list_t *out,
                              zan_istr_t *out_lib, zan_istr_t *out_entry) {
    /* Parse zero or more `[A, B(...)]` attribute groups; append each as an
     * AST_ATTRIBUTE (name = last dotted segment; args = positional expressions
     * and AST_ASSIGNMENT nodes for `Name = value`). Retained on the following
     * declaration for the compile-time attribute evaluator / routegen.
     * `[DllImport(...)]` is also decoded into *out_lib / *out_entry. */
    while (parser_check(p, TK_LBRACKET)) {
        parser_advance(p); /* [ */
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
                        parser_advance(p); /* ident */
                        parser_advance(p); /* = */
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
                                               zan_istr_t dll_entry_point);

/* Synthesize the accessor method of a property with a custom body. `name` is
 * the mangled method name (`get_Prop` / `set_Prop`), `ret_type` the property's
 * type (NULL for a setter), `value_type` the setter's incoming `value` type
 * (NULL for a getter). The body is the parsed accessor block. The resulting
 * AST_METHOD_DECL joins the type's members via p->pending_members so it flows
 * through nsresolve/binder/irgen exactly like a handwritten method. */
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
    zan_ast_list_init(&n->method_decl.where_clauses);
    if (value_type) {
        zan_ast_node_t *param = zan_ast_new(p->arena, AST_PARAM, loc);
        /* the setter parameter is named `value`, C#-style */
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
    parse_attr_usages(p, &attrs, &dll_import_lib, &dll_entry_point);
    zan_ast_node_t *n = parse_member_decl_inner(p, dll_import_lib, dll_entry_point);
    if (n) n->attributes = attrs;
    return n;
}

static zan_ast_node_t *parse_member_decl_inner(zan_parser_t *p,
                                               zan_istr_t dll_import_lib,
                                               zan_istr_t dll_entry_point) {
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

    /* `partial` is contextual here too: only a modifier right before a type
     * keyword (nested `partial class` parts merge with their top-level
     * parts once the nested types are hoisted to unit level). */
    if (parser_check(p, TK_IDENT) && p->current.str_val.len == 7 &&
        memcmp(p->current.str_val.str, "partial", 7) == 0) {
        zan_token_kind_t nk = zan_lexer_peek(p->lex).kind;
        if (nk == TK_CLASS || nk == TK_STRUCT || nk == TK_INTERFACE) {
            parser_advance(p);
            mods |= MOD_PARTIAL;
        }
    }

    /* nested type declaration: `[mods] class|struct|interface|enum Name {...}`
     * inside a class body. The member modifier parser has already consumed
     * `static`/`sealed`/... so the remaining token is the type keyword. */
    if (parser_check(p, TK_CLASS) || parser_check(p, TK_STRUCT) ||
        parser_check(p, TK_INTERFACE) || parser_check(p, TK_ENUM)) {
        return parse_type_decl(p, mods);
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

    /* user-defined conversion operator:
     * `[mods] implicit operator T2(T1 v) { }` / `explicit operator T2(T1 v)`.
     * `implicit`/`explicit` are contextual keywords: only here, where a
     * member's return type would be, do they introduce a conversion, so an
     * identifier named `implicit`/`explicit` elsewhere keeps working. The
     * member lowers to a static `op_implicit`/`op_explicit` method that the
     * cast/assignment sites call through the normal operator protocol. */
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
        parser_advance(p); /* implicit / explicit */
        parser_advance(p); /* operator */
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
            (zan_istr_t){ (char *)conv_name, 11 }; /* both are 11 chars */
        cn->method_decl.return_type = conv_ret;
        cn->method_decl.params = conv_params;
        zan_ast_list_init(&cn->method_decl.type_params);
        zan_ast_list_init(&cn->method_decl.where_clauses);
        cn->method_decl.body = conv_body;
        cn->method_decl.modifiers = mods | MOD_STATIC;
        cn->method_decl.extern_lib = (zan_istr_t){NULL, 0};
        cn->method_decl.entry_point = (zan_istr_t){NULL, 0};
        return cn;
    }
ordinary_member:
    /* C17 requires a statement after a label, not a declaration. */
    ;
    /* type or identifier */
    zan_ast_node_t *type = parse_type_ref(p);

    /* constructor: ClassName(params) [: base(args)] { } */
    if (parser_check(p, TK_LPAREN)) {
        zan_istr_t name = type->type_ref.name;
        zan_ast_list_t params = parse_param_list(p);

        /* optional base/this initializer: : base(...) or : this(...) */
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
        n->method_decl.base_args = base_args;
        n->method_decl.has_base_init = has_base_init;
        n->method_decl.has_this_init = has_this_init;
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

    /* indexer: type this [params] { get ... set ... } / => expr;
     * C#-style indexers lower to the existing op_index/op_index_set protocol:
     * the synthesized methods are instance (non-static) members whose receiver
     * is `this`, matching the irgen call sites that pass the indexed object as
     * the first argument. The accessor bodies keep their C# shape (they may
     * touch `this` fields and the `value` parameter directly). */
    if (parser_check(p, TK_THIS)) {
        parser_advance(p); /* this */
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
            /* expression-bodied indexer: type this[i] => expr; */
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
            parser_advance(p); /* { */
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
                    parser_advance(p); /* skip unknown accessor token */
                }
            }
            parser_expect(p, TK_RBRACE);
        } else {
            parser_expect(p, TK_LBRACE);
        }

        zan_ast_node_t *n = zan_ast_new(p->arena, AST_PROPERTY_DECL, loc);
        zan_istr_t iname = {(char *)"Item", 4}; /* .NET-style indexer name */
        n->field_decl.name = iname;
        n->field_decl.type = type;
        n->field_decl.initializer = NULL;
        n->field_decl.modifiers = mods;
        n->field_decl.getter_body = getter_body;
        n->field_decl.setter_body = setter_body;
        n->field_decl.has_getter = has_getter;
        n->field_decl.has_setter = has_setter;
        n->field_decl.has_init = has_init;

        /* Synthesize instance op_index(index...) / op_index_set(index..., value)
         * methods. A custom getter body needs a real method for `obj[i]` to
         * call; an automatic accessor (no body) is skipped -- the caller falls
         * back to the plain array/list slot path, which is the C# automatic
         * indexer's default backing behavior for element types. */
        if (getter_body) {
            zan_ast_node_t *g = zan_ast_new(p->arena, AST_METHOD_DECL, loc);
            zan_istr_t gistr = {(char *)"op_index", 8};
            g->method_decl.name = gistr;
            g->method_decl.return_type = type;
            g->method_decl.params = idx_params;
            zan_ast_list_init(&g->method_decl.type_params);
            zan_ast_list_init(&g->method_decl.where_clauses);
            g->method_decl.body = getter_body;
            g->method_decl.modifiers = mods; /* instance, receiver is `this` */
            g->method_decl.extern_lib = (zan_istr_t){NULL, 0};
            g->method_decl.entry_point = (zan_istr_t){NULL, 0};
            zan_ast_list_push(&p->pending_members, g, p->arena);
        }
        if (setter_body) {
            zan_ast_node_t *s = zan_ast_new(p->arena, AST_METHOD_DECL, loc);
            zan_istr_t sistr = {(char *)"op_index_set", 12};
            s->method_decl.name = sistr;
            s->method_decl.return_type = NULL; /* void */
            s->method_decl.params = idx_params;
            zan_ast_list_init(&s->method_decl.type_params);
            zan_ast_list_init(&s->method_decl.where_clauses);
            zan_ast_node_t *vp = zan_ast_new(p->arena, AST_PARAM, loc);
            zan_istr_t vn = {(char *)"value", 5};
            vp->param.name = vn;
            vp->param.type = type;
            vp->param.is_this = false;
            zan_ast_list_push(&s->method_decl.params, vp, p->arena);
            s->method_decl.body = setter_body;
            s->method_decl.modifiers = mods;
            s->method_decl.extern_lib = (zan_istr_t){NULL, 0};
            s->method_decl.entry_point = (zan_istr_t){NULL, 0};
            zan_ast_list_push(&p->pending_members, s, p->arena);
        }
        return n;
    }

    /* method or field: need name next */
    if (!parser_check(p, TK_IDENT)) {
        zan_diag_emit(p->diag, DIAG_ERROR, p->current.loc, "expected member name");
        return parser_error_node(p);
    }
    parser_advance(p);
    zan_istr_t name = p->previous.str_val;

    /* expression-bodied property: type Name => expr;
     * A get-only property with an expression body. The body is stored on the
     * property node and a synthesized `get_<name>` method is queued, so a bare
     * read `a.Name` (no parentheses) lowers to the accessor call. */
    if (parser_check(p, TK_ARROW)) {
        parser_advance(p); /* => */
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
                    parser_advance(p); /* skip unknown accessor token */
                }
            }
            parser_expect(p, TK_RBRACE);

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
            n->field_decl.getter_body = getter_body;
            n->field_decl.setter_body = setter_body;
            n->field_decl.has_getter = has_getter;
            n->field_decl.has_setter = has_setter;
            n->field_decl.has_init = has_init;

            /* A custom getter/setter body needs a real method to call on read
             * `a.Name` / write `a.Name = v`: synthesize `get_<name>` /
             * `set_<name>` and queue them right after this member. */
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

    /* field: type name [= initializer] {, name [= initializer]} ;
     * C#-style comma declarators: each name becomes its own field sharing the
     * declared type and modifiers. Extra declarators queue through
     * pending_members so the class body keeps declaration order. */
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
            /* C# variance annotation: `interface I<out T>`, `I<in T>`.
             * Accepted and consumed (the type system stays invariant);
             * the variance marker is not recorded. */
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
            /* Property accessors with custom bodies queue synthesized
             * get_<name>/set_<name> methods; drain them into the member list
             * so they participate in nsresolve/binder/irgen like handwritten
             * methods. */
            for (int pi = 0; pi < p->pending_members.count; pi++) {
                zan_ast_list_push(&members, p->pending_members.items[pi],
                                  p->arena);
            }
            p->pending_members.count = 0;
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

        /* Block-scoped `namespace X { ... }`: parse its members now. Each
         * member lands in comp_unit.decls alongside file-scoped declarations
         * and carries the same ns_name/ns_usings once stamping runs -- that
         * is how both namespace spellings were always treated downstream,
         * because the stamping pass keys off the unit's single ns slot and
         * the binder only ever saw comp_unit.decls. Previously the decls
         * loop that follows stopped at this block's `}`, so every top-level
         * declaration after the namespace block was silently dropped; the
         * member loop here consumes the block body (including its `}`) and
         * lets parsing fall through to the same decls loop, which keeps
         * parsing declarations after the block. The unit keeps a single ns
         * slot, so a *later* namespace header would join the block's name;
         * Zan sources declare at most one namespace per file (C#'s
         * convention) and the whole corpus uses the file-scoped spelling. */
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

    /* type declarations */
    while (!parser_check(p, TK_EOF) && !parser_check(p, TK_RBRACE)) {
        if (!parse_top_level_decl(p, unit))
            parser_advance(p); /* skip to recover */
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

/* Bounded append into the synthetic-source buffers (tref_write,
 * gen_event_holder, gen_record_class). `*off` saturates at cap once the
 * buffer is full, so cap - *off can never underflow to a huge size_t (which
 * would let snprintf overflow the stack buffer). Callers pass `&n` as off and
 * read `n` back afterwards; never write `n += zsrc_append(...)` here -- the
 * function already updates n through the pointer, and mixing the two (the old
 * `n += f(&n)` form) is unspecified evaluation order that double-counts on
 * some compilers (e.g. MinGW GCC), leaving gaps of uninitialized stack bytes
 * in the generated source. */
/* Capacity of the synthetic-source buffers. Generated text scales with the
 * declaration (a record's fields, a delegate's parameters), so this is sized
 * far above any plausible declaration and an overflow is reported as an error
 * instead of feeding truncated source to the lexer. */
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
    /* remembered so `op_call` (raising the event as `E(...)`) can forward the
     * same parameter list to Invoke */
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

    /* `E(args)` raises the event, as in C#. The holder is null until the first
     * subscription, and a call with no subscribers simply does nothing (the
     * `E?.Invoke(...)` shape C# code writes by hand). */
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

/* Lower `record Name(T1 A, T2 B);` into a class with public fields, a
 * positional constructor, field-wise op_eq/op_neq value equality, and a
 * C#-style ToString. */
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
    zsrc_append(src, cap, &n,
        "    }\n"
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

/* Hoist one nested type declaration out of `type_node`'s member list into the
 * compilation-unit top level, recursing into deeper nesting first so the
 * whole subtree is lifted. Returns the number of types hoisted. */
static int hoist_nested_types(zan_ast_node_t *unit, zan_ast_node_t *type_node,
                              zan_ast_list_t *decls, zan_arena_t *arena) {
    int hoisted = 0;
    zan_ast_list_t *members = &type_node->type_decl.members;
    int m = members->count;
    int mi = 0;
    while (mi < m) {
        zan_ast_node_t *mem = members->items[mi];
        if (mem->kind == AST_CLASS_DECL || mem->kind == AST_STRUCT_DECL ||
            mem->kind == AST_INTERFACE_DECL || mem->kind == AST_ENUM_DECL) {
            hoisted += hoist_nested_types(unit, mem, decls, arena);
            zan_ast_list_push(decls, mem, arena);
            hoisted++;
            /* remove from the member list (order-preserving) */
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
    /* Holder classes generated so far, grown on demand: a fixed cap used to
     * stop generating them silently, and every later `event` of a new
     * delegate type then referenced a class that was never emitted. */
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
