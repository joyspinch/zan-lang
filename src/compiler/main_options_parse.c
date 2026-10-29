/* main_options_parse.c - 编译器 CLI 选项声明与命令行解析循环 */

    const char *input_file = NULL;
    const char **input_files = NULL;
    int input_count = 0;
    int input_cap = 0;
    int explicit_input_count = 0;
    const char *output_file = NULL;
    bool do_dump_tokens = false;
    bool do_dump_ast = false;
    const char *emit_symbols_path = NULL; /* --emit-symbols <file> */
    const char *gen_meta_path = NULL;
    bool do_emit_ir = false;
    int check_leaks_opt = -1;
    int arc_guard_opt = -1;
    int runtime_checks_opt = -1;
    bool runtime_checks = true;
    bool strict_runtime = false;
    bool publish_mode = false;
    int obfuscate_strings_opt = -1;
    bool hide_diagnostics = false;
    const char *site_map_path = NULL;
    bool strip_symbols = false;
    int error_limit = -1;
    bool debug_info = false;
    /* 小对象内存分配器管理池 */
    int fast_alloc_opt = 0;
    const char *stdlib_path = NULL;
    bool auto_stdlib = true;
    bool packages_disabled = false;
    bool quiet = false;
    const char *package_api = NULL;
    const char *package_install_dir = NULL;
    const char *package_name = NULL;
    const char *package_project = NULL;
    bool package_list_missing = false;
    bool do_deny_warnings = false;
    const char *warn_level_arg = NULL;   /* A382② --warn-level */
    const char *suppress_arg = NULL;     /* A382② --suppress */
    zan_pkg_scope_t package_scope = ZAN_PKG_SCOPE_PROJECT;
    int opt_level = -1;
#define ZAN_MAX_PP_DEFINES 1024
#define ZAN_MAX_LINK_INPUTS 1024
#define ZAN_MAX_EMBED_SPECS 1024
    const char *pp_defines[ZAN_MAX_PP_DEFINES];
    int pp_define_count = 0;
    const char *target_name = NULL; /* --target <name|triple>; NULL = host */
    bool link_static_drivers = false;
    const char *driver_dir_override = NULL; /* --driver-dir */
    const char *link_subsystem = NULL;
    const char *icon_path = NULL;
    bool no_icon = false;
    bool default_icon_explicit = false;
    bool emit_lib = false;
    bool lib_shared = false;
    const char *apk_path = NULL;
    const char *apk_package = NULL;
    const char *apk_label = NULL;
    const char *ipa_path = NULL;
    const char *ipa_bundle_id = NULL;
    const char *ipa_name = NULL;
    const char *extra_link_inputs[ZAN_MAX_LINK_INPUTS]; int extra_link_input_count = 0;
    const char *embed_specs[ZAN_MAX_EMBED_SPECS]; int embed_spec_count = 0;
    const char *extra_link_libs[ZAN_LINK_MAX_LIBS]; int extra_link_lib_count = 0;
    const char *extra_lib_paths[ZAN_LINK_MAX_DIRS]; int extra_lib_path_count = 0;
    char resolved_stdlib_root[1024] = {0};
    char **design_outs = NULL;
    size_t design_count = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--dump-tokens") == 0) {
            do_dump_tokens = true;
        } else if (strcmp(argv[i], "--dump-ast") == 0) {
            do_dump_ast = true;
        } else if (strcmp(argv[i], "--emit-symbols") == 0 && i + 1 < argc) {
            emit_symbols_path = argv[++i];
        } else if (strcmp(argv[i], "--no-gen") == 0) {
            zan_gen_enabled = 0;
        } else if (strcmp(argv[i], "--quiet") == 0 || strcmp(argv[i], "-q") == 0) {
            quiet = true;
        } else if (strcmp(argv[i], "--gen-meta") == 0 && i + 1 < argc) {
            gen_meta_path = argv[++i];
        } else if (strcmp(argv[i], "--emit-ir") == 0) {
            do_emit_ir = true;
        } else if (strcmp(argv[i], "--check-leaks") == 0) {
            check_leaks_opt = 1;
        } else if (strcmp(argv[i], "--no-check-leaks") == 0) {
            check_leaks_opt = 0;
        } else if (strcmp(argv[i], "--arc-guard") == 0) {
            arc_guard_opt = 1;
        } else if (strcmp(argv[i], "--no-arc-guard") == 0) {
            arc_guard_opt = 0;
        } else if (strcmp(argv[i], "--no-runtime-checks") == 0) {
            runtime_checks_opt = 0;
            runtime_checks = false;
        } else if (strcmp(argv[i], "--runtime-checks") == 0) {
            runtime_checks_opt = 1;
            runtime_checks = true;
        } else if (strcmp(argv[i], "--strict-runtime") == 0) {
            strict_runtime = true;
        } else if (strcmp(argv[i], "--deny-warnings") == 0) {
            do_deny_warnings = true;
        } else if (strncmp(argv[i], "--warn-level=", 13) == 0) {
            warn_level_arg = argv[i] + 13;
        } else if (strcmp(argv[i], "--warn-level") == 0 && i + 1 < argc) {
            warn_level_arg = argv[++i];
        } else if (strncmp(argv[i], "--suppress=", 11) == 0) {
            suppress_arg = argv[i] + 11;
        } else if (strcmp(argv[i], "--suppress") == 0 && i + 1 < argc) {
            suppress_arg = argv[++i];
        } else if (strcmp(argv[i], "--publish") == 0) {
            publish_mode = true;
        } else if (strcmp(argv[i], "--obfuscate-strings") == 0) {
            obfuscate_strings_opt = 1;
        } else if (strcmp(argv[i], "--no-obfuscate-strings") == 0) {
            obfuscate_strings_opt = 0;
        } else if (strcmp(argv[i], "--hide-diagnostics") == 0) {
            hide_diagnostics = true;
        } else if (strcmp(argv[i], "--strip-symbols") == 0) {
            strip_symbols = true;
        } else if (strcmp(argv[i], "--emit-site-map") == 0 && i + 1 < argc) {
            site_map_path = argv[++i];
        } else if (strncmp(argv[i], "--emit-site-map=", 16) == 0) {
            site_map_path = argv[i] + 16;
        } else if (strcmp(argv[i], "-g") == 0 || strcmp(argv[i], "--debug") == 0) {
            debug_info = true;
        } else if (strcmp(argv[i], "--fast-alloc") == 0) {
            fast_alloc_opt = 1;
        } else if (strcmp(argv[i], "--no-fast-alloc") == 0) {
            fast_alloc_opt = -1;
        } else if (strcmp(argv[i], "--link-mode") == 0 && i + 1 < argc) {
            const char *m = argv[++i];
            if (strcmp(m, "static") == 0) link_static_drivers = true;
            else if (strcmp(m, "shared") == 0) link_static_drivers = false;
            else { fprintf(stderr, "error: --link-mode must be 'static' or 'shared'\n"); return 1; }
        } else if (strcmp(argv[i], "--driver-dir") == 0 && i + 1 < argc) {
            driver_dir_override = argv[++i];
        } else if (strcmp(argv[i], "--target") == 0 && i + 1 < argc) {
            target_name = argv[++i];
        } else if (strcmp(argv[i], "--list-targets") == 0) {
            const zan_target_info_t *ts;
            int nt = zan_target_list(&ts);
            printf("Available cross-compilation targets:\n");
            for (int t = 0; t < nt; t++)
                printf("  %-12s %-30s %s\n", ts[t].name, ts[t].triple, ts[t].desc);
            return 0;
        } else if (strcmp(argv[i], "--api") == 0 && i + 1 < argc) {
            return zan_query_api(argv[++i]);
        } else if (strcmp(argv[i], "--stdlib-path") == 0 && i + 1 < argc) {
            stdlib_path = argv[++i];
        } else if (strcmp(argv[i], "--auto-stdlib") == 0) {
            auto_stdlib = true;
        } else if (strcmp(argv[i], "--no-stdlib") == 0) {
            auto_stdlib = false;
        } else if (strcmp(argv[i], "--no-packages") == 0) {
            packages_disabled = true;
            package_project_root[0] = '\0';
        } else if (strcmp(argv[i], "--package-api") == 0 && i + 1 < argc) {
            package_api = argv[++i];
        } else if (strcmp(argv[i], "--package-list-missing") == 0) {
            package_list_missing = true;
        } else if (strcmp(argv[i], "--package-install") == 0 && i + 1 < argc) {
            package_install_dir = argv[++i];
        } else if (strcmp(argv[i], "--package-name") == 0 && i + 1 < argc) {
            package_name = argv[++i];
        } else if (strcmp(argv[i], "--package-project") == 0 && i + 1 < argc) {
            package_project = argv[++i];
        } else if (strcmp(argv[i], "--package-scope") == 0 && i + 1 < argc) {
            const char *scope = argv[++i];
            if (strcmp(scope, "project") == 0) package_scope = ZAN_PKG_SCOPE_PROJECT;
            else if (strcmp(scope, "global") == 0) package_scope = ZAN_PKG_SCOPE_GLOBAL;
            else { fprintf(stderr, "ZANPKG_STATUS action=install status=invalid_scope\n"); return 1; }
        } else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            output_file = argv[++i];
        } else if (strcmp(argv[i], "-O0") == 0) {
            opt_level = 0;
        } else if (strcmp(argv[i], "-O1") == 0) {
            opt_level = 1;
        } else if (strcmp(argv[i], "-O2") == 0) {
            opt_level = 2;
        } else if (strcmp(argv[i], "-O3") == 0) {
            opt_level = ZAN_OPT_AGGRESSIVE;
        } else if (strcmp(argv[i], "-Os") == 0) {
            opt_level = ZAN_OPT_SIZE;
        } else if (strcmp(argv[i], "-Oz") == 0) {
            opt_level = ZAN_OPT_SIZE_MIN;
        } else if (strncmp(argv[i], "-ferror-limit=", 14) == 0) {
            error_limit = atoi(argv[i] + 14);
        } else if (strcmp(argv[i], "--subsystem") == 0 && i + 1 < argc) {
            link_subsystem = argv[++i];
        } else if (strcmp(argv[i], "--time") == 0) {
            g_time_phases = true;
            g_phase_start = now_ms();
        } else if (strcmp(argv[i], "--emit-lib") == 0) {
            emit_lib = true;
        } else if (strcmp(argv[i], "--emit-apk") == 0 && i + 1 < argc) {
            emit_lib = true;
            apk_path = argv[++i];
        } else if (strcmp(argv[i], "--apk-package") == 0 && i + 1 < argc) {
            apk_package = argv[++i];
        } else if (strcmp(argv[i], "--apk-label") == 0 && i + 1 < argc) {
            apk_label = argv[++i];
        } else if (strcmp(argv[i], "--emit-ipa") == 0 && i + 1 < argc) {
            ipa_path = argv[++i];
        } else if (strcmp(argv[i], "--ipa-bundle-id") == 0 && i + 1 < argc) {
            ipa_bundle_id = argv[++i];
        } else if (strcmp(argv[i], "--ipa-name") == 0 && i + 1 < argc) {
            ipa_name = argv[++i];
        } else if (strcmp(argv[i], "--no-icon") == 0) {
            no_icon = true;
        } else if (strcmp(argv[i], "--default-icon") == 0) {
            default_icon_explicit = true;
        } else if (strcmp(argv[i], "--icon") == 0 && i + 1 < argc) {
            icon_path = argv[++i];
        } else if (strcmp(argv[i], "--embed") == 0 && i + 1 < argc) {
            if (embed_spec_count < ZAN_MAX_EMBED_SPECS) embed_specs[embed_spec_count++] = argv[++i];
            else { fprintf(stderr, "error: too many --embed (max %d)\n", ZAN_MAX_EMBED_SPECS); return 1; }
        } else if (strcmp(argv[i], "--link-input") == 0 && i + 1 < argc) {
            if (!zan_link_path_is_safe(argv[i + 1])) {
                fprintf(stderr,
                        "error: --link-input value may not contain double"
                        " quotes or control characters\n");
                return 1;
            }
            if (extra_link_input_count < ZAN_MAX_LINK_INPUTS) extra_link_inputs[extra_link_input_count++] = argv[++i];
            else { fprintf(stderr, "error: too many --link-input (max %d)\n", ZAN_MAX_LINK_INPUTS); return 1; }
        } else if (strcmp(argv[i], "--link-lib") == 0 && i + 1 < argc) {
            /* 该值会被原样拼进链接命令行（-l<lib>）后经 system() 执行：
             * 引号/空格/&/| 等元字符就是命令注入面（审计 P1-9）。库名
             * 本来就只能是标识符形态——与 DllImport 同一字符集白名单，
             * 越界一律拒绝；要传路径用 --libpath / -L。 */
            if (!zan_dllimport_name_is_safe(argv[i + 1], (int)strlen(argv[i + 1]))) {
                fprintf(stderr,
                        "error: --link-lib value '%s' may only contain "
                        "[A-Za-z0-9._+-] (it is passed to the linker as "
                        "-l<name>; use --libpath for search paths)\n",
                        argv[i + 1]);
                return 1;
            }
            if (extra_link_lib_count < ZAN_LINK_MAX_LIBS)
                extra_link_libs[extra_link_lib_count++] = argv[++i];
            else { fprintf(stderr, "error: too many --link-lib (max %d)\n",
                           ZAN_LINK_MAX_LIBS); return 1; }
        } else if (strcmp(argv[i], "--libpath") == 0 && i + 1 < argc) {
            if (!zan_link_path_is_safe(argv[i + 1])) {
                fprintf(stderr,
                        "error: --libpath value may not contain double"
                        " quotes or control characters\n");
                return 1;
            }
            if (extra_lib_path_count < ZAN_LINK_MAX_DIRS)
                extra_lib_paths[extra_lib_path_count++] = argv[++i];
            else { fprintf(stderr, "error: too many --libpath (max %d)\n",
                           ZAN_LINK_MAX_DIRS); return 1; }
        } else if (strncmp(argv[i], "-L", 2) == 0 && argv[i][2] != '\0') {
            if (!zan_link_path_is_safe(argv[i] + 2)) {
                fprintf(stderr,
                        "error: -L value may not contain double quotes"
                        " or control characters\n");
                return 1;
            }
            if (extra_lib_path_count < ZAN_LINK_MAX_DIRS)
                extra_lib_paths[extra_lib_path_count++] = argv[i] + 2;
            else { fprintf(stderr, "error: too many -L dirs (max %d)\n",
                           ZAN_LINK_MAX_DIRS); return 1; }
        } else if (strncmp(argv[i], "-D", 2) == 0 && argv[i][2] != '\0') {
            if (pp_define_count < ZAN_MAX_PP_DEFINES) pp_defines[pp_define_count++] = argv[i] + 2;
            else { fprintf(stderr, "error: too many -D defines (max %d)\n", ZAN_MAX_PP_DEFINES);
                   return 1; }
        } else if (argv[i][0] != '-') {
            /* 位置参数可以是源文件，也可以是目录：目录递归展开成 .zan 列表。
               命令行长度因此与项目规模解耦（见 zan_collect_dir_sources）。 */
            if (zan_path_is_directory(argv[i])) {
                int before = input_count;
                zan_collect_dir_sources(argv[i], &input_files, &input_count,
                                        &input_cap);
                if (input_count == before) {
                    fprintf(stderr,
                            "error: no .zan source files under '%s'\n", argv[i]);
                    return 1;
                }
                qsort(input_files + before, (size_t)(input_count - before),
                      sizeof(*input_files), zan_path_cmp);
            } else {
                input_files_push(&input_files, &input_count, &input_cap, argv[i]);
            }
        } else {
            fprintf(stderr, "unknown option: %s\n", argv[i]);
            return 1;
        }
    }

    if (package_api) {
        char status[256];
        if (!zan_pkg_api_validate(package_api, status, sizeof(status))) {
            fprintf(stderr, "%s\n", status); return 1;
        }
        if (!package_install_dir) {
            fprintf(stderr, "%s\n", status); return 2;
        }
    }
    if (package_install_dir) {
        char status[512];
        if (!package_name) {
            fprintf(stderr, "ZANPKG_STATUS action=install status=missing_name\n"); return 1;
        }
        const char *install_project = package_project ? package_project : ".";
        zan_package_t package_manifest;
        char manifest_path[1024];
        snprintf(manifest_path, sizeof(manifest_path), "%s/zan.pkg", package_install_dir);
        bool manifest_ok = zan_pkg_load(&package_manifest, manifest_path);
        if (!manifest_ok) { memset(&package_manifest, 0, sizeof(package_manifest)); }
        bool ok = zan_pkg_install_local(package_install_dir, package_name,
                                        package_scope, install_project,
                                        status, sizeof(status));
        fprintf(stderr, "%s\n", status);
        if (ok && package_manifest.plugin_id[0])
            fprintf(stderr, "ZANPKG_USAGE plugin_id=%s package=%s scope=%s\n",
                    package_manifest.plugin_id, package_name,
                    package_scope == ZAN_PKG_SCOPE_GLOBAL ? "global" : "project");
        if (manifest_ok) { zan_pkg_destroy(&package_manifest); }
        return ok ? 0 : 1;
    }

