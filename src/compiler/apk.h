/* apk */

#ifndef ZAN_APK_H
#define ZAN_APK_H

#include <stddef.h>

/* Assemble + sign the APK */
int zan_apk_build(const char *apk_path, const char *lib_main,
                  const char *abi, const char *package, const char *label,
                  const char *shell_dir, char **extra_libs, int extra_count,
                  int perm_count, const char (*perms)[128]);

#endif /* ZAN_APK_H */
