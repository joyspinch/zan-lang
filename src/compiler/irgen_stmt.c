/* irgen_stmt.c -- statement codegen (emit_stmt and friends).
 *
 * Part of the irgen translation unit: this file is #include'd by irgen.c
 * (in a fixed order) and must not be compiled standalone. Splitting keeps
 * the single-TU static linkage while keeping each concern in its own file.
 */

/* ---- statement codegen ---- */

/* Find or create the basic block for a goto label in the current function. */
static void emit_release_static_rc_fields(zan_irgen_t *g, zan_ast_node_t *unit);
/* Structural type compatibility for async frame-slot reuse: two same-named
 * declarations may share one frame slot only when their types genuinely
 * agree, because the slot's LLVM type and width are baked into the frame
 * struct at plan time. */
static bool async_slot_type_compatible(zan_type_t *a, zan_type_t *b) {
    if (a == b) return true;
    if (!a || !b) return false;
    int depth = 0;
    while (a && b && depth < 32) {
        if (a == b) return true;
        if (a->kind != b->kind) return false;
        if (a->name.len != b->name.len ||
            (a->name.len && memcmp(a->name.str, b->name.str,
                                   (size_t)a->name.len) != 0))
            return false;
        if (a->type_arg_count != b->type_arg_count) return false;
        for (int i = 0; i < a->type_arg_count; i++)
            if (!async_slot_type_compatible(a->type_args[i], b->type_args[i]))
                return false;
        a = a->element_type;
        b = b->element_type;
        depth++;
    }
    return a == b;
}

static LLVMBasicBlockRef irgen_goto_label(zan_irgen_t *g, zan_istr_t name) {
    LLVMValueRef fn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder));
    for (int i = 0; i < g->goto_label_count; i++) {
        if (g->goto_labels[i].fn == fn &&
            g->goto_labels[i].name.len == name.len &&
            memcmp(g->goto_labels[i].name.str, name.str,
                   (size_t)name.len) == 0)
            return g->goto_labels[i].bb;
    }
    if (!ZAN_TAB_ENSURE(g->goto_labels, g->goto_label_count,
                        g->goto_label_cap, 64)) return NULL;
    LLVMBasicBlockRef bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "label");
    g->goto_labels[g->goto_label_count].name = name;
    g->goto_labels[g->goto_label_count].fn = fn;
    g->goto_labels[g->goto_label_count].bb = bb;
    g->goto_label_count++;
    return bb;
}

/* An empty function the debugger can put a breakpoint on, so a DAP client's
 * "break when an exception is thrown / goes unhandled" actually stops. Only
 * emitted for `zanc -g`; one local copy per module. */
static LLVMValueRef get_eh_hook_fn(zan_irgen_t *g, const char *name) {
    LLVMValueRef f = LLVMGetNamedFunction(g->mod, name);
    if (f) return f;
    LLVMTypeRef fty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), NULL, 0, 0);
    f = LLVMAddFunction(g->mod, name, fty);
    /* Internal: a weak external turns into a mangled COFF alias that gdb
     * cannot break on by name, while a local symbol per module gives the
     * breakpoint one location per compilation unit. */
    LLVMSetLinkage(f, LLVMInternalLinkage);
    LLVMBasicBlockRef saved = LLVMGetInsertBlock(g->builder);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, f, "entry");
    LLVMPositionBuilderAtEnd(g->builder, entry);
    LLVMBuildRetVoid(g->builder);
    if (saved) LLVMPositionBuilderAtEnd(g->builder, saved);
    return f;
}

/* Calls one of the debugger hooks above. */
static void emit_eh_hook_call(zan_irgen_t *g, const char *name) {
    if (!g->emit_debug) return;
    LLVMValueRef f = get_eh_hook_fn(g, name);
    LLVMTypeRef fty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), NULL, 0, 0);
    zan_call2(g->builder, fty, f, NULL, 0, "");
}

static void emit_stmt(zan_irgen_t *g, zan_ast_node_t *stmt, local_scope_t *locals);

/* B5: bind a switch case's pattern variable (`case T x:`) to the discriminant.
 * Each call gets a fresh slot in the current scope, so `when` guards and case
 * bodies can reference x. The case-body loop rebinds after every body has
 * truncated locals back to switch_start (the chain-time binding lives at a
 * higher index and is wiped by the first body's scope-exit release). */
static void emit_switch_pattern_bind(zan_irgen_t *g, local_scope_t *locals,
                                     zan_ast_node_t *sc, LLVMValueRef switch_val) {
    if (!sc->switch_case.type_pattern || sc->switch_case.var_name.len == 0) return;
    zan_type_t *pt = resolve_type_ctx(g, sc->switch_case.type_pattern);
    if (!pt) return;
    LLVMTypeRef ptll = map_type(g, pt);
    LLVMValueRef slot = emit_entry_alloca(g, ptll, "cpat");
    zan_store_fit(g, LLVMConstNull(ptll), slot);
    LLVMValueRef cv = switch_val;
    if (LLVMTypeOf(cv) != ptll &&
        LLVMGetTypeKind(ptll) == LLVMPointerTypeKind &&
        LLVMGetTypeKind(LLVMTypeOf(cv)) == LLVMPointerTypeKind)
        cv = LLVMBuildBitCast(g->builder, cv, ptll, "cpat.cast");
    zan_store_fit(g, cv, slot);
    local_add(locals, sc->switch_case.var_name, slot, pt);
}

/* Run the `finally` bodies of the try statements this exit path leaves, from
 * the innermost open one down to (and including) `base`. C# runs them on every
 * way out of a try, and this lowering has nothing to hang a cleanup off, so the
 * bodies are emitted inline once per exit path. While emitting body i the stack
 * is truncated to i, so a `return` inside a finally runs only the finallys
 * outside it -- never itself. */
/* Release the monitor a `lock (obj)` took. The object is reloaded from its
 * alloca so the call works from any exit path, including ones the entry block
 * does not dominate. */
static void emit_monitor_exit(zan_irgen_t *g, LLVMValueRef obj_slot) {
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef mon_ty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx),
                                          &i8ptr, 1, 0);
    LLVMValueRef exit_fn = LLVMGetNamedFunction(g->mod, "zan_monitor_exit");
    if (!exit_fn) exit_fn = LLVMAddFunction(g->mod, "zan_monitor_exit", mon_ty);
    LLVMValueRef obj = LLVMBuildLoad2(g->builder, i8ptr, obj_slot, "lock.obj");
    zan_call2(g->builder, mon_ty, exit_fn, &obj, 1, "");
}

/* Restore __zan_eh_top for an exit path that jumps out of the try statements
 * armed from `base` up (their handlers are disarmed by leaving). Reads the top
 * the outermost of them was armed at, so nested tries need no arithmetic. In an
 * async body the handlers are frame-resident and re-armed per invocation, and
 * the try-entry allocas do not survive a suspension, so those keep their own
 * bookkeeping (frame.hcount). */
static void emit_eh_disarm_from(zan_irgen_t *g, int base) {
    if (base < g->eh_armed_base) base = g->eh_armed_base;
    if (g->eh_armed_count <= base || g->current_async_frame) return;
    LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
    LLVMValueRef top_g, bufs_g, exc_g;
    get_eh_globals(g, &top_g, &bufs_g, &exc_g);
    LLVMValueRef ot = LLVMBuildLoad2(g->builder, i32t,
        g->eh_armed[base].old_top_slot, "eh.old.exit");
    zan_store_fit(g, ot, top_g);
}

static void emit_pending_finallys(zan_irgen_t *g, local_scope_t *locals, int base) {
    if (base < 0) base = 0;
    int saved = g->finally_count;
    for (int i = saved - 1; i >= base; i--) {
        if (g->finallys[i].monitor_obj) {
            emit_monitor_exit(g, g->finallys[i].monitor_obj);
            continue;
        }
        if (!g->finallys[i].body) continue;
        int saved_locals = locals->count;
        g->finally_count = i;
        emit_stmt(g, g->finallys[i].body, locals);
        locals->count = saved_locals;
        if (LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder))) break;
    }
    g->finally_count = saved;
}

/* Run one try's finally body while an exception is in flight, then put the
 * in-flight exception back: the body may run try/catch of its own, and that
 * overwrites the exception globals. */
static void emit_finally_on_exception_path(zan_irgen_t *g, local_scope_t *locals,
                                           int fin_idx) {
    if (fin_idx < 0) return;
    if (g->finallys[fin_idx].monitor_obj) {
        /* no exception state to preserve: the exit call runs no Zan code */
        emit_monitor_exit(g, g->finallys[fin_idx].monitor_obj);
        return;
    }
    if (!g->finallys[fin_idx].body) return;
    LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMValueRef top_g, bufs_g, exc_g;
    get_eh_globals(g, &top_g, &bufs_g, &exc_g);
    LLVMValueRef own_g = get_eh_exc_owned_global(g);
    LLVMValueRef tid_g = get_eh_exc_tid_global(g);
    LLVMValueRef s_exc, s_own, s_tid;
    if (g->current_async_frame && fin_idx < ZAN_MAX_FINALLY_DEPTH) {
        /* the body may await, which returns from this $resume and leaves its
         * allocas behind: keep the exception in the heap frame. Addressed from
         * the entry block, like emit_entry_alloca, so the GEPs dominate the
         * blocks the CPS split puts the reload in. */
        LLVMValueRef idx[2] = { LLVMConstInt(i32t, 0, 0),
                                LLVMConstInt(i32t, (unsigned)fin_idx, 0) };
        LLVMBasicBlockRef cur_bb = LLVMGetInsertBlock(g->builder);
        LLVMBasicBlockRef entry_bb =
            LLVMGetEntryBasicBlock(LLVMGetBasicBlockParent(cur_bb));
        LLVMValueRef entry_term = LLVMGetBasicBlockTerminator(entry_bb);
        if (entry_term) LLVMPositionBuilderBefore(g->builder, entry_term);
        else LLVMPositionBuilderAtEnd(g->builder, entry_bb);
        s_exc = LLVMBuildGEP2(g->builder,
            LLVMArrayType(i8ptr, ZAN_MAX_FINALLY_DEPTH),
            LLVMBuildStructGEP2(g->builder, g->current_async_frame_type,
                g->current_async_frame, ASYNC_FRAME_FINEXC, "fin.f"),
            idx, 2, "fin.exc.slot");
        s_own = LLVMBuildGEP2(g->builder,
            LLVMArrayType(i32t, ZAN_MAX_FINALLY_DEPTH),
            LLVMBuildStructGEP2(g->builder, g->current_async_frame_type,
                g->current_async_frame, ASYNC_FRAME_FINEXC_OWNED, "fin.fo"),
            idx, 2, "fin.own.slot");
        s_tid = LLVMBuildGEP2(g->builder,
            LLVMArrayType(i8ptr, ZAN_MAX_FINALLY_DEPTH),
            LLVMBuildStructGEP2(g->builder, g->current_async_frame_type,
                g->current_async_frame, ASYNC_FRAME_FINEXC_TID, "fin.ft"),
            idx, 2, "fin.tid.slot");
        LLVMPositionBuilderAtEnd(g->builder, cur_bb);
    } else {
        s_exc = emit_entry_alloca(g, i8ptr, "fin.exc");
        s_own = emit_entry_alloca(g, i32t, "fin.own");
        s_tid = emit_entry_alloca(g, i8ptr, "fin.tid");
    }
    zan_store_fit(g, LLVMBuildLoad2(g->builder, i8ptr, exc_g, "fin.exc.v"), s_exc);
    zan_store_fit(g, LLVMBuildLoad2(g->builder, i32t, own_g, "fin.own.v"), s_own);
    zan_store_fit(g, LLVMBuildLoad2(g->builder, i8ptr, tid_g, "fin.tid.v"), s_tid);
    int saved_count = g->finally_count;
    int saved_locals = locals->count;
    g->finally_count = fin_idx;   /* the body must not re-run itself */
    emit_stmt(g, g->finallys[fin_idx].body, locals);
    locals->count = saved_locals;
    g->finally_count = saved_count;
    if (LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder))) return;
    zan_store_fit(g, LLVMBuildLoad2(g->builder, i8ptr, s_exc, "fin.exc.r"), exc_g);
    zan_store_fit(g, LLVMBuildLoad2(g->builder, i32t, s_own, "fin.own.r"), own_g);
    zan_store_fit(g, LLVMBuildLoad2(g->builder, i8ptr, s_tid, "fin.tid.r"), tid_g);
}

/* A throw leaves every enclosing try whose handler is no longer armed for it
 * (its catch or finally body is what is running); those finallys run at the
 * throw site. The try whose handler will take the longjmp keeps its own: the
 * catch path runs it. */
static void emit_finallys_left_by_throw(zan_irgen_t *g, local_scope_t *locals) {
    int base = g->finally_count;
    while (base > 0 && !g->finallys[base - 1].in_try_body) base--;
    for (int i = g->finally_count - 1; i >= base; i--) {
        emit_finally_on_exception_path(g, locals, i);
        if (LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder))) return;
    }
}

/* Hand the in-flight exception (still in the globals) to the next outer
 * handler, or report it unhandled and exit. */
static void emit_eh_propagate_tail(zan_irgen_t *g) {
    LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMValueRef top_g, bufs_g, exc_g;
    get_eh_globals(g, &top_g, &bufs_g, &exc_g);
    LLVMValueRef fn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder));
    LLVMValueRef rtop = LLVMBuildLoad2(g->builder, i32t, top_g, "reh.top");
    LLVMValueRef rhas = zan_icmp(g->builder, LLVMIntSGE, rtop,
        LLVMConstInt(i32t, 0, 0), "reh.has");
    LLVMBasicBlockRef rjmp_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "reh.jmp");
    LLVMBasicBlockRef rdie_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "reh.die");
    LLVMBuildCondBr(g->builder, rhas, rjmp_bb, rdie_bb);
    LLVMPositionBuilderAtEnd(g->builder, rjmp_bb);
    /* an exception leaving this coroutine now travels through the frame (see
     * emit_async_exc_epilogue), so the longjmp stays inside this invocation --
     * no cross-frame unwinding here */
    if (g->current_async_frame) emit_async_save_slots(g);
    emit_eh_longjmp(g, emit_eh_buf_ptr(g, rtop));
    LLVMBuildUnreachable(g->builder);
    LLVMPositionBuilderAtEnd(g->builder, rdie_bb);
    emit_eh_hook_call(g, "__zan_eh_unhandled");
    LLVMValueRef printf_fn = LLVMGetNamedFunction(g->mod, "printf");
    if (printf_fn) {
        LLVMTypeRef printf_ty = LLVMFunctionType(i32t, &i8ptr, 1, 1);
        /* Report the in-flight exception so an uncaught throw in a published
         * app shows WHY it died, not just "Unhandled exception". A string
         * throw prints its text (string user data is NUL-terminated); a class
         * throw prints a type note -- its ToString would need a vtable call
         * this tail block cannot afford. */
        LLVMValueRef exc = LLVMBuildLoad2(g->builder, i8ptr, exc_g, "reh.exc");
        LLVMValueRef tid = LLVMBuildLoad2(g->builder, i8ptr,
            get_eh_exc_tid_global(g), "reh.tid");
        LLVMValueRef hasExc = zan_icmp(g->builder, LLVMIntNE, exc,
            LLVMConstNull(i8ptr), "reh.has");
        LLVMValueRef isStr = zan_icmp(g->builder, LLVMIntEQ, tid,
            LLVMConstNull(i8ptr), "reh.isstr");
        LLVMBasicBlockRef reh_check_bb =
            LLVMAppendBasicBlockInContext(g->ctx, fn, "reh.check");
        LLVMBasicBlockRef reh_str_bb =
            LLVMAppendBasicBlockInContext(g->ctx, fn, "reh.str");
        LLVMBasicBlockRef reh_cls_bb =
            LLVMAppendBasicBlockInContext(g->ctx, fn, "reh.cls");
        LLVMBasicBlockRef reh_none_bb =
            LLVMAppendBasicBlockInContext(g->ctx, fn, "reh.none");
        LLVMBasicBlockRef reh_cont_bb =
            LLVMAppendBasicBlockInContext(g->ctx, fn, "reh.cont");
        LLVMBuildCondBr(g->builder, hasExc, reh_check_bb, reh_none_bb);
        LLVMPositionBuilderAtEnd(g->builder, reh_check_bb);
        LLVMBuildCondBr(g->builder, isStr, reh_str_bb, reh_cls_bb);
        /* string throw: print the message itself */
        LLVMPositionBuilderAtEnd(g->builder, reh_str_bb);
        {
            LLVMValueRef sfmt = LLVMBuildGlobalStringPtr(g->builder,
                "Unhandled exception: %s\n", "reh.sfmt");
            LLVMValueRef sargs[2] = { sfmt, exc };
            zan_call2(g->builder, printf_ty, printf_fn, sargs, 2, "");
        }
        LLVMBuildBr(g->builder, reh_cont_bb);
        /* class throw: name the category, the object is opaque here */
        LLVMPositionBuilderAtEnd(g->builder, reh_cls_bb);
        {
            LLVMValueRef cfmt = LLVMBuildGlobalStringPtr(g->builder,
                "Unhandled exception (class object)\n", "reh.cfmt");
            zan_call2(g->builder, printf_ty, printf_fn, &cfmt, 1, "");
        }
        LLVMBuildBr(g->builder, reh_cont_bb);
        /* no exception object in flight (internal rethrow miss) */
        LLVMPositionBuilderAtEnd(g->builder, reh_none_bb);
        {
            LLVMValueRef nfmt = LLVMBuildGlobalStringPtr(g->builder,
                "Unhandled exception\n", "reh.nfmt");
            zan_call2(g->builder, printf_ty, printf_fn, &nfmt, 1, "");
        }
        LLVMBuildBr(g->builder, reh_cont_bb);
        LLVMPositionBuilderAtEnd(g->builder, reh_cont_bb);
    }
    LLVMTypeRef exit_ty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), &i32t, 1, 0);
    LLVMValueRef exit_fn = get_libc_fn(g, "exit", exit_ty);
    LLVMValueRef one = LLVMConstInt(i32t, 1, 0);
    zan_call2(g->builder, exit_ty, exit_fn, &one, 1, "");
    LLVMBuildUnreachable(g->builder);
}

/* True when `expr` is a call to an extern/DllImport function. Extern calls
 * hand back raw pointers typed `string` (calloc/malloc views, FFI buffers);
 * their NUL terminator is not a reliable ordinal bound, so a string local fed
 * from one is treated as an opaque byte buffer by the string index guards. */
static int call_targets_extern(zan_irgen_t *g, zan_ast_node_t *expr) {
    if (!expr || expr->kind != AST_CALL || !expr->call.callee) return 0;
    zan_symbol_t *sym = NULL;
    if (expr->call.callee->kind == AST_IDENTIFIER) {
        /* A static method of the enclosing class (`calloc(...)` inside the
         * class that declares it) resolves through the type's own members;
         * zan_binder_lookup keys off the binder's current scope, which irgen
         * may not still sit in. */
        if (g->current_type_sym)
            sym = get_method_sym(g->current_type_sym,
                                 expr->call.callee->ident.name);
        if (!sym)
            sym = zan_binder_lookup(g->binder, expr->call.callee->ident.name);
    } else if (expr->call.callee->kind == AST_MEMBER_ACCESS &&
               expr->call.callee->member.object->kind == AST_IDENTIFIER) {
        /* ClassName.ExternMethod(...) */
        zan_symbol_t *cls = zan_binder_lookup(g->binder,
            expr->call.callee->member.object->ident.name);
        if (cls) sym = get_method_sym(cls, expr->call.callee->member.name);
    }
    if (!sym || !sym->decl || sym->decl->kind != AST_METHOD_DECL) return 0;
    return sym->decl->method_decl.extern_lib.str != NULL ||
           (sym->decl->method_decl.modifiers & MOD_EXTERN) != 0;
}

/* A33-2b: declare a local that a lambda assigns to. Its storage is a heap cell
 * shared with every closure that captures it (see the boxed-local comment in
 * irgen_expr.c), so both sides read and write the one variable. Returns 0 for
 * an ordinary local, which the general path below then handles.
 *
 * Scalars and rc-managed references only; an async local already lives in the
 * coroutine frame, which every resumption shares. */
static int emit_boxed_var_decl(zan_irgen_t *g, zan_ast_node_t *stmt,
                               local_scope_t *locals) {
    if (g->current_async_frame || !g->current_fn_body) return 0;
    zan_type_t *type = stmt->var_decl.type
        ? resolve_type_ctx(g, stmt->var_decl.type)
        : (stmt->var_decl.initializer
               ? infer_expr_type(g, stmt->var_decl.initializer, locals)
               : NULL);
    if (!type) return 0;
    LLVMTypeRef payload = map_type(g, type);
    LLVMTypeKind k = LLVMGetTypeKind(payload);
    int rc = is_rc_managed_type(type) && k == LLVMPointerTypeKind;
    if (!rc && k != LLVMIntegerTypeKind && k != LLVMFloatTypeKind &&
        k != LLVMDoubleTypeKind) return 0;
    if (!local_is_lambda_written(g, locals, stmt->var_decl.name)) return 0;

    /* the cell is born holding a null payload, so its destructor is safe even
     * if the initializer throws, and the first capture releases nothing */
    LLVMValueRef cell = emit_box_cell(g, stmt->loc, payload, type, NULL);
    LLVMValueRef slot = box_value_ptr(g, cell, payload);
    local_add(locals, stmt->var_decl.name, slot, type);
    locals->vars[locals->count - 1].box_cell = cell;
    locals->vars[locals->count - 1].box_owned = 1;
    if (rc) locals->vars[locals->count - 1].arc_owned = 1;
    if (type && type->kind == TYPE_STRING && stmt->var_decl.initializer &&
        call_targets_extern(g, stmt->var_decl.initializer))
        locals->vars[locals->count - 1].opaque_string = 1;
    if (stmt->var_decl.initializer) {
        LLVMValueRef init =
            (type->kind == TYPE_DELEGATE &&
             stmt->var_decl.initializer->kind == AST_LAMBDA)
                ? emit_lambda_typed(g, stmt->var_decl.initializer, type, locals)
                : emit_expr(g, stmt->var_decl.initializer, locals);
        if (rc)
            emit_rc_capture_local(g, type, slot, init,
                                  stmt->var_decl.initializer, locals);
        else
            zan_store_fit(g, init, slot);
    }
    return 1;
}

static void emit_stmt(zan_irgen_t *g, zan_ast_node_t *stmt, local_scope_t *locals) {
    if (!stmt) return;

    /* Unreachable code: the previous statement already terminated this block
     * (return / throw / break / continue), so anything after it belongs to a
     * fresh, unreachable block -- appending to the terminated one would build
     * "terminator in the middle of a basic block". Platform-conditional
     * sources hit this constantly (`throw new PlatformNotSupportedException();
     * return info;` on the platform where the body is #if'd out). */
    LLVMBasicBlockRef cur_bb = LLVMGetInsertBlock(g->builder);
    if (cur_bb && LLVMGetBasicBlockTerminator(cur_bb)) {
        LLVMBasicBlockRef dead_bb = LLVMAppendBasicBlockInContext(
            g->ctx, LLVMGetBasicBlockParent(cur_bb), "dead");
        LLVMPositionBuilderAtEnd(g->builder, dead_bb);
    }

    /* Attach this statement's source location for DWARF line tables (no-op
     * unless `-g`). Lazily creates the enclosing function's DISubprogram. */
    di_set_loc(g, stmt->loc);

    /* ARC: track control-flow nesting so class locals declared inside a
     * conditional/loop body (whose stack slot does not dominate the exit) are
     * not registered as owning references. Every branch here ends in `break`,
     * so the matching decrement below always runs. */
    int arc_nested = (stmt->kind == AST_IF_STMT || stmt->kind == AST_WHILE_STMT ||
                      stmt->kind == AST_DO_WHILE_STMT || stmt->kind == AST_FOR_STMT ||
                      stmt->kind == AST_FOREACH_STMT || stmt->kind == AST_SWITCH_STMT ||
                      stmt->kind == AST_TRY_STMT);
    if (arc_nested) g->arc_stmt_depth++;

    switch (stmt->kind) {
    case AST_BLOCK: {
        int block_start = locals->count;
        for (int i = 0; i < stmt->block.stmts.count; i++) {
            zan_ast_node_t *bs = stmt->block.stmts.items[i];
            emit_stmt(g, bs, locals);
            /* an await handed control back to the scheduler, which may have
             * run Task.Cancel on this coroutine; observe it here, at a
             * statement boundary where completing is an early `return` */
            if (g->current_async_frame && anf_stmt_contains_await(bs))
                emit_async_cancel_check(g, locals);
        }
        if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder))) {
            emit_release_owned_locals_from(g, locals, block_start);
        }
        break;
    }

    case AST_VAR_DECL: {
        /* `int fd = SomethingReturningNint()` is the same silent truncation as
         * an assignment or an argument, and is how handle carriers typed `int`
         * usually enter a function. */
        if (stmt->var_decl.type && stmt->var_decl.initializer) {
            check_implicit_narrowing(g, resolve_type_ctx(g, stmt->var_decl.type),
                           infer_expr_type(g, stmt->var_decl.initializer, locals),
                           stmt->var_decl.initializer, "initializer");
        }
        /* A33-2b: a local a lambda assigns to is one variable shared with the
         * closure, so it is declared in a heap cell instead of the frame. */
        if (emit_boxed_var_decl(g, stmt, locals)) return;
        /* In an async $resume body, named scalar locals were pre-allocated in
         * the entry block and their storage lives in the heap frame (so they
         * survive suspensions). Reuse that slot instead of a fresh alloca:
         * just evaluate the initializer and store into it. */
        if (g->current_async_frame && g->current_async_slot_count > 0) {
            local_var_t *pre = local_find(locals, stmt->var_decl.name);
            if (pre) {
                /* Resolve this declaration's own type -- explicitly written or
                 * inferred from its initializer -- before deciding whether it
                 * may reuse the frame slot a previous same-named declaration
                 * scanned into the frame struct. */
                zan_type_t *type = stmt->var_decl.type
                    ? resolve_type_ctx(g, stmt->var_decl.type)
                    : NULL;
                if (!type && stmt->var_decl.initializer)
                    type = infer_expr_type(g, stmt->var_decl.initializer, locals);
                if (!type) type = pre->type;
                for (int i = 0; i < g->current_async_slot_count; i++) {
                    /* A shadowing declaration with a different type must NOT
                     * reuse the slot: the slot's LLVM type is baked into the
                     * frame struct, so storing through it would type-confuse
                     * both variables (the frame save/reload would also move
                     * the wrong width). Skip the reuse and let the ordinary
                     * scoped declaration below shadow it instead. */
                    if (g->current_async_slots[i].slot_alloca == pre->alloca &&
                        !async_slot_type_compatible(type, pre->type))
                        continue;
                    if (g->current_async_slots[i].slot_alloca == pre->alloca) {
                        /* Own every rc-managed local, including those declared
                         * inside loop/if/block bodies (arc_stmt_depth != 0):
                         * per-block release at scope exit (emit_release_owned_
                         * locals_from) frees them each iteration and truncates
                         * them out of scope, so there is no double-release at
                         * function exit. Gating on top-level only leaked one
                         * object per iteration for class-typed loop locals.
                         *
                         * A string local initialized from a call (e.g. `string
                         * cur = Get(key)`) is owned exactly like the synchronous
                         * path: the callee hands back a +1 reference (borrowed
                         * returns are retained before return), and
                         * emit_rc_capture_local moves it in without an extra
                         * retain. Excluding such locals from ownership leaked one
                         * string per async call. */
                        int arc_own = (type && is_rc_managed_type(type) &&
                                       LLVMGetTypeKind(g->current_async_slots[i].llvm) == LLVMPointerTypeKind);
                        /* No null-init here: the frame is zeroed at allocation
                         * and the slot is reloaded at the top of this state block,
                         * so emit_rc_capture_local's release-of-old correctly frees
                         * the previous iteration's occupant instead of a clobbered
                         * null. */
                        if (stmt->var_decl.initializer) {
                            LLVMValueRef iv = emit_expr(g, stmt->var_decl.initializer, locals);
                            LLVMTypeRef slot_ty = g->current_async_slots[i].llvm;
                            if (arc_own) {
                                emit_rc_capture_local(g, type, pre->alloca, iv,
                                    stmt->var_decl.initializer, locals);
                            } else {
                                iv = coerce_int_to(g, iv, slot_ty);
                                /* pointer-shaped locals (string/array/List/class) are
                                 * frame-resident too; normalize a differing pointer
                                 * type to the slot's before the store. */
                                if (LLVMGetTypeKind(slot_ty) == LLVMPointerTypeKind &&
                                    LLVMGetTypeKind(LLVMTypeOf(iv)) == LLVMPointerTypeKind &&
                                    LLVMTypeOf(iv) != slot_ty) {
                                    iv = LLVMBuildBitCast(g->builder, iv, slot_ty, "fl.bc");
                                }
                                zan_store_fit(g, iv, pre->alloca);
                            }
                        }
                        if (arc_own) pre->arc_owned = 1;
                        /* A frame-resident string fed from an extern call is a
                         * raw byte buffer (same rule as the sync path). */
                        if (type && type->kind == TYPE_STRING &&
                            stmt->var_decl.initializer &&
                            call_targets_extern(g, stmt->var_decl.initializer))
                            pre->opaque_string = 1;
                        return;
                    }
                }
            }
        }
        zan_type_t *type = g->binder->type_int; /* default */
        if (stmt->var_decl.type) {
            type = resolve_type_ctx(g, stmt->var_decl.type);
        } else if (stmt->var_decl.initializer) {
            zan_ast_node_t *init = stmt->var_decl.initializer;

            /* check if initializer is array creation: new int[5] */
            if (init->kind == AST_NEW_EXPR && init->new_expr.is_array) {
                LLVMValueRef arr_val = emit_expr(g, init, locals);
                LLVMTypeRef ptr_type = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                LLVMValueRef alloca = emit_entry_alloca(g, ptr_type, "arr");
                zan_store_fit(g, arr_val, alloca);

                /* the allocation node is written `T[]`, so it already resolves
                 * to the array type: wrapping it again made `var a = new T[n]`
                 * a T[][] and lost T's own type arguments. */
                zan_type_t *arr_type = resolve_type_ctx(g, init->new_expr.type);
                if (!arr_type || arr_type->kind != TYPE_ARRAY) {
                    zan_type_t *elem_type = arr_type ? arr_type : g->binder->type_int;
                    arr_type = (zan_type_t *)zan_arena_alloc(g->arena, sizeof(zan_type_t));
                    memset(arr_type, 0, sizeof(zan_type_t));
                    arr_type->kind = TYPE_ARRAY;
                    arr_type->element_type = elem_type;
                    arr_type->array_rank = 1;
                }
                local_add(locals, stmt->var_decl.name, alloca, arr_type);
                /* `new T[n]` yields an owned (+1) buffer that was stored
                 * directly above, so the slot owns it: scope exit releases. */
                arc_own_local(g, locals);
                return;
            }

            /* check if initializer is a List creation: new List<int>() */
            if (init->kind == AST_NEW_EXPR && init->new_expr.type &&
                init->new_expr.type->kind == AST_TYPE_REF) {
                zan_istr_t tname = init->new_expr.type->type_ref.name;
                if (tname.len == 4 && memcmp(tname.str, "List", 4) == 0) {
                    LLVMValueRef list_val = emit_expr(g, init, locals);
                    LLVMTypeRef ptr_type = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                    LLVMValueRef alloca = emit_entry_alloca(g, ptr_type, "list");
                    zan_store_fit(g, list_val, alloca);
                    zan_type_t *list_type = resolve_type_ctx(g, init->new_expr.type);
                    local_add(locals, stmt->var_decl.name, alloca, list_type);
                    /* List is refcounted: `new` yields an owned (+1) reference, so
                     * this local owns it and must release it at scope exit. */
                    if (list_type && is_rc_managed_type(list_type))
                        arc_own_local(g, locals);
                    return;
                }
                if (tname.len == 13 && memcmp(tname.str, "StringBuilder", 13) == 0) {
                    LLVMValueRef sb_val = emit_expr(g, init, locals);
                    LLVMTypeRef ptr_type = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                    LLVMValueRef alloca = emit_entry_alloca(g, ptr_type, "sb");
                    zan_store_fit(g, sb_val, alloca);
                    zan_type_t *sb_type = resolve_type_ctx(g, init->new_expr.type);
                    local_add(locals, stmt->var_decl.name, alloca, sb_type);
                    /* StringBuilder is refcounted: `new` yields an owned (+1)
                     * reference this local owns and releases at scope exit. */
                    if (sb_type && is_rc_managed_type(sb_type))
                        arc_own_local(g, locals);
                    return;
                }
                if ((tname.len == 4 && memcmp(tname.str, "Dict", 4) == 0) ||
                    (tname.len == 10 && memcmp(tname.str, "Dictionary", 10) == 0)) {
                    LLVMValueRef dict_val = emit_expr(g, init, locals);
                    LLVMTypeRef ptr_type = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                    LLVMValueRef alloca = emit_entry_alloca(g, ptr_type, "dict");
                    zan_store_fit(g, dict_val, alloca);
                    zan_type_t *dict_type = resolve_type_ctx(g, init->new_expr.type);
                    local_add(locals, stmt->var_decl.name, alloca, dict_type);
                    /* This local owns the freshly-built dict: release its
                     * rc-managed keys/values at scope exit. */
                    arc_own_local(g, locals);
                    return;
                }
            }

            /* check if initializer is a struct construction: Point { X = 3.0, Y = 4.0 } */
            if (init->kind == AST_NEW_EXPR && init->new_expr.type) {
                zan_istr_t type_name = {NULL, 0};
                if (init->new_expr.type->kind == AST_IDENTIFIER) {
                    type_name = init->new_expr.type->ident.name;
                } else if (init->new_expr.type->kind == AST_TYPE_REF) {
                    type_name = init->new_expr.type->type_ref.name;
                }
                if (type_name.str) {
                    zan_symbol_t *sym = zan_binder_lookup(g->binder, type_name);
                    if (sym && sym->type && (sym->type->kind == TYPE_STRUCT || sym->type->kind == TYPE_CLASS)) {
                        type = sym->type;
                        LLVMTypeRef st = get_struct_llvm_type(g, sym);
                        if (st) {
                            LLVMValueRef alloca = emit_entry_alloca(g, st, "var");
                            zan_store_fit(g, LLVMConstNull(st), alloca);

                            /* try calling constructor */
                            bool ctor_called = false;
                            zan_type_t *new_inst = resolve_type_ctx(
                                g, init->new_expr.type);
                            /* object-initializer tail: `Field = v` writes and
                             * member collection initializers both trail any
                             * positional constructor arguments (mirrors the
                             * init_start walk in emit_expr_new_expr). A
                             * postfix generic-type initializer keeps its
                             * member-writes in arg_inits (args is empty). */
                            int init_start = init->new_expr.args.count;
                            while (init_start > 0) {
                                zan_ast_node_t *a =
                                    init->new_expr.args.items[init_start - 1];
                                if (a->kind == AST_COLL_INIT) {
                                    if (get_field_index(sym, a->coll_init.name) < 0)
                                        break;
                                    init_start--;
                                    continue;
                                }
                                if (a->kind != AST_ASSIGNMENT ||
                                    a->binary.left->kind != AST_IDENTIFIER ||
                                    get_field_index(sym,
                                        a->binary.left->ident.name) < 0)
                                    break;
                                init_start--;
                            }
                            zan_ast_list_t ctor_args = init->new_expr.args;
                            ctor_args.count = init_start;
                            struct zan_ctor_entry *ctor = find_ctor(
                                g, sym, &ctor_args, locals, NULL);
                            if (!ctor) {
                                zan_ast_list_t filled;
                                if (fill_ctor_default_args(g, sym, &ctor_args,
                                                           &filled)) {
                                    struct zan_ctor_entry *dc = find_ctor(
                                        g, sym, &filled, locals, NULL);
                                    if (dc) {
                                        ctor = dc;
                                        ctor_args = filled;
                                    }
                                }
                            }
                            if (ctor) {
                                int argc = ctor_args.count + 1;
                                LLVMValueRef *call_args = (LLVMValueRef *)calloc(
                                    (size_t)argc, sizeof(LLVMValueRef));
                                call_args[0] = alloca;
                                for (int k = 0; k < ctor_args.count; k++) {
                                    zan_ast_node_t *param =
                                        ctor->decl->method_decl.params.items[k];
                                    zan_type_t *pt = zan_binder_resolve_type(
                                        g->binder, param->param.type);
                                    call_args[k + 1] = emit_arg_typed(
                                        g, ctor_args.items[k], pt, locals);
                                }
                                coerce_args_to_params(g, ctor->fn_type, call_args, argc);
                                zan_call2(g->builder, ctor->fn_type, ctor->fn,
                                    call_args, (unsigned)argc, "");
                                for (int k = 0; k < ctor_args.count; k++)
                                    emit_release_owned_call_temp(g,
                                        ctor_args.items[k],
                                        call_args[k + 1], locals);
                                free(call_args);
                                ctor_called = true;
                            }

                            /* object-initializer member writes after the
                             * constructor (or after the implicit field
                             * initializers when no constructor ran) */
                            if (!ctor_called) {
                                emit_implicit_field_initializers(
                                    g, sym, new_inst ? new_inst : sym->type,
                                    alloca, locals);
                            }
                            /* Ordinary initializers trail constructor
                             * arguments in args; the generic-postfix form
                             * stores the same tail in arg_inits. Present both
                             * shapes as one list for identical lowering. */
                            zan_ast_list_t object_inits;
                            zan_ast_list_init(&object_inits);
                            for (int oi = init_start;
                                 oi < init->new_expr.args.count; oi++) {
                                zan_ast_list_push(&object_inits,
                                    init->new_expr.args.items[oi], g->arena);
                            }
                            for (int oi = 0;
                                 oi < init->new_expr.arg_inits.count; oi++) {
                                zan_ast_list_push(&object_inits,
                                    init->new_expr.arg_inits.items[oi], g->arena);
                            }
                            for (int i = 0; i < object_inits.count; i++) {
                                zan_ast_node_t *arg = object_inits.items[i];
                                /* `Members = { a, b }`: same synthetic
                                 * `member.Add(item)` lowering as the
                                 * emit_expr_new_expr path, sharing its ARC
                                 * handling through the ordinary call emitter. */
                                if (arg->kind == AST_COLL_INIT) {
                                    zan_istr_t cname = arg->coll_init.name;
                                    zan_symbol_t *msym = get_field_sym(sym, cname);
                                    if (!msym || !msym->type) continue;
                                    int getter_owned = 0;
                                    zan_ast_node_t *recv = zan_ast_new(g->arena,
                                        AST_IDENTIFIER, arg->loc);
                                    recv->ident.name = cname;
                                    LLVMTypeRef coll_lt = map_type(g, msym->type);
                                    LLVMValueRef cslot = emit_entry_alloca(g,
                                        coll_lt, "cinit.slot");
                                    zan_store_fit(g, LLVMConstNull(coll_lt), cslot);
                                    int cfi = get_field_index(sym, cname);
                                    zan_symbol_t *getter =
                                        property_getter_sym(g, msym);
                                    if (getter) {
                                        LLVMValueRef v = emit_property_getter_call(
                                            g, getter,
                                            new_inst ? new_inst : sym->type,
                                            alloca, arg, locals);
                                        zan_store_fit(g, v, cslot);
                                        /* the getter handed us +1 on the
                                         * collection; the temp slot took that
                                         * reference and is dropped without a
                                         * scope-exit release, so hand it back
                                         * once the Adds are done */
                                        getter_owned = 1;
                                    } else if (cfi >= 0) {
                                        LLVMValueRef cptr = emit_field_ptr(g, sym,
                                            st, alloca, cfi, "cinit.ptr");
                                        LLVMValueRef v = LLVMBuildLoad2(g->builder,
                                            coll_lt, cptr, "cinit.load");
                                        zan_store_fit(g, v, cslot);
                                    } else {
                                        continue;
                                    }
                                    local_add(locals, cname, cslot, msym->type);
                                    for (int k = 0;
                                         k < arg->coll_init.items.count; k++) {
                                        zan_ast_node_t *item =
                                            arg->coll_init.items.items[k];
                                        zan_ast_node_t *madd = zan_ast_new(g->arena,
                                            AST_MEMBER_ACCESS, item->loc);
                                        madd->member.object = recv;
                                        madd->member.name = (zan_istr_t){"Add", 3};
                                        madd->member.null_cond = 0;
                                        zan_ast_node_t *addcall = zan_ast_new(
                                            g->arena, AST_CALL, item->loc);
                                        addcall->call.callee = madd;
                                        zan_ast_list_init(&addcall->call.args);
                                        zan_ast_list_init(&addcall->call.type_args);
                                        zan_ast_list_push(&addcall->call.args,
                                                          item, g->arena);
                                        if (msym->type &&
                                            type_named(msym->type, "Dict", 4) &&
                                            k + 1 < arg->coll_init.items.count) {
                                            zan_ast_list_push(&addcall->call.args,
                                                arg->coll_init.items.items[++k],
                                                g->arena);
                                        }
                                        emit_expr(g, addcall, locals);
                                    }
                                    for (int lv = locals->count - 1; lv >= 0; lv--) {
                                        if (locals->vars[lv].name.len == cname.len &&
                                            memcmp(locals->vars[lv].name.str,
                                                   cname.str, (size_t)cname.len) == 0) {
                                            for (int sh = lv; sh < locals->count - 1; sh++)
                                                locals->vars[sh] = locals->vars[sh + 1];
                                            locals->count--;
                                            break;
                                        }
                                    }
                                    if (getter_owned) {
                                        LLVMValueRef cv = LLVMBuildLoad2(g->builder,
                                            coll_lt, cslot, "cinit.done");
                                        emit_rc_release_for_type(g, msym->type, cv);
                                    }
                                    continue;
                                }
                                if (arg->kind == AST_ASSIGNMENT && arg->binary.left->kind == AST_IDENTIFIER) {
                                    zan_symbol_t *fsym = get_field_sym(sym, arg->binary.left->ident.name);
                                    /* custom-setter property in an object
                                     * initializer: `new Foo { Prop = v }`
                                     * dispatches to set_Prop(v) */
                                    zan_symbol_t *setter = property_setter_sym(g, fsym);
                                    if (setter) {
                                        emit_property_setter_call(g, setter,
                                            new_inst ? new_inst : sym->type,
                                            alloca, emit_expr(g, arg->binary.right, locals),
                                            arg->binary.left, arg->binary.right, locals);
                                        continue;
                                    }
                                    int fi = get_field_index(sym, arg->binary.left->ident.name);
                                    if (fi >= 0) {
                                        LLVMValueRef fptr = emit_field_ptr(g, sym, st, alloca, fi, "finit");
                                        LLVMValueRef fval = emit_expr(g, arg->binary.right, locals);
                                        if (fsym && fsym->type) {
                                            LLVMTypeRef target_t = map_type(g, fsym->type);
                                            LLVMTypeRef val_t = LLVMTypeOf(fval);
                                            if (LLVMGetTypeKind(target_t) == LLVMFloatTypeKind &&
                                                LLVMGetTypeKind(val_t) == LLVMDoubleTypeKind) {
                                                fval = LLVMBuildFPTrunc(g->builder, fval, target_t, "trunc");
                                            } else if (LLVMGetTypeKind(target_t) == LLVMDoubleTypeKind &&
                                                       LLVMGetTypeKind(val_t) == LLVMFloatTypeKind) {
                                                fval = LLVMBuildFPExt(g->builder, fval, target_t, "ext");
                                            }
                                        }
                                        zan_store_fit(g, fval, fptr);
                                    }
                                }
                            }
                            local_add(locals, stmt->var_decl.name, alloca, type);
                            return;
                        }
                    }
                }
            }

            /* tuple initializer: `var t = (a, b, ...)` is a value of the
             * synthesized anonymous tuple struct; the local keeps the whole
             * struct (so `t.Item1` resolves), copied by value from the temp
             * slot the tuple emitter built. */
            if (init->kind == AST_TUPLE_EXPR) {
                zan_type_t *ttype = infer_expr_type(g, init, locals);
                if (ttype && ttype->kind == TYPE_STRUCT && ttype->sym) {
                    /* map_type lazily registers a synthesized tuple struct
                     * that was never seen by irgen pass 1 (e.g. a decon RHS
                     * the checker did not visit); fall back to it so the
                     * slot and the local are both typed as the struct. */
                    LLVMTypeRef st = get_struct_llvm_type(g, ttype->sym);
                    if (!st) st = map_type(g, ttype);
                    if (st) {
                        LLVMValueRef alloca = emit_entry_alloca(g, st, "tup");
                        zan_store_fit(g, LLVMConstNull(st), alloca);
                        LLVMValueRef tup = emit_expr(g, init, locals);
                        LLVMValueRef tupval =
                            LLVMGetTypeKind(LLVMTypeOf(tup)) == LLVMPointerTypeKind
                                ? LLVMBuildLoad2(g->builder, st, tup, "tup.load")
                                : tup;
                        zan_store_fit(g, tupval, alloca);
                        local_add(locals, stmt->var_decl.name, alloca, ttype);
                        return;
                    }
                }
            }

            /* regular type inference from initializer */
            LLVMValueRef init_val = emit_expr(g, init, locals);
            LLVMTypeRef init_type = LLVMTypeOf(init_val);
            LLVMValueRef alloca = emit_entry_alloca(g, init_type, "var");
            zan_store_fit(g, init_val, alloca);
            if (LLVMGetTypeKind(init_type) == LLVMDoubleTypeKind) {
                type = g->binder->type_double;
            } else if (LLVMGetTypeKind(init_type) == LLVMFloatTypeKind) {
                type = g->binder->type_float;
            } else if (LLVMGetTypeKind(init_type) == LLVMPointerTypeKind) {
                type = g->binder->type_string;
            } else if (LLVMGetTypeKind(init_type) == LLVMIntegerTypeKind) {
                unsigned bits = LLVMGetIntTypeWidth(init_type);
                if (bits <= 32) type = g->binder->type_int;
                else type = g->binder->type_long;
            } else if (llvm_is_nullable(init_type)) {
                /* `var v = maybe;` keeps the nullable type, so `v.HasValue`
                 * still resolves. */
                zan_type_t *it = infer_expr_type(g, init, locals);
                type = (it && it->kind == TYPE_NULLABLE) ? it : g->binder->type_int;
            } else {
                type = g->binder->type_int;
            }
            if (type && type->kind == TYPE_STRING) {
                LLVMTypeRef llvm_string = map_type(g, type);
                LLVMValueRef slot = emit_entry_alloca(g, llvm_string, "var");
                zan_store_fit(g, LLVMConstNull(llvm_string), slot);
                emit_rc_capture_local(g, type, slot, init_val, init, locals);
                local_add(locals, stmt->var_decl.name, slot, type);
                if (call_targets_extern(g, init))
                    locals->vars[locals->count - 1].opaque_string = 1;
                arc_own_local(g, locals);
                return;
            }
            local_add(locals, stmt->var_decl.name, alloca, type);
            return;
        }

        LLVMTypeRef llvm_type = map_type(g, type);
        LLVMValueRef alloca = (type && type->kind == TYPE_STRING)
            ? emit_entry_alloca(g, llvm_type, "var")
            : emit_entry_alloca(g, llvm_type, "var");

        /* ARC: a class-typed local holds an owning heap reference. Track every
         * rc-managed local, including those declared inside loop/if/block
         * bodies: each enclosing block releases its owned locals at scope exit
         * (emit_release_owned_locals_from) and truncates them out of scope, so
         * they are freed once per iteration and never double-released at
         * function exit. (Gating on arc_stmt_depth==0 leaked one object per
         * iteration for class-typed loop/if locals.) Null-init so the release
         * at exit is safe even before the initializer has stored anything. */
        int arc_own = (type && is_rc_managed_type(type) &&
                       LLVMGetTypeKind(llvm_type) == LLVMPointerTypeKind);
        if (arc_own) zan_store_fit(g, LLVMConstNull(llvm_type), alloca);
        /* A declared-but-unassigned string has no null form in the language:
         * every read of it (`s == ""`, `s.Length`, printing it) dereferenced
         * a null pointer and took the process down. It starts empty instead,
         * which is what every other unassigned type does (0 / false). */
        if (type && type->kind == TYPE_STRING && !stmt->var_decl.initializer) {
            zan_istr_t empty = { (char *)"", 0 };
            zan_store_fit(g, emit_string_literal_rc(g, empty), alloca);
        }
        /* An `object` local decides ownership per stored value at runtime (see
         * the object-local comment in irgen_generics.c). Its flag lives with the
         * slot, which is only registered as a local further down, so track it
         * on a stand-in until then. */
        local_var_t obj_slot;
        memset(&obj_slot, 0, sizeof(obj_slot));
        obj_slot.alloca = alloca;
        obj_slot.type = type;
        int obj_own = local_is_dyn_obj(g, &obj_slot);
        if (obj_own) zan_store_fit(g, LLVMConstNull(llvm_type), alloca);

        if (stmt->var_decl.initializer) {
            check_value_type_mismatch(g, type,
                infer_expr_type(g, stmt->var_decl.initializer, locals),
                stmt->var_decl.initializer, "initializer");
            check_generic_invariance(g, type,
                infer_expr_type(g, stmt->var_decl.initializer, locals),
                stmt->var_decl.initializer, "initializer");
            if (type && type->kind == TYPE_DELEGATE &&
                stmt->var_decl.initializer->kind != AST_LAMBDA)
                check_delegate_async_match(g, stmt->var_decl.initializer, type, locals);
            LLVMValueRef init_val =
                (type && type->kind == TYPE_DELEGATE &&
                 stmt->var_decl.initializer->kind == AST_LAMBDA)
                    ? emit_lambda_typed(g, stmt->var_decl.initializer, type, locals)
                    : emit_expr(g, stmt->var_decl.initializer, locals);
            /* user-defined implicit conversion in a typed initializer:
             * `double d = c;` / `Fahrenheit t = 7.25;` (B12). A conversion
             * whose target is rc-managed yields an owned (+1) reference (the
             * conversion method's `return new T(...)`), exactly like an
             * AST_CALL result; hand the store below a dummy `new` node so it
             * moves that reference instead of retaining a second one (mirrors
             * the binding-lowering path). */
            zan_ast_node_t *init_src = stmt->var_decl.initializer;
            bool conv_owned = false;
            if (type) {
                zan_type_t *ity = infer_expr_type(g, init_src, locals);
                if (ity) {
                    conv_owned =
                        (find_user_conversion(g, ity, type, "op_implicit") != NULL) &&
                        is_rc_managed_type(type);
                    init_val = emit_user_conversion(g, ity, type, "op_implicit",
                                                    init_val, init_src, locals);
                }
            }
            if (arc_own) {
                emit_rc_capture_local(g, type, alloca, init_val,
                    conv_owned ? owned_rhs_marker(g, init_src->loc) : init_src,
                    locals);
            } else if (obj_own) {
                emit_obj_local_store(g, &obj_slot, init_val,
                    infer_expr_type(g, stmt->var_decl.initializer, locals),
                    stmt->var_decl.initializer, locals);
            } else {
                init_val = coerce_int_to(g, init_val, llvm_type);
                zan_store_fit(g, init_val, alloca);
            }
        }

        /* An array variable initialized from a List (`string[] p = s.Split(",")`)
         * reads a length header that is not there and indexes a List struct as
         * if it were a buffer. Reject it rather than corrupting memory. */
        if (type && type->kind == TYPE_ARRAY && stmt->var_decl.initializer) {
            zan_type_t *it = infer_expr_type(g, stmt->var_decl.initializer, locals);
            /* An array type carries its element's name ("List" for
             * List<string>[]), so the name test alone rejected the legal
             * `List<string>[] rows = new List<string>[3]`. */
            if (it && it->kind != TYPE_ARRAY && it->name.str &&
                ((type_named(it, "List", 4)) ||
                 (type_named(it, "Dictionary", 10)) ||
                 (type_named(it, "Dict", 4))))
                zan_diag_emit(g->diag, DIAG_ERROR, stmt->loc,
                    "cannot initialize an array from '%.*s': declare the "
                    "variable as '%.*s' instead",
                    (int)it->name.len, it->name.str,
                    (int)it->name.len, it->name.str);
        }

        local_add(locals, stmt->var_decl.name, alloca, type);
        /* `string buf = calloc(...)`: an extern call's string is a raw buffer,
         * so its bounds come from the caller's explicit length, not strlen. */
        if (type && type->kind == TYPE_STRING && stmt->var_decl.initializer &&
            call_targets_extern(g, stmt->var_decl.initializer))
            locals->vars[locals->count - 1].opaque_string = 1;
        if (arc_own) arc_own_local(g, locals);
        if (obj_own)
            locals->vars[locals->count - 1].obj_rc_flag = obj_slot.obj_rc_flag;
        /* A local initialized with a freshly-built dict owns the dict's
         * rc-managed keys/values and releases them at scope exit. */
        if (type &&
            ((type_named(type, "Dict", 4)) ||
             (type_named(type, "Dictionary", 10))) &&
            stmt->var_decl.initializer &&
            stmt->var_decl.initializer->kind == AST_NEW_EXPR)
            arc_own_local(g, locals);
        /* any `new T[n]` initializer: capture the element count so `a.Length`
         * reads it (the raw buffer has no length header; strlen over binary
         * data returned garbage). Size limited to side-effect-free exprs. */
        if (type && type->kind == TYPE_ARRAY &&
            stmt->var_decl.initializer &&
            stmt->var_decl.initializer->kind == AST_NEW_EXPR &&
            stmt->var_decl.initializer->new_expr.is_array &&
            stmt->var_decl.initializer->new_expr.args.count > 0) {
            if (stmt->var_decl.initializer->new_expr.array_init) {
                /* new T[] { ... }: the length is the element count. */
                LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
                LLVMValueRef lslot = emit_entry_alloca(g, i64t, "arr.lenslot3");
                zan_store_fit(g, LLVMConstInt(i64t,
                    (unsigned long long)stmt->var_decl.initializer->new_expr.args.count, 0),
                    lslot);
                locals->vars[locals->count - 1].arr_len_slot = lslot;
                break;
            }
            zan_ast_node_t *sz = stmt->var_decl.initializer->new_expr.args.items[0];
            if (sz->kind == AST_INT_LITERAL || sz->kind == AST_IDENTIFIER) {
                LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
                LLVMValueRef szv = emit_expr(g, sz, locals);
                if (LLVMGetTypeKind(LLVMTypeOf(szv)) == LLVMIntegerTypeKind &&
                    LLVMGetIntTypeWidth(LLVMTypeOf(szv)) < 64)
                    szv = LLVMBuildSExt(g->builder, szv, i64t, "arr.len");
                LLVMValueRef lslot = emit_entry_alloca(g, i64t, "arr.lenslot2");
                zan_store_fit(g, szv, lslot);
                locals->vars[locals->count - 1].arr_len_slot = lslot;
            }
        }
        break;
    }

    case AST_TUPLE_DECON: {
        /* `var (a, b) = rhs;` / `(int a, string b) = rhs;` — evaluate rhs into
         * a temporary tuple slot, then declare each name initialized from its
         * ItemN field. */
        zan_ast_node_t *init = stmt->tuple_decon.initializer;
        zan_type_t *ttype = infer_expr_type(g, init, locals);
        if (!ttype || ttype->kind != TYPE_STRUCT || !ttype->sym) {
            zan_diag_emit(g->diag, DIAG_ERROR, stmt->loc,
                          "right-hand side of a deconstruction is not a tuple");
            break;
        }
        LLVMTypeRef st = get_struct_llvm_type(g, ttype->sym);
        if (!st) st = map_type(g, ttype); /* lazily register synthesized struct */
        LLVMValueRef tmp = NULL;
        if (st) {
            tmp = emit_entry_alloca(g, st, "dtmp");
            zan_store_fit(g, LLVMConstNull(st), tmp);
            if (init->kind == AST_TUPLE_EXPR) {
                /* the initializer is itself a tuple literal: build it in place
                 * so the temp slot is filled once, not copied twice */
                int n = init->tuple_expr.items.count;
                for (int i = 0; i < n; i++) {
                    zan_ast_node_t *item = init->tuple_expr.items.items[i];
                    char fname[16];
                    snprintf(fname, sizeof fname, "Item%d", i + 1);
                    zan_istr_t f_istr = { fname, (uint32_t)strlen(fname) };
                    zan_symbol_t *fsym = get_field_sym(ttype->sym, f_istr);
                    if (!fsym) continue;
                    int fi = get_field_index(ttype->sym, f_istr);
                    if (fi < 0) fi = i;
                    LLVMValueRef fptr = emit_field_ptr(g, ttype->sym, st, tmp,
                                                       fi, "df");
                    LLVMValueRef fval = emit_arg_typed(g, item, fsym->type,
                                                       locals);
                    zan_store_fit(g, fval, fptr);
                }
            } else {
                LLVMValueRef tv = emit_expr(g, init, locals);
                LLVMValueRef tvv =
                    LLVMGetTypeKind(LLVMTypeOf(tv)) == LLVMPointerTypeKind
                        ? LLVMBuildLoad2(g->builder, st, tv, "dtmp.load")
                        : tv;
                zan_store_fit(g, tvv, tmp);
            }
        }
        for (int i = 0; i < stmt->tuple_decon.names.count; i++) {
            zan_ast_node_t *nm = stmt->tuple_decon.names.items[i];
            zan_ast_node_t *ty = stmt->tuple_decon.types.items[i];
            if (!nm || nm->kind != AST_IDENTIFIER) continue;
            zan_type_t *et = ty ? resolve_type_ctx(g, ty) : NULL;
            if (!et) {
                char fname[16];
                snprintf(fname, sizeof fname, "Item%d", i + 1);
                zan_istr_t f_istr = { fname, (uint32_t)strlen(fname) };
                zan_symbol_t *fsym = ttype->sym
                    ? get_field_sym(ttype->sym, f_istr) : NULL;
                et = fsym ? fsym->type : g->binder->type_int;
            }
            LLVMTypeRef elt = map_type(g, et);
            LLVMValueRef slot = emit_entry_alloca(g, elt, "d");
            zan_store_fit(g, LLVMConstNull(elt), slot);
            if (st && tmp) {
                char fname[16];
                snprintf(fname, sizeof fname, "Item%d", i + 1);
                zan_istr_t f_istr = { fname, (uint32_t)strlen(fname) };
                int fi = get_field_index(ttype->sym, f_istr);
                if (fi < 0) fi = i;
                LLVMValueRef fptr = emit_field_ptr(g, ttype->sym, st, tmp,
                                                   fi, "de");
                LLVMValueRef fval = LLVMBuildLoad2(g->builder, elt, fptr,
                                                   "de.load");
                zan_store_fit(g, fval, slot);
            }
            local_add(locals, nm->ident.name, slot, et);
        }
        break;
    }

    case AST_EXPR_STMT: {
        zan_ast_node_t *e = stmt->expr_stmt.expr;
        LLVMValueRef ev = emit_expr(g, e, locals);
        /* `Foo();` where Foo is async: the ramp allocated a heap frame and
         * handed back its task handle, and the statement drops it. Nothing
         * else ever schedules that frame, so the body never ran and the frame
         * never reached a free -- the call was a silent no-op leaking one
         * frame per execution. Detach it exactly as `Task.Spawn(Foo())` does:
         * the body runs and the reaper frees the frame at completion. */
        if (ev && e->kind == AST_CALL && emit_detach_async_call(g, ev, false))
            break;
        /* A discarded expression statement whose value is a freshly owned (+1)
         * rc reference must release it, or it leaks. This covers fluent method
         * chains used as statements (e.g. `builder.Add(x);` where Add returns
         * `this`) and bare `new`/call results. Assignments and non-call
         * expressions are not owned (expr_yields_owned_rc_value == 0).
         *
         * A bare `await E;` result arrives as an i64 (async frames store every
         * result in an i64 slot); coerce it back to a pointer using the
         * inferred rc type before releasing, or a discarded owned awaited
         * string/object leaks once per await. */
        if (ev && expr_yields_owned_rc_value(g, e, locals)) {
            zan_type_t *et = infer_expr_type(g, e, locals);
            if (et && is_rc_managed_type(et)) {
                LLVMTypeKind evk = LLVMGetTypeKind(LLVMTypeOf(ev));
                if (evk == LLVMPointerTypeKind) {
                    emit_rc_release_for_type(g, et, ev);
                } else if (evk == LLVMIntegerTypeKind &&
                           e->kind == AST_AWAIT_EXPR) {
                    LLVMValueRef p = emit_boundary_coerce(g, ev,
                        LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0));
                    emit_rc_release_for_type(g, et, p);
                }
            }
        }
        break;
    }

    case AST_RETURN_STMT:
        if (stmt->ret.value) {
            check_implicit_narrowing(g, g->current_fn_zan_ret_type,
                infer_expr_type(g, stmt->ret.value, locals),
                stmt->ret.value, "return");
        }
        /* Inside an async $resume body, `return e` completes the state machine:
         * store the result into the frame, mark it done, and `ret void` — the
         * frame pointer (Task) was already handed out by the ramp. (The awaiter
         * wake is added in S3 with the await protocol.) */
        if (g->current_async_frame) {
            LLVMValueRef ri = NULL;
            if (stmt->ret.value) {
                LLVMValueRef rv = emit_expr(g, stmt->ret.value, locals);
                zan_type_t *ret_type = concretize(g,
                    infer_expr_type(g, stmt->ret.value, locals));
                if (is_rc_managed_type(ret_type) &&
                    !expr_yields_owned_rc_value(g, stmt->ret.value, locals)) {
                    emit_rc_retain_for_type(g, ret_type, rv);
                }
                /* encode against the *declared* return type, not the type of
                 * this particular expression: the awaiter decodes with the
                 * declared one, so `return 0;` from an async `double` method
                 * has to reach the slot as a double */
                ri = coerce_to_frame_result(g, coerce_async_ret(g, rv),
                                            g->current_async_ret_type
                                                ? g->current_async_ret_type
                                                : ret_type);
            }
            /* C#: the return value is evaluated first, then every enclosing
             * finally runs, then the function returns. Spill the value across
             * them -- a finally body may `await`, which ends this invocation
             * and leaves the SSA value behind. */
            if (g->finally_count > 0) {
                LLVMValueRef ri_slot = ri
                    ? emit_entry_alloca(g, LLVMTypeOf(ri), "ret.fin.slot") : NULL;
                if (ri_slot) zan_store_fit(g, ri, ri_slot);
                emit_pending_finallys(g, locals, 0);
                if (LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder)))
                    break;   /* a finally left the function itself */
                if (ri_slot)
                    ri = LLVMBuildLoad2(g->builder, LLVMTypeOf(ri), ri_slot,
                                        "ret.fin");
            }
            emit_release_active_catch_excs(g, 0);
            emit_async_complete(g, locals, ri);
            break;
        }
        if (stmt->ret.value) {
            LLVMValueRef val = emit_expr(g, stmt->ret.value, locals);
            /* user-defined implicit conversion: `return c;` from a method
             * declared to return double where the source type declares
             * `implicit operator double` (B12). */
            bool conv_ret = false;
            if (g->current_fn_zan_ret_type) {
                zan_type_t *vty = infer_expr_type(g, stmt->ret.value, locals);
                if (find_user_conversion(g, vty, g->current_fn_zan_ret_type,
                                         "op_implicit")) {
                    val = emit_user_conversion(g, vty, g->current_fn_zan_ret_type,
                                               "op_implicit", val,
                                               stmt->ret.value, locals);
                    conv_ret = true;
                }
            }
            /* ARC: hand the caller an owned (+1) reference, then release our
             * owning locals. Retaining a borrowed return value first keeps it
             * alive when it aliases a local about to be released. A converted
             * value is already owned (+1) like a method-call result, so it is
             * moved rather than retained. */
            zan_type_t *ret_type = concretize(g,
                infer_expr_type(g, stmt->ret.value, locals));
            if (conv_ret) ret_type = g->current_fn_zan_ret_type;
            if (is_rc_managed_type(ret_type) &&
                !expr_yields_owned_rc_value(g, stmt->ret.value, locals) &&
                !conv_ret) {
                emit_rc_retain_for_type(g, ret_type, val);
            }
            if (g->finally_count > 0) {
                LLVMValueRef v_slot =
                    emit_entry_alloca(g, LLVMTypeOf(val), "ret.fin.slot");
                zan_store_fit(g, val, v_slot);
                emit_pending_finallys(g, locals, 0);
                if (LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder)))
                    break;
                val = LLVMBuildLoad2(g->builder, LLVMTypeOf(val), v_slot,
                                     "ret.fin");
            }
            /* `return a;` needs no exclusion from the exit release pass: an
             * array is retained above like any other rc-managed return, so the
             * release of the local it aliases leaves the caller's +1. */
            emit_release_owned_locals(g, locals);
            emit_release_active_catch_excs(g, 0);
            /* convert return value to match function return type */
            LLVMTypeRef fn_ret = g->current_fn_ret_type;
            LLVMTypeRef val_t = LLVMTypeOf(val);
            if (val_t != fn_ret) {
                if (llvm_is_nullable(fn_ret)) {
                    /* `return 5;` / `return null;` from a `T?` method. */
                    val = coerce_int_to(g, val, fn_ret);
                } else if (LLVMGetTypeKind(fn_ret) == LLVMFloatTypeKind &&
                    LLVMGetTypeKind(val_t) == LLVMDoubleTypeKind) {
                    val = LLVMBuildFPTrunc(g->builder, val, fn_ret, "rettrunc");
                } else if (LLVMGetTypeKind(fn_ret) == LLVMDoubleTypeKind &&
                           LLVMGetTypeKind(val_t) == LLVMFloatTypeKind) {
                    val = LLVMBuildFPExt(g->builder, val, fn_ret, "retext");
                } else if ((LLVMGetTypeKind(fn_ret) == LLVMDoubleTypeKind ||
                            LLVMGetTypeKind(fn_ret) == LLVMFloatTypeKind) &&
                           LLVMGetTypeKind(val_t) == LLVMIntegerTypeKind &&
                           LLVMGetIntTypeWidth(val_t) > 1) {
                    /* `return 2;` from a `double` method converts, as in C#;
                     * returning the integer bit pattern failed verification. */
                    val = LLVMBuildSIToFP(g->builder, val, fn_ret, "retsitofp");
                } else if (LLVMGetTypeKind(fn_ret) == LLVMIntegerTypeKind &&
                           LLVMGetTypeKind(val_t) == LLVMIntegerTypeKind) {
                    unsigned fn_bits = LLVMGetIntTypeWidth(fn_ret);
                    unsigned val_bits = LLVMGetIntTypeWidth(val_t);
                    if (fn_bits > val_bits) {
                        /* Of the narrow widths only i1 (`bool`) and i8 (`byte`)
                         * occur and both are unsigned, so `return someByte;`
                         * from an `int` method must zero-extend -- sign-extending
                         * made `return buf[i];` read 0xE4 back as -28. */
                        val = val_bits <= 8
                            ? LLVMBuildZExt(g->builder, val, fn_ret, "retzext")
                            : LLVMBuildSExt(g->builder, val, fn_ret, "retext");
                    } else if (fn_bits < val_bits) {
                        val = LLVMBuildTrunc(g->builder, val, fn_ret, "rettrunc");
                    }
                } else if (LLVMGetTypeKind(fn_ret) == LLVMStructTypeKind &&
                           LLVMGetTypeKind(val_t) == LLVMPointerTypeKind) {
                    /* A value struct returns by value: `new Pair(3,4)` is a
                     * stack slot behind a pointer, but the function signature
                     * returns the aggregate. Loading it here makes
                     * `return new Pair(...)` verifiable (it used to `ret ptr`
                     * into a `%struct.Pair` function and fail verification).
                     * Must precede the pointer-coercion branches below, since
                     * the stack slot is itself a pointer. */
                    val = LLVMBuildLoad2(g->builder, fn_ret, val, "ret.struct");
                } else if (LLVMGetTypeKind(fn_ret) == LLVMPointerTypeKind &&
                           LLVMGetTypeKind(val_t) == LLVMPointerTypeKind) {
                    /* e.g. `return null` (i8*) from a method returning a
                     * concrete class pointer, or vice versa */
                    val = LLVMBuildBitCast(g->builder, val, fn_ret, "retcast");
                } else if (LLVMGetTypeKind(fn_ret) == LLVMPointerTypeKind ||
                           LLVMGetTypeKind(val_t) == LLVMPointerTypeKind) {
                    /* A specialized generic body computes a T-typed value in
                     * its concrete form (i32 for Queue<int>) while the
                     * signature keeps the erased pointer form; the caller
                     * coerces back. */
                    val = emit_boundary_coerce(g, val, fn_ret);
                }
            }
            /* `return 0;` in Main ends the program: release the static fields
             * too, exactly like falling off the end of Main does, so a
             * singleton held in a static field is not reported as a leak. */
            if (g->current_fn_is_main) emit_release_static_rc_fields(g, NULL);
            emit_eh_disarm_from(g, 0);
            LLVMBuildRet(g->builder, val);
        } else {
            emit_pending_finallys(g, locals, 0);
            if (LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder)))
                break;
            emit_release_owned_locals(g, locals);
            emit_release_active_catch_excs(g, 0);
            /* A bare `return;` normally maps to `ret void`. The program entry
             * `Main` is lowered to an LLVM `i32 main`, though, so a bare return
             * there must yield an exit code to match the function's return type
             * (mirrors the implicit end-of-main `ret i32 0`). */
            LLVMTypeRef fn_ret = g->current_fn_ret_type;
            if (g->current_fn_is_main) emit_release_static_rc_fields(g, NULL);
            emit_eh_disarm_from(g, 0);
            if (fn_ret && LLVMGetTypeKind(fn_ret) != LLVMVoidTypeKind) {
                LLVMBuildRet(g->builder, LLVMConstNull(fn_ret));
            } else {
                LLVMBuildRetVoid(g->builder);
            }
        }
        break;

    case AST_IF_STMT: {
        int then_start = locals->count;
        LLVMValueRef cond = emit_expr(g, stmt->if_stmt.cond, locals);
        /* ensure cond is i1 */
        cond = zan_tobool(g->builder, cond, "tobool");

        LLVMBasicBlockRef then_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "then");
        LLVMBasicBlockRef else_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "else");
        LLVMBasicBlockRef merge_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "merge");

        LLVMBuildCondBr(g->builder, cond, then_bb, stmt->if_stmt.else_body ? else_bb : merge_bb);

        LLVMPositionBuilderAtEnd(g->builder, then_bb);
        emit_stmt(g, stmt->if_stmt.then_body, locals);
        if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder))) {
            emit_release_owned_locals_from(g, locals, then_start);
            LLVMBuildBr(g->builder, merge_bb);
        } else {
            emit_release_owned_locals_from(g, locals, then_start);
        }

        int else_start = locals->count;
        LLVMPositionBuilderAtEnd(g->builder, else_bb);
        if (stmt->if_stmt.else_body) {
            emit_stmt(g, stmt->if_stmt.else_body, locals);
        }
        if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder))) {
            emit_release_owned_locals_from(g, locals, else_start);
            LLVMBuildBr(g->builder, merge_bb);
        } else {
            emit_release_owned_locals_from(g, locals, else_start);
        }

        LLVMPositionBuilderAtEnd(g->builder, merge_bb);
        break;
    }

    case AST_WHILE_STMT: {
        int body_start = locals->count;
        LLVMBasicBlockRef cond_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "while.cond");
        LLVMBasicBlockRef body_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "while.body");
        LLVMBasicBlockRef end_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "while.end");

        LLVMBasicBlockRef saved_break = g->break_target;
        LLVMBasicBlockRef saved_cont = g->continue_target;
        int saved_loop_base = g->loop_locals_base;
        int saved_loop_cbase = g->loop_catch_base;
        int saved_loop_fbase = g->finally_loop_base;
        g->break_target = end_bb;
        g->continue_target = cond_bb;
        g->loop_locals_base = body_start;
        g->loop_catch_base = g->catch_cleanup_count;
        g->finally_loop_base = g->finally_count;
        int saved_loop_ehbase = g->eh_armed_loop_base;
        g->eh_armed_loop_base = g->eh_armed_count;

        LLVMBuildBr(g->builder, cond_bb);
        LLVMPositionBuilderAtEnd(g->builder, cond_bb);
        LLVMValueRef cond = emit_expr(g, stmt->while_stmt.cond, locals);
        cond = zan_tobool(g->builder, cond, "tobool");
        LLVMBuildCondBr(g->builder, cond, body_bb, end_bb);

        LLVMPositionBuilderAtEnd(g->builder, body_bb);
        emit_stmt(g, stmt->while_stmt.body, locals);
        if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder))) {
            emit_release_owned_locals_from(g, locals, body_start);
            LLVMBuildBr(g->builder, cond_bb);
        } else {
            emit_release_owned_locals_from(g, locals, body_start);
        }

        g->break_target = saved_break;
        g->continue_target = saved_cont;
        g->loop_locals_base = saved_loop_base;
        g->loop_catch_base = saved_loop_cbase;
        g->finally_loop_base = saved_loop_fbase;
        g->eh_armed_loop_base = saved_loop_ehbase;

        LLVMPositionBuilderAtEnd(g->builder, end_bb);
        break;
    }

    case AST_FOR_STMT: {
        int for_start = locals->count;
        if (stmt->for_stmt.init) emit_stmt(g, stmt->for_stmt.init, locals);
        /* Loop variables declared in the init clause live in [for_start,
         * for_body_start) and must survive across iterations (the step and
         * condition read them). Only body-scope locals [for_body_start, ..)
         * are released at the end of each iteration; the loop variables are
         * dropped once, at loop exit. Releasing from for_start each iteration
         * would truncate the loop var out of scope, so the step `i = i + 1`
         * and condition `i < n` would operate on a dropped slot -> infinite
         * loop. */
        int for_body_start = locals->count;

        LLVMBasicBlockRef cond_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "for.cond");
        LLVMBasicBlockRef body_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "for.body");
        LLVMBasicBlockRef step_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "for.step");
        LLVMBasicBlockRef end_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "for.end");

        LLVMBasicBlockRef saved_break = g->break_target;
        LLVMBasicBlockRef saved_cont = g->continue_target;
        int saved_loop_base = g->loop_locals_base;
        int saved_loop_cbase = g->loop_catch_base;
        int saved_loop_fbase = g->finally_loop_base;
        g->break_target = end_bb;
        g->continue_target = step_bb;
        g->loop_locals_base = for_body_start;
        g->loop_catch_base = g->catch_cleanup_count;
        g->finally_loop_base = g->finally_count;
        int saved_loop_ehbase = g->eh_armed_loop_base;
        g->eh_armed_loop_base = g->eh_armed_count;

        LLVMBuildBr(g->builder, cond_bb);
        LLVMPositionBuilderAtEnd(g->builder, cond_bb);
        if (stmt->for_stmt.cond) {
            LLVMValueRef cond = emit_expr(g, stmt->for_stmt.cond, locals);
            cond = zan_tobool(g->builder, cond, "tobool");
            LLVMBuildCondBr(g->builder, cond, body_bb, end_bb);
        } else {
            LLVMBuildBr(g->builder, body_bb);
        }

        LLVMPositionBuilderAtEnd(g->builder, body_bb);
        emit_stmt(g, stmt->for_stmt.body, locals);
        if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder))) {
            emit_release_owned_locals_from(g, locals, for_body_start);
            LLVMBuildBr(g->builder, step_bb);
        } else {
            emit_release_owned_locals_from(g, locals, for_body_start);
        }

        LLVMPositionBuilderAtEnd(g->builder, step_bb);
        if (stmt->for_stmt.step) emit_expr(g, stmt->for_stmt.step, locals);
        LLVMBuildBr(g->builder, cond_bb);

        g->break_target = saved_break;
        g->continue_target = saved_cont;
        g->loop_locals_base = saved_loop_base;
        g->loop_catch_base = saved_loop_cbase;
        g->finally_loop_base = saved_loop_fbase;
        g->eh_armed_loop_base = saved_loop_ehbase;

        LLVMPositionBuilderAtEnd(g->builder, end_bb);
        /* Drop the loop variables (and any owned init-clause locals) now that
         * the loop has fully exited. */
        emit_release_owned_locals_from(g, locals, for_start);
        break;
    }

    case AST_BREAK_STMT:
        if (g->break_target) {
            /* leaving every try entered inside this loop runs their finallys */
            emit_pending_finallys(g, locals, g->finally_loop_base);
            if (LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder)))
                break;
            emit_release_owned_locals_range(g, locals, g->loop_locals_base);
            emit_release_active_catch_excs(g, g->loop_catch_base);
            emit_eh_disarm_from(g, g->eh_armed_loop_base);
            LLVMBuildBr(g->builder, g->break_target);
        }
        break;

    case AST_CONTINUE_STMT:
        if (g->continue_target) {
            emit_pending_finallys(g, locals, g->finally_loop_base);
            if (LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder)))
                break;
            emit_release_owned_locals_range(g, locals, g->loop_locals_base);
            emit_release_active_catch_excs(g, g->loop_catch_base);
            emit_eh_disarm_from(g, g->eh_armed_loop_base);
            LLVMBuildBr(g->builder, g->continue_target);
        }
        break;

    case AST_SWITCH_STMT: {
        int switch_start = locals->count;
        LLVMValueRef switch_val = emit_expr(g, stmt->switch_stmt.expr, locals);
        LLVMBasicBlockRef end_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "sw.end");

        /* count non-default cases. A pure type-pattern case (`case Dog d:`)
         * carries no `pattern` node -- it must not be mistaken for the
         * default arm, or its body would run unconditionally as the
         * fallthrough target. */
        int num_cases = 0;
        zan_ast_node_t *default_case = NULL;
        for (int i = 0; i < stmt->switch_stmt.cases.count; i++) {
            zan_ast_node_t *sc = stmt->switch_stmt.cases.items[i];
            if (sc->switch_case.pattern || sc->switch_case.type_pattern ||
                sc->switch_case.when_cond || sc->switch_case.var_name.len > 0) {
                num_cases++;
            } else {
                default_case = sc;
            }
        }

        LLVMBasicBlockRef default_bb = default_case
            ? LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "sw.default")
            : end_bb;

        /* `break` in a case leaves the switch, not an enclosing loop
         * (`continue` keeps targeting the loop, so it is left alone) */
        LLVMBasicBlockRef sw_saved_break = g->break_target;
        g->break_target = end_bb;
        /* ARC: a `break` out of a case body releases owning locals via
         * g->loop_locals_base (see AST_BREAK_STMT). Point that base at the
         * switch discriminant's slot, so the break releases only locals
         * declared inside the switch -- not the discriminant itself, which
         * stays owned until the enclosing scope exits. Without this the
         * release range started at 0 and freed the switch value, so `case
         * T x:` followed by `x is T` read a dangling pointer. */
        int sw_saved_loop_base = g->loop_locals_base;
        g->loop_locals_base = switch_start;

        /* B5: pattern cases — `case T x:`, `case null:`, `case ... when g:`.
         * A type match is a runtime is-check, a null match a pointer compare,
         * and a guard a side condition, so none of them can ride
         * LLVMBuildSwitch; any switch containing one is lowered to a chain of
         * per-case comparisons instead. */
        bool has_patterns = false;
        for (int i = 0; i < stmt->switch_stmt.cases.count && !has_patterns; i++) {
            zan_ast_node_t *sc = stmt->switch_stmt.cases.items[i];
            if (sc->switch_case.type_pattern || sc->switch_case.when_cond ||
                sc->switch_case.var_name.len > 0 ||
                (sc->switch_case.pattern &&
                 sc->switch_case.pattern->kind == AST_NULL_LITERAL))
                has_patterns = true;
        }
        if (has_patterns) {
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
            LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
            bool sw_string = is_string_expr(g, stmt->switch_stmt.expr, locals);
            LLVMValueRef strcmp_fn = NULL;
            LLVMTypeRef strcmp_ty = NULL;
            if (sw_string) {
                strcmp_fn = LLVMGetNamedFunction(g->mod, "strcmp");
                strcmp_ty = LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0);
                if (!strcmp_fn) strcmp_fn = LLVMAddFunction(g->mod, "strcmp", strcmp_ty);
            }

            int nc = stmt->switch_stmt.cases.count;
            LLVMBasicBlockRef *pmatch = (LLVMBasicBlockRef *)calloc(
                (size_t)(nc > 0 ? nc : 1), sizeof(LLVMBasicBlockRef));
            int *psrc = (int *)calloc((size_t)(nc > 0 ? nc : 1), sizeof(int));
            int ci = 0;

            LLVMBasicBlockRef test_bb = LLVMAppendBasicBlockInContext(
                g->ctx, g->current_fn, "sw.ptest");
            LLVMBuildBr(g->builder, test_bb);

            for (int i = 0; i < nc; i++) {
                zan_ast_node_t *sc = stmt->switch_stmt.cases.items[i];
                if (!sc->switch_case.pattern && !sc->switch_case.type_pattern)
                    continue; /* default handled after the chain */
                LLVMPositionBuilderAtEnd(g->builder, test_bb);
                LLVMBasicBlockRef match_bb = LLVMAppendBasicBlockInContext(
                    g->ctx, g->current_fn, "sw.pmatch");
                /* always a fresh fail block: it either hosts the next case's
                 * test or is wired to default/end after the chain. */
                LLVMBasicBlockRef fail_bb = LLVMAppendBasicBlockInContext(
                    g->ctx, g->current_fn, "sw.pnext");

                /* pattern variable: bind before the guard so `when` can read
                 * it. The case body rebinds it separately (see below) because
                 * each body's scope-exit release truncates locals back to
                 * switch_start, wiping every chain-time binding. */
                emit_switch_pattern_bind(g, locals, sc, switch_val);

                /* the match condition */
                LLVMValueRef cond;
                if (sc->switch_case.type_pattern) {
                    zan_type_t *pt = resolve_type_ctx(g, sc->switch_case.type_pattern);
                    if (pt && (pt->kind == TYPE_CLASS || pt->kind == TYPE_STRING ||
                               pt->kind == TYPE_OBJECT || pt->kind == TYPE_INTERFACE))
                        cond = emit_runtime_is_check_name(g, switch_val, pt);
                    else {
                        /* A value type has no runtime type variation, so the
                         * match is decided statically -- the same rule
                         * AST_IS_EXPR uses: true iff the discriminant's static
                         * type is the pattern type. It was hardcoded false, so
                         * `case int n when n > 3:` (and the switch-expression
                         * arm that lowers to it) never matched and the switch
                         * silently fell through to default. */
                        zan_type_t *dt = infer_expr_type(g, stmt->switch_stmt.expr,
                                                         locals);
                        cond = LLVMConstInt(LLVMInt1TypeInContext(g->ctx),
                            (pt && dt && types_equal(dt, pt)) ? 1 : 0, 0);
                    }
                } else if (sc->switch_case.pattern->kind == AST_NULL_LITERAL) {
                    cond = zan_icmp(g->builder, LLVMIntEQ, switch_val,
                        LLVMConstNull(LLVMTypeOf(switch_val)), "sw.null");
                } else if (sw_string) {
                    LLVMValueRef case_val = emit_expr(g, sc->switch_case.pattern, locals);
                    LLVMValueRef cmp = zan_call2(g->builder, strcmp_ty, strcmp_fn,
                        (LLVMValueRef[]){ switch_val, case_val }, 2, "swcmp");
                    cond = zan_icmp(g->builder, LLVMIntEQ, cmp,
                        LLVMConstInt(i32, 0, 0), "sweq");
                } else {
                    LLVMValueRef case_val = emit_expr(g, sc->switch_case.pattern, locals);
                    cond = zan_icmp(g->builder, LLVMIntEQ, switch_val, case_val,
                                    "sw.eq");
                }
                /* `when` guard: evaluated only after the pattern matched.
                 * The guard runs in its own block so a failed match (null
                 * discriminant, wrong runtime type) never evaluates it --
                 * the pattern variable is only safe to read after the match
                 * held. A plain `and` would evaluate it unconditionally and
                 * crash on `case Dog d when d.Legs > 3:` for a null value. */
                if (sc->switch_case.when_cond) {
                    LLVMBasicBlockRef guard_bb = LLVMAppendBasicBlockInContext(
                        g->ctx, g->current_fn, "sw.pguard");
                    LLVMBuildCondBr(g->builder, cond, guard_bb, fail_bb);
                    LLVMPositionBuilderAtEnd(g->builder, guard_bb);
                    LLVMValueRef gv = emit_expr(g, sc->switch_case.when_cond, locals);
                    if (LLVMGetTypeKind(LLVMTypeOf(gv)) != LLVMIntegerTypeKind ||
                        LLVMGetIntTypeWidth(LLVMTypeOf(gv)) != 1)
                        gv = LLVMBuildICmp(g->builder, LLVMIntNE, gv,
                            LLVMConstInt(LLVMTypeOf(gv), 0, 0), "sw.guard");
                    LLVMBuildCondBr(g->builder, gv, match_bb, fail_bb);
                } else {
                    LLVMBuildCondBr(g->builder, cond, match_bb, fail_bb);
                }
                pmatch[ci] = match_bb;
                psrc[ci] = i;
                ci++;
                test_bb = fail_bb;
            }

            /* the last test's failure lands in default / end */
            LLVMPositionBuilderAtEnd(g->builder, test_bb);
            if (default_case)
                LLVMBuildBr(g->builder, default_bb);
            else
                LLVMBuildBr(g->builder, end_bb);

            /* case bodies */
            for (int k = 0; k < ci; k++) {
                int i = psrc[k];
                zan_ast_node_t *sc = stmt->switch_stmt.cases.items[i];
                LLVMPositionBuilderAtEnd(g->builder, pmatch[k]);
                bool case_empty = (sc->switch_case.body &&
                                   sc->switch_case.body->block.stmts.count == 0);
                if (case_empty) {
                    LLVMBasicBlockRef fall = NULL;
                    for (int k2 = k + 1; k2 < ci; k2++) {
                        zan_ast_node_t *nxt = stmt->switch_stmt.cases.items[psrc[k2]];
                        if (nxt->switch_case.body &&
                            nxt->switch_case.body->block.stmts.count > 0) {
                            fall = pmatch[k2];
                            break;
                        }
                    }
                    if (!fall) fall = default_bb;
                    LLVMBuildBr(g->builder, fall);
                    continue;
                }
                /* rebind the pattern variable in this body's scope: previous
                 * bodies' scope-exit release truncated locals past the chain
                 * time binding, so the body must see its own copy */
                emit_switch_pattern_bind(g, locals, sc, switch_val);
                emit_stmt(g, sc->switch_case.body, locals);
                if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder))) {
                    emit_release_owned_locals_from(g, locals, switch_start);
                    LLVMBuildBr(g->builder, end_bb);
                } else {
                    emit_release_owned_locals_from(g, locals, switch_start);
                }
            }
            free(pmatch);
            free(psrc);

            if (default_case) {
                LLVMPositionBuilderAtEnd(g->builder, default_bb);
                emit_stmt(g, default_case->switch_case.body, locals);
                if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder))) {
                    emit_release_owned_locals_from(g, locals, switch_start);
                    LLVMBuildBr(g->builder, end_bb);
                } else {
                    emit_release_owned_locals_from(g, locals, switch_start);
                }
            }

            g->break_target = sw_saved_break;
            g->loop_locals_base = sw_saved_loop_base;
            LLVMPositionBuilderAtEnd(g->builder, end_bb);
            break;
        }

        if (is_string_expr(g, stmt->switch_stmt.expr, locals)) {
            /* string switch: strcmp chain (LLVMBuildSwitch requires integers) */
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
            LLVMValueRef strcmp_fn = LLVMGetNamedFunction(g->mod, "strcmp");
            LLVMTypeRef strcmp_ty = LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0);
            if (!strcmp_fn) strcmp_fn = LLVMAddFunction(g->mod, "strcmp", strcmp_ty);

            LLVMBasicBlockRef *case_bbs = (LLVMBasicBlockRef *)calloc(
                (size_t)(num_cases > 0 ? num_cases : 1), sizeof(LLVMBasicBlockRef));
            int *case_src = (int *)calloc((size_t)(num_cases > 0 ? num_cases : 1),
                                          sizeof(int));
            int ci = 0;
            for (int i = 0; i < stmt->switch_stmt.cases.count; i++) {
                zan_ast_node_t *sc = stmt->switch_stmt.cases.items[i];
                if (!sc->switch_case.pattern) continue;
                LLVMValueRef case_val = emit_expr(g, sc->switch_case.pattern, locals);
                LLVMBasicBlockRef case_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "sw.case");
                LLVMBasicBlockRef next_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "sw.next");
                LLVMValueRef cmp = zan_call2(g->builder, strcmp_ty, strcmp_fn,
                    (LLVMValueRef[]){ switch_val, case_val }, 2, "swcmp");
                LLVMValueRef eq = zan_icmp(g->builder, LLVMIntEQ, cmp,
                    LLVMConstInt(i32, 0, 0), "sweq");
                LLVMBuildCondBr(g->builder, eq, case_bb, next_bb);
                LLVMPositionBuilderAtEnd(g->builder, next_bb);
                case_bbs[ci] = case_bb;
                case_src[ci] = i;
                ci++;
            }
            LLVMBuildBr(g->builder, default_bb);

            for (int k = 0; k < ci; k++) {
                int i = case_src[k];
                zan_ast_node_t *sc = stmt->switch_stmt.cases.items[i];
                LLVMPositionBuilderAtEnd(g->builder, case_bbs[k]);
                /* an empty case falls through into the next non-empty case
                 * (or default), mirroring the integer-switch fix */
                bool case_empty = (sc->switch_case.body &&
                                   sc->switch_case.body->block.stmts.count == 0);
                if (case_empty) {
                    LLVMBasicBlockRef fall_bb = NULL;
                    for (int k2 = k + 1; k2 < ci; k2++) {
                        zan_ast_node_t *nxt =
                            stmt->switch_stmt.cases.items[case_src[k2]];
                        if (nxt->switch_case.body &&
                            nxt->switch_case.body->block.stmts.count > 0) {
                            fall_bb = case_bbs[k2];
                            break;
                        }
                    }
                    if (!fall_bb) fall_bb = default_bb;
                    LLVMBuildBr(g->builder, fall_bb);
                    continue;
                }
                emit_stmt(g, sc->switch_case.body, locals);
                if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder))) {
                    emit_release_owned_locals_from(g, locals, switch_start);
                    LLVMBuildBr(g->builder, end_bb);
                } else {
                    emit_release_owned_locals_from(g, locals, switch_start);
                }
            }
            free(case_bbs);
            free(case_src);

            if (default_case) {
                LLVMPositionBuilderAtEnd(g->builder, default_bb);
                emit_stmt(g, default_case->switch_case.body, locals);
                if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder))) {
                    emit_release_owned_locals_from(g, locals, switch_start);
                    LLVMBuildBr(g->builder, end_bb);
                } else {
                    emit_release_owned_locals_from(g, locals, switch_start);
                }
            }

            g->break_target = sw_saved_break;
            g->loop_locals_base = sw_saved_loop_base;
            LLVMPositionBuilderAtEnd(g->builder, end_bb);
            break;
        }

        LLVMValueRef sw = LLVMBuildSwitch(g->builder, switch_val, default_bb, (unsigned)num_cases);

        /* C# case fallthrough: `case 1: case 2: body` lets both labels share
         * one body -- an empty case falls through into the next case (or, at
         * the tail, into default). Each case gets its own block and empty
         * cases are wired to the next non-empty one. */
        LLVMBasicBlockRef *case_bbs = (LLVMBasicBlockRef *)calloc(
            (size_t)(num_cases > 0 ? num_cases : 1), sizeof(LLVMBasicBlockRef));
        /* case_src[i] = index in stmt->cases of the i-th non-default case;
         * lets an empty case find the block of the next non-empty case */
        int *case_src = (int *)calloc((size_t)(num_cases > 0 ? num_cases : 1),
                                      sizeof(int));
        int nci = 0;
        /* pass 1: create a block for every case, add it to the switch */
        for (int i = 0; i < stmt->switch_stmt.cases.count; i++) {
            zan_ast_node_t *sc = stmt->switch_stmt.cases.items[i];
            if (!sc->switch_case.pattern) continue; /* default handled separately */

            LLVMBasicBlockRef case_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "sw.case");
            LLVMBasicBlockRef entry_bb = LLVMGetInsertBlock(g->builder);
            LLVMValueRef case_val = emit_expr(g, sc->switch_case.pattern, locals);
            /* LLVMBuildSwitch / LLVMConstIntGetSExtValue require a ConstantInt;
             * an arbitrary expression here would type-confuse inside LLVM's
             * C++ unwrap (UB / garbage constants). Diagnose and route this
             * case straight to the switch end instead. */
            if (!case_val || !LLVMIsAConstantInt(case_val)) {
                zan_diag_emit(g->diag, DIAG_ERROR, sc->loc,
                              "case label must be a compile-time constant "
                              "in an integer switch");
                LLVMPositionBuilderAtEnd(g->builder, case_bb);
                emit_release_owned_locals_from(g, locals, switch_start);
                LLVMBuildBr(g->builder, end_bb);
                LLVMPositionBuilderAtEnd(g->builder, entry_bb);
                continue;
            }
            /* A case label is an integer constant; fit it to the switch
             * operand's width with a *constant* cast. Widening the operand
             * instead (coerce_int_pair) would build a sext into this block,
             * which LLVMBuildSwitch has already terminated, leaving the
             * terminator no longer last -- invalid IR whenever the literal's
             * width (i64) differs from the operand's (e.g. i32 `int`). */
            LLVMTypeRef swty = LLVMTypeOf(switch_val);
            if (LLVMGetTypeKind(LLVMTypeOf(case_val)) == LLVMIntegerTypeKind &&
                LLVMGetTypeKind(swty) == LLVMIntegerTypeKind &&
                LLVMTypeOf(case_val) != swty) {
                long long cv = LLVMConstIntGetSExtValue(case_val);
                case_val = LLVMConstInt(swty, (unsigned long long)cv, 1);
            }
            LLVMAddCase(sw, case_val, case_bb);
            case_bbs[nci] = case_bb;
            case_src[nci] = i;
            nci++;
        }

        /* pass 2: emit each case body; an empty case (no statements) is a
         * fallthrough label and just branches into the next case's block */
        for (int k = 0; k < nci; k++) {
            int i = case_src[k];
            zan_ast_node_t *sc = stmt->switch_stmt.cases.items[i];
            LLVMBasicBlockRef case_bb = case_bbs[k];
            bool case_empty = (sc->switch_case.body &&
                               sc->switch_case.body->block.stmts.count == 0);
            /* the next label this case falls into: the next case with a
             * non-empty body, else default (or the switch end) */
            LLVMBasicBlockRef fall_bb = NULL;
            if (case_empty) {
                for (int k2 = k + 1; k2 < nci; k2++) {
                    zan_ast_node_t *nxt =
                        stmt->switch_stmt.cases.items[case_src[k2]];
                    if (nxt->switch_case.body &&
                        nxt->switch_case.body->block.stmts.count > 0) {
                        fall_bb = case_bbs[k2];
                        break;
                    }
                }
                if (!fall_bb) fall_bb = default_bb;
            }

            LLVMPositionBuilderAtEnd(g->builder, case_bb);
            if (case_empty) {
                LLVMBuildBr(g->builder, fall_bb);
                continue;
            }
            emit_stmt(g, sc->switch_case.body, locals);
            if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder))) {
                emit_release_owned_locals_from(g, locals, switch_start);
                LLVMBuildBr(g->builder, end_bb);
            } else {
                emit_release_owned_locals_from(g, locals, switch_start);
            }
        }
        free(case_bbs);
        free(case_src);

        /* emit default block */
        if (default_case) {
            LLVMPositionBuilderAtEnd(g->builder, default_bb);
            emit_stmt(g, default_case->switch_case.body, locals);
            if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder))) {
                emit_release_owned_locals_from(g, locals, switch_start);
                LLVMBuildBr(g->builder, end_bb);
            } else {
                emit_release_owned_locals_from(g, locals, switch_start);
            }
        }

        g->break_target = sw_saved_break;
        g->loop_locals_base = sw_saved_loop_base;
        LLVMPositionBuilderAtEnd(g->builder, end_bb);
        break;
    }

    case AST_TRY_STMT: {
        /* try/catch/finally via a per-program setjmp stack: entering a try
         * pushes a jmp_buf, `throw` longjmps to the innermost one with the
         * exception object in a global, catch pops the stack and binds the
         * exception local. */
        LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
        LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
        LLVMValueRef top_g, bufs_g, exc_g;
        get_eh_globals(g, &top_g, &bufs_g, &exc_g);
        LLVMValueRef fn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder));
        LLVMBasicBlockRef try_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "try.body");
        LLVMBasicBlockRef catch_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "try.catch");
        LLVMBasicBlockRef end_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "try.end");

        LLVMValueRef eh_tmptop_g = get_eh_tmp_top_global(g);
        /* compile-time id of this try within the enclosing async function;
         * indexes its frame-resident catch slots (-1 = not async / no room) */
        int async_hid = -1;
        LLVMValueRef tmp_mark = LLVMBuildLoad2(g->builder, i32t, eh_tmptop_g, "eh.tmpmark");
        LLVMValueRef old_top = LLVMBuildLoad2(g->builder, i32t, top_g, "eh.old");
        /* Spill try-entry EH state to allocas: an await inside the try body
         * splits the function during CPS lowering, so SSA values defined here
         * would not dominate their uses in the catch/end blocks. */
        LLVMValueRef tmp_mark_slot = emit_entry_alloca(g, i32t, "eh.tmpmark.slot");
        zan_store_fit(g, tmp_mark, tmp_mark_slot);
        LLVMValueRef old_top_slot = emit_entry_alloca(g, i32t, "eh.old.slot");
        zan_store_fit(g, old_top, old_top_slot);
        LLVMValueRef new_top = zan_add(g->builder, old_top,
            LLVMConstInt(i32t, 1, 0), "eh.new");
        zan_store_fit(g, new_top, top_g);
        /* record the depth this handler was armed at, so a throw below can
         * release the skipped frames' locals while they are still alive */
        zan_store_fit(g, tmp_mark, emit_eh_mark_ptr(g, new_top));
        /* An async frame records how many try handlers it currently has
         * armed (frame.hcount): __zan_async_unwind stops releasing frames at
         * the first one with an armed handler, since the exception resumes
         * inside it. Balanced by the decrements at try-exit / catch-entry. */
        if (g->current_async_frame) {
            LLVMValueRef hc_ptr = LLVMBuildStructGEP2(g->builder,
                g->current_async_frame_type, g->current_async_frame,
                ASYNC_FRAME_HCOUNT, "eh.hc");
            LLVMValueRef hc = LLVMBuildLoad2(g->builder, i32t, hc_ptr, "eh.hcv");
            zan_store_fit(g, zan_add(g->builder, hc,
                LLVMConstInt(i32t, 1, 0), "eh.hc1"), hc_ptr);
            /* Record this handler in the frame so a later resume can re-arm it:
             * the jmp_buf armed below dies with this invocation, but the try
             * itself stays open across every suspension in its body. The
             * re-entry block restores the eh bookkeeping this invocation's
             * try-entry would have written, then enters the catch. */
            async_hid = g->current_async_handler_next++;
            if (g->current_async_rearm_switch) {
                int hid = async_hid;
                LLVMValueRef hs = LLVMBuildStructGEP2(g->builder,
                    g->current_async_frame_type, g->current_async_frame,
                    ASYNC_FRAME_HSTACK, "eh.hs");
                LLVMValueRef hs_idx[2] = { LLVMConstInt(i32t, 0, 0), hc };
                LLVMValueRef hs_slot = LLVMBuildGEP2(g->builder,
                    LLVMArrayType(i32t, (unsigned)g->current_async_handler_cap),
                    hs, hs_idx, 2, "eh.hs.slot");
                zan_store_fit(g, LLVMConstInt(i32t, (unsigned)hid, 0), hs_slot);

                LLVMBasicBlockRef here = LLVMGetInsertBlock(g->builder);
                struct { const char *nm; LLVMValueRef sw; LLVMBasicBlockRef dst; } re[2] = {
                    { "try.rearm.init", g->current_async_rearm_init_switch,
                      g->current_async_rearm_next_bb },
                    { "try.rearm", g->current_async_rearm_switch, catch_bb }
                };
                for (int ri = 0; ri < 2; ri++) {
                    if (!re[ri].sw) continue;
                    LLVMBasicBlockRef re_bb = LLVMAppendBasicBlockInContext(g->ctx, fn,
                        re[ri].nm);
                    LLVMPositionBuilderAtEnd(g->builder, re_bb);
                    LLVMValueRef rtop = LLVMBuildLoad2(g->builder, i32t, top_g, "eh.rtop");
                    zan_store_fit(g, zan_sub(g->builder, rtop,
                        LLVMConstInt(i32t, 1, 0), "eh.rtop0"), old_top_slot);
                    zan_store_fit(g,
                        LLVMBuildLoad2(g->builder, i32t, eh_tmptop_g, "eh.rtmp"),
                        tmp_mark_slot);
                    LLVMBuildBr(g->builder, re[ri].dst);
                    LLVMAddCase(re[ri].sw, LLVMConstInt(i32t, (unsigned)hid, 0), re_bb);
                }
                LLVMPositionBuilderAtEnd(g->builder, here);
            }
            /* keep the heap frame in step with the allocas across this try:
             * a later invocation re-arms this handler and loads the frame at
             * its state block, so the frame must already carry what the try
             * entry saw. */
            emit_async_save_slots(g);
        }
        LLVMValueRef zero = LLVMConstInt(i32t, 0, 0);
        LLVMValueRef bufp = emit_eh_buf_ptr(g, new_top);
        LLVMValueRef r = emit_eh_setjmp(g, bufp);
        LLVMValueRef took = zan_icmp(g->builder, LLVMIntEQ, r, zero, "eh.took");
        LLVMBuildCondBr(g->builder, took, try_bb, catch_bb);

        LLVMPositionBuilderAtEnd(g->builder, try_bb);
        int try_start = locals->count;
        /* a throw inside this body unwinds to the catch below, skipping the
         * body's scope-exit releases: they move to the throw site */
        int saved_throw_base = g->throw_locals_base;
        int saved_throw_cbase = g->throw_catch_base;
        g->throw_locals_base = try_start;
        g->throw_catch_base = g->catch_cleanup_count;
        /* open this try's finally region: every exit path out of the body and
         * the catches below runs the finally before leaving (see
         * emit_pending_finallys) */
        int fin_idx = -1;
        if (stmt->try_stmt.finally_body &&
            g->finally_count < ZAN_MAX_FINALLY_DEPTH) {
            fin_idx = g->finally_count++;
            g->finallys[fin_idx].body = stmt->try_stmt.finally_body;
            g->finallys[fin_idx].monitor_obj = NULL;
            g->finallys[fin_idx].in_try_body = true;
        } else if (stmt->try_stmt.finally_body) {
            zan_diag_emit(g->diag, DIAG_ERROR, stmt->loc,
                "too many nested try/finally blocks in one function body");
        }
        int armed_idx = -1;
        if (g->eh_armed_count < ZAN_MAX_ARMED_TRY) {
            armed_idx = g->eh_armed_count++;
            g->eh_armed[armed_idx].old_top_slot = old_top_slot;
        }
        emit_stmt(g, stmt->try_stmt.try_body, locals);
        /* the handler covers the body only: the catches below run with it
         * already disarmed, so an exit path there leaves the enclosing tries */
        if (armed_idx >= 0) g->eh_armed_count = armed_idx;
        if (fin_idx >= 0) g->finallys[fin_idx].in_try_body = false;
        g->throw_locals_base = saved_throw_base;
        g->throw_catch_base = saved_throw_cbase;
        locals->count = try_start;
        if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder))) {
            LLVMValueRef ot = LLVMBuildLoad2(g->builder, i32t, old_top_slot, "eh.old.re");
            zan_store_fit(g, ot, top_g);
            if (g->current_async_frame) {
                LLVMValueRef hc_ptr = LLVMBuildStructGEP2(g->builder,
                    g->current_async_frame_type, g->current_async_frame,
                    ASYNC_FRAME_HCOUNT, "eh.hc");
                LLVMValueRef hc = LLVMBuildLoad2(g->builder, i32t, hc_ptr, "eh.hcv");
                zan_store_fit(g, zan_sub(g->builder, hc,
                    LLVMConstInt(i32t, 1, 0), "eh.hc0"), hc_ptr);
            }
            LLVMBuildBr(g->builder, end_bb);
        }

        LLVMPositionBuilderAtEnd(g->builder, catch_bb);
        {
            LLVMValueRef ot = LLVMBuildLoad2(g->builder, i32t, old_top_slot, "eh.old.re");
            zan_store_fit(g, ot, top_g);
            if (g->current_async_frame) {
                LLVMValueRef hc_ptr = LLVMBuildStructGEP2(g->builder,
                    g->current_async_frame_type, g->current_async_frame,
                    ASYNC_FRAME_HCOUNT, "eh.hc");
                LLVMValueRef hc = LLVMBuildLoad2(g->builder, i32t, hc_ptr, "eh.hcv");
                zan_store_fit(g, zan_sub(g->builder, hc,
                    LLVMConstInt(i32t, 1, 0), "eh.hc0"), hc_ptr);
                /* No reload from the frame: the handler this catch belongs to
                 * is re-armed by the invocation that raises the throw, whose
                 * state block already loaded the frame-resident slots, so the
                 * stack allocas are the live copy -- and the only one holding
                 * the locals assigned after the last suspension. */
            }
        }
        /* longjmp skipped the normal releases of temps pushed after this try
         * was entered (a throwing ctor's object, an owned receiver temp) —
         * release them now, restoring the temp stack to the try-entry depth */
        {
            LLVMTypeRef uwty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), &i32t, 1, 0);
            LLVMValueRef tm = LLVMBuildLoad2(g->builder, i32t, tmp_mark_slot, "eh.tmpmark.re");
            zan_call2(g->builder, uwty, get_eh_tmp_unwind_fn(g), &tm, 1, "");
        }
        LLVMValueRef exc_val = LLVMBuildLoad2(g->builder, i8ptr, exc_g, "exc");
        /* The caught exception and its ownership flag are read again by the
         * catch epilogue, which an `await` inside the catch body separates
         * from here by a suspension: this $resume invocation returns and its
         * stack allocas are garbage when the next one resumes into the
         * epilogue. Keep them in the heap frame, indexed by this try's
         * compile-time handler id, so they survive the suspension. */
        LLVMValueRef exc_slot, exc_owned_slot, exc_tid_slot;
        if (g->current_async_frame && async_hid >= 0 &&
            async_hid < g->current_async_handler_cap) {
            /* Address them in the entry block, like emit_entry_alloca: the
             * catch epilogue lives in a block the CPS split leaves outside
             * this one's dominance, so a GEP computed here would not
             * dominate its uses. */
            LLVMValueRef cidx[2] = { LLVMConstInt(i32t, 0, 0),
                                     LLVMConstInt(i32t, (unsigned)async_hid, 0) };
            LLVMBasicBlockRef cur_bb = LLVMGetInsertBlock(g->builder);
            LLVMBasicBlockRef entry_bb = LLVMGetEntryBasicBlock(
                LLVMGetBasicBlockParent(cur_bb));
            LLVMValueRef entry_term = LLVMGetBasicBlockTerminator(entry_bb);
            if (entry_term) LLVMPositionBuilderBefore(g->builder, entry_term);
            else LLVMPositionBuilderAtEnd(g->builder, entry_bb);
            exc_slot = LLVMBuildGEP2(g->builder,
                LLVMArrayType(i8ptr, (unsigned)g->current_async_handler_cap),
                LLVMBuildStructGEP2(g->builder, g->current_async_frame_type,
                    g->current_async_frame, ASYNC_FRAME_CEXC, "eh.cexc"),
                cidx, 2, "eh.exc.slot");
            exc_owned_slot = LLVMBuildGEP2(g->builder,
                LLVMArrayType(i32t, (unsigned)g->current_async_handler_cap),
                LLVMBuildStructGEP2(g->builder, g->current_async_frame_type,
                    g->current_async_frame, ASYNC_FRAME_CEXC_OWNED, "eh.cexcown"),
                cidx, 2, "eh.excown.slot");
            exc_tid_slot = LLVMBuildGEP2(g->builder,
                LLVMArrayType(i8ptr, (unsigned)g->current_async_handler_cap),
                LLVMBuildStructGEP2(g->builder, g->current_async_frame_type,
                    g->current_async_frame, ASYNC_FRAME_CEXC_TID, "eh.cexctid"),
                cidx, 2, "eh.exctid.slot");
            LLVMPositionBuilderAtEnd(g->builder, cur_bb);
        } else {
            exc_slot = emit_entry_alloca(g, i8ptr, "eh.exc.slot");
            /* set when a matched handler takes over the in-flight +1 (see the
             * ownership transfer at the top of each catch body) */
            exc_owned_slot = emit_entry_alloca(g, i32t, "eh.excown.slot");
            /* the thrown type, kept per handler so a bare `throw;` can rethrow
             * it after the global has moved on */
            exc_tid_slot = emit_entry_alloca(g, i8ptr, "eh.exctid.slot");
        }
        zan_store_fit(g, exc_val, exc_slot);
        zan_store_fit(g, LLVMConstInt(i32t, 0, 0), exc_owned_slot);
        zan_store_fit(g, LLVMConstNull(i8ptr), exc_tid_slot);
        int catch_start = locals->count;
        int ncatch = stmt->try_stmt.catches.count;
        if (ncatch > 0) {
            /* type-based dispatch: test each clause in order against the
             * thrown object's type descriptor (walking its base chain); if
             * no clause matches, rethrow to the next outer handler */
            LLVMValueRef tid_g = get_eh_exc_tid_global(g);
            LLVMValueRef thrown_tid = LLVMBuildLoad2(g->builder, i8ptr, tid_g, "exc.tid");
            zan_store_fit(g, thrown_tid, exc_tid_slot);
            LLVMValueRef match_fn = get_eh_tid_match_fn(g);
            LLVMTypeRef match_ty = LLVMFunctionType(LLVMInt1TypeInContext(g->ctx),
                (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0);
            LLVMBasicBlockRef done_bb =
                LLVMAppendBasicBlockInContext(g->ctx, fn, "catch.done");
            LLVMBasicBlockRef rethrow_bb =
                LLVMAppendBasicBlockInContext(g->ctx, fn, "catch.rethrow");

            for (int ci = 0; ci < ncatch; ci++) {
                zan_ast_node_t *cc = stmt->try_stmt.catches.items[ci];
                zan_type_t *et = cc->catch_clause.type
                    ? resolve_type_ctx(g, cc->catch_clause.type)
                    : NULL;
                LLVMBasicBlockRef body_bb =
                    LLVMAppendBasicBlockInContext(g->ctx, fn, "catch.body");
                LLVMBasicBlockRef miss_bb = (ci + 1 < ncatch)
                    ? LLVMAppendBasicBlockInContext(g->ctx, fn, "catch.test")
                    : rethrow_bb;
                if (et && et->kind == TYPE_CLASS && et->sym) {
                    LLVMValueRef want = LLVMBuildBitCast(g->builder,
                        get_class_tid_global(g, et->sym), i8ptr, "want.tid");
                    LLVMValueRef hits = zan_call2(g->builder, match_ty, match_fn,
                        (LLVMValueRef[]){ thrown_tid, want }, 2, "tid.hit");
                    LLVMBuildCondBr(g->builder, hits, body_bb, miss_bb);
                } else {
                    /* untyped / non-class clause: catches everything */
                    LLVMBuildBr(g->builder, body_bb);
                    if (miss_bb != rethrow_bb) {
                        /* unreachable later tests still need a terminator */
                        LLVMPositionBuilderAtEnd(g->builder, miss_bb);
                        LLVMBuildBr(g->builder, rethrow_bb);
                    }
                    miss_bb = NULL;
                }

                LLVMPositionBuilderAtEnd(g->builder, body_bb);
                if (cc->catch_clause.var_name.len > 0) {
                    LLVMValueRef ev = LLVMBuildLoad2(g->builder, i8ptr, exc_slot, "exc.re");
                    /* an await in the handler returns from this $resume, so a
                     * stack alloca would hold garbage when the handler resumes
                     * and dereferences the binding: use its frame slot (a
                     * storage-only local, ztype NULL, registered by the async
                     * scan) whenever the method has one */
                    LLVMValueRef ea = NULL;
                    if (g->current_async_frame) {
                        local_var_t *fv = local_find(locals, cc->catch_clause.var_name);
                        if (fv && !fv->type &&
                            LLVMGetTypeKind(local_slot_type(g, fv)) == LLVMPointerTypeKind)
                            ea = fv->alloca;
                    }
                    if (!ea) ea = emit_entry_alloca(g, i8ptr, "exc.var");
                    zan_store_fit(g, ev, ea);
                    local_add(locals, cc->catch_clause.var_name, ea, et);
                }
                /* transfer the in-flight +1 into this handler: clear the
                 * global owned flag (so a throw during the handler doesn't
                 * touch it) and stack the object as an EH temp so a throw
                 * escaping this handler releases it via the outer catch's
                 * temp unwind; the normal handler exit pops and releases. */
                {
                    LLVMValueRef owned_g2 = get_eh_exc_owned_global(g);
                    LLVMValueRef ofl = LLVMBuildLoad2(g->builder, i32t, owned_g2, "exc.ofl");
                    zan_store_fit(g, ofl, exc_owned_slot);
                    zan_store_fit(g, LLVMConstInt(i32t, 0, 0), owned_g2);
                    LLVMValueRef isown = zan_icmp(g->builder, LLVMIntNE, ofl,
                        LLVMConstInt(i32t, 0, 0), "exc.isown");
                    LLVMBasicBlockRef push_bb =
                        LLVMAppendBasicBlockInContext(g->ctx, fn, "exc.push");
                    LLVMBasicBlockRef pcont_bb =
                        LLVMAppendBasicBlockInContext(g->ctx, fn, "exc.pcont");
                    LLVMBuildCondBr(g->builder, isown, push_bb, pcont_bb);
                    LLVMPositionBuilderAtEnd(g->builder, push_bb);
                    LLVMValueRef ev2 = LLVMBuildLoad2(g->builder, i8ptr, exc_slot, "exc.re");
                    emit_eh_tmp_push(g, ev2);
                    LLVMBuildBr(g->builder, pcont_bb);
                    LLVMPositionBuilderAtEnd(g->builder, pcont_bb);
                }
                /* Grown on demand: skipping the entry left the handler's
                 * exception released only by the try epilogue, so a `return`
                 * or `throw` out of a deeply nested catch leaked it. */
                if (ZAN_TAB_ENSURE(g->catch_cleanups, g->catch_cleanup_count,
                                   g->catch_cleanup_cap, 16)) {
                    g->catch_cleanups[g->catch_cleanup_count].exc_slot = exc_slot;
                    g->catch_cleanups[g->catch_cleanup_count].owned_slot =
                        exc_owned_slot;
                    g->catch_cleanups[g->catch_cleanup_count].tid_slot =
                        exc_tid_slot;
                    g->catch_cleanup_count++;
                } else {
                    zan_diag_emit(g->diag, DIAG_ERROR, stmt->loc,
                        "out of memory tracking nested catch handlers");
                }
                int saved_cc = g->catch_cleanup_count;
                emit_stmt(g, cc->catch_clause.body, locals);
                g->catch_cleanup_count = saved_cc - 1;
                locals->count = catch_start;
                if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder)))
                    LLVMBuildBr(g->builder, done_bb);

                if (!miss_bb) break;   /* catch-all consumed the rest */
                LLVMPositionBuilderAtEnd(g->builder, miss_bb);
            }
            /* current block is the last miss target when every clause is
             * typed; it already IS rethrow_bb in that case */
            if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder)) &&
                LLVMGetInsertBlock(g->builder) != rethrow_bb)
                LLVMBuildBr(g->builder, rethrow_bb);

            /* rethrow: the exception object/owned/tid globals still hold the
             * in-flight exception; run this try's finally (C# runs it while
             * unwinding), then jump to the next outer handler (this try's frame
             * was already popped above) or die if none remains */
            LLVMPositionBuilderAtEnd(g->builder, rethrow_bb);
            emit_finally_on_exception_path(g, locals, fin_idx);
            if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder)))
                emit_eh_propagate_tail(g);

            LLVMPositionBuilderAtEnd(g->builder, done_bb);
        } else {
            /* try/finally with no catch clause catches nothing: run the finally
             * and hand the exception to the next outer handler. (Falling
             * through to the epilogue below would release it and continue as if
             * nothing had been thrown.) */
            emit_finally_on_exception_path(g, locals, fin_idx);
            if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder)))
                emit_eh_propagate_tail(g);
        }
        locals->count = catch_start;
        if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder))) {
            /* drop the throw-site +1 on the caught exception (every
             * RC-managed throw -- class, string, array, delegate -- sets
             * __zan_eh_exc_owned; only non-managed values leave it 0) */
            LLVMValueRef owned_g = get_eh_exc_owned_global(g);
            LLVMValueRef ofl = LLVMBuildLoad2(g->builder, i32t, owned_g, "exc.owned");
            LLVMValueRef is_owned = zan_icmp(g->builder, LLVMIntNE, ofl,
                LLVMConstInt(i32t, 0, 0), "exc.isown");
            LLVMValueRef cfn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder));
            LLVMBasicBlockRef rel_bb = LLVMAppendBasicBlockInContext(g->ctx, cfn, "exc.rel");
            LLVMBasicBlockRef cont_bb = LLVMAppendBasicBlockInContext(g->ctx, cfn, "exc.cont");
            LLVMBuildCondBr(g->builder, is_owned, rel_bb, cont_bb);
            LLVMPositionBuilderAtEnd(g->builder, rel_bb);
            LLVMValueRef ev = LLVMBuildLoad2(g->builder, i8ptr, exc_slot, "exc.re");
            zan_call2(g->builder,
                LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), &i8ptr, 1, 0),
                g->rt_release_dyn, &ev, 1, "");
            zan_store_fit(g, LLVMConstInt(i32t, 0, 0), owned_g);
            zan_store_fit(g, LLVMConstNull(i8ptr), exc_g);
            LLVMBuildBr(g->builder, cont_bb);
            LLVMPositionBuilderAtEnd(g->builder, cont_bb);
            /* handler-owned release: a matched catch body transferred the
             * in-flight +1 to itself (owned flag in exc_owned_slot, object
             * stacked as an EH temp) -- pop the temp and release it now */
            LLVMValueRef hofl = LLVMBuildLoad2(g->builder, i32t, exc_owned_slot,
                "exc.hown");
            LLVMValueRef h_owned = zan_icmp(g->builder, LLVMIntNE, hofl,
                LLVMConstInt(i32t, 0, 0), "exc.hisown");
            LLVMBasicBlockRef hrel_bb = LLVMAppendBasicBlockInContext(g->ctx, cfn, "exc.hrel");
            LLVMBasicBlockRef hcont_bb = LLVMAppendBasicBlockInContext(g->ctx, cfn, "exc.hcont");
            LLVMBuildCondBr(g->builder, h_owned, hrel_bb, hcont_bb);
            LLVMPositionBuilderAtEnd(g->builder, hrel_bb);
            emit_eh_tmp_pop(g);
            LLVMValueRef hev = LLVMBuildLoad2(g->builder, i8ptr, exc_slot, "exc.hre");
            zan_call2(g->builder,
                LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), &i8ptr, 1, 0),
                g->rt_release_dyn, &hev, 1, "");
            zan_store_fit(g, LLVMConstNull(i8ptr), exc_g);
            LLVMBuildBr(g->builder, hcont_bb);
            LLVMPositionBuilderAtEnd(g->builder, hcont_bb);
            LLVMBuildBr(g->builder, end_bb);
        }

        /* the region is closed: the normal-exit copy of the finally below is
         * the last one this try emits */
        if (fin_idx >= 0) g->finally_count = fin_idx;
        LLVMPositionBuilderAtEnd(g->builder, end_bb);
        if (stmt->try_stmt.finally_body) {
            emit_stmt(g, stmt->try_stmt.finally_body, locals);
        }
        break;
    }

    case AST_DO_WHILE_STMT: {
        /* do { body } while (cond); */
        int body_start = locals->count;
        LLVMValueRef fn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder));
        LLVMBasicBlockRef body_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "do.body");
        LLVMBasicBlockRef cond_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "do.cond");
        LLVMBasicBlockRef end_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "do.end");
        LLVMBuildBr(g->builder, body_bb);

        LLVMPositionBuilderAtEnd(g->builder, body_bb);
        emit_stmt(g, stmt->while_stmt.body, locals);
        if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder))) {
            emit_release_owned_locals_from(g, locals, body_start);
            LLVMBuildBr(g->builder, cond_bb);
        } else {
            emit_release_owned_locals_from(g, locals, body_start);
        }

        LLVMPositionBuilderAtEnd(g->builder, cond_bb);
        LLVMValueRef cond = emit_expr(g, stmt->while_stmt.cond, locals);
        cond = zan_tobool(g->builder, cond, "dcond");
        LLVMBuildCondBr(g->builder, cond, body_bb, end_bb);

        LLVMPositionBuilderAtEnd(g->builder, end_bb);
        break;
    }

    case AST_FOREACH_STMT: {
        /* foreach (var x in collection) { body } — simplified: iterate List<T> */
        LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
        LLVMValueRef fn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder));
        int fe_start = locals->count;

        /* evaluate collection */
        LLVMValueRef collection = emit_expr(g, stmt->foreach_stmt.collection, locals);

        /* element type: declared loop-var type, else inferred from collection */
        zan_type_t *elem_type = NULL;
        if (stmt->foreach_stmt.var_type)
            elem_type = resolve_type_ctx(g, stmt->foreach_stmt.var_type);
        if (!elem_type || elem_type->kind == TYPE_ERROR)
            elem_type = container_elem_type(
                infer_expr_type(g, stmt->foreach_stmt.collection, locals));
        if (!elem_type) elem_type = g->binder->type_int;
        LLVMTypeRef elem_llvm = map_type(g, elem_type);

        /* How the collection is laid out. A List has a count and a data
         * pointer; an array is a bare buffer whose length was captured where
         * it was declared; a string is NUL-terminated bytes. foreach used to
         * read every one of them as a List, so iterating an array or a string
         * loaded a count and a data pointer out of unrelated memory and the
         * program died on the first element. */
        zan_type_t *col_type = infer_expr_type(g, stmt->foreach_stmt.collection, locals);
        bool fe_array = col_type && col_type->kind == TYPE_ARRAY;
        bool fe_string = col_type && col_type->kind == TYPE_STRING;
        LLVMValueRef fe_len = NULL;
        if (fe_array) {
            fe_len = zan_array_len(g, collection);
        } else if (fe_string) {
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMValueRef sp = LLVMBuildBitCast(g->builder, collection, i8ptr, "fe.sp");
            fe_len = emit_string_length(g, sp, stmt->loc);
        }

        /* Iteration state (element, index, collection) of a `foreach` in an
         * async body lives in the heap frame: an await in the loop body
         * re-enters at a block the pre-header never runs, so values computed
         * here neither dominate their uses nor survive the suspension. The
         * collection is what gets stored -- count and data are re-derived per
         * iteration, which is also what keeps the loop correct across a
         * reallocation of the list. */
        LLVMValueRef col_slot = NULL, idx_alloc = NULL, iter_alloc = NULL;
        if (g->current_async_frame) {
            int fe_id = g->current_async_foreach_next++;
            char nm[32];
            snprintf(nm, sizeof(nm), "$fe.c%d", fe_id);
            zan_istr_t cn = { nm, (uint32_t)strlen(nm) };
            local_var_t *cv = local_find(locals, cn);
            if (cv) col_slot = cv->alloca;
            snprintf(nm, sizeof(nm), "$fe.i%d", fe_id);
            zan_istr_t in_ = { nm, (uint32_t)strlen(nm) };
            local_var_t *iv = local_find(locals, in_);
            if (iv) idx_alloc = iv->alloca;
            /* the loop variable's frame slot is the storage-only one this
             * foreach registered (ztype NULL); anything else with that name is
             * a user local that merely shadows it, and must not be aliased */
            local_var_t *ev = local_find(locals, stmt->foreach_stmt.var_name);
            if (ev && !ev->type && LLVMGetTypeKind(elem_llvm) ==
                    LLVMGetTypeKind(local_slot_type(g, ev)))
                iter_alloc = ev->alloca;
        }
        if (col_slot)
            zan_store_fit(g,
                LLVMBuildBitCast(g->builder, collection,
                    LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0), "fe.colp"),
                col_slot);

        /* index variable */
        if (!idx_alloc) idx_alloc = emit_entry_alloca(g, i64, "fi");
        zan_store_fit(g, LLVMConstInt(i64, 0, 0), idx_alloc);

        /* iteration variable */
        if (!iter_alloc) iter_alloc = emit_entry_alloca(g, elem_llvm, "fv");
        local_add(locals, stmt->foreach_stmt.var_name, iter_alloc, elem_type);

        LLVMTypeRef list_ptr_ty = LLVMTypeOf(collection);
        LLVMBasicBlockRef cond_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "fe.cond");
        LLVMBasicBlockRef body_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "fe.body");
        /* the index advance is its own block so `continue` can reach it: a
         * `continue` that branched straight to the condition would spin
         * forever on the same element */
        LLVMBasicBlockRef step_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "fe.step");
        LLVMBasicBlockRef end_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "fe.end");

        LLVMBasicBlockRef fe_saved_break = g->break_target;
        LLVMBasicBlockRef fe_saved_cont = g->continue_target;
        int fe_saved_loop_base = g->loop_locals_base;
        int fe_saved_loop_cbase = g->loop_catch_base;
        int fe_saved_loop_fbase = g->finally_loop_base;
        int fe_saved_loop_ehbase = g->eh_armed_loop_base;
        g->break_target = end_bb;
        g->continue_target = step_bb;
        g->loop_locals_base = fe_start;
        g->loop_catch_base = g->catch_cleanup_count;
        g->finally_loop_base = g->finally_count;
        g->eh_armed_loop_base = g->eh_armed_count;

        LLVMBuildBr(g->builder, cond_bb);
        LLVMPositionBuilderAtEnd(g->builder, cond_bb);
        /* count: field 0 of the List struct, re-read each iteration */
        LLVMValueRef col_cond = col_slot
            ? LLVMBuildBitCast(g->builder,
                  LLVMBuildLoad2(g->builder,
                      LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0),
                      col_slot, "fe.col"),
                  list_ptr_ty, "fe.colc")
            : collection;
        LLVMValueRef count;
        if (fe_array || fe_string) {
            /* the pre-header length is a register, so in an async body it
             * neither dominates the condition (re-entered from a later resume)
             * nor survives the suspension: re-derive it from the reloaded
             * collection, exactly as the List path re-reads its count */
            if (col_slot && fe_array) {
                count = zan_array_len(g, col_cond);
            } else if (col_slot) {
                LLVMTypeRef i8p = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                LLVMValueRef sp2 = LLVMBuildBitCast(g->builder, col_cond, i8p, "fe.sp2");
                count = emit_string_length(g, sp2, stmt->loc);
            } else {
                count = fe_len;
            }
        } else {
            LLVMValueRef cnt_ptr = LLVMBuildStructGEP2(g->builder, g->list_struct_type,
                col_cond, 0, "cnt_ptr");
            count = LLVMBuildLoad2(g->builder, i64, cnt_ptr, "cnt");
        }
        LLVMValueRef idx_val = LLVMBuildLoad2(g->builder, i64, idx_alloc, "i");
        LLVMValueRef cmp = zan_icmp(g->builder, LLVMIntSLT, idx_val, count, "fcmp");
        LLVMBuildCondBr(g->builder, cmp, body_bb, end_bb);

        LLVMPositionBuilderAtEnd(g->builder, body_bb);
        /* data pointer: field 2 of the List struct, likewise re-read here */
        LLVMValueRef col_body = col_slot
            ? LLVMBuildBitCast(g->builder,
                  LLVMBuildLoad2(g->builder,
                      LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0),
                      col_slot, "fe.col"),
                  list_ptr_ty, "fe.colb")
            : collection;
        if (fe_array || fe_string) {
            LLVMValueRef ai = LLVMBuildLoad2(g->builder, i64, idx_alloc, "ib");
            LLVMTypeRef slot_ty = fe_string
                ? LLVMInt8TypeInContext(g->ctx) : elem_llvm;
            LLVMValueRef ep = LLVMBuildGEP2(g->builder, slot_ty, col_body, &ai, 1, "aep");
            LLVMValueRef av = LLVMBuildLoad2(g->builder, slot_ty, ep, "aelem");
            if (fe_string && slot_ty != elem_llvm)
                av = zan_iwiden(g->builder, av, elem_llvm);
            zan_store_fit(g, av, iter_alloc);

            emit_stmt(g, stmt->foreach_stmt.body, locals);
            emit_release_owned_locals_from(g, locals, fe_start);
            if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder)))
                LLVMBuildBr(g->builder, step_bb);

            LLVMPositionBuilderAtEnd(g->builder, step_bb);
            zan_store_fit(g, zan_add(g->builder,
                LLVMBuildLoad2(g->builder, i64, idx_alloc, "i2"),
                LLVMConstInt(i64, 1, 0), "next"), idx_alloc);
            LLVMBuildBr(g->builder, cond_bb);

            g->break_target = fe_saved_break;
            g->continue_target = fe_saved_cont;
            g->loop_locals_base = fe_saved_loop_base;
            g->loop_catch_base = fe_saved_loop_cbase;
            g->finally_loop_base = fe_saved_loop_fbase;
            g->eh_armed_loop_base = fe_saved_loop_ehbase;
            LLVMPositionBuilderAtEnd(g->builder, end_bb);
            emit_release_owned_call_temp(g, stmt->foreach_stmt.collection,
                                         collection, locals);
            break;
        }
        LLVMValueRef data_ptr = LLVMBuildStructGEP2(g->builder, g->list_struct_type,
            col_body, 2, "data_ptr");
        LLVMValueRef data = LLVMBuildLoad2(g->builder, LLVMPointerType(i64, 0),
            data_ptr, "data");
        LLVMValueRef idx_body = LLVMBuildLoad2(g->builder, i64, idx_alloc, "ib");
        /* load current element; slots physically hold an i64, so pointer
         * (class/string) elements need inttoptr, doubles a bitcast, and
         * narrower integers a trunc back to the value type */
        LLVMValueRef fe_widx = slot_word_index(g, idx_body,
            elem_slot_words(g, elem_type));
        LLVMValueRef elem_ptr = LLVMBuildGEP2(g->builder, i64, data, &fe_widx, 1, "ep");
        LLVMTypeKind ek = LLVMGetTypeKind(elem_llvm);
        LLVMValueRef elem = (ek == LLVMStructTypeKind)
            ? load_struct_from_slot(g, elem_ptr, elem_llvm)
            : LLVMBuildLoad2(g->builder, i64, elem_ptr, "elem");
        if (ek == LLVMPointerTypeKind)
            elem = LLVMBuildIntToPtr(g->builder, elem, elem_llvm, "elp");
        else if (ek == LLVMDoubleTypeKind)
            elem = LLVMBuildBitCast(g->builder, elem, elem_llvm, "elf");
        else if (ek == LLVMIntegerTypeKind && LLVMGetIntTypeWidth(elem_llvm) < 64)
            elem = LLVMBuildTrunc(g->builder, elem, elem_llvm, "elt");
        zan_store_fit(g, elem, iter_alloc);

        emit_stmt(g, stmt->foreach_stmt.body, locals);

        if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder))) {
            emit_release_owned_locals_from(g, locals, fe_start);
            LLVMBuildBr(g->builder, step_bb);
        } else {
            emit_release_owned_locals_from(g, locals, fe_start);
        }

        LLVMPositionBuilderAtEnd(g->builder, step_bb);
        LLVMValueRef next = zan_add(g->builder,
            LLVMBuildLoad2(g->builder, i64, idx_alloc, "i2"),
            LLVMConstInt(i64, 1, 0), "next");
        zan_store_fit(g, next, idx_alloc);
        LLVMBuildBr(g->builder, cond_bb);

        g->break_target = fe_saved_break;
        g->continue_target = fe_saved_cont;
        g->loop_locals_base = fe_saved_loop_base;
        g->loop_catch_base = fe_saved_loop_cbase;
        g->finally_loop_base = fe_saved_loop_fbase;
        g->eh_armed_loop_base = fe_saved_loop_ehbase;

        LLVMPositionBuilderAtEnd(g->builder, end_bb);
        /* an owned temporary collection (e.g. iterating a call result) is
         * consumed by the loop and released once iteration ends */
        LLVMValueRef col_end = col_slot
            ? LLVMBuildBitCast(g->builder,
                  LLVMBuildLoad2(g->builder,
                      LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0),
                      col_slot, "fe.col"),
                  list_ptr_ty, "fe.cole")
            : collection;
        emit_release_owned_call_temp(g, stmt->foreach_stmt.collection,
                                     col_end, locals);
        break;
    }

    case AST_LOCK_STMT: {
        /* lock (expr) body — enter/exit the runtime monitor around the body */
        g->uses_sync_runtime = true;
        LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
        LLVMTypeRef mon_ty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx),
                                              &i8ptr, 1, 0);
        LLVMValueRef enter_fn = LLVMGetNamedFunction(g->mod, "zan_monitor_enter");
        if (!enter_fn)
            enter_fn = LLVMAddFunction(g->mod, "zan_monitor_enter", mon_ty);
        LLVMValueRef exit_fn = LLVMGetNamedFunction(g->mod, "zan_monitor_exit");
        if (!exit_fn)
            exit_fn = LLVMAddFunction(g->mod, "zan_monitor_exit", mon_ty);

        LLVMValueRef obj = emit_expr(g, stmt->lock_stmt.expr, locals);
        LLVMTypeRef ot = LLVMTypeOf(obj);
        if (LLVMGetTypeKind(ot) == LLVMIntegerTypeKind)
            obj = LLVMBuildIntToPtr(g->builder, obj, i8ptr, "lockp");
        else if (LLVMGetTypeKind(ot) == LLVMPointerTypeKind && ot != i8ptr)
            obj = LLVMBuildBitCast(g->builder, obj, i8ptr, "lockp");
        /* C# lowers `lock` to try/finally, and so does this: a `return`,
         * `break` or throw out of the body used to walk off with the monitor
         * still held, which wedges every other thread for good. The object
         * goes in an alloca so those exit paths can reload it. */
        LLVMValueRef obj_slot = emit_entry_alloca(g, i8ptr, "lock.slot");
        zan_store_fit(g, obj, obj_slot);
        zan_call2(g->builder, mon_ty, enter_fn, &obj, 1, "");
        int lock_fin = -1;
        if (g->finally_count < ZAN_MAX_FINALLY_DEPTH) {
            lock_fin = g->finally_count++;
            g->finallys[lock_fin].body = NULL;
            g->finallys[lock_fin].monitor_obj = obj_slot;
            /* a throw in the body has no handler here, so it releases the
             * monitor at the throw site */
            g->finallys[lock_fin].in_try_body = false;
        } else {
            zan_diag_emit(g->diag, DIAG_ERROR, stmt->loc,
                "too many nested lock/try-finally blocks in one function body");
        }
        emit_stmt(g, stmt->lock_stmt.body, locals);
        if (lock_fin >= 0) g->finally_count = lock_fin;
        if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder))) {
            (void)exit_fn;
            emit_monitor_exit(g, obj_slot);
        }
        break;
    }

    case AST_LABEL_STMT: {
        LLVMBasicBlockRef bb = irgen_goto_label(g, stmt->ident.name);
        if (!bb) break;
        if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder)))
            LLVMBuildBr(g->builder, bb);
        LLVMPositionBuilderAtEnd(g->builder, bb);
        break;
    }

    case AST_GOTO_STMT: {
        LLVMValueRef gfn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder));
        LLVMBasicBlockRef bb = irgen_goto_label(g, stmt->ident.name);
        if (!bb) break;
        if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder)))
            LLVMBuildBr(g->builder, bb);
        /* anything after an unconditional goto is unreachable; park in a
         * fresh block so subsequent emission stays well-formed */
        LLVMBasicBlockRef cont =
            LLVMAppendBasicBlockInContext(g->ctx, gfn, "goto.cont");
        LLVMPositionBuilderAtEnd(g->builder, cont);
        break;
    }

    case AST_THROW_STMT: {
        /* throw expr; — store the exception and longjmp to the innermost
         * enclosing try (if any); otherwise print and exit(1).
         * throw;      — rethrow what the enclosing catch is handling. */
        bool rethrow = stmt->throw_stmt.value == NULL;
        if (rethrow) {
            /* the handler's slots live in the function that opened it: a
             * `throw;` in a lambda body nested inside a catch is not a
             * rethrow site (C# rejects it too) */
            LLVMValueRef hslot = g->catch_cleanup_count > 0
                ? g->catch_cleanups[g->catch_cleanup_count - 1].exc_slot : NULL;
            LLVMBasicBlockRef hbb = hslot ? LLVMGetInstructionParent(hslot) : NULL;
            if (!hbb || LLVMGetBasicBlockParent(hbb) !=
                    LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder))) {
                zan_diag_emit(g->diag, DIAG_ERROR, stmt->loc,
                    "a rethrow (`throw;`) is only valid inside a catch block");
                break;
            }
        }
        emit_eh_hook_call(g, "__zan_eh_throw");
        LLVMValueRef val = rethrow
            ? NULL : emit_expr(g, stmt->throw_stmt.value, locals);
        LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
        {
            LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
            LLVMValueRef top_g, bufs_g, exc_g;
            get_eh_globals(g, &top_g, &bufs_g, &exc_g);
            if (rethrow) {
                /* Put the handler's exception back in flight unchanged -- same
                 * object, same type descriptor, so outer clauses dispatch on
                 * its original dynamic type -- and hand its +1 back to the
                 * globals: the handler stops owning it, so drop the EH temp
                 * entry it stacked at entry and clear its owned flag (both the
                 * temp unwind and emit_release_active_catch_excs below would
                 * otherwise release it under the outer handler). */
                int ci = g->catch_cleanup_count - 1;
                LLVMValueRef exc_slot = g->catch_cleanups[ci].exc_slot;
                LLVMValueRef own_slot = g->catch_cleanups[ci].owned_slot;
                LLVMValueRef tid_slot = g->catch_cleanups[ci].tid_slot;
                LLVMValueRef ev = LLVMBuildLoad2(g->builder, i8ptr, exc_slot, "reth.exc");
                LLVMValueRef ofl = LLVMBuildLoad2(g->builder, i32t, own_slot, "reth.own");
                zan_store_fit(g, ev, exc_g);
                zan_store_fit(g, ofl, get_eh_exc_owned_global(g));
                zan_store_fit(g,
                    LLVMBuildLoad2(g->builder, i8ptr, tid_slot, "reth.tid"),
                    get_eh_exc_tid_global(g));
                LLVMValueRef rfn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder));
                LLVMValueRef isown = zan_icmp(g->builder, LLVMIntNE, ofl,
                    LLVMConstInt(i32t, 0, 0), "reth.isown");
                LLVMBasicBlockRef drop_bb =
                    LLVMAppendBasicBlockInContext(g->ctx, rfn, "reth.drop");
                LLVMBasicBlockRef dcont_bb =
                    LLVMAppendBasicBlockInContext(g->ctx, rfn, "reth.dcont");
                LLVMBuildCondBr(g->builder, isown, drop_bb, dcont_bb);
                LLVMPositionBuilderAtEnd(g->builder, drop_bb);
                emit_eh_tmp_drop(g, ev);
                LLVMBuildBr(g->builder, dcont_bb);
                LLVMPositionBuilderAtEnd(g->builder, dcont_bb);
                zan_store_fit(g, LLVMConstInt(i32t, 0, 0), own_slot);
                goto throw_unwind;
            }
            LLVMValueRef vail = val;
            if (LLVMGetTypeKind(LLVMTypeOf(vail)) != LLVMPointerTypeKind)
                vail = LLVMConstNull(i8ptr);
            else if (LLVMTypeOf(vail) != i8ptr)
                vail = LLVMBuildBitCast(g->builder, vail, i8ptr, "exc.bc");
            zan_store_fit(g, vail, exc_g);
            /* rc convention: for class-typed throws __zan_eh_exc carries a +1
             * reference (retain borrowed values here); the catch releases it
             * once the handler completes */
            {
                LLVMValueRef owned_g = get_eh_exc_owned_global(g);
                LLVMValueRef tid_g = get_eh_exc_tid_global(g);
                zan_type_t *tt = infer_expr_type(g, stmt->throw_stmt.value, locals);
                if (tt && is_rc_managed_type(tt) &&
                    LLVMGetTypeKind(LLVMTypeOf(val)) == LLVMPointerTypeKind) {
                    /* rc convention: __zan_eh_exc carries a +1 reference for
                     * every RC-managed throw -- class, string, array or
                     * delegate. Retain borrowed values here; the catch (and
                     * the propagate path) release through rt_release_dyn,
                     * which dispatches on the header tag. Leaving strings and
                     * arrays unowned used to let the pre-longjmp unwind
                     * release the local's last reference while __zan_eh_exc
                     * still pointed at it: the handler read freed memory. */
                    if (!expr_yields_owned_rc_value(g, stmt->throw_stmt.value, locals))
                        /* type-aware: a string retain must go through the
                         * sentinel-tolerant helper -- interned literals sit in
                         * read-only static storage, and the plain object
                         * retain would fault incrementing their refcount */
                        emit_rc_retain_for_type(g, tt, vail);
                    zan_store_fit(g, LLVMConstInt(i32t, 1, 0), owned_g);
                    /* record the thrown class's type descriptor so catch
                     * clauses can dispatch by type */
                    if (tt->kind == TYPE_CLASS && tt->sym) {
                        LLVMValueRef tid = get_class_tid_global(g, tt->sym);
                        zan_store_fit(g,
                            LLVMBuildBitCast(g->builder, tid, i8ptr, "tid.bc"),
                            tid_g);
                    } else {
                        zan_store_fit(g, LLVMConstNull(i8ptr), tid_g);
                    }
                } else {
                    zan_store_fit(g, LLVMConstInt(i32t, 0, 0), owned_g);
                    zan_store_fit(g, LLVMConstNull(i8ptr), tid_g);
                }
            }
throw_unwind:
            /* a throw out of a catch (or finally) body leaves that try, so its
             * finally runs here -- the longjmp below goes straight to an outer
             * handler and would skip it */
            emit_finallys_left_by_throw(g, locals);
            /* longjmp skips every scope-exit release between here and the
             * handler. An async body's locals live in its heap frame, which
             * the frame chain releases, so releasing this frame's abandoned
             * ones here is enough (A8-3). A plain frame's locals -- and those
             * of every frame between here and the handler -- are only
             * reachable through the slots they registered on the unwind
             * stack, released below while those frames are still alive
             * (A8-12). `throw` is noreturn, so the releases the normal paths
             * emit for the same locals stay correct: they are only reached
             * when no exception was raised. */
            if (g->current_async_frame) {
                emit_release_owned_locals_range(g, locals, g->throw_locals_base);
                emit_clear_owned_locals_range(g, locals, g->throw_locals_base);
            }
            /* a throw out of a catch body skips that handler's epilogue, so
             * the exception it caught is released here (the new one is
             * already stored in the globals and retained) */
            emit_release_active_catch_excs(g, g->throw_catch_base);
            LLVMValueRef top = LLVMBuildLoad2(g->builder, i32t, top_g, "eh.top");
            LLVMValueRef has = zan_icmp(g->builder, LLVMIntSGE, top,
                LLVMConstInt(i32t, 0, 0), "eh.has");
            LLVMValueRef fn2 = LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder));
            LLVMBasicBlockRef jmp_bb = LLVMAppendBasicBlockInContext(g->ctx, fn2, "throw.jmp");
            LLVMBasicBlockRef die_bb = LLVMAppendBasicBlockInContext(g->ctx, fn2, "throw.die");
            LLVMBuildCondBr(g->builder, has, jmp_bb, die_bb);
            LLVMPositionBuilderAtEnd(g->builder, jmp_bb);
            /* the handler is either a try of this invocation or this
             * invocation's trampoline, so keep the frame authoritative and
             * longjmp without touching the frames that await us */
            if (g->current_async_frame)
                emit_async_save_slots(g);
            else
                emit_eh_unwind_to_handler(g, top);
            emit_eh_longjmp(g, emit_eh_buf_ptr(g, top));
            LLVMBuildUnreachable(g->builder);
            LLVMPositionBuilderAtEnd(g->builder, die_bb);
            emit_eh_hook_call(g, "__zan_eh_unhandled");
            LLVMValueRef printf_fn = LLVMGetNamedFunction(g->mod, "printf");
            if (printf_fn) {
                LLVMTypeRef printf_ty = LLVMFunctionType(LLVMInt32TypeInContext(g->ctx),
                    &i8ptr, 1, 1);
                /* A string throw prints its message; a class throw prints a
                 * type note. The thrown value alone is not enough: a class
                 * object is also an i8*, and printing it as text yields
                 * garbage. The type descriptor (null = string) tells the two
                 * apart. */
                LLVMValueRef dexc = LLVMBuildLoad2(g->builder, i8ptr, exc_g,
                    "die.exc");
                LLVMValueRef dtid = LLVMBuildLoad2(g->builder, i8ptr,
                    get_eh_exc_tid_global(g), "die.tid");
                LLVMValueRef dhas = zan_icmp(g->builder, LLVMIntNE, dexc,
                    LLVMConstNull(i8ptr), "die.has");
                LLVMValueRef dstr = zan_icmp(g->builder, LLVMIntEQ, dtid,
                    LLVMConstNull(i8ptr), "die.str");
                LLVMBasicBlockRef die_check_bb =
                    LLVMAppendBasicBlockInContext(g->ctx, fn2, "die.check");
                LLVMBasicBlockRef die_str_bb =
                    LLVMAppendBasicBlockInContext(g->ctx, fn2, "die.str");
                LLVMBasicBlockRef die_cls_bb =
                    LLVMAppendBasicBlockInContext(g->ctx, fn2, "die.cls");
                LLVMBasicBlockRef die_none_bb =
                    LLVMAppendBasicBlockInContext(g->ctx, fn2, "die.none");
                LLVMBasicBlockRef die_cont_bb =
                    LLVMAppendBasicBlockInContext(g->ctx, fn2, "die.cont");
                LLVMBuildCondBr(g->builder, dhas, die_check_bb, die_none_bb);
                LLVMPositionBuilderAtEnd(g->builder, die_check_bb);
                LLVMBuildCondBr(g->builder, dstr, die_str_bb, die_cls_bb);
                LLVMPositionBuilderAtEnd(g->builder, die_str_bb);
                {
                    LLVMValueRef fmt = LLVMBuildGlobalStringPtr(g->builder,
                        "Unhandled exception: %s\n", "die.sfmt");
                    LLVMValueRef args[] = { fmt, dexc };
                    zan_call2(g->builder, printf_ty, printf_fn, args, 2, "");
                }
                LLVMBuildBr(g->builder, die_cont_bb);
                LLVMPositionBuilderAtEnd(g->builder, die_cls_bb);
                {
                    LLVMValueRef fmt = LLVMBuildGlobalStringPtr(g->builder,
                        "Unhandled exception (class object)\n", "die.cfmt");
                    LLVMValueRef args[] = { fmt };
                    zan_call2(g->builder, printf_ty, printf_fn, args, 1, "");
                }
                LLVMBuildBr(g->builder, die_cont_bb);
                LLVMPositionBuilderAtEnd(g->builder, die_none_bb);
                {
                    LLVMValueRef fmt = LLVMBuildGlobalStringPtr(g->builder,
                        "Unhandled exception\n", "die.nfmt");
                    LLVMValueRef args[] = { fmt };
                    zan_call2(g->builder, printf_ty, printf_fn, args, 1, "");
                }
                LLVMBuildBr(g->builder, die_cont_bb);
                LLVMPositionBuilderAtEnd(g->builder, die_cont_bb);
            }
        }
        /* ARC cleanup: release all managed locals before aborting */
        release_all_arc_locals(g, locals);
        /* call exit(1) */
        LLVMTypeRef exit_args[] = { LLVMInt32TypeInContext(g->ctx) };
        LLVMTypeRef exit_type = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx),
            exit_args, 1, 0);
        LLVMValueRef exit_fn = LLVMGetNamedFunction(g->mod, "exit");
        if (!exit_fn) {
            exit_fn = LLVMAddFunction(g->mod, "exit", exit_type);
        }
        LLVMValueRef exit_arg = LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 1, 0);
        zan_call2(g->builder, exit_type, exit_fn, &exit_arg, 1, "");
        LLVMBuildUnreachable(g->builder);
        break;
    }

    default:
        break;
    }

    if (arc_nested) g->arc_stmt_depth--;
}

/* Release every RC-managed static field into its backing global at program
 * exit, so long-lived singletons held in static fields do not leak. Mirrors
 * the static-field initializer pass at main() entry. Runs before the leak
 * report (which is scheduled via atexit and therefore fires afterwards).
 *
 * The sweep walks the registry built by get_static_field_global rather than
 * the compilation unit passed in: that unit only contains main()'s own
 * declarations, while stdlib singletons (Pinyin.cache, ...) are compiled into
 * separate units whose static fields would otherwise stay alive. */
static void emit_release_static_rc_fields(zan_irgen_t *g, zan_ast_node_t *unit) {
    (void)unit;
    for (int i = 0; i < g->static_field_count; i++) {
        zan_type_t *ft = g->static_fields[i].type;
        if (!ft) continue;
        LLVMValueRef gv = g->static_fields[i].gv;
        LLVMTypeRef lt = map_type(g, ft);
        LLVMValueRef old = LLVMBuildLoad2(g->builder, lt, gv, "sf.rel");
        emit_rc_release_for_type(g, ft, old);
        zan_store_fit(g, LLVMConstNull(lt), gv);
    }
}
