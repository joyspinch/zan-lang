LLVMValueRef refl_meta_for(zan_irgen_t *g, zan_type_t *t, const char *disp,
                           int disp_len);
static LLVMValueRef refl_get_vops_fn(zan_irgen_t *g, zan_type_t *t);
static int refl_struct_box_site(zan_irgen_t *g, zan_type_t *st);
static LLVMValueRef refl_emit_box_struct_val(zan_irgen_t *g, zan_type_t *st,
                                             LLVMValueRef sval,
                                             bool already_owned);
static LLVMValueRef refl_decimal_to_i64(zan_irgen_t *g, LLVMValueRef val);
static LLVMValueRef refl_decimal_to_f64(zan_irgen_t *g, LLVMValueRef val);
static LLVMValueRef refl_decimal_from_i64(zan_irgen_t *g, LLVMValueRef val);
static LLVMValueRef refl_decimal_from_f64(zan_irgen_t *g, LLVMValueRef val);

static LLVMTypeRef refl_field_type(zan_irgen_t *g) {
  if (!g->refl_field_type) {
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef elems[9] = {i8ptr, i8ptr, i64,   i64,  i8ptr,
                            i64,   i8ptr, i8ptr, i8ptr};
    g->refl_field_type = LLVMStructTypeInContext(g->ctx, elems, 9, 0);
  }
  return g->refl_field_type;
}

static LLVMValueRef refl_const_string(zan_irgen_t *g, const char *s, int len) {
  LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
  LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
  LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
  if (len < 0)
    len = 0;
  LLVMTypeRef rec_ty = LLVMStructTypeInContext(
      g->ctx, (LLVMTypeRef[]){i64, i64, LLVMArrayType(i8, (unsigned)(len + 1))},
      3, 0);
  LLVMValueRef *chars =
      (LLVMValueRef *)calloc((size_t)len + 1, sizeof(LLVMValueRef));
  if (!chars)
    return LLVMConstNull(LLVMPointerType(i8, 0));
  for (int i = 0; i < len; i++)
    chars[i] = LLVMConstInt(i8, (unsigned char)s[i], 0);
  chars[len] = LLVMConstInt(i8, 0, 0);
  LLVMValueRef init = LLVMConstNamedStruct(
      rec_ty,
      (LLVMValueRef[]){LLVMConstInt(i64, ZAN_STRING_SENTINEL_RC, 0),
                       LLVMConstInt(i64, ZAN_STR_HDR_WORD(len), 0),
                       LLVMConstArray(i8, chars, (unsigned)(len + 1))},
      3);
  free(chars);
  uint64_t hash = UINT64_C(14695981039346656037);
  for (int i = 0; i < len; i++) {
    hash ^= (unsigned char)s[i];
    hash *= UINT64_C(1099511628211);
  }
  char nm[96];
  LLVMValueRef gv;
  unsigned collision = 0;
  for (;;) {
    snprintf(nm, sizeof(nm), "__zan.refl.str.pool.%016llx.%d.%u",
             (unsigned long long)hash, len, collision);
    gv = LLVMGetNamedGlobal(g->mod, nm);
    if (!gv) {
      gv = LLVMAddGlobal(g->mod, rec_ty, nm);
      LLVMSetInitializer(gv, init);
      LLVMSetLinkage(gv, LLVMPrivateLinkage);
      LLVMSetGlobalConstant(gv, 1);
      LLVMSetUnnamedAddress(gv, LLVMGlobalUnnamedAddr);
      LLVMSetAlignment(gv, 8);
      break;
    }
    /* LLVM uniques constants in this context: equality checks the complete
     * length/header/payload initializer, not just the hash or C string. */
    if (LLVMGlobalGetValueType(gv) == rec_ty && LLVMIsGlobalConstant(gv) &&
        LLVMGetInitializer(gv) == init)
      break;
    if (collision == UINT_MAX) {
      zan_loc_t loc = {0};
      zan_diag_emit_code(g->diag, DIAG_ERROR, loc, 3101,
                         "reflection string constant pool exhausted");
      return LLVMConstNull(LLVMPointerType(i8, 0));
    }
    collision++;
  }
  LLVMValueRef idx[3] = {LLVMConstInt(i64, 0, 0), LLVMConstInt(i32, 2, 0),
                         LLVMConstInt(i64, 0, 0)};
  return LLVMConstGEP2(rec_ty, gv, idx, 3);
}

static int refl_field_kind(zan_type_t *t) {
  if (!t)
    return ZAN_REFL_FK_OTHER;
  switch (t->kind) {
  case TYPE_BOOL:
    return ZAN_REFL_FK_BOOL;
  case TYPE_SBYTE:
    return ZAN_REFL_FK_SBYTE;
  case TYPE_BYTE:
    return ZAN_REFL_FK_BYTE;
  case TYPE_SHORT:
    return ZAN_REFL_FK_SHORT;
  case TYPE_USHORT:
    return ZAN_REFL_FK_USHORT;
  case TYPE_INT:
    return ZAN_REFL_FK_INT;
  case TYPE_UINT:
    return ZAN_REFL_FK_UINT;
  case TYPE_LONG:
  case TYPE_NINT:
    return ZAN_REFL_FK_LONG;
  case TYPE_ULONG:
    return ZAN_REFL_FK_ULONG;
  case TYPE_CHAR:
    return ZAN_REFL_FK_CHAR;
  case TYPE_ENUM:
    return ZAN_REFL_FK_ENUM;
  case TYPE_FLOAT:
    return ZAN_REFL_FK_FLOAT;
  case TYPE_DOUBLE:
    return ZAN_REFL_FK_DOUBLE;
  case TYPE_DECIMAL:
    return ZAN_REFL_FK_DECIMAL;
  case TYPE_STRUCT:
    return ZAN_REFL_FK_STRUCT;
  case TYPE_NULLABLE:
    return ZAN_REFL_FK_NULLABLE;
  case TYPE_STRING:
    return ZAN_REFL_FK_STRING;
  case TYPE_OBJECT:
  case TYPE_CLASS:
  case TYPE_INTERFACE:
  case TYPE_ARRAY:
  case TYPE_DELEGATE:
    return ZAN_REFL_FK_REF;
  default:
    return ZAN_REFL_FK_OTHER;
  }
}

static int refl_type_kind(zan_type_t *t) {
  if (!t)
    return ZAN_REFL_TK_OTHER;
  switch (t->kind) {
  case TYPE_CLASS:
    return ZAN_REFL_TK_CLASS;
  case TYPE_STRUCT:
    return ZAN_REFL_TK_STRUCT;
  case TYPE_ENUM:
    return ZAN_REFL_TK_ENUM;
  case TYPE_INTERFACE:
    return ZAN_REFL_TK_INTERFACE;
  case TYPE_ARRAY:
    return ZAN_REFL_TK_ARRAY;
  case TYPE_NULLABLE:
    return ZAN_REFL_TK_NULLABLE;
  case TYPE_DELEGATE:
    return ZAN_REFL_TK_DELEGATE;
  case TYPE_TASK:
    return ZAN_REFL_TK_OTHER;
  default:
    return ZAN_REFL_TK_PRIMITIVE;
  }
}

static LLVMValueRef refl_field_type_name(zan_irgen_t *g, zan_type_t *t) {
  char buf[160];
  int n = t ? render_type_full(t, buf, sizeof(buf)) : 0;
  if (n <= 0) {
    buf[0] = '?';
    buf[1] = 0;
    n = 1;
  }
  return refl_const_string(g, buf, n);
}

static struct zan_struct_type_entry *refl_struct_entry(zan_irgen_t *g,
                                                       zan_symbol_t *sym) {
  for (int i = 0; i < g->struct_type_count; i++)
    if (g->struct_types[i].sym == sym)
      return &g->struct_types[i];
  return NULL;
}

static LLVMValueRef refl_slot_offset(zan_irgen_t *g, LLVMTypeRef st, int slot) {
  LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
  LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
  LLVMValueRef idx[2] = {LLVMConstInt(i64, 0, 0),
                         LLVMConstInt(i32, (unsigned long long)slot, 0)};
  LLVMValueRef p =
      LLVMConstGEP2(st, LLVMConstNull(LLVMPointerType(st, 0)), idx, 2);
  return LLVMConstPtrToInt(p, i64);
}

static void refl_pack_attrs(zan_ast_list_t *attrs, char *buf, size_t buf_size);
static LLVMValueRef refl_empty_string(zan_irgen_t *g);

static LLVMValueRef refl_declared_attrs(zan_irgen_t *g,
                                        const zan_ast_node_t *decl);

typedef struct {
  const char *str;
  size_t len;
  int idx;
} refl_sort_ent_t;

static int refl_sort_ent_cmp(const void *pa, const void *pb) {
  const refl_sort_ent_t *a = (const refl_sort_ent_t *)pa;
  const refl_sort_ent_t *b = (const refl_sort_ent_t *)pb;
  size_t minlen = a->len < b->len ? a->len : b->len;
  for (size_t i = 0; i < minlen; i++) {
    unsigned char ca = (unsigned char)a->str[i];
    unsigned char cb = (unsigned char)b->str[i];
    if (ca != cb)
      return ca < cb ? -1 : 1;
  }
  if (a->len != b->len)
    return a->len < b->len ? -1 : 1;
  return a->idx < b->idx ? -1 : (a->idx > b->idx ? 1 : 0);
}

static LLVMValueRef refl_emit_sorted_index(zan_irgen_t *g,
                                           const zan_istr_t *names, int count,
                                           const char *tag) {
  LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
  if (count < 4 || !names)
    return LLVMConstNull(i8ptr);
  refl_sort_ent_t *ents =
      (refl_sort_ent_t *)calloc((size_t)count, sizeof(refl_sort_ent_t));
  if (!ents)
    return LLVMConstNull(i8ptr);
  for (int i = 0; i < count; i++) {
    ents[i].str = names[i].str ? names[i].str : "";
    ents[i].len = names[i].str ? names[i].len : 0;
    ents[i].idx = i;
  }
  qsort(ents, (size_t)count, sizeof(refl_sort_ent_t), refl_sort_ent_cmp);
  LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
  LLVMValueRef *vals = (LLVMValueRef *)calloc((size_t)count, sizeof(LLVMValueRef));
  if (!vals) {
    free(ents);
    return LLVMConstNull(i8ptr);
  }
  for (int i = 0; i < count; i++)
    vals[i] = LLVMConstInt(i32, (unsigned long long)(unsigned)ents[i].idx, 0);
  LLVMTypeRef arr_ty = LLVMArrayType(i32, (unsigned)count);
  char nm[64];
  snprintf(nm, sizeof(nm), "__zan.refl.%s.%d", tag, g->refl_meta_count);
  LLVMValueRef gv = LLVMAddGlobal(g->mod, arr_ty, nm);
  LLVMSetInitializer(gv, LLVMConstArray(i32, vals, (unsigned)count));
  LLVMSetLinkage(gv, LLVMPrivateLinkage);
  LLVMSetGlobalConstant(gv, 1);
  LLVMSetUnnamedAddr(gv, LLVMGlobalUnnamedAddr);
  free(vals);
  free(ents);
  return LLVMConstBitCast(gv, i8ptr);
}

/* 发射the field-record array for a class/struct, or NULL when it has none */
static LLVMValueRef refl_emit_fields_class(zan_irgen_t *g, zan_symbol_t *sym,
                                           int *out_count,
                                           LLVMValueRef *out_sort) {
  *out_count = 0;
  if (out_sort)
    *out_sort = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0)
                    ? LLVMConstNull(
                          LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0))
                    : NULL;
  if (!sym)
    return NULL;
  LLVMTypeRef st = get_struct_llvm_type(g, sym);
  if (!st)
    return NULL;
  struct zan_struct_type_entry *entry = refl_struct_entry(g, sym);
  LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
  LLVMTypeRef frec = refl_field_type(g);

  int cap = sym->member_count > 0 ? sym->member_count : 1;
  LLVMValueRef *recs =
      (LLVMValueRef *)calloc((size_t)cap, sizeof(LLVMValueRef));
  zan_istr_t *fnames = (zan_istr_t *)calloc((size_t)cap, sizeof(zan_istr_t));
  if (!recs || !fnames) {
    free(recs);
    free(fnames);
    return NULL;
  }
  int n = 0;
  int slot = class_vptr_offset(sym);
  /* shadow 语义（B-ID147 绑定对账踩实）：派生类声明与继承同名的字段
   * 时，反射只认最派生那个——先扫一遍名字，被更派生声明遮蔽的基类
   * 条目不进表（否则 linear find 先命中基类槽位，子类字段读成基类值）。 */
  char *shadowed = (char *)calloc(
      (size_t)(sym->member_count > 0 ? sym->member_count : 1), 1);
  if (shadowed) {
    for (int i = 0; i < sym->member_count; i++) {
      zan_symbol_t *m = sym->members[i];
      if ((m->kind != SYM_FIELD && m->kind != SYM_PROPERTY) ||
          field_member_is_static(m))
        continue;
      for (int j = i + 1; j < sym->member_count && !shadowed[i]; j++) {
        zan_symbol_t *w = sym->members[j];
        if ((w->kind != SYM_FIELD && w->kind != SYM_PROPERTY) ||
            field_member_is_static(w))
          continue;
        if (w->name.len == m->name.len &&
            memcmp(w->name.str, m->name.str, (size_t)m->name.len) == 0)
          shadowed[i] = 1;
      }
    }
  }
  for (int i = 0; i < sym->member_count; i++) {
    zan_symbol_t *m = sym->members[i];
    if ((m->kind != SYM_FIELD && m->kind != SYM_PROPERTY) ||
        field_member_is_static(m))
      continue;
    if (shadowed && shadowed[i]) {
      slot++;
      continue;
    }
    /* Field reflection has the same visibility contract in every build
     * mode, including default-public and explicitly private fields. */
    LLVMValueRef off;
    if (entry && entry->explicit_layout && entry->field_offsets &&
        slot < entry->field_count)
      off = LLVMConstInt(i64, entry->field_offsets[slot], 0);
    else
      off = refl_slot_offset(g, st, slot);

    char fattrbuf[512];
    fattrbuf[0] = 0;
    if (m->decl)
      refl_pack_attrs(zan_ast_attributes(m->decl), fattrbuf, sizeof(fattrbuf));

    unsigned fflags =
        (m->kind == SYM_PROPERTY) ? (unsigned)ZAN_REFL_FF_PROPERTY : 0u;
    LLVMValueRef vops = refl_get_vops_fn(g, m->type);
    if (!vops)
      vops = LLVMConstNull(LLVMTypeOf(refl_empty_string(g)));
    fnames[n] = m->name;
    recs[n++] = LLVMConstNamedStruct(
        frec,
        (LLVMValueRef[]){
            refl_const_string(g, m->name.str, (int)m->name.len),
            refl_field_type_name(g, m->type),
            LLVMConstInt(i64, (unsigned long long)refl_field_kind(m->type), 0),
            off, refl_declared_attrs(g, m->decl),
            LLVMConstInt(i64, (unsigned long long)fflags, 0), vops,
            refl_meta_for(g, m->type, NULL, 0),
            refl_const_string(g, fattrbuf, (int)strlen(fattrbuf))},
        9);
    slot++;
  }
  LLVMValueRef arr = NULL;
  if (n > 0) {
    LLVMTypeRef arr_ty = LLVMArrayType(frec, (unsigned)n);
    char nm[96];
    snprintf(nm, sizeof(nm), "__zan.refl.fields.%.*s.%d", (int)sym->name.len,
             sym->name.str, g->refl_meta_count);
    LLVMValueRef gv = LLVMAddGlobal(g->mod, arr_ty, nm);
    LLVMSetInitializer(gv, LLVMConstArray(frec, recs, (unsigned)n));
    LLVMSetLinkage(gv, LLVMPrivateLinkage);
    LLVMSetGlobalConstant(gv, 1);
    arr =
        LLVMConstBitCast(gv, LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0));
    *out_count = n;
    if (out_sort)
      *out_sort = refl_emit_sorted_index(g, fnames, n, "fsort");
  }
  free(fnames);
  free(recs);
  free(shadowed);
  return arr;
}

/* Static fields and static properties live in a separate metadata table so
 * FieldCount / fields[] remain strictly instance/enum members for ORM/binders. */
static LLVMValueRef refl_emit_static_fields_class(zan_irgen_t *g,
                                                  zan_symbol_t *sym,
                                                  zan_type_t *inst,
                                                  int *out_count) {
  *out_count = 0;
  if (!sym)
    return NULL;
  LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
  LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
  LLVMTypeRef frec = refl_field_type(g);

  int cap = sym->member_count > 0 ? sym->member_count : 1;
  LLVMValueRef *recs =
      (LLVMValueRef *)calloc((size_t)cap, sizeof(LLVMValueRef));
  char *shadowed = (char *)calloc((size_t)cap, 1);
  if (!recs || !shadowed) {
    free(recs);
    free(shadowed);
    return NULL;
  }
  for (int i = 0; i < sym->member_count; i++) {
    zan_symbol_t *m = sym->members[i];
    if ((m->kind != SYM_FIELD && m->kind != SYM_PROPERTY) ||
        !field_member_is_static(m))
      continue;
    for (int j = i + 1; j < sym->member_count && !shadowed[i]; j++) {
      zan_symbol_t *w = sym->members[j];
      if ((w->kind != SYM_FIELD && w->kind != SYM_PROPERTY) ||
          !field_member_is_static(w))
        continue;
      if (w->name.len == m->name.len &&
          memcmp(w->name.str, m->name.str, (size_t)m->name.len) == 0)
        shadowed[i] = 1;
    }
  }
  int n = 0;
  bool can_static_global = !is_user_generic_sym(sym) ||
                           (inst && type_is_concrete(inst) &&
                            !type_mentions_canon(inst));
  for (int i = 0; i < sym->member_count; i++) {
    zan_symbol_t *m = sym->members[i];
    if ((m->kind != SYM_FIELD && m->kind != SYM_PROPERTY) ||
        !field_member_is_static(m) || shadowed[i])
      continue;
    LLVMValueRef gptr = LLVMConstNull(i8ptr);
    if (m->kind == SYM_FIELD && can_static_global) {
      zan_builder_point_t pt = zan_builder_save(g);
      bool saved_canon = g->cur_canon_body;
      g->cur_canon_body = false;
      LLVMClearInsertionPosition(g->builder);
      LLVMValueRef gv = get_static_field_global(g, sym, m, inst);
      g->cur_canon_body = saved_canon;
      zan_builder_restore(g, pt);
      if (gv)
        gptr = LLVMConstBitCast(gv, i8ptr);
    }
    char fattrbuf[512];
    fattrbuf[0] = 0;
    if (m->decl)
      refl_pack_attrs(zan_ast_attributes(m->decl), fattrbuf, sizeof(fattrbuf));
    unsigned fflags = (unsigned)ZAN_REFL_FF_STATIC |
                      ((m->kind == SYM_PROPERTY) ? (unsigned)ZAN_REFL_FF_PROPERTY
                                                 : 0u);
    LLVMValueRef vops = refl_get_vops_fn(g, m->type);
    LLVMValueRef off_val =
        vops ? LLVMConstPtrToInt(vops, i64) : LLVMConstInt(i64, 0, 0);
    recs[n++] = LLVMConstNamedStruct(
        frec,
        (LLVMValueRef[]){
            refl_const_string(g, m->name.str, (int)m->name.len),
            refl_field_type_name(g, m->type),
            LLVMConstInt(i64, (unsigned long long)refl_field_kind(m->type), 0),
            off_val, refl_declared_attrs(g, m->decl),
            LLVMConstInt(i64, (unsigned long long)fflags, 0), gptr,
            refl_meta_for(g, m->type, NULL, 0),
            refl_const_string(g, fattrbuf, (int)strlen(fattrbuf))},
        9);
  }
  LLVMValueRef arr = NULL;
  if (n > 0) {
    LLVMTypeRef arr_ty = LLVMArrayType(frec, (unsigned)n);
    char nm[96];
    snprintf(nm, sizeof(nm), "__zan.refl.sfields.%.*s.%d", (int)sym->name.len,
             sym->name.str, g->refl_meta_count);
    LLVMValueRef gv = LLVMAddGlobal(g->mod, arr_ty, nm);
    LLVMSetInitializer(gv, LLVMConstArray(frec, recs, (unsigned)n));
    LLVMSetLinkage(gv, LLVMPrivateLinkage);
    LLVMSetGlobalConstant(gv, 1);
    arr = LLVMConstBitCast(gv, i8ptr);
    *out_count = n;
  }
  free(recs);
  free(shadowed);
  return arr;
}

static LLVMValueRef refl_emit_fields_enum(zan_irgen_t *g, zan_symbol_t *sym,
                                          int *out_count,
                                          LLVMValueRef *out_sort) {
  *out_count = 0;
  if (out_sort)
    *out_sort = LLVMConstNull(LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0));
  if (!sym)
    return NULL;
  LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
  LLVMTypeRef frec = refl_field_type(g);
  int cap = sym->member_count > 0 ? sym->member_count : 1;
  LLVMValueRef *recs =
      (LLVMValueRef *)calloc((size_t)cap, sizeof(LLVMValueRef));
  zan_istr_t *fnames = (zan_istr_t *)calloc((size_t)cap, sizeof(zan_istr_t));
  if (!recs || !fnames) {
    free(recs);
    free(fnames);
    return NULL;
  }
  int n = 0;
  long long val = 0;
  for (int i = 0; i < sym->member_count; i++) {
    zan_symbol_t *m = sym->members[i];
    if (m->kind != SYM_ENUM_MEMBER)
      continue;
    if (m->decl && m->decl->kind == AST_ENUM_MEMBER &&
        m->decl->enum_member.value &&
        m->decl->enum_member.value->kind == AST_INT_LITERAL)
      val = m->decl->enum_member.value->int_val;
    fnames[n] = m->name;
    recs[n++] = LLVMConstNamedStruct(
        frec,
        (LLVMValueRef[]){
            refl_const_string(g, m->name.str, (int)m->name.len),
            refl_const_string(g, sym->name.str, (int)sym->name.len),
            LLVMConstInt(i64, ZAN_REFL_FK_ENUM_MEMBER, 0),
            LLVMConstInt(i64, (unsigned long long)val, 0),
            refl_declared_attrs(g, m->decl), LLVMConstInt(i64, 0, 0),
            refl_const_string(g, "", 0), refl_meta_for(g, sym->type, NULL, 0),
            refl_const_string(g, "", 0)},
        9);
    val++;
  }
  LLVMValueRef arr = NULL;
  if (n > 0) {
    LLVMTypeRef arr_ty = LLVMArrayType(frec, (unsigned)n);
    char nm[96];
    snprintf(nm, sizeof(nm), "__zan.refl.enum.%.*s.%d", (int)sym->name.len,
             sym->name.str, g->refl_meta_count);
    LLVMValueRef gv = LLVMAddGlobal(g->mod, arr_ty, nm);
    LLVMSetInitializer(gv, LLVMConstArray(frec, recs, (unsigned)n));
    LLVMSetLinkage(gv, LLVMPrivateLinkage);
    LLVMSetGlobalConstant(gv, 1);
    arr =
        LLVMConstBitCast(gv, LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0));
    *out_count = n;
    if (out_sort)
      *out_sort = refl_emit_sorted_index(g, fnames, n, "fsort");
  }
  free(fnames);
  free(recs);
  return arr;
}

static inline bool refl_is_builtin_list(zan_type_t *t) {
  return t && t->kind == TYPE_CLASS && t->name.len == 4 &&
         memcmp(t->name.str, "List", 4) == 0;
}

static LLVMValueRef refl_emit_fields_builtin_list(zan_irgen_t *g, zan_type_t *t,
                                                  int *out_count) {
  *out_count = 0;
  if (!t)
    return NULL;
  LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
  LLVMTypeRef frec = refl_field_type(g);

  LLVMValueRef recs[2];
  recs[0] = LLVMConstNamedStruct(
      frec,
      (LLVMValueRef[]){
          refl_const_string(g, "Count", 5),
          refl_field_type_name(g, g->binder->type_int),
          LLVMConstInt(
              i64, (unsigned long long)refl_field_kind(g->binder->type_int), 0),
          LLVMConstInt(i64, 0, 0), /* offset 0 in list struct */
          refl_const_string(g, "", 0), LLVMConstInt(i64, 0, 0),
          refl_const_string(g, "", 0),
          refl_meta_for(g, g->binder->type_int, NULL, 0),
          refl_const_string(g, "", 0)},
      9);
  recs[1] = LLVMConstNamedStruct(
      frec,
      (LLVMValueRef[]){
          refl_const_string(g, "count", 5),
          refl_field_type_name(g, g->binder->type_int),
          LLVMConstInt(
              i64, (unsigned long long)refl_field_kind(g->binder->type_int), 0),
          LLVMConstInt(i64, 0, 0), /* offset 0 in list struct */
          refl_const_string(g, "", 0), LLVMConstInt(i64, 0, 0),
          refl_const_string(g, "", 0),
          refl_meta_for(g, g->binder->type_int, NULL, 0),
          refl_const_string(g, "", 0)},
      9);

  LLVMTypeRef arr_ty = LLVMArrayType(frec, 2);
  char nm[96];
  snprintf(nm, sizeof(nm), "__zan.refl.fields.List.%d", g->refl_meta_count);
  LLVMValueRef gv = LLVMAddGlobal(g->mod, arr_ty, nm);
  LLVMSetInitializer(gv, LLVMConstArray(frec, recs, 2));
  LLVMSetLinkage(gv, LLVMPrivateLinkage);
  LLVMSetGlobalConstant(gv, 1);
  *out_count = 2;
  return LLVMConstBitCast(gv,
                          LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0));
}

static LLVMTypeRef refl_method_type(zan_irgen_t *g) {
  if (!g->refl_method_type) {
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef elems[] = {i8ptr, i8ptr, i64,   i64,  i8ptr,
                           i8ptr, i64,   i8ptr, i8ptr};
    g->refl_method_type = LLVMStructTypeInContext(g->ctx, elems, 9, 0);
  }
  return g->refl_method_type;
}

static int refl_accessor_flag(zan_symbol_t *sym, zan_istr_t name) {
  if (!sym || name.len <= 4)
    return 0;
  bool is_get = memcmp(name.str, "get_", 4) == 0;
  bool is_set = memcmp(name.str, "set_", 4) == 0;
  if (!is_get && !is_set)
    return 0;
  const char *pn = name.str + 4;
  int plen = (int)name.len - 4;
  for (int i = 0; i < sym->member_count; i++) {
    zan_symbol_t *p = sym->members[i];
    if (p && p->kind == SYM_PROPERTY && (int)p->name.len == plen &&
        memcmp(p->name.str, pn, (size_t)plen) == 0)
      return is_get ? ZAN_REFL_MF_PROPGET : ZAN_REFL_MF_PROPSET;
  }
  return 0;
}

/* 框架按名反射调用的生命周期钩子（Form / ChildWindow 的 OnLoad 等）。
 * 签名保持反射可见，publish 调用桩再按实际发射的 Invoke 名字需求保留。 */
static bool refl_is_framework_hook_name(const char *s, size_t n) {
  if (!s || n < 3)
    return false;
  /* 显式常用生命周期与事件钩子 */
  if (n == 6 && memcmp(s, "OnLoad", 6) == 0)
    return true;
  if (n == 6 && memcmp(s, "OnInit", 6) == 0)
    return true;
  if (n == 6 && memcmp(s, "OnShow", 6) == 0)
    return true;
  if (n == 6 && memcmp(s, "OnHide", 6) == 0)
    return true;
  if (n == 7 && memcmp(s, "OnClose", 7) == 0)
    return true;
  if (n == 9 && memcmp(s, "OnClosing", 9) == 0)
    return true;
  if (n == 8 && memcmp(s, "OnClosed", 8) == 0)
    return true;
  if (n == 9 && memcmp(s, "OnDestroy", 9) == 0)
    return true;
  if (n == 8 && memcmp(s, "OnResize", 8) == 0)
    return true;
  if (n == 7 && memcmp(s, "OnPaint", 7) == 0)
    return true;
  if (n == 8 && memcmp(s, "OnUpdate", 8) == 0)
    return true;
  /* 符合 OnPascalCase 的方法（如 OnCustomHook）属于可调用钩子候选。 */
  if (s[0] == 'O' && s[1] == 'n' && (s[2] >= 'A' && s[2] <= 'Z'))
    return true;
  return false;
}

bool refl_ctor_needs_thunk(zan_irgen_t *g) {
  /* Capability-wide fallback: a live CreateInstance may receive any TypeInfo,
   * including dynamic GetType(), fields, ElementType or generic arguments.
   * Library exports remain conservative for callers outside this module. */
  return !g->publish_reflect_demand || g->emit_lib || g->refl_ctor_used;
}

/* Incremental instance index for refl_owner_can_have_instance. The predicate
 * is asked for every method of every reflected class on every emission
 * fixed-point round; rescanning all allocation sites and all class base chains
 * per question was quadratic and took 3.6 s of a small Gui test's 8 s irgen
 * once plain builds adopted closed-world reflection. Sites and struct types
 * only ever grow, so fold each new one in once. */
typedef struct {
  zan_symbol_t *sym;
  bool has_site; /* some allocation site is this class or derives from it */
  bool vt_store; /* a derived vtable was seen stored into an object */
  int *derived;  /* struct_types indices whose base chain includes sym */
  int nderived, cap;
} refl_inst_entry_t;

struct refl_inst_index {
  refl_inst_entry_t *slots;
  int cap, count;
  int sites_seen, structs_seen;
  bool deep_site; /* a site's base chain exceeded the walk limit */
};

static refl_inst_entry_t *refl_inst_entry(refl_inst_index_t *ix,
                                          zan_symbol_t *sym) {
  if (ix->count * 2 >= ix->cap) {
    int ncap = ix->cap ? ix->cap * 2 : 256;
    refl_inst_entry_t *ns =
        (refl_inst_entry_t *)calloc((size_t)ncap, sizeof(*ns));
    if (!ns)
      return NULL;
    for (int i = 0; i < ix->cap; i++) {
      if (!ix->slots[i].sym)
        continue;
      size_t h = ((uintptr_t)ix->slots[i].sym >> 4) & (size_t)(ncap - 1);
      while (ns[h].sym)
        h = (h + 1) & (size_t)(ncap - 1);
      ns[h] = ix->slots[i];
    }
    free(ix->slots);
    ix->slots = ns;
    ix->cap = ncap;
  }
  size_t h = ((uintptr_t)sym >> 4) & (size_t)(ix->cap - 1);
  while (ix->slots[h].sym && ix->slots[h].sym != sym)
    h = (h + 1) & (size_t)(ix->cap - 1);
  if (!ix->slots[h].sym) {
    ix->slots[h].sym = sym;
    ix->count++;
  }
  return &ix->slots[h];
}

static refl_inst_entry_t *refl_inst_find(refl_inst_index_t *ix,
                                         zan_symbol_t *sym) {
  if (!ix->cap)
    return NULL;
  size_t h = ((uintptr_t)sym >> 4) & (size_t)(ix->cap - 1);
  while (ix->slots[h].sym) {
    if (ix->slots[h].sym == sym)
      return &ix->slots[h];
    h = (h + 1) & (size_t)(ix->cap - 1);
  }
  return NULL;
}

void refl_inst_index_free(zan_irgen_t *g) {
  refl_inst_index_t *ix = g->refl_inst_index;
  if (!ix)
    return;
  for (int i = 0; i < ix->cap; i++)
    free(ix->slots[i].derived);
  free(ix->slots);
  free(ix);
  g->refl_inst_index = NULL;
}

/* Returns false on allocation failure (callers then answer conservatively). */
static bool refl_inst_index_update(zan_irgen_t *g) {
  refl_inst_index_t *ix = g->refl_inst_index;
  if (!ix) {
    ix = (refl_inst_index_t *)calloc(1, sizeof(*ix));
    if (!ix)
      return false;
    ix->sites_seen = 1; /* site 0 is the sentinel */
    g->refl_inst_index = ix;
  }
  for (; ix->sites_seen < g->leak_site_count; ix->sites_seen++) {
    zan_symbol_t *sym = g->site_syms ? g->site_syms[ix->sites_seen] : NULL;
    zan_type_t *type = sym ? sym->type : NULL;
    int depth = 0;
    for (; type && type->sym && depth < 32; type = type->base_type, depth++) {
      refl_inst_entry_t *e = refl_inst_entry(ix, type->sym);
      if (!e)
        return false;
      e->has_site = true;
    }
    if (depth == 32 && type)
      ix->deep_site = true;
  }
  for (; ix->structs_seen < g->struct_type_count; ix->structs_seen++) {
    zan_symbol_t *sym = g->struct_types[ix->structs_seen].sym;
    zan_type_t *type = sym ? sym->type : NULL;
    for (int depth = 0; type && depth < 32; type = type->base_type, depth++) {
      if (!type->sym)
        continue;
      refl_inst_entry_t *e = refl_inst_entry(ix, type->sym);
      if (!e)
        return false;
      if (e->nderived && e->derived[e->nderived - 1] == ix->structs_seen)
        continue;
      if (e->nderived == e->cap) {
        int ncap = e->cap ? e->cap * 2 : 4;
        int *nd = (int *)realloc(e->derived, (size_t)ncap * sizeof(int));
        if (!nd)
          return false;
        e->derived = nd;
        e->cap = ncap;
      }
      e->derived[e->nderived++] = ix->structs_seen;
    }
  }
  return true;
}

bool refl_owner_can_have_instance(zan_irgen_t *g, zan_symbol_t *owner) {
  if (!g->publish_instance_prune || !owner || !owner->type ||
      owner->type->kind != TYPE_CLASS || refl_ctor_needs_thunk(g))
    return true;
  if (!refl_inst_index_update(g))
    return true;
  refl_inst_index_t *ix = g->refl_inst_index;
  /* Recursive field metadata describes types without creating their objects.
   * A derived allocation can still receive methods declared on this owner. */
  if (ix->deep_site)
    return true;
  refl_inst_entry_t *e = refl_inst_find(ix, owner);
  if (!e)
    return false;
  if (e->has_site || e->vt_store)
    return true;
  /* Runtime guard exceptions have site 0, but their vtable is stored into a
   * real object. Metadata and interface tag comparisons do not make stores.
   * Stores appear as code is emitted, so a miss is re-checked next time. */
  for (int i = 0; i < e->nderived; i++) {
    zan_symbol_t *sym = g->struct_types[e->derived[i]].sym;
    char name[320];
    snprintf(name, sizeof(name), "__zan_vtable_%.*s", (int)sym->name.len,
             sym->name.str);
    if (vtable_has_instance_store(LLVMGetNamedGlobal(g->mod, name), 0)) {
      e->vt_store = true;
      return true;
    }
  }
  return false;
}

static bool refl_invoke_literal_demanded(zan_irgen_t *g, zan_istr_t name) {
  if (!g->publish_reflect_demand)
    return true;
  for (int i = 0; i < g->refl_invoke_name_count; i++) {
    zan_istr_t invoked = g->refl_invoke_names[i];
    if (invoked.len == name.len && memcmp(invoked.str, name.str, name.len) == 0)
      return true;
  }
  return false;
}

typedef struct {
    zan_istr_t *items;
    int count;
    int cap;
} refl_literal_pool_t;

static bool refl_is_identifier_text(const char *s, size_t n) {
    if (n == 0 || n > 255) return false;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        bool ok = c == '_' || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                  (i > 0 && c >= '0' && c <= '9') || c >= 0x80;
        if (!ok) return false;
    }
    return true;
}

static zan_walk_action_t refl_collect_literal(zan_ast_node_t *node, void *user) {
    refl_literal_pool_t *pool = (refl_literal_pool_t *)user;
    if (node->kind != AST_STRING_LITERAL) return ZAN_WALK_CONTINUE;
    zan_istr_t text = node->str_val;
    if (!text.str || !refl_is_identifier_text(text.str, text.len)) return ZAN_WALK_CONTINUE;
    if (pool->count == pool->cap) {
        int cap = pool->cap ? pool->cap * 2 : 256;
        zan_istr_t *items = (zan_istr_t *)realloc(pool->items, (size_t)cap * sizeof(*items));
        if (!items) return ZAN_WALK_ABORT;
        pool->items = items;
        pool->cap = cap;
    }
    pool->items[pool->count++] = text;
    return ZAN_WALK_CONTINUE;
}

static int refl_istr_cmp(const void *a, const void *b) {
    const zan_istr_t *x = (const zan_istr_t *)a, *y = (const zan_istr_t *)b;
    size_t n = x->len < y->len ? x->len : y->len;
    int c = memcmp(x->str, y->str, n);
    if (c) return c;
    return x->len < y->len ? -1 : x->len > y->len ? 1 : 0;
}

/* A computed Invoke name is a runtime string; in a closed-world build it can
 * only name a method if that name appears as a string literal somewhere in
 * the program (or in a design file, or is a framework hook -- handled by the
 * callers). Names assembled purely at runtime (concatenation, JSON) are not
 * supported for plain methods; give such a method an attribute to keep it.
 * On allocation failure the pool stays NULL and every name is kept. */
void refl_collect_literal_names(zan_irgen_t *g, zan_ast_node_t *unit) {
    g->refl_literal_names = NULL;
    g->refl_literal_name_count = -1;
    if (!g->closed_world || !unit) return;
    refl_literal_pool_t pool = {NULL, 0, 0};
    if (!zan_ast_walk(unit, refl_collect_literal, NULL, &pool)) {
        free(pool.items);
        return;
    }
    if (pool.count) qsort(pool.items, (size_t)pool.count, sizeof(*pool.items), refl_istr_cmp);
    int n = 0;
    for (int i = 0; i < pool.count; i++) {
        if (n && refl_istr_cmp(&pool.items[n - 1], &pool.items[i]) == 0) continue;
        pool.items[n++] = pool.items[i];
    }
    zan_istr_t *kept = n ? zan_arena_alloc(g->arena, (size_t)n * sizeof(*kept)) : NULL;
    if (n && !kept) {
        free(pool.items);
        return;
    }
    if (n) memcpy(kept, pool.items, (size_t)n * sizeof(*kept));
    free(pool.items);
    g->refl_literal_names = kept;
    g->refl_literal_name_count = n;
}

static bool refl_invoke_name_demanded(zan_irgen_t *g, zan_istr_t name) {
  if (refl_invoke_literal_demanded(g, name))
    return true;
  if (!g->refl_invoke_unknown)
    return false;
  /* Framework hooks are dispatched by computed names by design. */
  if (refl_is_framework_hook_name(name.str, name.len))
    return true;
  if (g->refl_literal_name_count < 0)
    return true;
  return g->refl_literal_name_count > 0 &&
         bsearch(&name, g->refl_literal_names,
                 (size_t)g->refl_literal_name_count, sizeof(zan_istr_t),
                 refl_istr_cmp) != NULL;
}

/* F4/S1 谓词上移（design.md § P_method_thunk）：thunk 生成与 emit_target
 * body-root 的唯一判定。
 * 非 closed_world（-g / 库 / ZAN_NO_DEV_PRUNE=1）：全部方法。
 * closed_world（发布与普通构建）：仅 accessor（get_/set_ 且 owner 同名
 * property）、带注解方法、被 Invoke 字面名点到（或存在非字面名 Invoke）的方法、
 * 设计稿事件处理器与实际 Invoke 可能选中的框架生命周期钩子。
 * 注意与 refl_method_should_export
 * 的\u533a\u522b：\u672c\u8c13\u8bcd\u4e0d\u770b MOD_PUBLIC\uff08\u79c1\u6709
 * accessor/\u6ce8\u89e3\u4ecd\u751f
 * thunk，\u4e0e\u73b0\u72b6\u4e00\u81f4）\u3002
 */
static bool refl_is_lib_export_owner(zan_irgen_t *g, zan_symbol_t *owner) {
  return g->emit_lib && owner && owner->decl &&
         !owner->decl->from_stdlib && !owner->decl->from_package &&
         !is_user_generic_sym(owner);
}

bool refl_method_needs_thunk(zan_irgen_t *g, zan_symbol_t *owner,
                             zan_ast_node_t *decl) {
  if (!decl || decl->kind != AST_METHOD_DECL) {
    return false;
  }
  if (!g->publish_reflect_demand || refl_is_lib_export_owner(g, owner)) {
    return true;
  }
  if (!(decl->method_decl.modifiers & MOD_STATIC) &&
      !refl_owner_can_have_instance(g, owner))
    return false;
  /* Inherited accessors are collected from their declaring base, while this
   * owner is the reflected receiver; its flattened properties retain names. */
  if (refl_accessor_flag(owner, decl->method_decl.name) != 0) {
    return true;
  }
  zan_ast_list_t *attrs = zan_ast_attributes(decl);
  if (attrs && attrs->count > 0) {
    return true;
  }
  /* Any other method (framework hooks included) is callable exactly when an
   * emitted Invoke may select it: its literal name was invoked, or some
   * Invoke takes a computed name and this name occurs as a string literal.
   * Without this, `obj.InvokeInt("Add", ..)` on a plain public method
   * silently returned 0 under --publish.
   * HasMethod and member enumeration still retain the unchanged signatures. */
  return refl_invoke_name_demanded(g, decl->method_decl.name);
}

static bool refl_method_should_export(zan_irgen_t *g, zan_symbol_t *sym,
                                      zan_symbol_t *m) {
  if (!m || m->kind != SYM_METHOD || !m->decl ||
      m->decl->kind != AST_METHOD_DECL)
    return false;
  if (!g->closed_world)
    return true;
  if (refl_is_framework_hook_name(m->name.str, m->name.len))
    return true;
  /* closed_world（发布与普通构建）下：
   * 1. 属性访问器（get_/set_）必须保留，支持运行期动态数据绑定
   * 2. 带有显式特性注解（attributes）的方法保留
   * 3. 必须是公开方法；私有/内部方法在发布期绝对不暴露反射
   */
  if (refl_accessor_flag(sym, m->name) != 0)
    return true;
  zan_ast_list_t *attrs = zan_ast_attributes(m->decl);
  if (attrs && attrs->count > 0)
    return true;
  uint32_t mods = m->modifiers | m->decl->method_decl.modifiers;
  /* An override takes the overridden member's accessibility: dropping a
   * bare `override` row made Invoke fall through to the base row and run
   * the base implementation. */
  if (!(mods & (MOD_PUBLIC | MOD_OVERRIDE)))
    return false;
  return true;
}

/* 反射方法表条目收集：本类声明 + 沿 base_type 基类链补收继承方法。
 *
 * 字段表本来就含继承条目（sym->members 摊平了基类字段），方法此前
 * 只收本类声明——派生类实例 HasMethod/Invoke 对基类方法全部 miss
 * （基类批量架构的 html @click 处理器声明在基类上即全灭）。
 *
 * 遮蔽语义：名字级、最近声明胜出——与 refl_mfind 按名首中、字段表
 * B-ID147 shadow 口径对齐。基类条目排除静态（实例反射语义）；本类
 * 条目保持旧口径不动（含静态）。
 *
 * out==NULL 时纯计数，供 refl_shape_mtab 定容量；填充与计数共用本
 * 遍历，两处口径永不漂移。 */
static int refl_methods_walk(zan_irgen_t *g, zan_symbol_t *sym,
                             zan_symbol_t **out, int cap) {
  int n = 0;
  if (!sym)
    return 0;
  zan_symbol_t **reg = (zan_symbol_t **)calloc(64, sizeof(zan_symbol_t *));
  int rn = 0, rcap = 64;
  for (int pass = 0; pass < 2; pass++) {
    if (pass == 1 && !reg)
      break; /* 登记表分配失败：退化回本类口径 */
    zan_symbol_t *owner = sym;
    zan_type_t *bt = sym->type ? sym->type->base_type : NULL;
    int depth = 0;
    for (; pass == 0 || (bt && bt->sym && bt->sym != sym && depth < 32);
         bt = bt ? bt->base_type : NULL, depth++) {
      if (pass == 1)
        owner = bt->sym;
      for (int i = 0; i < owner->member_count; i++) {
        zan_symbol_t *m = owner->members[i];
        if (!refl_method_should_export(g, owner, m))
          continue;
        if (pass == 1 && (m->modifiers & MOD_STATIC))
          continue;
        int dup = 0;
        for (int r = 0; r < rn && !dup; r++) {
          if (reg[r]->name.len == m->name.len &&
              memcmp(reg[r]->name.str, m->name.str, (size_t)m->name.len) == 0)
            dup = 1;
        }
        if (dup)
          continue;
        if (reg) {
          if (rn == rcap) {
            int ncap = rcap * 2;
            zan_symbol_t **nreg =
                (zan_symbol_t **)realloc(reg, (size_t)ncap * sizeof(*nreg));
            if (!nreg)
              break;
            reg = nreg;
            rcap = ncap;
          }
          reg[rn++] = m;
        }
        if (out) {
          if (n >= cap)
            break;
          out[n] = m;
        }
        n++;
      }
      if (pass == 0)
        break;
    }
  }
  free(reg);
  return n;
}

static int refl_collect_methods(zan_irgen_t *g, zan_symbol_t *sym,
                                zan_symbol_t **out, int cap) {
  return refl_methods_walk(g, sym, out, cap);
}

static int refl_collect_ctors(zan_symbol_t *sym, zan_ast_node_t **out,
                              int cap) {
  int n = 0;
  if (!sym || !sym->decl)
    return 0;
  zan_ast_list_t *members = &sym->decl->type_decl.members;
  for (int i = 0; i < members->count && n < cap; i++) {
    zan_ast_node_t *m = members->items[i];
    if (m->kind != AST_CONSTRUCTOR_DECL)
      continue;
    if (m->method_decl.modifiers & MOD_STATIC)
      continue;
    out[n++] = m;
  }
  return n;
}

static LLVMValueRef refl_slot_to(zan_irgen_t *g, LLVMValueRef slot,
                                 LLVMTypeRef want, bool *ok) {
  LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
  LLVMTypeRef dbl = LLVMDoubleTypeInContext(g->ctx);
  switch (LLVMGetTypeKind(want)) {
  case LLVMIntegerTypeKind:
    if (want == i64)
      return slot;
    return LLVMBuildTrunc(g->builder, slot, want, "refl.a.tr");
  case LLVMDoubleTypeKind:
    return LLVMBuildBitCast(g->builder, slot, dbl, "refl.a.d");
  case LLVMFloatTypeKind:
    return LLVMBuildFPTrunc(g->builder,
                            LLVMBuildBitCast(g->builder, slot, dbl, "refl.a.d"),
                            want, "refl.a.f");
  case LLVMPointerTypeKind:
    return LLVMBuildIntToPtr(g->builder, slot, want, "refl.a.p");
  default:
    *ok = false;
    return LLVMConstNull(want);
  }
}

static int refl_meta_index_for(zan_irgen_t *g, zan_type_t *t) {
  if (!t)
    return -1;
  LLVMValueRef rec = refl_meta_for(g, t, NULL, 0);
  for (int i = 0; i < g->refl_meta_count; i++) {
    if (g->refl_metas[i].rec == rec)
      return i;
  }
  return -1;
}

static int refl_struct_box_site(zan_irgen_t *g, zan_type_t *st) {
  if (!irgen_boxed_eligible_struct(g, st))
    return -1;
  int mi = refl_meta_index_for(g, st);
  if (mi < 0)
    return reserve_arc_site(g, st->sym, st, 0, NULL);
  if (g->refl_metas[mi].box_site < 0) {
    uint32_t sf = g->di_cur_file, sl = g->di_cur_line;
    g->di_cur_file = g->di_cur_line = 0;
    g->refl_metas[mi].box_site = reserve_arc_site(g, st->sym, st, 0, NULL);
    g->di_cur_file = sf;
    g->di_cur_line = sl;
  }
  return g->refl_metas[mi].box_site;
}

static LLVMValueRef refl_emit_box_struct_val(zan_irgen_t *g, zan_type_t *st,
                                             LLVMValueRef sval,
                                             bool already_owned) {
  LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
  LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
  LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
  if (!irgen_boxed_eligible_struct(g, st))
    return LLVMConstNull(i8ptr);
  LLVMTypeRef lt = map_type(g, st);
  sval = coerce_struct_value(g, sval, st);
  if (!lt || LLVMGetTypeKind(lt) != LLVMStructTypeKind ||
      LLVMGetTypeKind(LLVMTypeOf(sval)) != LLVMStructTypeKind)
    return LLVMConstNull(i8ptr);
  int site = refl_struct_box_site(g, st);
  if (site < 0)
    return LLVMConstNull(i8ptr);
  char nbuf[320];
  snprintf(nbuf, sizeof(nbuf), "box:%.*s", (int)st->name.len,
           st->name.str ? st->name.str : "?");
  LLVMTypeRef alloc_ty = LLVMGlobalGetValueType(g->rt_alloc);
  LLVMValueRef sz8 = LLVMConstAdd(LLVMSizeOf(lt), LLVMConstInt(i64, 8, 0));
  LLVMValueRef raw =
      zan_call2(g->builder, alloc_ty, g->rt_alloc,
                (LLVMValueRef[]){sz8, arc_site_arg(g, site),
                                 zan_irgen_intern_string(g, nbuf)},
                3, "refl.box");
  LLVMValueRef tag_slot =
      LLVMBuildBitCast(g->builder, raw, LLVMPointerType(i8ptr, 0), "refl.boxvp");
  LLVMBuildStore(g->builder, get_vtable_global(g, st->sym), tag_slot);
  if (LLVMCountStructElementTypes(lt) > 0) {
    LLVMValueRef eight = LLVMConstInt(i64, 8, 0);
    LLVMValueRef p8 = LLVMBuildGEP2(g->builder, i8, raw, &eight, 1, "refl.boxpl");
    LLVMValueRef slot =
        LLVMBuildBitCast(g->builder, p8, LLVMPointerType(lt, 0), "refl.boxsl");
    LLVMBuildStore(g->builder, sval, slot);
    if (!already_owned && type_contains_collection_rc(g, st, 0))
      emit_collection_value_retain(g, st, sval, 0);
  }
  return raw;
}

static LLVMValueRef refl_decimal_to_i64(zan_irgen_t *g, LLVMValueRef val) {
  LLVMTypeRef dec_ty = get_decimal_type(g);
  LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
  LLVMValueRef slot = emit_entry_alloca(g, dec_ty, "refl.d2i.s");
  LLVMBuildStore(g->builder, val, slot);
  LLVMTypeRef fn_ty =
      LLVMFunctionType(i64, (LLVMTypeRef[]){LLVMPointerType(dec_ty, 0)}, 1, 0);
  LLVMValueRef fn = get_libc_fn(g, "__zan_decimal_to_int64", fn_ty);
  return zan_call2(g->builder, fn_ty, fn, (LLVMValueRef[]){slot}, 1,
                   "refl.d2i");
}

static LLVMValueRef refl_decimal_to_f64(zan_irgen_t *g, LLVMValueRef val) {
  LLVMTypeRef dec_ty = get_decimal_type(g);
  LLVMTypeRef dbl = LLVMDoubleTypeInContext(g->ctx);
  LLVMValueRef slot = emit_entry_alloca(g, dec_ty, "refl.d2f.s");
  LLVMBuildStore(g->builder, val, slot);
  LLVMTypeRef fn_ty =
      LLVMFunctionType(dbl, (LLVMTypeRef[]){LLVMPointerType(dec_ty, 0)}, 1, 0);
  LLVMValueRef fn = get_libc_fn(g, "__zan_decimal_to_double", fn_ty);
  return zan_call2(g->builder, fn_ty, fn, (LLVMValueRef[]){slot}, 1,
                   "refl.d2f");
}

static LLVMValueRef refl_decimal_from_i64(zan_irgen_t *g, LLVMValueRef val) {
  LLVMTypeRef dec_ty = get_decimal_type(g);
  LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
  LLVMTypeRef vd = LLVMVoidTypeInContext(g->ctx);
  LLVMValueRef slot = emit_entry_alloca(g, dec_ty, "refl.i2d.s");
  LLVMTypeRef fn_ty = LLVMFunctionType(
      vd, (LLVMTypeRef[]){i64, LLVMPointerType(dec_ty, 0)}, 2, 0);
  LLVMValueRef fn = get_libc_fn(g, "__zan_decimal_from_int64", fn_ty);
  zan_call2(g->builder, fn_ty, fn, (LLVMValueRef[]){val, slot}, 2, "");
  return LLVMBuildLoad2(g->builder, dec_ty, slot, "refl.i2d");
}

static LLVMValueRef refl_decimal_from_f64(zan_irgen_t *g, LLVMValueRef val) {
  LLVMTypeRef dec_ty = get_decimal_type(g);
  LLVMTypeRef dbl = LLVMDoubleTypeInContext(g->ctx);
  LLVMTypeRef vd = LLVMVoidTypeInContext(g->ctx);
  LLVMValueRef slot = emit_entry_alloca(g, dec_ty, "refl.f2d.s");
  LLVMTypeRef fn_ty = LLVMFunctionType(
      vd, (LLVMTypeRef[]){dbl, LLVMPointerType(dec_ty, 0)}, 2, 0);
  LLVMValueRef fn = get_libc_fn(g, "__zan_decimal_from_double", fn_ty);
  zan_call2(g->builder, fn_ty, fn, (LLVMValueRef[]){val, slot}, 2, "");
  return LLVMBuildLoad2(g->builder, dec_ty, slot, "refl.f2d");
}

static LLVMValueRef refl_vops_scalar_to_i64(zan_irgen_t *g, zan_type_t *t,
                                            LLVMValueRef val) {
  LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
  if (!t)
    return LLVMConstInt(i64, 0, 0);
  if (t->kind == TYPE_DECIMAL)
    return refl_decimal_to_i64(g, val);
  LLVMTypeKind tk = LLVMGetTypeKind(LLVMTypeOf(val));
  if (tk == LLVMFloatTypeKind || tk == LLVMDoubleTypeKind)
    return emit_fptosi_guarded(g, val, i64, (zan_loc_t){0}, NULL);
  if (tk == LLVMIntegerTypeKind)
    return extend_int_for_slot(g, val, t, i64);
  return LLVMConstInt(i64, 0, 0);
}

static LLVMValueRef refl_vops_scalar_to_f64_bits(zan_irgen_t *g, zan_type_t *t,
                                                 LLVMValueRef val) {
  LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
  LLVMTypeRef dbl = LLVMDoubleTypeInContext(g->ctx);
  if (!t)
    return LLVMConstInt(i64, 0, 0);
  if (t->kind == TYPE_DECIMAL)
    return LLVMBuildBitCast(g->builder, refl_decimal_to_f64(g, val), i64,
                            "vops.df");
  LLVMTypeKind tk = LLVMGetTypeKind(LLVMTypeOf(val));
  if (tk == LLVMFloatTypeKind)
    return LLVMBuildBitCast(
        g->builder, LLVMBuildFPExt(g->builder, val, dbl, "vops.fext"), i64,
        "vops.fb");
  if (tk == LLVMDoubleTypeKind)
    return LLVMBuildBitCast(g->builder, val, i64, "vops.db");
  if (tk == LLVMIntegerTypeKind)
    return LLVMBuildBitCast(
        g->builder,
        LLVMBuildSIToFP(g->builder, extend_int_for_slot(g, val, t, i64), dbl,
                        "vops.i2d"),
        i64, "vops.ib");
  return LLVMConstInt(i64, 0, 0);
}

static bool refl_type_is_numeric_scalar(zan_type_t *t) {
  if (!t)
    return false;
  switch (t->kind) {
  case TYPE_BOOL:
  case TYPE_BYTE:
  case TYPE_SBYTE:
  case TYPE_SHORT:
  case TYPE_USHORT:
  case TYPE_INT:
  case TYPE_UINT:
  case TYPE_LONG:
  case TYPE_ULONG:
  case TYPE_NINT:
  case TYPE_CHAR:
  case TYPE_ENUM:
  case TYPE_FLOAT:
  case TYPE_DOUBLE:
    return true;
  default:
    return false;
  }
}

static LLVMValueRef refl_convert_slot_to_scalar(zan_irgen_t *g, zan_type_t *t,
                                                LLVMValueRef val,
                                                LLVMValueRef vkind) {
  LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
  LLVMTypeRef dbl = LLVMDoubleTypeInContext(g->ctx);
  LLVMTypeRef flt = LLVMFloatTypeInContext(g->ctx);
  LLVMTypeRef target = map_type(g, t);
  LLVMValueRef is_fp =
      LLVMBuildICmp(g->builder, LLVMIntEQ, vkind,
                    LLVMConstInt(i64, ZAN_REFL_VK_DOUBLE, 0), "vops.isfp");
  if (t->kind == TYPE_FLOAT || t->kind == TYPE_DOUBLE) {
    LLVMValueRef d = LLVMBuildSelect(
        g->builder, is_fp, LLVMBuildBitCast(g->builder, val, dbl, "vops.dbc"),
        LLVMBuildSIToFP(g->builder, val, dbl, "vops.i2f"), "vops.dval");
    return t->kind == TYPE_FLOAT
               ? LLVMBuildFPTrunc(g->builder, d, flt, "vops.flt")
               : d;
  }
  LLVMValueRef iv = LLVMBuildSelect(
      g->builder, is_fp,
      emit_fptosi_guarded(g,
                          LLVMBuildBitCast(g->builder, val, dbl, "vops.dbc2"),
                          i64, (zan_loc_t){0}, is_fp),
      val, "vops.ival");
  if (t->kind == TYPE_BOOL)
    return LLVMBuildICmp(g->builder, LLVMIntNE, iv, LLVMConstInt(i64, 0, 0),
                         "vops.b");
  if (target == i64)
    return iv;
  return LLVMBuildTrunc(g->builder, iv, target, "vops.tr");
}

static LLVMValueRef refl_get_vops_fn(zan_irgen_t *g, zan_type_t *t) {
  if (!t || !type_is_concrete(t) || type_mentions_canon(t))
    return NULL;
  if (t->kind != TYPE_STRUCT && t->kind != TYPE_NULLABLE)
    return NULL;
  if (t->kind == TYPE_STRUCT) {
    if (!irgen_boxed_eligible_struct(g, t))
      return NULL;
    LLVMTypeRef lt = map_type(g, t);
    if (!lt || LLVMGetTypeKind(lt) != LLVMStructTypeKind)
      return NULL;
  } else {
    if (!t->element_type || !llvm_is_nullable(map_type(g, t)))
      return NULL;
  }
  int mi = refl_meta_index_for(g, t);
  if (mi < 0)
    return NULL;
  if (g->refl_metas[mi].vops)
    return g->refl_metas[mi].vops;

  LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
  LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
  LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
  LLVMTypeRef dbl = LLVMDoubleTypeInContext(g->ctx);
  LLVMTypeRef fn_ty =
      LLVMFunctionType(i64, (LLVMTypeRef[]){i64, i8ptr, i64, i64}, 4, 0);
  char nm[96];
  snprintf(nm, sizeof(nm), "__zan.refl.vops.%d", mi);
  LLVMValueRef fn = LLVMAddFunction(g->mod, nm, fn_ty);
  LLVMSetLinkage(fn, LLVMInternalLinkage);
  LLVMValueRef fn_ptr = LLVMConstBitCast(fn, i8ptr);
  g->refl_metas[mi].vops = fn_ptr;

  uint32_t saved_file = g->di_cur_file, saved_line = g->di_cur_line;
  uint32_t saved_col = g->di_cur_col;
  LLVMMetadataRef saved_last_loc = g->di_last_loc;
  LLVMValueRef saved_last_fn = g->di_last_loc_fn;
  zan_builder_point_t pt = zan_builder_save(g);
  LLVMValueRef saved_fn = g->current_fn;
  LLVMTypeRef saved_fn_ret = g->current_fn_ret_type;
  zan_type_t *saved_fn_zan_ret = g->current_fn_zan_ret_type;
  zan_ast_node_t *saved_fn_body = g->current_fn_body;
  bool saved_fn_is_main = g->current_fn_is_main;
  bool saved_fn_is_ctor = g->current_fn_is_ctor;

  g->current_fn = fn;
  g->current_fn_ret_type = i64;
  g->current_fn_zan_ret_type = g->binder->type_long;
  g->current_fn_body = NULL;
  g->current_fn_is_main = false;
  g->current_fn_is_ctor = false;

  LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
  LLVMBasicBlockRef b_gi = LLVMAppendBasicBlockInContext(g->ctx, fn, "get_i64");
  LLVMBasicBlockRef b_gf = LLVMAppendBasicBlockInContext(g->ctx, fn, "get_f64");
  LLVMBasicBlockRef b_go = LLVMAppendBasicBlockInContext(g->ctx, fn, "get_obj");
  LLVMBasicBlockRef b_set = LLVMAppendBasicBlockInContext(g->ctx, fn, "set");
  LLVMBasicBlockRef b_zero = LLVMAppendBasicBlockInContext(g->ctx, fn, "zero");

  LLVMValueRef op = LLVMGetParam(fn, 0);
  LLVMValueRef slot_ptr = LLVMGetParam(fn, 1);
  LLVMValueRef val = LLVMGetParam(fn, 2);
  LLVMValueRef vkind = LLVMGetParam(fn, 3);

  LLVMPositionBuilderAtEnd(g->builder, entry);
  di_clear(g);
  LLVMValueRef sw = LLVMBuildSwitch(g->builder, op, b_zero, 4);
  LLVMAddCase(sw, LLVMConstInt(i64, ZAN_REFL_VOP_GET_I64, 0), b_gi);
  LLVMAddCase(sw, LLVMConstInt(i64, ZAN_REFL_VOP_GET_F64, 0), b_gf);
  LLVMAddCase(sw, LLVMConstInt(i64, ZAN_REFL_VOP_GET_OBJ, 0), b_go);
  LLVMAddCase(sw, LLVMConstInt(i64, ZAN_REFL_VOP_SET, 0), b_set);

  if (t->kind == TYPE_STRUCT) {
    LLVMTypeRef st = map_type(g, t);
    LLVMValueRef sp =
        LLVMBuildBitCast(g->builder, slot_ptr, LLVMPointerType(st, 0), "v.sp");
    LLVMPositionBuilderAtEnd(g->builder, b_gi);
    LLVMBuildRet(g->builder, LLVMConstInt(i64, 0, 0));

    LLVMPositionBuilderAtEnd(g->builder, b_gf);
    LLVMBuildRet(g->builder, LLVMConstInt(i64, 0, 0));

    LLVMPositionBuilderAtEnd(g->builder, b_go);
    LLVMValueRef sval = LLVMBuildLoad2(g->builder, st, sp, "v.sv");
    LLVMValueRef box = refl_emit_box_struct_val(g, t, sval, false);
    LLVMBuildRet(g->builder, LLVMBuildPtrToInt(g->builder, box, i64, "v.bi"));

    LLVMPositionBuilderAtEnd(g->builder, b_set);
    LLVMValueRef is_ptr =
        LLVMBuildAnd(g->builder,
                     LLVMBuildICmp(g->builder, LLVMIntEQ, vkind,
                                   LLVMConstInt(i64, ZAN_REFL_VK_PTR, 0),
                                   "v.isptr"),
                     LLVMBuildICmp(g->builder, LLVMIntNE, val,
                                   LLVMConstInt(i64, 0, 0), "v.nn"),
                     "v.ptrok");
    LLVMBasicBlockRef chk_tag =
        LLVMAppendBasicBlockInContext(g->ctx, fn, "set.tag");
    LLVMBasicBlockRef do_set =
        LLVMAppendBasicBlockInContext(g->ctx, fn, "set.ok");
    LLVMBuildCondBr(g->builder, is_ptr, chk_tag, b_zero);

    LLVMPositionBuilderAtEnd(g->builder, chk_tag);
    LLVMValueRef boxp = LLVMBuildIntToPtr(g->builder, val, i8ptr, "v.bp");
    LLVMValueRef tag_ok = emit_runtime_is_check_name(g, boxp, t);
    LLVMBuildCondBr(g->builder, tag_ok, do_set, b_zero);

    LLVMPositionBuilderAtEnd(g->builder, do_set);
    if (LLVMCountStructElementTypes(st) > 0) {
      LLVMValueRef eight = LLVMConstInt(i64, 8, 0);
      LLVMValueRef p8 = LLVMBuildGEP2(g->builder, i8, boxp, &eight, 1, "v.p8");
      LLVMValueRef nsv = LLVMBuildLoad2(
          g->builder, st,
          LLVMBuildBitCast(g->builder, p8, LLVMPointerType(st, 0), "v.np"),
          "v.nsv");
      if (type_contains_collection_rc(g, t, 0)) {
        emit_collection_value_retain(g, t, nsv, 0);
        LLVMValueRef osv = LLVMBuildLoad2(g->builder, st, sp, "v.osv");
        LLVMBuildStore(g->builder, nsv, sp);
        emit_collection_value_release(g, t, osv, 0);
      } else {
        LLVMBuildStore(g->builder, nsv, sp);
      }
    }
    LLVMBuildRet(g->builder, LLVMConstInt(i64, 1, 0));
  } else {
    zan_type_t *et = t->element_type;
    LLVMTypeRef nty = map_type(g, t);
    LLVMValueRef np =
        LLVMBuildBitCast(g->builder, slot_ptr, LLVMPointerType(nty, 0), "v.np");

    /* GET_I64 */
    LLVMPositionBuilderAtEnd(g->builder, b_gi);
    {
      LLVMValueRef nv = LLVMBuildLoad2(g->builder, nty, np, "v.nv");
      LLVMBasicBlockRef some =
          LLVMAppendBasicBlockInContext(g->ctx, fn, "gi.some");
      LLVMBuildCondBr(g->builder, nullable_has_value(g, nv), some, b_zero);
      LLVMPositionBuilderAtEnd(g->builder, some);
      LLVMBuildRet(g->builder,
                   refl_vops_scalar_to_i64(g, et, nullable_get_payload(g, nv)));
    }

    /* GET_F64 */
    LLVMPositionBuilderAtEnd(g->builder, b_gf);
    {
      LLVMValueRef nv = LLVMBuildLoad2(g->builder, nty, np, "v.nv");
      LLVMBasicBlockRef some =
          LLVMAppendBasicBlockInContext(g->ctx, fn, "gf.some");
      LLVMBuildCondBr(g->builder, nullable_has_value(g, nv), some, b_zero);
      LLVMPositionBuilderAtEnd(g->builder, some);
      LLVMBuildRet(
          g->builder,
          refl_vops_scalar_to_f64_bits(g, et, nullable_get_payload(g, nv)));
    }

    /* GET_OBJ */
    LLVMPositionBuilderAtEnd(g->builder, b_go);
    if (et->kind == TYPE_STRUCT && irgen_boxed_eligible_struct(g, et)) {
      LLVMValueRef nv = LLVMBuildLoad2(g->builder, nty, np, "v.nv");
      LLVMBasicBlockRef some =
          LLVMAppendBasicBlockInContext(g->ctx, fn, "go.some");
      LLVMBuildCondBr(g->builder, nullable_has_value(g, nv), some, b_zero);
      LLVMPositionBuilderAtEnd(g->builder, some);
      LLVMValueRef box =
          refl_emit_box_struct_val(g, et, nullable_get_payload(g, nv), false);
      LLVMBuildRet(g->builder, LLVMBuildPtrToInt(g->builder, box, i64, "v.bi"));
    } else {
      LLVMBuildRet(g->builder, LLVMConstInt(i64, 0, 0));
    }

    /* SET */
    LLVMPositionBuilderAtEnd(g->builder, b_set);
    LLVMValueRef is_null_arg = LLVMBuildAnd(
        g->builder,
        LLVMBuildICmp(g->builder, LLVMIntEQ, vkind,
                      LLVMConstInt(i64, ZAN_REFL_VK_PTR, 0), "v.iskp"),
        LLVMBuildICmp(g->builder, LLVMIntEQ, val, LLVMConstInt(i64, 0, 0),
                      "v.isz"),
        "v.isnull");
    LLVMBasicBlockRef set_null =
        LLVMAppendBasicBlockInContext(g->ctx, fn, "set.null");
    LLVMBasicBlockRef set_val =
        LLVMAppendBasicBlockInContext(g->ctx, fn, "set.val");
    LLVMBuildCondBr(g->builder, is_null_arg, set_null, set_val);

    LLVMPositionBuilderAtEnd(g->builder, set_null);
    if (type_contains_collection_rc(g, et, 0)) {
      LLVMValueRef onv = LLVMBuildLoad2(g->builder, nty, np, "v.onv");
      LLVMBuildStore(g->builder, LLVMConstNull(nty), np);
      emit_collection_value_release(g, t, onv, 0);
    } else {
      LLVMBuildStore(g->builder, LLVMConstNull(nty), np);
    }
    LLVMBuildRet(g->builder, LLVMConstInt(i64, 1, 0));

    LLVMPositionBuilderAtEnd(g->builder, set_val);
    if (et->kind == TYPE_STRUCT && irgen_boxed_eligible_struct(g, et)) {
      LLVMValueRef is_ptr = LLVMBuildICmp(
          g->builder, LLVMIntEQ, vkind, LLVMConstInt(i64, ZAN_REFL_VK_PTR, 0),
          "v.isptr");
      LLVMBasicBlockRef chk_tag =
          LLVMAppendBasicBlockInContext(g->ctx, fn, "set.tag");
      LLVMBasicBlockRef do_set =
          LLVMAppendBasicBlockInContext(g->ctx, fn, "set.ok");
      LLVMBuildCondBr(g->builder, is_ptr, chk_tag, b_zero);

      LLVMPositionBuilderAtEnd(g->builder, chk_tag);
      LLVMValueRef boxp = LLVMBuildIntToPtr(g->builder, val, i8ptr, "v.bp");
      LLVMValueRef tag_ok = emit_runtime_is_check_name(g, boxp, et);
      LLVMBuildCondBr(g->builder, tag_ok, do_set, b_zero);

      LLVMPositionBuilderAtEnd(g->builder, do_set);
      LLVMTypeRef est = map_type(g, et);
      LLVMValueRef nsv = LLVMConstNull(est);
      if (LLVMCountStructElementTypes(est) > 0) {
        LLVMValueRef eight = LLVMConstInt(i64, 8, 0);
        LLVMValueRef p8 = LLVMBuildGEP2(g->builder, i8, boxp, &eight, 1, "v.p8");
        nsv = LLVMBuildLoad2(
            g->builder, est,
            LLVMBuildBitCast(g->builder, p8, LLVMPointerType(est, 0), "v.np"),
            "v.nsv");
      }
      LLVMValueRef nnv = nullable_some(g, nty, nsv);
      if (type_contains_collection_rc(g, et, 0)) {
        emit_collection_value_retain(g, et, nsv, 0);
        LLVMValueRef onv = LLVMBuildLoad2(g->builder, nty, np, "v.onv");
        LLVMBuildStore(g->builder, nnv, np);
        emit_collection_value_release(g, t, onv, 0);
      } else {
        LLVMBuildStore(g->builder, nnv, np);
      }
      LLVMBuildRet(g->builder, LLVMConstInt(i64, 1, 0));
    } else if (et->kind == TYPE_DECIMAL) {
      LLVMBasicBlockRef d_dec =
          LLVMAppendBasicBlockInContext(g->ctx, fn, "set.dec");
      LLVMBasicBlockRef d_int =
          LLVMAppendBasicBlockInContext(g->ctx, fn, "set.dint");
      LLVMBasicBlockRef d_dbl =
          LLVMAppendBasicBlockInContext(g->ctx, fn, "set.ddbl");
      LLVMValueRef dsw = LLVMBuildSwitch(g->builder, vkind, b_zero, 3);
      LLVMAddCase(dsw, LLVMConstInt(i64, ZAN_REFL_VK_DECIMAL, 0), d_dec);
      LLVMAddCase(dsw, LLVMConstInt(i64, ZAN_REFL_VK_INT, 0), d_int);
      LLVMAddCase(dsw, LLVMConstInt(i64, ZAN_REFL_VK_DOUBLE, 0), d_dbl);

      LLVMPositionBuilderAtEnd(g->builder, d_dec);
      LLVMBasicBlockRef d_decld =
          LLVMAppendBasicBlockInContext(g->ctx, fn, "set.decld");
      LLVMBuildCondBr(g->builder,
                      LLVMBuildICmp(g->builder, LLVMIntNE, val,
                                    LLVMConstInt(i64, 0, 0), "v.dnn"),
                      d_decld, b_zero);
      LLVMPositionBuilderAtEnd(g->builder, d_decld);
      LLVMTypeRef dec_ty = get_decimal_type(g);
      LLVMValueRef dv = LLVMBuildLoad2(
          g->builder, dec_ty,
          LLVMBuildIntToPtr(g->builder, val, LLVMPointerType(dec_ty, 0),
                            "v.dp"),
          "v.dv");
      LLVMBuildStore(g->builder, nullable_some(g, nty, dv), np);
      LLVMBuildRet(g->builder, LLVMConstInt(i64, 1, 0));

      LLVMPositionBuilderAtEnd(g->builder, d_int);
      LLVMValueRef di = refl_decimal_from_i64(g, val);
      LLVMBuildStore(g->builder, nullable_some(g, nty, di), np);
      LLVMBuildRet(g->builder, LLVMConstInt(i64, 1, 0));

      LLVMPositionBuilderAtEnd(g->builder, d_dbl);
      LLVMValueRef df = refl_decimal_from_f64(
          g, LLVMBuildBitCast(g->builder, val, dbl, "v.dfbc"));
      LLVMBuildStore(g->builder, nullable_some(g, nty, df), np);
      LLVMBuildRet(g->builder, LLVMConstInt(i64, 1, 0));
    } else if (refl_type_is_numeric_scalar(et)) {
      LLVMValueRef is_num = LLVMBuildOr(
          g->builder,
          LLVMBuildICmp(g->builder, LLVMIntEQ, vkind,
                        LLVMConstInt(i64, ZAN_REFL_VK_INT, 0), "v.isi"),
          LLVMBuildICmp(g->builder, LLVMIntEQ, vkind,
                        LLVMConstInt(i64, ZAN_REFL_VK_DOUBLE, 0), "v.isd"),
          "v.isnum");
      LLVMBasicBlockRef do_set =
          LLVMAppendBasicBlockInContext(g->ctx, fn, "set.num");
      LLVMBuildCondBr(g->builder, is_num, do_set, b_zero);

      LLVMPositionBuilderAtEnd(g->builder, do_set);
      LLVMValueRef pv = refl_convert_slot_to_scalar(g, et, val, vkind);
      LLVMBuildStore(g->builder, nullable_some(g, nty, pv), np);
      LLVMBuildRet(g->builder, LLVMConstInt(i64, 1, 0));
    } else {
      LLVMBuildRet(g->builder, LLVMConstInt(i64, 0, 0));
    }
  }

  LLVMPositionBuilderAtEnd(g->builder, b_zero);
  LLVMBuildRet(g->builder, LLVMConstInt(i64, 0, 0));

  g->current_fn = saved_fn;
  g->current_fn_ret_type = saved_fn_ret;
  g->current_fn_zan_ret_type = saved_fn_zan_ret;
  g->current_fn_body = saved_fn_body;
  g->current_fn_is_main = saved_fn_is_main;
  g->current_fn_is_ctor = saved_fn_is_ctor;
  zan_builder_restore(g, pt);
  g->di_cur_file = saved_file;
  g->di_cur_line = saved_line;
  g->di_cur_col = saved_col;
  g->di_last_loc = saved_last_loc;
  g->di_last_loc_fn = saved_last_fn;
  return fn_ptr;
}
