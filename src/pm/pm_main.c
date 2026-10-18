/* pm_main.c: zan-pm 独立包管理器命令行工具 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "../compiler/package.h"
#include "../compiler/arena.h"
#include "../common/json.h"
#include "zan_version.h"

#ifdef _WIN32
#include <direct.h>
#define mkdir(p) _mkdir(p)
#else
#include <sys/stat.h>
#endif

static void print_usage(const char *prog) {
    printf("Zan Package Manager (zan-pm) %s\n\n", ZAN_VERSION);
    printf("Usage: %s <command> [options]\n\n", prog);
    printf("Commands:\n");
    printf("  init [name] [version]    Initialize a new zan.pkg package manifest\n");
    printf("  add <name> [constraint]  Add a package dependency to zan.pkg\n");
    printf("  remove <name>            Remove a package dependency from zan.pkg\n");
    printf("  list                     List all dependencies declared in zan.pkg\n");
    printf("  lock                     Resolve dependencies and generate zan.lock\n");
    printf("  check                    Verify package manifest and lockfile integrity\n\n");
    printf("Options:\n");
    printf("  --project <dir>          Target project directory (default: current directory)\n");
    printf("  --source <path|url>      Package source path or repository for 'add'\n");
    printf("  -h, --help               Display this help message\n");
    printf("  -v, --version            Display version information\n");
}

static int cmd_init(const char *project_dir, int argc, char **argv) {
    const char *name = "MyPackage";
    const char *version = "0.1.0";
    if (argc > 0 && argv[0][0] != '-') name = argv[0];
    if (argc > 1 && argv[1][0] != '-') version = argv[1];

    char manifest_path[1024];
    snprintf(manifest_path, sizeof(manifest_path), "%s/zan.pkg", project_dir);

    FILE *f = fopen(manifest_path, "rb");
    if (f) {
        fclose(f);
        fprintf(stderr, "error: '%s' already exists\n", manifest_path);
        return 1;
    }

    zan_package_t pkg;
    zan_pkg_new(&pkg, name, version);
    bool ok = zan_pkg_save(&pkg, manifest_path);
    zan_pkg_destroy(&pkg);

    if (ok) {
        printf("Created package manifest '%s' (%s v%s)\n", manifest_path, name, version);
        return 0;
    } else {
        fprintf(stderr, "error: failed to write '%s'\n", manifest_path);
        return 1;
    }
}

static int cmd_add(const char *project_dir, int argc, char **argv, const char *source_override) {
    if (argc < 1) {
        fprintf(stderr, "error: 'add' requires package name: zan-pm add <name> [constraint]\n");
        return 1;
    }
    const char *name = argv[0];
    const char *constraint = (argc > 1 && argv[1][0] != '-') ? argv[1] : "*";
    const char *source = source_override ? source_override : "";

    char manifest_path[1024];
    snprintf(manifest_path, sizeof(manifest_path), "%s/zan.pkg", project_dir);

    zan_package_t pkg;
    if (!zan_pkg_load(&pkg, manifest_path)) {
        fprintf(stderr, "error: failed to load package manifest '%s'\n", manifest_path);
        return 1;
    }

    /* 检查是否已存在同名依赖，若存在则更新 */
    zan_pkg_remove_dep(&pkg, name);
    zan_pkg_add_dep(&pkg, name, source, constraint);

    bool ok = zan_pkg_save(&pkg, manifest_path);
    zan_pkg_destroy(&pkg);

    if (ok) {
        printf("Added dependency '%s' (%s) to '%s'\n", name, constraint, manifest_path);
        return 0;
    } else {
        fprintf(stderr, "error: failed to save '%s'\n", manifest_path);
        return 1;
    }
}

static int cmd_remove(const char *project_dir, int argc, char **argv) {
    if (argc < 1) {
        fprintf(stderr, "error: 'remove' requires package name: zan-pm remove <name>\n");
        return 1;
    }
    const char *name = argv[0];

    char manifest_path[1024];
    snprintf(manifest_path, sizeof(manifest_path), "%s/zan.pkg", project_dir);

    zan_package_t pkg;
    if (!zan_pkg_load(&pkg, manifest_path)) {
        fprintf(stderr, "error: failed to load package manifest '%s'\n", manifest_path);
        return 1;
    }

    bool removed = zan_pkg_remove_dep(&pkg, name);
    if (!removed) {
        fprintf(stderr, "warning: dependency '%s' was not found in '%s'\n", name, manifest_path);
        zan_pkg_destroy(&pkg);
        return 0;
    }

    bool ok = zan_pkg_save(&pkg, manifest_path);
    zan_pkg_destroy(&pkg);

    if (ok) {
        printf("Removed dependency '%s' from '%s'\n", name, manifest_path);
        return 0;
    } else {
        fprintf(stderr, "error: failed to save '%s'\n", manifest_path);
        return 1;
    }
}

static int cmd_list(const char *project_dir) {
    char manifest_path[1024];
    snprintf(manifest_path, sizeof(manifest_path), "%s/zan.pkg", project_dir);

    zan_package_t pkg;
    if (!zan_pkg_load(&pkg, manifest_path)) {
        fprintf(stderr, "error: failed to load package manifest '%s'\n", manifest_path);
        return 1;
    }

    printf("Package: %s\n", pkg.name[0] ? pkg.name : "<unnamed>");
    if (pkg.has_version) {
        char vbuf[64];
        zan_version_format(&pkg.version, vbuf, sizeof(vbuf));
        printf("Version: %s\n", vbuf);
    }
    printf("Dependencies (%d):\n", pkg.dep_count);
    for (int i = 0; i < pkg.dep_count; i++) {
        zan_dependency_t *d = &pkg.deps[i];
        printf("  - %s", d->name);
        if (d->source[0]) {
            printf(" (source: %s)", d->source);
        }
        printf("\n");
    }

    zan_pkg_destroy(&pkg);
    return 0;
}

static int cmd_lock(const char *project_dir) {
    char manifest_path[1024];
    snprintf(manifest_path, sizeof(manifest_path), "%s/zan.pkg", project_dir);

    zan_package_t pkg;
    if (!zan_pkg_load(&pkg, manifest_path)) {
        fprintf(stderr, "error: failed to load package manifest '%s'\n", manifest_path);
        return 1;
    }

    zan_pkg_registry_t reg;
    zan_pkg_init(&reg, project_dir);

    for (int i = 0; i < pkg.dep_count; i++) {
        zan_dependency_t *d = &pkg.deps[i];
        zan_package_t *dep_pkg = (zan_package_t *)calloc(1, sizeof(zan_package_t));
        strncpy(dep_pkg->name, d->name, sizeof(dep_pkg->name) - 1);
        strncpy(dep_pkg->source, d->source, sizeof(dep_pkg->source) - 1);
        dep_pkg->version = d->min_ver;
        dep_pkg->has_version = true;
        reg.resolved[reg.resolved_count++] = dep_pkg;
    }

    bool ok = zan_pkg_write_lock(&reg);
    zan_pkg_registry_destroy(&reg);
    zan_pkg_destroy(&pkg);

    if (ok) {
        printf("Successfully generated '%s/zan.lock'\n", project_dir);
        return 0;
    } else {
        fprintf(stderr, "error: failed to write lockfile for '%s'\n", project_dir);
        return 1;
    }
}

static int cmd_check(const char *project_dir) {
    char manifest_path[1024];
    snprintf(manifest_path, sizeof(manifest_path), "%s/zan.pkg", project_dir);

    zan_package_t pkg;
    if (!zan_pkg_load(&pkg, manifest_path)) {
        fprintf(stderr, "error: failed to load package manifest '%s'\n", manifest_path);
        return 1;
    }

    zan_pkg_registry_t reg;
    zan_pkg_init(&reg, project_dir);
    bool has_lock = zan_pkg_read_lock(&reg);

    printf("Package: %s\n", pkg.name);
    printf("Declared dependencies: %d\n", pkg.dep_count);
    printf("Lockfile status: %s\n", has_lock ? "valid" : "missing or invalid");
    if (has_lock) {
        printf("Resolved in lockfile: %d\n", reg.resolved_count);
    }

    zan_pkg_registry_destroy(&reg);
    zan_pkg_destroy(&pkg);
    return has_lock ? 0 : 1;
}

int main(int argc, char **argv) {
    const char *project_dir = ".";
    const char *source_override = NULL;
    const char *command = NULL;
    const char *cmd_args[64];
    int cmd_argc = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        }
        if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0) {
            printf("zan-pm %s\n", ZAN_VERSION);
            return 0;
        }
        if (strcmp(argv[i], "--project") == 0 && i + 1 < argc) {
            project_dir = argv[++i];
            continue;
        }
        if (strcmp(argv[i], "--source") == 0 && i + 1 < argc) {
            source_override = argv[++i];
            continue;
        }
        if (argv[i][0] == '-') {
            fprintf(stderr, "unknown option: '%s'\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
        if (!command) {
            command = argv[i];
        } else if (cmd_argc < 64) {
            cmd_args[cmd_argc++] = argv[i];
        }
    }

    if (!command) {
        print_usage(argv[0]);
        return 1;
    }

    if (strcmp(command, "init") == 0) {
        return cmd_init(project_dir, cmd_argc, (char **)cmd_args);
    } else if (strcmp(command, "add") == 0) {
        return cmd_add(project_dir, cmd_argc, (char **)cmd_args, source_override);
    } else if (strcmp(command, "remove") == 0) {
        return cmd_remove(project_dir, cmd_argc, (char **)cmd_args);
    } else if (strcmp(command, "list") == 0) {
        return cmd_list(project_dir);
    } else if (strcmp(command, "lock") == 0) {
        return cmd_lock(project_dir);
    } else if (strcmp(command, "check") == 0) {
        return cmd_check(project_dir);
    } else {
        fprintf(stderr, "unknown command: '%s'\n", command);
        print_usage(argv[0]);
        return 1;
    }
}
