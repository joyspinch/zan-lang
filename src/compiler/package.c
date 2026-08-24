/* package.c -- Zan package manager implementation. */

#include "package.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#define PATH_SEP "\\"
#define popen _popen
#define pclose _pclose
#define strdup _strdup
#else
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#define PATH_SEP "/"
#endif

#include "../common/host_oom.h"
/* ---- version parsing ---- */

bool zan_version_parse(const char *str, zan_version_t *out) {
    memset(out, 0, sizeof(*out));
    if (!str || !*str) return false;
    int consumed = 0;
    int n = sscanf(str, "%d.%d.%d%n", &out->major, &out->minor, &out->patch, &consumed);
    if (n < 3) {
        n = sscanf(str, "%d.%d%n", &out->major, &out->minor, &consumed);
        if (n < 2) return false;
        out->patch = 0;
    }
    if (str[consumed] == '-') {
        const char *pre = str + consumed + 1;
        size_t plen = strlen(pre);
        if (plen >= sizeof(out->prerelease)) plen = sizeof(out->prerelease) - 1;
        memcpy(out->prerelease, pre, plen);
        out->prerelease[plen] = 0;
    }
    return true;
}

int zan_version_compare(const zan_version_t *a, const zan_version_t *b) {
    if (a->major != b->major) return a->major - b->major;
    if (a->minor != b->minor) return a->minor - b->minor;
    if (a->patch != b->patch) return a->patch - b->patch;
    bool a_pre = (a->prerelease[0] != 0);
    bool b_pre = (b->prerelease[0] != 0);
    if (!a_pre && b_pre) return 1;
    if (a_pre && !b_pre) return -1;
    if (a_pre && b_pre) return strcmp(a->prerelease, b->prerelease);
    return 0;
}

char *zan_version_format(const zan_version_t *v, char *buf, int buf_size) {
    if (v->prerelease[0]) {
        snprintf(buf, (size_t)buf_size, "%d.%d.%d-%s", v->major, v->minor, v->patch, v->prerelease);
    } else {
        snprintf(buf, (size_t)buf_size, "%d.%d.%d", v->major, v->minor, v->patch);
    }
    return buf;
}

/* ---- manifest parsing ---- */

static void skip_ws(const char **p) {
    while (**p && isspace((unsigned char)**p)) (*p)++;
}

static bool read_qstr(const char **p, char *out, int max_len) {
    skip_ws(p);
    if (**p != '"') return false;
    (*p)++;
    int i = 0;
    while (**p && **p != '"' && i < max_len - 1) { out[i++] = **p; (*p)++; }
    out[i] = 0;
    if (**p == '"') (*p)++;
    return true;
}

bool zan_pkg_load(zan_package_t *pkg, const char *manifest_path) {
    FILE *f = fopen(manifest_path, "r");
    if (!f) return false;
    memset(pkg, 0, sizeof(*pkg));
    pkg->dep_cap = 16;
    pkg->deps = (zan_dependency_t *)calloc((size_t)pkg->dep_cap, sizeof(zan_dependency_t));

    char line[1024];
    bool in_deps = false;
    while (fgets(line, sizeof(line), f)) {
        const char *p = line;
        skip_ws(&p);
        if (*p == '#' || *p == '\n' || *p == '\r' || *p == 0) continue;
        if (*p == '[') {
            in_deps = (strncmp(p, "[deps]", 6) == 0 || strncmp(p, "[dependencies]", 14) == 0);
            continue;
        }
        char key[128] = {0};
        int ki = 0;
        while (*p && *p != '=' && !isspace((unsigned char)*p) && ki < 127) { key[ki++] = *p; p++; }
        key[ki] = 0;
        skip_ws(&p);
        if (*p == '=') p++;
        skip_ws(&p);

        if (!in_deps) {
            char val[256] = {0};
            if (*p == '"') { read_qstr(&p, val, sizeof(val)); }
            else { int vi = 0; while (*p && *p != '\n' && *p != '\r' && vi < 255) { val[vi++] = *p; p++; } val[vi] = 0; }
            if (strcmp(key, "name") == 0) strncpy(pkg->name, val, sizeof(pkg->name) - 1);
            else if (strcmp(key, "version") == 0) {
                pkg->has_version = zan_version_parse(val, &pkg->version);
            }
            else if (strcmp(key, "description") == 0) strncpy(pkg->description, val, sizeof(pkg->description) - 1);
            else if (strcmp(key, "author") == 0) strncpy(pkg->author, val, sizeof(pkg->author) - 1);
            else if (strcmp(key, "license") == 0) strncpy(pkg->license, val, sizeof(pkg->license) - 1);
            else if (strcmp(key, "entry") == 0) strncpy(pkg->entry_point, val, sizeof(pkg->entry_point) - 1);
        } else {
            if (pkg->dep_count >= pkg->dep_cap) {
                pkg->dep_cap *= 2;
                pkg->deps = (zan_dependency_t *)realloc(pkg->deps, sizeof(zan_dependency_t) * (size_t)pkg->dep_cap);
            }
            zan_dependency_t *dep = &pkg->deps[pkg->dep_count];
            memset(dep, 0, sizeof(*dep));
            strncpy(dep->name, key, sizeof(dep->name) - 1);
            if (*p == '{') {
                p++;
                while (*p && *p != '}') {
                    skip_ws(&p);
                    char dkey[64] = {0}; int di = 0;
                    while (*p && *p != '=' && !isspace((unsigned char)*p) && *p != '}' && di < 63) { dkey[di++] = *p; p++; }
                    dkey[di] = 0; skip_ws(&p);
                    if (*p == '=') p++; skip_ws(&p);
                    char dval[512] = {0};
                    if (*p == '"') { read_qstr(&p, dval, sizeof(dval)); }
                    skip_ws(&p); if (*p == ',') p++;
                    if (strcmp(dkey, "source") == 0 || strcmp(dkey, "git") == 0) strncpy(dep->source, dval, sizeof(dep->source) - 1);
                    else if (strcmp(dkey, "version") == 0) {
                        const char *v = dval;
                        if (v[0] == '^') { dep->kind = ZAN_DEP_COMPAT; zan_version_parse(v + 1, &dep->min_ver); }
                        else if (v[0] == '>' && v[1] == '=') { dep->kind = ZAN_DEP_MINIMUM; zan_version_parse(v + 2, &dep->min_ver); }
                        else if (v[0] == '=') { dep->kind = ZAN_DEP_EXACT; zan_version_parse(v + 1, &dep->min_ver); }
                        else { dep->kind = ZAN_DEP_COMPAT; zan_version_parse(v, &dep->min_ver); }
                    }
                }
                if (*p == '}') p++;
            }
            pkg->dep_count++;
        }
    }
    fclose(f);
    return true;
}

bool zan_pkg_save(const zan_package_t *pkg, const char *manifest_path) {
    FILE *f = fopen(manifest_path, "w");
    if (!f) return false;
    char ver_buf[64];
    zan_version_format(&pkg->version, ver_buf, sizeof(ver_buf));
    fprintf(f, "# Zan Package Manifest\n");
    fprintf(f, "name = \"%s\"\n", pkg->name);
    fprintf(f, "version = \"%s\"\n", ver_buf);
    if (pkg->description[0]) fprintf(f, "description = \"%s\"\n", pkg->description);
    if (pkg->author[0]) fprintf(f, "author = \"%s\"\n", pkg->author);
    if (pkg->license[0]) fprintf(f, "license = \"%s\"\n", pkg->license);
    if (pkg->entry_point[0]) fprintf(f, "entry = \"%s\"\n", pkg->entry_point);
    if (pkg->dep_count > 0) {
        fprintf(f, "\n[deps]\n");
        for (int i = 0; i < pkg->dep_count; i++) {
            const zan_dependency_t *d = &pkg->deps[i];
            char dep_ver[64]; zan_version_format(&d->min_ver, dep_ver, sizeof(dep_ver));
            const char *prefix = "";
            switch (d->kind) {
            case ZAN_DEP_COMPAT: prefix = "^"; break;
            case ZAN_DEP_MINIMUM: prefix = ">="; break;
            case ZAN_DEP_EXACT: prefix = "="; break;
            case ZAN_DEP_RANGE: prefix = ">="; break;
            }
            fprintf(f, "%s = { source = \"%s\", version = \"%s%s\" }\n", d->name, d->source, prefix, dep_ver);
        }
    }
    bool ok = (ferror(f) == 0);
    if (fclose(f) != 0) ok = false;
    return ok;
}

void zan_pkg_new(zan_package_t *pkg, const char *name, const char *version) {
    memset(pkg, 0, sizeof(*pkg));
    strncpy(pkg->name, name, sizeof(pkg->name) - 1);
    zan_version_parse(version, &pkg->version);
    pkg->dep_cap = 16;
    pkg->deps = (zan_dependency_t *)calloc((size_t)pkg->dep_cap, sizeof(zan_dependency_t));
}

void zan_pkg_add_dep(zan_package_t *pkg, const char *name, const char *source, const char *version_constraint) {
    if (pkg->dep_count >= pkg->dep_cap) {
        pkg->dep_cap *= 2;
        pkg->deps = (zan_dependency_t *)realloc(pkg->deps, sizeof(zan_dependency_t) * (size_t)pkg->dep_cap);
    }
    zan_dependency_t *dep = &pkg->deps[pkg->dep_count++];
    memset(dep, 0, sizeof(*dep));
    strncpy(dep->name, name, sizeof(dep->name) - 1);
    strncpy(dep->source, source, sizeof(dep->source) - 1);
    const char *v = version_constraint;
    if (v[0] == '^') { dep->kind = ZAN_DEP_COMPAT; v++; }
    else if (v[0] == '>' && v[1] == '=') { dep->kind = ZAN_DEP_MINIMUM; v += 2; }
    else if (v[0] == '=') { dep->kind = ZAN_DEP_EXACT; v++; }
    else { dep->kind = ZAN_DEP_COMPAT; }
    zan_version_parse(v, &dep->min_ver);
}

bool zan_pkg_remove_dep(zan_package_t *pkg, const char *name) {
    for (int i = 0; i < pkg->dep_count; i++) {
        if (strcmp(pkg->deps[i].name, name) == 0) {
            memmove(&pkg->deps[i], &pkg->deps[i + 1], sizeof(zan_dependency_t) * (size_t)(pkg->dep_count - i - 1));
            pkg->dep_count--;
            return true;
        }
    }
    return false;
}

/* ---- resolution ---- */

static bool pkg_is_dir(const char *path);

static void ensure_dir_pkg(const char *path) {
#ifdef _WIN32
    CreateDirectoryA(path, NULL);
#else
    mkdir(path, 0755);
#endif
}

void zan_pkg_init(zan_pkg_registry_t *reg, const char *project_dir) {
    memset(reg, 0, sizeof(*reg));
    size_t dlen = strlen(project_dir);
    reg->cache_dir = (char *)malloc(dlen + 32);
    snprintf(reg->cache_dir, dlen + 32, "%s" PATH_SEP ".zan-packages", project_dir);
    ensure_dir_pkg(reg->cache_dir);
    reg->lock_file = (char *)malloc(dlen + 16);
    snprintf(reg->lock_file, dlen + 16, "%s" PATH_SEP "zan.lock", project_dir);
}

bool zan_pkg_version_satisfies(const zan_dependency_t *dep, const zan_version_t *ver) {
    switch (dep->kind) {
    case ZAN_DEP_EXACT: return zan_version_compare(ver, &dep->min_ver) == 0;
    case ZAN_DEP_MINIMUM: return zan_version_compare(ver, &dep->min_ver) >= 0;
    case ZAN_DEP_COMPAT: {
        if (zan_version_compare(ver, &dep->min_ver) < 0) return false;
        if (dep->min_ver.major > 0) return ver->major == dep->min_ver.major;
        else if (dep->min_ver.minor > 0) return ver->major == 0 && ver->minor == dep->min_ver.minor;
        return zan_version_compare(ver, &dep->min_ver) == 0;
    }
    case ZAN_DEP_RANGE:
        return zan_version_compare(ver, &dep->min_ver) >= 0 && zan_version_compare(ver, &dep->max_ver) < 0;
    }
    return false;
}

/* A dependency source and version are interpolated into a shell command below.
 * They originate from an untrusted zan.pkg manifest, so anything outside the
 * character set legitimately used by git remote URLs / semver strings is
 * rejected to prevent OS command injection. */
static bool pkg_token_is_shell_safe(const char *s) {
    if (!s) return true;
    for (const char *p = s; *p; p++) {
        unsigned char c = (unsigned char)*p;
        if (isalnum(c)) continue;
        if (strchr(":/@._~%+-", c) != NULL) continue;
        return false;
    }
    return true;
}

bool zan_pkg_fetch(zan_pkg_registry_t *reg, const zan_dependency_t *dep) {
    char pkg_dir[1024];
    snprintf(pkg_dir, sizeof(pkg_dir), "%s" PATH_SEP "%s", reg->cache_dir, dep->name);
    if (pkg_is_dir(pkg_dir)) return true;
    if (dep->source[0] && (strncmp(dep->source, "http", 4) == 0 ||
                           strncmp(dep->source, "git@", 4) == 0)) {
        char cmd[2048]; char ver_buf[64];
        zan_version_format(&dep->min_ver, ver_buf, sizeof(ver_buf));
        if (!pkg_token_is_shell_safe(dep->name) ||
            !pkg_token_is_shell_safe(dep->source) ||
            !pkg_token_is_shell_safe(ver_buf)) {
            fprintf(stderr, "error: refusing to fetch '%s': unsafe characters in package, source or version\n", dep->name);
            return false;
        }
        snprintf(cmd, sizeof(cmd), "git clone --depth 1 --branch v%s \"%s\" \"%s\" 2>&1", ver_buf, dep->source, pkg_dir);
        int ret = system(cmd);
        if (ret != 0) {
            snprintf(cmd, sizeof(cmd), "git clone --depth 1 \"%s\" \"%s\" 2>&1", dep->source, pkg_dir);
            ret = system(cmd);
        }
        return ret == 0;
    }
    return false;
}

/* Resolve `root`'s dependency tree. `seen` carries the names already resolved
 * during this run: the resolver has no other memory of what it visited, so a
 * dependency cycle (A -> B -> A) would otherwise recurse until zanc's stack
 * is exhausted. An already-seen name is simply not re-entered -- its own deps
 * were resolved when it was first seen. */
static bool zan_pkg_resolve_seen(zan_pkg_registry_t *reg, zan_package_t *root,
                                 char (*seen)[128], int *seen_count) {
    bool all_ok = true;
    for (int i = 0; i < root->dep_count; i++) {
        zan_dependency_t *dep = &root->deps[i];
        fprintf(stderr, "Resolving: %s\n", dep->name);
        if (!zan_pkg_fetch(reg, dep)) {
            fprintf(stderr, "error: failed to fetch package '%s' from %s\n", dep->name, dep->source);
            all_ok = false;
            continue;
        }
        char manifest_path[1024];
        snprintf(manifest_path, sizeof(manifest_path), "%s" PATH_SEP "%s" PATH_SEP "zan.pkg", reg->cache_dir, dep->name);
        zan_package_t fetched_pkg;
        if (zan_pkg_load(&fetched_pkg, manifest_path)) {
            if (!zan_pkg_version_satisfies(dep, &fetched_pkg.version)) {
                char ver_buf[64]; zan_version_format(&fetched_pkg.version, ver_buf, sizeof(ver_buf));
                fprintf(stderr, "warning: package '%s' version %s may not satisfy constraint\n", dep->name, ver_buf);
            }
            /* Propagate transitive resolution failures instead of dropping
             * them: an unresolved sub-dependency must fail the whole resolve. */
            if (fetched_pkg.dep_count > 0) {
                bool seen_before = false;
                for (int s = 0; s < *seen_count; s++) {
                    if (strcmp(seen[s], fetched_pkg.name) == 0) { seen_before = true; break; }
                }
                if (seen_before) {
                    fprintf(stderr, "note: '%s' is already resolved (shared dependency "
                                    "or dependency cycle); skipping its subtree\n",
                            fetched_pkg.name);
                } else if (*seen_count < 256) {
                    snprintf(seen[*seen_count], 128, "%s", fetched_pkg.name);
                    (*seen_count)++;
                    if (!zan_pkg_resolve_seen(reg, &fetched_pkg, seen, seen_count))
                        all_ok = false;
                } else {
                    fprintf(stderr, "error: dependency chain deeper than 256 packages\n");
                    all_ok = false;
                }
            }
            zan_pkg_destroy(&fetched_pkg);
        } else {
            fprintf(stderr, "error: failed to read manifest for package '%s'\n", dep->name);
            all_ok = false;
        }
    }
    return all_ok;
}

bool zan_pkg_resolve(zan_pkg_registry_t *reg, zan_package_t *root) {
    char seen[256][128];
    int seen_count = 0;
    /* seed with the root itself: a transitive dep on the root package must
     * terminate too */
    if (root->name[0]) {
        snprintf(seen[seen_count++], 128, "%s", root->name);
    }
    return zan_pkg_resolve_seen(reg, root, seen, &seen_count);
}

char **zan_pkg_get_sources(zan_pkg_registry_t *reg, int *out_count) {
    (void)reg;
    *out_count = 0;
    return NULL;
}

bool zan_pkg_write_lock(zan_pkg_registry_t *reg) {
    FILE *f = fopen(reg->lock_file, "w");
    if (!f) return false;
    fprintf(f, "# Zan lock file - auto-generated\n\n");
    for (int i = 0; i < reg->resolved_count; i++) {
        zan_package_t *pkg = reg->resolved[i];
        char ver_buf[64]; zan_version_format(&pkg->version, ver_buf, sizeof(ver_buf));
        fprintf(f, "[[package]]\nname = \"%s\"\nversion = \"%s\"\n\n", pkg->name, ver_buf);
    }
    bool ok = (ferror(f) == 0);
    if (fclose(f) != 0) ok = false;
    return ok;
}

bool zan_pkg_read_lock(zan_pkg_registry_t *reg) { (void)reg; return false; }

/* ---- installed package stores / safe local installation ---- */

static bool pkg_safe_component(const char *s) {
    if (!s || !*s || strcmp(s, ".") == 0 || strcmp(s, "..") == 0) return false;
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (!isalnum(c) && c != '_' && c != '-' && c != '.') return false;
    }
    return true;
}

static bool pkg_safe_namespace_path(const char *s) {
    if (!s || !*s || s[0] == '/' || s[0] == '\\') return false;
    char part[128]; size_t n = 0;
    for (;;) {
        char c = *s++;
        if (c == '/' || c == '\\' || c == 0) {
            if (n == 0 || n >= sizeof(part)) return false;
            part[n] = 0;
            if (!pkg_safe_component(part)) return false;
            n = 0;
            if (!c) return true;
        } else if (n + 1 < sizeof(part)) {
            part[n++] = c;
        } else return false;
    }
}

static bool pkg_is_dir(const char *path) {
#ifdef _WIN32
    DWORD a = GetFileAttributesA(path);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY) != 0;
#else
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
#endif
}

bool zan_pkg_global_store(char *out, size_t out_size) {
    const char *base;
#ifdef _WIN32
    base = getenv("LOCALAPPDATA");
    if (!base || !*base) return false;
    return snprintf(out, out_size, "%s\\Zan\\packages", base) > 0 &&
           strlen(out) < out_size;
#else
    base = getenv("XDG_DATA_HOME");
    if (base && *base)
        return snprintf(out, out_size, "%s/zan/packages", base) > 0 &&
               strlen(out) < out_size;
    base = getenv("HOME");
    if (!base || !*base) return false;
    return snprintf(out, out_size, "%s/.local/share/zan/packages", base) > 0 &&
           strlen(out) < out_size;
#endif
}

static int pkg_scan_store(const char *store, const char *namespace_path,
                          char (*out_dirs)[1024], int count, int max_dirs) {
    if (!pkg_is_dir(store)) return count;
#ifdef _WIN32
    char pattern[1024]; WIN32_FIND_DATAA fd;
    snprintf(pattern, sizeof(pattern), "%s\\*", store);
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return count;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
            (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
            !pkg_safe_component(fd.cFileName)) continue;
        char cand[1024];
        snprintf(cand, sizeof(cand), "%s\\%s\\stdlib\\%s", store,
                 fd.cFileName, namespace_path);
        if (pkg_is_dir(cand) && count < max_dirs)
            snprintf(out_dirs[count++], 1024, "%s", cand);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#else
    DIR *d = opendir(store); if (!d) return count;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (!pkg_safe_component(e->d_name)) continue;
        char root[1024], cand[1024]; struct stat st;
        snprintf(root, sizeof(root), "%s/%s", store, e->d_name);
        if (lstat(root, &st) != 0 || !S_ISDIR(st.st_mode) || S_ISLNK(st.st_mode)) continue;
        snprintf(cand, sizeof(cand), "%s/stdlib/%s", root, namespace_path);
        if (pkg_is_dir(cand) && count < max_dirs)
            snprintf(out_dirs[count++], 1024, "%s", cand);
    }
    closedir(d);
#endif
    return count;
}

int zan_pkg_find_namespace(const char *project_dir, const char *namespace_path,
                           char (*out_dirs)[1024], int max_dirs) {
    if (!project_dir || !pkg_safe_namespace_path(namespace_path) ||
        !out_dirs || max_dirs <= 0) return 0;
    char store[1024]; int count = 0;
    snprintf(store, sizeof(store), "%s" PATH_SEP ".zan-packages", project_dir);
    count = pkg_scan_store(store, namespace_path, out_dirs, count, max_dirs);
    if (zan_pkg_global_store(store, sizeof(store)))
        count = pkg_scan_store(store, namespace_path, out_dirs, count, max_dirs);
    return count;
}

static bool pkg_mkdirs(const char *path) {
    char tmp[1024]; size_t len = strlen(path);
    if (len == 0 || len >= sizeof(tmp)) return false;
    memcpy(tmp, path, len + 1);
    for (size_t i = 1; i <= len; i++) {
        if (tmp[i] != '/' && tmp[i] != '\\' && tmp[i] != 0) continue;
#ifdef _WIN32
        if (i == 2 && tmp[1] == ':') continue;
#endif
        char save = tmp[i]; tmp[i] = 0;
        if (*tmp && !pkg_is_dir(tmp)) {
#ifdef _WIN32
            if (!CreateDirectoryA(tmp, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) return false;
#else
            if (mkdir(tmp, 0755) != 0 && errno != EEXIST) return false;
#endif
        }
        tmp[i] = save;
    }
    return true;
}

static bool pkg_copy_tree(const char *src, const char *dst, int depth) {
    if (depth > 64 || !pkg_mkdirs(dst)) return false;
#ifdef _WIN32
    char pattern[1024]; WIN32_FIND_DATAA fd;
    snprintf(pattern, sizeof(pattern), "%s\\*", src);
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return false;
    bool ok = true;
    do {
        if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) continue;
        if (!pkg_safe_component(fd.cFileName) ||
            (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) { ok = false; break; }
        char a[1024], b[1024];
        snprintf(a, sizeof(a), "%s\\%s", src, fd.cFileName);
        snprintf(b, sizeof(b), "%s\\%s", dst, fd.cFileName);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ok = pkg_copy_tree(a, b, depth + 1);
        else ok = CopyFileA(a, b, TRUE) != 0;
    } while (ok && FindNextFileA(h, &fd));
    FindClose(h); return ok;
#else
    DIR *d = opendir(src); if (!d) return false;
    bool ok = true; struct dirent *e;
    while (ok && (e = readdir(d)) != NULL) {
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
        if (!pkg_safe_component(e->d_name)) { ok = false; break; }
        char a[1024], b[1024]; struct stat st;
        snprintf(a, sizeof(a), "%s/%s", src, e->d_name);
        snprintf(b, sizeof(b), "%s/%s", dst, e->d_name);
        if (lstat(a, &st) != 0 || S_ISLNK(st.st_mode)) { ok = false; break; }
        if (S_ISDIR(st.st_mode)) ok = pkg_copy_tree(a, b, depth + 1);
        else if (S_ISREG(st.st_mode)) {
            FILE *in = fopen(a, "rb"), *out = in ? fopen(b, "wb") : NULL;
            if (!out) { if (in) fclose(in); ok = false; break; }
            char buf[16384]; size_t n;
            while ((n = fread(buf, 1, sizeof(buf), in)) != 0)
                if (fwrite(buf, 1, n, out) != n) { ok = false; break; }
            if (ferror(in) || fclose(out) != 0) ok = false;
            fclose(in);
        } else ok = false;
    }
    closedir(d); return ok;
#endif
}

static void pkg_remove_tree(const char *path) {
#ifdef _WIN32
    char pattern[1024]; WIN32_FIND_DATAA fd;
    snprintf(pattern, sizeof(pattern), "%s\\*", path);
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) continue;
            char p[1024]; snprintf(p, sizeof(p), "%s\\%s", path, fd.cFileName);
            if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
                !(fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) pkg_remove_tree(p);
            else DeleteFileA(p);
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
    RemoveDirectoryA(path);
#else
    DIR *d = opendir(path); struct dirent *e;
    if (d) { while ((e = readdir(d)) != NULL) {
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
        char p[1024]; struct stat st; snprintf(p, sizeof(p), "%s/%s", path, e->d_name);
        if (lstat(p, &st) == 0 && S_ISDIR(st.st_mode) && !S_ISLNK(st.st_mode)) pkg_remove_tree(p);
        else unlink(p);
    } closedir(d); }
    rmdir(path);
#endif
}

bool zan_pkg_install_local(const char *source_dir, const char *package_name,
                           zan_pkg_scope_t scope, const char *project_dir,
                           char *status, size_t status_size) {
    if (!source_dir || !pkg_safe_component(package_name) || !pkg_is_dir(source_dir)) {
        snprintf(status, status_size, "ZANPKG_STATUS action=install status=invalid_source"); return false;
    }
    char manifest[1024];
    snprintf(manifest, sizeof(manifest), "%s" PATH_SEP "zan.pkg", source_dir);
    zan_package_t pkg;
    if (!zan_pkg_load(&pkg, manifest) || !pkg.name[0] || strcmp(pkg.name, package_name) != 0) {
        snprintf(status, status_size, "ZANPKG_STATUS action=install status=invalid_manifest package=%s", package_name);
        return false;
    }
    /* An unversioned package cannot be upgraded, pinned or audited, so the
     * secure installer refuses it outright. */
    if (!pkg.has_version) {
        snprintf(status, status_size, "ZANPKG_STATUS action=install status=missing_version package=%s", package_name);
        zan_pkg_destroy(&pkg);
        return false;
    }
    /* The compiler only discovers installed packages through their
     * stdlib/<namespace>/ layout, so a package without it would install
     * silently and never resolve. */
    char pkg_stdlib[1024];
    snprintf(pkg_stdlib, sizeof(pkg_stdlib), "%s" PATH_SEP "stdlib", source_dir);
    if (!pkg_is_dir(pkg_stdlib)) {
        snprintf(status, status_size, "ZANPKG_STATUS action=install status=no_stdlib_layout package=%s", package_name);
        zan_pkg_destroy(&pkg);
        return false;
    }
    zan_pkg_destroy(&pkg);
    char store[1024];
    if (scope == ZAN_PKG_SCOPE_GLOBAL) {
        if (!zan_pkg_global_store(store, sizeof(store))) {
            snprintf(status, status_size, "ZANPKG_STATUS action=install status=no_global_store package=%s", package_name); return false;
        }
    } else {
        if (!project_dir || !*project_dir) {
            snprintf(status, status_size, "ZANPKG_STATUS action=install status=no_project package=%s", package_name); return false;
        }
        snprintf(store, sizeof(store), "%s" PATH_SEP ".zan-packages", project_dir);
    }
    if (!pkg_mkdirs(store)) {
        snprintf(status, status_size, "ZANPKG_STATUS action=install status=io_error package=%s", package_name); return false;
    }
    char dst[1024], stage[1024];
    snprintf(dst, sizeof(dst), "%s" PATH_SEP "%s", store, package_name);
#ifdef _WIN32
    snprintf(stage, sizeof(stage), "%s" PATH_SEP ".%s.tmp.%lu", store, package_name, (unsigned long)GetCurrentProcessId());
#else
    snprintf(stage, sizeof(stage), "%s/.%s.tmp.%ld", store, package_name, (long)getpid());
#endif
    if (pkg_is_dir(dst)) {
        snprintf(status, status_size, "ZANPKG_STATUS action=install status=exists package=%s", package_name); return false;
    }
    pkg_remove_tree(stage);
    if (!pkg_copy_tree(source_dir, stage, 0)) {
        pkg_remove_tree(stage);
        snprintf(status, status_size, "ZANPKG_STATUS action=install status=unsafe_or_io package=%s", package_name); return false;
    }
#ifdef _WIN32
    bool renamed = MoveFileExA(stage, dst, MOVEFILE_WRITE_THROUGH) != 0;
#else
    bool renamed = rename(stage, dst) == 0;
#endif
    if (!renamed) { pkg_remove_tree(stage); snprintf(status, status_size, "ZANPKG_STATUS action=install status=io_error package=%s", package_name); return false; }
    snprintf(status, status_size, "ZANPKG_STATUS action=install status=installed package=%s scope=%s",
             package_name, scope == ZAN_PKG_SCOPE_GLOBAL ? "global" : "project");
    return true;
}

bool zan_pkg_api_validate(const char *api_url, char *status, size_t status_size) {
    bool secure = api_url && (strncmp(api_url, "https://", 8) == 0 ||
        strncmp(api_url, "http://localhost", 16) == 0 ||
        strncmp(api_url, "http://127.0.0.1", 16) == 0 ||
        strncmp(api_url, "http://[::1]", 12) == 0);
    if (!secure) {
        snprintf(status, status_size, "ZANPKG_STATUS action=api status=insecure_url");
        return false;
    }
    snprintf(status, status_size, "ZANPKG_STATUS action=remote status=unsupported capability=http_archive_signature");
    return true;
}

void zan_pkg_destroy(zan_package_t *pkg) {
    free(pkg->deps);
    free(pkg->source_dirs);
    memset(pkg, 0, sizeof(*pkg));
}

void zan_pkg_registry_destroy(zan_pkg_registry_t *reg) {
    free(reg->cache_dir);
    free(reg->lock_file);
    for (int i = 0; i < reg->resolved_count; i++) { zan_pkg_destroy(reg->resolved[i]); free(reg->resolved[i]); }
    free(reg->resolved);
    memset(reg, 0, sizeof(*reg));
}
