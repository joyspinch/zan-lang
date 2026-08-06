/* rpc_framing_test.c -- reproduces A3: three Content-Length framing bugs in
 * rpc_read_message_cb.
 *
 *   A3.1 content-length parsed with bare strtol -- no errno / endptr / range
 *        check, so "Content-Length: abc" silently becomes 0 and
 *        "Content-Length: 9999999999" overflows long.
 *   A3.2 a partial body (got < content_length, got > 0) was accepted as a
 *        complete message because the guard was `got != cl && got == 0`.
 *   A3.3 rpc_read_message(FILE*) passed max_len = 0, disabling the size cap so
 *        a single header could force a multi-GB allocation.
 *
 * The test drives rpc_read_message_cb with an in-memory reader so the framing
 * logic can be probed without a real pipe. */
#include "src/common/rpc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

#define EXPECT(cond, ...) do {                                              \
    if (!(cond)) {                                                          \
        failures++;                                                         \
        fprintf(stderr, "FAIL: ");                                          \
        fprintf(stderr, __VA_ARGS__);                                       \
        fprintf(stderr, "\n");                                              \
    }                                                                       \
} while (0)

/* ---- in-memory reader: serves a fixed buffer one byte (or chunk) at a time ---- */
typedef struct {
    const char *data;
    size_t len;
    size_t pos;
} mem_reader;

static int mem_read_byte(void *ctx, char *buf, int n) {
    mem_reader *m = (mem_reader *)ctx;
    if (m->pos >= m->len) return 0; /* EOF */
    int avail = (int)(m->len - m->pos);
    if (n > avail) n = avail;
    memcpy(buf, m->data + m->pos, (size_t)n);
    m->pos += (size_t)n;
    return n;
}

static char *read_frame(const char *frame, long max_len) {
    mem_reader m = { frame, strlen(frame), 0 };
    return rpc_read_message_cb(mem_read_byte, &m, max_len);
}

/* ---- A3.1: malformed / out-of-range Content-Length ---- */
static void test_bad_content_length(void) {
    /* "abc" is not a number; original strtol gives 0, which then looks like a
     * zero-length body and returns a 1-byte "". Must be rejected. */
    char *r = read_frame("Content-Length: abc\r\n\r\n", 1024);
    EXPECT(r == NULL, "Content-Length: abc should be rejected");

    /* negative length */
    r = read_frame("Content-Length: -5\r\n\r\n", 1024);
    EXPECT(r == NULL, "negative Content-Length should be rejected");

    /* trailing junk after the digits */
    r = read_frame("Content-Length: 5x\r\n\r\n12345", 1024);
    EXPECT(r == NULL, "Content-Length with trailing junk should be rejected");

    /* empty value */
    r = read_frame("Content-Length:\r\n\r\n", 1024);
    EXPECT(r == NULL, "empty Content-Length should be rejected");
}

/* ---- A3.3: oversize Content-Length capped ---- */
static void test_oversize_capped(void) {
    /* A huge but parseable length must be rejected without allocating. With the
     * cap disabled (max_len=0) the original code would malloc(~10GB). */
    char *r = read_frame("Content-Length: 9999999999\r\n\r\n", 16 * 1024 * 1024);
    EXPECT(r == NULL, "oversize Content-Length should be rejected under the cap");
}

/* ---- A3.2: partial body rejected ---- */
static void test_partial_body_rejected(void) {
    /* Declare 100 bytes but supply only 50 then EOF. The original guard
     * (`got != cl && got == 0`) let this through as a "complete" 50-byte
     * message. Must return NULL. */
    const char *hdr = "Content-Length: 100\r\n\r\n";
    size_t hl = strlen(hdr);
    char *frame = (char *)malloc(hl + 50);
    memcpy(frame, hdr, hl);
    for (int i = 0; i < 50; i++) frame[hl + i] = 'x';
    mem_reader m = { frame, hl + 50, 0 };
    char *r = rpc_read_message_cb(mem_read_byte, &m, 1024 * 1024);
    EXPECT(r == NULL, "partial body (50 of 100) should be rejected, not accepted");
    free(r);
    free(frame);
}

/* ---- normal frame still works ---- */
static void test_normal_frame(void) {
    const char *body = "{\"method\":\"initialize\"}";
    char header[64];
    snprintf(header, sizeof(header), "Content-Length: %zu\r\n\r\n", strlen(body));
    char *frame = (char *)malloc(strlen(header) + strlen(body) + 1);
    sprintf(frame, "%s%s", header, body);
    char *r = read_frame(frame, 1024 * 1024);
    EXPECT(r != NULL, "normal frame should parse");
    if (r) {
        EXPECT(strcmp(r, body) == 0, "body mismatch: got '%s'", r);
        free(r);
    }
    free(frame);
}

/* ---- multiple headers, Content-Length not first ---- */
static void test_content_length_not_first(void) {
    const char *body = "12345";
    char frame[256];
    snprintf(frame, sizeof(frame),
             "Content-Type: application/json\r\nContent-Length: %zu\r\n\r\n%s",
             strlen(body), body);
    char *r = read_frame(frame, 1024 * 1024);
    EXPECT(r != NULL, "frame with Content-Length not first should parse");
    if (r) {
        EXPECT(strcmp(r, body) == 0, "body mismatch");
        free(r);
    }
}

/* ---- zero-length body is a valid (if unusual) message ---- */
static void test_zero_length_body(void) {
    char *r = read_frame("Content-Length: 0\r\n\r\n", 1024);
    EXPECT(r != NULL, "zero-length body should parse");
    if (r) {
        EXPECT(r[0] == '\0', "zero-length body should be empty string");
        free(r);
    }
}

/* ---- EOF before any header byte returns NULL (no spurious message) ---- */
static void test_immediate_eof(void) {
    mem_reader m = { "", 0, 0 };
    char *r = rpc_read_message_cb(mem_read_byte, &m, 1024);
    EXPECT(r == NULL, "immediate EOF should return NULL");
}

int main(void) {
    test_bad_content_length();
    test_oversize_capped();
    test_partial_body_rejected();
    test_normal_frame();
    test_content_length_not_first();
    test_zero_length_body();
    test_immediate_eof();
    if (failures) {
        fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    printf("ok\n");
    return 0;
}
