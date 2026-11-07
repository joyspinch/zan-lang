#include "package.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#define PATH_SEP "\\"
#define popen _popen
#define pclose _pclose
#define strdup _strdup
#else
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <pthread.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#define PATH_SEP "/"
#endif

#include "../common/host_oom.h"
#include "../common/json.h"
#include "diag.h"
#include "incremental.h"

/* Forward declarations for cross-module static utilities */
static bool pkg_is_dir(const char *path);
static bool pkg_safe_component(const char *s);
static bool pkg_mkdirs(const char *path);
static bool pkg_copy_tree(const char *src, const char *dst, int depth);

/* Composite Dispatcher Submodules */
#include "package/package_semver.c"
#include "package/package_manifest.c"
#include "package/package_solve.c"
#include "package/package_store.c"
#include "package/package_index.c"
