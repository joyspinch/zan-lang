/* irgen_stmt.c -- statement codegen (emit_stmt and friends).
 *
 * Part of the irgen translation unit: this file is #include'd by irgen.c
 * (in a fixed order) and must not be compiled standalone. Splitting keeps
 * the single-TU static linkage while keeping each concern in its own file.
 */

/* ---- statement codegen ---- */

/* Find or create the basic block for a goto label in the current function. */
static LLVMBasicBlockRef irgen_goto_label(zan_irgen_t *g, zan_istr_t name) {
    LLVMValueRef fn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder));
    for (int i = 0; i < g->goto_label_count; i++) {
        if (g->goto_labels[i].fn == fn &&
            g->goto_labels[i].name.len == name.len &&
            memcmp(g->goto_labels[i].name.str, name.str,
                   (size_t)name.len) == 0)
            return g->goto_labels[i].bb;
    }
    if (g->goto_label_count >= 256) return NULL;
    LLVMBasicBlockRef bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "label");
    g->goto_labels[g->goto_label_count].name = name;
    g->goto_labels[g->goto_label_count].fn = fn;
    g->goto_labels[g->goto_label_count].bb = bb;
    g->goto_label_count++;
    return bb;
}

static void emit_stmt(zan_irgen_t *g, zan_ast_node_t *stmt, local_scope_t *locals) {
    if (!stmt) return;

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
            emit_stmt(g, stmt->block.stmts.items[i], locals);
        }
        if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder))) {
            emit_release_owned_locals_from(g, locals, block_start);
        }
        break;
    }

    case AST_VAR_DECL: {
        /* In an async $resume body, named scalar locals were pre-allocated in
         * the entry block and their storage lives in the heap frame (so they
         * survive suspensions). Reuse that slot instead of a fresh alloca:
         * just evaluate the initializer and store into it. */
        if (g->current_async_frame && g->current_async_slot_count > 0) {
            local_var_t *pre = local_find(locals, stmt->var_decl.name);
            if (pre) {
                zan_type_t *type = stmt->var_decl.type
                    ? zan_binder_resolve_type(g->binder, stmt->var_decl.type)
                    : pre->type;
                for (int i = 0; i < g->current_async_slot_count; i++) {
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
                                LLVMBuildStore(g->builder, iv, pre->alloca);
                            }
                        }
                        if (arc_own) pre->arc_owned = 1;
                        return;
                    }
                }
            }
        }
        zan_type_t *type = g->binder->type_int; /* default */
        if (stmt->var_decl.type) {
            type = zan_binder_resolve_type(g->binder, stmt->var_decl.type);
        } else if (stmt->var_decl.initializer) {
            zan_ast_node_t *init = stmt->var_decl.initializer;

            /* check if initializer is array creation: new int[5] */
            if (init->kind == AST_NEW_EXPR && init->new_expr.is_array) {
                LLVMValueRef arr_val = emit_expr(g, init, locals);
                LLVMTypeRef ptr_type = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                LLVMValueRef alloca = emit_entry_alloca(g, ptr_type, "arr");
                LLVMBuildStore(g->builder, arr_val, alloca);

                zan_type_t *elem_type = zan_binder_resolve_type(g->binder, init->new_expr.type);
                if (!elem_type) elem_type = g->binder->type_int;
                zan_type_t *arr_type = (zan_type_t *)zan_arena_alloc(g->arena, sizeof(zan_type_t));
                arr_type->kind = TYPE_ARRAY;
                arr_type->element_type = elem_type;
                arr_type->name = (zan_istr_t){NULL, 0};
                arr_type->sym = NULL;
                arr_type->type_args = NULL;
                arr_type->type_arg_count = 0;
                local_add(locals, stmt->var_decl.name, alloca, arr_type);
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
                    LLVMBuildStore(g->builder, list_val, alloca);
                    zan_type_t *list_type = zan_binder_resolve_type(g->binder, init->new_expr.type);
                    local_add(locals, stmt->var_decl.name, alloca, list_type);
                    /* List is refcounted: `new` yields an owned (+1) reference, so
                     * this local owns it and must release it at scope exit. */
                    if (list_type && is_rc_managed_type(list_type))
                        locals->vars[locals->count - 1].arc_owned = 1;
                    return;
                }
                if (tname.len == 13 && memcmp(tname.str, "StringBuilder", 13) == 0) {
                    LLVMValueRef sb_val = emit_expr(g, init, locals);
                    LLVMTypeRef ptr_type = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                    LLVMValueRef alloca = emit_entry_alloca(g, ptr_type, "sb");
                    LLVMBuildStore(g->builder, sb_val, alloca);
                    zan_type_t *sb_type = zan_binder_resolve_type(g->binder, init->new_expr.type);
                    local_add(locals, stmt->var_decl.name, alloca, sb_type);
                    /* StringBuilder is refcounted: `new` yields an owned (+1)
                     * reference this local owns and releases at scope exit. */
                    if (sb_type && is_rc_managed_type(sb_type))
                        locals->vars[locals->count - 1].arc_owned = 1;
                    return;
                }
                if ((tname.len == 4 && memcmp(tname.str, "Dict", 4) == 0) ||
                    (tname.len == 10 && memcmp(tname.str, "Dictionary", 10) == 0)) {
                    LLVMValueRef dict_val = emit_expr(g, init, locals);
                    LLVMTypeRef ptr_type = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                    LLVMValueRef alloca = emit_entry_alloca(g, ptr_type, "dict");
                    LLVMBuildStore(g->builder, dict_val, alloca);
                    zan_type_t *dict_type = zan_binder_resolve_type(g->binder, init->new_expr.type);
                    local_add(locals, stmt->var_decl.name, alloca, dict_type);
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
                            LLVMBuildStore(g->builder, LLVMConstNull(st), alloca);

                            /* try calling constructor */
                            bool ctor_called = false;
                            for (int ci = 0; ci < g->ctor_count; ci++) {
                                if (g->ctors[ci].type_sym == sym &&
                                    g->ctors[ci].param_count == init->new_expr.args.count) {
                                    int argc = init->new_expr.args.count + 1;
                                    LLVMValueRef *call_args = (LLVMValueRef *)calloc((size_t)argc, sizeof(LLVMValueRef));
                                    call_args[0] = alloca;
                                    for (int k = 0; k < init->new_expr.args.count; k++) {
                                        call_args[k + 1] = emit_expr(g, init->new_expr.args.items[k], locals);
                                    }
                                    zan_call2(g->builder, g->ctors[ci].fn_type,
                                        g->ctors[ci].fn, call_args, (unsigned)argc, "");
                                    free(call_args);
                                    ctor_called = true;
                                    break;
                                }
                            }

                            /* if no constructor, handle field initializers */
                            if (!ctor_called) {
                                for (int i = 0; i < init->new_expr.args.count; i++) {
                                    zan_ast_node_t *arg = init->new_expr.args.items[i];
                                    if (arg->kind == AST_ASSIGNMENT && arg->binary.left->kind == AST_IDENTIFIER) {
                                        int fi = get_field_index(sym, arg->binary.left->ident.name);
                                        if (fi >= 0) {
                                            LLVMValueRef fptr = LLVMBuildStructGEP2(g->builder, st, alloca, (unsigned)fi, "finit");
                                            LLVMValueRef fval = emit_expr(g, arg->binary.right, locals);
                                            zan_symbol_t *fsym = get_field_sym(sym, arg->binary.left->ident.name);
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
                                            LLVMBuildStore(g->builder, fval, fptr);
                                        }
                                    }
                                }
                            }
                            local_add(locals, stmt->var_decl.name, alloca, type);
                            return;
                        }
                    }
                }
            }

            /* regular type inference from initializer */
            LLVMValueRef init_val = emit_expr(g, init, locals);
            LLVMTypeRef init_type = LLVMTypeOf(init_val);
            LLVMValueRef alloca = emit_entry_alloca(g, init_type, "var");
            LLVMBuildStore(g->builder, init_val, alloca);
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
            } else {
                type = g->binder->type_int;
            }
            if (type && type->kind == TYPE_STRING) {
                LLVMTypeRef llvm_string = map_type(g, type);
                LLVMValueRef slot = emit_entry_alloca(g, llvm_string, "var");
                LLVMBuildStore(g->builder, LLVMConstNull(llvm_string), slot);
                emit_rc_capture_local(g, type, slot, init_val, init, locals);
                local_add(locals, stmt->var_decl.name, slot, type);
                locals->vars[locals->count - 1].arc_owned = 1;
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
        if (arc_own) LLVMBuildStore(g->builder, LLVMConstNull(llvm_type), alloca);

        if (stmt->var_decl.initializer) {
            LLVMValueRef init_val =
                (type && type->kind == TYPE_DELEGATE &&
                 stmt->var_decl.initializer->kind == AST_LAMBDA)
                    ? emit_lambda_typed(g, stmt->var_decl.initializer, type, locals)
                    : emit_expr(g, stmt->var_decl.initializer, locals);
            if (arc_own) {
                emit_rc_capture_local(g, type, alloca, init_val, stmt->var_decl.initializer, locals);
            } else {
                init_val = coerce_int_to(g, init_val, llvm_type);
                LLVMBuildStore(g->builder, init_val, alloca);
            }
        }

        local_add(locals, stmt->var_decl.name, alloca, type);
        if (arc_own) locals->vars[locals->count - 1].arc_owned = 1;
        /* rc-element array from `new T[n]`: remember the element count so its
         * elements can be released at scope exit (arrays have no length header). */
        if (type && type->kind == TYPE_ARRAY && type->element_type &&
            is_rc_managed_type(type->element_type) &&
            !g->current_async_frame &&
            stmt->var_decl.initializer &&
            stmt->var_decl.initializer->kind == AST_NEW_EXPR &&
            stmt->var_decl.initializer->new_expr.is_array &&
            stmt->var_decl.initializer->new_expr.args.count > 0 &&
            !rc_array_local_escapes(g, stmt->var_decl.name)) {
            zan_ast_node_t *sz = stmt->var_decl.initializer->new_expr.args.items[0];
            if (sz->kind == AST_INT_LITERAL || sz->kind == AST_IDENTIFIER) {
                LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
                LLVMValueRef szv = emit_expr(g, sz, locals);
                if (LLVMGetTypeKind(LLVMTypeOf(szv)) == LLVMIntegerTypeKind &&
                    LLVMGetIntTypeWidth(LLVMTypeOf(szv)) < 64)
                    szv = LLVMBuildSExt(g->builder, szv, i64t, "arr.len");
                LLVMValueRef lenslot = emit_entry_alloca(g, i64t, "arr.lenslot");
                LLVMBuildStore(g->builder, szv, lenslot);
                locals->vars[locals->count - 1].arr_len = lenslot;
            }
        }
        /* any `new T[n]` initializer: capture the element count so `a.Length`
         * reads it (the raw buffer has no length header; strlen over binary
         * data returned garbage). Size limited to side-effect-free exprs. */
        if (type && type->kind == TYPE_ARRAY &&
            stmt->var_decl.initializer &&
            stmt->var_decl.initializer->kind == AST_NEW_EXPR &&
            stmt->var_decl.initializer->new_expr.is_array &&
            stmt->var_decl.initializer->new_expr.args.count > 0) {
            zan_ast_node_t *sz = stmt->var_decl.initializer->new_expr.args.items[0];
            if (sz->kind == AST_INT_LITERAL || sz->kind == AST_IDENTIFIER) {
                LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
                LLVMValueRef szv = emit_expr(g, sz, locals);
                if (LLVMGetTypeKind(LLVMTypeOf(szv)) == LLVMIntegerTypeKind &&
                    LLVMGetIntTypeWidth(LLVMTypeOf(szv)) < 64)
                    szv = LLVMBuildSExt(g->builder, szv, i64t, "arr.len");
                LLVMValueRef lslot = emit_entry_alloca(g, i64t, "arr.lenslot2");
                LLVMBuildStore(g->builder, szv, lslot);
                locals->vars[locals->count - 1].arr_len_slot = lslot;
            }
        }
        break;
    }

    case AST_EXPR_STMT: {
        zan_ast_node_t *e = stmt->expr_stmt.expr;
        LLVMValueRef ev = emit_expr(g, e, locals);
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
        /* Inside an async $resume body, `return e` completes the state machine:
         * store the result into the frame, mark it done, and `ret void` — the
         * frame pointer (Task) was already handed out by the ramp. (The awaiter
         * wake is added in S3 with the await protocol.) */
        if (g->current_async_frame) {
            LLVMValueRef ri = NULL;
            if (stmt->ret.value) {
                LLVMValueRef rv = emit_expr(g, stmt->ret.value, locals);
                zan_type_t *ret_type = infer_expr_type(g, stmt->ret.value, locals);
                if (is_rc_managed_type(ret_type) &&
                    !expr_yields_owned_rc_value(g, stmt->ret.value, locals)) {
                    emit_rc_retain_for_type(g, ret_type, rv);
                }
                ri = coerce_to_i64(g, rv);
            }
            emit_async_complete(g, locals, ri);
            break;
        }
        if (stmt->ret.value) {
            LLVMValueRef val = emit_expr(g, stmt->ret.value, locals);
            /* ARC: hand the caller an owned (+1) reference, then release our
             * owning locals. Retaining a borrowed return value first keeps it
             * alive when it aliases a local about to be released. */
            zan_type_t *ret_type = infer_expr_type(g, stmt->ret.value, locals);
            if (is_rc_managed_type(ret_type) &&
                !expr_yields_owned_rc_value(g, stmt->ret.value, locals)) {
                emit_rc_retain_for_type(g, ret_type, val);
            }
            emit_release_owned_locals(g, locals);
            /* convert return value to match function return type */
            LLVMTypeRef fn_ret = g->current_fn_ret_type;
            LLVMTypeRef val_t = LLVMTypeOf(val);
            if (val_t != fn_ret) {
                if (LLVMGetTypeKind(fn_ret) == LLVMFloatTypeKind &&
                    LLVMGetTypeKind(val_t) == LLVMDoubleTypeKind) {
                    val = LLVMBuildFPTrunc(g->builder, val, fn_ret, "rettrunc");
                } else if (LLVMGetTypeKind(fn_ret) == LLVMDoubleTypeKind &&
                           LLVMGetTypeKind(val_t) == LLVMFloatTypeKind) {
                    val = LLVMBuildFPExt(g->builder, val, fn_ret, "retext");
                } else if (LLVMGetTypeKind(fn_ret) == LLVMIntegerTypeKind &&
                           LLVMGetTypeKind(val_t) == LLVMIntegerTypeKind) {
                    unsigned fn_bits = LLVMGetIntTypeWidth(fn_ret);
                    unsigned val_bits = LLVMGetIntTypeWidth(val_t);
                    if (fn_bits > val_bits) {
                        val = LLVMBuildSExt(g->builder, val, fn_ret, "retext");
                    } else if (fn_bits < val_bits) {
                        val = LLVMBuildTrunc(g->builder, val, fn_ret, "rettrunc");
                    }
                } else if (LLVMGetTypeKind(fn_ret) == LLVMPointerTypeKind &&
                           LLVMGetTypeKind(val_t) == LLVMPointerTypeKind) {
                    /* e.g. `return null` (i8*) from a method returning a
                     * concrete class pointer, or vice versa */
                    val = LLVMBuildBitCast(g->builder, val, fn_ret, "retcast");
                }
            }
            LLVMBuildRet(g->builder, val);
        } else {
            emit_release_owned_locals(g, locals);
            /* A bare `return;` normally maps to `ret void`. The program entry
             * `Main` is lowered to an LLVM `i32 main`, though, so a bare return
             * there must yield an exit code to match the function's return type
             * (mirrors the implicit end-of-main `ret i32 0`). */
            LLVMTypeRef fn_ret = g->current_fn_ret_type;
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
        if (LLVMGetTypeKind(LLVMTypeOf(cond)) != LLVMIntegerTypeKind ||
            LLVMGetIntTypeWidth(LLVMTypeOf(cond)) != 1) {
            cond = LLVMBuildICmp(g->builder, LLVMIntNE, cond,
                                 LLVMConstInt(LLVMTypeOf(cond), 0, 0), "tobool");
        }

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
        g->break_target = end_bb;
        g->continue_target = cond_bb;
        g->loop_locals_base = body_start;

        LLVMBuildBr(g->builder, cond_bb);
        LLVMPositionBuilderAtEnd(g->builder, cond_bb);
        LLVMValueRef cond = emit_expr(g, stmt->while_stmt.cond, locals);
        if (LLVMGetTypeKind(LLVMTypeOf(cond)) != LLVMIntegerTypeKind ||
            LLVMGetIntTypeWidth(LLVMTypeOf(cond)) != 1) {
            cond = LLVMBuildICmp(g->builder, LLVMIntNE, cond,
                                 LLVMConstInt(LLVMTypeOf(cond), 0, 0), "tobool");
        }
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
        g->break_target = end_bb;
        g->continue_target = step_bb;
        g->loop_locals_base = for_body_start;

        LLVMBuildBr(g->builder, cond_bb);
        LLVMPositionBuilderAtEnd(g->builder, cond_bb);
        if (stmt->for_stmt.cond) {
            LLVMValueRef cond = emit_expr(g, stmt->for_stmt.cond, locals);
            if (LLVMGetTypeKind(LLVMTypeOf(cond)) != LLVMIntegerTypeKind ||
                LLVMGetIntTypeWidth(LLVMTypeOf(cond)) != 1) {
                cond = LLVMBuildICmp(g->builder, LLVMIntNE, cond,
                                     LLVMConstInt(LLVMTypeOf(cond), 0, 0), "tobool");
            }
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

        LLVMPositionBuilderAtEnd(g->builder, end_bb);
        /* Drop the loop variables (and any owned init-clause locals) now that
         * the loop has fully exited. */
        emit_release_owned_locals_from(g, locals, for_start);
        break;
    }

    case AST_BREAK_STMT:
        if (g->break_target) {
            emit_release_owned_locals_range(g, locals, g->loop_locals_base);
            LLVMBuildBr(g->builder, g->break_target);
        }
        break;

    case AST_CONTINUE_STMT:
        if (g->continue_target) {
            emit_release_owned_locals_range(g, locals, g->loop_locals_base);
            LLVMBuildBr(g->builder, g->continue_target);
        }
        break;

    case AST_SWITCH_STMT: {
        int switch_start = locals->count;
        LLVMValueRef switch_val = emit_expr(g, stmt->switch_stmt.expr, locals);
        LLVMBasicBlockRef end_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "sw.end");

        /* count non-default cases */
        int num_cases = 0;
        zan_ast_node_t *default_case = NULL;
        for (int i = 0; i < stmt->switch_stmt.cases.count; i++) {
            zan_ast_node_t *sc = stmt->switch_stmt.cases.items[i];
            if (sc->switch_case.pattern) {
                num_cases++;
            } else {
                default_case = sc;
            }
        }

        LLVMBasicBlockRef default_bb = default_case
            ? LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "sw.default")
            : end_bb;

        if (is_string_expr(g, stmt->switch_stmt.expr, locals)) {
            /* string switch: strcmp chain (LLVMBuildSwitch requires integers) */
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
            LLVMValueRef strcmp_fn = LLVMGetNamedFunction(g->mod, "strcmp");
            LLVMTypeRef strcmp_ty = LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0);
            if (!strcmp_fn) strcmp_fn = LLVMAddFunction(g->mod, "strcmp", strcmp_ty);

            LLVMBasicBlockRef *case_bbs = (LLVMBasicBlockRef *)calloc(
                (size_t)(num_cases > 0 ? num_cases : 1), sizeof(LLVMBasicBlockRef));
            int ci = 0;
            for (int i = 0; i < stmt->switch_stmt.cases.count; i++) {
                zan_ast_node_t *sc = stmt->switch_stmt.cases.items[i];
                if (!sc->switch_case.pattern) continue;
                LLVMValueRef case_val = emit_expr(g, sc->switch_case.pattern, locals);
                LLVMBasicBlockRef case_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "sw.case");
                LLVMBasicBlockRef next_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "sw.next");
                LLVMValueRef cmp = zan_call2(g->builder, strcmp_ty, strcmp_fn,
                    (LLVMValueRef[]){ switch_val, case_val }, 2, "swcmp");
                LLVMValueRef eq = LLVMBuildICmp(g->builder, LLVMIntEQ, cmp,
                    LLVMConstInt(i32, 0, 0), "sweq");
                LLVMBuildCondBr(g->builder, eq, case_bb, next_bb);
                LLVMPositionBuilderAtEnd(g->builder, next_bb);
                case_bbs[ci++] = case_bb;
            }
            LLVMBuildBr(g->builder, default_bb);

            ci = 0;
            for (int i = 0; i < stmt->switch_stmt.cases.count; i++) {
                zan_ast_node_t *sc = stmt->switch_stmt.cases.items[i];
                if (!sc->switch_case.pattern) continue;
                LLVMPositionBuilderAtEnd(g->builder, case_bbs[ci++]);
                emit_stmt(g, sc->switch_case.body, locals);
                if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder))) {
                    emit_release_owned_locals_from(g, locals, switch_start);
                    LLVMBuildBr(g->builder, end_bb);
                } else {
                    emit_release_owned_locals_from(g, locals, switch_start);
                }
            }
            free(case_bbs);

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

            LLVMPositionBuilderAtEnd(g->builder, end_bb);
            break;
        }

        LLVMValueRef sw = LLVMBuildSwitch(g->builder, switch_val, default_bb, (unsigned)num_cases);

        /* emit each case block */
        for (int i = 0; i < stmt->switch_stmt.cases.count; i++) {
            zan_ast_node_t *sc = stmt->switch_stmt.cases.items[i];
            if (!sc->switch_case.pattern) continue; /* default handled separately */

            LLVMBasicBlockRef case_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "sw.case");
            LLVMValueRef case_val = emit_expr(g, sc->switch_case.pattern, locals);
            /* case label must match the switch operand's integer width */
            coerce_int_pair(g, &switch_val, &case_val);
            LLVMAddCase(sw, case_val, case_bb);

            LLVMPositionBuilderAtEnd(g->builder, case_bb);
            emit_stmt(g, sc->switch_case.body, locals);
            if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder))) {
                emit_release_owned_locals_from(g, locals, switch_start);
                LLVMBuildBr(g->builder, end_bb);
            } else {
                emit_release_owned_locals_from(g, locals, switch_start);
            }
        }

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

        LLVMValueRef eh_tmps_g, eh_tmptop_g;
        get_eh_tmp_globals(g, &eh_tmps_g, &eh_tmptop_g);
        (void)eh_tmps_g;
        LLVMValueRef tmp_mark = LLVMBuildLoad2(g->builder, i32t, eh_tmptop_g, "eh.tmpmark");
        LLVMValueRef old_top = LLVMBuildLoad2(g->builder, i32t, top_g, "eh.old");
        LLVMValueRef new_top = LLVMBuildAdd(g->builder, old_top,
            LLVMConstInt(i32t, 1, 0), "eh.new");
        LLVMBuildStore(g->builder, new_top, top_g);
        LLVMValueRef zero = LLVMConstInt(i32t, 0, 0);
        LLVMValueRef gep_idx[2] = { zero, new_top };
        LLVMTypeRef bufs_ty = LLVMGlobalGetValueType(bufs_g);
        LLVMValueRef buf = LLVMBuildGEP2(g->builder, bufs_ty, bufs_g, gep_idx, 2, "eh.buf");
        LLVMValueRef bufp = LLVMBuildBitCast(g->builder, buf, i8ptr, "eh.bufp");
        LLVMValueRef r = emit_eh_setjmp(g, bufp);
        LLVMValueRef took = LLVMBuildICmp(g->builder, LLVMIntEQ, r, zero, "eh.took");
        LLVMBuildCondBr(g->builder, took, try_bb, catch_bb);

        LLVMPositionBuilderAtEnd(g->builder, try_bb);
        int try_start = locals->count;
        emit_stmt(g, stmt->try_stmt.try_body, locals);
        locals->count = try_start;
        if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder))) {
            LLVMBuildStore(g->builder, old_top, top_g);
            LLVMBuildBr(g->builder, end_bb);
        }

        LLVMPositionBuilderAtEnd(g->builder, catch_bb);
        LLVMBuildStore(g->builder, old_top, top_g);
        /* longjmp skipped the normal releases of temps pushed after this try
         * was entered (a throwing ctor's object, an owned receiver temp) —
         * release them now, restoring the temp stack to the try-entry depth */
        {
            LLVMTypeRef uwty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), &i32t, 1, 0);
            zan_call2(g->builder, uwty, get_eh_tmp_unwind_fn(g), &tmp_mark, 1, "");
        }
        LLVMValueRef exc_val = LLVMBuildLoad2(g->builder, i8ptr, exc_g, "exc");
        int catch_start = locals->count;
        int ncatch = stmt->try_stmt.catches.count;
        if (ncatch > 0) {
            /* type-based dispatch: test each clause in order against the
             * thrown object's type descriptor (walking its base chain); if
             * no clause matches, rethrow to the next outer handler */
            LLVMValueRef tid_g = get_eh_exc_tid_global(g);
            LLVMValueRef thrown_tid = LLVMBuildLoad2(g->builder, i8ptr, tid_g, "exc.tid");
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
                    ? zan_binder_resolve_type(g->binder, cc->catch_clause.type)
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
                    LLVMValueRef ea = emit_entry_alloca(g, i8ptr, "exc.var");
                    LLVMBuildStore(g->builder, exc_val, ea);
                    local_add(locals, cc->catch_clause.var_name, ea, et);
                }
                emit_stmt(g, cc->catch_clause.body, locals);
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
             * in-flight exception; jump to the next outer handler (this try's
             * frame was already popped above) or die if none remains */
            LLVMPositionBuilderAtEnd(g->builder, rethrow_bb);
            {
                LLVMValueRef rtop = LLVMBuildLoad2(g->builder, i32t, top_g, "reh.top");
                LLVMValueRef rhas = LLVMBuildICmp(g->builder, LLVMIntSGE, rtop,
                    LLVMConstInt(i32t, 0, 0), "reh.has");
                LLVMBasicBlockRef rjmp_bb =
                    LLVMAppendBasicBlockInContext(g->ctx, fn, "reh.jmp");
                LLVMBasicBlockRef rdie_bb =
                    LLVMAppendBasicBlockInContext(g->ctx, fn, "reh.die");
                LLVMBuildCondBr(g->builder, rhas, rjmp_bb, rdie_bb);
                LLVMPositionBuilderAtEnd(g->builder, rjmp_bb);
                LLVMValueRef rgep_idx[2] = { LLVMConstInt(i32t, 0, 0), rtop };
                LLVMValueRef rbuf = LLVMBuildGEP2(g->builder,
                    LLVMGlobalGetValueType(bufs_g), bufs_g, rgep_idx, 2, "reh.buf");
                LLVMValueRef rbufp = LLVMBuildBitCast(g->builder, rbuf, i8ptr, "reh.bufp");
                emit_eh_longjmp(g, rbufp);
                LLVMBuildUnreachable(g->builder);
                LLVMPositionBuilderAtEnd(g->builder, rdie_bb);
                LLVMValueRef printf_fn = LLVMGetNamedFunction(g->mod, "printf");
                if (printf_fn) {
                    LLVMTypeRef printf_ty = LLVMFunctionType(i32t, &i8ptr, 1, 1);
                    LLVMValueRef fmt = LLVMBuildGlobalStringPtr(g->builder,
                        "Unhandled exception\n", "rexfmt");
                    zan_call2(g->builder, printf_ty, printf_fn, &fmt, 1, "");
                }
                LLVMTypeRef exit_ty = LLVMFunctionType(
                    LLVMVoidTypeInContext(g->ctx), &i32t, 1, 0);
                LLVMValueRef exit_fn = get_libc_fn(g, "exit", exit_ty);
                LLVMValueRef one = LLVMConstInt(i32t, 1, 0);
                zan_call2(g->builder, exit_ty, exit_fn, &one, 1, "");
                LLVMBuildUnreachable(g->builder);
            }

            LLVMPositionBuilderAtEnd(g->builder, done_bb);
        }
        locals->count = catch_start;
        if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder))) {
            /* drop the throw-site +1 on the caught exception (class-typed
             * throws set __zan_eh_exc_owned; string throws leave it 0) */
            LLVMValueRef owned_g = get_eh_exc_owned_global(g);
            LLVMValueRef ofl = LLVMBuildLoad2(g->builder, i32t, owned_g, "exc.owned");
            LLVMValueRef is_owned = LLVMBuildICmp(g->builder, LLVMIntNE, ofl,
                LLVMConstInt(i32t, 0, 0), "exc.isown");
            LLVMValueRef cfn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder));
            LLVMBasicBlockRef rel_bb = LLVMAppendBasicBlockInContext(g->ctx, cfn, "exc.rel");
            LLVMBasicBlockRef cont_bb = LLVMAppendBasicBlockInContext(g->ctx, cfn, "exc.cont");
            LLVMBuildCondBr(g->builder, is_owned, rel_bb, cont_bb);
            LLVMPositionBuilderAtEnd(g->builder, rel_bb);
            zan_call2(g->builder,
                LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), &i8ptr, 1, 0),
                g->rt_release_dyn, &exc_val, 1, "");
            LLVMBuildStore(g->builder, LLVMConstInt(i32t, 0, 0), owned_g);
            LLVMBuildStore(g->builder, LLVMConstNull(i8ptr), exc_g);
            LLVMBuildBr(g->builder, cont_bb);
            LLVMPositionBuilderAtEnd(g->builder, cont_bb);
            LLVMBuildBr(g->builder, end_bb);
        }

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
        if (LLVMTypeOf(cond) != LLVMInt1TypeInContext(g->ctx)) {
            cond = LLVMBuildICmp(g->builder, LLVMIntNE, cond,
                                 LLVMConstInt(LLVMTypeOf(cond), 0, 0), "dcond");
        }
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
            elem_type = zan_binder_resolve_type(g->binder, stmt->foreach_stmt.var_type);
        if (!elem_type || elem_type->kind == TYPE_ERROR)
            elem_type = container_elem_type(
                infer_expr_type(g, stmt->foreach_stmt.collection, locals));
        if (!elem_type) elem_type = g->binder->type_int;
        LLVMTypeRef elem_llvm = map_type(g, elem_type);

        /* get count: field 0 of List struct */
        LLVMValueRef cnt_ptr = LLVMBuildStructGEP2(g->builder, g->list_struct_type,
            collection, 0, "cnt_ptr");
        LLVMValueRef count = LLVMBuildLoad2(g->builder, i64, cnt_ptr, "cnt");

        /* get data pointer: field 2 of List struct */
        LLVMValueRef data_ptr = LLVMBuildStructGEP2(g->builder, g->list_struct_type,
            collection, 2, "data_ptr");
        LLVMValueRef data = LLVMBuildLoad2(g->builder, LLVMPointerType(i64, 0),
            data_ptr, "data");

        /* index variable */
        LLVMValueRef idx_alloc = emit_entry_alloca(g, i64, "fi");
        LLVMBuildStore(g->builder, LLVMConstInt(i64, 0, 0), idx_alloc);

        /* iteration variable */
        LLVMValueRef iter_alloc = emit_entry_alloca(g, elem_llvm, "fv");
        local_add(locals, stmt->foreach_stmt.var_name, iter_alloc, elem_type);

        LLVMBasicBlockRef cond_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "fe.cond");
        LLVMBasicBlockRef body_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "fe.body");
        LLVMBasicBlockRef end_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "fe.end");

        LLVMBuildBr(g->builder, cond_bb);
        LLVMPositionBuilderAtEnd(g->builder, cond_bb);
        LLVMValueRef idx_val = LLVMBuildLoad2(g->builder, i64, idx_alloc, "i");
        LLVMValueRef cmp = LLVMBuildICmp(g->builder, LLVMIntSLT, idx_val, count, "fcmp");
        LLVMBuildCondBr(g->builder, cmp, body_bb, end_bb);

        LLVMPositionBuilderAtEnd(g->builder, body_bb);
        /* load current element; slots physically hold an i64, so pointer
         * (class/string) elements need inttoptr, doubles a bitcast, and
         * narrower integers a trunc back to the value type */
        LLVMValueRef elem_ptr = LLVMBuildGEP2(g->builder, i64, data, &idx_val, 1, "ep");
        LLVMValueRef elem = LLVMBuildLoad2(g->builder, i64, elem_ptr, "elem");
        LLVMTypeKind ek = LLVMGetTypeKind(elem_llvm);
        if (ek == LLVMPointerTypeKind)
            elem = LLVMBuildIntToPtr(g->builder, elem, elem_llvm, "elp");
        else if (ek == LLVMDoubleTypeKind)
            elem = LLVMBuildBitCast(g->builder, elem, elem_llvm, "elf");
        else if (ek == LLVMIntegerTypeKind && LLVMGetIntTypeWidth(elem_llvm) < 64)
            elem = LLVMBuildTrunc(g->builder, elem, elem_llvm, "elt");
        LLVMBuildStore(g->builder, elem, iter_alloc);

        emit_stmt(g, stmt->foreach_stmt.body, locals);

        if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder))) {
            /* increment index */
            emit_release_owned_locals_from(g, locals, fe_start);
            LLVMValueRef next = LLVMBuildAdd(g->builder,
                LLVMBuildLoad2(g->builder, i64, idx_alloc, "i2"),
                LLVMConstInt(i64, 1, 0), "next");
            LLVMBuildStore(g->builder, next, idx_alloc);
            LLVMBuildBr(g->builder, cond_bb);
        } else {
            emit_release_owned_locals_from(g, locals, fe_start);
        }

        LLVMPositionBuilderAtEnd(g->builder, end_bb);
        /* an owned temporary collection (e.g. iterating a call result) is
         * consumed by the loop and released once iteration ends */
        emit_release_owned_call_temp(g, stmt->foreach_stmt.collection,
                                     collection, locals);
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
        zan_call2(g->builder, mon_ty, enter_fn, &obj, 1, "");
        emit_stmt(g, stmt->lock_stmt.body, locals);
        if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder)))
            zan_call2(g->builder, mon_ty, exit_fn, &obj, 1, "");
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
         * enclosing try (if any); otherwise print and exit(1). */
        LLVMValueRef val = emit_expr(g, stmt->throw_stmt.value, locals);
        LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
        {
            LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
            LLVMValueRef top_g, bufs_g, exc_g;
            get_eh_globals(g, &top_g, &bufs_g, &exc_g);
            LLVMValueRef vail = val;
            if (LLVMGetTypeKind(LLVMTypeOf(vail)) != LLVMPointerTypeKind)
                vail = LLVMConstNull(i8ptr);
            else if (LLVMTypeOf(vail) != i8ptr)
                vail = LLVMBuildBitCast(g->builder, vail, i8ptr, "exc.bc");
            LLVMBuildStore(g->builder, vail, exc_g);
            /* rc convention: for class-typed throws __zan_eh_exc carries a +1
             * reference (retain borrowed values here); the catch releases it
             * once the handler completes */
            {
                LLVMValueRef owned_g = get_eh_exc_owned_global(g);
                LLVMValueRef tid_g = get_eh_exc_tid_global(g);
                zan_type_t *tt = infer_expr_type(g, stmt->throw_stmt.value, locals);
                if (tt && tt->kind == TYPE_CLASS &&
                    LLVMGetTypeKind(LLVMTypeOf(val)) == LLVMPointerTypeKind) {
                    if (!expr_yields_owned_rc_value(g, stmt->throw_stmt.value, locals))
                        emit_arc_retain(g, vail);
                    LLVMBuildStore(g->builder, LLVMConstInt(i32t, 1, 0), owned_g);
                    /* record the thrown class's type descriptor so catch
                     * clauses can dispatch by type */
                    if (tt->sym) {
                        LLVMValueRef tid = get_class_tid_global(g, tt->sym);
                        LLVMBuildStore(g->builder,
                            LLVMBuildBitCast(g->builder, tid, i8ptr, "tid.bc"),
                            tid_g);
                    } else {
                        LLVMBuildStore(g->builder, LLVMConstNull(i8ptr), tid_g);
                    }
                } else {
                    LLVMBuildStore(g->builder, LLVMConstInt(i32t, 0, 0), owned_g);
                    LLVMBuildStore(g->builder, LLVMConstNull(i8ptr), tid_g);
                }
            }
            LLVMValueRef top = LLVMBuildLoad2(g->builder, i32t, top_g, "eh.top");
            LLVMValueRef has = LLVMBuildICmp(g->builder, LLVMIntSGE, top,
                LLVMConstInt(i32t, 0, 0), "eh.has");
            LLVMValueRef fn2 = LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder));
            LLVMBasicBlockRef jmp_bb = LLVMAppendBasicBlockInContext(g->ctx, fn2, "throw.jmp");
            LLVMBasicBlockRef die_bb = LLVMAppendBasicBlockInContext(g->ctx, fn2, "throw.die");
            LLVMBuildCondBr(g->builder, has, jmp_bb, die_bb);
            LLVMPositionBuilderAtEnd(g->builder, jmp_bb);
            LLVMValueRef gep_idx[2] = { LLVMConstInt(i32t, 0, 0), top };
            LLVMValueRef buf = LLVMBuildGEP2(g->builder,
                LLVMGlobalGetValueType(bufs_g), bufs_g, gep_idx, 2, "eh.buf");
            LLVMValueRef bufp = LLVMBuildBitCast(g->builder, buf, i8ptr, "eh.bufp");
            emit_eh_longjmp(g, bufp);
            LLVMBuildUnreachable(g->builder);
            LLVMPositionBuilderAtEnd(g->builder, die_bb);
        }
        LLVMValueRef printf_fn = LLVMGetNamedFunction(g->mod, "printf");
        if (printf_fn) {
            LLVMTypeRef printf_ty = LLVMFunctionType(LLVMInt32TypeInContext(g->ctx),
                &i8ptr, 1, 1);
            if (LLVMTypeOf(val) == i8ptr) {
                LLVMValueRef fmt = LLVMBuildGlobalStringPtr(g->builder,
                    "Unhandled exception: %s\n", "exfmt");
                LLVMValueRef args[] = { fmt, val };
                zan_call2(g->builder, printf_ty, printf_fn, args, 2, "");
            } else {
                LLVMValueRef fmt = LLVMBuildGlobalStringPtr(g->builder,
                    "Unhandled exception\n", "exfmt");
                LLVMValueRef args[] = { fmt };
                zan_call2(g->builder, printf_ty, printf_fn, args, 1, "");
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
 * report (which is scheduled via atexit and therefore fires afterwards). */
static void emit_release_static_rc_fields(zan_irgen_t *g, zan_ast_node_t *unit) {
    if (!unit || unit->kind != AST_COMPILATION_UNIT) return;
    zan_symbol_t *saved_type = g->current_type_sym;
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
            zan_symbol_t *fs = get_field_sym(csym, m->field_decl.name);
            if (!fs || !fs->type || !is_rc_managed_type(fs->type)) continue;
            LLVMValueRef gv = get_static_field_global(g, csym, fs);
            if (!gv) continue;
            LLVMTypeRef ft = map_type(g, fs->type);
            LLVMValueRef old = LLVMBuildLoad2(g->builder, ft, gv, "sf.rel");
            emit_rc_release_for_type(g, fs->type, old);
            LLVMBuildStore(g->builder, LLVMConstNull(ft), gv);
        }
    }
    g->current_type_sym = saved_type;
}
