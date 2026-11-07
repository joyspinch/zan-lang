LLVMValueRef get_str_join_fn(zan_irgen_t *g) {
    LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "__zan_str_join");
    if (fn) return fn;
    LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i64ptr = LLVMPointerType(i64, 0);
    LLVMTypeRef fnty = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0);
    fn = LLVMAddFunction(g->mod, "__zan_str_join", fnty);
    LLVMSetLinkage(fn, LLVMInternalLinkage);
    LLVMTypeRef strlen_ty = LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr }, 1, 0);
    LLVMTypeRef memcpy_ty = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr, i64 }, 3, 0);
    LLVMValueRef memcpy_fn = LLVMGetNamedFunction(g->mod, "memcpy");
    if (!memcpy_fn) memcpy_fn = LLVMAddFunction(g->mod, "memcpy", memcpy_ty);
    LLVMBasicBlockRef saved = LLVMGetInsertBlock(g->builder);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
    LLVMBasicBlockRef m_cond = LLVMAppendBasicBlockInContext(g->ctx, fn, "m.cond");
    LLVMBasicBlockRef m_body = LLVMAppendBasicBlockInContext(g->ctx, fn, "m.body");
    LLVMBasicBlockRef c_pre = LLVMAppendBasicBlockInContext(g->ctx, fn, "c.pre");
    LLVMBasicBlockRef c_cond = LLVMAppendBasicBlockInContext(g->ctx, fn, "c.cond");
    LLVMBasicBlockRef c_body = LLVMAppendBasicBlockInContext(g->ctx, fn, "c.body");
    LLVMBasicBlockRef c_sep = LLVMAppendBasicBlockInContext(g->ctx, fn, "c.sep");
    LLVMBasicBlockRef c_next = LLVMAppendBasicBlockInContext(g->ctx, fn, "c.next");
    LLVMBasicBlockRef done = LLVMAppendBasicBlockInContext(g->ctx, fn, "done");
    LLVMPositionBuilderAtEnd(g->builder, entry);
    LLVMValueRef sep = LLVMGetParam(fn, 0);
    LLVMValueRef lraw = LLVMGetParam(fn, 1);
    LLVMValueRef lp = LLVMBuildBitCast(g->builder, lraw,
        LLVMPointerType(g->list_struct_type, 0), "lp");
    LLVMValueRef cntp = LLVMBuildStructGEP2(g->builder, g->list_struct_type, lp, 0, "cntp");
    LLVMValueRef n = LLVMBuildLoad2(g->builder, i64, cntp, "n");
    LLVMValueRef dpp = LLVMBuildStructGEP2(g->builder, g->list_struct_type, lp, 2, "dpp");
    LLVMValueRef data = LLVMBuildLoad2(g->builder, i64ptr, dpp, "data");
    LLVMValueRef seplen = zan_call2(g->builder, strlen_ty, g->fn_strlen, &sep, 1, "seplen");
    LLVMValueRef idx_a = LLVMBuildAlloca(g->builder, i64, "i");
    LLVMValueRef tot_a = LLVMBuildAlloca(g->builder, i64, "tot");
    LLVMValueRef pos_a = LLVMBuildAlloca(g->builder, i64, "pos");
    LLVMBuildStore(g->builder, LLVMConstInt(i64, 0, 0), idx_a);
    LLVMValueRef nz = zan_icmp(g->builder, LLVMIntSGT, n, LLVMConstInt(i64, 0, 0), "nz");
    LLVMValueRef nm1 = zan_sub(g->builder, n, LLVMConstInt(i64, 1, 0), "nm1");
    LLVMValueRef sepsum = LLVMBuildSelect(g->builder, nz,
        zan_mul(g->builder, seplen, nm1, "ss"), LLVMConstInt(i64, 0, 0), "sepsum");
    LLVMBuildStore(g->builder, sepsum, tot_a);
    LLVMBuildBr(g->builder, m_cond);
    LLVMPositionBuilderAtEnd(g->builder, m_cond);
    LLVMValueRef mi = LLVMBuildLoad2(g->builder, i64, idx_a, "mi");
    LLVMBuildCondBr(g->builder,
        zan_icmp(g->builder, LLVMIntSLT, mi, n, "mlt"), m_body, c_pre);
    LLVMPositionBuilderAtEnd(g->builder, m_body);
    LLVMValueRef slot = LLVMBuildGEP2(g->builder, i64, data, &mi, 1, "slot");
    LLVMValueRef sv = emit_str_nonnull(g, LLVMBuildIntToPtr(g->builder,
        LLVMBuildLoad2(g->builder, i64, slot, "svi"), i8ptr, "sv"));
    LLVMValueRef sl = zan_call2(g->builder, strlen_ty, g->fn_strlen, &sv, 1, "sl");
    LLVMBuildStore(g->builder,
        zan_add(g->builder, LLVMBuildLoad2(g->builder, i64, tot_a, "t0"), sl, "t1"), tot_a);
    LLVMBuildStore(g->builder,
        zan_add(g->builder, mi, LLVMConstInt(i64, 1, 0), "mi1"), idx_a);
    LLVMBuildBr(g->builder, m_cond);
    LLVMPositionBuilderAtEnd(g->builder, c_pre);
    LLVMValueRef total = LLVMBuildLoad2(g->builder, i64, tot_a, "total");
    LLVMValueRef buf = emit_string_alloc_rc(g,
        zan_add(g->builder, total, LLVMConstInt(i64, 1, 0), "tot1"));
    LLVMBuildStore(g->builder, LLVMConstInt(i64, 0, 0), idx_a);
    LLVMBuildStore(g->builder, LLVMConstInt(i64, 0, 0), pos_a);
    LLVMBuildBr(g->builder, c_cond);
    LLVMPositionBuilderAtEnd(g->builder, c_cond);
    LLVMValueRef ci = LLVMBuildLoad2(g->builder, i64, idx_a, "ci");
    LLVMBuildCondBr(g->builder,
        zan_icmp(g->builder, LLVMIntSLT, ci, n, "clt"), c_body, done);
    LLVMPositionBuilderAtEnd(g->builder, c_body);
    LLVMValueRef slot2 = LLVMBuildGEP2(g->builder, i64, data, &ci, 1, "slot2");
    LLVMValueRef sv2 = emit_str_nonnull(g, LLVMBuildIntToPtr(g->builder,
        LLVMBuildLoad2(g->builder, i64, slot2, "svi2"), i8ptr, "sv2"));
    LLVMValueRef sl2 = zan_call2(g->builder, strlen_ty, g->fn_strlen, &sv2, 1, "sl2");
    LLVMValueRef pos = LLVMBuildLoad2(g->builder, i64, pos_a, "pos");
    LLVMValueRef dst = LLVMBuildGEP2(g->builder, i8, buf, &pos, 1, "dst");
    zan_call2(g->builder, memcpy_ty, memcpy_fn,
        (LLVMValueRef[]){ dst, sv2, sl2 }, 3, "");
    LLVMValueRef pos2 = zan_add(g->builder, pos, sl2, "pos2");
    LLVMBuildStore(g->builder, pos2, pos_a);
    LLVMValueRef last = zan_icmp(g->builder, LLVMIntSGE,
        zan_add(g->builder, ci, LLVMConstInt(i64, 1, 0), "ci1"), n, "last");
    LLVMBuildCondBr(g->builder, last, c_next, c_sep);
    LLVMPositionBuilderAtEnd(g->builder, c_sep);
    LLVMValueRef dst2 = LLVMBuildGEP2(g->builder, i8, buf, &pos2, 1, "dst2");
    zan_call2(g->builder, memcpy_ty, memcpy_fn,
        (LLVMValueRef[]){ dst2, sep, seplen }, 3, "");
    LLVMBuildStore(g->builder,
        zan_add(g->builder, pos2, seplen, "pos3"), pos_a);
    LLVMBuildBr(g->builder, c_next);
    LLVMPositionBuilderAtEnd(g->builder, c_next);
    LLVMBuildStore(g->builder,
        zan_add(g->builder, ci, LLVMConstInt(i64, 1, 0), "ni"), idx_a);
    LLVMBuildBr(g->builder, c_cond);
    LLVMPositionBuilderAtEnd(g->builder, done);
    LLVMValueRef endpos = LLVMBuildLoad2(g->builder, i64, pos_a, "endpos");
    LLVMValueRef endptr = LLVMBuildGEP2(g->builder, i8, buf, &endpos, 1, "endptr");
    LLVMBuildStore(g->builder, LLVMConstInt(i8, 0, 0), endptr);
    LLVMBuildRet(g->builder, buf);
    if (saved) LLVMPositionBuilderAtEnd(g->builder, saved);
    return fn;
}
LLVMValueRef coerce_dict_key(zan_irgen_t *g, LLVMValueRef key,
                                    zan_type_t *kt) {
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    if (LLVMGetTypeKind(LLVMTypeOf(key)) == LLVMIntegerTypeKind) {
        LLVMTypeRef kem = kt ? map_type(g, kt) : NULL;
        if (kem && LLVMGetTypeKind(kem) == LLVMIntegerTypeKind &&
            LLVMGetIntTypeWidth(kem) < LLVMGetIntTypeWidth(LLVMTypeOf(key)))
            key = LLVMBuildTrunc(g->builder, key, kem, "k.nw");
        if (LLVMGetIntTypeWidth(LLVMTypeOf(key)) < 64)
            key = LLVMBuildSExt(g->builder, key, i64, "k.sx");
        return LLVMBuildIntToPtr(g->builder, key, i8ptr, "k.ip");
    }
    if (LLVMTypeOf(key) != i8ptr)
        return LLVMBuildBitCast(g->builder, key, i8ptr, "k.bc");
    return key;
}

/* 发射`keys[i] == search` for a Dict scan via __zan_dict_key_eq */
static LLVMValueRef emit_dict_key_eq(zan_irgen_t *g, zan_type_t *dict_type,
                                     LLVMValueRef kv, LLVMValueRef search) {
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    zan_type_t *kt = dict_key_type(g, dict_type);
    LLVMValueRef is_str = LLVMConstInt(i64,
        (!kt || kt->kind == TYPE_STRING) ? 1 : 0, 0);
    LLVMValueRef keq = get_dict_key_eq_fn(g);
    return zan_call2(g->builder, LLVMGlobalGetValueType(keq), keq,
        (LLVMValueRef[]){ kv, search, is_str }, 3, "keq");
}

/* 发射`__zan_dict_find(dict, key, is_str)` for a dict of type `dict_type` */
LLVMValueRef emit_dict_find(zan_irgen_t *g, zan_type_t *dict_type,
                                   LLVMValueRef draw, LLVMValueRef key) {
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    zan_type_t *kt = dict_key_type(g, dict_type);
    LLVMValueRef is_str = LLVMConstInt(i64, (!kt || kt->kind == TYPE_STRING) ? 1 : 0, 0);
    LLVMValueRef fn = get_dict_find_fn(g);
    if (LLVMTypeOf(draw) != i8ptr)
        draw = LLVMBuildBitCast(g->builder, draw, i8ptr, "d8");
    return zan_call2(g->builder, LLVMGlobalGetValueType(fn), fn,
        (LLVMValueRef[]){ draw, key, is_str }, 3, "dfind");
}
zan_type_t *static_access_inst(zan_irgen_t *g, zan_ast_node_t *obj_expr) {
    if (!obj_expr || !obj_expr->ident.inst_type_ref) return NULL;
    zan_type_t *t = resolve_type_ctx(g, obj_expr->ident.inst_type_ref);
    return (t && t->type_arg_count > 0) ? t : NULL;
}
LLVMValueRef get_static_field_global(zan_irgen_t *g, zan_symbol_t *class_sym,
                                            zan_symbol_t *fsym,
                                            zan_type_t *inst) {
    if (!class_sym || !fsym || !fsym->decl ||
        (fsym->decl->kind != AST_FIELD_DECL &&
         fsym->decl->kind != AST_PROPERTY_DECL))
        return NULL;
    if (!((fsym->modifiers & MOD_STATIC) ||
          (fsym->decl->field_decl.modifiers & MOD_STATIC)))
        return NULL;
    if (fsym->parent && (fsym->parent->kind == SYM_CLASS ||
                         fsym->parent->kind == SYM_STRUCT)) {
        class_sym = fsym->parent;
    }
    /* 泛型类的静态字段按闭合特化类型独立隔离 (如 Stat<int> 与 Stat<string> 拥有独立静态槽) */
    if (!inst && g->cur_inst && g->cur_inst->sym == class_sym) inst = g->cur_inst;
    char suffix[256];
    suffix[0] = '\0';
    if (inst && inst->sym == class_sym && inst->type_arg_count > 0)
        mangle_inst_suffix(suffix, sizeof(suffix), inst);
    char name[512];
    snprintf(name, sizeof(name), "__zan_sf_%.*s_%.*s%s",
             (int)class_sym->name.len, class_sym->name.str,
             (int)fsym->name.len, fsym->name.str, suffix);
    LLVMValueRef gv = LLVMGetNamedGlobal(g->mod, name);
    if (gv) return gv;
    LLVMTypeRef ft = fsym->type ? map_type(g, fsym->type)
                                : LLVMInt64TypeInContext(g->ctx);
    gv = LLVMAddGlobal(g->mod, ft, name);
    LLVMSetLinkage(gv, LLVMInternalLinkage);
    LLVMSetInitializer(gv, LLVMConstNull(ft));
    if (fsym->type && (is_rc_managed_type(fsym->type) || fsym->type->kind == TYPE_OBJECT)) {
        if (g->static_field_count >= g->static_field_cap) {
            g->static_field_cap = g->static_field_cap ? g->static_field_cap * 2 : 16;
            g->static_fields = (struct zan_static_field_ref *)realloc(
                g->static_fields,
                (size_t)g->static_field_cap * sizeof(*g->static_fields));
        }
        g->static_fields[g->static_field_count].type = fsym->type;
        g->static_fields[g->static_field_count].gv = gv;
        g->static_field_count++;
    }
    return gv;
}

static int is_stable_field_base(zan_ast_node_t *expr) {
    if (!expr) return 0;
    if (expr->kind == AST_IDENTIFIER || expr->kind == AST_THIS_EXPR ||
        expr->kind == AST_BASE_EXPR) return 1;
    return expr->kind == AST_MEMBER_ACCESS &&
           is_stable_field_base(expr->member.object);
}
void emit_invalidate_freed_string(zan_irgen_t *g, zan_ast_node_t *arg,
                                         local_scope_t *locals) {
    if (!arg || !locals) return;
    if (arg->kind == AST_IDENTIFIER) {
        local_var_t *local = local_find(locals, arg->ident.name);
        if (local && local->type && local->type->kind == TYPE_STRING) {
            LLVMTypeRef slot_type = local_slot_type(g, local);
            if (LLVMGetTypeKind(slot_type) == LLVMPointerTypeKind) {
                LLVMBuildStore(g->builder, LLVMConstNull(slot_type), local->alloca);
            }
            return;
        }
        if (g->current_type_sym) {
            zan_symbol_t *field = get_field_sym(g->current_type_sym, arg->ident.name);
            if (!field || !field->type || field->type->kind != TYPE_STRING) return;
            LLVMValueRef global = get_static_field_global(g, g->current_type_sym, field, NULL);
            LLVMTypeRef field_type = map_type(g, field->type);
            if (global) {
                LLVMBuildStore(g->builder, LLVMConstNull(field_type), global);
            } else if (g->current_this) {
                int fi = get_field_index(g->current_type_sym, arg->ident.name);
                LLVMTypeRef st = get_struct_llvm_type(g, g->current_type_sym);
                if (fi >= 0 && st) {
                    LLVMValueRef this_ptr = LLVMBuildLoad2(g->builder,
                        LLVMPointerType(st, 0), g->current_this, "this");
                    LLVMValueRef field_ptr = emit_field_ptr(g, g->current_type_sym, st,
                        this_ptr, fi, "freed.fld");
                    LLVMBuildStore(g->builder, LLVMConstNull(field_type), field_ptr);
                }
            }
        }
        return;
    }
    if (arg->kind != AST_MEMBER_ACCESS) return;
    zan_type_t *field_type = infer_expr_type(g, arg, locals);
    if (!field_type || field_type->kind != TYPE_STRING) return;
    zan_ast_node_t *obj = arg->member.object;
    zan_symbol_t *class_sym = NULL;
    LLVMValueRef object_ptr = NULL;
    if (obj->kind == AST_IDENTIFIER) {
        local_var_t *local = local_find(locals, obj->ident.name);
        if (local && local->type && local->type->sym) {
            class_sym = local->type->sym;
            LLVMTypeRef st = get_struct_llvm_type(g, class_sym);
            if (st) object_ptr = struct_base_ptr(g, local, st);
        } else {
            class_sym = zan_binder_lookup(g->binder, obj->ident.name);
            if (class_sym && (class_sym->kind == SYM_CLASS ||
                              class_sym->kind == SYM_STRUCT)) {
                zan_symbol_t *field = get_field_sym(class_sym, arg->member.name);
                LLVMValueRef global = get_static_field_global(g, class_sym, field,
                    static_access_inst(g, obj));
                if (global) {
                    LLVMBuildStore(g->builder,
                        LLVMConstNull(map_type(g, field_type)), global);
                    return;
                }
            }
            class_sym = NULL;
        }
    } else if ((obj->kind == AST_THIS_EXPR || obj->kind == AST_BASE_EXPR) &&
               g->current_type_sym && g->current_this) {
        class_sym = g->current_type_sym;
        LLVMTypeRef st = get_struct_llvm_type(g, class_sym);
        if (st) {
            object_ptr = LLVMBuildLoad2(g->builder, LLVMPointerType(st, 0),
                                       g->current_this, "this");
        }
    } else if (is_stable_field_base(obj)) {
        class_sym = expr_class_sym(g, obj, locals);
        if (class_sym) object_ptr = emit_expr(g, obj, locals);
    }
    if (!class_sym || !object_ptr ||
        LLVMGetTypeKind(LLVMTypeOf(object_ptr)) != LLVMPointerTypeKind) return;
    int fi = get_field_index(class_sym, arg->member.name);
    LLVMTypeRef st = get_struct_llvm_type(g, class_sym);
    if (fi < 0 || !st) return;
    LLVMValueRef field_ptr = emit_field_ptr(g, class_sym, st, object_ptr, fi, "freed.fld");
    LLVMBuildStore(g->builder, LLVMConstNull(map_type(g, field_type)), field_ptr);
}
LLVMValueRef emit_null_cond(zan_irgen_t *g, zan_ast_node_t *expr,
                                   zan_ast_node_t *qmem, local_scope_t *locals) {
    LLVMValueRef obj = emit_expr(g, qmem->member.object, locals);
    if (LLVMGetTypeKind(LLVMTypeOf(obj)) != LLVMPointerTypeKind) {
        qmem->member.null_cond = 0;
        LLVMValueRef v = emit_expr(g, expr, locals);
        qmem->member.null_cond = 1;
        return v;
    }
    zan_type_t *oty = infer_expr_type(g, qmem->member.object, locals);
    char nm[32];
    snprintf(nm, sizeof(nm), "__qdot%d", g->qdot_counter++);
    zan_istr_t iname = { zan_arena_strdup(g->arena, nm, strlen(nm)),
                           (int)strlen(nm) };
    LLVMValueRef slot = emit_entry_alloca(g, LLVMTypeOf(obj), nm);
    LLVMBuildStore(g->builder, obj, slot);
    local_add(locals, iname, slot, oty);

    zan_ast_node_t *ident = zan_ast_new(g->arena, AST_IDENTIFIER, qmem->loc);
    ident->ident.name = iname;
    zan_ast_node_t *saved_obj = qmem->member.object;
    qmem->member.object = ident;
    qmem->member.null_cond = 0;

    LLVMValueRef fn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder));
    LLVMBasicBlockRef then_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "qdot.then");
    LLVMBasicBlockRef merge_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "qdot.end");
    LLVMValueRef isnull = zan_icmp(g->builder, LLVMIntEQ, obj,
        LLVMConstNull(LLVMTypeOf(obj)), "qdot.isnull");
    LLVMBasicBlockRef entry_bb = LLVMGetInsertBlock(g->builder);
    LLVMBuildCondBr(g->builder, isnull, merge_bb, then_bb);

    LLVMPositionBuilderAtEnd(g->builder, then_bb);
    LLVMValueRef v = emit_expr(g, expr, locals);
    LLVMTypeRef rt = LLVMTypeOf(v);
    if (LLVMGetTypeKind(rt) != LLVMVoidTypeKind &&
        LLVMGetTypeKind(rt) != LLVMPointerTypeKind && !llvm_is_nullable(rt)) {
        LLVMValueRef some = nullable_some(g, nullable_type_of(g, rt), v);
        if (some) v = some;
    }
    LLVMBasicBlockRef then_end = LLVMGetInsertBlock(g->builder);
    LLVMBuildBr(g->builder, merge_bb);

    qmem->member.object = saved_obj;
    qmem->member.null_cond = 1;

    LLVMPositionBuilderAtEnd(g->builder, merge_bb);
    LLVMTypeRef vt = LLVMTypeOf(v);
    if (LLVMGetTypeKind(vt) == LLVMVoidTypeKind) {
        emit_release_owned_call_temp(g, saved_obj, obj, locals);
        return LLVMConstInt(LLVMInt64TypeInContext(g->ctx), 0, 0);
    }
    LLVMValueRef phi = LLVMBuildPhi(g->builder, vt, "qdot");
    LLVMValueRef dflt = LLVMConstNull(vt);
    LLVMValueRef vals[] = { dflt, v };
    LLVMBasicBlockRef bbs[] = { entry_bb, then_end };
    LLVMAddIncoming(phi, vals, bbs, 2);
    emit_release_owned_call_temp(g, saved_obj, obj, locals);
    return phi;
}
void reorder_named_args_impl(zan_irgen_t *g, zan_ast_list_t *args,
                                    zan_loc_t loc, zan_ast_node_t *decl) {
    if (!args || !decl) return;
    if (decl->kind != AST_METHOD_DECL &&
        decl->kind != AST_CONSTRUCTOR_DECL) return;
    int n = args->count;
    int has_named = 0;
    for (int i = 0; i < n; i++) {
        if (args->items[i] &&
            args->items[i]->kind == AST_NAMED_ARG) { has_named = 1; break; }
    }
    if (!has_named) return;

    zan_ast_list_t *ps = &decl->method_decl.params;
    int m = ps->count;
    int *slot = (int *)malloc(sizeof(int) * (size_t)(m > n ? m : n));
    for (int i = 0; i < (m > n ? m : n); i++) slot[i] = -1;
    int *used_name = (int *)malloc(sizeof(int) * (size_t)n);
    for (int i = 0; i < n; i++) used_name[i] = 0;

    for (int i = 0; i < n; i++) {
        zan_ast_node_t *arg = args->items[i];
        if (!arg || arg->kind != AST_NAMED_ARG) continue;
        int found = -1;
        for (int j = 0; j < m; j++) {
            zan_ast_node_t *p = ps->items[j];
            if (!p || p->kind != AST_PARAM) continue;
            if (p->param.is_this) continue;
            if (p->param.name.len == arg->named_arg.name.len &&
                memcmp(p->param.name.str, arg->named_arg.name.str,
                       (size_t)p->param.name.len) == 0) {
                found = j;
                break;
            }
        }
        if (found < 0) {
            zan_diag_emit_code(g->diag, DIAG_ERROR, arg->loc, 2100,
                          "no parameter named '%.*s'",
                          (int)arg->named_arg.name.len,
                          arg->named_arg.name.str);
        } else if (slot[found] >= 0) {
            zan_diag_emit_code(g->diag, DIAG_ERROR, arg->loc, 2101,
                          "parameter '%.*s' is already assigned",
                          (int)arg->named_arg.name.len,
                          arg->named_arg.name.str);
        } else {
            slot[found] = i;
            used_name[i] = 1;
        }
    }

    int j = 0;
    for (int i = 0; i < n; i++) {
        if (used_name[i]) continue;
        while (j < m && (slot[j] >= 0 || ps->items[j]->param.is_this)) j++;
        if (j >= m) break;
        slot[j] = i;
        j++;
    }

    zan_ast_node_t **reb = (zan_ast_node_t **)malloc(sizeof(zan_ast_node_t *) *
                                                     (size_t)n);
    if (!reb) { free(slot); free(used_name); return; }
    int ok = 1;
    for (int p = 0; p < n; p++) {
        if (slot[p] < 0 || slot[p] >= n) {
            ok = 0;
            zan_diag_emit_code(g->diag, DIAG_ERROR, loc, 2102,
                          "named arguments must bind to the leading "
                          "parameters in order");
            break;
        }
        zan_ast_node_t *arg = args->items[slot[p]];
        if (arg && arg->kind == AST_NAMED_ARG) arg = arg->named_arg.expr;
        reb[p] = arg;
    }
    if (ok)
        for (int i = 0; i < n; i++)
            args->items[i] = reb[i];
    free(reb);
    free(slot);
    free(used_name);
}

static void reorder_named_args(zan_irgen_t *g, zan_ast_node_t *call,
                               zan_symbol_t *method_sym) {
    if (!call || call->kind != AST_CALL) return;
    if (!method_sym || !method_sym->decl) return;
    reorder_named_args_impl(g, &call->call.args, call->loc,
                            method_sym->decl);
}
void fill_default_args(zan_irgen_t *g, zan_ast_node_t *call,
                              zan_symbol_t *method_sym) {
    if (!call || call->kind != AST_CALL) return;
    if (!method_sym || !method_sym->decl) return;
    if (method_sym->decl->kind != AST_METHOD_DECL &&
        method_sym->decl->kind != AST_CONSTRUCTOR_DECL) return;
    reorder_named_args(g, call, method_sym);
    if (method_is_params_variadic(method_sym)) {
        /* params 方法也填充尾参之前的可选形参（A381 params 组合）：
         * "(int a, int b = 10, params int[] rest)" 的 b 在调用缺省时要填，
         * params 数组本身留给 pack_params_args 收（0 个也收空数组）。 */
        zan_ast_list_t *pps = &method_sym->decl->method_decl.params;
        int fill_to = pps->count - 1;
        for (int i = call->call.args.count; i < fill_to; i++) {
            zan_ast_node_t *pp = pps->items[i];
            if (!pp || pp->kind != AST_PARAM || !pp->param.default_val) return;
            zan_ast_list_push(&call->call.args, pp->param.default_val, g->arena);
        }
        return;
    }
    if (method_sym->name.len > 3 &&
        memcmp(method_sym->name.str, "op_", 3) == 0) return;
    zan_ast_list_t *ps = &method_sym->decl->method_decl.params;
    for (int i = call->call.args.count; i < ps->count; i++) {
        zan_ast_node_t *p = ps->items[i];
        if (!p || p->kind != AST_PARAM || !p->param.default_val) return;
        zan_ast_list_push(&call->call.args, p->param.default_val, g->arena);
    }
}
void pack_params_args(zan_irgen_t *g, zan_ast_node_t *call,
                             zan_symbol_t *method_sym, local_scope_t *locals) {
    if (!method_sym || !method_sym->decl ||
        method_sym->decl->kind != AST_METHOD_DECL) return;
    if (!method_is_params_variadic(method_sym)) return;
    zan_ast_list_t *ps = &method_sym->decl->method_decl.params;
    int injected = method_sym->name.len == 7 &&
        memcmp(method_sym->name.str, "op_call", 7) == 0 &&
        (method_sym->modifiers & MOD_STATIC) != 0;
    int visible = ps->count - injected;
    int fixed = visible - 1;
    int argc = call->call.args.count;
    if (argc < fixed) return;
    if (argc == visible) {
        zan_ast_node_t *la = call->call.args.items[argc - 1];
        if (la->kind == AST_NEW_EXPR) return;
        zan_type_t *lt = infer_expr_type(g, la, locals);
        if (lt && lt->kind == TYPE_ARRAY) return;
    }
    zan_ast_node_t *last_p = ps->items[ps->count - 1];
    zan_ast_node_t *arr = zan_ast_new(g->arena, AST_NEW_EXPR, call->loc);
    arr->new_expr.type = last_p->param.type; /* T[] */
    arr->new_expr.is_array = true;
    arr->new_expr.array_init = true;
    zan_ast_list_init(&arr->new_expr.args);
    for (int i = fixed; i < argc; i++)
        zan_ast_list_push(&arr->new_expr.args, call->call.args.items[i], g->arena);
    call->call.args.count = fixed;
    zan_ast_list_push(&call->call.args, arr, g->arena);
}
LLVMValueRef emit_ref_arg(zan_irgen_t *g, zan_ast_node_t *arg,
                                 local_scope_t *locals);
