    /* parse */
    zan_arena_t *arena = zan_arena_new();
    g_main_arena = arena;
    zan_diag_t *diag = zan_diag_new(arena);
    zan_diag_set_deny_warnings(diag, do_deny_warnings);
    /* A382②：--warn-level none|default|error（error 兼容 --deny-warnings）；
     * --suppress 逗号分隔诊断编号（W0003 / 3 皆可，字母仅作文档）。 */
    {
        int warn_level = 1;
        if (warn_level_arg) {
            if (strcmp(warn_level_arg, "none") == 0) warn_level = 0;
            else if (strcmp(warn_level_arg, "default") == 0) warn_level = 1;
            else if (strcmp(warn_level_arg, "error") == 0) {
                warn_level = 2;
                do_deny_warnings = true;
                zan_diag_set_deny_warnings(diag, true);
            } else {
                fprintf(stderr,
                        "error: unknown --warn-level '%s' (none|default|error)\n",
                        warn_level_arg);
                return 2;
            }
        }
        zan_diag_set_warn_level(diag, warn_level);
        if (suppress_arg) {
            const char *p = suppress_arg;
            while (*p) {
                while (*p == ',' || *p == ' ') p++;
                if (!*p) break;
                const char *tok = p;
                while (*p && *p != ',' && *p != ' ') p++;
                int code = 0;
                for (const char *q = tok; q < p; q++) {
                    if (*q >= '0' && *q <= '9') code = code * 10 + (*q - '0');
                }
                if (code > 0) zan_diag_suppress_code(diag, code);
            }
        }
    }
    if (error_limit >= 0) zan_diag_set_max_errors(diag, error_limit);

    zan_ast_node_t *ast = NULL;
    int scanned = 0;
    for (;;) {
        int round_end = input_count;
        zan_compile_trace("pullin round begin: scanned=%d inputs=%d", scanned, input_count);
        double __rt0 = now_ms();
    for (int fi = scanned; fi < round_end; fi++) {
        if (fi > 0 && zan_is_zcomp_path(input_files[fi])) continue;
        size_t slen = 0;
        char *src = (fi == 0) ? source : read_file_arena(arena, input_files[fi], &slen);
        if (fi == 0) slen = source_len;
        if (!src) {
            fprintf(stderr, "error: cannot read '%s'\n", input_files[fi]);
            zan_arena_free(arena);
            free(source);
            return 1;
        }
        zan_diag_add_file(diag, input_files[fi], src);

        zan_lexer_t lex;
        zan_lexer_init(&lex, src, slen, fi, arena, diag);

        zan_apply_lex_defines(&lex, target, pp_defines, pp_define_count,
                              publish_mode);

        zan_parser_t parser;
        zan_parser_init(&parser, &lex, arena, diag);

        int errors_before = diag->error_count;
        zan_ast_node_t *unit = zan_parser_parse(&parser);
        g_scale_stats.real_parses++;
        int parse_failed = !unit || diag->error_count > errors_before;
        zan_nsresolve_stamp(unit, arena);
        if (pi_filter_active) {
            int seed_entry = fi < explicit_input_count &&
                             !zan_is_zcomp_path(input_files[fi]);
            pi_seed_stdlib_input =
                seed_entry ? pi_reach_input_dir(input_files[fi]) : 0;
            pi_seeding_stdlib =
                fi > 0 && auto_stdlib && resolved_stdlib_root[0] &&
                input_files[fi] &&
                zan_path_is_under(input_files[fi], resolved_stdlib_root);
            if (parse_failed) {
                g_scale_stats.throwaway_parse_fallbacks++;
                pi_seed_source(src, slen);
            } else {
                pi_seed_parsed_unit(unit, seed_entry);
            }
            pi_seeding_stdlib = 0;
            pi_seed_stdlib_input = 0;
        }
        if (fi > 0 && auto_stdlib && resolved_stdlib_root[0] && input_files[fi] &&
            zan_path_is_under(input_files[fi], resolved_stdlib_root)) {
            for (int k = 0; k < unit->comp_unit.decls.count; k++)
                if (unit->comp_unit.decls.items[k])
                    unit->comp_unit.decls.items[k]->from_stdlib = 1;
        }
        if (input_files[fi] && zan_path_is_package_source(input_files[fi])) {
            for (int k = 0; k < unit->comp_unit.decls.count; k++)
                if (unit->comp_unit.decls.items[k])
                    unit->comp_unit.decls.items[k]->from_package = 1;
        }

        if (!ast) {
            ast = unit;
        } else {
            for (int k = 0; k < unit->comp_unit.usings.count; k++)
                zan_ast_list_push(&ast->comp_unit.usings,
                                  unit->comp_unit.usings.items[k], arena);
            for (int k = 0; k < unit->comp_unit.decls.count; k++)
                zan_ast_list_push(&ast->comp_unit.decls,
                                  unit->comp_unit.decls.items[k], arena);
            if (!ast->comp_unit.ns) ast->comp_unit.ns = unit->comp_unit.ns;
        }
    }
        scanned = round_end;
        if (pi_filter_active) {
            double __ct0 = now_ms();
            int changed = pi_close_once(resolved_stdlib_root);
            int fresh = pi_append_included(&input_files, &input_count,
                                           &input_cap);
            zan_compile_trace("pullin round done: parse=%.1fms close=%.1fms changed=%d fresh=%d inputs=%d",
                              __ct0 - __rt0, now_ms() - __ct0, changed, fresh, input_count);
            if (!changed && !fresh) {
                if (!pi_repair_done && pi_unsatisfied_live_name()) {
                    pi_repair_done = 1;
                    pi_repair_walk(resolved_stdlib_root, "");
                    continue;
                }
                pi_debug_dump();
                break;
            }
        } else if (scanned >= input_count) {
            break;
        }
    }
    phase("parse");
    probe_phase_mem("parse");
    if (g_time_phases) zan_arena_dump_stats();

    if (!zan_diag_has_errors(diag)) {
        zan_compile_trace("flatten nested types");
        zan_parser_flatten_nested_types(ast, arena, diag);
        zan_compile_trace("merge partials");
        zan_parser_merge_partials(ast, arena, diag);
        zan_compile_trace("desugar events");
        zan_parser_desugar_events(ast, arena, diag);
        phase("flatten/merge");
        zan_compile_trace("nsresolve");
        zan_nsresolve_run(ast, arena, diag);
        phase("nsresolve_1");
        zan_compile_trace("specialize generic bases");
        zan_parser_specialize_generic_bases(ast, arena, diag);
        phase("specialize_bases");

        if (gen_meta_path) {
            char *meta = zan_genmeta_export_files(ast, diag);
            if (!meta) {
                fprintf(stderr, "error: cannot export metadata\n");
                zan_arena_free(arena);
                free(source);
                return 1;
            }
            FILE *out = fopen(gen_meta_path, "wb");
            int ok = out &&
                     fwrite(meta, 1, strlen(meta), out) == strlen(meta) &&
                     fclose(out) == 0;
            free(meta);
            if (!ok) {
                fprintf(stderr, "error: cannot write metadata '%s'\n",
                        gen_meta_path);
                zan_arena_free(arena);
                free(source);
                return 1;
            }
            zan_arena_free(arena);
            free(source);
            return 0;
        }

        if (pi_filter_active) {
            for (;;) {
                int changed = pi_close_once(resolved_stdlib_root);
                int fresh = pi_append_included(&input_files, &input_count,
                                               &input_cap);
                if (!changed && !fresh) {
                    if (!pi_repair_done && pi_unsatisfied_live_name()) {
                        pi_repair_done = 1;
                        pi_repair_walk(resolved_stdlib_root, "");
                        continue;
                    }
                    pi_debug_dump();
                    break;
                }
                for (int fi = input_count - fresh;
                     fresh > 0 && fi < input_count; fi++) {
                    int errors_before = diag->error_count;
                    zan_ast_node_t *unit = parse_secondary_unit(
                        input_files[fi], target, pp_defines, pp_define_count,
                        publish_mode, arena, diag);
                    if (!unit) {
                        fprintf(stderr, "error: cannot read '%s'\n",
                                input_files[fi]);
                        zan_arena_free(arena);
                        free(source);
                        return 1;
                    }
                    zan_nsresolve_stamp(unit, arena);
                    pi_seeding_stdlib =
                        auto_stdlib && resolved_stdlib_root[0] &&
                        zan_path_is_under(input_files[fi],
                                          resolved_stdlib_root);
                    if (diag->error_count > errors_before) {
                        g_scale_stats.throwaway_parse_fallbacks++;
                        size_t flen = 0;
                        char *fsrc = read_file(input_files[fi], &flen);
                        if (fsrc) {
                            pi_seed_source(fsrc, flen);
                            free(fsrc);
                        }
                    } else {
                        pi_seed_parsed_unit(unit, 0);
                    }
                    pi_seeding_stdlib = 0;
                    if (auto_stdlib && resolved_stdlib_root[0] &&
                        zan_path_is_under(input_files[fi], resolved_stdlib_root)) {
                        for (int k = 0; k < unit->comp_unit.decls.count; k++)
                            if (unit->comp_unit.decls.items[k])
                                unit->comp_unit.decls.items[k]->from_stdlib = 1;
                    }
                    if (zan_path_is_package_source(input_files[fi])) {
                        for (int k = 0; k < unit->comp_unit.decls.count; k++)
                            if (unit->comp_unit.decls.items[k])
                                unit->comp_unit.decls.items[k]->from_package = 1;
                    }
                    for (int k = 0; k < unit->comp_unit.usings.count; k++)
                        zan_ast_list_push(&ast->comp_unit.usings,
                                          unit->comp_unit.usings.items[k], arena);
                    for (int k = 0; k < unit->comp_unit.decls.count; k++)
                        zan_ast_list_push(&ast->comp_unit.decls,
                                          unit->comp_unit.decls.items[k], arena);
                }
            }
        }
        if (pi_arena) {
            zan_arena_free(pi_arena);
            pi_arena = NULL;
            memset(pi_table, 0, sizeof(pi_table));
            pi_dirs_head = pi_dirs_tail = NULL;
        }
        /* 约定路由的动作派发器：必须早于最后一次 nsresolve（新声明的类型
           引用要进解析/裁剪视野），又必须晚于所有输入解析完（控制器齐了）。 */
        zan_compile_trace("synth action dispatch");
        zan_synth_action_dispatch(ast, arena, diag);
        /* 框架化窗口入口：同一窗口期合成 __ZanDocMain（零参 Bootstrap
           调用点驱动，见 zan_synth_docapp_main 注释）。 */
        zan_compile_trace("synth docapp main");
        zan_synth_docapp_main(ast, arena, diag, input_file);
        zan_compile_trace("nsresolve generated");
        zan_nsresolve_run(ast, arena, diag);
        zan_compile_trace("prune");
        zan_nsresolve_prune(ast, arena, diag);
        phase("prune");
        zan_compile_trace("resolve done");
    }

    package_sources_destroy();

    if (emit_symbols_path) {
        int srv = zan_symbols_emit(ast, emit_symbols_path);
        if (srv != 0)
            fprintf(stderr, "error: cannot write symbol index '%s'\n",
                    emit_symbols_path);
        zan_arena_free(arena);
        return srv;
    }

    if (do_dump_ast) {
        if (!zan_diag_has_errors(diag)) {
            dump_ast_node(ast, 0);
        }
    }

    if (zan_diag_has_errors(diag)) {
        fprintf(stderr, "\n%d error(s), %d warning(s)\n",
                diag->error_count, diag->warning_count);
        report_suppressed_errors(diag);
        zan_diag_free_buffers(diag);
        zan_arena_free(arena);
        free(source);
        return 1;
    }

    phase("resolve");
    probe_phase_mem("resolve");

    zan_binder_t binder;
    zan_binder_init(&binder, arena, diag);
    zan_compile_trace("bind");
    zan_binder_bind(&binder, ast);

    phase("bind");
    probe_phase_mem("bind");

    zan_checker_t checker;
    zan_checker_init(&checker, &binder, arena, diag);
    zan_compile_trace("check");
    zan_checker_check(&checker, ast);

    if (do_dump_ast) {
    }

    if (zan_diag_has_errors(diag)) {
        fprintf(stderr, "\n%d error(s) after type checking\n",
                diag->error_count);
        report_suppressed_errors(diag);
        zan_diag_free_buffers(diag);
        zan_arena_free(arena);
        free(source);
        return 1;
    }

    phase("check");
    probe_phase_mem("check");

    zan_irgen_t irgen;
    const char *irgen_triple = NULL;
    if (cross_compiling) {
        if (target.os == ZAN_OS_WINDOWS)
            irgen_triple = (target.arch == ZAN_ARCH_AARCH64)
                           ? "aarch64-w64-windows-gnu"
                           : "x86_64-w64-windows-gnu";
        else
            irgen_triple = target.triple;
    }
    if (output_file && !emit_lib) {
        const char *ext = strrchr(output_file, '.');
        if (ext) {
            if (strcmp(ext, ".dll") == 0 || strcmp(ext, ".so") == 0 ||
                strcmp(ext, ".dylib") == 0) {
                emit_lib = true; lib_shared = true;
            } else if (strcmp(ext, ".a") == 0 || strcmp(ext, ".lib") == 0) {
                emit_lib = true; lib_shared = false;
            }
        }
    }
    if (emit_lib && output_file && !lib_shared) {
        const char *ext = strrchr(output_file, '.');
        if (ext && (strcmp(ext, ".dll") == 0 || strcmp(ext, ".so") == 0 ||
                    strcmp(ext, ".dylib") == 0))
            lib_shared = true;
    }
    if (apk_path) {
        lib_shared = true;
        if (target.os != ZAN_OS_ANDROID) {
            fprintf(stderr, "error: --emit-apk requires --target "
                    "android-x64 or android-arm64\n");
            return 1;
        }
        if (project_root_has_manifest()) {
            if (!load_proj_android_keys_done) {
                load_proj_android_keys();
                load_proj_android_keys_done = true;
            }
            if (!proj_android_keys_ok) return 1;
            if (!apk_package && proj_android_package[0]) {
                apk_package = proj_android_package;
            }
            if (!apk_label && proj_android_label[0]) {
                apk_label = proj_android_label;
            }
        }
    }
    if (!ipa_path && output_file) {
        size_t olen = strlen(output_file);
        if (olen >= 4 && strcmp(output_file + olen - 4, ".ipa") == 0) {
            ipa_path = output_file;
        }
    }
    if (ipa_path) {
        if (target.os != ZAN_OS_IOS) {
            fprintf(stderr, "error: --emit-ipa requires --target ios-arm64\n");
            return 1;
        }
    }
    bool check_leaks = check_leaks_opt >= 0 ? check_leaks_opt != 0
                                           : (debug_info && !publish_mode);
    bool arc_guard = arc_guard_opt >= 0 ? arc_guard_opt != 0
                                        : (debug_info && !publish_mode);
    /* 生产级安全基线：无论 debug 还是 publish 模式，
       默认均保留 runtime_checks（数组/切片越界防线、除零保护等）。
       仅当用户显式传递 --no-runtime-checks 时才予以禁用。 */
    if (runtime_checks_opt < 0) {
        runtime_checks = true;
    }
    bool arc_net = publish_mode && arc_guard_opt != 1 && arc_guard_opt != 0;
    zan_arena_t *ir_arena = zan_arena_new();
    if (!ir_arena) {
        fprintf(stderr, "error: failed to allocate code-generation arena\n");
        zan_diag_free_buffers(diag);
        zan_arena_free(arena);
        free(source);
        return 1;
    }
    if (zan_irgen_init(&irgen, ir_arena, diag, &binder, input_file,
                       irgen_triple,
                       target.os == ZAN_OS_WINDOWS, external_async_executor,
                       check_leaks, runtime_checks, arc_guard,
                       arc_net) != ZAN_OK) {
        fprintf(stderr, "error: failed to initialize code generator\n");
        zan_diag_free_buffers(diag);
        zan_arena_free(ir_arena);
        zan_arena_free(arena);
        free(source);
        return 1;
    }
    irgen.emit_debug = debug_info;
    irgen.strict_runtime = strict_runtime;
    irgen.publish_mode = publish_mode;
    if (publish_mode && zan_inputs_use_design_doc(input_files, input_count)) {
        char handler_root[1024];
        zan_design_root_for(input_files[0], handler_root, sizeof(handler_root));
        irgen.html_handler_count =
            zan_embed_collect_html_handlers(handler_root, &irgen.html_handlers);
        fprintf(stderr, "note: retained %d html event handler name(s) from %s/src\n",
                irgen.html_handler_count, handler_root);
    }
    irgen.irgen_checked_depth = checked_arith ? 1 : 0; /* B-ID76 */
    irgen.rt_guard_split = !cross_compiling;
    const char *obf_env = getenv("ZAN_OBF");
    const char *no_obf_env = getenv("ZAN_NO_OBF");
    if (obfuscate_strings_opt >= 0) {
        irgen.obfuscate_strings = (obfuscate_strings_opt == 1);
    } else if (obf_env && obf_env[0] == '1') {
        irgen.obfuscate_strings = true;
    } else if (no_obf_env && no_obf_env[0] == '1') {
        irgen.obfuscate_strings = false;
    } else {
        irgen.obfuscate_strings = false;
    }
    irgen.hide_diagnostics = hide_diagnostics;
    irgen.site_map_path = site_map_path;
    if (site_map_path && site_map_path[0]) {
        irgen.site_map_fp = fopen(site_map_path, "w");
        if (!irgen.site_map_fp) {
            fprintf(stderr, "warning: cannot open site map file for writing: %s\n", site_map_path);
        } else {
            fprintf(irgen.site_map_fp, "# Zan Diagnostic Site Map\n# Input: %s\n", input_file ? input_file : "zan");
        }
    }
    irgen.fast_codegen = false;
    irgen.emit_lib = emit_lib;
    irgen.emit_shared = lib_shared;

    const char *shard_env = getenv("ZAN_SHARD");
    const char *no_shard_env = getenv("ZAN_NO_SHARD");
    bool native_arch = (target.arch == ZAN_ARCH_X86_64 ||
                        target.arch == ZAN_ARCH_AARCH64);
    bool shard_opt_out = (shard_env && shard_env[0] == '0') ||
                         (no_shard_env && no_shard_env[0] == '1');
    bool want_shard = native_arch && !cross_compiling && !shard_opt_out &&
                      ((shard_env && shard_env[0] == '1') || publish_mode);

    zan_irgen_bind_target(&irgen);
    if (zan_irgen_emit(&irgen, ast) != ZAN_OK) {
        fprintf(stderr, "error: code generation failed\n");
        report_suppressed_errors(irgen.diag);
        zan_diag_free_buffers(irgen.diag);
        zan_irgen_destroy(&irgen);
        zan_arena_free(ir_arena);
        zan_arena_free(arena);
        free(source);
        return 1;
    }

