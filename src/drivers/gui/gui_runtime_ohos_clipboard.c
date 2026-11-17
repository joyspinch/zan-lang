/* Included by gui_runtime_ohos.c; SDK APIs are optional at runtime (API 13+).
 * Host fault tests replace only the platform headers/loader/allocation boundary. */
#ifndef ZAN_GUI_OHOS_NATIVE_TEST
#include <database/pasteboard/oh_pasteboard.h>
#include <database/pasteboard/oh_pasteboard_err_code.h>
#include <database/udmf/udmf.h>
#include <database/udmf/udmf_err_code.h>
#include <database/udmf/uds.h>
#include <dlfcn.h>
#include <pthread.h>
#endif
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Derive every function type from the real SDK declaration. No strong reference
 * is emitted: old systems can still load the GUI library and report failure. */
#define OHOS_CLIP_FUNCTIONS(X) \
    X(OH_Pasteboard_Create) X(OH_Pasteboard_Destroy) \
    X(OH_Pasteboard_SetData) X(OH_Pasteboard_GetData) \
    X(OH_UdmfData_Create) X(OH_UdmfData_Destroy) \
    X(OH_UdmfData_AddRecord) X(OH_UdmfData_GetRecords) \
    X(OH_UdmfRecord_Create) X(OH_UdmfRecord_Destroy) \
    X(OH_UdmfRecord_AddPlainText) X(OH_UdmfRecord_GetPlainText) \
    X(OH_UdsPlainText_Create) X(OH_UdsPlainText_Destroy) \
    X(OH_UdsPlainText_SetContent) X(OH_UdsPlainText_GetContent)
#define OHOS_CLIP_FIELD(name) __typeof__(&name) name;
static struct { OHOS_CLIP_FUNCTIONS(OHOS_CLIP_FIELD) } g_ohos_clip;
#undef OHOS_CLIP_FIELD
static pthread_once_t g_ohos_clip_once = PTHREAD_ONCE_INIT;
static void *g_ohos_pasteboard_lib, *g_ohos_udmf_lib;
static int g_ohos_clip_ready;

static void ohos_clip_init(void) {
    g_ohos_pasteboard_lib = dlopen("libpasteboard.so", RTLD_NOW | RTLD_LOCAL);
    g_ohos_udmf_lib = dlopen("libudmf.so", RTLD_NOW | RTLD_LOCAL);
    if (!g_ohos_pasteboard_lib || !g_ohos_udmf_lib) goto fail;
#define OHOS_CLIP_LOAD(name) \
    g_ohos_clip.name = (__typeof__(&name))dlsym( \
        strncmp(#name, "OH_Pasteboard_", 14) == 0 \
            ? g_ohos_pasteboard_lib : g_ohos_udmf_lib, #name); \
    if (!g_ohos_clip.name) goto fail;
    OHOS_CLIP_FUNCTIONS(OHOS_CLIP_LOAD)
#undef OHOS_CLIP_LOAD
    g_ohos_clip_ready = 1;
    return;
fail:
    memset(&g_ohos_clip, 0, sizeof(g_ohos_clip));
    if (g_ohos_pasteboard_lib) dlclose(g_ohos_pasteboard_lib);
    if (g_ohos_udmf_lib) dlclose(g_ohos_udmf_lib);
    g_ohos_pasteboard_lib = g_ohos_udmf_lib = NULL;
}
#undef OHOS_CLIP_FUNCTIONS

/* A returned string is a snapshot, valid until the next get on this thread.
 * TLS owns only a successful system read, never a local clipboard substitute. */
static pthread_once_t g_ohos_clip_key_once = PTHREAD_ONCE_INIT;
static pthread_key_t g_ohos_clip_key;
static int g_ohos_clip_key_ready;
static void ohos_clip_key_init(void) {
    g_ohos_clip_key_ready = pthread_key_create(&g_ohos_clip_key, free) == 0;
}

static int ohos_clip_clear_result(void) {
    if (pthread_once(&g_ohos_clip_key_once, ohos_clip_key_init) != 0 ||
        !g_ohos_clip_key_ready) return 0;
    void *old = pthread_getspecific(g_ohos_clip_key);
    if (pthread_setspecific(g_ohos_clip_key, NULL) != 0) return 0;
    free(old);
    return 1;
}

EXPORT i32 zan_gui_set_clipboard(const char *utf8) {
    if (!utf8 || pthread_once(&g_ohos_clip_once, ohos_clip_init) != 0 ||
        !g_ohos_clip_ready) return 1;
    OH_Pasteboard *board = g_ohos_clip.OH_Pasteboard_Create();
    OH_UdmfData *data = g_ohos_clip.OH_UdmfData_Create();
    OH_UdmfRecord *record = g_ohos_clip.OH_UdmfRecord_Create();
    OH_UdsPlainText *plain = g_ohos_clip.OH_UdsPlainText_Create();
    int result = 1;
    if (!board || !data || !record || !plain) goto done;
    if (g_ohos_clip.OH_UdsPlainText_SetContent(plain, utf8) != UDMF_E_OK ||
        g_ohos_clip.OH_UdmfRecord_AddPlainText(record, plain) != UDMF_E_OK ||
        g_ohos_clip.OH_UdmfData_AddRecord(data, record) != UDMF_E_OK) goto done;
    result = g_ohos_clip.OH_Pasteboard_SetData(board, data) == ERR_OK ? 0 : 1;
done:
    /* AddRecord/AddPlainText retain the underlying value, not these wrappers. */
    if (plain) g_ohos_clip.OH_UdsPlainText_Destroy(plain);
    if (record) g_ohos_clip.OH_UdmfRecord_Destroy(record);
    if (data) g_ohos_clip.OH_UdmfData_Destroy(data);
    if (board) g_ohos_clip.OH_Pasteboard_Destroy(board);
    return result;
}

EXPORT const char *zan_gui_get_clipboard(void) {
    if (!ohos_clip_clear_result() ||
        pthread_once(&g_ohos_clip_once, ohos_clip_init) != 0 ||
        !g_ohos_clip_ready) return "";
    OH_Pasteboard *board = g_ohos_clip.OH_Pasteboard_Create();
    OH_UdmfData *data = NULL;
    OH_UdsPlainText *plain = NULL;
    char *result = NULL;
    int status = -1;
    if (!board) goto done;
    /* READ_PASTEBOARD denial, a busy service and absent text all stay failures. */
    data = g_ohos_clip.OH_Pasteboard_GetData(board, &status);
    if (!data || status != ERR_OK) goto done;
    unsigned int count = 0;
    OH_UdmfRecord **records = g_ohos_clip.OH_UdmfData_GetRecords(data, &count);
    if (!records || !count) goto done;
    plain = g_ohos_clip.OH_UdsPlainText_Create();
    if (!plain) goto done;
    for (unsigned int i = 0; i < count; ++i) {
        if (!records[i] ||
            g_ohos_clip.OH_UdmfRecord_GetPlainText(records[i], plain) != UDMF_E_OK) continue;
        const char *text = g_ohos_clip.OH_UdsPlainText_GetContent(plain);
        if (!text) continue;
        size_t n = strlen(text);
        if (n == SIZE_MAX) break;
        result = (char *)malloc(n + 1);
        if (result) memcpy(result, text, n + 1);
        break;
    }
done:
    /* GetRecords and its wrappers are borrowed from data; never free them. */
    if (plain) g_ohos_clip.OH_UdsPlainText_Destroy(plain);
    if (data) g_ohos_clip.OH_UdmfData_Destroy(data);
    if (board) g_ohos_clip.OH_Pasteboard_Destroy(board);
    if (!result) return "";
    if (pthread_setspecific(g_ohos_clip_key, result) != 0) {
        free(result);
        return "";
    }
    return result;
}
