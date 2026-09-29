/* genrun.c -- Zan-scripted code-generator runner (see genrun.h). */

#include "genrun.h"
#include "genmeta.h"
#include "nsresolve.h"
#include "arena.h"
#include "diag.h"
#include "lexer.h"
#include "parser.h"
#include "ast.h"
#include "../common/json.h"

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../common/host_oom.h"

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#include <process.h>
#else
#include <dirent.h>
#include <errno.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#endif
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

int zan_gen_enabled = 1;

/* Captured texts of the generator-produced sources from the latest codegen
 * run (filled by zan_merge_sources). main.c re-seeds the demand-driven
 * stdlib pull-in with them: generated classes reference stdlib types the
 * user program never spells (OrmSelect/OrmMeta/...), so the declaring files
 * must join the parse even though no user token names them. Consumed via
 * zan_gen_take_source_texts, which hands over and clears the capture. */
static char **g_gen_texts = NULL;
static int g_gen_text_count = 0;
static int g_gen_text_cap = 0;

void zan_gen_take_source_texts(char ***texts, int *count) {
    *texts = g_gen_texts;
    *count = g_gen_text_count;
    g_gen_texts = NULL;
    g_gen_text_count = 0;
    g_gen_text_cap = 0;
}

#ifdef _WIN32
#define GEN_DIR_SEP_STR "\\"
#define GEN_EXE_SUFFIX ".exe"
#else
#define GEN_DIR_SEP_STR "/"
#define GEN_EXE_SUFFIX ""
#endif

/* The generator entry source is <stdlib_root>/System/Compiler/ZanGen.zan,
 * compiled with --auto-stdlib, so its whole stdlib closure (System.Web and
 * anything else it reaches) comes along without being listed here; the
 * cache key hashes the stdlib tree instead (see zan_gen_hash_stdlib). */

/* Full path of the running zanc executable (needed to compile the generator
 * with ourselves). */
static void zan_self_exe(char *out, size_t outsz) {
#ifdef _WIN32
    GetModuleFileNameA(NULL, out, (DWORD)outsz);
#elif defined(__APPLE__)
    { uint32_t sz = (uint32_t)outsz;
      if (_NSGetExecutablePath(out, &sz) != 0) out[0] = '\0'; }
#else
    { ssize_t n = readlink("/proc/self/exe", out, outsz - 1);
      if (n > 0) out[n] = '\0'; else out[0] = '\0'; }
#endif
}

int zan_gen_cache_dir(char *dir, size_t dir_size) {
    const char *base;
#ifdef _WIN32
    base = getenv("LOCALAPPDATA");
    if (!base || !*base) return -1;
    if (snprintf(dir, dir_size, "%s\\Zan\\gen", base) <= 0) return -1;
    /* Create every missing level: CreateDirectory only makes the last one, so
     * a machine without %LOCALAPPDATA%\Zan yet got no cache directory at all
     * and the generator compile failed with "cannot emit object file". */
    for (char *p = dir + 1; *p; p++) {
        if (*p != '\\' && *p != '/') continue;
        char sep = *p;
        *p = '\0';
        CreateDirectoryA(dir, NULL); /* ok if it already exists */
        *p = sep;
    }
    if (!CreateDirectoryA(dir, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) {
        return -1;
    }
    return 0;
#else
    base = getenv("XDG_CACHE_HOME");
    if (base && *base) {
        if (snprintf(dir, dir_size, "%s/zan/gen", base) <= 0) return -1;
    } else {
        base = getenv("HOME");
        if (!base || !*base) return -1;
        if (snprintf(dir, dir_size, "%s/.cache/zan/gen", base) <= 0) return -1;
    }
    /* Create every missing level: the cache root itself may not exist yet. */
    for (char *p = dir + 1; *p; p++) {
        if (*p != '/') continue;
        *p = '\0';
        if (mkdir(dir, 0755) != 0 && errno != EEXIST) { *p = '/'; return -1; }
        *p = '/';
    }
    if (mkdir(dir, 0755) != 0 && errno != EEXIST) return -1;
    return 0;
#endif
}

static void zan_gen_hash_bytes(uint64_t *hash, const void *data, size_t len) {
    const unsigned char *p = (const unsigned char *)data;
    for (size_t i = 0; i < len; i++) {
        *hash ^= (uint64_t)p[i];
        *hash *= UINT64_C(1099511628211);
    }
}

static void zan_gen_hash_text(uint64_t *hash, const char *text) {
    zan_gen_hash_bytes(hash, text, strlen(text));
    zan_gen_hash_bytes(hash, "\0", 1);
}

static int zan_gen_hash_file(uint64_t *hash, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    unsigned char buf[32768];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
        zan_gen_hash_bytes(hash, buf, n);
    int ok = ferror(f) ? -1 : 0;
    fclose(f);
    return ok;
}

/* Spawn a child process and wait for it. argv[0] is the program. Returns the
 * exit code, or -1 when the child could not be spawned. The child inherits
 * our stdout/stderr, so its diagnostics pass through untouched. */
static int zan_spawn_wait(char *const argv[]) {
#ifdef _WIN32
    intptr_t r = _spawnv(_P_WAIT, argv[0], (const char *const *)argv);
    if (r < 0) return -1;
    return (int)r;
#else
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        execv(argv[0], argv);
        _exit(127);
    }
    int st = 0;
    while (waitpid(pid, &st, 0) < 0) {
        if (errno != EINTR) return -1;
    }
    if (!WIFEXITED(st)) return -1;
    return WEXITSTATUS(st);
#endif
}

/* ---- cache cleanup ----
 *
 * The generator cache (%LOCALAPPDATA%\Zan\gen on Windows,
 * $XDG_CACHE_HOME/zan/gen on Linux) is shared across every zanc invocation
 * and may accumulate files we never want to keep around:
 *
 *   ZanGen_<hash>_<pid>.exe   per-process compile temp; zanc killed before
 *                              MoveFileExA leaves it behind
 *   gen_codegen_<pid>_in.json  per-process metadata I/O; same orphan problem
 *   gen_codegen_<pid>_out.json
 *   gen_design_<pid>_in.json
 *   gen_design_<pid>_out.json
 *
 * The published ZanGen_<hash>.exe is *not* touched here: it is the warm
 * cache that future invocations read, and the gen_cache_isolation test
 * (tests/run_gen_cache_isolation.cmake) asserts the count of those entries
 * after building two stdlib worktrees. Sweep is best-effort and runs once
 * per zanc process. */

static int zan_pid_alive(uint32_t pid) {
    if (pid == 0) return 0;
#ifdef _WIN32
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE,
                           FALSE, (DWORD)pid);
    if (!h) return 0;
    DWORD wait = WaitForSingleObject(h, 0);
    CloseHandle(h);
    return wait != WAIT_OBJECT_0;
#else
    if (kill((pid_t)pid, 0) == 0) return 1;
    return errno == EPERM;
#endif
}

/* Walk `dir` and call `cb(name, ud)` for every entry (basename only, no path
 * prefix). Skips "." and "..". Best-effort: missing dir is not an error. */
static void zan_gen_scan_dir(const char *dir,
                             void (*cb)(const char *name, void *ud),
                             void *ud) {
#ifdef _WIN32
    char pattern[ZAN_GEN_MAX_PATH];
    snprintf(pattern, sizeof(pattern), "%s\\*", dir);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (fd.cFileName[0] == '.') {
            if (fd.cFileName[1] == '\0' ||
                (fd.cFileName[1] == '.' && fd.cFileName[2] == '\0'))
                continue;
        }
        cb(fd.cFileName, ud);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#else
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.') {
            if (e->d_name[1] == '\0' ||
                (e->d_name[1] == '.' && e->d_name[2] == '\0'))
                continue;
        }
        cb(e->d_name, ud);
    }
    closedir(d);
#endif
}

/* Try to match `s` against `prefix` exactly; if it matches, point `rest` at
 * the first byte after the prefix and return 1. Else return 0. */
static int zan_gen_strip(const char *s, const char *prefix, const char **rest) {
    size_t n = strlen(prefix);
    if (strncmp(s, prefix, n) != 0) return 0;
    *rest = s + n;
    return 1;
}

/* Parse the ZanGen_<hash>[_<pid>] form. `s` points just past "ZanGen_".
 * The hash must be exactly 16 lowercase hex digits. If followed by "_<pid>"
 * (decimal, <= 10 digits, no .exe), the pid is written to *out and the
 * function returns 2. If only the hash is present (optionally followed by
 * ".exe" on Windows), returns 1. Returns 0 if `s` is not a ZanGen name. */
static int zan_gen_parse_zangen(const char *s, uint32_t *out) {
    if (!s) return 0;
    static const char hex[] = "0123456789abcdef";
    if (strlen(s) < 16) return 0;
    for (int i = 0; i < 16; i++) {
        const char *p = strchr(hex, s[i]);
        if (!p) return 0;
    }
    const char *p = s + 16;
    if (*p == '\0') return 1;
    if (*p == '_') {
        const char *q = p + 1;
        const char *end = q;
        while (*end >= '0' && *end <= '9') end++;
        if (end == q || end - q > 10) return 0;
        /* On Windows an optional ".exe" suffix is allowed. */
        if (end[0] == '.') {
            if (end[1] != 'e' || end[2] != 'x' || end[3] != 'e' || end[4])
                return 0;
        } else if (end[0] != '\0') {
            return 0;
        }
        uint64_t v = 0;
        for (const char *r = q; r < end; r++) {
            v = v * 10 + (uint64_t)(*r - '0');
            if (v > 0xFFFFFFFFu) return 0;
        }
        *out = (uint32_t)v;
        return 2;
    }
#ifdef _WIN32
    if (strcmp(p, ".exe") == 0) return 1;
#endif
    return 0;
}

/* Parse a decimal PID from the segment of `s` immediately following the
 * literal prefix it was stripped against. The segment runs from `s` to
 * either the end of the string, an optional ".exe" suffix, or an optional
 * "_in.json"/"_out.json" suffix. Returns 1 on success and writes to *out,
 * else 0. Capped at 10 digits (max uint32). */
static int zan_gen_parse_pid(const char *s, uint32_t *out) {
    if (!s) return 0;
    const char *p = s;
    const char *end = p + strlen(p);
    /* "ZanGen_<hash>_<pid>.exe" or "ZanGen_<hash>_<pid>" */
    if (end - p >= 4 && memcmp(end - 4, ".exe", 4) == 0) end -= 4;
    /* "gen_codegen_<pid>_in.json" / "_out.json" */
    else if (end - p > 8 && memcmp(end - 8, "_in.json", 8) == 0) end -= 8;
    else if (end - p > 9 && memcmp(end - 9, "_out.json", 9) == 0) end -= 9;
    if (end == p) return 0;
    uint64_t v = 0;
    for (const char *q = p; q < end; q++) {
        if (*q < '0' || *q > '9') return 0;
        v = v * 10 + (uint64_t)(*q - '0');
        if (v > 0xFFFFFFFFu) return 0;
    }
    *out = (uint32_t)v;
    return 1;
}

static void zan_gen_sweep_one(const char *name, void *ud) {
    const char *dir = (const char *)ud;
    const char *rest = NULL;
    char path[ZAN_GEN_MAX_PATH];
    uint32_t pid = 0;
#ifdef _WIN32
    const char sep = '\\';
#else
    const char sep = '/';
#endif

    /* ZanGen_<16hex>[.exe] -- published cache, do not touch.
     * ZanGen_<16hex>_<pid>[.exe] -- per-process compile temp; reap if the
     *   holding zanc is gone. */
    if (zan_gen_strip(name, "ZanGen_", &rest)) {
        int kind = zan_gen_parse_zangen(rest, &pid);
        if (kind == 2 && !zan_pid_alive(pid)) {
            snprintf(path, sizeof(path), "%s%c%s", dir, sep, name);
#ifdef _WIN32
            DeleteFileA(path);
#else
            remove(path);
#endif
        }
        return;
    }
    /* gen_codegen_<pid>_in.json / _out.json -- per-process metadata I/O. */
    if (zan_gen_strip(name, "gen_codegen_", &rest)) {
        if (zan_gen_parse_pid(rest, &pid) && !zan_pid_alive(pid)) {
            snprintf(path, sizeof(path), "%s%c%s", dir, sep, name);
#ifdef _WIN32
            DeleteFileA(path);
#else
            remove(path);
#endif
        }
        return;
    }
    if (zan_gen_strip(name, "gen_design_", &rest)) {
        if (zan_gen_parse_pid(rest, &pid) && !zan_pid_alive(pid)) {
            snprintf(path, sizeof(path), "%s%c%s", dir, sep, name);
#ifdef _WIN32
            DeleteFileA(path);
#else
            remove(path);
#endif
        }
        return;
    }
    /* Anything else: leave alone. */
}

static void zan_gen_sweep(const char *dir) {
    static int swept = 0;
    if (swept) return;
    swept = 1;
    zan_gen_scan_dir(dir, zan_gen_sweep_one, (void *)dir);
}

/* The generator's behavior is defined by its whole stdlib closure, not just
 * the System/Compiler sources: ZanGen.zan is compiled with --auto-stdlib, so
 * GenForm/GenHtml freely reach into System.Web (HtmlParser, DesignerHtml)
 * and every other stdlib module. The cache key therefore hashes every .zan
 * under the stdlib root (deterministic order), so any stdlib edit yields a
 * fresh generator image. */

typedef struct zan_gen_strlist {
    char **v;
    size_t n;
    size_t cap;
} zan_gen_strlist;

static void zan_gen_strlist_push(zan_gen_strlist *l, const char *s) {
    if (l->n == l->cap) {
        size_t ncap = l->cap ? l->cap * 2 : 64;
        char **nv = (char **)realloc(l->v, ncap * sizeof(char *));
        if (!nv) return; /* oom: the walk reports fewer files, cache may */
        l->v = nv;       /* collide after an edit - acceptable degradation */
        l->cap = ncap;
    }
    size_t len = strlen(s);
    char *copy = (char *)malloc(len + 1);
    if (!copy) return;
    memcpy(copy, s, len + 1);
    l->v[l->n++] = copy;
}

static int zan_gen_relcmp(const void *a, const void *b) {
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

/* Collect relative paths of every .zan under `root` (any nesting depth).
 * Returns 0 on success, -1 when the root itself cannot be entered. */
static int zan_gen_collect_zan(char *abs, size_t abscap, const char *rel,
                               size_t rellen, zan_gen_strlist *out) {
#ifdef _WIN32
    char pattern[ZAN_GEN_MAX_PATH];
    if (abscap < ZAN_GEN_MAX_PATH) return -1;
    snprintf(pattern, sizeof(pattern), "%s\\*", abs);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return rellen == 0 ? -1 : 0;
    do {
        const char *name = fd.cFileName;
        if (name[0] == '.' && (name[1] == '\0' ||
                               (name[1] == '.' && name[2] == '\0'))) continue;
        size_t alen = strlen(abs);
        size_t nlen = strlen(name);
        if (alen + 1 + nlen + 1 > abscap) continue;
        char relbuf[ZAN_GEN_MAX_PATH];
        snprintf(relbuf, sizeof(relbuf), "%s%s%.*s", rel,
                 rellen ? "/" : "", (int)nlen, name);
        abs[alen] = '\\';
        memcpy(abs + alen + 1, name, nlen + 1);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            zan_gen_collect_zan(abs, abscap, relbuf, strlen(relbuf), out);
        } else if (nlen > 4 && strcmp(name + nlen - 4, ".zan") == 0) {
            zan_gen_strlist_push(out, relbuf);
        }
        abs[alen] = '\0';
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return 0;
#else
    DIR *d = opendir(abs[0] ? abs : ".");
    if (!d) return rellen == 0 ? -1 : 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        const char *name = e->d_name;
        if (name[0] == '.' && (name[1] == '\0' ||
                               (name[1] == '.' && name[2] == '\0'))) continue;
        size_t alen = strlen(abs);
        size_t nlen = strlen(name);
        if (alen + 1 + nlen + 1 > abscap) continue;
        abs[alen] = '/';
        memcpy(abs + alen + 1, name, nlen + 1);
        char relbuf[ZAN_GEN_MAX_PATH];
        struct stat st;
        if (stat(abs, &st) != 0) { abs[alen] = '\0'; continue; }
        if (S_ISDIR(st.st_mode)) {
            snprintf(relbuf, sizeof(relbuf), "%s%s%.*s", rel,
                     rellen ? "/" : "", (int)nlen, name);
            zan_gen_collect_zan(abs, abscap, relbuf, strlen(relbuf), out);
        } else if (nlen > 4 && strcmp(name + nlen - 4, ".zan") == 0) {
            snprintf(relbuf, sizeof(relbuf), "%s%s%.*s", rel,
                     rellen ? "/" : "", (int)nlen, name);
            zan_gen_strlist_push(out, relbuf);
        }
        abs[alen] = '\0';
    }
    closedir(d);
    return 0;
#endif
}

/* Hash every .zan file under the stdlib root into `key`: first the relative
 * path (renames/moves count), then the bytes. */
static int zan_gen_hash_stdlib(uint64_t *key, const char *root) {
    char abs[ZAN_GEN_MAX_PATH];
    snprintf(abs, sizeof(abs), "%s", root);
    zan_gen_strlist list;
    list.v = NULL;
    list.n = 0;
    list.cap = 0;
    if (zan_gen_collect_zan(abs, sizeof(abs), "", 0, &list) != 0) return -1;
    qsort(list.v, list.n, sizeof(char *), zan_gen_relcmp);
    for (size_t i = 0; i < list.n; i++) {
        zan_gen_hash_text(key, list.v[i]);
        char full[ZAN_GEN_MAX_PATH];
        snprintf(full, sizeof(full), "%s%c%s", root, GEN_DIR_SEP_STR[0],
                 list.v[i]);
        if (zan_gen_hash_file(key, full) != 0) {
            for (size_t j = 0; j < list.n; j++) free(list.v[j]);
            free(list.v);
            return -1;
        }
    }
    for (size_t j = 0; j < list.n; j++) free(list.v[j]);
    free(list.v);
    return 0;
}

int zan_gen_ensure(const char *stdlib_root, char *exe, size_t exe_size) {
    if (!zan_gen_enabled) return -1;
    char dir[ZAN_GEN_MAX_PATH];
    if (zan_gen_cache_dir(dir, sizeof(dir)) != 0) {
        fprintf(stderr, "error: no user cache dir for the code generators\n");
        return -1;
    }
    /* Reap orphans from a prior crashed/killed zanc, before we decide to
     * (re)compile or reuse the published image. */
    zan_gen_sweep(dir);

    /* The cache must not be shared merely because timestamps happen to line
     * up. Different worktrees can carry incompatible generator sources whose
     * checkout times are newer than the cached executable. Key the image by
     * compiler bytes, stdlib root and generator contents instead. */
    char zexe[ZAN_GEN_MAX_PATH];
    zan_self_exe(zexe, sizeof(zexe));
    if (!zexe[0]) {
        fprintf(stderr, "error: cannot locate zanc to compile the code generators\n");
        return -1;
    }
    uint64_t key = UINT64_C(1469598103934665603);
    zan_gen_hash_text(&key, "zan-generator-cache-v2");
    zan_gen_hash_text(&key, zexe);
    if (zan_gen_hash_file(&key, zexe) != 0) {
        fprintf(stderr, "error: cannot hash zanc for the code-generator cache\n");
        return -1;
    }
    zan_gen_hash_text(&key, stdlib_root);
    if (zan_gen_hash_stdlib(&key, stdlib_root) != 0) {
        fprintf(stderr, "error: cannot hash stdlib for the code-generator"
                        " cache\n");
        return -1;
    }
    snprintf(exe, exe_size, "%s%sZanGen_%016llx%s", dir, GEN_DIR_SEP_STR,
             (unsigned long long)key, GEN_EXE_SUFFIX);

    if (
#ifdef _WIN32
        GetFileAttributesA(exe) == INVALID_FILE_ATTRIBUTES
#else
        access(exe, F_OK) != 0
#endif
    ) {
        char src[ZAN_GEN_MAX_PATH];
        snprintf(src, sizeof(src), "%s%cSystem%cCompiler%cZanGen.zan",
                 stdlib_root, GEN_DIR_SEP_STR[0], GEN_DIR_SEP_STR[0],
                 GEN_DIR_SEP_STR[0]);
        fprintf(stderr, "zan: compiling code generators (first use; cached at %s)\n", exe);
        /* Compile to a per-process path and rename into place: parallel
         * builds share this cache, and a compile straight onto `exe` makes
         * them fight over the same intermediate object file. */
        char tmp[ZAN_GEN_MAX_PATH];
#ifdef _WIN32
        int pid = (int)_getpid();
#else
        int pid = (int)getpid();
#endif
        snprintf(tmp, sizeof(tmp), "%s%cZanGen_%016llx_%d%s", dir,
                 GEN_DIR_SEP_STR[0], (unsigned long long)key, pid,
                 GEN_EXE_SUFFIX);
        /* --quiet: this is a nested build of our own generator, and the
         * child inherits our stdout. Its "Compiled N files -> ..." progress
         * line would otherwise land on the caller's stdout -- the machine
         * channel `--emit-ir` writes the IR to -- making two emissions of
         * the same source differ (A313). */
        char *argv[] = {
            zexe, src, "--stdlib-path", (char *)stdlib_root, "--auto-stdlib",
            "--no-gen", "--quiet", "-DZAN_GEN_MAIN=1", "-o", tmp, NULL
        };
        int r = zan_spawn_wait(argv);
        if (r != 0) {
            remove(tmp);
            fprintf(stderr, "error: code-generator compile failed (exit %d)\n", r);
            return -1;
        }
#ifdef _WIN32
        if (!MoveFileExA(tmp, exe, MOVEFILE_REPLACE_EXISTING)) {
#else
        if (rename(tmp, exe) != 0) {
#endif
            /* A parallel build compiling the same generator may hold the
             * published image open (Windows cannot replace a running exe).
             * The content-addressed destination proves their image has the
             * same inputs, so drop our copy and reuse it. */
            remove(tmp);
            FILE *published = fopen(exe, "rb");
            if (published) {
                fclose(published);
                return 0;
            }
            fprintf(stderr, "error: cannot publish the compiled code generator\n");
            return -1;
        }
    }
    return 0;
}

int zan_gen_run(const char *exe, const char *meta_path, const char *out_path) {
    char *argv[] = { (char *)exe, (char *)meta_path, (char *)out_path, NULL };
    int r = zan_spawn_wait(argv);
    if (r != 0) {
        fprintf(stderr, "error: code generator failed (exit %d)\n", r);
        return -1;
    }
    return 0;
}

/* ---- design-document translation (the "design" mode) ---- */

/* A design document: generator input, translated to Zan source by
 * zan_gen_design before anything lexes it. Exported so the pre-parse
 * heuristic token scans in main.c can skip it -- lexing raw HTML as Zan
 * cannot produce anything but garbage tokens. */
bool zan_is_design_path(const char *p) {
    size_t n = strlen(p);
    return (n > 7 && strcmp(p + n - 7, ".zscene") == 0) ||
           (n > 5 && strcmp(p + n - 5, ".html") == 0) ||
           (n > 4 && strcmp(p + n - 4, ".htm") == 0);
}

/* A saved user component (designer "save as component"): pure design data
 * the generators expand into every referencing design doc. Not a translatable
 * design document itself, and never parsed as Zan source. */
bool zan_is_zcomp_path(const char *p) {
    size_t n = strlen(p);
    return n > 6 && strcmp(p + n - 6, ".zcomp") == 0;
}

static char *zan_read_file(const char *path, size_t *len_out) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long n = ftell(f);
    if (n < 0 || fseek(f, 0, SEEK_SET) != 0) { fclose(f); return NULL; }
    char *buf = (char *)malloc((size_t)n + 1);
    if (!buf) { fclose(f); return NULL; }
    if (n > 0 && fread(buf, 1, (size_t)n, f) != (size_t)n) {
        free(buf); fclose(f); return NULL;
    }
    buf[n] = '\0';
    fclose(f);
    if (len_out) *len_out = (size_t)n;
    return buf;
}

char **zan_gen_design(const char *stdlib_root, const char *const *paths,
                      size_t count) {
    char **outs = (char **)calloc(count ? count : 1, sizeof(char *));
    if (!outs) {
        fprintf(stderr, "error: out of memory\n");
        return NULL;
    }

    /* Collect the design inputs; nothing to run without any. */
    int ndesign = 0;
    for (size_t i = 0; i < count; i++)
        if (zan_is_design_path(paths[i])) ndesign++;
    if (ndesign == 0) return outs;
    if (!zan_gen_enabled) {
        fprintf(stderr,
                "error: design document input needs the code generators, "
                "which --no-gen disables\n");
        free(outs);
        return NULL;
    }
    if (!stdlib_root || !stdlib_root[0]) {
        fprintf(stderr,
                "error: design document input needs the standard library "
                "to compile the code generators\n");
        free(outs);
        return NULL;
    }

    char dir[ZAN_GEN_MAX_PATH];
    if (zan_gen_cache_dir(dir, sizeof(dir)) != 0) {
        fprintf(stderr, "error: no user cache dir for the code generators\n");
        free(outs);
        return NULL;
    }
    char exe[ZAN_GEN_MAX_PATH];
    if (zan_gen_ensure(stdlib_root, exe, sizeof(exe)) != 0) {
        free(outs);
        return NULL;
    }

    /* Build the design request: { mode, files: [{ name, text, emitMain }] }.
     * `name` is the full input path; the generator echoes it back verbatim in
     * each reply source, which is how the texts are matched to inputs. */
    json_value *files = json_new_arr();
    for (size_t i = 0; i < count; i++) {
        if (!zan_is_design_path(paths[i])) continue;
        size_t tlen = 0;
        char *text = zan_read_file(paths[i], &tlen);
        if (!text) {
            fprintf(stderr, "error: cannot read '%s'\n", paths[i]);
            json_free(files);
            free(outs);
            return NULL;
        }
        /* A design document is JSON, and JSON must not start with a byte order
         * mark: an editor that saves the design doc as "UTF-8 with BOM"
         * (Notepad, PowerShell's Set-Content) would otherwise make the
         * generator fail with "cannot translate design document". Drop it. */
        const char *body = text;
        if (tlen >= 3 && (unsigned char)body[0] == 0xEF &&
            (unsigned char)body[1] == 0xBB && (unsigned char)body[2] == 0xBF)
            body += 3;
        json_value *f = json_new_obj();
        json_obj_set(f, "name", json_new_str(paths[i]));
        json_obj_set(f, "text", json_new_str(body));
        json_obj_set(f, "emitMain", json_new_bool(i == 0));
        json_arr_add(files, f);
        free(text);
    }
    /* Saved user components ride the same input list; the generator indexes
     * them by file base name and expands every "ref" that points at one. */
    json_value *comps = json_new_arr();
    for (size_t i = 0; i < count; i++) {
        if (!zan_is_zcomp_path(paths[i])) continue;
        size_t clen = 0;
        char *ctext = zan_read_file(paths[i], &clen);
        if (!ctext) {
            fprintf(stderr, "error: cannot read '%s'\n", paths[i]);
            json_free(files);
            json_free(comps);
            free(outs);
            return NULL;
        }
        const char *cbody = ctext;
        if (clen >= 3 && (unsigned char)cbody[0] == 0xEF &&
            (unsigned char)cbody[1] == 0xBB && (unsigned char)cbody[2] == 0xBF)
            cbody += 3;
        json_value *c = json_new_obj();
        json_obj_set(c, "name", json_new_str(paths[i]));
        json_obj_set(c, "text", json_new_str(cbody));
        json_arr_add(comps, c);
        free(ctext);
    }
    json_value *req = json_new_obj();
    json_obj_set(req, "mode", json_new_str("design"));
    json_obj_set(req, "files", files);
    json_obj_set(req, "components", comps);
    char *meta = json_serialize(req);
    json_free(req);

    char meta_path[ZAN_GEN_MAX_PATH], out_path[ZAN_GEN_MAX_PATH];
#ifdef _WIN32
    int pid = (int)_getpid();
#else
    int pid = (int)getpid();
#endif
    snprintf(meta_path, sizeof(meta_path), "%s%cgen_design_%d_in.json",
             dir, GEN_DIR_SEP_STR[0], pid);
    snprintf(out_path, sizeof(out_path), "%s%cgen_design_%d_out.json",
             dir, GEN_DIR_SEP_STR[0], pid);

    int ok = 0;
    FILE *mf = fopen(meta_path, "wb");
    if (!mf || !meta ||
        fwrite(meta, 1, strlen(meta), mf) != strlen(meta)) {
        fprintf(stderr, "error: cannot write generator request\n");
        if (mf) fclose(mf);
        ok = -1;
    } else if (fclose(mf) != 0) {
        /* the FILE* is already closed here; closing it again is UB */
        fprintf(stderr, "error: cannot write generator request\n");
        ok = -1;
    }
    free(meta);
    if (ok == 0 && zan_gen_run(exe, meta_path, out_path) != 0) ok = -1;

    if (ok == 0) {
        size_t olen = 0;
        char *reply = zan_read_file(out_path, &olen);
        json_value *root = reply ? json_parse(reply) : NULL;
        if (!root) {
            fprintf(stderr, "error: invalid generator reply (design)\n");
            ok = -1;
        } else {
            json_value *err = json_obj_get(root, "error");
            if (err && err->type == JSON_STR && err->as.str && err->as.str[0]) {
                fprintf(stderr, "error: %s\n", err->as.str);
                ok = -1;
            } else {
                json_value *sources = json_obj_get(root, "sources");
                if (!sources || sources->type != JSON_ARR) {
                    fprintf(stderr, "error: generator reply has no sources\n");
                    ok = -1;
                } else {
                    for (int si = 0; si < sources->as.arr.count; si++) {
                        json_value *src = sources->as.arr.items[si];
                        const char *name = json_get_str(json_obj_get(src, "name"));
                        const char *text = json_get_str(json_obj_get(src, "text"));
                        if (!name || !text) continue;
                        for (size_t i = 0; i < count; i++) {
                            if (outs[i]) continue;
                            if (strcmp(paths[i], name) == 0) {
                                outs[i] = strdup(text);
                                break;
                            }
                        }
                    }
                }
            }
            json_free(root);
        }
        free(reply);
    }

    /* ZAN_KEEP_GEN_REQ=1 keeps the request/reply pair for debugging a
     * generator mismatch (default: removed). */
    if (getenv("ZAN_KEEP_GEN_REQ") == NULL) {
        remove(meta_path);
        remove(out_path);
    }
    if (ok != 0) {
        for (size_t i = 0; i < count; i++) free(outs[i]);
        free(outs);
        return NULL;
    }
    return outs;
}

/* ---- codegen mode (jsongen/dbgen/routegen) ---- */

/* ---- AST-level codegen triggers (zero-allocation fast path) ----
 *
 * Checks whether any call site or declaration in the compilation unit matches
 * the shapes expected by jsongen, routegen, or dbgen. By evaluating directly
 * on the AST, we completely bypass exporting and parsing multi-megabyte JSON
 * metadata when codegen is not needed, and avoid a second json_parse DOM tree
 * when it is needed. */

static bool ast_call_triggers(zan_ast_node_t *call) {
    if (!call || call->kind != AST_CALL || !call->call.callee)
        return false;
    zan_ast_node_t *callee = call->call.callee;
    const char *name = NULL;
    const char *recv_name = NULL;

    if (callee->kind == AST_MEMBER_ACCESS) {
        if (callee->member.name.str)
            name = callee->member.name.str;
        if (callee->member.object && callee->member.object->kind == AST_IDENTIFIER)
            recv_name = callee->member.object->ident.name.str;
    } else if (callee->kind == AST_IDENTIFIER) {
        if (callee->ident.name.str)
            name = callee->ident.name.str;
    }

    if (!name) return false;

    /* 1. Json trigger: Json.Serialize / Json.Deserialize */
    if (strcmp(name, "Deserialize") == 0 || strcmp(name, "Serialize") == 0) {
        if (recv_name && strcmp(recv_name, "Json") == 0) return true;
        if (call->call.type_args.count > 0) return true;
    }

    /* 2. ORM generic roots: db.Insert<T>, db.Select<T>, db.Update<T>, etc.
     * Ordinary collection calls like `list.Insert(idx, val)` have 0 type_args. */
    if (call->call.type_args.count > 0) {
        if (strcmp(name, "Insert") == 0 ||
            strcmp(name, "Update") == 0 ||
            strcmp(name, "Delete") == 0 ||
            strcmp(name, "Select") == 0 ||
            strcmp(name, "Query") == 0 ||
            strcmp(name, "SyncStructure") == 0 ||
            strcmp(name, "SyncStructureAsync") == 0 ||
            strcmp(name, "SyncStructureAll") == 0 ||
            strcmp(name, "SyncStructureAllAsync") == 0) {
            fprintf(stderr, "TRIGGER by ORM generic: %s\n", name);
            return true;
        }
    }

    return false;
}

static bool ast_expr_triggers(zan_ast_node_t *n);
static bool ast_stmt_triggers(zan_ast_node_t *n);

static bool ast_expr_triggers(zan_ast_node_t *n) {
    if (!n) return false;
    switch (n->kind) {
    case AST_CALL:
        if (ast_call_triggers(n)) return true;
        if (n->call.callee && ast_expr_triggers(n->call.callee)) return true;
        for (int i = 0; i < n->call.args.count; i++)
            if (ast_expr_triggers(n->call.args.items[i])) return true;
        for (int i = 0; i < n->call.type_args.count; i++)
            if (ast_expr_triggers(n->call.type_args.items[i])) return true;
        break;
    case AST_BINARY:
    case AST_ASSIGNMENT:
        if (ast_expr_triggers(n->binary.left)) return true;
        if (ast_expr_triggers(n->binary.right)) return true;
        break;
    case AST_UNARY:
    case AST_POSTFIX_UNARY:
        if (ast_expr_triggers(n->unary.operand)) return true;
        break;
    case AST_MEMBER_ACCESS:
        if (ast_expr_triggers(n->member.object)) return true;
        break;
    case AST_INDEX:
        if (ast_expr_triggers(n->index.object)) return true;
        if (ast_expr_triggers(n->index.index)) return true;
        for (int i = 0; i < n->index.extra.count; i++)
            if (ast_expr_triggers(n->index.extra.items[i])) return true;
        break;
    case AST_CONDITIONAL:
        if (ast_expr_triggers(n->conditional.cond)) return true;
        if (ast_expr_triggers(n->conditional.then_expr)) return true;
        if (ast_expr_triggers(n->conditional.else_expr)) return true;
        break;
    case AST_NEW_EXPR:
        if (ast_expr_triggers(n->new_expr.call_init)) return true;
        for (int i = 0; i < n->new_expr.args.count; i++)
            if (ast_expr_triggers(n->new_expr.args.items[i])) return true;
        for (int i = 0; i < n->new_expr.arg_inits.count; i++)
            if (ast_expr_triggers(n->new_expr.arg_inits.items[i])) return true;
        break;
    case AST_COLL_INIT:
        for (int i = 0; i < n->coll_init.items.count; i++)
            if (ast_expr_triggers(n->coll_init.items.items[i])) return true;
        break;
    case AST_CAST_EXPR:
        if (ast_expr_triggers(n->cast.expr)) return true;
        break;
    case AST_IS_EXPR:
    case AST_AS_EXPR:
        if (ast_expr_triggers(n->type_test.expr)) return true;
        break;
    case AST_LAMBDA:
        if (ast_expr_triggers(n->lambda.body)) return true;
        break;
    case AST_AWAIT_EXPR:
        if (ast_expr_triggers(n->await_expr.expr)) return true;
        break;
    case AST_REF_ARG:
        if (ast_expr_triggers(n->ref_arg.expr)) return true;
        break;
    case AST_STRING_INTERP:
        for (int i = 0; i < n->string_interp.parts.count; i++)
            if (ast_expr_triggers(n->string_interp.parts.items[i])) return true;
        break;
    default:
        break;
    }
    return false;
}

static bool ast_stmt_triggers(zan_ast_node_t *n) {
    if (!n) return false;
    switch (n->kind) {
    case AST_BLOCK:
        for (int i = 0; i < n->block.stmts.count; i++)
            if (ast_stmt_triggers(n->block.stmts.items[i])) return true;
        break;
    case AST_VAR_DECL:
        if (ast_expr_triggers(n->var_decl.initializer)) return true;
        break;
    case AST_EXPR_STMT:
        if (ast_expr_triggers(n->expr_stmt.expr)) return true;
        break;
    case AST_RETURN_STMT:
        if (ast_expr_triggers(n->ret.value)) return true;
        break;
    case AST_IF_STMT:
        if (ast_expr_triggers(n->if_stmt.cond)) return true;
        if (ast_stmt_triggers(n->if_stmt.then_body)) return true;
        if (ast_stmt_triggers(n->if_stmt.else_body)) return true;
        break;
    case AST_WHILE_STMT:
    case AST_DO_WHILE_STMT:
        if (ast_expr_triggers(n->while_stmt.cond)) return true;
        if (ast_stmt_triggers(n->while_stmt.body)) return true;
        break;
    case AST_FOR_STMT:
        if (ast_stmt_triggers(n->for_stmt.init)) return true;
        if (ast_expr_triggers(n->for_stmt.cond)) return true;
        if (ast_expr_triggers(n->for_stmt.step)) return true;
        if (ast_stmt_triggers(n->for_stmt.body)) return true;
        break;
    case AST_FOREACH_STMT:
        if (ast_expr_triggers(n->foreach_stmt.collection)) return true;
        if (ast_stmt_triggers(n->foreach_stmt.body)) return true;
        break;
    case AST_THROW_STMT:
        if (ast_expr_triggers(n->throw_stmt.value)) return true;
        break;
    case AST_TRY_STMT:
        if (ast_stmt_triggers(n->try_stmt.try_body)) return true;
        for (int i = 0; i < n->try_stmt.catches.count; i++)
            if (ast_stmt_triggers(n->try_stmt.catches.items[i])) return true;
        if (ast_stmt_triggers(n->try_stmt.finally_body)) return true;
        break;
    case AST_CATCH_CLAUSE:
        if (ast_stmt_triggers(n->catch_clause.body)) return true;
        break;
    case AST_SWITCH_STMT:
        if (ast_expr_triggers(n->switch_stmt.expr)) return true;
        for (int i = 0; i < n->switch_stmt.cases.count; i++)
            if (ast_stmt_triggers(n->switch_stmt.cases.items[i])) return true;
        break;
    case AST_SWITCH_CASE:
        if (ast_expr_triggers(n->switch_case.pattern)) return true;
        if (ast_stmt_triggers(n->switch_case.body)) return true;
        break;
    case AST_LOCK_STMT:
        if (ast_expr_triggers(n->lock_stmt.expr)) return true;
        if (ast_stmt_triggers(n->lock_stmt.body)) return true;
        break;
    case AST_CHECKED_STMT:
        if (ast_stmt_triggers(n->checked_stmt.body)) return true;
        break;
    case AST_YIELD_STMT:
        if (ast_expr_triggers(n->yield_stmt.value)) return true;
        break;
    default:
        if (ast_expr_triggers(n)) return true;
        break;
    }
    return false;
}

static bool ast_type_triggers(zan_ast_node_t *decl) {
    if (!decl || (decl->kind != AST_CLASS_DECL && decl->kind != AST_STRUCT_DECL))
        return false;
    /* 1. Name ends with "Controller" */
    const char *name = decl->type_decl.name.str;
    size_t nlen = decl->type_decl.name.len;
    if (name && nlen >= 10 && memcmp(name + nlen - 10, "Controller", 10) == 0) {
        return true;
    }
    /* 2. Base classes: Controller or ApiController */
    for (int i = 0; i < decl->type_decl.bases.count; i++) {
        zan_ast_node_t *b = decl->type_decl.bases.items[i];
        if (!b) continue;
        const char *bname = NULL;
        if (b->kind == AST_IDENTIFIER) bname = b->ident.name.str;
        else if (b->kind == AST_QUALIFIED_NAME && b->qualified_name.parts.count > 0) {
            zan_ast_node_t *last = b->qualified_name.parts.items[b->qualified_name.parts.count - 1];
            if (last && last->kind == AST_IDENTIFIER) bname = last->ident.name.str;
        }
        if (bname && (strcmp(bname, "Controller") == 0 || strcmp(bname, "ApiController") == 0)) {
            return true;
        }
    }
    /* 3. Attributes: Route, ApiController, Table */
    zan_ast_list_t *attrs = zan_ast_attributes(decl);
    if (attrs) {
        for (int i = 0; i < attrs->count; i++) {
            zan_ast_node_t *a = attrs->items[i];
            if (!a || a->kind != AST_ATTRIBUTE) continue;
            const char *an = NULL;
            if (a->attribute.name->kind == AST_IDENTIFIER)
                an = a->attribute.name->ident.name.str;
            if (an && (strcmp(an, "Route") == 0 ||
                       strcmp(an, "ApiController") == 0 ||
                       strcmp(an, "Table") == 0)) {
                return true;
            }
        }
    }
    return false;
}

static bool zan_gen_ast_triggered(zan_ast_node_t *unit) {
    if (!unit || unit->kind != AST_COMPILATION_UNIT) return false;
    for (int i = 0; i < unit->comp_unit.decls.count; i++) {
        zan_ast_node_t *decl = unit->comp_unit.decls.items[i];
        if (!decl) continue;
        if (ast_type_triggers(decl)) return true;
        if (decl->kind == AST_CLASS_DECL || decl->kind == AST_STRUCT_DECL) {
            for (int j = 0; j < decl->type_decl.members.count; j++) {
                zan_ast_node_t *m = decl->type_decl.members.items[j];
                if (!m) continue;
                if (m->kind == AST_METHOD_DECL || m->kind == AST_CONSTRUCTOR_DECL) {
                    if (ast_stmt_triggers(m->method_decl.body)) return true;
                } else if (m->kind == AST_FIELD_DECL || m->kind == AST_PROPERTY_DECL) {
                    if (ast_expr_triggers(m->field_decl.initializer)) return true;
                }
            }
        }
    }
    return false;
}

/* ---- rewrite directives ----
 *
 * The generators decide everything (framework names, SQL fragments, bind
 * lists, Expr<T> trees); the compiler is only the "surgeon": locate the
 * call site by id and mechanically rebuild the AST per the directive.
 *
 *   json_call   {id, callee}        retarget callee to __JsonBind.<callee>,
 *                                   drop type args
 *   db_root     {id, name, extra}   `recv.Xxx<T>(a)` -> `__DbBind.<name>(recv[, a])`
 *   db_acc_head {id, name, conn}    `<acc>.<Entity>` chain head ->
 *                                   `__DbBind.<name>(<conn>)` on the receiver
 *   db_acc_root {id, tree}          whole call replaced by <tree>
 *   db_repo_call {id, tree}          entity accessor call replaced by a
 *                                   compile-time-resolved DAO call tree
 *   db_chain    {id, ops:[{m,args}]} chain `<recv>.<m>(args)...`, replaces call
 *   db_expr_arg {id, param, tree}   args[param] replaced by <tree>
 *
 * Directives execute in descending id order, matching the C dbgen's
 * children-first visit: the chain root (`__DbBind.Q_T`) is rewritten before
 * the chain methods above it look for it. `tree` fields are expression trees
 * serialized by the exporter (genmeta.c) and rebuilt here verbatim. */

static zan_ast_node_t *rw_ident(zan_arena_t *arena, zan_loc_t loc,
                                const char *name) {
    zan_ast_node_t *n = zan_ast_new(arena, AST_IDENTIFIER, loc);
    size_t l = strlen(name);
    n->ident.name.str = zan_arena_strdup(arena, name, l);
    n->ident.name.len = (uint32_t)l;
    return n;
}

static zan_ast_node_t *rw_member(zan_arena_t *arena, zan_loc_t loc,
                                 zan_ast_node_t *obj, const char *name) {
    zan_ast_node_t *n = zan_ast_new(arena, AST_MEMBER_ACCESS, loc);
    n->member.object = obj;
    size_t l = strlen(name);
    n->member.name.str = zan_arena_strdup(arena, name, l);
    n->member.name.len = (uint32_t)l;
    n->member.null_cond = 0;
    return n;
}

static zan_ast_node_t *rw_call(zan_arena_t *arena, zan_loc_t loc,
                               zan_ast_node_t *callee, json_value *args) {
    zan_ast_node_t *n = zan_ast_new(arena, AST_CALL, loc);
    n->call.callee = callee;
    zan_ast_list_init(&n->call.args);
    zan_ast_list_init(&n->call.type_args);
    if (args && args->type == JSON_ARR) {
        for (int i = 0; i < args->as.arr.count; i++) {
            zan_ast_node_t *a =
                zan_genmeta_expr_from_json(args->as.arr.items[i], arena);
            if (a) zan_ast_list_push(&n->call.args, a, arena);
        }
    }
    return n;
}

static void rw_set_member_name(zan_arena_t *arena, zan_ast_node_t *ce,
                               const char *name) {
    size_t l = strlen(name);
    ce->member.name.str = zan_arena_strdup(arena, name, l);
    ce->member.name.len = (uint32_t)l;
}

static void rw_set_ident(zan_arena_t *arena, zan_ast_node_t *id,
                         const char *name) {
    size_t l = strlen(name);
    id->ident.name.str = zan_arena_strdup(arena, name, l);
    id->ident.name.len = (uint32_t)l;
}

/* Ascending-id order: call-site ids are assigned children-first (the same
 * order the old C dbgen visits), so ascending replays the exact rewrite sequence of
 * the C generators. The array is tiny, insertion sort is fine. */
static void rw_sort_desc(json_value *rw, int *order, int count) {
    for (int i = 1; i < count; i++) {
        int v = order[i];
        int j = i - 1;
        while (j >= 0 && json_get_num(
               json_obj_get(rw->as.arr.items[order[j]], "id"), 0) >
               json_get_num(json_obj_get(rw->as.arr.items[v], "id"), 0)) {
            order[j + 1] = order[j];
            j--;
        }
        order[j + 1] = v;
    }
}

static void zan_apply_rewrites(zan_ast_node_t *unit, json_value *rw,
                               zan_arena_t *arena) {
    if (!rw || rw->type != JSON_ARR) return;
    int count = rw->as.arr.count;
    if (count == 0) return;
    int *order = (int *)malloc((size_t)count * sizeof(int));
    if (!order) return;
    for (int i = 0; i < count; i++) order[i] = i;
    rw_sort_desc(rw, order, count);

    /* Snapshot the call sites before rewriting anything: rewrites splice in
     * new call nodes (e.g. `__DbBind.Q_T(...)`), which would shift the
     * walking counter a fresh find_call relies on. Node *pointers* never
     * change, so the snapshot stays exact for the whole pass. */
    int total = zan_genmeta_index_calls(unit, NULL, 0);
    zan_ast_node_t **idx = NULL;
    if (total > 0) {
        idx = (zan_ast_node_t **)malloc((size_t)total * sizeof(zan_ast_node_t *));
        if (idx) zan_genmeta_index_calls(unit, idx, total);
    }

    for (int i = 0; i < count; i++) {
        json_value *op = rw->as.arr.items[order[i]];
        const char *opname = json_get_str(json_obj_get(op, "op"));
        if (!opname) continue;
        int id = (int)json_get_num(json_obj_get(op, "id"), 0);
        zan_ast_node_t *call =
            (idx && id >= 1 && id <= total) ? idx[id - 1] : NULL;
        if (!call || call->kind != AST_CALL) continue;
        zan_ast_node_t *ce = call->call.callee;
        zan_loc_t loc = call->loc;

        if (strcmp(opname, "json_call") == 0) {
            const char *callee = json_get_str(json_obj_get(op, "callee"));
            if (!callee || !ce || ce->kind != AST_MEMBER_ACCESS) continue;
            zan_ast_node_t *obj = ce->member.object;
            if (!obj || obj->kind != AST_IDENTIFIER) continue;
            rw_set_ident(arena, obj, "__JsonBind");
            rw_set_member_name(arena, ce, callee);
            call->call.type_args.count = 0;
            continue;
        }

        if (strcmp(opname, "db_root") == 0) {
            const char *name = json_get_str(json_obj_get(op, "name"));
            if (!name || !ce || ce->kind != AST_MEMBER_ACCESS) continue;
            zan_ast_node_t *recv = ce->member.object;
            json_value *extra = json_obj_get(op, "extra");
            bool keep_extra = json_get_bool(extra, false) &&
                              call->call.args.count > 0;
            zan_ast_node_t *ex =
                keep_extra ? call->call.args.items[0] : NULL;
            /* a fresh node replaces the callee object; `recv` (the original
             * receiver) must stay intact because it becomes the first
             * argument below */
            ce->member.object = rw_ident(arena, loc, "__DbBind");
            rw_set_member_name(arena, ce, name);
            call->call.type_args.count = 0;
            zan_ast_list_init(&call->call.args);
            zan_ast_list_push(&call->call.args, recv, arena);
            if (ex) zan_ast_list_push(&call->call.args, ex, arena);
            continue;
        }

        if (strcmp(opname, "db_acc_head") == 0) {
            const char *name = json_get_str(json_obj_get(op, "name"));
            json_value *conn = json_obj_get(op, "conn");
            if (!name || !ce || ce->kind != AST_MEMBER_ACCESS) continue;
            zan_ast_node_t *b = rw_ident(arena, loc, "__DbBind");
            zan_ast_node_t *m = rw_member(arena, loc, b, name);
            zan_ast_node_t *c = zan_ast_new(arena, AST_CALL, loc);
            c->call.callee = m;
            zan_ast_list_init(&c->call.args);
            zan_ast_list_init(&c->call.type_args);
            zan_ast_node_t *conn_n = conn
                ? zan_genmeta_expr_from_json(conn, arena) : NULL;
            if (conn_n) zan_ast_list_push(&c->call.args, conn_n, arena);
            ce->member.object = c;
            continue;
        }

        if (strcmp(opname, "db_acc_root") == 0 ||
            strcmp(opname, "db_repo_call") == 0) {
            zan_ast_node_t *repl = zan_genmeta_expr_from_json(
                json_obj_get(op, "tree"), arena);
            if (repl) *call = *repl;
            continue;
        }

        if (strcmp(opname, "db_chain") == 0) {
            json_value *ops = json_obj_get(op, "ops");
            if (!ops || ops->type != JSON_ARR || !ce ||
                ce->kind != AST_MEMBER_ACCESS)
                continue;
            zan_ast_node_t *node = ce->member.object;
            for (int k = 0; k < ops->as.arr.count; k++) {
                json_value *o = ops->as.arr.items[k];
                const char *m = json_get_str(json_obj_get(o, "m"));
                if (!m) continue;
                zan_ast_node_t *c = rw_call(
                    arena, loc, rw_member(arena, loc, node, m),
                    json_obj_get(o, "args"));
                node = c;
            }
            *call = *node;
            continue;
        }

        if (strcmp(opname, "db_expr_arg") == 0) {
            int param = (int)json_get_num(json_obj_get(op, "param"), -1);
            zan_ast_node_t *t = zan_genmeta_expr_from_json(
                json_obj_get(op, "tree"), arena);
            if (t && param >= 0 && param < call->call.args.count)
                call->call.args.items[param] = t;
            continue;
        }
    }
    free(idx);
    free(order);
}

/* Parse each generated source and merge its declarations into the unit. */
static void zan_merge_sources(zan_ast_node_t *unit, json_value *sources,
                              zan_arena_t *arena, zan_diag_t *diag) {
    if (!sources || sources->type != JSON_ARR) return;
    for (int i = 0; i < sources->as.arr.count; i++) {
        json_value *src = sources->as.arr.items[i];
        const char *text = json_get_str(json_obj_get(src, "text"));
        if (!text) continue;
        /* Capture for the demand-driven pull-in's second round (freed by
         * the consumer in zan_gen_take_source_texts). */
        if (g_gen_text_count == g_gen_text_cap) {
            int ncap = g_gen_text_cap ? g_gen_text_cap * 2 : 4;
            char **grown = (char **)realloc(g_gen_texts,
                                            (size_t)ncap * sizeof(*grown));
            if (grown) {
                g_gen_texts = grown;
                g_gen_text_cap = ncap;
            }
        }
        if (g_gen_text_count < g_gen_text_cap)
            g_gen_texts[g_gen_text_count++] = strdup(text);
        zan_lexer_t lex;
        zan_lexer_init(&lex, text, strlen(text), 0, arena, diag);
        zan_parser_t gp;
        zan_parser_init(&gp, &lex, arena, diag);
        zan_ast_node_t *gu = zan_parser_parse(&gp);
        if (!gu) continue;
        /* Generated decls bind like a late-added source file: stamp each with
         * its own unit's namespace + usings, or the file's `using
         * System.Data.Orm;` header never reaches the binder and every
         * unqualified OrmMeta/DbValues reference fails to resolve. */
        zan_nsresolve_stamp(gu, arena);
        for (int k = 0; k < gu->comp_unit.decls.count; k++)
            zan_ast_list_push(&unit->comp_unit.decls, gu->comp_unit.decls.items[k],
                              arena);
    }
}

/* Report generator diagnostics (errors/warnings) through the compiler's
 * diag sink, where they carry the user file:line locations the generator
 * embedded in the metadata. */
static void zan_report_diags(zan_diag_t *diag, json_value *arr, bool is_error) {
    if (!arr || arr->type != JSON_ARR) return;
    for (int i = 0; i < arr->as.arr.count; i++) {
        json_value *d = arr->as.arr.items[i];
        int file = (int)json_get_num(json_obj_get(d, "file"), 0);
        int line = (int)json_get_num(json_obj_get(d, "line"), 0);
        int col = (int)json_get_num(json_obj_get(d, "col"), 0);
        const char *msg = json_get_str(json_obj_get(d, "msg"));
        if (!msg) continue;
        zan_diag_emit(diag, is_error ? DIAG_ERROR : DIAG_WARNING,
                      zan_loc((uint32_t)file, (uint32_t)line, (uint32_t)col, 0),
                      "%s", msg);
    }
}

/* Run the Zan-scripted code generators over the compilation unit: export the
 * metadata, run the cached generator executable, then apply the reply --
 * generated sources are parsed and merged, rewrite directives retarget call
 * sites, diagnostics flow into `diag`. Returns 0 on success (or when nothing
 * triggered), -1 on failure with a message on stderr. */
int zan_gen_codegen(zan_ast_node_t *unit, zan_arena_t *arena,
                    zan_diag_t *diag, const char *stdlib_root) {
    if (!zan_gen_enabled || !unit) return 0;

    /* Fast path: check triggers directly on the AST without any allocation.
     * Projects with no Json/ORM/route shapes exit in <2ms with ZERO heap allocations. */
    if (!zan_gen_ast_triggered(unit)) return 0;

    if (!stdlib_root || !stdlib_root[0]) {
        fprintf(stderr, "error: code generation needs the standard library\n");
        return -1;
    }

    char dir[ZAN_GEN_MAX_PATH];
    if (zan_gen_cache_dir(dir, sizeof(dir)) != 0) {
        fprintf(stderr, "error: no user cache dir for the code generators\n");
        return -1;
    }
    char exe[ZAN_GEN_MAX_PATH];
    if (zan_gen_ensure(stdlib_root, exe, sizeof(exe)) != 0) {
        return -1;
    }

    /* Export with the file table: the generators ignore it, but the index
     * emitter (GenIndex, opt-in via ZAN_INDEX_DIR) turns the `file` ids on
     * every declaration into paths with it. */
    char *meta = zan_genmeta_export_files(unit, diag);
    if (!meta) return 0;

    char meta_path[ZAN_GEN_MAX_PATH], out_path[ZAN_GEN_MAX_PATH];
#ifdef _WIN32
    int pid = (int)_getpid();
#else
    int pid = (int)getpid();
#endif
    snprintf(meta_path, sizeof(meta_path), "%s%cgen_codegen_%d_in.json",
             dir, GEN_DIR_SEP_STR[0], pid);
    snprintf(out_path, sizeof(out_path), "%s%cgen_codegen_%d_out.json",
             dir, GEN_DIR_SEP_STR[0], pid);

    int rc = -1;
    /* Stream the metadata into the codegen request directly:
     * {"mode":"codegen","unit":<meta>}.
     * Avoid duplicating the multi-megabyte string into another heap buffer. */
    FILE *mf = fopen(meta_path, "wb");
    if (!mf) {
        fprintf(stderr, "error: cannot write generator request\n");
        free(meta);
        remove(meta_path);
        return -1;
    }
    const char *prefix = "{\"mode\":\"codegen\",\"unit\":";
    const char *suffix = "}";
    size_t mlen = strlen(meta);
    if (fwrite(prefix, 1, strlen(prefix), mf) != strlen(prefix) ||
        fwrite(meta, 1, mlen, mf) != mlen ||
        fwrite(suffix, 1, strlen(suffix), mf) != strlen(suffix) ||
        fclose(mf) != 0) {
        fprintf(stderr, "error: cannot write generator request\n");
        free(meta);
        remove(meta_path);
        return -1;
    }
    free(meta);

    if (zan_gen_run(exe, meta_path, out_path) != 0) goto done;

    {
        size_t olen = 0;
        char *reply = zan_read_file(out_path, &olen);
        json_value *root = reply ? json_parse(reply) : NULL;
        if (!root) {
            fprintf(stderr, "error: invalid generator reply (codegen)\n");
            free(reply);
            goto done;
        }
        const char *err = json_get_str(json_obj_get(root, "error"));
        if (err && err[0]) {
            fprintf(stderr, "error: %s\n", err);
            json_free(root);
            free(reply);
            goto done;
        }
        json_value *errors = json_obj_get(root, "errors");
        json_value *warnings = json_obj_get(root, "warnings");
        zan_report_diags(diag, warnings, false);
        zan_report_diags(diag, errors, true);
        /* Rewrites were already decided while scanning (matching the old
         * in-place C generators); generated sources are only merged when no
         * generator diagnostics were raised. */
        zan_apply_rewrites(unit, json_obj_get(root, "rewrites"), arena);
        if (errors && errors->type == JSON_ARR && errors->as.arr.count > 0) {
            json_free(root);
            free(reply);
            rc = 0; /* diagnostics already reported; main() fails later */
            goto done;
        }
        zan_merge_sources(unit, json_obj_get(root, "sources"), arena, diag);
        /* ZAN_GEN_REPLY=<path>: keep a copy of the reply (test hook --
         * tests/gen/ asserts on the generated text and rewrite directives). */
        {
            const char *gr = getenv("ZAN_GEN_REPLY");
            if (gr && gr[0]) {
                FILE *cf = fopen(gr, "wb");
                if (cf) {
                    FILE *rf = fopen(out_path, "rb");
                    if (rf) {
                        char buf[8192];
                        size_t n;
                        while ((n = fread(buf, 1, sizeof(buf), rf)) > 0)
                            fwrite(buf, 1, n, cf);
                        fclose(rf);
                    }
                    fclose(cf);
                }
            }
        }
        json_free(root);
        free(reply);
        rc = 0;
    }

done:
    remove(meta_path);
    remove(out_path);
    return rc;
}
