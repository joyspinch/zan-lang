/* FRAGMENT: host=main
 * 拼接片段（非独立 TU）：由 main.c 的 main() 函数体 #include，
 * 位于 main_frontend.c 之后、main_linker.c 之前。
 * consumes: irgen, ast, target, arena, ir_arena, source, want_shard,
 *   native_arch, publish_mode, binder
 * defines: generated_objects, obj_tmp, obj_path, driver_reg, driver_prefixes,
 * mf （链接与清理阶段在同一 main() 作用域继续消费。） */
zan_prepare_static_runtime(&irgen, &target, extra_link_input_count > 0);

phase("irgen");
probe_phase_mem("irgen");
if (g_time_phases) {
  size_t definitions = 0, declarations = 0, blocks = 0, instructions = 0;
  for (LLVMValueRef fn = LLVMGetFirstFunction(irgen.mod); fn;
       fn = LLVMGetNextFunction(fn)) {
    LLVMBasicBlockRef bb = LLVMGetFirstBasicBlock(fn);
    if (!bb) {
      declarations++;
      continue;
    }
    definitions++;
    for (; bb; bb = LLVMGetNextBasicBlock(bb)) {
      blocks++;
      for (LLVMValueRef inst = LLVMGetFirstInstruction(bb); inst;
           inst = LLVMGetNextInstruction(inst))
        instructions++;
    }
  }
  fprintf(stderr,
          "IR stats: %zu AST nodes created; %zu defined functions, "
          "%zu declarations, %zu blocks, %zu instructions\n",
          zan_ast_node_count(), definitions, declarations, blocks,
          instructions);
  fprintf(stderr,
          "Scale stats: %zu file reads (%zu MB), %zu metadata scans, "
          "%zu meta-cache hit/%zu miss/%zu written, "
          "%zu seed sources/%zu lexer passes, %zu throwaway parses "
          "(%zu fallback), %zu real parses, %zu secondary parses\n",
          g_scale_stats.file_reads, g_scale_stats.bytes_read / (1024 * 1024),
          g_scale_stats.metadata_scans, g_scale_stats.metadata_cache_hits,
          g_scale_stats.metadata_cache_misses,
          g_scale_stats.metadata_cache_writes, g_scale_stats.seed_sources,
          g_scale_stats.seed_lex_passes, g_scale_stats.throwaway_parses,
          g_scale_stats.throwaway_parse_fallbacks, g_scale_stats.real_parses,
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
  zan_diag_t *ir_diag =
      (zan_diag_t *)zan_arena_alloc(ir_arena, sizeof(*ir_diag));
  *ir_diag = *diag;
  ir_diag->file_sources = NULL;
  const char **ir_file_names = (const char **)malloc(
      (size_t)ir_diag->file_count * sizeof(*ir_file_names));
  for (int i = 0; i < ir_diag->file_count; i++)
    ir_file_names[i] = zan_arena_strdup(ir_arena, ir_diag->file_names[i],
                                        strlen(ir_diag->file_names[i]));
  ir_diag->file_names = ir_file_names;
  ir_diag->entries = NULL;
  ir_diag->entry_count = ir_diag->entry_cap = 0;
  ir_diag->dedup_sigs = NULL;
  ir_diag->dedup_count = ir_diag->dedup_cap = 0;
  if (diag->suppress_count > 0 && diag->suppress_codes) {
    ir_diag->suppress_codes =
        (int *)malloc(sizeof(int) * (size_t)diag->suppress_count);
    if (ir_diag->suppress_codes) {
      memcpy(ir_diag->suppress_codes, diag->suppress_codes,
             sizeof(int) * (size_t)diag->suppress_count);
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
  for (int i = 0; i < irgen.struct_type_count; i++) {
    zan_symbol_t *cur = irgen.struct_types[i].sym;
    irgen.struct_types[i].base_struct_idx = -1;
    irgen.struct_types[i].devirt_abstract = false;
    irgen.struct_types[i].devirt_generic = false;
    irgen.struct_types[i].vtable_name = NULL;
    if (!cur)
      continue;
    irgen.struct_types[i].devirt_abstract = (cur->modifiers & MOD_ABSTRACT) != 0;
    irgen.struct_types[i].devirt_generic =
        cur->decl && cur->decl->type_decl.type_params.count > 0;
    char vt_buf[320];
    int vtn = snprintf(vt_buf, sizeof(vt_buf), "__zan_vtable_%.*s",
                       (int)cur->name.len, cur->name.str);
    if (vtn > 0 && vtn < (int)sizeof(vt_buf))
      irgen.struct_types[i].vtable_name =
          zan_arena_strdup(ir_arena, vt_buf, (size_t)vtn);
    for (zan_type_t *bt = (cur->type ? cur->type->base_type : NULL); bt;
         bt = bt->base_type) {
      if (!bt->sym || bt->sym == cur)
        continue;
      for (int j = 0; j < irgen.struct_type_count; j++) {
        if (irgen.struct_types[j].sym == bt->sym) {
          irgen.struct_types[i].base_struct_idx = j;
          break;
        }
      }
      if (irgen.struct_types[i].base_struct_idx >= 0)
        break;
    }
  }
  for (int i = 0; i < irgen.struct_type_count; i++) {
    irgen.struct_types[i].sym = NULL;
  }
  zan_diag_free_buffers(diag);
  zan_arena_free(arena);
  arena = NULL;
  g_main_arena = ir_arena;
  irgen.binder = NULL;
}
probe_phase_mem("release ast");
if (source) {
  free(source);
  source = NULL;
}

char **shard_objs = NULL;
int shard_n = 0;
const char *mf_json_path = getenv("ZAN_CODEGEN_MANIFEST_JSON");
zan_cg_manifest_t mf;
bool mf_built = false;
if (getenv("ZAN_CODEGEN_MANIFEST") || mf_json_path || want_shard) {
  if (zan_opt_strip_unused(&irgen) != ZAN_OK)
    goto codegen_failed;
  zan_irgen_prune_extern_libs(&irgen);
  phase("manifest");
  probe_phase_mem("manifest");
  bool mf_native =
      target.arch == ZAN_ARCH_X86_64 || target.arch == ZAN_ARCH_AARCH64;
  zan_irgen_manifest_build(&irgen, &mf, mf_native);
  mf_built = true;
  if (getenv("ZAN_CODEGEN_MANIFEST"))
    zan_irgen_manifest_report(&irgen, &mf);
  if (mf_json_path &&
      zan_irgen_manifest_write_json(&irgen, &mf, mf_json_path) != ZAN_OK)
    fprintf(stderr, "warning: cannot write codegen manifest '%s'\n",
            mf_json_path);
}

/* Discard unused stdlib bodies before checking hardware requirements.
 * A declaration (or a dead helper) must not reject an ordinary baseline app. */
if (irgen.cpu_profile && strcmp(irgen.cpu_profile, "baseline") == 0) {
  if (zan_opt_strip_unused(&irgen) != ZAN_OK ||
      zan_opt_validate_cpu(&irgen, irgen.mod) != ZAN_OK)
    goto codegen_failed;
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
char final_link_path[1024];
const char *obj_path = final_link_path;
int path_len;
if (ipa_path) {
  path_len = snprintf(final_link_path, sizeof(final_link_path), "%s.macho_tmp",
                      ipa_path);
} else if (output_file) {
  path_len =
      snprintf(final_link_path, sizeof(final_link_path), "%s", output_file);
} else {
  size_t ilen = strlen(input_file);
  const char *def_ext = (target.os == ZAN_OS_WINDOWS) ? ".exe" : "";
  if (ilen > 4 && strcmp(input_file + ilen - 4, ".zan") == 0) {
    path_len = snprintf(final_link_path, sizeof(final_link_path), "%.*s%s",
                        (int)(ilen - 4), input_file, def_ext);
  } else {
    path_len =
        snprintf(final_link_path, sizeof(final_link_path), "%s%s", input_file,
                 (target.os == ZAN_OS_WINDOWS) ? ".exe" : ".out");
  }
}

const char *requested_output =
    ipa_path ? ipa_path : (apk_path ? apk_path : output_file);
if (requested_output) {
  char out_dir[1024];
  size_t olen = strlen(requested_output);
  if (olen < sizeof(out_dir)) {
    memcpy(out_dir, requested_output, olen + 1);
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
  if (path_len <= 0 || path_len >= (int)sizeof(final_link_path)) {
    fprintf(stderr, "error: invalid or too long output path\n");
    goto codegen_failed;
  }
  const char *artifact_path = requested_output ? requested_output : obj_path;
  if (!create_link_work_dir(artifact_path)) {
    fprintf(stderr, "error: cannot create a staging directory for '%s'\n",
            obj_path);
    goto codegen_failed;
  }
  if (!emit_lib || apk_path || ipa_path) {
    int n = snprintf(link_work_output, sizeof(link_work_output), "%s/linked-%s",
                     link_work_dir, zan_path_basename(final_link_path));
    if (n < 0 || n >= (int)sizeof(link_work_output)) {
      link_work_output[0] = '\0';
      fprintf(stderr, "error: staged output path is too long\n");
      goto codegen_failed;
    }
    obj_path = link_work_output;
  }
  if (!mf_built) {
    if (zan_opt_strip_unused(&irgen) != ZAN_OK)
      goto codegen_failed;
    zan_irgen_prune_extern_libs(&irgen);
  }
  zan_discover_drivers(resolved_stdlib_root, &driver_reg, &irgen);
  zan_driver_capture_conditions(&irgen, ir_arena, &driver_reg, &target,
                                driver_dir_override, &driver_prefixes);
}

const int explicit_link_input_count = extra_link_input_count;
const bool check_elf_imports =
    lib_shared && (target.os == ZAN_OS_LINUX || target.os == ZAN_OS_OHOS);
const bool *live_imports = NULL;
if (!do_emit_ir && want_shard && mf_built) {
  /* A live import may disappear from the main module after sharding. */
  if (explicit_link_input_count || check_elf_imports)
    live_imports = zan_capture_live_imports(&irgen);
  char shard_base[1100];
  snprintf(shard_base, sizeof(shard_base), "%s/module", link_work_dir);
  phase("shard");
  shard_n = zan_irgen_shard_run(&irgen, &mf, shard_base, &shard_objs);
  if (shard_n < 0) {
    fprintf(stderr, "error: shard emission failed\n");
    zan_diag_free_buffers(irgen.diag);
    zan_irgen_destroy(&irgen);
    zan_arena_free(ir_arena);
    if (arena)
      zan_arena_free(arena);
    free(source);
    return 1;
  }
}
if (shard_n > 0) {
  irgen.sharded_module = true;
  phase("shard emit");
}
probe_phase_mem("shard emit");
if (mf_built) {
  zan_irgen_manifest_free(&mf);
  mf_built = false;
}

if (effective_opt > ZAN_OPT_NONE) {
  zan_opt_report_t opt_report =
      zan_optimize(&irgen, NULL, effective_opt, &opt_knobs);
  if (!opt_report.success)
    goto codegen_failed;
  zan_opt_report_print(&opt_report);
  phase("optimize");
  probe_phase_mem("optimize");
  if (shard_n == 0)
    zan_irgen_prune_extern_libs(&irgen);
} else {
  if (!irgen.target_is_wasm)
    irgen.fast_codegen = true;
  if (!do_emit_ir) {
    if (zan_opt_strip_unused(&irgen) != ZAN_OK)
      goto codegen_failed;
    if (shard_n == 0)
      zan_irgen_prune_extern_libs(&irgen);
  }
}

if (!do_emit_ir && target.os == ZAN_OS_WASI &&
    !zan_wasi_capabilities_supported(&irgen))
  goto codegen_failed;

goto codegen_ready;
codegen_failed : fprintf(stderr, "error: code generation stopped\n");
if (mf_built)
  zan_irgen_manifest_free(&mf);
for (int i = 0; i < shard_n; i++) {
  remove(shard_objs[i]);
  free(shard_objs[i]);
}
free(shard_objs);
zan_diag_free_buffers(irgen.diag);
zan_irgen_destroy(&irgen);
zan_arena_free(ir_arena);
if (arena)
  zan_arena_free(arena);
free(source);
return 1;
codegen_ready : if (do_emit_ir) {
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
            "zanc: 消费方请用同样的特性，例如 llc -mcpu=%s -mattr=%s "
            "-filetype=obj out.ll\n",
            irgen.target_cpu,
            irgen.target_features[0] ? irgen.target_features : "(default)",
            irgen.target_cpu,
            irgen.target_features[0] ? irgen.target_features : "+sse2");
  }
}
else {
  if (shard_n == 0 && (explicit_link_input_count || check_elf_imports))
    live_imports = zan_capture_live_imports(&irgen);
  char driver_dirs[ZAN_MAX_USED_DRIVERS][1024];
  const char *used_drivers[ZAN_MAX_USED_DRIVERS];
  int used_driver_count = 0;
  int used_driver_len[ZAN_MAX_USED_DRIVERS];
  const char *used_driver_module[ZAN_MAX_USED_DRIVERS];
  const char *used_driver_root[ZAN_MAX_USED_DRIVERS];
  bool used_driver_runtime[ZAN_MAX_USED_DRIVERS] = {false};
  bool used_driver_static[ZAN_MAX_USED_DRIVERS] = {false};
  bool used_driver_embedded[ZAN_MAX_USED_DRIVERS] = {false};
  char embedded_driver_file[ZAN_MAX_USED_DRIVERS][128];
  char static_driver_libs[ZAN_MAX_STATIC_DRV_LIBS][128];
  int static_driver_lib_count = 0;
  for (int li = 0; li < irgen.extern_lib_count; li++) {
    if (used_driver_count >= ZAN_MAX_USED_DRIVERS)
      link_cap_exceeded("native drivers", ZAN_MAX_USED_DRIVERS);
    int nlen;
    const char *nm = zan_dllimport_lname(irgen.extern_libs[li].str,
                                         (int)irgen.extern_libs[li].len, &nlen);
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
    if (!sym[0])
      continue;
    if (!zan_driver_prefix_live(&irgen, &driver_prefixes, sym))
      continue;
    const char *lib = driver_reg.entries[di].lib;
    bool dup = false;
    for (int u = 0; u < used_driver_count; u++)
      if ((int)strlen(lib) == used_driver_len[u] &&
          memcmp(lib, used_drivers[u], strlen(lib)) == 0)
        dup = true;
    if (dup)
      continue;
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
          snprintf(driver_dirs[d], sizeof(driver_dirs[d]), "%s/%s/drivers/%s",
                   droot, mod, dsub);
        }
      }
    }
  }

  /* ---- Optional DllImport libraries on a Windows static publish ----
   * A [DllImport] library whose driver ships neither a static archive
   * nor a shared DLL in the effective driver dirs cannot be linked on a
   * single-file static publish; stub its calls and drop -l<lib>. */
  if (!emit_lib && target.os == ZAN_OS_WINDOWS && link_static_drivers &&
      publish_mode) {
    /* Backwards: dropping a lib compacts extern_libs and shifts slots. */
    for (int li = irgen.extern_lib_count - 1; li >= 0; li--) {
      int nlen;
      const char *nm = zan_dllimport_lname(
          irgen.extern_libs[li].str, (int)irgen.extern_libs[li].len, &nlen);
      if (!nm || zan_win_system_lib(nm, nlen))
        continue;
      if (zan_link_inputs_supply_import(&irgen, irgen.extern_libs[li],
                                        live_imports, extra_link_inputs,
                                        explicit_link_input_count)) {
        continue;
      }
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
      static const char fmts[6][28] = {"%s/static/lib%s.a", "%s/static/%s.lib",
                                       "%s/lib%s.a",        "%s/%s.lib",
                                       "%s/lib%s.dll",      "%s/%s.dll"};
      if (dir[0]) {
        for (int f = 0; f < 6 && !resolvable; f++) {
          char cand[1300];
          snprintf(cand, sizeof(cand), fmts[f], dir, nm);
          resolvable = zan_file_exists(cand);
        }
      }
      for (int di = 0; di < extra_lib_path_count && !resolvable; di++) {
        for (int f = 0; f < 6 && !resolvable; f++) {
          char cand[1300];
          snprintf(cand, sizeof(cand), fmts[f], extra_lib_paths[di], nm);
          resolvable = zan_file_exists(cand);
        }
      }
      if (resolvable)
        continue;
      int n = zan_irgen_stub_extern_lib(&irgen, irgen.extern_libs[li].str,
                                        (int)irgen.extern_libs[li].len);
      if (n > 0) {
        char libname[128];
        snprintf(libname, sizeof(libname), "%.*s",
                 (int)irgen.extern_libs[li].len, irgen.extern_libs[li].str);
        zan_irgen_drop_extern_lib(&irgen, irgen.extern_libs[li].str,
                                  (int)irgen.extern_libs[li].len);
        fprintf(stderr,
                "warning: no archive or shared library resolved "
                "for [DllImport(\"%s\")] in the static publish;"
                " its %d function(s) are stubbed and will fail at"
                " run time\n",
                libname, n);
      }
    }
  }

  char cross_archives[ZAN_MAX_USED_DRIVERS][1200];
  int cross_archive_count = 0;
  char cross_dylibs[ZAN_MAX_USED_DRIVERS][1200];
  int cross_dylib_count = 0;
  for (int d = 0; cross_compiling && d < used_driver_count; d++) {
    if (used_driver_runtime[d] || !driver_dirs[d][0])
      continue;
    if (target.os == ZAN_OS_LINUX || target.os == ZAN_OS_OHOS) {
      char *archive = cross_archives[cross_archive_count];
      snprintf(archive, sizeof(cross_archives[0]), "%s/static/lib%.*s.a",
               driver_dirs[d], used_driver_len[d], used_drivers[d]);
      if (zan_file_exists(archive)) {
        used_driver_static[d] = true;
        cross_archive_count++;
      }
    } else if (target.os == ZAN_OS_MACOS) {
      char *dylib = cross_dylibs[cross_dylib_count];
      if (zan_find_macos_driver_dylib(driver_dirs[d], used_drivers[d],
                                      used_driver_len[d], dylib,
                                      sizeof(cross_dylibs[0])))
        cross_dylib_count++;
    }
  }

  if (target.os == ZAN_OS_WINDOWS && link_subsystem == NULL) {
    int mcap = 64;
    char(*mdirs)[1024] = (char(*)[1024])malloc((size_t)mcap * sizeof(*mdirs));
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
                    memcmp(used_drivers[d], pkg.subsystem_driver,
                           (size_t)slen) == 0) {
                  match = true;
                  break;
                }
              }
            }
            if (!match && pkg.subsystem_sym[0] &&
                zan_driver_prefix_live(&irgen, &driver_prefixes,
                                       pkg.subsystem_sym)) {
              match = true;
            }
            if (match) {
              if (strcmp(pkg.subsystem, "windows") == 0)
                link_subsystem = "windows";
              else if (strcmp(pkg.subsystem, "console") == 0)
                link_subsystem = "console";
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
      if (!used_driver_runtime[d] || !driver_dirs[d][0])
        continue;
      if (embed_driver_spec_count >= 128)
        link_cap_exceeded("embedded driver DLLs", 128);
      if (embed_spec_count >= ZAN_MAX_EMBED_SPECS)
        link_cap_exceeded("--embed resources", ZAN_MAX_EMBED_SPECS);
      char drv[64];
      snprintf(drv, sizeof(drv), "%.*s", used_driver_len[d], used_drivers[d]);
      char file[128], src[1300];
      snprintf(file, sizeof(file), "%s.dll", drv);
      snprintf(src, sizeof(src), "%s/%s", driver_dirs[d], file);
      if (!zan_file_exists(src)) {
        snprintf(file, sizeof(file), "lib%s.dll", drv);
        snprintf(src, sizeof(src), "%s/%s", driver_dirs[d], file);
      }
      if (!zan_file_exists(src))
        continue;
      if (zan_embed_driver_spec(src, file,
                                embed_driver_specs[embed_driver_spec_count],
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
      snprintf(embedded_driver_file[d], sizeof(embedded_driver_file[0]), "%s",
               file);
    }
  }

  /* 包清单 [embed] 声明式资源扫描：解耦所有包内嵌资源 */
  int mcap = 64;
  char(*mdirs)[1024] = (char(*)[1024])malloc((size_t)mcap * sizeof(*mdirs));
  if (mdirs) {
    int mcnt = zan_pkg_all_manifest_dirs(package_project_root, mdirs, mcap);
    for (int m = 0; m < mcnt; m++) {
      char mpath[1200];
      snprintf(mpath, sizeof(mpath), "%s/zan.pkg", mdirs[m]);
      zan_package_t pkg;
      if (!zan_pkg_load(&pkg, mpath))
        continue;
      for (int e = 0; e < pkg.embed_count; e++) {
        const zan_pkg_embed_t *emb = &pkg.embeds[e];
        if (emb->sym_prefix[0] &&
            !zan_driver_prefix_live(&irgen, &driver_prefixes,
                                    emb->sym_prefix)) {
          continue;
        }
        bool staged = false;
        for (int es = 0; es < embed_spec_count && !staged; es++) {
          const char *seq = strrchr(embed_specs[es], '=');
          if (seq && strcmp(seq + 1, emb->mount) == 0)
            staged = true;
        }
        if (staged)
          continue;

        char res_path[1200];
        if (!zan_resolve_package_resource(mdirs[m], resolved_stdlib_root,
                                          emb->rel_path, res_path,
                                          sizeof(res_path))) {
          continue;
        }

        char *spec_str =
            (char *)malloc(strlen(res_path) + strlen(emb->mount) + 8);
        if (!spec_str)
          continue;
        snprintf(spec_str, strlen(res_path) + strlen(emb->mount) + 8, "%s=%s",
                 res_path, emb->mount);
        if (embed_spec_count >= ZAN_MAX_EMBED_SPECS) {
          fprintf(stderr,
                  "warning: cannot auto-embed resource '%s' from '%s'; --embed "
                  "limit reached\n",
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
          const char *gate_val =
              emb->gate_key[0] ? zan_proj_get(emb->gate_key) : NULL;
          bool gate_enabled =
              !emb->gate_key[0] || (gate_val && strcmp(gate_val, "1") == 0);

          char *keep[64];
          int keep_count = 0;
          if (emb->baseline[0]) {
            char base_copy[128];
            strncpy(base_copy, emb->baseline, sizeof(base_copy) - 1);
            base_copy[sizeof(base_copy) - 1] = 0;
            char *save_b = NULL;
            char *b_tok = strtok_r(base_copy, ",", &save_b);
            while (b_tok && keep_count < 64) {
              while (*b_tok == ' ')
                b_tok++;
              size_t blen = strlen(b_tok);
              while (blen > 0 && b_tok[blen - 1] == ' ')
                b_tok[--blen] = 0;
              if (b_tok[0])
                keep[keep_count++] = strdup(b_tok);
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
                while (*f_tok == ' ')
                  f_tok++;
                size_t flen = strlen(f_tok);
                while (flen > 0 && f_tok[flen - 1] == ' ')
                  f_tok[--flen] = 0;
                if (f_tok[0] && strcmp(f_tok, "-") != 0) {
                  bool dup = false;
                  for (int j = 0; j < keep_count; j++) {
                    if (strcmp(keep[j], f_tok) == 0) {
                      dup = true;
                      break;
                    }
                  }
                  if (!dup)
                    keep[keep_count++] = strdup(f_tok);
                }
                f_tok = strtok_r(NULL, ",", &save_f);
              }
            }
          }
          if (keep_count > 0) {
            spec_subdirs[spec_at] =
                (char **)malloc((size_t)keep_count * sizeof(char *));
            if (spec_subdirs[spec_at]) {
              for (int k = 0; k < keep_count; k++) {
                spec_subdirs[spec_at][k] = keep[k];
              }
              spec_subdir_counts[spec_at] = keep_count;
            } else {
              for (int k = 0; k < keep_count; k++)
                free(keep[k]);
            }
          }
        }
      }
      zan_pkg_destroy(&pkg);
    }
    free(mdirs);
  }

  if (irgen.uses_embed_api && resolved_stdlib_root[0]) {
    char assets_dir[4200];
    snprintf(assets_dir, sizeof(assets_dir), "%s/assets", user_resource_root);
    bool assets_staged = false;
    for (int es = 0; es < embed_spec_count && !assets_staged; es++) {
      const char *seq = strrchr(embed_specs[es], '=');
      if (seq && strcmp(seq + 1, "assets") == 0)
        assets_staged = true;
      if (!seq) {
        const char *base = zan_path_basename(embed_specs[es]);
        if (base && strcmp(base, "assets") == 0)
          assets_staged = true;
      }
    }
    if (!assets_staged && zan_file_exists(assets_dir)) {
      const char *adir = assets_dir;
      assets_staged = true;
      char *assets_spec = (char *)malloc(strlen(adir) + 32);
      if (assets_spec) {
        snprintf(assets_spec, strlen(adir) + 32, "%s=assets", adir);
        if (embed_spec_count < ZAN_MAX_EMBED_SPECS) {
          embed_specs[embed_spec_count++] = assets_spec;
        } else {
          fprintf(stderr,
                  "warning: cannot auto-embed the "
                  "project assets from '%s'; --embed resources "
                  "limit reached\n",
                  adir);
          assets_staged = false;
          free(assets_spec);
        }
      }
    }
  }

  if (embed_spec_count > 0 || irgen.uses_embed_api) {
    zan_embed_spec_t *spec_items = (zan_embed_spec_t *)calloc(
        (size_t)embed_spec_count, sizeof(zan_embed_spec_t));
    if (spec_items) {
      for (int es = 0; es < embed_spec_count; es++) {
        spec_items[es].spec = embed_specs[es];
        spec_items[es].subdirs = (const char *const *)spec_subdirs[es];
        spec_items[es].subdir_count = spec_subdir_counts[es];
      }
    }
    int nres = spec_items ? zan_embed_emit_spec_list(&irgen, spec_items,
                                                     embed_spec_count)
                          : -1;
    free(spec_items);
    for (int es = 0; es < embed_spec_count; es++) {
      if (spec_subdirs[es]) {
        for (int k = 0; k < spec_subdir_counts[es]; k++)
          free(spec_subdirs[es][k]);
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

  char obj_tmp[1100];
  generated_object_vec_t generated_objects = {0};
  snprintf(obj_tmp, sizeof(obj_tmp), "%s/module.o", link_work_dir);

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
    generated_object_vec_remove(&generated_objects, obj_tmp);
    fprintf(stderr, "error: failed to emit object file\n");
    zan_diag_free_buffers(irgen.diag);
    zan_irgen_destroy(&irgen);
    zan_arena_free(ir_arena);
    zan_arena_free(arena);
    free(source);
    return 1;
  }

  generated_object_vec_add(&generated_objects, obj_tmp);
  for (int si = 1; si < irgen.split_obj_count; si++)
    generated_object_vec_add(&generated_objects, irgen.split_objs[si]);
  for (int si = 0; si < irgen.split_obj_count; si++)
    free(irgen.split_objs[si]);
  free(irgen.split_objs);
  irgen.split_objs = NULL;
  irgen.split_obj_count = 0;
  phase("write obj");
  probe_phase_mem("write obj");

  zan_irgen_release_llvm(&irgen);
  probe_phase_mem("free llvm");
