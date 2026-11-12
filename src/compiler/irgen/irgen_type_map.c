#define ZAN_NULLABLE_PREFIX "zan.nullable."
LLVMValueRef coerce_int_to(zan_irgen_t *g, LLVMValueRef v, LLVMTypeRef target);
bool llvm_is_nullable(LLVMTypeRef t) {
    if (!t || LLVMGetTypeKind(t) != LLVMStructTypeKind) return false;
    const char *n = LLVMGetStructName(t);
    return n && strncmp(n, ZAN_NULLABLE_PREFIX,
                        sizeof(ZAN_NULLABLE_PREFIX) - 1) == 0;
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
                 (*p >= '0' && *p <= '9') ? *p : '_';
        name[off++] = c;
    }
    name[off] = 0;
    if (pn) LLVMDisposeMessage(pn);
    LLVMTypeRef existing = LLVMGetTypeByName2(g->ctx, name);
    if (existing) return existing;
    LLVMTypeRef st = LLVMStructCreateNamed(g->ctx, name);
    LLVMTypeRef body[2] = { payload, LLVMInt1TypeInContext(g->ctx) };
    LLVMStructSetBody(st, body, 2, 0);
    return st;
}
LLVMValueRef nullable_none(LLVMTypeRef nty) {
    return LLVMConstNull(nty);
}
LLVMValueRef nullable_some(zan_irgen_t *g, LLVMTypeRef nty, LLVMValueRef v) {
    LLVMTypeRef pl = nullable_payload_type(nty);
    LLVMValueRef fit = coerce_int_to(g, v, pl);
    if (LLVMTypeOf(fit) != pl) return NULL;
    LLVMValueRef agg = LLVMBuildInsertValue(g->builder, LLVMGetUndef(nty), fit,
                                            0, "nv.val");
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
    if (!type || !type->sym) return LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    for (int i = 0; i < g->struct_type_count; i++) {
        if (g->struct_types[i].sym == type->sym) {
            return g->struct_types[i].llvm_type;
        }
    }
    /* 有声明或有合成成员时算布局——此时必须把结构体类型注册进去。
     * 合成类型（如 Tuple、LINQ 中间 row）无 AST 声明节点但有 members，
     * 普通结构体/类有 decl 节点。两者都需要注册 LLVM struct 布局，
     * 否则字段类型会降级成 i8* / ptr，导致 GEP 校验失败或字段错位。 */
    if (type->sym->decl || type->sym->member_count > 0) register_struct_type(g, type->sym);
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
        LLVMTypeRef dec_fields[] = {
            LLVMInt32TypeInContext(g->ctx),
            LLVMInt32TypeInContext(g->ctx),
            LLVMInt64TypeInContext(g->ctx)
        };
        g->decimal_struct_type = LLVMStructCreateNamed(g->ctx, "Decimal");
        LLVMStructSetBody(g->decimal_struct_type, dec_fields, 3, 0);
    }
    return g->decimal_struct_type;
}
LLVMTypeRef map_type(zan_irgen_t *g, zan_type_t *type) {
    if (!type) return LLVMVoidTypeInContext(g->ctx);
    switch (type->kind) {
    case TYPE_VOID:   return LLVMVoidTypeInContext(g->ctx);
    case TYPE_BOOL:   return LLVMInt1TypeInContext(g->ctx);
    case TYPE_BYTE:   return LLVMInt8TypeInContext(g->ctx);
    case TYPE_SHORT:  return LLVMInt16TypeInContext(g->ctx);
    case TYPE_INT:    return LLVMInt32TypeInContext(g->ctx);
    case TYPE_LONG:   return LLVMInt64TypeInContext(g->ctx);
    case TYPE_SBYTE:  return LLVMInt8TypeInContext(g->ctx);
    case TYPE_USHORT: return LLVMInt16TypeInContext(g->ctx);
    case TYPE_UINT:   return LLVMInt32TypeInContext(g->ctx);
    case TYPE_ULONG:  return LLVMInt64TypeInContext(g->ctx);
    case TYPE_NINT:   return LLVMInt64TypeInContext(g->ctx);
    case TYPE_TASK:   return LLVMInt64TypeInContext(g->ctx);
    case TYPE_FLOAT:  return LLVMFloatTypeInContext(g->ctx);
    case TYPE_DOUBLE: return LLVMDoubleTypeInContext(g->ctx);
    case TYPE_DECIMAL: return get_decimal_type(g);
    case TYPE_CHAR:   return LLVMInt64TypeInContext(g->ctx);
    case TYPE_STRING: return LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    case TYPE_OBJECT: return LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    case TYPE_ENUM:
        return LLVMInt64TypeInContext(g->ctx);
    case TYPE_NULLABLE:
        if (type->element_type)
            return nullable_type_of(g, map_type(g, type->element_type));
        return LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    case TYPE_DELEGATE: {
        int pc = type->delegate_param_count;
        LLVMTypeRef *param_types = (LLVMTypeRef *)calloc(
            (size_t)(pc > 0 ? pc : 1), sizeof(LLVMTypeRef));
        for (int i = 0; i < pc; i++) {
            param_types[i] = map_type(g, type->delegate_param_types[i]);
        }
        LLVMTypeRef ret = type->delegate_is_async
            ? LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0)
            : (type->delegate_ret_type
                ? map_type(g, type->delegate_ret_type)
                : LLVMVoidTypeInContext(g->ctx));
        LLVMTypeRef fn_type = LLVMFunctionType(ret, param_types, (unsigned)pc, 0);
        free(param_types);
        return LLVMPointerType(fn_type, 0);
    }
    case TYPE_STRUCT:
    case TYPE_CLASS: {
        if ((type->name.len == 4 && memcmp(type->name.str, "Span", 4) == 0) ||
            (type->name.len == 12 && memcmp(type->name.str, "ReadOnlySpan", 12) == 0))
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
    default:          return LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    }
}
LLVMValueRef promote_loaded(zan_irgen_t *g, LLVMValueRef v,
                                   zan_type_t *type) {
    if (!v || !type) return v;
    if (LLVMGetTypeKind(LLVMTypeOf(v)) != LLVMIntegerTypeKind) return v;
    if (LLVMGetIntTypeWidth(LLVMTypeOf(v)) >= 64) return v;
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
int class_vptr_offset(zan_symbol_t *sym) {
    if (!sym) return 0;
    if (sym->kind == SYM_STRUCT) return 0;
    if (decl_is_explicit_layout(sym)) return 0;
    return 1;
}
LLVMTypeRef get_struct_llvm_type(zan_irgen_t *g, zan_symbol_t *sym) {
    for (int i = 0; i < g->struct_type_count; i++) {
        if (g->struct_types[i].sym == sym) return g->struct_types[i].llvm_type;
    }
    return NULL;
}

struct zan_struct_type_entry *get_struct_entry(zan_irgen_t *g,
                                                      zan_symbol_t *sym) {
    for (int i = 0; i < g->struct_type_count; i++) {
        if (g->struct_types[i].sym == sym) return &g->struct_types[i];
    }
    return NULL;
}

/* Address a field of an instance */
LLVMValueRef emit_field_ptr(zan_irgen_t *g, zan_symbol_t *type_sym,
                                   LLVMTypeRef st, LLVMValueRef base,
                                   int fi, const char *name) {
    struct zan_struct_type_entry *e = type_sym ? get_struct_entry(g, type_sym) : NULL;
    if (e && e->explicit_layout && fi >= 0 && fi < e->field_count) {
        LLVMValueRef off = LLVMConstInt(LLVMInt64TypeInContext(g->ctx),
                                        e->field_offsets[fi], 0);
        LLVMValueRef raw = LLVMBuildInBoundsGEP2(g->builder,
            LLVMInt8TypeInContext(g->ctx), base, &off, 1, "fld.off");
        LLVMTypeRef wrap = LLVMStructTypeInContext(g->ctx, &e->field_llvm[fi], 1, 0);
        return LLVMBuildStructGEP2(g->builder, wrap, raw, 0, name);
    }
    return LLVMBuildStructGEP2(g->builder, st, base, (unsigned)fi, name);
}
bool field_member_is_static(zan_symbol_t *m) {
    if (m->modifiers & MOD_STATIC) return true;
    if (m->kind == SYM_FIELD && m->decl &&
        (m->decl->field_decl.modifiers & MOD_STATIC)) return true;
    return false;
}
int get_field_index(zan_symbol_t *type_sym, zan_istr_t field_name) {
    int idx = class_vptr_offset(type_sym);
    int found = -1;
    for (int i = 0; i < type_sym->member_count; i++) {
        if (type_sym->members[i]->kind == SYM_FIELD ||
            type_sym->members[i]->kind == SYM_PROPERTY) {
            if (field_member_is_static(type_sym->members[i])) continue;
            if (type_sym->members[i]->name.len == field_name.len &&
                memcmp(type_sym->members[i]->name.str, field_name.str, field_name.len) == 0) {
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
    if (type_sym->type && type_sym->type->base_type && type_sym->type->base_type->sym) {
        return get_method_sym(type_sym->type->base_type->sym, method_name);
    }
    if (type_sym->kind == SYM_INTERFACE && type_sym->type) {
        for (int i = 0; i < type_sym->type->interface_count; i++) {
            zan_type_t *it = type_sym->type->interfaces[i];
            if (!it || !it->sym || it->sym == type_sym) continue;
            zan_symbol_t *m = get_method_sym(it->sym, method_name);
            if (m) return m;
        }
    }
    return NULL;
}

/* 返回the symbol that was created for a specific method-declaration AST node */
zan_symbol_t *method_sym_for_decl(zan_symbol_t *type_sym, zan_ast_node_t *decl) {
    zan_class_index_t *ci = class_index_for(type_sym);
    for (int i = ci->decl_buckets[irgen_ptr_bucket(decl, ci->cap)]; i >= 0;
         i = ci->decl_next[i]) {
        zan_symbol_t *m = type_sym->members[i];
        if (m->kind == SYM_METHOD && m->decl == decl) return m;
    }
    return NULL;
}
int method_is_params_variadic(zan_symbol_t *m) {
    if (!m || !m->decl || m->decl->method_decl.params.count == 0) return 0;
    zan_ast_node_t *last =
        m->decl->method_decl.params.items[m->decl->method_decl.params.count - 1];
    return last->kind == AST_PARAM && last->param.is_params;
}
int method_accepts_arity(zan_symbol_t *m, int argc) {
    if (!m || !m->decl || m->decl->kind != AST_METHOD_DECL) return 0;
    int pc = m->decl->method_decl.params.count;
    if (pc == argc) return 1;
    if (m->decl->method_decl.is_variadic && argc > pc) return 1;
    bool params_tail = method_is_params_variadic(m) != 0;
    /* params 尾参本身可缺省（收 0..n 个实参）；它前面的可选形参按各自的
     * 默认值逐个可省——"(int a, int b = 10, params int[] rest)" 在 argc=1
     * 时合法（b 填默认、params 收空数组），此前被当成固定 arity 拒绝
     * （A381 params 组合清单）。 */
    int fixed = params_tail ? pc - 1 : pc;
    if (params_tail && argc >= fixed) return 1;
    if (argc < fixed) {
        for (int i = argc; i < fixed; i++) {
            zan_ast_node_t *p = m->decl->method_decl.params.items[i];
            if (!p || p->kind != AST_PARAM || !p->param.default_val) return 0;
        }
        return 1;
    }
    return 0;
}

static int method_type_param_count(zan_symbol_t *m) {
    if (!m || !m->decl || m->decl->kind != AST_METHOD_DECL) return 0;
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
            if (m->kind != SYM_METHOD || !member_name_is(m, name)) continue;
            if (type_arg_count > 0 &&
                method_type_param_count(m) != type_arg_count)
                continue;
            if (m->decl && method_accepts_arity(m, argc)) {
                if (method_is_params_variadic(m)) {
                    if (!variadic) variadic = m;
                } else {
                    return m;
                }
            }
        }
        if (variadic) return variadic;
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
    if (!iface || depth > 512) return NULL;
    zan_symbol_t *m = resolve_overload(iface, name, argc, type_arg_count);
    if (m) return m;
    if (!iface->type) return NULL;
    for (int i = 0; i < iface->type->interface_count; i++) {
        zan_type_t *it = iface->type->interfaces[i];
        if (!it || !it->sym || it->sym == iface) continue;
        m = resolve_iface_overload_depth(it->sym, name, argc, type_arg_count,
                                         depth + 1);
        if (m) return m;
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
           (sym->decl->kind == AST_STRUCT_DECL || sym->decl->kind == AST_CLASS_DECL) &&
           sym->decl->type_decl.is_explicit_layout;
}

static bool field_offset_attr(zan_symbol_t *field, unsigned long *out) {
    if (!field->decl) return false;
    zan_ast_list_t *attrs = zan_ast_attributes(field->decl);
    for (int i = 0; i < attrs->count; i++) {
        zan_ast_node_t *a = attrs->items[i];
        if (a->kind != AST_ATTRIBUTE || !a->attribute.name) continue;
        zan_istr_t n = a->attribute.name->ident.name;
        if (!n.str || n.len != 11 || memcmp(n.str, "FieldOffset", 11) != 0) continue;
        if (a->attribute.args.count < 1) continue;
        zan_ast_node_t *v = a->attribute.args.items[0];
        if (v->kind != AST_INT_LITERAL || v->int_val < 0) continue;
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
    if (!ftype || ftype->kind != TYPE_TYPE_PARAM) return base;
    unsigned long have = abi_size_of(base), need = have;
    for (int i = 0; i < g->generic_inst_count; i++) {
        if (g->generic_insts[i].type_sym != sym) continue;
        zan_type_t *ct = subst_type_param_deep(g, ftype,
                                               g->generic_insts[i].inst);
        if (!ct || ct == ftype) continue;
        unsigned long sz = abi_size_of(map_type(g, ct));
        if (sz > need) need = sz;
    }
    if (need <= have) return base;
    return LLVMArrayType(LLVMInt64TypeInContext(g->ctx),
                         (unsigned)((need + 7) / 8));
}
void register_struct_type(zan_irgen_t *g, zan_symbol_t *sym) {
    if (get_struct_llvm_type(g, sym)) return;
    g->struct_types = irgen_grow(g->struct_types, &g->struct_type_cap,
                                 g->struct_type_count + 1,
                                 sizeof(*g->struct_types));

    char name_buf[256];
    snprintf(name_buf, sizeof(name_buf), "struct.%.*s", (int)sym->name.len, sym->name.str);
    LLVMTypeRef st = LLVMStructCreateNamed(g->ctx, name_buf);
    g->struct_types[g->struct_type_count].sym = sym;
    g->struct_types[g->struct_type_count].llvm_type = st;
    g->struct_type_count++;
    /* 本结构体在表里的下标：下面算字段类型时可能**递归**注册别的结构体，
     * 那会让 g->struct_types 扩容搬家——事后不能再用"最后一个"当下标，
     * 也不能缓存 &g->struct_types[...] 指针（那是野指针）。 */
    int my_index = g->struct_type_count - 1;

    int field_count = 0;
    for (int i = 0; i < sym->member_count; i++) {
        if ((sym->members[i]->kind == SYM_FIELD ||
             sym->members[i]->kind == SYM_PROPERTY) &&
            !field_member_is_static(sym->members[i])) field_count++;
    }

    int vptr = class_vptr_offset(sym);
    LLVMTypeRef *field_types = (LLVMTypeRef *)calloc((size_t)(field_count + vptr), sizeof(LLVMTypeRef));
    int fi = 0;
    if (vptr) {
        field_types[fi++] = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    }
    for (int i = 0; i < sym->member_count; i++) {
        if ((sym->members[i]->kind == SYM_FIELD ||
             sym->members[i]->kind == SYM_PROPERTY) &&
            !field_member_is_static(sym->members[i])) {
            field_types[fi++] = generic_field_slot(g, sym,
                sym->members[i]->type, map_type(g, sym->members[i]->type));
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
        zan_diag_emit_code(g->diag, DIAG_ERROR, sym->decl ? sym->decl->loc : zan_loc(0, 0, 0, 0), 3093,
                      "explicit-layout type '%.*s' cannot have virtual methods",
                      (int)sym->name.len, sym->name.str);
    }
    for (int i = 0; i < sym->member_count; i++) {
        zan_symbol_t *m = sym->members[i];
        if ((m->kind != SYM_FIELD && m->kind != SYM_PROPERTY) ||
            field_member_is_static(m)) continue;
        unsigned long off = 0;
        if (!field_offset_attr(m, &off)) {
            zan_diag_emit_code(g->diag, DIAG_ERROR, m->decl ? m->decl->loc : sym->decl->loc, 3094,
                          "field '%.*s' of explicit-layout type '%.*s' needs [FieldOffset(n)]",
                          (int)m->name.len, m->name.str,
                          (int)sym->name.len, sym->name.str);
        }
        entry->field_offsets[slot] = off;
        unsigned long fa = abi_align_of(field_types[slot]);
        if (off % fa != 0) {
            zan_diag_emit_code(g->diag, DIAG_ERROR, m->decl ? m->decl->loc : sym->decl->loc, 3095,
                          "[FieldOffset(%lu)] on '%.*s' is not %lu-byte aligned",
                          off, (int)m->name.len, m->name.str, fa);
        }
        if (fa > align) align = fa;
        unsigned long end = off + abi_size_of(field_types[slot]);
        if (end > size) size = end;
        slot++;
    }
    if (size % align) size += align - size % align;

    LLVMTypeRef body[2];
    unsigned nbody = 0;
    body[nbody++] = LLVMIntTypeInContext(g->ctx, (unsigned)(align * 8));
    if (size > align)
        body[nbody++] = LLVMArrayType(LLVMInt8TypeInContext(g->ctx),
                                      (unsigned)(size - align));
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
    if (sym->type && sym->type->interface_count > 0) return true;
    if (sym->type && sym->type->base_type && sym->type->base_type->sym) {
        return class_has_virtual_methods(sym->type->base_type->sym);
    }
    return false;
}
int method_declared_param_count(zan_symbol_t *m) {
    if (!m || !m->decl || m->decl->kind != AST_METHOD_DECL) return -1;
    return m->decl->method_decl.params.count;
}
/* A type view keeps substitutions on base edges without allocating types.
 * chain[0] is the derived owner; chain[k]'s arguments are expressed in
 * chain[k-1]'s scope (Derived<U> : Base<List<U>>). */
static zan_type_t *virtual_signature_subst(zan_type_t *t, zan_type_t **chain,
                                          int *scope) {
    while (t && t->kind == TYPE_TYPE_PARAM && *scope > 0) {
        zan_type_t *owner = chain[*scope];
        if (!owner || !owner->sym || !owner->sym->decl) break;
        zan_ast_list_t *tps = &owner->sym->decl->type_decl.type_params;
        int i = 0;
        for (; i < tps->count; i++) {
            zan_istr_t name = tps->items[i]->ident.name;
            if (name.len == t->name.len &&
                memcmp(name.str, t->name.str, (size_t)name.len) == 0)
                break;
        }
        if (i >= tps->count || i >= owner->type_arg_count ||
            !owner->type_args[i]) break;
        t = owner->type_args[i];
        (*scope)--;
    }
    return t;
}
static bool virtual_signature_type_equal(zan_type_t *a, int ascope,
                                         zan_type_t *b, int bscope,
                                         zan_type_t **chain, int depth) {
    if (depth > 64) return false;
    a = virtual_signature_subst(a, chain, &ascope);
    b = virtual_signature_subst(b, chain, &bscope);
    if (a == b && ascope == bscope) return true;
    if (!a || !b || a->kind != b->kind) return false;
    if (a->sym && b->sym && a->sym != b->sym) return false;
    if (a->kind == TYPE_ARRAY || a->kind == TYPE_NULLABLE) {
        if (a->kind == TYPE_ARRAY && a->array_rank != b->array_rank) return false;
        return virtual_signature_type_equal(a->element_type, ascope,
                    b->element_type, bscope, chain, depth + 1);
    }
    if (!(a->sym && a->sym == b->sym) &&
        (a->name.len != b->name.len ||
         (a->name.len && memcmp(a->name.str, b->name.str,
                               (size_t)a->name.len) != 0)))
        return false;
    if (a->type_arg_count != b->type_arg_count) return false;
    for (int i = 0; i < a->type_arg_count; i++)
        if (!virtual_signature_type_equal(a->type_args[i], ascope,
                b->type_args[i], bscope, chain, depth + 1)) return false;
    return true;
}

/* Slot identity follows the declared parameter signature, not call arity:
 * overloads may have the same number of arguments and different ABIs. */
bool virtual_method_signature_equal(zan_symbol_t *a, zan_symbol_t *b) {
    if (a == b) return a != NULL;
    if (!a || !b || !member_name_is(a, b->name) ||
        method_declared_param_count(a) < 0 ||
        method_declared_param_count(a) != method_declared_param_count(b) ||
        method_type_param_count(a) != method_type_param_count(b) ||
        a->member_count != b->member_count)
        return false;
    zan_type_t *chain[64];
    int ascope = 0, bscope = 0;
    chain[0] = b->parent ? b->parent->type : NULL;
    for (int i = 0; chain[i] && i < 63; i++) {
        if (chain[i]->sym == a->parent) { ascope = i; break; }
        chain[i + 1] = chain[i]->base_type;
    }
    if (ascope == 0 && a->parent != b->parent) {
        chain[0] = a->parent ? a->parent->type : NULL;
        for (int i = 0; chain[i] && i < 63; i++) {
            if (chain[i]->sym == b->parent) { bscope = i; break; }
            chain[i + 1] = chain[i]->base_type;
        }
    }
    for (int i = 0; i < a->member_count; i++) {
        zan_symbol_t *ap = a->members[i], *bp = b->members[i];
        if (!ap || !bp || ap->kind != SYM_PARAM || bp->kind != SYM_PARAM ||
            !virtual_signature_type_equal(ap->type, ascope, bp->type, bscope,
                                          chain, 0))
            return false;
        if (!ap->decl || !bp->decl ||
            ap->decl->param.by_ref != bp->decl->param.by_ref)
            return false;
    }
    return true;
}
int get_virtual_method_index(zan_symbol_t *type_sym,
                                    zan_symbol_t *method_sym);
bool method_introduces_virtual_slot(zan_symbol_t *type_sym, zan_symbol_t *m) {
    if (!m || m->kind != SYM_METHOD) return false;
    if ((m->modifiers & (MOD_VIRTUAL | MOD_ABSTRACT)) &&
        !(m->modifiers & MOD_OVERRIDE)) {
        return true;
    }
    if (m->modifiers & MOD_OVERRIDE) {
        zan_symbol_t *base = (type_sym && type_sym->type && type_sym->type->base_type)
            ? type_sym->type->base_type->sym : NULL;
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
int get_virtual_method_index(zan_symbol_t *type_sym,
                                    zan_symbol_t *method_sym) {
    if (!method_sym) return -1;
    if (type_sym->type && type_sym->type->base_type && type_sym->type->base_type->sym) {
        int idx = get_virtual_method_index(type_sym->type->base_type->sym, method_sym);
        if (idx >= 0) return idx;
    }
    int base_count = 0;
    if (type_sym->type && type_sym->type->base_type && type_sym->type->base_type->sym) {
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
    s->vars = (local_var_t *)zan_arena_alloc(arena, sizeof(local_var_t) * (size_t)s->cap);
    s->pattern_count = 0;
    s->pattern_cap = 0;
    s->patterns = NULL;
}
local_scope_t *local_scope_new(zan_arena_t *arena) {
    local_scope_t *s = (local_scope_t *)zan_arena_alloc(arena, sizeof(local_scope_t));
    local_scope_init(s, arena);
    return s;
}
LLVMTypeRef map_type(zan_irgen_t *g, zan_type_t *type);
LLVMTypeRef local_slot_type(zan_irgen_t *g, local_var_t *v) {
    if (LLVMIsAAllocaInst(v->alloca)) return LLVMGetAllocatedType(v->alloca);
    return map_type(g, v->type);
}

unsigned g_local_gen = 0;
void local_add(local_scope_t *scope, zan_istr_t name, LLVMValueRef alloca, zan_type_t *type) {
    g_local_gen++;
    if (scope->count >= scope->cap) {
        int new_cap = scope->cap > 0 ? scope->cap * 2 : INITIAL_LOCALS;
        local_var_t *grown = (local_var_t *)zan_arena_alloc(scope->arena,
            sizeof(local_var_t) * (size_t)new_cap);
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
LLVMValueRef emit_entry_alloca(zan_irgen_t *g, LLVMTypeRef ty, const char *name) {
    zan_builder_point_t pt = zan_builder_save(g);
    LLVMValueRef fn = pt.bb ? LLVMGetBasicBlockParent(pt.bb) : g->current_fn;
    LLVMBasicBlockRef entry = LLVMGetEntryBasicBlock(fn);
    LLVMValueRef term = LLVMGetBasicBlockTerminator(entry);
    if (term) LLVMPositionBuilderBefore(g->builder, term);
    else LLVMPositionBuilderAtEnd(g->builder, entry);
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
local_var_t *local_find_async_role(local_scope_t *scope,
                                         zan_ast_node_t *decl, int role) {
    if (!decl) return NULL;
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
    if (!scope || !decl) return NULL;
    for (int i = scope->count - 1; i >= 0; i--) {
        if (scope->vars[i].binding_decl == decl) return &scope->vars[i];
    }
    return NULL;
}
pattern_binding_t *local_find_pattern_binding(local_scope_t *scope,
                                                      zan_ast_node_t *decl) {
    if (!scope || !decl) return NULL;
    for (int i = scope->pattern_count - 1; i >= 0; i--)
        if (scope->patterns[i].decl == decl) return &scope->patterns[i];
    return NULL;
}
pattern_binding_t *local_add_pattern_binding(local_scope_t *scope,
                                                     zan_ast_node_t *decl) {
    if (!scope || !decl) return NULL;
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
    return t && t->kind != TYPE_ARRAY && t->name.str &&
           (int)t->name.len == len && memcmp(t->name.str, n, (size_t)len) == 0;
}
int is_builtin_collection_type(zan_type_t *t) {
    if (!t || t->kind != TYPE_CLASS) return 0;
    zan_istr_t n = t->name;
    return (n.len == 4 && memcmp(n.str, "List", 4) == 0) ||
           (n.len == 4 && memcmp(n.str, "Dict", 4) == 0) ||
           (n.len == 13 && memcmp(n.str, "StringBuilder", 13) == 0);
}

static int is_rc_collection_type(zan_type_t *t) {
    if (!t || t->kind != TYPE_CLASS) return 0;
    zan_istr_t n = t->name;
    return (n.len == 4 && memcmp(n.str, "List", 4) == 0) ||
           (n.len == 4 && memcmp(n.str, "Dict", 4) == 0) ||
           (n.len == 13 && memcmp(n.str, "StringBuilder", 13) == 0);
}
int is_arc_managed_type(zan_type_t *t) {
    if (!t) return 0;
    if (t->kind == TYPE_INTERFACE) return 1;
    if (t->kind != TYPE_CLASS) return 0;
    if (is_builtin_collection_type(t)) return is_rc_collection_type(t);
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
void emit_list_release_elems(zan_irgen_t *g, zan_type_t *elem_type, LLVMValueRef col);
void emit_dict_release_elems(zan_irgen_t *g, zan_type_t *dict_type, LLVMValueRef col);
void emit_array_release_elems(zan_irgen_t *g, zan_type_t *elem_type,
                                     LLVMValueRef arr, LLVMValueRef len);
void emit_release_obj_value(zan_irgen_t *g, LLVMValueRef cur);
void emit_release_obj_local(zan_irgen_t *g, local_var_t *v);
void emit_struct_local_release(zan_irgen_t *g, zan_type_t *type,
                                      LLVMValueRef slot);
LLVMValueRef zan_store_fit(zan_irgen_t *g, LLVMValueRef val, LLVMValueRef ptr);
void release_all_arc_locals(zan_irgen_t *g, local_scope_t *locals) {
    for (int i = 0; i < locals->count; i++) {
        if (locals->vars[i].box_cell) continue;
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

static unsigned llvm_scalar_size(LLVMTypeRef t) {
    switch (LLVMGetTypeKind(t)) {
    case LLVMIntegerTypeKind: return (LLVMGetIntTypeWidth(t) + 7) / 8;
    case LLVMPointerTypeKind: return 8;
    case LLVMFloatTypeKind:   return 4;
    case LLVMDoubleTypeKind:  return 8;
    case LLVMStructTypeKind: {
        unsigned total = 0;
        unsigned n = LLVMCountStructElementTypes(t);
        for (unsigned i = 0; i < n; i++) {
            unsigned fs = llvm_scalar_size(LLVMStructGetTypeAtIndex(t, i));
            if (!fs) return 0;
            if (fs > 1 && total % fs) total += fs - (total % fs);
            total += fs;
        }
        return total;
    }
    default: return 0;
    }
}
unsigned elem_slot_words(zan_irgen_t *g, zan_type_t *elem) {
    if (!elem || (elem->kind != TYPE_STRUCT && elem->kind != TYPE_DECIMAL)) return 1;
    LLVMTypeRef st = map_type(g, elem);
    if (!st || LLVMGetTypeKind(st) != LLVMStructTypeKind) return 1;
    unsigned sz = llvm_scalar_size(st);
    if (!sz) return 1;
    return (sz + 7) / 8;
}
LLVMValueRef slot_word_index(zan_irgen_t *g, LLVMValueRef idx,
                                    unsigned words) {
    if (words <= 1) return idx;
    return zan_mul(g->builder, idx,
                   LLVMConstInt(LLVMTypeOf(idx), words, 0), "slot.wi");
}

static void check_struct_fits_slot(zan_irgen_t *g, LLVMTypeRef st, zan_ast_node_t *at) {
    if (llvm_scalar_size(st)) return;
    zan_loc_t loc; memset(&loc, 0, sizeof(loc));
    if (at) loc = at->loc;
    zan_diag_emit_code(g->diag, DIAG_ERROR, loc, 3096,
        "this value struct cannot be a collection element: the slot allocator "
        "cannot compute its layout; use a class instead");
}
void store_struct_in_slot(zan_irgen_t *g, LLVMValueRef v,
                                 LLVMValueRef slot_ptr, zan_ast_node_t *at) {
    LLVMTypeRef st = LLVMTypeOf(v);
    check_struct_fits_slot(g, st, at);
    LLVMValueRef sp = LLVMBuildBitCast(g->builder, slot_ptr,
        LLVMPointerType(st, 0), "slot.sp");
    LLVMBuildStore(g->builder, v, sp);
}
LLVMValueRef load_struct_from_slot(zan_irgen_t *g, LLVMValueRef slot_ptr,
                                          LLVMTypeRef st) {
    LLVMValueRef sp = LLVMBuildBitCast(g->builder, slot_ptr,
        LLVMPointerType(st, 0), "slot.lp");
    return LLVMBuildLoad2(g->builder, st, sp, "slot.struct");
}
LLVMValueRef extend_int_for_slot(zan_irgen_t *g, LLVMValueRef v,
                                        zan_type_t *elem_type,
                                        LLVMTypeRef slot_ty) {
    int uns = 0;
    if (elem_type) {
        switch (elem_type->kind) {
        case TYPE_BOOL: case TYPE_BYTE: case TYPE_USHORT:
        case TYPE_UINT: case TYPE_ULONG: case TYPE_CHAR:
            uns = 1; break;
        default: break;
        }
    }
    return uns ? LLVMBuildZExt(g->builder, v, slot_ty, "slot.zx")
               : LLVMBuildSExt(g->builder, v, slot_ty, "slot.sx");
}
void emit_collection_slot_store(zan_irgen_t *g, zan_type_t *elem_type,
                                       LLVMTypeRef slot_ty, LLVMValueRef slot_ptr,
                                       LLVMValueRef value,
                                       zan_ast_node_t *rhs, local_scope_t *locals,
                                       int overwrite_old);
void emit_collection_release_raw_slot(zan_irgen_t *g, zan_type_t *elem_type,
                                             LLVMValueRef raw, LLVMTypeRef slot_ty);
LLVMValueRef get_calloc_fn(zan_irgen_t *g) {
    LLVMValueRef f = LLVMGetNamedFunction(g->mod, "calloc");
    if (!f) {
        LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
        LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
        LLVMTypeRef ft = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i64, i64 }, 2, 0);
        f = LLVMAddFunction(g->mod, "calloc", ft);
    }
    return f;
}
LLVMValueRef emit_alloc_rc_collection(zan_irgen_t *g, zan_ast_node_t *expr,
                                             long size, int coll_kind,
                                             zan_type_t *elem_type) {
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    if (g->cur_inst) elem_type = subst_type_param_deep(g, elem_type, g->cur_inst);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    int site_idx = reserve_arc_site(g, NULL, NULL, coll_kind, elem_type);
    LLVMValueRef site_name = LLVMConstNull(i8ptr);
    if (g->check_leaks) {
        char site_buf[600];
        const char *sfile = loc_site_file(g, expr->loc);
        const char *knm = (coll_kind == 2) ? "StringBuilder"
                        : (coll_kind == 3) ? "Dictionary" : "List";
        int elen = (elem_type && elem_type->name.len) ? elem_type->name.len : 1;
        const char *estr = (elem_type && elem_type->name.len) ? elem_type->name.str : "?";
        snprintf(site_buf, sizeof(site_buf), "%s:%u:%u [%s<%.*s>]",
                 sfile, expr->loc.line, expr->loc.col, knm, elen, estr);
        site_name = zan_irgen_intern_string(g, site_buf);
    }
    LLVMTypeRef alloc_fn_type = LLVMFunctionType(i8ptr,
        (LLVMTypeRef[]){ i64, i64, i8ptr }, 3, 0);
    LLVMValueRef args[] = { LLVMConstInt(i64, (unsigned long long)size, 0),
                            arc_site_arg(g, site_idx),
                            site_name };
    return zan_call2(g->builder, alloc_fn_type, g->rt_alloc, args, 3, "coll");
}
int eh_slot_kind_of(zan_type_t *t) {
    if (!t) return ZAN_EH_SLOT_OBJ;
    if (t->kind == TYPE_STRING) return ZAN_EH_SLOT_STR;
    if (t->kind == TYPE_DELEGATE) return ZAN_EH_SLOT_DLG;
    if (t->kind == TYPE_ARRAY) return ZAN_EH_SLOT_ARR;
    return ZAN_EH_SLOT_OBJ;
}

static LLVMValueRef get_itoa64_fn(zan_irgen_t *g) {
    if (g->fn_itoa64) return g->fn_itoa64;
    LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
    LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef fn_ty = LLVMFunctionType(i64,
        (LLVMTypeRef[]){ i8ptr, i64, i32 }, 3, 0);
    LLVMValueRef fn = LLVMAddFunction(g->mod, "__zan_itoa64", fn_ty);
    LLVMSetLinkage(fn, LLVMInternalLinkage);
    g->fn_itoa64 = fn;

    LLVMBasicBlockRef save = LLVMGetInsertBlock(g->builder);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
    LLVMBasicBlockRef digit = LLVMAppendBasicBlockInContext(g->ctx, fn, "digit");
    LLVMBasicBlockRef done = LLVMAppendBasicBlockInContext(g->ctx, fn, "done");
    LLVMBasicBlockRef sign = LLVMAppendBasicBlockInContext(g->ctx, fn, "sign");
    LLVMBasicBlockRef copy = LLVMAppendBasicBlockInContext(g->ctx, fn, "copy");

    LLVMValueRef buf = LLVMGetParam(fn, 0);
    LLVMValueRef v = LLVMGetParam(fn, 1);
    LLVMValueRef uns = LLVMGetParam(fn, 2);
    LLVMValueRef zero64 = LLVMConstInt(i64, 0, 0);
    LLVMValueRef ten = LLVMConstInt(i64, 10, 0);
    LLVMValueRef one = LLVMConstInt(i64, 1, 0);
    LLVMValueRef cap = LLVMConstInt(i64, 24, 0);

    LLVMPositionBuilderAtEnd(g->builder, entry);
    LLVMValueRef tmp = LLVMBuildArrayAlloca(g->builder, i8, cap, "itoa.tmp");
    LLVMValueRef is_neg = LLVMBuildAnd(g->builder,
        LLVMBuildICmp(g->builder, LLVMIntSLT, v, zero64, "itoa.slt"),
        LLVMBuildICmp(g->builder, LLVMIntEQ, uns,
                      LLVMConstInt(i32, 0, 0), "itoa.signed"), "itoa.neg");
    LLVMValueRef mag = LLVMBuildSelect(g->builder, is_neg,
        LLVMBuildNeg(g->builder, v, "itoa.negv"), v, "itoa.mag");
    LLVMBuildBr(g->builder, digit);

    LLVMPositionBuilderAtEnd(g->builder, digit);
    LLVMValueRef rest = LLVMBuildPhi(g->builder, i64, "itoa.rest");
    LLVMValueRef pos = LLVMBuildPhi(g->builder, i64, "itoa.pos");
    LLVMValueRef next_pos = LLVMBuildSub(g->builder, pos, one, "itoa.pos.n");
    LLVMValueRef d = LLVMBuildURem(g->builder, rest, ten, "itoa.d");
    LLVMValueRef ch = LLVMBuildTrunc(g->builder,
        LLVMBuildAdd(g->builder, d, LLVMConstInt(i64, '0', 0), "itoa.dc"),
        i8, "itoa.ch");
    LLVMBuildStore(g->builder, ch,
        LLVMBuildInBoundsGEP2(g->builder, i8, tmp, &next_pos, 1, "itoa.dp"));
    LLVMValueRef next_rest = LLVMBuildUDiv(g->builder, rest, ten, "itoa.rest.n");
    LLVMAddIncoming(rest, (LLVMValueRef[]){ mag, next_rest },
                    (LLVMBasicBlockRef[]){ entry, digit }, 2);
    LLVMAddIncoming(pos, (LLVMValueRef[]){ cap, next_pos },
                    (LLVMBasicBlockRef[]){ entry, digit }, 2);
    LLVMBuildCondBr(g->builder,
        LLVMBuildICmp(g->builder, LLVMIntEQ, next_rest, zero64, "itoa.last"),
        done, digit);

    LLVMPositionBuilderAtEnd(g->builder, done);
    LLVMBuildCondBr(g->builder, is_neg, sign, copy);

    LLVMPositionBuilderAtEnd(g->builder, sign);
    LLVMValueRef sign_pos = LLVMBuildSub(g->builder, next_pos, one, "itoa.sp");
    LLVMBuildStore(g->builder, LLVMConstInt(i8, '-', 0),
        LLVMBuildInBoundsGEP2(g->builder, i8, tmp, &sign_pos, 1, "itoa.spp"));
    LLVMBuildBr(g->builder, copy);

    LLVMPositionBuilderAtEnd(g->builder, copy);
    LLVMValueRef start = LLVMBuildPhi(g->builder, i64, "itoa.start");
    LLVMAddIncoming(start, (LLVMValueRef[]){ next_pos, sign_pos },
                    (LLVMBasicBlockRef[]){ done, sign }, 2);
    LLVMValueRef len = LLVMBuildSub(g->builder, cap, start, "itoa.len");
    LLVMValueRef src = LLVMBuildInBoundsGEP2(g->builder, i8, tmp, &start, 1,
                                             "itoa.src");
    LLVMTypeRef memcpy_ty = LLVMFunctionType(i8ptr,
        (LLVMTypeRef[]){ i8ptr, i8ptr, i64 }, 3, 0);
    LLVMValueRef memcpy_fn = LLVMGetNamedFunction(g->mod, "memcpy");
    if (!memcpy_fn) memcpy_fn = LLVMAddFunction(g->mod, "memcpy", memcpy_ty);
    zan_call2(g->builder, memcpy_ty, memcpy_fn,
              (LLVMValueRef[]){ buf, src, len }, 3, "");
    LLVMBuildStore(g->builder, LLVMConstInt(i8, 0, 0),
        LLVMBuildInBoundsGEP2(g->builder, i8, buf, &len, 1, "itoa.end"));
    LLVMBuildRet(g->builder, len);

    if (save) LLVMPositionBuilderAtEnd(g->builder, save);
    return fn;
}
LLVMValueRef emit_itoa_into(zan_irgen_t *g, LLVMValueRef buf,
                                   LLVMValueRef v, int is_unsigned) {
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMValueRef fn = get_itoa64_fn(g);
    LLVMTypeRef fn_ty = LLVMFunctionType(i64,
        (LLVMTypeRef[]){ i8ptr, i64, i32 }, 3, 0);
    LLVMValueRef args[] = { buf, v, LLVMConstInt(i32, is_unsigned ? 1 : 0, 0) };
    return zan_call2(g->builder, fn_ty, fn, args, 3, "itoa");
}
void emit_dbl_str(zan_irgen_t *g, LLVMValueRef buf, LLVMValueRef cap,
                         LLVMValueRef v) {
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef dbl = LLVMDoubleTypeInContext(g->ctx);
    LLVMTypeRef fn_ty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx),
        (LLVMTypeRef[]){ i8ptr, i64t, dbl }, 3, 0);
    LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "zan_rt_dbl_str");
    if (!fn) fn = LLVMAddFunction(g->mod, "zan_rt_dbl_str", fn_ty);
    LLVMValueRef arg = v;
    LLVMValueRef cap_val = cap;
    if (LLVMGetTypeKind(LLVMTypeOf(v)) == LLVMFloatTypeKind) {
        arg = LLVMBuildFPExt(g->builder, v, dbl, "dbl.ext");
        cap_val = LLVMBuildOr(g->builder, cap,
            LLVMConstInt(i64t, 0x8000000000000000ULL, 0), "cap.flt");
    }
    LLVMValueRef args[] = { buf, cap_val, arg };
    zan_call2(g->builder, fn_ty, fn, args, 3, "");
}
void zan_irgen_compact_completed(zan_irgen_t *g, LLVMValueRef fn) {
    if (!g->function_compactor || zan_diag_has_errors(g->diag)) return;
    char error[4096];
    if (!zan_irgen_compactor_run((zan_irgen_compactor_t *)g->function_compactor,
                                 fn, error, sizeof(error))) {
        zan_diag_emit_code(g->diag, DIAG_ERROR, zan_loc(0, 0, 0, 0), 3097,
                      "LLVM function compaction failed: %s", error);
    }
}

/* A556-1 隐式装箱（定义在 irgen_expr.c）：值形状流向 object/接口槽 */
LLVMValueRef emit_implicit_box(zan_irgen_t *g, zan_type_t *lt,
                                      LLVMValueRef val, zan_ast_node_t *rhs,
                                      local_scope_t *locals);
