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
        if (LLVMGlobalGetValueType(gv) == rec_ty &&
            LLVMIsGlobalConstant(gv) && LLVMGetInitializer(gv) == init)
            break;
        if (collision == UINT_MAX) {
            zan_loc_t loc = {0};
            zan_diag_emit_code(g->diag, DIAG_ERROR, loc, 3101,
                               "reflection string constant pool exhausted");
            return LLVMConstNull(LLVMPointerType(i8, 0));
        }
        collision++;
    }
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
static LLVMValueRef refl_empty_string(zan_irgen_t *g);

static LLVMValueRef refl_declared_attrs(zan_irgen_t *g, const zan_ast_node_t *decl);

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
        if (m->decl) refl_pack_attrs(zan_ast_attributes(m->decl), fattrbuf, sizeof(fattrbuf));

        recs[n++] = LLVMConstNamedStruct(frec, (LLVMValueRef[]){
            refl_const_string(g, m->name.str, (int)m->name.len),
            refl_field_type_name(g, m->type),
            LLVMConstInt(i64, (unsigned long long)refl_field_kind(m->type), 0),
            off,
            refl_declared_attrs(g, m->decl),
            LLVMConstInt(i64, 0, 0),
            LLVMConstNull(LLVMTypeOf(refl_empty_string(g))),
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
            refl_declared_attrs(g, m->decl),
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

static int refl_accessor_flag(zan_symbol_t *sym, zan_istr_t name) {
    if (!sym || name.len <= 4) return 0;
    bool is_get = memcmp(name.str, "get_", 4) == 0;
    bool is_set = memcmp(name.str, "set_", 4) == 0;
    if (!is_get && !is_set) return 0;
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
    /* 符合 OnPascalCase 的方法（如 OnCustomHook）属于可调用钩子候选。 */
    if (s[0] == 'O' && s[1] == 'n' && (s[2] >= 'A' && s[2] <= 'Z'))
        return true;
    return false;
}

/* 设计稿事件处理器与框架钩子只经 Invoke 按名调用：publish 不保留的话 HasMethod
 * 对账通过、点击/回调却静默不执行。 */
bool refl_is_exact_html_handler_name(zan_irgen_t *g, const char *s, size_t n) {
    for (int i = 0; i < g->html_handler_count; i++) {
        const char *h = g->html_handlers[i];
        if (strlen(h) == n && memcmp(h, s, n) == 0) return true;
    }
    return false;
}

bool refl_is_html_handler_name(zan_irgen_t *g, const char *s, size_t n) {
    return refl_is_framework_hook_name(s, n) || refl_is_exact_html_handler_name(g, s, n);
}

static bool refl_is_html_handler(zan_irgen_t *g, zan_symbol_t *m) {
    return refl_is_html_handler_name(g, (const char *)m->name.str, (size_t)m->name.len);
}

bool refl_ctor_needs_thunk(zan_irgen_t *g) {
    /* Capability-wide fallback: a live CreateInstance may receive any TypeInfo,
     * including dynamic GetType(), fields, ElementType or generic arguments.
     * Library exports remain conservative for callers outside this module. */
    return !g->publish_mode || g->emit_lib || g->refl_ctor_used;
}

bool refl_owner_can_have_instance(zan_irgen_t *g, zan_symbol_t *owner) {
    if (!g->publish_instance_prune || !owner || !owner->type || owner->type->kind != TYPE_CLASS ||
        refl_ctor_needs_thunk(g))
        return true;
    /* Recursive field metadata describes types without creating their objects.
     * A derived allocation can still receive methods declared on this owner. */
    for (int i = 1; i < g->leak_site_count; i++) {
        zan_symbol_t *sym = g->site_syms ? g->site_syms[i] : NULL;
        zan_type_t *type = sym ? sym->type : NULL;
        int depth = 0;
        for (; type && type->sym && depth < 32; type = type->base_type, depth++) {
            if (type->sym == owner) return true;
        }
        if (depth == 32 && type) return true;
    }
    /* Runtime guard exceptions have site 0, but their vtable is stored into a
     * real object. Metadata and interface tag comparisons do not make stores. */
    for (int i = 0; i < g->struct_type_count; i++) {
        zan_symbol_t *sym = g->struct_types[i].sym;
        zan_type_t *type = sym ? sym->type : NULL;
        for (int depth = 0; type && depth < 32; type = type->base_type, depth++) {
            if (type->sym != owner) continue;
            char name[320];
            snprintf(name, sizeof(name), "__zan_vtable_%.*s", (int)sym->name.len, sym->name.str);
            if (vtable_has_instance_store(LLVMGetNamedGlobal(g->mod, name), 0)) return true;
            break;
        }
    }
    return false;
}

static bool refl_invoke_name_demanded(zan_irgen_t *g, zan_istr_t name) {
    if (!g->publish_reflect_demand || g->refl_invoke_unknown) return true;
    for (int i = 0; i < g->refl_invoke_name_count; i++) {
        zan_istr_t invoked = g->refl_invoke_names[i];
        if (invoked.len == name.len && memcmp(invoked.str, name.str, name.len) == 0)
            return true;
    }
    return false;
}

/* F4/S1 谓词上移（design.md § P_method_thunk）：thunk 生成与 emit_target body-root 的唯一判定。
 * 非 publish：全部方法（现状不变）。
 * publish：仅 accessor（get_/set_ 且 owner 同名 property）、带注解方法、
 * 设计稿事件处理器与实际 Invoke 可能选中的框架生命周期钩子。
 * 注意与 refl_method_should_export 的\u533a\u522b：\u672c\u8c13\u8bcd\u4e0d\u770b MOD_PUBLIC\uff08\u79c1\u6709 accessor/\u6ce8\u89e3\u4ecd\u751f thunk，\u4e0e\u73b0\u72b6\u4e00\u81f4）\u3002
 */
bool refl_method_needs_thunk(zan_irgen_t *g, zan_symbol_t *owner,
                             zan_ast_node_t *decl) {
    if (!decl || decl->kind != AST_METHOD_DECL) { return false; }
    if (!g->publish_mode) { return true; }
    if (!(decl->method_decl.modifiers & MOD_STATIC) &&
        !refl_owner_can_have_instance(g, owner))
        return false;
    /* Inherited accessors are collected from their declaring base, while this
     * owner is the reflected receiver; its flattened properties retain names. */
    if (refl_accessor_flag(owner, decl->method_decl.name) != 0) { return true; }
    zan_ast_list_t *attrs = zan_ast_attributes(decl);
    if (attrs && attrs->count > 0) { return true; }
    zan_istr_t name = decl->method_decl.name;
    if (refl_is_exact_html_handler_name(g, name.str, name.len)) return true;
    /* Named hooks are callable only when an emitted Invoke may select them.
     * HasMethod and member enumeration still retain the unchanged signatures. */
    return refl_is_framework_hook_name(name.str, name.len) &&
           refl_invoke_name_demanded(g, name);
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
    if (refl_accessor_flag(sym, m->name) != 0)
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

