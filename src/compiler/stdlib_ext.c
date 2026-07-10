/* stdlib_ext.c -- Extended standard library runtime (M7.3). */

#include "stdlib_ext.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")
#else
#include <pthread.h>
#include <unistd.h>
#include <time.h>
#endif

#include "../common/host_oom.h"
/* ==== HTTP Client ==== */

#ifdef _WIN32

zan_http_response_t *zan_http_get(const char *url) {
    zan_http_response_t *resp = (zan_http_response_t *)calloc(1, sizeof(zan_http_response_t));
    resp->status_code = -1;

    /* Convert URL to wide string */
    int wlen = MultiByteToWideChar(CP_UTF8, 0, url, -1, NULL, 0);
    wchar_t *wurl = (wchar_t *)malloc(sizeof(wchar_t) * (size_t)wlen);
    MultiByteToWideChar(CP_UTF8, 0, url, -1, wurl, wlen);

    /* Crack URL */
    URL_COMPONENTSW uc;
    memset(&uc, 0, sizeof(uc));
    uc.dwStructSize = sizeof(uc);
    wchar_t host[256] = {0}, path[1024] = {0};
    uc.lpszHostName = host; uc.dwHostNameLength = 256;
    uc.lpszUrlPath = path; uc.dwUrlPathLength = 1024;
    if (!WinHttpCrackUrl(wurl, 0, 0, &uc)) { free(wurl); return resp; }

    HINTERNET session = WinHttpOpen(L"Zan/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, NULL, NULL, 0);
    if (!session) { free(wurl); return resp; }

    HINTERNET connect = WinHttpConnect(session, host, uc.nPort, 0);
    if (!connect) { WinHttpCloseHandle(session); free(wurl); return resp; }

    DWORD flags = (uc.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET request = WinHttpOpenRequest(connect, L"GET", path, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!request) { WinHttpCloseHandle(connect); WinHttpCloseHandle(session); free(wurl); return resp; }

    if (WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(request, NULL)) {
        DWORD status = 0, size = sizeof(status);
        WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, NULL, &status, &size, NULL);
        resp->status_code = (int)status;

        /* Read body */
        char *body = NULL;
        size_t body_len = 0, body_cap = 4096;
        body = (char *)malloc(body_cap);
        DWORD bytes_read;
        while (WinHttpReadData(request, body + body_len, (DWORD)(body_cap - body_len - 1), &bytes_read) && bytes_read > 0) {
            body_len += bytes_read;
            if (body_len + 1024 >= body_cap) { body_cap *= 2; body = (char *)realloc(body, body_cap); }
        }
        body[body_len] = 0;
        resp->body = body;
        resp->body_len = body_len;
    }

    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    free(wurl);
    return resp;
}

zan_http_response_t *zan_http_post(const char *url, const char *body, const char *content_type) {
    zan_http_response_t *resp = (zan_http_response_t *)calloc(1, sizeof(zan_http_response_t));
    resp->status_code = -1;

    int wlen = MultiByteToWideChar(CP_UTF8, 0, url, -1, NULL, 0);
    wchar_t *wurl = (wchar_t *)malloc(sizeof(wchar_t) * (size_t)wlen);
    MultiByteToWideChar(CP_UTF8, 0, url, -1, wurl, wlen);

    URL_COMPONENTSW uc;
    memset(&uc, 0, sizeof(uc));
    uc.dwStructSize = sizeof(uc);
    wchar_t host[256] = {0}, path[1024] = {0};
    uc.lpszHostName = host; uc.dwHostNameLength = 256;
    uc.lpszUrlPath = path; uc.dwUrlPathLength = 1024;
    if (!WinHttpCrackUrl(wurl, 0, 0, &uc)) { free(wurl); return resp; }

    HINTERNET session = WinHttpOpen(L"Zan/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, NULL, NULL, 0);
    if (!session) { free(wurl); return resp; }

    HINTERNET connect = WinHttpConnect(session, host, uc.nPort, 0);
    if (!connect) { WinHttpCloseHandle(session); free(wurl); return resp; }

    DWORD flags = (uc.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET request = WinHttpOpenRequest(connect, L"POST", path, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!request) { WinHttpCloseHandle(connect); WinHttpCloseHandle(session); free(wurl); return resp; }

    /* Set content type header */
    wchar_t ct_header[256];
    swprintf(ct_header, 256, L"Content-Type: %hs", content_type ? content_type : "application/json");

    DWORD body_len = body ? (DWORD)strlen(body) : 0;
    if (WinHttpSendRequest(request, ct_header, -1, (LPVOID)body, body_len, body_len, 0) &&
        WinHttpReceiveResponse(request, NULL)) {
        DWORD status = 0, size = sizeof(status);
        WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, NULL, &status, &size, NULL);
        resp->status_code = (int)status;

        char *rbody = (char *)malloc(4096);
        size_t rlen = 0, rcap = 4096;
        DWORD bytes_read;
        while (WinHttpReadData(request, rbody + rlen, (DWORD)(rcap - rlen - 1), &bytes_read) && bytes_read > 0) {
            rlen += bytes_read;
            if (rlen + 1024 >= rcap) { rcap *= 2; rbody = (char *)realloc(rbody, rcap); }
        }
        rbody[rlen] = 0;
        resp->body = rbody;
        resp->body_len = rlen;
    }

    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    free(wurl);
    return resp;
}

#else /* POSIX - use libcurl if available, otherwise stub */

zan_http_response_t *zan_http_get(const char *url) {
    (void)url;
    zan_http_response_t *resp = (zan_http_response_t *)calloc(1, sizeof(zan_http_response_t));
    resp->status_code = -1;
    resp->body = strdup("HTTP not available on this platform without libcurl");
    resp->body_len = strlen(resp->body);
    return resp;
}

zan_http_response_t *zan_http_post(const char *url, const char *body, const char *content_type) {
    (void)url; (void)body; (void)content_type;
    zan_http_response_t *resp = (zan_http_response_t *)calloc(1, sizeof(zan_http_response_t));
    resp->status_code = -1;
    return resp;
}

#endif

void zan_http_response_free(zan_http_response_t *resp) {
    if (!resp) return;
    free(resp->body);
    free(resp->headers);
    free(resp);
}

/* ==== Threading ==== */

#ifdef _WIN32

zan_thread_t zan_thread_create(zan_thread_fn fn, void *arg) {
    return (zan_thread_t)CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)fn, arg, 0, NULL);
}

void zan_thread_join(zan_thread_t thread) {
    WaitForSingleObject((HANDLE)thread, INFINITE);
    CloseHandle((HANDLE)thread);
}

void zan_thread_sleep(int ms) { Sleep((DWORD)ms); }

int64_t zan_thread_id(void) { return (int64_t)GetCurrentThreadId(); }

zan_mutex_t zan_mutex_create(void) {
    CRITICAL_SECTION *cs = (CRITICAL_SECTION *)malloc(sizeof(CRITICAL_SECTION));
    InitializeCriticalSection(cs);
    return (zan_mutex_t)cs;
}

void zan_mutex_lock(zan_mutex_t mtx) { EnterCriticalSection((CRITICAL_SECTION *)mtx); }
void zan_mutex_unlock(zan_mutex_t mtx) { LeaveCriticalSection((CRITICAL_SECTION *)mtx); }
void zan_mutex_destroy(zan_mutex_t mtx) { DeleteCriticalSection((CRITICAL_SECTION *)mtx); free(mtx); }

zan_event_t zan_event_create(void) { return (zan_event_t)CreateEventA(NULL, FALSE, FALSE, NULL); }
void zan_event_signal(zan_event_t evt) { SetEvent((HANDLE)evt); }
void zan_event_wait(zan_event_t evt) { WaitForSingleObject((HANDLE)evt, INFINITE); }
void zan_event_destroy(zan_event_t evt) { CloseHandle((HANDLE)evt); }

int64_t zan_atomic_add(volatile int64_t *ptr, int64_t val) { return InterlockedExchangeAdd64(ptr, val); }
int64_t zan_atomic_load(volatile int64_t *ptr) { return InterlockedCompareExchange64(ptr, 0, 0); }
void zan_atomic_store(volatile int64_t *ptr, int64_t val) { InterlockedExchange64(ptr, val); }

#else /* POSIX */

zan_thread_t zan_thread_create(zan_thread_fn fn, void *arg) {
    pthread_t *t = (pthread_t *)malloc(sizeof(pthread_t));
    pthread_create(t, NULL, (void *(*)(void *))fn, arg);
    return (zan_thread_t)t;
}

void zan_thread_join(zan_thread_t thread) {
    pthread_join(*(pthread_t *)thread, NULL);
    free(thread);
}

void zan_thread_sleep(int ms) {
    struct timespec ts = { ms / 1000, (ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}

int64_t zan_thread_id(void) { return (int64_t)(uintptr_t)pthread_self(); }

zan_mutex_t zan_mutex_create(void) {
    pthread_mutex_t *m = (pthread_mutex_t *)malloc(sizeof(pthread_mutex_t));
    pthread_mutex_init(m, NULL);
    return (zan_mutex_t)m;
}

void zan_mutex_lock(zan_mutex_t mtx) { pthread_mutex_lock((pthread_mutex_t *)mtx); }
void zan_mutex_unlock(zan_mutex_t mtx) { pthread_mutex_unlock((pthread_mutex_t *)mtx); }
void zan_mutex_destroy(zan_mutex_t mtx) { pthread_mutex_destroy((pthread_mutex_t *)mtx); free(mtx); }

zan_event_t zan_event_create(void) {
    /* Simplified: use mutex+cond pair */
    pthread_cond_t *c = (pthread_cond_t *)malloc(sizeof(pthread_cond_t));
    pthread_cond_init(c, NULL);
    return (zan_event_t)c;
}
void zan_event_signal(zan_event_t evt) { pthread_cond_signal((pthread_cond_t *)evt); }
void zan_event_wait(zan_event_t evt) { (void)evt; /* simplified */ }
void zan_event_destroy(zan_event_t evt) { pthread_cond_destroy((pthread_cond_t *)evt); free(evt); }

int64_t zan_atomic_add(volatile int64_t *ptr, int64_t val) { return __sync_fetch_and_add(ptr, val); }
int64_t zan_atomic_load(volatile int64_t *ptr) { return __sync_val_compare_and_swap(ptr, 0, 0); }
void zan_atomic_store(volatile int64_t *ptr, int64_t val) { __sync_lock_test_and_set(ptr, val); }

#endif

/* ==== JSON Parser ==== */

static void skip_json_ws(const char **p, const char *end) {
    while (*p < end && isspace((unsigned char)**p)) (*p)++;
}

static zan_json_value_t *parse_json_value(const char **p, const char *end);

static char *parse_json_string(const char **p, const char *end, size_t *out_len) {
    if (**p != '"') return NULL;
    (*p)++;
    size_t cap = 64, len = 0;
    char *buf = (char *)malloc(cap);
    while (*p < end && **p != '"') {
        if (**p == '\\') {
            (*p)++;
            if (*p >= end) break;
            switch (**p) {
            case '"': buf[len++] = '"'; break;
            case '\\': buf[len++] = '\\'; break;
            case '/': buf[len++] = '/'; break;
            case 'n': buf[len++] = '\n'; break;
            case 'r': buf[len++] = '\r'; break;
            case 't': buf[len++] = '\t'; break;
            default: buf[len++] = **p; break;
            }
        } else {
            buf[len++] = **p;
        }
        (*p)++;
        if (len + 2 >= cap) { cap *= 2; buf = (char *)realloc(buf, cap); }
    }
    if (*p < end && **p == '"') (*p)++;
    buf[len] = 0;
    if (out_len) *out_len = len;
    return buf;
}

static zan_json_value_t *parse_json_value(const char **p, const char *end) {
    skip_json_ws(p, end);
    if (*p >= end) return NULL;

    zan_json_value_t *val = (zan_json_value_t *)calloc(1, sizeof(zan_json_value_t));

    if (**p == '"') {
        val->type = ZAN_JSON_STRING;
        val->string_val.str = parse_json_string(p, end, &val->string_val.len);
    } else if (**p == '{') {
        val->type = ZAN_JSON_OBJECT;
        val->object_val.cap = 8;
        val->object_val.keys = (char **)malloc(sizeof(char *) * 8);
        val->object_val.values = (zan_json_value_t **)malloc(sizeof(zan_json_value_t *) * 8);
        (*p)++;
        skip_json_ws(p, end);
        while (*p < end && **p != '}') {
            skip_json_ws(p, end);
            char *key = parse_json_string(p, end, NULL);
            skip_json_ws(p, end);
            if (*p < end && **p == ':') (*p)++;
            zan_json_value_t *v = parse_json_value(p, end);
            if (val->object_val.count >= val->object_val.cap) {
                val->object_val.cap *= 2;
                val->object_val.keys = (char **)realloc(val->object_val.keys, sizeof(char *) * (size_t)val->object_val.cap);
                val->object_val.values = (zan_json_value_t **)realloc(val->object_val.values, sizeof(zan_json_value_t *) * (size_t)val->object_val.cap);
            }
            val->object_val.keys[val->object_val.count] = key;
            val->object_val.values[val->object_val.count] = v;
            val->object_val.count++;
            skip_json_ws(p, end);
            if (*p < end && **p == ',') (*p)++;
        }
        if (*p < end) (*p)++;
    } else if (**p == '[') {
        val->type = ZAN_JSON_ARRAY;
        val->array_val.cap = 8;
        val->array_val.items = (zan_json_value_t **)malloc(sizeof(zan_json_value_t *) * 8);
        (*p)++;
        skip_json_ws(p, end);
        while (*p < end && **p != ']') {
            zan_json_value_t *item = parse_json_value(p, end);
            if (val->array_val.count >= val->array_val.cap) {
                val->array_val.cap *= 2;
                val->array_val.items = (zan_json_value_t **)realloc(val->array_val.items, sizeof(zan_json_value_t *) * (size_t)val->array_val.cap);
            }
            val->array_val.items[val->array_val.count++] = item;
            skip_json_ws(p, end);
            if (*p < end && **p == ',') (*p)++;
        }
        if (*p < end) (*p)++;
    } else if (strncmp(*p, "true", 4) == 0) {
        val->type = ZAN_JSON_BOOL; val->bool_val = true; *p += 4;
    } else if (strncmp(*p, "false", 5) == 0) {
        val->type = ZAN_JSON_BOOL; val->bool_val = false; *p += 5;
    } else if (strncmp(*p, "null", 4) == 0) {
        val->type = ZAN_JSON_NULL; *p += 4;
    } else if (**p == '-' || isdigit((unsigned char)**p)) {
        val->type = ZAN_JSON_NUMBER;
        char *numend;
        val->number_val = strtod(*p, &numend);
        *p = numend;
    } else {
        free(val);
        return NULL;
    }

    return val;
}

zan_json_value_t *zan_json_parse(const char *json, size_t len) {
    const char *p = json;
    const char *end = json + len;
    return parse_json_value(&p, end);
}

/* JSON serialization */
static void json_serialize_value(zan_string_builder_t *sb, const zan_json_value_t *val, bool pretty, int depth);

static void json_indent(zan_string_builder_t *sb, int depth) {
    for (int i = 0; i < depth * 2; i++) zan_sb_append_char(sb, ' ');
}

static void json_serialize_value(zan_string_builder_t *sb, const zan_json_value_t *val, bool pretty, int depth) {
    if (!val) { zan_sb_append(sb, "null"); return; }

    switch (val->type) {
    case ZAN_JSON_NULL: zan_sb_append(sb, "null"); break;
    case ZAN_JSON_BOOL: zan_sb_append(sb, val->bool_val ? "true" : "false"); break;
    case ZAN_JSON_NUMBER: {
        char num[64];
        snprintf(num, sizeof(num), "%g", val->number_val);
        zan_sb_append(sb, num);
        break;
    }
    case ZAN_JSON_STRING:
        zan_sb_append_char(sb, '"');
        for (size_t i = 0; i < val->string_val.len; i++) {
            char c = val->string_val.str[i];
            switch (c) {
            case '"': zan_sb_append(sb, "\\\""); break;
            case '\\': zan_sb_append(sb, "\\\\"); break;
            case '\n': zan_sb_append(sb, "\\n"); break;
            case '\r': zan_sb_append(sb, "\\r"); break;
            case '\t': zan_sb_append(sb, "\\t"); break;
            default: zan_sb_append_char(sb, c); break;
            }
        }
        zan_sb_append_char(sb, '"');
        break;
    case ZAN_JSON_ARRAY:
        zan_sb_append_char(sb, '[');
        for (int i = 0; i < val->array_val.count; i++) {
            if (i > 0) zan_sb_append_char(sb, ',');
            if (pretty) { zan_sb_append_char(sb, '\n'); json_indent(sb, depth + 1); }
            json_serialize_value(sb, val->array_val.items[i], pretty, depth + 1);
        }
        if (pretty && val->array_val.count > 0) { zan_sb_append_char(sb, '\n'); json_indent(sb, depth); }
        zan_sb_append_char(sb, ']');
        break;
    case ZAN_JSON_OBJECT:
        zan_sb_append_char(sb, '{');
        for (int i = 0; i < val->object_val.count; i++) {
            if (i > 0) zan_sb_append_char(sb, ',');
            if (pretty) { zan_sb_append_char(sb, '\n'); json_indent(sb, depth + 1); }
            zan_sb_append_char(sb, '"');
            zan_sb_append(sb, val->object_val.keys[i]);
            zan_sb_append_char(sb, '"');
            zan_sb_append_char(sb, ':');
            if (pretty) zan_sb_append_char(sb, ' ');
            json_serialize_value(sb, val->object_val.values[i], pretty, depth + 1);
        }
        if (pretty && val->object_val.count > 0) { zan_sb_append_char(sb, '\n'); json_indent(sb, depth); }
        zan_sb_append_char(sb, '}');
        break;
    }
}

char *zan_json_serialize(const zan_json_value_t *val, bool pretty) {
    zan_string_builder_t sb;
    zan_sb_init(&sb);
    json_serialize_value(&sb, val, pretty, 0);
    return zan_sb_to_string(&sb);
}

zan_json_value_t *zan_json_get(const zan_json_value_t *obj, const char *key) {
    if (!obj || obj->type != ZAN_JSON_OBJECT) return NULL;
    for (int i = 0; i < obj->object_val.count; i++) {
        if (strcmp(obj->object_val.keys[i], key) == 0) return obj->object_val.values[i];
    }
    return NULL;
}

zan_json_value_t *zan_json_index(const zan_json_value_t *arr, int idx) {
    if (!arr || arr->type != ZAN_JSON_ARRAY || idx < 0 || idx >= arr->array_val.count) return NULL;
    return arr->array_val.items[idx];
}

const char *zan_json_as_string(const zan_json_value_t *val) {
    if (!val || val->type != ZAN_JSON_STRING) return "";
    return val->string_val.str;
}

double zan_json_as_number(const zan_json_value_t *val) {
    if (!val || val->type != ZAN_JSON_NUMBER) return 0.0;
    return val->number_val;
}

bool zan_json_as_bool(const zan_json_value_t *val) {
    if (!val || val->type != ZAN_JSON_BOOL) return false;
    return val->bool_val;
}

zan_json_value_t *zan_json_new_object(void) {
    zan_json_value_t *v = (zan_json_value_t *)calloc(1, sizeof(zan_json_value_t));
    v->type = ZAN_JSON_OBJECT;
    v->object_val.cap = 8;
    v->object_val.keys = (char **)malloc(sizeof(char *) * 8);
    v->object_val.values = (zan_json_value_t **)malloc(sizeof(zan_json_value_t *) * 8);
    return v;
}

zan_json_value_t *zan_json_new_array(void) {
    zan_json_value_t *v = (zan_json_value_t *)calloc(1, sizeof(zan_json_value_t));
    v->type = ZAN_JSON_ARRAY;
    v->array_val.cap = 8;
    v->array_val.items = (zan_json_value_t **)malloc(sizeof(zan_json_value_t *) * 8);
    return v;
}

zan_json_value_t *zan_json_new_string(const char *str) {
    zan_json_value_t *v = (zan_json_value_t *)calloc(1, sizeof(zan_json_value_t));
    v->type = ZAN_JSON_STRING;
    v->string_val.len = strlen(str);
    v->string_val.str = (char *)malloc(v->string_val.len + 1);
    memcpy(v->string_val.str, str, v->string_val.len + 1);
    return v;
}

zan_json_value_t *zan_json_new_number(double num) {
    zan_json_value_t *v = (zan_json_value_t *)calloc(1, sizeof(zan_json_value_t));
    v->type = ZAN_JSON_NUMBER; v->number_val = num;
    return v;
}

zan_json_value_t *zan_json_new_bool(bool val) {
    zan_json_value_t *v = (zan_json_value_t *)calloc(1, sizeof(zan_json_value_t));
    v->type = ZAN_JSON_BOOL; v->bool_val = val;
    return v;
}

zan_json_value_t *zan_json_new_null(void) {
    zan_json_value_t *v = (zan_json_value_t *)calloc(1, sizeof(zan_json_value_t));
    v->type = ZAN_JSON_NULL;
    return v;
}

void zan_json_object_set(zan_json_value_t *obj, const char *key, zan_json_value_t *val) {
    if (!obj || obj->type != ZAN_JSON_OBJECT) return;
    if (obj->object_val.count >= obj->object_val.cap) {
        obj->object_val.cap *= 2;
        obj->object_val.keys = (char **)realloc(obj->object_val.keys, sizeof(char *) * (size_t)obj->object_val.cap);
        obj->object_val.values = (zan_json_value_t **)realloc(obj->object_val.values, sizeof(zan_json_value_t *) * (size_t)obj->object_val.cap);
    }
    size_t klen = strlen(key);
    obj->object_val.keys[obj->object_val.count] = (char *)malloc(klen + 1);
    memcpy(obj->object_val.keys[obj->object_val.count], key, klen + 1);
    obj->object_val.values[obj->object_val.count] = val;
    obj->object_val.count++;
}

void zan_json_array_push(zan_json_value_t *arr, zan_json_value_t *val) {
    if (!arr || arr->type != ZAN_JSON_ARRAY) return;
    if (arr->array_val.count >= arr->array_val.cap) {
        arr->array_val.cap *= 2;
        arr->array_val.items = (zan_json_value_t **)realloc(arr->array_val.items, sizeof(zan_json_value_t *) * (size_t)arr->array_val.cap);
    }
    arr->array_val.items[arr->array_val.count++] = val;
}

void zan_json_free(zan_json_value_t *val) {
    if (!val) return;
    switch (val->type) {
    case ZAN_JSON_STRING: free(val->string_val.str); break;
    case ZAN_JSON_ARRAY:
        for (int i = 0; i < val->array_val.count; i++) zan_json_free(val->array_val.items[i]);
        free(val->array_val.items); break;
    case ZAN_JSON_OBJECT:
        for (int i = 0; i < val->object_val.count; i++) { free(val->object_val.keys[i]); zan_json_free(val->object_val.values[i]); }
        free(val->object_val.keys); free(val->object_val.values); break;
    default: break;
    }
    free(val);
}

/* ==== StringBuilder ==== */

void zan_sb_init(zan_string_builder_t *sb) {
    sb->cap = 256;
    sb->buf = (char *)malloc(sb->cap);
    sb->buf[0] = 0;
    sb->len = 0;
}

static void sb_ensure(zan_string_builder_t *sb, size_t extra) {
    if (sb->len + extra + 1 > sb->cap) {
        while (sb->len + extra + 1 > sb->cap) sb->cap *= 2;
        sb->buf = (char *)realloc(sb->buf, sb->cap);
    }
}

void zan_sb_append(zan_string_builder_t *sb, const char *str) {
    size_t slen = strlen(str);
    sb_ensure(sb, slen);
    memcpy(sb->buf + sb->len, str, slen);
    sb->len += slen;
    sb->buf[sb->len] = 0;
}

void zan_sb_append_char(zan_string_builder_t *sb, char c) {
    sb_ensure(sb, 1);
    sb->buf[sb->len++] = c;
    sb->buf[sb->len] = 0;
}

void zan_sb_append_int(zan_string_builder_t *sb, int64_t val) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%lld", (long long)val);
    zan_sb_append(sb, buf);
}

void zan_sb_append_double(zan_string_builder_t *sb, double val) {
    char buf[64];
    snprintf(buf, sizeof(buf), "%g", val);
    zan_sb_append(sb, buf);
}

char *zan_sb_to_string(zan_string_builder_t *sb) {
    char *result = sb->buf;
    sb->buf = NULL;
    sb->len = 0;
    sb->cap = 0;
    return result;
}

void zan_sb_clear(zan_string_builder_t *sb) {
    sb->len = 0;
    if (sb->buf) sb->buf[0] = 0;
}

void zan_sb_destroy(zan_string_builder_t *sb) {
    free(sb->buf);
    sb->buf = NULL;
    sb->len = 0;
    sb->cap = 0;
}
