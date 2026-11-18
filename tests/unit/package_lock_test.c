
#include "package.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#ifdef _WIN32
#include <direct.h>
#define mkdir(p) _mkdir(p)
#define rmdir _rmdir
#define unlink _unlink
#else
#include <sys/stat.h>
#include <unistd.h>
/* 调用点按 Windows 形状写单参 mkdir；POSIX 的真签名带 mode，这里对齐
 * （_GNU_SOURCE 起声明可见后，单参调用会撞 too-few-arguments 硬错误）。 */
#define mkdir(p) mkdir((p), 0755)
#endif

static int failures = 0;

#define EXPECT(cond, ...) do {                                              \
    if (!(cond)) {                                                          \
        failures++;                                                         \
        fprintf(stderr, "FAIL: ");                                          \
        fprintf(stderr, __VA_ARGS__);                                       \
        fprintf(stderr, "\n");                                              \
    }                                                                       \
} while (0)

static void test_lock_roundtrip(void) {
    mkdir("test_pkg_scratch");
    zan_pkg_registry_t reg;
    zan_pkg_init(&reg, "test_pkg_scratch");

    zan_package_t *p1 = (zan_package_t *)calloc(1, sizeof(zan_package_t));
    strncpy(p1->name, "Test.PkgA", sizeof(p1->name) - 1);
    zan_version_parse("1.2.3", &p1->version);
    p1->has_version = true;
    reg.resolved[reg.resolved_count++] = p1;

    zan_package_t *p2 = (zan_package_t *)calloc(1, sizeof(zan_package_t));
    strncpy(p2->name, "Test.PkgB", sizeof(p2->name) - 1);
    zan_version_parse("2.0.0-beta.1", &p2->version);
    p2->has_version = true;
    reg.resolved[reg.resolved_count++] = p2;

    EXPECT(zan_pkg_write_lock(&reg), "write lock should succeed");

    zan_pkg_registry_t reg2;
    zan_pkg_init(&reg2, "test_pkg_scratch");
    EXPECT(zan_pkg_read_lock(&reg2), "read lock should succeed");
    EXPECT(reg2.resolved_count == 2, "read lock should recover 2 packages, got %d", reg2.resolved_count);

    if (reg2.resolved_count == 2) {
        EXPECT(strcmp(reg2.resolved[0]->name, "Test.PkgA") == 0, "pkg 0 name mismatch");
        EXPECT(reg2.resolved[0]->version.major == 1 && reg2.resolved[0]->version.minor == 2 && reg2.resolved[0]->version.patch == 3, "pkg 0 version mismatch");
        EXPECT(strcmp(reg2.resolved[1]->name, "Test.PkgB") == 0, "pkg 1 name mismatch");
        EXPECT(reg2.resolved[1]->version.major == 2 && strcmp(reg2.resolved[1]->version.prerelease, "beta.1") == 0, "pkg 1 prerelease mismatch");
    }

    zan_pkg_registry_destroy(&reg);
    zan_pkg_registry_destroy(&reg2);
}

static void test_corrupt_lock_rejected(void) {
    mkdir("test_pkg_scratch");
    FILE *f = fopen("test_pkg_scratch/zan.lock", "w");
    assert(f);
    fprintf(f, "[[package]]\nname = \"Bad.Pkg\"\nversion = \"not_a_version\"\n");
    fclose(f);

    zan_pkg_registry_t reg;
    zan_pkg_init(&reg, "test_pkg_scratch");
    EXPECT(!zan_pkg_read_lock(&reg), "corrupted version should fail read_lock");
    zan_pkg_registry_destroy(&reg);
}

static void test_unsafe_name_rejected(void) {
    mkdir("test_pkg_scratch");
    FILE *f = fopen("test_pkg_scratch/zan.lock", "w");
    assert(f);
    fprintf(f, "[[package]]\nname = \"../../escape\"\nversion = \"1.0.0\"\n");
    fclose(f);

    zan_pkg_registry_t reg;
    zan_pkg_init(&reg, "test_pkg_scratch");
    EXPECT(!zan_pkg_read_lock(&reg), "unsafe package name should fail read_lock");
    zan_pkg_registry_destroy(&reg);
}

static void write_fixture(const char *path, const char *text) {
    FILE *f = fopen(path, "wb");
    if (!f) {
        perror(path);
        exit(1);
    }
    fputs(text, f);
    if (fclose(f) != 0) {
        perror(path);
        exit(1);
    }
}

static int probe_fixture(const char *path, char *ns, size_t cap) {
    (void)path;
    snprintf(ns, cap, "LockFixture");
    return 1;
}

static void expect_restored_source(const char *path, void *count) {
    EXPECT(strstr(path, ".zan-packages") != NULL, "unexpected source %s", path);
    (*(int *)count)++;
}

static void test_locked_project_discovery(void) {
    const char *project = "test_pkg_scratch/locked";
    const char *manifest = "name = \"LockFixture\"\nversion = \"1.0.0\"\n";
    const char *source = "test_pkg_scratch/locked/.zan-packages/LockFixture/Locked.zan";
    const char *lock = "test_pkg_scratch/locked/zan.lock";
    const char *restored = "test_pkg_scratch/locked/.zan-packages/LockFixture";
    const char *ambient = "test_pkg_scratch/locked/packages/LockFixture/Ambient.zan";
    const char *extra = "test_pkg_scratch/locked/.zan-packages/Unlisted/Other.zan";
    mkdir(project);
    mkdir("test_pkg_scratch/locked/packages");
    mkdir("test_pkg_scratch/locked/packages/LockFixture");
    mkdir("test_pkg_scratch/locked/.zan-packages");
    mkdir(restored);
    mkdir("test_pkg_scratch/locked/.zan-packages/Unlisted");
    write_fixture("test_pkg_scratch/locked/packages/LockFixture/zan.pkg", manifest);
    write_fixture("test_pkg_scratch/locked/.zan-packages/LockFixture/zan.pkg", manifest);
    write_fixture(ambient, "namespace LockFixture;\n");
    write_fixture(extra, "namespace LockFixture;\n");
    write_fixture(source, "namespace LockFixture;\n");

    char checksum[65] = {0}, text[256], dirs[8][1024];
    EXPECT(zan_pkg_compute_tree_checksum(restored, checksum), "hash restored package");
    snprintf(text, sizeof(text), "[[package]]\nname = \"LockFixture\"\nversion = \"1.0.0\"\nchecksum = \"%s\"\n", checksum);
    write_fixture(lock, text);
    EXPECT(zan_pkg_prepare_project(project), "valid restored lock should prepare");
    EXPECT(zan_pkg_all_source_roots(project, dirs, 8) == 1 && strstr(dirs[0], ".zan-packages"),
           "only the restored source root should be visible");
    EXPECT(zan_pkg_all_manifest_dirs(project, dirs, 8) == 1 && strstr(dirs[0], ".zan-packages"),
           "only the restored package manifest should be visible");
    int count = 0;
    EXPECT(zan_pkg_visit_namespace(project, "LockFixture", probe_fixture, expect_restored_source,
                                   &count, 0) == 1 && count == 1,
           "only the restored source should be indexed");

    write_fixture(source, "namespace LockFixture; // changed\n");
    EXPECT(!zan_pkg_prepare_project(project), "a changed locked source should fail validation");
    write_fixture(lock, "[[package]]\nname = \"LockFixture\"\nversion = \"2.0.0\"\n");
    EXPECT(!zan_pkg_prepare_project(project), "restored manifest version must match the lock");
    write_fixture(lock, text);
    EXPECT(unlink("test_pkg_scratch/locked/.zan-packages/LockFixture/zan.pkg") == 0,
           "remove restored manifest");
    EXPECT(!zan_pkg_prepare_project(project), "missing restored manifest must not use loose copy");
    unlink(lock);
    EXPECT(zan_pkg_prepare_project(project) && zan_pkg_all_source_roots(project, dirs, 8) > 1,
           "loose package discovery should resume after removing lock");

    unlink(source);
    unlink(extra);
    unlink(ambient);
    unlink("test_pkg_scratch/locked/packages/LockFixture/zan.pkg");
    rmdir(restored);
    rmdir("test_pkg_scratch/locked/.zan-packages/Unlisted");
    rmdir("test_pkg_scratch/locked/.zan-packages");
    rmdir("test_pkg_scratch/locked/packages/LockFixture");
    rmdir("test_pkg_scratch/locked/packages");
    rmdir(project);
}

int main(void) {
    test_lock_roundtrip();
    test_corrupt_lock_rejected();
    test_unsafe_name_rejected();
    test_locked_project_discovery();

    if (failures == 0) {
        printf("All package lock tests passed.\n");
        return 0;
    }
    printf("%d test(s) failed.\n", failures);
    return 1;
}
