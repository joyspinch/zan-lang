/* rt_timer_diag.c: 跨平台参数转换、软失败日志记录与致命错误处理 */

int zan_utf8_argv(int *argc, char ***argv) {
#if defined(_WIN32)
  if (!argc || !argv)
    return 0;

  int wide_argc = 0;
  LPWSTR *wide_argv = CommandLineToArgvW(GetCommandLineW(), &wide_argc);
  if (!wide_argv || wide_argc < 1)
    return 0;

  char **utf8_argv = (char **)calloc((size_t)wide_argc + 1, sizeof(*utf8_argv));
  if (!utf8_argv) {
    LocalFree(wide_argv);
    return 0;
  }

  for (int i = 0; i < wide_argc; i++) {
    int bytes =
        WideCharToMultiByte(CP_UTF8, 0, wide_argv[i], -1, NULL, 0, NULL, NULL);
    if (bytes <= 0) {
      for (int j = 0; j < i; j++)
        free(utf8_argv[j]);
      free(utf8_argv);
      LocalFree(wide_argv);
      return 0;
    }
    utf8_argv[i] = (char *)malloc((size_t)bytes);
    if (!utf8_argv[i]) {
      for (int j = 0; j < i; j++)
        free(utf8_argv[j]);
      free(utf8_argv);
      LocalFree(wide_argv);
      return 0;
    }
    if (WideCharToMultiByte(CP_UTF8, 0, wide_argv[i], -1, utf8_argv[i], bytes,
                            NULL, NULL) != bytes) {
      free(utf8_argv[i]);
      for (int j = 0; j < i; j++)
        free(utf8_argv[j]);
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

#define ZAN_SOFT_MAX_SITES 1024

static char *g_soft_seen[ZAN_SOFT_MAX_SITES];
static int g_soft_seen_count;
static int g_soft_strict;

/* 结果一经算出即不变：无锁快路径读已发布的值，只有首次计算才进锁。
 * 曾每次调用都拿全局 timer_lock——与定时器调度同一把锁，热路径上的
 * 软失败判定会把多 worker 串行化。 */
static int g_soft_hard = -1;

static int zan_soft_is_hard(void) {
  int v = __atomic_load_n(&g_soft_hard, __ATOMIC_ACQUIRE);
  if (v >= 0)
    return v;
  timer_lock();
  v = g_soft_hard;
  if (v < 0) {
    const char *env = getenv("ZAN_RT_HARD");
    v = (env && *env && *env != '0') ? 1 : 0;
    if (!v && !env)
      v = g_soft_strict;
    __atomic_store_n(&g_soft_hard, v, __ATOMIC_RELEASE);
  }
  timer_unlock();
  return v;
}

int zan_rt_soft_is_hard(void) { return zan_soft_is_hard(); }

void zan_rt_set_strict(void) { g_soft_strict = 1; }

#define ZAN_SOFT_SCRATCH_RC (UINT64_C(1) << 62)
#define ZAN_SOFT_SCRATCH_PAYLOAD 4096
static union {
  unsigned char bytes[ZAN_SOFT_PREFIX_SIZE + 8 + ZAN_SOFT_SCRATCH_PAYLOAD];
  long double align_it;
} g_soft_scratch_store;
static unsigned char *g_soft_scratch;

static void store_u64_le(unsigned char *p, uint64_t v) {
  for (int i = 0; i < 8; i++) {
    p[i] = (unsigned char)(v >> (8 * i));
  }
}

/* 编译器在大量普通函数里插这条调用取软失败暂存区：返回的是常量指针，
 * 只需首次初始化。曾每次都拿全局 timer_lock——Web 压测下多 worker 在这把锁
 * 上排队，锁等待占 CPU 采样的四成以上（psynch_mutexwait 归因 ~6000/19000）。
 * 改为 acquire 读快路径；未发布时进锁双检、初始化后 release 发布。 */
/* Prefix the generated code reads in front of a soft scratch base P (see
 * ZAN_SOFT_ARR_BASE_OFF in zan_abi.h). hdr = start of the prefix:
 *   empty array A = P + ZAN_SOFT_ARR_BASE_OFF: rc, rc magic, count 0, magic,
 *   then ZAN_SOFT_ARR_SPAN zero bytes;
 *   object at P: rc/magic at P-32/P-24, array length (and the self-host
 *   object rc) at P-16, discriminator P-8. The C host's object/string rc is
 *   the low half of P-8 (docs/OBJECT_LAYOUT.md): ARRAY_MAGIC's low word is
 *   huge, so it never reaches zero, and its high word decodes to an
 *   out-of-range allocation site, so descriptor lookups miss. */
static void soft_scratch_init_header(unsigned char *hdr) {
  store_u64_le(hdr + 0, ZAN_SOFT_SCRATCH_RC); /* A-32: array rc */
  store_u64_le(hdr + 8, ZAN_ARRAY_RC_MAGIC);
  store_u64_le(hdr + 16, 0); /* A-16: count 0 */
  store_u64_le(hdr + 24, ZAN_ARRAY_MAGIC);
  hdr += ZAN_SOFT_PREFIX_SIZE - 32;
  store_u64_le(hdr + 0, ZAN_SOFT_SCRATCH_RC); /* P-32: array rc */
  store_u64_le(hdr + 8, ZAN_ARRAY_RC_MAGIC);
  store_u64_le(hdr + 16, ZAN_SOFT_SCRATCH_RC); /* P-16: length / self-host rc */
  store_u64_le(hdr + 24, ZAN_ARRAY_MAGIC);     /* P-8:  discriminator */
}

static unsigned char *soft_scratch_shared(void) {
  unsigned char *scratch = __atomic_load_n(&g_soft_scratch, __ATOMIC_ACQUIRE);
  if (__builtin_expect(scratch != NULL, 1))
    return scratch;
  timer_lock();
  scratch = g_soft_scratch;
  if (!scratch) {
    unsigned char *hdr = g_soft_scratch_store.bytes;
    memset(hdr, 0, sizeof(g_soft_scratch_store.bytes));
    soft_scratch_init_header(hdr);
    scratch = hdr + ZAN_SOFT_PREFIX_SIZE;
    __atomic_store_n(&g_soft_scratch, scratch, __ATOMIC_RELEASE);
  }
  timer_unlock();
  return scratch;
}

/* Per-thread soft-failure scratch. A null receiver outside a try continues
 * on this memory, so it must not be shared: one process-wide buffer let a
 * thread (one request) read what another wrote through its own null object,
 * and racing writes to it are data races. It must also be bounded: the
 * caller adds the field offset of whatever class it accessed, which can
 * exceed any fixed payload. So each thread that ever fails maps
 * [guard][header + 1 MiB payload][guard] lazily: untouched pages cost no
 * physical memory, and an offset past the payload faults on a guard page
 * instead of corrupting adjacent memory. Freed at thread exit. Targets
 * without threads or mmap (wasm, bare metal) and any mapping failure keep
 * the shared static buffer. */
#define ZAN_SOFT_TLS_PAYLOAD ((size_t)1 << 20)
#if (defined(_WIN32) || defined(__unix__) || defined(__APPLE__)) &&            \
    !defined(__wasm__) && !defined(ZAN_BARE_METAL)
#define ZAN_SOFT_PER_THREAD 1
#if !defined(_WIN32)
#include <pthread.h>
#include <sys/mman.h>
#include <unistd.h>
#endif

static size_t soft_page(void) {
#if defined(_WIN32)
  SYSTEM_INFO si;
  GetSystemInfo(&si);
  return si.dwPageSize ? si.dwPageSize : 4096;
#else
  long ps = sysconf(_SC_PAGESIZE);
  return ps > 0 ? (size_t)ps : 4096;
#endif
}

static size_t soft_map_len(size_t page) {
  size_t body =
      (ZAN_SOFT_PREFIX_SIZE + ZAN_SOFT_TLS_PAYLOAD + page - 1) / page * page;
  return page + body + page;
}

/* A finished thread's region is wiped and pooled, never unmapped: a pointer
 * that escaped into it (a `ref` to a field of a null object) must keep
 * landing in valid, harmless scratch rather than in whatever mapping reuses
 * the address. Wiping replaces the payload with fresh zero pages, so nothing
 * one thread wrote is visible to the next. */
#define ZAN_SOFT_POOL_MAX 256
static unsigned char *g_soft_pool[ZAN_SOFT_POOL_MAX];
static int g_soft_pool_n;

static int soft_wipe(unsigned char *scratch) {
  size_t page = soft_page();
  unsigned char *map = scratch - ZAN_SOFT_PREFIX_SIZE - page;
  size_t body = soft_map_len(page) - 2 * page;
#if defined(_WIN32)
  if (!VirtualFree(map + page, body, MEM_DECOMMIT))
    return 0;
  if (!VirtualAlloc(map + page, body, MEM_COMMIT, PAGE_READWRITE))
    return 0;
#else
  if (mmap(map + page, body, PROT_READ | PROT_WRITE,
           MAP_PRIVATE | MAP_ANON | MAP_FIXED, -1, 0) == MAP_FAILED)
    return 0;
#endif
  soft_scratch_init_header(map + page);
  return 1;
}

static void soft_release(unsigned char *scratch) {
  if (!scratch)
    return;
  if (!soft_wipe(scratch))
    return; /* keep it mapped; never hand it out again */
  timer_lock();
  if (g_soft_pool_n < ZAN_SOFT_POOL_MAX)
    g_soft_pool[g_soft_pool_n++] = scratch;
  timer_unlock();
}

static unsigned char *soft_pool_take(void) {
  unsigned char *s = NULL;
  timer_lock();
  if (g_soft_pool_n > 0)
    s = g_soft_pool[--g_soft_pool_n];
  timer_unlock();
  return s;
}

static unsigned char *soft_map(void) {
  size_t page = soft_page(), len = soft_map_len(page);
#if defined(_WIN32)
  unsigned char *map =
      (unsigned char *)VirtualAlloc(NULL, len, MEM_RESERVE, PAGE_NOACCESS);
  if (!map)
    return NULL;
  if (!VirtualAlloc(map + page, len - 2 * page, MEM_COMMIT, PAGE_READWRITE)) {
    VirtualFree(map, 0, MEM_RELEASE);
    return NULL;
  }
#else
  unsigned char *map = (unsigned char *)mmap(
      NULL, len, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0);
  if (map == MAP_FAILED)
    return NULL;
  if (mprotect(map + page, len - 2 * page, PROT_READ | PROT_WRITE) != 0) {
    munmap(map, len);
    return NULL;
  }
#endif
  soft_scratch_init_header(map + page);
  return map + page + ZAN_SOFT_PREFIX_SIZE;
}

static _Thread_local unsigned char *t_soft_scratch;
static _Thread_local int t_soft_scratch_exiting;
static int g_soft_key_state; /* 0 none, 1 creating, 2 ready, 3 failed */
#if defined(_WIN32)
static DWORD g_soft_fls = FLS_OUT_OF_INDEXES;
static void WINAPI soft_thread_exit(void *p) {
  t_soft_scratch = NULL;
  t_soft_scratch_exiting = 1;
  soft_release((unsigned char *)p);
}
#else
static pthread_key_t g_soft_key;
static void soft_thread_exit(void *p) {
  t_soft_scratch = NULL;
  t_soft_scratch_exiting = 1;
  soft_release((unsigned char *)p);
}
#endif

static int soft_key_ready(void) {
  int st = __atomic_load_n(&g_soft_key_state, __ATOMIC_ACQUIRE);
  if (st == 2)
    return 1;
  if (st == 3)
    return 0;
  timer_lock();
  if (g_soft_key_state == 0) {
#if defined(_WIN32)
    g_soft_fls = FlsAlloc(soft_thread_exit);
    int ok = g_soft_fls != FLS_OUT_OF_INDEXES;
#else
    int ok = pthread_key_create(&g_soft_key, soft_thread_exit) == 0;
#endif
    __atomic_store_n(&g_soft_key_state, ok ? 2 : 3, __ATOMIC_RELEASE);
  }
  st = g_soft_key_state;
  timer_unlock();
  return st == 2;
}
#endif

unsigned char *zan_rt_soft_scratch(void) {
#if defined(ZAN_SOFT_PER_THREAD)
  unsigned char *mine = t_soft_scratch;
  if (__builtin_expect(mine != NULL, 1))
    return mine;
  if (t_soft_scratch_exiting)
    return soft_scratch_shared();
  static int shared_only = -1;
  int so = __atomic_load_n(&shared_only, __ATOMIC_RELAXED);
  if (so < 0) {
    so = getenv("ZAN_SOFT_SHARED_SCRATCH") != NULL;
    __atomic_store_n(&shared_only, so, __ATOMIC_RELAXED);
  }
  if (!so && soft_key_ready()) {
    mine = soft_pool_take();
    if (!mine)
      mine = soft_map();
    if (mine) {
#if defined(_WIN32)
      if (!FlsSetValue(g_soft_fls, mine)) {
        soft_release(mine);
        return soft_scratch_shared();
      }
#else
      if (pthread_setspecific(g_soft_key, mine) != 0) {
        soft_release(mine);
        return soft_scratch_shared();
      }
#endif
      t_soft_scratch = mine;
      return mine;
    }
  }
#endif
  return soft_scratch_shared();
}

static int zan_soft_seen(const char *text) {
  for (int i = 0; i < g_soft_seen_count; i++)
    if (g_soft_seen[i] == text)
      return 1;
  if (g_soft_seen_count < ZAN_SOFT_MAX_SITES)
    g_soft_seen[g_soft_seen_count++] = (char *)text;
  return 0;
}

static struct {
  const char *file;
  unsigned line;
  unsigned col;
} g_soft_seen3[ZAN_SOFT_MAX_SITES];
static int g_soft_seen3_count;

static int zan_soft_seen3(const char *file, unsigned line, unsigned col) {
  for (int i = 0; i < g_soft_seen3_count; i++)
    if (g_soft_seen3[i].file == file && g_soft_seen3[i].line == line &&
        g_soft_seen3[i].col == col)
      return 1;
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
  if (n == 0 || n >= cap) {
    snprintf(path, cap, "zan_soft.log");
    return;
  }
  char *slash = strrchr(path, '\\');
  if (!slash) {
    snprintf(path, cap, "zan_soft.log");
    return;
  }
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
  ssize_t n =
      readlink("/proc/self/exe", g_soft_logdir, sizeof(g_soft_logdir) - 6);
  if (n <= 0)
    return;
  g_soft_logdir[n] = '\0';
  char *slash = strrchr(g_soft_logdir, '/');
  if (!slash) {
    memcpy(g_soft_logdir, "logs", 5);
    return;
  }
  slash[1] = '\0';
  strncat(g_soft_logdir, "logs",
          sizeof(g_soft_logdir) - strlen(g_soft_logdir) - 1);
}

/* <logdir>/<YYYYMM>/<DD> */
static void zan_soft_log_path(char *path, size_t cap) {
  if (!g_soft_logdir[0])
    zan_soft_init_logdir();
  time_t now = time(NULL);
  struct tm lt;
  if (!localtime_r(&now, &lt)) {
    snprintf(path, cap, "zan_soft.log");
    return;
  }
  char month[16], day[16];
  strftime(month, sizeof month, "%Y%m", &lt);
  strftime(day, sizeof day, "%d", &lt);
  char dir[4096];
  if ((size_t)snprintf(dir, sizeof dir, "%s/%s", g_soft_logdir, month) >=
      sizeof dir) {
    snprintf(path, cap, "zan_soft.log");
    return;
  }
  mkdir(g_soft_logdir, 0755);
  mkdir(dir, 0755);
    /* A directory that fits but leaves no room for "/DD.log" would be cut
     * mid-name and log to a stray path; fall back like the overflow above. */
    if ((size_t)snprintf(path, cap, "%s/%s.log", dir, day) >= cap)
        snprintf(path, cap, "zan_soft.log");
}
#endif

static void zan_soft_append(const char *text) {
  char path[4096];
  zan_soft_log_path(path, sizeof path);
  FILE *f = fopen(path, "ab");
  if (!f) {
#if defined(_WIN32)
    /* exe 目录只读（Program Files / 部署盘）时的最后一档：用户临时
     * 目录。软诊断宁可落进 %TEMP% 也不要静默丢（审计 P1-2）。 */
    char tmp[MAX_PATH];
    DWORD tn = GetTempPathA((DWORD)sizeof tmp, tmp);
    if (tn > 0 && tn < sizeof tmp) {
      snprintf(path, sizeof path, "%szan_crash.log", tmp);
      f = fopen(path, "ab");
    }
#endif
  }
  if (!f)
    return;
  fseek(f, 0, SEEK_END);
  long size = ftell(f);
  if (size == 0) {
#if defined(_WIN32)
    char exe[MAX_PATH];
    DWORD en = GetModuleFileNameA(NULL, exe, sizeof exe);
    if (en == 0 || en >= sizeof exe)
      snprintf(exe, sizeof exe, "<unknown>");
    fprintf(f, "==== ZAN RUNTIME (soft) ====\nexe=%s\n", exe);
#else
    char exe[4096];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    if (n > 0)
      exe[n] = '\0';
    else
      snprintf(exe, sizeof exe, "<unknown>");
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
  if (!text)
    return;
  timer_lock();
  int seen = zan_soft_seen(text);
  timer_unlock();
  if (seen)
    return;
  fprintf(stderr, "%s", text);
  fflush(stderr);
  zan_soft_append(text);
}

void zan_rt_soft_note2(const char *prefix, const char *msg) {
  if (!prefix)
    return;
  char buf[1400];
  timer_lock();
  int seen = zan_soft_seen(prefix);
  if (!seen) {
    size_t n = strlen(prefix);
    if (n >= sizeof buf)
      n = sizeof buf - 1;
    memcpy(buf, prefix, n);
    if (msg) {
      size_t m = strlen(msg);
      if (n + m >= sizeof buf)
        m = sizeof buf - 1 - n;
      memcpy(buf + n, msg, m);
      n += m;
    }
    buf[n] = '\0';
  }
  timer_unlock();
  if (seen)
    return;
  fprintf(stderr, "%s", buf);
  fflush(stderr);
  zan_soft_append(buf);
}

void zan_rt_soft_note3(const char *file, unsigned line, unsigned col,
                       const char *msg) {
  char buf[1400];
  int n = snprintf(buf, sizeof buf, "%s:%u:%u: runtime error: %s",
                   file ? file : "<unknown>", line, col, msg ? msg : "");
  if (n <= 0)
    return;
  if ((size_t)n >= sizeof buf - 1)
    n = (int)sizeof buf - 2;
  buf[n] = '\n';
  buf[n + 1] = '\0';
  timer_lock();
  int seen = zan_soft_seen3(file, line, col);
  timer_unlock();
  if (seen)
    return;
  fprintf(stderr, "%s", buf);
  fflush(stderr);
  zan_soft_append(buf);
}

void zan_rt_guard_fail2(const char *prefix, const char *msg) {
  if (zan_soft_is_hard()) {
    char buf[1400];
    size_t n = prefix ? strlen(prefix) : 0;
    if (n >= sizeof buf)
      n = sizeof buf - 1;
    memcpy(buf, prefix ? prefix : "", n);
    if (msg) {
      size_t m = strlen(msg);
      if (n + m >= sizeof buf)
        m = sizeof buf - 1 - n;
      memcpy(buf + n, msg, m);
      n += m;
    }
    buf[n] = '\0';
    fprintf(stderr, "%s", buf);
    fflush(stderr);
#if defined(_WIN32)
    void(WINAPI * raise)(DWORD, DWORD, DWORD, const ULONG_PTR *) =
        RaiseException;
    unsigned long code = 0xE0A2C010u;
    ULONG_PTR args[2] = {(ULONG_PTR)buf, 70};
    raise(code, 0, 2, args);
#endif
    exit(70);
  }
  zan_rt_soft_note2(prefix, msg);
}

void zan_rt_guard_fail3(const char *file, unsigned line, unsigned col,
                        const char *msg) {
  if (zan_soft_is_hard()) {
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
#if defined(_WIN32)
    void(WINAPI * raise)(DWORD, DWORD, DWORD, const ULONG_PTR *) =
        RaiseException;
    unsigned long code = 0xE0A2C010u;
    ULONG_PTR args[2] = {(ULONG_PTR)buf, 70};
    raise(code, 0, 2, args);
#endif
    exit(70);
  }
  zan_rt_soft_note3(file, line, col, msg);
}

static zan_fatal_fn g_fatal_handler;

void zan_rt_set_fatal_handler(zan_fatal_fn fn) { g_fatal_handler = fn; }

/* stdout 缓冲策略（编译器在 main 入口、任何输出之前调用一次；见 emit_main.c）：
 * 目标是普通文件（重定向到文件）时用 64 KiB 整块缓冲，其余（终端、管道、套接字）
 * 按编译器给的默认（POSIX 逐行 / Windows 不缓冲）。实时消费者（IDE 控制台、
 * 日志采集管道）不受影响。 */
void zan_rt_stdout_setup(FILE *out, int mode, long long size) {
    int to_file = 0;
#if defined(_WIN32)
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    to_file = h && h != INVALID_HANDLE_VALUE && GetFileType(h) == FILE_TYPE_DISK;
#elif !defined(__wasm__) && !defined(ZAN_BARE_METAL)
    struct stat st;
    to_file = fstat(fileno(out), &st) == 0 && S_ISREG(st.st_mode);
#endif
    if (to_file) setvbuf(out, NULL, _IOFBF, 65536);
    else setvbuf(out, NULL, mode, (size_t)size);
}

void zan_rt_fatal(const char *category, const char *message) {
  /* abort() 不冲刷 stdio：整块缓冲到文件时先把已写的输出落盘 */
  fflush(stdout);
  fprintf(stderr, "zan runtime: fatal (%s): %s\n",
          category ? category : "runtime", message ? message : "");
  fflush(stderr);
  zan_fatal_fn fn = g_fatal_handler;
  if (fn)
    fn(category, message);
  abort();
}

/* 头字可读性慢路径：生成代码在 string/数组上探测 [p-need, p) 的 ARC/串头字
 * 判定「托管还是外来」。p 自身可读，故 (p & 4095) >= need 时头字与 p 同页，
 * 编译器内联放行；只有 p 贴近页首（外来指针在独立映射页首、或托管块头恰在
 * 前一页末尾）才调到这里。用不会触发信号的「安全拷贝」系统调用试读：不可读
 * （未映射 / PROT_NONE）返回 0，生成代码按外来串处理（strlen、不碰 RC）；
 * 判不了（系统调用被沙箱拒绝等）返回 1，即回到旧行为。 */
#if defined(__APPLE__)
#include <mach/mach.h>
#include <dlfcn.h>
#include <mach-o/loader.h>
#elif defined(__linux__) && !defined(ZAN_BARE_METAL)
#include <sys/uio.h>
#endif

#if defined(__APPLE__)
/* String literals live in this image (const blob, __cstring), and which of
 * them start a page depends on layout: a hot literal there paid a
 * vm_read_overwrite on every length query (a published self-hosted compiler
 * spent 1 s of 4.8 s in the kernel). The image's readable segments stay
 * mapped for the process lifetime, so answer for them without a syscall. */
#define ZAN_IMG_MAX_SEGS 8
static uintptr_t g_img_seg_lo[ZAN_IMG_MAX_SEGS], g_img_seg_hi[ZAN_IMG_MAX_SEGS];
static int g_img_seg_n;
static int g_img_state; /* 0 = not scanned, 1 = scanned (atomic) */

static void zan_img_segments_init(void) {
    Dl_info di;
    int n = 0;
    if (dladdr((const void *)&zan_rt_hdr_readable, &di) && di.dli_fbase) {
        const struct mach_header_64 *mh = (const struct mach_header_64 *)di.dli_fbase;
        if (mh->magic == MH_MAGIC_64) {
            const struct load_command *lc = (const struct load_command *)(mh + 1);
            intptr_t slide = 0;
            int have_slide = 0;
            for (uint32_t i = 0; i < mh->ncmds; i++) {
                if (lc->cmd == LC_SEGMENT_64) {
                    const struct segment_command_64 *sc = (const struct segment_command_64 *)lc;
                    if (!have_slide && sc->fileoff == 0 && sc->filesize != 0) {
                        slide = (intptr_t)((uintptr_t)mh - (uintptr_t)sc->vmaddr);
                        have_slide = 1;
                    }
                }
                lc = (const struct load_command *)((const char *)lc + lc->cmdsize);
            }
            lc = (const struct load_command *)(mh + 1);
            for (uint32_t i = 0; have_slide && i < mh->ncmds && n < ZAN_IMG_MAX_SEGS; i++) {
                if (lc->cmd == LC_SEGMENT_64) {
                    const struct segment_command_64 *sc = (const struct segment_command_64 *)lc;
                    if ((sc->initprot & VM_PROT_READ) && sc->vmsize &&
                        strcmp(sc->segname, "__LINKEDIT") != 0) {
                        g_img_seg_lo[n] = (uintptr_t)sc->vmaddr + (uintptr_t)slide;
                        g_img_seg_hi[n] = g_img_seg_lo[n] + (uintptr_t)sc->vmsize;
                        n++;
                    }
                }
                lc = (const struct load_command *)((const char *)lc + lc->cmdsize);
            }
        }
    }
    g_img_seg_n = n;
    __atomic_store_n(&g_img_state, 1, __ATOMIC_RELEASE);
}

static int zan_in_image(uintptr_t lo, uintptr_t hi) {
    if (!__atomic_load_n(&g_img_state, __ATOMIC_ACQUIRE)) zan_img_segments_init();
    for (int i = 0; i < g_img_seg_n; i++)
        if (lo >= g_img_seg_lo[i] && hi <= g_img_seg_hi[i]) return 1;
    return 0;
}
#endif

int zan_rt_hdr_readable(const void *p, int64_t need) {
    if (!p || need <= 0) return 1;
    uintptr_t lo = (uintptr_t)p - (uintptr_t)need;
    if (lo > (uintptr_t)p) return 0;
#if defined(__APPLE__)
    if (zan_in_image(lo, (uintptr_t)p)) return 1;
#endif
#if defined(_WIN32)
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery((LPCVOID)lo, &mbi, sizeof mbi)) return 0;
    if (mbi.State != MEM_COMMIT) return 0;
    if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return 0;
    return 1;
#elif defined(__APPLE__)
    unsigned char buf[64];
    vm_size_t got = 0;
    if ((size_t)need > sizeof buf) need = (int64_t)sizeof buf;
    kern_return_t kr = vm_read_overwrite(mach_task_self(), (vm_address_t)lo,
        (vm_size_t)need, (vm_address_t)buf, &got);
    if (kr == KERN_SUCCESS) return 1;
    return (kr == KERN_INVALID_ADDRESS || kr == KERN_PROTECTION_FAILURE) ? 0 : 1;
#elif defined(__linux__) && !defined(ZAN_BARE_METAL)
    unsigned char buf[64];
    if ((size_t)need > sizeof buf) need = (int64_t)sizeof buf;
    struct iovec local = { buf, (size_t)need };
    struct iovec remote = { (void *)lo, (size_t)need };
    int saved = errno;
    ssize_t n = process_vm_readv(getpid(), &local, 1, &remote, 1, 0);
    int err = errno;
    errno = saved;
    if (n == (ssize_t)need) return 1;
    return (n >= 0 || err == EFAULT) ? 0 : 1;
#else
    return 1;
#endif
}
