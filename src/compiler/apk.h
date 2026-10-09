/* apk.h -- one-shot Android APK assembly for zanc (--emit-apk).
 *
 * Packages the linked libmain.so (NativeActivity shell) plus bundled driver
 * .so files into a signed APK with no Android SDK: AndroidManifest.xml comes
 * from a precompiled binary template with only the package name and label
 * patched in its string pool (no aapt2); resources.arsc / classes.dex are
 * fixed prebuilts (shell Java side, built by scripts/build_apk_shell_dex.sh);
 * native libs are STORED and 4-byte aligned, resources.arsc is uncompressed
 * and 4-byte aligned (Android 11+ requires it); signing runs apksigner.jar
 * through a discovered or auto-downloaded Java runtime. */

#ifndef ZAN_APK_H
#define ZAN_APK_H

#include <stddef.h>

/* Assemble + sign the APK.
 *
 *   apk_path     output .apk (overwritten)
 *   lib_main     path of the linked libmain.so (goes to lib/<abi>/libmain.so)
 *   abi          "x86_64" or "arm64-v8a" (maps from the zan target arch)
 *   package      application id, e.g. "com.example.myapp" ([a-zA-Z0-9_.])
 *   label        application label (UTF-8; also the launcher name)
 *   shell_dir    directory holding the prebuilt shell assets
 *                (AndroidManifest.xml.bin, resources.arsc, classes.dex)
 *   extra_libs   extra .so files to pack into lib/<abi>/ (bundled drivers),
 *                each a path; may be NULL
 *   extra_count  number of extra_libs entries
 *   perms        extra <uses-permission android:name> values to append to
 *                the manifest (full names or "android.permission.X"); each
 *                up to 127 chars; may be NULL when perm_count is 0
 *   perm_count   number of perms entries
 *
 * Returns 0 on success. On failure a diagnostic was printed to stderr and a
 * nonzero code is returned; any partial output file is removed. */
int zan_apk_build(const char *apk_path, const char *lib_main,
                  const char *abi, const char *package, const char *label,
                  const char *shell_dir, char **extra_libs, int extra_count,
                  int perm_count, const char (*perms)[128]);

#endif /* ZAN_APK_H */
