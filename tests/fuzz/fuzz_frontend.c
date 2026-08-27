/*
 * libFuzzer entry point for the Zan front-end: lexer, parser, the post-parse
 * resolve steps, the binder and the type checker.
 *
 * Feeds arbitrary bytes through the same phase order the driver uses
 * (src/compiler/main.c) so AddressSanitizer / UBSan can flag crashes,
 * out-of-bounds reads and undefined behaviour on malformed input. Diagnostics
 * are captured rather than printed, and no phase may abort -- the fuzzer drives
 * thousands of inputs per second.
 *
 * The phase gates below are not an optimization, they are what keeps the
 * findings real. The driver runs the resolve steps only on a clean parse, and
 * binds only on a clean resolve; a binder fed a half-parsed AST would crash in
 * states the compiler can never reach, and every such crash is a false report
 * that costs a triage cycle. Mirror the driver's gates exactly.
 *
 * Everything linked here is LLVM-free, which is what keeps the job seconds
 * rather than minutes. Fuzzing irgen would pull in LLVM and a target machine;
 * that is a separate harness, not an extension of this one.
 *
 * Build (see the fuzz CI job):
 *   clang -g -O1 -fsanitize=fuzzer,address,undefined -Isrc/compiler \
 *     src/compiler/{arena,diag,lexer,parser,ast,nsresolve,binder,checker,
 *                   definite,reflect_api}.c src/common/host_oom.c \
 *     tests/fuzz/fuzz_frontend.c -o fuzz_frontend
 */
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
    /* Cap input size so pathological inputs stay fast. */
    if (size > 64 * 1024) return 0;

    /* NUL-terminate a private copy: the lexer honours source_len, but a
     * terminator guards any defensive source[pos] peeks. */
    char *src = (char *)malloc(size + 1);
    if (!src) return 0;
    memcpy(src, data, size);
    src[size] = '\0';

    zan_arena_t *arena = zan_arena_new();
    zan_diag_t *diag = zan_diag_new(arena);
    zan_diag_set_capture(diag, true);           /* store, don't print */
    zan_diag_add_file(diag, "<fuzz>", src);

    /* 1) Standalone lexer pass: pull tokens until EOF. */
    {
        zan_lexer_t lex;
        zan_lexer_init(&lex, src, size, 0, arena, diag);
        for (int i = 0; i < 1 << 20; i++) {
            zan_token_t t = zan_lexer_next(&lex);
            if (t.kind == TK_EOF) break;
        }
    }

    /* 2) Full parse pass on a fresh lexer. */
    zan_ast_node_t *ast = NULL;
    {
        zan_lexer_t lex;
        zan_lexer_init(&lex, src, size, 0, arena, diag);
        zan_parser_t parser;
        zan_parser_init(&parser, &lex, arena, diag);
        ast = zan_parser_parse(&parser);
    }

    /* 3) Post-parse resolve steps, then 4) bind and 5) check -- gated the way
     * the driver gates them. The code generators the driver runs between
     * nsresolve and bind are skipped: they shell out to a zanc subprocess. */
    if (ast && !zan_diag_has_errors(diag)) {
        zan_parser_flatten_nested_types(ast, arena, diag);
        zan_parser_merge_partials(ast, arena, diag);
        zan_parser_desugar_events(ast, arena, diag);
        zan_nsresolve_run(ast, arena, diag);

        if (!zan_diag_has_errors(diag)) {
            zan_binder_t binder;
            zan_binder_init(&binder, arena, diag);
            zan_binder_bind(&binder, ast);

            /* No gate here: the driver type-checks even when binding reported,
             * so the checker must tolerate a partially bound program. */
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
