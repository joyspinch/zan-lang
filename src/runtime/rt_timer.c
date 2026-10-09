/* 内部辅助实现 */
#if !defined(_WIN32) && !defined(__APPLE__)
#define _POSIX_C_SOURCE 200809L
#endif

/* 内部辅助实现 */
#if defined(_WIN32) && !defined(_WIN32_WINNT)
#define _WIN32_WINNT 0x0601
#endif

#include "rt_timer.h"

#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include "../common/host_oom.h"
#include "../common/zan_abi.h"

/* Persistent crash logging */
#if !defined(__wasm__) && !defined(ZAN_BARE_METAL)
#include "rt_crash.h"
#endif

#if defined(_WIN32)
#include <windows.h>
#include <shellapi.h>
typedef CRITICAL_SECTION zan_timer_mutex_t;
static INIT_ONCE g_lock_once = INIT_ONCE_STATIC_INIT;
static zan_timer_mutex_t g_lock;
static BOOL CALLBACK timer_lock_init(PINIT_ONCE once, PVOID param, PVOID *ctx) {
    (void)once; (void)param; (void)ctx;
    InitializeCriticalSection(&g_lock);
    return TRUE;
}
static void timer_lock(void) {
    InitOnceExecuteOnce(&g_lock_once, timer_lock_init, NULL, NULL);
    EnterCriticalSection(&g_lock);
}
static void timer_unlock(void) { LeaveCriticalSection(&g_lock); }
#elif defined(__wasm__)
/* 内部辅助逻辑 */
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
/* cloudlibc's time */
int clock_gettime(clockid_t, struct timespec *);
static void timer_lock(void) {}
static void timer_unlock(void) {}
#elif defined(ZAN_BARE_METAL)
/* 内部辅助实现 */
#include <time.h>
static void timer_lock(void) {}
static void timer_unlock(void) {}
#else
#include <pthread.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
typedef pthread_mutex_t zan_timer_mutex_t;
static zan_timer_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static void timer_lock(void) { pthread_mutex_lock(&g_lock); }
static void timer_unlock(void) { pthread_mutex_unlock(&g_lock); }
#endif

/* 内部辅助实现 */
#if defined(_WIN32)
static unsigned long timer_self_tid(void) { return GetCurrentThreadId(); }
static void timer_yield(void) { Sleep(1); }
#elif defined(__wasm__) || defined(ZAN_BARE_METAL)
static unsigned long timer_self_tid(void) { return 1; }
static void timer_yield(void) { (void)0; }
#else
#include <sched.h>
static unsigned long timer_self_tid(void) {
    return (unsigned long)(uintptr_t)pthread_self();
}
static void timer_yield(void) { sched_yield(); }
#endif
/* 内部辅助实现 */
int zan_utf8_argv(int *argc, char ***argv) {
#if defined(_WIN32)
    if (!argc || !argv) return 0;

    int wide_argc = 0;
    LPWSTR *wide_argv = CommandLineToArgvW(GetCommandLineW(), &wide_argc);
    if (!wide_argv || wide_argc < 1) return 0;

    char **utf8_argv = (char **)calloc((size_t)wide_argc + 1, sizeof(*utf8_argv));
    if (!utf8_argv) {
        LocalFree(wide_argv);
        return 0;
    }

    for (int i = 0; i < wide_argc; i++) {
        int bytes = WideCharToMultiByte(CP_UTF8, 0, wide_argv[i], -1,
                                        NULL, 0, NULL, NULL);
        if (bytes <= 0) {
            for (int j = 0; j < i; j++) free(utf8_argv[j]);
            free(utf8_argv);
            LocalFree(wide_argv);
            return 0;
        }
        utf8_argv[i] = (char *)malloc((size_t)bytes);
        if (!utf8_argv[i]) {
            for (int j = 0; j < i; j++) free(utf8_argv[j]);
            free(utf8_argv);
            LocalFree(wide_argv);
            return 0;
        }
        if (WideCharToMultiByte(CP_UTF8, 0, wide_argv[i], -1,
                                utf8_argv[i], bytes, NULL, NULL) != bytes) {
            free(utf8_argv[i]);
            for (int j = 0; j < i; j++) free(utf8_argv[j]);
            free(utf8_argv);
            LocalFree(wide_argv);
            return 0;
        }
    }

    LocalFree(wide_argv);
    *argc = wide_argc;
    *argv = utf8_argv;
    return 1;
#else
    (void)argc;
    (void)argv;
    return 0;
#endif
}

/* 内部辅助实现 */

#define ZAN_SOFT_MAX_SITES 1024

static char *g_soft_seen[ZAN_SOFT_MAX_SITES];
static int g_soft_seen_count;
/* 内部辅助实现 */
static int g_soft_strict;

static int zan_soft_is_hard(void) {
    /* 内部辅助实现 */
    timer_lock();
    static int hard = -1;
    if (hard < 0) {
        const char *env = getenv("ZAN_RT_HARD");
        hard = (env && *env && *env != '0') ? 1 : 0;
        if (!hard && !env) hard = g_soft_strict;
    }
    int v = hard;
    timer_unlock();
    return v;
}

int zan_rt_soft_is_hard(void) { return zan_soft_is_hard(); }

/* 内部辅助实现 */
void zan_rt_set_strict(void) { g_soft_strict = 1; }

/* Scratch substitute for a null base on the soft path (see rt_timer */
#define ZAN_SOFT_SCRATCH_RC  (UINT64_C(1) << 62)
#define ZAN_SOFT_SCRATCH_PAYLOAD 4096
static union {
    unsigned char bytes[40 + ZAN_SOFT_SCRATCH_PAYLOAD];
    /* 16-align the payload: object headers assume 16-byte-aligned rc slots */
    long double align_it;
} g_soft_scratch_store;
static unsigned char *g_soft_scratch;

static void store_u64_le(unsigned char *p, uint64_t v) {
    for (int i = 0; i < 8; i++) { p[i] = (unsigned char)(v >> (8 * i)); }
}

unsigned char *zan_rt_soft_scratch(void) {
    /* 内部辅助实现 */
    timer_lock();
    if (!g_soft_scratch) {
        unsigned char *hdr = g_soft_scratch_store.bytes;
        memset(hdr, 0, sizeof(g_soft_scratch_store.bytes));
        store_u64_le(hdr + 0,  ZAN_SOFT_SCRATCH_RC);    /* P-32: array rc */
        store_u64_le(hdr + 8,  ZAN_ARRAY_RC_MAGIC);     /* P-24: arr guard */
        store_u64_le(hdr + 16, ZAN_SOFT_SCRATCH_RC);    /* P-16: object rc */
        store_u64_le(hdr + 24, ZAN_ARRAY_MAGIC);        /* P-8:  discriminator */
        g_soft_scratch = hdr + 32;
    }
    unsigned char *scratch = g_soft_scratch;
    timer_unlock();
    return scratch;
}

/* 内部辅助逻辑 */
static int zan_soft_seen(const char *text) {
    for (int i = 0; i < g_soft_seen_count; i++)
        if (g_soft_seen[i] == text) return 1;
    if (g_soft_seen_count < ZAN_SOFT_MAX_SITES)
        g_soft_seen[g_soft_seen_count++] = (char *)text;
    return 0;
}

/* 内部辅助逻辑 */
static struct {
    const char *file;
    unsigned line;
    unsigned col;
} g_soft_seen3[ZAN_SOFT_MAX_SITES];
static int g_soft_seen3_count;

static int zan_soft_seen3(const char *file, unsigned line, unsigned col) {
    for (int i = 0; i < g_soft_seen3_count; i++)
        if (g_soft_seen3[i].file == file && g_soft_seen3[i].line == line &&
            g_soft_seen3[i].col == col) return 1;
    if (g_soft_seen3_count < ZAN_SOFT_MAX_SITES) {
        g_soft_seen3[g_soft_seen3_count].file = file;
        g_soft_seen3[g_soft_seen3_count].line = line;
        g_soft_seen3[g_soft_seen3_count].col = col;
        g_soft_seen3_count++;
    }
    return 0;
}

#if defined(_WIN32)
static void zan_soft_log_path(char *path, size_t cap) {
    DWORD n = GetModuleFileNameA(NULL, path, (DWORD)cap);
    if (n == 0 || n >= cap) { snprintf(path, cap, "zan_soft.log"); return; }
    char *slash = strrchr(path, '\\');
    if (!slash) { snprintf(path, cap, "zan_soft.log"); return; }
    slash[1] = '\0';
    strncat(path, "zan_crash.log", cap - strlen(path) - 1);
}
#else
static char g_soft_logdir[4096];

static void zan_soft_init_logdir(void) {
    const char *env = getenv("ZAN_LOG_DIR");
    if (env && *env && strlen(env) < sizeof(g_soft_logdir)) {
        memcpy(g_soft_logdir, env, strlen(env) + 1);
        return;
    }
    memcpy(g_soft_logdir, "logs", 5);
    ssize_t n = readlink("/proc/self/exe", g_soft_logdir,
                         sizeof(g_soft_logdir) - 6);
    if (n <= 0) return;
    g_soft_logdir[n] = '\0';
    char *slash = strrchr(g_soft_logdir, '/');
    if (!slash) { memcpy(g_soft_logdir, "logs", 5); return; }
    slash[1] = '\0';
    strncat(g_soft_logdir, "logs", sizeof(g_soft_logdir) - strlen(g_soft_logdir) - 1);
}

/* <logdir>/<YYYYMM>/<DD> */
static void zan_soft_log_path(char *path, size_t cap) {
    if (!g_soft_logdir[0]) zan_soft_init_logdir();
    time_t now = time(NULL);
    struct tm lt;
    if (!localtime_r(&now, &lt)) { snprintf(path, cap, "zan_soft.log"); return; }
    char month[16], day[16];
    strftime(month, sizeof month, "%Y%m", &lt);
    strftime(day, sizeof day, "%d", &lt);
    char dir[4096];
    if ((size_t)snprintf(dir, sizeof dir, "%s/%s", g_soft_logdir, month)
            >= sizeof dir) { snprintf(path, cap, "zan_soft.log"); return; }
    mkdir(g_soft_logdir, 0755);
    mkdir(dir, 0755);
    snprintf(path, cap, "%s/%s.log", dir, day);
}
#endif

static void zan_soft_append(const char *text) {
    char path[4096];
    zan_soft_log_path(path, sizeof path);
    FILE *f = fopen(path, "ab");
    if (!f) return;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    /* 内部辅助实现 */
    if (size == 0) {
#if defined(_WIN32)
        char exe[MAX_PATH];
        DWORD en = GetModuleFileNameA(NULL, exe, sizeof exe);
        if (en == 0 || en >= sizeof exe) snprintf(exe, sizeof exe, "<unknown>");
        fprintf(f, "==== ZAN RUNTIME (soft) ====\nexe=%s\n", exe);
#else
        char exe[4096];
        ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
        if (n > 0) exe[n] = '\0'; else snprintf(exe, sizeof exe, "<unknown>");
        fprintf(f, "==== ZAN RUNTIME (soft) ====\nexe=%s\n", exe);
#endif
    }
#if defined(_WIN32)
    SYSTEMTIME st;
    GetLocalTime(&st);
    fprintf(f, "%04d-%02d-%02d %02d:%02d:%02d.%03d soft runtime error: %s",
            st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
            st.wMilliseconds, text);
#else
    char stamp[32];
    time_t now = time(NULL);
    struct tm lt;
    localtime_r(&now, &lt);
    strftime(stamp, sizeof stamp, "%Y-%m-%d %H:%M:%S", &lt);
    fprintf(f, "%s soft runtime error: %s", stamp, text);
#endif
    fclose(f);
}

void zan_rt_soft_note(const char *text) {
    if (!text) return;
    timer_lock();
    int seen = zan_soft_seen(text);
    timer_unlock();
    if (seen) return;
    fprintf(stderr, "%s", text);
    fflush(stderr);
    zan_soft_append(text);
}

/* Two-part soft report */
void zan_rt_soft_note2(const char *prefix, const char *msg) {
    if (!prefix) return;
    char buf[1400];
    timer_lock();
    int seen = zan_soft_seen(prefix);
    if (!seen) {
        size_t n = strlen(prefix);
        if (n >= sizeof buf) n = sizeof buf - 1;
        memcpy(buf, prefix, n);
        if (msg) {
            size_t m = strlen(msg);
            if (n + m >= sizeof buf) m = sizeof buf - 1 - n;
            memcpy(buf + n, msg, m);
            n += m;
        }
        buf[n] = '\0';
    }
    timer_unlock();
    if (seen) return;
    fprintf(stderr, "%s", buf);
    fflush(stderr);
    zan_soft_append(buf);
}

/* 内部辅助逻辑 */
void zan_rt_soft_note3(const char *file, unsigned line, unsigned col,
                       const char *msg) {
    char buf[1400];
    int n = snprintf(buf, sizeof buf, "%s:%u:%u: runtime error: %s",
                     file ? file : "<unknown>", line, col,
                     msg ? msg : "");
    if (n <= 0) return;
    /* Reserve two bytes for the trailing "\n\0" pair */
    if ((size_t)n >= sizeof buf - 1) n = (int)sizeof buf - 2;
    buf[n] = '\n';
    buf[n + 1] = '\0';
    timer_lock();
    int seen = zan_soft_seen3(file, line, col);
    timer_unlock();
    if (seen) return;
    fprintf(stderr, "%s", buf);
    fflush(stderr);
    zan_soft_append(buf);
}

void zan_rt_guard_fail2(const char *prefix, const char *msg) {
    if (zan_soft_is_hard()) {
        char buf[1400];
        size_t n = prefix ? strlen(prefix) : 0;
        if (n >= sizeof buf) n = sizeof buf - 1;
        memcpy(buf, prefix ? prefix : "", n);
        if (msg) {
            size_t m = strlen(msg);
            if (n + m >= sizeof buf) m = sizeof buf - 1 - n;
            memcpy(buf + n, msg, m);
            n += m;
        }
        buf[n] = '\0';
        fprintf(stderr, "%s", buf);
        fflush(stderr);
#if defined(_WIN32)
        /* Raise the fault-message record so the crash filter appends it to zan_crash */
        void (WINAPI *raise)(DWORD, DWORD, DWORD, const ULONG_PTR *) =
            RaiseException;
        unsigned long code = 0xE0A2C010u; 
/* ZAN_RT_FAULT_MESSAGE (keep in sync with rt_crash */
        ULONG_PTR args[2] = { (ULONG_PTR)buf, 70 };
        raise(code, 0, 2, args);
#endif
        exit(70);
    }
    zan_rt_soft_note2(prefix, msg);
}

void zan_rt_guard_fail3(const char *file, unsigned line, unsigned col,
                        const char *msg) {
    if (zan_soft_is_hard()) {
#if defined(_WIN32)
        /* 内部辅助实现 */
        char buf[1400];
        snprintf(buf, sizeof buf, "%s:%u:%u: runtime error: %s",
                 file ? file : "<unknown>", line, col, msg ? msg : "");
        size_t n = strlen(buf);
        if (n && buf[n - 1] != '\n' && n + 1 < sizeof buf) {
            buf[n] = '\n';
            buf[n + 1] = '\0';
        }
        fprintf(stderr, "%s", buf);
        fflush(stderr);
        void (WINAPI *raise)(DWORD, DWORD, DWORD, const ULONG_PTR *) =
            RaiseException;
        unsigned long code = 0xE0A2C010u; 
/* ZAN_RT_FAULT_MESSAGE (keep in sync with rt_crash */
        ULONG_PTR args[2] = { (ULONG_PTR)buf, 70 };
        raise(code, 0, 2, args);
#endif
        exit(70);
    }
    zan_rt_soft_note3(file, line, col, msg);
}

/* ---- Fatal takeover (zan_rt_fatal) ---- */

static zan_fatal_fn g_fatal_handler;

void zan_rt_set_fatal_handler(zan_fatal_fn fn) { g_fatal_handler = fn; }

void zan_rt_fatal(const char *category, const char *message) {
    /* 内部辅助逻辑 */
    fprintf(stderr, "zan runtime: fatal (%s): %s\n",
            category ? category : "runtime", message ? message : "");
    fflush(stderr);
    zan_fatal_fn fn = g_fatal_handler;
    if (fn) fn(category, message);
    /* 内部辅助实现 */
    abort();
}
typedef enum zan_timer_kind {
    ZAN_TIMER_DELAY = 0,
    ZAN_TIMER_PUBLIC = 1
} zan_timer_kind;
typedef struct zan_timer_entry {
    /* 内部辅助逻辑 */
    long long due_us;
    long long id;
    long long interval;
    long long exec_msec;
    long long exec_count;
    long long round;
    unsigned long long sequence;
    zan_timer_kind kind;
    int removed;
    /* 内部辅助逻辑 */
    int waking;
    zan_timer_callback_t callback;
    void *frame;
    zan_timer_step_t step;
} zan_timer_entry;

static zan_timer_entry **g_heap;
static size_t g_heap_len;
static size_t g_heap_cap;
static long long g_next_id = 1;
static long long g_round;
static unsigned long long g_sequence;
static int g_initialized;
static void (*g_ready_hook)(void *frame, zan_timer_step_t step);
/* 内部辅助逻辑 */
static zan_timer_entry *g_dispatching;
/* 内部辅助逻辑 */
static unsigned long g_dispatch_tid;
/* Live (not removed) entries currently in the heap */
static volatile long long g_live;

/* 内部辅助实现 */
#if defined(ZAN_BARE_METAL)
__attribute__((weak))
#endif
long long zan_timer_now_ms(void) {
#if defined(_WIN32)
    return (long long)GetTickCount64();
#elif defined(CLOCK_MONOTONIC)
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
#else
    return 0;                  /* no monotonic clock: board overrides */
#endif
}

/* 内部辅助实现 */
long long zan_co_quantum_ms(void) {
    static long long q = -1;
    if (q >= 0) return q;
    long long v = 2;
    const char *e = getenv("ZAN_CO_QUANTUM_MS");
    if (e && *e) v = strtoll(e, NULL, 10);
    if (v < 0) v = 0;
    q = v;
    return q;
}

/* 内部辅助逻辑 */
long long zan_co_precise_us(void) {
#if defined(_WIN32)
    static long long freq;
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    if (!freq) {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        freq = f.QuadPart;
    }
    return (c.QuadPart / freq) * 1000000 +
           (c.QuadPart % freq) * 1000000 / freq;
#elif defined(CLOCK_MONOTONIC)
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
#else
    return zan_timer_now_ms() * 1000;   /* coarse boards: ms is the truth */
#endif
}

static int timer_less(const zan_timer_entry *a, const zan_timer_entry *b) {
    return a->due_us < b->due_us ||
           (a->due_us == b->due_us && a->sequence < b->sequence);
}

static void heap_swap(size_t a, size_t b) {
    zan_timer_entry *entry = g_heap[a];
    g_heap[a] = g_heap[b];
    g_heap[b] = entry;
}

/* Push onto the heap */
static int heap_push(zan_timer_entry *entry) {
    if (g_heap_len == g_heap_cap) {
        size_t cap = g_heap_cap ? g_heap_cap * 2 : 64;
        zan_timer_entry **heap = (zan_timer_entry **)realloc(g_heap, cap * sizeof(*heap));
        if (!heap) {
            entry->removed = 1;
            return -1;
        }
        g_heap = heap;
        g_heap_cap = cap;
    }
    if (!entry->removed) g_live++;
    size_t index = g_heap_len++;
    g_heap[index] = entry;
    while (index > 0) {
        size_t parent = (index - 1) / 2;
        if (!timer_less(entry, g_heap[parent])) break;
        heap_swap(index, parent);
        index = parent;
    }
    return 0;
}

static zan_timer_entry *heap_pop(void) {
    if (g_heap_len == 0) return NULL;
    zan_timer_entry *root = g_heap[0];
    if (!root->removed) g_live--;
    zan_timer_entry *last = g_heap[--g_heap_len];
    if (g_heap_len == 0) return root;
    g_heap[0] = last;
    size_t index = 0;
    for (;;) {
        size_t left = index * 2 + 1;
        if (left >= g_heap_len) break;
        size_t right = left + 1;
        size_t smallest = right < g_heap_len && timer_less(g_heap[right], g_heap[left])
            ? right : left;
        if (!timer_less(g_heap[smallest], g_heap[index])) break;
        heap_swap(index, smallest);
        index = smallest;
    }
    return root;
}

void zan_timer_set_ready_hook(void (*ready)(void *frame, zan_timer_step_t step)) {
    timer_lock();
    g_ready_hook = ready;
    timer_unlock();
}

void zan_timer_runtime_reset(void) {
    timer_lock();
    for (size_t i = 0; i < g_heap_len; i++) free(g_heap[i]);
    g_heap_len = 0;
    g_live = 0;
    g_next_id = 1;
    g_round = 0;
    g_sequence = 0;
    g_initialized = 1;
    timer_unlock();
}

/* Shared deadline arithmetic: saturate instead of wrapping */
long long zan_timer_saturating_due(long long now_ms, long long delay_ms) {
    if (delay_ms < 0) return now_ms;
    if (delay_ms > LLONG_MAX - now_ms) return LLONG_MAX;
    return now_ms + delay_ms;
}

/* 内部辅助实现 */
long long zan_timer_saturating_due_us(long long now_us, long long delay_us) {
    if (delay_us < 0) return now_us;
    if (delay_us > LLONG_MAX - now_us) return LLONG_MAX;
    return now_us + delay_us;
}

static long long delay_ms_to_us(long long ms) {
    if (ms < 0) return 0;
    if (ms > LLONG_MAX / 1000) return LLONG_MAX;
    return ms * 1000;
}

void zan_timer_delay(long long ms, void *frame, zan_timer_step_t step) {
    if (!step) return;
    zan_timer_entry *entry = (zan_timer_entry *)calloc(1, sizeof(*entry));
    if (!entry) return;   
/* 内部辅助逻辑 */
    entry->due_us = zan_timer_saturating_due_us(zan_co_precise_us(),
                                                               delay_ms_to_us(ms));
    entry->kind = ZAN_TIMER_DELAY;
    entry->frame = frame;
    entry->step = step;
    timer_lock();
    g_initialized = 1;
    entry->sequence = ++g_sequence;   /* shared counter: only touch under the lock */
    if (heap_push(entry) != 0) {
        /* Heap growth failed under load: drop the delay */
        timer_unlock();
        free(entry);   /* the entry never entered the heap */
        step(frame);
        return;
    }
    timer_unlock();
}

/* Cancel every pending DELAY entry naming `frame` */
int zan_timer_cancel_delay(void *frame) {
    int found = 0;
    if (!frame) return 0;
    timer_lock();
    for (size_t i = 0; i < g_heap_len; i++) {
        zan_timer_entry *entry = g_heap[i];
        if (entry->kind == ZAN_TIMER_DELAY && !entry->removed &&
            entry->frame == frame) {
            entry->removed = 1;
            g_live--;
            found++;
        }
    }
    if (g_dispatching && g_dispatching->kind == ZAN_TIMER_DELAY &&
        !g_dispatching->removed && g_dispatching->frame == frame) {
        g_dispatching->removed = 1;
        found++;
    }
    /* 内部辅助实现 */
    while (g_dispatching && g_dispatching->kind == ZAN_TIMER_DELAY &&
           g_dispatching->waking && g_dispatching->frame == frame &&
           g_dispatch_tid != timer_self_tid()) {
        timer_unlock();
        timer_yield();
        timer_lock();
    }
    /* 内部辅助逻辑 */
    size_t old_len = g_heap_len;
    size_t w = 0;
    for (size_t i = 0; i < g_heap_len; i++) {
        if (g_heap[i]->removed && g_heap[i]->kind == ZAN_TIMER_DELAY) {
            free(g_heap[i]);
        } else {
            g_heap[w++] = g_heap[i];
        }
    }
    g_heap_len = w;
    /* 内部辅助实现 */
    if (w != old_len && g_heap_len > 1) {
        for (size_t i = g_heap_len / 2; i-- > 0;) {
            size_t index = i;
            for (;;) {
                size_t left = index * 2 + 1;
                if (left >= g_heap_len) break;
                size_t right = left + 1;
                size_t smallest = right < g_heap_len &&
                                  timer_less(g_heap[right], g_heap[left])
                    ? right : left;
                if (!timer_less(g_heap[smallest], g_heap[index])) break;
                heap_swap(index, smallest);
                index = smallest;
            }
        }
    }
    timer_unlock();
    return found;
}

static long long timer_add(long long ms, zan_timer_callback_t callback, int repeat) {
    if (ms < 1 || !callback) return 0;
    zan_timer_entry *entry = (zan_timer_entry *)calloc(1, sizeof(*entry));
    if (!entry) return 0;   /* OOM: report failure as an invalid timer id. */
    /* 内部辅助实现 */
    entry->callback = callback;
    timer_lock();
    g_initialized = 1;
    entry->id = g_next_id++;
    if (entry->id <= 0) entry->id = g_next_id = 1;
    entry->interval = repeat ? ms : 0;
    entry->due_us = zan_timer_saturating_due_us(zan_co_precise_us(),
                                                               delay_ms_to_us(ms));
    entry->sequence = ++g_sequence;
    entry->kind = ZAN_TIMER_PUBLIC;
    if (heap_push(entry) != 0) {
        timer_unlock();
        free(entry);
        return 0;
    }
    timer_unlock();
    return entry->id;
}

long long zan_timer_tick(long long interval, zan_timer_callback_t callback) {
    return timer_add(interval, callback, 1);
}

long long zan_timer_after(long long delay, zan_timer_callback_t callback) {
    return timer_add(delay, callback, 0);
}

long long zan_timer_next_timeout(void) {
    long long timeout = -1;
    timer_lock();
    while (g_heap_len > 0 && g_heap[0]->removed) free(heap_pop());
    if (g_heap_len > 0) {
        /* 内部辅助逻辑 */
        long long rem_us = g_heap[0]->due_us - zan_co_precise_us();
        timeout = (rem_us <= 0) ? 0 : (rem_us + 999) / 1000;
    }
    timer_unlock();
    return timeout;
}

long long zan_timer_dispatch_due(void) {
    size_t dispatched = 0;
    for (;;) {
        long long now = zan_co_precise_us();
        timer_lock();
        while (g_heap_len > 0 && g_heap[0]->removed) free(heap_pop());
        if (g_heap_len == 0 || g_heap[0]->due_us > now) {
            timer_unlock();
            return dispatched;
        }
        zan_timer_entry *entry = heap_pop();
        if (entry->kind == ZAN_TIMER_PUBLIC) {
            entry->exec_count++;
            entry->round = ++g_round;
        }
        /* 内部辅助实现 */
        zan_timer_entry *prev = g_dispatching;
        g_dispatching = entry;
        timer_unlock();

        long long started_us = zan_co_precise_us();
        if (entry->kind == ZAN_TIMER_DELAY) {
            /* 内部辅助实现 */
            void (*ready)(void *, zan_timer_step_t);
            timer_lock();
            ready = g_ready_hook;
            int cancelled = entry->removed;
            if (!cancelled) {
                /* 内部辅助实现 */
                entry->waking = 1;
                g_dispatch_tid = timer_self_tid();
            }
            timer_unlock();
            if (!cancelled) {
                if (ready) ready(entry->frame, entry->step);
                else entry->step(entry->frame);
            }
        } else entry->callback();
        long long elapsed = zan_co_precise_us() - started_us;
        dispatched++;

        timer_lock();
        /* 内部辅助实现 */
        entry->waking = 0;
        g_dispatching = prev;
        if (entry->kind == ZAN_TIMER_PUBLIC) entry->exec_msec = elapsed / 1000;
        if (entry->kind == ZAN_TIMER_PUBLIC && entry->interval > 0 && !entry->removed) {
            long long cur_now = zan_co_precise_us();
            long long interval_us = delay_ms_to_us(entry->interval);
            entry->due_us = zan_timer_saturating_due_us(entry->due_us, interval_us);
            if (entry->due_us < cur_now) {
                /* 内部辅助实现 */
                if (cur_now - entry->due_us > interval_us) {
                    long long lag = cur_now - entry->due_us;
                    long long skips = lag / interval_us;
                    entry->due_us = zan_timer_saturating_due_us(entry->due_us, skips * interval_us);
                    if (entry->due_us <= cur_now) {
                        entry->due_us = zan_timer_saturating_due_us(entry->due_us, interval_us);
                    }
                }
            }
            entry->sequence = ++g_sequence;
            if (heap_push(entry) != 0) {
                /* 内部辅助逻辑 */
                timer_unlock();
                free(entry);
                continue;
            }
            timer_unlock();
        } else {
            timer_unlock();
            free(entry);
        }
    }
}

long long zan_timer_pending(void) {
    /* 内部辅助实现 */
    return __atomic_load_n(&g_live, __ATOMIC_RELAXED);
}

int zan_timer_clear(long long id) {
    int found = 0;
    timer_lock();
    for (size_t i = 0; i < g_heap_len; i++) {
        zan_timer_entry *entry = g_heap[i];
        if (entry->kind == ZAN_TIMER_PUBLIC && entry->id == id && !entry->removed) {
            entry->removed = 1;
            g_live--;
            found = 1;
            break;
        }
    }
    /* 内部辅助实现 */
    if (!found && g_dispatching && g_dispatching->kind == ZAN_TIMER_PUBLIC &&
        g_dispatching->id == id && !g_dispatching->removed) {
        g_dispatching->removed = 1;
        found = 1;
    }
    timer_unlock();
    return found;
}

long long zan_timer_clear_all(void) {
    long long count = 0;
    timer_lock();
    for (size_t i = 0; i < g_heap_len; i++) {
        zan_timer_entry *entry = g_heap[i];
        if (entry->kind == ZAN_TIMER_PUBLIC && !entry->removed) {
            entry->removed = 1;
            g_live--;
            count++;
        }
    }
    /* 内部辅助实现 */
    if (g_dispatching && g_dispatching->kind == ZAN_TIMER_PUBLIC &&
        !g_dispatching->removed) {
        g_dispatching->removed = 1;
        count++;
    }
    timer_unlock();
    return count;
}

int zan_timer_info(long long id, long long *exec_msec, long long *exec_count,
                   long long *interval, long long *round, int *removed) {
    int found = 0;
    timer_lock();
    for (size_t i = 0; i < g_heap_len; i++) {
        zan_timer_entry *entry = g_heap[i];
        if (entry->kind == ZAN_TIMER_PUBLIC && entry->id == id) {
            if (exec_msec) *exec_msec = entry->exec_msec;
            if (exec_count) *exec_count = entry->exec_count;
            if (interval) *interval = entry->interval;
            if (round) *round = entry->round;
            if (removed) *removed = entry->removed;
            found = 1;
            break;
        }
    }
    timer_unlock();
    return found;
}

long long zan_timer_list_count(void) {
    long long count = 0;
    timer_lock();
    for (size_t i = 0; i < g_heap_len; i++)
        if (g_heap[i]->kind == ZAN_TIMER_PUBLIC && !g_heap[i]->removed) count++;
    timer_unlock();
    return count;
}

long long zan_timer_list_at(long long index) {
    long long current = 0;
    long long id = 0;
    timer_lock();
    for (size_t i = 0; i < g_heap_len; i++) {
        zan_timer_entry *entry = g_heap[i];
        if (entry->kind != ZAN_TIMER_PUBLIC || entry->removed) continue;
        if (current++ == index) { id = entry->id; break; }
    }
    timer_unlock();
    return id;
}

void zan_timer_stats(long long *initialized, long long *num, long long *round) {
    /* 内部辅助实现 */
    timer_lock();
    if (initialized) *initialized = g_initialized;
    if (num) {
        size_t count = 0;
        for (size_t i = 0; i < g_heap_len; i++)
            if (g_heap[i]->kind == ZAN_TIMER_PUBLIC && !g_heap[i]->removed) count++;
        *num = (long long)count;
    }
    if (round) *round = g_round;
    timer_unlock();
}

/* ---- registry of live detached (Task */

/* Tombstone: a slot whose frame was removed */
#define ZAN_LIVE_DEAD ((void *)(uintptr_t)1)

static void  **g_colive_slots;
static size_t   g_colive_cap;    /* power of two, 0 until first insert */
static size_t   g_colive_live;   /* occupied slots */
static size_t   g_colive_dead;   /* tombstones */

static volatile int g_colive_lock;

/* Bounded TTAS backoff: pause-spin a few rounds, then hand the core back */
static void zan_lock_backoff(int spins) {
    if (spins < 64) {
#if defined(__i386__) || defined(__x86_64__)
        __builtin_ia32_pause();
#endif
        return;
    }
#if defined(_WIN32)
    SwitchToThread();
#elif defined(__wasm__) || defined(ZAN_BARE_METAL)
    /* single-threaded: the holder cannot be preempted, no yield exists */
#else
    sched_yield();
#endif
}

static void live_lock(void) {
    for (int spins = 0;; spins++) {
        if (!__sync_lock_test_and_set(&g_colive_lock, 1)) return;
        zan_lock_backoff(spins);
    }
}

static void live_unlock(void) { __sync_lock_release(&g_colive_lock); }

/* 内部辅助实现 */
static size_t live_hash(void *p) {
    uint64_t x = (uint64_t)(uintptr_t)p;
    x ^= x >> 33; x *= 0xff51afd7ed558ccdULL;
    x ^= x >> 33; x *= 0xc4ceb9fe1a85ec53ULL;
    x ^= x >> 33;
    return (size_t)x;
}

/* Grow (or compact, when tombstones are what filled the table) to `ncap` */
static void live_rehash(size_t ncap) {
    void **old = g_colive_slots;
    size_t ocap = g_colive_cap;
    void **ns = (void **)calloc(ncap, sizeof(*ns));
    if (!ns) zan_rt_fatal("oom", "timer: coroutine-live table grow failed");
    g_colive_slots = ns;
    g_colive_cap = ncap;
    g_colive_dead = 0;
    g_colive_live = 0;
    for (size_t i = 0; i < ocap; i++) {
        void *f = old[i];
        if (!f || f == ZAN_LIVE_DEAD) continue;
        size_t j = live_hash(f) & (ncap - 1);
        while (ns[j]) j = (j + 1) & (ncap - 1);
        ns[j] = f;
        g_colive_live++;
    }
    free(old);
}

void zan_co_live_add(void *frame) {
    if (!frame) return;
    live_lock();
    /* 内部辅助实现 */
    if ((g_colive_live + g_colive_dead + 1) * 4 > g_colive_cap * 3)
        live_rehash(g_colive_cap ? (g_colive_live * 4 > g_colive_cap ? g_colive_cap * 2 : g_colive_cap) : 64);
    size_t mask = g_colive_cap - 1;
    size_t i = live_hash(frame) & mask;
    size_t reuse = (size_t)-1;
    for (;;) {
        void *cur = g_colive_slots[i];
        if (!cur) break;
        if (cur == frame) { live_unlock(); return; }
        if (cur == ZAN_LIVE_DEAD && reuse == (size_t)-1) reuse = i;
        i = (i + 1) & mask;
    }
    if (reuse != (size_t)-1) { i = reuse; g_colive_dead--; }
    g_colive_slots[i] = frame;
    g_colive_live++;
    live_unlock();
}

static void join_on_untrack(void *frame, void **out_joiner,
                            zan_timer_step_t *out_step);

void zan_co_live_del(void *frame) {
    if (!frame || !g_colive_cap) return;
    /* 内部辅助实现 */
    void *fire_joiner = NULL;
    zan_timer_step_t fire_step = NULL;
    live_lock();
    join_on_untrack(frame, &fire_joiner, &fire_step);
    size_t mask = g_colive_cap - 1;
    size_t i = live_hash(frame) & mask;
    for (;;) {
        void *cur = g_colive_slots[i];
        if (!cur) break;
        if (cur == frame) {
            g_colive_slots[i] = ZAN_LIVE_DEAD;
            g_colive_live--;
            g_colive_dead++;
            break;
        }
        i = (i + 1) & mask;
    }
    live_unlock();
    void (*_hook)(void *, zan_timer_step_t) = g_ready_hook;
    if (fire_joiner && fire_step && _hook) _hook(fire_joiner, fire_step);
}

int zan_co_live_count(void) {
    if (!g_colive_cap) return 0;
    live_lock();
    int n = (int)g_colive_live;
    live_unlock();
    return n;
}

/* 内部辅助逻辑 */
static int live_has_nolock(void *frame) {
    size_t mask = g_colive_cap - 1;
    size_t i = live_hash(frame) & mask;
    for (;;) {
        void *cur = g_colive_slots[i];
        if (!cur) return 0;
        if (cur == frame) return 1;
        i = (i + 1) & mask;
    }
}

int zan_co_live_has(void *frame) {
    if (!frame || !g_colive_cap) return 0;
    live_lock();
    int found = live_has_nolock(frame);
    live_unlock();
    return found;
}

void zan_co_live_reset(void) {
    live_lock();
    free(g_colive_slots);
    g_colive_slots = NULL;
    g_colive_cap = g_colive_live = g_colive_dead = 0;
    live_unlock();
}

/* ---- event-driven join (Task */

#define JOIN_OFF_DONE (12 + (int)sizeof(void *)) /* co_header: done field */
/* 内部辅助实现 */
typedef struct zan_co_header_probe {
    long long sched;                /* ASYNC_FRAME_SCHED (i64) */
    void (*sched_step)(void *);     /* ASYNC_FRAME_SCHED_STEP (ptr) */
    int state;                      /* ASYNC_FRAME_STATE (i32) */
    int done;                       /* ASYNC_FRAME_DONE (i32) */
} zan_co_header_probe;
_Static_assert(offsetof(zan_co_header_probe, done) == JOIN_OFF_DONE,
               "JOIN_OFF_DONE drifts from the emitter's ASYNC_FRAME_DONE "
               "(see irgen_expr_core.c)");
_Static_assert(offsetof(zan_co_header_probe, state) == JOIN_OFF_DONE - (int)sizeof(int),
               "co header state/done pair out of order vs JOIN_OFF_DONE");

typedef struct zan_join_pair {
    void            *frame;
    struct zan_join *owner;
    int              idx;
    int              done;    /* set by the untrack hook */
} zan_join_pair_t;

typedef struct zan_join {
    int              any;     /* fire on first completion (WhenAny) */
    int              fired;   /* exactly one fire per entry */
    void            *joiner;  /* suspended WhenAll/WhenAny frame */
    zan_timer_step_t joiner_step;
    int              npairs;  /* bound pairs, filled during the bind phase */
    int              winner;  /* any mode: first completed pair's index */
    /* Bound pairs not yet marked done */
    int              remaining;
    int              capacity; /* elements allocated in pairs[] */
    zan_join_pair_t  pairs[]; /* flexible array, one allocation */
} zan_join_t;

static zan_join_pair_t **g_joinmap_slots;
static size_t   g_joinmap_cap;   /* power of two, 0 until first insert */
static size_t   g_joinmap_live;
static size_t   g_joinmap_dead;
#define JOINMAP_TOMB ((zan_join_pair_t *)(uintptr_t)1)

static void joinmap_rehash(size_t ncap) {
    zan_join_pair_t **nslots = calloc(ncap, sizeof(*nslots));
    if (!nslots) { zan_rt_fatal("oom", "joinmap: rehash failed"); return; }
    size_t nmask = ncap - 1;
    for (size_t i = 0; i < g_joinmap_cap; i++) {
        zan_join_pair_t *cur = g_joinmap_slots[i];
        if (!cur || cur == JOINMAP_TOMB) continue;
        size_t j = live_hash(cur->frame) & nmask;
        while (nslots[j]) j = (j + 1) & nmask;
        nslots[j] = cur;
    }
    free(g_joinmap_slots);
    g_joinmap_slots = nslots;
    g_joinmap_cap = ncap;
    g_joinmap_dead = 0;
}

static void joinmap_put(zan_join_pair_t *pr) {
    /* 内部辅助实现 */
    if ((g_joinmap_live + g_joinmap_dead + 1) * 4 >= g_joinmap_cap * 3)
        joinmap_rehash(g_joinmap_cap
                           ? (g_joinmap_live * 4 > g_joinmap_cap ? g_joinmap_cap * 2
                                                                 : g_joinmap_cap)
                           : 64);
    size_t mask = g_joinmap_cap - 1;
    size_t i = live_hash(pr->frame) & mask;
    for (;;) {
        zan_join_pair_t *cur = g_joinmap_slots[i];
        if (!cur || cur == JOINMAP_TOMB) {
            if (cur == JOINMAP_TOMB) g_joinmap_dead--;
            g_joinmap_slots[i] = pr;
            g_joinmap_live++;
            return;
        }
        if (cur != JOINMAP_TOMB && cur->frame == pr->frame) return; /* already bound */
        i = (i + 1) & mask;
    }
}

static zan_join_pair_t *joinmap_get(void *frame) {
    if (!g_joinmap_cap) return NULL;
    size_t mask = g_joinmap_cap - 1;
    size_t i = live_hash(frame) & mask;
    for (;;) {
        zan_join_pair_t *cur = g_joinmap_slots[i];
        if (!cur) return NULL;
        if (cur != JOINMAP_TOMB && cur->frame == frame) return cur;
        i = (i + 1) & mask;
    }
}

/* 内部辅助实现 */
static void joinmap_remove(void *frame, zan_join_pair_t *pr) {
    if (!g_joinmap_cap) return;
    size_t mask = g_joinmap_cap - 1;
    size_t i = live_hash(frame) & mask;
    for (;;) {
        zan_join_pair_t *cur = g_joinmap_slots[i];
        if (!cur) return;
        if (cur == pr) {
            if (cur->frame != frame) return; /* recycled: someone else owns it now */
            g_joinmap_slots[i] = JOINMAP_TOMB;
            g_joinmap_live--;
            g_joinmap_dead++;
            return;
        }
        i = (i + 1) & mask;
    }
}

static int join_pair_done(const zan_join_pair_t *pr) {
    if (pr->done) return 1;
    if (!live_has_nolock(pr->frame)) return 1;   /* untracked: completed */
    /* The emitter publishes DONE with a release xchg (irgen_async */
    int32_t done = __atomic_load_n(
        (const volatile int32_t *)((const unsigned char *)pr->frame + JOIN_OFF_DONE),
        __ATOMIC_ACQUIRE);
    return done != 0;
}

static int join_any_done(zan_join_t *j) {
    /* 内部辅助实现 */
    if (j->npairs == 0) return 1;
    for (int i = 0; i < j->npairs; i++)
        if (join_pair_done(&j->pairs[i])) { j->winner = j->pairs[i].idx; return 1; }
    return 0;
}

static void join_fire_locked(zan_join_t *j, void **out_joiner, zan_timer_step_t *out_step) {
    *out_joiner = NULL;
    if (j->fired) return;
    /* 内部辅助实现 */
    int trig = j->any ? join_any_done(j) : (j->remaining == 0);
    if (!trig) return;
    j->fired = 1;
    *out_joiner = j->joiner;
    *out_step = j->joiner_step;
}

/* untrack hook: frame just completed and is leaving the live registry */
static void join_on_untrack(void *frame, void **out_joiner, zan_timer_step_t *out_step) {
    *out_joiner = NULL;
    zan_join_pair_t *pr = joinmap_get(frame);
    if (!pr) return;
    pr->done = 1;
    pr->owner->remaining--;
    joinmap_remove(frame, pr);
    join_fire_locked(pr->owner, out_joiner, out_step);
}

/* 内部辅助逻辑 */
static int zan_sched_trace(void) {
    static volatile int t = -1;
    int cur = __atomic_load_n(&t, __ATOMIC_RELAXED);
    if (cur < 0) {
        const char *e = getenv("ZAN_SCHED_TRACE");
        cur = (e && *e && *e != '0') ? 1 : 0;
        __atomic_store_n(&t, cur, __ATOMIC_RELAXED);
    }
    return cur;
}

void zan_join_complete(void *frame) {
    if (!frame || !g_joinmap_cap) return;
    void *fire_joiner = NULL;
    zan_timer_step_t fire_step = NULL;
    int rem_dbg = -1, np_dbg = -1, any_dbg = -1, fired_dbg = 0;
    live_lock();
    zan_join_pair_t *pr = joinmap_get(frame);
    if (pr && !pr->done) {
        pr->done = 1;
        pr->owner->remaining--;
        rem_dbg = pr->owner->remaining;
        np_dbg = pr->owner->npairs;
        any_dbg = pr->owner->any;
        joinmap_remove(frame, pr);
        join_fire_locked(pr->owner, &fire_joiner, &fire_step);
        fired_dbg = (fire_joiner && fire_step) ? 1 : 0;
    }
    live_unlock();
    if (zan_sched_trace())
        fprintf(stderr, "[st] joincmp frame=%p rem=%d np=%d any=%d fired=%d\n",
                frame, rem_dbg, np_dbg, any_dbg, fired_dbg);
    void (*_hook)(void *, zan_timer_step_t) = g_ready_hook;
    if (fire_joiner && fire_step && _hook) _hook(fire_joiner, fire_step);
}

long long zan_join_new(int npairs, int any) {
    size_t sz = sizeof(zan_join_t) + (size_t)npairs * sizeof(zan_join_pair_t);
    zan_join_t *j = malloc(sz);
    if (!j) zan_rt_fatal("oom", "join: entry alloc failed");
    memset(j, 0, sz);
    j->any = (any != 0);
    j->capacity = npairs;
    return (long long)(intptr_t)j;
}

/* bind one handle to the join */
int zan_join_bind(long long entry, void *frame, int idx) {
    zan_join_t *j = (zan_join_t *)(intptr_t)entry;
    if (!j || !frame) return 0;
    int bound = 0;
    live_lock();
    if (live_has_nolock(frame) && !joinmap_get(frame)) {
        /* 内部辅助实现 */
        int32_t done = __atomic_load_n(
            (const volatile int32_t *)((const unsigned char *)frame + JOIN_OFF_DONE),
            __ATOMIC_ACQUIRE);
        if (!done) {
            if (j->npairs >= j->capacity) { live_unlock(); return 0; }
            zan_join_pair_t *pr = &j->pairs[j->npairs];
            pr->frame = frame;
            pr->owner = j;
            pr->idx = idx;
            pr->done = 0;
            j->npairs++;
            j->remaining++;
            joinmap_put(pr);
            bound = 1;
        }
    }
    live_unlock();
    return bound;
}

/* suspend the caller until the join fires */
int zan_join_wait2(long long entry, void *frame, zan_timer_step_t step) {
    zan_join_t *j = (zan_join_t *)(intptr_t)entry;
    if (!j || !g_ready_hook || !frame || !step) return 2;
    int satisfied;
    live_lock();
    satisfied = j->fired || (j->any ? join_any_done(j) : j->remaining == 0);
    if (!satisfied) { j->joiner = frame; j->joiner_step = step; }
    live_unlock();
    return satisfied ? 1 : 0;
}

void zan_join_cancel(long long entry) {
    zan_join_t *j = (zan_join_t *)(intptr_t)entry;
    if (!j) return;
    live_lock();
    for (int i = 0; i < j->npairs; i++)
        joinmap_remove(j->pairs[i].frame, &j->pairs[i]);
    live_unlock();
    free(j);
}

/* 内部辅助逻辑 */
static volatile int g_cfg_workers   = 0;    /* 0  = unset (CPU count) */
static volatile int g_cfg_io_shards = 0;    /* 0  = unset (one per worker) */
static volatile int g_cfg_sync_fast = -1;   /* -1 = unset */

void zan_async_set_workers(int32_t n)    { __atomic_store_n(&g_cfg_workers, (n > 0) ? (int)n : 0, __ATOMIC_RELEASE); }
void zan_async_set_io_shards(int32_t n)  { __atomic_store_n(&g_cfg_io_shards, (n > 0) ? (int)n : 0, __ATOMIC_RELEASE); }
void zan_async_set_sync_fast(int32_t on) { __atomic_store_n(&g_cfg_sync_fast, on ? 1 : 0, __ATOMIC_RELEASE); }

int32_t zan_async_cfg_workers(void)   { return (int32_t)__atomic_load_n(&g_cfg_workers, __ATOMIC_ACQUIRE); }
int32_t zan_async_cfg_io_shards(void) { return (int32_t)__atomic_load_n(&g_cfg_io_shards, __ATOMIC_ACQUIRE); }
int32_t zan_async_cfg_sync_fast(void) { return (int32_t)__atomic_load_n(&g_cfg_sync_fast, __ATOMIC_ACQUIRE); }

/* 内部辅助实现 */
void zan_rt_dbl_str(char *buf, unsigned long long cap, double v) {
    if (v != v) { snprintf(buf, (size_t)cap, "NaN"); return; }
    if (v == 0.0) { snprintf(buf, (size_t)cap, "0"); return; }
    const char *sign = (v < 0.0) ? "-" : "";
    double a = (v < 0.0) ? -v : v;
    if (a > 1.7976931348623157e308) {
        snprintf(buf, (size_t)cap, "%sInfinity", sign);
        return;
    }
    /* shortest digit search: the smallest precision whose % */
    char m[40];
    int p;
    for (p = 1; p < 17; p++) {
        snprintf(m, sizeof m, "%.*e", p - 1, v);
        if (strtod(m, NULL) == v) break;
    }
    if (p == 17) snprintf(m, sizeof m, "%.16e", v);

    /* split "-d */
    char *q = (*m == '-') ? m + 1 : m;
    char *es = strchr(q, 'e');
    int e10 = atoi(es + 1);
    char sig[24];
    int n = 0;
    for (char *c = q; c < es; c++)
        if (*c >= '0' && *c <= '9') sig[n++] = *c;
    while (n > 1 && sig[n - 1] == '0') n--;
    sig[n] = 0;

    char out[48];
    if (e10 >= -4 && e10 <= 14) {
        if (e10 >= n) {
            /* integral value: the digits plus e10-(n-1) trailing zeros */
            char zeros[24];
            int z = e10 - (n - 1);
            memset(zeros, '0', (size_t)z);
            zeros[z] = 0;
            snprintf(out, sizeof out, "%s%s%s", sign, sig, zeros);
    } else if (e10 >= 0) {
        /* point inside the digit run: head */
        int h = e10 + 1;
        if (sig[h])
            snprintf(out, sizeof out, "%s%.*s.%s", sign, h, sig, sig + h);
        else
            snprintf(out, sizeof out, "%s%.*s", sign, h, sig);
    } else {
            /* |v| < 1: 0. + (-e10-1) zeros + digits */
            char zeros[8];
            int z = -e10 - 1;
            memset(zeros, '0', (size_t)z);
            zeros[z] = 0;
            snprintf(out, sizeof out, "%s0.%s%s", sign, zeros, sig);
        }
    } else {
        /* scientific: d[ */
        if (sig[1])
            snprintf(out, sizeof out, "%s%c.%sE%+03d", sign, sig[0], sig + 1,
                     e10);
        else
            snprintf(out, sizeof out, "%s%sE%+03d", sign, sig, e10);
    }
    snprintf(buf, (size_t)cap, "%s", out);
}

/* 内部辅助实现 */
double zan_rt_dbl_parse(const char *s, char **endp) {
    if (endp) *endp = (char *)s;
    if (strcmp(s, "NaN") == 0) {
        if (endp) *endp = (char *)s + 3;
        return (double)NAN;
    }
    if (strcmp(s, "Infinity") == 0 || strcmp(s, "+Infinity") == 0) {
        if (endp) *endp = (char *)s + (s[0] == '+' ? 9 : 8);
        return (double)INFINITY;
    }
    if (strcmp(s, "-Infinity") == 0) {
        if (endp) *endp = (char *)s + 9;
        return -(double)INFINITY;
    }
    return strtod(s, endp);
}

/* Zan Hardware Acceleration Engine & Cryptographic / SIMD Drivers */
#include "rt_hw_accel.c"

