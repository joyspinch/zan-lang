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
#include "crosscomp.h"
#include "zan_version.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#ifdef _WIN32
#include <windows.h>
#include <process.h>
#else
#include <unistd.h>
#include <dirent.h>
#include <strings.h>
#include <sys/stat.h>
#endif
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

#include "../common/host_oom.h"
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

/* ---- input-file list helpers (auto-stdlib dedup) ---- */

/* Produce a normalized comparison key for a path so the same file passed both
 * explicitly and via auto-stdlib (which may differ in case / slash direction,
 * e.g. "stdlib\Gui\Widget\Select.zan" vs "...\stdlib\gui/widget\Select.zan")
 * compares equal and is only compiled once. */
static void canon_key(const char *in, char *out, size_t out_sz) {
#ifdef _WIN32
    char full[1024];
    DWORD n = GetFullPathNameA(in, (DWORD)sizeof(full), full, NULL);
    if (n == 0 || n >= sizeof(full)) snprintf(full, sizeof(full), "%s", in);
    size_t j = 0;
    for (size_t i = 0; full[i] && j + 1 < out_sz; i++) {
        char c = full[i];
        if (c == '/') c = '\\';
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        out[j++] = c;
    }
    out[j] = '\0';
#else
    char full[4096];
    if (realpath(in, full) == NULL) snprintf(full, sizeof(full), "%s", in);
    snprintf(out, out_sz, "%s", full);
#endif
}

static int input_file_present(const char **files, int count, const char *cand) {
    char ck[1024];
    canon_key(cand, ck, sizeof(ck));
#ifndef _WIN32
    struct stat cand_stat;
    int have_cand_stat = stat(cand, &cand_stat) == 0;
#endif
    for (int i = 0; i < count; i++) {
        char ek[1024];
        canon_key(files[i], ek, sizeof(ek));
        if (strcmp(ck, ek) == 0) return 1;
#ifndef _WIN32
        if (have_cand_stat) {
            struct stat existing_stat;
            if (stat(files[i], &existing_stat) == 0 &&
                cand_stat.st_dev == existing_stat.st_dev &&
                cand_stat.st_ino == existing_stat.st_ino) return 1;
        }
#endif
    }
    return 0;
}

/* Append an auto-discovered stdlib file: skip if it does not exist or is
 * already present (explicitly or from an earlier auto-include). */
static void add_stdlib_input(const char **files, int *count, const char *path) {
    FILE *check = fopen(path, "rb");
    if (!check) return;
    fclose(check);
    if (input_file_present(files, *count, path)) return;
    if (*count < 128) {
        char *dup = (char *)malloc(strlen(path) + 1);
        memcpy(dup, path, strlen(path) + 1);
        files[(*count)++] = dup;
    }
}

#ifndef _WIN32
static int resolve_dir_component(const char *parent, const char *component,
                                 char *resolved, size_t resolved_sz) {
    DIR *dir = opendir(parent);
    if (!dir) return 0;
    char match_name[256] = {0};
    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        if (strcasecmp(ent->d_name, component) != 0) continue;
        if (!match_name[0] || strcmp(ent->d_name, component) == 0)
            snprintf(match_name, sizeof(match_name), "%s", ent->d_name);
        if (strcmp(ent->d_name, component) == 0) break;
    }
    closedir(dir);
    if (!match_name[0]) return 0;
    char candidate[1024];
    int n = snprintf(candidate, sizeof(candidate), "%s/%s", parent, match_name);
    if (n < 0 || (size_t)n >= sizeof(candidate)) return 0;
    DIR *match = opendir(candidate);
    if (!match) return 0;
    closedir(match);
    snprintf(resolved, resolved_sz, "%s", candidate);
    return 1;
}

static int resolve_stdlib_dir(const char *stdlib_root, const char *subdir,
                              char *resolved, size_t resolved_sz) {
    char current[1024];
    snprintf(current, sizeof(current), "%s", stdlib_root);
    const char *p = subdir;
    while (*p) {
        char component[256];
        size_t len = 0;
        while (p[len] && p[len] != '/') len++;
        if (len == 0 || len >= sizeof(component)) return 0;
        memcpy(component, p, len);
        component[len] = '\0';
        char next[1024];
        if (!resolve_dir_component(current, component, next, sizeof(next))) return 0;
        snprintf(current, sizeof(current), "%s", next);
        p += len;
        if (*p == '/') p++;
    }
    snprintf(resolved, resolved_sz, "%s", current);
    return 1;
}
#endif

/* Auto-include every *.zan file in stdlib_root/subdir (dedup + existence
 * checked). Returns 1 if any new file was added. `subdir` uses '/' separators
 * (accepted by the Win32 file APIs too). */
static int glob_stdlib_dir(const char *stdlib_root, const char *subdir,
                           const char **files, int *count) {
    int before = *count;
#ifdef _WIN32
    char glob_path[1024];
    snprintf(glob_path, sizeof(glob_path), "%s\\%s\\*.zan", stdlib_root, subdir);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(glob_path, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            char mod_path[1024];
            snprintf(mod_path, sizeof(mod_path), "%s\\%s\\%s",
                     stdlib_root, subdir, fd.cFileName);
            add_stdlib_input(files, count, mod_path);
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
#else
    char dir_path[1024];
    if (!resolve_stdlib_dir(stdlib_root, subdir, dir_path, sizeof(dir_path)))
        return 0;
    DIR *d = opendir(dir_path);
    if (d) {
        struct dirent *ent;
        while ((ent = readdir(d)) != NULL) {
            size_t nlen = strlen(ent->d_name);
            if (nlen < 5 || strcmp(ent->d_name + nlen - 4, ".zan") != 0) continue;
            char mod_path[1024];
            snprintf(mod_path, sizeof(mod_path), "%s/%s", dir_path, ent->d_name);
            add_stdlib_input(files, count, mod_path);
        }
        closedir(d);
    }
#endif
    return *count != before;
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

/* Map a [DllImport] name to its linker basename (the -l argument).
 * Strips a leading "lib" so DllImport("libpq") and DllImport("pq") both
 * resolve to libpq, and normalizes identically on every link path
 * (bundled ld, clang fallback, Unix cc). Sets *out_len. Returns NULL for
 * implicit CRT/libc/libm pseudo-libs that must not be emitted as -l. */
static const char *zan_dllimport_lname(const char *lib, int lib_len,
                                       int *out_len) {
    if (lib_len == 3 && memcmp(lib, "crt", 3) == 0) return NULL;
    if (lib_len == 6 && memcmp(lib, "msvcrt", 6) == 0) return NULL;
    const char *name = lib;
    int n = lib_len;
    if (n > 3 && memcmp(name, "lib", 3) == 0) { name += 3; n -= 3; }
    if (n == 1 && (name[0] == 'c' || name[0] == 'm')) return NULL;
    *out_len = n;
    return name;
}

/* Registry of third-party native drivers a published program must carry,
 * unlike system libs (kernel32/msvcrt/libc/.) that already exist on every
 * target. Instead of hardcoding the set in the compiler, it is DISCOVERED from
 * the stdlib tree: each native-backed module declares the [DllImport] library
 * basenames it owns in a `drivers/driver.manifest` file (one -l basename per
 * line; blank lines and '#' comments ignored). The module that ships a driver
 * is simply the directory that contains that `drivers/` folder, so its bundle
 * lives at <stdlib_root>/<module>/drivers/<target-sub>/ and travels with
 * stdlib. Adding a new native module therefore needs no compiler change. */
#define ZAN_MAX_DRIVERS 32
typedef struct {
    char lib[64];      /* normalized -l basename, e.g. "sqlite3", "zan_sdl3" */
    char module[512];  /* owning module path relative to stdlib root, '/'-sep */
} zan_driver_entry_t;
typedef struct {
    zan_driver_entry_t entries[ZAN_MAX_DRIVERS];
    int count;
} zan_driver_registry_t;

static void zan_registry_add(zan_driver_registry_t *reg,
                             const char *lib, const char *module) {
    if (reg->count >= ZAN_MAX_DRIVERS) return;
    for (int i = 0; i < reg->count; i++)
        if (strcmp(reg->entries[i].lib, lib) == 0) return; /* first wins */
    snprintf(reg->entries[reg->count].lib,
             sizeof(reg->entries[0].lib), "%s", lib);
    snprintf(reg->entries[reg->count].module,
             sizeof(reg->entries[0].module), "%s", module);
    reg->count++;
}

/* Read one module's driver.manifest, registering each listed lib against the
 * owning module (its directory relative to the stdlib root). */
static void zan_read_driver_manifest(const char *manifest_path,
                                     const char *module,
                                     zan_driver_registry_t *reg) {
    FILE *f = fopen(manifest_path, "rb");
    if (!f) return;
    char line[128];
    while (fgets(line, sizeof(line), f)) {
        char *s = line;
        while (*s == ' ' || *s == '\t') s++;
        size_t len = strlen(s);
        while (len > 0 && (s[len - 1] == '\n' || s[len - 1] == '\r' ||
                           s[len - 1] == ' ' || s[len - 1] == '\t'))
            s[--len] = '\0';
        if (s[0] == '\0' || s[0] == '#') continue;
        zan_registry_add(reg, s, module);
    }
    fclose(f);
}

/* Recursively walk the stdlib tree looking for module directories that own a
 * `drivers/driver.manifest`. `rel` is the current directory relative to the
 * stdlib root ('/'-separated, empty at the root). Depth is bounded so a stray
 * symlink cycle can't wedge the compiler; `drivers`/`native`/dot directories
 * are not descended into (driver payloads, not further modules). */
static void zan_scan_drivers_dir(const char *dir_full, const char *rel,
                                 int depth, zan_driver_registry_t *reg) {
    if (depth > 8) return;
    char manifest[1200];
    snprintf(manifest, sizeof(manifest), "%s/drivers/driver.manifest", dir_full);
    if (rel[0]) zan_read_driver_manifest(manifest, rel, reg);
#ifdef _WIN32
    char glob[1200];
    snprintf(glob, sizeof(glob), "%s\\*", dir_full);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(glob, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
            const char *nm = fd.cFileName;
            if (nm[0] == '.') continue;
            if (strcmp(nm, "drivers") == 0 || strcmp(nm, "native") == 0) continue;
            char sub[1200];
            snprintf(sub, sizeof(sub), "%s\\%s", dir_full, nm);
            char subrel[512];
            if (rel[0]) snprintf(subrel, sizeof(subrel), "%s/%s", rel, nm);
            else snprintf(subrel, sizeof(subrel), "%s", nm);
            zan_scan_drivers_dir(sub, subrel, depth + 1, reg);
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
#else
    DIR *d = opendir(dir_full);
    if (d) {
        struct dirent *ent;
        while ((ent = readdir(d)) != NULL) {
            const char *nm = ent->d_name;
            if (nm[0] == '.') continue;
            if (strcmp(nm, "drivers") == 0 || strcmp(nm, "native") == 0) continue;
            char sub[1200];
            snprintf(sub, sizeof(sub), "%s/%s", dir_full, nm);
            struct stat st;
            if (stat(sub, &st) != 0 || !S_ISDIR(st.st_mode)) continue;
            char subrel[512];
            if (rel[0]) snprintf(subrel, sizeof(subrel), "%s/%s", rel, nm);
            else snprintf(subrel, sizeof(subrel), "%s", nm);
            zan_scan_drivers_dir(sub, subrel, depth + 1, reg);
        }
        closedir(d);
    }
#endif
}

static void zan_discover_drivers(const char *stdlib_root,
                                 zan_driver_registry_t *reg) {
    reg->count = 0;
    if (!stdlib_root || !stdlib_root[0]) return;
    zan_scan_drivers_dir(stdlib_root, "", 0, reg);
}

/* Index of the discovered driver whose lib basename matches, or -1. */
static int zan_driver_find(const zan_driver_registry_t *reg,
                           const char *lname, int len) {
    for (int i = 0; i < reg->count; i++)
        if ((int)strlen(reg->entries[i].lib) == len &&
            memcmp(reg->entries[i].lib, lname, (size_t)len) == 0) return i;
    return -1;
}

/* Copy a file byte-for-byte (portable; no shell). Returns 0 on success. */
static int zan_copy_file(const char *src, const char *dst) {
    FILE *in = fopen(src, "rb");
    if (!in) return -1;
    FILE *out = fopen(dst, "wb");
    if (!out) { fclose(in); return -1; }
    char buf[65536]; size_t n; int rc = 0;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) { rc = -1; break; }
    }
    fclose(in);
    if (fclose(out) != 0) rc = -1;
    return rc;
}

/* A driver-bundle manifest lists runtime libraries to copy next to the
 * published executable. Each entry must be a bare filename inside the driver
 * directory: reject absolute paths, drive letters, path separators and ".."
 * so a manifest can never make publishing read or (worse) write outside the
 * driver / output directory. */
static bool zan_is_safe_bundle_name(const char *name) {
    if (!name || !name[0]) return false;
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) return false;
    for (const char *p = name; *p; p++)
        if (*p == '/' || *p == '\\' || *p == ':') return false;
    return true;
}

/* Per-target driver-bundle subdirectory under <module>/drivers/. Matches the
 * cross-compile toolchain naming so publishing for a target reads the drivers
 * built for that target. */
static const char *zan_driver_subdir(const zan_target_t *t) {
    if (t->os == ZAN_OS_LINUX)
        return (t->arch == ZAN_ARCH_AARCH64) ? "linux-arm64" : "linux-x64";
    if (t->os == ZAN_OS_MACOS)
        return (t->arch == ZAN_ARCH_AARCH64) ? "macos-arm64" : "macos-x64";
    return (t->arch == ZAN_ARCH_AARCH64) ? "win-arm64" : "win-x64";
}

/* Directory containing the running zanc executable. Everything zanc links with
 * (runtime objects, linker bundle, sysroot) is installed as its sibling, so
 * paths are resolved relative to this at runtime -- never a build-time absolute
 * path baked in, which would break once zanc is relocated/redistributed. */
static void zan_exe_dir(char *out, size_t outsz) {
    out[0] = '\0';
#ifdef _WIN32
    GetModuleFileNameA(NULL, out, (DWORD)outsz);
    { char *s = strrchr(out, '\\'); if (s) *s = '\0'; }
#elif defined(__APPLE__)
    { uint32_t sz = (uint32_t)outsz;
      if (_NSGetExecutablePath(out, &sz) != 0) out[0] = '\0';
      char *s = strrchr(out, '/'); if (s) *s = '\0'; }
#else
    { ssize_t n = readlink("/proc/self/exe", out, outsz - 1);
      if (n > 0) { out[n] = '\0'; char *s = strrchr(out, '/'); if (s) *s = '\0'; } }
#endif
}

/* Bare filename of a path (after the last '/' or '\\'). */
static const char *zan_path_basename(const char *p) {
    const char *b = p;
    for (const char *q = p; *q; q++)
        if (*q == '/' || *q == '\\') b = q + 1;
    return b;
}

static void print_usage(void) {
    fprintf(stderr, "Zan Compiler v%s\n", ZAN_VERSION);
    fprintf(stderr, "Usage: zanc <source.zan> [options]\n");
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  -o <output>     Output file\n");
    fprintf(stderr, "  --dump-tokens   Dump lexer tokens\n");
    fprintf(stderr, "  --dump-ast      Dump parse tree\n");
    fprintf(stderr, "  --emit-ir       Emit LLVM IR to stdout\n");
    fprintf(stderr, "  --check-leaks   Report unreleased objects at program exit\n");
    fprintf(stderr, "  --no-runtime-checks  Disable runtime guards (e.g. division by zero)\n");
    fprintf(stderr, "  --publish        Build optimized release binary (strip debug, optimize)\n");
    fprintf(stderr, "  --link-mode <m>  Native driver linking on publish: shared (copy driver\n");
    fprintf(stderr, "                   libs next to the exe, default) or static (link into the exe)\n");
    fprintf(stderr, "  --driver-dir <d> Override the bundled native driver directory\n");
    fprintf(stderr, "  --stdlib-path <dir>  Path to stdlib directory\n");
    fprintf(stderr, "  --auto-stdlib    Automatically find and include stdlib .zan files\n");
    fprintf(stderr, "  -O0/-O1/-O2/-O3  Set optimization level (default: O0, --publish: O2)\n");
    fprintf(stderr, "  -g, --debug      Emit DWARF debug info for source-level debugging (forces -O0)\n");
    fprintf(stderr, "  --target <name>  Cross-compile for target (e.g. linux-x64, linux-musl)\n");
    fprintf(stderr, "  --list-targets   Show available cross-compilation targets\n");
    fprintf(stderr, "  --subsystem <s>  PE subsystem: console (default) or windows (GUI, Win only)\n");
    fprintf(stderr, "  -L<dir>/--libpath <dir>  Add a native library search directory\n");
    fprintf(stderr, "  --link-lib <name>  Link an extra native library (-> -l<name>)\n");
    fprintf(stderr, "  --link-input <f>   Link an extra object/resource/library file\n");
    fprintf(stderr, "  -D<name>[=value] Define preprocessor symbol\n");
    fprintf(stderr, "  --version, -v    Print version and exit\n");
    fprintf(stderr, "  --help, -h       Show this help and exit\n");
}

int main(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--version") == 0 || strcmp(argv[i], "-v") == 0) {
            printf("zanc %s\n", ZAN_VERSION);
            return 0;
        }
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_usage();
            return 0;
        }
    }
    if (argc < 2) {
        print_usage();
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
    bool debug_info = false; /* -g / --debug: emit DWARF for source debugging */
    bool mt_scheduler = false;
    const char *stdlib_path = NULL;
    bool auto_stdlib = true;
    int opt_level = -1; /* -1 = auto (O0 default, O2 for publish) */
    const char *pp_defines[64];
    int pp_define_count = 0;
    const char *target_name = NULL; /* --target <name|triple>; NULL = host */
    bool link_static_drivers = false; /* --link-mode static; default shared */
    const char *driver_dir_override = NULL; /* --driver-dir */
    /* Extra native link inputs, so the IDE (and callers) can produce GUI/native
     * executables through zanc's own self-contained linker instead of shelling
     * out to clang: --subsystem <console|windows>, repeatable --link-input
     * <obj|res|lib>, --link-lib <name> (-> -l<name>) and -L<dir> / --libpath. */
    const char *link_subsystem = NULL;
    const char *extra_link_inputs[32]; int extra_link_input_count = 0;
    const char *extra_link_libs[32];   int extra_link_lib_count = 0;
    const char *extra_lib_paths[16];   int extra_lib_path_count = 0;
    /* Resolved stdlib root, hoisted so the native-driver block (which lives
     * outside the stdlib-discovery scope) can root driver dirs at
     * <stdlib_root>/<module>/drivers/<target>/. Empty when no stdlib is used. */
    char resolved_stdlib_root[1024] = {0};

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
        } else if (strcmp(argv[i], "-g") == 0 || strcmp(argv[i], "--debug") == 0) {
            debug_info = true;
        } else if (strcmp(argv[i], "--async-workers") == 0 ||
                   strcmp(argv[i], "--mt") == 0) {
            /* Use the multi-worker coroutine scheduler: async programs run
             * their ready queue across a thread pool (worker count from the
             * ZAN_CO_WORKERS env var at run time; default = CPU count). */
            mt_scheduler = true;
        } else if (strcmp(argv[i], "--link-mode") == 0 && i + 1 < argc) {
            const char *m = argv[++i];
            if (strcmp(m, "static") == 0) link_static_drivers = true;
            else if (strcmp(m, "shared") == 0) link_static_drivers = false;
            else { fprintf(stderr, "error: --link-mode must be 'static' or 'shared'\n"); return 1; }
        } else if (strcmp(argv[i], "--driver-dir") == 0 && i + 1 < argc) {
            driver_dir_override = argv[++i];
        } else if (strcmp(argv[i], "--target") == 0 && i + 1 < argc) {
            target_name = argv[++i];
        } else if (strcmp(argv[i], "--list-targets") == 0) {
            const zan_target_info_t *ts;
            int nt = zan_target_list(&ts);
            printf("Available cross-compilation targets:\n");
            for (int t = 0; t < nt; t++)
                printf("  %-12s %-30s %s\n", ts[t].name, ts[t].triple, ts[t].desc);
            return 0;
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
        } else if (strcmp(argv[i], "--subsystem") == 0 && i + 1 < argc) {
            link_subsystem = argv[++i];
        } else if (strcmp(argv[i], "--link-input") == 0 && i + 1 < argc) {
            if (extra_link_input_count < 32) extra_link_inputs[extra_link_input_count++] = argv[++i];
            else { fprintf(stderr, "error: too many --link-input (max 32)\n"); return 1; }
        } else if (strcmp(argv[i], "--link-lib") == 0 && i + 1 < argc) {
            if (extra_link_lib_count < 32) extra_link_libs[extra_link_lib_count++] = argv[++i];
            else { fprintf(stderr, "error: too many --link-lib (max 32)\n"); return 1; }
        } else if (strcmp(argv[i], "--libpath") == 0 && i + 1 < argc) {
            if (extra_lib_path_count < 16) extra_lib_paths[extra_lib_path_count++] = argv[++i];
            else { fprintf(stderr, "error: too many --libpath (max 16)\n"); return 1; }
        } else if (strncmp(argv[i], "-L", 2) == 0 && argv[i][2] != '\0') {
            if (extra_lib_path_count < 16) extra_lib_paths[extra_lib_path_count++] = argv[i] + 2;
            else { fprintf(stderr, "error: too many -L dirs (max 16)\n"); return 1; }
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

    /* ---- resolve compilation target (host unless --target given) ---- */
    zan_target_t target;
    if (target_name) {
        if (!zan_target_parse(target_name, &target)) {
            fprintf(stderr, "error: unknown target '%s' (see --list-targets)\n", target_name);
            return 1;
        }
    } else {
        zan_target_host(&target);
    }
    bool cross_compiling = (target_name != NULL);

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
        /* Hoist for the native-driver block below (outside this scope). */
        snprintf(resolved_stdlib_root, sizeof(resolved_stdlib_root), "%s",
                 stdlib_root);

        /* Auto-include stdlib modules by PATH. Every `using X.Y.Z;` directive
         * maps directly to the directory stdlib_root/X/Y/Z, and all *.zan files
         * there are compiled in. There is deliberately NO hand-maintained
         * namespace->file table: adding or extending a stdlib module is just
         * dropping .zan files at the matching path, requiring no compiler change
         * or rebuild. The stdlib directory layout therefore mirrors the
         * namespace hierarchy 1:1 (e.g. `using Gui.Widget;` -> stdlib/Gui/Widget,
         * `using System.Windows.Forms;` -> stdlib/System/Windows/Forms).
         *
         * Resolve to a fixpoint because a pulled-in module may itself `using`
         * another namespace (e.g. System.Net.WebSocket -> System.Text), so
         * re-scan every included file until nothing new is added. */
        int changed = 1;
        while (changed) {
            changed = 0;
            for (int fi = 0; fi < input_count; fi++) {
                size_t slen3 = 0;
                char *src3 = read_file(input_files[fi], &slen3);
                if (!src3) continue;
                const char *p = src3;
                while ((p = strstr(p, "using ")) != NULL) {
                    /* require the match to start a token (not e.g. "reusing ") */
                    if (p != src3) {
                        char prev = p[-1];
                        if (prev != '\n' && prev != '\r' && prev != ' ' &&
                            prev != '\t' && prev != ';' && prev != '{') {
                            p += 6;
                            continue;
                        }
                    }
                    p += 6;
                    while (*p == ' ' || *p == '\t') p++;
                    char subdir[512];
                    int j = 0;
                    while (j < 510) {
                        char c = *p;
                        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                            (c >= '0' && c <= '9') || c == '_') {
                            subdir[j++] = c;
                        } else if (c == '.') {
                            subdir[j++] = '/';
                        } else {
                            break;
                        }
                        p++;
                    }
                    subdir[j] = '\0';
                    if (j > 0 && *p == ';') {
                        if (glob_stdlib_dir(stdlib_root, subdir,
                                            input_files, &input_count)) {
                            changed = 1;
                        }
                    }
                }
                free(src3);
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

        /* Auto-define platform macros for the *target* (== host unless
         * --target was given), so cross-compiled sources see the destination
         * OS/arch (e.g. LINUX instead of WINDOWS). */
        switch (target.os) {
        case ZAN_OS_WINDOWS:
            zan_lexer_define(&lex, "WINDOWS", "1");
            zan_lexer_define(&lex, "WIN32", "1");
            break;
        case ZAN_OS_LINUX:
            zan_lexer_define(&lex, "LINUX", "1");
            break;
        case ZAN_OS_MACOS:
            zan_lexer_define(&lex, "MACOS", "1");
            zan_lexer_define(&lex, "APPLE", "1");
            break;
        default:
            break;
        }
        if (target.arch == ZAN_ARCH_AARCH64)
            zan_lexer_define(&lex, "ARM64", "1");
        else if (target.arch == ZAN_ARCH_X86_64)
            zan_lexer_define(&lex, "X86_64", "1");
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
    if (zan_irgen_init(&irgen, arena, diag, &binder, input_file,
                       cross_compiling ? target.triple : NULL,
                       target.os == ZAN_OS_WINDOWS, mt_scheduler,
                       check_leaks) != ZAN_OK) {
        fprintf(stderr, "error: failed to initialize code generator\n");
        zan_arena_free(arena);
        free(source);
        return 1;
    }
    irgen.runtime_checks = runtime_checks;
    irgen.emit_debug = debug_info;

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
    /* Optimization reorders/folds instructions and drops locals, which makes
     * DWARF line tables and variable locations unreliable. Debug builds stay at
     * -O0 so single-stepping and breakpoints map faithfully to source. */
    if (debug_info && effective_opt > ZAN_OPT_NONE) {
        fprintf(stderr, "note: -g forces -O0 (debug info is emitted unoptimized)\n");
        effective_opt = ZAN_OPT_NONE;
    }
    if (effective_opt > ZAN_OPT_NONE) {
        zan_opt_report_t opt_report = zan_optimize(&irgen, &binder, effective_opt);
        zan_opt_report_print(&opt_report);
    }

    if (do_emit_ir) {
        if (zan_irgen_write_ir(&irgen, NULL) != ZAN_OK) {
            fprintf(stderr, "error: failed to write LLVM IR\n");
            zan_irgen_destroy(&irgen);
            zan_arena_free(arena);
            free(source);
            return 1;
        }
    } else {
        /* determine output path */
        char obj_path[1024];
        if (output_file) {
            snprintf(obj_path, sizeof(obj_path), "%s", output_file);
        } else {
            /* input.zan ? input */
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

        /* ---- link object ? executable ---- */
        int link_ret;

        /* The compiler's own directory: the runtime objects below ship as its
         * siblings (copied there by the build / publish step), so we resolve
         * them relative to zanc at runtime rather than trusting the build-time
         * absolute path CMake baked into ZAN_RT_*_OBJ (which vanishes once zanc
         * is relocated into a redistributable dist\toolchain). */
        char link_exe_dir[1024];
        zan_exe_dir(link_exe_dir, sizeof(link_exe_dir));
        char rt_io_buf[1200];
        char rt_sync_buf[1200];

        /* Socket-async programs (await Socket.ReadReady/WriteReady) link the
         * readiness reactor object shipped with zanc; it provides zan_io_wait_co
         * and the strong zan_io_pump_timeout that overrides the program's weak
         * timer-only fallback. */
        const char *rt_io_obj = NULL;
#ifdef ZAN_RT_IO_OBJ
        if (irgen.uses_socket_async) {
            snprintf(rt_io_buf, sizeof(rt_io_buf), "%s/%s",
                     link_exe_dir, zan_path_basename(ZAN_RT_IO_OBJ));
            rt_io_obj = rt_io_buf;
        }
#endif
        /* --async-workers (mt_scheduler): the inline single-thread coroutine
         * driver was NOT emitted, so the program must link the multi-worker
         * reactor variant, which supplies both the reactor and the driver.
         * Force-link it even for non-socket async programs. */
#ifdef ZAN_RT_IO_MT_OBJ
        if (mt_scheduler) {
            snprintf(rt_io_buf, sizeof(rt_io_buf), "%s/%s",
                     link_exe_dir, zan_path_basename(ZAN_RT_IO_MT_OBJ));
            rt_io_obj = rt_io_buf;
        }
#endif
        const char *rt_sync_obj = NULL;
#ifdef ZAN_RT_SYNC_OBJ
        if (irgen.uses_sync_runtime) {
            snprintf(rt_sync_buf, sizeof(rt_sync_buf), "%s/%s",
                     link_exe_dir, zan_path_basename(ZAN_RT_SYNC_OBJ));
            rt_sync_obj = rt_sync_buf;
        }
#endif
        if (cross_compiling && rt_sync_obj && target.os != ZAN_OS_LINUX) {
            fprintf(stderr,
                    "error: AtomicInt and SharedTable are not available for "
                    "this cross-compilation target yet\n");
            remove(obj_tmp);
            zan_irgen_destroy(&irgen);
            zan_arena_free(arena);
            free(source);
            return 1;
        }

        /* Extra library search dirs for [DllImport] libs, taken from the
         * $ZAN_LIB_PATH env var (platform PATH separator). This lets a Zan
         * program link against a library that is not on the default system
         * search path - e.g. a freshly built zan_gui in the CMake build dir. */
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

#ifdef __APPLE__
        /* macOS: Homebrew installs into a non-default prefix, so system deps
         * pulled in by [DllImport] (unixODBC's libodbc for System.Data / the
         * ORM, libpq for System.Data.Postgres, ...) are not on the linker's
         * default search path. Add the standard Homebrew lib dirs - including
         * keg-only formulae like libpq - but only those that actually exist,
         * so a missing prefix never emits a "directory not found" warning.
         * This lets DB/ORM programs link on a stock Homebrew machine without
         * the user having to set ZAN_LIB_PATH by hand. */
        {
            char home_lib[512]; home_lib[0] = '\0';
            char home_pq[512];  home_pq[0] = '\0';
            const char *home = getenv("HOME");
            if (home && *home) {
                snprintf(home_lib, sizeof(home_lib), "%s/.homebrew/lib", home);
                snprintf(home_pq, sizeof(home_pq),
                         "%s/.homebrew/opt/libpq/lib", home);
            }
            const char *mac_lib_dirs[] = {
                home_lib[0] ? home_lib : NULL,
                "/opt/homebrew/lib",
                "/usr/local/lib",
                home_pq[0] ? home_pq : NULL,
                "/opt/homebrew/opt/libpq/lib",
                "/usr/local/opt/libpq/lib",
                NULL,
            };
            for (int ci = 0; mac_lib_dirs[ci]; ci++) {
                if (zan_lib_ndirs >= 16) break;
                if (!mac_lib_dirs[ci][0]) continue;
                if (access(mac_lib_dirs[ci], F_OK) != 0) continue;
                int dup = 0;
                for (int dj = 0; dj < zan_lib_ndirs; dj++)
                    if (strcmp(zan_lib_dirs[dj], mac_lib_dirs[ci]) == 0) { dup = 1; break; }
                if (dup) continue;
                snprintf(zan_lib_dirs[zan_lib_ndirs++], sizeof(zan_lib_dirs[0]),
                         "%s", mac_lib_dirs[ci]);
            }
        }
#endif

        /* ---- native stdlib drivers ------------------------------------
         * Third-party drivers (libpq, sqlite3, SDL3 bridges, ...) are NOT
         * present on an
         * arbitrary target, so a published program must carry them. Each driver
         * ships inside the stdlib module that owns it, at
         * <stdlib_root>/<module>/drivers/<target-sub>/ (e.g. System/Data/Sqlite
         * for sqlite3), overridable with --driver-dir. Which libs are drivers
         * and which module owns each is discovered from the stdlib tree's
         * `drivers/driver.manifest` files (see zan_discover_drivers), not
         * hardcoded here. Its directory is added to the link search path so
         * both dev builds and publishes resolve the driver. On Windows the
         * runtime libraries are always copied next to the executable because
         * there is no rpath equivalent; other targets copy them on
         * `--publish`. */
        zan_driver_registry_t driver_reg;
        zan_discover_drivers(resolved_stdlib_root, &driver_reg);
        char driver_dirs[16][1024];
        const char *used_drivers[16]; int used_driver_count = 0;
        int used_driver_len[16];
        const char *used_driver_module[16];
        for (int li = 0; li < irgen.extern_lib_count && used_driver_count < 16; li++) {
            int nlen;
            const char *nm = zan_dllimport_lname(
                irgen.extern_libs[li].str, (int)irgen.extern_libs[li].len, &nlen);
            int didx = nm ? zan_driver_find(&driver_reg, nm, nlen) : -1;
            if (didx >= 0) {
                used_drivers[used_driver_count] = nm;
                used_driver_len[used_driver_count] = nlen;
                used_driver_module[used_driver_count] = driver_reg.entries[didx].module;
                used_driver_count++;
            }
        }
        {
            const char *dsub = zan_driver_subdir(&target);
            for (int d = 0; d < used_driver_count; d++) {
                driver_dirs[d][0] = '\0';
                if (driver_dir_override) {
                    snprintf(driver_dirs[d], sizeof(driver_dirs[d]), "%s",
                             driver_dir_override);
                } else {
                    const char *mod = used_driver_module[d];
                    if (mod && mod[0] && resolved_stdlib_root[0]) {
                        snprintf(driver_dirs[d], sizeof(driver_dirs[d]),
                                 "%s/%s/drivers/%s", resolved_stdlib_root,
                                 mod, dsub);
                    }
                }
                if (!driver_dirs[d][0]) continue;
                /* Add the driver dir to the link search path (link-time -l
                 * resolution) for every link branch below. Static linking reads
                 * the archives from the "static" subdir so shared import libs
                 * and static archives can coexist without ld ambiguity. */
                char linkdir[1100];
                if (link_static_drivers)
                    snprintf(linkdir, sizeof(linkdir), "%s/static", driver_dirs[d]);
                else
                    snprintf(linkdir, sizeof(linkdir), "%s", driver_dirs[d]);
                if (zan_lib_ndirs < 16 && strlen(linkdir) < sizeof(zan_lib_dirs[0])) {
                    /* Prepend, not append: a bundled driver must take link-search
                     * precedence over the auto-added Homebrew/system fallback dirs.
                     * Otherwise a keg-only Homebrew libpq is linked in place of the
                     * shipped driver, and its absolute install-name defeats the
                     * @rpath bundling (the published exe would look for the driver
                     * at the developer's Homebrew path instead of beside itself). */
                    memmove(&zan_lib_dirs[1], &zan_lib_dirs[0],
                            (size_t)zan_lib_ndirs * sizeof(zan_lib_dirs[0]));
                    snprintf(zan_lib_dirs[0], sizeof(zan_lib_dirs[0]), "%s", linkdir);
                    zan_lib_ndirs++;
                }
            }
        }

        if (cross_compiling && target.os == ZAN_OS_LINUX) {
            /* Self-contained cross-compile to Linux: static-link the ELF object
             * against a bundled musl sysroot with ld.lld. The result is a
             * dependency-free static binary - no glibc, no shared libs, no WSL
             * on the target - so "build on Windows, upload, run" just works.
             * Sysroot lives next to zanc at toolchain/<sub>/ (crt*.o + libc.a,
             * plus a linux-built zanrt_io.o for socket-async programs). */
            char exe_dir[1024] = {0};
#ifdef _WIN32
            GetModuleFileNameA(NULL, exe_dir, sizeof(exe_dir));
            { char *s = strrchr(exe_dir, '\\'); if (s) *s = '\0'; }
#elif defined(__APPLE__)
            { uint32_t sz = sizeof(exe_dir);
              if (_NSGetExecutablePath(exe_dir, &sz) != 0) exe_dir[0] = '\0';
              char *s = strrchr(exe_dir, '/'); if (s) *s = '\0'; }
#else
            { ssize_t n = readlink("/proc/self/exe", exe_dir, sizeof(exe_dir) - 1);
              if (n > 0) { exe_dir[n] = '\0'; char *s = strrchr(exe_dir, '/'); if (s) *s = '\0'; } }
#endif
            const char *sub = (target.arch == ZAN_ARCH_AARCH64)
                              ? "linux-arm64" : "linux-musl";
            /* Sysroot sits right next to zanc, in whatever directory the
             * compiler was installed into (dev: build/<sub>; release:
             * dist/toolchain/<sub>). No separate toolchain/ subdirectory. */
            char sys[1200];
            snprintf(sys, sizeof(sys), "%s/%s", exe_dir, sub);

            char cmd[4096];
            snprintf(cmd, sizeof(cmd),
                     "ld.lld -static%s -o \"%s\" \"%s/crt1.o\" \"%s/crti.o\" \"%s\"",
                     publish_mode ? " -s" : "", obj_path, sys, sys, obj_tmp);
            if (irgen.uses_socket_async) {
                size_t cur = strlen(cmd);
                snprintf(cmd + cur, sizeof(cmd) - cur, " \"%s/zanrt_io.o\"", sys);
            }
            if (irgen.uses_sync_runtime) {
                /* atomics / shared-table runtime; its pthread, flock and shm
                 * symbols resolve from the static musl libc.a below. */
                size_t cur = strlen(cmd);
                snprintf(cmd + cur, sizeof(cmd) - cur, " \"%s/zanrt_sync.o\"", sys);
            }
            { size_t cur = strlen(cmd);
              snprintf(cmd + cur, sizeof(cmd) - cur,
                       " --start-group \"%s/libc.a\" --end-group \"%s/crtn.o\"",
                       sys, sys); }
            link_ret = system(cmd);
        } else if (cross_compiling) {
            fprintf(stderr,
                    "error: cross-compilation to '%s' is not supported yet; "
                    "only linux targets are implemented (see --list-targets)\n",
                    target.triple);
            remove(obj_tmp);
            zan_irgen_destroy(&irgen);
            zan_arena_free(arena);
            free(source);
            return 1;
        } else {
#ifdef _WIN32
        /* Self-contained linking: prefer the bundled ld.lld + MinGW-w64 runtime
         * shipped next to zanc (in <zanc_dir>/toolchain), so producing an .exe
         * needs only zan - no external clang / MSVC / Windows SDK. Objects are
         * emitted with the x86_64-w64-windows-gnu ABI (see zan_irgen_write_obj).
         * If the bundle is absent we fall back to a system clang targeting the
         * same mingw ABI. */
        char exe_dir[1024];
        GetModuleFileNameA(NULL, exe_dir, sizeof(exe_dir));
        { char *s = strrchr(exe_dir, '\\'); if (s) *s = '\0'; }
        /* The linker bundle (ld + mingw runtime) sits right next to zanc, in
         * whatever directory the compiler was installed into (dev: build\;
         * release: dist\toolchain\). No separate toolchain\ subdirectory. */
        char ld_path[1200], syslib[1200];
        snprintf(ld_path, sizeof(ld_path), "%s\\ld.exe", exe_dir);
        snprintf(syslib, sizeof(syslib), "%s\\mingw\\lib", exe_dir);
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
            const char *argv[160];
            int a = 0;
            argv[a++] = ld_path;
            argv[a++] = "-m";      argv[a++] = "i386pep";
            argv[a++] = "-Bdynamic";
            /* 256 MB stack: the self-hosted compiler recurses deeply. */
            argv[a++] = "--stack"; argv[a++] = "268435456";
            if (publish_mode) argv[a++] = "-s";
            /* GUI apps: hide the console window (still entered via main). */
            if (link_subsystem && strcmp(link_subsystem, "windows") == 0) {
                argv[a++] = "--subsystem"; argv[a++] = "windows";
            }
            argv[a++] = "-o";      argv[a++] = obj_path;
            argv[a++] = crt2;
            argv[a++] = crtbeg;
            argv[a++] = lflag;
            char ldirbufs[16][520];
            for (int di = 0; di < zan_lib_ndirs && a < 120; di++) {
                snprintf(ldirbufs[di], sizeof(ldirbufs[di]), "-L%s", zan_lib_dirs[di]);
                argv[a++] = ldirbufs[di];
            }
            /* caller-supplied library search dirs (-L / --libpath) */
            char elpbufs[16][520];
            for (int di = 0; di < extra_lib_path_count && a < 120; di++) {
                snprintf(elpbufs[di], sizeof(elpbufs[di]), "-L%s", extra_lib_paths[di]);
                argv[a++] = elpbufs[di];
            }
            argv[a++] = obj_tmp;
            if (rt_io_obj) argv[a++] = rt_io_obj;
            if (rt_sync_obj) argv[a++] = rt_sync_obj;
            /* caller-supplied objects/resources (--link-input) */
            for (int ei = 0; ei < extra_link_input_count && a < 130; ei++)
                argv[a++] = extra_link_inputs[ei];
            argv[a++] = "--start-group";
            /* caller-supplied libraries (--link-lib), inside the group so they
             * can resolve against and be resolved by the system libs. */
            char elibbufs[32][160]; int neb = 0;
            for (int ei = 0; ei < extra_link_lib_count && a < 140 && neb < 32; ei++) {
                snprintf(elibbufs[neb], sizeof(elibbufs[neb]), "-l%s", extra_link_libs[ei]);
                argv[a++] = elibbufs[neb++];
            }
            argv[a++] = "-lmingw32"; argv[a++] = "-lgcc";
            argv[a++] = "-lmoldname"; argv[a++] = "-lmingwex";
            argv[a++] = "-lmsvcrt";   argv[a++] = "-lkernel32";
            argv[a++] = "-ladvapi32"; argv[a++] = "-lshell32";
            argv[a++] = "-luser32";
            if (rt_io_obj) argv[a++] = "-lws2_32";
            /* extern [DllImport] libraries (skip those already in the CRT) */
            char libbufs[24][128]; int nb = 0;
            for (int li = 0; li < irgen.extern_lib_count && a < 150 && nb < 24; li++) {
                int nlen;
                const char *nm = zan_dllimport_lname(
                    irgen.extern_libs[li].str,
                    (int)irgen.extern_libs[li].len, &nlen);
                if (!nm) continue;
                snprintf(libbufs[nb], sizeof(libbufs[nb]), "-l%.*s", nlen, nm);
                argv[a++] = libbufs[nb++];
            }
            argv[a++] = "--end-group";
            argv[a++] = crtend;
            argv[a] = NULL;
            link_ret = (int)_spawnv(_P_WAIT, ld_path, argv);
        } else {
            char link_cmd[4096];
            /* 256 MB stack: the self-hosted compiler recurses deeply. Mirrors
             * the bundled-ld branch above; without it a clang-linked zanc
             * overflows the default 1 MB Windows stack when self-compiling. */
            snprintf(link_cmd, sizeof(link_cmd),
                     "clang --target=x86_64-w64-windows-gnu \"%s\" -o \"%s\" "
                     "-Wl,--stack,268435456%s",
                     obj_tmp, obj_path, publish_mode ? " -O2 -s" : "");
            if (rt_io_obj) {
                size_t cur = strlen(link_cmd);
                snprintf(link_cmd + cur, sizeof(link_cmd) - cur, " \"%s\" -lws2_32", rt_io_obj);
            }
            if (rt_sync_obj) {
                size_t cur = strlen(link_cmd);
                snprintf(link_cmd + cur, sizeof(link_cmd) - cur, " \"%s\"", rt_sync_obj);
            }
            for (int di = 0; di < zan_lib_ndirs; di++) {
                size_t cur = strlen(link_cmd);
                snprintf(link_cmd + cur, sizeof(link_cmd) - cur, " -L\"%s\"", zan_lib_dirs[di]);
            }
            if (link_subsystem && strcmp(link_subsystem, "windows") == 0) {
                size_t cur = strlen(link_cmd);
                snprintf(link_cmd + cur, sizeof(link_cmd) - cur, " -Wl,--subsystem,windows");
            }
            for (int di = 0; di < extra_lib_path_count; di++) {
                size_t cur = strlen(link_cmd);
                snprintf(link_cmd + cur, sizeof(link_cmd) - cur, " -L\"%s\"", extra_lib_paths[di]);
            }
            for (int ei = 0; ei < extra_link_input_count; ei++) {
                size_t cur = strlen(link_cmd);
                snprintf(link_cmd + cur, sizeof(link_cmd) - cur, " \"%s\"", extra_link_inputs[ei]);
            }
            for (int ei = 0; ei < extra_link_lib_count; ei++) {
                size_t cur = strlen(link_cmd);
                snprintf(link_cmd + cur, sizeof(link_cmd) - cur, " -l%s", extra_link_libs[ei]);
            }
            for (int li = 0; li < irgen.extern_lib_count; li++) {
                int nlen;
                const char *nm = zan_dllimport_lname(
                    irgen.extern_libs[li].str,
                    (int)irgen.extern_libs[li].len, &nlen);
                if (!nm) continue;
                size_t cur = strlen(link_cmd);
                snprintf(link_cmd + cur, sizeof(link_cmd) - cur, " -l%.*s", nlen, nm);
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
        if (rt_sync_obj) {
            size_t cur = strlen(link_cmd);
#ifdef __APPLE__
            snprintf(link_cmd + cur, sizeof(link_cmd) - cur,
                     " \"%s\" -pthread", rt_sync_obj);
#else
            snprintf(link_cmd + cur, sizeof(link_cmd) - cur,
                     " \"%s\" -pthread -lrt", rt_sync_obj);
#endif
        }
        for (int di = 0; di < zan_lib_ndirs; di++) {
            size_t cur = strlen(link_cmd);
            /* -L for link-time resolution, -rpath so the produced exe can load
             * the shared library at runtime without LD_LIBRARY_PATH. */
            snprintf(link_cmd + cur, sizeof(link_cmd) - cur,
                     " -L\"%s\" -Wl,-rpath,\"%s\"", zan_lib_dirs[di], zan_lib_dirs[di]);
        }
        /* Runtime search path relative to the executable, so a --publish build
         * whose driver dylibs are copied next to the exe stays self-contained
         * even after the whole directory is relocated to the target machine. */
        if (used_driver_count > 0) {
            size_t cur = strlen(link_cmd);
#ifdef __APPLE__
            snprintf(link_cmd + cur, sizeof(link_cmd) - cur,
                     " -Wl,-rpath,@loader_path");
#else
            snprintf(link_cmd + cur, sizeof(link_cmd) - cur,
                     " -Wl,-rpath,'$ORIGIN'");
#endif
        }
        /* Windows-only system import libraries have no counterpart on
         * Unix (their functionality is provided through the cross-platform
         * zan_gui native library instead), so skip them here - mirroring the
         * CRT skip on the Windows link path. */
        static const char *const win_only_libs[] = {
            "user32", "gdi32", "kernel32", "advapi32", "shell32", "ole32",
            "oleaut32", "comdlg32", "comctl32", "gdiplus", "dwmapi", "shcore",
            "uxtheme", "msimg32", "winmm", "ws2_32", "shlwapi", "opengl32", NULL };
        for (int li = 0; li < irgen.extern_lib_count; li++) {
            const char *lib = irgen.extern_libs[li].str;
            int lib_len = (int)irgen.extern_libs[li].len;
            int skip = 0;
            /* Windows-only system import libs have no -l counterpart on Unix. */
            for (int wi = 0; win_only_libs[wi]; wi++) {
                if ((int)strlen(win_only_libs[wi]) == lib_len &&
                    memcmp(win_only_libs[wi], lib, lib_len) == 0) { skip = 1; break; }
            }
            if (skip) continue;
            /* Shared normalization: strip a leading "lib", drop implicit
             * CRT/libc/libm pseudo-libs (libm already via -lm). */
            int name_len;
            const char *name = zan_dllimport_lname(lib, lib_len, &name_len);
            if (!name) continue;
            size_t cur = strlen(link_cmd);
            snprintf(link_cmd + cur, sizeof(link_cmd) - cur, " -l%.*s", name_len, name);
        }
        /* caller-supplied link inputs (--libpath / --link-input / --link-lib);
         * --subsystem is Windows-only and ignored here. */
        for (int di = 0; di < extra_lib_path_count; di++) {
            size_t cur = strlen(link_cmd);
            snprintf(link_cmd + cur, sizeof(link_cmd) - cur,
                     " -L\"%s\" -Wl,-rpath,\"%s\"", extra_lib_paths[di], extra_lib_paths[di]);
        }
        for (int ei = 0; ei < extra_link_input_count; ei++) {
            size_t cur = strlen(link_cmd);
            snprintf(link_cmd + cur, sizeof(link_cmd) - cur, " \"%s\"", extra_link_inputs[ei]);
        }
        for (int ei = 0; ei < extra_link_lib_count; ei++) {
            size_t cur = strlen(link_cmd);
            snprintf(link_cmd + cur, sizeof(link_cmd) - cur, " -l%s", extra_link_libs[ei]);
        }
        link_ret = system(link_cmd);
#endif
        }

        /* clean up object file */
        remove(obj_tmp);

        if (link_ret != 0) {
            fprintf(stderr, "error: linking failed\n");
            zan_irgen_destroy(&irgen);
            zan_arena_free(arena);
            free(source);
            return 1;
        }

        /* ---- bundle native driver runtime libraries (shared) ------------
         * Copy each used driver's runtime shared libraries next to the produced
         * executable for published programs and for all Windows builds, where
         * a linked DLL must be available beside the executable at launch. The
         * set of files per driver is taken
         * from an optional manifest "<driver_dir>/<driver>.bundle" (one file
         * name per line - lets a driver ship its own dependencies, e.g. libpq
         * with the OpenSSL DLLs); absent a manifest, common default file names
         * are tried. Static linking folds the driver into the exe, so nothing
         * is copied there. */
        if ((publish_mode || target.os == ZAN_OS_WINDOWS) &&
            !link_static_drivers && used_driver_count > 0) {
            char outdir[1024];
            snprintf(outdir, sizeof(outdir), "%s", obj_path);
            { char *s1 = strrchr(outdir, '/'); char *s2 = strrchr(outdir, '\\');
              char *s = (s1 > s2) ? s1 : s2;
              if (s) *s = '\0'; else snprintf(outdir, sizeof(outdir), "."); }

            bool win_target = (target.os == ZAN_OS_WINDOWS);
            for (int d = 0; d < used_driver_count; d++) {
                const char *driver_dir = driver_dirs[d];
                if (!driver_dir[0]) continue;
                char drv[64];
                snprintf(drv, sizeof(drv), "%.*s",
                         used_driver_len[d], used_drivers[d]);

                /* candidate runtime file names for this driver. The cap must
                 * cover a driver's whole dependency closure (e.g. libpq ships
                 * libpq + its OpenSSL and Kerberos dylibs -> 8 files). */
                char cands[16][128]; int ncand = 0;
                char manifest[1200];
                snprintf(manifest, sizeof(manifest), "%s/%s.bundle", driver_dir, drv);
                FILE *mf = fopen(manifest, "rb");
                if (mf) {
                    char line[128];
                    while (ncand < 16 && fgets(line, sizeof(line), mf)) {
                        size_t l = strlen(line);
                        while (l > 0 && (line[l-1] == '\n' || line[l-1] == '\r'
                                         || line[l-1] == ' ' || line[l-1] == '\t'))
                            line[--l] = '\0';
                        if (l > 0) {
                            if (zan_is_safe_bundle_name(line)) {
                                snprintf(cands[ncand++], sizeof(cands[0]), "%s", line);
                            } else {
                                fprintf(stderr, "warning: ignoring unsafe entry "
                                    "'%s' in %s (must be a bare filename)\n",
                                    line, manifest);
                            }
                        }
                    }
                    fclose(mf);
                } else if (win_target) {
                    snprintf(cands[ncand++], sizeof(cands[0]), "%s.dll", drv);
                    snprintf(cands[ncand++], sizeof(cands[0]), "lib%s.dll", drv);
                } else {
                    snprintf(cands[ncand++], sizeof(cands[0]), "lib%s.so", drv);
                }

                int copied = 0;
                for (int c = 0; c < ncand; c++) {
                    char src[1300], dst[1300];
                    snprintf(src, sizeof(src), "%s/%s", driver_dir, cands[c]);
                    snprintf(dst, sizeof(dst), "%s/%s", outdir, cands[c]);
                    if (zan_copy_file(src, dst) == 0) {
                        printf("  bundled driver '%s' ? %s\n", drv, cands[c]);
                        copied++;
                    }
                }
                if (copied == 0) {
                    fprintf(stderr,
                        "warning: driver '%s' was not bundled (no runtime library "
                        "found in %s). The published program will require '%s' to "
                        "be installed on the target, or add a %s/%s.bundle manifest.\n",
                        drv, driver_dir, drv, driver_dir, drv);
                }
            }
        }

        if (input_count == 1) {
            printf("%s '%s' ? '%s'\n", publish_mode ? "Published" : "Compiled", input_file, obj_path);
        } else {
            printf("%s %d files ? '%s'\n", publish_mode ? "Published" : "Compiled", input_count, obj_path);
        }
    }

    zan_irgen_destroy(&irgen);
    zan_arena_free(arena);
    free(source);
    return 0;
}
