/* clock_gettime is POSIX, not ISO C: a strict -std=c11 build (which is how the
 * timer object is compiled, natively and for every cross target) hides it
 * behind this feature macro, so ask for it before any header is pulled in.
 * Darwin exposes it unconditionally and narrows other APIs when asked for
 * strict POSIX, so leave it alone there. */
#if !defined(_WIN32) && !defined(__APPLE__)
#define _POSIX_C_SOURCE 200809L
#endif

/* GetTickCount64 needs Vista+ headers; without this the strict -std=c11 MinGW
 * build only gets an implicit declaration and truncates the 64-bit tick count
 * to int. */
#if defined(_WIN32) && !defined(_WIN32_WINNT)
#define _WIN32_WINNT 0x0601
#endif

#include "rt_timer.h"

#include <stdlib.h>
#include <string.h>
#include "../common/host_oom.h"

/* Persistent crash logging. This object is linked into every program zanc
 * builds (its coroutine driver calls zan_timer_*, see src/compiler/main.c), so
 * including the logger here is what gives a plain console or GUI program the
 * same crash record the async reactor and the GUI runtime already installed --
 * before this, a program that used neither died without a trace. Every symbol
 * in the header is static and installation is idempotent, so the runtimes that
 * also include it stay correct. The wasm sysroot has no signals or process
 * paths, so it keeps the previous behaviour. */
#if !defined(__wasm__)
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
/* Single-threaded wasm (WASI): the sysroot has no pthreads, and none are
 * needed -- one thread cannot race the timer heap. */
#include <time.h>
/* cloudlibc's time.h defines CLOCK_MONOTONIC (as a clockid) but does not
 * declare clock_gettime; declare it -- libc.a provides the implementation
 * as a WASI host-import wrapper. */
int clock_gettime(clockid_t, struct timespec *);
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
/* The Windows C runtime's argv is decoded with the process ANSI code page,
 * whereas Zan strings and the Windows file APIs use UTF-8. Reparse the
 * original Unicode command line and retain the converted vector for process
 * lifetime, matching the CRT argv lifetime. This object is linked into every
 * native executable, so the compiler can use the helper without a new optional
 * runtime dependency. */
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


/* ---- Fail-soft fault reports (zan_rt_soft_note) ----
 *
 * The compiler's runtime guards used to have one outcome: print and exit(70).
 * That turns a latent null reference in a long-running service into a crash
 * loop at 3am: the process dies, the supervisor restarts it, it hits the same
 * guard and dies again. The soft path instead appends the message to the same
 * dated log the crash handler already writes (POSIX: <exe_dir>/logs/YYYYMM/
 * DD.log, Windows: <exe_dir>\zan_crash.log), prints it on stderr once, and
 * lets the caller continue with a default value; the dedup table keeps a
 * hot loop that trips the same site from flooding the disk. ZAN_RT_HARD=1
 * restores the historical hard exit (the compiler test-suite relies on the
 * exit status to pin the guard behavior).
 *
 * Callers run on the guarded thread, not inside a signal handler, so this may
 * use stdio and the environment freely -- but it must never itself abort:
 * every allocation/open failure silently degrades to "no log entry". */

#define ZAN_SOFT_MAX_SITES 256

static char *g_soft_seen[ZAN_SOFT_MAX_SITES];
static int g_soft_seen_count;

static int zan_soft_is_hard(void) {
    static int hard = -1;
    if (hard < 0) {
        const char *env = getenv("ZAN_RT_HARD");
        hard = (env && *env && *env != '0') ? 1 : 0;
    }
    return hard;
}

int zan_rt_soft_is_hard(void) { return zan_soft_is_hard(); }

/* Scratch substitute for a null base on the soft path (see rt_timer.h). */
static unsigned char g_soft_scratch[256];

unsigned char *zan_rt_soft_scratch(void) { return g_soft_scratch; }

/* One message per site per process: the same null field hit in a loop would
 * otherwise append an entry per iteration. Pointer identity of the compiler-
 * emitted global string is the site identity. */
static int zan_soft_seen(const char *text) {
    for (int i = 0; i < g_soft_seen_count; i++)
        if (g_soft_seen[i] == text) return 1;
    if (g_soft_seen_count < ZAN_SOFT_MAX_SITES)
        g_soft_seen[g_soft_seen_count++] = (char *)text;
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

/* <logdir>/<YYYYMM>/<DD>.log -- the same file the crash handler records into,
 * so a soft report sits between the crash records the operator already reads.
 * Runs outside a signal handler, so localtime_r is fine here. */
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
    /* Header line once per file (best effort): a fresh log starts with the
     * exe so a lone soft line is attributable even if the process exits
     * before anything else logs. */
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

/* Two-part soft report. `prefix` is per-site ("Gui/App.zan:818:40: runtime
 * error: "), `msg` is one of a few hundred shared templates. Site identity
 * (and therefore the dedup key) is the prefix pointer: every guard site
 * emits exactly one prefix global, mirroring how zan_rt_soft_note keys on
 * the whole-text pointer. Composition happens under the same lock that
 * guards the dedup table, into a buffer long enough for any prefix+msg the
 * emitter produces (the compiler caps each part at 640 bytes). */
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

void zan_rt_guard_fail2(const char *prefix, const char *msg) {
    if (zan_soft_is_hard()) {
#if defined(_WIN32)
        /* Same contract as the old inline hard path in generated code: print
         * prefix+msg, then raise the fault-message record so the crash filter
         * appends it to zan_crash.log. The filter resumes this thread
         * (CONTINUE_EXECUTION for 0xE0A2C010), so the exit below still runs --
         * same exit status, same atexit reports. */
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
        void (WINAPI *raise)(DWORD, DWORD, DWORD, const ULONG_PTR *) =
            RaiseException;
        unsigned long code = 0xE0A2C010u; /* ZAN_RT_FAULT_MESSAGE (keep in sync
                                             with rt_crash.h / irgen_generics.c) */
        ULONG_PTR args[2] = { (ULONG_PTR)buf, 70 };
        raise(code, 0, 2, args);
#endif
        exit(70);
    }
    zan_rt_soft_note2(prefix, msg);
}
typedef enum zan_timer_kind {
    ZAN_TIMER_DELAY = 0,
    ZAN_TIMER_PUBLIC = 1
} zan_timer_kind;
typedef struct zan_timer_entry {
    long long due_ms;
    long long id;
    long long interval;
    long long exec_msec;
    long long exec_count;
    long long round;
    unsigned long long sequence;
    zan_timer_kind kind;
    int removed;
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
/* Entry whose callback is currently running (dispatch popped it from the heap
 * and is executing it outside the lock). zan_timer_clear must reach it too:
 * without this, a tick callback could not clear itself -- the entry is no
 * longer in the heap, so the usual scan would miss it and the timer would
 * reschedule forever. Read/written under the lock. */
static zan_timer_entry *g_dispatching;
/* Live (not removed) entries currently in the heap. Maintained on every push /
 * pop / removal so zan_timer_pending() is an O(1) unlocked read: the
 * multi-worker driver calls it once per scheduler loop iteration on every
 * worker, and the old "take the global lock and walk the whole heap" version
 * made the timer lock the pool's hottest contention point. */
static volatile long long g_live;

long long zan_timer_now_ms(void) {
#if defined(_WIN32)
    return (long long)GetTickCount64();
#elif defined(__APPLE__)
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
#endif
}

static int timer_less(const zan_timer_entry *a, const zan_timer_entry *b) {
    return a->due_ms < b->due_ms ||
           (a->due_ms == b->due_ms && a->sequence < b->sequence);
}

static void heap_swap(size_t a, size_t b) {
    zan_timer_entry *entry = g_heap[a];
    g_heap[a] = g_heap[b];
    g_heap[b] = entry;
}

static void heap_push(zan_timer_entry *entry) {
    if (g_heap_len == g_heap_cap) {
        size_t cap = g_heap_cap ? g_heap_cap * 2 : 64;
        zan_timer_entry **heap = (zan_timer_entry **)realloc(g_heap, cap * sizeof(*heap));
        if (!heap) abort();
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

void zan_timer_delay(long long ms, void *frame, zan_timer_step_t step) {
    if (!step) return;
    zan_timer_entry *entry = (zan_timer_entry *)calloc(1, sizeof(*entry));
    if (!entry) abort();
    entry->due_ms = zan_timer_now_ms() + (ms > 0 ? ms : 0);
    entry->kind = ZAN_TIMER_DELAY;
    entry->frame = frame;
    entry->step = step;
    timer_lock();
    g_initialized = 1;
    entry->sequence = ++g_sequence;   /* shared counter: only touch under the lock */
    heap_push(entry);
    timer_unlock();
}

static long long timer_add(long long ms, zan_timer_callback_t callback, int repeat) {
    if (ms < 1 || !callback) return 0;
    zan_timer_entry *entry = (zan_timer_entry *)calloc(1, sizeof(*entry));
    if (!entry) abort();
    /* Set the callback before publishing the entry: another thread dispatching
     * due timers must never see a pushed entry with a NULL callback. */
    entry->callback = callback;
    timer_lock();
    g_initialized = 1;
    entry->id = g_next_id++;
    if (entry->id <= 0) entry->id = g_next_id = 1;
    entry->interval = repeat ? ms : 0;
    entry->due_ms = zan_timer_now_ms() + ms;
    entry->sequence = ++g_sequence;
    entry->kind = ZAN_TIMER_PUBLIC;
    heap_push(entry);
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
        timeout = g_heap[0]->due_ms - zan_timer_now_ms();
        if (timeout < 0) timeout = 0;
    }
    timer_unlock();
    return timeout;
}

long long zan_timer_dispatch_due(void) {
    size_t dispatched = 0;
    for (;;) {
        long long now = zan_timer_now_ms();
        timer_lock();
        while (g_heap_len > 0 && g_heap[0]->removed) free(heap_pop());
        if (g_heap_len == 0 || g_heap[0]->due_ms > now) {
            timer_unlock();
            return dispatched;
        }
        zan_timer_entry *entry = heap_pop();
        if (entry->kind == ZAN_TIMER_PUBLIC) {
            entry->exec_count++;
            entry->round = ++g_round;
        }
        /* Publish the running entry under the lock so a clear() from inside
         * the callback (or from another thread) can mark it removed. Save the
         * previous entry: a callback that itself dispatches due timers would
         * otherwise clobber the outer entry's window. */
        zan_timer_entry *prev = g_dispatching;
        g_dispatching = entry;
        timer_unlock();

        long long started = zan_timer_now_ms();
        if (entry->kind == ZAN_TIMER_DELAY) {
            /* The hook is installed once at startup but set under the lock;
             * read it under the same lock so a concurrent install is visible. */
            void (*ready)(void *, zan_timer_step_t);
            timer_lock();
            ready = g_ready_hook;
            timer_unlock();
            if (ready) ready(entry->frame, entry->step);
            else entry->step(entry->frame);
        } else entry->callback();
        long long elapsed = zan_timer_now_ms() - started;
        dispatched++;

        timer_lock();
        g_dispatching = prev;
        if (entry->kind == ZAN_TIMER_PUBLIC) entry->exec_msec = elapsed;
        if (entry->kind == ZAN_TIMER_PUBLIC && entry->interval > 0 && !entry->removed) {
            entry->due_ms = zan_timer_now_ms() + entry->interval;
            entry->sequence = ++g_sequence;
            heap_push(entry);
            timer_unlock();
        } else {
            timer_unlock();
            free(entry);
        }
    }
}

long long zan_timer_pending(void) {
    return g_live;
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
    /* The entry whose callback is running is not in the heap; clear it here so
     * a tick callback can cancel itself (it would otherwise reschedule). */
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
    /* Also cover the running entry: a clear_all from inside a callback (or
     * from another thread) must stop the timer from rescheduling. */
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
    /* All three are written under the lock (dispatch bumps g_round/exec_count,
     * add/reset write g_initialized), so take it once and count inline instead
     * of calling zan_timer_list_count (which would re-lock). */
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

long long swoole_timer_tick(long long interval, zan_timer_callback_t callback) { return zan_timer_tick(interval, callback); }
long long swoole_timer_after(long long delay, zan_timer_callback_t callback) { return zan_timer_after(delay, callback); }
int swoole_timer_clear(long long id) { return zan_timer_clear(id); }
long long swoole_timer_clear_all(void) { return zan_timer_clear_all(); }
int swoole_timer_info(long long id, long long *a, long long *b, long long *c, long long *d, int *e) { return zan_timer_info(id, a, b, c, d, e); }
long long swoole_timer_list_count(void) { return zan_timer_list_count(); }
long long swoole_timer_list_at(long long index) { return zan_timer_list_at(index); }
void swoole_timer_stats(long long *a, long long *b, long long *c) { zan_timer_stats(a, b, c); }

/* ---- registry of live detached (Task.Spawn) coroutine frames ----
 *
 * A spawn handle is a raw frame pointer that can outlive the coroutine (the
 * reaper frees the frame), so Task.Cancel / Task.WhenAll must ask "is this
 * frame still alive?" before dereferencing it. That used to be an intrusive
 * singly-linked list rooted in a compiler-emitted global (__zan_co_live),
 * walked and spliced by emitted code with plain loads and stores. Two problems:
 *
 *   - unlinking scanned the list, so a server holding N live coroutines paid
 *     O(N) per completion -- at 1000 connections, a walk of a thousand frames
 *     per finished coroutine;
 *   - the splice was unsynchronized, which the multi-worker driver
 *     (--async-workers) turns into a crash: workers on different OS threads
 *     mutate the list concurrently and one follows a stale link (an access
 *     violation inside the emitted __zan_co_untrack).
 *
 * Same registry as an open-addressed hash set of frame pointers behind a spin
 * lock: O(1) expected for all operations and safe from any thread the driver
 * runs. It lives in this object rather than rt_co.c because emitted code calls
 * it unconditionally and zanc links this object into every native program,
 * while rt_co.c is replaced wholesale by the multi-worker driver.
 */


/* Tombstone: a slot whose frame was removed. Linear probing cannot simply
 * clear a slot without breaking the probe chains that run through it, and no
 * real frame pointer can be 1 (frames are at least 16-byte aligned). */
#define ZAN_LIVE_DEAD ((void *)(uintptr_t)1)

static void  **g_colive_slots;
static size_t   g_colive_cap;    /* power of two, 0 until first insert */
static size_t   g_colive_live;   /* occupied slots */
static size_t   g_colive_dead;   /* tombstones */

static volatile int g_colive_lock;

static void live_lock(void) {
    while (__sync_lock_test_and_set(&g_colive_lock, 1)) {
        while (g_colive_lock) {
#if defined(__i386__) || defined(__x86_64__)
            __builtin_ia32_pause();
#endif
        }
    }
}

static void live_unlock(void) { __sync_lock_release(&g_colive_lock); }

/* Pointer mix (splitmix64 finaliser): frames come from the allocator in
 * 16-byte-aligned runs, so the low bits alone would collide heavily. */
static size_t live_hash(void *p) {
    uint64_t x = (uint64_t)(uintptr_t)p;
    x ^= x >> 33; x *= 0xff51afd7ed558ccdULL;
    x ^= x >> 33; x *= 0xc4ceb9fe1a85ec53ULL;
    x ^= x >> 33;
    return (size_t)x;
}

/* Grow (or compact, when tombstones are what filled the table) to `ncap`. */
static void live_rehash(size_t ncap) {
    void **old = g_colive_slots;
    size_t ocap = g_colive_cap;
    void **ns = (void **)calloc(ncap, sizeof(*ns));
    if (!ns) zan_host_oom();
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
    /* Keep the load factor at or below 3/4 counting tombstones: probe chains
     * stay short, and a table churning in place (a server where coroutines
     * come and go) compacts instead of growing without bound. */
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

void zan_co_live_del(void *frame) {
    if (!frame || !g_colive_cap) return;
    live_lock();
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
}

int zan_co_live_has(void *frame) {
    if (!frame || !g_colive_cap) return 0;
    live_lock();
    size_t mask = g_colive_cap - 1;
    size_t i = live_hash(frame) & mask;
    int found = 0;
    for (;;) {
        void *cur = g_colive_slots[i];
        if (!cur) break;
        if (cur == frame) { found = 1; break; }
        i = (i + 1) & mask;
    }
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

/* ---- async runtime configuration ----
 * Per-program scheduler/reactor settings, written by System.Threading
 * .AsyncRuntime from Main and read by the multi-worker driver when it starts
 * (the generated main calls zan_co_sched_init at entry and zan_co_sched_run
 * after the body, so a call in Main lands between the two).
 *
 * They live here, not in rt_io.c, because this object is linked into every
 * program while the reactor object is linked only for socket-async ones -- a
 * program that only configures the runtime must still link. Programs that
 * configure nothing keep the previous behaviour: the driver falls back to the
 * ZAN_CO_WORKERS / ZAN_IO_SHARDS / ZAN_IO_SYNCFAST environment variables,
 * which stay available for A/B measurements without a rebuild. */
static volatile int g_cfg_workers   = 0;    /* 0  = unset (CPU count) */
static volatile int g_cfg_io_shards = 0;    /* 0  = unset (one per worker) */
static volatile int g_cfg_sync_fast = -1;   /* -1 = unset */

void zan_async_set_workers(int32_t n)    { g_cfg_workers = (n > 0) ? (int)n : 0; }
void zan_async_set_io_shards(int32_t n)  { g_cfg_io_shards = (n > 0) ? (int)n : 0; }
void zan_async_set_sync_fast(int32_t on) { g_cfg_sync_fast = on ? 1 : 0; }

int32_t zan_async_cfg_workers(void)   { return (int32_t)g_cfg_workers; }
int32_t zan_async_cfg_io_shards(void) { return (int32_t)g_cfg_io_shards; }
int32_t zan_async_cfg_sync_fast(void) { return (int32_t)g_cfg_sync_fast; }
