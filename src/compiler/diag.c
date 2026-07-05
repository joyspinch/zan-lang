/* diag.c -- Diagnostic reporting implementation. */

#include "diag.h"
#include "arena.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>

zan_diag_t *zan_diag_new(zan_arena_t *arena) {
    zan_diag_t *d = (zan_diag_t *)zan_arena_alloc(arena, sizeof(zan_diag_t));
    d->error_count = 0;
    d->warning_count = 0;
    d->max_errors = 100;
    d->file_names = NULL;
    d->file_sources = NULL;
    d->file_count = 0;
    d->capture = false;
    d->entries = NULL;
    d->entry_count = 0;
    d->entry_cap = 0;
    return d;
}

void zan_diag_add_file(zan_diag_t *diag, const char *name, const char *source) {
    /* simple dynamic array for file list */
    int idx = diag->file_count;
    diag->file_count++;

    const char **names = (const char **)realloc((void *)diag->file_names,
                                                 sizeof(char *) * (size_t)diag->file_count);
    const char **sources = (const char **)realloc((void *)diag->file_sources,
                                                   sizeof(char *) * (size_t)diag->file_count);
    names[idx] = name;
    sources[idx] = source;
    diag->file_names = names;
    diag->file_sources = sources;
}

/* find the line containing `offset` in `source` and return its start */
static const char *find_line_start(const char *source, uint32_t offset) {
    const char *p = source + offset;
    while (p > source && p[-1] != '\n') p--;
    return p;
}

static int find_line_len(const char *line_start) {
    const char *p = line_start;
    while (*p && *p != '\n' && *p != '\r') p++;
    return (int)(p - line_start);
}

void zan_diag_set_capture(zan_diag_t *diag, bool enabled) {
    diag->capture = enabled;
}

int zan_diag_entry_count(const zan_diag_t *diag) {
    return diag->entry_count;
}

const zan_diag_entry_t *zan_diag_entry_at(const zan_diag_t *diag, int index) {
    if (index < 0 || index >= diag->entry_count) return NULL;
    return &diag->entries[index];
}

void zan_diag_free_buffers(zan_diag_t *diag) {
    free(diag->entries);
    diag->entries = NULL;
    diag->entry_count = 0;
    diag->entry_cap = 0;
    free((void *)diag->file_names);
    free((void *)diag->file_sources);
    diag->file_names = NULL;
    diag->file_sources = NULL;
    diag->file_count = 0;
}

static void diag_capture_entry(zan_diag_t *diag, zan_diag_level_t level,
                               zan_loc_t loc, const char *fmt, va_list args) {
    if (diag->entry_count >= diag->entry_cap) {
        int new_cap = diag->entry_cap ? diag->entry_cap * 2 : 16;
        zan_diag_entry_t *grown = (zan_diag_entry_t *)realloc(
            diag->entries, sizeof(zan_diag_entry_t) * (size_t)new_cap);
        if (!grown) return;
        diag->entries = grown;
        diag->entry_cap = new_cap;
    }
    zan_diag_entry_t *e = &diag->entries[diag->entry_count++];
    e->level = level;
    e->loc = loc;
    vsnprintf(e->message, sizeof(e->message), fmt, args);
}

void zan_diag_emit(zan_diag_t *diag, zan_diag_level_t level, zan_loc_t loc,
                   const char *fmt, ...) {
    if (level == DIAG_ERROR) {
        diag->error_count++;
        if (diag->error_count > diag->max_errors) return;
    } else if (level == DIAG_WARNING) {
        diag->warning_count++;
    }

    /* structured capture path: store and skip stderr rendering */
    if (diag->capture) {
        va_list args;
        va_start(args, fmt);
        diag_capture_entry(diag, level, loc, fmt, args);
        va_end(args);
        return;
    }

    const char *level_str = "note";
    const char *color = "\033[36m"; /* cyan */
    if (level == DIAG_ERROR) {
        level_str = "error";
        color = "\033[31m"; /* red */
    } else if (level == DIAG_WARNING) {
        level_str = "warning";
        color = "\033[33m"; /* yellow */
    }

    const char *file_name = "<unknown>";
    if (loc.file_id < (uint32_t)diag->file_count && diag->file_names) {
        file_name = diag->file_names[loc.file_id];
    }

    /* header: file:line:col: level: message */
    fprintf(stderr, "%s:%u:%u: %s%s\033[0m: ", file_name, loc.line, loc.col,
            color, level_str);

    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fprintf(stderr, "\n");

    /* show source line if available */
    if (loc.file_id < (uint32_t)diag->file_count && diag->file_sources) {
        const char *source = diag->file_sources[loc.file_id];
        if (source && loc.offset < strlen(source)) {
            const char *line_start = find_line_start(source, loc.offset);
            int line_len = find_line_len(line_start);
            fprintf(stderr, " %4u | %.*s\n", loc.line, line_len, line_start);
            fprintf(stderr, "      | ");
            for (uint32_t i = 1; i < loc.col; i++) fprintf(stderr, " ");
            fprintf(stderr, "%s^\033[0m\n", color);
        }
    }
}

bool zan_diag_has_errors(zan_diag_t *diag) {
    return diag->error_count > 0;
}
