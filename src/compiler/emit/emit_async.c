static int generic_variant_count(zan_irgen_t *g, zan_symbol_t *type_sym) {
    int n = 0;
    for (int i = 0; i < g->generic_inst_count; i++)
        if (g->generic_insts[i].type_sym == type_sym) n++;
    return n;
}
bool method_is_tp_template(zan_irgen_t *g, zan_ast_node_t *member);
static bool class_member_uses_tp(zan_irgen_t *g, zan_ast_node_t *decl,
                                 zan_ast_node_t *member);

static void emit_tp_erased_stub(zan_irgen_t *g, LLVMValueRef fn) {
    LLVMBasicBlockRef saved = LLVMGetInsertBlock(g->builder);
    LLVMBasicBlockRef bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
    LLVMPositionBuilderAtEnd(g->builder, bb);
    LLVMValueRef ab = LLVMGetNamedFunction(g->mod, "abort");
    LLVMTypeRef vfn = ab ? LLVMGlobalGetValueType(ab)
                         : LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), NULL, 0, 0);
    if (!ab) ab = LLVMAddFunction(g->mod, "abort", vfn);
    zan_call2(g->builder, vfn, ab, NULL, 0, "");
    LLVMBuildUnreachable(g->builder);
    if (saved) LLVMPositionBuilderAtEnd(g->builder, saved);
    else if (g->function_compactor) LLVMClearInsertionPosition(g->builder);
    zan_irgen_compact_completed(g, fn);
}

static bool is_task_like_type(zan_type_t *t) {
    if (!t) return false;
    if (t->kind == TYPE_TASK) return true;
    if (t->name.str && ((t->name.len >= 4 && memcmp(t->name.str, "Task", 4) == 0) ||
                        (t->name.len >= 9 && memcmp(t->name.str, "ValueTask", 9) == 0))) return true;
    if (t->sym && t->sym->name.str &&
        ((t->sym->name.len == 4 && memcmp(t->sym->name.str, "Task", 4) == 0) ||
         (t->sym->name.len == 9 && memcmp(t->sym->name.str, "ValueTask", 9) == 0))) return true;
    return false;
}

static void declare_async_method(zan_irgen_t *g, method_body_work_t *w,
                                 const char *fn_name) {
    zan_ast_node_t *member = w->member;
    zan_symbol_t *type_sym = w->type_sym;
    LLVMTypeRef *param_types = w->param_types;
    int param_count = w->param_count;
    int param_offset = w->param_offset;
    bool is_static = w->is_static;
    int total_params = param_count + param_offset;
    LLVMValueRef fn = NULL, resume_fn = NULL;
    LLVMTypeRef frame_type = NULL;
    w->is_async = true;
    g->has_async_work = true;
    w->handler_cap = 1;
        LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
        LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
        LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);

        {
            int anf_counter = 0;
            anf_normalize_block(g, member->method_decl.body, &anf_counter);
        }

        async_scan_t scan = { .g = g, .scope =
                              local_scope_new(g->arena) };
        scan.body = member->method_decl.body;
        if (!is_static)
            local_add(scan.scope, (zan_istr_t){(char *)"this", 4}, NULL,
                      type_sym->type);
        for (int k = 0; k < param_count; k++) {
            zan_ast_node_t *param = member->method_decl.params.items[k];
            local_add(scan.scope, param->param.name, NULL,
                      resolve_type_ctx(g, param->param.type));
        }
        zan_symbol_t *scan_saved_type = g->current_type_sym;
        g->current_type_sym = type_sym;
        async_scan_stmt(&scan, member->method_decl.body);
        g->current_type_sym = scan_saved_type;
        w->await_count = scan.await_count;
        w->alocals = scan.locals;
        w->alocal_count = scan.local_count;
        w->fin_depth_max = scan.fin_depth_max;
        w->try_count = scan.try_count;
        /* A549：处理器数组（HSTACK/HPENDING/CEXC*）的容量必须覆盖"发射点数"
         * 而不是"源码 try 数"。finally 体按提前离开的转移语句内联复制
         * （emit_finally_body），每个副本里的 try 都领新的 handler id；路由
         * 生成代码这类 try+catch+finally+转移 的形状实测 14 个发射点 vs 容量
         * 7，hid≥7 的 GEP 越界写进帧内相邻字段，把 __c 的高 32 位踩成
         * pending 计数 → 恢复后 retain 垃圾指针段错误。
         * 上限 = try_count × (1 + 转移语句数 × finally 嵌套深)。 */
        int xfer_count = async_count_transfers(member->method_decl.body);
        int fin_nest = scan.fin_depth_max > 0 ? scan.fin_depth_max : 1;
        w->handler_cap = scan.try_count * (1 + xfer_count * fin_nest);
        if (w->handler_cap < 1) w->handler_cap = 1;

        int locals_base = ASYNC_FRAME_FIRST_PARAM + total_params;
        w->sub_base = locals_base + w->alocal_count;
        int nfields = w->sub_base + (w->await_count > 0 ? 1 : 0);

        int rval_slot = -1;
        zan_type_t *raw_ret = w->ret_type ? w->ret_type : g->binder->type_void;
        if (member->method_decl.return_type) {
            raw_ret = zan_binder_resolve_type(g->binder, member->method_decl.return_type);
            if (w->cur_inst) raw_ret = subst_type_param_deep(g, raw_ret, w->cur_inst);
        }
        zan_type_t *ret_type = raw_ret;
        bool is_task = member->method_decl.is_task_return;
        if (ret_type && ret_type->type_arg_count > 0 && is_task_like_type(ret_type)) {
            ret_type = concretize(g, ret_type->type_args[0]);
            is_task = true;
        }
        /* B-ID64：非 Task 聚合 async 返回（`async Big M()`）随类型化返回值字段
         * 解禁——E3087"结果槽只有一个机器字"的限制在帧尾 RVAL 槽落地后失去
         * 依据：聚合返回存 RVAL、RESULT 槽承载槽地址，await 按值拷出、
         * fire-and-forget 走 co.orphan 按类型释放。 */
        LLVMTypeRef lret = map_type(g, ret_type);
        bool is_agg_ret = (lret && LLVMGetTypeKind(lret) == LLVMStructTypeKind);
        /* B-ID64 类型化返回值字段：凡非 void 返回一律在帧尾挂 RVAL 槽（此前
         * 只有聚合返回才有 ret_agg_slot）。RVAL 是返回值的权威类型化存储，
         * i64 RESULT 槽保留为兼容协议（头布局/调度器 scratch/Task<T> 轮询）。 */
        bool has_rval = (ret_type && ret_type->kind != TYPE_VOID && lret);
        if (has_rval) {
            rval_slot = nfields++;
        }
        w->rval_slot = rval_slot;

        LLVMTypeRef *fields = (LLVMTypeRef *)calloc((size_t)nfields, sizeof(LLVMTypeRef));
        fields[ASYNC_FRAME_SCHED] = i64;
        fields[ASYNC_FRAME_SCHED_STEP] = g->co_step_ptr;
        fields[ASYNC_FRAME_STATE] = i32;
        fields[ASYNC_FRAME_DONE] = i32;
        fields[ASYNC_FRAME_AWAITER] = i8ptr;
        fields[ASYNC_FRAME_AWAITER_STEP] = g->co_step_ptr;
        fields[ASYNC_FRAME_RESULT] = i64;
        fields[ASYNC_FRAME_PENDING_COUNT] = i32;
        fields[ASYNC_FRAME_CLEANUP] = g->co_step_ptr;
        fields[ASYNC_FRAME_HCOUNT] = i32;
        fields[ASYNC_FRAME_SELF_STEP] = g->co_step_ptr;
        fields[ASYNC_FRAME_EXC] = i8ptr;
        fields[ASYNC_FRAME_EXC_TID] = i8ptr;
        fields[ASYNC_FRAME_EXC_OWNED] = i32;
        fields[ASYNC_FRAME_CANCEL] = i32;
        fields[ASYNC_FRAME_CHILD] = i8ptr;
        fields[ASYNC_FRAME_LNEXT] = i8ptr;
        fields[ASYNC_FRAME_HSTACK] = LLVMArrayType(i32, (unsigned)w->handler_cap);
        fields[ASYNC_FRAME_CEXC] = LLVMArrayType(i8ptr, (unsigned)w->handler_cap);
        fields[ASYNC_FRAME_CEXC_OWNED] = LLVMArrayType(i32, (unsigned)w->handler_cap);
        fields[ASYNC_FRAME_CEXC_TID] = LLVMArrayType(i8ptr, (unsigned)w->handler_cap);
        {
            unsigned pending_cap = w->fin_depth_max > 0
                ? (unsigned)w->fin_depth_max + 1 : 0;
            LLVMTypeRef record_fields[] = { i64, i8ptr, i32, lret };
            LLVMTypeRef record = LLVMStructTypeInContext(g->ctx, record_fields,
                                                         is_agg_ret ? 4 : 3, 0);
            fields[ASYNC_FRAME_PENDING] = LLVMArrayType(record, pending_cap);
            fields[ASYNC_FRAME_HPENDING] = LLVMArrayType(i32, (unsigned)w->handler_cap);
        }
        for (int k = 0; k < total_params; k++) {
            fields[ASYNC_FRAME_FIRST_PARAM + k] = param_types[k];
        }
        for (int k = 0; k < w->alocal_count; k++) {
            w->alocals[k].frame_index = locals_base + k;
            fields[locals_base + k] = w->alocals[k].llvm;
        }
        if (w->await_count > 0) {
            fields[w->sub_base] = i8ptr;
        }
        if (has_rval) {
            fields[rval_slot] = lret;
        }
        char frame_name[560];
        snprintf(frame_name, sizeof(frame_name), "%s$frame", fn_name);
        w->frame_type = frame_type = LLVMStructCreateNamed(g->ctx, frame_name);
        LLVMStructSetBody(frame_type, fields, (unsigned)nfields, 0);
        free(fields);

        w->fn_type = LLVMFunctionType(i8ptr, param_types, (unsigned)total_params, 0);
        w->fn = fn = LLVMAddFunction(g->mod, fn_name, w->fn_type);
        if (!(g->emit_lib &&
              (member->method_decl.modifiers & MOD_PUBLIC) != 0))
            zan_set_module_local(fn);

        char resume_name[560];
        snprintf(resume_name, sizeof(resume_name), "%s$resume", fn_name);
        w->resume_fn = resume_fn = LLVMAddFunction(g->mod, resume_name, g->co_step_type);
        zan_set_module_local(resume_fn);
    (void)fn; (void)resume_fn; (void)frame_type; (void)total_params;
    (void)type_sym; (void)is_static;
}

/* 发射the ramp, resume state machine and cleanup fn of one async method */
static void emit_async_method_ir(zan_irgen_t *g, method_body_work_t *w) {
    zan_ast_node_t *member = w->member;
    zan_symbol_t *type_sym = w->type_sym;
    LLVMValueRef fn = w->fn;
    LLVMTypeRef *param_types = w->param_types;
    int param_count = w->param_count;
    int param_offset = w->param_offset;
    bool is_static = w->is_static;
    zan_ast_list_t *saved_mtps = g->cur_mtps;
    zan_type_t **saved_mbind = g->cur_mbind;
    zan_type_t *saved_inst = g->cur_inst;
    LLVMBasicBlockRef saved_bb = LLVMGetInsertBlock(g->builder);
    g->cur_mtps = w->mtps;
    g->cur_mbind = w->mbind;
    g->cur_inst = w->cur_inst;
        LLVMValueRef ramp_fn = fn;
        LLVMValueRef resume_fn = w->resume_fn;
        LLVMTypeRef frame_type = w->frame_type;
        LLVMTypeRef frame_ptr_ty = LLVMPointerType(frame_type, 0);
        LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
        LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
        LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
        int total_params = param_count + param_offset;

        LLVMValueRef cleanup_fn;
        {
            size_t rn_len = 0;
            const char *rn = LLVMGetValueName2(ramp_fn, &rn_len);
            char cleanup_name[560];
            snprintf(cleanup_name, sizeof(cleanup_name), "%.*s$cleanup",
                     (int)rn_len, rn ? rn : "");
            cleanup_fn = LLVMAddFunction(g->mod, cleanup_name, g->co_step_type);
            LLVMSetLinkage(cleanup_fn, LLVMInternalLinkage);
        }

        /* ---- ramp ---- */
        g->current_fn = ramp_fn;
        LLVMBasicBlockRef ramp_entry = LLVMAppendBasicBlockInContext(g->ctx, ramp_fn, "entry");
        LLVMPositionBuilderAtEnd(g->builder, ramp_entry);
        di_set_loc(g, member->loc);
        LLVMValueRef fsize = LLVMSizeOf(frame_type);
        LLVMValueRef raw;
        if (g->rt_co_frame_alloc) {
            raw = zan_call2(g->builder, g->rt_co_frame_alloc_type, g->rt_co_frame_alloc, &fsize, 1, "frame.raw");
        } else {
            LLVMTypeRef malloc_ty = LLVMGlobalGetValueType(g->fn_malloc);
            raw = zan_call2(g->builder, malloc_ty, g->fn_malloc, &fsize, 1, "frame.raw");
        }
        zan_irgen_emit_oom_check(g, ramp_fn, raw);
        emit_rt_alloc_add(g, fsize);
        {
            LLVMTypeRef i8ptr0 = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMTypeRef memset_ty = LLVMFunctionType(i8ptr0,
                (LLVMTypeRef[]){ i8ptr0, LLVMInt32TypeInContext(g->ctx),
                                 LLVMInt64TypeInContext(g->ctx) }, 3, 0);
            LLVMValueRef memset_fn = LLVMGetNamedFunction(g->mod, "memset");
            if (!memset_fn) memset_fn = LLVMAddFunction(g->mod, "memset", memset_ty);
            zan_call2(g->builder, memset_ty, memset_fn,
                (LLVMValueRef[]){ raw, LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0),
                                  fsize }, 3, "");
        }
        LLVMValueRef rframe = LLVMBuildBitCast(g->builder, raw, frame_ptr_ty, "frame");
        int this_owned = 0;
        zan_type_t *this_owned_type = NULL;
        LLVMBuildStore(g->builder, LLVMConstInt(i32, 0, 0),
            LLVMBuildStructGEP2(g->builder, frame_type, rframe, ASYNC_FRAME_STATE, "st"));
        LLVMBuildStore(g->builder, LLVMConstInt(i32, 0, 0),
            LLVMBuildStructGEP2(g->builder, frame_type, rframe, ASYNC_FRAME_DONE, "dn"));
        LLVMBuildStore(g->builder, LLVMConstNull(i8ptr),
            LLVMBuildStructGEP2(g->builder, frame_type, rframe, ASYNC_FRAME_AWAITER, "aw"));
        LLVMBuildStore(g->builder, LLVMConstNull(g->co_step_ptr),
            LLVMBuildStructGEP2(g->builder, frame_type, rframe, ASYNC_FRAME_AWAITER_STEP, "aws"));
        LLVMBuildStore(g->builder, LLVMConstInt(i64, 0, 0),
            LLVMBuildStructGEP2(g->builder, frame_type, rframe, ASYNC_FRAME_RESULT, "rs"));
        LLVMBuildStore(g->builder, cleanup_fn,
            LLVMBuildStructGEP2(g->builder, frame_type, rframe, ASYNC_FRAME_CLEANUP, "cl"));
        LLVMBuildStore(g->builder, resume_fn,
            LLVMBuildStructGEP2(g->builder, frame_type, rframe, ASYNC_FRAME_SELF_STEP, "selfstep"));
        for (int k = 0; k < total_params; k++) {
            LLVMValueRef pv = LLVMGetParam(ramp_fn, (unsigned)k);
            LLVMValueRef slot = LLVMBuildStructGEP2(g->builder, frame_type, rframe,
                (unsigned)(ASYNC_FRAME_FIRST_PARAM + k), "arg");
            LLVMBuildStore(g->builder, pv, slot);
            if (k < param_offset) {
                zan_type_t *rt = type_sym ? type_sym->type : NULL;
                if (is_rc_managed_type(rt) &&
                    LLVMGetTypeKind(LLVMTypeOf(pv)) == LLVMPointerTypeKind) {
                    emit_rc_retain_for_type(g, rt, pv);
                    this_owned = 1;
                    this_owned_type = rt;
                }
            } else {
                zan_ast_node_t *pn = member->method_decl.params.items[k - param_offset];
                if (!pn->param.by_ref) {
                    zan_type_t *pt = resolve_type_ctx(g, pn->param.type);
                    if (is_rc_managed_type(pt) &&
                        LLVMGetTypeKind(LLVMTypeOf(pv)) == LLVMPointerTypeKind) {
                        emit_rc_retain_for_type(g, pt, pv);
                    } else if (pt && pt->kind == TYPE_STRUCT &&
                               type_contains_collection_rc(g, pt, 0)) {
                        emit_collection_value_retain(g, pt, pv, 0);
                    }
                }
            }
        }
        for (int k = 0; k < w->alocal_count; k++) {
            async_local_t *al = &w->alocals[k];
            if (!al->boxed) continue;
            LLVMValueRef cell = emit_box_cell(g, al->decl->loc,
                map_type(g, al->ztype), al->ztype, NULL);
            LLVMValueRef tagged = LLVMBuildIntToPtr(g->builder,
                LLVMBuildOr(g->builder,
                    LLVMBuildPtrToInt(g->builder, cell, i64, "fl.box.i"),
                    LLVMConstInt(i64, ZAN_CLOSURE_TAG, 0), "fl.box.tag"),
                i8ptr, "fl.box.owner");
            LLVMBuildStore(g->builder, tagged,
                LLVMBuildStructGEP2(g->builder, frame_type, rframe,
                    (unsigned)al->frame_index, "fl.box.slot"));
        }
        LLVMBuildRet(g->builder, raw);

        /* ---- resume ---- */
        LLVMValueRef saved_fn = g->current_fn;
        g->current_fn = resume_fn;
        LLVMBasicBlockRef res_entry = LLVMAppendBasicBlockInContext(g->ctx, resume_fn, "entry");
        LLVMPositionBuilderAtEnd(g->builder, res_entry);
        di_set_loc(g, member->loc);
        LLVMValueRef fparam = LLVMGetParam(resume_fn, 0);
        LLVMValueRef sframe = LLVMBuildBitCast(g->builder, fparam, frame_ptr_ty, "frame");

        local_scope_t *locals = local_scope_new(g->arena);

        int alocal_count = w->alocal_count;
        int slot_total = total_params + alocal_count;
        zan_async_slot_t *slots = (zan_async_slot_t *)zan_arena_alloc(g->arena,
            sizeof(zan_async_slot_t) * (size_t)(slot_total > 0 ? slot_total : 1));
        int si = 0;
        LLVMValueRef res_this = NULL;
        if (!is_static) {
            res_this = LLVMBuildAlloca(g->builder, param_types[0], "this");
            slots[si].slot_alloca = res_this;
            slots[si].llvm = param_types[0];
            slots[si].frame_index = ASYNC_FRAME_FIRST_PARAM;
            si++;
        }
        for (int k = 0; k < param_count; k++) {
            zan_ast_node_t *param = member->method_decl.params.items[k];
            LLVMTypeRef pty = param_types[k + param_offset];
            LLVMValueRef pa = LLVMBuildAlloca(g->builder, pty, "p");
            zan_type_t *pt = resolve_type_ctx(g, param->param.type);
            LLVMValueRef binding = param->param.by_ref
                ? LLVMBuildLoad2(g->builder, pty, pa, "p.ref") : pa;
            local_add(locals, param->param.name, binding, pt);
            locals->vars[locals->count - 1].byref_slot = param->param.by_ref;
            if (param->param.by_ref && is_rc_managed_type(pt))
                locals->vars[locals->count - 1].arc_owned = 1;
            if (pt && pt->kind == TYPE_STRING)
                locals->vars[locals->count - 1].opaque_string = 1;
            if (!param->param.by_ref && is_rc_managed_type(pt) &&
                LLVMGetTypeKind(pty) == LLVMPointerTypeKind) {
                locals->vars[locals->count - 1].arc_owned = 1;
            } else if (!param->param.by_ref && pt && pt->kind == TYPE_STRUCT &&
                       type_contains_collection_rc(g, pt, 0)) {
                locals->vars[locals->count - 1].struct_rc = 1;
            }
            slots[si].slot_alloca = pa;
            slots[si].llvm = pty;
            slots[si].frame_index = ASYNC_FRAME_FIRST_PARAM + param_offset + k;
            si++;
        }
        for (int k = 0; k < alocal_count; k++) {
            LLVMValueRef la = LLVMBuildAlloca(g->builder, w->alocals[k].llvm, "fl");
            zan_istr_t fname;
            if (w->alocals[k].decl) {
                char flbuf[80];
                int fln = snprintf(flbuf, sizeof(flbuf), "$fl%d.%s", k,
                    w->alocals[k].name.str ? w->alocals[k].name.str : "l");
                char *fp = (char *)zan_arena_alloc(g->arena, (size_t)fln + 1);
                memcpy(fp, flbuf, (size_t)fln + 1);
                fname.str = fp;
                fname.len = (uint32_t)fln;
            } else {
                fname = w->alocals[k].name;
            }
            local_add(locals, fname, la, w->alocals[k].ztype);
            local_var_t *lv = &locals->vars[locals->count - 1];
            lv->async_decl = w->alocals[k].decl;
            lv->async_role = w->alocals[k].role;
            if (w->alocals[k].boxed) {
                LLVMValueRef tagged = LLVMBuildLoad2(g->builder, i8ptr, la,
                                                     "fl.box.owner");
                LLVMValueRef cell = emit_closure_untag(g, tagged);
                lv->alloca = box_value_ptr(g, cell,
                                          map_type(g, w->alocals[k].ztype));
                lv->box_cell = cell;
                lv->box_owner_slot = la;
                lv->box_owned = 1;
            } else if (!w->alocals[k].no_arc &&
                       is_rc_managed_type(w->alocals[k].ztype) &&
                       LLVMGetTypeKind(w->alocals[k].llvm) == LLVMPointerTypeKind) {
                lv->arc_owned = 1;
            } else if (!w->alocals[k].no_arc && w->alocals[k].ztype &&
                       w->alocals[k].ztype->kind == TYPE_STRUCT &&
                       type_contains_collection_rc(g, w->alocals[k].ztype, 0)) {
                lv->struct_rc = 1;
            }
            slots[si].slot_alloca = la;
            slots[si].llvm = w->alocals[k].llvm;
            slots[si].frame_index = w->alocals[k].frame_index;
            si++;
        }

        LLVMTypeRef saved_fn_ret = g->current_fn_ret_type;
        zan_type_t *saved_fn_zan_ret = g->current_fn_zan_ret_type;
        LLVMValueRef saved_this = g->current_this;
        zan_symbol_t *saved_type_sym = g->current_type_sym;
        LLVMValueRef saved_async_frame = g->current_async_frame;
        LLVMTypeRef saved_async_frame_type = g->current_async_frame_type;
        LLVMValueRef saved_async_resume_fn = g->current_async_resume_fn;
        zan_ast_node_t *saved_async_body = g->current_async_body;
        zan_type_t *saved_async_ret_type = g->current_async_ret_type;
        LLVMValueRef saved_async_switch = g->current_async_switch;
        int saved_next_state = g->current_async_next_state;
        int saved_sub_base = g->current_async_sub_base;
        int saved_sub_next = g->current_async_sub_next;
        int saved_rval_slot = g->current_async_rval_slot;
        void *saved_slots = (void *)g->current_async_slots;
        int saved_slot_count = g->current_async_slot_count;
        int saved_frame_local_count = g->current_async_frame_local_count;
        LLVMBasicBlockRef saved_complete_bb = g->current_async_complete_bb;
        LLVMValueRef saved_result_phi = g->current_async_result_phi;
        LLVMBasicBlockRef saved_requeue_bb = g->current_async_requeue_bb;
        LLVMBasicBlockRef saved_cancel_bb = g->current_async_cancel_bb;
        LLVMBasicBlockRef saved_rethrow_bb = g->current_async_rethrow_bb;
        LLVMBasicBlockRef saved_sub_rethrow_bb = g->current_async_sub_rethrow_bb;
        LLVMValueRef saved_sub_rethrow_phi_sub = g->current_async_sub_rethrow_phi_sub;
        LLVMValueRef saved_sub_rethrow_phi_ev = g->current_async_sub_rethrow_phi_ev;
        LLVMValueRef saved_state_ptr = g->current_async_state_ptr;
        LLVMValueRef saved_cancel_ptr = g->current_async_cancel_ptr;
        LLVMValueRef saved_self_i8 = g->current_async_self_i8;
        LLVMValueRef saved_self_int = g->current_async_self_int;
        LLVMValueRef saved_child_ptr = g->current_async_child_ptr;
        LLVMValueRef saved_sub_slot_ptr = g->current_async_sub_slot_ptr;
        LLVMValueRef saved_result_ptr = g->current_async_result_ptr;
        LLVMValueRef saved_eh_entry = g->current_async_eh_entry;
        LLVMBasicBlockRef saved_exc_bb = g->current_async_exc_bb;
        LLVMValueRef saved_rearm = g->current_async_rearm_switch;
        int saved_handler_next = g->current_async_handler_next;
        int saved_handler_cap = g->current_async_handler_cap;
        int saved_try_count = g->current_async_try_count;
        int saved_foreach_next = g->current_async_foreach_next;
        int saved_this_owned = g->current_async_this_owned;
        zan_type_t *saved_this_owned_type = g->current_async_this_type;
        int saved_lpad_depth = g->lpad_depth;

        g->current_fn_ret_type = LLVMVoidTypeInContext(g->ctx);
        int saved_throw_base = g->throw_locals_base;
        int saved_catch_cc = g->catch_cleanup_count;
        int saved_throw_cb = g->throw_catch_base;
        int saved_fin_c = g->finally_count;
        zan_irgen_pending_context_t saved_pending = g->pending;
        int saved_fin_lb = g->finally_loop_base;
        int saved_eh_c = g->eh_armed_count;
        int saved_eh_b = g->eh_armed_base;
        int saved_eh_lb = g->eh_armed_loop_base;
        g->throw_locals_base = 0;
        g->catch_cleanup_count = 0;
        g->throw_catch_base = 0;
        g->finally_count = 0;
        g->pending = (zan_irgen_pending_context_t){0};
        g->finally_loop_base = 0;
        g->eh_armed_base = g->eh_armed_count;
        g->eh_armed_loop_base = g->eh_armed_count;
        g->current_this = is_static ? NULL : res_this;
        g->current_type_sym = type_sym;
        g->current_async_frame = sframe;
        g->current_async_frame_type = frame_type;
        g->current_async_resume_fn = resume_fn;
        g->current_async_body = member->method_decl.body;
        g->current_async_ret_type = concretize(g,
            member->method_decl.return_type
                ? zan_binder_resolve_type(g->binder, member->method_decl.return_type)
                : g->binder->type_void);
        if (g->current_async_ret_type && g->current_async_ret_type->type_arg_count > 0 &&
            is_task_like_type(g->current_async_ret_type)) {
            g->current_async_ret_type = concretize(g, g->current_async_ret_type->type_args[0]);
        }
        g->current_fn_zan_ret_type = g->current_async_ret_type;
        g->current_async_rval_slot = w->rval_slot;
        g->current_async_next_state = 1;
        g->current_async_sub_base = w->sub_base;
        g->current_async_sub_next = 0;
        g->current_async_slots = slots;
        g->current_async_slot_count = slot_total;
        g->current_async_frame_local_count = locals->count;
        g->current_async_complete_bb = NULL;
        g->current_async_result_phi = NULL;
        g->current_async_suspend_ret_bb = NULL;
        g->current_async_requeue_bb = NULL;
        g->current_async_cancel_bb = NULL;
        g->current_async_rethrow_bb = NULL;
        g->current_async_sub_rethrow_bb = NULL;
        g->current_async_sub_rethrow_phi_sub = NULL;
        g->current_async_sub_rethrow_phi_ev = NULL;
        g->current_async_self_i8 = LLVMBuildBitCast(g->builder, sframe, i8ptr, "self.i8");
        g->current_async_state_ptr = LLVMBuildStructGEP2(g->builder, frame_type, sframe,
            ASYNC_FRAME_STATE, "self.state");
        g->current_async_cancel_ptr = LLVMBuildStructGEP2(g->builder, frame_type, sframe,
            ASYNC_FRAME_CANCEL, "fr.cancel.p");
        g->current_async_child_ptr = LLVMBuildStructGEP2(g->builder, frame_type, sframe,
            ASYNC_FRAME_CHILD, "self.child");
        /* sub 槽字段只在有 await 的方法里进 frame 布局（nfields = sub_base +
         * (await_count > 0 ? 1 : 0)）：await-free 的 async（如只包一层的
         * 包装方法）frame 没有这个字段，无条件 GEP sub_base 是越界索引，
         * LLVM 校验器直接拒绝模块（E3090）。没 await 就永远没有子协程
         * 挂载点，置空即可——懒取路径见 get_async_sub_slot_ptr。 */
        g->current_async_sub_slot_ptr = w->await_count > 0
            ? LLVMBuildStructGEP2(g->builder, frame_type, sframe,
                  (unsigned)w->sub_base, "self.sub_slot")
            : NULL;
        g->current_async_result_ptr = LLVMBuildStructGEP2(g->builder, frame_type, sframe,
            ASYNC_FRAME_RESULT, "self.res_slot");
        {
            LLVMTypeRef ptr_int_ty = g->target_is_wasm ? i32 : i64;
            g->current_async_self_int = LLVMBuildPtrToInt(g->builder, g->current_async_self_i8,
                ptr_int_ty, "self.int");
        }
        g->current_async_eh_entry = NULL;
        g->current_async_exc_bb = NULL;
        g->current_async_rearm_switch = NULL;
        g->current_async_handler_next = 0;
        g->current_async_handler_cap = w->handler_cap;
        g->current_async_try_count = w->try_count;
        g->current_async_foreach_next = 0;
        g->current_async_this_owned = this_owned;
        g->current_async_this_type = this_owned_type;

        emit_async_eh_prologue(g);

        LLVMBuildStore(g->builder,
            LLVMConstNull(LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0)),
            get_async_child_ptr(g));

        LLVMValueRef cancel_ptr = get_async_cancel_ptr(g);
        LLVMValueRef cancel_val = LLVMBuildLoad2(g->builder, i32, cancel_ptr, "fr.cancelled");
        LLVMBasicBlockRef can_bb = get_async_cancel_bb(g, locals);
        if (!can_bb) {
            can_bb = LLVMAppendBasicBlockInContext(g->ctx, resume_fn, "co.cancelled");
            LLVMPositionBuilderAtEnd(g->builder, can_bb);
            emit_async_complete(g, locals, NULL);
            g->current_async_cancel_bb = can_bb;
        }
        LLVMBasicBlockRef disp_bb = LLVMAppendBasicBlockInContext(g->ctx, resume_fn, "co.dispatch");
        LLVMBuildCondBr(g->builder,
            zan_icmp(g->builder, LLVMIntNE, cancel_val, LLVMConstInt(i32, 0, 0), "is.cancelled"),
            can_bb, disp_bb);

        LLVMPositionBuilderAtEnd(g->builder, disp_bb);
        LLVMValueRef state = LLVMBuildLoad2(g->builder, i32,
            get_async_state_ptr(g),
            "state");
        LLVMBasicBlockRef body_bb = LLVMAppendBasicBlockInContext(g->ctx, resume_fn, "co.start");
        LLVMValueRef sw = LLVMBuildSwitch(g->builder, state, body_bb, (unsigned)(w->await_count + 1));
        LLVMAddCase(sw, LLVMConstInt(i32, 0, 0), body_bb);
        g->current_async_switch = sw;

        LLVMPositionBuilderAtEnd(g->builder, body_bb);

        if (member->method_decl.body->kind == AST_BLOCK) {
            for (int k = 0; k < member->method_decl.body->block.stmts.count; k++) {
                zan_ast_node_t *bs = member->method_decl.body->block.stmts.items[k];
                emit_stmt(g, bs, locals);
            }
        } else {
            check_implicit_narrowing(g, g->current_fn_zan_ret_type,
                infer_expr_type(g, member->method_decl.body, locals),
                member->method_decl.body, "return");
            LLVMValueRef val = emit_expr(g, member->method_decl.body, locals);
            emit_async_complete(g, locals,
                coerce_to_frame_result(g, coerce_async_ret(g, val),
                                       g->current_async_ret_type));
        }

        if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder))) {
            emit_async_complete(g, locals, NULL);
        }

        emit_async_exc_epilogue(g, locals);
        emit_async_complete_epilogue(g, locals);
        emit_async_finalize_slots(g);

        g->current_fn = saved_fn;
        g->current_fn_ret_type = saved_fn_ret;
        g->current_fn_zan_ret_type = saved_fn_zan_ret;
        g->throw_locals_base = saved_throw_base;
        g->catch_cleanup_count = saved_catch_cc;
        g->throw_catch_base = saved_throw_cb;
        g->finally_count = saved_fin_c;
        g->pending = saved_pending;
        g->finally_loop_base = saved_fin_lb;
        g->eh_armed_count = saved_eh_c;
        g->eh_armed_base = saved_eh_b;
        g->eh_armed_loop_base = saved_eh_lb;
        g->lpad_depth = saved_lpad_depth;
        g->current_this = saved_this;
        g->current_type_sym = saved_type_sym;
        g->current_async_frame = saved_async_frame;
        g->current_async_frame_type = saved_async_frame_type;
        g->current_async_resume_fn = saved_async_resume_fn;
        g->current_async_body = saved_async_body;
        g->current_async_ret_type = saved_async_ret_type;
        g->current_async_switch = saved_async_switch;
        g->current_async_next_state = saved_next_state;
        g->current_async_sub_base = saved_sub_base;
        g->current_async_sub_next = saved_sub_next;
        g->current_async_rval_slot = saved_rval_slot;
        g->current_async_slots = saved_slots;
        g->current_async_slot_count = saved_slot_count;
        g->current_async_frame_local_count = saved_frame_local_count;
        g->current_async_complete_bb = saved_complete_bb;
        g->current_async_result_phi = saved_result_phi;
        g->current_async_requeue_bb = saved_requeue_bb;
        g->current_async_cancel_bb = saved_cancel_bb;
        g->current_async_rethrow_bb = saved_rethrow_bb;
        g->current_async_sub_rethrow_bb = saved_sub_rethrow_bb;
        g->current_async_sub_rethrow_phi_sub = saved_sub_rethrow_phi_sub;
        g->current_async_sub_rethrow_phi_ev = saved_sub_rethrow_phi_ev;
        g->current_async_state_ptr = saved_state_ptr;
        g->current_async_cancel_ptr = saved_cancel_ptr;
        g->current_async_self_i8 = saved_self_i8;
        g->current_async_self_int = saved_self_int;
        g->current_async_child_ptr = saved_child_ptr;
        g->current_async_sub_slot_ptr = saved_sub_slot_ptr;
        g->current_async_result_ptr = saved_result_ptr;
        g->current_async_eh_entry = saved_eh_entry;
        g->current_async_exc_bb = saved_exc_bb;
        g->current_async_rearm_switch = saved_rearm;
        g->current_async_handler_next = saved_handler_next;
        g->current_async_handler_cap = saved_handler_cap;
        g->current_async_try_count = saved_try_count;
        g->current_async_foreach_next = saved_foreach_next;
        g->current_async_this_owned = saved_this_owned;
        g->current_async_this_type = saved_this_owned_type;

        {
            LLVMBasicBlockRef cl_entry =
                LLVMAppendBasicBlockInContext(g->ctx, cleanup_fn, "entry");
            LLVMPositionBuilderAtEnd(g->builder, cl_entry);
            di_clear(g);
            LLVMValueRef saved_cl_fn = g->current_fn;
            g->current_fn = cleanup_fn;
            LLVMValueRef cparam = LLVMGetParam(cleanup_fn, 0);
            LLVMValueRef cframe = LLVMBuildBitCast(g->builder, cparam,
                frame_ptr_ty, "frame");
            if (this_owned && this_owned_type) {
                LLVMValueRef sp = LLVMBuildStructGEP2(g->builder, frame_type,
                    cframe, (unsigned)ASYNC_FRAME_FIRST_PARAM, "cl.this");
                emit_rc_release_for_type(g, this_owned_type,
                    LLVMBuildLoad2(g->builder, param_types[0], sp, "cl.thisv"));
            }
            for (int k = 0; k < param_count; k++) {
                zan_ast_node_t *param = member->method_decl.params.items[k];
                if (param->param.by_ref) continue;
                zan_type_t *pt = resolve_type_ctx(g, param->param.type);
                LLVMTypeRef pty = param_types[k + param_offset];
                if (!type_contains_collection_rc(g, pt, 0)) continue;
                LLVMValueRef sp = LLVMBuildStructGEP2(g->builder, frame_type, cframe,
                    (unsigned)(ASYNC_FRAME_FIRST_PARAM + param_offset + k), "cl.p");
                LLVMValueRef v = LLVMBuildLoad2(g->builder, pty, sp, "cl.pv");
                emit_collection_value_release(g, pt, v, 0);
            }
            for (int k = 0; k < w->alocal_count; k++) {
                async_local_t *al = &w->alocals[k];
                if (al->boxed) {
                    LLVMValueRef sp = LLVMBuildStructGEP2(g->builder, frame_type,
                        cframe, (unsigned)al->frame_index, "cl.box");
                    emit_closure_release(g,
                        LLVMBuildLoad2(g->builder, i8ptr, sp, "cl.box.owner"));
                    LLVMBuildStore(g->builder, LLVMConstNull(i8ptr), sp);
                    continue;
                }
                if (al->no_arc || !type_contains_collection_rc(g, al->ztype, 0))
                    continue;
                LLVMValueRef sp = LLVMBuildStructGEP2(g->builder, frame_type, cframe,
                    (unsigned)al->frame_index, "cl.l");
                LLVMValueRef v = LLVMBuildLoad2(g->builder, al->llvm, sp, "cl.lv");
                emit_collection_value_release(g, al->ztype, v, 0);
            }
            emit_async_pending_discard(g, cframe, frame_type, LLVMConstInt(i32, 0, 0));
            emit_co_cancel_delay(g, cparam);
            zan_emit_frame_free(g, cparam);
            LLVMBuildRetVoid(g->builder);
            g->current_fn = saved_cl_fn;
        }
        LLVMValueRef pending_discard_fn = NULL;
        if (LLVMGetArrayLength(LLVMStructGetTypeAtIndex(frame_type, ASYNC_FRAME_PENDING))) {
            emit_async_pending_discard_body(g, frame_type, w->ret_type);
            pending_discard_fn = async_pending_discard_fn(g, frame_type);
        }
        if (saved_bb) LLVMPositionBuilderAtEnd(g->builder, saved_bb);
        else LLVMClearInsertionPosition(g->builder);
        zan_irgen_compact_completed(g, resume_fn);
        zan_irgen_compact_completed(g, ramp_fn);
        zan_irgen_compact_completed(g, cleanup_fn);
        if (pending_discard_fn) zan_irgen_compact_completed(g, pending_discard_fn);
        free(param_types);
    g->cur_mtps = saved_mtps;
    g->cur_mbind = saved_mbind;
    g->cur_inst = saved_inst;
    if (saved_bb) LLVMPositionBuilderAtEnd(g->builder, saved_bb);
    (void)type_sym; (void)fn;
}

static void own_written_param(zan_irgen_t *g, local_scope_t *locals,
                              zan_ast_node_t *param, zan_type_t *pt,
                              LLVMTypeRef pty, LLVMValueRef pv,
                              zan_ast_node_t *body) {
    if (!pt || pt->kind == TYPE_OBJECT || !is_rc_managed_type(pt)) return;
    if (LLVMGetTypeKind(pty) != LLVMPointerTypeKind) return;
    if (locals && locals->count > 0 &&
        locals->vars[locals->count - 1].box_cell) return;
    if (!body_writes_ident(g, body, param->param.name)) return;
    emit_rc_retain_for_type(g, pt, pv);
    arc_own_local(g, locals);
}

/* 抽象方法（abstract）没有实现体，但它是虚表槽位的一员：派生类的
 * override 要落进同一个槽位，基类这一格也得有个可调用的目标。
 * 这里发射一个「未被覆盖就致命失败」的桩——正常路径永远走不到它，
 * 真被调到说明派生类漏了覆盖（或者有人直接 new 了抽象类）。
 *
 * 桩用 zan_rt_fatal 而不是软告警：漏覆盖是编程错误，不能悄悄放过。 */
static void emit_abstract_method_stub(zan_irgen_t *g, LLVMValueRef fn,
                                      zan_symbol_t *type_sym,
                                      zan_ast_node_t *member) {
    LLVMBasicBlockRef bb =
        LLVMAppendBasicBlockInContext(g->ctx, fn, "abstract.stub");
    LLVMPositionBuilderAtEnd(g->builder, bb);

    char msg[512];
    snprintf(msg, sizeof(msg),
             "abstract method '%.*s.%.*s' was not overridden",
             (int)type_sym->name.len, type_sym->name.str,
             (int)member->method_decl.name.len, member->method_decl.name.str);

    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef ft = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx),
                                       (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0);
    LLVMValueRef fatal = LLVMGetNamedFunction(g->mod, "zan_rt_fatal");
    if (!fatal) fatal = LLVMAddFunction(g->mod, "zan_rt_fatal", ft);
    LLVMValueRef args[2] = { zan_irgen_intern_string(g, "abstract"),
                              zan_irgen_intern_string(g, msg) };
    zan_call2(g->builder, ft, fatal, args, 2, "");
    /* zan_rt_fatal 不会返回；unreachable 让非 void 返回类型也合法。 */
    LLVMBuildUnreachable(g->builder);
}

static method_body_work_t *declare_user_methods(zan_irgen_t *g,
                                                 zan_ast_node_t *unit,
                                                 int *out_work_count) {
    discover_generic_insts(g, unit);

    int work_cap = 0;
    for (int i = 0; i < unit->comp_unit.decls.count; i++) {
        zan_ast_node_t *decl = unit->comp_unit.decls.items[i];
        if (decl->kind == AST_CLASS_DECL || decl->kind == AST_STRUCT_DECL) {
            zan_symbol_t *ts = zan_binder_lookup(g->binder, decl->type_decl.name);
            int variants = 1 + (ts ? generic_variant_count(g, ts) : 0);
            work_cap += decl->type_decl.members.count * variants;
        }
    }
    method_body_work_t *work = NULL;
    int work_count = 0;
    if (work_cap > 0) {
        work = (method_body_work_t *)calloc((size_t)work_cap, sizeof(method_body_work_t));
    }

    for (int i = 0; i < unit->comp_unit.decls.count; i++) {
        zan_ast_node_t *decl = unit->comp_unit.decls.items[i];
        if (decl->kind != AST_CLASS_DECL && decl->kind != AST_STRUCT_DECL) continue;

        zan_symbol_t *type_sym = zan_binder_lookup(g->binder, decl->type_decl.name);
        if (!type_sym) continue;

        /* 发射one variant per (erased + each concrete instantiation) */
        int nvar = 0, var_cap = 8;
        zan_type_t **variants =
            (zan_type_t **)malloc((size_t)var_cap * sizeof(*variants));
        variants[nvar++] = NULL;
        if (is_user_generic_sym(type_sym)) {
            for (int gi = 0; gi < g->generic_inst_count; gi++) {
                if (g->generic_insts[gi].type_sym != type_sym) continue;
                if (nvar == var_cap) {
                    var_cap *= 2;
                    variants = (zan_type_t **)realloc(variants,
                        (size_t)var_cap * sizeof(*variants));
                }
                variants[nvar++] = g->generic_insts[gi].inst;
            }
        }

        for (int vi = 0; vi < nvar; vi++) {
        zan_type_t *cur_variant = variants[vi];
        char vsuffix[256];
        vsuffix[0] = '\0';
        if (cur_variant) mangle_inst_suffix(vsuffix, sizeof(vsuffix), cur_variant);

        for (int j = 0; j < decl->type_decl.members.count; j++) {
            zan_ast_node_t *member = decl->type_decl.members.items[j];
            bool is_ctor = (member->kind == AST_CONSTRUCTOR_DECL);
            if (member->kind != AST_METHOD_DECL && !is_ctor) continue;
            bool is_type_init = is_ctor &&
                (member->method_decl.modifiers & MOD_STATIC) != 0;
            if (is_type_init && cur_variant) continue;
            if (method_is_tp_template(g, member)) continue;
            bool is_extern_decl =
                member->kind == AST_METHOD_DECL && !member->method_decl.body &&
                (zan_ast_method_extern_lib(member).str ||
                 (member->method_decl.modifiers & MOD_EXTERN) != 0);
            if (cur_variant && is_extern_decl)
                continue;

            if (is_extern_decl) {
                int pc = member->method_decl.params.count;
                LLVMTypeRef *pt = (LLVMTypeRef *)calloc((size_t)(pc > 0 ? pc : 1), sizeof(LLVMTypeRef));
                zan_type_t **pzt = (zan_type_t **)calloc((size_t)(pc > 0 ? pc : 1), sizeof(zan_type_t *));
                for (int k = 0; k < pc; k++) {
                    zan_ast_node_t *param = member->method_decl.params.items[k];
                    zan_type_t *ptype = zan_binder_resolve_type(g->binder, param->param.type);
                    pzt[k] = ptype;
                    LLVMTypeRef llvm_pt = map_type(g, ptype);
                    pt[k] = param->param.by_ref
                        ? LLVMPointerType(llvm_pt, 0)
                        : llvm_pt;
                }
                zan_type_t *rt = member->method_decl.return_type
                    ? zan_binder_resolve_type(g->binder, member->method_decl.return_type)
                    : g->binder->type_void;
                LLVMTypeRef llvm_rt = map_type(g, rt);
                LLVMTypeRef ft = LLVMFunctionType(llvm_rt, pt, (unsigned)pc,
                    member->method_decl.is_variadic ? 1 : 0);
                char ext_name[256];
                zan_istr_t *ep = zan_ast_method_entry_point(member);
                if (ep) {
                    snprintf(ext_name, sizeof(ext_name), "%.*s",
                             (int)ep->len, ep->str);
                } else {
                    snprintf(ext_name, sizeof(ext_name), "%.*s",
                             (int)member->method_decl.name.len,
                             member->method_decl.name.str);
                }
                if (strncmp(ext_name, "zan_file_", 9) == 0 ||
                    strncmp(ext_name, "zan_pkg_", 8) == 0) {
                    g->uses_file_runtime = true;
                }
                if (strncmp(ext_name, "zan_atomic_int_", 15) == 0 ||
                    strncmp(ext_name, "zan_shared_", 11) == 0 ||
                    strncmp(ext_name, "zan_thread_", 11) == 0 ||
                    strncmp(ext_name, "zan_dispatch_", 13) == 0 ||
                    strncmp(ext_name, "zan_monotonic_", 14) == 0 ||
                    strncmp(ext_name, "zan_stopwatch_", 14) == 0 ||
                    strncmp(ext_name, "zan_monitor_", 12) == 0 ||
                    strncmp(ext_name, "zan_mmap_", 9) == 0 ||
                    strncmp(ext_name, "zan_exe_dir_", 12) == 0 ||
                    strncmp(ext_name, "zan_dir_list_", 13) == 0 ||
                    strncmp(ext_name, "zan_plat_", 9) == 0) {
                    if (getenv("ZAN_TRACE_SYNC"))
                        fprintf(stderr, "[sync-flag] %s\n", ext_name);
                    g->uses_sync_runtime = true;
                }
                if (strncmp(ext_name, "zan_io_", 7) == 0) {
                    g->uses_socket_async = true;
                }
                if (strncmp(ext_name, "zan_gate_", 9) == 0) {
                    g->uses_socket_async = true;
                }
                if (strncmp(ext_name, "zan_timer_", 10) == 0) {
                    g->uses_timer_runtime = true;
                }
                if (strncmp(ext_name, "zan_embed_", 10) == 0) {
                    g->uses_embed_api = true;
                    if (strncmp(ext_name, "zan_embed_decode", 16) == 0 ||
                        strncmp(ext_name, "zan_embed_rawlen", 16) == 0) {
                        g->uses_inflate = true;
                    }
                }
                LLVMValueRef efn = NULL;
                if (!member->method_decl.is_variadic)
                    efn = abi_extern_thunk(g, ext_name, ft);
                if (!efn) efn = LLVMGetNamedFunction(g->mod, ext_name);
                if (!efn) {
                    efn = LLVMAddFunction(g->mod, ext_name, ft);
                    abi_add_int_ext_attrs(g, efn, rt, pzt, pc);
                }
                free(pzt);
                zan_symbol_t *method_sym = method_sym_for_decl(type_sym, member);
                if (method_sym) {
                    irgen_register_function(g, method_sym, efn, ft);
                }
                zan_istr_t ext_lib = zan_ast_method_extern_lib(member);
                if (ext_lib.str) {
                    bool already = false;
                    for (int li = 0; li < g->extern_lib_count; li++) {
                        if (g->extern_libs[li].len == ext_lib.len &&
                            memcmp(g->extern_libs[li].str, ext_lib.str,
                                   ext_lib.len) == 0) {
                            already = true;
                            break;
                        }
                    }
                    if (!already &&
                        ZAN_TAB_ENSURE(g->extern_libs, g->extern_lib_count,
                                       g->extern_lib_cap, 16)) {
                        g->extern_libs[g->extern_lib_count++] = ext_lib;
                    }
                }
                if (ext_lib.str) {
                    zan_istr_t *ep = zan_ast_method_entry_point(member);
                    zan_istr_t sym = ep ? *ep : member->method_decl.name;
                    bool seen = false;
                    for (int fi = 0; fi < g->extern_fn_count; fi++) {
                        if (g->extern_fns[fi].name.len == sym.len &&
                            memcmp(g->extern_fns[fi].name.str, sym.str, sym.len) == 0) {
                            seen = true;
                            break;
                        }
                    }
                    if (!seen &&
                        ZAN_TAB_ENSURE(g->extern_fns, g->extern_fn_count,
                                       g->extern_fn_cap, 128)) {
                        g->extern_fns[g->extern_fn_count].lib = ext_lib;
                        g->extern_fns[g->extern_fn_count].name = sym;
                        g->extern_fn_count++;
                    }
                }
                free(pt);
                continue;
            }

            /* abstract 方法没有实现体，但它要占一个虚表槽位：继续往下走，
             * 生成函数与致命桩，而不是在这里跳过（跳过会让调用点落到
             * "could not be resolved to any callable" 的兜底错误）。 */
            bool is_abstract_method =
                !member->method_decl.body &&
                (member->method_decl.modifiers & MOD_ABSTRACT) != 0;
            if (!member->method_decl.body && !is_abstract_method) continue;

            bool is_static = (!is_ctor || is_type_init) &&
                (member->method_decl.modifiers & MOD_STATIC) != 0;
            if (is_static && member->method_decl.name.len == 4 &&
                memcmp(member->method_decl.name.str, "Main", 4) == 0 &&
                !(member->method_decl.modifiers & MOD_ASYNC)) continue;

            char fn_name[512];
            if (is_type_init) {
                snprintf(fn_name, sizeof(fn_name), "%.*s_cctor",
                         (int)decl->type_decl.name.len, decl->type_decl.name.str);
            } else if (is_ctor) {
                snprintf(fn_name, sizeof(fn_name), "%.*s_ctor%s",
                         (int)decl->type_decl.name.len, decl->type_decl.name.str,
                         vsuffix);
            } else {
                snprintf(fn_name, sizeof(fn_name), "%.*s_%.*s%s",
                         (int)decl->type_decl.name.len, decl->type_decl.name.str,
                         (int)member->method_decl.name.len, member->method_decl.name.str,
                         vsuffix);
                {
                    char base_name[512];
                    int oi = 2;
                    snprintf(base_name, sizeof(base_name), "%s", fn_name);
                    while (LLVMGetNamedFunction(g->mod, fn_name)) {
                        snprintf(fn_name, sizeof(fn_name), "%s$o%d",
                                 base_name, oi++);
                    }
                }
            }

            int param_count = member->method_decl.params.count;
            int total_params = is_static ? param_count : param_count + 1;
            LLVMTypeRef *param_types = (LLVMTypeRef *)calloc((size_t)(total_params > 0 ? total_params : 1), sizeof(LLVMTypeRef));

            int param_offset = 0;
            if (!is_static) {
                LLVMTypeRef struct_type = get_struct_llvm_type(g, type_sym);
                param_types[0] = struct_type ? LLVMPointerType(struct_type, 0)
                                             : LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                param_offset = 1;
            }

            for (int k = 0; k < param_count; k++) {
                zan_ast_node_t *param = member->method_decl.params.items[k];
                zan_type_t *pt = zan_binder_resolve_type(g->binder, param->param.type);
                if (cur_variant) pt = subst_type_param_deep(g, pt, cur_variant);
                param_types[k + param_offset] = param->param.by_ref
                    ? LLVMPointerType(map_type(g, pt), 0)
                    : map_type(g, pt);
            }

            zan_type_t *ret_type = is_ctor ? g->binder->type_void
                : (member->method_decl.return_type
                    ? zan_binder_resolve_type(g->binder, member->method_decl.return_type)
                    : g->binder->type_void);
            if (cur_variant) ret_type = subst_type_param_deep(g, ret_type, cur_variant);
            LLVMTypeRef llvm_ret = map_type(g, ret_type);
            bool is_async = !is_ctor && !is_abstract_method &&
                (member->method_decl.modifiers & MOD_ASYNC) != 0;

            LLVMTypeRef fn_type = NULL;
            LLVMValueRef fn = NULL;
            LLVMValueRef resume_fn = NULL;
            LLVMTypeRef frame_type = NULL;
            int a_await_count = 0;
            async_local_t *a_locals = NULL;
            int a_local_count = 0;
            int a_sub_base = 0;
	            int a_rval_slot = -1;
	            int a_handler_cap = 1;
	            int a_try_count = 0;

	            if (is_async) {
	                g->has_async_work = true;
	                method_body_work_t adecl;
	                memset(&adecl, 0, sizeof(adecl));
	                adecl.member = member;
	                adecl.type_sym = type_sym;
	                adecl.is_static = is_static;
	                adecl.param_types = param_types;
	                adecl.param_count = param_count;
	                adecl.param_offset = param_offset;
	                adecl.ret_type = ret_type;
	                adecl.llvm_ret = llvm_ret;
	                adecl.cur_inst = cur_variant;
	                declare_async_method(g, &adecl, fn_name);
	                fn = adecl.fn;
	                fn_type = adecl.fn_type;
	                resume_fn = adecl.resume_fn;
	                frame_type = adecl.frame_type;
	                a_await_count = adecl.await_count;
	                a_locals = adecl.alocals;
	                a_local_count = adecl.alocal_count;
	                a_sub_base = adecl.sub_base;
	                a_rval_slot = adecl.rval_slot;
	                a_handler_cap = adecl.handler_cap;
	                a_try_count = adecl.try_count;
            } else {
                fn_type = LLVMFunctionType(llvm_ret, param_types, (unsigned)total_params, 0);
                fn = LLVMAddFunction(g->mod, fn_name, fn_type);
                if (!(g->emit_lib &&
                      (member->method_decl.modifiers & MOD_PUBLIC) != 0))
                    zan_set_module_local(fn);
            }

            if (is_type_init) {
            } else if (is_ctor) {
                if (cur_variant) {
                    add_generic_ctor(g, type_sym, member,
                                     cur_variant->type_args,
                                     cur_variant->type_arg_count, param_count,
                                     fn, fn_type);
                } else {
                    g->ctors = irgen_grow(g->ctors, &g->ctor_cap,
                                          g->ctor_count + 1,
                                          sizeof(*g->ctors));
                    g->ctors[g->ctor_count].type_sym = type_sym;
                    g->ctors[g->ctor_count].decl = member;
                    g->ctors[g->ctor_count].fn = fn;
                    g->ctors[g->ctor_count].fn_type = fn_type;
                    g->ctors[g->ctor_count].param_count = param_count;
                    g->ctor_count++;
                }
            } else {
                zan_symbol_t *method_sym = method_sym_for_decl(type_sym, member);
                if (cur_variant) {
                    add_generic_fn(g, method_sym, cur_variant->type_args,
                                   cur_variant->type_arg_count, fn, fn_type);
                } else {
                    irgen_register_function(g, method_sym, fn, fn_type);
                }
            }

            if (!cur_variant && class_member_uses_tp(g, decl, member)) {
                emit_tp_erased_stub(g, fn);
                if (resume_fn) emit_tp_erased_stub(g, resume_fn);
                free(param_types);
                continue;
            }

            /* 抽象方法：函数与签名已经就位（调用点要靠它拿到函数类型），
             * 补上致命桩后收工——没有实现体，不进方法体发射队列。 */
            if (is_abstract_method) {
                LLVMValueRef saved_fn = g->current_fn;
                g->current_fn = fn;
                emit_abstract_method_stub(g, fn, type_sym, member);
                g->current_fn = saved_fn;
                free(param_types);
                continue;
            }

            if (work && work_count < work_cap) {
                work[work_count].member = member;
                work[work_count].type_sym = type_sym;
                work[work_count].fn = fn;
                work[work_count].param_types = param_types;
                work[work_count].param_count = param_count;
                work[work_count].param_offset = param_offset;
                work[work_count].is_static = is_static;
                work[work_count].llvm_ret = llvm_ret;
                work[work_count].ret_type = ret_type;
                work[work_count].is_async = is_async;
                work[work_count].resume_fn = resume_fn;
                work[work_count].frame_type = frame_type;
                work[work_count].await_count = a_await_count;
                work[work_count].alocals = a_locals;
                work[work_count].alocal_count = a_local_count;
	                work[work_count].sub_base = a_sub_base;
	                work[work_count].rval_slot = a_rval_slot;
	                work[work_count].handler_cap = a_handler_cap;
	                work[work_count].try_count = a_try_count;
	                work[work_count].cur_inst = cur_variant;
                work_count++;
            } else {
                free(param_types);
            }
        }
        }
        free(variants);
    }

    *out_work_count = work_count;
    return work;
}

typedef struct {
    LLVMValueRef key;
    int idx;
} work_fn_slot_t;

typedef struct {
    work_fn_slot_t *slots;
    int cap;
    const method_body_work_t *work;
    int work_count;
} work_fn_index_t;

