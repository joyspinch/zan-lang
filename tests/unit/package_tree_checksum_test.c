#include "package.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#define mkdir(p) _mkdir(p)
#else
#include <sys/stat.h>
#include <unistd.h>
#define mkdir(p) mkdir((p), 0755)
#endif

/* B-ID90: 包校验和必须覆盖整个源码树，不能只算 manifest——
 * 篡改任何源文件都要翻车，目录遍历必须确定性（同树同哈希）。 */
static int failures = 0;

#define EXPECT(cond, ...) do {                                              \
    if (!(cond)) {                                                          \
        failures++;                                                         \
        fprintf(stderr, "FAIL: ");                                          \
        fprintf(stderr, __VA_ARGS__);                                       \
        fprintf(stderr, "\n");                                              \
    }                                                                       \
} while (0)

static void write_file(const char *path, const char *content) {
    FILE *f = fopen(path, "wb");
    if (!f) { fprintf(stderr, "cannot write %s\n", path); exit(1); }
    fputs(content, f);
    fclose(f);
}

static void mkdir_p2(const char *base, const char *name) {
    char p[512];
    snprintf(p, sizeof p, "%s/%s", base, name);
    mkdir(p);
}

int main(void) {
    char dir[512];
    snprintf(dir, sizeof dir, "tree_ck_smoke");
#ifdef _WIN32
    mkdir(dir);
#else
    mkdir(dir, 0755);
#endif
    mkdir_p2(dir, "src");
    write_file("tree_ck_smoke/zan.pkg", "name = \"smoke\"\n");
    write_file("tree_ck_smoke/src/a.zan", "int a() { return 1; }\n");
    write_file("tree_ck_smoke/src/b.zan", "int b() { return 2; }\n");

    char h1[65], h2[65];
    EXPECT(zan_pkg_compute_tree_checksum(dir, h1), "tree checksum computes");
    EXPECT(zan_pkg_compute_tree_checksum(dir, h2), "recompute succeeds");
    EXPECT(strcmp(h1, h2) == 0, "same tree must hash identically");

    /* 篡改一个源文件：哈希必须变（manifest 未动，旧算法抓不到） */
    write_file("tree_ck_smoke/src/b.zan", "int b() { return 999; }\n");
    EXPECT(zan_pkg_compute_tree_checksum(dir, h2), "recompute after tamper");
    EXPECT(strcmp(h1, h2) != 0, "tampered source must change hash");

    /* 恢复内容，哈希必须复原（确定性，不受时间戳影响） */
    write_file("tree_ck_smoke/src/b.zan", "int b() { return 2; }\n");
    EXPECT(zan_pkg_compute_tree_checksum(dir, h2), "recompute after restore");
    EXPECT(strcmp(h1, h2) == 0, "restored tree must hash like original");

    /* 对调两个文件内容：集合相同但路径->内容映射变了，哈希必须变 */
    write_file("tree_ck_smoke/src/a.zan", "int b() { return 2; }\n");
    write_file("tree_ck_smoke/src/b.zan", "int a() { return 1; }\n");
    EXPECT(zan_pkg_compute_tree_checksum(dir, h2), "recompute after swap");
    EXPECT(strcmp(h1, h2) != 0, "path-content swap must change hash");

    /* 新增文件必须变哈希 */
    write_file("tree_ck_smoke/src/a.zan", "int a() { return 1; }\n");
    write_file("tree_ck_smoke/src/b.zan", "int b() { return 2; }\n");
    write_file("tree_ck_smoke/src/c.zan", "int c() { return 3; }\n");
    EXPECT(zan_pkg_compute_tree_checksum(dir, h2), "recompute after add");
    EXPECT(strcmp(h1, h2) != 0, "added file must change hash");

    /* .git 目录不入账：clone 元数据在包间本就可能不同（基线=加了 c.zan 之后） */
    char h3[65];
    memcpy(h3, h2, sizeof h3);
    mkdir_p2(dir, ".git");
    write_file("tree_ck_smoke/.git/config", "random clone metadata\n");
    EXPECT(zan_pkg_compute_tree_checksum(dir, h2), "recompute with .git");
    EXPECT(strcmp(h3, h2) == 0, ".git metadata must not affect hash");

    /* 不存在的目录必须失败，不得静默给常量哈希 */
    EXPECT(!zan_pkg_compute_tree_checksum("tree_ck_no_such_dir", h2),
           "missing dir must fail");

    /* 清理 */
    unlink("tree_ck_smoke/.git/config");
#ifdef _WIN32
    _rmdir("tree_ck_smoke/.git");
#else
    rmdir("tree_ck_smoke/.git");
#endif
    unlink("tree_ck_smoke/zan.pkg");
    unlink("tree_ck_smoke/src/a.zan");
    unlink("tree_ck_smoke/src/b.zan");
    unlink("tree_ck_smoke/src/c.zan");
#ifdef _WIN32
    _rmdir("tree_ck_smoke/src");
    _rmdir("tree_ck_smoke");
#else
    rmdir("tree_ck_smoke/src");
    rmdir("tree_ck_smoke");
#endif

    if (failures) {
        fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    printf("ok: package tree checksum\n");
    return 0;
}
