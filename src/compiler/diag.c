
#include "diag.h"
#include "arena.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>

#include "../common/host_oom.h"
zan_diag_t *zan_diag_new(zan_arena_t *arena) {
    zan_diag_t *d = (zan_diag_t *)zan_arena_alloc(arena, sizeof(zan_diag_t));
    d->error_count = 0;
    d->warning_count = 0;
    d->max_errors = 100;
    d->suppressed_errors = 0;
    d->limit_notice_shown = false;
    d->file_names = NULL;
    d->file_sources = NULL;
    d->file_count = 0;
    d->capture = false;
    d->treat_warnings_as_errors = false;
    d->warn_level = 1; /* default：告警照常打印（A382②） */
    d->suppress_codes = NULL;
    d->suppress_count = 0;
    d->suppress_cap = 0;
    d->entries = NULL;
    d->entry_count = 0;
    d->entry_cap = 0;
    d->dup_file_id = 0;
    d->dup_line = 0;
    d->dup_line_errors = 0;
    d->dup_line_suppressed = 0;
    d->dup_line_notice_shown = false;
    return d;
}

void zan_diag_set_max_errors(zan_diag_t *diag, int max_errors) {
    if (!diag) return;
    diag->max_errors = max_errors;
}

int zan_diag_suppressed_errors(const zan_diag_t *diag) {
    return diag ? diag->suppressed_errors : 0;
}

void zan_diag_add_file(zan_diag_t *diag, const char *name, const char *source) {
    int idx = diag->file_count;
    int new_count = idx + 1;

    const char **names = (const char **)realloc((void *)diag->file_names,
                                                 sizeof(char *) * (size_t)new_count);
    if (!names) return;
    diag->file_names = names;

    const char **sources = (const char **)realloc((void *)diag->file_sources,
                                                   sizeof(char *) * (size_t)new_count);
    if (!sources) return;
    diag->file_sources = sources;

    names[idx] = name;
    sources[idx] = source;
    diag->file_count = new_count;
}

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

void zan_diag_set_deny_warnings(zan_diag_t *diag, bool enabled) {
    if (diag) diag->treat_warnings_as_errors = enabled;
}

void zan_diag_set_warn_level(zan_diag_t *diag, int level) {
    if (diag) diag->warn_level = level;
}

void zan_diag_suppress_code(zan_diag_t *diag, int code) {
    if (!diag || code <= 0) return;
    if (diag->suppress_count == diag->suppress_cap) {
        int ncap = diag->suppress_cap ? diag->suppress_cap * 2 : 8;
        int *grown = (int *)realloc(diag->suppress_codes,
                                    sizeof(int) * (size_t)ncap);
        if (!grown) return;
        diag->suppress_codes = grown;
        diag->suppress_cap = ncap;
    }
    diag->suppress_codes[diag->suppress_count++] = code;
}

static int zan_diag_code_suppressed(const zan_diag_t *diag, int code) {
    for (int i = 0; i < diag->suppress_count; i++) {
        if (diag->suppress_codes[i] == code) return 1;
    }
    return 0;
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
    free(diag->suppress_codes);
    diag->suppress_codes = NULL;
    diag->suppress_count = 0;
    diag->suppress_cap = 0;
    free((void *)diag->file_names);
    free((void *)diag->file_sources);
    diag->file_names = NULL;
    diag->file_sources = NULL;
    diag->file_count = 0;
}

static zan_diag_entry_t *diag_capture_entry(zan_diag_t *diag, zan_diag_level_t level,
                                            zan_loc_t loc) {
    if (diag->entry_count >= diag->entry_cap) {
        int new_cap = diag->entry_cap ? diag->entry_cap * 2 : 16;
        zan_diag_entry_t *grown = (zan_diag_entry_t *)realloc(
            diag->entries, sizeof(zan_diag_entry_t) * (size_t)new_cap);
        if (!grown) return NULL;
        diag->entries = grown;
        diag->entry_cap = new_cap;
    }
    zan_diag_entry_t *e = &diag->entries[diag->entry_count++];
    e->level = level;
    e->loc = loc;
    e->code = 0;
    e->message[0] = '\0';
    return e;
}

static void zan_diag_vemit(zan_diag_t *diag, zan_diag_level_t level,
                           zan_loc_t loc, int code, const char *fmt,
                           va_list args);

void zan_diag_emit(zan_diag_t *diag, zan_diag_level_t level, zan_loc_t loc,
                   const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    zan_diag_vemit(diag, level, loc, 0, fmt, args);
    va_end(args);
}

static void zan_diag_vemit(zan_diag_t *diag, zan_diag_level_t level,
                           zan_loc_t loc, int code, const char *fmt,
                           va_list args) {
    char msgbuf[512];
    char prefix[16];
    if (code > 0) {
        snprintf(prefix, sizeof(prefix), "[%c%04d] ",
                 level == DIAG_WARNING ? 'W' : 'E', code);
    } else {
        prefix[0] = '\0';
    }
    int plen = (int)strlen(prefix);
    memcpy(msgbuf, prefix, (size_t)plen);
    vsnprintf(msgbuf + plen, sizeof(msgbuf) - (size_t)plen, fmt, args);

    if (level == DIAG_WARNING && diag->treat_warnings_as_errors) {
        level = DIAG_ERROR;
    }

    /* A382②：--warn-level=none 与 --suppress 的告警过滤，在计数前丢弃。
     * capture 模式（LSP）不受过滤，照常入表——过滤是 zanc 的打印/计数策略。 */
    if (level == DIAG_WARNING && !diag->capture) {
        if (diag->warn_level == 0) return;
        if (code > 0 && zan_diag_code_suppressed(diag, code)) return;
    }

    if (level == DIAG_ERROR) {
        diag->error_count++;
        if (diag->max_errors > 0 && diag->error_count > diag->max_errors) {
            /* 限制诊断信息条数上限，避免语法崩溃时输出大量级联错误 */
            diag->suppressed_errors++;
            if (!diag->limit_notice_shown && !diag->capture) {
                diag->limit_notice_shown = true;
                fprintf(stderr,
                        "\033[33mnote\033[0m: too many errors, stopping after %d"
                        " (further errors are suppressed; use"
                        " -ferror-limit=0 for all)\n",
                        diag->max_errors);
            }
            return;
        }
    } else if (level == DIAG_WARNING) {
        diag->warning_count++;
    }

    /* 折叠单行级联报错噪音，限制单行最大打印错误数 */
    if (level == DIAG_ERROR) {
        if (diag->dup_line != loc.line || diag->dup_file_id != loc.file_id) {
            diag->dup_file_id = loc.file_id;
            diag->dup_line = loc.line;
            diag->dup_line_errors = 0;
            diag->dup_line_suppressed = 0;
            diag->dup_line_notice_shown = false;
        }
        if (++diag->dup_line_errors > ZAN_DIAG_MAX_ERRORS_PER_LINE) {
            diag->dup_line_suppressed++;
            if (!diag->dup_line_notice_shown && !diag->capture) {
                diag->dup_line_notice_shown = true;
                fprintf(stderr,
                        "\033[33mnote\033[0m: line %u: more than %d errors,"
                        " rest of the line suppressed\n",
                        loc.line, ZAN_DIAG_MAX_ERRORS_PER_LINE);
            }
            return;
        }
    }

    if (diag->capture) {
        zan_diag_entry_t *e = diag_capture_entry(diag, level, loc);
        if (e) {
            e->code = code;
            snprintf(e->message, sizeof(e->message), "%s", msgbuf);
        }
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

    fprintf(stderr, "%s:%u:%u: %s%s\033[0m: %s\n", file_name, loc.line, loc.col,
            color, level_str, msgbuf);

    if (loc.file_id < (uint32_t)diag->file_count && diag->file_sources) {
        const char *source = diag->file_sources[loc.file_id];
        if (source && loc.offset < strlen(source)) {
            const char *line_start = find_line_start(source, loc.offset);
            int line_len = find_line_len(line_start);
            int col0 = loc.col > 0 ? (int)loc.col - 1 : 0;
            int vis_start = 0;
            if (line_len > ZAN_DIAG_MAX_SOURCE_ECHO) {
                vis_start = col0 - ZAN_DIAG_MAX_SOURCE_ECHO / 2;
                if (vis_start + ZAN_DIAG_MAX_SOURCE_ECHO > line_len)
                    vis_start = line_len - ZAN_DIAG_MAX_SOURCE_ECHO;
                if (vis_start < 0) vis_start = 0;
            }
            int vis_len = line_len - vis_start;
            if (vis_len > ZAN_DIAG_MAX_SOURCE_ECHO) vis_len = ZAN_DIAG_MAX_SOURCE_ECHO;
            fprintf(stderr, " %4u | %s%.*s%s\n", loc.line,
                    vis_start > 0 ? "..." : "", vis_len, line_start + vis_start,
                    vis_start + vis_len < line_len ? "..." : "");
            fprintf(stderr, "      | ");
            int indent = col0 - vis_start + (vis_start > 0 ? 3 : 0);
            if (indent < 0) indent = 0;
            for (int i = 0; i < indent; i++) fprintf(stderr, " ");
            fprintf(stderr, "%s^\033[0m\n", color);
        }
    }
}

bool zan_diag_has_errors(zan_diag_t *diag) {
    return diag->error_count > 0;
}

void zan_diag_emit_code(zan_diag_t *diag, zan_diag_level_t level, zan_loc_t loc,
                        int code, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    zan_diag_vemit(diag, level, loc, code, fmt, args);
    va_end(args);
}

void zan_compile_trace(const char *fmt, ...) {
    static int on = -1;
    if (on < 0) on = getenv("ZANC_TRACE") ? 1 : 0;
    if (!on) return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    fflush(stderr);
}
