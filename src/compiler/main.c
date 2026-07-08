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
#include "optimizer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#include <process.h>
#else
#include <unistd.h>
#endif
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

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
        fprintf(stderr, "  --check-leaks   Report unreleased objects at program exit\n");
        fprintf(stderr, "  --no-runtime-checks  Disable runtime guards (e.g. division by zero)\n");
        fprintf(stderr, "  --publish        Build optimized release binary (strip debug, optimize)\n");
        fprintf(stderr, "  --stdlib-path <dir>  Path to stdlib directory\n");
        fprintf(stderr, "  --auto-stdlib    Automatically find and include stdlib .zan files\n");
        fprintf(stderr, "  -O0/-O1/-O2/-O3  Set optimization level (default: O0, --publish: O2)\n");
        fprintf(stderr, "  -D<name>[=value] Define preprocessor symbol\n");
        return 1;
    }

    const char *input_file = NULL;
    const char *input_files[128];
    int input_count = 0;
    const char *output_file = NULL;
    bool do_dump_tokens = false;
    bool do_dump_ast = false;
    bool do_emit_ir = false;
    bool check_leaks = false;
    bool runtime_checks = true;
    bool publish_mode = false;
    const char *stdlib_path = NULL;
    bool auto_stdlib = true;
    int opt_level = -1; /* -1 = auto (O0 default, O2 for publish) */
    const char *pp_defines[64];
    int pp_define_count = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--dump-tokens") == 0) {
            do_dump_tokens = true;
        } else if (strcmp(argv[i], "--dump-ast") == 0) {
            do_dump_ast = true;
        } else if (strcmp(argv[i], "--emit-ir") == 0) {
            do_emit_ir = true;
        } else if (strcmp(argv[i], "--check-leaks") == 0) {
            check_leaks = true;
        } else if (strcmp(argv[i], "--no-runtime-checks") == 0) {
            runtime_checks = false;
        } else if (strcmp(argv[i], "--publish") == 0) {
            publish_mode = true;
        } else if (strcmp(argv[i], "--stdlib-path") == 0 && i + 1 < argc) {
            stdlib_path = argv[++i];
        } else if (strcmp(argv[i], "--auto-stdlib") == 0) {
            auto_stdlib = true;
        } else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            output_file = argv[++i];
        } else if (strcmp(argv[i], "-O0") == 0) {
            opt_level = 0;
        } else if (strcmp(argv[i], "-O1") == 0) {
            opt_level = 1;
        } else if (strcmp(argv[i], "-O2") == 0) {
            opt_level = 2;
        } else if (strcmp(argv[i], "-O3") == 0) {
            opt_level = 3;
        } else if (strncmp(argv[i], "-D", 2) == 0 && argv[i][2] != '\0') {
            if (pp_define_count < 64) pp_defines[pp_define_count++] = argv[i] + 2;
        } else if (argv[i][0] != '-') {
            if (input_count < 128) {
                input_files[input_count++] = argv[i];
            } else {
                fprintf(stderr, "error: too many input files (max 128)\n");
                return 1;
            }
        } else {
            fprintf(stderr, "unknown option: %s\n", argv[i]);
            return 1;
        }
    }

    if (input_count == 0) {
        fprintf(stderr, "error: no input file\n");
        return 1;
    }
    input_file = input_files[0];

    /* --auto-stdlib: discover stdlib path relative to compiler executable,
     * then scan using directives in source files and add matching stdlib .zan files */
    if (auto_stdlib || stdlib_path) {
        /* determine stdlib root */
        char stdlib_root[1024];
        if (stdlib_path) {
            snprintf(stdlib_root, sizeof(stdlib_root), "%s", stdlib_path);
        } else {
            /* try: executable_dir/../stdlib/ or executable_dir/stdlib/ */
#ifdef _WIN32
            char exe_path[1024];
            GetModuleFileNameA(NULL, exe_path, sizeof(exe_path));
            char *last_sep = strrchr(exe_path, '\\');
            if (last_sep) *last_sep = '\0';
            snprintf(stdlib_root, sizeof(stdlib_root), "%s\\..\\stdlib", exe_path);
            /* check if it exists, fallback to sibling */
            {
                DWORD attr = GetFileAttributesA(stdlib_root);
                if (attr == INVALID_FILE_ATTRIBUTES || !(attr & FILE_ATTRIBUTE_DIRECTORY)) {
                    snprintf(stdlib_root, sizeof(stdlib_root), "%s\\stdlib", exe_path);
                }
            }
#elif defined(__APPLE__)
            /* macOS has no /proc; ask dyld for the executable path. */
            char exe_path[1024];
            uint32_t exe_sz = sizeof(exe_path);
            if (_NSGetExecutablePath(exe_path, &exe_sz) == 0) {
                char *last_sep = strrchr(exe_path, '/');
                if (last_sep) *last_sep = '\0';
                snprintf(stdlib_root, sizeof(stdlib_root), "%s/../stdlib", exe_path);
            } else {
                snprintf(stdlib_root, sizeof(stdlib_root), "stdlib");
            }
#else
            /* on Linux, use /proc/self/exe */
            char exe_path[1024];
            ssize_t elen = readlink("/proc/self/exe", exe_path, sizeof(exe_path)-1);
            if (elen > 0) {
                exe_path[elen] = '\0';
                char *last_sep = strrchr(exe_path, '/');
                if (last_sep) *last_sep = '\0';
                snprintf(stdlib_root, sizeof(stdlib_root), "%s/../stdlib", exe_path);
            } else {
                snprintf(stdlib_root, sizeof(stdlib_root), "stdlib");
            }
#endif
        }

        /* Scan all input files for 'using System.XXX' directives, then
         * auto-include matching stdlib .zan files. */
        static const struct { const char *using_ns; const char *subdir; const char *files[32]; } stdlib_map[] = {
            {"System.IO",        "System/IO",        {"File.zan", "Path.zan", "Directory.zan", NULL}},
            {"System.Text",      "System/Text",      {"Encoding.zan", NULL}},
            {"System.Json",      "System/Json",      {"Json.zan", NULL}},
            {"System.Threading", "System/Threading",  {"Threading.zan", NULL}},
            {"System.Net.Sockets", "System/Net/Sockets", {"Socket.zan", "AsyncSocket.zan", "TcpListener.zan", "TcpClient.zan", "UdpClient.zan", NULL}},
            {"System.Net.Http",  "System/Net/Http",  {"HttpRequest.zan", "HttpResponse.zan", "HttpServer.zan", "HttpClient.zan", NULL}},
            {"System.Net.WebSocket", "System/Net/WebSocket", {"WebSocket.zan", NULL}},
            {"System.Net.Mqtt",  "System/Net/Mqtt",  {"MqttClient.zan", NULL}},
            {"System.Net",       "System/Net",        {"Net.zan", "Worker.zan", NULL}},
            {"System.Data.Orm",  "System/Data/Orm",  {"Model.zan", "QueryBuilder.zan", NULL}},
            {"System.Data",      "System/Data",      {"DbConnection.zan", "DbResult.zan", NULL}},
            {"System.Diagnostics", "System/Diagnostics", {"Process.zan", NULL}},
            {"Platform",         "Platform",          {"Runtime.zan", NULL}},
            {"System.Windows.Forms", "System/Windows", {"Forms.zan", NULL}},
            {"System.Drawing", "System/Drawing", {"Primitives.zan", "Graphics.zan", NULL}},
            {"Gui", "gui", {
                "Types.zan", "Theme.zan", "Render.zan", "Native.zan",
                "Event.zan", "Layout.zan", "App.zan", "Reactive.zan",
                "Icon.zan", NULL
            }},
            {"Gui.Widget", "gui/widget", {
                "Button.zan", "Checkbox.zan", "Radio.zan", "Switch.zan",
                "Slider.zan", "Progress.zan", "Card.zan", "Modal.zan",
                "Tabs.zan", "Tag.zan", "Badge.zan", "Avatar.zan",
                "Tooltip.zan", "Divider.zan", "Select.zan", "Table.zan",
                "Steps.zan", "Notification.zan", "Scrollbar.zan",
                "Skeleton.zan", "Empty.zan", "Input.zan", "Label.zan",
                "Loading.zan", "Breadcrumb.zan", "Pagination.zan", NULL
            }},
            {NULL, NULL, {NULL}}
        };
        /* Resolve stdlib dependencies to a fixpoint: an included file (user
         * source OR an already-pulled stdlib module) may itself `using` another
         * stdlib namespace. Re-scan every included file and pull newly-needed
         * modules until nothing more is added, so importing e.g.
         * System.Net.WebSocket transitively pulls System.Text (Encoding). */
        int ns_added[64] = {0};
        int changed = 1;
        while (changed) {
            changed = 0;
            /* collect which namespaces are used across all included files */
            int ns_used[64] = {0};
            for (int fi = 0; fi < input_count; fi++) {
                size_t slen2 = 0;
                char *src2 = read_file(input_files[fi], &slen2);
                if (!src2) continue;
                for (int mi = 0; stdlib_map[mi].using_ns; mi++) {
                    /* Match "using Namespace;" exactly, not as a substring of
                       a longer namespace (e.g. System.Net should NOT match when
                       the source only has "using System.Net.Sockets;"). */
                    char needle[256];
                    snprintf(needle, sizeof(needle), "using %s;", stdlib_map[mi].using_ns);
                    if (strstr(src2, needle)) {
                        ns_used[mi] = 1;
                    }
                }
                free(src2);
            }
            /* add matching stdlib files not yet pulled */
            for (int mi = 0; stdlib_map[mi].using_ns && mi < 64; mi++) {
                if (!ns_used[mi] || ns_added[mi]) continue;
                ns_added[mi] = 1;
                changed = 1;
                for (int fi2 = 0; fi2 < 32 && stdlib_map[mi].files[fi2]; fi2++) {
                    char mod_path[1024];
#ifdef _WIN32
                    snprintf(mod_path, sizeof(mod_path), "%s\\%s\\%s",
                             stdlib_root, stdlib_map[mi].subdir, stdlib_map[mi].files[fi2]);
#else
                    snprintf(mod_path, sizeof(mod_path), "%s/%s/%s",
                             stdlib_root, stdlib_map[mi].subdir, stdlib_map[mi].files[fi2]);
#endif
                    FILE *check = fopen(mod_path, "rb");
                    if (check) {
                        fclose(check);
                        if (input_count < 128) {
                            char *dup = (char *)malloc(strlen(mod_path) + 1);
                            strcpy(dup, mod_path);
                            input_files[input_count++] = dup;
                        }
                    }
                }
            }
        }
    }

    /* read first source (also used for --dump-tokens) */
    size_t source_len;
    char *source = read_file(input_file, &source_len);
    if (!source) return 1;

    /* dump tokens mode (first file only) */
    if (do_dump_tokens) {
        dump_tokens(source, source_len, input_file);
        free(source);
        return 0;
    }

    /* parse */
    zan_arena_t *arena = zan_arena_new();
    zan_diag_t *diag = zan_diag_new(arena);

    /* Parse every input file and merge their declarations into a single
     * compilation unit so that names resolve across files (multi-file
     * compilation: zanc a.zan b.zan ... -o out). */
    zan_ast_node_t *ast = NULL;
    for (int fi = 0; fi < input_count; fi++) {
        size_t slen = 0;
        char *src = (fi == 0) ? source : read_file(input_files[fi], &slen);
        if (fi == 0) slen = source_len;
        if (!src) {
            fprintf(stderr, "error: cannot read '%s'\n", input_files[fi]);
            zan_arena_free(arena);
            free(source);
            return 1;
        }
        zan_diag_add_file(diag, input_files[fi], src);

        zan_lexer_t lex;
        zan_lexer_init(&lex, src, slen, fi, arena, diag);

        /* Auto-define platform macros */
#ifdef _WIN32
        zan_lexer_define(&lex, "WINDOWS", "1");
        zan_lexer_define(&lex, "WIN32", "1");
#elif defined(__linux__)
        zan_lexer_define(&lex, "LINUX", "1");
#elif defined(__APPLE__)
        zan_lexer_define(&lex, "MACOS", "1");
        zan_lexer_define(&lex, "APPLE", "1");
#endif
#if defined(__x86_64__) || defined(_M_X64)
        zan_lexer_define(&lex, "X86_64", "1");
#elif defined(__aarch64__) || defined(_M_ARM64)
        zan_lexer_define(&lex, "ARM64", "1");
#endif
        zan_lexer_define(&lex, "ZAN", "1");
        /* User -D defines */
        for (int di = 0; di < pp_define_count; di++) {
            char dname[64]; const char *dval = "1";
            const char *eq = strchr(pp_defines[di], '=');
            if (eq) {
                int nl = (int)(eq - pp_defines[di]);
                if (nl > 63) nl = 63;
                memcpy(dname, pp_defines[di], nl); dname[nl] = '\0';
                dval = eq + 1;
            } else {
                strncpy(dname, pp_defines[di], 63); dname[63] = '\0';
            }
            zan_lexer_define(&lex, dname, dval);
        }

        zan_parser_t parser;
        zan_parser_init(&parser, &lex, arena, diag);

        zan_ast_node_t *unit = zan_parser_parse(&parser);

        if (!ast) {
            ast = unit;
        } else {
            for (int k = 0; k < unit->comp_unit.usings.count; k++)
                zan_ast_list_push(&ast->comp_unit.usings,
                                  unit->comp_unit.usings.items[k], arena);
            for (int k = 0; k < unit->comp_unit.decls.count; k++)
                zan_ast_list_push(&ast->comp_unit.decls,
                                  unit->comp_unit.decls.items[k], arena);
            if (!ast->comp_unit.ns) ast->comp_unit.ns = unit->comp_unit.ns;
        }
    }

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
    irgen.check_leaks = check_leaks;
    irgen.runtime_checks = runtime_checks;

    if (zan_irgen_emit(&irgen, ast) != ZAN_OK) {
        fprintf(stderr, "error: code generation failed\n");
        zan_irgen_destroy(&irgen);
        zan_arena_free(arena);
        free(source);
        return 1;
    }

    /* ---- optimize ---- */
    zan_opt_level_t effective_opt = ZAN_OPT_NONE;
    if (opt_level >= 0) {
        effective_opt = (zan_opt_level_t)opt_level;
    } else if (publish_mode) {
        effective_opt = ZAN_OPT_FULL; /* O2 for --publish */
    }
    if (effective_opt > ZAN_OPT_NONE) {
        zan_opt_report_t opt_report = zan_optimize(&irgen, &binder, effective_opt);
        zan_opt_report_print(&opt_report);
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

        /* ---- link object → executable ---- */
        int link_ret;

        /* Socket-async programs (await Socket.ReadReady/WriteReady) link the
         * readiness reactor object shipped with zanc; it provides zan_io_wait_co
         * and the strong zan_io_pump that overrides the program's weak no-op.
         * ZAN_RT_IO_OBJ is the build/install path baked in by CMake. */
        const char *rt_io_obj = NULL;
#ifdef ZAN_RT_IO_OBJ
        if (irgen.uses_socket_async) rt_io_obj = ZAN_RT_IO_OBJ;
#endif

        /* Extra library search dirs for [DllImport] libs, taken from the
         * $ZAN_LIB_PATH env var (platform PATH separator). This lets a Zan
         * program link against a library that is not on the default system
         * search path — e.g. a freshly built zan_gui in the CMake build dir. */
        char zan_lib_dirs[16][512]; int zan_lib_ndirs = 0;
        {
            const char *lp = getenv("ZAN_LIB_PATH");
            if (lp && *lp) {
#ifdef _WIN32
                const char sep = ';';
#else
                const char sep = ':';
#endif
                const char *p = lp;
                while (*p && zan_lib_ndirs < 16) {
                    const char *e = strchr(p, sep);
                    size_t n = e ? (size_t)(e - p) : strlen(p);
                    if (n > 0 && n < sizeof(zan_lib_dirs[0])) {
                        memcpy(zan_lib_dirs[zan_lib_ndirs], p, n);
                        zan_lib_dirs[zan_lib_ndirs][n] = '\0';
                        zan_lib_ndirs++;
                    }
                    if (!e) break;
                    p = e + 1;
                }
            }
        }
#ifdef _WIN32
        /* Self-contained linking: prefer the bundled ld.lld + MinGW-w64 runtime
         * shipped next to zanc (in <zanc_dir>/toolchain), so producing an .exe
         * needs only zan — no external clang / MSVC / Windows SDK. Objects are
         * emitted with the x86_64-w64-windows-gnu ABI (see zan_irgen_write_obj).
         * If the bundle is absent we fall back to a system clang targeting the
         * same mingw ABI. */
        char exe_dir[1024];
        GetModuleFileNameA(NULL, exe_dir, sizeof(exe_dir));
        { char *s = strrchr(exe_dir, '\\'); if (s) *s = '\0'; }
        char ld_path[1200], syslib[1200];
        snprintf(ld_path, sizeof(ld_path), "%s\\toolchain\\ld.exe", exe_dir);
        snprintf(syslib, sizeof(syslib), "%s\\toolchain\\mingw\\lib", exe_dir);
        int have_bundle =
            GetFileAttributesA(ld_path) != INVALID_FILE_ATTRIBUTES &&
            GetFileAttributesA(syslib)  != INVALID_FILE_ATTRIBUTES;

        if (have_bundle) {
            char crt2[1300], crtbeg[1300], crtend[1300], lflag[1300];
            snprintf(crt2,   sizeof(crt2),   "%s\\crt2.o", syslib);
            snprintf(crtbeg, sizeof(crtbeg), "%s\\crtbegin.o", syslib);
            snprintf(crtend, sizeof(crtend), "%s\\crtend.o", syslib);
            snprintf(lflag,  sizeof(lflag),  "-L%s", syslib);

            /* Invoke the bundled GNU ld (mingw binutils) directly. The system
             * import/static libs have circular references, so wrap them in
             * --start-group/--end-group for ld's single-pass resolver. */
            const char *argv[80];
            int a = 0;
            argv[a++] = ld_path;
            argv[a++] = "-m";      argv[a++] = "i386pep";
            argv[a++] = "-Bdynamic";
            /* 256 MB stack: the self-hosted compiler recurses deeply. */
            argv[a++] = "--stack"; argv[a++] = "268435456";
            if (publish_mode) argv[a++] = "-s";
            argv[a++] = "-o";      argv[a++] = obj_path;
            argv[a++] = crt2;
            argv[a++] = crtbeg;
            argv[a++] = lflag;
            char ldirbufs[16][520];
            for (int di = 0; di < zan_lib_ndirs && a < 60; di++) {
                snprintf(ldirbufs[di], sizeof(ldirbufs[di]), "-L%s", zan_lib_dirs[di]);
                argv[a++] = ldirbufs[di];
            }
            argv[a++] = obj_tmp;
            if (rt_io_obj) argv[a++] = rt_io_obj;
            argv[a++] = "--start-group";
            argv[a++] = "-lmingw32"; argv[a++] = "-lgcc";
            argv[a++] = "-lmoldname"; argv[a++] = "-lmingwex";
            argv[a++] = "-lmsvcrt";   argv[a++] = "-lkernel32";
            argv[a++] = "-ladvapi32"; argv[a++] = "-lshell32";
            argv[a++] = "-luser32";
            if (rt_io_obj) argv[a++] = "-lws2_32";
            /* extern [DllImport] libraries (skip those already in the CRT) */
            char libbufs[24][128]; int nb = 0;
            for (int li = 0; li < irgen.extern_lib_count && a < 68 && nb < 24; li++) {
                const char *lib = irgen.extern_libs[li].str;
                int lib_len = (int)irgen.extern_libs[li].len;
                if (lib_len == 1 && (lib[0] == 'm' || lib[0] == 'c')) continue;
                if (lib_len == 3 && memcmp(lib, "crt", 3) == 0) continue;
                if (lib_len == 6 && memcmp(lib, "msvcrt", 6) == 0) continue;
                snprintf(libbufs[nb], sizeof(libbufs[nb]), "-l%.*s", lib_len, lib);
                argv[a++] = libbufs[nb++];
            }
            argv[a++] = "--end-group";
            argv[a++] = crtend;
            argv[a] = NULL;
            link_ret = (int)_spawnv(_P_WAIT, ld_path, argv);
        } else {
            char link_cmd[4096];
            snprintf(link_cmd, sizeof(link_cmd),
                     "clang --target=x86_64-w64-windows-gnu \"%s\" -o \"%s\"%s",
                     obj_tmp, obj_path, publish_mode ? " -O2 -s" : "");
            if (rt_io_obj) {
                size_t cur = strlen(link_cmd);
                snprintf(link_cmd + cur, sizeof(link_cmd) - cur, " \"%s\" -lws2_32", rt_io_obj);
            }
            for (int di = 0; di < zan_lib_ndirs; di++) {
                size_t cur = strlen(link_cmd);
                snprintf(link_cmd + cur, sizeof(link_cmd) - cur, " -L\"%s\"", zan_lib_dirs[di]);
            }
            for (int li = 0; li < irgen.extern_lib_count; li++) {
                const char *lib = irgen.extern_libs[li].str;
                int lib_len = (int)irgen.extern_libs[li].len;
                if (lib_len == 1 && (lib[0] == 'm' || lib[0] == 'c')) continue;
                if (lib_len == 3 && memcmp(lib, "crt", 3) == 0) continue;
                if (lib_len == 6 && memcmp(lib, "msvcrt", 6) == 0) continue;
                size_t cur = strlen(link_cmd);
                snprintf(link_cmd + cur, sizeof(link_cmd) - cur, " -l%.*s", lib_len, lib);
            }
            link_ret = system(link_cmd);
        }
#else
        char link_cmd[4096];
        if (publish_mode) {
            snprintf(link_cmd, sizeof(link_cmd), "cc \"%s\" -o \"%s\" -lm -O2 -s",
                     obj_tmp, obj_path);
        } else {
            snprintf(link_cmd, sizeof(link_cmd), "cc \"%s\" -o \"%s\" -lm",
                     obj_tmp, obj_path);
        }
        if (rt_io_obj) {
            size_t cur = strlen(link_cmd);
            snprintf(link_cmd + cur, sizeof(link_cmd) - cur, " \"%s\"", rt_io_obj);
        }
        for (int di = 0; di < zan_lib_ndirs; di++) {
            size_t cur = strlen(link_cmd);
            /* -L for link-time resolution, -rpath so the produced exe can load
             * the shared library at runtime without LD_LIBRARY_PATH. */
            snprintf(link_cmd + cur, sizeof(link_cmd) - cur,
                     " -L\"%s\" -Wl,-rpath,\"%s\"", zan_lib_dirs[di], zan_lib_dirs[di]);
        }
        /* Windows-only system import libraries have no counterpart on
         * Unix (their functionality is provided through the cross-platform
         * zan_gui native library instead), so skip them here — mirroring the
         * CRT skip on the Windows link path. */
        static const char *const win_only_libs[] = {
            "user32", "gdi32", "kernel32", "advapi32", "shell32", "ole32",
            "oleaut32", "comdlg32", "comctl32", "gdiplus", "dwmapi", "shcore",
            "uxtheme", "msimg32", "winmm", "ws2_32", "shlwapi", "opengl32", NULL };
        for (int li = 0; li < irgen.extern_lib_count; li++) {
            const char *lib = irgen.extern_libs[li].str;
            int lib_len = (int)irgen.extern_libs[li].len;
            int skip = 0;
            /* The Windows CRT pseudo-libs have no -l counterpart on Unix —
             * mirror the CRT skip on the Windows link path instead of emitting
             * a bogus -lcrt. */
            if ((lib_len == 3 && memcmp(lib, "crt", 3) == 0) ||
                (lib_len == 6 && memcmp(lib, "msvcrt", 6) == 0)) {
                continue;
            }
            for (int wi = 0; win_only_libs[wi]; wi++) {
                if ((int)strlen(win_only_libs[wi]) == lib_len &&
                    memcmp(win_only_libs[wi], lib, lib_len) == 0) { skip = 1; break; }
            }
            if (skip) continue;
            /* A [DllImport("libfoo")] names the shared object by its base name;
             * on Unix `-l` re-adds the "lib" prefix, so strip a leading "lib"
             * (DllImport("libc") -> -lc, "libpthread" -> -lpthread) to avoid a
             * bogus -llibc. libc/libm are implicit (libm already via -lm). */
            const char *name = lib; int name_len = lib_len;
            if (name_len > 3 && memcmp(name, "lib", 3) == 0) { name += 3; name_len -= 3; }
            if (name_len == 1 && (name[0] == 'c' || name[0] == 'm')) continue;
            size_t cur = strlen(link_cmd);
            snprintf(link_cmd + cur, sizeof(link_cmd) - cur, " -l%.*s", name_len, name);
        }
        link_ret = system(link_cmd);
#endif

        /* clean up object file */
        remove(obj_tmp);

        if (link_ret != 0) {
            fprintf(stderr, "error: linking failed\n");
            zan_irgen_destroy(&irgen);
            zan_arena_free(arena);
            free(source);
            return 1;
        }

        if (input_count == 1) {
            printf("%s '%s' → '%s'\n", publish_mode ? "Published" : "Compiled", input_file, obj_path);
        } else {
            printf("%s %d files → '%s'\n", publish_mode ? "Published" : "Compiled", input_count, obj_path);
        }
    }

    zan_irgen_destroy(&irgen);
    zan_arena_free(arena);
    free(source);
    return 0;
}
