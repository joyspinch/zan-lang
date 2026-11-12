
#define ZAN_RT_FAULT_MESSAGE 0xE0A2C010u
void emit_fatal_report(zan_irgen_t *g, LLVMValueRef text, int exit_code) {
    LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i8p = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMValueRef fmt = zan_irgen_intern_string(g, "%s");
    LLVMValueRef pargs[] = { fmt, text };
    zan_call2(g->builder, g->printf_type, g->fn_printf, pargs, 2, "");

    if (g->target_is_windows) {
        LLVMTypeRef fl_ty = LLVMFunctionType(i32t, &i8p, 1, 0);
        LLVMValueRef fl = LLVMGetNamedFunction(g->mod, "fflush");
        if (!fl) fl = LLVMAddFunction(g->mod, "fflush", fl_ty);
        LLVMValueRef nullp = LLVMConstPointerNull(i8p);
        zan_call2(g->builder, fl_ty, fl, &nullp, 1, "");

        LLVMTypeRef re_args[] = { i32t, i32t, i32t, LLVMPointerType(i64t, 0) };
        LLVMTypeRef re_ty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx),
                                             re_args, 4, 0);
        LLVMValueRef re = LLVMGetNamedFunction(g->mod, "RaiseException");
        if (!re) re = LLVMAddFunction(g->mod, "RaiseException", re_ty);
        LLVMValueRef slots = LLVMBuildArrayAlloca(g->builder, i64t,
            LLVMConstInt(i32t, 2, 0), "fatalargs");
        LLVMValueRef vals[2] = {
            LLVMBuildPtrToInt(g->builder, text, i64t, "fatalmsg"),
            LLVMConstInt(i64t, (unsigned long long)exit_code, 0)
        };
        for (int i = 0; i < 2; i++) {
            LLVMValueRef idx = LLVMConstInt(i32t, (unsigned long long)i, 0);
            LLVMValueRef slot = LLVMBuildGEP2(g->builder, i64t, slots, &idx, 1,
                                              "fatalslot");
            LLVMBuildStore(g->builder, vals[i], slot);
        }
        LLVMValueRef cargs[] = { LLVMConstInt(i32t, ZAN_RT_FAULT_MESSAGE, 0),
                                 LLVMConstInt(i32t, 0, 0),
                                 LLVMConstInt(i32t, 2, 0), slots };
        zan_call2(g->builder, re_ty, re, cargs, 4, "");
    }
    LLVMValueRef code = LLVMConstInt(i32t, (unsigned long long)exit_code, 0);
    zan_call2(g->builder, g->exit_type, g->fn_exit, &code, 1, "");
    LLVMBuildUnreachable(g->builder);
}


static void emit_guard_hard(zan_irgen_t *g, LLVMValueRef text, int exit_code) {
    LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i8p = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMValueRef fmt = zan_irgen_intern_string(g, "%s");
    LLVMValueRef pargs[] = { fmt, text };
    zan_call2(g->builder, g->printf_type, g->fn_printf, pargs, 2, "");

    if (g->target_is_windows) {
        LLVMTypeRef fl_ty = LLVMFunctionType(i32t, &i8p, 1, 0);
        LLVMValueRef fl = LLVMGetNamedFunction(g->mod, "fflush");
        if (!fl) fl = LLVMAddFunction(g->mod, "fflush", fl_ty);
        LLVMValueRef nullp = LLVMConstPointerNull(i8p);
        zan_call2(g->builder, fl_ty, fl, &nullp, 1, "");

        LLVMTypeRef re_args[] = { i32t, i32t, i32t, LLVMPointerType(i64t, 0) };
        LLVMTypeRef re_ty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx),
                                             re_args, 4, 0);
        LLVMValueRef re = LLVMGetNamedFunction(g->mod, "RaiseException");
        if (!re) re = LLVMAddFunction(g->mod, "RaiseException", re_ty);
        LLVMValueRef slots = LLVMBuildArrayAlloca(g->builder, i64t,
            LLVMConstInt(i32t, 2, 0), "fatalargs");
        LLVMValueRef vals[2] = {
            LLVMBuildPtrToInt(g->builder, text, i64t, "fatalmsg"),
            LLVMConstInt(i64t, (unsigned long long)exit_code, 0)
        };
        for (int i = 0; i < 2; i++) {
            LLVMValueRef idx = LLVMConstInt(i32t, (unsigned long long)i, 0);
            LLVMValueRef slot = LLVMBuildGEP2(g->builder, i64t, slots, &idx, 1,
                                              "fatalslot");
            LLVMBuildStore(g->builder, vals[i], slot);
        }
        LLVMValueRef cargs[] = { LLVMConstInt(i32t, ZAN_RT_FAULT_MESSAGE, 0),
                                 LLVMConstInt(i32t, 0, 0),
                                 LLVMConstInt(i32t, 2, 0), slots };
        zan_call2(g->builder, re_ty, re, cargs, 4, "");
    }
    LLVMValueRef code = LLVMConstInt(i32t, (unsigned long long)exit_code, 0);
    zan_call2(g->builder, g->exit_type, g->fn_exit, &code, 1, "");
    LLVMBuildUnreachable(g->builder);
}
void emit_guard_report3(zan_irgen_t *g, LLVMValueRef file_gv,
                               unsigned line, unsigned col,
                               LLVMValueRef msg_gv);
void emit_guard_report_merged(zan_irgen_t *g, LLVMValueRef text);
void emit_guard_report3(zan_irgen_t *g, LLVMValueRef file_gv,
                               unsigned line, unsigned col,
                               LLVMValueRef msg_gv) {
    LLVMTypeRef i8p = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef fail_ty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx),
                                           (LLVMTypeRef[]){ i8p, i32t, i32t,
                                                            i8p }, 4, 0);
    LLVMValueRef fail_fn = LLVMGetNamedFunction(g->mod, "zan_rt_guard_fail3");
    if (!fail_fn) fail_fn = LLVMAddFunction(g->mod, "zan_rt_guard_fail3",
                                            fail_ty);
    LLVMValueRef fargs[] = {
        file_gv,
        LLVMConstInt(i32t, line, 0),
        LLVMConstInt(i32t, col, 0),
        msg_gv
    };
    zan_call2(g->builder, fail_ty, fail_fn, fargs, 4, "");
}
void emit_guard_report_merged(zan_irgen_t *g, LLVMValueRef text) {
    LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i8p = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);

    LLVMTypeRef ishard_ty = LLVMFunctionType(i32t, NULL, 0, 0);
    LLVMValueRef ishard_fn = LLVMGetNamedFunction(g->mod, "zan_rt_soft_is_hard");
    if (!ishard_fn) ishard_fn = LLVMAddFunction(g->mod, "zan_rt_soft_is_hard",
                                                ishard_ty);
    LLVMValueRef hard = zan_call2(g->builder, ishard_ty, ishard_fn, NULL, 0,
                                  "rt.hard");

    LLVMTypeRef note_ty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx),
                                           (LLVMTypeRef[]){ i8p }, 1, 0);
    LLVMValueRef note_fn = LLVMGetNamedFunction(g->mod, "zan_rt_soft_note");
    if (!note_fn) note_fn = LLVMAddFunction(g->mod, "zan_rt_soft_note", note_ty);

    LLVMBasicBlockRef hard_bb = LLVMAppendBasicBlockInContext(g->ctx,
        g->current_fn, "rt.hardpath");
    LLVMBasicBlockRef soft_cont_bb = LLVMAppendBasicBlockInContext(g->ctx,
        g->current_fn, "rt.softcont");
    LLVMBuildCondBr(g->builder,
                    zan_icmp(g->builder, LLVMIntNE, hard,
                             LLVMConstInt(i32t, 0, 0), "rt.hardcmp"),
                    hard_bb, soft_cont_bb);

    LLVMPositionBuilderAtEnd(g->builder, hard_bb);
    emit_guard_hard(g, text, 70);

    LLVMPositionBuilderAtEnd(g->builder, soft_cont_bb);
    zan_call2(g->builder, note_ty, note_fn, &text, 1, "");
}
void emit_io_abort_if(zan_irgen_t *g, LLVMValueRef cond, const char *msg) {
    if (!g->current_fn) return;
    LLVMBasicBlockRef fail_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "io.fail");
    LLVMBasicBlockRef cont_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "io.cont");
    LLVMBuildCondBr(g->builder, cond, fail_bb, cont_bb);
    LLVMPositionBuilderAtEnd(g->builder, fail_bb);
    LLVMValueRef text = zan_irgen_intern_string(g, msg);
    emit_fatal_report(g, text, 1);
    LLVMPositionBuilderAtEnd(g->builder, cont_bb);
}
void emit_fopen_check(zan_irgen_t *g, LLVMValueRef fp, const char *msg) {
    if (!g->current_fn) return;
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMValueRef isnull = zan_icmp(g->builder, LLVMIntEQ, fp,
        LLVMConstPointerNull(i8ptr), "fp.isnull");
    emit_io_abort_if(g, isnull, msg);
}

static LLVMValueRef emit_expect_false(zan_irgen_t *g, LLVMValueRef cond) {
    if (!cond) return cond;
    if (LLVMGetTypeKind(LLVMTypeOf(cond)) != LLVMIntegerTypeKind) return cond;
    if (LLVMGetIntTypeWidth(LLVMTypeOf(cond)) != 1) return cond;
    if (!g->expect_false_fn) {
        LLVMTypeRef i1 = LLVMInt1TypeInContext(g->ctx);
        LLVMTypeRef ty = LLVMFunctionType(i1, (LLVMTypeRef[]){ i1, i1 }, 2, 0);
        g->expect_false_fn = LLVMAddFunction(g->mod, "llvm.expect.i1", ty);
    }
    LLVMValueRef args[] = { cond, LLVMConstInt(LLVMInt1TypeInContext(g->ctx), 0, 0) };
    return LLVMBuildCall2(g->builder,
                          LLVMGlobalGetValueType(g->expect_false_fn),
                          g->expect_false_fn, args, 2, "guard.expect");
}

static zan_symbol_t *lookup_sys_class(zan_irgen_t *g, const char *short_name, const char *full_name) {
    if (!g->binder) return NULL;
    zan_symbol_t *sym = zan_binder_lookup(g->binder, (zan_istr_t){ (char *)short_name, (int)strlen(short_name) });
    if (!sym) {
        sym = zan_binder_lookup(g->binder, (zan_istr_t){ (char *)full_name, (int)strlen(full_name) });
    }
    return sym;
}

static zan_symbol_t *infer_guard_exception_sym(zan_irgen_t *g, const char *msg) {
    zan_symbol_t *sym = NULL;
    if (strstr(msg, "InvalidCast")) {
        /* B-ID163③：强转/拆箱失败抛 InvalidCastException。放分支链最前，
         * 防消息体其他子串（"instance"/"target" 等）被后面的分支误判。 */
        sym = lookup_sys_class(g, "InvalidCastException", "System.InvalidCastException");
    } else if (strstr(msg, "division by zero")) {
        sym = lookup_sys_class(g, "DivideByZeroException", "System.DivideByZeroException");
    } else if (strstr(msg, "not a valid")) {
        /* 解析失败（int.Parse / Convert.ToInt* 等的 "input is not a valid
         * ..."）。必须放在 "range" 分支之前：旧解析文案带 "(or out of
         * range)" 尾巴，曾被 "range" 子串整体误判成 IndexOutOfRange。 */
        sym = lookup_sys_class(g, "FormatException", "System.FormatException");
    } else if (strstr(msg, "outside the range")) {
        sym = lookup_sys_class(g, "OverflowException", "System.OverflowException");
    } else if (strstr(msg, "out of bounds") || strstr(msg, "index") || strstr(msg, "range")) {
        sym = lookup_sys_class(g, "IndexOutOfRangeException", "System.IndexOutOfRangeException");
    } else if (strstr(msg, "null reference")) {
        sym = lookup_sys_class(g, "NullReferenceException", "System.NullReferenceException");
    } else if (strstr(msg, "overflow")) {
        sym = lookup_sys_class(g, "OverflowException", "System.OverflowException");
    }
    if (!sym) {
        sym = lookup_sys_class(g, "Exception", "System.Exception");
    }
    return sym;
}

static void emit_throw_guard_exception(zan_irgen_t *g, zan_symbol_t *sym,
                                       const char *msg) {
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);

    /* Allocate 24-byte Exception object (vtable, Message, InnerException) */
    LLVMTypeRef alloc_ty = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i64, i64, i8ptr }, 3, 0);
    LLVMValueRef site_val = LLVMConstInt(i64, 0, 0);
    LLVMValueRef site_name = zan_irgen_intern_string(g, "runtime_exception");
    LLVMValueRef raw = zan_call2(g->builder, alloc_ty, g->rt_alloc,
        (LLVMValueRef[]){ LLVMConstInt(i64, 24, 0), site_val, site_name }, 3, "rt.exc");
    LLVMValueRef obj_arr = LLVMBuildBitCast(g->builder, raw, LLVMPointerType(i8ptr, 0), "rt.fields");

    /* Slot 0: vtable */
    LLVMValueRef vtg = get_vtable_global(g, sym);
    LLVMValueRef vtg_ptr = vtg ? LLVMBuildBitCast(g->builder, vtg, i8ptr, "vtg.ptr") : LLVMConstNull(i8ptr);
    LLVMValueRef f0 = LLVMBuildGEP2(g->builder, i8ptr, obj_arr, (LLVMValueRef[]){ LLVMConstInt(i64, 0, 0) }, 1, "f0");
    LLVMBuildStore(g->builder, vtg_ptr, f0);

    /* Slot 1: Message */
    LLVMValueRef msg_str = emit_string_literal_rc(g, (zan_istr_t){ (char *)msg, (int)strlen(msg) });
    LLVMValueRef f1 = LLVMBuildGEP2(g->builder, i8ptr, obj_arr, (LLVMValueRef[]){ LLVMConstInt(i64, 1, 0) }, 1, "f1");
    LLVMBuildStore(g->builder, msg_str, f1);

    /* Slot 2: InnerException = null */
    LLVMValueRef f2 = LLVMBuildGEP2(g->builder, i8ptr, obj_arr, (LLVMValueRef[]){ LLVMConstInt(i64, 2, 0) }, 1, "f2");
    LLVMBuildStore(g->builder, LLVMConstNull(i8ptr), f2);

    /* Store to EH globals */
    LLVMValueRef top_g, bufs_g, exc_g;
    get_eh_globals(g, &top_g, &bufs_g, &exc_g);
    LLVMValueRef owned_g = get_eh_exc_owned_global(g);
    LLVMValueRef tid_g = get_eh_exc_tid_global(g);

    zan_store_fit(g, raw, exc_g);
    zan_store_fit(g, LLVMConstInt(i32t, 1, 0), owned_g);
    LLVMValueRef tid = get_class_tid_global(g, sym);
    LLVMValueRef tid_ptr = LLVMBuildBitCast(g->builder, tid, i8ptr, "tid.ptr");
    zan_store_fit(g, tid_ptr, tid_g);

    /* Unwind finally blocks if any */
    if (g->finally_count > 0 && g->current_locals) {
        emit_finallys_left_by_throw(g, (local_scope_t *)g->current_locals);
        if (LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder))) return;
    }

    (void)top_g;
    if (g->current_async_frame) emit_raise_exception(g);
    else emit_raise_exception_through_cleanups(g);
}
void emit_runtime_check(zan_irgen_t *g, LLVMValueRef is_error,
                               zan_loc_t loc, const char *msg) {
    if (!g->runtime_checks || !g->current_fn) return;
    is_error = emit_expect_false(g, is_error);

    LLVMBasicBlockRef bad_bb  = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "rt.bad");
    LLVMBasicBlockRef cont_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "rt.cont");
    LLVMBuildCondBr(g->builder, is_error, bad_bb, cont_bb);
    LLVMPositionBuilderAtEnd(g->builder, bad_bb);

    zan_symbol_t *sym = infer_guard_exception_sym(g, msg);
    if (sym && !g->target_is_wasm) {
        LLVMValueRef top_g, bufs_g, exc_g;
        get_eh_globals(g, &top_g, &bufs_g, &exc_g);
        LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
        LLVMValueRef top_val = LLVMBuildLoad2(g->builder, i32t, top_g, "rt.top");
        LLVMValueRef in_try = zan_icmp(g->builder, LLVMIntSGE, top_val,
            LLVMConstInt(i32t, 0, 0), "rt.intry");

        LLVMBasicBlockRef throw_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "rt.throw");
        LLVMBasicBlockRef nothrow_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "rt.nothrow");
        LLVMBuildCondBr(g->builder, in_try, throw_bb, nothrow_bb);

        LLVMPositionBuilderAtEnd(g->builder, throw_bb);
        emit_throw_guard_exception(g, sym, msg);

        LLVMPositionBuilderAtEnd(g->builder, nothrow_bb);
    }

    if (!g->rt_guard_split) {
        char buf[640];
        const char *file0 = loc_site_file(g, loc);
        if (g->hide_diagnostics) {
            snprintf(buf, sizeof(buf), "%s: runtime error: %s\n",
                     file0, msg);
        } else {
            snprintf(buf, sizeof(buf), "%s:%u:%u: runtime error: %s\n",
                     file0, loc.line, loc.col, msg);
        }
        LLVMValueRef text0 = zan_irgen_intern_string(g, buf);
        emit_guard_report_merged(g, text0);
        LLVMBuildBr(g->builder, cont_bb);
    } else {
        LLVMValueRef file_gv = zan_irgen_intern_string(g, loc_site_file(g, loc));
        char mbuf[320];
        snprintf(mbuf, sizeof(mbuf), "%s\n", msg);
        LLVMValueRef msg_gv = zan_irgen_intern_string(g, mbuf);
        uint32_t line_arg = g->hide_diagnostics ? 0 : loc.line;
        uint32_t col_arg = g->hide_diagnostics ? 0 : loc.col;
        emit_guard_report3(g, file_gv, line_arg, col_arg, msg_gv);
        LLVMBuildBr(g->builder, cont_bb);
    }

    LLVMPositionBuilderAtEnd(g->builder, cont_bb);
}

/* B-ID163②：擦除体（泛型类/泛型方法的未特化变体，类型实参未定，
 * irgen_call_spec.c 同款场景）里的 new T() 落到 irgen 兜底——旧实现直接
 * 返回零值，调用方拿 null 继续跑（TASKS B-ID163② 静默失效实录）。擦除体
 * 编译期造不出对象（实参未定；单态化路径不经过兜底，conformance
 * generic_new_constraint 不受影响），改为运行期响亮硬失败：擦除体真被
 * 调到这里即 exit 70 带明确文案。 */
void emit_erased_new_hard_fail(zan_irgen_t *g) {
    if (!g->current_fn) return;
    LLVMValueRef text = zan_irgen_intern_string(g,
        "new T() in a generic-erased body: the type argument is not available here");
    emit_guard_hard(g, text, 70);
}

/* B-ID163③：引用下转强转/拆箱失败一律抛异常（A509 软失败通道停用于
 * 强转族——旧实现 try 外 stderr 一次后拿 null 继续跑，正是静默默认值
 * 缺陷本体）。try 内可 catch（emit_raise_exception 的 lpad 通路）；
 * try 外未捕获由运行时按未处理异常终止进程。C# 同语义。wasm 无 EH
 * 通道，兜底硬失败 exit 70。调用方发射后续块前需检查/补 terminator。 */
void emit_cast_throw(zan_irgen_t *g, zan_loc_t loc, const char *msg) {
    (void)loc;
    if (!g->current_fn) return;
    zan_symbol_t *sym = infer_guard_exception_sym(g, msg);
    if (sym && !g->target_is_wasm) {
        emit_throw_guard_exception(g, sym, msg);
        return;
    }
    LLVMValueRef text = zan_irgen_intern_string(g, msg);
    emit_guard_hard(g, text, 70);
}

/* B-ID180：浮点→整型全通道 poison 收口（cast / 三元统一 / Convert 族，
 * 自举孪生三处同口径）。LLVM fptosi 对溢出值产生 poison：x86 得 0x8000…、
 * ARM64 饱和、WASM trap——三平台三种结果且优化器可任意利用，违反
 * SECURITY.md §1「无 UB」承诺（探针 _scratch/bid180_probe：越界值打印
 * 出 -0 垃圾、try 内毫无异常分发）。发射序：
 *   ① 范围谓词 [-2^63, 2^63)：NaN 两个比较全 false 自动落网；
 *   ② emit_runtime_check —— try 内抛 OverflowException（消息含
 *      "outside the range" 走既有映射），try 外 soft 报告后继续；
 *   ③ clamp 归一化（上越界→INT64_MAX、下越界→INT64_MIN、NaN→0），
 *      fptosi 输入必定义，绝无 poison；
 *   ④ 窄目标（<64 位）截断：域内值与旧窄直转逐位一致，域外旧 poison
 *      现为定义值。 */
LLVMValueRef emit_fptosi_guarded(zan_irgen_t *g, LLVMValueRef val,
                                 LLVMTypeRef target, zan_loc_t loc,
                                 LLVMValueRef arm_mask) {
    LLVMTypeRef dbl = LLVMDoubleTypeInContext(g->ctx);
    if (LLVMGetTypeKind(LLVMTypeOf(val)) == LLVMFloatTypeKind)
        val = LLVMBuildFPExt(g->builder, val, dbl, "f2i.ext");
    /* f64 可精确表示 ±2^63；上界开区间（fptosi(2^63) 即 poison）。 */
    LLVMValueRef lo = LLVMConstReal(dbl, -9223372036854775808.0);
    LLVMValueRef hi = LLVMConstReal(dbl, 9223372036854775808.0);
    LLVMValueRef ge_lo = LLVMBuildFCmp(g->builder, LLVMRealOGE, val, lo, "f2i.lo");
    LLVMValueRef lt_hi = LLVMBuildFCmp(g->builder, LLVMRealOLT, val, hi, "f2i.hi");
    LLVMValueRef in_range = LLVMBuildAnd(g->builder, ge_lo, lt_hi, "f2i.in");
    LLVMBasicBlockRef cur_bb = LLVMGetInsertBlock(g->builder);
    LLVMValueRef cur_fn = cur_bb ? LLVMGetBasicBlockParent(cur_bb) : NULL;
    if (g->runtime_checks && cur_fn && cur_fn == g->current_fn && loc.line > 0 && !arm_mask) {
        LLVMValueRef bad = LLVMBuildNot(g->builder, in_range, "f2i.bad");
        emit_runtime_check(g, bad, loc,
            "value is outside the range of the target type");
    }
    /* clamp：域内值正常转，域外/NaN 归一化（上越界→INT64_MAX、下越界→INT64_MIN、NaN→0） */
    LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
    LLVMValueRef safe_f = LLVMBuildSelect(g->builder, in_range, val,
        LLVMConstReal(dbl, 0.0), "f2i.safe_f");
    LLVMValueRef converted = LLVMBuildFPToSI(g->builder, safe_f, i64t, "f2i.val");
    LLVMValueRef ge_hi = LLVMBuildFCmp(g->builder, LLVMRealOGE, val, hi, "f2i.chi");
    LLVMValueRef lt_lo = LLVMBuildFCmp(g->builder, LLVMRealOLT, val, lo, "f2i.clo");
    LLVMValueRef clamped = LLVMBuildSelect(g->builder, ge_hi,
        LLVMConstInt(i64t, INT64_MAX, 1),
        LLVMBuildSelect(g->builder, lt_lo,
            LLVMConstInt(i64t, INT64_MIN, 1),
            converted, "f2i.clo"),
        "f2i.chi");
    if (LLVMGetIntTypeWidth(target) < 64)
        return LLVMBuildTrunc(g->builder, clamped, target, "f2i.trunc");
    return clamped;
}

static void emit_string_null_report(zan_irgen_t *g, LLVMValueRef payload,
                                    zan_loc_t loc) {
    if (!g->rt_guard_split) {
        char buf[160];
        const char *file0 = loc_site_file(g, loc);
        if (g->hide_diagnostics) {
            snprintf(buf, sizeof(buf),
                     "%s: runtime error: null reference where a string/byte "
                     "buffer is required (length probe)\n",
                     file0);
        } else {
            snprintf(buf, sizeof(buf),
                     "%s:%u:%u: runtime error: null reference where a string/byte "
                     "buffer is required (length probe)\n",
                     file0, loc.line, loc.col);
        }
        emit_guard_report_merged(g, zan_irgen_intern_string(g, buf));
        return;
    }
    const char *file = loc_site_file(g, loc);
    LLVMValueRef file_gv = zan_irgen_intern_string(g, file);
    LLVMValueRef msg_gv = zan_irgen_intern_string(g,
        "null reference where a string/byte buffer is required (length probe)\n");
    uint32_t line_arg = g->hide_diagnostics ? 0 : loc.line;
    uint32_t col_arg = g->hide_diagnostics ? 0 : loc.col;
    emit_guard_report3(g, file_gv, line_arg, col_arg, msg_gv);
}
LLVMValueRef emit_index_i64(zan_irgen_t *g, LLVMValueRef value,
                                   const char *name) {
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeKind kind = LLVMGetTypeKind(LLVMTypeOf(value));
    if (kind != LLVMIntegerTypeKind) return value;
    unsigned width = LLVMGetIntTypeWidth(LLVMTypeOf(value));
    if (width < 64) return LLVMBuildSExt(g->builder, value, i64, name);
    if (width > 64) return LLVMBuildTrunc(g->builder, value, i64, name);
    return value;
}
void emit_index_range_check(zan_irgen_t *g, LLVMValueRef index,
                                   LLVMValueRef length, bool allow_end,
                                   zan_loc_t loc, const char *kind) {
    if (!g->runtime_checks || !g->current_fn) return;
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    index = emit_index_i64(g, index, "idx.i64");
    length = emit_index_i64(g, length, "len.i64");
    LLVMValueRef negative = zan_icmp(g->builder, LLVMIntSLT, index,
                                     LLVMConstInt(i64, 0, 0), "idx.neg");
    LLVMIntPredicate upper_pred = allow_end ? LLVMIntSGT : LLVMIntSGE;
    LLVMValueRef outside = zan_icmp(g->builder, upper_pred, index, length,
                                    "idx.high");
    LLVMValueRef bad = LLVMBuildOr(g->builder, negative, outside, "idx.bad");
    char msg[96];
    snprintf(msg, sizeof(msg), "%s index out of bounds", kind ? kind : "array");
    emit_runtime_check(g, bad, loc, msg);
}
void emit_index_bounds_check(zan_irgen_t *g, LLVMValueRef index,
                                    LLVMValueRef length, zan_loc_t loc,
                                    const char *kind) {
    emit_index_range_check(g, index, length, false, loc, kind);
}

/* A507 越界写保护（TASKS.md）：与 emit_index_range_check 同一谓词，但把
 * "坏"标志返回给调用方——store 位点用它做 cond_br 跳过写入。曾
 * `a[9]=99` 诊断照发后仍钳位落进槽 0（静默覆盖 a[0]）。 */
LLVMValueRef emit_index_oob_pred(zan_irgen_t *g, LLVMValueRef index,
                                        LLVMValueRef length) {
    LLVMTypeRef i1 = LLVMInt1TypeInContext(g->ctx);
    if (!g->runtime_checks || !g->current_fn)
        return LLVMConstInt(i1, 0, 0);
    if (LLVMGetTypeKind(LLVMTypeOf(index)) != LLVMIntegerTypeKind ||
        LLVMGetTypeKind(LLVMTypeOf(length)) != LLVMIntegerTypeKind)
        return LLVMConstInt(i1, 0, 0);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    index = emit_index_i64(g, index, "oob.idx");
    length = emit_index_i64(g, length, "oob.len");
    LLVMValueRef neg = zan_icmp(g->builder, LLVMIntSLT, index,
                                LLVMConstInt(i64, 0, 0), "oob.neg");
    LLVMValueRef high = zan_icmp(g->builder, LLVMIntSGE, index, length,
                                 "oob.high");
    return LLVMBuildOr(g->builder, neg, high, "oob.bad");
}

/* 越界写位点协议：split 在诊断之后切控制流（oob → end，否则 → do 块），
 * 调用方把 store（连着配套的 retain/release）发射在 do 块里，finish 收口。
 * end_bb 为 NULL 表示谓词恒假/检查关闭，store 照旧直落。 */
void emit_oob_store_split(zan_irgen_t *g, LLVMValueRef oob,
                                 LLVMBasicBlockRef *do_bb,
                                 LLVMBasicBlockRef *end_bb) {
    *do_bb = NULL;
    *end_bb = NULL;
    if (!g->runtime_checks || !g->current_fn) return;
    if (LLVMIsConstant(oob)) return; /* 恒假：不切块 */
    LLVMValueRef fn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder));
    *do_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "sto.do");
    *end_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "sto.end");
    LLVMBuildCondBr(g->builder, oob, *end_bb, *do_bb);
    LLVMPositionBuilderAtEnd(g->builder, *do_bb);
}
void emit_oob_store_finish(zan_irgen_t *g, LLVMBasicBlockRef do_bb,
                                  LLVMBasicBlockRef end_bb) {
    if (!end_bb) return;
    (void)do_bb;
    LLVMBuildBr(g->builder, end_bb);
    LLVMPositionBuilderAtEnd(g->builder, end_bb);
}
LLVMValueRef emit_index_safe_check(zan_irgen_t *g, LLVMValueRef index,
                                          LLVMValueRef length, bool allow_end,
                                          zan_loc_t loc, const char *kind) {
    if (!g->runtime_checks || !g->current_fn) return index;
    emit_index_range_check(g, index, length, allow_end, loc, kind);
    if (LLVMGetTypeKind(LLVMTypeOf(index)) != LLVMIntegerTypeKind)
        return index;
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMValueRef idx = emit_index_i64(g, index, "safeidx.i64");
    LLVMValueRef len = emit_index_i64(g, length, "safeidx.len");
    LLVMValueRef negative = zan_icmp(g->builder, LLVMIntSLT, idx,
                                     LLVMConstInt(i64, 0, 0), "safeidx.neg");
    LLVMIntPredicate upper_pred = allow_end ? LLVMIntSGT : LLVMIntSGE;
    LLVMValueRef outside = zan_icmp(g->builder, upper_pred, idx, len,
                                    "safeidx.high");
    LLVMValueRef unsafe = LLVMBuildOr(g->builder, negative, outside,
                                      "safeidx.bad");
    return LLVMBuildSelect(g->builder, unsafe,
                           LLVMConstInt(LLVMTypeOf(index), 0, 0), index,
                           "safeidx.ok");
}
LLVMValueRef emit_index_safe_bounds(zan_irgen_t *g, LLVMValueRef index,
                                           LLVMValueRef length, zan_loc_t loc,
                                           const char *kind) {
    return emit_index_safe_check(g, index, length, false, loc, kind);
}
LLVMValueRef emit_string_base_guard(zan_irgen_t *g, LLVMValueRef payload,
                                           zan_loc_t loc) {
    if (!g->runtime_checks || !g->current_fn) return payload;
    if (LLVMGetTypeKind(LLVMTypeOf(payload)) != LLVMPointerTypeKind) return payload;
    LLVMValueRef fn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder));
    if (!fn) return payload;
    LLVMValueRef isnull = zan_icmp(g->builder, LLVMIntEQ, payload,
        LLVMConstNull(LLVMTypeOf(payload)), "strbase.null");
    LLVMBasicBlockRef fast_bb = LLVMGetInsertBlock(g->builder);
    LLVMBasicBlockRef slow_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "strbase.null");
    LLVMBasicBlockRef done_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "strbase.done");
    LLVMBuildCondBr(g->builder, isnull, slow_bb, done_bb);
    LLVMPositionBuilderAtEnd(g->builder, slow_bb);
    emit_runtime_check(g, isnull, loc,
        "null reference where a string/byte buffer is required (element access)");
    LLVMValueRef scratch = emit_soft_scratch_cached(g);
    scratch = LLVMBuildBitCast(g->builder, scratch, LLVMTypeOf(payload),
                               "soft.scratch.bc");
    LLVMBasicBlockRef slow_end = LLVMGetInsertBlock(g->builder);
    LLVMBuildBr(g->builder, done_bb);
    LLVMPositionBuilderAtEnd(g->builder, done_bb);
    LLVMValueRef phi = LLVMBuildPhi(g->builder, LLVMTypeOf(payload), "strbase.phi");
    LLVMValueRef vals[2] = { payload, scratch };
    LLVMBasicBlockRef bbs[2] = { fast_bb, slow_end };
    LLVMAddIncoming(phi, vals, bbs, 2);
    return phi;
}

static LLVMValueRef emit_soft_scratch_cached(zan_irgen_t *g) {
    LLVMBasicBlockRef cur = LLVMGetInsertBlock(g->builder);
    LLVMValueRef fn = LLVMGetBasicBlockParent(cur);
    LLVMTypeRef i8p = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    if (!g->soft_scratch_slot || g->soft_scratch_fn != fn) {
        g->soft_scratch_slot = emit_entry_alloca(g, i8p, "soft.scratch.slot");
        zan_builder_point_t pt = zan_builder_save(g);
        LLVMBasicBlockRef entry = LLVMGetEntryBasicBlock(fn);
        LLVMValueRef term = LLVMGetBasicBlockTerminator(entry);
        if (term) LLVMPositionBuilderBefore(g->builder, term);
        else LLVMPositionBuilderAtEnd(g->builder, entry);
        LLVMTypeRef fn_ty = LLVMFunctionType(i8p, NULL, 0, 0);
        LLVMValueRef sf = LLVMGetNamedFunction(g->mod, "zan_rt_soft_scratch");
        if (!sf) sf = LLVMAddFunction(g->mod, "zan_rt_soft_scratch", fn_ty);
        LLVMValueRef scratch = zan_call2(g->builder, fn_ty, sf, NULL, 0,
                                         "soft.scratch.init");
        LLVMBuildStore(g->builder, scratch, g->soft_scratch_slot);
        zan_builder_restore(g, pt);
        g->soft_scratch_fn = fn;
    }
    return LLVMBuildLoad2(g->builder, i8p, g->soft_scratch_slot, "soft.scratch");
}
LLVMValueRef emit_soft_base_select(zan_irgen_t *g, LLVMValueRef base,
                                          LLVMValueRef isnull, zan_loc_t loc);
LLVMValueRef emit_string_elem_guard(zan_irgen_t *g, LLVMValueRef payload,
                                           LLVMValueRef index, zan_loc_t loc,
                                           LLVMValueRef *arr_ptr_out) {
    if (!g->runtime_checks || !g->current_fn) return index;
    LLVMValueRef isnull = zan_icmp(g->builder, LLVMIntEQ, payload,
        LLVMConstNull(LLVMTypeOf(payload)), "stridx.null");
    emit_runtime_check(g, isnull, loc,
        "null reference where a string/byte buffer is required (element access)");
    payload = emit_soft_base_select(g, payload, isnull, loc);
    if (arr_ptr_out) *arr_ptr_out = payload;
    LLVMValueRef unsafe = isnull;
    if (LLVMGetTypeKind(LLVMTypeOf(index)) != LLVMIntegerTypeKind) return index;
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMValueRef idx = emit_index_i64(g, index, "stridx.i64");
    LLVMValueRef neg = zan_icmp(g->builder, LLVMIntSLT, idx,
                                LLVMConstInt(i64, 0, 0), "stridx.neg");
    emit_runtime_check(g, neg, loc, "string index out of bounds");
    unsafe = LLVMBuildOr(g->builder, unsafe, neg, "stridx.unsafe");
    return LLVMBuildSelect(g->builder, unsafe,
                           LLVMConstInt(LLVMTypeOf(idx), 0, 0), idx,
                           "stridx.safe");
}
LLVMValueRef emit_soft_base_select(zan_irgen_t *g, LLVMValueRef base,
                                          LLVMValueRef isnull, zan_loc_t loc) {
    if (!g->runtime_checks || !g->current_fn) return base;
    if (LLVMGetTypeKind(LLVMTypeOf(base)) != LLVMPointerTypeKind) return base;
    LLVMValueRef scratch = emit_soft_scratch_cached(g);
    scratch = LLVMBuildBitCast(g->builder, scratch, LLVMTypeOf(base),
                               "soft.scratch.bc");
    return LLVMBuildSelect(g->builder, isnull, scratch, base, "soft.base");
}

static LLVMValueRef emit_string_len_ex(zan_irgen_t *g, LLVMValueRef payload,
                                      int array_count, zan_loc_t loc) {
    LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
    LLVMTypeRef i8p = LLVMPointerType(i8, 0);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i64p = LLVMPointerType(i64, 0);
    LLVMTypeRef strlen_ty = LLVMFunctionType(i64, (LLVMTypeRef[]){ i8p }, 1, 0);
    if (LLVMGetTypeKind(LLVMTypeOf(payload)) != LLVMPointerTypeKind)
        return zan_call2(g->builder, strlen_ty, g->fn_strlen, &payload, 1,
                         "strbuf.strlen");

    LLVMBasicBlockRef entry_bb = LLVMGetInsertBlock(g->builder);
    LLVMValueRef fn = LLVMGetBasicBlockParent(entry_bb);
    LLVMBasicBlockRef probe_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "strbuf.probe");
    LLVMBasicBlockRef arr_bb = array_count
        ? LLVMAppendBasicBlockInContext(g->ctx, fn, "strbuf.arr") : NULL;
    LLVMBasicBlockRef slow_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "strbuf.slow");
    LLVMBasicBlockRef stamp_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "strbuf.stamp");
    LLVMBasicBlockRef stamp_do_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "strbuf.stampdo");
    LLVMBasicBlockRef slow_end_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "strbuf.slowend");
    LLVMBasicBlockRef raw_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "strbuf.raw");
    LLVMBasicBlockRef done_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "strbuf.done");

    LLVMValueRef hdr_ptr = LLVMBuildGEP2(g->builder, i8, payload,
        &(LLVMValueRef){ LLVMConstInt(i64, (uint64_t)ZAN_OBJ_SITE_OFF, 1) }, 1,
        "strbuf.hdrp");
    LLVMValueRef read_ok = zan_hdr_read_ok(g, payload);
    LLVMValueRef probe_ok = zan_icmp(g->builder, LLVMIntNE, payload,
        LLVMConstNull(LLVMTypeOf(payload)), "strbuf.nonnull");
    if (read_ok) probe_ok = zan_and(g->builder, probe_ok, read_ok, "strbuf.ok");
    LLVMBuildCondBr(g->builder, probe_ok, probe_bb, raw_bb);

    LLVMPositionBuilderAtEnd(g->builder, probe_bb);
    LLVMValueRef hdr_iptr = LLVMBuildBitCast(g->builder, hdr_ptr, i64p,
                                             "strbuf.hdrip");
    LLVMValueRef word = LLVMBuildLoad2(g->builder, i64, hdr_iptr, "strbuf.hdr");
    LLVMValueRef is_str = zan_hdr_is_string(g, word, "strbuf.isstr");
    LLVMValueRef cached = LLVMBuildAnd(g->builder, word,
        LLVMConstInt(i64, ZAN_STR_LEN_MASK, 0), "strbuf.cached");
    LLVMValueRef measured = zan_icmp(g->builder, LLVMIntNE, cached,
        LLVMConstInt(i64, ZAN_STR_LEN_UNKNOWN, 0), "strbuf.measured");
    LLVMValueRef known = zan_and(g->builder, is_str, measured, "strbuf.known");
    LLVMBuildCondBr(g->builder, known, done_bb,
                    arr_bb ? arr_bb : slow_bb);

    LLVMValueRef count = NULL;
    LLVMValueRef phi_raw = NULL;
    LLVMBasicBlockRef phi_raw_pred = NULL;
    if (arr_bb) {
        LLVMBasicBlockRef arr_test_bb = arr_bb;
        arr_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "strbuf.arrcount");
        LLVMPositionBuilderAtEnd(g->builder, arr_test_bb);
        LLVMValueRef is_array = zan_icmp(g->builder, LLVMIntEQ, word,
            LLVMConstInt(i64, ZAN_ARRAY_MAGIC, 0), "strbuf.isarr");
        LLVMBuildCondBr(g->builder, is_array, arr_bb, slow_bb);
        LLVMPositionBuilderAtEnd(g->builder, arr_bb);
        LLVMValueRef cnt_ptr = LLVMBuildGEP2(g->builder, i8, payload,
            &(LLVMValueRef){ LLVMConstInt(i64, (uint64_t)ZAN_OBJ_RC_OFF, 1) }, 1,
            "strbuf.countp");
        count = LLVMBuildLoad2(g->builder,
            i64, LLVMBuildBitCast(g->builder, cnt_ptr, i64p, "strbuf.countip"),
            "strbuf.count");
        LLVMBuildBr(g->builder, done_bb);
    }

    LLVMPositionBuilderAtEnd(g->builder, slow_bb);
    LLVMValueRef string_len = zan_call2(g->builder, strlen_ty, g->fn_strlen,
                                        &payload, 1, "strbuf.strlen");
    LLVMValueRef fits = zan_and(g->builder,
        zan_icmp(g->builder, LLVMIntNE, string_len,
                 LLVMConstInt(i64, ZAN_STR_LEN_UNKNOWN, 0), "strbuf.notunk"),
        zan_icmp(g->builder, LLVMIntULE, string_len,
                 LLVMConstInt(i64, ZAN_STR_LEN_MASK, 0), "strbuf.fits32"),
        "strbuf.fits");
    LLVMBuildCondBr(g->builder, zan_and(g->builder, is_str, fits,
                                        "strbuf.stampable"),
                    stamp_bb, slow_end_bb);

    LLVMPositionBuilderAtEnd(g->builder, stamp_bb);
    LLVMValueRef rc_ptr = LLVMBuildGEP2(g->builder, i8, payload,
        &(LLVMValueRef){ LLVMConstInt(i64, (uint64_t)ZAN_OBJ_RC_OFF, 1) }, 1,
        "strbuf.rcp");
    LLVMValueRef rc = LLVMBuildLoad2(g->builder, i64,
        LLVMBuildBitCast(g->builder, rc_ptr, i64p, "strbuf.rcip"), "strbuf.rc");
    LLVMBuildCondBr(g->builder,
        zan_icmp(g->builder, LLVMIntNE, rc,
                 LLVMConstInt(i64, ZAN_STRING_SENTINEL_RC, 0),
                 "strbuf.writable"),
        stamp_do_bb, slow_end_bb);

    LLVMPositionBuilderAtEnd(g->builder, stamp_do_bb);
    LLVMBuildStore(g->builder, LLVMBuildOr(g->builder,
            LLVMConstInt(i64, ZAN_STRING_TAG << 32, 0), string_len,
            "strbuf.newhdr"),
        hdr_iptr);
    LLVMBuildBr(g->builder, slow_end_bb);

    LLVMPositionBuilderAtEnd(g->builder, slow_end_bb);
    LLVMBuildBr(g->builder, done_bb);

    LLVMPositionBuilderAtEnd(g->builder, raw_bb);
    if (g->runtime_checks) {
        emit_string_null_report(g, payload, loc);
        LLVMBuildBr(g->builder, done_bb);
        phi_raw = LLVMConstInt(i64, 0, 0);
        phi_raw_pred = LLVMGetInsertBlock(g->builder);
    } else {
        phi_raw = zan_call2(g->builder, strlen_ty, g->fn_strlen,
                            &payload, 1, "strbuf.rawlen");
        LLVMBuildBr(g->builder, done_bb);
        phi_raw_pred = raw_bb;
    }
    LLVMPositionBuilderAtEnd(g->builder, done_bb);
    LLVMValueRef phi = LLVMBuildPhi(g->builder, i64, "strbuf.len");
    LLVMAddIncoming(phi, (LLVMValueRef[]){ cached },
                    (LLVMBasicBlockRef[]){ probe_bb }, 1);
    if (arr_bb)
        LLVMAddIncoming(phi, (LLVMValueRef[]){ count },
                        (LLVMBasicBlockRef[]){ arr_bb }, 1);
    LLVMAddIncoming(phi, (LLVMValueRef[]){ string_len },
                    (LLVMBasicBlockRef[]){ slow_end_bb }, 1);
    if (phi_raw)
        LLVMAddIncoming(phi, (LLVMValueRef[]){ phi_raw },
                        (LLVMBasicBlockRef[]){ phi_raw_pred }, 1);
    return phi;
}
void emit_string_len_invalidate(zan_irgen_t *g, LLVMValueRef payload) {
    if (LLVMGetTypeKind(LLVMTypeOf(payload)) != LLVMPointerTypeKind) return;
    LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i64p = LLVMPointerType(i64, 0);
    LLVMBasicBlockRef entry_bb = LLVMGetInsertBlock(g->builder);
    LLVMValueRef fn = LLVMGetBasicBlockParent(entry_bb);
    LLVMBasicBlockRef probe_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "strinv.probe");
    LLVMBasicBlockRef clear_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "strinv.clear");
    LLVMBasicBlockRef done_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "strinv.done");
    LLVMValueRef hdr_ptr = LLVMBuildGEP2(g->builder, i8, payload,
        &(LLVMValueRef){ LLVMConstInt(i64, (uint64_t)ZAN_OBJ_SITE_OFF, 1) }, 1,
        "strinv.hdrp");
    LLVMValueRef read_ok = zan_hdr_read_ok(g, payload);
    LLVMValueRef nonnull = zan_icmp(g->builder, LLVMIntNE, payload,
        LLVMConstNull(LLVMTypeOf(payload)), "strinv.nonnull");
    if (read_ok) nonnull = zan_and(g->builder, nonnull, read_ok, "strinv.ok");
    LLVMBuildCondBr(g->builder, nonnull, probe_bb, done_bb);

    LLVMPositionBuilderAtEnd(g->builder, probe_bb);
    LLVMValueRef hdr_iptr = LLVMBuildBitCast(g->builder, hdr_ptr, i64p,
                                             "strinv.hdrip");
    LLVMValueRef word = LLVMBuildLoad2(g->builder, i64, hdr_iptr, "strinv.hdr");
    LLVMValueRef stale = zan_and(g->builder,
        zan_hdr_is_string(g, word, "strinv.isstr"),
        zan_icmp(g->builder, LLVMIntNE,
                 LLVMBuildAnd(g->builder, word,
                     LLVMConstInt(i64, ZAN_STR_LEN_MASK, 0), "strinv.cached"),
                 LLVMConstInt(i64, ZAN_STR_LEN_UNKNOWN, 0), "strinv.measured"),
        "strinv.stale");
    LLVMBuildCondBr(g->builder, stale, clear_bb, done_bb);

    LLVMPositionBuilderAtEnd(g->builder, clear_bb);
    LLVMValueRef rc_ptr = LLVMBuildGEP2(g->builder, i8, payload,
        &(LLVMValueRef){ LLVMConstInt(i64, (uint64_t)ZAN_OBJ_RC_OFF, 1) }, 1,
        "strinv.rcp");
    LLVMValueRef rc = LLVMBuildLoad2(g->builder, i64,
        LLVMBuildBitCast(g->builder, rc_ptr, i64p, "strinv.rcip"), "strinv.rc");
    LLVMBasicBlockRef store_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "strinv.store");
    LLVMBuildCondBr(g->builder,
        zan_icmp(g->builder, LLVMIntNE, rc,
                 LLVMConstInt(i64, ZAN_STRING_SENTINEL_RC, 0),
                 "strinv.writable"),
        store_bb, done_bb);
    LLVMPositionBuilderAtEnd(g->builder, store_bb);
    LLVMBuildStore(g->builder,
                   LLVMConstInt(i64, ZAN_STR_HDR_WORD(ZAN_STR_LEN_UNKNOWN), 0),
                   hdr_iptr);
    LLVMBuildBr(g->builder, done_bb);

    LLVMPositionBuilderAtEnd(g->builder, done_bb);
}
void emit_string_len_set(zan_irgen_t *g, LLVMValueRef payload,
                                LLVMValueRef len) {
    LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i64p = LLVMPointerType(i64, 0);
    LLVMValueRef fits = zan_and(g->builder,
        zan_icmp(g->builder, LLVMIntULE, len,
                 LLVMConstInt(i64, ZAN_STR_LEN_MASK, 0), "strset.fits32"),
        zan_icmp(g->builder, LLVMIntNE, len,
                 LLVMConstInt(i64, ZAN_STR_LEN_UNKNOWN, 0), "strset.notunk"),
        "strset.ok");
    LLVMValueRef half = LLVMBuildSelect(g->builder, fits, len,
        LLVMConstInt(i64, ZAN_STR_LEN_UNKNOWN, 0), "strset.half");
    LLVMValueRef word = LLVMBuildOr(g->builder,
        LLVMConstInt(i64, ZAN_STRING_TAG << 32, 0), half, "strset.hdr");
    LLVMValueRef hdr_ptr = LLVMBuildGEP2(g->builder, i8, payload,
        &(LLVMValueRef){ LLVMConstInt(i64, (uint64_t)ZAN_OBJ_SITE_OFF, 1) }, 1,
        "strset.hdrp");
    LLVMBuildStore(g->builder, word,
        LLVMBuildBitCast(g->builder, hdr_ptr, i64p, "strset.hdrip"));
}
bool sym_declares_extern(zan_symbol_t *sym) {
    if (!sym || !sym->decl || sym->decl->kind != AST_METHOD_DECL) return false;
    return zan_ast_method_extern_lib(sym->decl).str != NULL ||
           (sym->decl->method_decl.modifiers & MOD_EXTERN) != 0;
}

static zan_symbol_t *extern_check_callee_sym(zan_irgen_t *g,
                                             zan_ast_node_t *call) {
    zan_ast_node_t *callee = call->call.callee;
    if (!callee) return NULL;
    if (callee->kind == AST_IDENTIFIER) {
        if (g->current_type_sym)
            return get_method_sym(g->current_type_sym,
                                  callee->ident.name);
        return zan_binder_lookup(g->binder, callee->ident.name);
    }
    if (callee->kind == AST_MEMBER_ACCESS &&
        callee->member.object->kind == AST_IDENTIFIER) {
        zan_symbol_t *cls = zan_binder_lookup(g->binder,
            callee->member.object->ident.name);
        if (cls) return get_method_sym(cls, callee->member.name);
    }
    return NULL;
}
void emit_extern_call_len_invalidate(zan_irgen_t *g, LLVMValueRef fn,
                                            LLVMValueRef *args, int argc,
                                            zan_ast_node_t *call,
                                            local_scope_t *locals) {
    if (!fn || !args || argc <= 0 || !call) return;
    if (!LLVMIsAFunction(fn)) return;
    zan_symbol_t *cs = extern_check_callee_sym(g, call);
    if (!sym_declares_extern(cs)) return;
    int m = call->call.args.count;
    if (m <= 0 || m > argc) return;
    for (int i = 0; i < m; i++) {
        LLVMValueRef v = args[argc - m + i];
        if (!v || LLVMGetTypeKind(LLVMTypeOf(v)) != LLVMPointerTypeKind)
            continue;
        if (!is_string_expr(g, call->call.args.items[i], locals)) continue;
        emit_string_len_invalidate(g, v);
    }
}
LLVMValueRef emit_string_buffer_len(zan_irgen_t *g, LLVMValueRef payload,
                                           zan_loc_t loc) {
    return emit_string_len_ex(g, payload, 1, loc);
}
