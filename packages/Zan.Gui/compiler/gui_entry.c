#include "gui_entry.h"
#include "arena.h"
#include "diag.h"
#include "lexer.h"
#include "parser.h"
#include "synth_buffer.h"
#include <string.h>

static zan_walk_action_t stop_at_entry(const zan_ast_node_t *node, void *data) {
    (void)data;
    if (node->kind != AST_METHOD_DECL || !node->method_decl.name.str)
        return ZAN_WALK_CONTINUE;

    zan_istr_t name = node->method_decl.name;
    if (name.len == 4 && memcmp(name.str, "Main", 4) == 0 &&
        (node->method_decl.modifiers & MOD_STATIC))
        return ZAN_WALK_ABORT;
    if (name.len == 12 && memcmp(name.str, "__DesignMain", 12) == 0)
        return ZAN_WALK_ABORT;
    return ZAN_WALK_CONTINUE;
}

bool zan_gui_needs_entry(const zan_ast_node_t *unit) {
    if (!unit || unit->kind != AST_COMPILATION_UNIT) return false;
    for (int i = 0; i < unit->comp_unit.decls.count; i++) {
        if (!zan_ast_walk_const(unit->comp_unit.decls.items[i], stop_at_entry,
                                NULL, NULL))
            return false;
    }
    return true;
}

void zan_gui_synthesize_entry(zan_ast_node_t *unit, zan_arena_t *arena,
                              zan_diag_t *diag, const char *project_type,
                              const char *project_entry, const char *input_file) {
    if (!project_type || strcmp(project_type, "gui") != 0) return;

    /* zan.proj entry takes precedence over the first CLI input. The file
       basename, with its first letter capitalized, names the class to show. */
    const char *entry = project_entry && project_entry[0] ? project_entry : input_file;
    if (!entry) return;
    const char *slash = strrchr(entry, '/');
    const char *bslash = strrchr(entry, '\\');
    if (bslash && (!slash || bslash > slash)) slash = bslash;
    const char *base = slash ? slash + 1 : entry;
    char cls[256];
    size_t blen = strlen(base);
    if (blen >= sizeof(cls)) blen = sizeof(cls) - 1;
    memcpy(cls, base, blen);
    cls[blen] = 0;
    char *dot = strrchr(cls, '.');
    if (dot) *dot = 0;
    if (cls[0] >= 'a' && cls[0] <= 'z') cls[0] = (char)(cls[0] - 32);
    if (cls[0] == 0) return;

    zan_synth_buf_t b;
    memset(&b, 0, sizeof(b));
    zan_synth_putf(&b, "/* 由编译器合成的窗口入口（勿手改）：type=gui 工程无 Main\n");
    zan_synth_putf(&b, "   时的回退入口；用户写了 Main 即整体优先，本类不参与。 */\n");
    zan_synth_putf(&b, "static class __ZanDocMain {\n");
    zan_synth_putf(&b, "    static void __DesignMain() {\n");
    zan_synth_putf(&b, "        new %s().Show();\n", cls);
    zan_synth_putf(&b, "    }\n}\n");
    if (!b.buf) return;

    size_t slen = b.len;
    char *src = zan_arena_strdup(arena, b.buf, slen);
    free(b.buf);
    if (!src) return;
    int file_id = diag->file_count;
    zan_diag_add_file(diag, "<docapp-main>", src);
    zan_lexer_t lex;
    zan_lexer_init(&lex, src, slen, file_id, arena, diag);
    zan_parser_t parser;
    zan_parser_init(&parser, &lex, arena, diag);
    zan_ast_node_t *synth = zan_parser_parse(&parser);
    if (!synth || synth->kind != AST_COMPILATION_UNIT) return;
    for (int i = 0; i < synth->comp_unit.usings.count; i++)
        zan_ast_list_push(&unit->comp_unit.usings,
                          synth->comp_unit.usings.items[i], arena);
    for (int i = 0; i < synth->comp_unit.decls.count; i++)
        zan_ast_list_push(&unit->comp_unit.decls,
                          synth->comp_unit.decls.items[i], arena);
}
