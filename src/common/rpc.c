/* rpc.c -- Content-Length framed transport over stdio (see rpc.h). */
#include "rpc.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>

#include "host_oom.h"

/* The message cap lives in rpc.h (RPC_MAX_MESSAGE) so callers see the same
 * number this file's default reader enforces -- keep it that way. */

/* Parse the value of a "Content-Length:" header line (everything after the
 * colon). Returns the length on success, or -1 on any malformation: no digits,
 * trailing junk, out of long range, or negative. */
static long parse_content_length(const char *s) {
    errno = 0;
    char *end = NULL;
    long v = strtol(s, &end, 10);
    if (end == s) return -1;                 /* no digits */
    /* allow trailing spaces/tabs but nothing else */
    while (*end == ' ' || *end == '\t') end++;
    if (*end != '\0') return -1;             /* trailing junk */
    if (errno == ERANGE) return -1;
    if (v < 0) return -1;
    return v;
}

char *rpc_read_message_cb(rpc_reader_fn reader, void *ctx, long max_len) {
    char line[512];
    long content_length = -1;
    bool have_content_length = false;

    /* read headers until a blank line */
    for (;;) {
        int len = 0;
        bool overflow = false;
        for (;;) {
            char c;
            int r = reader(ctx, &c, 1);
            if (r <= 0) {
                if (len == 0) return NULL; /* EOF before any header byte */
                break;                     /* EOF mid-line: process what we have */
            }
            if (len < (int)sizeof(line) - 1) {
                line[len++] = c;
            } else if (c != '\n') {
                /* header line longer than the buffer: a legitimate LSP/DAP
                 * header is never this long; treat as protocol error rather
                 * than silently truncating (which could mis-parse the length). */
                overflow = true;
            }
            if (c == '\n') break;
        }

        /* strip trailing CR/LF */
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
            len--;
        line[len] = '\0';

        if (overflow) return NULL;

        if (len == 0) break; /* end of headers */

        /* case-insensitive match of "Content-Length:" */
        const char *prefix = "content-length:";
        size_t plen = strlen(prefix);
        bool match = true;
        for (size_t i = 0; i < plen; i++) {
            if (tolower((unsigned char)line[i]) != prefix[i]) { match = false; break; }
        }
        if (match) {
            long v = parse_content_length(line + plen);
            if (v < 0) return NULL;          /* malformed Content-Length */
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
        int r = reader(ctx, body + got, (int)(content_length - got));
        if (r <= 0) break; /* stream closed mid-message */
        got += r;
    }
    body[got] = '\0';
    /* A short read is a truncated message, not a complete one: a peer that
     * sent fewer bytes than it declared (or hit EOF mid-body) must not be
     * handed upstream as if the full body arrived. */
    if (got != content_length) {
        free(body);
        return NULL;
    }
    return body;
}

bool rpc_write_message_cb(rpc_writer_fn writer, void *ctx, const char *payload) {
    char header[64];
    size_t len = strlen(payload);
    /* The writer ABI takes an int length; a >INT_MAX payload would truncate
     * to a negative count and become a huge fwrite. Fail instead of
     * corrupting the stream (the read side caps at RPC_MAX_MESSAGE anyway). */
    if (len > (size_t)0x7FFFFFFF) return false;
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
    return rpc_read_message_cb(file_reader, in, RPC_MAX_MESSAGE);
}

void rpc_write_message(FILE *out, const char *payload) {
    rpc_write_message_cb(file_writer, out, payload);
    fflush(out);
}
