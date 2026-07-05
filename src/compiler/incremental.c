/* incremental.c -- Incremental compilation and parallel build support. */

#include "incremental.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#define PATH_SEP "\\"
#define zan_strdup _strdup
#else
#include <sys/stat.h>
#include <unistd.h>
#include <pthread.h>
#define PATH_SEP "/"
#define zan_strdup strdup
#endif

/* ---- FNV-1a 64-bit hash ---- */

uint64_t zan_hash_buffer(const void *data, size_t len) {
    const uint8_t *p = (const uint8_t *)data;
    uint64_t h = 0xcbf29ce484222325ULL;
    for (size_t i = 0; i < len; i++) {
        h ^= p[i];
        h *= 0x100000001b3ULL;
    }
    return h;
}

uint64_t zan_hash_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    uint64_t h = 0xcbf29ce484222325ULL;
    uint8_t buf[8192];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        for (size_t i = 0; i < n; i++) {
            h ^= buf[i];
            h *= 0x100000001b3ULL;
        }
    }
    fclose(f);
    return h;
}

uint64_t zan_file_mtime(const char *path) {
#ifdef _WIN32
    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (!GetFileAttributesExA(path, GetFileExInfoStandard, &fad)) return 0;
    ULARGE_INTEGER li;
    li.LowPart = fad.ftLastWriteTime.dwLowDateTime;
    li.HighPart = fad.ftLastWriteTime.dwHighDateTime;
    return li.QuadPart;
#else
    struct stat st;
    if (stat(path, &st) != 0) return 0;
    return (uint64_t)st.st_mtime;
#endif
}

uint64_t zan_file_size(const char *path) {
#ifdef _WIN32
    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (!GetFileAttributesExA(path, GetFileExInfoStandard, &fad)) return 0;
    return ((uint64_t)fad.nFileSizeHigh << 32) | fad.nFileSizeLow;
#else
    struct stat st;
    if (stat(path, &st) != 0) return 0;
    return (uint64_t)st.st_size;
#endif
}

int zan_cpu_count(void) {
#ifdef _WIN32
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return (int)si.dwNumberOfProcessors;
#else
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    return n > 0 ? (int)n : 1;
#endif
}

/* ---- cache directory management ---- */

static void ensure_cache_dir(const char *path) {
#ifdef _WIN32
    CreateDirectoryA(path, NULL);
#else
    mkdir(path, 0755);
#endif
}

void zan_incr_init(zan_incr_cache_t *cache, const char *project_dir) {
    memset(cache, 0, sizeof(*cache));
    size_t dlen = strlen(project_dir);
    cache->cache_dir = (char *)malloc(dlen + 16);
    snprintf(cache->cache_dir, dlen + 16, "%s" PATH_SEP ".zan-cache", project_dir);
    ensure_cache_dir(cache->cache_dir);
    cache->unit_cap = 64;
    cache->units = (zan_compile_unit_t *)calloc((size_t)cache->unit_cap, sizeof(zan_compile_unit_t));
    cache->cache_valid = false;
}

/* ---- manifest load/save ---- */

#define CACHE_MAGIC 0x5A414E43
#define CACHE_VERSION 1

bool zan_incr_load(zan_incr_cache_t *cache) {
    char manifest[1024];
    snprintf(manifest, sizeof(manifest), "%s" PATH_SEP "manifest.bin", cache->cache_dir);

    FILE *f = fopen(manifest, "rb");
    if (!f) return false;

    uint32_t magic, version;
    if (fread(&magic, 4, 1, f) != 1 || magic != CACHE_MAGIC) { fclose(f); return false; }
    if (fread(&version, 4, 1, f) != 1 || version != CACHE_VERSION) { fclose(f); return false; }

    int32_t count;
    if (fread(&count, 4, 1, f) != 1 || count < 0 || count > 10000) { fclose(f); return false; }

    for (int i = 0; i < count; i++) {
        uint16_t slen;
        char path_buf[1024];

        if (fread(&slen, 2, 1, f) != 1) break;
        if (slen >= sizeof(path_buf)) break;
        if (fread(path_buf, 1, slen, f) != slen) break;
        path_buf[slen] = 0;

        zan_compile_unit_t unit;
        memset(&unit, 0, sizeof(unit));
        unit.source_path = zan_strdup(path_buf);

        if (fread(&slen, 2, 1, f) != 1) break;
        if (slen >= sizeof(path_buf)) break;
        if (fread(path_buf, 1, slen, f) != slen) break;
        path_buf[slen] = 0;
        unit.object_path = zan_strdup(path_buf);

        if (fread(&unit.stamp, sizeof(zan_file_stamp_t), 1, f) != 1) break;

        int16_t dep_count;
        if (fread(&dep_count, 2, 1, f) != 1) break;
        unit.dep_count = dep_count;
        if (dep_count > 0) {
            unit.deps = (char **)malloc(sizeof(char *) * (size_t)dep_count);
            for (int d = 0; d < dep_count; d++) {
                if (fread(&slen, 2, 1, f) != 1) break;
                if (slen >= sizeof(path_buf)) break;
                if (fread(path_buf, 1, slen, f) != slen) break;
                path_buf[slen] = 0;
                unit.deps[d] = zan_strdup(path_buf);
            }
        }

        if (cache->unit_count >= cache->unit_cap) {
            cache->unit_cap *= 2;
            cache->units = (zan_compile_unit_t *)realloc(cache->units,
                sizeof(zan_compile_unit_t) * (size_t)cache->unit_cap);
        }
        cache->units[cache->unit_count++] = unit;
    }

    fclose(f);
    cache->cache_valid = true;
    return true;
}

bool zan_incr_save(zan_incr_cache_t *cache) {
    char manifest[1024];
    snprintf(manifest, sizeof(manifest), "%s" PATH_SEP "manifest.bin", cache->cache_dir);

    FILE *f = fopen(manifest, "wb");
    if (!f) return false;

    uint32_t magic = CACHE_MAGIC, version = CACHE_VERSION;
    fwrite(&magic, 4, 1, f);
    fwrite(&version, 4, 1, f);

    int32_t count = (int32_t)cache->unit_count;
    fwrite(&count, 4, 1, f);

    for (int i = 0; i < cache->unit_count; i++) {
        zan_compile_unit_t *u = &cache->units[i];
        uint16_t slen;

        slen = (uint16_t)strlen(u->source_path);
        fwrite(&slen, 2, 1, f);
        fwrite(u->source_path, 1, slen, f);

        slen = (uint16_t)strlen(u->object_path);
        fwrite(&slen, 2, 1, f);
        fwrite(u->object_path, 1, slen, f);

        fwrite(&u->stamp, sizeof(zan_file_stamp_t), 1, f);

        int16_t dc = (int16_t)u->dep_count;
        fwrite(&dc, 2, 1, f);
        for (int d = 0; d < u->dep_count; d++) {
            slen = (uint16_t)strlen(u->deps[d]);
            fwrite(&slen, 2, 1, f);
            fwrite(u->deps[d], 1, slen, f);
        }
    }

    fclose(f);
    return true;
}

/* ---- rebuild detection ---- */

static zan_compile_unit_t *find_unit(zan_incr_cache_t *cache, const char *path) {
    for (int i = 0; i < cache->unit_count; i++) {
        if (strcmp(cache->units[i].source_path, path) == 0)
            return &cache->units[i];
    }
    return NULL;
}

bool zan_incr_needs_rebuild(zan_incr_cache_t *cache, const char *source_path) {
    if (!cache->cache_valid) return true;

    zan_compile_unit_t *unit = find_unit(cache, source_path);
    if (!unit) return true;

    FILE *f = fopen(unit->object_path, "rb");
    if (!f) return true;
    fclose(f);

    uint64_t cur_mtime = zan_file_mtime(source_path);
    uint64_t cur_size = zan_file_size(source_path);
    if (cur_mtime == unit->stamp.mtime && cur_size == unit->stamp.size) {
        return false;
    }

    uint64_t cur_hash = zan_hash_file(source_path);
    if (cur_hash == unit->stamp.hash) {
        unit->stamp.mtime = cur_mtime;
        unit->stamp.size = cur_size;
        return false;
    }

    return true;
}

void zan_incr_register(zan_incr_cache_t *cache, const char *source_path,
                       const char *object_path, const char **deps, int dep_count) {
    zan_compile_unit_t *existing = find_unit(cache, source_path);
    zan_compile_unit_t *unit;

    if (existing) {
        free(existing->object_path);
        for (int i = 0; i < existing->dep_count; i++) free(existing->deps[i]);
        free(existing->deps);
        unit = existing;
    } else {
        if (cache->unit_count >= cache->unit_cap) {
            cache->unit_cap *= 2;
            cache->units = (zan_compile_unit_t *)realloc(cache->units,
                sizeof(zan_compile_unit_t) * (size_t)cache->unit_cap);
        }
        unit = &cache->units[cache->unit_count++];
        memset(unit, 0, sizeof(*unit));
        unit->source_path = zan_strdup(source_path);
    }

    unit->object_path = zan_strdup(object_path);
    unit->stamp.hash = zan_hash_file(source_path);
    unit->stamp.mtime = zan_file_mtime(source_path);
    unit->stamp.size = zan_file_size(source_path);

    unit->dep_count = dep_count;
    if (dep_count > 0) {
        unit->deps = (char **)malloc(sizeof(char *) * (size_t)dep_count);
        for (int i = 0; i < dep_count; i++) {
            unit->deps[i] = zan_strdup(deps[i]);
        }
    } else {
        unit->deps = NULL;
    }

    cache->cache_valid = true;
}

const char *zan_incr_get_object(zan_incr_cache_t *cache, const char *source_path) {
    if (!cache->cache_valid) return NULL;
    zan_compile_unit_t *unit = find_unit(cache, source_path);
    if (!unit) return NULL;
    if (zan_incr_needs_rebuild(cache, source_path)) return NULL;
    return unit->object_path;
}

void zan_incr_invalidate(zan_incr_cache_t *cache, const char *changed_file) {
    for (int i = 0; i < cache->unit_count; i++) {
        zan_compile_unit_t *u = &cache->units[i];
        if (strcmp(u->source_path, changed_file) == 0) {
            u->needs_rebuild = true;
            continue;
        }
        for (int d = 0; d < u->dep_count; d++) {
            if (strcmp(u->deps[d], changed_file) == 0) {
                u->needs_rebuild = true;
                break;
            }
        }
    }
}

void zan_incr_clean(zan_incr_cache_t *cache) {
    for (int i = 0; i < cache->unit_count; i++) {
        if (cache->units[i].object_path) {
            remove(cache->units[i].object_path);
        }
    }
    cache->unit_count = 0;
    cache->cache_valid = false;
    zan_incr_save(cache);
}

void zan_incr_destroy(zan_incr_cache_t *cache) {
    for (int i = 0; i < cache->unit_count; i++) {
        free(cache->units[i].source_path);
        free(cache->units[i].object_path);
        for (int d = 0; d < cache->units[i].dep_count; d++)
            free(cache->units[i].deps[d]);
        free(cache->units[i].deps);
    }
    free(cache->units);
    free(cache->cache_dir);
    memset(cache, 0, sizeof(*cache));
}

/* ---- parallel compilation ---- */

typedef struct {
    const char *source_path;
    const char *output_dir;
    zan_compile_result_t *result;
    zan_incr_cache_t *cache;
} worker_task_t;

#ifdef _WIN32

static DWORD WINAPI compile_worker(LPVOID arg) {
    worker_task_t *task = (worker_task_t *)arg;
    char obj_path[1024];

    const char *fname = strrchr(task->source_path, '\\');
    if (!fname) fname = strrchr(task->source_path, '/');
    if (fname) fname++; else fname = task->source_path;

    size_t flen = strlen(fname);
    if (flen > 4 && strcmp(fname + flen - 4, ".zan") == 0) flen -= 4;
    snprintf(obj_path, sizeof(obj_path), "%s\\%.*s.o", task->output_dir, (int)flen, fname);

    if (task->cache) {
        const char *cached = zan_incr_get_object(task->cache, task->source_path);
        if (cached) {
            task->result->source_path = zan_strdup(task->source_path);
            task->result->object_path = zan_strdup(cached);
            task->result->exit_code = 0;
            task->result->error_msg = NULL;
            return 0;
        }
    }

    char cmd[2048];
    snprintf(cmd, sizeof(cmd), "zanc \"%s\" --emit-obj -o \"%s\" 2>&1", task->source_path, obj_path);

    FILE *pipe = _popen(cmd, "r");
    if (!pipe) {
        task->result->source_path = zan_strdup(task->source_path);
        task->result->object_path = NULL;
        task->result->exit_code = -1;
        task->result->error_msg = zan_strdup("failed to spawn compiler");
        return 1;
    }

    char output[4096];
    size_t total = 0;
    char line[512];
    memset(output, 0, sizeof(output));
    while (fgets(line, sizeof(line), pipe)) {
        size_t ll = strlen(line);
        if (total + ll < sizeof(output) - 1) { memcpy(output + total, line, ll); total += ll; }
    }
    output[total] = 0;
    int ret = _pclose(pipe);

    task->result->source_path = zan_strdup(task->source_path);
    task->result->object_path = (ret == 0) ? zan_strdup(obj_path) : NULL;
    task->result->exit_code = ret;
    task->result->error_msg = (ret != 0 && total > 0) ? zan_strdup(output) : NULL;

    if (ret == 0 && task->cache) {
        zan_incr_register(task->cache, task->source_path, obj_path, NULL, 0);
    }
    return 0;
}

zan_compile_result_t *zan_parallel_compile(zan_parallel_opts_t *opts) {
    int n = opts->file_count;
    if (n <= 0) return NULL;

    int nthreads = opts->thread_count;
    if (nthreads <= 0) nthreads = zan_cpu_count();
    if (nthreads > n) nthreads = n;
    if (nthreads > 64) nthreads = 64;

    zan_compile_result_t *results = (zan_compile_result_t *)calloc((size_t)n, sizeof(zan_compile_result_t));
    worker_task_t *tasks = (worker_task_t *)calloc((size_t)n, sizeof(worker_task_t));

    for (int i = 0; i < n; i++) {
        tasks[i].source_path = opts->source_files[i];
        tasks[i].output_dir = opts->output_dir;
        tasks[i].result = &results[i];
        tasks[i].cache = opts->incremental ? opts->cache : NULL;
    }

    HANDLE *threads = (HANDLE *)malloc(sizeof(HANDLE) * (size_t)nthreads);
    int idx = 0;
    while (idx < n) {
        int batch = (n - idx < nthreads) ? (n - idx) : nthreads;
        for (int t = 0; t < batch; t++) {
            threads[t] = CreateThread(NULL, 0, compile_worker, &tasks[idx + t], 0, NULL);
        }
        WaitForMultipleObjects((DWORD)batch, threads, TRUE, INFINITE);
        for (int t = 0; t < batch; t++) { CloseHandle(threads[t]); }
        idx += batch;
    }

    free(threads);
    free(tasks);
    return results;
}

#else /* POSIX */

static void *compile_worker_posix(void *arg) {
    worker_task_t *task = (worker_task_t *)arg;
    char obj_path[1024];

    const char *fname = strrchr(task->source_path, '/');
    if (fname) fname++; else fname = task->source_path;

    size_t flen = strlen(fname);
    if (flen > 4 && strcmp(fname + flen - 4, ".zan") == 0) flen -= 4;
    snprintf(obj_path, sizeof(obj_path), "%s/%.*s.o", task->output_dir, (int)flen, fname);

    if (task->cache) {
        const char *cached = zan_incr_get_object(task->cache, task->source_path);
        if (cached) {
            task->result->source_path = zan_strdup(task->source_path);
            task->result->object_path = zan_strdup(cached);
            task->result->exit_code = 0;
            task->result->error_msg = NULL;
            return NULL;
        }
    }

    char cmd[2048];
    snprintf(cmd, sizeof(cmd), "zanc \"%s\" --emit-obj -o \"%s\" 2>&1", task->source_path, obj_path);

    FILE *pipe = popen(cmd, "r");
    if (!pipe) {
        task->result->source_path = zan_strdup(task->source_path);
        task->result->object_path = NULL;
        task->result->exit_code = -1;
        task->result->error_msg = zan_strdup("failed to spawn compiler");
        return NULL;
    }

    char output[4096];
    size_t total = 0;
    char line[512];
    memset(output, 0, sizeof(output));
    while (fgets(line, sizeof(line), pipe)) {
        size_t ll = strlen(line);
        if (total + ll < sizeof(output) - 1) { memcpy(output + total, line, ll); total += ll; }
    }
    output[total] = 0;
    int ret = pclose(pipe);

    task->result->source_path = zan_strdup(task->source_path);
    task->result->object_path = (ret == 0) ? zan_strdup(obj_path) : NULL;
    task->result->exit_code = ret;
    task->result->error_msg = (ret != 0 && total > 0) ? zan_strdup(output) : NULL;

    if (ret == 0 && task->cache) {
        zan_incr_register(task->cache, task->source_path, obj_path, NULL, 0);
    }
    return NULL;
}

zan_compile_result_t *zan_parallel_compile(zan_parallel_opts_t *opts) {
    int n = opts->file_count;
    if (n <= 0) return NULL;

    int nthreads = opts->thread_count;
    if (nthreads <= 0) nthreads = zan_cpu_count();
    if (nthreads > n) nthreads = n;
    if (nthreads > 64) nthreads = 64;

    zan_compile_result_t *results = (zan_compile_result_t *)calloc((size_t)n, sizeof(zan_compile_result_t));
    worker_task_t *tasks = (worker_task_t *)calloc((size_t)n, sizeof(worker_task_t));

    for (int i = 0; i < n; i++) {
        tasks[i].source_path = opts->source_files[i];
        tasks[i].output_dir = opts->output_dir;
        tasks[i].result = &results[i];
        tasks[i].cache = opts->incremental ? opts->cache : NULL;
    }

    pthread_t *threads = (pthread_t *)malloc(sizeof(pthread_t) * (size_t)nthreads);
    int idx = 0;
    while (idx < n) {
        int batch = (n - idx < nthreads) ? (n - idx) : nthreads;
        for (int t = 0; t < batch; t++) {
            pthread_create(&threads[t], NULL, compile_worker_posix, &tasks[idx + t]);
        }
        for (int t = 0; t < batch; t++) { pthread_join(threads[t], NULL); }
        idx += batch;
    }

    free(threads);
    free(tasks);
    return results;
}

#endif

void zan_parallel_results_free(zan_compile_result_t *results, int count) {
    for (int i = 0; i < count; i++) {
        free(results[i].source_path);
        free(results[i].object_path);
        free(results[i].error_msg);
    }
    free(results);
}
