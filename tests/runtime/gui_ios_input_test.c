/* Host-side checks of the exact session/Unicode reducer used by UIKit callbacks.
 * Only the UIKit/dispatch adapter is excluded; no copied input implementation. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ZAN_GUI_IOS_INPUT_TEST
#include "../../src/drivers/gui/gui_runtime_ios_input.c"

#define CHECK(test, message) do { if (!(test)) { \
    fprintf(stderr, "FAIL: %s (line %d)\n", message, __LINE__); exit(1); \
} } while (0)
#define COUNT(array) (sizeof(array) / sizeof((array)[0]))

typedef struct {
    zan_ios_input_state_t state;
    zan_ios_input_snapshot_t native;
    uint32_t events[8192];
    size_t count;
    unsigned batches, clears;
    int reject;
    int notify_during_clear;
} fixture_t;

static int capture(void *context, const uint32_t *events, size_t count) {
    fixture_t *fixture = (fixture_t *)context;
    if (fixture->reject) return 0;
    CHECK(count <= COUNT(fixture->events) - fixture->count, "test event buffer capacity");
    memcpy(fixture->events + fixture->count, events, count * sizeof(uint32_t));
    fixture->count += count;
    ++fixture->batches;
    return 1;
}

static void clear_native(void *context) {
    fixture_t *fixture = (fixture_t *)context;
    ++fixture->clears;
    /* setText: may notify synchronously before the native text is replaced. */
    if (fixture->notify_during_clear) {
        CHECK(zan_ios_input_flush(&fixture->state, &fixture->native, capture,
                                 clear_native, fixture), "suppressed clear notification");
        CHECK(zan_ios_input_begin(&fixture->state, 0, 0, fixture->native.length) == 0,
              "setText callback must not begin another operation");
    }
    memset(&fixture->native, 0, sizeof(fixture->native));
}

static void start(fixture_t *fixture) {
    memset(fixture, 0, sizeof(*fixture));
    zan_ios_input_start(&fixture->state);
}

static void flush(fixture_t *fixture) {
    CHECK(zan_ios_input_flush(&fixture->state, &fixture->native, capture,
                             clear_native, fixture), "flush production reducer");
}

static void end(fixture_t *fixture, unsigned generation) {
    CHECK(zan_ios_input_end(&fixture->state, generation, &fixture->native,
                           capture, clear_native, fixture), "finish production operation");
}

static void text(fixture_t *fixture, const uint16_t *value, size_t length) {
    fixture->native.text = value;
    fixture->native.length = length;
    fixture->native.has_marked = 0;
    fixture->native.marked_text = NULL;
    fixture->native.marked_length = 0;
}

static void marked(fixture_t *fixture, const uint16_t *value, size_t length) {
    fixture->native.has_marked = 1;
    fixture->native.marked_text = value;
    fixture->native.marked_length = length;
}

static void expect(const fixture_t *fixture, const uint32_t *events, size_t count) {
    CHECK(fixture->count == count, "exact event count, including duplicate detection");
    for (size_t i = 0; i < count; ++i) {
        CHECK(fixture->events[i] == events[i], "event must contain a complete Unicode scalar");
    }
}

static void composition_commit(void) {
    fixture_t fixture;
    start(&fixture);
    const uint16_t pinyin[] = {'n', 'i'};
    const uint16_t han[] = {0x4f60, 0x597d};
    unsigned operation = zan_ios_input_begin(&fixture.state, 0, 0, fixture.native.length);
    marked(&fixture, pinyin, COUNT(pinyin));
    flush(&fixture); /* Delegate notification inside setMarkedText. */
    CHECK(fixture.state.composing == NULL, "nested notification cannot publish transient state");
    end(&fixture, operation);
    CHECK(fixture.count == 0 && fixture.clears == 0, "marked text stays in UIKit");
    CHECK(strcmp(fixture.state.composing, "ni") == 0, "marked text snapshot is real");

    operation = zan_ios_input_begin(&fixture.state, 0, 1, fixture.native.length); /* insertText */
    unsigned unmark = zan_ios_input_begin(&fixture.state, 0, 1, fixture.native.length);
    text(&fixture, han, COUNT(han));
    end(&fixture, unmark);
    flush(&fixture);
    CHECK(fixture.count == 0, "inner unmark and delegate must not commit twice");
    fixture.notify_during_clear = 1;
    end(&fixture, operation);
    const uint32_t expected[] = {0x4f60, 0x597d};
    expect(&fixture, expected, COUNT(expected));
    CHECK(fixture.batches == 1 && fixture.clears == 1, "one atomic commit and one clear");
    CHECK(fixture.state.composing == NULL, "commit clears composition snapshot");
    flush(&fixture); /* Late delegate notification reads the cleared native view. */
    unsigned late_unmark = zan_ios_input_begin(&fixture.state, 0, 0, fixture.native.length);
    end(&fixture, late_unmark);
    expect(&fixture, expected, COUNT(expected));
    zan_ios_input_stop(&fixture.state);
    puts("CJK commit: nested methods and notifications emit once");
}

static void unmark_commit_and_delete(void) {
    fixture_t fixture;
    start(&fixture);
    const uint16_t han[] = {0x4e2d};
    unsigned operation = zan_ios_input_begin(&fixture.state, 0, 0, fixture.native.length);
    marked(&fixture, han, COUNT(han));
    end(&fixture, operation);
    CHECK(strcmp(fixture.state.composing, "\xe4\xb8\xad") == 0,
          "CJK marked snapshot is UTF-8");
    operation = zan_ios_input_begin(&fixture.state, 0, 1, fixture.native.length); /* Standalone unmark. */
    text(&fixture, han, COUNT(han));
    end(&fixture, operation);
    const uint32_t committed[] = {0x4e2d};
    expect(&fixture, committed, COUNT(committed));

    operation = zan_ios_input_begin(&fixture.state, 1, 0, fixture.native.length);
    unsigned nested = zan_ios_input_begin(&fixture.state, 1, 0, fixture.native.length);
    end(&fixture, nested);
    end(&fixture, operation);
    flush(&fixture);
    const uint32_t deleted[] = {0x4e2d, 8};
    expect(&fixture, deleted, COUNT(deleted));
    CHECK(fixture.batches == 2, "one deleteBackward produces one code8 event");

    marked(&fixture, han, COUNT(han));
    flush(&fixture);
    operation = zan_ios_input_begin(&fixture.state, 1, 1, fixture.native.length);
    marked(&fixture, NULL, 0); /* IME removed a character, marked range remains. */
    end(&fixture, operation);
    expect(&fixture, deleted, COUNT(deleted));
    CHECK(fixture.state.composing && !*fixture.state.composing,
          "empty marked range is still native composition");
    operation = zan_ios_input_begin(&fixture.state, 1, 1, fixture.native.length);
    memset(&fixture.native, 0, sizeof(fixture.native)); /* Last marked char cancelled. */
    end(&fixture, operation);
    expect(&fixture, deleted, COUNT(deleted));
    CHECK(fixture.state.composing == NULL, "cancellation clears marked text without document delete");
    zan_ios_input_stop(&fixture.state);
    puts("delete: empty proxy forwards code8; marked deletion stays native");
}

static void unicode_and_newline(void) {
    fixture_t fixture;
    start(&fixture);
    const uint16_t value[] = {'A', 0x4e2d, 0xd83d, 0xde00, 0xd83d, 0xdc69,
                              0x200d, 0xd83d, 0xdcbb, 'e', 0x0301,
                              '\r', '\n', '\n', '\t', 0xd800, 'B', 0xdc00};
    const uint32_t expected[] = {'A', 0x4e2d, 0x1f600, 0x1f469, 0x200d, 0x1f4bb,
                                 'e', 0x0301, 13, 13, 9, 0xfffd, 'B', 0xfffd};
    unsigned operation = zan_ios_input_begin(&fixture.state, 0, 0, fixture.native.length);
    text(&fixture, value, COUNT(value));
    fixture.notify_during_clear = 1;
    end(&fixture, operation);
    flush(&fixture);
    expect(&fixture, expected, COUNT(expected));
    const uint16_t emoji[] = {0xd83d, 0xde00};
    marked(&fixture, emoji, COUNT(emoji));
    flush(&fixture);
    CHECK(strcmp(fixture.state.composing, "\xf0\x9f\x98\x80") == 0,
          "marked emoji encodes one scalar to four UTF-8 bytes");
    zan_ios_input_stop(&fixture.state);
    puts("Unicode: surrogate pairs, ZWJ, combining marks, invalid surrogates and CRLF");
}

static void session_lifetime(void) {
    fixture_t fixture;
    start(&fixture);
    const uint16_t old[] = {'o', 'l', 'd'};
    const uint16_t fresh[] = {'n'};
    marked(&fixture, old, COUNT(old));
    flush(&fixture);
    unsigned stale = zan_ios_input_begin(&fixture.state, 0, 1, fixture.native.length);
    zan_ios_input_stop(&fixture.state);
    CHECK(!fixture.state.active && !fixture.state.composing, "close releases composition");
    text(&fixture, old, COUNT(old));
    end(&fixture, stale);
    flush(&fixture);
    CHECK(fixture.count == 0, "late callback after close cannot commit");
    zan_ios_input_start(&fixture.state);
    unsigned operation = zan_ios_input_begin(&fixture.state, 0, 0, fixture.native.length);
    text(&fixture, fresh, COUNT(fresh));
    end(&fixture, stale);
    CHECK(fixture.state.depth == 1, "old session cannot finish the new operation");
    end(&fixture, operation);
    const uint32_t expected[] = {'n'};
    expect(&fixture, expected, COUNT(expected));
    zan_ios_input_stop(&fixture.state);
    puts("session: close/reopen rejects callbacks from the previous generation");
}

static void long_commit_and_retry(void) {
    fixture_t fixture;
    start(&fixture);
    uint16_t paste[4096];
    for (size_t i = 0; i < COUNT(paste); ++i) paste[i] = (uint16_t)('a' + i % 26);
    unsigned operation = zan_ios_input_begin(&fixture.state, 0, 0, fixture.native.length);
    text(&fixture, paste, COUNT(paste));
    fixture.reject = 1;
    CHECK(!zan_ios_input_end(&fixture.state, operation, &fixture.native,
                            capture, clear_native, &fixture), "failed atomic enqueue is reported");
    CHECK(fixture.count == 0 && fixture.clears == 0, "failed commit retains all native text");
    CHECK(fixture.native.length == COUNT(paste), "paste is not cleared or truncated");
    fixture.reject = 0;
    flush(&fixture);
    CHECK(fixture.count == COUNT(paste) && fixture.batches == 1, "long paste commits as one batch");
    for (size_t i = 0; i < COUNT(paste); ++i) CHECK(fixture.events[i] == paste[i], "paste scalar order");
    flush(&fixture);
    CHECK(fixture.count == COUNT(paste), "retry cannot duplicate committed text");
    operation = zan_ios_input_begin(&fixture.state, 1, 0, fixture.native.length);
    fixture.reject = 1;
    CHECK(!zan_ios_input_end(&fixture.state, operation, &fixture.native,
                            capture, clear_native, &fixture), "failed delete enqueue is reported");
    fixture.reject = 0;
    flush(&fixture);
    CHECK(fixture.events[fixture.count - 1] == 8 && fixture.count == COUNT(paste) + 1,
          "retry preserves exactly one pending backspace");
    zan_ios_input_stop(&fixture.state);
    puts("atomic enqueue: long paste and pending delete survive rejection without replay");
}

static void failed_paste_then_delete(const uint16_t *paste, size_t length,
                                      size_t remaining_length,
                                      const uint32_t *remaining, size_t scalar_count) {
    fixture_t fixture;
    start(&fixture);
    unsigned operation = zan_ios_input_begin(&fixture.state, 0, 0, fixture.native.length);
    text(&fixture, paste, length);
    fixture.reject = 1;
    CHECK(!zan_ios_input_end(&fixture.state, operation, &fixture.native,
                            capture, clear_native, &fixture), "retain failed paste before delete");
    CHECK(fixture.native.length == length && fixture.count == 0,
          "failed paste never reaches the existing document");

    operation = zan_ios_input_begin(&fixture.state, 1, 0, fixture.native.length);
    /* Only inject UIKit's resulting buffer; the production core decides whether
     * this operation also needs a document backspace. No copied reducer logic. */
    text(&fixture, paste, remaining_length);
    CHECK(!zan_ios_input_end(&fixture.state, operation, &fixture.native,
                            capture, clear_native, &fixture), "edited staging retries enqueue");
    CHECK(fixture.state.deletes == 0 && fixture.count == 0,
          "delete of staged text must not accumulate a document backspace");
    fixture.reject = 0;
    fixture.notify_during_clear = 1;
    flush(&fixture);
    expect(&fixture, remaining, scalar_count);
    flush(&fixture);
    expect(&fixture, remaining, scalar_count);
    CHECK(fixture.batches == 1 && fixture.clears == 1,
          "remaining staging commits once, preserving the existing document prefix");
    zan_ios_input_stop(&fixture.state);

    /* Keep rejecting while deleting the entire native paste. Crossing from one
     * native character to empty is still a local edit. Only the following empty
     * proxy backspace is allowed to remove the existing document's last scalar. */
    start(&fixture);
    operation = zan_ios_input_begin(&fixture.state, 0, 0, fixture.native.length);
    text(&fixture, paste, length);
    fixture.reject = 1;
    CHECK(!zan_ios_input_end(&fixture.state, operation, &fixture.native,
                            capture, clear_native, &fixture), "retain second failed paste");
    operation = zan_ios_input_begin(&fixture.state, 1, 0, fixture.native.length);
    text(&fixture, paste, remaining_length);
    CHECK(!zan_ios_input_end(&fixture.state, operation, &fixture.native,
                            capture, clear_native, &fixture), "first local delete keeps remaining staging");
    operation = zan_ios_input_begin(&fixture.state, 1, 0, fixture.native.length);
    text(&fixture, NULL, 0);
    end(&fixture, operation);
    CHECK(fixture.state.deletes == 0 && fixture.count == 0 && fixture.clears == 0,
          "deleting the last staged character leaves document Z unchanged");
    operation = zan_ios_input_begin(&fixture.state, 1, 0, fixture.native.length);
    unsigned nested = zan_ios_input_begin(&fixture.state, 1, 0, fixture.native.length);
    end(&fixture, nested);
    CHECK(!zan_ios_input_end(&fixture.state, operation, &fixture.native,
                            capture, clear_native, &fixture), "document delete can also await retry");
    CHECK(fixture.state.deletes == 1, "only the empty-proxy outer delete reaches document");
    fixture.reject = 0;
    flush(&fixture);
    const uint32_t document_delete[] = {8};
    expect(&fixture, document_delete, COUNT(document_delete));
    flush(&fixture);
    expect(&fixture, document_delete, COUNT(document_delete));
    zan_ios_input_stop(&fixture.state);
}

static void staged_delete_after_enqueue_failure(void) {
    const uint16_t ascii[] = {'A', 'B'};
    const uint32_t ascii_remaining[] = {'A'};
    failed_paste_then_delete(ascii, COUNT(ascii), 1,
                             ascii_remaining, COUNT(ascii_remaining));
    const uint16_t cjk[] = {0x4e2d, 0x6587};
    const uint32_t cjk_remaining[] = {0x4e2d};
    failed_paste_then_delete(cjk, COUNT(cjk), 1,
                             cjk_remaining, COUNT(cjk_remaining));
    const uint16_t unicode[] = {0xd83d, 0xde00, 0xd83d, 0xdc69,
                                0x200d, 0xd83d, 0xdcbb};
    const uint32_t unicode_remaining[] = {0x1f600};
    /* UIKit may delete an entire ZWJ cluster; no UTF-16 unit is sent as code8. */
    failed_paste_then_delete(unicode, COUNT(unicode), 2,
                             unicode_remaining, COUNT(unicode_remaining));
    puts("failed paste deletion: edits staging only; empty proxy alone deletes document");
}

static void caret_points(void) {
    double x, y;
    zan_ios_input_point(120, 240, 2, &x, &y);
    CHECK(x == 60 && y == 120, "2x device pixels to points");
    zan_ios_input_point(-9, 301, 3, &x, &y);
    CHECK(x == -3 && fabs(y - 100.33333333333333) < 1e-10,
          "3x conversion preserves sub-point caret and negative coordinates");
    zan_ios_input_point(7, 9, 0, &x, &y);
    CHECK(x == 7 && y == 9, "invalid scale does not divide by zero");
    puts("caret: device pixels convert once at the native view scale");
}

int main(void) {
    composition_commit();
    unmark_commit_and_delete();
    unicode_and_newline();
    session_lifetime();
    long_commit_and_retry();
    staged_delete_after_enqueue_failure();
    caret_points();
    puts("iOS input session tests passed");
    return 0;
}
