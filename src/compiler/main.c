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

#define ZAN_LINK_MAX_ARGV        8192
#define ZAN_LINK_MAX_LIBS        2048
#define ZAN_LINK_MAX_DIRS        1024
#define ZAN_MAX_USED_DRIVERS     1024
#define ZAN_MAX_STATIC_DRV_LIBS  2048
#define ZAN_LINK_ARGV_TAIL        32

typedef struct {
    char **paths;
    int count;
    int capacity;
} generated_object_vec_t;

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

static char *read_file_arena(zan_arena_t *arena, const char *path, size_t *out_len) {
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
    if (out_len) *out_len = read;
    return buf;
}

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
static int *input_key_idx = NULL;
static int input_key_idx_cap = 0;
#ifndef _WIN32
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

static int stdlib_has_dir(const char *stdlib_root, const char *subdir) {
    if (!subdir) return 0;
    if (strncmp(subdir, "System", 6) == 0 &&
        (subdir[6] == '\0' || subdir[6] == '/' || subdir[6] == '\\' || subdir[6] == '.')) {
        return 1;
    }
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

static void subdir_to_namespace(const char *subdir, char *out_ns, size_t cap) {
    size_t i = 0;
    for (; subdir[i] && i + 1 < cap; i++) {
        out_ns[i] = (subdir[i] == '/' || subdir[i] == '\\') ? '.' : subdir[i];
    }
    out_ns[i] = '\0';
}

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
    int hierarchical = stdlib_has_dir(stdlib_root, subdir) == 0;
    pkg_auto_include_t args = { files, count, cap };
    int package_count = package_visit_namespace(subdir, package_add_input,
                                                  &args, hierarchical);
    found = found || package_count > 0;
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

static int skip_project_dir(const char *name) {
    static const char *skip[] = {
        ".zan-packages", ".git", ".svn", ".hg", "build", "publish",
        "bin", "obj", "node_modules", "_scratch", "dist", "out", "vendor"
    };
    for (size_t i = 0; i < sizeof(skip) / sizeof(skip[0]); i++)
        if (strcmp(name, skip[i]) == 0) return 1;
    return 0;
}

/* ---- 目录输入：递归展开 .zan 源文件 ------------------------------------
 *
 * 动机：Windows 命令行长度上限约 32K 字符。一个 5618 文件的包把路径摊在
 * 命令行上，CreateProcess 直接起不来（"文件名或扩展名太长"）——而调用方
 * 往往只看到输出里没有 error，就当成编译过了，于是一个**从没被编译过**的
 * 包看起来是健康的（scripts/pkg_sweep.ps1 曾长期这样误报）。
 *
 * 把"编译一个目录"做成编译器的一等公民之后：
 *  - 命令行长度与项目规模解耦（大包一个路径就够，不再需要 @响应文件）；
 *  - 枚举规则只此一份（跳过哪些目录、要不要递归、什么顺序），脚本 / IDE /
 *    CI 不会各自实现一遍而各自漂移。
 *
 * 展开结果按路径字典序排序：目录遍历的原始顺序在 Windows 上不确定，而
 * 输入顺序会影响合并单元里 decl 的先后——可复现构建与差分门都要求稳定。
 */
static int zan_path_cmp(const void *a, const void *b) {
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

static bool zan_path_is_directory(const char *path) {
#ifdef _WIN32
    return zan_utf8_is_directory(path) != 0;
#else
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
#endif
}

static void zan_collect_dir_sources(const char *dir, const char ***files,
                                    int *count, int *cap) {
#ifdef _WIN32
    char pattern[1024];
    snprintf(pattern, sizeof(pattern), "%s\\*", dir);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) continue;
        char path[1024];
        snprintf(path, sizeof(path), "%s\\%s", dir, fd.cFileName);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) continue;
            if (fd.cFileName[0] == '.' || skip_project_dir(fd.cFileName)) continue;
            zan_collect_dir_sources(path, files, count, cap);
        } else {
            size_t nlen = strlen(fd.cFileName);
            if (nlen < 5 || strcmp(fd.cFileName + nlen - 4, ".zan") != 0) continue;
            add_stdlib_input(files, count, cap, path);
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#else
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
        char path[1024];
        snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
        struct stat st;
        if (lstat(path, &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) {
            if (S_ISLNK(st.st_mode)) continue;
            if (e->d_name[0] == '.' || skip_project_dir(e->d_name)) continue;
            zan_collect_dir_sources(path, files, count, cap);
        } else if (S_ISREG(st.st_mode)) {
            size_t nlen = strlen(e->d_name);
            if (nlen < 5 || strcmp(e->d_name + nlen - 4, ".zan") != 0) continue;
            add_stdlib_input(files, count, cap, path);
        }
    }
    closedir(d);
#endif
}

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

        char pkg_dir[1200];
        snprintf(pkg_dir, sizeof(pkg_dir), "%s/packages", package_project_root);
#ifdef _WIN32
        DWORD a_attr = zan_utf8_get_file_attributes(pkg_dir);
        if (a_attr != INVALID_FILE_ATTRIBUTES && (a_attr & FILE_ATTRIBUTE_DIRECTORY)) return;
#else
        struct stat st_pkg;
        if (stat(pkg_dir, &st_pkg) == 0 && S_ISDIR(st_pkg.st_mode)) return;
#endif

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

static char proj_android_package[256];
static char proj_android_label[256];
static bool proj_android_keys_ok = true;
static char proj_android_perms[128][128];
static int proj_android_perm_count = 0;
static char proj_skin_names[128][64];
static int proj_skin_name_count = 0;
static bool proj_skins_enabled = false;
static bool load_proj_android_keys_done = false;
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



#include "main_prune.c"

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
    case AST_DECIMAL_LITERAL:
        printf("DecimalLiteral %.*sm\n", node->str_val.len, node->str_val.str ? node->str_val.str : "0");
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

static const char *zan_dllimport_lname(const char *lib, int lib_len,
                                       int *out_len) {
    if (lib_len == 3 && memcmp(lib, "crt", 3) == 0) return NULL;
    if (lib_len == 6 && memcmp(lib, "msvcrt", 6) == 0) return NULL;
    const char *name = lib;
    int n = lib_len;
    if (n > 3 && memcmp(name, "lib", 3) == 0) { name += 3; n -= 3; }
    if (n == 1 && (name[0] == 'c' || name[0] == 'm')) return NULL;
    if (!zan_dllimport_name_is_safe(name, n)) {
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

#define ZAN_MAX_DRIVERS 1024
typedef struct {
    char lib[64];      /* normalized -l basename, e.g. "sqlite3", "zan_gui" */
    char module[512];
    char sym[64];
    char root[1024];
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
        if (strcmp(reg->entries[i].lib, lib) == 0) return;
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

static int zan_copy_file_ex(const char *src, const char *dst, char *err_buf, size_t err_cap) {
    if (err_buf && err_cap > 0) err_buf[0] = '\0';
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

static bool zan_is_safe_bundle_name(const char *name) {
    if (!name || !name[0]) return false;
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) return false;
    for (const char *p = name; *p; p++) {
        char c = *p;
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') || c == '.' || c == '_' ||
                  c == '-' || c == '+';
        if (!ok) return false;
    }
    return true;
}

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
    if (snapshot) {
        for (const zan_driver_prefix_snapshot_entry_t *p = snapshot->head; p;
             p = p->next) {
            if (p->prefix && strcmp(p->prefix, prefix) == 0) return p->defined;
        }
    }
    return (!snapshot || !snapshot->captured) && zan_irgen_defines_prefix(g, prefix);
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

static bool zan_file_exists(const char *path) {
#ifdef _WIN32
    return zan_utf8_get_file_attributes(path) != INVALID_FILE_ATTRIBUTES;
#else
    return access(path, F_OK) == 0;
#endif
}

static void zan_extract_json_str(const char *line, const char *key, char *out, size_t outsz) {
    out[0] = '\0';
    char needle[64];
    snprintf(needle, sizeof(needle), "\"%s\":\"", key);
    const char *p = strstr(line, needle);
    if (!p) return;
    p += strlen(needle);
    size_t i = 0;
    while (*p && *p != '"' && i + 1 < outsz) {
        if (*p == '\\' && *(p + 1)) p++;
        out[i++] = *p++;
    }
    out[i] = '\0';
}

static int zan_extract_json_int(const char *line, const char *key) {
    char needle[64];
    snprintf(needle, sizeof(needle), "\"%s\":", key);
    const char *p = strstr(line, needle);
    if (!p) return 0;
    p += strlen(needle);
    while (*p == ' ' || *p == '\t') p++;
    return atoi(p);
}

static const char *zan_strcasestr(const char *haystack, const char *needle) {
    if (!haystack || !needle) return NULL;
    if (!*needle) return haystack;
    for (; *haystack; haystack++) {
        if (tolower((unsigned char)*haystack) == tolower((unsigned char)*needle)) {
            const char *h = haystack;
            const char *n = needle;
            while (*h && *n && tolower((unsigned char)*h) == tolower((unsigned char)*n)) {
                h++;
                n++;
            }
            if (!*n) return haystack;
        }
    }
    return NULL;
}

static int zan_query_api(const char *symbol_query) {
    if (!symbol_query || !*symbol_query) {
        fprintf(stderr, "error: --api requires a symbol name or fragment (e.g. 'Button' or 'File.ReadAllText')\n");
        return 1;
    }
    char exe_dir[1024];
    zan_exe_dir(exe_dir, sizeof(exe_dir));

    char cand1[1200], cand2[1200], cand3[256], cand4[256];
    snprintf(cand1, sizeof(cand1), "%s/knowledge/symbols.json", exe_dir);
    snprintf(cand2, sizeof(cand2), "%s/../knowledge/symbols.json", exe_dir);
    snprintf(cand3, sizeof(cand3), ".zanmap/symbols.json");
    snprintf(cand4, sizeof(cand4), "knowledge/symbols.json");

    const char *paths[] = { cand1, cand2, cand3, cand4 };
    const char *found_path = NULL;
    for (size_t i = 0; i < sizeof(paths) / sizeof(paths[0]); i++) {
        if (zan_file_exists(paths[i])) {
            found_path = paths[i];
            break;
        }
    }

    if (!found_path) {
        fprintf(stderr, "error: API symbol index not found (checked knowledge/symbols.json, .zanmap/symbols.json).\n");
        fprintf(stderr, "Hint: Run scripts/gen_knowledge.ps1 to generate the offline API index.\n");
        return 1;
    }

    FILE *f = fopen(found_path, "r");
    if (!f) {
        fprintf(stderr, "error: could not open %s\n", found_path);
        return 1;
    }

    char line[4096];
    int count = 0;
    const int max_results = 50;
    printf("=== API search for '%s' (from %s) ===\n", symbol_query, found_path);

    while (fgets(line, sizeof(line), f)) {
        if (!zan_strcasestr(line, symbol_query)) continue;

        char name[256], kind[64], sig[512], file[512];
        zan_extract_json_str(line, "name", name, sizeof(name));
        zan_extract_json_str(line, "kind", kind, sizeof(kind));
        zan_extract_json_str(line, "sig", sig, sizeof(sig));
        zan_extract_json_str(line, "file", file, sizeof(file));
        int line_no = zan_extract_json_int(line, "line");

        if (!name[0]) continue;

        if (sig[0]) {
            printf("  [%s] %s :: %s  (%s:%d)\n", kind[0] ? kind : "symbol", name, sig, file, line_no);
        } else {
            printf("  [%s] %s  (%s:%d)\n", kind[0] ? kind : "type", name, file, line_no);
        }
        count++;
        if (count >= max_results) {
            printf("  ... (capped at %d results)\n", max_results);
            break;
        }
    }
    fclose(f);
    printf("Total found: %d\n", count);
    return 0;
}

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

static const char *zan_path_basename(const char *p) {
    const char *b = p;
    for (const char *q = p; *q; q++)
        if (*q == '/' || *q == '\\') b = q + 1;
    return b;
}

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
    if (nc == '\0') return 1;
    return nc == '/' || nc == '\\';
}

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
    bool use_comdat = target->os != ZAN_OS_MACOS && target->os != ZAN_OS_IOS;

    LLVMValueRef weak_state[] = {
        g->weak_buckets, g->weak_lock, g->weak_count
    };
    for (size_t i = 0; i < sizeof(weak_state) / sizeof(weak_state[0]); i++)
        zan_runtime_fallback(g, weak_state[i], use_comdat);

    static const char *const co_state[] = {
        "__zan_co_head", "__zan_co_tail", "__zan_co_nodes",
        "__zan_co_slice_start", "__zan_co_poll_tick", "__zan_co_quantum_us"
    };
    if (!g->external_async_executor) {
        for (size_t i = 0; i < sizeof(co_state) / sizeof(co_state[0]); i++)
            zan_runtime_fallback(g, LLVMGetNamedGlobal(g->mod, co_state[i]),
                                 use_comdat);
    }

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

    LLVMValueRef helpers[] = {
        g->rt_println, g->rt_print_int, g->rt_print_uint, g->rt_print_double,
        g->rt_print_float,
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
    intptr_t rc = zan_utf8_spawnv(_P_WAIT, path,
                                  (const char *const *)argv);
    if (rc < 0)
        fprintf(stderr, "error: cannot start archiver '%s': %s\n",
                path, strerror(errno));
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
    if (!env_ar || !*env_ar) env_ar = getenv("LLVM_AR");
    const char *tools[] = {
        env_ar ? env_ar : "",
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


#include "main_options.c"


int main(int argc, char **argv) {
#ifdef _WIN32
    char **utf8_argv = zan_utf8_command_line_argv(&argc);
    if (utf8_argv) argv = utf8_argv;
#endif
    {
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


#include "main_options_parse.c"

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
    if (package_project)
        snprintf(package_project_root, sizeof(package_project_root), "%s",
                 package_project);
    else if (!packages_disabled)
        resolve_package_project_root(input_file);
    for (int fi = 0; fi < input_count; fi++) {
        if (zan_is_zcomp_path(input_files[fi])) continue;
        size_t nlen = 0;
        char *nsrc = read_file(input_files[fi], &nlen);
        if (!nsrc) continue;
        scan_namespace_tokens(nsrc, nlen);
        free(nsrc);
    }
    if (project_root_has_manifest()) {
        scan_project_namespaces(package_project_root);
    }

    zan_target_t target;
    if (target_name) {
        if (!zan_target_parse(target_name, &target)) {
            fprintf(stderr, "error: target '%s' is not available in this build "
                            "(see --list-targets)\n", target_name);
            return 1;
        }
    } else {
        zan_target_host(&target);
    }
    bool cross_compiling = (target_name != NULL);
    bool external_async_executor = (target.os == ZAN_OS_WINDOWS ||
                                    target.os == ZAN_OS_LINUX ||
                                    target.os == ZAN_OS_MACOS ||
                                    target.os == ZAN_OS_IOS) &&
                                   (target.arch == ZAN_ARCH_X86_64 ||
                                    target.arch == ZAN_ARCH_AARCH64);

    if (auto_stdlib || stdlib_path) {
        char stdlib_root[1024];
        if (stdlib_path) {
            snprintf(stdlib_root, sizeof(stdlib_root), "%s", stdlib_path);
        } else {
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

        pi_filter_active = !emit_symbols_path &&
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
                if (zan_is_zcomp_path(input_files[fi])) continue;
                pi_reach_input_dir(input_files[fi]);
                size_t slen3 = 0;
                char *src3 = read_file(input_files[fi], &slen3);
                if (!src3) continue;
                scan_using_tokens(src3, strlen(src3), stdlib_root,
                                  &input_files, &input_count, &input_cap);
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
    /* A382②：--warn-level none|default|error（error 兼容 --deny-warnings）；
     * --suppress 逗号分隔诊断编号（W0003 / 3 皆可，字母仅作文档）。 */
    {
        int warn_level = 1;
        if (warn_level_arg) {
            if (strcmp(warn_level_arg, "none") == 0) warn_level = 0;
            else if (strcmp(warn_level_arg, "default") == 0) warn_level = 1;
            else if (strcmp(warn_level_arg, "error") == 0) {
                warn_level = 2;
                do_deny_warnings = true;
                zan_diag_set_deny_warnings(diag, true);
            } else {
                fprintf(stderr,
                        "error: unknown --warn-level '%s' (none|default|error)\n",
                        warn_level_arg);
                return 2;
            }
        }
        zan_diag_set_warn_level(diag, warn_level);
        if (suppress_arg) {
            const char *p = suppress_arg;
            while (*p) {
                while (*p == ',' || *p == ' ') p++;
                if (!*p) break;
                const char *tok = p;
                while (*p && *p != ',' && *p != ' ') p++;
                int code = 0;
                for (const char *q = tok; q < p; q++) {
                    if (*q >= '0' && *q <= '9') code = code * 10 + (*q - '0');
                }
                if (code > 0) zan_diag_suppress_code(diag, code);
            }
        }
    }
    if (error_limit >= 0) zan_diag_set_max_errors(diag, error_limit);

    zan_ast_node_t *ast = NULL;
    int scanned = 0;
    for (;;) {
        int round_end = input_count;
    for (int fi = scanned; fi < round_end; fi++) {
        if (fi > 0 && zan_is_zcomp_path(input_files[fi])) continue;
        size_t slen = 0;
        char *src = (fi == 0) ? source : read_file_arena(arena, input_files[fi], &slen);
        if (fi == 0) slen = source_len;
        if (!src) {
            fprintf(stderr, "error: cannot read '%s'\n", input_files[fi]);
            zan_arena_free(arena);
            free(source);
            return 1;
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
        if (pi_filter_active) {
            int seed_entry = fi < explicit_input_count &&
                             !zan_is_zcomp_path(input_files[fi]);
            pi_seed_stdlib_input =
                seed_entry ? pi_reach_input_dir(input_files[fi]) : 0;
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
            int changed = pi_close_once(resolved_stdlib_root);
            int fresh = pi_append_included(&input_files, &input_count,
                                           &input_cap);
            if (!changed && !fresh) {
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

        if (pi_filter_active) {
            for (;;) {
                int changed = pi_close_once(resolved_stdlib_root);
                int fresh = pi_append_included(&input_files, &input_count,
                                               &input_cap);
                if (!changed && !fresh) {
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
                    pi_seeding_stdlib =
                        auto_stdlib && resolved_stdlib_root[0] &&
                        zan_path_is_under(input_files[fi],
                                          resolved_stdlib_root);
                    if (diag->error_count > errors_before) {
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
        zan_compile_trace("nsresolve generated");
        zan_nsresolve_run(ast, arena, diag);
        zan_compile_trace("prune");
        zan_nsresolve_prune(ast, arena, diag);
        phase("prune");
        zan_compile_trace("resolve done");
    }

    package_sources_destroy();

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
    const char *irgen_triple = NULL;
    if (cross_compiling) {
        if (target.os == ZAN_OS_WINDOWS)
            irgen_triple = (target.arch == ZAN_ARCH_AARCH64)
                           ? "aarch64-w64-windows-gnu"
                           : "x86_64-w64-windows-gnu";
        else
            irgen_triple = target.triple;
    }
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
        lib_shared = true;
        if (target.os != ZAN_OS_ANDROID) {
            fprintf(stderr, "error: --emit-apk requires --target "
                    "android-x64 or android-arm64\n");
            return 1;
        }
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
    bool check_leaks = check_leaks_opt >= 0 ? check_leaks_opt != 0
                                           : (debug_info && !publish_mode);
    bool arc_guard = arc_guard_opt >= 0 ? arc_guard_opt != 0
                                        : (debug_info && !publish_mode);
    /* 生产级安全基线：无论 debug 还是 publish 模式，
       默认均保留 runtime_checks（数组/切片越界防线、除零保护等）。
       仅当用户显式传递 --no-runtime-checks 时才予以禁用。 */
    if (runtime_checks_opt < 0) {
        runtime_checks = true;
    }
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
    irgen.rt_guard_split = !cross_compiling;
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
    irgen.hide_diagnostics = hide_diagnostics;
    irgen.site_map_path = site_map_path;
    if (site_map_path && site_map_path[0]) {
        irgen.site_map_fp = fopen(site_map_path, "w");
        if (!irgen.site_map_fp) {
            fprintf(stderr, "warning: cannot open site map file for writing: %s\n", site_map_path);
        } else {
            fprintf(irgen.site_map_fp, "# Zan Diagnostic Site Map\n# Input: %s\n", input_file ? input_file : "zan");
        }
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
    bool want_shard = native_arch && !cross_compiling && !shard_opt_out &&
                      ((shard_env && shard_env[0] == '1') || publish_mode);

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

    const char *mf_json_path = getenv("ZAN_CODEGEN_MANIFEST_JSON");
    zan_cg_manifest_t mf;
    bool mf_built = false;
    if (getenv("ZAN_CODEGEN_MANIFEST") || mf_json_path || want_shard) {
        zan_opt_strip_unused(&irgen);
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
        effective_opt = ZAN_OPT_SIZE;
    } else if (!debug_info) {
        effective_opt = ZAN_OPT_FULL;
    }
    if (debug_info && effective_opt > ZAN_OPT_NONE) {
        fprintf(stderr, "note: -g forces -O0 (debug info is emitted unoptimized)\n");
        effective_opt = ZAN_OPT_NONE;
    }
    char obj_path[1024];
    if (ipa_path) {
        snprintf(obj_path, sizeof(obj_path), "%s.macho_tmp", ipa_path);
    } else if (output_file) {
        snprintf(obj_path, sizeof(obj_path), "%s", output_file);
    } else {
        size_t ilen = strlen(input_file);
        const char *def_ext = (target.os == ZAN_OS_WINDOWS) ? ".exe" : "";
        if (ilen > 4 && strcmp(input_file + ilen - 4, ".zan") == 0) {
            snprintf(obj_path, sizeof(obj_path), "%.*s%s", (int)(ilen - 4), input_file, def_ext);
        } else {
            snprintf(obj_path, sizeof(obj_path), "%s%s", input_file,
                     (target.os == ZAN_OS_WINDOWS) ? ".exe" : ".out");
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
        zan_driver_capture_conditions(&irgen, ir_arena, &driver_reg, &target,
                                      driver_dir_override, &driver_prefixes);
    }

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
        if (shard_n == 0)
            zan_irgen_prune_extern_libs(&irgen);
    } else {
        if (!irgen.target_is_wasm)
            irgen.fast_codegen = true;
        if (!do_emit_ir) {
            zan_opt_strip_unused(&irgen);
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
        /* 提示消费方：这份 IR 依赖编译时选定的目标特性，generic CPU 下
           256 位向量路径无法合法化（项目台账 A388）。 */
        if (irgen.target_cpu[0]) {
            fprintf(stderr,
                    "zanc: --emit-ir 输出的 IR 依赖目标特性 cpu=%s features=%s\n"
                    "zanc: 消费方请用同样的特性，例如 llc -mcpu=%s -mattr=%s -filetype=obj out.ll\n",
                    irgen.target_cpu,
                    irgen.target_features[0] ? irgen.target_features : "(default)",
                    irgen.target_cpu,
                    irgen.target_features[0] ? irgen.target_features : "+sse2");
        }
    } else {
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
                if (!nm) continue;
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

        if (cross_compiling && target.os == ZAN_OS_OHOS) {
            char oexe[1024] = {0};
            zan_exe_dir(oexe, sizeof(oexe));
            const char *odrop_sub = (target.arch == ZAN_ARCH_AARCH64)
                                    ? "ohos-arm64" : "ohos-x64";
            zan_driver_registry_t oreg;
            zan_discover_drivers(resolved_stdlib_root, &oreg);
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
                } else if (target.os == ZAN_OS_IOS &&
                           (strncmp(irgen.extern_libs[li].str, "zan_gui", irgen.extern_libs[li].len) == 0 ||
                            strncmp(irgen.extern_libs[li].str, "zan_image", irgen.extern_libs[li].len) == 0 ||
                            strncmp(irgen.extern_libs[li].str, "zan_game", irgen.extern_libs[li].len) == 0 ||
                            strncmp(irgen.extern_libs[li].str, "zan_audio", irgen.extern_libs[li].len) == 0)) {
                    /* iOS 使用静态内嵌的 zanrt_gui.o / zanrt_image.o / zanrt_game.o / zanrt_audio.o，不打桩也不需要外部 dylib */
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

        if (cross_compiling && target.os == ZAN_OS_ANDROID) {
            char aexe[1024] = {0};
            zan_exe_dir(aexe, sizeof(aexe));
            const char *asub = (target.arch == ZAN_ARCH_AARCH64)
                               ? "android-arm64" : "android-x64";
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
        bool used_driver_runtime[ZAN_MAX_USED_DRIVERS] = { false };
        bool used_driver_static[ZAN_MAX_USED_DRIVERS] = { false };
        bool used_driver_embedded[ZAN_MAX_USED_DRIVERS] = { false };
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

        if (target.os == ZAN_OS_WINDOWS && link_subsystem == NULL) {
            bool is_gui_app = false;
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

        if (target.os == ZAN_OS_WINDOWS) {
            char win_exe_dir[1024] = {0};
            zan_exe_dir(win_exe_dir, sizeof(win_exe_dir));
            for (int li = irgen.extern_lib_count - 1; li >= 0; li--) {
                int nlen;
                const char *nm = zan_dllimport_lname(
                    irgen.extern_libs[li].str,
                    (int)irgen.extern_libs[li].len, &nlen);
                if (!nm || zan_win_system_lib(nm, nlen)) continue;
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

        if (resolved_stdlib_root[0] &&
            zan_driver_prefix_live(&irgen, &driver_prefixes, "IconSvgData_")) {
            char icons_dir[1200];
            if (zan_resolve_gui_resource_dir(resolved_stdlib_root, "Gui/icons",
                                             icons_dir, sizeof(icons_dir))) {
                char *icon_spec = (char *)malloc(strlen(icons_dir) + 32);
                if (icon_spec) {
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
                        snprintf(skin_spec, strlen(skins_dir) + 32,
                                 "%s=skins", skins_dir);
                        if (embed_spec_count < ZAN_MAX_EMBED_SPECS) {
                            int spec_at = embed_spec_count++;
                            embed_specs[spec_at] = skin_spec;
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

        if (irgen.uses_embed_api &&
            resolved_stdlib_root[0]) {
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
                if (!seq) {
                    const char *base = zan_path_basename(embed_specs[es]);
                    if (base && strcmp(base, "assets") == 0) assets_staged = true;
                }
            }
            for (int ac = 0; ac < assets_cand_count && !assets_staged; ac++) {
                const char *adir = assets_cands[ac];
                if (!zan_file_exists(adir)) { continue; }
                assets_staged = true;
                char *assets_spec = (char *)malloc(strlen(adir) + 32);
                if (assets_spec) {
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

        zan_irgen_release_llvm(&irgen);
        probe_phase_mem("free llvm");


#include "main_linker.c"


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

        if (apk_path) {
            const char *abi = (target.arch == ZAN_ARCH_AARCH64)
                              ? "arm64-v8a" : "x86_64";
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
            remove(obj_path);
        }

        if (ipa_path) {
            const char *app_name = ipa_name;
            char def_name[256];
            if (!app_name) {
                const char *base = zan_path_basename(ipa_path);
                if (!base || !base[0] || strcmp(base, "out.exe") == 0 || strcmp(base, "main") == 0) {
                    base = zan_path_basename(input_file);
                }
                size_t blen = strlen(base);
                if (blen > 4 && (strcmp(base + blen - 4, ".ipa") == 0 || strcmp(base + blen - 4, ".exe") == 0 || strcmp(base + blen - 4, ".zan") == 0)) {
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
            const char *effective_icon = icon_path;
            if (!effective_icon) {
                const char *candidates[] = {
                    "assets/icon.png",
                    "assets/icon.ico",
                    "icon.png",
                    "icon.ico",
                    NULL
                };
                for (int ci = 0; candidates[ci]; ci++) {
                    FILE *ft = fopen(candidates[ci], "rb");
                    if (ft) {
                        fclose(ft);
                        effective_icon = candidates[ci];
                        break;
                    }
                }
            }
            const char *effective_assets = NULL;
            char assets_cand[1200] = {0};
            if (input_file) {
                char adir[1200];
                snprintf(adir, sizeof(adir), "%s", input_file);
                char *s1 = strrchr(adir, '/');
                char *s2 = strrchr(adir, '\\');
                if (!s1 || (s2 && s2 > s1)) s1 = s2;
                if (s1) {
                    *s1 = 0;
                    snprintf(assets_cand, sizeof(assets_cand), "%s/assets", adir);
                    if (zan_file_exists(assets_cand)) {
                        effective_assets = assets_cand;
                    } else {
                        char *p1 = strrchr(adir, '/');
                        char *p2 = strrchr(adir, '\\');
                        if (!p1 || (p2 && p2 > p1)) p1 = p2;
                        if (p1) {
                            *p1 = 0;
                            snprintf(assets_cand, sizeof(assets_cand), "%s/assets", adir);
                            if (zan_file_exists(assets_cand)) effective_assets = assets_cand;
                        }
                    }
                }
            }
            if (!effective_assets && zan_file_exists("assets")) {
                effective_assets = "assets";
            }
            if (zan_ipa_build(ipa_path, obj_path, app_name, bundle_id, app_name, "1.0.0", effective_icon, effective_assets) != 0) {
                remove(obj_path);
                zan_diag_free_buffers(irgen.diag);
                zan_irgen_destroy(&irgen);
                zan_arena_free(ir_arena);
                zan_arena_free(arena);
                free(source);
                return 1;
            }
            remove(obj_path);
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
    fflush(stdout);
    fflush(stderr);
    ExitProcess(0);
#else
    LLVMShutdown();
    return 0;
#endif
}

