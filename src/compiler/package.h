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

/* Parse "1.2.3" or "1.2.3-beta" into version struct */
bool zan_version_parse(const char *str, zan_version_t *out);

/* Compare two versions: <0, 0, >0 */
int zan_version_compare(const zan_version_t *a, const zan_version_t *b);

/* Format version to string (writes to buf, returns buf) */
char *zan_version_format(const zan_version_t *v, char *buf, int buf_size);

typedef enum {
    ZAN_DEP_EXACT,    /* =1.2.3 */
    ZAN_DEP_COMPAT,   /* ^1.2.3 (>=1.2.3, <2.0.0) */
    ZAN_DEP_MINIMUM,  /* >=1.2.3 */
    ZAN_DEP_RANGE,    /* >=1.0.0 <2.0.0 */
} zan_dep_kind_t;

typedef struct {
    char name[128];         /* package name */
    char source[512];       /* git URL or local path */
    zan_dep_kind_t kind;
    zan_version_t min_ver;
    zan_version_t max_ver;  /* for range constraints */
} zan_dependency_t;

typedef struct {
    char name[128];              /* package name */
    zan_version_t version;       /* package version */
    bool has_version;            /* manifest declared a parseable version */
    char description[256];       /* short description */
    char author[128];            /* author name */
    char license[64];            /* license identifier */
    char entry_point[256];       /* main source file */
    zan_dependency_t *deps;      /* dependencies */
    int dep_count;
    int dep_cap;
    char **source_dirs;          /* source directories to compile */
    int source_dir_count;
    char plugin_id[64];          /* 商业插件包标识 */
} zan_package_t;

typedef struct {
    char *cache_dir;             /* .zan-packages/ */
    char *lock_file;             /* zan.lock */
    zan_package_t **resolved;    /* resolved dependency tree */
    int resolved_count;
} zan_pkg_registry_t;

/* Initialize package system for a project */
void zan_pkg_init(zan_pkg_registry_t *reg, const char *project_dir);

/* Load package manifest from zan.pkg file */
bool zan_pkg_load(zan_package_t *pkg, const char *manifest_path);

/* 编译时输出商业插件使用标识至 stderr */
void zan_pkg_note_usage(const char *store, const char *package_name);

/* Save package manifest to zan.pkg file */
bool zan_pkg_save(const zan_package_t *pkg, const char *manifest_path);

/* Create a new empty package manifest */
void zan_pkg_new(zan_package_t *pkg, const char *name, const char *version);

/* Add a dependency to the manifest */
void zan_pkg_add_dep(zan_package_t *pkg, const char *name, const char *source,
                     const char *version_constraint);

/* Remove a dependency from the manifest */
bool zan_pkg_remove_dep(zan_package_t *pkg, const char *name);

/* Resolve all dependencies (download + version check) */
bool zan_pkg_resolve(zan_pkg_registry_t *reg, zan_package_t *root);

/* Fetch a package from its source (git clone or copy) */
bool zan_pkg_fetch(zan_pkg_registry_t *reg, const zan_dependency_t *dep);

/* Check if a version satisfies a dependency constraint */
bool zan_pkg_version_satisfies(const zan_dependency_t *dep, const zan_version_t *ver);

/* Get list of all source files from resolved packages */
char **zan_pkg_get_sources(zan_pkg_registry_t *reg, int *out_count);

/* Write lock file with resolved versions */
bool zan_pkg_write_lock(zan_pkg_registry_t *reg);

/* Read lock file for reproducible builds */
bool zan_pkg_read_lock(zan_pkg_registry_t *reg);

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
