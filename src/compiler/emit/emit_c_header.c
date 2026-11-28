/* emit_c_header.c: Automatic C11/C++ header generator (--emit-header <file.h>)
 * Emits all user [Export] / [UnmanagedCallersOnly] functions and their
 * transitively referenced C-layout structs and enums in topological order. */

typedef struct {
  zan_symbol_t **items;
  int count;
  int cap;
} c_hdr_sym_list_t;

static bool c_hdr_list_has(const c_hdr_sym_list_t *l, zan_symbol_t *s) {
  for (int i = 0; i < l->count; i++) {
    if (l->items[i] == s)
      return true;
  }
  return false;
}

static void c_hdr_list_push(c_hdr_sym_list_t *l, zan_symbol_t *s) {
  if (!s || c_hdr_list_has(l, s))
    return;
  if (l->count >= l->cap) {
    l->cap = l->cap ? l->cap * 2 : 16;
    l->items = (zan_symbol_t **)realloc(l->items,
                                        (size_t)l->cap * sizeof(*l->items));
  }
  l->items[l->count++] = s;
}

static void c_hdr_collect_type(zan_irgen_t *g, zan_type_t *t,
                               c_hdr_sym_list_t *visiting,
                               c_hdr_sym_list_t *order) {
  if (!t)
    return;
  if (is_inline_array_type(t)) {
    c_hdr_collect_type(g, inline_array_elem(t), visiting, order);
    return;
  }
  if (t->kind == TYPE_ENUM && t->sym && t->sym->decl) {
    c_hdr_list_push(order, t->sym);
    return;
  }
  if (t->kind == TYPE_STRUCT && t->sym && t->sym->decl &&
      t->sym->decl->kind == AST_STRUCT_DECL && !t->sym->decl->from_stdlib &&
      !t->sym->decl->from_package) {
    if (c_hdr_list_has(order, t->sym) || c_hdr_list_has(visiting, t->sym))
      return;
    c_hdr_list_push(visiting, t->sym);
    zan_ast_node_t *sd = t->sym->decl;
    for (int i = 0; i < sd->type_decl.members.count; i++) {
      zan_ast_node_t *m = sd->type_decl.members.items[i];
      if (!m || (m->kind != AST_FIELD_DECL && m->kind != AST_PROPERTY_DECL))
        continue;
      if (m->field_decl.modifiers & MOD_STATIC)
        continue;
      if (m->kind == AST_PROPERTY_DECL &&
          (m->field_decl.getter_body || m->field_decl.setter_body))
        continue;
      zan_type_t *ft = zan_binder_resolve_type(g->binder, m->field_decl.type);
      c_hdr_collect_type(g, ft, visiting, order);
    }
    c_hdr_list_push(order, t->sym);
  }
}

static void c_hdr_format_type(zan_type_t *t, char *buf, size_t cap) {
  if (!t) {
    snprintf(buf, cap, "void");
    return;
  }
  switch (t->kind) {
  case TYPE_VOID:
    snprintf(buf, cap, "void");
    return;
  case TYPE_BOOL:
    snprintf(buf, cap, "bool");
    return;
  case TYPE_SBYTE:
    snprintf(buf, cap, "int8_t");
    return;
  case TYPE_BYTE:
    snprintf(buf, cap, "uint8_t");
    return;
  case TYPE_SHORT:
    snprintf(buf, cap, "int16_t");
    return;
  case TYPE_USHORT:
  case TYPE_CHAR:
    snprintf(buf, cap, "uint16_t");
    return;
  case TYPE_INT:
    snprintf(buf, cap, "int32_t");
    return;
  case TYPE_UINT:
    snprintf(buf, cap, "uint32_t");
    return;
  case TYPE_LONG:
    snprintf(buf, cap, "int64_t");
    return;
  case TYPE_ULONG:
    snprintf(buf, cap, "uint64_t");
    return;
  case TYPE_FLOAT:
    snprintf(buf, cap, "float");
    return;
  case TYPE_DOUBLE:
    snprintf(buf, cap, "double");
    return;
  case TYPE_NINT:
    snprintf(buf, cap, "intptr_t");
    return;
  case TYPE_STRUCT:
  case TYPE_ENUM:
    if (t->sym && t->sym->name.str) {
      snprintf(buf, cap, "%.*s", (int)t->sym->name.len, t->sym->name.str);
      return;
    }
    if (t->name.str) {
      snprintf(buf, cap, "%.*s", (int)t->name.len, t->name.str);
      return;
    }
    break;
  default:
    break;
  }
  snprintf(buf, cap, "intptr_t");
}

static void c_hdr_format_field_decl(zan_type_t *ft, zan_istr_t name,
                                    char *buf, size_t cap) {
  char cty[128];
  if (is_inline_array_type(ft)) {
    zan_type_t *et = inline_array_elem(ft);
    int64_t n = inline_array_len(ft);
    c_hdr_format_type(et, cty, sizeof(cty));
    snprintf(buf, cap, "%s %.*s[%lld]", cty, (int)name.len, name.str,
             (long long)(n > 0 ? n : 1));
  } else {
    c_hdr_format_type(ft, cty, sizeof(cty));
    snprintf(buf, cap, "%s %.*s", cty, (int)name.len, name.str);
  }
}

typedef struct {
  zan_ast_node_t *fd;
  zan_type_t *ft;
  int64_t offset;
  int64_t size;
  int decl_idx;
} c_hdr_field_t;

zan_status_t zan_irgen_write_c_header(zan_irgen_t *g, zan_ast_node_t *unit,
                                      const char *path) {
  if (!g || !unit || unit->kind != AST_COMPILATION_UNIT || !path || !path[0])
    return ZAN_ERROR;
  FILE *fp = fopen(path, "wb");
  if (!fp) {
    fprintf(stderr, "error: cannot open header output '%s'\n", path);
    return ZAN_ERROR;
  }

  c_hdr_sym_list_t visiting = {0};
  c_hdr_sym_list_t order = {0};

  /* 1. Collect all referenced structs/enums from exported C ABI functions & globals */
  for (int i = 0; i < unit->comp_unit.decls.count; i++) {
    zan_ast_node_t *decl = unit->comp_unit.decls.items[i];
    if (!decl || decl->from_stdlib || decl->from_package)
      continue;
    if (decl->kind != AST_CLASS_DECL && decl->kind != AST_STRUCT_DECL)
      continue;
    for (int j = 0; j < decl->type_decl.members.count; j++) {
      zan_ast_node_t *m = decl->type_decl.members.items[j];
      if (!m)
        continue;
      if (m->kind == AST_FIELD_DECL && (m->field_decl.modifiers & MOD_STATIC) &&
          field_c_attr_name(m, "Export", NULL, 0)) {
        zan_type_t *ft = zan_binder_resolve_type(g->binder, m->field_decl.type);
        c_hdr_collect_type(g, ft, &visiting, &order);
        continue;
      }
      if (m->kind != AST_METHOD_DECL)
        continue;
      if (!method_c_export_name(m, NULL, 0))
        continue;
      zan_type_t *rt = zan_binder_resolve_type(g->binder, m->method_decl.return_type);
      c_hdr_collect_type(g, rt, &visiting, &order);
      for (int k = 0; k < m->method_decl.params.count; k++) {
        zan_ast_node_t *p = m->method_decl.params.items[k];
        if (!p || p->kind != AST_PARAM)
          continue;
        zan_type_t *pt = zan_binder_resolve_type(g->binder, p->param.type);
        c_hdr_collect_type(g, pt, &visiting, &order);
      }
    }
  }

  fprintf(fp, "/* Generated by zanc (--emit-header). Do not edit manually. */\n");
  fprintf(fp, "#pragma once\n\n");
  fprintf(fp, "#include <stdbool.h>\n");
  fprintf(fp, "#include <stddef.h>\n");
  fprintf(fp, "#include <stdint.h>\n\n");
  fprintf(fp, "#if defined(__cplusplus)\n");
  fprintf(fp, "#define ZAN_ALIGNAS(n) alignas(n)\n");
  fprintf(fp, "#elif defined(__GNUC__) || defined(__clang__)\n");
  fprintf(fp, "#define ZAN_ALIGNAS(n) __attribute__((aligned(n)))\n");
  fprintf(fp, "#elif defined(_MSC_VER)\n");
  fprintf(fp, "#define ZAN_ALIGNAS(n) __declspec(align(n))\n");
  fprintf(fp, "#else\n");
  fprintf(fp, "#define ZAN_ALIGNAS(n)\n");
  fprintf(fp, "#endif\n\n");
  fprintf(fp, "#if defined(__GNUC__) || defined(__clang__)\n");
  fprintf(fp, "#define ZAN_CC_STDCALL __attribute__((stdcall))\n");
  fprintf(fp, "#define ZAN_CC_FASTCALL __attribute__((fastcall))\n");
  fprintf(fp, "#define ZAN_CC_VECTORCALL __attribute__((vectorcall))\n");
  fprintf(fp, "#define ZAN_CC_SYSV64 __attribute__((sysv_abi))\n");
  fprintf(fp, "#define ZAN_CC_WIN64 __attribute__((ms_abi))\n");
  fprintf(fp, "#elif defined(_MSC_VER)\n");
  fprintf(fp, "#define ZAN_CC_STDCALL __stdcall\n");
  fprintf(fp, "#define ZAN_CC_FASTCALL __fastcall\n");
  fprintf(fp, "#define ZAN_CC_VECTORCALL __vectorcall\n");
  fprintf(fp, "#define ZAN_CC_SYSV64\n");
  fprintf(fp, "#define ZAN_CC_WIN64\n");
  fprintf(fp, "#else\n");
  fprintf(fp, "#define ZAN_CC_STDCALL\n");
  fprintf(fp, "#define ZAN_CC_FASTCALL\n");
  fprintf(fp, "#define ZAN_CC_VECTORCALL\n");
  fprintf(fp, "#define ZAN_CC_SYSV64\n");
  fprintf(fp, "#define ZAN_CC_WIN64\n");
  fprintf(fp, "#endif\n\n");
  fprintf(fp, "#if defined(_MSC_VER)\n");
  fprintf(fp, "#define ZAN_NORETURN __declspec(noreturn)\n");
  fprintf(fp, "#elif defined(__cplusplus)\n");
  fprintf(fp, "#define ZAN_NORETURN [[noreturn]]\n");
  fprintf(fp, "#elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L\n");
  fprintf(fp, "#define ZAN_NORETURN _Noreturn\n");
  fprintf(fp, "#elif defined(__GNUC__) || defined(__clang__)\n");
  fprintf(fp, "#define ZAN_NORETURN __attribute__((noreturn))\n");
  fprintf(fp, "#else\n");
  fprintf(fp, "#define ZAN_NORETURN\n");
  fprintf(fp, "#endif\n\n");
  fprintf(fp, "#ifdef __cplusplus\nextern \"C\" {\n#endif\n\n");

  /* 2. Emit collected enums and structs in topological order */
  for (int i = 0; i < order.count; i++) {
    zan_symbol_t *sym = order.items[i];
    if (!sym || !sym->decl)
      continue;
    if (sym->kind == SYM_ENUM && sym->decl->kind == AST_ENUM_DECL) {
      fprintf(fp, "typedef int32_t %.*s;\n", (int)sym->name.len, sym->name.str);
      fprintf(fp, "enum {\n");
      int64_t next_val = 0;
      for (int m = 0; m < sym->decl->type_decl.members.count; m++) {
        zan_ast_node_t *em = sym->decl->type_decl.members.items[m];
        if (!em || em->kind != AST_ENUM_MEMBER)
          continue;
        if (em->enum_member.value && em->enum_member.value->kind == AST_INT_LITERAL)
          next_val = em->enum_member.value->int_val;
        fprintf(fp, "    %.*s_%.*s = %lld,\n", (int)sym->name.len, sym->name.str,
                (int)em->enum_member.name.len, em->enum_member.name.str,
                (long long)next_val);
        next_val++;
      }
      fprintf(fp, "};\n\n");
    } else if (sym->kind == SYM_STRUCT && sym->decl->kind == AST_STRUCT_DECL) {
      unsigned long pack = sym->decl->type_decl.pack_align;
      bool is_explicit = sym->decl->type_decl.is_explicit_layout;
      LLVMTargetDataRef td = LLVMGetModuleDataLayout(g->mod);
      if (sym->type)
        (void)map_type(g, sym->type);
      struct zan_struct_type_entry *se = NULL;
      for (int si = 0; si < g->struct_type_count; si++) {
        if (g->struct_types[si].sym == sym) {
          se = &g->struct_types[si];
          break;
        }
      }
      int64_t req_align = 0;
      bool has_req_align = zan_ast_attr_int(sym->decl, "Align", &req_align) &&
                           req_align >= 1 && (req_align & (req_align - 1)) == 0;
      c_hdr_field_t fields[256];
      int fcount = 0;
      for (int m = 0; m < sym->decl->type_decl.members.count && fcount < 256; m++) {
        zan_ast_node_t *fd = sym->decl->type_decl.members.items[m];
        if (!fd || (fd->kind != AST_FIELD_DECL && fd->kind != AST_PROPERTY_DECL))
          continue;
        if (fd->field_decl.modifiers & MOD_STATIC)
          continue;
        if (fd->kind == AST_PROPERTY_DECL &&
            (fd->field_decl.getter_body || fd->field_decl.setter_body))
          continue;
        zan_type_t *ft = zan_binder_resolve_type(g->binder, fd->field_decl.type);
        int64_t foff = (is_explicit && se && se->field_offsets && fcount < se->field_count)
                           ? (int64_t)se->field_offsets[fcount]
                           : 0;
        int64_t fsz = 0;
        if (ft && td) {
          LLVMTypeRef lft = map_type(g, ft);
          if (lft)
            fsz = (int64_t)LLVMABISizeOfType(td, lft);
        }
        fields[fcount].fd = fd;
        fields[fcount].ft = ft;
        fields[fcount].offset = foff;
        fields[fcount].size = fsz > 0 ? fsz : 1;
        fields[fcount].decl_idx = fcount;
        fcount++;
      }
      bool has_overlap = false;
      bool all_zero_off = (fcount > 1);
      if (is_explicit && fcount > 1) {
        /* Sort by (offset, decl_idx) to detect overlap clusters. */
        for (int a = 1; a < fcount; a++) {
          c_hdr_field_t key = fields[a];
          int b = a - 1;
          while (b >= 0 && (fields[b].offset > key.offset ||
                            (fields[b].offset == key.offset &&
                             fields[b].decl_idx > key.decl_idx))) {
            fields[b + 1] = fields[b];
            b--;
          }
          fields[b + 1] = key;
        }
        int64_t max_end = fields[0].offset + fields[0].size;
        if (fields[0].offset != 0)
          all_zero_off = false;
        for (int a = 1; a < fcount; a++) {
          if (fields[a].offset != 0)
            all_zero_off = false;
          if (fields[a].offset < max_end)
            has_overlap = true;
          int64_t end = fields[a].offset + fields[a].size;
          if (end > max_end)
            max_end = end;
        }
      }
      if (has_overlap && all_zero_off) {
        if (pack > 0)
          fprintf(fp, "#pragma pack(push, %lu)\n", pack);
        if (has_req_align)
          fprintf(fp, "typedef union ZAN_ALIGNAS(%lld) %.*s {\n",
                  (long long)req_align, (int)sym->name.len, sym->name.str);
        else
          fprintf(fp, "typedef union %.*s {\n", (int)sym->name.len, sym->name.str);
        for (int a = 0; a < fcount; a++) {
          char fdecl[256];
          c_hdr_format_field_decl(fields[a].ft, fields[a].fd->field_decl.name,
                                  fdecl, sizeof(fdecl));
          fprintf(fp, "    %s;\n", fdecl);
        }
        fprintf(fp, "} %.*s;\n", (int)sym->name.len, sym->name.str);
        if (pack > 0)
          fprintf(fp, "#pragma pack(pop)\n");
        fprintf(fp, "\n");
      } else {
        if (pack > 0)
          fprintf(fp, "#pragma pack(push, %lu)\n", pack);
        else if (is_explicit)
          fprintf(fp, "#pragma pack(push, 1)\n");
        if (has_req_align)
          fprintf(fp, "typedef struct ZAN_ALIGNAS(%lld) %.*s {\n",
                  (long long)req_align, (int)sym->name.len, sym->name.str);
        else
          fprintf(fp, "typedef struct %.*s {\n", (int)sym->name.len, sym->name.str);
        int64_t cur_off = 0;
        int pad_idx = 0;
        int upad_idx = 0;
        if (has_overlap) {
          int fi = 0;
          while (fi < fcount) {
            int64_t c_min = fields[fi].offset;
            int64_t c_end = fields[fi].offset + fields[fi].size;
            int fj = fi + 1;
            while (fj < fcount && fields[fj].offset < c_end) {
              int64_t end = fields[fj].offset + fields[fj].size;
              if (end > c_end)
                c_end = end;
              fj++;
            }
            if (c_min > cur_off) {
              fprintf(fp, "    uint8_t _zan_pad_%d[%lld];\n", pad_idx++,
                      (long long)(c_min - cur_off));
            }
            if (fj - fi == 1) {
              char fdecl[256];
              c_hdr_format_field_decl(fields[fi].ft, fields[fi].fd->field_decl.name,
                                      fdecl, sizeof(fdecl));
              fprintf(fp, "    %s;\n", fdecl);
            } else {
              fprintf(fp, "    union {\n");
              for (int fk = fi; fk < fj; fk++) {
                char fdecl[256];
                c_hdr_format_field_decl(fields[fk].ft, fields[fk].fd->field_decl.name,
                                        fdecl, sizeof(fdecl));
                if (fields[fk].offset == c_min) {
                  fprintf(fp, "        %s;\n", fdecl);
                } else {
                  fprintf(fp,
                          "        struct { uint8_t _zan_u_pad_%d[%lld]; %s; };\n",
                          upad_idx++, (long long)(fields[fk].offset - c_min), fdecl);
                }
              }
              fprintf(fp, "    };\n");
            }
            cur_off = c_end;
            fi = fj;
          }
        } else {
          for (int a = 0; a < fcount; a++) {
            if (is_explicit && fields[a].offset > cur_off) {
              fprintf(fp, "    uint8_t _zan_pad_%d[%lld];\n", pad_idx++,
                      (long long)(fields[a].offset - cur_off));
              cur_off = fields[a].offset;
            }
            char fdecl[256];
            c_hdr_format_field_decl(fields[a].ft, fields[a].fd->field_decl.name,
                                    fdecl, sizeof(fdecl));
            fprintf(fp, "    %s;\n", fdecl);
            if (is_explicit)
              cur_off += fields[a].size;
          }
        }
        if (fcount == 0)
          fprintf(fp, "    uint8_t _zan_empty;\n");
        fprintf(fp, "} %.*s;\n", (int)sym->name.len, sym->name.str);
        if (pack > 0 || is_explicit)
          fprintf(fp, "#pragma pack(pop)\n");
        fprintf(fp, "\n");
      }
    }
  }

  /* 3. Emit exported C ABI globals */
  for (int i = 0; i < unit->comp_unit.decls.count; i++) {
    zan_ast_node_t *decl = unit->comp_unit.decls.items[i];
    if (!decl || decl->from_stdlib || decl->from_package)
      continue;
    if (decl->kind != AST_CLASS_DECL && decl->kind != AST_STRUCT_DECL)
      continue;
    for (int j = 0; j < decl->type_decl.members.count; j++) {
      zan_ast_node_t *m = decl->type_decl.members.items[j];
      if (!m || m->kind != AST_FIELD_DECL || !(m->field_decl.modifiers & MOD_STATIC))
        continue;
      char csym[512];
      if (!field_c_attr_name(m, "Export", csym, sizeof(csym)))
        continue;
      zan_type_t *ft = zan_binder_resolve_type(g->binder, m->field_decl.type);
      char sec_buf[128];
      bool is_const = (m->field_decl.modifiers & MOD_READONLY) != 0 ||
                      (zan_ast_attr_string(m, "Section", sec_buf, sizeof(sec_buf)) &&
                       (strstr(sec_buf, "rodata") != NULL ||
                        strstr(sec_buf, "rdata") != NULL));
      char cty[128];
      if (is_inline_array_type(ft)) {
        zan_type_t *et = inline_array_elem(ft);
        int64_t n = inline_array_len(ft);
        c_hdr_format_type(et, cty, sizeof(cty));
        fprintf(fp, "extern %s%s %s[%lld];\n", is_const ? "const " : "", cty,
                csym, (long long)(n > 0 ? n : 1));
      } else {
        c_hdr_format_type(ft, cty, sizeof(cty));
        fprintf(fp, "extern %s%s %s;\n", is_const ? "const " : "", cty, csym);
      }
    }
  }

  /* 4. Emit exported C ABI function prototypes */
  for (int i = 0; i < unit->comp_unit.decls.count; i++) {
    zan_ast_node_t *decl = unit->comp_unit.decls.items[i];
    if (!decl || decl->from_stdlib || decl->from_package)
      continue;
    if (decl->kind != AST_CLASS_DECL && decl->kind != AST_STRUCT_DECL)
      continue;
    for (int j = 0; j < decl->type_decl.members.count; j++) {
      zan_ast_node_t *m = decl->type_decl.members.items[j];
      if (!m || m->kind != AST_METHOD_DECL)
        continue;
      char csym[512];
      if (!method_c_export_name(m, csym, sizeof(csym)))
        continue;
      zan_type_t *rt = zan_binder_resolve_type(g->binder, m->method_decl.return_type);
      char rty[128];
      c_hdr_format_type(rt, rty, sizeof(rty));
      const char *cc_macro = "";
      method_calling_conv(m, NULL, &cc_macro);
      bool is_noreturn = zan_ast_has_attr(m, "NoReturn");
      fprintf(fp, "%s%s %s%s(", is_noreturn ? "ZAN_NORETURN " : "", rty,
              cc_macro ? cc_macro : "", csym);
      if (m->method_decl.params.count == 0) {
        fprintf(fp, "void");
      } else {
        for (int k = 0; k < m->method_decl.params.count; k++) {
          zan_ast_node_t *p = m->method_decl.params.items[k];
          if (k > 0)
            fprintf(fp, ", ");
          if (!p || p->kind != AST_PARAM)
            continue;
          zan_type_t *pt = zan_binder_resolve_type(g->binder, p->param.type);
          char pty[128];
          c_hdr_format_type(pt, pty, sizeof(pty));
          if (p->param.by_ref != 0)
            fprintf(fp, "%s *%.*s", pty, (int)p->param.name.len,
                    p->param.name.str);
          else
            fprintf(fp, "%s %.*s", pty, (int)p->param.name.len,
                    p->param.name.str);
        }
      }
      fprintf(fp, ");\n");
    }
  }

  fprintf(fp, "\n#ifdef __cplusplus\n}\n#endif\n");
  fclose(fp);
  free(visiting.items);
  free(order.items);
  return ZAN_OK;
}

static bool freestanding_symbol_forbidden(const char *name) {
  if (!name || !name[0])
    return false;
  if (strncmp(name, "llvm.", 5) == 0)
    return false;
  if (strncmp(name, "zan_rt_", 7) == 0 || strncmp(name, "__zan_", 6) == 0 ||
      strncmp(name, "zan_co_", 7) == 0 || strncmp(name, "zan_utf8_", 9) == 0)
    return true;
  static const char *const crt_syms[] = {
      "malloc", "calloc", "realloc", "free", "printf", "fprintf", "puts",
      "putchar", "fwrite", "fopen", "fclose", "fflush", "exit", "_exit",
      "abort", "getenv", "setvbuf", "_setmode", "__acrt_iob_func",
      "SetConsoleOutputCP", "SetConsoleCP", "qsort", "strcmp", "strspn",
      "strcspn", "memchr", NULL};
  for (int i = 0; crt_syms[i]; i++) {
    if (strcmp(name, crt_syms[i]) == 0)
      return true;
  }
  return false;
}

zan_status_t zan_irgen_audit_freestanding(zan_irgen_t *g) {
  if (!g || !g->mod || (!g->freestanding && !g->emit_obj))
    return ZAN_OK;
  if (g->builder)
    LLVMClearInsertionPosition(g->builder);
  /* Strip unused Zan runtime / GC weak & internal stubs after all passes. */
  for (int pass = 0; pass < 4; pass++) {
    bool changed = false;
    LLVMValueRef fn = LLVMGetFirstFunction(g->mod);
    while (fn) {
      LLVMValueRef next = LLVMGetNextFunction(fn);
      const char *nm = LLVMGetValueName(fn);
      bool is_rt = nm && (strncmp(nm, "zan_rt_", 7) == 0 ||
                          strncmp(nm, "zan_gc_", 7) == 0 ||
                          strncmp(nm, "zan_co_", 7) == 0 ||
                          strncmp(nm, "__zan_stdio_", 12) == 0 ||
                          strncmp(nm, "__zan_live_", 11) == 0 ||
                          strncmp(nm, "__zan_rt_superseded_", 20) == 0);
      LLVMLinkage lnk = LLVMGetLinkage(fn);
      bool removable = LLVMIsDeclaration(fn) ||
                       lnk == LLVMWeakAnyLinkage ||
                       lnk == LLVMInternalLinkage ||
                       lnk == LLVMPrivateLinkage;
      if (is_rt && removable && !LLVMGetFirstUse(fn)) {
        LLVMDeleteFunction(fn);
        changed = true;
      }
      fn = next;
    }
    if (!changed)
      break;
  }
  if (!g->freestanding)
    return ZAN_OK;
  bool bad = false;
  for (LLVMValueRef fn = LLVMGetFirstFunction(g->mod); fn;
       fn = LLVMGetNextFunction(fn)) {
    if (!LLVMIsDeclaration(fn))
      continue;
    if (!LLVMGetFirstUse(fn))
      continue;
    const char *name = LLVMGetValueName(fn);
    if (freestanding_symbol_forbidden(name)) {
      zan_loc_t loc = {0};
      zan_diag_emit_code(
          g->diag, DIAG_ERROR, loc, 3102,
          "--freestanding module references forbidden runtime/CRT symbol '%s'",
          name);
      bad = true;
    }
  }
  return bad ? ZAN_ERROR : ZAN_OK;
}
