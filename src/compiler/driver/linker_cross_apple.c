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
                if (rt_crash_obj) {
                    cmd_appendf(cmd, sizeof(cmd), " \"%s\"", rt_crash_obj);
                }
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
        char lld_path[1200], syslib[1200];
        snprintf(lld_path, sizeof(lld_path), "%s\\ld.lld.exe", exe_dir);
        snprintf(syslib, sizeof(syslib), "%s\\mingw\\lib", exe_dir);
        DWORD lld_attrs = zan_utf8_get_file_attributes(lld_path);
        int have_bundle =
            zan_utf8_get_file_attributes(syslib) != INVALID_FILE_ATTRIBUTES;

        /* GNU ld 2.36 corrupts COFF TLS SECREL offsets under ASLR. Both
         * native EXE paths must use bundled LLD, including the clang fallback. */
        if (lld_attrs == INVALID_FILE_ATTRIBUTES ||
            (lld_attrs & FILE_ATTRIBUTE_DIRECTORY)) {
            fprintf(stderr,
                "zanc: error: bundled ld.lld.exe is required for Windows native "
                "TLS linking; restore the bundled LLVM linker\n");
            link_ret = 1;
        } else if (have_bundle) {
            char crt2[1300], crtbeg[1300], crtend[1300], lflag[1300];
            snprintf(crt2,   sizeof(crt2),   "%s\\crt2.o", syslib);
            snprintf(crtbeg, sizeof(crtbeg), "%s\\crtbegin.o", syslib);
            snprintf(crtend, sizeof(crtend), "%s\\crtend.o", syslib);
            snprintf(lflag,  sizeof(lflag),  "-L%s", syslib);

            const char *argv[ZAN_LINK_MAX_ARGV];
            int a = 0;
            argv[a++] = lld_path;
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
            if (rt_crash_obj) argv[a++] = rt_crash_obj;
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
            argv[a++] = "-lgcc_eh";  argv[a++] = "-lsupc++";
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
            argv[0] = lld_path;
            if (getenv("ZAN_LINK_ECHO")) {
                fprintf(stderr, "[link]");
                for (int i = 1; i < a; i++) fprintf(stderr, " %s", argv[i]);
                fprintf(stderr, "\n");
            }
            link_ret = (int)zan_utf8_spawnv(_P_WAIT, lld_path, argv);
        } else {
            char link_cmd[4096];
            snprintf(link_cmd, sizeof(link_cmd),
                     "clang --target=x86_64-w64-windows-gnu --ld-path=\"%s\" "
                     "\"%s\" -o \"%s\" -Wl,--stack,268435456%s",
                     lld_path, generated_objects.paths[0], obj_path,
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
                if (rt_crash_obj) {
                    cmd_appendf(link_cmd, sizeof(link_cmd), " \"%s\"", rt_crash_obj);
                }
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
        if (rt_crash_obj) {
            cmd_appendf(link_cmd, sizeof(link_cmd), " \"%s\"", rt_crash_obj);
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
                     " -Wl,--wrap=calloc -Wl,--wrap=realloc"
                     " -Wl,--wrap=__malloc_allzerop", rt_mem_obj);
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
