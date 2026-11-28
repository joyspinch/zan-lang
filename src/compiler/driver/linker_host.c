if (target.os == ZAN_OS_WINDOWS) {
  const char *sep = strrchr(obj_path, '\\');
  if (!sep)
    sep = strrchr(obj_path, '/');
  const char *base = sep ? sep + 1 : obj_path;
  int llen;
  const char *lname = zan_dllimport_lname(base, (int)strlen(base), &llen);
  if (!lname) {
    lname = base;
    llen = (int)strlen(base);
  }
  size_t plen = (size_t)(base - obj_path);
  snprintf(implib, sizeof(implib), "%.*slib%.*s.a", (int)plen, obj_path, llen,
           lname);
}
if (target.os == ZAN_OS_WINDOWS) {
#ifdef _WIN32
  char dll_exe_dir[1024];
  dll_exe_dir[0] = '\0';
  GetModuleFileNameA(NULL, dll_exe_dir, sizeof(dll_exe_dir));
  {
    char *s = strrchr(dll_exe_dir, '\\');
    if (s)
      *s = '\0';
  }
  char ld_path[1200];
  snprintf(ld_path, sizeof(ld_path), "%s\\ld.lld.exe", dll_exe_dir);
  const char *dll_ld = ld_path;
  bool dll_is_lld = true;
  if (!zan_file_exists(ld_path)) {
    snprintf(ld_path, sizeof(ld_path), "%s\\ld.exe", dll_exe_dir);
    if (zan_file_exists(ld_path)) {
      dll_is_lld = false;
    } else {
      dll_ld = "ld.lld";
    }
  }
  const char *argv[ZAN_LINK_MAX_ARGV];
  int a = 0;
  argv[a++] = dll_ld;
  argv[a++] = "-m";
  argv[a++] = (target.arch == ZAN_ARCH_AARCH64) ? "arm64pe" : "i386pep";
  argv[a++] = "-shared";
  argv[a++] = "-Bdynamic";
  argv[a++] = "--dynamicbase";
  argv[a++] = "--nxcompat";
  argv[a++] = "--high-entropy-va";
  if (dll_is_lld) {
    argv[a++] = "--gc-sections";
    if (publish_mode || strip_symbols)
      argv[a++] = "-s";
    if (publish_mode) {
      argv[a++] = "--icf=safe";
      argv[a++] = "-O2";
    }
  } else {
    argv[a++] = "--gc-sections";
    if (publish_mode || strip_symbols)
      argv[a++] = "-s";
  }
  char dllcrt2[1300], dllcrtbeg[1300], dllcrtend[1300];
  snprintf(dllcrt2, sizeof(dllcrt2), "%s\\mingw\\lib\\dllcrt2.o", dll_exe_dir);
  snprintf(dllcrtbeg, sizeof(dllcrtbeg), "%s\\mingw\\lib\\crtbegin.o",
           dll_exe_dir);
  snprintf(dllcrtend, sizeof(dllcrtend), "%s\\mingw\\lib\\crtend.o",
           dll_exe_dir);
  const char *dll_entry =
      zan_file_exists(dllcrt2) ? "DllMainCRTStartup" : "DllMain";
  argv[a++] = "-e";
  argv[a++] = dll_entry;
  argv[a++] = "-o";
  argv[a++] = obj_path;
  argv[a++] = obj_tmp;
  if (zan_file_exists(dllcrt2))
    argv[a++] = dllcrt2;
  if (zan_file_exists(dllcrtbeg))
    argv[a++] = dllcrtbeg;
  if (rt_io_obj)
    argv[a++] = rt_io_obj;
  if (rt_sync_obj)
    argv[a++] = rt_sync_obj;
  if (rt_file_obj)
    argv[a++] = rt_file_obj;
  if (rt_decimal_obj)
    argv[a++] = rt_decimal_obj;
  if (rt_embed_obj)
    argv[a++] = rt_embed_obj;
  if (rt_inflate_obj)
    argv[a++] = rt_inflate_obj;
  if (rt_timer_obj)
    argv[a++] = rt_timer_obj;
  if (rt_crash_obj)
    argv[a++] = rt_crash_obj;
  argv[a++] = "-out-implib";
  argv[a++] = implib;
  char ldirbufs[ZAN_LINK_MAX_DIRS * 2 + 4][520];
  int nld = 0;
  if (dll_exe_dir[0] && zan_file_exists(dll_exe_dir + 0)) {
    char mlib[1300];
    snprintf(mlib, sizeof(mlib), "%s\\mingw\\lib", dll_exe_dir);
    if (zan_file_exists(mlib)) {
      snprintf(ldirbufs[nld], sizeof(ldirbufs[0]), "-L%s", mlib);
      argv[a++] = ldirbufs[nld++];
    }
  }
  for (int di = 0; di < zan_lib_ndirs; di++) {
    if (a >= ZAN_LINK_MAX_ARGV - ZAN_LINK_ARGV_TAIL)
      link_cap_exceeded("linker arguments", ZAN_LINK_MAX_ARGV);
    snprintf(ldirbufs[nld], sizeof(ldirbufs[0]), "-L%s", zan_lib_dirs[di]);
    argv[a++] = ldirbufs[nld++];
  }
  for (int di = 0; di < extra_lib_path_count; di++) {
    if (a >= ZAN_LINK_MAX_ARGV - ZAN_LINK_ARGV_TAIL)
      link_cap_exceeded("linker arguments", ZAN_LINK_MAX_ARGV);
    snprintf(ldirbufs[nld], sizeof(ldirbufs[0]), "-L%s", extra_lib_paths[di]);
    argv[a++] = ldirbufs[nld++];
  }
  argv[a++] = "--start-group";
  static const char *const dllcrt[] = {
      "-lmingw32", "-lgcc",    "-lgcc_eh",   "-lsupc++",  "-lmoldname",
      "-lmingwex", "-lmsvcrt", "-lkernel32", "-lshell32", "-l:libwinpthread.a",
      NULL};
  for (int li = 0; dllcrt[li]; li++) {
    if (a >= ZAN_LINK_MAX_ARGV - ZAN_LINK_ARGV_TAIL)
      link_cap_exceeded("linker arguments", ZAN_LINK_MAX_ARGV);
    argv[a++] = dllcrt[li];
  }
  if (rt_io_obj)
    argv[a++] = "-lws2_32";
  for (int ei = 0; ei < extra_link_input_count; ei++) {
    if (a >= ZAN_LINK_MAX_ARGV - ZAN_LINK_ARGV_TAIL)
      link_cap_exceeded("linker arguments", ZAN_LINK_MAX_ARGV);
    argv[a++] = extra_link_inputs[ei];
  }
  char libbufs[ZAN_LINK_MAX_LIBS][128];
  for (int li = 0; li < import_lib_count; li++) {
    if (a >= ZAN_LINK_MAX_ARGV - ZAN_LINK_ARGV_TAIL)
      link_cap_exceeded("linker arguments", ZAN_LINK_MAX_ARGV);
    snprintf(libbufs[li], sizeof(libbufs[0]), "-l%.*s", import_lib_lens[li],
             import_libs[li]);
    argv[a++] = libbufs[li];
  }
  char explicit_libs[ZAN_LINK_MAX_LIBS][160];
  for (int ei = 0; ei < extra_link_lib_count; ei++) {
    if (a >= ZAN_LINK_MAX_ARGV - ZAN_LINK_ARGV_TAIL)
      link_cap_exceeded("linker arguments", ZAN_LINK_MAX_ARGV);
    snprintf(explicit_libs[ei], sizeof(explicit_libs[0]), "-l%s",
             extra_link_libs[ei]);
    argv[a++] = explicit_libs[ei];
  }
  for (int li = 0; li < static_driver_lib_count; li++) {
    if (a >= ZAN_LINK_MAX_ARGV - ZAN_LINK_ARGV_TAIL)
      link_cap_exceeded("linker arguments", ZAN_LINK_MAX_ARGV);
    argv[a++] = static_driver_libs[li];
  }
  if (rt_timer_archive)
    argv[a++] = rt_timer_archive;
  if (rt_crash_archive)
    argv[a++] = rt_crash_archive;
  if (rt_crypto_archive)
    argv[a++] = rt_crypto_archive;
  argv[a++] = "--end-group";
  if (zan_file_exists(dllcrtend))
    argv[a++] = dllcrtend;
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
  const char *wsub = (target.arch == ZAN_ARCH_AARCH64) ? "arm64pe" : "i386pep";
  snprintf(cmd, sizeof(cmd),
           "ld.lld -m %s -shared --dynamicbase --nxcompat --high-entropy-va"
           " --gc-sections%s -e DllMain -o \"%s\" \"%s\""
           " -out-implib \"%s\"",
           wsub, (publish_mode || strip_symbols) ? " -s --icf=safe -O2" : "",
           obj_path, obj_tmp, implib);
  {
    cmd_appendf(cmd, sizeof(cmd),
                " -lmingw32 -lmoldname -lmingwex -lmsvcrt"
                " -lkernel32 -lshell32");
  }
  for (int di = 0; di < zan_lib_ndirs; di++) {
    cmd_appendf(cmd, sizeof(cmd), " -L\"%s\"", zan_lib_dirs[di]);
  }
  for (int di = 0; di < extra_lib_path_count; di++) {
    cmd_appendf(cmd, sizeof(cmd), " -L\"%s\"", extra_lib_paths[di]);
  }
  for (int ei = 0; ei < extra_link_input_count; ei++) {
    cmd_appendf(cmd, sizeof(cmd), " \"%s\"", extra_link_inputs[ei]);
  }
  for (int ei = 0; ei < extra_link_lib_count; ei++) {
    cmd_appendf(cmd, sizeof(cmd), " -l%s", extra_link_libs[ei]);
  }
  for (int li = 0; li < import_lib_count; li++) {
    cmd_appendf(cmd, sizeof(cmd), " -l%.*s", import_lib_lens[li],
                import_libs[li]);
  }
  for (int li = 0; li < static_driver_lib_count; li++) {
    cmd_appendf(cmd, sizeof(cmd), " %s", static_driver_libs[li]);
  }
  if (rt_timer_archive) {
    cmd_appendf(cmd, sizeof(cmd), " \"%s\"", rt_timer_archive);
  }
  if (rt_crash_archive) {
    cmd_appendf(cmd, sizeof(cmd), " \"%s\"", rt_crash_archive);
  }
  if (rt_crypto_archive) {
    cmd_appendf(cmd, sizeof(cmd), " \"%s\"", rt_crypto_archive);
  }
  if (getenv("ZAN_VERBOSE_LINK"))
    fprintf(stderr, "[link] %s\n", cmd);
  link_ret = system(cmd);
  lib_spawned = true;
#endif
} else if (target.os == ZAN_OS_LINUX) {
  snprintf(cmd, sizeof(cmd),
           "ld.lld -shared -z relro -z now -z noexecstack --gc-sections%s -o "
           "\"%s\" \"%s\"",
           (publish_mode || strip_symbols) ? " -s --icf=all -O2" : "",
           shared_out, obj_tmp);
  if (rt_io_obj) {
    cmd_appendf(cmd, sizeof(cmd), " \"%s\"", rt_io_obj);
  }
  if (rt_sync_obj) {
    cmd_appendf(cmd, sizeof(cmd), " \"%s\"", rt_sync_obj);
  }
  if (rt_file_obj) {
    cmd_appendf(cmd, sizeof(cmd), " \"%s\"", rt_file_obj);
  }
  if (rt_decimal_obj) {
    cmd_appendf(cmd, sizeof(cmd), " \"%s\"", rt_decimal_obj);
  }
  if (rt_embed_obj) {
    cmd_appendf(cmd, sizeof(cmd), " \"%s\"", rt_embed_obj);
  }
  if (rt_inflate_obj) {
    cmd_appendf(cmd, sizeof(cmd), " \"%s\"", rt_inflate_obj);
  }
  if (rt_timer_obj) {
    cmd_appendf(cmd, sizeof(cmd), " \"%s\"", rt_timer_obj);
  }
  if (rt_crash_obj) {
    cmd_appendf(cmd, sizeof(cmd), " \"%s\"", rt_crash_obj);
  }
  if (rt_crypto_archive) {
    cmd_appendf(cmd, sizeof(cmd), " \"%s\"", rt_crypto_archive);
  }
} else if (target.os == ZAN_OS_MACOS) {
  const char *march = (target.arch == ZAN_ARCH_AARCH64) ? "arm64" : "x86_64";
  char system_tbd[1200], cxxabi_tbd[1200];
  snprintf(system_tbd, sizeof(system_tbd), "%s/macos/libSystem.tbd", exe_dir);
  snprintf(cxxabi_tbd, sizeof(cxxabi_tbd), "%s/macos/libc++abi.tbd", exe_dir);
  const char *missing_tbd = !zan_file_exists(system_tbd)   ? system_tbd
                            : !zan_file_exists(cxxabi_tbd) ? cxxabi_tbd
                                                           : NULL;
  if (missing_tbd) {
    fprintf(stderr,
            "error: bundled Apple SDK stub not found at '%s'; "
            "reinstall zan or rebuild with toolchain/macos present\n",
            missing_tbd);
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
           "11.0 11.0 -dead_strip%s -o \"%s\" \"%s\" \"%s\" \"%s\"",
           march, (publish_mode || strip_symbols) ? " -S" : "", obj_path,
           obj_tmp, system_tbd, cxxabi_tbd);
  if (rt_io_obj) {
    cmd_appendf(cmd, sizeof(cmd), " \"%s\"", rt_io_obj);
  }
  if (rt_sync_obj) {
    cmd_appendf(cmd, sizeof(cmd), " \"%s\"", rt_sync_obj);
  }
  if (rt_file_obj) {
    cmd_appendf(cmd, sizeof(cmd), " \"%s\"", rt_file_obj);
  }
  if (rt_decimal_obj) {
    cmd_appendf(cmd, sizeof(cmd), " \"%s\"", rt_decimal_obj);
  }
  if (rt_embed_obj) {
    cmd_appendf(cmd, sizeof(cmd), " \"%s\"", rt_embed_obj);
  }
  if (rt_inflate_obj) {
    cmd_appendf(cmd, sizeof(cmd), " \"%s\"", rt_inflate_obj);
  }
  if (rt_timer_obj) {
    cmd_appendf(cmd, sizeof(cmd), " \"%s\"", rt_timer_obj);
  }
  if (rt_crash_obj) {
    cmd_appendf(cmd, sizeof(cmd), " \"%s\"", rt_crash_obj);
  }
  if (rt_crypto_archive) {
    cmd_appendf(cmd, sizeof(cmd), " \"%s\"", rt_crypto_archive);
  }
} else if (cross_compiling && target.os == ZAN_OS_ANDROID) {
  char exe_dir3[1024] = {0};
  zan_exe_dir(exe_dir3, sizeof(exe_dir3));
  const char *asub3 =
      (target.arch == ZAN_ARCH_AARCH64) ? "android-arm64" : "android-x64";
  char sys3[1200];
  snprintf(sys3, sizeof(sys3), "%s/%s", exe_dir3, asub3);
  bool needs_activity_glue = apk_path != NULL;
  for (int d = 0; d < used_driver_count && !needs_activity_glue; d++) {
    needs_activity_glue =
        used_driver_len[d] == 7 && memcmp(used_drivers[d], "zan_gui", 7) == 0;
  }
  char libc3[1300], glue3[1300];
  snprintf(libc3, sizeof(libc3), "%s/libc.so", sys3);
  snprintf(glue3, sizeof(glue3), "%s/android_native_app_glue.o", sys3);
  if (!zan_file_exists(libc3) ||
      (needs_activity_glue && !zan_file_exists(glue3))) {
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
           "ld.lld -shared --no-undefined --eh-frame-hdr --gc-sections%s -o "
           "\"%s\" \"%s\"",
           (publish_mode || strip_symbols) ? " -s --icf=all -O2" : "", obj_path,
           obj_tmp);
  if (needs_activity_glue)
    cmd_appendf(cmd, sizeof(cmd), " \"%s\"", glue3);
  for (int di = 0; di < zan_lib_ndirs; di++) {
    cmd_appendf(cmd, sizeof(cmd), " -L\"%s\"", zan_lib_dirs[di]);
  }
  cmd_appendf(cmd, sizeof(cmd), " -L\"%s\"", sys3);
  for (int di = 0; di < extra_lib_path_count; di++) {
    cmd_appendf(cmd, sizeof(cmd), " -L\"%s\"", extra_lib_paths[di]);
  }
  for (int ei = 0; ei < extra_link_input_count; ei++) {
    cmd_appendf(cmd, sizeof(cmd), " \"%s\"", extra_link_inputs[ei]);
  }
  for (int ei = 0; ei < extra_link_lib_count; ei++) {
    cmd_appendf(cmd, sizeof(cmd), " -l%s", extra_link_libs[ei]);
  }
  if (rt_io_obj) {
    cmd_appendf(cmd, sizeof(cmd), " \"%s\"", rt_io_obj);
  }
  if (rt_sync_obj) {
    cmd_appendf(cmd, sizeof(cmd), " \"%s\"", rt_sync_obj);
  }
  if (rt_file_obj) {
    cmd_appendf(cmd, sizeof(cmd), " \"%s\"", rt_file_obj);
  }
  if (rt_decimal_obj) {
    cmd_appendf(cmd, sizeof(cmd), " \"%s\"", rt_decimal_obj);
  }
  if (rt_embed_obj) {
    cmd_appendf(cmd, sizeof(cmd), " \"%s\"", rt_embed_obj);
  }
  if (rt_inflate_obj) {
    cmd_appendf(cmd, sizeof(cmd), " \"%s\"", rt_inflate_obj);
  }
  if (rt_timer_obj) {
    cmd_appendf(cmd, sizeof(cmd), " \"%s\"", rt_timer_obj);
  }
  if (rt_crash_obj) {
    cmd_appendf(cmd, sizeof(cmd), " \"%s\"", rt_crash_obj);
  }
  if (rt_crypto_archive) {
    cmd_appendf(cmd, sizeof(cmd), " \"%s\"", rt_crypto_archive);
  }
  for (int li = 0; li < static_driver_lib_count; li++) {
    cmd_appendf(cmd, sizeof(cmd), " %s", static_driver_libs[li]);
  }
  for (int li = 0; li < import_lib_count; li++) {
    cmd_appendf(cmd, sizeof(cmd), " -l%.*s", import_lib_lens[li],
                import_libs[li]);
  }
  {
    cmd_appendf(cmd, sizeof(cmd),
                " \"%s/libc.so\" \"%s/libm.so\""
                " \"%s/liblog.so\" \"%s/libdl.so\"",
                sys3, sys3, sys3, sys3);
  }
  {
    cmd_appendf(cmd, sizeof(cmd),
                " \"%s/libandroid.so\" \"%s/libEGL.so\""
                " \"%s/libGLESv2.so\" \"%s/libaaudio.so\"",
                sys3, sys3, sys3, sys3);
  }
  cmd_appendf(cmd, sizeof(cmd),
              " \"%s/libc++abi.a\" \"%s/libunwind.a\""
              " \"%s/libclang_rt.builtins.a\"",
              sys3, sys3, sys3);
  if (getenv("ZAN_VERBOSE_LINK"))
    fprintf(stderr, "[link] %s\n", cmd);
  link_ret = system(cmd);
  lib_spawned = true;
} else if (target.os == ZAN_OS_OHOS) {
  const char *osub =
      (target.arch == ZAN_ARCH_AARCH64) ? "ohos-arm64" : "ohos-x64";
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
           "ld.lld -shared --gc-sections%s -o \"%s\" \"%s/zap_main.o\""
           " \"%s\" \"%s/libclang_rt.builtins.a\"",
           (publish_mode || strip_symbols) ? " -s --icf=all -O2" : "",
           shared_out, sys4, obj_tmp, sys4);
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
    static const char *const ohos_gl_libs[] = {"libEGL.so", "libGLESv3.so"};
    for (size_t gi = 0; gi < sizeof(ohos_gl_libs) / sizeof(ohos_gl_libs[0]);
         gi++) {
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
    cmd_appendf(cmd, sizeof(cmd), " \"%s\"", cross_archives[d]);
  }
  if (rt_timer_obj) {
    cmd_appendf(cmd, sizeof(cmd), " \"%s\"", rt_timer_obj);
  }
  if (rt_crash_obj) {
    cmd_appendf(cmd, sizeof(cmd), " \"%s\"", rt_crash_obj);
  }
  if (rt_crypto_archive) {
    cmd_appendf(cmd, sizeof(cmd), " \"%s\"", rt_crypto_archive);
  }
  if (irgen.uses_socket_async) {
    cmd_appendf(cmd, sizeof(cmd), " \"%s/zanrt_io.o\"", sys4);
  }
  if (irgen.uses_sync_runtime) {
    cmd_appendf(cmd, sizeof(cmd), " \"%s/zanrt_sync.o\"", sys4);
  }
  if (irgen.uses_file_runtime) {
    cmd_appendf(cmd, sizeof(cmd), " \"%s/zanrt_file.o\"", sys4);
  }
  if (irgen.uses_decimal_runtime) {
    cmd_appendf(cmd, sizeof(cmd), " \"%s/zanrt_decimal.o\"", sys4);
  }
  if (irgen.uses_embed_api) {
    cmd_appendf(cmd, sizeof(cmd), " \"%s/zan_embed_api.o\"", sys4);
  }
  if (irgen.uses_inflate) {
    cmd_appendf(cmd, sizeof(cmd), " \"%s/zan_inflate.o\"", sys4);
  }
  cmd_appendf(cmd, sizeof(cmd), " -L\"%s\"", sys4);
  for (int di = 0; di < zan_lib_ndirs; di++) {
    cmd_appendf(cmd, sizeof(cmd), " -L\"%s\"", zan_lib_dirs[di]);
  }
  for (int di = 0; di < extra_lib_path_count; di++) {
    cmd_appendf(cmd, sizeof(cmd), " -L\"%s\"", extra_lib_paths[di]);
  }
  for (int ei = 0; ei < extra_link_input_count; ei++) {
    cmd_appendf(cmd, sizeof(cmd), " \"%s\"", extra_link_inputs[ei]);
  }
  for (int ei = 0; ei < extra_link_lib_count; ei++) {
    cmd_appendf(cmd, sizeof(cmd), " -l%s", extra_link_libs[ei]);
  }
  for (int li = 0; li < import_lib_count; li++) {
    cmd_appendf(cmd, sizeof(cmd), " -l%.*s", import_lib_lens[li],
                import_libs[li]);
  }
  for (int li = 0; li < static_driver_lib_count; li++) {
    cmd_appendf(cmd, sizeof(cmd), " %s", static_driver_libs[li]);
  }
  if (getenv("ZAN_VERBOSE_LINK"))
    fprintf(stderr, "[link] %s\n", cmd);
  link_ret = zan_link_elf_shared(cmd, sizeof(cmd), &irgen, live_imports,
                                 elf_diagnostics);
  lib_spawned = true;
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
    cmd_appendf(cmd, sizeof(cmd), " -L\"%s\"", zan_lib_dirs[di]);
  }
  for (int di = 0; di < extra_lib_path_count; di++) {
    cmd_appendf(cmd, sizeof(cmd), " -L\"%s\"", extra_lib_paths[di]);
  }
  for (int ei = 0; ei < extra_link_input_count; ei++) {
    cmd_appendf(cmd, sizeof(cmd), " \"%s\"", extra_link_inputs[ei]);
  }
  for (int ei = 0; ei < extra_link_lib_count; ei++) {
    cmd_appendf(cmd, sizeof(cmd), " -l%s", extra_link_libs[ei]);
  }
  if (target.os == ZAN_OS_LINUX) {
    for (int d = 0; d < cross_archive_count; d++) {
      cmd_appendf(cmd, sizeof(cmd), " \"%s\"", cross_archives[d]);
    }
  } else if (target.os == ZAN_OS_MACOS) {
    for (int d = 0; d < cross_dylib_count; d++) {
      cmd_appendf(cmd, sizeof(cmd), " \"%s\"", cross_dylibs[d]);
    }
  }
  for (int li = 0; li < import_lib_count; li++) {
    cmd_appendf(cmd, sizeof(cmd), " -l%.*s", import_lib_lens[li],
                import_libs[li]);
  }
  for (int li = 0; li < static_driver_lib_count; li++) {
    cmd_appendf(cmd, sizeof(cmd), " %s", static_driver_libs[li]);
  }
  if (getenv("ZAN_VERBOSE_LINK"))
    fprintf(stderr, "[link] %s\n", cmd);
  link_ret = check_elf_imports
                 ? zan_link_elf_shared(cmd, sizeof(cmd), &irgen, live_imports,
                                       elf_diagnostics)
                 : system(cmd);
}
if (check_elf_imports && live_imports) {
  if (link_ret == 0 && !publish_link_output(shared_out, obj_path)) {
    fprintf(stderr, "error: cannot publish linked output '%s'\n", obj_path);
    link_ret = 1;
  }
  if (link_ret != 0)
    remove(shared_out);
}
}
