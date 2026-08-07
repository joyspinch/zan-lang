/* formgen_test.c -- .zform kinds are real Zan Control class names. */
#include "src/compiler/arena.h"
#include "src/compiler/diag.h"
#include "src/compiler/formgen.h"

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

static void test_direct_kinds_and_properties(void) {
    const char *json =
        "{\"name\":\"KindForm\",\"fields\":["
        "{\"kind\":\"Tabs\",\"name\":\"DocumentTabs\","
        "\"options\":[\"One\",\"Two\"]},"
        "{\"kind\":\"MyComponent\",\"name\":\"CustomView\","
        "\"placeholder\":\"Search\"}]}";
    zan_arena_t *arena = zan_arena_new();
    zan_diag_t *diag = zan_diag_new(arena);
    zan_diag_set_capture(diag, true);
    char *out = zan_formgen_translate(json, strlen(json), "KindForm.zform",
                                      diag, 0);

    EXPECT(out != NULL, "translation failed");
    EXPECT(!zan_diag_has_errors(diag), "translation reported an error");
    if (out) {
        EXPECT(strstr(out, "static Tabs DocumentTabs;") != NULL,
               "Tabs field was not strongly typed");
        EXPECT(strstr(out, "DocumentTabs = new Tabs();") != NULL,
               "Tabs constructor was not emitted directly");
        EXPECT(strstr(out, "static MyComponent CustomView;") != NULL,
               "custom field was not strongly typed");
        EXPECT(strstr(out, "CustomView = new MyComponent();") != NULL,
               "custom constructor was not emitted directly");
        EXPECT(strstr(out, "DocumentTabs.SetProp(\"options\", \"One|Two\");") != NULL,
               "options property setup was not emitted for the field");
        EXPECT(strstr(out, "CustomView.SetProp(\"placeholder\", \"Search\");") != NULL,
               "placeholder property setup was not emitted for the field");
        EXPECT(strstr(out, "Panel DocumentTabs") == NULL,
               "Tabs was mapped to Panel");
    }

    free(out);
    zan_diag_free_buffers(diag);
    zan_arena_free(arena);
}

static void test_invalid_kind_diagnostic(void) {
    const char *json =
        "{\"name\":\"BadForm\",\"fields\":["
        "{\"kind\":\"Not-Valid\",\"name\":\"bad\"}]}";
    zan_arena_t *arena = zan_arena_new();
    zan_diag_t *diag = zan_diag_new(arena);
    zan_diag_set_capture(diag, true);
    char *out = zan_formgen_translate(json, strlen(json), "BadForm.zform",
                                      diag, 0);

    EXPECT(out == NULL, "invalid kind should stop translation");
    EXPECT(zan_diag_has_errors(diag), "invalid kind produced no diagnostic");
    EXPECT(zan_diag_entry_count(diag) > 0,
           "invalid kind diagnostic was not captured");

    free(out);
    zan_diag_free_buffers(diag);
    zan_arena_free(arena);
}

int main(void) {
    test_direct_kinds_and_properties();
    test_invalid_kind_diagnostic();
    if (failures) {
        fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    printf("ok\n");
    return 0;
}
