/* main.c -- Zan compiler entry point.
 *
 * Usage: zanc <source.zan> [-o output] [--dump-tokens] [--dump-ast]
 */

#include "zan.h"
#include "arena.h"
#include "diag.h"
#include "lexer.h"
#include "parser.h"
#include "ast.h"
#include "binder.h"
#include "checker.h"
#include "irgen.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- file reading ---- */

static char *read_file(const char *path, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "error: cannot open file '%s'\n", path);
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = (char *)malloc((size_t)len + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    size_t read = fread(buf, 1, (size_t)len, f);
    buf[read] = '\0';
    fclose(f);
    if (out_len) *out_len = read;
    return buf;
}

/* ---- dump tokens ---- */

static void dump_tokens(const char *source, size_t len, const char *filename) {
    zan_arena_t *arena = zan_arena_new();
    zan_diag_t *diag = zan_diag_new(arena);
    zan_diag_add_file(diag, filename, source);

    zan_lexer_t lex;
    zan_lexer_init(&lex, source, len, 0, arena, diag);

    for (;;) {
        zan_token_t tok = zan_lexer_next(&lex);
        if (tok.kind == TK_EOF) break;

        printf("%4u:%2u  %-20s", tok.loc.line, tok.loc.col,
               zan_token_kind_name(tok.kind));

        switch (tok.kind) {
        case TK_INT_LIT:
            printf("  %lld", (long long)tok.int_val);
            break;
        case TK_FLOAT_LIT:
            printf("  %g", tok.float_val);
            break;
        case TK_STRING_LIT:
        case TK_IDENT:
            printf("  \"%.*s\"", tok.str_val.len, tok.str_val.str);
            break;
        case TK_CHAR_LIT:
            printf("  '%c'", (char)tok.int_val);
            break;
        default:
            break;
        }
        printf("\n");
    }

    zan_arena_free(arena);
}

/* ---- dump AST (simple indented print) ---- */

static void indent(int depth) {
    for (int i = 0; i < depth; i++) printf("  ");
}

static void dump_ast_node(zan_ast_node_t *node, int depth);

static void dump_list(const char *label, zan_ast_list_t *list, int depth) {
    if (list->count == 0) return;
    indent(depth);
    printf("%s (%d):\n", label, list->count);
    for (int i = 0; i < list->count; i++) {
        dump_ast_node(list->items[i], depth + 1);
    }
}

static void dump_ast_node(zan_ast_node_t *node, int depth) {
    if (!node) return;
    indent(depth);

    switch (node->kind) {
    case AST_COMPILATION_UNIT:
        printf("CompilationUnit\n");
        dump_list("usings", &node->comp_unit.usings, depth + 1);
        if (node->comp_unit.ns) {
            indent(depth + 1);
            printf("namespace:\n");
            dump_ast_node(node->comp_unit.ns, depth + 2);
        }
        dump_list("declarations", &node->comp_unit.decls, depth + 1);
        break;

    case AST_USING_DECL:
        printf("UsingDecl%s\n", node->using_decl.is_static ? " (static)" : "");
        dump_ast_node(node->using_decl.name, depth + 1);
        break;

    case AST_NAMESPACE_DECL:
        printf("NamespaceDecl%s\n",
               node->namespace_decl.is_file_scoped ? " (file-scoped)" : "");
        dump_ast_node(node->namespace_decl.name, depth + 1);
        break;

    case AST_CLASS_DECL:
    case AST_STRUCT_DECL:
    case AST_INTERFACE_DECL:
    case AST_ENUM_DECL:
        printf("%s '%.*s' (mods=0x%x)\n",
               node->kind == AST_CLASS_DECL ? "ClassDecl" :
               node->kind == AST_STRUCT_DECL ? "StructDecl" :
               node->kind == AST_INTERFACE_DECL ? "InterfaceDecl" : "EnumDecl",
               node->type_decl.name.len, node->type_decl.name.str,
               node->type_decl.modifiers);
        dump_list("type_params", &node->type_decl.type_params, depth + 1);
        dump_list("bases", &node->type_decl.bases, depth + 1);
        dump_list("members", &node->type_decl.members, depth + 1);
        break;

    case AST_METHOD_DECL:
    case AST_CONSTRUCTOR_DECL:
    case AST_DESTRUCTOR_DECL:
        printf("%s '%.*s' (mods=0x%x)\n",
               node->kind == AST_METHOD_DECL ? "MethodDecl" :
               node->kind == AST_CONSTRUCTOR_DECL ? "ConstructorDecl" : "DestructorDecl",
               node->method_decl.name.len, node->method_decl.name.str,
               node->method_decl.modifiers);
        if (node->method_decl.return_type) {
            indent(depth + 1);
            printf("return_type:\n");
            dump_ast_node(node->method_decl.return_type, depth + 2);
        }
        dump_list("params", &node->method_decl.params, depth + 1);
        if (node->method_decl.body) {
            indent(depth + 1);
            printf("body:\n");
            dump_ast_node(node->method_decl.body, depth + 2);
        }
        break;

    case AST_PROPERTY_DECL:
        printf("PropertyDecl '%.*s' (mods=0x%x)\n",
               node->field_decl.name.len, node->field_decl.name.str,
               node->field_decl.modifiers);
        if (node->field_decl.type) {
            indent(depth + 1);
            printf("type:\n");
            dump_ast_node(node->field_decl.type, depth + 2);
        }
        if (node->field_decl.initializer) {
            indent(depth + 1);
            printf("default:\n");
            dump_ast_node(node->field_decl.initializer, depth + 2);
        }
        break;

    case AST_FIELD_DECL:
        printf("FieldDecl '%.*s' (mods=0x%x)\n",
               node->field_decl.name.len, node->field_decl.name.str,
               node->field_decl.modifiers);
        if (node->field_decl.type) {
            indent(depth + 1);
            printf("type:\n");
            dump_ast_node(node->field_decl.type, depth + 2);
        }
        if (node->field_decl.initializer) {
            indent(depth + 1);
            printf("init:\n");
            dump_ast_node(node->field_decl.initializer, depth + 2);
        }
        break;

    case AST_PARAM:
        printf("Param '%.*s'\n", node->param.name.len, node->param.name.str);
        if (node->param.type) {
            dump_ast_node(node->param.type, depth + 1);
        }
        break;

    case AST_BLOCK:
        printf("Block\n");
        dump_list("stmts", &node->block.stmts, depth + 1);
        break;

    case AST_VAR_DECL:
        printf("VarDecl '%.*s'%s%s\n",
               node->var_decl.name.len, node->var_decl.name.str,
               node->var_decl.is_const ? " const" : "",
               node->var_decl.is_let ? " let" : "");
        if (node->var_decl.type) {
            indent(depth + 1);
            printf("type:\n");
            dump_ast_node(node->var_decl.type, depth + 2);
        }
        if (node->var_decl.initializer) {
            indent(depth + 1);
            printf("init:\n");
            dump_ast_node(node->var_decl.initializer, depth + 2);
        }
        break;

    case AST_RETURN_STMT:
        printf("ReturnStmt\n");
        if (node->ret.value) {
            dump_ast_node(node->ret.value, depth + 1);
        }
        break;

    case AST_IF_STMT:
        printf("IfStmt\n");
        indent(depth + 1); printf("cond:\n");
        dump_ast_node(node->if_stmt.cond, depth + 2);
        indent(depth + 1); printf("then:\n");
        dump_ast_node(node->if_stmt.then_body, depth + 2);
        if (node->if_stmt.else_body) {
            indent(depth + 1); printf("else:\n");
            dump_ast_node(node->if_stmt.else_body, depth + 2);
        }
        break;

    case AST_WHILE_STMT:
    case AST_DO_WHILE_STMT:
        printf("%s\n", node->kind == AST_WHILE_STMT ? "WhileStmt" : "DoWhileStmt");
        indent(depth + 1); printf("cond:\n");
        dump_ast_node(node->while_stmt.cond, depth + 2);
        indent(depth + 1); printf("body:\n");
        dump_ast_node(node->while_stmt.body, depth + 2);
        break;

    case AST_FOR_STMT:
        printf("ForStmt\n");
        if (node->for_stmt.init) {
            indent(depth + 1); printf("init:\n");
            dump_ast_node(node->for_stmt.init, depth + 2);
        }
        if (node->for_stmt.cond) {
            indent(depth + 1); printf("cond:\n");
            dump_ast_node(node->for_stmt.cond, depth + 2);
        }
        if (node->for_stmt.step) {
            indent(depth + 1); printf("step:\n");
            dump_ast_node(node->for_stmt.step, depth + 2);
        }
        indent(depth + 1); printf("body:\n");
        dump_ast_node(node->for_stmt.body, depth + 2);
        break;

    case AST_FOREACH_STMT:
        printf("ForeachStmt '%.*s'\n",
               node->foreach_stmt.var_name.len, node->foreach_stmt.var_name.str);
        indent(depth + 1); printf("collection:\n");
        dump_ast_node(node->foreach_stmt.collection, depth + 2);
        indent(depth + 1); printf("body:\n");
        dump_ast_node(node->foreach_stmt.body, depth + 2);
        break;

    case AST_BREAK_STMT:
        printf("BreakStmt\n");
        break;

    case AST_CONTINUE_STMT:
        printf("ContinueStmt\n");
        break;

    case AST_EXPR_STMT:
        printf("ExprStmt\n");
        dump_ast_node(node->expr_stmt.expr, depth + 1);
        break;

    case AST_BINARY:
        printf("Binary '%s'\n", zan_token_kind_name(node->binary.op));
        dump_ast_node(node->binary.left, depth + 1);
        dump_ast_node(node->binary.right, depth + 1);
        break;

    case AST_UNARY:
        printf("Unary '%s'\n", zan_token_kind_name(node->unary.op));
        dump_ast_node(node->unary.operand, depth + 1);
        break;

    case AST_POSTFIX_UNARY:
        printf("PostfixUnary '%s'\n", zan_token_kind_name(node->unary.op));
        dump_ast_node(node->unary.operand, depth + 1);
        break;

    case AST_ASSIGNMENT:
        printf("Assignment '%s'\n", zan_token_kind_name(node->binary.op));
        dump_ast_node(node->binary.left, depth + 1);
        dump_ast_node(node->binary.right, depth + 1);
        break;

    case AST_CALL:
        printf("Call\n");
        indent(depth + 1); printf("callee:\n");
        dump_ast_node(node->call.callee, depth + 2);
        dump_list("args", &node->call.args, depth + 1);
        break;

    case AST_MEMBER_ACCESS:
        printf("MemberAccess '%.*s'\n", node->member.name.len, node->member.name.str);
        dump_ast_node(node->member.object, depth + 1);
        break;

    case AST_INDEX:
        printf("Index\n");
        dump_ast_node(node->index.object, depth + 1);
        dump_ast_node(node->index.index, depth + 1);
        break;

    case AST_NEW_EXPR:
        printf("NewExpr\n");
        dump_ast_node(node->new_expr.type, depth + 1);
        dump_list("args", &node->new_expr.args, depth + 1);
        break;

    case AST_CONDITIONAL:
        printf("Conditional\n");
        dump_ast_node(node->conditional.cond, depth + 1);
        dump_ast_node(node->conditional.then_expr, depth + 1);
        dump_ast_node(node->conditional.else_expr, depth + 1);
        break;

    case AST_INT_LITERAL:
        printf("IntLiteral %lld\n", (long long)node->int_val);
        break;
    case AST_FLOAT_LITERAL:
        printf("FloatLiteral %g\n", node->float_val);
        break;
    case AST_STRING_LITERAL:
        printf("StringLiteral \"%.*s\"\n", node->str_val.len, node->str_val.str);
        break;
    case AST_CHAR_LITERAL:
        printf("CharLiteral '%c'\n", (char)node->int_val);
        break;
    case AST_BOOL_LITERAL:
        printf("BoolLiteral %s\n", node->bool_val ? "true" : "false");
        break;
    case AST_NULL_LITERAL:
        printf("NullLiteral\n");
        break;
    case AST_THIS_EXPR:
        printf("This\n");
        break;
    case AST_BASE_EXPR:
        printf("Base\n");
        break;

    case AST_IDENTIFIER:
        printf("Identifier '%.*s'\n", node->ident.name.len, node->ident.name.str);
        break;

    case AST_QUALIFIED_NAME:
        printf("QualifiedName ");
        for (int i = 0; i < node->qualified_name.parts.count; i++) {
            if (i > 0) printf(".");
            zan_ast_node_t *part = node->qualified_name.parts.items[i];
            printf("%.*s", part->ident.name.len, part->ident.name.str);
        }
        printf("\n");
        break;

    case AST_TYPE_REF:
        printf("TypeRef '%.*s'%s%s",
               node->type_ref.name.len, node->type_ref.name.str,
               node->type_ref.is_nullable ? "?" : "",
               node->type_ref.is_array ? "[]" : "");
        if (node->type_ref.type_args.count > 0) {
            printf("<");
            for (int i = 0; i < node->type_ref.type_args.count; i++) {
                if (i > 0) printf(", ");
                zan_ast_node_t *ta = node->type_ref.type_args.items[i];
                printf("%.*s", ta->type_ref.name.len, ta->type_ref.name.str);
            }
            printf(">");
        }
        printf("\n");
        break;

    case AST_LAMBDA:
        printf("Lambda\n");
        dump_list("params", &node->lambda.params, depth + 1);
        indent(depth + 1); printf("body:\n");
        dump_ast_node(node->lambda.body, depth + 2);
        break;

    case AST_STRING_INTERP:
        printf("StringInterp (%d parts)\n", node->string_interp.parts.count);
        dump_list("parts", &node->string_interp.parts, depth + 1);
        break;

    case AST_ENUM_MEMBER:
        printf("EnumMember '%.*s'\n",
               node->enum_member.name.len, node->enum_member.name.str);
        if (node->enum_member.value) {
            dump_ast_node(node->enum_member.value, depth + 1);
        }
        break;

    default:
        printf("<%d>\n", node->kind);
        break;
    }
}

/* ---- main ---- */

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Zan Compiler v0.1.0\n");
        fprintf(stderr, "Usage: zanc <source.zan> [options]\n");
        fprintf(stderr, "Options:\n");
        fprintf(stderr, "  -o <output>     Output file\n");
        fprintf(stderr, "  --dump-tokens   Dump lexer tokens\n");
        fprintf(stderr, "  --dump-ast      Dump parse tree\n");
        fprintf(stderr, "  --emit-ir       Emit LLVM IR to stdout\n");
        return 1;
    }

    const char *input_file = NULL;
    const char *output_file = NULL;
    bool do_dump_tokens = false;
    bool do_dump_ast = false;
    bool do_emit_ir = false;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--dump-tokens") == 0) {
            do_dump_tokens = true;
        } else if (strcmp(argv[i], "--dump-ast") == 0) {
            do_dump_ast = true;
        } else if (strcmp(argv[i], "--emit-ir") == 0) {
            do_emit_ir = true;
        } else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            output_file = argv[++i];
        } else if (argv[i][0] != '-') {
            input_file = argv[i];
        } else {
            fprintf(stderr, "unknown option: %s\n", argv[i]);
            return 1;
        }
    }

    if (!input_file) {
        fprintf(stderr, "error: no input file\n");
        return 1;
    }

    /* read source */
    size_t source_len;
    char *source = read_file(input_file, &source_len);
    if (!source) return 1;

    /* dump tokens mode */
    if (do_dump_tokens) {
        dump_tokens(source, source_len, input_file);
        free(source);
        return 0;
    }

    /* parse */
    zan_arena_t *arena = zan_arena_new();
    zan_diag_t *diag = zan_diag_new(arena);
    zan_diag_add_file(diag, input_file, source);

    zan_lexer_t lex;
    zan_lexer_init(&lex, source, source_len, 0, arena, diag);

    zan_parser_t parser;
    zan_parser_init(&parser, &lex, arena, diag);

    zan_ast_node_t *ast = zan_parser_parse(&parser);

    if (do_dump_ast) {
        if (!zan_diag_has_errors(diag)) {
            dump_ast_node(ast, 0);
        }
    }

    if (zan_diag_has_errors(diag)) {
        fprintf(stderr, "\n%d error(s), %d warning(s)\n",
                diag->error_count, diag->warning_count);
        zan_arena_free(arena);
        free(source);
        return 1;
    }

    /* ---- bind ---- */
    zan_binder_t binder;
    zan_binder_init(&binder, arena, diag);
    zan_binder_bind(&binder, ast);

    /* ---- type check ---- */
    zan_checker_t checker;
    zan_checker_init(&checker, &binder, arena, diag);
    zan_checker_check(&checker, ast);

    if (do_dump_ast) {
        /* already printed */
    }

    if (zan_diag_has_errors(diag)) {
        fprintf(stderr, "\n%d error(s) after type checking\n",
                diag->error_count);
        zan_arena_free(arena);
        free(source);
        return 1;
    }

    /* ---- codegen ---- */
    zan_irgen_t irgen;
    if (zan_irgen_init(&irgen, arena, diag, &binder, input_file) != ZAN_OK) {
        fprintf(stderr, "error: failed to initialize code generator\n");
        zan_arena_free(arena);
        free(source);
        return 1;
    }

    if (zan_irgen_emit(&irgen, ast) != ZAN_OK) {
        fprintf(stderr, "error: code generation failed\n");
        zan_irgen_destroy(&irgen);
        zan_arena_free(arena);
        free(source);
        return 1;
    }

    if (do_emit_ir) {
        zan_irgen_write_ir(&irgen, NULL);
    } else {
        /* determine output path */
        char obj_path[1024];
        if (output_file) {
            snprintf(obj_path, sizeof(obj_path), "%s", output_file);
        } else {
            /* input.zan → input */
            size_t ilen = strlen(input_file);
            if (ilen > 4 && strcmp(input_file + ilen - 4, ".zan") == 0) {
                snprintf(obj_path, sizeof(obj_path), "%.*s", (int)(ilen - 4), input_file);
            } else {
                snprintf(obj_path, sizeof(obj_path), "%s.out", input_file);
            }
        }

        /* emit object file */
        char obj_tmp[1024];
        snprintf(obj_tmp, sizeof(obj_tmp), "%s.o", obj_path);
        if (zan_irgen_write_obj(&irgen, obj_tmp) != ZAN_OK) {
            fprintf(stderr, "error: failed to emit object file\n");
            zan_irgen_destroy(&irgen);
            zan_arena_free(arena);
            free(source);
            return 1;
        }

        /* link with system C compiler */
        char link_cmd[2048];
#ifdef _WIN32
        snprintf(link_cmd, sizeof(link_cmd), "clang \"%s\" -o \"%s\"",
                 obj_tmp, obj_path);
#else
        snprintf(link_cmd, sizeof(link_cmd), "cc \"%s\" -o \"%s\" -lm",
                 obj_tmp, obj_path);
#endif
        int link_ret = system(link_cmd);

        /* clean up object file */
        remove(obj_tmp);

        if (link_ret != 0) {
            fprintf(stderr, "error: linking failed\n");
            zan_irgen_destroy(&irgen);
            zan_arena_free(arena);
            free(source);
            return 1;
        }

        printf("Compiled '%s' → '%s'\n", input_file, obj_path);
    }

    zan_irgen_destroy(&irgen);
    zan_arena_free(arena);
    free(source);
    return 0;
}
