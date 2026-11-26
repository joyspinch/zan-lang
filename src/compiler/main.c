#include "main_options_parse.h"
/* main */

#include "zan.h"
#include "arena.h"
#include "diag.h"
#include "lexer.h"
#include "parser.h"
#include "genmeta.h"
#include "nsresolve.h"
#include "ast.h"
#include "binder.h"
#include "checker.h"
#include "irgen.h"
#include "optimizer.h"
#include "build_cache.h"
#include "build_partition.h"
#include "procmem.h"
#include "crosscomp.h"
#include "winres.h"
#include "embedres.h"
#include "symbols.h"
#include "query.h"
#include "markup_check.h"
#include "package.h"
#include "win_utf8.h"
#include "../packaging/mobile_postlink.h"
#include <llvm-c/Comdat.h>
/* zan */
#include "zan_default_icon.h"
#include "zan_version.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include <time.h>
#include <errno.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#include <process.h>
#define fopen zan_utf8_fopen
#define remove zan_utf8_remove
#define rename zan_utf8_rename
#define system zan_utf8_system
#define strtok_r(str, delim, save) strtok_s((str), (delim), (save))
#ifndef S_ISDIR
#define S_ISDIR(m) (((m) & _S_IFMT) == _S_IFDIR)
#endif
#else
#include <unistd.h>
#include <dirent.h>
#include <strings.h>
#include <spawn.h>
#include <sys/wait.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#endif
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

#include "../common/host_oom.h"

#define ZAN_LINK_MAX_ARGV 8192
#define ZAN_MAX_USED_DRIVERS 1024
#define ZAN_MAX_STATIC_DRV_LIBS 2048
#define ZAN_LINK_ARGV_TAIL 32

typedef struct {
  char **paths;
  int count;
  int capacity;
} generated_object_vec_t;

static char link_work_dir[1024];
static char link_work_icon[1100];
static char link_work_output[1100];
static char link_work_package[1100];
static char link_work_package_sidecar[1120];
static char link_work_unsigned_package[1120];

static bool publish_link_output(const char *from, const char *to) {
#ifdef _WIN32
  wchar_t *wide_from = zan_utf8_to_wide_alloc(from);
  wchar_t *wide_to = zan_utf8_to_wide_alloc(to);
  bool ok = wide_from && wide_to &&
            MoveFileExW(wide_from, wide_to, MOVEFILE_REPLACE_EXISTING);
  free(wide_from);
  free(wide_to);
  return ok;
#else
  return rename(from, to) == 0;
#endif
}

static void clear_link_work_dir(void) {
  if (!link_work_dir[0])
    return;
  if (link_work_output[0])
    remove(link_work_output);
  if (link_work_icon[0])
    remove(link_work_icon);
  if (link_work_package[0])
    remove(link_work_package);
  if (link_work_package_sidecar[0])
    remove(link_work_package_sidecar);
  if (link_work_unsigned_package[0])
    remove(link_work_unsigned_package);
#ifdef _WIN32
  wchar_t *wide = zan_utf8_to_wide_alloc(link_work_dir);
  if (wide) {
    if (RemoveDirectoryW(wide))
      link_work_dir[0] = '\0';
    free(wide);
  }
#else
  if (rmdir(link_work_dir) == 0)
    link_work_dir[0] = '\0';
#endif
}

static bool create_link_work_dir(const char *output) {
#ifdef _WIN32
  char full_path[1024];
  if (!zan_utf8_full_path(output, full_path, sizeof(full_path)))
    return false;
  output = full_path;
#endif
  const char *sep = strrchr(output, '/');
#ifdef _WIN32
  const char *win_sep = strrchr(output, '\\');
  if (win_sep && (!sep || win_sep > sep))
    sep = win_sep;
#endif
  size_t parent_len = sep ? (size_t)(sep - output) + 1 : 0;
  if (parent_len + 64 >= sizeof(link_work_dir))
    return false;
#ifdef _WIN32
  bool created = false;
  for (unsigned attempt = 0; attempt < 64; attempt++) {
    snprintf(link_work_dir, sizeof(link_work_dir), "%.*s.zanc-%lu-%llu-%u",
             (int)parent_len, output, (unsigned long)GetCurrentProcessId(),
             (unsigned long long)GetTickCount64(), attempt);
    wchar_t *wide = zan_utf8_to_wide_alloc(link_work_dir);
    if (!wide)
      return false;
    created = CreateDirectoryW(wide, NULL) != 0;
    DWORD error = created ? ERROR_SUCCESS : GetLastError();
    free(wide);
    if (created)
      break;
    if (error != ERROR_ALREADY_EXISTS && error != ERROR_FILE_EXISTS)
      return false;
  }
  if (!created)
    return false;
#else
  snprintf(link_work_dir, sizeof(link_work_dir), "%.*s.zanc-XXXXXX",
           (int)parent_len, output);
  if (!mkdtemp(link_work_dir))
    return false;
#endif
  if (atexit(clear_link_work_dir) == 0)
    return true;
  clear_link_work_dir();
  return false;
}

static bool zan_is_zcomp_path(const char *p) {
  size_t n = strlen(p);
  return n > 6 && strcmp(p + n - 6, ".zcomp") == 0;
}

static bool zan_is_html_input(const char *p) {
  size_t n = strlen(p);
  return (n > 5 && strcmp(p + n - 5, ".html") == 0) ||
         (n > 4 && strcmp(p + n - 4, ".htm") == 0);
}

static void generated_object_vec_add(generated_object_vec_t *objects,
                                     const char *path) {
  if (objects->count == objects->capacity) {
    int next = objects->capacity ? objects->capacity * 2 : 4;
    char **grown =
        (char **)realloc(objects->paths, (size_t)next * sizeof(*grown));
    if (!grown) {
      fprintf(stderr, "error: out of memory tracking generated objects\n");
      exit(1);
    }
    objects->paths = grown;
    objects->capacity = next;
  }
  size_t len = strlen(path);
  objects->paths[objects->count] = (char *)malloc(len + 1);
  if (!objects->paths[objects->count]) {
    fprintf(stderr, "error: out of memory tracking generated object path\n");
    exit(1);
  }
  memcpy(objects->paths[objects->count], path, len + 1);
  objects->count++;
}

static void generated_object_vec_release(generated_object_vec_t *objects) {
  for (int i = 0; i < objects->count; i++)
    free(objects->paths[i]);
  free(objects->paths);
  objects->paths = NULL;
  objects->count = 0;
  objects->capacity = 0;
}

static void generated_object_vec_remove(generated_object_vec_t *objects,
                                        const char *single_fallback) {
  if (objects->count == 0) {
    remove(single_fallback);
  } else {
    for (int i = 0; i < objects->count; i++)
      remove(objects->paths[i]);
  }
  generated_object_vec_release(objects);
}

static void generated_object_vec_keep_or_remove(generated_object_vec_t *objects,
                                                const char *single_fallback,
                                                bool keep) {
  if (keep) {
    if (objects->count == 0) {
      fprintf(stderr, "note: failed-link object retained at '%s'\n",
              single_fallback);
    } else {
      for (int i = 0; i < objects->count; i++)
        fprintf(stderr, "note: failed-link object retained at '%s'\n",
                objects->paths[i]);
    }
    generated_object_vec_release(objects);
  } else {
    generated_object_vec_remove(objects, single_fallback);
  }
}

static void link_cap_exceeded(const char *what, int cap) {
  fprintf(stderr,
          "zanc: too many %s for one link (limit %d) -- raise the matching"
          " ZAN_LINK_MAX_* in src/compiler/main.c\n",
          what, cap);
  exit(1);
}

static void cmd_appendf(char *cmd, size_t cap, const char *fmt, ...) {
  size_t cur = strlen(cmd);
  int need = -1;
  if (cur < cap) {
    va_list ap;
    va_start(ap, fmt);
    need = vsnprintf(cmd + cur, cap - cur, fmt, ap);
    va_end(ap);
  }
  if (need < 0 || cur >= cap || (size_t)need >= cap - cur) {
    fprintf(stderr,
            "zanc: link command line exceeded its %d-byte buffer -- too"
            " many inputs for one link\n",
            (int)cap);
    exit(1);
  }
}

bool g_time_phases = false;
double g_phase_start = 0.0;

typedef struct {
  size_t file_reads;
  size_t bytes_read;
  size_t metadata_scans;
  size_t seed_sources;
  size_t seed_lex_passes;
  size_t throwaway_parses;
  size_t throwaway_parse_fallbacks;
  size_t real_parses;
  size_t secondary_parses;
  size_t metadata_cache_hits;
  size_t metadata_cache_misses;
  size_t metadata_cache_writes;
} zan_scale_stats_t;

static zan_scale_stats_t g_scale_stats;

double now_ms(void) {
#ifdef _WIN32
  LARGE_INTEGER f, c;
  QueryPerformanceFrequency(&f);
  QueryPerformanceCounter(&c);
  return (double)c.QuadPart * 1000.0 / (double)f.QuadPart;
#else
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
#endif
}

static zan_arena_t *g_main_arena = NULL;

static void phase(const char *name) {
  if (!g_time_phases)
    return;
  double t = now_ms();
  zan_procmem_t pm = zan_procmem_read();
  size_t mem_mb = pm.commit / (1024 * 1024), ws_mb = pm.ws / (1024 * 1024),
         peak_mb = pm.peak / (1024 * 1024);
  size_t ar_mb =
      g_main_arena ? (zan_arena_total_bytes(g_main_arena) / (1024 * 1024)) : 0;
  if (g_phase_start > 0.0)
    fprintf(stderr,
            "%10.1f ms  [Commit: %4zu MB, Peak: %4zu MB, WS: %4zu MB, Arena: "
            "%4zu MB]  %s\n",
            t - g_phase_start, mem_mb, peak_mb, ws_mb, ar_mb, name);
  g_phase_start = t;
}

static void report_suppressed_errors(const zan_diag_t *diag) {
  int dropped = zan_diag_suppressed_errors(diag);
  if (dropped > 0) {
    fprintf(stderr,
            "note: %d further error(s) were suppressed by the error limit"
            " (-ferror-limit=0 shows all)\n",
            dropped);
  }
}

static void probe_phase_mem(const char *name) {
  if (!getenv("ZAN_PROBE_MEM"))
    return;
  zan_procmem_t pm = zan_procmem_read();
  size_t ar_mb =
      g_main_arena ? (zan_arena_total_bytes(g_main_arena) / (1024 * 1024)) : 0;
  fprintf(
      stderr,
      "    [phase %-14s Commit: %4zu MB, Peak: %4zu MB, Arena: %4zu MB]  %s\n",
      "", pm.commit / (1024 * 1024), pm.peak / (1024 * 1024), ar_mb, name);
}

static char *read_file(const char *path, size_t *out_len) {
#ifdef _WIN32
  if (zan_utf8_is_directory(path)) {
#else
  struct stat st;
  if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) {
#endif
    fprintf(stderr, "error: '%s' is a directory, not a source file\n", path);
    return NULL;
  }
  FILE *f = fopen(path, "rb");
  if (!f) {
    fprintf(stderr, "error: cannot open file '%s'\n", path);
    return NULL;
  }
  fseek(f, 0, SEEK_END);
  long len = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (len < 0) {
    fclose(f);
    fprintf(stderr, "error: cannot read file '%s'\n", path);
    return NULL;
  }
  char *buf = (char *)malloc((size_t)len + 1);
  if (!buf) {
    fclose(f);
    return NULL;
  }
  size_t read = fread(buf, 1, (size_t)len, f);
  buf[read] = '\0';
  fclose(f);
  g_scale_stats.file_reads++;
  g_scale_stats.bytes_read += read;
  if (out_len)
    *out_len = read;
  return buf;
}

static char *read_file_arena(zan_arena_t *arena, const char *path,
                             size_t *out_len) {
#ifdef _WIN32
  if (zan_utf8_is_directory(path)) {
#else
  struct stat st;
  if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) {
#endif
    fprintf(stderr, "error: '%s' is a directory, not a source file\n", path);
    return NULL;
  }
  FILE *f = fopen(path, "rb");
  if (!f) {
    fprintf(stderr, "error: cannot open file '%s'\n", path);
    return NULL;
  }
  fseek(f, 0, SEEK_END);
  long len = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (len < 0) {
    fclose(f);
    fprintf(stderr, "error: cannot read file '%s'\n", path);
    return NULL;
  }
  char *buf = (char *)zan_arena_alloc(arena, (size_t)len + 1);
  if (!buf) {
    fclose(f);
    return NULL;
  }
  size_t read = fread(buf, 1, (size_t)len, f);
  buf[read] = '\0';
  fclose(f);
  g_scale_stats.file_reads++;
  g_scale_stats.bytes_read += read;
  if (out_len)
    *out_len = read;
  return buf;
}

#include "driver/main_sources.c"
#include "driver/main_semantic_refs.c"

#include "driver/main_prune.c"
#include "driver/main_synth.c"

static void zan_apply_lex_defines(zan_lexer_t *lex, zan_target_t target,
                                  const char *const *pp_defines,
                                  int pp_define_count, bool is_publish) {
  switch (target.os) {
  case ZAN_OS_WINDOWS:
    zan_lexer_define(lex, "WINDOWS", "1");
    zan_lexer_define(lex, "WIN32", "1");
    break;
  case ZAN_OS_LINUX:
    zan_lexer_define(lex, "LINUX", "1");
    break;
  case ZAN_OS_ANDROID:
    zan_lexer_define(lex, "LINUX", "1");
    zan_lexer_define(lex, "ANDROID", "1");
    break;
  case ZAN_OS_OHOS:
    zan_lexer_define(lex, "LINUX", "1");
    zan_lexer_define(lex, "MUSL", "1");
    zan_lexer_define(lex, "OHOS", "1");
    break;
  case ZAN_OS_MACOS:
    zan_lexer_define(lex, "MACOS", "1");
    zan_lexer_define(lex, "APPLE", "1");
    break;
  case ZAN_OS_IOS:
    zan_lexer_define(lex, "IOS", "1");
    zan_lexer_define(lex, "APPLE", "1");
    break;
  default:
    break;
  }
  if (target.arch == ZAN_ARCH_AARCH64)
    zan_lexer_define(lex, "ARM64", "1");
  else if (target.arch == ZAN_ARCH_X86_64)
    zan_lexer_define(lex, "X86_64", "1");
  else if (target.arch == ZAN_ARCH_RISCV64)
    zan_lexer_define(lex, "RISCV64", "1");
  else if (target.arch == ZAN_ARCH_RISCV32)
    zan_lexer_define(lex, "RISCV32", "1");
  else if (target.arch == ZAN_ARCH_WASM32)
    zan_lexer_define(lex, "WASM32", "1");
  if (target.os == ZAN_OS_WASI)
    zan_lexer_define(lex, "WASI", "1");
  if (target.abi == ZAN_ABI_MUSL)
    zan_lexer_define(lex, "MUSL", "1");
  zan_lexer_define(lex, "ZAN", "1");
  for (int di = 0; di < pp_define_count; di++) {
    char dname[64];
    const char *dval = "1";
    const char *eq = strchr(pp_defines[di], '=');
    if (eq) {
      int nl = (int)(eq - pp_defines[di]);
      if (nl > 63)
        nl = 63;
      memcpy(dname, pp_defines[di], nl);
      dname[nl] = '\0';
      dval = eq + 1;
    } else {
      strncpy(dname, pp_defines[di], 63);
      dname[63] = '\0';
    }
    zan_lexer_define(lex, dname, dval);
  }
  if (is_publish) {
    zan_lexer_define(lex, "PUBLISH", "1");
    zan_lexer_define(lex, "RELEASE", "1");
  } else {
    zan_lexer_define(lex, "DEBUG", "1");
  }
}

static zan_ast_node_t *
parse_secondary_unit(const char *path, zan_target_t target,
                     const char *const *pp_defines, int pp_define_count,
                     bool is_publish, zan_arena_t *arena, zan_diag_t *diag) {
  g_scale_stats.secondary_parses++;
  size_t slen = 0;
  char *src = read_file(path, &slen);
  if (!src)
    return NULL;
  char *heap_src = src;
  src = zan_arena_strdup(arena, heap_src, slen);
  free(heap_src);
  if (!src)
    return NULL;
  int file_id = diag->file_count;
  zan_diag_add_file(diag, path, src);
  zan_lexer_t lex;
  zan_lexer_init(&lex, src, slen, file_id, arena, diag);
  zan_apply_lex_defines(&lex, target, pp_defines, pp_define_count, is_publish);
  zan_parser_t parser;
  zan_parser_init(&parser, &lex, arena, diag);
  zan_ast_node_t *unit = zan_parser_parse(&parser);
  return unit;
}

#include "driver/main_dump.c"
#include "driver/main_driver.c"

/* 输入源里出现 Gui 窗口类记号（" : Form"，B-ID149 窗口即对象形态）
 * 才启用自动内嵌：服务端/web 程序的 html 是运行期盘上资产，绝不能
 * 烘死进 exe。 */
static bool zan_inputs_use_design_doc(const char *const *input_files,
                                      int input_count) {
  for (int fi = 0; fi < input_count; fi++) {
    if (zan_is_zcomp_path(input_files[fi]))
      continue;
    size_t len = 0;
    char *src = read_file(input_files[fi], &len);
    if (!src)
      continue;
    bool hit = strstr(src, " : Form") != NULL ||
               strstr(src, " : ChildWindow") != NULL;
    free(src);
    if (hit)
      return true;
  }
  return false;
}

/* 输入文件所属工程根：向上找最近的 zan.proj 祖先目录；散文件编译
 * 退回输入所在目录。不向上吞 packages/ 祖先（package_project_root
 * 的规则会把仓库内构建的整个仓库指成设计根）。 */
static void zan_design_root_for(const char *input, char *out, size_t sz) {
  char abs[1024];
#ifdef _WIN32
  if (!zan_utf8_full_path(input, abs, sizeof(abs)))
    snprintf(abs, sizeof(abs), "%s", input);
#else
  char *rp = realpath(input, NULL);
  snprintf(abs, sizeof(abs), "%s", rp ? rp : input);
  free(rp);
#endif
  char dir[1024];
  snprintf(dir, sizeof(dir), "%s", abs);
  char *fwd = strrchr(dir, '/');
  char *back = strrchr(dir, '\\');
  char *sep = (fwd && fwd > back) ? fwd : back;
  if (sep)
    *sep = '\0';
  else
    snprintf(dir, sizeof(dir), ".");
  for (;;) {
    char probe[1200];
    snprintf(probe, sizeof(probe), "%s/zan.proj", dir);
    if (zan_file_exists(probe))
      break;
    fwd = strrchr(dir, '/');
    back = strrchr(dir, '\\');
    char *last = (fwd && fwd > back) ? fwd : back;
    if (!last)
      break;
    if (last == dir) {
      if (dir[1] == '\0')
        break;
      dir[1] = '\0';
      continue;
    }
    *last = '\0';
  }
  snprintf(out, sz, "%s", dir);
}

static int zan_run_archiver(const char *tool, const char *format,
                            const char *arc, const char *obj) {
  const char *argv[] = {tool, format, "rcsD", arc, obj, NULL};
#ifdef _WIN32
  wchar_t *wide_tool = zan_utf8_to_wide_alloc(tool);
  if (!wide_tool)
    return 1;
  DWORD needed = SearchPathW(NULL, wide_tool, L".exe", 0, NULL, NULL);
  if (!needed) {
    free(wide_tool);
    return -1;
  }
  wchar_t *wide_path = malloc((size_t)needed * sizeof(*wide_path));
  if (!wide_path) {
    free(wide_tool);
    return 1;
  }
  DWORD written =
      SearchPathW(NULL, wide_tool, L".exe", needed, wide_path, NULL);
  free(wide_tool);
  if (!written || written >= needed) {
    free(wide_path);
    return -1;
  }
  char *path = zan_wide_to_utf8_alloc(wide_path);
  free(wide_path);
  if (!path)
    return 1;
  argv[0] = path;
  intptr_t rc = zan_utf8_spawnv(_P_WAIT, path, (const char *const *)argv);
  if (rc < 0)
    fprintf(stderr, "error: cannot start archiver '%s': %s\n", path,
            strerror(errno));
  free(path);
  return rc == 0 ? 0 : 1;
#else
  extern char **environ;
  pid_t child;
  int rc = posix_spawnp(&child, tool, NULL, NULL, (char *const *)argv, environ);
  if (rc == ENOENT || rc == ENOTDIR)
    return -1;
  if (rc != 0) {
    fprintf(stderr, "error: cannot start archiver '%s': %s\n", tool,
            strerror(rc));
    return 1;
  }
  int status;
  while (waitpid(child, &status, 0) < 0) {
    if (errno == EINTR)
      continue;
    fprintf(stderr, "error: cannot wait for archiver '%s': %s\n", tool,
            strerror(errno));
    return 1;
  }
  return WIFEXITED(status) && WEXITSTATUS(status) == 0 ? 0 : 1;
#endif
}

static int zan_write_static_lib(const char *obj, const char *arc,
                                zan_os_t target_os) {
  char exe_dir[1024], bundled[1200] = {0}, versioned[64];
  zan_exe_dir(exe_dir, sizeof(exe_dir));
#ifdef _WIN32
  const char *suffix = ".exe";
#else
  const char *suffix = "";
#endif
  if (exe_dir[0])
    snprintf(bundled, sizeof(bundled), "%s/llvm-ar%s", exe_dir, suffix);
  snprintf(versioned, sizeof(versioned), "llvm-ar-%d%s", ZAN_LLVM_MAJOR,
           suffix);
  const char *env_ar = getenv("ZAN_LLVM_AR");
  if (!env_ar || !*env_ar)
    env_ar = getenv("LLVM_AR");
  const char *tools[] = {env_ar ? env_ar : "",
                         bundled,
                         "llvm-ar",
                         versioned,
#ifdef __APPLE__
                         "/opt/homebrew/opt/llvm/bin/llvm-ar",
                         "/usr/local/opt/llvm/bin/llvm-ar",
#endif
                         NULL};
  const char *format = (target_os == ZAN_OS_MACOS || target_os == ZAN_OS_IOS)
                           ? "--format=darwin"
                           : "--format=gnu";
  size_t tmp_size = strlen(obj) + sizeof(".a");
  char *tmp = malloc(tmp_size);
  if (!tmp)
    return -1;
  snprintf(tmp, tmp_size, "%s.a", obj);
  if (remove(tmp) != 0 && errno != ENOENT) {
    fprintf(stderr, "error: cannot replace archive temporary '%s': %s\n", tmp,
            strerror(errno));
    free(tmp);
    return -1;
  }
  int rc = -1;
  for (int i = 0; tools[i]; i++) {
    if (!tools[i][0])
      continue;
    rc = zan_run_archiver(tools[i], format, tmp, obj);
    if (rc >= 0)
      break;
  }
  if (rc < 0)
    fprintf(stderr,
            "error: static library emission requires llvm-ar; install LLVM "
            "on PATH or place llvm-ar beside zanc\n");
  if (rc == 0 && !publish_link_output(tmp, arc))
    rc = -1;
  remove(tmp);
  free(tmp);
  return rc == 0 ? 0 : -1;
}

#include "driver/main_options.c"
#include "driver/main_ir_object.c"

static int zan_path_under_dir_folded(const char *path, const char *root) {
  if (!path || !path[0] || !root || !root[0])
    return 0;
  char full_path[4096];
#ifdef _WIN32
  if (zan_utf8_full_path(path, full_path, sizeof(full_path)))
    path = full_path;
#else
  if (realpath(path, full_path))
    path = full_path;
#endif
  size_t rl = strlen(root);
  while (rl > 0 && (root[rl - 1] == '/' || root[rl - 1] == '\\'))
    rl--;
  if (rl == 0)
    return 0;
  for (size_t k = 0; k < rl; k++) {
    char a = path[k], b = root[k];
    if (a == '\0')
      return 0;
    if (a == '\\')
      a = '/';
    if (b == '\\')
      b = '/';
#ifdef _WIN32
    if (a >= 'A' && a <= 'Z')
      a = (char)(a - 'A' + 'a');
    if (b >= 'A' && b <= 'Z')
      b = (char)(b - 'A' + 'a');
#endif
    if (a != b)
      return 0;
  }
  char nc = path[rl];
  return nc == '\0' || nc == '/' || nc == '\\';
}

static bool zan_create_gen_work_dir(const char *anchor, char *out,
                                    size_t out_cap) {
  if (!anchor || !anchor[0] || !out || out_cap < 64)
    return false;
  char full_path[1024];
#ifdef _WIN32
  if (zan_utf8_full_path(anchor, full_path, sizeof(full_path)))
    anchor = full_path;
#endif
  const char *sep = strrchr(anchor, '/');
#ifdef _WIN32
  const char *win_sep = strrchr(anchor, '\\');
  if (win_sep && (!sep || win_sep > sep))
    sep = win_sep;
#endif
  size_t parent_len = sep ? (size_t)(sep - anchor) + 1 : 0;
  if (parent_len + 64 >= out_cap)
    return false;
#ifdef _WIN32
  for (unsigned attempt = 0; attempt < 64; attempt++) {
    snprintf(out, out_cap, "%.*s.zanc-gen-%lu-%llu-%u", (int)parent_len, anchor,
             (unsigned long)GetCurrentProcessId(),
             (unsigned long long)GetTickCount64(), attempt);
    wchar_t *wide = zan_utf8_to_wide_alloc(out);
    if (!wide)
      return false;
    BOOL ok = CreateDirectoryW(wide, NULL);
    DWORD err = ok ? ERROR_SUCCESS : GetLastError();
    free(wide);
    if (ok)
      return true;
    if (err != ERROR_ALREADY_EXISTS && err != ERROR_FILE_EXISTS)
      return false;
  }
  return false;
#else
  snprintf(out, out_cap, "%.*s.zanc-gen-XXXXXX", (int)parent_len, anchor);
  return mkdtemp(out) != NULL;
#endif
}

static void zan_remove_dir_empty(const char *dir) {
  if (!dir || !dir[0])
    return;
#ifdef _WIN32
  wchar_t *wide = zan_utf8_to_wide_alloc(dir);
  if (wide) {
    RemoveDirectoryW(wide);
    free(wide);
  }
#else
  rmdir(dir);
#endif
}

static void zan_resolve_pkg_generator(const char *tool, const char *pkg_dir,
                                      char *out, size_t out_cap) {
  out[0] = '\0';
  if (!tool || !tool[0])
    return;
  char exe_dir[1024] = {0};
  zan_exe_dir(exe_dir, sizeof(exe_dir));
#ifdef _WIN32
  const bool has_exe =
      strlen(tool) >= 4 && _stricmp(tool + strlen(tool) - 4, ".exe") == 0;
  const char *cands[4] = {0};
  char b0[1200] = {0}, b1[1200] = {0}, b2[1200] = {0}, b3[1200] = {0};
  int nc = 0;
  if (exe_dir[0]) {
    if (!has_exe) {
      snprintf(b0, sizeof(b0), "%s/%s.exe", exe_dir, tool);
      cands[nc++] = b0;
    }
    snprintf(b1, sizeof(b1), "%s/%s", exe_dir, tool);
    cands[nc++] = b1;
  }
  if (pkg_dir && pkg_dir[0]) {
    if (!has_exe) {
      snprintf(b2, sizeof(b2), "%s/%s.exe", pkg_dir, tool);
      cands[nc++] = b2;
    }
    snprintf(b3, sizeof(b3), "%s/%s", pkg_dir, tool);
    cands[nc++] = b3;
  }
#else
  const char *cands[2] = {0};
  char b0[1200] = {0}, b1[1200] = {0};
  int nc = 0;
  if (exe_dir[0]) {
    snprintf(b0, sizeof(b0), "%s/%s", exe_dir, tool);
    cands[nc++] = b0;
  }
  if (pkg_dir && pkg_dir[0]) {
    snprintf(b1, sizeof(b1), "%s/%s", pkg_dir, tool);
    cands[nc++] = b1;
  }
#endif
  for (int i = 0; i < nc; i++) {
    if (cands[i][0] && zan_file_exists(cands[i])) {
      snprintf(out, out_cap, "%s", cands[i]);
      return;
    }
  }
  snprintf(out, out_cap, "%s", tool);
}

static int zan_run_pkg_generator(const char *tool, const char *meta_path,
                                 const char *out_path) {
  const char *argv[] = {tool, "--generate", "auto", meta_path, out_path, NULL};
#ifdef _WIN32
  char *resolved = NULL;
  if (!strchr(tool, '/') && !strchr(tool, '\\')) {
    wchar_t *wide_tool = zan_utf8_to_wide_alloc(tool);
    if (!wide_tool)
      return -1;
    DWORD needed = SearchPathW(NULL, wide_tool, L".exe", 0, NULL, NULL);
    if (!needed) {
      free(wide_tool);
      return -1;
    }
    wchar_t *wide_path = malloc((size_t)needed * sizeof(*wide_path));
    if (!wide_path) {
      free(wide_tool);
      return -1;
    }
    DWORD written =
        SearchPathW(NULL, wide_tool, L".exe", needed, wide_path, NULL);
    free(wide_tool);
    if (!written || written >= needed) {
      free(wide_path);
      return -1;
    }
    resolved = zan_wide_to_utf8_alloc(wide_path);
    free(wide_path);
    if (!resolved)
      return -1;
    argv[0] = resolved;
  } else if (!zan_file_exists(tool)) {
    return -1;
  }
  intptr_t rc = zan_utf8_spawnv(_P_WAIT, argv[0], argv);
  free(resolved);
  return rc < 0 ? -1 : (int)rc;
#else
  extern char **environ;
  pid_t child;
  int rc = posix_spawnp(&child, tool, NULL, NULL, (char *const *)argv, environ);
  if (rc == ENOENT || rc == ENOTDIR)
    return -1;
  if (rc != 0)
    return 1;
  int status;
  while (waitpid(child, &status, 0) < 0) {
    if (errno == EINTR)
      continue;
    return 1;
  }
  if (!WIFEXITED(status))
    return 1;
  return WEXITSTATUS(status);
#endif
}

int main(int argc, char **argv) {
#ifdef _WIN32
  char **utf8_argv = zan_utf8_command_line_argv(&argc);
  if (utf8_argv)
    argv = utf8_argv;
#endif
  {
    /* ZAN_LLVM_ARGS：空格分隔的额外 LLVM 选项（如 "-time-passes"），
     * 编译器自身性能诊断用。 */
    const char *cl[34] = {"zanc", "--wasm-enable-eh"};
    int ncl = 2;
    static char extra[1024];
    const char *env = getenv("ZAN_LLVM_ARGS");
    if (env && env[0]) {
      snprintf(extra, sizeof(extra), "%s", env);
      for (char *t = strtok(extra, " "); t && ncl < 33; t = strtok(NULL, " "))
        cl[ncl++] = t;
    }
    cl[ncl] = NULL;
    LLVMParseCommandLineOptions(ncl, cl, "");
  }
  expand_arg_files(&argc, &argv);
  if (argc > 1 && strcmp(argv[1], "--compile-ir") == 0)
    return compile_ir_object(argc, argv);

  zan_bcache_init(argc, argv);
  zan_options_t opts;
  zan_options_status_t opt_st = zan_options_parse(argc, argv, &opts);
  if (opt_st != ZAN_OPTIONS_OK) {
    return opts.exit_code;
  }

  const char *input_file = opts.input_file;
  const char **input_files = opts.input_files;
  int input_count = opts.input_count;
  int input_cap = opts.input_cap;
  int explicit_input_count = opts.explicit_input_count;
  const char *output_file = opts.output_file;
  bool do_dump_tokens = opts.do_dump_tokens;
  bool do_dump_ast = opts.do_dump_ast;
  const char *emit_symbols_path = opts.emit_symbols_path;
  bool do_summary = opts.do_summary;
  const char *emit_deps_path = opts.emit_deps_path;
  const char *query_name = opts.query_name;
  const char *gen_meta_path = opts.gen_meta_path;
  bool gen_meta_bodies = opts.gen_meta_bodies;
  bool do_emit_ir = opts.do_emit_ir;
  int check_leaks_opt = opts.check_leaks_opt;
  bool checked_arith = opts.checked_arith;
  bool fp_contract = opts.fp_contract;
  int arc_guard_opt = opts.arc_guard_opt;
  int runtime_checks_opt = opts.runtime_checks_opt;
  bool runtime_checks = opts.runtime_checks;
  bool strict_runtime = opts.strict_runtime;
  bool publish_mode = opts.publish_mode;
  bool no_pullin_filter = opts.no_pullin_filter;
  int obfuscate_strings_opt = opts.obfuscate_strings_opt;
  bool hide_diagnostics = opts.hide_diagnostics;
  const char *site_map_path = opts.site_map_path;
  bool strip_symbols = opts.strip_symbols;
  int error_limit = opts.error_limit;
  bool debug_info = opts.debug_info;
  int fast_alloc_opt = opts.fast_alloc_opt;
  const char *stdlib_path = opts.stdlib_path;
  bool auto_stdlib = opts.auto_stdlib;
  bool packages_disabled = opts.packages_disabled;
  bool no_pkg_gen = opts.no_pkg_gen;
  bool quiet = opts.quiet;
  const char *package_project = opts.package_project;
  bool package_list_missing = opts.package_list_missing;
  bool do_deny_warnings = opts.do_deny_warnings;
  const char *warn_level_arg = opts.warn_level_arg;
  const char *suppress_arg = opts.suppress_arg;
  int opt_level = opts.opt_level;
  zan_opt_knobs_t opt_knobs = opts.opt_knobs;
  const char **pp_defines = (const char **)opts.pp_defines;
  int pp_define_count = opts.pp_define_count;
  const char *target_name = opts.target_name;
  bool link_static_drivers = opts.link_static_drivers;
  const char *driver_dir_override = opts.driver_dir_override;
  const char *link_subsystem = opts.link_subsystem;
  const char *icon_path = opts.icon_path;
  bool no_icon = opts.no_icon;
  bool default_icon_explicit = opts.default_icon_explicit;
  bool emit_lib = opts.emit_lib;
  bool lib_shared = opts.lib_shared;
  const char *apk_path = opts.apk_path;
  const char *apk_package = opts.apk_package;
  const char *apk_label = opts.apk_label;
  const char *ipa_path = opts.ipa_path;
  const char *ipa_bundle_id = opts.ipa_bundle_id;
  const char *ipa_name = opts.ipa_name;
  const char **extra_link_inputs = (const char **)opts.extra_link_inputs;
  int extra_link_input_count = opts.extra_link_input_count;
  const char **embed_specs = (const char **)opts.embed_specs;
  int embed_spec_count = opts.embed_spec_count;
  const char **extra_link_libs = (const char **)opts.extra_link_libs;
  int extra_link_lib_count = opts.extra_link_lib_count;
  const char **extra_lib_paths = (const char **)opts.extra_lib_paths;
  int extra_lib_path_count = opts.extra_lib_path_count;
  char resolved_stdlib_root[1024];
  memcpy(resolved_stdlib_root, opts.resolved_stdlib_root,
         sizeof(resolved_stdlib_root));

  if (input_count == 0) {
    fprintf(stderr, "error: no input file\n");
    return 1;
  }

  for (int fi = 0; fi < input_count; fi++) {
    if (zan_is_html_input(input_files[fi])) {
      fprintf(stderr,
              "error: '%s': the compile-time HTML channel has been "
              "removed; build the window with Gui widgets and load "
              "markup at runtime via Gui.App.LoadHtml\n",
              input_files[fi]);
      return 1;
    }
  }

  input_file = input_files[0];
  char user_resource_root[4096];
  resolve_user_resource_root(input_file, user_resource_root,
                             sizeof(user_resource_root));
  if (package_project)
    snprintf(package_project_root, sizeof(package_project_root), "%s",
             package_project);
  else if (!packages_disabled)
    resolve_package_project_root(input_file);
  for (int fi = 0; fi < input_count; fi++) {
    if (zan_is_zcomp_path(input_files[fi]))
      continue;
    size_t nlen = 0;
    char *nsrc = read_file(input_files[fi], &nlen);
    if (!nsrc)
      continue;
    scan_namespace_tokens(nsrc, nlen);
    free(nsrc);
  }
  if (project_root_has_manifest()) {
    scan_project_namespaces(package_project_root);
  }

  zan_target_t target;
  if (target_name) {
    if (!zan_target_parse(target_name, &target)) {
      fprintf(stderr,
              "error: target '%s' is not available in this build "
              "(see --list-targets)\n",
              target_name);
      return 1;
    }
  } else {
    zan_target_host(&target);
  }
  if (opts.cpu_profile && target.arch != ZAN_ARCH_X86_64) {
    fprintf(stderr,
            "error: --cpu-profile is supported only for x86-64 targets\n");
    return 1;
  }
  bool cross_compiling = (target_name != NULL);
  bool external_async_executor =
      (target.os == ZAN_OS_WINDOWS || target.os == ZAN_OS_LINUX ||
       target.os == ZAN_OS_MACOS || target.os == ZAN_OS_IOS) &&
      (target.arch == ZAN_ARCH_X86_64 || target.arch == ZAN_ARCH_AARCH64);

  if (auto_stdlib || stdlib_path) {
    char stdlib_root[1024];
    if (stdlib_path) {
      snprintf(stdlib_root, sizeof(stdlib_root), "%s", stdlib_path);
    } else {
#ifdef _WIN32
      char exe_path[1024];
      GetModuleFileNameA(NULL, exe_path, sizeof(exe_path));
      char *last_sep = strrchr(exe_path, '\\');
      if (last_sep)
        *last_sep = '\0';
      snprintf(stdlib_root, sizeof(stdlib_root), "%s\\..\\stdlib", exe_path);
      {
        DWORD attr = zan_utf8_get_file_attributes(stdlib_root);
        if (attr == INVALID_FILE_ATTRIBUTES ||
            !(attr & FILE_ATTRIBUTE_DIRECTORY)) {
          snprintf(stdlib_root, sizeof(stdlib_root), "%s\\stdlib", exe_path);
        }
      }
#elif defined(__APPLE__)
      char exe_path[1024];
      uint32_t exe_sz = sizeof(exe_path);
      if (_NSGetExecutablePath(exe_path, &exe_sz) == 0) {
        char *last_sep = strrchr(exe_path, '/');
        if (last_sep)
          *last_sep = '\0';
        snprintf(stdlib_root, sizeof(stdlib_root), "%s/../stdlib", exe_path);
      } else {
        snprintf(stdlib_root, sizeof(stdlib_root), "stdlib");
      }
#else
      char exe_path[1024];
      ssize_t elen = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
      if (elen > 0) {
        exe_path[elen] = '\0';
        char *last_sep = strrchr(exe_path, '/');
        if (last_sep)
          *last_sep = '\0';
        snprintf(stdlib_root, sizeof(stdlib_root), "%s/../stdlib", exe_path);
      } else {
        snprintf(stdlib_root, sizeof(stdlib_root), "stdlib");
      }
#endif
    }
    {
      char norm_root[4096];
#ifdef _WIN32
      if (zan_utf8_full_path(stdlib_root, norm_root, sizeof(norm_root)))
        snprintf(stdlib_root, sizeof(stdlib_root), "%s", norm_root);
#else
      if (realpath(stdlib_root, norm_root))
        snprintf(stdlib_root, sizeof(stdlib_root), "%s", norm_root);
#endif
    }
    snprintf(resolved_stdlib_root, sizeof(resolved_stdlib_root), "%s",
             stdlib_root);

#ifdef _WIN32
    {
      DWORD root_attr = zan_utf8_get_file_attributes(stdlib_root);
      if (auto_stdlib && !stdlib_path &&
          (root_attr == INVALID_FILE_ATTRIBUTES ||
           !(root_attr & FILE_ATTRIBUTE_DIRECTORY)))
        fprintf(stderr,
                "warning: --auto-stdlib: no stdlib directory found next "
                "to the compiler (tried %s); `using` directives will not "
                "resolve stdlib namespaces (pass --stdlib-path)\n",
                stdlib_root);
    }
#endif

    explicit_input_count = input_count;

    pi_filter_active = !no_pullin_filter && !emit_symbols_path &&
                       getenv("ZAN_NO_PULLIN_FILTER") == NULL;
    if (pi_filter_active) {
      pi_arena = zan_arena_new();
      pi_target = target;
      pi_pp_defines = pp_defines;
      pi_pp_define_count = pp_define_count;
      pi_publish_mode = publish_mode;
      pi_stdlib_root_buf = resolved_stdlib_root;
    } else {
      int scanned = 0;
      while (scanned < input_count) {
        int round_end = input_count;
        for (int fi = scanned; fi < round_end; fi++) {
          if (zan_is_zcomp_path(input_files[fi]))
            continue;
          pi_reach_input_dir(input_files[fi]);
          size_t slen3 = 0;
          char *src3 = read_file(input_files[fi], &slen3);
          if (!src3)
            continue;
          scan_using_tokens(src3, strlen(src3), stdlib_root, &input_files,
                            &input_count, &input_cap);
          free(src3);
        }
        scanned = round_end;
      }
    }
  }

  if (package_list_missing) {
    fprintf(stderr, "ZANPKG_STATUS action=list-missing status=ok count=%d\n",
            missing_namespace_count);
    return missing_namespace_count ? 3 : 0;
  }

  phase("scan inputs");

  size_t source_len;
  char *source = read_file(input_file, &source_len);
  if (!source)
    return 1;
  if (do_dump_tokens) {
    dump_tokens(source, source_len, input_file, opts.diagnostics_jsonl);
    free(source);
    return 0;
  }

#include "driver/main_frontend.c"

#include "driver/main_codegen.c"

#include "driver/main_linker.c"

  phase("link");
  probe_phase_mem("link");

  bool keep_failed_obj =
      link_ret != 0 && getenv("ZAN_KEEP_FAILED_OBJ") != NULL;
  generated_object_vec_keep_or_remove(&generated_objects, obj_tmp,
                                      keep_failed_obj);

  bool publish_failed = link_ret == 0 && !emit_lib && !apk_path && !ipa_path &&
                        !publish_link_output(obj_path, final_link_path);
  if (link_ret != 0 || publish_failed) {
    if (!keep_failed_obj)
      clear_link_work_dir();
    if (publish_failed)
      fprintf(stderr, "error: cannot publish linked output '%s'\n",
              final_link_path);
    else
      fprintf(stderr, "error: linking failed\n");
    zan_diag_free_buffers(irgen.diag);
    zan_irgen_destroy(&irgen);
    zan_arena_free(ir_arena);
    zan_arena_free(arena);
    free(source);
    return 1;
  }

  if ((publish_mode || target.os == ZAN_OS_WINDOWS) && used_driver_count > 0) {
    char outdir[1024];
    snprintf(outdir, sizeof(outdir), "%s", final_link_path);
    {
      char *s1 = strrchr(outdir, '/');
      char *s2 = strrchr(outdir, '\\');
      char *s = (s1 > s2) ? s1 : s2;
      if (s)
        *s = '\0';
      else
        snprintf(outdir, sizeof(outdir), ".");
    }

    bool win_target = (target.os == ZAN_OS_WINDOWS);
    const char *dsub = zan_driver_subdir(&target);
    zan_driver_bundle_context_t bundle_ctx = {.reg = &driver_reg,
                                              .irgen = &irgen,
                                              .prefixes = &driver_prefixes,
                                              .subdir = dsub,
                                              .outdir = outdir,
                                              .quiet = quiet};
    for (int d = 0; d < used_driver_count; d++) {
      const char *driver_dir = driver_dirs[d];
      if (!driver_dir[0])
        continue;
      if (used_driver_static[d] && !used_driver_runtime[d])
        continue;
      char drv[64];
      snprintf(drv, sizeof(drv), "%.*s", used_driver_len[d], used_drivers[d]);

      char cands[64][128];
      int ncand = 0;
      char manifest[1200];
      snprintf(manifest, sizeof(manifest), "%s/%s.bundle", driver_dir, drv);
      if (getenv("ZAN_DEBUG_LINK")) {
        fprintf(stderr, "[DEBUG] checking manifest %s\n", manifest);
        fflush(stderr);
      }
      FILE *mf = fopen(manifest, "rb");
      if (mf) {
        char line[128];
        while (fgets(line, sizeof(line), mf)) {
          if (ncand >= 64) {
            fclose(mf);
            link_cap_exceeded("files in a driver bundle "
                              "manifest",
                              64);
          }
          const char *prefix;
          char *entry = zan_driver_bundle_entry(line, &prefix);
          if (!entry || (prefix && !zan_driver_prefix_live(
                                       &irgen, &driver_prefixes, prefix)))
            continue;
          {
            if (strncmp(entry, "@driver/", 8) == 0) {
              const char *dep = entry + 8;
              size_t dl = strlen(dep);
              if (dl == 0 || dl >= 64 || !zan_is_safe_bundle_name(dep)) {
                fprintf(stderr,
                        "warning: ignoring unsafe "
                        "@driver entry '%s' in %s (must be a "
                        "bare driver name)\n",
                        entry, manifest);
              } else if (ncand < 64) {
                snprintf(cands[ncand++], sizeof(cands[0]), "@driver/%s", dep);
              }
            } else if (zan_is_safe_bundle_name(entry)) {
              snprintf(cands[ncand++], sizeof(cands[0]), "%s", entry);
            } else {
              fprintf(stderr,
                      "warning: ignoring unsafe entry "
                      "'%s' in %s (must be a bare filename)\n",
                      entry, manifest);
            }
          }
        }
        fclose(mf);
      } else if (win_target) {
        snprintf(cands[ncand++], sizeof(cands[0]), "%s.dll", drv);
        snprintf(cands[ncand++], sizeof(cands[0]), "lib%s.dll", drv);
      } else if (target.os == ZAN_OS_MACOS) {
        snprintf(cands[ncand++], sizeof(cands[0]), "lib%s.dylib", drv);
      } else {
        snprintf(cands[ncand++], sizeof(cands[0]), "lib%s.so", drv);
      }

      int copied = 0;
      int dself = 0;
      int copy_failed_count = 0;
      for (int c = 0; c < ncand; c++) {
        char src[1300], dst[1300];
        if (getenv("ZAN_DEBUG_LINK")) {
          fprintf(stderr, "[DEBUG] candidate[%d] = %s\n", c, cands[c]);
          fflush(stderr);
        }
        if (used_driver_embedded[d] &&
            strcmp(cands[c], embedded_driver_file[d]) == 0) {
          if (!quiet)
            printf("  embedded driver '%s' ? %s (inside the "
                   "executable)\n",
                   drv, cands[c]);
          copied++;
          continue;
        }
        bool got = false;
        if (strncmp(cands[c], "@driver/", 8) == 0) {
          got = zan_bundle_dependency(&bundle_ctx, cands[c] + 8, manifest,
                                      &copy_failed_count) > 0;
        } else {
          snprintf(src, sizeof(src), "%s/%s", driver_dir, cands[c]);
          snprintf(dst, sizeof(dst), "%s/%s", outdir, cands[c]);
          if (zan_file_exists(src)) {
            char errbuf[256];
            if (zan_copy_file_ex(src, dst, errbuf, sizeof(errbuf)) == 0) {
              got = true;
              dself++;
            } else {
              fprintf(stderr,
                      "warning: failed to copy driver '%s' file "
                      "'%s' -> '%s': %s\n",
                      drv, src, dst, errbuf);
              copy_failed_count++;
            }
          }
        }
        if (got) {
          if (!quiet)
            printf("  bundled driver '%s' ? %s\n", drv, cands[c]);
          copied++;
        }
      }
      if (copied == 0 && used_driver_runtime[d]) {
        if (!quiet)
          printf("  note: driver '%s' not bundled (%s is empty); the "
                 "program will use a system-installed %s\n",
                 drv, driver_dir, drv);
      } else if (copied == 0 && dself == 0 && !used_driver_runtime[d]) {
        if (copy_failed_count > 0) {
          fprintf(stderr,
                  "warning: driver '%s' was not bundled because copy "
                  "operation(s) failed "
                  "(target file(s) in %s may be locked by another running "
                  "process, or write permission denied).\n",
                  drv, outdir);
        } else {
          fprintf(
              stderr,
              "warning: driver '%s' was not bundled (no runtime library "
              "found in %s). The published program will require '%s' to "
              "be installed on the target, or add a %s/%s.bundle manifest.\n",
              drv, driver_dir, drv, driver_dir, drv);
        }
      }
    }
  }

  if (apk_path || ipa_path) {
    const char *package_path = apk_path ? apk_path : ipa_path;
    snprintf(link_work_package, sizeof(link_work_package), "%s/package%s",
             link_work_dir, apk_path ? ".apk" : ".ipa");
    if (apk_path) {
      snprintf(link_work_package_sidecar, sizeof(link_work_package_sidecar),
               "%s.idsig", link_work_package);
      snprintf(link_work_unsigned_package, sizeof(link_work_unsigned_package),
               "%s.unsigned", link_work_package);
    }
    const zan_mobile_postlink_options_t mobile = {
        .input_path = input_file,
        .linked_path = obj_path,
        .staged_path = link_work_package,
        .apk_path = apk_path,
        .apk_package = apk_package,
        .apk_label = apk_label,
        .apk_arm64 = target.arch == ZAN_ARCH_AARCH64,
        .quiet = quiet,
        .driver_count = used_driver_count,
        .driver_dirs = driver_dirs,
        .driver_names = used_drivers,
        .driver_name_lengths = used_driver_len,
        .android_permission_count = proj_android_perm_count,
        .android_permissions = proj_android_perms,
        .ipa_path = ipa_path,
        .ipa_name = ipa_name,
        .ipa_bundle_id = ipa_bundle_id,
        .icon_path = icon_path};
    int package_status = zan_mobile_postlink(&mobile);
    if (package_status != 0 && apk_path &&
        zan_file_exists(link_work_unsigned_package)) {
      char unsigned_path[1120];
      int n = snprintf(unsigned_path, sizeof(unsigned_path), "%s.unsigned",
                       apk_path);
      if (n > 0 && (size_t)n < sizeof(unsigned_path) &&
          publish_link_output(link_work_unsigned_package, unsigned_path))
        fprintf(stderr, "  (unsigned package kept at %s)\n", unsigned_path);
    }
    if (package_status == 0 &&
        !publish_link_output(link_work_package, package_path)) {
      fprintf(stderr, "error: cannot publish package '%s'\n", package_path);
      package_status = 1;
    }
    if (package_status == 0 && apk_path &&
        zan_file_exists(link_work_package_sidecar)) {
      char idsig_path[1120];
      int n = snprintf(idsig_path, sizeof(idsig_path), "%s.idsig", apk_path);
      if (n <= 0 || (size_t)n >= sizeof(idsig_path) ||
          !publish_link_output(link_work_package_sidecar, idsig_path))
        fprintf(stderr, "warning: cannot publish APK v4 signature for '%s'\n",
                apk_path);
    }
    if (package_status != 0) {
      zan_diag_free_buffers(irgen.diag);
      zan_irgen_destroy(&irgen);
      zan_arena_free(ir_arena);
      zan_arena_free(arena);
      free(source);
      return 1;
    }
  }

  if (!quiet) {
    const char *final_out =
        ipa_path ? ipa_path : (apk_path ? apk_path : final_link_path);
    if (input_count == 1) {
      printf("%s '%s' ? '%s'\n", publish_mode ? "Published" : "Compiled",
             input_file, final_out);
    } else {
      printf("%s %d files ? '%s'\n", publish_mode ? "Published" : "Compiled",
             input_count, final_out);
    }
  }
  fflush(stdout);
  fflush(stderr);
}

zan_diag_free_buffers(irgen.diag);

zan_irgen_destroy(&irgen);
zan_arena_free(ir_arena);
zan_arena_free(arena);
free(source);
#ifdef _WIN32
/* ExitProcess bypasses the CRT's atexit callbacks. */
clear_link_work_dir();
fflush(stdout);
fflush(stderr);
ExitProcess(0);
#else
LLVMShutdown();
return 0;
#endif
}
