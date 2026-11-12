/* main_linker.c - 跨平台链接器参数组装与驱动库定位执行 */
/* 涵盖 Windows (lld-link/ld), Linux (ld.lld/musl), macOS (ld64/lld), WASM (wasm-ld), Android, OHOS 等目标 */

        char icon_obj[1100];
        icon_obj[0] = '\0';
        if (!no_icon && !emit_lib && target.os == ZAN_OS_WINDOWS) {
            int arm64 = target.arch == ZAN_ARCH_AARCH64;
            snprintf(icon_obj, sizeof(icon_obj), "%s.icon.o", obj_path);
            int ires = 1;
            if (icon_path) {
                ires = zan_winres_icon_object(icon_path, icon_obj, arm64);
                if (ires != 0)
                    fprintf(stderr, "warning: cannot embed icon '%s'; "
                                    "the executable gets the built-in one\n",
                            icon_path);
            }
            if (ires != 0 && (!publish_mode || default_icon_explicit)) {
                ires = zan_winres_icon_object_mem(zan_default_icon,
                                                  ZAN_DEFAULT_ICON_LEN,
                                                  icon_obj, arm64);
            }
            if (ires != 0) {
                icon_obj[0] = '\0';
            } else if (extra_link_input_count < ZAN_MAX_LINK_INPUTS) {
                extra_link_inputs[extra_link_input_count++] = icon_obj;
            } else {
                link_cap_exceeded("link inputs", ZAN_MAX_LINK_INPUTS);
            }
        }

        int link_ret;

        char link_exe_dir[1024];
        zan_exe_dir(link_exe_dir, sizeof(link_exe_dir));
        char rt_io_buf[1200];
        char rt_sync_buf[1200];
        char rt_file_buf[1200];
        char rt_decimal_buf[1200];
        char rt_embed_buf[1200];
        char rt_inflate_buf[1200];
        char rt_timer_buf[1200];
        char rt_crash_buf[1200];
        char rt_crypto_buf[1200];
        char rt_mem_buf[1200];

        const char *rt_io_obj = NULL;
#ifdef ZAN_RT_IO_OBJ
        if (irgen.uses_socket_async) {
            snprintf(rt_io_buf, sizeof(rt_io_buf), "%s/%s",
                     link_exe_dir, zan_path_basename(ZAN_RT_IO_OBJ));
            rt_io_obj = rt_io_buf;
        }
#endif
#ifdef ZAN_RT_IO_MT_OBJ
        if (external_async_executor) {
            snprintf(rt_io_buf, sizeof(rt_io_buf), "%s/%s",
                     link_exe_dir, zan_path_basename(ZAN_RT_IO_MT_OBJ));
            rt_io_obj = rt_io_buf;
        }
#endif
        const char *rt_sync_obj = NULL;
#ifdef ZAN_RT_SYNC_OBJ
        if (irgen.uses_sync_runtime) {
            snprintf(rt_sync_buf, sizeof(rt_sync_buf), "%s/%s",
                     link_exe_dir, zan_path_basename(ZAN_RT_SYNC_OBJ));
            rt_sync_obj = rt_sync_buf;
        }
#endif
        const char *rt_file_obj = NULL;
#ifdef ZAN_RT_FILE_OBJ
        if (irgen.uses_file_runtime) {
            snprintf(rt_file_buf, sizeof(rt_file_buf), "%s/%s",
                     link_exe_dir, zan_path_basename(ZAN_RT_FILE_OBJ));
            rt_file_obj = rt_file_buf;
        }
#endif
        const char *rt_decimal_obj = NULL;
#ifdef ZAN_RT_DECIMAL_OBJ
        if (irgen.uses_decimal_runtime) {
            snprintf(rt_decimal_buf, sizeof(rt_decimal_buf), "%s/%s",
                     link_exe_dir, zan_path_basename(ZAN_RT_DECIMAL_OBJ));
            rt_decimal_obj = rt_decimal_buf;
        }
#endif
        const char *rt_embed_obj = NULL;
#ifdef ZAN_EMBED_OBJ
        if (irgen.uses_embed_api) {
            snprintf(rt_embed_buf, sizeof(rt_embed_buf), "%s/%s",
                     link_exe_dir, zan_path_basename(ZAN_EMBED_OBJ));
            rt_embed_obj = rt_embed_buf;
        }
#endif
        const char *rt_inflate_obj = NULL;
#ifdef ZAN_INFLATE_OBJ
        snprintf(rt_inflate_buf, sizeof(rt_inflate_buf), "%s/%s",
                 link_exe_dir, zan_path_basename(ZAN_INFLATE_OBJ));
        if (zan_file_exists(rt_inflate_buf)) rt_inflate_obj = rt_inflate_buf;
#endif
        const char *rt_timer_obj = NULL;
#ifdef ZAN_RT_TIMER_OBJ
        if (!cross_compiling) {
            snprintf(rt_timer_buf, sizeof(rt_timer_buf), "%s/%s",
                     link_exe_dir, zan_path_basename(ZAN_RT_TIMER_OBJ));
            rt_timer_obj = rt_timer_buf;
        }
#endif
        /* Crash reporter（B-ID106 自 rt_crash.h 拆出）：rt_io.o 引
         * zan__crash_install、gui 引 guard_call，凡链这些对象的目标都需要
         * zanrt_crash.o；拆分前它随 rt_timer.o 的头内联构造器无条件在场，
         * 这里保持等效的无条件注入。老部署缺此文件时静默跳过——丢的只是
         * 崩溃报告能力，不阻塞正常链接。 */
        const char *rt_crash_obj = NULL;
#ifdef ZAN_RT_CRASH_OBJ
        if (!cross_compiling) {
            snprintf(rt_crash_buf, sizeof(rt_crash_buf), "%s/%s",
                     link_exe_dir, zan_path_basename(ZAN_RT_CRASH_OBJ));
            if (zan_file_exists(rt_crash_buf)) rt_crash_obj = rt_crash_buf;
        }
#endif
        /* Hardware-accelerated crypto: an ARCHIVE, not a bare object. A bare
           .o is loaded whole (section GC cannot trim it), so every program
           used to carry the whole SHA/AES/SM3/SM4/AVX2 block even when it
           called none of it. As an archive member the linker pulls it only
           when the program actually references a zan_hw_* symbol. Always
           injected -- which symbols are needed is only known at link time. */
        const char *rt_crypto_archive = NULL;
#ifdef ZAN_RT_CRYPTO_ARCHIVE
        if (!cross_compiling) {
            snprintf(rt_crypto_buf, sizeof(rt_crypto_buf), "%s/%s",
                     link_exe_dir, zan_path_basename(ZAN_RT_CRYPTO_ARCHIVE));
            if (zan_file_exists(rt_crypto_buf)) rt_crypto_archive = rt_crypto_buf;
        }
#endif
        /* 小对象内存分配器管理池 */
        bool fast_alloc = fast_alloc_opt >= 0;
        const char *rt_mem_obj = NULL;
#ifdef ZAN_RT_MEM_OBJ
        if (fast_alloc && !cross_compiling &&
            target.os != ZAN_OS_MACOS && target.os != ZAN_OS_IOS) {
            snprintf(rt_mem_buf, sizeof(rt_mem_buf), "%s/%s",
                     link_exe_dir, zan_path_basename(ZAN_RT_MEM_OBJ));
            if (zan_file_exists(rt_mem_buf)) rt_mem_obj = rt_mem_buf;
        }
#endif
        if (fast_alloc_opt == 1 && !rt_mem_obj) {
            fprintf(stderr,
                    "warning: --fast-alloc ignored: no allocator object for "
                    "this target\n");
        }

        const char *target_rt_sub = NULL;
        if (cross_compiling) {
            const char *tsub = NULL;
            if (target.os == ZAN_OS_LINUX) {
                tsub = (target.arch == ZAN_ARCH_AARCH64) ? "linux-arm64"
                     : (target.arch == ZAN_ARCH_RISCV64) ? "linux-riscv64"
                     : "linux-musl";
            } else if (target.os == ZAN_OS_ANDROID) {
                tsub = (target.arch == ZAN_ARCH_AARCH64) ? "android-arm64"
                                                         : "android-x64";
            } else if (target.os == ZAN_OS_OHOS) {
                tsub = (target.arch == ZAN_ARCH_AARCH64) ? "ohos-arm64"
                                                         : "ohos-x64";
            } else if (target.os == ZAN_OS_WINDOWS) {
                tsub = (target.arch == ZAN_ARCH_AARCH64) ? "win-arm64" : "win-x64";
            } else if (target.os == ZAN_OS_MACOS) {
                tsub = (target.arch == ZAN_ARCH_AARCH64) ? "macos/arm64"
                                                         : "macos/x64";
            } else if (target.os == ZAN_OS_IOS) {
                tsub = "ios/arm64";
            }
            if (tsub) {
                snprintf(rt_timer_buf, sizeof(rt_timer_buf),
                         "%s/%s/zanrt_timer.o", link_exe_dir, tsub);
                if (!zan_file_exists(rt_timer_buf)) {
                    fprintf(stderr,
                            "error: bundled %s timer runtime not found at '%s'; "
                            "reinstall zan or rebuild with toolchain/%s present "
                            "(see scripts/build_*_rt.sh)\n",
                            tsub, rt_timer_buf, tsub);
                    generated_object_vec_remove(&generated_objects, obj_tmp);
                    zan_diag_free_buffers(irgen.diag);
                    zan_irgen_destroy(&irgen);
                    zan_arena_free(ir_arena);
                    zan_arena_free(arena);
                    free(source);
                    return 1;
                }
                rt_timer_obj = rt_timer_buf;
                /* Cross runtimes reference the target crash reporter too;
                   never resolve this dependency to a host object. */
                snprintf(rt_crash_buf, sizeof(rt_crash_buf),
                         "%s/%s/zanrt_crash.o", link_exe_dir, tsub);
                rt_crash_obj = zan_file_exists(rt_crash_buf) ? rt_crash_buf : NULL;
                target_rt_sub = tsub;
                /* The crypto archive follows the same layout: one per
                   cross target, next to the target's other runtime objects
                   (toolchain/<tsub>/libzancrypto.a, staged into
                   <exe_dir>/<tsub>/ by CMake and publish_ide.ps1).
                   Assign unconditionally: rt_crypto_buf is shared with the
                   native path above, so leaving the old pointer in place would
                   hand the linker a stale path (and, for a target without an
                   archive, a path that does not exist at all). */
                snprintf(rt_crypto_buf, sizeof(rt_crypto_buf), "%s/%s/libzancrypto.a",
                         link_exe_dir, tsub);
                rt_crypto_archive = zan_file_exists(rt_crypto_buf) ? rt_crypto_buf : NULL;
            }
        }
        if (cross_compiling && rt_sync_obj && target.os != ZAN_OS_LINUX
            && target.os != ZAN_OS_ANDROID && target.os != ZAN_OS_OHOS
            && target.os != ZAN_OS_MACOS && target.os != ZAN_OS_WINDOWS
            && target.os != ZAN_OS_IOS) {
            int needs_sync = 1;
            if (target.os == ZAN_OS_WASI) {
                static const char *const disp_pre[] = {
                    "zan_dispatch_", NULL
                };
                static const char *const other_sync_pre[] = {
                    "zan_atomic_int_", "zan_shared_table_",
                    "zan_thread_", "zan_monitor_",
                    "zan_monotonic_", "zan_plat_",
                    "zan_exe_dir_into", "zan_dir_list_into",
                    NULL
                };
                static const char *const gui_pre[] = { "zan_gui_", NULL };
                int has_disp = wasm_obj_vec_refs_any(&generated_objects, disp_pre);
                int has_other = wasm_obj_vec_refs_any(&generated_objects, other_sync_pre);
                needs_sync = (has_other || has_disp)
                    && !wasm_obj_vec_refs_any(&generated_objects, gui_pre);
            }
            if (needs_sync) {
                fprintf(stderr,
                        "error: AtomicInt and SharedTable are not available for "
                        "this cross-compilation target yet\n");
                generated_object_vec_remove(&generated_objects, obj_tmp);
                zan_diag_free_buffers(irgen.diag);
                zan_irgen_destroy(&irgen);
                zan_arena_free(ir_arena);
                zan_arena_free(arena);
                free(source);
                return 1;
            }
        }

        char zan_lib_dirs[ZAN_LINK_MAX_DIRS][512]; int zan_lib_ndirs = 0;
        {
            const char *lp = getenv("ZAN_LIB_PATH");
            if (lp && *lp) {
#ifdef _WIN32
                const char sep = ';';
#else
                const char sep = ':';
#endif
                const char *p = lp;
                while (*p && zan_lib_ndirs < ZAN_LINK_MAX_DIRS) {
                    const char *e = strchr(p, sep);
                    size_t n = e ? (size_t)(e - p) : strlen(p);
                    if (n > 0 && n < sizeof(zan_lib_dirs[0])) {
                        memcpy(zan_lib_dirs[zan_lib_ndirs], p, n);
                        zan_lib_dirs[zan_lib_ndirs][n] = '\0';
                        zan_lib_ndirs++;
                    }
                    if (!e) break;
                    p = e + 1;
                }
            }
        }

#ifdef __APPLE__
        {
            char home_lib[512]; home_lib[0] = '\0';
            const char *home = getenv("HOME");
            if (home && *home) {
                snprintf(home_lib, sizeof(home_lib), "%s/.homebrew/lib", home);
            }
            const char *mac_lib_dirs[] = {
                home_lib[0] ? home_lib : NULL,
                "/opt/homebrew/lib",
                "/usr/local/lib",
                NULL,
            };
            for (int ci = 0; mac_lib_dirs[ci]; ci++) {
                if (zan_lib_ndirs >= 16) break;
                if (!mac_lib_dirs[ci][0]) continue;
                if (access(mac_lib_dirs[ci], F_OK) != 0) continue;
                int dup = 0;
                for (int dj = 0; dj < zan_lib_ndirs; dj++)
                    if (strcmp(zan_lib_dirs[dj], mac_lib_dirs[ci]) == 0) { dup = 1; break; }
                if (dup) continue;
                snprintf(zan_lib_dirs[zan_lib_ndirs++], sizeof(zan_lib_dirs[0]),
                         "%s", mac_lib_dirs[ci]);
            }
        }
#endif

        for (int d = 0; d < used_driver_count; d++) {
            if (!driver_dirs[d][0]) continue;
            char linkdir[1100];
            bool added_shared = false;
            bool want_static = link_static_drivers &&
                !used_driver_runtime[d] && cross_dylib_count == 0;
            char archive[1200];
            archive[0] = '\0';
            if (want_static) {
                snprintf(archive, sizeof(archive), "%s/static/lib%.*s.a",
                         driver_dirs[d], used_driver_len[d],
                         used_drivers[d]);
            }
            if (want_static && zan_file_exists(archive)) {
                used_driver_static[d] = true;
                snprintf(linkdir, sizeof(linkdir), "%s/static", driver_dirs[d]);
                char libs_manifest[1200];
                snprintf(libs_manifest, sizeof(libs_manifest),
                         "%s/static/%.*s.libs", driver_dirs[d],
                         used_driver_len[d], used_drivers[d]);
                if (static_driver_lib_count >= ZAN_MAX_STATIC_DRV_LIBS)
                    link_cap_exceeded("static driver libraries",
                                      ZAN_MAX_STATIC_DRV_LIBS);
                static_driver_lib_count += zan_read_static_libs(
                    libs_manifest,
                    &static_driver_libs[static_driver_lib_count],
                    ZAN_MAX_STATIC_DRV_LIBS - static_driver_lib_count, target.os);
            } else {
                if (want_static) {
                    fprintf(stderr,
                            "note: no static archive for driver '%.*s' "
                            "(expected '%s'); falling back to the shared "
                            "driver, which will still be published beside "
                            "the executable\n",
                            used_driver_len[d], used_drivers[d], archive);
                }
                snprintf(linkdir, sizeof(linkdir), "%s", driver_dirs[d]);
            }
            if (zan_lib_ndirs < ZAN_LINK_MAX_DIRS && strlen(linkdir) < sizeof(zan_lib_dirs[0])) {
                memmove(&zan_lib_dirs[1], &zan_lib_dirs[0],
                        (size_t)zan_lib_ndirs * sizeof(zan_lib_dirs[0]));
                snprintf(zan_lib_dirs[0], sizeof(zan_lib_dirs[0]), "%s", linkdir);
                zan_lib_ndirs++;
                added_shared = true;
            }
            if (added_shared && !link_static_drivers) {
                char statdir[1100];
                snprintf(statdir, sizeof(statdir), "%s/static", driver_dirs[d]);
                if (zan_lib_ndirs < ZAN_LINK_MAX_DIRS && zan_file_exists(statdir) &&
                    strlen(statdir) < sizeof(zan_lib_dirs[0])) {
                    memmove(&zan_lib_dirs[2], &zan_lib_dirs[1],
                            (size_t)(zan_lib_ndirs - 1) * sizeof(zan_lib_dirs[0]));
                    snprintf(zan_lib_dirs[1], sizeof(zan_lib_dirs[1]), "%s", statdir);
                    zan_lib_ndirs++;
                }
            }
        }

        if (link_static_drivers && resolved_stdlib_root[0]) {
            const char *dsub = zan_driver_subdir(&target);
            for (int li = 0; li < static_driver_lib_count; li++) {
                const char *entry = static_driver_libs[li];
                if (entry[0] != '-' || entry[1] != 'l' ||
                    !entry[2] || !zan_is_safe_bundle_name(entry + 2))
                    continue;
                bool resolved = false;
                for (int di = 0; di < zan_lib_ndirs && !resolved; di++) {
                    char candidate[1200];
                    int n = snprintf(candidate, sizeof(candidate),
                                     "%s/lib%s.a", zan_lib_dirs[di], entry + 2);
                    if (n >= 0 && (size_t)n < sizeof(candidate) &&
                        zan_file_exists(candidate)) {
                        resolved = true;
                        continue;
                    }
#ifdef _WIN32
                    n = snprintf(candidate, sizeof(candidate),
                                 "%s/%s.lib", zan_lib_dirs[di], entry + 2);
                    if (n >= 0 && (size_t)n < sizeof(candidate) &&
                        zan_file_exists(candidate))
                        resolved = true;
#endif
                }
                if (resolved) continue;
                char dependency_dir[1200];
                if (zan_find_static_library(resolved_stdlib_root, dsub,
                                            entry + 2, dependency_dir,
                                            sizeof(dependency_dir))) {
                    bool duplicate = false;
                    for (int di = 0; di < zan_lib_ndirs; di++)
                        if (strcmp(zan_lib_dirs[di], dependency_dir) == 0) {
                            duplicate = true;
                            break;
                        }
                    if (duplicate) continue;
                    if (zan_lib_ndirs >= 16) {
                        fprintf(stderr,
                                "warning: static-driver dependency search "
                                "path limit reached; ignoring '%s' for '%s'\n",
                                dependency_dir, entry);
                        continue;
                    }
                    memmove(&zan_lib_dirs[1], &zan_lib_dirs[0],
                            (size_t)zan_lib_ndirs * sizeof(zan_lib_dirs[0]));
                    snprintf(zan_lib_dirs[0], sizeof(zan_lib_dirs[0]), "%s",
                             dependency_dir);
                    zan_lib_ndirs++;
                    continue;
                }
#ifdef _WIN32
                if (zan_win_system_lib(entry + 2, (int)strlen(entry + 2)))
                    continue;
#endif
                fprintf(stderr,
                        "warning: static-driver library '%s' could not be "
                        "resolved for target '%s'\n", entry, dsub);
            }
        }

        if (emit_lib) {
            if (!lib_shared) {
                if (zan_write_static_lib(obj_tmp, obj_path, target.os) != 0) {
                    fprintf(stderr, "error: failed to write static library '%s'\n",
                            obj_path);
                    generated_object_vec_remove(&generated_objects, obj_tmp);
                    zan_diag_free_buffers(irgen.diag);
                    zan_irgen_destroy(&irgen);
                    zan_arena_free(ir_arena);
                    zan_arena_free(arena);
                    free(source);
                    return 1;
                }
                link_ret = 0;
            } else {
                if (cross_compiling
                    && (rt_io_obj || rt_sync_obj || rt_file_obj
                        || rt_decimal_obj || rt_embed_obj)) {
                    if (!target_rt_sub) {
                        fprintf(stderr,
                                "error: no bundled runtime objects for this "
                                "cross-compilation target; link a static "
                                "library instead\n");
                        generated_object_vec_remove(&generated_objects, obj_tmp);
                        zan_diag_free_buffers(irgen.diag);
                        zan_irgen_destroy(&irgen);
                        zan_arena_free(ir_arena);
                        zan_arena_free(arena);
                        free(source);
                        return 1;
                    }
                    const char *missing = NULL;
                    if (rt_io_obj) {
                        snprintf(rt_io_buf, sizeof(rt_io_buf), "%s/%s/%s",
                                 link_exe_dir, target_rt_sub,
                                 external_async_executor ? "zanrt_io_mt.o" : "zanrt_io.o");
                        rt_io_obj = rt_io_buf;
                        if (!zan_file_exists(rt_io_obj)) missing = rt_io_obj;
                    }
                    if (rt_sync_obj) {
                        snprintf(rt_sync_buf, sizeof(rt_sync_buf),
                                 "%s/%s/zanrt_sync.o", link_exe_dir,
                                 target_rt_sub);
                        rt_sync_obj = rt_sync_buf;
                        if (!zan_file_exists(rt_sync_obj)) missing = rt_sync_obj;
                    }
                    if (rt_file_obj) {
                        snprintf(rt_file_buf, sizeof(rt_file_buf),
                                 "%s/%s/zanrt_file.o", link_exe_dir,
                                 target_rt_sub);
                        rt_file_obj = rt_file_buf;
                        if (!zan_file_exists(rt_file_obj)) missing = rt_file_obj;
                    }
                    if (rt_decimal_obj) {
                        snprintf(rt_decimal_buf, sizeof(rt_decimal_buf),
                                 "%s/%s/zanrt_decimal.o", link_exe_dir,
                                 target_rt_sub);
                        rt_decimal_obj = rt_decimal_buf;
                        if (!zan_file_exists(rt_decimal_obj)) missing = rt_decimal_obj;
                    }
                    if (rt_embed_obj) {
                        snprintf(rt_embed_buf, sizeof(rt_embed_buf),
                                 "%s/%s/zan_embed_api.o", link_exe_dir,
                                 target_rt_sub);
                        rt_embed_obj = rt_embed_buf;
                        if (!zan_file_exists(rt_embed_obj)) missing = rt_embed_obj;
                    }
                    if (rt_inflate_obj) {
                        snprintf(rt_inflate_buf, sizeof(rt_inflate_buf),
                                 "%s/%s/zan_inflate.o", link_exe_dir,
                                 target_rt_sub);
                        rt_inflate_obj = rt_inflate_buf;
                        if (!zan_file_exists(rt_inflate_obj)) missing = rt_inflate_obj;
                    }
                    if (missing) {
                        fprintf(stderr,
                                "error: bundled %s runtime object not found at "
                                "'%s'; reinstall zan or rebuild with "
                                "toolchain/%s present (see "
                                "scripts/build_*_rt.sh)\n",
                                target_rt_sub, missing, target_rt_sub);
                        generated_object_vec_remove(&generated_objects, obj_tmp);
                        zan_diag_free_buffers(irgen.diag);
                        zan_irgen_destroy(&irgen);
                        zan_arena_free(ir_arena);
                        zan_arena_free(arena);
                        free(source);
                        return 1;
                    }
                }
                char cmd[8192];
                bool lib_spawned = false;
                char exe_dir[1024];
                zan_exe_dir(exe_dir, sizeof(exe_dir));
                char implib[1100] = {0};
