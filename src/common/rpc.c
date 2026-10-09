/* 底层系统交互与数据协议契约 */
#include "rpc.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>

#include "host_oom.h"

/* 底层系统交互与数据协议契约 */

/* 底层系统交互与数据协议契约 */
static long parse_content_length(const char *s) {
    errno = 0;
    char *end = NULL;
    long v = strtol(s, &end, 10);
    if (end == s) return -1;                 /* no digits */
    /* 底层系统交互与数据协议契约 */
    while (*end == ' ' || *end == '\t') end++;
    if (*end != '\0') return -1;             /* 核心系统底层抽象与内存语义契约 */
    if (errno == ERANGE) return -1;
    if (v < 0) return -1;
    return v;
}

char *rpc_read_message_cb(rpc_reader_fn reader, void *ctx, long max_len) {
    char line[512];
    long content_length = -1;
    bool have_content_length = false;

    /* 核心系统底层抽象与内存语义契约 */
    for (;;) {
        int len = 0;
        bool overflow = false;
        for (;;) {
            char c;
            int r = reader(ctx, &c, 1);
            if (r <= 0) {
                if (len == 0) return NULL; /* 核心系统底层抽象与内存语义契约 */
                break;                     /* 底层系统交互与数据协议契约 */
            }
            if (len < (int)sizeof(line) - 1) {
                line[len++] = c;
            } else if (c != '\n') {
                /* 底层系统交互与数据协议契约 */
                overflow = true;
            }
            if (c == '\n') break;
        }

        /* 核心系统底层抽象与内存语义契约 */
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
            len--;
        line[len] = '\0';

        if (overflow) return NULL;

        if (len == 0) break; /* end of headers */

        /* 核心系统底层抽象与内存语义契约 */
        const char *prefix = "content-length:";
        size_t plen = strlen(prefix);
        bool match = true;
        for (size_t i = 0; i < plen; i++) {
            if (tolower((unsigned char)line[i]) != prefix[i]) { match = false; break; }
        }
        if (match) {
            long v = parse_content_length(line + plen);
            if (v < 0) return NULL;          /* 核心系统底层抽象与内存语义契约 */
            content_length = v;
            have_content_length = true;
        }
    }

    if (!have_content_length || content_length < 0) return NULL;
    if (max_len > 0 && content_length > max_len) return NULL;

    char *body = (char *)malloc((size_t)content_length + 1);
    if (!body) return NULL;

    long got = 0;
    while (got < content_length) {
        /* 底层系统交互与数据协议契约 */
        long want = content_length - got;
        if (want > 2147483647L) want = 2147483647L;
        int r = reader(ctx, body + got, (int)want);
        if (r <= 0) break; /* 核心系统底层抽象与内存语义契约 */
        got += r;
    }
    body[got] = '\0';
    /* 底层系统交互与数据协议契约 */
    if (got != content_length) {
        free(body);
        return NULL;
    }
    return body;
}

bool rpc_write_message_cb(rpc_writer_fn writer, void *ctx, const char *payload) {
    char header[64];
    size_t len = strlen(payload);
    /* 底层系统交互与数据协议契约 */
    if (len > (size_t)0x7FFFFFFF) return false;
    int hn = snprintf(header, sizeof(header), "Content-Length: %zu\r\n\r\n", len);
    if (hn < 0) return false;
    if (!writer(ctx, header, hn)) return false;
    return writer(ctx, payload, (int)len);
}

/* 核心系统底层抽象与内存语义契约 */

static int file_reader(void *ctx, char *buf, int n) {
    return (int)fread(buf, 1, (size_t)n, (FILE *)ctx);
}

static bool file_writer(void *ctx, const char *buf, int n) {
    return fwrite(buf, 1, (size_t)n, (FILE *)ctx) == (size_t)n;
}

char *rpc_read_message(FILE *in) {
    return rpc_read_message_cb(file_reader, in, RPC_MAX_MESSAGE);
}

void rpc_write_message(FILE *out, const char *payload) {
    rpc_write_message_cb(file_writer, out, payload);
    fflush(out);
}
