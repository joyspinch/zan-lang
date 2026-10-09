/* 模块核心语义抽象与接口调用契约 */

#ifndef _WIN32
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
#endif
#endif

#include "package.h"
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#include <io.h>
#include <process.h>
#define fixture_mkdir(path) _mkdir(path)
#define fixture_unlink(path) _unlink(path)
#define fixture_rmdir(path) _rmdir(path)
#define fixture_pid() ((long)_getpid())
#define PROFILE_ENV "LOCALAPPDATA"
#else
#include <sys/stat.h>
#include <unistd.h>
#define fixture_mkdir(path) mkdir((path), 0700)
#define fixture_unlink(path) unlink(path)
#define fixture_rmdir(path) rmdir(path)
#define fixture_pid() ((long)getpid())
#define PROFILE_ENV "XDG_DATA_HOME"
#endif

#define NS "ZanIndexFixture"
#define DECLARE(suffix) "ns " NS "." suffix "\n"
#define ARRAY_COUNT(array) (sizeof(array) / sizeof((array)[0]))

enum { PATH_CAP = 4096, MAX_FILES = 96, MAX_DIRS = 256, MANY_QUERIES = 256 };

static int failures;
static unsigned long probe_calls;

#define EXPECT(condition, ...) do {                                        \
    if (!(condition)) {                                                    \
        ++failures;                                                        \
        fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__);                 \
        fprintf(stderr, __VA_ARGS__);                                      \
        fputc('\n', stderr);                                               \
    }                                                                      \
} while (0)

enum fixture_store {
    STORE_A_PACKAGES,
    STORE_A_CACHE,
    STORE_B_PACKAGES,
    STORE_GLOBAL,
    STORE_OUTSIDE,
    STORE_COUNT
};

enum fixture_file {
    F_A_OTHER_FIRST,
    F_A_TREE_FIRST,
    F_A_GRAND,
    F_A_CHILD,
    F_A_TREE_IN_DIR,
    F_A_OTHER_IN_DIR,
    F_A_TREE_IN_BRANCH,
    F_A_BOUNDARY,
    F_A_CHILD_IN_ROOT,
    F_A_TREE_LAST,
    F_SECOND_CHILD,
    F_SECOND_TREE,
    F_HELPER_TREE,
    F_HELPER_CHILD,
    F_FAILED_CHILD,
    F_HELPER_GRAND,
    F_MOVED,
    F_FLAT_LAYOUT,
    F_FLAT_HELPER,
    F_SRC_LAYOUT,
    F_SRC_HELPER,
    F_SRC_IGNORED_STDLIB,
    F_SRC_IGNORED_FLAT,
    F_STDLIB_LAYOUT,
    F_STDLIB_HELPER,
    F_STDLIB_IGNORED_FLAT,
    F_A_ISOLATION,
    F_B_ISOLATION,
    F_PROJECT_SHADOW,
    F_CACHED_SHADOW,
    F_GLOBAL_SHADOW,
    F_CACHE_SHADOW_WINNER,
    F_GLOBAL_CACHE_SHADOW,
    F_CACHE_CHILD,
    F_CACHE_TREE,
    F_GLOBAL_CHILD,
    F_GLOBAL_GRAND,
    F_GLOBAL_TREE,
    F_OUTSIDE,
    F_SOURCE_COUNT,
    F_ADDED = F_SOURCE_COUNT,
    F_LINK_FILE,
    F_LINK_DIRECTORY
};

struct source_spec {
    enum fixture_store store;
    const char *relative_path;
    const char *first_line;
    unsigned visible_a;
    unsigned visible_b;
};

/* 模块核心语义抽象与接口调用契约 */
static const struct source_spec source_specs[F_SOURCE_COUNT] = {
    [F_A_OTHER_FIRST] = { STORE_A_PACKAGES,
        NS ".AOrder/src/A-before.zan", DECLARE("Other"), 1, 0 },
    [F_A_TREE_FIRST] = { STORE_A_PACKAGES,
        NS ".AOrder/src/a-dir/A-first.zan", DECLARE("Tree"), 1, 0 },
    [F_A_GRAND] = { STORE_A_PACKAGES,
        NS ".AOrder/src/a-dir/a-grand.zan", DECLARE("Tree.Child.Grand"), 1, 0 },
    [F_A_CHILD] = { STORE_A_PACKAGES,
        NS ".AOrder/src/a-dir/b-child.zan", DECLARE("Tree.Child"), 1, 0 },
    [F_A_TREE_IN_DIR] = { STORE_A_PACKAGES,
        NS ".AOrder/src/a-dir/z-root.zan", DECLARE("Tree"), 1, 0 },
    [F_A_OTHER_IN_DIR] = { STORE_A_PACKAGES,
        NS ".AOrder/src/b-branch/a-cross.zan", DECLARE("Other"), 1, 0 },
    [F_A_TREE_IN_BRANCH] = { STORE_A_PACKAGES,
        NS ".AOrder/src/b-branch/b-root.zan", DECLARE("Tree"), 1, 0 },
    [F_A_BOUNDARY] = { STORE_A_PACKAGES,
        NS ".AOrder/src/b-branch/c-boundary.zan", DECLARE("Treeish"), 1, 0 },
    [F_A_CHILD_IN_ROOT] = { STORE_A_PACKAGES,
        NS ".AOrder/src/m-root.zan", DECLARE("Tree.Child"), 1, 0 },
    [F_A_TREE_LAST] = { STORE_A_PACKAGES,
        NS ".AOrder/src/z-tail.zan", DECLARE("Tree"), 1, 0 },
    [F_SECOND_CHILD] = { STORE_A_PACKAGES,
        NS ".BOrder/src/a-child.zan", DECLARE("Tree.Child"), 1, 0 },
    [F_SECOND_TREE] = { STORE_A_PACKAGES,
        NS ".BOrder/src/z-root.zan", DECLARE("Tree"), 1, 0 },
    [F_HELPER_TREE] = { STORE_A_PACKAGES,
        NS ".Helpers/src/" NS "/Tree/Helper.zan", "helper\n", 1, 0 },
    [F_HELPER_CHILD] = { STORE_A_PACKAGES,
        NS ".Helpers/src/" NS "/Tree/Child/LeafHelper.zan", "helper\n", 1, 0 },
    [F_FAILED_CHILD] = { STORE_A_PACKAGES,
        NS ".Helpers/src/" NS "/Tree/Child/Broken.zan", "probe-failure\n", 1, 0 },
    [F_HELPER_GRAND] = { STORE_A_PACKAGES,
        NS ".Helpers/src/" NS "/Tree/Child/Grand/DeepHelper.zan", "helper\n", 1, 0 },
    [F_MOVED] = { STORE_A_PACKAGES,
        NS ".Helpers/src/" NS "/FalseFolder/Moved.zan", DECLARE("Elsewhere"), 1, 0 },
    [F_FLAT_LAYOUT] = { STORE_A_PACKAGES,
        NS ".LayoutFlat/selected.zan", DECLARE("Layout"), 1, 0 },
    [F_FLAT_HELPER] = { STORE_A_PACKAGES,
        NS ".LayoutFlat/" NS "/Layout/Child/Helper.zan", "helper\n", 1, 0 },
    [F_SRC_LAYOUT] = { STORE_A_PACKAGES,
        NS ".LayoutSrc/src/selected.zan", DECLARE("Layout"), 1, 0 },
    [F_SRC_HELPER] = { STORE_A_PACKAGES,
        NS ".LayoutSrc/src/" NS "/Layout/Child/Helper.zan", "helper\n", 1, 0 },
    [F_SRC_IGNORED_STDLIB] = { STORE_A_PACKAGES,
        NS ".LayoutSrc/stdlib/ignored.zan", DECLARE("Layout"), 0, 0 },
    [F_SRC_IGNORED_FLAT] = { STORE_A_PACKAGES,
        NS ".LayoutSrc/ignored.zan", DECLARE("Layout"), 0, 0 },
    [F_STDLIB_LAYOUT] = { STORE_A_PACKAGES,
        NS ".LayoutStdlib/stdlib/selected.zan", DECLARE("Layout"), 1, 0 },
    [F_STDLIB_HELPER] = { STORE_A_PACKAGES,
        NS ".LayoutStdlib/stdlib/" NS "/Layout/Child/Helper.zan", "helper\n", 1, 0 },
    [F_STDLIB_IGNORED_FLAT] = { STORE_A_PACKAGES,
        NS ".LayoutStdlib/ignored.zan", DECLARE("Layout"), 0, 0 },
    [F_A_ISOLATION] = { STORE_A_PACKAGES,
        NS ".Isolation/src/marker.zan", DECLARE("Isolation"), 1, 0 },
    [F_B_ISOLATION] = { STORE_B_PACKAGES,
        NS ".Isolation/src/marker.zan", DECLARE("Isolation"), 0, 1 },
    [F_PROJECT_SHADOW] = { STORE_A_PACKAGES,
        NS ".Shadow/src/high.zan", DECLARE("Shadow.High"), 1, 0 },
    [F_CACHED_SHADOW] = { STORE_A_CACHE,
        NS ".Shadow/src/low.zan", DECLARE("Shadow.Cached"), 0, 0 },
    [F_GLOBAL_SHADOW] = { STORE_GLOBAL,
        NS ".Shadow/src/low.zan", DECLARE("Shadow.Global"), 0, 1 },
    [F_CACHE_SHADOW_WINNER] = { STORE_A_CACHE,
        NS ".CacheShadow/src/winner.zan", DECLARE("CacheShadow.Winner"), 1, 0 },
    [F_GLOBAL_CACHE_SHADOW] = { STORE_GLOBAL,
        NS ".CacheShadow/src/hidden.zan", DECLARE("CacheShadow.Hidden"), 0, 1 },
    [F_CACHE_CHILD] = { STORE_A_CACHE,
        NS ".COrder/src/a-child.zan", DECLARE("Tree.Child"), 1, 0 },
    [F_CACHE_TREE] = { STORE_A_CACHE,
        NS ".COrder/src/z-root.zan", DECLARE("Tree"), 1, 0 },
    [F_GLOBAL_CHILD] = { STORE_GLOBAL,
        NS ".DOrder/src/a-child.zan", DECLARE("Tree.Child"), 1, 1 },
    [F_GLOBAL_GRAND] = { STORE_GLOBAL,
        NS ".DOrder/src/m-grand.zan", DECLARE("Tree.Child.Grand"), 1, 1 },
    [F_GLOBAL_TREE] = { STORE_GLOBAL,
        NS ".DOrder/src/z-root.zan", DECLARE("Tree"), 1, 1 },
    [F_OUTSIDE] = { STORE_OUTSIDE,
        "source.zan", DECLARE("Symlink"), 0, 0 }
};

struct tracked_file {
    char *path;
    unsigned long probes;
};

struct fixture {
    char root[PATH_CAP];
    char project_a[PATH_CAP];
    char project_b[PATH_CAP];
    char profile[PATH_CAP];
    char stores[STORE_COUNT][PATH_CAP];
    struct tracked_file files[MAX_FILES];
    char *directories[MAX_DIRS];
    size_t directory_count;
    char *old_profile_env;
    int had_profile_env;
    int profile_env_changed;
};

static struct fixture fixture;

static int path_char(unsigned char c) {
    if (c == '\\') c = '/';
#ifdef _WIN32
    c = (unsigned char)tolower(c);
#endif
    return c;
}

static int same_path(const char *left, const char *right) {
    while (*left && *right) {
        if (path_char((unsigned char)*left++) !=
            path_char((unsigned char)*right++)) return 0;
    }
    return *left == *right;
}

static int path_within(const char *path, const char *parent) {
    while (*parent) {
        if (!*path || path_char((unsigned char)*path++) !=
                      path_char((unsigned char)*parent++)) return 0;
    }
    return !*path || path_char((unsigned char)*path) == '/';
}

static void normalize_windows_path(char *path) {
#ifdef _WIN32
    for (; *path; ++path) if (*path == '\\') *path = '/';
#else
    (void)path;
#endif
}

static char *copy_string(const char *value) {
    size_t size = strlen(value) + 1;
    char *copy = (char *)malloc(size);
    EXPECT(copy != NULL, "allocate %zu bytes for fixture bookkeeping", size);
    if (copy) memcpy(copy, value, size);
    return copy;
}

static int join_path(char out[PATH_CAP], const char *parent, const char *child) {
    size_t length = strlen(parent);
    const char *separator = length && path_char((unsigned char)parent[length - 1]) == '/'
                          ? "" : "/";
    int written = snprintf(out, PATH_CAP, "%s%s%s", parent, separator, child);
    EXPECT(written >= 0 && written < PATH_CAP, "fixture path is too long");
    if (written < 0 || written >= PATH_CAP) return 0;
    normalize_windows_path(out);
    return 1;
}

static int create_fixture_root(const char *scratch_parent) {
#ifdef _WIN32
    char *absolute_parent = _fullpath(NULL, scratch_parent, 0);
#else
    char *absolute_parent = realpath(scratch_parent, NULL);
#endif
    EXPECT(absolute_parent != NULL, "resolve scratch parent '%s': %s",
           scratch_parent, strerror(errno));
    if (!absolute_parent) return 0;
    normalize_windows_path(absolute_parent);

    for (unsigned attempt = 0; attempt < 1000; ++attempt) {
        char child[96];
        snprintf(child, sizeof(child), "package_namespace_%ld_%u", fixture_pid(), attempt);
        if (!join_path(fixture.root, absolute_parent, child)) break;
        char *owned_path = copy_string(fixture.root);
        if (!owned_path) break;
        if (fixture_mkdir(fixture.root) == 0) {
            fixture.directories[fixture.directory_count++] = owned_path;
            free(absolute_parent);
            return 1;
        }
        int error = errno;
        free(owned_path);
        if (error != EEXIST) {
            EXPECT(0, "create fixture root '%s': %s", fixture.root, strerror(error));
            free(absolute_parent);
            return 0;
        }
    }
    free(absolute_parent);
    EXPECT(0, "could not create an exclusive PID-specific fixture directory");
    return 0;
}

static int create_directory(const char *path) {
    EXPECT(path_within(path, fixture.root), "directory escaped fixture: %s", path);
    if (!path_within(path, fixture.root)) return 0;
    for (size_t i = 0; i < fixture.directory_count; ++i)
        if (same_path(path, fixture.directories[i])) return 1;
    EXPECT(fixture.directory_count < MAX_DIRS, "directory bookkeeping overflow");
    if (fixture.directory_count >= MAX_DIRS) return 0;
    char *owned_path = copy_string(path);
    if (!owned_path) return 0;
    int result = fixture_mkdir(path);
    EXPECT(result == 0, "create fixture directory '%s': %s", path, strerror(errno));
    if (result != 0) {
        free(owned_path);
        return 0;
    }
    fixture.directories[fixture.directory_count++] = owned_path;
    return 1;
}

static int create_directory_tree(const char *path) {
    char current[PATH_CAP];
    size_t length = strlen(path);
    EXPECT(length < sizeof(current) && path_within(path, fixture.root),
           "invalid fixture directory: %s", path);
    if (length >= sizeof(current) || !path_within(path, fixture.root)) return 0;
    memcpy(current, path, length + 1);
    for (size_t i = strlen(fixture.root) + 1; i < length; ++i) {
        if (current[i] == '/') {
            current[i] = '\0';
            int ok = create_directory(current);
            current[i] = '/';
            if (!ok) return 0;
        }
    }
    return create_directory(current);
}

static int create_parent_directories(const char *path) {
    char parent[PATH_CAP];
    size_t length = strlen(path);
    EXPECT(length < sizeof(parent), "source path is too long");
    if (length >= sizeof(parent)) return 0;
    memcpy(parent, path, length + 1);
    char *separator = strrchr(parent, '/');
    EXPECT(separator != NULL, "source has no parent directory: %s", path);
    if (!separator) return 0;
    *separator = '\0';
    return create_directory_tree(parent);
}

static int create_source(int id, enum fixture_store store,
                         const char *relative_path, const char *first_line) {
    char path[PATH_CAP];
    EXPECT(id >= 0 && id < MAX_FILES, "invalid fixture source ID %d", id);
    if (id < 0 || id >= MAX_FILES) return 0;
    EXPECT(fixture.files[id].path == NULL, "fixture source ID %d already exists", id);
    if (fixture.files[id].path) return 0;
    if (!join_path(path, fixture.stores[store], relative_path) ||
        !create_parent_directories(path)) return 0;
    char *owned_path = copy_string(path);
    if (!owned_path) return 0;
    FILE *stream = fopen(path, "wb");
    EXPECT(stream != NULL, "create fixture source '%s': %s", path, strerror(errno));
    if (!stream) {
        free(owned_path);
        return 0;
    }
    fixture.files[id].path = owned_path;
    int wrote = fputs(first_line, stream) != EOF;
    int closed = fclose(stream) == 0;
    EXPECT(wrote && closed, "write fixture source '%s'", path);
    return wrote && closed;
}

static int set_profile_env(const char *value) {
#ifdef _WIN32
    int result = _putenv_s(PROFILE_ENV, value ? value : "");
#else
    int result = value ? setenv(PROFILE_ENV, value, 1) : unsetenv(PROFILE_ENV);
#endif
    EXPECT(result == 0, "set/restore %s", PROFILE_ENV);
    return result == 0;
}

static int prepare_fixture(const char *scratch_parent) {
    if (!create_fixture_root(scratch_parent) ||
        !join_path(fixture.project_a, fixture.root, "project-a") ||
        !join_path(fixture.project_b, fixture.root, "project-b") ||
        !join_path(fixture.profile, fixture.root, "profile") ||
        !create_directory_tree(fixture.profile)) return 0;

    const char *old_profile = getenv(PROFILE_ENV);
    fixture.had_profile_env = old_profile != NULL;
    if (old_profile) {
        fixture.old_profile_env = copy_string(old_profile);
        if (!fixture.old_profile_env) return 0;
    }
    if (!set_profile_env(fixture.profile)) return 0;
    fixture.profile_env_changed = 1;

    if (!join_path(fixture.stores[STORE_A_PACKAGES], fixture.project_a, "packages") ||
        !join_path(fixture.stores[STORE_A_CACHE], fixture.project_a, ".zan-packages") ||
        !join_path(fixture.stores[STORE_B_PACKAGES], fixture.project_b, "packages") ||
        !join_path(fixture.stores[STORE_OUTSIDE], fixture.root, "outside")) return 0;
    int global_ok = zan_pkg_global_store(fixture.stores[STORE_GLOBAL], PATH_CAP);
    EXPECT(global_ok, "resolve isolated global package store");
    if (!global_ok) return 0;
    normalize_windows_path(fixture.stores[STORE_GLOBAL]);
    EXPECT(path_within(fixture.stores[STORE_GLOBAL], fixture.profile),
           "global store escaped isolated profile: %s", fixture.stores[STORE_GLOBAL]);
    if (!path_within(fixture.stores[STORE_GLOBAL], fixture.profile)) return 0;

    for (size_t i = F_SOURCE_COUNT; i > 0; --i) {
        const struct source_spec *spec = &source_specs[i - 1];
        if (!create_source((int)i - 1, spec->store, spec->relative_path,
                           spec->first_line)) return 0;
    }
    return 1;
}

#ifndef _WIN32
static int create_optional_symlink(int id, const char *relative_path, const char *target) {
    char path[PATH_CAP];
    if (!join_path(path, fixture.stores[STORE_A_PACKAGES], relative_path) ||
        !create_parent_directories(path)) return 0;
    char *owned_path = copy_string(path);
    if (!owned_path) return 0;
    if (symlink(target, path) == 0) {
        fixture.files[id].path = owned_path;
        return 1;
    }
    int error = errno;
    free(owned_path);
    if (error == ENOSYS || error == EPERM || error == EACCES || error == EOPNOTSUPP) {
        fprintf(stderr, "SKIP symlink fixture '%s': %s\n", path, strerror(error));
        return 1;
    }
    EXPECT(0, "create symlink fixture '%s': %s", path, strerror(error));
    return 0;
}
#endif

static int prepare_symlinks(void) {
#ifndef _WIN32
    if (!create_optional_symlink(F_LINK_FILE, NS ".AOrder/src/link-file.zan",
                                 fixture.files[F_A_TREE_LAST].path) ||
        !create_optional_symlink(F_LINK_DIRECTORY, NS ".AOrder/src/link-directory",
                                 fixture.stores[STORE_OUTSIDE])) return 0;
#endif
    return 1;
}

static void cleanup_fixture(void) {
    if (fixture.profile_env_changed) {
        set_profile_env(fixture.had_profile_env ? fixture.old_profile_env : NULL);
        fixture.profile_env_changed = 0;
    }
    free(fixture.old_profile_env);
    fixture.old_profile_env = NULL;

    /* 模块核心语义抽象与接口调用契约 */
    for (int i = MAX_FILES - 1; i >= 0; --i) {
        if (!fixture.files[i].path) continue;
        int result = fixture_unlink(fixture.files[i].path);
        EXPECT(result == 0, "remove owned fixture file '%s': %s",
               fixture.files[i].path, strerror(errno));
        free(fixture.files[i].path);
        fixture.files[i].path = NULL;
    }
    while (fixture.directory_count) {
        char *path = fixture.directories[--fixture.directory_count];
        int result = fixture_rmdir(path);
        EXPECT(result == 0, "remove owned fixture directory '%s': %s", path, strerror(errno));
        free(path);
    }
}

static int find_fixture_file(const char *path) {
    for (int i = 0; i < MAX_FILES; ++i)
        if (fixture.files[i].path && same_path(path, fixture.files[i].path)) return i;
    return -1;
}

static int namespace_probe(const char *path, char *out_ns, size_t cap) {
    ++probe_calls;
    if (out_ns && cap) out_ns[0] = '\0';
    int id = find_fixture_file(path);
    if (id < 0) {
        /* 模块核心语义抽象与接口调用契约 */
        EXPECT(!path_within(path, fixture.root), "probed untracked fixture path: %s", path);
        return 0;
    }
    ++fixture.files[id].probes;
    if (!out_ns || !cap) return 0;

    FILE *stream = fopen(path, "rb");
    EXPECT(stream != NULL, "open fixture in namespace probe: %s", path);
    if (!stream) return 0;
    char line[2048];
    int read = fgets(line, sizeof(line), stream) != NULL;
    int closed = fclose(stream) == 0;
    EXPECT(read && closed, "read first line of fixture source: %s", path);
    if (!read || !closed) return 0;
    if (strncmp(line, "probe-failure", sizeof("probe-failure") - 1) == 0) return 0;

    char declared[1024];
    if (sscanf(line, "ns %1023s", declared) == 1) {
        size_t length = strlen(declared);
        if (length >= cap) return 0;
        memcpy(out_ns, declared, length + 1);
    }
    /* 模块核心语义抽象与接口调用契约 */
    return 1;
}

struct visit_result {
    int file_ids[MAX_FILES];
    size_t count;
    int overflow;
};

static void collect_source(const char *path, void *context) {
    struct visit_result *result = (struct visit_result *)context;
    EXPECT(result != NULL, "visitor context must be preserved");
    if (!result) return;
    int id = find_fixture_file(path);
    EXPECT(id >= 0, "visitor returned a non-fixture/original-source path: %s", path);
    if (result->count < MAX_FILES) result->file_ids[result->count] = id;
    else result->overflow = 1;
    ++result->count;
}

static void check_result(const char *query, int hierarchical, int returned,
                         const struct visit_result *result,
                         const int *expected, size_t expected_count) {
    const char *name = query ? query : "(null)";
    EXPECT(returned == (int)expected_count,
           "query '%s', hierarchical=%d: returned %d, expected %zu",
           name, hierarchical, returned, expected_count);
    EXPECT(result->count == expected_count && !result->overflow,
           "query '%s', hierarchical=%d: %zu callbacks, expected %zu",
           name, hierarchical, result->count, expected_count);
    size_t compare_count = result->count < expected_count ? result->count : expected_count;
    if (compare_count > MAX_FILES) compare_count = MAX_FILES;
    for (size_t i = 0; i < compare_count; ++i) {
        int actual = result->file_ids[i];
        EXPECT(actual == expected[i],
               "query '%s', hierarchical=%d: callback %zu is '%s', expected '%s'",
               name, hierarchical, i,
               actual >= 0 ? fixture.files[actual].path : "(untracked)",
               fixture.files[expected[i]].path);
    }
}

static void expect_query(const zan_pkg_source_index_t *index, const char *query,
                         int hierarchical, const int *expected, size_t expected_count) {
    struct visit_result result = { { 0 }, 0, 0 };
    int returned = zan_pkg_source_index_visit(index, query, collect_source, &result, hierarchical);
    check_result(query, hierarchical, returned, &result, expected, expected_count);
}

static void expect_legacy(const char *project, const char *query, int hierarchical,
                          const int *expected, size_t expected_count) {
    struct visit_result result = { { 0 }, 0, 0 };
    int returned = zan_pkg_visit_namespace(project, query, namespace_probe,
                                           collect_source, &result, hierarchical);
    check_result(query, hierarchical, returned, &result, expected, expected_count);
}

static void save_probe_counts(unsigned long saved[MAX_FILES]) {
    for (int i = 0; i < MAX_FILES; ++i) saved[i] = fixture.files[i].probes;
}

static void expect_no_probes(unsigned long total, const unsigned long saved[MAX_FILES]) {
    EXPECT(probe_calls == total, "visits reprobed sources: %lu calls became %lu", total, probe_calls);
    for (int i = 0; i < MAX_FILES; ++i) {
        EXPECT(fixture.files[i].probes == saved[i], "visit reprobed fixture ID %d", i);
    }
}

static void expect_create_probes(const unsigned long saved[MAX_FILES], int project_a) {
    for (int i = 0; i < MAX_FILES; ++i) {
        unsigned expected = 0;
        if (i < F_SOURCE_COUNT)
            expected = project_a ? source_specs[i].visible_a : source_specs[i].visible_b;
        else if (i == F_ADDED && fixture.files[i].path && project_a)
            expected = 1;
        EXPECT(fixture.files[i].probes == saved[i] + expected,
               "create project %c: fixture ID %d ('%s') had %lu new probes, expected %u",
               project_a ? 'A' : 'B', i,
               fixture.files[i].path ? fixture.files[i].path : "(not created)",
               fixture.files[i].probes - saved[i], expected);
    }
}

static const int tree_exact[] = {
    F_A_TREE_FIRST, F_A_TREE_IN_DIR, F_A_TREE_IN_BRANCH, F_A_TREE_LAST,
    F_SECOND_TREE, F_HELPER_TREE, F_CACHE_TREE, F_GLOBAL_TREE
};
static const int tree_hierarchical[] = {
    F_A_TREE_FIRST, F_A_GRAND, F_A_CHILD, F_A_TREE_IN_DIR,
    F_A_TREE_IN_BRANCH, F_A_CHILD_IN_ROOT, F_A_TREE_LAST,
    F_SECOND_CHILD, F_SECOND_TREE, F_HELPER_TREE,
    F_CACHE_CHILD, F_CACHE_TREE, F_GLOBAL_TREE
};
static const int child_exact[] = {
    F_A_CHILD, F_A_CHILD_IN_ROOT, F_SECOND_CHILD,
    F_FAILED_CHILD, F_HELPER_CHILD, F_CACHE_CHILD, F_GLOBAL_CHILD
};
static const int child_hierarchical[] = {
    F_A_GRAND, F_A_CHILD, F_A_CHILD_IN_ROOT, F_SECOND_CHILD,
    F_FAILED_CHILD, F_HELPER_CHILD, F_CACHE_CHILD, F_GLOBAL_CHILD
};
static const int grand_exact[] = { F_A_GRAND, F_HELPER_GRAND, F_GLOBAL_GRAND };
static const int other_exact[] = { F_A_OTHER_FIRST, F_A_OTHER_IN_DIR };
static const int layout_exact[] = { F_FLAT_LAYOUT, F_SRC_LAYOUT, F_STDLIB_LAYOUT };
static const int layout_helpers[] = { F_FLAT_HELPER, F_SRC_HELPER, F_STDLIB_HELPER };

#define QUERY(index, name, hierarchy, expected) \
    expect_query((index), (name), (hierarchy), (expected), ARRAY_COUNT(expected))

static void test_namespace_matching(const zan_pkg_source_index_t *index) {
    unsigned long saved[MAX_FILES];
    save_probe_counts(saved);
    unsigned long total = probe_calls;

    QUERY(index, NS ".Tree", 0, tree_exact);
    QUERY(index, NS ".Tree", 1, tree_hierarchical);
    QUERY(index, NS "/Tree", 0, tree_exact);
    QUERY(index, NS "\\Tree", 1, tree_hierarchical);
    QUERY(index, NS "/Tree\\Child", 0, child_exact);
    QUERY(index, NS ".Tree.Child", 1, child_hierarchical);
    QUERY(index, NS ".Tree.Child.Grand", 0, grand_exact);
    QUERY(index, NS ".Tree.Child.Grand", 1, grand_exact);
    QUERY(index, NS ".Other", 0, other_exact);
    QUERY(index, NS ".Other", 1, other_exact);
    QUERY(index, NS ".Layout", 0, layout_exact);
    QUERY(index, NS ".Layout", 1, layout_exact);
    QUERY(index, NS "/Layout/Child", 0, layout_helpers);
    QUERY(index, NS "/Layout/Child", 1, layout_helpers);

    const int boundary[] = { F_A_BOUNDARY };
    const int moved[] = { F_MOVED };
    QUERY(index, NS ".Treeish", 1, boundary);
    QUERY(index, NS ".Elsewhere", 0, moved);
    expect_query(index, NS ".FalseFolder", 0, NULL, 0);
    expect_query(index, NS ".FalseFolder", 1, NULL, 0);
    expect_query(index, "src/" NS "/Tree", 0, NULL, 0);
    expect_query(index, "stdlib/" NS "/Layout/Child", 0, NULL, 0);
    expect_query(index, NS ".Symlink", 1, NULL, 0);
    expect_no_probes(total, saved);
}

static void test_shadowing(const zan_pkg_source_index_t *index) {
    const int high[] = { F_PROJECT_SHADOW };
    const int cache_winner[] = { F_CACHE_SHADOW_WINNER };
    QUERY(index, NS ".Shadow.High", 0, high);
    QUERY(index, NS ".Shadow", 1, high);
    expect_query(index, NS ".Shadow.Cached", 0, NULL, 0);
    expect_query(index, NS ".Shadow.Cached", 1, NULL, 0);
    expect_query(index, NS ".Shadow.Global", 0, NULL, 0);
    expect_query(index, NS ".Shadow.Global", 1, NULL, 0);
    QUERY(index, NS ".CacheShadow.Winner", 0, cache_winner);
    QUERY(index, NS ".CacheShadow", 1, cache_winner);
    expect_query(index, NS ".CacheShadow.Hidden", 0, NULL, 0);
    expect_query(index, NS ".CacheShadow.Hidden", 1, NULL, 0);
}

static void test_query_guards(const zan_pkg_source_index_t *index) {
    static const char *const unsafe_queries[] = {
        ".", "..", "../" NS "/Tree", NS "/../Tree", NS "\\..\\Tree",
        "/" NS "/Tree", "\\" NS "\\Tree", "C:/" NS "/Tree",
        NS "/Tree/../../Escape"
    };
    unsigned long saved[MAX_FILES];
    save_probe_counts(saved);
    unsigned long total = probe_calls;
    expect_query(NULL, NS ".Tree", 0, NULL, 0);
    expect_query(index, NULL, 0, NULL, 0);
    expect_query(index, NULL, 1, NULL, 0);
    expect_query(index, "", 0, NULL, 0);
    expect_query(index, "", 1, NULL, 0);
    EXPECT(zan_pkg_source_index_visit(index, NS ".Tree", NULL, NULL, 0) == 0,
           "visit without a visitor must return zero");
    EXPECT(zan_pkg_source_index_visit(index, NS ".Tree", NULL, NULL, 1) == 0,
           "hierarchical visit without a visitor must return zero");
    for (size_t i = 0; i < ARRAY_COUNT(unsafe_queries); ++i) {
        expect_query(index, unsafe_queries[i], 0, NULL, 0);
        expect_query(index, unsafe_queries[i], 1, NULL, 0);
    }

    char long_namespace[8192];
    size_t prefix = sizeof(NS ".") - 1;
    memcpy(long_namespace, NS ".", prefix);
    memset(long_namespace + prefix, 'N', sizeof(long_namespace) - prefix - 1);
    long_namespace[sizeof(long_namespace) - 1] = '\0';
    expect_query(index, long_namespace, 0, NULL, 0);
    expect_query(index, long_namespace, 1, NULL, 0);
    expect_no_probes(total, saved);
    zan_pkg_source_index_destroy(NULL);
}

static void test_many_queries(const zan_pkg_source_index_t *index) {
    static const char *const queries[] = {
        NS ".Tree", NS "/Tree", NS "\\Tree", NS ".Tree"
    };
    unsigned long saved[MAX_FILES];
    save_probe_counts(saved);
    unsigned long total = probe_calls;
    for (unsigned i = 0; i < MANY_QUERIES; ++i) {
        int hierarchical = (int)(i & 1);
        const int *expected = hierarchical ? tree_hierarchical : tree_exact;
        size_t count = hierarchical ? ARRAY_COUNT(tree_hierarchical) : ARRAY_COUNT(tree_exact);
        expect_query(index, queries[i % ARRAY_COUNT(queries)], hierarchical, expected, count);
        char missing[128];
        snprintf(missing, sizeof(missing), NS ".Missing.Query%u", i);
        expect_query(index, missing, hierarchical, NULL, 0);
    }
    expect_no_probes(total, saved);
}

static void test_absent_project(void) {
    const char *const projects[] = { NULL, "" };
    unsigned long saved[MAX_FILES];
    save_probe_counts(saved);
    unsigned long total = probe_calls;
    for (size_t i = 0; i < ARRAY_COUNT(projects); ++i) {
        zan_pkg_source_index_t *index = zan_pkg_source_index_create(projects[i], namespace_probe);
        EXPECT(index == NULL, "packages must be disabled with %s project",
               projects[i] ? "empty" : "null");
        expect_query(index, NS ".Tree", 0, NULL, 0);
        expect_query(index, NS ".Tree", 1, NULL, 0);
        expect_legacy(projects[i], NS ".Tree", 1, NULL, 0);
        zan_pkg_source_index_destroy(index);
    }
    EXPECT(zan_pkg_source_index_create(fixture.project_a, NULL) == NULL,
           "create without a probe must return null");
    expect_no_probes(total, saved);
}

static void test_independent_indexes(const zan_pkg_source_index_t *a,
                                     const zan_pkg_source_index_t *b) {
    const int isolation_a[] = { F_A_ISOLATION };
    const int isolation_b[] = { F_B_ISOLATION };
    const int global_tree[] = { F_GLOBAL_TREE };
    const int global_shadow[] = { F_GLOBAL_SHADOW };
    const int global_cache_shadow[] = { F_GLOBAL_CACHE_SHADOW };
    unsigned long saved[MAX_FILES];
    save_probe_counts(saved);
    unsigned long total = probe_calls;
    for (unsigned i = 0; i < 8; ++i) {
        QUERY(a, NS ".Isolation", 0, isolation_a);
        QUERY(b, NS ".Isolation", 0, isolation_b);
        QUERY(a, NS ".Tree", 1, tree_hierarchical);
        QUERY(b, NS ".Tree", 1, global_tree);
    }
    expect_query(a, NS ".Shadow.Global", 1, NULL, 0);
    QUERY(b, NS ".Shadow.Global", 1, global_shadow);
    expect_query(a, NS ".CacheShadow.Hidden", 1, NULL, 0);
    QUERY(b, NS ".CacheShadow.Hidden", 1, global_cache_shadow);
    expect_no_probes(total, saved);
}

static int modify_fixture_sources(void) {
    const char *path = fixture.files[F_A_TREE_LAST].path;
    FILE *stream = fopen(path, "wb");
    EXPECT(stream != NULL, "rewrite owned fixture source: %s", path);
    if (!stream) return 0;
    int wrote = fputs(DECLARE("Changed"), stream) != EOF;
    int closed = fclose(stream) == 0;
    EXPECT(wrote && closed, "rewrite owned fixture namespace: %s", path);
    if (!wrote || !closed) return 0;
    return create_source(F_ADDED, STORE_A_PACKAGES,
                         NS ".AOrder/src/0-added.zan", DECLARE("Tree"));
}

static const int refreshed_tree_exact[] = {
    F_ADDED, F_A_TREE_FIRST, F_A_TREE_IN_DIR, F_A_TREE_IN_BRANCH,
    F_SECOND_TREE, F_HELPER_TREE, F_CACHE_TREE, F_GLOBAL_TREE
};
static const int refreshed_tree_hierarchical[] = {
    F_ADDED, F_A_TREE_FIRST, F_A_GRAND, F_A_CHILD, F_A_TREE_IN_DIR,
    F_A_TREE_IN_BRANCH, F_A_CHILD_IN_ROOT, F_SECOND_CHILD, F_SECOND_TREE,
    F_HELPER_TREE, F_CACHE_CHILD, F_CACHE_TREE, F_GLOBAL_TREE
};

static void test_old_snapshot(const zan_pkg_source_index_t *a,
                              const zan_pkg_source_index_t *b) {
    unsigned long saved[MAX_FILES];
    save_probe_counts(saved);
    unsigned long total = probe_calls;
    QUERY(a, NS ".Tree", 0, tree_exact);
    QUERY(a, NS ".Tree", 1, tree_hierarchical);
    expect_query(a, NS ".Changed", 0, NULL, 0);
    const int isolation_b[] = { F_B_ISOLATION };
    const int global_tree[] = { F_GLOBAL_TREE };
    QUERY(b, NS ".Isolation", 0, isolation_b);
    QUERY(b, NS ".Tree", 1, global_tree);
    expect_query(b, NS ".Changed", 1, NULL, 0);
    expect_no_probes(total, saved);
}

static void test_refreshed_snapshot(const zan_pkg_source_index_t *index) {
    const int changed[] = { F_A_TREE_LAST };
    unsigned long saved[MAX_FILES];
    save_probe_counts(saved);
    unsigned long total = probe_calls;
    QUERY(index, NS ".Tree", 0, refreshed_tree_exact);
    QUERY(index, NS ".Tree", 1, refreshed_tree_hierarchical);
    QUERY(index, NS ".Changed", 0, changed);
    QUERY(index, NS "/Changed", 1, changed);
    QUERY(index, NS ".Tree.Child", 1, child_hierarchical);
    expect_no_probes(total, saved);
}

static void test_legacy_compatibility(void) {
    unsigned long saved[MAX_FILES];
    save_probe_counts(saved);
    expect_legacy(fixture.project_a, NS ".Tree", 0,
                  refreshed_tree_exact, ARRAY_COUNT(refreshed_tree_exact));
    /* 模块核心语义抽象与接口调用契约 */
    expect_create_probes(saved, 1);
    expect_legacy(fixture.project_a, NS "/Tree", 1,
                  refreshed_tree_hierarchical, ARRAY_COUNT(refreshed_tree_hierarchical));
    expect_legacy(fixture.project_a, NS "\\Tree\\Child", 0,
                  child_exact, ARRAY_COUNT(child_exact));
    expect_legacy(fixture.project_a, NS ".Missing.Legacy", 1, NULL, 0);
    expect_legacy(NULL, NS ".Tree", 1, NULL, 0);
    save_probe_counts(saved);
    unsigned long total = probe_calls;
    EXPECT(zan_pkg_visit_namespace(fixture.project_a, NS ".Tree", namespace_probe,
                                   NULL, NULL, 0) == 0,
           "legacy visit without a visitor must return zero");
    expect_legacy(fixture.project_a, "../" NS "/Tree", 1, NULL, 0);
    char long_namespace[512];
    memset(long_namespace, 'N', sizeof(long_namespace) - 1);
    long_namespace[sizeof(long_namespace) - 1] = '\0';
    expect_legacy(fixture.project_a, long_namespace, 1, NULL, 0);
    expect_no_probes(total, saved);
}

int main(int argc, char **argv) {
    if (argc != 2 || !argv[1][0]) {
        fprintf(stderr, "Usage: %s <existing scratch parent directory>\n", argv[0]);
        return 2;
    }

    zan_pkg_source_index_t *index_a = NULL;
    zan_pkg_source_index_t *index_b = NULL;
    unsigned long saved[MAX_FILES];
    if (!prepare_fixture(argv[1]) || !prepare_symlinks()) goto cleanup;

    save_probe_counts(saved);
    index_a = zan_pkg_source_index_create(fixture.project_a, namespace_probe);
    EXPECT(index_a != NULL, "create project A snapshot");
    if (!index_a) goto cleanup;
    expect_create_probes(saved, 1);
    test_namespace_matching(index_a);
    test_shadowing(index_a);
    test_query_guards(index_a);
    test_many_queries(index_a);

    save_probe_counts(saved);
    index_b = zan_pkg_source_index_create(fixture.project_b, namespace_probe);
    EXPECT(index_b != NULL, "create project B snapshot while A is alive");
    if (!index_b) goto cleanup;
    expect_create_probes(saved, 0);
    test_independent_indexes(index_a, index_b);
    test_absent_project();

    if (!modify_fixture_sources()) goto cleanup;
    test_old_snapshot(index_a, index_b);
    zan_pkg_source_index_destroy(index_a);
    index_a = NULL;

    save_probe_counts(saved);
    index_a = zan_pkg_source_index_create(fixture.project_a, namespace_probe);
    EXPECT(index_a != NULL, "recreate project A snapshot after source changes");
    if (!index_a) goto cleanup;
    expect_create_probes(saved, 1);
    test_refreshed_snapshot(index_a);
    test_legacy_compatibility();

cleanup:
    zan_pkg_source_index_destroy(index_b);
    zan_pkg_source_index_destroy(index_a);
    cleanup_fixture();
    if (failures) {
        fprintf(stderr, "%d package namespace test(s) failed.\n", failures);
        return 1;
    }
    printf("All package namespace tests passed.\n");
    return 0;
}
