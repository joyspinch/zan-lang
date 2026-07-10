/* zanfmt.c -- Zan Code Formatter.
 *
 * Formats .zan source files with consistent indentation, spacing, and style.
 *
 * Usage:
 *   zanfmt <file.zan>              Format file (in-place)
 *   zanfmt --check <file.zan>      Check if file needs formatting
 *   zanfmt --stdout <file.zan>     Print formatted output to stdout
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>

#define MAX_LINE 4096
#define INDENT_SIZE 4

#include "../common/host_oom.h"
typedef struct {
    char *buf;
    int len;
    int cap;
} strbuf_t;

static void sb_init(strbuf_t *sb) {
    sb->cap = 4096;
    sb->buf = (char *)malloc((size_t)sb->cap);
    sb->len = 0;
    sb->buf[0] = 0;
}

static void sb_append(strbuf_t *sb, const char *s, int n) {
    if (n < 0) n = (int)strlen(s);
    while (sb->len + n + 1 > sb->cap) {
        sb->cap *= 2;
        sb->buf = (char *)realloc(sb->buf, (size_t)sb->cap);
    }
    memcpy(sb->buf + sb->len, s, (size_t)n);
    sb->len += n;
    sb->buf[sb->len] = 0;
}

static void sb_char(strbuf_t *sb, char c) {
    sb_append(sb, &c, 1);
}

static void sb_indent(strbuf_t *sb, int level) {
    for (int i = 0; i < level * INDENT_SIZE; i++) {
        sb_char(sb, ' ');
    }
}

static void sb_free(strbuf_t *sb) {
    free(sb->buf);
    sb->buf = NULL;
    sb->len = sb->cap = 0;
}

static char *read_file(const char *path, long *out_size) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = (char *)malloc((size_t)sz + 1);
    fread(buf, 1, (size_t)sz, f);
    buf[sz] = 0;
    fclose(f);
    if (out_size) *out_size = sz;
    return buf;
}

/* Trim trailing whitespace from a line */
static int trim_trailing(const char *line, int len) {
    while (len > 0 && (line[len - 1] == ' ' || line[len - 1] == '\t' || line[len - 1] == '\r')) {
        len--;
    }
    return len;
}

/* Count leading whitespace */
static int leading_ws(const char *line) {
    int n = 0;
    while (line[n] == ' ' || line[n] == '\t') n++;
    return n;
}

/* Check if a line is blank */
static bool is_blank(const char *line, int len) {
    for (int i = 0; i < len; i++) {
        if (line[i] != ' ' && line[i] != '\t' && line[i] != '\r') return false;
    }
    return true;
}

/* Check if trimmed line starts with a specific word */
static bool starts_with_word(const char *p, const char *word) {
    int wlen = (int)strlen(word);
    if (strncmp(p, word, (size_t)wlen) != 0) return false;
    return !isalnum((unsigned char)p[wlen]) && p[wlen] != '_';
}

/* Format a Zan source file */
static char *format_zan(const char *src, long src_len) {
    strbuf_t out;
    sb_init(&out);

    int indent = 0;
    bool in_string = false;
    bool in_line_comment = false;
    bool in_block_comment = false;
    int consecutive_blanks = 0;

    const char *p = src;
    const char *end = src + src_len;

    while (p < end) {
        /* find end of current line */
        const char *line_start = p;
        while (p < end && *p != '\n') p++;
        int line_len = (int)(p - line_start);
        if (p < end) p++; /* skip \n */

        /* trim trailing whitespace */
        int trimmed = trim_trailing(line_start, line_len);

        /* handle blank lines: max 1 consecutive blank line */
        if (is_blank(line_start, trimmed)) {
            consecutive_blanks++;
            if (consecutive_blanks <= 1) {
                sb_char(&out, '\n');
            }
            continue;
        }
        consecutive_blanks = 0;

        /* get the content part (skip existing whitespace) */
        int ws = leading_ws(line_start);
        const char *content = line_start + ws;
        int content_len = trimmed - ws;

        /* check if line starts with closing brace — dedent before writing */
        if (content[0] == '}') {
            indent--;
            if (indent < 0) indent = 0;
        }

        /* write indented line */
        sb_indent(&out, indent);
        sb_append(&out, content, content_len);
        sb_char(&out, '\n');

        /* check if line ends with opening brace — indent next line */
        if (content_len > 0 && content[content_len - 1] == '{') {
            indent++;
        }

        /* handle } else { on same line: don't double-indent */
        if (content[0] == '}' && content_len > 1) {
            /* check for "} else {" or "} catch {" etc. */
            const char *after_brace = content + 1;
            while (*after_brace == ' ') after_brace++;
            if (starts_with_word(after_brace, "else") ||
                starts_with_word(after_brace, "catch") ||
                starts_with_word(after_brace, "finally")) {
                /* the closing } already dedented, the trailing { re-indents */
                /* so net effect is neutral — already handled above */
            }
        }
    }

    /* ensure file ends with newline */
    if (out.len > 0 && out.buf[out.len - 1] != '\n') {
        sb_char(&out, '\n');
    }

    return out.buf;
}

static void print_usage(void) {
    printf("Zan Code Formatter (zanfmt)\n\n");
    printf("Usage:\n");
    printf("  zanfmt <file.zan>              Format file in-place\n");
    printf("  zanfmt --check <file.zan>      Check formatting (exit 1 if changes needed)\n");
    printf("  zanfmt --stdout <file.zan>     Print formatted to stdout\n");
    printf("  zanfmt --help                  Show this help\n");
}

int main(int argc, char **argv) {
    if (argc < 2) { print_usage(); return 0; }

    bool check_mode = false;
    bool stdout_mode = false;
    const char *file_path = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--check") == 0) check_mode = true;
        else if (strcmp(argv[i], "--stdout") == 0) stdout_mode = true;
        else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_usage(); return 0;
        }
        else file_path = argv[i];
    }

    if (!file_path) { fprintf(stderr, "error: no file specified\n"); return 1; }

    long src_len = 0;
    char *src = read_file(file_path, &src_len);
    if (!src) { fprintf(stderr, "error: cannot read '%s'\n", file_path); return 1; }

    char *formatted = format_zan(src, src_len);

    if (check_mode) {
        bool same = (strlen(formatted) == (size_t)src_len && memcmp(formatted, src, (size_t)src_len) == 0);
        free(src);
        free(formatted);
        if (same) {
            printf("%s: ok\n", file_path);
            return 0;
        } else {
            printf("%s: needs formatting\n", file_path);
            return 1;
        }
    }

    if (stdout_mode) {
        printf("%s", formatted);
    } else {
        /* write in-place */
        FILE *f = fopen(file_path, "wb");
        if (!f) { fprintf(stderr, "error: cannot write '%s'\n", file_path); return 1; }
        fwrite(formatted, 1, strlen(formatted), f);
        fclose(f);
        printf("Formatted: %s\n", file_path);
    }

    free(src);
    free(formatted);
    return 0;
}
