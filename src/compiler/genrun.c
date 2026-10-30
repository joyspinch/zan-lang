/* genrun */

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



#ifdef _WIN32
#define GEN_DIR_SEP_STR "\\"
#define GEN_EXE_SUFFIX ".exe"
#else
#define GEN_DIR_SEP_STR "/"
#define GEN_EXE_SUFFIX ""
#endif


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
    /* 创建every missing level: CreateDirectory only makes the last one */
    for (char *p = dir + 1; *p; p++) {
        if (*p != '\\' && *p != '/') continue;
        char sep = *p;
        *p = '\0';
        CreateDirectoryA(dir, NULL);
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
    /* 创建every missing level: the cache root itself may not exist yet */
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

static int zan_gen_strip(const char *s, const char *prefix, const char **rest) {
    size_t n = strlen(prefix);
    if (strncmp(s, prefix, n) != 0) return 0;
    *rest = s + n;
    return 1;
}

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

static int zan_gen_parse_pid(const char *s, uint32_t *out) {
    if (!s) return 0;
    const char *p = s;
    const char *end = p + strlen(p);
    if (end - p >= 4 && memcmp(end - 4, ".exe", 4) == 0) end -= 4;
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

typedef struct zan_gen_cached_item {
    char name[ZAN_GEN_MAX_PATH];
    uint64_t key;
    uint64_t mtime;
} zan_gen_cached_item;

typedef struct zan_gen_sweep_ctx {
    const char *dir;
    uint64_t keep_key;
    zan_gen_cached_item items[64];
    int item_count;
} zan_gen_sweep_ctx;

static void zan_gen_sweep_one(const char *name, void *ud) {
    zan_gen_sweep_ctx *ctx = (zan_gen_sweep_ctx *)ud;
    const char *dir = ctx->dir;
    const char *rest = NULL;
    char path[ZAN_GEN_MAX_PATH];
    uint32_t pid = 0;
#ifdef _WIN32
    const char sep = '\\';
#else
    const char sep = '/';
#endif

    /* ZanGen_<16hex>[_pid].exe */
    if (zan_gen_strip(name, "ZanGen_", &rest)) {
        int kind = zan_gen_parse_zangen(rest, &pid);
        if (kind == 1) {
            uint64_t fkey = 0;
            for (int i = 0; i < 16; i++) {
                char c = rest[i];
                int d = (c >= '0' && c <= '9') ? (c - '0') :
                        (c >= 'a' && c <= 'f') ? (c - 'a' + 10) :
                        (c >= 'A' && c <= 'F') ? (c - 'A' + 10) : 0;
                fkey = (fkey << 4) | (uint64_t)d;
            }
            if (ctx->item_count < 64) {
                snprintf(ctx->items[ctx->item_count].name,
                         sizeof(ctx->items[ctx->item_count].name), "%s", name);
                ctx->items[ctx->item_count].key = fkey;
                uint64_t mt = 0;
#ifdef _WIN32
                WIN32_FILE_ATTRIBUTE_DATA fad;
                snprintf(path, sizeof(path), "%s%c%s", dir, sep, name);
                if (GetFileAttributesExA(path, GetFileExInfoStandard, &fad)) {
                    mt = ((uint64_t)fad.ftLastWriteTime.dwHighDateTime << 32) |
                         (uint64_t)fad.ftLastWriteTime.dwLowDateTime;
                }
#else
                struct stat st;
                snprintf(path, sizeof(path), "%s%c%s", dir, sep, name);
                if (stat(path, &st) == 0) {
                    mt = (uint64_t)st.st_mtime;
                }
#endif
                ctx->items[ctx->item_count].mtime = mt;
                ctx->item_count++;
            }
            return;
        }
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
    /* gen_codegen_<pid>_in */
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
}

static void zan_gen_sweep(const char *dir, uint64_t keep_key) {
    static int swept = 0;
    if (swept) return;
    swept = 1;
    zan_gen_sweep_ctx ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.dir = dir;
    ctx.keep_key = keep_key;
    zan_gen_scan_dir(dir, zan_gen_sweep_one, &ctx);

    /* 严格容量淘汰：至多保留 2 个已发布镜像（当前活跃版本 + 最多1个旧版本），彻底杜绝无限膨胀 */
    if (ctx.item_count > 2) {
        for (int i = 0; i < ctx.item_count - 1; i++) {
            for (int j = i + 1; j < ctx.item_count; j++) {
                int score_i = (ctx.items[i].key == keep_key) ? 1 : 0;
                int score_j = (ctx.items[j].key == keep_key) ? 1 : 0;
                int swap = 0;
                if (score_j > score_i) swap = 1;
                else if (score_j == score_i && ctx.items[j].mtime > ctx.items[i].mtime) swap = 1;
                if (swap) {
                    zan_gen_cached_item tmp = ctx.items[i];
                    ctx.items[i] = ctx.items[j];
                    ctx.items[j] = tmp;
                }
            }
        }
        for (int i = 2; i < ctx.item_count; i++) {
            char del_path[ZAN_GEN_MAX_PATH];
#ifdef _WIN32
            const char s = '\\';
#else
            const char s = '/';
#endif
            snprintf(del_path, sizeof(del_path), "%s%c%s", dir, s, ctx.items[i].name);
#ifdef _WIN32
            DeleteFileA(del_path);
#else
            remove(del_path);
#endif
        }
    }
}


typedef struct zan_gen_strlist {
    char **v;
    size_t n;
    size_t cap;
} zan_gen_strlist;

static void zan_gen_strlist_push(zan_gen_strlist *l, const char *s) {
    if (l->n == l->cap) {
        size_t ncap = l->cap ? l->cap * 2 : 64;
        char **nv = (char **)realloc(l->v, ncap * sizeof(char *));
        if (!nv) return;
        l->v = nv;
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
    char zexe[ZAN_GEN_MAX_PATH];
    zan_self_exe(zexe, sizeof(zexe));
    if (!zexe[0]) {
        fprintf(stderr, "error: cannot locate zanc to compile the code generators\n");
        return -1;
    }
    /* 优先复用编译器同级已有的预编译生成器（如 build/ZanGen.exe 或 bin/ZanGen.exe），杜绝磁盘污染与动态编译 */
    {
        char tool_exe[ZAN_GEN_MAX_PATH];
        snprintf(tool_exe, sizeof(tool_exe), "%s", zexe);
        char *slash = strrchr(tool_exe, '/');
        char *bslash = strrchr(tool_exe, '\\');
        if (!slash || (bslash && bslash > slash)) slash = bslash;
        if (slash) {
            slash[1] = '\0';
            strncat(tool_exe, "ZanGen" GEN_EXE_SUFFIX, sizeof(tool_exe) - strlen(tool_exe) - 1);
#ifdef _WIN32
            if (GetFileAttributesA(tool_exe) != INVALID_FILE_ATTRIBUTES) {
                snprintf(exe, exe_size, "%s", tool_exe);
                return 0;
            }
#else
            if (access(tool_exe, X_OK) == 0) {
                snprintf(exe, exe_size, "%s", tool_exe);
                return 0;
            }
#endif
        }
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
    /* 自动清理历史旧版本和死进程残留，仅保留当前活跃的生成器 */
    zan_gen_sweep(dir, key);

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
        char tmp[ZAN_GEN_MAX_PATH];
#ifdef _WIN32
        int pid = (int)_getpid();
#else
        int pid = (int)getpid();
#endif
        snprintf(tmp, sizeof(tmp), "%s%cZanGen_%016llx_%d%s", dir,
                 GEN_DIR_SEP_STR[0], (unsigned long long)key, pid,
                 GEN_EXE_SUFFIX);
        char *argv[] = {
            zexe, src, "--stdlib-path", (char *)stdlib_root, "--auto-stdlib",
            "--no-gen", "--no-packages", "--quiet", "-DZAN_GEN_MAIN=1",
            "-o", tmp, NULL
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

/* 内部辅助逻辑：统一设计稿路径仅识别标准 .html/.htm（GUI窗体与游戏场景） */
bool zan_is_design_path(const char *p) {
    size_t n = strlen(p);
    return (n > 5 && strcmp(p + n - 5, ".html") == 0) ||
           (n > 4 && strcmp(p + n - 4, ".htm") == 0);
}

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

    /* 构建the design request: { mode, files: [{ name, text, emitMain }] } */
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


