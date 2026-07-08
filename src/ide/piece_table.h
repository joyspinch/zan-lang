/* piece_table.h -- Piece table text buffer for the Zan IDE.
 *
 * A piece table stores the document as a sequence of "pieces" that reference
 * either the original file content or an append-only add buffer. Edits are
 * O(pieces) insert/delete on a doubly-linked list; text is never moved.
 */
#ifndef ZAN_PIECE_TABLE_H
#define ZAN_PIECE_TABLE_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Which buffer a piece references */
typedef enum {
    PT_BUF_ORIGINAL,   /* immutable file content */
    PT_BUF_ADD         /* append-only edit buffer */
} pt_buf_kind_t;

/* A single piece in the table */
typedef struct pt_piece {
    struct pt_piece *prev;
    struct pt_piece *next;
    pt_buf_kind_t    buf;       /* which buffer */
    size_t           start;     /* byte offset into that buffer */
    size_t           length;    /* byte count */
    size_t           line_count;/* cached newline count in this piece */
} pt_piece_t;

/* Undo/redo action */
typedef enum {
    PT_ACTION_INSERT,
    PT_ACTION_DELETE
} pt_action_kind_t;

typedef struct pt_action {
    pt_action_kind_t kind;
    size_t           offset;    /* document offset of the action */
    char            *text;      /* inserted/deleted text (owned copy) */
    size_t           length;    /* byte length */
    struct pt_action *next;     /* stack link */
} pt_action_t;

/* The piece table itself */
typedef struct {
    char        *original;      /* original file content (immutable) */
    size_t       original_len;

    char        *add_buf;       /* append-only buffer for edits */
    size_t       add_len;
    size_t       add_cap;

    pt_piece_t   sentinel;      /* sentinel node (head of circular list) */
    size_t       total_length;  /* cached total document length in bytes */
    size_t       total_lines;   /* cached total line count */

    pt_action_t *undo_stack;    /* undo stack */
    pt_action_t *redo_stack;    /* redo stack */
} pt_table_t;

/* Create a piece table from file content (takes ownership of `content`).
 * Pass NULL / 0 for an empty document. */
pt_table_t *pt_create(char *content, size_t length);

/* Free all resources. */
void pt_destroy(pt_table_t *pt);

/* Insert `text` (length `len`) at byte offset `offset`. */
void pt_insert(pt_table_t *pt, size_t offset, const char *text, size_t len);

/* Delete `len` bytes starting at byte offset `offset`. */
void pt_delete(pt_table_t *pt, size_t offset, size_t len);

/* Undo the last action. Returns true if something was undone. */
bool pt_undo(pt_table_t *pt);

/* Redo the last undone action. Returns true if something was redone. */
bool pt_redo(pt_table_t *pt);

/* Get total document length in bytes. */
size_t pt_length(const pt_table_t *pt);

/* Get total line count (1-based: empty doc = 1 line). */
size_t pt_line_count(const pt_table_t *pt);

/* Copy bytes [offset, offset+len) into `dst`. Returns bytes copied. */
size_t pt_get_text(const pt_table_t *pt, size_t offset, size_t len, char *dst);

/* Get the byte offset of line `line` (0-based line index). */
size_t pt_line_start(const pt_table_t *pt, size_t line);

/* Get the length of line `line` (0-based, including newline if present). */
size_t pt_line_length(const pt_table_t *pt, size_t line);

/* Convert byte offset to (line, col) — both 0-based. */
void pt_offset_to_pos(const pt_table_t *pt, size_t offset,
                      size_t *out_line, size_t *out_col);

/* Convert (line, col) to byte offset. */
size_t pt_pos_to_offset(const pt_table_t *pt, size_t line, size_t col);

/* Extract the full document as a contiguous string (caller must free). */
char *pt_to_string(const pt_table_t *pt);

#ifdef __cplusplus
}
#endif

#endif /* ZAN_PIECE_TABLE_H */
