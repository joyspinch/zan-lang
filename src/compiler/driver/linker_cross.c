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
                if (rt_crash_obj) {
                    cmd_appendf(cmd, sizeof(cmd), " \"%s\"", rt_crash_obj);
                }
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
                if (rt_crash_obj) {
                    cmd_appendf(cmd, sizeof(cmd), " \"%s\"", rt_crash_obj);
                }
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
                if (rt_crash_obj) {
                    cmd_appendf(cmd, sizeof(cmd), " \"%s\"", rt_crash_obj);
                }
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
                if (rt_crash_obj) {
                    cmd_appendf(cmd, sizeof(cmd), " \"%s\"", rt_crash_obj);
                }
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
                       ? " -lgcc -lgcc_eh -lsupc++"
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
            { char wcrash[1300];
              snprintf(wcrash, sizeof wcrash, "%s/zanrt_crash.o", sys);
              if (zan_file_exists(wcrash))
                  cmd_appendf(cmd, sizeof(cmd), " \"%s\"", wcrash); }

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
