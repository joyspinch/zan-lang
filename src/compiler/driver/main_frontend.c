/* FRAGMENT: host=main
 * 拼接片段（非独立 TU）：由 main.c 的 main() 函数体 #include，
 * 位于 source 读入与 --dump-tokens 早退之后、main_codegen.c 之前。
 * consumes: source, source_len, input_file, input_count, target,
 *   publish_mode, cross_compiling, resolved_stdlib_root, do_deny_warnings,
 *   check_leaks_opt, arc_guard_opt
 * defines: arena, diag, ast, binder, irgen, ir_arena, native_arch, want_shard
 * （下游 main_codegen.c / main_linker.c 在同一 main() 作用域继续消费。） */
/* parse */
zan_arena_t *arena = zan_arena_new();
g_main_arena = arena;
zan_diag_t *diag = zan_diag_new(arena);
diag->jsonl = opts.diagnostics_jsonl;
zan_diag_set_deny_warnings(diag, do_deny_warnings);
/* A382②：--warn-level none|default|error（error 兼容 --deny-warnings）；
 * --suppress 逗号分隔诊断编号（W0003 / 3 皆可，字母仅作文档）。 */
{
  int warn_level = 1;
  if (warn_level_arg) {
    if (strcmp(warn_level_arg, "none") == 0)
      warn_level = 0;
    else if (strcmp(warn_level_arg, "default") == 0)
      warn_level = 1;
    else if (strcmp(warn_level_arg, "error") == 0) {
      warn_level = 2;
      do_deny_warnings = true;
      zan_diag_set_deny_warnings(diag, true);
    } else {
      fprintf(stderr, "error: unknown --warn-level '%s' (none|default|error)\n",
              warn_level_arg);
      return 2;
    }
  }
  zan_diag_set_warn_level(diag, warn_level);
  if (suppress_arg) {
    const char *p = suppress_arg;
    while (*p) {
      while (*p == ',' || *p == ' ')
        p++;
      if (!*p)
        break;
      const char *tok = p;
      while (*p && *p != ',' && *p != ' ')
        p++;
      int code = 0;
      for (const char *q = tok; q < p; q++) {
        if (*q >= '0' && *q <= '9')
          code = code * 10 + (*q - '0');
      }
      if (code > 0)
        zan_diag_suppress_code(diag, code);
    }
  }
}
if (error_limit >= 0)
  zan_diag_set_max_errors(diag, error_limit);

zan_ast_node_t *ast = NULL;
int scanned = 0;
for (;;) {
  int round_end = input_count;
  zan_compile_trace("pullin round begin: scanned=%d inputs=%d", scanned,
                    input_count);
  double __rt0 = now_ms();
  for (int fi = scanned; fi < round_end; fi++) {
    if (fi > 0 && zan_is_zcomp_path(input_files[fi]))
      continue;
    size_t slen = 0;
    char *src =
        (fi == 0) ? source : read_file_arena(arena, input_files[fi], &slen);
    if (fi == 0)
      slen = source_len;
    if (!src) {
      fprintf(stderr, "error: cannot read '%s'\n", input_files[fi]);
      zan_arena_free(arena);
      free(source);
      return 1;
    }
    int file_id = diag->file_count;
    zan_diag_add_file(diag, input_files[fi], src);

    zan_lexer_t lex;
    zan_lexer_init(&lex, src, slen, file_id, arena, diag);

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
      int seed_entry =
          fi < explicit_input_count && !zan_is_zcomp_path(input_files[fi]);
      pi_seed_stdlib_input =
          seed_entry ? pi_reach_input_dir(input_files[fi]) : 0;
      pi_seeding_stdlib =
          fi > 0 && auto_stdlib && resolved_stdlib_root[0] && input_files[fi] &&
          zan_path_is_under(input_files[fi], resolved_stdlib_root);
      if (parse_failed) {
        g_scale_stats.throwaway_parse_fallbacks++;
        pi_seed_source(src, slen, opts.diagnostics_jsonl);
      } else {
        pi_seed_parsed_unit(unit, seed_entry);
        pi_seed_manifest_unit(unit, input_files[fi]);
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
    if ((gen_meta_path || fi >= explicit_input_count) && input_files[fi] &&
        zan_path_is_package_source(input_files[fi])) {
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
        zan_ast_list_push(&ast->comp_unit.decls, unit->comp_unit.decls.items[k],
                          arena);
      if (!ast->comp_unit.ns)
        ast->comp_unit.ns = unit->comp_unit.ns;
    }
  }
  scanned = round_end;
  if (pi_filter_active) {
    double __ct0 = now_ms();
    int changed = pi_close_once(resolved_stdlib_root);
    int fresh = pi_append_included(&input_files, &input_count, &input_cap);
    zan_compile_trace("pullin round done: parse=%.1fms close=%.1fms changed=%d "
                      "fresh=%d inputs=%d",
                      __ct0 - __rt0, now_ms() - __ct0, changed, fresh,
                      input_count);
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
if (missing_namespace_count > 0) {
  fprintf(stderr,
          "error: %d unresolved imported namespace(s); "
          "see ZANPKG_MISSING diagnostics above\n",
          missing_namespace_count);
  package_sources_destroy();
  zan_diag_free_buffers(diag);
  zan_arena_free(arena);
  free(source);
  return 3;
}
phase("parse");
probe_phase_mem("parse");
if (g_time_phases)
  zan_arena_dump_stats();

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

  if (gen_meta_path && !zan_diag_has_errors(diag)) {
    char *meta = zan_genmeta_export_files(ast, diag, gen_meta_bodies);
    if (!meta) {
      fprintf(stderr, "error: cannot export metadata\n");
      zan_arena_free(arena);
      free(source);
      return 1;
    }
    FILE *out = fopen(gen_meta_path, "wb");
    int ok = out && fwrite(meta, 1, strlen(meta), out) == strlen(meta) &&
             fclose(out) == 0;
    free(meta);
    if (!ok) {
      fprintf(stderr, "error: cannot write metadata '%s'\n", gen_meta_path);
      zan_arena_free(arena);
      free(source);
      return 1;
    }
    zan_arena_free(arena);
    free(source);
    return 0;
  }

  bool pkg_gen_added = false;
  if (!no_pkg_gen && !packages_disabled && !emit_symbols_path &&
      !gen_meta_path && !zan_diag_has_errors(diag) && package_project_root[0]) {
    int mcap = 64;
    char(*mdirs)[1024] = (char(*)[1024])malloc((size_t)mcap * sizeof(*mdirs));
    if (mdirs) {
      int mcnt = zan_pkg_all_manifest_dirs(package_project_root, mdirs, mcap);
      char (*gen_tools)[128] =
          (char(*)[128])calloc((size_t)(mcnt > 0 ? mcnt : 1), sizeof(*gen_tools));
      char (*gen_pkg_dirs)[1024] =
          (char(*)[1024])calloc((size_t)(mcnt > 0 ? mcnt : 1), sizeof(*gen_pkg_dirs));
      char (*gen_pkg_names)[128] =
          (char(*)[128])calloc((size_t)(mcnt > 0 ? mcnt : 1), sizeof(*gen_pkg_names));
      int gen_count = 0;
      for (int m = 0; m < mcnt && gen_tools && gen_pkg_dirs && gen_pkg_names; m++) {
        char tools_dir[1200];
        snprintf(tools_dir, sizeof(tools_dir), "%s/tools", mdirs[m]);
        bool pkg_used = false;
        for (int fi = 0; fi < input_count && !pkg_used; fi++) {
          if (input_files[fi] &&
              zan_path_under_dir_folded(input_files[fi], mdirs[m]) &&
              !zan_path_under_dir_folded(input_files[fi], tools_dir))
            pkg_used = true;
        }
        if (!pkg_used)
          continue;
        char mpath[1200];
        snprintf(mpath, sizeof(mpath), "%s/zan.pkg", mdirs[m]);
        zan_package_t pkg;
        if (!zan_pkg_load(&pkg, mpath))
          continue;
        if (pkg.generator[0]) {
          snprintf(gen_tools[gen_count], sizeof(gen_tools[gen_count]), "%s",
                   pkg.generator);
          snprintf(gen_pkg_dirs[gen_count], sizeof(gen_pkg_dirs[gen_count]), "%s",
                   mdirs[m]);
          snprintf(gen_pkg_names[gen_count], sizeof(gen_pkg_names[gen_count]), "%s",
                   pkg.name[0] ? pkg.name : pkg.generator);
          gen_count++;
        }
        zan_pkg_destroy(&pkg);
      }
      free(mdirs);

      if (gen_count > 0) {
        char gen_work_dir[1024] = {0};
        const char *anchor = output_file ? output_file : input_file;
        char *meta = zan_genmeta_export_files(ast, diag, true);
        if (!meta || !zan_create_gen_work_dir(anchor, gen_work_dir, sizeof(gen_work_dir))) {
          free(meta);
          free(gen_tools);
          free(gen_pkg_dirs);
          free(gen_pkg_names);
          fprintf(stderr, "error: cannot prepare package generator staging\n");
          package_sources_destroy();
          zan_diag_free_buffers(diag);
          zan_arena_free(arena);
          free(source);
          return 1;
        }
        char meta_tmp[1200];
        snprintf(meta_tmp, sizeof(meta_tmp), "%s/declarations.json", gen_work_dir);
        FILE *mf = fopen(meta_tmp, "wb");
        size_t mlen = strlen(meta);
        bool wrote_meta = mf && fwrite(meta, 1, mlen, mf) == mlen && fclose(mf) == 0;
        free(meta);
        if (!wrote_meta) {
          remove(meta_tmp);
          zan_remove_dir_empty(gen_work_dir);
          free(gen_tools);
          free(gen_pkg_dirs);
          free(gen_pkg_names);
          fprintf(stderr, "error: cannot write metadata '%s'\n", meta_tmp);
          package_sources_destroy();
          zan_diag_free_buffers(diag);
          zan_arena_free(arena);
          free(source);
          return 1;
        }

        for (int gidx = 0; gidx < gen_count; gidx++) {
          char tool_path[1280];
          zan_resolve_pkg_generator(gen_tools[gidx], gen_pkg_dirs[gidx],
                                    tool_path, sizeof(tool_path));
          char gen_out[1200];
          snprintf(gen_out, sizeof(gen_out), "%s/gen-%d.zan", gen_work_dir, gidx);
          int grc = zan_run_pkg_generator(tool_path, meta_tmp, gen_out);
          if (grc != 0) {
            remove(gen_out);
            remove(meta_tmp);
            zan_remove_dir_empty(gen_work_dir);
            if (grc < 0) {
              fprintf(stderr,
                      "error: cannot find generator '%s' declared by package '%s'\n",
                      gen_tools[gidx], gen_pkg_names[gidx]);
            } else {
              fprintf(stderr,
                      "error: generator '%s' for package '%s' failed (exit %d)\n",
                      gen_tools[gidx], gen_pkg_names[gidx], grc);
            }
            free(gen_tools);
            free(gen_pkg_dirs);
            free(gen_pkg_names);
            package_sources_destroy();
            zan_diag_free_buffers(diag);
            zan_arena_free(arena);
            free(source);
            return 1;
          }
          size_t glen = 0;
          char *gsrc = read_file_arena(arena, gen_out, &glen);
          remove(gen_out);
          bool has_content = false;
          if (gsrc) {
            for (size_t k = 0; k < glen; k++) {
              unsigned char ch = (unsigned char)gsrc[k];
              if (ch > ' ') {
                has_content = true;
                break;
              }
            }
          }
          if (has_content) {
            char label[256];
            snprintf(label, sizeof(label), "%s.generated.zan",
                     gen_pkg_names[gidx]);
            char *glabel = zan_arena_strdup(arena, label, strlen(label));
            int file_id = diag->file_count;
            zan_diag_add_file(diag, glabel ? glabel : label, gsrc);
            zan_lexer_t glex;
            zan_lexer_init(&glex, gsrc, glen, file_id, arena, diag);
            zan_apply_lex_defines(&glex, target, pp_defines, pp_define_count,
                                  publish_mode);
            zan_parser_t gparser;
            zan_parser_init(&gparser, &glex, arena, diag);
            zan_ast_node_t *gunit = zan_parser_parse(&gparser);
            if (gunit) {
              pkg_gen_added = true;
              zan_nsresolve_stamp(gunit, arena);
              if (pi_filter_active)
                pi_seed_parsed_unit(gunit, 1);
              zan_parser_flatten_nested_types(gunit, arena, diag);
              for (int k = 0; k < gunit->comp_unit.usings.count; k++)
                zan_ast_list_push(&ast->comp_unit.usings,
                                  gunit->comp_unit.usings.items[k], arena);
              for (int k = 0; k < gunit->comp_unit.decls.count; k++)
                zan_ast_list_push(&ast->comp_unit.decls,
                                  gunit->comp_unit.decls.items[k], arena);
            }
          }
        }
        remove(meta_tmp);
        zan_remove_dir_empty(gen_work_dir);
      }
      free(gen_tools);
      free(gen_pkg_dirs);
      free(gen_pkg_names);
    }
  }

  if (pi_filter_active) {
    for (;;) {
      int changed = pi_close_once(resolved_stdlib_root);
      int fresh = pi_append_included(&input_files, &input_count, &input_cap);
      if (!changed && !fresh) {
        if (!pi_repair_done && pi_unsatisfied_live_name()) {
          pi_repair_done = 1;
          pi_repair_walk(resolved_stdlib_root, "");
          continue;
        }
        pi_debug_dump();
        break;
      }
      for (int fi = input_count - fresh; fresh > 0 && fi < input_count; fi++) {
        int errors_before = diag->error_count;
        zan_ast_node_t *unit =
            parse_secondary_unit(input_files[fi], target, pp_defines,
                                 pp_define_count, publish_mode, arena, diag);
        if (!unit) {
          fprintf(stderr, "error: cannot read '%s'\n", input_files[fi]);
          zan_arena_free(arena);
          free(source);
          return 1;
        }
        zan_nsresolve_stamp(unit, arena);
        pi_seeding_stdlib =
            auto_stdlib && resolved_stdlib_root[0] &&
            zan_path_is_under(input_files[fi], resolved_stdlib_root);
        if (diag->error_count > errors_before) {
          g_scale_stats.throwaway_parse_fallbacks++;
          size_t flen = 0;
          char *fsrc = read_file(input_files[fi], &flen);
          if (fsrc) {
            pi_seed_source(fsrc, flen, opts.diagnostics_jsonl);
            free(fsrc);
          }
        } else {
          pi_seed_parsed_unit(unit, 0);
          pi_seed_manifest_unit(unit, input_files[fi]);
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
        zan_parser_flatten_nested_types(unit, arena, diag);
        pkg_gen_added = true;
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
    pi_reset_manifest_roots();
  }
  if (pkg_gen_added) {
    zan_compile_trace("merge partials (post pkg-gen)");
    zan_parser_merge_partials(ast, arena, diag);
    zan_compile_trace("desugar events (post pkg-gen)");
    zan_parser_desugar_events(ast, arena, diag);
  }
  zan_compile_trace("nsresolve final");
  zan_nsresolve_run(ast, arena, diag);
  if (pkg_gen_added) {
    zan_compile_trace("specialize generic bases (post pkg-gen)");
    zan_parser_specialize_generic_bases(ast, arena, diag);
  }
  zan_compile_trace("prune");
  zan_nsresolve_prune(ast, arena, diag);
  zan_compile_trace("gui kind manifest");
  zan_synthesize_gui_kind_manifests(ast, arena, diag);
  phase("prune");
  zan_compile_trace("resolve done");
}

package_sources_destroy();
if (missing_namespace_count > 0) {
  fprintf(stderr,
          "error: %d unresolved imported namespace(s); "
          "see ZANPKG_MISSING diagnostics above\n",
          missing_namespace_count);
  zan_diag_free_buffers(diag);
  zan_arena_free(arena);
  free(source);
  return 3;
}

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
  fprintf(stderr, "\n%d error(s), %d warning(s)\n", diag->error_count,
          diag->warning_count);
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
semantic_query_t semantic_refs = {0};
if (opts.semantic_refs_file &&
    semantic_refs_find_file(&semantic_refs, diag, opts.semantic_refs_file)) {
  binder.symbol_reference = semantic_refs_record_symbol;
  binder.symbol_reference_context = &semantic_refs.refs;
}
zan_compile_trace("bind");
zan_binder_bind(&binder, ast);

phase("bind");
probe_phase_mem("bind");

zan_checker_t checker;
zan_checker_init(&checker, &binder, arena, diag);
if (binder.symbol_reference) {
  checker.lexical_ref = semantic_refs_record;
  checker.lexical_ref_context = &semantic_refs.refs;
}
zan_compile_trace("check");
zan_checker_check(&checker, ast);

if (do_dump_ast) {
}

if (zan_diag_has_errors(diag)) {
  fprintf(stderr, "\n%d error(s) after type checking\n", diag->error_count);
  report_suppressed_errors(diag);
  free(semantic_refs.refs.items);
  zan_diag_free_buffers(diag);
  zan_arena_free(arena);
  free(source);
  return 1;
}

phase("check");
probe_phase_mem("check");

if (opts.semantic_refs_file) {
  int rc = 1;
  if (checker.lexical_ref) {
    rc = semantic_refs_print(&semantic_refs, diag, opts.semantic_refs_line,
                             opts.semantic_refs_col);
  } else {
    fprintf(stderr,
            "error: --semantic-refs requires one loaded source matching '%s'\n",
            opts.semantic_refs_file);
  }
  free(semantic_refs.refs.items);
  zan_diag_free_buffers(diag);
  zan_arena_free(arena);
  free(source);
  return rc;
}

zan_irgen_t irgen;
const char *irgen_triple = NULL;
if (cross_compiling) {
  if (target.os == ZAN_OS_WINDOWS)
    irgen_triple = (target.arch == ZAN_ARCH_AARCH64) ? "aarch64-w64-windows-gnu"
                                                     : "x86_64-w64-windows-gnu";
  else
    irgen_triple = target.triple;
}
if (output_file && !emit_lib) {
  const char *ext = strrchr(output_file, '.');
  if (ext) {
    if (strcmp(ext, ".dll") == 0 || strcmp(ext, ".so") == 0 ||
        strcmp(ext, ".dylib") == 0) {
      emit_lib = true;
      lib_shared = true;
    } else if (strcmp(ext, ".a") == 0 || strcmp(ext, ".lib") == 0) {
      emit_lib = true;
      lib_shared = false;
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
    if (!proj_android_keys_ok)
      return 1;
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
bool check_leaks =
    check_leaks_opt >= 0 ? check_leaks_opt != 0 : (debug_info && !publish_mode);
bool arc_guard =
    arc_guard_opt >= 0 ? arc_guard_opt != 0 : (debug_info && !publish_mode);
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
if (zan_irgen_init(&irgen, ir_arena, diag, &binder, input_file, irgen_triple,
                   target.os == ZAN_OS_WINDOWS, external_async_executor,
                   check_leaks, runtime_checks, arc_guard, arc_net) != ZAN_OK) {
  fprintf(stderr, "error: failed to initialize code generator\n");
  zan_diag_free_buffers(diag);
  zan_arena_free(ir_arena);
  zan_arena_free(arena);
  free(source);
  return 1;
}
irgen.cpu_profile = opts.cpu_profile;
irgen.emit_debug = debug_info;
irgen.strict_runtime = strict_runtime;
irgen.publish_mode = publish_mode;
irgen.codegen_level = opts.codegen_level;
irgen.shard_opt = opts.shard_opt;
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
    fprintf(stderr, "warning: cannot open site map file for writing: %s\n",
            site_map_path);
  } else {
    fprintf(irgen.site_map_fp, "# Zan Diagnostic Site Map\n# Input: %s\n",
            input_file ? input_file : "zan");
  }
}
irgen.fast_codegen = false;
irgen.emit_lib = emit_lib;
irgen.emit_shared = lib_shared;

const char *shard_env = getenv("ZAN_SHARD");
const char *no_shard_env = getenv("ZAN_NO_SHARD");
bool native_arch =
    (target.arch == ZAN_ARCH_X86_64 || target.arch == ZAN_ARCH_AARCH64);
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
