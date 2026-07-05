/* editor.c -- Editor state management implementation. */
#include "editor.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

/* Zan keywords for syntax highlighting */
static const char *zan_keywords[] = {
    "abstract", "as", "async", "await", "base", "bool", "break", "byte",
    "case", "catch", "char", "class", "const", "continue", "decimal",
    "default", "delegate", "do", "double", "else", "enum", "event",
    "explicit", "extern", "false", "finally", "fixed", "float", "for",
    "foreach", "goto", "if", "implicit", "in", "int", "interface",
    "internal", "is", "lock", "long", "namespace", "new", "null",
    "object", "operator", "out", "override", "params", "partial",
    "private", "protected", "public", "readonly", "ref", "return",
    "sbyte", "sealed", "short", "sizeof", "stackalloc", "static",
    "string", "struct", "switch", "this", "throw", "true", "try",
    "typeof", "uint", "ulong", "unchecked", "unsafe", "ushort",
    "using", "var", "virtual", "void", "volatile", "while", "yield",
    NULL
};

static const char *zan_types[] = {
    "int", "float", "double", "bool", "string", "char", "byte", "long",
    "short", "void", "var", "object", "decimal", "uint", "ulong",
    "ushort", "sbyte", NULL
};

static bool is_keyword(const char *word, int len) {
    for (int i = 0; zan_keywords[i]; i++) {
        if ((int)strlen(zan_keywords[i]) == len &&
            memcmp(zan_keywords[i], word, (size_t)len) == 0)
            return true;
    }
    return false;
}

static bool is_type_name(const char *word, int len) {
    for (int i = 0; zan_types[i]; i++) {
        if ((int)strlen(zan_types[i]) == len &&
            memcmp(zan_types[i], word, (size_t)len) == 0)
            return true;
    }
    return false;
}

void editor_init(editor_t *ed) {
    memset(ed, 0, sizeof(editor_t));
    ed->tab_size = 4;
    ed->use_spaces = true;
    ed->show_line_numbers = true;
    ed->word_wrap = false;
    ed->view_cols = 80;
    ed->view_rows = 25;
    ed->active_tab = -1;
}

void editor_destroy(editor_t *ed) {
    for (int i = 0; i < ed->tab_count; i++) {
        if (ed->tabs[i].buffer) {
            pt_destroy(ed->tabs[i].buffer);
        }
        if (ed->tabs[i].syn_cache) {
            for (size_t j = 0; j < ed->tabs[i].syn_cache_count; j++) {
                free(ed->tabs[i].syn_cache[j].spans);
            }
            free(ed->tabs[i].syn_cache);
        }
    }
}

int editor_open_file(editor_t *ed, const char *filepath) {
    if (ed->tab_count >= EDITOR_MAX_TABS) return -1;

    FILE *f = fopen(filepath, "rb");
    if (!f) return -1;

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *content = NULL;
    size_t len = 0;
    if (size > 0) {
        content = (char *)malloc((size_t)size);
        len = fread(content, 1, (size_t)size, f);
    }
    fclose(f);

    int idx = ed->tab_count++;
    editor_tab_t *tab = &ed->tabs[idx];
    memset(tab, 0, sizeof(editor_tab_t));

    strncpy(tab->filepath, filepath, sizeof(tab->filepath) - 1);

    /* extract filename for title */
    const char *name = strrchr(filepath, '\\');
    if (!name) name = strrchr(filepath, '/');
    name = name ? name + 1 : filepath;
    strncpy(tab->title, name, sizeof(tab->title) - 1);

    tab->buffer = pt_create(content, len);
    tab->modified = false;
    tab->is_new = false;
    tab->sel_anchor_line = -1;
    tab->sel_anchor_col = -1;

    ed->active_tab = idx;
    editor_highlight_visible(ed);
    return idx;
}

int editor_new_file(editor_t *ed) {
    if (ed->tab_count >= EDITOR_MAX_TABS) return -1;

    int idx = ed->tab_count++;
    editor_tab_t *tab = &ed->tabs[idx];
    memset(tab, 0, sizeof(editor_tab_t));

    snprintf(tab->title, sizeof(tab->title), "Untitled-%d", idx + 1);
    tab->buffer = pt_create(NULL, 0);
    tab->modified = false;
    tab->is_new = true;
    tab->sel_anchor_line = -1;
    tab->sel_anchor_col = -1;

    ed->active_tab = idx;
    return idx;
}

bool editor_save(editor_t *ed) {
    editor_tab_t *tab = editor_active(ed);
    if (!tab) return false;
    if (tab->is_new) return false; /* need save_as */

    FILE *f = fopen(tab->filepath, "wb");
    if (!f) return false;

    char *text = pt_to_string(tab->buffer);
    size_t len = pt_length(tab->buffer);
    fwrite(text, 1, len, f);
    free(text);
    fclose(f);

    tab->modified = false;
    return true;
}

bool editor_save_as(editor_t *ed, const char *filepath) {
    editor_tab_t *tab = editor_active(ed);
    if (!tab) return false;

    strncpy(tab->filepath, filepath, sizeof(tab->filepath) - 1);
    const char *name = strrchr(filepath, '\\');
    if (!name) name = strrchr(filepath, '/');
    name = name ? name + 1 : filepath;
    strncpy(tab->title, name, sizeof(tab->title) - 1);
    tab->is_new = false;

    return editor_save(ed);
}

bool editor_close_tab(editor_t *ed, int tab_idx) {
    if (tab_idx < 0 || tab_idx >= ed->tab_count) return false;

    editor_tab_t *tab = &ed->tabs[tab_idx];
    if (tab->buffer) pt_destroy(tab->buffer);
    if (tab->syn_cache) {
        for (size_t j = 0; j < tab->syn_cache_count; j++)
            free(tab->syn_cache[j].spans);
        free(tab->syn_cache);
    }

    /* shift remaining tabs */
    for (int i = tab_idx; i < ed->tab_count - 1; i++) {
        ed->tabs[i] = ed->tabs[i + 1];
    }
    ed->tab_count--;

    if (ed->active_tab >= ed->tab_count)
        ed->active_tab = ed->tab_count - 1;

    return true;
}

editor_tab_t *editor_active(editor_t *ed) {
    if (ed->active_tab < 0 || ed->active_tab >= ed->tab_count) return NULL;
    return &ed->tabs[ed->active_tab];
}

/* --- Cursor movement --- */

void editor_move_left(editor_t *ed) {
    editor_tab_t *tab = editor_active(ed);
    if (!tab) return;
    editor_clear_selection(ed);
    if (tab->cursor_col > 0) {
        tab->cursor_col--;
    } else if (tab->cursor_line > 0) {
        tab->cursor_line--;
        size_t llen = pt_line_length(tab->buffer, tab->cursor_line);
        tab->cursor_col = llen > 0 ? llen - 1 : 0;
    }
    editor_ensure_cursor_visible(ed);
}

void editor_move_right(editor_t *ed) {
    editor_tab_t *tab = editor_active(ed);
    if (!tab) return;
    editor_clear_selection(ed);
    size_t llen = pt_line_length(tab->buffer, tab->cursor_line);
    size_t max_col = llen > 0 ? llen - 1 : 0;
    if (tab->cursor_col < max_col) {
        tab->cursor_col++;
    } else if (tab->cursor_line + 1 < pt_line_count(tab->buffer)) {
        tab->cursor_line++;
        tab->cursor_col = 0;
    }
    editor_ensure_cursor_visible(ed);
}

void editor_move_up(editor_t *ed) {
    editor_tab_t *tab = editor_active(ed);
    if (!tab) return;
    editor_clear_selection(ed);
    if (tab->cursor_line > 0) {
        tab->cursor_line--;
        size_t llen = pt_line_length(tab->buffer, tab->cursor_line);
        if (tab->cursor_col > llen) tab->cursor_col = llen > 0 ? llen - 1 : 0;
    }
    editor_ensure_cursor_visible(ed);
}

void editor_move_down(editor_t *ed) {
    editor_tab_t *tab = editor_active(ed);
    if (!tab) return;
    editor_clear_selection(ed);
    if (tab->cursor_line + 1 < pt_line_count(tab->buffer)) {
        tab->cursor_line++;
        size_t llen = pt_line_length(tab->buffer, tab->cursor_line);
        if (tab->cursor_col > llen) tab->cursor_col = llen > 0 ? llen - 1 : 0;
    }
    editor_ensure_cursor_visible(ed);
}

void editor_move_home(editor_t *ed) {
    editor_tab_t *tab = editor_active(ed);
    if (!tab) return;
    editor_clear_selection(ed);
    tab->cursor_col = 0;
    editor_ensure_cursor_visible(ed);
}

void editor_move_end(editor_t *ed) {
    editor_tab_t *tab = editor_active(ed);
    if (!tab) return;
    editor_clear_selection(ed);
    size_t llen = pt_line_length(tab->buffer, tab->cursor_line);
    tab->cursor_col = llen > 0 ? llen - 1 : 0;
    editor_ensure_cursor_visible(ed);
}

void editor_move_page_up(editor_t *ed) {
    editor_tab_t *tab = editor_active(ed);
    if (!tab) return;
    editor_clear_selection(ed);
    if (tab->cursor_line >= (size_t)ed->view_rows)
        tab->cursor_line -= (size_t)ed->view_rows;
    else
        tab->cursor_line = 0;
    editor_ensure_cursor_visible(ed);
}

void editor_move_page_down(editor_t *ed) {
    editor_tab_t *tab = editor_active(ed);
    if (!tab) return;
    editor_clear_selection(ed);
    tab->cursor_line += (size_t)ed->view_rows;
    size_t max_line = pt_line_count(tab->buffer);
    if (max_line > 0 && tab->cursor_line >= max_line)
        tab->cursor_line = max_line - 1;
    editor_ensure_cursor_visible(ed);
}

void editor_move_word_left(editor_t *ed) {
    editor_tab_t *tab = editor_active(ed);
    if (!tab) return;
    editor_clear_selection(ed);
    if (tab->cursor_col == 0) {
        editor_move_left(ed);
        return;
    }
    /* get line text */
    size_t llen = pt_line_length(tab->buffer, tab->cursor_line);
    char *line = (char *)malloc(llen + 1);
    size_t ls = pt_line_start(tab->buffer, tab->cursor_line);
    pt_get_text(tab->buffer, ls, llen, line);
    line[llen] = '\0';

    size_t col = tab->cursor_col;
    /* skip whitespace backward */
    while (col > 0 && isspace((unsigned char)line[col - 1])) col--;
    /* skip word backward */
    while (col > 0 && isalnum((unsigned char)line[col - 1])) col--;
    tab->cursor_col = col;
    free(line);
    editor_ensure_cursor_visible(ed);
}

void editor_move_word_right(editor_t *ed) {
    editor_tab_t *tab = editor_active(ed);
    if (!tab) return;
    editor_clear_selection(ed);

    size_t llen = pt_line_length(tab->buffer, tab->cursor_line);
    char *line = (char *)malloc(llen + 1);
    size_t ls = pt_line_start(tab->buffer, tab->cursor_line);
    pt_get_text(tab->buffer, ls, llen, line);
    line[llen] = '\0';

    size_t col = tab->cursor_col;
    /* skip word forward */
    while (col < llen && isalnum((unsigned char)line[col])) col++;
    /* skip whitespace forward */
    while (col < llen && isspace((unsigned char)line[col])) col++;
    tab->cursor_col = col;
    free(line);
    editor_ensure_cursor_visible(ed);
}

/* --- Text editing --- */

void editor_insert_char(editor_t *ed, char ch) {
    editor_tab_t *tab = editor_active(ed);
    if (!tab) return;
    if (editor_has_selection(ed)) editor_delete_selection(ed);

    size_t off = pt_pos_to_offset(tab->buffer, tab->cursor_line, tab->cursor_col);
    pt_insert(tab->buffer, off, &ch, 1);
    tab->cursor_col++;
    tab->modified = true;
    editor_invalidate_syntax(ed, tab->cursor_line);
    editor_ensure_cursor_visible(ed);
}

void editor_insert_text(editor_t *ed, const char *text, size_t len) {
    editor_tab_t *tab = editor_active(ed);
    if (!tab) return;
    if (editor_has_selection(ed)) editor_delete_selection(ed);

    size_t off = pt_pos_to_offset(tab->buffer, tab->cursor_line, tab->cursor_col);
    pt_insert(tab->buffer, off, text, len);

    /* advance cursor */
    for (size_t i = 0; i < len; i++) {
        if (text[i] == '\n') {
            tab->cursor_line++;
            tab->cursor_col = 0;
        } else {
            tab->cursor_col++;
        }
    }
    tab->modified = true;
    editor_invalidate_syntax(ed, 0);
    editor_ensure_cursor_visible(ed);
}

void editor_insert_newline(editor_t *ed) {
    editor_tab_t *tab = editor_active(ed);
    if (!tab) return;
    if (editor_has_selection(ed)) editor_delete_selection(ed);

    /* auto-indent: get leading whitespace of current line */
    size_t llen = pt_line_length(tab->buffer, tab->cursor_line);
    char *line = (char *)malloc(llen + 1);
    size_t ls = pt_line_start(tab->buffer, tab->cursor_line);
    pt_get_text(tab->buffer, ls, llen, line);
    line[llen] = '\0';

    size_t indent = 0;
    while (indent < llen && (line[indent] == ' ' || line[indent] == '\t'))
        indent++;

    char *nl_buf = (char *)malloc(1 + indent);
    nl_buf[0] = '\n';
    memcpy(nl_buf + 1, line, indent);
    free(line);

    size_t off = pt_pos_to_offset(tab->buffer, tab->cursor_line, tab->cursor_col);
    pt_insert(tab->buffer, off, nl_buf, 1 + indent);
    tab->cursor_line++;
    tab->cursor_col = indent;
    tab->modified = true;
    free(nl_buf);
    editor_invalidate_syntax(ed, tab->cursor_line > 0 ? tab->cursor_line - 1 : 0);
    editor_ensure_cursor_visible(ed);
}

void editor_backspace(editor_t *ed) {
    editor_tab_t *tab = editor_active(ed);
    if (!tab) return;
    if (editor_has_selection(ed)) {
        editor_delete_selection(ed);
        return;
    }
    size_t off = pt_pos_to_offset(tab->buffer, tab->cursor_line, tab->cursor_col);
    if (off == 0) return;

    editor_move_left(ed);
    pt_delete(tab->buffer, off - 1, 1);
    tab->modified = true;
    editor_invalidate_syntax(ed, tab->cursor_line);
}

void editor_delete_char(editor_t *ed) {
    editor_tab_t *tab = editor_active(ed);
    if (!tab) return;
    if (editor_has_selection(ed)) {
        editor_delete_selection(ed);
        return;
    }
    size_t off = pt_pos_to_offset(tab->buffer, tab->cursor_line, tab->cursor_col);
    if (off >= pt_length(tab->buffer)) return;

    pt_delete(tab->buffer, off, 1);
    tab->modified = true;
    editor_invalidate_syntax(ed, tab->cursor_line);
}

void editor_indent(editor_t *ed) {
    editor_tab_t *tab = editor_active(ed);
    if (!tab) return;
    if (ed->use_spaces) {
        int spaces = ed->tab_size - (int)(tab->cursor_col % (size_t)ed->tab_size);
        for (int i = 0; i < spaces; i++)
            editor_insert_char(ed, ' ');
    } else {
        editor_insert_char(ed, '\t');
    }
}

/* --- Selection --- */

void editor_select_all(editor_t *ed) {
    editor_tab_t *tab = editor_active(ed);
    if (!tab) return;
    tab->sel_anchor_line = 0;
    tab->sel_anchor_col = 0;
    size_t last_line = pt_line_count(tab->buffer);
    if (last_line > 0) last_line--;
    tab->cursor_line = last_line;
    tab->cursor_col = pt_line_length(tab->buffer, last_line);
}

void editor_start_selection(editor_t *ed) {
    editor_tab_t *tab = editor_active(ed);
    if (!tab) return;
    if (tab->sel_anchor_line < 0) {
        tab->sel_anchor_line = (int)tab->cursor_line;
        tab->sel_anchor_col = (int)tab->cursor_col;
    }
}

void editor_clear_selection(editor_t *ed) {
    editor_tab_t *tab = editor_active(ed);
    if (!tab) return;
    tab->sel_anchor_line = -1;
    tab->sel_anchor_col = -1;
}

bool editor_has_selection(editor_t *ed) {
    editor_tab_t *tab = editor_active(ed);
    if (!tab) return false;
    return tab->sel_anchor_line >= 0;
}

char *editor_get_selection(editor_t *ed, size_t *out_len) {
    editor_tab_t *tab = editor_active(ed);
    if (!tab || tab->sel_anchor_line < 0) { *out_len = 0; return NULL; }

    size_t start = pt_pos_to_offset(tab->buffer,
        (size_t)tab->sel_anchor_line, (size_t)tab->sel_anchor_col);
    size_t end = pt_pos_to_offset(tab->buffer,
        tab->cursor_line, tab->cursor_col);
    if (start > end) { size_t t = start; start = end; end = t; }

    size_t len = end - start;
    char *text = (char *)malloc(len + 1);
    pt_get_text(tab->buffer, start, len, text);
    text[len] = '\0';
    *out_len = len;
    return text;
}

void editor_delete_selection(editor_t *ed) {
    editor_tab_t *tab = editor_active(ed);
    if (!tab || tab->sel_anchor_line < 0) return;

    size_t start = pt_pos_to_offset(tab->buffer,
        (size_t)tab->sel_anchor_line, (size_t)tab->sel_anchor_col);
    size_t end = pt_pos_to_offset(tab->buffer,
        tab->cursor_line, tab->cursor_col);
    if (start > end) { size_t t = start; start = end; end = t; }

    size_t len = end - start;
    pt_delete(tab->buffer, start, len);

    pt_offset_to_pos(tab->buffer, start, &tab->cursor_line, &tab->cursor_col);
    tab->modified = true;
    editor_clear_selection(ed);
    editor_invalidate_syntax(ed, tab->cursor_line);
}

/* --- Clipboard (platform-specific, stub for now) --- */

#ifdef _WIN32
#include <windows.h>
void editor_copy(editor_t *ed) {
    size_t len;
    char *text = editor_get_selection(ed, &len);
    if (!text || len == 0) { free(text); return; }

    if (OpenClipboard(NULL)) {
        EmptyClipboard();
        HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, len + 1);
        if (hg) {
            char *dst = (char *)GlobalLock(hg);
            memcpy(dst, text, len);
            dst[len] = '\0';
            GlobalUnlock(hg);
            SetClipboardData(CF_TEXT, hg);
        }
        CloseClipboard();
    }
    free(text);
}

void editor_cut(editor_t *ed) {
    editor_copy(ed);
    editor_delete_selection(ed);
}

void editor_paste(editor_t *ed) {
    if (!IsClipboardFormatAvailable(CF_TEXT)) return;
    if (!OpenClipboard(NULL)) return;

    HGLOBAL hg = GetClipboardData(CF_TEXT);
    if (hg) {
        char *text = (char *)GlobalLock(hg);
        if (text) {
            editor_insert_text(ed, text, strlen(text));
        }
        GlobalUnlock(hg);
    }
    CloseClipboard();
}
#else
/* Non-Windows stubs */
void editor_copy(editor_t *ed) { (void)ed; }
void editor_cut(editor_t *ed) { (void)ed; }
void editor_paste(editor_t *ed) { (void)ed; }
#endif

/* --- Undo/Redo --- */

void editor_undo(editor_t *ed) {
    editor_tab_t *tab = editor_active(ed);
    if (!tab) return;
    if (pt_undo(tab->buffer)) {
        tab->modified = true;
        editor_invalidate_syntax(ed, 0);
    }
}

void editor_redo(editor_t *ed) {
    editor_tab_t *tab = editor_active(ed);
    if (!tab) return;
    if (pt_redo(tab->buffer)) {
        tab->modified = true;
        editor_invalidate_syntax(ed, 0);
    }
}

/* --- Search --- */

bool editor_find(editor_t *ed, const char *query, bool case_sensitive) {
    strncpy(ed->search.query, query, sizeof(ed->search.query) - 1);
    ed->search.case_sensitive = case_sensitive;
    ed->search.active = true;
    return editor_find_next(ed);
}

bool editor_find_next(editor_t *ed) {
    editor_tab_t *tab = editor_active(ed);
    if (!tab || !ed->search.active) return false;

    char *doc = pt_to_string(tab->buffer);
    size_t doc_len = pt_length(tab->buffer);
    size_t qlen = strlen(ed->search.query);
    if (qlen == 0) { free(doc); return false; }

    size_t start = pt_pos_to_offset(tab->buffer,
        tab->cursor_line, tab->cursor_col);

    for (size_t i = start; i + qlen <= doc_len; i++) {
        bool match = true;
        for (size_t j = 0; j < qlen; j++) {
            char a = doc[i + j], b = ed->search.query[j];
            if (!ed->search.case_sensitive) {
                a = (char)tolower((unsigned char)a);
                b = (char)tolower((unsigned char)b);
            }
            if (a != b) { match = false; break; }
        }
        if (match) {
            pt_offset_to_pos(tab->buffer, i,
                &ed->search.match_line, &ed->search.match_col);
            ed->search.match_len = qlen;
            tab->cursor_line = ed->search.match_line;
            tab->cursor_col = ed->search.match_col + qlen;
            tab->sel_anchor_line = (int)ed->search.match_line;
            tab->sel_anchor_col = (int)ed->search.match_col;
            editor_ensure_cursor_visible(ed);
            free(doc);
            return true;
        }
    }
    free(doc);
    return false;
}

void editor_replace(editor_t *ed, const char *replacement) {
    if (!editor_has_selection(ed)) return;
    editor_delete_selection(ed);
    editor_insert_text(ed, replacement, strlen(replacement));
}

void editor_replace_all(editor_t *ed, const char *query,
                        const char *replacement) {
    editor_tab_t *tab = editor_active(ed);
    if (!tab) return;

    char *doc = pt_to_string(tab->buffer);
    size_t doc_len = pt_length(tab->buffer);
    size_t qlen = strlen(query);
    size_t rlen = strlen(replacement);
    if (qlen == 0) { free(doc); return; }

    /* count occurrences and build new doc */
    size_t new_len = 0;
    size_t count = 0;
    for (size_t i = 0; i < doc_len; ) {
        if (i + qlen <= doc_len && memcmp(doc + i, query, qlen) == 0) {
            new_len += rlen;
            i += qlen;
            count++;
        } else {
            new_len++;
            i++;
        }
    }

    if (count == 0) { free(doc); return; }

    char *new_doc = (char *)malloc(new_len);
    size_t j = 0;
    for (size_t i = 0; i < doc_len; ) {
        if (i + qlen <= doc_len && memcmp(doc + i, query, qlen) == 0) {
            memcpy(new_doc + j, replacement, rlen);
            j += rlen;
            i += qlen;
        } else {
            new_doc[j++] = doc[i++];
        }
    }

    /* replace entire buffer */
    pt_delete(tab->buffer, 0, doc_len);
    pt_insert(tab->buffer, 0, new_doc, new_len);
    tab->cursor_line = 0;
    tab->cursor_col = 0;
    tab->modified = true;
    editor_invalidate_syntax(ed, 0);

    free(doc);
    free(new_doc);
}

/* --- Syntax highlighting --- */

static void ensure_syn_cache(editor_tab_t *tab, size_t lines) {
    if (lines <= tab->syn_cache_cap) return;
    size_t new_cap = lines + 64;
    tab->syn_cache = (syn_line_t *)realloc(tab->syn_cache,
        new_cap * sizeof(syn_line_t));
    for (size_t i = tab->syn_cache_cap; i < new_cap; i++) {
        tab->syn_cache[i].spans = NULL;
        tab->syn_cache[i].span_count = 0;
        tab->syn_cache[i].span_cap = 0;
        tab->syn_cache[i].dirty = true;
    }
    tab->syn_cache_cap = new_cap;
    tab->syn_cache_count = lines;
}

static void add_span(syn_line_t *sl, int col_start, int col_end, syn_kind_t kind) {
    if (sl->span_count >= sl->span_cap) {
        sl->span_cap = sl->span_cap ? sl->span_cap * 2 : 16;
        sl->spans = (syn_span_t *)realloc(sl->spans,
            (size_t)sl->span_cap * sizeof(syn_span_t));
    }
    sl->spans[sl->span_count].col_start = col_start;
    sl->spans[sl->span_count].col_end = col_end;
    sl->spans[sl->span_count].kind = kind;
    sl->span_count++;
}

void editor_highlight_line(editor_t *ed, size_t line) {
    editor_tab_t *tab = editor_active(ed);
    if (!tab) return;

    size_t total = pt_line_count(tab->buffer);
    if (line >= total) return;
    ensure_syn_cache(tab, total);

    syn_line_t *sl = &tab->syn_cache[line];
    sl->span_count = 0;

    size_t llen = pt_line_length(tab->buffer, line);
    if (llen == 0) { sl->dirty = false; return; }

    char *buf = (char *)malloc(llen + 1);
    size_t ls = pt_line_start(tab->buffer, line);
    pt_get_text(tab->buffer, ls, llen, buf);
    buf[llen] = '\0';

    int i = 0;
    int len = (int)llen;

    while (i < len) {
        /* skip whitespace */
        if (isspace((unsigned char)buf[i])) { i++; continue; }

        /* single-line comment */
        if (i + 1 < len && buf[i] == '/' && buf[i + 1] == '/') {
            add_span(sl, i, len, SYN_COMMENT);
            break;
        }

        /* string literal */
        if (buf[i] == '"' || buf[i] == '\'') {
            char quote = buf[i];
            int start = i++;
            while (i < len && buf[i] != quote) {
                if (buf[i] == '\\' && i + 1 < len) i++;
                i++;
            }
            if (i < len) i++; /* skip closing quote */
            add_span(sl, start, i, SYN_STRING);
            continue;
        }

        /* number */
        if (isdigit((unsigned char)buf[i]) ||
            (buf[i] == '.' && i + 1 < len && isdigit((unsigned char)buf[i + 1]))) {
            int start = i;
            while (i < len && (isdigit((unsigned char)buf[i]) ||
                               buf[i] == '.' || buf[i] == 'x' ||
                               buf[i] == 'f' ||
                               (buf[i] >= 'a' && buf[i] <= 'f') ||
                               (buf[i] >= 'A' && buf[i] <= 'F')))
                i++;
            add_span(sl, start, i, SYN_NUMBER);
            continue;
        }

        /* identifier or keyword */
        if (isalpha((unsigned char)buf[i]) || buf[i] == '_') {
            int start = i;
            while (i < len && (isalnum((unsigned char)buf[i]) || buf[i] == '_'))
                i++;
            int wlen = i - start;
            if (is_type_name(buf + start, wlen))
                add_span(sl, start, i, SYN_TYPE);
            else if (is_keyword(buf + start, wlen))
                add_span(sl, start, i, SYN_KEYWORD);
            else if (i < len && buf[i] == '(')
                add_span(sl, start, i, SYN_BUILTIN);
            else
                add_span(sl, start, i, SYN_IDENTIFIER);
            continue;
        }

        /* operator */
        if (strchr("+-*/%=<>!&|^~?:.;,{}[]()", buf[i])) {
            add_span(sl, i, i + 1, SYN_OPERATOR);
            i++;
            continue;
        }

        /* anything else */
        i++;
    }

    free(buf);
    sl->dirty = false;
}

void editor_highlight_visible(editor_t *ed) {
    editor_tab_t *tab = editor_active(ed);
    if (!tab) return;

    size_t first = tab->scroll_line;
    size_t last = first + (size_t)ed->view_rows + 1;
    size_t total = pt_line_count(tab->buffer);
    if (last > total) last = total;

    for (size_t i = first; i < last; i++) {
        editor_highlight_line(ed, i);
    }
}

void editor_invalidate_syntax(editor_t *ed, size_t from_line) {
    editor_tab_t *tab = editor_active(ed);
    if (!tab || !tab->syn_cache) return;
    for (size_t i = from_line; i < tab->syn_cache_count; i++) {
        tab->syn_cache[i].dirty = true;
    }
}

/* --- Viewport --- */

void editor_ensure_cursor_visible(editor_t *ed) {
    editor_tab_t *tab = editor_active(ed);
    if (!tab) return;

    /* vertical */
    if (tab->cursor_line < tab->scroll_line)
        tab->scroll_line = tab->cursor_line;
    else if (tab->cursor_line >= tab->scroll_line + (size_t)ed->view_rows)
        tab->scroll_line = tab->cursor_line - (size_t)ed->view_rows + 1;

    /* horizontal */
    if (tab->cursor_col < tab->scroll_col)
        tab->scroll_col = tab->cursor_col;
    else if (tab->cursor_col >= tab->scroll_col + (size_t)ed->view_cols)
        tab->scroll_col = tab->cursor_col - (size_t)ed->view_cols + 1;
}

void editor_scroll_to(editor_t *ed, size_t line) {
    editor_tab_t *tab = editor_active(ed);
    if (!tab) return;
    tab->scroll_line = line;
    editor_highlight_visible(ed);
}
