    LLVMValueRef arc_trace_ev = NULL;
    LLVMTypeRef arc_ra_ty = NULL;
    LLVMTypeRef arc_ev_ty = NULL;
    if (g->check_leaks) {
        LLVMTypeRef arc_i64 = LLVMInt64TypeInContext(g->ctx);
        LLVMTypeRef arc_i32t = LLVMInt32TypeInContext(g->ctx);
        LLVMTypeRef arc_i8p = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
        LLVMValueRef arc_trace_glob = LLVMAddGlobal(g->mod, arc_i32t, "__zan_arc_trace");
        LLVMSetInitializer(arc_trace_glob,
            LLVMConstInt(arc_i32t, (uint64_t)-1, 0));
        LLVMSetLinkage(arc_trace_glob, LLVMInternalLinkage);
        LLVMTypeRef arc_getenv_ty = LLVMFunctionType(arc_i8p,
            (LLVMTypeRef[]){ arc_i8p }, 1, 0);
        LLVMValueRef arc_getenv = LLVMGetNamedFunction(g->mod, "getenv");
        if (!arc_getenv) arc_getenv = LLVMAddFunction(g->mod, "getenv", arc_getenv_ty);
        arc_ra_ty = LLVMFunctionType(arc_i8p, (LLVMTypeRef[]){ arc_i32t }, 1, 0);
        LLVMValueRef arc_returnaddr = LLVMAddFunction(g->mod, "llvm.returnaddress", arc_ra_ty);
        LLVMTypeRef arc_ev_args[] = { arc_i8p, arc_i8p, arc_i64, arc_i64, arc_i8p };
        arc_ev_ty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), arc_ev_args, 5, 0);
        arc_trace_ev = LLVMAddFunction(g->mod, "__zan_arc_trace_ev", arc_ev_ty);
        LLVMSetLinkage(arc_trace_ev, LLVMInternalLinkage);
        LLVMBasicBlockRef ev_bb = LLVMAppendBasicBlockInContext(g->ctx, arc_trace_ev, "entry");
        LLVMBasicBlockRef ev_init = LLVMAppendBasicBlockInContext(g->ctx, arc_trace_ev, "init");
        LLVMBasicBlockRef ev_check = LLVMAppendBasicBlockInContext(g->ctx, arc_trace_ev, "check");
        LLVMBasicBlockRef ev_print = LLVMAppendBasicBlockInContext(g->ctx, arc_trace_ev, "print");
        LLVMBasicBlockRef ev_ret = LLVMAppendBasicBlockInContext(g->ctx, arc_trace_ev, "ret");
        LLVMPositionBuilderAtEnd(g->builder, ev_bb);
        LLVMValueRef ev_tag = LLVMGetParam(arc_trace_ev, 0);
        LLVMValueRef ev_obj = LLVMGetParam(arc_trace_ev, 1);
        LLVMValueRef ev_rc = LLVMGetParam(arc_trace_ev, 2);
        LLVMValueRef ev_site = LLVMGetParam(arc_trace_ev, 3);
        LLVMValueRef ev_ra = LLVMGetParam(arc_trace_ev, 4);
        LLVMValueRef ev_t = LLVMBuildLoad2(g->builder, arc_i32t, arc_trace_glob, "t");
        LLVMBuildCondBr(g->builder,
            zan_icmp(g->builder, LLVMIntEQ, ev_t,
                LLVMConstInt(arc_i32t, (uint64_t)-1, 0), "uninit"),
            ev_init, ev_check);
        LLVMPositionBuilderAtEnd(g->builder, ev_init);
        LLVMValueRef ev_key = zan_irgen_intern_string(g, "ZAN_ARC_TRACE");
        LLVMValueRef ev_hit = zan_call2(g->builder, arc_getenv_ty, arc_getenv,
            &ev_key, 1, "hit");
        LLVMValueRef ev_on = zan_icmp(g->builder, LLVMIntNE, ev_hit,
            LLVMConstNull(arc_i8p), "on");
        LLVMValueRef ev_on32 = LLVMBuildZExt(g->builder, ev_on, arc_i32t, "on32");
        LLVMBuildStore(g->builder, ev_on32, arc_trace_glob);
        LLVMBuildBr(g->builder, ev_check);
        LLVMPositionBuilderAtEnd(g->builder, ev_check);
        LLVMValueRef ev_phi = LLVMBuildPhi(g->builder, arc_i32t, "t2");
        LLVMValueRef ev_incomings[2] = { ev_t, ev_on32 };
        LLVMBasicBlockRef ev_inblocks[2] = { ev_bb, ev_init };
        LLVMAddIncoming(ev_phi, ev_incomings, ev_inblocks, 2);
        LLVMValueRef ev_off = zan_icmp(g->builder, LLVMIntEQ, ev_phi,
            LLVMConstInt(arc_i32t, 0, 0), "off");
        LLVMBuildCondBr(g->builder, ev_off, ev_ret, ev_print);
        LLVMPositionBuilderAtEnd(g->builder, ev_print);
        LLVMValueRef ev_obji = LLVMBuildPtrToInt(g->builder, ev_obj, arc_i64, "obji");
        LLVMValueRef ev_rai = LLVMBuildPtrToInt(g->builder, ev_ra, arc_i64, "rai");
        LLVMValueRef ev_msg = zan_irgen_intern_string(g,
            "[arc] %s obj=0x%llx site=%lld rc=%lld ra=0x%llx\n");
        LLVMValueRef ev_pargs[6] = { ev_msg, ev_tag, ev_obji, ev_site, ev_rc, ev_rai };
        zan_call2(g->builder, g->printf_type, g->fn_printf, ev_pargs, 6, "");
        LLVMBuildBr(g->builder, ev_ret);
        LLVMPositionBuilderAtEnd(g->builder, ev_ret);
        LLVMBuildRetVoid(g->builder);
        (void)ev_phi; (void)ev_obj; (void)ev_rc; (void)ev_site; (void)ev_ra;
        (void)ev_tag;
    }

    {
        LLVMTypeRef live_reg_args[] = { i8ptr, i64 };
        g->rt_live_register = LLVMAddFunction(g->mod, "zan_rt_live_register",
            LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), live_reg_args, 2, 0));
        LLVMTypeRef live_unreg_args[] = { i8ptr };
        g->rt_live_unregister = LLVMAddFunction(g->mod, "zan_rt_live_unregister",
            LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), live_unreg_args, 1, 0));
    }

    LLVMTypeRef retain_args[] = { i8ptr };
    LLVMTypeRef retain_type = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), retain_args, 1, 0);
    g->rt_retain = LLVMAddFunction(g->mod, "zan_rt_retain", retain_type);

    {
        LLVMBasicBlockRef bb = LLVMAppendBasicBlockInContext(g->ctx, g->rt_retain, "entry");
        LLVMPositionBuilderAtEnd(g->builder, bb);
        LLVMValueRef obj = LLVMGetParam(g->rt_retain, 0);
        LLVMValueRef is_null = zan_icmp(g->builder, LLVMIntEQ, obj,
            LLVMConstNull(i8ptr), "isnull");
        LLVMBasicBlockRef do_retain = LLVMAppendBasicBlockInContext(g->ctx, g->rt_retain, "retain");
        LLVMBasicBlockRef ret_bb = LLVMAppendBasicBlockInContext(g->ctx, g->rt_retain, "ret");
        LLVMBuildCondBr(g->builder, is_null, ret_bb, do_retain);
        LLVMPositionBuilderAtEnd(g->builder, do_retain);
        LLVMValueRef neg16 = LLVMConstInt(i64, (uint64_t)ZAN_OBJ_RC_OFF, 1);
        LLVMValueRef rc_ptr = LLVMBuildGEP2(g->builder, LLVMInt8TypeInContext(g->ctx), obj, &neg16, 1, "rcptr");
        LLVMValueRef rc_iptr = LLVMBuildBitCast(g->builder, rc_ptr,
            LLVMPointerType(i64, 0), "rciptr");
        /* 读一次引用计数：既是 immortal 判定，也是 freed 守卫的输入。 */
        LLVMValueRef rc_now = LLVMBuildLoad2(g->builder, i64, rc_iptr, "rcnow");
        if (g->arc_guard) {
            emit_arc_freed_use_check(g, g->rt_retain, rc_now, obj,
                                     ZAN_ARC_FAULT_USE_OBJ,
                                     "retain through a stale reference (object "
                                     "was freed: missing retain when stored)");
        }
        /* immortal 对象（字符串字面量等静态对象）的引用计数是哨兵值，不参与
         * 计数，更不能写——静态对象在只读段，写它就是访问冲突。字面量存进
         * object 槽时走的就是这条通用 retain。与 zan_rt_str_retain /
         * zan_rt_release_dyn 的哨兵判定同口径。 */
        LLVMValueRef is_immortal = zan_icmp(g->builder, LLVMIntEQ, rc_now,
            LLVMConstInt(i64, ZAN_STRING_SENTINEL_RC, 0), "isimmortal");
        LLVMBasicBlockRef imm_bb = LLVMAppendBasicBlockInContext(g->ctx, g->rt_retain, "immortal");
        LLVMBasicBlockRef add_bb = LLVMAppendBasicBlockInContext(g->ctx, g->rt_retain, "add");
        LLVMBuildCondBr(g->builder, is_immortal, imm_bb, add_bb);
        LLVMPositionBuilderAtEnd(g->builder, imm_bb);
        LLVMBuildBr(g->builder, ret_bb);
        LLVMPositionBuilderAtEnd(g->builder, add_bb);
        LLVMValueRef rc_pre = LLVMBuildAtomicRMW(g->builder, LLVMAtomicRMWBinOpAdd,
            rc_iptr, LLVMConstInt(i64, 1, 0), LLVMAtomicOrderingMonotonic, 0);
        emit_arc_underflow_check(g, g->rt_retain, rc_pre, obj, NULL,
                                 ZAN_ARC_FAULT_RETAIN,
                                 "retain of an already-freed object");
        if (arc_trace_ev) {
            LLVMValueRef trc_new = zan_add(g->builder, rc_pre,
                LLVMConstInt(i64, 1, 0), "trcnew");
            LLVMValueRef tneg8 = LLVMConstInt(i64, (uint64_t)ZAN_OBJ_SITE_OFF, 1);
            LLVMValueRef tsp = LLVMBuildGEP2(g->builder, LLVMInt8TypeInContext(g->ctx),
                obj, &tneg8, 1, "trsp");
            LLVMValueRef tsite = LLVMBuildLoad2(g->builder, i64,
                LLVMBuildBitCast(g->builder, tsp, LLVMPointerType(i64, 0), "trsip"),
                "trsite");
            emit_arc_trace_call(g, arc_trace_ev, "R", obj, trc_new, tsite);
        }
        LLVMBuildBr(g->builder, ret_bb);
        LLVMPositionBuilderAtEnd(g->builder, ret_bb);
        LLVMBuildRetVoid(g->builder);
    }

    emit_weak_runtime(g);

    LLVMTypeRef release_args[] = { i8ptr };
    LLVMTypeRef release_type = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), release_args, 1, 0);
    g->rt_release = LLVMAddFunction(g->mod, "zan_rt_release", release_type);

    {
        LLVMBasicBlockRef bb = LLVMAppendBasicBlockInContext(g->ctx, g->rt_release, "entry");
        LLVMPositionBuilderAtEnd(g->builder, bb);
        LLVMValueRef obj = LLVMGetParam(g->rt_release, 0);
        LLVMValueRef is_null = zan_icmp(g->builder, LLVMIntEQ, obj,
            LLVMConstNull(i8ptr), "isnull");
        LLVMBasicBlockRef do_release = LLVMAppendBasicBlockInContext(g->ctx, g->rt_release, "release");
        LLVMBasicBlockRef ret_bb = LLVMAppendBasicBlockInContext(g->ctx, g->rt_release, "ret");
        LLVMBuildCondBr(g->builder, is_null, ret_bb, do_release);
        LLVMPositionBuilderAtEnd(g->builder, do_release);
        LLVMValueRef neg16 = LLVMConstInt(i64, (uint64_t)ZAN_OBJ_RC_OFF, 1);
        LLVMValueRef neg8  = LLVMConstInt(i64, (uint64_t)ZAN_OBJ_SITE_OFF, 1);
        LLVMValueRef rc_ptr = LLVMBuildGEP2(g->builder, LLVMInt8TypeInContext(g->ctx), obj, &neg16, 1, "rcptr");
        LLVMValueRef rc_iptr = LLVMBuildBitCast(g->builder, rc_ptr,
            LLVMPointerType(i64, 0), "rciptr");
        /* 读一次引用计数：既是 immortal 判定，也是 freed 守卫的输入。 */
        LLVMValueRef rc_now = LLVMBuildLoad2(g->builder, i64, rc_iptr, "rcnow");
        if (g->arc_guard) {
            emit_arc_freed_use_check(g, g->rt_release, rc_now, obj,
                                     ZAN_ARC_FAULT_USE_OBJ,
                                     "release through a stale reference "
                                     "(object was already freed)");
        }
        /* immortal 对象（字符串字面量等静态对象）不参与引用计数：既不减计数，
         * 也不释放——静态对象在只读段，减计数同样是写只读段。 */
        LLVMValueRef is_immortal = zan_icmp(g->builder, LLVMIntEQ, rc_now,
            LLVMConstInt(i64, ZAN_STRING_SENTINEL_RC, 0), "isimmortal");
        LLVMBasicBlockRef imm_bb = LLVMAppendBasicBlockInContext(g->ctx, g->rt_release, "immortal");
        LLVMBasicBlockRef sub_bb = LLVMAppendBasicBlockInContext(g->ctx, g->rt_release, "sub");
        LLVMBuildCondBr(g->builder, is_immortal, imm_bb, sub_bb);
        LLVMPositionBuilderAtEnd(g->builder, imm_bb);
        LLVMBuildBr(g->builder, ret_bb);
        LLVMPositionBuilderAtEnd(g->builder, sub_bb);
        LLVMValueRef rc_old = LLVMBuildAtomicRMW(g->builder, LLVMAtomicRMWBinOpSub, rc_iptr,
            LLVMConstInt(i64, 1, 0), LLVMAtomicOrderingAcquireRelease, 0);
        {
            LLVMValueRef sp = LLVMBuildGEP2(g->builder, LLVMInt8TypeInContext(g->ctx),
                obj, &neg8, 1, "arcsp");
            LLVMValueRef sip = LLVMBuildBitCast(g->builder, sp,
                LLVMPointerType(i64, 0), "arcsip");
            LLVMValueRef s = LLVMBuildLoad2(g->builder, i64, sip, "arcsite");
            emit_arc_underflow_check(g, g->rt_release, rc_old, obj, s,
                                     ZAN_ARC_FAULT_RELEASE,
                                     "release of an already-freed object");
        }
        LLVMValueRef rc1 = zan_sub(g->builder, rc_old, LLVMConstInt(i64, 1, 0), "rc1");
        if (arc_trace_ev) {
            LLVMValueRef rsneg8 = LLVMConstInt(i64, (uint64_t)ZAN_OBJ_SITE_OFF, 1);
            LLVMValueRef rsp = LLVMBuildGEP2(g->builder, LLVMInt8TypeInContext(g->ctx),
                obj, &rsneg8, 1, "rrsp");
            LLVMValueRef rsite = LLVMBuildLoad2(g->builder, i64,
                LLVMBuildBitCast(g->builder, rsp, LLVMPointerType(i64, 0), "rrsip"),
                "rrsite");
            emit_arc_trace_call(g, arc_trace_ev, "r", obj, rc1, rsite);
        }
        LLVMValueRef is_zero = zan_icmp(g->builder, LLVMIntEQ, rc1,
            LLVMConstInt(i64, 0, 0), "iszero");
        LLVMBasicBlockRef free_bb = LLVMAppendBasicBlockInContext(g->ctx, g->rt_release, "dofree");
        LLVMBuildCondBr(g->builder, is_zero, free_bb, ret_bb);
        LLVMPositionBuilderAtEnd(g->builder, free_bb);
        zan_call2(g->builder, LLVMGlobalGetValueType(g->rt_weak_nil_all),
                  g->rt_weak_nil_all, &obj, 1, "");
        LLVMValueRef freefn = get_arc_free_decl(g);
        zan_call2(g->builder, LLVMGlobalGetValueType(freefn), freefn, &obj, 1, "");
        LLVMBuildBr(g->builder, ret_bb);
        LLVMPositionBuilderAtEnd(g->builder, ret_bb);
        LLVMBuildRetVoid(g->builder);
    }

    {
        if (!g->rt_arr_release) {
            LLVMTypeRef arr_fnty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), release_args, 1, 0);
            g->rt_arr_release = LLVMAddFunction(g->mod, "zan_rt_arr_release", arr_fnty);
        }
        LLVMTypeRef reld_type = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), release_args, 1, 0);
        g->rt_release_dyn = LLVMAddFunction(g->mod, "zan_rt_release_dyn", reld_type);
        LLVMBasicBlockRef bb = LLVMAppendBasicBlockInContext(g->ctx, g->rt_release_dyn, "entry");
        LLVMPositionBuilderAtEnd(g->builder, bb);
        LLVMValueRef obj = LLVMGetParam(g->rt_release_dyn, 0);
        LLVMValueRef is_null = zan_icmp(g->builder, LLVMIntEQ, obj, LLVMConstNull(i8ptr), "isnull");
        LLVMBasicBlockRef cont = LLVMAppendBasicBlockInContext(g->ctx, g->rt_release_dyn, "cont");
        LLVMBasicBlockRef lookup = LLVMAppendBasicBlockInContext(g->ctx, g->rt_release_dyn, "lookup");
        LLVMBasicBlockRef calld = LLVMAppendBasicBlockInContext(g->ctx, g->rt_release_dyn, "calld");
        LLVMBasicBlockRef fb = LLVMAppendBasicBlockInContext(g->ctx, g->rt_release_dyn, "fallback");
        LLVMBasicBlockRef ret_bb = LLVMAppendBasicBlockInContext(g->ctx, g->rt_release_dyn, "ret");
        LLVMBuildCondBr(g->builder, is_null, ret_bb, cont);
        LLVMPositionBuilderAtEnd(g->builder, cont);
        LLVMValueRef sent16 = LLVMConstInt(i64, (uint64_t)ZAN_OBJ_RC_OFF, 1);
        LLVMValueRef sent_rc = LLVMBuildLoad2(g->builder, i64,
            LLVMBuildBitCast(g->builder,
                LLVMBuildGEP2(g->builder, LLVMInt8TypeInContext(g->ctx), obj, &sent16, 1, "sentp"),
                LLVMPointerType(i64, 0), "sentip"), "sentrc");
        LLVMBasicBlockRef literal = LLVMAppendBasicBlockInContext(g->ctx, g->rt_release_dyn, "islit");
        LLVMBuildCondBr(g->builder,
            zan_icmp(g->builder, LLVMIntEQ, sent_rc,
                LLVMConstInt(i64, ZAN_STRING_SENTINEL_RC, 0), "issent"),
            ret_bb, literal);
        LLVMPositionBuilderAtEnd(g->builder, literal);
        if (arc_trace_ev) {
            LLVMValueRef dneg16 = LLVMConstInt(i64, (uint64_t)ZAN_OBJ_RC_OFF, 1);
            LLVMValueRef drcp = LLVMBuildGEP2(g->builder, LLVMInt8TypeInContext(g->ctx),
                obj, &dneg16, 1, "drcp");
            LLVMValueRef drc = LLVMBuildLoad2(g->builder, i64,
                LLVMBuildBitCast(g->builder, drcp, LLVMPointerType(i64, 0), "drcip"),
                "drc");
            LLVMValueRef dneg8 = LLVMConstInt(i64, (uint64_t)ZAN_OBJ_SITE_OFF, 1);
            LLVMValueRef dsp = LLVMBuildGEP2(g->builder, LLVMInt8TypeInContext(g->ctx),
                obj, &dneg8, 1, "ddsp");
            LLVMValueRef dsite = LLVMBuildLoad2(g->builder, i64,
                LLVMBuildBitCast(g->builder, dsp, LLVMPointerType(i64, 0), "ddsip"),
                "dsite");
            emit_arc_trace_call(g, arc_trace_ev, "D", obj, drc, dsite);
        }
        LLVMValueRef neg8 = LLVMConstInt(i64, (uint64_t)ZAN_OBJ_SITE_OFF, 1);
        LLVMValueRef sptr = LLVMBuildGEP2(g->builder, LLVMInt8TypeInContext(g->ctx), obj, &neg8, 1, "sptr");
        LLVMValueRef siptr = LLVMBuildBitCast(g->builder, sptr, LLVMPointerType(i64, 0), "siptr");
        LLVMValueRef dpp;
        if (!g->desc_hdr) {
            LLVMValueRef site = LLVMBuildLoad2(g->builder, i64, siptr, "site");
            LLVMValueRef bound = LLVMBuildLoad2(g->builder, i64,
                g->g_site_count, "bound");
            LLVMValueRef inrange = zan_icmp(g->builder, LLVMIntULT, site,
                bound, "inrange");
            LLVMBuildCondBr(g->builder, inrange, lookup, fb);
            LLVMPositionBuilderAtEnd(g->builder, lookup);
            LLVMValueRef dtbl = LLVMBuildLoad2(g->builder,
                LLVMPointerType(i8ptr, 0), g->g_site_dtors, "dtbl");
            LLVMValueRef dtor = LLVMBuildLoad2(g->builder, i8ptr,
                LLVMBuildGEP2(g->builder, i8ptr, dtbl, &site, 1, "dtorp"), "dtor");
            dpp = dtor;
            LLVMValueRef hasd = zan_icmp(g->builder, LLVMIntNE, dtor, LLVMConstNull(i8ptr), "hasd");
            LLVMBuildCondBr(g->builder, hasd, calld, fb);
        } else {
            LLVMValueRef desc = LLVMBuildLoad2(g->builder, i64, siptr, "desc");
            LLVMValueRef is_arr_magic = zan_icmp(g->builder, LLVMIntEQ, desc,
                LLVMConstInt(i64, ZAN_ARRAY_MAGIC, 0), "is.arrmagic");
            LLVMValueRef is_rank = zan_and(g->builder,
                zan_icmp(g->builder, LLVMIntUGT, desc, LLVMConstInt(i64, 0, 0), "is.rnz"),
                zan_icmp(g->builder, LLVMIntULT, desc, LLVMConstInt(i64, 4096, 0), "is.rank"),
                "is.isrank");
            LLVMValueRef is_raw_arr = zan_or(g->builder, is_arr_magic, is_rank, "is.rawarr");
            LLVMBasicBlockRef rel_arr_bb = LLVMAppendBasicBlockInContext(g->ctx, g->rt_release_dyn, "relarr");
            LLVMBasicBlockRef not_raw = LLVMAppendBasicBlockInContext(g->ctx, g->rt_release_dyn, "notraw");
            LLVMBuildCondBr(g->builder, is_raw_arr, rel_arr_bb, not_raw);

            LLVMPositionBuilderAtEnd(g->builder, rel_arr_bb);
            zan_call2(g->builder, LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), &i8ptr, 1, 0),
                g->rt_arr_release, &obj, 1, "");
            LLVMBuildBr(g->builder, ret_bb);

            LLVMPositionBuilderAtEnd(g->builder, not_raw);
            LLVMValueRef dnz = zan_icmp(g->builder, LLVMIntNE, desc,
                LLVMConstInt(i64, 0, 0), "descnz");
            LLVMBuildCondBr(g->builder, dnz, lookup, fb);
            LLVMPositionBuilderAtEnd(g->builder, lookup);
            LLVMValueRef desc_p = LLVMBuildIntToPtr(g->builder, desc,
                LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0), "desc.p");
            dpp = LLVMBuildLoad2(g->builder, i8ptr, desc_p, "dtor");
            LLVMValueRef hasd = zan_icmp(g->builder, LLVMIntNE, dpp,
                LLVMConstNull(i8ptr), "hasd");
            LLVMBuildCondBr(g->builder, hasd, calld, fb);
        }
        LLVMPositionBuilderAtEnd(g->builder, calld);
        LLVMTypeRef dfnty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), &i8ptr, 1, 0);
        LLVMValueRef dfn = LLVMBuildBitCast(g->builder, dpp, LLVMPointerType(dfnty, 0), "dfn");
        /* B-ID78 深树级联析构防护：类释放深度超过阈值时不再同步下钻，
         * 把 (释放函数, 死对象) 交给延迟队列，最外层出口迭代排空，
         * 数千层链表/树的释放不再耗尽线程栈 */
        {
            LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
            if (!g->arc_defer_depth) {
                g->arc_defer_depth = LLVMAddGlobal(g->mod, i32t,
                                                   "__zan_arc_rel_depth");
                LLVMSetInitializer(g->arc_defer_depth, LLVMConstNull(i32t));
                /* TLS 留待多线程释放语义定型；当前普通全局（探针单线程，
                 * ARC 释放主线程语义下正确） */
                if (!g->target_is_wasm && getenv("ZAN_ARC_DEFER_TLS"))
                    LLVMSetThreadLocal(g->arc_defer_depth, 1);
                LLVMTypeRef push_ty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx),
                    (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0);
                g->rt_defer_push = LLVMAddFunction(g->mod, "zan_rt_defer_push", push_ty);
                LLVMTypeRef pend_ty = LLVMFunctionType(i32t, NULL, 0, 0);
                g->rt_defer_pending = LLVMAddFunction(g->mod, "zan_rt_defer_pending", pend_ty);
                LLVMTypeRef drain_ty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), NULL, 0, 0);
                g->rt_defer_drain = LLVMAddFunction(g->mod, "zan_rt_defer_drain", drain_ty);
            }
            enum { ZAN_ARC_DEFER_DEPTH = 100 };
            LLVMValueRef dcur = LLVMBuildLoad2(g->builder, i32t,
                                               g->arc_defer_depth, "adepth");
            LLVMBasicBlockRef defer_bb = LLVMAppendBasicBlockInContext(g->ctx, g->rt_release_dyn, "adefer");
            LLVMBasicBlockRef sync_bb = LLVMAppendBasicBlockInContext(g->ctx, g->rt_release_dyn, "async2");
            LLVMBasicBlockRef drain_bb = LLVMAppendBasicBlockInContext(g->ctx, g->rt_release_dyn, "adrain");
            LLVMBasicBlockRef tail_bb = LLVMAppendBasicBlockInContext(g->ctx, g->rt_release_dyn, "atail");
            LLVMBuildCondBr(g->builder,
                zan_icmp(g->builder, LLVMIntUGE, dcur,
                         LLVMConstInt(i32t, ZAN_ARC_DEFER_DEPTH, 0), "adeep"),
                defer_bb, sync_bb);
            /* 超阈值：挂队列立即返回，不同步下钻 */
            LLVMPositionBuilderAtEnd(g->builder, defer_bb);
            zan_call2(g->builder, LLVMGlobalGetValueType(g->rt_defer_push),
                      g->rt_defer_push, (LLVMValueRef[]){ dpp, obj }, 2, "");
            LLVMBuildBr(g->builder, ret_bb);
            /* 同步路径：深度 +1 → 调类释放 → 读回实际值 -1（嵌套内层
             * 会在本层 call 期间涨落，入口快照不可复用）→ 归零且有
             * 欠账则排空 */
            LLVMPositionBuilderAtEnd(g->builder, sync_bb);
            LLVMBuildStore(g->builder,
                zan_add(g->builder, dcur, LLVMConstInt(i32t, 1, 0), "aup"),
                g->arc_defer_depth);
            zan_call2(g->builder, dfnty, dfn, &obj, 1, "");
            LLVMValueRef dnow = LLVMBuildLoad2(g->builder, i32t,
                                               g->arc_defer_depth, "adnow");
            LLVMValueRef ddown = zan_add(g->builder, dnow,
                LLVMConstInt(i32t, (unsigned)-1, 0), "adown");
            LLVMBuildStore(g->builder, ddown, g->arc_defer_depth);
            LLVMValueRef dzero = zan_icmp(g->builder, LLVMIntEQ, ddown,
                LLVMConstInt(i32t, 0, 0), "azero");
            LLVMValueRef pend = zan_call2(g->builder,
                LLVMGlobalGetValueType(g->rt_defer_pending),
                g->rt_defer_pending, NULL, 0, "apend");
            LLVMValueRef pend1 = zan_icmp(g->builder, LLVMIntNE, pend,
                LLVMConstInt(i32t, 0, 0), "apend1");
            LLVMBuildCondBr(g->builder, zan_and(g->builder, dzero, pend1, "ado"),
                drain_bb, tail_bb);
            LLVMPositionBuilderAtEnd(g->builder, drain_bb);
            zan_call2(g->builder, LLVMGlobalGetValueType(g->rt_defer_drain),
                      g->rt_defer_drain, NULL, 0, "");
            LLVMBuildBr(g->builder, tail_bb);
            LLVMPositionBuilderAtEnd(g->builder, tail_bb);
        }
        /* B-ID78 回归补丁：defer 三符号在此补弱回退体（必须等声明建成，
         * 见 irgen_weak.c emit_defer_fallback 注释） */
        emit_defer_fallback(g);
        LLVMBuildBr(g->builder, ret_bb);
        LLVMPositionBuilderAtEnd(g->builder, fb);
        zan_call2(g->builder, LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), &i8ptr, 1, 0),
            g->rt_release, &obj, 1, "");
        LLVMBuildBr(g->builder, ret_bb);
        LLVMPositionBuilderAtEnd(g->builder, ret_bb);
        LLVMBuildRetVoid(g->builder);
    }

    LLVMTypeRef alloc_args[] = { i64, i64, i8ptr };
    LLVMTypeRef alloc_type = LLVMFunctionType(i8ptr, alloc_args, 3, 0);
    g->rt_alloc = LLVMAddFunction(g->mod, "zan_rt_alloc", alloc_type);

    {
        LLVMBasicBlockRef bb = LLVMAppendBasicBlockInContext(g->ctx, g->rt_alloc, "entry");
        LLVMPositionBuilderAtEnd(g->builder, bb);
        LLVMValueRef size = LLVMGetParam(g->rt_alloc, 0);
        LLVMValueRef site = LLVMGetParam(g->rt_alloc, 1);
        LLVMValueRef name = LLVMGetParam(g->rt_alloc, 2);
        LLVMValueRef total = zan_add(g->builder, size, LLVMConstInt(i64, ZAN_OBJ_HDR_SIZE, 0), "total");
        LLVMTypeRef malloc_fn_type = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i64 }, 1, 0);
        LLVMValueRef raw = zan_call2(g->builder, malloc_fn_type, g->fn_malloc, &total, 1, "raw");
        zan_irgen_emit_oom_check(g, g->rt_alloc, raw);
        emit_rt_alloc_add(g, total);
        /* refcount = 1 at raw[0..7] */
        LLVMValueRef rc_ptr = LLVMBuildBitCast(g->builder, raw, LLVMPointerType(i64, 0), "rcptr");
        LLVMBuildStore(g->builder, LLVMConstInt(i64, 1, 0), rc_ptr);
        LLVMValueRef eight = LLVMConstInt(i64, (uint64_t)(-ZAN_OBJ_SITE_OFF), 0);
        LLVMValueRef site_ptr = LLVMBuildGEP2(g->builder, LLVMInt8TypeInContext(g->ctx), raw, &eight, 1, "sptr");
        LLVMValueRef site_iptr = LLVMBuildBitCast(g->builder, site_ptr, LLVMPointerType(i64, 0), "siptr");
        LLVMBuildStore(g->builder, site, site_iptr);
        LLVMValueRef hdr_off = LLVMConstInt(i64, ZAN_OBJ_HDR_SIZE, 0);
        LLVMValueRef user_ptr = LLVMBuildGEP2(g->builder, LLVMInt8TypeInContext(g->ctx), raw, &hdr_off, 1, "usr");
        if (g->rt_live_register) {
            zan_call2(g->builder, LLVMGlobalGetValueType(g->rt_live_register),
                g->rt_live_register, (LLVMValueRef[]){ user_ptr, total }, 2, "");
        }
        if (g->check_leaks) {
            emit_leak_counter_add(g, g->g_live, 1);
            LLVMValueRef ltbl = LLVMBuildLoad2(g->builder,
                LLVMPointerType(i64, 0), g->g_site_live, "ltbl");
            LLVMValueRef sc_ptr = LLVMBuildGEP2(g->builder, i64, ltbl, &site, 1, "scptr");
            emit_leak_counter_add(g, sc_ptr, 1);
            LLVMValueRef ntbl = LLVMBuildLoad2(g->builder,
                LLVMPointerType(i8ptr, 0), g->g_site_names, "ntbl");
            LLVMValueRef nm_ptr = LLVMBuildGEP2(g->builder, i8ptr, ntbl, &site, 1, "nmptr");
            LLVMBuildStore(g->builder, name, nm_ptr);
            emit_arc_trace_call(g, arc_trace_ev, "A", user_ptr,
                LLVMConstInt(i64, 1, 0), site);
        }
        LLVMBuildRet(g->builder, user_ptr);
    }

    {
        LLVMTypeRef str_alloc_args[] = { i64 };
        LLVMTypeRef str_alloc_type = LLVMFunctionType(i8ptr, str_alloc_args, 1, 0);
        g->rt_str_alloc = LLVMAddFunction(g->mod, "zan_rt_str_alloc", str_alloc_type);
        LLVMBasicBlockRef bb = LLVMAppendBasicBlockInContext(g->ctx, g->rt_str_alloc, "entry");
        LLVMPositionBuilderAtEnd(g->builder, bb);
        LLVMValueRef size = LLVMGetParam(g->rt_str_alloc, 0);
        LLVMValueRef total = zan_add(g->builder, size, LLVMConstInt(i64, ZAN_OBJ_HDR_SIZE, 0), "total");
        LLVMTypeRef malloc_fn_type = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i64 }, 1, 0);
        LLVMValueRef raw = zan_call2(g->builder, malloc_fn_type, g->fn_malloc, &total, 1, "raw");
        zan_irgen_emit_oom_check(g, g->rt_str_alloc, raw);
        emit_rt_alloc_add(g, total);
        LLVMValueRef rc_ptr = LLVMBuildBitCast(g->builder, raw, LLVMPointerType(i64, 0), "rcptr");
        LLVMBuildStore(g->builder, LLVMConstInt(i64, 1, 0), rc_ptr);
        LLVMValueRef eight = LLVMConstInt(i64, (uint64_t)(-ZAN_OBJ_SITE_OFF), 0);
        LLVMValueRef magic_ptr = LLVMBuildGEP2(g->builder, LLVMInt8TypeInContext(g->ctx), raw, &eight, 1, "magicp");
        LLVMValueRef magic_iptr = LLVMBuildBitCast(g->builder, magic_ptr, LLVMPointerType(i64, 0), "magicip");
        LLVMBuildStore(g->builder,
                       LLVMConstInt(i64, ZAN_STR_HDR_WORD(ZAN_STR_LEN_UNKNOWN), 0),
                       magic_iptr);
        LLVMValueRef hdr_off = LLVMConstInt(i64, ZAN_OBJ_HDR_SIZE, 0);
        LLVMValueRef user_ptr = LLVMBuildGEP2(g->builder, LLVMInt8TypeInContext(g->ctx), raw, &hdr_off, 1, "usr");
        if (g->rt_live_register) {
            zan_call2(g->builder, LLVMGlobalGetValueType(g->rt_live_register),
                g->rt_live_register, (LLVMValueRef[]){ user_ptr, total }, 2, "");
        }
        if (g->check_leaks) {
            emit_leak_counter_add(g, g->g_live, 1);
        }
        LLVMBuildRet(g->builder, user_ptr);
    }

    for (int rel = 0; rel < 2; rel++) {
        LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
        LLVMTypeRef i8t = LLVMInt8TypeInContext(g->ctx);
        LLVMTypeRef i8p = LLVMPointerType(i8t, 0);
        LLVMTypeRef fnty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx),
                                            (LLVMTypeRef[]){ i8p }, 1, 0);
        const char *fn_name = rel ? "zan_rt_arr_release" : "zan_rt_arr_retain";
        LLVMValueRef fn = LLVMGetNamedFunction(g->mod, fn_name);
        if (!fn)
            fn = LLVMAddFunction(g->mod, fn_name, fnty);
        if (rel) g->rt_arr_release = fn; else g->rt_arr_retain = fn;
        LLVMBasicBlockRef bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
        LLVMPositionBuilderAtEnd(g->builder, bb);
        LLVMValueRef obj = LLVMGetParam(fn, 0);
        LLVMBasicBlockRef ret_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "ret");
        LLVMBasicBlockRef cont_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "cont");
        LLVMBuildCondBr(g->builder,
            zan_icmp(g->builder, LLVMIntEQ, obj, LLVMConstNull(i8p), "isnull"),
            ret_bb, cont_bb);
        LLVMPositionBuilderAtEnd(g->builder, cont_bb);
        LLVMValueRef gmoff = LLVMConstInt(i64t, (uint64_t)ZAN_ARR_RC_MAGIC_OFF, 1);
        LLVMValueRef magic_ptr = LLVMBuildGEP2(g->builder, i8t, obj, &gmoff, 1, "magicp");
        emit_header_read_guard(g, fn, magic_ptr, ret_bb);
        LLVMValueRef magic = LLVMBuildLoad2(g->builder, i64t,
            LLVMBuildBitCast(g->builder, magic_ptr, LLVMPointerType(i64t, 0), "magicip"),
            "magic");
        LLVMValueRef has_magic = zan_icmp(g->builder, LLVMIntEQ, magic,
            LLVMConstInt(i64t, ZAN_ARRAY_RC_MAGIC, 0), "hasmagic");
        LLVMBasicBlockRef work_bb = LLVMAppendBasicBlockInContext(g->ctx, fn,
            rel ? "release" : "retain");
        LLVMBuildCondBr(g->builder, has_magic, work_bb, ret_bb);
        LLVMPositionBuilderAtEnd(g->builder, work_bb);
        LLVMValueRef rcoff = LLVMConstInt(i64t, (uint64_t)ZAN_ARR_RC_OFF, 1);
        LLVMValueRef rc_ptr = LLVMBuildGEP2(g->builder, i8t, obj, &rcoff, 1, "rcptr");
        LLVMValueRef rc_iptr = LLVMBuildBitCast(g->builder, rc_ptr,
            LLVMPointerType(i64t, 0), "rciptr");
        LLVMValueRef rc = LLVMBuildLoad2(g->builder, i64t, rc_iptr, "rc");
        emit_arc_freed_use_check(g, fn, rc, obj, ZAN_ARC_FAULT_USE_ARR,
            rel ? "release through a stale reference (array was already freed)"
                : "retain through a stale reference (array was freed: missing "
                  "retain when stored)");
        if (!rel) {
            LLVMValueRef rc_pre = LLVMBuildAtomicRMW(g->builder, LLVMAtomicRMWBinOpAdd,
                rc_iptr, LLVMConstInt(i64t, 1, 0), LLVMAtomicOrderingMonotonic, 0);
            emit_arc_underflow_check(g, fn, rc_pre, obj, NULL,
                ZAN_ARC_FAULT_ARR_RETAIN, "retain of an already-freed array");
            LLVMBuildBr(g->builder, ret_bb);
        } else {
            LLVMValueRef rc_old = LLVMBuildAtomicRMW(g->builder, LLVMAtomicRMWBinOpSub,
                rc_iptr, LLVMConstInt(i64t, 1, 0), LLVMAtomicOrderingAcquireRelease, 0);
            emit_arc_underflow_check(g, fn, rc_old, obj, NULL,
                ZAN_ARC_FAULT_ARR_RELEASE, "release of an already-freed array");
            LLVMValueRef rc1 = zan_sub(g->builder, rc_old, LLVMConstInt(i64t, 1, 0), "rc1");
            LLVMBasicBlockRef free_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "dofree");
            LLVMBuildCondBr(g->builder,
                zan_icmp(g->builder, LLVMIntEQ, rc1, LLVMConstInt(i64t, 0, 0), "iszero"),
                free_bb, ret_bb);
            LLVMPositionBuilderAtEnd(g->builder, free_bb);
            if (g->rt_live_unregister) {
                zan_call2(g->builder, LLVMGlobalGetValueType(g->rt_live_unregister),
                    g->rt_live_unregister, &obj, 1, "");
            }
            if (g->arc_guard) {
                emit_arc_quarantine(g, obj, rc_iptr);
            } else {
                LLVMValueRef raw = LLVMBuildGEP2(g->builder, i8t, obj, &rcoff, 1, "raw");
                zan_call2(g->builder,
                    LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), &i8p, 1, 0),
                    g->fn_free, &raw, 1, "");
            }
            LLVMBuildBr(g->builder, ret_bb);
        }
        LLVMPositionBuilderAtEnd(g->builder, ret_bb);
        LLVMBuildRetVoid(g->builder);
    }

    {
        LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
        LLVMTypeRef i8p = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
        LLVMTypeRef retain_type = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), (LLVMTypeRef[]){ i8p }, 1, 0);
        g->rt_str_retain = LLVMAddFunction(g->mod, "zan_rt_str_retain", retain_type);
        LLVMBasicBlockRef bb = LLVMAppendBasicBlockInContext(g->ctx, g->rt_str_retain, "entry");
        LLVMPositionBuilderAtEnd(g->builder, bb);
        LLVMValueRef obj = LLVMGetParam(g->rt_str_retain, 0);
        LLVMValueRef is_null = zan_icmp(g->builder, LLVMIntEQ, obj, LLVMConstNull(i8p), "isnull");
        LLVMBasicBlockRef ret_bb = LLVMAppendBasicBlockInContext(g->ctx, g->rt_str_retain, "ret");
        LLVMBasicBlockRef cont_bb = LLVMAppendBasicBlockInContext(g->ctx, g->rt_str_retain, "cont");
        LLVMBuildCondBr(g->builder, is_null, ret_bb, cont_bb);
        LLVMPositionBuilderAtEnd(g->builder, cont_bb);
        LLVMValueRef neg8 = LLVMConstInt(i64t, (uint64_t)ZAN_OBJ_SITE_OFF, 1);
        LLVMValueRef magic_ptr = LLVMBuildGEP2(g->builder, LLVMInt8TypeInContext(g->ctx), obj, &neg8, 1, "magicp");
        emit_header_read_guard(g, g->rt_str_retain, magic_ptr, ret_bb);
        LLVMValueRef magic_iptr = LLVMBuildBitCast(g->builder, magic_ptr, LLVMPointerType(i64t, 0), "magicip");
        LLVMValueRef magic = LLVMBuildLoad2(g->builder, i64t, magic_iptr, "magic");
        LLVMValueRef has_magic = zan_hdr_is_string(g, magic, "hasmagic");
        LLVMBasicBlockRef retain_bb = LLVMAppendBasicBlockInContext(g->ctx, g->rt_str_retain, "retain");
        LLVMBasicBlockRef arr_bb = LLVMAppendBasicBlockInContext(g->ctx, g->rt_str_retain, "arrfwd");
        LLVMBuildCondBr(g->builder, has_magic, retain_bb, arr_bb);
        LLVMPositionBuilderAtEnd(g->builder, arr_bb);
        zan_call2(g->builder, retain_type, g->rt_arr_retain, &obj, 1, "");
        LLVMBuildBr(g->builder, ret_bb);
        LLVMPositionBuilderAtEnd(g->builder, retain_bb);
        LLVMValueRef neg16 = LLVMConstInt(i64t, (uint64_t)ZAN_OBJ_RC_OFF, 1);
        LLVMValueRef rc_ptr = LLVMBuildGEP2(g->builder, LLVMInt8TypeInContext(g->ctx), obj, &neg16, 1, "rcptr");
        LLVMValueRef rc_iptr = LLVMBuildBitCast(g->builder, rc_ptr, LLVMPointerType(i64t, 0), "rciptr");
        LLVMValueRef rc = LLVMBuildLoad2(g->builder, i64t, rc_iptr, "rc");
        LLVMValueRef is_sent = zan_icmp(g->builder, LLVMIntEQ, rc,
            LLVMConstInt(i64t, ZAN_STRING_SENTINEL_RC, 0), "issent");
        LLVMBasicBlockRef add_bb = LLVMAppendBasicBlockInContext(g->ctx, g->rt_str_retain, "add");
        LLVMBuildCondBr(g->builder, is_sent, ret_bb, add_bb);
        LLVMPositionBuilderAtEnd(g->builder, add_bb);
        emit_arc_freed_use_check(g, g->rt_str_retain, rc, obj,
                                 ZAN_ARC_FAULT_USE_STR,
                                 "retain through a stale reference (string was "
                                 "freed: missing retain when stored)");
        LLVMValueRef rc_pre = LLVMBuildAtomicRMW(g->builder, LLVMAtomicRMWBinOpAdd,
            rc_iptr, LLVMConstInt(i64t, 1, 0), LLVMAtomicOrderingMonotonic, 0);
        emit_arc_underflow_check(g, g->rt_str_retain, rc_pre, obj, NULL,
                                 ZAN_ARC_FAULT_STR_RETAIN,
                                 "retain of an already-freed string");
        LLVMBuildBr(g->builder, ret_bb);
        LLVMPositionBuilderAtEnd(g->builder, ret_bb);
        LLVMBuildRetVoid(g->builder);
    }

    {
        LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
        LLVMTypeRef i8p = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
        LLVMTypeRef release_type = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), (LLVMTypeRef[]){ i8p }, 1, 0);
        g->rt_str_release = LLVMAddFunction(g->mod, "zan_rt_str_release", release_type);
        LLVMBasicBlockRef bb = LLVMAppendBasicBlockInContext(g->ctx, g->rt_str_release, "entry");
        LLVMPositionBuilderAtEnd(g->builder, bb);
        LLVMValueRef obj = LLVMGetParam(g->rt_str_release, 0);
        LLVMValueRef is_null = zan_icmp(g->builder, LLVMIntEQ, obj, LLVMConstNull(i8p), "isnull");
        LLVMBasicBlockRef ret_bb = LLVMAppendBasicBlockInContext(g->ctx, g->rt_str_release, "ret");
        LLVMBasicBlockRef cont_bb = LLVMAppendBasicBlockInContext(g->ctx, g->rt_str_release, "cont");
        LLVMBuildCondBr(g->builder, is_null, ret_bb, cont_bb);
        LLVMPositionBuilderAtEnd(g->builder, cont_bb);
        LLVMValueRef neg8 = LLVMConstInt(i64t, (uint64_t)ZAN_OBJ_SITE_OFF, 1);
        LLVMValueRef magic_ptr = LLVMBuildGEP2(g->builder, LLVMInt8TypeInContext(g->ctx), obj, &neg8, 1, "magicp");
        emit_header_read_guard(g, g->rt_str_release, magic_ptr, ret_bb);
        LLVMValueRef magic_iptr = LLVMBuildBitCast(g->builder, magic_ptr, LLVMPointerType(i64t, 0), "magicip");
        LLVMValueRef magic = LLVMBuildLoad2(g->builder, i64t, magic_iptr, "magic");
        LLVMValueRef has_magic = zan_hdr_is_string(g, magic, "hasmagic");
        LLVMBasicBlockRef rel_bb = LLVMAppendBasicBlockInContext(g->ctx, g->rt_str_release, "release");
        LLVMBasicBlockRef arr_bb = LLVMAppendBasicBlockInContext(g->ctx, g->rt_str_release, "arrfwd");
        LLVMBuildCondBr(g->builder, has_magic, rel_bb, arr_bb);
        LLVMPositionBuilderAtEnd(g->builder, arr_bb);
        zan_call2(g->builder, release_type, g->rt_arr_release, &obj, 1, "");
        LLVMBuildBr(g->builder, ret_bb);
        LLVMPositionBuilderAtEnd(g->builder, rel_bb);
        LLVMValueRef neg16 = LLVMConstInt(i64t, (uint64_t)ZAN_OBJ_RC_OFF, 1);
        LLVMValueRef rc_ptr = LLVMBuildGEP2(g->builder, LLVMInt8TypeInContext(g->ctx), obj, &neg16, 1, "rcptr");
        LLVMValueRef rc_iptr = LLVMBuildBitCast(g->builder, rc_ptr, LLVMPointerType(i64t, 0), "rciptr");
        LLVMValueRef rc = LLVMBuildLoad2(g->builder, i64t, rc_iptr, "rc");
        LLVMValueRef is_sent = zan_icmp(g->builder, LLVMIntEQ, rc,
            LLVMConstInt(i64t, ZAN_STRING_SENTINEL_RC, 0), "issent");
        LLVMBasicBlockRef dec_bb = LLVMAppendBasicBlockInContext(g->ctx, g->rt_str_release, "dec");
        LLVMBuildCondBr(g->builder, is_sent, ret_bb, dec_bb);
        LLVMPositionBuilderAtEnd(g->builder, dec_bb);
        emit_arc_freed_use_check(g, g->rt_str_release, rc, obj,
                                 ZAN_ARC_FAULT_USE_STR,
                                 "release through a stale reference "
                                 "(string was already freed)");
        LLVMValueRef rc_old = LLVMBuildAtomicRMW(g->builder, LLVMAtomicRMWBinOpSub, rc_iptr,
            LLVMConstInt(i64t, 1, 0), LLVMAtomicOrderingAcquireRelease, 0);
        emit_arc_underflow_check(g, g->rt_str_release, rc_old, obj, NULL,
                                 ZAN_ARC_FAULT_STR_RELEASE,
                                 "release of an already-freed string");
        LLVMValueRef rc1 = zan_sub(g->builder, rc_old, LLVMConstInt(i64t, 1, 0), "rc1");
        LLVMValueRef is_zero = zan_icmp(g->builder, LLVMIntEQ, rc1, LLVMConstInt(i64t, 0, 0), "iszero");
        LLVMBasicBlockRef free_bb = LLVMAppendBasicBlockInContext(g->ctx, g->rt_str_release, "dofree");
        LLVMBuildCondBr(g->builder, is_zero, free_bb, ret_bb);
        LLVMPositionBuilderAtEnd(g->builder, free_bb);
        if (g->rt_live_unregister) {
            zan_call2(g->builder, LLVMGlobalGetValueType(g->rt_live_unregister),
                g->rt_live_unregister, &obj, 1, "");
        }
        LLVMValueRef header_ptr = LLVMBuildGEP2(g->builder, LLVMInt8TypeInContext(g->ctx), obj, &neg16, 1, "hdr");
        LLVMTypeRef free_fn_type = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx),
            (LLVMTypeRef[]){ i8p }, 1, 0);
        if (g->arc_guard) emit_arc_quarantine(g, obj, rc_iptr);
        else zan_call2(g->builder, free_fn_type, g->fn_free, &header_ptr, 1, "");
        {
            emit_leak_counter_add(g, g->g_live, -1);
        }
        LLVMBuildBr(g->builder, ret_bb);
        LLVMPositionBuilderAtEnd(g->builder, ret_bb);
        LLVMBuildRetVoid(g->builder);
    }

