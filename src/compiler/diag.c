
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
    d->pragma_ranges = NULL;
    d->pragma_count = 0;
    d->pragma_cap = 0;
    d->entries = NULL;
    d->entry_count = 0;
    d->entry_cap = 0;
    d->dup_file_id = 0;
    d->dup_line = 0;
    d->dup_line_errors = 0;
    d->dup_line_suppressed = 0;
    d->dup_line_notice_shown = false;
    d->dedup_sigs = NULL;
    d->dedup_count = 0;
    d->dedup_cap = 0;
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

void zan_diag_pragma_warning(zan_diag_t *diag, zan_loc_t loc, int disable,
                             const int *codes, int count) {
    if (!diag) return;
    if (disable) {
        int n = count > 0 ? count : 1;   /* 无编号 = 通配一条 code=0 区间 */
        for (int k = 0; k < n; k++) {
            if (diag->pragma_count == diag->pragma_cap) {
                int ncap = diag->pragma_cap ? diag->pragma_cap * 2 : 8;
                zan_diag_pragma_range_t *grown =
                    (zan_diag_pragma_range_t *)realloc(
                        diag->pragma_ranges,
                        sizeof(zan_diag_pragma_range_t) * (size_t)ncap);
                if (!grown) return;
                diag->pragma_ranges = grown;
                diag->pragma_cap = ncap;
            }
            zan_diag_pragma_range_t *r =
                &diag->pragma_ranges[diag->pragma_count++];
            r->code = count > 0 ? codes[k] : 0;
            r->file_id = loc.file_id;
            r->from_line = loc.line;
            r->to_line = UINT32_MAX;
        }
        return;
    }
    /* restore：关闭本文件当前行前所有开放区间；无编号 = 全部，有编号 =
     * 同码或通配区间都关（通配区间覆盖面大于 restore 点名的编号） */
    for (int i = 0; i < diag->pragma_count; i++) {
        zan_diag_pragma_range_t *r = &diag->pragma_ranges[i];
        if (r->to_line != UINT32_MAX) continue;
        if (r->file_id != loc.file_id) continue;
        if (count > 0 && r->code != 0) {
            int hit = 0;
            for (int k = 0; k < count; k++) {
                if (codes[k] == r->code) { hit = 1; break; }
            }
            if (!hit) continue;
        }
        r->to_line = loc.line;
    }
}

/* A382③：loc 落在任一覆盖该编号的 enable 区间内（半开 [from, to)） */
static int zan_diag_pragma_suppressed(const zan_diag_t *diag, int code,
                                      zan_loc_t loc) {
    for (int i = 0; i < diag->pragma_count; i++) {
        const zan_diag_pragma_range_t *r = &diag->pragma_ranges[i];
        if (r->file_id != loc.file_id) continue;
        if (loc.line < r->from_line || loc.line >= r->to_line) continue;
        if (r->code == 0 || r->code == code) return 1;
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
    if (!diag) return;
    free(diag->entries);
    diag->entries = NULL;
    diag->entry_count = 0;
    diag->entry_cap = 0;
    free(diag->suppress_codes);
    diag->suppress_codes = NULL;
    diag->suppress_count = 0;
    diag->suppress_cap = 0;
    free(diag->pragma_ranges);
    diag->pragma_ranges = NULL;
    diag->pragma_count = 0;
    diag->pragma_cap = 0;
    if (diag->dedup_sigs) {
        for (int i = 0; i < diag->dedup_count; i++) {
            free(diag->dedup_sigs[i].msg);
        }
        free(diag->dedup_sigs);
        diag->dedup_sigs = NULL;
    }
    diag->dedup_count = 0;
    diag->dedup_cap = 0;
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
    char msgbuf[1024];
    char prefix[16];
    if (code > 0) {
        snprintf(prefix, sizeof(prefix), "[%c%04d] ",
                 level == DIAG_WARNING ? 'W' : 'E', code);
    } else {
        prefix[0] = '\0';
    }
    int plen = (int)strlen(prefix);
    memcpy(msgbuf, prefix, (size_t)plen);
    /* msgbuf 从 args 的副本格式化：调用方 va_list 保持未消费，
     * dedup 块还要用它做一次完整串格式化（B-ID206③）。 */
    va_list msg_args;
    va_copy(msg_args, args);
    /* 消息缓冲固定 1024：超长签名/标识符在此安全截断。vsnprintf 返回全长度，
     * 达到容量即发生截断——从尾部回退到 UTF-8 码点边界再补 "..."。截断点落
     * 在多字节序列中间会产生非法 UTF-8，LSP 把 message 原样透传 JSON 时
     * 直接断流（lsp_main.c）。 */
    int produced = vsnprintf(msgbuf + plen, sizeof(msgbuf) - (size_t)plen,
                             fmt, msg_args);
    va_end(msg_args);
    if (produced < 0) {
        msgbuf[plen] = '\0'; /* 编码失败：退化为前缀 */
    } else if (produced >= (int)(sizeof(msgbuf) - (size_t)plen)) {
        /* vsnprintf 实写内容止于 sizeof-2（NUL 在 sizeof-1）。先给 "..."
         * 预留 3 字节再回退码点边界——顺序不能反：回退后再 clamp 会把
         * 已合法的尾字节吞掉给省略号腾位。 */
        size_t end = sizeof(msgbuf) - 4;
        while (end > (size_t)plen &&
               ((unsigned char)msgbuf[end - 1] & 0xC0) == 0x80)
            end--; /* 回跳 continuation bytes（0x80-0xBF） */
        if (end > (size_t)plen && ((unsigned char)msgbuf[end - 1] & 0x80) != 0)
            end--; /* 剩余首字节是多字节序列的 lead（续字节已被上一步清空）：
                    * 序列必然残缺，丢弃整个码点 */
        memcpy(msgbuf + end, "...", 4);
    }

    if (level == DIAG_WARNING && diag->treat_warnings_as_errors) {
        level = DIAG_ERROR;
    }

    /* A382②：--warn-level=none 与 --suppress 的告警过滤，在计数前丢弃。
     * capture 模式（LSP）不受过滤，照常入表——过滤是 zanc 的打印/计数策略。 */
    if (level == DIAG_WARNING && !diag->capture) {
        if (diag->warn_level == 0) return;
        if (code > 0 && zan_diag_code_suppressed(diag, code)) return;
        if (zan_diag_pragma_suppressed(diag, code, loc)) return;
    }

    /* B-ID106：精确重复诊断（同 file/line/col/level/消息）去重——回溯重扫
     * 曾同错重报。在计数前去重，重复不消耗 -ferror-limit 预算、不触发
     * 单行级联帽；capture 模式（LSP）同口径，结构化表也不再收重复项。
     * 签名线性表 4096 条封顶（内存有界），超顶不再登记、退化为不去重。
     * B-ID206③：签名比完整格式化串，不是截断后的 msgbuf——两个超长诊断
     * 截到同一 1024 字节前缀但尾部不同时，截断串比对会把两条不同的错误
     * 误判成同一条（单元钉 unit_diag_dedup::test_diag_dedup_full_string）。
     * 量长+落盘各一次 va_copy；失败退回截断串，宁重报不误吞。 */
    if (level == DIAG_ERROR || level == DIAG_WARNING) {
        char *full_msg = NULL;
        va_list len_args;
        va_copy(len_args, args);
        int full_len = vsnprintf(NULL, 0, fmt, len_args);
        va_end(len_args);
        if (full_len > 0) {
            full_msg = (char *)malloc((size_t)full_len + 1);
            if (full_msg) {
                va_list put_args;
                va_copy(put_args, args);
                if (vsnprintf(full_msg, (size_t)full_len + 1, fmt, put_args)
                        < 0) {
                    free(full_msg);
                    full_msg = NULL;
                }
                va_end(put_args);
            }
        }
        const char *sig = full_msg ? full_msg : msgbuf;
        bool dup = false;
        for (int i = 0; i < diag->dedup_count; i++) {
            const zan_diag_dedup_sig_t *s = &diag->dedup_sigs[i];
            if (s->file_id == loc.file_id && s->line == loc.line &&
                s->col == loc.col && s->level == (int)level &&
                strcmp(s->msg, sig) == 0) { dup = true; break; }
        }
        if (dup) {
            free(full_msg);
            return;
        }
        if (diag->dedup_count < 4096) {
            if (diag->dedup_count == diag->dedup_cap) {
                int ncap = diag->dedup_cap ? diag->dedup_cap * 2 : 64;
                zan_diag_dedup_sig_t *grown = (zan_diag_dedup_sig_t *)realloc(
                    diag->dedup_sigs,
                    sizeof(zan_diag_dedup_sig_t) * (size_t)ncap);
                if (!grown) {
                    free(full_msg);
                    return; /* OOM 保守退化：宁重报不误吞 */
                }
                diag->dedup_sigs = grown;
                diag->dedup_cap = ncap;
            }
            zan_diag_dedup_sig_t *s = &diag->dedup_sigs[diag->dedup_count++];
            s->file_id = loc.file_id;
            s->line = loc.line;
            s->col = loc.col;
            s->level = (int)level;
            if (full_msg) {
                s->msg = full_msg; /* 所有权移交，zan_diag_free_buffers 释放 */
            } else {
                s->msg = (char *)malloc(strlen(msgbuf) + 1);
                if (!s->msg) zan_host_oom();
                strcpy(s->msg, msgbuf);
            }
        } else {
            /* 4096 封顶：不再登记，完整串即弃 */
            free(full_msg);
        }
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
