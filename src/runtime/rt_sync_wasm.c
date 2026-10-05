/* rt_sync_wasm.c -- wasm32 (WASI) single-threaded equivalents for the sync
 * runtime symbols.
 *
 * wasm has no pthreads and no shared memory, so rt_sync.o (the real
 * implementation) cannot be built for this target. Whole-namespace stdlib
 * pull-in (System.Threading via System.IO -> DirectoryWatcher, DataTable
 * export, HttpClient/Socket timeout plumbing) makes GUI-sized programs
 * reference these symbols even when most are never called at runtime, so
 * the wasm link needs definitions, not rejections.
 *
 * Behaviour on this target (one thread, one instantiation):
 *   zan_thread_start        runs the body synchronously, returns 1 (C# +
 *                           capture-less method group callers keep working;
 *                           there is no parallelism to lose).
 *   zan_thread_current_id   constant 1.
 *   zan_atomic_int_*        plain load/store on a malloc'd i64 cell -- the
 *                           ABI (an opaque i64 handle) is preserved, only
 *                           the atomicity is meaningless without threads.
 *   zan_mutex (pthread_*)   provided by rt_wasm.c's no-op shims.
 *   zan_monitor_enter/exit  no-ops (recursive by triviality: one thread).
 *   zan_monotonic_us/ns     real clock_gettime(CLOCK_MONOTONIC) -- WASI
 *                           emulates it with the page clock.
 *   zan_shared_table_*      graceful stubs: create/open/attach return 0
 *                           (SharedTable's Zan side treats 0 as "not
 *                           available"), getters return 0/empty, everything
 *                           else is a no-op. Cross-process tables cannot
 *                           exist on wasm; the Zan wrappers surface the
 *                           failure instead of crashing.
 *
 * Signatures match what the stdlib [DllImport]s declare as adapted to
 * wasm32: Zan nint/long are i64 at the wasm ABI boundary, so every
 * handle/delta parameter is int64_t here and string parameters arrive as
 * char* (wasm32 pointers are 32-bit; the i64 locals on the C side just
 * truncate to the pointer width, which is lossless in the other direction).
 *
 * Compile with: zig cc -target wasm32-wasi -std=gnu11 -O2 -c rt_sync_wasm.c
 * (shipped pre-compiled as toolchain/wasm32/zanrt_syncw.o)
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef int64_t i64;

/* ---- threads ------------------------------------------------------------ */

int32_t zan_thread_start(void *body) {
    /* body is the tagged closure record / bare fn pointer of a ThreadStart
     * delegate; invoke it with the delegate ABI: tagged records carry the
     * fn at record slot 0 and are called fn(rec), bare pointers are called
     * directly (ZAN_CLOSURE_TAG = bit 0). */
    if (!body) return 0;
    uintptr_t v = (uintptr_t)body;
    void (*fn)(void *);
    void *arg;
    if (v & 1) {
        void *rec = (void *)(v & ~(uintptr_t)1);
        /* wasm32 record layout: { fn, dtor, target } -- pointer slots are
         * 4 bytes wide (see gui_runtime_wasm.c wdisp). */
        fn = *(void (**)(void *))((char *)rec + 0 * sizeof(void *));
        arg = rec;
    } else {
        fn = (void (*)(void *))body;
        arg = NULL;
    }
    if (!fn) return 0;
    fn(arg);
    return 1;
}

int64_t zan_thread_current_id(void) {
    return 1;
}

/* ---- atomics (plain cells; one thread) ---------------------------------- */

typedef struct {
    i64 value;
} zan_wasm_atomic;

i64 zan_atomic_int_create(i64 initial_value) {
    zan_wasm_atomic *a = (zan_wasm_atomic *)malloc(sizeof(*a));
    if (!a) return 0;
    a->value = initial_value;
    return (i64)(intptr_t)a;
}

void zan_atomic_int_destroy(i64 handle) {
    free((void *)(intptr_t)handle);
}

i64 zan_atomic_int_load(i64 handle) {
    zan_wasm_atomic *a = (zan_wasm_atomic *)(intptr_t)handle;
    return a ? a->value : 0;
}

void zan_atomic_int_store(i64 handle, i64 value) {
    zan_wasm_atomic *a = (zan_wasm_atomic *)(intptr_t)handle;
    if (a) a->value = value;
}

i64 zan_atomic_int_exchange(i64 handle, i64 value) {
    zan_wasm_atomic *a = (zan_wasm_atomic *)(intptr_t)handle;
    if (!a) return 0;
    i64 old = a->value;
    a->value = value;
    return old;
}

i64 zan_atomic_int_compare_exchange(i64 handle, i64 expected, i64 desired) {
    zan_wasm_atomic *a = (zan_wasm_atomic *)(intptr_t)handle;
    if (!a) return 0;
    if (a->value == expected) { a->value = desired; return expected; }
    return a->value;
}

i64 zan_atomic_int_add(i64 handle, i64 delta) {
    zan_wasm_atomic *a = (zan_wasm_atomic *)(intptr_t)handle;
    if (!a) return 0;
    i64 old = a->value;
    a->value = old + delta;
    return old;
}

/* ---- monitor (lock statement) ------------------------------------------- */

void zan_monitor_enter(void *obj) { (void)obj; }
void zan_monitor_exit(void *obj)  { (void)obj; }

/* ---- monotonic clocks ---------------------------------------------------- */

i64 zan_monotonic_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (i64)ts.tv_sec * 1000000 + (i64)ts.tv_nsec / 1000;
}

i64 zan_monotonic_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (i64)ts.tv_sec * 1000000000 + (i64)ts.tv_nsec;
}

i64 zan_stopwatch_ticks(void) {
    return zan_monotonic_ns();
}

i64 zan_stopwatch_frequency(void) {
    return 1000000000;
}

i64 zan_monotonic_ticks(void) {
    return zan_monotonic_ns();
}

i64 zan_monotonic_frequency(void) {
    return 1000000000;
}

/* ---- shared table (cross-process; impossible here -> graceful stubs) ----- */

#define ZAN_TABLE_MAX_STRING 1048576

static char g_wasm_shared_string[ZAN_TABLE_MAX_STRING + 1];

i64 zan_shared_table_hash(const char *value) {
    /* FNV-1a, same as zan_hash_bytes in rt_sync.c -- callers may persist
     * hashes, keep the algorithm identical. Remap 0 to 1 like the native
     * side: a persisted 0 doubles as "no hash" downstream. */
    if (!value) return 0;
    uint64_t h = 1469598103934665603ull;
    for (const unsigned char *p = (const unsigned char *)value; *p; p++) {
        h ^= *p;
        h *= 1099511628211ull;
    }
    return (i64)(h ? h : 1);
}

i64 zan_shared_table_create(const char *name, int32_t capacity,
                            int32_t key_size, const char *schema) {
    (void)name; (void)capacity; (void)key_size; (void)schema;
    return 0;
}

i64 zan_shared_table_create_anon(int32_t capacity, int32_t key_size,
                                 const char *schema) {
    (void)capacity; (void)key_size; (void)schema;
    return 0;
}

i64 zan_shared_table_open(const char *name) {
    (void)name;
    return 0;
}

i64 zan_shared_table_attach(i64 os_handle) {
    (void)os_handle;
    return 0;
}

i64 zan_shared_table_handle(i64 handle) {
    (void)handle;
    return 0;
}

int32_t zan_shared_table_destroy(i64 handle) {
    (void)handle;
    return 0;
}

int32_t zan_shared_table_close(i64 handle) {
    (void)handle;
    return 0;
}

int32_t zan_shared_table_set_int(i64 handle, const char *key,
                                 const char *column, i64 value) {
    (void)handle; (void)key; (void)column; (void)value;
    return 0;
}

i64 zan_shared_table_get_int(i64 handle, const char *key, const char *column) {
    (void)handle; (void)key; (void)column;
    return 0;
}

int32_t zan_shared_table_set_float(i64 handle, const char *key,
                                   const char *column, double value) {
    (void)handle; (void)key; (void)column; (void)value;
    return 0;
}

double zan_shared_table_get_float(i64 handle, const char *key,
                                  const char *column) {
    (void)handle; (void)key; (void)column;
    return 0;
}

int32_t zan_shared_table_set_string(i64 handle, const char *key,
                                    const char *column, const char *value) {
    (void)handle; (void)key; (void)column; (void)value;
    return 0;
}

const char *zan_shared_table_get_string(i64 handle, const char *key,
                                        const char *column) {
    (void)handle; (void)key; (void)column;
    g_wasm_shared_string[0] = '\0';
    return g_wasm_shared_string;
}

i64 zan_shared_table_increment(i64 handle, const char *key,
                               const char *column, i64 delta) {
    (void)handle; (void)key; (void)column; (void)delta;
    return 0;
}

int32_t zan_shared_table_expire(i64 handle, const char *key, i64 ttl_ms) {
    (void)handle; (void)key; (void)ttl_ms;
    return 0;
}

int32_t zan_shared_table_expire_at(i64 handle, const char *key, i64 unix_ms) {
    (void)handle; (void)key; (void)unix_ms;
    return 0;
}

i64 zan_shared_table_expires_at(i64 handle, const char *key) {
    (void)handle; (void)key;
    return 0;
}

i64 zan_shared_table_purge_expired(i64 handle, i64 now_ms) {
    (void)handle; (void)now_ms;
    return 0;
}

int32_t zan_shared_table_rate_allow(i64 handle, const char *key, i64 now_ms,
                                    i64 window_ms, i64 limit) {
    (void)handle; (void)key; (void)now_ms; (void)window_ms; (void)limit;
    return 0;
}

int32_t zan_shared_table_lock_acquire(i64 handle, const char *key, i64 owner,
                                      i64 now_ms, i64 lease_ms) {
    (void)handle; (void)key; (void)owner; (void)now_ms; (void)lease_ms;
    return 1;   /* single thread: the lease is always ours */
}

int32_t zan_shared_table_lock_release(i64 handle, const char *key, i64 owner) {
    (void)handle; (void)key; (void)owner;
    return 1;
}

int32_t zan_shared_table_delete(i64 handle, const char *key) {
    (void)handle; (void)key;
    return 0;
}

int32_t zan_shared_table_exists(i64 handle, const char *key) {
    (void)handle; (void)key;
    return 0;
}

i64 zan_shared_table_count(i64 handle) {
    (void)handle;
    return 0;
}

void zan_shared_table_clear(i64 handle) {
    (void)handle;
}

i64 zan_shared_table_stat(i64 handle, int32_t what) {
    (void)handle; (void)what;
    return 0;
}

int32_t zan_shared_table_set_int_at(i64 handle, i64 key_hash,
                                    const char *column, i64 value) {
    (void)handle; (void)key_hash; (void)column; (void)value;
    return 0;
}

i64 zan_shared_table_get_int_at(i64 handle, i64 key_hash, const char *column) {
    (void)handle; (void)key_hash; (void)column;
    return 0;
}

i64 zan_shared_table_increment_at(i64 handle, i64 key_hash,
                                  const char *column, i64 delta) {
    (void)handle; (void)key_hash; (void)column; (void)delta;
    return 0;
}

i64 zan_shared_table_extreme_at(i64 handle, i64 key_hash, const char *column,
                                i64 value, i64 keep_larger) {
    (void)handle; (void)key_hash; (void)column; (void)value; (void)keep_larger;
    return 0;
}

int32_t zan_shared_table_set_string_at(i64 handle, i64 key_hash,
                                       const char *column, const char *value) {
    (void)handle; (void)key_hash; (void)column; (void)value;
    return 0;
}

const char *zan_shared_table_get_string_at(i64 handle, i64 key_hash,
                                           const char *column) {
    (void)handle; (void)key_hash; (void)column;
    g_wasm_shared_string[0] = '\0';
    return g_wasm_shared_string;
}

int32_t zan_shared_table_match_at(i64 handle, i64 key_hash,
                                  const char *column, const char *text) {
    (void)handle; (void)key_hash; (void)column; (void)text;
    return 0;
}

int32_t zan_shared_table_exists_at(i64 handle, i64 key_hash) {
    (void)handle; (void)key_hash;
    return 0;
}

int32_t zan_shared_table_delete_at(i64 handle, i64 key_hash) {
    (void)handle; (void)key_hash;
    return 0;
}

/* ---- gate + socket-async stubs -------------------------------------------
 * The Gate/async-socket layer rides the runtime IO reactor (rt_io.c), which
 * cannot build for wasm. GUI stdlib code (HttpClient timeout plumbing,
 * Socket declarations) references these symbols at declaration level, so
 * GUI-sized programs need definitions. Sockets cannot exist on wasm, so
 * every operation fails cleanly: connect/resolve report failure, send/recv
 * report error, alive reports dead, ready reports not-ready. Gate handles
 * never round-trip through real waits on this target (Task.Delay paths are
 * the coroutine scheduler's own timers, not gates), so gate_new returning 0
 * is only a marker that must never crash if signaled. */
long zan_gate_new(void) {
    return 0;
}

void zan_gate_signal(long h) {
    (void)h;
}

void zan_gate_free(long h) {
    (void)h;
}

long long zan_io_socket_send(long long sock, const char *buf, long long len, int flags) {
    (void)sock; (void)buf; (void)flags;
    return len ? -1 : 0;
}

long long zan_io_socket_recv(long long sock, char *buf, long long len, int flags) {
    (void)sock; (void)buf; (void)flags;
    return len ? -1 : 0;
}

int zan_io_socket_ready(long long sock, int write_ready) {
    (void)sock; (void)write_ready;
    return 0;
}

int zan_io_socket_alive(long long sock) {
    (void)sock;
    return 0;
}

/* Close-notification hook: wasm32 has no reactor and no waiters to
 * fail; Socket.Close still calls it, so provide the symbol. */
void zan_io_close_notify(long long sock) {
    (void)sock;
}

int zan_io_connect_status(long long sock) {
    (void)sock;
    return -1;
}

const char *zan_io_socket_peer_ip(long long sock) {
    (void)sock;
    g_wasm_shared_string[0] = '\0';
    return g_wasm_shared_string;
}

int zan_io_socket_peer_ip_into(long long sock, char *buf, int cap) {
    (void)sock;
    if (buf && cap > 0) buf[0] = '\0';
    return -1;
}

void zan_io_socket_cleanup(void) {}

int zan_io_resolve_ipv4(const char *hostname) {
    (void)hostname;
    return -1;
}

int zan_io_resolve_sa(const char *name, int port, void *buf, int cap) {
    (void)name; (void)port; (void)buf; (void)cap;
    return -1;
}

int zan_io_resolve_all(const char *name, int port, void *buf, int cap) {
    (void)name; (void)port; (void)buf; (void)cap;
    return -1;
}

long long zan_io_resolve_all_async(long long name, int port, long long buf,
                                   int cap) {
    (void)name; (void)port; (void)buf; (void)cap;
    return -1;
}

long long zan_io_connect_sa(long long sock, const void *sa, int len, int timeout_ms) {
    (void)sock; (void)sa; (void)len; (void)timeout_ms;
    return -1;
}

int zan_io_sockaddr_family(const void *sa, int len) {
    (void)sa; (void)len;
    return 0;
}

int zan_io_sockaddr_is_safe(const void *sa, int len, int allow_loopback) {
    (void)sa; (void)len; (void)allow_loopback;
    return 0;
}

const char *zan_io_sockaddr_ip_str(const void *sa) {
    (void)sa;
    g_wasm_shared_string[0] = '\0';
    return g_wasm_shared_string;
}

int zan_io_sockaddr_ip_str_into(int sa, long long buf, int cap) {
    (void)sa;
    if (buf && cap > 0) ((char *)(uintptr_t)buf)[0] = '\0';
    return -1;
}

/* ---- setjmp/longjmp (async trampoline) -----------------------------------
 * wasi-libc ships neither symbol. The only wasm-side references come from
 * the async-state-machine EH trampoline (irgen_async.c emit_eh_setjmp);
 * wasm try/catch itself lowers to engine EH and never lands here. The
 * trampoline protocol: setjmp returns 0 to run the body, non-zero when
 * longjmp'd back. A longjmp on wasm means "unwind to the async frame
 * handler", and since the coroutines this target can actually run are
 * driven by the scheduler's own resume path (which clears the handler
 * before re-entry), reaching the longjmp means the frame has nowhere to
 * go -- abort loudly instead of corrupting the stack. */
int _setjmp(void *env) {
    (void)env;
    return 0;
}
/* longjmp itself already comes from rt_wasm.c's abort-shaped shim; no
 * second definition here (wasm-ld rejects the duplicate). */

/* ---- OpenSSL (ssl/crypto) failure stubs ----------------------------------
 * TLS clients cannot exist on wasm (no sockets, no libssl). TlsStream.zan
 * declares the openssl surface for desktop builds; the GUI stdlib pulls the
 * declarations in, so the link needs definitions. Every entry point fails
 * the way openssl does on a dead context: allocation returns NULL (the Zan
 * side checks for that), handshake/read/write return the error family,
 * verification returns failure. A program that actually reaches for TLS
 * surfaces the failure through TlsStream/HttpClient's normal error path
 * instead of trapping the instance. */
void *TLS_server_method(void) { return 0; }
void *TLS_client_method(void) { return 0; }

void *SSL_CTX_new(const void *method) { (void)method; return 0; }
void SSL_CTX_free(void *ctx) { (void)ctx; }
int SSL_CTX_use_certificate_chain_file(void *ctx, const char *file) {
    (void)ctx; (void)file; return 0;
}
int SSL_CTX_use_PrivateKey_file(void *ctx, const char *file, int type) {
    (void)ctx; (void)file; (void)type; return 0;
}
int SSL_CTX_check_private_key(const void *ctx) { (void)ctx; return 0; }
void SSL_CTX_set_verify(void *ctx, int mode, const void *cb) {
    (void)ctx; (void)mode; (void)cb;
}
int SSL_CTX_set_default_verify_paths(void *ctx) { (void)ctx; return 0; }
int SSL_CTX_load_verify_locations(void *ctx, const char *cafile,
                                  const char *capath) {
    (void)ctx; (void)cafile; (void)capath; return 0;
}
void *SSL_CTX_get_cert_store(const void *ctx) { (void)ctx; return 0; }

void *SSL_new(const void *ctx) { (void)ctx; return 0; }
void SSL_free(void *ssl) { (void)ssl; }
void SSL_set_bio(void *ssl, void *rbio, void *wbio) {
    (void)ssl; (void)rbio; (void)wbio;
}
void SSL_set_accept_state(void *ssl) { (void)ssl; }
void SSL_set_connect_state(void *ssl) { (void)ssl; }
int SSL_do_handshake(void *ssl) { (void)ssl; return -1; }
int SSL_read(void *ssl, void *buf, int num) {
    (void)ssl; (void)buf; (void)num; return -1;
}
int SSL_write(void *ssl, const void *buf, int num) {
    (void)ssl; (void)buf; (void)num; return -1;
}
int SSL_get_error(const void *ssl, int ret) {
    (void)ssl; (void)ret; return 1;   /* SSL_ERROR_SSL: tls layer is dead */
}
int SSL_set_verify(void *ssl, int mode, const void *cb) {
    (void)ssl; (void)mode; (void)cb; return 0;
}
int SSL_shutdown(void *ssl) { (void)ssl; return 0; }
long SSL_ctrl(void *ssl, int cmd, long larg, void *parg) {
    (void)ssl; (void)cmd; (void)larg; (void)parg; return 0;
}
void *SSL_get0_param(void *ssl) { (void)ssl; return 0; }
long long SSL_get_verify_result(const void *ssl) {
    (void)ssl; return 20;             /* X509_V_ERR_APPLICATION_VERIFICATION */
}
void *SSL_get1_peer_certificate(const void *ssl) { (void)ssl; return 0; }

long long ERR_get_error(void) { return 0; }
void *d2i_X509(void **px, const unsigned char **pp, long len) {
    (void)px; (void)pp; (void)len; return 0;
}
int X509_STORE_add_cert(void *store, const void *x) {
    (void)store; (void)x; return 0;
}
void X509_free(void *x) { (void)x; }
const char *X509_get_issuer_name(const void *x) { (void)x; return ""; }
const char *X509_get_subject_name(const void *x) { (void)x; return ""; }
int X509_NAME_oneline(const void *name, unsigned char *buf, int size) {
    (void)name;
    if (buf && size > 0) buf[0] = '\0';
    return 0;
}
int X509_VERIFY_PARAM_set1_host(void *param, const char *name, long long len) {
    (void)param; (void)name; (void)len; return 0;
}
int X509_VERIFY_PARAM_set1_ip_asc(void *param, const char *ipasc) {
    (void)param; (void)ipasc; return 0;
}
void X509_VERIFY_PARAM_set_hostflags(void *param, long long flags) {
    (void)param; (void)flags;
}
const char *X509_VERIFY_PARAM_get0_name(const void *param) {
    (void)param; return "";
}

void *BIO_new(const void *method) { (void)method; return 0; }
void *BIO_s_mem(void) { return 0; }
int BIO_write(void *bio, const void *data, int dlen) {
    (void)bio; (void)data; (void)dlen; return -1;
}
int BIO_read(void *bio, void *data, int dlen) {
    (void)bio; (void)data; (void)dlen; return -1;
}
int BIO_ctrl_pending(const void *bio) { (void)bio; return 0; }
const void *X509_get_X509_PUBKEY(const void *x) { (void)x; return 0; }
int i2d_X509_PUBKEY(const void *pubkey, unsigned char **pp) {
    (void)pubkey; (void)pp; return 0;
}
void CRYPTO_free(long long p, int file, int line) {
    (void)file; (void)line;
    free((void *)(uintptr_t)p);
}

/* ---- coroutine IO reactor stubs (irgen.c's always-declared externs) ------
 * Every program that uses async/await gets these declarations even without
 * sockets (the async machinery declares them unconditionally; the linker
 * only pulls them when referenced). Socket-bearing awaits must resume the
 * frame with a failure result: each stub stores -1/0 into the out-slot and
 * resumes the step so the coroutine unwinds through its own error path
 * instead of hanging forever. */
typedef void (*zan_co_step)(void *frame);

static void zan_wasm_co_fail(void *frame, zan_co_step step) {
    if (frame && step) step(frame);
}

void zan_io_wait_co(long long fd, int interest, void *frame, zan_co_step step) {
    (void)fd; (void)interest;
    zan_wasm_co_fail(frame, step);
}

void zan_io_recv_co(long long fd, void *buf, int len, void *frame,
                    zan_co_step step, long long *out_n) {
    (void)fd; (void)buf; (void)len;
    if (out_n) *out_n = -1;
    zan_wasm_co_fail(frame, step);
}

void zan_io_accept_co(long long fd, void *frame, zan_co_step step,
                      long long *out_sock) {
    (void)fd;
    if (out_sock) *out_sock = -1;
    zan_wasm_co_fail(frame, step);
}

void zan_io_resolve_co(const void *name, int port, void *buf, int cap,
                       void *frame, zan_co_step step, int *out_n) {
    (void)name; (void)port; (void)buf; (void)cap;
    if (out_n) *out_n = -1;
    zan_wasm_co_fail(frame, step);
}

void zan_io_resolve_sa_co(const void *name, int port, void *buf, int cap,
                          void *frame, zan_co_step step, int *out_len) {
    (void)name; (void)port; (void)buf; (void)cap;
    if (out_len) *out_len = -1;
    zan_wasm_co_fail(frame, step);
}

void zan_rt_blocking_co(void *fn, int32_t argc, long long a0, long long a1,
                        long long a2, long long a3, void *frame, zan_co_step step,
                        long long *out) {
    (void)fn; (void)argc; (void)a0; (void)a1; (void)a2; (void)a3;
    /* No worker threads to park this on; the stub has nothing to run. */
    if (out) *out = 0;
    zan_wasm_co_fail(frame, step);
}

/* ---- libc surface wasi-libc lacks ---------------------------------------- */
/* Process.Start's POSIX path (Process.zan) declares system(); a browser tab
 * has neither a shell nor child processes, so the command never runs. */
int system(const char *cmd) {
    (void)cmd;
    return -1;
}

/* wasi-libc resolves the environment lazily: the first getenv triggers the
 * environ population, which walks the host-provided env block through the
 * zan WASI shims and crashes (strncmp over garbage entries) -- the shims'
 * empty-environ contract is not one wasi-libc's population path anticipates.
 * A browser tab has no environment variables anyway, so answer NULL
 * directly: callers (Skin.Env and friends) already treat NULL as "unset". */
char *getenv(const char *name) {
    (void)name;
    return 0;
}

/* ---- safe process execution stubs (cross-platform consistency) --------- */
int32_t zan_proc_run_safe(const char *exe, const char **args, int32_t argc) {
    (void)exe; (void)args; (void)argc;
    return -1;
}

int32_t zan_proc_start_detached_safe(const char *exe, const char **args, int32_t argc) {
    (void)exe; (void)args; (void)argc;
    return -1;
}

int32_t zan_proc_capture_safe(const char *exe, const char **args, int32_t argc,
                              char **out_buf, int32_t *out_len, int32_t *exit_code) {
    (void)exe; (void)args; (void)argc;
    if (out_buf) *out_buf = NULL;
    if (out_len) *out_len = 0;
    if (exit_code) *exit_code = -1;
    return -1;
}

void zan_proc_free_buf(char *buf) {
    if (buf) free(buf);
}

/* ---- memory mapped file stubs (WASM browser/sandbox has no mmap/shm) --- */
long long zan_mmap_create(const char *name, long long size) {
    (void)name; (void)size;
    return 0;
}

long long zan_mmap_open(const char *name, long long size) {
    (void)name; (void)size;
    return 0;
}

long long zan_mmap_from_file(const char *path, long long size) {
    (void)path; (void)size;
    return 0;
}

long long zan_mmap_map(long long handle, long long size) {
    (void)handle; (void)size;
    return 0;
}

long long zan_mmap_unmap(long long ptr, long long size) {
    (void)ptr; (void)size;
    return 0;
}

long long zan_mmap_flush(long long ptr, long long size) {
    (void)ptr; (void)size;
    return 0;
}

long long zan_mmap_close(long long handle) {
    (void)handle;
    return 0;
}

long long zan_mmap_unlink(const char *name) {
    (void)name;
    return 0;
}

