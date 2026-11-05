#ifndef ZAN_MAIN_OPTIONS_PARSE_H
#define ZAN_MAIN_OPTIONS_PARSE_H

#include "zan.h"
#include "package.h"
#include "optimizer.h"
#include "zan_version.h"
#include <stdbool.h>
#include <stddef.h>

#define ZAN_MAX_PP_DEFINES 1024
#define ZAN_MAX_LINK_INPUTS 1024
#define ZAN_MAX_EMBED_SPECS 1024
#define ZAN_LINK_MAX_LIBS 2048
#define ZAN_LINK_MAX_DIRS 1024

typedef enum {
    ZAN_OPTIONS_OK = 0,       /* Continue compiler execution */
    ZAN_OPTIONS_EXIT = 1,     /* Handled an informational / action command cleanly */
    ZAN_OPTIONS_ERROR = 2     /* Option error occurred, error message already printed */
} zan_options_status_t;

typedef struct zan_options {
    const char *input_file;
    const char **input_files;
    int input_count;
    int input_cap;
    int explicit_input_count;
    const char *output_file;
    bool do_dump_tokens;
    bool do_dump_ast;
    const char *emit_symbols_path;
    const char *gen_meta_path;
    bool do_emit_ir;
    int check_leaks_opt;
    bool checked_arith;
    int arc_guard_opt;
    int runtime_checks_opt;
    bool runtime_checks;
    bool strict_runtime;
    bool publish_mode;
    int obfuscate_strings_opt;
    bool hide_diagnostics;
    const char *site_map_path;
    bool strip_symbols;
    int error_limit;
    bool debug_info;
    int fast_alloc_opt;
    const char *stdlib_path;
    bool auto_stdlib;
    bool packages_disabled;
    bool quiet;
    const char *package_api;
    const char *package_install_dir;
    const char *package_name;
    const char *package_project;
    bool package_list_missing;
    bool do_deny_warnings;
    const char *warn_level_arg;
    const char *suppress_arg;
    zan_pkg_scope_t package_scope;
    int opt_level;
    const char *pp_defines[ZAN_MAX_PP_DEFINES];
    int pp_define_count;
    const char *target_name;
    bool link_static_drivers;
    const char *driver_dir_override;
    const char *link_subsystem;
    const char *icon_path;
    bool no_icon;
    bool default_icon_explicit;
    bool emit_lib;
    bool lib_shared;
    const char *apk_path;
    const char *apk_package;
    const char *apk_label;
    const char *ipa_path;
    const char *ipa_bundle_id;
    const char *ipa_name;
    const char *extra_link_inputs[ZAN_MAX_LINK_INPUTS];
    int extra_link_input_count;
    const char *embed_specs[ZAN_MAX_EMBED_SPECS];
    int embed_spec_count;
    const char *extra_link_libs[ZAN_LINK_MAX_LIBS];
    int extra_link_lib_count;
    const char *extra_lib_paths[ZAN_LINK_MAX_DIRS];
    int extra_lib_path_count;
    char resolved_stdlib_root[1024];

    /* Exit code when status != ZAN_OPTIONS_OK */
    int exit_code;
} zan_options_t;

void zan_options_init(zan_options_t *opts);
zan_options_status_t zan_options_parse(int argc, char **argv, zan_options_t *opts);

/* Shared variables from main.c */
extern char package_project_root[1024];
extern bool g_time_phases;
extern double g_phase_start;
double now_ms(void);

/* Shared helpers defined in main.c and main_options.c */
void print_usage(void);
bool zan_link_path_is_safe(const char *v);
void input_files_push(const char ***files, int *count, int *cap, const char *path);
bool zan_path_is_directory(const char *path);
void zan_collect_dir_sources(const char *dir, const char ***files, int *count, int *cap);
int zan_path_cmp(const void *a, const void *b);
bool zan_dllimport_name_is_safe(const char *name, int len);
int zan_query_api(const char *symbol_query);

#endif /* ZAN_MAIN_OPTIONS_PARSE_H */
