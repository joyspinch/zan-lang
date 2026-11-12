#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../../src/common/zan_abi.h"

typedef struct {
    const char *name;
    const unsigned char *data;
    long long len;
} embed_entry;
extern void zan_embed_register(const embed_entry *, long long);
extern const char *zan_embed_read(const char *);
extern const unsigned char *zan_embed_bytes(const char *, int *);

static int valid(const char *text, uint32_t len) {
    uint64_t rc, word;
    if (!text || ((uintptr_t)text & 7)) return 0;
    memcpy(&rc, text - 16, 8);
    memcpy(&word, text - 8, 8);
    return rc == ZAN_STRING_SENTINEL_RC && word == ZAN_STR_HDR_WORD(len);
}

int main(void) {
    /* Source is deliberately not a Zan string, and binary has an interior NUL. */
    static const unsigned char raw[] = { 'A', 0, 0xff, 'Z' };
    static const unsigned char text[] = { 't', 'e', 'x', 't' };
    static const embed_entry entries[] = {
        { "binary", raw, sizeof(raw) }, { "text", text, sizeof(text) },
        { "empty", NULL, 0 }
    };
    if (!valid(zan_embed_read("missing"), 0)) return 1;
    zan_embed_register(entries, 3);
    const char *a = zan_embed_read("text");
    if (!valid(a, 4) || strcmp(a, "text")) return 2;
    for (int i = 0; i < 64; ++i)
        if (zan_embed_read("text") != a) return 3;
    const char *b = zan_embed_read("binary");
    if (!valid(b, 1) || memcmp(b, raw, sizeof(raw)) || b[4]) return 4;
    int len = -1;
    if (zan_embed_bytes("binary", &len) != raw || len != 4) return 5;
    if (!valid(zan_embed_read("empty"), 0) ||
        !valid(zan_embed_read("missing"), 0)) return 6;
    zan_embed_register(NULL, 0);
    if (strcmp(a, "text") || !valid(a, 4)) return 7;
    if (zan_embed_bytes("missing", &len) || len != 0) return 8;
    puts("PASS embed_string_abi");
    return 0;
}
