#include "mobile_postlink.h"
#include "apk.h"
#include "ipa.h"

#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include "win_utf8.h"
#define fopen zan_utf8_fopen
#define remove zan_utf8_remove
#else
#include <unistd.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#include <stdint.h>
#endif
#endif

static const char *last_path_separator(const char *path) {
    const char *slash = strrchr(path, '/');
    const char *backslash = strrchr(path, '\\');
    return !slash || (backslash && backslash > slash) ? backslash : slash;
}

static const char *path_basename(const char *path) {
    const char *separator = last_path_separator(path);
    return separator ? separator + 1 : path;
}

static void strip_zan_suffix(char *name) {
    char *dot = strrchr(name, '.');
    if (dot && strcmp(dot, ".zan") == 0) *dot = '\0';
}

static int build_apk(const zan_mobile_postlink_options_t *options) {
    const char *abi = options->apk_arm64 ? "arm64-v8a" : "x86_64";
    const char *base = path_basename(options->input_path);
    char package[256], label[256];

    if (options->apk_package) {
        if (strlen(options->apk_package) >= sizeof(package)) {
            fprintf(stderr, "error: --apk-package exceeds %zu bytes\n",
                    sizeof(package) - 1);
            return 1;
        }
        snprintf(package, sizeof(package), "%s", options->apk_package);
    } else {
        snprintf(package, sizeof(package), "dev.zan.%s", base);
        strip_zan_suffix(package);
        for (char *c = package; *c; c++) {
            if (!((*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') ||
                  (*c >= '0' && *c <= '9') || *c == '_' || *c == '.')) {
                *c = '_';
            }
        }
    }
    if (options->apk_label) {
        if (strlen(options->apk_label) >= sizeof(label)) {
            fprintf(stderr, "error: --apk-label exceeds %zu bytes\n",
                    sizeof(label) - 1);
            return 1;
        }
        snprintf(label, sizeof(label), "%s", options->apk_label);
    } else {
        snprintf(label, sizeof(label), "%s", base);
        strip_zan_suffix(label);
    }

    char extra_paths[256][128];
    char *extras[256];
    int extra_count = 0;
    for (int d = 0; d < options->driver_count && extra_count < 256; d++) {
        const char *dir = options->driver_dirs[d];
        if (!dir[0]) continue;
        char driver[64];
        snprintf(driver, sizeof(driver), "%.*s", options->driver_name_lengths[d],
                 options->driver_names[d]);
        char manifest_path[1300];
        snprintf(manifest_path, sizeof(manifest_path), "%s/%s.bundle", dir, driver);
        FILE *manifest = fopen(manifest_path, "rb");
        if (!manifest) continue;
        char line[128];
        while (fgets(line, sizeof(line), manifest) && extra_count < 256) {
            size_t len = strlen(line);
            while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r' ||
                               line[len - 1] == ' ' || line[len - 1] == '\t')) {
                line[--len] = '\0';
            }
            if (!strstr(line, ".so")) continue;
            snprintf(extra_paths[extra_count], sizeof(extra_paths[0]), "%s/%s", dir, line);
            extras[extra_count] = extra_paths[extra_count];
            extra_count++;
        }
        fclose(manifest);
    }

    char exe_dir[1024] = {0};
#ifdef _WIN32
    char module[1024];
    DWORD len = GetModuleFileNameA(NULL, module, sizeof(module));
    if (len > 0 && len < sizeof(module)) {
        char *separator = strrchr(module, '\\');
        if (separator) *separator = '\0';
        snprintf(exe_dir, sizeof(exe_dir), "%s", module);
    }
#elif defined(__APPLE__)
    uint32_t len = sizeof(exe_dir);
    if (_NSGetExecutablePath(exe_dir, &len) == 0) {
        char *separator = strrchr(exe_dir, '/');
        if (separator) *separator = '\0';
    }
#else
    ssize_t len = readlink("/proc/self/exe", exe_dir, sizeof(exe_dir) - 1);
    if (len > 0) {
        exe_dir[len] = '\0';
        char *separator = strrchr(exe_dir, '/');
        if (separator) *separator = '\0';
    }
#endif
    char shell_dir[1300];
    snprintf(shell_dir, sizeof(shell_dir), "%s/apk-shell", exe_dir);
    if (!options->quiet)
        printf("  packaging APK ? %s\n", options->apk_path);
    return zan_apk_build(options->staged_path, options->linked_path, abi, package, label,
                         shell_dir, extras, extra_count, options->android_permission_count,
                         options->android_permissions);
}

static bool path_exists(const char *path) {
#ifdef _WIN32
    return zan_utf8_get_file_attributes(path) != INVALID_FILE_ATTRIBUTES;
#else
    return access(path, F_OK) == 0;
#endif
}

static const char *find_ipa_assets(const char *input_path, char candidate[1200]) {
    if (input_path) {
        char dir[1200];
        snprintf(dir, sizeof(dir), "%s", input_path);
        for (int level = 0; level < 2; level++) {
            const char *separator = last_path_separator(dir);
            if (!separator) break;
            dir[separator - dir] = '\0';
            snprintf(candidate, 1200, "%s/assets", dir);
            if (path_exists(candidate)) return candidate;
        }
    }
    return path_exists("assets") ? "assets" : NULL;
}

static int build_ipa(const zan_mobile_postlink_options_t *options) {
    const char *app_name = options->ipa_name;
    char default_name[256];
    if (!app_name) {
        const char *base = path_basename(options->ipa_path);
        if (!base[0] || strcmp(base, "out.exe") == 0 || strcmp(base, "main") == 0)
            base = path_basename(options->input_path);
        size_t len = strlen(base);
        if (len > 4 && (strcmp(base + len - 4, ".ipa") == 0 ||
                        strcmp(base + len - 4, ".exe") == 0 ||
                        strcmp(base + len - 4, ".zan") == 0)) {
            snprintf(default_name, sizeof(default_name), "%.*s", (int)(len - 4), base);
        } else {
            snprintf(default_name, sizeof(default_name), "%s", base);
        }
        app_name = default_name;
    }
    const char *bundle_id = options->ipa_bundle_id;
    char default_bundle_id[512];
    if (!bundle_id) {
        snprintf(default_bundle_id, sizeof(default_bundle_id), "dev.zan.%s", app_name);
        bundle_id = default_bundle_id;
    }

    const char *icon = options->icon_path;
    if (!icon) {
        const char *candidates[] = {"assets/icon.png", "assets/icon.ico", "icon.png", "icon.ico", NULL};
        for (int i = 0; candidates[i]; i++) {
            FILE *file = fopen(candidates[i], "rb");
            if (file) {
                fclose(file);
                icon = candidates[i];
                break;
            }
        }
    }

    char assets_candidate[1200];
    const char *assets = find_ipa_assets(options->input_path, assets_candidate);
    return zan_ipa_build(options->staged_path, options->linked_path, app_name, bundle_id,
                         app_name, "1.0.0", icon, assets);
}

int zan_mobile_postlink(const zan_mobile_postlink_options_t *options) {
    int status = options->apk_path ? build_apk(options) : build_ipa(options);
    remove(options->linked_path);
    return status != 0;
}
