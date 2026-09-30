/* zan_cef_helper -- the Chromium subprocess executable for macOS bundles.
 *
 * On macOS a Chromium child process must not be the host application's own
 * executable: it would be launched inside the host's .app bundle, so
 * LaunchServices registers every render/gpu/utility process as a copy of the
 * app (a Dock icon per child). CEF's answer is a separate helper bundle, and
 * this program is its executable: ~30 KB whose whole job is to load the
 * zan_cef driver next door and hand the process over to cef_execute_process.
 *
 * It deliberately does not duplicate the host program: the helper needs no Zan
 * runtime, no GUI driver and no TLS libraries, only libcef -- which it reaches
 * through the same driver dylib the browser process already loaded, so the two
 * always agree on the CEF API version they were built against.
 *
 * Where the driver and the CEF runtime live comes from the browser process
 * through the environment (zan_cef_init exports these before it initializes
 * CEF, and Chromium passes its own environment on to the children):
 *
 *   ZAN_CEF_HELPER_DRIVER    absolute path of libzan_cef.dylib
 *   ZAN_CEF_HELPER_RUNTIME   CEF runtime directory (holds Release/...framework)
 *   ZAN_CEF_HELPER_SWITCHES  the host's extra Chromium switches
 *
 * The driver path also has a layout fallback, so a helper started with a
 * stripped environment still finds it: Contents/Frameworks/<x> Helper.app/
 * Contents/MacOS/<x> Helper -> Contents/MacOS/libzan_cef.dylib of the outer
 * bundle, and, failing that, next to the helper itself.
 */

#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

typedef int (*zch_execute_fn)(const char *runtime_dir, const char *switches);

static int zch_exists(const char *path) {
    struct stat st;
    return path && path[0] && stat(path, &st) == 0 && !S_ISDIR(st.st_mode);
}

/* Strips `n` trailing path components from `p`, in place. */
static void zch_up(char *p, int n) {
    for (int i = 0; i < n; i++) {
        char *slash = strrchr(p, '/');
        if (!slash) { p[0] = '\0'; return; }
        *slash = '\0';
    }
}

static int zch_exe_path(char *buf, size_t cap) {
#ifdef __APPLE__
    uint32_t size = (uint32_t)cap;
    if (_NSGetExecutablePath(buf, &size) == 0) return 1;
#else
    ssize_t n = readlink("/proc/self/exe", buf, cap - 1);
    if (n > 0) { buf[n] = '\0'; return 1; }
#endif
    buf[0] = '\0';
    return 0;
}

/* libzan_cef.dylib: environment first (what the browser process actually
 * loaded), then the two places the publish layout can put it. */
static int zch_driver_path(char *out, size_t cap) {
    const char *env = getenv("ZAN_CEF_HELPER_DRIVER");
    if (zch_exists(env)) {
        snprintf(out, cap, "%s", env);
        return 1;
    }
    char exe[1024];
    if (!zch_exe_path(exe, sizeof(exe))) return 0;

    /* .../<app>.app/Contents/Frameworks/<x> Helper.app/Contents/MacOS/<x> */
    char base[1024];
    snprintf(base, sizeof(base), "%s", exe);
    zch_up(base, 5);
    snprintf(out, cap, "%s/MacOS/libzan_cef.dylib", base);
    if (zch_exists(out)) return 1;

    snprintf(base, sizeof(base), "%s", exe);
    zch_up(base, 1);
    snprintf(out, cap, "%s/libzan_cef.dylib", base);
    return zch_exists(out);
}

/* ZAN_CEF_LOG=1 (Chromium hands the browser process's environment to its
 * children, so the host's setting reaches every helper): report what this
 * process is and how far it got. A Chromium child that dies before CEF has
 * initialized its logging writes nothing to the CEF log file, so this is the
 * only place the type of a subprocess that never came up shows up. */
static int zch_log_on(void) {
    const char *v = getenv("ZAN_CEF_LOG");
    return v && v[0] && strcmp(v, "0") != 0;
}

/* The --type= of this process, "browser" when there is none. */
static const char *zch_type(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        if (argv[i] && strncmp(argv[i], "--type=", 7) == 0) return argv[i] + 7;
    }
    return "browser";
}

int main(int argc, char **argv) {
    int log = zch_log_on();
    const char *type = zch_type(argc, argv);
    if (log) {
        fprintf(stderr, "[helper] enter type=%s pid=%d argc=%d\n", type,
                (int)getpid(), argc);
        fflush(stderr);
    }
    char driver[1024];
    if (!zch_driver_path(driver, sizeof(driver))) {
        fprintf(stderr, "zan_cef_helper: cannot locate libzan_cef.dylib "
                        "(set ZAN_CEF_HELPER_DRIVER)\n");
        return 1;
    }
    void *lib = dlopen(driver, RTLD_NOW | RTLD_LOCAL);
    if (!lib) {
        fprintf(stderr, "zan_cef_helper: cannot load %s: %s\n", driver,
                dlerror());
        return 1;
    }
    void *sym = dlsym(lib, "zan_cef_execute_process");
    if (!sym) {
        fprintf(stderr, "zan_cef_helper: %s exports no "
                        "zan_cef_execute_process\n", driver);
        return 1;
    }
    zch_execute_fn execute = NULL;
    memcpy(&execute, &sym, sizeof(void *));

    const char *runtime = getenv("ZAN_CEF_HELPER_RUNTIME");
    const char *switches = getenv("ZAN_CEF_HELPER_SWITCHES");
    if (!runtime || !runtime[0]) {
        fprintf(stderr, "zan_cef_helper: ZAN_CEF_HELPER_RUNTIME is not set; "
                        "the browser process exports it before it starts CEF\n");
        return 1;
    }
    if (log) {
        fprintf(stderr, "[helper] execute type=%s driver=%s runtime=%s\n", type,
                driver, runtime);
        fflush(stderr);
    }
    int rc = execute(runtime, switches ? switches : "");
    if (log) {
        fprintf(stderr, "[helper] leave type=%s rc=%d\n", type, rc);
        fflush(stderr);
    }
    /* -1 means "this is the browser process" -- impossible here, since the
     * helper is only ever spawned by Chromium with a --type= argument. */
    if (rc < 0) return 1;
    return rc;
}
