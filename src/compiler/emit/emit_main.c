#include "irgen_internal.h"


static void emit_eh_terminate_setup(zan_irgen_t *g, bool define_body) {
    if (g->target_is_wasm) return;
    LLVMTypeRef void_ty = LLVMVoidTypeInContext(g->ctx);
    LLVMTypeRef term_fn_ty = LLVMFunctionType(void_ty, NULL, 0, 0);
    LLVMValueRef term_fn = LLVMGetNamedFunction(g->mod, "__zan_eh_terminate");
    if (!term_fn)
        term_fn = LLVMAddFunction(g->mod, "__zan_eh_terminate", term_fn_ty);
    /* Main must finish discovering static fields before its terminal exit
     * can share the same ARC cleanup as the normal exit. */
    if (define_body && LLVMCountBasicBlocks(term_fn) == 0) {
        LLVMBasicBlockRef tbb = LLVMAppendBasicBlockInContext(g->ctx, term_fn, "entry");
        zan_builder_point_t pt = zan_builder_save(g);
        g->current_fn = term_fn;
        LLVMPositionBuilderAtEnd(g->builder, tbb);
        di_clear(g);
        emit_eh_hook_call(g, "__zan_eh_unhandled");
        LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
        LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
        LLVMValueRef top_g, bufs_g, exc_g;
        get_eh_globals(g, &top_g, &bufs_g, &exc_g);
        LLVMValueRef printf_fn = get_libc_fn(g, "printf", LLVMFunctionType(i32t, &i8ptr, 1, 1));
        LLVMValueRef exc = LLVMBuildLoad2(g->builder, i8ptr, exc_g, "term.exc");
        LLVMValueRef tid = LLVMBuildLoad2(g->builder, i8ptr, get_eh_exc_tid_global(g), "term.tid");
        LLVMValueRef hasExc = zan_icmp(g->builder, LLVMIntNE, exc, LLVMConstNull(i8ptr), "term.has");
        LLVMValueRef isStr = zan_icmp(g->builder, LLVMIntEQ, tid, LLVMConstNull(i8ptr), "term.isstr");
        LLVMBasicBlockRef fn_bb = LLVMGetInsertBlock(g->builder);
        LLVMValueRef cur_fn = LLVMGetBasicBlockParent(fn_bb);
        LLVMBasicBlockRef check_bb = LLVMAppendBasicBlockInContext(g->ctx, cur_fn, "term.check");
        LLVMBasicBlockRef str_bb = LLVMAppendBasicBlockInContext(g->ctx, cur_fn, "term.str");
        LLVMBasicBlockRef cls_bb = LLVMAppendBasicBlockInContext(g->ctx, cur_fn, "term.cls");
        LLVMBasicBlockRef none_bb = LLVMAppendBasicBlockInContext(g->ctx, cur_fn, "term.none");
        LLVMBasicBlockRef cont_bb = LLVMAppendBasicBlockInContext(g->ctx, cur_fn, "term.cont");
        LLVMBuildCondBr(g->builder, hasExc, check_bb, none_bb);
        LLVMPositionBuilderAtEnd(g->builder, check_bb);
        LLVMBuildCondBr(g->builder, isStr, str_bb, cls_bb);
        LLVMPositionBuilderAtEnd(g->builder, str_bb);
        {
            LLVMValueRef sfmt = zan_irgen_intern_string(g, "Unhandled exception: %s\n");
            zan_call2(g->builder, LLVMFunctionType(i32t, &i8ptr, 1, 1), printf_fn,
                (LLVMValueRef[]){ sfmt, exc }, 2, "");
            LLVMBuildBr(g->builder, cont_bb);
        }
        LLVMPositionBuilderAtEnd(g->builder, cls_bb);
        {
            LLVMValueRef name_fn = get_eh_tid_name_fn(g);
            LLVMValueRef cname = zan_call2(g->builder,
                LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr }, 1, 0),
                name_fn, (LLVMValueRef[]){ tid }, 1, "term.cname");
            LLVMValueRef found = zan_icmp(g->builder, LLVMIntNE, cname,
                LLVMConstNull(i8ptr), "term.cfound");
            LLVMBasicBlockRef named_bb = LLVMAppendBasicBlockInContext(g->ctx, cur_fn, "term.named");
            LLVMBasicBlockRef anon_bb = LLVMAppendBasicBlockInContext(g->ctx, cur_fn, "term.anon");
            LLVMBuildCondBr(g->builder, found, named_bb, anon_bb);
            LLVMPositionBuilderAtEnd(g->builder, named_bb);
            emit_unhandled_class_exc_print(g, cur_fn, LLVMFunctionType(i32t, &i8ptr, 1, 1),
                printf_fn, cname, exc, tid, cont_bb);
            LLVMPositionBuilderAtEnd(g->builder, anon_bb);
            LLVMValueRef afmt = zan_irgen_intern_string(g, "Unhandled exception: class instance\n");
            zan_call2(g->builder, LLVMFunctionType(i32t, &i8ptr, 1, 1), printf_fn,
                (LLVMValueRef[]){ afmt }, 1, "");
            LLVMBuildBr(g->builder, cont_bb);
        }
        LLVMPositionBuilderAtEnd(g->builder, none_bb);
        {
            LLVMValueRef nfmt = zan_irgen_intern_string(g, "Unhandled exception\n");
            zan_call2(g->builder, LLVMFunctionType(i32t, &i8ptr, 1, 1), printf_fn,
                (LLVMValueRef[]){ nfmt }, 1, "");
            LLVMBuildBr(g->builder, cont_bb);
        }
        LLVMPositionBuilderAtEnd(g->builder, cont_bb);
        /* The native unwind payload only transports this TLS owner. Release
         * the Zan value once, after its diagnostic, without treating a foreign
         * native exception as an ARC object. */
        LLVMValueRef owned_g = get_eh_exc_owned_global(g);
        LLVMValueRef owned = LLVMBuildLoad2(g->builder, i32t, owned_g, "term.owned");
        zan_store_fit(g, LLVMConstInt(i32t, 0, 0), owned_g);
        zan_store_fit(g, LLVMConstNull(i8ptr), exc_g);
        zan_store_fit(g, LLVMConstNull(i8ptr), get_eh_exc_tid_global(g));
        LLVMBasicBlockRef release_bb = LLVMAppendBasicBlockInContext(g->ctx, cur_fn, "term.release");
        LLVMBasicBlockRef release_obj = LLVMAppendBasicBlockInContext(g->ctx, cur_fn, "term.release.obj");
        LLVMBasicBlockRef release_str = LLVMAppendBasicBlockInContext(g->ctx, cur_fn, "term.release.str");
        LLVMBasicBlockRef cleanup_bb = LLVMAppendBasicBlockInContext(g->ctx, cur_fn, "term.cleanup");
        LLVMBuildCondBr(g->builder, zan_icmp(g->builder, LLVMIntNE, owned,
            LLVMConstInt(i32t, 0, 0), "term.isowned"), release_bb, cleanup_bb);
        LLVMPositionBuilderAtEnd(g->builder, release_bb);
        LLVMBuildCondBr(g->builder, isStr, release_str, release_obj);
        LLVMPositionBuilderAtEnd(g->builder, release_obj);
        zan_call2(g->builder, LLVMGlobalGetValueType(g->rt_release_dyn),
            g->rt_release_dyn, &exc, 1, "");
        LLVMBuildBr(g->builder, cleanup_bb);
        LLVMPositionBuilderAtEnd(g->builder, release_str);
        zan_call2(g->builder, LLVMGlobalGetValueType(g->rt_str_release),
            g->rt_str_release, &exc, 1, "");
        LLVMBuildBr(g->builder, cleanup_bb);
        LLVMPositionBuilderAtEnd(g->builder, cleanup_bb);
        emit_release_static_rc_fields(g, NULL);
        LLVMValueRef exit_fn = get_libc_fn(g, "exit", LLVMFunctionType(void_ty, &i32t, 1, 0));
        LLVMValueRef code1 = LLVMConstInt(i32t, 1, 0);
        zan_call2(g->builder, LLVMFunctionType(void_ty, &i32t, 1, 0), exit_fn, &code1, 1, "");
        LLVMBuildUnreachable(g->builder);
        zan_builder_restore(g, pt);
    }
    if (define_body) return;

    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef st_ty = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr }, 1, 0);
    LLVMValueRef set_term = LLVMGetNamedFunction(g->mod, "_ZSt13set_terminatePFvvE");
    if (!set_term) set_term = LLVMAddFunction(g->mod, "_ZSt13set_terminatePFvvE", st_ty);
    LLVMValueRef term_ptr = LLVMBuildBitCast(g->builder, term_fn, i8ptr, "term.fp");
    zan_call2(g->builder, st_ty, set_term, &term_ptr, 1, "");
}

static void emit_main_method(zan_irgen_t *g, zan_ast_node_t *method, zan_symbol_t *type_sym,
                             zan_ast_node_t *unit) {
    LLVMTypeRef i32ty = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef i8ptrptr = LLVMPointerType(i8ptr, 0);
    LLVMTypeRef main_params[] = { i32ty, i8ptrptr };
    LLVMTypeRef main_type = LLVMFunctionType(i32ty, main_params, 2, 0);
    LLVMValueRef main_fn = LLVMAddFunction(g->mod, "main", main_type);

    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, main_fn, "entry");
    LLVMPositionBuilderAtEnd(g->builder, entry);
    di_clear(g);
    emit_eh_terminate_setup(g, false);

    if (g->target_is_windows) {
        LLVMTypeRef uintt = LLVMInt32TypeInContext(g->ctx);
        LLVMTypeRef setcp_type = LLVMFunctionType(uintt, (LLVMTypeRef[]){ uintt }, 1, 0);
        LLVMValueRef fn_set_out = LLVMGetNamedFunction(g->mod, "SetConsoleOutputCP");
        if (!fn_set_out) fn_set_out = LLVMAddFunction(g->mod, "SetConsoleOutputCP", setcp_type);
        LLVMValueRef fn_set_in = LLVMGetNamedFunction(g->mod, "SetConsoleCP");
        if (!fn_set_in) fn_set_in = LLVMAddFunction(g->mod, "SetConsoleCP", setcp_type);
        LLVMValueRef cp_utf8 = LLVMConstInt(uintt, 65001, 0);
        zan_call2(g->builder, setcp_type, fn_set_out, &cp_utf8, 1, "");
        zan_call2(g->builder, setcp_type, fn_set_in, &cp_utf8, 1, "");
    }

    LLVMValueRef main_argc = LLVMGetParam(main_fn, 0);
    LLVMValueRef main_argv = LLVMGetParam(main_fn, 1);
    if (g->target_is_windows) {
        LLVMTypeRef i32ptr = LLVMPointerType(i32ty, 0);
        LLVMTypeRef i8ptrptrptr = LLVMPointerType(i8ptrptr, 0);
        LLVMTypeRef utf8_argv_type = LLVMFunctionType(i32ty,
            (LLVMTypeRef[]){ i32ptr, i8ptrptrptr }, 2, 0);
        LLVMValueRef utf8_argv_fn = LLVMGetNamedFunction(g->mod, "zan_utf8_argv");
        if (!utf8_argv_fn)
            utf8_argv_fn = LLVMAddFunction(g->mod, "zan_utf8_argv", utf8_argv_type);
        LLVMValueRef argc_slot = LLVMBuildAlloca(g->builder, i32ty, "utf8_argc");
        LLVMValueRef argv_slot = LLVMBuildAlloca(g->builder, i8ptrptr, "utf8_argv");
        LLVMBuildStore(g->builder, main_argc, argc_slot);
        LLVMBuildStore(g->builder, main_argv, argv_slot);
        LLVMValueRef utf8_argv_args[] = { argc_slot, argv_slot };
        zan_call2(g->builder, utf8_argv_type, utf8_argv_fn, utf8_argv_args, 2, "");
        main_argc = LLVMBuildLoad2(g->builder, i32ty, argc_slot, "utf8_argc.value");
        main_argv = LLVMBuildLoad2(g->builder, i8ptrptr, argv_slot, "utf8_argv.value");
    }

    LLVMValueRef g_argc = LLVMGetNamedGlobal(g->mod, "__zan_argc");
    if (!g_argc) {
        g_argc = LLVMAddGlobal(g->mod, i32ty, "__zan_argc");
        LLVMSetInitializer(g_argc, LLVMConstInt(i32ty, 0, 0));
    }
    LLVMValueRef g_argv = LLVMGetNamedGlobal(g->mod, "__zan_argv");
    if (!g_argv) {
        g_argv = LLVMAddGlobal(g->mod, i8ptrptr, "__zan_argv");
        LLVMSetInitializer(g_argv, LLVMConstNull(i8ptrptr));
    }
    LLVMBuildStore(g->builder, main_argc, g_argc);
    LLVMBuildStore(g->builder, main_argv, g_argv);

    {
        LLVMTypeRef i8p = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
        LLVMTypeRef svi32 = LLVMInt32TypeInContext(g->ctx);
        LLVMTypeRef svi64 = LLVMInt64TypeInContext(g->ctx);
        LLVMValueRef stdout_ptr;
        if (g->target_is_windows) {
            LLVMTypeRef iob_type = LLVMFunctionType(i8p, (LLVMTypeRef[]){ svi32 }, 1, 0);
            LLVMValueRef iobfn = LLVMGetNamedFunction(g->mod, "__acrt_iob_func");
            if (!iobfn) iobfn = LLVMAddFunction(g->mod, "__acrt_iob_func", iob_type);
            LLVMValueRef one = LLVMConstInt(svi32, 1, 0);
            stdout_ptr = zan_call2(g->builder, iob_type, iobfn, &one, 1, "stdout");
        } else {
            const char *soname = (g->target_is_macos || g->target_is_ios) ? "__stdoutp" : "stdout";
            LLVMValueRef sg = LLVMGetNamedGlobal(g->mod, soname);
            if (!sg) sg = LLVMAddGlobal(g->mod, i8p, soname);
            stdout_ptr = LLVMBuildLoad2(g->builder, i8p, sg, "stdout");
        }
        LLVMTypeRef sv_type = LLVMFunctionType(svi32,
            (LLVMTypeRef[]){ i8p, i8p, svi32, svi64 }, 4, 0);
        LLVMValueRef sv = LLVMGetNamedFunction(g->mod, "setvbuf");
        if (!sv) sv = LLVMAddFunction(g->mod, "setvbuf", sv_type);
        int sv_mode = g->target_is_windows ? 4 /* _IONBF */ : 1 /* _IOLBF */;
        /* 缓冲策略交给运行时 zan_rt_stdout_setup：终端/管道保持逐行（IDE 控制台、
         * docker logs 等实时消费者依赖），重定向到普通文件时整块缓冲——曾每行
         * 一次 write(2)，200 万行写文件 3.5 s（C 全缓冲 0.46 s）。模块内放同名
         * 弱实现 = 原样 setvbuf，未带此符号的旧预编译交叉运行时行为不变。 */
        LLVMTypeRef su_type = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx),
            (LLVMTypeRef[]){ i8p, svi32, svi64 }, 3, 0);
        LLVMValueRef su = LLVMGetNamedFunction(g->mod, "zan_rt_stdout_setup");
        if (!su) {
            su = LLVMAddFunction(g->mod, "zan_rt_stdout_setup", su_type);
            LLVMSetLinkage(su, LLVMWeakAnyLinkage);
            LLVMBasicBlockRef cur = LLVMGetInsertBlock(g->builder);
            LLVMPositionBuilderAtEnd(g->builder,
                LLVMAppendBasicBlockInContext(g->ctx, su, "entry"));
            LLVMValueRef fb_args[] = { LLVMGetParam(su, 0), LLVMConstNull(i8p),
                LLVMGetParam(su, 1), LLVMGetParam(su, 2) };
            zan_call2(g->builder, sv_type, sv, fb_args, 4, "");
            LLVMBuildRetVoid(g->builder);
            LLVMPositionBuilderAtEnd(g->builder, cur);
        }
        LLVMValueRef su_args[] = { stdout_ptr,
            LLVMConstInt(svi32, sv_mode, 0),
            LLVMConstInt(svi64, g->target_is_windows ? 0 : 4096, 0) };
        zan_call2(g->builder, su_type, su, su_args, 3, "");
    }

    if (g->target_is_windows) {
        LLVMValueRef set_out_cp = LLVMGetNamedFunction(g->mod, "SetConsoleOutputCP");
        LLVMTypeRef setcp_type = LLVMFunctionType(i32ty, (LLVMTypeRef[]){ i32ty }, 1, 0);
        if (!set_out_cp)
            set_out_cp = LLVMAddFunction(g->mod, "SetConsoleOutputCP", setcp_type);
        LLVMValueRef set_in_cp = LLVMGetNamedFunction(g->mod, "SetConsoleCP");
        if (!set_in_cp)
            set_in_cp = LLVMAddFunction(g->mod, "SetConsoleCP", setcp_type);
        LLVMValueRef utf8cp = LLVMConstInt(i32ty, 65001, 0);
        zan_call2(g->builder, setcp_type, set_out_cp, &utf8cp, 1, "");
        zan_call2(g->builder, setcp_type, set_in_cp, &utf8cp, 1, "");
    }

    g->current_fn = main_fn;
    g->current_fn_ret_type = LLVMInt32TypeInContext(g->ctx);
    if (g->strict_runtime) {
        LLVMTypeRef strict_ty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx),
                                                 NULL, 0, 0);
        LLVMValueRef strict_fn = LLVMGetNamedFunction(g->mod, "zan_rt_set_strict");
        if (!strict_fn)
            strict_fn = LLVMAddFunction(g->mod, "zan_rt_set_strict", strict_ty);
        zan_call2(g->builder, strict_ty, strict_fn, NULL, 0, "");
    }
    g->throw_locals_base = 0;
    g->catch_cleanup_count = 0;
    g->throw_catch_base = 0;
    g->finally_count = 0;
    g->pending = (zan_irgen_pending_context_t){0};
    g->finally_loop_base = 0;
    g->eh_armed_count = 0;
    g->eh_armed_base = 0;
    g->eh_armed_loop_base = 0;
    g->current_type_sym = type_sym;
    g->current_this = NULL;
    g->current_fn_body = method->method_decl.body;
    irgen_record_static_type_use(g, type_sym, NULL);

    if (g->check_leaks) {
        emit_leak_report_support(g);
        zan_call2(g->builder, g->atexit_type, g->fn_atexit,
                       &g->fn_report_leaks, 1, "");
    }

    if (!g->desc_hdr && g->g_site_field_graphs) {
        LLVMTypeRef i8p = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
        LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
        LLVMTypeRef reg_args[] = { LLVMPointerType(i8p, 0), LLVMPointerType(i8p, 0), i64t };
        LLVMTypeRef reg_ty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), reg_args, 3, 0);
        LLVMValueRef reg_fn = LLVMGetNamedFunction(g->mod, "zan_rt_register_site_field_graphs");
        if (!reg_fn) reg_fn = LLVMAddFunction(g->mod, "zan_rt_register_site_field_graphs", reg_ty);
        LLVMValueRef tbl = LLVMBuildLoad2(g->builder, LLVMPointerType(i8p, 0), g->g_site_field_graphs, "fgtbl");
        LLVMValueRef tntbl = g->g_site_tynames ? LLVMBuildLoad2(g->builder, LLVMPointerType(i8p, 0), g->g_site_tynames, "tntbl")
                                               : LLVMConstNull(LLVMPointerType(i8p, 0));
        /* 站点数在 main 发射时还没定（之后生成的代码继续分配站点），常量会
         * 冻结成 1——运行时据此拒绝全部站点，GC 探测/回收/快照在 --check-leaks
         * 下全部失明。读模块末尾才填好的 __zan_site_count。 */
        LLVMValueRef sc = g->g_site_count
            ? LLVMBuildLoad2(g->builder, i64t, g->g_site_count, "fgcnt")
            : LLVMConstInt(i64t, (uint64_t)g->leak_site_count, 0);
        zan_call2(g->builder, reg_ty, reg_fn, (LLVMValueRef[]){ tbl, tntbl, sc }, 3, "");
        if (g->g_site_dtors) {
            /* GC.CollectCycles 在站点表模式下按站点找类型化释放函数 */
            LLVMTypeRef rd_ty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx),
                (LLVMTypeRef[]){ LLVMPointerType(i8p, 0), i64t }, 2, 0);
            LLVMValueRef rd_fn = LLVMGetNamedFunction(g->mod, "zan_rt_register_site_dtors");
            if (!rd_fn) {
                rd_fn = LLVMAddFunction(g->mod, "zan_rt_register_site_dtors", rd_ty);
                LLVMSetLinkage(rd_fn, LLVMWeakAnyLinkage);
                LLVMBasicBlockRef cur = LLVMGetInsertBlock(g->builder);
                LLVMPositionBuilderAtEnd(g->builder,
                    LLVMAppendBasicBlockInContext(g->ctx, rd_fn, "entry"));
                LLVMBuildRetVoid(g->builder);
                LLVMPositionBuilderAtEnd(g->builder, cur);
            }
            LLVMValueRef dt = LLVMBuildLoad2(g->builder, LLVMPointerType(i8p, 0),
                g->g_site_dtors, "dttbl");
            zan_call2(g->builder, rd_ty, rd_fn, (LLVMValueRef[]){ dt, sc }, 2, "");
        }
    }

    /* 协程工作窃取调度器 */
    zan_call2(g->builder, g->rt_co_sched_init_type, g->rt_co_sched_init, NULL, 0, "");

    di_set_loc(g, method->loc);

    /* Its body is filled only after callable/type demand reaches the fixed
     * point. A loaded declaration or an initializer cannot root itself. */
    LLVMValueRef static_init = get_static_init_coordinator(g);
    zan_call2(g->builder, LLVMGlobalGetValueType(static_init), static_init,
              NULL, 0, "");

    if ((method->method_decl.modifiers & MOD_ASYNC) &&
        method->method_decl.params.count == 0 && type_sym) {
        char ramp_name[512];
        snprintf(ramp_name, sizeof(ramp_name), "%.*s_Main",
                 (int)type_sym->name.len, type_sym->name.str);
        LLVMValueRef ramp = LLVMGetNamedFunction(g->mod, ramp_name);
        char res_name[520];
        snprintf(res_name, sizeof(res_name), "%s$resume", ramp_name);
        LLVMValueRef resume = LLVMGetNamedFunction(g->mod, res_name);
        if (ramp && resume) {
            LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
            LLVMTypeRef mi8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMValueRef aw_tmp_mark = LLVMBuildLoad2(g->builder,
                LLVMInt32TypeInContext(g->ctx), get_eh_tmp_top_global(g),
                "main.tmpmark");
            LLVMValueRef sub = zan_call2(g->builder, LLVMGlobalGetValueType(ramp),
                ramp, NULL, 0, "main.task");
            LLVMValueRef sub_i8 = LLVMBuildBitCast(g->builder, sub, mi8ptr, "main.task8");
            LLVMValueRef sched_args[] = { sub_i8, resume };
            zan_call2(g->builder, g->rt_co_ready_type, g->rt_co_ready, sched_args, 2, "");
            zan_call2(g->builder, g->rt_co_sched_run_type, g->rt_co_sched_run, NULL, 0, "");
            emit_async_check_sub_exc(g, sub_i8, aw_tmp_mark);
            LLVMValueRef rptr = LLVMBuildStructGEP2(g->builder, g->co_header_type,
                sub_i8, ASYNC_FRAME_RESULT, "main.res.p");
            LLVMValueRef res = LLVMBuildLoad2(g->builder, i64, rptr, "main.res");
            zan_emit_frame_free(g, sub_i8);
            emit_release_static_rc_fields(g, unit);
            LLVMBuildRet(g->builder, LLVMBuildTrunc(g->builder, res,
                LLVMInt32TypeInContext(g->ctx), "main.ret"));
            emit_eh_terminate_setup(g, true);
            g->current_type_sym = NULL;
            g->current_fn_body = NULL;
            return;
        }
    }

    local_scope_t *locals = local_scope_new(g->arena);

    if (method->method_decl.params.count == 1) {
        zan_ast_node_t *param = method->method_decl.params.items[0];
        zan_type_t *pt = zan_binder_resolve_type(g->binder, param->param.type);
        if (pt && pt->kind == TYPE_ARRAY && pt->element_type &&
            pt->element_type->kind == TYPE_STRING) {
            LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
            LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
            LLVMTypeRef i8ptrptr = LLVMPointerType(i8ptr, 0);
            LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
            LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
            LLVMValueRef g_argc = LLVMGetNamedGlobal(g->mod, "__zan_argc");
            if (!g_argc) {
                g_argc = LLVMAddGlobal(g->mod, i32, "__zan_argc");
                LLVMSetInitializer(g_argc, LLVMConstInt(i32, 0, 0));
            }
            LLVMValueRef g_argv = LLVMGetNamedGlobal(g->mod, "__zan_argv");
            if (!g_argv) {
                g_argv = LLVMAddGlobal(g->mod, i8ptrptr, "__zan_argv");
                LLVMSetInitializer(g_argv, LLVMConstNull(i8ptrptr));
            }
            LLVMValueRef argc = LLVMBuildLoad2(g->builder, i32, g_argc, "ma.argc");
            LLVMValueRef argc64 = LLVMBuildSExt(g->builder, argc, i64t, "ma.argc64");
            LLVMValueRef nneg = zan_icmp(g->builder, LLVMIntSGT, argc64,
                LLVMConstInt(i64t, 0, 0), "ma.nneg");
            LLVMValueRef n = LLVMBuildSelect(g->builder, nneg,
                zan_sub(g->builder, argc64, LLVMConstInt(i64t, 1, 0), "ma.n"),
                LLVMConstInt(i64t, 0, 0), "ma.count");
            LLVMValueRef total = zan_mul(g->builder, n,
                LLVMSizeOf(i8ptrptr), "ma.total");
            LLVMValueRef arr = zan_array_alloc_typed(g, total, n, g->binder->type_string);
            LLVMValueRef argv = LLVMBuildLoad2(g->builder, i8ptrptr, g_argv, "ma.argv");
            LLVMValueRef lp = emit_entry_alloca(g, i64t, "ma.i");
            zan_store_fit(g, LLVMConstInt(i64t, 0, 0), lp);
            LLVMValueRef ffn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder));
            LLVMBasicBlockRef cond_bb = LLVMAppendBasicBlockInContext(g->ctx, ffn, "ma.cond");
            LLVMBasicBlockRef body_bb = LLVMAppendBasicBlockInContext(g->ctx, ffn, "ma.fill");
            LLVMBasicBlockRef done_bb = LLVMAppendBasicBlockInContext(g->ctx, ffn, "ma.filled");
            LLVMBuildBr(g->builder, cond_bb);
            LLVMPositionBuilderAtEnd(g->builder, cond_bb);
            LLVMValueRef iv = LLVMBuildLoad2(g->builder, i64t, lp, "ma.iv");
            LLVMBuildCondBr(g->builder, zan_icmp(g->builder, LLVMIntSLT, iv, n, "ma.more"),
                body_bb, done_bb);
            LLVMPositionBuilderAtEnd(g->builder, body_bb);
            LLVMValueRef iv1 = zan_add(g->builder, iv, LLVMConstInt(i64t, 1, 0), "ma.i1");
            LLVMValueRef slot = LLVMBuildGEP2(g->builder, i8ptr, argv, &iv1, 1, "ma.slot");
            LLVMValueRef cstr = LLVMBuildLoad2(g->builder, i8ptr, slot, "ma.cstr");
            LLVMValueRef len = zan_call2(g->builder,
                LLVMFunctionType(i64t, (LLVMTypeRef[]){ i8ptr }, 1, 0),
                g->fn_strlen, &cstr, 1, "ma.len");
            LLVMValueRef cap = zan_add(g->builder, len, LLVMConstInt(i64t, 1, 0), "ma.cap");
            LLVMValueRef buf = emit_string_alloc_rc(g, cap);
            LLVMTypeRef memcpy_ty = LLVMFunctionType(i8ptr,
                (LLVMTypeRef[]){ i8ptr, i8ptr, i64t }, 3, 0);
            LLVMValueRef mc = get_libc_fn(g, "memcpy", memcpy_ty);
            zan_call2(g->builder, memcpy_ty, mc,
                (LLVMValueRef[]){ buf, cstr, len }, 3, "");
            LLVMValueRef endp = LLVMBuildGEP2(g->builder, i8, buf, &len, 1, "ma.ep");
            zan_store_fit(g, LLVMConstInt(i8, 0, 0), endp);
            LLVMTypeRef i64p = LLVMPointerType(i64t, 0);
            LLVMValueRef fits = zan_icmp(g->builder, LLVMIntULE, len,
                LLVMConstInt(i64t, ZAN_STR_LEN_MASK, 0), "ma.fits");
            LLVMValueRef half = LLVMBuildSelect(g->builder, fits, len,
                LLVMConstInt(i64t, ZAN_STR_LEN_UNKNOWN, 0), "ma.half");
            LLVMValueRef word = LLVMBuildOr(g->builder,
                LLVMConstInt(i64t, ZAN_STRING_TAG << 32, 0), half, "ma.hdr");
            LLVMValueRef hdr_ptr = LLVMBuildGEP2(g->builder, i8, buf,
                &(LLVMValueRef){ LLVMConstInt(i64t, (uint64_t)ZAN_OBJ_SITE_OFF, 1) }, 1,
                "ma.hdrp");
            LLVMBuildStore(g->builder, word,
                LLVMBuildBitCast(g->builder, hdr_ptr, i64p, "ma.hdrip"));
            LLVMValueRef elem = LLVMBuildGEP2(g->builder, i8ptr, arr, &iv, 1, "ma.ep.slot");
            zan_store_fit(g, buf, elem);
            zan_store_fit(g, iv1, lp);
            LLVMBuildBr(g->builder, cond_bb);
            LLVMPositionBuilderAtEnd(g->builder, done_bb);
            zan_type_t *args_type = zan_binder_make_array_type(g->binder,
                g->binder->type_string);
            LLVMValueRef slot_a = emit_entry_alloca(g, i8ptrptr, "ma.slot");
            zan_store_fit(g, arr, slot_a);
            local_add(locals, param->param.name, slot_a, args_type);
            box_captured_parameter(g, locals, param, args_type, i8ptrptr, arr,
                                   method->method_decl.body);
            if (locals->vars[locals->count - 1].box_cell) {
                emit_rc_release_for_type(g, args_type, arr);
            } else {
                arc_own_local(g, locals);
            }
        }
    }

    g->current_fn_is_main = true;
    if (method->method_decl.body) {
        emit_stmt(g, method->method_decl.body, locals);
    }
    g->current_fn_is_main = false;

    if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder))) {
        emit_release_owned_locals(g, locals);
        emit_release_static_rc_fields(g, unit);
        LLVMBuildRet(g->builder, LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0));
    }
    emit_eh_terminate_setup(g, true);
    g->current_type_sym = NULL;
    g->current_fn_body = NULL;
}


