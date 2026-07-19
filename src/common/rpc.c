/* rpc.c -- Content-Length framed transport over stdio (see rpc.h). */
#include "rpc.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>

#include "host_oom.h"

char *rpc_read_message_cb(rpc_reader_fn reader, void *ctx, long max_len) {
    char line[512];
    long content_length = -1;

    /* read headers until a blank line */
    for (;;) {
        int len = 0;
        for (;;) {
            char c;
            int r = reader(ctx, &c, 1);
            if (r <= 0) {
                if (len == 0) return NULL; /* EOF before any header byte */
                break;                     /* EOF mid-line: process what we have */
            }
            if (len < (int)sizeof(line) - 1) line[len++] = c;
            if (c == '\n') break;
        }

        /* strip trailing CR/LF */
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
            len--;
        line[len] = '\0';

        if (len == 0) break; /* end of headers */

        /* case-insensitive match of "Content-Length:" */
        const char *prefix = "content-length:";
        size_t plen = strlen(prefix);
        bool match = true;
        for (size_t i = 0; i < plen; i++) {
            if (tolower((unsigned char)line[i]) != prefix[i]) { match = false; break; }
        }
        if (match) {
            content_length = strtol(line + plen, NULL, 10);
        }
    }

    if (content_length < 0) return NULL;
    if (max_len > 0 && content_length > max_len) return NULL;

    char *body = (char *)malloc((size_t)content_length + 1);
    if (!body) return NULL;

    long got = 0;
    while (got < content_length) {
        int r = reader(ctx, body + got, (int)(content_length - got));
        if (r <= 0) break; /* stream closed mid-message */
        got += r;
    }
    body[got] = '\0';
    if (got != content_length && got == 0) {
        free(body);
        return NULL;
    }
    return body;
}

bool rpc_write_message_cb(rpc_writer_fn writer, void *ctx, const char *payload) {
    char header[64];
    size_t len = strlen(payload);
    int hn = snprintf(header, sizeof(header), "Content-Length: %zu\r\n\r\n", len);
    if (hn < 0) return false;
    if (!writer(ctx, header, hn)) return false;
    return writer(ctx, payload, (int)len);
}

/* ---- FILE-stream wrappers ---- */

static int file_reader(void *ctx, char *buf, int n) {
    return (int)fread(buf, 1, (size_t)n, (FILE *)ctx);
}

static bool file_writer(void *ctx, const char *buf, int n) {
    return fwrite(buf, 1, (size_t)n, (FILE *)ctx) == (size_t)n;
}

char *rpc_read_message(FILE *in) {
    return rpc_read_message_cb(file_reader, in, 0);
}

void rpc_write_message(FILE *out, const char *payload) {
    rpc_write_message_cb(file_writer, out, payload);
    fflush(out);
}
