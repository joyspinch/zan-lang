/* irgen_emit.c -- top-level emission: globals, user-defined methods, entry point
 * and object/IR file output.
 *
 * Part of the irgen translation unit: this file is #include'd by irgen.c
 * (in a fixed order) and must not be compiled standalone. Splitting keeps
 * the single-TU static linkage while keeping each concern in its own file.
 */

/* ---- top-level emission ---- */

static void emit_main_method(zan_irgen_t *g, zan_ast_node_t *method, zan_symbol_t *type_sym,
                             zan_ast_node_t *unit) {
    /* create main(i32 argc, i8** argv) so command-line args are available via
     * the Environment.ArgCount()/ArgAt() builtins. */
    LLVMTypeRef i32ty = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef i8ptrptr = LLVMPointerType(i8ptr, 0);
    LLVMTypeRef main_params[] = { i32ty, i8ptrptr };
    LLVMTypeRef main_type = LLVMFunctionType(i32ty, main_params, 2, 0);
    LLVMValueRef main_fn = LLVMAddFunction(g->mod, "main", main_type);

    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, main_fn, "entry");
    LLVMPositionBuilderAtEnd(g->builder, entry);
    di_clear(g);

    /* On Windows, switch the console code page to UTF-8 (65001) so that
     * non-ASCII output (e.g. CJK text) renders correctly instead of being
     * decoded with the legacy OEM/ANSI codepage. Our string data is UTF-8, and
     * this only affects console handles (a no-op when stdout is a pipe/file),
     * so redirected output keeps its raw UTF-8 bytes. */
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

    /* stash argc/argv into module globals for Environment.* builtins */
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
    LLVMBuildStore(g->builder, LLVMGetParam(main_fn, 0), g_argc);
    LLVMBuildStore(g->builder, LLVMGetParam(main_fn, 1), g_argv);

    /* Make stdout flush promptly so a long-running program's output (a
     * server's startup/request logs, progress prints, ...) is visible
     * immediately instead of sitting in a block buffer until the process
     * exits. The Windows CRT does not honor line buffering (it treats
     * _IOLBF as full buffering), so use unbuffered there; ELF libc gets
     * line buffering (flush on newline), which is cheap. Mirrors C#'s
     * Console.Out.AutoFlush = true. */
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
            const char *soname = g->target_is_macos ? "__stdoutp" : "stdout";
            LLVMValueRef sg = LLVMGetNamedGlobal(g->mod, soname);
            if (!sg) sg = LLVMAddGlobal(g->mod, i8p, soname);
            stdout_ptr = LLVMBuildLoad2(g->builder, i8p, sg, "stdout");
        }
        LLVMTypeRef sv_type = LLVMFunctionType(svi32,
            (LLVMTypeRef[]){ i8p, i8p, svi32, svi64 }, 4, 0);
        LLVMValueRef sv = LLVMGetNamedFunction(g->mod, "setvbuf");
        if (!sv) sv = LLVMAddFunction(g->mod, "setvbuf", sv_type);
        int sv_mode = g->target_is_windows ? 4 /* _IONBF */ : 1 /* _IOLBF */;
        LLVMValueRef sv_args[] = { stdout_ptr, LLVMConstNull(i8p),
            LLVMConstInt(svi32, sv_mode, 0),
            LLVMConstInt(svi64, g->target_is_windows ? 0 : 4096, 0) };
        zan_call2(g->builder, sv_type, sv, sv_args, 4, "");
    }

    /* Programs emit UTF-8 bytes; the Windows console defaults to the legacy
     * OEM/ANSI code page, which renders CJK/accented output as mojibake.
     * Switch the console to UTF-8 (65001). Only affects console rendering:
     * output redirected to a pipe/file keeps its raw UTF-8 bytes. */
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
    g->throw_locals_base = 0;
    g->catch_cleanup_count = 0;
    g->throw_catch_base = 0;
    g->finally_count = 0;
    g->finally_loop_base = 0;
    g->eh_armed_count = 0;
    g->eh_armed_base = 0;
    g->eh_armed_loop_base = 0;
    g->current_type_sym = type_sym;
    g->current_this = NULL;
    g->current_fn_body = method->method_decl.body;

    /* schedule the leak report to run at program exit */
    if (g->check_leaks) {
        emit_leak_report_support(g);
        zan_call2(g->builder, g->atexit_type, g->fn_atexit,
                       &g->fn_report_leaks, 1, "");
    }

    /* Initialize the coroutine scheduler exactly once, before the body runs.
     * Task.Spawn appends onto the ready queue; a root-level `await` used to call
     * zan_co_sched_init itself, which resets the queue and would discard any
     * coroutine spawned before that await. Doing it once here (dominating every
     * Task.Spawn and every root await) fixes concurrent client/server programs
     * where the server is spawned and a client is awaited. Harmless for
     * non-async programs (it just nulls an already-empty queue). */
    zan_call2(g->builder, g->rt_co_sched_init_type, g->rt_co_sched_init, NULL, 0, "");

    /* The static-field initializers below emit calls into Main's entry block.
     * Anchor them to Main's source location so that, under -g, calls to
     * (possibly always-inlined) generated helpers such as Foo_Defaults carry a
     * !dbg location; the LLVM verifier rejects inlinable calls without one in a
     * function that has debug info. No-op when debug info is disabled. */
    di_set_loc(g, method->loc);

    /* Apply static-field initializers once, at program entry, into their
     * backing globals. Runs before the Main body so every subsequent read
     * (in Main or in any method/coroutine) observes the initialized value. */
    if (unit && unit->kind == AST_COMPILATION_UNIT) {
        local_scope_t *sf_locals = local_scope_new(g->arena);
        zan_symbol_t *saved_type = g->current_type_sym;
        LLVMValueRef saved_this = g->current_this;
        g->current_this = NULL;
        for (int di = 0; di < unit->comp_unit.decls.count; di++) {
            zan_ast_node_t *d = unit->comp_unit.decls.items[di];
            if (d->kind != AST_CLASS_DECL && d->kind != AST_STRUCT_DECL) continue;
            zan_symbol_t *csym = zan_binder_lookup(g->binder, d->type_decl.name);
            if (!csym) continue;
            g->current_type_sym = csym;
            for (int mi = 0; mi < d->type_decl.members.count; mi++) {
                zan_ast_node_t *m = d->type_decl.members.items[mi];
                if (m->kind != AST_FIELD_DECL && m->kind != AST_PROPERTY_DECL) continue;
                if (!(m->field_decl.modifiers & MOD_STATIC)) continue;
                if (!m->field_decl.initializer) continue;
                /* a custom-accessor static property has no backing global; only
                 * automatic static properties (`static T P { get; set; }`)
                 * accept an initializer */
                if (m->kind == AST_PROPERTY_DECL &&
                    (m->field_decl.getter_body || m->field_decl.setter_body))
                    continue;
                zan_symbol_t *fs = get_field_sym(csym, m->field_decl.name);
                /* A generic class's static is per closed instantiation, so its
                 * initializer runs once for each instantiation the program
                 * uses -- one store into one shared global would leave every
                 * other instantiation at zero. */
                zan_type_t *insts[64];
                int ninst = 0;
                if (d->type_decl.type_params.count > 0) {
                    for (int gi = 0; gi < g->generic_inst_count && ninst < 64; gi++) {
                        if (g->generic_insts[gi].type_sym != csym) continue;
                        bool seen = false;
                        for (int k = 0; k < ninst && !seen; k++)
                            seen = types_equal(insts[k], g->generic_insts[gi].inst);
                        if (!seen) insts[ninst++] = g->generic_insts[gi].inst;
                    }
                } else {
                    insts[ninst++] = NULL;
                }
                for (int ii = 0; ii < ninst; ii++) {
                zan_type_t *saved_field_inst = g->cur_inst;
                g->cur_inst = insts[ii];
                LLVMValueRef gv = get_static_field_global(g, csym, fs, insts[ii]);
                if (gv) {
                zan_type_t *source_type = infer_expr_type(
                    g, m->field_decl.initializer, sf_locals);
                check_implicit_narrowing(g, fs->type, source_type,
                    m->field_decl.initializer, "field initializer");
                check_value_type_mismatch(g, fs->type, source_type,
                    m->field_decl.initializer, "field initializer");
                LLVMValueRef v = fs->type && fs->type->kind == TYPE_DELEGATE &&
                                 m->field_decl.initializer->kind == AST_LAMBDA
                    ? emit_lambda_typed(g, m->field_decl.initializer,
                                        fs->type, sf_locals)
                    : emit_expr(g, m->field_decl.initializer, sf_locals);
                if (fs->type && is_rc_managed_type(fs->type)) {
                    emit_rc_store_field(g, fs->type, gv, v, m->field_decl.initializer, sf_locals,
                                        (fs->modifiers & MOD_WEAK) ? 1 : 0);
                } else {
                    LLVMTypeRef ft = fs->type ? map_type(g, fs->type)
                                              : LLVMInt64TypeInContext(g->ctx);
                    LLVMBuildStore(g->builder, coerce_int_to(g, v, ft), gv);
                }
                }
                g->cur_inst = saved_field_inst;
                }
            }
        }
        /* ...then run each `static T()` type initializer, in declaration
         * order, so a static field it assigns is already set before any user
         * code (Main or a method reached from it) can observe it. C# triggers
         * these lazily on first use of the type; running them all at entry is
         * the same observable order for a single-file program and avoids a
         * per-type guard on every static access. */
        for (int di = 0; di < unit->comp_unit.decls.count; di++) {
            zan_ast_node_t *d = unit->comp_unit.decls.items[di];
            if (d->kind != AST_CLASS_DECL && d->kind != AST_STRUCT_DECL) continue;
            char cctor_name[512];
            snprintf(cctor_name, sizeof(cctor_name), "%.*s_cctor",
                     (int)d->type_decl.name.len, d->type_decl.name.str);
            LLVMValueRef cctor = LLVMGetNamedFunction(g->mod, cctor_name);
            if (!cctor) continue;
            zan_call2(g->builder, LLVMGlobalGetValueType(cctor), cctor,
                      NULL, 0, "");
        }
        g->current_type_sym = saved_type;
        g->current_this = saved_this;
    }

    /* An async Main was emitted in Pass 2 as a normal ramp/$resume pair (see
     * the Main skip in emit_user_methods). Call the ramp, enqueue the frame,
     * and run the scheduler to completion so root-level awaits and Task.Spawn
     * share one ready queue. */
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
            LLVMValueRef sub = zan_call2(g->builder, LLVMGlobalGetValueType(ramp),
                ramp, NULL, 0, "main.task");
            LLVMValueRef sub_i8 = LLVMBuildBitCast(g->builder, sub, mi8ptr, "main.task8");
            LLVMValueRef sched_args[] = { sub_i8, resume };
            zan_call2(g->builder, g->rt_co_ready_type, g->rt_co_ready, sched_args, 2, "");
            zan_call2(g->builder, g->rt_co_sched_run_type, g->rt_co_sched_run, NULL, 0, "");
            emit_async_check_sub_exc(g, sub_i8);
            LLVMValueRef rptr = LLVMBuildStructGEP2(g->builder, g->co_header_type,
                sub_i8, ASYNC_FRAME_RESULT, "main.res.p");
            LLVMValueRef res = LLVMBuildLoad2(g->builder, i64, rptr, "main.res");
            zan_emit_frame_free(g, sub_i8);
            emit_release_static_rc_fields(g, unit);
            /* void Main: the result slot is zero-initialized, so this still
             * returns 0. */
            LLVMBuildRet(g->builder, LLVMBuildTrunc(g->builder, res,
                LLVMInt32TypeInContext(g->ctx), "main.ret"));
            g->current_type_sym = NULL;
            g->current_fn_body = NULL;
            return;
        }
    }

    local_scope_t *locals = local_scope_new(g->arena);

    g->current_fn_is_main = true;
    if (method->method_decl.body) {
        emit_stmt(g, method->method_decl.body, locals);
    }
    g->current_fn_is_main = false;

    /* add return 0 if no terminator */
    if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder))) {
        emit_release_owned_locals(g, locals);
        emit_release_static_rc_fields(g, unit);
        LLVMBuildRet(g->builder, LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0));
    }
    g->current_type_sym = NULL;
    g->current_fn_body = NULL;
}

/* ---- emit user-defined methods ---- */

/* Deferred body-emission work item: captures everything needed to emit a
 * method/constructor body after ALL functions have been declared, so bodies
 * may make forward references to methods declared later. */
typedef struct {
    zan_ast_node_t *member;
    zan_symbol_t   *type_sym;
    LLVMValueRef    fn;
    LLVMTypeRef    *param_types;
    int             param_count;
    int             param_offset;
    bool            is_static;
    LLVMTypeRef     llvm_ret;
    zan_type_t     *ret_type;
    /* async CPS lowering: when is_async, `fn` is the ramp (returns the task
     * handle) and the body is emitted into `resume_fn` over `frame_type`. */
    bool            is_async;
    LLVMValueRef    resume_fn;
    LLVMTypeRef     frame_type;
    int             await_count;   /* await points in the body (states 1..N) */
    async_local_t  *alocals;       /* named scalar locals held in the frame */
    int             alocal_count;
    int             sub_base;       /* frame index of the first sub-task slot */
    int             handler_cap;    /* per-handler slots in the frame */
    zan_type_t     *cur_inst;       /* instantiation being specialized, or NULL */
    LLVMTypeRef     fn_type;        /* signature of `fn` (the ramp, when async) */
    zan_ast_list_t *mtps;           /* method type params of a specialization */
    zan_type_t    **mbind;          /* their concrete bindings, or NULL */
} method_body_work_t;

/* Count how many discovered instantiations exist for a given generic type. */
static int generic_variant_count(zan_irgen_t *g, zan_symbol_t *type_sym) {
    int n = 0;
    for (int i = 0; i < g->generic_inst_count; i++)
        if (g->generic_insts[i].type_sym == type_sym) n++;
    return n;
}

static bool method_is_tp_template(zan_ast_node_t *member);
static bool class_member_uses_tp(zan_ast_node_t *decl, zan_ast_node_t *member);

/* Body for the erased variant of a generic-class member that reaches into one
 * of the class's type parameters: there is no erased lowering for `t.Member`
 * when T is unknown, and every call site whose receiver instantiation is known
 * routes to a specialized variant instead. Keeping the symbol (rather than
 * dropping it) keeps every existing reference linkable. */
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
}

/* Declare the ramp/resume pair and heap-frame layout of an async method.
 * Shared by Pass A (the erased / per-class-instantiation variants) and by an
 * async method specialization created from a call site: with the type-param
 * bindings active (g->cur_mtps / g->cur_mbind / g->cur_inst) every type in the
 * signature, frame and local layout resolves to its concrete form. */
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
    w->handler_cap = 1;
        LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
        LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
        LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);

        /* Normalize awaits into A-normal form so no intermediate value
         * crosses a suspension in a register (compound / multiple
         * awaits). This mutates the body in place; the scan and body
         * emission below both see the rewritten tree. */
        {
            int anf_counter = 0;
            anf_normalize_block(g, member->method_decl.body, &anf_counter);
        }

        /* Scan the body: count await points (→ states / sub-task slots)
         * and collect named scalar locals that must live in the frame
         * so they survive across suspensions. */
        async_scan_t scan = { g, 0, NULL, 0, 0,
                              local_scope_new(g->arena), 0, 0 };
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
        /* One per-handler slot group per try the body lowers. LLVM
         * rejects a zero-length array member, so a body with no try at
         * all still gets one unused slot. */
        w->handler_cap = scan.try_count > 0 ? scan.try_count : 1;

        /* frame = fixed header + params + frame locals +
         * one i8* sub-task handle per await point. */
        int locals_base = ASYNC_FRAME_FIRST_PARAM + total_params;
        w->sub_base = locals_base + w->alocal_count;
        int nfields = w->sub_base + w->await_count;
        LLVMTypeRef *fields = (LLVMTypeRef *)calloc((size_t)nfields, sizeof(LLVMTypeRef));
        fields[ASYNC_FRAME_SCHED] = i64;
        fields[ASYNC_FRAME_SCHED_STEP] = g->co_step_ptr;
        fields[ASYNC_FRAME_STATE] = i32;
        fields[ASYNC_FRAME_DONE] = i32;
        fields[ASYNC_FRAME_AWAITER] = i8ptr;
        fields[ASYNC_FRAME_AWAITER_STEP] = g->co_step_ptr;
        fields[ASYNC_FRAME_RESULT] = i64;
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
        fields[ASYNC_FRAME_FINEXC] =
            LLVMArrayType(i8ptr, ZAN_MAX_FINALLY_DEPTH);
        fields[ASYNC_FRAME_FINEXC_OWNED] =
            LLVMArrayType(i32, ZAN_MAX_FINALLY_DEPTH);
        fields[ASYNC_FRAME_FINEXC_TID] =
            LLVMArrayType(i8ptr, ZAN_MAX_FINALLY_DEPTH);
        for (int k = 0; k < total_params; k++) {
            fields[ASYNC_FRAME_FIRST_PARAM + k] = param_types[k];
        }
        for (int k = 0; k < w->alocal_count; k++) {
            w->alocals[k].frame_index = locals_base + k;
            fields[locals_base + k] = w->alocals[k].llvm;
        }
        for (int k = 0; k < w->await_count; k++) {
            fields[w->sub_base + k] = i8ptr;
        }
        char frame_name[560];
        snprintf(frame_name, sizeof(frame_name), "%s$frame", fn_name);
        w->frame_type = frame_type = LLVMStructCreateNamed(g->ctx, frame_name);
        LLVMStructSetBody(frame_type, fields, (unsigned)nfields, 0);
        free(fields);

        /* ramp keeps the external param list but returns the task
         * handle (i8*); the body runs later in resume(frame). */
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

/* Emit the ramp, resume state machine and cleanup fn of one async method.
 * Shared by Pass A/B and by async method specializations (A32-3b). */
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

        /* Per-coroutine cleanup fn (stored in the frame header): releases
         * every rc value the frame owns and frees the frame itself. Called
         * by __zan_async_unwind when an exception skips this frame. */
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
        LLVMBasicBlockRef ramp_entry = LLVMAppendBasicBlockInContext(g->ctx, ramp_fn, "entry");
        LLVMPositionBuilderAtEnd(g->builder, ramp_entry);
        /* -g: anchor to the async method's decl line so ramp/resume prologue
         * calls (frame malloc/memset, arg retains, state restore) carry an
         * in-scope !dbg -- the resume fn gains a DISubprogram from its body
         * statements, and LLVM rejects any inlinable call without a location
         * in a debug-info function. (no-op when debug info is disabled.) */
        di_set_loc(g, member->loc);
        LLVMTypeRef malloc_ty = LLVMGlobalGetValueType(g->fn_malloc);
        LLVMValueRef fsize = LLVMSizeOf(frame_type);
        LLVMValueRef raw = zan_call2(g->builder, malloc_ty, g->fn_malloc, &fsize, 1, "frame.raw");
        zan_irgen_emit_oom_check(g, ramp_fn, raw);
        /* Zero the frame so every owning (RC) local slot starts null. The
         * per-iteration capture of a loop-body local releases the reloaded
         * previous occupant of its slot; that requires the slot to be null
         * (not garbage) before its first write. */
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
        /* Record this frame's own resume fn so an awaiter can drive it
         * without resolving `<ramp>$resume` by name -- the only way to await
         * an indirect (delegate/function-pointer) async call. */
        LLVMBuildStore(g->builder, resume_fn,
            LLVMBuildStructGEP2(g->builder, frame_type, rframe, ASYNC_FRAME_SELF_STEP, "selfstep"));
        for (int k = 0; k < total_params; k++) {
            LLVMValueRef pv = LLVMGetParam(ramp_fn, (unsigned)k);
            LLVMValueRef slot = LLVMBuildStructGEP2(g->builder, frame_type, rframe,
                (unsigned)(ASYNC_FRAME_FIRST_PARAM + k), "arg");
            LLVMBuildStore(g->builder, pv, slot);
            /* The caller passes arguments by borrow and releases owned temps
             * right after the ramp returns, but the heap frame outlives that
             * temp and the coroutine reads the argument after suspension. So
             * the frame takes ownership of ARC-managed by-value arguments: it
             * retains here (synchronously, before the caller's release) and
             * releases them from emit_async_complete (see the matching
             * arc_owned marking on the resume-body param locals).
             *
             * The receiver needs the same treatment: a fluent receiver temp
             * (`db.Select<T>().Where(..).ToListAsync()`, `Make().RunAsync()`)
             * is an owned temp the caller releases as soon as the ramp
             * returns -- before the body has run a single statement -- so a
             * borrowed `this` would be a use-after-free (unlike a synchronous
             * method, whose body has already finished by then). */
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
                    }
                }
            }
        }
        LLVMBuildRet(g->builder, raw);

        /* ---- resume ---- */
        LLVMBasicBlockRef res_entry = LLVMAppendBasicBlockInContext(g->ctx, resume_fn, "entry");
        LLVMPositionBuilderAtEnd(g->builder, res_entry);
        di_set_loc(g, member->loc);
        LLVMValueRef fparam = LLVMGetParam(resume_fn, 0);
        LLVMValueRef sframe = LLVMBuildBitCast(g->builder, fparam, frame_ptr_ty, "frame");

        local_scope_t *locals = local_scope_new(g->arena);

        /* Frame-resident slots: `this` (instance methods), params, and named
         * scalar locals. Their storage is stack allocas created here in the
         * entry block (so they dominate every state block); values are saved
         * to / reloaded from the heap frame around each suspension. */
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
            local_add(locals, param->param.name, pa, pt);
            if (pt && pt->kind == TYPE_STRING)
                locals->vars[locals->count - 1].opaque_string = 1;
            /* Balances the retain the ramp performed for ARC-managed by-value
             * params: the frame owns them, so release at coroutine completion. */
            if (!param->param.by_ref && is_rc_managed_type(pt) &&
                LLVMGetTypeKind(pty) == LLVMPointerTypeKind) {
                locals->vars[locals->count - 1].arc_owned = 1;
            }
            slots[si].slot_alloca = pa;
            slots[si].llvm = pty;
            slots[si].frame_index = ASYNC_FRAME_FIRST_PARAM + param_offset + k;
            si++;
        }
        for (int k = 0; k < alocal_count; k++) {
            LLVMValueRef la = LLVMBuildAlloca(g->builder, w->alocals[k].llvm, "fl");
            local_add(locals, w->alocals[k].name, la, w->alocals[k].ztype);
            /* A frame-resident rc local is owned for the whole coroutine
             * (it is never dropped at inner block scope like a synchronous
             * local). Mark it owned and null-init its slot *here*, at
             * registration, rather than when its var-decl is emitted: a
             * `return` lexically preceding the declaration (e.g. an early
             * exit at the top of a loop whose body declares the local after
             * an await) must still release the value a prior iteration
             * stored, or it leaks. The null-init makes the release a no-op
             * on paths where the local was never assigned. */
            if (!w->alocals[k].no_arc &&
                is_rc_managed_type(w->alocals[k].ztype) &&
                LLVMGetTypeKind(w->alocals[k].llvm) == LLVMPointerTypeKind) {
                LLVMBuildStore(g->builder,
                    LLVMConstNull(w->alocals[k].llvm), la);
                locals->vars[locals->count - 1].arc_owned = 1;
            }
            slots[si].slot_alloca = la;
            slots[si].llvm = w->alocals[k].llvm;
            slots[si].frame_index = w->alocals[k].frame_index;
            si++;
        }

        LLVMValueRef saved_fn = g->current_fn;
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
        void *saved_slots = (void *)g->current_async_slots;
        int saved_slot_count = g->current_async_slot_count;
        LLVMValueRef saved_eh_entry = g->current_async_eh_entry;
        LLVMBasicBlockRef saved_exc_bb = g->current_async_exc_bb;
        LLVMValueRef saved_rearm = g->current_async_rearm_switch;
        int saved_handler_next = g->current_async_handler_next;
        int saved_handler_cap = g->current_async_handler_cap;
        int saved_foreach_next = g->current_async_foreach_next;
        int saved_this_owned = g->current_async_this_owned;
        zan_type_t *saved_this_owned_type = g->current_async_this_type;

        g->current_fn = resume_fn;
        g->current_fn_ret_type = LLVMVoidTypeInContext(g->ctx);
        /* a nested body starts with no enclosing try of its own */
        int saved_throw_base = g->throw_locals_base;
        int saved_catch_cc = g->catch_cleanup_count;
        int saved_throw_cb = g->throw_catch_base;
        int saved_fin_c = g->finally_count;
        int saved_fin_lb = g->finally_loop_base;
        int saved_eh_c = g->eh_armed_count;
        int saved_eh_b = g->eh_armed_base;
        int saved_eh_lb = g->eh_armed_loop_base;
        g->throw_locals_base = 0;
        g->catch_cleanup_count = 0;
        g->throw_catch_base = 0;
        g->finally_count = 0;
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
        g->current_fn_zan_ret_type = g->current_async_ret_type;
        g->current_async_next_state = 1;
        g->current_async_sub_base = w->sub_base;
        g->current_async_sub_next = 0;
        g->current_async_slots = slots;
        g->current_async_slot_count = slot_total;
        g->current_async_eh_entry = NULL;
        g->current_async_exc_bb = NULL;
        g->current_async_rearm_switch = NULL;
        g->current_async_handler_next = 0;
        g->current_async_handler_cap = w->handler_cap;
        g->current_async_foreach_next = 0;
        g->current_async_this_owned = this_owned;
        g->current_async_this_type = this_owned_type;

        /* arm this invocation's exception trampoline (and re-arm the
         * handlers of the tries the frame is suspended inside) before the
         * state dispatch, so a throw anywhere in the body lands on a live
         * stack frame */
        emit_async_eh_prologue(g);

        /* This invocation is running, so it is not suspended on a
         * sub-frame: drop the CHILD link the last suspension left behind
         * (it is about to be consumed and freed). Cancellation walks that
         * chain, and a stale entry would point at freed memory. */
        LLVMBuildStore(g->builder,
            LLVMConstNull(LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0)),
            LLVMBuildStructGEP2(g->builder, frame_type, sframe,
                ASYNC_FRAME_CHILD, "fr.child"));

        LLVMValueRef state = LLVMBuildLoad2(g->builder, i32,
            LLVMBuildStructGEP2(g->builder, frame_type, sframe, ASYNC_FRAME_STATE, "st.ptr"),
            "state");
        LLVMBasicBlockRef body_bb = LLVMAppendBasicBlockInContext(g->ctx, resume_fn, "co.start");
        /* switch(state): case 0 (start) -> body; each await point appends a
         * resume-k case (see the AST_AWAIT_EXPR lowering). */
        LLVMValueRef sw = LLVMBuildSwitch(g->builder, state, body_bb, (unsigned)(w->await_count + 1));
        LLVMAddCase(sw, LLVMConstInt(i32, 0, 0), body_bb);
        g->current_async_switch = sw;

        LLVMPositionBuilderAtEnd(g->builder, body_bb);
        emit_async_reload_slots(g); /* load `this`/params from the frame */
        /* cancelling a coroutine that is still queued for its first step
         * must skip the body entirely */
        emit_async_cancel_check(g, locals);

        if (member->method_decl.body->kind == AST_BLOCK) {
            for (int k = 0; k < member->method_decl.body->block.stmts.count; k++) {
                zan_ast_node_t *bs = member->method_decl.body->block.stmts.items[k];
                emit_stmt(g, bs, locals);
                /* a statement that awaited gave the scheduler a chance to
                 * run Task.Cancel; observe it at this statement boundary,
                 * where completing behaves exactly like an early `return` */
                if (anf_stmt_contains_await(bs))
                    emit_async_cancel_check(g, locals);
            }
        } else {
            /* expression body (=> expr): treat as `return expr`. */
            check_implicit_narrowing(g, g->current_fn_zan_ret_type,
                infer_expr_type(g, member->method_decl.body, locals),
                member->method_decl.body, "return");
            LLVMValueRef val = emit_expr(g, member->method_decl.body, locals);
            emit_async_complete(g, locals,
                coerce_to_frame_result(g, coerce_async_ret(g, val),
                                       g->current_async_ret_type));
        }

        /* fall off the end: implicit completion (void / default result). */
        if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder))) {
            emit_async_complete(g, locals, NULL);
        }

        /* an exception nothing in the body caught completes the coroutine
         * with that exception, for the awaiter to re-throw */
        emit_async_exc_epilogue(g, locals);

        g->current_fn = saved_fn;
        g->current_fn_ret_type = saved_fn_ret;
        g->current_fn_zan_ret_type = saved_fn_zan_ret;
        g->throw_locals_base = saved_throw_base;
        g->catch_cleanup_count = saved_catch_cc;
        g->throw_catch_base = saved_throw_cb;
        g->finally_count = saved_fin_c;
        g->finally_loop_base = saved_fin_lb;
        g->eh_armed_count = saved_eh_c;
        g->eh_armed_base = saved_eh_b;
        g->eh_armed_loop_base = saved_eh_lb;
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
        g->current_async_slots = saved_slots;
        g->current_async_slot_count = saved_slot_count;
        g->current_async_eh_entry = saved_eh_entry;
        g->current_async_exc_bb = saved_exc_bb;
        g->current_async_rearm_switch = saved_rearm;
        g->current_async_handler_next = saved_handler_next;
        g->current_async_handler_cap = saved_handler_cap;
        g->current_async_foreach_next = saved_foreach_next;
        g->current_async_this_owned = saved_this_owned;
        g->current_async_this_type = saved_this_owned_type;

        /* ---- cleanup: release owned rc slots from the frame, free it.
         * Mirrors the arc_owned marking above: by-value rc params (the
         * ramp retained them) and frame-resident rc locals. Values are
         * read from the heap frame -- authoritative for a suspended
         * coroutine (slots are saved at every suspension and throw). */
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
                if (!is_rc_managed_type(pt) ||
                    LLVMGetTypeKind(pty) != LLVMPointerTypeKind) continue;
                LLVMValueRef sp = LLVMBuildStructGEP2(g->builder, frame_type, cframe,
                    (unsigned)(ASYNC_FRAME_FIRST_PARAM + param_offset + k), "cl.p");
                LLVMValueRef v = LLVMBuildLoad2(g->builder, pty, sp, "cl.pv");
                emit_rc_release_for_type(g, pt, v);
            }
            for (int k = 0; k < w->alocal_count; k++) {
                if (w->alocals[k].no_arc ||
                    !is_rc_managed_type(w->alocals[k].ztype) ||
                    LLVMGetTypeKind(w->alocals[k].llvm) != LLVMPointerTypeKind)
                    continue;
                LLVMValueRef sp = LLVMBuildStructGEP2(g->builder, frame_type, cframe,
                    (unsigned)w->alocals[k].frame_index, "cl.l");
                LLVMValueRef v = LLVMBuildLoad2(g->builder,
                    w->alocals[k].llvm, sp, "cl.lv");
                emit_rc_release_for_type(g, w->alocals[k].ztype, v);
            }
            zan_emit_frame_free(g, cparam);
            LLVMBuildRetVoid(g->builder);
            g->current_fn = saved_cl_fn;
        }
        free(param_types);
    g->cur_mtps = saved_mtps;
    g->cur_mbind = saved_mbind;
    g->cur_inst = saved_inst;
    if (saved_bb) LLVMPositionBuilderAtEnd(g->builder, saved_bb);
    (void)type_sym; (void)fn;
}

static void emit_user_methods(zan_irgen_t *g, zan_ast_node_t *unit) {
    /* Discover every concrete instantiation of a user generic class up front so
     * Pass A can emit one specialized variant per instantiation (in addition to
     * the erased variant). */
    discover_generic_insts(g, unit);

    /* Size an upper bound for the deferred body work list, counting the erased
     * variant plus one per discovered instantiation for generic types. */
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

    /* Pass A: declare & register every function/constructor. */
    for (int i = 0; i < unit->comp_unit.decls.count; i++) {
        zan_ast_node_t *decl = unit->comp_unit.decls.items[i];
        if (decl->kind != AST_CLASS_DECL && decl->kind != AST_STRUCT_DECL) continue;

        /* look up symbol for this type */
        zan_symbol_t *type_sym = zan_binder_lookup(g->binder, decl->type_decl.name);
        if (!type_sym) continue;

        /* Emit one variant per (erased + each concrete instantiation). The
         * erased variant (cur_variant == NULL) keeps the existing symbol names
         * and registration; specialized variants add a name suffix and register
         * into the generic fn/ctor tables. Signatures are identical across
         * variants (type parameters still lower to the erased representation);
         * only the body differs, via g->cur_inst set in Pass B. */
        zan_type_t *variants[64];
        int nvar = 0;
        variants[nvar++] = NULL;
        if (is_user_generic_sym(type_sym)) {
            for (int gi = 0; gi < g->generic_inst_count && nvar < 64; gi++)
                if (g->generic_insts[gi].type_sym == type_sym)
                    variants[nvar++] = g->generic_insts[gi].inst;
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
            /* `static T()` is a type initializer, not a constructor: it takes
             * no `this`, is never selected by `new T(...)`, and runs once at
             * program entry (emit_main_method calls T_cctor). It used to be
             * registered as a zero-argument constructor, so its body ran only
             * when the type was first instantiated -- a program that merely
             * read a static field saw the uninitialized value. */
            bool is_type_init = is_ctor &&
                (member->method_decl.modifiers & MOD_STATIC) != 0;
            /* A generic type's initializer is emitted once, off the erased
             * declaration, not per instantiation. */
            if (is_type_init && cur_variant) continue;
            /* A generic method that reaches into its own type parameters is a
             * template: it exists only as monomorphized copies, emitted from
             * the call sites (emit_method_spec_body). Emitting an erased
             * instance here produced broken IR, since `c.Name()` on a T has
             * nothing to resolve against. */
            /* an instance template monomorphizes too (`this` is prepended to
             * the specialized signature; a generic declaring type adds its
             * instantiation to the specialization key), and so does an async
             * one (its ramp/resume/frame are built per specialization). */
            if (method_is_tp_template(member)) continue;
            /* extern/DllImport methods have no generic body: only the erased
             * variant declares them. */
            bool is_extern_decl =
                member->kind == AST_METHOD_DECL && !member->method_decl.body &&
                (member->method_decl.extern_lib.str ||
                 (member->method_decl.modifiers & MOD_EXTERN) != 0);
            if (cur_variant && is_extern_decl)
                continue;

            /* extern methods (with or without [DllImport]) become a plain
             * external declaration resolved by the linker; without one a
             * bodyless extern would silently return a default value. */
            if (is_extern_decl) {
                /* build extern function declaration */
                int pc = member->method_decl.params.count;
                LLVMTypeRef *pt = (LLVMTypeRef *)calloc((size_t)(pc > 0 ? pc : 1), sizeof(LLVMTypeRef));
                zan_type_t **pzt = (zan_type_t **)calloc((size_t)(pc > 0 ? pc : 1), sizeof(zan_type_t *));
                for (int k = 0; k < pc; k++) {
                    zan_ast_node_t *param = member->method_decl.params.items[k];
                    zan_type_t *ptype = zan_binder_resolve_type(g->binder, param->param.type);
                    pzt[k] = ptype;
                    pt[k] = map_type(g, ptype);
                }
                zan_type_t *rt = member->method_decl.return_type
                    ? zan_binder_resolve_type(g->binder, member->method_decl.return_type)
                    : g->binder->type_void;
                LLVMTypeRef llvm_rt = map_type(g, rt);
                LLVMTypeRef ft = LLVMFunctionType(llvm_rt, pt, (unsigned)pc, 0);
                /* use entry_point if specified, otherwise method name */
                char ext_name[256];
                if (member->method_decl.entry_point.str) {
                    snprintf(ext_name, sizeof(ext_name), "%.*s",
                             (int)member->method_decl.entry_point.len,
                             member->method_decl.entry_point.str);
                } else {
                    snprintf(ext_name, sizeof(ext_name), "%.*s",
                             (int)member->method_decl.name.len,
                             member->method_decl.name.str);
                }
                if (strncmp(ext_name, "zan_file_", 9) == 0 ||
                    strncmp(ext_name, "zan_pkg_", 8) == 0) {
                    /* file metadata + FILE* stream IO (System.IO.FileInfo /
                     * FileStream): split into its own runtime object so a
                     * file-IO program does not drag in the atomics/threads/
                     * shared-table runtime (rt_file.o vs rt_sync.o). */
                    g->uses_file_runtime = true;
                }
                if (strncmp(ext_name, "zan_atomic_int_", 15) == 0 ||
                    strncmp(ext_name, "zan_shared_table_", 17) == 0 ||
                    strncmp(ext_name, "zan_thread_", 11) == 0 ||
                    strncmp(ext_name, "zan_dispatch_", 13) == 0 ||
                    strncmp(ext_name, "zan_monotonic_", 14) == 0 ||
                    strncmp(ext_name, "zan_plat_", 9) == 0) {
                    if (getenv("ZAN_TRACE_SYNC"))
                        fprintf(stderr, "[sync-flag] %s\n", ext_name);
                    g->uses_sync_runtime = true;
                }
                if (strncmp(ext_name, "zan_io_socket_", 14) == 0) {
                    g->uses_socket_async = true;
                }
                if (strncmp(ext_name, "zan_gate_", 9) == 0) {
                    g->uses_socket_async = true;
                }
                if (strncmp(ext_name, "zan_timer_", 10) == 0 ||
                    strncmp(ext_name, "swoole_timer_", 13) == 0) {
                    g->uses_timer_runtime = true;
                }
                if (strncmp(ext_name, "zan_embed_", 10) == 0) {
                    g->uses_embed_api = true;
                }
                /* Reuse existing declaration if the symbol already exists in the module
                 * (e.g. built-in malloc/free/strlen, or duplicate DllImport across files). */
                /* A struct crossing the boundary is not passed the way LLVM
                 * passes a first-class aggregate: abi_extern_thunk declares the
                 * symbol with the platform C signature and wraps it. */
                LLVMValueRef efn = abi_extern_thunk(g, ext_name, ft);
                if (!efn) efn = LLVMGetNamedFunction(g->mod, ext_name);
                if (!efn) {
                    efn = LLVMAddFunction(g->mod, ext_name, ft);
                    abi_add_int_ext_attrs(g, efn, rt, pzt, pc);
                }
                free(pzt);
                /* register as a static method so it can be called */
                zan_symbol_t *method_sym = method_sym_for_decl(type_sym, member);
                if (method_sym) {
                    irgen_register_function(g, method_sym, efn, ft);
                }
                /* store lib name for linker */
                if (member->method_decl.extern_lib.str && g->extern_lib_count < 64) {
                    bool already = false;
                    for (int li = 0; li < g->extern_lib_count; li++) {
                        if (g->extern_libs[li].len == member->method_decl.extern_lib.len &&
                            memcmp(g->extern_libs[li].str, member->method_decl.extern_lib.str,
                                   member->method_decl.extern_lib.len) == 0) {
                            already = true;
                            break;
                        }
                    }
                    if (!already) {
                        g->extern_libs[g->extern_lib_count++] = member->method_decl.extern_lib;
                    }
                }
                /* record (lib, fn) so an unresolvable lib can be stubbed when
                 * cross-linking a static Linux binary */
                if (member->method_decl.extern_lib.str &&
                    g->extern_fn_count < (int)(sizeof(g->extern_fns) / sizeof(g->extern_fns[0]))) {
                    zan_istr_t sym = member->method_decl.entry_point.str
                        ? member->method_decl.entry_point
                        : member->method_decl.name;
                    bool seen = false;
                    for (int fi = 0; fi < g->extern_fn_count; fi++) {
                        if (g->extern_fns[fi].name.len == sym.len &&
                            memcmp(g->extern_fns[fi].name.str, sym.str, sym.len) == 0) {
                            seen = true;
                            break;
                        }
                    }
                    if (!seen) {
                        g->extern_fns[g->extern_fn_count].lib = member->method_decl.extern_lib;
                        g->extern_fns[g->extern_fn_count].name = sym;
                        g->extern_fn_count++;
                    }
                }
                free(pt);
                continue;
            }

            if (!member->method_decl.body) continue;

            /* skip static Main — handled separately. An `async` Main IS
             * emitted here, as an ordinary ramp/$resume pair: the real `main`
             * (emit_main_method) then drives it on the scheduler. Inlining an
             * async Main's body at the root would lower every `await
             * Task.Delay` to a bare sleep that never runs the ready queue, so
             * coroutines spawned with Task.Spawn would starve. */
            bool is_static = (!is_ctor || is_type_init) &&
                (member->method_decl.modifiers & MOD_STATIC) != 0;
            if (is_static && member->method_decl.name.len == 4 &&
                memcmp(member->method_decl.name.str, "Main", 4) == 0 &&
                !(member->method_decl.modifiers & MOD_ASYNC)) continue;

            /* build function name: TypeName_MethodName or TypeName_ctor,
             * plus a per-instantiation suffix (e.g. HashSet_Add$string) for a
             * specialized variant so it does not collide with the erased one. */
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
                /* Same-named overloads would otherwise all be added as this
                 * one LLVM symbol; LLVM silently renames the later ones (e.g.
                 * "Box_GetAsync.1"), which breaks the async ramp/$resume
                 * pairing - an await site derives the resume symbol from the
                 * callee's actual name and would find nothing, falling into
                 * the legacy busy-wait path (a hang). Uniquify explicitly so
                 * every ramp keeps a matching "$resume". */
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

            /* build function type */
            int param_count = member->method_decl.params.count;
            int total_params = is_static ? param_count : param_count + 1;
            LLVMTypeRef *param_types = (LLVMTypeRef *)calloc((size_t)(total_params > 0 ? total_params : 1), sizeof(LLVMTypeRef));

            int param_offset = 0;
            if (!is_static) {
                /* this pointer for instance methods */
                LLVMTypeRef struct_type = get_struct_llvm_type(g, type_sym);
                param_types[0] = struct_type ? LLVMPointerType(struct_type, 0)
                                             : LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                param_offset = 1;
            }

            /* A specialized variant is monomorphic: its signature must use the
             * instantiation's concrete types. Leaving a value-type argument
             * erased to a pointer made the caller pass a struct by value to a
             * ptr parameter (Box<GVec>.Put), which LLVM rejects. */
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
            /* async methods lower to a heap frame + ramp + resume (see
             * docs/ASYNC_CPS_DESIGN.md); ctors are never async. */
            bool is_async = !is_ctor && (member->method_decl.modifiers & MOD_ASYNC) != 0;

            LLVMTypeRef fn_type = NULL;
            LLVMValueRef fn = NULL;
            LLVMValueRef resume_fn = NULL;
            LLVMTypeRef frame_type = NULL;
            int a_await_count = 0;
            async_local_t *a_locals = NULL;
            int a_local_count = 0;
            int a_sub_base = 0;
            int a_handler_cap = 1;

            if (is_async) {
                method_body_work_t adecl;
                memset(&adecl, 0, sizeof(adecl));
                adecl.member = member;
                adecl.type_sym = type_sym;
                adecl.is_static = is_static;
                adecl.param_types = param_types;
                adecl.param_count = param_count;
                adecl.param_offset = param_offset;
                declare_async_method(g, &adecl, fn_name);
                fn = adecl.fn;
                fn_type = adecl.fn_type;
                resume_fn = adecl.resume_fn;
                frame_type = adecl.frame_type;
                a_await_count = adecl.await_count;
                a_locals = adecl.alocals;
                a_local_count = adecl.alocal_count;
                a_sub_base = adecl.sub_base;
                a_handler_cap = adecl.handler_cap;
            } else {
                fn_type = LLVMFunctionType(llvm_ret, param_types, (unsigned)total_params, 0);
                fn = LLVMAddFunction(g->mod, fn_name, fn_type);
                if (!(g->emit_lib &&
                      (member->method_decl.modifiers & MOD_PUBLIC) != 0))
                    zan_set_module_local(fn);
            }

            /* register in function/ctor table. For async methods the ramp is
             * the callable symbol (a call site receives the task handle). The
             * erased variant registers in the ordinary tables; a specialized
             * variant registers in the generic tables keyed by (sym, args) so a
             * concrete call site can route to it. */
            if (is_type_init) {
                /* callable only from program entry; not in any dispatch table */
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

            /* The erased variant of a member reaching into the class's own
             * type parameters has no valid lowering; only the specialized
             * variants below carry a real body. */
            if (!cur_variant && class_member_uses_tp(decl, member)) {
                emit_tp_erased_stub(g, fn);
                free(param_types);
                continue;
            }

            /* defer body emission to Pass B so calls may forward-reference
             * methods declared later in this (or another) type */
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
                work[work_count].handler_cap = a_handler_cap;
                work[work_count].cur_inst = cur_variant;
                work_count++;
            } else {
                free(param_types);
            }
        }
        } /* end variant loop */
    }

    /* Pass B: emit every deferred body now that all functions are declared. */
    for (int w = 0; w < work_count; w++) {
        zan_ast_node_t *member = work[w].member;
        zan_symbol_t *type_sym = work[w].type_sym;
        zan_compile_trace("emit %.*s.%.*s",
                        type_sym ? (int)type_sym->name.len : 1,
                        type_sym ? type_sym->name.str : "?",
                        (int)member->method_decl.name.len,
                        member->method_decl.name.str);
        LLVMValueRef fn = work[w].fn;
        LLVMTypeRef *param_types = work[w].param_types;
        int param_count = work[w].param_count;
        int param_offset = work[w].param_offset;
        bool is_static = work[w].is_static;
        LLVMTypeRef llvm_ret = work[w].llvm_ret;
        zan_type_t *ret_type = work[w].ret_type;
        LLVMValueRef this_alloca = NULL;
        /* Activate the instantiation context for a specialized variant so that
         * intrinsic element comparisons in this body substitute the type
         * parameter to its concrete argument (NULL for erased/non-generic). */
        g->cur_inst = work[w].cur_inst;

        /* async method: emit ramp (allocate frame, stash params, hand out the
         * task handle) + resume (state-machine entry running the body). See
         * docs/ASYNC_CPS_DESIGN.md. */
        if (work[w].is_async) {
            emit_async_method_ir(g, &work[w]);
            continue;
        }

        LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
        LLVMPositionBuilderAtEnd(g->builder, entry);
        /* -g: anchor to the method's declaration line before emitting the
         * prologue. This eagerly creates this function's DISubprogram and
         * gives prologue instructions -- notably a constructor's base/chained
         * ctor call -- a valid in-scope !dbg, which LLVM requires of every
         * inlinable call in a function that carries debug info. Statements
         * override it per line. (no-op when debug info is disabled.) */
        di_set_loc(g, member->loc);

        local_scope_t *locals = local_scope_new(g->arena);

        if (!is_static) {
            /* bind 'this' as first parameter */
            this_alloca = LLVMBuildAlloca(g->builder, param_types[0], "this");
            LLVMBuildStore(g->builder, LLVMGetParam(fn, 0), this_alloca);
        }

        /* bind method parameters */
        for (int k = 0; k < param_count; k++) {
            zan_ast_node_t *param = member->method_decl.params.items[k];
            zan_type_t *pt = zan_binder_resolve_type(g->binder, param->param.type);
            if (g->cur_inst) pt = subst_type_param_deep(g, pt, g->cur_inst);
            if (param->param.by_ref) {
                /* `ref`/`out`: the incoming pointer IS the storage slot, so
                 * reads/writes go straight through to the caller's variable. */
                local_add(locals, param->param.name,
                          LLVMGetParam(fn, (unsigned)(k + param_offset)), pt);
                if (pt && pt->kind == TYPE_STRING)
                    locals->vars[locals->count - 1].opaque_string = 1;
                /* The caller's slot owns the reference it holds, so writing
                 * through it must retain the new value and release the old
                 * one; scope exit leaves it alone (byref_slot). Without this
                 * `out string s; s = v;` handed the caller a borrowed value
                 * it then released -- a refcount deficit that double-frees. */
                if (is_rc_managed_type(pt))
                    locals->vars[locals->count - 1].byref_slot =
                        locals->vars[locals->count - 1].arc_owned = 1;
                continue;
            }
            LLVMValueRef param_alloca = LLVMBuildAlloca(g->builder, param_types[k + param_offset], "p");
            LLVMBuildStore(g->builder, LLVMGetParam(fn, (unsigned)(k + param_offset)), param_alloca);
            local_add(locals, param->param.name, param_alloca, pt);
            if (pt && pt->kind == TYPE_STRING)
                locals->vars[locals->count - 1].opaque_string = 1;
        }

        LLVMValueRef saved_fn = g->current_fn;
        LLVMTypeRef saved_fn_ret = g->current_fn_ret_type;
        zan_type_t *saved_fn_zan_ret = g->current_fn_zan_ret_type;
        LLVMValueRef saved_this = g->current_this;
        zan_symbol_t *saved_type_sym = g->current_type_sym;
        zan_ast_node_t *saved_fn_body = g->current_fn_body;
        g->current_fn = fn;
        g->current_fn_ret_type = llvm_ret;
        g->current_fn_zan_ret_type = ret_type;
        int saved_throw_base = g->throw_locals_base;
        int saved_catch_cc = g->catch_cleanup_count;
        int saved_throw_cb = g->throw_catch_base;
        int saved_fin_c = g->finally_count;
        int saved_fin_lb = g->finally_loop_base;
        int saved_eh_c = g->eh_armed_count;
        int saved_eh_b = g->eh_armed_base;
        int saved_eh_lb = g->eh_armed_loop_base;
        g->throw_locals_base = 0;
        g->catch_cleanup_count = 0;
        g->throw_catch_base = 0;
        g->finally_count = 0;
        g->finally_loop_base = 0;
        g->eh_armed_base = g->eh_armed_count;
        g->eh_armed_loop_base = g->eh_armed_count;
        g->current_this = is_static ? NULL : this_alloca;
        g->current_type_sym = type_sym;
        g->current_fn_body = member->method_decl.body;
        g->current_fn_is_ctor = member->kind == AST_CONSTRUCTOR_DECL;
        g->current_fn_no_runtime = zan_ast_has_attr(member, "NoRuntime");

        /* `is_static` on a constructor means `static T()`: no `this`, so no
         * base/this chaining and no instance field initializers. */
        if (member->kind == AST_CONSTRUCTOR_DECL && !is_static) {
            bool this_init = member->method_decl.has_this_init;
            bool initializer_target_called = false;
            zan_symbol_t *target_sym = NULL;
            if (this_init) {
                target_sym = type_sym;
            } else if (type_sym->type && type_sym->type->base_type &&
                       type_sym->type->base_type->sym) {
                target_sym = type_sym->type->base_type->sym;
            }
            if (target_sym) {
                zan_ast_list_t *init_args = &member->method_decl.base_args;
                struct zan_ctor_entry *target = find_ctor(
                    g, target_sym, init_args, locals, this_init ? member : NULL);
                zan_ast_list_t init_args_filled;
                if (!target &&
                    fill_ctor_default_args(g, target_sym, init_args,
                                           &init_args_filled)) {
                    struct zan_ctor_entry *dt = find_ctor(
                        g, target_sym, &init_args_filled, locals,
                        this_init ? member : NULL);
                    if (dt) {
                        target = dt;
                        init_args = &init_args_filled;
                    }
                }
                LLVMValueRef target_fn = target ? target->fn : NULL;
                LLVMTypeRef target_fn_type = target ? target->fn_type : NULL;
                if (target && g->cur_inst && target_sym == type_sym) {
                    LLVMValueRef specialized = find_generic_ctor(
                        g, target_sym, target->decl, g->cur_inst->type_args,
                        g->cur_inst->type_arg_count, &target_fn_type);
                    if (specialized) target_fn = specialized;
                }
                if (!target) {
                    if (this_init || member->method_decl.has_base_init) {
                        zan_diag_emit(g->diag, DIAG_ERROR, member->loc,
                            "no matching %s constructor with %d argument(s)",
                            this_init ? "this" : "base", init_args->count);
                    }
                } else {
                    LLVMValueRef thisv = LLVMBuildLoad2(
                        g->builder, param_types[0], this_alloca,
                        this_init ? "this.chain" : "this.base");
                    if (!this_init) {
                        LLVMTypeRef bst = get_struct_llvm_type(g, target_sym);
                        if (bst)
                            thisv = LLVMBuildBitCast(g->builder, thisv,
                                LLVMPointerType(bst, 0), "base.this");
                    }
                    int argc = init_args->count + 1;
                    LLVMValueRef *cargs = (LLVMValueRef *)malloc(
                        sizeof(LLVMValueRef) * (size_t)argc);
                    cargs[0] = thisv;
                    for (int ai = 0; ai < init_args->count; ai++) {
                        zan_ast_node_t *param =
                            target->decl->method_decl.params.items[ai];
                        zan_type_t *pt = zan_binder_resolve_type(
                            g->binder, param->param.type);
                        cargs[ai + 1] = emit_arg_typed(
                            g, init_args->items[ai], pt, locals);
                    }
                    coerce_args_to_params(g, target_fn_type, cargs, argc);
                    zan_call2(g->builder, target_fn_type, target_fn,
                              cargs, (unsigned)argc, "");
                    initializer_target_called = true;
                    for (int ai = 0; ai < init_args->count; ai++)
                        emit_release_owned_call_temp(
                            g, init_args->items[ai], cargs[ai + 1], locals);
                    free(cargs);
                }
            }
            if (!this_init) {
                LLVMValueRef thisv = LLVMBuildLoad2(
                    g->builder, param_types[0], this_alloca, "this.fields");
                zan_type_t *recv_type = g->cur_inst ? g->cur_inst : type_sym->type;
                zan_type_t *base_type = type_sym->type
                    ? type_sym->type->base_type : NULL;
                if (base_type && base_type->sym && !initializer_target_called) {
                    emit_implicit_field_initializers(
                        g, base_type->sym, base_type, thisv, locals);
                }
                emit_decl_field_initializers(
                    g, type_sym, recv_type, thisv, locals);
            }
        }

        /* emit method body */
        if (member->method_decl.body->kind == AST_BLOCK) {
            for (int k = 0; k < member->method_decl.body->block.stmts.count; k++) {
                emit_stmt(g, member->method_decl.body->block.stmts.items[k], locals);
            }
        } else {
            /* expression body (=> expr) */
            check_implicit_narrowing(g, g->current_fn_zan_ret_type,
                infer_expr_type(g, member->method_decl.body, locals),
                member->method_decl.body, "return");
            LLVMValueRef val = emit_expr(g, member->method_decl.body, locals);
            /* convert return type if needed */
            LLVMTypeRef val_t = LLVMTypeOf(val);
            if (val_t != llvm_ret) {
                if (LLVMGetTypeKind(llvm_ret) == LLVMFloatTypeKind &&
                    LLVMGetTypeKind(val_t) == LLVMDoubleTypeKind) {
                    val = LLVMBuildFPTrunc(g->builder, val, llvm_ret, "trunc");
                } else if (LLVMGetTypeKind(llvm_ret) == LLVMDoubleTypeKind &&
                           LLVMGetTypeKind(val_t) == LLVMFloatTypeKind) {
                    val = LLVMBuildFPExt(g->builder, val, llvm_ret, "ext");
                }
            }
            if (is_rc_managed_type(ret_type) &&
                !expr_yields_owned_rc_value(g, member->method_decl.body, locals)) {
                emit_rc_retain_for_type(g, ret_type, val);
            }
            emit_release_owned_locals(g, locals);
            LLVMBuildRet(g->builder, val);
        }

        /* ensure function has terminator */
        LLVMBasicBlockRef cur_bb = LLVMGetInsertBlock(g->builder);
        if (!LLVMGetBasicBlockTerminator(cur_bb)) {
            emit_release_owned_locals(g, locals);
            if (ret_type->kind == TYPE_VOID) {
                LLVMBuildRetVoid(g->builder);
            } else {
                LLVMBuildRet(g->builder, LLVMConstNull(llvm_ret));
            }
        }

        g->current_fn = saved_fn;
        g->current_fn_ret_type = saved_fn_ret;
        g->current_fn_zan_ret_type = saved_fn_zan_ret;
        g->throw_locals_base = saved_throw_base;
        g->catch_cleanup_count = saved_catch_cc;
        g->throw_catch_base = saved_throw_cb;
        g->finally_count = saved_fin_c;
        g->finally_loop_base = saved_fin_lb;
        g->eh_armed_count = saved_eh_c;
        g->eh_armed_base = saved_eh_b;
        g->eh_armed_loop_base = saved_eh_lb;
        g->current_this = saved_this;
        g->current_type_sym = saved_type_sym;
        g->current_fn_body = saved_fn_body;
        g->current_fn_no_runtime = false;
        free(param_types);
    }

    g->cur_inst = NULL;
    free(work);
    emit_pending_method_specs(g);
}

/* ---- method-level monomorphization (bodies for zan_method_spec) ----
 *
 * A generic method (one declaring its own <T,...>) is normally emitted once
 * with type parameters erased to a machine pointer, which loses type-specific
 * semantics: `<`/`==` on a string key compare pointers, double keys compare
 * raw bits, a replaced ARC value is never released, and an `int` argument or
 * a `T`-typed return travels as an address (`M<int>(7)` handed the callee 7 as
 * a pointer and the caller read the result back as one). Whenever a call site
 * can bind every type parameter to a concrete type, a specialized copy with a
 * concrete signature is declared here and its body emitted from the pending
 * queue; the erased variant only serves calls that cannot bind. */

/* ---- generic methods that reach into their own type parameters -----------
 * `c.Name()` or `c.id` where `c` is declared with one of the method's own type
 * parameters has no erased form: without a binding for T the member cannot be
 * resolved, and the erased body silently fell through to a default value --
 * miscompiled IR (`ret i32 0` out of a pointer-returning function), rejected
 * by the LLVM verifier. Such a method is a pure template: no erased instance
 * is emitted for it and every call site must monomorphize (A7-1). */

typedef struct {
    zan_ast_list_t *tps;
    zan_istr_t      names[32];  /* locals/params whose declared type is a tp */
    int             count;
    zan_istr_t      fields[32]; /* fields of the enclosing type, declared as a tp */
    int             field_count;
    bool            found;
} tp_use_scan_t;

static bool tp_istr_eq(zan_istr_t a, zan_istr_t b) {
    return a.len == b.len && a.str && b.str &&
           memcmp(a.str, b.str, (size_t)a.len) == 0;
}

/* `T` / `T[]` / `List<T>`: a type reference that names a type parameter. */
static bool tp_typeref_is_tp(tp_use_scan_t *s, zan_ast_node_t *tref) {
    if (!tref || tref->kind != AST_TYPE_REF) return false;
    for (int i = 0; i < s->tps->count; i++)
        if (tp_istr_eq(s->tps->items[i]->ident.name, tref->type_ref.name))
            return true;
    return false;
}

static void tp_scan_bind(tp_use_scan_t *s, zan_istr_t name) {
    if (s->count < (int)(sizeof(s->names) / sizeof(s->names[0])))
        s->names[s->count++] = name;
}

static bool tp_scan_is_tp_field(tp_use_scan_t *s, zan_istr_t name) {
    for (int i = 0; i < s->field_count; i++)
        if (tp_istr_eq(s->fields[i], name)) return true;
    return false;
}

/* `c` (a local/parameter of type T), `item` / `this.item` (a field of type T). */
static bool tp_scan_is_tp_value(tp_use_scan_t *s, zan_ast_node_t *e) {
    if (!e) return false;
    if (e->kind == AST_IDENTIFIER) {
        for (int i = 0; i < s->count; i++)
            if (tp_istr_eq(s->names[i], e->ident.name)) return true;
        return tp_scan_is_tp_field(s, e->ident.name);
    }
    if (e->kind == AST_MEMBER_ACCESS && e->member.object &&
        e->member.object->kind == AST_THIS_EXPR)
        return tp_scan_is_tp_field(s, e->member.name);
    return false;
}

static void tp_scan_stmt(tp_use_scan_t *s, zan_ast_node_t *st);

static void tp_scan_expr(tp_use_scan_t *s, zan_ast_node_t *e) {
    if (!e || s->found) return;
    switch (e->kind) {
    case AST_MEMBER_ACCESS:
        if (tp_scan_is_tp_value(s, e->member.object)) { s->found = true; return; }
        tp_scan_expr(s, e->member.object);
        return;
    case AST_INDEX:
        if (tp_scan_is_tp_value(s, e->index.object)) { s->found = true; return; }
        tp_scan_expr(s, e->index.object);
        tp_scan_expr(s, e->index.index);
        return;
    case AST_CALL:
        /* `Other<T>(x)`: forwarding one of our own type parameters to another
         * generic call has no erased form either -- the callee is itself a
         * template, so this body only exists monomorphized */
        for (int i = 0; i < e->call.type_args.count; i++)
            if (tp_typeref_is_tp(s, e->call.type_args.items[i])) {
                s->found = true;
                return;
            }
        tp_scan_expr(s, e->call.callee);
        for (int i = 0; i < e->call.args.count; i++)
            tp_scan_expr(s, e->call.args.items[i]);
        return;
    case AST_BINARY:
    case AST_ASSIGNMENT:
        tp_scan_expr(s, e->binary.left);
        tp_scan_expr(s, e->binary.right);
        return;
    case AST_UNARY:
    case AST_POSTFIX_UNARY: tp_scan_expr(s, e->unary.operand); return;
    case AST_CONDITIONAL:
        tp_scan_expr(s, e->conditional.cond);
        tp_scan_expr(s, e->conditional.then_expr);
        tp_scan_expr(s, e->conditional.else_expr);
        return;
    case AST_NEW_EXPR:
        for (int i = 0; i < e->new_expr.args.count; i++)
            tp_scan_expr(s, e->new_expr.args.items[i]);
        return;
    case AST_CAST_EXPR:  tp_scan_expr(s, e->cast.expr); return;
    case AST_IS_EXPR:
    case AST_AS_EXPR:    tp_scan_expr(s, e->type_test.expr); return;
    case AST_AWAIT_EXPR: tp_scan_expr(s, e->await_expr.expr); return;
    case AST_LAMBDA:     tp_scan_stmt(s, e->lambda.body); return;
    case AST_STRING_INTERP:
        for (int i = 0; i < e->string_interp.parts.count; i++)
            tp_scan_expr(s, e->string_interp.parts.items[i]);
        return;
    default: return;
    }
}

static void tp_scan_stmt(tp_use_scan_t *s, zan_ast_node_t *st) {
    if (!st || s->found) return;
    switch (st->kind) {
    case AST_BLOCK:
        for (int i = 0; i < st->block.stmts.count; i++)
            tp_scan_stmt(s, st->block.stmts.items[i]);
        return;
    case AST_VAR_DECL:
        if (tp_typeref_is_tp(s, st->var_decl.type)) tp_scan_bind(s, st->var_decl.name);
        tp_scan_expr(s, st->var_decl.initializer);
        return;
    case AST_EXPR_STMT:   tp_scan_expr(s, st->expr_stmt.expr); return;
    case AST_RETURN_STMT: tp_scan_expr(s, st->ret.value); return;
    case AST_THROW_STMT:  tp_scan_expr(s, st->throw_stmt.value); return;
    case AST_IF_STMT:
        tp_scan_expr(s, st->if_stmt.cond);
        tp_scan_stmt(s, st->if_stmt.then_body);
        tp_scan_stmt(s, st->if_stmt.else_body);
        return;
    case AST_WHILE_STMT:
    case AST_DO_WHILE_STMT:
        tp_scan_expr(s, st->while_stmt.cond);
        tp_scan_stmt(s, st->while_stmt.body);
        return;
    case AST_FOR_STMT:
        tp_scan_stmt(s, st->for_stmt.init);
        tp_scan_expr(s, st->for_stmt.cond);
        tp_scan_stmt(s, st->for_stmt.step);
        tp_scan_stmt(s, st->for_stmt.body);
        return;
    case AST_FOREACH_STMT:
        if (tp_typeref_is_tp(s, st->foreach_stmt.var_type))
            tp_scan_bind(s, st->foreach_stmt.var_name);
        tp_scan_expr(s, st->foreach_stmt.collection);
        tp_scan_stmt(s, st->foreach_stmt.body);
        return;
    case AST_TRY_STMT:
        tp_scan_stmt(s, st->try_stmt.try_body);
        for (int i = 0; i < st->try_stmt.catches.count; i++)
            tp_scan_stmt(s, st->try_stmt.catches.items[i]->catch_clause.body);
        tp_scan_stmt(s, st->try_stmt.finally_body);
        return;
    case AST_SWITCH_STMT:
        tp_scan_expr(s, st->switch_stmt.expr);
        for (int i = 0; i < st->switch_stmt.cases.count; i++)
            tp_scan_stmt(s, st->switch_stmt.cases.items[i]->switch_case.body);
        return;
    case AST_LOCK_STMT:
        tp_scan_expr(s, st->while_stmt.cond);
        tp_scan_stmt(s, st->while_stmt.body);
        return;
    default:
        tp_scan_expr(s, st);
        return;
    }
}

/* True when this generic method's body reaches through a value of one of its
 * own type parameters, i.e. it only makes sense monomorphized. */
static bool method_is_tp_template(zan_ast_node_t *member) {
    if (!member || member->kind != AST_METHOD_DECL) return false;
    zan_ast_list_t *tps = &member->method_decl.type_params;
    if (tps->count == 0 || !member->method_decl.body) return false;
    tp_use_scan_t s;
    memset(&s, 0, sizeof(s));
    s.tps = tps;
    for (int i = 0; i < member->method_decl.params.count; i++) {
        zan_ast_node_t *p = member->method_decl.params.items[i];
        if (p && tp_typeref_is_tp(&s, p->param.type)) tp_scan_bind(&s, p->param.name);
    }
    if (s.count == 0) return false;
    tp_scan_stmt(&s, member->method_decl.body);
    return s.found;
}

/* Same question for a member of a generic *class*: does its body reach through
 * a value of one of the class's type parameters (a T field, a T parameter, a T
 * local)? The erased variant of such a body has nothing to resolve the member
 * against, so only the per-instantiation variants are emittable. */
static bool class_member_uses_tp(zan_ast_node_t *decl, zan_ast_node_t *member) {
    if (!decl || !member) return false;
    zan_ast_list_t *tps = &decl->type_decl.type_params;
    if (tps->count == 0) return false;
    zan_ast_node_t *body = NULL;
    zan_ast_list_t *params = NULL;
    if (member->kind == AST_METHOD_DECL || member->kind == AST_CONSTRUCTOR_DECL) {
        body = member->method_decl.body;
        params = &member->method_decl.params;
    }
    if (!body) return false;
    tp_use_scan_t s;
    memset(&s, 0, sizeof(s));
    s.tps = tps;
    for (int i = 0; i < decl->type_decl.members.count; i++) {
        zan_ast_node_t *f = decl->type_decl.members.items[i];
        if (f && f->kind == AST_FIELD_DECL && tp_typeref_is_tp(&s, f->field_decl.type) &&
            s.field_count < (int)(sizeof(s.fields) / sizeof(s.fields[0])))
            s.fields[s.field_count++] = f->field_decl.name;
    }
    for (int i = 0; params && i < params->count; i++) {
        zan_ast_node_t *p = params->items[i];
        if (p && tp_typeref_is_tp(&s, p->param.type)) tp_scan_bind(&s, p->param.name);
    }
    if (s.count == 0 && s.field_count == 0) return false;
    tp_scan_stmt(&s, body);
    return s.found;
}

/* Two specializations share a declaring-type instantiation when they were
 * reached through the same class type arguments (`Pool<string>` vs
 * `Pool<int>`); a non-generic declaring type has none. */
static bool mspec_owner_eq(zan_type_t *a, zan_type_t *b) {
    if (a == b) return true;
    if (!a || !b || a->sym != b->sym) return false;
    return type_arglists_equal(a->type_args, a->type_arg_count,
                               b->type_args, b->type_arg_count);
}

static int get_or_create_method_spec(zan_irgen_t *g, zan_symbol_t *msym,
                                     zan_type_t **bind, int bindc,
                                     zan_type_t *owner_inst) {
    if (!msym || !msym->decl || msym->decl->kind != AST_METHOD_DECL) return -1;
    zan_ast_node_t *member = msym->decl;
    if (!member->method_decl.body) return -1;
    /* an async body lowers to a ramp/resume/frame triple; it specializes the
     * same way, with the whole frame layout built from the bound types */
    bool spec_async = (member->method_decl.modifiers & MOD_ASYNC) != 0;
    bool spec_static = (member->method_decl.modifiers & MOD_STATIC) != 0;
    zan_ast_list_t *tps = &member->method_decl.type_params;
    if (tps->count != bindc || bindc <= 0 || bindc > 8) return -1;
    for (int i = 0; i < bindc; i++)
        if (!bind[i] || !type_is_concrete(bind[i])) return -1;
    zan_symbol_t *type_sym = msym->parent;
    if (!type_sym ||
        (type_sym->kind != SYM_CLASS && type_sym->kind != SYM_STRUCT))
        return -1;
    /* An instance generic method specializes the same way a static one does,
     * with `this` prepended to the signature. When the declaring type is
     * itself generic the specialization is additionally keyed on the receiver's
     * instantiation, which supplies the class type parameters to every type
     * resolved out of the body (A32-3a). */
    if (!is_user_generic_sym(type_sym)) owner_inst = NULL;
    else if (!spec_static) {
        if (!owner_inst || owner_inst->sym != type_sym ||
            !type_is_concrete(owner_inst))
            return -1;
    } else {
        owner_inst = NULL;
    }
    int this_off = spec_static ? 0 : 1;

    for (int i = 0; i < g->method_spec_count; i++)
        if (g->method_specs[i].msym == msym &&
            mspec_owner_eq(g->method_specs[i].owner_inst, owner_inst) &&
            type_arglists_equal(g->method_specs[i].bind,
                                g->method_specs[i].bindc, bind, bindc))
            return i;

    /* mangled name: Type_Method$$tok1$tok2, uniquified across overloads */
    char fn_name[512];
    {
        char osuffix[256];
        osuffix[0] = '\0';
        if (owner_inst) mangle_inst_suffix(osuffix, sizeof(osuffix), owner_inst);
        size_t off = (size_t)snprintf(fn_name, sizeof(fn_name), "%.*s%s_%.*s$",
            (int)type_sym->name.len, type_sym->name.str, osuffix,
            (int)msym->name.len, msym->name.str);
        for (int i = 0; i < bindc; i++) {
            if (off < sizeof(fn_name) - 1) fn_name[off++] = '$';
            fn_name[off < sizeof(fn_name) ? off : sizeof(fn_name) - 1] = '\0';
            mangle_type_token(fn_name, sizeof(fn_name), &off, bind[i]);
        }
        if (off < sizeof(fn_name)) fn_name[off] = '\0';
        else fn_name[sizeof(fn_name) - 1] = '\0';
        char base_name[512];
        int oi = 2;
        snprintf(base_name, sizeof(base_name), "%s", fn_name);
        while (LLVMGetNamedFunction(g->mod, fn_name))
            snprintf(fn_name, sizeof(fn_name), "%s$o%d", base_name, oi++);
    }

    int param_count = member->method_decl.params.count;
    int total_params = param_count + this_off;
    LLVMTypeRef *param_types = (LLVMTypeRef *)calloc(
        (size_t)(total_params > 0 ? total_params : 1), sizeof(LLVMTypeRef));
    if (this_off) {
        LLVMTypeRef st = get_struct_llvm_type(g, type_sym);
        param_types[0] = st ? LLVMPointerType(st, 0)
                            : LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    }
    for (int k = 0; k < param_count; k++) {
        zan_ast_node_t *param = member->method_decl.params.items[k];
        zan_type_t *pt = subst_method_tp(g,
            zan_binder_resolve_type(g->binder, param->param.type), tps, bind);
        if (owner_inst) pt = subst_type_param_deep(g, pt, owner_inst);
        param_types[k + this_off] = param->param.by_ref
            ? LLVMPointerType(map_type(g, pt), 0)
            : map_type(g, pt);
    }
    zan_type_t *ret_type = member->method_decl.return_type
        ? subst_method_tp(g, zan_binder_resolve_type(g->binder,
              member->method_decl.return_type), tps, bind)
        : g->binder->type_void;
    if (owner_inst) ret_type = subst_type_param_deep(g, ret_type, owner_inst);

    zan_type_t **bcopy = (zan_type_t **)zan_arena_alloc(g->arena,
        sizeof(zan_type_t *) * (size_t)bindc);
    for (int i = 0; i < bindc; i++) bcopy[i] = bind[i];

    LLVMTypeRef fn_type = NULL;
    LLVMValueRef fn = NULL;
    method_body_work_t *air = NULL;
    if (spec_async) {
        /* the ramp/resume/frame are built with the bindings active, so params,
         * frame-resident locals and the result all use concrete types */
        air = (method_body_work_t *)zan_arena_alloc(g->arena, sizeof(*air));
        memset(air, 0, sizeof(*air));
        air->member = member;
        air->type_sym = type_sym;
        air->is_static = spec_static;
        air->param_types = param_types;   /* owned by the work item */
        air->param_count = param_count;
        air->param_offset = this_off;
        air->llvm_ret = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
        air->ret_type = ret_type;
        air->cur_inst = owner_inst;
        air->mtps = tps;
        air->mbind = bcopy;
        zan_ast_list_t *saved_mtps = g->cur_mtps;
        zan_type_t **saved_mbind = g->cur_mbind;
        zan_type_t *saved_inst = g->cur_inst;
        g->cur_mtps = tps;
        g->cur_mbind = bcopy;
        g->cur_inst = owner_inst;
        declare_async_method(g, air, fn_name);
        g->cur_mtps = saved_mtps;
        g->cur_mbind = saved_mbind;
        g->cur_inst = saved_inst;
        fn = air->fn;
        fn_type = air->fn_type;
    } else {
        fn_type = LLVMFunctionType(map_type(g, ret_type), param_types,
                                   (unsigned)total_params, 0);
        fn = LLVMAddFunction(g->mod, fn_name, fn_type);
        zan_set_module_local(fn);
        free(param_types);
    }

    if (g->method_spec_count >= g->method_spec_cap) {
        int ncap = g->method_spec_cap ? g->method_spec_cap * 2 : 32;
        g->method_specs = realloc(g->method_specs,
                                  (size_t)ncap * sizeof(*g->method_specs));
        g->method_spec_cap = ncap;
    }
    int idx = g->method_spec_count++;
    g->method_specs[idx].msym = msym;
    g->method_specs[idx].type_sym = type_sym;
    g->method_specs[idx].owner_inst = owner_inst;
    g->method_specs[idx].member = member;
    g->method_specs[idx].bind = bcopy;
    g->method_specs[idx].bindc = bindc;
    g->method_specs[idx].fn = fn;
    g->method_specs[idx].fn_type = fn_type;
    g->method_specs[idx].is_async = spec_async;
    g->method_specs[idx].async_ir = air;
    return idx;
}

/* Emit the body of one specialization: the static, non-async subset of
 * emit_user_methods Pass B, with the type-parameter bindings active so every
 * type ref resolved from this body comes out concrete (resolve_type_ctx). */
static void emit_method_spec_body(zan_irgen_t *g, int idx) {
    struct zan_method_spec sp = g->method_specs[idx];
    zan_ast_node_t *member = sp.member;
    zan_ast_list_t *tps = &member->method_decl.type_params;

    if (sp.is_async) {
        emit_async_method_ir(g, (method_body_work_t *)sp.async_ir);
        return;
    }

    zan_ast_list_t *saved_mtps = g->cur_mtps;
    zan_type_t **saved_mbind = g->cur_mbind;
    zan_type_t *saved_inst = g->cur_inst;
    g->cur_mtps = tps;
    g->cur_mbind = sp.bind;
    g->cur_inst = sp.owner_inst;

    LLVMBasicBlockRef saved_bb = LLVMGetInsertBlock(g->builder);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, sp.fn, "entry");
    LLVMPositionBuilderAtEnd(g->builder, entry);
    /* -g: see emit_user_methods Pass B -- anchor to the decl line so the
     * specialization's prologue (incl. any chained ctor call) gets a valid
     * in-scope !dbg before per-statement locations take over. */
    di_set_loc(g, member->loc);

    local_scope_t *locals = local_scope_new(g->arena);
    int param_count = member->method_decl.params.count;
    unsigned npt = LLVMCountParamTypes(sp.fn_type);
    int this_off = (member->method_decl.modifiers & MOD_STATIC) ? 0 : 1;
    LLVMTypeRef *param_types = (LLVMTypeRef *)calloc(
        (size_t)(npt > 0 ? npt : 1), sizeof(LLVMTypeRef));
    LLVMGetParamTypes(sp.fn_type, param_types);
    LLVMValueRef spec_this = NULL;
    if (this_off) {
        spec_this = LLVMBuildAlloca(g->builder, param_types[0], "this");
        LLVMBuildStore(g->builder, LLVMGetParam(sp.fn, 0), spec_this);
    }
    for (int k = 0; k < param_count; k++) {
        zan_ast_node_t *param = member->method_decl.params.items[k];
        zan_type_t *pt = resolve_type_ctx(g, param->param.type);
        unsigned pi = (unsigned)(k + this_off);
        if (param->param.by_ref) {
            local_add(locals, param->param.name, LLVMGetParam(sp.fn, pi), pt);
            if (pt && pt->kind == TYPE_STRING)
                locals->vars[locals->count - 1].opaque_string = 1;
            if (is_rc_managed_type(pt))
                locals->vars[locals->count - 1].byref_slot =
                    locals->vars[locals->count - 1].arc_owned = 1;
            continue;
        }
        LLVMValueRef param_alloca = LLVMBuildAlloca(g->builder, param_types[pi], "p");
        LLVMBuildStore(g->builder, LLVMGetParam(sp.fn, pi), param_alloca);
        local_add(locals, param->param.name, param_alloca, pt);
    }
    free(param_types);

    zan_type_t *ret_type = member->method_decl.return_type
        ? resolve_type_ctx(g, member->method_decl.return_type)
        : g->binder->type_void;
    LLVMTypeRef llvm_ret = LLVMGetReturnType(sp.fn_type);

    LLVMValueRef saved_fn = g->current_fn;
    LLVMTypeRef saved_fn_ret = g->current_fn_ret_type;
    zan_type_t *saved_fn_zan_ret = g->current_fn_zan_ret_type;
    LLVMValueRef saved_this = g->current_this;
    zan_symbol_t *saved_type_sym = g->current_type_sym;
    zan_ast_node_t *saved_fn_body = g->current_fn_body;
    g->current_fn = sp.fn;
    g->current_fn_ret_type = llvm_ret;
    g->current_fn_zan_ret_type = ret_type;
    int saved_throw_base = g->throw_locals_base;
    int saved_catch_cc = g->catch_cleanup_count;
    int saved_throw_cb = g->throw_catch_base;
    int saved_fin_c = g->finally_count;
    int saved_fin_lb = g->finally_loop_base;
    int saved_eh_c = g->eh_armed_count;
    int saved_eh_b = g->eh_armed_base;
    int saved_eh_lb = g->eh_armed_loop_base;
    g->throw_locals_base = 0;
    g->catch_cleanup_count = 0;
    g->throw_catch_base = 0;
    g->finally_count = 0;
    g->finally_loop_base = 0;
    g->eh_armed_base = g->eh_armed_count;
    g->eh_armed_loop_base = g->eh_armed_count;
    g->current_this = spec_this;
    g->current_type_sym = sp.type_sym;
    g->current_fn_body = member->method_decl.body;

    if (member->method_decl.body->kind == AST_BLOCK) {
        for (int k = 0; k < member->method_decl.body->block.stmts.count; k++) {
            emit_stmt(g, member->method_decl.body->block.stmts.items[k], locals);
        }
    } else {
        check_implicit_narrowing(g, g->current_fn_zan_ret_type,
            infer_expr_type(g, member->method_decl.body, locals),
            member->method_decl.body, "return");
        LLVMValueRef val = emit_expr(g, member->method_decl.body, locals);
        LLVMTypeRef val_t = LLVMTypeOf(val);
        if (val_t != llvm_ret) {
            if (LLVMGetTypeKind(llvm_ret) == LLVMFloatTypeKind &&
                LLVMGetTypeKind(val_t) == LLVMDoubleTypeKind) {
                val = LLVMBuildFPTrunc(g->builder, val, llvm_ret, "trunc");
            } else if (LLVMGetTypeKind(llvm_ret) == LLVMDoubleTypeKind &&
                       LLVMGetTypeKind(val_t) == LLVMFloatTypeKind) {
                val = LLVMBuildFPExt(g->builder, val, llvm_ret, "ext");
            } else if (LLVMGetTypeKind(llvm_ret) == LLVMStructTypeKind &&
                       LLVMGetTypeKind(val_t) == LLVMPointerTypeKind) {
                /* value struct return: `Pair Make() => new Pair(3, 4);` */
                val = LLVMBuildLoad2(g->builder, llvm_ret, val, "ret.struct");
            }
        }
        if (is_rc_managed_type(ret_type) &&
            !expr_yields_owned_rc_value(g, member->method_decl.body, locals)) {
            emit_rc_retain_for_type(g, ret_type, val);
        }
        emit_release_owned_locals(g, locals);
        LLVMBuildRet(g->builder, val);
    }

    LLVMBasicBlockRef cur_bb = LLVMGetInsertBlock(g->builder);
    if (!LLVMGetBasicBlockTerminator(cur_bb)) {
        emit_release_owned_locals(g, locals);
        if (ret_type->kind == TYPE_VOID) {
            LLVMBuildRetVoid(g->builder);
        } else {
            LLVMBuildRet(g->builder, LLVMConstNull(llvm_ret));
        }
    }

    g->current_fn = saved_fn;
    g->current_fn_ret_type = saved_fn_ret;
    g->current_fn_zan_ret_type = saved_fn_zan_ret;
    g->throw_locals_base = saved_throw_base;
    g->catch_cleanup_count = saved_catch_cc;
    g->throw_catch_base = saved_throw_cb;
    g->finally_count = saved_fin_c;
    g->finally_loop_base = saved_fin_lb;
    g->eh_armed_count = saved_eh_c;
    g->eh_armed_base = saved_eh_b;
    g->eh_armed_loop_base = saved_eh_lb;
    g->current_this = saved_this;
    g->current_type_sym = saved_type_sym;
    g->current_fn_body = saved_fn_body;
    g->cur_mtps = saved_mtps;
    g->cur_mbind = saved_mbind;
    g->cur_inst = saved_inst;
    if (saved_bb) LLVMPositionBuilderAtEnd(g->builder, saved_bb);
}

/* Drain the queue; emitting a body may enqueue further specializations. */
static void emit_pending_method_specs(zan_irgen_t *g) {
    while (g->method_spec_emitted < g->method_spec_count) {
        int i = g->method_spec_emitted++;
        emit_method_spec_body(g, i);
    }
}

/* A minimal Windows DLL entry point: `BOOL DllMain(...) { return TRUE; }`.
 * The loader passes (hinstDLL, reason, reserved) but a zero-arg function
 * ignores them; the non-zero return keeps the process running. External
 * linkage keeps it alive through GlobalDCE and lets the linker resolve
 * `-e DllMain`. */
static void emit_windows_dll_main(zan_irgen_t *g) {
    LLVMValueRef existing = LLVMGetNamedFunction(g->mod, "DllMain");
    if (existing) return;
    LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef ft = LLVMFunctionType(i32, NULL, 0, 0);
    LLVMValueRef fn = LLVMAddFunction(g->mod, "DllMain", ft);
    LLVMSetLinkage(fn, LLVMExternalLinkage);
    LLVMBasicBlockRef bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
    LLVMPositionBuilderAtEnd(g->builder, bb);
    LLVMBuildRet(g->builder, LLVMConstInt(i32, 1, 0));
}

zan_status_t zan_irgen_emit(zan_irgen_t *g, zan_ast_node_t *unit) {
    if (!unit || unit->kind != AST_COMPILATION_UNIT) return ZAN_ERROR;
    g_di_emit_ctx = g; /* so local_add can forward variables to di_declare_var */

    /* Instantiations first: a generic class's field slots are sized from the
     * concrete types bound to its type parameters, so they must be known
     * before the layout below is fixed. */
    discover_generic_insts(g, unit);

    /* Pass 1: register all struct/class types */
    for (int i = 0; i < unit->comp_unit.decls.count; i++) {
        zan_ast_node_t *decl = unit->comp_unit.decls.items[i];
        if (decl->kind == AST_STRUCT_DECL || decl->kind == AST_CLASS_DECL) {
            zan_symbol_t *sym = zan_binder_lookup(g->binder, decl->type_decl.name);
            if (sym) register_struct_type(g, sym);
        }
    }

    /* Pass 2: emit user-defined methods */
    emit_user_methods(g, unit);

    /* Pass 3: find and emit static Main method */
    for (int i = 0; i < unit->comp_unit.decls.count; i++) {
        zan_ast_node_t *decl = unit->comp_unit.decls.items[i];
        if (decl->kind != AST_CLASS_DECL && decl->kind != AST_STRUCT_DECL) continue;

        for (int j = 0; j < decl->type_decl.members.count; j++) {
            zan_ast_node_t *member = decl->type_decl.members.items[j];
            if (member->kind == AST_METHOD_DECL &&
                (member->method_decl.modifiers & MOD_STATIC) &&
                member->method_decl.name.len == 4 &&
                memcmp(member->method_decl.name.str, "Main", 4) == 0) {
                {
                    zan_symbol_t *main_type_sym = zan_binder_lookup(g->binder, decl->type_decl.name);
                    emit_main_method(g, member, main_type_sym, unit);
                }
                goto done;
            }
        }
    }
done:
    ;
    /* Main may have created method specializations too. */
    emit_pending_method_specs(g);
    /* Synthesise per-class release functions now that every class type has
     * been registered (Pass 1) and referenced (Passes 2/3). */
    di_clear(g); /* the following are synthetic fns; no user source scope */
    emit_all_class_releases(g);
    emit_site_dtor_table(g);
    emit_site_tyname_table(g);
    emit_site_meta_table(g);
    emit_vtables(g);
    /* Reflected method/constructor tables: their records point at the real
     * functions and at thunks over them, so they can only be filled in once
     * every function, specialization and vtable above exists. */
    refl_finalize_mtabs(g);
    /* A Windows DLL must carry a real entry point: without one the PE
     * AddressOfEntryPoint falls back to the start of .text, so the Windows
     * loader calls the first exported function (often zan_rt_println) as
     * DllMain on process attach/detach, printing garbage to stdout (the
     * "MZ" PE magic of the image base it was handed as hinstDLL). Emit a
     * minimal DllMain returning TRUE (non-zero) and link with `-e DllMain`. */
    if (g->emit_lib && g->emit_shared && g->target_is_windows)
        emit_windows_dll_main(g);
    /* --publish: emit the .ctors constructor that un-scrambles string literals
     * (no-op unless obfuscation is on and literals were recorded). */
    zan_irgen_emit_string_deobf(g);
    /* An error diagnostic emitted during codegen (e.g. an unsupported await
     * form flagged by the ANF pass) must fail the build — the driver only
     * checks diagnostics before codegen, so surface it here. */
    if (zan_diag_has_errors(g->diag)) {
        return ZAN_ERROR;
    }
    /* Finalize DWARF metadata (resolves temporary nodes) before verification. */
    if (g->emit_debug && g->di_builder) {
        LLVMSetCurrentDebugLocation2(g->builder, NULL);
        LLVMDIBuilderFinalize(g->di_builder);
    }
    /* verify module */
    char *error = NULL;
    if (LLVMVerifyModule(g->mod, LLVMReturnStatusAction, &error)) {
        /* The module-level message quotes the offending instruction but not
         * the function it sits in, which is what one actually needs to find
         * the source. Re-verify per function to name it. */
        const char *culprit = NULL;
        for (LLVMValueRef fn = LLVMGetFirstFunction(g->mod); fn;
             fn = LLVMGetNextFunction(fn)) {
            if (LLVMIsDeclaration(fn)) continue;
            if (LLVMVerifyFunction(fn, LLVMReturnStatusAction)) {
                culprit = LLVMGetValueName(fn);
                break;
            }
        }
        zan_diag_emit(g->diag, DIAG_ERROR, zan_loc(0, 0, 0, 0),
                      "LLVM verification failed%s%s: %s",
                      culprit ? " in " : "", culprit ? culprit : "", error);
        LLVMDisposeMessage(error);
        return ZAN_ERROR;
    }
    if (error) LLVMDisposeMessage(error);

    return ZAN_OK;
}

/* ---- output ---- */

zan_status_t zan_irgen_write_ir(zan_irgen_t *g, const char *path) {
    char *ir = LLVMPrintModuleToString(g->mod);
    if (!path) {
        int rc = fputs(ir, stdout);
        LLVMDisposeMessage(ir);
        return rc < 0 ? ZAN_ERROR : ZAN_OK;
    }
    FILE *f = fopen(path, "w");
    if (!f) {
        LLVMDisposeMessage(ir);
        return ZAN_ERROR;
    }
    /* Report short writes (e.g. a full disk) instead of claiming success and
     * leaving a truncated .ll behind. */
    bool ok = (fputs(ir, f) >= 0);
    if (fclose(f) != 0) ok = false;
    LLVMDisposeMessage(ir);
    return ok ? ZAN_OK : ZAN_ERROR;
}

int zan_irgen_stub_extern_lib(zan_irgen_t *g, const char *lib, int lib_len) {
    int stubbed = 0;
    for (int i = 0; i < g->extern_fn_count; i++) {
        if ((int)g->extern_fns[i].lib.len != lib_len ||
            memcmp(g->extern_fns[i].lib.str, lib, (size_t)lib_len) != 0)
            continue;
        char nm[256];
        snprintf(nm, sizeof(nm), "%.*s", (int)g->extern_fns[i].name.len,
                 g->extern_fns[i].name.str);
        LLVMValueRef fn = LLVMGetNamedFunction(g->mod, nm);
        if (!fn) continue; /* optimized away: nothing references it */
        if (LLVMCountBasicBlocks(fn) > 0) continue; /* already defined */
        LLVMBasicBlockRef bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
        LLVMBuilderRef b = LLVMCreateBuilderInContext(g->ctx);
        LLVMPositionBuilderAtEnd(b, bb);
        LLVMTypeRef ft = LLVMGlobalGetValueType(fn);
        LLVMTypeRef rt = LLVMGetReturnType(ft);
        switch (LLVMGetTypeKind(rt)) {
        case LLVMVoidTypeKind:
            LLVMBuildRetVoid(b);
            break;
        case LLVMIntegerTypeKind:
            /* -1 doubles as SQL_ERROR / a generic nonzero failure code, but a
             * pointer-wide result is a handle (LoadLibrary, GetProcAddress,
             * ...) that callers null-check and otherwise call through, so -1
             * there turns "stubbed" into a jump to 0xffff...ffff. */
            LLVMBuildRet(b, LLVMConstInt(rt,
                LLVMGetIntTypeWidth(rt) >= 64 ? 0 : (unsigned long long)-1, 1));
            break;
        case LLVMFloatTypeKind:
        case LLVMDoubleTypeKind:
            LLVMBuildRet(b, LLVMConstReal(rt, 0.0));
            break;
        default:
            LLVMBuildRet(b, LLVMConstNull(rt));
            break;
        }
        LLVMDisposeBuilder(b);
        stubbed++;
    }
    return stubbed;
}

int zan_irgen_prune_extern_libs(zan_irgen_t *g) {
    int kept = 0, dropped = 0;
    for (int li = 0; li < g->extern_lib_count; li++) {
        const char *lib = g->extern_libs[li].str;
        int lib_len = (int)g->extern_libs[li].len;
        int live = 0;
        for (int i = 0; i < g->extern_fn_count && !live; i++) {
            if ((int)g->extern_fns[i].lib.len != lib_len ||
                memcmp(g->extern_fns[i].lib.str, lib, (size_t)lib_len) != 0)
                continue;
            char nm[256];
            snprintf(nm, sizeof(nm), "%.*s", (int)g->extern_fns[i].name.len,
                     g->extern_fns[i].name.str);
            /* The sweep erases declarations nothing references, so a surviving
             * declaration (or a definition, e.g. an already-stubbed one) is
             * what makes the library needed at link time. */
            if (LLVMGetNamedFunction(g->mod, nm)) live = 1;
        }
        /* A library with no recorded imports at all is not ours to judge (the
         * declarations may have been registered elsewhere): keep it. */
        if (!live) {
            int has_fns = 0;
            for (int i = 0; i < g->extern_fn_count && !has_fns; i++) {
                if ((int)g->extern_fns[i].lib.len == lib_len &&
                    memcmp(g->extern_fns[i].lib.str, lib, (size_t)lib_len) == 0)
                    has_fns = 1;
            }
            if (!has_fns) live = 1;
        }
        if (live) {
            g->extern_libs[kept++] = g->extern_libs[li];
        } else {
            dropped++;
        }
    }
    g->extern_lib_count = kept;
    return dropped;
}

bool zan_irgen_defines_prefix(zan_irgen_t *g, const char *prefix) {
    size_t plen = strlen(prefix);
    if (!plen) return false;
    for (LLVMValueRef fn = LLVMGetFirstFunction(g->mod); fn;
         fn = LLVMGetNextFunction(fn)) {
        if (LLVMCountBasicBlocks(fn) == 0) continue; /* declaration only */
        size_t nlen = 0;
        const char *nm = LLVMGetValueName2(fn, &nlen);
        if (nm && nlen >= plen && memcmp(nm, prefix, plen) == 0) return true;
    }
    return false;
}

/* wasm32 libc adapters (see zan_irgen_write_obj): define `fn` (which must be
 * a body-less function of type src_ft) as a thin wrapper that converts its
 * arguments to `lft` (the real 32-bit libc signature), calls `real`, and
 * converts the result back. Conversions are int<->ptr and int-width casts;
 * wasm pointers fit in 32 bits so the i64 handles Zan uses are safe to
 * truncate. */
static void w32_build_adapter_into(zan_irgen_t *g, LLVMValueRef fn,
                                   LLVMValueRef real, LLVMTypeRef lft) {
    LLVMTypeRef src_ft = LLVMGlobalGetValueType(fn);
    unsigned nparams = LLVMCountParamTypes(src_ft);
    if (nparams > 8 || LLVMCountParamTypes(lft) != nparams) return;
    LLVMTypeRef sps[8], lps[8];
    LLVMGetParamTypes(src_ft, sps);
    LLVMGetParamTypes(lft, lps);
    LLVMBasicBlockRef bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
    LLVMBuilderRef b = LLVMCreateBuilderInContext(g->ctx);
    LLVMPositionBuilderAtEnd(b, bb);
    LLVMValueRef args[8];
    for (unsigned p = 0; p < nparams; p++) {
        LLVMValueRef a = LLVMGetParam(fn, p);
        LLVMTypeKind sk = LLVMGetTypeKind(sps[p]);
        LLVMTypeKind lk = LLVMGetTypeKind(lps[p]);
        if (sps[p] == lps[p]) {
            args[p] = a;
        } else if (sk == LLVMIntegerTypeKind && lk == LLVMPointerTypeKind) {
            args[p] = LLVMBuildIntToPtr(b, a, lps[p], "");
        } else if (sk == LLVMPointerTypeKind && lk == LLVMIntegerTypeKind) {
            args[p] = LLVMBuildPtrToInt(b, a, lps[p], "");
        } else if (sk == LLVMIntegerTypeKind && lk == LLVMIntegerTypeKind) {
            args[p] = LLVMBuildIntCast2(b, a, lps[p], 1, "");
        } else if (sk == LLVMPointerTypeKind && lk == LLVMPointerTypeKind) {
            args[p] = LLVMBuildPointerCast(b, a, lps[p], "");
        } else {
            args[p] = a;
        }
    }
    LLVMValueRef rv = zan_call2(b, lft, real, args, nparams, "");
    LLVMTypeRef srt = LLVMGetReturnType(src_ft);
    LLVMTypeRef lrt = LLVMGetReturnType(lft);
    if (LLVMGetTypeKind(srt) == LLVMVoidTypeKind) {
        LLVMBuildRetVoid(b);
    } else if (srt == lrt) {
        LLVMBuildRet(b, rv);
    } else if (LLVMGetTypeKind(lrt) == LLVMVoidTypeKind) {
        LLVMBuildRet(b, LLVMConstNull(srt));
    } else if (LLVMGetTypeKind(lrt) == LLVMPointerTypeKind &&
               LLVMGetTypeKind(srt) == LLVMIntegerTypeKind) {
        LLVMBuildRet(b, LLVMBuildPtrToInt(b, rv, srt, ""));
    } else if (LLVMGetTypeKind(lrt) == LLVMIntegerTypeKind &&
               LLVMGetTypeKind(srt) == LLVMIntegerTypeKind) {
        LLVMBuildRet(b, LLVMBuildIntCast2(b, rv, srt, 1, ""));
    } else if (LLVMGetTypeKind(lrt) == LLVMIntegerTypeKind &&
               LLVMGetTypeKind(srt) == LLVMPointerTypeKind) {
        LLVMBuildRet(b, LLVMBuildIntToPtr(b, rv, srt, ""));
    } else {
        LLVMBuildRet(b, rv);
    }
    LLVMDisposeBuilder(b);
}

static LLVMValueRef w32_build_adapter(zan_irgen_t *g, const char *name,
                                      LLVMTypeRef src_ft, LLVMValueRef real,
                                      LLVMTypeRef lft) {
    if (LLVMCountParamTypes(src_ft) != LLVMCountParamTypes(lft) ||
        LLVMCountParamTypes(src_ft) > 8)
        return NULL;
    LLVMValueRef fn = LLVMAddFunction(g->mod, name, src_ft);
    LLVMSetLinkage(fn, LLVMInternalLinkage);
    w32_build_adapter_into(g, fn, real, lft);
    return fn;
}

zan_status_t zan_irgen_write_obj(zan_irgen_t *g, const char *path) {
    /* wasm32: libc size_t/long are 32-bit but the IR declares these libc
     * functions with i64 sizes (Zan int). Redirect the declarations to the
     * zan_w32_* wrappers (src/runtime/rt_wasm.c, shipped pre-compiled in the
     * wasm32 sysroot) whose signatures take i64 and forward to the real
     * 32-bit libc, so wasm's strict signature checking is satisfied. */
    if (strncmp(g->target_triple, "wasm32", 6) == 0) {
        /* wasi-libc's _start calls __main_argc_argv (the name clang gives a
         * two-argument main on wasm), not "main". */
        LLVMValueRef mainf = LLVMGetNamedFunction(g->mod, "main");
        if (mainf && LLVMCountBasicBlocks(mainf) > 0)
            LLVMSetValueName2(mainf, "__main_argc_argv",
                              strlen("__main_argc_argv"));
        /* Variadic snprintf cannot be adapted in IR (varargs cannot be
         * forwarded); route it to the C wrapper in rt_wasm.c instead. */
        {
            LLVMValueRef f = LLVMGetNamedFunction(g->mod, "snprintf");
            if (f && LLVMCountBasicBlocks(f) == 0)
                LLVMSetValueName2(f, "zan_w32_snprintf",
                                  strlen("zan_w32_snprintf"));
        }
        /* Zan IR declares libc functions with 64-bit ints (Zan int is i64,
         * and pointers passed through Zan `int` handles are i64 too), but
         * wasm32's size_t/long/pointers are 32-bit and wasm enforces exact
         * call signatures. For each such declaration, turn it into a thin
         * adapter that truncates/extends the values and calls the real
         * 32-bit libc function.
         * Signature codes: p=pointer, i=i32, j=i64, s=size_t(i32),
         * v=void; first char is the return, the rest are the params. */
        static const struct { const char *name; const char *sig; } w32adapt[] = {
            { "malloc", "ps" },      { "calloc", "pss" },
            { "realloc", "pps" },    { "free", "vp" },
            { "strlen", "sp" },      { "memcpy", "ppps" },
            { "memset", "ppis" },    { "memcmp", "ipps" },
            { "strcat", "ppp" },     { "strcpy", "ppp" },
            { "strncpy", "ppps" },   { "strcmp", "ipp" },
            { "strncmp", "ipps" },   { "strrchr", "ppi" },
            { "strstr", "ppp" },     { "atoi", "ip" },
            { "fopen", "ppp" },      { "fclose", "ip" },
            { "fgetc", "ip" },       { "fputc", "iip" },
            { "fputs", "ipp" },      { "fgets", "ppip" },
            { "fread", "spssp" },    { "fwrite", "spssp" },
            { "fseek", "ipii" },     { "ftell", "ip" },
            { "fflush", "ip" },      { "remove", "ip" },
            { "rename", "ipp" },     { "chdir", "ip" },
            { "getcwd", "pps" },     { "mkdir", "ipi" },
            { "rmdir", "ip" },       { "opendir", "pp" },
            { "readdir", "pp" },     { "closedir", "ip" },
            { "time", "jp" },        { "poll", "ipii" },
            /* the startup stdout line-buffering call (irgen_emit.c) declares
             * size as i64 (Zan int); wasm's setvbuf takes a 32-bit size_t. */
            { "setvbuf", "ipipi" },
            /* wasi-libc ships dlfcn stubs that return 0 ("no dynamic
             * linking"): the IR declares these with 64-bit nint, so adapt
             * them too, keeping the graceful-failure path the stdlib's
             * Interop.Entry relies on instead of a wasm-ld trap stub. */
            { "dlopen", "pip" },     { "dlsym", "ppp" },
            { "dlclose", "ip" },
            { NULL, NULL }
        };
        LLVMTypeRef w_i32 = LLVMInt32TypeInContext(g->ctx);
        LLVMTypeRef w_i64 = LLVMInt64TypeInContext(g->ctx);
        LLVMTypeRef w_ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
        for (int i = 0; w32adapt[i].name; i++) {
            LLVMValueRef decl = LLVMGetNamedFunction(g->mod, w32adapt[i].name);
            if (!decl || LLVMCountBasicBlocks(decl) > 0) continue;
            LLVMTypeRef dft = LLVMGlobalGetValueType(decl);
            if (LLVMIsFunctionVarArg(dft)) continue;
            const char *sig = w32adapt[i].sig;
            int nparams = (int)strlen(sig) - 1;
            if (nparams > 8 || (int)LLVMCountParamTypes(dft) != nparams)
                continue;
            /* the real libc function's type */
            LLVMTypeRef lps[8];
            for (int p = 0; p < nparams; p++) {
                char c = sig[p + 1];
                lps[p] = (c == 'p') ? w_ptr : (c == 'j') ? w_i64 : w_i32;
            }
            char rc = sig[0];
            LLVMTypeRef lrt = (rc == 'v') ? LLVMVoidTypeInContext(g->ctx)
                              : (rc == 'p') ? w_ptr
                              : (rc == 'j') ? w_i64 : w_i32;
            LLVMTypeRef lft = LLVMFunctionType(lrt, lps, (unsigned)nparams, 0);
            /* rename the declaration; re-add the real libc function */
            char an[80];
            snprintf(an, sizeof(an), "__zan_w32ir_%s", w32adapt[i].name);
            LLVMSetValueName2(decl, an, strlen(an));
            LLVMValueRef real = LLVMAddFunction(g->mod, w32adapt[i].name, lft);
            /* Different call sites may use different signatures for the same
             * function (Zan reuses an existing declaration whatever its
             * type), so rewrite every direct call: give each distinct
             * call-site type its own adapter that converts the values and
             * calls the real 32-bit libc function. */
            LLVMTypeRef cts[8];
            LLVMValueRef cad[8];
            int ncts = 0;
            LLVMUseRef use = LLVMGetFirstUse(decl);
            while (use) {
                LLVMUseRef next = LLVMGetNextUse(use);
                LLVMValueRef user = LLVMGetUser(use);
                if (LLVMIsACallInst(user) &&
                    LLVMGetCalledValue(user) == decl) {
                    LLVMTypeRef cft = LLVMGetCalledFunctionType(user);
                    if (cft == lft) {
                        /* already the 32-bit ABI: call libc directly */
                        LLVMSetOperand(user,
                                       LLVMGetNumOperands(user) - 1, real);
                    } else if (!LLVMIsFunctionVarArg(cft) &&
                               (int)LLVMCountParamTypes(cft) == nparams) {
                        LLVMValueRef ad = NULL;
                        for (int k = 0; k < ncts; k++) {
                            if (cts[k] == cft) { ad = cad[k]; break; }
                        }
                        if (!ad) {
                            char nm[96];
                            snprintf(nm, sizeof(nm), "%s.v%d", an, ncts);
                            ad = w32_build_adapter(g, nm, cft, real, lft);
                            if (ad && ncts < 8) {
                                cts[ncts] = cft;
                                cad[ncts] = ad;
                                ncts++;
                            }
                        }
                        if (ad)
                            LLVMSetOperand(user,
                                           LLVMGetNumOperands(user) - 1, ad);
                    }
                }
                use = next;
            }
            /* Any remaining (indirect) uses go through the renamed
             * declaration itself: define it as an adapter too. */
            if (LLVMGetFirstUse(decl) && dft != lft) {
                w32_build_adapter_into(g, decl, real, lft);
                LLVMSetLinkage(decl, LLVMInternalLinkage);
            }
        }
    }
    /* Initialize every target family the build links (see CMakeLists.txt), so
     * zanc can emit code for the host regardless of architecture (e.g. arm64
     * macOS) as well as cross-compile to the advertised targets. */
    LLVMInitializeX86TargetInfo();
    LLVMInitializeX86Target();
    LLVMInitializeX86TargetMC();
    LLVMInitializeX86AsmParser();
    LLVMInitializeX86AsmPrinter();

    LLVMInitializeAArch64TargetInfo();
    LLVMInitializeAArch64Target();
    LLVMInitializeAArch64TargetMC();
    LLVMInitializeAArch64AsmParser();
    LLVMInitializeAArch64AsmPrinter();

#ifdef ZAN_HAVE_LLVM_ARM
    LLVMInitializeARMTargetInfo();
    LLVMInitializeARMTarget();
    LLVMInitializeARMTargetMC();
    LLVMInitializeARMAsmParser();
    LLVMInitializeARMAsmPrinter();
#endif

#ifdef ZAN_HAVE_LLVM_WEBASSEMBLY
    LLVMInitializeWebAssemblyTargetInfo();
    LLVMInitializeWebAssemblyTarget();
    LLVMInitializeWebAssemblyTargetMC();
    LLVMInitializeWebAssemblyAsmParser();
    LLVMInitializeWebAssemblyAsmPrinter();
#endif

#ifdef ZAN_HAVE_LLVM_RISCV
    LLVMInitializeRISCVTargetInfo();
    LLVMInitializeRISCVTarget();
    LLVMInitializeRISCVTargetMC();
    LLVMInitializeRISCVAsmParser();
    LLVMInitializeRISCVAsmPrinter();
#endif

    char *triple;
    if (g->target_triple[0]) {
        /* Cross-compilation: emit for the requested target triple verbatim
         * (e.g. x86_64-unknown-linux-musl). The X86/AArch64 backends produce
         * the right object format (ELF/Mach-O/COFF) from the triple's OS. */
        triple = LLVMCreateMessage(g->target_triple);
    } else {
        triple = LLVMGetDefaultTargetTriple();
#ifdef _WIN32
        /* Emit GNU-ABI (MinGW) objects so the produced code links against the
         * bundled ld.lld + mingw-w64 runtime, keeping zanc self-contained:
         * building an .exe needs only zan, no external clang / MSVC / Windows
         * SDK. Preserve the host architecture prefix and swap the vendor/abi. */
        {
            const char *dash = strchr(triple, '-');
            size_t archlen = dash ? (size_t)(dash - triple) : strlen(triple);
            char gnu[128];
            if (archlen > sizeof(gnu) - 20) archlen = sizeof(gnu) - 20;
            memcpy(gnu, triple, archlen);
            snprintf(gnu + archlen, sizeof(gnu) - archlen, "-w64-windows-gnu");
            LLVMDisposeMessage(triple);
            triple = LLVMCreateMessage(gnu);
        }
#endif
    }
    LLVMTargetRef target;
    char *error = NULL;

    if (LLVMGetTargetFromTriple(triple, &target, &error)) {
        zan_diag_emit(g->diag, DIAG_ERROR, zan_loc(0, 0, 0, 0),
                      "failed to get target: %s", error);
        LLVMDisposeMessage(error);
        LLVMDisposeMessage(triple);
        return ZAN_ERROR;
    }

    /* RISC-V: match the RV64GC / lp64d ABI the bundled musl sysroot uses
     * (the ABI must be recorded as a module flag for the backend to lower
     * doubles into FP registers). */
    const char *tm_cpu = "generic";
    const char *tm_features = "";
    if (strncmp(triple, "riscv64", 7) == 0) {
        tm_cpu = "generic-rv64";
        tm_features = "+m,+a,+f,+d,+c";
        LLVMAddModuleFlag(g->mod, LLVMModuleFlagBehaviorError,
                          "target-abi", strlen("target-abi"),
                          LLVMValueAsMetadata(LLVMMDStringInContext(
                              g->ctx, "lp64d", 5)));
    }
    /* Machine codegen dominates compile time. Development builds (no
     * --publish / -O) use the fast path (FastISel, no machine-level
     * optimization); release builds keep the optimizing selector. */
    LLVMCodeGenOptLevel cg = g->fast_codegen ? LLVMCodeGenLevelNone
                                             : LLVMCodeGenLevelDefault;
    LLVMTargetMachineRef tm = LLVMCreateTargetMachine(
        target, triple, tm_cpu, tm_features,
        cg, LLVMRelocPIC, LLVMCodeModelDefault);

    LLVMSetTarget(g->mod, triple);
    LLVMTargetDataRef dl = LLVMCreateTargetDataLayout(tm);
    char *dl_str = LLVMCopyStringRepOfTargetData(dl);
    LLVMSetDataLayout(g->mod, dl_str);
    LLVMDisposeMessage(dl_str);
    LLVMDisposeTargetData(dl);

    if (LLVMTargetMachineEmitToFile(tm, g->mod, (char *)path,
                                     LLVMObjectFile, &error)) {
        zan_diag_emit(g->diag, DIAG_ERROR, zan_loc(0, 0, 0, 0),
                      "failed to emit object file: %s", error);
        LLVMDisposeMessage(error);
        LLVMDisposeTargetMachine(tm);
        LLVMDisposeMessage(triple);
        return ZAN_ERROR;
    }

    LLVMDisposeTargetMachine(tm);
    LLVMDisposeMessage(triple);
    return ZAN_OK;
}
