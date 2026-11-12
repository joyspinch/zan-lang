static LLVMValueRef refl_to_slot(zan_irgen_t *g, LLVMValueRef v) {
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef dbl = LLVMDoubleTypeInContext(g->ctx);
    LLVMTypeRef t = LLVMTypeOf(v);
    switch (LLVMGetTypeKind(t)) {
    case LLVMIntegerTypeKind:
        if (t == i64) return v;
        if (LLVMGetIntTypeWidth(t) == 1)
            return LLVMBuildZExt(g->builder, v, i64, "refl.r.z");
        return LLVMBuildSExt(g->builder, v, i64, "refl.r.s");
    case LLVMDoubleTypeKind:
        return LLVMBuildBitCast(g->builder, v, i64, "refl.r.d");
    case LLVMFloatTypeKind:
        return LLVMBuildBitCast(g->builder,
            LLVMBuildFPExt(g->builder, v, dbl, "refl.r.fe"), i64, "refl.r.f");
    case LLVMPointerTypeKind:
        return LLVMBuildPtrToInt(g->builder, v, i64, "refl.r.p");
    default:
        return LLVMConstInt(i64, 0, 0);
    }
}

static LLVMValueRef refl_make_thunk(zan_irgen_t *g, LLVMValueRef fn,
                                    LLVMTypeRef fn_ty, bool is_static,
                                    const char *tag) {
    if (!fn || !fn_ty) return NULL;
    LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i64ptr = LLVMPointerType(i64, 0);
    LLVMTypeRef vd = LLVMVoidTypeInContext(g->ctx);

    unsigned total = LLVMCountParamTypes(fn_ty);
    unsigned max_params = is_static ? ZAN_REFL_ARG_SLOTS : ZAN_REFL_ARG_SLOTS + 1;
    if (total > max_params) return NULL;
    LLVMTypeRef ptypes[ZAN_REFL_ARG_SLOTS + 1];
    LLVMGetParamTypes(fn_ty, ptypes);
    unsigned first = is_static ? 0 : 1;
    if (!is_static && (total < 1 ||
                       LLVMGetTypeKind(ptypes[0]) != LLVMPointerTypeKind))
        return NULL;

    char nm[128];
    snprintf(nm, sizeof(nm), "__zan.refl.thunk.%s.%d", tag ? tag : "m",
             g->refl_thunk_count++);
    LLVMTypeRef th_ty = LLVMFunctionType(vd,
        (LLVMTypeRef[]){ i8ptr, i64ptr, i64ptr }, 3, 0);
    LLVMValueRef th = LLVMAddFunction(g->mod, nm, th_ty);
    LLVMSetLinkage(th, LLVMInternalLinkage);

    zan_builder_point_t pt = zan_builder_save(g);
    LLVMValueRef saved_fn = g->current_fn;
    LLVMTypeRef saved_fn_ret = g->current_fn_ret_type;
    zan_type_t *saved_fn_zan_ret = g->current_fn_zan_ret_type;
    zan_ast_node_t *saved_fn_body = g->current_fn_body;
    bool saved_fn_is_main = g->current_fn_is_main;
    bool saved_fn_is_ctor = g->current_fn_is_ctor;

    g->current_fn = th;
    g->current_fn_ret_type = vd;
    g->current_fn_zan_ret_type = g->binder->type_void;
    g->current_fn_body = NULL;
    g->current_fn_is_main = false;
    g->current_fn_is_ctor = false;

    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, th, "entry");
    LLVMPositionBuilderAtEnd(g->builder, entry);

    bool ok = true;
    LLVMValueRef call_args[ZAN_REFL_ARG_SLOTS + 1];
    if (!is_static)
        call_args[0] = LLVMBuildBitCast(g->builder, LLVMGetParam(th, 0),
                                        ptypes[0], "refl.self");
    for (unsigned i = first; i < total && ok; i++) {
        LLVMValueRef k = LLVMConstInt(i64, (unsigned long long)(i - first), 0);
        LLVMValueRef sp = LLVMBuildGEP2(g->builder, i64, LLVMGetParam(th, 1),
                                        &k, 1, "refl.as");
        LLVMValueRef slot = LLVMBuildLoad2(g->builder, i64, sp, "refl.av");
        call_args[i] = refl_slot_to(g, slot, ptypes[i], &ok);
    }
    LLVMTypeRef rty = LLVMGetReturnType(fn_ty);
    if (ok && LLVMGetTypeKind(rty) != LLVMVoidTypeKind &&
        LLVMGetTypeKind(rty) != LLVMIntegerTypeKind &&
        LLVMGetTypeKind(rty) != LLVMFloatTypeKind &&
        LLVMGetTypeKind(rty) != LLVMDoubleTypeKind &&
        LLVMGetTypeKind(rty) != LLVMPointerTypeKind)
        ok = false;
    if (!ok) {
        LLVMDeleteFunction(th);
        g->current_fn = saved_fn;
        g->current_fn_ret_type = saved_fn_ret;
        g->current_fn_zan_ret_type = saved_fn_zan_ret;
        g->current_fn_body = saved_fn_body;
        g->current_fn_is_main = saved_fn_is_main;
        g->current_fn_is_ctor = saved_fn_is_ctor;
        zan_builder_restore(g, pt);
        return NULL;
    }

    LLVMValueRef res = zan_call2(g->builder, fn_ty, fn, call_args, total, "");
    LLVMValueRef slot = LLVMGetTypeKind(rty) == LLVMVoidTypeKind
        ? LLVMConstInt(i64, 0, 0) : refl_to_slot(g, res);
    LLVMBuildStore(g->builder, slot, LLVMGetParam(th, 2));
    LLVMBuildRetVoid(g->builder);
    g->current_fn = saved_fn;
    g->current_fn_ret_type = saved_fn_ret;
    g->current_fn_zan_ret_type = saved_fn_zan_ret;
    g->current_fn_body = saved_fn_body;
    g->current_fn_is_main = saved_fn_is_main;
    g->current_fn_is_ctor = saved_fn_is_ctor;
    zan_builder_restore(g, pt);
    return LLVMConstBitCast(th, i8ptr);
}

static LLVMValueRef refl_param_names(zan_irgen_t *g, zan_ast_list_t *params) {
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    if (!params || params->count <= 0) return LLVMConstNull(i8ptr);
    LLVMValueRef *names = (LLVMValueRef *)calloc((size_t)params->count,
                                                 sizeof(LLVMValueRef));
    if (!names) return LLVMConstNull(i8ptr);
    for (int i = 0; i < params->count; i++) {
        zan_ast_node_t *p = params->items[i];
        zan_type_t *pt = zan_binder_resolve_type(g->binder, p->param.type);
        names[i] = refl_field_type_name(g, pt);
    }
    LLVMTypeRef arr_ty = LLVMArrayType(i8ptr, (unsigned)params->count);
    char nm[64];
    snprintf(nm, sizeof(nm), "__zan.refl.ptypes.%d", g->refl_str_count++);
    LLVMValueRef gv = LLVMAddGlobal(g->mod, arr_ty, nm);
    LLVMSetInitializer(gv, LLVMConstArray(i8ptr, names,
                                          (unsigned)params->count));
    LLVMSetLinkage(gv, LLVMPrivateLinkage);
    LLVMSetGlobalConstant(gv, 1);
    free(names);
    return LLVMConstBitCast(gv, i8ptr);
}

static LLVMValueRef refl_method_record(zan_irgen_t *g, zan_symbol_t *sym,
                                       zan_symbol_t *msym, bool late) {
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef mrec = refl_method_type(g);
    zan_ast_node_t *decl = msym->decl;
    zan_type_t *rt = decl->method_decl.return_type
        ? zan_binder_resolve_type(g->binder, decl->method_decl.return_type)
        : g->binder->type_void;
    bool is_static = (msym->modifiers & MOD_STATIC) != 0 ||
                     (decl->method_decl.modifiers & MOD_STATIC) != 0;
    int flags = is_static ? ZAN_REFL_MF_STATIC : 0;
    if (decl->method_decl.modifiers & (MOD_VIRTUAL | MOD_OVERRIDE | MOD_ABSTRACT))
        flags |= ZAN_REFL_MF_VIRTUAL;
    flags |= refl_accessor_flag(sym, msym);
    LLVMValueRef thunk = LLVMConstNull(i8ptr);
    if (late) {
        /* 在 publish 模式下，仅为属性访问器（get_/set_）、带注解方法、
         * 设计稿事件处理器与框架生命周期钩子（B-ID160）生成 thunk，
         * 避免为所有普通方法生成包装桩强行锚定死代码，让链接器 --gc-sections 正常工作 */
        zan_ast_list_t *attrs = zan_ast_attributes(decl);
        bool need_thunk = !g->publish_mode ||
                          (flags & (ZAN_REFL_MF_PROPGET | ZAN_REFL_MF_PROPSET)) != 0 ||
                          (attrs && attrs->count > 0) ||
                          refl_is_html_handler(g, msym);
        if (need_thunk) {
            int fi = irgen_find_function(g, msym);
            if (fi >= 0) {
                LLVMValueRef t = refl_make_thunk(g, g->functions[fi].fn,
                                                 g->functions[fi].fn_type,
                                                 is_static, "m");
                if (t) thunk = t;
            }
        }
    }
    char mattrbuf[4096];
    refl_pack_attrs(decl ? zan_ast_attributes(decl) : NULL, mattrbuf, sizeof(mattrbuf));
    if (decl && decl->kind == AST_METHOD_DECL) {
        refl_append_method_params(g, decl, mattrbuf, sizeof(mattrbuf));
    }
    LLVMValueRef mattrs = refl_const_string(g, mattrbuf, (int)strlen(mattrbuf));
    return LLVMConstNamedStruct(mrec, (LLVMValueRef[]){
        refl_const_string(g, msym->name.str, (int)msym->name.len),
        refl_field_type_name(g, rt),
        LLVMConstInt(i64, (unsigned long long)(
            rt && rt->kind == TYPE_VOID ? 0 : refl_field_kind(rt)), 0),
        LLVMConstInt(i64, (unsigned long long)decl->method_decl.params.count, 0),
        refl_param_names(g, &decl->method_decl.params),
        thunk,
        LLVMConstInt(i64, (unsigned long long)flags, 0),
        mattrs }, 8);
}

static LLVMValueRef refl_make_ctor_thunk(zan_irgen_t *g, zan_symbol_t *sym,
                                         LLVMValueRef ctor_fn,
                                         LLVMTypeRef ctor_ty) {
    if (!sym || !sym->type || sym->type->kind != TYPE_CLASS) return NULL;
    LLVMTypeRef st = get_struct_llvm_type(g, sym);
    if (!st) return NULL;
    LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i64ptr = LLVMPointerType(i64, 0);

    unsigned total = (ctor_fn && ctor_ty) ? LLVMCountParamTypes(ctor_ty) : 0;
    if (total > ZAN_REFL_ARG_SLOTS + 1) return NULL;
    LLVMTypeRef ptypes[ZAN_REFL_ARG_SLOTS + 1];
    if (total > 0) LLVMGetParamTypes(ctor_ty, ptypes);

    char nm[128];
    snprintf(nm, sizeof(nm), "__zan.refl.thunk.new.%d", g->refl_thunk_count++);
    LLVMValueRef th = LLVMAddFunction(g->mod, nm,
        LLVMFunctionType(LLVMVoidTypeInContext(g->ctx),
                         (LLVMTypeRef[]){ i8ptr, i64ptr, i64ptr }, 3, 0));
    LLVMSetLinkage(th, LLVMInternalLinkage);

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

    g->current_fn = th;
    g->current_fn_ret_type = LLVMVoidTypeInContext(g->ctx);
    g->current_fn_zan_ret_type = g->binder->type_void;
    g->current_fn_body = NULL;
    g->current_fn_is_main = false;
    g->current_fn_is_ctor = false;

    LLVMPositionBuilderAtEnd(g->builder,
        LLVMAppendBasicBlockInContext(g->ctx, th, "entry"));
    di_clear(g);

    /* 站点必须在 desc_init（站点→类型名表）冻结**之前**登记：这里晚于
     * 名表发射，新建点查无此名，反射造出的对象过不了任何 is/下转
     * （ORM MapRow 的 (T)obj 全军覆没）。预登记站点见
     * refl_reserve_thunk_sites；此处只取用。 */
    int site = -1;
    for (int i = 0; i < g->refl_mtab_count; i++) {
        if (g->refl_mtabs[i].ctors && g->refl_mtabs[i].sym == sym &&
            g->refl_mtabs[i].site >= 0) {
            site = g->refl_mtabs[i].site;
            break;
        }
    }
    if (site < 0) site = reserve_arc_site(g, sym, sym->type, 0, NULL);
    LLVMValueRef raw = zan_call2(g->builder,
        LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i64, i64, i8ptr }, 3, 0),
        g->rt_alloc, (LLVMValueRef[]){ LLVMSizeOf(st),
            arc_site_arg(g, site),
            LLVMConstNull(i8ptr) }, 3, "refl.newobj");
    LLVMValueRef obj = LLVMBuildBitCast(g->builder, raw,
                                        LLVMPointerType(st, 0), "refl.objp");
    LLVMBuildStore(g->builder, LLVMConstNull(st), obj);
    if (class_has_virtual_methods(sym)) {
        LLVMValueRef vp = LLVMBuildStructGEP2(g->builder, st, obj, 0, "refl.vpf");
        LLVMBuildStore(g->builder,
            LLVMBuildBitCast(g->builder, get_vtable_global(g, sym), i8ptr,
                             "refl.vt"), vp);
    }

    emit_eh_tmp_push(g, obj);
    if (total > 0) {
        bool ok = true;
        LLVMValueRef args[ZAN_REFL_ARG_SLOTS + 1];
        args[0] = LLVMBuildBitCast(g->builder, obj, ptypes[0], "refl.this");
        for (unsigned i = 1; i < total && ok; i++) {
            LLVMValueRef k = LLVMConstInt(i64, (unsigned long long)(i - 1), 0);
            LLVMValueRef sp = LLVMBuildGEP2(g->builder, i64, LLVMGetParam(th, 1),
                                            &k, 1, "refl.as");
            args[i] = refl_slot_to(g,
                LLVMBuildLoad2(g->builder, i64, sp, "refl.av"), ptypes[i], &ok);
        }
        if (!ok) {
            LLVMDeleteFunction(th);
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
            return NULL;
        }
        zan_call2(g->builder, ctor_ty, ctor_fn, args, total, "");
    } else if (sym->type->kind == TYPE_CLASS) {
        /* 无显式构造的类：new 路径在调用点内联发字段初始化器
         * （emit_implicit_field_initializers），反射造对象没有调用点——
         * thunk 里补发同一份（EH 语义同 new 路径），CreateInstance 与
         * new 对齐。 */
        local_scope_t *locals = local_scope_new(g->arena);
        emit_implicit_field_initializers(g, sym, sym->type, obj, locals);
    }
    emit_eh_tmp_pop(g);
    LLVMBuildStore(g->builder,
        LLVMBuildPtrToInt(g->builder, raw, i64, "refl.newi"),
        LLVMGetParam(th, 2));
    LLVMBuildRetVoid(g->builder);
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
    return LLVMConstBitCast(th, i8ptr);
}

static LLVMValueRef refl_ctor_record(zan_irgen_t *g, zan_symbol_t *sym,
                                     zan_ast_node_t *decl, bool late) {
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMValueRef thunk = LLVMConstNull(i8ptr);
    if (!decl) {
        if (late) {
            LLVMValueRef t = refl_make_ctor_thunk(g, sym, NULL, NULL);
            if (t) thunk = t;
        }
        return LLVMConstNamedStruct(refl_method_type(g), (LLVMValueRef[]){
            refl_const_string(g, ".ctor", 5),
            refl_const_string(g, sym->name.str, (int)sym->name.len),
            LLVMConstInt(i64, ZAN_REFL_FK_REF, 0),
            LLVMConstInt(i64, 0, 0),
            refl_param_names(g, NULL),
            thunk,
            LLVMConstInt(i64, 0, 0),
            refl_empty_string(g) }, 8);
    }
    if (late) {
        for (int i = 0; i < g->ctor_count; i++) {
            if (g->ctors[i].type_sym != sym || g->ctors[i].decl != decl)
                continue;
            LLVMValueRef t = refl_make_ctor_thunk(g, sym, g->ctors[i].fn,
                                                  g->ctors[i].fn_type);
            if (t) thunk = t;
            break;
        }
    }
    return LLVMConstNamedStruct(refl_method_type(g), (LLVMValueRef[]){
        refl_const_string(g, ".ctor", 5),
        refl_const_string(g, sym->name.str, (int)sym->name.len),
        LLVMConstInt(i64, ZAN_REFL_FK_REF, 0),
        LLVMConstInt(i64, (unsigned long long)decl->method_decl.params.count, 0),
        refl_param_names(g, &decl->method_decl.params),
        thunk,
        LLVMConstInt(i64, 0, 0),
        refl_empty_string(g) }, 8);
}

typedef struct {
    zan_builder_point_t point;
    uint32_t file, line, col;
    LLVMMetadataRef last_loc;
    LLVMValueRef last_loc_fn;
} refl_list_point_t;

static refl_list_point_t refl_list_save(zan_irgen_t *g) {
    return (refl_list_point_t){ zan_builder_save(g), g->di_cur_file,
        g->di_cur_line, g->di_cur_col, g->di_last_loc, g->di_last_loc_fn };
}

static void refl_list_restore(zan_irgen_t *g, refl_list_point_t pt) {
    zan_builder_restore(g, pt.point);
    g->di_cur_file = pt.file;
    g->di_cur_line = pt.line;
    g->di_cur_col = pt.col;
    g->di_last_loc = pt.last_loc;
    g->di_last_loc_fn = pt.last_loc_fn;
}

/* Reflection arguments/results have one i64 slot; aggregate values have no
 * representation in that ABI. Keep their metadata, but no callable thunk. */
static bool refl_list_scalar_element(zan_irgen_t *g, zan_type_t *elem_type) {
    LLVMTypeKind kind = LLVMGetTypeKind(map_type(g, elem_type));
    return kind == LLVMIntegerTypeKind || kind == LLVMFloatTypeKind ||
        kind == LLVMDoubleTypeKind || kind == LLVMPointerTypeKind;
}

static LLVMValueRef refl_emit_ctor_builtin_list(zan_irgen_t *g, zan_type_t *t) {
    zan_type_t *elem_type = container_elem_type(t);
    /* Metadata may be discovered while emitting user code. Reserve now, before
     * any site-indexed tables freeze; emitting this thunk consumes that site. */
    uint32_t saved_file = g->di_cur_file, saved_line = g->di_cur_line;
    g->di_cur_file = g->di_cur_line = 0;
    int site = reserve_arc_site(g, NULL, NULL, 1, elem_type);
    g->di_cur_file = saved_file;
    g->di_cur_line = saved_line;
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i64ptr = LLVMPointerType(i64, 0);
    LLVMTypeRef vd = LLVMVoidTypeInContext(g->ctx);
    char nm[96];
    snprintf(nm, sizeof(nm), "__zan.refl.thunk.list_ctor.%d", g->refl_thunk_count++);
    LLVMValueRef th = LLVMAddFunction(g->mod, nm,
        LLVMFunctionType(vd, (LLVMTypeRef[]){ i8ptr, i64ptr, i64ptr }, 3, 0));
    LLVMSetLinkage(th, LLVMInternalLinkage);
    refl_list_point_t pt = refl_list_save(g);
    g->current_fn = th;
    LLVMPositionBuilderAtEnd(g->builder,
        LLVMAppendBasicBlockInContext(g->ctx, th, "entry"));
    di_clear(g);
    LLVMValueRef raw = zan_call2(g->builder,
        LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i64, i64, i8ptr }, 3, 0),
        g->rt_alloc, (LLVMValueRef[]){ LLVMSizeOf(g->list_struct_type),
            arc_site_arg(g, site), LLVMConstNull(i8ptr) }, 3, "refl.list");
    LLVMValueRef list = LLVMBuildBitCast(g->builder, raw,
        LLVMPointerType(g->list_struct_type, 0), "refl.lp");
    LLVMBuildStore(g->builder, LLVMConstNull(g->list_struct_type), list);
    LLVMBuildStore(g->builder, refl_to_slot(g, raw), LLVMGetParam(th, 2));
    LLVMBuildRetVoid(g->builder);
    refl_list_restore(g, pt);

    LLVMValueRef rec = LLVMConstNamedStruct(refl_method_type(g), (LLVMValueRef[]){
        refl_const_string(g, ".ctor", 5), refl_field_type_name(g, t),
        LLVMConstInt(i64, ZAN_REFL_FK_REF, 0), LLVMConstInt(i64, 0, 0),
        LLVMConstNull(i8ptr), LLVMConstBitCast(th, i8ptr),
        LLVMConstInt(i64, 0, 0), refl_empty_string(g) }, 8);
    snprintf(nm, sizeof(nm), "__zan.refl.ctor.List.%d", g->refl_thunk_count++);
    LLVMTypeRef arr_ty = LLVMArrayType(refl_method_type(g), 1);
    LLVMValueRef gv = LLVMAddGlobal(g->mod, arr_ty, nm);
    LLVMSetInitializer(gv, LLVMConstArray(refl_method_type(g), &rec, 1));
    LLVMSetLinkage(gv, LLVMPrivateLinkage);
    LLVMSetGlobalConstant(gv, 1);
    return LLVMConstBitCast(gv, i8ptr);
}

static LLVMValueRef refl_make_list_add_thunk(zan_irgen_t *g, zan_type_t *elem_type) {
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    if (!refl_list_scalar_element(g, elem_type)) return LLVMConstNull(i8ptr);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i64ptr = LLVMPointerType(i64, 0);
    LLVMTypeRef vd = LLVMVoidTypeInContext(g->ctx);
    char nm[96];
    snprintf(nm, sizeof(nm), "__zan.refl.thunk.list_add.%d", g->refl_thunk_count++);
    LLVMValueRef th = LLVMAddFunction(g->mod, nm,
        LLVMFunctionType(vd, (LLVMTypeRef[]){ i8ptr, i64ptr, i64ptr }, 3, 0));
    LLVMSetLinkage(th, LLVMInternalLinkage);
    refl_list_point_t pt = refl_list_save(g);
    g->current_fn = th;
    LLVMPositionBuilderAtEnd(g->builder,
        LLVMAppendBasicBlockInContext(g->ctx, th, "entry"));
    di_clear(g);
    LLVMValueRef lp = LLVMBuildBitCast(g->builder, LLVMGetParam(th, 0),
        LLVMPointerType(g->list_struct_type, 0), "refl.lp");
    LLVMValueRef cntp = LLVMBuildStructGEP2(g->builder, g->list_struct_type, lp, 0, "refl.cntp");
    LLVMValueRef capp = LLVMBuildStructGEP2(g->builder, g->list_struct_type, lp, 1, "refl.capp");
    LLVMValueRef datap = LLVMBuildStructGEP2(g->builder, g->list_struct_type, lp, 2, "refl.datap");
    LLVMValueRef cnt = LLVMBuildLoad2(g->builder, i64, cntp, "refl.cnt");
    LLVMValueRef cap = LLVMBuildLoad2(g->builder, i64, capp, "refl.cap");
    unsigned words = elem_slot_words(g, elem_type);
    LLVMBasicBlockRef grow = LLVMAppendBasicBlockInContext(g->ctx, th, "grow");
    LLVMBasicBlockRef add = LLVMAppendBasicBlockInContext(g->ctx, th, "add");
    LLVMBuildCondBr(g->builder,
        LLVMBuildICmp(g->builder, LLVMIntUGE, cnt, cap, "refl.grow"), grow, add);
    LLVMPositionBuilderAtEnd(g->builder, grow);
    LLVMValueRef newcap = LLVMBuildSelect(g->builder,
        LLVMBuildICmp(g->builder, LLVMIntEQ, cap, LLVMConstInt(i64, 0, 0), "refl.empty"),
        LLVMConstInt(i64, 8, 0),
        LLVMBuildMul(g->builder, cap, LLVMConstInt(i64, 2, 0), "refl.double"), "refl.newcap");
    LLVMValueRef olddata = LLVMBuildLoad2(g->builder, i64ptr, datap, "refl.olddata");
    LLVMValueRef data = zan_call2(g->builder,
        LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i64 }, 2, 0), g->fn_realloc,
        (LLVMValueRef[]){ LLVMBuildBitCast(g->builder, olddata, i8ptr, "refl.oldraw"),
            LLVMBuildMul(g->builder, newcap, LLVMConstInt(i64, 8 * words, 0), "refl.size") },
        2, "refl.data");
    zan_irgen_emit_oom_check(g, th, data);
    LLVMBuildStore(g->builder, LLVMBuildBitCast(g->builder, data, i64ptr, "refl.newdata"), datap);
    LLVMBuildStore(g->builder, newcap, capp);
    LLVMBuildBr(g->builder, add);
    LLVMPositionBuilderAtEnd(g->builder, add);
    LLVMValueRef buffer = LLVMBuildLoad2(g->builder, i64ptr, datap, "refl.buffer");
    LLVMValueRef widx = slot_word_index(g, cnt, words);
    LLVMValueRef slotp = LLVMBuildGEP2(g->builder, i64, buffer, &widx, 1, "refl.slotp");
    bool ok = true;
    LLVMValueRef value = refl_slot_to(g,
        LLVMBuildLoad2(g->builder, i64, LLVMGetParam(th, 1), "refl.arg"),
        map_type(g, elem_type), &ok);
    /* Invoke borrows arguments. The normal typed store retains references,
     * including nested collections, and preserves null as an occupied slot. */
    emit_collection_slot_store(g, elem_type, i64, slotp, value, NULL, NULL, 0);
    LLVMBuildStore(g->builder,
        LLVMBuildAdd(g->builder, cnt, LLVMConstInt(i64, 1, 0), "refl.next"), cntp);
    LLVMBuildStore(g->builder, LLVMConstInt(i64, 0, 0), LLVMGetParam(th, 2));
    LLVMBuildRetVoid(g->builder);
    refl_list_restore(g, pt);
    return LLVMConstBitCast(th, i8ptr);
}

static LLVMValueRef refl_make_list_count_thunk(zan_irgen_t *g) {
    if (g->fn_list_count_thunk) return g->fn_list_count_thunk;
    LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i64ptr = LLVMPointerType(i64, 0);
    LLVMTypeRef vd = LLVMVoidTypeInContext(g->ctx);

    LLVMTypeRef th_ty = LLVMFunctionType(vd, (LLVMTypeRef[]){ i8ptr, i64ptr, i64ptr }, 3, 0);
    LLVMValueRef th = LLVMAddFunction(g->mod, "__zan.refl.thunk.list_count", th_ty);
    LLVMSetLinkage(th, LLVMInternalLinkage);
    g->fn_list_count_thunk = th;

    refl_list_point_t pt = refl_list_save(g);
    g->current_fn = th;
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, th, "entry");
    LLVMPositionBuilderAtEnd(g->builder, entry);
    di_clear(g);

    LLVMValueRef self = LLVMGetParam(th, 0);
    LLVMValueRef ret_slot = LLVMGetParam(th, 2);

    LLVMValueRef lp = LLVMBuildBitCast(g->builder, self,
        LLVMPointerType(g->list_struct_type, 0), "refl.lp");
    LLVMValueRef cntp = LLVMBuildStructGEP2(g->builder, g->list_struct_type, lp, 0, "refl.cntp");
    LLVMValueRef cnt = LLVMBuildLoad2(g->builder, i64, cntp, "refl.cnt");

    LLVMBuildStore(g->builder, cnt, ret_slot);
    LLVMBuildRetVoid(g->builder);

    refl_list_restore(g, pt);
    return LLVMConstBitCast(th, i8ptr);
}

static LLVMValueRef refl_make_list_get_thunk(zan_irgen_t *g, zan_type_t *elem_type) {
    LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
    if (!refl_list_scalar_element(g, elem_type)) return LLVMConstNull(i8ptr);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i64ptr = LLVMPointerType(i64, 0);
    LLVMTypeRef vd = LLVMVoidTypeInContext(g->ctx);

    char nm[128];
    snprintf(nm, sizeof(nm), "__zan.refl.thunk.list_get.%d", g->refl_thunk_count++);
    LLVMTypeRef th_ty = LLVMFunctionType(vd, (LLVMTypeRef[]){ i8ptr, i64ptr, i64ptr }, 3, 0);
    LLVMValueRef th = LLVMAddFunction(g->mod, nm, th_ty);
    LLVMSetLinkage(th, LLVMInternalLinkage);

    refl_list_point_t pt = refl_list_save(g);
    g->current_fn = th;
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, th, "entry");
    LLVMBasicBlockRef in_b  = LLVMAppendBasicBlockInContext(g->ctx, th, "in_bounds");
    LLVMBasicBlockRef out_b = LLVMAppendBasicBlockInContext(g->ctx, th, "out_bounds");

    LLVMPositionBuilderAtEnd(g->builder, entry);
    di_clear(g);
    LLVMValueRef self = LLVMGetParam(th, 0);
    LLVMValueRef args = LLVMGetParam(th, 1);
    LLVMValueRef ret_slot = LLVMGetParam(th, 2);

    LLVMValueRef idx = LLVMBuildLoad2(g->builder, i64, args, "refl.idx");

    LLVMValueRef lp = LLVMBuildBitCast(g->builder, self,
        LLVMPointerType(g->list_struct_type, 0), "refl.lp");
    LLVMValueRef cntp = LLVMBuildStructGEP2(g->builder, g->list_struct_type, lp, 0, "refl.cntp");
    LLVMValueRef cnt = LLVMBuildLoad2(g->builder, i64, cntp, "refl.cnt");

    LLVMValueRef c1 = LLVMBuildICmp(g->builder, LLVMIntSGE, idx, LLVMConstInt(i64, 0, 0), "refl.c1");
    LLVMValueRef c2 = LLVMBuildICmp(g->builder, LLVMIntSLT, idx, cnt, "refl.c2");
    LLVMValueRef ok = LLVMBuildAnd(g->builder, c1, c2, "refl.ok");
    LLVMBuildCondBr(g->builder, ok, in_b, out_b);

    LLVMPositionBuilderAtEnd(g->builder, out_b);
    LLVMBuildStore(g->builder, LLVMConstInt(i64, 0, 0), ret_slot);
    LLVMBuildRetVoid(g->builder);

    LLVMPositionBuilderAtEnd(g->builder, in_b);
    LLVMValueRef datap = LLVMBuildStructGEP2(g->builder, g->list_struct_type, lp, 2, "refl.datap");
    LLVMValueRef data = LLVMBuildLoad2(g->builder, LLVMPointerType(i64, 0), datap, "refl.data");

    int slot_words = (int)elem_slot_words(g, elem_type);
    LLVMValueRef widx = slot_word_index(g, idx, (unsigned)slot_words);
    LLVMValueRef ep = LLVMBuildGEP2(g->builder, i64, data, &widx, 1, "refl.ep");
    LLVMValueRef raw = LLVMBuildLoad2(g->builder, i64, ep, "refl.raw");

    bool is_ref = elem_type && (
        elem_type->kind == TYPE_CLASS ||
        elem_type->kind == TYPE_STRING ||
        elem_type->kind == TYPE_ARRAY ||
        elem_type->kind == TYPE_INTERFACE ||
        elem_type->kind == TYPE_OBJECT ||
        elem_type->kind == TYPE_DELEGATE ||
        elem_type->kind == TYPE_TASK);

    if (is_ref) {
        LLVMBasicBlockRef do_ret = LLVMAppendBasicBlockInContext(g->ctx, th, "do_retain");
        LLVMBasicBlockRef done   = LLVMAppendBasicBlockInContext(g->ctx, th, "done");
        LLVMValueRef nz = LLVMBuildICmp(g->builder, LLVMIntNE, raw, LLVMConstInt(i64, 0, 0), "refl.nz");
        LLVMBuildCondBr(g->builder, nz, do_ret, done);

        LLVMPositionBuilderAtEnd(g->builder, do_ret);
        LLVMValueRef obj_ptr = LLVMBuildIntToPtr(g->builder, raw, i8ptr, "refl.op");
        LLVMTypeRef arc_ty = LLVMFunctionType(vd, (LLVMTypeRef[]){ i8ptr }, 1, 0);
        zan_call2(g->builder, arc_ty, g->rt_retain, &obj_ptr, 1, "");
        LLVMBuildBr(g->builder, done);

        LLVMPositionBuilderAtEnd(g->builder, done);
    }

    LLVMTypeRef elem_llvm = map_type(g, elem_type);
    LLVMTypeKind kind = LLVMGetTypeKind(elem_llvm);
    LLVMValueRef result = raw;
    if (kind == LLVMFloatTypeKind) {
        LLVMValueRef bits = LLVMBuildTrunc(g->builder, raw,
            LLVMInt32TypeInContext(g->ctx), "refl.fbits");
        result = refl_to_slot(g, LLVMBuildBitCast(g->builder, bits, elem_llvm, "refl.float"));
    } else if (kind == LLVMIntegerTypeKind && LLVMGetIntTypeWidth(elem_llvm) < 64) {
        LLVMValueRef value = LLVMBuildTrunc(g->builder, raw, elem_llvm, "refl.narrow");
        result = extend_int_for_slot(g, value, elem_type, i64);
    }
    LLVMBuildStore(g->builder, result, ret_slot);
    LLVMBuildRetVoid(g->builder);

    refl_list_restore(g, pt);
    return LLVMConstBitCast(th, i8ptr);
}

static LLVMValueRef refl_emit_methods_builtin_list(zan_irgen_t *g, zan_type_t *t,
                                                  int *out_count) {
    *out_count = 0;
    if (!t) return NULL;
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef mrec = refl_method_type(g);

    zan_type_t *elem_type = (t->type_arg_count > 0 && t->type_args)
        ? t->type_args[0] : g->binder->type_object;

    LLVMValueRef count_thunk = refl_make_list_count_thunk(g);
    LLVMValueRef get_thunk = refl_make_list_get_thunk(g, elem_type);
    LLVMValueRef add_thunk = refl_make_list_add_thunk(g, elem_type);

    /* 参数名 index */
    LLVMValueRef pnames[1] = { refl_const_string(g, "index", 5) };
    LLVMTypeRef pat = LLVMArrayType(i8ptr, 1);
    char pnm[64];
    snprintf(pnm, sizeof(pnm), "__zan.refl.pnames.list.%d", g->refl_str_count++);
    LLVMValueRef pgv = LLVMAddGlobal(g->mod, pat, pnm);
    LLVMSetInitializer(pgv, LLVMConstArray(i8ptr, pnames, 1));
    LLVMSetLinkage(pgv, LLVMPrivateLinkage);
    LLVMSetGlobalConstant(pgv, 1);
    LLVMValueRef pidx[2] = { LLVMConstInt(i64, 0, 0), LLVMConstInt(i64, 0, 0) };
    LLVMValueRef param_names_ptr = LLVMConstGEP2(pat, pgv, pidx, 2);

    LLVMValueRef recs[4];
    /* 0: Count() -> int */
    recs[0] = LLVMConstNamedStruct(mrec, (LLVMValueRef[]){
        refl_const_string(g, "Count", 5),
        refl_field_type_name(g, g->binder->type_int),
        LLVMConstInt(i64, (unsigned long long)refl_field_kind(g->binder->type_int), 0),
        LLVMConstInt(i64, 0, 0),
        LLVMConstNull(i8ptr),
        count_thunk,
        LLVMConstInt(i64, 0, 0),
        refl_empty_string(g) }, 8);

    /* 1: Get(int index) -> elem_type */
    recs[1] = LLVMConstNamedStruct(mrec, (LLVMValueRef[]){
        refl_const_string(g, "Get", 3),
        refl_field_type_name(g, elem_type),
        LLVMConstInt(i64, (unsigned long long)refl_field_kind(elem_type), 0),
        LLVMConstInt(i64, 1, 0),
        param_names_ptr,
        get_thunk,
        LLVMConstInt(i64, 0, 0),
        refl_empty_string(g) }, 8);

    /* 2: At(int index) -> elem_type */
    recs[2] = LLVMConstNamedStruct(mrec, (LLVMValueRef[]){
        refl_const_string(g, "At", 2),
        refl_field_type_name(g, elem_type),
        LLVMConstInt(i64, (unsigned long long)refl_field_kind(elem_type), 0),
        LLVMConstInt(i64, 1, 0),
        param_names_ptr,
        get_thunk,
        LLVMConstInt(i64, 0, 0),
        refl_empty_string(g) }, 8);

    LLVMValueRef value_name = refl_field_type_name(g, elem_type);
    snprintf(pnm, sizeof(pnm), "__zan.refl.pnames.list.%d", g->refl_str_count++);
    LLVMValueRef vgv = LLVMAddGlobal(g->mod, pat, pnm);
    LLVMSetInitializer(vgv, LLVMConstArray(i8ptr, &value_name, 1));
    LLVMSetLinkage(vgv, LLVMPrivateLinkage);
    LLVMSetGlobalConstant(vgv, 1);
    recs[3] = LLVMConstNamedStruct(mrec, (LLVMValueRef[]){
        refl_const_string(g, "Add", 3),
        refl_field_type_name(g, g->binder->type_void),
        LLVMConstInt(i64, (unsigned long long)refl_field_kind(g->binder->type_void), 0),
        LLVMConstInt(i64, 1, 0), LLVMConstGEP2(pat, vgv, pidx, 2),
        add_thunk, LLVMConstInt(i64, 0, 0), refl_empty_string(g) }, 8);

    LLVMTypeRef arr_ty = LLVMArrayType(mrec, 4);
    char nm[96];
    snprintf(nm, sizeof(nm), "__zan.refl.mtab.List.%d", g->refl_meta_count);
    LLVMValueRef gv = LLVMAddGlobal(g->mod, arr_ty, nm);
    LLVMSetInitializer(gv, LLVMConstArray(mrec, recs, 4));
    LLVMSetLinkage(gv, LLVMPrivateLinkage);
    LLVMSetGlobalConstant(gv, 1);
    *out_count = 4;
    return LLVMConstBitCast(gv, i8ptr);
}

static LLVMValueRef refl_mtab_init(zan_irgen_t *g, zan_symbol_t *sym,
                                   bool ctors, int n, bool late) {
    LLVMTypeRef mrec = refl_method_type(g);
    LLVMValueRef *recs = (LLVMValueRef *)calloc((size_t)n, sizeof(LLVMValueRef));
    if (!recs) return NULL;
    if (ctors) {
        zan_ast_node_t **decls = (zan_ast_node_t **)calloc((size_t)n,
                                                    sizeof(zan_ast_node_t *));
        int got = decls ? refl_collect_ctors(sym, decls, n) : 0;
        for (int i = 0; i < n; i++)
            recs[i] = i < got ? refl_ctor_record(g, sym, decls[i], late)
                              : (got == 0 && i == 0 ? refl_ctor_record(g, sym, NULL, late)
                                                    : LLVMConstNull(mrec));
        free(decls);
    } else {
        zan_symbol_t **msyms = (zan_symbol_t **)calloc((size_t)n,
                                                    sizeof(zan_symbol_t *));
        int got = msyms ? refl_collect_methods(g, sym, msyms, n) : 0;
        if (got > n) got = n;
        for (int i = 0; i < n; i++)
            recs[i] = i < got ? refl_method_record(g, sym, msyms[i], late)
                              : LLVMConstNull(mrec);
        free(msyms);
    }
    LLVMValueRef init = LLVMConstArray(mrec, recs, (unsigned)n);
    free(recs);
    return init;
}

static LLVMValueRef refl_shape_mtab(zan_irgen_t *g, zan_symbol_t *sym,
                                    bool ctors, int *out_count) {
    *out_count = 0;
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    if (!sym) return LLVMConstNull(i8ptr);
    int n = 0;
    if (ctors) {
        if (!sym->decl) return LLVMConstNull(i8ptr);
        zan_ast_list_t *ms = &sym->decl->type_decl.members;
        for (int i = 0; i < ms->count; i++)
            if (ms->items[i]->kind == AST_CONSTRUCTOR_DECL &&
                !(ms->items[i]->method_decl.modifiers & MOD_STATIC))
                n++;
        if (n == 0 && sym->type && (sym->type->kind == TYPE_CLASS || sym->type->kind == TYPE_STRUCT))
            n = 1;
    } else {
        n = refl_methods_walk(g, sym, NULL, 0);
    }
    if (n <= 0) return LLVMConstNull(i8ptr);

    LLVMTypeRef arr_ty = LLVMArrayType(refl_method_type(g), (unsigned)n);
    char nm[128];
    snprintf(nm, sizeof(nm), "__zan.refl.%s.%.*s.%d", ctors ? "ctors" : "mtab",
             (int)sym->name.len, sym->name.str, g->refl_mtab_count);
    LLVMValueRef gv = LLVMAddGlobal(g->mod, arr_ty, nm);
    LLVMSetInitializer(gv, LLVMConstNull(arr_ty));
    LLVMSetLinkage(gv, LLVMPrivateLinkage);
    LLVMSetAlignment(gv, 8);

    if (g->refl_mtab_count == g->refl_mtab_cap) {
        int ncap = g->refl_mtab_cap ? g->refl_mtab_cap * 2 : 16;
        g->refl_mtabs = (typeof(g->refl_mtabs))realloc(g->refl_mtabs,
            (size_t)ncap * sizeof(*g->refl_mtabs));
        g->refl_mtab_cap = ncap;
    }
    g->refl_mtabs[g->refl_mtab_count].gv = gv;
    g->refl_mtabs[g->refl_mtab_count].arr_ty = arr_ty;
    g->refl_mtabs[g->refl_mtab_count].sym = sym;
    g->refl_mtabs[g->refl_mtab_count].n = n;
    g->refl_mtabs[g->refl_mtab_count].ctors = ctors;
    g->refl_mtabs[g->refl_mtab_count].site = -1;
    g->refl_mtab_count++;
    *out_count = n;
    return LLVMConstBitCast(gv, LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0));
}

static void refl_pack_val_str(zan_ast_node_t *r, char *buf, size_t cap) {
    if (!r || cap <= 1) { if (cap > 0) buf[0] = 0; return; }
    if (r->kind == AST_STRING_LITERAL) {
        snprintf(buf, cap, "%s", r->str_val.str ? r->str_val.str : "");
    } else if (r->kind == AST_BOOL_LITERAL) {
        snprintf(buf, cap, "%s", r->bool_val ? "true" : "false");
    } else if (r->kind == AST_INT_LITERAL) {
        snprintf(buf, cap, "%lld", (long long)r->int_val);
    } else if (r->kind == AST_MEMBER_ACCESS && r->member.name.str) {
        if (r->member.object && r->member.object->kind == AST_IDENTIFIER && r->member.object->ident.name.str) {
            snprintf(buf, cap, "%.*s.%.*s",
                     (int)r->member.object->ident.name.len, r->member.object->ident.name.str,
                     (int)r->member.name.len, r->member.name.str);
        } else {
            snprintf(buf, cap, "%.*s", (int)r->member.name.len, r->member.name.str);
        }
    } else if (r->kind == AST_IDENTIFIER && r->ident.name.str) {
        snprintf(buf, cap, "%.*s", (int)r->ident.name.len, r->ident.name.str);
    } else if (r->kind == AST_BINARY && r->binary.left && r->binary.right) {
        char left[128], right[128];
        refl_pack_val_str(r->binary.left, left, sizeof(left));
        refl_pack_val_str(r->binary.right, right, sizeof(right));
        const char *op = "|";
        if (r->binary.op == TK_PIPE) op = "|";
        else if (r->binary.op == TK_AMP) op = "&";
        else if (r->binary.op == TK_PLUS) op = "+";
        snprintf(buf, cap, "%s%s%s", left, op, right);
    } else {
        buf[0] = 0;
    }
}

typedef struct {
    char name[64];
    char type[32];
    bool required;
    char def[64];
    char desc[128];
    char where[32];
} refl_param_entry_t;

typedef struct {
    refl_param_entry_t items[32];
    int count;
} refl_param_list_t;

typedef struct {
    const char *fn;
    const char *type;
    bool required;
    bool has_default;
    bool has_label;
    const char *where;
} refl_rg_reader_t;

static const refl_rg_reader_t s_refl_readers[] = {
    { "In",        "string", false, false, false, "query" },
    { "InText",    "string", false, false, false, "query" },
    { "InRaw",     "string", false, false, false, "query" },
    { "InInt",     "int",    false, true,  false, "query" },
    { "InLong",    "long",   false, true,  false, "query" },
    { "InDouble",  "number", false, true,  false, "query" },
    { "InBool",    "bool",   false, true,  false, "query" },
    { "HasIn",     "bool",   false, false, false, "query" },
    { "Need",      "string", true,  false, true,  "query" },
    { "NeedText",  "string", true,  false, true,  "query" },
    { "NeedInt",   "int",    true,  false, true,  "query" },
    { "NeedLong",  "long",   true,  false, true,  "query" },
    { "Param",     "string", true,  false, false, "path"  },
    { NULL, NULL, false, false, false, NULL },
};

static inline bool refl_istr_is(zan_istr_t s, const char *lit) {
    size_t len = strlen(lit);
    return s.len == len && memcmp(s.str, lit, len) == 0;
}

static inline void refl_istr_to(char *out, size_t cap, zan_istr_t s) {
    if (!cap) return;
    size_t n = s.len < cap - 1 ? s.len : cap - 1;
    if (s.str && n) memcpy(out, s.str, n);
    out[n] = 0;
}

static void refl_rg_literal(zan_ast_node_t *e, char *out, size_t cap) {
    out[0] = 0;
    if (!e || cap <= 1) return;
    bool neg = false;
    if (e->kind == AST_UNARY && e->unary.operand) {
        neg = true;
        e = e->unary.operand;
    }
    if (e->kind == AST_INT_LITERAL) {
        snprintf(out, cap, "%s%lld", neg ? "-" : "", (long long)e->int_val);
    } else if (e->kind == AST_FLOAT_LITERAL) {
        snprintf(out, cap, "%s%g", neg ? "-" : "", e->float_val);
    } else if (e->kind == AST_BOOL_LITERAL) {
        snprintf(out, cap, "%s", e->bool_val ? "true" : "false");
    } else if (e->kind == AST_STRING_LITERAL) {
        refl_istr_to(out, cap, e->str_val);
    }
}

static bool refl_is_input_receiver(zan_ast_node_t *obj) {
    if (!obj) return false;
    if (obj->kind == AST_THIS_EXPR) return true;
    if (obj->kind == AST_IDENTIFIER)
        return refl_istr_is(obj->ident.name, "ctx") || refl_istr_is(obj->ident.name, "c");
    if (obj->kind == AST_CALL && obj->call.callee &&
        obj->call.callee->kind == AST_MEMBER_ACCESS &&
        refl_istr_is(obj->call.callee->member.name, "Ctx"))
        return true;
    return false;
}

static void refl_rg_params_add(refl_param_list_t *ps, const refl_rg_reader_t *rd, zan_ast_node_t *call) {
    if (ps->count >= (int)(sizeof(ps->items) / sizeof(ps->items[0]))) return;
    if (call->call.args.count < 1) return;
    zan_ast_node_t *a0 = call->call.args.items[0];
    if (a0->kind != AST_STRING_LITERAL) return;

    char nm[64]; refl_istr_to(nm, sizeof(nm), a0->str_val);
    if (!nm[0]) return;

    for (int i = 0; i < ps->count; i++) {
        if (strcmp(ps->items[i].name, nm) != 0) continue;
        refl_param_entry_t *p = &ps->items[i];
        if (rd->required) p->required = true;
        if (rd->has_label && call->call.args.count >= 2 && !p->desc[0])
            refl_rg_literal(call->call.args.items[1], p->desc, sizeof(p->desc));
        if (rd->has_default && call->call.args.count >= 2 && !p->def[0])
            refl_rg_literal(call->call.args.items[1], p->def, sizeof(p->def));
        if (strcmp(p->type, "string") == 0) snprintf(p->type, sizeof(p->type), "%s", rd->type);
        return;
    }

    refl_param_entry_t *p = &ps->items[ps->count++];
    memset(p, 0, sizeof(*p));
    snprintf(p->name, sizeof(p->name), "%s", nm);
    snprintf(p->type, sizeof(p->type), "%s", rd->type);
    p->required = rd->required;
    snprintf(p->where, sizeof(p->where), "%s", rd->where);
    if (call->call.args.count >= 2) {
        if (rd->has_default) refl_rg_literal(call->call.args.items[1], p->def, sizeof(p->def));
        if (rd->has_label) refl_rg_literal(call->call.args.items[1], p->desc, sizeof(p->desc));
    }
}

