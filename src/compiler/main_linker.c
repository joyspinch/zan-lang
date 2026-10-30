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
                if (target.os == ZAN_OS_WINDOWS) {
                    const char *sep = strrchr(obj_path, '\\');
                    if (!sep) sep = strrchr(obj_path, '/');
                    const char *base = sep ? sep + 1 : obj_path;
                    int llen;
                    const char *lname = zan_dllimport_lname(
                        base, (int)strlen(base), &llen);
                    if (!lname) { lname = base; llen = (int)strlen(base); }
                    size_t plen = (size_t)(base - obj_path);
                    snprintf(implib, sizeof(implib), "%.*slib%.*s.a",
                             (int)plen, obj_path, llen, lname);
                }
                if (target.os == ZAN_OS_WINDOWS) {
#ifdef _WIN32
                    char dll_exe_dir[1024]; dll_exe_dir[0] = '\0';
                    GetModuleFileNameA(NULL, dll_exe_dir, sizeof(dll_exe_dir));
                    { char *s = strrchr(dll_exe_dir, '\\'); if (s) *s = '\0'; }
                    char ld_path[1200];
                    snprintf(ld_path, sizeof(ld_path), "%s\\ld.exe", dll_exe_dir);
                    const char *dll_ld = ld_path;
                    if (!zan_file_exists(ld_path)) {
                        dll_ld = "ld.lld";
                    }
                    const char *argv[ZAN_LINK_MAX_ARGV];
                    int a = 0;
                    argv[a++] = dll_ld;
                    argv[a++] = "-m";
                    argv[a++] = (target.arch == ZAN_ARCH_AARCH64)
                                    ? "arm64pe" : "i386pep";
                    argv[a++] = "-shared";
                    argv[a++] = "-Bdynamic";
                    char dllcrt2[1300];
                    snprintf(dllcrt2, sizeof(dllcrt2),
                             "%s\\mingw\\lib\\dllcrt2.o", dll_exe_dir);
                    const char *dll_entry =
                        zan_file_exists(dllcrt2) ? "DllMainCRTStartup"
                                                 : "DllMain";
                    argv[a++] = "-e";
                    argv[a++] = dll_entry;
                    argv[a++] = "-o";
                    argv[a++] = obj_path;
                    argv[a++] = obj_tmp;
                    if (zan_file_exists(dllcrt2))
                        argv[a++] = dllcrt2;
                    if (rt_io_obj) argv[a++] = rt_io_obj;
                    if (rt_sync_obj) argv[a++] = rt_sync_obj;
                    if (rt_file_obj) argv[a++] = rt_file_obj;
                    if (rt_decimal_obj) argv[a++] = rt_decimal_obj;
                    if (rt_embed_obj) argv[a++] = rt_embed_obj;
                    if (rt_inflate_obj) argv[a++] = rt_inflate_obj;
                    if (rt_timer_obj) argv[a++] = rt_timer_obj;
                    if (rt_crypto_archive) argv[a++] = rt_crypto_archive;
                    argv[a++] = "-out-implib";
                    argv[a++] = implib;
                    char ldirbufs[ZAN_LINK_MAX_DIRS * 2 + 4][520]; int nld = 0;
                    if (dll_exe_dir[0] &&
                        zan_file_exists(dll_exe_dir + 0) ) {
                        char mlib[1300];
                        snprintf(mlib, sizeof(mlib), "%s\\mingw\\lib",
                                 dll_exe_dir);
                        if (zan_file_exists(mlib)) {
                            snprintf(ldirbufs[nld], sizeof(ldirbufs[0]),
                                     "-L%s", mlib);
                            argv[a++] = ldirbufs[nld++];
                        }
                    }
                    for (int di = 0; di < zan_lib_ndirs; di++) {
                        if (a >= ZAN_LINK_MAX_ARGV - ZAN_LINK_ARGV_TAIL)
                            link_cap_exceeded("linker arguments",
                                              ZAN_LINK_MAX_ARGV);
                        snprintf(ldirbufs[nld], sizeof(ldirbufs[0]),
                                 "-L%s", zan_lib_dirs[di]);
                        argv[a++] = ldirbufs[nld++];
                    }
                    for (int di = 0; di < extra_lib_path_count; di++) {
                        if (a >= ZAN_LINK_MAX_ARGV - ZAN_LINK_ARGV_TAIL)
                            link_cap_exceeded("linker arguments",
                                              ZAN_LINK_MAX_ARGV);
                        snprintf(ldirbufs[nld], sizeof(ldirbufs[0]),
                                 "-L%s", extra_lib_paths[di]);
                        argv[a++] = ldirbufs[nld++];
                    }
                    argv[a++] = "--start-group";
                    static const char *const dllcrt[] = {
                        "-lmingw32", "-lgcc", "-lmoldname", "-lmingwex",
                        "-lmsvcrt", "-lkernel32", "-lshell32",
                        "-l:libwinpthread.a", NULL };
                    for (int li = 0; dllcrt[li]; li++) {
                        if (a >= ZAN_LINK_MAX_ARGV - ZAN_LINK_ARGV_TAIL)
                            link_cap_exceeded("linker arguments", ZAN_LINK_MAX_ARGV);
                        argv[a++] = dllcrt[li];
                    }
                    if (rt_io_obj) argv[a++] = "-lws2_32";
                    char libbufs[ZAN_LINK_MAX_LIBS][128]; int nb = 0;
                    for (int li = 0; li < irgen.extern_lib_count; li++) {
                        if (nb >= ZAN_LINK_MAX_LIBS ||
                            a >= ZAN_LINK_MAX_ARGV - ZAN_LINK_ARGV_TAIL)
                            link_cap_exceeded("DllImport libraries", ZAN_LINK_MAX_LIBS);
                        int nlen;
                        const char *nm = zan_dllimport_lname(
                            irgen.extern_libs[li].str,
                            (int)irgen.extern_libs[li].len, &nlen);
                        if (!nm) continue;
                        snprintf(libbufs[nb], sizeof(libbufs[0]),
                                 "-l%.*s", nlen, nm);
                        argv[a++] = libbufs[nb++];
                    }
                    for (int li = 0; li < static_driver_lib_count; li++) {
                        if (a >= ZAN_LINK_MAX_ARGV - ZAN_LINK_ARGV_TAIL)
                            link_cap_exceeded("linker arguments", ZAN_LINK_MAX_ARGV);
                        argv[a++] = static_driver_libs[li];
                    }
                    argv[a++] = "--end-group";
                    argv[a] = NULL;
                    if (getenv("ZAN_VERBOSE_LINK")) {
                        fprintf(stderr, "[link]");
                        for (int ai = 0; ai < a; ai++)
                            fprintf(stderr, " %s", argv[ai]);
                        fprintf(stderr, "\n");
                    }
                    link_ret = (int)zan_utf8_spawnv(_P_WAIT, dll_ld, argv);
                    lib_spawned = true;
#else
                    const char *wsub = (target.arch == ZAN_ARCH_AARCH64)
                                       ? "arm64pe" : "i386pep";
                    snprintf(cmd, sizeof(cmd),
                             "ld.lld -m %s -shared -e DllMain -o \"%s\" \"%s\""
                             " -out-implib \"%s\"",
                             wsub, obj_path, obj_tmp, implib);
                    { cmd_appendf(cmd, sizeof(cmd),
                               " -lmingw32 -lmoldname -lmingwex -lmsvcrt"
                               " -lkernel32 -lshell32"); }
                    for (int di = 0; di < zan_lib_ndirs; di++) {
                        cmd_appendf(cmd, sizeof(cmd), " -L\"%s\"",
                                 zan_lib_dirs[di]);
                    }
                    for (int di = 0; di < extra_lib_path_count; di++) {
                        cmd_appendf(cmd, sizeof(cmd), " -L\"%s\"",
                                 extra_lib_paths[di]);
                    }
                    for (int li = 0; li < irgen.extern_lib_count; li++) {
                        int nlen;
                        const char *nm = zan_dllimport_lname(
                            irgen.extern_libs[li].str,
                            (int)irgen.extern_libs[li].len, &nlen);
                        if (!nm) continue;
                        if (target.os == ZAN_OS_MACOS || target.os == ZAN_OS_IOS) {
                            for (int d = 0; d < used_driver_count; d++) {
                                if (used_driver_runtime[d] ||
                                    used_driver_static[d] ||
                                    used_driver_len[d] != nlen ||
                                    memcmp(used_drivers[d], nm,
                                           (size_t)nlen) != 0)
                                    continue;
                                char primary[1200], fallback[1200];
                                snprintf(primary, sizeof(primary),
                                         "%s/lib%.*s.dylib", driver_dirs[d],
                                         nlen, nm);
                                if (!zan_file_exists(primary) &&
                                    zan_find_macos_driver_dylib(
                                        driver_dirs[d], nm, nlen,
                                        fallback, sizeof(fallback))) {
                                    cmd_appendf(cmd, sizeof(cmd), " \"%s\"",
                                             fallback);
                                    nm = NULL;
                                }
                                break;
                            }
                            if (!nm) continue;
                        }
                        cmd_appendf(cmd, sizeof(cmd), " -l%.*s",
                                 nlen, nm);
                    }
                    for (int li = 0; li < static_driver_lib_count; li++) {
                        cmd_appendf(cmd, sizeof(cmd), " %s",
                                 static_driver_libs[li]);
                    }
                    if (getenv("ZAN_VERBOSE_LINK"))
                        fprintf(stderr, "[link] %s\n", cmd);
                    link_ret = system(cmd);
#endif
                } else if (target.os == ZAN_OS_LINUX) {
                    snprintf(cmd, sizeof(cmd),
                             "ld.lld -shared -o \"%s\" \"%s\"", obj_path, obj_tmp);
                    if (rt_io_obj) {
                        cmd_appendf(cmd, sizeof(cmd), " \"%s\"",
                                 rt_io_obj);
                    }
                    if (rt_sync_obj) {
                        cmd_appendf(cmd, sizeof(cmd), " \"%s\"",
                                 rt_sync_obj);
                    }
                    if (rt_file_obj) {
                        cmd_appendf(cmd, sizeof(cmd), " \"%s\"",
                                 rt_file_obj);
                    }
                    if (rt_decimal_obj) {
                        cmd_appendf(cmd, sizeof(cmd), " \"%s\"",
                                 rt_decimal_obj);
                    }
                    if (rt_embed_obj) {
                        cmd_appendf(cmd, sizeof(cmd), " \"%s\"",
                                 rt_embed_obj);
                    }
                    if (rt_inflate_obj) {
                        cmd_appendf(cmd, sizeof(cmd), " \"%s\"",
                                 rt_inflate_obj);
                    }
                    if (rt_timer_obj) {
                        cmd_appendf(cmd, sizeof(cmd), " \"%s\"",
                                 rt_timer_obj);
                    }
                    if (rt_crypto_archive) {
                        cmd_appendf(cmd, sizeof(cmd), " \"%s\"",
                                 rt_crypto_archive);
                    }
                } else if (target.os == ZAN_OS_MACOS) {
                    const char *march = (target.arch == ZAN_ARCH_AARCH64)
                                        ? "arm64" : "x86_64";
                    char tbd[1200];
                    snprintf(tbd, sizeof(tbd), "%s/macos/libSystem.tbd",
                             exe_dir);
                    if (!zan_file_exists(tbd)) {
                        fprintf(stderr,
                                "error: bundled macOS libSystem stub not found "
                                "at '%s'; reinstall zan or rebuild with "
                                "toolchain/macos present\n", tbd);
                        generated_object_vec_remove(&generated_objects, obj_tmp);
                        zan_diag_free_buffers(irgen.diag);
                        zan_irgen_destroy(&irgen);
                        zan_arena_free(ir_arena);
                        zan_arena_free(arena);
                        free(source);
                        return 1;
                    }
                    snprintf(cmd, sizeof(cmd),
                             "ld64.lld -dylib -arch %s -platform_version macos "
                             "11.0 11.0 -o \"%s\" \"%s\" \"%s\"",
                             march, obj_path, obj_tmp, tbd);
                    if (rt_io_obj) {
                        cmd_appendf(cmd, sizeof(cmd), " \"%s\"",
                                 rt_io_obj);
                    }
                    if (rt_sync_obj) {
                        cmd_appendf(cmd, sizeof(cmd), " \"%s\"",
                                 rt_sync_obj);
                    }
                    if (rt_file_obj) {
                        cmd_appendf(cmd, sizeof(cmd), " \"%s\"",
                                 rt_file_obj);
                    }
                    if (rt_decimal_obj) {
                        cmd_appendf(cmd, sizeof(cmd), " \"%s\"",
                                 rt_decimal_obj);
                    }
                    if (rt_embed_obj) {
                        cmd_appendf(cmd, sizeof(cmd), " \"%s\"",
                                 rt_embed_obj);
                    }
                    if (rt_inflate_obj) {
                        cmd_appendf(cmd, sizeof(cmd), " \"%s\"",
                                 rt_inflate_obj);
                    }
                    if (rt_timer_obj) {
                        cmd_appendf(cmd, sizeof(cmd), " \"%s\"",
                                 rt_timer_obj);
                    }
                    if (rt_crypto_archive) {
                        cmd_appendf(cmd, sizeof(cmd), " \"%s\"",
                                 rt_crypto_archive);
                    }
                } else if (cross_compiling &&
                           target.os == ZAN_OS_ANDROID) {
                    char exe_dir3[1024] = {0};
                    zan_exe_dir(exe_dir3, sizeof(exe_dir3));
                    const char *asub3 = (target.arch == ZAN_ARCH_AARCH64)
                                        ? "android-arm64" : "android-x64";
                    char sys3[1200];
                    snprintf(sys3, sizeof(sys3), "%s/%s", exe_dir3, asub3);
                    char probe3[1300];
                    snprintf(probe3, sizeof(probe3),
                             "%s/android_native_app_glue.o", sys3);
                    if (!zan_file_exists(probe3)) {
                        fprintf(stderr,
                                "error: bundled %s sysroot subset not found "
                                "at '%s'; reinstall zan or rebuild with "
                                "toolchain/%s present\n",
                                asub3, sys3, asub3);
                        generated_object_vec_remove(&generated_objects, obj_tmp);
                        zan_diag_free_buffers(irgen.diag);
                        zan_irgen_destroy(&irgen);
                        zan_arena_free(ir_arena);
                        zan_arena_free(arena);
                        free(source);
                        return 1;
                    }
                    snprintf(cmd, sizeof(cmd),
                             "ld.lld -shared --no-undefined -o \"%s\""
                             " \"%s/android_native_app_glue.o\" \"%s\"",
                             obj_path, sys3, obj_tmp);
                    for (int di = 0; di < zan_lib_ndirs; di++) {
                        cmd_appendf(cmd, sizeof(cmd), " -L\"%s\"",
                                 zan_lib_dirs[di]);
                    }
                    for (int di = 0; di < extra_lib_path_count; di++) {
                        cmd_appendf(cmd, sizeof(cmd), " -L\"%s\"",
                                 extra_lib_paths[di]);
                    }
                    if (rt_io_obj) {
                        cmd_appendf(cmd, sizeof(cmd), " \"%s\"",
                                 rt_io_obj);
                    }
                    if (rt_sync_obj) {
                        cmd_appendf(cmd, sizeof(cmd), " \"%s\"",
                                 rt_sync_obj);
                    }
                    if (rt_file_obj) {
                        cmd_appendf(cmd, sizeof(cmd), " \"%s\"",
                                 rt_file_obj);
                    }
                    if (rt_decimal_obj) {
                        cmd_appendf(cmd, sizeof(cmd), " \"%s\"",
                                 rt_decimal_obj);
                    }
                    if (rt_embed_obj) {
                        cmd_appendf(cmd, sizeof(cmd), " \"%s\"",
                                 rt_embed_obj);
                    }
                    if (rt_inflate_obj) {
                        cmd_appendf(cmd, sizeof(cmd), " \"%s\"",
                                 rt_inflate_obj);
                    }
                    if (rt_timer_obj) {
                        cmd_appendf(cmd, sizeof(cmd), " \"%s\"",
                                 rt_timer_obj);
                    }
                    if (rt_crypto_archive) {
                        cmd_appendf(cmd, sizeof(cmd), " \"%s\"",
                                 rt_crypto_archive);
                    }
                    for (int li = 0; li < static_driver_lib_count; li++) {
                        cmd_appendf(cmd, sizeof(cmd), " %s",
                                 static_driver_libs[li]);
                    }
                    for (int li = 0; li < irgen.extern_lib_count; li++) {
                        if (zan_win_system_lib(irgen.extern_libs[li].str,
                                               (int)irgen.extern_libs[li].len))
                            continue;
                        int nlen;
                        const char *nm = zan_dllimport_lname(
                            irgen.extern_libs[li].str,
                            (int)irgen.extern_libs[li].len, &nlen);
                        if (!nm) continue;
                        cmd_appendf(cmd, sizeof(cmd), " -l%.*s",
                                 nlen, nm);
                    }
                    { cmd_appendf(cmd, sizeof(cmd),
                               " \"%s/libc.so\" \"%s/libm.so\""
                               " \"%s/liblog.so\" \"%s/libdl.so\"",
                               sys3, sys3, sys3, sys3); }
                    { cmd_appendf(cmd, sizeof(cmd),
                               " \"%s/libandroid.so\" \"%s/libEGL.so\""
                               " \"%s/libGLESv2.so\" \"%s/libaaudio.so\"",
                               sys3, sys3, sys3, sys3); }
                    { cmd_appendf(cmd, sizeof(cmd),
                               " \"%s/libclang_rt.builtins.a\"",
                               sys3); }
                    if (getenv("ZAN_VERBOSE_LINK"))
                        fprintf(stderr, "[link] %s\n", cmd);
                    link_ret = system(cmd);
                } else if (target.os == ZAN_OS_OHOS) {
                    const char *osub = (target.arch == ZAN_ARCH_AARCH64)
                                       ? "ohos-arm64" : "ohos-x64";
                    char sys4[1200];
                    snprintf(sys4, sizeof(sys4), "%s/%s", link_exe_dir, osub);
                    char probe4[1300];
                    snprintf(probe4, sizeof(probe4), "%s/zap_main.o", sys4);
                    if (!zan_file_exists(probe4)) {
                        fprintf(stderr,
                                "error: bundled %s sysroot subset not found "
                                "at '%s'; reinstall zan or rebuild with "
                                "toolchain/%s present\n",
                                osub, sys4, osub);
                        generated_object_vec_remove(&generated_objects, obj_tmp);
                        zan_diag_free_buffers(irgen.diag);
                        zan_irgen_destroy(&irgen);
                        zan_arena_free(ir_arena);
                        zan_arena_free(arena);
                        free(source);
                        return 1;
                    }
                    snprintf(cmd, sizeof(cmd),
                             "ld.lld -shared -o \"%s\" \"%s/zap_main.o\""
                             " \"%s\" \"%s/libclang_rt.builtins.a\"",
                             obj_path, sys4, obj_tmp, sys4);
                    /* OHOS 的 GUI 驱动是**预编译产物**（仓库里只有 .a/.so，没有
                       源码），它直接调用 EGL/GLES 与 FreeType。链接期要的是
                       **OHOS NDK 里的同名库**（设备系统提供真库，链接只需要它们
                       满足符号、并把 DT_NEEDED 记进 HAP）。仓库里原先放的是两个
                       "记录 DT_NEEDED 的空壳"，**导不出任何符号**——拿它们去链
                       接 GUI 程序只会得到一串 undefined symbol（eglMakeCurrent /
                       glCreateShader / FT_*）。空壳已删（scripts\ohos_stub.c 一并
                       删掉）；这里改成：有就链上，没有就说清楚缺哪个、去哪拷。
                       只有 EGL/GLES 两项：OHOS 的 FreeType 是**静态**链进驱动
                       的（scripts/build_gui_ohos.sh 从源码编出 libfreetype_*.a
                       再归档），设备上并不存在 libfreetype.so 可链——把它列进来
                       只会让人去找一个哪都没有的文件。 */
                    {
                        static const char *const ohos_gl_libs[] = {
                            "libEGL.so", "libGLESv3.so"
                        };
                        for (size_t gi = 0; gi < sizeof(ohos_gl_libs) / sizeof(ohos_gl_libs[0]); gi++) {
                            char gl[1400];
                            snprintf(gl, sizeof(gl), "%s/%s", sys4, ohos_gl_libs[gi]);
                            if (zan_file_exists(gl)) {
                                cmd_appendf(cmd, sizeof(cmd), " \"%s\"", gl);
                                continue;
                            }
                            fprintf(stderr,
                                    "note: %s is not in the bundled %s subset ('%s'); "
                                    "copy it out of an OHOS sysroot's "
                                    "usr/lib/<triple>/ (an OHOS NDK, or DevEco "
                                    "Studio's sdk/default/openharmony/native/sysroot)"
                                    " with scripts\\build_cross_rt.cmd "
                                    "(TASKS.md A503)\n",
                                    ohos_gl_libs[gi], osub, gl);
                        }
                    }
                    for (int d = 0; d < cross_archive_count; d++) {
                        cmd_appendf(cmd, sizeof(cmd), " \"%s\"",
                                 cross_archives[d]);
                    }
                    if (rt_timer_obj) {
                        cmd_appendf(cmd, sizeof(cmd), " \"%s\"",
                                 rt_timer_obj);
                    }
                    if (rt_crypto_archive) {
                        cmd_appendf(cmd, sizeof(cmd), " \"%s\"",
                                 rt_crypto_archive);
                    }
                    if (irgen.uses_socket_async) {
                        cmd_appendf(cmd, sizeof(cmd),
                                 " \"%s/zanrt_io.o\"", sys4);
                    }
                    if (irgen.uses_sync_runtime) {
                        cmd_appendf(cmd, sizeof(cmd),
                                 " \"%s/zanrt_sync.o\"", sys4);
                    }
                    if (irgen.uses_file_runtime) {
                        cmd_appendf(cmd, sizeof(cmd),
                                 " \"%s/zanrt_file.o\"", sys4);
                    }
                    if (irgen.uses_decimal_runtime) {
                        cmd_appendf(cmd, sizeof(cmd),
                                 " \"%s/zanrt_decimal.o\"", sys4);
                    }
                    if (irgen.uses_embed_api) {
                        cmd_appendf(cmd, sizeof(cmd),
                                 " \"%s/zan_embed_api.o\"", sys4);
                    }
                    if (irgen.uses_inflate) {
                        cmd_appendf(cmd, sizeof(cmd),
                                 " \"%s/zan_inflate.o\"", sys4);
                    }
                    if (getenv("ZAN_VERBOSE_LINK"))
                        fprintf(stderr, "[link] %s\n", cmd);
                    link_ret = system(cmd);
                } else {
                    fprintf(stderr, "error: shared libraries are not supported "
                            "for this target yet\n");
                    generated_object_vec_remove(&generated_objects, obj_tmp);
                    zan_diag_free_buffers(irgen.diag);
                    zan_irgen_destroy(&irgen);
                    zan_arena_free(ir_arena);
                    zan_arena_free(arena);
                    free(source);
                    return 1;
                }
                if (!lib_spawned) {
                    for (int di = 0; di < zan_lib_ndirs; di++) {
                        cmd_appendf(cmd, sizeof(cmd), " -L\"%s\"",
                                 zan_lib_dirs[di]);
                    }
                    for (int di = 0; di < extra_lib_path_count; di++) {
                        cmd_appendf(cmd, sizeof(cmd), " -L\"%s\"",
                                 extra_lib_paths[di]);
                    }
                    for (int li = 0; li < irgen.extern_lib_count; li++) {
                        if (target.os != ZAN_OS_WINDOWS &&
                            zan_win_system_lib(irgen.extern_libs[li].str,
                                               (int)irgen.extern_libs[li].len))
                            continue;
                        int nlen;
                        const char *nm = zan_dllimport_lname(
                            irgen.extern_libs[li].str,
                            (int)irgen.extern_libs[li].len, &nlen);
                        if (!nm) continue;
                        cmd_appendf(cmd, sizeof(cmd), " -l%.*s",
                                 nlen, nm);
                    }
                    for (int li = 0; li < static_driver_lib_count; li++) {
                        cmd_appendf(cmd, sizeof(cmd), " %s",
                                 static_driver_libs[li]);
                    }
                    if (getenv("ZAN_VERBOSE_LINK"))
                        fprintf(stderr, "[link] %s\n", cmd);
                    link_ret = system(cmd);
                }
            }
        } else if (cross_compiling && target.os == ZAN_OS_LINUX) {
            char exe_dir[1024] = {0};
#ifdef _WIN32
            GetModuleFileNameA(NULL, exe_dir, sizeof(exe_dir));
            { char *s = strrchr(exe_dir, '\\'); if (s) *s = '\0'; }
#elif defined(__APPLE__)
            { uint32_t sz = sizeof(exe_dir);
              if (_NSGetExecutablePath(exe_dir, &sz) != 0) exe_dir[0] = '\0';
              char *s = strrchr(exe_dir, '/'); if (s) *s = '\0'; }
#else
            { ssize_t n = readlink("/proc/self/exe", exe_dir, sizeof(exe_dir) - 1);
              if (n > 0) { exe_dir[n] = '\0'; char *s = strrchr(exe_dir, '/'); if (s) *s = '\0'; } }
#endif
            const char *sub = (target.arch == ZAN_ARCH_AARCH64)
                              ? "linux-arm64"
                              : (target.arch == ZAN_ARCH_RISCV64)
                              ? "linux-riscv64" : "linux-musl";
            char sys[1200];
            snprintf(sys, sizeof(sys), "%s/%s", exe_dir, sub);

            char cmd[4096];
            snprintf(cmd, sizeof(cmd),
                     "ld.lld -static%s --gc-sections -o \"%s\" \"%s/crt1.o\" \"%s/crti.o\" \"%s\"",
                     (publish_mode || strip_symbols) ? " -s --icf=all -O2" : "", obj_path, sys, sys, obj_tmp);
            for (int di = 0; di < zan_lib_ndirs; di++) {
                cmd_appendf(cmd, sizeof(cmd), " -L\"%s\"",
                         zan_lib_dirs[di]);
            }
            for (int di = 0; di < extra_lib_path_count; di++) {
                cmd_appendf(cmd, sizeof(cmd), " -L\"%s\"",
                         extra_lib_paths[di]);
            }
            if (rt_timer_obj) {
                cmd_appendf(cmd, sizeof(cmd), " \"%s\"", rt_timer_obj);
            }
            if (rt_crypto_archive) {
                cmd_appendf(cmd, sizeof(cmd), " \"%s\"", rt_crypto_archive);
            }
            if (rt_io_obj || irgen.uses_socket_async || external_async_executor) {
                const char *ioname = external_async_executor ? "zanrt_io_mt.o" : "zanrt_io.o";
                char rt_io_path[1400];
                snprintf(rt_io_path, sizeof(rt_io_path), "%s/%s", sys, ioname);
                if (!zan_file_exists(rt_io_path) && external_async_executor) {
                    snprintf(rt_io_path, sizeof(rt_io_path), "%s/zanrt_io.o", sys);
                }
                if (zan_file_exists(rt_io_path)) {
                    cmd_appendf(cmd, sizeof(cmd), " \"%s\"", rt_io_path);
                }
            }
            if (irgen.uses_sync_runtime) {
                cmd_appendf(cmd, sizeof(cmd), " \"%s/zanrt_sync.o\"", sys);
            }
            if (irgen.uses_file_runtime) {
                cmd_appendf(cmd, sizeof(cmd), " \"%s/zanrt_file.o\"", sys);
            }
            if (irgen.uses_decimal_runtime) {
                cmd_appendf(cmd, sizeof(cmd), " \"%s/zanrt_decimal.o\"", sys);
            }
            if (irgen.uses_embed_api) {
                cmd_appendf(cmd, sizeof(cmd), " \"%s/zan_embed_api.o\"", sys);
            }
            if (irgen.uses_inflate) {
                cmd_appendf(cmd, sizeof(cmd), " \"%s/zan_inflate.o\"", sys);
            }
            { char memobj[1300];
              snprintf(memobj, sizeof(memobj), "%s/zanrt_mem.o", sys);
              if (zan_file_exists(memobj)) {
                  cmd_appendf(cmd, sizeof(cmd),
                           " \"%s\" --wrap=malloc --wrap=free"
                           " --wrap=calloc --wrap=realloc", memobj);
              } }
            { cmd_appendf(cmd, sizeof(cmd), " --start-group \"%s/libc.a\"", sys); }
            { char gcclib[1300];
              snprintf(gcclib, sizeof(gcclib), "%s/libgcc.a", sys);
              if (zan_file_exists(gcclib)) {
                  cmd_appendf(cmd, sizeof(cmd), " \"%s\"", gcclib);
              } }
            for (int d = 0; d < cross_archive_count; d++) {
                cmd_appendf(cmd, sizeof(cmd), " \"%s\"", cross_archives[d]);
            }
            {
                static const char *const ft_libs[] = {
                    "libfreetype.a", "libfontconfig.a", "libexpat.a"
                };
                for (int li = 0; li < 3; li++) {
                    char libpath[1300];
                    snprintf(libpath, sizeof(libpath), "%s/%s", sys,
                             ft_libs[li]);
                    if (zan_file_exists(libpath)) {
                        cmd_appendf(cmd, sizeof(cmd), " \"%s\"", libpath);
                    }
                }
            }
            for (int li = 0; li < static_driver_lib_count; li++) {
                cmd_appendf(cmd, sizeof(cmd), " %s",
                         static_driver_libs[li]);
            }
            { cmd_appendf(cmd, sizeof(cmd),
                       " --end-group \"%s/crtn.o\"", sys); }
            link_ret = system(cmd);
        } else if (cross_compiling && target.os == ZAN_OS_OHOS) {
            char exe_dir[1024] = {0};
            zan_exe_dir(exe_dir, sizeof(exe_dir));
            const char *osub = (target.arch == ZAN_ARCH_AARCH64)
                               ? "ohos-arm64" : "ohos-x64";
            char sys[1200];
            snprintf(sys, sizeof(sys), "%s/%s", exe_dir, osub);
            char probe[1300];
            snprintf(probe, sizeof(probe), "%s/libc.a", sys);
            if (!zan_file_exists(probe)) {
                fprintf(stderr,
                        "error: bundled %s OHOS sysroot subset not found at "
                        "'%s'; reinstall zan or rebuild with toolchain/%s "
                        "present\n", osub, sys, osub);
                generated_object_vec_remove(&generated_objects, obj_tmp);
                zan_diag_free_buffers(irgen.diag);
                zan_irgen_destroy(&irgen);
                zan_arena_free(ir_arena);
                zan_arena_free(arena);
                free(source);
                return 1;
            }
            char cmd[8192];
            /* OHOS 的 exe 走**全静态**链接（musl 的 libc.a + 静态 libunwind 与
               compiler-rt），静态模型里没有 EGL/GLES/FreeType 的位置，而预编译
               的 OHOS GUI 驱动直接调用它们 ⇒ 任何 GUI 程序在这条路上都链不过
               （一串 undefined symbol：eglMakeCurrent / glCreateShader / FT_*）。
               与其让用户去猜，直接说清楚：OHOS 的 GUI 走 HAP 形态（--emit-lib），
               exe 形态要等驱动改成运行时 dlopen 才通得过。 */
            {
                /* 判断"这个程序带 GUI 驱动"：驱动静态库进链接行的路径不止一条
                   （跨目标的 cross_archives、目标自己的 static_driver_libs、
                   used_drivers 清单），逐个扫一遍，别只盯 extern_libs——GUI 程序
                   可能一个 DllImport 都不写。 */
                int needs_gui = 0;
                for (int d = 0; d < cross_archive_count && !needs_gui; d++) {
                    if (strstr(cross_archives[d], "libzan_gui")) needs_gui = 1;
                }
                for (int li = 0; li < static_driver_lib_count && !needs_gui; li++) {
                    if (strstr(static_driver_libs[li], "libzan_gui")) needs_gui = 1;
                }
                for (int d = 0; d < used_driver_count && !needs_gui; d++) {
                    if (used_driver_len[d] == 7 &&
                        strncmp(used_drivers[d], "zan_gui", 7) == 0) {
                        needs_gui = 1;
                    }
                }
                if (needs_gui) {
                    fprintf(stderr,
                            "error: an OHOS GUI program cannot be linked into a "
                            "static executable: the OHOS GUI driver calls EGL/GLES "
                            "and FreeType directly, and the static link has no place "
                            "for them.\n"
                            "  use --emit-lib (the HAP shape) for OHOS GUI, or wait "
                            "for the driver to resolve them at run time (dlopen).\n"
                            "  see TASKS.md A503\n");
                    generated_object_vec_remove(&generated_objects, obj_tmp);
                    zan_diag_free_buffers(irgen.diag);
                    zan_irgen_destroy(&irgen);
                    zan_arena_free(ir_arena);
                    zan_arena_free(arena);
                    free(source);
                    return 1;
                }
            }
            snprintf(cmd, sizeof(cmd),
                     "ld.lld -static%s --gc-sections -o \"%s\" \"%s/crt1.o\" \"%s/crti.o\""
                     " \"%s/clang_rt.crtbegin.o\" \"%s\"",
                     (publish_mode || strip_symbols) ? " -s --icf=all -O2" : "", obj_path, sys, sys, sys, obj_tmp);
            for (int di = 0; di < zan_lib_ndirs; di++) {
                cmd_appendf(cmd, sizeof(cmd), " -L\"%s\"",
                         zan_lib_dirs[di]);
            }
            for (int di = 0; di < extra_lib_path_count; di++) {
                cmd_appendf(cmd, sizeof(cmd), " -L\"%s\"",
                         extra_lib_paths[di]);
            }
            if (rt_timer_obj) {
                cmd_appendf(cmd, sizeof(cmd), " \"%s\"", rt_timer_obj);
            }
            if (rt_crypto_archive) {
                cmd_appendf(cmd, sizeof(cmd), " \"%s\"", rt_crypto_archive);
            }
            if (irgen.uses_socket_async) {
                cmd_appendf(cmd, sizeof(cmd), " \"%s/zanrt_io.o\"", sys);
            }
            if (irgen.uses_sync_runtime) {
                cmd_appendf(cmd, sizeof(cmd), " \"%s/zanrt_sync.o\"", sys);
            }
            if (irgen.uses_file_runtime) {
                cmd_appendf(cmd, sizeof(cmd), " \"%s/zanrt_file.o\"", sys);
            }
            if (irgen.uses_decimal_runtime) {
                cmd_appendf(cmd, sizeof(cmd), " \"%s/zanrt_decimal.o\"", sys);
            }
            if (irgen.uses_embed_api) {
                cmd_appendf(cmd, sizeof(cmd),
                         " \"%s/zan_embed_api.o\"", sys);
            }
            if (irgen.uses_inflate) {
                cmd_appendf(cmd, sizeof(cmd),
                         " \"%s/zan_inflate.o\"", sys);
            }
            { cmd_appendf(cmd, sizeof(cmd),
                       " --start-group \"%s/libc.a\" \"%s/libunwind.a\""
                       " \"%s/libclang_rt.builtins.a\"", sys, sys, sys); }
            for (int d = 0; d < cross_archive_count; d++) {
                cmd_appendf(cmd, sizeof(cmd), " \"%s\"", cross_archives[d]);
            }
            for (int li = 0; li < static_driver_lib_count; li++) {
                cmd_appendf(cmd, sizeof(cmd), " %s",
                         static_driver_libs[li]);
            }
            { cmd_appendf(cmd, sizeof(cmd),
                       " --end-group \"%s/clang_rt.crtend.o\" \"%s/crtn.o\"",
                       sys, sys); }
            if (getenv("ZAN_VERBOSE_LINK"))
                fprintf(stderr, "[link] %s\n", cmd);
            link_ret = system(cmd);
        } else if (cross_compiling && target.os == ZAN_OS_ANDROID) {
            char exe_dir[1024] = {0};
            zan_exe_dir(exe_dir, sizeof(exe_dir));
            const char *asub = (target.arch == ZAN_ARCH_AARCH64)
                               ? "android-arm64" : "android-x64";
            char sys[1200];
            snprintf(sys, sizeof(sys), "%s/%s", exe_dir, asub);
            const char *drv_libs[ZAN_LINK_MAX_LIBS];
            int drv_lib_len[ZAN_LINK_MAX_LIBS];
            int drv_lib_count = 0;
            for (int li = 0; li < irgen.extern_lib_count; li++) {
                if (zan_win_system_lib(irgen.extern_libs[li].str,
                                       (int)irgen.extern_libs[li].len))
                    continue;
                int nlen;
                const char *nm = zan_dllimport_lname(
                    irgen.extern_libs[li].str,
                    (int)irgen.extern_libs[li].len, &nlen);
                if (!nm) continue;
                if (drv_lib_count >= ZAN_LINK_MAX_LIBS)
                    link_cap_exceeded("DllImport libraries", ZAN_LINK_MAX_LIBS);
                drv_libs[drv_lib_count] = nm;
                drv_lib_len[drv_lib_count] = nlen;
                drv_lib_count++;
            }
            char cmd[8192];
            if (drv_lib_count > 0) {
                snprintf(cmd, sizeof(cmd),
                         "ld.lld -pie%s --gc-sections -o \"%s\" \"%s/crtbegin_dynamic.o\""
                         " \"%s\"",
                         (publish_mode || strip_symbols) ? " -s --icf=all -O2" : "", obj_path, sys, obj_tmp);
            } else {
                snprintf(cmd, sizeof(cmd),
                         "ld.lld -static%s --gc-sections -o \"%s\" \"%s/crtbegin_static.o\""
                         " \"%s\"",
                         (publish_mode || strip_symbols) ? " -s --icf=all -O2" : "", obj_path, sys, obj_tmp);
            }
            for (int di = 0; di < zan_lib_ndirs; di++) {
                cmd_appendf(cmd, sizeof(cmd), " -L\"%s\"",
                         zan_lib_dirs[di]);
            }
            for (int di = 0; di < extra_lib_path_count; di++) {
                cmd_appendf(cmd, sizeof(cmd), " -L\"%s\"",
                         extra_lib_paths[di]);
            }
            if (rt_timer_obj) {
                cmd_appendf(cmd, sizeof(cmd), " \"%s\"", rt_timer_obj);
            }
            if (rt_crypto_archive) {
                cmd_appendf(cmd, sizeof(cmd), " \"%s\"", rt_crypto_archive);
            }
            if (irgen.uses_socket_async) {
                cmd_appendf(cmd, sizeof(cmd), " \"%s/zanrt_io.o\"", sys);
            }
            if (irgen.uses_sync_runtime) {
                cmd_appendf(cmd, sizeof(cmd), " \"%s/zanrt_sync.o\"", sys);
            }
            if (irgen.uses_file_runtime) {
                cmd_appendf(cmd, sizeof(cmd), " \"%s/zanrt_file.o\"", sys);
            }
            if (irgen.uses_decimal_runtime) {
                cmd_appendf(cmd, sizeof(cmd), " \"%s/zanrt_decimal.o\"", sys);
            }
            if (irgen.uses_embed_api) {
                cmd_appendf(cmd, sizeof(cmd),
                         " \"%s/zan_embed_api.o\"", sys);
            }
            if (irgen.uses_inflate) {
                cmd_appendf(cmd, sizeof(cmd),
                         " \"%s/zan_inflate.o\"", sys);
            }
            if (drv_lib_count > 0) {
                cmd_appendf(cmd, sizeof(cmd), " --start-group");
            } else {
                cmd_appendf(cmd, sizeof(cmd),
                         " --start-group \"%s/libc.a\" \"%s/libm.a\"", sys, sys);
            }
            for (int d = 0; d < cross_archive_count; d++) {
                cmd_appendf(cmd, sizeof(cmd), " \"%s\"", cross_archives[d]);
            }
            for (int li = 0; li < static_driver_lib_count; li++) {
                cmd_appendf(cmd, sizeof(cmd), " %s",
                         static_driver_libs[li]);
            }
            for (int li = 0; li < drv_lib_count; li++) {
                cmd_appendf(cmd, sizeof(cmd), " -l%.*s",
                         drv_lib_len[li], drv_libs[li]);
            }
            if (drv_lib_count > 0) {
                cmd_appendf(cmd, sizeof(cmd),
                         " --end-group \"%s/libc.so\" \"%s/libm.so\""
                         " \"%s/liblog.so\" \"%s/libdl.so\""
                         " \"%s/libandroid.so\" \"%s/libEGL.so\""
                         " \"%s/libGLESv2.so\" \"%s/libaaudio.so\""
                         " \"%s/crtend_android.o\""
                         " \"%s/libclang_rt.builtins.a\"",
                         sys, sys, sys, sys, sys, sys, sys, sys, sys, sys);
            } else {
                cmd_appendf(cmd, sizeof(cmd),
                         " --end-group \"%s/libdl.a\" \"%s/crtend_android.o\""
                         " \"%s/libclang_rt.builtins.a\"",
                         sys, sys, sys);
            }
            if (getenv("ZAN_VERBOSE_LINK"))
                fprintf(stderr, "[link] %s\n", cmd);
            link_ret = system(cmd);
        } else if (cross_compiling && target.os == ZAN_OS_WINDOWS) {
            char exe_dir2[1024];
            zan_exe_dir(exe_dir2, sizeof(exe_dir2));
            const char *wsub = (target.arch == ZAN_ARCH_AARCH64)
                               ? "win-arm64" : "win-x64";
            char syslib[1200];
            snprintf(syslib, sizeof(syslib), "%s/%s/mingw/lib", exe_dir2, wsub);
            char probe[1300];
            snprintf(probe, sizeof(probe), "%s/crt2.o", syslib);
            if (!zan_file_exists(probe)) {
                fprintf(stderr,
                        "error: bundled %s mingw runtime not found at '%s'; "
                        "reinstall zan or rebuild with toolchain/%s present\n",
                        wsub, syslib, wsub);
                generated_object_vec_remove(&generated_objects, obj_tmp);
                zan_diag_free_buffers(irgen.diag);
                zan_irgen_destroy(&irgen);
                zan_arena_free(ir_arena);
                zan_arena_free(arena);
                free(source);
                return 1;
            }
            char winrt_io[1400] = {0};
            char winrt_sync[1400] = {0};
            char winrt_file[1400] = {0};
            char winrt_decimal[1400] = {0};
            char winrt_embed[1400] = {0};
            if (rt_io_obj)
                snprintf(winrt_io, sizeof(winrt_io), "%s/%s/%s", exe_dir2, wsub,
                         external_async_executor ? "zanrt_io_mt.o" : "zanrt_io.o");
            if (rt_sync_obj)
                snprintf(winrt_sync, sizeof(winrt_sync), "%s/%s/zanrt_sync.o",
                         exe_dir2, wsub);
            if (rt_file_obj)
                snprintf(winrt_file, sizeof(winrt_file), "%s/%s/zanrt_file.o",
                         exe_dir2, wsub);
            if (rt_decimal_obj)
                snprintf(winrt_decimal, sizeof(winrt_decimal), "%s/%s/zanrt_decimal.o",
                         exe_dir2, wsub);
            if (rt_embed_obj)
                snprintf(winrt_embed, sizeof(winrt_embed),
                         "%s/%s/zan_embed_api.o", exe_dir2, wsub);
            char winrt_inflate[1400] = {0};
            if (rt_inflate_obj)
                snprintf(winrt_inflate, sizeof(winrt_inflate),
                         "%s/%s/zan_inflate.o", exe_dir2, wsub);
            if ((winrt_io[0] && !zan_file_exists(winrt_io))
                || (winrt_sync[0] && !zan_file_exists(winrt_sync))
                || (winrt_file[0] && !zan_file_exists(winrt_file))
                || (winrt_decimal[0] && !zan_file_exists(winrt_decimal))
                || (winrt_embed[0] && !zan_file_exists(winrt_embed))
                || (winrt_inflate[0] && !zan_file_exists(winrt_inflate))) {
                fprintf(stderr,
                        "error: bundled %s runtime objects not found in "
                        "'%s/%s'; reinstall zan or rebuild with toolchain/%s "
                        "present (see scripts/build_win_rt.sh)\n",
                        wsub, exe_dir2, wsub, wsub);
                generated_object_vec_remove(&generated_objects, obj_tmp);
                zan_diag_free_buffers(irgen.diag);
                zan_irgen_destroy(&irgen);
                zan_arena_free(ir_arena);
                zan_arena_free(arena);
                free(source);
                return 1;
            }
            char gcclib[1300];
            snprintf(gcclib, sizeof(gcclib), "%s/libgcc.a", syslib);
            int have_gcc = zan_file_exists(gcclib);

            char cmd[8192];
            snprintf(cmd, sizeof(cmd),
                     "ld.lld -m %s --stack 268435456%s --gc-sections%s -o \"%s\" "
                     "\"%s/crt2.o\" \"%s/crtbegin.o\"",
                     (target.arch == ZAN_ARCH_AARCH64) ? "arm64pe" : "i386pep",
                     (publish_mode || strip_symbols) ? " -s --icf=all -O2" : "",
                     (link_subsystem && strcmp(link_subsystem, "windows") == 0)
                         ? " --subsystem windows" : "",
                     obj_path, syslib, syslib);
            for (int di = 0; di < zan_lib_ndirs; di++) {
                cmd_appendf(cmd, sizeof(cmd), " -L\"%s\"",
                         zan_lib_dirs[di]);
            }
            { cmd_appendf(cmd, sizeof(cmd), " -L\"%s\"", syslib); }
            for (int di = 0; di < extra_lib_path_count; di++) {
                cmd_appendf(cmd, sizeof(cmd), " -L\"%s\"",
                         extra_lib_paths[di]);
            }
            { cmd_appendf(cmd, sizeof(cmd), " \"%s\"", obj_tmp); }
            if (rt_timer_obj) {
                cmd_appendf(cmd, sizeof(cmd), " \"%s\"", rt_timer_obj);
            }
            if (rt_crypto_archive) {
                cmd_appendf(cmd, sizeof(cmd), " \"%s\"", rt_crypto_archive);
            }
            if (winrt_io[0]) {
                cmd_appendf(cmd, sizeof(cmd), " \"%s\"", winrt_io);
            }
            if (winrt_sync[0]) {
                cmd_appendf(cmd, sizeof(cmd), " \"%s\"", winrt_sync);
            }
            if (winrt_file[0]) {
                cmd_appendf(cmd, sizeof(cmd), " \"%s\"", winrt_file);
            }
            if (winrt_decimal[0]) {
                cmd_appendf(cmd, sizeof(cmd), " \"%s\"", winrt_decimal);
            }
            if (winrt_embed[0]) {
                cmd_appendf(cmd, sizeof(cmd), " \"%s\"", winrt_embed);
            }
            if (winrt_inflate[0]) {
                cmd_appendf(cmd, sizeof(cmd), " \"%s\"", winrt_inflate);
            }
            for (int ei = 0; ei < extra_link_input_count; ei++) {
                cmd_appendf(cmd, sizeof(cmd), " \"%s\"",
                         extra_link_inputs[ei]);
            }
            { cmd_appendf(cmd, sizeof(cmd), " --start-group"
                       " -lmingw32 -lmoldname -lmingwex -lmsvcrt"
                       " -lkernel32 -ladvapi32 -lshell32 -luser32 -lversion"); }
            if (winrt_io[0]) {
                cmd_appendf(cmd, sizeof(cmd), " -lws2_32");
            }
            { cmd_appendf(cmd, sizeof(cmd), have_gcc
                       ? " -lgcc -lgcc_eh"
                       : " -lclang_rt.builtins-aarch64 -lunwind -lucrt"); }
            for (int ei = 0; ei < extra_link_lib_count; ei++) {
                cmd_appendf(cmd, sizeof(cmd), " -l%s",
                         extra_link_libs[ei]);
            }
            for (int li = 0; li < irgen.extern_lib_count; li++) {
                int nlen;
                const char *nm = zan_dllimport_lname(
                    irgen.extern_libs[li].str,
                    (int)irgen.extern_libs[li].len, &nlen);
                if (!nm) continue;
                cmd_appendf(cmd, sizeof(cmd), " -l%.*s", nlen, nm);
            }
            for (int li = 0; li < static_driver_lib_count; li++) {
                cmd_appendf(cmd, sizeof(cmd), " %s",
                         static_driver_libs[li]);
            }
            { cmd_appendf(cmd, sizeof(cmd),
                       " --end-group \"%s/crtend.o\"", syslib); }
            if (getenv("ZAN_VERBOSE_LINK"))
                fprintf(stderr, "[link win] %s\n", cmd);
            link_ret = system(cmd);
        } else if (cross_compiling && target.os == ZAN_OS_WASI) {
            char exe_dir2[1024];
            zan_exe_dir(exe_dir2, sizeof(exe_dir2));
            char sys[1200];
            snprintf(sys, sizeof(sys), "%s/wasm32", exe_dir2);
            char probe[1300];
            snprintf(probe, sizeof(probe), "%s/libc.a", sys);
            if (!zan_file_exists(probe)) {
                fprintf(stderr,
                        "error: bundled wasm32 wasi sysroot not found at "
                        "'%s'; reinstall zan or rebuild with "
                        "toolchain/wasm32 present\n", sys);
                generated_object_vec_remove(&generated_objects, obj_tmp);
                zan_diag_free_buffers(irgen.diag);
                zan_irgen_destroy(&irgen);
                zan_arena_free(ir_arena);
                zan_arena_free(arena);
                free(source);
                return 1;
            }
            if (rt_io_obj) {
                static const char *const sock_pre[] = {
                    "zan_io_socket_", "zan_gate_", "zan_io_connect_",
                    "zan_io_resolve", "zan_io_sockaddr_",
                    NULL
                };
                static const char *const gui_pre2[] = { "zan_gui_", NULL };
                if (wasm_obj_vec_refs_any(&generated_objects, sock_pre)
                    && !wasm_obj_vec_refs_any(&generated_objects, gui_pre2)) {
                    fprintf(stderr,
                            "error: socket-async programs are not available for "
                            "the wasm32 target\n");
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
            snprintf(cmd, sizeof(cmd),
                     "wasm-ld%s --gc-sections -z stack-size=4194304 --max-memory=536870912 "
                     "--table-base=2 "
                     "-o \"%s\" \"%s/crt1.o\" \"%s\"",
                     (publish_mode || strip_symbols) ? " -s" : "", obj_path, sys,
                     obj_tmp);
            for (int ei = 0; ei < extra_link_input_count; ei++) {
                cmd_appendf(cmd, sizeof(cmd), " \"%s\"",
                         extra_link_inputs[ei]);
            }
            if (irgen.uses_file_runtime) {
                cmd_appendf(cmd, sizeof(cmd), " \"%s/zanrt_file.o\"",
                         sys);
            }
            if (irgen.uses_inflate) {
                char inflateobj[1300];
                snprintf(inflateobj, sizeof(inflateobj), "%s/zan_inflate.o", sys);
                if (!zan_file_exists(inflateobj)) {
                    fprintf(stderr,
                            "error: embedded resources for wasm32 need the "
                            "inflate runtime object at '%s'; rebuild it with "
                            "scripts\\build_cross_rt.cmd\n", inflateobj);
                    generated_object_vec_remove(&generated_objects, obj_tmp);
                    zan_diag_free_buffers(irgen.diag);
                    zan_irgen_destroy(&irgen);
                    zan_arena_free(ir_arena);
                    zan_arena_free(arena);
                    free(source);
                    return 1;
                }
                cmd_appendf(cmd, sizeof(cmd), " \"%s\"", inflateobj);
            }
            { cmd_appendf(cmd, sizeof(cmd),
                       " \"%s/zanrt_timer.o\" \"%s/zanrt_wasm.o\"",
                       sys, sys); }

            /* The 128-bit Decimal runtime: programs that reach System.Numerics
               reference __zan_decimal_*, and the wasm link has to supply them
               like every other target does. The object sits next to the other
               wasm runtime objects (toolchain/wasm32/zanrt_decimal.o, built by
               scripts\\build_cross_rt.cmd). It goes in BEFORE the crypto
               archive below: an archive member is extracted only for symbols
               that are still undefined, so the archive has to see the
               decimal symbols already resolved. */
            if (irgen.uses_decimal_runtime) {
                char decobj[1300];
                snprintf(decobj, sizeof(decobj), "%s/zanrt_decimal.o", sys);
                if (!zan_file_exists(decobj)) {
                    fprintf(stderr,
                            "error: wasm32 program needs the decimal runtime "
                            "object at '%s'; rebuild it with "
                            "scripts\\build_cross_rt.cmd\n", decobj);
                    generated_object_vec_remove(&generated_objects, obj_tmp);
                    zan_diag_free_buffers(irgen.diag);
                    zan_irgen_destroy(&irgen);
                    zan_arena_free(ir_arena);
                    zan_arena_free(arena);
                    free(source);
                    return 1;
                }
                cmd_appendf(cmd, sizeof(cmd), " \"%s\"", decobj);
            }
            /* The crypto archive rides along with the timer object: rt_timer.c
               no longer text-includes rt_hw_accel.c, so the SHA/AES/SM3/SM4
               block lives in an archive member and the linker only pulls it
               when the program really references a zan_hw_* symbol. It must
               come after zanrt_timer.o on the link line -- an archive member
               is extracted only for symbols that are still undefined, so a
               timer object that still carries the block would define them
               first and the member would stay out (no duplicate symbol). */
            {
                char cryptoarc[1300];
                snprintf(cryptoarc, sizeof(cryptoarc), "%s/libzancrypto.a", sys);
                if (zan_file_exists(cryptoarc)) {
                    cmd_appendf(cmd, sizeof(cmd), " \"%s\"", cryptoarc);
                }
            }
            {
                static const char *const gui_pre[] = { "zan_gui_", NULL };
                static const char *const image_pre[] = { "zan_image_", NULL };
                static const char *const audio_pre[] = { "zan_audio_", NULL };
                static const char *const game_pre[] = { "zan_game_", NULL };
                int need_game = wasm_obj_vec_refs_any(&generated_objects, game_pre);
                int need_gui = need_game || wasm_obj_vec_refs_any(&generated_objects, gui_pre);
                const int need_module[] = {
                    need_game || wasm_obj_vec_refs_any(&generated_objects, image_pre),
                    wasm_obj_vec_refs_any(&generated_objects, audio_pre),
                    need_game
                };
                static const char *const module_obj[] = {
                    "zanrt_image.o", "zanrt_audio.o", "zanrt_game.o"
                };
                for (int mi = 0; mi < 3; mi++) {
                    if (!need_module[mi]) continue;
                    char nativeobj[1300];
                    snprintf(nativeobj, sizeof(nativeobj), "%s/%s", sys, module_obj[mi]);
                    if (!zan_file_exists(nativeobj)) {
                        fprintf(stderr,
                                "error: wasm32 native module needs runtime object "
                                "at '%s'; rebuild it with scripts\\build_cross_rt.cmd\n",
                                nativeobj);
                        generated_object_vec_remove(&generated_objects, obj_tmp);
                        zan_diag_free_buffers(irgen.diag);
                        zan_irgen_destroy(&irgen);
                        zan_arena_free(ir_arena);
                        zan_arena_free(arena);
                        free(source);
                        return 1;
                    }
                    cmd_appendf(cmd, sizeof(cmd), " \"%s\"", nativeobj);
                }
                if (need_gui) {
                    char guiobj[1300];
                    snprintf(guiobj, sizeof(guiobj), "%s/zanrt_gui.o", sys);
                    if (!zan_file_exists(guiobj)) {
                        fprintf(stderr,
                                "error: GUI programs for wasm32 need the gui "
                                "runtime object at '%s'; rebuild it with "
                                "scripts\\build_cross_rt.cmd\n", guiobj);
                        generated_object_vec_remove(&generated_objects, obj_tmp);
                        zan_diag_free_buffers(irgen.diag);
                        zan_irgen_destroy(&irgen);
                        zan_arena_free(ir_arena);
                        zan_arena_free(arena);
                        free(source);
                        return 1;
                    }
                    cmd_appendf(cmd, sizeof(cmd),
                             " \"%s\" --export=zan_gui_wasm_feed", guiobj);
                    /* zanrt_gui */
                    char ftlib[1300];
                    snprintf(ftlib, sizeof(ftlib), "%s/libfreetype.a", sys);
                    if (zan_file_exists(ftlib)) {
                        cmd_appendf(cmd, sizeof(cmd), " \"%s\"", ftlib);
                    }
                    char syncwobj[1300];
                    snprintf(syncwobj, sizeof(syncwobj),
                             "%s/zanrt_syncw.o", sys);
                    if (zan_file_exists(syncwobj)) {
                        cmd_appendf(cmd, sizeof(cmd), " \"%s\"", syncwobj);
                    }
                }
            }
            if (irgen.wasm_eh_used) {
                cmd_appendf(cmd, sizeof(cmd), " \"%s/zanrt_ehtag.o\"",
                         sys);
            }
            { cmd_appendf(cmd, sizeof(cmd),
                       " \"%s/libc.a\" \"%s/libm.a\" \"%s/libzigc.a\""
                       " \"%s/libclang_rt.builtins-wasm32.a\"",
                       sys, sys, sys, sys); }
            link_ret = system(cmd);
        } else if (cross_compiling && (target.os == ZAN_OS_MACOS || target.os == ZAN_OS_IOS)) {
            char exe_dir2[1024];
            zan_exe_dir(exe_dir2, sizeof(exe_dir2));
            char tbd[1200];
            if (target.os == ZAN_OS_IOS) {
                snprintf(tbd, sizeof(tbd), "%s/ios/libSystem.tbd", exe_dir2);
                if (!zan_file_exists(tbd)) {
                    snprintf(tbd, sizeof(tbd), "%s/macos/libSystem.tbd", exe_dir2);
                }
            } else {
                snprintf(tbd, sizeof(tbd), "%s/macos/libSystem.tbd", exe_dir2);
            }
            if (!zan_file_exists(tbd)) {
                fprintf(stderr,
                        "error: bundled %s libSystem stub not found at "
                        "'%s'; reinstall zan or rebuild with "
                        "toolchain/%s present\n",
                        (target.os == ZAN_OS_IOS) ? "iOS" : "macOS",
                        tbd,
                        (target.os == ZAN_OS_IOS) ? "ios" : "macos");
                generated_object_vec_remove(&generated_objects, obj_tmp);
                zan_diag_free_buffers(irgen.diag);
                zan_irgen_destroy(&irgen);
                zan_arena_free(ir_arena);
                zan_arena_free(arena);
                free(source);
                return 1;
            }
            const char *march = (target.arch == ZAN_ARCH_AARCH64)
                                ? "arm64" : "x86_64";
            char macrt[1200];
            if (target.os == ZAN_OS_IOS) {
                snprintf(macrt, sizeof(macrt), "%s/ios/arm64", exe_dir2);
            } else {
                snprintf(macrt, sizeof(macrt), "%s/macos/%s", exe_dir2,
                         (target.arch == ZAN_ARCH_AARCH64) ? "arm64" : "x64");
            }
            char macrt_io[1400] = {0};
            char macrt_sync[1400] = {0};
            char macrt_file[1400] = {0};
            char macrt_decimal[1400] = {0};
            char macrt_embed[1400] = {0};
            if (rt_io_obj) {
                snprintf(macrt_io, sizeof(macrt_io), "%s/%s", macrt,
                         external_async_executor ? "zanrt_io_mt.o" : "zanrt_io.o");
            }
            if (rt_sync_obj) {
                snprintf(macrt_sync, sizeof(macrt_sync), "%s/zanrt_sync.o",
                         macrt);
            }
            if (rt_file_obj) {
                snprintf(macrt_file, sizeof(macrt_file), "%s/zanrt_file.o",
                         macrt);
            }
            if (rt_decimal_obj) {
                snprintf(macrt_decimal, sizeof(macrt_decimal), "%s/zanrt_decimal.o",
                         macrt);
            }
            if (rt_embed_obj) {
                snprintf(macrt_embed, sizeof(macrt_embed),
                         "%s/zan_embed_api.o", macrt);
            }
            char macrt_inflate[1400] = {0};
            if (rt_inflate_obj) {
                snprintf(macrt_inflate, sizeof(macrt_inflate),
                         "%s/zan_inflate.o", macrt);
            }
            if ((macrt_io[0] && !zan_file_exists(macrt_io))
                || (macrt_sync[0] && !zan_file_exists(macrt_sync))
                || (macrt_file[0] && !zan_file_exists(macrt_file))
                || (macrt_decimal[0] && !zan_file_exists(macrt_decimal))
                || (macrt_embed[0] && !zan_file_exists(macrt_embed))
                || (macrt_inflate[0] && !zan_file_exists(macrt_inflate))) {
                fprintf(stderr,
                        "error: bundled %s runtime objects not found in "
                        "'%s'; reinstall zan or rebuild with toolchain/%s "
                        "present (see scripts/build_%s_rt.sh)\n",
                        (target.os == ZAN_OS_IOS) ? "iOS" : "macOS",
                        macrt,
                        (target.os == ZAN_OS_IOS) ? "ios" : "macos",
                        (target.os == ZAN_OS_IOS) ? "ios" : "macos");
                generated_object_vec_remove(&generated_objects, obj_tmp);
                zan_diag_free_buffers(irgen.diag);
                zan_irgen_destroy(&irgen);
                zan_arena_free(ir_arena);
                zan_arena_free(arena);
                free(source);
                return 1;
            }
            char cmd[8192];
            if (target.os == ZAN_OS_IOS) {
                snprintf(cmd, sizeof(cmd),
                         "ld64.lld -arch arm64 -platform_version ios 14.0 14.0 -adhoc_codesign"
                         "%s -o \"%s\" \"%s\"",
                         (publish_mode || strip_symbols) ? " -dead_strip" : "",
                         obj_path, obj_tmp);
            } else {
                snprintf(cmd, sizeof(cmd),
                         "ld64.lld -arch %s -platform_version macos 11.0 11.0 -adhoc_codesign"
                         "%s -o \"%s\" \"%s\"",
                         march, (publish_mode || strip_symbols) ? " -dead_strip" : "",
                         obj_path, obj_tmp);
            }
            if (rt_timer_obj) {
                cmd_appendf(cmd, sizeof(cmd), " \"%s\"", rt_timer_obj);
            }
            if (rt_crypto_archive) {
                cmd_appendf(cmd, sizeof(cmd), " \"%s\"", rt_crypto_archive);
            }
            if (macrt_io[0]) {
                cmd_appendf(cmd, sizeof(cmd), " \"%s\"", macrt_io);
            }
            if (macrt_sync[0]) {
                cmd_appendf(cmd, sizeof(cmd), " \"%s\"", macrt_sync);
            }
            if (macrt_file[0]) {
                cmd_appendf(cmd, sizeof(cmd), " \"%s\"", macrt_file);
            }
            if (macrt_decimal[0]) {
                cmd_appendf(cmd, sizeof(cmd), " \"%s\"", macrt_decimal);
            }
            if (macrt_embed[0]) {
                cmd_appendf(cmd, sizeof(cmd), " \"%s\"", macrt_embed);
            }
            if (macrt_inflate[0]) {
                cmd_appendf(cmd, sizeof(cmd), " \"%s\"", macrt_inflate);
            }
            char macrt_gui[1400] = {0};
            snprintf(macrt_gui, sizeof(macrt_gui), "%s/zanrt_gui.o", macrt);
            if (!zan_file_exists(macrt_gui)) {
                if (target.os == ZAN_OS_IOS) {
                    snprintf(macrt_gui, sizeof(macrt_gui), "%s/toolchain/ios/arm64/zanrt_gui.o", exe_dir2);
                    if (!zan_file_exists(macrt_gui)) {
                        snprintf(macrt_gui, sizeof(macrt_gui), "toolchain/ios/arm64/zanrt_gui.o");
                    }
                } else {
                    snprintf(macrt_gui, sizeof(macrt_gui), "%s/toolchain/macos/%s/zanrt_gui.o",
                             exe_dir2, (target.arch == ZAN_ARCH_AARCH64) ? "arm64" : "x64");
                    if (!zan_file_exists(macrt_gui)) {
                        snprintf(macrt_gui, sizeof(macrt_gui), "toolchain/macos/%s/zanrt_gui.o",
                                 (target.arch == ZAN_ARCH_AARCH64) ? "arm64" : "x64");
                    }
                }
            }
            bool has_game_driver = false;
            if (target.os == ZAN_OS_IOS) {
                for (int li = 0; li < irgen.extern_lib_count; li++) {
                    if (strncmp(irgen.extern_libs[li].str, "zan_game", irgen.extern_libs[li].len) == 0) {
                        has_game_driver = true;
                        break;
                    }
                }
            }
            bool has_gui_driver = has_game_driver;
            if (target.os == ZAN_OS_IOS && !has_gui_driver) {
                for (int li = 0; li < irgen.extern_lib_count; li++) {
                    if (strncmp(irgen.extern_libs[li].str, "zan_gui", irgen.extern_libs[li].len) == 0) {
                        has_gui_driver = true;
                        break;
                    }
                }
            }
            if (!has_gui_driver) {
                for (int di = 0; di < cross_dylib_count; di++) {
                    if (strstr(cross_dylibs[di], "zan_gui") != NULL) {
                        has_gui_driver = true;
                        break;
                    }
                }
            }
            if (has_gui_driver && zan_file_exists(macrt_gui)) {
                cmd_appendf(cmd, sizeof(cmd), " \"%s\"", macrt_gui);
            }
            char macrt_image[1400] = {0};
            snprintf(macrt_image, sizeof(macrt_image), "%s/zanrt_image.o", macrt);
            if (!zan_file_exists(macrt_image)) {
                if (target.os == ZAN_OS_IOS) {
                    snprintf(macrt_image, sizeof(macrt_image), "%s/toolchain/ios/arm64/zanrt_image.o", exe_dir2);
                    if (!zan_file_exists(macrt_image)) {
                        snprintf(macrt_image, sizeof(macrt_image), "toolchain/ios/arm64/zanrt_image.o");
                    }
                }
            }
            bool has_image_driver = has_game_driver;
            if (target.os == ZAN_OS_IOS && !has_image_driver) {
                for (int li = 0; li < irgen.extern_lib_count; li++) {
                    if (strncmp(irgen.extern_libs[li].str, "zan_image", irgen.extern_libs[li].len) == 0) {
                        has_image_driver = true;
                        break;
                    }
                }
            }
            if (has_image_driver && zan_file_exists(macrt_image)) {
                cmd_appendf(cmd, sizeof(cmd), " \"%s\"", macrt_image);
            }
            char macrt_game[1400] = {0};
            snprintf(macrt_game, sizeof(macrt_game), "%s/zanrt_game.o", macrt);
            if (!zan_file_exists(macrt_game)) {
                if (target.os == ZAN_OS_IOS) {
                    snprintf(macrt_game, sizeof(macrt_game), "%s/toolchain/ios/arm64/zanrt_game.o", exe_dir2);
                    if (!zan_file_exists(macrt_game)) {
                        snprintf(macrt_game, sizeof(macrt_game), "toolchain/ios/arm64/zanrt_game.o");
                    }
                }
            }
            if (has_game_driver && zan_file_exists(macrt_game)) {
                cmd_appendf(cmd, sizeof(cmd), " \"%s\"", macrt_game);
            }
            char macrt_audio[1400] = {0};
            snprintf(macrt_audio, sizeof(macrt_audio), "%s/zanrt_audio.o", macrt);
            if (!zan_file_exists(macrt_audio)) {
                if (target.os == ZAN_OS_IOS) {
                    snprintf(macrt_audio, sizeof(macrt_audio), "%s/toolchain/ios/arm64/zanrt_audio.o", exe_dir2);
                    if (!zan_file_exists(macrt_audio)) {
                        snprintf(macrt_audio, sizeof(macrt_audio), "toolchain/ios/arm64/zanrt_audio.o");
                    }
                }
            }
            bool has_audio_driver = false;
            if (target.os == ZAN_OS_IOS) {
                for (int li = 0; li < irgen.extern_lib_count; li++) {
                    if (strncmp(irgen.extern_libs[li].str, "zan_audio", irgen.extern_libs[li].len) == 0) {
                        has_audio_driver = true;
                        break;
                    }
                }
            }
            if (has_audio_driver && zan_file_exists(macrt_audio)) {
                cmd_appendf(cmd, sizeof(cmd), " \"%s\"", macrt_audio);
            }
            for (int ei = 0; ei < extra_link_input_count; ei++) {
                cmd_appendf(cmd, sizeof(cmd), " \"%s\"",
                         extra_link_inputs[ei]);
            }
            for (int di = 0; di < cross_dylib_count; di++) {
                cmd_appendf(cmd, sizeof(cmd), " \"%s\"",
                         cross_dylibs[di]);
            }
            if (cross_dylib_count > 0) {
                cmd_appendf(cmd, sizeof(cmd),
                         " -rpath @loader_path");
            }
            { cmd_appendf(cmd, sizeof(cmd), " \"%s\"", tbd); }
            if (target.os == ZAN_OS_IOS && has_gui_driver) {
                const char *ios_tbds[] = {"UIKit.tbd", "Foundation.tbd", "CoreGraphics.tbd", "QuartzCore.tbd", "CoreText.tbd"};
                for (size_t ti = 0; ti < sizeof(ios_tbds) / sizeof(ios_tbds[0]); ti++) {
                    char fw_tbd[1200];
                    snprintf(fw_tbd, sizeof(fw_tbd), "%s/ios/%s", exe_dir2, ios_tbds[ti]);
                    if (!zan_file_exists(fw_tbd)) {
                        snprintf(fw_tbd, sizeof(fw_tbd), "toolchain/ios/%s", ios_tbds[ti]);
                    }
                    if (zan_file_exists(fw_tbd)) {
                        cmd_appendf(cmd, sizeof(cmd), " \"%s\"", fw_tbd);
                    }
                }
            }
            if (getenv("ZAN_VERBOSE_LINK")) {
                fprintf(stderr, "zanc: [link] %s\n", cmd);
            }
            link_ret = system(cmd);
        } else if (cross_compiling && target.os == ZAN_OS_FREESTANDING) {
            remove(obj_path);
            if (rename(obj_tmp, obj_path) != 0) {
                fprintf(stderr, "error: cannot write object '%s'\n",
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
        } else if (cross_compiling) {
            fprintf(stderr,
                    "error: cross-compilation to '%s' is not supported yet "
                    "(see --list-targets)\n",
                    target.triple);
            generated_object_vec_remove(&generated_objects, obj_tmp);
            zan_diag_free_buffers(irgen.diag);
            zan_irgen_destroy(&irgen);
            zan_arena_free(ir_arena);
            zan_arena_free(arena);
            free(source);
            return 1;
        } else {
#ifdef _WIN32
        char exe_dir[1024];
        GetModuleFileNameA(NULL, exe_dir, sizeof(exe_dir));
        { char *s = strrchr(exe_dir, '\\'); if (s) *s = '\0'; }
        char ld_path[1200], syslib[1200];
        snprintf(ld_path, sizeof(ld_path), "%s\\ld.exe", exe_dir);
        snprintf(syslib, sizeof(syslib), "%s\\mingw\\lib", exe_dir);
        int have_bundle =
            zan_utf8_get_file_attributes(ld_path) != INVALID_FILE_ATTRIBUTES &&
            zan_utf8_get_file_attributes(syslib)  != INVALID_FILE_ATTRIBUTES;

        if (have_bundle) {
            char crt2[1300], crtbeg[1300], crtend[1300], lflag[1300];
            snprintf(crt2,   sizeof(crt2),   "%s\\crt2.o", syslib);
            snprintf(crtbeg, sizeof(crtbeg), "%s\\crtbegin.o", syslib);
            snprintf(crtend, sizeof(crtend), "%s\\crtend.o", syslib);
            snprintf(lflag,  sizeof(lflag),  "-L%s", syslib);

            const char *argv[ZAN_LINK_MAX_ARGV];
            int a = 0;
            argv[a++] = ld_path;
            argv[a++] = "-m";      argv[a++] = "i386pep";
            argv[a++] = "-Bdynamic";
            argv[a++] = "--stack"; argv[a++] = "268435456";
            if (publish_mode || strip_symbols) {
                argv[a++] = "-s";
            }
            argv[a++] = "--gc-sections";
            if (link_subsystem && strcmp(link_subsystem, "windows") == 0) {
                argv[a++] = "--subsystem"; argv[a++] = "windows";
            }
            argv[a++] = "-o";      argv[a++] = obj_path;
            argv[a++] = crt2;
            argv[a++] = crtbeg;
            char ldirbufs[ZAN_LINK_MAX_DIRS][520];
            for (int di = 0; di < zan_lib_ndirs; di++) {
                if (a >= ZAN_LINK_MAX_ARGV - ZAN_LINK_ARGV_TAIL)
                    link_cap_exceeded("linker arguments", ZAN_LINK_MAX_ARGV);
                snprintf(ldirbufs[di], sizeof(ldirbufs[di]), "-L%s", zan_lib_dirs[di]);
                argv[a++] = ldirbufs[di];
            }
            argv[a++] = lflag;
            char elpbufs[ZAN_LINK_MAX_DIRS][520];
            for (int di = 0; di < extra_lib_path_count; di++) {
                if (a >= ZAN_LINK_MAX_ARGV - ZAN_LINK_ARGV_TAIL)
                    link_cap_exceeded("linker arguments", ZAN_LINK_MAX_ARGV);
                snprintf(elpbufs[di], sizeof(elpbufs[di]), "-L%s", extra_lib_paths[di]);
                argv[a++] = elpbufs[di];
            }
            for (int oi = 0; oi < generated_objects.count; oi++) {
                if (a >= ZAN_LINK_MAX_ARGV - ZAN_LINK_ARGV_TAIL)
                    link_cap_exceeded("generated objects", ZAN_LINK_MAX_ARGV);
                argv[a++] = generated_objects.paths[oi];
            }
            if (rt_io_obj) argv[a++] = rt_io_obj;
            if (rt_sync_obj) argv[a++] = rt_sync_obj;
            if (rt_file_obj) argv[a++] = rt_file_obj;
            if (rt_decimal_obj) argv[a++] = rt_decimal_obj;
            if (rt_embed_obj) argv[a++] = rt_embed_obj;
            if (rt_inflate_obj) argv[a++] = rt_inflate_obj;
            if (rt_timer_obj) argv[a++] = rt_timer_obj;
            if (rt_crypto_archive) argv[a++] = rt_crypto_archive;
            if (rt_mem_obj) {
                argv[a++] = rt_mem_obj;
                argv[a++] = "--wrap=malloc";  argv[a++] = "--wrap=free";
                argv[a++] = "--wrap=calloc"; argv[a++] = "--wrap=realloc";
            }
            for (int ei = 0; ei < extra_link_input_count; ei++) {
                if (a >= ZAN_LINK_MAX_ARGV - ZAN_LINK_ARGV_TAIL)
                    link_cap_exceeded("linker arguments", ZAN_LINK_MAX_ARGV);
                argv[a++] = extra_link_inputs[ei];
            }
            argv[a++] = "--start-group";
            char elibbufs[ZAN_LINK_MAX_LIBS][160]; int neb = 0;
            for (int ei = 0; ei < extra_link_lib_count; ei++) {
                if (a >= ZAN_LINK_MAX_ARGV - ZAN_LINK_ARGV_TAIL || neb >= ZAN_LINK_MAX_LIBS)
                    link_cap_exceeded("linker arguments", ZAN_LINK_MAX_ARGV);
                snprintf(elibbufs[neb], sizeof(elibbufs[neb]), "-l%s", extra_link_libs[ei]);
                argv[a++] = elibbufs[neb++];
            }
            argv[a++] = "-lmingw32"; argv[a++] = "-lgcc";
            argv[a++] = "-lmoldname"; argv[a++] = "-lmingwex";
            argv[a++] = "-lmsvcrt";   argv[a++] = "-lkernel32";
            argv[a++] = "-ladvapi32"; argv[a++] = "-lshell32";
            argv[a++] = "-luser32";   argv[a++] = "-lversion";
            argv[a++] = "-l:libwinpthread.a";
            if (rt_io_obj) argv[a++] = "-lws2_32";
            char libbufs[ZAN_LINK_MAX_LIBS][128]; int nb = 0;
            for (int li = 0; li < irgen.extern_lib_count; li++) {
                if (nb >= ZAN_LINK_MAX_LIBS ||
                    a >= ZAN_LINK_MAX_ARGV - ZAN_LINK_ARGV_TAIL)
                    link_cap_exceeded("DllImport libraries", ZAN_LINK_MAX_LIBS);
                int nlen;
                const char *nm = zan_dllimport_lname(
                    irgen.extern_libs[li].str,
                    (int)irgen.extern_libs[li].len, &nlen);
                if (!nm) continue;
                snprintf(libbufs[nb], sizeof(libbufs[nb]), "-l%.*s", nlen, nm);
                argv[a++] = libbufs[nb++];
            }
            for (int li = 0; li < static_driver_lib_count; li++) {
                if (a >= ZAN_LINK_MAX_ARGV - ZAN_LINK_ARGV_TAIL)
                    link_cap_exceeded("linker arguments", ZAN_LINK_MAX_ARGV);
                argv[a++] = static_driver_libs[li];
            }
            argv[a++] = "--end-group";
            argv[a++] = crtend;
            argv[a] = NULL;
            char lld_path[1200];
            snprintf(lld_path, sizeof(lld_path), "%s\\ld.lld.exe", exe_dir);
            const char *linker = rt_mem_obj ? ld_path
                                 : (zan_utf8_get_file_attributes(lld_path) != INVALID_FILE_ATTRIBUTES
                                    ? lld_path : ld_path);
            argv[0] = linker;
            if (getenv("ZAN_LINK_ECHO")) {
                fprintf(stderr, "[link]");
                for (int i = 1; i < a; i++) fprintf(stderr, " %s", argv[i]);
                fprintf(stderr, "\n");
            }
            link_ret = (int)zan_utf8_spawnv(_P_WAIT, linker, argv);
        } else {
            char link_cmd[4096];
            snprintf(link_cmd, sizeof(link_cmd),
                     "clang --target=x86_64-w64-windows-gnu \"%s\" -o \"%s\" "
                     "-Wl,--stack,268435456%s",
                     generated_objects.paths[0], obj_path,
                     publish_mode ? " -O2 -s" : "");
            for (int oi = 1; oi < generated_objects.count; oi++) {
                cmd_appendf(link_cmd, sizeof(link_cmd), " \"%s\"",
                         generated_objects.paths[oi]);
            }
            if (rt_io_obj) {
                cmd_appendf(link_cmd, sizeof(link_cmd), " \"%s\" -lws2_32", rt_io_obj);
            }
            if (rt_sync_obj) {
                cmd_appendf(link_cmd, sizeof(link_cmd), " \"%s\"", rt_sync_obj);
            }
            if (rt_file_obj) {
                cmd_appendf(link_cmd, sizeof(link_cmd), " \"%s\"", rt_file_obj);
            }
            if (rt_decimal_obj) {
                cmd_appendf(link_cmd, sizeof(link_cmd), " \"%s\"", rt_decimal_obj);
            }
            if (rt_embed_obj) {
                cmd_appendf(link_cmd, sizeof(link_cmd), " \"%s\"", rt_embed_obj);
            }
            if (rt_inflate_obj) {
                cmd_appendf(link_cmd, sizeof(link_cmd), " \"%s\"", rt_inflate_obj);
            }
            if (rt_timer_obj) {
                cmd_appendf(link_cmd, sizeof(link_cmd), " \"%s\"", rt_timer_obj);
            }
            if (rt_crypto_archive) {
                cmd_appendf(link_cmd, sizeof(link_cmd), " \"%s\"", rt_crypto_archive);
            }
            if (rt_mem_obj) {
                cmd_appendf(link_cmd, sizeof(link_cmd),
                         " \"%s\" -Wl,--wrap=malloc -Wl,--wrap=free"
                         " -Wl,--wrap=calloc -Wl,--wrap=realloc", rt_mem_obj);
            }
            for (int di = 0; di < zan_lib_ndirs; di++) {
                cmd_appendf(link_cmd, sizeof(link_cmd), " -L\"%s\"", zan_lib_dirs[di]);
            }
            if (link_subsystem && strcmp(link_subsystem, "windows") == 0) {
                cmd_appendf(link_cmd, sizeof(link_cmd), " -Wl,--subsystem,windows");
            }
            for (int di = 0; di < extra_lib_path_count; di++) {
                cmd_appendf(link_cmd, sizeof(link_cmd), " -L\"%s\"", extra_lib_paths[di]);
            }
            for (int ei = 0; ei < extra_link_input_count; ei++) {
                cmd_appendf(link_cmd, sizeof(link_cmd), " \"%s\"", extra_link_inputs[ei]);
            }
            for (int ei = 0; ei < extra_link_lib_count; ei++) {
                cmd_appendf(link_cmd, sizeof(link_cmd), " -l%s", extra_link_libs[ei]);
            }
            {
                cmd_appendf(link_cmd, sizeof(link_cmd),
                         " -l:libwinpthread.a");
            }
            for (int li = 0; li < irgen.extern_lib_count; li++) {
                int nlen;
                const char *nm = zan_dllimport_lname(
                    irgen.extern_libs[li].str,
                    (int)irgen.extern_libs[li].len, &nlen);
                if (!nm) continue;
                cmd_appendf(link_cmd, sizeof(link_cmd), " -l%.*s", nlen, nm);
            }
            for (int li = 0; li < static_driver_lib_count; li++) {
                cmd_appendf(link_cmd, sizeof(link_cmd), " %s",
                         static_driver_libs[li]);
            }
            if (getenv("ZAN_LINK_ECHO")) fprintf(stderr, "[link] %s\n", link_cmd);
            link_ret = system(link_cmd);
        }
#else
        char link_cmd[4096];
        if (publish_mode || strip_symbols) {
            snprintf(link_cmd, sizeof(link_cmd), "cc \"%s\" -o \"%s\" -lm -O2 -s",
                     obj_tmp, obj_path);
        } else {
            snprintf(link_cmd, sizeof(link_cmd), "cc \"%s\" -o \"%s\" -lm",
                     obj_tmp, obj_path);
        }
        if (rt_io_obj) {
            cmd_appendf(link_cmd, sizeof(link_cmd), " \"%s\"", rt_io_obj);
        }
        if (rt_sync_obj) {
#ifdef __APPLE__
            cmd_appendf(link_cmd, sizeof(link_cmd),
                     " \"%s\" -pthread", rt_sync_obj);
#else
            cmd_appendf(link_cmd, sizeof(link_cmd),
                     " \"%s\" -pthread -lrt", rt_sync_obj);
#endif
        }
        if (rt_file_obj) {
#ifdef __APPLE__
            cmd_appendf(link_cmd, sizeof(link_cmd),
                     " \"%s\" -pthread", rt_file_obj);
#else
            cmd_appendf(link_cmd, sizeof(link_cmd),
                     " \"%s\" -pthread", rt_file_obj);
#endif
        }
        if (rt_decimal_obj) {
            cmd_appendf(link_cmd, sizeof(link_cmd), " \"%s\"", rt_decimal_obj);
        }
        if (rt_embed_obj) {
            cmd_appendf(link_cmd, sizeof(link_cmd), " \"%s\"", rt_embed_obj);
        }
        if (rt_inflate_obj) {
            cmd_appendf(link_cmd, sizeof(link_cmd), " \"%s\"", rt_inflate_obj);
        }
        if (rt_timer_obj) {
#ifdef __APPLE__
            cmd_appendf(link_cmd, sizeof(link_cmd),
                     " \"%s\" -pthread", rt_timer_obj);
#else
            cmd_appendf(link_cmd, sizeof(link_cmd),
                     " \"%s\" -pthread -lrt", rt_timer_obj);
#endif
        }
        if (rt_crypto_archive) {
            cmd_appendf(link_cmd, sizeof(link_cmd), " \"%s\"", rt_crypto_archive);
        }
        if (rt_mem_obj) {
#ifdef __APPLE__
            cmd_appendf(link_cmd, sizeof(link_cmd), " \"%s\"", rt_mem_obj);
#else
            cmd_appendf(link_cmd, sizeof(link_cmd),
                     " \"%s\" -Wl,--wrap=malloc -Wl,--wrap=free"
                     " -Wl,--wrap=calloc -Wl,--wrap=realloc", rt_mem_obj);
#endif
        }
        for (int di = 0; di < zan_lib_ndirs; di++) {
            cmd_appendf(link_cmd, sizeof(link_cmd),
                     " -L\"%s\" -Wl,-rpath,\"%s\"", zan_lib_dirs[di], zan_lib_dirs[di]);
        }
        if (used_driver_count > 0) {
#ifdef __APPLE__
            cmd_appendf(link_cmd, sizeof(link_cmd),
                     " -Wl,-rpath,@loader_path");
#else
            cmd_appendf(link_cmd, sizeof(link_cmd),
                     " -Wl,-rpath,'$ORIGIN'");
#endif
        }
        static const char *const win_only_libs[] = {
            "user32", "gdi32", "kernel32", "advapi32", "shell32", "ole32",
            "oleaut32", "comdlg32", "comctl32", "gdiplus", "dwmapi", "shcore",
            "uxtheme", "msimg32", "winmm", "ws2_32", "shlwapi", "opengl32", NULL };
        for (int li = 0; li < irgen.extern_lib_count; li++) {
            const char *lib = irgen.extern_libs[li].str;
            int lib_len = (int)irgen.extern_libs[li].len;
            int skip = 0;
            for (int wi = 0; win_only_libs[wi]; wi++) {
                if ((int)strlen(win_only_libs[wi]) == lib_len &&
                    memcmp(win_only_libs[wi], lib, lib_len) == 0) { skip = 1; break; }
            }
            if (skip) continue;
            int name_len;
            const char *name = zan_dllimport_lname(lib, lib_len, &name_len);
            if (!name) continue;
            if (target.os == ZAN_OS_MACOS) {
                for (int d = 0; d < used_driver_count; d++) {
                    if (used_driver_runtime[d] ||
                        used_driver_static[d] ||
                        used_driver_len[d] != name_len ||
                        memcmp(used_drivers[d], name,
                               (size_t)name_len) != 0)
                        continue;
                    char primary[1200], fallback[1200];
                    snprintf(primary, sizeof(primary),
                             "%s/lib%.*s.dylib", driver_dirs[d],
                             name_len, name);
                    if (!zan_file_exists(primary) &&
                        zan_find_macos_driver_dylib(
                            driver_dirs[d], name, name_len,
                            fallback, sizeof(fallback))) {
                        cmd_appendf(link_cmd, sizeof(link_cmd),
                                 " \"%s\"", fallback);
                        name = NULL;
                    }
                    break;
                }
                if (!name) continue;
            }
            cmd_appendf(link_cmd, sizeof(link_cmd), " -l%.*s", name_len, name);
        }
        for (int li = 0; li < static_driver_lib_count; li++) {
            cmd_appendf(link_cmd, sizeof(link_cmd), " %s",
                     static_driver_libs[li]);
        }
        for (int di = 0; di < extra_lib_path_count; di++) {
            cmd_appendf(link_cmd, sizeof(link_cmd),
                     " -L\"%s\" -Wl,-rpath,\"%s\"", extra_lib_paths[di], extra_lib_paths[di]);
        }
        for (int ei = 0; ei < extra_link_input_count; ei++) {
            cmd_appendf(link_cmd, sizeof(link_cmd), " \"%s\"", extra_link_inputs[ei]);
        }
        for (int ei = 0; ei < extra_link_lib_count; ei++) {
            cmd_appendf(link_cmd, sizeof(link_cmd), " -l%s", extra_link_libs[ei]);
        }
        {
            cmd_appendf(link_cmd, sizeof(link_cmd), " -lm");
        }
        link_ret = system(link_cmd);
#endif
        }
