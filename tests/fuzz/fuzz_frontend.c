/* 底层系统交互与数据协议契约 */
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "arena.h"
#include "diag.h"
#include "lexer.h"
#include "parser.h"
#include "ast.h"
#include "nsresolve.h"
#include "binder.h"
#include "checker.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    /* 底层系统交互与数据协议契约 */
    if (size > 64 * 1024) return 0;

    /* 底层系统交互与数据协议契约 */
    char *src = (char *)malloc(size + 1);
    if (!src) return 0;
    memcpy(src, data, size);
    src[size] = '\0';

    zan_arena_t *arena = zan_arena_new();
    zan_diag_t *diag = zan_diag_new(arena);
    zan_diag_set_capture(diag, true);           /* store, don't print */
    zan_diag_add_file(diag, "<fuzz>", src);

    /* 底层系统交互与数据协议契约 */
    {
        zan_lexer_t lex;
        zan_lexer_init(&lex, src, size, 0, arena, diag);
        for (int i = 0; i < 1 << 20; i++) {
            zan_token_t t = zan_lexer_next(&lex);
            if (t.kind == TK_EOF) break;
        }
    }

    /* 核心系统底层抽象与内存语义契约 */
    zan_ast_node_t *ast = NULL;
    {
        zan_lexer_t lex;
        zan_lexer_init(&lex, src, size, 0, arena, diag);
        zan_parser_t parser;
        zan_parser_init(&parser, &lex, arena, diag);
        ast = zan_parser_parse(&parser);
    }

    /* 底层系统交互与数据协议契约 */
    if (ast && !zan_diag_has_errors(diag)) {
        zan_parser_flatten_nested_types(ast, arena, diag);
        zan_parser_merge_partials(ast, arena, diag);
        zan_parser_desugar_events(ast, arena, diag);
        zan_nsresolve_run(ast, arena, diag);

        if (!zan_diag_has_errors(diag)) {
            zan_binder_t binder;
            zan_binder_init(&binder, arena, diag);
            zan_binder_bind(&binder, ast);

            /* 底层系统交互与数据协议契约 */
            zan_checker_t checker;
            zan_checker_init(&checker, &binder, arena, diag);
            zan_checker_check(&checker, ast);
        }
    }

    zan_diag_free_buffers(diag);
    zan_arena_free(arena);
    free(src);
    return 0;
}
