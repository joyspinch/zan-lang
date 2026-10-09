/* main */

#include "zan.h"
#include "arena.h"
#include "diag.h"
#include "lexer.h"
#include "parser.h"
#include "genrun.h"
#include "genmeta.h"
#include "nsresolve.h"
#include "ast.h"
#include "binder.h"
#include "checker.h"
#include "irgen.h"
#include "optimizer.h"
#include "crosscomp.h"
#include "winres.h"
#include "embedres.h"
#include "symbols.h"
#include "package.h"
#include "win_utf8.h"
#include "apk.h"
#include "ipa.h"
#include <llvm-c/Comdat.h>
/* zan */
#include "zan_default_icon.h"
#include "zan_version.h"
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
/* 模块核心语义抽象与接口调用契约 */
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

/* 模块核心语义抽象与接口调用契约 */
#define ZAN_LINK_MAX_ARGV        8192
#define ZAN_LINK_MAX_LIBS        2048
#define ZAN_LINK_MAX_DIRS        1024
#define ZAN_MAX_USED_DRIVERS     1024
#define ZAN_MAX_STATIC_DRV_LIBS  2048
/* 内部辅助逻辑 */
#define ZAN_LINK_ARGV_TAIL        32

/* 内部辅助逻辑 */
typedef struct {
    char **paths;
    int count;
    int capacity;
} generated_object_vec_t;

static void generated_object_vec_add(generated_object_vec_t *objects,
                                     const char *path) {
    if (objects->count == objects->capacity) {
        int next = objects->capacity ? objects->capacity * 2 : 4;
        char **grown = (char **)realloc(objects->paths,
                                        (size_t)next * sizeof(*grown));
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
            " ZAN_LINK_MAX_* in src/compiler/main.c\n", what, cap);
    exit(1);
}

/* 内部辅助逻辑 */
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
                " many inputs for one link\n", (int)cap);
        exit(1);
    }
}

static bool g_time_phases = false;
static double g_phase_start = 0.0;

/* 模块核心语义抽象与接口调用契约 */
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

static double now_ms(void) {
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

/* 编译期中间表示与代码生成内部规范 */
static void phase(const char *name) {
    if (!g_time_phases) return;
    double t = now_ms();
    size_t mem_mb = 0, ws_mb = 0, peak_mb = 0;
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
        mem_mb = pmc.PagefileUsage / (1024 * 1024);
        ws_mb = pmc.WorkingSetSize / (1024 * 1024);
        peak_mb = pmc.PeakPagefileUsage / (1024 * 1024);
    }
#endif
    size_t ar_mb = g_main_arena ? (zan_arena_total_bytes(g_main_arena) / (1024 * 1024)) : 0;
    if (g_phase_start > 0.0)
        fprintf(stderr, "%10.1f ms  [Commit: %4zu MB, Peak: %4zu MB, WS: %4zu MB, Arena: %4zu MB]  %s\n",
                t - g_phase_start, mem_mb, peak_mb, ws_mb, ar_mb, name);
    g_phase_start = t;
}

/* 内部辅助逻辑 */
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
#ifdef _WIN32
    if (!getenv("ZAN_PROBE_MEM")) return;
    PROCESS_MEMORY_COUNTERS pmc;
    if (!GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) return;
    size_t ar_mb = g_main_arena ? (zan_arena_total_bytes(g_main_arena) / (1024 * 1024)) : 0;
    fprintf(stderr, "    [phase %-14s Commit: %4zu MB, Peak: %4zu MB, Arena: %4zu MB]  %s\n",
            "", pmc.PagefileUsage / (1024 * 1024),
            pmc.PeakPagefileUsage / (1024 * 1024), ar_mb, name);
#endif
}

static char *read_file(const char *path, size_t *out_len) {
#ifdef _WIN32
    if (zan_utf8_is_directory(path)) {
#else
    struct stat st;
    if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) {
#endif
        /* 内部辅助逻辑 */
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
    if (out_len) *out_len = read;
    return buf;
}

/* 内部辅助逻辑 */
static void canon_key(const char *in, char *out, size_t out_sz) {
#ifdef _WIN32
    char full[4096];
    if (!zan_utf8_full_path(in, full, sizeof(full)))
        snprintf(full, sizeof(full), "%s", in);
    size_t j = 0;
    for (size_t i = 0; full[i] && j + 1 < out_sz; i++) {
        char c = full[i];
        if (c == '/') c = '\\';
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        out[j++] = c;
    }
    out[j] = '\0';
#else
    char full[4096];
    if (realpath(in, full) == NULL) snprintf(full, sizeof(full), "%s", in);
    snprintf(out, out_sz, "%s", full);
#endif
}

/* 内部辅助逻辑 */
typedef struct {
    char *key;
#ifndef _WIN32
    dev_t dev;
    ino_t ino;
    int have_stat;
#endif
} input_key_t;

static input_key_t *input_keys = NULL;
static int input_key_count = 0;
static int input_key_cap = 0;
/* 内部辅助逻辑 */
static int *input_key_idx = NULL;
static int input_key_idx_cap = 0; /* 核心系统底层抽象与内存语义契约 */
#ifndef _WIN32
/* 内部辅助逻辑 */
static int *input_ino_idx = NULL;
static int input_ino_idx_cap = 0;
#endif

static uint64_t input_hash_str(const char *s) {
    uint64_t h = 1469598103934665603ULL;
    for (; *s; s++) {
        h ^= (unsigned char)*s;
        h *= 1099511628211ULL;
    }
    return h;
}

#ifndef _WIN32
static uint64_t input_hash_ino(dev_t dev, ino_t ino) {
    uint64_t h = (uint64_t)dev;
    h = h * 0x9E3779B97F4A7C15ULL + (uint64_t)ino;
    h ^= h >> 29;
    h *= 0xBF58476D1CE4E5B9ULL;
    h ^= h >> 32;
    return h;
}
#endif

static void input_key_idx_rehash(int ncap) {
    int *grown = (int *)malloc((size_t)ncap * sizeof(*grown));
    if (!grown) {
        fprintf(stderr, "error: out of memory tracking input files\n");
        exit(1);
    }
    for (int i = 0; i < ncap; i++) grown[i] = -1;
    for (int i = 0; i < input_key_count; i++) {
        size_t j = (size_t)input_hash_str(input_keys[i].key) & (size_t)(ncap - 1);
        while (grown[j] >= 0) j = (j + 1) & (size_t)(ncap - 1);
        grown[j] = i;
    }
    free(input_key_idx);
    input_key_idx = grown;
    input_key_idx_cap = ncap;
}

#ifndef _WIN32
static void input_ino_idx_rehash(int ncap) {
    int *grown = (int *)malloc((size_t)ncap * sizeof(*grown));
    if (!grown) {
        fprintf(stderr, "error: out of memory tracking input files\n");
        exit(1);
    }
    for (int i = 0; i < ncap; i++) grown[i] = -1;
    for (int i = 0; i < input_key_count; i++) {
        if (!input_keys[i].have_stat) continue;
        size_t j = (size_t)input_hash_ino(input_keys[i].dev,
                                          input_keys[i].ino) & (size_t)(ncap - 1);
        while (grown[j] >= 0) j = (j + 1) & (size_t)(ncap - 1);
        grown[j] = i;
    }
    free(input_ino_idx);
    input_ino_idx = grown;
    input_ino_idx_cap = ncap;
}
#endif

static void input_key_add(const char *path) {
    if (input_key_count == input_key_cap) {
        int ncap = input_key_cap ? input_key_cap * 2 : 16;
        input_key_t *grown =
            (input_key_t *)realloc(input_keys, (size_t)ncap * sizeof(*grown));
        if (!grown) {
            fprintf(stderr, "error: out of memory tracking input files\n");
            exit(1);
        }
        input_keys = grown;
        input_key_cap = ncap;
    }
    char k[1024];
    canon_key(path, k, sizeof(k));
    input_key_t *e = &input_keys[input_key_count++];
    e->key = (char *)malloc(strlen(k) + 1);
    if (!e->key) {
        fprintf(stderr, "error: out of memory tracking input files\n");
        exit(1);
    }
    memcpy(e->key, k, strlen(k) + 1);
#ifndef _WIN32
    struct stat st;
    e->have_stat = stat(path, &st) == 0;
    if (e->have_stat) { e->dev = st.st_dev; e->ino = st.st_ino; }
#endif
    if ((input_key_count + 1) * 2 >= input_key_idx_cap)
        input_key_idx_rehash(input_key_idx_cap ? input_key_idx_cap * 2 : 64);
    size_t j = (size_t)input_hash_str(e->key) & (size_t)(input_key_idx_cap - 1);
    while (input_key_idx[j] >= 0) j = (j + 1) & (size_t)(input_key_idx_cap - 1);
    input_key_idx[j] = input_key_count - 1;
#ifndef _WIN32
    if (e->have_stat) {
        if ((input_key_count + 1) * 2 >= input_ino_idx_cap)
            input_ino_idx_rehash(input_ino_idx_cap ? input_ino_idx_cap * 2 : 64);
        size_t ij = (size_t)input_hash_ino(e->dev, e->ino)
                    & (size_t)(input_ino_idx_cap - 1);
        while (input_ino_idx[ij] >= 0) ij = (ij + 1) & (size_t)(input_ino_idx_cap - 1);
        input_ino_idx[ij] = input_key_count - 1;
    }
#endif
}

static int input_file_present(const char *cand) {
    char ck[1024];
    canon_key(cand, ck, sizeof(ck));
    if (input_key_idx_cap) {
        size_t j = (size_t)input_hash_str(ck) & (size_t)(input_key_idx_cap - 1);
        while (input_key_idx[j] >= 0) {
            if (strcmp(ck, input_keys[input_key_idx[j]].key) == 0) return 1;
            j = (j + 1) & (size_t)(input_key_idx_cap - 1);
        }
    }
#ifndef _WIN32
    struct stat cand_stat;
    int have_cand_stat = stat(cand, &cand_stat) == 0;
    if (have_cand_stat && input_ino_idx_cap) {
        size_t j = (size_t)input_hash_ino(cand_stat.st_dev, cand_stat.st_ino)
                   & (size_t)(input_ino_idx_cap - 1);
        while (input_ino_idx[j] >= 0) {
            input_key_t *e = &input_keys[input_ino_idx[j]];
            if (e->have_stat && e->dev == cand_stat.st_dev &&
                e->ino == cand_stat.st_ino) return 1;
            j = (j + 1) & (size_t)(input_ino_idx_cap - 1);
        }
    }
#endif
    return 0;
}

/* 模块核心语义抽象与接口调用契约 */
static void input_files_push(const char ***files, int *count, int *cap,
                             const char *path) {
    if (*count == *cap) {
        int ncap = *cap ? *cap * 2 : 16;
        const char **grown =
            (const char **)realloc((void *)*files, (size_t)ncap * sizeof(*grown));
        if (!grown) {
            fprintf(stderr, "error: out of memory tracking input files\n");
            exit(1);
        }
        *files = grown;
        *cap = ncap;
    }
    (*files)[(*count)++] = path;
    input_key_add(path);
}

/* 模块核心语义抽象与接口调用契约 */
static void add_stdlib_input(const char ***files, int *count, int *cap,
                             const char *path) {
    FILE *check = fopen(path, "rb");
    if (!check) return;
    fclose(check);
    if (input_file_present(path)) return;
    char *dup = (char *)malloc(strlen(path) + 1);
    memcpy(dup, path, strlen(path) + 1);
    input_files_push(files, count, cap, dup);
}

#ifndef _WIN32
static int resolve_dir_component(const char *parent, const char *component,
                                 char *resolved, size_t resolved_sz) {
    DIR *dir = opendir(parent);
    if (!dir) return 0;
    char match_name[256] = {0};
    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        if (strcasecmp(ent->d_name, component) != 0) continue;
        if (!match_name[0] || strcmp(ent->d_name, component) == 0)
            snprintf(match_name, sizeof(match_name), "%s", ent->d_name);
        if (strcmp(ent->d_name, component) == 0) break;
    }
    closedir(dir);
    if (!match_name[0]) return 0;
    char candidate[1024];
    int n = snprintf(candidate, sizeof(candidate), "%s/%s", parent, match_name);
    if (n < 0 || (size_t)n >= sizeof(candidate)) return 0;
    DIR *match = opendir(candidate);
    if (!match) return 0;
    closedir(match);
    snprintf(resolved, resolved_sz, "%s", candidate);
    return 1;
}

static int resolve_stdlib_dir(const char *stdlib_root, const char *subdir,
                              char *resolved, size_t resolved_sz) {
    char current[1024];
    snprintf(current, sizeof(current), "%s", stdlib_root);
    const char *p = subdir;
    while (*p) {
        char component[256];
        size_t len = 0;
        while (p[len] && p[len] != '/') len++;
        if (len == 0 || len >= sizeof(component)) return 0;
        memcpy(component, p, len);
        component[len] = '\0';
        char next[1024];
        if (!resolve_dir_component(current, component, next, sizeof(next))) return 0;
        snprintf(current, sizeof(current), "%s", next);
        p += len;
        if (*p == '/') p++;
    }
    snprintf(resolved, resolved_sz, "%s", current);
    return 1;
}
#endif

/* 内部辅助逻辑 */
static int stdlib_has_dir(const char *stdlib_root, const char *subdir) {
#ifdef _WIN32
    char p[1024];
    if (snprintf(p, sizeof(p), "%s\\%s", stdlib_root, subdir) >= (int)sizeof(p)) return 0;
    for (char *q = p; *q; q++) if (*q == '/') *q = '\\';
    DWORD a = GetFileAttributesA(p);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
#else
    char r[1024];
    return resolve_stdlib_dir(stdlib_root, subdir, r, sizeof(r));
#endif
}

/* 核心系统底层抽象与内存语义契约 */
static char **globbed_dirs = NULL;
static int globbed_dir_count = 0;
static int globbed_dir_cap = 0;
static int missing_namespace_count = 0;
static char package_project_root[1024] = ".";
static char **project_namespaces = NULL;
static int project_namespace_count = 0;
static int project_namespace_cap = 0;

static int project_namespace_declared(const char *name) {
    for (int i = 0; i < project_namespace_count; i++) {
        const char *decl = project_namespaces[i];
        size_t dn = strlen(decl), nn = strlen(name);
        if (strcmp(decl, name) == 0) return 1;
        if (dn > nn && strncmp(decl, name, nn) == 0 && decl[nn] == '/') return 1;
        if (nn > dn && strncmp(name, decl, dn) == 0 && name[dn] == '/') return 1;
    }
    return 0;
}

static void project_namespace_add(const char *name) {
    if (!name[0] || project_namespace_declared(name)) return;
    if (project_namespace_count == project_namespace_cap) {
        int ncap = project_namespace_cap ? project_namespace_cap * 2 : 16;
        char **grown = (char **)realloc(project_namespaces,
                                        (size_t)ncap * sizeof(*grown));
        if (!grown) return;
        project_namespaces = grown;
        project_namespace_cap = ncap;
    }
    char *dup = (char *)malloc(strlen(name) + 1);
    if (!dup) return;
    memcpy(dup, name, strlen(name) + 1);
    project_namespaces[project_namespace_count++] = dup;
}

static int subdir_globbed(const char *subdir) {
    for (int i = 0; i < globbed_dir_count; i++)
        if (strcmp(globbed_dirs[i], subdir) == 0) return 1;
    if (globbed_dir_count == globbed_dir_cap) {
        int ncap = globbed_dir_cap ? globbed_dir_cap * 2 : 32;
        char **grown = (char **)realloc(globbed_dirs, (size_t)ncap * sizeof(*grown));
        if (!grown) return 0;
        globbed_dirs = grown;
        globbed_dir_cap = ncap;
    }
    char *dup = (char *)malloc(strlen(subdir) + 1);
    if (!dup) return 0;
    memcpy(dup, subdir, strlen(subdir) + 1);
    globbed_dirs[globbed_dir_count++] = dup;
    return 0;
}

/* Probe a */
static int probe_file_namespace(const char *path, char *out_ns, size_t cap) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    char buf[1024];
    size_t nr = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    if (nr == 0) return 0;
    buf[nr] = '\0';

    const char *p = buf;
    while (*p) {
        if (p[0] == '/' && p[1] == '/') {
            p += 2;
            while (*p && *p != '\n' && *p != '\r') p++;
            continue;
        }
        if (p[0] == '/' && p[1] == '*') {
            p += 2;
            while (*p && !(p[0] == '*' && p[1] == '/')) p++;
            if (*p) p += 2;
            continue;
        }
        if (*p == '"') {
            p++;
            while (*p && *p != '"') {
                if (*p == '\\' && p[1]) p += 2;
                else p++;
            }
            if (*p) p++;
            continue;
        }
        if (strncmp(p, "namespace", 9) == 0 &&
            (p == buf || (unsigned char)p[-1] <= 32 || p[-1] == ';') &&
            ((unsigned char)p[9] <= 32)) {
            p += 9;
            while (*p && (unsigned char)*p <= 32) p++;
            size_t idx = 0;
            while (*p && (isalnum((unsigned char)*p) || *p == '_' || *p == '.')) {
                if (idx + 1 < cap) out_ns[idx++] = *p;
                p++;
            }
            out_ns[idx] = '\0';
            return idx > 0;
        }
        p++;
    }
    return 0;
}

/* 模块核心语义抽象与接口调用契约 */
static void subdir_to_namespace(const char *subdir, char *out_ns, size_t cap) {
    size_t i = 0;
    for (; subdir[i] && i + 1 < cap; i++) {
        out_ns[i] = (subdir[i] == '/' || subdir[i] == '\\') ? '.' : subdir[i];
    }
    out_ns[i] = '\0';
}

/* 核心系统底层抽象与内存语义契约 */
static int glob_stdlib_dir(const char *stdlib_root, const char *subdir,
                           const char ***files, int *count, int *cap) {
    int before = *count;
    char target_ns[256];
    subdir_to_namespace(subdir, target_ns, sizeof(target_ns));

#ifdef _WIN32
    char glob_path[1024];
    snprintf(glob_path, sizeof(glob_path), "%s\\%s\\*.zan", stdlib_root, subdir);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(glob_path, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            char mod_path[1024];
            snprintf(mod_path, sizeof(mod_path), "%s\\%s\\%s",
                     stdlib_root, subdir, fd.cFileName);
            add_stdlib_input(files, count, cap, mod_path);
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }

    char sub_pattern[1024];
    snprintf(sub_pattern, sizeof(sub_pattern), "%s\\%s\\*", stdlib_root, subdir);
    h = FindFirstFileA(sub_pattern, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
            if (fd.cFileName[0] == '.') continue;
            char candidate_sub[1024];
            snprintf(candidate_sub, sizeof(candidate_sub), "%s\\%s\\%s\\*.zan",
                     stdlib_root, subdir, fd.cFileName);
            WIN32_FIND_DATAA sub_fd;
            HANDLE sub_h = FindFirstFileA(candidate_sub, &sub_fd);
            if (sub_h != INVALID_HANDLE_VALUE) {
                char first_path[1024];
                snprintf(first_path, sizeof(first_path), "%s\\%s\\%s\\%s",
                         stdlib_root, subdir, fd.cFileName, sub_fd.cFileName);
                char declared_ns[256] = {0};
                if (probe_file_namespace(first_path, declared_ns, sizeof(declared_ns)) &&
                    strcmp(declared_ns, target_ns) == 0) {
                    do {
                        if (sub_fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
                        char mod_path[1024];
                        snprintf(mod_path, sizeof(mod_path), "%s\\%s\\%s\\%s",
                                 stdlib_root, subdir, fd.cFileName, sub_fd.cFileName);
                        add_stdlib_input(files, count, cap, mod_path);
                    } while (FindNextFileA(sub_h, &sub_fd));
                }
                FindClose(sub_h);
            }
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
#else
    char dir_path[1024];
    if (!resolve_stdlib_dir(stdlib_root, subdir, dir_path, sizeof(dir_path)))
        return 0;
    DIR *d = opendir(dir_path);
    if (d) {
        struct dirent *ent;
        while ((ent = readdir(d)) != NULL) {
            if (ent->d_name[0] == '.') continue;
            char entry_path[1024];
            snprintf(entry_path, sizeof(entry_path), "%s/%s", dir_path, ent->d_name);
            struct stat st;
            if (stat(entry_path, &st) == 0 && S_ISDIR(st.st_mode)) {
                DIR *subd = opendir(entry_path);
                if (subd) {
                    struct dirent *sub_ent;
                    int sub_matched = 0;
                    while ((sub_ent = readdir(subd)) != NULL) {
                        size_t slen = strlen(sub_ent->d_name);
                        if (slen < 5 || strcmp(sub_ent->d_name + slen - 4, ".zan") != 0) continue;
                        char sub_file_path[1024];
                        snprintf(sub_file_path, sizeof(sub_file_path), "%s/%s", entry_path, sub_ent->d_name);
                        if (!sub_matched) {
                            char declared_ns[256] = {0};
                            if (probe_file_namespace(sub_file_path, declared_ns, sizeof(declared_ns)) &&
                                strcmp(declared_ns, target_ns) == 0) {
                                sub_matched = 1;
                            } else {
                                break;
                            }
                        }
                        if (sub_matched) {
                            add_stdlib_input(files, count, cap, sub_file_path);
                        }
                    }
                    closedir(subd);
                }
                continue;
            }
            size_t nlen = strlen(ent->d_name);
            if (nlen < 5 || strcmp(ent->d_name + nlen - 4, ".zan") != 0) continue;
            char mod_path[1024];
            snprintf(mod_path, sizeof(mod_path), "%s/%s", dir_path, ent->d_name);
            add_stdlib_input(files, count, cap, mod_path);
        }
        closedir(d);
    }
#endif
    return *count > before;
}

static zan_pkg_source_index_t *package_source_index;

static void package_sources_destroy(void) {
    zan_pkg_source_index_destroy(package_source_index);
    package_source_index = NULL;
}

static int package_visit_namespace(const char *subdir,
                                    zan_pkg_source_visitor_t visitor,
                                    void *context, int hierarchical) {
    if (!package_project_root[0]) return 0;
    if (!package_source_index) {
        package_source_index = zan_pkg_source_index_create(
            package_project_root, probe_file_namespace);
        atexit(package_sources_destroy);
    }
    return zan_pkg_source_index_visit(package_source_index, subdir, visitor,
                                       context, hierarchical);
}

typedef struct {
    const char ***files;
    int *count;
    int *cap;
} pkg_auto_include_t;

static void package_add_input(const char *path, void *context) {
    pkg_auto_include_t *a = (pkg_auto_include_t *)context;
    add_stdlib_input(a->files, a->count, a->cap, path);
}

static int auto_include_namespace(const char *stdlib_root, const char *subdir,
                                  const char ***files, int *count, int *cap) {
    int found = glob_stdlib_dir(stdlib_root, subdir, files, count, cap);
    char probe_dir[1024];
    int hierarchical = stdlib_has_dir(stdlib_root, subdir) == 0;
    pkg_auto_include_t args = { files, count, cap };
    int package_count = package_visit_namespace(subdir, package_add_input,
                                                  &args, hierarchical);
    found = found || package_count > 0;
    /* 内部辅助逻辑 */
    if (!found && package_project_root[0] != '\0' &&
        strcmp(subdir, "System") != 0 &&
        !project_namespace_declared(subdir)) {
        fprintf(stderr, "ZANPKG_MISSING namespace=%s\n", subdir);
        missing_namespace_count++;
    }
    return found;
}

static void scan_namespace_tokens(const char *source, size_t len) {
    zan_arena_t *arena = zan_arena_new();
    zan_diag_t *diag = zan_diag_new(arena);
    /* 内部辅助逻辑 */
    zan_diag_set_capture(diag, true);
    zan_lexer_t lex;
    zan_lexer_init(&lex, source, len, 0, arena, diag);
    for (;;) {
        zan_token_t tok = zan_lexer_next(&lex);
        if (tok.kind == TK_EOF) break;
        if (tok.kind != TK_NAMESPACE) continue;
        char name[512]; size_t used = 0;
        tok = zan_lexer_next(&lex);
        if (tok.kind != TK_IDENT) continue;
        for (;;) {
            if (used && used + 1 < sizeof(name)) name[used++] = '/';
            if (used + tok.str_val.len >= sizeof(name)) break;
            memcpy(name + used, tok.str_val.str, tok.str_val.len);
            used += tok.str_val.len;
            tok = zan_lexer_next(&lex);
            if (tok.kind != TK_DOT) break;
            tok = zan_lexer_next(&lex);
            if (tok.kind != TK_IDENT) break;
        }
        name[used] = 0;
        if (tok.kind == TK_SEMICOLON && used > 0) project_namespace_add(name);
    }
    zan_diag_free_buffers(diag);
    zan_arena_free(arena);
}

/* 内部辅助逻辑 */
static int skip_project_dir(const char *name) {
    static const char *skip[] = {
        ".zan-packages", ".git", ".svn", ".hg", "build", "publish",
        "bin", "obj", "node_modules", "_scratch", "dist", "out", "vendor"
    };
    for (size_t i = 0; i < sizeof(skip) / sizeof(skip[0]); i++)
        if (strcmp(name, skip[i]) == 0) return 1;
    return 0;
}

/* 内部辅助逻辑 */
static void scan_project_namespaces(const char *root) {
#ifdef _WIN32
    char pattern[1024];
    snprintf(pattern, sizeof(pattern), "%s\\*", root);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) continue;
        char path[1024];
        snprintf(path, sizeof(path), "%s\\%s", root, fd.cFileName);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) continue;
            if (!skip_project_dir(fd.cFileName)) scan_project_namespaces(path);
        } else {
            size_t nlen = strlen(fd.cFileName);
            if (nlen < 5 || strcmp(fd.cFileName + nlen - 4, ".zan") != 0) continue;
            size_t n = 0;
            char *src = read_file(path, &n);
            if (src) { scan_namespace_tokens(src, n); free(src); }
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#else
    DIR *d = opendir(root);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
        char path[1024];
        snprintf(path, sizeof(path), "%s/%s", root, e->d_name);
        struct stat st;
        if (lstat(path, &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) {
            if (!S_ISLNK(st.st_mode) && !skip_project_dir(e->d_name))
                scan_project_namespaces(path);
        } else if (S_ISREG(st.st_mode)) {
            size_t nlen = strlen(e->d_name);
            if (nlen < 5 || strcmp(e->d_name + nlen - 4, ".zan") != 0) continue;
            size_t n = 0;
            char *src = read_file(path, &n);
            if (src) { scan_namespace_tokens(src, n); free(src); }
        }
    }
    closedir(d);
#endif
}

static bool project_root_has_manifest(void) {
    char manifest[1200];
    snprintf(manifest, sizeof(manifest), "%s/zan.proj", package_project_root);
    FILE *f = fopen(manifest, "rb");
    if (f) { fclose(f); return true; }
    return false;
}

static void resolve_package_project_root(const char *input) {
    snprintf(package_project_root, sizeof(package_project_root), "%s", input);
    char *sep = strrchr(package_project_root, '/');
    char *back = strrchr(package_project_root, '\\');
    if (!sep || (back && back > sep)) sep = back;
    if (sep) *sep = 0;
    else snprintf(package_project_root, sizeof(package_project_root), ".");
    for (;;) {
        char manifest[1200];
        snprintf(manifest, sizeof(manifest), "%s/zan.proj", package_project_root);
        FILE *f = fopen(manifest, "rb");
        if (f) { fclose(f); return; }
        char *a = strrchr(package_project_root, '/');
        char *b = strrchr(package_project_root, '\\');
        char *last = (!a || (b && b > a)) ? b : a;
        if (!last) break;
#ifdef _WIN32
        if (last == package_project_root + 2 && package_project_root[1] == ':') break;
#endif
        *last = 0;
    }
    snprintf(package_project_root, sizeof(package_project_root), ".");
}

/* 内部辅助实现 */
/* 内部辅助逻辑 */
static char proj_android_package[256];
static char proj_android_label[256];
static bool proj_android_keys_ok = true;
static char proj_android_perms[128][128];
static int proj_android_perm_count = 0;
static char proj_skin_names[128][64];
static int proj_skin_name_count = 0;
static bool proj_skins_enabled = false;
static bool load_proj_android_keys_done = false;
/* 内部辅助逻辑 */
static const char **skin_filter = NULL;
static int skin_filter_count = 0;

static char *proj_trim(char *s) {
    while (*s == ' ' || *s == '\t') s++;
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t'
                     || s[n - 1] == '\r' || s[n - 1] == '\n')) s[--n] = 0;
    return s;
}

static void load_proj_android_keys(void) {
    char path[1400];
    snprintf(path, sizeof(path), "%s/zan.proj", package_project_root);
    FILE *f = fopen(path, "rb");
    if (!f) return;
    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        char *key = proj_trim(line);
        char *val = proj_trim(eq + 1);
        if (strcmp(key, "skins") == 0) {
            proj_skins_enabled = strcmp(val, "1") == 0;
        } else if (strcmp(key, "androidPackage") == 0) {
            if (strlen(val) >= sizeof(proj_android_package)) {
                fprintf(stderr, "error: zan.proj androidPackage exceeds "
                        "%zu bytes\n", sizeof(proj_android_package) - 1);
                proj_android_keys_ok = false;
            } else {
                snprintf(proj_android_package,
                         sizeof(proj_android_package), "%s", val);
            }
        } else if (strcmp(key, "androidLabel") == 0) {
            if (strlen(val) >= sizeof(proj_android_label)) {
                fprintf(stderr, "error: zan.proj androidLabel exceeds "
                        "%zu bytes\n", sizeof(proj_android_label) - 1);
                proj_android_keys_ok = false;
            } else {
                snprintf(proj_android_label,
                         sizeof(proj_android_label), "%s", val);
            }
        } else if (strcmp(key, "androidPermissions") == 0) {
            char *save = NULL;
            char *tok = strtok_r(val, ",", &save);
            while (tok) {
                char *p = proj_trim(tok);
                if (*p) {
                    if (proj_android_perm_count >= 128) {
                        fprintf(stderr, "error: zan.proj androidPermissions: "
                                "more than 128 entries\n");
                        proj_android_keys_ok = false;
                        break;
                    }
                    char *dst = proj_android_perms[proj_android_perm_count];
                    int wn = strncmp(p, "android.permission.", 19) == 0
                           ? snprintf(dst, 128, "%s", p)
                           : snprintf(dst, 128, "android.permission.%s", p);
                    if (wn >= 128) {
                        fprintf(stderr, "error: zan.proj androidPermissions: "
                                "'%s' exceeds 127 bytes\n", p);
                        proj_android_keys_ok = false;
                    } else {
                        proj_android_perm_count++;
                    }
                }
                tok = strtok_r(NULL, ",", &save);
            }
        } else if (strcmp(key, "skinlist") == 0) {
            char *save = NULL;
            char *tok = strtok_r(val, ",", &save);
            while (tok && proj_skin_name_count < 128) {
                char *p = proj_trim(tok);
                if (*p && strcmp(p, "-") != 0) {
                    snprintf(proj_skin_names[proj_skin_name_count],
                             sizeof(proj_skin_names[0]), "%s", p);
                    proj_skin_name_count++;
                }
                tok = strtok_r(NULL, ",", &save);
            }
        }
    }
    fclose(f);
}

static void scan_using_tokens(const char *source, size_t len,
                              const char *stdlib_root,
                              const char ***files, int *count, int *cap) {
    zan_arena_t *arena = zan_arena_new();
    zan_diag_t *diag = zan_diag_new(arena);
    /* 内部辅助逻辑 */
    zan_diag_set_capture(diag, true);
    zan_lexer_t lex;
    zan_lexer_init(&lex, source, len, 0, arena, diag);
    for (;;) {
        zan_token_t tok = zan_lexer_next(&lex);
        if (tok.kind == TK_EOF) break;
        if (tok.kind != TK_USING) continue;
        char subdir[512]; size_t used = 0;
        tok = zan_lexer_next(&lex);
        if (tok.kind != TK_IDENT) continue;
        for (;;) {
            if (used && used + 1 < sizeof(subdir)) subdir[used++] = '/';
            if (used + tok.str_val.len >= sizeof(subdir)) break;
            memcpy(subdir + used, tok.str_val.str, tok.str_val.len);
            used += tok.str_val.len;
            tok = zan_lexer_next(&lex);
            if (tok.kind != TK_DOT) break;
            tok = zan_lexer_next(&lex);
            if (tok.kind != TK_IDENT) break;
        }
        subdir[used] = 0;
        if (tok.kind == TK_SEMICOLON && used > 0 && !subdir_globbed(subdir))
            auto_include_namespace(stdlib_root, subdir, files, count, cap);
    }
    zan_diag_free_buffers(diag);
    zan_arena_free(arena);
}

/* 内部辅助实现 */

static zan_arena_t *pi_arena = NULL;
static int pi_filter_active = 0;
/* 底层系统交互与数据协议契约 */
static int pi_seed_stdlib_input = 0;
/* 内部辅助逻辑 */
static int pi_seeding_stdlib = 0;
/* 内部辅助逻辑 */
static int pi_repair_scanning = 0;

typedef struct pi_name {
    const char *str;
    unsigned len;
    int flagged;                /* 底层系统交互与数据协议契约 */
    int flagged_stdlib;         /* 内部辅助实现 */
    int user_decl;              /* 内部辅助实现 */
    int ns_root;                /* 核心系统底层抽象与内存语义契约 */
    struct pi_name *next;
} pi_name_t;

#define PI_BUCKETS 8192u
static pi_name_t *pi_table[PI_BUCKETS];

typedef struct pi_file {
    char *path;
    pi_name_t **top; int top_count, top_cap;      /* 核心系统底层抽象与内存语义契约 */
    pi_name_t **idents; int ident_count, ident_cap; /* 核心系统底层抽象与内存语义契约 */
    char **usings; int using_count, using_cap;    /* 核心系统底层抽象与内存语义契约 */
    int has_ext;                /* 核心系统底层抽象与内存语义契约 */
    int pkg_src;                /* 内部辅助实现 */
    int gate_live;              /* 内部辅助实现 */
    int included;               /* 核心系统底层抽象与内存语义契约 */
    int parsed;                 /* 核心系统底层抽象与内存语义契约 */
    int seeded;                 /* 核心系统底层抽象与内存语义契约 */
    struct pi_file *dnext;
} pi_file_t;

typedef struct pi_dir {
    char *subdir;               /* 核心系统底层抽象与内存语义契约 */
    pi_file_t *files;
    int file_count, file_cap;
    int reached;                /* globbed + metadata-scanned */
    struct pi_dir *next;
} pi_dir_t;

static pi_dir_t *pi_dirs_head = NULL;
static pi_dir_t *pi_dirs_tail = NULL;

/* 底层系统交互与数据协议契约 */
static zan_target_t pi_target;
static const char *const *pi_pp_defines = NULL;
static int pi_pp_define_count = 0;
static bool pi_publish_mode = false;
static void zan_apply_lex_defines(zan_lexer_t *lex, zan_target_t target,
                                  const char *const *pp_defines,
                                  int pp_define_count,
                                  bool is_publish);

static unsigned pi_hash(const char *s, size_t len) {
    unsigned h = 2166136261u;
    for (size_t i = 0; i < len; i++) h = (h ^ (unsigned char)s[i]) * 16777619u;
    return h;
}

static pi_name_t *pi_intern(const char *s, size_t len) {
    unsigned b = pi_hash(s, len) & (PI_BUCKETS - 1);
    for (pi_name_t *p = pi_table[b]; p; p = p->next)
        if (p->len == len && memcmp(p->str, s, len) == 0) return p;
    pi_name_t *p = (pi_name_t *)zan_arena_alloc(pi_arena, sizeof(*p));
    if (!p) return NULL;
    char *dup = (char *)zan_arena_alloc(pi_arena, len + 1);
    if (!dup) return NULL;
    memcpy(dup, s, len);
    dup[len] = 0;
    p->str = dup;
    p->len = (unsigned)len;
    p->flagged = 0;
    p->flagged_stdlib = 0;
    p->next = pi_table[b];
    pi_table[b] = p;
    return p;
}

/* 模块核心语义抽象与接口调用契约 */
static int pi_reserve(void *arr_p, int count, int *cap, size_t elem_sz) {
    if (count < *cap) return 1;
    int ncap = *cap ? *cap * 2 : 8;
    void *grown = zan_arena_alloc(pi_arena, (size_t)ncap * elem_sz);
    if (!grown) return 0;
    memcpy(grown, *(void **)arr_p, (size_t)count * elem_sz);
    *(void **)arr_p = grown;
    *cap = ncap;
    return 1;
}

static void pi_reach(const char *subdir) {
    for (pi_dir_t *d = pi_dirs_head; d; d = d->next)
        if (strcmp(d->subdir, subdir) == 0) return;
    pi_dir_t *d = (pi_dir_t *)zan_arena_alloc(pi_arena, sizeof(*d));
    if (!d) return;
    size_t len = strlen(subdir);
    char *dup = (char *)zan_arena_alloc(pi_arena, len + 1);
    if (!dup) return;
    memcpy(dup, subdir, len + 1);
    d->subdir = dup;
    d->files = NULL;
    d->file_count = 0;
    d->reached = 0;
    d->next = NULL;
    if (pi_dirs_tail) pi_dirs_tail->next = d;
    else pi_dirs_head = d;
    pi_dirs_tail = d;
}

/* 模块核心语义抽象与接口调用契约 */
static const char *pi_stdlib_root_buf;

/* 内部辅助逻辑 */
static int pi_reach_input_dir(const char *file) {
    char comps[64][256];
    int n = 0;
    size_t len = strlen(file);
    size_t i = 0;
    while (i < len && n < 64) {
        size_t start = i;
        while (i < len && file[i] != '/' && file[i] != 92) i++;
        size_t cl = i - start;
        if (cl > 0 && cl < 256) {
            memcpy(comps[n], file + start, cl);
            comps[n][cl] = 0;
            n++;
        }
        i++;
    }
    if (n < 2 || !pi_stdlib_root_buf) return 0;
    const char *root = pi_stdlib_root_buf;
    size_t rl = strlen(root);
    size_t rs = rl;
    while (rs > 0 && root[rs - 1] != '/' && root[rs - 1] != 92) rs--;
    const char *leaf = root + rs;
    size_t ll = strlen(leaf);
    int base = -1;
    for (int k = n - 2; k >= 0; k--) {
        if (strlen(comps[k]) == ll) {
            int eq = 1;
            for (size_t c = 0; c < ll; c++) {
                char a = comps[k][c], b = leaf[c];
                if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
                if (b >= 'A' && b <= 'Z') b = (char)(b + 32);
                if (a != b) { eq = 0; break; }
            }
            if (eq) { base = k; break; }
        }
    }
    if (base < 0 || base == n - 1) return 0;  /* 核心系统底层抽象与内存语义契约 */
    char sub[1024];
    size_t used = 0;
    for (int k = base + 1; k < n - 1; k++) {  /* 核心系统底层抽象与内存语义契约 */
        size_t cl = strlen(comps[k]);
        if (used && used + 1 < sizeof(sub)) sub[used++] = '/';
        if (used + cl + 1 > sizeof(sub)) return 1;
        memcpy(sub + used, comps[k], cl);
        used += cl;
    }
    sub[used] = 0;
    pi_reach(sub);
    char declared_ns[256] = {0};
    if (probe_file_namespace(file, declared_ns, sizeof(declared_ns))) {
        char canon_sub[256];
        size_t c = 0;
        for (; declared_ns[c] && c + 1 < sizeof(canon_sub); c++) {
            canon_sub[c] = declared_ns[c] == '.' ? '/' : declared_ns[c];
        }
        canon_sub[c] = '\0';
        if (canon_sub[0] && strcmp(canon_sub, sub) != 0) {
            pi_reach(canon_sub);
        }
    }
    return 1;
}

static void pi_add_file(pi_dir_t *d, const char *path) {
    if (!pi_reserve((void *)&d->files, d->file_count, &d->file_cap,
                    sizeof(pi_file_t)))
        return;
    pi_file_t *f = &d->files[d->file_count++];
    size_t len = strlen(path);
    f->path = (char *)zan_arena_alloc(pi_arena, len + 1);
    if (f->path) memcpy(f->path, path, len + 1);
    f->top = NULL; f->top_count = 0; f->top_cap = 0;
    f->idents = NULL; f->ident_count = 0; f->ident_cap = 0;
    f->usings = NULL; f->using_count = 0; f->using_cap = 0;
    f->has_ext = 0;
    f->pkg_src = 0;
    f->included = 0;
    f->parsed = 0;
    f->seeded = 0;
    f->dnext = NULL;
}

/* 模块核心语义抽象与接口调用契约 */
static void pi_glob_into(pi_dir_t *d, const char *root, const char *subdir) {
    char target_ns[256];
    subdir_to_namespace(subdir, target_ns, sizeof(target_ns));

#ifdef _WIN32
    char glob_path[1024];
    if (subdir[0])
        snprintf(glob_path, sizeof(glob_path), "%s\\%s\\*.zan", root, subdir);
    else
        snprintf(glob_path, sizeof(glob_path), "%s\\*.zan", root);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(glob_path, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            char mod_path[1024];
            if (subdir[0])
                snprintf(mod_path, sizeof(mod_path), "%s\\%s\\%s",
                         root, subdir, fd.cFileName);
            else
                snprintf(mod_path, sizeof(mod_path), "%s\\%s", root, fd.cFileName);
            pi_add_file(d, mod_path);
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }

    if (subdir[0]) {
        char sub_pattern[1024];
        snprintf(sub_pattern, sizeof(sub_pattern), "%s\\%s\\*", root, subdir);
        h = FindFirstFileA(sub_pattern, &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
                if (fd.cFileName[0] == '.') continue;
                char candidate_sub[1024];
                snprintf(candidate_sub, sizeof(candidate_sub), "%s\\%s\\%s\\*.zan",
                         root, subdir, fd.cFileName);
                WIN32_FIND_DATAA sub_fd;
                HANDLE sub_h = FindFirstFileA(candidate_sub, &sub_fd);
                if (sub_h != INVALID_HANDLE_VALUE) {
                    char first_path[1024];
                    snprintf(first_path, sizeof(first_path), "%s\\%s\\%s\\%s",
                             root, subdir, fd.cFileName, sub_fd.cFileName);
                    char declared_ns[256] = {0};
                    if (probe_file_namespace(first_path, declared_ns, sizeof(declared_ns)) &&
                        strcmp(declared_ns, target_ns) == 0) {
                        do {
                            if (sub_fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
                            char mod_path[1024];
                            snprintf(mod_path, sizeof(mod_path), "%s\\%s\\%s\\%s",
                                     root, subdir, fd.cFileName, sub_fd.cFileName);
                            pi_add_file(d, mod_path);
                        } while (FindNextFileA(sub_h, &sub_fd));
                    }
                    FindClose(sub_h);
                }
            } while (FindNextFileA(h, &fd));
            FindClose(h);
        }
    }
#else
    char dir_path[1024];
    if (!resolve_stdlib_dir(root, subdir, dir_path, sizeof(dir_path)))
        return;
    DIR *dr = opendir(dir_path);
    if (dr) {
        struct dirent *ent;
        while ((ent = readdir(dr)) != NULL) {
            if (ent->d_name[0] == '.') continue;
            char entry_path[1024];
            snprintf(entry_path, sizeof(entry_path), "%s/%s", dir_path, ent->d_name);
            struct stat st;
            if (stat(entry_path, &st) == 0 && S_ISDIR(st.st_mode)) {
                DIR *subdr = opendir(entry_path);
                if (subdr) {
                    struct dirent *sub_ent;
                    int sub_matched = 0;
                    while ((sub_ent = readdir(subdr)) != NULL) {
                        size_t slen = strlen(sub_ent->d_name);
                        if (slen < 5 || strcmp(sub_ent->d_name + slen - 4, ".zan") != 0) continue;
                        char sub_file_path[1024];
                        snprintf(sub_file_path, sizeof(sub_file_path), "%s/%s", entry_path, sub_ent->d_name);
                        if (!sub_matched) {
                            char declared_ns[256] = {0};
                            if (probe_file_namespace(sub_file_path, declared_ns, sizeof(declared_ns)) &&
                                strcmp(declared_ns, target_ns) == 0) {
                                sub_matched = 1;
                            } else {
                                break;
                            }
                        }
                        if (sub_matched) {
                            pi_add_file(d, sub_file_path);
                        }
                    }
                    closedir(subdr);
                }
                continue;
            }
            size_t nlen = strlen(ent->d_name);
            if (nlen < 5 || strcmp(ent->d_name + nlen - 4, ".zan") != 0) continue;
            char mod_path[1024];
            snprintf(mod_path, sizeof(mod_path), "%s/%s", dir_path, ent->d_name);
            pi_add_file(d, mod_path);
        }
        closedir(dr);
    }
#endif
}

/* Record a dotted `using A */
static void pi_note_using(pi_file_t *f, const char *subdir) {
    if (!subdir[0]) return;
    /* 内部辅助逻辑 */
    if (pi_repair_scanning) return;
    if (!pi_reserve((void *)&f->usings, f->using_count, &f->using_cap,
                    sizeof(char *)))
        return;
    size_t len = strlen(subdir);
    char *dup = (char *)zan_arena_alloc(pi_arena, len + 1);
    if (!dup) return;
    memcpy(dup, subdir, len + 1);
    f->usings[f->using_count++] = dup;
    pi_reach(dup);
}

static void pi_flag_ident(pi_file_t *f, const char *s, size_t len) {
    pi_name_t *name = pi_intern(s, len);
    if (!name) return;
    if (!f) {
        /* 模块核心语义抽象与接口调用契约 */
        name->flagged = 1;
        if (pi_seeding_stdlib) name->flagged_stdlib = 1;
        return;
    }
    if (!pi_reserve((void *)&f->idents, f->ident_count, &f->ident_cap,
                    sizeof(pi_name_t *)))
        return;
    f->idents[f->ident_count++] = name;
}

/* 内部辅助逻辑 */
static uint64_t pi_meta_hash_bytes(uint64_t h, const void *data, size_t len) {
    const unsigned char *p = (const unsigned char *)data;
    for (size_t i = 0; i < len; i++) {
        h ^= p[i];
        h *= UINT64_C(1099511628211);
    }
    return h;
}

static uint64_t pi_meta_hash_u32(uint64_t h, uint32_t value) {
    return pi_meta_hash_bytes(h, &value, sizeof(value));
}

static uint64_t pi_meta_hash_text(uint64_t h, const char *text) {
    size_t len = text ? strlen(text) : 0;
    h = pi_meta_hash_u32(h, (uint32_t)len);
    return pi_meta_hash_bytes(h, text, len);
}

static uint64_t pi_meta_compiler_hash(void) {
    static uint64_t identity;
    static int checked;
    if (checked) return identity;
    checked = 1;
    char exe[4096];
#ifdef _WIN32
    DWORD n = GetModuleFileNameA(NULL, exe, sizeof(exe));
    if (!n || n >= sizeof(exe)) return 0;
#elif defined(__APPLE__)
    uint32_t sz = sizeof(exe);
    if (_NSGetExecutablePath(exe, &sz) != 0) return 0;
#else
    ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    if (n <= 0) return 0;
    exe[n] = 0;
#endif
    /* 内部辅助逻辑 */
    FILE *in = fopen(exe, "rb");
    if (!in) return 0;
    uint64_t h = UINT64_C(1469598103934665603);
    unsigned char buf[65536];
    size_t nread;
    while ((nread = fread(buf, 1, sizeof(buf), in)) != 0)
        h = pi_meta_hash_bytes(h, buf, nread);
    int ok = !ferror(in);
    fclose(in);
    if (ok) identity = h ? h : 1;
    return identity;
}

static uint64_t pi_meta_file_hash(const char *path, const char *src, size_t len) {
    uint64_t compiler = pi_meta_compiler_hash();
    if (!compiler) return 0;
    uint64_t h = UINT64_C(1469598103934665603);
    /* 内部辅助逻辑 */
    h = pi_meta_hash_text(h, "zan-pullin-meta-v2");
    h = pi_meta_hash_text(h, ZAN_VERSION);
    h = pi_meta_hash_bytes(h, &compiler, sizeof(compiler));
    h = pi_meta_hash_text(h, path);
    h = pi_meta_hash_u32(h, (uint32_t)pi_target.arch);
    h = pi_meta_hash_u32(h, (uint32_t)pi_target.os);
    h = pi_meta_hash_u32(h, (uint32_t)pi_target.abi);
    h = pi_meta_hash_text(h, pi_target.triple);
    h = pi_meta_hash_text(h, pi_target.cpu);
    h = pi_meta_hash_text(h, pi_target.features);
    h = pi_meta_hash_u32(h, (uint32_t)pi_target.pointer_size);
    h = pi_meta_hash_u32(h, pi_target.pic ? 1u : 0u);
    h = pi_meta_hash_u32(h, pi_publish_mode ? 1u : 0u);
    h = pi_meta_hash_u32(h, (uint32_t)pi_pp_define_count);
    for (int i = 0; i < pi_pp_define_count; i++)
        h = pi_meta_hash_text(h, pi_pp_defines[i]);
    h = pi_meta_hash_u32(h, (uint32_t)len);
    return pi_meta_hash_bytes(h, src, len);
}

static void pi_meta_cache_path(uint64_t key, char *out, size_t out_sz) {
    out[0] = 0;
    if (!key) return;
    /* 内部辅助逻辑 */
    const char *override = getenv("ZAN_META_CACHE_DIR");
    if (override && *override) {
#ifdef _WIN32
        snprintf(out, out_sz, "%s\\pullin-meta-%016llx.bin", override,
                 (unsigned long long)key);
#else
        snprintf(out, out_sz, "%s/pullin-meta-%016llx.bin", override,
                 (unsigned long long)key);
#endif
        return;
    }
#ifdef _WIN32
    const char *base = getenv("LOCALAPPDATA");
    if (base && *base)
        snprintf(out, out_sz, "%s\\Zan\\pullin-meta-%016llx.bin", base,
                 (unsigned long long)key);
#else
    const char *base = getenv("XDG_CACHE_HOME");
    if (!base || !*base) base = getenv("HOME");
    if (base && *base) {
        if (getenv("XDG_CACHE_HOME"))
            snprintf(out, out_sz, "%s/zan/pullin-meta-%016llx.bin", base,
                     (unsigned long long)key);
        else
            snprintf(out, out_sz, "%s/.cache/zan/pullin-meta-%016llx.bin", base,
                     (unsigned long long)key);
    }
#endif
}

/* 内部辅助实现 */
#define PI_META_MAGIC UINT32_C(0x5a504d32)
#define PI_META_VERSION UINT32_C(2)
#define PI_META_MAX_NAMES 100000u

static int pi_meta_cache_load(pi_file_t *f, const char *src, size_t len) {
    if (!f || !f->path) return 0;
    uint64_t key = pi_meta_file_hash(f->path, src, len);
    char cache[1024];
    pi_meta_cache_path(key, cache, sizeof(cache));
    if (!cache[0]) return 0;
    FILE *in = fopen(cache, "rb");
    if (!in) return 0;
    /* 内部辅助逻辑 */
    pi_dir_t *saved_head = pi_dirs_head, *saved_tail = pi_dirs_tail;
    uint32_t magic = 0, version = 0, top = 0, idents = 0, usings = 0;
    unsigned char ext = 0;
    int ok = fread(&magic, sizeof(magic), 1, in) == 1 &&
             fread(&version, sizeof(version), 1, in) == 1 &&
             fread(&top, sizeof(top), 1, in) == 1 &&
             fread(&idents, sizeof(idents), 1, in) == 1 &&
             fread(&usings, sizeof(usings), 1, in) == 1 &&
             fread(&ext, sizeof(ext), 1, in) == 1 &&
             magic == PI_META_MAGIC && version == PI_META_VERSION &&
             top <= PI_META_MAX_NAMES && idents <= PI_META_MAX_NAMES &&
             usings <= PI_META_MAX_NAMES;
    for (uint32_t i = 0; ok && i < top; i++) {
        uint16_t n = 0;
        char buf[1024];
        ok = fread(&n, sizeof(n), 1, in) == 1 && n < sizeof(buf) &&
             fread(buf, 1, n, in) == n;
        if (!ok) break;
        buf[n] = 0;
        pi_name_t *name = pi_intern(buf, n);
        ok = name && pi_reserve((void *)&f->top, f->top_count,
                                &f->top_cap, sizeof(pi_name_t *));
        if (ok) f->top[f->top_count++] = name;
    }
    for (uint32_t i = 0; ok && i < idents; i++) {
        uint16_t n = 0;
        char buf[1024];
        ok = fread(&n, sizeof(n), 1, in) == 1 && n < sizeof(buf) &&
             fread(buf, 1, n, in) == n;
        if (!ok) break;
        pi_flag_ident(f, buf, n);
    }
    for (uint32_t i = 0; ok && i < usings; i++) {
        uint16_t n = 0;
        char buf[1024];
        ok = fread(&n, sizeof(n), 1, in) == 1 && n < sizeof(buf) &&
             fread(buf, 1, n, in) == n;
        if (!ok) break;
        buf[n] = 0;
        ok = pi_reserve((void *)&f->usings, f->using_count,
                        &f->using_cap, sizeof(char *));
        if (ok) {
            char *dup = (char *)zan_arena_alloc(pi_arena, n + 1);
            ok = dup != NULL;
            if (ok) {
                memcpy(dup, buf, n + 1);
                f->usings[f->using_count++] = dup;
                pi_reach(dup);
            }
        }
    }
    fclose(in);
    if (!ok) {
        f->top = NULL; f->top_count = f->top_cap = 0;
        f->usings = NULL; f->using_count = f->using_cap = 0;
        pi_dirs_head = saved_head;
        pi_dirs_tail = saved_tail;
        return 0;
    }
    f->has_ext = ext != 0;
    g_scale_stats.metadata_cache_hits++;
    return 1;
}

static void pi_meta_cache_write(const pi_file_t *f, const char *src, size_t len) {
    if (!f || !f->path) return;
    uint64_t key = pi_meta_file_hash(f->path, src, len);
    char cache[1024], tmp[1060];
    pi_meta_cache_path(key, cache, sizeof(cache));
    if (!cache[0]) return;
    /* 创建the cache directory (one level deep suffices everywhere) */
    {
        char dir[1024];
        size_t dl = strlen(cache);
        size_t cut = dl;
        while (cut > 0 && cache[cut - 1] != '/' && cache[cut - 1] != '\\') cut--;
        if (cut == 0 || cut >= sizeof(dir)) return;
        memcpy(dir, cache, cut - 1);
        dir[cut - 1] = 0;
#ifdef _WIN32
        CreateDirectoryA(dir, NULL);
#else
        mkdir(dir, 0755);
#endif
    }
#ifdef _WIN32
    snprintf(tmp, sizeof(tmp), "%s.tmp.%lu", cache, (unsigned long)GetCurrentProcessId());
#else
    snprintf(tmp, sizeof(tmp), "%s.tmp.%ld", cache, (long)getpid());
#endif
    FILE *out = fopen(tmp, "wb");
    if (!out) return;
    uint32_t magic = PI_META_MAGIC, version = PI_META_VERSION;
    uint32_t top = (uint32_t)f->top_count;
    uint32_t idents = (uint32_t)f->ident_count;
    uint32_t usings = (uint32_t)f->using_count;
    unsigned char ext = (unsigned char)(f->has_ext != 0);
    int ok = fwrite(&magic, sizeof(magic), 1, out) == 1 &&
             fwrite(&version, sizeof(version), 1, out) == 1 &&
             fwrite(&top, sizeof(top), 1, out) == 1 &&
             fwrite(&idents, sizeof(idents), 1, out) == 1 &&
             fwrite(&usings, sizeof(usings), 1, out) == 1 &&
             fwrite(&ext, sizeof(ext), 1, out) == 1;
    for (uint32_t i = 0; ok && i < top; i++) {
        const pi_name_t *n = f->top[i];
        uint16_t len16 = n->len > UINT16_MAX ? 0 : (uint16_t)n->len;
        ok = len16 != 0 || n->len == 0;
        ok = ok && fwrite(&len16, sizeof(len16), 1, out) == 1 &&
             fwrite(n->str, 1, len16, out) == len16;
    }
    for (uint32_t i = 0; ok && i < idents; i++) {
        const pi_name_t *n = f->idents[i];
        uint16_t len16 = n->len > UINT16_MAX ? 0 : (uint16_t)n->len;
        ok = len16 != 0 || n->len == 0;
        ok = ok && fwrite(&len16, sizeof(len16), 1, out) == 1 &&
             fwrite(n->str, 1, len16, out) == len16;
    }
    for (uint32_t i = 0; ok && i < usings; i++) {
        size_t nlen = strlen(f->usings[i]);
        uint16_t len16 = nlen > UINT16_MAX ? 0 : (uint16_t)nlen;
        ok = nlen <= UINT16_MAX && fwrite(&len16, sizeof(len16), 1, out) == 1 &&
             fwrite(f->usings[i], 1, len16, out) == len16;
    }
    if (fclose(out) != 0) ok = 0;
    if (ok) {
#ifdef _WIN32
        ok = MoveFileExA(tmp, cache, MOVEFILE_REPLACE_EXISTING) != 0;
#else
        ok = rename(tmp, cache) == 0;
#endif
        if (ok) g_scale_stats.metadata_cache_writes++;
        else remove(tmp);
    } else remove(tmp);
}

static void pi_scan_file(pi_file_t *f) {
    g_scale_stats.metadata_scans++;
    size_t len = 0;
    char *src = read_file(f->path, &len);
    if (!src) {
        /* 内部辅助逻辑 */
        f->included = 1;
        return;
    }
    if (pi_meta_cache_load(f, src, len)) {
        free(src);
        return;
    }
    g_scale_stats.metadata_cache_misses++;
    zan_arena_t *arena = zan_arena_new();
    zan_diag_t *diag = zan_diag_new(arena);
    /* 内部辅助逻辑 */
    zan_diag_set_capture(diag, true);
    zan_lexer_t lex;
    zan_lexer_init(&lex, src, len, 0, arena, diag);
    zan_apply_lex_defines(&lex, pi_target, pi_pp_defines, pi_pp_define_count,
                          pi_publish_mode);
    int depth = 0;
    zan_token_kind_t prev = TK_EOF;
    pi_name_t *last_id = NULL;  /* 底层系统交互与数据协议契约 */
    for (;;) {
        zan_token_t tok = zan_lexer_next(&lex);
        if (tok.kind == TK_EOF) break;
        switch (tok.kind) {
        case TK_LBRACE:
            depth++;
            break;
        case TK_RBRACE:
            if (depth > 0) depth--;
            break;
        case TK_USING: {
            char subdir[512];
            size_t used = 0;
            tok = zan_lexer_next(&lex);
            if (tok.kind != TK_IDENT) { prev = TK_IDENT; continue; }
            for (;;) {
                if (used && used + 1 < sizeof(subdir)) subdir[used++] = '/';
                if (used + tok.str_val.len >= sizeof(subdir)) break;
                memcpy(subdir + used, tok.str_val.str, tok.str_val.len);
                used += tok.str_val.len;
                tok = zan_lexer_next(&lex);
                if (tok.kind != TK_DOT) break;
                tok = zan_lexer_next(&lex);
                if (tok.kind != TK_IDENT) break;
            }
            subdir[used] = 0;
            if (tok.kind == TK_SEMICOLON) pi_note_using(f, subdir);
            prev = tok.kind;
            continue;
        }
        case TK_CLASS:
        case TK_STRUCT:
        case TK_ENUM:
        case TK_INTERFACE:
            if (depth <= 1 && prev != TK_COLON && prev != TK_COMMA) {
                zan_token_t next = zan_lexer_peek(&lex);
                if (next.kind == TK_IDENT) {
                    tok = zan_lexer_next(&lex);
                    pi_name_t *name = pi_intern(tok.str_val.str,
                                                tok.str_val.len);
                    if (name &&
                        pi_reserve((void *)&f->top, f->top_count, &f->top_cap,
                                   sizeof(pi_name_t *)))
                        f->top[f->top_count++] = name;
                    prev = TK_IDENT;
                    continue;
                }
            }
            break;
        case TK_DELEGATE:
            /* 核心系统底层抽象与内存语义契约 */
            if (depth <= 1 && prev != TK_COLON && prev != TK_COMMA) {
                pi_name_t *name = NULL;
                int angle = 0;
                for (;;) {
                    tok = zan_lexer_next(&lex);
                    if (tok.kind == TK_EOF || tok.kind == TK_SEMICOLON)
                        break;
                    if (tok.kind == TK_LPAREN && angle == 0) break;
                    if (tok.kind == TK_LESS) angle++;
                    else if (tok.kind == TK_GREATER) { if (angle > 0) angle--; }
                    else if (tok.kind == TK_IDENT && angle == 0)
                        name = pi_intern(tok.str_val.str, tok.str_val.len);
                }
                if (name && tok.kind == TK_LPAREN &&
                    pi_reserve((void *)&f->top, f->top_count, &f->top_cap,
                               sizeof(pi_name_t *)))
                    f->top[f->top_count++] = name;
                /* 模块核心语义抽象与接口调用契约 */
                prev = tok.kind;
                continue;
            }
            break;
        case TK_THIS: {
            /* 模块核心语义抽象与接口调用契约 */
            zan_token_t next = zan_lexer_peek(&lex);
            if (prev == TK_LPAREN && (next.kind == TK_IDENT ||
                                      next.kind == TK_STRING ||
                                      next.kind == TK_BOOL ||
                                      next.kind == TK_CHAR ||
                                      next.kind == TK_INT ||
                                      next.kind == TK_LONG ||
                                      next.kind == TK_SHORT ||
                                      next.kind == TK_BYTE ||
                                      next.kind == TK_DOUBLE ||
                                      next.kind == TK_FLOAT ||
                                      next.kind == TK_UINT ||
                                      next.kind == TK_ULONG ||
                                      next.kind == TK_NINT ||
                                      next.kind == TK_OBJECT))
                f->has_ext = 1;
            break;
        }
        case TK_IDENT:
            /* 核心系统底层抽象与内存语义契约 */
            if (depth <= 1 && tok.str_val.len == 6 &&
                memcmp(tok.str_val.str, "record", 6) == 0 &&
                zan_lexer_peek(&lex).kind == TK_IDENT) {
                tok = zan_lexer_next(&lex);
                pi_name_t *name = pi_intern(tok.str_val.str, tok.str_val.len);
                if (name &&
                    pi_reserve((void *)&f->top, f->top_count, &f->top_cap,
                               sizeof(pi_name_t *)))
                    f->top[f->top_count++] = name;
                prev = TK_IDENT;
                continue;
            }
            pi_name_t *cur_id = pi_intern(tok.str_val.str,
                                          tok.str_val.len);
            if (prev != TK_DOT) last_id = cur_id;
            pi_flag_ident(f, tok.str_val.str, tok.str_val.len);
            /* 核心系统底层抽象与内存语义契约 */
            if (prev == TK_DOT && last_id && last_id->len == 4 &&
                memcmp(last_id->str, "Task", 4) == 0 && tok.str_val.len == 7 &&
                (memcmp(tok.str_val.str, "WhenAll", 7) == 0 ||
                 memcmp(tok.str_val.str, "WhenAny", 7) == 0))
                pi_flag_ident(f, "TaskJoin", 8);
            break;
        default:
            break;
        }
        prev = tok.kind;
    }
    zan_diag_free_buffers(diag);
    zan_arena_free(arena);
    pi_meta_cache_write(f, src, len);
    free(src);
}

/* 内部辅助实现 */
/* 内部辅助逻辑 */
static int pi_typeish(zan_token_kind_t k) {
    return k == TK_IDENT || k == TK_STRING || k == TK_BOOL || k == TK_CHAR ||
           k == TK_INT || k == TK_LONG || k == TK_SHORT || k == TK_BYTE ||
           k == TK_DOUBLE || k == TK_FLOAT || k == TK_UINT ||
           k == TK_ULONG || k == TK_NINT || k == TK_OBJECT ||
           k == TK_VOID;
}

static void pi_seed_source(const char *source, size_t len) {
    g_scale_stats.seed_sources++;
    zan_arena_t *arena = zan_arena_new();
    zan_diag_t *diag = zan_diag_new(arena);
    zan_lexer_t lex;

    /* 内部辅助逻辑 */
    for (int pass = 0; pass < 2; pass++) {
        g_scale_stats.seed_lex_passes++;
        zan_lexer_init(&lex, source, len, 0, arena, diag);
        zan_apply_lex_defines(&lex, pi_target, pi_pp_defines,
                              pi_pp_define_count, pi_publish_mode);
        int depth = 0;
        /* 内部辅助逻辑 */
        int ns_file_scoped = 0;
        zan_token_kind_t prev = TK_EOF;
        pi_name_t *chain = NULL;    /* 核心系统底层抽象与内存语义契约 */
        for (;;) {
            zan_token_t tok = zan_lexer_next(&lex);
            if (tok.kind == TK_EOF) break;
            switch (tok.kind) {
            case TK_LBRACE:
                depth++;
                break;
            case TK_RBRACE:
                if (depth > 0) depth--;
                break;
            case TK_USING: {
                char subdir[512];
                size_t used = 0;
                tok = zan_lexer_next(&lex);
                if (tok.kind != TK_IDENT) { prev = TK_IDENT; continue; }
                for (;;) {
                    if (used && used + 1 < sizeof(subdir))
                        subdir[used++] = '/';
                    if (used + tok.str_val.len >= sizeof(subdir)) break;
                    memcpy(subdir + used, tok.str_val.str, tok.str_val.len);
                    used += tok.str_val.len;
                    tok = zan_lexer_next(&lex);
                    if (tok.kind != TK_DOT) break;
                    tok = zan_lexer_next(&lex);
                    if (tok.kind != TK_IDENT) break;
                }
                subdir[used] = 0;
                if (tok.kind == TK_SEMICOLON && used > 0) {
                    pi_reach(subdir);
                    /* 底层系统交互与数据协议契约 */
                    for (char *seg = subdir; *seg; ) {
                        char *dot = strchr(seg, '/');
                        if (dot) *dot = 0;
                        pi_name_t *s = pi_intern(seg, strlen(seg));
                        if (s) s->ns_root = 1;
                        if (!dot) break;
                        seg = dot + 1;
                    }
                }
                prev = tok.kind;
                chain = NULL;
                continue;
            }
            case TK_NAMESPACE: {
                /* 模块核心语义抽象与接口调用契约 */
                char nsname[512];
                size_t used = 0;
                tok = zan_lexer_next(&lex);
                if (tok.kind != TK_IDENT) { prev = TK_IDENT; continue; }
                for (;;) {
                    if (used && used + 1 < sizeof(nsname))
                        nsname[used++] = '/';
                    if (used + tok.str_val.len >= sizeof(nsname)) break;
                    memcpy(nsname + used, tok.str_val.str, tok.str_val.len);
                    used += tok.str_val.len;
                    tok = zan_lexer_next(&lex);
                    if (tok.kind != TK_DOT) break;
                    tok = zan_lexer_next(&lex);
                    if (tok.kind != TK_IDENT) break;
                }
                nsname[used] = 0;
                if (tok.kind == TK_SEMICOLON)
                    ns_file_scoped = 1;   /* 核心系统底层抽象与内存语义契约 */
                if (tok.kind == TK_SEMICOLON || tok.kind == TK_LBRACE) {
                    for (char *seg = nsname; *seg; ) {
                        char *dot = strchr(seg, '/');
                        if (dot) *dot = 0;
                        pi_name_t *s = pi_intern(seg, strlen(seg));
                        if (s) s->ns_root = 1;
                        if (!dot) break;
                        seg = dot + 1;
                    }
                }
                prev = tok.kind;
                chain = NULL;
                continue;
            }
            case TK_CLASS:
            case TK_STRUCT:
            case TK_ENUM:
            case TK_INTERFACE:
                if (depth == 0 && !ns_file_scoped &&
                    prev != TK_COLON && prev != TK_COMMA &&
                    pass == 1) {
                    zan_token_t next = zan_lexer_peek(&lex);
                    if (next.kind == TK_IDENT) {
                        tok = zan_lexer_next(&lex);
                        pi_name_t *name = pi_intern(tok.str_val.str,
                                                    tok.str_val.len);
                        if (name) name->user_decl = 1;
                        prev = TK_IDENT;
                        chain = NULL;
                        continue;
                    }
                }
                break;
            case TK_DELEGATE:
                if (depth == 0 && !ns_file_scoped &&
                    prev != TK_COLON && prev != TK_COMMA &&
                    pass == 1) {
                    pi_name_t *name = NULL;
                    int angle = 0;
                    for (;;) {
                        tok = zan_lexer_next(&lex);
                        if (tok.kind == TK_EOF || tok.kind == TK_SEMICOLON)
                            break;
                        if (tok.kind == TK_LPAREN && angle == 0) break;
                        if (tok.kind == TK_LESS) angle++;
                        else if (tok.kind == TK_GREATER) {
                            if (angle > 0) angle--;
                        } else if (tok.kind == TK_IDENT && angle == 0)
                            name = pi_intern(tok.str_val.str, tok.str_val.len);
                    }
                    if (name && tok.kind == TK_LPAREN) name->user_decl = 1;
                    prev = tok.kind;
                    chain = NULL;
                    continue;
                }
                break;
            case TK_IDENT:
                /* 核心系统底层抽象与内存语义契约 */
                if (depth == 0 && !ns_file_scoped &&
                    tok.str_val.len == 6 &&
                    memcmp(tok.str_val.str, "record", 6) == 0 &&
                    zan_lexer_peek(&lex).kind == TK_IDENT && pass == 1) {
                    tok = zan_lexer_next(&lex);
                    pi_name_t *name = pi_intern(tok.str_val.str,
                                                tok.str_val.len);
                    if (name) name->user_decl = 1;
                    prev = TK_IDENT;
                    chain = NULL;
                    continue;
                }
                {
                    pi_name_t *name = pi_intern(tok.str_val.str,
                                                tok.str_val.len);
                    if (pass == 1 && name) {
                        if (prev != TK_DOT) {
                            chain = name;
                            zan_token_t next = zan_lexer_peek(&lex);
                            /* 核心系统底层抽象与内存语义契约 */
                            if (name->len == 4 &&
                                memcmp(name->str, "Task", 4) == 0 &&
                                next.kind == TK_DOT) {
                                zan_token_t meth =
                                    zan_lexer_peek2(&lex);
                                if (meth.kind == TK_IDENT &&
                                    ((meth.str_val.len == 7 &&
                                      memcmp(meth.str_val.str,
                                             "WhenAll", 7) == 0) ||
                                     (meth.str_val.len == 7 &&
                                      memcmp(meth.str_val.str,
                                             "WhenAny", 7) == 0))) {
                                    pi_name_t *tj =
                                        pi_intern("TaskJoin", 8);
                                    if (tj) { tj->flagged = 1; if (pi_seeding_stdlib) tj->flagged_stdlib = 1; }
                                }
                            }
                            if (depth >= 1 && pi_typeish(prev) &&
                                (next.kind == TK_LPAREN ||
                                 next.kind == TK_LBRACE)) {
                                /* 内部辅助实现 */
                            } else if (!name->user_decl) {
                                name->flagged = 1;
                                if (pi_seeding_stdlib)
                                    name->flagged_stdlib = 1;
                            }
                        } else if (chain && chain->ns_root) {
                            /* 核心系统底层抽象与内存语义契约 */
                            name->flagged = 1;
                            if (pi_seeding_stdlib)
                                name->flagged_stdlib = 1;
                        } else if (chain && chain->len == 4 &&
                                   memcmp(chain->str, "Task", 4) == 0 &&
                                   name->len == 7 &&
                                   (memcmp(name->str, "WhenAll", 7) == 0 ||
                                    memcmp(name->str, "WhenAny", 7) == 0)) {
                            /* 核心系统底层抽象与内存语义契约 */
                            pi_name_t *tj = pi_intern("TaskJoin", 8);
                            if (tj) { tj->flagged = 1; if (pi_seeding_stdlib) tj->flagged_stdlib = 1; }
                        }
                    }
                }
                break;
            case TK_DOT:
                /* 内部辅助逻辑 */
                break;
            default:
                chain = NULL;
                break;
            }
            /* 内部辅助逻辑 */
            if (tok.kind != TK_DOT && tok.kind != TK_IDENT) chain = NULL;
            prev = tok.kind;
        }
    }
    zan_arena_free(arena);
}

/* 内部辅助实现 */

static void pi_seed_chain(const zan_ast_node_t *n, int in_chain);

static void pi_seed_ast(const zan_ast_node_t *n) { pi_seed_chain(n, 0); }

static void pi_seed_list(const zan_ast_list_t *list) {
    for (int i = 0; i < list->count; i++) pi_seed_ast(list->items[i]);
}

static void pi_flag_istr(zan_istr_t name) {
    if (!name.str || name.len <= 0) return;
    pi_name_t *p = pi_intern(name.str, (size_t)name.len);
    if (p && !p->user_decl) {
        p->flagged = 1;
        if (pi_seeding_stdlib) p->flagged_stdlib = 1;
    }
}

/* 模块核心语义抽象与接口调用契约 */
static void pi_flag_qualified(zan_istr_t name) {
    if (!name.str || name.len <= 0) return;
    pi_name_t *p = pi_intern(name.str, (size_t)name.len);
    if (p) {
        p->flagged = 1;
        if (pi_seeding_stdlib) p->flagged_stdlib = 1;
    }
}

static int pi_is_ns_root(zan_istr_t name) {
    if (!name.str || name.len <= 0) return 0;
    pi_name_t *p = pi_intern(name.str, (size_t)name.len);
    return p && p->ns_root;
}

static void pi_seed_chain(const zan_ast_node_t *n, int in_chain) {
    if (!n) return;
    if (n->meta && n->meta->attributes.count > 0)
        pi_seed_list(&n->meta->attributes);
    switch (n->kind) {
    /* 模块核心语义抽象与接口调用契约 */
    case AST_TYPE_REF:
        /* 内部辅助逻辑 */
        if (n->type_ref.name.str) {
            const char *s = n->type_ref.name.str;
            unsigned start = 0;
            for (unsigned ci = 0; ci <= n->type_ref.name.len; ci++) {
                if (ci == n->type_ref.name.len || s[ci] == '.') {
                    if (ci > start)
                        pi_flag_ident(NULL, s + start, (size_t)(ci - start));
                    start = ci + 1;
                }
            }
        }
        pi_seed_list(&n->type_ref.type_args);
        pi_seed_ast(n->type_ref.array_element);
        return;
    case AST_QUALIFIED_NAME:
        /* a */
        for (int qi = 0; qi < n->qualified_name.parts.count; qi++)
            pi_flag_istr(n->qualified_name.parts.items[qi]->ident.name);
        return;

    /* 底层系统交互与数据协议契约 */
    case AST_METHOD_DECL:
    case AST_CONSTRUCTOR_DECL:
    case AST_DESTRUCTOR_DECL:
    case AST_DELEGATE_DECL:
        pi_seed_ast(n->method_decl.return_type);
        pi_seed_list(&n->method_decl.params);
        pi_seed_list(&n->method_decl.type_params);
        if (n->method_decl.ext) {
            pi_seed_list(&n->method_decl.ext->where_clauses);
            pi_seed_list(&n->method_decl.ext->base_args);
        }
        pi_seed_ast(n->method_decl.body);
        return;
    case AST_CLASS_DECL:
    case AST_STRUCT_DECL:
    case AST_INTERFACE_DECL:
        pi_seed_list(&n->type_decl.bases);
        pi_seed_list(&n->type_decl.members);
        if (n->type_decl.where_clauses)
            pi_seed_list(n->type_decl.where_clauses);
        return;
    case AST_ENUM_DECL:
        pi_seed_list(&n->type_decl.bases);
        pi_seed_list(&n->type_decl.members);
        return;
    case AST_FIELD_DECL:
    case AST_PROPERTY_DECL:
        pi_seed_ast(n->field_decl.type);
        pi_seed_ast(n->field_decl.initializer);
        pi_seed_ast(n->field_decl.getter_body);
        pi_seed_ast(n->field_decl.setter_body);
        if (n->field_decl.indexer_params)
            pi_seed_list(n->field_decl.indexer_params);
        return;
    case AST_ENUM_MEMBER:
        pi_seed_ast(n->enum_member.value);
        return;
    case AST_PARAM:
        pi_seed_ast(n->param.type);
        pi_seed_ast(n->param.default_val);
        return;
    case AST_VAR_DECL:
        pi_seed_ast(n->var_decl.type);
        pi_seed_ast(n->var_decl.initializer);
        return;
    case AST_TUPLE_DECON:
        pi_seed_list(&n->tuple_decon.types);
        pi_seed_ast(n->tuple_decon.initializer);
        return;
    case AST_WHERE_CLAUSE:
        pi_seed_list(&n->where_clause.constraints);
        return;
    case AST_ATTRIBUTE:
        if (n->attribute.name) {
            if (n->attribute.name->kind == AST_IDENTIFIER)
                pi_flag_istr(n->attribute.name->ident.name);
            else
                pi_seed_ast(n->attribute.name);
        }
        pi_seed_list(&n->attribute.args);
        return;
    case AST_USING_DECL:
        return;
    case AST_NAMESPACE_DECL:
        pi_seed_list(&n->namespace_decl.members);
        return;

    /* statements */
    case AST_COMPILATION_UNIT:
        /* 内部辅助逻辑 */
        pi_seed_ast(n->comp_unit.ns);
        pi_seed_list(&n->comp_unit.decls);
        return;
    case AST_BLOCK:
        pi_seed_list(&n->block.stmts);
        return;
    case AST_EXPR_STMT:
        pi_seed_ast(n->expr_stmt.expr);
        return;
    case AST_RETURN_STMT:
        pi_seed_ast(n->ret.value);
        return;
    case AST_IF_STMT:
        pi_seed_ast(n->if_stmt.cond);
        pi_seed_ast(n->if_stmt.then_body);
        pi_seed_ast(n->if_stmt.else_body);
        return;
    case AST_WHILE_STMT:
    case AST_DO_WHILE_STMT:
        pi_seed_ast(n->while_stmt.cond);
        pi_seed_ast(n->while_stmt.body);
        return;
    case AST_FOR_STMT:
        pi_seed_ast(n->for_stmt.init);
        pi_seed_ast(n->for_stmt.cond);
        pi_seed_ast(n->for_stmt.step);
        pi_seed_ast(n->for_stmt.body);
        return;
    case AST_FOREACH_STMT:
        pi_seed_ast(n->foreach_stmt.var_type);
        pi_seed_ast(n->foreach_stmt.collection);
        pi_seed_ast(n->foreach_stmt.body);
        return;
    case AST_THROW_STMT:
        pi_seed_ast(n->throw_stmt.value);
        return;
    case AST_TRY_STMT:
        pi_seed_ast(n->try_stmt.try_body);
        pi_seed_list(&n->try_stmt.catches);
        pi_seed_ast(n->try_stmt.finally_body);
        return;
    case AST_CATCH_CLAUSE:
        pi_seed_ast(n->catch_clause.type);
        pi_seed_ast(n->catch_clause.body);
        return;
    case AST_SWITCH_STMT:
        pi_seed_ast(n->switch_stmt.expr);
        pi_seed_list(&n->switch_stmt.cases);
        return;
    case AST_SWITCH_CASE:
        pi_seed_ast(n->switch_case.pattern);
        pi_seed_ast(n->switch_case.type_pattern);
        pi_seed_ast(n->switch_case.when_cond);
        pi_seed_ast(n->switch_case.body);
        return;
    case AST_SWITCH_EXPR:
        pi_seed_ast(n->switch_expr.expr);
        pi_seed_list(&n->switch_expr.arms);
        return;
    case AST_SWITCH_ARM:
        pi_seed_ast(n->switch_arm.pattern);
        pi_seed_ast(n->switch_arm.type_pattern);
        pi_seed_ast(n->switch_arm.when_cond);
        pi_seed_ast(n->switch_arm.result);
        return;
    case AST_YIELD_STMT:
        pi_seed_ast(n->yield_stmt.value);
        return;
    case AST_LOCK_STMT:
        pi_seed_ast(n->lock_stmt.expr);
        pi_seed_ast(n->lock_stmt.body);
        return;
    case AST_CHECKED_STMT:
        pi_seed_ast(n->checked_stmt.body);
        return;
    case AST_QUERY_EXPR:
        pi_seed_ast(n->query.source);
        pi_seed_list(&n->query.clauses);
        pi_seed_ast(n->query.group_expr);
        pi_seed_ast(n->query.group_key);
        pi_seed_ast(n->query.select);
        return;
    case AST_QUERY_WHERE:
    case AST_QUERY_LET:
    case AST_QUERY_ORDERBY:
        pi_seed_ast(n->query_clause.expr);
        return;
    case AST_QUERY_JOIN:
        pi_seed_ast(n->query_clause.source);
        pi_seed_ast(n->query_clause.left_key);
        pi_seed_ast(n->query_clause.right_key);
        return;
    case AST_WITH_EXPR:
        pi_seed_ast(n->with_expr.expr);
        pi_seed_list(&n->with_expr.assigns);
        return;

    /* expressions */
    case AST_IDENTIFIER:
        /* 内部辅助逻辑 */
        pi_seed_ast(n->ident.inst_type_ref);
        return;
    case AST_BINARY:
    case AST_ASSIGNMENT:
        pi_seed_ast(n->binary.left);
        pi_seed_ast(n->binary.right);
        return;
    case AST_UNARY:
    case AST_POSTFIX_UNARY:
        pi_seed_ast(n->unary.operand);
        return;
    case AST_CALL:
        /* 模块核心语义抽象与接口调用契约 */
        if (n->call.callee && n->call.callee->kind != AST_IDENTIFIER)
            pi_seed_chain(n->call.callee, 0);
        pi_seed_list(&n->call.args);
        pi_seed_list(&n->call.type_args);
        return;
    case AST_MEMBER_ACCESS: {
        zan_ast_node_t *obj = n->member.object;
        /* `Ns */
        if (in_chain) {
            zan_ast_node_t *rt = n;
            while (rt->kind == AST_MEMBER_ACCESS)
                rt = rt->member.object;
            if (rt->kind == AST_IDENTIFIER &&
                pi_is_ns_root(rt->ident.name))
                pi_flag_qualified(n->member.name);
            else
                pi_flag_istr(n->member.name);
        }
        if (obj && obj->kind == AST_IDENTIFIER) {
            /* `Root */
            pi_flag_istr(obj->ident.name);
            /* 核心系统底层抽象与内存语义契约 */
            if (obj->ident.name.len == 4 &&
                memcmp(obj->ident.name.str, "Task", 4) == 0) {
                zan_istr_t m = n->member.name;
                if (m.str && m.len == 7 &&
                    (memcmp(m.str, "WhenAll", 7) == 0 ||
                     memcmp(m.str, "WhenAny", 7) == 0)) {
                    pi_name_t *tj = pi_intern("TaskJoin", 8);
                    if (tj) { tj->flagged = 1; if (pi_seeding_stdlib) tj->flagged_stdlib = 1; }
                }
            }
        }
        pi_seed_chain(obj, 1);
        return;
    }
    case AST_INDEX:
        pi_seed_ast(n->index.object);
        pi_seed_ast(n->index.index);
        pi_seed_list(&n->index.extra);
        return;
    case AST_NEW_EXPR:
        pi_seed_ast(n->new_expr.type);
        pi_seed_ast(n->new_expr.call_init);
        pi_seed_list(&n->new_expr.args);
        pi_seed_list(&n->new_expr.arg_inits);
        return;
    case AST_CAST_EXPR:
    case AST_TYPEOF_EXPR:
    case AST_SIZEOF_EXPR:
        pi_seed_ast(n->cast.type);
        pi_seed_ast(n->cast.expr);
        return;
    case AST_IS_EXPR:
    case AST_AS_EXPR:
        pi_seed_ast(n->type_test.expr);
        pi_seed_ast(n->type_test.type);
        return;
    case AST_CONDITIONAL:
        pi_seed_ast(n->conditional.cond);
        pi_seed_ast(n->conditional.then_expr);
        pi_seed_ast(n->conditional.else_expr);
        return;
    case AST_LAMBDA:
        pi_seed_list(&n->lambda.params);
        pi_seed_ast(n->lambda.body);
        return;
    case AST_AWAIT_EXPR:
        pi_seed_ast(n->await_expr.expr);
        return;
    case AST_STRING_INTERP:
        pi_seed_list(&n->string_interp.parts);
        pi_seed_list(&n->string_interp.formats);
        return;
    case AST_TUPLE_EXPR:
        pi_seed_list(&n->tuple_expr.items);
        return;
    case AST_TUPLE_TYPE:
        pi_seed_list(&n->tuple_type.elems);
        return;
    case AST_REF_ARG:
        pi_seed_ast(n->ref_arg.expr);
        pi_seed_ast(n->ref_arg.decl_type);
        return;
    case AST_NAMED_ARG:
        pi_seed_ast(n->named_arg.expr);
        return;
    case AST_COLL_INIT:
        pi_seed_list(&n->coll_init.items);
        return;

    /* 底层系统交互与数据协议契约 */
    default:
        return;
    }
}

static void pi_add_package_source(const char *path, void *context) {
    pi_dir_t *d = (pi_dir_t *)context;
    pi_add_file(d, path);
    if (d->file_count > 0) { d->files[d->file_count - 1].pkg_src = 1; }
}

/* 模块核心语义抽象与接口调用契约 */
static void pi_process_dir(pi_dir_t *d, const char *stdlib_root) {
    if (d->reached) return;
    d->reached = 1;
    int before = d->file_count;
    pi_glob_into(d, stdlib_root, d->subdir);
    /* 内部辅助实现 */
    int hierarchical = stdlib_has_dir(stdlib_root, d->subdir) == 0;
    int found = d->file_count != before;
    int pre_visit = d->file_count;
    int package_count = package_visit_namespace(d->subdir, pi_add_package_source,
                                                  d, hierarchical);
    found = found || package_count > 0;
    /* 内部辅助逻辑 */
    if (!hierarchical) {
        for (int i = pre_visit; i < d->file_count; i++)
            d->files[i].gate_live = 1;
    }
    /* 内部辅助逻辑 */
    if (!found && package_project_root[0] != '\0' &&
        strcmp(d->subdir, "System") != 0 &&
        !project_namespace_declared(d->subdir)) {
        fprintf(stderr, "ZANPKG_MISSING namespace=%s\n", d->subdir);
        missing_namespace_count++;
    }
    for (int i = 0; i < d->file_count; i++)
        pi_scan_file(&d->files[i]);
}

/* 内部辅助逻辑 */
static int pi_close_once(const char *stdlib_root) {
    int changed = 0;
    for (pi_dir_t *d = pi_dirs_head; d; d = d->next) {
        if (!d->reached) pi_process_dir(d, stdlib_root);
        for (int i = 0; i < d->file_count; i++) {
            pi_file_t *f = &d->files[i];
            if (f->included) continue;
            int hit = f->has_ext || (f->pkg_src && !f->gate_live);
            const char *why = f->has_ext ? "ext"
                              : (f->pkg_src ? (f->gate_live ? NULL : "pkg")
                                             : NULL);
            for (int k = 0; k < f->top_count && !hit; k++)
                if (f->top[k]->flagged) { hit = 1; why = f->top[k]->str; }
            if (!hit) continue;
            f->included = 1;
            if (getenv("ZAN_PULLIN_DEBUG") != NULL)
                fprintf(stderr, "[pullin] incl %s because %s\n",
                        f->path ? f->path : "?", why ? why : "?");
            changed = 1;
            for (int k = 0; k < f->using_count; k++)
                pi_reach(f->usings[k]);
        }
    }
    return changed;
}

/* 模块核心语义抽象与接口调用契约 */

static int pi_repair_done = 0;

/* 底层系统交互与数据协议契约 */
static pi_name_t *pi_unsatisfied_set[256];
static int pi_unsatisfied_count = 0;

static int pi_name_declared_by_scanned(pi_name_t *n) {
    for (pi_dir_t *d = pi_dirs_head; d; d = d->next)
        for (int i = 0; i < d->file_count; i++)
            for (int k = 0; k < d->files[i].top_count; k++)
                if (d->files[i].top[k] == n) return 1;
    return 0;
}

/* 内部辅助实现 */
static int pi_unsatisfied_live_name(void) {
    pi_unsatisfied_count = 0;
    for (unsigned b = 0; b < PI_BUCKETS; b++)
        for (pi_name_t *p = pi_table[b]; p; p = p->next) {
            if (!p->flagged_stdlib || pi_name_declared_by_scanned(p)) continue;
            if (pi_unsatisfied_count >= (int)(
                    sizeof(pi_unsatisfied_set) / sizeof(pi_unsatisfied_set[0])))
                return 1;
            pi_unsatisfied_set[pi_unsatisfied_count++] = p;
        }
    return pi_unsatisfied_count > 0;
}

static void pi_unsatisfied_mark_declared(pi_name_t *n) {
    for (int i = 0; i < pi_unsatisfied_count; i++) {
        if (pi_unsatisfied_set[i] != n) continue;
        pi_unsatisfied_set[i] = pi_unsatisfied_set[--pi_unsatisfied_count];
        return;
    }
}

static int pi_name_in_unsatisfied(pi_name_t *n) {
    for (int i = 0; i < pi_unsatisfied_count; i++)
        if (pi_unsatisfied_set[i] == n) return 1;
    return 0;
}

/* 核心系统底层抽象与内存语义契约 */
static void pi_repair_scan_file(const char *path) {
    pi_file_t f;
    memset(&f, 0, sizeof(f));
    f.path = (char *)path;
    pi_repair_scanning = 1;
    pi_scan_file(&f);
    pi_repair_scanning = 0;
    for (int k = 0; k < f.top_count; k++)
        pi_unsatisfied_mark_declared(f.top[k]);
}

/* 内部辅助逻辑 */
static int pi_repair_walk(const char *root, const char *rel) {
    char dir_path[1024];
    if (rel[0]) snprintf(dir_path, sizeof(dir_path), "%s/%s", root, rel);
    else snprintf(dir_path, sizeof(dir_path), "%s", root);
    int reached_any = 0;
    int dir_declares = 0;
#ifdef _WIN32
    char pattern[1024];
    snprintf(pattern, sizeof(pattern), "%s\\*", dir_path);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    do {
        if (fd.cFileName[0] == '.') continue;
        char child_path[1024];
        snprintf(child_path, sizeof(child_path), "%s\\%s", dir_path,
                 fd.cFileName);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) continue;
            char child[512];
            snprintf(child, sizeof(child), "%s%s%s", rel, rel[0] ? "/" : "",
                     fd.cFileName);
            if (pi_repair_walk(root, child)) reached_any = 1;
            continue;
        }
        size_t nlen = strlen(fd.cFileName);
        if (nlen < 5 || strcmp(fd.cFileName + nlen - 4, ".zan") != 0) continue;
        int before = pi_unsatisfied_count;
        pi_repair_scan_file(child_path);
        if (pi_unsatisfied_count < before) dir_declares = 1;
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#else
    DIR *d = opendir(dir_path);
    if (!d) return 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.') continue;
        char entry_path[1024];
        snprintf(entry_path, sizeof(entry_path), "%s/%s", dir_path,
                 e->d_name);
        struct stat st;
        if (lstat(entry_path, &st) != 0) continue;
        if (S_ISDIR(st.st_mode) && !S_ISLNK(st.st_mode)) {
            char child[512];
            snprintf(child, sizeof(child), "%s%s%s", rel, rel[0] ? "/" : "",
                     e->d_name);
            if (pi_repair_walk(root, child)) reached_any = 1;
            continue;
        }
        if (!S_ISREG(st.st_mode)) continue;
        size_t nlen = strlen(e->d_name);
        if (nlen < 5 || strcmp(e->d_name + nlen - 4, ".zan") != 0) continue;
        int before = pi_unsatisfied_count;
        pi_repair_scan_file(entry_path);
        if (pi_unsatisfied_count < before) dir_declares = 1;
    }
    closedir(d);
#endif
    if (dir_declares) {
        if (getenv("ZAN_PULLIN_DEBUG") != NULL)
            fprintf(stderr, "[pullin] repair reach %s\n",
                    rel[0] ? rel : "(root)");
        pi_reach(rel);
        reached_any = 1;
    }
    return reached_any;
}

/* 内部辅助逻辑 */
static void pi_seed_parsed_unit(zan_ast_node_t *unit, int is_entry) {
    {
        /* 内部辅助实现 */
        for (int i = 0; i < unit->comp_unit.usings.count; i++) {
            zan_ast_node_t *u = unit->comp_unit.usings.items[i];
            zan_ast_node_t *qn = u ? u->using_decl.name : NULL;
            if (!qn || qn->kind != AST_QUALIFIED_NAME) continue;
            for (int k = 0; k < qn->qualified_name.parts.count; k++) {
                zan_istr_t seg =
                    qn->qualified_name.parts.items[k]->ident.name;
                if (!seg.str) continue;
                pi_name_t *nr = pi_intern(seg.str, (size_t)seg.len);
                if (nr) nr->ns_root = 1;
            }
        }
        if (unit->comp_unit.ns &&
            unit->comp_unit.ns->kind == AST_NAMESPACE_DECL) {
            zan_ast_node_t *qn = unit->comp_unit.ns->namespace_decl.name;
            if (qn && qn->kind == AST_QUALIFIED_NAME)
                for (int k = 0; k < qn->qualified_name.parts.count; k++) {
                    zan_istr_t seg =
                        qn->qualified_name.parts.items[k]->ident.name;
                    if (!seg.str) continue;
                    pi_name_t *nr = pi_intern(seg.str, (size_t)seg.len);
                    if (nr) nr->ns_root = 1;
                }
        }
        if (is_entry) {
            for (int i = 0; i < unit->comp_unit.usings.count; i++) {
                zan_ast_node_t *u = unit->comp_unit.usings.items[i];
                zan_ast_node_t *qn = u ? u->using_decl.name : NULL;
                if (!qn || qn->kind != AST_QUALIFIED_NAME) continue;
                char subdir[1024];
                size_t used = 0;
                for (int k = 0; k < qn->qualified_name.parts.count; k++) {
                    zan_istr_t seg =
                        qn->qualified_name.parts.items[k]->ident.name;
                    if (!seg.str) continue;
                    if (used && used + 1 < sizeof(subdir))
                        subdir[used++] = '/';
                    for (unsigned c = 0;
                         c < seg.len && used + 1 < sizeof(subdir); c++)
                        subdir[used++] = seg.str[c];
                }
                subdir[used] = 0;
                pi_reach(subdir);
                if (u->using_decl.is_static &&
                    qn->qualified_name.parts.count > 0)
                    pi_flag_istr(qn->qualified_name.parts.items[
                        qn->qualified_name.parts.count - 1]->ident.name);
            }
        }
        /* 内部辅助实现 */
        if (is_entry && !pi_seed_stdlib_input) {
            for (int i = 0; i < unit->comp_unit.decls.count; i++) {
                zan_ast_node_t *d = unit->comp_unit.decls.items[i];
                if (!d || (d->kind != AST_CLASS_DECL && d->kind != AST_STRUCT_DECL &&
                           d->kind != AST_INTERFACE_DECL &&
                           d->kind != AST_DELEGATE_DECL))
                    continue;
                if (zan_ast_ns_name(d).len > 0) continue;
                /* 底层系统交互与数据协议契约 */
                zan_istr_t name = d->type_decl.name;
                if (!name.str || name.len <= 0) continue;
                pi_name_t *nm = pi_intern(name.str,
                                          (size_t)name.len);
                if (nm) nm->user_decl = 1;
            }
        }
        pi_seed_ast(unit);
    }
}

static void pi_debug_dump(void) {
    if (getenv("ZAN_PULLIN_DEBUG") == NULL) return;
    for (pi_dir_t *d = pi_dirs_head; d; d = d->next)
        for (int i = 0; i < d->file_count; i++)
            fprintf(stderr, "[pullin] %s %s (top=%d, idents=%d)\n",
                    d->files[i].included ? "INCL" : "skip",
                    d->files[i].path ? d->files[i].path : "(null)",
                    d->files[i].top_count, d->files[i].ident_count);
}

static int pi_append_included(const char ***files, int *count, int *cap);

/* 内部辅助逻辑 */
static int pi_close_converged(const char *stdlib_root, const char ***files,
                              int *count, int *cap) {
    int appended = 0;
    for (;;) {
        int changed = pi_close_once(stdlib_root);
        int fresh = pi_append_included(files, count, cap);
        appended += fresh;
        if (!changed && !fresh) break;
    }
    pi_debug_dump();
    return appended;
}

/* 模块核心语义抽象与接口调用契约 */
static int pi_append_included(const char ***files, int *count, int *cap) {
    int before = *count;
    for (pi_dir_t *d = pi_dirs_head; d; d = d->next)
        for (int i = 0; i < d->file_count; i++) {
            pi_file_t *f = &d->files[i];
            if (f->included && !f->parsed && f->path) {
                add_stdlib_input(files, count, cap, f->path);
                f->parsed = 1;
            }
        }
    return *count - before;
}

/* 内部辅助逻辑 */
static void zan_apply_lex_defines(zan_lexer_t *lex, zan_target_t target,
                                  const char *const *pp_defines,
                                  int pp_define_count,
                                  bool is_publish) {
    switch (target.os) {
    case ZAN_OS_WINDOWS:
        zan_lexer_define(lex, "WINDOWS", "1");
        zan_lexer_define(lex, "WIN32", "1");
        break;
    case ZAN_OS_LINUX:
        zan_lexer_define(lex, "LINUX", "1");
        break;
    case ZAN_OS_ANDROID:
        /* 内部辅助逻辑 */
        zan_lexer_define(lex, "LINUX", "1");
        zan_lexer_define(lex, "ANDROID", "1");
        break;
    case ZAN_OS_OHOS:
        /* 内部辅助逻辑 */
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
            if (nl > 63) nl = 63;
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

/* 内部辅助逻辑 */
static zan_ast_node_t *parse_secondary_unit(const char *path,
                                            zan_target_t target,
                                            const char *const *pp_defines,
                                            int pp_define_count,
                                            bool is_publish,
                                            zan_arena_t *arena,
                                            zan_diag_t *diag) {
    g_scale_stats.secondary_parses++;
    size_t slen = 0;
    char *src = read_file(path, &slen);
    if (!src) return NULL;
    /* 内部辅助实现 */
    char *heap_src = src;
    src = zan_arena_strdup(arena, heap_src, slen);
    free(heap_src);
    if (!src) return NULL;
    int file_id = diag->file_count;
    zan_diag_add_file(diag, path, src);
    zan_lexer_t lex;
    zan_lexer_init(&lex, src, slen, file_id, arena, diag);
    zan_apply_lex_defines(&lex, target, pp_defines, pp_define_count,
                          is_publish);
    zan_parser_t parser;
    zan_parser_init(&parser, &lex, arena, diag);
    zan_ast_node_t *unit = zan_parser_parse(&parser);
    return unit;
}

static void dump_tokens(const char *source, size_t len, const char *filename) {
    zan_arena_t *arena = zan_arena_new();
    zan_diag_t *diag = zan_diag_new(arena);
    zan_diag_add_file(diag, filename, source);

    zan_lexer_t lex;
    zan_lexer_init(&lex, source, len, 0, arena, diag);

    for (;;) {
        zan_token_t tok = zan_lexer_next(&lex);
        if (tok.kind == TK_EOF) break;

        printf("%4u:%2u  %-20s", tok.loc.line, tok.loc.col,
               zan_token_kind_name(tok.kind));

        switch (tok.kind) {
        case TK_INT_LIT:
            printf("  %lld", (long long)tok.int_val);
            break;
        case TK_FLOAT_LIT:
            printf("  %g", tok.float_val);
            break;
        case TK_STRING_LIT:
        case TK_IDENT:
            printf("  \"%.*s\"", tok.str_val.len, tok.str_val.str);
            break;
        case TK_CHAR_LIT:
            printf("  '%c'", (char)tok.int_val);
            break;
        default:
            break;
        }
        printf("\n");
    }

    zan_arena_free(arena);
}

static void indent(int depth) {
    for (int i = 0; i < depth; i++) printf("  ");
}

static void dump_ast_node(zan_ast_node_t *node, int depth);

static void dump_list(const char *label, zan_ast_list_t *list, int depth) {
    if (list->count == 0) return;
    indent(depth);
    printf("%s (%d):\n", label, list->count);
    for (int i = 0; i < list->count; i++) {
        dump_ast_node(list->items[i], depth + 1);
    }
}

static void dump_ast_node(zan_ast_node_t *node, int depth) {
    if (!node) return;
    indent(depth);

    switch (node->kind) {
    case AST_COMPILATION_UNIT:
        printf("CompilationUnit\n");
        dump_list("usings", &node->comp_unit.usings, depth + 1);
        if (node->comp_unit.ns) {
            indent(depth + 1);
            printf("namespace:\n");
            dump_ast_node(node->comp_unit.ns, depth + 2);
        }
        dump_list("declarations", &node->comp_unit.decls, depth + 1);
        break;

    case AST_USING_DECL:
        printf("UsingDecl%s\n", node->using_decl.is_static ? " (static)" : "");
        dump_ast_node(node->using_decl.name, depth + 1);
        break;

    case AST_NAMESPACE_DECL:
        printf("NamespaceDecl%s\n",
               node->namespace_decl.is_file_scoped ? " (file-scoped)" : "");
        dump_ast_node(node->namespace_decl.name, depth + 1);
        dump_list("members", &node->namespace_decl.members, depth + 1);
        break;

    case AST_CLASS_DECL:
    case AST_STRUCT_DECL:
    case AST_INTERFACE_DECL:
    case AST_ENUM_DECL:
        printf("%s '%.*s' (mods=0x%x)\n",
               node->kind == AST_CLASS_DECL ? "ClassDecl" :
               node->kind == AST_STRUCT_DECL ? "StructDecl" :
               node->kind == AST_INTERFACE_DECL ? "InterfaceDecl" : "EnumDecl",
               node->type_decl.name.len, node->type_decl.name.str,
               node->type_decl.modifiers);
        dump_list("type_params", &node->type_decl.type_params, depth + 1);
        dump_list("bases", &node->type_decl.bases, depth + 1);
        dump_list("members", &node->type_decl.members, depth + 1);
        break;

    case AST_METHOD_DECL:
    case AST_CONSTRUCTOR_DECL:
    case AST_DESTRUCTOR_DECL:
        printf("%s '%.*s' (mods=0x%x)\n",
               node->kind == AST_METHOD_DECL ? "MethodDecl" :
               node->kind == AST_CONSTRUCTOR_DECL ? "ConstructorDecl" : "DestructorDecl",
               node->method_decl.name.len, node->method_decl.name.str,
               node->method_decl.modifiers);
        if (node->method_decl.return_type) {
            indent(depth + 1);
            printf("return_type:\n");
            dump_ast_node(node->method_decl.return_type, depth + 2);
        }
        dump_list("params", &node->method_decl.params, depth + 1);
        if (node->method_decl.body) {
            indent(depth + 1);
            printf("body:\n");
            dump_ast_node(node->method_decl.body, depth + 2);
        }
        break;

    case AST_PROPERTY_DECL:
        printf("PropertyDecl '%.*s' (mods=0x%x)\n",
               node->field_decl.name.len, node->field_decl.name.str,
               node->field_decl.modifiers);
        if (node->field_decl.type) {
            indent(depth + 1);
            printf("type:\n");
            dump_ast_node(node->field_decl.type, depth + 2);
        }
        if (node->field_decl.initializer) {
            indent(depth + 1);
            printf("default:\n");
            dump_ast_node(node->field_decl.initializer, depth + 2);
        }
        break;

    case AST_FIELD_DECL:
        printf("FieldDecl '%.*s' (mods=0x%x)\n",
               node->field_decl.name.len, node->field_decl.name.str,
               node->field_decl.modifiers);
        if (node->field_decl.type) {
            indent(depth + 1);
            printf("type:\n");
            dump_ast_node(node->field_decl.type, depth + 2);
        }
        if (node->field_decl.initializer) {
            indent(depth + 1);
            printf("init:\n");
            dump_ast_node(node->field_decl.initializer, depth + 2);
        }
        break;

    case AST_PARAM:
        printf("Param '%.*s'\n", node->param.name.len, node->param.name.str);
        if (node->param.type) {
            dump_ast_node(node->param.type, depth + 1);
        }
        break;

    case AST_BLOCK:
        printf("Block\n");
        dump_list("stmts", &node->block.stmts, depth + 1);
        break;

    case AST_VAR_DECL:
        printf("VarDecl '%.*s'%s%s\n",
               node->var_decl.name.len, node->var_decl.name.str,
               node->var_decl.is_const ? " const" : "",
               node->var_decl.is_let ? " let" : "");
        if (node->var_decl.type) {
            indent(depth + 1);
            printf("type:\n");
            dump_ast_node(node->var_decl.type, depth + 2);
        }
        if (node->var_decl.initializer) {
            indent(depth + 1);
            printf("init:\n");
            dump_ast_node(node->var_decl.initializer, depth + 2);
        }
        break;

    case AST_RETURN_STMT:
        printf("ReturnStmt\n");
        if (node->ret.value) {
            dump_ast_node(node->ret.value, depth + 1);
        }
        break;

    case AST_IF_STMT:
        printf("IfStmt\n");
        indent(depth + 1); printf("cond:\n");
        dump_ast_node(node->if_stmt.cond, depth + 2);
        indent(depth + 1); printf("then:\n");
        dump_ast_node(node->if_stmt.then_body, depth + 2);
        if (node->if_stmt.else_body) {
            indent(depth + 1); printf("else:\n");
            dump_ast_node(node->if_stmt.else_body, depth + 2);
        }
        break;

    case AST_WHILE_STMT:
    case AST_DO_WHILE_STMT:
        printf("%s\n", node->kind == AST_WHILE_STMT ? "WhileStmt" : "DoWhileStmt");
        indent(depth + 1); printf("cond:\n");
        dump_ast_node(node->while_stmt.cond, depth + 2);
        indent(depth + 1); printf("body:\n");
        dump_ast_node(node->while_stmt.body, depth + 2);
        break;

    case AST_FOR_STMT:
        printf("ForStmt\n");
        if (node->for_stmt.init) {
            indent(depth + 1); printf("init:\n");
            dump_ast_node(node->for_stmt.init, depth + 2);
        }
        if (node->for_stmt.cond) {
            indent(depth + 1); printf("cond:\n");
            dump_ast_node(node->for_stmt.cond, depth + 2);
        }
        if (node->for_stmt.step) {
            indent(depth + 1); printf("step:\n");
            dump_ast_node(node->for_stmt.step, depth + 2);
        }
        indent(depth + 1); printf("body:\n");
        dump_ast_node(node->for_stmt.body, depth + 2);
        break;

    case AST_FOREACH_STMT:
        printf("ForeachStmt '%.*s'\n",
               node->foreach_stmt.var_name.len, node->foreach_stmt.var_name.str);
        indent(depth + 1); printf("collection:\n");
        dump_ast_node(node->foreach_stmt.collection, depth + 2);
        indent(depth + 1); printf("body:\n");
        dump_ast_node(node->foreach_stmt.body, depth + 2);
        break;

    case AST_BREAK_STMT:
        printf("BreakStmt\n");
        break;

    case AST_CONTINUE_STMT:
        printf("ContinueStmt\n");
        break;

    case AST_EXPR_STMT:
        printf("ExprStmt\n");
        dump_ast_node(node->expr_stmt.expr, depth + 1);
        break;

    case AST_BINARY:
        printf("Binary '%s'%s%s\n", zan_token_kind_name(node->binary.op),
               node->binary.checked > 0 ? " [checked]" : "",
               node->binary.checked < 0 ? " [unchecked]" : "");
        dump_ast_node(node->binary.left, depth + 1);
        dump_ast_node(node->binary.right, depth + 1);
        break;

    case AST_UNARY:
        printf("Unary '%s'\n", zan_token_kind_name(node->unary.op));
        dump_ast_node(node->unary.operand, depth + 1);
        break;

    case AST_POSTFIX_UNARY:
        printf("PostfixUnary '%s'\n", zan_token_kind_name(node->unary.op));
        dump_ast_node(node->unary.operand, depth + 1);
        break;

    case AST_ASSIGNMENT:
        printf("Assignment '%s'\n", zan_token_kind_name(node->binary.op));
        dump_ast_node(node->binary.left, depth + 1);
        dump_ast_node(node->binary.right, depth + 1);
        break;

    case AST_CALL:
        printf("Call\n");
        indent(depth + 1); printf("callee:\n");
        dump_ast_node(node->call.callee, depth + 2);
        dump_list("args", &node->call.args, depth + 1);
        break;

    case AST_MEMBER_ACCESS:
        printf("MemberAccess '%.*s'\n", node->member.name.len, node->member.name.str);
        dump_ast_node(node->member.object, depth + 1);
        break;

    case AST_INDEX:
        printf("Index\n");
        dump_ast_node(node->index.object, depth + 1);
        dump_ast_node(node->index.index, depth + 1);
        break;

    case AST_NEW_EXPR:
        printf("NewExpr\n");
        dump_ast_node(node->new_expr.type, depth + 1);
        if (node->new_expr.call_init) {
            printf("%*scall_init:\n", depth * 2 + 2, "");
            dump_ast_node(node->new_expr.call_init, depth + 2);
        }
        dump_list("args", &node->new_expr.args, depth + 1);
        dump_list("arg_inits", &node->new_expr.arg_inits, depth + 1);
        break;

    case AST_CONDITIONAL:
        printf("Conditional\n");
        dump_ast_node(node->conditional.cond, depth + 1);
        dump_ast_node(node->conditional.then_expr, depth + 1);
        dump_ast_node(node->conditional.else_expr, depth + 1);
        break;

    case AST_INT_LITERAL:
        printf("IntLiteral %lld\n", (long long)node->int_val);
        break;
    case AST_FLOAT_LITERAL:
        printf("FloatLiteral %g\n", node->float_val);
        break;
    case AST_STRING_LITERAL:
        printf("StringLiteral \"%.*s\"\n", node->str_val.len, node->str_val.str);
        break;
    case AST_CHAR_LITERAL:
        printf("CharLiteral '%c'\n", (char)node->int_val);
        break;
    case AST_BOOL_LITERAL:
        printf("BoolLiteral %s\n", node->bool_val ? "true" : "false");
        break;
    case AST_NULL_LITERAL:
        printf("NullLiteral\n");
        break;
    case AST_THIS_EXPR:
        printf("This\n");
        break;
    case AST_BASE_EXPR:
        printf("Base\n");
        break;

    case AST_IDENTIFIER:
        printf("Identifier '%.*s'\n", node->ident.name.len, node->ident.name.str);
        break;

    case AST_QUALIFIED_NAME:
        printf("QualifiedName ");
        for (int i = 0; i < node->qualified_name.parts.count; i++) {
            if (i > 0) printf(".");
            zan_ast_node_t *part = node->qualified_name.parts.items[i];
            printf("%.*s", part->ident.name.len, part->ident.name.str);
        }
        printf("\n");
        break;

    case AST_TYPE_REF:
        printf("TypeRef '%.*s'%s%s",
               node->type_ref.name.len, node->type_ref.name.str,
               node->type_ref.is_nullable ? "?" : "",
               node->type_ref.is_array ? "[]" : "");
        if (node->type_ref.type_args.count > 0) {
            printf("<");
            for (int i = 0; i < node->type_ref.type_args.count; i++) {
                if (i > 0) printf(", ");
                zan_ast_node_t *ta = node->type_ref.type_args.items[i];
                printf("%.*s", ta->type_ref.name.len, ta->type_ref.name.str);
            }
            printf(">");
        }
        printf("\n");
        break;

    case AST_LAMBDA:
        printf("Lambda\n");
        dump_list("params", &node->lambda.params, depth + 1);
        indent(depth + 1); printf("body:\n");
        dump_ast_node(node->lambda.body, depth + 2);
        break;

    case AST_STRING_INTERP:
        printf("StringInterp (%d parts)\n", node->string_interp.parts.count);
        dump_list("parts", &node->string_interp.parts, depth + 1);
        break;

    case AST_ENUM_MEMBER:
        printf("EnumMember '%.*s'\n",
               node->enum_member.name.len, node->enum_member.name.str);
        if (node->enum_member.value) {
            dump_ast_node(node->enum_member.value, depth + 1);
        }
        break;

    case AST_CHECKED_STMT:
        printf(node->checked_stmt.checked ? "CheckedBlock\n"
                                          : "UncheckedBlock\n");
        dump_ast_node(node->checked_stmt.body, depth + 1);
        break;

    default:
        printf("<%d>\n", node->kind);
        break;
    }
}

/* 内部辅助实现 */
static bool zan_dllimport_name_is_safe(const char *name, int len) {
    if (!name || len <= 0) return false;
    for (int i = 0; i < len; i++) {
        unsigned char c = (unsigned char)name[i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9')) continue;
        if (c == '.' || c == '_' || c == '+' || c == '-') continue;
        return false;
    }
    return true;
}

/* 内部辅助实现 */
static const char *zan_dllimport_lname(const char *lib, int lib_len,
                                       int *out_len) {
    if (lib_len == 3 && memcmp(lib, "crt", 3) == 0) return NULL;
    if (lib_len == 6 && memcmp(lib, "msvcrt", 6) == 0) return NULL;
    const char *name = lib;
    int n = lib_len;
    if (n > 3 && memcmp(name, "lib", 3) == 0) { name += 3; n -= 3; }
    if (n == 1 && (name[0] == 'c' || name[0] == 'm')) return NULL;
    if (!zan_dllimport_name_is_safe(name, n)) {
        /* 内部辅助逻辑 */
        fprintf(stderr,
                "error: [DllImport] library name '%.*s' contains characters "
                "that are not allowed on a linker command line "
                "(letters, digits, '.', '_', '+', '-' only)\n",
                n, name);
        return NULL;
    }
    *out_len = n;
    return name;
}

/* 内部辅助逻辑 */
static bool zan_win_system_lib(const char *lib, int lib_len) {
    static const char *const names[] = {
        "kernel32", "user32", "gdi32", "advapi32", "shell32", "shlwapi",
        "ole32", "oleaut32", "comdlg32", "comctl32", "gdiplus", "dwmapi",
        "shcore", "uxtheme", "msimg32", "winmm", "ws2_32", "opengl32",
        "psapi", "version", "setupapi", "secur32", "iphlpapi", "crypt32",
        "bcrypt", "wininet", "winhttp", "urlmon", "userenv", "uuid",
        "rpcrt4", "imm32", NULL };
    for (int i = 0; names[i]; i++)
        if ((int)strlen(names[i]) == lib_len
            && memcmp(names[i], lib, (size_t)lib_len) == 0)
            return true;
    return false;
}

/* 内部辅助实现 */
#define ZAN_MAX_DRIVERS 1024
typedef struct {
    char lib[64];      /* normalized -l basename, e.g. "sqlite3", "zan_sdl3" */
    char module[512];  /* 底层系统交互与数据协议契约 */
    char sym[64];      /* 内部辅助逻辑 */
    char root[1024];   /* 内部辅助逻辑 */
} zan_driver_entry_t;
typedef struct {
    zan_driver_entry_t entries[ZAN_MAX_DRIVERS];
    int count;
} zan_driver_registry_t;

static void zan_registry_add(zan_driver_registry_t *reg,
                             const char *lib, const char *module,
                             const char *sym, const char *root) {
    if (reg->count >= ZAN_MAX_DRIVERS) return;
    for (int i = 0; i < reg->count; i++)
        if (strcmp(reg->entries[i].lib, lib) == 0) return; /* 核心系统底层抽象与内存语义契约 */
    snprintf(reg->entries[reg->count].lib,
             sizeof(reg->entries[0].lib), "%s", lib);
    snprintf(reg->entries[reg->count].module,
             sizeof(reg->entries[0].module), "%s", module);
    snprintf(reg->entries[reg->count].sym,
             sizeof(reg->entries[0].sym), "%s", sym ? sym : "");
    snprintf(reg->entries[reg->count].root,
             sizeof(reg->entries[0].root), "%s", root ? root : "");
    reg->count++;
}

/* 核心系统底层抽象与内存语义契约 */
static void zan_read_driver_manifest(const char *manifest_path,
                                     const char *module,
                                     zan_driver_registry_t *reg,
                                     const char *root) {
    FILE *f = fopen(manifest_path, "rb");
    if (!f) return;
    char line[128];
    while (fgets(line, sizeof(line), f)) {
        char *s = line;
        while (*s == ' ' || *s == '\t') s++;
        size_t len = strlen(s);
        while (len > 0 && (s[len - 1] == '\n' || s[len - 1] == '\r' ||
                           s[len - 1] == ' ' || s[len - 1] == '\t'))
            s[--len] = '\0';
        if (s[0] == '\0' || s[0] == '#') continue;
        /* 内部辅助逻辑 */
        const char *sym = "";
        char *cond = strstr(s, " if ");
        if (cond) {
            *cond = '\0';
            sym = cond + 4;
            while (*sym == ' ' || *sym == '\t') sym++;
            size_t ll = strlen(s);
            while (ll > 0 && (s[ll - 1] == ' ' || s[ll - 1] == '\t'))
                s[--ll] = '\0';
            if (s[0] == '\0') continue;
        }
        zan_registry_add(reg, s, module, sym, root);
    }
    fclose(f);
}

typedef void (*zan_stdlib_dir_callback)(const char *dir_full,
                                        const char *rel, void *ctx);
static bool zan_file_exists(const char *path);
static bool zan_is_safe_bundle_name(const char *name);

/* 内部辅助逻辑 */
static void zan_walk_stdlib_dirs(const char *dir_full, const char *rel,
                                 int depth, zan_stdlib_dir_callback visit,
                                 void *ctx) {
    if (depth > 8) return;
    visit(dir_full, rel, ctx);
#ifdef _WIN32
    char glob[1200];
    snprintf(glob, sizeof(glob), "%s\\*", dir_full);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(glob, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
            const char *nm = fd.cFileName;
            if (nm[0] == '.') continue;
            if (strcmp(nm, "drivers") == 0 || strcmp(nm, "native") == 0) continue;
            char sub[1200];
            snprintf(sub, sizeof(sub), "%s\\%s", dir_full, nm);
            char subrel[512];
            if (rel[0]) snprintf(subrel, sizeof(subrel), "%s/%s", rel, nm);
            else snprintf(subrel, sizeof(subrel), "%s", nm);
            zan_walk_stdlib_dirs(sub, subrel, depth + 1, visit, ctx);
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
#else
    DIR *d = opendir(dir_full);
    if (d) {
        struct dirent *ent;
        while ((ent = readdir(d)) != NULL) {
            const char *nm = ent->d_name;
            if (nm[0] == '.') continue;
            if (strcmp(nm, "drivers") == 0 || strcmp(nm, "native") == 0) continue;
            char sub[1200];
            snprintf(sub, sizeof(sub), "%s/%s", dir_full, nm);
            struct stat st;
            if (stat(sub, &st) != 0 || !S_ISDIR(st.st_mode)) continue;
            char subrel[512];
            if (rel[0]) snprintf(subrel, sizeof(subrel), "%s/%s", rel, nm);
            else snprintf(subrel, sizeof(subrel), "%s", nm);
            zan_walk_stdlib_dirs(sub, subrel, depth + 1, visit, ctx);
        }
        closedir(d);
    }
#endif
}

/* 内部辅助逻辑 */
typedef struct {
    zan_driver_registry_t *reg;
    const char *root;
} zan_driver_root_scan_t;

static void zan_scan_driver_module(const char *dir_full, const char *rel,
                                   void *ctx) {
    if (!rel[0]) return;
    zan_driver_root_scan_t *scan = (zan_driver_root_scan_t *)ctx;
    char manifest[1200];
    snprintf(manifest, sizeof(manifest), "%s/drivers/driver.manifest", dir_full);
    zan_read_driver_manifest(manifest, rel, scan->reg, scan->root);
}

typedef char zan_package_source_root_t[1024];

/* 模块核心语义抽象与接口调用契约 */
static zan_package_source_root_t *zan_collect_package_source_roots(int *count) {
    int cap = 32;
    zan_package_source_root_t *roots = NULL;
    for (;;) {
        zan_package_source_root_t *grown = realloc(roots, (size_t)cap * sizeof(*roots));
        if (!grown) {
            free(roots);
            fprintf(stderr, "error: out of memory discovering package source roots\n");
            exit(1);
        }
        roots = grown;
        *count = zan_pkg_all_source_roots(package_project_root, roots, cap);
        if (*count < cap) return roots;
        if (cap > INT32_MAX / 2 || (size_t)cap > SIZE_MAX / sizeof(*roots) / 2) {
            free(roots);
            fprintf(stderr, "error: too many package source roots\n");
            exit(1);
        }
        cap *= 2;
    }
}

static void zan_discover_drivers(const char *stdlib_root,
                                 zan_driver_registry_t *reg) {
    reg->count = 0;
    zan_driver_root_scan_t scan;
    if (stdlib_root && stdlib_root[0]) {
        scan.reg = reg;
        scan.root = stdlib_root;
        zan_walk_stdlib_dirs(stdlib_root, "", 0, zan_scan_driver_module, &scan);
    }
    /* 编译期中间表示与代码生成内部规范 */
    int pkg_n;
    zan_package_source_root_t *pkg_roots = zan_collect_package_source_roots(&pkg_n);
    for (int i = 0; i < pkg_n; i++) {
        scan.reg = reg;
        scan.root = pkg_roots[i];
        zan_walk_stdlib_dirs(pkg_roots[i], "", 0, zan_scan_driver_module,
                             &scan);
    }
    free(pkg_roots);
}

/* 模块核心语义抽象与接口调用契约 */
static int zan_driver_find(const zan_driver_registry_t *reg,
                           const char *lname, int len) {
    for (int i = 0; i < reg->count; i++)
        if ((int)strlen(reg->entries[i].lib) == len &&
            memcmp(reg->entries[i].lib, lname, (size_t)len) == 0) return i;
    return -1;
}

typedef struct {
    const char *target_subdir;
    const char *library_name;
    char result[1200];
} zan_static_library_search_t;

/* 内部辅助实现 */
static void zan_find_static_library_dir(const char *dir_full, const char *rel,
                                        void *ctx) {
    (void)rel;
    zan_static_library_search_t *search = (zan_static_library_search_t *)ctx;
    if (search->result[0]) return;
    char archive[1400];
    int n = snprintf(archive, sizeof(archive),
                     "%s/drivers/%s/static/lib%s.a", dir_full,
                     search->target_subdir, search->library_name);
    if (n < 0 || (size_t)n >= sizeof(archive)) return;
    if (!zan_file_exists(archive)) return;
    n = snprintf(search->result, sizeof(search->result),
                 "%s/drivers/%s/static", dir_full, search->target_subdir);
    if (n < 0 || (size_t)n >= sizeof(search->result))
        search->result[0] = '\0';
}

static bool zan_find_static_library(const char *stdlib_root,
                                    const char *target_subdir,
                                    const char *library_name,
                                    char *out, size_t outsz) {
    if (!stdlib_root || !target_subdir || !library_name ||
        !zan_is_safe_bundle_name(library_name))
        return false;
    zan_static_library_search_t search = {
        .target_subdir = target_subdir,
        .library_name = library_name,
        .result = {0}
    };
    zan_walk_stdlib_dirs(stdlib_root, "", 0,
                         zan_find_static_library_dir, &search);
    if (!search.result[0]) {
        /* 内部辅助逻辑 */
        int pkg_n;
        zan_package_source_root_t *pkg_roots = zan_collect_package_source_roots(&pkg_n);
        for (int i = 0; i < pkg_n && !search.result[0]; i++)
            zan_walk_stdlib_dirs(pkg_roots[i], "", 0,
                                 zan_find_static_library_dir, &search);
        free(pkg_roots);
    }
    if (!search.result[0] || strlen(search.result) >= outsz) return false;
    snprintf(out, outsz, "%s", search.result);
    return true;
}

/* 内部辅助逻辑 */
static bool zan_resolve_gui_resource_dir(const char *stdlib_root,
                                        const char *rel, char *out,
                                        size_t cap) {
    if (stdlib_root && stdlib_root[0]) {
        snprintf(out, cap, "%s/%s", stdlib_root, rel);
        if (zan_file_exists(out)) return true;
    }
    int pkg_n;
    zan_package_source_root_t *pkg_roots = zan_collect_package_source_roots(&pkg_n);
    for (int i = 0; i < pkg_n; i++) {
        snprintf(out, cap, "%s/%s", pkg_roots[i], rel);
        if (zan_file_exists(out)) {
            free(pkg_roots);
            return true;
        }
    }
    free(pkg_roots);
    out[0] = '\0';
    return false;
}

/* 底层系统交互与数据协议契约 */
static int zan_copy_file_ex(const char *src, const char *dst, char *err_buf, size_t err_cap) {
    if (err_buf && err_cap > 0) err_buf[0] = '\0';
    /* 内部辅助逻辑 */
#ifdef _WIN32
    if (_stricmp(src, dst) == 0) return 0;
    { WIN32_FILE_ATTRIBUTE_DATA sa, da;
      if (GetFileAttributesExA(src, GetFileExInfoStandard, &sa) &&
          GetFileAttributesExA(dst, GetFileExInfoStandard, &da)) {
          HANDLE hs = CreateFileA(src, 0, FILE_SHARE_READ | FILE_SHARE_WRITE,
              NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
          HANDLE hd = CreateFileA(dst, 0, FILE_SHARE_READ | FILE_SHARE_WRITE,
              NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
          if (hs != INVALID_HANDLE_VALUE && hd != INVALID_HANDLE_VALUE) {
              BY_HANDLE_FILE_INFORMATION is, id;
              if (GetFileInformationByHandle(hs, &is) &&
                  GetFileInformationByHandle(hd, &id) &&
                  is.dwVolumeSerialNumber == id.dwVolumeSerialNumber &&
                  is.nFileIndexHigh == id.nFileIndexHigh &&
                  is.nFileIndexLow == id.nFileIndexLow) {
                  CloseHandle(hs); CloseHandle(hd);
                  return 0;
              }
          }
          if (hs != INVALID_HANDLE_VALUE) CloseHandle(hs);
          if (hd != INVALID_HANDLE_VALUE) CloseHandle(hd);
      } }
#else
    if (strcmp(src, dst) == 0) return 0;
    { struct stat ss, ds;
      if (stat(src, &ss) == 0 && stat(dst, &ds) == 0 &&
          ss.st_dev == ds.st_dev && ss.st_ino == ds.st_ino)
          return 0; }
#endif
    FILE *in = fopen(src, "rb");
    if (!in) {
        if (err_buf && err_cap > 0) {
#ifdef _WIN32
            DWORD err = GetLastError();
            snprintf(err_buf, err_cap, "cannot open source '%s' (errno %d, winerr %lu)",
                     src, errno, (unsigned long)err);
#else
            snprintf(err_buf, err_cap, "cannot open source '%s' (errno %d: %s)",
                     src, errno, strerror(errno));
#endif
        }
        return -1;
    }
    FILE *out = fopen(dst, "wb");
    if (!out) {
        if (err_buf && err_cap > 0) {
#ifdef _WIN32
            DWORD err = GetLastError();
            snprintf(err_buf, err_cap, "cannot open destination '%s' for writing (errno %d, winerr %lu)",
                     dst, errno, (unsigned long)err);
#else
            snprintf(err_buf, err_cap, "cannot open destination '%s' for writing (errno %d: %s)",
                     dst, errno, strerror(errno));
#endif
        }
        fclose(in);
        return -1;
    }
    char buf[65536]; size_t n; int rc = 0;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) {
            rc = -1;
            if (err_buf && err_cap > 0) {
#ifdef _WIN32
                DWORD err = GetLastError();
                snprintf(err_buf, err_cap, "failed writing to destination '%s' (errno %d, winerr %lu)",
                         dst, errno, (unsigned long)err);
#else
                snprintf(err_buf, err_cap, "failed writing to destination '%s' (errno %d: %s)",
                         dst, errno, strerror(errno));
#endif
            }
            break;
        }
    }
    fclose(in);
    if (fclose(out) != 0) {
        rc = -1;
        if (err_buf && err_cap > 0 && err_buf[0] == '\0') {
#ifdef _WIN32
            DWORD err = GetLastError();
            snprintf(err_buf, err_cap, "failed flushing/closing destination '%s' (errno %d, winerr %lu)",
                     dst, errno, (unsigned long)err);
#else
            snprintf(err_buf, err_cap, "failed flushing/closing destination '%s' (errno %d: %s)",
                     dst, errno, strerror(errno));
#endif
        }
    }
    return rc;
}

static int zan_copy_file(const char *src, const char *dst) {
    return zan_copy_file_ex(src, dst, NULL, 0);
}

/* 内部辅助逻辑 */
static bool zan_is_safe_bundle_name(const char *name) {
    if (!name || !name[0]) return false;
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) return false;
    /* 内部辅助逻辑 */
    for (const char *p = name; *p; p++) {
        char c = *p;
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') || c == '.' || c == '_' ||
                  c == '-' || c == '+';
        if (!ok) return false;
    }
    return true;
}

/* 内部辅助逻辑 */
static int zan_read_static_libs(const char *path, char out[][128], int max,
                                zan_os_t target_os) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    int count = 0;
    char line[128];
    while (count < max && fgets(line, sizeof(line), f)) {
        size_t n = strlen(line);
        while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r' ||
                         line[n - 1] == ' ' || line[n - 1] == '\t'))
            line[--n] = '\0';
        char *comment = strchr(line, '#');
        if (comment) {
            *comment = '\0';
            n = strlen(line);
            while (n > 0 && (line[n - 1] == ' ' || line[n - 1] == '\t'))
                line[--n] = '\0';
        }
        char *first = line;
        while (*first == ' ' || *first == '\t') first++;
        if (first != line) {
            memmove(line, first, strlen(first) + 1);
            n = strlen(line);
        }
        if (n == 0) continue;
        const char *framework_tag = "@framework/";
        size_t framework_tag_len = strlen(framework_tag);
        if (strncmp(line, framework_tag, framework_tag_len) == 0) {
            if (target_os != ZAN_OS_MACOS) continue;
            const char *framework = line + framework_tag_len;
            if (framework[0] == '-' || !zan_is_safe_bundle_name(framework)) {
                fprintf(stderr, "warning: ignoring unsafe framework entry '%s' in %s "
                        "(expected @framework/<bare framework name>)\n", line, path);
                continue;
            }
            if (max - count < 2) {
                fclose(f);
                link_cap_exceeded("static driver link arguments", max);
            }
            snprintf(out[count++], sizeof(out[0]), "%s", "-framework");
            snprintf(out[count++], sizeof(out[0]), "%s", framework);
            continue;
        }
        char *name = line;
        if (n >= 3 && line[0] == '-' && line[1] == 'l') {
            name = line + 2;
        }
        if (!zan_is_safe_bundle_name(name)) {
            fprintf(stderr, "warning: ignoring unsafe entry '%s' in %s "
                            "(expected a bare library basename)\n", line, path);
            continue;
        }
        snprintf(out[count], sizeof(out[0]), "-l%s", name);
        count++;
    }
    fclose(f);
    return count;
}

/* 内部辅助逻辑 */
static const char *zan_driver_subdir(const zan_target_t *t) {
    if (t->os == ZAN_OS_LINUX)
        return (t->arch == ZAN_ARCH_AARCH64) ? "linux-arm64"
             : (t->arch == ZAN_ARCH_RISCV64) ? "linux-riscv64" : "linux-x64";
    if (t->os == ZAN_OS_MACOS)
        return (t->arch == ZAN_ARCH_AARCH64) ? "macos-arm64" : "macos-x64";
    if (t->os == ZAN_OS_IOS)
        return "ios-arm64";
    if (t->os == ZAN_OS_ANDROID)
        return (t->arch == ZAN_ARCH_AARCH64) ? "android-arm64" : "android-x64";
    if (t->os == ZAN_OS_OHOS)
        return (t->arch == ZAN_ARCH_AARCH64) ? "ohos-arm64" : "ohos-x64";
    return (t->arch == ZAN_ARCH_AARCH64) ? "win-arm64" : "win-x64";
}

/* 内部辅助实现 */
typedef struct zan_driver_prefix_snapshot_entry {
    const char *prefix;
    bool defined;
    struct zan_driver_prefix_snapshot_entry *next;
} zan_driver_prefix_snapshot_entry_t;
typedef struct {
    zan_driver_prefix_snapshot_entry_t *head;
    bool captured;
} zan_driver_prefix_snapshot_t;

static bool zan_driver_prefix_live(zan_irgen_t *g,
                                   const zan_driver_prefix_snapshot_t *snapshot,
                                   const char *prefix) {
    for (const zan_driver_prefix_snapshot_entry_t *p = snapshot->head; p;
         p = p->next)
        if (strcmp(p->prefix, prefix) == 0) return p->defined;
    /* 内部辅助逻辑 */
    return !snapshot->captured && zan_irgen_defines_prefix(g, prefix);
}

static void zan_driver_prefix_record(zan_irgen_t *g, zan_arena_t *arena,
                                      zan_driver_prefix_snapshot_t *snapshot,
                                      const char *prefix) {
    if (!prefix[0]) return;
    for (zan_driver_prefix_snapshot_entry_t *p = snapshot->head; p; p = p->next)
        if (strcmp(p->prefix, prefix) == 0) return;
    zan_driver_prefix_snapshot_entry_t *p = zan_arena_alloc(arena, sizeof(*p));
    p->prefix = zan_arena_strdup(arena, prefix, strlen(prefix));
    p->defined = zan_irgen_defines_prefix(g, prefix);
    p->next = snapshot->head;
    snapshot->head = p;
}

/* 内部辅助逻辑 */
static char *zan_driver_bundle_entry(char *line, const char **prefix) {
    while (*line == ' ' || *line == '\t') line++;
    size_t len = strlen(line);
    while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r' ||
                       line[len - 1] == ' ' || line[len - 1] == '\t'))
        line[--len] = '\0';
    *prefix = NULL;
    if (!line[0] || line[0] == '#') return NULL;
    char *cond = strstr(line, " if ");
    if (cond) {
        *cond = '\0';
        const char *pfx = cond + 4;
        while (*pfx == ' ' || *pfx == '\t') pfx++;
        *prefix = pfx;
        len = strlen(line);
        while (len > 0 && (line[len - 1] == ' ' || line[len - 1] == '\t'))
            line[--len] = '\0';
    }
    return line[0] ? line : NULL;
}

static void zan_driver_capture_conditions(zan_irgen_t *g, zan_arena_t *arena,
                                          const zan_driver_registry_t *reg,
                                          const zan_target_t *target,
                                          const char *override_dir,
                                          zan_driver_prefix_snapshot_t *snapshot) {
    if (snapshot->captured) return;
    static const char *resource_prefixes[] = {
        "Gui_", "IconSvgData_", "Pinyin_", "Skin_", "Chart_"
    };
    for (size_t p = 0; p < sizeof(resource_prefixes) / sizeof(resource_prefixes[0]); p++)
        zan_driver_prefix_record(g, arena, snapshot, resource_prefixes[p]);
    for (int d = 0; d < reg->count; d++) {
        const zan_driver_entry_t *driver = &reg->entries[d];
        zan_driver_prefix_record(g, arena, snapshot, driver->sym);
        /* 模块核心语义抽象与接口调用契约 */
        for (int pass = 0; pass < (override_dir ? 2 : 1); pass++) {
            char dir[1200], manifest[1400];
            if (pass == 1)
                snprintf(dir, sizeof(dir), "%s", override_dir);
            else
                snprintf(dir, sizeof(dir), "%s/%s/drivers/%s", driver->root,
                         driver->module, zan_driver_subdir(target));
            snprintf(manifest, sizeof(manifest), "%s/%s.bundle", dir, driver->lib);
            FILE *f = fopen(manifest, "rb");
            if (!f) continue;
            char line[128];
            while (fgets(line, sizeof(line), f)) {
                const char *prefix;
                if (zan_driver_bundle_entry(line, &prefix) && prefix)
                    zan_driver_prefix_record(g, arena, snapshot, prefix);
            }
            fclose(f);
        }
    }
    snapshot->captured = true;
}

typedef struct {
    const zan_driver_registry_t *reg;
    zan_irgen_t *irgen;
    const zan_driver_prefix_snapshot_t *prefixes;
    const char *subdir;
    const char *outdir;
    bool quiet;
    bool visiting[ZAN_MAX_DRIVERS];
} zan_driver_bundle_context_t;

/* 内部辅助逻辑 */
static int zan_bundle_dependency(zan_driver_bundle_context_t *ctx,
                                 const char *dep, const char *from_manifest,
                                 int *copy_failed_count) {
    int idx = zan_driver_find(ctx->reg, dep, (int)strlen(dep));
    if (idx < 0 || !ctx->reg->entries[idx].root[0]) {
        fprintf(stderr, "warning: @driver/%s listed in %s has no registered owner\n",
                dep, from_manifest);
        return 0;
    }
    if (ctx->visiting[idx]) {
        fprintf(stderr, "warning: ignoring cyclic @driver/%s in %s\n",
                dep, from_manifest);
        return 0;
    }
    const zan_driver_entry_t *owner = &ctx->reg->entries[idx];
    char dir[1200], manifest[1400];
    snprintf(dir, sizeof(dir), "%s/%s/drivers/%s", owner->root,
             owner->module, ctx->subdir);
    snprintf(manifest, sizeof(manifest), "%s/%s.bundle", dir, dep);
    FILE *f = fopen(manifest, "rb");
    if (!f) {
        fprintf(stderr, "warning: @driver/%s listed in %s has no bundle under "
                "its owning module's drivers/%s\n", dep, from_manifest, ctx->subdir);
        return 0;
    }
    ctx->visiting[idx] = true;
    int copied = 0, entries = 0;
    char line[128];
    while (fgets(line, sizeof(line), f)) {
        const char *prefix;
        char *entry = zan_driver_bundle_entry(line, &prefix);
        if (!entry || (prefix &&
            !zan_driver_prefix_live(ctx->irgen, ctx->prefixes, prefix))) continue;
        if (++entries > 64) {
            fclose(f);
            link_cap_exceeded("files in a driver bundle manifest", 64);
        }
        if (strncmp(entry, "@driver/", 8) == 0) {
            const char *next = entry + 8;
            if (strlen(next) < 64 && zan_is_safe_bundle_name(next))
                copied += zan_bundle_dependency(ctx, next, manifest, copy_failed_count);
            else
                fprintf(stderr, "warning: ignoring unsafe @driver entry '%s' in %s\n",
                        entry, manifest);
            continue;
        }
        if (!zan_is_safe_bundle_name(entry)) {
            fprintf(stderr, "warning: ignoring unsafe entry '%s' in %s "
                    "(must be a bare filename)\n", entry, manifest);
            continue;
        }
        char src[1400], dst[1300], errbuf[256];
        snprintf(src, sizeof(src), "%s/%s", dir, entry);
        snprintf(dst, sizeof(dst), "%s/%s", ctx->outdir, entry);
        if (zan_copy_file_ex(src, dst, errbuf, sizeof(errbuf)) == 0) {
            if (!ctx->quiet)
                printf("  bundled driver '%s' ? %s (via @driver/%s)\n", dep, entry, dep);
            copied++;
        } else {
            fprintf(stderr, "warning: failed to copy bundled driver file "
                    "'%s' -> '%s': %s\n", src, dst, errbuf);
            (*copy_failed_count)++;
        }
    }
    fclose(f);
    ctx->visiting[idx] = false;
    return copied;
}

/* 内部辅助实现 */
static void zan_exe_dir(char *out, size_t outsz) {
    out[0] = '\0';
#ifdef _WIN32
    GetModuleFileNameA(NULL, out, (DWORD)outsz);
    { char *s = strrchr(out, '\\'); if (s) *s = '\0'; }
#elif defined(__APPLE__)
    { uint32_t sz = (uint32_t)outsz;
      if (_NSGetExecutablePath(out, &sz) != 0) out[0] = '\0';
      char *s = strrchr(out, '/'); if (s) *s = '\0'; }
#else
    { ssize_t n = readlink("/proc/self/exe", out, outsz - 1);
      if (n > 0) { out[n] = '\0'; char *s = strrchr(out, '/'); if (s) *s = '\0'; } }
#endif
}

/* 核心系统底层抽象与内存语义契约 */
static bool zan_file_exists(const char *path) {
#ifdef _WIN32
    return zan_utf8_get_file_attributes(path) != INVALID_FILE_ATTRIBUTES;
#else
    return access(path, F_OK) == 0;
#endif
}

/* 模块核心语义抽象与接口调用契约 */
static bool zan_read_first_bundle_name(const char *path,
                                       char *out, size_t outsz) {
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    char line[128];
    while (fgets(line, sizeof(line), f)) {
        size_t n = strlen(line);
        while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r' ||
                         line[n - 1] == ' ' || line[n - 1] == '\t'))
            line[--n] = '\0';
        char *first = line;
        while (*first == ' ' || *first == '\t') first++;
        if (first != line) {
            memmove(line, first, strlen(first) + 1);
            n = strlen(line);
        }
        if (n == 0 || !zan_is_safe_bundle_name(line)) continue;
        if (n >= outsz) continue;
        snprintf(out, outsz, "%s", line);
        fclose(f);
        return true;
    }
    fclose(f);
    return false;
}

/* 核心系统底层抽象与内存语义契约 */
static bool zan_find_macos_driver_dylib(const char *dir,
                                        const char *name, int len,
                                        char *out, size_t outsz) {
    char direct[1200];
    snprintf(direct, sizeof(direct), "%s/lib%.*s.dylib", dir, len, name);
    if (zan_file_exists(direct)) {
        snprintf(out, outsz, "%s", direct);
        return true;
    }
    char manifest[1200], bundled[128];
    snprintf(manifest, sizeof(manifest), "%s/%.*s.bundle", dir, len, name);
    if (!zan_read_first_bundle_name(manifest, bundled, sizeof(bundled)))
        return false;
    snprintf(out, outsz, "%s/%s", dir, bundled);
    return zan_file_exists(out);
}

/* 底层系统交互与数据协议契约 */
static const char *zan_path_basename(const char *p) {
    const char *b = p;
    for (const char *q = p; *q; q++)
        if (*q == '/' || *q == '\\') b = q + 1;
    return b;
}

/* 内部辅助逻辑 */
static int zan_path_is_under(const char *path, const char *root) {
    if (!path || !root || !root[0]) return 0;
    size_t rl = strlen(root);
    while (rl > 0 && (root[rl - 1] == '/' || root[rl - 1] == '\\')) rl--;
#ifdef _WIN32
    if (_strnicmp(path, root, (size_t)rl) != 0) return 0;
#else
    if (strncmp(path, root, rl) != 0) return 0;
#endif
    char nc = path[rl];
    if (nc == '\0') return 1;               /* 核心系统底层抽象与内存语义契约 */
    return nc == '/' || nc == '\\';
}

/* 内部辅助实现 */
static int wasm_obj_refs_any(const char *obj, const char *const *prefixes) {
    char nmout[1300];
    snprintf(nmout, sizeof(nmout), "%s.nm", obj);
    char nmcmd[1600];
    snprintf(nmcmd, sizeof(nmcmd), "llvm-nm -u \"%s\" > \"%s\" 2>&1", obj, nmout);
    if (system(nmcmd) != 0) { remove(nmout); return 0; }
    int found = 0;
    FILE *f = fopen(nmout, "rb");
    if (f) {
        char line[512];
        while (fgets(line, sizeof(line), f)) {
            for (int pi = 0; prefixes[pi]; pi++) {
                if (strstr(line, prefixes[pi])) { found = 1; break; }
            }
            if (found) break;
        }
        fclose(f);
    }
    remove(nmout);
    return found;
}

static int wasm_obj_vec_refs_any(const generated_object_vec_t *objects,
                                 const char *const *prefixes) {
    for (int i = 0; i < objects->count; i++) {
        if (wasm_obj_refs_any(objects->paths[i], prefixes)) return 1;
    }
    return 0;
}

/* 底层系统交互与数据协议契约 */
static void zan_runtime_fallback(zan_irgen_t *g, LLVMValueRef value,
                                  bool use_comdat) {
    if (!value || LLVMIsDeclaration(value)) return;
    LLVMSetLinkage(value, LLVMLinkOnceAnyLinkage);
    if (use_comdat) {
        LLVMComdatRef group = LLVMGetOrInsertComdat(g->mod,
                                                     LLVMGetValueName(value));
        LLVMSetComdatSelectionKind(group, LLVMAnyComdatSelectionKind);
        LLVMSetComdat(value, group);
    }
}

static void zan_prepare_static_runtime(zan_irgen_t *g,
                                        const zan_target_t *target) {
    if (g->emit_lib && g->emit_shared) return;
    /* 模块核心语义抽象与接口调用契约 */
    bool use_comdat = target->os != ZAN_OS_MACOS && target->os != ZAN_OS_IOS;

    /* 内部辅助逻辑 */
    LLVMValueRef weak_state[] = {
        g->weak_buckets, g->weak_lock, g->weak_count
    };
    for (size_t i = 0; i < sizeof(weak_state) / sizeof(weak_state[0]); i++)
        zan_runtime_fallback(g, weak_state[i], use_comdat);

    /* 内部辅助实现 */
    static const char *const co_state[] = {
        "__zan_co_head", "__zan_co_tail", "__zan_co_nodes",
        "__zan_co_slice_start", "__zan_co_poll_tick", "__zan_co_quantum_us"
    };
    if (!g->external_async_executor) {
        for (size_t i = 0; i < sizeof(co_state) / sizeof(co_state[0]); i++)
            zan_runtime_fallback(g, LLVMGetNamedGlobal(g->mod, co_state[i]),
                                 use_comdat);
    }

    /* 模块核心语义抽象与接口调用契约 */
    LLVMValueRef io_fallbacks[] = {
        g->rt_io_pump_timeout, g->rt_io_has_pending,
        LLVMGetNamedFunction(g->mod, "zan_io_pump")
    };
    for (size_t i = 0; i < sizeof(io_fallbacks) / sizeof(io_fallbacks[0]); i++)
        zan_runtime_fallback(g, io_fallbacks[i], use_comdat);

    if (!g->emit_lib) return;
    LLVMValueRef co_fallbacks[] = {
        g->rt_co_ready, g->rt_co_poll, g->rt_co_frame_free,
        g->rt_co_sched_init, g->rt_co_sched_run, g->rt_co_sched_run_until
    };
    for (size_t i = 0; i < sizeof(co_fallbacks) / sizeof(co_fallbacks[0]); i++)
        zan_runtime_fallback(g, co_fallbacks[i], use_comdat);

    /* 内部辅助实现 */
    LLVMValueRef helpers[] = {
        g->rt_println, g->rt_print_int, g->rt_print_uint, g->rt_print_double,
        g->rt_retain, g->rt_release, g->rt_release_dyn, g->rt_alloc,
        g->rt_str_retain, g->rt_str_release, g->rt_str_alloc,
        g->rt_arr_retain, g->rt_arr_release,
        g->rt_weak_store, g->rt_weak_nil_all, g->rt_weak_load_retain,
        g->rt_weak_destroy_begin
    };
    for (size_t i = 0; i < sizeof(helpers) / sizeof(helpers[0]); i++) {
        if (!helpers[i] || LLVMIsDeclaration(helpers[i])) continue;
        LLVMSetLinkage(helpers[i], LLVMInternalLinkage);
    }
}

/* 内部辅助逻辑 */
static int zan_run_archiver(const char *tool, const char *format,
                            const char *arc, const char *obj) {
    const char *argv[] = {tool, format, "rcsD", arc, obj, NULL};
#ifdef _WIN32
    wchar_t *wide_tool = zan_utf8_to_wide_alloc(tool);
    if (!wide_tool) return 1;
    DWORD needed = SearchPathW(NULL, wide_tool, L".exe", 0, NULL, NULL);
    if (!needed) { free(wide_tool); return -1; }
    wchar_t *wide_path = malloc((size_t)needed * sizeof(*wide_path));
    if (!wide_path) { free(wide_tool); return 1; }
    DWORD written = SearchPathW(NULL, wide_tool, L".exe", needed, wide_path, NULL);
    free(wide_tool);
    if (!written || written >= needed) { free(wide_path); return -1; }
    char *path = zan_wide_to_utf8_alloc(wide_path);
    free(wide_path);
    if (!path) return 1;
    argv[0] = path;
    /* 编译期中间表示与代码生成内部规范 */
    char *quoted[6] = {0};
    for (int i = 0; i < 5; i++) {
        size_t len = strlen(argv[i]);
        quoted[i] = malloc(len + 3);
        if (!quoted[i]) {
            for (int j = 0; j < i; j++) free(quoted[j]);
            free(path);
            return 1;
        }
        snprintf(quoted[i], len + 3, "\"%s\"", argv[i]);
    }
    intptr_t rc = zan_utf8_spawnv(_P_WAIT, path,
                                  (const char *const *)quoted);
    if (rc < 0)
        fprintf(stderr, "error: cannot start archiver '%s': %s\n",
                path, strerror(errno));
    for (int i = 0; i < 5; i++) free(quoted[i]);
    free(path);
    return rc == 0 ? 0 : 1;
#else
    extern char **environ;
    pid_t child;
    int rc = posix_spawnp(&child, tool, NULL, NULL,
                          (char *const *)argv, environ);
    if (rc == ENOENT || rc == ENOTDIR) return -1;
    if (rc != 0) {
        fprintf(stderr, "error: cannot start archiver '%s': %s\n",
                tool, strerror(rc));
        return 1;
    }
    int status;
    while (waitpid(child, &status, 0) < 0) {
        if (errno == EINTR) continue;
        fprintf(stderr, "error: cannot wait for archiver '%s': %s\n",
                tool, strerror(errno));
        return 1;
    }
    return WIFEXITED(status) && WEXITSTATUS(status) == 0 ? 0 : 1;
#endif
}

/* 内部辅助逻辑 */
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
    const char *tools[] = {
        bundled, "llvm-ar", versioned,
#ifdef __APPLE__
        "/opt/homebrew/opt/llvm/bin/llvm-ar",
        "/usr/local/opt/llvm/bin/llvm-ar",
#endif
        NULL
    };
    const char *format = (target_os == ZAN_OS_MACOS || target_os == ZAN_OS_IOS)
                             ? "--format=darwin" : "--format=gnu";
    size_t tmp_size = strlen(obj) + sizeof(".a");
    char *tmp = malloc(tmp_size);
    if (!tmp) return -1;
    snprintf(tmp, tmp_size, "%s.a", obj);
    if (remove(tmp) != 0 && errno != ENOENT) {
        fprintf(stderr, "error: cannot replace archive temporary '%s': %s\n",
                tmp, strerror(errno));
        free(tmp);
        return -1;
    }
    int rc = -1;
    for (int i = 0; tools[i]; i++) {
        if (!tools[i][0]) continue;
        rc = zan_run_archiver(tools[i], format, tmp, obj);
        if (rc >= 0) break;
    }
    if (rc < 0)
        fprintf(stderr,
                "error: static library emission requires llvm-ar; install LLVM "
                "on PATH or place llvm-ar beside zanc\n");
    if (rc == 0) {
#ifdef _WIN32
        wchar_t *wide_tmp = zan_utf8_to_wide_alloc(tmp);
        wchar_t *wide_arc = zan_utf8_to_wide_alloc(arc);
        rc = wide_tmp && wide_arc &&
             MoveFileExW(wide_tmp, wide_arc, MOVEFILE_REPLACE_EXISTING) ? 0 : -1;
        free(wide_tmp);
        free(wide_arc);
#else
        rc = rename(tmp, arc);
#endif
    }
    remove(tmp);
    free(tmp);
    return rc == 0 ? 0 : -1;
}

static void print_usage(void) {
    fprintf(stderr, "Zan Compiler v%s\n", ZAN_VERSION);
    fprintf(stderr, "Usage: zanc <source.zan> [options]\n");
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  -o <output>     Output file (library: .dll/.so/.dylib shared,\n");
    fprintf(stderr, "                   .a/.lib static; executable: any other suffix)\n");
    fprintf(stderr, "  --emit-lib      Force library output (no CRT/entry point)\n");
    fprintf(stderr, "  --dump-tokens   Dump lexer tokens\n");
    fprintf(stderr, "  --dump-ast      Dump parse tree\n");
    fprintf(stderr, "  --emit-ir       Emit LLVM IR to stdout\n");
    fprintf(stderr, "  --quiet, -q     Suppress progress lines on stdout (driver bundling,\n");
    fprintf(stderr, "                   APK packaging, the \"Compiled N files\" summary)\n");
    fprintf(stderr, "  --check-leaks   Report unreleased objects at program exit (default with -g)\n");
    fprintf(stderr, "  --arc-guard     Quarantine freed objects and trap stale retain/release (default with -g)\n");
    fprintf(stderr, "  --no-check-leaks, --no-arc-guard  Turn those off in a debug build\n");
    fprintf(stderr, "                   (--no-arc-guard also turns off the --publish over-release net)\n");
    fprintf(stderr, "  --no-runtime-checks  Disable runtime guards (e.g. division by zero)\n");
    fprintf(stderr, "  --strict-runtime  Guard failures exit(70) without ZAN_RT_HARD=1\n");
    fprintf(stderr, "  --deny-warnings   Treat compiler warnings as fatal errors\n");
    fprintf(stderr, "  --publish        Build optimized release binary (strip debug, optimize)\n");
    fprintf(stderr, "  --fast-alloc     Front-end malloc with the per-thread small-object\n");
    fprintf(stderr, "                   allocator (server workloads; native targets only)\n");
    fprintf(stderr, "  --link-mode <m>  Native driver linking on publish: shared (copy driver\n");
    fprintf(stderr, "                   libs next to the exe, default) or static (link into the exe)\n");
    fprintf(stderr, "  --driver-dir <d> Override the bundled native driver directory\n");
    fprintf(stderr, "  --stdlib-path <dir>  Path to stdlib directory\n");
    fprintf(stderr, "  --auto-stdlib    Automatically find stdlib and installed package namespaces (default)\n");
    fprintf(stderr, "  --no-stdlib      Disable automatic stdlib and package discovery\n");
    fprintf(stderr, "  --no-packages    Disable package discovery only (used for the nested\n");
    fprintf(stderr, "                   code-generator build, whose closure is stdlib-only)\n");
    fprintf(stderr, "  --package-api <url>  Configure marketplace API (HTTPS; localhost HTTP allowed)\n");
    fprintf(stderr, "  --package-list-missing  Scan inputs and print missing namespace diagnostics\n");
    fprintf(stderr, "  --package-install <dir> --package-name <name>  Install a local package directory\n");
    fprintf(stderr, "  --package-scope <project|global>  Select install scope (default project)\n");
    fprintf(stderr, "  --package-project <dir>  Project root for package discovery and\n"
                           "                           project-scope installation\n");
    fprintf(stderr, "  -O0..-O3/-Os/-Oz Set optimization level (default: O0; --publish: Os;\n");
    fprintf(stderr, "                   -Oz = minimum size for edge/embedded deployments)\n");
    fprintf(stderr, "  -g, --debug      Emit DWARF debug info for source-level debugging (forces -O0)\n");
    fprintf(stderr, "  --target <name>  Cross-compile for target (e.g. linux-x64, linux-musl)\n");
    fprintf(stderr, "  --list-targets   Show available cross-compilation targets\n");
    fprintf(stderr, "  --subsystem <s>  PE subsystem: windows (GUI, auto-detected for GUI/html apps) or console\n");
    fprintf(stderr, "  -L<dir>/--libpath <dir>  Add a native library search directory\n");
    fprintf(stderr, "  --link-lib <name>  Link an extra native library (-> -l<name>)\n");
    fprintf(stderr, "  --link-input <f>   Link an extra object/resource/library file\n");
    fprintf(stderr, "  --icon <f.ico>   Embed an icon in the Windows executable\n");
    fprintf(stderr, "  --embed <p[=n]>  Bake a file/directory into the executable as\n");
    fprintf(stderr, "                   resources named <n>/<relative path> (repeatable)\n");
    fprintf(stderr, "  --no-icon        Do not embed any icon (skip the built-in default)\n");
    fprintf(stderr, "  --emit-symbols <f> Write the type/member index (IDE completion) and exit\n");
    fprintf(stderr, "  --no-gen         Disable the Zan-scripted code generators (bootstrap)\n");
    fprintf(stderr, "  --gen-meta <f>   Export compilation-unit metadata JSON to <f> and exit\n");
    fprintf(stderr, "  --time           Report how long each compiler phase took\n");
    fprintf(stderr, "  -D<name>[=value] Define preprocessor symbol\n");
    fprintf(stderr, "  --version, -v    Print version and exit\n");
    fprintf(stderr, "  --help, -h       Show this help and exit\n");
    fprintf(stderr, "  @<file>          Read further arguments from <file> (one per line or\n");
    fprintf(stderr, "                   whitespace separated, \"quoted\" for paths with spaces)\n");
}

/* 模块核心语义抽象与接口调用契约 */
typedef struct {
    char **items;
    int count;
    int cap;
} arg_list_t;

static void arg_list_push(arg_list_t *list, char *item) {
    if (list->count == list->cap) {
        list->cap = list->cap ? list->cap * 2 : 32;
        list->items = (char **)realloc(list->items, sizeof(char *) * (size_t)list->cap);
        if (!list->items) {
            fprintf(stderr, "error: out of memory expanding arguments\n");
            exit(1);
        }
    }
    list->items[list->count++] = item;
}

static bool expand_arg_file(const char *path, arg_list_t *out, int depth);

/* 内部辅助逻辑 */
static void arg_list_add(arg_list_t *out, const char *token, int depth) {
    if (token[0] == '@' && token[1] != '\0') {
        if (!expand_arg_file(token + 1, out, depth + 1)) { exit(1); }
        return;
    }
    char *copy = strdup(token);
    if (!copy) {
        fprintf(stderr, "error: out of memory expanding arguments\n");
        exit(1);
    }
    arg_list_push(out, copy);
}

static bool expand_arg_file(const char *path, arg_list_t *out, int depth) {
    if (depth > 8) {
        fprintf(stderr, "error: argument file '%s' nested too deeply\n", path);
        return false;
    }
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        fprintf(stderr, "error: cannot open argument file '%s'\n", path);
        return false;
    }
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (size < 0) { fclose(fp); return false; }
    char *text = (char *)malloc((size_t)size + 1);
    if (!text) { fclose(fp); return false; }
    size_t got = fread(text, 1, (size_t)size, fp);
    fclose(fp);
    text[got] = '\0';

    char *token = (char *)malloc(got + 1);
    if (!token) { free(text); return false; }
    size_t i = 0;
    /* 内部辅助逻辑 */
    if (got >= 3 && (unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB
        && (unsigned char)text[2] == 0xBF) {
        i = 3;
    }
    while (i < got) {
        while (i < got && (unsigned char)text[i] <= ' ') { i++; }
        if (i >= got) { break; }
        if (text[i] == '#') {
            while (i < got && text[i] != '\n') { i++; }
            continue;
        }
        size_t n = 0;
        bool quoted = false;
        while (i < got) {
            char c = text[i];
            if (c == '"') { quoted = !quoted; i++; continue; }
            if (!quoted && (unsigned char)c <= ' ') { break; }
            token[n++] = c;
            i++;
        }
        token[n] = '\0';
        if (n > 0) { arg_list_add(out, token, depth); }
    }
    free(token);
    free(text);
    return true;
}

/* 模块核心语义抽象与接口调用契约 */
static void expand_arg_files(int *argc, char ***argv) {
    bool any = false;
    for (int i = 1; i < *argc; i++) {
        if ((*argv)[i][0] == '@' && (*argv)[i][1] != '\0') { any = true; break; }
    }
    if (!any) { return; }
    arg_list_t out = {NULL, 0, 0};
    arg_list_add(&out, (*argv)[0], 0);
    for (int i = 1; i < *argc; i++) { arg_list_add(&out, (*argv)[i], 0); }
    arg_list_push(&out, NULL);
    *argc = out.count - 1;
    *argv = out.items;
}

int main(int argc, char **argv) {
#ifdef _WIN32
    char **utf8_argv = zan_utf8_command_line_argv(&argc);
    if (utf8_argv) argv = utf8_argv;
#endif
    {
        /* 内部辅助实现 */
        const char *cl[] = { "zanc", "--wasm-enable-eh", NULL };
        LLVMParseCommandLineOptions(2, cl, "");
    }
    expand_arg_files(&argc, &argv);
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--version") == 0 || strcmp(argv[i], "-v") == 0) {
            printf("zanc %s\n", ZAN_VERSION);
            return 0;
        }
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_usage();
            return 0;
        }
    }
    if (argc < 2) {
        print_usage();
        return 1;
    }

    const char *input_file = NULL;
    const char **input_files = NULL;
    int input_count = 0;
    int input_cap = 0;
    /* 内部辅助逻辑 */
    int explicit_input_count = 0;
    const char *output_file = NULL;
    bool do_dump_tokens = false;
    bool do_dump_ast = false;
    const char *emit_symbols_path = NULL; /* --emit-symbols <file> */
    const char *gen_meta_path = NULL;     /* 核心系统底层抽象与内存语义契约 */
    bool do_emit_ir = false;
    /* 内部辅助逻辑 */
    int check_leaks_opt = -1;
    int arc_guard_opt = -1;
    bool runtime_checks = true;
    /* 内部辅助逻辑 */
    bool strict_runtime = false;
    bool publish_mode = false;
    int obfuscate_strings_opt = -1;
    /* 内部辅助逻辑 */
    int error_limit = -1;
    bool debug_info = false; /* 底层系统交互与数据协议契约 */
    /* 小对象内存分配器管理池 */
    int fast_alloc_opt = 0;
    const char *stdlib_path = NULL;
    bool auto_stdlib = true;
    bool packages_disabled = false;
    /* 模块核心语义抽象与接口调用契约 */
    bool quiet = false;
    const char *package_api = NULL;
    const char *package_install_dir = NULL;
    const char *package_name = NULL;
    const char *package_project = NULL;
    bool package_list_missing = false;
    bool do_deny_warnings = false;
    zan_pkg_scope_t package_scope = ZAN_PKG_SCOPE_PROJECT;
    int opt_level = -1; /* 核心系统底层抽象与内存语义契约 */
#define ZAN_MAX_PP_DEFINES 1024
#define ZAN_MAX_LINK_INPUTS 1024
#define ZAN_MAX_EMBED_SPECS 1024
    const char *pp_defines[ZAN_MAX_PP_DEFINES];
    int pp_define_count = 0;
    const char *target_name = NULL; /* --target <name|triple>; NULL = host */
    bool link_static_drivers = false; /* 核心系统底层抽象与内存语义契约 */
    const char *driver_dir_override = NULL; /* --driver-dir */
    /* 内部辅助逻辑 */
    const char *link_subsystem = NULL;
    const char *icon_path = NULL;   /* 底层系统交互与数据协议契约 */
    bool no_icon = false;           /* 底层系统交互与数据协议契约 */
    bool emit_lib = false;          /* 底层系统交互与数据协议契约 */
    bool lib_shared = false;        /* 底层系统交互与数据协议契约 */
    const char *apk_path = NULL;    /* 底层系统交互与数据协议契约 */
    const char *apk_package = NULL; /* 底层系统交互与数据协议契约 */
    const char *apk_label = NULL;   /* 底层系统交互与数据协议契约 */
    const char *ipa_path = NULL;    /* 模块核心语义抽象与接口调用契约 */
    const char *ipa_bundle_id = NULL; /* 核心系统底层抽象与内存语义契约 */
    const char *ipa_name = NULL;    /* 核心系统底层抽象与内存语义契约 */
    const char *extra_link_inputs[ZAN_MAX_LINK_INPUTS]; int extra_link_input_count = 0;
    const char *embed_specs[ZAN_MAX_EMBED_SPECS]; int embed_spec_count = 0;
    const char *extra_link_libs[ZAN_LINK_MAX_LIBS]; int extra_link_lib_count = 0;
    const char *extra_lib_paths[ZAN_LINK_MAX_DIRS]; int extra_lib_path_count = 0;
    /* 内部辅助逻辑 */
    char resolved_stdlib_root[1024] = {0};
    char **design_outs = NULL;   /* 底层系统交互与数据协议契约 */
    size_t design_count = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--dump-tokens") == 0) {
            do_dump_tokens = true;
        } else if (strcmp(argv[i], "--dump-ast") == 0) {
            do_dump_ast = true;
        } else if (strcmp(argv[i], "--emit-symbols") == 0 && i + 1 < argc) {
            emit_symbols_path = argv[++i];
        } else if (strcmp(argv[i], "--no-gen") == 0) {
            zan_gen_enabled = 0;
        } else if (strcmp(argv[i], "--quiet") == 0 || strcmp(argv[i], "-q") == 0) {
            quiet = true;
        } else if (strcmp(argv[i], "--gen-meta") == 0 && i + 1 < argc) {
            gen_meta_path = argv[++i];
        } else if (strcmp(argv[i], "--emit-ir") == 0) {
            do_emit_ir = true;
        } else if (strcmp(argv[i], "--check-leaks") == 0) {
            check_leaks_opt = 1;
        } else if (strcmp(argv[i], "--no-check-leaks") == 0) {
            check_leaks_opt = 0;
        } else if (strcmp(argv[i], "--arc-guard") == 0) {
            arc_guard_opt = 1;
        } else if (strcmp(argv[i], "--no-arc-guard") == 0) {
            arc_guard_opt = 0;
        } else if (strcmp(argv[i], "--no-runtime-checks") == 0) {
            runtime_checks = false;
        } else if (strcmp(argv[i], "--strict-runtime") == 0) {
            strict_runtime = true;
        } else if (strcmp(argv[i], "--deny-warnings") == 0) {
            do_deny_warnings = true;
        } else if (strcmp(argv[i], "--publish") == 0) {
            publish_mode = true;
        } else if (strcmp(argv[i], "--obfuscate-strings") == 0) {
            obfuscate_strings_opt = 1;
        } else if (strcmp(argv[i], "--no-obfuscate-strings") == 0) {
            obfuscate_strings_opt = 0;
        } else if (strcmp(argv[i], "-g") == 0 || strcmp(argv[i], "--debug") == 0) {
            debug_info = true;
        } else if (strcmp(argv[i], "--fast-alloc") == 0) {
            fast_alloc_opt = 1;
        } else if (strcmp(argv[i], "--no-fast-alloc") == 0) {
            /* 内部辅助逻辑 */
            fast_alloc_opt = -1;
        } else if (strcmp(argv[i], "--link-mode") == 0 && i + 1 < argc) {
            const char *m = argv[++i];
            if (strcmp(m, "static") == 0) link_static_drivers = true;
            else if (strcmp(m, "shared") == 0) link_static_drivers = false;
            else { fprintf(stderr, "error: --link-mode must be 'static' or 'shared'\n"); return 1; }
        } else if (strcmp(argv[i], "--driver-dir") == 0 && i + 1 < argc) {
            driver_dir_override = argv[++i];
        } else if (strcmp(argv[i], "--target") == 0 && i + 1 < argc) {
            target_name = argv[++i];
        } else if (strcmp(argv[i], "--list-targets") == 0) {
            const zan_target_info_t *ts;
            int nt = zan_target_list(&ts);
            printf("Available cross-compilation targets:\n");
            for (int t = 0; t < nt; t++)
                printf("  %-12s %-30s %s\n", ts[t].name, ts[t].triple, ts[t].desc);
            return 0;
        } else if (strcmp(argv[i], "--stdlib-path") == 0 && i + 1 < argc) {
            stdlib_path = argv[++i];
        } else if (strcmp(argv[i], "--auto-stdlib") == 0) {
            auto_stdlib = true;
        } else if (strcmp(argv[i], "--no-stdlib") == 0) {
            auto_stdlib = false;
        } else if (strcmp(argv[i], "--no-packages") == 0) {
            /* 内部辅助逻辑 */
            packages_disabled = true;
            package_project_root[0] = '\0';
        } else if (strcmp(argv[i], "--package-api") == 0 && i + 1 < argc) {
            package_api = argv[++i];
        } else if (strcmp(argv[i], "--package-list-missing") == 0) {
            package_list_missing = true;
        } else if (strcmp(argv[i], "--package-install") == 0 && i + 1 < argc) {
            package_install_dir = argv[++i];
        } else if (strcmp(argv[i], "--package-name") == 0 && i + 1 < argc) {
            package_name = argv[++i];
        } else if (strcmp(argv[i], "--package-project") == 0 && i + 1 < argc) {
            package_project = argv[++i];
        } else if (strcmp(argv[i], "--package-scope") == 0 && i + 1 < argc) {
            const char *scope = argv[++i];
            if (strcmp(scope, "project") == 0) package_scope = ZAN_PKG_SCOPE_PROJECT;
            else if (strcmp(scope, "global") == 0) package_scope = ZAN_PKG_SCOPE_GLOBAL;
            else { fprintf(stderr, "ZANPKG_STATUS action=install status=invalid_scope\n"); return 1; }
        } else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            output_file = argv[++i];
        } else if (strcmp(argv[i], "-O0") == 0) {
            opt_level = 0;
        } else if (strcmp(argv[i], "-O1") == 0) {
            opt_level = 1;
        } else if (strcmp(argv[i], "-O2") == 0) {
            opt_level = 2;
        } else if (strcmp(argv[i], "-O3") == 0) {
            opt_level = ZAN_OPT_AGGRESSIVE;
        } else if (strcmp(argv[i], "-Os") == 0) {
            opt_level = ZAN_OPT_SIZE;
        } else if (strcmp(argv[i], "-Oz") == 0) {
            opt_level = ZAN_OPT_SIZE_MIN;
        } else if (strncmp(argv[i], "-ferror-limit=", 14) == 0) {
            /* 核心系统底层抽象与内存语义契约 */
            error_limit = atoi(argv[i] + 14);
        } else if (strcmp(argv[i], "--subsystem") == 0 && i + 1 < argc) {
            link_subsystem = argv[++i];
        } else if (strcmp(argv[i], "--time") == 0) {
            g_time_phases = true;
            g_phase_start = now_ms();
        } else if (strcmp(argv[i], "--emit-lib") == 0) {
            emit_lib = true;
        } else if (strcmp(argv[i], "--emit-apk") == 0 && i + 1 < argc) {
            /* 底层系统交互与数据协议契约 */
            emit_lib = true;
            apk_path = argv[++i];
        } else if (strcmp(argv[i], "--apk-package") == 0 && i + 1 < argc) {
            apk_package = argv[++i];
        } else if (strcmp(argv[i], "--apk-label") == 0 && i + 1 < argc) {
            apk_label = argv[++i];
        } else if (strcmp(argv[i], "--emit-ipa") == 0 && i + 1 < argc) {
            ipa_path = argv[++i];
        } else if (strcmp(argv[i], "--ipa-bundle-id") == 0 && i + 1 < argc) {
            ipa_bundle_id = argv[++i];
        } else if (strcmp(argv[i], "--ipa-name") == 0 && i + 1 < argc) {
            ipa_name = argv[++i];
        } else if (strcmp(argv[i], "--no-icon") == 0) {
            no_icon = true;
        } else if (strcmp(argv[i], "--icon") == 0 && i + 1 < argc) {
            icon_path = argv[++i];
        } else if (strcmp(argv[i], "--embed") == 0 && i + 1 < argc) {
            if (embed_spec_count < ZAN_MAX_EMBED_SPECS) embed_specs[embed_spec_count++] = argv[++i];
            else { fprintf(stderr, "error: too many --embed (max %d)\n", ZAN_MAX_EMBED_SPECS); return 1; }
        } else if (strcmp(argv[i], "--link-input") == 0 && i + 1 < argc) {
            if (extra_link_input_count < ZAN_MAX_LINK_INPUTS) extra_link_inputs[extra_link_input_count++] = argv[++i];
            else { fprintf(stderr, "error: too many --link-input (max %d)\n", ZAN_MAX_LINK_INPUTS); return 1; }
        } else if (strcmp(argv[i], "--link-lib") == 0 && i + 1 < argc) {
            if (extra_link_lib_count < ZAN_LINK_MAX_LIBS)
                extra_link_libs[extra_link_lib_count++] = argv[++i];
            else { fprintf(stderr, "error: too many --link-lib (max %d)\n",
                           ZAN_LINK_MAX_LIBS); return 1; }
        } else if (strcmp(argv[i], "--libpath") == 0 && i + 1 < argc) {
            if (extra_lib_path_count < ZAN_LINK_MAX_DIRS)
                extra_lib_paths[extra_lib_path_count++] = argv[++i];
            else { fprintf(stderr, "error: too many --libpath (max %d)\n",
                           ZAN_LINK_MAX_DIRS); return 1; }
        } else if (strncmp(argv[i], "-L", 2) == 0 && argv[i][2] != '\0') {
            if (extra_lib_path_count < ZAN_LINK_MAX_DIRS)
                extra_lib_paths[extra_lib_path_count++] = argv[i] + 2;
            else { fprintf(stderr, "error: too many -L dirs (max %d)\n",
                           ZAN_LINK_MAX_DIRS); return 1; }
        } else if (strncmp(argv[i], "-D", 2) == 0 && argv[i][2] != '\0') {
            if (pp_define_count < ZAN_MAX_PP_DEFINES) pp_defines[pp_define_count++] = argv[i] + 2;
            else { fprintf(stderr, "error: too many -D defines (max %d)\n", ZAN_MAX_PP_DEFINES);
                   return 1; }
        } else if (argv[i][0] != '-') {
            input_files_push(&input_files, &input_count, &input_cap, argv[i]);
        } else {
            fprintf(stderr, "unknown option: %s\n", argv[i]);
            return 1;
        }
    }

    if (package_api) {
        char status[256];
        if (!zan_pkg_api_validate(package_api, status, sizeof(status))) {
            fprintf(stderr, "%s\n", status); return 1;
        }
        if (!package_install_dir) {
            fprintf(stderr, "%s\n", status); return 2;
        }
    }
    if (package_install_dir) {
        char status[512];
        if (!package_name) {
            fprintf(stderr, "ZANPKG_STATUS action=install status=missing_name\n"); return 1;
        }
        const char *install_project = package_project ? package_project : ".";
        zan_package_t package_manifest;
        char manifest_path[1024];
        snprintf(manifest_path, sizeof(manifest_path), "%s/zan.pkg", package_install_dir);
        bool manifest_ok = zan_pkg_load(&package_manifest, manifest_path);
        if (!manifest_ok) { memset(&package_manifest, 0, sizeof(package_manifest)); }
        bool ok = zan_pkg_install_local(package_install_dir, package_name,
                                        package_scope, install_project,
                                        status, sizeof(status));
        fprintf(stderr, "%s\n", status);
        if (ok && package_manifest.plugin_id[0])
            fprintf(stderr, "ZANPKG_USAGE plugin_id=%s package=%s scope=%s\n",
                    package_manifest.plugin_id, package_name,
                    package_scope == ZAN_PKG_SCOPE_GLOBAL ? "global" : "project");
        if (manifest_ok) { zan_pkg_destroy(&package_manifest); }
        return ok ? 0 : 1;
    }

    if (input_count == 0) {
        fprintf(stderr, "error: no input file\n");
        return 1;
    }
    /* 核心系统底层抽象与内存语义契约 */
    for (int fi = 0; fi < input_count; fi++) {
        size_t pn = strlen(input_files[fi]);
        if (pn > 6 && strcmp(input_files[fi] + pn - 6, ".zform") == 0) {
            fprintf(stderr,
                    "error: '%s': the legacy .zform design format is no "
                    "longer supported; convert the document to an .html "
                    "design doc and recompile\n",
                    input_files[fi]);
            return 1;
        }
        if (pn > 7 && strcmp(input_files[fi] + pn - 7, ".zscene") == 0) {
            fprintf(stderr,
                    "error: '%s': the legacy .zscene format is no longer "
                    "supported; convert the scene document to an .html "
                    "scene doc (<body data-zan-scene=...>) and recompile\n",
                    input_files[fi]);
            return 1;
        }
    }

    input_file = input_files[0];
    /* 内部辅助实现 */
    if (package_project)
        snprintf(package_project_root, sizeof(package_project_root), "%s",
                 package_project);
    else if (!packages_disabled)
        resolve_package_project_root(input_file);
    for (int fi = 0; fi < input_count; fi++) {
        /* 内部辅助逻辑 */
        if (zan_is_design_path(input_files[fi]) ||
            zan_is_zcomp_path(input_files[fi])) continue;
        size_t nlen = 0;
        char *nsrc = read_file(input_files[fi], &nlen);
        if (!nsrc) continue;
        scan_namespace_tokens(nsrc, nlen);
        free(nsrc);
    }
    /* 底层系统交互与数据协议契约 */
    if (project_root_has_manifest()) {
        scan_project_namespaces(package_project_root);
    }

    zan_target_t target;
    if (target_name) {
        if (!zan_target_parse(target_name, &target)) {
            fprintf(stderr, "error: unknown target '%s' (see --list-targets)\n", target_name);
            return 1;
        }
    } else {
        zan_target_host(&target);
    }
    bool cross_compiling = (target_name != NULL);
    /* 内部辅助逻辑 */
    bool external_async_executor = (target.os == ZAN_OS_WINDOWS ||
                                    target.os == ZAN_OS_LINUX ||
                                    target.os == ZAN_OS_MACOS ||
                                    target.os == ZAN_OS_IOS) &&
                                   (target.arch == ZAN_ARCH_X86_64 ||
                                    target.arch == ZAN_ARCH_AARCH64);

    /* 内部辅助逻辑 */
    if (auto_stdlib || stdlib_path) {
        char stdlib_root[1024];
        if (stdlib_path) {
            snprintf(stdlib_root, sizeof(stdlib_root), "%s", stdlib_path);
        } else {
            /* 底层系统交互与数据协议契约 */
#ifdef _WIN32
            char exe_path[1024];
            GetModuleFileNameA(NULL, exe_path, sizeof(exe_path));
            char *last_sep = strrchr(exe_path, '\\');
            if (last_sep) *last_sep = '\0';
            snprintf(stdlib_root, sizeof(stdlib_root), "%s\\..\\stdlib", exe_path);
            {
                DWORD attr = zan_utf8_get_file_attributes(stdlib_root);
                if (attr == INVALID_FILE_ATTRIBUTES || !(attr & FILE_ATTRIBUTE_DIRECTORY)) {
                    snprintf(stdlib_root, sizeof(stdlib_root), "%s\\stdlib", exe_path);
                }
            }
#elif defined(__APPLE__)
            /* 模块核心语义抽象与接口调用契约 */
            char exe_path[1024];
            uint32_t exe_sz = sizeof(exe_path);
            if (_NSGetExecutablePath(exe_path, &exe_sz) == 0) {
                char *last_sep = strrchr(exe_path, '/');
                if (last_sep) *last_sep = '\0';
                snprintf(stdlib_root, sizeof(stdlib_root), "%s/../stdlib", exe_path);
            } else {
                snprintf(stdlib_root, sizeof(stdlib_root), "stdlib");
            }
#else
            char exe_path[1024];
            ssize_t elen = readlink("/proc/self/exe", exe_path, sizeof(exe_path)-1);
            if (elen > 0) {
                exe_path[elen] = '\0';
                char *last_sep = strrchr(exe_path, '/');
                if (last_sep) *last_sep = '\0';
                snprintf(stdlib_root, sizeof(stdlib_root), "%s/../stdlib", exe_path);
            } else {
                snprintf(stdlib_root, sizeof(stdlib_root), "stdlib");
            }
#endif
        }
    /* 核心系统底层抽象与内存语义契约 */
    {
        /* 内部辅助逻辑 */
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

    /* 内部辅助逻辑 */
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

        /* 底层系统交互与数据协议契约 */
        /* 内部辅助逻辑 */
        design_outs = zan_gen_design(
            resolved_stdlib_root, (const char *const *)input_files,
            (size_t)input_count);
        if (!design_outs) return 1;
        design_count = (size_t)input_count;
        explicit_input_count = input_count;

        /* 内部辅助实现 */
        pi_filter_active = !emit_symbols_path &&
                           getenv("ZAN_NO_PULLIN_FILTER") == NULL;
        if (pi_filter_active) {
            pi_arena = zan_arena_new();
            pi_target = target;
            pi_pp_defines = pp_defines;
            pi_pp_define_count = pp_define_count;
            pi_publish_mode = publish_mode;
            /* 内部辅助实现 */
            pi_stdlib_root_buf = resolved_stdlib_root;
            /* 内部辅助逻辑 */
        } else {
        /* 内部辅助逻辑 */
        int scanned = 0;
        while (scanned < input_count) {
            int round_end = input_count;
            for (int fi = scanned; fi < round_end; fi++) {
                /* 内部辅助逻辑 */
                if (zan_is_zcomp_path(input_files[fi])) continue;
                pi_reach_input_dir(input_files[fi]);
                size_t slen3 = 0;
                char *src3 = read_file(input_files[fi], &slen3);
                if (!src3) continue;
                /* 底层系统交互与数据协议契约 */
                char *owned = NULL;
                if ((size_t)fi < design_count && design_outs[fi]) {
                    free(src3);
                    src3 = strdup(design_outs[fi]);
                    owned = src3;
                    if (!src3) { fprintf(stderr, "error: out of memory\n"); return 1; }
                }
                scan_using_tokens(src3, strlen(src3), stdlib_root,
                                  &input_files, &input_count, &input_cap);
                free(owned ? owned : src3);
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

    /* 模块核心语义抽象与接口调用契约 */
    size_t source_len;
    char *source = read_file(input_file, &source_len);
    if (!source) return 1;
    if (do_dump_tokens) {
        dump_tokens(source, source_len, input_file);
        free(source);
        return 0;
    }

    /* parse */
    zan_arena_t *arena = zan_arena_new();
    g_main_arena = arena;
    zan_diag_t *diag = zan_diag_new(arena);
    zan_diag_set_deny_warnings(diag, do_deny_warnings);
    if (error_limit >= 0) zan_diag_set_max_errors(diag, error_limit);

    /* 内部辅助实现 */
    zan_ast_node_t *ast = NULL;
    int scanned = 0;
    for (;;) {
        int round_end = input_count;
    for (int fi = scanned; fi < round_end; fi++) {
        /* 核心系统底层抽象与内存语义契约 */
        if (fi > 0 && zan_is_zcomp_path(input_files[fi])) continue;
        size_t slen = 0;
        char *src = (fi == 0) ? source : read_file(input_files[fi], &slen);
        if (fi == 0) slen = source_len;
        if (!src) {
            fprintf(stderr, "error: cannot read '%s'\n", input_files[fi]);
            zan_arena_free(arena);
            free(source);
            return 1;
        }
        if (fi > 0) {
            /* 内部辅助实现 */
            char *heap_src = src;
            src = zan_arena_strdup(arena, heap_src, slen);
            free(heap_src);
            if (!src) {
                fprintf(stderr, "error: out of memory\n");
                zan_arena_free(arena);
                free(source);
                return 1;
            }
        }
        {
            /* 内部辅助实现 */
            if ((size_t)fi < design_count && design_outs[fi]) {
                char *owned_text = design_outs[fi];
                design_outs[fi] = NULL;
                if (fi == 0) {
                    /* 内部辅助逻辑 */
                    free(src);
                    src = owned_text;
                    source = src;
                } else {
                    /* 内部辅助逻辑 */
                    src = zan_arena_strdup(arena, owned_text,
                                           strlen(owned_text));
                    free(owned_text);
                    if (!src) {
                        fprintf(stderr, "error: out of memory\n");
                        zan_arena_free(arena);
                        free(source);
                        return 1;
                    }
                }
                slen = strlen(src);
            }
        }
        zan_diag_add_file(diag, input_files[fi], src);

        zan_lexer_t lex;
        zan_lexer_init(&lex, src, slen, fi, arena, diag);

        zan_apply_lex_defines(&lex, target, pp_defines, pp_define_count,
                              publish_mode);

        zan_parser_t parser;
        zan_parser_init(&parser, &lex, arena, diag);

        int errors_before = diag->error_count;
        zan_ast_node_t *unit = zan_parser_parse(&parser);
        g_scale_stats.real_parses++;
        int parse_failed = !unit || diag->error_count > errors_before;
        zan_nsresolve_stamp(unit, arena);
        /* 模块核心语义抽象与接口调用契约 */
        if (pi_filter_active) {
            int seed_entry = fi < explicit_input_count &&
                             !zan_is_zcomp_path(input_files[fi]);
            pi_seed_stdlib_input =
                seed_entry ? pi_reach_input_dir(input_files[fi]) : 0;
            /* 模块核心语义抽象与接口调用契约 */
            pi_seeding_stdlib =
                fi > 0 && auto_stdlib && resolved_stdlib_root[0] &&
                input_files[fi] &&
                zan_path_is_under(input_files[fi], resolved_stdlib_root);
            if (parse_failed) {
                g_scale_stats.throwaway_parse_fallbacks++;
                pi_seed_source(src, slen);
            } else {
                pi_seed_parsed_unit(unit, seed_entry);
            }
            pi_seeding_stdlib = 0;
            pi_seed_stdlib_input = 0;
        }
        /* 内部辅助逻辑 */
        if (fi > 0 && auto_stdlib && resolved_stdlib_root[0] && input_files[fi] &&
            zan_path_is_under(input_files[fi], resolved_stdlib_root)) {
            for (int k = 0; k < unit->comp_unit.decls.count; k++)
                if (unit->comp_unit.decls.items[k])
                    unit->comp_unit.decls.items[k]->from_stdlib = 1;
        }

        if (!ast) {
            ast = unit;
        } else {
            for (int k = 0; k < unit->comp_unit.usings.count; k++)
                zan_ast_list_push(&ast->comp_unit.usings,
                                  unit->comp_unit.usings.items[k], arena);
            for (int k = 0; k < unit->comp_unit.decls.count; k++)
                zan_ast_list_push(&ast->comp_unit.decls,
                                  unit->comp_unit.decls.items[k], arena);
            if (!ast->comp_unit.ns) ast->comp_unit.ns = unit->comp_unit.ns;
        }
    }
        scanned = round_end;
        if (pi_filter_active) {
            /* 内部辅助逻辑 */
            int changed = pi_close_once(resolved_stdlib_root);
            int fresh = pi_append_included(&input_files, &input_count,
                                           &input_cap);
            if (!changed && !fresh) {
                /* 内部辅助实现 */
                if (!pi_repair_done && pi_unsatisfied_live_name()) {
                    pi_repair_done = 1;
                    pi_repair_walk(resolved_stdlib_root, "");
                    continue;
                }
                pi_debug_dump();
                break;
            }
        } else if (scanned >= input_count) {
            break;
        }
    }
    free(design_outs); /* 底层系统交互与数据协议契约 */

    phase("parse");
    probe_phase_mem("parse");
    if (g_time_phases) zan_arena_dump_stats();

    if (!zan_diag_has_errors(diag)) {
        zan_compile_trace("flatten nested types");
        zan_parser_flatten_nested_types(ast, arena, diag);
        zan_compile_trace("merge partials");
        zan_parser_merge_partials(ast, arena, diag);
        zan_compile_trace("desugar events");
        zan_parser_desugar_events(ast, arena, diag);
        phase("flatten/merge");
        zan_compile_trace("nsresolve");
        zan_nsresolve_run(ast, arena, diag);
        phase("nsresolve_1");
        zan_compile_trace("specialize generic bases");
        zan_parser_specialize_generic_bases(ast, arena, diag);
        phase("specialize_bases");

        /* 内部辅助逻辑 */
        if (gen_meta_path) {
            char *meta = zan_genmeta_export_files(ast, diag);
            if (!meta) {
                fprintf(stderr, "error: cannot export metadata\n");
                zan_arena_free(arena);
                free(source);
                return 1;
            }
            FILE *out = fopen(gen_meta_path, "wb");
            int ok = out &&
                     fwrite(meta, 1, strlen(meta), out) == strlen(meta) &&
                     fclose(out) == 0;
            free(meta);
            if (!ok) {
                fprintf(stderr, "error: cannot write metadata '%s'\n",
                        gen_meta_path);
                zan_arena_free(arena);
                free(source);
                return 1;
            }
            zan_arena_free(arena);
            free(source);
            return 0;
        }

        zan_compile_trace("codegen");
        /* 内部辅助逻辑 */
        if (zan_gen_enabled &&
            zan_gen_codegen(ast, arena, diag, resolved_stdlib_root) != 0) {
            zan_arena_free(arena);
            free(source);
            return 1;
        }
        phase("codegen");
        /* 内部辅助实现 */
        if (pi_filter_active) {
            char **gen_texts = NULL;
            int gen_text_count = 0;
            zan_gen_take_source_texts(&gen_texts, &gen_text_count);
            for (int gi = 0; gi < gen_text_count; gi++) {
                pi_seed_source(gen_texts[gi], strlen(gen_texts[gi]));
                free(gen_texts[gi]);
            }
            free(gen_texts);
            /* 内部辅助实现 */
            for (;;) {
                int changed = pi_close_once(resolved_stdlib_root);
                int fresh = pi_append_included(&input_files, &input_count,
                                               &input_cap);
                if (!changed && !fresh) {
                    /* 内部辅助逻辑 */
                    if (!pi_repair_done && pi_unsatisfied_live_name()) {
                        pi_repair_done = 1;
                        pi_repair_walk(resolved_stdlib_root, "");
                        continue;
                    }
                    pi_debug_dump();
                    break;
                }
                for (int fi = input_count - fresh;
                     fresh > 0 && fi < input_count; fi++) {
                    int errors_before = diag->error_count;
                    zan_ast_node_t *unit = parse_secondary_unit(
                        input_files[fi], target, pp_defines, pp_define_count,
                        publish_mode, arena, diag);
                    if (!unit) {
                        fprintf(stderr, "error: cannot read '%s'\n",
                                input_files[fi]);
                        zan_arena_free(arena);
                        free(source);
                        return 1;
                    }
                    zan_nsresolve_stamp(unit, arena);
                    /* 内部辅助逻辑 */
                    pi_seeding_stdlib =
                        auto_stdlib && resolved_stdlib_root[0] &&
                        zan_path_is_under(input_files[fi],
                                          resolved_stdlib_root);
                    if (diag->error_count > errors_before) {
                        /* 内部辅助逻辑 */
                        g_scale_stats.throwaway_parse_fallbacks++;
                        size_t flen = 0;
                        char *fsrc = read_file(input_files[fi], &flen);
                        if (fsrc) {
                            pi_seed_source(fsrc, flen);
                            free(fsrc);
                        }
                    } else {
                        pi_seed_parsed_unit(unit, 0);
                    }
                    pi_seeding_stdlib = 0;
                    if (auto_stdlib && resolved_stdlib_root[0] &&
                        zan_path_is_under(input_files[fi], resolved_stdlib_root)) {
                        for (int k = 0; k < unit->comp_unit.decls.count; k++)
                            if (unit->comp_unit.decls.items[k])
                                unit->comp_unit.decls.items[k]->from_stdlib = 1;
                    }
                    for (int k = 0; k < unit->comp_unit.usings.count; k++)
                        zan_ast_list_push(&ast->comp_unit.usings,
                                          unit->comp_unit.usings.items[k], arena);
                    for (int k = 0; k < unit->comp_unit.decls.count; k++)
                        zan_ast_list_push(&ast->comp_unit.decls,
                                          unit->comp_unit.decls.items[k], arena);
                }
            }
        }
        if (pi_arena) {
            zan_arena_free(pi_arena);
            pi_arena = NULL;
            memset(pi_table, 0, sizeof(pi_table));
            pi_dirs_head = pi_dirs_tail = NULL;
        }
        /* 内部辅助实现 */
        zan_compile_trace("nsresolve generated");
        zan_nsresolve_run(ast, arena, diag);
        /* 底层系统交互与数据协议契约 */
        zan_compile_trace("prune");
        zan_nsresolve_prune(ast, arena, diag);
        phase("prune");
        zan_compile_trace("resolve done");
    }

    package_sources_destroy();

    /* 内部辅助实现 */
    if (emit_symbols_path) {
        int srv = zan_symbols_emit(ast, emit_symbols_path);
        if (srv != 0)
            fprintf(stderr, "error: cannot write symbol index '%s'\n",
                    emit_symbols_path);
        zan_arena_free(arena);
        return srv;
    }

    if (do_dump_ast) {
        if (!zan_diag_has_errors(diag)) {
            dump_ast_node(ast, 0);
        }
    }

    if (zan_diag_has_errors(diag)) {
        fprintf(stderr, "\n%d error(s), %d warning(s)\n",
                diag->error_count, diag->warning_count);
        report_suppressed_errors(diag);
        zan_diag_free_buffers(diag);
        zan_arena_free(arena);
        free(source);
        return 1;
    }

    phase("resolve");
    probe_phase_mem("resolve");

    zan_binder_t binder;
    zan_binder_init(&binder, arena, diag);
    zan_compile_trace("bind");
    zan_binder_bind(&binder, ast);

    phase("bind");
    probe_phase_mem("bind");

    zan_checker_t checker;
    zan_checker_init(&checker, &binder, arena, diag);
    zan_compile_trace("check");
    zan_checker_check(&checker, ast);

    if (do_dump_ast) {
        /* 核心系统底层抽象与内存语义契约 */
    }

    if (zan_diag_has_errors(diag)) {
        fprintf(stderr, "\n%d error(s) after type checking\n",
                diag->error_count);
        report_suppressed_errors(diag);
        zan_diag_free_buffers(diag);
        zan_arena_free(arena);
        free(source);
        return 1;
    }

    phase("check");
    probe_phase_mem("check");

    zan_irgen_t irgen;
    /* 内部辅助逻辑 */
    const char *irgen_triple = NULL;
    if (cross_compiling) {
        if (target.os == ZAN_OS_WINDOWS)
            irgen_triple = (target.arch == ZAN_ARCH_AARCH64)
                           ? "aarch64-w64-windows-gnu"
                           : "x86_64-w64-windows-gnu";
        else
            irgen_triple = target.triple;
    }
    /* 核心系统底层抽象与内存语义契约 */
    if (output_file && !emit_lib) {
        const char *ext = strrchr(output_file, '.');
        if (ext) {
            if (strcmp(ext, ".dll") == 0 || strcmp(ext, ".so") == 0 ||
                strcmp(ext, ".dylib") == 0) {
                emit_lib = true; lib_shared = true;
            } else if (strcmp(ext, ".a") == 0 || strcmp(ext, ".lib") == 0) {
                emit_lib = true; lib_shared = false;
            }
        }
    }
    if (emit_lib && output_file && !lib_shared) {
        const char *ext = strrchr(output_file, '.');
        if (ext && (strcmp(ext, ".dll") == 0 || strcmp(ext, ".so") == 0 ||
                    strcmp(ext, ".dylib") == 0))
            lib_shared = true;
    }
    if (apk_path) {
        /* 核心系统底层抽象与内存语义契约 */
        lib_shared = true;
        if (target.os != ZAN_OS_ANDROID) {
            fprintf(stderr, "error: --emit-apk requires --target "
                    "android-x64 or android-arm64\n");
            return 1;
        }
        /* 模块核心语义抽象与接口调用契约 */
        if (project_root_has_manifest()) {
            if (!load_proj_android_keys_done) {
                load_proj_android_keys();
                load_proj_android_keys_done = true;
            }
            if (!proj_android_keys_ok) return 1;
            if (!apk_package && proj_android_package[0]) {
                apk_package = proj_android_package;
            }
            if (!apk_label && proj_android_label[0]) {
                apk_label = proj_android_label;
            }
        }
    }
    if (!ipa_path && output_file) {
        size_t olen = strlen(output_file);
        if (olen >= 4 && strcmp(output_file + olen - 4, ".ipa") == 0) {
            ipa_path = output_file;
        }
    }
    if (ipa_path) {
        if (target.os != ZAN_OS_IOS) {
            fprintf(stderr, "error: --emit-ipa requires --target ios-arm64\n");
            return 1;
        }
    }
    /* 内部辅助逻辑 */
    bool check_leaks = check_leaks_opt >= 0 ? check_leaks_opt != 0
                                           : (debug_info && !publish_mode);
    bool arc_guard = arc_guard_opt >= 0 ? arc_guard_opt != 0
                                        : (debug_info && !publish_mode);
    /* 内部辅助实现 */
    bool arc_net = publish_mode && arc_guard_opt != 1 && arc_guard_opt != 0;
    zan_arena_t *ir_arena = zan_arena_new();
    if (!ir_arena) {
        fprintf(stderr, "error: failed to allocate code-generation arena\n");
        zan_diag_free_buffers(diag);
        zan_arena_free(arena);
        free(source);
        return 1;
    }
    if (zan_irgen_init(&irgen, ir_arena, diag, &binder, input_file,
                       irgen_triple,
                       target.os == ZAN_OS_WINDOWS, external_async_executor,
                       check_leaks, runtime_checks, arc_guard,
                       arc_net) != ZAN_OK) {
        fprintf(stderr, "error: failed to initialize code generator\n");
        zan_diag_free_buffers(diag);
        zan_arena_free(ir_arena);
        zan_arena_free(arena);
        free(source);
        return 1;
    }
    irgen.emit_debug = debug_info;
    irgen.strict_runtime = strict_runtime;
    irgen.publish_mode = publish_mode;
    /* 内部辅助逻辑 */
    irgen.rt_guard_split = !cross_compiling;
    /* 模块核心语义抽象与接口调用契约 */
    const char *obf_env = getenv("ZAN_OBF");
    const char *no_obf_env = getenv("ZAN_NO_OBF");
    if (obfuscate_strings_opt >= 0) {
        irgen.obfuscate_strings = (obfuscate_strings_opt == 1);
    } else if (obf_env && obf_env[0] == '1') {
        irgen.obfuscate_strings = true;
    } else if (no_obf_env && no_obf_env[0] == '1') {
        irgen.obfuscate_strings = false;
    } else {
        irgen.obfuscate_strings = false;
    }
    irgen.fast_codegen = false;
    irgen.emit_lib = emit_lib;
    irgen.emit_shared = lib_shared;

    const char *shard_env = getenv("ZAN_SHARD");
    const char *no_shard_env = getenv("ZAN_NO_SHARD");
    bool native_arch = (target.arch == ZAN_ARCH_X86_64 ||
                        target.arch == ZAN_ARCH_AARCH64);
    bool shard_opt_out = (shard_env && shard_env[0] == '0') ||
                         (no_shard_env && no_shard_env[0] == '1');
    /* 底层系统交互与数据协议契约 */
    bool want_shard = native_arch && !cross_compiling && !shard_opt_out &&
                      ((shard_env && shard_env[0] == '1') || publish_mode);

    /* 内部辅助逻辑 */
    zan_irgen_bind_target(&irgen);
    if (zan_irgen_emit(&irgen, ast) != ZAN_OK) {
        fprintf(stderr, "error: code generation failed\n");
        report_suppressed_errors(irgen.diag);
        zan_diag_free_buffers(irgen.diag);
        zan_irgen_destroy(&irgen);
        zan_arena_free(ir_arena);
        zan_arena_free(arena);
        free(source);
        return 1;
    }

    /* 内部辅助逻辑 */
    zan_prepare_static_runtime(&irgen, &target);

    phase("irgen");
    probe_phase_mem("irgen");
    if (g_time_phases) {
        size_t definitions = 0, declarations = 0, blocks = 0, instructions = 0;
        for (LLVMValueRef fn = LLVMGetFirstFunction(irgen.mod); fn;
             fn = LLVMGetNextFunction(fn)) {
            LLVMBasicBlockRef bb = LLVMGetFirstBasicBlock(fn);
            if (!bb) { declarations++; continue; }
            definitions++;
            for (; bb; bb = LLVMGetNextBasicBlock(bb)) {
                blocks++;
                for (LLVMValueRef inst = LLVMGetFirstInstruction(bb); inst;
                     inst = LLVMGetNextInstruction(inst)) instructions++;
            }
        }
        fprintf(stderr, "IR stats: %zu AST nodes created; %zu defined functions, "
                "%zu declarations, %zu blocks, %zu instructions\n",
                zan_ast_node_count(), definitions, declarations, blocks, instructions);
        fprintf(stderr, "Scale stats: %zu file reads (%zu MB), %zu metadata scans, "
                "%zu meta-cache hit/%zu miss/%zu written, "
                "%zu seed sources/%zu lexer passes, %zu throwaway parses "
                "(%zu fallback), %zu real parses, %zu secondary parses\n",
                g_scale_stats.file_reads,
                g_scale_stats.bytes_read / (1024 * 1024),
                g_scale_stats.metadata_scans,
                g_scale_stats.metadata_cache_hits,
                g_scale_stats.metadata_cache_misses,
                g_scale_stats.metadata_cache_writes,
                g_scale_stats.seed_sources,
                g_scale_stats.seed_lex_passes,
                g_scale_stats.throwaway_parses,
                g_scale_stats.throwaway_parse_fallbacks,
                g_scale_stats.real_parses,
                g_scale_stats.secondary_parses);
        zan_arena_dump_stats();
    }

    /* 编译期中间表示与代码生成内部规范 */
    if (arena) {
        for (int i = 0; i < irgen.extern_lib_count; i++) {
            zan_istr_t *lib = &irgen.extern_libs[i];
            lib->str = zan_arena_strdup(ir_arena, lib->str, lib->len);
        }
        for (int i = 0; i < irgen.extern_fn_count; i++) {
            zan_istr_t *lib = &irgen.extern_fns[i].lib;
            zan_istr_t *name = &irgen.extern_fns[i].name;
            lib->str = zan_arena_strdup(ir_arena, lib->str, lib->len);
            name->str = zan_arena_strdup(ir_arena, name->str, name->len);
        }
        zan_diag_t *ir_diag = (zan_diag_t *)zan_arena_alloc(ir_arena, sizeof(*ir_diag));
        *ir_diag = *diag;
        ir_diag->file_sources = NULL;
        const char **ir_file_names = (const char **)malloc(
            (size_t)ir_diag->file_count * sizeof(*ir_file_names));
        for (int i = 0; i < ir_diag->file_count; i++)
            ir_file_names[i] = zan_arena_strdup(ir_arena,
                ir_diag->file_names[i], strlen(ir_diag->file_names[i]));
        ir_diag->file_names = ir_file_names;
        ir_diag->entries = NULL;
        ir_diag->entry_count = ir_diag->entry_cap = 0;
        irgen.diag = ir_diag;
        zan_diag_free_buffers(diag);
        zan_arena_free(arena);
        arena = NULL;
        g_main_arena = ir_arena;
        irgen.binder = NULL;
    }
    probe_phase_mem("release ast");
    if (source) { free(source); source = NULL; }

    /* 内部辅助实现 */
    const char *mf_json_path = getenv("ZAN_CODEGEN_MANIFEST_JSON");
    zan_cg_manifest_t mf;
    bool mf_built = false;
    if (getenv("ZAN_CODEGEN_MANIFEST") || mf_json_path || want_shard) {
        zan_opt_strip_unused(&irgen);
        /* 底层系统交互与数据协议契约 */
        zan_irgen_prune_extern_libs(&irgen);
        phase("manifest");
        probe_phase_mem("manifest");
        bool mf_native = target.arch == ZAN_ARCH_X86_64 ||
                         target.arch == ZAN_ARCH_AARCH64;
        zan_irgen_manifest_build(&irgen, &mf, mf_native);
        mf_built = true;
        if (getenv("ZAN_CODEGEN_MANIFEST"))
            zan_irgen_manifest_report(&irgen, &mf);
        if (mf_json_path &&
            zan_irgen_manifest_write_json(&irgen, &mf, mf_json_path) != ZAN_OK)
            fprintf(stderr, "warning: cannot write codegen manifest '%s'\n",
                    mf_json_path);
    }

    zan_opt_level_t effective_opt = ZAN_OPT_NONE;
    if (opt_level >= 0) {
        effective_opt = (zan_opt_level_t)opt_level;
    } else if (publish_mode) {
        /* 内部辅助逻辑 */
        effective_opt = ZAN_OPT_SIZE;
    }
    /* 内部辅助逻辑 */
    if (debug_info && effective_opt > ZAN_OPT_NONE) {
        fprintf(stderr, "note: -g forces -O0 (debug info is emitted unoptimized)\n");
        effective_opt = ZAN_OPT_NONE;
    }
    /* 底层系统交互与数据协议契约 */
    char obj_path[1024];
    if (ipa_path) {
        snprintf(obj_path, sizeof(obj_path), "%s.macho_tmp", ipa_path);
    } else if (output_file) {
        snprintf(obj_path, sizeof(obj_path), "%s", output_file);
    } else {
        size_t ilen = strlen(input_file);
        if (ilen > 4 && strcmp(input_file + ilen - 4, ".zan") == 0) {
            snprintf(obj_path, sizeof(obj_path), "%.*s", (int)(ilen - 4), input_file);
        } else {
            snprintf(obj_path, sizeof(obj_path), "%s.out", input_file);
        }
    }

    if (output_file) {
        char out_dir[1024];
        size_t olen = strlen(output_file);
        if (olen < sizeof(out_dir)) {
            memcpy(out_dir, output_file, olen + 1);
            for (char *p = out_dir; *p; p++) {
                if ((*p == '/' || *p == '\\') && p > out_dir) {
                    char sep = *p;
                    *p = '\0';
#ifdef _WIN32
                    CreateDirectoryA(out_dir, NULL);
#else
                    mkdir(out_dir, 0755);
#endif
                    *p = sep;
                }
            }
        }
    }

    zan_driver_registry_t driver_reg;
    zan_driver_prefix_snapshot_t driver_prefixes = {0};
    if (!do_emit_ir) {
        if (!mf_built) {
            zan_opt_strip_unused(&irgen);
            zan_irgen_prune_extern_libs(&irgen);
        }
        zan_discover_drivers(resolved_stdlib_root, &driver_reg);
        /* 内部辅助逻辑 */
        zan_driver_capture_conditions(&irgen, ir_arena, &driver_reg, &target,
                                      driver_dir_override, &driver_prefixes);
    }

    /* 内部辅助实现 */
    char **shard_objs = NULL;
    int shard_n = 0;
    if (!do_emit_ir && want_shard && mf_built) {
        phase("shard");
        shard_n = zan_irgen_shard_run(&irgen, &mf, obj_path, &shard_objs);
        if (shard_n < 0) {
            fprintf(stderr, "error: shard emission failed\n");
            zan_diag_free_buffers(irgen.diag);
            zan_irgen_destroy(&irgen);
            zan_arena_free(ir_arena);
            if (arena) zan_arena_free(arena);
            free(source);
            return 1;
        }
    }
    if (shard_n > 0) phase("shard emit");
    probe_phase_mem("shard emit");
    if (mf_built) {
        zan_irgen_manifest_free(&mf);
        mf_built = false;
    }

    if (effective_opt > ZAN_OPT_NONE) {
        zan_opt_report_t opt_report = zan_optimize(&irgen, NULL, effective_opt);
        zan_opt_report_print(&opt_report);
        phase("optimize");
        probe_phase_mem("optimize");
        /* 内部辅助逻辑 */
        if (shard_n == 0)
            zan_irgen_prune_extern_libs(&irgen);
    } else {
        /* 内部辅助逻辑 */
        /* 内部辅助实现 */
        if (!irgen.target_is_wasm)
            irgen.fast_codegen = true;
        if (!do_emit_ir) {
            zan_opt_strip_unused(&irgen);
            /* 内部辅助实现 */
            if (shard_n == 0)
                zan_irgen_prune_extern_libs(&irgen);
        }
    }

    if (do_emit_ir) {
        if (zan_irgen_write_ir(&irgen, NULL) != ZAN_OK) {
            fprintf(stderr, "error: failed to write LLVM IR\n");
            zan_diag_free_buffers(irgen.diag);
            zan_irgen_destroy(&irgen);
            zan_arena_free(ir_arena);
            zan_arena_free(arena);
            free(source);
            return 1;
        }
    } else {

        /* 内部辅助实现 */
        char cross_archives[ZAN_MAX_USED_DRIVERS][1200];
        int cross_archive_count = 0;
        if (cross_compiling && (target.os == ZAN_OS_LINUX
                                || target.os == ZAN_OS_OHOS)) {
            zan_driver_registry_t cross_reg;
            zan_discover_drivers(resolved_stdlib_root, &cross_reg);
            const char *dsub = zan_driver_subdir(&target);
            for (int li = 0; li < irgen.extern_lib_count; li++) {
                int nlen;
                const char *nm = zan_dllimport_lname(
                    irgen.extern_libs[li].str,
                    (int)irgen.extern_libs[li].len, &nlen);
                if (!nm) continue; /* 底层系统交互与数据协议契约 */
                char archive[1200];
                archive[0] = '\0';
                int didx = zan_driver_find(&cross_reg, nm, nlen);
                if (didx >= 0) {
                    snprintf(archive, sizeof(archive),
                             "%s/%s/drivers/%s/static/lib%.*s.a",
                             cross_reg.entries[didx].root,
                             cross_reg.entries[didx].module, dsub, nlen, nm);
                }
                if (archive[0] && zan_file_exists(archive)) {
                    if (cross_archive_count >= ZAN_MAX_USED_DRIVERS)
                        link_cap_exceeded("static driver archives", ZAN_MAX_USED_DRIVERS);
                    {
                        snprintf(cross_archives[cross_archive_count],
                                 sizeof(cross_archives[0]), "%s", archive);
                        cross_archive_count++;
                    }
                } else {
                    int n = zan_irgen_stub_extern_lib(
                        &irgen, irgen.extern_libs[li].str,
                        (int)irgen.extern_libs[li].len);
                    if (n > 0 && !zan_win_system_lib(
                            irgen.extern_libs[li].str,
                            (int)irgen.extern_libs[li].len)) {
                        fprintf(stderr,
                                "warning: no static %s archive for "
                                "[DllImport(\"%.*s\")]; its %d function(s) "
                                "are stubbed and will fail at runtime\n",
                                dsub, (int)irgen.extern_libs[li].len,
                                irgen.extern_libs[li].str, n);
                    }
                }
            }
        }

        /* 内部辅助实现 */
        if (cross_compiling && target.os == ZAN_OS_OHOS) {
            char oexe[1024] = {0};
            zan_exe_dir(oexe, sizeof(oexe));
            const char *odrop_sub = (target.arch == ZAN_ARCH_AARCH64)
                                    ? "ohos-arm64" : "ohos-x64";
            zan_driver_registry_t oreg;
            zan_discover_drivers(resolved_stdlib_root, &oreg);
            /* 内部辅助逻辑 */
            for (int li = irgen.extern_lib_count - 1; li >= 0; li--) {
                if (zan_win_system_lib(irgen.extern_libs[li].str,
                                       (int)irgen.extern_libs[li].len))
                    continue;
                int nlen;
                const char *nm = zan_dllimport_lname(
                    irgen.extern_libs[li].str,
                    (int)irgen.extern_libs[li].len, &nlen);
                if (!nm) continue;
                int resolvable = 0;
                int via_static_archive = 0;
                { char drvso[1300];
                  snprintf(drvso, sizeof(drvso), "%s/%s/lib%.*s.so",
                           oexe, odrop_sub, nlen, nm);
                  if (zan_file_exists(drvso)) {
                      resolvable = 1;
                  } else {
                      int didx = zan_driver_find(&oreg, nm, nlen);
                      if (didx >= 0) {
                          snprintf(drvso, sizeof(drvso),
                                   "%s/%s/drivers/%s/static/lib%.*s.a",
                                   oreg.entries[didx].root,
                                   oreg.entries[didx].module,
                                   odrop_sub, nlen, nm);
                          if (zan_file_exists(drvso)) {
                              resolvable = 1;
                              via_static_archive = 1;
                          } else {
                              snprintf(drvso, sizeof(drvso),
                                       "%s/%s/drivers/%s/lib%.*s.so",
                                       oreg.entries[didx].root,
                                       oreg.entries[didx].module,
                                       odrop_sub, nlen, nm);
                              resolvable = zan_file_exists(drvso);
                          }
                      }
                  }
                }
                if (via_static_archive) {
                    /* 内部辅助逻辑 */
                    zan_irgen_drop_extern_lib(
                        &irgen, irgen.extern_libs[li].str,
                        (int)irgen.extern_libs[li].len);
                } else if (!resolvable) {
                    zan_irgen_stub_extern_lib(
                        &irgen, irgen.extern_libs[li].str,
                        (int)irgen.extern_libs[li].len);
                    zan_irgen_drop_extern_lib(
                        &irgen, irgen.extern_libs[li].str,
                        (int)irgen.extern_libs[li].len);
                }
            }
        }

        /* 内部辅助实现 */
        char cross_dylibs[ZAN_MAX_USED_DRIVERS][1200];
        int cross_dylib_count = 0;
        if (cross_compiling && target.os == ZAN_OS_MACOS) {
            zan_driver_registry_t mac_reg;
            zan_discover_drivers(resolved_stdlib_root, &mac_reg);
            const char *dsub = zan_driver_subdir(&target);
            for (int li = 0; li < irgen.extern_lib_count; li++) {
                int nlen;
                const char *nm = zan_dllimport_lname(
                    irgen.extern_libs[li].str,
                    (int)irgen.extern_libs[li].len, &nlen);
                if (!nm) continue; /* CRT/libc/libm: resolved by libSystem */
                char dylib[1200];
                dylib[0] = '\0';
                int didx = zan_driver_find(&mac_reg, nm, nlen);
                if (didx >= 0) {
                    char driver_dir[1200];
                    snprintf(driver_dir, sizeof(driver_dir),
                             "%s/%s/drivers/%s", mac_reg.entries[didx].root,
                             mac_reg.entries[didx].module, dsub);
                    zan_find_macos_driver_dylib(
                        driver_dir, nm, nlen, dylib, sizeof(dylib));
                }
                if (dylib[0]) {
                    if (cross_dylib_count >= ZAN_MAX_USED_DRIVERS)
                        link_cap_exceeded("driver dylibs", ZAN_MAX_USED_DRIVERS);
                    {
                        snprintf(cross_dylibs[cross_dylib_count],
                                 sizeof(cross_dylibs[0]), "%s", dylib);
                        cross_dylib_count++;
                    }
                } else {
                    int n = zan_irgen_stub_extern_lib(
                        &irgen, irgen.extern_libs[li].str,
                        (int)irgen.extern_libs[li].len);
                    if (n > 0 && !zan_win_system_lib(
                            irgen.extern_libs[li].str,
                            (int)irgen.extern_libs[li].len)) {
                        fprintf(stderr,
                                "warning: no %s driver dylib for "
                                "[DllImport(\"%.*s\")]; its %d function(s) "
                                "are stubbed and will fail at runtime\n",
                                dsub, (int)irgen.extern_libs[li].len,
                                irgen.extern_libs[li].str, n);
                    }
                }
            }
        }

        /* 内部辅助逻辑 */
        if (target.os != ZAN_OS_WINDOWS) {
            for (int li = irgen.extern_lib_count - 1; li >= 0; li--) {
                if (!zan_win_system_lib(irgen.extern_libs[li].str,
                                        (int)irgen.extern_libs[li].len))
                    continue;
                zan_irgen_stub_extern_lib(&irgen, irgen.extern_libs[li].str,
                                          (int)irgen.extern_libs[li].len);
                zan_irgen_drop_extern_lib(&irgen, irgen.extern_libs[li].str,
                                          (int)irgen.extern_libs[li].len);
            }
        }

        /* 核心系统底层抽象与内存语义契约 */
        /* 内部辅助逻辑 */
        /* 内部辅助逻辑 */
        if (cross_compiling && target.os == ZAN_OS_ANDROID) {
            char aexe[1024] = {0};
            zan_exe_dir(aexe, sizeof(aexe));
            const char *asub = (target.arch == ZAN_ARCH_AARCH64)
                               ? "android-arm64" : "android-x64";
            /* 内部辅助逻辑 */
            for (int li = irgen.extern_lib_count - 1; li >= 0; li--) {
                if (zan_win_system_lib(irgen.extern_libs[li].str,
                                       (int)irgen.extern_libs[li].len))
                    continue;
                int nlen;
                const char *nm = zan_dllimport_lname(
                    irgen.extern_libs[li].str,
                    (int)irgen.extern_libs[li].len, &nlen);
                if (!nm) continue;
                int resolvable = 0;
                { char stubso[1200];
                  snprintf(stubso, sizeof(stubso), "%s/%s/lib%.*s.so",
                           aexe, asub, nlen, nm);
                  if (zan_file_exists(stubso)) {
                      resolvable = 1;
                  } else {
                      int didx = zan_driver_find(&driver_reg, nm, nlen);
                      if (didx >= 0) {
                          snprintf(stubso, sizeof(stubso),
                                   "%s/%s/drivers/%s/lib%.*s.so",
                                   driver_reg.entries[didx].root,
                                   driver_reg.entries[didx].module,
                                   zan_driver_subdir(&target), nlen, nm);
                          resolvable = zan_file_exists(stubso);
                          if (!resolvable) {
                              /* 内部辅助逻辑 */
                              snprintf(stubso, sizeof(stubso),
                                       "%s/%s/drivers/%s/static/lib%.*s.a",
                                       driver_reg.entries[didx].root,
                                       driver_reg.entries[didx].module,
                                       zan_driver_subdir(&target), nlen, nm);
                              resolvable = zan_file_exists(stubso);
                          }
                      }
                  }
                }
                if (!resolvable) {
                    int nst = zan_irgen_stub_extern_lib(
                        &irgen, irgen.extern_libs[li].str,
                        (int)irgen.extern_libs[li].len);
                    zan_irgen_drop_extern_lib(
                        &irgen, irgen.extern_libs[li].str,
                        (int)irgen.extern_libs[li].len);
                    if (nst > 0) {
                        fprintf(stderr,
                                "warning: no android driver for "
                                "[DllImport(\"%.*s\")]; its %d function(s) "
                                "are stubbed and will fail at runtime\n",
                                nlen, nm, nst);
                    }
                }
            }
        }
        char driver_dirs[ZAN_MAX_USED_DRIVERS][1024];
        const char *used_drivers[ZAN_MAX_USED_DRIVERS]; int used_driver_count = 0;
        int used_driver_len[ZAN_MAX_USED_DRIVERS];
        const char *used_driver_module[ZAN_MAX_USED_DRIVERS];
        const char *used_driver_root[ZAN_MAX_USED_DRIVERS];
        bool used_driver_runtime[ZAN_MAX_USED_DRIVERS] = { false };  /* 核心系统底层抽象与内存语义契约 */
        bool used_driver_static[ZAN_MAX_USED_DRIVERS] = { false };
        bool used_driver_embedded[ZAN_MAX_USED_DRIVERS] = { false };  /* 核心系统底层抽象与内存语义契约 */
        char embedded_driver_file[ZAN_MAX_USED_DRIVERS][128];
        char static_driver_libs[ZAN_MAX_STATIC_DRV_LIBS][128];
        int static_driver_lib_count = 0;
        for (int li = 0; li < irgen.extern_lib_count; li++) {
            if (used_driver_count >= ZAN_MAX_USED_DRIVERS)
                link_cap_exceeded("native drivers", ZAN_MAX_USED_DRIVERS);
            int nlen;
            const char *nm = zan_dllimport_lname(
                irgen.extern_libs[li].str, (int)irgen.extern_libs[li].len, &nlen);
            int didx = nm ? zan_driver_find(&driver_reg, nm, nlen) : -1;
            if (didx >= 0) {
                used_drivers[used_driver_count] = nm;
                used_driver_len[used_driver_count] = nlen;
                used_driver_module[used_driver_count] = driver_reg.entries[didx].module;
                used_driver_root[used_driver_count] = driver_reg.entries[didx].root;
                used_driver_runtime[used_driver_count] = false;
                used_driver_count++;
            }
        }
        /* 内部辅助逻辑 */
        for (int di = 0; di < driver_reg.count; di++) {
            if (used_driver_count >= ZAN_MAX_USED_DRIVERS)
                link_cap_exceeded("native drivers", ZAN_MAX_USED_DRIVERS);
            const char *sym = driver_reg.entries[di].sym;
            if (!sym[0]) continue;
            if (!zan_driver_prefix_live(&irgen, &driver_prefixes, sym)) continue;
            const char *lib = driver_reg.entries[di].lib;
            bool dup = false;
            for (int u = 0; u < used_driver_count; u++)
                if ((int)strlen(lib) == used_driver_len[u] &&
                    memcmp(lib, used_drivers[u], strlen(lib)) == 0) dup = true;
            if (dup) continue;
            used_drivers[used_driver_count] = lib;
            used_driver_len[used_driver_count] = (int)strlen(lib);
            used_driver_module[used_driver_count] = driver_reg.entries[di].module;
            used_driver_root[used_driver_count] = driver_reg.entries[di].root;
            used_driver_runtime[used_driver_count] = true;
            used_driver_count++;
        }
        {
            const char *dsub = zan_driver_subdir(&target);
            for (int d = 0; d < used_driver_count; d++) {
                driver_dirs[d][0] = '\0';
                if (driver_dir_override) {
                    snprintf(driver_dirs[d], sizeof(driver_dirs[d]), "%s",
                             driver_dir_override);
                } else {
                    const char *mod = used_driver_module[d];
                    const char *droot = used_driver_root[d];
                    if (mod && mod[0] && droot && droot[0]) {
                        snprintf(driver_dirs[d], sizeof(driver_dirs[d]),
                                 "%s/%s/drivers/%s", droot, mod, dsub);
                    }
                }
            }
        }

        /* 内部辅助逻辑 */
        if (target.os == ZAN_OS_WINDOWS && link_subsystem == NULL) {
            bool is_gui_app = (design_count > 0);
            if (!is_gui_app) {
                for (int d = 0; d < used_driver_count; d++) {
                    if (used_driver_len[d] == 7 && memcmp(used_drivers[d], "zan_gui", 7) == 0) {
                        is_gui_app = true;
                        break;
                    }
                }
            }
            if (!is_gui_app && zan_driver_prefix_live(&irgen, &driver_prefixes, "Gui_")) {
                is_gui_app = true;
            }
            if (is_gui_app) {
                link_subsystem = "windows";
            }
        }

        /* 内部辅助实现 */
        char embed_driver_specs[128][1300];
        int embed_driver_spec_count = 0;
        if (target.os == ZAN_OS_WINDOWS && link_static_drivers) {
            for (int d = 0; d < used_driver_count; d++) {
                if (!used_driver_runtime[d] || !driver_dirs[d][0]) continue;
                if (embed_driver_spec_count >= 128)
                    link_cap_exceeded("embedded driver DLLs", 128);
                if (embed_spec_count >= ZAN_MAX_EMBED_SPECS)
                    link_cap_exceeded("--embed resources", ZAN_MAX_EMBED_SPECS);
                char drv[64];
                snprintf(drv, sizeof(drv), "%.*s", used_driver_len[d],
                         used_drivers[d]);
                char file[128], src[1300];
                snprintf(file, sizeof(file), "%s.dll", drv);
                snprintf(src, sizeof(src), "%s/%s", driver_dirs[d], file);
                if (!zan_file_exists(src)) {
                    snprintf(file, sizeof(file), "lib%s.dll", drv);
                    snprintf(src, sizeof(src), "%s/%s", driver_dirs[d], file);
                }
                if (!zan_file_exists(src)) continue;
                if (zan_embed_driver_spec(
                        src, file, embed_driver_specs[embed_driver_spec_count],
                        sizeof(embed_driver_specs[0])) != 0) {
                    fprintf(stderr,
                            "warning: cannot embed driver '%s' from '%s'; it "
                            "will be published beside the executable\n",
                            drv, src);
                    continue;
                }
                embed_specs[embed_spec_count++] =
                    embed_driver_specs[embed_driver_spec_count++];
                used_driver_embedded[d] = true;
                snprintf(embedded_driver_file[d],
                         sizeof(embedded_driver_file[0]), "%s", file);
            }
        }

        /* 内部辅助实现 */
        if (target.os == ZAN_OS_WINDOWS) {
            char win_exe_dir[1024] = {0};
            zan_exe_dir(win_exe_dir, sizeof(win_exe_dir));
            /* 模块核心语义抽象与接口调用契约 */
            for (int li = irgen.extern_lib_count - 1; li >= 0; li--) {
                int nlen;
                const char *nm = zan_dllimport_lname(
                    irgen.extern_libs[li].str,
                    (int)irgen.extern_libs[li].len, &nlen);
                if (!nm || zan_win_system_lib(nm, nlen)) continue;
                /* 内部辅助逻辑 */
                char dir[1200];
                dir[0] = '\0';
                for (int d = 0; d < used_driver_count; d++) {
                    if (used_driver_len[d] == nlen &&
                        memcmp(used_drivers[d], nm, (size_t)nlen) == 0) {
                        snprintf(dir, sizeof(dir), "%s", driver_dirs[d]);
                        break;
                    }
                }
                bool resolvable = false;
                static const char fmts[8][28] = {
                    "%s/static/lib%s.a", "%s/static/%s.lib",
                    "%s/lib%s.a",        "%s/%s.lib",
                    "%s/lib%s.dll.a",    "%s/%s.dll.a",
                    "%s/lib%s.dll",      "%s/%s.dll"
                };
                if (dir[0]) {
                    for (int f = 0; f < 8 && !resolvable; f++) {
                        char cand[1300];
                        snprintf(cand, sizeof(cand), fmts[f], dir, nm);
                        resolvable = zan_file_exists(cand);
                    }
                }
                if (!resolvable && win_exe_dir[0]) {
                    char mlib[1300];
                    snprintf(mlib, sizeof(mlib), "%s\\mingw\\lib\\lib%s.a", win_exe_dir, nm);
                    resolvable = zan_file_exists(mlib);
                    if (!resolvable) {
                        snprintf(mlib, sizeof(mlib), "%s\\mingw\\lib\\lib%s.dll.a", win_exe_dir, nm);
                        resolvable = zan_file_exists(mlib);
                    }
                }
                if (!resolvable) {
                    for (int di = 0; di < extra_lib_path_count && !resolvable; di++) {
                        char cand[1300];
                        snprintf(cand, sizeof(cand), "%s/lib%s.a", extra_lib_paths[di], nm);
                        resolvable = zan_file_exists(cand);
                        if (!resolvable) {
                            snprintf(cand, sizeof(cand), "%s/lib%s.dll.a", extra_lib_paths[di], nm);
                            resolvable = zan_file_exists(cand);
                        }
                    }
                }
                if (resolvable) continue;
                int n = zan_irgen_stub_extern_lib(
                    &irgen, irgen.extern_libs[li].str,
                    (int)irgen.extern_libs[li].len);
                if (n > 0) {
                    /* 模块核心语义抽象与接口调用契约 */
                    char libname[128];
                    snprintf(libname, sizeof(libname), "%.*s",
                             (int)irgen.extern_libs[li].len,
                             irgen.extern_libs[li].str);
                    zan_irgen_drop_extern_lib(
                        &irgen, irgen.extern_libs[li].str,
                        (int)irgen.extern_libs[li].len);
                    fprintf(stderr,
                            "warning: no archive or shared library resolved "
                            "for [DllImport(\"%s\")];"
                            " its %d function(s) are stubbed and will fail at"
                            " run time\n", libname, n);
                }
            }
        }

        /* 内部辅助实现 */
        if (resolved_stdlib_root[0] &&
            zan_driver_prefix_live(&irgen, &driver_prefixes, "IconSvgData_")) {
            char icons_dir[1200];
            if (zan_resolve_gui_resource_dir(resolved_stdlib_root, "Gui/icons",
                                             icons_dir, sizeof(icons_dir))) {
                char *icon_spec = (char *)malloc(strlen(icons_dir) + 32);
                if (icon_spec) {
                    /* 模块核心语义抽象与接口调用契约 */
                    snprintf(icon_spec, strlen(icons_dir) + 32,
                             "%s=icons", icons_dir);
                    if (embed_spec_count < ZAN_MAX_EMBED_SPECS) {
                        embed_specs[embed_spec_count++] = icon_spec;
                    } else {
                        fprintf(stderr, "warning: cannot auto-embed Gui icon "
                                "packs from '%s'; --embed resources limit "
                                "reached\n", icons_dir);
                        free(icon_spec);
                    }
                }
            }
        }

        /* Pinyin: System */
        if (zan_driver_prefix_live(&irgen, &driver_prefixes, "Pinyin_")) {
            char pinyin_path[1200];
            if (zan_resolve_gui_resource_dir(resolved_stdlib_root,
                                            "System/Text/data/pinyin.txt",
                                            pinyin_path, sizeof(pinyin_path))
                ) {
                char *pinyin_spec = (char *)malloc(strlen(pinyin_path) + 32);
                if (pinyin_spec) {
                    /* 底层系统交互与数据协议契约 */
                    snprintf(pinyin_spec, strlen(pinyin_path) + 32,
                             "%s=text/pinyin.txt", pinyin_path);
                    if (embed_spec_count < ZAN_MAX_EMBED_SPECS) {
                        embed_specs[embed_spec_count++] = pinyin_spec;
                    } else {
                        fprintf(stderr, "warning: cannot auto-embed the "
                                "pinyin dictionary from '%s'; --embed "
                                "resources limit reached\n", pinyin_path);
                        free(pinyin_spec);
                    }
                }
            }
        }

        /* 核心系统底层抽象与内存语义契约 */
        if (resolved_stdlib_root[0] &&
            zan_driver_prefix_live(&irgen, &driver_prefixes, "Skin_")) {
            bool skins_staged = false;
            for (int es = 0; es < embed_spec_count && !skins_staged; es++) {
                const char *seq = strrchr(embed_specs[es], '=');
                if (!seq || strcmp(seq + 1, "skins") != 0) { continue; }
                char base_css[1240];
                int dirlen = (int)(seq - embed_specs[es]);
                if (dirlen <= 0 || dirlen > (int)sizeof(base_css) - 16) {
                    continue;
                }
                memcpy(base_css, embed_specs[es], (size_t)dirlen);
                memcpy(base_css + dirlen, "/base.css", 10);
                if (zan_file_exists(base_css)) { skins_staged = true; }
            }
            if (!skins_staged) {
                char skins_dir[1200];
                if (zan_resolve_gui_resource_dir(resolved_stdlib_root,
                                                 "Gui/skins", skins_dir,
                                                 sizeof(skins_dir))) {
                    char *skin_spec = (char *)malloc(strlen(skins_dir) + 32);
                    if (skin_spec) {
                        /* 核心系统底层抽象与内存语义契约 */
                        snprintf(skin_spec, strlen(skins_dir) + 32,
                                 "%s=skins", skins_dir);
                        if (embed_spec_count < ZAN_MAX_EMBED_SPECS) {
                            int spec_at = embed_spec_count++;
                            embed_specs[spec_at] = skin_spec;
                            /* 核心系统底层抽象与内存语义契约 */
                            if (!load_proj_android_keys_done &&
                                project_root_has_manifest()) {
                                load_proj_android_keys();
                                load_proj_android_keys_done = true;
                            }
                            static const char *baseline[2] = { "dark", "light" };
                            const char *keep[18];
                            int keep_count = 0;
                            for (int b = 0; b < 2; b++) keep[keep_count++] = baseline[b];
                            if (proj_skins_enabled && proj_skin_name_count > 0) {
                                for (int k = 0; k < proj_skin_name_count
                                        && keep_count < 18; k++) {
                                    bool dup = false;
                                    for (int j = 0; j < keep_count; j++) {
                                        if (strcmp(keep[j],
                                                   proj_skin_names[k]) == 0) {
                                            dup = true; break;
                                        }
                                    }
                                    if (!dup) keep[keep_count++] = proj_skin_names[k];
                                }
                            }
                            skin_filter = (const char **)malloc(
                                (size_t)keep_count * sizeof(char *));
                            if (skin_filter) {
                                memcpy(skin_filter, keep,
                                       (size_t)keep_count * sizeof(char *));
                                skin_filter_count = keep_count;
                            }
                        } else {
                            fprintf(stderr, "warning: cannot auto-embed Gui "
                                    "skin packs from '%s'; --embed resources "
                                    "limit reached\n", skins_dir);
                            free(skin_spec);
                        }
                    }
                }
            }
        }

        /* 核心系统底层抽象与内存语义契约 */
        if (resolved_stdlib_root[0] &&
            zan_driver_prefix_live(&irgen, &driver_prefixes, "Chart_")) {
            bool chartthemes_staged = false;
            for (int es = 0; es < embed_spec_count && !chartthemes_staged;
                 es++) {
                const char *seq = strrchr(embed_specs[es], '=');
                if (seq && strcmp(seq + 1, "chartthemes") == 0)
                    chartthemes_staged = true;
            }
            if (!chartthemes_staged) {
                char themes_dir[1200];
                snprintf(themes_dir, sizeof(themes_dir),
                         "%s/Gui/Component/Chart/themes",
                         resolved_stdlib_root);
                if (zan_file_exists(themes_dir)) {
                    char *theme_spec = (char *)malloc(strlen(themes_dir) + 32);
                    if (theme_spec) {
                        snprintf(theme_spec, strlen(themes_dir) + 32,
                                 "%s=chartthemes", themes_dir);
                        if (embed_spec_count < ZAN_MAX_EMBED_SPECS) {
                            embed_specs[embed_spec_count++] = theme_spec;
                        } else {
                            fprintf(stderr, "warning: cannot auto-embed chart "
                                    "theme packs from '%s'; --embed resources "
                                    "limit reached\n", themes_dir);
                            free(theme_spec);
                        }
                    }
                }
            }
        }

        /* 内部辅助实现 */
        if (irgen.uses_embed_api &&
            resolved_stdlib_root[0]) {
            /* 内部辅助实现 */
            char assets_cands[4][1200];
            int assets_cand_count = 0;
            for (int fi = 0; fi < input_count && assets_cand_count < 2; fi++) {
                char assets_dir[1400];
                snprintf(assets_dir, sizeof(assets_dir), "%s",
                         input_files[fi]);
                char *asep = strrchr(assets_dir, '/');
                char *aback = strrchr(assets_dir, '\\');
                if (!asep || (aback && aback > asep)) asep = aback;
                if (!asep) continue;
                *asep = 0;
                snprintf(assets_cands[assets_cand_count], 1200, "%s/assets",
                         assets_dir);
                if (zan_file_exists(assets_cands[assets_cand_count]))
                    assets_cand_count++;
                else if (assets_cand_count < 3) {
                    /* 模块核心语义抽象与接口调用契约 */
                    char *psep = strrchr(assets_dir, '/');
                    char *pback = strrchr(assets_dir, '\\');
                    if (!psep || (pback && pback > psep)) psep = pback;
                    if (psep && psep != assets_dir) {
                        *psep = 0;
                        snprintf(assets_cands[assets_cand_count], 1200,
                                 "%s/assets", assets_dir);
                        if (zan_file_exists(assets_cands[assets_cand_count]))
                            assets_cand_count++;
                    }
                }
            }
            if (assets_cand_count < 2 &&
                strcmp(package_project_root, ".") != 0) {
                snprintf(assets_cands[assets_cand_count], 1200, "%s/assets",
                         package_project_root);
                assets_cand_count++;
            }
            bool assets_staged = false;
            for (int es = 0; es < embed_spec_count && !assets_staged; es++) {
                const char *seq = strrchr(embed_specs[es], '=');
                if (seq && strcmp(seq + 1, "assets") == 0) assets_staged = true;
            }
            for (int ac = 0; ac < assets_cand_count && !assets_staged; ac++) {
                const char *adir = assets_cands[ac];
                if (!zan_file_exists(adir)) { continue; }
                assets_staged = true;
                char *assets_spec = (char *)malloc(strlen(adir) + 32);
                if (assets_spec) {
                    /* 模块核心语义抽象与接口调用契约 */
                    snprintf(assets_spec, strlen(adir) + 32, "%s=assets", adir);
                    if (embed_spec_count < ZAN_MAX_EMBED_SPECS) {
                        embed_specs[embed_spec_count++] = assets_spec;
                    } else {
                        fprintf(stderr, "warning: cannot auto-embed the "
                                "project assets from '%s'; --embed resources "
                                "limit reached\n", adir);
                        assets_staged = false;
                        free(assets_spec);
                    }
                }
            }
        }

        /* 内部辅助逻辑 */
        if (embed_spec_count > 0 || irgen.uses_embed_api) {
            int nres = zan_embed_emit_specs_filtered(&irgen, embed_specs,
                embed_spec_count, skin_filter, skin_filter_count);
            if (nres < 0) {
                zan_diag_free_buffers(irgen.diag);
                zan_irgen_destroy(&irgen);
                zan_arena_free(ir_arena);
                zan_arena_free(arena);
                free(source);
                return 1;
            }
        }

        char obj_tmp[1024];
        generated_object_vec_t generated_objects = {0};
        snprintf(obj_tmp, sizeof(obj_tmp), "%s.o", obj_path);

        if (shard_n > 0) {
            for (int i = 0; i < shard_n; i++) {
                generated_object_vec_add(&generated_objects, shard_objs[i]);
                free(shard_objs[i]);
            }
            free(shard_objs);
            shard_objs = NULL;
            shard_n = 0;
        }

        phase("emit obj");
        probe_phase_mem("emit obj");
        if (zan_irgen_write_obj(&irgen, obj_tmp) != ZAN_OK) {
            fprintf(stderr, "error: failed to emit object file\n");
            zan_diag_free_buffers(irgen.diag);
            zan_irgen_destroy(&irgen);
            zan_arena_free(ir_arena);
            zan_arena_free(arena);
            free(source);
            return 1;
        }

        generated_object_vec_add(&generated_objects, obj_tmp);
        phase("write obj");
        probe_phase_mem("write obj");

        /* 内部辅助逻辑 */
        zan_irgen_release_llvm(&irgen);
        probe_phase_mem("free llvm");

        /* 底层系统交互与数据协议契约 */
        char icon_obj[1100];
        icon_obj[0] = '\0';
        if (!no_icon && !emit_lib && target.os == ZAN_OS_WINDOWS) {
            int arm64 = target.arch == ZAN_ARCH_AARCH64;
            snprintf(icon_obj, sizeof(icon_obj), "%s.icon.o", obj_path);
            int ires;
            if (icon_path) {
                ires = zan_winres_icon_object(icon_path, icon_obj, arm64);
                if (ires != 0)
                    fprintf(stderr, "warning: cannot embed icon '%s'; "
                                    "the executable gets the built-in one\n",
                            icon_path);
            } else {
                ires = 1;
            }
            /* 内部辅助逻辑 */
            if (ires != 0)
                ires = zan_winres_icon_object_mem(zan_default_icon,
                                                  ZAN_DEFAULT_ICON_LEN,
                                                  icon_obj, arm64);
            if (ires != 0) {
                icon_obj[0] = '\0';
            } else if (extra_link_input_count < ZAN_MAX_LINK_INPUTS) {
                extra_link_inputs[extra_link_input_count++] = icon_obj;
            } else {
                link_cap_exceeded("link inputs", ZAN_MAX_LINK_INPUTS);
            }
        }

        int link_ret;

        /* 内部辅助逻辑 */
        char link_exe_dir[1024];
        zan_exe_dir(link_exe_dir, sizeof(link_exe_dir));
        char rt_io_buf[1200];
        char rt_sync_buf[1200];
        char rt_file_buf[1200];
        char rt_embed_buf[1200];
        char rt_inflate_buf[1200];
        char rt_timer_buf[1200];
        char rt_mem_buf[1200];

        /* 内部辅助逻辑 */
        const char *rt_io_obj = NULL;
#ifdef ZAN_RT_IO_OBJ
        if (irgen.uses_socket_async) {
            snprintf(rt_io_buf, sizeof(rt_io_buf), "%s/%s",
                     link_exe_dir, zan_path_basename(ZAN_RT_IO_OBJ));
            rt_io_obj = rt_io_buf;
        }
#endif
        /* 内部辅助逻辑 */
#ifdef ZAN_RT_IO_MT_OBJ
        if (external_async_executor) {
            snprintf(rt_io_buf, sizeof(rt_io_buf), "%s/%s",
                     link_exe_dir, zan_path_basename(ZAN_RT_IO_MT_OBJ));
            rt_io_obj = rt_io_buf;
        }
#endif
        const char *rt_sync_obj = NULL;
#ifdef ZAN_RT_SYNC_OBJ
        if (irgen.uses_sync_runtime) {
            snprintf(rt_sync_buf, sizeof(rt_sync_buf), "%s/%s",
                     link_exe_dir, zan_path_basename(ZAN_RT_SYNC_OBJ));
            rt_sync_obj = rt_sync_buf;
        }
#endif
        /* 内部辅助逻辑 */
        const char *rt_file_obj = NULL;
#ifdef ZAN_RT_FILE_OBJ
        if (irgen.uses_file_runtime) {
            snprintf(rt_file_buf, sizeof(rt_file_buf), "%s/%s",
                     link_exe_dir, zan_path_basename(ZAN_RT_FILE_OBJ));
            rt_file_obj = rt_file_buf;
        }
#endif
        /* 内部辅助实现 */
        const char *rt_embed_obj = NULL;
#ifdef ZAN_EMBED_OBJ
        if (irgen.uses_embed_api) {
            snprintf(rt_embed_buf, sizeof(rt_embed_buf), "%s/%s",
                     link_exe_dir, zan_path_basename(ZAN_EMBED_OBJ));
            rt_embed_obj = rt_embed_buf;
        }
#endif
        /* 内部辅助逻辑 */
        const char *rt_inflate_obj = NULL;
#ifdef ZAN_INFLATE_OBJ
        if (irgen.uses_inflate) {
            snprintf(rt_inflate_buf, sizeof(rt_inflate_buf), "%s/%s",
                     link_exe_dir, zan_path_basename(ZAN_INFLATE_OBJ));
            rt_inflate_obj = rt_inflate_buf;
        }
#endif
        /* 内部辅助逻辑 */
        const char *rt_timer_obj = NULL;
#ifdef ZAN_RT_TIMER_OBJ
        if (!cross_compiling) {
            snprintf(rt_timer_buf, sizeof(rt_timer_buf), "%s/%s",
                     link_exe_dir, zan_path_basename(ZAN_RT_TIMER_OBJ));
            rt_timer_obj = rt_timer_buf;
        }
#endif
        /* 小对象内存分配器管理池 */
        bool fast_alloc = fast_alloc_opt >= 0;
        const char *rt_mem_obj = NULL;
#ifdef ZAN_RT_MEM_OBJ
        if (fast_alloc && !cross_compiling &&
            target.os != ZAN_OS_MACOS && target.os != ZAN_OS_IOS) {
            snprintf(rt_mem_buf, sizeof(rt_mem_buf), "%s/%s",
                     link_exe_dir, zan_path_basename(ZAN_RT_MEM_OBJ));
            if (zan_file_exists(rt_mem_buf)) rt_mem_obj = rt_mem_buf;
        }
#endif
        if (fast_alloc_opt == 1 && !rt_mem_obj) {
            fprintf(stderr,
                    "warning: --fast-alloc ignored: no allocator object for "
                    "this target\n");
        }

        /* 内部辅助逻辑 */
        const char *target_rt_sub = NULL;
        if (cross_compiling) {
            /* 内部辅助逻辑 */
            const char *tsub = NULL;
            if (target.os == ZAN_OS_LINUX) {
                tsub = (target.arch == ZAN_ARCH_AARCH64) ? "linux-arm64"
                     : (target.arch == ZAN_ARCH_RISCV64) ? "linux-riscv64"
                     : "linux-musl";
            } else if (target.os == ZAN_OS_ANDROID) {
                tsub = (target.arch == ZAN_ARCH_AARCH64) ? "android-arm64"
                                                         : "android-x64";
            } else if (target.os == ZAN_OS_OHOS) {
                tsub = (target.arch == ZAN_ARCH_AARCH64) ? "ohos-arm64"
                                                         : "ohos-x64";
            } else if (target.os == ZAN_OS_WINDOWS) {
                tsub = (target.arch == ZAN_ARCH_AARCH64) ? "win-arm64" : "win-x64";
            } else if (target.os == ZAN_OS_MACOS) {
                tsub = (target.arch == ZAN_ARCH_AARCH64) ? "macos/arm64"
                                                         : "macos/x64";
            } else if (target.os == ZAN_OS_IOS) {
                tsub = "ios/arm64";
            }
            if (tsub) {
                snprintf(rt_timer_buf, sizeof(rt_timer_buf),
                         "%s/%s/zanrt_timer.o", link_exe_dir, tsub);
                if (!zan_file_exists(rt_timer_buf)) {
                    fprintf(stderr,
                            "error: bundled %s timer runtime not found at '%s'; "
                            "reinstall zan or rebuild with toolchain/%s present "
                            "(see scripts/build_*_rt.sh)\n",
                            tsub, rt_timer_buf, tsub);
                    generated_object_vec_remove(&generated_objects, obj_tmp);
                    zan_diag_free_buffers(irgen.diag);
                    zan_irgen_destroy(&irgen);
                    zan_arena_free(ir_arena);
                    zan_arena_free(arena);
                    free(source);
                    return 1;
                }
                rt_timer_obj = rt_timer_buf;
                target_rt_sub = tsub;
            }
        }
        if (cross_compiling && rt_sync_obj && target.os != ZAN_OS_LINUX
            && target.os != ZAN_OS_ANDROID && target.os != ZAN_OS_OHOS
            && target.os != ZAN_OS_MACOS && target.os != ZAN_OS_WINDOWS) {
            /* 内部辅助实现 */
            int needs_sync = 1;   /* 核心系统底层抽象与内存语义契约 */
            if (target.os == ZAN_OS_WASI) {
                static const char *const disp_pre[] = {
                    "zan_dispatch_", NULL
                };
                static const char *const other_sync_pre[] = {
                    "zan_atomic_int_", "zan_shared_table_",
                    "zan_thread_", "zan_monitor_",
                    "zan_monotonic_", "zan_plat_",
                    "zan_exe_dir_into", "zan_dir_list_into",
                    NULL
                };
                static const char *const gui_pre[] = { "zan_gui_", NULL };
                /* 内部辅助实现 */
                int has_disp = wasm_obj_vec_refs_any(&generated_objects, disp_pre);
                int has_other = wasm_obj_vec_refs_any(&generated_objects, other_sync_pre);
                needs_sync = (has_other || has_disp)
                    && !wasm_obj_vec_refs_any(&generated_objects, gui_pre);
            }
            if (needs_sync) {
                fprintf(stderr,
                        "error: AtomicInt and SharedTable are not available for "
                        "this cross-compilation target yet\n");
                generated_object_vec_remove(&generated_objects, obj_tmp);
                zan_diag_free_buffers(irgen.diag);
                zan_irgen_destroy(&irgen);
                zan_arena_free(ir_arena);
                zan_arena_free(arena);
                free(source);
                return 1;
            }
        }

        /* 内部辅助逻辑 */
        char zan_lib_dirs[ZAN_LINK_MAX_DIRS][512]; int zan_lib_ndirs = 0;
        {
            const char *lp = getenv("ZAN_LIB_PATH");
            if (lp && *lp) {
#ifdef _WIN32
                const char sep = ';';
#else
                const char sep = ':';
#endif
                const char *p = lp;
                while (*p && zan_lib_ndirs < ZAN_LINK_MAX_DIRS) {
                    const char *e = strchr(p, sep);
                    size_t n = e ? (size_t)(e - p) : strlen(p);
                    if (n > 0 && n < sizeof(zan_lib_dirs[0])) {
                        memcpy(zan_lib_dirs[zan_lib_ndirs], p, n);
                        zan_lib_dirs[zan_lib_ndirs][n] = '\0';
                        zan_lib_ndirs++;
                    }
                    if (!e) break;
                    p = e + 1;
                }
            }
        }

#ifdef __APPLE__
        /* 编译期中间表示与代码生成内部规范 */
        {
            char home_lib[512]; home_lib[0] = '\0';
            char home_pq[512];  home_pq[0] = '\0';
            const char *home = getenv("HOME");
            if (home && *home) {
                snprintf(home_lib, sizeof(home_lib), "%s/.homebrew/lib", home);
                snprintf(home_pq, sizeof(home_pq),
                         "%s/.homebrew/opt/libpq/lib", home);
            }
            const char *mac_lib_dirs[] = {
                home_lib[0] ? home_lib : NULL,
                "/opt/homebrew/lib",
                "/usr/local/lib",
                home_pq[0] ? home_pq : NULL,
                "/opt/homebrew/opt/libpq/lib",
                "/usr/local/opt/libpq/lib",
                NULL,
            };
            for (int ci = 0; mac_lib_dirs[ci]; ci++) {
                if (zan_lib_ndirs >= 16) break;
                if (!mac_lib_dirs[ci][0]) continue;
                if (access(mac_lib_dirs[ci], F_OK) != 0) continue;
                int dup = 0;
                for (int dj = 0; dj < zan_lib_ndirs; dj++)
                    if (strcmp(zan_lib_dirs[dj], mac_lib_dirs[ci]) == 0) { dup = 1; break; }
                if (dup) continue;
                snprintf(zan_lib_dirs[zan_lib_ndirs++], sizeof(zan_lib_dirs[0]),
                         "%s", mac_lib_dirs[ci]);
            }
        }
#endif

        /* 内部辅助逻辑 */
        for (int d = 0; d < used_driver_count; d++) {
            if (!driver_dirs[d][0]) continue;
            /* 编译期中间表示与代码生成内部规范 */
            char linkdir[1100];
            bool added_shared = false;
            bool want_static = link_static_drivers &&
                !used_driver_runtime[d] && cross_dylib_count == 0;
            char archive[1200];
            archive[0] = '\0';
            if (want_static) {
                snprintf(archive, sizeof(archive), "%s/static/lib%.*s.a",
                         driver_dirs[d], used_driver_len[d],
                         used_drivers[d]);
            }
            if (want_static && zan_file_exists(archive)) {
                used_driver_static[d] = true;
                snprintf(linkdir, sizeof(linkdir), "%s/static", driver_dirs[d]);
                char libs_manifest[1200];
                snprintf(libs_manifest, sizeof(libs_manifest),
                         "%s/static/%.*s.libs", driver_dirs[d],
                         used_driver_len[d], used_drivers[d]);
                if (static_driver_lib_count >= ZAN_MAX_STATIC_DRV_LIBS)
                    link_cap_exceeded("static driver libraries",
                                      ZAN_MAX_STATIC_DRV_LIBS);
                static_driver_lib_count += zan_read_static_libs(
                    libs_manifest,
                    &static_driver_libs[static_driver_lib_count],
                    ZAN_MAX_STATIC_DRV_LIBS - static_driver_lib_count, target.os);
            } else {
                if (want_static) {
                    fprintf(stderr,
                            "note: no static archive for driver '%.*s' "
                            "(expected '%s'); falling back to the shared "
                            "driver, which will still be published beside "
                            "the executable\n",
                            used_driver_len[d], used_drivers[d], archive);
                }
                snprintf(linkdir, sizeof(linkdir), "%s", driver_dirs[d]);
            }
            if (zan_lib_ndirs < ZAN_LINK_MAX_DIRS && strlen(linkdir) < sizeof(zan_lib_dirs[0])) {
                /* 内部辅助实现 */
                memmove(&zan_lib_dirs[1], &zan_lib_dirs[0],
                        (size_t)zan_lib_ndirs * sizeof(zan_lib_dirs[0]));
                snprintf(zan_lib_dirs[0], sizeof(zan_lib_dirs[0]), "%s", linkdir);
                zan_lib_ndirs++;
                added_shared = true;
            }
            /* 内部辅助实现 */
            if (added_shared && !link_static_drivers) {
                char statdir[1100];
                snprintf(statdir, sizeof(statdir), "%s/static", driver_dirs[d]);
                if (zan_lib_ndirs < ZAN_LINK_MAX_DIRS && zan_file_exists(statdir) &&
                    strlen(statdir) < sizeof(zan_lib_dirs[0])) {
                    memmove(&zan_lib_dirs[2], &zan_lib_dirs[1],
                            (size_t)(zan_lib_ndirs - 1) * sizeof(zan_lib_dirs[0]));
                    snprintf(zan_lib_dirs[1], sizeof(zan_lib_dirs[1]), "%s", statdir);
                    zan_lib_ndirs++;
                }
            }
        }

        /* 内部辅助实现 */
        if (link_static_drivers && resolved_stdlib_root[0]) {
            const char *dsub = zan_driver_subdir(&target);
            for (int li = 0; li < static_driver_lib_count; li++) {
                const char *entry = static_driver_libs[li];
                if (entry[0] != '-' || entry[1] != 'l' ||
                    !entry[2] || !zan_is_safe_bundle_name(entry + 2))
                    continue;
                bool resolved = false;
                for (int di = 0; di < zan_lib_ndirs && !resolved; di++) {
                    char candidate[1200];
                    int n = snprintf(candidate, sizeof(candidate),
                                     "%s/lib%s.a", zan_lib_dirs[di], entry + 2);
                    if (n >= 0 && (size_t)n < sizeof(candidate) &&
                        zan_file_exists(candidate)) {
                        resolved = true;
                        continue;
                    }
#ifdef _WIN32
                    n = snprintf(candidate, sizeof(candidate),
                                 "%s/%s.lib", zan_lib_dirs[di], entry + 2);
                    if (n >= 0 && (size_t)n < sizeof(candidate) &&
                        zan_file_exists(candidate))
                        resolved = true;
#endif
                }
                if (resolved) continue;
                char dependency_dir[1200];
                if (zan_find_static_library(resolved_stdlib_root, dsub,
                                            entry + 2, dependency_dir,
                                            sizeof(dependency_dir))) {
                    bool duplicate = false;
                    for (int di = 0; di < zan_lib_ndirs; di++)
                        if (strcmp(zan_lib_dirs[di], dependency_dir) == 0) {
                            duplicate = true;
                            break;
                        }
                    if (duplicate) continue;
                    if (zan_lib_ndirs >= 16) {
                        fprintf(stderr,
                                "warning: static-driver dependency search "
                                "path limit reached; ignoring '%s' for '%s'\n",
                                dependency_dir, entry);
                        continue;
                    }
                    memmove(&zan_lib_dirs[1], &zan_lib_dirs[0],
                            (size_t)zan_lib_ndirs * sizeof(zan_lib_dirs[0]));
                    snprintf(zan_lib_dirs[0], sizeof(zan_lib_dirs[0]), "%s",
                             dependency_dir);
                    zan_lib_ndirs++;
                    continue;
                }
#ifdef _WIN32
                if (zan_win_system_lib(entry + 2, (int)strlen(entry + 2)))
                    continue;
#endif
                fprintf(stderr,
                        "warning: static-driver library '%s' could not be "
                        "resolved for target '%s'\n", entry, dsub);
            }
        }

        if (emit_lib) {
            /* 内部辅助逻辑 */
            /* 内部辅助逻辑 */
            if (!lib_shared) {
                if (zan_write_static_lib(obj_tmp, obj_path, target.os) != 0) {
                    fprintf(stderr, "error: failed to write static library '%s'\n",
                            obj_path);
                    generated_object_vec_remove(&generated_objects, obj_tmp);
                    zan_diag_free_buffers(irgen.diag);
                    zan_irgen_destroy(&irgen);
                    zan_arena_free(ir_arena);
                    zan_arena_free(arena);
                    free(source);
                    return 1;
                }
                link_ret = 0;
            } else {
                if (cross_compiling
                    && (rt_io_obj || rt_sync_obj || rt_file_obj
                        || rt_embed_obj)) {
                    if (!target_rt_sub) {
                        fprintf(stderr,
                                "error: no bundled runtime objects for this "
                                "cross-compilation target; link a static "
                                "library instead\n");
                        generated_object_vec_remove(&generated_objects, obj_tmp);
                        zan_diag_free_buffers(irgen.diag);
                        zan_irgen_destroy(&irgen);
                        zan_arena_free(ir_arena);
                        zan_arena_free(arena);
                        free(source);
                        return 1;
                    }
                    const char *missing = NULL;
                    if (rt_io_obj) {
                        snprintf(rt_io_buf, sizeof(rt_io_buf), "%s/%s/%s",
                                 link_exe_dir, target_rt_sub,
                                 external_async_executor ? "zanrt_io_mt.o" : "zanrt_io.o");
                        rt_io_obj = rt_io_buf;
                        if (!zan_file_exists(rt_io_obj)) missing = rt_io_obj;
                    }
                    if (rt_sync_obj) {
                        snprintf(rt_sync_buf, sizeof(rt_sync_buf),
                                 "%s/%s/zanrt_sync.o", link_exe_dir,
                                 target_rt_sub);
                        rt_sync_obj = rt_sync_buf;
                        if (!zan_file_exists(rt_sync_obj)) missing = rt_sync_obj;
                    }
                    if (rt_file_obj) {
                        snprintf(rt_file_buf, sizeof(rt_file_buf),
                                 "%s/%s/zanrt_file.o", link_exe_dir,
                                 target_rt_sub);
                        rt_file_obj = rt_file_buf;
                        if (!zan_file_exists(rt_file_obj)) missing = rt_file_obj;
                    }
                    if (rt_embed_obj) {
                        snprintf(rt_embed_buf, sizeof(rt_embed_buf),
                                 "%s/%s/zan_embed_api.o", link_exe_dir,
                                 target_rt_sub);
                        rt_embed_obj = rt_embed_buf;
                        if (!zan_file_exists(rt_embed_obj)) missing = rt_embed_obj;
                    }
                    if (rt_inflate_obj) {
                        snprintf(rt_inflate_buf, sizeof(rt_inflate_buf),
                                 "%s/%s/zan_inflate.o", link_exe_dir,
                                 target_rt_sub);
                        rt_inflate_obj = rt_inflate_buf;
                        if (!zan_file_exists(rt_inflate_obj)) missing = rt_inflate_obj;
                    }
                    if (missing) {
                        fprintf(stderr,
                                "error: bundled %s runtime object not found at "
                                "'%s'; reinstall zan or rebuild with "
                                "toolchain/%s present (see "
                                "scripts/build_*_rt.sh)\n",
                                target_rt_sub, missing, target_rt_sub);
                        generated_object_vec_remove(&generated_objects, obj_tmp);
                        zan_diag_free_buffers(irgen.diag);
                        zan_irgen_destroy(&irgen);
                        zan_arena_free(ir_arena);
                        zan_arena_free(arena);
                        free(source);
                        return 1;
                    }
                }
                char cmd[8192];
                bool lib_spawned = false; /* 核心系统底层抽象与内存语义契约 */
                char exe_dir[1024];
                zan_exe_dir(exe_dir, sizeof(exe_dir));
                /* 内部辅助逻辑 */
                char implib[1100] = {0};
                if (target.os == ZAN_OS_WINDOWS) {
                    const char *sep = strrchr(obj_path, '\\');
                    if (!sep) sep = strrchr(obj_path, '/');
                    const char *base = sep ? sep + 1 : obj_path;
                    int llen;
                    const char *lname = zan_dllimport_lname(
                        base, (int)strlen(base), &llen);
                    if (!lname) { lname = base; llen = (int)strlen(base); }
                    size_t plen = (size_t)(base - obj_path);
                    snprintf(implib, sizeof(implib), "%.*slib%.*s.a",
                             (int)plen, obj_path, llen, lname);
                }
                if (target.os == ZAN_OS_WINDOWS) {
#ifdef _WIN32
                    /* 模块核心语义抽象与接口调用契约 */
                    char dll_exe_dir[1024]; dll_exe_dir[0] = '\0';
                    GetModuleFileNameA(NULL, dll_exe_dir, sizeof(dll_exe_dir));
                    { char *s = strrchr(dll_exe_dir, '\\'); if (s) *s = '\0'; }
                    char ld_path[1200];
                    snprintf(ld_path, sizeof(ld_path), "%s\\ld.exe", dll_exe_dir);
                    const char *dll_ld = ld_path;
                    if (!zan_file_exists(ld_path)) {
                        dll_ld = "ld.lld";
                    }
                    const char *argv[ZAN_LINK_MAX_ARGV];
                    int a = 0;
                    argv[a++] = dll_ld;
                    argv[a++] = "-m";
                    argv[a++] = (target.arch == ZAN_ARCH_AARCH64)
                                    ? "arm64pe" : "i386pep";
                    argv[a++] = "-shared";
                    argv[a++] = "-Bdynamic";
                    /* 底层系统交互与数据协议契约 */
                    char dllcrt2[1300];
                    snprintf(dllcrt2, sizeof(dllcrt2),
                             "%s\\mingw\\lib\\dllcrt2.o", dll_exe_dir);
                    const char *dll_entry =
                        zan_file_exists(dllcrt2) ? "DllMainCRTStartup"
                                                 : "DllMain";
                    argv[a++] = "-e";
                    argv[a++] = dll_entry;
                    argv[a++] = "-o";
                    argv[a++] = obj_path;
                    argv[a++] = obj_tmp;
                    if (zan_file_exists(dllcrt2))
                        argv[a++] = dllcrt2;
                    /* 内部辅助逻辑 */
                    if (rt_io_obj) argv[a++] = rt_io_obj;
                    if (rt_sync_obj) argv[a++] = rt_sync_obj;
                    if (rt_file_obj) argv[a++] = rt_file_obj;
                    if (rt_embed_obj) argv[a++] = rt_embed_obj;
                    if (rt_inflate_obj) argv[a++] = rt_inflate_obj;
                    if (rt_timer_obj) argv[a++] = rt_timer_obj;
                    argv[a++] = "-out-implib";
                    argv[a++] = implib;
                    char ldirbufs[ZAN_LINK_MAX_DIRS * 2 + 4][520]; int nld = 0;
                    if (dll_exe_dir[0] &&
                        zan_file_exists(dll_exe_dir + 0) /* 核心系统底层抽象与内存语义契约 */) {
                        /* 模块核心语义抽象与接口调用契约 */
                        char mlib[1300];
                        snprintf(mlib, sizeof(mlib), "%s\\mingw\\lib",
                                 dll_exe_dir);
                        if (zan_file_exists(mlib)) {
                            snprintf(ldirbufs[nld], sizeof(ldirbufs[0]),
                                     "-L%s", mlib);
                            argv[a++] = ldirbufs[nld++];
                        }
                    }
                    for (int di = 0; di < zan_lib_ndirs; di++) {
                        if (a >= ZAN_LINK_MAX_ARGV - ZAN_LINK_ARGV_TAIL)
                            link_cap_exceeded("linker arguments",
                                              ZAN_LINK_MAX_ARGV);
                        snprintf(ldirbufs[nld], sizeof(ldirbufs[0]),
                                 "-L%s", zan_lib_dirs[di]);
                        argv[a++] = ldirbufs[nld++];
                    }
                    for (int di = 0; di < extra_lib_path_count; di++) {
                        if (a >= ZAN_LINK_MAX_ARGV - ZAN_LINK_ARGV_TAIL)
                            link_cap_exceeded("linker arguments",
                                              ZAN_LINK_MAX_ARGV);
                        snprintf(ldirbufs[nld], sizeof(ldirbufs[0]),
                                 "-L%s", extra_lib_paths[di]);
                        argv[a++] = ldirbufs[nld++];
                    }
                    /* 内部辅助逻辑 */
                    argv[a++] = "--start-group";
                    /* 模块核心语义抽象与接口调用契约 */
                    static const char *const dllcrt[] = {
                        "-lmingw32", "-lgcc", "-lmoldname", "-lmingwex",
                        "-lmsvcrt", "-lkernel32", "-lshell32",
                        "-l:libwinpthread.a", NULL };
                    for (int li = 0; dllcrt[li]; li++) {
                        if (a >= ZAN_LINK_MAX_ARGV - ZAN_LINK_ARGV_TAIL)
                            link_cap_exceeded("linker arguments", ZAN_LINK_MAX_ARGV);
                        argv[a++] = dllcrt[li];
                    }
                    /* 核心系统底层抽象与内存语义契约 */
                    if (rt_io_obj) argv[a++] = "-lws2_32";
                    char libbufs[ZAN_LINK_MAX_LIBS][128]; int nb = 0;
                    for (int li = 0; li < irgen.extern_lib_count; li++) {
                        if (nb >= ZAN_LINK_MAX_LIBS ||
                            a >= ZAN_LINK_MAX_ARGV - ZAN_LINK_ARGV_TAIL)
                            link_cap_exceeded("DllImport libraries", ZAN_LINK_MAX_LIBS);
                        int nlen;
                        const char *nm = zan_dllimport_lname(
                            irgen.extern_libs[li].str,
                            (int)irgen.extern_libs[li].len, &nlen);
                        if (!nm) continue;
                        snprintf(libbufs[nb], sizeof(libbufs[0]),
                                 "-l%.*s", nlen, nm);
                        argv[a++] = libbufs[nb++];
                    }
                    for (int li = 0; li < static_driver_lib_count; li++) {
                        if (a >= ZAN_LINK_MAX_ARGV - ZAN_LINK_ARGV_TAIL)
                            link_cap_exceeded("linker arguments", ZAN_LINK_MAX_ARGV);
                        argv[a++] = static_driver_libs[li];
                    }
                    argv[a++] = "--end-group";
                    argv[a] = NULL;
                    if (getenv("ZAN_VERBOSE_LINK")) {
                        fprintf(stderr, "[link]");
                        for (int ai = 0; ai < a; ai++)
                            fprintf(stderr, " %s", argv[ai]);
                        fprintf(stderr, "\n");
                    }
                    link_ret = (int)zan_utf8_spawnv(_P_WAIT, dll_ld, argv);
                    lib_spawned = true;
#else
                    /* 内部辅助逻辑 */
                    const char *wsub = (target.arch == ZAN_ARCH_AARCH64)
                                       ? "arm64pe" : "i386pep";
                    snprintf(cmd, sizeof(cmd),
                             "ld.lld -m %s -shared -e DllMain -o \"%s\" \"%s\""
                             " -out-implib \"%s\"",
                             wsub, obj_path, obj_tmp, implib);
                    { cmd_appendf(cmd, sizeof(cmd),
                               " -lmingw32 -lmoldname -lmingwex -lmsvcrt"
                               " -lkernel32 -lshell32"); }
                    for (int di = 0; di < zan_lib_ndirs; di++) {
                        cmd_appendf(cmd, sizeof(cmd), " -L\"%s\"",
                                 zan_lib_dirs[di]);
                    }
                    for (int di = 0; di < extra_lib_path_count; di++) {
                        cmd_appendf(cmd, sizeof(cmd), " -L\"%s\"",
                                 extra_lib_paths[di]);
                    }
                    for (int li = 0; li < irgen.extern_lib_count; li++) {
                        int nlen;
                        const char *nm = zan_dllimport_lname(
                            irgen.extern_libs[li].str,
                            (int)irgen.extern_libs[li].len, &nlen);
                        if (!nm) continue;
                        if (target.os == ZAN_OS_MACOS || target.os == ZAN_OS_IOS) {
                            for (int d = 0; d < used_driver_count; d++) {
                                if (used_driver_runtime[d] ||
                                    used_driver_static[d] ||
                                    used_driver_len[d] != nlen ||
                                    memcmp(used_drivers[d], nm,
                                           (size_t)nlen) != 0)
                                    continue;
                                char primary[1200], fallback[1200];
                                snprintf(primary, sizeof(primary),
                                         "%s/lib%.*s.dylib", driver_dirs[d],
                                         nlen, nm);
                                if (!zan_file_exists(primary) &&
                                    zan_find_macos_driver_dylib(
                                        driver_dirs[d], nm, nlen,
                                        fallback, sizeof(fallback))) {
                                    cmd_appendf(cmd, sizeof(cmd), " \"%s\"",
                                             fallback);
                                    nm = NULL;
                                }
                                break;
                            }
                            if (!nm) continue;
                        }
                        cmd_appendf(cmd, sizeof(cmd), " -l%.*s",
                                 nlen, nm);
                    }
                    for (int li = 0; li < static_driver_lib_count; li++) {
                        cmd_appendf(cmd, sizeof(cmd), " %s",
                                 static_driver_libs[li]);
                    }
                    if (getenv("ZAN_VERBOSE_LINK"))
                        fprintf(stderr, "[link] %s\n", cmd);
                    link_ret = system(cmd);
#endif
                } else if (target.os == ZAN_OS_LINUX) {
                    snprintf(cmd, sizeof(cmd),
                             "ld.lld -shared -o \"%s\" \"%s\"", obj_path, obj_tmp);
                    /* 内部辅助逻辑 */
                    if (rt_io_obj) {
                        cmd_appendf(cmd, sizeof(cmd), " \"%s\"",
                                 rt_io_obj);
                    }
                    if (rt_sync_obj) {
                        cmd_appendf(cmd, sizeof(cmd), " \"%s\"",
                                 rt_sync_obj);
                    }
                    if (rt_file_obj) {
                        cmd_appendf(cmd, sizeof(cmd), " \"%s\"",
                                 rt_file_obj);
                    }
                    if (rt_embed_obj) {
                        cmd_appendf(cmd, sizeof(cmd), " \"%s\"",
                                 rt_embed_obj);
                    }
                    if (rt_inflate_obj) {
                        cmd_appendf(cmd, sizeof(cmd), " \"%s\"",
                                 rt_inflate_obj);
                    }
                    if (rt_timer_obj) {
                        cmd_appendf(cmd, sizeof(cmd), " \"%s\"",
                                 rt_timer_obj);
                    }
                } else if (target.os == ZAN_OS_MACOS) {
                    const char *march = (target.arch == ZAN_ARCH_AARCH64)
                                        ? "arm64" : "x86_64";
                    /* 内部辅助逻辑 */
                    char tbd[1200];
                    snprintf(tbd, sizeof(tbd), "%s/macos/libSystem.tbd",
                             exe_dir);
                    if (!zan_file_exists(tbd)) {
                        fprintf(stderr,
                                "error: bundled macOS libSystem stub not found "
                                "at '%s'; reinstall zan or rebuild with "
                                "toolchain/macos present\n", tbd);
                        generated_object_vec_remove(&generated_objects, obj_tmp);
                        zan_diag_free_buffers(irgen.diag);
                        zan_irgen_destroy(&irgen);
                        zan_arena_free(ir_arena);
                        zan_arena_free(arena);
                        free(source);
                        return 1;
                    }
                    snprintf(cmd, sizeof(cmd),
                             "ld64.lld -dylib -arch %s -platform_version macos "
                             "11.0 11.0 -o \"%s\" \"%s\" \"%s\"",
                             march, obj_path, obj_tmp, tbd);
                    /* 内部辅助逻辑 */
                    if (rt_io_obj) {
                        cmd_appendf(cmd, sizeof(cmd), " \"%s\"",
                                 rt_io_obj);
                    }
                    if (rt_sync_obj) {
                        cmd_appendf(cmd, sizeof(cmd), " \"%s\"",
                                 rt_sync_obj);
                    }
                    if (rt_file_obj) {
                        cmd_appendf(cmd, sizeof(cmd), " \"%s\"",
                                 rt_file_obj);
                    }
                    if (rt_embed_obj) {
                        cmd_appendf(cmd, sizeof(cmd), " \"%s\"",
                                 rt_embed_obj);
                    }
                    if (rt_inflate_obj) {
                        cmd_appendf(cmd, sizeof(cmd), " \"%s\"",
                                 rt_inflate_obj);
                    }
                    if (rt_timer_obj) {
                        cmd_appendf(cmd, sizeof(cmd), " \"%s\"",
                                 rt_timer_obj);
                    }
                } else if (cross_compiling &&
                           target.os == ZAN_OS_ANDROID) {
                    /* 底层系统交互与数据协议契约 */
                    char exe_dir3[1024] = {0};
                    zan_exe_dir(exe_dir3, sizeof(exe_dir3));
                    const char *asub3 = (target.arch == ZAN_ARCH_AARCH64)
                                        ? "android-arm64" : "android-x64";
                    char sys3[1200];
                    snprintf(sys3, sizeof(sys3), "%s/%s", exe_dir3, asub3);
                    char probe3[1300];
                    snprintf(probe3, sizeof(probe3),
                             "%s/android_native_app_glue.o", sys3);
                    if (!zan_file_exists(probe3)) {
                        fprintf(stderr,
                                "error: bundled %s sysroot subset not found "
                                "at '%s'; reinstall zan or rebuild with "
                                "toolchain/%s present\n",
                                asub3, sys3, asub3);
                        generated_object_vec_remove(&generated_objects, obj_tmp);
                        zan_diag_free_buffers(irgen.diag);
                        zan_irgen_destroy(&irgen);
                        zan_arena_free(ir_arena);
                        zan_arena_free(arena);
                        free(source);
                        return 1;
                    }
                    snprintf(cmd, sizeof(cmd),
                             "ld.lld -shared --no-undefined -o \"%s\""
                             " \"%s/android_native_app_glue.o\" \"%s\"",
                             obj_path, sys3, obj_tmp);
                    for (int di = 0; di < zan_lib_ndirs; di++) {
                        cmd_appendf(cmd, sizeof(cmd), " -L\"%s\"",
                                 zan_lib_dirs[di]);
                    }
                    for (int di = 0; di < extra_lib_path_count; di++) {
                        cmd_appendf(cmd, sizeof(cmd), " -L\"%s\"",
                                 extra_lib_paths[di]);
                    }
                    if (rt_io_obj) {
                        cmd_appendf(cmd, sizeof(cmd), " \"%s\"",
                                 rt_io_obj);
                    }
                    if (rt_sync_obj) {
                        cmd_appendf(cmd, sizeof(cmd), " \"%s\"",
                                 rt_sync_obj);
                    }
                    if (rt_file_obj) {
                        cmd_appendf(cmd, sizeof(cmd), " \"%s\"",
                                 rt_file_obj);
                    }
                    if (rt_embed_obj) {
                        cmd_appendf(cmd, sizeof(cmd), " \"%s\"",
                                 rt_embed_obj);
                    }
                    if (rt_inflate_obj) {
                        cmd_appendf(cmd, sizeof(cmd), " \"%s\"",
                                 rt_inflate_obj);
                    }
                    if (rt_timer_obj) {
                        cmd_appendf(cmd, sizeof(cmd), " \"%s\"",
                                 rt_timer_obj);
                    }
                    for (int li = 0; li < static_driver_lib_count; li++) {
                        cmd_appendf(cmd, sizeof(cmd), " %s",
                                 static_driver_libs[li]);
                    }
                    for (int li = 0; li < irgen.extern_lib_count; li++) {
                        if (zan_win_system_lib(irgen.extern_libs[li].str,
                                               (int)irgen.extern_libs[li].len))
                            continue;
                        int nlen;
                        const char *nm = zan_dllimport_lname(
                            irgen.extern_libs[li].str,
                            (int)irgen.extern_libs[li].len, &nlen);
                        if (!nm) continue;
                        cmd_appendf(cmd, sizeof(cmd), " -l%.*s",
                                 nlen, nm);
                    }
                    { cmd_appendf(cmd, sizeof(cmd),
                               " \"%s/libc.so\" \"%s/libm.so\""
                               " \"%s/liblog.so\" \"%s/libdl.so\"",
                               sys3, sys3, sys3, sys3); }
                    /* 内部辅助实现 */
                    { cmd_appendf(cmd, sizeof(cmd),
                               " \"%s/libandroid.so\" \"%s/libEGL.so\""
                               " \"%s/libGLESv2.so\" \"%s/libaaudio.so\"",
                               sys3, sys3, sys3, sys3); }
                    { cmd_appendf(cmd, sizeof(cmd),
                               " \"%s/libclang_rt.builtins.a\"",
                               sys3); }
                    if (getenv("ZAN_VERBOSE_LINK"))
                        fprintf(stderr, "[link] %s\n", cmd);
                    link_ret = system(cmd);
                } else if (target.os == ZAN_OS_OHOS) {
                    /* 模块核心语义抽象与接口调用契约 */
                    const char *osub = (target.arch == ZAN_ARCH_AARCH64)
                                       ? "ohos-arm64" : "ohos-x64";
                    char sys4[1200];
                    snprintf(sys4, sizeof(sys4), "%s/%s", link_exe_dir, osub);
                    char probe4[1300];
                    snprintf(probe4, sizeof(probe4), "%s/zap_main.o", sys4);
                    if (!zan_file_exists(probe4)) {
                        fprintf(stderr,
                                "error: bundled %s sysroot subset not found "
                                "at '%s'; reinstall zan or rebuild with "
                                "toolchain/%s present\n",
                                osub, sys4, osub);
                        generated_object_vec_remove(&generated_objects, obj_tmp);
                        zan_diag_free_buffers(irgen.diag);
                        zan_irgen_destroy(&irgen);
                        zan_arena_free(ir_arena);
                        zan_arena_free(arena);
                        free(source);
                        return 1;
                    }
                    snprintf(cmd, sizeof(cmd),
                             "ld.lld -shared -o \"%s\" \"%s/zap_main.o\""
                             " \"%s\" \"%s/libclang_rt.builtins.a\""
                             " \"%s/libEGL.so\" \"%s/libGLESv3.so\"",
                             obj_path, sys4, obj_tmp, sys4, sys4, sys4);
                    /* 内部辅助逻辑 */
                    for (int d = 0; d < cross_archive_count; d++) {
                        cmd_appendf(cmd, sizeof(cmd), " \"%s\"",
                                 cross_archives[d]);
                    }
                    /* 内部辅助逻辑 */
                    if (rt_timer_obj) {
                        cmd_appendf(cmd, sizeof(cmd), " \"%s\"",
                                 rt_timer_obj);
                    }
                    if (irgen.uses_socket_async) {
                        cmd_appendf(cmd, sizeof(cmd),
                                 " \"%s/zanrt_io.o\"", sys4);
                    }
                    if (irgen.uses_sync_runtime) {
                        cmd_appendf(cmd, sizeof(cmd),
                                 " \"%s/zanrt_sync.o\"", sys4);
                    }
                    if (irgen.uses_file_runtime) {
                        cmd_appendf(cmd, sizeof(cmd),
                                 " \"%s/zanrt_file.o\"", sys4);
                    }
                    if (irgen.uses_embed_api) {
                        cmd_appendf(cmd, sizeof(cmd),
                                 " \"%s/zan_embed_api.o\"", sys4);
                    }
                    if (irgen.uses_inflate) {
                        cmd_appendf(cmd, sizeof(cmd),
                                 " \"%s/zan_inflate.o\"", sys4);
                    }
                    if (getenv("ZAN_VERBOSE_LINK"))
                        fprintf(stderr, "[link] %s\n", cmd);
                    link_ret = system(cmd);
                } else {
                    fprintf(stderr, "error: shared libraries are not supported "
                            "for this target yet\n");
                    generated_object_vec_remove(&generated_objects, obj_tmp);
                    zan_diag_free_buffers(irgen.diag);
                    zan_irgen_destroy(&irgen);
                    zan_arena_free(ir_arena);
                    zan_arena_free(arena);
                    free(source);
                    return 1;
                }
                /* 内部辅助逻辑 */
                if (!lib_spawned) {
                    for (int di = 0; di < zan_lib_ndirs; di++) {
                        cmd_appendf(cmd, sizeof(cmd), " -L\"%s\"",
                                 zan_lib_dirs[di]);
                    }
                    for (int di = 0; di < extra_lib_path_count; di++) {
                        cmd_appendf(cmd, sizeof(cmd), " -L\"%s\"",
                                 extra_lib_paths[di]);
                    }
                    for (int li = 0; li < irgen.extern_lib_count; li++) {
                        /* 内部辅助逻辑 */
                        if (target.os != ZAN_OS_WINDOWS &&
                            zan_win_system_lib(irgen.extern_libs[li].str,
                                               (int)irgen.extern_libs[li].len))
                            continue;
                        int nlen;
                        const char *nm = zan_dllimport_lname(
                            irgen.extern_libs[li].str,
                            (int)irgen.extern_libs[li].len, &nlen);
                        if (!nm) continue;
                        cmd_appendf(cmd, sizeof(cmd), " -l%.*s",
                                 nlen, nm);
                    }
                    for (int li = 0; li < static_driver_lib_count; li++) {
                        cmd_appendf(cmd, sizeof(cmd), " %s",
                                 static_driver_libs[li]);
                    }
                    if (getenv("ZAN_VERBOSE_LINK"))
                        fprintf(stderr, "[link] %s\n", cmd);
                    link_ret = system(cmd);
                }
            }
        } else if (cross_compiling && target.os == ZAN_OS_LINUX) {
            /* 内部辅助逻辑 */
            char exe_dir[1024] = {0};
#ifdef _WIN32
            GetModuleFileNameA(NULL, exe_dir, sizeof(exe_dir));
            { char *s = strrchr(exe_dir, '\\'); if (s) *s = '\0'; }
#elif defined(__APPLE__)
            { uint32_t sz = sizeof(exe_dir);
              if (_NSGetExecutablePath(exe_dir, &sz) != 0) exe_dir[0] = '\0';
              char *s = strrchr(exe_dir, '/'); if (s) *s = '\0'; }
#else
            { ssize_t n = readlink("/proc/self/exe", exe_dir, sizeof(exe_dir) - 1);
              if (n > 0) { exe_dir[n] = '\0'; char *s = strrchr(exe_dir, '/'); if (s) *s = '\0'; } }
#endif
            const char *sub = (target.arch == ZAN_ARCH_AARCH64)
                              ? "linux-arm64"
                              : (target.arch == ZAN_ARCH_RISCV64)
                              ? "linux-riscv64" : "linux-musl";
            char sys[1200];
            snprintf(sys, sizeof(sys), "%s/%s", exe_dir, sub);

            char cmd[4096];
            /* 底层系统交互与数据协议契约 */
            snprintf(cmd, sizeof(cmd),
                     "ld.lld -static%s -o \"%s\" \"%s/crt1.o\" \"%s/crti.o\" \"%s\"",
                     publish_mode ? " -s --gc-sections" : "", obj_path, sys, sys, obj_tmp);
            for (int di = 0; di < zan_lib_ndirs; di++) {
                cmd_appendf(cmd, sizeof(cmd), " -L\"%s\"",
                         zan_lib_dirs[di]);
            }
            for (int di = 0; di < extra_lib_path_count; di++) {
                cmd_appendf(cmd, sizeof(cmd), " -L\"%s\"",
                         extra_lib_paths[di]);
            }
            if (rt_timer_obj) {
                cmd_appendf(cmd, sizeof(cmd), " \"%s\"", rt_timer_obj);
            }
            if (rt_io_obj || irgen.uses_socket_async || external_async_executor) {
                const char *ioname = external_async_executor ? "zanrt_io_mt.o" : "zanrt_io.o";
                char rt_io_path[1400];
                snprintf(rt_io_path, sizeof(rt_io_path), "%s/%s", sys, ioname);
                if (!zan_file_exists(rt_io_path) && external_async_executor) {
                    snprintf(rt_io_path, sizeof(rt_io_path), "%s/zanrt_io.o", sys);
                }
                if (zan_file_exists(rt_io_path)) {
                    cmd_appendf(cmd, sizeof(cmd), " \"%s\"", rt_io_path);
                }
            }
            if (irgen.uses_sync_runtime) {
                /* 内部辅助逻辑 */
                cmd_appendf(cmd, sizeof(cmd), " \"%s/zanrt_sync.o\"", sys);
            }
            if (irgen.uses_file_runtime) {
                cmd_appendf(cmd, sizeof(cmd), " \"%s/zanrt_file.o\"", sys);
            }
            if (irgen.uses_embed_api) {
                /* 模块核心语义抽象与接口调用契约 */
                cmd_appendf(cmd, sizeof(cmd), " \"%s/zan_embed_api.o\"", sys);
            }
            if (irgen.uses_inflate) {
                cmd_appendf(cmd, sizeof(cmd), " \"%s/zan_inflate.o\"", sys);
            }
            /* 内部辅助实现 */
            { char memobj[1300];
              snprintf(memobj, sizeof(memobj), "%s/zanrt_mem.o", sys);
              if (zan_file_exists(memobj)) {
                  cmd_appendf(cmd, sizeof(cmd),
                           " \"%s\" --wrap=malloc --wrap=free"
                           " --wrap=calloc --wrap=realloc", memobj);
              } }
            { cmd_appendf(cmd, sizeof(cmd), " --start-group \"%s/libc.a\"", sys); }
            /* 底层系统交互与数据协议契约 */
            { char gcclib[1300];
              snprintf(gcclib, sizeof(gcclib), "%s/libgcc.a", sys);
              if (zan_file_exists(gcclib)) {
                  cmd_appendf(cmd, sizeof(cmd), " \"%s\"", gcclib);
              } }
            /* 内部辅助逻辑 */
            for (int d = 0; d < cross_archive_count; d++) {
                cmd_appendf(cmd, sizeof(cmd), " \"%s\"", cross_archives[d]);
            }
            /* 内部辅助逻辑 */
            {
                static const char *const ft_libs[] = {
                    "libfreetype.a", "libfontconfig.a", "libexpat.a"
                };
                for (int li = 0; li < 3; li++) {
                    char libpath[1300];
                    snprintf(libpath, sizeof(libpath), "%s/%s", sys,
                             ft_libs[li]);
                    if (zan_file_exists(libpath)) {
                        cmd_appendf(cmd, sizeof(cmd), " \"%s\"", libpath);
                    }
                }
            }
            for (int li = 0; li < static_driver_lib_count; li++) {
                cmd_appendf(cmd, sizeof(cmd), " %s",
                         static_driver_libs[li]);
            }
            { cmd_appendf(cmd, sizeof(cmd),
                       " --end-group \"%s/crtn.o\"", sys); }
            link_ret = system(cmd);
        } else if (cross_compiling && target.os == ZAN_OS_OHOS) {
            /* 核心系统底层抽象与内存语义契约 */
            char exe_dir[1024] = {0};
            zan_exe_dir(exe_dir, sizeof(exe_dir));
            const char *osub = (target.arch == ZAN_ARCH_AARCH64)
                               ? "ohos-arm64" : "ohos-x64";
            char sys[1200];
            snprintf(sys, sizeof(sys), "%s/%s", exe_dir, osub);
            char probe[1300];
            snprintf(probe, sizeof(probe), "%s/libc.a", sys);
            if (!zan_file_exists(probe)) {
                fprintf(stderr,
                        "error: bundled %s OHOS sysroot subset not found at "
                        "'%s'; reinstall zan or rebuild with toolchain/%s "
                        "present\n", osub, sys, osub);
                generated_object_vec_remove(&generated_objects, obj_tmp);
                zan_diag_free_buffers(irgen.diag);
                zan_irgen_destroy(&irgen);
                zan_arena_free(ir_arena);
                zan_arena_free(arena);
                free(source);
                return 1;
            }
            char cmd[8192];
            snprintf(cmd, sizeof(cmd),
                     "ld.lld -static%s -o \"%s\" \"%s/crt1.o\" \"%s/crti.o\""
                     " \"%s/clang_rt.crtbegin.o\" \"%s\"",
                     publish_mode ? " -s --gc-sections" : "", obj_path, sys, sys, sys, obj_tmp);
            for (int di = 0; di < zan_lib_ndirs; di++) {
                cmd_appendf(cmd, sizeof(cmd), " -L\"%s\"",
                         zan_lib_dirs[di]);
            }
            for (int di = 0; di < extra_lib_path_count; di++) {
                cmd_appendf(cmd, sizeof(cmd), " -L\"%s\"",
                         extra_lib_paths[di]);
            }
            if (rt_timer_obj) {
                cmd_appendf(cmd, sizeof(cmd), " \"%s\"", rt_timer_obj);
            }
            if (irgen.uses_socket_async) {
                cmd_appendf(cmd, sizeof(cmd), " \"%s/zanrt_io.o\"", sys);
            }
            if (irgen.uses_sync_runtime) {
                /* 模块核心语义抽象与接口调用契约 */
                cmd_appendf(cmd, sizeof(cmd), " \"%s/zanrt_sync.o\"", sys);
            }
            if (irgen.uses_file_runtime) {
                cmd_appendf(cmd, sizeof(cmd), " \"%s/zanrt_file.o\"", sys);
            }
            if (irgen.uses_embed_api) {
                cmd_appendf(cmd, sizeof(cmd),
                         " \"%s/zan_embed_api.o\"", sys);
            }
            if (irgen.uses_inflate) {
                cmd_appendf(cmd, sizeof(cmd),
                         " \"%s/zan_inflate.o\"", sys);
            }
            { cmd_appendf(cmd, sizeof(cmd),
                       " --start-group \"%s/libc.a\" \"%s/libunwind.a\""
                       " \"%s/libclang_rt.builtins.a\"", sys, sys, sys); }
            for (int d = 0; d < cross_archive_count; d++) {
                cmd_appendf(cmd, sizeof(cmd), " \"%s\"", cross_archives[d]);
            }
            for (int li = 0; li < static_driver_lib_count; li++) {
                cmd_appendf(cmd, sizeof(cmd), " %s",
                         static_driver_libs[li]);
            }
            { cmd_appendf(cmd, sizeof(cmd),
                       " --end-group \"%s/clang_rt.crtend.o\" \"%s/crtn.o\"",
                       sys, sys); }
            if (getenv("ZAN_VERBOSE_LINK"))
                fprintf(stderr, "[link] %s\n", cmd);
            link_ret = system(cmd);
        } else if (cross_compiling && target.os == ZAN_OS_ANDROID) {
            /* 内部辅助实现 */
            char exe_dir[1024] = {0};
            zan_exe_dir(exe_dir, sizeof(exe_dir));
            const char *asub = (target.arch == ZAN_ARCH_AARCH64)
                               ? "android-arm64" : "android-x64";
            char sys[1200];
            snprintf(sys, sizeof(sys), "%s/%s", exe_dir, asub);
            /* 内部辅助逻辑 */
            const char *drv_libs[ZAN_LINK_MAX_LIBS];
            int drv_lib_len[ZAN_LINK_MAX_LIBS];
            int drv_lib_count = 0;
            for (int li = 0; li < irgen.extern_lib_count; li++) {
                if (zan_win_system_lib(irgen.extern_libs[li].str,
                                       (int)irgen.extern_libs[li].len))
                    continue;
                int nlen;
                const char *nm = zan_dllimport_lname(
                    irgen.extern_libs[li].str,
                    (int)irgen.extern_libs[li].len, &nlen);
                if (!nm) continue;
                if (drv_lib_count >= ZAN_LINK_MAX_LIBS)
                    link_cap_exceeded("DllImport libraries", ZAN_LINK_MAX_LIBS);
                drv_libs[drv_lib_count] = nm;
                drv_lib_len[drv_lib_count] = nlen;
                drv_lib_count++;
            }
            char cmd[8192];
            if (drv_lib_count > 0) {
                /* 核心系统底层抽象与内存语义契约 */
                snprintf(cmd, sizeof(cmd),
                         "ld.lld -pie%s -o \"%s\" \"%s/crtbegin_dynamic.o\""
                         " \"%s\"",
                         publish_mode ? " -s --gc-sections" : "", obj_path, sys, obj_tmp);
            } else {
                snprintf(cmd, sizeof(cmd),
                         "ld.lld -static%s -o \"%s\" \"%s/crtbegin_static.o\""
                         " \"%s\"",
                         publish_mode ? " -s --gc-sections" : "", obj_path, sys, obj_tmp);
            }
            for (int di = 0; di < zan_lib_ndirs; di++) {
                cmd_appendf(cmd, sizeof(cmd), " -L\"%s\"",
                         zan_lib_dirs[di]);
            }
            for (int di = 0; di < extra_lib_path_count; di++) {
                cmd_appendf(cmd, sizeof(cmd), " -L\"%s\"",
                         extra_lib_paths[di]);
            }
            if (rt_timer_obj) {
                cmd_appendf(cmd, sizeof(cmd), " \"%s\"", rt_timer_obj);
            }
            if (irgen.uses_socket_async) {
                cmd_appendf(cmd, sizeof(cmd), " \"%s/zanrt_io.o\"", sys);
            }
            if (irgen.uses_sync_runtime) {
                /* 模块核心语义抽象与接口调用契约 */
                cmd_appendf(cmd, sizeof(cmd), " \"%s/zanrt_sync.o\"", sys);
            }
            if (irgen.uses_file_runtime) {
                cmd_appendf(cmd, sizeof(cmd), " \"%s/zanrt_file.o\"", sys);
            }
            if (irgen.uses_embed_api) {
                cmd_appendf(cmd, sizeof(cmd),
                         " \"%s/zan_embed_api.o\"", sys);
            }
            if (irgen.uses_inflate) {
                cmd_appendf(cmd, sizeof(cmd),
                         " \"%s/zan_inflate.o\"", sys);
            }
            if (drv_lib_count > 0) {
                /* 模块核心语义抽象与接口调用契约 */
                cmd_appendf(cmd, sizeof(cmd), " --start-group");
            } else {
                cmd_appendf(cmd, sizeof(cmd),
                         " --start-group \"%s/libc.a\" \"%s/libm.a\"", sys, sys);
            }
            for (int d = 0; d < cross_archive_count; d++) {
                cmd_appendf(cmd, sizeof(cmd), " \"%s\"", cross_archives[d]);
            }
            for (int li = 0; li < static_driver_lib_count; li++) {
                cmd_appendf(cmd, sizeof(cmd), " %s",
                         static_driver_libs[li]);
            }
            for (int li = 0; li < drv_lib_count; li++) {
                cmd_appendf(cmd, sizeof(cmd), " -l%.*s",
                         drv_lib_len[li], drv_libs[li]);
            }
            if (drv_lib_count > 0) {
                cmd_appendf(cmd, sizeof(cmd),
                         " --end-group \"%s/libc.so\" \"%s/libm.so\""
                         " \"%s/liblog.so\" \"%s/libdl.so\""
                         " \"%s/libandroid.so\" \"%s/libEGL.so\""
                         " \"%s/libGLESv2.so\" \"%s/libaaudio.so\""
                         " \"%s/crtend_android.o\""
                         " \"%s/libclang_rt.builtins.a\"",
                         sys, sys, sys, sys, sys, sys, sys, sys, sys, sys);
            } else {
                cmd_appendf(cmd, sizeof(cmd),
                         " --end-group \"%s/libdl.a\" \"%s/crtend_android.o\""
                         " \"%s/libclang_rt.builtins.a\"",
                         sys, sys, sys);
            }
            if (getenv("ZAN_VERBOSE_LINK"))
                fprintf(stderr, "[link] %s\n", cmd);
            link_ret = system(cmd);
        } else if (cross_compiling && target.os == ZAN_OS_WINDOWS) {
            /* 核心系统底层抽象与内存语义契约 */
            char exe_dir2[1024];
            zan_exe_dir(exe_dir2, sizeof(exe_dir2));
            const char *wsub = (target.arch == ZAN_ARCH_AARCH64)
                               ? "win-arm64" : "win-x64";
            char syslib[1200];
            snprintf(syslib, sizeof(syslib), "%s/%s/mingw/lib", exe_dir2, wsub);
            char probe[1300];
            snprintf(probe, sizeof(probe), "%s/crt2.o", syslib);
            if (!zan_file_exists(probe)) {
                fprintf(stderr,
                        "error: bundled %s mingw runtime not found at '%s'; "
                        "reinstall zan or rebuild with toolchain/%s present\n",
                        wsub, syslib, wsub);
                generated_object_vec_remove(&generated_objects, obj_tmp);
                zan_diag_free_buffers(irgen.diag);
                zan_irgen_destroy(&irgen);
                zan_arena_free(ir_arena);
                zan_arena_free(arena);
                free(source);
                return 1;
            }
            /* 内部辅助逻辑 */
            char winrt_io[1400] = {0};
            char winrt_sync[1400] = {0};
            char winrt_file[1400] = {0};
            char winrt_embed[1400] = {0};
            if (rt_io_obj)
                snprintf(winrt_io, sizeof(winrt_io), "%s/%s/%s", exe_dir2, wsub,
                         external_async_executor ? "zanrt_io_mt.o" : "zanrt_io.o");
            if (rt_sync_obj)
                snprintf(winrt_sync, sizeof(winrt_sync), "%s/%s/zanrt_sync.o",
                         exe_dir2, wsub);
            if (rt_file_obj)
                snprintf(winrt_file, sizeof(winrt_file), "%s/%s/zanrt_file.o",
                         exe_dir2, wsub);
            if (rt_embed_obj)
                snprintf(winrt_embed, sizeof(winrt_embed),
                         "%s/%s/zan_embed_api.o", exe_dir2, wsub);
            char winrt_inflate[1400] = {0};
            if (rt_inflate_obj)
                snprintf(winrt_inflate, sizeof(winrt_inflate),
                         "%s/%s/zan_inflate.o", exe_dir2, wsub);
            if ((winrt_io[0] && !zan_file_exists(winrt_io))
                || (winrt_sync[0] && !zan_file_exists(winrt_sync))
                || (winrt_file[0] && !zan_file_exists(winrt_file))
                || (winrt_embed[0] && !zan_file_exists(winrt_embed))
                || (winrt_inflate[0] && !zan_file_exists(winrt_inflate))) {
                fprintf(stderr,
                        "error: bundled %s runtime objects not found in "
                        "'%s/%s'; reinstall zan or rebuild with toolchain/%s "
                        "present (see scripts/build_win_rt.sh)\n",
                        wsub, exe_dir2, wsub, wsub);
                generated_object_vec_remove(&generated_objects, obj_tmp);
                zan_diag_free_buffers(irgen.diag);
                zan_irgen_destroy(&irgen);
                zan_arena_free(ir_arena);
                zan_arena_free(arena);
                free(source);
                return 1;
            }
            char gcclib[1300];
            snprintf(gcclib, sizeof(gcclib), "%s/libgcc.a", syslib);
            int have_gcc = zan_file_exists(gcclib);

            char cmd[8192];
            snprintf(cmd, sizeof(cmd),
                     "ld.lld -m %s --stack 268435456%s%s -o \"%s\" "
                     "\"%s/crt2.o\" \"%s/crtbegin.o\"",
                     (target.arch == ZAN_ARCH_AARCH64) ? "arm64pe" : "i386pep",
                     publish_mode ? " -s --gc-sections" : "",
                     (link_subsystem && strcmp(link_subsystem, "windows") == 0)
                         ? " --subsystem windows" : "",
                     obj_path, syslib, syslib);
            for (int di = 0; di < zan_lib_ndirs; di++) {
                cmd_appendf(cmd, sizeof(cmd), " -L\"%s\"",
                         zan_lib_dirs[di]);
            }
            { cmd_appendf(cmd, sizeof(cmd), " -L\"%s\"", syslib); }
            for (int di = 0; di < extra_lib_path_count; di++) {
                cmd_appendf(cmd, sizeof(cmd), " -L\"%s\"",
                         extra_lib_paths[di]);
            }
            { cmd_appendf(cmd, sizeof(cmd), " \"%s\"", obj_tmp); }
            if (rt_timer_obj) {
                cmd_appendf(cmd, sizeof(cmd), " \"%s\"", rt_timer_obj);
            }
            if (winrt_io[0]) {
                cmd_appendf(cmd, sizeof(cmd), " \"%s\"", winrt_io);
            }
            if (winrt_sync[0]) {
                cmd_appendf(cmd, sizeof(cmd), " \"%s\"", winrt_sync);
            }
            if (winrt_file[0]) {
                cmd_appendf(cmd, sizeof(cmd), " \"%s\"", winrt_file);
            }
            if (winrt_embed[0]) {
                cmd_appendf(cmd, sizeof(cmd), " \"%s\"", winrt_embed);
            }
            if (winrt_inflate[0]) {
                cmd_appendf(cmd, sizeof(cmd), " \"%s\"", winrt_inflate);
            }
            for (int ei = 0; ei < extra_link_input_count; ei++) {
                cmd_appendf(cmd, sizeof(cmd), " \"%s\"",
                         extra_link_inputs[ei]);
            }
            { cmd_appendf(cmd, sizeof(cmd), " --start-group"
                       " -lmingw32 -lmoldname -lmingwex -lmsvcrt"
                       " -lkernel32 -ladvapi32 -lshell32 -luser32"); }
            if (winrt_io[0]) {
                cmd_appendf(cmd, sizeof(cmd), " -lws2_32");
            }
            { cmd_appendf(cmd, sizeof(cmd), have_gcc
                       ? " -lgcc -lgcc_eh"
                       : " -lclang_rt.builtins-aarch64 -lunwind -lucrt"); }
            for (int ei = 0; ei < extra_link_lib_count; ei++) {
                cmd_appendf(cmd, sizeof(cmd), " -l%s",
                         extra_link_libs[ei]);
            }
            /* 内部辅助逻辑 */
            for (int li = 0; li < irgen.extern_lib_count; li++) {
                int nlen;
                const char *nm = zan_dllimport_lname(
                    irgen.extern_libs[li].str,
                    (int)irgen.extern_libs[li].len, &nlen);
                if (!nm) continue;
                cmd_appendf(cmd, sizeof(cmd), " -l%.*s", nlen, nm);
            }
            for (int li = 0; li < static_driver_lib_count; li++) {
                cmd_appendf(cmd, sizeof(cmd), " %s",
                         static_driver_libs[li]);
            }
            { cmd_appendf(cmd, sizeof(cmd),
                       " --end-group \"%s/crtend.o\"", syslib); }
            if (getenv("ZAN_VERBOSE_LINK"))
                fprintf(stderr, "[link win] %s\n", cmd);
            link_ret = system(cmd);
        } else if (cross_compiling && target.os == ZAN_OS_WASI) {
            /* 内部辅助逻辑 */
            char exe_dir2[1024];
            zan_exe_dir(exe_dir2, sizeof(exe_dir2));
            char sys[1200];
            snprintf(sys, sizeof(sys), "%s/wasm32", exe_dir2);
            char probe[1300];
            snprintf(probe, sizeof(probe), "%s/libc.a", sys);
            if (!zan_file_exists(probe)) {
                fprintf(stderr,
                        "error: bundled wasm32 wasi sysroot not found at "
                        "'%s'; reinstall zan or rebuild with "
                        "toolchain/wasm32 present\n", sys);
                generated_object_vec_remove(&generated_objects, obj_tmp);
                zan_diag_free_buffers(irgen.diag);
                zan_irgen_destroy(&irgen);
                zan_arena_free(ir_arena);
                zan_arena_free(arena);
                free(source);
                return 1;
            }
            if (rt_io_obj) {
                /* 内部辅助实现 */
                static const char *const sock_pre[] = {
                    "zan_io_socket_", "zan_gate_", "zan_io_connect_",
                    "zan_io_resolve", "zan_io_sockaddr_",
                    NULL
                };
                static const char *const gui_pre2[] = { "zan_gui_", NULL };
                if (wasm_obj_vec_refs_any(&generated_objects, sock_pre)
                    && !wasm_obj_vec_refs_any(&generated_objects, gui_pre2)) {
                    fprintf(stderr,
                            "error: socket-async programs are not available for "
                            "the wasm32 target\n");
                    generated_object_vec_remove(&generated_objects, obj_tmp);
                    zan_diag_free_buffers(irgen.diag);
                    zan_irgen_destroy(&irgen);
                    zan_arena_free(ir_arena);
                    zan_arena_free(arena);
                    free(source);
                    return 1;
                }
            }
            char cmd[8192];
            /* 内部辅助实现 */
            snprintf(cmd, sizeof(cmd),
                     "wasm-ld%s -z stack-size=4194304 --max-memory=536870912 "
                     "--table-base=2 "
                     "-o \"%s\" \"%s/crt1.o\" \"%s\"",
                     publish_mode ? " -s --gc-sections" : "", obj_path, sys,
                     obj_tmp);
            for (int ei = 0; ei < extra_link_input_count; ei++) {
                cmd_appendf(cmd, sizeof(cmd), " \"%s\"",
                         extra_link_inputs[ei]);
            }
            if (irgen.uses_file_runtime) {
                /* 编译期中间表示与代码生成内部规范 */
                cmd_appendf(cmd, sizeof(cmd), " \"%s/zanrt_file.o\"",
                         sys);
            }
            if (irgen.uses_inflate) {
                char inflateobj[1300];
                snprintf(inflateobj, sizeof(inflateobj), "%s/zan_inflate.o", sys);
                if (!zan_file_exists(inflateobj)) {
                    fprintf(stderr,
                            "error: embedded resources for wasm32 need the "
                            "inflate runtime object at '%s'; rebuild it with "
                            "scripts\\build_cross_rt.cmd\n", inflateobj);
                    generated_object_vec_remove(&generated_objects, obj_tmp);
                    zan_diag_free_buffers(irgen.diag);
                    zan_irgen_destroy(&irgen);
                    zan_arena_free(ir_arena);
                    zan_arena_free(arena);
                    free(source);
                    return 1;
                }
                cmd_appendf(cmd, sizeof(cmd), " \"%s\"", inflateobj);
            }
            /* 内部辅助逻辑 */
            { cmd_appendf(cmd, sizeof(cmd),
                       " \"%s/zanrt_timer.o\" \"%s/zanrt_wasm.o\"",
                       sys, sys); }
            {
                static const char *const gui_pre[] = { "zan_gui_", NULL };
                static const char *const image_pre[] = { "zan_image_", NULL };
                static const char *const audio_pre[] = { "zan_audio_", NULL };
                static const char *const game_pre[] = { "zan_game_", NULL };
                int need_game = wasm_obj_vec_refs_any(&generated_objects, game_pre);
                /* 模块核心语义抽象与接口调用契约 */
                int need_gui = need_game || wasm_obj_vec_refs_any(&generated_objects, gui_pre);
                const int need_module[] = {
                    need_game || wasm_obj_vec_refs_any(&generated_objects, image_pre),
                    wasm_obj_vec_refs_any(&generated_objects, audio_pre),
                    need_game
                };
                static const char *const module_obj[] = {
                    "zanrt_image.o", "zanrt_audio.o", "zanrt_game.o"
                };
                for (int mi = 0; mi < 3; mi++) {
                    if (!need_module[mi]) continue;
                    char nativeobj[1300];
                    snprintf(nativeobj, sizeof(nativeobj), "%s/%s", sys, module_obj[mi]);
                    if (!zan_file_exists(nativeobj)) {
                        fprintf(stderr,
                                "error: wasm32 native module needs runtime object "
                                "at '%s'; rebuild it with scripts\\build_cross_rt.cmd\n",
                                nativeobj);
                        generated_object_vec_remove(&generated_objects, obj_tmp);
                        zan_diag_free_buffers(irgen.diag);
                        zan_irgen_destroy(&irgen);
                        zan_arena_free(ir_arena);
                        zan_arena_free(arena);
                        free(source);
                        return 1;
                    }
                    cmd_appendf(cmd, sizeof(cmd), " \"%s\"", nativeobj);
                }
                /* 模块核心语义抽象与接口调用契约 */
                if (need_gui) {
                    char guiobj[1300];
                    snprintf(guiobj, sizeof(guiobj), "%s/zanrt_gui.o", sys);
                    if (!zan_file_exists(guiobj)) {
                        fprintf(stderr,
                                "error: GUI programs for wasm32 need the gui "
                                "runtime object at '%s'; rebuild it with "
                                "scripts\\build_cross_rt.cmd\n", guiobj);
                        generated_object_vec_remove(&generated_objects, obj_tmp);
                        zan_diag_free_buffers(irgen.diag);
                        zan_irgen_destroy(&irgen);
                        zan_arena_free(ir_arena);
                        zan_arena_free(arena);
                        free(source);
                        return 1;
                    }
                    cmd_appendf(cmd, sizeof(cmd),
                             " \"%s\" --export=zan_gui_wasm_feed", guiobj);
                    /* zanrt_gui */
                    char ftlib[1300];
                    snprintf(ftlib, sizeof(ftlib), "%s/libfreetype.a", sys);
                    if (zan_file_exists(ftlib)) {
                        cmd_appendf(cmd, sizeof(cmd), " \"%s\"", ftlib);
                    }
                    /* 内部辅助逻辑 */
                    char syncwobj[1300];
                    snprintf(syncwobj, sizeof(syncwobj),
                             "%s/zanrt_syncw.o", sys);
                    if (zan_file_exists(syncwobj)) {
                        cmd_appendf(cmd, sizeof(cmd), " \"%s\"", syncwobj);
                    }
                }
            }
            if (irgen.wasm_eh_used) {
                /* 模块核心语义抽象与接口调用契约 */
                cmd_appendf(cmd, sizeof(cmd), " \"%s/zanrt_ehtag.o\"",
                         sys);
            }
            { cmd_appendf(cmd, sizeof(cmd),
                       " \"%s/libc.a\" \"%s/libm.a\" \"%s/libzigc.a\""
                       " \"%s/libclang_rt.builtins-wasm32.a\"",
                       sys, sys, sys, sys); }
            link_ret = system(cmd);
        } else if (cross_compiling && (target.os == ZAN_OS_MACOS || target.os == ZAN_OS_IOS)) {
            /* 底层系统交互与数据协议契约 */
            char exe_dir2[1024];
            zan_exe_dir(exe_dir2, sizeof(exe_dir2));
            char tbd[1200];
            if (target.os == ZAN_OS_IOS) {
                snprintf(tbd, sizeof(tbd), "%s/ios/libSystem.tbd", exe_dir2);
                if (!zan_file_exists(tbd)) {
                    snprintf(tbd, sizeof(tbd), "%s/macos/libSystem.tbd", exe_dir2);
                }
            } else {
                snprintf(tbd, sizeof(tbd), "%s/macos/libSystem.tbd", exe_dir2);
            }
            if (!zan_file_exists(tbd)) {
                fprintf(stderr,
                        "error: bundled %s libSystem stub not found at "
                        "'%s'; reinstall zan or rebuild with "
                        "toolchain/%s present\n",
                        (target.os == ZAN_OS_IOS) ? "iOS" : "macOS",
                        tbd,
                        (target.os == ZAN_OS_IOS) ? "ios" : "macos");
                generated_object_vec_remove(&generated_objects, obj_tmp);
                zan_diag_free_buffers(irgen.diag);
                zan_irgen_destroy(&irgen);
                zan_arena_free(ir_arena);
                zan_arena_free(arena);
                free(source);
                return 1;
            }
            const char *march = (target.arch == ZAN_ARCH_AARCH64)
                                ? "arm64" : "x86_64";
            char macrt[1200];
            if (target.os == ZAN_OS_IOS) {
                snprintf(macrt, sizeof(macrt), "%s/ios/arm64", exe_dir2);
            } else {
                snprintf(macrt, sizeof(macrt), "%s/macos/%s", exe_dir2,
                         (target.arch == ZAN_ARCH_AARCH64) ? "arm64" : "x64");
            }
            char macrt_io[1400] = {0};
            char macrt_sync[1400] = {0};
            char macrt_file[1400] = {0};
            char macrt_embed[1400] = {0};
            if (rt_io_obj) {
                snprintf(macrt_io, sizeof(macrt_io), "%s/%s", macrt,
                         external_async_executor ? "zanrt_io_mt.o" : "zanrt_io.o");
            }
            if (rt_sync_obj) {
                snprintf(macrt_sync, sizeof(macrt_sync), "%s/zanrt_sync.o",
                         macrt);
            }
            if (rt_file_obj) {
                snprintf(macrt_file, sizeof(macrt_file), "%s/zanrt_file.o",
                         macrt);
            }
            if (rt_embed_obj) {
                snprintf(macrt_embed, sizeof(macrt_embed),
                         "%s/zan_embed_api.o", macrt);
            }
            char macrt_inflate[1400] = {0};
            if (rt_inflate_obj) {
                snprintf(macrt_inflate, sizeof(macrt_inflate),
                         "%s/zan_inflate.o", macrt);
            }
            if ((macrt_io[0] && !zan_file_exists(macrt_io))
                || (macrt_sync[0] && !zan_file_exists(macrt_sync))
                || (macrt_file[0] && !zan_file_exists(macrt_file))
                || (macrt_embed[0] && !zan_file_exists(macrt_embed))
                || (macrt_inflate[0] && !zan_file_exists(macrt_inflate))) {
                fprintf(stderr,
                        "error: bundled %s runtime objects not found in "
                        "'%s'; reinstall zan or rebuild with toolchain/%s "
                        "present (see scripts/build_%s_rt.sh)\n",
                        (target.os == ZAN_OS_IOS) ? "iOS" : "macOS",
                        macrt,
                        (target.os == ZAN_OS_IOS) ? "ios" : "macos",
                        (target.os == ZAN_OS_IOS) ? "ios" : "macos");
                generated_object_vec_remove(&generated_objects, obj_tmp);
                zan_diag_free_buffers(irgen.diag);
                zan_irgen_destroy(&irgen);
                zan_arena_free(ir_arena);
                zan_arena_free(arena);
                free(source);
                return 1;
            }
            char cmd[8192];
            if (target.os == ZAN_OS_IOS) {
                snprintf(cmd, sizeof(cmd),
                         "ld64.lld -arch arm64 -platform_version ios 14.0 14.0 -adhoc_codesign"
                         "%s -o \"%s\" \"%s\"",
                         publish_mode ? " -dead_strip" : "",
                         obj_path, obj_tmp);
            } else {
                snprintf(cmd, sizeof(cmd),
                         "ld64.lld -arch %s -platform_version macos 11.0 11.0 -adhoc_codesign"
                         "%s -o \"%s\" \"%s\"",
                         march, publish_mode ? " -dead_strip" : "",
                         obj_path, obj_tmp);
            }
            if (rt_timer_obj) {
                cmd_appendf(cmd, sizeof(cmd), " \"%s\"", rt_timer_obj);
            }
            if (macrt_io[0]) {
                cmd_appendf(cmd, sizeof(cmd), " \"%s\"", macrt_io);
            }
            if (macrt_sync[0]) {
                cmd_appendf(cmd, sizeof(cmd), " \"%s\"", macrt_sync);
            }
            if (macrt_file[0]) {
                cmd_appendf(cmd, sizeof(cmd), " \"%s\"", macrt_file);
            }
            if (macrt_embed[0]) {
                cmd_appendf(cmd, sizeof(cmd), " \"%s\"", macrt_embed);
            }
            if (macrt_inflate[0]) {
                cmd_appendf(cmd, sizeof(cmd), " \"%s\"", macrt_inflate);
            }
            char macrt_gui[1400] = {0};
            snprintf(macrt_gui, sizeof(macrt_gui), "%s/zanrt_gui.o", macrt);
            if (!zan_file_exists(macrt_gui)) {
                if (target.os == ZAN_OS_IOS) {
                    snprintf(macrt_gui, sizeof(macrt_gui), "%s/toolchain/ios/arm64/zanrt_gui.o", exe_dir2);
                    if (!zan_file_exists(macrt_gui)) {
                        snprintf(macrt_gui, sizeof(macrt_gui), "toolchain/ios/arm64/zanrt_gui.o");
                    }
                } else {
                    snprintf(macrt_gui, sizeof(macrt_gui), "%s/toolchain/macos/%s/zanrt_gui.o",
                             exe_dir2, (target.arch == ZAN_ARCH_AARCH64) ? "arm64" : "x64");
                    if (!zan_file_exists(macrt_gui)) {
                        snprintf(macrt_gui, sizeof(macrt_gui), "toolchain/macos/%s/zanrt_gui.o",
                                 (target.arch == ZAN_ARCH_AARCH64) ? "arm64" : "x64");
                    }
                }
            }
            bool has_gui_driver = false;
            for (int di = 0; di < cross_dylib_count; di++) {
                if (strstr(cross_dylibs[di], "zan_gui") != NULL) {
                    has_gui_driver = true;
                    break;
                }
            }
            if (has_gui_driver && zan_file_exists(macrt_gui)) {
                cmd_appendf(cmd, sizeof(cmd), " \"%s\"", macrt_gui);
            }
            for (int ei = 0; ei < extra_link_input_count; ei++) {
                cmd_appendf(cmd, sizeof(cmd), " \"%s\"",
                         extra_link_inputs[ei]);
            }
            for (int di = 0; di < cross_dylib_count; di++) {
                cmd_appendf(cmd, sizeof(cmd), " \"%s\"",
                         cross_dylibs[di]);
            }
            if (cross_dylib_count > 0) {
                cmd_appendf(cmd, sizeof(cmd),
                         " -rpath @loader_path");
            }
            { cmd_appendf(cmd, sizeof(cmd), " \"%s\"", tbd); }
            link_ret = system(cmd);
        } else if (cross_compiling && target.os == ZAN_OS_FREESTANDING) {
            /* 内部辅助实现 */
            remove(obj_path);
            if (rename(obj_tmp, obj_path) != 0) {
                fprintf(stderr, "error: cannot write object '%s'\n",
                        obj_path);
                generated_object_vec_remove(&generated_objects, obj_tmp);
                zan_diag_free_buffers(irgen.diag);
                zan_irgen_destroy(&irgen);
                zan_arena_free(ir_arena);
                zan_arena_free(arena);
                free(source);
                return 1;
            }
            link_ret = 0;
        } else if (cross_compiling) {
            fprintf(stderr,
                    "error: cross-compilation to '%s' is not supported yet "
                    "(see --list-targets)\n",
                    target.triple);
            generated_object_vec_remove(&generated_objects, obj_tmp);
            zan_diag_free_buffers(irgen.diag);
            zan_irgen_destroy(&irgen);
            zan_arena_free(ir_arena);
            zan_arena_free(arena);
            free(source);
            return 1;
        } else {
#ifdef _WIN32
        /* 内部辅助逻辑 */
        char exe_dir[1024];
        GetModuleFileNameA(NULL, exe_dir, sizeof(exe_dir));
        { char *s = strrchr(exe_dir, '\\'); if (s) *s = '\0'; }
        char ld_path[1200], syslib[1200];
        snprintf(ld_path, sizeof(ld_path), "%s\\ld.exe", exe_dir);
        snprintf(syslib, sizeof(syslib), "%s\\mingw\\lib", exe_dir);
        int have_bundle =
            zan_utf8_get_file_attributes(ld_path) != INVALID_FILE_ATTRIBUTES &&
            zan_utf8_get_file_attributes(syslib)  != INVALID_FILE_ATTRIBUTES;

        if (have_bundle) {
            char crt2[1300], crtbeg[1300], crtend[1300], lflag[1300];
            snprintf(crt2,   sizeof(crt2),   "%s\\crt2.o", syslib);
            snprintf(crtbeg, sizeof(crtbeg), "%s\\crtbegin.o", syslib);
            snprintf(crtend, sizeof(crtend), "%s\\crtend.o", syslib);
            snprintf(lflag,  sizeof(lflag),  "-L%s", syslib);

            /* 内部辅助实现 */
            const char *argv[ZAN_LINK_MAX_ARGV];
            int a = 0;
            argv[a++] = ld_path;
            argv[a++] = "-m";      argv[a++] = "i386pep";
            argv[a++] = "-Bdynamic";
            /* 底层系统交互与数据协议契约 */
            argv[a++] = "--stack"; argv[a++] = "268435456";
            if (publish_mode) {
                argv[a++] = "-s";
                /* 核心系统底层抽象与内存语义契约 */
                argv[a++] = "--gc-sections";
            }
            /* 模块核心语义抽象与接口调用契约 */
            if (link_subsystem && strcmp(link_subsystem, "windows") == 0) {
                argv[a++] = "--subsystem"; argv[a++] = "windows";
            }
            argv[a++] = "-o";      argv[a++] = obj_path;
            argv[a++] = crt2;
            argv[a++] = crtbeg;
            char ldirbufs[ZAN_LINK_MAX_DIRS][520];
            /* 内部辅助逻辑 */
            for (int di = 0; di < zan_lib_ndirs; di++) {
                if (a >= ZAN_LINK_MAX_ARGV - ZAN_LINK_ARGV_TAIL)
                    link_cap_exceeded("linker arguments", ZAN_LINK_MAX_ARGV);
                snprintf(ldirbufs[di], sizeof(ldirbufs[di]), "-L%s", zan_lib_dirs[di]);
                argv[a++] = ldirbufs[di];
            }
            argv[a++] = lflag;
            char elpbufs[ZAN_LINK_MAX_DIRS][520];
            for (int di = 0; di < extra_lib_path_count; di++) {
                if (a >= ZAN_LINK_MAX_ARGV - ZAN_LINK_ARGV_TAIL)
                    link_cap_exceeded("linker arguments", ZAN_LINK_MAX_ARGV);
                snprintf(elpbufs[di], sizeof(elpbufs[di]), "-L%s", extra_lib_paths[di]);
                argv[a++] = elpbufs[di];
            }
            for (int oi = 0; oi < generated_objects.count; oi++) {
                if (a >= ZAN_LINK_MAX_ARGV - ZAN_LINK_ARGV_TAIL)
                    link_cap_exceeded("generated objects", ZAN_LINK_MAX_ARGV);
                argv[a++] = generated_objects.paths[oi];
            }
            if (rt_io_obj) argv[a++] = rt_io_obj;
            if (rt_sync_obj) argv[a++] = rt_sync_obj;
            if (rt_file_obj) argv[a++] = rt_file_obj;
            if (rt_embed_obj) argv[a++] = rt_embed_obj;
            if (rt_inflate_obj) argv[a++] = rt_inflate_obj;
            if (rt_timer_obj) argv[a++] = rt_timer_obj;
            if (rt_mem_obj) {
                argv[a++] = rt_mem_obj;
                argv[a++] = "--wrap=malloc";  argv[a++] = "--wrap=free";
                argv[a++] = "--wrap=calloc"; argv[a++] = "--wrap=realloc";
            }
            /* 核心系统底层抽象与内存语义契约 */
            for (int ei = 0; ei < extra_link_input_count; ei++) {
                if (a >= ZAN_LINK_MAX_ARGV - ZAN_LINK_ARGV_TAIL)
                    link_cap_exceeded("linker arguments", ZAN_LINK_MAX_ARGV);
                argv[a++] = extra_link_inputs[ei];
            }
            argv[a++] = "--start-group";
            /* 内部辅助逻辑 */
            char elibbufs[ZAN_LINK_MAX_LIBS][160]; int neb = 0;
            for (int ei = 0; ei < extra_link_lib_count; ei++) {
                if (a >= ZAN_LINK_MAX_ARGV - ZAN_LINK_ARGV_TAIL || neb >= ZAN_LINK_MAX_LIBS)
                    link_cap_exceeded("linker arguments", ZAN_LINK_MAX_ARGV);
                snprintf(elibbufs[neb], sizeof(elibbufs[neb]), "-l%s", extra_link_libs[ei]);
                argv[a++] = elibbufs[neb++];
            }
            argv[a++] = "-lmingw32"; argv[a++] = "-lgcc";
            argv[a++] = "-lmoldname"; argv[a++] = "-lmingwex";
            argv[a++] = "-lmsvcrt";   argv[a++] = "-lkernel32";
            argv[a++] = "-ladvapi32"; argv[a++] = "-lshell32";
            argv[a++] = "-luser32";   argv[a++] = "-l:libwinpthread.a";
            if (rt_io_obj) argv[a++] = "-lws2_32";
            char libbufs[ZAN_LINK_MAX_LIBS][128]; int nb = 0;
            for (int li = 0; li < irgen.extern_lib_count; li++) {
                if (nb >= ZAN_LINK_MAX_LIBS ||
                    a >= ZAN_LINK_MAX_ARGV - ZAN_LINK_ARGV_TAIL)
                    link_cap_exceeded("DllImport libraries", ZAN_LINK_MAX_LIBS);
                int nlen;
                const char *nm = zan_dllimport_lname(
                    irgen.extern_libs[li].str,
                    (int)irgen.extern_libs[li].len, &nlen);
                if (!nm) continue;
                snprintf(libbufs[nb], sizeof(libbufs[nb]), "-l%.*s", nlen, nm);
                argv[a++] = libbufs[nb++];
            }
            for (int li = 0; li < static_driver_lib_count; li++) {
                if (a >= ZAN_LINK_MAX_ARGV - ZAN_LINK_ARGV_TAIL)
                    link_cap_exceeded("linker arguments", ZAN_LINK_MAX_ARGV);
                argv[a++] = static_driver_libs[li];
            }
            argv[a++] = "--end-group";
            argv[a++] = crtend;
            argv[a] = NULL;
            char lld_path[1200];
            snprintf(lld_path, sizeof(lld_path), "%s\\ld.lld.exe", exe_dir);
            /* 内部辅助实现 */
            const char *linker = rt_mem_obj ? ld_path
                                 : (zan_utf8_get_file_attributes(lld_path) != INVALID_FILE_ATTRIBUTES
                                    ? lld_path : ld_path);
            argv[0] = linker;
            if (getenv("ZAN_LINK_ECHO")) {
                fprintf(stderr, "[link]");
                for (int i = 1; i < a; i++) fprintf(stderr, " %s", argv[i]);
                fprintf(stderr, "\n");
            }
            link_ret = (int)zan_utf8_spawnv(_P_WAIT, linker, argv);
        } else {
            char link_cmd[4096];
            /* 内部辅助逻辑 */
            snprintf(link_cmd, sizeof(link_cmd),
                     "clang --target=x86_64-w64-windows-gnu \"%s\" -o \"%s\" "
                     "-Wl,--stack,268435456%s",
                     generated_objects.paths[0], obj_path,
                     publish_mode ? " -O2 -s" : "");
            for (int oi = 1; oi < generated_objects.count; oi++) {
                cmd_appendf(link_cmd, sizeof(link_cmd), " \"%s\"",
                         generated_objects.paths[oi]);
            }
            if (rt_io_obj) {
                cmd_appendf(link_cmd, sizeof(link_cmd), " \"%s\" -lws2_32", rt_io_obj);
            }
            if (rt_sync_obj) {
                cmd_appendf(link_cmd, sizeof(link_cmd), " \"%s\"", rt_sync_obj);
            }
            if (rt_file_obj) {
                cmd_appendf(link_cmd, sizeof(link_cmd), " \"%s\"", rt_file_obj);
            }
            if (rt_embed_obj) {
                cmd_appendf(link_cmd, sizeof(link_cmd), " \"%s\"", rt_embed_obj);
            }
            if (rt_inflate_obj) {
                cmd_appendf(link_cmd, sizeof(link_cmd), " \"%s\"", rt_inflate_obj);
            }
            if (rt_timer_obj) {
                cmd_appendf(link_cmd, sizeof(link_cmd), " \"%s\"", rt_timer_obj);
            }
            if (rt_mem_obj) {
                cmd_appendf(link_cmd, sizeof(link_cmd),
                         " \"%s\" -Wl,--wrap=malloc -Wl,--wrap=free"
                         " -Wl,--wrap=calloc -Wl,--wrap=realloc", rt_mem_obj);
            }
            for (int di = 0; di < zan_lib_ndirs; di++) {
                cmd_appendf(link_cmd, sizeof(link_cmd), " -L\"%s\"", zan_lib_dirs[di]);
            }
            if (link_subsystem && strcmp(link_subsystem, "windows") == 0) {
                cmd_appendf(link_cmd, sizeof(link_cmd), " -Wl,--subsystem,windows");
            }
            for (int di = 0; di < extra_lib_path_count; di++) {
                cmd_appendf(link_cmd, sizeof(link_cmd), " -L\"%s\"", extra_lib_paths[di]);
            }
            for (int ei = 0; ei < extra_link_input_count; ei++) {
                cmd_appendf(link_cmd, sizeof(link_cmd), " \"%s\"", extra_link_inputs[ei]);
            }
            for (int ei = 0; ei < extra_link_lib_count; ei++) {
                cmd_appendf(link_cmd, sizeof(link_cmd), " -l%s", extra_link_libs[ei]);
            }
            {
                cmd_appendf(link_cmd, sizeof(link_cmd),
                         " -l:libwinpthread.a");
            }
            for (int li = 0; li < irgen.extern_lib_count; li++) {
                int nlen;
                const char *nm = zan_dllimport_lname(
                    irgen.extern_libs[li].str,
                    (int)irgen.extern_libs[li].len, &nlen);
                if (!nm) continue;
                cmd_appendf(link_cmd, sizeof(link_cmd), " -l%.*s", nlen, nm);
            }
            for (int li = 0; li < static_driver_lib_count; li++) {
                cmd_appendf(link_cmd, sizeof(link_cmd), " %s",
                         static_driver_libs[li]);
            }
            if (getenv("ZAN_LINK_ECHO")) fprintf(stderr, "[link] %s\n", link_cmd);
            link_ret = system(link_cmd);
        }
#else
        char link_cmd[4096];
        if (publish_mode) {
            snprintf(link_cmd, sizeof(link_cmd), "cc \"%s\" -o \"%s\" -lm -O2 -s",
                     obj_tmp, obj_path);
        } else {
            snprintf(link_cmd, sizeof(link_cmd), "cc \"%s\" -o \"%s\" -lm",
                     obj_tmp, obj_path);
        }
        if (rt_io_obj) {
            cmd_appendf(link_cmd, sizeof(link_cmd), " \"%s\"", rt_io_obj);
        }
        if (rt_sync_obj) {
#ifdef __APPLE__
            cmd_appendf(link_cmd, sizeof(link_cmd),
                     " \"%s\" -pthread", rt_sync_obj);
#else
            cmd_appendf(link_cmd, sizeof(link_cmd),
                     " \"%s\" -pthread -lrt", rt_sync_obj);
#endif
        }
        if (rt_file_obj) {
#ifdef __APPLE__
            cmd_appendf(link_cmd, sizeof(link_cmd),
                     " \"%s\" -pthread", rt_file_obj);
#else
            cmd_appendf(link_cmd, sizeof(link_cmd),
                     " \"%s\" -pthread", rt_file_obj);
#endif
        }
        if (rt_embed_obj) {
            cmd_appendf(link_cmd, sizeof(link_cmd), " \"%s\"", rt_embed_obj);
        }
        if (rt_inflate_obj) {
            cmd_appendf(link_cmd, sizeof(link_cmd), " \"%s\"", rt_inflate_obj);
        }
        if (rt_timer_obj) {
#ifdef __APPLE__
            cmd_appendf(link_cmd, sizeof(link_cmd),
                     " \"%s\" -pthread", rt_timer_obj);
#else
            cmd_appendf(link_cmd, sizeof(link_cmd),
                     " \"%s\" -pthread -lrt", rt_timer_obj);
#endif
        }
        if (rt_mem_obj) {
#ifdef __APPLE__
            cmd_appendf(link_cmd, sizeof(link_cmd), " \"%s\"", rt_mem_obj);
#else
            cmd_appendf(link_cmd, sizeof(link_cmd),
                     " \"%s\" -Wl,--wrap=malloc -Wl,--wrap=free"
                     " -Wl,--wrap=calloc -Wl,--wrap=realloc", rt_mem_obj);
#endif
        }
        for (int di = 0; di < zan_lib_ndirs; di++) {
            /* 内部辅助逻辑 */
            cmd_appendf(link_cmd, sizeof(link_cmd),
                     " -L\"%s\" -Wl,-rpath,\"%s\"", zan_lib_dirs[di], zan_lib_dirs[di]);
        }
        /* 内部辅助逻辑 */
        if (used_driver_count > 0) {
#ifdef __APPLE__
            cmd_appendf(link_cmd, sizeof(link_cmd),
                     " -Wl,-rpath,@loader_path");
#else
            cmd_appendf(link_cmd, sizeof(link_cmd),
                     " -Wl,-rpath,'$ORIGIN'");
#endif
        }
        /* 内部辅助逻辑 */
        static const char *const win_only_libs[] = {
            "user32", "gdi32", "kernel32", "advapi32", "shell32", "ole32",
            "oleaut32", "comdlg32", "comctl32", "gdiplus", "dwmapi", "shcore",
            "uxtheme", "msimg32", "winmm", "ws2_32", "shlwapi", "opengl32", NULL };
        for (int li = 0; li < irgen.extern_lib_count; li++) {
            const char *lib = irgen.extern_libs[li].str;
            int lib_len = (int)irgen.extern_libs[li].len;
            int skip = 0;
            for (int wi = 0; win_only_libs[wi]; wi++) {
                if ((int)strlen(win_only_libs[wi]) == lib_len &&
                    memcmp(win_only_libs[wi], lib, lib_len) == 0) { skip = 1; break; }
            }
            if (skip) continue;
            /* 内部辅助逻辑 */
            int name_len;
            const char *name = zan_dllimport_lname(lib, lib_len, &name_len);
            if (!name) continue;
            if (target.os == ZAN_OS_MACOS) {
                for (int d = 0; d < used_driver_count; d++) {
                    if (used_driver_runtime[d] ||
                        used_driver_static[d] ||
                        used_driver_len[d] != name_len ||
                        memcmp(used_drivers[d], name,
                               (size_t)name_len) != 0)
                        continue;
                    char primary[1200], fallback[1200];
                    snprintf(primary, sizeof(primary),
                             "%s/lib%.*s.dylib", driver_dirs[d],
                             name_len, name);
                    if (!zan_file_exists(primary) &&
                        zan_find_macos_driver_dylib(
                            driver_dirs[d], name, name_len,
                            fallback, sizeof(fallback))) {
                        cmd_appendf(link_cmd, sizeof(link_cmd),
                                 " \"%s\"", fallback);
                        name = NULL;
                    }
                    break;
                }
                if (!name) continue;
            }
            cmd_appendf(link_cmd, sizeof(link_cmd), " -l%.*s", name_len, name);
        }
        for (int li = 0; li < static_driver_lib_count; li++) {
            cmd_appendf(link_cmd, sizeof(link_cmd), " %s",
                     static_driver_libs[li]);
        }
        /* 模块核心语义抽象与接口调用契约 */
        for (int di = 0; di < extra_lib_path_count; di++) {
            cmd_appendf(link_cmd, sizeof(link_cmd),
                     " -L\"%s\" -Wl,-rpath,\"%s\"", extra_lib_paths[di], extra_lib_paths[di]);
        }
        for (int ei = 0; ei < extra_link_input_count; ei++) {
            cmd_appendf(link_cmd, sizeof(link_cmd), " \"%s\"", extra_link_inputs[ei]);
        }
        for (int ei = 0; ei < extra_link_lib_count; ei++) {
            cmd_appendf(link_cmd, sizeof(link_cmd), " -l%s", extra_link_libs[ei]);
        }
        /* 内部辅助实现 */
        {
            cmd_appendf(link_cmd, sizeof(link_cmd), " -lm");
        }
        link_ret = system(link_cmd);
#endif
        }

        phase("link");
        probe_phase_mem("link");

        generated_object_vec_keep_or_remove(&generated_objects, obj_tmp,
                                            link_ret != 0 &&
                                            getenv("ZAN_KEEP_FAILED_OBJ") != NULL);
        if (icon_obj[0]) remove(icon_obj);

        if (link_ret != 0) {
            fprintf(stderr, "error: linking failed\n");
            zan_diag_free_buffers(irgen.diag);
            zan_irgen_destroy(&irgen);
            zan_arena_free(ir_arena);
            zan_arena_free(arena);
            free(source);
            return 1;
        }

        /* 内部辅助实现 */
        if ((publish_mode || target.os == ZAN_OS_WINDOWS) &&
            used_driver_count > 0) {
            char outdir[1024];
            snprintf(outdir, sizeof(outdir), "%s", obj_path);
            { char *s1 = strrchr(outdir, '/'); char *s2 = strrchr(outdir, '\\');
              char *s = (s1 > s2) ? s1 : s2;
              if (s) *s = '\0'; else snprintf(outdir, sizeof(outdir), "."); }

            bool win_target = (target.os == ZAN_OS_WINDOWS);
            const char *dsub = zan_driver_subdir(&target);
            zan_driver_bundle_context_t bundle_ctx = {
                .reg = &driver_reg, .irgen = &irgen, .prefixes = &driver_prefixes,
                .subdir = dsub, .outdir = outdir, .quiet = quiet
            };
            for (int d = 0; d < used_driver_count; d++) {
                const char *driver_dir = driver_dirs[d];
                if (!driver_dir[0]) continue;
                if (used_driver_static[d] && !used_driver_runtime[d]) continue;
                char drv[64];
                snprintf(drv, sizeof(drv), "%.*s",
                         used_driver_len[d], used_drivers[d]);

                /* 内部辅助逻辑 */
                char cands[64][128]; int ncand = 0;
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
                                              "manifest", 64);
                        }
                        const char *prefix;
                        char *entry = zan_driver_bundle_entry(line, &prefix);
                        if (!entry || (prefix &&
                            !zan_driver_prefix_live(&irgen, &driver_prefixes, prefix)))
                            continue;
                        {
                            if (strncmp(entry, "@driver/", 8) == 0) {
                                /* 内部辅助实现 */
                                const char *dep = entry + 8;
                                size_t dl = strlen(dep);
                                if (dl == 0 || dl >= 64 ||
                                    !zan_is_safe_bundle_name(dep)) {
                                    fprintf(stderr, "warning: ignoring unsafe "
                                        "@driver entry '%s' in %s (must be a "
                                        "bare driver name)\n", entry, manifest);
                                } else if (ncand < 64) {
                                    snprintf(cands[ncand++], sizeof(cands[0]),
                                             "@driver/%s", dep);
                                }
                            } else if (zan_is_safe_bundle_name(entry)) {
                                snprintf(cands[ncand++], sizeof(cands[0]), "%s", entry);
                            } else {
                                fprintf(stderr, "warning: ignoring unsafe entry "
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
                int dself = 0; /* 底层系统交互与数据协议契约 */
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
                                   "executable)\n", drv, cands[c]);
                        copied++;
                        continue;
                    }
                    bool got = false;
                    if (strncmp(cands[c], "@driver/", 8) == 0) {
                        got = zan_bundle_dependency(&bundle_ctx, cands[c] + 8,
                                                     manifest, &copy_failed_count) > 0;
                    } else {
                        snprintf(src, sizeof(src), "%s/%s", driver_dir, cands[c]);
                        snprintf(dst, sizeof(dst), "%s/%s", outdir, cands[c]);
                        if (zan_file_exists(src)) {
                            char errbuf[256];
                            if (zan_copy_file_ex(src, dst, errbuf, sizeof(errbuf)) == 0) {
                                got = true;
                                dself++;
                            } else {
                                fprintf(stderr, "warning: failed to copy driver '%s' file "
                                        "'%s' -> '%s': %s\n", drv, src, dst, errbuf);
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
                    /* 内部辅助逻辑 */
                    if (!quiet)
                        printf("  note: driver '%s' not bundled (%s is empty); the "
                               "program will use a system-installed %s\n",
                               drv, driver_dir, drv);
                } else if (copied == 0 && dself == 0 && !used_driver_runtime[d]) {
                    if (copy_failed_count > 0) {
                        fprintf(stderr,
                            "warning: driver '%s' was not bundled because copy operation(s) failed "
                            "(target file(s) in %s may be locked by another running process, or write permission denied).\n",
                            drv, outdir);
                    } else {
                        fprintf(stderr,
                            "warning: driver '%s' was not bundled (no runtime library "
                            "found in %s). The published program will require '%s' to "
                            "be installed on the target, or add a %s/%s.bundle manifest.\n",
                            drv, driver_dir, drv, driver_dir, drv);
                    }
                }
            }
        }

        /* 模块核心语义抽象与接口调用契约 */
        if (apk_path) {
            const char *abi = (target.arch == ZAN_ARCH_AARCH64)
                              ? "arm64-v8a" : "x86_64";
            /* 模块核心语义抽象与接口调用契约 */
            char pkg[256], lbl[256];
            if (apk_package) {
                if (strlen(apk_package) >= sizeof(pkg)) {
                    fprintf(stderr, "error: --apk-package exceeds %zu bytes\n",
                            sizeof(pkg) - 1);
                    remove(obj_path);
                    zan_diag_free_buffers(irgen.diag);
                    zan_irgen_destroy(&irgen);
                    zan_arena_free(ir_arena);
                    zan_arena_free(arena);
                    free(source);
                    return 1;
                }
                snprintf(pkg, sizeof(pkg), "%s", apk_package);
            } else {
              const char *base = strrchr(input_file, '/');
              const char *base2 = strrchr(input_file, '\\');
              if (base2 > base) base = base2;
              base = base ? base + 1 : input_file;
              snprintf(pkg, sizeof(pkg), "dev.zan.%s", base);
              { char *dot = strrchr(pkg, '.');
                if (dot && strcmp(dot, ".zan") == 0) *dot = 0; }
              /* 底层系统交互与数据协议契约 */
              for (char *c = pkg; *c; c++) {
                  if (!((*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z')
                        || (*c >= '0' && *c <= '9') || *c == '_' || *c == '.'))
                      *c = '_';
              }
            }
            if (apk_label) {
                if (strlen(apk_label) >= sizeof(lbl)) {
                    fprintf(stderr, "error: --apk-label exceeds %zu bytes\n",
                            sizeof(lbl) - 1);
                    remove(obj_path);
                    zan_diag_free_buffers(irgen.diag);
                    zan_irgen_destroy(&irgen);
                    zan_arena_free(ir_arena);
                    zan_arena_free(arena);
                    free(source);
                    return 1;
                }
                snprintf(lbl, sizeof(lbl), "%s", apk_label);
            } else {
              const char *base = strrchr(input_file, '/');
              const char *base2 = strrchr(input_file, '\\');
              if (base2 > base) base = base2;
              base = base ? base + 1 : input_file;
              snprintf(lbl, sizeof(lbl), "%s", base);
              { char *d2 = strrchr(lbl, '.');
                if (d2 && strcmp(d2, ".zan") == 0) *d2 = 0; }
            }
            /* 底层系统交互与数据协议契约 */
            char *extras[256]; int nextra = 0;
            { char outdir_a[1024]; snprintf(outdir_a, sizeof(outdir_a), "%s", obj_path);
              { char *s1 = strrchr(outdir_a, '/'); char *s2 = strrchr(outdir_a, '\\');
                char *s = (s1 > s2) ? s1 : s2; if (s) *s = 0; }
              for (int d = 0; d < used_driver_count && nextra < 256; d++) {
                  const char *dd = driver_dirs[d];
                  if (!dd[0]) continue;
                  char drv[64];
                  snprintf(drv, sizeof(drv), "%.*s", used_driver_len[d], used_drivers[d]);
                  char manifest_p[1300];
                  snprintf(manifest_p, sizeof(manifest_p), "%s/%s.bundle", dd, drv);
                  FILE *mf = fopen(manifest_p, "rb");
                  if (!mf) continue;
                  char line[128];
                  while (fgets(line, sizeof(line), mf) && nextra < 256) {
                      size_t l = strlen(line);
                      while (l > 0 && (line[l-1]=='\n'||line[l-1]=='\r'||line[l-1]==' '||line[l-1]=='\t')) line[--l]=0;
                      if (l == 0) continue;
                      if (!strstr(line, ".so")) continue;
                      static char names[256][128];
                      snprintf(names[nextra], sizeof(names[0]), "%s/%s", dd, line);
                      extras[nextra] = names[nextra];
                      nextra++;
                  }
                  fclose(mf);
              }
            }
            char shell_dir[1300];
            { char exe_dir_a[1024] = {0};
#ifdef _WIN32
              { char mod[1024]; DWORD n = GetModuleFileNameA(NULL, mod, sizeof(mod));
                if (n > 0 && n < sizeof(mod)) { char *s = strrchr(mod, '\\'); if (s) *s = 0; snprintf(exe_dir_a, sizeof(exe_dir_a), "%s", mod); } }
#elif defined(__APPLE__)
              { uint32_t sz = sizeof(exe_dir_a); if (_NSGetExecutablePath(exe_dir_a, &sz) == 0) { char *s = strrchr(exe_dir_a, '/'); if (s) *s = 0; } }
#else
              { ssize_t n = readlink("/proc/self/exe", exe_dir_a, sizeof(exe_dir_a) - 1);
                if (n > 0) { exe_dir_a[n] = 0; char *s = strrchr(exe_dir_a, '/'); if (s) *s = 0; } }
#endif
              snprintf(shell_dir, sizeof(shell_dir), "%s/apk-shell", exe_dir_a);
            }
            if (!quiet)
                printf("  packaging APK ? %s\n", apk_path);
            if (zan_apk_build(apk_path, obj_path, abi, pkg, lbl, shell_dir,
                              extras, nextra,
                              proj_android_perm_count, proj_android_perms) != 0) {
                remove(obj_path);
                zan_diag_free_buffers(irgen.diag);
                zan_irgen_destroy(&irgen);
                zan_arena_free(ir_arena);
                zan_arena_free(arena);
                free(source);
                return 1;
            }
            remove(obj_path); /* 核心系统底层抽象与内存语义契约 */
        }

        if (ipa_path) {
            const char *app_name = ipa_name;
            char def_name[256];
            if (!app_name) {
                const char *base = zan_path_basename(input_file);
                size_t blen = strlen(base);
                if (blen > 4 && strcmp(base + blen - 4, ".zan") == 0) {
                    snprintf(def_name, sizeof(def_name), "%.*s", (int)(blen - 4), base);
                } else {
                    snprintf(def_name, sizeof(def_name), "%s", base);
                }
                app_name = def_name;
            }
            char def_bundle_id[512];
            const char *bundle_id = ipa_bundle_id;
            if (!bundle_id) {
                snprintf(def_bundle_id, sizeof(def_bundle_id), "dev.zan.%s", app_name);
                bundle_id = def_bundle_id;
            }
            if (zan_ipa_build(ipa_path, obj_path, app_name, bundle_id, app_name, "1.0.0") != 0) {
                remove(obj_path);
                zan_diag_free_buffers(irgen.diag);
                zan_irgen_destroy(&irgen);
                zan_arena_free(ir_arena);
                zan_arena_free(arena);
                free(source);
                return 1;
            }
            remove(obj_path); /* 底层系统交互与数据协议契约 */
        }

        if (!quiet) {
            const char *final_out = ipa_path ? ipa_path : (apk_path ? apk_path : obj_path);
            if (input_count == 1) {
                printf("%s '%s' ? '%s'\n", publish_mode ? "Published" : "Compiled", input_file, final_out);
            } else {
                printf("%s %d files ? '%s'\n", publish_mode ? "Published" : "Compiled", input_count, final_out);
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
    /* 内部辅助实现 */
    fflush(stdout);
    fflush(stderr);
    ExitProcess(0);
#else
    LLVMShutdown();
    return 0;
#endif
}

