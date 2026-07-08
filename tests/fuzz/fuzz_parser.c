/*
 * libFuzzer entry point for the Zan lexer + parser.
 *
 * Feeds arbitrary bytes through the full front-end path used by the compiler
 * (lex every token, then run the recursive-descent parser) so AddressSanitizer
 * / UBSan can flag crashes, out-of-bounds reads, and undefined behaviour on
 * malformed input. Diagnostics are captured (not printed) and error handling
 * must never abort — the fuzzer drives thousands of inputs per second.
 *
 * Build (see tests/fuzz/README or the fuzz CI job):
 *   clang -g -O1 -fsanitize=fuzzer,address,undefined -Isrc/compiler \
 *     src/compiler/{arena,diag,lexer,parser,ast}.c tests/fuzz/fuzz_parser.c \
 *     -o fuzz_parser
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
    {
        zan_lexer_t lex;
        zan_lexer_init(&lex, src, size, 0, arena, diag);
        zan_parser_t parser;
        zan_parser_init(&parser, &lex, arena, diag);
        (void)zan_parser_parse(&parser);
    }

    zan_diag_free_buffers(diag);
    zan_arena_free(arena);
    free(src);
    return 0;
}
