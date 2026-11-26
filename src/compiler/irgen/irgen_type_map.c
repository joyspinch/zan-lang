#include "checker.h"

#define ZAN_NULLABLE_PREFIX "zan.nullable."
LLVMValueRef coerce_int_to(zan_irgen_t *g, LLVMValueRef v, LLVMTypeRef target);
bool llvm_is_nullable(LLVMTypeRef t) {
  if (!t || LLVMGetTypeKind(t) != LLVMStructTypeKind)
    return false;
  const char *n = LLVMGetStructName(t);
  return n &&
         strncmp(n, ZAN_NULLABLE_PREFIX, sizeof(ZAN_NULLABLE_PREFIX) - 1) == 0;
}
LLVMTypeRef nullable_payload_type(LLVMTypeRef t) {
  return LLVMStructGetTypeAtIndex(t, 0);
}
LLVMTypeRef nullable_type_of(zan_irgen_t *g, LLVMTypeRef payload) {
  char name[256];
  size_t off = 0;
  memcpy(name, ZAN_NULLABLE_PREFIX, sizeof(ZAN_NULLABLE_PREFIX) - 1);
  off = sizeof(ZAN_NULLABLE_PREFIX) - 1;
  char *pn = LLVMPrintTypeToString(payload);
  for (const char *p = pn; p && *p && off < sizeof(name) - 1; p++) {
    char c = (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
                     (*p >= '0' && *p <= '9')
                 ? *p
                 : '_';
    name[off++] = c;
  }
  name[off] = 0;
  if (pn)
    LLVMDisposeMessage(pn);
  LLVMTypeRef existing = LLVMGetTypeByName2(g->ctx, name);
  if (existing)
    return existing;
  LLVMTypeRef st = LLVMStructCreateNamed(g->ctx, name);
  LLVMTypeRef body[2] = {payload, LLVMInt1TypeInContext(g->ctx)};
  LLVMStructSetBody(st, body, 2, 0);
  return st;
}
LLVMValueRef nullable_none(LLVMTypeRef nty) { return LLVMConstNull(nty); }
LLVMValueRef nullable_some(zan_irgen_t *g, LLVMTypeRef nty, LLVMValueRef v) {
  LLVMTypeRef pl = nullable_payload_type(nty);
  LLVMValueRef fit = coerce_int_to(g, v, pl);
  if (LLVMTypeOf(fit) != pl)
    return NULL;
  LLVMValueRef agg =
      LLVMBuildInsertValue(g->builder, LLVMGetUndef(nty), fit, 0, "nv.val");
  return LLVMBuildInsertValue(g->builder, agg,
                              LLVMConstInt(LLVMInt1TypeInContext(g->ctx), 1, 0),
                              1, "nv.some");
}
LLVMValueRef nullable_has_value(zan_irgen_t *g, LLVMValueRef v) {
  return LLVMBuildExtractValue(g->builder, v, 1, "nv.has");
}
LLVMValueRef nullable_get_payload(zan_irgen_t *g, LLVMValueRef v) {
  return LLVMBuildExtractValue(g->builder, v, 0, "nv.get");
}
void register_struct_type(zan_irgen_t *g, zan_symbol_t *sym);

static LLVMTypeRef map_tuple_struct(zan_irgen_t *g, zan_type_t *type) {
  if (!type || !type->sym)
    return LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
  for (int i = 0; i < g->struct_type_count; i++) {
    if (g->struct_types[i].sym == type->sym) {
      return g->struct_types[i].llvm_type;
    }
  }
  /* 有声明或有合成成员时算布局——此时必须把结构体类型注册进去。
   * 合成类型（如 Tuple、LINQ 中间 row）无 AST 声明节点但有 members，
   * 普通结构体/类有 decl 节点。两者都需要注册 LLVM struct 布局，
   * 否则字段类型会降级成 i8* / ptr，导致 GEP 校验失败或字段错位。 */
  if (type->sym->decl || type->sym->member_count > 0)
    register_struct_type(g, type->sym);
  for (int i = 0; i < g->struct_type_count; i++) {
    if (g->struct_types[i].sym == type->sym) {
      return g->struct_types[i].llvm_type;
    }
  }
  return LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
}

LLVMTypeRef get_decimal_type(zan_irgen_t *g) {
  g->uses_decimal_runtime = true;
  if (!g->decimal_struct_type) {
    LLVMTypeRef dec_fields[] = {LLVMInt32TypeInContext(g->ctx),
                                LLVMInt32TypeInContext(g->ctx),
                                LLVMInt64TypeInContext(g->ctx)};
    g->decimal_struct_type = LLVMStructCreateNamed(g->ctx, "Decimal");
    LLVMStructSetBody(g->decimal_struct_type, dec_fields, 3, 0);
  }
  return g->decimal_struct_type;
}
LLVMTypeRef map_type(zan_irgen_t *g, zan_type_t *type) {
  if (!type)
    return LLVMVoidTypeInContext(g->ctx);
  if (type->is_canon)
    return LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
  switch (type->kind) {
  case TYPE_VOID:
    return LLVMVoidTypeInContext(g->ctx);
  case TYPE_BOOL:
    return LLVMInt1TypeInContext(g->ctx);
  case TYPE_BYTE:
    return LLVMInt8TypeInContext(g->ctx);
  case TYPE_SHORT:
    return LLVMInt16TypeInContext(g->ctx);
  case TYPE_INT:
    return LLVMInt32TypeInContext(g->ctx);
  case TYPE_LONG:
    return LLVMInt64TypeInContext(g->ctx);
  case TYPE_SBYTE:
    return LLVMInt8TypeInContext(g->ctx);
  case TYPE_USHORT:
    return LLVMInt16TypeInContext(g->ctx);
  case TYPE_UINT:
    return LLVMInt32TypeInContext(g->ctx);
  case TYPE_ULONG:
    return LLVMInt64TypeInContext(g->ctx);
  case TYPE_NINT:
    return LLVMInt64TypeInContext(g->ctx);
  case TYPE_TASK:
    return LLVMInt64TypeInContext(g->ctx);
  case TYPE_FLOAT:
    return LLVMFloatTypeInContext(g->ctx);
  case TYPE_DOUBLE:
    return LLVMDoubleTypeInContext(g->ctx);
  case TYPE_DECIMAL:
    return get_decimal_type(g);
  case TYPE_CHAR:
    return LLVMInt64TypeInContext(g->ctx);
  case TYPE_STRING:
    return LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
  case TYPE_OBJECT:
    return LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
  case TYPE_ENUM:
    return LLVMInt64TypeInContext(g->ctx);
  case TYPE_NULLABLE:
    if (type->element_type)
      return nullable_type_of(g, map_type(g, type->element_type));
    return LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
  case TYPE_DELEGATE:
    /* Delegate values use opaque pointers; invocation builds its own
     * function signature. Expanding it here loops on recursive delegates. */
    return LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
  case TYPE_STRUCT:
  case TYPE_CLASS: {
    if ((type->name.len == 4 && memcmp(type->name.str, "Span", 4) == 0) ||
        (type->name.len == 12 &&
         memcmp(type->name.str, "ReadOnlySpan", 12) == 0))
      return g->span_struct_type;
    for (int i = 0; i < g->struct_type_count; i++) {
      if (g->struct_types[i].sym == type->sym) {
        if (type->kind == TYPE_CLASS) {
          return LLVMPointerType(g->struct_types[i].llvm_type, 0);
        }
        return g->struct_types[i].llvm_type;
      }
    }
    /* 还没注册：有声明或合成成员才能算布局——先注册再取。
     * 类取指针（引用语义），结构体取值（值语义）。 */
    if (type->sym && (type->sym->decl || type->sym->member_count > 0)) {
      register_struct_type(g, type->sym);
      for (int i = 0; i < g->struct_type_count; i++) {
        if (g->struct_types[i].sym == type->sym) {
          if (type->kind == TYPE_CLASS) {
            return LLVMPointerType(g->struct_types[i].llvm_type, 0);
          }
          return g->struct_types[i].llvm_type;
        }
      }
    }
    return map_tuple_struct(g, type);
  }
  default:
    return LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
  }
}
LLVMValueRef promote_loaded(zan_irgen_t *g, LLVMValueRef v, zan_type_t *type) {
  if (!v || !type)
    return v;
  if (LLVMGetTypeKind(LLVMTypeOf(v)) != LLVMIntegerTypeKind)
    return v;
  if (LLVMGetIntTypeWidth(LLVMTypeOf(v)) >= 64)
    return v;
  LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
  switch (type->kind) {
  case TYPE_BYTE:
    /* byte 是无符号窄整型：装载值必须零拓宽到规范 i64 表示，
     * 否则裸 i8 落进下游 SIToFP 会按符号位解释（255 → -1.0）。 */
    return LLVMBuildZExt(g->builder, v, i64t, "zx.u8");
  case TYPE_SBYTE:
    return LLVMBuildSExt(g->builder, v, i64t, "sx.i8");
  case TYPE_CHAR:
    /* char 语义 = 无符号 UTF-16 码元（0..65535），同 byte 零拓宽。 */
    return LLVMBuildZExt(g->builder, v, i64t, "zx.char");
  case TYPE_USHORT:
    return LLVMBuildZExt(g->builder, v, i64t, "zx.u16");
  case TYPE_UINT:
    return LLVMBuildZExt(g->builder, v, i64t, "zx.u32");
  default:
    return v;
  }
}
bool class_has_virtual_methods(zan_symbol_t *sym);
bool decl_is_explicit_layout(zan_symbol_t *sym);

/* 类布局里的 vptr 槽：类一律在 0 号槽留一个指针位。
 *
 * 为什么要"一律"：继承链上只要有一处不一致，同一个字段就会有两套下标。
 * 以前 vptr 槽只在"本类或祖先有多态"时才留，于是
 *     Base（无接口、无虚方法 → 无 vptr）
 *     Derived : Base, IFace（实现了接口 → 有 vptr）
 * 两条布局里 Base 的字段一个在下标 0、一个在下标 1：基类构造器把字段写
 * 进了派生对象的 vptr 槽，基类方法读到的也是错位的槽位——派生类一继承
 * 基类就坏（字段读到垃圾，ARC 释放也漏掉基类字段）。
 *
 * 类一律留这个槽之后，"这条链上有没有多态"就不再影响字段偏移：基类与
 * 派生类对同一批字段给出同一个下标，跨模块（包的基类 + 应用的派生类）
 * 也对得上。结构体是值类型，没有 vptr 槽；显式布局类型自己给偏移，
 * 同样不留。
 */
/* 2026-11-24 起改为"按继承树"：树根（最上层的非 object 祖先）的所有后代里
 * 只要有一个类有虚/抽象/覆写方法或实现了接口，整棵树都留槽；整棵树都没有
 * 多态（binary-trees 的 Node 这类）就不留——槽里永远是 null，白占 8 字节。
 * 同一棵树里所有类的答案相同，所以基类与派生类对同一字段的下标仍一致。
 * zanc 是整程序编译器：zan_irgen_emit 在注册任何类布局之前调
 * class_vptr_plan_compute 扫一遍全部类型声明；计划算出来之前（或计划外的
 * 类）一律按旧口径留槽。按首字比对身份的分派（接口分派、object.ToString
 * 候选链）只在有 vptr 的对象上命中：无 vptr 对象的首字是字段值，不会等于
 * 任何 vtable 的地址。设计见 docs/OBJECT_LAYOUT.md。 */
static zan_symbol_t **g_vptr_poly_roots; /* 有多态的树根 */
static int g_vptr_poly_count, g_vptr_poly_cap;
static zan_symbol_t **g_vptr_known; /* 计划覆盖到的全部类 */
static int g_vptr_known_count, g_vptr_known_cap;
static bool g_vptr_plan_ready;

static zan_symbol_t *class_layout_root(zan_symbol_t *sym) {
    for (int guard = 0; sym && guard < 256; guard++) {
        zan_symbol_t *base = sym->type && sym->type->base_type ? sym->type->base_type->sym : NULL;
        if (!base || base == sym || base->kind != SYM_CLASS) break;
        sym = base;
    }
    return sym;
}
static bool vptr_set_has(zan_symbol_t **set, int n, zan_symbol_t *sym) {
    for (int i = 0; i < n; i++)
        if (set[i] == sym) return true;
    return false;
}
static void vptr_set_add(zan_symbol_t ***set, int *n, int *cap, zan_symbol_t *sym) {
    if (vptr_set_has(*set, *n, sym)) return;
    if (*n == *cap) {
        int nc = *cap ? *cap * 2 : 64;
        zan_symbol_t **ns = (zan_symbol_t **)realloc(*set, (size_t)nc * sizeof(*ns));
        if (!ns) {
            fprintf(stderr, "zanc: out of memory planning class layouts\n");
            exit(1);
        }
        *set = ns;
        *cap = nc;
    }
    (*set)[(*n)++] = sym;
}
static void vptr_plan_visit(zan_symbol_t *sym, int depth) {
    if (!sym || depth > 32) return;
    if (sym->kind == SYM_CLASS) {
        vptr_set_add(&g_vptr_known, &g_vptr_known_count, &g_vptr_known_cap, sym);
        if (class_has_virtual_methods(sym))
            vptr_set_add(&g_vptr_poly_roots, &g_vptr_poly_count, &g_vptr_poly_cap,
                         class_layout_root(sym));
    }
    if (sym->kind != SYM_CLASS && sym->kind != SYM_STRUCT && sym->kind != SYM_NAMESPACE) return;
    for (int i = 0; i < sym->member_count; i++) {
        zan_symbol_t *m = sym->members[i];
        if (m && (m->kind == SYM_CLASS || m->kind == SYM_STRUCT || m->kind == SYM_NAMESPACE))
            vptr_plan_visit(m, depth + 1);
    }
}
void class_vptr_plan_compute(zan_irgen_t *g, zan_ast_node_t *unit) {
    g_vptr_poly_count = 0;
    g_vptr_known_count = 0;
    g_vptr_plan_ready = false;
    const char *off = getenv("ZAN_VPTR_ALWAYS");
    if (off && strcmp(off, "1") == 0) return;
    for (int i = 0; i < unit->comp_unit.decls.count; i++) {
        zan_ast_node_t *decl = unit->comp_unit.decls.items[i];
        if (decl->kind != AST_CLASS_DECL && decl->kind != AST_STRUCT_DECL) continue;
        vptr_plan_visit(zan_binder_lookup(g->binder, decl->type_decl.name), 0);
    }
    /* 计划之前就注册过布局的类（初始化期的运行时类型）已经按留槽排好了：
     * 把它们的树根记成"留槽"，整棵树跟着它们走。 */
    for (int i = 0; i < g->struct_type_count; i++) {
        zan_symbol_t *sym = g->struct_types[i].sym;
        if (sym && sym->kind == SYM_CLASS)
            vptr_set_add(&g_vptr_poly_roots, &g_vptr_poly_count, &g_vptr_poly_cap,
                         class_layout_root(sym));
    }
    g_vptr_plan_ready = true;
}
int class_vptr_offset(zan_symbol_t *sym) {
  if (!sym)
    return 0;
  if (sym->kind == SYM_STRUCT)
    return 0;
  if (decl_is_explicit_layout(sym))
    return 0;
  if (!g_vptr_plan_ready || sym->kind != SYM_CLASS)
    return 1;
  if (class_has_virtual_methods(sym))
    return 1;
  zan_symbol_t *root = class_layout_root(sym);
  if (!vptr_set_has(g_vptr_known, g_vptr_known_count, sym) ||
      !vptr_set_has(g_vptr_known, g_vptr_known_count, root))
    return 1;
  return vptr_set_has(g_vptr_poly_roots, g_vptr_poly_count, root) ? 1 : 0;
}
static inline size_t struct_sym_hash(const zan_symbol_t *sym) {
  uintptr_t x = (uintptr_t)sym;
  x ^= x >> 17;
  x *= (uintptr_t)0xbf58476d1ce4e5b9ULL;
  x ^= x >> 31;
  return (size_t)x;
}

static void struct_index_insert(zan_irgen_t *g, zan_symbol_t *sym, int idx) {
  if (!sym)
    return;
  if ((g->struct_type_count + 1) * 10 >= g->struct_type_index_cap * 7) {
    int ncap = g->struct_type_index_cap ? g->struct_type_index_cap * 2 : 64;
    struct zan_struct_index_slot *nt = (struct zan_struct_index_slot *)calloc(
        (size_t)ncap, sizeof(*nt));
    if (!nt)
      return;
    size_t mask = (size_t)(ncap - 1);
    for (int i = 0; i < g->struct_type_count; i++) {
      zan_symbol_t *s = g->struct_types[i].sym;
      if (!s)
        continue;
      size_t h = struct_sym_hash(s) & mask;
      while (nt[h].sym)
        h = (h + 1) & mask;
      nt[h].sym = s;
      nt[h].idx = i;
    }
    free(g->struct_type_index);
    g->struct_type_index = nt;
    g->struct_type_index_cap = ncap;
    return;
  }
  size_t mask = (size_t)(g->struct_type_index_cap - 1);
  size_t h = struct_sym_hash(sym) & mask;
  while (g->struct_type_index[h].sym && g->struct_type_index[h].sym != sym)
    h = (h + 1) & mask;
  g->struct_type_index[h].sym = sym;
  g->struct_type_index[h].idx = idx;
}

struct zan_struct_type_entry *get_struct_entry(zan_irgen_t *g,
                                               zan_symbol_t *sym) {
  if (!sym)
    return NULL;
  if (g->struct_type_index && g->struct_type_index_cap > 0) {
    size_t mask = (size_t)(g->struct_type_index_cap - 1);
    size_t h = struct_sym_hash(sym) & mask;
    while (g->struct_type_index[h].sym) {
      if (g->struct_type_index[h].sym == sym)
        return &g->struct_types[g->struct_type_index[h].idx];
      h = (h + 1) & mask;
    }
    return NULL;
  }
  for (int i = 0; i < g->struct_type_count; i++) {
    if (g->struct_types[i].sym == sym)
      return &g->struct_types[i];
  }
  return NULL;
}

LLVMTypeRef get_struct_llvm_type(zan_irgen_t *g, zan_symbol_t *sym) {
  struct zan_struct_type_entry *e = get_struct_entry(g, sym);
  return e ? e->llvm_type : NULL;
}

/* Address a field of an instance */
LLVMValueRef emit_field_ptr(zan_irgen_t *g, zan_symbol_t *type_sym,
                            LLVMTypeRef st, LLVMValueRef base, int fi,
                            const char *name) {
  struct zan_struct_type_entry *e =
      type_sym ? get_struct_entry(g, type_sym) : NULL;
  if (e && e->explicit_layout && fi >= 0 && fi < e->field_count) {
    LLVMValueRef off =
        LLVMConstInt(LLVMInt64TypeInContext(g->ctx), e->field_offsets[fi], 0);
    LLVMValueRef raw = LLVMBuildInBoundsGEP2(
        g->builder, LLVMInt8TypeInContext(g->ctx), base, &off, 1, "fld.off");
    LLVMTypeRef wrap =
        LLVMStructTypeInContext(g->ctx, &e->field_llvm[fi], 1, 0);
    return LLVMBuildStructGEP2(g->builder, wrap, raw, 0, name);
  }
  LLVMValueRef p =
      LLVMBuildStructGEP2(g->builder, st, base, (unsigned)fi, name);
  zan_tbaa_mark_field(g, p, st, fi);
  return p;
}
bool field_member_is_static(zan_symbol_t *m) {
  if (m->modifiers & MOD_STATIC)
    return true;
  if (m->kind == SYM_FIELD && m->decl &&
      (m->decl->field_decl.modifiers & MOD_STATIC))
    return true;
  return false;
}
int get_field_index(zan_symbol_t *type_sym, zan_istr_t field_name) {
  int idx = class_vptr_offset(type_sym);
  int found = -1;
  for (int i = 0; i < type_sym->member_count; i++) {
    if (type_sym->members[i]->kind == SYM_FIELD ||
        type_sym->members[i]->kind == SYM_PROPERTY) {
      if (field_member_is_static(type_sym->members[i]))
        continue;
      if (type_sym->members[i]->name.len == field_name.len &&
          memcmp(type_sym->members[i]->name.str, field_name.str,
                 field_name.len) == 0) {
        found = idx;
      }
      idx++;
    }
  }
  return found;
}
zan_symbol_t *get_field_sym(zan_symbol_t *type_sym, zan_istr_t field_name) {
  zan_class_index_t *ci = class_index_for(type_sym);
  zan_symbol_t *found = NULL;
  for (int i = member_first_named(ci, field_name); i >= 0;
       i = member_next_named(ci, i)) {
    zan_symbol_t *m = type_sym->members[i];
    if ((m->kind == SYM_FIELD || m->kind == SYM_PROPERTY) &&
        member_name_is(m, field_name)) {
      found = m;
    }
  }
  return found;
}
zan_symbol_t *get_method_sym(zan_symbol_t *type_sym, zan_istr_t method_name) {
  zan_class_index_t *ci = class_index_for(type_sym);
  for (int i = member_first_named(ci, method_name); i >= 0;
       i = member_next_named(ci, i)) {
    zan_symbol_t *m = type_sym->members[i];
    if (m->kind == SYM_METHOD && member_name_is(m, method_name))
      return m;
  }
  if (type_sym->type && type_sym->type->base_type &&
      type_sym->type->base_type->sym) {
    return get_method_sym(type_sym->type->base_type->sym, method_name);
  }
  if (type_sym->kind == SYM_INTERFACE && type_sym->type) {
    for (int i = 0; i < type_sym->type->interface_count; i++) {
      zan_type_t *it = type_sym->type->interfaces[i];
      if (!it || !it->sym || it->sym == type_sym)
        continue;
      zan_symbol_t *m = get_method_sym(it->sym, method_name);
      if (m)
        return m;
    }
  }
  return NULL;
}

/* 返回the symbol that was created for a specific method-declaration AST node */
zan_symbol_t *method_sym_for_decl(zan_symbol_t *type_sym,
                                  zan_ast_node_t *decl) {
  zan_class_index_t *ci = class_index_for(type_sym);
  for (int i = ci->decl_buckets[irgen_ptr_bucket(decl, ci->cap)]; i >= 0;
       i = ci->decl_next[i]) {
    zan_symbol_t *m = type_sym->members[i];
    if (m->kind == SYM_METHOD && m->decl == decl)
      return m;
  }
  return NULL;
}
int method_is_params_variadic(zan_symbol_t *m) {
  if (!m || !m->decl || m->decl->method_decl.params.count == 0)
    return 0;
  zan_ast_node_t *last =
      m->decl->method_decl.params.items[m->decl->method_decl.params.count - 1];
  return last->kind == AST_PARAM && last->param.is_params;
}
int method_accepts_arity(zan_symbol_t *m, int argc) {
  if (!m || !m->decl || m->decl->kind != AST_METHOD_DECL)
    return 0;
  int pc = m->decl->method_decl.params.count;
  if (pc == argc)
    return 1;
  if (m->decl->method_decl.is_variadic && argc > pc)
    return 1;
  bool params_tail = method_is_params_variadic(m) != 0;
  /* params 尾参本身可缺省（收 0..n 个实参）；它前面的可选形参按各自的
   * 默认值逐个可省——"(int a, int b = 10, params int[] rest)" 在 argc=1
   * 时合法（b 填默认、params 收空数组），此前被当成固定 arity 拒绝
   * （A381 params 组合清单）。 */
  int fixed = params_tail ? pc - 1 : pc;
  if (params_tail && argc >= fixed)
    return 1;
  if (argc < fixed) {
    for (int i = argc; i < fixed; i++) {
      zan_ast_node_t *p = m->decl->method_decl.params.items[i];
      if (!p || p->kind != AST_PARAM || !p->param.default_val)
        return 0;
    }
    return 1;
  }
  return 0;
}

static int method_type_param_count(zan_symbol_t *m) {
  if (!m || !m->decl || m->decl->kind != AST_METHOD_DECL)
    return 0;
  return m->decl->method_decl.type_params.count;
}
zan_symbol_t *resolve_overload(zan_symbol_t *type_sym, zan_istr_t name,
                               int argc, int type_arg_count) {
  int depth = 0;
  while (type_sym && depth++ < 512) {
    zan_symbol_t *variadic = NULL;
    zan_class_index_t *ci = class_index_for(type_sym);
    for (int i = member_first_named(ci, name); i >= 0;
         i = member_next_named(ci, i)) {
      zan_symbol_t *m = type_sym->members[i];
      if (m->kind != SYM_METHOD || !member_name_is(m, name))
        continue;
      if (type_arg_count > 0 && method_type_param_count(m) != type_arg_count)
        continue;
      if (m->decl && method_accepts_arity(m, argc)) {
        if (method_is_params_variadic(m)) {
          if (!variadic)
            variadic = m;
        } else {
          return m;
        }
      }
    }
    if (variadic)
      return variadic;
    if (!type_sym->type || !type_sym->type->base_type ||
        !type_sym->type->base_type->sym)
      break;
    type_sym = type_sym->type->base_type->sym;
  }
  return NULL;
}

static zan_symbol_t *resolve_iface_overload_depth(zan_symbol_t *iface,
                                                  zan_istr_t name, int argc,
                                                  int type_arg_count,
                                                  int depth) {
  if (!iface || depth > 512)
    return NULL;
  zan_symbol_t *m = resolve_overload(iface, name, argc, type_arg_count);
  if (m)
    return m;
  if (!iface->type)
    return NULL;
  for (int i = 0; i < iface->type->interface_count; i++) {
    zan_type_t *it = iface->type->interfaces[i];
    if (!it || !it->sym || it->sym == iface)
      continue;
    m = resolve_iface_overload_depth(it->sym, name, argc, type_arg_count,
                                     depth + 1);
    if (m)
      return m;
  }
  return NULL;
}
zan_symbol_t *resolve_iface_overload(zan_symbol_t *iface, zan_istr_t name,
                                     int argc, int type_arg_count) {
  return resolve_iface_overload_depth(iface, name, argc, type_arg_count, 0);
}

/* Defined in irgen_abi */
unsigned long abi_size_of(LLVMTypeRef t);
unsigned long abi_align_of(LLVMTypeRef t);
bool decl_is_explicit_layout(zan_symbol_t *sym) {
  return sym->decl &&
         (sym->decl->kind == AST_STRUCT_DECL ||
          sym->decl->kind == AST_CLASS_DECL) &&
         sym->decl->type_decl.is_explicit_layout;
}

static bool field_offset_attr(zan_symbol_t *field, unsigned long *out) {
  if (!field->decl)
    return false;
  zan_ast_list_t *attrs = zan_ast_attributes(field->decl);
  for (int i = 0; i < attrs->count; i++) {
    zan_ast_node_t *a = attrs->items[i];
    if (a->kind != AST_ATTRIBUTE || !a->attribute.name)
      continue;
    zan_istr_t n = a->attribute.name->ident.name;
    if (!n.str || n.len != 11 || memcmp(n.str, "FieldOffset", 11) != 0)
      continue;
    if (a->attribute.args.count < 1)
      continue;
    zan_ast_node_t *v = a->attribute.args.items[0];
    if (v->kind != AST_INT_LITERAL || v->int_val < 0)
      continue;
    *out = (unsigned long)v->int_val;
    return true;
  }
  return false;
}
zan_type_t *subst_type_param_deep(zan_irgen_t *g, zan_type_t *t,
                                  zan_type_t *recv);
unsigned long abi_size_of(LLVMTypeRef t);

static LLVMTypeRef generic_field_slot(zan_irgen_t *g, zan_symbol_t *sym,
                                      zan_type_t *ftype, LLVMTypeRef base) {
  if (!ftype || ftype->kind != TYPE_TYPE_PARAM)
    return base;
  unsigned long have = abi_size_of(base), need = have;
  for (int i = 0; i < g->generic_inst_count; i++) {
    if (g->generic_insts[i].type_sym != sym)
      continue;
    zan_type_t *ct = subst_type_param_deep(g, ftype, g->generic_insts[i].inst);
    if (!ct || ct == ftype)
      continue;
    unsigned long sz = abi_size_of(map_type(g, ct));
    if (sz > need)
      need = sz;
  }
  if (need <= have)
    return base;
  return LLVMArrayType(LLVMInt64TypeInContext(g->ctx),
                       (unsigned)((need + 7) / 8));
}
void register_struct_type(zan_irgen_t *g, zan_symbol_t *sym) {
  if (get_struct_llvm_type(g, sym))
    return;
  g->struct_types =
      irgen_grow(g->struct_types, &g->struct_type_cap, g->struct_type_count + 1,
                 sizeof(*g->struct_types));

  char name_buf[256];
  snprintf(name_buf, sizeof(name_buf), "struct.%.*s", (int)sym->name.len,
           sym->name.str);
  LLVMTypeRef st = LLVMStructCreateNamed(g->ctx, name_buf);
  memset(&g->struct_types[g->struct_type_count], 0, sizeof(*g->struct_types));
  g->struct_types[g->struct_type_count].sym = sym;
  g->struct_types[g->struct_type_count].llvm_type = st;
  g->struct_types[g->struct_type_count].base_struct_idx = -1;
  int my_index = g->struct_type_count++;
  struct_index_insert(g, sym, my_index);

  int field_count = 0;
  for (int i = 0; i < sym->member_count; i++) {
    if ((sym->members[i]->kind == SYM_FIELD ||
         sym->members[i]->kind == SYM_PROPERTY) &&
        !field_member_is_static(sym->members[i]))
      field_count++;
  }

  int vptr = class_vptr_offset(sym);
  LLVMTypeRef *field_types =
      (LLVMTypeRef *)calloc((size_t)(field_count + vptr), sizeof(LLVMTypeRef));
  int fi = 0;
  if (vptr) {
    field_types[fi++] = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
  }
  for (int i = 0; i < sym->member_count; i++) {
    if ((sym->members[i]->kind == SYM_FIELD ||
         sym->members[i]->kind == SYM_PROPERTY) &&
        !field_member_is_static(sym->members[i])) {
      field_types[fi++] = generic_field_slot(
          g, sym, sym->members[i]->type, map_type(g, sym->members[i]->type));
    }
  }

  struct zan_struct_type_entry *entry = &g->struct_types[my_index];
  entry->field_count = field_count + vptr;
  entry->field_llvm = field_types;
  entry->explicit_layout = decl_is_explicit_layout(sym);

  if (!entry->explicit_layout) {
    LLVMStructSetBody(st, field_types, (unsigned)(field_count + vptr), 0);
    return;
  }

  entry->field_offsets = (unsigned long *)calloc((size_t)(field_count + vptr),
                                                 sizeof(unsigned long));
  unsigned long size = 0, align = 1;
  int slot = vptr;
  if (class_has_virtual_methods(sym)) {
    zan_diag_emit_code(
        g->diag, DIAG_ERROR, sym->decl ? sym->decl->loc : zan_loc(0, 0, 0, 0),
        3093, "explicit-layout type '%.*s' cannot have virtual methods",
        (int)sym->name.len, sym->name.str);
  }
  for (int i = 0; i < sym->member_count; i++) {
    zan_symbol_t *m = sym->members[i];
    if ((m->kind != SYM_FIELD && m->kind != SYM_PROPERTY) ||
        field_member_is_static(m))
      continue;
    unsigned long off = 0;
    if (!field_offset_attr(m, &off)) {
      zan_diag_emit_code(
          g->diag, DIAG_ERROR, m->decl ? m->decl->loc : sym->decl->loc, 3094,
          "field '%.*s' of explicit-layout type '%.*s' needs [FieldOffset(n)]",
          (int)m->name.len, m->name.str, (int)sym->name.len, sym->name.str);
    }
    entry->field_offsets[slot] = off;
    unsigned long fa = abi_align_of(field_types[slot]);
    if (off % fa != 0) {
      zan_diag_emit_code(g->diag, DIAG_ERROR,
                         m->decl ? m->decl->loc : sym->decl->loc, 3095,
                         "[FieldOffset(%lu)] on '%.*s' is not %lu-byte aligned",
                         off, (int)m->name.len, m->name.str, fa);
    }
    if (fa > align)
      align = fa;
    unsigned long end = off + abi_size_of(field_types[slot]);
    if (end > size)
      size = end;
    slot++;
  }
  if (size % align)
    size += align - size % align;

  LLVMTypeRef body[2];
  unsigned nbody = 0;
  body[nbody++] = LLVMIntTypeInContext(g->ctx, (unsigned)(align * 8));
  if (size > align)
    body[nbody++] =
        LLVMArrayType(LLVMInt8TypeInContext(g->ctx), (unsigned)(size - align));
  LLVMStructSetBody(st, body, nbody, 0);
}
bool class_has_virtual_methods(zan_symbol_t *sym) {
  for (int i = 0; i < sym->member_count; i++) {
    if (sym->members[i]->kind == SYM_METHOD &&
        (sym->members[i]->modifiers &
         (MOD_VIRTUAL | MOD_OVERRIDE | MOD_ABSTRACT))) {
      return true;
    }
  }
  if (sym->type && sym->type->interface_count > 0)
    return true;
  if (sym->type && sym->type->base_type && sym->type->base_type->sym) {
    return class_has_virtual_methods(sym->type->base_type->sym);
  }
  return false;
}
int method_declared_param_count(zan_symbol_t *m) {
  if (!m || !m->decl || m->decl->kind != AST_METHOD_DECL)
    return -1;
  return m->decl->method_decl.params.count;
}
/* Keep runtime slot selection in step with the front-end contract check. */
bool virtual_method_signature_equal(zan_symbol_t *a, zan_symbol_t *b) {
  return zan_checker_virtual_signature_equal(a, b);
}
int get_virtual_method_index(zan_symbol_t *type_sym, zan_symbol_t *method_sym);
bool method_introduces_virtual_slot(zan_symbol_t *type_sym, zan_symbol_t *m) {
  if (!m || m->kind != SYM_METHOD)
    return false;
  if ((m->modifiers & (MOD_VIRTUAL | MOD_ABSTRACT)) &&
      !(m->modifiers & MOD_OVERRIDE)) {
    return true;
  }
  if (m->modifiers & MOD_OVERRIDE) {
    zan_symbol_t *base =
        (type_sym && type_sym->type && type_sym->type->base_type)
            ? type_sym->type->base_type->sym
            : NULL;
    if (!base || get_virtual_method_index(base, m) < 0) {
      return true;
    }
  }
  return false;
}
int count_virtual_methods(zan_symbol_t *sym) {
  int count = 0;
  if (sym->type && sym->type->base_type && sym->type->base_type->sym) {
    count = count_virtual_methods(sym->type->base_type->sym);
  }
  for (int i = 0; i < sym->member_count; i++) {
    if (method_introduces_virtual_slot(sym, sym->members[i])) {
      count++;
    }
  }
  return count;
}
int get_virtual_method_index(zan_symbol_t *type_sym, zan_symbol_t *method_sym) {
  if (!method_sym)
    return -1;
  if (type_sym->type && type_sym->type->base_type &&
      type_sym->type->base_type->sym) {
    int idx =
        get_virtual_method_index(type_sym->type->base_type->sym, method_sym);
    if (idx >= 0)
      return idx;
  }
  int base_count = 0;
  if (type_sym->type && type_sym->type->base_type &&
      type_sym->type->base_type->sym) {
    base_count = count_virtual_methods(type_sym->type->base_type->sym);
  }
  int idx = base_count;
  for (int i = 0; i < type_sym->member_count; i++) {
    zan_symbol_t *m = type_sym->members[i];
    if (method_introduces_virtual_slot(type_sym, m)) {
      if (virtual_method_signature_equal(m, method_sym)) {
        return idx;
      }
      idx++;
    }
  }
  return -1;
}
void local_scope_init(local_scope_t *s, zan_arena_t *arena) {
  s->count = 0;
  s->cap = INITIAL_LOCALS;
  s->arena = arena;
  s->vars = (local_var_t *)zan_arena_alloc(arena, sizeof(local_var_t) *
                                                      (size_t)s->cap);
  s->pattern_count = 0;
  s->pattern_cap = 0;
  s->patterns = NULL;
  s->arg_owners = NULL;
}
local_scope_t *local_scope_new(zan_arena_t *arena) {
  local_scope_t *s =
      (local_scope_t *)zan_arena_alloc(arena, sizeof(local_scope_t));
  local_scope_init(s, arena);
  return s;
}
LLVMTypeRef map_type(zan_irgen_t *g, zan_type_t *type);
LLVMTypeRef local_slot_type(zan_irgen_t *g, local_var_t *v) {
  if (LLVMIsAAllocaInst(v->alloca))
    return LLVMGetAllocatedType(v->alloca);
  return map_type(g, v->type);
}

unsigned g_local_gen = 0;
void local_add(local_scope_t *scope, zan_istr_t name, LLVMValueRef alloca,
               zan_type_t *type) {
  g_local_gen++;
  if (scope->count >= scope->cap) {
    int new_cap = scope->cap > 0 ? scope->cap * 2 : INITIAL_LOCALS;
    local_var_t *grown = (local_var_t *)zan_arena_alloc(
        scope->arena, sizeof(local_var_t) * (size_t)new_cap);
    if (scope->count > 0 && scope->vars) {
      memcpy(grown, scope->vars, sizeof(local_var_t) * (size_t)scope->count);
    }
    scope->vars = grown;
    scope->cap = new_cap;
  }
  scope->vars[scope->count].name = name;
  scope->vars[scope->count].alloca = alloca;
  scope->vars[scope->count].type = type;
  scope->vars[scope->count].arc_owned = 0;
  scope->vars[scope->count].byref_slot = 0;
  scope->vars[scope->count].eh_slot = 0;
  scope->vars[scope->count].arr_len_slot = NULL;
  scope->vars[scope->count].box_cell = NULL;
  scope->vars[scope->count].box_owner_slot = NULL;
  scope->vars[scope->count].box_owned = 0;
  scope->vars[scope->count].opaque_string = 0;
  scope->vars[scope->count].obj_rc_flag = NULL;
  scope->vars[scope->count].binding_decl = NULL;
  scope->vars[scope->count].async_decl = NULL;
  scope->vars[scope->count].async_role = 0;
  scope->vars[scope->count].frame_owner = -1;
  scope->vars[scope->count].struct_rc = 0;
  scope->vars[scope->count].per_iteration = 0;
  scope->count++;
  di_declare_var(g_di_emit_ctx, name, alloca, type);
}
LLVMValueRef emit_entry_alloca(zan_irgen_t *g, LLVMTypeRef ty,
                               const char *name) {
  zan_builder_point_t pt = zan_builder_save(g);
  LLVMValueRef fn = pt.bb ? LLVMGetBasicBlockParent(pt.bb) : g->current_fn;
  LLVMBasicBlockRef entry = LLVMGetEntryBasicBlock(fn);
  LLVMValueRef term = LLVMGetBasicBlockTerminator(entry);
  if (term)
    LLVMPositionBuilderBefore(g->builder, term);
  else
    LLVMPositionBuilderAtEnd(g->builder, entry);
  LLVMValueRef alloca = LLVMBuildAlloca(g->builder, ty, name);
  if (LLVMGetTypeKind(ty) == LLVMPointerTypeKind)
    LLVMBuildStore(g->builder, LLVMConstNull(ty), alloca);
  else if (LLVMGetTypeKind(ty) == LLVMStructTypeKind)
    LLVMBuildStore(g->builder, LLVMConstNull(ty), alloca);
  zan_builder_restore(g, pt);
  return alloca;
}
local_var_t *local_find(local_scope_t *scope, zan_istr_t name) {
  for (int i = scope->count - 1; i >= 0; i--) {
    if (scope->vars[i].name.len == name.len &&
        memcmp(scope->vars[i].name.str, name.str, (size_t)name.len) == 0) {
      return &scope->vars[i];
    }
  }
  return NULL;
}
local_var_t *local_find_async_role(local_scope_t *scope, zan_ast_node_t *decl,
                                   int role) {
  if (!decl)
    return NULL;
  for (int i = scope->count - 1; i >= 0; i--) {
    if (scope->vars[i].async_decl == decl && scope->vars[i].async_role == role)
      return &scope->vars[i];
  }
  return NULL;
}
local_var_t *local_find_async_decl(local_scope_t *scope, zan_ast_node_t *decl) {
  return local_find_async_role(scope, decl, ASYNC_LOCAL_VALUE);
}
local_var_t *local_find_binding_decl(local_scope_t *scope,
                                     zan_ast_node_t *decl) {
  if (!scope || !decl)
    return NULL;
  for (int i = scope->count - 1; i >= 0; i--) {
    if (scope->vars[i].binding_decl == decl)
      return &scope->vars[i];
  }
  return NULL;
}
pattern_binding_t *local_find_pattern_binding(local_scope_t *scope,
                                              zan_ast_node_t *decl) {
  if (!scope || !decl)
    return NULL;
  for (int i = scope->pattern_count - 1; i >= 0; i--)
    if (scope->patterns[i].decl == decl)
      return &scope->patterns[i];
  return NULL;
}
pattern_binding_t *local_add_pattern_binding(local_scope_t *scope,
                                             zan_ast_node_t *decl) {
  if (!scope || !decl)
    return NULL;
  if (scope->pattern_count >= scope->pattern_cap) {
    int nc = scope->pattern_cap ? scope->pattern_cap * 2 : 16;
    pattern_binding_t *np = (pattern_binding_t *)zan_arena_alloc(
        scope->arena, sizeof(pattern_binding_t) * (size_t)nc);
    if (scope->pattern_count)
      memcpy(np, scope->patterns,
             sizeof(pattern_binding_t) * (size_t)scope->pattern_count);
    scope->patterns = np;
    scope->pattern_cap = nc;
  }
  pattern_binding_t *p = &scope->patterns[scope->pattern_count++];
  memset(p, 0, sizeof(*p));
  p->decl = decl;
  return p;
}
int type_named(zan_type_t *t, const char *n, int len) {
  return t && t->kind != TYPE_ARRAY && t->name.str && (int)t->name.len == len &&
         memcmp(t->name.str, n, (size_t)len) == 0;
}
int is_builtin_collection_type(zan_type_t *t) {
  if (!t || t->kind != TYPE_CLASS)
    return 0;
  zan_istr_t n = t->name;
  return (n.len == 4 && memcmp(n.str, "List", 4) == 0) ||
         (n.len == 4 && memcmp(n.str, "Dict", 4) == 0) ||
         (n.len == 13 && memcmp(n.str, "StringBuilder", 13) == 0);
}

static int is_rc_collection_type(zan_type_t *t) {
  if (!t || t->kind != TYPE_CLASS)
    return 0;
  zan_istr_t n = t->name;
  return (n.len == 4 && memcmp(n.str, "List", 4) == 0) ||
         (n.len == 4 && memcmp(n.str, "Dict", 4) == 0) ||
         (n.len == 13 && memcmp(n.str, "StringBuilder", 13) == 0);
}
int is_arc_managed_type(zan_type_t *t) {
  if (!t)
    return 0;
  if (t->kind == TYPE_INTERFACE)
    return 1;
  if (t->kind != TYPE_CLASS)
    return 0;
  if (is_builtin_collection_type(t))
    return is_rc_collection_type(t);
  return 1;
}
int is_rc_managed_type(zan_type_t *t) {
  return t && (t->kind == TYPE_STRING || t->kind == TYPE_DELEGATE ||
               t->kind == TYPE_ARRAY || t->kind == TYPE_OBJECT ||
               is_arc_managed_type(t));
}
void emit_rc_release_for_type(zan_irgen_t *g, zan_type_t *type, LLVMValueRef v);
int expr_yields_owned_rc_value(zan_irgen_t *g, zan_ast_node_t *e,
                               local_scope_t *locals);
void emit_rc_retain_for_type(zan_irgen_t *g, zan_type_t *type, LLVMValueRef v);
void emit_arc_release_typed(zan_irgen_t *g, zan_type_t *type, LLVMValueRef v);
LLVMValueRef get_class_release_decl(zan_irgen_t *g, zan_symbol_t *sym,
                                    zan_type_t *inst);
void emit_list_release_elems(zan_irgen_t *g, zan_type_t *elem_type,
                             LLVMValueRef col);
void emit_dict_release_elems(zan_irgen_t *g, zan_type_t *dict_type,
                             LLVMValueRef col);
void emit_array_release_elems(zan_irgen_t *g, zan_type_t *elem_type,
                              LLVMValueRef arr, LLVMValueRef len);
void emit_release_obj_value(zan_irgen_t *g, LLVMValueRef cur);
void emit_release_obj_local(zan_irgen_t *g, local_var_t *v);
void emit_struct_local_release(zan_irgen_t *g, zan_type_t *type,
                               LLVMValueRef slot);
LLVMValueRef zan_store_fit(zan_irgen_t *g, LLVMValueRef val, LLVMValueRef ptr);
void release_all_arc_locals(zan_irgen_t *g, local_scope_t *locals) {
  for (int i = 0; i < locals->count; i++) {
    if (locals->vars[i].box_cell)
      continue;
    if (locals->vars[i].obj_rc_flag) {
      emit_release_obj_local(g, &locals->vars[i]);
    } else if (locals->vars[i].struct_rc) {
      emit_struct_local_release(g, locals->vars[i].type,
                                locals->vars[i].alloca);
    } else if (is_rc_managed_type(locals->vars[i].type)) {
      LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
      LLVMValueRef val = LLVMBuildLoad2(g->builder, i8ptr,
                                        locals->vars[i].alloca, "arc_cleanup");
      emit_rc_release_for_type(g, locals->vars[i].type, val);
    }
  }
}

unsigned llvm_scalar_size(LLVMTypeRef t) {
  switch (LLVMGetTypeKind(t)) {
  case LLVMIntegerTypeKind:
    return (LLVMGetIntTypeWidth(t) + 7) / 8;
  case LLVMPointerTypeKind:
    return 8;
  case LLVMFloatTypeKind:
    return 4;
  case LLVMDoubleTypeKind:
    return 8;
  case LLVMStructTypeKind: {
    unsigned total = 0;
    unsigned n = LLVMCountStructElementTypes(t);
    for (unsigned i = 0; i < n; i++) {
      unsigned fs = llvm_scalar_size(LLVMStructGetTypeAtIndex(t, i));
      if (!fs)
        return 0;
      if (fs > 1 && total % fs)
        total += fs - (total % fs);
      total += fs;
    }
    return total;
  }
  default:
    return 0;
  }
}
unsigned elem_slot_words(zan_irgen_t *g, zan_type_t *elem) {
  if (!elem || (elem->kind != TYPE_STRUCT && elem->kind != TYPE_DECIMAL &&
                elem->kind != TYPE_NULLABLE))
    return 1;
  LLVMTypeRef st = map_type(g, elem);
  if (!st || LLVMGetTypeKind(st) != LLVMStructTypeKind)
    return 1;
  unsigned sz = llvm_scalar_size(st);
  if (!sz)
    return 1;
  return (sz + 7) / 8;
}
LLVMValueRef slot_word_index(zan_irgen_t *g, LLVMValueRef idx, unsigned words) {
  if (words <= 1)
    return idx;
  return zan_mul(g->builder, idx, LLVMConstInt(LLVMTypeOf(idx), words, 0),
                 "slot.wi");
}

static void check_struct_fits_slot(zan_irgen_t *g, LLVMTypeRef st,
                                   zan_ast_node_t *at) {
  if (llvm_scalar_size(st))
    return;
  zan_loc_t loc;
  memset(&loc, 0, sizeof(loc));
  if (at)
    loc = at->loc;
  zan_diag_emit_code(
      g->diag, DIAG_ERROR, loc, 3096,
      "this value struct cannot be a collection element: the slot allocator "
      "cannot compute its layout; use a class instead");
}
void store_struct_in_slot(zan_irgen_t *g, LLVMValueRef v, LLVMValueRef slot_ptr,
                          zan_ast_node_t *at) {
  LLVMTypeRef st = LLVMTypeOf(v);
  check_struct_fits_slot(g, st, at);
  LLVMValueRef sp =
      LLVMBuildBitCast(g->builder, slot_ptr, LLVMPointerType(st, 0), "slot.sp");
  LLVMBuildStore(g->builder, v, sp);
}
LLVMValueRef load_struct_from_slot(zan_irgen_t *g, LLVMValueRef slot_ptr,
                                   LLVMTypeRef st) {
  LLVMValueRef sp =
      LLVMBuildBitCast(g->builder, slot_ptr, LLVMPointerType(st, 0), "slot.lp");
  return LLVMBuildLoad2(g->builder, st, sp, "slot.struct");
}
LLVMValueRef extend_int_for_slot(zan_irgen_t *g, LLVMValueRef v,
                                 zan_type_t *elem_type, LLVMTypeRef slot_ty) {
  int uns = 0;
  if (elem_type) {
    switch (elem_type->kind) {
    case TYPE_BOOL:
    case TYPE_BYTE:
    case TYPE_USHORT:
    case TYPE_UINT:
    case TYPE_ULONG:
    case TYPE_CHAR:
      uns = 1;
      break;
    default:
      break;
    }
  }
  return uns ? LLVMBuildZExt(g->builder, v, slot_ty, "slot.zx")
             : LLVMBuildSExt(g->builder, v, slot_ty, "slot.sx");
}
void emit_collection_slot_store(zan_irgen_t *g, zan_type_t *elem_type,
                                LLVMTypeRef slot_ty, LLVMValueRef slot_ptr,
                                LLVMValueRef value, zan_ast_node_t *rhs,
                                local_scope_t *locals, int overwrite_old);
void emit_collection_release_raw_slot(zan_irgen_t *g, zan_type_t *elem_type,
                                      LLVMValueRef raw, LLVMTypeRef slot_ty);
LLVMValueRef get_calloc_fn(zan_irgen_t *g) {
  LLVMValueRef f = LLVMGetNamedFunction(g->mod, "calloc");
  if (!f) {
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef ft = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){i64, i64}, 2, 0);
    f = LLVMAddFunction(g->mod, "calloc", ft);
    unsigned na = LLVMGetEnumAttributeKindForName("noalias", 7);
    if (na)
      LLVMAddAttributeAtIndex(f, LLVMAttributeReturnIndex,
                              LLVMCreateEnumAttribute(g->ctx, na, 0));
  }
  return f;
}
LLVMValueRef emit_alloc_rc_collection(zan_irgen_t *g, zan_ast_node_t *expr,
                                      long size, int coll_kind,
                                      zan_type_t *elem_type) {
  LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
  if (g->cur_inst)
    elem_type = subst_type_param_deep(g, elem_type, g->cur_inst);
  LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
  LLVMValueRef site_arg_val = NULL;
  if (g->cur_canon_body && g->cur_gctx_val && elem_type &&
      type_mentions_canon(elem_type) && g->desc_hdr) {
    LLVMValueRef dptr = emit_gctx_load_slot(
        g, GCTX_SLOT_ARC_DESC, NULL, elem_type, NULL, 0, 0, coll_kind);
    if (dptr)
      site_arg_val = LLVMBuildPtrToInt(g->builder, dptr, i64, "coll.desc");
  }
  int site_idx = site_arg_val
                     ? -1
                     : reserve_arc_site(g, NULL, NULL, coll_kind, elem_type);
  LLVMValueRef site_name = LLVMConstNull(i8ptr);
  if (g->check_leaks) {
    char site_buf[600];
    const char *sfile = loc_site_file(g, expr->loc);
    const char *knm = (coll_kind == 2)   ? "StringBuilder"
                      : (coll_kind == 3) ? "Dictionary"
                                         : "List";
    int elen = (elem_type && elem_type->name.len) ? elem_type->name.len : 1;
    const char *estr =
        (elem_type && elem_type->name.len) ? elem_type->name.str : "?";
    snprintf(site_buf, sizeof(site_buf), "%s:%u:%u [%s<%.*s>]", sfile,
             expr->loc.line, expr->loc.col, knm, elen, estr);
    site_name = zan_irgen_intern_string(g, site_buf);
  }
  LLVMTypeRef alloc_fn_type =
      LLVMFunctionType(i8ptr, (LLVMTypeRef[]){i64, i64, i8ptr}, 3, 0);
  LLVMValueRef args[] = {
      LLVMConstInt(i64, (unsigned long long)size, 0),
      site_arg_val ? site_arg_val : arc_site_arg(g, site_idx), site_name};
  return zan_call2(g->builder, alloc_fn_type, g->rt_alloc, args, 3, "coll");
}
int eh_slot_kind_of(zan_type_t *t) {
  if (!t)
    return ZAN_EH_SLOT_OBJ;
  if (t->kind == TYPE_STRING)
    return ZAN_EH_SLOT_STR;
  if (t->kind == TYPE_DELEGATE)
    return ZAN_EH_SLOT_DLG;
  if (t->kind == TYPE_ARRAY)
    return ZAN_EH_SLOT_ARR;
  return ZAN_EH_SLOT_OBJ;
}

/* 200B 双十进制数字查找表 ("000102...99")：每次迭代 /100 出两位，
 * 将整数格式化除法次数减半并与 LLVM 魔数乘法优化配合。 */
static LLVMValueRef get_digits200_table(zan_irgen_t *g) {
  LLVMValueRef tbl = LLVMGetNamedGlobal(g->mod, "__zan.digits200");
  if (tbl)
    return tbl;
  static const char k_digits200[200] =
      "0001020304050607080910111213141516171819"
      "2021222324252627282930313233343536373839"
      "4041424344454647484950515253545556575859"
      "6061626364656667686970717273747576777879"
      "8081828384858687888990919293949596979899";
  LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
  LLVMTypeRef ty = LLVMArrayType(i8, 200);
  tbl = LLVMAddGlobal(g->mod, ty, "__zan.digits200");
  LLVMSetLinkage(tbl, LLVMPrivateLinkage);
  LLVMSetUnnamedAddr(tbl, LLVMGlobalUnnamedAddr);
  LLVMSetGlobalConstant(tbl, 1);
  LLVMSetInitializer(tbl, LLVMConstStringInContext(g->ctx, k_digits200, 200, 1));
  return tbl;
}

/* 在调用方 24B 栈缓冲 tmp24 末尾反向写入十进制数字（含负号），返回 [start, len]。 */
static void emit_itoa64_digits_into_tmp(zan_irgen_t *g, LLVMValueRef fn,
                                        LLVMValueRef tmp24, LLVMValueRef v,
                                        LLVMValueRef uns,
                                        LLVMValueRef *out_start,
                                        LLVMValueRef *out_len) {
  LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
  LLVMTypeRef i16 = LLVMInt16TypeInContext(g->ctx);
  LLVMTypeRef i16p = LLVMPointerType(i16, 0);
  LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
  LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
  LLVMTypeRef d200_ty = LLVMArrayType(i8, 200);
  LLVMValueRef d200 = get_digits200_table(g);

  LLVMValueRef zero64 = LLVMConstInt(i64, 0, 0);
  LLVMValueRef one = LLVMConstInt(i64, 1, 0);
  LLVMValueRef two = LLVMConstInt(i64, 2, 0);
  LLVMValueRef ten = LLVMConstInt(i64, 10, 0);
  LLVMValueRef hundred = LLVMConstInt(i64, 100, 0);
  LLVMValueRef cap = LLVMConstInt(i64, 24, 0);

  LLVMBasicBlockRef cur_bb = LLVMGetInsertBlock(g->builder);
  LLVMBasicBlockRef loop2 = LLVMAppendBasicBlockInContext(g->ctx, fn, "itoa.l2");
  LLVMBasicBlockRef tail = LLVMAppendBasicBlockInContext(g->ctx, fn, "itoa.tail");
  LLVMBasicBlockRef two_d = LLVMAppendBasicBlockInContext(g->ctx, fn, "itoa.d2");
  LLVMBasicBlockRef one_d = LLVMAppendBasicBlockInContext(g->ctx, fn, "itoa.d1");
  LLVMBasicBlockRef sign_chk =
      LLVMAppendBasicBlockInContext(g->ctx, fn, "itoa.schk");
  LLVMBasicBlockRef sign_bb =
      LLVMAppendBasicBlockInContext(g->ctx, fn, "itoa.sign");
  LLVMBasicBlockRef done = LLVMAppendBasicBlockInContext(g->ctx, fn, "itoa.done");

  LLVMValueRef is_neg = LLVMBuildAnd(
      g->builder, LLVMBuildICmp(g->builder, LLVMIntSLT, v, zero64, "itoa.slt"),
      LLVMBuildICmp(g->builder, LLVMIntEQ, uns, LLVMConstInt(i32, 0, 0),
                    "itoa.signed"),
      "itoa.neg");
  LLVMValueRef mag =
      LLVMBuildSelect(g->builder, is_neg,
                      LLVMBuildNeg(g->builder, v, "itoa.negv"), v, "itoa.mag");
  LLVMBuildCondBr(g->builder,
                  LLVMBuildICmp(g->builder, LLVMIntUGE, mag, hundred, "itoa.ge100"),
                  loop2, tail);

  /* 双位循环：rest >= 100 时每次 UDiv/URem 100 写 2 字节 */
  LLVMPositionBuilderAtEnd(g->builder, loop2);
  LLVMValueRef rest = LLVMBuildPhi(g->builder, i64, "itoa.rest");
  LLVMValueRef pos = LLVMBuildPhi(g->builder, i64, "itoa.pos");
  LLVMValueRef q = LLVMBuildUDiv(g->builder, rest, hundred, "itoa.q");
  LLVMValueRef r = LLVMBuildURem(g->builder, rest, hundred, "itoa.r");
  LLVMValueRef r2 = LLVMBuildShl(g->builder, r, one, "itoa.r2");
  LLVMValueRef d2_src = LLVMBuildInBoundsGEP2(
      g->builder, d200_ty, d200, (LLVMValueRef[]){zero64, r2}, 2, "itoa.s2");
  LLVMValueRef pair = LLVMBuildLoad2(
      g->builder, i16, LLVMBuildBitCast(g->builder, d2_src, i16p, "itoa.s2p"),
      "itoa.pair");
  LLVMSetAlignment(pair, 1);
  LLVMValueRef next_pos = LLVMBuildSub(g->builder, pos, two, "itoa.pos.n");
  LLVMValueRef d2_dst =
      LLVMBuildInBoundsGEP2(g->builder, i8, tmp24, &next_pos, 1, "itoa.d2");
  LLVMValueRef st2 = LLVMBuildStore(
      g->builder, pair, LLVMBuildBitCast(g->builder, d2_dst, i16p, "itoa.d2p"));
  LLVMSetAlignment(st2, 1);
  LLVMAddIncoming(rest, (LLVMValueRef[]){mag, q},
                  (LLVMBasicBlockRef[]){cur_bb, loop2}, 2);
  LLVMAddIncoming(pos, (LLVMValueRef[]){cap, next_pos},
                  (LLVMBasicBlockRef[]){cur_bb, loop2}, 2);
  LLVMBuildCondBr(g->builder,
                  LLVMBuildICmp(g->builder, LLVMIntUGE, q, hundred, "itoa.more"),
                  loop2, tail);

  /* 尾数 (0..99)：>=10 写 2 字节，否则写 1 字节 */
  LLVMPositionBuilderAtEnd(g->builder, tail);
  LLVMValueRef rem_v = LLVMBuildPhi(g->builder, i64, "itoa.rv");
  LLVMValueRef rem_pos = LLVMBuildPhi(g->builder, i64, "itoa.rp");
  LLVMAddIncoming(rem_v, (LLVMValueRef[]){mag, q},
                  (LLVMBasicBlockRef[]){cur_bb, loop2}, 2);
  LLVMAddIncoming(rem_pos, (LLVMValueRef[]){cap, next_pos},
                  (LLVMBasicBlockRef[]){cur_bb, loop2}, 2);
  LLVMBuildCondBr(g->builder,
                  LLVMBuildICmp(g->builder, LLVMIntUGE, rem_v, ten, "itoa.ge10"),
                  two_d, one_d);

  LLVMPositionBuilderAtEnd(g->builder, two_d);
  LLVMValueRef tr2 = LLVMBuildShl(g->builder, rem_v, one, "itoa.tr2");
  LLVMValueRef ts2 = LLVMBuildInBoundsGEP2(
      g->builder, d200_ty, d200, (LLVMValueRef[]){zero64, tr2}, 2, "itoa.ts2");
  LLVMValueRef tpair = LLVMBuildLoad2(
      g->builder, i16, LLVMBuildBitCast(g->builder, ts2, i16p, "itoa.ts2p"),
      "itoa.tpair");
  LLVMSetAlignment(tpair, 1);
  LLVMValueRef p2 = LLVMBuildSub(g->builder, rem_pos, two, "itoa.p2");
  LLVMValueRef td2 =
      LLVMBuildInBoundsGEP2(g->builder, i8, tmp24, &p2, 1, "itoa.td2");
  LLVMValueRef tst2 = LLVMBuildStore(
      g->builder, tpair, LLVMBuildBitCast(g->builder, td2, i16p, "itoa.td2p"));
  LLVMSetAlignment(tst2, 1);
  LLVMBuildBr(g->builder, sign_chk);

  LLVMPositionBuilderAtEnd(g->builder, one_d);
  LLVMValueRef p1 = LLVMBuildSub(g->builder, rem_pos, one, "itoa.p1");
  LLVMValueRef ch1 = LLVMBuildTrunc(
      g->builder,
      LLVMBuildAdd(g->builder, rem_v, LLVMConstInt(i64, '0', 0), "itoa.dc1"), i8,
      "itoa.ch1");
  LLVMBuildStore(
      g->builder, ch1,
      LLVMBuildInBoundsGEP2(g->builder, i8, tmp24, &p1, 1, "itoa.dp1"));
  LLVMBuildBr(g->builder, sign_chk);

  LLVMPositionBuilderAtEnd(g->builder, sign_chk);
  LLVMValueRef dpos = LLVMBuildPhi(g->builder, i64, "itoa.dpos");
  LLVMAddIncoming(dpos, (LLVMValueRef[]){p2, p1},
                  (LLVMBasicBlockRef[]){two_d, one_d}, 2);
  LLVMBuildCondBr(g->builder, is_neg, sign_bb, done);

  LLVMPositionBuilderAtEnd(g->builder, sign_bb);
  LLVMValueRef spos = LLVMBuildSub(g->builder, dpos, one, "itoa.sp");
  LLVMBuildStore(
      g->builder, LLVMConstInt(i8, '-', 0),
      LLVMBuildInBoundsGEP2(g->builder, i8, tmp24, &spos, 1, "itoa.spp"));
  LLVMBuildBr(g->builder, done);

  LLVMPositionBuilderAtEnd(g->builder, done);
  LLVMValueRef start = LLVMBuildPhi(g->builder, i64, "itoa.start");
  LLVMAddIncoming(start, (LLVMValueRef[]){dpos, spos},
                  (LLVMBasicBlockRef[]){sign_chk, sign_bb}, 2);
  *out_start = start;
  *out_len = LLVMBuildSub(g->builder, cap, start, "itoa.len");
}

static LLVMValueRef get_itoa64_fn(zan_irgen_t *g) {
  if (g->fn_itoa64)
    return g->fn_itoa64;
  LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
  LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
  LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
  LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
  LLVMTypeRef fn_ty =
      LLVMFunctionType(i64, (LLVMTypeRef[]){i8ptr, i64, i32}, 3, 0);
  LLVMValueRef fn = LLVMAddFunction(g->mod, "__zan_itoa64", fn_ty);
  LLVMSetLinkage(fn, LLVMInternalLinkage);
  {
    unsigned nw = LLVMGetEnumAttributeKindForName("nounwind", 8);
    if (nw)
      LLVMAddAttributeAtIndex(fn, (LLVMAttributeIndex)LLVMAttributeFunctionIndex,
                              LLVMCreateEnumAttribute(g->ctx, nw, 0));
    unsigned ih = LLVMGetEnumAttributeKindForName("inlinehint", 10);
    if (ih)
      LLVMAddAttributeAtIndex(fn, (LLVMAttributeIndex)LLVMAttributeFunctionIndex,
                              LLVMCreateEnumAttribute(g->ctx, ih, 0));
  }
  g->fn_itoa64 = fn;

  LLVMBasicBlockRef save = LLVMGetInsertBlock(g->builder);
  LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
  LLVMBasicBlockRef fast1 = LLVMAppendBasicBlockInContext(g->ctx, fn, "fast1");
  LLVMBasicBlockRef full = LLVMAppendBasicBlockInContext(g->ctx, fn, "full");

  LLVMValueRef buf = LLVMGetParam(fn, 0);
  LLVMValueRef v = LLVMGetParam(fn, 1);
  LLVMValueRef uns = LLVMGetParam(fn, 2);
  LLVMValueRef one = LLVMConstInt(i64, 1, 0);
  LLVMValueRef cap = LLVMConstInt(i64, 24, 0);

  LLVMPositionBuilderAtEnd(g->builder, entry);
  /* 0..9 单数字快路径（无符号比较对 signed/unsigned 均成立） */
  LLVMBuildCondBr(
      g->builder,
      LLVMBuildICmp(g->builder, LLVMIntULT, v, LLVMConstInt(i64, 10, 0),
                    "itoa.lt10"),
      fast1, full);

  LLVMPositionBuilderAtEnd(g->builder, fast1);
  LLVMValueRef d0 = LLVMBuildTrunc(
      g->builder,
      LLVMBuildAdd(g->builder, v, LLVMConstInt(i64, '0', 0), "itoa.f0"), i8,
      "itoa.fc");
  LLVMBuildStore(g->builder, d0, buf);
  LLVMBuildStore(g->builder, LLVMConstInt(i8, 0, 0),
                 LLVMBuildInBoundsGEP2(g->builder, i8, buf, &one, 1, "itoa.fn"));
  LLVMBuildRet(g->builder, one);

  LLVMPositionBuilderAtEnd(g->builder, full);
  LLVMValueRef tmp = LLVMBuildArrayAlloca(g->builder, i8, cap, "itoa.tmp");
  LLVMValueRef start = NULL, len = NULL;
  emit_itoa64_digits_into_tmp(g, fn, tmp, v, uns, &start, &len);
  LLVMValueRef src =
      LLVMBuildInBoundsGEP2(g->builder, i8, tmp, &start, 1, "itoa.src");
  LLVMTypeRef memcpy_ty =
      LLVMFunctionType(i8ptr, (LLVMTypeRef[]){i8ptr, i8ptr, i64}, 3, 0);
  LLVMValueRef memcpy_fn = LLVMGetNamedFunction(g->mod, "memcpy");
  if (!memcpy_fn)
    memcpy_fn = LLVMAddFunction(g->mod, "memcpy", memcpy_ty);
  zan_call2(g->builder, memcpy_ty, memcpy_fn, (LLVMValueRef[]){buf, src, len},
            3, "");
  LLVMBuildStore(
      g->builder, LLVMConstInt(i8, 0, 0),
      LLVMBuildInBoundsGEP2(g->builder, i8, buf, &len, 1, "itoa.end"));
  LLVMBuildRet(g->builder, len);

  if (save)
    LLVMPositionBuilderAtEnd(g->builder, save);
  return fn;
}
LLVMValueRef emit_itoa_into(zan_irgen_t *g, LLVMValueRef buf, LLVMValueRef v,
                            int is_unsigned) {
  LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
  LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
  LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
  LLVMValueRef fn = get_itoa64_fn(g);
  LLVMTypeRef fn_ty =
      LLVMFunctionType(i64, (LLVMTypeRef[]){i8ptr, i64, i32}, 3, 0);
  LLVMValueRef args[] = {buf, v, LLVMConstInt(i32, is_unsigned ? 1 : 0, 0)};
  return zan_call2(g->builder, fn_ty, fn, args, 3, "itoa");
}
/* 整数 → 托管串：0..9 直接返回不朽 ASCII 表（零堆分配），其余直接从
 * digits200 栈缓冲单次 memcpy 到精确分配的托管串（消除双拷贝 + 函数调用）。 */
LLVMValueRef emit_itoa_string(zan_irgen_t *g, LLVMValueRef v, int is_unsigned) {
    LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
    LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef fn_ty = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i64, i32 }, 2, 0);
    LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "__zan_itoa_str");
    if (!fn) {
        fn = LLVMAddFunction(g->mod, "__zan_itoa_str", fn_ty);
        LLVMSetLinkage(fn, LLVMInternalLinkage);
        {
            unsigned nw = LLVMGetEnumAttributeKindForName("nounwind", 8);
            if (nw)
                LLVMAddAttributeAtIndex(fn, (LLVMAttributeIndex)LLVMAttributeFunctionIndex,
                                        LLVMCreateEnumAttribute(g->ctx, nw, 0));
        }
        LLVMBasicBlockRef save = LLVMGetInsertBlock(g->builder);
        LLVMValueRef saved_fn = g->current_fn;
        g->current_fn = fn;
        LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
        LLVMBasicBlockRef fast1 = LLVMAppendBasicBlockInContext(g->ctx, fn, "fast1");
        LLVMBasicBlockRef full = LLVMAppendBasicBlockInContext(g->ctx, fn, "full");

        LLVMValueRef pv = LLVMGetParam(fn, 0);
        LLVMValueRef puns = LLVMGetParam(fn, 1);

        LLVMPositionBuilderAtEnd(g->builder, entry);
        LLVMBuildCondBr(g->builder,
            LLVMBuildICmp(g->builder, LLVMIntULT, pv, LLVMConstInt(i64, 10, 0), "s.lt10"),
            fast1, full);

        LLVMPositionBuilderAtEnd(g->builder, fast1);
        LLVMValueRef asc_idx = LLVMBuildAdd(g->builder, pv, LLVMConstInt(i64, '0', 0), "s.d0");
        LLVMBuildRet(g->builder, emit_ascii_char_ptr(g, asc_idx));

        LLVMPositionBuilderAtEnd(g->builder, full);
        LLVMValueRef tmp = LLVMBuildArrayAlloca(g->builder, i8,
            LLVMConstInt(i64, 24, 0), "itoa.s.tmp");
        LLVMValueRef start = NULL, len = NULL;
        emit_itoa64_digits_into_tmp(g, fn, tmp, pv, puns, &start, &len);
        LLVMValueRef src = LLVMBuildInBoundsGEP2(g->builder, i8, tmp, &start, 1, "itoa.s.src");
        LLVMValueRef n1 = LLVMBuildAdd(g->builder, len,
            LLVMConstInt(i64, 1, 0), "itoa.s.n1");
        LLVMValueRef str = emit_string_alloc_rc(g, n1);
        LLVMTypeRef memcpy_ty = LLVMFunctionType(i8ptr,
            (LLVMTypeRef[]){ i8ptr, i8ptr, i64 }, 3, 0);
        LLVMValueRef memcpy_fn = LLVMGetNamedFunction(g->mod, "memcpy");
        if (!memcpy_fn) memcpy_fn = LLVMAddFunction(g->mod, "memcpy", memcpy_ty);
        zan_call2(g->builder, memcpy_ty, memcpy_fn,
                  (LLVMValueRef[]){ str, src, len }, 3, "");
        LLVMBuildStore(g->builder, LLVMConstInt(i8, 0, 0),
            LLVMBuildInBoundsGEP2(g->builder, i8, str, &len, 1, "itoa.s.end"));
        emit_string_len_set(g, str, len);
        LLVMBuildRet(g->builder, str);
        g->current_fn = saved_fn;
        if (save) LLVMPositionBuilderAtEnd(g->builder, save);
    }
    LLVMValueRef args[] = { v, LLVMConstInt(i32, is_unsigned ? 1 : 0, 0) };
    return zan_call2(g->builder, fn_ty, fn, args, 2, "itoa.str");
}
void emit_dbl_str(zan_irgen_t *g, LLVMValueRef buf, LLVMValueRef cap,
                  LLVMValueRef v) {
  LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
  LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
  LLVMTypeRef dbl = LLVMDoubleTypeInContext(g->ctx);
  LLVMTypeRef fn_ty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx),
                                       (LLVMTypeRef[]){i8ptr, i64t, dbl}, 3, 0);
  LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "zan_rt_dbl_str");
  if (!fn)
    fn = LLVMAddFunction(g->mod, "zan_rt_dbl_str", fn_ty);
  LLVMValueRef arg = v;
  LLVMValueRef cap_val = cap;
  if (LLVMGetTypeKind(LLVMTypeOf(v)) == LLVMFloatTypeKind) {
    arg = LLVMBuildFPExt(g->builder, v, dbl, "dbl.ext");
    cap_val =
        LLVMBuildOr(g->builder, cap,
                    LLVMConstInt(i64t, 0x8000000000000000ULL, 0), "cap.flt");
  }
  LLVMValueRef args[] = {buf, cap_val, arg};
  zan_call2(g->builder, fn_ty, fn, args, 3, "");
}
/* 浮点 → 托管串，按实际长度精确分配（同 emit_itoa_string）：栈上 40 字节
 * 出最短往返文本 → strlen → 分配 len+1 → 拷贝 → 记长度。float 沿用
 * zan_rt_dbl_str 的 cap 高位标记。 */
LLVMValueRef emit_dbl_string(zan_irgen_t *g, LLVMValueRef v) {
    LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef dbl = LLVMDoubleTypeInContext(g->ctx);
    bool is_flt = LLVMGetTypeKind(LLVMTypeOf(v)) == LLVMFloatTypeKind;
    const char *name = is_flt ? "__zan_ftoa_str" : "__zan_dtoa_str";
    LLVMTypeRef vt = is_flt ? LLVMFloatTypeInContext(g->ctx) : dbl;
    LLVMTypeRef fn_ty = LLVMFunctionType(i8ptr, &vt, 1, 0);
    LLVMValueRef fn = LLVMGetNamedFunction(g->mod, name);
    if (!fn) {
        fn = LLVMAddFunction(g->mod, name, fn_ty);
        LLVMSetLinkage(fn, LLVMInternalLinkage);
        LLVMBasicBlockRef save = LLVMGetInsertBlock(g->builder);
        LLVMValueRef saved_fn = g->current_fn;
        g->current_fn = fn;
        LLVMPositionBuilderAtEnd(g->builder,
            LLVMAppendBasicBlockInContext(g->ctx, fn, "entry"));
        LLVMValueRef tmp = LLVMBuildArrayAlloca(g->builder, i8,
            LLVMConstInt(i64, 40, 0), "dtoa.s.tmp");
        emit_dbl_str(g, tmp, LLVMConstInt(i64, 40, 0), LLVMGetParam(fn, 0));
        LLVMTypeRef strlen_ty = LLVMFunctionType(i64, &i8ptr, 1, 0);
        LLVMValueRef strlen_fn = LLVMGetNamedFunction(g->mod, "strlen");
        if (!strlen_fn) strlen_fn = LLVMAddFunction(g->mod, "strlen", strlen_ty);
        LLVMValueRef len = zan_call2(g->builder, strlen_ty, strlen_fn, &tmp, 1,
                                     "dtoa.s.len");
        LLVMValueRef n1 = LLVMBuildAdd(g->builder, len,
            LLVMConstInt(i64, 1, 0), "dtoa.s.n1");
        LLVMValueRef str = emit_string_alloc_rc(g, n1);
        LLVMTypeRef memcpy_ty = LLVMFunctionType(i8ptr,
            (LLVMTypeRef[]){ i8ptr, i8ptr, i64 }, 3, 0);
        LLVMValueRef memcpy_fn = LLVMGetNamedFunction(g->mod, "memcpy");
        if (!memcpy_fn) memcpy_fn = LLVMAddFunction(g->mod, "memcpy", memcpy_ty);
        zan_call2(g->builder, memcpy_ty, memcpy_fn,
                  (LLVMValueRef[]){ str, tmp, n1 }, 3, "");
        emit_string_len_set(g, str, len);
        LLVMBuildRet(g->builder, str);
        g->current_fn = saved_fn;
        if (save) LLVMPositionBuilderAtEnd(g->builder, save);
    }
    return zan_call2(g->builder, fn_ty, fn, &v, 1, "dtoa.str");
}

/* 浮点定点格式快路径（"F0".."F6" / "%.0f".."%.6f" / "%.0lf".."%.6lf"）：
 * 拆分出只写缓冲区的 __zan_ftoa_fixed(dst, v, prec, pfmt) -> len，托管串分配留在调用方，
 * 以便 escape/SSO 优化对不逃逸的 dv.ToString("F2") 自动栈上零堆分配；
 * 当 |v|*10^prec < 1e12 且距半整数边界 > 0.001（数学保证单次乘法的 <=0.5 ULP 误差不可能
 * 跨越 0.5 舍入界，与 snprintf 逐字符比特等价）时，用纯 SSE2 整数转换 + digits200 写表，
 * 零 snprintf、零 libm 调用；边界/NaN/Inf/极大值无缝回退 snprintf。 */
LLVMValueRef emit_dbl_fixed_string(zan_irgen_t *g, LLVMValueRef v, int prec,
                                   LLVMValueRef pfmt_lit) {
    LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
    LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef dbl = LLVMDoubleTypeInContext(g->ctx);
    LLVMTypeRef memcpy_ty = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr, i64 }, 3, 0);
    LLVMValueRef memcpy_fn = LLVMGetNamedFunction(g->mod, "memcpy");
    if (!memcpy_fn) memcpy_fn = LLVMAddFunction(g->mod, "memcpy", memcpy_ty);

    LLVMTypeRef fn_ty = LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr, dbl, i32, i8ptr }, 4, 0);
    LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "__zan_ftoa_fixed");
    if (!fn) {
        fn = LLVMAddFunction(g->mod, "__zan_ftoa_fixed", fn_ty);
        LLVMSetLinkage(fn, LLVMInternalLinkage);
        {
            unsigned nw = LLVMGetEnumAttributeKindForName("nounwind", 8);
            if (nw)
                LLVMAddAttributeAtIndex(fn, (LLVMAttributeIndex)LLVMAttributeFunctionIndex,
                                        LLVMCreateEnumAttribute(g->ctx, nw, 0));
        }
        LLVMBasicBlockRef save = LLVMGetInsertBlock(g->builder);
        LLVMValueRef saved_fn = g->current_fn;
        g->current_fn = fn;

        /* 常量表：10^0..10^6（double 与 i64 各一份） */
        LLVMValueRef fpow_vals[7];
        LLVMValueRef ipow_vals[7];
        double fp = 1.0;
        unsigned long long ip = 1;
        for (int k = 0; k < 7; k++) {
            fpow_vals[k] = LLVMConstReal(dbl, fp);
            ipow_vals[k] = LLVMConstInt(i64, ip, 0);
            fp *= 10.0;
            ip *= 10ULL;
        }
        LLVMTypeRef fpow_arr_ty = LLVMArrayType(dbl, 7);
        LLVMValueRef fpow_gv = LLVMAddGlobal(g->mod, fpow_arr_ty, "__zan_ftos_fpow10");
        LLVMSetLinkage(fpow_gv, LLVMInternalLinkage);
        LLVMSetGlobalConstant(fpow_gv, 1);
        LLVMSetInitializer(fpow_gv, LLVMConstArray(dbl, fpow_vals, 7));

        LLVMTypeRef ipow_arr_ty = LLVMArrayType(i64, 7);
        LLVMValueRef ipow_gv = LLVMAddGlobal(g->mod, ipow_arr_ty, "__zan_ftos_ipow10");
        LLVMSetLinkage(ipow_gv, LLVMInternalLinkage);
        LLVMSetGlobalConstant(ipow_gv, 1);
        LLVMSetInitializer(ipow_gv, LLVMConstArray(i64, ipow_vals, 7));

        LLVMTypeRef d1_ty = LLVMFunctionType(dbl, &dbl, 1, 0);
        LLVMValueRef fabs_fn = LLVMGetNamedFunction(g->mod, "llvm.fabs.f64");
        if (!fabs_fn) fabs_fn = LLVMAddFunction(g->mod, "llvm.fabs.f64", d1_ty);

        LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
        LLVMBasicBlockRef chk_rnd = LLVMAppendBasicBlockInContext(g->ctx, fn, "chkr");
        LLVMBasicBlockRef fast_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "fast");
        LLVMBasicBlockRef neg_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "neg");
        LLVMBasicBlockRef frac_chk = LLVMAppendBasicBlockInContext(g->ctx, fn, "fchk");
        LLVMBasicBlockRef frac_prep = LLVMAppendBasicBlockInContext(g->ctx, fn, "fprep");
        LLVMBasicBlockRef frac_loop = LLVMAppendBasicBlockInContext(g->ctx, fn, "floop");
        LLVMBasicBlockRef emit_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "femit");
        LLVMBasicBlockRef slow_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "slow");

        LLVMValueRef pdst = LLVMGetParam(fn, 0);
        LLVMValueRef pv = LLVMGetParam(fn, 1);
        LLVMValueRef pprec = LLVMGetParam(fn, 2);
        LLVMValueRef pfmt = LLVMGetParam(fn, 3);

        LLVMPositionBuilderAtEnd(g->builder, entry);
        LLVMValueRef tmp = LLVMBuildArrayAlloca(g->builder, i8,
            LLVMConstInt(i64, 40, 0), "f.tmp");
        LLVMValueRef prec64 = LLVMBuildZExt(g->builder, pprec, i64, "f.p64");
        LLVMValueRef zero64 = LLVMConstInt(i64, 0, 0);
        LLVMValueRef scale = LLVMBuildLoad2(g->builder, dbl,
            LLVMBuildInBoundsGEP2(g->builder, fpow_arr_ty, fpow_gv,
                (LLVMValueRef[]){ zero64, prec64 }, 2, "f.scp"), "f.sc");
        LLVMValueRef av = zan_call2(g->builder, d1_ty, fabs_fn, &pv, 1, "f.av");
        LLVMValueRef scaled = LLVMBuildFMul(g->builder, av, scale, "f.scaled");
        /* 先判范围（ordered OLT 自动排除 NaN/Inf/极大值），确保后续 fptoui 绝不 UB */
        LLVMValueRef in_range = LLVMBuildFCmp(g->builder, LLVMRealOLT, scaled,
            LLVMConstReal(dbl, 1e12), "f.inrng");
        LLVMBuildCondBr(g->builder, in_range, chk_rnd, slow_bb);

        /* 纯 SSE2 半整数边界检验（零 floor/rint libm 调用）：
         * scaled >= 0 且 < 1e12 时 fptoui 就是精确 floor，距 0.5 超过 0.001 时
         * (uint64_t)(scaled + 0.5) 与无穷精度十进制舍入数学等价。 */
        LLVMPositionBuilderAtEnd(g->builder, chk_rnd);
        LLVMValueRef fl_i = LLVMBuildFPToUI(g->builder, scaled, i64, "f.fli");
        LLVMValueRef fl = LLVMBuildUIToFP(g->builder, fl_i, dbl, "f.fl");
        LLVMValueRef frac_d = LLVMBuildFSub(g->builder, scaled, fl, "f.fd");
        LLVMValueRef half_d = LLVMBuildFSub(g->builder, frac_d,
            LLVMConstReal(dbl, 0.5), "f.hd");
        LLVMValueRef half_err = zan_call2(g->builder, d1_ty, fabs_fn, &half_d, 1, "f.herr");
        LLVMValueRef safe_rnd = LLVMBuildFCmp(g->builder, LLVMRealOGT, half_err,
            LLVMConstReal(dbl, 0.001), "f.srnd");
        LLVMBuildCondBr(g->builder, safe_rnd, fast_bb, slow_bb);

        LLVMPositionBuilderAtEnd(g->builder, fast_bb);
        LLVMValueRef sp_half = LLVMBuildFAdd(g->builder, scaled,
            LLVMConstReal(dbl, 0.5), "f.sph");
        LLVMValueRef total = LLVMBuildFPToUI(g->builder, sp_half, i64, "f.tot");
        LLVMValueRef iscale = LLVMBuildLoad2(g->builder, i64,
            LLVMBuildInBoundsGEP2(g->builder, ipow_arr_ty, ipow_gv,
                (LLVMValueRef[]){ zero64, prec64 }, 2, "f.iscp"), "f.isc");
        LLVMValueRef int_part = LLVMBuildUDiv(g->builder, total, iscale, "f.int");
        LLVMValueRef frac_part = LLVMBuildURem(g->builder, total, iscale, "f.frac");
        LLVMValueRef istart = NULL, ilen = NULL;
        emit_itoa64_digits_into_tmp(g, fn, tmp, int_part,
            LLVMConstInt(i32, 1, 0), &istart, &ilen);
        LLVMBasicBlockRef itoa_end_bb = LLVMGetInsertBlock(g->builder);
        LLVMValueRef vbits = LLVMBuildBitCast(g->builder, pv, i64, "f.bits");
        LLVMValueRef is_neg = LLVMBuildICmp(g->builder, LLVMIntSLT, vbits, zero64, "f.neg");
        LLVMBuildCondBr(g->builder, is_neg, neg_bb, frac_chk);

        LLVMPositionBuilderAtEnd(g->builder, neg_bb);
        LLVMValueRef nstart = LLVMBuildSub(g->builder, istart,
            LLVMConstInt(i64, 1, 0), "f.nstart");
        LLVMBuildStore(g->builder, LLVMConstInt(i8, '-', 0),
            LLVMBuildInBoundsGEP2(g->builder, i8, tmp, &nstart, 1, "f.negp"));
        LLVMBuildBr(g->builder, frac_chk);

        LLVMPositionBuilderAtEnd(g->builder, frac_chk);
        LLVMValueRef start_phi = LLVMBuildPhi(g->builder, i64, "f.start");
        LLVMAddIncoming(start_phi, (LLVMValueRef[]){ istart, nstart },
            (LLVMBasicBlockRef[]){ itoa_end_bb, neg_bb }, 2);
        LLVMValueRef has_frac = LLVMBuildICmp(g->builder, LLVMIntSGT, prec64, zero64, "f.hf");
        LLVMBuildCondBr(g->builder, has_frac, frac_prep, emit_bb);

        LLVMPositionBuilderAtEnd(g->builder, frac_prep);
        LLVMValueRef dot_idx = LLVMConstInt(i64, 24, 0);
        LLVMBuildStore(g->builder, LLVMConstInt(i8, '.', 0),
            LLVMBuildInBoundsGEP2(g->builder, i8, tmp, &dot_idx, 1, "f.dotp"));
        LLVMValueRef fend = LLVMBuildAdd(g->builder, dot_idx, prec64, "f.fend");
        LLVMValueRef fend_p1 = LLVMBuildAdd(g->builder,
            LLVMConstInt(i64, 25, 0), prec64, "f.ep1");
        LLVMBuildBr(g->builder, frac_loop);

        LLVMPositionBuilderAtEnd(g->builder, frac_loop);
        LLVMValueRef fpos = LLVMBuildPhi(g->builder, i64, "f.fpos");
        LLVMValueRef fval = LLVMBuildPhi(g->builder, i64, "f.fval");
        LLVMValueRef fdig = LLVMBuildURem(g->builder, fval, LLVMConstInt(i64, 10, 0), "f.dig");
        LLVMValueRef fnxt = LLVMBuildUDiv(g->builder, fval, LLVMConstInt(i64, 10, 0), "f.nxt");
        LLVMValueRef fch = LLVMBuildTrunc(g->builder,
            LLVMBuildAdd(g->builder, fdig, LLVMConstInt(i64, '0', 0), "f.d0"), i8, "f.ch");
        LLVMBuildStore(g->builder, fch,
            LLVMBuildInBoundsGEP2(g->builder, i8, tmp, &fpos, 1, "f.fp"));
        LLVMValueRef fpos_prev = LLVMBuildSub(g->builder, fpos,
            LLVMConstInt(i64, 1, 0), "f.fprev");
        LLVMAddIncoming(fpos, (LLVMValueRef[]){ fend, fpos_prev },
            (LLVMBasicBlockRef[]){ frac_prep, frac_loop }, 2);
        LLVMAddIncoming(fval, (LLVMValueRef[]){ frac_part, fnxt },
            (LLVMBasicBlockRef[]){ frac_prep, frac_loop }, 2);
        LLVMBuildCondBr(g->builder,
            LLVMBuildICmp(g->builder, LLVMIntSGT, fpos_prev, dot_idx, "f.fmore"),
            frac_loop, emit_bb);

        LLVMPositionBuilderAtEnd(g->builder, emit_bb);
        LLVMValueRef end_phi = LLVMBuildPhi(g->builder, i64, "f.end");
        LLVMAddIncoming(end_phi,
            (LLVMValueRef[]){ LLVMConstInt(i64, 24, 0), fend_p1 },
            (LLVMBasicBlockRef[]){ frac_chk, frac_loop }, 2);
        LLVMValueRef len = LLVMBuildSub(g->builder, end_phi, start_phi, "f.len");
        LLVMValueRef src = LLVMBuildInBoundsGEP2(g->builder, i8, tmp, &start_phi, 1, "f.src");
        zan_call2(g->builder, memcpy_ty, memcpy_fn, (LLVMValueRef[]){ pdst, src, len }, 3, "");
        LLVMBuildStore(g->builder, LLVMConstInt(i8, 0, 0),
            LLVMBuildInBoundsGEP2(g->builder, i8, pdst, &len, 1, "f.nul"));
        LLVMBuildRet(g->builder, len);

        /* 慢路径：边界/NaN/Inf/极大值回退 snprintf */
        LLVMPositionBuilderAtEnd(g->builder, slow_bb);
        LLVMTypeRef snp_ty = LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr, i64, i8ptr }, 3, 1);
        LLVMValueRef wn = zan_call2(g->builder, snp_ty, g->fn_snprintf,
            (LLVMValueRef[]){ pdst, LLVMConstInt(i64, 640, 0), pfmt, pv }, 4, "f.wn");
        LLVMValueRef wn64 = LLVMBuildSExt(g->builder, wn, i64, "f.wn64");
        LLVMValueRef max639 = LLVMConstInt(i64, 639, 0);
        LLVMValueRef sl0 = LLVMBuildSelect(g->builder,
            LLVMBuildICmp(g->builder, LLVMIntSLT, wn64, zero64, "f.lt0"),
            zero64, wn64, "f.l0");
        LLVMValueRef slen = LLVMBuildSelect(g->builder,
            LLVMBuildICmp(g->builder, LLVMIntSGT, sl0, max639, "f.gt"),
            max639, sl0, "f.slen");
        LLVMBuildRet(g->builder, slen);

        g->current_fn = saved_fn;
        if (save) LLVMPositionBuilderAtEnd(g->builder, save);
    }
    /* 在调用方栈上格式化再分配托管串，自动触发 escape/SSO 零堆分配 */
    LLVMValueRef scratch = emit_entry_scratch(g, 640, "ftos.tmp");
    LLVMValueRef flen = zan_call2(g->builder, fn_ty, fn,
        (LLVMValueRef[]){ scratch, v, LLVMConstInt(i32, prec, 0), pfmt_lit }, 4, "ftos.len");
    LLVMValueRef bufsz = LLVMBuildAdd(g->builder, flen, LLVMConstInt(i64, 1, 0), "ftos.bsz");
    LLVMValueRef buf = emit_string_alloc_rc(g, bufsz);
    zan_call2(g->builder, memcpy_ty, memcpy_fn, (LLVMValueRef[]){ buf, scratch, flen }, 3, "");
    LLVMBuildStore(g->builder, LLVMConstInt(i8, 0, 0),
        LLVMBuildInBoundsGEP2(g->builder, i8, buf, &flen, 1, "ftos.nul"));
    emit_string_len_set(g, buf, flen);
    return buf;
}
void zan_irgen_compact_completed(zan_irgen_t *g, LLVMValueRef fn) {
  if (!g->function_compactor || zan_diag_has_errors(g->diag))
    return;
  char error[4096];
  if (!zan_irgen_compactor_run((zan_irgen_compactor_t *)g->function_compactor,
                               fn, error, sizeof(error))) {
    zan_diag_emit_code(g->diag, DIAG_ERROR, zan_loc(0, 0, 0, 0), 3097,
                       "LLVM function compaction failed: %s", error);
  }
}

/* A556-1 隐式装箱（定义在 irgen_expr.c）：值形状流向 object/接口槽 */
LLVMValueRef emit_implicit_box(zan_irgen_t *g, zan_type_t *lt, LLVMValueRef val,
                               zan_ast_node_t *rhs, local_scope_t *locals);
