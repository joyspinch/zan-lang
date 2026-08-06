/* json_trailing_test.c -- reproduces A2: json_parse must reject trailing data
 * after the first complete value. The original parser returned as soon as it
 * had one value, so `{"a":1} garbage` was silently accepted (and the garbage
 * ignored), masking malformed input from LSP/DAP peers. */
#include "src/common/json.h"

#include <stdio.h>
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

static void test_trailing_garbage_rejected(void) {
    EXPECT(json_parse("{\"a\":1} garbage") == NULL,
           "object followed by garbage should be rejected");
    EXPECT(json_parse("{\"a\":1}x") == NULL,
           "object followed by 'x' should be rejected");
    EXPECT(json_parse("1 2") == NULL,
           "two numbers should be rejected");
    EXPECT(json_parse("[] []") == NULL,
           "two arrays should be rejected");
    EXPECT(json_parse("true false") == NULL,
           "two literals should be rejected");
    EXPECT(json_parse("null,") == NULL,
           "trailing comma should be rejected");
    EXPECT(json_parse("{}{}") == NULL,
           "two objects should be rejected");
    EXPECT(json_parse("[1] [2]") == NULL,
           "array followed by array should be rejected");
}

static void test_trailing_whitespace_still_accepted(void) {
    json_value *v;
    EXPECT((v = json_parse("{\"a\":1}   ")) != NULL, "trailing spaces rejected");
    json_free(v);
    EXPECT((v = json_parse("  42\t\n")) != NULL, "leading+trailing ws rejected");
    json_free(v);
    EXPECT((v = json_parse("[]\r\n")) != NULL, "trailing CRLF rejected");
    json_free(v);
    EXPECT((v = json_parse("  null  ")) != NULL, "null with surrounding ws rejected");
    json_free(v);
}

static void test_plain_documents_still_parse(void) {
    json_value *v;
    EXPECT((v = json_parse("{\"a\":1}")) != NULL, "plain object rejected");
    if (v) {
        EXPECT(json_get_num(json_obj_get(v, "a"), -1) == 1.0, "a != 1");
        json_free(v);
    }
    EXPECT((v = json_parse("[1,2,3]")) != NULL, "plain array rejected");
    if (v) {
        EXPECT(json_arr_count(v) == 3, "array count != 3");
        json_free(v);
    }
    EXPECT((v = json_parse("\"hello\"")) != NULL, "plain string rejected");
    json_free(v);
    EXPECT((v = json_parse("3.14")) != NULL, "plain number rejected");
    json_free(v);
    EXPECT((v = json_parse("")) == NULL, "empty string should be rejected");
    EXPECT((v = json_parse("   ")) == NULL, "whitespace-only should be rejected");
}

int main(void) {
    test_trailing_garbage_rejected();
    test_trailing_whitespace_still_accepted();
    test_plain_documents_still_parse();
    if (failures) {
        fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    printf("ok\n");
    return 0;
}
