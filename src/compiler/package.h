/* package.h -- Zan package manager.
 *
 * Simple dependency resolution with source-based packages.
 * Package manifest: zan.pkg (JSON-like format)
 * Package sources: git repositories with version tags
 * Local cache: .zan-packages/
 */

#ifndef ZAN_PACKAGE_H
#define ZAN_PACKAGE_H

#include <stdbool.h>
#include <stdint.h>

/* ---- version ---- */

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

/* ---- dependency specification ---- */

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

/* ---- package manifest (zan.pkg) ---- */

typedef struct {
    char name[128];              /* package name */
    zan_version_t version;       /* package version */
    char description[256];       /* short description */
    char author[128];            /* author name */
    char license[64];            /* license identifier */
    char entry_point[256];       /* main source file */
    zan_dependency_t *deps;      /* dependencies */
    int dep_count;
    int dep_cap;
    char **source_dirs;          /* source directories to compile */
    int source_dir_count;
} zan_package_t;

/* ---- package registry/cache ---- */

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

/* Save package manifest to zan.pkg file */
bool zan_pkg_save(const zan_package_t *pkg, const char *manifest_path);

/* Create a new empty package manifest */
void zan_pkg_new(zan_package_t *pkg, const char *name, const char *version);

/* Add a dependency to the manifest */
void zan_pkg_add_dep(zan_package_t *pkg, const char *name, const char *source,
                     const char *version_constraint);

/* Remove a dependency from the manifest */
bool zan_pkg_remove_dep(zan_package_t *pkg, const char *name);

/* ---- dependency resolution ---- */

/* Resolve all dependencies (download + version check) */
bool zan_pkg_resolve(zan_pkg_registry_t *reg, zan_package_t *root);

/* Fetch a package from its source (git clone or copy) */
bool zan_pkg_fetch(zan_pkg_registry_t *reg, const zan_dependency_t *dep);

/* Check if a version satisfies a dependency constraint */
bool zan_pkg_version_satisfies(const zan_dependency_t *dep, const zan_version_t *ver);

/* Get list of all source files from resolved packages */
char **zan_pkg_get_sources(zan_pkg_registry_t *reg, int *out_count);

/* ---- lock file ---- */

/* Write lock file with resolved versions */
bool zan_pkg_write_lock(zan_pkg_registry_t *reg);

/* Read lock file for reproducible builds */
bool zan_pkg_read_lock(zan_pkg_registry_t *reg);

/* ---- cleanup ---- */

void zan_pkg_destroy(zan_package_t *pkg);
void zan_pkg_registry_destroy(zan_pkg_registry_t *reg);

#endif /* ZAN_PACKAGE_H */
