#include "main_options_parse.h"
/* main */

#include "zan.h"
#include "arena.h"
#include "diag.h"
#include "lexer.h"
#include "parser.h"
#include "genmeta.h"
#include "nsresolve.h"
#include "ast.h"
#include "binder.h"
#include "checker.h"
#include "irgen.h"
#include "optimizer.h"
#include "crosscomp.h"
#include "winres.h"
#include "embedres.h"
#include "symbols.h"
#include "package.h"
#include "win_utf8.h"
#include "apk.h"
#include "ipa.h"
#include <llvm-c/Comdat.h>
/* zan */
#include "zan_default_icon.h"
#include "zan_version.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include <time.h>
#include <errno.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#include <process.h>
#define fopen zan_utf8_fopen
#define remove zan_utf8_remove
#define rename zan_utf8_rename
#define system zan_utf8_system
#define strtok_r(str, delim, save) strtok_s((str), (delim), (save))
#ifndef S_ISDIR
#define S_ISDIR(m) (((m) & _S_IFMT) == _S_IFDIR)
#endif
#else
#include <unistd.h>
#include <dirent.h>
#include <strings.h>
#include <spawn.h>
#include <sys/wait.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#endif
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

#include "../common/host_oom.h"

#define ZAN_LINK_MAX_ARGV        8192
#define ZAN_MAX_USED_DRIVERS     1024
#define ZAN_MAX_STATIC_DRV_LIBS  2048
#define ZAN_LINK_ARGV_TAIL        32

typedef struct {
    char **paths;
    int count;
    int capacity;
} generated_object_vec_t;

static bool zan_is_zcomp_path(const char *p) {
    size_t n = strlen(p);
    return n > 6 && strcmp(p + n - 6, ".zcomp") == 0;
}

static bool zan_is_html_input(const char *p) {
    size_t n = strlen(p);
    return (n > 5 && strcmp(p + n - 5, ".html") == 0) ||
           (n > 4 && strcmp(p + n - 4, ".htm") == 0);
}

static void generated_object_vec_add(generated_object_vec_t *objects,
                                     const char *path) {
    if (objects->count == objects->capacity) {
        int next = objects->capacity ? objects->capacity * 2 : 4;
        char **grown = (char **)realloc(objects->paths,
                                        (size_t)next * sizeof(*grown));
        if (!grown) {
            fprintf(stderr, "error: out of memory tracking generated objects\n");
            exit(1);
        }
        objects->paths = grown;
        objects->capacity = next;
    }
    size_t len = strlen(path);
    objects->paths[objects->count] = (char *)malloc(len + 1);
    if (!objects->paths[objects->count]) {
        fprintf(stderr, "error: out of memory tracking generated object path\n");
        exit(1);
    }
    memcpy(objects->paths[objects->count], path, len + 1);
    objects->count++;
}

static void generated_object_vec_release(generated_object_vec_t *objects) {
    for (int i = 0; i < objects->count; i++)
        free(objects->paths[i]);
    free(objects->paths);
    objects->paths = NULL;
    objects->count = 0;
    objects->capacity = 0;
}

static void generated_object_vec_remove(generated_object_vec_t *objects,
                                        const char *single_fallback) {
    if (objects->count == 0) {
        remove(single_fallback);
    } else {
        for (int i = 0; i < objects->count; i++)
            remove(objects->paths[i]);
    }
    generated_object_vec_release(objects);
}

static void generated_object_vec_keep_or_remove(generated_object_vec_t *objects,
                                                const char *single_fallback,
                                                bool keep) {
    if (keep) {
        if (objects->count == 0) {
            fprintf(stderr, "note: failed-link object retained at '%s'\n",
                    single_fallback);
        } else {
            for (int i = 0; i < objects->count; i++)
                fprintf(stderr, "note: failed-link object retained at '%s'\n",
                        objects->paths[i]);
        }
        generated_object_vec_release(objects);
    } else {
        generated_object_vec_remove(objects, single_fallback);
    }
}

static void link_cap_exceeded(const char *what, int cap) {
    fprintf(stderr,
            "zanc: too many %s for one link (limit %d) -- raise the matching"
            " ZAN_LINK_MAX_* in src/compiler/main.c\n", what, cap);
    exit(1);
}

static void cmd_appendf(char *cmd, size_t cap, const char *fmt, ...) {
    size_t cur = strlen(cmd);
    int need = -1;
    if (cur < cap) {
        va_list ap;
        va_start(ap, fmt);
        need = vsnprintf(cmd + cur, cap - cur, fmt, ap);
        va_end(ap);
    }
    if (need < 0 || cur >= cap || (size_t)need >= cap - cur) {
        fprintf(stderr,
                "zanc: link command line exceeded its %d-byte buffer -- too"
                " many inputs for one link\n", (int)cap);
        exit(1);
    }
}

bool g_time_phases = false;
double g_phase_start = 0.0;

typedef struct {
    size_t file_reads;
    size_t bytes_read;
    size_t metadata_scans;
    size_t seed_sources;
    size_t seed_lex_passes;
    size_t throwaway_parses;
    size_t throwaway_parse_fallbacks;
    size_t real_parses;
    size_t secondary_parses;
    size_t metadata_cache_hits;
    size_t metadata_cache_misses;
    size_t metadata_cache_writes;
} zan_scale_stats_t;

static zan_scale_stats_t g_scale_stats;

double now_ms(void) {
#ifdef _WIN32
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart * 1000.0 / (double)f.QuadPart;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
#endif
}

static zan_arena_t *g_main_arena = NULL;

static void phase(const char *name) {
    if (!g_time_phases) return;
    double t = now_ms();
    size_t mem_mb = 0, ws_mb = 0, peak_mb = 0;
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
        mem_mb = pmc.PagefileUsage / (1024 * 1024);
        ws_mb = pmc.WorkingSetSize / (1024 * 1024);
        peak_mb = pmc.PeakPagefileUsage / (1024 * 1024);
    }
#endif
    size_t ar_mb = g_main_arena ? (zan_arena_total_bytes(g_main_arena) / (1024 * 1024)) : 0;
    if (g_phase_start > 0.0)
        fprintf(stderr, "%10.1f ms  [Commit: %4zu MB, Peak: %4zu MB, WS: %4zu MB, Arena: %4zu MB]  %s\n",
                t - g_phase_start, mem_mb, peak_mb, ws_mb, ar_mb, name);
    g_phase_start = t;
}

static void report_suppressed_errors(const zan_diag_t *diag) {
    int dropped = zan_diag_suppressed_errors(diag);
    if (dropped > 0) {
        fprintf(stderr,
                "note: %d further error(s) were suppressed by the error limit"
                " (-ferror-limit=0 shows all)\n",
                dropped);
    }
}

static void probe_phase_mem(const char *name) {
#ifdef _WIN32
    if (!getenv("ZAN_PROBE_MEM")) return;
    PROCESS_MEMORY_COUNTERS pmc;
    if (!GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) return;
    size_t ar_mb = g_main_arena ? (zan_arena_total_bytes(g_main_arena) / (1024 * 1024)) : 0;
    fprintf(stderr, "    [phase %-14s Commit: %4zu MB, Peak: %4zu MB, Arena: %4zu MB]  %s\n",
            "", pmc.PagefileUsage / (1024 * 1024),
            pmc.PeakPagefileUsage / (1024 * 1024), ar_mb, name);
#endif
}

static char *read_file(const char *path, size_t *out_len) {
#ifdef _WIN32
    if (zan_utf8_is_directory(path)) {
#else
    struct stat st;
    if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) {
#endif
        fprintf(stderr, "error: '%s' is a directory, not a source file\n", path);
        return NULL;
    }
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "error: cannot open file '%s'\n", path);
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len < 0) {
        fclose(f);
        fprintf(stderr, "error: cannot read file '%s'\n", path);
        return NULL;
    }
    char *buf = (char *)malloc((size_t)len + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    size_t read = fread(buf, 1, (size_t)len, f);
    buf[read] = '\0';
    fclose(f);
    g_scale_stats.file_reads++;
    g_scale_stats.bytes_read += read;
    if (out_len) *out_len = read;
    return buf;
}

static char *read_file_arena(zan_arena_t *arena, const char *path, size_t *out_len) {
#ifdef _WIN32
    if (zan_utf8_is_directory(path)) {
#else
    struct stat st;
    if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) {
#endif
        fprintf(stderr, "error: '%s' is a directory, not a source file\n", path);
        return NULL;
    }
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "error: cannot open file '%s'\n", path);
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len < 0) {
        fclose(f);
        fprintf(stderr, "error: cannot read file '%s'\n", path);
        return NULL;
    }
    char *buf = (char *)zan_arena_alloc(arena, (size_t)len + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    size_t read = fread(buf, 1, (size_t)len, f);
    buf[read] = '\0';
    fclose(f);
    g_scale_stats.file_reads++;
    g_scale_stats.bytes_read += read;
    if (out_len) *out_len = read;
    return buf;
}

#include "driver/main_sources.c"



#include "driver/main_prune.c"

static void zan_apply_lex_defines(zan_lexer_t *lex, zan_target_t target,
                                  const char *const *pp_defines,
                                  int pp_define_count,
                                  bool is_publish) {
    switch (target.os) {
    case ZAN_OS_WINDOWS:
        zan_lexer_define(lex, "WINDOWS", "1");
        zan_lexer_define(lex, "WIN32", "1");
        break;
    case ZAN_OS_LINUX:
        zan_lexer_define(lex, "LINUX", "1");
        break;
    case ZAN_OS_ANDROID:
        zan_lexer_define(lex, "LINUX", "1");
        zan_lexer_define(lex, "ANDROID", "1");
        break;
    case ZAN_OS_OHOS:
        zan_lexer_define(lex, "LINUX", "1");
        zan_lexer_define(lex, "MUSL", "1");
        zan_lexer_define(lex, "OHOS", "1");
        break;
    case ZAN_OS_MACOS:
        zan_lexer_define(lex, "MACOS", "1");
        zan_lexer_define(lex, "APPLE", "1");
        break;
    case ZAN_OS_IOS:
        zan_lexer_define(lex, "IOS", "1");
        zan_lexer_define(lex, "APPLE", "1");
        break;
    default:
        break;
    }
    if (target.arch == ZAN_ARCH_AARCH64)
        zan_lexer_define(lex, "ARM64", "1");
    else if (target.arch == ZAN_ARCH_X86_64)
        zan_lexer_define(lex, "X86_64", "1");
    else if (target.arch == ZAN_ARCH_RISCV64)
        zan_lexer_define(lex, "RISCV64", "1");
    else if (target.arch == ZAN_ARCH_RISCV32)
        zan_lexer_define(lex, "RISCV32", "1");
    else if (target.arch == ZAN_ARCH_WASM32)
        zan_lexer_define(lex, "WASM32", "1");
    if (target.os == ZAN_OS_WASI)
        zan_lexer_define(lex, "WASI", "1");
    if (target.abi == ZAN_ABI_MUSL)
        zan_lexer_define(lex, "MUSL", "1");
    zan_lexer_define(lex, "ZAN", "1");
    for (int di = 0; di < pp_define_count; di++) {
        char dname[64];
        const char *dval = "1";
        const char *eq = strchr(pp_defines[di], '=');
        if (eq) {
            int nl = (int)(eq - pp_defines[di]);
            if (nl > 63) nl = 63;
            memcpy(dname, pp_defines[di], nl);
            dname[nl] = '\0';
            dval = eq + 1;
        } else {
            strncpy(dname, pp_defines[di], 63);
            dname[63] = '\0';
        }
        zan_lexer_define(lex, dname, dval);
    }
    if (is_publish) {
        zan_lexer_define(lex, "PUBLISH", "1");
        zan_lexer_define(lex, "RELEASE", "1");
    } else {
        zan_lexer_define(lex, "DEBUG", "1");
    }
}

static zan_ast_node_t *parse_secondary_unit(const char *path,
                                            zan_target_t target,
                                            const char *const *pp_defines,
                                            int pp_define_count,
                                            bool is_publish,
                                            zan_arena_t *arena,
                                            zan_diag_t *diag) {
    g_scale_stats.secondary_parses++;
    size_t slen = 0;
    char *src = read_file(path, &slen);
    if (!src) return NULL;
    char *heap_src = src;
    src = zan_arena_strdup(arena, heap_src, slen);
    free(heap_src);
    if (!src) return NULL;
    int file_id = diag->file_count;
    zan_diag_add_file(diag, path, src);
    zan_lexer_t lex;
    zan_lexer_init(&lex, src, slen, file_id, arena, diag);
    zan_apply_lex_defines(&lex, target, pp_defines, pp_define_count,
                          is_publish);
    zan_parser_t parser;
    zan_parser_init(&parser, &lex, arena, diag);
    zan_ast_node_t *unit = zan_parser_parse(&parser);
    return unit;
}

#include "driver/main_synth.c"
#include "driver/main_dump.c"
#include "driver/main_driver.c"

static int zan_run_archiver(const char *tool, const char *format,
                            const char *arc, const char *obj) {
    const char *argv[] = {tool, format, "rcsD", arc, obj, NULL};
#ifdef _WIN32
    wchar_t *wide_tool = zan_utf8_to_wide_alloc(tool);
    if (!wide_tool) return 1;
    DWORD needed = SearchPathW(NULL, wide_tool, L".exe", 0, NULL, NULL);
    if (!needed) { free(wide_tool); return -1; }
    wchar_t *wide_path = malloc((size_t)needed * sizeof(*wide_path));
    if (!wide_path) { free(wide_tool); return 1; }
    DWORD written = SearchPathW(NULL, wide_tool, L".exe", needed, wide_path, NULL);
    free(wide_tool);
    if (!written || written >= needed) { free(wide_path); return -1; }
    char *path = zan_wide_to_utf8_alloc(wide_path);
    free(wide_path);
    if (!path) return 1;
    argv[0] = path;
    intptr_t rc = zan_utf8_spawnv(_P_WAIT, path,
                                  (const char *const *)argv);
    if (rc < 0)
        fprintf(stderr, "error: cannot start archiver '%s': %s\n",
                path, strerror(errno));
    free(path);
    return rc == 0 ? 0 : 1;
#else
    extern char **environ;
    pid_t child;
    int rc = posix_spawnp(&child, tool, NULL, NULL,
                          (char *const *)argv, environ);
    if (rc == ENOENT || rc == ENOTDIR) return -1;
    if (rc != 0) {
        fprintf(stderr, "error: cannot start archiver '%s': %s\n",
                tool, strerror(rc));
        return 1;
    }
    int status;
    while (waitpid(child, &status, 0) < 0) {
        if (errno == EINTR) continue;
        fprintf(stderr, "error: cannot wait for archiver '%s': %s\n",
                tool, strerror(errno));
        return 1;
    }
    return WIFEXITED(status) && WEXITSTATUS(status) == 0 ? 0 : 1;
#endif
}

static int zan_write_static_lib(const char *obj, const char *arc,
                                zan_os_t target_os) {
    char exe_dir[1024], bundled[1200] = {0}, versioned[64];
    zan_exe_dir(exe_dir, sizeof(exe_dir));
#ifdef _WIN32
    const char *suffix = ".exe";
#else
    const char *suffix = "";
#endif
    if (exe_dir[0])
        snprintf(bundled, sizeof(bundled), "%s/llvm-ar%s", exe_dir, suffix);
    snprintf(versioned, sizeof(versioned), "llvm-ar-%d%s", ZAN_LLVM_MAJOR,
             suffix);
    const char *env_ar = getenv("ZAN_LLVM_AR");
    if (!env_ar || !*env_ar) env_ar = getenv("LLVM_AR");
    const char *tools[] = {
        env_ar ? env_ar : "",
        bundled, "llvm-ar", versioned,
#ifdef __APPLE__
        "/opt/homebrew/opt/llvm/bin/llvm-ar",
        "/usr/local/opt/llvm/bin/llvm-ar",
#endif
        NULL
    };
    const char *format = (target_os == ZAN_OS_MACOS || target_os == ZAN_OS_IOS)
                             ? "--format=darwin" : "--format=gnu";
    size_t tmp_size = strlen(obj) + sizeof(".a");
    char *tmp = malloc(tmp_size);
    if (!tmp) return -1;
    snprintf(tmp, tmp_size, "%s.a", obj);
    if (remove(tmp) != 0 && errno != ENOENT) {
        fprintf(stderr, "error: cannot replace archive temporary '%s': %s\n",
                tmp, strerror(errno));
        free(tmp);
        return -1;
    }
    int rc = -1;
    for (int i = 0; tools[i]; i++) {
        if (!tools[i][0]) continue;
        rc = zan_run_archiver(tools[i], format, tmp, obj);
        if (rc >= 0) break;
    }
    if (rc < 0)
        fprintf(stderr,
                "error: static library emission requires llvm-ar; install LLVM "
                "on PATH or place llvm-ar beside zanc\n");
    if (rc == 0) {
#ifdef _WIN32
        wchar_t *wide_tmp = zan_utf8_to_wide_alloc(tmp);
        wchar_t *wide_arc = zan_utf8_to_wide_alloc(arc);
        rc = wide_tmp && wide_arc &&
             MoveFileExW(wide_tmp, wide_arc, MOVEFILE_REPLACE_EXISTING) ? 0 : -1;
        free(wide_tmp);
        free(wide_arc);
#else
        rc = rename(tmp, arc);
#endif
    }
    remove(tmp);
    free(tmp);
    return rc == 0 ? 0 : -1;
}


#include "driver/main_options.c"


int main(int argc, char **argv) {
#ifdef _WIN32
    char **utf8_argv = zan_utf8_command_line_argv(&argc);
    if (utf8_argv) argv = utf8_argv;
#endif
    {
        const char *cl[] = { "zanc", "--wasm-enable-eh", NULL };
        LLVMParseCommandLineOptions(2, cl, "");
    }
    expand_arg_files(&argc, &argv);
        zan_options_t opts;
    zan_options_status_t opt_st = zan_options_parse(argc, argv, &opts);
    if (opt_st != ZAN_OPTIONS_OK) {
        return opts.exit_code;
    }

    const char *input_file = opts.input_file;
    const char **input_files = opts.input_files;
    int input_count = opts.input_count;
    int input_cap = opts.input_cap;
    int explicit_input_count = opts.explicit_input_count;
    const char *output_file = opts.output_file;
    bool do_dump_tokens = opts.do_dump_tokens;
    bool do_dump_ast = opts.do_dump_ast;
    const char *emit_symbols_path = opts.emit_symbols_path;
    const char *gen_meta_path = opts.gen_meta_path;
    bool do_emit_ir = opts.do_emit_ir;
    int check_leaks_opt = opts.check_leaks_opt;
    bool checked_arith = opts.checked_arith;
    int arc_guard_opt = opts.arc_guard_opt;
    int runtime_checks_opt = opts.runtime_checks_opt;
    bool runtime_checks = opts.runtime_checks;
    bool strict_runtime = opts.strict_runtime;
    bool publish_mode = opts.publish_mode;
    int obfuscate_strings_opt = opts.obfuscate_strings_opt;
    bool hide_diagnostics = opts.hide_diagnostics;
    const char *site_map_path = opts.site_map_path;
    bool strip_symbols = opts.strip_symbols;
    int error_limit = opts.error_limit;
    bool debug_info = opts.debug_info;
    int fast_alloc_opt = opts.fast_alloc_opt;
    const char *stdlib_path = opts.stdlib_path;
    bool auto_stdlib = opts.auto_stdlib;
    bool packages_disabled = opts.packages_disabled;
    bool quiet = opts.quiet;
    const char *package_project = opts.package_project;
    bool package_list_missing = opts.package_list_missing;
    bool do_deny_warnings = opts.do_deny_warnings;
    const char *warn_level_arg = opts.warn_level_arg;
    const char *suppress_arg = opts.suppress_arg;
    int opt_level = opts.opt_level;
    const char **pp_defines = (const char **)opts.pp_defines;
    int pp_define_count = opts.pp_define_count;
    const char *target_name = opts.target_name;
    bool link_static_drivers = opts.link_static_drivers;
    const char *driver_dir_override = opts.driver_dir_override;
    const char *link_subsystem = opts.link_subsystem;
    const char *icon_path = opts.icon_path;
    bool no_icon = opts.no_icon;
    bool default_icon_explicit = opts.default_icon_explicit;
    bool emit_lib = opts.emit_lib;
    bool lib_shared = opts.lib_shared;
    const char *apk_path = opts.apk_path;
    const char *apk_package = opts.apk_package;
    const char *apk_label = opts.apk_label;
    const char *ipa_path = opts.ipa_path;
    const char *ipa_bundle_id = opts.ipa_bundle_id;
    const char *ipa_name = opts.ipa_name;
    const char **extra_link_inputs = (const char **)opts.extra_link_inputs;
    int extra_link_input_count = opts.extra_link_input_count;
    const char **embed_specs = (const char **)opts.embed_specs;
    int embed_spec_count = opts.embed_spec_count;
    const char **extra_link_libs = (const char **)opts.extra_link_libs;
    int extra_link_lib_count = opts.extra_link_lib_count;
    const char **extra_lib_paths = (const char **)opts.extra_lib_paths;
    int extra_lib_path_count = opts.extra_lib_path_count;
    char resolved_stdlib_root[1024];
    memcpy(resolved_stdlib_root, opts.resolved_stdlib_root, sizeof(resolved_stdlib_root));

    if (input_count == 0) {
        fprintf(stderr, "error: no input file\n");
        return 1;
    }

    for (int fi = 0; fi < input_count; fi++) {
        if (zan_is_html_input(input_files[fi])) {
            fprintf(stderr,
                    "error: '%s': the compile-time HTML channel has been "
                    "removed; build the window with Gui widgets and load "
                    "markup at runtime via Gui.App.LoadHtml\n",
                    input_files[fi]);
            return 1;
        }
    }

    input_file = input_files[0];
    if (package_project)
        snprintf(package_project_root, sizeof(package_project_root), "%s",
                 package_project);
    else if (!packages_disabled)
        resolve_package_project_root(input_file);
    for (int fi = 0; fi < input_count; fi++) {
        if (zan_is_zcomp_path(input_files[fi])) continue;
        size_t nlen = 0;
        char *nsrc = read_file(input_files[fi], &nlen);
        if (!nsrc) continue;
        scan_namespace_tokens(nsrc, nlen);
        free(nsrc);
    }
    if (project_root_has_manifest()) {
        scan_project_namespaces(package_project_root);
    }

    zan_target_t target;
    if (target_name) {
        if (!zan_target_parse(target_name, &target)) {
            fprintf(stderr, "error: target '%s' is not available in this build "
                            "(see --list-targets)\n", target_name);
            return 1;
        }
    } else {
        zan_target_host(&target);
    }
    bool cross_compiling = (target_name != NULL);
    bool external_async_executor = (target.os == ZAN_OS_WINDOWS ||
                                    target.os == ZAN_OS_LINUX ||
                                    target.os == ZAN_OS_MACOS ||
                                    target.os == ZAN_OS_IOS) &&
                                   (target.arch == ZAN_ARCH_X86_64 ||
                                    target.arch == ZAN_ARCH_AARCH64);

    if (auto_stdlib || stdlib_path) {
        char stdlib_root[1024];
        if (stdlib_path) {
            snprintf(stdlib_root, sizeof(stdlib_root), "%s", stdlib_path);
        } else {
#ifdef _WIN32
            char exe_path[1024];
            GetModuleFileNameA(NULL, exe_path, sizeof(exe_path));
            char *last_sep = strrchr(exe_path, '\\');
            if (last_sep) *last_sep = '\0';
            snprintf(stdlib_root, sizeof(stdlib_root), "%s\\..\\stdlib", exe_path);
            {
                DWORD attr = zan_utf8_get_file_attributes(stdlib_root);
                if (attr == INVALID_FILE_ATTRIBUTES || !(attr & FILE_ATTRIBUTE_DIRECTORY)) {
                    snprintf(stdlib_root, sizeof(stdlib_root), "%s\\stdlib", exe_path);
                }
            }
#elif defined(__APPLE__)
            char exe_path[1024];
            uint32_t exe_sz = sizeof(exe_path);
            if (_NSGetExecutablePath(exe_path, &exe_sz) == 0) {
                char *last_sep = strrchr(exe_path, '/');
                if (last_sep) *last_sep = '\0';
                snprintf(stdlib_root, sizeof(stdlib_root), "%s/../stdlib", exe_path);
            } else {
                snprintf(stdlib_root, sizeof(stdlib_root), "stdlib");
            }
#else
            char exe_path[1024];
            ssize_t elen = readlink("/proc/self/exe", exe_path, sizeof(exe_path)-1);
            if (elen > 0) {
                exe_path[elen] = '\0';
                char *last_sep = strrchr(exe_path, '/');
                if (last_sep) *last_sep = '\0';
                snprintf(stdlib_root, sizeof(stdlib_root), "%s/../stdlib", exe_path);
            } else {
                snprintf(stdlib_root, sizeof(stdlib_root), "stdlib");
            }
#endif
        }
    {
        char norm_root[4096];
#ifdef _WIN32
        if (zan_utf8_full_path(stdlib_root, norm_root, sizeof(norm_root)))
            snprintf(stdlib_root, sizeof(stdlib_root), "%s", norm_root);
#else
        if (realpath(stdlib_root, norm_root))
            snprintf(stdlib_root, sizeof(stdlib_root), "%s", norm_root);
#endif
    }
    snprintf(resolved_stdlib_root, sizeof(resolved_stdlib_root), "%s",
             stdlib_root);

#ifdef _WIN32
    {
        DWORD root_attr = zan_utf8_get_file_attributes(stdlib_root);
        if (auto_stdlib && !stdlib_path &&
            (root_attr == INVALID_FILE_ATTRIBUTES ||
             !(root_attr & FILE_ATTRIBUTE_DIRECTORY)))
            fprintf(stderr,
                    "warning: --auto-stdlib: no stdlib directory found next "
                    "to the compiler (tried %s); `using` directives will not "
                    "resolve stdlib namespaces (pass --stdlib-path)\n",
                    stdlib_root);
    }
#endif

        explicit_input_count = input_count;

        pi_filter_active = !emit_symbols_path &&
                           getenv("ZAN_NO_PULLIN_FILTER") == NULL;
        if (pi_filter_active) {
            pi_arena = zan_arena_new();
            pi_target = target;
            pi_pp_defines = pp_defines;
            pi_pp_define_count = pp_define_count;
            pi_publish_mode = publish_mode;
            pi_stdlib_root_buf = resolved_stdlib_root;
        } else {
        int scanned = 0;
        while (scanned < input_count) {
            int round_end = input_count;
            for (int fi = scanned; fi < round_end; fi++) {
                if (zan_is_zcomp_path(input_files[fi])) continue;
                pi_reach_input_dir(input_files[fi]);
                size_t slen3 = 0;
                char *src3 = read_file(input_files[fi], &slen3);
                if (!src3) continue;
                scan_using_tokens(src3, strlen(src3), stdlib_root,
                                  &input_files, &input_count, &input_cap);
                free(src3);
            }
            scanned = round_end;
        }
        }
    }

    if (package_list_missing) {
        fprintf(stderr, "ZANPKG_STATUS action=list-missing status=ok count=%d\n",
                missing_namespace_count);
        return missing_namespace_count ? 3 : 0;
    }

    phase("scan inputs");

    size_t source_len;
    char *source = read_file(input_file, &source_len);
    if (!source) return 1;
    if (do_dump_tokens) {
        dump_tokens(source, source_len, input_file);
        free(source);
        return 0;
    }

#include "driver/main_frontend.c"

#include "driver/main_codegen.c"

#include "driver/main_linker.c"


        phase("link");
        probe_phase_mem("link");

        generated_object_vec_keep_or_remove(&generated_objects, obj_tmp,
                                            link_ret != 0 &&
                                            getenv("ZAN_KEEP_FAILED_OBJ") != NULL);
        if (icon_obj[0]) remove(icon_obj);

        if (link_ret != 0) {
            fprintf(stderr, "error: linking failed\n");
            zan_diag_free_buffers(irgen.diag);
            zan_irgen_destroy(&irgen);
            zan_arena_free(ir_arena);
            zan_arena_free(arena);
            free(source);
            return 1;
        }

        if ((publish_mode || target.os == ZAN_OS_WINDOWS) &&
            used_driver_count > 0) {
            char outdir[1024];
            snprintf(outdir, sizeof(outdir), "%s", obj_path);
            { char *s1 = strrchr(outdir, '/'); char *s2 = strrchr(outdir, '\\');
              char *s = (s1 > s2) ? s1 : s2;
              if (s) *s = '\0'; else snprintf(outdir, sizeof(outdir), "."); }

            bool win_target = (target.os == ZAN_OS_WINDOWS);
            const char *dsub = zan_driver_subdir(&target);
            zan_driver_bundle_context_t bundle_ctx = {
                .reg = &driver_reg, .irgen = &irgen, .prefixes = &driver_prefixes,
                .subdir = dsub, .outdir = outdir, .quiet = quiet
            };
            for (int d = 0; d < used_driver_count; d++) {
                const char *driver_dir = driver_dirs[d];
                if (!driver_dir[0]) continue;
                if (used_driver_static[d] && !used_driver_runtime[d]) continue;
                char drv[64];
                snprintf(drv, sizeof(drv), "%.*s",
                         used_driver_len[d], used_drivers[d]);

                char cands[64][128]; int ncand = 0;
                char manifest[1200];
                snprintf(manifest, sizeof(manifest), "%s/%s.bundle", driver_dir, drv);
                if (getenv("ZAN_DEBUG_LINK")) {
                    fprintf(stderr, "[DEBUG] checking manifest %s\n", manifest);
                    fflush(stderr);
                }
                FILE *mf = fopen(manifest, "rb");
                if (mf) {
                    char line[128];
                    while (fgets(line, sizeof(line), mf)) {
                        if (ncand >= 64) {
                            fclose(mf);
                            link_cap_exceeded("files in a driver bundle "
                                              "manifest", 64);
                        }
                        const char *prefix;
                        char *entry = zan_driver_bundle_entry(line, &prefix);
                        if (!entry || (prefix &&
                            !zan_driver_prefix_live(&irgen, &driver_prefixes, prefix)))
                            continue;
                        {
                            if (strncmp(entry, "@driver/", 8) == 0) {
                                const char *dep = entry + 8;
                                size_t dl = strlen(dep);
                                if (dl == 0 || dl >= 64 ||
                                    !zan_is_safe_bundle_name(dep)) {
                                    fprintf(stderr, "warning: ignoring unsafe "
                                        "@driver entry '%s' in %s (must be a "
                                        "bare driver name)\n", entry, manifest);
                                } else if (ncand < 64) {
                                    snprintf(cands[ncand++], sizeof(cands[0]),
                                             "@driver/%s", dep);
                                }
                            } else if (zan_is_safe_bundle_name(entry)) {
                                snprintf(cands[ncand++], sizeof(cands[0]), "%s", entry);
                            } else {
                                fprintf(stderr, "warning: ignoring unsafe entry "
                                    "'%s' in %s (must be a bare filename)\n",
                                    entry, manifest);
                            }
                        }
                    }
                    fclose(mf);
                } else if (win_target) {
                    snprintf(cands[ncand++], sizeof(cands[0]), "%s.dll", drv);
                    snprintf(cands[ncand++], sizeof(cands[0]), "lib%s.dll", drv);
                } else if (target.os == ZAN_OS_MACOS) {
                    snprintf(cands[ncand++], sizeof(cands[0]), "lib%s.dylib", drv);
                } else {
                    snprintf(cands[ncand++], sizeof(cands[0]), "lib%s.so", drv);
                }

                int copied = 0;
                int dself = 0;
                int copy_failed_count = 0;
                for (int c = 0; c < ncand; c++) {
                    char src[1300], dst[1300];
                    if (getenv("ZAN_DEBUG_LINK")) {
                        fprintf(stderr, "[DEBUG] candidate[%d] = %s\n", c, cands[c]);
                        fflush(stderr);
                    }
                    if (used_driver_embedded[d] &&
                        strcmp(cands[c], embedded_driver_file[d]) == 0) {
                        if (!quiet)
                            printf("  embedded driver '%s' ? %s (inside the "
                                   "executable)\n", drv, cands[c]);
                        copied++;
                        continue;
                    }
                    bool got = false;
                    if (strncmp(cands[c], "@driver/", 8) == 0) {
                        got = zan_bundle_dependency(&bundle_ctx, cands[c] + 8,
                                                     manifest, &copy_failed_count) > 0;
                    } else {
                        snprintf(src, sizeof(src), "%s/%s", driver_dir, cands[c]);
                        snprintf(dst, sizeof(dst), "%s/%s", outdir, cands[c]);
                        if (zan_file_exists(src)) {
                            char errbuf[256];
                            if (zan_copy_file_ex(src, dst, errbuf, sizeof(errbuf)) == 0) {
                                got = true;
                                dself++;
                            } else {
                                fprintf(stderr, "warning: failed to copy driver '%s' file "
                                        "'%s' -> '%s': %s\n", drv, src, dst, errbuf);
                                copy_failed_count++;
                            }
                        }
                    }
                    if (got) {
                        if (!quiet)
                            printf("  bundled driver '%s' ? %s\n", drv, cands[c]);
                        copied++;
                    }
                }
                if (copied == 0 && used_driver_runtime[d]) {
                    if (!quiet)
                        printf("  note: driver '%s' not bundled (%s is empty); the "
                               "program will use a system-installed %s\n",
                               drv, driver_dir, drv);
                } else if (copied == 0 && dself == 0 && !used_driver_runtime[d]) {
                    if (copy_failed_count > 0) {
                        fprintf(stderr,
                            "warning: driver '%s' was not bundled because copy operation(s) failed "
                            "(target file(s) in %s may be locked by another running process, or write permission denied).\n",
                            drv, outdir);
                    } else {
                        fprintf(stderr,
                            "warning: driver '%s' was not bundled (no runtime library "
                            "found in %s). The published program will require '%s' to "
                            "be installed on the target, or add a %s/%s.bundle manifest.\n",
                            drv, driver_dir, drv, driver_dir, drv);
                    }
                }
            }
        }

        if (apk_path) {
            const char *abi = (target.arch == ZAN_ARCH_AARCH64)
                              ? "arm64-v8a" : "x86_64";
            char pkg[256], lbl[256];
            if (apk_package) {
                if (strlen(apk_package) >= sizeof(pkg)) {
                    fprintf(stderr, "error: --apk-package exceeds %zu bytes\n",
                            sizeof(pkg) - 1);
                    remove(obj_path);
                    zan_diag_free_buffers(irgen.diag);
                    zan_irgen_destroy(&irgen);
                    zan_arena_free(ir_arena);
                    zan_arena_free(arena);
                    free(source);
                    return 1;
                }
                snprintf(pkg, sizeof(pkg), "%s", apk_package);
            } else {
              const char *base = strrchr(input_file, '/');
              const char *base2 = strrchr(input_file, '\\');
              if (base2 > base) base = base2;
              base = base ? base + 1 : input_file;
              snprintf(pkg, sizeof(pkg), "dev.zan.%s", base);
              { char *dot = strrchr(pkg, '.');
                if (dot && strcmp(dot, ".zan") == 0) *dot = 0; }
              for (char *c = pkg; *c; c++) {
                  if (!((*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z')
                        || (*c >= '0' && *c <= '9') || *c == '_' || *c == '.'))
                      *c = '_';
              }
            }
            if (apk_label) {
                if (strlen(apk_label) >= sizeof(lbl)) {
                    fprintf(stderr, "error: --apk-label exceeds %zu bytes\n",
                            sizeof(lbl) - 1);
                    remove(obj_path);
                    zan_diag_free_buffers(irgen.diag);
                    zan_irgen_destroy(&irgen);
                    zan_arena_free(ir_arena);
                    zan_arena_free(arena);
                    free(source);
                    return 1;
                }
                snprintf(lbl, sizeof(lbl), "%s", apk_label);
            } else {
              const char *base = strrchr(input_file, '/');
              const char *base2 = strrchr(input_file, '\\');
              if (base2 > base) base = base2;
              base = base ? base + 1 : input_file;
              snprintf(lbl, sizeof(lbl), "%s", base);
              { char *d2 = strrchr(lbl, '.');
                if (d2 && strcmp(d2, ".zan") == 0) *d2 = 0; }
            }
            char *extras[256]; int nextra = 0;
            { char outdir_a[1024]; snprintf(outdir_a, sizeof(outdir_a), "%s", obj_path);
              { char *s1 = strrchr(outdir_a, '/'); char *s2 = strrchr(outdir_a, '\\');
                char *s = (s1 > s2) ? s1 : s2; if (s) *s = 0; }
              for (int d = 0; d < used_driver_count && nextra < 256; d++) {
                  const char *dd = driver_dirs[d];
                  if (!dd[0]) continue;
                  char drv[64];
                  snprintf(drv, sizeof(drv), "%.*s", used_driver_len[d], used_drivers[d]);
                  char manifest_p[1300];
                  snprintf(manifest_p, sizeof(manifest_p), "%s/%s.bundle", dd, drv);
                  FILE *mf = fopen(manifest_p, "rb");
                  if (!mf) continue;
                  char line[128];
                  while (fgets(line, sizeof(line), mf) && nextra < 256) {
                      size_t l = strlen(line);
                      while (l > 0 && (line[l-1]=='\n'||line[l-1]=='\r'||line[l-1]==' '||line[l-1]=='\t')) line[--l]=0;
                      if (l == 0) continue;
                      if (!strstr(line, ".so")) continue;
                      static char names[256][128];
                      snprintf(names[nextra], sizeof(names[0]), "%s/%s", dd, line);
                      extras[nextra] = names[nextra];
                      nextra++;
                  }
                  fclose(mf);
              }
            }
            char shell_dir[1300];
            { char exe_dir_a[1024] = {0};
#ifdef _WIN32
              { char mod[1024]; DWORD n = GetModuleFileNameA(NULL, mod, sizeof(mod));
                if (n > 0 && n < sizeof(mod)) { char *s = strrchr(mod, '\\'); if (s) *s = 0; snprintf(exe_dir_a, sizeof(exe_dir_a), "%s", mod); } }
#elif defined(__APPLE__)
              { uint32_t sz = sizeof(exe_dir_a); if (_NSGetExecutablePath(exe_dir_a, &sz) == 0) { char *s = strrchr(exe_dir_a, '/'); if (s) *s = 0; } }
#else
              { ssize_t n = readlink("/proc/self/exe", exe_dir_a, sizeof(exe_dir_a) - 1);
                if (n > 0) { exe_dir_a[n] = 0; char *s = strrchr(exe_dir_a, '/'); if (s) *s = 0; } }
#endif
              snprintf(shell_dir, sizeof(shell_dir), "%s/apk-shell", exe_dir_a);
            }
            if (!quiet)
                printf("  packaging APK ? %s\n", apk_path);
            if (zan_apk_build(apk_path, obj_path, abi, pkg, lbl, shell_dir,
                              extras, nextra,
                              proj_android_perm_count, proj_android_perms) != 0) {
                remove(obj_path);
                zan_diag_free_buffers(irgen.diag);
                zan_irgen_destroy(&irgen);
                zan_arena_free(ir_arena);
                zan_arena_free(arena);
                free(source);
                return 1;
            }
            remove(obj_path);
        }

        if (ipa_path) {
            const char *app_name = ipa_name;
            char def_name[256];
            if (!app_name) {
                const char *base = zan_path_basename(ipa_path);
                if (!base || !base[0] || strcmp(base, "out.exe") == 0 || strcmp(base, "main") == 0) {
                    base = zan_path_basename(input_file);
                }
                size_t blen = strlen(base);
                if (blen > 4 && (strcmp(base + blen - 4, ".ipa") == 0 || strcmp(base + blen - 4, ".exe") == 0 || strcmp(base + blen - 4, ".zan") == 0)) {
                    snprintf(def_name, sizeof(def_name), "%.*s", (int)(blen - 4), base);
                } else {
                    snprintf(def_name, sizeof(def_name), "%s", base);
                }
                app_name = def_name;
            }
            char def_bundle_id[512];
            const char *bundle_id = ipa_bundle_id;
            if (!bundle_id) {
                snprintf(def_bundle_id, sizeof(def_bundle_id), "dev.zan.%s", app_name);
                bundle_id = def_bundle_id;
            }
            const char *effective_icon = icon_path;
            if (!effective_icon) {
                const char *candidates[] = {
                    "assets/icon.png",
                    "assets/icon.ico",
                    "icon.png",
                    "icon.ico",
                    NULL
                };
                for (int ci = 0; candidates[ci]; ci++) {
                    FILE *ft = fopen(candidates[ci], "rb");
                    if (ft) {
                        fclose(ft);
                        effective_icon = candidates[ci];
                        break;
                    }
                }
            }
            const char *effective_assets = NULL;
            char assets_cand[1200] = {0};
            if (input_file) {
                char adir[1200];
                snprintf(adir, sizeof(adir), "%s", input_file);
                char *s1 = strrchr(adir, '/');
                char *s2 = strrchr(adir, '\\');
                if (!s1 || (s2 && s2 > s1)) s1 = s2;
                if (s1) {
                    *s1 = 0;
                    snprintf(assets_cand, sizeof(assets_cand), "%s/assets", adir);
                    if (zan_file_exists(assets_cand)) {
                        effective_assets = assets_cand;
                    } else {
                        char *p1 = strrchr(adir, '/');
                        char *p2 = strrchr(adir, '\\');
                        if (!p1 || (p2 && p2 > p1)) p1 = p2;
                        if (p1) {
                            *p1 = 0;
                            snprintf(assets_cand, sizeof(assets_cand), "%s/assets", adir);
                            if (zan_file_exists(assets_cand)) effective_assets = assets_cand;
                        }
                    }
                }
            }
            if (!effective_assets && zan_file_exists("assets")) {
                effective_assets = "assets";
            }
            if (zan_ipa_build(ipa_path, obj_path, app_name, bundle_id, app_name, "1.0.0", effective_icon, effective_assets) != 0) {
                remove(obj_path);
                zan_diag_free_buffers(irgen.diag);
                zan_irgen_destroy(&irgen);
                zan_arena_free(ir_arena);
                zan_arena_free(arena);
                free(source);
                return 1;
            }
            remove(obj_path);
        }

        if (!quiet) {
            const char *final_out = ipa_path ? ipa_path : (apk_path ? apk_path : obj_path);
            if (input_count == 1) {
                printf("%s '%s' ? '%s'\n", publish_mode ? "Published" : "Compiled", input_file, final_out);
            } else {
                printf("%s %d files ? '%s'\n", publish_mode ? "Published" : "Compiled", input_count, final_out);
            }
        }
        fflush(stdout);
        fflush(stderr);
    }

    zan_diag_free_buffers(irgen.diag);

    zan_irgen_destroy(&irgen);
    zan_arena_free(ir_arena);
    zan_arena_free(arena);
    free(source);
#ifdef _WIN32
    fflush(stdout);
    fflush(stderr);
    ExitProcess(0);
#else
    LLVMShutdown();
    return 0;
#endif
}

