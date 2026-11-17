/* Included by gui_runtime_ohos.c. Optional native IME API (API 12+). */
#ifndef ZAN_GUI_OHOS_NATIVE_TEST
#include <uchar.h>
#include <inputmethod/inputmethod_controller_capi.h>
#include <inputmethod/inputmethod_inputmethod_proxy_capi.h>
#include <inputmethod/inputmethod_cursor_info_capi.h>
#include <inputmethod/inputmethod_text_config_capi.h>
#include <dlfcn.h>
#include <pthread.h>
#endif
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define OHOS_IME_FUNCTIONS(X) \
    X(OH_TextEditorProxy_Create) X(OH_TextEditorProxy_Destroy) \
    X(OH_TextEditorProxy_SetGetTextConfigFunc) X(OH_TextEditorProxy_SetInsertTextFunc) \
    X(OH_TextEditorProxy_SetDeleteForwardFunc) X(OH_TextEditorProxy_SetDeleteBackwardFunc) \
    X(OH_TextEditorProxy_SetSendEnterKeyFunc) X(OH_TextEditorProxy_SetMoveCursorFunc) \
    X(OH_TextEditorProxy_SetSendKeyboardStatusFunc) \
    X(OH_TextEditorProxy_SetHandleSetSelectionFunc) X(OH_TextEditorProxy_SetHandleExtendActionFunc) \
    X(OH_TextEditorProxy_SetGetLeftTextOfCursorFunc) X(OH_TextEditorProxy_SetGetRightTextOfCursorFunc) \
    X(OH_TextEditorProxy_SetGetTextIndexAtCursorFunc) X(OH_TextEditorProxy_SetReceivePrivateCommandFunc) \
    X(OH_TextEditorProxy_SetSetPreviewTextFunc) X(OH_TextEditorProxy_SetFinishTextPreviewFunc) \
    X(OH_InputMethodController_Attach) X(OH_InputMethodController_Detach) \
    X(OH_AttachOptions_Create) X(OH_AttachOptions_Destroy) \
    X(OH_TextConfig_SetInputType) X(OH_TextConfig_SetEnterKeyType) \
    X(OH_TextConfig_SetPreviewTextSupport) X(OH_TextConfig_GetCursorInfo) \
    X(OH_CursorInfo_Create) X(OH_CursorInfo_Destroy) X(OH_CursorInfo_SetRect) \
    X(OH_InputMethodProxy_NotifyCursorUpdate)
#define OHOS_IME_FIELD(name) __typeof__(&name) name;
static struct { OHOS_IME_FUNCTIONS(OHOS_IME_FIELD) } g_ohos_ime;
#undef OHOS_IME_FIELD
static pthread_once_t g_ohos_ime_once = PTHREAD_ONCE_INIT;
static void *g_ohos_ime_lib;
static int g_ohos_ime_ready;
static pthread_mutex_t g_ime_session_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t g_ime_state_lock = PTHREAD_MUTEX_INITIALIZER;
static InputMethod_TextEditorProxy *g_ime_proxy;
static InputMethod_InputMethodProxy *g_ime_im;
static char16_t *g_ime_preview_units;
static size_t g_ime_preview_length;
static char *g_ime_preview_text;
static int g_ime_preview_active;
static int g_ime_x, g_ime_y;
static double g_ime_screen_x, g_ime_screen_y, g_ime_surface_scale;
static int g_ime_geometry_valid;
/* Callback identity is enabled only during a live attach attempt/session. */
static InputMethod_TextEditorProxy *g_ime_callback_proxy;
typedef struct ohos_ime_operation {
    struct ohos_ime_operation *next;
    uint64_t after_event;
    size_t length, index;
    int kind, code; /* kind=0 decodes text; otherwise repeat this event. */
    char16_t text[];
} ohos_ime_operation;
static ohos_ime_operation *g_ime_operation_head, *g_ime_operation_tail;

static int ohos_ime_callback_begin(InputMethod_TextEditorProxy *proxy) {
    pthread_mutex_lock(&g_ime_state_lock);
    if (proxy && proxy == g_ime_callback_proxy) return 1;
    pthread_mutex_unlock(&g_ime_state_lock);
    return 0;
}

static void ohos_ime_load(void) {
    g_ohos_ime_lib = dlopen("libohinputmethod.so", RTLD_NOW | RTLD_LOCAL);
    if (!g_ohos_ime_lib) return;
#define OHOS_IME_LOAD(name) \
    g_ohos_ime.name = (__typeof__(&name))dlsym(g_ohos_ime_lib, #name); \
    if (!g_ohos_ime.name) goto fail;
    OHOS_IME_FUNCTIONS(OHOS_IME_LOAD)
#undef OHOS_IME_LOAD
    g_ohos_ime_ready = 1;
    return;
fail:
    memset(&g_ohos_ime, 0, sizeof(g_ohos_ime));
    dlclose(g_ohos_ime_lib);
    g_ohos_ime_lib = NULL;
}
#undef OHOS_IME_FUNCTIONS

/* Replace malformed UTF-16 with U+FFFD instead of emitting surrogate values. */
static int ime_utf16_next(const char16_t *s, size_t n, size_t *i) {
    if (*i >= n) return 0;
    unsigned int u = s[(*i)++];
    if (u >= 0xD800 && u <= 0xDBFF && *i < n) {
        unsigned int lo = s[*i];
        if (lo >= 0xDC00 && lo <= 0xDFFF) {
            ++*i;
            return (int)(0x10000u + ((u - 0xD800u) << 10) + lo - 0xDC00u);
        }
    }
    return u >= 0xD800 && u <= 0xDFFF ? 0xFFFD : (int)u;
}

static char *ohos_ime_utf8(const char16_t *text, size_t n) {
    if (n > (SIZE_MAX - 1) / 3) return NULL;
    char *out = (char *)malloc(n * 3 + 1);
    if (!out) return NULL;
    size_t i = 0, o = 0;
    while (i < n) {
        unsigned int c = (unsigned int)ime_utf16_next(text, n, &i);
        if (!c) c = 0xFFFD; /* The public text ABI cannot contain embedded NUL. */
        if (c < 0x80) out[o++] = (char)c;
        else if (c < 0x800) {
            out[o++] = (char)(0xC0 | (c >> 6));
            out[o++] = (char)(0x80 | (c & 63));
        } else if (c < 0x10000) {
            out[o++] = (char)(0xE0 | (c >> 12));
            out[o++] = (char)(0x80 | ((c >> 6) & 63));
            out[o++] = (char)(0x80 | (c & 63));
        } else {
            out[o++] = (char)(0xF0 | (c >> 18));
            out[o++] = (char)(0x80 | ((c >> 12) & 63));
            out[o++] = (char)(0x80 | ((c >> 6) & 63));
            out[o++] = (char)(0x80 | (c & 63));
        }
    }
    out[o] = 0;
    return out;
}

static void ohos_ime_clear_preview_locked(void) {
    free(g_ime_preview_units);
    free(g_ime_preview_text);
    g_ime_preview_units = NULL;
    g_ime_preview_text = NULL;
    g_ime_preview_length = 0;
    g_ime_preview_active = 0;
}

static void ohos_ime_changed(void) {
    pthread_mutex_lock(&g_oq_lock);
    oq_push_locked(0, 0, 0, 0, 0, 0);
    pthread_mutex_unlock(&g_oq_lock);
}

static int ohos_ime_utf16_boundary(size_t i) {
    return i == 0 || i == g_ime_preview_length ||
        !(g_ime_preview_units[i - 1] >= 0xD800 && g_ime_preview_units[i - 1] <= 0xDBFF &&
          g_ime_preview_units[i] >= 0xDC00 && g_ime_preview_units[i] <= 0xDFFF);
}

static int32_t ime_on_set_preview_text(InputMethod_TextEditorProxy *proxy,
        const char16_t text[], size_t length, int32_t start, int32_t end) {
    if (!text && length) return IME_ERR_PARAMCHECK;
    if (!ohos_ime_callback_begin(proxy)) return IME_ERR_DETACHED;
    size_t from = 0, to = g_ime_preview_length;
    if (start != -1 || end != -1) {
        if (start < 0 || end < start || (size_t)end > g_ime_preview_length ||
            !ohos_ime_utf16_boundary((size_t)start) || !ohos_ime_utf16_boundary((size_t)end)) {
            pthread_mutex_unlock(&g_ime_state_lock);
            return IME_ERR_PARAMCHECK;
        }
        from = (size_t)start;
        to = (size_t)end;
    }
    size_t kept = g_ime_preview_length - (to - from);
    if (length > SIZE_MAX / sizeof(char16_t) - kept - 1) {
        pthread_mutex_unlock(&g_ime_state_lock);
        return IME_ERR_PARAMCHECK;
    }
    size_t total = kept + length;
    char16_t *units = (char16_t *)malloc((total + 1) * sizeof(char16_t));
    if (!units) {
        pthread_mutex_unlock(&g_ime_state_lock);
        return IME_ERR_UNDEFINED;
    }
    if (from) memcpy(units, g_ime_preview_units, from * sizeof(char16_t));
    if (length) memcpy(units + from, text, length * sizeof(char16_t));
    size_t tail = g_ime_preview_length - to;
    if (tail) memcpy(units + from + length, g_ime_preview_units + to, tail * sizeof(char16_t));
    units[total] = 0;
    char *utf8 = ohos_ime_utf8(units, total);
    if (!utf8) {
        free(units);
        pthread_mutex_unlock(&g_ime_state_lock);
        return IME_ERR_UNDEFINED;
    }
    free(g_ime_preview_units);
    free(g_ime_preview_text);
    g_ime_preview_units = units;
    g_ime_preview_length = total;
    g_ime_preview_text = utf8;
    g_ime_preview_active = 1; /* An empty update is not a finish callback. */
    pthread_mutex_unlock(&g_ime_state_lock);
    ohos_ime_changed();
    return IME_ERR_OK;
}

static pthread_once_t g_ime_text_key_once = PTHREAD_ONCE_INIT;
static pthread_key_t g_ime_text_key;
static int g_ime_text_key_ready;
static void ohos_ime_text_key_init(void) {
    g_ime_text_key_ready = pthread_key_create(&g_ime_text_key, free) == 0;
}

/* Snapshot valid until the next call on this thread, including across commit. */
EXPORT const char *zan_gui_ime_composing(void) {
    if (pthread_once(&g_ime_text_key_once, ohos_ime_text_key_init) != 0 ||
        !g_ime_text_key_ready) return "";
    void *old = pthread_getspecific(g_ime_text_key);
    if (pthread_setspecific(g_ime_text_key, NULL) != 0) return "";
    free(old);
    pthread_mutex_lock(&g_ime_state_lock);
    const char *text = g_ime_preview_text ? g_ime_preview_text : "";
    size_t n = strlen(text);
    char *copy = (char *)malloc(n + 1);
    if (copy) memcpy(copy, text, n + 1);
    pthread_mutex_unlock(&g_ime_state_lock);
    if (!copy) return "";
    if (pthread_setspecific(g_ime_text_key, copy) != 0) {
        free(copy);
        return "";
    }
    return copy;
}

/* With the state lock held, append an atomic operation after existing events. */
static void ohos_ime_enqueue_operation(ohos_ime_operation *operation) {
    pthread_mutex_lock(&g_oq_lock);
    operation->after_event = g_oq_written;
    g_oq_no_coalesce = 1;
    if (g_ime_operation_tail) g_ime_operation_tail->next = operation;
    else g_ime_operation_head = operation;
    g_ime_operation_tail = operation;
    pthread_mutex_unlock(&g_oq_lock);
}

static void ime_on_insert(InputMethod_TextEditorProxy *proxy, const char16_t *text, size_t length) {
    if ((!text && length) || !ohos_ime_callback_begin(proxy)) return;
    if (length > (SIZE_MAX - sizeof(ohos_ime_operation)) / sizeof(char16_t)) {
        pthread_mutex_unlock(&g_ime_state_lock);
        return;
    }
    ohos_ime_operation *commit = NULL;
    if (length) {
        commit = (ohos_ime_operation *)malloc(sizeof(*commit) + length * sizeof(char16_t));
        if (!commit) {
            pthread_mutex_unlock(&g_ime_state_lock);
            /* This SDK callback returns void; allocation failure cannot be
             * reported. Keep the preview rather than falsely completing it. */
            return;
        }
        commit->next = NULL;
        commit->length = length;
        commit->index = 0;
        commit->kind = commit->code = 0;
        memcpy(commit->text, text, length * sizeof(char16_t));
    }
    ohos_ime_clear_preview_locked();
    if (commit) ohos_ime_enqueue_operation(commit);
    pthread_mutex_unlock(&g_ime_state_lock);
}

/* Called by the production oq_pop with its lock held. Deletes and text share
 * one FIFO/barrier so neither ring capacity nor input coalescing truncates them. */
static int ohos_ime_pop_operation_locked(int *kind, int *code) {
    while (g_ime_operation_head && g_oq_read >= g_ime_operation_head->after_event) {
        ohos_ime_operation *operation = g_ime_operation_head;
        while (operation->index < operation->length) {
            if (operation->kind) {
                ++operation->index;
                *kind = operation->kind;
                *code = operation->code;
                return 1;
            }
            int cp = ime_utf16_next(operation->text, operation->length, &operation->index);
            if (cp) { *kind = 6; *code = cp == '\n' ? 13 : cp; return 1; }
        }
        g_ime_operation_head = operation->next;
        if (!g_ime_operation_head) g_ime_operation_tail = NULL;
        free(operation);
    }
    return 0;
}

static int ohos_ime_pending_locked(void) { return g_ime_operation_head != NULL; }

static void ime_on_finish_preview(InputMethod_TextEditorProxy *proxy) {
    if (!ohos_ime_callback_begin(proxy)) return;
    ohos_ime_clear_preview_locked();
    pthread_mutex_unlock(&g_ime_state_lock);
    ohos_ime_changed();
}

static void ohos_ime_repeat(InputMethod_TextEditorProxy *proxy, int32_t length, int kind, int code) {
    if (length <= 0 || !ohos_ime_callback_begin(proxy)) return;
    ohos_ime_operation *operation = (ohos_ime_operation *)malloc(sizeof(*operation));
    if (operation) {
        operation->next = NULL;
        operation->length = (size_t)length;
        operation->index = 0;
        operation->kind = kind;
        operation->code = code;
        ohos_ime_enqueue_operation(operation);
    }
    /* The SDK callback is void: allocation failure has no status channel. */
    pthread_mutex_unlock(&g_ime_state_lock);
}

static void ime_on_delete_backward(InputMethod_TextEditorProxy *proxy, int32_t length) {
    ohos_ime_repeat(proxy, length, 6, 8);
}

static void ime_on_delete_forward(InputMethod_TextEditorProxy *proxy, int32_t length) {
    ohos_ime_repeat(proxy, length, 4, 46);
}

static void ime_on_enter_key(InputMethod_TextEditorProxy *proxy, InputMethod_EnterKeyType key) {
    (void)key;
    ohos_ime_repeat(proxy, 1, 6, 13);
}

static void ime_on_move_cursor(InputMethod_TextEditorProxy *proxy, InputMethod_Direction dir) {
    int vk;
    switch (dir) {
    case IME_DIRECTION_LEFT: vk = 37; break;
    case IME_DIRECTION_UP: vk = 38; break;
    case IME_DIRECTION_RIGHT: vk = 39; break;
    case IME_DIRECTION_DOWN: vk = 40; break;
    default: return;
    }
    ohos_ime_repeat(proxy, 1, 4, vk);
}

/* Full editor context is not supplied by the current GUI ABI. */
static void ime_on_get_left_text(InputMethod_TextEditorProxy *proxy,
        int32_t number, char16_t text[], size_t *length) {
    (void)proxy; (void)number; (void)text;
    if (length) *length = 0;
}
static void ime_on_get_right_text(InputMethod_TextEditorProxy *proxy,
        int32_t number, char16_t text[], size_t *length) {
    ime_on_get_left_text(proxy, number, text, length);
}
static int32_t ime_on_get_index_at_cursor(InputMethod_TextEditorProxy *proxy) {
    (void)proxy; return 0;
}
static int32_t ime_on_private_command(InputMethod_TextEditorProxy *proxy,
        InputMethod_PrivateCommand *commands[], size_t size) {
    (void)proxy; (void)commands; (void)size; return IME_ERR_OK;
}
static void ime_on_keyboard_status(InputMethod_TextEditorProxy *proxy, InputMethod_KeyboardStatus status) {
    (void)proxy; (void)status;
}
static void ime_on_set_selection(InputMethod_TextEditorProxy *proxy, int32_t start, int32_t end) {
    (void)proxy; (void)start; (void)end;
}
static void ime_on_extend_action(InputMethod_TextEditorProxy *proxy, InputMethod_ExtendAction action) {
    (void)proxy; (void)action;
}

/* All values here are physical screen pixels. A missing origin stays unknown. */
static int ohos_ime_cursor_rect(double rect[4]) {
    pthread_mutex_lock(&g_ime_state_lock);
    int valid = g_ime_geometry_valid;
    rect[0] = g_ime_screen_x + (double)g_ime_x * g_ime_surface_scale;
    rect[1] = g_ime_screen_y + (double)g_ime_y * g_ime_surface_scale;
    rect[2] = rect[3] = g_ime_surface_scale;
    pthread_mutex_unlock(&g_ime_state_lock);
    return valid && isfinite(rect[0]) && isfinite(rect[1]);
}

static void ime_on_get_text_config(InputMethod_TextEditorProxy *proxy, InputMethod_TextConfig *config) {
    (void)proxy;
    g_ohos_ime.OH_TextConfig_SetInputType(config, IME_TEXT_INPUT_TYPE_TEXT);
    g_ohos_ime.OH_TextConfig_SetEnterKeyType(config, IME_ENTER_KEY_UNSPECIFIED);
    g_ohos_ime.OH_TextConfig_SetPreviewTextSupport(config, true);
    double r[4];
    InputMethod_CursorInfo *cursor = NULL;
    if (ohos_ime_cursor_rect(r) &&
        g_ohos_ime.OH_TextConfig_GetCursorInfo(config, &cursor) == IME_ERR_OK && cursor)
        g_ohos_ime.OH_CursorInfo_SetRect(cursor, r[0], r[1], r[2], r[3]);
    /* config and its cursor belong to the callback: never retain/destroy them. */
}

static void ohos_ime_notify_cursor_locked(void) {
    double r[4];
    if (!g_ime_im || !ohos_ime_cursor_rect(r)) return;
    InputMethod_CursorInfo *cursor = g_ohos_ime.OH_CursorInfo_Create(r[0], r[1], r[2], r[3]);
    if (!cursor) return;
    g_ohos_ime.OH_InputMethodProxy_NotifyCursorUpdate(g_ime_im, cursor);
    g_ohos_ime.OH_CursorInfo_Destroy(cursor);
}

static void ohos_ime_invalidate_geometry(void) {
    pthread_mutex_lock(&g_ime_state_lock);
    g_ime_geometry_valid = 0;
    pthread_mutex_unlock(&g_ime_state_lock);
}

/* Host bridge, call AFTER every attach and whenever screen/window/layout moves:
 *   zan_gui_ohos_set_geometry(surfaceScreenLeftPx, surfaceScreenTopPx,
 *                             physicalScreenPixelsPerSurfacePixel);
 * The origin is the drawing surface's absolute screen origin, including window
 * position and content offsets. Convert ArkUI vp values to physical px first;
 * GetXComponentOffset alone is relative to its parent and is insufficient.
 * set_ime_pos remains in drawable pixels; an ordinary untransformed surface has
 * scale=1. For unavailable geometry pass scale=0 (invalidates, returns -1).
 * No absolute cursor position is synthesized until this bridge reports one.
 */
EXPORT i32 zan_gui_ohos_set_geometry(double left, double top, double scale) {
    int valid = isfinite(left) && isfinite(top) && isfinite(scale) && scale > 0;
    pthread_mutex_lock(&g_ime_state_lock);
    g_ime_geometry_valid = valid;
    if (valid) {
        g_ime_screen_x = left;
        g_ime_screen_y = top;
        g_ime_surface_scale = scale;
    }
    pthread_mutex_unlock(&g_ime_state_lock);
    if (!valid) return -1;
    pthread_mutex_lock(&g_ime_session_lock);
    ohos_ime_notify_cursor_locked();
    pthread_mutex_unlock(&g_ime_session_lock);
    return 0;
}

EXPORT void zan_gui_set_ime_pos(i32 x, i32 y) {
    pthread_mutex_lock(&g_ime_state_lock);
    g_ime_x = x; g_ime_y = y;
    pthread_mutex_unlock(&g_ime_state_lock);
    pthread_mutex_lock(&g_ime_session_lock);
    ohos_ime_notify_cursor_locked();
    pthread_mutex_unlock(&g_ime_session_lock);
}

static int ohos_ime_ensure_proxy(void) {
    if (g_ime_proxy) return 1;
    InputMethod_TextEditorProxy *p = g_ohos_ime.OH_TextEditorProxy_Create();
    if (!p) return 0;
#define OHOS_IME_SET(name, callback) \
    if (g_ohos_ime.OH_TextEditorProxy_Set##name##Func(p, callback) != IME_ERR_OK) goto fail;
    OHOS_IME_SET(GetTextConfig, ime_on_get_text_config)
    OHOS_IME_SET(InsertText, ime_on_insert)
    OHOS_IME_SET(DeleteForward, ime_on_delete_forward)
    OHOS_IME_SET(DeleteBackward, ime_on_delete_backward)
    OHOS_IME_SET(SendEnterKey, ime_on_enter_key)
    OHOS_IME_SET(MoveCursor, ime_on_move_cursor)
    OHOS_IME_SET(SendKeyboardStatus, ime_on_keyboard_status)
    OHOS_IME_SET(HandleSetSelection, ime_on_set_selection)
    OHOS_IME_SET(HandleExtendAction, ime_on_extend_action)
    OHOS_IME_SET(GetLeftTextOfCursor, ime_on_get_left_text)
    OHOS_IME_SET(GetRightTextOfCursor, ime_on_get_right_text)
    OHOS_IME_SET(GetTextIndexAtCursor, ime_on_get_index_at_cursor)
    OHOS_IME_SET(ReceivePrivateCommand, ime_on_private_command)
    OHOS_IME_SET(SetPreviewText, ime_on_set_preview_text)
    OHOS_IME_SET(FinishTextPreview, ime_on_finish_preview)
#undef OHOS_IME_SET
    g_ime_proxy = p;
    return 1;
fail:
    g_ohos_ime.OH_TextEditorProxy_Destroy(p);
    return 0;
}

/* Existing GUI focus ABI: no attach is attempted when native symbols are absent. */
EXPORT void zan_gui_set_ime_open(i32 on) {
    if (pthread_once(&g_ohos_ime_once, ohos_ime_load) != 0 || !g_ohos_ime_ready) return;
    pthread_mutex_lock(&g_ime_session_lock);
    if (on && !g_ime_im && ohos_ime_ensure_proxy()) {
        InputMethod_AttachOptions *opts = g_ohos_ime.OH_AttachOptions_Create(true);
        if (opts) {
            InputMethod_InputMethodProxy *im = NULL;
            pthread_mutex_lock(&g_ime_state_lock);
            g_ime_callback_proxy = g_ime_proxy;
            pthread_mutex_unlock(&g_ime_state_lock);
            InputMethod_ErrorCode ec = g_ohos_ime.OH_InputMethodController_Attach(g_ime_proxy, opts, &im);
            g_ohos_ime.OH_AttachOptions_Destroy(opts);
            if (ec == IME_ERR_OK && im) {
                g_ime_im = im;
                ohos_ime_notify_cursor_locked();
            } else {
                pthread_mutex_lock(&g_ime_state_lock);
                g_ime_callback_proxy = NULL;
                ohos_ime_clear_preview_locked();
                pthread_mutex_unlock(&g_ime_state_lock);
            }
        }
    } else if (!on) {
        pthread_mutex_lock(&g_ime_state_lock);
        g_ime_callback_proxy = NULL;
        ohos_ime_clear_preview_locked();
        pthread_mutex_unlock(&g_ime_state_lock);
        if (g_ime_im) g_ohos_ime.OH_InputMethodController_Detach(g_ime_im);
        g_ime_im = NULL;
        /* Do not reattach a proxy from a closed session. Native Detach/Destroy
         * owns callback quiescence; the gate is disabled before either call. */
        if (g_ime_proxy) g_ohos_ime.OH_TextEditorProxy_Destroy(g_ime_proxy);
        g_ime_proxy = NULL;
        ohos_ime_changed();
    }
    pthread_mutex_unlock(&g_ime_session_lock);
}

void zan_gui_ohos_ime_shutdown(void) {
    pthread_mutex_lock(&g_ime_session_lock);
    pthread_mutex_lock(&g_ime_state_lock);
    g_ime_callback_proxy = NULL;
    ohos_ime_clear_preview_locked();
    g_ime_geometry_valid = 0;
    pthread_mutex_unlock(&g_ime_state_lock);
    if (g_ime_im) g_ohos_ime.OH_InputMethodController_Detach(g_ime_im);
    g_ime_im = NULL;
    if (g_ime_proxy) g_ohos_ime.OH_TextEditorProxy_Destroy(g_ime_proxy);
    g_ime_proxy = NULL;
    pthread_mutex_lock(&g_oq_lock);
    while (g_ime_operation_head) {
        ohos_ime_operation *next = g_ime_operation_head->next;
        free(g_ime_operation_head);
        g_ime_operation_head = next;
    }
    g_ime_operation_tail = NULL;
    pthread_mutex_unlock(&g_oq_lock);
    pthread_mutex_unlock(&g_ime_session_lock);
}
