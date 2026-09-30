/* json_nul_test.c -- json_parse must reject \u0000. The escape decodes to a
 * raw NUL inside the value string, which the NUL-terminated string model
 * cannot carry: strlen/strcmp consumers silently truncate at it ("a\u0000b"
 * reads back "a") and a re-serialize drops the tail, so a round trip changes
 * bytes and masks peer desync. Other escaped control characters (U+0001..
 * U+001F) are representable and must keep parsing and round-tripping. */
#include "src/common/json.h"

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

static void test_nul_escape_rejected(void) {
    EXPECT(json_parse("\"a\\u0000b\"") == NULL,
           "string with \\u0000 should be rejected");
    EXPECT(json_parse("\"\\u0000\"") == NULL,
           "lone \\u0000 string should be rejected");
    EXPECT(json_parse("{\"a\\u0000b\":1}") == NULL,
           "object key with \\u0000 should be rejected");
    EXPECT(json_parse("{\"a\":\"x\\u0000y\"}") == NULL,
           "object value with \\u0000 should be rejected");
    EXPECT(json_parse("[\"\\u0000\"]") == NULL,
           "array element with \\u0000 should be rejected");
}

static void test_other_control_escapes_still_work(void) {
    json_value *v;
    char *s;

    /* U+0001 is representable; the serializer re-escapes it, so parse ->
     * serialize -> parse is byte-stable. */
    EXPECT((v = json_parse("\"a\\u0001\\u001fb\"")) != NULL,
           "escaped U+0001/U+001F rejected");
    if (v) {
        EXPECT((s = json_serialize(v)) != NULL, "serialize failed");
        if (s) {
            EXPECT(strcmp(s, "\"a\\u0001\\u001fb\"") == 0,
                   "control escapes did not round-trip");
            free(s);
        }
        json_free(v);
    }

    /* Ordinary escapes and non-ASCII BMP must be unaffected by the new
     * rejection. */
    EXPECT((v = json_parse("\"A\\u0041\\u4e2d\\ud83d\\ude00\"")) != NULL,
           "ascii/BMP/surrogate-pair string rejected");
    if (v) {
        EXPECT((s = json_serialize(v)) != NULL, "serialize failed");
        if (s) {
            /* Non-ASCII re-serializes as raw UTF-8 bytes (only < 0x20 is
             * re-escaped), so U+4E2D -> E4 B8 AD, U+1F600 -> F0 9F 98 80. */
            EXPECT(strcmp(s, "\"AA\344\270\255\360\237\230\200\"") == 0,
                   "ascii/BMP/surrogate-pair round-trip changed bytes");
            free(s);
        }
        json_free(v);
    }
}

int main(void) {
    test_nul_escape_rejected();
    test_other_control_escapes_still_work();
    if (failures) {
        fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    printf("ok\n");
    return 0;
}
