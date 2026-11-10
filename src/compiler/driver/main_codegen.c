/* FRAGMENT（拼接片段，非独立 TU）：由 main.c 的 main() 函数体 #include，
 * 位于 main_frontend.c 之后、main_linker.c 之前。
 * consumes: main_frontend.c 产出的 irgen/ast/target/arena/ir_arena/
 *   source/want_shard/native_arch/publish_mode 与 phase()/probe_phase_mem()
 * defines: mf/mf_built, effective_opt, driver_reg/driver_prefixes,
 *   shard_n, generated_objects, obj_tmp（链接阶段消费） */
    zan_prepare_static_runtime(&irgen, &target);

    phase("irgen");
    probe_phase_mem("irgen");
    if (g_time_phases) {
        size_t definitions = 0, declarations = 0, blocks = 0, instructions = 0;
        for (LLVMValueRef fn = LLVMGetFirstFunction(irgen.mod); fn;
             fn = LLVMGetNextFunction(fn)) {
            LLVMBasicBlockRef bb = LLVMGetFirstBasicBlock(fn);
            if (!bb) { declarations++; continue; }
            definitions++;
            for (; bb; bb = LLVMGetNextBasicBlock(bb)) {
                blocks++;
                for (LLVMValueRef inst = LLVMGetFirstInstruction(bb); inst;
                     inst = LLVMGetNextInstruction(inst)) instructions++;
            }
        }
        fprintf(stderr, "IR stats: %zu AST nodes created; %zu defined functions, "
                "%zu declarations, %zu blocks, %zu instructions\n",
                zan_ast_node_count(), definitions, declarations, blocks, instructions);
        fprintf(stderr, "Scale stats: %zu file reads (%zu MB), %zu metadata scans, "
                "%zu meta-cache hit/%zu miss/%zu written, "
                "%zu seed sources/%zu lexer passes, %zu throwaway parses "
                "(%zu fallback), %zu real parses, %zu secondary parses\n",
                g_scale_stats.file_reads,
                g_scale_stats.bytes_read / (1024 * 1024),
                g_scale_stats.metadata_scans,
                g_scale_stats.metadata_cache_hits,
                g_scale_stats.metadata_cache_misses,
                g_scale_stats.metadata_cache_writes,
                g_scale_stats.seed_sources,
                g_scale_stats.seed_lex_passes,
                g_scale_stats.throwaway_parses,
                g_scale_stats.throwaway_parse_fallbacks,
                g_scale_stats.real_parses,
                g_scale_stats.secondary_parses);
        zan_arena_dump_stats();
    }

    if (arena) {
        for (int i = 0; i < irgen.extern_lib_count; i++) {
            zan_istr_t *lib = &irgen.extern_libs[i];
            lib->str = zan_arena_strdup(ir_arena, lib->str, lib->len);
        }
        for (int i = 0; i < irgen.extern_fn_count; i++) {
            zan_istr_t *lib = &irgen.extern_fns[i].lib;
            zan_istr_t *name = &irgen.extern_fns[i].name;
            lib->str = zan_arena_strdup(ir_arena, lib->str, lib->len);
            name->str = zan_arena_strdup(ir_arena, name->str, name->len);
        }
        zan_diag_t *ir_diag = (zan_diag_t *)zan_arena_alloc(ir_arena, sizeof(*ir_diag));
        *ir_diag = *diag;
        ir_diag->file_sources = NULL;
        const char **ir_file_names = (const char **)malloc(
            (size_t)ir_diag->file_count * sizeof(*ir_file_names));
        for (int i = 0; i < ir_diag->file_count; i++)
            ir_file_names[i] = zan_arena_strdup(ir_arena,
                ir_diag->file_names[i], strlen(ir_diag->file_names[i]));
        ir_diag->file_names = ir_file_names;
        ir_diag->entries = NULL;
        ir_diag->entry_count = ir_diag->entry_cap = 0;
        ir_diag->dedup_sigs = NULL;
        ir_diag->dedup_count = ir_diag->dedup_cap = 0;
        if (diag->suppress_count > 0 && diag->suppress_codes) {
            ir_diag->suppress_codes = (int *)malloc(sizeof(int) * (size_t)diag->suppress_count);
            if (ir_diag->suppress_codes) {
                memcpy(ir_diag->suppress_codes, diag->suppress_codes, sizeof(int) * (size_t)diag->suppress_count);
                ir_diag->suppress_cap = diag->suppress_count;
            } else {
                ir_diag->suppress_count = ir_diag->suppress_cap = 0;
            }
        } else {
            ir_diag->suppress_codes = NULL;
            ir_diag->suppress_count = ir_diag->suppress_cap = 0;
        }
        if (diag->pragma_count > 0 && diag->pragma_ranges) {
            ir_diag->pragma_ranges = (zan_diag_pragma_range_t *)malloc(
                sizeof(zan_diag_pragma_range_t) * (size_t)diag->pragma_count);
            if (ir_diag->pragma_ranges) {
                memcpy(ir_diag->pragma_ranges, diag->pragma_ranges,
                       sizeof(zan_diag_pragma_range_t) * (size_t)diag->pragma_count);
                ir_diag->pragma_cap = diag->pragma_count;
            } else {
                ir_diag->pragma_count = ir_diag->pragma_cap = 0;
            }
        } else {
            ir_diag->pragma_ranges = NULL;
            ir_diag->pragma_count = ir_diag->pragma_cap = 0;
        }
        irgen.diag = ir_diag;
        zan_diag_free_buffers(diag);
        zan_arena_free(arena);
        arena = NULL;
        g_main_arena = ir_arena;
        irgen.binder = NULL;
    }
    probe_phase_mem("release ast");
    if (source) { free(source); source = NULL; }

    const char *mf_json_path = getenv("ZAN_CODEGEN_MANIFEST_JSON");
    zan_cg_manifest_t mf;
    bool mf_built = false;
    if (getenv("ZAN_CODEGEN_MANIFEST") || mf_json_path || want_shard) {
        zan_opt_strip_unused(&irgen);
        zan_irgen_prune_extern_libs(&irgen);
        phase("manifest");
        probe_phase_mem("manifest");
        bool mf_native = target.arch == ZAN_ARCH_X86_64 ||
                         target.arch == ZAN_ARCH_AARCH64;
        zan_irgen_manifest_build(&irgen, &mf, mf_native);
        mf_built = true;
        if (getenv("ZAN_CODEGEN_MANIFEST"))
            zan_irgen_manifest_report(&irgen, &mf);
        if (mf_json_path &&
            zan_irgen_manifest_write_json(&irgen, &mf, mf_json_path) != ZAN_OK)
            fprintf(stderr, "warning: cannot write codegen manifest '%s'\n",
                    mf_json_path);
    }

    zan_opt_level_t effective_opt = ZAN_OPT_NONE;
    if (opt_level >= 0) {
        effective_opt = (zan_opt_level_t)opt_level;
    } else if (publish_mode) {
        effective_opt = ZAN_OPT_SIZE;
    } else if (!debug_info) {
        effective_opt = ZAN_OPT_FULL;
    }
    if (debug_info && effective_opt > ZAN_OPT_NONE) {
        fprintf(stderr, "note: -g forces -O0 (debug info is emitted unoptimized)\n");
        effective_opt = ZAN_OPT_NONE;
    }
    char obj_path[1024];
    if (ipa_path) {
        snprintf(obj_path, sizeof(obj_path), "%s.macho_tmp", ipa_path);
    } else if (output_file) {
        snprintf(obj_path, sizeof(obj_path), "%s", output_file);
    } else {
        size_t ilen = strlen(input_file);
        const char *def_ext = (target.os == ZAN_OS_WINDOWS) ? ".exe" : "";
        if (ilen > 4 && strcmp(input_file + ilen - 4, ".zan") == 0) {
            snprintf(obj_path, sizeof(obj_path), "%.*s%s", (int)(ilen - 4), input_file, def_ext);
        } else {
            snprintf(obj_path, sizeof(obj_path), "%s%s", input_file,
                     (target.os == ZAN_OS_WINDOWS) ? ".exe" : ".out");
        }
    }

    if (output_file) {
        char out_dir[1024];
        size_t olen = strlen(output_file);
        if (olen < sizeof(out_dir)) {
            memcpy(out_dir, output_file, olen + 1);
            for (char *p = out_dir; *p; p++) {
                if ((*p == '/' || *p == '\\') && p > out_dir) {
                    char sep = *p;
                    *p = '\0';
#ifdef _WIN32
                    CreateDirectoryA(out_dir, NULL);
#else
                    mkdir(out_dir, 0755);
#endif
                    *p = sep;
                }
            }
        }
    }

    zan_driver_registry_t driver_reg;
    zan_driver_prefix_snapshot_t driver_prefixes = {0};
    if (!do_emit_ir) {
        if (!mf_built) {
            zan_opt_strip_unused(&irgen);
            zan_irgen_prune_extern_libs(&irgen);
        }
        zan_discover_drivers(resolved_stdlib_root, &driver_reg);
        zan_driver_capture_conditions(&irgen, ir_arena, &driver_reg, &target,
                                      driver_dir_override, &driver_prefixes);
    }

    char **shard_objs = NULL;
    int shard_n = 0;
    if (!do_emit_ir && want_shard && mf_built) {
        phase("shard");
        shard_n = zan_irgen_shard_run(&irgen, &mf, obj_path, &shard_objs);
        if (shard_n < 0) {
            fprintf(stderr, "error: shard emission failed\n");
            zan_diag_free_buffers(irgen.diag);
            zan_irgen_destroy(&irgen);
            zan_arena_free(ir_arena);
            if (arena) zan_arena_free(arena);
            free(source);
            return 1;
        }
    }
    if (shard_n > 0) phase("shard emit");
    probe_phase_mem("shard emit");
    if (mf_built) {
        zan_irgen_manifest_free(&mf);
        mf_built = false;
    }

    if (effective_opt > ZAN_OPT_NONE) {
        zan_opt_report_t opt_report = zan_optimize(&irgen, NULL, effective_opt);
        zan_opt_report_print(&opt_report);
        phase("optimize");
        probe_phase_mem("optimize");
        if (shard_n == 0)
            zan_irgen_prune_extern_libs(&irgen);
    } else {
        if (!irgen.target_is_wasm)
            irgen.fast_codegen = true;
        if (!do_emit_ir) {
            zan_opt_strip_unused(&irgen);
            if (shard_n == 0)
                zan_irgen_prune_extern_libs(&irgen);
        }
    }

    if (do_emit_ir) {
        if (zan_irgen_write_ir(&irgen, NULL) != ZAN_OK) {
            fprintf(stderr, "error: failed to write LLVM IR\n");
            zan_diag_free_buffers(irgen.diag);
            zan_irgen_destroy(&irgen);
            zan_arena_free(ir_arena);
            zan_arena_free(arena);
            free(source);
            return 1;
        }
        /* 提示消费方：这份 IR 依赖编译时选定的目标特性，generic CPU 下
           256 位向量路径无法合法化（项目台账 A388）。 */
        if (irgen.target_cpu[0]) {
            fprintf(stderr,
                    "zanc: --emit-ir 输出的 IR 依赖目标特性 cpu=%s features=%s\n"
                    "zanc: 消费方请用同样的特性，例如 llc -mcpu=%s -mattr=%s -filetype=obj out.ll\n",
                    irgen.target_cpu,
                    irgen.target_features[0] ? irgen.target_features : "(default)",
                    irgen.target_cpu,
                    irgen.target_features[0] ? irgen.target_features : "+sse2");
        }
    } else {
        char cross_archives[ZAN_MAX_USED_DRIVERS][1200];
        int cross_archive_count = 0;
        if (cross_compiling && (target.os == ZAN_OS_LINUX
                                || target.os == ZAN_OS_OHOS)) {
            zan_driver_registry_t cross_reg;
            zan_discover_drivers(resolved_stdlib_root, &cross_reg);
            const char *dsub = zan_driver_subdir(&target);
            for (int li = 0; li < irgen.extern_lib_count; li++) {
                int nlen;
                const char *nm = zan_dllimport_lname(
                    irgen.extern_libs[li].str,
                    (int)irgen.extern_libs[li].len, &nlen);
                if (!nm) continue;
                char archive[1200];
                archive[0] = '\0';
                int didx = zan_driver_find(&cross_reg, nm, nlen);
                if (didx >= 0) {
                    snprintf(archive, sizeof(archive),
                             "%s/%s/drivers/%s/static/lib%.*s.a",
                             cross_reg.entries[didx].root,
                             cross_reg.entries[didx].module, dsub, nlen, nm);
                }
                if (archive[0] && zan_file_exists(archive)) {
                    if (cross_archive_count >= ZAN_MAX_USED_DRIVERS)
                        link_cap_exceeded("static driver archives", ZAN_MAX_USED_DRIVERS);
                    {
                        snprintf(cross_archives[cross_archive_count],
                                 sizeof(cross_archives[0]), "%s", archive);
                        cross_archive_count++;
                    }
                } else {
                    int n = zan_irgen_stub_extern_lib(
                        &irgen, irgen.extern_libs[li].str,
                        (int)irgen.extern_libs[li].len);
                    if (n > 0 && !zan_win_system_lib(
                            irgen.extern_libs[li].str,
                            (int)irgen.extern_libs[li].len)) {
                        fprintf(stderr,
                                "warning: no static %s archive for "
                                "[DllImport(\"%.*s\")]; its %d function(s) "
                                "are stubbed and will fail at runtime\n",
                                dsub, (int)irgen.extern_libs[li].len,
                                irgen.extern_libs[li].str, n);
                    }
                }
            }
        }

        if (cross_compiling && target.os == ZAN_OS_OHOS) {
            char oexe[1024] = {0};
            zan_exe_dir(oexe, sizeof(oexe));
            const char *odrop_sub = (target.arch == ZAN_ARCH_AARCH64)
                                    ? "ohos-arm64" : "ohos-x64";
            zan_driver_registry_t oreg;
            zan_discover_drivers(resolved_stdlib_root, &oreg);
            for (int li = irgen.extern_lib_count - 1; li >= 0; li--) {
                if (zan_win_system_lib(irgen.extern_libs[li].str,
                                       (int)irgen.extern_libs[li].len))
                    continue;
                int nlen;
                const char *nm = zan_dllimport_lname(
                    irgen.extern_libs[li].str,
                    (int)irgen.extern_libs[li].len, &nlen);
                if (!nm) continue;
                int resolvable = 0;
                int via_static_archive = 0;
                { char drvso[1300];
                  snprintf(drvso, sizeof(drvso), "%s/%s/lib%.*s.so",
                           oexe, odrop_sub, nlen, nm);
                  if (zan_file_exists(drvso)) {
                      resolvable = 1;
                  } else {
                      int didx = zan_driver_find(&oreg, nm, nlen);
                      if (didx >= 0) {
                          snprintf(drvso, sizeof(drvso),
                                   "%s/%s/drivers/%s/static/lib%.*s.a",
                                   oreg.entries[didx].root,
                                   oreg.entries[didx].module,
                                   odrop_sub, nlen, nm);
                          if (zan_file_exists(drvso)) {
                              resolvable = 1;
                              via_static_archive = 1;
                          } else {
                              snprintf(drvso, sizeof(drvso),
                                       "%s/%s/drivers/%s/lib%.*s.so",
                                       oreg.entries[didx].root,
                                       oreg.entries[didx].module,
                                       odrop_sub, nlen, nm);
                              resolvable = zan_file_exists(drvso);
                          }
                      }
                  }
                }
                if (via_static_archive) {
                    zan_irgen_drop_extern_lib(
                        &irgen, irgen.extern_libs[li].str,
                        (int)irgen.extern_libs[li].len);
                } else if (!resolvable) {
                    zan_irgen_stub_extern_lib(
                        &irgen, irgen.extern_libs[li].str,
                        (int)irgen.extern_libs[li].len);
                    zan_irgen_drop_extern_lib(
                        &irgen, irgen.extern_libs[li].str,
                        (int)irgen.extern_libs[li].len);
                }
            }
        }

        char cross_dylibs[ZAN_MAX_USED_DRIVERS][1200];
        int cross_dylib_count = 0;
        if (cross_compiling && target.os == ZAN_OS_MACOS) {
            zan_driver_registry_t mac_reg;
            zan_discover_drivers(resolved_stdlib_root, &mac_reg);
            const char *dsub = zan_driver_subdir(&target);
            for (int li = 0; li < irgen.extern_lib_count; li++) {
                int nlen;
                const char *nm = zan_dllimport_lname(
                    irgen.extern_libs[li].str,
                    (int)irgen.extern_libs[li].len, &nlen);
                if (!nm) continue; /* CRT/libc/libm: resolved by libSystem */
                char dylib[1200];
                dylib[0] = '\0';
                int didx = zan_driver_find(&mac_reg, nm, nlen);
                if (didx >= 0) {
                    char driver_dir[1200];
                    snprintf(driver_dir, sizeof(driver_dir),
                             "%s/%s/drivers/%s", mac_reg.entries[didx].root,
                             mac_reg.entries[didx].module, dsub);
                    zan_find_macos_driver_dylib(
                        driver_dir, nm, nlen, dylib, sizeof(dylib));
                }
                if (dylib[0]) {
                    if (cross_dylib_count >= ZAN_MAX_USED_DRIVERS)
                        link_cap_exceeded("driver dylibs", ZAN_MAX_USED_DRIVERS);
                    {
                        snprintf(cross_dylibs[cross_dylib_count],
                                 sizeof(cross_dylibs[0]), "%s", dylib);
                        cross_dylib_count++;
                    }
                } else if (target.os == ZAN_OS_IOS &&
                           (strncmp(irgen.extern_libs[li].str, "zan_gui", irgen.extern_libs[li].len) == 0 ||
                            strncmp(irgen.extern_libs[li].str, "zan_image", irgen.extern_libs[li].len) == 0 ||
                            strncmp(irgen.extern_libs[li].str, "zan_game", irgen.extern_libs[li].len) == 0 ||
                            strncmp(irgen.extern_libs[li].str, "zan_audio", irgen.extern_libs[li].len) == 0)) {
                    /* iOS 使用静态内嵌的 zanrt_gui.o / zanrt_image.o / zanrt_game.o / zanrt_audio.o，不打桩也不需要外部 dylib */
                } else {
                    int n = zan_irgen_stub_extern_lib(
                        &irgen, irgen.extern_libs[li].str,
                        (int)irgen.extern_libs[li].len);
                    if (n > 0 && !zan_win_system_lib(
                            irgen.extern_libs[li].str,
                            (int)irgen.extern_libs[li].len)) {
                        fprintf(stderr,
                                "warning: no %s driver dylib for "
                                "[DllImport(\"%.*s\")]; its %d function(s) "
                                "are stubbed and will fail at runtime\n",
                                dsub, (int)irgen.extern_libs[li].len,
                                irgen.extern_libs[li].str, n);
                    }
                }
            }
        }

        if (target.os != ZAN_OS_WINDOWS) {
            for (int li = irgen.extern_lib_count - 1; li >= 0; li--) {
                if (!zan_win_system_lib(irgen.extern_libs[li].str,
                                        (int)irgen.extern_libs[li].len))
                    continue;
                zan_irgen_stub_extern_lib(&irgen, irgen.extern_libs[li].str,
                                          (int)irgen.extern_libs[li].len);
                zan_irgen_drop_extern_lib(&irgen, irgen.extern_libs[li].str,
                                          (int)irgen.extern_libs[li].len);
            }
        }

        if (cross_compiling && target.os == ZAN_OS_ANDROID) {
            char aexe[1024] = {0};
            zan_exe_dir(aexe, sizeof(aexe));
            const char *asub = (target.arch == ZAN_ARCH_AARCH64)
                               ? "android-arm64" : "android-x64";
            for (int li = irgen.extern_lib_count - 1; li >= 0; li--) {
                if (zan_win_system_lib(irgen.extern_libs[li].str,
                                       (int)irgen.extern_libs[li].len))
                    continue;
                int nlen;
                const char *nm = zan_dllimport_lname(
                    irgen.extern_libs[li].str,
                    (int)irgen.extern_libs[li].len, &nlen);
                if (!nm) continue;
                int resolvable = 0;
                { char stubso[1200];
                  snprintf(stubso, sizeof(stubso), "%s/%s/lib%.*s.so",
                           aexe, asub, nlen, nm);
                  if (zan_file_exists(stubso)) {
                      resolvable = 1;
                  } else {
                      int didx = zan_driver_find(&driver_reg, nm, nlen);
                      if (didx >= 0) {
                          snprintf(stubso, sizeof(stubso),
                                   "%s/%s/drivers/%s/lib%.*s.so",
                                   driver_reg.entries[didx].root,
                                   driver_reg.entries[didx].module,
                                   zan_driver_subdir(&target), nlen, nm);
                          resolvable = zan_file_exists(stubso);
                          if (!resolvable) {
                              snprintf(stubso, sizeof(stubso),
                                       "%s/%s/drivers/%s/static/lib%.*s.a",
                                       driver_reg.entries[didx].root,
                                       driver_reg.entries[didx].module,
                                       zan_driver_subdir(&target), nlen, nm);
                              resolvable = zan_file_exists(stubso);
                          }
                      }
                  }
                }
                if (!resolvable) {
                    int nst = zan_irgen_stub_extern_lib(
                        &irgen, irgen.extern_libs[li].str,
                        (int)irgen.extern_libs[li].len);
                    zan_irgen_drop_extern_lib(
                        &irgen, irgen.extern_libs[li].str,
                        (int)irgen.extern_libs[li].len);
                    if (nst > 0) {
                        fprintf(stderr,
                                "warning: no android driver for "
                                "[DllImport(\"%.*s\")]; its %d function(s) "
                                "are stubbed and will fail at runtime\n",
                                nlen, nm, nst);
                    }
                }
            }
        }
        char driver_dirs[ZAN_MAX_USED_DRIVERS][1024];
        const char *used_drivers[ZAN_MAX_USED_DRIVERS]; int used_driver_count = 0;
        int used_driver_len[ZAN_MAX_USED_DRIVERS];
        const char *used_driver_module[ZAN_MAX_USED_DRIVERS];
        const char *used_driver_root[ZAN_MAX_USED_DRIVERS];
        bool used_driver_runtime[ZAN_MAX_USED_DRIVERS] = { false };
        bool used_driver_static[ZAN_MAX_USED_DRIVERS] = { false };
        bool used_driver_embedded[ZAN_MAX_USED_DRIVERS] = { false };
        char embedded_driver_file[ZAN_MAX_USED_DRIVERS][128];
        char static_driver_libs[ZAN_MAX_STATIC_DRV_LIBS][128];
        int static_driver_lib_count = 0;
        for (int li = 0; li < irgen.extern_lib_count; li++) {
            if (used_driver_count >= ZAN_MAX_USED_DRIVERS)
                link_cap_exceeded("native drivers", ZAN_MAX_USED_DRIVERS);
            int nlen;
            const char *nm = zan_dllimport_lname(
                irgen.extern_libs[li].str, (int)irgen.extern_libs[li].len, &nlen);
            int didx = nm ? zan_driver_find(&driver_reg, nm, nlen) : -1;
            if (didx >= 0) {
                used_drivers[used_driver_count] = nm;
                used_driver_len[used_driver_count] = nlen;
                used_driver_module[used_driver_count] = driver_reg.entries[didx].module;
                used_driver_root[used_driver_count] = driver_reg.entries[didx].root;
                used_driver_runtime[used_driver_count] = false;
                used_driver_count++;
            }
        }
        for (int di = 0; di < driver_reg.count; di++) {
            if (used_driver_count >= ZAN_MAX_USED_DRIVERS)
                link_cap_exceeded("native drivers", ZAN_MAX_USED_DRIVERS);
            const char *sym = driver_reg.entries[di].sym;
            if (!sym[0]) continue;
            if (!zan_driver_prefix_live(&irgen, &driver_prefixes, sym)) continue;
            const char *lib = driver_reg.entries[di].lib;
            bool dup = false;
            for (int u = 0; u < used_driver_count; u++)
                if ((int)strlen(lib) == used_driver_len[u] &&
                    memcmp(lib, used_drivers[u], strlen(lib)) == 0) dup = true;
            if (dup) continue;
            used_drivers[used_driver_count] = lib;
            used_driver_len[used_driver_count] = (int)strlen(lib);
            used_driver_module[used_driver_count] = driver_reg.entries[di].module;
            used_driver_root[used_driver_count] = driver_reg.entries[di].root;
            used_driver_runtime[used_driver_count] = true;
            used_driver_count++;
        }
        {
            const char *dsub = zan_driver_subdir(&target);
            for (int d = 0; d < used_driver_count; d++) {
                driver_dirs[d][0] = '\0';
                if (driver_dir_override) {
                    snprintf(driver_dirs[d], sizeof(driver_dirs[d]), "%s",
                             driver_dir_override);
                } else {
                    const char *mod = used_driver_module[d];
                    const char *droot = used_driver_root[d];
                    if (mod && mod[0] && droot && droot[0]) {
                        snprintf(driver_dirs[d], sizeof(driver_dirs[d]),
                                 "%s/%s/drivers/%s", droot, mod, dsub);
                    }
                }
            }
        }

        if (target.os == ZAN_OS_WINDOWS && link_subsystem == NULL) {
            int mcap = 64;
            char (*mdirs)[1024] = (char (*)[1024])malloc((size_t)mcap * sizeof(*mdirs));
            if (mdirs) {
                int mcnt = zan_pkg_all_manifest_dirs(package_project_root, mdirs, mcap);
                for (int m = 0; m < mcnt && link_subsystem == NULL; m++) {
                    char mpath[1200];
                    snprintf(mpath, sizeof(mpath), "%s/zan.pkg", mdirs[m]);
                    zan_package_t pkg;
                    if (zan_pkg_load(&pkg, mpath)) {
                        if (pkg.subsystem[0]) {
                            bool match = false;
                            if (pkg.subsystem_driver[0]) {
                                int slen = (int)strlen(pkg.subsystem_driver);
                                for (int d = 0; d < used_driver_count; d++) {
                                    if (used_driver_len[d] == slen &&
                                        memcmp(used_drivers[d], pkg.subsystem_driver, (size_t)slen) == 0) {
                                        match = true;
                                        break;
                                    }
                                }
                            }
                            if (!match && pkg.subsystem_sym[0] &&
                                zan_driver_prefix_live(&irgen, &driver_prefixes, pkg.subsystem_sym)) {
                                match = true;
                            }
                            if (match) {
                                if (strcmp(pkg.subsystem, "windows") == 0) link_subsystem = "windows";
                                else if (strcmp(pkg.subsystem, "console") == 0) link_subsystem = "console";
                            }
                        }
                        zan_pkg_destroy(&pkg);
                    }
                }
                free(mdirs);
            }
        }

        char embed_driver_specs[128][1300];
        int embed_driver_spec_count = 0;
        char **spec_subdirs[ZAN_MAX_EMBED_SPECS] = {0};
        int spec_subdir_counts[ZAN_MAX_EMBED_SPECS] = {0};
        if (target.os == ZAN_OS_WINDOWS && link_static_drivers) {
            for (int d = 0; d < used_driver_count; d++) {
                if (!used_driver_runtime[d] || !driver_dirs[d][0]) continue;
                if (embed_driver_spec_count >= 128)
                    link_cap_exceeded("embedded driver DLLs", 128);
                if (embed_spec_count >= ZAN_MAX_EMBED_SPECS)
                    link_cap_exceeded("--embed resources", ZAN_MAX_EMBED_SPECS);
                char drv[64];
                snprintf(drv, sizeof(drv), "%.*s", used_driver_len[d],
                         used_drivers[d]);
                char file[128], src[1300];
                snprintf(file, sizeof(file), "%s.dll", drv);
                snprintf(src, sizeof(src), "%s/%s", driver_dirs[d], file);
                if (!zan_file_exists(src)) {
                    snprintf(file, sizeof(file), "lib%s.dll", drv);
                    snprintf(src, sizeof(src), "%s/%s", driver_dirs[d], file);
                }
                if (!zan_file_exists(src)) continue;
                if (zan_embed_driver_spec(
                        src, file, embed_driver_specs[embed_driver_spec_count],
                        sizeof(embed_driver_specs[0])) != 0) {
                    fprintf(stderr,
                            "warning: cannot embed driver '%s' from '%s'; it "
                            "will be published beside the executable\n",
                            drv, src);
                    continue;
                }
                embed_specs[embed_spec_count++] =
                    embed_driver_specs[embed_driver_spec_count++];
                used_driver_embedded[d] = true;
                snprintf(embedded_driver_file[d],
                         sizeof(embedded_driver_file[0]), "%s", file);
            }
        }

        if (target.os == ZAN_OS_WINDOWS) {
            char win_exe_dir[1024] = {0};
            zan_exe_dir(win_exe_dir, sizeof(win_exe_dir));
            for (int li = irgen.extern_lib_count - 1; li >= 0; li--) {
                int nlen;
                const char *nm = zan_dllimport_lname(
                    irgen.extern_libs[li].str,
                    (int)irgen.extern_libs[li].len, &nlen);
                if (!nm || zan_win_system_lib(nm, nlen)) continue;
                char dir[1200];
                dir[0] = '\0';
                for (int d = 0; d < used_driver_count; d++) {
                    if (used_driver_len[d] == nlen &&
                        memcmp(used_drivers[d], nm, (size_t)nlen) == 0) {
                        snprintf(dir, sizeof(dir), "%s", driver_dirs[d]);
                        break;
                    }
                }
                bool resolvable = false;
                static const char fmts[8][28] = {
                    "%s/static/lib%s.a", "%s/static/%s.lib",
                    "%s/lib%s.a",        "%s/%s.lib",
                    "%s/lib%s.dll.a",    "%s/%s.dll.a",
                    "%s/lib%s.dll",      "%s/%s.dll"
                };
                if (dir[0]) {
                    for (int f = 0; f < 8 && !resolvable; f++) {
                        char cand[1300];
                        snprintf(cand, sizeof(cand), fmts[f], dir, nm);
                        resolvable = zan_file_exists(cand);
                    }
                }
                if (!resolvable && win_exe_dir[0]) {
                    char mlib[1300];
                    snprintf(mlib, sizeof(mlib), "%s\\mingw\\lib\\lib%s.a", win_exe_dir, nm);
                    resolvable = zan_file_exists(mlib);
                    if (!resolvable) {
                        snprintf(mlib, sizeof(mlib), "%s\\mingw\\lib\\lib%s.dll.a", win_exe_dir, nm);
                        resolvable = zan_file_exists(mlib);
                    }
                }
                if (!resolvable) {
                    for (int di = 0; di < extra_lib_path_count && !resolvable; di++) {
                        char cand[1300];
                        snprintf(cand, sizeof(cand), "%s/lib%s.a", extra_lib_paths[di], nm);
                        resolvable = zan_file_exists(cand);
                        if (!resolvable) {
                            snprintf(cand, sizeof(cand), "%s/lib%s.dll.a", extra_lib_paths[di], nm);
                            resolvable = zan_file_exists(cand);
                        }
                    }
                }
                if (resolvable) continue;
                int n = zan_irgen_stub_extern_lib(
                    &irgen, irgen.extern_libs[li].str,
                    (int)irgen.extern_libs[li].len);
                if (n > 0) {
                    char libname[128];
                    snprintf(libname, sizeof(libname), "%.*s",
                             (int)irgen.extern_libs[li].len,
                             irgen.extern_libs[li].str);
                    zan_irgen_drop_extern_lib(
                        &irgen, irgen.extern_libs[li].str,
                        (int)irgen.extern_libs[li].len);
                    fprintf(stderr,
                            "warning: no archive or shared library resolved "
                            "for [DllImport(\"%s\")];"
                            " its %d function(s) are stubbed and will fail at"
                            " run time\n", libname, n);
                }
            }
        }

        /* 包清单 [embed] 声明式资源扫描：解耦所有包内嵌资源 */
        int mcap = 64;
        char (*mdirs)[1024] = (char (*)[1024])malloc((size_t)mcap * sizeof(*mdirs));
        if (mdirs) {
            int mcnt = zan_pkg_all_manifest_dirs(package_project_root, mdirs, mcap);
            for (int m = 0; m < mcnt; m++) {
                char mpath[1200];
                snprintf(mpath, sizeof(mpath), "%s/zan.pkg", mdirs[m]);
                zan_package_t pkg;
                if (!zan_pkg_load(&pkg, mpath)) continue;
                for (int e = 0; e < pkg.embed_count; e++) {
                    const zan_pkg_embed_t *emb = &pkg.embeds[e];
                    if (emb->sym_prefix[0] &&
                        !zan_driver_prefix_live(&irgen, &driver_prefixes, emb->sym_prefix)) {
                        continue;
                    }
                    bool staged = false;
                    for (int es = 0; es < embed_spec_count && !staged; es++) {
                        const char *seq = strrchr(embed_specs[es], '=');
                        if (seq && strcmp(seq + 1, emb->mount) == 0) staged = true;
                    }
                    if (staged) continue;

                    char res_path[1200];
                    if (!zan_resolve_package_resource(mdirs[m], resolved_stdlib_root,
                                                      emb->rel_path, res_path, sizeof(res_path))) {
                        continue;
                    }

                    char *spec_str = (char *)malloc(strlen(res_path) + strlen(emb->mount) + 8);
                    if (!spec_str) continue;
                    snprintf(spec_str, strlen(res_path) + strlen(emb->mount) + 8, "%s=%s", res_path, emb->mount);
                    if (embed_spec_count >= ZAN_MAX_EMBED_SPECS) {
                        fprintf(stderr, "warning: cannot auto-embed resource '%s' from '%s'; --embed limit reached\n",
                                emb->mount, res_path);
                        free(spec_str);
                        continue;
                    }
                    int spec_at = embed_spec_count++;
                    embed_specs[spec_at] = spec_str;

                    if (emb->gate_key[0] || emb->filter_key[0] || emb->baseline[0]) {
                        if (!load_proj_android_keys_done && project_root_has_manifest()) {
                            load_proj_android_keys();
                            load_proj_android_keys_done = true;
                        }
                        const char *gate_val = emb->gate_key[0] ? zan_proj_get(emb->gate_key) : NULL;
                        bool gate_enabled = !emb->gate_key[0] || (gate_val && strcmp(gate_val, "1") == 0);

                        char *keep[64];
                        int keep_count = 0;
                        if (emb->baseline[0]) {
                            char base_copy[128];
                            strncpy(base_copy, emb->baseline, sizeof(base_copy) - 1);
                            base_copy[sizeof(base_copy) - 1] = 0;
                            char *save_b = NULL;
                            char *b_tok = strtok_r(base_copy, ",", &save_b);
                            while (b_tok && keep_count < 64) {
                                while (*b_tok == ' ') b_tok++;
                                size_t blen = strlen(b_tok);
                                while (blen > 0 && b_tok[blen - 1] == ' ') b_tok[--blen] = 0;
                                if (b_tok[0]) keep[keep_count++] = strdup(b_tok);
                                b_tok = strtok_r(NULL, ",", &save_b);
                            }
                        }
                        if (gate_enabled && emb->filter_key[0]) {
                            const char *fval = zan_proj_get(emb->filter_key);
                            if (fval && fval[0]) {
                                char fval_copy[512];
                                strncpy(fval_copy, fval, sizeof(fval_copy) - 1);
                                fval_copy[sizeof(fval_copy) - 1] = 0;
                                char *save_f = NULL;
                                char *f_tok = strtok_r(fval_copy, ",", &save_f);
                                while (f_tok && keep_count < 64) {
                                    while (*f_tok == ' ') f_tok++;
                                    size_t flen = strlen(f_tok);
                                    while (flen > 0 && f_tok[flen - 1] == ' ') f_tok[--flen] = 0;
                                    if (f_tok[0] && strcmp(f_tok, "-") != 0) {
                                        bool dup = false;
                                        for (int j = 0; j < keep_count; j++) {
                                            if (strcmp(keep[j], f_tok) == 0) { dup = true; break; }
                                        }
                                        if (!dup) keep[keep_count++] = strdup(f_tok);
                                    }
                                    f_tok = strtok_r(NULL, ",", &save_f);
                                }
                            }
                        }
                        if (keep_count > 0) {
                            spec_subdirs[spec_at] = (char **)malloc((size_t)keep_count * sizeof(char *));
                            if (spec_subdirs[spec_at]) {
                                for (int k = 0; k < keep_count; k++) {
                                    spec_subdirs[spec_at][k] = keep[k];
                                }
                                spec_subdir_counts[spec_at] = keep_count;
                            } else {
                                for (int k = 0; k < keep_count; k++) free(keep[k]);
                            }
                        }
                    }
                }
                zan_pkg_destroy(&pkg);
            }
            free(mdirs);
        }

        /* 设计稿：--publish 且源码是窗口类形态（" : Form"）时，把工程
         * src/ 下的 *.html 自动加密内嵌进 exe——发布态界面资产不留在
         * 盘上。 */
        if (publish_mode && zan_inputs_use_design_doc(input_files, input_count)) {
            char design_root[1024];
            zan_design_root_for(input_files[0], design_root, sizeof(design_root));
            static char design_specs[256][1300];
            int nd = zan_embed_collect_design_html(design_root, design_specs, 256);
            if (nd < 0) {
                fprintf(stderr, "warning: more than 256 design html files "
                        "under '%s/src'; only the first 256 are embedded\n",
                        design_root);
                nd = 256;
            }
            for (int di = 0; di < nd; di++) {
                if (embed_spec_count < ZAN_MAX_EMBED_SPECS) {
                    embed_specs[embed_spec_count++] = design_specs[di];
                } else {
                    fprintf(stderr, "warning: cannot auto-embed design html "
                            "from '%s/src'; --embed resources limit reached\n",
                            design_root);
                    break;
                }
            }
            if (nd > 0) {
                fprintf(stderr, "note: auto-embedded %d design html file(s) "
                        "from '%s/src' (encrypted in publish output)\n",
                        nd, design_root);
            }
        }

        if (irgen.uses_embed_api &&
            resolved_stdlib_root[0]) {
            char assets_cands[4][1200];
            int assets_cand_count = 0;
            for (int fi = 0; fi < input_count && assets_cand_count < 2; fi++) {
                char assets_dir[1400];
                snprintf(assets_dir, sizeof(assets_dir), "%s",
                         input_files[fi]);
                char *asep = strrchr(assets_dir, '/');
                char *aback = strrchr(assets_dir, '\\');
                if (!asep || (aback && aback > asep)) asep = aback;
                if (!asep) continue;
                *asep = 0;
                snprintf(assets_cands[assets_cand_count], 1200, "%s/assets",
                         assets_dir);
                if (zan_file_exists(assets_cands[assets_cand_count]))
                    assets_cand_count++;
                else if (assets_cand_count < 3) {
                    char *psep = strrchr(assets_dir, '/');
                    char *pback = strrchr(assets_dir, '\\');
                    if (!psep || (pback && pback > psep)) psep = pback;
                    if (psep && psep != assets_dir) {
                        *psep = 0;
                        snprintf(assets_cands[assets_cand_count], 1200,
                                 "%s/assets", assets_dir);
                        if (zan_file_exists(assets_cands[assets_cand_count]))
                            assets_cand_count++;
                    }
                }
            }
            if (assets_cand_count < 2 &&
                strcmp(package_project_root, ".") != 0) {
                snprintf(assets_cands[assets_cand_count], 1200, "%s/assets",
                         package_project_root);
                assets_cand_count++;
            }
            bool assets_staged = false;
            for (int es = 0; es < embed_spec_count && !assets_staged; es++) {
                const char *seq = strrchr(embed_specs[es], '=');
                if (seq && strcmp(seq + 1, "assets") == 0) assets_staged = true;
                if (!seq) {
                    const char *base = zan_path_basename(embed_specs[es]);
                    if (base && strcmp(base, "assets") == 0) assets_staged = true;
                }
            }
            for (int ac = 0; ac < assets_cand_count && !assets_staged; ac++) {
                const char *adir = assets_cands[ac];
                if (!zan_file_exists(adir)) { continue; }
                assets_staged = true;
                char *assets_spec = (char *)malloc(strlen(adir) + 32);
                if (assets_spec) {
                    snprintf(assets_spec, strlen(adir) + 32, "%s=assets", adir);
                    if (embed_spec_count < ZAN_MAX_EMBED_SPECS) {
                        embed_specs[embed_spec_count++] = assets_spec;
                    } else {
                        fprintf(stderr, "warning: cannot auto-embed the "
                                "project assets from '%s'; --embed resources "
                                "limit reached\n", adir);
                        assets_staged = false;
                        free(assets_spec);
                    }
                }
            }
        }

        if (embed_spec_count > 0 || irgen.uses_embed_api) {
            zan_embed_spec_t *spec_items = (zan_embed_spec_t *)calloc((size_t)embed_spec_count, sizeof(zan_embed_spec_t));
            if (spec_items) {
                for (int es = 0; es < embed_spec_count; es++) {
                    spec_items[es].spec = embed_specs[es];
                    spec_items[es].subdirs = (const char *const *)spec_subdirs[es];
                    spec_items[es].subdir_count = spec_subdir_counts[es];
                }
            }
            int nres = spec_items ? zan_embed_emit_spec_list(&irgen, spec_items, embed_spec_count) : -1;
            free(spec_items);
            for (int es = 0; es < embed_spec_count; es++) {
                if (spec_subdirs[es]) {
                    for (int k = 0; k < spec_subdir_counts[es]; k++) free(spec_subdirs[es][k]);
                    free(spec_subdirs[es]);
                    spec_subdirs[es] = NULL;
                }
            }
            if (nres < 0) {
                zan_diag_free_buffers(irgen.diag);
                zan_irgen_destroy(&irgen);
                zan_arena_free(ir_arena);
                zan_arena_free(arena);
                free(source);
                return 1;
            }
        }

        char obj_tmp[1024];
        generated_object_vec_t generated_objects = {0};
        snprintf(obj_tmp, sizeof(obj_tmp), "%s.o", obj_path);

        if (shard_n > 0) {
            for (int i = 0; i < shard_n; i++) {
                generated_object_vec_add(&generated_objects, shard_objs[i]);
                free(shard_objs[i]);
            }
            free(shard_objs);
            shard_objs = NULL;
            shard_n = 0;
        }

        phase("emit obj");
        probe_phase_mem("emit obj");
        if (zan_irgen_write_obj(&irgen, obj_tmp) != ZAN_OK) {
            fprintf(stderr, "error: failed to emit object file\n");
            zan_diag_free_buffers(irgen.diag);
            zan_irgen_destroy(&irgen);
            zan_arena_free(ir_arena);
            zan_arena_free(arena);
            free(source);
            return 1;
        }

        generated_object_vec_add(&generated_objects, obj_tmp);
        phase("write obj");
        probe_phase_mem("write obj");

        zan_irgen_release_llvm(&irgen);
        probe_phase_mem("free llvm");


