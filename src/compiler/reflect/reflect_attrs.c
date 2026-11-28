/* Declaration records preserve attribute and argument order. A starts an
 * attribute; each argument carries a kind, optional name and text. Lengths
 * count UTF-8 bytes so delimiters in a literal stay part of its value. */
static void refl_attr_append(char **data, size_t *len, size_t *cap,
                             const char *text, size_t count) {
  if (count > (size_t)INT_MAX - 1 - *len)
    zan_host_oom();
  size_t need = *len + count;
  if (need > *cap) {
    size_t next = *cap ? *cap * 2 : 128;
    if (next < need)
      next = need;
    char *grown = realloc(*data, next);
    if (!grown)
      zan_host_oom();
    *data = grown;
    *cap = next;
  }
  if (count)
    memcpy(*data + *len, text, count);
  *len = need;
}

static void refl_attr_text(char **data, size_t *len, size_t *cap,
                           zan_istr_t text) {
  char prefix[16];
  int n = snprintf(prefix, sizeof(prefix), "%u:", (unsigned)text.len);
  refl_attr_append(data, len, cap, prefix, (size_t)n);
  refl_attr_append(data, len, cap, text.str, text.len);
}

/* Preserve the spelling of symbolic names and unions of names. Resolution
 * of those names belongs to the consumer of the attribute. */
static bool refl_attr_symbol(const zan_ast_node_t *expr, bool allow_union,
                             char **text, size_t *len, size_t *cap) {
  if (!expr)
    return false;
  if (expr->kind == AST_IDENTIFIER && expr->ident.name.len) {
    refl_attr_append(text, len, cap, expr->ident.name.str,
                     expr->ident.name.len);
    return true;
  }
  if (expr->kind == AST_MEMBER_ACCESS && !expr->member.null_cond &&
      expr->member.name.len &&
      refl_attr_symbol(expr->member.object, false, text, len, cap)) {
    refl_attr_append(text, len, cap, ".", 1);
    refl_attr_append(text, len, cap, expr->member.name.str,
                     expr->member.name.len);
    return true;
  }
  if (allow_union && expr->kind == AST_BINARY && expr->binary.op == TK_PIPE &&
      refl_attr_symbol(expr->binary.left, true, text, len, cap)) {
    refl_attr_append(text, len, cap, "|", 1);
    return refl_attr_symbol(expr->binary.right, true, text, len, cap);
  }
  return false;
}

static LLVMValueRef refl_declared_attrs(zan_irgen_t *g,
                                        const zan_ast_node_t *decl) {
  const zan_ast_list_t *attrs = zan_ast_attributes_const(decl);
  char *data = NULL;
  size_t len = 0, cap = 0;
  char *symbol = NULL;
  size_t symbol_cap = 0;
  refl_attr_append(&data, &len, &cap, "!1;", 3);
  for (int i = 0; i < attrs->count; i++) {
    const zan_ast_node_t *attr = attrs->items[i];
    if (!attr || attr->kind != AST_ATTRIBUTE || !attr->attribute.name ||
        attr->attribute.name->kind != AST_IDENTIFIER)
      continue;
    zan_istr_t name = attr->attribute.name->ident.name;
    if (!name.len)
      continue;
    refl_attr_append(&data, &len, &cap, "A", 1);
    refl_attr_text(&data, &len, &cap, name);
    for (int j = 0; j < attr->attribute.args.count; j++) {
      const zan_ast_node_t *arg = attr->attribute.args.items[j];
      if (!arg)
        continue;
      zan_istr_t key = {0};
      if (arg->kind == AST_ASSIGNMENT && arg->binary.left &&
          arg->binary.left->kind == AST_IDENTIFIER && arg->binary.right) {
        key = arg->binary.left->ident.name;
        arg = arg->binary.right;
      }
      char kind = 'X';
      char number[64];
      zan_istr_t value = {0};
      switch (arg->kind) {
      case AST_STRING_LITERAL:
        kind = 'S';
        value = arg->str_val;
        break;
      case AST_BOOL_LITERAL:
        kind = 'B';
        value =
            arg->bool_val ? (zan_istr_t){"true", 4} : (zan_istr_t){"false", 5};
        break;
      case AST_INT_LITERAL:
        kind = 'I';
        value = (zan_istr_t){number,
                             (uint32_t)snprintf(number, sizeof(number), "%lld",
                                                (long long)arg->int_val)};
        break;
      case AST_FLOAT_LITERAL:
        kind = 'F';
        value =
            (zan_istr_t){number, (uint32_t)snprintf(number, sizeof(number),
                                                    "%.17g", arg->float_val)};
        break;
      default: {
        size_t symbol_len = 0;
        if (refl_attr_symbol(arg, true, &symbol, &symbol_len, &symbol_cap)) {
          kind = 'E';
          value = (zan_istr_t){symbol, (uint32_t)symbol_len};
        }
        break;
      }
      }
      refl_attr_append(&data, &len, &cap, &kind, 1);
      refl_attr_text(&data, &len, &cap, key);
      refl_attr_text(&data, &len, &cap, value);
      refl_attr_append(&data, &len, &cap, ";", 1);
    }
  }
  LLVMValueRef out = refl_const_string(g, data, (int)len);
  free(symbol);
  free(data);
  return out;
}

static void refl_pack_attrs(zan_ast_list_t *attrs, char *buf, size_t buf_size) {
  if (!attrs || attrs->count == 0 || buf_size <= 1) {
    if (buf_size > 0)
      buf[0] = 0;
    return;
  }
  size_t pos = 0;
  buf[0] = 0;
  for (int i = 0; i < attrs->count; i++) {
    zan_ast_node_t *a = attrs->items[i];
    if (!a || a->kind != AST_ATTRIBUTE || !a->attribute.name)
      continue;
    if (a->attribute.name->kind != AST_IDENTIFIER)
      continue;
    zan_istr_t n = a->attribute.name->ident.name;
    if (!n.str || n.len == 0)
      continue;

    char attr_name[64];
    int nlen = (int)n.len;
    if (nlen > 60)
      nlen = 60;
    memcpy(attr_name, n.str, (size_t)nlen);
    attr_name[nlen] = 0;
    if (nlen > 9 && memcmp(attr_name + nlen - 9, "Attribute", 9) == 0) {
      attr_name[nlen - 9] = 0;
    }

    if (a->attribute.args.count == 0) {
      int written = snprintf(buf + pos, buf_size - pos, "%s=true;", attr_name);
      if (written > 0 && pos + (size_t)written < buf_size)
        pos += (size_t)written;
      continue;
    }

    for (int j = 0; j < a->attribute.args.count; j++) {
      zan_ast_node_t *arg = a->attribute.args.items[j];
      if (!arg)
        continue;
      if (arg->kind == AST_ASSIGNMENT && arg->binary.left &&
          arg->binary.left->kind == AST_IDENTIFIER && arg->binary.right) {
        zan_istr_t kname = arg->binary.left->ident.name;
        char kstr[64];
        int klen = (int)kname.len;
        if (klen > 60)
          klen = 60;
        memcpy(kstr, kname.str, (size_t)klen);
        kstr[klen] = 0;

        char val_str[256];
        refl_pack_val_str(arg->binary.right, val_str, sizeof(val_str));
        int written = snprintf(buf + pos, buf_size - pos, "%s.%s=%s;",
                               attr_name, kstr, val_str);
        if (written > 0 && pos + (size_t)written < buf_size)
          pos += (size_t)written;
      } else {
        char val_str[256];
        refl_pack_val_str(arg, val_str, sizeof(val_str));
        int written =
            snprintf(buf + pos, buf_size - pos, "%s=%s;", attr_name, val_str);
        if (written > 0 && pos + (size_t)written < buf_size)
          pos += (size_t)written;
      }
    }
  }
}

static void refl_append_inherited_attrs(char *dest, size_t dest_cap,
                                        const char *src) {
  if (!src || !*src || dest_cap <= 1)
    return;
  const char *p = src;
  while (*p) {
    const char *semi = strchr(p, ';');
    if (!semi)
      semi = p + strlen(p);
    const char *eq = memchr(p, '=', (size_t)(semi - p));
    if (eq) {
      size_t klen = (size_t)(eq - p + 1);
      char key[128];
      if (klen < sizeof(key)) {
        memcpy(key, p, klen);
        key[klen] = 0;
        if (!strstr(dest, key)) {
          size_t cur_len = strlen(dest);
          size_t token_len = (size_t)(semi - p);
          if (cur_len + token_len + 2 < dest_cap) {
            memcpy(dest + cur_len, p, token_len);
            cur_len += token_len;
            dest[cur_len++] = ';';
            dest[cur_len] = 0;
          }
        }
      }
    }
    p = (*semi == ';') ? semi + 1 : semi;
  }
}

/* B-ID107：短名渲染——与 render_type_full 同构但不注入命名空间前缀，
 * 顶层与泛型实参一律剥 ns（List<Ns.Foo> -> List<Foo>），对齐 C# Name 契约 */
static int render_type_short(zan_type_t *t, char *buf, int cap) {
  if (!t || cap <= 1)
    return 0;
  int n = 0;
  if (t->kind == TYPE_ARRAY && t->element_type) {
    n = render_type_short(t->element_type, buf, cap);
    if (n < cap - 1) {
      buf[n++] = '[';
      for (int r = 1; r < (t->array_rank > 1 ? t->array_rank : 1); r++)
        if (n < cap - 1)
          buf[n++] = ',';
      if (n < cap - 1)
        buf[n++] = ']';
    }
    if (n < cap)
      buf[n] = '\0';
    return n;
  }
  if (t->kind == TYPE_NULLABLE && t->element_type) {
    n = render_type_short(t->element_type, buf, cap);
    if (n < cap - 1)
      buf[n++] = '?';
    if (n < cap)
      buf[n] = '\0';
    return n;
  }
  if (t->name.len) {
    zan_istr_t short_name = t->name;
    if (t->sym && t->sym->decl) {
      zan_istr_t orig = zan_ast_orig_name(t->sym->decl);
      if (orig.len > 0)
        short_name = orig;
    }
    int w = (int)short_name.len;
    if (w > cap - n - 1)
      w = cap - n - 1;
    memcpy(buf + n, short_name.str, (size_t)w);
    n += w;
  } else if (n < cap - 1) {
    buf[n++] = '?';
  }
  if (t->type_arg_count > 0 && n < cap - 2) {
    buf[n++] = '<';
    for (int i = 0; i < t->type_arg_count && n < cap - 2; i++) {
      if (i)
        buf[n++] = ',';
      n += render_type_short(t->type_args[i], buf + n, cap - n);
    }
    if (n < cap - 1)
      buf[n++] = '>';
  }
  if (n < cap)
    buf[n] = '\0';
  return n;
}
LLVMValueRef refl_meta_for(zan_irgen_t *g, zan_type_t *t, const char *disp,
                           int disp_len) {
  char namebuf[256];
  if (!disp || disp_len <= 0) {
    int n = t ? render_type_full(t, namebuf, sizeof(namebuf)) : 0;
    if (n <= 0) {
      namebuf[0] = '?';
      namebuf[1] = 0;
      n = 1;
    }
    disp = namebuf;
    disp_len = n;
  }
  /* B-ID107：blob 名槽存短名（Name），FullName 另存 -112 槽，
   * 去重键维持全名（metas[].name 仍存 disp） */
  char shortbuf[256];
  int sn = render_type_short(t, shortbuf, sizeof(shortbuf));
  if (sn <= 0) {
    if (disp_len > (int)sizeof(shortbuf) - 1)
      disp_len = (int)sizeof(shortbuf) - 1;
    memcpy(shortbuf, disp, (size_t)disp_len);
    shortbuf[disp_len] = 0;
    sn = disp_len;
  }
  zan_symbol_t *sym = t ? t->sym : NULL;
  for (int i = 0; i < g->refl_meta_count; i++) {
    if (g->refl_metas[i].sym == sym &&
        strncmp(g->refl_metas[i].name, disp, (size_t)disp_len) == 0 &&
        g->refl_metas[i].name[disp_len] == 0)
      return g->refl_metas[i].rec;
  }

  LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
  LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
  LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
  LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);

  char tattrbuf[2048];
  tattrbuf[0] = 0;
  if (sym && sym->decl)
    refl_pack_attrs(zan_ast_attributes(sym->decl), tattrbuf, sizeof(tattrbuf));
  for (zan_symbol_t *cur = (sym && sym->type && sym->type->base_type)
                               ? sym->type->base_type->sym
                               : NULL;
       cur;
       cur = (cur->type && cur->type->base_type) ? cur->type->base_type->sym
                                                 : NULL) {
    if (cur->decl) {
      char base_buf[1024];
      base_buf[0] = 0;
      refl_pack_attrs(zan_ast_attributes(cur->decl), base_buf,
                      sizeof(base_buf));
      refl_append_inherited_attrs(tattrbuf, sizeof(tattrbuf), base_buf);
    }
  }
  LLVMValueRef tattr_val =
      refl_const_string(g, tattrbuf, (int)strlen(tattrbuf));

  LLVMValueRef declared_attrs = refl_declared_attrs(g, sym ? sym->decl : NULL);

  LLVMTypeRef rec_ty = LLVMStructTypeInContext(
      g->ctx,
      (LLVMTypeRef[]){i8ptr, /* sfields (-144) */
                      i64,   /* scount (-136) */
                      i8ptr, /* fsort (-128) */
                      i8ptr, /* msort (-120) */
                      i8ptr, /* full_name (-112) B-ID107 */
                      i8ptr, /* type_attrs (-104) */
                      i8ptr, /* declared attributes (-96) */
                      i8ptr, i64, i8ptr, i64, i8ptr, i8ptr, i8ptr, i64, i64,
                      i64, i64, LLVMArrayType(i8, (unsigned)(sn + 1))},
      19, 0);
  char nm[128];
  snprintf(nm, sizeof(nm), "__zan.refl.type.%d", g->refl_meta_count);
  LLVMValueRef gv = LLVMAddGlobal(g->mod, rec_ty, nm);
  LLVMSetInitializer(gv, LLVMConstNull(rec_ty));
  LLVMSetLinkage(gv, LLVMPrivateLinkage);
  LLVMSetGlobalConstant(gv, 1);
  LLVMSetAlignment(gv, 8);
  LLVMValueRef idx[3] = {LLVMConstInt(i64, 0, 0), LLVMConstInt(i32, 18, 0),
                         LLVMConstInt(i64, 0, 0)};
  LLVMValueRef rec = LLVMConstGEP2(rec_ty, gv, idx, 3);

  if (g->refl_meta_count == g->refl_meta_cap) {
    int ncap = g->refl_meta_cap ? g->refl_meta_cap * 2 : 32;
    g->refl_metas = (typeof(g->refl_metas))realloc(
        g->refl_metas, (size_t)ncap * sizeof(*g->refl_metas));
    g->refl_meta_cap = ncap;
  }
  g->refl_metas[g->refl_meta_count].sym = sym;
  g->refl_metas[g->refl_meta_count].type = t;
  g->refl_metas[g->refl_meta_count].name =
      zan_arena_strdup(g->arena, disp, (size_t)disp_len);
  g->refl_metas[g->refl_meta_count].rec = rec;
  g->refl_metas[g->refl_meta_count].gv = gv;
  g->refl_metas[g->refl_meta_count].vops = NULL;
  g->refl_metas[g->refl_meta_count].box_site = -1;
  g->refl_meta_count++;
  g->refl_used = true;

  int field_count = 0, sfield_count = 0;
  LLVMValueRef fields = NULL, sfields = LLVMConstNull(i8ptr);
  LLVMValueRef fsort = LLVMConstNull(i8ptr);
  if (t && sym) {
    if (t->kind == TYPE_ENUM)
      fields = refl_emit_fields_enum(g, sym, &field_count, &fsort);
    else if (t->kind == TYPE_CLASS || t->kind == TYPE_STRUCT) {
      fields = refl_emit_fields_class(g, sym, &field_count, &fsort);
      LLVMValueRef sf =
          refl_emit_static_fields_class(g, sym, t, &sfield_count);
      if (sf)
        sfields = sf;
    }
  }
  if (!fields && refl_is_builtin_list(t)) {
    fields = refl_emit_fields_builtin_list(g, t, &field_count);
  }
  if (!fields)
    fields = LLVMConstNull(i8ptr);

  int method_count = 0, ctor_count = 0;
  LLVMValueRef methods = LLVMConstNull(i8ptr), ctors = LLVMConstNull(i8ptr);
  LLVMValueRef msort = LLVMConstNull(i8ptr);
  if (t && sym &&
      (t->kind == TYPE_CLASS || t->kind == TYPE_STRUCT ||
       t->kind == TYPE_INTERFACE)) {
    methods = refl_shape_mtab(g, sym, t, false, &method_count);
    if (method_count >= 4) {
      zan_symbol_t **msyms =
          (zan_symbol_t **)calloc((size_t)method_count, sizeof(zan_symbol_t *));
      zan_istr_t *mnames =
          (zan_istr_t *)calloc((size_t)method_count, sizeof(zan_istr_t));
      if (msyms && mnames) {
        int got = refl_collect_methods(g, sym, msyms, method_count);
        for (int i = 0; i < got; i++)
          mnames[i] = msyms[i]->name;
        msort = refl_emit_sorted_index(g, mnames, got, "msort");
      }
      free(mnames);
      free(msyms);
    }
    ctors = refl_shape_mtab(g, sym, t, true, &ctor_count);
  } else if (refl_is_builtin_list(t)) {
    methods = refl_emit_methods_builtin_list(g, t, &method_count);
    if (method_count == 4) {
      static const zan_istr_t list_mnames[4] = {
          {"Count", 5}, {"Get", 3}, {"At", 2}, {"Add", 3}};
      msort = refl_emit_sorted_index(g, list_mnames, 4, "msort");
    }
    ctors = refl_emit_ctor_builtin_list(g, t);
    ctor_count = 1;
  }

  LLVMValueRef elem = LLVMConstNull(i8ptr);
  if (t && (t->kind == TYPE_ARRAY || t->kind == TYPE_NULLABLE) &&
      t->element_type)
    elem = refl_meta_for(g, t->element_type, NULL, 0);
  else if (refl_is_builtin_list(t) && t->type_arg_count > 0 && t->type_args)
    elem = refl_meta_for(g, t->type_args[0], NULL, 0);

  LLVMValueRef targs = LLVMConstNull(i8ptr);
  if (t && t->type_arg_count > 0 && t->type_args) {
    int n = t->type_arg_count;
    LLVMValueRef *recs =
        (LLVMValueRef *)calloc((size_t)n + 1, sizeof(LLVMValueRef));
    if (recs) {
      for (int i = 0; i < n; i++)
        recs[i] = t->type_args[i] ? refl_meta_for(g, t->type_args[i], NULL, 0)
                                  : LLVMConstNull(i8ptr);
      recs[n] = LLVMConstNull(i8ptr);
      LLVMTypeRef arr_ty = LLVMArrayType(i8ptr, (unsigned)n + 1);
      char tn[64];
      snprintf(tn, sizeof(tn), "__zan.refl.targs.%d", g->refl_str_count++);
      LLVMValueRef tgv = LLVMAddGlobal(g->mod, arr_ty, tn);
      LLVMSetInitializer(tgv, LLVMConstArray(i8ptr, recs, (unsigned)n + 1));
      LLVMSetLinkage(tgv, LLVMPrivateLinkage);
      LLVMSetGlobalConstant(tgv, 1);
      free(recs);
      targs = LLVMConstBitCast(tgv, i8ptr);
    }
  }

  LLVMValueRef *chars =
      (LLVMValueRef *)calloc((size_t)sn + 1, sizeof(LLVMValueRef));
  if (!chars)
    return rec;
  for (int i = 0; i < sn; i++)
    chars[i] = LLVMConstInt(i8, (unsigned char)shortbuf[i], 0);
  chars[sn] = LLVMConstInt(i8, 0, 0);
  LLVMSetInitializer(
      gv,
      LLVMConstNamedStruct(
          rec_ty,
          (LLVMValueRef[]){
              sfields,
              LLVMConstInt(i64, (unsigned long long)sfield_count, 0),
              fsort, /* fsort (-128) */
              msort, /* msort (-120) */
              refl_const_string(g, disp, disp_len), /* FullName（B-ID107） */
              tattr_val, declared_attrs, methods,
              LLVMConstInt(i64, (unsigned long long)method_count, 0), ctors,
              LLVMConstInt(i64, (unsigned long long)ctor_count, 0), elem, targs,
              fields, LLVMConstInt(i64, (unsigned long long)field_count, 0),
              LLVMConstInt(i64, (unsigned long long)refl_type_kind(t), 0),
              LLVMConstInt(i64, ZAN_STR_LIT_W0(sn), 0),
              LLVMConstInt(i64, ZAN_STR_LIT_W1, 0),
              LLVMConstArray(i8, chars, (unsigned)(sn + 1))},
          19));
  free(chars);
  return rec;
}

/* ctor thunk 的分配点必须在站点→类型名表冻结之前登记（irgen_emit.c
 * 的顺序：refl_reserve_thunk_sites → arc desc/tyname 表 →
 * refl_finalize_mtabs）。登记时 sym 已知，名表把实体类名的继承链
 * 挂到该分配点上，反射 CreateInstance 造出的对象才能通过 is/下转。 */
void refl_reserve_thunk_sites(zan_irgen_t *g) {
  /* emit_site_meta_table snapshots leak_site_count before lazily asking for
   * metadata. A closed List discovered there needs its own ctor site, as do
   * nested generic arguments. Warm the entire reachable site set first.
   * Metadata records enter the cache before visiting children, so recursive
   * class graphs terminate; each synthetic List site is reserved only once. */
  if (g->refl_used) {
    int sites, metas, mtabs;
    int ctor_cursor = 0;
    bool site_meta_before;
    int site_roots_before;
    bool site_all_before;
    do {
      refl_materialize_field_typeinfos(g);
      site_meta_before = refl_needs_site_meta(g);
      site_roots_before = g->refl_site_root_count;
      site_all_before = g->refl_site_all;
      sites = g->leak_site_count;
      metas = g->refl_meta_count;
      mtabs = g->refl_mtab_count;
      if (refl_needs_site_meta(g)) {
        for (int i = 1; i < g->leak_site_count; i++) {
          if (g->site_coll && g->site_coll[i]) {
            if (g->site_coll[i] == 1 && refl_needs_site_meta_for_list(g)) {
              zan_type_t *elem = g->site_coll_elem[i];
              refl_meta_for(g, zan_binder_make_list_type(g->binder, elem), NULL,
                            0);
            }
          } else {
            zan_symbol_t *sym = g->site_syms ? g->site_syms[i] : NULL;
            if (sym && sym->type && refl_needs_site_meta_for_sym(g, sym)) {
              zan_type_t *t =
                  g->site_inst && g->site_inst[i] ? g->site_inst[i] : sym->type;
              refl_meta_for(g, t, NULL, 0);
            }
          }
        }
      }
      for (int i = 0; i < g->refl_meta_count; i++) {
        zan_type_t *mt = g->refl_metas[i].type;
        if (mt && mt->kind == TYPE_STRUCT && irgen_boxed_eligible_struct(g, mt) &&
            g->refl_metas[i].box_site < 0) {
          refl_struct_box_site(g, mt);
        }
      }
      for (int i = 0; i < g->refl_mtab_count; i++) {
        if (!g->refl_mtabs[i].ctors && g->refl_mtabs[i].sym) {
          zan_symbol_t *sym = g->refl_mtabs[i].sym;
          int mc = refl_methods_walk(g, sym, NULL, 0);
          if (mc > 0) {
            zan_symbol_t **msyms =
                (zan_symbol_t **)calloc((size_t)mc, sizeof(zan_symbol_t *));
            if (msyms) {
              int got = refl_collect_methods(g, sym, msyms, mc);
              for (int j = 0; j < got; j++) {
                zan_ast_node_t *mdecl = msyms[j] ? msyms[j]->decl : NULL;
                if (!mdecl || mdecl->kind != AST_METHOD_DECL ||
                    !mdecl->method_decl.return_type ||
                    !refl_method_needs_thunk(g, sym, mdecl))
                  continue;
                zan_type_t *rt = zan_binder_resolve_type(
                    g->binder, mdecl->method_decl.return_type);
                zan_type_t *owner_inst = refl_method_owner_inst(
                    g, sym, g->refl_mtabs[i].inst, msyms[j]);
                if (rt && owner_inst)
                  rt = subst_type_param_deep(g, rt, owner_inst);
                if (rt && type_is_concrete(rt) && !type_mentions_canon(rt) &&
                    (rt->kind == TYPE_STRUCT || rt->kind == TYPE_NULLABLE)) {
                  refl_meta_for(g, rt, NULL, 0);
                }
                if (msyms[j]->parent && is_user_generic_sym(msyms[j]->parent) &&
                    owner_inst && type_is_concrete(owner_inst) &&
                    !type_mentions_canon(owner_inst)) {
                  resolve_gctx_arg_for_inst(g, false, owner_inst->sym,
                                            owner_inst, NULL, 0);
                }
              }
              free(msyms);
            }
          }
        }
        if (g->refl_mtabs[i].ctors && g->refl_mtabs[i].sym &&
            is_user_generic_sym(g->refl_mtabs[i].sym) &&
            g->refl_mtabs[i].inst && type_is_concrete(g->refl_mtabs[i].inst) &&
            !type_mentions_canon(g->refl_mtabs[i].inst)) {
          resolve_gctx_arg_for_inst(g, false, g->refl_mtabs[i].sym,
                                    g->refl_mtabs[i].inst, NULL, 0);
        }
        if (!refl_ctor_needs_thunk(g) || !g->refl_mtabs[i].ctors ||
            g->refl_mtabs[i].site >= 0)
          continue;
        zan_symbol_t *sym = g->refl_mtabs[i].sym;
        if (!sym || !sym->type)
          continue;
        zan_type_t *site_ty =
            g->refl_mtabs[i].inst ? g->refl_mtabs[i].inst : sym->type;
        if (sym->type->kind == TYPE_CLASS) {
          g->refl_mtabs[i].site = reserve_arc_site(g, sym, site_ty, 0, NULL);
        } else if (sym->type->kind == TYPE_STRUCT &&
                   irgen_boxed_eligible_struct(g, site_ty)) {
          g->refl_mtabs[i].site = refl_struct_box_site(g, site_ty);
        }
      }
      /* Implicit class field initializers can allocate further classes,
       * Lists or closures. Emit ctor bodies here, while site tables can
       * still grow, rather than during the post-freeze method fixup. */
      int ctor_end = g->refl_mtab_count;
      while (ctor_cursor < ctor_end) {
        int i = ctor_cursor++;
        if (!refl_ctor_needs_thunk(g) || !g->refl_mtabs[i].ctors ||
            g->refl_mtabs[i].ctor_ready)
          continue;
        g->refl_mtabs[i].ctor_ready = true;
        LLVMValueRef init;
        if (g->refl_mtabs[i].list_type) {
          LLVMValueRef rec = refl_builtin_list_ctor_record(
              g, g->refl_mtabs[i].list_type, true);
          init = LLVMConstArray(refl_method_type(g), &rec, 1);
        } else {
          init = refl_mtab_init(g, g->refl_mtabs[i].sym, g->refl_mtabs[i].inst,
                                true, g->refl_mtabs[i].n, true);
        }
        if (init)
          LLVMSetInitializer(g->refl_mtabs[i].gv, init);
      }
    } while (site_meta_before != refl_needs_site_meta(g) ||
             site_roots_before != g->refl_site_root_count ||
             site_all_before != g->refl_site_all ||
             sites != g->leak_site_count || metas != g->refl_meta_count ||
             mtabs != g->refl_mtab_count);
  }
}
/* Project only after emitted capabilities converge. Keep the ABI, row order,
 * call flags and parameter counts; lookup/invocation do not need type strings.
 */
static void refl_project_member_table(zan_irgen_t *g, LLVMValueRef table,
                                      LLVMTypeRef row_type, unsigned arity,
                                      unsigned omitted_columns) {
  if (!omitted_columns)
    return;
  while (LLVMIsAConstantExpr(table)) {
    LLVMOpcode op = LLVMGetConstOpcode(table);
    if (op != LLVMBitCast && op != LLVMAddrSpaceCast)
      return;
    table = LLVMGetOperand(table, 0);
  }
  if (!LLVMIsAGlobalVariable(table))
    return;
  LLVMValueRef init = LLVMGetInitializer(table);
  LLVMTypeRef type = LLVMGlobalGetValueType(table);
  if (!init || LLVMGetTypeKind(type) != LLVMArrayTypeKind)
    return;
  unsigned n = LLVMGetArrayLength(type);
  LLVMValueRef *rows = (LLVMValueRef *)calloc(n, sizeof(*rows));
  if (!rows)
    return; /* Allocation failure keeps complete metadata. */
  for (unsigned i = 0; i < n; i++) {
    LLVMValueRef row = LLVMGetAggregateElement(init, i);
    if (!row || LLVMIsNull(row)) {
      rows[i] = LLVMConstNull(row_type);
      continue;
    }
    LLVMValueRef values[9];
    for (unsigned j = 0; j < arity; j++) {
      LLVMValueRef value = LLVMGetAggregateElement(row, j);
      values[j] = omitted_columns & (1u << j) ? LLVMConstNull(LLVMTypeOf(value))
                                              : value;
    }
    rows[i] = LLVMConstNamedStruct(row_type, values, arity);
  }
  LLVMSetInitializer(table, LLVMConstArray(row_type, rows, n));
  free(rows);
}

static void refl_project_member_metadata(zan_irgen_t *g) {
  if (!g->publish_reflect_demand || g->emit_lib)
    return;
  unsigned queries = g->refl_query_used;
  unsigned fields = 0, methods = 0, ctors = (1u << 1) | (1u << 7);
  if (!(queries & ZAN_REFL_QUERY_FIELD_TYPE))
    fields |= 1u << 1;
  if (!(queries & ZAN_REFL_QUERY_FIELD_DECLATTRS))
    fields |= 1u << 4;
  if (!g->refl_field_rw_used)
    fields |= 1u << 6;
  if (!(queries & ZAN_REFL_QUERY_FIELD_TYPEINFO))
    fields |= 1u << 7;
  if (!(queries & ZAN_REFL_QUERY_FIELD_ATTRS))
    fields |= 1u << 8;
  if (!(queries & ZAN_REFL_QUERY_METHOD_RETURN))
    methods |= 1u << 1;
  if (!(queries & ZAN_REFL_QUERY_METHOD_PARAMS))
    methods |= 1u << 4;
  if (!(queries & ZAN_REFL_QUERY_METHOD_ATTRS))
    methods |= 1u << 7;
  if (!(queries & ZAN_REFL_QUERY_METHOD_DECLATTRS))
    methods |= 1u << 8;
  if (!(queries & ZAN_REFL_QUERY_CTOR_PARAMS))
    ctors |= 1u << 4;
  for (int i = 0; i < g->refl_meta_count; i++) {
    LLVMValueRef gv = g->refl_metas[i].gv;
    LLVMValueRef init = LLVMGetInitializer(gv);
    if (!init || LLVMIsNull(init))
      continue;
    LLVMValueRef values[19];
    for (unsigned j = 0; j < 19; j++)
      values[j] = LLVMGetAggregateElement(init, j);
    if (!g->refl_sfield_used) {
      values[0] = LLVMConstNull(LLVMTypeOf(values[0]));
      values[1] = LLVMConstNull(LLVMTypeOf(values[1]));
    } else {
      refl_project_member_table(g, values[0], refl_field_type(g), 9,
                                fields & ~(1u << 6));
    }
    refl_project_member_table(g, values[13], refl_field_type(g), 9, fields);
    refl_project_member_table(g, values[7], refl_method_type(g), 9, methods);
    if (!g->refl_ctor_used && !(queries & ZAN_REFL_QUERY_CTORS)) {
      values[9] = LLVMConstNull(LLVMTypeOf(values[9]));
      values[10] = LLVMConstNull(LLVMTypeOf(values[10]));
    } else {
      refl_project_member_table(g, values[9], refl_method_type(g), 9, ctors);
    }
    LLVMSetInitializer(
        gv, LLVMConstNamedStruct(LLVMGlobalGetValueType(gv), values, 19));
  }
}

void refl_finalize_mtabs(zan_irgen_t *g) {
  for (int i = 0; i < g->refl_mtab_count; i++) {
    if (g->refl_mtabs[i].ctors)
      continue;
    LLVMValueRef init =
        refl_mtab_init(g, g->refl_mtabs[i].sym, g->refl_mtabs[i].inst,
                       g->refl_mtabs[i].ctors, g->refl_mtabs[i].n, true);
    if (init)
      LLVMSetInitializer(g->refl_mtabs[i].gv, init);
  }
  refl_project_member_metadata(g);
}

static LLVMValueRef refl_hdr_i64(zan_irgen_t *g, LLVMValueRef ti, int off,
                                 const char *name) {
  LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
  LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
  LLVMValueRef d = LLVMConstInt(i64, (unsigned long long)(long long)off, 1);
  LLVMValueRef p = LLVMBuildGEP2(g->builder, i8, ti, &d, 1, "refl.hp");
  p = LLVMBuildBitCast(g->builder, p, LLVMPointerType(i64, 0), "refl.hpc");
  return LLVMBuildLoad2(g->builder, i64, p, name);
}
