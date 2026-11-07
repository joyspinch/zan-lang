/* rt_file.c: 文件元数据、文件流与文件锁运行时（跨平台组合分发入口） */

#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS 1
#endif

#if defined(_WIN32)
#if defined(_WIN32_WINNT) && _WIN32_WINNT < 0x0601
#undef _WIN32_WINNT
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#endif

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include "file/rt_file_win.c"
#else
#include "file/rt_file_posix.c"
#endif

/* --------------------------------------------------------------------------
 * 跨平台应用执行目录与包解析路径探测
 * -------------------------------------------------------------------------- */

const char *zan_file_app_dir(void) {
    static char dir[4096];
    if (plat_appdir_is_resolved()) return dir;
    plat_appdir_acquire();
    if (plat_appdir_is_resolved()) return dir;

    const char *env = getenv("ZAN_APP_DIR");
    char local[4096];
    local[0] = '\0';
    if (env && env[0]) {
        size_t n = strlen(env);
        if (n < sizeof(local)) {
            memcpy(local, env, n + 1);
        } else {
            fprintf(stderr,
                    "zan: ZAN_APP_DIR too long (%zu bytes, limit %zu) -- "
                    "falling back to the executable directory\n",
                    n, sizeof(local) - 1);
            fflush(stderr);
        }
    } else {
        char exe[4096];
        exe[0] = 0;
        plat_get_executable_path(exe, sizeof(exe));
        if (exe[0]) {
            char *fwd = strrchr(exe, '/');
            char *back = strrchr(exe, '\\');
            char *sep = fwd > back ? fwd : back;
            if (sep && sep != exe) {
                *sep = 0;
                if (strlen(exe) < sizeof(local))
                    memcpy(local, exe, strlen(exe) + 1);
            }
        }
    }
    memcpy(dir, local, strlen(local) + 1);
    plat_appdir_release();
    return dir;
}

static const char *zan_alt_path(const char *path, int which, char *out,
                                size_t cap) {
    if (!path || !path[0]) return NULL;
    if (path[0] == '/' || path[0] == '\\') return NULL;
    if (path[1] == ':') return NULL;
    const char *base = which == 0 ? getenv("ZAN_PKG_DIR") : zan_file_app_dir();
    if (!base || !base[0]) return NULL;
    size_t bl = strlen(base), pl = strlen(path);
    if (bl + pl + 2 > cap) return NULL;
    memcpy(out, base, bl);
    out[bl] = '/';
    memcpy(out + bl + 1, path, pl + 1);
    return out;
}

#define ZAN_ALT_BASES 2

const char *zan_file_read_path(const char *path) {
    if (zan_file_attributes_at(path) >= 0) return "";
    static _Thread_local char alt[4096];
    for (int which = 0; which < ZAN_ALT_BASES; which++) {
        const char *p = zan_alt_path(path, which, alt, sizeof(alt));
        if (p && zan_file_attributes_at(p) >= 0) return p;
    }
    return "";
}

void *zan_pkg_fopen(const char *path, const char *mode) {
    FILE *f = (FILE *)zan_file_fopen(path, mode);
    if (f) return f;
    if (!mode || mode[0] != 'r') return NULL;
    if (strchr(mode, '+')) return NULL;
    char alt[4096];
    for (int which = 0; which < ZAN_ALT_BASES; which++) {
        const char *p = zan_alt_path(path, which, alt, sizeof(alt));
        if (!p) continue;
        f = (FILE *)zan_file_fopen(p, mode);
        if (f) return f;
    }
    return NULL;
}

long long zan_file_attributes(const char *path) {
    long long r = zan_file_attributes_at(path);
    if (r >= 0) return r;
    char alt[4096];
    for (int which = 0; which < ZAN_ALT_BASES; which++) {
        const char *p = zan_alt_path(path, which, alt, sizeof(alt));
        if (!p) continue;
        r = zan_file_attributes_at(p);
        if (r >= 0) return r;
    }
    return -1;
}

/* --------------------------------------------------------------------------
 * 跨平台文件句柄表（Thread-safe Handle Table）
 * -------------------------------------------------------------------------- */

#define ZAN_FH_CAP0 64
#define ZAN_FH_CAP_MAX (1024 * 1024)
typedef struct {
    FILE *fp;
    uint32_t gen;
    int open;
    int inuse;
    FILE *dying;
} zan_fh_slot;
static zan_fh_slot *g_fh_table = NULL;
static uint32_t g_fh_cap = 0;

static void zan_fh_ensure(void) {
    if (g_fh_table) return;
    g_fh_table = (zan_fh_slot *)calloc(ZAN_FH_CAP0, sizeof(*g_fh_table));
    if (!g_fh_table) return;
    g_fh_cap = ZAN_FH_CAP0;
}

static int zan_fh_grow(void) {
    uint32_t ncap;
    zan_fh_slot *ntab;
    if (g_fh_cap == 0) return 0;
    ncap = g_fh_cap * 2;
    if (ncap > ZAN_FH_CAP_MAX || ncap < g_fh_cap) return 0;
    ntab = (zan_fh_slot *)calloc((size_t)ncap, sizeof(*ntab));
    if (!ntab) return 0;
    memcpy(ntab, g_fh_table, (size_t)g_fh_cap * sizeof(*ntab));
    free(g_fh_table);
    g_fh_table = ntab;
    g_fh_cap = ncap;
    return 1;
}

static long zan_fh_index(long long handle) {
    unsigned long long h = (unsigned long long)handle;
    uint32_t idx = (uint32_t)(h & 0xFFFFFFFFULL);
    uint32_t gen = (uint32_t)(h >> 32);
    if (!g_fh_table || idx >= g_fh_cap) return -1;
    zan_fh_slot *s = &g_fh_table[idx];
    if (!s->open || s->fp == NULL || s->gen != gen) return -1;
    return (long)idx;
}

static FILE *zan_fh_pin(long long handle, long *idx_out) {
    zan_fh_lock();
    zan_fh_ensure();
    long idx = zan_fh_index(handle);
    FILE *f = NULL;
    if (idx >= 0) {
        f = g_fh_table[idx].fp;
        g_fh_table[idx].inuse++;
    }
    *idx_out = idx;
    zan_fh_unlock();
    return f;
}

static void zan_fh_unpin(long idx) {
    FILE *dying = NULL;
    if (idx < 0) return;
    zan_fh_lock();
    if (g_fh_table && (uint32_t)idx < g_fh_cap) {
        zan_fh_slot *s = &g_fh_table[idx];
        if (s->inuse > 0 && --s->inuse == 0 && s->dying) {
            dying = s->dying;
            s->dying = NULL;
        }
    }
    zan_fh_unlock();
    if (dying) fclose(dying);
}

long long zan_file_open(const char *path, const char *mode) {
    if (!path || !path[0] || !mode || !mode[0]) return 0;
    FILE *f = (FILE *)zan_file_fopen(path, mode);
    if (!f) return 0;
    zan_fh_lock();
    zan_fh_ensure();
    long long handle = 0;
    if (g_fh_table) {
        for (;;) {
            uint32_t i = 0;
            while (i < g_fh_cap) {
                zan_fh_slot *s = &g_fh_table[i];
                if (!s->open && !s->dying && s->gen != UINT32_MAX) {
                    if (s->gen == 0) { s->gen = 1; }
                    s->fp = f;
                    s->open = 1;
                    handle = (long long)(((unsigned long long)s->gen << 32)
                                         | (unsigned long long)i);
                    break;
                }
                i = i + 1;
            }
            if (i < g_fh_cap) break;    /* claimed a slot */
            if (!zan_fh_grow()) break;
        }
    }
    zan_fh_unlock();
    if (handle == 0) { fclose(f); return 0; }
    return handle;
}

long long zan_file_read(long long handle, long long buf, long long count) {
    if (!buf || count <= 0) return 0;
    long idx;
    FILE *f = zan_fh_pin(handle, &idx);
    if (!f) return 0;
    long long n = (long long)fread((void *)(intptr_t)buf, 1, (size_t)count, f);
    zan_fh_unpin(idx);
    return n;
}

long long zan_file_write(long long handle, long long buf, long long count) {
    if (!buf || count <= 0) return 0;
    long idx;
    FILE *f = zan_fh_pin(handle, &idx);
    if (!f) return 0;
    long long n = (long long)fwrite((const void *)(intptr_t)buf, 1, (size_t)count, f);
    zan_fh_unpin(idx);
    return n;
}

/* `origin`: 0 = begin, 1 = current, 2 = end */
long long zan_file_seek(long long handle, long long offset, int origin) {
    long idx;
    FILE *f = zan_fh_pin(handle, &idx);
    if (!f) return -1;
    int whence = origin == 1 ? SEEK_CUR : (origin == 2 ? SEEK_END : SEEK_SET);
    long long r = plat_file_seek_raw(f, offset, whence);
    zan_fh_unpin(idx);
    return r;
}

long long zan_file_tell(long long handle) {
    long idx;
    FILE *f = zan_fh_pin(handle, &idx);
    if (!f) return -1;
    long long r = plat_file_tell_raw(f);
    zan_fh_unpin(idx);
    return r;
}

long long zan_file_flush(long long handle) {
    long idx;
    FILE *f = zan_fh_pin(handle, &idx);
    if (!f) return -1;
    int rc = fflush(f);
    zan_fh_unpin(idx);
    return rc == 0 ? 1 : 0;
}

long long zan_file_close(long long handle) {
    zan_fh_lock();
    zan_fh_ensure();
    long idx = zan_fh_index(handle);
    FILE *f = NULL;
    int deferred = 0;
    if (idx >= 0) {
        zan_fh_slot *s = &g_fh_table[idx];
        s->open = 0;
        if (s->inuse > 0) {
            s->dying = s->fp;
            s->fp = NULL;
            deferred = 1;
        } else {
            f = s->fp;
            s->fp = NULL;
        }
        uint32_t next_gen = (uint32_t)(s->gen + 1);
        if (next_gen != 0) s->gen = next_gen;
    }
    zan_fh_unlock();
    if (deferred) return 1;
    if (!f) return 0;
    return fclose(f) == 0 ? 1 : 0;
}

long long zan_file_eof(long long handle) {
    long idx;
    FILE *f = zan_fh_pin(handle, &idx);
    if (!f) return 1;
    int e = feof(f) ? 1 : 0;
    zan_fh_unpin(idx);
    return e;
}

/* --------------------------------------------------------------------------
 * 跨平台文件锁表（Cross-process File Lock Table）
 * -------------------------------------------------------------------------- */

#define ZAN_LK_CAP 32
typedef struct {
    plat_file_lock_t lock;
    int used;
    uint32_t gen;
} zan_lk_slot;
static zan_lk_slot g_lk_table[ZAN_LK_CAP];

static long zan_lk_index(long long handle) {
    if (handle <= 0) return -1;
    unsigned long long h = (unsigned long long)handle;
    uint32_t gen = (uint32_t)(h >> 32);
    unsigned long long slot = h & 0xFFFFFFFFULL;
    if (slot >= (unsigned long long)ZAN_LK_CAP) return -1;
    if (g_lk_table[slot].used != 1 || g_lk_table[slot].gen != gen) return -1;
    return (long)slot;
}

long long zan_file_try_lock(const char *path) {
    if (!path || !path[0]) return 0;
    zan_fh_lock();
    long slot = -1;
    for (long i = 0; i < ZAN_LK_CAP; i++) {
        if (!g_lk_table[i].used) {
            slot = i;
            g_lk_table[slot].used = 1;
            break;
        }
    }
    zan_fh_unlock();
    if (slot < 0) return 0;

    plat_file_lock_t lk = plat_file_lock_open(path);
    if (lk == PLAT_FILE_LOCK_INVALID) {
        zan_fh_lock();
        g_lk_table[slot].used = 0;
        zan_fh_unlock();
        return 0;
    }

    zan_fh_lock();
    g_lk_table[slot].lock = lk;
    uint32_t next_gen = (uint32_t)(g_lk_table[slot].gen + 1);
    if (next_gen == 0) {
        g_lk_table[slot].lock = PLAT_FILE_LOCK_INVALID;
        g_lk_table[slot].used = 2;
        zan_fh_unlock();
        plat_file_lock_close(lk);
        return 0;
    }
    g_lk_table[slot].gen = next_gen;
    unsigned long long lk_handle =
        ((unsigned long long)g_lk_table[slot].gen << 32)
        | (unsigned long long)slot;
    zan_fh_unlock();
    return (long long)lk_handle;
}

long long zan_file_unlock(long long handle) {
    zan_fh_lock();
    long slot = zan_lk_index(handle);
    if (slot < 0) {
        zan_fh_unlock();
        return 0;
    }
    zan_lk_slot *s = &g_lk_table[slot];
    plat_file_lock_t lk = s->lock;
    s->lock = PLAT_FILE_LOCK_INVALID;
    s->used = 0;
    s->gen = s->gen + 1;
    if (s->gen == 0) {
        s->used = 2;
    }
    zan_fh_unlock();
    plat_file_lock_close(lk);
    return 1;
}
