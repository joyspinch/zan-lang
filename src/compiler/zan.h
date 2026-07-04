/* zan.h -- Common types and forward declarations for the Zan compiler.
 *
 * Every compiler module includes this header.
 */

#ifndef ZAN_H
#define ZAN_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* ---- status codes ---- */

typedef enum {
    ZAN_OK = 0,
    ZAN_ERROR,
    ZAN_OOM,
} zan_status_t;

/* ---- source location ---- */

typedef struct {
    uint32_t file_id;
    uint32_t line;
    uint32_t col;
    uint32_t offset;
} zan_loc_t;

static inline zan_loc_t zan_loc(uint32_t file_id, uint32_t line, uint32_t col, uint32_t offset) {
    zan_loc_t loc = { file_id, line, col, offset };
    return loc;
}

/* ---- forward declarations ---- */

typedef struct zan_arena zan_arena_t;
typedef struct zan_intern zan_intern_t;
typedef struct zan_diag zan_diag_t;
typedef struct zan_source zan_source_t;
typedef struct zan_token zan_token_t;
typedef struct zan_lexer zan_lexer_t;
typedef struct zan_ast_node zan_ast_node_t;
typedef struct zan_parser zan_parser_t;
typedef struct zan_binder zan_binder_t;
typedef struct zan_checker zan_checker_t;
typedef struct zan_irgen zan_irgen_t;
typedef struct zan_type zan_type_t;
typedef struct zan_symbol zan_symbol_t;

/* ---- interned string handle ---- */

typedef struct {
    const char *str;
    uint32_t len;
} zan_istr_t;

#endif /* ZAN_H */
