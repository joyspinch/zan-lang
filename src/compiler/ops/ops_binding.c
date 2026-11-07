/* Binding<T> 属性与字段双向绑定原语降解 */
int type_is_binding(zan_type_t *t) {
    return t && t->kind == TYPE_CLASS && t->name.len == 7 &&
           memcmp(t->name.str, "Binding", 7) == 0;
}

/* 泛型类上的委托字段签名特化与解析 */
zan_type_t *subst_delegate_sig(zan_irgen_t *g, zan_type_t *t,
                                      zan_type_t *recv) {
    if (!t || t->kind != TYPE_DELEGATE || !recv || recv->type_arg_count <= 0)
        return t;
    bool needs = (t->delegate_ret_type && type_mentions_tp(t->delegate_ret_type));
    for (int i = 0; !needs && i < t->delegate_param_count; i++)
        needs = (t->delegate_param_types[i] && type_mentions_tp(t->delegate_param_types[i]));
    for (int i = 0; !needs && i < t->type_arg_count; i++)
        needs = (t->type_args[i] && type_mentions_tp(t->type_args[i]));
    if (!needs) return t;

    zan_type_t *out = (zan_type_t *)zan_arena_alloc(g->arena, sizeof(zan_type_t));
    *out = *t;
    out->delegate_ret_type = subst_type_param_deep(g, t->delegate_ret_type, recv);
    if (t->delegate_param_count > 0) {
        out->delegate_param_types = (zan_type_t **)zan_arena_alloc(g->arena,
            sizeof(zan_type_t *) * (size_t)t->delegate_param_count);
        for (int i = 0; i < t->delegate_param_count; i++)
            out->delegate_param_types[i] =
                subst_type_param_deep(g, t->delegate_param_types[i], recv);
    }
    if (t->type_arg_count > 0) {
        out->type_args = (zan_type_t **)zan_arena_alloc(g->arena,
            sizeof(zan_type_t *) * (size_t)t->type_arg_count);
        for (int i = 0; i < t->type_arg_count; i++)
            out->type_args[i] = subst_type_param_deep(g, t->type_args[i], recv);
    }
    return out;
}

static zan_type_t *assign_lhs_type(zan_irgen_t *g, zan_ast_node_t *lhs,
                                   local_scope_t *locals) {
    if (!lhs) return NULL;
    if (lhs->kind == AST_IDENTIFIER) {
        local_var_t *lv = local_find(locals, lhs->ident.name);
        if (lv) return lv->type;
        if (g->current_type_sym) {
            zan_symbol_t *fs = get_field_sym(g->current_type_sym, lhs->ident.name);
            if (fs) return subst_delegate_sig(g, fs->type, g->cur_inst);
        }
        return NULL;
    }
    if (lhs->kind == AST_MEMBER_ACCESS)
        return subst_delegate_sig(g, member_access_field_type(g, locals, lhs),
                                  infer_expr_type(g, lhs->member.object, locals));
    return NULL;
}

/* 为类字段合成或查询缓存的 getter/setter 访问器函数 */
static void get_binding_accessors(zan_irgen_t *g, zan_symbol_t *cls,
        zan_symbol_t *fs, LLVMValueRef *out_get, LLVMValueRef *out_set) {
    *out_get = NULL;
    *out_set = NULL;
    for (int i = 0; i < g->bind_acc_count; i++) {
        if (g->bind_accs[i].cls == cls && g->bind_accs[i].field == fs) {
            *out_get = g->bind_accs[i].get_fn;
            *out_set = g->bind_accs[i].set_fn;
            return;
        }
    }
    LLVMTypeRef st = get_struct_llvm_type(g, cls);
    int fi = get_field_index(cls, fs->name);
    if (!st || fi < 0 || !fs->type) return;
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef vt = map_type(g, fs->type);

    LLVMBasicBlockRef saved_bb = LLVMGetInsertBlock(g->builder);
    LLVMValueRef saved_fn = g->current_fn;

    char name[512];
    snprintf(name, sizeof(name), "__zbind_get_%.*s_%.*s",
             (int)cls->name.len, cls->name.str, (int)fs->name.len, fs->name.str);
    LLVMTypeRef get_ty = LLVMFunctionType(vt, &i8ptr, 1, 0);
    LLVMValueRef get_fn = LLVMAddFunction(g->mod, name, get_ty);
    LLVMSetLinkage(get_fn, LLVMInternalLinkage);
    {
        LLVMBasicBlockRef bb = LLVMAppendBasicBlockInContext(g->ctx, get_fn, "entry");
        LLVMPositionBuilderAtEnd(g->builder, bb);
        g->current_fn = get_fn;
        LLVMValueRef obj = LLVMBuildBitCast(g->builder, LLVMGetParam(get_fn, 0),
            LLVMPointerType(st, 0), "obj");
        LLVMValueRef fptr = emit_field_ptr(g, cls, st, obj, fi, "fld");
        LLVMValueRef val;
        if (fs->modifiers & MOD_WEAK) {
            val = emit_weak_field_load(g, fptr, vt);
        } else {
            val = LLVMBuildLoad2(g->builder, vt, fptr, "val");
            if (is_rc_managed_type(fs->type))
                emit_rc_retain_for_type(g, fs->type, val);
        }
        LLVMBuildRet(g->builder, val);
    }

    snprintf(name, sizeof(name), "__zbind_set_%.*s_%.*s",
             (int)cls->name.len, cls->name.str, (int)fs->name.len, fs->name.str);
    LLVMTypeRef set_params[2] = { i8ptr, vt };
    LLVMTypeRef set_ty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), set_params, 2, 0);
    LLVMValueRef set_fn = LLVMAddFunction(g->mod, name, set_ty);
    LLVMSetLinkage(set_fn, LLVMInternalLinkage);
    {
        LLVMBasicBlockRef bb = LLVMAppendBasicBlockInContext(g->ctx, set_fn, "entry");
        LLVMPositionBuilderAtEnd(g->builder, bb);
        g->current_fn = set_fn;
        LLVMValueRef obj = LLVMBuildBitCast(g->builder, LLVMGetParam(set_fn, 0),
            LLVMPointerType(st, 0), "obj");
        LLVMValueRef v = LLVMGetParam(set_fn, 1);
        LLVMValueRef fptr = emit_field_ptr(g, cls, st, obj, fi, "fld");
        /* 弱引用字段写入防护：维护弱引用注册表槽位 */
        emit_rc_store_field(g, fs->type, fptr, v, NULL, NULL,
                            (fs->modifiers & MOD_WEAK) ? 1 : 0);
        LLVMBuildRetVoid(g->builder);
    }

    g->current_fn = saved_fn;
    if (saved_bb) LLVMPositionBuilderAtEnd(g->builder, saved_bb);

    if (ZAN_TAB_ENSURE(g->bind_accs, g->bind_acc_count, g->bind_acc_cap, 128)) {
        g->bind_accs[g->bind_acc_count].cls = cls;
        g->bind_accs[g->bind_acc_count].field = fs;
        g->bind_accs[g->bind_acc_count].get_fn = get_fn;
        g->bind_accs[g->bind_acc_count].set_fn = set_fn;
        g->bind_acc_count++;
    }
    *out_get = get_fn;
    *out_set = set_fn;
}

/* 为右值构建 Binding<T> 实例对象（返回 +1 持有引用） */
LLVMValueRef emit_binding_value(zan_irgen_t *g, zan_type_t *bind_t,
        zan_ast_node_t *rhs, local_scope_t *locals) {
    /* b = null 清除数据绑定状态 */
    if (rhs && rhs->kind == AST_NULL_LITERAL) return NULL;
    zan_symbol_t *bsym = bind_t->sym;
    if (!bsym) {
        zan_istr_t bn = { (char *)"Binding", 7 };
        bsym = zan_binder_lookup(g->binder, bn);
    }
    if (!bsym) return NULL;
    LLVMTypeRef st = get_struct_llvm_type(g, bsym);
    if (!st) return NULL;
    zan_istr_t n_target = { (char *)"target", 6 };
    zan_istr_t n_getter = { (char *)"getter", 6 };
    zan_istr_t n_setter = { (char *)"setter", 6 };
    zan_istr_t n_live = { (char *)"live", 4 };
    zan_istr_t n_const = { (char *)"constVal", 8 };
    int fi_target = get_field_index(bsym, n_target);
    int fi_getter = get_field_index(bsym, n_getter);
    int fi_setter = get_field_index(bsym, n_setter);
    int fi_live = get_field_index(bsym, n_live);
    int fi_const = get_field_index(bsym, n_const);
    if (fi_target < 0 || fi_getter < 0 || fi_setter < 0 ||
        fi_live < 0 || fi_const < 0) return NULL;
    zan_type_t *T = bind_t->type_arg_count > 0 ? bind_t->type_args[0] : NULL;

    /* 类内部裸字段名解析为 this 字段并绑定目标 */
    if (rhs && rhs->kind == AST_IDENTIFIER && g->current_this &&
        g->current_type_sym && !local_find(locals, rhs->ident.name)) {
        zan_symbol_t *own = get_field_sym(g->current_type_sym, rhs->ident.name);
        if (own && !field_member_is_static(own)) {
            zan_ast_node_t *self = zan_ast_new(g->arena, AST_THIS_EXPR, rhs->loc);
            zan_ast_node_t *ma = zan_ast_new(g->arena, AST_MEMBER_ACCESS, rhs->loc);
            ma->member.object = self;
            ma->member.name = rhs->ident.name;
            ma->member.null_cond = 0;
            rhs = ma;
        }
    }

    zan_symbol_t *cls = NULL;
    zan_symbol_t *ffs = NULL;
    if (rhs && rhs->kind == AST_MEMBER_ACCESS && !rhs->member.null_cond) {
        cls = expr_class_sym(g, rhs->member.object, locals);
        if (cls) {
            ffs = get_field_sym(cls, rhs->member.name);
            if (ffs && field_member_is_static(ffs)) ffs = NULL;
            if (ffs && !ffs->type) ffs = NULL;
            if (ffs && T && ffs->type->kind != T->kind) ffs = NULL;
            /* 绑定目标使用非持有弱引用，防止双向绑定循环引用导致内存泄漏 */
            if (ffs) {
                zan_type_t *ot = infer_expr_type(g, rhs->member.object, locals);
                if (ot && is_rc_managed_type(ot) &&
                    expr_yields_owned_rc_value(g, rhs->member.object, locals))
                    ffs = NULL;
            }
        }
    }
    LLVMValueRef get_fn = NULL, set_fn = NULL;
    if (ffs) {
        get_binding_accessors(g, cls, ffs, &get_fn, &set_fn);
        if (!get_fn || !set_fn) ffs = NULL;
    }

    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);

    LLVMValueRef site_name = LLVMConstNull(i8ptr);
    LLVMValueRef site_val;
    {
        int site_idx = reserve_arc_site(g, bsym, bind_t, 0, NULL);
        site_val = arc_site_arg(g, site_idx);
        if (g->check_leaks) {
            char site_buf[600];
            const char *sfile = loc_site_file(g, rhs->loc);
            snprintf(site_buf, sizeof(site_buf), "%s:%u:%u",
                     sfile, rhs->loc.line, rhs->loc.col);
            site_name = zan_irgen_intern_string(g, site_buf);
        }
    }
    LLVMTypeRef alloc_fn_type = LLVMFunctionType(i8ptr,
        (LLVMTypeRef[]){ i64, i64, i8ptr }, 3, 0);
    LLVMValueRef alloc_args[] = { LLVMSizeOf(st), site_val, site_name };
    LLVMValueRef raw = zan_call2(g->builder, alloc_fn_type, g->rt_alloc,
        alloc_args, 3, "bindobj");
    LLVMValueRef objp = LLVMBuildBitCast(g->builder, raw, LLVMPointerType(st, 0), "bindp");
    zan_store_fit(g, LLVMConstNull(st), objp);

    LLVMValueRef live_ptr = LLVMBuildStructGEP2(g->builder, st, objp, (unsigned)fi_live, "b.live");
    LLVMTypeRef live_t = LLVMStructGetTypeAtIndex(st, (unsigned)fi_live);

    if (ffs) {
        LLVMValueRef obj_val = emit_expr(g, rhs->member.object, locals);
        LLVMValueRef tptr = LLVMBuildStructGEP2(g->builder, st, objp, (unsigned)fi_target, "b.tgt");
        LLVMTypeRef tgt_t = LLVMStructGetTypeAtIndex(st, (unsigned)fi_target);
        LLVMValueRef obj_cast = obj_val;
        if (LLVMTypeOf(obj_cast) != tgt_t &&
            LLVMGetTypeKind(LLVMTypeOf(obj_cast)) == LLVMPointerTypeKind)
            obj_cast = LLVMBuildBitCast(g->builder, obj_cast, tgt_t, "b.tgt.bc");
        /* 非持有弱引用绑定目标管理 */
        zan_store_fit(g, obj_cast, tptr);

        LLVMValueRef gptr = LLVMBuildStructGEP2(g->builder, st, objp, (unsigned)fi_getter, "b.get");
        LLVMTypeRef gslot_t = LLVMStructGetTypeAtIndex(st, (unsigned)fi_getter);
        zan_store_fit(g,
            LLVMBuildBitCast(g->builder,
                emit_binding_acc_delegate(g, get_fn,
                    LLVMGlobalGetValueType(get_fn)),
                gslot_t, "b.get.bc"), gptr);
        LLVMValueRef sptr = LLVMBuildStructGEP2(g->builder, st, objp, (unsigned)fi_setter, "b.set");
        LLVMTypeRef sslot_t = LLVMStructGetTypeAtIndex(st, (unsigned)fi_setter);
        zan_store_fit(g,
            LLVMBuildBitCast(g->builder,
                emit_binding_acc_delegate(g, set_fn,
                    LLVMGlobalGetValueType(set_fn)),
                sslot_t, "b.set.bc"), sptr);
        zan_store_fit(g, LLVMConstInt(live_t, 1, 0), live_ptr);
    } else {
        LLVMValueRef v = emit_expr(g, rhs, locals);
        LLVMValueRef cptr = LLVMBuildStructGEP2(g->builder, st, objp, (unsigned)fi_const, "b.cv");
        LLVMTypeRef cslot_t = LLVMStructGetTypeAtIndex(st, (unsigned)fi_const);
        LLVMTypeKind vk = LLVMGetTypeKind(LLVMTypeOf(v));
        if (LLVMGetTypeKind(cslot_t) == LLVMIntegerTypeKind &&
            vk == LLVMIntegerTypeKind &&
            LLVMGetIntTypeWidth(LLVMTypeOf(v)) != LLVMGetIntTypeWidth(cslot_t)) {
            v = coerce_int_to(g, v, cslot_t);
        } else if (vk == LLVMPointerTypeKind &&
                   LLVMGetTypeKind(cslot_t) == LLVMPointerTypeKind &&
                   LLVMTypeOf(v) != cslot_t) {
            v = LLVMBuildBitCast(g->builder, v, cslot_t, "b.cv.bc");
        }
        if (T && is_rc_managed_type(T) &&
            !expr_yields_owned_rc_value(g, rhs, locals))
            emit_rc_retain_for_type(g, T, v);
        zan_store_fit(g, v, cptr);
        zan_store_fit(g, LLVMConstInt(live_t, 0, 0), live_ptr);
    }
    return LLVMBuildBitCast(g->builder, objp, i8ptr, "bindv");
}

/* 控制台颜色属性赋值跟踪槽 */
#define ZAN_CONSOLE_FG_DEFAULT 7 /* ConsoleColor.Gray */
#define ZAN_CONSOLE_BG_DEFAULT 0 /* ConsoleColor.Black */

static LLVMValueRef console_color_slot(zan_irgen_t *g, int is_bg) {
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    const char *nm = is_bg ? "__zan_console_bg" : "__zan_console_fg";
    LLVMValueRef slot = LLVMGetNamedGlobal(g->mod, nm);
    if (!slot) {
        slot = LLVMAddGlobal(g->mod, i64, nm);
        LLVMSetInitializer(slot, LLVMConstInt(i64,
            is_bg ? ZAN_CONSOLE_BG_DEFAULT : ZAN_CONSOLE_FG_DEFAULT, 0));
        LLVMSetLinkage(slot, LLVMPrivateLinkage);
    }
    return slot;
}
LLVMValueRef emit_console_color_get(zan_irgen_t *g, int is_bg) {
    return LLVMBuildLoad2(g->builder, LLVMInt64TypeInContext(g->ctx),
                          console_color_slot(g, is_bg), "concolor");
}
void emit_console_color_reset(zan_irgen_t *g) {
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMBuildStore(g->builder,
        LLVMConstInt(i64, ZAN_CONSOLE_FG_DEFAULT, 0), console_color_slot(g, 0));
    LLVMBuildStore(g->builder,
        LLVMConstInt(i64, ZAN_CONSOLE_BG_DEFAULT, 0), console_color_slot(g, 1));
}

/* 为控制台前景色/背景色赋值发射 ANSI 转义序列 */
static void emit_console_color(zan_irgen_t *g, LLVMValueRef color, int is_bg) {
    LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef arrTy = LLVMArrayType(i32, 16);
    const char *nm = is_bg ? "__zan_ansi_bg" : "__zan_ansi_fg";
    LLVMValueRef tbl = LLVMGetNamedGlobal(g->mod, nm);
    if (!tbl) {
        static const int fg[16] = { 30,34,32,36,31,35,33,37,
                                    90,94,92,96,91,95,93,97 };
        LLVMValueRef vals[16];
        for (int i = 0; i < 16; i++)
            vals[i] = LLVMConstInt(i32, (unsigned)(fg[i] + (is_bg ? 10 : 0)), 0);
        tbl = LLVMAddGlobal(g->mod, arrTy, nm);
        LLVMSetInitializer(tbl, LLVMConstArray(i32, vals, 16));
        LLVMSetGlobalConstant(tbl, 1);
        LLVMSetLinkage(tbl, LLVMPrivateLinkage);
    }
    if (LLVMGetTypeKind(LLVMTypeOf(color)) != LLVMIntegerTypeKind)
        color = LLVMConstInt(i64, 0, 0);
    else if (LLVMGetIntTypeWidth(LLVMTypeOf(color)) < 64)
        color = LLVMBuildSExt(g->builder, color, i64, "csx");
    LLVMValueRef idx = zan_and(g->builder, color, LLVMConstInt(i64, 15, 0), "cidx");
    LLVMBuildStore(g->builder, idx, console_color_slot(g, is_bg));
    LLVMValueRef gep = LLVMBuildInBoundsGEP2(g->builder, arrTy, tbl,
        (LLVMValueRef[]){ LLVMConstInt(i64, 0, 0), idx }, 2, "ansip");
    LLVMValueRef code = LLVMBuildLoad2(g->builder, i32, gep, "ansicode");
    LLVMTypeRef pty = LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr }, 1, 1);
    LLVMValueRef pf = LLVMGetNamedFunction(g->mod, "printf");
    if (!pf) pf = LLVMAddFunction(g->mod, "printf", pty);
    LLVMValueRef fmt = zan_irgen_intern_string(g, "\033[%dm");
    zan_call2(g->builder, pty, pf, (LLVMValueRef[]){ fmt, code }, 2, "");
}

/* 通过 ANSI/VT OSC 序列设置控制台窗口标题 */
static void emit_console_title(zan_irgen_t *g, LLVMValueRef s) {
    LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef pty = LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr }, 1, 1);
    LLVMValueRef pf = LLVMGetNamedFunction(g->mod, "printf");
    if (!pf) pf = LLVMAddFunction(g->mod, "printf", pty);
    LLVMValueRef fmt = zan_irgen_intern_string(g, "\033]0;%s\007");
    zan_call2(g->builder, pty, pf, (LLVMValueRef[]){ fmt, s }, 2, "");
}

/* 字段写入操作所需处理的真实存储类型推导 */
zan_type_t *field_store_type(zan_irgen_t *g, zan_symbol_t *fsym,
                                    zan_type_t *recv) {
    zan_type_t *t = field_type_at(g, fsym, recv);
    if (!t) return NULL;
    return concretize(g, t);
}

static zan_symbol_t *field_sym_for_decl(zan_symbol_t *type_sym,
                                        zan_ast_node_t *decl) {
    if (!type_sym || !decl) return NULL;
    for (int i = 0; i < type_sym->member_count; i++) {
        zan_symbol_t *member = type_sym->members[i];
        if ((member->kind == SYM_FIELD || member->kind == SYM_PROPERTY) &&
            member->decl == decl) return member;
    }
    return NULL;
}
void emit_decl_field_initializers(zan_irgen_t *g,
                                         zan_symbol_t *type_sym,
                                         zan_type_t *recv_type,
                                         LLVMValueRef object_ptr,
                                         local_scope_t *locals) {
    if (!type_sym || !type_sym->decl || !object_ptr) return;
    zan_ast_node_t *decl = type_sym->decl;
    if (decl->kind != AST_CLASS_DECL && decl->kind != AST_STRUCT_DECL) return;
    LLVMTypeRef st = get_struct_llvm_type(g, type_sym);
    if (!st) return;
    LLVMTypeRef object_type = LLVMPointerType(st, 0);
    object_ptr = emit_boundary_coerce(g, object_ptr, object_type);

    zan_symbol_t *saved_type_sym = g->current_type_sym;
    LLVMValueRef saved_this = g->current_this;
    zan_type_t *saved_inst = g->cur_inst;
    LLVMValueRef this_slot = emit_entry_alloca(g, object_type, "field.init.this");
    LLVMBuildStore(g->builder, object_ptr, this_slot);
    g->current_type_sym = type_sym;
    g->current_this = this_slot;
    g->cur_inst = recv_type;

    for (int i = 0; i < decl->type_decl.members.count; i++) {
        zan_ast_node_t *field = decl->type_decl.members.items[i];
        if (!field || (field->kind != AST_FIELD_DECL &&
                       field->kind != AST_PROPERTY_DECL) ||
            /* 自定义访问器属性无后备存储槽，通过方法调用存取 */
            (field->kind == AST_PROPERTY_DECL &&
             (field->field_decl.getter_body || field->field_decl.setter_body)) ||
            (field->field_decl.modifiers & MOD_STATIC) ||
            !field->field_decl.initializer) continue;
        zan_symbol_t *fsym = field_sym_for_decl(type_sym, field);
        if (!fsym) continue;
        int fi = get_field_index(type_sym, field->field_decl.name);
        if (fi < 0) continue;
        zan_type_t *field_type = field_store_type(g, fsym, recv_type);
        zan_type_t *source_type = infer_expr_type(
            g, field->field_decl.initializer, locals);
        check_implicit_narrowing(g, field_type, source_type,
                                 field->field_decl.initializer,
                                 "field initializer");
        check_value_type_mismatch(g, field_type, source_type,
                                  field->field_decl.initializer,
                                  "field initializer");
        check_generic_invariance(g, field_type, source_type,
                                 field->field_decl.initializer,
                                 "field initializer");
        LLVMValueRef value = field_type && field_type->kind == TYPE_DELEGATE &&
                             field->field_decl.initializer->kind == AST_LAMBDA
            ? emit_lambda_typed(g, field->field_decl.initializer,
                                field_type, locals)
            : emit_expr(g, field->field_decl.initializer, locals);
        LLVMValueRef field_ptr = emit_field_ptr(
            g, type_sym, st, object_ptr, fi, "field.init");
        LLVMTypeRef slot_type = map_type(g, fsym->type);
        value = emit_boundary_coerce(g, value, slot_type);
        if (field_type && (is_rc_managed_type(field_type) || field_type->kind == TYPE_OBJECT)) {
            emit_rc_store_field(g, field_type, field_ptr, value,
                                field->field_decl.initializer, locals,
                                (fsym->modifiers & MOD_WEAK) ? 1 : 0);
        } else {
            zan_store_fit(g, value, field_ptr);
        }
    }

    g->cur_inst = saved_inst;
    g->current_this = saved_this;
    g->current_type_sym = saved_type_sym;
}
void emit_implicit_field_initializers(zan_irgen_t *g,
                                             zan_symbol_t *type_sym,
                                             zan_type_t *recv_type,
                                             LLVMValueRef object_ptr,
                                             local_scope_t *locals) {
    if (!type_sym) return;
    zan_type_t *base_type = type_sym->type ? type_sym->type->base_type : NULL;
    if (base_type && base_type->sym && base_type->sym != type_sym) {
        emit_implicit_field_initializers(g, base_type->sym, base_type,
                                         object_ptr, locals);
    }
    emit_decl_field_initializers(g, type_sym, recv_type, object_ptr, locals);
}

/* 判断接收者表达式是否持有消费方需释放的所有权引用 */
int receiver_is_owned_temp(zan_irgen_t *g, zan_ast_node_t *object,
                                  local_scope_t *locals, zan_type_t *obj_type,
                                  LLVMValueRef obj_val) {
    return obj_val && obj_type && is_rc_managed_type(obj_type) &&
           !expr_is_local_ident(object, locals) &&
           expr_yields_owned_rc_value(g, object, locals);
}

/* 链式调用中的临时接收者对象在访问完成后及时释放 */
LLVMValueRef finish_member_of_temp(zan_irgen_t *g, zan_ast_node_t *expr,
                                          local_scope_t *locals,
                                          zan_type_t *obj_type,
                                          LLVMValueRef obj_val, LLVMValueRef fv) {
    if (!receiver_is_owned_temp(g, expr->member.object, locals, obj_type, obj_val))
        return fv;
    zan_type_t *ft = member_owned_field_type(g, expr, locals);
    if (ft && is_rc_managed_type(ft) &&
        LLVMGetTypeKind(LLVMTypeOf(fv)) == LLVMPointerTypeKind &&
        /* 弱引用字段读取时已从注册表获取 +1 强引用 */
        !member_field_is_weak(g, expr, locals))
        emit_rc_retain_for_type(g, ft, fv);
    emit_rc_release_for_type(g, obj_type, obj_val);
    return fv;
}

/* readonly 字段赋值校验与写入约束 */
static void check_readonly_store(zan_irgen_t *g, zan_ast_node_t *lhs,
                                 local_scope_t *locals) {
    if (!lhs || lhs->kind != AST_MEMBER_ACCESS) return;
    if (lhs->member.object && lhs->member.object->kind == AST_THIS_EXPR) return;
    zan_type_t *ot = infer_expr_type(g, lhs->member.object, locals);
    if (!ot || !ot->sym) return;
    zan_symbol_t *f = get_field_sym(ot->sym, lhs->member.name);
    if (!f || !f->decl || f->decl->kind != AST_FIELD_DECL) return;
    if ((f->decl->field_decl.modifiers & MOD_READONLY) == 0) return;
    if (g->current_fn_is_ctor && g->current_type_sym == ot->sym) return;
    zan_diag_emit_code(g->diag, DIAG_ERROR, lhs->loc, 3006,
                  "cannot assign to readonly field '%.*s' outside a "
                  "constructor of '%.*s'",
                  (int)lhs->member.name.len, lhs->member.name.str,
                  (int)ot->sym->name.len, ot->sym->name.str);
}

/* 用户自定义隐式/显式类型转换运算符 */
zan_symbol_t *find_user_conversion(zan_irgen_t *g, zan_type_t *from_type,
                                          zan_type_t *to_type,
                                          const char *op_name);
LLVMValueRef emit_user_conversion(zan_irgen_t *g, zan_type_t *from_type,
                                         zan_type_t *to_type, const char *op_name,
                                         LLVMValueRef val, zan_ast_node_t *val_expr,
                                         local_scope_t *locals);

/* 多维矩形数组元素寻址：结合各维度形状计算一维平铺偏移 */
LLVMValueRef emit_mdarray_elem_ptr(zan_irgen_t *g, LLVMValueRef arr_ptr,
        zan_ast_node_t *expr, LLVMTypeRef elem_llvm, local_scope_t *locals,
        LLVMValueRef *out_oob) {
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
    zan_type_t *at = infer_expr_type(g, expr->index.object, locals);
    int rank = (at && at->array_rank > 1) ? at->array_rank : 1;
    LLVMValueRef dim_ptr = LLVMBuildBitCast(g->builder, arr_ptr,
        LLVMPointerType(i64, 0), "md.dp");
    LLVMValueRef zero = LLVMConstInt(i64, 0, 0);
    LLVMValueRef idx = emit_expr(g, expr->index.index, locals);
    idx = emit_index_i64(g, idx, "md.i0");
    LLVMValueRef dim0 = LLVMBuildLoad2(g->builder, i64,
        LLVMBuildGEP2(g->builder, i64, dim_ptr, &zero, 1, "md.d0"), "md.dim0");
    LLVMValueRef total_oob = emit_index_oob_pred(g, idx, dim0);
    emit_index_bounds_check(g, idx, dim0, expr->loc, "array");
    idx = emit_index_safe_bounds(g, idx, dim0, expr->loc, "array");
    LLVMValueRef flat = idx;
    for (int d = 1; d < rank; d++) {
        LLVMValueRef off = LLVMConstInt(i64, (unsigned long long)d, 0);
        LLVMValueRef dimv = LLVMBuildLoad2(g->builder, i64,
            LLVMBuildGEP2(g->builder, i64, dim_ptr, &off, 1, "md.dd"), "md.dim");
        zan_ast_node_t *extra_node = (d - 1 < expr->index.extra.count)
            ? expr->index.extra.items[d - 1] : NULL;
        LLVMValueRef ix = extra_node ? emit_expr(g, extra_node, locals) : LLVMConstInt(i64, 0, 0);
        ix = emit_index_i64(g, ix, "md.ix");
        LLVMValueRef cur_oob = emit_index_oob_pred(g, ix, dimv);
        total_oob = LLVMBuildOr(g->builder, total_oob, cur_oob, "md.oob");
        emit_index_bounds_check(g, ix, dimv, expr->loc, "array");
        ix = emit_index_safe_bounds(g, ix, dimv, expr->loc, "array");
        flat = zan_mul(g->builder, flat, dimv, "md.fm");
        flat = zan_add(g->builder, flat, ix, "md.fa");
    }
    if (out_oob) {
        *out_oob = total_oob;
    }
    LLVMValueRef data_off = LLVMConstInt(i64,
        (unsigned long long)rank * 8, 0);
    LLVMValueRef data = LLVMBuildGEP2(g->builder, i8, arr_ptr, &data_off, 1, "md.data");
    LLVMValueRef typed = LLVMBuildBitCast(g->builder, data,
        LLVMPointerType(elem_llvm, 0), "md.typed");
    return LLVMBuildGEP2(g->builder, elem_llvm, typed, &flat, 1, "md.ep");
}

/* 复合赋值：目标操作数仅求值一次，防止副作用重复触发 */
static int ca_synth_counter = 0;

static void ca_hoist_spine(zan_irgen_t *g, zan_ast_node_t **slot,
                           local_scope_t *locals, int is_root);

/* 将目标表达式求值入隐藏临时变量，并在原地替换为读取引用 */
static void ca_replace_with_temp(zan_irgen_t *g, zan_ast_node_t **slot,
                                 local_scope_t *locals) {
    zan_ast_node_t *node = *slot;
    if (!node) return;
    zan_type_t *type = infer_expr_type(g, node, locals);
    /* 缺乏明确类型时避免分配错误尺寸的临时槽 */
    if (!type) return;
    char nbuf[40];
    int n = (int)__atomic_fetch_add(&ca_synth_counter, 1, __ATOMIC_SEQ_CST);
    snprintf(nbuf, sizeof nbuf, "__zca%d", n);
    uint32_t nlen = (uint32_t)strlen(nbuf);
    zan_istr_t name = { zan_arena_strdup(g->arena, nbuf, nlen), nlen };
    LLVMValueRef alloca = emit_entry_alloca(g, map_type(g, type), nbuf);
    LLVMValueRef val = emit_expr(g, node, locals);
    zan_store_fit(g, val, alloca);
    local_add(locals, name, alloca, type);
    locals->vars[locals->count - 1].arc_owned =
        (is_rc_managed_type(type) && expr_yields_owned_rc_value(g, node, locals))
            ? 1 : 0;
    zan_ast_node_t *id = zan_ast_new(g->arena, AST_IDENTIFIER, node->loc);
    id->ident.name = name;
    *slot = id;
}

static void ca_hoist_spine(zan_irgen_t *g, zan_ast_node_t **slot,
                           local_scope_t *locals, int is_root) {
    zan_ast_node_t *node = *slot;
    if (!node) return;
    switch (node->kind) {
    case AST_INDEX:
        if (is_root) {
            ca_hoist_spine(g, &node->index.object, locals, 0);
            ca_hoist_spine(g, &node->index.index, locals, 0);
        } else {
            ca_replace_with_temp(g, slot, locals);
        }
        return;
    case AST_MEMBER_ACCESS:
        if (is_root) {
            ca_hoist_spine(g, &node->member.object, locals, 0);
        } else {
            ca_replace_with_temp(g, slot, locals);
        }
        return;
    case AST_IDENTIFIER:
    case AST_THIS_EXPR:
    case AST_INT_LITERAL:
    case AST_FLOAT_LITERAL:
    case AST_STRING_LITERAL:
    case AST_CHAR_LITERAL:
    case AST_BOOL_LITERAL:
    case AST_NULL_LITERAL:
        return;
    default:
        ca_replace_with_temp(g, slot, locals);
        return;
    }
}

/* 计算索引表达式 (AST_INDEX) 选中的目标元素内存地址 */
LLVMValueRef emit_struct_elem_ptr(zan_irgen_t *g, zan_ast_node_t *expr,
                                         local_scope_t *locals) {
    zan_ast_node_t *base = expr->index.object;
    zan_type_t *bt = infer_expr_type(g, base, locals);
    if (!bt) return NULL;
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i8p = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMValueRef idx = emit_expr(g, expr->index.index, locals);
    if (LLVMGetTypeKind(LLVMTypeOf(idx)) == LLVMIntegerTypeKind &&
        LLVMGetIntTypeWidth(LLVMTypeOf(idx)) < 64)
        idx = LLVMBuildSExt(g->builder, idx, i64, "ix");
    if (type_named(bt, "List", 4)) {
        LLVMValueRef raw = emit_expr(g, base, locals);
        LLVMValueRef list_ptr = LLVMBuildBitCast(g->builder, raw,
            LLVMPointerType(g->list_struct_type, 0), "lptr");
        LLVMValueRef count = LLVMBuildLoad2(g->builder, i64,
            LLVMBuildStructGEP2(g->builder, g->list_struct_type, list_ptr, 0,
                "cf"), "cnt");
        emit_index_bounds_check(g, idx, count, expr->loc, "list");
        idx = emit_index_safe_bounds(g, idx, count, expr->loc, "list");
        idx = slot_word_index(g, idx, elem_slot_words(g, container_elem_type(bt)));
        LLVMValueRef data = LLVMBuildLoad2(g->builder, LLVMPointerType(i64, 0),
            LLVMBuildStructGEP2(g->builder, g->list_struct_type, list_ptr, 2,
                "df"), "data");
        LLVMValueRef ep = LLVMBuildGEP2(g->builder, i64, data, &idx, 1, "ep");
        return LLVMBuildBitCast(g->builder, ep, i8p, "epb");
    }
    if (bt->kind == TYPE_ARRAY && bt->array_rank == 1) {
        LLVMValueRef arr = emit_expr(g, base, locals);
        emit_index_bounds_check(g, idx, zan_array_len(g, arr), expr->loc, "array");
        idx = emit_index_safe_bounds(g, idx, zan_array_len(g, arr),
            expr->loc, "array");
        zan_type_t *et = container_elem_type(bt);
        LLVMTypeRef etl = et ? map_type(g, et) : i64;
        LLVMValueRef typed = LLVMBuildBitCast(g->builder, arr,
            LLVMPointerType(etl, 0), "arrp");
        LLVMValueRef ep = LLVMBuildGEP2(g->builder, etl, typed, &idx, 1, "ep");
        return LLVMBuildBitCast(g->builder, ep, i8p, "epb");
    }
    if (is_span_type(bt)) {
        LLVMValueRef sv = emit_expr(g, base, locals);
        LLVMValueRef sbase = LLVMBuildExtractValue(g->builder, sv, 0, "spb");
        LLVMValueRef slen = LLVMBuildExtractValue(g->builder, sv, 1, "spl");
        emit_index_bounds_check(g, idx, slen, expr->loc, "span");
        idx = emit_index_safe_bounds(g, idx, slen, expr->loc, "span");
        zan_type_t *et = container_elem_type(bt);
        LLVMTypeRef etl = span_elem_storage_type(g, et);
        LLVMValueRef typed = LLVMBuildBitCast(g->builder, sbase,
            LLVMPointerType(etl, 0), "spp");
        LLVMValueRef ep = LLVMBuildGEP2(g->builder, etl, typed, &idx, 1, "ep");
        return LLVMBuildBitCast(g->builder, ep, i8p, "epb");
    }
    return NULL;
}
LLVMValueRef emit_ref_lvalue_ptr(zan_irgen_t *g, zan_ast_node_t *tgt,
                                        local_scope_t *locals);
bool irgen_boxed_eligible_struct(zan_irgen_t *g, zan_type_t *t);
LLVMValueRef emit_box_struct(zan_irgen_t *g, zan_type_t *st,
                                    zan_ast_node_t *src_expr,
                                    LLVMValueRef val, local_scope_t *locals);

/* A556-1 隐式装箱：值形状（struct 聚合）流向 object/接口槽时自动装箱。
 * C# 同语义：隐式装箱等价一次隐藏的 (object) cast——箱是新堆对象
 * （fresh owned），消费方不再 retain；箱内字段对来源 retain/转移的判定
 * 由 emit_box_struct 按操作数 owned 性处理。目标不是引用槽或值已是
 * 指针时原样返回。装箱发生后把 RHS 节点登记进 boxed 集合
 * （irgen_note_boxed_expr），下游所有权判定按 owned 走。 */
LLVMValueRef emit_implicit_box(zan_irgen_t *g, zan_type_t *lt,
                                      LLVMValueRef val, zan_ast_node_t *rhs,
                                      local_scope_t *locals) {
    if (!val || !lt || !rhs) return val;
    if (lt->kind != TYPE_OBJECT && lt->kind != TYPE_INTERFACE) return val;
    /* 同一 RHS 节点在一条语句内会经多条存储路径到达这里（赋值总派发
     * 已装箱，字段/槽位捕获再走一次）：箱按节点登记去重，第二个箱不但
     * 泄漏第一个，还会把箱首字 vtable 当字段 0 装进载荷。 */
    if (irgen_expr_was_boxed(g, rhs)) return val;
    zan_type_t *st = infer_expr_type(g, rhs, locals);
    bool boxable = st && st->kind == TYPE_STRUCT &&
        irgen_boxed_eligible_struct(g, st);
    /* 指针形值一般是引用形状（类/串/数组/集合），无需装箱；唯一例外是
     * 构造临时：new T{...} 的 emit_expr 结果是栈临时地址而不是已装箱
     * 引用，放行会让栈指针存进 object 槽（A556-4），必须交给
     * emit_box_struct 按值取出后再装箱。 */
    if (LLVMGetTypeKind(LLVMTypeOf(val)) == LLVMPointerTypeKind && !boxable)
        return val;
    if (!boxable)
        return val;
    irgen_note_boxed_expr(g, rhs);
    return emit_box_struct(g, st, rhs, val, locals);
}
