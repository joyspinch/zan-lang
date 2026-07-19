/* package.c -- Zan package manager implementation. */

#include "package.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

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
            else if (strcmp(key, "version") == 0) zan_version_parse(val, &pkg->version);
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
    fclose(f);
    return true;
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
 * rejected to prevent OS command injection (e.g. a source containing a double
 * quote followed by "; rm -rf ~"). */
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
    FILE *f = fopen(pkg_dir, "r");
    if (f) { fclose(f); return true; }
    if (dep->source[0] && (strncmp(dep->source, "http", 4) == 0 || strncmp(dep->source, "git@", 4) == 0)) {
        char cmd[2048]; char ver_buf[64];
        zan_version_format(&dep->min_ver, ver_buf, sizeof(ver_buf));
        if (!pkg_token_is_shell_safe(dep->source) || !pkg_token_is_shell_safe(ver_buf)) {
            fprintf(stderr, "error: refusing to fetch '%s': unsafe characters in source or version\n", dep->name);
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

bool zan_pkg_resolve(zan_pkg_registry_t *reg, zan_package_t *root) {
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
            if (fetched_pkg.dep_count > 0) zan_pkg_resolve(reg, &fetched_pkg);
            zan_pkg_destroy(&fetched_pkg);
        }
    }
    return all_ok;
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
    fclose(f);
    return true;
}

bool zan_pkg_read_lock(zan_pkg_registry_t *reg) { (void)reg; return false; }

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
