/* rpc.c -- Content-Length framed transport over stdio (see rpc.h). */
#include "rpc.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>

/* Read a single header line (up to and including the terminating \n) into
 * `buf`. Returns the length, or -1 on EOF. */
static int read_line(FILE *in, char *buf, int cap) {
    int len = 0;
    int c;
    while ((c = fgetc(in)) != EOF) {
        if (len < cap - 1) buf[len++] = (char)c;
        if (c == '\n') break;
    }
    buf[len] = '\0';
    if (c == EOF && len == 0) return -1;
    return len;
}

char *rpc_read_message(FILE *in) {
    char line[512];
    long content_length = -1;

    /* read headers until blank line */
    for (;;) {
        int n = read_line(in, line, (int)sizeof(line));
        if (n < 0) return NULL; /* EOF */

        /* strip trailing CR/LF */
        while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r'))
            line[--n] = '\0';

        if (n == 0) break; /* end of headers */

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

    char *body = (char *)malloc((size_t)content_length + 1);
    if (!body) return NULL;

    size_t got = fread(body, 1, (size_t)content_length, in);
    body[got] = '\0';
    if (got != (size_t)content_length) {
        /* short read (stream closed mid-message) */
        if (got == 0) { free(body); return NULL; }
    }
    return body;
}

void rpc_write_message(FILE *out, const char *payload) {
    size_t len = strlen(payload);
    fprintf(out, "Content-Length: %zu\r\n\r\n", len);
    fwrite(payload, 1, len, out);
    fflush(out);
}
