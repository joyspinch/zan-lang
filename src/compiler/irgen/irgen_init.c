zan_status_t zan_irgen_init(zan_irgen_t *g, zan_arena_t *arena,
                            zan_diag_t *diag, zan_binder_t *binder,
                            const char *module_name,
                            const char *target_triple,
                            bool target_is_windows, bool external_async_executor,
                            bool check_leaks, bool runtime_checks,
                            bool arc_guard, bool arc_net) {
    memset(g, 0, sizeof(*g));
    class_index_reset();
    s_current_irgen = g;
    g->arena = arena;
    g->diag = diag;
    g->binder = binder;
    g->runtime_checks = runtime_checks;
    g->arc_guard = arc_guard;
    g->arc_net = arc_net;
    g->check_leaks = check_leaks;
    if (target_triple && target_triple[0])
        snprintf(g->target_triple, sizeof(g->target_triple), "%s", target_triple);
    g->target_is_windows = target_is_windows;
    if (g->target_triple[0]) {
        g->target_is_ios = strstr(g->target_triple, "ios") != NULL;
        g->target_is_macos = (strstr(g->target_triple, "apple") != NULL
                             || strstr(g->target_triple, "darwin") != NULL)
                             && !g->target_is_ios;
        g->target_is_wasm = strstr(g->target_triple, "wasm") != NULL;
    } else {
#ifdef __APPLE__
        g->target_is_macos = true;
#endif
    }
    g->external_async_executor = external_async_executor;
    g->has_async_work = false;

    {
        static const unsigned char zan_obf_pad[16] = {
            0x5a, 0x67, 0xa3, 0x1c, 0xd9, 0x84, 0x2f, 0x70,
            0xbe, 0x11, 0x4d, 0xe6, 0x93, 0x28, 0xc5, 0x7a
        };
        uint64_t h = 0xcbf29ce484222325ULL;
        const char *seeds[2] = { module_name ? module_name : "",
                                 g->target_triple };
        for (int s = 0; s < 2; s++) {
            for (const char *p = seeds[s]; p && *p; p++) {
                h ^= (unsigned char)*p;
                h *= 0x100000001b3ULL;
            }
            h ^= 0x9e3779b97f4a7c15ULL;
            h *= 0x100000001b3ULL;
        }
        for (int i = 0; i < 16; i++) {
            uint64_t mix = h + (uint64_t)i * 0x9e3779b97f4a7c15ULL;
            mix ^= mix >> 29;
            mix *= 0xbf58476d1ce4e5b9ULL;
            mix ^= mix >> 32;
            g->obf_key[i] = (unsigned char)(zan_obf_pad[i] ^ (unsigned char)mix);
        }
    }
    g->catch_cleanups = NULL;
    g->catch_cleanup_count = 0;
    g->catch_cleanup_cap = 0;
    g->obfuscate_strings = false;
    g->obf_literals = NULL;
    g->obf_literal_count = 0;
    g->obf_literal_cap = 0;
    g->string_literals = NULL;
    g->string_literal_count = 0;
    g->string_literal_cap = 0;
    g->hide_diagnostics = false;
    g->site_map_path = NULL;
    g->site_map_fp = NULL;
    g->next_site_id = 0;
    g->site_records = NULL;
    g->site_record_count = 0;
    g->site_record_cap = 0;

    g->ctx = LLVMContextCreate();
    g->mod = LLVMModuleCreateWithNameInContext(module_name, g->ctx);
    g->builder = LLVMCreateBuilderInContext(g->ctx);

    g->emit_debug = false;
    g->di_builder = NULL;
    g->di_cu = NULL;
    g->di_files = NULL;
    g->di_file_cap = 0;
    g->di_cur_line = 0;
    g->di_cur_file = UINT32_MAX;

    g->src_file = module_name;

    g->g_live = LLVMAddGlobal(g->mod, LLVMInt64TypeInContext(g->ctx), "__zan_live");
    LLVMSetInitializer(g->g_live, LLVMConstInt(LLVMInt64TypeInContext(g->ctx), 0, 0));
    LLVMSetLinkage(g->g_live, LLVMInternalLinkage);

    /* site 0 留作哨兵：0 在运行时语义是"未跟踪对象"（refl_obj_type_fn
     * 的 site>0 守卫、rt_alloc 零初始化头），真实分配点从 1 起。曾经
     * 从 0 起数——主文件先于 stdlib 发射时，用户类的第一个分配点抢到
     * 0 号，该类所有对象的动态反射（site→meta 解析）被 site>0 守卫
     * 拒绝，SetFieldX/GetFieldX 静默失效（orm_extended 插桩构建
     * DetectColInt 全落兜底列，探针 pOff6/pOff11）。 */
    g->leak_site_count = 1;
    g->desc_hdr = !check_leaks;
    g->desc_gv_cap = 0;
    g->leak_site_cap = 256;
    {
        LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
        LLVMTypeRef i8p  = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
        if (!g->desc_hdr) {
            g->g_site_count = LLVMAddGlobal(g->mod, i64t, "__zan_site_count");
            LLVMSetInitializer(g->g_site_count, LLVMConstInt(i64t, 0, 0));
            LLVMSetLinkage(g->g_site_count, LLVMInternalLinkage);
            g->g_site_live = LLVMAddGlobal(g->mod, LLVMPointerType(i64t, 0), "__zan_site_live");
            LLVMSetInitializer(g->g_site_live, LLVMConstNull(LLVMPointerType(i64t, 0)));
            LLVMSetLinkage(g->g_site_live, LLVMInternalLinkage);
            g->g_site_names = LLVMAddGlobal(g->mod, LLVMPointerType(i8p, 0), "__zan_site_names");
            LLVMSetInitializer(g->g_site_names, LLVMConstNull(LLVMPointerType(i8p, 0)));
            LLVMSetLinkage(g->g_site_names, LLVMInternalLinkage);
            g->g_site_dtors = LLVMAddGlobal(g->mod, LLVMPointerType(i8p, 0), "__zan_site_dtors");
            LLVMSetInitializer(g->g_site_dtors, LLVMConstNull(LLVMPointerType(i8p, 0)));
            LLVMSetLinkage(g->g_site_dtors, LLVMInternalLinkage);
            g->g_site_tynames = LLVMAddGlobal(g->mod, LLVMPointerType(i8p, 0), "__zan_site_tynames");
            LLVMSetInitializer(g->g_site_tynames, LLVMConstNull(LLVMPointerType(i8p, 0)));
            LLVMSetLinkage(g->g_site_tynames, LLVMInternalLinkage);
	            g->g_site_meta = LLVMAddGlobal(g->mod, LLVMPointerType(i8p, 0), "__zan_site_meta");
	            LLVMSetInitializer(g->g_site_meta, LLVMConstNull(LLVMPointerType(i8p, 0)));
	            LLVMSetLinkage(g->g_site_meta, LLVMInternalLinkage);
	            g->g_site_field_graphs = LLVMAddGlobal(g->mod, LLVMPointerType(i8p, 0), "__zan_site_field_graphs");
	            LLVMSetInitializer(g->g_site_field_graphs, LLVMConstNull(LLVMPointerType(i8p, 0)));
	            LLVMSetLinkage(g->g_site_field_graphs, LLVMInternalLinkage);
	        }
        g->site_syms = (zan_symbol_t **)calloc(g->leak_site_cap, sizeof(zan_symbol_t *));
        g->site_coll = (int *)calloc(g->leak_site_cap, sizeof(int));
        g->site_coll_elem = (zan_type_t **)calloc(g->leak_site_cap, sizeof(zan_type_t *));
        g->site_inst = (zan_type_t **)calloc(g->leak_site_cap, sizeof(zan_type_t *));
        g->site_loc_file = (uint32_t *)calloc(g->leak_site_cap, sizeof(uint32_t));
        g->site_loc_line = (uint32_t *)calloc(g->leak_site_cap, sizeof(uint32_t));
    }

    LLVMTypeRef printf_args[] = { LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0) };
    LLVMTypeRef printf_type = LLVMFunctionType(
        LLVMInt32TypeInContext(g->ctx), printf_args, 1, 1 /* varargs */);
    LLVMValueRef printf_fn = LLVMAddFunction(g->mod, "printf", printf_type);
    g->fn_printf = printf_fn;
    g->printf_type = printf_type;

    LLVMTypeRef println_args[] = { LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0) };
    LLVMTypeRef println_type = LLVMFunctionType(
        LLVMVoidTypeInContext(g->ctx), println_args, 1, 0);
    g->rt_println = LLVMAddFunction(g->mod, "zan_rt_println", println_type);

    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, g->rt_println, "entry");
    LLVMPositionBuilderAtEnd(g->builder, entry);

    /* B-ID113：Windows 目标显式 \r\n（C# Environment.NewLine 语义）——配合
     * startup 二进制 stdout，CRT 文本模式翻译退场后 golden 字节不变 */
    LLVMValueRef fmt_str = zan_irgen_intern_string(g,
        g->target_is_windows ? "%s\r\n" : "%s\n");
    LLVMValueRef args[] = { fmt_str, LLVMGetParam(g->rt_println, 0) };
    zan_call2(g->builder, printf_type, printf_fn, args, 2, "");
    LLVMBuildRetVoid(g->builder);

    LLVMTypeRef pint_args[] = { LLVMInt64TypeInContext(g->ctx) };
    LLVMTypeRef pint_type = LLVMFunctionType(
        LLVMVoidTypeInContext(g->ctx), pint_args, 1, 0);
    g->rt_print_int = LLVMAddFunction(g->mod, "zan_rt_print_int", pint_type);

    LLVMBasicBlockRef pint_entry = LLVMAppendBasicBlockInContext(g->ctx, g->rt_print_int, "entry");
    LLVMPositionBuilderAtEnd(g->builder, pint_entry);
    LLVMValueRef int_fmt = zan_irgen_intern_string(g,
        g->target_is_windows ? "%lld\r\n" : "%lld\n");
    LLVMValueRef iargs[] = { int_fmt, LLVMGetParam(g->rt_print_int, 0) };
    zan_call2(g->builder, printf_type, printf_fn, iargs, 2, "");
    LLVMBuildRetVoid(g->builder);

    g->rt_print_uint = LLVMAddFunction(g->mod, "zan_rt_print_uint", pint_type);

    LLVMBasicBlockRef puint_entry = LLVMAppendBasicBlockInContext(g->ctx, g->rt_print_uint, "entry");
    LLVMPositionBuilderAtEnd(g->builder, puint_entry);
    LLVMValueRef uint_fmt = zan_irgen_intern_string(g,
        g->target_is_windows ? "%llu\r\n" : "%llu\n");
    LLVMValueRef uargs[] = { uint_fmt, LLVMGetParam(g->rt_print_uint, 0) };
    zan_call2(g->builder, printf_type, printf_fn, uargs, 2, "");
    LLVMBuildRetVoid(g->builder);

    LLVMTypeRef pdbl_args[] = { LLVMDoubleTypeInContext(g->ctx) };
    LLVMTypeRef pdbl_type = LLVMFunctionType(
        LLVMVoidTypeInContext(g->ctx), pdbl_args, 1, 0);
    g->rt_print_double = LLVMAddFunction(g->mod, "zan_rt_print_double", pdbl_type);

    LLVMBasicBlockRef pdbl_entry = LLVMAppendBasicBlockInContext(g->ctx, g->rt_print_double, "entry");
    LLVMPositionBuilderAtEnd(g->builder, pdbl_entry);
    {
        LLVMTypeRef i8p_t = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
        LLVMTypeRef i64_t = LLVMInt64TypeInContext(g->ctx);
        LLVMTypeRef ds_fn_ty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx),
            (LLVMTypeRef[]){ i8p_t, i64_t, LLVMDoubleTypeInContext(g->ctx) }, 3, 0);
        LLVMValueRef ds_fn = LLVMGetNamedFunction(g->mod, "zan_rt_dbl_str");
        if (!ds_fn) ds_fn = LLVMAddFunction(g->mod, "zan_rt_dbl_str", ds_fn_ty);
        LLVMValueRef ds_buf = LLVMBuildAlloca(g->builder,
            LLVMArrayType(LLVMInt8TypeInContext(g->ctx), 40), "pdbl.buf");
        LLVMValueRef ds_bufp = LLVMBuildBitCast(g->builder, ds_buf, i8p_t,
                                                "pdbl.bufp");
        LLVMValueRef ds_args[] = { ds_bufp,
            LLVMConstInt(i64_t, 40, 0), LLVMGetParam(g->rt_print_double, 0) };
        zan_call2(g->builder, ds_fn_ty, ds_fn, ds_args, 3, "");
        LLVMValueRef nl_fmt = zan_irgen_intern_string(g,
            g->target_is_windows ? "%s\r\n" : "%s\n");
        LLVMValueRef dargs[] = { nl_fmt, ds_bufp };
        zan_call2(g->builder, printf_type, printf_fn, dargs, 2, "");
    }
    LLVMBuildRetVoid(g->builder);

    LLVMTypeRef pflt_args[] = { LLVMFloatTypeInContext(g->ctx) };
    LLVMTypeRef pflt_type = LLVMFunctionType(
        LLVMVoidTypeInContext(g->ctx), pflt_args, 1, 0);
    g->rt_print_float = LLVMAddFunction(g->mod, "zan_rt_print_float", pflt_type);

    LLVMBasicBlockRef pflt_entry = LLVMAppendBasicBlockInContext(g->ctx, g->rt_print_float, "entry");
    LLVMPositionBuilderAtEnd(g->builder, pflt_entry);
    {
        LLVMTypeRef i8p_t = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
        LLVMTypeRef i64_t = LLVMInt64TypeInContext(g->ctx);
        LLVMTypeRef ds_fn_ty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx),
            (LLVMTypeRef[]){ i8p_t, i64_t, LLVMDoubleTypeInContext(g->ctx) }, 3, 0);
        LLVMValueRef ds_fn = LLVMGetNamedFunction(g->mod, "zan_rt_dbl_str");
        if (!ds_fn) ds_fn = LLVMAddFunction(g->mod, "zan_rt_dbl_str", ds_fn_ty);
        LLVMValueRef ds_buf = LLVMBuildAlloca(g->builder,
            LLVMArrayType(LLVMInt8TypeInContext(g->ctx), 40), "pflt.buf");
        LLVMValueRef ds_bufp = LLVMBuildBitCast(g->builder, ds_buf, i8p_t,
                                                "pflt.bufp");
        LLVMValueRef ext_arg = LLVMBuildFPExt(g->builder,
            LLVMGetParam(g->rt_print_float, 0), LLVMDoubleTypeInContext(g->ctx), "flt.ext");
        LLVMValueRef ds_args[] = { ds_bufp,
            LLVMConstInt(i64_t, 40ULL | 0x8000000000000000ULL, 0), ext_arg };
        zan_call2(g->builder, ds_fn_ty, ds_fn, ds_args, 3, "");
        LLVMValueRef nl_fmt = zan_irgen_intern_string(g,
            g->target_is_windows ? "%s\r\n" : "%s\n");
        LLVMValueRef dargs[] = { nl_fmt, ds_bufp };
        zan_call2(g->builder, printf_type, printf_fn, dargs, 2, "");
    }
    LLVMBuildRetVoid(g->builder);

    /* B-ID113：Windows 目标在 main 前把 stdout/stderr 设为二进制模式——
     * CRT 文本模式曾把 "\n" 翻译成 "\r\n"，CLI 工具「输出到管道」与
     * 「内部写文件」字节不一致（zanfmt --stdout f > f 整文件幻影 diff）。
     * fd 常量 1/2 直用免 FILE* 跨 CRT 形态取手；_O_BINARY = 0x8000。
     * POSIX/wasm 无文本模式翻译，不发射。 */
    if (g->target_is_windows) {
        LLVMTypeRef sm_i32 = LLVMInt32TypeInContext(g->ctx);
        LLVMTypeRef sm_ty = LLVMFunctionType(sm_i32,
            (LLVMTypeRef[]){ sm_i32, sm_i32 }, 2, 0);
        LLVMValueRef setmode = LLVMAddFunction(g->mod, "_setmode", sm_ty);
        LLVMTypeRef init_ty = LLVMFunctionType(
            LLVMVoidTypeInContext(g->ctx), NULL, 0, 0);
        LLVMValueRef init_fn = LLVMAddFunction(g->mod, "__zan_stdio_init", init_ty);
        LLVMBasicBlockRef sm_saved = LLVMGetInsertBlock(g->builder);
        LLVMBasicBlockRef sm_entry = LLVMAppendBasicBlockInContext(g->ctx, init_fn, "entry");
        LLVMPositionBuilderAtEnd(g->builder, sm_entry);
        LLVMValueRef obinary = LLVMConstInt(sm_i32, 0x8000, 0);
        LLVMValueRef sm_args1[] = { LLVMConstInt(sm_i32, 1, 0), obinary };
        LLVMValueRef sm_args2[] = { LLVMConstInt(sm_i32, 2, 0), obinary };
        zan_call2(g->builder, sm_ty, setmode, sm_args1, 2, "");
        zan_call2(g->builder, sm_ty, setmode, sm_args2, 2, "");
        LLVMBuildRetVoid(g->builder);
        LLVMPositionBuilderAtEnd(g->builder, sm_saved);
        LLVMValueRef ctor_fields[] = {
            LLVMConstInt(sm_i32, 65535, 0), init_fn, LLVMConstNull(
                LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0))
        };
        LLVMTypeRef ctor_ty = LLVMStructTypeInContext(g->ctx,
            (LLVMTypeRef[]){ sm_i32, LLVMTypeOf(init_fn),
                             LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0) }, 3, 0);
        LLVMValueRef ctor_c = LLVMConstStruct(ctor_fields, 3, 0);
        LLVMTypeRef ctor_arr = LLVMArrayType(LLVMTypeOf(ctor_c), 1);
        LLVMValueRef ctors = LLVMAddGlobal(g->mod, ctor_arr, "llvm.global_ctors");
        LLVMSetLinkage(ctors, LLVMAppendingLinkage);
        LLVMSetInitializer(ctors, LLVMConstArray(LLVMTypeOf(ctor_c), &ctor_c, 1));
    }

    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);

    LLVMTypeRef snprintf_args[] = { i8ptr, i64, i8ptr };
    LLVMTypeRef snprintf_type = LLVMFunctionType(i32, snprintf_args, 3, 1);
    g->fn_snprintf = LLVMAddFunction(g->mod, "snprintf", snprintf_type);

    /* void *malloc(size_t) */
    LLVMTypeRef malloc_args[] = { i64 };
    LLVMTypeRef malloc_type = LLVMFunctionType(i8ptr, malloc_args, 1, 0);
    g->fn_malloc = LLVMAddFunction(g->mod, "malloc", malloc_type);

    LLVMTypeRef free_args[] = { i8ptr };
    LLVMTypeRef free_type = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), free_args, 1, 0);
    g->fn_free = LLVMAddFunction(g->mod, "free", free_type);

    LLVMTypeRef exit_args[] = { i32 };
    g->exit_type = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), exit_args, 1, 0);
    g->fn_exit = LLVMAddFunction(g->mod, "exit", g->exit_type);

    if (g->check_leaks) {
        LLVMTypeRef void_fn_type = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), NULL, 0, 0);
        LLVMTypeRef void_fn_ptr = LLVMPointerType(void_fn_type, 0);
        LLVMTypeRef atexit_args[] = { void_fn_ptr };
        g->atexit_type = LLVMFunctionType(i32, atexit_args, 1, 0);
        g->fn_atexit = LLVMAddFunction(g->mod, "atexit", g->atexit_type);
    }

    /* void* realloc(void*, size_t) */
    LLVMTypeRef realloc_args[] = { i8ptr, i64 };
    LLVMTypeRef realloc_type = LLVMFunctionType(i8ptr, realloc_args, 2, 0);
    g->fn_realloc = LLVMAddFunction(g->mod, "realloc", realloc_type);

    LLVMTypeRef list_fields[] = { i64, i64, LLVMPointerType(i64, 0) };
    g->list_struct_type = LLVMStructCreateNamed(g->ctx, "List");
    LLVMStructSetBody(g->list_struct_type, list_fields, 3, 0);

    LLVMTypeRef span_fields[] = { i8ptr, i64 };
    g->span_struct_type = LLVMStructCreateNamed(g->ctx, "Span");
    LLVMStructSetBody(g->span_struct_type, span_fields, 2, 0);

    LLVMTypeRef dict_fields[] = { i64, i64, LLVMPointerType(i8ptr, 0), LLVMPointerType(i64, 0),
                                  LLVMPointerType(i64, 0), i64, i64, i64 };
    g->dict_struct_type = LLVMStructCreateNamed(g->ctx, "Dict");
    LLVMStructSetBody(g->dict_struct_type, dict_fields, 8, 0);

    g->task_struct_type = LLVMStructCreateNamed(g->ctx, "Task");
    LLVMTypeRef task_fields[] = { i64, i64, i64 };
    LLVMStructSetBody(g->task_struct_type, task_fields, 3, 0);

    LLVMTypeRef sb_fields[] = { i64, i64, i8ptr };
    g->sb_struct_type = LLVMStructCreateNamed(g->ctx, "StringBuilder");
    LLVMStructSetBody(g->sb_struct_type, sb_fields, 3, 0);

    LLVMTypeRef dec_fields[] = {
        LLVMInt32TypeInContext(g->ctx),
        LLVMInt32TypeInContext(g->ctx),
        i64
    };
    g->decimal_struct_type = LLVMStructCreateNamed(g->ctx, "Decimal");
    LLVMStructSetBody(g->decimal_struct_type, dec_fields, 3, 0);

    LLVMTypeRef co_step_args[] = { i8ptr };
    g->co_step_type = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), co_step_args, 1, 0);
    g->co_step_ptr = LLVMPointerType(g->co_step_type, 0);
    LLVMTypeRef co_ready_args[] = { i8ptr, g->co_step_ptr };
    g->rt_co_ready_type = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), co_ready_args, 2, 0);
    g->rt_co_ready = LLVMAddFunction(g->mod, "zan_co_ready", g->rt_co_ready_type);
    g->rt_co_poll_type = LLVMFunctionType(LLVMInt32TypeInContext(g->ctx), NULL, 0, 0);
    g->rt_co_poll = LLVMAddFunction(g->mod, "zan_co_poll", g->rt_co_poll_type);
    g->rt_co_frame_alloc_type = LLVMFunctionType(i8ptr,
                                                 (LLVMTypeRef[]){ LLVMInt64TypeInContext(g->ctx) }, 1, 0);
    g->rt_co_frame_alloc = LLVMAddFunction(g->mod, "__zan_co_frame_alloc",
                                           g->rt_co_frame_alloc_type);
    g->rt_co_frame_free_type = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx),
                                                (LLVMTypeRef[]){ i8ptr }, 1, 0);
    g->rt_co_frame_free = LLVMAddFunction(g->mod, "__zan_co_frame_free",
                                          g->rt_co_frame_free_type);
    g->rt_co_sched_init_type = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), NULL, 0, 0);
    g->rt_co_sched_init = LLVMAddFunction(g->mod, "zan_co_sched_init", g->rt_co_sched_init_type);
    g->rt_co_sched_run_type = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), NULL, 0, 0);
    g->rt_co_sched_run = LLVMAddFunction(g->mod, "zan_co_sched_run", g->rt_co_sched_run_type);
    g->rt_co_sched_run_until_type = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx),
        (LLVMTypeRef[]){ LLVMPointerType(LLVMInt32TypeInContext(g->ctx), 0) }, 1, 0);
    g->rt_co_sched_run_until = LLVMAddFunction(g->mod, "zan_co_sched_run_until",
        g->rt_co_sched_run_until_type);
    {
        LLVMTypeRef i64d = LLVMInt64TypeInContext(g->ctx);
        LLVMTypeRef delay_args[] = { i64d, i8ptr, g->co_step_ptr };
        g->rt_co_delay_type = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), delay_args, 3, 0);
        g->rt_co_delay = LLVMAddFunction(g->mod, "zan_co_delay", g->rt_co_delay_type);
    }

    {
        LLVMTypeRef i64d = LLVMInt64TypeInContext(g->ctx);
        LLVMTypeRef i32d = LLVMInt32TypeInContext(g->ctx);
        LLVMTypeRef wc_args[] = { i64d, i32d, i8ptr, g->co_step_ptr };
        g->rt_io_wait_co_type = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), wc_args, 4, 0);
        g->rt_io_wait_co = LLVMAddFunction(g->mod, "zan_io_wait_co", g->rt_io_wait_co_type);
        LLVMTypeRef i64ptr = LLVMPointerType(i64d, 0);
        LLVMTypeRef rc_args[] = { i64d, i8ptr, i32d, i8ptr, g->co_step_ptr, i64ptr };
        g->rt_io_recv_co_type = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), rc_args, 6, 0);
        g->rt_io_recv_co = LLVMAddFunction(g->mod, "zan_io_recv_co", g->rt_io_recv_co_type);
        LLVMTypeRef rtc_args[] = { i64d, i8ptr, i32d, i64d, i8ptr, g->co_step_ptr, i64ptr };
        g->rt_io_recv_to_co_type = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), rtc_args, 7, 0);
        g->rt_io_recv_to_co = LLVMAddFunction(g->mod, "zan_io_recv_to_co", g->rt_io_recv_to_co_type);
        LLVMTypeRef ac_args[] = { i64d, i8ptr, g->co_step_ptr, i64ptr };
        g->rt_io_accept_co_type = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), ac_args, 4, 0);
        g->rt_io_accept_co = LLVMAddFunction(g->mod, "zan_io_accept_co", g->rt_io_accept_co_type);
        LLVMTypeRef i32ptr = LLVMPointerType(i32d, 0);
        LLVMTypeRef dc_args[] = { i8ptr, i8ptr, g->co_step_ptr, i32ptr };
        g->rt_io_resolve_co_type = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), dc_args, 4, 0);
        g->rt_io_resolve_co = LLVMAddFunction(g->mod, "zan_io_resolve_co", g->rt_io_resolve_co_type);
        LLVMTypeRef sa_args[] = { i8ptr, i32d, i8ptr, i32d, i8ptr,
                                  g->co_step_ptr, i32ptr };
        g->rt_io_resolve_sa_co_type = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), sa_args, 7, 0);
        g->rt_io_resolve_sa_co = LLVMAddFunction(g->mod, "zan_io_resolve_sa_co",
            g->rt_io_resolve_sa_co_type);
        LLVMTypeRef fnptr = i8ptr;
        LLVMTypeRef argc_t = i32d;
        LLVMTypeRef blocking_args[] = { fnptr, argc_t, i64d, i64d, i64d,
                                        i64d, i8ptr, g->co_step_ptr,
                                        LLVMPointerType(i64d, 0) };
        g->rt_blocking_co_type = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx),
                                                   blocking_args, 9, 0);
        g->rt_blocking_co = LLVMAddFunction(g->mod, "zan_rt_blocking_co",
                                            g->rt_blocking_co_type);
        LLVMTypeRef pump_args[] = { i64d };
        g->rt_io_pump_timeout_type = LLVMFunctionType(i32d, pump_args, 1, 0);
        g->rt_io_pump_timeout = LLVMAddFunction(g->mod, "zan_io_pump_timeout",
            g->rt_io_pump_timeout_type);
        LLVMSetLinkage(g->rt_io_pump_timeout,
            g->external_async_executor ? LLVMExternalLinkage : LLVMExternalWeakLinkage);

        g->rt_io_has_pending_type = LLVMFunctionType(i32d, NULL, 0, 0);
        g->rt_io_has_pending = LLVMAddFunction(g->mod, "zan_io_has_pending",
            g->rt_io_has_pending_type);
        LLVMSetLinkage(g->rt_io_has_pending,
            g->external_async_executor ? LLVMExternalLinkage : LLVMExternalWeakLinkage);

        LLVMTypeRef legacy_pump_type = LLVMFunctionType(i32d, NULL, 0, 0);
        LLVMValueRef legacy_pump = LLVMAddFunction(g->mod, "zan_io_pump",
            legacy_pump_type);
        LLVMSetLinkage(legacy_pump, LLVMWeakAnyLinkage);
        LLVMBasicBlockRef legacy_entry = LLVMAppendBasicBlockInContext(g->ctx,
            legacy_pump, "entry");
        LLVMPositionBuilderAtEnd(g->builder, legacy_entry);
        LLVMValueRef legacy_woke = zan_call2(g->builder,
            g->rt_io_pump_timeout_type, g->rt_io_pump_timeout,
            (LLVMValueRef[]){ LLVMConstInt(i64d, (uint64_t)-1, 1) }, 1, "woke");
        LLVMBuildRet(g->builder, legacy_woke);
    }
    g->uses_socket_async = false;

    if (!g->external_async_executor) {
        LLVMTypeRef voidt = LLVMVoidTypeInContext(g->ctx);
        LLVMTypeRef node_fields[] = { i8ptr /* next */, i8ptr /*frame*/, g->co_step_ptr /*step*/ };
        LLVMTypeRef node_ty = LLVMStructCreateNamed(g->ctx, "zan.co.node");
        LLVMStructSetBody(node_ty, node_fields, 3, 0);
        LLVMValueRef g_head = LLVMAddGlobal(g->mod, i8ptr, "__zan_co_head");
        LLVMSetInitializer(g_head, LLVMConstNull(i8ptr));
        LLVMSetLinkage(g_head, LLVMInternalLinkage);
        LLVMValueRef g_tail = LLVMAddGlobal(g->mod, i8ptr, "__zan_co_tail");
        LLVMSetInitializer(g_tail, LLVMConstNull(i8ptr));
        LLVMSetLinkage(g_tail, LLVMInternalLinkage);
        LLVMValueRef g_nodes = LLVMAddGlobal(g->mod, i8ptr, "__zan_co_nodes");
        LLVMSetInitializer(g_nodes, LLVMConstNull(i8ptr));
        LLVMSetLinkage(g_nodes, LLVMInternalLinkage);
        LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);

        LLVMValueRef g_slice_start = LLVMAddGlobal(g->mod, i64t, "__zan_co_slice_start");
        LLVMSetInitializer(g_slice_start, LLVMConstInt(i64t, 0, 0));
        LLVMValueRef g_poll_tick = LLVMAddGlobal(g->mod, i64t, "__zan_co_poll_tick");
        LLVMSetInitializer(g_poll_tick, LLVMConstInt(i64t, 0, 0));
        LLVMSetLinkage(g_slice_start, LLVMInternalLinkage);
        LLVMValueRef g_quantum = LLVMAddGlobal(g->mod, i64t, "__zan_co_quantum_us");
        LLVMSetInitializer(g_quantum, LLVMConstInt(i64t, 0, 0));
        LLVMSetLinkage(g_quantum, LLVMInternalLinkage);
        LLVMTypeRef now_type = LLVMFunctionType(i64t, NULL, 0, 0);
        LLVMValueRef co_quantum = LLVMAddFunction(g->mod, "zan_co_quantum_ms", now_type);
        LLVMValueRef precise_now = LLVMAddFunction(g->mod, "zan_co_precise_us", now_type);

        LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
        LLVMTypeRef timer_reset_type = LLVMFunctionType(voidt, NULL, 0, 0);
        LLVMValueRef timer_reset = LLVMAddFunction(g->mod, "zan_timer_runtime_reset", timer_reset_type);
        LLVMTypeRef timer_hook_type = LLVMFunctionType(voidt,
            (LLVMTypeRef[]){ g->rt_co_ready_type ? LLVMPointerType(g->rt_co_ready_type, 0) : i8ptr }, 1, 0);
        LLVMValueRef timer_set_hook = LLVMAddFunction(g->mod, "zan_timer_set_ready_hook", timer_hook_type);
        LLVMTypeRef timer_delay_type = LLVMFunctionType(voidt,
            (LLVMTypeRef[]){ i64t, i8ptr, g->co_step_ptr }, 3, 0);
        LLVMValueRef timer_delay = LLVMAddFunction(g->mod, "zan_timer_delay", timer_delay_type);
        LLVMTypeRef timer_next_type = LLVMFunctionType(i64t, NULL, 0, 0);
        LLVMValueRef timer_next = LLVMAddFunction(g->mod, "zan_timer_next_timeout", timer_next_type);
        LLVMValueRef timer_dispatch = LLVMAddFunction(g->mod, "zan_timer_dispatch_due", timer_next_type);

        LLVMTypeRef sleep_args[] = { i32t };
        LLVMTypeRef sleep_type = LLVMFunctionType(voidt, sleep_args, 1, 0);
        LLVMValueRef fn_sleep = NULL;
        LLVMTypeRef poll_args[] = { i8ptr, i64t, i32t };
        LLVMTypeRef poll_type = LLVMFunctionType(i32t, poll_args, 3, 0);
        LLVMValueRef fn_poll = NULL;
        if (g->target_is_windows)
            fn_sleep = LLVMAddFunction(g->mod, "Sleep", sleep_type);
        else
            fn_poll = LLVMAddFunction(g->mod, "poll", poll_type);

        {
            LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx,
                g->rt_io_pump_timeout, "entry");
            LLVMBasicBlockRef sleep = LLVMAppendBasicBlockInContext(g->ctx,
                g->rt_io_pump_timeout, "sleep");
            LLVMBasicBlockRef ret = LLVMAppendBasicBlockInContext(g->ctx,
                g->rt_io_pump_timeout, "ret");
            LLVMPositionBuilderAtEnd(g->builder, entry);
            LLVMValueRef timeout = LLVMGetParam(g->rt_io_pump_timeout, 0);
            LLVMValueRef positive = zan_icmp(g->builder, LLVMIntSGT, timeout,
                LLVMConstInt(i64t, 0, 0), "positive");
            LLVMBuildCondBr(g->builder, positive, sleep, ret);

            LLVMPositionBuilderAtEnd(g->builder, sleep);
            LLVMValueRef timeout32 = LLVMBuildTrunc(g->builder, timeout, i32t, "timeout32");
            if (g->target_is_windows)
                zan_call2(g->builder, sleep_type, fn_sleep,
                    (LLVMValueRef[]){ timeout32 }, 1, "");
            else
                zan_call2(g->builder, poll_type, fn_poll,
                    (LLVMValueRef[]){ LLVMConstNull(i8ptr), LLVMConstInt(i64t, 0, 0), timeout32 }, 3, "");
            LLVMBuildBr(g->builder, ret);

            LLVMPositionBuilderAtEnd(g->builder, ret);
            LLVMBuildRet(g->builder, LLVMConstInt(i32t, 0, 0));
        }

        {
            LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx,
                g->rt_io_has_pending, "entry");
            LLVMPositionBuilderAtEnd(g->builder, entry);
            LLVMBuildRet(g->builder, LLVMConstInt(i32t, 0, 0));
        }

        {
            LLVMBasicBlockRef bb = LLVMAppendBasicBlockInContext(g->ctx, g->rt_co_sched_init, "entry");
            LLVMPositionBuilderAtEnd(g->builder, bb);
            LLVMBuildStore(g->builder, LLVMConstNull(i8ptr), g_head);
            LLVMBuildStore(g->builder, LLVMConstNull(i8ptr), g_tail);
            LLVMValueRef q = zan_call2(g->builder, now_type, co_quantum, NULL, 0, "q");
            q = LLVMBuildMul(g->builder, q, LLVMConstInt(i64t, 1000, 0), "q.us");
            LLVMBuildStore(g->builder, q, g_quantum);
            LLVMValueRef t0 = zan_call2(g->builder, now_type, precise_now, NULL, 0, "t0");
            LLVMBuildStore(g->builder, t0, g_slice_start);
            zan_call2(g->builder, timer_reset_type, timer_reset, NULL, 0, "");
            zan_call2(g->builder, timer_hook_type, timer_set_hook,
                (LLVMValueRef[]){ g->rt_co_ready }, 1, "");
            LLVMBuildRetVoid(g->builder);
        }

        {
            LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, g->rt_co_poll, "entry");
            LLVMBasicBlockRef check = LLVMAppendBasicBlockInContext(g->ctx, g->rt_co_poll, "check");
            LLVMBasicBlockRef gate  = LLVMAppendBasicBlockInContext(g->ctx, g->rt_co_poll, "gate");
            LLVMBasicBlockRef yes   = LLVMAppendBasicBlockInContext(g->ctx, g->rt_co_poll, "yes");
            LLVMBasicBlockRef no    = LLVMAppendBasicBlockInContext(g->ctx, g->rt_co_poll, "no");
            LLVMPositionBuilderAtEnd(g->builder, entry);
            LLVMValueRef q = LLVMBuildLoad2(g->builder, i64t, g_quantum, "q");
            LLVMValueRef enabled = zan_icmp(g->builder, LLVMIntSGT, q,
                LLVMConstInt(i64t, 0, 0), "q.on");
            LLVMBuildCondBr(g->builder, enabled, check, no);

            LLVMPositionBuilderAtEnd(g->builder, check);
            LLVMValueRef tick = LLVMBuildLoad2(g->builder, i64t, g_poll_tick, "tick");
            LLVMValueRef tick1 = LLVMBuildAdd(g->builder, tick,
                LLVMConstInt(i64t, 1, 0), "tick1");
            LLVMBuildStore(g->builder, tick1, g_poll_tick);
            LLVMValueRef hit = zan_icmp(g->builder, LLVMIntEQ,
                LLVMBuildAnd(g->builder, tick1, LLVMConstInt(i64t, 255, 0), "tick.lo"),
                LLVMConstInt(i64t, 0, 0), "gate.hit");
            LLVMBuildCondBr(g->builder, hit, gate, no);

            LLVMPositionBuilderAtEnd(g->builder, gate);
            LLVMValueRef now = zan_call2(g->builder, now_type, precise_now, NULL, 0, "now");
            LLVMValueRef start = LLVMBuildLoad2(g->builder, i64t, g_slice_start, "start");
            LLVMValueRef elapsed = LLVMBuildSub(g->builder, now, start, "elapsed");
            LLVMValueRef over = zan_icmp(g->builder, LLVMIntSGE, elapsed, q, "over");
            LLVMBuildCondBr(g->builder, over, yes, no);

            LLVMPositionBuilderAtEnd(g->builder, yes);
            LLVMBuildRet(g->builder, LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 1, 0));
            LLVMPositionBuilderAtEnd(g->builder, no);
            LLVMBuildRet(g->builder, LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0));
        }

        {
            LLVMBasicBlockRef bb = LLVMAppendBasicBlockInContext(g->ctx, g->rt_co_delay, "entry");
            LLVMPositionBuilderAtEnd(g->builder, bb);
            zan_call2(g->builder, timer_delay_type, timer_delay,
                (LLVMValueRef[]){ LLVMGetParam(g->rt_co_delay, 0),
                                  LLVMGetParam(g->rt_co_delay, 1),
                                  LLVMGetParam(g->rt_co_delay, 2) }, 3, "");
            LLVMBuildRetVoid(g->builder);
        }

        {
            LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, g->rt_co_ready, "entry");
            LLVMBasicBlockRef cont  = LLVMAppendBasicBlockInContext(g->ctx, g->rt_co_ready, "cont");
            LLVMBasicBlockRef empty = LLVMAppendBasicBlockInContext(g->ctx, g->rt_co_ready, "empty");
            LLVMBasicBlockRef nonempty = LLVMAppendBasicBlockInContext(g->ctx, g->rt_co_ready, "append");
            LLVMBasicBlockRef done  = LLVMAppendBasicBlockInContext(g->ctx, g->rt_co_ready, "done");
            LLVMBasicBlockRef ret   = LLVMAppendBasicBlockInContext(g->ctx, g->rt_co_ready, "ret");
            LLVMPositionBuilderAtEnd(g->builder, entry);
            LLVMValueRef frame = LLVMGetParam(g->rt_co_ready, 0);
            LLVMValueRef step  = LLVMGetParam(g->rt_co_ready, 1);
            LLVMValueRef step_null = zan_icmp(g->builder, LLVMIntEQ, step,
                LLVMConstNull(g->co_step_ptr), "step.null");
            LLVMBuildCondBr(g->builder, step_null, ret, cont);

            LLVMPositionBuilderAtEnd(g->builder, cont);
            LLVMBasicBlockRef reuse_bb = LLVMAppendBasicBlockInContext(g->ctx,
                g->rt_co_ready, "node.reuse");
            LLVMBasicBlockRef alloc_bb = LLVMAppendBasicBlockInContext(g->ctx,
                g->rt_co_ready, "node.alloc");
            LLVMBasicBlockRef have_bb = LLVMAppendBasicBlockInContext(g->ctx,
                g->rt_co_ready, "node.have");
            LLVMValueRef spare = LLVMBuildLoad2(g->builder, i8ptr, g_nodes, "spare");
            LLVMValueRef has_spare = zan_icmp(g->builder, LLVMIntNE, spare,
                LLVMConstNull(i8ptr), "has.spare");
            LLVMBuildCondBr(g->builder, has_spare, reuse_bb, alloc_bb);

            LLVMPositionBuilderAtEnd(g->builder, reuse_bb);
            LLVMValueRef spare_next = LLVMBuildLoad2(g->builder, i8ptr,
                LLVMBuildStructGEP2(g->builder, node_ty, spare, 0, "spare.next.p"),
                "spare.next");
            LLVMBuildStore(g->builder, spare_next, g_nodes);
            LLVMBuildBr(g->builder, have_bb);

            LLVMPositionBuilderAtEnd(g->builder, alloc_bb);
            LLVMValueRef fresh = zan_call2(g->builder, malloc_type, g->fn_malloc,
                (LLVMValueRef[]){ LLVMSizeOf(node_ty) }, 1, "node");
            zan_irgen_emit_oom_check(g, g->rt_co_ready, fresh);
            LLVMBasicBlockRef fresh_bb = LLVMGetInsertBlock(g->builder);
            LLVMBuildBr(g->builder, have_bb);

            LLVMPositionBuilderAtEnd(g->builder, have_bb);
            LLVMValueRef node = LLVMBuildPhi(g->builder, i8ptr, "node");
            LLVMAddIncoming(node, (LLVMValueRef[]){ spare, fresh },
                (LLVMBasicBlockRef[]){ reuse_bb, fresh_bb }, 2);
            LLVMValueRef nnext = LLVMBuildStructGEP2(g->builder, node_ty, node, 0, "n.next");
            LLVMBuildStore(g->builder, LLVMConstNull(i8ptr), nnext);
            LLVMValueRef nframe = LLVMBuildStructGEP2(g->builder, node_ty, node, 1, "n.frame");
            LLVMBuildStore(g->builder, frame, nframe);
            LLVMValueRef nstep = LLVMBuildStructGEP2(g->builder, node_ty, node, 2, "n.step");
            LLVMBuildStore(g->builder, step, nstep);
            LLVMValueRef tail = LLVMBuildLoad2(g->builder, i8ptr, g_tail, "tail");
            LLVMValueRef is_empty = zan_icmp(g->builder, LLVMIntEQ, tail,
                LLVMConstNull(i8ptr), "q.empty");
            LLVMBuildCondBr(g->builder, is_empty, empty, nonempty);

            LLVMPositionBuilderAtEnd(g->builder, empty);
            LLVMBuildStore(g->builder, node, g_head);
            LLVMBuildBr(g->builder, done);

            LLVMPositionBuilderAtEnd(g->builder, nonempty);
            LLVMValueRef tail_next = LLVMBuildStructGEP2(g->builder, node_ty, tail, 0, "tail.next");
            LLVMBuildStore(g->builder, node, tail_next);
            LLVMBuildBr(g->builder, done);

            LLVMPositionBuilderAtEnd(g->builder, done);
            LLVMBuildStore(g->builder, node, g_tail);
            LLVMBuildBr(g->builder, ret);

            LLVMPositionBuilderAtEnd(g->builder, ret);
            LLVMBuildRetVoid(g->builder);
        }

        {
            LLVMValueRef runfn = g->rt_co_sched_run_until;
            LLVMTypeRef i32p = LLVMPointerType(i32t, 0);
            LLVMBasicBlockRef entry_bb = LLVMAppendBasicBlockInContext(g->ctx, runfn, "entry");
            LLVMBasicBlockRef head_bb = LLVMAppendBasicBlockInContext(g->ctx, runfn, "loop");
            LLVMBasicBlockRef chk_bb = LLVMAppendBasicBlockInContext(g->ctx, runfn, "done.chk");
            LLVMBasicBlockRef pump_bb = LLVMAppendBasicBlockInContext(g->ctx, runfn, "pump");
            LLVMBasicBlockRef body_bb = LLVMAppendBasicBlockInContext(g->ctx, runfn, "body");
            LLVMBasicBlockRef last_bb = LLVMAppendBasicBlockInContext(g->ctx, runfn, "last");
            LLVMBasicBlockRef after_bb = LLVMAppendBasicBlockInContext(g->ctx, runfn, "after");
            LLVMBasicBlockRef timers_bb = LLVMAppendBasicBlockInContext(g->ctx, runfn, "timers");
            LLVMBasicBlockRef wait_timer = LLVMAppendBasicBlockInContext(g->ctx, runfn, "timer.wait");
            LLVMBasicBlockRef io_bb = LLVMAppendBasicBlockInContext(g->ctx, runfn, "io.pump");
            LLVMBasicBlockRef exit_bb = LLVMAppendBasicBlockInContext(g->ctx, runfn, "exit");
            LLVMPositionBuilderAtEnd(g->builder, entry_bb);
            LLVMValueRef doneptr = LLVMGetParam(runfn, 0);
            LLVMBuildBr(g->builder, head_bb);

            LLVMPositionBuilderAtEnd(g->builder, head_bb);
            LLVMValueRef no_flag = zan_icmp(g->builder, LLVMIntEQ, doneptr,
                LLVMConstNull(i32p), "done.noflag");
            LLVMBuildCondBr(g->builder, no_flag, pump_bb, chk_bb);

            LLVMPositionBuilderAtEnd(g->builder, chk_bb);
            LLVMValueRef dflag = LLVMBuildLoad2(g->builder, i32t, doneptr, "done.flag");
            LLVMValueRef is_done = zan_icmp(g->builder, LLVMIntNE, dflag,
                LLVMConstInt(i32t, 0, 0), "done.set");
            LLVMBuildCondBr(g->builder, is_done, exit_bb, pump_bb);

            LLVMPositionBuilderAtEnd(g->builder, pump_bb);
            LLVMValueRef head = LLVMBuildLoad2(g->builder, i8ptr, g_head, "head");
            LLVMValueRef empty = zan_icmp(g->builder, LLVMIntEQ, head,
                LLVMConstNull(i8ptr), "q.empty");
            LLVMBuildCondBr(g->builder, empty, timers_bb, body_bb);

            LLVMPositionBuilderAtEnd(g->builder, body_bb);
            LLVMValueRef fr_ptr = LLVMBuildStructGEP2(g->builder, node_ty, head, 1, "n.frame");
            LLVMValueRef fr = LLVMBuildLoad2(g->builder, i8ptr, fr_ptr, "frame");
            LLVMValueRef st_ptr = LLVMBuildStructGEP2(g->builder, node_ty, head, 2, "n.step");
            LLVMValueRef st = LLVMBuildLoad2(g->builder, g->co_step_ptr, st_ptr, "step");
            LLVMValueRef nx_ptr = LLVMBuildStructGEP2(g->builder, node_ty, head, 0, "n.next");
            LLVMValueRef nx = LLVMBuildLoad2(g->builder, i8ptr, nx_ptr, "next");
            LLVMBuildStore(g->builder, nx, g_head);
            LLVMValueRef is_last = zan_icmp(g->builder, LLVMIntEQ, nx,
                LLVMConstNull(i8ptr), "is.last");
            LLVMBuildCondBr(g->builder, is_last, last_bb, after_bb);

            LLVMPositionBuilderAtEnd(g->builder, last_bb);
            LLVMBuildStore(g->builder, LLVMConstNull(i8ptr), g_tail);
            LLVMBuildBr(g->builder, after_bb);

            LLVMPositionBuilderAtEnd(g->builder, after_bb);
            LLVMBuildStore(g->builder,
                LLVMBuildLoad2(g->builder, i8ptr, g_nodes, "nodes.head"),
                LLVMBuildStructGEP2(g->builder, node_ty, head, 0, "n.recycle"));
            LLVMBuildStore(g->builder, head, g_nodes);
            (void)zan_call2(g->builder, timer_next_type, timer_dispatch,
                NULL, 0, "due.busy");
            LLVMValueRef slice_now = zan_call2(g->builder, now_type, precise_now,
                NULL, 0, "slice.now");
            LLVMBuildStore(g->builder, slice_now, g_slice_start);
            zan_call2(g->builder, g->co_step_type, st,
                (LLVMValueRef[]){ fr }, 1, "");
            LLVMBuildBr(g->builder, head_bb);

            LLVMPositionBuilderAtEnd(g->builder, timers_bb);
            LLVMValueRef dispatched = zan_call2(g->builder, timer_next_type,
                timer_dispatch, NULL, 0, "timer.dispatched");
            LLVMValueRef has_due = zan_icmp(g->builder, LLVMIntSGT, dispatched,
                LLVMConstInt(i64t, 0, 0), "timer.has_due");
            LLVMBuildCondBr(g->builder, has_due, head_bb, wait_timer);

            LLVMPositionBuilderAtEnd(g->builder, wait_timer);
            LLVMValueRef timeout = zan_call2(g->builder, timer_next_type,
                timer_next, NULL, 0, "timer.timeout");
            LLVMValueRef has_timer = zan_icmp(g->builder, LLVMIntSGE, timeout,
                LLVMConstInt(i64t, 0, 0), "timer.pending");
            LLVMBuildBr(g->builder, io_bb);

            LLVMPositionBuilderAtEnd(g->builder, io_bb);
            LLVMValueRef woke = zan_call2(g->builder,
                g->rt_io_pump_timeout_type, g->rt_io_pump_timeout,
                (LLVMValueRef[]){ timeout }, 1, "woke");
            LLVMValueRef more = zan_icmp(g->builder, LLVMIntSGT, woke,
                LLVMConstInt(i64t, 0, 0), "io.more");
            LLVMValueRef pend = zan_call2(g->builder,
                g->rt_io_has_pending_type, g->rt_io_has_pending, NULL, 0,
                "io.pending");
            LLVMValueRef has_pending = zan_icmp(g->builder, LLVMIntSGT, pend,
                LLVMConstInt(i64t, 0, 0), "io.has_pending");
            LLVMValueRef cont_timer = LLVMBuildOr(g->builder, more, has_timer,
                "sched.more");
            LLVMValueRef continue_run = LLVMBuildOr(g->builder, cont_timer,
                has_pending, "sched.more2");
            LLVMBuildCondBr(g->builder, continue_run, head_bb, exit_bb);

            LLVMPositionBuilderAtEnd(g->builder, exit_bb);
            LLVMBuildRetVoid(g->builder);
        }

        {
            LLVMBasicBlockRef bb = LLVMAppendBasicBlockInContext(g->ctx,
                g->rt_co_sched_run, "entry");
            LLVMPositionBuilderAtEnd(g->builder, bb);
            zan_call2(g->builder, g->rt_co_sched_run_until_type,
                g->rt_co_sched_run_until,
                (LLVMValueRef[]){ LLVMConstNull(LLVMPointerType(i32t, 0)) }, 1, "");
            LLVMBuildRetVoid(g->builder);
        }
        (void)voidt;
    }
    LLVMTypeRef co_hdr_fields[] = {
        i64, g->co_step_ptr,   /* SCHED, SCHED_STEP: scheduler-owned */
        LLVMInt32TypeInContext(g->ctx), LLVMInt32TypeInContext(g->ctx),
        i8ptr, g->co_step_ptr, i64,
        g->co_step_ptr, LLVMInt32TypeInContext(g->ctx),
        g->co_step_ptr,
        i8ptr, i8ptr, LLVMInt32TypeInContext(g->ctx),
        LLVMInt32TypeInContext(g->ctx),
        i8ptr,
        i8ptr
    };
    g->co_header_type = LLVMStructCreateNamed(g->ctx, "zan.co.header");
    LLVMStructSetBody(g->co_header_type, co_hdr_fields, 16, 0);
    g->current_async_frame = NULL;
    g->current_async_frame_type = NULL;
    g->current_async_resume_fn = NULL;
    g->current_async_ret_type = NULL;
    g->current_async_switch = NULL;
    g->current_async_next_state = 1;
    g->current_async_sub_base = 0;
    g->current_async_sub_next = 0;
    g->current_async_rval_slot = -1;
    g->current_async_slots = NULL;
    g->current_async_slot_count = 0;
    g->current_async_sub_rethrow_bb = NULL;
    g->current_async_sub_rethrow_phi_sub = NULL;
    g->current_async_sub_rethrow_phi_ev = NULL;
    g->current_async_sub_slot_ptr = NULL;

    LLVMTypeRef strcmp_args[] = { i8ptr, i8ptr };
    LLVMTypeRef strcmp_type = LLVMFunctionType(LLVMInt32TypeInContext(g->ctx), strcmp_args, 2, 0);
    g->fn_strcmp = LLVMAddFunction(g->mod, "strcmp", strcmp_type);

    LLVMTypeRef strrchr_args[] = { i8ptr, LLVMInt32TypeInContext(g->ctx) };
    LLVMTypeRef strrchr_type = LLVMFunctionType(i8ptr, strrchr_args, 2, 0);
    LLVMAddFunction(g->mod, "strrchr", strrchr_type);

    LLVMTypeRef strlen_args[] = { i8ptr };
    LLVMTypeRef strlen_type = LLVMFunctionType(i64, strlen_args, 1, 0);
    g->fn_strlen = LLVMAddFunction(g->mod, "strlen", strlen_type);

    LLVMTypeRef strcpy_args[] = { i8ptr, i8ptr };
    LLVMTypeRef strcpy_type = LLVMFunctionType(i8ptr, strcpy_args, 2, 0);
    g->fn_strcpy = LLVMAddFunction(g->mod, "strcpy", strcpy_type);

    LLVMTypeRef strcat_args[] = { i8ptr, i8ptr };
    LLVMTypeRef strcat_type = LLVMFunctionType(i8ptr, strcat_args, 2, 0);
    g->fn_strcat = LLVMAddFunction(g->mod, "strcat", strcat_type);

    #include "irgen_init_rt.c"
    return ZAN_OK;
}

void zan_irgen_release_llvm(zan_irgen_t *g) {
    if (!g) return;
    if (s_current_irgen == g) s_current_irgen = NULL;
    if (g_di_emit_ctx == g) g_di_emit_ctx = NULL;
    zan_irgen_compactor_destroy((zan_irgen_compactor_t *)g->function_compactor);
    g->function_compactor = NULL;
    if (g->builder) {
        LLVMDisposeBuilder(g->builder);
        g->builder = NULL;
    }
    if (g->mod) {
        LLVMDisposeModule(g->mod);
        g->mod = NULL;
    }
    free(g->functions);
    g->functions = NULL;
    g->function_count = g->function_cap = 0;
    free(g->fn_index);
    g->fn_index = NULL;
    g->fn_index_cap = 0;
    free(g->struct_types);
    g->struct_types = NULL;
    g->struct_type_count = g->struct_type_cap = 0;
    free(g->obf_literals);
    g->obf_literals = NULL;
    g->obf_literal_count = g->obf_literal_cap = 0;
    free(g->string_literals);
    g->string_literals = NULL;
    g->string_literal_count = g->string_literal_cap = 0;
    if (g->site_map_fp) {
        fclose(g->site_map_fp);
        g->site_map_fp = NULL;
    }
    free(g->site_records);
    g->site_records = NULL;
    g->site_record_count = g->site_record_cap = 0;
    free(g->goto_labels);
    g->goto_labels = NULL;
    g->goto_label_count = g->goto_label_cap = 0;
    free(g->goto_fixups);
    g->goto_fixups = NULL;
    g->goto_fixup_count = g->goto_fixup_cap = 0;
    free(g->bind_accs);
    g->bind_accs = NULL;
    g->bind_acc_count = g->bind_acc_cap = 0;
    free(g->catch_cleanups);
    g->catch_cleanups = NULL;
    g->catch_cleanup_count = g->catch_cleanup_cap = 0;
    free(g->body_write_memo);
    g->body_write_memo = NULL;
    g->body_write_memo_cap = g->body_write_memo_count = 0;
    g->body_write_scan_done = NULL;
    free(g->site_inst);
    g->site_inst = NULL;
    free(g->class_release);
    g->class_release = NULL;
    g->class_release_count = g->class_release_cap = 0;
    class_index_reset();
}

void zan_irgen_destroy(zan_irgen_t *g) {
    if (s_current_irgen == g) s_current_irgen = NULL;
    if (g_di_emit_ctx == g) g_di_emit_ctx = NULL;
    free(g->catch_cleanups);
    g->catch_cleanups = NULL;
    g->catch_cleanup_count = g->catch_cleanup_cap = 0;
    free(g->finallys);
    g->finallys = NULL;
    g->finally_count = g->finally_cap = 0;
    free(g->eh_armed);
    g->eh_armed = NULL;
    g->eh_armed_count = g->eh_armed_cap = 0;
    free(g->di_files);
    g->di_files = NULL;
    g->di_file_cap = 0;
    free(g->goto_labels);
    g->goto_labels = NULL;
    g->goto_label_count = g->goto_label_cap = 0;
    free(g->goto_fixups);
    g->goto_fixups = NULL;
    g->goto_fixup_count = g->goto_fixup_cap = 0;
    free(g->bind_accs);
    g->bind_accs = NULL;
    g->bind_acc_count = g->bind_acc_cap = 0;
    free(g->extern_libs);
    g->extern_libs = NULL;
    g->extern_lib_count = g->extern_lib_cap = 0;
    free(g->extern_fns);
    g->extern_fns = NULL;
    g->extern_fn_count = g->extern_fn_cap = 0;
    for (int i = 0; i < g->abi_pending_count; i++) free(g->abi_pending[i]);
    free(g->abi_pending);
    g->abi_pending = NULL;
    g->abi_pending_count = g->abi_pending_cap = 0;
    free(g->obf_literals);
    g->obf_literals = NULL;
    g->obf_literal_count = g->obf_literal_cap = 0;
    free(g->string_literals);
    g->string_literals = NULL;
    g->string_literal_count = g->string_literal_cap = 0;
    zan_irgen_compactor_destroy((zan_irgen_compactor_t *)g->function_compactor);
    g->function_compactor = NULL;
    if (g->builder) LLVMDisposeBuilder(g->builder);
    if (g->mod) LLVMDisposeModule(g->mod);
    if (g->ctx) g->ctx = NULL;
    free(g->functions);
    g->functions = NULL;
    g->function_count = 0;
    g->function_cap = 0;
    free(g->fn_index);
    g->fn_index = NULL;
    g->fn_index_cap = 0;
    class_index_reset();
    free(g->struct_types);
    g->struct_types = NULL;
    g->struct_type_count = g->struct_type_cap = 0;
    free(g->body_write_memo);
    g->body_write_memo = NULL;
    g->body_write_memo_cap = g->body_write_memo_count = 0;
    g->body_write_scan_done = NULL;
    free(g->site_syms);
    g->site_syms = NULL;
    free(g->site_coll);
    g->site_coll = NULL;
    free(g->site_coll_elem);
    g->site_coll_elem = NULL;
    free(g->site_inst);
    g->site_inst = NULL;
    free(g->site_loc_file);
    g->site_loc_file = NULL;
    free(g->site_loc_line);
    g->site_loc_line = NULL;
    g->leak_site_cap = 0;
    free(g->class_release);
    g->class_release = NULL;
    g->class_release_count = g->class_release_cap = 0;
    free(g->ctors);
    g->ctors = NULL;
    g->ctor_count = g->ctor_cap = 0;
    free(g->generic_fns);
    g->generic_fns = NULL;
    g->generic_fn_count = g->generic_fn_cap = 0;
    free(g->generic_ctors);
    g->generic_ctors = NULL;
    g->generic_ctor_count = g->generic_ctor_cap = 0;
    free(g->generic_insts);
    g->generic_insts = NULL;
    g->generic_inst_count = g->generic_inst_cap = 0;
    free(g->method_specs);
    g->method_specs = NULL;
    g->method_spec_count = g->method_spec_cap = g->method_spec_emitted = 0;
    free(g->static_fields);
    g->static_fields = NULL;
    g->static_field_count = g->static_field_cap = 0;
    g->cur_mtps = NULL;
    g->cur_mbind = NULL;
    zan_irgen_shard_buf_free(g);
}

void zan_irgen_shard_buf_append(zan_irgen_t *g, LLVMValueRef fn, const char *txt) {
    if (!g || !txt || !txt[0]) return;
    if (g->streaming_shard_count == 0 || g->streaming_shard_cur_fns >= 250) {
        if (g->streaming_shard_count >= g->streaming_shard_cap) {
            int ncap = g->streaming_shard_cap ? g->streaming_shard_cap * 2 : 16;
            g->streaming_shards = (struct zan_shard_buf *)realloc(
                g->streaming_shards, (size_t)ncap * sizeof(*g->streaming_shards));
            for (int i = g->streaming_shard_cap; i < ncap; i++)
                memset(&g->streaming_shards[i], 0, sizeof(g->streaming_shards[i]));
            g->streaming_shard_cap = ncap;
        }
        g->streaming_shard_count++;
        g->streaming_shard_cur_fns = 0;
    }
    struct zan_shard_buf *sb = &g->streaming_shards[g->streaming_shard_count - 1];
    size_t tlen = strlen(txt);
    if (sb->len + tlen + 2 > sb->cap) {
        size_t ncap = sb->cap ? sb->cap * 2 : 65536;
        while (ncap < sb->len + tlen + 2) ncap *= 2;
        sb->text = (char *)realloc(sb->text, ncap);
        sb->cap = ncap;
    }
    memcpy(sb->text + sb->len, txt, tlen);
    sb->len += tlen;
    sb->text[sb->len++] = '\n';
    sb->text[sb->len] = '\0';
    if (fn) {
        if (sb->fn_count >= sb->fns_cap) {
            int ncap = sb->fns_cap ? sb->fns_cap * 2 : 64;
            sb->fns = (LLVMValueRef *)realloc(sb->fns, (size_t)ncap * sizeof(LLVMValueRef));
            sb->fns_cap = ncap;
        }
        sb->fns[sb->fn_count] = fn;
    }
    sb->fn_count++;
    g->streaming_shard_cur_fns++;
}

void zan_irgen_shard_buf_free(zan_irgen_t *g) {
    if (!g || !g->streaming_shards) return;
    for (int i = 0; i < g->streaming_shard_count; i++) {
        free(g->streaming_shards[i].text);
        free(g->streaming_shards[i].fns);
    }
    free(g->streaming_shards);
    g->streaming_shards = NULL;
    g->streaming_shard_count = g->streaming_shard_cap = g->streaming_shard_cur_fns = 0;
}
zan_type_t *subst_method_tp(zan_irgen_t *g, zan_type_t *t,
                                   zan_ast_list_t *tps, zan_type_t **bind);
int get_or_create_method_spec(zan_irgen_t *g, zan_symbol_t *msym,
                                     zan_type_t **bind, int bindc,
                                     zan_type_t *owner_inst);
void emit_pending_method_specs(zan_irgen_t *g);
zan_type_t *subst_type_param_deep(zan_irgen_t *g, zan_type_t *t,
                                         zan_type_t *recv);
zan_type_t *resolve_type_ctx(zan_irgen_t *g, zan_ast_node_t *tref) {
    zan_type_t *t = zan_binder_resolve_type(g->binder, tref);
    if (g->cur_mtps && t) t = subst_method_tp(g, t, g->cur_mtps, g->cur_mbind);
    if (t && g->cur_inst) t = subst_type_param_deep(g, t, g->cur_inst);
    return t;
}
zan_type_t *field_type_here(zan_irgen_t *g, zan_type_t *t) {
    if (t && g->cur_inst) t = subst_type_param_deep(g, t, g->cur_inst);
    return t;
}

/* 字段的真实类型：把声明处的类型参数按**接收者实例**替换（含嵌套泛型实参）。
 *
 * 泛型类的类型参数字段是**按值内联**存在槽位里的（generic_field_slot 按已知
 * 实例的最大 ABI 尺寸把槽位加宽成 [N x i64]）。写路径按当前实例替换类型实参，
 * 写进去的是内联值；读路径若按擦除形态读，就会把内联值的前 8 字节当成指针
 * 解引用——实测 16 字节结构体直接 0xC0000005。
 *
 * recv 为 NULL 时退回当前实例；recv 是擦除形态（泛型类的 this、泛型参数化
 * 类型）且就是当前类时，才用当前实例兜底——别的泛型类的擦除形态不能拿当前
 * 实例的实参去替换，否则会替错类。 */
zan_type_t *field_type_at(zan_irgen_t *g, zan_symbol_t *fsym,
                                 zan_type_t *recv) {
    if (!fsym || !fsym->type) return NULL;
    zan_type_t *t = fsym->type;
    if (recv) {
        zan_type_t *nt = subst_type_param_deep(g, t, recv);
        if (nt != t) return nt;
        if (g->cur_inst && recv->sym == g->current_type_sym)
            return subst_type_param_deep(g, t, g->cur_inst);
        return t;
    }
    if (g->cur_inst) t = subst_type_param_deep(g, t, g->cur_inst);
    return t;
}

