}
void build_class_release_body(zan_irgen_t *g, zan_symbol_t *sym,
                                    zan_type_t *inst, LLVMValueRef fn) {
    di_clear(g);
    LLVMContextRef c = g->ctx;
    LLVMBuilderRef b = g->builder;
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(c), 0);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(c);
    LLVMTypeRef structT = get_struct_llvm_type(g, sym);
    LLVMValueRef obj = LLVMGetParam(fn, 0);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(c, fn, "entry");
    LLVMBasicBlockRef cont  = LLVMAppendBasicBlockInContext(c, fn, "cont");
    LLVMBasicBlockRef relf  = LLVMAppendBasicBlockInContext(c, fn, "relf");
    LLVMBasicBlockRef dorel = LLVMAppendBasicBlockInContext(c, fn, "dorel");
    LLVMBasicBlockRef ret   = LLVMAppendBasicBlockInContext(c, fn, "ret");
    LLVMPositionBuilderAtEnd(b, entry);
    LLVMValueRef isnull = zan_icmp(b, LLVMIntEQ, obj, LLVMConstNull(i8ptr), "isnull");
    LLVMBuildCondBr(b, isnull, ret, cont);
    LLVMPositionBuilderAtEnd(b, cont);
    LLVMValueRef neg16 = LLVMConstInt(i64, (unsigned long long)ZAN_OBJ_RC_OFF, 1);
    LLVMValueRef rcp = LLVMBuildGEP2(b, LLVMInt8TypeInContext(c), obj, &neg16, 1, "rcp");
    LLVMValueRef rcip = LLVMBuildBitCast(b, rcp, LLVMPointerType(i64, 0), "rcip");
    LLVMValueRef rc_old = LLVMBuildAtomicRMW(b, LLVMAtomicRMWBinOpSub, rcip,
        LLVMConstInt(i64, 1, 0), LLVMAtomicOrderingAcquireRelease, 0);
    LLVMValueRef is1 = zan_icmp(b, LLVMIntEQ, rc_old, LLVMConstInt(i64, 1, 0), "is1");
    LLVMValueRef over = zan_icmp(b, LLVMIntSLE, rc_old, LLVMConstInt(i64, 0, 0), "over");
    LLVMBasicBlockRef last_bb = LLVMAppendBasicBlockInContext(c, fn, "last");
    LLVMBuildCondBr(b, over, dorel, last_bb);
    LLVMPositionBuilderAtEnd(b, last_bb);
    LLVMBasicBlockRef begin_bb = LLVMAppendBasicBlockInContext(c, fn, "begin");
    LLVMBuildCondBr(b, is1, begin_bb, ret);
    LLVMPositionBuilderAtEnd(b, begin_bb);
    LLVMValueRef weak_ok = zan_call2(b,
        LLVMGlobalGetValueType(g->rt_weak_destroy_begin),
        g->rt_weak_destroy_begin, &obj, 1, "weak.begin");
    LLVMBuildCondBr(b, weak_ok, relf, ret);
    LLVMPositionBuilderAtEnd(b, relf);
    LLVMValueRef self = LLVMBuildBitCast(b, obj, LLVMPointerType(structT, 0), "self");
    if (sym->kind == SYM_STRUCT) {
        /* A556-2：箱首字是 vtable tag（emit_box_struct 布局契约），载荷
         * 在 +8——字段 walk 先跳过 tag 字。类不走这里（class_vptr_offset
         * 已把 vptr 计为 0 号字段）。 */
        LLVMValueRef p8 = LLVMBuildGEP2(b, LLVMInt8TypeInContext(c), obj,
            (LLVMValueRef[]){ LLVMConstInt(LLVMInt64TypeInContext(c), 8, 0) },
            1, "box.p8");
        self = LLVMBuildBitCast(b, p8, LLVMPointerType(structT, 0), "self.p8");
    }
    int fi = class_vptr_offset(sym);
    for (int i = 0; i < sym->member_count; i++) {
        zan_symbol_t *m = sym->members[i];
        if (m->kind != SYM_FIELD && m->kind != SYM_PROPERTY) continue;
        if (field_member_is_static(m)) continue;
        int idx = fi++;
        zan_type_t *ft = m->type;
        if (!ft) continue;
        if (inst) ft = subst_type_param_deep(g, ft, inst);
        if (!ft || ft->kind == TYPE_TYPE_PARAM) continue;
        if (type_is_binding(inst ? inst : sym->type) &&
            m->name.len == 6 && memcmp(m->name.str, "target", 6) == 0)
            continue;
        if ((m->modifiers & MOD_WEAK) && is_arc_managed_type(ft) &&
            (ft->kind == TYPE_INTERFACE ||
             (ft->kind == TYPE_CLASS && ft->sym != NULL))) {
            if (LLVMGetTypeKind(map_type(g, ft)) == LLVMPointerTypeKind) {
                LLVMValueRef fp = LLVMBuildStructGEP2(b, structT, self,
                    (unsigned)idx, "weak.fp");
                emit_weak_store(g, fp,
                    LLVMConstPointerNull(LLVMPointerType(
                        LLVMInt8TypeInContext(c), 0)));
            }
            continue;
        }
        if (ft->kind == TYPE_STRING) {
            LLVMValueRef fp = LLVMBuildStructGEP2(b, structT, self, (unsigned)idx, "fp");
            LLVMValueRef s = LLVMBuildLoad2(b, i8ptr, fp, "fs");
            emit_string_release(g, s);
        } else if (ft->kind == TYPE_DELEGATE) {
            LLVMValueRef fp = LLVMBuildStructGEP2(g->builder, structT, self,
                                                  (unsigned)idx, "fp");
            LLVMValueRef dv = LLVMBuildLoad2(g->builder, i8ptr, fp, "fd");
            emit_closure_release(g, dv);
        } else if (ft->kind == TYPE_ARRAY) {
            LLVMValueRef fp = LLVMBuildStructGEP2(b, structT, self, (unsigned)idx, "fp");
            LLVMValueRef av = LLVMBuildLoad2(b, i8ptr, fp, "fa");
            emit_array_release(g, ft, av);
        } else if (is_arc_managed_type(ft)) {
            LLVMValueRef fp = LLVMBuildStructGEP2(b, structT, self, (unsigned)idx, "fp");
            LLVMValueRef cv = LLVMBuildLoad2(b, map_type(g, ft), fp, "fc");
            emit_arc_release_typed(g, ft, cv);
        } else if (ft->kind == TYPE_STRUCT) {
            /* 装箱结构体的嵌套结构体字段（A556-3）：内层托管叶子按值
             * 递归释放（emit_collection_value_release 对聚合值逐字段
             * 下钻），GEP 位移与载荷布局一致。 */
            LLVMValueRef fp = LLVMBuildStructGEP2(b, structT, self, (unsigned)idx, "fp");
            LLVMValueRef nv = LLVMBuildLoad2(b, map_type(g, ft), fp, "fn");
            emit_collection_value_release(g, ft, nv, 0);
        } else if (ft->kind == TYPE_OBJECT) {
            LLVMValueRef fp = LLVMBuildStructGEP2(b, structT, self, (unsigned)idx, "fp");
            LLVMValueRef cv = LLVMBuildLoad2(b, i8ptr, fp, "fc");
            emit_release_obj_value(g, cv);
        }
    }
    LLVMBasicBlockRef freebb = LLVMAppendBasicBlockInContext(c, fn, "freebb");
    LLVMBuildBr(b, freebb);
    LLVMPositionBuilderAtEnd(b, freebb);
    LLVMValueRef freefn = get_arc_free_decl(g);
    zan_call2(b, LLVMGlobalGetValueType(freefn), freefn, &obj, 1, "");
    LLVMBuildBr(b, ret);
    LLVMPositionBuilderAtEnd(b, dorel);
    zan_call2(b, LLVMFunctionType(LLVMVoidTypeInContext(c), &i8ptr, 1, 0),
                   g->rt_release, &obj, 1, "");
    LLVMBuildBr(b, ret);
    LLVMPositionBuilderAtEnd(b, ret);
    LLVMBuildRetVoid(b);
}

static void build_collection_release_body(zan_irgen_t *g, int coll_kind,
                                          zan_type_t *elem_type, LLVMValueRef fn) {
    di_clear(g);
    LLVMContextRef c = g->ctx;
    LLVMBuilderRef b = g->builder;
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(c), 0);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(c);
    LLVMValueRef obj = LLVMGetParam(fn, 0);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(c, fn, "entry");
    LLVMBasicBlockRef cont  = LLVMAppendBasicBlockInContext(c, fn, "cont");
    LLVMBasicBlockRef relf  = LLVMAppendBasicBlockInContext(c, fn, "relf");
    LLVMBasicBlockRef dorel = LLVMAppendBasicBlockInContext(c, fn, "dorel");
    LLVMBasicBlockRef ret   = LLVMAppendBasicBlockInContext(c, fn, "ret");
    LLVMPositionBuilderAtEnd(b, entry);
    LLVMValueRef isnull = zan_icmp(b, LLVMIntEQ, obj, LLVMConstNull(i8ptr), "isnull");
    LLVMBuildCondBr(b, isnull, ret, cont);
    LLVMPositionBuilderAtEnd(b, cont);
    LLVMValueRef neg16 = LLVMConstInt(i64, (unsigned long long)ZAN_OBJ_RC_OFF, 1);
    LLVMValueRef rcp = LLVMBuildGEP2(b, LLVMInt8TypeInContext(c), obj, &neg16, 1, "rcp");
    LLVMValueRef rcip = LLVMBuildBitCast(b, rcp, LLVMPointerType(i64, 0), "rcip");
    LLVMValueRef rc_old = LLVMBuildAtomicRMW(b, LLVMAtomicRMWBinOpSub, rcip,
        LLVMConstInt(i64, 1, 0), LLVMAtomicOrderingAcquireRelease, 0);
    LLVMValueRef is1 = zan_icmp(b, LLVMIntEQ, rc_old, LLVMConstInt(i64, 1, 0), "is1");
    LLVMValueRef over = zan_icmp(b, LLVMIntSLE, rc_old, LLVMConstInt(i64, 0, 0), "over");
    LLVMBasicBlockRef last_bb = LLVMAppendBasicBlockInContext(c, fn, "last");
    LLVMBuildCondBr(b, over, dorel, last_bb);
    LLVMPositionBuilderAtEnd(b, last_bb);
    LLVMBuildCondBr(b, is1, relf, ret);
    LLVMPositionBuilderAtEnd(b, relf);
    LLVMTypeRef free_ty = LLVMFunctionType(LLVMVoidTypeInContext(c), &i8ptr, 1, 0);
    if (coll_kind == 1) {
        LLVMValueRef lp = LLVMBuildBitCast(b, obj, LLVMPointerType(g->list_struct_type, 0), "lp");
        LLVMValueRef dp = LLVMBuildStructGEP2(b, g->list_struct_type, lp, 2, "dp");
        LLVMValueRef data = LLVMBuildLoad2(b, LLVMPointerType(i64, 0), dp, "data");
        emit_list_release_elems(g, elem_type, obj);
        LLVMValueRef d8 = LLVMBuildBitCast(b, data, i8ptr, "d8");
        zan_call2(b, free_ty, g->fn_free, &d8, 1, "");
    } else if (coll_kind == 3) {
        LLVMValueRef dp = LLVMBuildBitCast(b, obj, LLVMPointerType(g->dict_struct_type, 0), "dp");
        LLVMValueRef ks = LLVMBuildLoad2(b, i8ptr,
            LLVMBuildStructGEP2(b, g->dict_struct_type, dp, 2, "kp"), "ks8");
        LLVMValueRef vs = LLVMBuildLoad2(b, i8ptr,
            LLVMBuildStructGEP2(b, g->dict_struct_type, dp, 3, "vp"), "vs8");
        LLVMValueRef ix = LLVMBuildLoad2(b, i8ptr,
            LLVMBuildStructGEP2(b, g->dict_struct_type, dp, 4, "ip"), "ix8");
        emit_dict_release_elems(g, elem_type, obj);
        zan_call2(b, free_ty, g->fn_free, &ks, 1, "");
        zan_call2(b, free_ty, g->fn_free, &vs, 1, "");
        zan_call2(b, free_ty, g->fn_free, &ix, 1, "");
    } else if (coll_kind == 2) {
        LLVMValueRef sp = LLVMBuildBitCast(b, obj, LLVMPointerType(g->sb_struct_type, 0), "sp");
        LLVMValueRef dp = LLVMBuildStructGEP2(b, g->sb_struct_type, sp, 2, "dp");
        LLVMValueRef data = LLVMBuildLoad2(b, i8ptr, dp, "data");
        zan_call2(b, free_ty, g->fn_free, &data, 1, "");
    }
    LLVMBasicBlockRef freebb = LLVMAppendBasicBlockInContext(c, fn, "freebb");
    LLVMBuildBr(b, freebb);
    LLVMPositionBuilderAtEnd(b, freebb);
    LLVMValueRef freefn = get_arc_free_decl(g);
    zan_call2(b, LLVMGlobalGetValueType(freefn), freefn, &obj, 1, "");
    LLVMBuildBr(b, ret);
    LLVMPositionBuilderAtEnd(b, dorel);
    zan_call2(b, LLVMFunctionType(LLVMVoidTypeInContext(c), &i8ptr, 1, 0),
                   g->rt_release, &obj, 1, "");
    LLVMBuildBr(b, ret);
    LLVMPositionBuilderAtEnd(b, ret);
    LLVMBuildRetVoid(b);
}
LLVMValueRef get_collection_release_decl(zan_irgen_t *g, int site) {
    char name[64];
    snprintf(name, sizeof(name), "__zan_release_coll_%d", site);
    LLVMValueRef existing = LLVMGetNamedFunction(g->mod, name);
    if (existing) return existing;
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef ft = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), &i8ptr, 1, 0);
    LLVMValueRef fn = LLVMAddFunction(g->mod, name, ft);
    LLVMSetLinkage(fn, LLVMInternalLinkage);
    build_collection_release_body(g, g->site_coll[site], g->site_coll_elem[site], fn);
    return fn;
}
void emit_arc_release_typed(zan_irgen_t *g, zan_type_t *type, LLVMValueRef v) {
    (void)type;
    if (!v || LLVMGetTypeKind(LLVMTypeOf(v)) != LLVMPointerTypeKind) return;
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    if (LLVMTypeOf(v) != i8ptr) v = LLVMBuildBitCast(g->builder, v, i8ptr, "arc.rlt");
    zan_call2(g->builder, LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), &i8ptr, 1, 0),
                   g->rt_release_dyn, &v, 1, "");
}

/* 构建the bodies of every class release function */
void emit_all_class_releases(zan_irgen_t *g) {
    for (int i = 0; i < g->struct_type_count; i++) {
        zan_symbol_t *sym = g->struct_types[i].sym;
        if (!sym || !sym->type || !is_arc_managed_type(sym->type)) continue;
        LLVMValueRef fn = get_class_release_decl(g, sym, NULL);
        if (fn) build_class_release_body(g, sym, NULL, fn);
    }
    for (int i = 0; g->site_inst && i < g->leak_site_count; i++) {
        zan_symbol_t *sym = g->site_syms ? g->site_syms[i] : NULL;
        zan_type_t *inst = g->site_inst[i];
        if (!sym || !inst || !inst->type_arg_count || !type_is_concrete(inst)) continue;
        int seen = 0;
        for (int j = 0; j < g->class_release_count && !seen; j++)
            if (g->class_release[j].sym == sym && g->class_release[j].inst &&
                types_equal(g->class_release[j].inst, inst))
                seen = 1;
        if (seen) continue;
        LLVMValueRef fn = get_class_release_decl(g, sym, inst);
        if (fn) build_class_release_body(g, sym, inst, fn);
    }
    /* A454 装箱 site：非托管结构体被装箱时 desc-init 会为它建释放函数
     * 壳（get_class_release_decl 把非泛型 inst 归一为 NULL，去重键与
     * loop 1/2 都对不上）——这里补 body（无托管字段即空体，只剩
     * 引用计数与 free 逻辑）。泛型实例化 site 已由上面的 loop 2 覆盖。 */
    for (int i = 0; g->site_syms && i < g->leak_site_count; i++) {
        zan_symbol_t *sym = g->site_syms[i];
        if (!sym || sym->kind != SYM_STRUCT) continue;
        if (!sym->type || is_arc_managed_type(sym->type)) continue;
        zan_type_t *inst = g->site_inst ? g->site_inst[i] : NULL;
        if (inst && inst->type_arg_count > 0) continue;
        LLVMValueRef fn = get_class_release_decl(g, sym, inst);
        if (fn) build_class_release_body(g, sym, NULL, fn);
    }
}
void emit_site_live_tables(zan_irgen_t *g) {
    if (g->desc_hdr || !g->g_site_live || !g->g_site_count) return;
    LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i8p = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    unsigned n = (unsigned)g->leak_site_count;
    LLVMTypeRef lt = LLVMArrayType(i64t, n);
    LLVMValueRef live = LLVMAddGlobal(g->mod, lt, "__zan_site_live_tbl");
    LLVMSetInitializer(live, LLVMConstNull(lt));
    LLVMSetLinkage(live, LLVMInternalLinkage);
    LLVMSetInitializer(g->g_site_live,
        LLVMConstBitCast(live, LLVMPointerType(i64t, 0)));
    LLVMTypeRef nt = LLVMArrayType(i8p, n);
    LLVMValueRef names = LLVMAddGlobal(g->mod, nt, "__zan_site_names_tbl");
    LLVMSetInitializer(names, LLVMConstNull(nt));
    LLVMSetLinkage(names, LLVMInternalLinkage);
    LLVMSetInitializer(g->g_site_names,
        LLVMConstBitCast(names, LLVMPointerType(i8p, 0)));
    LLVMSetInitializer(g->g_site_count, LLVMConstInt(i64t, n, 0));
}
void emit_site_dtor_table(zan_irgen_t *g) {
    if (!g->site_syms || g->desc_hdr) return;
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    int n = g->leak_site_count;
    if (n <= 0) return;
    LLVMValueRef *elems = (LLVMValueRef *)calloc((size_t)n, sizeof(LLVMValueRef));
    for (int i = 0; i < n; i++) {
        LLVMValueRef e = LLVMConstNull(i8ptr);
        if (g->site_coll && g->site_coll[i]) {
            LLVMValueRef fn = get_collection_release_decl(g, i);
            if (fn) e = LLVMConstBitCast(fn, i8ptr);
        } else if (g->site_syms[i]) {
            LLVMValueRef fn = get_class_release_decl(g, g->site_syms[i],
                                  g->site_inst ? g->site_inst[i] : NULL);
            if (fn) e = LLVMConstBitCast(fn, i8ptr);
        }
        elems[i] = e;
    }
    LLVMTypeRef at = LLVMArrayType(i8ptr, (unsigned)n);
    LLVMValueRef arr = LLVMAddGlobal(g->mod, at, "__zan_site_dtor_tbl");
    LLVMSetInitializer(arr, LLVMConstArray(i8ptr, elems, (unsigned)n));
    LLVMSetLinkage(arr, LLVMInternalLinkage);
    LLVMSetInitializer(g->g_site_dtors,
        LLVMConstBitCast(arr, LLVMPointerType(i8ptr, 0)));
    free(elems);
}
void emit_site_tyname_table(zan_irgen_t *g) {
    if (!g->site_syms || g->desc_hdr) return;
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    int n = g->leak_site_count;
    if (n <= 0) return;
    LLVMValueRef *elems = (LLVMValueRef *)calloc((size_t)n, sizeof(LLVMValueRef));
    for (int i = 0; i < n; i++) {
        elems[i] = LLVMConstNull(i8ptr);
        const char *names[64];
        int k = 0;
        if (g->site_coll && g->site_coll[i]) {
            int ck = g->site_coll[i];
            names[k++] = arc_collection_type_name(g, ck,
                g->site_coll_elem ? g->site_coll_elem[i] : NULL);
        } else if (g->site_syms[i]) {
            zan_symbol_t *cur = g->site_syms[i];
            while (cur && k < 63) {
                char buf[320];
                int len = (int)cur->name.len;
                if (len > 319) len = 319;
                memcpy(buf, cur->name.str, (size_t)len);
                buf[len] = 0;
                names[k++] = zan_arena_strdup(g->arena, buf, (size_t)len);
                if (cur->type) {
                    for (int ifi = 0; ifi < cur->type->interface_count && k < 63; ifi++) {
                        zan_type_t *iface = cur->type->interfaces[ifi];
                        if (iface && iface->sym) {
                            int ilen = (int)iface->sym->name.len;
                            if (ilen > 319) ilen = 319;
                            char ibuf[320];
                            memcpy(ibuf, iface->sym->name.str, (size_t)ilen);
                            ibuf[ilen] = 0;
                            names[k++] = zan_arena_strdup(g->arena, ibuf, (size_t)ilen);
                        }
                    }
                }
                cur = (cur->type && cur->type->base_type)
                          ? cur->type->base_type->sym : NULL;
            }
        }
        if (k == 0) continue;
        LLVMValueRef *nptr = (LLVMValueRef *)calloc((size_t)k + 1, sizeof(LLVMValueRef));
        for (int j = 0; j < k; j++)
            nptr[j] = zan_irgen_intern_string(g, names[j]);
        nptr[k] = LLVMConstNull(i8ptr);
        LLVMTypeRef at = LLVMArrayType(i8ptr, (unsigned)k + 1);
        char gname[64];
        snprintf(gname, sizeof(gname), "__zan_tynames_%d", i);
        LLVMValueRef gv = LLVMAddGlobal(g->mod, at, gname);
        LLVMSetInitializer(gv, LLVMConstArray(i8ptr, nptr, (unsigned)k + 1));
        LLVMSetLinkage(gv, LLVMInternalLinkage);
        LLVMSetGlobalConstant(gv, 1);
        LLVMSetUnnamedAddr(gv, LLVMGlobalUnnamedAddr);
        free(nptr);
        LLVMValueRef idxs[] = { LLVMConstInt(i64, 0, 0), LLVMConstInt(i64, 0, 0) };
        elems[i] = LLVMBuildGEP2(g->builder, at, gv, idxs, 2, "tn.ptr");
    }
    LLVMTypeRef at = LLVMArrayType(i8ptr, (unsigned)n);
    LLVMValueRef arr = LLVMAddGlobal(g->mod, at, "__zan_site_tyname_tbl");
    LLVMSetInitializer(arr, LLVMConstArray(i8ptr, elems, (unsigned)n));
    LLVMSetLinkage(arr, LLVMInternalLinkage);
    LLVMSetInitializer(g->g_site_tynames,
        LLVMConstBitCast(arr, LLVMPointerType(i8ptr, 0)));
    free(elems);
}
LLVMValueRef build_site_field_graph(zan_irgen_t *g, int site_idx) {
    char fg_name[64];
    snprintf(fg_name, sizeof(fg_name), "__zan_fg_%d", site_idx);
    LLVMValueRef existing = LLVMGetNamedGlobal(g->mod, fg_name);
    LLVMTypeRef i8p = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    if (existing) return LLVMConstBitCast(existing, i8p);

    LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
    uint32_t kind = 3;
    uint32_t ref_count = 0;
    uint32_t instance_size = 0;
    uint32_t offsets[256];
    memset(offsets, 0, sizeof(offsets));

    if (g->site_coll && g->site_coll[site_idx]) {
        int ck = g->site_coll[site_idx];
        if (ck == 1) { /* List */
            zan_type_t *elem_type = g->site_coll_elem ? g->site_coll_elem[site_idx] : NULL;
            if (elem_type && (is_arc_managed_type(elem_type) ||
                              elem_type->kind == TYPE_STRING ||
                              elem_type->kind == TYPE_DELEGATE ||
                              elem_type->kind == TYPE_ARRAY ||
                              elem_type->kind == TYPE_OBJECT)) {
                kind = 1;
                ref_count = 1;
                offsets[0] = 16;
            } else {
                kind = 3;
                ref_count = 0;
            }
        } else if (ck == 3) { /* Dict */
            kind = 2;
            ref_count = 3;
            offsets[0] = 16;
            offsets[1] = 24;
            offsets[2] = 0;
        } else {
            kind = 3;
            ref_count = 0;
        }
    } else if (g->site_syms && g->site_syms[site_idx]) {
        zan_symbol_t *sym = g->site_syms[site_idx];
        zan_type_t *inst = g->site_inst ? g->site_inst[site_idx] : NULL;
        struct zan_struct_type_entry *e = get_struct_entry(g, sym);
        if (e && e->field_llvm) {
            int vptr = class_vptr_offset(sym);
            unsigned long cursor = vptr ? 8 : 0;
            int slot = vptr;
            for (int i = 0; i < sym->member_count; i++) {
                zan_symbol_t *m = sym->members[i];
                if ((m->kind != SYM_FIELD && m->kind != SYM_PROPERTY) || field_member_is_static(m))
                    continue;
                unsigned long fa = abi_align_of(e->field_llvm[slot]);
                unsigned long off = e->explicit_layout ? e->field_offsets[slot] : di_align_up(cursor, fa);
                unsigned long fsize = abi_size_of(e->field_llvm[slot]);
                if (!e->explicit_layout) cursor = off + fsize;
                slot++;

                zan_type_t *ft = m->type;
                if (inst) ft = subst_type_param_deep(g, ft, inst);
                if (!ft) continue;
                if (!(m->modifiers & MOD_WEAK)) {
                    bool is_rc = (ft->kind == TYPE_STRING ||
                                  ft->kind == TYPE_DELEGATE ||
                                  ft->kind == TYPE_ARRAY ||
                                  is_arc_managed_type(ft) ||
                                  ft->kind == TYPE_OBJECT);
                    if (is_rc && ref_count < 256) {
                        offsets[ref_count++] = (uint32_t)off;
                    }
                }
            }
            instance_size = (uint32_t)cursor;
            kind = (ref_count > 0) ? 0 : 3;
        }
    } else {
        /* Closure or unknown site */
        kind = 0;
        ref_count = 1;
        offsets[0] = 16;
        instance_size = 24;
    }

    LLVMTypeRef ref_ty = LLVMStructType((LLVMTypeRef[]){ i32t, i32t }, 2, 0);
    LLVMTypeRef refs_arr_ty = LLVMArrayType(ref_ty, (unsigned)ref_count);
    LLVMTypeRef fg_fields_ty[5] = { i32t, i32t, i32t, i32t, refs_arr_ty };
    LLVMTypeRef fg_ty = LLVMStructType(fg_fields_ty, 5, 0);

    LLVMValueRef fg_gv = LLVMAddGlobal(g->mod, fg_ty, fg_name);
    LLVMSetLinkage(fg_gv, LLVMInternalLinkage);
    LLVMSetGlobalConstant(fg_gv, 1);

    LLVMValueRef *ref_elems = NULL;
    if (ref_count > 0) {
        ref_elems = (LLVMValueRef *)calloc((size_t)ref_count, sizeof(LLVMValueRef));
        for (uint32_t k = 0; k < ref_count; k++) {
            LLVMValueRef rvals[2] = {
                LLVMConstInt(i32t, offsets[k], 0),
                LLVMConstInt(i32t, 1, 0)
            };
            ref_elems[k] = LLVMConstStruct(rvals, 2, 0);
        }
    }
    LLVMValueRef refs_val = LLVMConstArray(ref_ty, ref_elems, (unsigned)ref_count);
    if (ref_elems) free(ref_elems);

    LLVMValueRef fg_init[5] = {
        LLVMConstInt(i32t, kind, 0),
        LLVMConstInt(i32t, ref_count, 0),
        LLVMConstInt(i32t, instance_size, 0),
        LLVMConstInt(i32t, 0, 0),
        refs_val
    };
    LLVMSetInitializer(fg_gv, LLVMConstStruct(fg_init, 5, 0));
    return LLVMConstBitCast(fg_gv, i8p);
}
void emit_site_field_graph_table(zan_irgen_t *g) {
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    int n = g->leak_site_count;
    if (n <= 0) return;
    LLVMValueRef *elems = (LLVMValueRef *)calloc((size_t)n, sizeof(LLVMValueRef));
    for (int i = 0; i < n; i++) {
        elems[i] = build_site_field_graph(g, i);
    }
    LLVMTypeRef at = LLVMArrayType(i8ptr, (unsigned)n);
    LLVMValueRef arr = LLVMAddGlobal(g->mod, at, "__zan_site_field_graph_tbl");
    LLVMSetInitializer(arr, LLVMConstArray(i8ptr, elems, (unsigned)n));
    LLVMSetLinkage(arr, LLVMInternalLinkage);
    if (g->g_site_field_graphs) {
        LLVMSetInitializer(g->g_site_field_graphs,
            LLVMConstBitCast(arr, LLVMPointerType(i8ptr, 0)));
    }
    free(elems);
}

void zan_irgen_emit_arc_desc_init(zan_irgen_t *g);

