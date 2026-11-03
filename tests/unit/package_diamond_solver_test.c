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

static void test_semver_range_conversion(void) {
    zan_dependency_t dep;
    zan_version_range_t range;

    /* 1. ^1.2.3 -> [1.2.3, 2.0.0) */
    memset(&dep, 0, sizeof(dep));
    dep.kind = ZAN_DEP_COMPAT;
    zan_version_parse("1.2.3", &dep.min_ver);
    zan_dep_to_range(&dep, &range);

    EXPECT(range.min.major == 1 && range.min.minor == 2 && range.min.patch == 3, "min version mismatch");
    EXPECT(range.min_included == true, "min should be included");
    EXPECT(range.has_max == true, "should have max");
    EXPECT(range.max.major == 2 && range.max.minor == 0 && range.max.patch == 0, "max version mismatch");
    EXPECT(range.max_included == false, "max should be excluded");

    /* 2. ^0.2.3 -> [0.2.3, 0.3.0) */
    zan_version_parse("0.2.3", &dep.min_ver);
    zan_dep_to_range(&dep, &range);
    EXPECT(range.max.major == 0 && range.max.minor == 3 && range.max.patch == 0, "0.x compat max mismatch");

    /* 3. ~1.2.3 -> [1.2.3, 1.3.0) */
    dep.kind = ZAN_DEP_TILDE;
    zan_version_parse("1.2.3", &dep.min_ver);
    zan_dep_to_range(&dep, &range);
    EXPECT(range.max.major == 1 && range.max.minor == 3 && range.max.patch == 0, "tilde max mismatch");

    /* 4. 包含性判定 */
    zan_version_t v1, v2, v3;
    zan_version_parse("1.2.3", &v1);
    zan_version_parse("1.9.9", &v2);
    zan_version_parse("2.0.0", &v3);

    dep.kind = ZAN_DEP_COMPAT;
    zan_version_parse("1.2.3", &dep.min_ver);
    zan_dep_to_range(&dep, &range);

    EXPECT(zan_version_satisfies_range(&v1, &range) == true, "1.2.3 should satisfy ^1.2.3");
    EXPECT(zan_version_satisfies_range(&v2, &range) == true, "1.9.9 should satisfy ^1.2.3");
    EXPECT(zan_version_satisfies_range(&v3, &range) == false, "2.0.0 should not satisfy ^1.2.3");
}

static void test_constraint_intersection(void) {
    zan_dependency_t d1, d2;
    zan_version_range_t r1, r2, out;

    /* 1. 兼容相交：^1.0.0 ∩ ^1.2.0 -> [1.2.0, 2.0.0) */
    memset(&d1, 0, sizeof(d1));
    d1.kind = ZAN_DEP_COMPAT;
    zan_version_parse("1.0.0", &d1.min_ver);
    zan_dep_to_range(&d1, &r1);

    memset(&d2, 0, sizeof(d2));
    d2.kind = ZAN_DEP_COMPAT;
    zan_version_parse("1.2.0", &d2.min_ver);
    zan_dep_to_range(&d2, &r2);

    bool ok = zan_range_intersect(&r1, &r2, &out);
    EXPECT(ok == true, "compatible ranges should intersect");
    EXPECT(out.min.major == 1 && out.min.minor == 2 && out.min.patch == 0, "intersect min mismatch");
    EXPECT(out.max.major == 2 && out.max.minor == 0 && out.max.patch == 0, "intersect max mismatch");

    /* 2. 冲突不相交：^1.0.0 ∩ ^2.0.0 -> empty */
    zan_version_parse("2.0.0", &d2.min_ver);
    zan_dep_to_range(&d2, &r2);

    ok = zan_range_intersect(&r1, &r2, &out);
    EXPECT(ok == false, "conflicting major ranges must have empty intersection");

    /* 3. 边界不相交：<=1.2.0 ∩ >1.2.0 */
    memset(&r1, 0, sizeof(r1));
    zan_version_parse("0.0.0", &r1.min);
    r1.min_included = true;
    zan_version_parse("1.2.0", &r1.max);
    r1.max_included = true;
    r1.has_max = true;

    memset(&r2, 0, sizeof(r2));
    zan_version_parse("1.2.0", &r2.min);
    r2.min_included = false; /* > 1.2.0 */
    r2.has_max = false;

    ok = zan_range_intersect(&r1, &r2, &out);
    EXPECT(ok == false, "mutually exclusive boundary must have empty intersection");
}

static void test_diamond_dependency_solver(void) {
    /* 模拟钻石依赖拓扑：
     * Root -> PkgA (^1.0.0), PkgB (^1.0.0)
     * PkgA -> DepD (^1.0.0)
     * PkgB -> DepD (^1.3.0)
     * 期望求解结果：DepD 收敛至 ^1.3.0 ([1.3.0, 2.0.0))，并仲裁为 1.3.0
     */
    char scratch_dir[] = "test_diamond_scratch";
    mkdir(scratch_dir);
    char pkgs_dir[256];
    snprintf(pkgs_dir, sizeof(pkgs_dir), "%s/.zan-packages", scratch_dir);
    mkdir(pkgs_dir);

    /* 建立 DepD 包：版本 1.3.0 */
    char depd_dir[256], depd_pkg[256];
    snprintf(depd_dir, sizeof(depd_dir), "%s/DepD", pkgs_dir);
    mkdir(depd_dir);
    snprintf(depd_pkg, sizeof(depd_pkg), "%s/zan.pkg", depd_dir);
    FILE *f = fopen(depd_pkg, "w");
    assert(f);
    fprintf(f, "[package]\nname = \"DepD\"\nversion = \"1.3.0\"\n");
    fclose(f);

    /* 建立 PkgA 包：依赖 DepD ^1.0.0 */
    char pkga_dir[256], pkga_pkg[256];
    snprintf(pkga_dir, sizeof(pkga_dir), "%s/PkgA", pkgs_dir);
    mkdir(pkga_dir);
    snprintf(pkga_pkg, sizeof(pkga_pkg), "%s/zan.pkg", pkga_dir);
    f = fopen(pkga_pkg, "w");
    assert(f);
    fprintf(f, "[package]\nname = \"PkgA\"\nversion = \"1.0.0\"\n[dependencies]\nDepD = { version = \"^1.0.0\" }\n");
    fclose(f);

    /* 建立 PkgB 包：依赖 DepD ^1.3.0 */
    char pkgb_dir[256], pkgb_pkg[256];
    snprintf(pkgb_dir, sizeof(pkgb_dir), "%s/PkgB", pkgs_dir);
    mkdir(pkgb_dir);
    snprintf(pkgb_pkg, sizeof(pkgb_pkg), "%s/zan.pkg", pkgb_dir);
    f = fopen(pkgb_pkg, "w");
    assert(f);
    fprintf(f, "[package]\nname = \"PkgB\"\nversion = \"1.0.0\"\n[dependencies]\nDepD = { version = \"^1.3.0\" }\n");
    fclose(f);

    /* 建立 Root Package */
    zan_package_t root;
    zan_pkg_new(&root, "RootApp", "1.0.0");
    zan_pkg_add_dep(&root, "PkgA", "", "^1.0.0");
    zan_pkg_add_dep(&root, "PkgB", "", "^1.0.0");

    zan_pkg_registry_t reg;
    zan_pkg_init(&reg, scratch_dir);

    bool solve_ok = zan_pkg_resolve(&reg, &root);
    EXPECT(solve_ok == true, "diamond dependency resolution should succeed");

    /* 验证 DepD 是否成功仲裁收敛 */
    bool found_depd = false;
    for (int i = 0; i < reg.resolved_count; i++) {
        if (strcmp(reg.resolved[i]->name, "DepD") == 0) {
            found_depd = true;
            EXPECT(reg.resolved[i]->version.major == 1 &&
                   reg.resolved[i]->version.minor == 3 &&
                   reg.resolved[i]->version.patch == 0,
                   "DepD resolved version should be 1.3.0, got %d.%d.%d",
                   reg.resolved[i]->version.major, reg.resolved[i]->version.minor, reg.resolved[i]->version.patch);
            break;
        }
    }
    EXPECT(found_depd == true, "DepD should be present in resolved list");

    /* 写入 lockfile 校验 */
    EXPECT(zan_pkg_write_lock(&reg) == true, "lockfile write should succeed");

    zan_pkg_destroy(&root);
    zan_pkg_registry_destroy(&reg);
}

static void test_conflict_detection_and_derivation(void) {
    /* 模拟不可调和的冲突依赖：
     * Root -> PkgA (^1.0.0), PkgC (^1.0.0)
     * PkgA -> Incompat (^1.0.0)
     * PkgC -> Incompat (^2.0.0)
     * 期望结果：求解器必须拦截并返回 false，打印因果链解释
     */
    char scratch_dir[] = "test_conflict_scratch";
    mkdir(scratch_dir);
    char pkgs_dir[256];
    snprintf(pkgs_dir, sizeof(pkgs_dir), "%s/.zan-packages", scratch_dir);
    mkdir(pkgs_dir);

    char pkga_dir[256], pkga_pkg[256];
    snprintf(pkga_dir, sizeof(pkga_dir), "%s/PkgA", pkgs_dir);
    mkdir(pkga_dir);
    snprintf(pkga_pkg, sizeof(pkga_pkg), "%s/zan.pkg", pkga_dir);
    FILE *f = fopen(pkga_pkg, "w");
    assert(f);
    fprintf(f, "[package]\nname = \"PkgA\"\nversion = \"1.0.0\"\n[dependencies]\nIncompat = { version = \"^1.0.0\" }\n");
    fclose(f);

    char pkgc_dir[256], pkgc_pkg[256];
    snprintf(pkgc_dir, sizeof(pkgc_dir), "%s/PkgC", pkgs_dir);
    mkdir(pkgc_dir);
    snprintf(pkgc_pkg, sizeof(pkgc_pkg), "%s/zan.pkg", pkgc_dir);
    f = fopen(pkgc_pkg, "w");
    assert(f);
    fprintf(f, "[package]\nname = \"PkgC\"\nversion = \"1.0.0\"\n[dependencies]\nIncompat = { version = \"^2.0.0\" }\n");
    fclose(f);

    zan_package_t root;
    zan_pkg_new(&root, "ConflictApp", "1.0.0");
    zan_pkg_add_dep(&root, "PkgA", "", "^1.0.0");
    zan_pkg_add_dep(&root, "PkgC", "", "^1.0.0");

    zan_pkg_registry_t reg;
    zan_pkg_init(&reg, scratch_dir);

    bool solve_ok = zan_pkg_resolve(&reg, &root);
    EXPECT(solve_ok == false, "conflicting dependencies should be rejected by solver");

    zan_pkg_destroy(&root);
    zan_pkg_registry_destroy(&reg);
}

int main(void) {
    test_semver_range_conversion();
    test_constraint_intersection();
    test_diamond_dependency_solver();
    test_conflict_detection_and_derivation();

    if (failures == 0) {
        printf("All diamond dependency solver & SemVer tests passed!\n");
        return 0;
    }
    printf("%d test(s) failed.\n", failures);
    return 1;
}
