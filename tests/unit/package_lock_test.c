/* 底层系统交互与数据协议契约 */

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
#include <unistd.h>
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

    /* 核心系统底层抽象与内存语义契约 */
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

    /* 核心系统底层抽象与内存语义契约 */
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
    /* 底层系统交互与数据协议契约 */
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
    /* 底层系统交互与数据协议契约 */
    FILE *f = fopen("test_pkg_scratch/zan.lock", "w");
    assert(f);
    fprintf(f, "[[package]]\nname = \"../../escape\"\nversion = \"1.0.0\"\n");
    fclose(f);

    zan_pkg_registry_t reg;
    zan_pkg_init(&reg, "test_pkg_scratch");
    EXPECT(!zan_pkg_read_lock(&reg), "unsafe package name should fail read_lock");
    zan_pkg_registry_destroy(&reg);
}

int main(void) {
    test_lock_roundtrip();
    test_corrupt_lock_rejected();
    test_unsafe_name_rejected();

    if (failures == 0) {
        printf("All package lock tests passed.\n");
        return 0;
    }
    printf("%d test(s) failed.\n", failures);
    return 1;
}
