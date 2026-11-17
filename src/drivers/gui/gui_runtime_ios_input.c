/* Included by gui_runtime_ios.c; the host test includes the same session core. */
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

typedef struct {
    const uint16_t *text;
    size_t length;
    const uint16_t *marked_text;
    size_t marked_length;
    int has_marked;
} zan_ios_input_snapshot_t;

typedef struct {
    unsigned generation;
    unsigned depth;
    size_t deletes;
    int active;
    int suppress;
    char *composing;
} zan_ios_input_state_t;

typedef int (*zan_ios_input_emit_fn)(void *, const uint32_t *, size_t);
typedef void (*zan_ios_input_clear_fn)(void *);

static uint32_t zan_ios_input_scalar(const uint16_t *text, size_t length, size_t *at) {
    uint32_t cp = text[(*at)++];
    if (cp >= 0xd800 && cp <= 0xdbff) {
        if (*at < length && text[*at] >= 0xdc00 && text[*at] <= 0xdfff) {
            cp = 0x10000 + ((cp - 0xd800) << 10) + (text[(*at)++] - 0xdc00);
        } else {
            cp = 0xfffd;
        }
    } else if (cp >= 0xdc00 && cp <= 0xdfff) {
        cp = 0xfffd;
    }
    return cp;
}

static char *zan_ios_input_utf8(const uint16_t *text, size_t length) {
    if (length > (SIZE_MAX - 1) / 3 || (length && !text)) return NULL;
    char *out = (char *)malloc(length * 3 + 1);
    if (!out) return NULL;
    size_t at = 0, used = 0;
    while (at < length) {
        uint32_t cp = zan_ios_input_scalar(text, length, &at);
        if (cp < 0x80) {
            out[used++] = (char)cp;
        } else if (cp < 0x800) {
            out[used++] = (char)(0xc0 | (cp >> 6));
            out[used++] = (char)(0x80 | (cp & 63));
        } else if (cp < 0x10000) {
            out[used++] = (char)(0xe0 | (cp >> 12));
            out[used++] = (char)(0x80 | ((cp >> 6) & 63));
            out[used++] = (char)(0x80 | (cp & 63));
        } else {
            out[used++] = (char)(0xf0 | (cp >> 18));
            out[used++] = (char)(0x80 | ((cp >> 12) & 63));
            out[used++] = (char)(0x80 | ((cp >> 6) & 63));
            out[used++] = (char)(0x80 | (cp & 63));
        }
    }
    out[used] = 0;
    return out;
}

static void zan_ios_input_stop(zan_ios_input_state_t *state) {
    state->active = 0;
    if (++state->generation == 0) ++state->generation;
    state->depth = 0;
    state->deletes = 0;
    free(state->composing);
    state->composing = NULL;
}

static void zan_ios_input_start(zan_ios_input_state_t *state) {
    zan_ios_input_stop(state);
    state->active = 1;
}

static unsigned zan_ios_input_begin(zan_ios_input_state_t *state, int deletion,
                                     int has_marked, size_t staged_length) {
    if (!state->active || state->suppress) return 0;
    /* UIKit deletes uncommitted staging text itself, including text retained
     * after a failed enqueue. Only an already empty proxy deletes the document. */
    if (state->depth++ == 0 && deletion && !has_marked && !staged_length) ++state->deletes;
    return state->generation;
}

/* emit is atomic: a full native queue must not commit half a paste and replay it.
 * clear calls back into UIKit, so suppression also covers nested notifications.
 * Text is never sent while UIKit still owns a marked range. */
static int zan_ios_input_flush(zan_ios_input_state_t *state,
                              const zan_ios_input_snapshot_t *snapshot,
                              zan_ios_input_emit_fn emit,
                              zan_ios_input_clear_fn clear, void *context) {
    if (!state->active || state->suppress || state->depth) return 1;
    if (snapshot->has_marked) {
        char *text = zan_ios_input_utf8(snapshot->marked_text, snapshot->marked_length);
        if (!text) return 0;
        free(state->composing);
        state->composing = text;
        return 1;
    }
    free(state->composing);
    state->composing = NULL;
    if (snapshot->length && !snapshot->text) return 0;
    if (state->deletes > SIZE_MAX - snapshot->length) return 0;
    size_t capacity = state->deletes + snapshot->length;
    if (!capacity) return 1;
    if (capacity > SIZE_MAX / sizeof(uint32_t)) return 0;
    uint32_t *events = (uint32_t *)malloc(capacity * sizeof(uint32_t));
    if (!events) return 0;
    size_t count = 0, at = 0;
    for (size_t i = 0; i < state->deletes; ++i) events[count++] = 8;
    while (at < snapshot->length) {
        uint32_t cp = zan_ios_input_scalar(snapshot->text, snapshot->length, &at);
        if (cp == '\r') {
            if (at < snapshot->length && snapshot->text[at] == '\n') ++at;
            cp = 13;
        } else if (cp == '\n') {
            cp = 13;
        }
        events[count++] = cp;
    }
    int accepted = emit(context, events, count);
    free(events);
    if (!accepted) return 0;
    state->deletes = 0;
    state->suppress = 1;
    clear(context);
    state->suppress = 0;
    return 1;
}

static int zan_ios_input_end(zan_ios_input_state_t *state, unsigned generation,
                            const zan_ios_input_snapshot_t *snapshot,
                            zan_ios_input_emit_fn emit,
                            zan_ios_input_clear_fn clear, void *context) {
    if (!generation || !state->active || generation != state->generation) return 1;
    if (state->depth) --state->depth;
    return zan_ios_input_flush(state, snapshot, emit, clear, context);
}

static void zan_ios_input_point(int x, int y, double scale, double *px, double *py) {
    if (!isfinite(scale) || scale <= 0) scale = 1;
    *px = (double)x / scale;
    *py = (double)y / scale;
}

#ifndef ZAN_GUI_IOS_INPUT_TEST

/* iOS ARM64: NSInteger/NSUInteger are 64 bit; NSRange is two integer registers,
 * CGRect uses indirect return and floating-point arguments. No variadic calls. */
typedef struct { uintptr_t location, length; } zan_ios_range_t;
typedef void (*zan_ios_void0_fn)(zan_id_t, zan_sel_t);
typedef bool (*zan_ios_bool0_fn)(zan_id_t, zan_sel_t);
typedef void (*zan_ios_void1_fn)(zan_id_t, zan_sel_t, zan_id_t);
typedef void (*zan_ios_int1_fn)(zan_id_t, zan_sel_t, intptr_t);
typedef uintptr_t (*zan_ios_uint0_fn)(zan_id_t, zan_sel_t);
typedef void (*zan_ios_chars_fn)(zan_id_t, zan_sel_t, uint16_t *, zan_ios_range_t);
typedef void (*zan_ios_mark_fn)(zan_id_t, zan_sel_t, zan_id_t, zan_ios_range_t);
typedef void (*zan_ios_rect1_fn)(zan_id_t, zan_sel_t, zan_cg_rect_t);
typedef zan_cg_rect_t (*zan_ios_rect_obj_fn)(zan_id_t, zan_sel_t, zan_id_t);
typedef zan_cg_rect_t (*zan_ios_convert_fn)(zan_id_t, zan_sel_t, zan_cg_rect_t, zan_id_t);
typedef void (*zan_ios_observe_fn)(zan_id_t, zan_sel_t, zan_id_t, zan_sel_t, zan_id_t, zan_id_t);
typedef bool (*zan_ios_point_event_fn)(zan_id_t, zan_sel_t, zan_cg_point_t, zan_id_t);

static zan_ios_input_state_t g_ios_input;
static zan_id_t g_ios_input_view;
static int g_ios_input_requested;
static int g_ios_input_suspended;
static int g_ios_input_x, g_ios_input_y;
/* C API result belongs to the querying thread, not to UIKit callbacks. */
static pthread_key_t g_ios_input_query_key;
static pthread_once_t g_ios_input_query_once = PTHREAD_ONCE_INIT;
static int g_ios_input_query_key_status;
static zan_ios_void1_fn g_ios_super_insert;
static zan_ios_mark_fn g_ios_super_mark;
static zan_ios_void0_fn g_ios_super_unmark, g_ios_super_delete;
static void (*g_ios_super_replace)(zan_id_t, zan_sel_t, zan_id_t, zan_id_t);

static zan_sel_t zan_ios_input_sel(const char *name) {
    return g_bridge.sel_registerName(name);
}

static void zan_ios_input_void(zan_id_t object, const char *name) {
    ((zan_ios_void0_fn)g_bridge.objc_msgSend)(object, zan_ios_input_sel(name));
}

static bool zan_ios_input_bool(zan_id_t object, const char *name) {
    return ((zan_ios_bool0_fn)g_bridge.objc_msgSend)(object, zan_ios_input_sel(name));
}

static void zan_ios_input_object(zan_id_t object, const char *name, zan_id_t value) {
    ((zan_ios_void1_fn)g_bridge.objc_msgSend)(object, zan_ios_input_sel(name), value);
}

static void zan_ios_input_integer(zan_id_t object, const char *name, intptr_t value) {
    ((zan_ios_int1_fn)g_bridge.objc_msgSend)(object, zan_ios_input_sel(name), value);
}

static int zan_ios_input_on_main(void (*work)(void *), void *context) {
    if (!g_bridge.initialized || !p_pthread_main_np) {
        fprintf(stderr, "iOS text input: main-thread bridge unavailable\n");
        return 0;
    }
    if (p_pthread_main_np()) {
        work(context);
        return 1;
    }
    if (!p_dispatch_sync_f || !p_dispatch_main_q) {
        fprintf(stderr, "iOS text input: main-queue dispatch unavailable\n");
        return 0;
    }
    p_dispatch_sync_f(p_dispatch_main_q, context, work);
    return 1;
}

static int zan_ios_input_emit(void *context, const uint32_t *events, size_t count) {
    (void)context;
    pthread_mutex_lock(&g_iq_lock);
    int accepted = iq_reserve_locked(count);
    if (accepted) {
        for (size_t i = 0; i < count; ++i) {
            iq_push_locked(6, 0, 0, 0, (int)events[i], 0, 0);
        }
    }
    pthread_mutex_unlock(&g_iq_lock);
    return accepted;
}

static void zan_ios_input_clear(void *context) {
    zan_id_t view = (zan_id_t)context;
    zan_id_t empty = zan_send1_str((zan_id_t)g_bridge.objc_getClass("NSString"),
                                  zan_ios_input_sel("stringWithUTF8String:"), "");
    zan_ios_input_object(view, "setText:", empty);
}

static int zan_ios_input_copy_string(zan_id_t string, uint16_t **text, size_t *length) {
    *text = NULL;
    *length = 0;
    if (!string) return 1;
    uintptr_t count = ((zan_ios_uint0_fn)g_bridge.objc_msgSend)(string,
                                                            zan_ios_input_sel("length"));
    if (!count) return 1;
    if (count > SIZE_MAX / sizeof(uint16_t)) return 0;
    *text = (uint16_t *)malloc((size_t)count * sizeof(uint16_t));
    if (!*text) return 0;
    zan_ios_range_t range = {0, count};
    ((zan_ios_chars_fn)g_bridge.objc_msgSend)(string,
        zan_ios_input_sel("getCharacters:range:"), *text, range);
    *length = (size_t)count;
    return 1;
}

static int zan_ios_input_has_marked(zan_id_t view) {
    return zan_send0(view, zan_ios_input_sel("markedTextRange")) != NULL;
}

static void zan_ios_input_collect(zan_id_t view, unsigned generation, int ending) {
    if (!g_ios_input.active || g_ios_input.suppress || view != g_ios_input_view) return;
    /* Nested UIKit operations can notify before the outer method finishes. */
    if (ending && generation != g_ios_input.generation) return;
    if (ending && g_ios_input.depth > 1) {
        --g_ios_input.depth;
        return;
    }
    if (!ending && g_ios_input.depth) return;
    uint16_t *text = NULL, *marked = NULL;
    zan_ios_input_snapshot_t snapshot = {0};
    zan_id_t range = zan_send0(view, zan_ios_input_sel("markedTextRange"));
    snapshot.has_marked = range != NULL;
    zan_id_t string = range ? zan_send1(view, zan_ios_input_sel("textInRange:"), range)
                           : zan_send0(view, zan_ios_input_sel("text"));
    int copied = range ? zan_ios_input_copy_string(string, &marked, &snapshot.marked_length)
                       : zan_ios_input_copy_string(string, &text, &snapshot.length);
    snapshot.text = text;
    snapshot.marked_text = marked;
    if (!copied) {
        if (ending && g_ios_input.depth) --g_ios_input.depth;
        fprintf(stderr, "iOS text input: cannot copy native text\n");
    } else {
        int had_composing = g_ios_input.composing && *g_ios_input.composing;
        int accepted = ending ? zan_ios_input_end(&g_ios_input, generation, &snapshot,
                                                zan_ios_input_emit, zan_ios_input_clear, view)
                              : zan_ios_input_flush(&g_ios_input, &snapshot,
                                                  zan_ios_input_emit, zan_ios_input_clear, view);
        if (!accepted) fprintf(stderr, "iOS text input: cannot queue native text\n");
        if (snapshot.has_marked || had_composing) {
            pthread_mutex_lock(&g_iq_lock);
            iq_push_locked(0, 0, 0, 0, 0, 0, 0);
            pthread_mutex_unlock(&g_iq_lock);
        }
    }
    free(text);
    free(marked);
}

static unsigned zan_ios_input_enter(zan_id_t view, int deletion) {
    if (view != g_ios_input_view) return 0;
    size_t staged_length = 0;
    if (deletion) {
        zan_id_t text = zan_send0(view, zan_ios_input_sel("text"));
        if (text) {
            staged_length = (size_t)((zan_ios_uint0_fn)g_bridge.objc_msgSend)(text,
                                                        zan_ios_input_sel("length"));
        }
    }
    return zan_ios_input_begin(&g_ios_input, deletion, zan_ios_input_has_marked(view),
                               staged_length);
}

static void zan_ios_input_insert(zan_id_t self, zan_sel_t command, zan_id_t text) {
    unsigned generation = zan_ios_input_enter(self, 0);
    g_ios_super_insert(self, command, text);
    if (generation) zan_ios_input_collect(self, generation, 1);
}

static void zan_ios_input_mark(zan_id_t self, zan_sel_t command, zan_id_t text,
                               zan_ios_range_t selection) {
    unsigned generation = zan_ios_input_enter(self, 0);
    g_ios_super_mark(self, command, text, selection);
    if (generation) zan_ios_input_collect(self, generation, 1);
}

static void zan_ios_input_unmark(zan_id_t self, zan_sel_t command) {
    unsigned generation = zan_ios_input_enter(self, 0);
    g_ios_super_unmark(self, command);
    if (generation) zan_ios_input_collect(self, generation, 1);
}

static void zan_ios_input_delete(zan_id_t self, zan_sel_t command) {
    unsigned generation = zan_ios_input_enter(self, 1);
    g_ios_super_delete(self, command);
    if (generation) zan_ios_input_collect(self, generation, 1);
}

static void zan_ios_input_replace(zan_id_t self, zan_sel_t command,
                                  zan_id_t range, zan_id_t text) {
    unsigned generation = zan_ios_input_enter(self, 0);
    g_ios_super_replace(self, command, range, text);
    if (generation) zan_ios_input_collect(self, generation, 1);
}

static void zan_ios_input_changed(zan_id_t self, zan_sel_t command, zan_id_t view) {
    (void)command;
    if (self == view) zan_ios_input_collect(view, 0, 0);
}

static void zan_ios_input_did_end(zan_id_t self, zan_sel_t command, zan_id_t view) {
    (void)command;
    if (self != g_ios_input_view || self != view || g_ios_input.suppress) return;
    zan_ios_input_stop(&g_ios_input);
    if (!g_ios_input_suspended) g_ios_input_requested = 0;
    g_ios_input.suppress = 1;
    zan_ios_input_clear(view);
    g_ios_input.suppress = 0;
    g_ios_keyboard_height = 0;
}

static zan_cg_rect_t zan_ios_input_caret(zan_id_t self, zan_sel_t command, zan_id_t position) {
    (void)self; (void)command; (void)position;
    zan_cg_rect_t rect = {{0, 0}, {1, 1}};
    return rect;
}

static bool zan_ios_input_has_text(zan_id_t self, zan_sel_t command) {
    (void)command;
    /* The document is held by Zan, even when the native staging buffer is empty. */
    return self == g_ios_input_view && g_ios_input.active;
}

static bool zan_ios_input_hit(zan_id_t self, zan_sel_t command,
                              zan_cg_point_t point, zan_id_t event) {
    (void)self; (void)command; (void)point; (void)event;
    return false; /* Transparent input proxy must not steal the Zan view's taps. */
}

static zan_id_t zan_ios_input_constant(const char *name) {
    zan_id_t *value = (zan_id_t *)dlsym(RTLD_DEFAULT, name);
    return value ? *value : NULL;
}

static void zan_ios_input_keyboard(zan_id_t self, zan_sel_t command, zan_id_t notification) {
    (void)command;
    if (self != g_ios_input_view || !g_ios_input.active) return;
    zan_id_t name = zan_send0(notification, zan_ios_input_sel("name"));
    if (((zan_msg_bool1_fn)g_bridge.objc_msgSend)(name, zan_ios_input_sel("isEqualToString:"),
                                               zan_ios_input_constant("UIKeyboardWillHideNotification"))) {
        g_ios_keyboard_height = 0;
        return;
    }
    zan_id_t info = zan_send0(notification, zan_ios_input_sel("userInfo"));
    zan_id_t value = zan_send1(info, zan_ios_input_sel("objectForKey:"),
                             zan_ios_input_constant("UIKeyboardFrameEndUserInfoKey"));
    if (!value || !g_iwin.ui_view || !g_iwin.ui_window) return;
    zan_cg_rect_t frame = ((zan_msg_rect0_fn)g_bridge.objc_msgSend)(value,
                                                        zan_ios_input_sel("CGRectValue"));
    zan_id_t screen = zan_send0(g_iwin.ui_window, zan_ios_input_sel("screen"));
    zan_id_t space = zan_send0(screen, zan_ios_input_sel("coordinateSpace"));
    if (!space) return;
    frame = ((zan_ios_convert_fn)g_bridge.objc_msgSend)(g_iwin.ui_view,
               zan_ios_input_sel("convertRect:fromCoordinateSpace:"), frame, space);
    zan_cg_rect_t bounds = ((zan_msg_rect0_fn)g_bridge.objc_msgSend)(g_iwin.ui_view,
                                                        zan_ios_input_sel("bounds"));
    double bottom = bounds.origin.y + bounds.size.height;
    double height = 0;
    if (frame.origin.x < bounds.origin.x + bounds.size.width &&
        frame.origin.x + frame.size.width > bounds.origin.x &&
        frame.origin.y + frame.size.height >= bottom) {
        height = bottom - fmax(bounds.origin.y, frame.origin.y);
        if (height < 0) height = 0;
    }
    double scale = ((zan_msg_dbl0_fn)g_bridge.objc_msgSend)(g_iwin.ui_view,
                                                   zan_ios_input_sel("contentScaleFactor"));
    g_ios_keyboard_height = (int)ceil(height * scale);
}

static int zan_ios_input_register(void) {
    zan_class_t base = g_bridge.objc_getClass("UITextView");
    if (!base || !g_bridge.class_getInstanceMethod || !g_bridge.method_getImplementation ||
        !g_bridge.objc_allocateClassPair || !g_bridge.class_addMethod ||
        !g_bridge.objc_registerClassPair) return 0;
    const char *names[] = {"insertText:", "setMarkedText:selectedRange:", "unmarkText",
                          "deleteBackward", "replaceRange:withText:"};
    zan_imp_t implementations[5];
    for (int i = 0; i < 5; ++i) {
        zan_method_t method = g_bridge.class_getInstanceMethod(base, zan_ios_input_sel(names[i]));
        if (!method) return 0;
        implementations[i] = g_bridge.method_getImplementation(method);
        if (!implementations[i]) return 0;
    }
    g_ios_super_insert = (zan_ios_void1_fn)implementations[0];
    g_ios_super_mark = (zan_ios_mark_fn)implementations[1];
    g_ios_super_unmark = (zan_ios_void0_fn)implementations[2];
    g_ios_super_delete = (zan_ios_void0_fn)implementations[3];
    g_ios_super_replace = (void (*)(zan_id_t, zan_sel_t, zan_id_t, zan_id_t))implementations[4];
    if (g_bridge.objc_getClass("ZanIOSInputView")) return 1;
    zan_class_t cls = g_bridge.objc_allocateClassPair(base, "ZanIOSInputView", 0);
    if (!cls) return 0;
    struct { const char *name; zan_imp_t implementation; const char *types; } methods[] = {
        {"insertText:", (zan_imp_t)zan_ios_input_insert, "v@:@"},
        {"setMarkedText:selectedRange:", (zan_imp_t)zan_ios_input_mark, "v@:@{_NSRange=QQ}"},
        {"unmarkText", (zan_imp_t)zan_ios_input_unmark, "v@:"},
        {"deleteBackward", (zan_imp_t)zan_ios_input_delete, "v@:"},
        {"replaceRange:withText:", (zan_imp_t)zan_ios_input_replace, "v@:@@"},
        {"textViewDidChange:", (zan_imp_t)zan_ios_input_changed, "v@:@"},
        {"textViewDidEndEditing:", (zan_imp_t)zan_ios_input_did_end, "v@:@"},
        {"caretRectForPosition:", (zan_imp_t)zan_ios_input_caret,
            "{CGRect={CGPoint=dd}{CGSize=dd}}@:@"},
        {"firstRectForRange:", (zan_imp_t)zan_ios_input_caret,
            "{CGRect={CGPoint=dd}{CGSize=dd}}@:@"},
        {"hasText", (zan_imp_t)zan_ios_input_has_text, "B@:"},
        {"pointInside:withEvent:", (zan_imp_t)zan_ios_input_hit, "B@:{CGPoint=dd}@"},
        {"zanKeyboardChanged:", (zan_imp_t)zan_ios_input_keyboard, "v@:@"}
    };
    for (size_t i = 0; i < sizeof(methods) / sizeof(methods[0]); ++i) {
        if (!g_bridge.class_addMethod(cls, zan_ios_input_sel(methods[i].name),
                                     methods[i].implementation, methods[i].types)) {
            void (*dispose)(zan_class_t) = (void (*)(zan_class_t))dlsym(RTLD_DEFAULT,
                                                                    "objc_disposeClassPair");
            if (dispose) dispose(cls);
            return 0;
        }
    }
    g_bridge.objc_registerClassPair(cls);
    return 1;
}

static void zan_ios_input_position(void) {
    if (!g_ios_input_view || !g_iwin.ui_view) return;
    double scale = ((zan_msg_dbl0_fn)g_bridge.objc_msgSend)(g_iwin.ui_view,
                                                   zan_ios_input_sel("contentScaleFactor"));
    zan_cg_rect_t rect = {{0, 0}, {1, 1}};
    zan_ios_input_point(g_ios_input_x, g_ios_input_y, scale, &rect.origin.x, &rect.origin.y);
    ((zan_ios_rect1_fn)g_bridge.objc_msgSend)(g_ios_input_view,
                                            zan_ios_input_sel("setFrame:"), rect);
}

static int zan_ios_input_create(void) {
    if (g_ios_input_view) return 1;
    if (!g_iwin.attached || g_iwin.closed || !g_iwin.ui_view || !zan_ios_input_register()) return 0;
    if (!zan_ios_input_constant("UIKeyboardWillChangeFrameNotification") ||
        !zan_ios_input_constant("UIKeyboardWillHideNotification") ||
        !zan_ios_input_constant("UIKeyboardFrameEndUserInfoKey")) return 0;
    zan_cg_rect_t frame = {{0, 0}, {1, 1}};
    zan_id_t allocated = zan_send0((zan_id_t)g_bridge.objc_getClass("ZanIOSInputView"),
                                  zan_ios_input_sel("alloc"));
    zan_id_t view = zan_send_init_rect(allocated, zan_ios_input_sel("initWithFrame:"), frame);
    if (!view) return 0;
    g_ios_input_view = view; /* Own alloc/init reference, plus the superview's retain. */
    zan_id_t color = zan_send0((zan_id_t)g_bridge.objc_getClass("UIColor"),
                              zan_ios_input_sel("clearColor"));
    zan_ios_input_object(view, "setBackgroundColor:", color);
    zan_ios_input_object(view, "setTextColor:", color);
    zan_ios_input_object(view, "setTintColor:", color);
    zan_send1_bool(view, zan_ios_input_sel("setOpaque:"), false);
    zan_send1_bool(view, zan_ios_input_sel("setScrollEnabled:"), false);
    zan_send1_bool(view, zan_ios_input_sel("setEditable:"), true);
    zan_send1_bool(view, zan_ios_input_sel("setSelectable:"), true);
    zan_ios_input_integer(view, "setAutocapitalizationType:", 0);
    zan_ios_input_integer(view, "setAutocorrectionType:", 1);
    zan_ios_input_integer(view, "setSpellCheckingType:", 1);
    zan_ios_input_integer(view, "setSmartQuotesType:", 1);
    zan_ios_input_integer(view, "setSmartDashesType:", 1);
    zan_ios_input_integer(view, "setSmartInsertDeleteType:", 1);
    zan_ios_input_object(view, "setTextContentType:", NULL);
    zan_ios_input_object(view, "setDelegate:", view);
    zan_ios_input_object(g_iwin.ui_view, "addSubview:", view);
    zan_id_t center = zan_send0((zan_id_t)g_bridge.objc_getClass("NSNotificationCenter"),
                               zan_ios_input_sel("defaultCenter"));
    const char *notifications[] = {"UIKeyboardWillChangeFrameNotification", "UIKeyboardWillHideNotification"};
    for (size_t i = 0; i < 2; ++i) {
        ((zan_ios_observe_fn)g_bridge.objc_msgSend)(center,
            zan_ios_input_sel("addObserver:selector:name:object:"), view,
            zan_ios_input_sel("zanKeyboardChanged:"), zan_ios_input_constant(notifications[i]), NULL);
    }
    zan_ios_input_position();
    return 1;
}

static void zan_ios_input_deactivate(void) {
    zan_ios_input_stop(&g_ios_input); /* Stop before resign/unmark can reenter delegates. */
    if (g_ios_input_view) {
        g_ios_input.suppress = 1;
        zan_ios_input_void(g_ios_input_view, "unmarkText");
        zan_ios_input_clear(g_ios_input_view);
        (void)zan_ios_input_bool(g_ios_input_view, "resignFirstResponder");
        g_ios_input.suppress = 0;
        zan_id_t center = zan_send0((zan_id_t)g_bridge.objc_getClass("NSNotificationCenter"),
                                   zan_ios_input_sel("defaultCenter"));
        zan_ios_input_object(center, "removeObserver:", g_ios_input_view);
        zan_ios_input_object(g_ios_input_view, "setDelegate:", NULL);
        zan_ios_input_void(g_ios_input_view, "removeFromSuperview");
        zan_ios_input_void(g_ios_input_view, "release");
        g_ios_input_view = NULL;
    }
    g_ios_keyboard_height = 0;
}

static void zan_ios_input_apply(void) {
    if (!g_ios_input_requested || g_ios_input_suspended) {
        zan_ios_input_deactivate();
        return;
    }
    if (g_ios_input.active) {
        zan_ios_input_position();
        return;
    }
    if (!zan_ios_input_create()) {
        fprintf(stderr, "iOS text input: cannot create UIKit input view\n");
        return;
    }
    zan_ios_input_start(&g_ios_input);
    if (!zan_ios_input_bool(g_ios_input_view, "becomeFirstResponder")) {
        zan_ios_input_deactivate();
        fprintf(stderr, "iOS text input: UIKit rejected first responder\n");
    }
}

static void zan_ios_input_dispose_on_main(void *context) {
    (void)context;
    g_ios_input_requested = 0;
    zan_ios_input_deactivate();
}

static void zan_ios_input_shutdown(void) {
    (void)zan_ios_input_on_main(zan_ios_input_dispose_on_main, NULL);
}

static void zan_ios_input_resign(zan_id_t self, zan_sel_t command, zan_id_t application) {
    (void)self; (void)command; (void)application;
    g_ios_input_suspended = 1;
    zan_ios_input_deactivate();
}

static void zan_ios_input_resume(zan_id_t self, zan_sel_t command, zan_id_t application) {
    (void)self; (void)command; (void)application;
    g_ios_input_suspended = 0;
    if (g_ios_input_requested) zan_ios_input_apply();
}

static void zan_ios_input_terminate(zan_id_t self, zan_sel_t command, zan_id_t application) {
    (void)self; (void)command; (void)application;
    zan_ios_input_dispose_on_main(NULL);
}

static void zan_ios_input_open_on_main(void *context) {
    g_ios_input_requested = *(int *)context != 0;
    zan_ios_input_apply();
}

EXPORT void zan_gui_set_ime_open(i32 on) {
    int requested = on != 0;
    (void)zan_ios_input_on_main(zan_ios_input_open_on_main, &requested);
}

static void zan_ios_input_pos_on_main(void *context) {
    const int *point = (const int *)context;
    g_ios_input_x = point[0];
    g_ios_input_y = point[1];
    zan_ios_input_position();
}

EXPORT void zan_gui_set_ime_pos(i32 x, i32 y) {
    int point[2] = {x, y};
    (void)zan_ios_input_on_main(zan_ios_input_pos_on_main, point);
}

static void zan_ios_input_query_on_main(void *context) {
    const char *source = g_ios_input.active && g_ios_input.composing ? g_ios_input.composing : "";
    size_t length = strlen(source);
    char *copy = (char *)malloc(length + 1);
    if (!copy) {
        fprintf(stderr, "iOS text input: cannot copy composition snapshot\n");
        return;
    }
    memcpy(copy, source, length + 1);
    *(char **)context = copy;
}

static void zan_ios_input_query_key_init(void) {
    g_ios_input_query_key_status = pthread_key_create(&g_ios_input_query_key, free);
}

EXPORT const char *zan_gui_ime_composing(void) {
    pthread_once(&g_ios_input_query_once, zan_ios_input_query_key_init);
    if (g_ios_input_query_key_status) {
        fprintf(stderr, "iOS text input: cannot create composition result storage\n");
        return "";
    }
    char *result = NULL;
    if (!zan_ios_input_on_main(zan_ios_input_query_on_main, &result) || !result) return "";
    void *previous = pthread_getspecific(g_ios_input_query_key);
    if (pthread_setspecific(g_ios_input_query_key, result)) {
        free(result);
        fprintf(stderr, "iOS text input: cannot retain composition result\n");
        return "";
    }
    free(previous);
    return result; /* Valid until this thread's next query or thread exit. */
}

#endif /* !ZAN_GUI_IOS_INPUT_TEST */
