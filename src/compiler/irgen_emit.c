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
                if (m->kind != AST_FIELD_DECL) continue;
                if (!(m->field_decl.modifiers & MOD_STATIC)) continue;
                if (!m->field_decl.initializer) continue;
                zan_symbol_t *fs = get_field_sym(csym, m->field_decl.name);
                LLVMValueRef gv = get_static_field_global(g, csym, fs);
                if (!gv) continue;
                LLVMValueRef v = emit_expr(g, m->field_decl.initializer, sf_locals);
                if (fs->type && is_rc_managed_type(fs->type)) {
                    emit_rc_store_field(g, fs->type, gv, v, m->field_decl.initializer, sf_locals,
                                        (fs->modifiers & MOD_WEAK) ? 1 : 0);
                } else {
                    LLVMTypeRef ft = fs->type ? map_type(g, fs->type)
                                              : LLVMInt64TypeInContext(g->ctx);
                    LLVMBuildStore(g->builder, coerce_int_to(g, v, ft), gv);
                }
            }
        }
        g->current_type_sym = saved_type;
        g->current_this = saved_this;
    }

    local_scope_t *locals = local_scope_new(g->arena);

    if (method->method_decl.body) {
        emit_stmt(g, method->method_decl.body, locals);
    }

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
    zan_type_t     *cur_inst;       /* instantiation being specialized, or NULL */
} method_body_work_t;

/* Count how many discovered instantiations exist for a given generic type. */
static int generic_variant_count(zan_irgen_t *g, zan_symbol_t *type_sym) {
    int n = 0;
    for (int i = 0; i < g->generic_inst_count; i++)
        if (g->generic_insts[i].type_sym == type_sym) n++;
    return n;
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
            /* extern/DllImport methods have no generic body: only the erased
             * variant declares them. */
            if (cur_variant && member->kind == AST_METHOD_DECL &&
                member->method_decl.extern_lib.str && !member->method_decl.body)
                continue;

            /* [DllImport] extern methods: generate extern declaration, skip body */
            if (member->kind == AST_METHOD_DECL && member->method_decl.extern_lib.str &&
                !member->method_decl.body) {
                /* build extern function declaration */
                int pc = member->method_decl.params.count;
                LLVMTypeRef *pt = (LLVMTypeRef *)calloc((size_t)(pc > 0 ? pc : 1), sizeof(LLVMTypeRef));
                for (int k = 0; k < pc; k++) {
                    zan_ast_node_t *param = member->method_decl.params.items[k];
                    zan_type_t *ptype = zan_binder_resolve_type(g->binder, param->param.type);
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
                if (strncmp(ext_name, "zan_atomic_int_", 15) == 0 ||
                    strncmp(ext_name, "zan_shared_table_", 17) == 0 ||
                    strncmp(ext_name, "zan_thread_", 11) == 0 ||
                    strncmp(ext_name, "zan_dispatch_", 13) == 0) {
                    g->uses_sync_runtime = true;
                }
                if (strncmp(ext_name, "zan_io_socket_", 14) == 0) {
                    g->uses_socket_async = true;
                }
                if (strncmp(ext_name, "zan_gate_", 9) == 0) {
                    g->uses_socket_async = true;
                }
                /* Reuse existing declaration if the symbol already exists in the module
                 * (e.g. built-in malloc/free/strlen, or duplicate DllImport across files). */
                LLVMValueRef efn = LLVMGetNamedFunction(g->mod, ext_name);
                if (!efn) {
                    efn = LLVMAddFunction(g->mod, ext_name, ft);
                }
                /* register as a static method so it can be called */
                zan_symbol_t *method_sym = method_sym_for_decl(type_sym, member);
                if (method_sym) {
                    irgen_register_function(g, method_sym, efn, ft);
                }
                /* store lib name for linker */
                if (g->extern_lib_count < 64) {
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
                if (g->extern_fn_count < (int)(sizeof(g->extern_fns) / sizeof(g->extern_fns[0]))) {
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

            /* skip static Main — handled separately */
            bool is_static = !is_ctor && (member->method_decl.modifiers & MOD_STATIC) != 0;
            if (is_static && member->method_decl.name.len == 4 &&
                memcmp(member->method_decl.name.str, "Main", 4) == 0) continue;

            /* build function name: TypeName_MethodName or TypeName_ctor,
             * plus a per-instantiation suffix (e.g. HashSet_Add$string) for a
             * specialized variant so it does not collide with the erased one. */
            char fn_name[512];
            if (is_ctor) {
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

            for (int k = 0; k < param_count; k++) {
                zan_ast_node_t *param = member->method_decl.params.items[k];
                zan_type_t *pt = zan_binder_resolve_type(g->binder, param->param.type);
                param_types[k + param_offset] = param->param.by_ref
                    ? LLVMPointerType(map_type(g, pt), 0)
                    : map_type(g, pt);
            }

            zan_type_t *ret_type = is_ctor ? g->binder->type_void
                : (member->method_decl.return_type
                    ? zan_binder_resolve_type(g->binder, member->method_decl.return_type)
                    : g->binder->type_void);
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

            if (is_async) {
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
                async_scan_t scan = { g, 0, NULL, 0, 0 };
                async_scan_stmt(&scan, member->method_decl.body);
                a_await_count = scan.await_count;
                a_locals = scan.locals;
                a_local_count = scan.local_count;

                /* frame = fixed 5-field header + params + frame locals +
                 * one i8* sub-task handle per await point. */
                int locals_base = ASYNC_FRAME_FIRST_PARAM + total_params;
                a_sub_base = locals_base + a_local_count;
                int nfields = a_sub_base + a_await_count;
                LLVMTypeRef *fields = (LLVMTypeRef *)calloc((size_t)nfields, sizeof(LLVMTypeRef));
                fields[ASYNC_FRAME_STATE] = i32;
                fields[ASYNC_FRAME_DONE] = i32;
                fields[ASYNC_FRAME_AWAITER] = i8ptr;
                fields[ASYNC_FRAME_AWAITER_STEP] = g->co_step_ptr;
                fields[ASYNC_FRAME_RESULT] = i64;
                for (int k = 0; k < total_params; k++) {
                    fields[ASYNC_FRAME_FIRST_PARAM + k] = param_types[k];
                }
                for (int k = 0; k < a_local_count; k++) {
                    a_locals[k].frame_index = locals_base + k;
                    fields[locals_base + k] = a_locals[k].llvm;
                }
                for (int k = 0; k < a_await_count; k++) {
                    fields[a_sub_base + k] = i8ptr;
                }
                char frame_name[560];
                snprintf(frame_name, sizeof(frame_name), "%s$frame", fn_name);
                frame_type = LLVMStructCreateNamed(g->ctx, frame_name);
                LLVMStructSetBody(frame_type, fields, (unsigned)nfields, 0);
                free(fields);

                /* ramp keeps the external param list but returns the task
                 * handle (i8*); the body runs later in resume(frame). */
                fn_type = LLVMFunctionType(i8ptr, param_types, (unsigned)total_params, 0);
                fn = LLVMAddFunction(g->mod, fn_name, fn_type);

                char resume_name[560];
                snprintf(resume_name, sizeof(resume_name), "%s$resume", fn_name);
                resume_fn = LLVMAddFunction(g->mod, resume_name, g->co_step_type);
            } else {
                fn_type = LLVMFunctionType(llvm_ret, param_types, (unsigned)total_params, 0);
                fn = LLVMAddFunction(g->mod, fn_name, fn_type);
            }

            /* register in function/ctor table. For async methods the ramp is
             * the callable symbol (a call site receives the task handle). The
             * erased variant registers in the ordinary tables; a specialized
             * variant registers in the generic tables keyed by (sym, args) so a
             * concrete call site can route to it. */
            if (is_ctor) {
                if (cur_variant) {
                    add_generic_ctor(g, type_sym, cur_variant->type_args,
                                     cur_variant->type_arg_count, param_count,
                                     fn, fn_type);
                } else if (g->ctor_count < 256) {
                    g->ctors[g->ctor_count].type_sym = type_sym;
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
            LLVMValueRef ramp_fn = fn;
            LLVMValueRef resume_fn = work[w].resume_fn;
            LLVMTypeRef frame_type = work[w].frame_type;
            LLVMTypeRef frame_ptr_ty = LLVMPointerType(frame_type, 0);
            LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
            LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            int total_params = param_count + param_offset;

            /* ---- ramp ---- */
            LLVMBasicBlockRef ramp_entry = LLVMAppendBasicBlockInContext(g->ctx, ramp_fn, "entry");
            LLVMPositionBuilderAtEnd(g->builder, ramp_entry);
            di_clear(g);
            LLVMTypeRef malloc_ty = LLVMGlobalGetValueType(g->fn_malloc);
            LLVMValueRef fsize = LLVMSizeOf(frame_type);
            LLVMValueRef raw = zan_call2(g->builder, malloc_ty, g->fn_malloc, &fsize, 1, "frame.raw");
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
                 * arc_owned marking on the resume-body param locals). `this`
                 * (k < param_offset) stays a borrow, as in synchronous methods. */
                if (k >= param_offset) {
                    zan_ast_node_t *pn = member->method_decl.params.items[k - param_offset];
                    if (!pn->param.by_ref) {
                        zan_type_t *pt = zan_binder_resolve_type(g->binder, pn->param.type);
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
            di_clear(g);
            LLVMValueRef fparam = LLVMGetParam(resume_fn, 0);
            LLVMValueRef sframe = LLVMBuildBitCast(g->builder, fparam, frame_ptr_ty, "frame");

            local_scope_t *locals = local_scope_new(g->arena);

            /* Frame-resident slots: `this` (instance methods), params, and named
             * scalar locals. Their storage is stack allocas created here in the
             * entry block (so they dominate every state block); values are saved
             * to / reloaded from the heap frame around each suspension. */
            int alocal_count = work[w].alocal_count;
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
                zan_type_t *pt = zan_binder_resolve_type(g->binder, param->param.type);
                local_add(locals, param->param.name, pa, pt);
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
                LLVMValueRef la = LLVMBuildAlloca(g->builder, work[w].alocals[k].llvm, "fl");
                local_add(locals, work[w].alocals[k].name, la, work[w].alocals[k].ztype);
                /* A frame-resident rc local is owned for the whole coroutine
                 * (it is never dropped at inner block scope like a synchronous
                 * local). Mark it owned and null-init its slot *here*, at
                 * registration, rather than when its var-decl is emitted: a
                 * `return` lexically preceding the declaration (e.g. an early
                 * exit at the top of a loop whose body declares the local after
                 * an await) must still release the value a prior iteration
                 * stored, or it leaks. The null-init makes the release a no-op
                 * on paths where the local was never assigned. */
                if (is_rc_managed_type(work[w].alocals[k].ztype) &&
                    LLVMGetTypeKind(work[w].alocals[k].llvm) == LLVMPointerTypeKind) {
                    LLVMBuildStore(g->builder,
                        LLVMConstNull(work[w].alocals[k].llvm), la);
                    locals->vars[locals->count - 1].arc_owned = 1;
                }
                slots[si].slot_alloca = la;
                slots[si].llvm = work[w].alocals[k].llvm;
                slots[si].frame_index = work[w].alocals[k].frame_index;
                si++;
            }

            LLVMValueRef state = LLVMBuildLoad2(g->builder, i32,
                LLVMBuildStructGEP2(g->builder, frame_type, sframe, ASYNC_FRAME_STATE, "st.ptr"),
                "state");
            LLVMBasicBlockRef body_bb = LLVMAppendBasicBlockInContext(g->ctx, resume_fn, "co.start");
            /* switch(state): case 0 (start) -> body; each await point appends a
             * resume-k case (see the AST_AWAIT_EXPR lowering). */
            LLVMValueRef sw = LLVMBuildSwitch(g->builder, state, body_bb, (unsigned)(work[w].await_count + 1));
            LLVMAddCase(sw, LLVMConstInt(i32, 0, 0), body_bb);

            LLVMValueRef saved_fn = g->current_fn;
            LLVMTypeRef saved_fn_ret = g->current_fn_ret_type;
            LLVMValueRef saved_this = g->current_this;
            zan_symbol_t *saved_type_sym = g->current_type_sym;
            LLVMValueRef saved_async_frame = g->current_async_frame;
            LLVMTypeRef saved_async_frame_type = g->current_async_frame_type;
            LLVMValueRef saved_async_resume_fn = g->current_async_resume_fn;
            LLVMValueRef saved_async_switch = g->current_async_switch;
            int saved_next_state = g->current_async_next_state;
            int saved_sub_base = g->current_async_sub_base;
            int saved_sub_next = g->current_async_sub_next;
            void *saved_slots = (void *)g->current_async_slots;
            int saved_slot_count = g->current_async_slot_count;

            g->current_fn = resume_fn;
            g->current_fn_ret_type = LLVMVoidTypeInContext(g->ctx);
            g->current_this = is_static ? NULL : res_this;
            g->current_type_sym = type_sym;
            g->current_async_frame = sframe;
            g->current_async_frame_type = frame_type;
            g->current_async_resume_fn = resume_fn;
            g->current_async_switch = sw;
            g->current_async_next_state = 1;
            g->current_async_sub_base = work[w].sub_base;
            g->current_async_sub_next = 0;
            g->current_async_slots = slots;
            g->current_async_slot_count = slot_total;

            LLVMPositionBuilderAtEnd(g->builder, body_bb);
            emit_async_reload_slots(g); /* load `this`/params from the frame */

            if (member->method_decl.body->kind == AST_BLOCK) {
                for (int k = 0; k < member->method_decl.body->block.stmts.count; k++) {
                    emit_stmt(g, member->method_decl.body->block.stmts.items[k], locals);
                }
            } else {
                /* expression body (=> expr): treat as `return expr`. */
                LLVMValueRef val = emit_expr(g, member->method_decl.body, locals);
                emit_async_complete(g, locals, coerce_to_i64(g, val));
            }

            /* fall off the end: implicit completion (void / default result). */
            if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder))) {
                emit_async_complete(g, locals, NULL);
            }

            g->current_fn = saved_fn;
            g->current_fn_ret_type = saved_fn_ret;
            g->current_this = saved_this;
            g->current_type_sym = saved_type_sym;
            g->current_async_frame = saved_async_frame;
            g->current_async_frame_type = saved_async_frame_type;
            g->current_async_resume_fn = saved_async_resume_fn;
            g->current_async_switch = saved_async_switch;
            g->current_async_next_state = saved_next_state;
            g->current_async_sub_base = saved_sub_base;
            g->current_async_sub_next = saved_sub_next;
            g->current_async_slots = saved_slots;
            g->current_async_slot_count = saved_slot_count;
            free(param_types);
            continue;
        }

        LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
        LLVMPositionBuilderAtEnd(g->builder, entry);
        di_clear(g);

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
            if (param->param.by_ref) {
                /* `ref`/`out`: the incoming pointer IS the storage slot, so
                 * reads/writes go straight through to the caller's variable. */
                local_add(locals, param->param.name,
                          LLVMGetParam(fn, (unsigned)(k + param_offset)), pt);
                continue;
            }
            LLVMValueRef param_alloca = LLVMBuildAlloca(g->builder, param_types[k + param_offset], "p");
            LLVMBuildStore(g->builder, LLVMGetParam(fn, (unsigned)(k + param_offset)), param_alloca);
            local_add(locals, param->param.name, param_alloca, pt);
        }

        LLVMValueRef saved_fn = g->current_fn;
        LLVMTypeRef saved_fn_ret = g->current_fn_ret_type;
        LLVMValueRef saved_this = g->current_this;
        zan_symbol_t *saved_type_sym = g->current_type_sym;
        zan_ast_node_t *saved_fn_body = g->current_fn_body;
        g->current_fn = fn;
        g->current_fn_ret_type = llvm_ret;
        g->current_this = is_static ? NULL : this_alloca;
        g->current_type_sym = type_sym;
        g->current_fn_body = member->method_decl.body;

        /* base construction: a derived constructor first chains to its base
         * class's constructor so inherited fields are initialised (base
         * fields are laid out as a prefix of the derived struct, so `this`
         * upcasts by a plain bitcast). An explicit `: base(args)` initializer
         * selects the base overload by argument count; otherwise the
         * parameterless base constructor is chained implicitly. */
        if (member->kind == AST_CONSTRUCTOR_DECL && type_sym->type && type_sym->type->base_type &&
            type_sym->type->base_type->sym) {
            zan_symbol_t *base_sym = type_sym->type->base_type->sym;
            int want_args = member->method_decl.has_base_init
                ? member->method_decl.base_args.count : 0;
            for (int ci = 0; ci < g->ctor_count; ci++) {
                if (g->ctors[ci].type_sym == base_sym && g->ctors[ci].param_count == want_args) {
                    LLVMValueRef thisv = LLVMBuildLoad2(g->builder, param_types[0], this_alloca, "this.base");
                    LLVMTypeRef bst = get_struct_llvm_type(g, base_sym);
                    if (bst) thisv = LLVMBuildBitCast(g->builder, thisv, LLVMPointerType(bst, 0), "base.this");
                    if (want_args == 0) {
                        zan_call2(g->builder, g->ctors[ci].fn_type, g->ctors[ci].fn, &thisv, 1, "");
                    } else {
                        LLVMValueRef *cargs = (LLVMValueRef *)malloc(
                            sizeof(LLVMValueRef) * (size_t)(want_args + 1));
                        cargs[0] = thisv;
                        for (int ai = 0; ai < want_args; ai++) {
                            cargs[ai + 1] = emit_expr(g,
                                member->method_decl.base_args.items[ai], locals);
                        }
                        zan_call2(g->builder, g->ctors[ci].fn_type, g->ctors[ci].fn,
                                  cargs, (unsigned)(want_args + 1), "");
                        free(cargs);
                    }
                    break;
                }
            }
        }

        /* emit method body */
        if (member->method_decl.body->kind == AST_BLOCK) {
            for (int k = 0; k < member->method_decl.body->block.stmts.count; k++) {
                emit_stmt(g, member->method_decl.body->block.stmts.items[k], locals);
            }
        } else {
            /* expression body (=> expr) */
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
        g->current_this = saved_this;
        g->current_type_sym = saved_type_sym;
        g->current_fn_body = saved_fn_body;
        free(param_types);
    }

    g->cur_inst = NULL;
    free(work);
    emit_pending_method_specs(g);
}

/* ---- method-level monomorphization (bodies for zan_method_spec) ----
 *
 * A generic method (one declaring its own <T,...>) is normally emitted once
 * with type parameters erased to machine words, which loses type-specific
 * semantics: `<`/`==` on a string key compare pointers, double keys compare
 * raw bits, and a replaced ARC value is never released. When a call site can
 * bind every type parameter to a concrete type AND some binding's semantics
 * differ from the erased representation, a specialized copy with a concrete
 * signature is declared here and its body emitted from the pending queue. */

/* Erased codegen is already correct for int-like bindings; only strings,
 * floating types and composite/reference types need a specialized copy. */
static bool mspec_bind_worthwhile(zan_type_t **bind, int bindc) {
    for (int i = 0; i < bindc; i++) {
        if (!bind[i]) return false;
        switch (bind[i]->kind) {
        case TYPE_STRING: case TYPE_FLOAT: case TYPE_DOUBLE:
        case TYPE_CLASS: case TYPE_STRUCT: case TYPE_INTERFACE:
        case TYPE_ARRAY:
            return true;
        default:
            break;
        }
    }
    return false;
}

static int get_or_create_method_spec(zan_irgen_t *g, zan_symbol_t *msym,
                                     zan_type_t **bind, int bindc) {
    if (!msym || !msym->decl || msym->decl->kind != AST_METHOD_DECL) return -1;
    zan_ast_node_t *member = msym->decl;
    if (!member->method_decl.body) return -1;
    /* async bodies lower through the CPS frame machinery; keep them erased */
    if (member->method_decl.modifiers & MOD_ASYNC) return -1;
    if (!(member->method_decl.modifiers & MOD_STATIC)) return -1;
    zan_ast_list_t *tps = &member->method_decl.type_params;
    if (tps->count != bindc || bindc <= 0 || bindc > 8) return -1;
    for (int i = 0; i < bindc; i++)
        if (!bind[i] || !type_is_concrete(bind[i])) return -1;
    if (!mspec_bind_worthwhile(bind, bindc)) return -1;
    zan_symbol_t *type_sym = msym->parent;
    if (!type_sym ||
        (type_sym->kind != SYM_CLASS && type_sym->kind != SYM_STRUCT))
        return -1;

    for (int i = 0; i < g->method_spec_count; i++)
        if (g->method_specs[i].msym == msym &&
            type_arglists_equal(g->method_specs[i].bind,
                                g->method_specs[i].bindc, bind, bindc))
            return i;

    /* mangled name: Type_Method$$tok1$tok2, uniquified across overloads */
    char fn_name[512];
    {
        size_t off = (size_t)snprintf(fn_name, sizeof(fn_name), "%.*s_%.*s$",
            (int)type_sym->name.len, type_sym->name.str,
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
    LLVMTypeRef *param_types = (LLVMTypeRef *)calloc(
        (size_t)(param_count > 0 ? param_count : 1), sizeof(LLVMTypeRef));
    for (int k = 0; k < param_count; k++) {
        zan_ast_node_t *param = member->method_decl.params.items[k];
        zan_type_t *pt = subst_method_tp(g,
            zan_binder_resolve_type(g->binder, param->param.type), tps, bind);
        param_types[k] = param->param.by_ref
            ? LLVMPointerType(map_type(g, pt), 0)
            : map_type(g, pt);
    }
    zan_type_t *ret_type = member->method_decl.return_type
        ? subst_method_tp(g, zan_binder_resolve_type(g->binder,
              member->method_decl.return_type), tps, bind)
        : g->binder->type_void;
    LLVMTypeRef fn_type = LLVMFunctionType(map_type(g, ret_type), param_types,
                                           (unsigned)param_count, 0);
    LLVMValueRef fn = LLVMAddFunction(g->mod, fn_name, fn_type);
    free(param_types);

    if (g->method_spec_count >= g->method_spec_cap) {
        int ncap = g->method_spec_cap ? g->method_spec_cap * 2 : 32;
        g->method_specs = realloc(g->method_specs,
                                  (size_t)ncap * sizeof(*g->method_specs));
        g->method_spec_cap = ncap;
    }
    zan_type_t **bcopy = (zan_type_t **)zan_arena_alloc(g->arena,
        sizeof(zan_type_t *) * (size_t)bindc);
    for (int i = 0; i < bindc; i++) bcopy[i] = bind[i];
    int idx = g->method_spec_count++;
    g->method_specs[idx].msym = msym;
    g->method_specs[idx].type_sym = type_sym;
    g->method_specs[idx].member = member;
    g->method_specs[idx].bind = bcopy;
    g->method_specs[idx].bindc = bindc;
    g->method_specs[idx].fn = fn;
    g->method_specs[idx].fn_type = fn_type;
    return idx;
}

/* Emit the body of one specialization: the static, non-async subset of
 * emit_user_methods Pass B, with the type-parameter bindings active so every
 * type ref resolved from this body comes out concrete (resolve_type_ctx). */
static void emit_method_spec_body(zan_irgen_t *g, int idx) {
    struct zan_method_spec sp = g->method_specs[idx];
    zan_ast_node_t *member = sp.member;
    zan_ast_list_t *tps = &member->method_decl.type_params;

    zan_ast_list_t *saved_mtps = g->cur_mtps;
    zan_type_t **saved_mbind = g->cur_mbind;
    zan_type_t *saved_inst = g->cur_inst;
    g->cur_mtps = tps;
    g->cur_mbind = sp.bind;
    g->cur_inst = NULL;

    LLVMBasicBlockRef saved_bb = LLVMGetInsertBlock(g->builder);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, sp.fn, "entry");
    LLVMPositionBuilderAtEnd(g->builder, entry);
    di_clear(g);

    local_scope_t *locals = local_scope_new(g->arena);
    int param_count = member->method_decl.params.count;
    unsigned npt = LLVMCountParamTypes(sp.fn_type);
    LLVMTypeRef *param_types = (LLVMTypeRef *)calloc(
        (size_t)(npt > 0 ? npt : 1), sizeof(LLVMTypeRef));
    LLVMGetParamTypes(sp.fn_type, param_types);
    for (int k = 0; k < param_count; k++) {
        zan_ast_node_t *param = member->method_decl.params.items[k];
        zan_type_t *pt = resolve_type_ctx(g, param->param.type);
        if (param->param.by_ref) {
            local_add(locals, param->param.name,
                      LLVMGetParam(sp.fn, (unsigned)k), pt);
            continue;
        }
        LLVMValueRef param_alloca = LLVMBuildAlloca(g->builder, param_types[k], "p");
        LLVMBuildStore(g->builder, LLVMGetParam(sp.fn, (unsigned)k), param_alloca);
        local_add(locals, param->param.name, param_alloca, pt);
    }
    free(param_types);

    zan_type_t *ret_type = member->method_decl.return_type
        ? resolve_type_ctx(g, member->method_decl.return_type)
        : g->binder->type_void;
    LLVMTypeRef llvm_ret = LLVMGetReturnType(sp.fn_type);

    LLVMValueRef saved_fn = g->current_fn;
    LLVMTypeRef saved_fn_ret = g->current_fn_ret_type;
    LLVMValueRef saved_this = g->current_this;
    zan_symbol_t *saved_type_sym = g->current_type_sym;
    zan_ast_node_t *saved_fn_body = g->current_fn_body;
    g->current_fn = sp.fn;
    g->current_fn_ret_type = llvm_ret;
    g->current_this = NULL;
    g->current_type_sym = sp.type_sym;
    g->current_fn_body = member->method_decl.body;

    if (member->method_decl.body->kind == AST_BLOCK) {
        for (int k = 0; k < member->method_decl.body->block.stmts.count; k++) {
            emit_stmt(g, member->method_decl.body->block.stmts.items[k], locals);
        }
    } else {
        LLVMValueRef val = emit_expr(g, member->method_decl.body, locals);
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

zan_status_t zan_irgen_emit(zan_irgen_t *g, zan_ast_node_t *unit) {
    if (!unit || unit->kind != AST_COMPILATION_UNIT) return ZAN_ERROR;
    g_di_emit_ctx = g; /* so local_add can forward variables to di_declare_var */

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
    emit_vtables(g);
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
        zan_diag_emit(g->diag, DIAG_ERROR, zan_loc(0, 0, 0, 0),
                      "LLVM verification failed: %s", error);
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
            /* -1 doubles as SQL_ERROR / a generic nonzero failure code */
            LLVMBuildRet(b, LLVMConstInt(rt, (unsigned long long)-1, 1));
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
    LLVMTargetMachineRef tm = LLVMCreateTargetMachine(
        target, triple, tm_cpu, tm_features,
        LLVMCodeGenLevelDefault, LLVMRelocPIC, LLVMCodeModelDefault);

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
