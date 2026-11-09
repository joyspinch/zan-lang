/* package.h: Zan 包管理器，支持源码包多层存储解析 */

#ifndef ZAN_PACKAGE_H
#define ZAN_PACKAGE_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

typedef struct {
    int major;
    int minor;
    int patch;
    char prerelease[32];  /* e.g. "alpha", "beta.1" */
} zan_version_t;

bool zan_version_parse(const char *str, zan_version_t *out);

int zan_version_compare(const zan_version_t *a, const zan_version_t *b);

char *zan_version_format(const zan_version_t *v, char *buf, int buf_size);

typedef enum {
    ZAN_DEP_EXACT,    /* =1.2.3 */
    ZAN_DEP_COMPAT,   /* ^1.2.3 (>=1.2.3, <2.0.0) */
    ZAN_DEP_TILDE,    /* ~1.2.3 (>=1.2.3, <1.3.0) */
    ZAN_DEP_MINIMUM,  /* >=1.2.3 */
    ZAN_DEP_MAXIMUM,  /* <=1.2.3 */
    ZAN_DEP_RANGE,    /* >=1.0.0 <2.0.0 */
    ZAN_DEP_ANY,      /* * */
} zan_dep_kind_t;

typedef struct {
    char name[128];
    char source[512];
    zan_dep_kind_t kind;
    zan_version_t min_ver;
    zan_version_t max_ver;
} zan_dependency_t;

/* 归一化语义版本区间 [min, max)，支持开闭区间与无界 */
typedef struct {
    zan_version_t min;
    bool min_included;
    zan_version_t max;
    bool max_included;
    bool has_max;
} zan_version_range_t;

/* 将 dependency 转换为归一化区间 */
void zan_dep_to_range(const zan_dependency_t *dep, zan_version_range_t *out);

/* 格式化输出版本区间至字符串（如 ">=1.2.0, <2.0.0"） */
char *zan_range_format(const zan_version_range_t *r, char *buf, int buf_size);

/* 计算两个版本区间的交集。若存在有效交集返回 true 并写入 out；若不相容返回 false */
bool zan_range_intersect(const zan_version_range_t *a, const zan_version_range_t *b,
                         zan_version_range_t *out);

/* 判定版本是否落在区间内 */
bool zan_version_satisfies_range(const zan_version_t *v, const zan_version_range_t *r);

typedef struct {
    char mount[64];
    char rel_path[256];
    char sym_prefix[64];
    char filter_key[64];
    char gate_key[64];
    char baseline[128];
} zan_pkg_embed_t;

typedef struct {
    char name[128];
    char source[512];
    char checksum[65];
    zan_version_t version;
    bool has_version;
    char description[256];
    char author[128];
    char license[64];
    char entry_point[256];
    char subsystem[32];          /* e.g. "windows", "console" */
    char subsystem_sym[64];      /* 触发子系统的符号前缀，如 "Gui_" */
    char subsystem_driver[64];   /* 触发子系统使用的驱动名，如 "zan_gui" */
    zan_dependency_t *deps;      /* dependencies */
    int dep_count;
    int dep_cap;
    char **source_dirs;
    int source_dir_count;
    char plugin_id[64];          /* 商业插件包标识 */
    zan_pkg_embed_t *embeds;     /* [embed] 声明资源清单 */
    int embed_count;
    int embed_cap;
} zan_package_t;

typedef struct {
    char *cache_dir;             /* .zan-packages/ */
    char *lock_file;             /* zan.lock */
    zan_package_t **resolved;
    int resolved_count;
} zan_pkg_registry_t;

void zan_pkg_init(zan_pkg_registry_t *reg, const char *project_dir);

bool zan_pkg_load(zan_package_t *pkg, const char *manifest_path);

/* 编译时输出商业插件使用标识至 stderr */
void zan_pkg_note_usage(const char *store, const char *package_name);

bool zan_pkg_save(const zan_package_t *pkg, const char *manifest_path);

void zan_pkg_new(zan_package_t *pkg, const char *name, const char *version);

void zan_pkg_add_dep(zan_package_t *pkg, const char *name, const char *source,
                     const char *version_constraint);

bool zan_pkg_remove_dep(zan_package_t *pkg, const char *name);

bool zan_pkg_resolve(zan_pkg_registry_t *reg, zan_package_t *root);

bool zan_pkg_fetch(zan_pkg_registry_t *reg, const zan_dependency_t *dep);

bool zan_pkg_version_satisfies(const zan_dependency_t *dep, const zan_version_t *ver);

char **zan_pkg_get_sources(zan_pkg_registry_t *reg, int *out_count);

bool zan_pkg_write_lock(zan_pkg_registry_t *reg);

bool zan_pkg_read_lock(zan_pkg_registry_t *reg);

/* 计算文件 SHA-256 校验和（小写十六进制，共 64 字符 + '\\0'） */
bool zan_pkg_compute_checksum(const char *file_path, char out_hex[65]);
bool zan_pkg_compute_tree_checksum(const char *dir, char out_hex[65]);

typedef enum {
    ZAN_PKG_SCOPE_PROJECT,
    ZAN_PKG_SCOPE_GLOBAL
} zan_pkg_scope_t;

/* 解析平台全局包存储路径 (Windows: LOCALAPPDATA, POSIX: XDG/HOME) */
bool zan_pkg_global_store(char *out, size_t out_size);

/* 查找包含指定命名空间路径的已安装包目录，项目包优先于全局包 */
int zan_pkg_find_namespace(const char *project_dir, const char *namespace_path,
                           char (*out_dirs)[1024], int max_dirs);

/* 遍历所有包存储区内可见的包源码根目录 */
int zan_pkg_all_source_roots(const char *project_dir,
                             char (*out_roots)[1024], int max_roots);

/* 遍历所有包存储区内可见的包根目录（即含有 zan.pkg 的目录） */
int zan_pkg_all_manifest_dirs(const char *project_dir,
                              char (*out_dirs)[1024], int max_dirs);

/* 按声明的命名空间访问已安装包文件（忽略物理目录结构） */
typedef void (*zan_pkg_source_visitor_t)(const char *path, void *context);
typedef int (*zan_pkg_namespace_probe_t)(const char *path, char *out_ns, size_t cap);

/* 编译期包快照：一次性扫描存储区并缓存，保证后续查询 O(1) */
typedef struct zan_pkg_source_index zan_pkg_source_index_t;
zan_pkg_source_index_t *zan_pkg_source_index_create(
    const char *project_dir, zan_pkg_namespace_probe_t probe);
int zan_pkg_source_index_visit(const zan_pkg_source_index_t *index,
                                const char *namespace_path,
                                zan_pkg_source_visitor_t visitor, void *context,
                                int hierarchical);
void zan_pkg_source_index_destroy(zan_pkg_source_index_t *index);

int zan_pkg_visit_namespace(const char *project_dir, const char *namespace_path,
                            zan_pkg_namespace_probe_t probe,
                            zan_pkg_source_visitor_t visitor, void *context,
                            int hierarchical);

/* 本地安全目录安装基础原语：校验包内 zan.pkg 清单文件 */
bool zan_pkg_install_local(const char *source_dir, const char *package_name,
                           zan_pkg_scope_t scope, const char *project_dir,
                           char *status, size_t status_size);

/* 校验插件市场传输协议与配置 */
bool zan_pkg_api_validate(const char *api_url, char *status, size_t status_size);

void zan_pkg_destroy(zan_package_t *pkg);
void zan_pkg_registry_destroy(zan_pkg_registry_t *reg);

#endif /* ZAN_PACKAGE_H */
