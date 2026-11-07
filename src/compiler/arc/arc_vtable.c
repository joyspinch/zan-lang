
static LLVMValueRef find_fn_for_sym(zan_irgen_t *g, zan_symbol_t *msym) {
    if (!msym) return NULL;
    int i = irgen_find_function(g, msym);
    return i >= 0 ? g->functions[i].fn : NULL;
}

static void collect_vslot_decls(zan_symbol_t *sym, zan_symbol_t **out, int *n, int cap) {
    if (sym->type && sym->type->base_type && sym->type->base_type->sym)
        collect_vslot_decls(sym->type->base_type->sym, out, n, cap);
    for (int i = 0; i < sym->member_count; i++) {
        zan_symbol_t *m = sym->members[i];
        if (method_introduces_virtual_slot(sym, m)) {
            if (*n < cap) out[(*n)++] = m;
        }
    }
}
LLVMValueRef get_vtable_global(zan_irgen_t *g, zan_symbol_t *sym) {
    char name[320];
    snprintf(name, sizeof(name), "__zan_vtable_%.*s",
             (int)sym->name.len, sym->name.str);
    LLVMValueRef gv = LLVMGetNamedGlobal(g->mod, name);
    if (gv) return gv;
    int n = count_virtual_methods(sym);
    if (n < 1) n = 1;
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef at = LLVMArrayType(i8ptr, (unsigned)n);
    gv = LLVMAddGlobal(g->mod, at, name);
    LLVMSetInitializer(gv, LLVMConstNull(at));
    zan_set_odr_comdat(g, gv, name);
    return gv;
}

/* 从实例 inst 出发沿基类链找"声明该方法的类"在这次实例化里的具体类型
 * （`class Ch<T> : Base<T>` 的 Base<Order>、`class Ch<T> : Base<List<T>>` 的
 * Base<List<Order>>）。找不到返回 NULL。 */
zan_type_t *vtable_owner_inst(zan_irgen_t *g, zan_type_t *inst,
                                     zan_symbol_t *want) {
    zan_type_t *cur = inst;
    for (int guard = 0; cur && guard < 16; guard++) {
        if (cur->sym == want) return cur;
        if (!cur->base_type) break;
        cur = subst_type_param_deep(g, cur->base_type, cur);
    }
    return NULL;
}

/* 一个槽位该填哪个函数：声明类本身是泛型类时填**特化版**方法体，
 * 否则填擦除版。签名对不上（类型实参是值类型时特化版的 LLVM 签名会变）
 * 就退回擦除版——虚调用点按擦除签名发起调用，签名不一致会被误读。 */
static LLVMValueRef vtable_slot_fn(zan_irgen_t *g, zan_symbol_t *impl,
                                    zan_type_t *inst) {
    if (!impl) return NULL;
    LLVMValueRef erased = find_fn_for_sym(g, impl);
    if (impl->parent && is_user_generic_sym(impl->parent)) {
        zan_type_t *owner = vtable_owner_inst(g, inst, impl->parent);
        if (owner && owner->type_arg_count > 0) {
            LLVMTypeRef spec_ty = NULL;
            LLVMValueRef spec = find_generic_fn(g, impl, owner->type_args,
                                                owner->type_arg_count, &spec_ty);
            LLVMTypeRef erased_ty = erased ? LLVMGlobalGetValueType(erased) : NULL;
            if (spec && erased_ty && spec_ty == erased_ty) return spec;
        }
    }
    return erased;
}

/* 泛型类的**每个实例化**都要有自己的 vtable。
 *
 * 整个类只有一张表（`__zan_vtable_Ch`）时，表里的槽位放的是**擦除版**方法体
 * （`find_fn_for_sym` 拿到的那个）。擦除版拿不到类型实参，方法体里的
 * `m is T` 恒为 false——于是经基类引用做虚调用时，泛型类的覆写方法判不中
 * 消息类型（静默错值，不报错）。特化版方法体（`Ch_Accepts$Order`）明明
 * 已经发射出来了，却没被挂进表里。
 *
 * 实例化自己的表里放特化版方法体，判定才对。 */
static void fill_vtable_inst(zan_irgen_t *g, LLVMValueRef gv,
                             zan_symbol_t *sym, zan_type_t *inst) {
    if (!gv || !sym) return;
    int n = count_virtual_methods(sym);
    if (n < 1) return;
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    zan_symbol_t **decls = (zan_symbol_t **)calloc((size_t)n,
                                                   sizeof(zan_symbol_t *));
    int nd = 0;
    collect_vslot_decls(sym, decls, &nd, n);
    LLVMValueRef *elems = (LLVMValueRef *)calloc((size_t)n, sizeof(LLVMValueRef));
    for (int s = 0; s < n; s++) {
        LLVMValueRef e = LLVMConstNull(i8ptr);
        if (s < nd) {
            zan_symbol_t *decl = decls[s];
            int arity = decl->decl ? decl->decl->method_decl.params.count : 0;
            zan_symbol_t *impl = resolve_overload(sym, decl->name, arity, 0);
            LLVMValueRef fn = vtable_slot_fn(g, impl, inst);
            if (fn) e = LLVMConstBitCast(fn, i8ptr);
        }
        elems[s] = e;
    }
    LLVMSetInitializer(gv, LLVMConstArray(i8ptr, elems, (unsigned)n));
    LLVMSetGlobalConstant(gv, 1);
    free(elems);
    free(decls);
}
LLVMValueRef get_vtable_global_inst(zan_irgen_t *g, zan_symbol_t *sym,
                                           zan_type_t *inst) {
    if (!inst || !inst->sym || inst->sym != sym) return get_vtable_global(g, sym);
    if (!is_user_generic_sym(sym) || inst->type_arg_count <= 0)
        return get_vtable_global(g, sym);
    char suffix[256];
    mangle_inst_suffix(suffix, sizeof(suffix), inst);
    if (!suffix[0]) return get_vtable_global(g, sym);
    char name[384];
    snprintf(name, sizeof(name), "__zan_vtable_%.*s%s",
             (int)sym->name.len, sym->name.str, suffix);
    LLVMValueRef gv = LLVMGetNamedGlobal(g->mod, name);
    if (gv) return gv;
    int n = count_virtual_methods(sym);
    if (n < 1) n = 1;
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef at = LLVMArrayType(i8ptr, (unsigned)n);
    gv = LLVMAddGlobal(g->mod, at, name);
    LLVMSetInitializer(gv, LLVMConstNull(at));
    zan_set_odr_comdat(g, gv, name);
    /* 立刻填：特化方法体在函数声明阶段就有了，不必等方法体发射。 */
    fill_vtable_inst(g, gv, sym, inst);
    return gv;
}
void emit_vtables(zan_irgen_t *g) {
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    for (int i = 0; i < g->struct_type_count; i++) {
        zan_symbol_t *sym = g->struct_types[i].sym;
        if (!sym || !class_has_virtual_methods(sym)) continue;
        int n = count_virtual_methods(sym);
        if (n < 1) continue;
        zan_symbol_t **decls = (zan_symbol_t **)calloc((size_t)n,
                                                       sizeof(zan_symbol_t *));
        int nd = 0;
        collect_vslot_decls(sym, decls, &nd, n);
        LLVMValueRef *elems = (LLVMValueRef *)calloc((size_t)n, sizeof(LLVMValueRef));
        for (int s = 0; s < n; s++) {
            LLVMValueRef e = LLVMConstNull(i8ptr);
            if (s < nd) {
                zan_symbol_t *decl = decls[s];
                int arity = decl->decl ? decl->decl->method_decl.params.count : 0;
                zan_symbol_t *impl = resolve_overload(sym, decl->name, arity, 0);
                LLVMValueRef fn = vtable_slot_fn(g, impl, NULL);
                if (fn) e = LLVMConstBitCast(fn, i8ptr);
            }
            elems[s] = e;
        }
        LLVMValueRef gv = get_vtable_global(g, sym);
        LLVMSetInitializer(gv, LLVMConstArray(i8ptr, elems, (unsigned)n));
        LLVMSetGlobalConstant(gv, 1);
        char name[320];
        snprintf(name, sizeof(name), "__zan_vtable_%.*s",
                 (int)sym->name.len, sym->name.str);
        zan_set_odr_comdat(g, gv, name);
        free(elems);
        free(decls);
    }
    /* 泛型类的每个实例化一张自己的表：槽位指向特化版方法体，
     * 虚调用经基类引用进来时，方法体里的类型判定才拿得到具体类型。
     * 表在 `get_vtable_global_inst` 里创建即填（特化方法体在声明阶段
     * 就有了），这里只保证"已发现但还没 new 出来"的实例化也有表。 */
    for (int i = 0; i < g->generic_inst_count; i++) {
        zan_type_t *inst = g->generic_insts[i].inst;
        zan_symbol_t *sym = g->generic_insts[i].type_sym;
        if (!inst || !sym || !class_has_virtual_methods(sym)) continue;
        get_vtable_global_inst(g, sym, inst);
    }
}
LLVMValueRef coerce_int_to(zan_irgen_t *g, LLVMValueRef v, LLVMTypeRef target);
LLVMValueRef emit_boundary_coerce(zan_irgen_t *g, LLVMValueRef v,
                                         LLVMTypeRef target) {
    if (!v || !target) return v;
    LLVMTypeRef vt = LLVMTypeOf(v);
    if (vt == target) return v;
    LLVMTypeKind vk = LLVMGetTypeKind(vt);
    LLVMTypeKind tk = LLVMGetTypeKind(target);
    LLVMContextRef c = g->ctx;
    LLVMTypeRef i32 = LLVMInt32TypeInContext(c);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(c);

    /* decimal 目标 + 整数来源：调用实参 / 返回值 / 数组元素这三类边界的
     * 隐式 int→decimal 转换。漏掉时 i64 原样穿透，LLVM 校验当场失败
     * （i64 传给 %Decimal 形参）。 */
    if (tk == LLVMStructTypeKind && target == get_decimal_type(g) &&
        vk == LLVMIntegerTypeKind)
        return coerce_to_decimal(g, v);

    if (tk == LLVMPointerTypeKind) {
        if (vk == LLVMPointerTypeKind)
            return LLVMBuildBitCast(g->builder, v, target, "bc.pp");
        if (vk == LLVMIntegerTypeKind) {
            if (LLVMGetIntTypeWidth(vt) < 64)
                v = zan_iwiden(g->builder, v, i64);
            return LLVMBuildIntToPtr(g->builder, v, target, "bc.ip");
        }
        if (vk == LLVMFloatTypeKind) {
            LLVMValueRef iv = LLVMBuildBitCast(g->builder, v, i32, "bc.fi");
            return LLVMBuildIntToPtr(g->builder, iv, target, "bc.ip");
        }
        if (vk == LLVMDoubleTypeKind) {
            LLVMValueRef iv = LLVMBuildBitCast(g->builder, v, i64, "bc.di");
            return LLVMBuildIntToPtr(g->builder, iv, target, "bc.ip");
        }
    } else if (tk == LLVMIntegerTypeKind) {
        if (vk == LLVMPointerTypeKind)
            return LLVMBuildPtrToInt(g->builder, v, target, "bc.pi");
        if (vk == LLVMIntegerTypeKind)
            return coerce_int_to(g, v, target);
    } else if (tk == LLVMFloatTypeKind) {
        if (vk == LLVMPointerTypeKind) {
            LLVMValueRef iv = LLVMBuildPtrToInt(g->builder, v, i32, "bc.pi");
            return LLVMBuildBitCast(g->builder, iv, target, "bc.if");
        }
        if (vk == LLVMDoubleTypeKind)
            return LLVMBuildFPTrunc(g->builder, v, target, "bc.fptr");
    } else if (tk == LLVMDoubleTypeKind) {
        if (vk == LLVMPointerTypeKind) {
            LLVMValueRef iv = LLVMBuildPtrToInt(g->builder, v, i64, "bc.pi");
            return LLVMBuildBitCast(g->builder, iv, target, "bc.id");
        }
        if (vk == LLVMFloatTypeKind)
            return LLVMBuildFPExt(g->builder, v, target, "bc.fpext");
    }
    return v;
}

/* 结构体按值传参的实参规整。
 *
 * 被调方按值接收结构体形参（形参 LLVM 类型即结构体类型），而调用点的实参
 * 表达式是构造临时对象时，emit_expr 返回的是该临时对象的地址（指针）。
 * 变量实参则返回已按值取出的结构体。两种形态不一致会让调用点传出指针，
 * 与被调方签名不符（LLVM 校验失败：Call parameter type does not match）。
 * 这里统一按形参类型判定：形参是结构体、实参是指针时，先按值取出再传递。
 * `ref` / `out` 形参的 LLVM 类型是指针，不走这条路径。 */
LLVMValueRef coerce_struct_value_arg(zan_irgen_t *g, LLVMValueRef v,
                                             LLVMTypeRef param_ty) {
    if (!v || !param_ty) return v;
    if (LLVMGetTypeKind(param_ty) != LLVMStructTypeKind) return v;
    if (LLVMGetTypeKind(LLVMTypeOf(v)) != LLVMPointerTypeKind) return v;
    return LLVMBuildLoad2(g->builder, param_ty, v, "arg.sval");
}
void coerce_args_to_params(zan_irgen_t *g, LLVMTypeRef fn_type,
                                  LLVMValueRef *call_args, int argc) {
    unsigned npt = LLVMCountParamTypes(fn_type);
    bool va = LLVMIsFunctionVarArg(fn_type) != 0;
    if ((npt == 0 && !va) || argc <= 0) return;
    LLVMTypeRef *pts = (LLVMTypeRef *)calloc((size_t)(npt > 0 ? npt : 1),
                                             sizeof(LLVMTypeRef));
    if (npt > 0) LLVMGetParamTypes(fn_type, pts);
    int n = (argc < (int)npt) ? argc : (int)npt;
    for (int i = 0; i < n; i++) {
        call_args[i] = coerce_struct_value_arg(g, call_args[i], pts[i]);
        call_args[i] = emit_boundary_coerce(g, call_args[i], pts[i]);
    }
    free(pts);
    for (int i = (int)npt; va && i < argc; i++) {
        LLVMValueRef v = call_args[i];
        if (!v) continue;
        LLVMTypeRef t = LLVMTypeOf(v);
        if (LLVMGetTypeKind(t) == LLVMIntegerTypeKind &&
            LLVMGetIntTypeWidth(t) < 32)
            call_args[i] = (LLVMGetIntTypeWidth(t) == 1)
                ? LLVMBuildZExt(g->builder, v,
                    LLVMInt32TypeInContext(g->ctx), "va.prom")
                : LLVMBuildSExt(g->builder, v,
                    LLVMInt32TypeInContext(g->ctx), "va.prom");
        else if (LLVMGetTypeKind(t) == LLVMFloatTypeKind)
            call_args[i] = LLVMBuildFPExt(g->builder, v,
                LLVMDoubleTypeInContext(g->ctx), "va.prom");
    }
}
zan_type_t *subst_type_param_deep(zan_irgen_t *g, zan_type_t *t,
                                         zan_type_t *recv) {
    if (!t || !recv || !recv->sym || !recv->sym->decl) return t;
    zan_ast_list_t *tps = &recv->sym->decl->type_decl.type_params;
    if (tps->count == 0 || recv->type_arg_count < tps->count) return t;
    return zan_binder_subst_named(g->binder, t, tps, recv->type_args);
}
zan_type_t *subst_type_param(zan_type_t *t, zan_type_t *recv) {
    if (!t || t->kind != TYPE_TYPE_PARAM || !recv || !recv->sym) return t;
    zan_ast_node_t *decl = recv->sym->decl;
    if (!decl) return t;
    zan_ast_list_t *tps = &decl->type_decl.type_params;
    for (int i = 0; i < tps->count && i < recv->type_arg_count; i++) {
        zan_ast_node_t *tp = tps->items[i];
        if (tp->kind != AST_IDENTIFIER) continue;
        if (tp->ident.name.len == t->name.len &&
            memcmp(tp->ident.name.str, t->name.str, (size_t)t->name.len) == 0)
            return recv->type_args[i];
    }
    return t;
}
zan_type_t *generic_method_ret(zan_irgen_t *g, zan_symbol_t *msym,
                                      zan_ast_node_t *call, local_scope_t *locals) {
    if (!msym || !msym->decl || msym->decl->kind != AST_METHOD_DECL) return NULL;
    if (!call || call->kind != AST_CALL) return NULL;
    zan_ast_list_t *tps = &msym->decl->method_decl.type_params;
    if (tps->count == 0) return NULL;
    zan_ast_node_t *ret_ref = msym->decl->method_decl.return_type;
    if (!ret_ref || ret_ref->kind != AST_TYPE_REF) return NULL;
    zan_istr_t rn = ret_ref->type_ref.name;
    int which = -1;
    for (int i = 0; i < tps->count; i++) {
        zan_istr_t tn = tps->items[i]->ident.name;
        if (tn.len == rn.len && memcmp(tn.str, rn.str, (size_t)rn.len) == 0) {
            which = i;
            break;
        }
    }
    if (which < 0) return NULL;
    if (call->call.type_args.count > which)
        return resolve_type_ctx(g, call->call.type_args.items[which]);
    zan_ast_list_t *params = &msym->decl->method_decl.params;
    for (int j = 0; j < params->count && j < call->call.args.count; j++) {
        zan_ast_node_t *pref = params->items[j]->param.type;
        if (pref && pref->kind == AST_TYPE_REF &&
            pref->type_ref.name.len == rn.len &&
            memcmp(pref->type_ref.name.str, rn.str, (size_t)rn.len) == 0)
            return infer_expr_type(g, call->call.args.items[j], locals);
    }
    return NULL;
}
bool types_equal(zan_type_t *a, zan_type_t *b) {
    if (a == b) return true;
    if (!a || !b) return false;
    if (a->kind != b->kind) return false;
    if (a->sym && b->sym && a->sym == b->sym) {
        /* 同一类型符号 */
    } else if (a->name.len != b->name.len ||
        (a->name.len && memcmp(a->name.str, b->name.str, (size_t)a->name.len) != 0)) {
        return false;
    }
    if (a->kind == TYPE_ARRAY || a->kind == TYPE_NULLABLE) {
        if (a->kind == TYPE_ARRAY && a->array_rank != b->array_rank)
            return false;
        return types_equal(a->element_type, b->element_type);
    }
    if (a->type_arg_count != b->type_arg_count) return false;
    for (int i = 0; i < a->type_arg_count; i++)
        if (!types_equal(a->type_args[i], b->type_args[i])) return false;
    return true;
}
bool type_arglists_equal(zan_type_t **a, int an, zan_type_t **b, int bn) {
    if (an != bn) return false;
    for (int i = 0; i < an; i++)
        if (!types_equal(a[i], b[i])) return false;
    return true;
}
bool type_is_concrete(zan_type_t *t) {
    if (!t) return false;
    if (t->kind == TYPE_TYPE_PARAM || t->kind == TYPE_ERROR) return false;
    if (t->kind == TYPE_ARRAY || t->kind == TYPE_NULLABLE)
        return type_is_concrete(t->element_type);
    for (int i = 0; i < t->type_arg_count; i++)
        if (!type_is_concrete(t->type_args[i])) return false;
    return true;
}
zan_type_t *concretize(zan_irgen_t *g, zan_type_t *t) {
    if (!g->cur_inst) return t;
    return subst_type_param_deep(g, t, g->cur_inst);
}
bool is_user_generic_sym(zan_symbol_t *sym) {
    if (!sym || !sym->decl) return false;
    zan_ast_node_t *d = sym->decl;
    if (d->kind != AST_CLASS_DECL && d->kind != AST_STRUCT_DECL) return false;
    return d->type_decl.type_params.count > 0;
}
void add_generic_fn(zan_irgen_t *g, zan_symbol_t *msym,
                           zan_type_t **args, int argc,
                           LLVMValueRef fn, LLVMTypeRef fn_type) {
    if (g->generic_fn_count >= g->generic_fn_cap) {
        int ncap = g->generic_fn_cap ? g->generic_fn_cap * 2 : 64;
        g->generic_fns = realloc(g->generic_fns,
                                 (size_t)ncap * sizeof(*g->generic_fns));
        g->generic_fn_cap = ncap;
    }
    g->generic_fns[g->generic_fn_count].msym = msym;
    g->generic_fns[g->generic_fn_count].args = args;
    g->generic_fns[g->generic_fn_count].argc = argc;
    g->generic_fns[g->generic_fn_count].fn = fn;
    g->generic_fns[g->generic_fn_count].fn_type = fn_type;
    g->generic_fn_count++;
}
LLVMValueRef find_generic_fn(zan_irgen_t *g, zan_symbol_t *msym,
                                    zan_type_t **args, int argc,
                                    LLVMTypeRef *out_fn_type) {
    if (!msym || argc <= 0 || !args) return NULL;
    for (int i = 0; i < g->generic_fn_count; i++) {
        if (g->generic_fns[i].msym == msym &&
            type_arglists_equal(g->generic_fns[i].args, g->generic_fns[i].argc,
                                args, argc)) {
            if (out_fn_type) *out_fn_type = g->generic_fns[i].fn_type;
            return g->generic_fns[i].fn;
        }
    }
    return NULL;
}
void add_generic_ctor(zan_irgen_t *g, zan_symbol_t *type_sym,
                             zan_ast_node_t *decl, zan_type_t **args, int argc,
                             int param_count,
                             LLVMValueRef fn, LLVMTypeRef fn_type) {
    if (g->generic_ctor_count >= g->generic_ctor_cap) {
        int ncap = g->generic_ctor_cap ? g->generic_ctor_cap * 2 : 64;
        g->generic_ctors = realloc(g->generic_ctors,
                                   (size_t)ncap * sizeof(*g->generic_ctors));
        g->generic_ctor_cap = ncap;
    }
    g->generic_ctors[g->generic_ctor_count].type_sym = type_sym;
    g->generic_ctors[g->generic_ctor_count].decl = decl;
    g->generic_ctors[g->generic_ctor_count].args = args;
    g->generic_ctors[g->generic_ctor_count].argc = argc;
    g->generic_ctors[g->generic_ctor_count].param_count = param_count;
    g->generic_ctors[g->generic_ctor_count].fn = fn;
    g->generic_ctors[g->generic_ctor_count].fn_type = fn_type;
    g->generic_ctor_count++;
}
LLVMValueRef find_generic_ctor(zan_irgen_t *g, zan_symbol_t *type_sym,
                                      zan_ast_node_t *decl,
                                      zan_type_t **args, int argc,
                                      LLVMTypeRef *out_fn_type) {
    if (!type_sym || argc <= 0 || !args) return NULL;
    for (int i = 0; i < g->generic_ctor_count; i++) {
        if (g->generic_ctors[i].type_sym == type_sym &&
            g->generic_ctors[i].decl == decl &&
            type_arglists_equal(g->generic_ctors[i].args, g->generic_ctors[i].argc,
                                args, argc)) {
            if (out_fn_type) *out_fn_type = g->generic_ctors[i].fn_type;
            return g->generic_ctors[i].fn;
        }
    }
    return NULL;
}
void add_generic_inst(zan_irgen_t *g, zan_type_t *inst) {
    if (!inst || !inst->sym || inst->type_arg_count <= 0) return;
    if (!is_user_generic_sym(inst->sym)) return;
    if (!type_is_concrete(inst)) return;
    for (int i = 0; i < g->generic_inst_count; i++)
        if (g->generic_insts[i].type_sym == inst->sym &&
            type_arglists_equal(g->generic_insts[i].inst->type_args,
                                g->generic_insts[i].inst->type_arg_count,
                                inst->type_args, inst->type_arg_count))
            return;
    if (g->generic_inst_count >= g->generic_inst_cap) {
        int ncap = g->generic_inst_cap ? g->generic_inst_cap * 2 : 32;
        g->generic_insts = realloc(g->generic_insts,
                                   (size_t)ncap * sizeof(*g->generic_insts));
        g->generic_inst_cap = ncap;
    }
    g->generic_insts[g->generic_inst_count].type_sym = inst->sym;
    g->generic_insts[g->generic_inst_count].inst = inst;
    g->generic_inst_count++;
}
void mangle_type_token(char *buf, size_t n, size_t *off, zan_type_t *t) {
    if (!t) return;
    if (t->kind == TYPE_ARRAY || t->kind == TYPE_NULLABLE) {
        mangle_type_token(buf, n, off, t->element_type);
        int w = snprintf(buf + (*off < n ? *off : n), (*off < n) ? n - *off : 0, "A");
        if (w > 0) *off += (size_t)w;
        return;
    }
    int w = snprintf(buf + (*off < n ? *off : n), (*off < n) ? n - *off : 0,
                     "%.*s", (int)t->name.len, t->name.str);
    if (w > 0) *off += (size_t)w;
    for (int i = 0; i < t->type_arg_count; i++) {
        w = snprintf(buf + (*off < n ? *off : n), (*off < n) ? n - *off : 0, "_");
        if (w > 0) *off += (size_t)w;
        mangle_type_token(buf, n, off, t->type_args[i]);
    }
}

/* 构建the function-name suffix for an instantiation, e */
void mangle_inst_suffix(char *buf, size_t n, zan_type_t *inst) {
    size_t off = 0;
    buf[0] = '\0';
    for (int i = 0; i < inst->type_arg_count; i++) {
        int w = snprintf(buf + (off < n ? off : n), (off < n) ? n - off : 0, "$");
        if (w > 0) off += (size_t)w;
        mangle_type_token(buf, n, &off, inst->type_args[i]);
    }
    if (off < n) buf[off] = '\0'; else buf[n - 1] = '\0';
}
