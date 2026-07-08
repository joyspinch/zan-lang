/* editor.h -- Editor state management for the Zan IDE.
 *
 * Wraps piece_table with cursor, selection, viewport, and tab management.
 */
#ifndef ZAN_EDITOR_H
#define ZAN_EDITOR_H

#include "piece_table.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Maximum open tabs */
#define EDITOR_MAX_TABS 64

/* Syntax token kinds for highlighting */
typedef enum {
    SYN_NONE,
    SYN_KEYWORD,
    SYN_TYPE,
    SYN_STRING,
    SYN_NUMBER,
    SYN_COMMENT,
    SYN_OPERATOR,
    SYN_IDENTIFIER,
    SYN_PREPROCESSOR,
    SYN_BUILTIN,
    SYN_STRING_INTERP,      /* interpolated string $"..." */
    SYN_STRING_VERBATIM,    /* verbatim string @"..." */
    SYN_ATTRIBUTE,          /* [Attribute] */
    SYN_GENERIC_PARAM,      /* <T> generic type params */
    SYN_NAMESPACE_REF,      /* namespace.Type references */
    SYN_ENUM_MEMBER,        /* enum member names */
    SYN_PARAM_NAME          /* named parameter in calls */
} syn_kind_t;

/* A syntax-highlighted span within a line */
typedef struct {
    int     col_start;
    int     col_end;
    syn_kind_t kind;
} syn_span_t;

/* State carried from previous line for multi-line constructs */
typedef enum {
    SYN_STATE_NORMAL = 0,
    SYN_STATE_BLOCK_COMMENT,   /* inside / * ... * / */
    SYN_STATE_VERBATIM_STRING, /* inside @"..." */
    SYN_STATE_RAW_STRING       /* inside """...""" */
} syn_line_state_t;

/* Highlighted line cache */
typedef struct {
    syn_span_t     *spans;
    int             span_count;
    int             span_cap;
    bool            dirty;
    syn_line_state_t start_state;  /* state at beginning of this line */
    syn_line_state_t end_state;    /* state at end of this line */
} syn_line_t;

/* A single editor tab */
typedef struct {
    char         filepath[512];    /* full path to file */
    char         title[64];        /* display name (filename) */
    pt_table_t  *buffer;           /* piece table text buffer */
    bool         modified;         /* has unsaved changes */
    bool         is_new;           /* untitled new file */

    /* cursor position (0-based) */
    size_t       cursor_line;
    size_t       cursor_col;

    /* selection anchor (-1 = no selection) */
    int          sel_anchor_line;
    int          sel_anchor_col;

    /* viewport */
    size_t       scroll_line;      /* first visible line */
    size_t       scroll_col;       /* horizontal scroll offset */

    /* syntax highlighting cache */
    syn_line_t  *syn_cache;
    size_t       syn_cache_count;
    size_t       syn_cache_cap;
} editor_tab_t;

/* Search state */
typedef struct {
    char    query[256];
    bool    case_sensitive;
    bool    active;
    size_t  match_line;
    size_t  match_col;
    size_t  match_len;
} search_state_t;

/* Main editor state */
typedef struct {
    editor_tab_t   tabs[EDITOR_MAX_TABS];
    int             tab_count;
    int             active_tab;

    search_state_t  search;

    /* editor settings */
    int             tab_size;
    bool            use_spaces;
    bool            show_line_numbers;
    bool            word_wrap;

    /* viewport dimensions (in characters) */
    int             view_cols;
    int             view_rows;
} editor_t;

/* Initialize editor state */
void editor_init(editor_t *ed);

/* Destroy editor state */
void editor_destroy(editor_t *ed);

/* Open a file in a new tab. Returns tab index or -1 on error. */
int editor_open_file(editor_t *ed, const char *filepath);

/* Create a new untitled tab. Returns tab index. */
int editor_new_file(editor_t *ed);

/* Save the current tab. Returns true on success. */
bool editor_save(editor_t *ed);

/* Save the current tab to a new path. Returns true on success. */
bool editor_save_as(editor_t *ed, const char *filepath);

/* Close a tab. Returns true if closed (handles unsaved prompt). */
bool editor_close_tab(editor_t *ed, int tab_idx);

/* Get the active tab (NULL if none). */
editor_tab_t *editor_active(editor_t *ed);

/* --- Cursor movement --- */
void editor_move_left(editor_t *ed);
void editor_move_right(editor_t *ed);
void editor_move_up(editor_t *ed);
void editor_move_down(editor_t *ed);
void editor_move_home(editor_t *ed);
void editor_move_end(editor_t *ed);
void editor_move_page_up(editor_t *ed);
void editor_move_page_down(editor_t *ed);
void editor_move_word_left(editor_t *ed);
void editor_move_word_right(editor_t *ed);

/* --- Text editing --- */
void editor_insert_char(editor_t *ed, char ch);
void editor_insert_text(editor_t *ed, const char *text, size_t len);
void editor_insert_newline(editor_t *ed);
void editor_backspace(editor_t *ed);
void editor_delete_char(editor_t *ed);
void editor_indent(editor_t *ed);

/* --- Selection --- */
void editor_select_all(editor_t *ed);
void editor_start_selection(editor_t *ed);
void editor_clear_selection(editor_t *ed);
bool editor_has_selection(editor_t *ed);
char *editor_get_selection(editor_t *ed, size_t *out_len);
void editor_delete_selection(editor_t *ed);

/* --- Clipboard --- */
void editor_copy(editor_t *ed);
void editor_cut(editor_t *ed);
void editor_paste(editor_t *ed);

/* --- Undo/Redo --- */
void editor_undo(editor_t *ed);
void editor_redo(editor_t *ed);

/* --- Search --- */
bool editor_find(editor_t *ed, const char *query, bool case_sensitive);
bool editor_find_next(editor_t *ed);
void editor_replace(editor_t *ed, const char *replacement);
void editor_replace_all(editor_t *ed, const char *query,
                        const char *replacement);

/* --- Syntax highlighting --- */
void editor_highlight_line(editor_t *ed, size_t line);
void editor_highlight_visible(editor_t *ed);
void editor_invalidate_syntax(editor_t *ed, size_t from_line);

/* --- Viewport --- */
void editor_ensure_cursor_visible(editor_t *ed);
void editor_scroll_to(editor_t *ed, size_t line);

#ifdef __cplusplus
}
#endif

#endif /* ZAN_EDITOR_H */
