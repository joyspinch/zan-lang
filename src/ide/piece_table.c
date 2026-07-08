/* piece_table.c -- Piece table implementation. */
#include "piece_table.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ---------- helpers ---------- */

static size_t count_newlines(const char *s, size_t len) {
    size_t n = 0;
    for (size_t i = 0; i < len; i++) {
        if (s[i] == '\n') n++;
    }
    return n;
}

static const char *piece_data(const pt_table_t *pt, const pt_piece_t *p) {
    return (p->buf == PT_BUF_ORIGINAL)
        ? pt->original + p->start
        : pt->add_buf  + p->start;
}

static pt_piece_t *piece_new(pt_buf_kind_t buf, size_t start, size_t length,
                             size_t line_count) {
    pt_piece_t *p = (pt_piece_t *)calloc(1, sizeof(pt_piece_t));
    p->buf = buf;
    p->start = start;
    p->length = length;
    p->line_count = line_count;
    return p;
}

static void piece_insert_after(pt_piece_t *after, pt_piece_t *p) {
    p->prev = after;
    p->next = after->next;
    after->next->prev = p;
    after->next = p;
}

static void piece_remove(pt_piece_t *p) {
    p->prev->next = p->next;
    p->next->prev = p->prev;
    p->prev = p->next = NULL;
}

static void add_buf_append(pt_table_t *pt, const char *text, size_t len) {
    while (pt->add_len + len > pt->add_cap) {
        size_t new_cap = pt->add_cap ? pt->add_cap * 2 : 4096;
        pt->add_buf = (char *)realloc(pt->add_buf, new_cap);
        pt->add_cap = new_cap;
    }
    memcpy(pt->add_buf + pt->add_len, text, len);
    pt->add_len += len;
}

/* find the piece containing byte offset `offset` and set `*piece_offset`
 * to the offset within that piece. */
static pt_piece_t *find_piece(const pt_table_t *pt, size_t offset,
                              size_t *piece_offset) {
    size_t pos = 0;
    pt_piece_t *p = pt->sentinel.next;
    while (p != &pt->sentinel) {
        if (offset <= pos + p->length) {
            *piece_offset = offset - pos;
            return p;
        }
        pos += p->length;
        p = p->next;
    }
    /* past end — return sentinel */
    *piece_offset = 0;
    return (pt_piece_t *)&pt->sentinel;
}

static void recalc_totals(pt_table_t *pt) {
    size_t total_len = 0, total_lines = 1;
    pt_piece_t *p = pt->sentinel.next;
    while (p != &pt->sentinel) {
        total_len += p->length;
        total_lines += p->line_count;
        p = p->next;
    }
    pt->total_length = total_len;
    pt->total_lines = total_lines;
}

static void push_undo(pt_table_t *pt, pt_action_kind_t kind,
                      size_t offset, const char *text, size_t len) {
    pt_action_t *a = (pt_action_t *)calloc(1, sizeof(pt_action_t));
    a->kind = kind;
    a->offset = offset;
    a->length = len;
    a->text = (char *)malloc(len);
    memcpy(a->text, text, len);
    a->next = pt->undo_stack;
    pt->undo_stack = a;

    /* clear redo stack on new action */
    while (pt->redo_stack) {
        pt_action_t *r = pt->redo_stack;
        pt->redo_stack = r->next;
        free(r->text);
        free(r);
    }
}

static void free_action_stack(pt_action_t *stack) {
    while (stack) {
        pt_action_t *next = stack->next;
        free(stack->text);
        free(stack);
        stack = next;
    }
}

/* ---------- public API ---------- */

pt_table_t *pt_create(char *content, size_t length) {
    pt_table_t *pt = (pt_table_t *)calloc(1, sizeof(pt_table_t));
    pt->sentinel.next = &pt->sentinel;
    pt->sentinel.prev = &pt->sentinel;

    if (content && length > 0) {
        pt->original = content;
        pt->original_len = length;
        size_t nl = count_newlines(content, length);
        pt_piece_t *p = piece_new(PT_BUF_ORIGINAL, 0, length, nl);
        piece_insert_after(&pt->sentinel, p);
    }

    recalc_totals(pt);
    return pt;
}

void pt_destroy(pt_table_t *pt) {
    if (!pt) return;
    /* free pieces */
    pt_piece_t *p = pt->sentinel.next;
    while (p != &pt->sentinel) {
        pt_piece_t *next = p->next;
        free(p);
        p = next;
    }
    free(pt->original);
    free(pt->add_buf);
    free_action_stack(pt->undo_stack);
    free_action_stack(pt->redo_stack);
    free(pt);
}

void pt_insert(pt_table_t *pt, size_t offset, const char *text, size_t len) {
    if (len == 0) return;
    if (offset > pt->total_length) offset = pt->total_length;

    /* record undo */
    push_undo(pt, PT_ACTION_INSERT, offset, text, len);

    /* append to add buffer */
    size_t add_start = pt->add_len;
    add_buf_append(pt, text, len);
    size_t nl = count_newlines(text, len);
    pt_piece_t *new_piece = piece_new(PT_BUF_ADD, add_start, len, nl);

    if (pt->sentinel.next == &pt->sentinel) {
        /* empty document */
        piece_insert_after(&pt->sentinel, new_piece);
    } else {
        size_t po;
        pt_piece_t *p = find_piece(pt, offset, &po);

        if (p == &pt->sentinel) {
            /* insert at end */
            piece_insert_after(pt->sentinel.prev, new_piece);
        } else if (po == 0) {
            /* insert before this piece */
            piece_insert_after(p->prev, new_piece);
        } else if (po == p->length) {
            /* insert after this piece */
            piece_insert_after(p, new_piece);
        } else {
            /* split the piece */
            const char *pdata = piece_data(pt, p);
            size_t nl_left = count_newlines(pdata, po);
            size_t nl_right = p->line_count - nl_left;

            pt_piece_t *right = piece_new(p->buf, p->start + po,
                                          p->length - po, nl_right);
            p->length = po;
            p->line_count = nl_left;

            piece_insert_after(p, new_piece);
            piece_insert_after(new_piece, right);
        }
    }

    recalc_totals(pt);
}

void pt_delete(pt_table_t *pt, size_t offset, size_t len) {
    if (len == 0) return;
    if (offset + len > pt->total_length) {
        len = pt->total_length - offset;
    }

    /* save deleted text for undo */
    char *deleted = (char *)malloc(len);
    pt_get_text(pt, offset, len, deleted);
    push_undo(pt, PT_ACTION_DELETE, offset, deleted, len);
    free(deleted);

    size_t remaining = len;
    size_t po;
    pt_piece_t *p = find_piece(pt, offset, &po);

    while (remaining > 0 && p != &pt->sentinel) {
        size_t avail = p->length - po;
        pt_piece_t *next = p->next;

        if (po == 0 && remaining >= avail) {
            /* delete entire piece */
            remaining -= avail;
            piece_remove(p);
            free(p);
        } else if (po == 0) {
            /* delete from start of piece */
            const char *pdata = piece_data(pt, p);
            size_t removed_nl = count_newlines(pdata, remaining);
            p->start += remaining;
            p->length -= remaining;
            p->line_count -= removed_nl;
            remaining = 0;
        } else if (po + remaining >= avail) {
            /* delete from middle to end */
            size_t del = avail;
            const char *pdata = piece_data(pt, p);
            size_t removed_nl = count_newlines(pdata + po, del);
            p->length = po;
            p->line_count -= removed_nl;
            remaining -= del;
        } else {
            /* delete from middle — split */
            const char *pdata = piece_data(pt, p);
            size_t removed_nl = count_newlines(pdata + po, remaining);

            pt_piece_t *right = piece_new(p->buf, p->start + po + remaining,
                                          p->length - po - remaining, 0);
            right->line_count = count_newlines(
                piece_data(pt, p) + po + remaining,
                p->length - po - remaining);

            p->length = po;
            p->line_count = count_newlines(pdata, po);

            piece_insert_after(p, right);
            remaining = 0;
            (void)removed_nl;
        }

        po = 0;
        p = next;
    }

    recalc_totals(pt);
}

bool pt_undo(pt_table_t *pt) {
    pt_action_t *a = pt->undo_stack;
    if (!a) return false;
    pt->undo_stack = a->next;

    /* move to redo stack */
    a->next = pt->redo_stack;
    pt->redo_stack = a;

    if (a->kind == PT_ACTION_INSERT) {
        /* undo insert = delete (without recording undo) */
        /* temporarily detach undo stack to prevent recursive recording */
        pt_action_t *save_undo = pt->undo_stack;
        pt_action_t *save_redo = pt->redo_stack;
        pt->undo_stack = NULL;
        pt->redo_stack = NULL;
        pt_delete(pt, a->offset, a->length);
        /* discard the undo action that pt_delete created */
        free_action_stack(pt->undo_stack);
        pt->undo_stack = save_undo;
        pt->redo_stack = save_redo;
    } else {
        /* undo delete = insert */
        pt_action_t *save_undo = pt->undo_stack;
        pt_action_t *save_redo = pt->redo_stack;
        pt->undo_stack = NULL;
        pt->redo_stack = NULL;
        pt_insert(pt, a->offset, a->text, a->length);
        free_action_stack(pt->undo_stack);
        pt->undo_stack = save_undo;
        pt->redo_stack = save_redo;
    }
    return true;
}

bool pt_redo(pt_table_t *pt) {
    pt_action_t *a = pt->redo_stack;
    if (!a) return false;
    pt->redo_stack = a->next;

    /* move to undo stack */
    a->next = pt->undo_stack;
    pt->undo_stack = a;

    if (a->kind == PT_ACTION_INSERT) {
        pt_action_t *save_undo = pt->undo_stack;
        pt_action_t *save_redo = pt->redo_stack;
        pt->undo_stack = NULL;
        pt->redo_stack = NULL;
        pt_insert(pt, a->offset, a->text, a->length);
        free_action_stack(pt->undo_stack);
        pt->undo_stack = save_undo;
        pt->redo_stack = save_redo;
    } else {
        pt_action_t *save_undo = pt->undo_stack;
        pt_action_t *save_redo = pt->redo_stack;
        pt->undo_stack = NULL;
        pt->redo_stack = NULL;
        pt_delete(pt, a->offset, a->length);
        free_action_stack(pt->undo_stack);
        pt->undo_stack = save_undo;
        pt->redo_stack = save_redo;
    }
    return true;
}

size_t pt_length(const pt_table_t *pt) {
    return pt->total_length;
}

size_t pt_line_count(const pt_table_t *pt) {
    return pt->total_lines;
}

size_t pt_get_text(const pt_table_t *pt, size_t offset, size_t len, char *dst) {
    if (offset >= pt->total_length) return 0;
    if (offset + len > pt->total_length) len = pt->total_length - offset;

    size_t copied = 0;
    size_t pos = 0;
    pt_piece_t *p = pt->sentinel.next;

    while (p != &pt->sentinel && copied < len) {
        if (pos + p->length > offset) {
            size_t skip = (offset > pos) ? offset - pos : 0;
            size_t avail = p->length - skip;
            size_t to_copy = (len - copied < avail) ? len - copied : avail;
            const char *src = piece_data(pt, p) + skip;
            memcpy(dst + copied, src, to_copy);
            copied += to_copy;
            offset += to_copy;
        }
        pos += p->length;
        p = p->next;
    }
    return copied;
}

void pt_offset_to_pos(const pt_table_t *pt, size_t offset,
                      size_t *out_line, size_t *out_col) {
    size_t line = 0, col = 0, pos = 0;
    pt_piece_t *p = pt->sentinel.next;

    while (p != &pt->sentinel) {
        const char *data = piece_data(pt, p);
        for (size_t i = 0; i < p->length && pos < offset; i++, pos++) {
            if (data[i] == '\n') {
                line++;
                col = 0;
            } else {
                col++;
            }
        }
        if (pos >= offset) break;
        p = p->next;
    }

    *out_line = line;
    *out_col = col;
}

size_t pt_pos_to_offset(const pt_table_t *pt, size_t line, size_t col) {
    size_t cur_line = 0, offset = 0;
    pt_piece_t *p = pt->sentinel.next;

    while (p != &pt->sentinel) {
        const char *data = piece_data(pt, p);
        for (size_t i = 0; i < p->length; i++) {
            if (cur_line == line) {
                if (col == 0) return offset;
                col--;
                offset++;
                if (data[i] == '\n') return offset - 1;
            } else {
                if (data[i] == '\n') cur_line++;
                offset++;
            }
        }
        if (cur_line == line && col == 0) return offset;
        p = p->next;
    }
    return offset;
}

size_t pt_line_start(const pt_table_t *pt, size_t line) {
    return pt_pos_to_offset(pt, line, 0);
}

size_t pt_line_length(const pt_table_t *pt, size_t line) {
    size_t start = pt_line_start(pt, line);
    size_t next_start;
    if (line + 1 < pt->total_lines) {
        next_start = pt_line_start(pt, line + 1);
    } else {
        next_start = pt->total_length;
    }
    return next_start - start;
}

char *pt_to_string(const pt_table_t *pt) {
    char *s = (char *)malloc(pt->total_length + 1);
    pt_get_text(pt, 0, pt->total_length, s);
    s[pt->total_length] = '\0';
    return s;
}
