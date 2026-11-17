/* Production OHOS clipboard/IME under injected OS failures. This does not
 * emulate system permissions or replace the SDK/link and on-device checks.
 * -DZAN_GUI_OHOS_SDK_CHECK compiles the same fragments against real SDK types
 * and emits strong API references solely to verify their link availability. */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <uchar.h>
#include <math.h>

typedef int32_t i32;
typedef int64_t i64;
typedef intptr_t iptr;
#define EXPORT
#define CHECK(c, msg) do { if (!(c)) { \
    fprintf(stderr, "FAIL line %d: %s\n", __LINE__, msg); exit(1); \
} } while (0)

#ifdef ZAN_GUI_OHOS_SDK_CHECK
#include <pthread.h>
#include <dlfcn.h>
#else
#ifdef _WIN32
#include <windows.h>
typedef SRWLOCK pthread_mutex_t;
#define PTHREAD_MUTEX_INITIALIZER SRWLOCK_INIT
static int pthread_mutex_lock(pthread_mutex_t *m) { AcquireSRWLockExclusive(m); return 0; }
static int pthread_mutex_unlock(pthread_mutex_t *m) { ReleaseSRWLockExclusive(m); return 0; }
typedef INIT_ONCE pthread_once_t;
#define PTHREAD_ONCE_INIT INIT_ONCE_STATIC_INIT
struct once_call { void (*fn)(void); };
static BOOL CALLBACK call_once(PINIT_ONCE once, PVOID arg, PVOID *context) {
    (void)once; (void)context;
    ((struct once_call *)arg)->fn();
    return TRUE;
}
static int pthread_once(pthread_once_t *once, void (*fn)(void)) {
    struct once_call arg = { fn };
    return InitOnceExecuteOnce(once, call_once, &arg, NULL) ? 0 : -1;
}
typedef DWORD pthread_key_t;
static int pthread_key_create(pthread_key_t *key, void (*destroy)(void *)) {
    *key = FlsAlloc((PFLS_CALLBACK_FUNCTION)destroy);
    return *key == FLS_OUT_OF_INDEXES ? -1 : 0;
}
static void *pthread_getspecific(pthread_key_t key) { return FlsGetValue(key); }
static int pthread_setspecific(pthread_key_t key, const void *value) {
    return FlsSetValue(key, (void *)value) ? 0 : -1;
}
#else
#include <pthread.h>
#endif

/* Deliberately small host boundary declarations. The SDK_CHECK branch above
 * uses only the installed official declarations, never this host ABI model. */
typedef struct OH_Pasteboard { int unused; } OH_Pasteboard;
typedef struct OH_UdsPlainText { char *text; } OH_UdsPlainText;
typedef struct OH_UdmfRecord { char *text; int borrowed; } OH_UdmfRecord;
typedef struct OH_UdmfData { OH_UdmfRecord *records[2]; unsigned int count; } OH_UdmfData;
enum { ERR_OK = 0, UDMF_E_OK = 0 };

typedef int InputMethod_ErrorCode;
typedef int InputMethod_TextInputType;
typedef int InputMethod_EnterKeyType;
typedef int InputMethod_Direction;
typedef int InputMethod_KeyboardStatus;
typedef int InputMethod_ExtendAction;
enum { IME_ERR_OK = 0, IME_ERR_UNDEFINED = 1, IME_ERR_PARAMCHECK = 401, IME_ERR_DETACHED = 12800009,
       IME_TEXT_INPUT_TYPE_TEXT = 0, IME_ENTER_KEY_UNSPECIFIED = 0,
       IME_DIRECTION_UP = 1, IME_DIRECTION_DOWN = 2,
       IME_DIRECTION_LEFT = 3, IME_DIRECTION_RIGHT = 4 };
typedef struct InputMethod_TextEditorProxy InputMethod_TextEditorProxy;
typedef struct InputMethod_PrivateCommand { int unused; } InputMethod_PrivateCommand;
typedef struct InputMethod_CursorInfo { double rect[4]; int borrowed; } InputMethod_CursorInfo;
typedef struct InputMethod_TextConfig { InputMethod_CursorInfo cursor; bool preview; } InputMethod_TextConfig;
typedef struct InputMethod_InputMethodProxy { int unused; } InputMethod_InputMethodProxy;
typedef struct InputMethod_AttachOptions { bool show; } InputMethod_AttachOptions;
typedef void (*OH_TextEditorProxy_GetTextConfigFunc)(InputMethod_TextEditorProxy *, InputMethod_TextConfig *);
typedef void (*OH_TextEditorProxy_InsertTextFunc)(InputMethod_TextEditorProxy *, const char16_t *, size_t);
typedef void (*OH_TextEditorProxy_DeleteForwardFunc)(InputMethod_TextEditorProxy *, int32_t);
typedef void (*OH_TextEditorProxy_DeleteBackwardFunc)(InputMethod_TextEditorProxy *, int32_t);
typedef void (*OH_TextEditorProxy_SendEnterKeyFunc)(InputMethod_TextEditorProxy *, InputMethod_EnterKeyType);
typedef void (*OH_TextEditorProxy_MoveCursorFunc)(InputMethod_TextEditorProxy *, InputMethod_Direction);
typedef void (*OH_TextEditorProxy_SendKeyboardStatusFunc)(InputMethod_TextEditorProxy *, InputMethod_KeyboardStatus);
typedef void (*OH_TextEditorProxy_HandleSetSelectionFunc)(InputMethod_TextEditorProxy *, int32_t, int32_t);
typedef void (*OH_TextEditorProxy_HandleExtendActionFunc)(InputMethod_TextEditorProxy *, InputMethod_ExtendAction);
typedef void (*OH_TextEditorProxy_GetLeftTextOfCursorFunc)(InputMethod_TextEditorProxy *, int32_t, char16_t [], size_t *);
typedef void (*OH_TextEditorProxy_GetRightTextOfCursorFunc)(InputMethod_TextEditorProxy *, int32_t, char16_t [], size_t *);
typedef int32_t (*OH_TextEditorProxy_GetTextIndexAtCursorFunc)(InputMethod_TextEditorProxy *);
typedef int32_t (*OH_TextEditorProxy_ReceivePrivateCommandFunc)(InputMethod_TextEditorProxy *, InputMethod_PrivateCommand *[], size_t);
typedef int32_t (*OH_TextEditorProxy_SetPreviewTextFunc)(InputMethod_TextEditorProxy *, const char16_t [], size_t, int32_t, int32_t);
typedef void (*OH_TextEditorProxy_FinishTextPreviewFunc)(InputMethod_TextEditorProxy *);
#define TEST_IME_SETTERS(X) \
    X(GetTextConfig, cfg) X(InsertText, insert) X(DeleteForward, delfwd) \
    X(DeleteBackward, delback) X(SendEnterKey, enter) X(MoveCursor, move) \
    X(SendKeyboardStatus, status) X(HandleSetSelection, selection) \
    X(HandleExtendAction, action) X(GetLeftTextOfCursor, left) \
    X(GetRightTextOfCursor, right) X(GetTextIndexAtCursor, index) \
    X(ReceivePrivateCommand, command) X(SetPreviewText, preview) X(FinishTextPreview, finish)
#define TEST_IME_MEMBER(name, field) OH_TextEditorProxy_##name##Func field;
struct InputMethod_TextEditorProxy { TEST_IME_SETTERS(TEST_IME_MEMBER) };
#undef TEST_IME_MEMBER

static const char *fail_api, *missing_symbol, *missing_library;
static int native_live, live_buffers, fail_alloc_after = -1;
static int library_closes, get_calls, set_calls, preview_supported;
static int create_proxy_calls, notify_calls, attach_calls;
/* Keep retired native fixture addresses until test teardown so late callbacks
 * can be injected deterministically without dereferencing freed storage. */
static InputMethod_TextEditorProxy *retired_proxies[32];
static size_t retired_proxy_count;
static int no_records, nontext_first, data_with_error;
static char *system_text;
static double last_cursor[4];
static InputMethod_TextEditorProxy *last_proxy;
static int failing(const char *name) { return fail_api && strcmp(fail_api, name) == 0; }
static void *native_alloc(size_t n, const char *name) {
    if (failing(name)) return NULL;
    void *p = calloc(1, n);
    CHECK(p, "allocate injected native object");
    ++native_live;
    return p;
}
static void native_release(void *p) { if (p) { --native_live; free(p); } }
static char *copy_text(const char *text) {
    if (!text) return NULL;
    size_t n = strlen(text) + 1;
    char *p = (char *)malloc(n);
    CHECK(p, "allocate native text");
    memcpy(p, text, n);
    return p;
}
static void *test_malloc(size_t n) {
    if (fail_alloc_after == 0) { fail_alloc_after = -1; return NULL; }
    if (fail_alloc_after > 0) --fail_alloc_after;
    void *p = malloc(n);
    if (p) ++live_buffers;
    return p;
}
static void test_free(void *p) { if (p) { --live_buffers; free(p); } }

static OH_Pasteboard *OH_Pasteboard_Create(void) {
    return (OH_Pasteboard *)native_alloc(sizeof(OH_Pasteboard), "OH_Pasteboard_Create");
}
static void OH_Pasteboard_Destroy(OH_Pasteboard *board) { native_release(board); }
static OH_UdmfData *OH_UdmfData_Create(void) {
    return (OH_UdmfData *)native_alloc(sizeof(OH_UdmfData), "OH_UdmfData_Create");
}
static OH_UdmfRecord *OH_UdmfRecord_Create(void) {
    return (OH_UdmfRecord *)native_alloc(sizeof(OH_UdmfRecord), "OH_UdmfRecord_Create");
}
static void record_release(OH_UdmfRecord *record) {
    if (record) { free(record->text); native_release(record); }
}
static void OH_UdmfRecord_Destroy(OH_UdmfRecord *record) {
    CHECK(!record || !record->borrowed, "GetRecords entries are owned by data");
    record_release(record);
}
static void OH_UdmfData_Destroy(OH_UdmfData *data) {
    if (!data) return;
    for (unsigned int i = 0; i < data->count; ++i) record_release(data->records[i]);
    native_release(data);
}
static OH_UdsPlainText *OH_UdsPlainText_Create(void) {
    return (OH_UdsPlainText *)native_alloc(sizeof(OH_UdsPlainText), "OH_UdsPlainText_Create");
}
static void OH_UdsPlainText_Destroy(OH_UdsPlainText *plain) {
    if (plain) { free(plain->text); native_release(plain); }
}
static int OH_UdsPlainText_SetContent(OH_UdsPlainText *plain, const char *text) {
    if (failing("OH_UdsPlainText_SetContent")) return 401;
    free(plain->text); plain->text = copy_text(text); return 0;
}
static const char *OH_UdsPlainText_GetContent(OH_UdsPlainText *plain) {
    return failing("OH_UdsPlainText_GetContent") ? NULL : plain->text;
}
static int OH_UdmfRecord_AddPlainText(OH_UdmfRecord *record, OH_UdsPlainText *plain) {
    if (failing("OH_UdmfRecord_AddPlainText")) return 401;
    free(record->text); record->text = copy_text(plain->text); return 0;
}
static int OH_UdmfData_AddRecord(OH_UdmfData *data, OH_UdmfRecord *record) {
    if (failing("OH_UdmfData_AddRecord")) return 401;
    OH_UdmfRecord *copy = (OH_UdmfRecord *)native_alloc(sizeof(*copy), "retained record");
    copy->text = copy_text(record->text); copy->borrowed = 1;
    data->records[data->count++] = copy; return 0;
}
static int OH_Pasteboard_SetData(OH_Pasteboard *board, OH_UdmfData *data) {
    (void)board; ++set_calls;
    if (failing("OH_Pasteboard_SetData")) return 12900003;
    free(system_text); system_text = copy_text(data->records[0]->text); return 0;
}
static OH_UdmfData *OH_Pasteboard_GetData(OH_Pasteboard *board, int *status) {
    (void)board; ++get_calls;
    if (failing("OH_Pasteboard_GetData")) { *status = 201; return NULL; }
    OH_UdmfData *data = OH_UdmfData_Create();
    *status = data_with_error ? 12900000 : 0;
    if (!data || !system_text || no_records) return data;
    if (nontext_first) {
        OH_UdmfRecord *other = OH_UdmfRecord_Create();
        if (!other) { OH_UdmfData_Destroy(data); return NULL; }
        other->borrowed = 1;
        data->records[data->count++] = other;
    }
    OH_UdmfRecord *record = OH_UdmfRecord_Create();
    if (!record) { OH_UdmfData_Destroy(data); return NULL; }
    record->text = copy_text(system_text); record->borrowed = 1;
    data->records[data->count++] = record; return data;
}
static OH_UdmfRecord **OH_UdmfData_GetRecords(OH_UdmfData *data, unsigned int *count) {
    *count = data->count;
    return failing("OH_UdmfData_GetRecords") ? NULL : data->records;
}
static int OH_UdmfRecord_GetPlainText(OH_UdmfRecord *record, OH_UdsPlainText *plain) {
    if (!record->text || failing("OH_UdmfRecord_GetPlainText")) return 1;
    free(plain->text); plain->text = copy_text(record->text); return 0;
}

static InputMethod_TextEditorProxy *OH_TextEditorProxy_Create(void) {
    ++create_proxy_calls;
    last_proxy = (InputMethod_TextEditorProxy *)native_alloc(sizeof(*last_proxy), "OH_TextEditorProxy_Create");
    return last_proxy;
}
static void OH_TextEditorProxy_Destroy(InputMethod_TextEditorProxy *proxy) {
    if (last_proxy == proxy) last_proxy = NULL;
    CHECK(retired_proxy_count < sizeof(retired_proxies) / sizeof(*retired_proxies), "retired fixture capacity");
    retired_proxies[retired_proxy_count++] = proxy;
    --native_live;
}
#define TEST_IME_SETTER(name, field) \
    static InputMethod_ErrorCode OH_TextEditorProxy_Set##name##Func( \
            InputMethod_TextEditorProxy *p, OH_TextEditorProxy_##name##Func fn) { \
        if (failing("OH_TextEditorProxy_Set" #name "Func")) return 401; \
        p->field = fn; return 0; \
    }
TEST_IME_SETTERS(TEST_IME_SETTER)
#undef TEST_IME_SETTER
static InputMethod_AttachOptions *OH_AttachOptions_Create(bool show) {
    InputMethod_AttachOptions *o = (InputMethod_AttachOptions *)native_alloc(sizeof(*o), "OH_AttachOptions_Create");
    if (o) o->show = show;
    return o;
}
static void OH_AttachOptions_Destroy(InputMethod_AttachOptions *o) { native_release(o); }
static InputMethod_ErrorCode OH_TextConfig_SetInputType(InputMethod_TextConfig *c, InputMethod_TextInputType type) {
    (void)c; CHECK(type == IME_TEXT_INPUT_TYPE_TEXT, "text input type"); return 0;
}
static InputMethod_ErrorCode OH_TextConfig_SetEnterKeyType(InputMethod_TextConfig *c, InputMethod_EnterKeyType type) {
    (void)c; CHECK(type == IME_ENTER_KEY_UNSPECIFIED, "enter type"); return 0;
}
static InputMethod_ErrorCode OH_TextConfig_SetPreviewTextSupport(InputMethod_TextConfig *c, bool supported) {
    c->preview = supported; preview_supported = supported; return 0;
}
static InputMethod_ErrorCode OH_TextConfig_GetCursorInfo(InputMethod_TextConfig *c, InputMethod_CursorInfo **cursor) {
    if (failing("OH_TextConfig_GetCursorInfo")) return 1;
    *cursor = &c->cursor; return 0;
}
static InputMethod_ErrorCode OH_CursorInfo_SetRect(InputMethod_CursorInfo *c,
        double left, double top, double width, double height) {
    c->rect[0] = left; c->rect[1] = top; c->rect[2] = width; c->rect[3] = height; return 0;
}
static InputMethod_CursorInfo *OH_CursorInfo_Create(double left, double top, double width, double height) {
    InputMethod_CursorInfo *c = (InputMethod_CursorInfo *)native_alloc(sizeof(*c), "OH_CursorInfo_Create");
    if (c) OH_CursorInfo_SetRect(c, left, top, width, height);
    return c;
}
static void OH_CursorInfo_Destroy(InputMethod_CursorInfo *c) {
    CHECK(!c->borrowed, "config cursor must never be destroyed"); native_release(c);
}
static InputMethod_ErrorCode OH_InputMethodProxy_NotifyCursorUpdate(
        InputMethod_InputMethodProxy *proxy, InputMethod_CursorInfo *cursor) {
    CHECK(proxy, "notify attached proxy");
    if (failing("OH_InputMethodProxy_NotifyCursorUpdate")) return 1;
    memcpy(last_cursor, cursor->rect, sizeof(last_cursor)); ++notify_calls; return 0;
}
static InputMethod_ErrorCode OH_InputMethodController_Attach(InputMethod_TextEditorProxy *p,
        InputMethod_AttachOptions *opts, InputMethod_InputMethodProxy **im) {
    CHECK(opts->show, "focus requests keyboard"); ++attach_calls;
    if (failing("OH_InputMethodController_Attach")) return 1;
    InputMethod_TextConfig config = {0}; config.cursor.borrowed = 1;
    p->cfg(p, &config);
    *im = (InputMethod_InputMethodProxy *)native_alloc(sizeof(**im), "attached proxy");
    return 0;
}
static InputMethod_ErrorCode OH_InputMethodController_Detach(InputMethod_InputMethodProxy *im) {
    native_release(im); return 0;
}

#define RTLD_NOW 2
#define RTLD_LOCAL 0
static void *test_dlopen(const char *name, int flags) {
    (void)flags;
    if (missing_library && strcmp(missing_library, name) == 0) return NULL;
    if (strcmp(name, "libpasteboard.so") == 0) return (void *)(uintptr_t)1;
    if (strcmp(name, "libudmf.so") == 0) return (void *)(uintptr_t)2;
    CHECK(strcmp(name, "libohinputmethod.so") == 0, "load official IME library");
    return (void *)(uintptr_t)3;
}
static int test_dlclose(void *handle) { CHECK(handle, "close valid library"); ++library_closes; return 0; }
static void *test_dlsym(void *handle, const char *name) {
    CHECK(handle, "resolve within explicit system library");
    if (missing_symbol && strcmp(name, missing_symbol) == 0) return NULL;
#define MATCH(symbol) if (strcmp(name, #symbol) == 0) return (void *)(uintptr_t)&symbol;
    MATCH(OH_Pasteboard_Create) MATCH(OH_Pasteboard_Destroy)
    MATCH(OH_Pasteboard_GetData) MATCH(OH_Pasteboard_SetData)
    MATCH(OH_UdmfData_Create) MATCH(OH_UdmfData_Destroy)
    MATCH(OH_UdmfData_AddRecord) MATCH(OH_UdmfData_GetRecords)
    MATCH(OH_UdmfRecord_Create) MATCH(OH_UdmfRecord_Destroy)
    MATCH(OH_UdmfRecord_AddPlainText) MATCH(OH_UdmfRecord_GetPlainText)
    MATCH(OH_UdsPlainText_Create) MATCH(OH_UdsPlainText_Destroy)
    MATCH(OH_UdsPlainText_SetContent) MATCH(OH_UdsPlainText_GetContent)
    MATCH(OH_TextEditorProxy_Create) MATCH(OH_TextEditorProxy_Destroy)
#define MATCH_SETTER(n, field) MATCH(OH_TextEditorProxy_Set##n##Func)
    TEST_IME_SETTERS(MATCH_SETTER)
#undef MATCH_SETTER
    MATCH(OH_AttachOptions_Create) MATCH(OH_AttachOptions_Destroy)
    MATCH(OH_InputMethodController_Attach) MATCH(OH_InputMethodController_Detach)
    MATCH(OH_TextConfig_SetInputType) MATCH(OH_TextConfig_SetEnterKeyType)
    MATCH(OH_TextConfig_SetPreviewTextSupport) MATCH(OH_TextConfig_GetCursorInfo)
    MATCH(OH_CursorInfo_Create) MATCH(OH_CursorInfo_Destroy) MATCH(OH_CursorInfo_SetRect)
    MATCH(OH_InputMethodProxy_NotifyCursorUpdate)
#undef MATCH
    CHECK(0, "unexpected native symbol"); return NULL;
}
#define ZAN_GUI_OHOS_NATIVE_TEST
#define dlopen test_dlopen
#define dlsym test_dlsym
#define dlclose test_dlclose
#define malloc test_malloc
#define free test_free
#endif

/* Only native-window/graphics storage is a boundary fixture. Ring, merging,
 * sequence barriers, polling and public Destroy are the production functions. */
static struct { void *nw; int w, h, attached, closed; } g_owin;
static int g_window_width, g_window_height;
#define ZAN_OHOS_HWND ((iptr)&g_owin)
#include "../../src/drivers/gui/gui_runtime_ohos_events.c"
#include "../../src/drivers/gui/gui_runtime_ohos_clipboard.c"
#include "../../src/drivers/gui/gui_runtime_ohos_input.c"

#ifdef ZAN_GUI_OHOS_SDK_CHECK
/* Force the linker to verify all used system symbols. These strong references
 * exist only in this SDK verification executable, not in the shipped driver. */
#define SDK_REF(name) (void *)&name,
static void *volatile sdk_symbols[] = {
    SDK_REF(OH_Pasteboard_Create) SDK_REF(OH_Pasteboard_Destroy)
    SDK_REF(OH_Pasteboard_GetData) SDK_REF(OH_Pasteboard_SetData)
    SDK_REF(OH_UdmfData_Create) SDK_REF(OH_UdmfData_Destroy)
    SDK_REF(OH_UdmfData_AddRecord) SDK_REF(OH_UdmfData_GetRecords)
    SDK_REF(OH_UdmfRecord_Create) SDK_REF(OH_UdmfRecord_Destroy)
    SDK_REF(OH_UdmfRecord_AddPlainText) SDK_REF(OH_UdmfRecord_GetPlainText)
    SDK_REF(OH_UdsPlainText_Create) SDK_REF(OH_UdsPlainText_Destroy)
    SDK_REF(OH_UdsPlainText_SetContent) SDK_REF(OH_UdsPlainText_GetContent)
    SDK_REF(OH_TextEditorProxy_Create) SDK_REF(OH_TextEditorProxy_Destroy)
    SDK_REF(OH_TextEditorProxy_SetGetTextConfigFunc) SDK_REF(OH_TextEditorProxy_SetInsertTextFunc)
    SDK_REF(OH_TextEditorProxy_SetDeleteForwardFunc) SDK_REF(OH_TextEditorProxy_SetDeleteBackwardFunc)
    SDK_REF(OH_TextEditorProxy_SetSendEnterKeyFunc) SDK_REF(OH_TextEditorProxy_SetMoveCursorFunc)
    SDK_REF(OH_TextEditorProxy_SetSendKeyboardStatusFunc) SDK_REF(OH_TextEditorProxy_SetHandleSetSelectionFunc)
    SDK_REF(OH_TextEditorProxy_SetHandleExtendActionFunc) SDK_REF(OH_TextEditorProxy_SetGetLeftTextOfCursorFunc)
    SDK_REF(OH_TextEditorProxy_SetGetRightTextOfCursorFunc) SDK_REF(OH_TextEditorProxy_SetGetTextIndexAtCursorFunc)
    SDK_REF(OH_TextEditorProxy_SetReceivePrivateCommandFunc) SDK_REF(OH_TextEditorProxy_SetSetPreviewTextFunc)
    SDK_REF(OH_TextEditorProxy_SetFinishTextPreviewFunc)
    SDK_REF(OH_AttachOptions_Create) SDK_REF(OH_AttachOptions_Destroy)
    SDK_REF(OH_InputMethodController_Attach) SDK_REF(OH_InputMethodController_Detach)
    SDK_REF(OH_TextConfig_SetInputType) SDK_REF(OH_TextConfig_SetEnterKeyType)
    SDK_REF(OH_TextConfig_SetPreviewTextSupport) SDK_REF(OH_TextConfig_GetCursorInfo)
    SDK_REF(OH_CursorInfo_Create) SDK_REF(OH_CursorInfo_Destroy) SDK_REF(OH_CursorInfo_SetRect)
    SDK_REF(OH_InputMethodProxy_NotifyCursorUpdate)
};
#undef SDK_REF
int main(void) { return sdk_symbols[0] ? 0 : 1; }
#else
#undef malloc
#undef free
#undef dlopen
#undef dlsym
#undef dlclose

static void reset_clip_loader(void) {
    CHECK(ohos_clip_clear_result(), "clear previous returned snapshot");
    if (g_ohos_pasteboard_lib) test_dlclose(g_ohos_pasteboard_lib);
    if (g_ohos_udmf_lib) test_dlclose(g_ohos_udmf_lib);
    g_ohos_pasteboard_lib = g_ohos_udmf_lib = NULL;
    memset(&g_ohos_clip, 0, sizeof(g_ohos_clip));
    g_ohos_clip_ready = 0;
    pthread_once_t fresh = PTHREAD_ONCE_INIT; g_ohos_clip_once = fresh;
    fail_api = missing_symbol = missing_library = NULL;
    no_records = nontext_first = data_with_error = 0;
    CHECK(native_live == 0, "all clipboard native objects released");
}
static void clear_snapshots(void) {
    CHECK(ohos_clip_clear_result(), "clear clipboard TLS");
    if (g_ime_text_key_ready) {
        void *p = pthread_getspecific(g_ime_text_key);
        CHECK(pthread_setspecific(g_ime_text_key, NULL) == 0, "clear IME TLS");
        test_free(p);
    }
}

static void test_clipboard(void) {
    reset_clip_loader();
    missing_library = "libpasteboard.so";
    CHECK(zan_gui_set_clipboard("not local") != 0, "missing system library fails");
    CHECK(!*zan_gui_get_clipboard(), "missing system library returns empty");
    reset_clip_loader(); missing_symbol = "OH_Pasteboard_GetData";
    CHECK(zan_gui_set_clipboard("not local") != 0, "incomplete API fails atomically");
    CHECK(!*zan_gui_get_clipboard(), "incomplete API has no cached success");
    reset_clip_loader();
    CHECK(zan_gui_set_clipboard(NULL) != 0, "NULL write fails");
    char text[8193];
    for (size_t i = 0; i < sizeof(text) - 1; ++i) text[i] = 'a';
    memcpy(text + 8184, "\xE4\xB8\xAD\xF0\x9F\x98\x80", 7); text[8192] = 0;
    CHECK(zan_gui_set_clipboard(text) == 0, "long UTF-8 reaches system pasteboard");
    CHECK(strcmp(system_text, text) == 0, "system text is never truncated at 2048 bytes");
    CHECK(strcmp(zan_gui_get_clipboard(), text) == 0, "copy read before destroying native data");
    free(system_text); system_text = copy_text("external writer");
    CHECK(strcmp(zan_gui_get_clipboard(), "external writer") == 0, "every get reads external updates");
    fail_api = "OH_Pasteboard_GetData";
    CHECK(!*zan_gui_get_clipboard(), "permission denial cannot return an old snapshot");
    fail_api = NULL; nontext_first = 1;
    CHECK(strcmp(zan_gui_get_clipboard(), "external writer") == 0, "skip nontext records");
    nontext_first = 0; no_records = 1;
    CHECK(!*zan_gui_get_clipboard(), "empty record list returns empty");
    no_records = 0; data_with_error = 1;
    CHECK(!*zan_gui_get_clipboard(), "nonnull error data is rejected and freed");
    data_with_error = 0;
    fail_alloc_after = 0;
    CHECK(!*zan_gui_get_clipboard(), "allocation failure returns empty");
    const char *set_failures[] = { "OH_Pasteboard_Create", "OH_UdmfData_Create",
        "OH_UdmfRecord_Create", "OH_UdsPlainText_Create", "OH_UdsPlainText_SetContent",
        "OH_UdmfRecord_AddPlainText", "OH_UdmfData_AddRecord", "OH_Pasteboard_SetData" };
    for (size_t i = 0; i < sizeof(set_failures) / sizeof(*set_failures); ++i) {
        fail_api = set_failures[i];
        CHECK(zan_gui_set_clipboard("failure") != 0, "propagate write-stage failure");
        CHECK(native_live == 0, "release partial write graph");
    }
    const char *get_failures[] = { "OH_Pasteboard_Create", "OH_UdmfData_Create",
        "OH_UdmfRecord_Create", "OH_UdsPlainText_Create", "OH_UdmfData_GetRecords",
        "OH_UdmfRecord_GetPlainText", "OH_UdsPlainText_GetContent" };
    for (size_t i = 0; i < sizeof(get_failures) / sizeof(*get_failures); ++i) {
        fail_api = get_failures[i];
        CHECK(!*zan_gui_get_clipboard(), "propagate read-stage failure");
        CHECK(native_live == 0, "release partial read graph");
    }
    fail_api = NULL;
    CHECK(zan_gui_set_clipboard("") == 0, "empty text is a real system write");
    CHECK(!*zan_gui_get_clipboard(), "read empty system text");
    reset_clip_loader();
    CHECK(set_calls > 1 && get_calls > 1, "exercise real production API boundary");
}

static void drain_events(void) {
    while (zan_gui_poll_event() == 0) { }
    CHECK(zan_gui_inject_pending() == 0, "production queue fully drained");
}

static void expect_event(int kind, int code) {
    CHECK(zan_gui_poll_event() == 0, "expected event available through public poll");
    CHECK(zan_gui_event_kind() == kind && zan_gui_event_keycode() == code, "event kind/code retain FIFO order");
    CHECK(zan_gui_event_hwnd() == ZAN_OHOS_HWND, "event retains source window");
}

static void expect_empty(void) {
    CHECK(zan_gui_poll_event() == 1 && zan_gui_inject_pending() == 0, "no extra or truncated-operation residue");
}

static void reset_ime_loader(void) {
    zan_gui_ohos_ime_shutdown();
    drain_events();
    if (g_ohos_ime_lib) test_dlclose(g_ohos_ime_lib);
    g_ohos_ime_lib = NULL; g_ohos_ime_ready = 0;
    memset(&g_ohos_ime, 0, sizeof(g_ohos_ime));
    pthread_once_t fresh = PTHREAD_ONCE_INIT; g_ohos_ime_once = fresh;
    fail_api = missing_symbol = missing_library = NULL;
    CHECK(native_live == 0, "release IME and cursor native objects");
}

static void test_ime_loading(void) {
    missing_library = "libohinputmethod.so";
    zan_gui_set_ime_open(1);
    CHECK(!g_ime_proxy && !g_ime_im, "missing IME library does not attach");
    reset_ime_loader();
    const char *missing[] = { "OH_TextEditorProxy_SetSendKeyboardStatusFunc",
        "OH_TextEditorProxy_SetHandleSetSelectionFunc", "OH_TextEditorProxy_SetHandleExtendActionFunc",
        "OH_TextEditorProxy_SetFinishTextPreviewFunc", "OH_InputMethodProxy_NotifyCursorUpdate" };
    for (size_t i = 0; i < sizeof(missing) / sizeof(*missing); ++i) {
        missing_symbol = missing[i];
        zan_gui_set_ime_open(1);
        CHECK(!g_ime_proxy && !g_ime_im, "missing callback symbol cannot be called through NULL");
        reset_ime_loader();
    }
    const char *failures[] = { "OH_TextEditorProxy_Create", "OH_AttachOptions_Create",
        "OH_TextEditorProxy_SetFinishTextPreviewFunc", "OH_InputMethodController_Attach" };
    for (size_t i = 0; i < sizeof(failures) / sizeof(*failures); ++i) {
        fail_api = failures[i];
        zan_gui_set_ime_open(1);
        CHECK(!g_ime_im, "IME create/register/attach failures do not mark attached");
        reset_ime_loader();
    }
}

static void test_ime_geometry(void) {
    zan_gui_set_ime_pos(10, 20);
    int before = notify_calls;
    zan_gui_set_ime_open(1);
    CHECK(g_ime_proxy && g_ime_im && preview_supported, "native proxy ABI and preview enabled");
    CHECK(notify_calls == before, "unknown screen origin never sends local coordinates");
    CHECK(zan_gui_ohos_set_geometry(100, 200, 2) == 0, "host supplies physical screen origin and scale");
    CHECK(last_cursor[0] == 120 && last_cursor[1] == 240, "surface pixels map to absolute screen pixels");
    CHECK(last_cursor[2] == 2 && last_cursor[3] == 2, "cursor marker follows surface transform");
    zan_gui_set_ime_pos(-5, 3);
    CHECK(last_cursor[0] == 90 && last_cursor[1] == 206, "negative local coordinates retain signed origin");
    InputMethod_TextConfig cfg = {0}; cfg.cursor.borrowed = 1;
    g_ime_proxy->cfg(g_ime_proxy, &cfg);
    CHECK(cfg.cursor.rect[0] == 90 && cfg.cursor.rect[1] == 206, "initial config uses same absolute transform");
    before = notify_calls;
    ohos_ime_invalidate_geometry(); /* Same lifecycle hook as surface attach/detach. */
    zan_gui_set_ime_pos(4, 6);
    CHECK(notify_calls == before, "surface lifecycle invalidation suppresses cursor notifications");
    InputMethod_TextConfig invalid_cfg = {0}; invalid_cfg.cursor.borrowed = 1;
    g_ime_proxy->cfg(g_ime_proxy, &invalid_cfg);
    CHECK(invalid_cfg.cursor.rect[0] == 0 && invalid_cfg.cursor.rect[1] == 0 &&
          invalid_cfg.cursor.rect[2] == 0 && invalid_cfg.cursor.rect[3] == 0,
          "invalidated geometry does not write stale coordinates into config");
    CHECK(zan_gui_ohos_set_geometry(300, 400, 1.5) == 0, "new surface geometry restores cursor delivery");
    CHECK(notify_calls == before + 1 && last_cursor[0] == 306 && last_cursor[1] == 409,
          "restored geometry uses the new origin and latest local position");
    before = notify_calls;
    CHECK(zan_gui_ohos_set_geometry(0, 0, 0) == -1, "host invalidates unavailable geometry");
    zan_gui_set_ime_pos(1, 2);
    CHECK(notify_calls == before, "invalid geometry suppresses notifications");
    CHECK(zan_gui_ohos_set_geometry(NAN, 0, 1) == -1, "NaN is not screen geometry");
    CHECK(zan_gui_ohos_set_geometry(1, 2, INFINITY) == -1, "infinite scale rejected");
    fail_api = "OH_CursorInfo_Create";
    CHECK(zan_gui_ohos_set_geometry(1, 2, 1) == 0, "native cursor allocation failure is safe");
    CHECK(notify_calls == before, "failed cursor allocation does not notify");
    fail_api = NULL;
}

static void test_ime_preview(void) {
    const char16_t text[] = { 0x4E2D, 0xD83D, 0xDE00 };
    CHECK(g_ime_proxy->preview(g_ime_proxy, text, 3, -1, -1) == 0, "actual callback accepts UTF-16 preview");
    CHECK(strcmp(zan_gui_ime_composing(), "\xE4\xB8\xAD\xF0\x9F\x98\x80") == 0, "preview converts surrogate pair");
    const char16_t replacement[] = { 'x' };
    CHECK(g_ime_proxy->preview(g_ime_proxy, replacement, 1, 1, 3) == 0, "preview replaces UTF-16 subrange");
    CHECK(strcmp(zan_gui_ime_composing(), "\xE4\xB8\xADx") == 0, "subrange replacement keeps prefix");
    fail_alloc_after = 1;
    CHECK(g_ime_proxy->preview(g_ime_proxy, text, 3, -1, -1) != 0, "UTF-8 allocation failure reports error");
    CHECK(strcmp(zan_gui_ime_composing(), "\xE4\xB8\xADx") == 0, "failed update preserves prior preview");
    CHECK(g_ime_proxy->preview(g_ime_proxy, NULL, 0, -1, -1) == 0, "empty preview update is valid");
    CHECK(g_ime_preview_active && !*zan_gui_ime_composing(), "empty preview is still active until finish/commit");
    CHECK(g_ime_proxy->preview(g_ime_proxy, NULL, 1, -1, -1) != 0, "invalid buffer rejected");
    CHECK(g_ime_proxy->preview(g_ime_proxy, text, 3, 0, 2) != 0, "invalid replacement range rejected");
    CHECK(g_ime_proxy->preview(g_ime_proxy, text, 3, -1, -1) == 0, "restore preview");
    const char *snapshot = zan_gui_ime_composing();
    drain_events();
    g_ime_proxy->insert(g_ime_proxy, text, 3);
    CHECK(strcmp(snapshot, "\xE4\xB8\xAD\xF0\x9F\x98\x80") == 0, "callback cannot invalidate caller snapshot");
    CHECK(!g_ime_preview_active && !*zan_gui_ime_composing(), "commit clears preview");
    expect_event(6, 0x4E2D);
    expect_event(6, 0x1F600);
    expect_empty();
    CHECK(g_ime_proxy->preview(g_ime_proxy, text, 3, -1, -1) == 0, "start next composition");
    g_ime_proxy->finish(g_ime_proxy);
    CHECK(!g_ime_preview_active && !*zan_gui_ime_composing(), "finish callback clears preview");
    CHECK(g_ime_proxy->preview(g_ime_proxy, text, 3, -1, -1) == 0, "preview before blur");
    InputMethod_TextEditorProxy *old = g_ime_proxy;
    OH_TextEditorProxy_InsertTextFunc late_insert = old->insert;
    OH_TextEditorProxy_SetPreviewTextFunc late_preview = old->preview;
    OH_TextEditorProxy_FinishTextPreviewFunc late_finish = old->finish;
    zan_gui_set_ime_open(0);
    CHECK(!g_ime_im && !g_ime_preview_active && !*zan_gui_ime_composing(), "blur clears native session and preview");
    late_insert(old, text, 3);
    CHECK(late_preview(old, text, 3, -1, -1) == IME_ERR_DETACHED, "late preview rejected after blur");
    late_finish(old);
    CHECK(!g_ime_operation_head && !g_ime_preview_active, "late callbacks cannot submit or revive preview");
    reset_ime_loader();
}

static void fill_ring(void) {
    for (int i = 0; i < ZAN_OQ_CAP - 1; ++i)
        CHECK(zan_gui_inject_event(ZAN_OHOS_HWND, 4, 0, 0, 0, 1000 + i, 0) == 0, "fill production ring");
    CHECK(zan_gui_inject_pending() == ZAN_OQ_CAP - 1, "actual ring is at capacity");
}

static void test_long_commit(void) {
    zan_gui_set_ime_open(1);
    drain_events();
    char16_t text[4096];
    for (size_t i = 0; i < 4096; ++i) text[i] = (char16_t)('a' + i % 26);
    fill_ring();
    g_ime_proxy->insert(g_ime_proxy, text, 4096);
    expect_event(4, 1000);
    CHECK(zan_gui_inject_event(ZAN_OHOS_HWND, 5, 0, 0, 0, 777, 0) == 0, "enqueue later ordinary event");
    for (int i = 1; i < ZAN_OQ_CAP - 1; ++i) expect_event(4, 1000 + i);
    for (size_t i = 0; i < 4096; ++i) expect_event(6, text[i]);
    expect_event(5, 777);
    expect_empty();
    reset_ime_loader();
}

static void test_delete_operations(void) {
    zan_gui_set_ime_open(1);
    drain_events();
    g_ime_proxy->delback(g_ime_proxy, 512);
    for (int i = 0; i < 512; ++i) expect_event(6, 8);
    expect_empty();
    g_ime_proxy->delfwd(g_ime_proxy, 512);
    for (int i = 0; i < 512; ++i) expect_event(4, 46);
    expect_empty();
    fill_ring();
    g_ime_proxy->delback(g_ime_proxy, 1);
    g_ime_proxy->delfwd(g_ime_proxy, 1);
    expect_event(4, 1000);
    CHECK(zan_gui_inject_event(ZAN_OHOS_HWND, 5, 0, 0, 0, 999, 0) == 0, "ordinary event after full-ring deletes");
    for (int i = 1; i < ZAN_OQ_CAP - 1; ++i) expect_event(4, 1000 + i);
    expect_event(6, 8);
    expect_event(4, 46);
    expect_event(5, 999);
    expect_empty();
    g_ime_proxy->delback(g_ime_proxy, 0);
    g_ime_proxy->delfwd(g_ime_proxy, -1);
    expect_empty();
    fail_alloc_after = 0;
    g_ime_proxy->delback(g_ime_proxy, 512);
    expect_empty(); /* SDK void callback has no failure result; no partial delete. */
    reset_ime_loader();
}

static void test_coalescing_barriers(void) {
    zan_gui_set_ime_open(1);
    drain_events();
    zan_gui_inject_event(ZAN_OHOS_HWND, 1, 10, 20, 0, 0, 0);
    zan_gui_inject_event(ZAN_OHOS_HWND, 1, 11, 21, 0, 0, 0);
    CHECK(zan_gui_inject_pending() == 1, "adjacent production moves coalesce");
    const char16_t text[] = { 'x' };
    g_ime_proxy->insert(g_ime_proxy, text, 1);
    zan_gui_inject_event(ZAN_OHOS_HWND, 1, 30, 40, 0, 0, 0);
    zan_gui_inject_event(ZAN_OHOS_HWND, 1, 31, 41, 0, 0, 0);
    expect_event(1, 0);
    CHECK(zan_gui_event_x() == 11 && zan_gui_event_y() == 21, "move before text barrier stays before");
    expect_event(6, 'x');
    expect_event(1, 0);
    CHECK(zan_gui_event_x() == 31 && zan_gui_event_y() == 41, "later moves merge only after barrier");
    zan_gui_inject_event(ZAN_OHOS_HWND, 13, 1, 2, 0, 3, 0);
    zan_gui_inject_event(ZAN_OHOS_HWND, 13, 2, 3, 0, 4, 0);
    g_ime_proxy->delback(g_ime_proxy, 2);
    zan_gui_inject_event(ZAN_OHOS_HWND, 13, 3, 4, 0, 5, 0);
    zan_gui_inject_event(ZAN_OHOS_HWND, 13, 4, 5, 0, 6, 0);
    expect_event(13, 7);
    expect_event(6, 8); expect_event(6, 8);
    expect_event(13, 11);
    pthread_mutex_lock(&g_oq_lock);
    oq_push_flag_locked(3, 7, 8, 1, 0, 0);
    pthread_mutex_unlock(&g_oq_lock);
    expect_event(3, 0);
    CHECK(zan_gui_event_flag() == 1 && zan_gui_event_button() == 1 &&
          zan_gui_event_mods() == 0 && zan_gui_event_seq() > 0, "production flag wrapper retains event metadata");
    expect_empty();
    reset_ime_loader();
}

static void test_destroy_lifecycle(void) {
    iptr window = zan_gui_create_window("native test", 640, 480);
    CHECK(window == ZAN_OHOS_HWND, "public create returns surface window handle");
    ohos_window_attach((void *)(uintptr_t)1, 640, 480);
    CHECK(g_owin.attached && g_window_width == 640 && g_window_height == 480, "production surface attach");
    drain_events();
    zan_gui_set_ime_open(1);
    CHECK(zan_gui_ohos_set_geometry(50, 60, 1) == 0, "valid geometry before destroy");
    InputMethod_TextEditorProxy *old = g_ime_proxy;
    const char16_t text[] = { 'x' };
    CHECK(old->preview(old, text, 1, -1, -1) == 0, "preview before public destroy");
    old->insert(old, text, 1);
    old->delback(old, 512);
    CHECK(zan_gui_destroy_window(window) == 0, "public destroy performs session cleanup");
    CHECK(!g_owin.attached && !g_owin.nw && !g_ime_proxy && !g_ime_im &&
          !g_ime_geometry_valid && !g_ime_preview_active, "destroy clears surface, IME and geometry");
    old->insert(old, text, 1); old->delback(old, 1); old->delfwd(old, 1);
    old->finish(old);
    CHECK(old->preview(old, text, 1, -1, -1) == IME_ERR_DETACHED, "destroyed-session preview rejected");
    expect_empty();
    CHECK(zan_gui_create_window("reopened", 800, 600) == window, "public recreate reuses mobile surface handle");
    ohos_window_attach((void *)(uintptr_t)2, 800, 600);
    drain_events();
    int before = notify_calls;
    zan_gui_set_ime_open(1);
    InputMethod_TextEditorProxy *current = g_ime_proxy;
    CHECK(current && current != old, "new native fixture session has its own proxy identity");
    CHECK(notify_calls == before, "reopen cannot reuse destroyed surface geometry");
    CHECK(current->preview(current, text, 1, -1, -1) == 0, "new session composition starts");
    drain_events();
    old->insert(old, text, 1); old->delback(old, 1); old->delfwd(old, 1);
    old->finish(old);
    CHECK(old->preview(old, text, 1, -1, -1) == IME_ERR_DETACHED, "old proxy rejected after reopen");
    CHECK(g_ime_preview_active && strcmp(zan_gui_ime_composing(), "x") == 0, "old finish cannot clear new preview");
    expect_empty();
    current->insert(current, text, 1);
    expect_event(6, 'x'); expect_empty();
    CHECK(zan_gui_destroy_window(window) == 0, "destroy reopened window");
    expect_empty();
    reset_ime_loader();
}

int main(void) {
    test_clipboard();
    test_ime_loading();
    test_ime_geometry();
    test_ime_preview();
    test_long_commit();
    test_delete_operations();
    test_coalescing_barriers();
    test_destroy_lifecycle();
    clear_snapshots();
    CHECK(native_live == 0 && live_buffers == 0, "all native wrappers and result buffers released");
    CHECK(create_proxy_calls > 0 && attach_calls > 0 && library_closes > 0, "exercise loader and real callback path");
    free(system_text);
    for (size_t i = 0; i < retired_proxy_count; ++i) free(retired_proxies[i]);
    puts("PASS: OHOS production clipboard, event ring, IME and destroy lifecycle");
    return 0;
}
#endif
