/* apk.h -- one-shot Android APK assembly for zanc (--emit-apk).
 *
 * A Zan GUI program for Android is a shared library (libmain.so exporting
 * SDL_main) run by an SDLActivity shell. This module packages the linked
 * library plus its bundled driver .so files into an installable, signed APK
 * without any Android SDK on the machine:
 *
 *   - AndroidManifest.xml comes from a precompiled binary template
 *     (toolchain/apk-shell/AndroidManifest.xml.bin); only the package name
 *     and application label are per-project, and both are patched in the
 *     template's binary string pool (no aapt2 needed).
 *   - resources.arsc / classes.dex are fixed prebuilt files (SDL3's Java
 *     side compiled once at template build time).
 *   - the zip is written here with STORED native libs aligned to 4 bytes
 *     and an uncompressed, 4-byte-aligned resources.arsc (Android 11+
 *     requires the latter); signing runs apksigner.jar (staged next to
 *     zanc) through a discovered or auto-downloaded Java runtime. */

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
 *
 * Returns 0 on success. On failure a diagnostic was printed to stderr and a
 * nonzero code is returned; any partial output file is removed. */
int zan_apk_build(const char *apk_path, const char *lib_main,
                  const char *abi, const char *package, const char *label,
                  const char *shell_dir, char **extra_libs, int extra_count);

#endif /* ZAN_APK_H */
