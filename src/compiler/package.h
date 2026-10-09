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

/* 核心系统底层抽象与内存语义契约 */
bool zan_version_parse(const char *str, zan_version_t *out);

/* 核心系统底层抽象与内存语义契约 */
int zan_version_compare(const zan_version_t *a, const zan_version_t *b);

/* 底层系统交互与数据协议契约 */
char *zan_version_format(const zan_version_t *v, char *buf, int buf_size);

typedef enum {
    ZAN_DEP_EXACT,    /* =1.2.3 */
    ZAN_DEP_COMPAT,   /* ^1.2.3 (>=1.2.3, <2.0.0) */
    ZAN_DEP_MINIMUM,  /* >=1.2.3 */
    ZAN_DEP_RANGE,    /* >=1.0.0 <2.0.0 */
} zan_dep_kind_t;

typedef struct {
    char name[128];         /* 核心系统底层抽象与内存语义契约 */
    char source[512];       /* 核心系统底层抽象与内存语义契约 */
    zan_dep_kind_t kind;
    zan_version_t min_ver;
    zan_version_t max_ver;  /* 核心系统底层抽象与内存语义契约 */
} zan_dependency_t;

typedef struct {
    char name[128];              /* 核心系统底层抽象与内存语义契约 */
    zan_version_t version;       /* 核心系统底层抽象与内存语义契约 */
    bool has_version;            /* 核心系统底层抽象与内存语义契约 */
    char description[256];       /* 核心系统底层抽象与内存语义契约 */
    char author[128];            /* 核心系统底层抽象与内存语义契约 */
    char license[64];            /* 核心系统底层抽象与内存语义契约 */
    char entry_point[256];       /* 核心系统底层抽象与内存语义契约 */
    zan_dependency_t *deps;      /* dependencies */
    int dep_count;
    int dep_cap;
    char **source_dirs;          /* 核心系统底层抽象与内存语义契约 */
    int source_dir_count;
    char plugin_id[64];          /* 商业插件包标识 */
} zan_package_t;

typedef struct {
    char *cache_dir;             /* .zan-packages/ */
    char *lock_file;             /* zan.lock */
    zan_package_t **resolved;    /* 核心系统底层抽象与内存语义契约 */
    int resolved_count;
} zan_pkg_registry_t;

/* 核心系统底层抽象与内存语义契约 */
void zan_pkg_init(zan_pkg_registry_t *reg, const char *project_dir);

/* 底层系统交互与数据协议契约 */
bool zan_pkg_load(zan_package_t *pkg, const char *manifest_path);

/* 编译时输出商业插件使用标识至 stderr */
void zan_pkg_note_usage(const char *store, const char *package_name);

/* 底层系统交互与数据协议契约 */
bool zan_pkg_save(const zan_package_t *pkg, const char *manifest_path);

/* 核心系统底层抽象与内存语义契约 */
void zan_pkg_new(zan_package_t *pkg, const char *name, const char *version);

/* 核心系统底层抽象与内存语义契约 */
void zan_pkg_add_dep(zan_package_t *pkg, const char *name, const char *source,
                     const char *version_constraint);

/* 核心系统底层抽象与内存语义契约 */
bool zan_pkg_remove_dep(zan_package_t *pkg, const char *name);

/* 底层系统交互与数据协议契约 */
bool zan_pkg_resolve(zan_pkg_registry_t *reg, zan_package_t *root);

/* 底层系统交互与数据协议契约 */
bool zan_pkg_fetch(zan_pkg_registry_t *reg, const zan_dependency_t *dep);

/* 核心系统底层抽象与内存语义契约 */
bool zan_pkg_version_satisfies(const zan_dependency_t *dep, const zan_version_t *ver);

/* 底层系统交互与数据协议契约 */
char **zan_pkg_get_sources(zan_pkg_registry_t *reg, int *out_count);

/* 底层系统交互与数据协议契约 */
bool zan_pkg_write_lock(zan_pkg_registry_t *reg);

/* 底层系统交互与数据协议契约 */
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
