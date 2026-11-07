LLVMValueRef refl_meta_for(zan_irgen_t *g, zan_type_t *t,
                                  const char *disp, int disp_len);

static LLVMTypeRef refl_field_type(zan_irgen_t *g) {
    if (!g->refl_field_type) {
        LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
        LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
        LLVMTypeRef elems[9] = { i8ptr, i8ptr, i64, i64, i8ptr, i64, i8ptr, i8ptr, i8ptr };
        g->refl_field_type = LLVMStructTypeInContext(g->ctx, elems, 9, 0);
    }
    return g->refl_field_type;
}

static LLVMValueRef refl_const_string(zan_irgen_t *g, const char *s, int len) {
    LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
    LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    if (len < 0) len = 0;
    LLVMTypeRef rec_ty = LLVMStructTypeInContext(g->ctx,
        (LLVMTypeRef[]){ i64, i64, LLVMArrayType(i8, (unsigned)(len + 1)) }, 3, 0);
    LLVMValueRef *chars = (LLVMValueRef *)calloc((size_t)len + 1, sizeof(LLVMValueRef));
    if (!chars) return LLVMConstNull(LLVMPointerType(i8, 0));
    for (int i = 0; i < len; i++)
        chars[i] = LLVMConstInt(i8, (unsigned char)s[i], 0);
    chars[len] = LLVMConstInt(i8, 0, 0);
    LLVMValueRef init = LLVMConstNamedStruct(rec_ty, (LLVMValueRef[]){
        LLVMConstInt(i64, ZAN_STRING_SENTINEL_RC, 0),
        LLVMConstInt(i64, ZAN_STR_HDR_WORD(len), 0),
        LLVMConstArray(i8, chars, (unsigned)(len + 1)) }, 3);
    free(chars);
    char nm[64];
    snprintf(nm, sizeof(nm), "__zan.refl.str.%d", g->refl_str_count++);
    LLVMValueRef gv = LLVMAddGlobal(g->mod, rec_ty, nm);
    LLVMSetInitializer(gv, init);
    LLVMSetLinkage(gv, LLVMPrivateLinkage);
    LLVMSetGlobalConstant(gv, 1);
    LLVMSetAlignment(gv, 8);
    LLVMValueRef idx[3] = { LLVMConstInt(i64, 0, 0), LLVMConstInt(i32, 2, 0),
                            LLVMConstInt(i64, 0, 0) };
    return LLVMConstGEP2(rec_ty, gv, idx, 3);
}

static int refl_field_kind(zan_type_t *t) {
    if (!t) return ZAN_REFL_FK_OTHER;
    switch (t->kind) {
    case TYPE_BOOL:   return ZAN_REFL_FK_BOOL;
    case TYPE_SBYTE:  return ZAN_REFL_FK_SBYTE;
    case TYPE_BYTE:   return ZAN_REFL_FK_BYTE;
    case TYPE_SHORT:  return ZAN_REFL_FK_SHORT;
    case TYPE_USHORT: return ZAN_REFL_FK_USHORT;
    case TYPE_INT:    return ZAN_REFL_FK_INT;
    case TYPE_UINT:   return ZAN_REFL_FK_UINT;
    case TYPE_LONG:
    case TYPE_NINT:   return ZAN_REFL_FK_LONG;
    case TYPE_ULONG:  return ZAN_REFL_FK_ULONG;
    case TYPE_CHAR:   return ZAN_REFL_FK_CHAR;
    case TYPE_ENUM:   return ZAN_REFL_FK_ENUM;
    case TYPE_FLOAT:  return ZAN_REFL_FK_FLOAT;
    case TYPE_DOUBLE: return ZAN_REFL_FK_DOUBLE;
    case TYPE_STRING: return ZAN_REFL_FK_STRING;
    case TYPE_OBJECT:
    case TYPE_CLASS:
    case TYPE_INTERFACE:
    case TYPE_ARRAY:
    case TYPE_DELEGATE: return ZAN_REFL_FK_REF;
    default: return ZAN_REFL_FK_OTHER;
    }
}

static int refl_type_kind(zan_type_t *t) {
    if (!t) return ZAN_REFL_TK_OTHER;
    switch (t->kind) {
    case TYPE_CLASS:     return ZAN_REFL_TK_CLASS;
    case TYPE_STRUCT:    return ZAN_REFL_TK_STRUCT;
    case TYPE_ENUM:      return ZAN_REFL_TK_ENUM;
    case TYPE_INTERFACE: return ZAN_REFL_TK_INTERFACE;
    case TYPE_ARRAY:     return ZAN_REFL_TK_ARRAY;
    case TYPE_NULLABLE:  return ZAN_REFL_TK_NULLABLE;
    case TYPE_DELEGATE:  return ZAN_REFL_TK_DELEGATE;
    case TYPE_TASK:      return ZAN_REFL_TK_OTHER;
    default:             return ZAN_REFL_TK_PRIMITIVE;
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
        if (g->struct_types[i].sym == sym) return &g->struct_types[i];
    return NULL;
}

static LLVMValueRef refl_slot_offset(zan_irgen_t *g, LLVMTypeRef st, int slot) {
    LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMValueRef idx[2] = { LLVMConstInt(i64, 0, 0),
                            LLVMConstInt(i32, (unsigned long long)slot, 0) };
    LLVMValueRef p = LLVMConstGEP2(st, LLVMConstNull(LLVMPointerType(st, 0)),
                                   idx, 2);
    return LLVMConstPtrToInt(p, i64);
}

static void refl_pack_attrs(zan_ast_list_t *attrs, char *buf, size_t buf_size);
static void refl_append_method_params(zan_irgen_t *g, zan_ast_node_t *decl, char *buf, size_t cap);
static LLVMValueRef refl_empty_string(zan_irgen_t *g);

static void refl_extract_field_attrs(zan_symbol_t *m, const char **custom_name,
                                     unsigned long long *flags, const char **dflt_val) {
    *custom_name = m && m->name.str ? m->name.str : "";
    *flags = 0;
    *dflt_val = "";
    if (!m || !m->decl) return;
    zan_ast_list_t *attrs = zan_ast_attributes(m->decl);
    for (int i = 0; i < attrs->count; i++) {
        zan_ast_node_t *a = attrs->items[i];
        if (!a || a->kind != AST_ATTRIBUTE || !a->attribute.name) continue;
        if (a->attribute.name->kind != AST_IDENTIFIER) continue;
        zan_istr_t n = a->attribute.name->ident.name;
        const char *s = n.str;
        int len = (int)n.len;
        int dot = -1;
        for (int k = 0; k < len; k++) {
            if (s[k] == '.') dot = k;
        }
        if (dot >= 0) {
            s += (dot + 1);
            len -= (dot + 1);
        }

        /* 1. Custom Name: JsonProperty, JsonPropertyName, Column */
        if ((len == 12 && memcmp(s, "JsonProperty", 12) == 0) ||
            (len == 21 && memcmp(s, "JsonPropertyAttribute", 21) == 0) ||
            (len == 16 && memcmp(s, "JsonPropertyName", 16) == 0) ||
            (len == 25 && memcmp(s, "JsonPropertyNameAttribute", 25) == 0) ||
            (len == 6  && memcmp(s, "Column", 6) == 0) ||
            (len == 15 && memcmp(s, "ColumnAttribute", 15) == 0)) {
            for (int j = 0; j < a->attribute.args.count; j++) {
                zan_ast_node_t *arg = a->attribute.args.items[j];
                if (!arg) continue;
                if (arg->kind == AST_STRING_LITERAL) {
                    *custom_name = arg->str_val.str;
                } else if (arg->kind == AST_ASSIGNMENT && arg->binary.left &&
                           arg->binary.left->kind == AST_IDENTIFIER && arg->binary.right) {
                    zan_istr_t an = arg->binary.left->ident.name;
                    if (an.len == 4 && memcmp(an.str, "Name", 4) == 0) {
                        if (arg->binary.right->kind == AST_STRING_LITERAL) {
                            *custom_name = arg->binary.right->str_val.str;
                        }
                    } else if (an.len == 6 && memcmp(an.str, "Ignore", 6) == 0) {
                        if (arg->binary.right->kind == AST_BOOL_LITERAL && arg->binary.right->bool_val) {
                            *flags |= 1ULL; /* bit 0 = ignored */
                        }
                    } else if (an.len == 10 && memcmp(an.str, "IsIdentity", 10) == 0) {
                        if (arg->binary.right->kind == AST_BOOL_LITERAL && arg->binary.right->bool_val) {
                            /* bit 3 = identity（自增列）。与 pk 位分离：
                             * 复合主键（IsPrimary）不能被当成自增列，
                             * PkList 按 IsIdent 排除自增列走列级主键。 */
                            *flags |= 8ULL;
                        }
                    } else if (an.len == 9 && memcmp(an.str, "IsPrimary", 9) == 0) {
                        if (arg->binary.right->kind == AST_BOOL_LITERAL && arg->binary.right->bool_val) {
                            *flags |= 4ULL; /* bit 2 = pk */
                        }
                    } else if (an.len == 8 && memcmp(an.str, "Required", 8) == 0) {
                        if (arg->binary.right->kind == AST_BOOL_LITERAL && arg->binary.right->bool_val) {
                            *flags |= 2ULL; /* bit 1 = required */
                        }
                    }
                }
            }
        }
        /* 2. Ignore: JsonIgnore, Ignore, NotMapped */
        if ((len == 10 && memcmp(s, "JsonIgnore", 10) == 0) ||
            (len == 19 && memcmp(s, "JsonIgnoreAttribute", 19) == 0) ||
            (len == 6  && memcmp(s, "Ignore", 6) == 0) ||
            (len == 15 && memcmp(s, "IgnoreAttribute", 15) == 0) ||
            (len == 9  && memcmp(s, "NotMapped", 9) == 0) ||
            (len == 18 && memcmp(s, "NotMappedAttribute", 18) == 0)) {
            *flags |= 1ULL; /* bit 0 = ignored */
        }
        /* 3. Required: JsonRequired, Required */
        if ((len == 12 && memcmp(s, "JsonRequired", 12) == 0) ||
            (len == 21 && memcmp(s, "JsonRequiredAttribute", 21) == 0) ||
            (len == 8  && memcmp(s, "Required", 8) == 0) ||
            (len == 17 && memcmp(s, "RequiredAttribute", 17) == 0)) {
            *flags |= 2ULL; /* bit 1 = required */
        }
        /* 4. Primary Key: Key, PrimaryKey */
        if ((len == 3  && memcmp(s, "Key", 3) == 0) ||
            (len == 12 && memcmp(s, "KeyAttribute", 12) == 0) ||
            (len == 10 && memcmp(s, "PrimaryKey", 10) == 0) ||
            (len == 19 && memcmp(s, "PrimaryKeyAttribute", 19) == 0)) {
            *flags |= 4ULL; /* bit 2 = pk */
        }
        /* 5. Default value: JsonDefault, Default, DefaultValue */
        if ((len == 11 && memcmp(s, "JsonDefault", 11) == 0) ||
            (len == 20 && memcmp(s, "JsonDefaultAttribute", 20) == 0) ||
            (len == 12 && memcmp(s, "DefaultValue", 12) == 0) ||
            (len == 21 && memcmp(s, "DefaultValueAttribute", 21) == 0)) {
            if (a->attribute.args.count > 0) {
                zan_ast_node_t *arg = a->attribute.args.items[0];
                if (arg->kind == AST_ASSIGNMENT && arg->binary.right) {
                    arg = arg->binary.right;
                }
                if (arg->kind == AST_STRING_LITERAL) {
                    *dflt_val = arg->str_val.str;
                } else if (arg->kind == AST_BOOL_LITERAL) {
                    *dflt_val = arg->bool_val ? "true" : "false";
                } else if (arg->kind == AST_INT_LITERAL) {
                    char *buf = (char *)malloc(32);
                    snprintf(buf, 32, "%lld", (long long)arg->int_val);
                    *dflt_val = buf;
                } else if (arg->kind == AST_FLOAT_LITERAL) {
                    char *buf = (char *)malloc(32);
                    snprintf(buf, 32, "%g", arg->float_val);
                    *dflt_val = buf;
                }
            }
        }
    }
}

/* 发射the field-record array for a class/struct, or NULL when it has none */
static LLVMValueRef refl_emit_fields_class(zan_irgen_t *g, zan_symbol_t *sym,
                                           int *out_count) {
    *out_count = 0;
    if (!sym) return NULL;
    LLVMTypeRef st = get_struct_llvm_type(g, sym);
    if (!st) return NULL;
    struct zan_struct_type_entry *entry = refl_struct_entry(g, sym);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef frec = refl_field_type(g);

    int cap = sym->member_count > 0 ? sym->member_count : 1;
    LLVMValueRef *recs = (LLVMValueRef *)calloc((size_t)cap, sizeof(LLVMValueRef));
    if (!recs) return NULL;
    int n = 0;
    int slot = class_vptr_offset(sym);
    /* shadow 语义（B-ID147 绑定对账踩实）：派生类声明与继承同名的字段
     * 时，反射只认最派生那个——先扫一遍名字，被更派生声明遮蔽的基类
     * 条目不进表（否则 linear find 先命中基类槽位，子类字段读成基类值）。 */
    char *shadowed = (char *)calloc((size_t)(sym->member_count > 0 ? sym->member_count : 1), 1);
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
        if (g->publish_mode) {
            /* 在 publish 模式下：
             * 私有字段（非 public 且不是属性）既不参与外部反射，也不参与 HTML 数据绑定，
             * 剥离其反射元数据记录，避免级联展开庞大的字段类型元数据树 */
            uint32_t mods = m->modifiers;
            if (m->decl && m->decl->kind == AST_FIELD_DECL)
                mods |= m->decl->field_decl.modifiers;
            if (m->kind == SYM_FIELD && !(mods & MOD_PUBLIC)) {
                slot++;
                continue;
            }
        }
        LLVMValueRef off;
        if (entry && entry->explicit_layout && entry->field_offsets &&
            slot < entry->field_count)
            off = LLVMConstInt(i64, entry->field_offsets[slot], 0);
        else
            off = refl_slot_offset(g, st, slot);

        const char *cname = "";
        unsigned long long flgs = 0;
        const char *dflt = "";
        refl_extract_field_attrs(m, &cname, &flgs, &dflt);

        char fattrbuf[512];
        fattrbuf[0] = 0;
        if (m->decl) refl_pack_attrs(zan_ast_attributes(m->decl), fattrbuf, sizeof(fattrbuf));

        recs[n++] = LLVMConstNamedStruct(frec, (LLVMValueRef[]){
            refl_const_string(g, m->name.str, (int)m->name.len),
            refl_field_type_name(g, m->type),
            LLVMConstInt(i64, (unsigned long long)refl_field_kind(m->type), 0),
            off,
            refl_const_string(g, cname, (int)strlen(cname)),
            LLVMConstInt(i64, flgs, 0),
            refl_const_string(g, dflt, (int)strlen(dflt)),
            refl_meta_for(g, m->type, NULL, 0),
            refl_const_string(g, fattrbuf, (int)strlen(fattrbuf)) }, 9);
        slot++;
    }
    LLVMValueRef arr = NULL;
    if (n > 0) {
        LLVMTypeRef arr_ty = LLVMArrayType(frec, (unsigned)n);
        char nm[96];
        snprintf(nm, sizeof(nm), "__zan.refl.fields.%.*s.%d",
                 (int)sym->name.len, sym->name.str, g->refl_meta_count);
        LLVMValueRef gv = LLVMAddGlobal(g->mod, arr_ty, nm);
        LLVMSetInitializer(gv, LLVMConstArray(frec, recs, (unsigned)n));
        LLVMSetLinkage(gv, LLVMPrivateLinkage);
        LLVMSetGlobalConstant(gv, 1);
        arr = LLVMConstBitCast(gv,
            LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0));
        *out_count = n;
    }
    free(recs);
    free(shadowed);
    return arr;
}

static LLVMValueRef refl_emit_fields_enum(zan_irgen_t *g, zan_symbol_t *sym,
                                          int *out_count) {
    *out_count = 0;
    if (!sym) return NULL;
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef frec = refl_field_type(g);
    int cap = sym->member_count > 0 ? sym->member_count : 1;
    LLVMValueRef *recs = (LLVMValueRef *)calloc((size_t)cap, sizeof(LLVMValueRef));
    if (!recs) return NULL;
    int n = 0;
    long long val = 0;
    for (int i = 0; i < sym->member_count; i++) {
        zan_symbol_t *m = sym->members[i];
        if (m->kind != SYM_ENUM_MEMBER) continue;
        if (m->decl && m->decl->kind == AST_ENUM_MEMBER &&
            m->decl->enum_member.value &&
            m->decl->enum_member.value->kind == AST_INT_LITERAL)
            val = m->decl->enum_member.value->int_val;
        recs[n++] = LLVMConstNamedStruct(frec, (LLVMValueRef[]){
            refl_const_string(g, m->name.str, (int)m->name.len),
            refl_const_string(g, sym->name.str, (int)sym->name.len),
            LLVMConstInt(i64, ZAN_REFL_FK_ENUM_MEMBER, 0),
            LLVMConstInt(i64, (unsigned long long)val, 0),
            refl_const_string(g, m->name.str, (int)m->name.len),
            LLVMConstInt(i64, 0, 0),
            refl_const_string(g, "", 0),
            refl_meta_for(g, sym->type, NULL, 0),
            refl_const_string(g, "", 0) }, 9);
        val++;
    }
    LLVMValueRef arr = NULL;
    if (n > 0) {
        LLVMTypeRef arr_ty = LLVMArrayType(frec, (unsigned)n);
        char nm[96];
        snprintf(nm, sizeof(nm), "__zan.refl.enum.%.*s.%d",
                 (int)sym->name.len, sym->name.str, g->refl_meta_count);
        LLVMValueRef gv = LLVMAddGlobal(g->mod, arr_ty, nm);
        LLVMSetInitializer(gv, LLVMConstArray(frec, recs, (unsigned)n));
        LLVMSetLinkage(gv, LLVMPrivateLinkage);
        LLVMSetGlobalConstant(gv, 1);
        arr = LLVMConstBitCast(gv,
            LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0));
        *out_count = n;
    }
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
    if (!t) return NULL;
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef frec = refl_field_type(g);

    LLVMValueRef recs[2];
    recs[0] = LLVMConstNamedStruct(frec, (LLVMValueRef[]){
        refl_const_string(g, "Count", 5),
        refl_field_type_name(g, g->binder->type_int),
        LLVMConstInt(i64, (unsigned long long)refl_field_kind(g->binder->type_int), 0),
        LLVMConstInt(i64, 0, 0), /* offset 0 in list struct */
        refl_const_string(g, "", 0),
        LLVMConstInt(i64, 0, 0),
        refl_const_string(g, "", 0),
        refl_meta_for(g, g->binder->type_int, NULL, 0),
        refl_const_string(g, "", 0) }, 9);
    recs[1] = LLVMConstNamedStruct(frec, (LLVMValueRef[]){
        refl_const_string(g, "count", 5),
        refl_field_type_name(g, g->binder->type_int),
        LLVMConstInt(i64, (unsigned long long)refl_field_kind(g->binder->type_int), 0),
        LLVMConstInt(i64, 0, 0), /* offset 0 in list struct */
        refl_const_string(g, "", 0),
        LLVMConstInt(i64, 0, 0),
        refl_const_string(g, "", 0),
        refl_meta_for(g, g->binder->type_int, NULL, 0),
        refl_const_string(g, "", 0) }, 9);

    LLVMTypeRef arr_ty = LLVMArrayType(frec, 2);
    char nm[96];
    snprintf(nm, sizeof(nm), "__zan.refl.fields.List.%d", g->refl_meta_count);
    LLVMValueRef gv = LLVMAddGlobal(g->mod, arr_ty, nm);
    LLVMSetInitializer(gv, LLVMConstArray(frec, recs, 2));
    LLVMSetLinkage(gv, LLVMPrivateLinkage);
    LLVMSetGlobalConstant(gv, 1);
    *out_count = 2;
    return LLVMConstBitCast(gv, LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0));
}

static LLVMTypeRef refl_method_type(zan_irgen_t *g) {
    if (!g->refl_method_type) {
        LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
        LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
        LLVMTypeRef elems[8] = { i8ptr, i8ptr, i64, i64, i8ptr, i8ptr, i64, i8ptr };
        g->refl_method_type = LLVMStructTypeInContext(g->ctx, elems, 8, 0);
    }
    return g->refl_method_type;
}

static int refl_accessor_flag(zan_symbol_t *sym, zan_symbol_t *msym) {
    if (!sym || !msym || msym->name.len <= 4) return 0;
    bool is_get = memcmp(msym->name.str, "get_", 4) == 0;
    bool is_set = memcmp(msym->name.str, "set_", 4) == 0;
    if (!is_get && !is_set) return 0;
    const char *pn = msym->name.str + 4;
    int plen = (int)msym->name.len - 4;
    for (int i = 0; i < sym->member_count; i++) {
        zan_symbol_t *p = sym->members[i];
        if (p && p->kind == SYM_PROPERTY && (int)p->name.len == plen &&
            memcmp(p->name.str, pn, (size_t)plen) == 0)
            return is_get ? ZAN_REFL_MF_PROPGET : ZAN_REFL_MF_PROPSET;
    }
    return 0;
}

/* 框架按名反射调用的生命周期钩子（Form / ChildWindow 的 OnLoad 等）。
 * 在 publish 模式下默认保留 thunk 与反射导出，避免 HasMethod 返回真而 Invoke 哑火（B-ID160）。 */
static bool refl_is_framework_hook_name(const char *s, size_t n) {
    if (!s || n < 3) return false;
    /* 显式常用生命周期与事件钩子 */
    if (n == 6 && memcmp(s, "OnLoad", 6) == 0) return true;
    if (n == 6 && memcmp(s, "OnInit", 6) == 0) return true;
    if (n == 6 && memcmp(s, "OnShow", 6) == 0) return true;
    if (n == 6 && memcmp(s, "OnHide", 6) == 0) return true;
    if (n == 7 && memcmp(s, "OnClose", 7) == 0) return true;
    if (n == 9 && memcmp(s, "OnClosing", 9) == 0) return true;
    if (n == 8 && memcmp(s, "OnClosed", 8) == 0) return true;
    if (n == 9 && memcmp(s, "OnDestroy", 9) == 0) return true;
    if (n == 8 && memcmp(s, "OnResize", 8) == 0) return true;
    if (n == 7 && memcmp(s, "OnPaint", 7) == 0) return true;
    if (n == 8 && memcmp(s, "OnUpdate", 8) == 0) return true;
    /* 凡是符合 OnPascalCase 规范的方法（如 OnCustomHook 等），
     * 均作为框架生命周期/事件钩子默认保留 thunk */
    if (s[0] == 'O' && s[1] == 'n' && (s[2] >= 'A' && s[2] <= 'Z'))
        return true;
    return false;
}

/* 设计稿事件处理器与框架钩子只经 Invoke 按名调用：publish 不保留的话 HasMethod
 * 对账通过、点击/回调却静默不执行。 */
bool refl_is_html_handler_name(zan_irgen_t *g, const char *s, size_t n) {
    if (refl_is_framework_hook_name(s, n))
        return true;
    for (int i = 0; i < g->html_handler_count; i++) {
        const char *h = g->html_handlers[i];
        if (strlen(h) == n && memcmp(h, s, n) == 0)
            return true;
    }
    return false;
}

static bool refl_is_html_handler(zan_irgen_t *g, zan_symbol_t *m) {
    return refl_is_html_handler_name(g, (const char *)m->name.str, (size_t)m->name.len);
}

static bool refl_method_should_export(zan_irgen_t *g, zan_symbol_t *sym, zan_symbol_t *m) {
    if (!m || m->kind != SYM_METHOD || !m->decl || m->decl->kind != AST_METHOD_DECL)
        return false;
    if (!g->publish_mode)
        return true;
    if (refl_is_html_handler(g, m))
        return true;
    /* publish 模式下：
     * 1. 属性访问器（get_/set_）必须保留，支持运行期动态数据绑定
     * 2. 带有显式特性注解（attributes）的方法保留
     * 3. 必须是公开方法；私有/内部方法在发布期绝对不暴露反射
     */
    if (refl_accessor_flag(sym, m) != 0)
        return true;
    zan_ast_list_t *attrs = zan_ast_attributes(m->decl);
    if (attrs && attrs->count > 0)
        return true;
    uint32_t mods = m->modifiers | m->decl->method_decl.modifiers;
    if (!(mods & MOD_PUBLIC))
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
    if (!sym) return 0;
    zan_symbol_t **reg = (zan_symbol_t **)calloc(64, sizeof(zan_symbol_t *));
    int rn = 0, rcap = 64;
    for (int pass = 0; pass < 2; pass++) {
        if (pass == 1 && !reg) break; /* 登记表分配失败：退化回本类口径 */
        zan_symbol_t *owner = sym;
        zan_type_t *bt = sym->type ? sym->type->base_type : NULL;
        int depth = 0;
        for (; pass == 0 || (bt && bt->sym && bt->sym != sym && depth < 32);
             bt = bt ? bt->base_type : NULL, depth++) {
            if (pass == 1) owner = bt->sym;
            for (int i = 0; i < owner->member_count; i++) {
                zan_symbol_t *m = owner->members[i];
                if (!refl_method_should_export(g, owner, m))
                    continue;
                if (pass == 1 && (m->modifiers & MOD_STATIC))
                    continue;
                int dup = 0;
                for (int r = 0; r < rn && !dup; r++) {
                    if (reg[r]->name.len == m->name.len &&
                        memcmp(reg[r]->name.str, m->name.str,
                               (size_t)m->name.len) == 0)
                        dup = 1;
                }
                if (dup) continue;
                if (reg) {
                    if (rn == rcap) {
                        int ncap = rcap * 2;
                        zan_symbol_t **nreg =
                            (zan_symbol_t **)realloc(reg,
                                (size_t)ncap * sizeof(*nreg));
                        if (!nreg) break;
                        reg = nreg;
                        rcap = ncap;
                    }
                    reg[rn++] = m;
                }
                if (out) {
                    if (n >= cap) break;
                    out[n] = m;
                }
                n++;
            }
            if (pass == 0) break;
        }
    }
    free(reg);
    return n;
}

static int refl_collect_methods(zan_irgen_t *g, zan_symbol_t *sym, zan_symbol_t **out, int cap) {
    return refl_methods_walk(g, sym, out, cap);
}

static int refl_collect_ctors(zan_symbol_t *sym, zan_ast_node_t **out, int cap) {
    int n = 0;
    if (!sym || !sym->decl) return 0;
    zan_ast_list_t *members = &sym->decl->type_decl.members;
    for (int i = 0; i < members->count && n < cap; i++) {
        zan_ast_node_t *m = members->items[i];
        if (m->kind != AST_CONSTRUCTOR_DECL) continue;
        if (m->method_decl.modifiers & MOD_STATIC) continue;
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
        if (want == i64) return slot;
        return LLVMBuildTrunc(g->builder, slot, want, "refl.a.tr");
    case LLVMDoubleTypeKind:
        return LLVMBuildBitCast(g->builder, slot, dbl, "refl.a.d");
    case LLVMFloatTypeKind:
        return LLVMBuildFPTrunc(g->builder,
            LLVMBuildBitCast(g->builder, slot, dbl, "refl.a.d"), want,
            "refl.a.f");
    case LLVMPointerTypeKind:
        return LLVMBuildIntToPtr(g->builder, slot, want, "refl.a.p");
    default:
        *ok = false;
        return LLVMConstNull(want);
    }
}

