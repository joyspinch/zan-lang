/* irgen_call.c -- the call-expression emitter: builtin/intrinsic dispatch,
 * overload resolution, receiver handling and user-method calls.
 *
 * Part of the irgen translation unit: #include'd by irgen.c after
 * irgen_expr.c; not compiled standalone.
 */

/* Detach an async call nobody awaits: `sub` is the frame its ramp returned.
 * Install the reaper as the frame's own awaiter step (awaiter = the frame
 * itself, a non-null marker) so emit_async_complete's awaiter-wake path
 * re-enqueues (frame, __zan_co_reap) and the driver frees the frame once the
 * body finishes; then schedule the frame and track it, so the driver can tell
 * a live frame from a reaped one before Task.Cancel writes through it.
 *
 * `keep_result` skips the reaper for a frame whose result is still to be read
 * (Task.Run of a value-returning method): Result/Wait reaps it instead.
 *
 * Returns the frame as i8*, or NULL when `sub` is not an async ramp result --
 * the caller then lowers the expression as an ordinary call. Shared by
 * Task.Spawn/Task.Run and by a discarded async call statement, which is a
 * spawn in every respect (see AST_EXPR_STMT in irgen_stmt.c). */
static LLVMValueRef emit_detach_async_call(zan_irgen_t *g, LLVMValueRef sub,
                                           bool keep_result) {
    if (!sub || LLVMGetTypeKind(LLVMTypeOf(sub)) != LLVMPointerTypeKind ||
        !LLVMIsACallInst(sub))
        return NULL;
    LLVMValueRef callee = LLVMGetCalledValue(sub);
    if (!callee) return NULL;
    size_t nl = 0;
    const char *cn = LLVMGetValueName2(callee, &nl);
    if (!cn || nl == 0 || nl >= 240) return NULL;
    char rn[256];
    memcpy(rn, cn, nl);
    memcpy(rn + nl, "$resume", 8); /* includes NUL */
    LLVMValueRef sub_resume = LLVMGetNamedFunction(g->mod, rn);
    if (!sub_resume) return NULL;

    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMValueRef sub_i8 = LLVMBuildBitCast(g->builder, sub, i8ptr, "spawn.sub");
    if (!keep_result) {
        LLVMTypeRef hdr = g->co_header_type;
        LLVMBuildStore(g->builder, sub_i8,
            LLVMBuildStructGEP2(g->builder, hdr, sub_i8, ASYNC_FRAME_AWAITER, "spawn.aw"));
        LLVMBuildStore(g->builder, get_co_reap_fn(g),
            LLVMBuildStructGEP2(g->builder, hdr, sub_i8, ASYNC_FRAME_AWAITER_STEP, "spawn.aws"));
    }
    LLVMValueRef sched_args[] = { sub_i8, sub_resume };
    zan_call2(g->builder, g->rt_co_ready_type, g->rt_co_ready, sched_args, 2, "");
    LLVMValueRef track = get_co_track_fn(g);
    zan_call2(g->builder, LLVMGlobalGetValueType(track), track, &sub_i8, 1, "");
    return sub_i8;
}

/* True when the identifier names a field of the class being compiled -- an
 * instance field (it reads as `this.<name>`) or a static one. Such a field
 * shadows a type of the same name wherever a receiver is resolved. Statics
 * count too, and in a static method they are the only fields in reach: a
 * designed form holds its controls in statics, so a control named after a
 * widget class (`static DataTable DataGrid;`) used to resolve to the type and
 * the call was emitted against the wrong (instance) signature -- invalid IR
 * rather than a diagnostic. */
static bool ident_names_own_field(zan_irgen_t *g, zan_ast_node_t *e) {
    if (!e || e->kind != AST_IDENTIFIER) return false;
    if (!g->current_type_sym) return false;
    if (g->current_this &&
        get_field_index(g->current_type_sym, e->ident.name) >= 0)
        return true;
    zan_symbol_t *fs = get_field_sym(g->current_type_sym, e->ident.name);
    return fs && fs->kind == SYM_FIELD && (fs->modifiers & MOD_STATIC) != 0;
}

/* True when a type still mentions an unbound type parameter (`T`, `List<T>`,
 * `T[]`), or is the error type. Such a type has no resolvable members, so a
 * call through it cannot be lowered until monomorphization binds it. */
static bool type_mentions_type_param(zan_type_t *t, int depth) {
    if (!t || depth > 8) return false;
    if (t->kind == TYPE_TYPE_PARAM || t->kind == TYPE_ERROR) return true;
    if (t->element_type && type_mentions_type_param(t->element_type, depth + 1))
        return true;
    for (int i = 0; i < t->type_arg_count; i++)
        if (type_mentions_type_param(t->type_args[i], depth + 1)) return true;
    return false;
}

/* Whether an unclaimed call sits in the erased body of a generic, reaching
 * through a receiver whose type is still open. Distinguishes "the code
 * generator failed to resolve a real call" from "this copy of the body is the
 * erased template and the real code lives in the specializations". */
static bool call_receiver_is_open_generic(zan_irgen_t *g, zan_ast_node_t *call,
                                         local_scope_t *locals) {
    zan_ast_node_t *callee = call->call.callee;
    if (!callee) return false;
    if (callee->kind == AST_MEMBER_ACCESS)
        return type_mentions_type_param(
            infer_expr_type(g, callee->member.object, locals), 0);
    /* A bare-name call inside a generic: an argument typed by a type parameter
     * is what keeps overload resolution from picking a target. */
    for (int i = 0; i < call->call.args.count; i++)
        if (type_mentions_type_param(
                infer_expr_type(g, call->call.args.items[i], locals), 0))
            return true;
    return false;
}

/* Walks the receiver chain of an unclaimed call and reports the first
 * `Type.Method(...)` link naming a class that has no such method. A chained
 * call hides the broken link from the per-call checks: the outer call resolves
 * nothing, so it never emits its receiver and the missing method went
 * unreported. */
static void diagnose_unresolved_static_chain(zan_irgen_t *g,
                                             zan_ast_node_t *recv) {
    while (recv && recv->kind == AST_CALL) {
        zan_ast_node_t *callee = recv->call.callee;
        if (!callee || callee->kind != AST_MEMBER_ACCESS) return;
        zan_ast_node_t *obj = callee->member.object;
        if (obj->kind == AST_IDENTIFIER && !ident_names_own_field(g, obj)) {
            zan_symbol_t *ts = zan_binder_lookup(g->binder, obj->ident.name);
            if (ts && (ts->kind == SYM_CLASS || ts->kind == SYM_STRUCT) &&
                !get_method_sym(ts, callee->member.name)) {
                zan_diag_emit(g->diag, DIAG_ERROR, recv->loc,
                    "unresolved call '%.*s.%.*s': type '%.*s' has no method "
                    "'%.*s'",
                    (int)obj->ident.name.len, obj->ident.name.str,
                    (int)callee->member.name.len, callee->member.name.str,
                    (int)obj->ident.name.len, obj->ident.name.str,
                    (int)callee->member.name.len, callee->member.name.str);
                return;
            }
        }
        recv = obj;
    }
}

/* Formats a name path (`Foo.Bar.Baz`) into `buf` as a dotted string for
 * diagnostics. Returns the length written. Non-path nodes write nothing. */
static int format_name_path(zan_ast_node_t *n, char *buf, int cap) {
    if (!n || cap <= 0) return 0;
    if (n->kind == AST_IDENTIFIER) {
        int l = (int)n->ident.name.len;
        if (l > cap) l = cap;
        memcpy(buf, n->ident.name.str, (size_t)l);
        return l;
    }
    if (n->kind == AST_MEMBER_ACCESS) {
        int off = format_name_path(n->member.object, buf, cap);
        if (off > 0 && off < cap) {
            buf[off++] = '.';
            int l = (int)n->member.name.len;
            if (l > cap - off) l = cap - off;
            memcpy(buf + off, n->member.name.str, (size_t)l);
            off += l;
        }
        return off;
    }
    return 0;
}

/* True when the call's receiver names a class compiled from source (user or
 * stdlib) that defines the called method. Builtin lowerings that duplicate a
 * stdlib class (File.*) step aside so the source implementation — with its
 * richer semantics such as thrown exceptions — wins whenever it is present
 * (e.g. under --auto-stdlib). */
static bool src_method_takes_over(zan_irgen_t *g, zan_ast_node_t *expr,
        local_scope_t *locals) {
    zan_ast_node_t *callee = expr->call.callee;
    if (callee->kind != AST_MEMBER_ACCESS ||
        callee->member.object->kind != AST_IDENTIFIER) return false;
    if (local_find(locals, callee->member.object->ident.name)) return false;
    if (ident_names_own_field(g, callee->member.object)) return false;
    zan_symbol_t *ts = zan_binder_lookup(g->binder, callee->member.object->ident.name);
    if (!ts || (ts->kind != SYM_CLASS && ts->kind != SYM_STRUCT)) return false;
    return get_method_sym(ts, callee->member.name) != NULL;
}

/* Try to route a call to a generic method through a monomorphized copy (see
 * get_or_create_method_spec in irgen_emit.c): every type parameter must be
 * inferable to a concrete type at this call site. Returns the spec index or
 * -1 to fall back to the erased call. */
static int try_method_spec(zan_irgen_t *g, zan_symbol_t *msym,
                           zan_ast_node_t *call, zan_ast_node_t *recv_expr,
                           local_scope_t *locals) {
    if (!msym || !msym->decl || msym->decl->kind != AST_METHOD_DECL) return -1;
    zan_ast_list_t *tps = &msym->decl->method_decl.type_params;
    if (tps->count == 0 || tps->count > 8) return -1;
    /* An instance method's receiver is not a declared parameter: it becomes
     * the implicit `this` argument (an extension method's `this T` receiver,
     * by contrast, IS declared parameter 0 of a static method). */
    bool m_static = (msym->decl->method_decl.modifiers & MOD_STATIC) != 0;
    int recv_params = (recv_expr && m_static) ? 1 : 0;
    /* exact arity only: `params` packing / defaults keep the erased path */
    if (msym->decl->method_decl.params.count !=
        call->call.args.count + recv_params) return -1;
    zan_type_t *owner_inst = NULL;
    if (!m_static) {
        /* the receiver has to be a reference the call site can hand over as a
         * plain pointer; struct receivers need their storage address, which
         * this path does not model */
        zan_type_t *rt = recv_expr ? infer_expr_type(g, recv_expr, locals)
                                   : (g->current_type_sym ? g->current_type_sym->type
                                                          : NULL);
        if (!rt || rt->kind != TYPE_CLASS) return -1;
        if (!recv_expr && !g->current_this) return -1;
        /* `Pool<string>.Wrap<int>` specializes per class instantiation too;
         * inside a generic body the receiver's own instantiation is the one
         * being emitted (`this.Wrap<int>()`). */
        if (rt->type_arg_count > 0) owner_inst = rt;
        else if (g->cur_inst && (!rt->sym || rt->sym == g->cur_inst->sym))
            owner_inst = g->cur_inst;
    }
    for (int j = 0; j < msym->decl->method_decl.params.count; j++)
        if (msym->decl->method_decl.params.items[j]->param.by_ref) return -1;
    zan_type_t *bind[8] = { NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL };
    infer_method_tp_bindings(g, msym, call, recv_expr, locals, bind);
    return get_or_create_method_spec(g, msym, bind, tps->count, owner_inst);
}

/* Emit a call to a monomorphized generic method: arguments are emitted
 * against the substituted (concrete) parameter types, so no boundary erasure
 * or borrowed-return fixups apply — the result is owned like any call. */
static LLVMValueRef emit_method_spec_call(zan_irgen_t *g, int idx,
                                          zan_ast_node_t *call,
                                          zan_ast_node_t *recv_expr,
                                          local_scope_t *locals) {
    /* by value: emitting an argument can create another specialization, which
     * reallocates g->method_specs and would dangle a pointer into it */
    struct zan_method_spec sp = g->method_specs[idx];
    zan_ast_list_t *tps = &sp.msym->decl->method_decl.type_params;
    int pcount = sp.msym->decl->method_decl.params.count;
    bool m_static = (sp.msym->decl->method_decl.modifiers & MOD_STATIC) != 0;
    int this_off = m_static ? 0 : 1;
    int arg_base = (recv_expr && m_static) ? 1 : 0;
    int total = pcount + this_off;
    LLVMValueRef *call_args = (LLVMValueRef *)calloc(
        (size_t)(total > 0 ? total : 1), sizeof(LLVMValueRef));
    zan_ast_node_t **aexprs = (zan_ast_node_t **)calloc(
        (size_t)(total > 0 ? total : 1), sizeof(zan_ast_node_t *));
    int *arg_eh_pushed = (int *)calloc(
        (size_t)(total > 0 ? total : 1), sizeof(int));
    if (this_off) {
        LLVMTypeRef this_ty = LLVMTypeOf(LLVMGetParam(sp.fn, 0));
        LLVMValueRef self;
        if (recv_expr) {
            self = emit_expr(g, recv_expr, locals);
        } else {
            self = LLVMBuildLoad2(g->builder,
                LLVMGetAllocatedType(g->current_this), g->current_this, "self");
        }
        if (LLVMTypeOf(self) != this_ty)
            self = LLVMBuildBitCast(g->builder, self, this_ty, "self.c");
        call_args[0] = self;
        aexprs[0] = recv_expr;
        zan_type_t *rt = recv_expr ? infer_expr_type(g, recv_expr, locals) : NULL;
        if (rt && is_rc_managed_type(rt) &&
            !expr_is_local_ident(recv_expr, locals) &&
            expr_yields_owned_rc_value(g, recv_expr, locals) &&
            LLVMGetTypeKind(LLVMTypeOf(self)) == LLVMPointerTypeKind) {
            emit_eh_tmp_push(g, self);
            arg_eh_pushed[0] = 1;
        }
    }
    for (int j = 0; j < pcount; j++) {
        aexprs[j + this_off] = (arg_base == 1 && j == 0)
            ? recv_expr : call->call.args.items[j - arg_base];
        zan_type_t *pt = subst_method_tp(g,
            method_param_type(g, sp.msym, j), tps, sp.bind);
        if (sp.owner_inst) pt = subst_type_param_deep(g, pt, sp.owner_inst);
        call_args[j + this_off] =
            emit_arg_typed(g, aexprs[j + this_off], pt, locals);
        zan_ast_node_t *arg = aexprs[j + this_off];
        zan_type_t *at = infer_expr_type(g, arg, locals);
        if (at && is_rc_managed_type(at) &&
            !expr_is_local_ident(arg, locals) &&
            expr_yields_owned_rc_value(g, arg, locals) &&
            LLVMGetTypeKind(LLVMTypeOf(call_args[j + this_off])) == LLVMPointerTypeKind) {
            int ehk = eh_slot_kind_of(at);
            if (ehk != ZAN_EH_SLOT_OBJ) {
                LLVMValueRef slot = emit_entry_alloca(g,
                    LLVMTypeOf(call_args[j + this_off]), "arg.eh");
                LLVMBuildStore(g->builder, call_args[j + this_off], slot);
                emit_eh_tmp_push_slot(g, slot, ehk);
            } else {
                emit_eh_tmp_push(g, call_args[j + this_off]);
            }
            arg_eh_pushed[j + this_off] = 1;
        }
    }
    coerce_args_to_params(g, sp.fn_type, call_args, total);
    const char *cn = (LLVMGetTypeKind(LLVMGetReturnType(sp.fn_type)) ==
                      LLVMVoidTypeKind) ? "" : "gspec";
    LLVMValueRef result = zan_call2(g->builder, sp.fn_type, sp.fn,
                                    call_args, (unsigned)total, cn);
    for (int j = total - 1; j >= 0; j--)
        if (arg_eh_pushed[j]) emit_eh_tmp_pop(g);
    for (int j = 0; j < total; j++)
        if (aexprs[j])
            emit_release_owned_call_temp(g, aexprs[j], call_args[j], locals);
    free(arg_eh_pushed);
    free(aexprs);
    free(call_args);
    return result;
}

/* Fresh rc string holding the C-string bytes in [start, end). The range may
 * be the empty range (end == start). Used by Path.GetFileName/GetExtension so
 * their results are owned copies that outlive the path argument. */
static LLVMValueRef emit_string_copy_range(zan_irgen_t *g,
        LLVMValueRef start, LLVMValueRef end) {
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMValueRef len = zan_sub(g->builder,
        LLVMBuildPtrToInt(g->builder, end, i64, "e.i"),
        LLVMBuildPtrToInt(g->builder, start, i64, "s.i"), "slen");
    LLVMValueRef total = zan_add(g->builder, len,
        LLVMConstInt(i64, 1, 0), "cap");
    LLVMValueRef buf = emit_string_alloc_rc(g, total);
    LLVMTypeRef memcpy_ty = LLVMFunctionType(i8ptr,
        (LLVMTypeRef[]){ i8ptr, i8ptr, i64 }, 3, 0);
    LLVMValueRef mc = get_libc_fn(g, "memcpy", memcpy_ty);
    zan_call2(g->builder, memcpy_ty, mc,
        (LLVMValueRef[]){ buf, start, len }, 3, "");
    LLVMValueRef endp = LLVMBuildGEP2(g->builder,
        LLVMInt8TypeInContext(g->ctx), buf, &len, 1, "ep");
    zan_store_fit(g, LLVMConstInt(LLVMInt8TypeInContext(g->ctx), 0, 0), endp);
    return buf;
}

/* True when `n` (a lambda body) contains a `return <expr>;` anywhere below it.
 * Task.Run's delegate parameter is an Action (void), so a value-returning body
 * would be emitted into a void function and fail LLVM verification; report it
 * like C# would instead. */
static bool lambda_body_has_value_return(zan_ast_node_t *n) {
    if (!n) return false;
    switch (n->kind) {
    case AST_RETURN_STMT:
        return n->ret.value != NULL;
    case AST_BLOCK:
        for (int i = 0; i < n->block.stmts.count; i++)
            if (lambda_body_has_value_return(n->block.stmts.items[i])) return true;
        return false;
    case AST_IF_STMT:
        return lambda_body_has_value_return(n->if_stmt.then_body) ||
               lambda_body_has_value_return(n->if_stmt.else_body);
    case AST_WHILE_STMT:
        return lambda_body_has_value_return(n->while_stmt.body);
    case AST_DO_WHILE_STMT:
        return lambda_body_has_value_return(n->while_stmt.body);
    case AST_FOR_STMT:
        return lambda_body_has_value_return(n->for_stmt.body);
    case AST_FOREACH_STMT:
        return lambda_body_has_value_return(n->foreach_stmt.body);
    case AST_SWITCH_STMT:
        for (int i = 0; i < n->switch_stmt.cases.count; i++) {
            zan_ast_node_t *cs = n->switch_stmt.cases.items[i];
            if (!cs) continue;
            if (lambda_body_has_value_return(cs->switch_case.body)) return true;
        }
        return false;
    case AST_TRY_STMT:
        return lambda_body_has_value_return(n->try_stmt.try_body) ||
               (n->try_stmt.finally_body &&
                lambda_body_has_value_return(n->try_stmt.finally_body));
    default:
        return false;
    }
}

/* C# overloads the string search/split methods on char, and a char argument
 * denotes the one-character string it spells. These three keep that
 * conversion out of every call site: without it a char argument reached
 * strstr/strncmp as an integer, which is what made `"abc".IndexOf('b')`
 * answer 0. */
static int is_string_like_expr(zan_irgen_t *g, zan_ast_node_t *e,
                               local_scope_t *locals) {
    return is_string_expr(g, e, locals) || expr_is_char(g, e, locals);
}

/* Emit `e` as a C string. `*owned` reports a freshly built char string, which
 * the caller releases directly (no source temporary owns it). */
static LLVMValueRef emit_string_like_arg(zan_irgen_t *g, zan_ast_node_t *e,
                                         local_scope_t *locals, int *owned) {
    LLVMValueRef v = emit_expr(g, e, locals);
    *owned = 0;
    if (LLVMGetTypeKind(LLVMTypeOf(v)) == LLVMIntegerTypeKind &&
        expr_is_char(g, e, locals)) {
        *owned = 1;
        return emit_char_to_cstr(g, v);
    }
    return v;
}

static void release_string_like_arg(zan_irgen_t *g, zan_ast_node_t *e,
                                    LLVMValueRef v, int owned,
                                    local_scope_t *locals) {
    if (owned) emit_string_release(g, v);
    else emit_release_owned_call_temp(g, e, v, locals);
}

/* The declared members of an enum symbol with their effective constants.
 * C# running counter: an explicit `= n` resets it, the next auto member
 * continues at n+1 — the same rule as the EnumType.Member fold in
 * irgen_expr.c and the reflection table in irgen_reflect.c. Returns the
 * member count written, capped at `cap`. */
static int irgen_enum_members(zan_symbol_t *sym, zan_symbol_t **out_syms,
                              long long *out_vals, int cap) {
    if (!sym) { return 0; }
    long long val = 0;
    int n = 0;
    for (int i = 0; i < sym->member_count && n < cap; i++) {
        zan_symbol_t *m = sym->members[i];
        if (!m || m->kind != SYM_ENUM_MEMBER) { continue; }
        if (m->decl && m->decl->kind == AST_ENUM_MEMBER &&
            m->decl->enum_member.value &&
            m->decl->enum_member.value->kind == AST_INT_LITERAL) {
            val = (long long)m->decl->enum_member.value->int_val;
        }
        out_syms[n] = m;
        out_vals[n] = val;
        n++;
        val++;
    }
    return n;
}

/* The collection intrinsics evaluate their receiver expression directly
 * instead of going through the regular instance-call path, so an owned (+1)
 * receiver (`Fetch().Items`) leaks its extra reference: nobody released it
 * after the operation. Mirror what a regular call does with its self argument
 * (the arg handling above): register the value for exception unwinding while
 * the arguments run, then pop and release it once the intrinsic is done.
 * Borrowed receivers (locals, statics, fields of locals) make both helpers
 * no-ops. */
static int emit_intrinsic_own_recv(zan_irgen_t *g, zan_ast_node_t *lobj,
                                   LLVMValueRef recv, local_scope_t *locals) {
    zan_type_t *lt = infer_expr_type(g, lobj, locals);
    if (!lt || !is_rc_managed_type(lt) ||
        expr_is_local_ident(lobj, locals) ||
        !expr_yields_owned_rc_value(g, lobj, locals)) return 0;
    emit_eh_tmp_push(g, recv);
    return 1;
}

static void emit_intrinsic_drop_recv(zan_irgen_t *g, zan_ast_node_t *lobj,
                                     LLVMValueRef recv, local_scope_t *locals,
                                     int pushed) {
    if (pushed) emit_eh_tmp_pop(g);
    emit_release_owned_call_temp(g, lobj, recv, locals);
}

static LLVMValueRef emit_expr_call(zan_irgen_t *g, zan_ast_node_t *expr,
        local_scope_t *locals) {
        /* Reflection (irgen_reflect.c): `ti.GetFieldName(i)` and friends on a
         * TypeInfo, and `obj.GetType()` / `obj.GetFieldInt("x")` on any class
         * or struct value. A user member of the same name wins, so these are
         * only claimed when the receiver's type declares no such member. */
        if (expr->call.callee->kind == AST_MEMBER_ACCESS) {
            zan_ast_node_t *recv = expr->call.callee->member.object;
            zan_istr_t mname = expr->call.callee->member.name;
            zan_ast_node_t **cargs = expr->call.args.items;
            int cargc = expr->call.args.count;
            zan_type_t *rt = infer_expr_type(g, recv, locals);
            LLVMValueRef rv = NULL;
            if (zan_refl_is_typeinfo(rt)) {
                if (refl_emit_typeinfo_member(g,
                                              emit_guarded_member_object(
                                                  g, expr->call.callee, locals),
                                              mname, cargs, cargc, locals, &rv))
                    return rv;
            } else if (zan_refl_is_reflectable(rt) &&
                       (!rt->sym || !get_method_sym(rt->sym, mname)) &&
                       zan_refl_instance_method(mname, NULL)) {
                if (refl_emit_instance_call(g, rt,
                                            emit_guarded_member_object(
                                                g, expr->call.callee, locals),
                                            mname, cargs, cargc, locals, &rv))
                    return rv;
            }
        }

        /* `v.GetValueOrDefault()` / `v.ToString()` on a nullable value type:
         * the payload (already zero when null) and the C# text form. */
        if (expr->call.callee->kind == AST_MEMBER_ACCESS &&
            expr->call.args.count == 0) {
            zan_ast_node_t *recv = expr->call.callee->member.object;
            zan_istr_t mname = expr->call.callee->member.name;
            zan_type_t *nt = infer_expr_type(g, recv, locals);
            bool is_gvod = mname.len == 17 &&
                memcmp(mname.str, "GetValueOrDefault", 17) == 0;
            bool is_tostr = mname.len == 8 && memcmp(mname.str, "ToString", 8) == 0;
            if (nt && nt->kind == TYPE_NULLABLE && (is_gvod || is_tostr)) {
                LLVMValueRef nv = emit_expr(g, recv, locals);
                if (llvm_is_nullable(LLVMTypeOf(nv)))
                    return is_gvod ? nullable_get_payload(g, nv)
                                   : emit_to_cstr(g, nv);
            }
        }

        /* `md.GetLength(dim)` on a rank-N rectangular array: load the
         * dimension from the shape in the allocation header (dims[d] at
         * arr + 8*d, see zan_mdarray_alloc). */
        if (expr->call.callee->kind == AST_MEMBER_ACCESS &&
            expr->call.args.count == 1) {
            zan_ast_node_t *recv = expr->call.callee->member.object;
            zan_istr_t mname = expr->call.callee->member.name;
            if (mname.len == 9 && memcmp(mname.str, "GetLength", 9) == 0) {
                zan_type_t *at = infer_expr_type(g, recv, locals);
                if (at && at->kind == TYPE_ARRAY && at->array_rank > 1) {
                    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                    LLVMValueRef arr = emit_expr(g, recv, locals);
                    LLVMValueRef dim = emit_expr(g, expr->call.args.items[0], locals);
                    dim = emit_index_i64(g, dim, "gl.dim");
                    LLVMValueRef oob = zan_icmp(g->builder, LLVMIntUGE, dim,
                        LLVMConstInt(i64, at->array_rank, 0), "gl.oob");
                    emit_runtime_check(g, oob, expr->loc,
                        "GetLength dimension out of range");
                    LLVMValueRef dim_ptr = LLVMBuildBitCast(g->builder, arr,
                        LLVMPointerType(i64, 0), "gl.dp");
                    LLVMValueRef slot = LLVMBuildGEP2(g->builder, i64, dim_ptr,
                        &dim, 1, "gl.slot");
                    return LLVMBuildLoad2(g->builder, i64, slot, "gl.val");
                }
            }
        }

        /* Task instance members — `t.Wait()`, `t.Result`, `t.IsCompleted` on
         * a Task/Task<T> value. The value is the coroutine handle Task.Run/
         * Task.Spawn hands back (the frame pointer as an i64). Wait pumps the
         * cooperative driver until that frame is done — the only way a
         * synchronous context can let a spawned coroutine make progress —
         * IsCompleted is a non-pumping probe, and Result reads the frame's
         * result slot for a Task<T> (whose spawn deliberately left the frame
         * unreaped so the result survives; see below). */
        if (expr->call.callee->kind == AST_MEMBER_ACCESS &&
            expr->call.args.count == 0) {
            zan_ast_node_t *recv = expr->call.callee->member.object;
            zan_istr_t mname = expr->call.callee->member.name;
            zan_type_t *ot = infer_expr_type(g, recv, locals);
            if (ot && ot->kind == TYPE_TASK) {
                bool is_wait = mname.len == 4 && memcmp(mname.str, "Wait", 4) == 0;
                bool is_res  = mname.len == 6 && memcmp(mname.str, "Result", 6) == 0;
                bool is_comp = mname.len == 11 && memcmp(mname.str, "IsCompleted", 11) == 0;
                if (is_wait || is_res || is_comp) {
                    LLVMTypeRef ti64 = LLVMInt64TypeInContext(g->ctx);
                    if (is_res && ot->type_arg_count != 1) {
                        zan_diag_emit(g->diag, DIAG_ERROR, expr->loc,
                            "Task.Result requires a Task<T> value");
                        return LLVMConstInt(ti64, 0, 0);
                    }
                    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                    LLVMValueRef h = emit_expr(g, recv, locals);
                    if (LLVMGetTypeKind(LLVMTypeOf(h)) == LLVMPointerTypeKind)
                        h = LLVMBuildPtrToInt(g->builder, h, ti64, "task.h");
                    else if (LLVMGetIntTypeWidth(LLVMTypeOf(h)) < 64)
                        h = zan_iwiden(g->builder, h, ti64);
                    LLVMValueRef hp = LLVMBuildIntToPtr(g->builder, h, i8ptr, "task.fp");
                    int mode = is_comp ? 2 : (is_res ? 1 :
                              (ot->type_arg_count == 1 ? 3 : 0));
                    return emit_task_member(g, hp,
                        ot->type_arg_count == 1 ? ot->type_args[0] : NULL, mode);
                }
            }
        }

        /* Task.Spawn(<asyncCall>) — fire-and-forget: run an async call as an
         * independent coroutine WITHOUT awaiting it. Task.Run is the same
         * lowering under the name the design docs use, except that when the
         * async call returns a value the frame is left unreaped so the caller
         * can read it through `Task<T>.Result` / `Wait()` (Task.Spawn always
         * reaps: its handles are only polled with IsDone/Cancel). Emits the
         * callee's ramp (heap frame) then schedules it on the cooperative
         * driver with no awaiter and without suspending the caller (contrast
         * await, which registers self as awaiter and suspends). This is the
         * concurrency primitive a server accept loop uses to handle each
         * connection on its own coroutine instead of serially. See
         * docs/ASYNC_CPS_DESIGN.md. */
        if ((is_call_to(expr, "Task", "Spawn") || is_call_to(expr, "Task", "Run")) &&
            expr->call.args.count == 1) {
            LLVMTypeRef sp_i64 = LLVMInt64TypeInContext(g->ctx);
            LLVMTypeRef sp_i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            zan_ast_node_t *sub_arg = expr->call.args.items[0];
            zan_type_t *sub_ty = infer_expr_type(g, sub_arg, locals);
            bool is_del = sub_ty && sub_ty->kind == TYPE_DELEGATE;
            if (sub_arg->kind != AST_CALL && sub_arg->kind != AST_LAMBDA && !is_del) {
                /* not a shape this lowering handles (e.g. Task.Run(5)): leave
                 * it to the normal call path so the unresolved-call
                 * diagnostic fires instead of silently lowering to 0 */
            } else {

            /* Task.Run(<delegate>) / Task.Spawn(<delegate>): the body runs to
             * completion right here and the task is done before Run returns.
             * Any awaits inside it pump the driver via the root-await path, so
             * this is observably identical to scheduling it on the pool in the
             * cooperative single-threaded model. The handle 0 is "already
             * done": __zan_co_isdone(0) returns 1 because no live frame
             * matches it, so Wait()/IsCompleted return at once. */
            if (sub_arg->kind == AST_LAMBDA || is_del) {
                int pc = 0;
                LLVMValueRef lv = NULL;
                LLVMTypeRef vret = LLVMVoidTypeInContext(g->ctx);
                if (sub_arg->kind == AST_LAMBDA) {
                    if (sub_arg->lambda.params.count != 0 ||
                        lambda_body_has_value_return(sub_arg->lambda.body)) {
                        zan_diag_emit(g->diag, DIAG_ERROR, sub_arg->loc,
                            "Task.Run delegate must be a parameterless void body "
                            "(an Action); pass an async call for results");
                        return LLVMConstInt(sp_i64, 0, 0);
                    }
                    zan_type_t *adt = (zan_type_t *)zan_arena_alloc(
                        g->arena, sizeof(zan_type_t));
                    memset(adt, 0, sizeof(*adt));
                    adt->kind = TYPE_DELEGATE;
                    adt->name = (zan_istr_t){ (char *)"__TaskAction", 12 };
                    adt->delegate_is_async = 0;
                    adt->delegate_param_count = 0;
                    adt->delegate_ret_type = g->binder->type_void;
                    lv = emit_lambda_typed(g, sub_arg, adt, locals);
                } else {
                    if (sub_ty->delegate_param_count != 0) {
                        zan_diag_emit(g->diag, DIAG_ERROR, sub_arg->loc,
                            "Task.Run delegate must take no parameters");
                        return LLVMConstInt(sp_i64, 0, 0);
                    }
                    lv = emit_expr(g, sub_arg, locals);
                    vret = sub_ty->delegate_ret_type
                        ? map_type(g, sub_ty->delegate_ret_type)
                        : vret;
                }
                LLVMTypeRef fn_type = LLVMFunctionType(vret, NULL, 0, 0);
                emit_delegate_invoke(g, lv, fn_type, vret, NULL, 0, "");
                emit_release_owned_call_temp(g, sub_arg, lv, locals);
                return LLVMConstInt(sp_i64, 0, 0);
            }
            LLVMValueRef sub = emit_expr(g, sub_arg, locals);
            /* A result-carrying Task.Run keeps its frame: it must stay alive
             * (tracked, DONE set at completion) until Result/Wait reads the
             * result and reaps it. */
            bool keep_result = is_call_to(expr, "Task", "Run") &&
                !is_call_to(expr, "Task", "Spawn") &&
                sub_ty && sub_ty->kind != TYPE_VOID;
            LLVMValueRef sub_i8 = emit_detach_async_call(g, sub, keep_result);
            emit_release_owned_call_temp(g, sub_arg, sub, locals);
            /* Task.Spawn yields the task handle, so the program can later
             * cancel the detached coroutine (Task.Cancel). Discarding it
             * stays fire-and-forget. */
            if (sub_i8) return LLVMBuildPtrToInt(g->builder, sub_i8, sp_i64, "spawn.h");
            return LLVMConstInt(sp_i64, 0, 0);
            }
        }

        /* Task.Cancel(handle) — request cooperative cancellation of a spawned
         * coroutine and of whatever it is transitively awaiting. The target
         * observes it at its next statement boundary after an await and
         * completes early; see get_co_cancel_fn. */
        if (is_call_to(expr, "Task", "Cancel") && expr->call.args.count == 1) {
            LLVMTypeRef ci8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMTypeRef ci64 = LLVMInt64TypeInContext(g->ctx);
            LLVMValueRef h = emit_expr(g, expr->call.args.items[0], locals);
            if (LLVMGetTypeKind(LLVMTypeOf(h)) == LLVMIntegerTypeKind) {
                if (LLVMGetIntTypeWidth(LLVMTypeOf(h)) < 64)
                    h = zan_iwiden(g->builder, h, ci64);
                h = LLVMBuildIntToPtr(g->builder, h, ci8ptr, "cancel.h");
            } else {
                h = LLVMBuildBitCast(g->builder, h, ci8ptr, "cancel.h");
            }
            LLVMValueRef cf = get_co_cancel_fn(g);
            zan_call2(g->builder, LLVMGlobalGetValueType(cf), cf, &h, 1, "");
            return LLVMConstInt(ci64, 0, 0);
        }

        /* Task.IsDone(handle) — whether the coroutine a Task.Spawn handle names
         * has completed. This is what fan-out joins are built on
         * (System.Threading.TaskJoin, which `Task.WhenAll` desugars to): without
         * it a spawned coroutine's completion is unobservable from the outside,
         * so every caller had to thread its own counter and gate through the
         * spawned bodies. See get_co_isdone_fn. Yields 1/0 as an `int`, like the
         * sibling Task.IsCancellationRequested. */
        if (is_call_to(expr, "Task", "IsDone") && expr->call.args.count == 1) {
            LLVMTypeRef di8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMTypeRef di64 = LLVMInt64TypeInContext(g->ctx);
            LLVMValueRef h = emit_expr(g, expr->call.args.items[0], locals);
            if (LLVMGetTypeKind(LLVMTypeOf(h)) == LLVMIntegerTypeKind) {
                if (LLVMGetIntTypeWidth(LLVMTypeOf(h)) < 64)
                    h = zan_iwiden(g->builder, h, di64);
                h = LLVMBuildIntToPtr(g->builder, h, di8ptr, "isdone.h");
            } else {
                h = LLVMBuildBitCast(g->builder, h, di8ptr, "isdone.h");
            }
            LLVMValueRef idf = get_co_isdone_fn(g);
            return zan_call2(g->builder, LLVMGlobalGetValueType(idf),
                             idf, &h, 1, "isdone");
        }

        /* Task.IsCancellationRequested() — inside an async body, whether this
         * coroutine has been cancelled, so a long-running body can bail out at
         * a point of its own choosing. Always 0 outside one. */
        if (is_call_to(expr, "Task", "IsCancellationRequested") &&
            expr->call.args.count == 0) {
            LLVMTypeRef ci32 = LLVMInt32TypeInContext(g->ctx);
            if (!g->current_async_frame) return LLVMConstInt(ci32, 0, 0);
            return LLVMBuildLoad2(g->builder, ci32,
                LLVMBuildStructGEP2(g->builder, g->current_async_frame_type,
                    g->current_async_frame, ASYNC_FRAME_CANCEL, "fr.cancel.p"),
                "cancel.req");
        }

        /* StringBuilder.Append(s) / StringBuilder.ToString() — growable byte
         * buffer with amortised O(1) append (capacity doubling). */
        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS) {
            zan_ast_node_t *sbcallee = expr->call.callee;
            zan_istr_t sbm = sbcallee->member.name;
            zan_type_t *sbt = infer_expr_type(g, sbcallee->member.object, locals);
            if (sbt && type_named(sbt, "StringBuilder", 13)) {
                LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
                LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
                LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                LLVMValueRef raw = emit_expr(g, sbcallee->member.object, locals);
                LLVMValueRef sbp = LLVMBuildBitCast(g->builder, raw,
                    LLVMPointerType(g->sb_struct_type, 0), "sbp");
                int sb_is_append = (sbm.len == 6 && memcmp(sbm.str, "Append", 6) == 0 &&
                                    expr->call.args.count == 1);
                int sb_is_appendline = (sbm.len == 10 && memcmp(sbm.str, "AppendLine", 10) == 0 &&
                                        expr->call.args.count <= 1);
                if (sb_is_append || sb_is_appendline) {
                    if (expr->call.args.count == 1) {
                        zan_ast_node_t *arg0 = expr->call.args.items[0];
                        LLVMValueRef v = emit_expr(g, arg0, locals);
                        /* A char appends as the character (C#), not as the
                         * decimal code emit_value_as_cstr would format. */
                        int char_arg =
                            LLVMGetTypeKind(LLVMTypeOf(v)) == LLVMIntegerTypeKind &&
                            expr_is_char(g, arg0, locals);
                        /* An integer formats straight into a stack buffer and
                         * yields its digit count, so neither the int nor a
                         * string literal needs a strlen to be appended. */
                        int int_arg = !char_arg &&
                            LLVMGetTypeKind(LLVMTypeOf(v)) == LLVMIntegerTypeKind &&
                            LLVMGetIntTypeWidth(LLVMTypeOf(v)) > 1;
                        LLVMValueRef s, slen;
                        if (int_arg) {
                            s = emit_entry_scratch(g, 40, "sb.i2s");
                            /* a `ulong` appends unsigned, or everything past
                             * 2^63 lands in the buffer as a negative number */
                            slen = emit_itoa_into(g, s,
                                emit_widen_i64_for_print(g, v),
                                expr_is_ulong(g, arg0, locals) ? 1 : 0);
                        } else {
                            s = char_arg ? emit_char_to_cstr(g, v)
                                         : emit_value_as_cstr(g, v);
                            slen = emit_cstr_len_of(g, s, arg0);
                        }
                        emit_sb_append_bytes(g, sbp, s, slen);
                        if (char_arg)
                            emit_string_release(g, s);
                        else if (LLVMGetTypeKind(LLVMTypeOf(v)) == LLVMPointerTypeKind)
                            emit_release_owned_call_temp(g, arg0, v, locals);
                    }
                    if (sb_is_appendline) {
                        zan_istr_t nl = { "\n", 1 };
                        LLVMValueRef nls = emit_string_literal_rc(g, nl);
                        emit_sb_append_bytes(g, sbp, nls, LLVMConstInt(i64, 1, 0));
                    }
                    return raw;
                }
                if (sbm.len == 8 && memcmp(sbm.str, "ToString", 8) == 0 &&
                    expr->call.args.count == 0) {
                    LLVMValueRef cptr = LLVMBuildStructGEP2(g->builder, g->sb_struct_type, sbp, 0, "sbcp");
                    LLVMValueRef count = LLVMBuildLoad2(g->builder, i64, cptr, "sbcv");
                    LLVMValueRef dptr = LLVMBuildStructGEP2(g->builder, g->sb_struct_type, sbp, 2, "sbdp");
                    LLVMValueRef data = LLVMBuildLoad2(g->builder, i8ptr, dptr, "sbdv");
                    LLVMValueRef bsz = zan_add(g->builder, count, LLVMConstInt(i64, 1, 0), "sbbsz");
                    LLVMValueRef buf = emit_string_alloc_rc(g, bsz);
                    LLVMValueRef memcpy_fn = LLVMGetNamedFunction(g->mod, "memcpy");
                    if (!memcpy_fn) {
                        memcpy_fn = LLVMAddFunction(g->mod, "memcpy",
                            LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr, i64 }, 3, 0));
                    }
                    zan_call2(g->builder,
                        LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr, i64 }, 3, 0),
                        memcpy_fn, (LLVMValueRef[]){ buf, data, count }, 3, "");
                    LLVMValueRef endp = LLVMBuildGEP2(g->builder, i8, buf, &count, 1, "sbend");
                    LLVMBuildStore(g->builder, LLVMConstInt(i8, 0, 0), endp);
                    return buf;
                }
            }
        }

        /* special-case Console.WriteLine */
        if (!zan_type_defines(g, "Console", "WriteLine") &&
            (is_call_to(expr, "Console", "WriteLine") ||
             is_call_to(expr, "Console", "PrintLine"))) {
            if (expr->call.args.count > 0) {
                zan_ast_node_t *arg_ast = expr->call.args.items[0];
                LLVMValueRef arg = emit_expr(g, arg_ast, locals);
                LLVMTypeRef arg_type = LLVMTypeOf(arg);

                if (LLVMGetTypeKind(arg_type) == LLVMPointerTypeKind) {
                    LLVMTypeRef fn_type = LLVMFunctionType(
                        LLVMVoidTypeInContext(g->ctx),
                        (LLVMTypeRef[]){ LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0) },
                        1, 0);
                    zan_call2(g->builder, fn_type, g->rt_println, &arg, 1, "");
                } else if (LLVMGetTypeKind(arg_type) == LLVMDoubleTypeKind ||
                           LLVMGetTypeKind(arg_type) == LLVMFloatTypeKind) {
                    LLVMTypeRef dbl = LLVMDoubleTypeInContext(g->ctx);
                    if (LLVMGetTypeKind(arg_type) == LLVMFloatTypeKind)
                        arg = LLVMBuildFPExt(g->builder, arg, dbl, "print.ext");
                    LLVMTypeRef fn_type = LLVMFunctionType(
                        LLVMVoidTypeInContext(g->ctx),
                        (LLVMTypeRef[]){ dbl },
                        1, 0);
                    zan_call2(g->builder, fn_type, g->rt_print_double, &arg, 1, "");
                } else if (llvm_is_nullable(arg_type)) {
                    /* a null `T?` prints as an empty line, as in C#. */
                    LLVMValueRef ns = emit_to_cstr(g, arg);
                    LLVMTypeRef i8p = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                    zan_call2(g->builder,
                        LLVMFunctionType(LLVMVoidTypeInContext(g->ctx),
                                         (LLVMTypeRef[]){ i8p }, 1, 0),
                        g->rt_println, &ns, 1, "");
                    emit_string_release(g, ns);
                } else if (expr_is_char(g, arg_ast, locals)) {
                    /* char prints as the character (C#), UTF-8 encoded. */
                    LLVMValueRef cs = emit_char_to_cstr(g, arg);
                    LLVMTypeRef i8p = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                    zan_call2(g->builder,
                        LLVMFunctionType(LLVMVoidTypeInContext(g->ctx),
                                         (LLVMTypeRef[]){ i8p }, 1, 0),
                        g->rt_println, &cs, 1, "");
                    emit_string_release(g, cs);
                } else if (expr_is_bool(g, arg_ast, locals)) {
                    /* bool prints as true/false (C#), not as 1/0. */
                    LLVMValueRef t = emit_string_literal_rc(g,
                        (zan_istr_t){ "true", 4 });
                    LLVMValueRef f = emit_string_literal_rc(g,
                        (zan_istr_t){ "false", 5 });
                    LLVMValueRef cond = arg;
                    if (LLVMGetTypeKind(LLVMTypeOf(arg)) ==
                            LLVMIntegerTypeKind &&
                        LLVMGetIntTypeWidth(LLVMTypeOf(arg)) != 1)
                        cond = LLVMBuildICmp(g->builder, LLVMIntNE, arg,
                            LLVMConstNull(LLVMTypeOf(arg)), "wl.bnz");
                    LLVMValueRef bs = LLVMBuildSelect(g->builder, cond, t, f,
                                                      "wl.b");
                    LLVMTypeRef i8p = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                    zan_call2(g->builder,
                        LLVMFunctionType(LLVMVoidTypeInContext(g->ctx),
                                         (LLVMTypeRef[]){ i8p }, 1, 0),
                        g->rt_println, &bs, 1, "");
                } else {
                    /* ensure integer arg is i64 for print_int; an unsigned
                     * value (uint/ulong/ushort/byte) must zero-extend so
                     * `uint.MaxValue` (0xFFFFFFFF in an i32 slot) prints as
                     * 4294967295 and not 18446744073709551615 */
                    bool print_unsigned = expr_is_unsigned_int(g, arg_ast, locals);
                    if (print_unsigned) {
                        LLVMTypeRef vt = LLVMTypeOf(arg);
                        if (LLVMGetTypeKind(vt) == LLVMIntegerTypeKind &&
                            LLVMGetIntTypeWidth(vt) < 64)
                            arg = LLVMBuildZExt(g->builder, arg,
                                LLVMInt64TypeInContext(g->ctx), "print.zext");
                        else
                            arg = emit_widen_i64_for_print(g, arg);
                    } else {
                        arg = emit_widen_i64_for_print(g, arg);
                    }
                    LLVMTypeRef fn_type = LLVMFunctionType(
                        LLVMVoidTypeInContext(g->ctx),
                        (LLVMTypeRef[]){ LLVMInt64TypeInContext(g->ctx) },
                        1, 0);
                    LLVMValueRef print_fn = print_unsigned
                        ? g->rt_print_uint : g->rt_print_int;
                    zan_call2(g->builder, fn_type, print_fn, &arg, 1, "");
                }
                emit_release_owned_call_temp(g, arg_ast, arg, locals);
            } else {
                /* WriteLine() with no argument -> a blank line, matching C#. */
                LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                LLVMValueRef empty = LLVMBuildGlobalStringPtr(g->builder, "", "wl_nl");
                zan_call2(g->builder,
                    LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), (LLVMTypeRef[]){ i8ptr }, 1, 0),
                    g->rt_println, &empty, 1, "");
            }
            return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
        }

        /* Console.Write (no newline) */
        if (!zan_type_defines(g, "Console", "Write") &&
            is_call_to(expr, "Console", "Write")) {
            if (expr->call.args.count > 0) {
                zan_ast_node_t *arg_ast = expr->call.args.items[0];
                LLVMValueRef arg = emit_expr(g, arg_ast, locals);
                LLVMTypeRef arg_type = LLVMTypeOf(arg);
                LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                LLVMTypeRef printf_args[] = { i8ptr };
                LLVMTypeRef printf_type = LLVMFunctionType(
                    LLVMInt32TypeInContext(g->ctx), printf_args, 1, 1);
                LLVMValueRef printf_fn = LLVMGetNamedFunction(g->mod, "printf");

                if (LLVMGetTypeKind(arg_type) == LLVMPointerTypeKind) {
                    LLVMValueRef fmt = LLVMBuildGlobalStringPtr(g->builder, "%s", "wfmt_s");
                    LLVMValueRef args[] = { fmt, arg };
                    zan_call2(g->builder, printf_type, printf_fn, args, 2, "");
                } else if (LLVMGetTypeKind(arg_type) == LLVMDoubleTypeKind ||
                           LLVMGetTypeKind(arg_type) == LLVMFloatTypeKind) {
                    LLVMValueRef fmt = LLVMBuildGlobalStringPtr(g->builder, "%g", "wfmt_d");
                    LLVMValueRef dbl_arg = arg;
                    if (LLVMGetTypeKind(arg_type) == LLVMFloatTypeKind)
                        dbl_arg = LLVMBuildFPExt(g->builder, arg, LLVMDoubleTypeInContext(g->ctx), "ext");
                    LLVMValueRef args[] = { fmt, dbl_arg };
                    zan_call2(g->builder, printf_type, printf_fn, args, 2, "");
                } else if (llvm_is_nullable(arg_type)) {
                    LLVMValueRef ns = emit_to_cstr(g, arg);
                    LLVMValueRef nfmt = LLVMBuildGlobalStringPtr(g->builder, "%s", "wfmt_n");
                    LLVMValueRef nargs[] = { nfmt, ns };
                    zan_call2(g->builder, printf_type, printf_fn, nargs, 2, "");
                    emit_string_release(g, ns);
                } else if (expr_is_char(g, arg_ast, locals)) {
                    LLVMValueRef cs = emit_char_to_cstr(g, arg);
                    LLVMValueRef cfmt = LLVMBuildGlobalStringPtr(g->builder, "%s", "wfmt_c");
                    LLVMValueRef cargs[] = { cfmt, cs };
                    zan_call2(g->builder, printf_type, printf_fn, cargs, 2, "");
                    emit_string_release(g, cs);
                } else if (expr_is_bool(g, arg_ast, locals)) {
                    /* bool prints as true/false (C#), not as 1/0. */
                    LLVMValueRef t = emit_string_literal_rc(g,
                        (zan_istr_t){ "true", 4 });
                    LLVMValueRef f = emit_string_literal_rc(g,
                        (zan_istr_t){ "false", 5 });
                    LLVMValueRef cond = arg;
                    if (LLVMGetTypeKind(LLVMTypeOf(arg)) ==
                            LLVMIntegerTypeKind &&
                        LLVMGetIntTypeWidth(LLVMTypeOf(arg)) != 1)
                        cond = LLVMBuildICmp(g->builder, LLVMIntNE, arg,
                            LLVMConstNull(LLVMTypeOf(arg)), "w.bnz");
                    LLVMValueRef bs = LLVMBuildSelect(g->builder, cond, t, f,
                                                      "w.b");
                    LLVMValueRef bfmt = LLVMBuildGlobalStringPtr(g->builder,
                        "%s", "wfmt_b");
                    LLVMValueRef bargs[] = { bfmt, bs };
                    zan_call2(g->builder, printf_type, printf_fn, bargs, 2, "");
                } else {
                    LLVMValueRef fmt = expr_is_ulong(g, arg_ast, locals)
                        ? LLVMBuildGlobalStringPtr(g->builder, "%llu", "wfmt_u")
                        : LLVMBuildGlobalStringPtr(g->builder, "%lld", "wfmt_i");
                    LLVMValueRef int_arg = emit_widen_i64_for_print(g, arg);
                    LLVMValueRef args[] = { fmt, int_arg };
                    zan_call2(g->builder, printf_type, printf_fn, args, 2, "");
                }
                emit_release_owned_call_temp(g, arg_ast, arg, locals);
            }
            return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
        }

        /* Console.ReadLine() -> reads a line from stdin, returns i8* */
        if (!zan_type_defines(g, "Console", "ReadLine") &&
            is_call_to(expr, "Console", "ReadLine")) {
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
            /* allocate 1024 byte buffer */
            LLVMValueRef buf_size = LLVMConstInt(i64, 1024, 0);
            LLVMValueRef buf = emit_string_alloc_rc(g, buf_size);
            /* declare fgets if needed */
            LLVMValueRef fgets_fn = LLVMGetNamedFunction(g->mod, "fgets");
            if (!fgets_fn) {
                LLVMTypeRef fgets_args[] = { i8ptr, LLVMInt32TypeInContext(g->ctx), i8ptr };
                LLVMTypeRef fgets_type = LLVMFunctionType(i8ptr, fgets_args, 3, 0);
                fgets_fn = LLVMAddFunction(g->mod, "fgets", fgets_type);
            }
            /* get stdin: Windows UCRT exposes the stdin FILE* via
             * __acrt_iob_func(0), but ELF libc (glibc/musl) exports a `stdin`
             * global instead. Referencing __acrt_iob_func unconditionally left
             * that symbol undefined when cross-compiling to linux, so pick the
             * right one for the target. */
            LLVMValueRef stdin_ptr;
            if (g->target_is_windows) {
                LLVMValueRef stdin_fn = LLVMGetNamedFunction(g->mod, "__acrt_iob_func");
                if (!stdin_fn) {
                    LLVMTypeRef iob_args[] = { LLVMInt32TypeInContext(g->ctx) };
                    LLVMTypeRef iob_type = LLVMFunctionType(i8ptr, iob_args, 1, 0);
                    stdin_fn = LLVMAddFunction(g->mod, "__acrt_iob_func", iob_type);
                }
                LLVMValueRef zero = LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
                stdin_ptr = zan_call2(g->builder,
                    LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ LLVMInt32TypeInContext(g->ctx) }, 1, 0),
                    stdin_fn, &zero, 1, "stdin");
            } else {
                const char *siname = g->target_is_macos ? "__stdinp" : "stdin";
                LLVMValueRef stdin_g = LLVMGetNamedGlobal(g->mod, siname);
                if (!stdin_g) { stdin_g = LLVMAddGlobal(g->mod, i8ptr, siname); }
                stdin_ptr = LLVMBuildLoad2(g->builder, i8ptr, stdin_g, "stdin");
            }
            LLVMValueRef sz = LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 1024, 0);
            LLVMValueRef fgets_args[] = { buf, sz, stdin_ptr };
            /* Zero the first byte before fgets: on EOF (fgets returns NULL with
             * the buffer contents untouched) the returned string is "" rather
             * than whatever garbage the allocation happened to hold. */
            LLVMBuildStore(g->builder, LLVMConstInt(LLVMInt8TypeInContext(g->ctx), 0, 0),
                buf);
            zan_call2(g->builder,
                LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, LLVMInt32TypeInContext(g->ctx), i8ptr }, 3, 0),
                fgets_fn, fgets_args, 3, "");
            return buf;
        }

        /* Console.Read() -> one char from stdin as int (i64); getchar() returns
         * the byte or EOF (-1). Portable across targets. */
        if (!zan_type_defines(g, "Console", "Read") &&
            is_call_to(expr, "Console", "Read")) {
            LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
            LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
            LLVMTypeRef getchar_ty = LLVMFunctionType(i32t, NULL, 0, 0);
            LLVMValueRef getchar_fn = get_libc_fn(g, "getchar", getchar_ty);
            LLVMValueRef r = zan_call2(g->builder, getchar_ty, getchar_fn, NULL, 0, "cread");
            return LLVMBuildSExt(g->builder, r, i64t, "cread64");
        }

        /* Console.ReadKey([bool]) -> one keypress as its char code (i64).
         * Windows uses _getch (no Enter, no echo -- matches C#); other targets
         * fall back to getchar(). The optional intercept bool is accepted for
         * source compatibility but not otherwise acted on. */
        if (!zan_type_defines(g, "Console", "ReadKey") &&
            is_call_to(expr, "Console", "ReadKey")) {
            LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
            LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
            const char *fname = g->target_is_windows ? "_getch" : "getchar";
            LLVMTypeRef fty = LLVMFunctionType(i32t, NULL, 0, 0);
            LLVMValueRef fn = get_libc_fn(g, fname, fty);
            LLVMValueRef r = zan_call2(g->builder, fty, fn, NULL, 0, "readkey");
            return LLVMBuildSExt(g->builder, r, i64t, "readkey64");
        }

        /* Console.Clear() -> clear screen + home cursor via an ANSI escape
         * (portable; Windows 10+ consoles interpret VT sequences). */
        if (!zan_type_defines(g, "Console", "Clear") &&
            is_call_to(expr, "Console", "Clear")) {
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMTypeRef printf_type = LLVMFunctionType(
                LLVMInt32TypeInContext(g->ctx), (LLVMTypeRef[]){ i8ptr }, 1, 1);
            LLVMValueRef printf_fn = LLVMGetNamedFunction(g->mod, "printf");
            if (!printf_fn) printf_fn = LLVMAddFunction(g->mod, "printf", printf_type);
            LLVMValueRef esc = LLVMBuildGlobalStringPtr(g->builder, "\033[2J\033[H", "clrseq");
            zan_call2(g->builder, printf_type, printf_fn, &esc, 1, "");
            return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
        }

        /* Console.ResetColor() -> reset all SGR attributes to default. */
        if (!zan_type_defines(g, "Console", "ResetColor") &&
            is_call_to(expr, "Console", "ResetColor")) {
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMTypeRef printf_type = LLVMFunctionType(
                LLVMInt32TypeInContext(g->ctx), (LLVMTypeRef[]){ i8ptr }, 1, 1);
            LLVMValueRef printf_fn = LLVMGetNamedFunction(g->mod, "printf");
            if (!printf_fn) printf_fn = LLVMAddFunction(g->mod, "printf", printf_type);
            LLVMValueRef esc = LLVMBuildGlobalStringPtr(g->builder, "\033[0m", "rstseq");
            zan_call2(g->builder, printf_type, printf_fn, &esc, 1, "");
            emit_console_color_reset(g);
            return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
        }

        /* ==== NativeMemory intrinsics ====
         * Raw off-heap memory access for binary IO (ByteBuffer, ZanDB pages,
         * network framing). Addresses travel as nint (i64); every operation
         * lowers to a libc call, so none of these values ever enter the ARC
         * string/object machinery. Scalar access goes through Span<T> views
         * over the address (little-endian, align 1). */
        {
            LLVMValueRef nm_out = NULL;
            if (emit_native_memory_call(g, expr, locals, &nm_out))
                return nm_out;
        }

        /* Span<T> views: arr.AsSpan(), span.Slice() -> value struct. */
        {
            LLVMValueRef sp_out = NULL;
            if (emit_span_call(g, expr, locals, &sp_out))
                return sp_out;
        }

        /* byte[] <-> string: s.ToBytes(), b.ToStr([off, len]). */
        {
            LLVMValueRef by_out = NULL;
            if (emit_bytes_call(g, expr, locals, &by_out))
                return by_out;
        }

        /* String.CompareOrdinal(a, b) → strcmp: byte-wise ordinal compare of
         * two NUL-terminated strings in one libc call. */
        if (is_call_to(expr, "String", "CompareOrdinal") && expr->call.args.count == 2) {
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
            LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
            zan_ast_node_t *a_ast = expr->call.args.items[0];
            zan_ast_node_t *b_ast = expr->call.args.items[1];
            LLVMValueRef a = emit_expr(g, a_ast, locals);
            LLVMValueRef b = emit_expr(g, b_ast, locals);
            LLVMTypeRef strcmp_ty = LLVMFunctionType(i32t, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0);
            LLVMValueRef strcmp_fn = get_libc_fn(g, "strcmp", strcmp_ty);
            LLVMValueRef r = zan_call2(g->builder, strcmp_ty, strcmp_fn,
                (LLVMValueRef[]){ a, b }, 2, "ordcmp");
            emit_release_owned_call_temp(g, a_ast, a, locals);
            emit_release_owned_call_temp(g, b_ast, b, locals);
            return LLVMBuildSExt(g->builder, r, i64t, "ordcmp64");
        }

        /* Math.Sqrt(expr) → llvm.sqrt */
        if (!zan_type_defines(g, "Math", "Sqrt") &&
            is_call_to(expr, "Math", "Sqrt") && expr->call.args.count == 1) {
            LLVMValueRef arg = emit_expr(g, expr->call.args.items[0], locals);
            LLVMTypeRef dbl = LLVMDoubleTypeInContext(g->ctx);
            /* ensure arg is double */
            if (LLVMGetTypeKind(LLVMTypeOf(arg)) == LLVMFloatTypeKind) {
                arg = LLVMBuildFPExt(g->builder, arg, dbl, "ext");
            } else if (LLVMGetTypeKind(LLVMTypeOf(arg)) != LLVMDoubleTypeKind) {
                arg = LLVMBuildSIToFP(g->builder, arg, dbl, "tofp");
            }
            /* declare sqrt if needed */
            LLVMValueRef sqrt_fn = LLVMGetNamedFunction(g->mod, "sqrt");
            if (!sqrt_fn) {
                LLVMTypeRef sqrt_args[] = { dbl };
                LLVMTypeRef sqrt_type = LLVMFunctionType(dbl, sqrt_args, 1, 0);
                sqrt_fn = LLVMAddFunction(g->mod, "sqrt", sqrt_type);
            }
            LLVMTypeRef sqrt_type = LLVMFunctionType(dbl, (LLVMTypeRef[]){ dbl }, 1, 0);
            return zan_call2(g->builder, sqrt_type, sqrt_fn, &arg, 1, "sqrt");
        }

        /* Math.Sin/Cos/Tan/Log/Exp(expr) → libm call (double -> double) */
        {
            static const char *math1[] = { "Sin", "sin", "Cos", "cos", "Tan", "tan",
                                           "Log", "log", "Exp", "exp", NULL };
            for (int mi = 0; math1[mi]; mi += 2) {
                if (!zan_type_defines(g, "Math", math1[mi]) &&
                    is_call_to(expr, "Math", math1[mi]) && expr->call.args.count == 1) {
                    LLVMValueRef arg = emit_expr(g, expr->call.args.items[0], locals);
                    LLVMTypeRef dbl = LLVMDoubleTypeInContext(g->ctx);
                    if (LLVMGetTypeKind(LLVMTypeOf(arg)) == LLVMFloatTypeKind) {
                        arg = LLVMBuildFPExt(g->builder, arg, dbl, "ext");
                    } else if (LLVMGetTypeKind(LLVMTypeOf(arg)) != LLVMDoubleTypeKind) {
                        arg = LLVMBuildSIToFP(g->builder, arg, dbl, "tofp");
                    }
                    LLVMTypeRef fty = LLVMFunctionType(dbl, (LLVMTypeRef[]){ dbl }, 1, 0);
                    LLVMValueRef fn = LLVMGetNamedFunction(g->mod, math1[mi + 1]);
                    if (!fn) fn = LLVMAddFunction(g->mod, math1[mi + 1], fty);
                    return zan_call2(g->builder, fty, fn, &arg, 1, math1[mi + 1]);
                }
            }
        }

        /* Math.Abs(expr) */
        if (!zan_type_defines(g, "Math", "Abs") &&
            is_call_to(expr, "Math", "Abs") && expr->call.args.count == 1) {
            LLVMValueRef arg = emit_expr(g, expr->call.args.items[0], locals);
            LLVMTypeRef arg_type = LLVMTypeOf(arg);
            /* only numeric kinds reach the lowering below; a pointer-shaped
             * operand would hit GetIntTypeWidth on an opaque pointer */
            if (LLVMGetTypeKind(arg_type) != LLVMIntegerTypeKind &&
                LLVMGetTypeKind(arg_type) != LLVMDoubleTypeKind &&
                LLVMGetTypeKind(arg_type) != LLVMFloatTypeKind) {
                zan_diag_emit(g->diag, DIAG_ERROR, expr->loc,
                              "Math.Abs requires an int or floating-point "
                              "argument");
                return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
            }
            if (LLVMGetTypeKind(arg_type) == LLVMDoubleTypeKind ||
                LLVMGetTypeKind(arg_type) == LLVMFloatTypeKind) {
                LLVMTypeRef dbl = LLVMDoubleTypeInContext(g->ctx);
                if (LLVMGetTypeKind(arg_type) == LLVMFloatTypeKind)
                    arg = LLVMBuildFPExt(g->builder, arg, dbl, "ext");
                LLVMValueRef fabs_fn = LLVMGetNamedFunction(g->mod, "fabs");
                if (!fabs_fn) {
                    LLVMTypeRef fabs_type = LLVMFunctionType(dbl, (LLVMTypeRef[]){ dbl }, 1, 0);
                    fabs_fn = LLVMAddFunction(g->mod, "fabs", fabs_type);
                }
                return zan_call2(g->builder, LLVMFunctionType(dbl, (LLVMTypeRef[]){ dbl }, 1, 0),
                    fabs_fn, &arg, 1, "fabs");
            } else {
                /* integer abs: (x ^ (x >> w-1)) - (x >> w-1), where the shift
                 * must span the operand's own width -- a hardcoded 31 leaves
                 * a long's sign bits in the mask. */
                unsigned abs_bits = LLVMGetIntTypeWidth(arg_type);
                LLVMValueRef shift = zan_ashr(g->builder, arg,
                    LLVMConstInt(arg_type, abs_bits - 1, 0), "sh");
                LLVMValueRef xor = zan_xor(g->builder, arg, shift, "xor");
                return zan_sub(g->builder, xor, shift, "abs");
            }
        }

        /* Math.Max(a, b) / Math.Min(a, b), on integers or on doubles: mixing
         * the two widens to double, the way C# picks the double overload. */
        if (!zan_type_defines(g, "Math", "Max") && !zan_type_defines(g, "Math", "Min") &&
            (is_call_to(expr, "Math", "Max") || is_call_to(expr, "Math", "Min")) &&
            expr->call.args.count == 2) {
            int want_max = is_call_to(expr, "Math", "Max");
            LLVMValueRef a = emit_expr(g, expr->call.args.items[0], locals);
            LLVMValueRef b = emit_expr(g, expr->call.args.items[1], locals);
            LLVMTypeKind ka = LLVMGetTypeKind(LLVMTypeOf(a));
            LLVMTypeKind kb = LLVMGetTypeKind(LLVMTypeOf(b));
            int fa = (ka == LLVMDoubleTypeKind || ka == LLVMFloatTypeKind);
            int fb = (kb == LLVMDoubleTypeKind || kb == LLVMFloatTypeKind);
            if (!fa && !fb &&
                ka != LLVMIntegerTypeKind && kb != LLVMIntegerTypeKind) {
                /* pointer/class operands: GetIntTypeWidth on an opaque
                 * pointer would abort; diagnose instead */
                zan_diag_emit(g->diag, DIAG_ERROR, expr->loc,
                              "Math.%s requires numeric arguments",
                              want_max ? "Max" : "Min");
                return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
            }
            if (fa || fb) {
                LLVMTypeRef dbl = LLVMDoubleTypeInContext(g->ctx);
                a = fa ? (ka == LLVMFloatTypeKind
                            ? LLVMBuildFPExt(g->builder, a, dbl, "ext")
                            : a)
                       : LLVMBuildSIToFP(g->builder, a, dbl, "tofp");
                b = fb ? (kb == LLVMFloatTypeKind
                            ? LLVMBuildFPExt(g->builder, b, dbl, "ext")
                            : b)
                       : LLVMBuildSIToFP(g->builder, b, dbl, "tofp");
                LLVMValueRef cmp = LLVMBuildFCmp(g->builder,
                    want_max ? LLVMRealOGT : LLVMRealOLT, a, b, "cmp");
                return LLVMBuildSelect(g->builder, cmp, a, b,
                    want_max ? "max" : "min");
            }
            /* Integers of different widths (Math.Max(anInt, aLong)) can be
             * neither compared nor selected between as they are. */
            unsigned wa = LLVMGetIntTypeWidth(LLVMTypeOf(a));
            unsigned wb = LLVMGetIntTypeWidth(LLVMTypeOf(b));
            if (wa < wb) a = LLVMBuildSExt(g->builder, a, LLVMTypeOf(b), "sx");
            else if (wb < wa) b = LLVMBuildSExt(g->builder, b, LLVMTypeOf(a), "sx");
            LLVMValueRef cmp = zan_icmp(g->builder,
                want_max ? LLVMIntSGT : LLVMIntSLT, a, b, "cmp");
            return LLVMBuildSelect(g->builder, cmp, a, b, want_max ? "max" : "min");
        }

        /* Math.Pow(base, exp) */
        if (!zan_type_defines(g, "Math", "Pow") &&
            is_call_to(expr, "Math", "Pow") && expr->call.args.count == 2) {
            LLVMTypeRef dbl = LLVMDoubleTypeInContext(g->ctx);
            LLVMValueRef base_v = emit_expr(g, expr->call.args.items[0], locals);
            LLVMValueRef exp_v = emit_expr(g, expr->call.args.items[1], locals);
            if (LLVMGetTypeKind(LLVMTypeOf(base_v)) == LLVMIntegerTypeKind)
                base_v = LLVMBuildSIToFP(g->builder, base_v, dbl, "tofp");
            if (LLVMGetTypeKind(LLVMTypeOf(exp_v)) == LLVMIntegerTypeKind)
                exp_v = LLVMBuildSIToFP(g->builder, exp_v, dbl, "tofp");
            LLVMValueRef pow_fn = LLVMGetNamedFunction(g->mod, "pow");
            if (!pow_fn) {
                LLVMTypeRef pow_args[] = { dbl, dbl };
                LLVMTypeRef pow_type = LLVMFunctionType(dbl, pow_args, 2, 0);
                pow_fn = LLVMAddFunction(g->mod, "pow", pow_type);
            }
            LLVMValueRef args[] = { base_v, exp_v };
            return zan_call2(g->builder, LLVMFunctionType(dbl, (LLVMTypeRef[]){ dbl, dbl }, 2, 0),
                pow_fn, args, 2, "pow");
        }

        /* Math.Floor(x) */
        if (!zan_type_defines(g, "Math", "Floor") &&
            is_call_to(expr, "Math", "Floor") && expr->call.args.count == 1) {
            LLVMTypeRef dbl = LLVMDoubleTypeInContext(g->ctx);
            LLVMValueRef arg = emit_expr(g, expr->call.args.items[0], locals);
            if (LLVMGetTypeKind(LLVMTypeOf(arg)) == LLVMIntegerTypeKind)
                arg = LLVMBuildSIToFP(g->builder, arg, dbl, "tofp");
            LLVMValueRef floor_fn = LLVMGetNamedFunction(g->mod, "floor");
            if (!floor_fn) {
                LLVMTypeRef floor_type = LLVMFunctionType(dbl, (LLVMTypeRef[]){ dbl }, 1, 0);
                floor_fn = LLVMAddFunction(g->mod, "floor", floor_type);
            }
            return zan_call2(g->builder, LLVMFunctionType(dbl, (LLVMTypeRef[]){ dbl }, 1, 0),
                floor_fn, &arg, 1, "floor");
        }

        /* Math.Ceiling(x) */
        if (!zan_type_defines(g, "Math", "Ceiling") &&
            is_call_to(expr, "Math", "Ceiling") && expr->call.args.count == 1) {
            LLVMTypeRef dbl = LLVMDoubleTypeInContext(g->ctx);
            LLVMValueRef arg = emit_expr(g, expr->call.args.items[0], locals);
            if (LLVMGetTypeKind(LLVMTypeOf(arg)) == LLVMIntegerTypeKind)
                arg = LLVMBuildSIToFP(g->builder, arg, dbl, "tofp");
            LLVMValueRef ceil_fn = LLVMGetNamedFunction(g->mod, "ceil");
            if (!ceil_fn) {
                LLVMTypeRef ceil_type = LLVMFunctionType(dbl, (LLVMTypeRef[]){ dbl }, 1, 0);
                ceil_fn = LLVMAddFunction(g->mod, "ceil", ceil_type);
            }
            return zan_call2(g->builder, LLVMFunctionType(dbl, (LLVMTypeRef[]){ dbl }, 1, 0),
                ceil_fn, &arg, 1, "ceil");
        }

        /* Math.Round(x[, digits]) */
        if (!zan_type_defines(g, "Math", "Round") &&
            is_call_to(expr, "Math", "Round") &&
            (expr->call.args.count == 1 || expr->call.args.count == 2)) {
            LLVMTypeRef dbl = LLVMDoubleTypeInContext(g->ctx);
            LLVMTypeRef d1_ty = LLVMFunctionType(dbl, (LLVMTypeRef[]){ dbl }, 1, 0);
            LLVMValueRef arg = emit_expr(g, expr->call.args.items[0], locals);
            if (LLVMGetTypeKind(LLVMTypeOf(arg)) == LLVMIntegerTypeKind)
                arg = LLVMBuildSIToFP(g->builder, arg, dbl, "tofp");
            else if (LLVMGetTypeKind(LLVMTypeOf(arg)) == LLVMFloatTypeKind)
                arg = LLVMBuildFPExt(g->builder, arg, dbl, "ext");
            LLVMValueRef round_fn = get_libc_fn(g, "round", d1_ty);
            if (expr->call.args.count == 1)
                return zan_call2(g->builder, d1_ty, round_fn, &arg, 1, "round");
            /* digits: round(x * 10^d) / 10^d */
            LLVMValueRef digits = emit_expr(g, expr->call.args.items[1], locals);
            if (LLVMGetTypeKind(LLVMTypeOf(digits)) == LLVMIntegerTypeKind)
                digits = LLVMBuildSIToFP(g->builder, digits, dbl, "dtofp");
            LLVMTypeRef d2_ty = LLVMFunctionType(dbl, (LLVMTypeRef[]){ dbl, dbl }, 2, 0);
            LLVMValueRef pow_fn = get_libc_fn(g, "pow", d2_ty);
            LLVMValueRef scale = zan_call2(g->builder, d2_ty, pow_fn,
                (LLVMValueRef[]){ LLVMConstReal(dbl, 10.0), digits }, 2, "scale");
            LLVMValueRef scaled = LLVMBuildFMul(g->builder, arg, scale, "scaled");
            LLVMValueRef r = zan_call2(g->builder, d1_ty, round_fn, &scaled, 1, "round");
            return LLVMBuildFDiv(g->builder, r, scale, "rdig");
        }

        /* Convert.ToDouble(x) */
        if (!zan_type_defines(g, "Convert", "ToDouble") &&
            is_call_to(expr, "Convert", "ToDouble") && expr->call.args.count == 1) {
            LLVMTypeRef dbl = LLVMDoubleTypeInContext(g->ctx);
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMValueRef arg = emit_expr(g, expr->call.args.items[0], locals);
            LLVMTypeKind ak = LLVMGetTypeKind(LLVMTypeOf(arg));
            if (ak == LLVMPointerTypeKind) {
                LLVMTypeRef strtod_ty = LLVMFunctionType(dbl,
                    (LLVMTypeRef[]){ i8ptr, LLVMPointerType(i8ptr, 0) }, 2, 0);
                LLVMValueRef strtod_fn = get_libc_fn(g, "strtod", strtod_ty);
                LLVMValueRef r = zan_call2(g->builder, strtod_ty, strtod_fn,
                    (LLVMValueRef[]){ arg, LLVMConstNull(LLVMPointerType(i8ptr, 0)) }, 2, "todbl");
                emit_release_owned_call_temp(g, expr->call.args.items[0], arg, locals);
                return r;
            }
            if (ak == LLVMIntegerTypeKind)
                return LLVMBuildSIToFP(g->builder, arg, dbl, "todbl");
            if (ak == LLVMFloatTypeKind)
                return LLVMBuildFPExt(g->builder, arg, dbl, "todbl");
            return arg;
        }

        /* int/long/Int32/Int64.Parse(s) and double/float/Double.Parse(s);
         * matching TryParse(s, out v) returning bool. */
        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS &&
            expr->call.callee->member.object->kind == AST_IDENTIFIER &&
            !local_find(locals, expr->call.callee->member.object->ident.name)) {
            zan_istr_t rn = expr->call.callee->member.object->ident.name;
            zan_istr_t pm = expr->call.callee->member.name;
            int int_recv = (rn.len == 3 && memcmp(rn.str, "int", 3) == 0) ||
                           (rn.len == 4 && memcmp(rn.str, "long", 4) == 0) ||
                           (rn.len == 5 && memcmp(rn.str, "Int32", 5) == 0) ||
                           (rn.len == 5 && memcmp(rn.str, "Int64", 5) == 0);
            int dbl_recv = (rn.len == 6 && memcmp(rn.str, "double", 6) == 0) ||
                           (rn.len == 5 && memcmp(rn.str, "float", 5) == 0) ||
                           (rn.len == 6 && memcmp(rn.str, "Double", 6) == 0) ||
                           (rn.len == 6 && memcmp(rn.str, "Single", 6) == 0);
            if ((int_recv || dbl_recv) &&
                pm.len == 5 && memcmp(pm.str, "Parse", 5) == 0 &&
                expr->call.args.count == 1) {
                LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                LLVMTypeRef i8pp = LLVMPointerType(i8ptr, 0);
                LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                LLVMTypeRef dbl = LLVMDoubleTypeInContext(g->ctx);
                LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
                LLVMValueRef s = emit_expr(g, expr->call.args.items[0], locals);
                LLVMValueRef r;
                if (int_recv) {
                    LLVMTypeRef strtoll_ty = LLVMFunctionType(i64,
                        (LLVMTypeRef[]){ i8ptr, i8pp, i32t }, 3, 0);
                    LLVMValueRef f = get_libc_fn(g, "strtoll", strtoll_ty);
                    r = zan_call2(g->builder, strtoll_ty, f,
                        (LLVMValueRef[]){ s, LLVMConstNull(i8pp),
                                          LLVMConstInt(i32t, 10, 0) }, 3, "parse");
                } else {
                    LLVMTypeRef strtod_ty = LLVMFunctionType(dbl,
                        (LLVMTypeRef[]){ i8ptr, i8pp }, 2, 0);
                    LLVMValueRef f = get_libc_fn(g, "strtod", strtod_ty);
                    r = zan_call2(g->builder, strtod_ty, f,
                        (LLVMValueRef[]){ s, LLVMConstNull(i8pp) }, 2, "parse");
                }
                emit_release_owned_call_temp(g, expr->call.args.items[0], s, locals);
                return r;
            }
            if ((int_recv || dbl_recv) &&
                pm.len == 8 && memcmp(pm.str, "TryParse", 8) == 0 &&
                expr->call.args.count == 2) {
                LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
                LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
                LLVMTypeRef i8pp = LLVMPointerType(i8ptr, 0);
                LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                LLVMTypeRef dbl = LLVMDoubleTypeInContext(g->ctx);
                LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
                LLVMValueRef s = emit_expr(g, expr->call.args.items[0], locals);
                LLVMValueRef outp = emit_expr(g, expr->call.args.items[1], locals);
                LLVMValueRef endp = emit_entry_alloca(g, i8ptr, "endp");
                LLVMBuildStore(g->builder, LLVMConstNull(i8ptr), endp);
                LLVMValueRef v;
                if (int_recv) {
                    LLVMTypeRef strtoll_ty = LLVMFunctionType(i64,
                        (LLVMTypeRef[]){ i8ptr, i8pp, i32t }, 3, 0);
                    LLVMValueRef f = get_libc_fn(g, "strtoll", strtoll_ty);
                    v = zan_call2(g->builder, strtoll_ty, f,
                        (LLVMValueRef[]){ s, endp, LLVMConstInt(i32t, 10, 0) }, 3, "tp");
                } else {
                    LLVMTypeRef strtod_ty = LLVMFunctionType(dbl,
                        (LLVMTypeRef[]){ i8ptr, i8pp }, 2, 0);
                    LLVMValueRef f = get_libc_fn(g, "strtod", strtod_ty);
                    v = zan_call2(g->builder, strtod_ty, f,
                        (LLVMValueRef[]){ s, endp }, 2, "tp");
                }
                LLVMValueRef e = LLVMBuildLoad2(g->builder, i8ptr, endp, "e");
                LLVMValueRef moved = zan_icmp(g->builder, LLVMIntNE, e, s, "moved");
                LLVMValueRef ch = LLVMBuildLoad2(g->builder, i8, e, "ch");
                LLVMValueRef at_end = zan_icmp(g->builder, LLVMIntEQ, ch,
                    LLVMConstInt(i8, 0, 0), "atend");
                LLVMValueRef ok = zan_and(g->builder, moved, at_end, "ok");
                LLVMValueRef stored = LLVMBuildSelect(g->builder, ok, v,
                    int_recv ? LLVMConstInt(i64, 0, 0) : (LLVMValueRef)LLVMConstReal(dbl, 0.0), "tpv");
                if (LLVMGetTypeKind(LLVMTypeOf(outp)) == LLVMPointerTypeKind)
                    LLVMBuildStore(g->builder, stored, outp);
                emit_release_owned_call_temp(g, expr->call.args.items[0], s, locals);
                return ok;
            }
        }

        /* string.IsNullOrEmpty / string.Join / string.Format statics. */
        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS &&
            expr->call.callee->member.object->kind == AST_IDENTIFIER &&
            !local_find(locals, expr->call.callee->member.object->ident.name)) {
            zan_istr_t rn = expr->call.callee->member.object->ident.name;
            int str_recv = (rn.len == 6 && memcmp(rn.str, "string", 6) == 0) ||
                           (rn.len == 6 && memcmp(rn.str, "String", 6) == 0);
            zan_istr_t pm = expr->call.callee->member.name;
            if (str_recv && pm.len == 13 &&
                memcmp(pm.str, "IsNullOrEmpty", 13) == 0 &&
                expr->call.args.count == 1) {
                LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
                LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
                LLVMValueRef s = emit_expr(g, expr->call.args.items[0], locals);
                LLVMValueRef isnull = zan_icmp(g->builder, LLVMIntEQ, s,
                    LLVMConstNull(i8ptr), "isnull");
                /* empty check must not deref null: select null ? true : *s==0 */
                LLVMBasicBlockRef chk_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "sne.chk");
                LLVMBasicBlockRef done_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "sne.done");
                LLVMBasicBlockRef cur_bb = LLVMGetInsertBlock(g->builder);
                LLVMBuildCondBr(g->builder, isnull, done_bb, chk_bb);
                LLVMPositionBuilderAtEnd(g->builder, chk_bb);
                LLVMValueRef ch = LLVMBuildLoad2(g->builder, i8, s, "ch");
                LLVMValueRef isempty = zan_icmp(g->builder, LLVMIntEQ, ch,
                    LLVMConstInt(i8, 0, 0), "isempty");
                LLVMBasicBlockRef chk_end = LLVMGetInsertBlock(g->builder);
                LLVMBuildBr(g->builder, done_bb);
                LLVMPositionBuilderAtEnd(g->builder, done_bb);
                LLVMValueRef phi = LLVMBuildPhi(g->builder, LLVMInt1TypeInContext(g->ctx), "sne");
                LLVMValueRef inv[2] = { LLVMConstInt(LLVMInt1TypeInContext(g->ctx), 1, 0), isempty };
                LLVMBasicBlockRef inb[2] = { cur_bb, chk_end };
                LLVMAddIncoming(phi, inv, inb, 2);
                emit_release_owned_call_temp(g, expr->call.args.items[0], s, locals);
                return phi;
            }
            if (str_recv && pm.len == 4 && memcmp(pm.str, "Join", 4) == 0 &&
                expr->call.args.count == 2) {
                LLVMValueRef sep = emit_expr(g, expr->call.args.items[0], locals);
                LLVMValueRef lst = emit_expr(g, expr->call.args.items[1], locals);
                LLVMValueRef hf = get_str_join_fn(g);
                LLVMValueRef res = zan_call2(g->builder,
                    LLVMGlobalGetValueType(hf), hf,
                    (LLVMValueRef[]){ sep, lst }, 2, "join");
                emit_release_owned_call_temp(g, expr->call.args.items[0], sep, locals);
                emit_release_owned_call_temp(g, expr->call.args.items[1], lst, locals);
                return res;
            }
            if (str_recv && pm.len == 6 && memcmp(pm.str, "Format", 6) == 0 &&
                expr->call.args.count >= 1 &&
                expr->call.args.items[0]->kind == AST_STRING_LITERAL) {
                /* compile-time expansion of a literal format: split on {N}
                 * placeholders and concatenate pieces with stringified args.
                 * `res` always holds an owned (+1) string; each fold releases
                 * the previous accumulator and any owned piece. */
                zan_istr_t f = expr->call.args.items[0]->str_val;
                zan_type_t *str_ty = g->binder->type_string;
                LLVMValueRef res = NULL;
                int seg_start = 0;
                for (int i = 0; i < f.len; i++) {
                    if (f.str[i] == '{' && i + 2 < f.len + 1) {
                        int j = i + 1, idx = 0, has = 0, oflow = 0;
                        while (j < f.len && f.str[j] >= '0' && f.str[j] <= '9') {
                            /* A huge digit run must not wrap `idx` negative:
                             * once it cannot name a valid argument, stop
                             * accumulating (keep consuming so the '}' check
                             * still sees the real token stream) and treat the
                             * whole placeholder as literal text. */
                            if (!oflow) {
                                if (idx > 214748363 ||
                                    idx >= expr->call.args.count) oflow = 1;
                                else idx = idx * 10 + (f.str[j] - '0');
                            }
                            j++; has = 1;
                        }
                        if (has && !oflow && j < f.len && f.str[j] == '}' &&
                            idx + 1 < expr->call.args.count) {
                            if (i > seg_start) {
                                zan_istr_t seg = { f.str + seg_start, i - seg_start };
                                LLVMValueRef sl = emit_string_literal_rc(g, seg);
                                if (res) {
                                    LLVMValueRef nr = emit_str_concat(g, res, sl);
                                    emit_rc_release_for_type(g, str_ty, res);
                                    emit_rc_release_for_type(g, str_ty, sl);
                                    res = nr;
                                } else {
                                    res = sl;
                                }
                            }
                            zan_ast_node_t *arg_ast = expr->call.args.items[idx + 1];
                            LLVMValueRef av = emit_expr(g, arg_ast, locals);
                            LLVMValueRef as = emit_to_cstr_of(g, av, arg_ast, locals);
                            int as_owned =
                                LLVMGetTypeKind(LLVMTypeOf(av)) != LLVMPointerTypeKind ||
                                expr_yields_owned_rc_value(g, arg_ast, locals);
                            if (res) {
                                LLVMValueRef nr = emit_str_concat(g, res, as);
                                emit_rc_release_for_type(g, str_ty, res);
                                if (as_owned)
                                    emit_rc_release_for_type(g, str_ty, as);
                                res = nr;
                            } else if (as_owned) {
                                res = as;
                            } else {
                                emit_rc_retain_for_type(g, str_ty, as);
                                res = as;
                            }
                            i = j;
                            seg_start = j + 1;
                        }
                    }
                }
                if (seg_start < f.len || !res) {
                    zan_istr_t seg = { f.str + seg_start, f.len - seg_start };
                    LLVMValueRef sl = emit_string_literal_rc(g, seg);
                    if (res) {
                        LLVMValueRef nr = emit_str_concat(g, res, sl);
                        emit_rc_release_for_type(g, str_ty, res);
                        emit_rc_release_for_type(g, str_ty, sl);
                        res = nr;
                    } else {
                        res = sl;
                    }
                }
                return res;
            }
        }

        /* String methods: str.Substring(start, len), str.Contains(sub), str.IndexOf(ch) */
        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS) {
            zan_ast_node_t *callee = expr->call.callee;
            zan_istr_t method_name = callee->member.name;
            
            /* Check if calling on a local variable of string type */
            if (callee->member.object->kind == AST_IDENTIFIER) {
                local_var_t *str_local = local_find(locals, callee->member.object->ident.name);
                if (str_local && LLVMGetTypeKind(LLVMTypeOf(LLVMBuildLoad2(g->builder,
                    LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0), str_local->alloca, "chk"))) == LLVMPointerTypeKind) {
                    /* It's a pointer — might be a string */
                    /* DON'T emit load twice; skip for now, let it fall through to general handler */
                }
            }
            
            /* Convert.ToInt / Convert.ToInt32 / Convert.ToInt64(x) */
            if (callee->member.object->kind == AST_IDENTIFIER) {
                zan_istr_t obj_name = callee->member.object->ident.name;
                /* A program of its own may define some of Convert: each
                 * lowering steps aside only for the method that program
                 * declares, so defining ToInt32 does not take ToString with
                 * it (which left every stdlib Convert.ToString unresolved). */
                if (obj_name.len == 7 && memcmp(obj_name.str, "Convert", 7) == 0) {
                    if (((method_name.len == 7 && memcmp(method_name.str, "ToInt32", 7) == 0) ||
                         (method_name.len == 7 && memcmp(method_name.str, "ToInt64", 7) == 0) ||
                         (method_name.len == 5 && memcmp(method_name.str, "ToInt", 5) == 0)) &&
                        !zan_type_defines(g, "Convert", "ToInt32") &&
                        !zan_type_defines(g, "Convert", "ToInt64") &&
                        !zan_type_defines(g, "Convert", "ToInt") &&
                        expr->call.args.count == 1) {
                        LLVMValueRef arg = emit_expr(g, expr->call.args.items[0], locals);
                        LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
                        LLVMTypeKind ak = LLVMGetTypeKind(LLVMTypeOf(arg));
                        LLVMValueRef parsed;
                        if (ak == LLVMPointerTypeKind) {
                            /* string -> strtoll(s, NULL, 10): full signed 64-bit.
                             * atoi returned i32 and, when a stdlib extern had
                             * declared it i64, left garbage in the upper bits --
                             * so values > 2^31 (and some negatives) came back
                             * corrupt. */
                            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                            LLVMTypeRef i8pp = LLVMPointerType(i8ptr, 0);
                            LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
                            LLVMTypeRef strtoll_ty = LLVMFunctionType(i64t,
                                (LLVMTypeRef[]){ i8ptr, i8pp, i32t }, 3, 0);
                            LLVMValueRef f = get_libc_fn(g, "strtoll", strtoll_ty);
                            parsed = zan_call2(g->builder, strtoll_ty, f,
                                (LLVMValueRef[]){ arg, LLVMConstNull(i8pp),
                                                  LLVMConstInt(i32t, 10, 0) }, 3, "toint");
                        } else if (ak == LLVMDoubleTypeKind || ak == LLVMFloatTypeKind) {
                            parsed = LLVMBuildFPToSI(g->builder, arg, i64t, "toint");
                        } else if (ak == LLVMIntegerTypeKind) {
                            parsed = (LLVMTypeOf(arg) == i64t) ? arg
                                : LLVMBuildSExt(g->builder, arg, i64t, "toint");
                        } else {
                            parsed = LLVMConstInt(i64t, 0, 0);
                        }
                        emit_release_owned_call_temp(
                            g, expr->call.args.items[0], arg, locals);
                        return parsed;
                    }
                    if (method_name.len == 8 && memcmp(method_name.str, "ToString", 8) == 0 &&
                        !zan_type_defines(g, "Convert", "ToString") &&
                        expr->call.args.count == 1) {
                        LLVMValueRef arg = emit_expr(g, expr->call.args.items[0], locals);
                        LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                        LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                        if (LLVMGetTypeKind(LLVMTypeOf(arg)) == LLVMPointerTypeKind) {
                            /* string receiver: return an owned copy, exactly
                             * like `s.ToString()` (the numeric branch below
                             * would format the pointer with %lld). */
                            LLVMTypeRef strlen_ty = LLVMFunctionType(i64,
                                (LLVMTypeRef[]){ i8ptr }, 1, 0);
                            LLVMTypeRef memcpy_ty = LLVMFunctionType(i8ptr,
                                (LLVMTypeRef[]){ i8ptr, i8ptr, i64 }, 3, 0);
                            LLVMValueRef memcpy_fn = get_libc_fn(g, "memcpy", memcpy_ty);
                            LLVMValueRef n = zan_call2(g->builder, strlen_ty,
                                g->fn_strlen, &arg, 1, "n");
                            LLVMValueRef bufsz = zan_add(g->builder, n,
                                LLVMConstInt(i64, 1, 0), "bsz");
                            LLVMValueRef buf = emit_string_alloc_rc(g, bufsz);
                            LLVMValueRef mcargs[] = { buf, arg, bufsz };
                            zan_call2(g->builder, memcpy_ty, memcpy_fn, mcargs, 3, "");
                            emit_release_owned_call_temp(
                                g, expr->call.args.items[0], arg, locals);
                            return buf;
                        }
                        /* char renders as the character (C#), not as the
                         * decimal code the numeric branch would print. */
                        if (expr_is_char(g, expr->call.args.items[0], locals))
                            return emit_char_to_cstr(g, arg);
                        /* allocate buffer and sprintf */
                        LLVMValueRef buf_size = LLVMConstInt(i64, 32, 0);
                        LLVMValueRef buf = emit_string_alloc_rc(g, buf_size);
                        LLVMTypeKind atk = LLVMGetTypeKind(LLVMTypeOf(arg));
                        LLVMValueRef fmt;
                        LLVMValueRef num_arg = arg;
                        if (atk == LLVMDoubleTypeKind || atk == LLVMFloatTypeKind) {
                            /* floating point: print with %g (varargs promote
                             * float to double), matching Console.WriteLine. */
                            fmt = LLVMBuildGlobalStringPtr(g->builder, "%g", "ftoa_fmt");
                            if (atk == LLVMFloatTypeKind) {
                                num_arg = LLVMBuildFPExt(g->builder, arg,
                                    LLVMDoubleTypeInContext(g->ctx), "ext");
                            }
                        } else {
                            /* signed by default; a ulong argument formats
                             * unsigned or values >= 2^63 come out negative. */
                            bool is_ul = expr_is_ulong(
                                g, expr->call.args.items[0], locals);
                            emit_itoa_into(g, buf,
                                emit_widen_i64_for_print(g, arg), is_ul ? 1 : 0);
                            return buf;
                        }
                        LLVMValueRef sn_args[] = { buf, LLVMConstInt(i64, 32, 0), fmt, num_arg };
                        zan_call2(g->builder,
                            LLVMFunctionType(LLVMInt32TypeInContext(g->ctx),
                                (LLVMTypeRef[]){ i8ptr, i64, i8ptr }, 3, 1),
                            g->fn_snprintf, sn_args, 4, "");
                        return buf;
                    }
                }
            }
        }

        /* str.Substring(start[, len]) -> heap-allocated copy of the slice.
         * With one argument, copies from `start` to the end of the string. */
        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS) {
            zan_ast_node_t *sc = expr->call.callee;
            zan_istr_t sm = sc->member.name;
            if (sm.len == 9 && memcmp(sm.str, "Substring", 9) == 0 &&
                (expr->call.args.count == 1 || expr->call.args.count == 2) &&
                is_string_expr(g, sc->member.object, locals)) {
                LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
                LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
                LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                LLVMValueRef s = emit_expr(g, sc->member.object, locals);
                LLVMValueRef start = coerce_int_to(g,
                    emit_expr(g, expr->call.args.items[0], locals), i64);
                /* bounds: `start` in [0, strlen]; the two-argument form also
                 * needs `len >= 0` and `start+len <= strlen`. Without this the
                 * GEP+memcpy below read arbitrary heap (and a one-argument
                 * call with start > strlen computes a negative size). */
                /* A receiver without a reliable NUL bound (a raw FFI buffer
                 * typed as `string`, e.g. a `struct dirent*` sliced byte by
                 * byte) would fail the window check against strlen even though
                 * the read is in range -- the same policy the string index
                 * guards use. */
                bool bounded = expr_has_reliable_string_bounds(sc->member.object,
                                                              locals) != 0;
                LLVMValueRef total = (bounded || expr->call.args.count == 1)
                    ? emit_string_length(g, s) : NULL;
                LLVMValueRef slen;
                if (expr->call.args.count == 2) {
                    slen = coerce_int_to(g,
                        emit_expr(g, expr->call.args.items[1], locals), i64);
                    if (total)
                        emit_span_window_check(g, start, slen, total, expr->loc,
                                               "substring");
                } else {
                    emit_index_range_check(g, start, total, true, expr->loc,
                                           "substring");
                    slen = zan_sub(g->builder, total, start, "subl");
                }
                LLVMValueRef bufsz = zan_add(g->builder, slen, LLVMConstInt(i64, 1, 0), "bsz");
                LLVMValueRef buf = emit_string_alloc_rc(g, bufsz);
                LLVMValueRef srcp = LLVMBuildGEP2(g->builder, i8, s, &start, 1, "srcp");
                LLVMValueRef memcpy_fn = LLVMGetNamedFunction(g->mod, "memcpy");
                if (!memcpy_fn) {
                    memcpy_fn = LLVMAddFunction(g->mod, "memcpy",
                        LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr, i64 }, 3, 0));
                }
                LLVMValueRef mcargs[] = { buf, srcp, slen };
                zan_call2(g->builder,
                    LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr, i64 }, 3, 0),
                    memcpy_fn, mcargs, 3, "");
                LLVMValueRef endp = LLVMBuildGEP2(g->builder, i8, buf, &slen, 1, "endp");
                LLVMBuildStore(g->builder, LLVMConstInt(i8, 0, 0), endp);
                /* the slice was cut inside a NUL-free range, so `slen` is the
                 * result's own length */
                emit_string_len_set(g, buf, slen);
                emit_release_owned_call_temp(g, sc->member.object, s, locals);
                return buf;
            }
        }

        /* str.Contains(sub) -> bool: substring search via strstr. Without
         * this, the call fell through to the generic fallback and lowered to
         * constant false for multi-character needles. */
        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS) {
            zan_ast_node_t *sc = expr->call.callee;
            zan_istr_t sm = sc->member.name;
            if (sm.len == 8 && memcmp(sm.str, "Contains", 8) == 0 &&
                expr->call.args.count == 1 &&
                is_string_expr(g, sc->member.object, locals) &&
                is_string_like_expr(g, expr->call.args.items[0], locals)) {
                LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                LLVMValueRef s = emit_expr(g, sc->member.object, locals);
                int sub_owned;
                LLVMValueRef sub = emit_string_like_arg(g, expr->call.args.items[0],
                                                        locals, &sub_owned);
                LLVMTypeRef strstr_type = LLVMFunctionType(i8ptr,
                    (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0);
                LLVMValueRef strstr_fn = LLVMGetNamedFunction(g->mod, "strstr");
                if (!strstr_fn)
                    strstr_fn = LLVMAddFunction(g->mod, "strstr", strstr_type);
                LLVMValueRef ss_args[] = { s, sub };
                LLVMValueRef hit = zan_call2(g->builder, strstr_type,
                    strstr_fn, ss_args, 2, "ss");
                LLVMValueRef res = zan_icmp(g->builder, LLVMIntNE, hit,
                    LLVMConstPointerNull(i8ptr), "ctn");
                emit_release_owned_call_temp(g, sc->member.object, s, locals);
                release_string_like_arg(g, expr->call.args.items[0], sub,
                                        sub_owned, locals);
                return res;
            }
        }

        /* str.IndexOf(sub) / str.LastIndexOf(sub) -> i64 byte index or -1. */
        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS) {
            zan_ast_node_t *sc = expr->call.callee;
            zan_istr_t sm = sc->member.name;
            int is_idx = (sm.len == 7 && memcmp(sm.str, "IndexOf", 7) == 0);
            int is_lidx = (sm.len == 11 && memcmp(sm.str, "LastIndexOf", 11) == 0);
            if (is_idx && (expr->call.args.count == 1 || expr->call.args.count == 2) &&
                is_string_expr(g, sc->member.object, locals) &&
                is_string_like_expr(g, expr->call.args.items[0], locals)) {
                LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                LLVMValueRef s = emit_expr(g, sc->member.object, locals);
                LLVMValueRef search = s;
                LLVMValueRef start = LLVMConstInt(i64, 0, 0);
                LLVMValueRef valid_start = LLVMConstInt(LLVMInt1TypeInContext(g->ctx), 1, 0);
                if (expr->call.args.count == 2) {
                    start = emit_expr(g, expr->call.args.items[1], locals);
                    if (LLVMGetTypeKind(LLVMTypeOf(start)) == LLVMIntegerTypeKind &&
                        LLVMGetIntTypeWidth(LLVMTypeOf(start)) < 64)
                        start = LLVMBuildSExt(g->builder, start, i64, "idx.start");
                    LLVMValueRef slen = emit_string_length(g, s);
                    LLVMValueRef nonneg = zan_icmp(g->builder, LLVMIntSGE, start,
                        LLVMConstInt(i64, 0, 0), "idx.nonneg");
                    LLVMValueRef within = zan_icmp(g->builder, LLVMIntSLE, start,
                        slen, "idx.within");
                    valid_start = zan_and(g->builder, nonneg, within, "idx.valid");
                    search = LLVMBuildGEP2(g->builder, LLVMInt8TypeInContext(g->ctx),
                        s, &start, 1, "idx.search");
                }
                int sub_owned;
                LLVMValueRef sub = emit_string_like_arg(g, expr->call.args.items[0],
                                                        locals, &sub_owned);
                LLVMTypeRef strstr_type = LLVMFunctionType(i8ptr,
                    (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0);
                LLVMValueRef strstr_fn = get_libc_fn(g, "strstr", strstr_type);
                LLVMValueRef ss_args[] = { search, sub };
                LLVMValueRef hit = zan_call2(g->builder, strstr_type,
                    strstr_fn, ss_args, 2, "ss");
                LLVMValueRef diff = zan_sub(g->builder,
                    LLVMBuildPtrToInt(g->builder, hit, i64, "hi"),
                    LLVMBuildPtrToInt(g->builder, s, i64, "si"), "diff");
                LLVMValueRef missed = zan_icmp(g->builder, LLVMIntEQ, hit,
                    LLVMConstPointerNull(i8ptr), "missed");
                LLVMValueRef invalid_start = zan_icmp(g->builder, LLVMIntEQ,
                    valid_start, LLVMConstInt(LLVMInt1TypeInContext(g->ctx), 0, 0),
                    "idx.invalid");
                LLVMValueRef failed = zan_or(g->builder, missed,
                    invalid_start, "idx.failed");
                LLVMValueRef res = LLVMBuildSelect(g->builder, failed,
                    LLVMConstInt(i64, (uint64_t)-1, 1), diff, "idx");
                emit_release_owned_call_temp(g, sc->member.object, s, locals);
                release_string_like_arg(g, expr->call.args.items[0], sub,
                                        sub_owned, locals);
                return res;
            }
            if (is_lidx && expr->call.args.count == 1 &&
                is_string_expr(g, sc->member.object, locals) &&
                is_string_like_expr(g, expr->call.args.items[0], locals)) {
                LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                LLVMValueRef s = emit_expr(g, sc->member.object, locals);
                int sub_owned;
                LLVMValueRef sub = emit_string_like_arg(g, expr->call.args.items[0],
                                                        locals, &sub_owned);
                LLVMValueRef lif = get_str_last_index_of_fn(g);
                LLVMValueRef li_args[] = { s, sub };
                LLVMValueRef res = zan_call2(g->builder, LLVMGlobalGetValueType(lif),
                    lif, li_args, 2, "lidx");
                emit_release_owned_call_temp(g, sc->member.object, s, locals);
                release_string_like_arg(g, expr->call.args.items[0], sub,
                                        sub_owned, locals);
                return res;
            }
        }

        /* str.StartsWith(p) / str.EndsWith(p) -> bool. */
        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS) {
            zan_ast_node_t *sc = expr->call.callee;
            zan_istr_t sm = sc->member.name;
            int is_sw = (sm.len == 10 && memcmp(sm.str, "StartsWith", 10) == 0);
            int is_ew = (sm.len == 8 && memcmp(sm.str, "EndsWith", 8) == 0);
            if ((is_sw || is_ew) && expr->call.args.count == 1 &&
                is_string_expr(g, sc->member.object, locals) &&
                is_string_like_expr(g, expr->call.args.items[0], locals)) {
                LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
                LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
                LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
                LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                LLVMTypeRef strlen_ty = LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr }, 1, 0);
                LLVMValueRef s = emit_expr(g, sc->member.object, locals);
                int p_owned;
                LLVMValueRef p = emit_string_like_arg(g, expr->call.args.items[0],
                                                      locals, &p_owned);
                LLVMValueRef lp = zan_call2(g->builder, strlen_ty, g->fn_strlen, &p, 1, "lp");
                LLVMValueRef res;
                if (is_sw) {
                    LLVMTypeRef strncmp_ty = LLVMFunctionType(i32,
                        (LLVMTypeRef[]){ i8ptr, i8ptr, i64 }, 3, 0);
                    LLVMValueRef strncmp_fn = get_libc_fn(g, "strncmp", strncmp_ty);
                    LLVMValueRef nc_args[] = { s, p, lp };
                    LLVMValueRef cmp = zan_call2(g->builder, strncmp_ty,
                        strncmp_fn, nc_args, 3, "swcmp");
                    res = zan_icmp(g->builder, LLVMIntEQ, cmp,
                        LLVMConstInt(i32, 0, 0), "sw");
                } else {
                    LLVMValueRef ls = zan_call2(g->builder, strlen_ty, g->fn_strlen, &s, 1, "ls");
                    LLVMValueRef off = zan_sub(g->builder, ls, lp, "off");
                    LLVMValueRef fits = zan_icmp(g->builder, LLVMIntSGE, off,
                        LLVMConstInt(i64, 0, 0), "fits");
                    LLVMValueRef offc = LLVMBuildSelect(g->builder, fits, off,
                        LLVMConstInt(i64, 0, 0), "offc");
                    LLVMValueRef tail = LLVMBuildGEP2(g->builder, i8, s, &offc, 1, "tailp");
                    LLVMTypeRef strcmp_ty = LLVMFunctionType(i32,
                        (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0);
                    LLVMValueRef strcmp_fn = get_libc_fn(g, "strcmp", strcmp_ty);
                    LLVMValueRef sc_args[] = { tail, p };
                    LLVMValueRef cmp = zan_call2(g->builder, strcmp_ty,
                        strcmp_fn, sc_args, 2, "ewcmp");
                    LLVMValueRef eq = zan_icmp(g->builder, LLVMIntEQ, cmp,
                        LLVMConstInt(i32, 0, 0), "eweq");
                    res = zan_and(g->builder, fits, eq, "ew");
                }
                emit_release_owned_call_temp(g, sc->member.object, s, locals);
                release_string_like_arg(g, expr->call.args.items[0], p,
                                        p_owned, locals);
                return res;
            }
        }

        /* str.Replace(from, to) -> fresh rc string. */
        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS) {
            zan_ast_node_t *sc = expr->call.callee;
            zan_istr_t sm = sc->member.name;
            if (sm.len == 7 && memcmp(sm.str, "Replace", 7) == 0 &&
                expr->call.args.count == 2 &&
                is_string_expr(g, sc->member.object, locals) &&
                is_string_like_expr(g, expr->call.args.items[0], locals) &&
                is_string_like_expr(g, expr->call.args.items[1], locals)) {
                LLVMValueRef s = emit_expr(g, sc->member.object, locals);
                int from_owned, to_owned;
                LLVMValueRef from = emit_string_like_arg(g, expr->call.args.items[0],
                                                         locals, &from_owned);
                LLVMValueRef to = emit_string_like_arg(g, expr->call.args.items[1],
                                                       locals, &to_owned);
                LLVMValueRef rf = get_str_replace_fn(g);
                LLVMValueRef rargs[] = { s, from, to };
                LLVMValueRef res = zan_call2(g->builder,
                    LLVMGlobalGetValueType(rf), rf, rargs, 3, "repl");
                emit_release_owned_call_temp(g, sc->member.object, s, locals);
                release_string_like_arg(g, expr->call.args.items[0], from,
                                        from_owned, locals);
                release_string_like_arg(g, expr->call.args.items[1], to,
                                        to_owned, locals);
                return res;
            }
        }

        /* str.Trim() / str.ToUpper() / str.ToLower() -> fresh rc string. */
        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS &&
            expr->call.args.count == 0) {
            zan_ast_node_t *sc = expr->call.callee;
            zan_istr_t sm = sc->member.name;
            int is_trim = (sm.len == 4 && memcmp(sm.str, "Trim", 4) == 0);
            int is_up = (sm.len == 7 && memcmp(sm.str, "ToUpper", 7) == 0);
            int is_low = (sm.len == 7 && memcmp(sm.str, "ToLower", 7) == 0);
            if ((is_trim || is_up || is_low) &&
                is_string_expr(g, sc->member.object, locals)) {
                LLVMValueRef s = emit_expr(g, sc->member.object, locals);
                LLVMValueRef hf = is_trim ? get_str_trim_fn(g)
                                          : get_str_case_fn(g, is_up);
                LLVMValueRef res = zan_call2(g->builder,
                    LLVMGlobalGetValueType(hf), hf, &s, 1, "strh");
                emit_release_owned_call_temp(g, sc->member.object, s, locals);
                return res;
            }
        }

        /* str.Split(sep) -> fresh List<string> of the separated segments. */
        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS &&
            expr->call.args.count == 1) {
            zan_ast_node_t *sc = expr->call.callee;
            zan_istr_t sm = sc->member.name;
            if (sm.len == 5 && memcmp(sm.str, "Split", 5) == 0 &&
                is_string_expr(g, sc->member.object, locals) &&
                is_string_like_expr(g, expr->call.args.items[0], locals)) {
                LLVMValueRef s = emit_expr(g, sc->member.object, locals);
                int sep_owned;
                LLVMValueRef sep = emit_string_like_arg(g, expr->call.args.items[0],
                                                        locals, &sep_owned);
                LLVMValueRef lst = emit_alloc_rc_collection(g, expr, 24, 1,
                                                            g->binder->type_string);
                LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                LLVMValueRef lp = LLVMBuildBitCast(g->builder, lst,
                    LLVMPointerType(g->list_struct_type, 0), "lp");
                LLVMBuildStore(g->builder, LLVMConstInt(i64, 0, 0),
                    LLVMBuildStructGEP2(g->builder, g->list_struct_type, lp, 0, "cnt"));
                LLVMBuildStore(g->builder, LLVMConstInt(i64, 0, 0),
                    LLVMBuildStructGEP2(g->builder, g->list_struct_type, lp, 1, "cap"));
                LLVMBuildStore(g->builder, LLVMConstNull(LLVMPointerType(i64, 0)),
                    LLVMBuildStructGEP2(g->builder, g->list_struct_type, lp, 2, "df"));
                LLVMValueRef hf = get_str_split_fn(g);
                zan_call2(g->builder, LLVMGlobalGetValueType(hf), hf,
                    (LLVMValueRef[]){ s, sep, lst }, 3, "");
                emit_release_owned_call_temp(g, sc->member.object, s, locals);
                release_string_like_arg(g, expr->call.args.items[0], sep,
                                        sep_owned, locals);
                return lst;
            }
        }

        /* EnumType.TryParse(text, out EnumType value): C# semantics over the
         * same declaration-order name table as ToString — a case-sensitive
         * strcmp probe; a hit stores the member constant into the out slot,
         * a miss stores default(Color) and yields false. */
        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS &&
            expr->call.callee->member.object->kind == AST_IDENTIFIER &&
            expr->call.args.count == 2 &&
            expr->call.callee->member.name.len == 8 &&
            memcmp(expr->call.callee->member.name.str, "TryParse", 8) == 0) {
            zan_symbol_t *es = zan_binder_lookup(g->binder,
                expr->call.callee->member.object->ident.name);
            if (es && es->kind == SYM_ENUM && es->member_count > 0) {
                zan_symbol_t *mems[256];
                long long vals[256];
                int n = irgen_enum_members(es, mems, vals, 256);
                LLVMTypeRef i8t = LLVMInt8TypeInContext(g->ctx);
                LLVMTypeRef i8pt = LLVMPointerType(i8t, 0);
                LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
                LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
                LLVMTypeRef strcmp_ty = LLVMFunctionType(i32t,
                    (LLVMTypeRef[]){ i8pt, i8pt }, 2, 0);
                LLVMValueRef s = emit_expr(g, expr->call.args.items[0],
                                           locals);
                zan_ast_node_t *out_arg = expr->call.args.items[1];
                LLVMValueRef out_ptr = NULL;
                if (out_arg->kind == AST_REF_ARG) {
                    out_ptr = emit_expr(g, out_arg, locals);
                } else if (out_arg->kind == AST_IDENTIFIER) {
                    local_var_t *ol = local_find(locals,
                        out_arg->ident.name);
                    if (ol) { out_ptr = ol->alloca; }
                }
                LLVMValueRef res_a = emit_entry_alloca(g, i32t, "tp.res");
                LLVMBasicBlockRef done_bb = LLVMAppendBasicBlockInContext(
                    g->ctx, g->current_fn, "tp.done");
                LLVMBasicBlockRef miss_bb = LLVMAppendBasicBlockInContext(
                    g->ctx, g->current_fn, "tp.miss");
                for (int i = 0; i < n; i++) {
                    LLVMBasicBlockRef next_bb =
                        (i == n - 1)
                            ? miss_bb
                            : LLVMAppendBasicBlockInContext(g->ctx,
                                g->current_fn, "tp.chk");
                    LLVMValueRef lit = emit_string_literal_rc(g,
                        mems[i]->name);
                    LLVMValueRef cmpv = zan_call2(g->builder, strcmp_ty,
                        g->fn_strcmp, (LLVMValueRef[]){ s, lit }, 2,
                        "tp.cmp");
                    LLVMValueRef eq = LLVMBuildICmp(g->builder, LLVMIntEQ,
                        cmpv, LLVMConstInt(i32t, 0, 0), "tp.eq");
                    LLVMBasicBlockRef hit_bb = LLVMAppendBasicBlockInContext(
                        g->ctx, g->current_fn, "tp.hit");
                    LLVMBuildCondBr(g->builder, eq, hit_bb, next_bb);
                    LLVMPositionBuilderAtEnd(g->builder, hit_bb);
                    if (out_ptr) {
                        emit_typed_out_store(g, es->type, out_ptr,
                            LLVMConstInt(i64t,
                                (unsigned long long)vals[i], 0));
                    }
                    LLVMBuildStore(g->builder, LLVMConstInt(i32t, 1, 0),
                                   res_a);
                    LLVMBuildBr(g->builder, done_bb);
                    LLVMPositionBuilderAtEnd(g->builder, next_bb);
                }
                /* no member carries this name: C# assigns the default */
                if (out_ptr) {
                    emit_typed_out_store(g, es->type, out_ptr,
                        LLVMConstInt(i64t, 0, 0));
                }
                LLVMBuildStore(g->builder, LLVMConstInt(i32t, 0, 0), res_a);
                LLVMBuildBr(g->builder, done_bb);
                LLVMPositionBuilderAtEnd(g->builder, done_bb);
                emit_release_owned_call_temp(g, expr->call.args.items[0], s,
                                             locals);
                return LLVMBuildLoad2(g->builder, i32t, res_a, "tp.out");
            }
        }

        /* value.ToString() on a scalar (int/float/bool/char/enum) or string
         * receiver. Class/struct receivers fall through to normal method
         * dispatch. Previously these calls lowered to the constant-0 fallback,
         * so e.g. `n.ToString()` always produced "0". */
        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS &&
            expr->call.args.count == 0) {
            zan_ast_node_t *sc = expr->call.callee;
            zan_istr_t sm = sc->member.name;
            if (sm.len == 8 && memcmp(sm.str, "ToString", 8) == 0) {
                zan_type_t *rt_ty = infer_expr_type(g, sc->member.object, locals);
                int handled_kind = rt_ty &&
                    (rt_ty->kind == TYPE_BOOL || rt_ty->kind == TYPE_BYTE ||
                     rt_ty->kind == TYPE_SHORT || rt_ty->kind == TYPE_INT ||
                     rt_ty->kind == TYPE_LONG || rt_ty->kind == TYPE_SBYTE ||
                     rt_ty->kind == TYPE_USHORT || rt_ty->kind == TYPE_UINT ||
                     rt_ty->kind == TYPE_ULONG || rt_ty->kind == TYPE_FLOAT ||
                     rt_ty->kind == TYPE_DOUBLE || rt_ty->kind == TYPE_CHAR ||
                     rt_ty->kind == TYPE_STRING || rt_ty->kind == TYPE_ENUM);
                if (handled_kind) {
                    LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
                    LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
                    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                    LLVMValueRef v = emit_expr(g, sc->member.object, locals);
                    LLVMTypeKind vk = LLVMGetTypeKind(LLVMTypeOf(v));
                    if (rt_ty->kind == TYPE_BOOL && vk == LLVMIntegerTypeKind &&
                        LLVMGetIntTypeWidth(LLVMTypeOf(v)) == 1) {
                        LLVMValueRef t = emit_string_literal_rc(g,
                            (zan_istr_t){ "true", 4 });
                        LLVMValueRef f = emit_string_literal_rc(g,
                            (zan_istr_t){ "false", 5 });
                        return LLVMBuildSelect(g->builder, v, t, f, "bstr");
                    }
                    if (vk == LLVMPointerTypeKind) {
                        /* string receiver: return an owned copy */
                        LLVMTypeRef strlen_ty = LLVMFunctionType(i64,
                            (LLVMTypeRef[]){ i8ptr }, 1, 0);
                        LLVMTypeRef memcpy_ty = LLVMFunctionType(i8ptr,
                            (LLVMTypeRef[]){ i8ptr, i8ptr, i64 }, 3, 0);
                        LLVMValueRef memcpy_fn = get_libc_fn(g, "memcpy", memcpy_ty);
                        LLVMValueRef n = zan_call2(g->builder, strlen_ty,
                            g->fn_strlen, &v, 1, "n");
                        LLVMValueRef bufsz = zan_add(g->builder, n,
                            LLVMConstInt(i64, 1, 0), "bsz");
                        LLVMValueRef buf = emit_string_alloc_rc(g, bufsz);
                        LLVMValueRef mcargs[] = { buf, v, bufsz };
                        zan_call2(g->builder, memcpy_ty, memcpy_fn, mcargs, 3, "");
                        emit_release_owned_call_temp(g, sc->member.object, v, locals);
                        return buf;
                    }
                    /* enum receiver: C# semantics — the name of the member
                     * whose constant equals the value; a value no member
                     * carries formats as its number. A branch chain into a
                     * phi join rather than a select: the numeric fallback
                     * allocates its buffer, so a select would leak the
                     * losing side. */
                    if (rt_ty->kind == TYPE_ENUM && rt_ty->sym &&
                        rt_ty->sym->member_count > 0) {
                        zan_symbol_t *mems[256];
                        long long vals[256];
                        int n = irgen_enum_members(rt_ty->sym, mems, vals, 256);
                        if (n > 0) {
                            LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
                            LLVMValueRef v64 = emit_widen_i64_for_print(g, v);
                            LLVMValueRef fn = LLVMGetBasicBlockParent(
                                LLVMGetInsertBlock(g->builder));
                            LLVMBasicBlockRef joinbb =
                                LLVMAppendBasicBlockInContext(g->ctx, fn,
                                                              "em.join");
                            LLVMBasicBlockRef hitbbs[256];
                            LLVMValueRef lits[256];
                            LLVMBasicBlockRef cur =
                                LLVMGetInsertBlock(g->builder);
                            for (int i = 0; i < n; i++) {
                                LLVMBasicBlockRef miss =
                                    (i == n - 1)
                                        ? LLVMAppendBasicBlockInContext(g->ctx,
                                            fn, "em.num")
                                        : LLVMAppendBasicBlockInContext(g->ctx,
                                            fn, "em.chk");
                                LLVMValueRef hit = LLVMBuildICmp(g->builder,
                                    LLVMIntEQ, v64,
                                    LLVMConstInt(i64t,
                                        (unsigned long long)vals[i], 0),
                                    "em.val");
                                hitbbs[i] = LLVMAppendBasicBlockInContext(
                                    g->ctx, fn, "em.hit");
                                LLVMBuildCondBr(g->builder, hit, hitbbs[i],
                                                miss);
                                /* the literal is materialized inside its
                                 * hit block so the phi incoming dominates */
                                LLVMPositionBuilderAtEnd(g->builder, hitbbs[i]);
                                lits[i] = emit_string_literal_rc(g,
                                    mems[i]->name);
                                LLVMBuildBr(g->builder, joinbb);
                                LLVMPositionBuilderAtEnd(g->builder, miss);
                                cur = miss;
                            }
                            /* Unmatched values only: format like an int. */
                            LLVMValueRef ebuf = emit_string_alloc_rc(g,
                                LLVMConstInt(i64t, 32, 0));
                            emit_itoa_into(g, ebuf, v64, 0);
                            LLVMBuildBr(g->builder, joinbb);
                            LLVMPositionBuilderAtEnd(g->builder, joinbb);
                            LLVMValueRef phi = LLVMBuildPhi(g->builder, i8ptr,
                                                            "em.name");
                            LLVMValueRef *vals_in = (LLVMValueRef *)calloc(
                                (size_t)n + 1, sizeof(LLVMValueRef));
                            LLVMBasicBlockRef *blks_in = (LLVMBasicBlockRef *)
                                calloc((size_t)n + 1, sizeof(*blks_in));
                            for (int i = 0; i < n; i++) {
                                vals_in[i] = lits[i];
                                blks_in[i] = hitbbs[i];
                            }
                            vals_in[n] = ebuf;
                            blks_in[n] = cur;
                            LLVMAddIncoming(phi, vals_in, blks_in, n + 1);
                            free(vals_in);
                            free(blks_in);
                            return phi;
                        }
                    }
                    /* numeric: format like Convert.ToString */
                    LLVMValueRef buf = emit_string_alloc_rc(g,
                        LLVMConstInt(i64, 32, 0));
                    LLVMValueRef fmt;
                    LLVMValueRef num_arg = v;
                    if (vk == LLVMDoubleTypeKind || vk == LLVMFloatTypeKind) {
                        fmt = LLVMBuildGlobalStringPtr(g->builder, "%g", "ftoa_fmt");
                        if (vk == LLVMFloatTypeKind) {
                            num_arg = LLVMBuildFPExt(g->builder, v,
                                LLVMDoubleTypeInContext(g->ctx), "ext");
                        }
                    } else {
                        emit_itoa_into(g, buf, emit_widen_i64_for_print(g, v),
                                       rt_ty->kind == TYPE_ULONG ? 1 : 0);
                        return buf;
                    }
                    LLVMValueRef sn_args[] = { buf, LLVMConstInt(i64, 32, 0), fmt, num_arg };
                    zan_call2(g->builder,
                        LLVMFunctionType(LLVMInt32TypeInContext(g->ctx),
                            (LLVMTypeRef[]){ i8ptr, i64, i8ptr }, 3, 1),
                        g->fn_snprintf, sn_args, 4, "");
                    return buf;
                }
            }
        }

        /* Environment.ArgCount() -> number of command-line args (excludes the
         * program name), i.e. argc - 1. */
        if (!zan_type_defines(g, "Environment", "ArgCount") &&
            is_call_to(expr, "Environment", "ArgCount") && expr->call.args.count == 0) {
            LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
            LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
            LLVMValueRef g_argc = LLVMGetNamedGlobal(g->mod, "__zan_argc");
            if (!g_argc) {
                g_argc = LLVMAddGlobal(g->mod, i32, "__zan_argc");
                LLVMSetInitializer(g_argc, LLVMConstInt(i32, 0, 0));
            }
            LLVMValueRef ac = LLVMBuildLoad2(g->builder, i32, g_argc, "argc");
            LLVMValueRef ac64 = LLVMBuildSExt(g->builder, ac, i64, "argc64");
            return zan_sub(g->builder, ac64, LLVMConstInt(i64, 1, 0), "nargs");
        }

        /* Environment.ArgAt(i) -> string : the (i+1)-th argv entry, so index 0
         * is the first user argument. Out-of-range indexes (negative or >= the
         * argument count) return "" instead of reading past the argv array. */
        if (!zan_type_defines(g, "Environment", "ArgAt") &&
            is_call_to(expr, "Environment", "ArgAt") && expr->call.args.count == 1) {
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMTypeRef i8ptrptr = LLVMPointerType(i8ptr, 0);
            LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
            LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
            zan_ast_node_t *idx_ast = expr->call.args.items[0];
            LLVMValueRef idx = emit_expr(g, idx_ast, locals);
            LLVMValueRef g_argc = LLVMGetNamedGlobal(g->mod, "__zan_argc");
            if (!g_argc) {
                g_argc = LLVMAddGlobal(g->mod, i32, "__zan_argc");
                LLVMSetInitializer(g_argc, LLVMConstInt(i32, 0, 0));
            }
            LLVMValueRef argc = LLVMBuildLoad2(g->builder, i32, g_argc, "argc");
            LLVMValueRef argc64 = LLVMBuildSExt(g->builder, argc, i64, "argc64");
            LLVMValueRef idx1 = zan_add(g->builder, idx, LLVMConstInt(i64, 1, 0), "argi");
            LLVMValueRef in_range = zan_and(g->builder,
                zan_icmp(g->builder, LLVMIntSGE, idx, LLVMConstInt(i64, 0, 0), "arg.nneg"),
                zan_icmp(g->builder, LLVMIntULT, idx1, argc64, "arg.range"),
                "arg.ok");
            emit_release_owned_call_temp(g, idx_ast, idx, locals);

            LLVMValueRef fn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder));
            LLVMBasicBlockRef cur_bb = LLVMGetInsertBlock(g->builder);
            LLVMBasicBlockRef ok_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "argat.ok");
            LLVMBasicBlockRef out_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "argat.out");
            LLVMBuildCondBr(g->builder, in_range, ok_bb, out_bb);

            LLVMPositionBuilderAtEnd(g->builder, ok_bb);
            LLVMValueRef g_argv = LLVMGetNamedGlobal(g->mod, "__zan_argv");
            if (!g_argv) {
                g_argv = LLVMAddGlobal(g->mod, i8ptrptr, "__zan_argv");
                LLVMSetInitializer(g_argv, LLVMConstNull(i8ptrptr));
            }
            LLVMValueRef argv = LLVMBuildLoad2(g->builder, i8ptrptr, g_argv, "argv");
            LLVMValueRef slot = LLVMBuildGEP2(g->builder, i8ptr, argv, &idx1, 1, "argslot");
            LLVMValueRef arg = LLVMBuildLoad2(g->builder, i8ptr, slot, "arg");
            LLVMBuildBr(g->builder, out_bb);

            LLVMPositionBuilderAtEnd(g->builder, out_bb);
            LLVMValueRef empty = emit_string_literal_rc(g, (zan_istr_t){ "", 0 });
            LLVMValueRef res = LLVMBuildPhi(g->builder, i8ptr, "argres");
            LLVMValueRef vals[] = { arg, empty };
            LLVMBasicBlockRef bbs[] = { ok_bb, cur_bb };
            LLVMAddIncoming(res, vals, bbs, 2);
            return res;
        }

        /* File.ReadAllText(path) -> string */
        if (!zan_type_defines(g, "File", "ReadAllText") &&
            is_call_to(expr, "File", "ReadAllText") && expr->call.args.count == 1 &&
            !src_method_takes_over(g, expr, locals)) {
            zan_ast_node_t *path_ast = expr->call.args.items[0];
            LLVMValueRef path_arg = emit_expr(g, path_ast, locals);
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
            LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
            /* declare fopen */
            LLVMValueRef fopen_fn = LLVMGetNamedFunction(g->mod, "fopen");
            if (!fopen_fn) {
                LLVMTypeRef fopen_type = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0);
                fopen_fn = LLVMAddFunction(g->mod, "fopen", fopen_type);
            }
            /* declare fseek, ftell, fread, fclose */
            LLVMValueRef fseek_fn = LLVMGetNamedFunction(g->mod, "fseek");
            if (!fseek_fn) {
                LLVMTypeRef fseek_type = LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr, i64, i32 }, 3, 0);
                fseek_fn = LLVMAddFunction(g->mod, "fseek", fseek_type);
            }
            LLVMValueRef ftell_fn = LLVMGetNamedFunction(g->mod, "ftell");
            if (!ftell_fn) {
                LLVMTypeRef ftell_type = LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr }, 1, 0);
                ftell_fn = LLVMAddFunction(g->mod, "ftell", ftell_type);
            }
            LLVMValueRef fread_fn = LLVMGetNamedFunction(g->mod, "fread");
            if (!fread_fn) {
                LLVMTypeRef fread_type = LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr, i64, i64, i8ptr }, 4, 0);
                fread_fn = LLVMAddFunction(g->mod, "fread", fread_type);
            }
            LLVMValueRef fclose_fn = LLVMGetNamedFunction(g->mod, "fclose");
            if (!fclose_fn) {
                LLVMTypeRef fclose_type = LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr }, 1, 0);
                fclose_fn = LLVMAddFunction(g->mod, "fclose", fclose_type);
            }
            /* open file */
            LLVMValueRef mode = LLVMBuildGlobalStringPtr(g->builder, "rb", "rb");
            LLVMValueRef open_args[] = { path_arg, mode };
            LLVMValueRef fp = zan_call2(g->builder,
                LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0),
                fopen_fn, open_args, 2, "fp");
            emit_fopen_check(g, fp, "cannot read file\n");
            /* seek to end, get size */
            LLVMValueRef seek_end_args[] = { fp, LLVMConstInt(i64, 0, 0), LLVMConstInt(i32, 2, 0) };
            LLVMValueRef se_end = zan_call2(g->builder, LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr, i64, i32 }, 3, 0),
                fseek_fn, seek_end_args, 3, "se_end");
            emit_io_abort_if(g, zan_icmp(g->builder, LLVMIntNE, se_end, LLVMConstInt(i32, 0, 0), "se_end.err"),
                "cannot read file\n");
            LLVMValueRef size = zan_call2(g->builder,
                LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr }, 1, 0), ftell_fn, &fp, 1, "sz");
            emit_io_abort_if(g, zan_icmp(g->builder, LLVMIntSLT, size, LLVMConstInt(i64, 0, 0), "sz.err"),
                "cannot read file\n");
            /* seek back to start */
            LLVMValueRef seek_start_args[] = { fp, LLVMConstInt(i64, 0, 0), LLVMConstInt(i32, 0, 0) };
            LLVMValueRef se_start = zan_call2(g->builder, LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr, i64, i32 }, 3, 0),
                fseek_fn, seek_start_args, 3, "se_start");
            emit_io_abort_if(g, zan_icmp(g->builder, LLVMIntNE, se_start, LLVMConstInt(i32, 0, 0), "se_start.err"),
                "cannot read file\n");
            /* allocate buffer (size+1 for null terminator) */
            LLVMValueRef buf_size = zan_add(g->builder, size, LLVMConstInt(i64, 1, 0), "bsz");
            LLVMValueRef buf = emit_string_alloc_rc(g, buf_size);
            /* read file; require the full byte count */
            LLVMValueRef fread_args[] = { buf, LLVMConstInt(i64, 1, 0), size, fp };
            LLVMValueRef got = zan_call2(g->builder, LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr, i64, i64, i8ptr }, 4, 0),
                fread_fn, fread_args, 4, "got");
            emit_io_abort_if(g, zan_icmp(g->builder, LLVMIntNE, got, size, "got.err"),
                "cannot read file\n");
            /* null terminate */
            LLVMValueRef end_ptr = LLVMBuildGEP2(g->builder, LLVMInt8TypeInContext(g->ctx), buf, &size, 1, "end");
            LLVMBuildStore(g->builder, LLVMConstInt(LLVMInt8TypeInContext(g->ctx), 0, 0), end_ptr);
            /* close */
            LLVMValueRef rd_close = zan_call2(g->builder, LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr }, 1, 0),
                fclose_fn, &fp, 1, "rd_close");
            emit_io_abort_if(g, zan_icmp(g->builder, LLVMIntNE, rd_close, LLVMConstInt(i32, 0, 0), "rd_close.err"),
                "cannot read file\n");
            emit_release_owned_call_temp(g, path_ast, path_arg, locals);
            return buf;
        }

        /* File.WriteAllText(path, content) */
        if (!zan_type_defines(g, "File", "WriteAllText") &&
            is_call_to(expr, "File", "WriteAllText") && expr->call.args.count == 2 &&
            !src_method_takes_over(g, expr, locals)) {
            zan_ast_node_t *path_ast = expr->call.args.items[0];
            zan_ast_node_t *content_ast = expr->call.args.items[1];
            LLVMValueRef path_arg = emit_expr(g, path_ast, locals);
            LLVMValueRef content_arg = emit_expr(g, content_ast, locals);
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
            LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
            LLVMValueRef fopen_fn = LLVMGetNamedFunction(g->mod, "fopen");
            if (!fopen_fn) {
                LLVMTypeRef fopen_type = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0);
                fopen_fn = LLVMAddFunction(g->mod, "fopen", fopen_type);
            }
            LLVMValueRef fputs_fn = LLVMGetNamedFunction(g->mod, "fputs");
            if (!fputs_fn) {
                LLVMTypeRef fputs_type = LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0);
                fputs_fn = LLVMAddFunction(g->mod, "fputs", fputs_type);
            }
            LLVMValueRef fclose_fn = LLVMGetNamedFunction(g->mod, "fclose");
            if (!fclose_fn) {
                LLVMTypeRef fclose_type = LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr }, 1, 0);
                fclose_fn = LLVMAddFunction(g->mod, "fclose", fclose_type);
            }
            LLVMValueRef mode = LLVMBuildGlobalStringPtr(g->builder, "w", "wmode");
            LLVMValueRef open_args[] = { path_arg, mode };
            LLVMValueRef fp = zan_call2(g->builder,
                LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0),
                fopen_fn, open_args, 2, "fp");
            emit_fopen_check(g, fp, "cannot write file\n");
            LLVMValueRef fputs_args[] = { content_arg, fp };
            LLVMValueRef put = zan_call2(g->builder, LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0),
                fputs_fn, fputs_args, 2, "put");
            emit_io_abort_if(g, zan_icmp(g->builder, LLVMIntSLT, put, LLVMConstInt(i32, 0, 0), "put.err"),
                "cannot write file\n");
            LLVMValueRef wr_close = zan_call2(g->builder, LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr }, 1, 0),
                fclose_fn, &fp, 1, "wr_close");
            emit_io_abort_if(g, zan_icmp(g->builder, LLVMIntNE, wr_close, LLVMConstInt(i32, 0, 0), "wr_close.err"),
                "cannot write file\n");
            emit_release_owned_call_temp(g, path_ast, path_arg, locals);
            emit_release_owned_call_temp(g, content_ast, content_arg, locals);
            return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
        }

        /* Path.GetFileName(path), Path.GetExtension(path), Path.Combine(a,b) */

        if (!zan_type_defines(g, "Path", "GetFileName") &&
            is_call_to(expr, "Path", "GetFileName") && expr->call.args.count == 1) {
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
            zan_ast_node_t *path_ast = expr->call.args.items[0];
            LLVMValueRef path_val = emit_expr(g, path_ast, locals);
            /* call strrchr(path, '/') then strrchr(path, '\\') and pick later one */
            LLVMValueRef strrchr_fn = LLVMGetNamedFunction(g->mod, "strrchr");
            LLVMValueRef slash_args[] = { path_val, LLVMConstInt(i32t, '/', 0) };
            LLVMValueRef slash = zan_call2(g->builder,
                LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i32t }, 2, 0),
                strrchr_fn, slash_args, 2, "slash");
            LLVMValueRef bslash_args[] = { path_val, LLVMConstInt(i32t, 92, 0) }; /* 92 = backslash */
            LLVMValueRef bslash = zan_call2(g->builder,
                LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i32t }, 2, 0),
                strrchr_fn, bslash_args, 2, "bslash");
            /* end of the path, for the substring copy */
            LLVMTypeRef strlen_ty = LLVMFunctionType(LLVMInt64TypeInContext(g->ctx),
                (LLVMTypeRef[]){ i8ptr }, 1, 0);
            LLVMValueRef plen = zan_call2(g->builder, strlen_ty, g->fn_strlen,
                &path_val, 1, "plen");
            LLVMValueRef pend = LLVMBuildGEP2(g->builder, LLVMInt8TypeInContext(g->ctx),
                path_val, &plen, 1, "pend");
            LLVMValueRef s_null = zan_icmp(g->builder, LLVMIntEQ, slash,
                LLVMConstNull(i8ptr), "snull");
            LLVMValueRef b_null = zan_icmp(g->builder, LLVMIntEQ, bslash,
                LLVMConstNull(i8ptr), "bnull");
            LLVMValueRef both_null = zan_and(g->builder, s_null, b_null, "bothnull");
            LLVMValueRef fn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder));
            LLVMBasicBlockRef cur_bb = LLVMGetInsertBlock(g->builder);
            LLVMBasicBlockRef both_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "fn.both");
            LLVMBasicBlockRef notboth_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "fn.notboth");
            LLVMBasicBlockRef use_bs_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "fn.bs");
            LLVMBasicBlockRef check_b_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "fn.chkb");
            LLVMBasicBlockRef use_sl_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "fn.sl");
            LLVMBasicBlockRef max_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "fn.max");
            LLVMBasicBlockRef emit_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "fn.emit");
            LLVMBuildCondBr(g->builder, both_null, both_bb, notboth_bb);

            LLVMPositionBuilderAtEnd(g->builder, both_bb);
            LLVMBuildBr(g->builder, emit_bb);

            LLVMPositionBuilderAtEnd(g->builder, notboth_bb);
            LLVMBuildCondBr(g->builder, s_null, use_bs_bb, check_b_bb);

            LLVMPositionBuilderAtEnd(g->builder, use_bs_bb);
            LLVMValueRef bs1 = LLVMBuildGEP2(g->builder, LLVMInt8TypeInContext(g->ctx),
                bslash, (LLVMValueRef[]){ LLVMConstInt(i32t, 1, 0) }, 1, "bs1");
            LLVMBuildBr(g->builder, emit_bb);

            LLVMPositionBuilderAtEnd(g->builder, check_b_bb);
            LLVMBuildCondBr(g->builder, b_null, use_sl_bb, max_bb);

            LLVMPositionBuilderAtEnd(g->builder, use_sl_bb);
            LLVMValueRef sl1 = LLVMBuildGEP2(g->builder, LLVMInt8TypeInContext(g->ctx),
                slash, (LLVMValueRef[]){ LLVMConstInt(i32t, 1, 0) }, 1, "sl1");
            LLVMBuildBr(g->builder, emit_bb);

            /* both present: pick the later one (both non-null, so the pointer
             * comparison is defined) */
            LLVMPositionBuilderAtEnd(g->builder, max_bb);
            LLVMValueRef s_gt = zan_icmp(g->builder, LLVMIntUGT, slash, bslash, "sgt");
            LLVMValueRef best = LLVMBuildSelect(g->builder, s_gt, slash, bslash, "best");
            LLVMValueRef b2 = LLVMBuildGEP2(g->builder, LLVMInt8TypeInContext(g->ctx),
                best, (LLVMValueRef[]){ LLVMConstInt(i32t, 1, 0) }, 1, "b2");
            LLVMBuildBr(g->builder, emit_bb);

            LLVMPositionBuilderAtEnd(g->builder, emit_bb);
            LLVMValueRef start = LLVMBuildPhi(g->builder, i8ptr, "fn.start");
            LLVMAddIncoming(start,
                (LLVMValueRef[]){ path_val, bs1, sl1, b2 },
                (LLVMBasicBlockRef[]){ both_bb, use_bs_bb, use_sl_bb, max_bb }, 4);
            /* fresh string so the result outlives the path argument's release */
            LLVMValueRef fname = emit_string_copy_range(g, start, pend);
            emit_release_owned_call_temp(g, path_ast, path_val, locals);
            return fname;
        }

        if (!zan_type_defines(g, "Path", "GetExtension") &&
            is_call_to(expr, "Path", "GetExtension") && expr->call.args.count == 1) {
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
            zan_ast_node_t *path_ast = expr->call.args.items[0];
            LLVMValueRef path_val = emit_expr(g, path_ast, locals);
            LLVMValueRef strrchr_fn = LLVMGetNamedFunction(g->mod, "strrchr");
            LLVMValueRef dot_args[] = { path_val, LLVMConstInt(i32t, '.', 0) };
            LLVMValueRef dot = zan_call2(g->builder,
                LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i32t }, 2, 0),
                strrchr_fn, dot_args, 2, "dot");
            /* no dot: return an empty string */
            LLVMValueRef is_null = zan_icmp(g->builder, LLVMIntEQ, dot,
                LLVMConstNull(i8ptr), "dnull");
            LLVMValueRef fn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder));
            LLVMBasicBlockRef cur_bb = LLVMGetInsertBlock(g->builder);
            LLVMBasicBlockRef empty_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "ext.empty");
            LLVMBasicBlockRef copy_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "ext.copy");
            LLVMBasicBlockRef done_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "ext.done");
            LLVMBuildCondBr(g->builder, is_null, empty_bb, copy_bb);

            LLVMPositionBuilderAtEnd(g->builder, empty_bb);
            LLVMValueRef empty = emit_string_literal_rc(g, (zan_istr_t){ "", 0 });
            LLVMBuildBr(g->builder, done_bb);

            /* copy [dot, end) into a fresh rc string so the result outlives
             * the path argument's release */
            LLVMPositionBuilderAtEnd(g->builder, copy_bb);
            LLVMTypeRef strlen_ty = LLVMFunctionType(LLVMInt64TypeInContext(g->ctx),
                (LLVMTypeRef[]){ i8ptr }, 1, 0);
            LLVMValueRef plen = zan_call2(g->builder, strlen_ty, g->fn_strlen,
                &path_val, 1, "plen");
            LLVMValueRef pend = LLVMBuildGEP2(g->builder, LLVMInt8TypeInContext(g->ctx),
                path_val, &plen, 1, "pend");
            LLVMValueRef ext = emit_string_copy_range(g, dot, pend);
            LLVMBuildBr(g->builder, done_bb);

            LLVMPositionBuilderAtEnd(g->builder, done_bb);
            LLVMValueRef res = LLVMBuildPhi(g->builder, i8ptr, "ext.res");
            LLVMAddIncoming(res, (LLVMValueRef[]){ empty, ext },
                (LLVMBasicBlockRef[]){ empty_bb, copy_bb }, 2);
            emit_release_owned_call_temp(g, path_ast, path_val, locals);
            return res;
        }

        if (!zan_type_defines(g, "Path", "Combine") &&
            is_call_to(expr, "Path", "Combine") && expr->call.args.count == 2) {
            LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMValueRef a = emit_expr(g, expr->call.args.items[0], locals);
            LLVMValueRef b = emit_expr(g, expr->call.args.items[1], locals);
            /* len = strlen(a) + 1 + strlen(b) + 1 */
            LLVMTypeRef strlen_type = LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr }, 1, 0);
            LLVMValueRef len_a = zan_call2(g->builder, strlen_type, g->fn_strlen, &a, 1, "la");
            LLVMValueRef len_b = zan_call2(g->builder, strlen_type, g->fn_strlen, &b, 1, "lb");
            LLVMValueRef total = zan_add(g->builder, len_a, len_b, "t");
            total = zan_add(g->builder, total, LLVMConstInt(i64, 2, 0), "t2"); /* +separator+null */
            LLVMValueRef buf = emit_string_alloc_rc(g, total);
            /* strcpy(buf, a) */
            LLVMValueRef strcpy_args[] = { buf, a };
            zan_call2(g->builder,
                LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0),
                g->fn_strcpy, strcpy_args, 2, "");
            /* strcat(buf, "/") */
            LLVMValueRef sep = LLVMBuildGlobalStringPtr(g->builder, g->target_is_windows ? "\\" : "/", "sep");
            LLVMValueRef cat1_args[] = { buf, sep };
            zan_call2(g->builder,
                LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0),
                g->fn_strcat, cat1_args, 2, "");
            /* strcat(buf, b) */
            LLVMValueRef cat2_args[] = { buf, b };
            zan_call2(g->builder,
                LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0),
                g->fn_strcat, cat2_args, 2, "");
            emit_release_owned_call_temp(g, expr->call.args.items[0], a, locals);
            emit_release_owned_call_temp(g, expr->call.args.items[1], b, locals);
            return buf;
        }

        /* File.AppendAllText(path, content) */
        if (!zan_type_defines(g, "File", "AppendAllText") &&
            is_call_to(expr, "File", "AppendAllText") && expr->call.args.count == 2 &&
            !src_method_takes_over(g, expr, locals)) {
            LLVMValueRef path_arg = emit_expr(g, expr->call.args.items[0], locals);
            LLVMValueRef content_arg = emit_expr(g, expr->call.args.items[1], locals);
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
            LLVMValueRef fopen_fn = LLVMGetNamedFunction(g->mod, "fopen");
            if (!fopen_fn) {
                LLVMTypeRef fopen_type = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0);
                fopen_fn = LLVMAddFunction(g->mod, "fopen", fopen_type);
            }
            LLVMValueRef fputs_fn = LLVMGetNamedFunction(g->mod, "fputs");
            if (!fputs_fn) {
                LLVMTypeRef fputs_type = LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0);
                fputs_fn = LLVMAddFunction(g->mod, "fputs", fputs_type);
            }
            LLVMValueRef fclose_fn = LLVMGetNamedFunction(g->mod, "fclose");
            if (!fclose_fn) {
                LLVMTypeRef fclose_type = LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr }, 1, 0);
                fclose_fn = LLVMAddFunction(g->mod, "fclose", fclose_type);
            }
            LLVMValueRef mode = LLVMBuildGlobalStringPtr(g->builder, "a", "amode");
            LLVMValueRef open_args[] = { path_arg, mode };
            LLVMValueRef fp = zan_call2(g->builder,
                LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0),
                fopen_fn, open_args, 2, "fp");
            emit_fopen_check(g, fp, "cannot write file\n");
            LLVMValueRef fputs_args[] = { content_arg, fp };
            zan_call2(g->builder, LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0),
                fputs_fn, fputs_args, 2, "");
            zan_call2(g->builder, LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr }, 1, 0),
                fclose_fn, &fp, 1, "");
            emit_release_owned_call_temp(g, expr->call.args.items[0], path_arg, locals);
            emit_release_owned_call_temp(g, expr->call.args.items[1], content_arg, locals);
            return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
        }

        /* File.Exists(path) -> bool */
        if (!zan_type_defines(g, "File", "Exists") &&
            is_call_to(expr, "File", "Exists") && expr->call.args.count == 1 &&
            !src_method_takes_over(g, expr, locals)) {
            LLVMValueRef path_arg = emit_expr(g, expr->call.args.items[0], locals);
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMValueRef fopen_fn = LLVMGetNamedFunction(g->mod, "fopen");
            if (!fopen_fn) {
                LLVMTypeRef fopen_type = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0);
                fopen_fn = LLVMAddFunction(g->mod, "fopen", fopen_type);
            }
            LLVMValueRef fclose_fn = LLVMGetNamedFunction(g->mod, "fclose");
            if (!fclose_fn) {
                LLVMTypeRef fclose_type = LLVMFunctionType(LLVMInt32TypeInContext(g->ctx), (LLVMTypeRef[]){ i8ptr }, 1, 0);
                fclose_fn = LLVMAddFunction(g->mod, "fclose", fclose_type);
            }
            LLVMValueRef mode = LLVMBuildGlobalStringPtr(g->builder, "rb", "rb");
            LLVMValueRef open_args[] = { path_arg, mode };
            LLVMValueRef fp = zan_call2(g->builder,
                LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0),
                fopen_fn, open_args, 2, "fp");
            LLVMValueRef is_null = zan_icmp(g->builder, LLVMIntNE, fp,
                LLVMConstNull(i8ptr), "exists");
            /* close if opened */
            LLVMBasicBlockRef close_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "fexist.close");
            LLVMBasicBlockRef end_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "fexist.end");
            LLVMBuildCondBr(g->builder, is_null, close_bb, end_bb);
            LLVMPositionBuilderAtEnd(g->builder, close_bb);
            zan_call2(g->builder, LLVMFunctionType(LLVMInt32TypeInContext(g->ctx), (LLVMTypeRef[]){ i8ptr }, 1, 0),
                fclose_fn, &fp, 1, "");
            LLVMBuildBr(g->builder, end_bb);
            LLVMPositionBuilderAtEnd(g->builder, end_bb);
            LLVMValueRef result = LLVMBuildZExt(g->builder, is_null, LLVMInt64TypeInContext(g->ctx), "fex");
            emit_release_owned_call_temp(g, expr->call.args.items[0], path_arg, locals);
            return result;
        }

        /* File.Delete(path) */
        if (!zan_type_defines(g, "File", "Delete") &&
            is_call_to(expr, "File", "Delete") && expr->call.args.count == 1 &&
            !src_method_takes_over(g, expr, locals)) {
            LLVMValueRef path_arg = emit_expr(g, expr->call.args.items[0], locals);
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
            LLVMValueRef remove_fn = LLVMGetNamedFunction(g->mod, "remove");
            if (!remove_fn) {
                LLVMTypeRef remove_type = LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr }, 1, 0);
                remove_fn = LLVMAddFunction(g->mod, "remove", remove_type);
            }
            zan_call2(g->builder, LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr }, 1, 0),
                remove_fn, &path_arg, 1, "");
            emit_release_owned_call_temp(g, expr->call.args.items[0], path_arg, locals);
            return LLVMConstInt(i32, 0, 0);
        }

        /* File.Move(source, dest) — rename */
        if (!zan_type_defines(g, "File", "Move") &&
            is_call_to(expr, "File", "Move") && expr->call.args.count == 2 &&
            !src_method_takes_over(g, expr, locals)) {
            LLVMValueRef src = emit_expr(g, expr->call.args.items[0], locals);
            LLVMValueRef dst = emit_expr(g, expr->call.args.items[1], locals);
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
            LLVMValueRef rename_fn = LLVMGetNamedFunction(g->mod, "rename");
            if (!rename_fn) {
                LLVMTypeRef rename_type = LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0);
                rename_fn = LLVMAddFunction(g->mod, "rename", rename_type);
            }
            LLVMValueRef args[] = { src, dst };
            zan_call2(g->builder, LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0),
                rename_fn, args, 2, "");
            emit_release_owned_call_temp(g, expr->call.args.items[0], src, locals);
            emit_release_owned_call_temp(g, expr->call.args.items[1], dst, locals);
            return LLVMConstInt(i32, 0, 0);
        }

        /* File.Copy(source, dest) — read source, write dest */
        if (!zan_type_defines(g, "File", "Copy") &&
            is_call_to(expr, "File", "Copy") && expr->call.args.count == 2 &&
            !src_method_takes_over(g, expr, locals)) {
            LLVMValueRef src = emit_expr(g, expr->call.args.items[0], locals);
            LLVMValueRef dst = emit_expr(g, expr->call.args.items[1], locals);
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
            LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
            /* declare helper functions */
            LLVMValueRef fopen_fn = LLVMGetNamedFunction(g->mod, "fopen");
            if (!fopen_fn) {
                LLVMTypeRef ft = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0);
                fopen_fn = LLVMAddFunction(g->mod, "fopen", ft);
            }
            LLVMValueRef fread_fn = LLVMGetNamedFunction(g->mod, "fread");
            if (!fread_fn) {
                LLVMTypeRef ft = LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr, i64, i64, i8ptr }, 4, 0);
                fread_fn = LLVMAddFunction(g->mod, "fread", ft);
            }
            LLVMValueRef fwrite_fn = LLVMGetNamedFunction(g->mod, "fwrite");
            if (!fwrite_fn) {
                LLVMTypeRef ft = LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr, i64, i64, i8ptr }, 4, 0);
                fwrite_fn = LLVMAddFunction(g->mod, "fwrite", ft);
            }
            LLVMValueRef fseek_fn = LLVMGetNamedFunction(g->mod, "fseek");
            if (!fseek_fn) {
                LLVMTypeRef ft = LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr, i64, i32 }, 3, 0);
                fseek_fn = LLVMAddFunction(g->mod, "fseek", ft);
            }
            LLVMValueRef ftell_fn = LLVMGetNamedFunction(g->mod, "ftell");
            if (!ftell_fn) {
                LLVMTypeRef ft = LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr }, 1, 0);
                ftell_fn = LLVMAddFunction(g->mod, "ftell", ft);
            }
            LLVMValueRef fclose_fn = LLVMGetNamedFunction(g->mod, "fclose");
            if (!fclose_fn) {
                LLVMTypeRef ft = LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr }, 1, 0);
                fclose_fn = LLVMAddFunction(g->mod, "fclose", ft);
            }
            /* open source for reading */
            LLVMValueRef rb = LLVMBuildGlobalStringPtr(g->builder, "rb", "rb");
            LLVMValueRef sargs[] = { src, rb };
            LLVMValueRef sfp = zan_call2(g->builder,
                LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0),
                fopen_fn, sargs, 2, "sfp");
            emit_fopen_check(g, sfp, "cannot read file\n");
            /* get size (ftell failure yields -1: clamp to an empty copy
             * instead of driving fread/fwrite with a huge size) */
            LLVMValueRef zero64 = LLVMConstInt(i64, 0, 0);
            LLVMValueRef seek_end[] = { sfp, zero64, LLVMConstInt(i32, 2, 0) };
            zan_call2(g->builder, LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr, i64, i32 }, 3, 0),
                fseek_fn, seek_end, 3, "");
            LLVMValueRef sz = zan_call2(g->builder,
                LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr }, 1, 0), ftell_fn, &sfp, 1, "sz");
            sz = LLVMBuildSelect(g->builder,
                zan_icmp(g->builder, LLVMIntSLT, sz, zero64, "sz.neg"),
                zero64, sz, "sz.clamp");
            LLVMValueRef seek_start[] = { sfp, zero64, LLVMConstInt(i32, 0, 0) };
            zan_call2(g->builder, LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr, i64, i32 }, 3, 0),
                fseek_fn, seek_start, 3, "");
            /* allocate a raw byte buffer: this must stay a plain malloc --
             * emit_string_alloc_rc hands back a header-offset pointer that
             * the free below must not receive */
            LLVMValueRef buf = zan_call2(g->builder,
                LLVMGlobalGetValueType(g->fn_malloc), g->fn_malloc,
                &sz, 1, "cp.buf");
            {
                LLVMTypeRef i8ptr_t = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                emit_io_abort_if(g, zan_icmp(g->builder, LLVMIntEQ, buf,
                    LLVMConstPointerNull(i8ptr_t), "cp.buf.null"),
                    "out of memory\n");
            }
            /* read */
            LLVMValueRef fread_args[] = { buf, LLVMConstInt(i64, 1, 0), sz, sfp };
            zan_call2(g->builder, LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr, i64, i64, i8ptr }, 4, 0),
                fread_fn, fread_args, 4, "");
            zan_call2(g->builder, LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr }, 1, 0),
                fclose_fn, &sfp, 1, "");
            /* open dest for writing */
            LLVMValueRef wb = LLVMBuildGlobalStringPtr(g->builder, "wb", "wb");
            LLVMValueRef dargs[] = { dst, wb };
            LLVMValueRef dfp = zan_call2(g->builder,
                LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0),
                fopen_fn, dargs, 2, "dfp");
            emit_fopen_check(g, dfp, "cannot write file\n");
            /* write */
            LLVMValueRef fwrite_args[] = { buf, LLVMConstInt(i64, 1, 0), sz, dfp };
            zan_call2(g->builder, LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr, i64, i64, i8ptr }, 4, 0),
                fwrite_fn, fwrite_args, 4, "");
            zan_call2(g->builder, LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr }, 1, 0),
                fclose_fn, &dfp, 1, "");
            /* free buffer */
            zan_call2(g->builder, LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), (LLVMTypeRef[]){ i8ptr }, 1, 0),
                g->fn_free, &buf, 1, "");
            emit_release_owned_call_temp(g, expr->call.args.items[0], src, locals);
            emit_release_owned_call_temp(g, expr->call.args.items[1], dst, locals);
            return LLVMConstInt(i32, 0, 0);
        }

        /* File.GetSize(path) -> int */
        if (!zan_type_defines(g, "File", "GetSize") &&
            is_call_to(expr, "File", "GetSize") && expr->call.args.count == 1 &&
            !src_method_takes_over(g, expr, locals)) {
            LLVMValueRef path_arg = emit_expr(g, expr->call.args.items[0], locals);
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
            LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
            LLVMValueRef fopen_fn = LLVMGetNamedFunction(g->mod, "fopen");
            if (!fopen_fn) {
                LLVMTypeRef ft = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0);
                fopen_fn = LLVMAddFunction(g->mod, "fopen", ft);
            }
            LLVMValueRef fseek_fn = LLVMGetNamedFunction(g->mod, "fseek");
            if (!fseek_fn) {
                LLVMTypeRef ft = LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr, i64, i32 }, 3, 0);
                fseek_fn = LLVMAddFunction(g->mod, "fseek", ft);
            }
            LLVMValueRef ftell_fn = LLVMGetNamedFunction(g->mod, "ftell");
            if (!ftell_fn) {
                LLVMTypeRef ft = LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr }, 1, 0);
                ftell_fn = LLVMAddFunction(g->mod, "ftell", ft);
            }
            LLVMValueRef fclose_fn = LLVMGetNamedFunction(g->mod, "fclose");
            if (!fclose_fn) {
                LLVMTypeRef ft = LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr }, 1, 0);
                fclose_fn = LLVMAddFunction(g->mod, "fclose", ft);
            }
            LLVMValueRef mode = LLVMBuildGlobalStringPtr(g->builder, "rb", "rb");
            LLVMValueRef open_args[] = { path_arg, mode };
            LLVMValueRef fp = zan_call2(g->builder,
                LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0),
                fopen_fn, open_args, 2, "fp");
            LLVMValueRef seek_end[] = { fp, LLVMConstInt(i64, 0, 0), LLVMConstInt(i32, 2, 0) };
            zan_call2(g->builder, LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr, i64, i32 }, 3, 0),
                fseek_fn, seek_end, 3, "");
            LLVMValueRef sz = zan_call2(g->builder,
                LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr }, 1, 0), ftell_fn, &fp, 1, "fsz");
            zan_call2(g->builder, LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr }, 1, 0),
                fclose_fn, &fp, 1, "");
            emit_release_owned_call_temp(g, expr->call.args.items[0], path_arg, locals);
            return sz;
        }

        /* Directory.Exists(path) -> bool */
        if (!zan_type_defines(g, "Directory", "Exists") &&
            is_call_to(expr, "Directory", "Exists") && expr->call.args.count == 1) {
            LLVMValueRef path_arg = emit_expr(g, expr->call.args.items[0], locals);
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
            LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
            /* Windows: GetFileAttributesA & FILE_ATTRIBUTE_DIRECTORY. POSIX: opendir!=NULL. */
            LLVMValueRef result;
            if (g->target_is_windows) {
                LLVMValueRef gfa_fn = LLVMGetNamedFunction(g->mod, "GetFileAttributesA");
                if (!gfa_fn) {
                    LLVMTypeRef gfa_type = LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr }, 1, 0);
                    gfa_fn = LLVMAddFunction(g->mod, "GetFileAttributesA", gfa_type);
                }
                LLVMValueRef attrs = zan_call2(g->builder,
                    LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr }, 1, 0),
                    gfa_fn, &path_arg, 1, "attrs");
                LLVMValueRef not_invalid = zan_icmp(g->builder, LLVMIntNE, attrs,
                    LLVMConstInt(i32, 0xFFFFFFFF, 0), "noinv");
                LLVMValueRef is_dir = zan_and(g->builder, attrs,
                    LLVMConstInt(i32, 0x10, 0), "isdir");
                LLVMValueRef is_dir_bool = zan_icmp(g->builder, LLVMIntNE, is_dir,
                    LLVMConstInt(i32, 0, 0), "isdirb");
                result = zan_and(g->builder, not_invalid, is_dir_bool, "dexist");
            } else {
                LLVMValueRef opendir_fn = LLVMGetNamedFunction(g->mod, "opendir");
                if (!opendir_fn) {
                    LLVMTypeRef ft = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr }, 1, 0);
                    opendir_fn = LLVMAddFunction(g->mod, "opendir", ft);
                }
                LLVMValueRef dirp = zan_call2(g->builder,
                    LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr }, 1, 0),
                    opendir_fn, &path_arg, 1, "dirp");
                result = zan_icmp(g->builder, LLVMIntNE, dirp, LLVMConstNull(i8ptr), "dopen");
                LLVMValueRef closedir_fn = LLVMGetNamedFunction(g->mod, "closedir");
                if (!closedir_fn) {
                    LLVMTypeRef ft = LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr }, 1, 0);
                    closedir_fn = LLVMAddFunction(g->mod, "closedir", ft);
                }
                LLVMBasicBlockRef cl_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "de.close");
                LLVMBasicBlockRef end_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "de.end");
                LLVMBuildCondBr(g->builder, result, cl_bb, end_bb);
                LLVMPositionBuilderAtEnd(g->builder, cl_bb);
                zan_call2(g->builder, LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr }, 1, 0),
                    closedir_fn, &dirp, 1, "");
                LLVMBuildBr(g->builder, end_bb);
                LLVMPositionBuilderAtEnd(g->builder, end_bb);
            }
            emit_release_owned_call_temp(g, expr->call.args.items[0], path_arg, locals);
            return LLVMBuildZExt(g->builder, result, i64, "dex");
        }

        /* Environment.ExeDir() — directory containing the running executable
         * (runtime helper zan_exe_dir_into fills an rc string buffer). */
        if (!zan_type_defines(g, "Environment", "ExeDir") &&
            is_call_to(expr, "Environment", "ExeDir") && expr->call.args.count == 0) {
            g->uses_sync_runtime = true;
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
            LLVMValueRef buf = emit_string_alloc_rc(g, LLVMConstInt(i64, 1024, 0));
            LLVMTypeRef ft = LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr, i64 }, 2, 0);
            LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "zan_exe_dir_into");
            if (!fn) fn = LLVMAddFunction(g->mod, "zan_exe_dir_into", ft);
            LLVMValueRef args2[] = { buf, LLVMConstInt(i64, 1024, 0) };
            zan_call2(g->builder, ft, fn, args2, 2, "");
            return buf;
        }

        /* Directory.ListNames(pattern) — '\n'-joined file names matching a
         * glob (runtime helper zan_dir_list_into fills an rc string buffer). */
        if (!zan_type_defines(g, "Directory", "ListNames") &&
            is_call_to(expr, "Directory", "ListNames") && expr->call.args.count == 1) {
            g->uses_sync_runtime = true;
            LLVMValueRef pat = emit_expr(g, expr->call.args.items[0], locals);
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
            LLVMValueRef buf = emit_string_alloc_rc(g, LLVMConstInt(i64, 65536, 0));
            LLVMTypeRef ft = LLVMFunctionType(i64,
                (LLVMTypeRef[]){ i8ptr, i8ptr, i64 }, 3, 0);
            LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "zan_dir_list_into");
            if (!fn) fn = LLVMAddFunction(g->mod, "zan_dir_list_into", ft);
            LLVMValueRef args3[] = { pat, buf, LLVMConstInt(i64, 65536, 0) };
            zan_call2(g->builder, ft, fn, args3, 3, "");
            emit_release_owned_call_temp(g, expr->call.args.items[0], pat, locals);
            return buf;
        }

        /* Directory.CreateDirectory(path) */
        if (!zan_type_defines(g, "Directory", "CreateDirectory") &&
            is_call_to(expr, "Directory", "CreateDirectory") && expr->call.args.count == 1) {
            LLVMValueRef path_arg = emit_expr(g, expr->call.args.items[0], locals);
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
            if (g->target_is_windows) {
                LLVMValueRef mkdir_fn = LLVMGetNamedFunction(g->mod, "_mkdir");
                if (!mkdir_fn) {
                    LLVMTypeRef ft = LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr }, 1, 0);
                    mkdir_fn = LLVMAddFunction(g->mod, "_mkdir", ft);
                }
                zan_call2(g->builder, LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr }, 1, 0),
                    mkdir_fn, &path_arg, 1, "");
            } else {
                LLVMValueRef mkdir_fn = LLVMGetNamedFunction(g->mod, "mkdir");
                if (!mkdir_fn) {
                    LLVMTypeRef ft = LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr, i32 }, 2, 0);
                    mkdir_fn = LLVMAddFunction(g->mod, "mkdir", ft);
                }
                LLVMValueRef margs[] = { path_arg, LLVMConstInt(i32, 0777, 0) };
                zan_call2(g->builder, LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr, i32 }, 2, 0),
                    mkdir_fn, margs, 2, "");
            }
            emit_release_owned_call_temp(g, expr->call.args.items[0], path_arg, locals);
            return LLVMConstInt(i32, 0, 0);
        }

        /* Directory.Delete(path) */
        if (!zan_type_defines(g, "Directory", "Delete") &&
            is_call_to(expr, "Directory", "Delete") && expr->call.args.count == 1) {
            LLVMValueRef path_arg = emit_expr(g, expr->call.args.items[0], locals);
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
            const char *rmname = g->target_is_windows ? "_rmdir" : "rmdir";
            LLVMValueRef rmdir_fn = LLVMGetNamedFunction(g->mod, rmname);
            if (!rmdir_fn) {
                LLVMTypeRef ft = LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr }, 1, 0);
                rmdir_fn = LLVMAddFunction(g->mod, rmname, ft);
            }
            zan_call2(g->builder, LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr }, 1, 0),
                rmdir_fn, &path_arg, 1, "");
            emit_release_owned_call_temp(g, expr->call.args.items[0], path_arg, locals);
            return LLVMConstInt(i32, 0, 0);
        }

        /* Directory.GetCurrentDirectory() -> string */
        if (!zan_type_defines(g, "Directory", "GetCurrentDirectory") &&
            is_call_to(expr, "Directory", "GetCurrentDirectory") && expr->call.args.count == 0) {
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
            LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
            LLVMValueRef buf = emit_string_alloc_rc(g, LLVMConstInt(i64, 4096, 0));
            const char *cwdname = g->target_is_windows ? "_getcwd" : "getcwd";
            LLVMValueRef getcwd_fn = LLVMGetNamedFunction(g->mod, cwdname);
            if (!getcwd_fn) {
                LLVMTypeRef ft = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i32 }, 2, 0);
                getcwd_fn = LLVMAddFunction(g->mod, cwdname, ft);
            }
            LLVMValueRef cwd_args[] = { buf, LLVMConstInt(i32, 4096, 0) };
            zan_call2(g->builder, LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i32 }, 2, 0),
                getcwd_fn, cwd_args, 2, "");
            return buf;
        }

        /* Directory.SetCurrentDirectory(path) */
        if (!zan_type_defines(g, "Directory", "SetCurrentDirectory") &&
            is_call_to(expr, "Directory", "SetCurrentDirectory") && expr->call.args.count == 1) {
            LLVMValueRef path_arg = emit_expr(g, expr->call.args.items[0], locals);
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
            const char *chname = g->target_is_windows ? "_chdir" : "chdir";
            LLVMValueRef chdir_fn = LLVMGetNamedFunction(g->mod, chname);
            if (!chdir_fn) {
                LLVMTypeRef ft = LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr }, 1, 0);
                chdir_fn = LLVMAddFunction(g->mod, chname, ft);
            }
            zan_call2(g->builder, LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr }, 1, 0),
                chdir_fn, &path_arg, 1, "");
            emit_release_owned_call_temp(g, expr->call.args.items[0], path_arg, locals);
            return LLVMConstInt(i32, 0, 0);
        }

        /* Path.GetDirectoryName(path) -> string */
        if (!zan_type_defines(g, "Path", "GetDirectoryName") &&
            is_call_to(expr, "Path", "GetDirectoryName") && expr->call.args.count == 1) {
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
            LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
            LLVMValueRef path_val = emit_expr(g, expr->call.args.items[0], locals);
            /* strlen */
            LLVMValueRef len = zan_call2(g->builder,
                LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr }, 1, 0), g->fn_strlen, &path_val, 1, "plen");
            /* allocate copy */
            LLVMValueRef bsz = zan_add(g->builder, len, LLVMConstInt(i64, 1, 0), "bsz");
            LLVMValueRef buf = emit_string_alloc_rc(g, bsz);
            LLVMValueRef cpy_args[] = { buf, path_val };
            zan_call2(g->builder, LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0),
                g->fn_strcpy, cpy_args, 2, "");
            /* find last separator */
            LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
            LLVMValueRef strrchr_fn = LLVMGetNamedFunction(g->mod, "strrchr");
            LLVMValueRef slash_args[] = { buf, LLVMConstInt(i32t, '/', 0) };
            LLVMValueRef slash = zan_call2(g->builder,
                LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i32t }, 2, 0),
                strrchr_fn, slash_args, 2, "sl");
            LLVMValueRef bslash_args[] = { buf, LLVMConstInt(i32t, 92, 0) };
            LLVMValueRef bslash = zan_call2(g->builder,
                LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i32t }, 2, 0),
                strrchr_fn, bslash_args, 2, "bsl");
            /* pick later one */
            LLVMValueRef sl_null = zan_icmp(g->builder, LLVMIntEQ, slash, LLVMConstNull(i8ptr), "snul");
            LLVMValueRef pick = LLVMBuildSelect(g->builder, sl_null, bslash, slash, "pk1");
            LLVMValueRef bs_null = zan_icmp(g->builder, LLVMIntEQ, bslash, LLVMConstNull(i8ptr), "bnul");
            LLVMValueRef sep_ptr = LLVMBuildSelect(g->builder, bs_null, pick,
                LLVMBuildSelect(g->builder,
                    zan_icmp(g->builder, LLVMIntUGT, bslash, pick, "bgt"),
                    bslash, pick, "pk2"), "sep");
            /* truncate at separator */
            LLVMValueRef found = zan_icmp(g->builder, LLVMIntNE, sep_ptr, LLVMConstNull(i8ptr), "fnd");
            LLVMBasicBlockRef trunc_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "dn.trunc");
            LLVMBasicBlockRef end_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "dn.end");
            LLVMBuildCondBr(g->builder, found, trunc_bb, end_bb);
            LLVMPositionBuilderAtEnd(g->builder, trunc_bb);
            LLVMBuildStore(g->builder, LLVMConstInt(i8, 0, 0), sep_ptr);
            LLVMBuildBr(g->builder, end_bb);
            LLVMPositionBuilderAtEnd(g->builder, end_bb);
            emit_release_owned_call_temp(g, expr->call.args.items[0], path_val, locals);
            return buf;
        }

        /* Path.HasExtension(path) -> bool */
        if (!zan_type_defines(g, "Path", "HasExtension") &&
            is_call_to(expr, "Path", "HasExtension") && expr->call.args.count == 1) {
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
            LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
            LLVMValueRef path_val = emit_expr(g, expr->call.args.items[0], locals);
            LLVMValueRef strrchr_fn = LLVMGetNamedFunction(g->mod, "strrchr");
            LLVMValueRef dot_args[] = { path_val, LLVMConstInt(i32t, '.', 0) };
            LLVMValueRef dot = zan_call2(g->builder,
                LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i32t }, 2, 0),
                strrchr_fn, dot_args, 2, "dot");
            LLVMValueRef has = zan_icmp(g->builder, LLVMIntNE, dot, LLVMConstNull(i8ptr), "hasext");
            emit_release_owned_call_temp(g, expr->call.args.items[0], path_val, locals);
            return LLVMBuildZExt(g->builder, has, i64, "he");
        }

        /* Path.GetTempPath() -> string */
        if (!zan_type_defines(g, "Path", "GetTempPath") &&
            is_call_to(expr, "Path", "GetTempPath") && expr->call.args.count == 0) {
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
            LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
            LLVMValueRef buf = emit_string_alloc_rc(g, LLVMConstInt(i64, 260, 0));
            if (g->target_is_windows) {
                LLVMValueRef gtp_fn = LLVMGetNamedFunction(g->mod, "GetTempPathA");
                if (!gtp_fn) {
                    LLVMTypeRef ft = LLVMFunctionType(i32, (LLVMTypeRef[]){ i32, i8ptr }, 2, 0);
                    gtp_fn = LLVMAddFunction(g->mod, "GetTempPathA", ft);
                }
                LLVMValueRef gtp_args[] = { LLVMConstInt(i32, 260, 0), buf };
                zan_call2(g->builder, LLVMFunctionType(i32, (LLVMTypeRef[]){ i32, i8ptr }, 2, 0),
                    gtp_fn, gtp_args, 2, "");
            } else {
                LLVMValueRef getenv_fn = LLVMGetNamedFunction(g->mod, "getenv");
                if (!getenv_fn) {
                    LLVMTypeRef ft = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr }, 1, 0);
                    getenv_fn = LLVMAddFunction(g->mod, "getenv", ft);
                }
                LLVMValueRef key = LLVMBuildGlobalStringPtr(g->builder, "TMPDIR", "tmpdir_k");
                LLVMValueRef env = zan_call2(g->builder,
                    LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr }, 1, 0),
                    getenv_fn, &key, 1, "tmpenv");
                LLVMValueRef isnull = zan_icmp(g->builder, LLVMIntEQ, env, LLVMConstNull(i8ptr), "tnull");
                LLVMValueRef deflt = LLVMBuildGlobalStringPtr(g->builder, "/tmp/", "tmpdef");
                LLVMValueRef src = LLVMBuildSelect(g->builder, isnull, deflt, env, "tmpsrc");
                LLVMValueRef cpy_args[] = { buf, src };
                zan_call2(g->builder, LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0),
                    g->fn_strcpy, cpy_args, 2, "");
            }
            return buf;
        }

        /* Path.GetFileNameWithoutExtension(path) -> string */
        if (!zan_type_defines(g, "Path", "GetFileNameWithoutExtension") &&
            is_call_to(expr, "Path", "GetFileNameWithoutExtension") && expr->call.args.count == 1) {
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
            LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
            LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
            LLVMValueRef path_val = emit_expr(g, expr->call.args.items[0], locals);
            /* Get filename first (reuse strrchr logic) */
            LLVMValueRef strrchr_fn = LLVMGetNamedFunction(g->mod, "strrchr");
            LLVMValueRef slash_args[] = { path_val, LLVMConstInt(i32t, '/', 0) };
            LLVMValueRef slash = zan_call2(g->builder,
                LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i32t }, 2, 0),
                strrchr_fn, slash_args, 2, "sl");
            LLVMValueRef bslash_args[] = { path_val, LLVMConstInt(i32t, 92, 0) };
            LLVMValueRef bslash = zan_call2(g->builder,
                LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i32t }, 2, 0),
                strrchr_fn, bslash_args, 2, "bsl");
            /* pick later separator */
            LLVMValueRef sl_null = zan_icmp(g->builder, LLVMIntEQ, slash, LLVMConstNull(i8ptr), "snul");
            LLVMValueRef bs_null = zan_icmp(g->builder, LLVMIntEQ, bslash, LLVMConstNull(i8ptr), "bnul");
            LLVMValueRef best = LLVMBuildSelect(g->builder, sl_null, bslash,
                LLVMBuildSelect(g->builder, bs_null, slash,
                    LLVMBuildSelect(g->builder, zan_icmp(g->builder, LLVMIntUGT, bslash, slash, "bgt"),
                        bslash, slash, "mx"), "pk"), "sep");
            LLVMValueRef has_sep = zan_icmp(g->builder, LLVMIntNE, best, LLVMConstNull(i8ptr), "hs");
            /* filename starts after separator+1, or is the whole path */
            LLVMValueRef after = LLVMBuildGEP2(g->builder, i8, best, &(LLVMValueRef){LLVMConstInt(i64, 1, 0)}, 1, "aft");
            LLVMValueRef fname = LLVMBuildSelect(g->builder, has_sep, after, path_val, "fn");
            /* make a copy, then truncate at last dot */
            LLVMValueRef flen = zan_call2(g->builder,
                LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr }, 1, 0), g->fn_strlen, &fname, 1, "flen");
            LLVMValueRef bsz = zan_add(g->builder, flen, LLVMConstInt(i64, 1, 0), "bsz");
            LLVMValueRef buf = emit_string_alloc_rc(g, bsz);
            LLVMValueRef cpy_args[] = { buf, fname };
            zan_call2(g->builder, LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0),
                g->fn_strcpy, cpy_args, 2, "");
            LLVMValueRef dot_args[] = { buf, LLVMConstInt(i32t, '.', 0) };
            LLVMValueRef dot = zan_call2(g->builder,
                LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i32t }, 2, 0),
                strrchr_fn, dot_args, 2, "dot");
            LLVMValueRef has_dot = zan_icmp(g->builder, LLVMIntNE, dot, LLVMConstNull(i8ptr), "hd");
            LLVMBasicBlockRef trunc_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "fnwe.trunc");
            LLVMBasicBlockRef end_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "fnwe.end");
            LLVMBuildCondBr(g->builder, has_dot, trunc_bb, end_bb);
            LLVMPositionBuilderAtEnd(g->builder, trunc_bb);
            LLVMBuildStore(g->builder, LLVMConstInt(i8, 0, 0), dot);
            LLVMBuildBr(g->builder, end_bb);
            LLVMPositionBuilderAtEnd(g->builder, end_bb);
            return buf;
        }


                /* List.Add(item) — append to dynamic list */
        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS) {
            zan_ast_node_t *callee = expr->call.callee;
            zan_istr_t method_name = callee->member.name;
            if (method_name.len == 3 && memcmp(method_name.str, "Add", 3) == 0 &&
                expr->call.args.count == 1) {
                zan_ast_node_t *lobj = callee->member.object;
                zan_type_t *ltype = infer_expr_type(g, lobj, locals);
                if (ltype && type_named(ltype, "List", 4)) {
                    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                    /* load list pointer (works for local vars and fields) */
                    LLVMValueRef raw_ptr = emit_expr(g, lobj, locals);
                    int recv_own = emit_intrinsic_own_recv(g, lobj, raw_ptr, locals);
                    (void)i8ptr;
                    LLVMValueRef list_ptr = LLVMBuildBitCast(g->builder, raw_ptr,
                        LLVMPointerType(g->list_struct_type, 0), "lptr");
                    /* load count */
                    LLVMValueRef count_ptr = LLVMBuildStructGEP2(g->builder, g->list_struct_type, list_ptr, 0, "cntp");
                    LLVMValueRef count = LLVMBuildLoad2(g->builder, i64, count_ptr, "cnt");
                    /* load capacity */
                    LLVMValueRef cap_ptr = LLVMBuildStructGEP2(g->builder, g->list_struct_type, list_ptr, 1, "capp");
                    LLVMValueRef cap = LLVMBuildLoad2(g->builder, i64, cap_ptr, "cap");
                    /* check if need to grow: if count >= capacity */
                    LLVMValueRef need_grow = zan_icmp(g->builder, LLVMIntUGE, count, cap, "grow");
                    LLVMBasicBlockRef grow_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "list.grow");
                    LLVMBasicBlockRef add_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "list.add");
                    LLVMBuildCondBr(g->builder, need_grow, grow_bb, add_bb);
                    /* grow block: double capacity, realloc */
                    LLVMPositionBuilderAtEnd(g->builder, grow_bb);
                    LLVMValueRef new_cap = zan_mul(g->builder, cap, LLVMConstInt(i64, 2, 0), "ncap");
                    LLVMBuildStore(g->builder, new_cap, cap_ptr);
                    LLVMValueRef data_field = LLVMBuildStructGEP2(g->builder, g->list_struct_type, list_ptr, 2, "df");
                    LLVMValueRef old_data = LLVMBuildLoad2(g->builder, LLVMPointerType(i64, 0), data_field, "od");
                    LLVMValueRef old_data_raw = LLVMBuildBitCast(g->builder, old_data, i8ptr, "odr");
                    unsigned lwords = elem_slot_words(g, container_elem_type(ltype));
                    LLVMValueRef new_size = zan_mul(g->builder, new_cap,
                        LLVMConstInt(i64, (unsigned long long)(8 * lwords), 0), "nsz");
                    LLVMValueRef realloc_args[] = { old_data_raw, new_size };
                    LLVMValueRef new_data_raw = zan_call2(g->builder,
                        LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i64 }, 2, 0),
                        g->fn_realloc, realloc_args, 2, "nd");
                    zan_irgen_emit_oom_check(g, g->current_fn, new_data_raw);
                    LLVMValueRef new_data = LLVMBuildBitCast(g->builder, new_data_raw, LLVMPointerType(i64, 0), "ndt");
                    LLVMBuildStore(g->builder, new_data, data_field);
                    LLVMBuildBr(g->builder, add_bb);
                    /* add block: store value at data[count], increment count */
                    LLVMPositionBuilderAtEnd(g->builder, add_bb);
                    LLVMValueRef data_field2 = LLVMBuildStructGEP2(g->builder, g->list_struct_type, list_ptr, 2, "df2");
                    LLVMValueRef data = LLVMBuildLoad2(g->builder, LLVMPointerType(i64, 0), data_field2, "d");
                    LLVMValueRef count2 = LLVMBuildLoad2(g->builder, i64, count_ptr, "cnt2");
                    LLVMValueRef wpos = slot_word_index(g, count2, lwords);
                    LLVMValueRef elem_ptr = LLVMBuildGEP2(g->builder, i64, data, &wpos, 1, "ep");
                    /* emit the value to add */
                    LLVMValueRef val = emit_expr(g, expr->call.args.items[0], locals);
                    emit_collection_slot_store(g, container_elem_type(ltype), i64, elem_ptr,
                        val, expr->call.args.items[0], locals, 0);
                    /* count++ */
                    LLVMValueRef new_count = zan_add(g->builder, count2, LLVMConstInt(i64, 1, 0), "nc");
                    LLVMBuildStore(g->builder, new_count, count_ptr);
                    emit_intrinsic_drop_recv(g, lobj, raw_ptr, locals, recv_own);
                    return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
                }
            }
        }

        /* List.AddRange(other) — append every element of another list */
        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS) {
            zan_ast_node_t *callee = expr->call.callee;
            zan_istr_t method_name = callee->member.name;
            if (method_name.len == 8 && memcmp(method_name.str, "AddRange", 8) == 0 &&
                expr->call.args.count == 1) {
                zan_ast_node_t *lobj = callee->member.object;
                zan_type_t *ltype = infer_expr_type(g, lobj, locals);
                if (ltype && type_named(ltype, "List", 4)) {
                    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                    LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
                    LLVMTypeRef i64ptr = LLVMPointerType(i64, 0);
                    LLVMTypeRef list_pt = LLVMPointerType(g->list_struct_type, 0);
                    zan_type_t *elem_type = container_elem_type(ltype);
                    unsigned ar_words = elem_slot_words(g, elem_type);
                    LLVMTypeRef elem_llvm = elem_type ? map_type(g, elem_type) : i64;
                    /* self + other list pointers */
                    LLVMValueRef self_raw = emit_expr(g, lobj, locals);
                    LLVMValueRef self_ptr = LLVMBuildBitCast(g->builder, self_raw, list_pt, "ar.self");
                    /* The source is another List or (C#: AddRange takes an
                     * IEnumerable) an array. An array carries its length in
                     * the allocation header and packs elements by their own
                     * type, so it needs its own count/read pair -- reading it
                     * as a list struct took the first element for a count and
                     * a data pointer out of the elements. */
                    zan_type_t *otype = infer_expr_type(g, expr->call.args.items[0], locals);
                    int src_array = otype && otype->kind == TYPE_ARRAY &&
                                    otype->array_rank <= 1;
                    LLVMTypeRef src_elem_llvm = elem_llvm;
                    if (src_array)
                        src_elem_llvm = otype->element_type
                            ? map_type(g, otype->element_type) : i64;
                    LLVMValueRef other_raw = emit_expr(g, expr->call.args.items[0], locals);
                    LLVMValueRef other_ptr = LLVMBuildBitCast(g->builder, other_raw, list_pt, "ar.other");
                    /* skip entirely when other is null */
                    LLVMValueRef other_i = LLVMBuildPtrToInt(g->builder, other_ptr, i64, "ar.oi");
                    LLVMValueRef is_null = zan_icmp(g->builder, LLVMIntEQ, other_i,
                        LLVMConstInt(i64, 0, 0), "ar.null");
                    LLVMBasicBlockRef ar_body0 = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "ar.enter");
                    LLVMBasicBlockRef ar_done = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "ar.done");
                    LLVMBuildCondBr(g->builder, is_null, ar_done, ar_body0);
                    LLVMPositionBuilderAtEnd(g->builder, ar_body0);
                    /* snapshot other's count (so self.AddRange(self) terminates) */
                    LLVMValueRef ocnt;
                    if (src_array) {
                        ocnt = zan_array_len(g, other_raw);
                    } else {
                        LLVMValueRef ocnt_ptr = LLVMBuildStructGEP2(g->builder, g->list_struct_type, other_ptr, 0, "ar.ocp");
                        ocnt = LLVMBuildLoad2(g->builder, i64, ocnt_ptr, "ar.ocnt");
                    }
                    LLVMValueRef idx_a = emit_entry_alloca(g, i64, "ar.i");
                    LLVMBuildStore(g->builder, LLVMConstInt(i64, 0, 0), idx_a);
                    LLVMBasicBlockRef c_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "ar.cond");
                    LLVMBasicBlockRef b_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "ar.step");
                    LLVMBuildBr(g->builder, c_bb);
                    LLVMPositionBuilderAtEnd(g->builder, c_bb);
                    LLVMValueRef ci = LLVMBuildLoad2(g->builder, i64, idx_a, "ar.ci");
                    LLVMValueRef more = zan_icmp(g->builder, LLVMIntULT, ci, ocnt, "ar.more");
                    LLVMBuildCondBr(g->builder, more, b_bb, ar_done);
                    LLVMPositionBuilderAtEnd(g->builder, b_bb);
                    /* read other[i] (reload data each step in case other == self grew) */
                    LLVMValueRef ci2 = LLVMBuildLoad2(g->builder, i64, idx_a, "ar.ci2");
                    LLVMValueRef rawv;
                    if (src_array) {
                        LLVMValueRef typed = LLVMBuildBitCast(g->builder, other_raw,
                            LLVMPointerType(src_elem_llvm, 0), "ar.ap");
                        LLVMValueRef ep = LLVMBuildGEP2(g->builder, src_elem_llvm,
                            typed, &ci2, 1, "ar.aep");
                        rawv = LLVMBuildLoad2(g->builder, src_elem_llvm, ep, "ar.av");
                        /* encode the element the way a list slot holds it */
                        switch (LLVMGetTypeKind(src_elem_llvm)) {
                        case LLVMStructTypeKind:
                            break;
                        case LLVMPointerTypeKind:
                            rawv = LLVMBuildPtrToInt(g->builder, rawv, i64, "ar.api");
                            break;
                        case LLVMDoubleTypeKind:
                        case LLVMFloatTypeKind:
                            if (LLVMGetTypeKind(src_elem_llvm) == LLVMFloatTypeKind)
                                rawv = LLVMBuildFPExt(g->builder, rawv,
                                    LLVMDoubleTypeInContext(g->ctx), "ar.afx");
                            rawv = LLVMBuildBitCast(g->builder, rawv, i64, "ar.afb");
                            break;
                        default:
                            if (LLVMGetIntTypeWidth(LLVMTypeOf(rawv)) < 64)
                                rawv = extend_int_for_slot(g, rawv,
                                    otype->element_type, i64);
                            break;
                        }
                    } else {
                        LLVMValueRef odata_field = LLVMBuildStructGEP2(g->builder, g->list_struct_type, other_ptr, 2, "ar.odf");
                        LLVMValueRef odata = LLVMBuildLoad2(g->builder, i64ptr, odata_field, "ar.od");
                        LLVMValueRef owpos = slot_word_index(g, ci2, ar_words);
                        LLVMValueRef oslot = LLVMBuildGEP2(g->builder, i64, odata, &owpos, 1, "ar.os");
                        rawv = LLVMGetTypeKind(elem_llvm) == LLVMStructTypeKind
                            ? load_struct_from_slot(g, oslot, elem_llvm)
                            : LLVMBuildLoad2(g->builder, i64, oslot, "ar.rv");
                    }
                    /* grow self if full (mirrors List.Add) */
                    LLVMValueRef s_cnt_ptr = LLVMBuildStructGEP2(g->builder, g->list_struct_type, self_ptr, 0, "ar.scp");
                    LLVMValueRef s_cnt = LLVMBuildLoad2(g->builder, i64, s_cnt_ptr, "ar.sc");
                    LLVMValueRef s_cap_ptr = LLVMBuildStructGEP2(g->builder, g->list_struct_type, self_ptr, 1, "ar.scapp");
                    LLVMValueRef s_cap = LLVMBuildLoad2(g->builder, i64, s_cap_ptr, "ar.scap");
                    LLVMValueRef need = zan_icmp(g->builder, LLVMIntUGE, s_cnt, s_cap, "ar.grow");
                    LLVMBasicBlockRef g_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "ar.grow");
                    LLVMBasicBlockRef s_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "ar.store");
                    LLVMBuildCondBr(g->builder, need, g_bb, s_bb);
                    LLVMPositionBuilderAtEnd(g->builder, g_bb);
                    /* newcap = cap == 0 ? 4 : cap * 2 */
                    LLVMValueRef cap_zero = zan_icmp(g->builder, LLVMIntEQ, s_cap,
                        LLVMConstInt(i64, 0, 0), "ar.cz");
                    LLVMValueRef dbl = zan_mul(g->builder, s_cap, LLVMConstInt(i64, 2, 0), "ar.dbl");
                    LLVMValueRef ncap = LLVMBuildSelect(g->builder, cap_zero,
                        LLVMConstInt(i64, 4, 0), dbl, "ar.ncap");
                    LLVMBuildStore(g->builder, ncap, s_cap_ptr);
                    LLVMValueRef s_df = LLVMBuildStructGEP2(g->builder, g->list_struct_type, self_ptr, 2, "ar.sdf");
                    LLVMValueRef old_data = LLVMBuildLoad2(g->builder, i64ptr, s_df, "ar.oldd");
                    LLVMValueRef old_raw = LLVMBuildBitCast(g->builder, old_data, i8ptr, "ar.oldr");
                    LLVMValueRef nsz = zan_mul(g->builder, ncap,
                        LLVMConstInt(i64, 8ULL * ar_words, 0), "ar.nsz");
                    LLVMValueRef re_args[] = { old_raw, nsz };
                    LLVMValueRef nd_raw = zan_call2(g->builder,
                        LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i64 }, 2, 0),
                        g->fn_realloc, re_args, 2, "ar.nd");
                    zan_irgen_emit_oom_check(g, g->current_fn, nd_raw);
                    LLVMValueRef nd = LLVMBuildBitCast(g->builder, nd_raw, i64ptr, "ar.ndt");
                    LLVMBuildStore(g->builder, nd, s_df);
                    LLVMBuildBr(g->builder, s_bb);
                    LLVMPositionBuilderAtEnd(g->builder, s_bb);
                    /* store raw value at self.data[count] */
                    LLVMValueRef s_df2 = LLVMBuildStructGEP2(g->builder, g->list_struct_type, self_ptr, 2, "ar.sdf2");
                    LLVMValueRef s_data = LLVMBuildLoad2(g->builder, i64ptr, s_df2, "ar.sd");
                    LLVMValueRef s_cnt2 = LLVMBuildLoad2(g->builder, i64, s_cnt_ptr, "ar.sc2");
                    LLVMValueRef swpos = slot_word_index(g, s_cnt2, ar_words);
                    LLVMValueRef s_slot = LLVMBuildGEP2(g->builder, i64, s_data, &swpos, 1, "ar.ss");
                    if (LLVMGetTypeKind(elem_llvm) == LLVMStructTypeKind)
                        store_struct_in_slot(g, rawv, s_slot, expr);
                    else
                        LLVMBuildStore(g->builder, rawv, s_slot);
                    /* both lists now reference the element: retain if managed */
                    if (is_rc_managed_type(elem_type)) {
                        LLVMTypeRef mt = map_type(g, elem_type);
                        if (LLVMGetTypeKind(mt) == LLVMPointerTypeKind) {
                            LLVMValueRef pv = LLVMBuildIntToPtr(g->builder, rawv, mt, "ar.pv");
                            emit_rc_retain_for_type(g, elem_type, pv);
                        }
                    }
                    LLVMValueRef s_nc = zan_add(g->builder, s_cnt2, LLVMConstInt(i64, 1, 0), "ar.snc");
                    LLVMBuildStore(g->builder, s_nc, s_cnt_ptr);
                    LLVMValueRef ni = zan_add(g->builder, ci2, LLVMConstInt(i64, 1, 0), "ar.ni");
                    LLVMBuildStore(g->builder, ni, idx_a);
                    LLVMBuildBr(g->builder, c_bb);
                    LLVMPositionBuilderAtEnd(g->builder, ar_done);
                    return LLVMConstInt(i32t, 0, 0);
                }
            }
        }

        /* List.Clear() — reset count to 0 */
        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS) {
            zan_ast_node_t *callee = expr->call.callee;
            zan_istr_t method_name = callee->member.name;
            if (method_name.len == 5 && memcmp(method_name.str, "Clear", 5) == 0 &&
                expr->call.args.count == 0) {
                zan_ast_node_t *lobj = callee->member.object;
                zan_type_t *ltype = infer_expr_type(g, lobj, locals);
                if (ltype && type_named(ltype, "List", 4)) {
                    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                    LLVMValueRef raw_ptr = emit_expr(g, lobj, locals);
                    LLVMValueRef list_ptr = LLVMBuildBitCast(g->builder, raw_ptr,
                        LLVMPointerType(g->list_struct_type, 0), "lptr");
                    LLVMValueRef count_ptr = LLVMBuildStructGEP2(g->builder, g->list_struct_type, list_ptr, 0, "cntp");
                    LLVMValueRef count = LLVMBuildLoad2(g->builder, i64, count_ptr, "cnt");
                    LLVMValueRef data_field = LLVMBuildStructGEP2(g->builder, g->list_struct_type, list_ptr, 2, "df");
                    LLVMValueRef data = LLVMBuildLoad2(g->builder, LLVMPointerType(i64, 0), data_field, "data");
                    LLVMValueRef idx_a = emit_entry_alloca(g, i64, "lc");
                    LLVMBuildStore(g->builder, LLVMConstInt(i64, 0, 0), idx_a);
                    LLVMBasicBlockRef c_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "lc.cond");
                    LLVMBasicBlockRef b_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "lc.body");
                    LLVMBasicBlockRef d_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "lc.done");
                    LLVMBuildBr(g->builder, c_bb);
                    LLVMPositionBuilderAtEnd(g->builder, c_bb);
                    LLVMValueRef ci = LLVMBuildLoad2(g->builder, i64, idx_a, "ci");
                    LLVMValueRef more = zan_icmp(g->builder, LLVMIntULT, ci, count, "more");
                    LLVMBuildCondBr(g->builder, more, b_bb, d_bb);
                    LLVMPositionBuilderAtEnd(g->builder, b_bb);
                    LLVMValueRef ci2 = LLVMBuildLoad2(g->builder, i64, idx_a, "ci2");
                    LLVMValueRef cw = slot_word_index(g, ci2,
                        elem_slot_words(g, container_elem_type(ltype)));
                    LLVMValueRef slot = LLVMBuildGEP2(g->builder, i64, data, &cw, 1, "sl");
                    zan_type_t *elem_type = container_elem_type(ltype);
                    LLVMValueRef val = load_collection_slot_value(g, elem_type, slot);
                    emit_collection_release_raw_slot(g, elem_type, val, i64);
                    LLVMValueRef ni = zan_add(g->builder, ci2, LLVMConstInt(i64, 1, 0), "ni");
                    LLVMBuildStore(g->builder, ni, idx_a);
                    LLVMBuildBr(g->builder, c_bb);
                    LLVMPositionBuilderAtEnd(g->builder, d_bb);
                    LLVMBuildStore(g->builder, LLVMConstInt(i64, 0, 0), count_ptr);
                    return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
                }
            }
        }

        /* List.RemoveAt(index) — shift elements left, decrement count */
        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS) {
            zan_ast_node_t *callee = expr->call.callee;
            zan_istr_t method_name = callee->member.name;
            if (method_name.len == 8 && memcmp(method_name.str, "RemoveAt", 8) == 0 &&
                expr->call.args.count == 1) {
                zan_ast_node_t *lobj = callee->member.object;
                zan_type_t *ltype = infer_expr_type(g, lobj, locals);
                if (ltype && type_named(ltype, "List", 4)) {
                    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                    LLVMValueRef raw_ptr = emit_expr(g, lobj, locals);
                    LLVMValueRef list_ptr = LLVMBuildBitCast(g->builder, raw_ptr,
                        LLVMPointerType(g->list_struct_type, 0), "lptr");
                    LLVMValueRef count_ptr = LLVMBuildStructGEP2(g->builder, g->list_struct_type, list_ptr, 0, "cntp");
                    LLVMValueRef count = LLVMBuildLoad2(g->builder, i64, count_ptr, "cnt");
                    LLVMValueRef data_field = LLVMBuildStructGEP2(g->builder, g->list_struct_type, list_ptr, 2, "df");
                    LLVMValueRef data = LLVMBuildLoad2(g->builder, LLVMPointerType(i64, 0), data_field, "data");
                    LLVMValueRef idx = emit_expr(g, expr->call.args.items[0], locals);
                    /* the index is `int` (i32); all buffer/word math below is
                     * i64, and the loop counter is stored into an i64 slot, so
                     * widen it here rather than store a half word. */
                    if (LLVMGetTypeKind(LLVMTypeOf(idx)) == LLVMIntegerTypeKind &&
                        LLVMGetIntTypeWidth(LLVMTypeOf(idx)) < 64) {
                        idx = LLVMBuildSExt(g->builder, idx, i64, "idx.sx");
                    }
                    emit_index_bounds_check(g, idx, count, expr->loc, "list");
                    unsigned rwords = elem_slot_words(g, container_elem_type(ltype));
                    LLVMValueRef widx = slot_word_index(g, idx, rwords);
                    LLVMValueRef removed_ptr = LLVMBuildGEP2(g->builder, i64, data, &widx, 1, "rmp");
                    zan_type_t *removed_type = container_elem_type(ltype);
                    LLVMValueRef removed = load_collection_slot_value(g,
                        removed_type, removed_ptr);
                    emit_collection_release_raw_slot(g, removed_type, removed, i64);
                    /* shift loop, in slot words so a multi-word element moves
                     * whole: for w = idx*words; w < (count-1)*words; w++ :
                     * data[w] = data[w+words] */
                    LLVMValueRef j_a = emit_entry_alloca(g, i64, "j");
                    LLVMBuildStore(g->builder, widx, j_a);
                    LLVMValueRef last = zan_sub(g->builder, count, LLVMConstInt(i64, 1, 0), "last");
                    LLVMValueRef last_w = slot_word_index(g, last, rwords);
                    LLVMBasicBlockRef cond_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "ra.cond");
                    LLVMBasicBlockRef body_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "ra.body");
                    LLVMBasicBlockRef done_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "ra.done");
                    LLVMBuildBr(g->builder, cond_bb);
                    LLVMPositionBuilderAtEnd(g->builder, cond_bb);
                    LLVMValueRef j = LLVMBuildLoad2(g->builder, i64, j_a, "j");
                    LLVMValueRef cmp = zan_icmp(g->builder, LLVMIntULT, j, last_w, "cmp");
                    LLVMBuildCondBr(g->builder, cmp, body_bb, done_bb);
                    LLVMPositionBuilderAtEnd(g->builder, body_bb);
                    LLVMValueRef j2 = LLVMBuildLoad2(g->builder, i64, j_a, "j2");
                    LLVMValueRef next = zan_add(g->builder, j2,
                        LLVMConstInt(i64, rwords, 0), "nxt");
                    LLVMValueRef src_slot = LLVMBuildGEP2(g->builder, i64, data, &next, 1, "ss");
                    LLVMValueRef val = LLVMBuildLoad2(g->builder, i64, src_slot, "sv");
                    LLVMValueRef dst_slot = LLVMBuildGEP2(g->builder, i64, data, &j2, 1, "ds");
                    LLVMBuildStore(g->builder, val, dst_slot);
                    LLVMValueRef j3 = zan_add(g->builder, j2, LLVMConstInt(i64, 1, 0), "j3");
                    LLVMBuildStore(g->builder, j3, j_a);
                    LLVMBuildBr(g->builder, cond_bb);
                    LLVMPositionBuilderAtEnd(g->builder, done_bb);
                    LLVMBuildStore(g->builder, last, count_ptr);
                    for (unsigned tw = 0; tw < rwords; tw++) {
                        LLVMValueRef tpos = zan_add(g->builder, last_w,
                            LLVMConstInt(i64, tw, 0), "tw");
                        LLVMValueRef tail_ptr = LLVMBuildGEP2(g->builder, i64, data,
                            &tpos, 1, "tail");
                        LLVMBuildStore(g->builder, LLVMConstInt(i64, 0, 0), tail_ptr);
                    }
                    return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
                }
            }
        }

        /* List.IndexOf(item) -> int (-1 if not found) */
        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS) {
            zan_ast_node_t *callee = expr->call.callee;
            zan_istr_t method_name = callee->member.name;
            if (method_name.len == 7 && memcmp(method_name.str, "IndexOf", 7) == 0 &&
                expr->call.args.count == 1) {
                zan_ast_node_t *lobj = callee->member.object;
                zan_type_t *ltype = infer_expr_type(g, lobj, locals);
                if (ltype && type_named(ltype, "List", 4)) {
                    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                    LLVMValueRef raw_ptr = emit_expr(g, lobj, locals);
                    LLVMValueRef list_ptr = LLVMBuildBitCast(g->builder, raw_ptr,
                        LLVMPointerType(g->list_struct_type, 0), "lptr");
                    LLVMValueRef count_ptr = LLVMBuildStructGEP2(g->builder, g->list_struct_type, list_ptr, 0, "cntp");
                    LLVMValueRef count = LLVMBuildLoad2(g->builder, i64, count_ptr, "cnt");
                    LLVMValueRef data_field = LLVMBuildStructGEP2(g->builder, g->list_struct_type, list_ptr, 2, "df");
                    LLVMValueRef data = LLVMBuildLoad2(g->builder, LLVMPointerType(i64, 0), data_field, "data");
                    zan_type_t *elem_type = concretize(g,
                        container_elem_type(ltype));
                    unsigned io_words = elem_slot_words(g, elem_type);
                    LLVMValueRef search = emit_expr(g, expr->call.args.items[0], locals);
                    /* result alloca — -1 for not found */
                    LLVMValueRef res = emit_entry_alloca(g, i64, "iofr");
                    LLVMBuildStore(g->builder, LLVMConstInt(i64, (uint64_t)-1LL, 1), res);
                    LLVMValueRef idx_a = emit_entry_alloca(g, i64, "ii");
                    LLVMBuildStore(g->builder, LLVMConstInt(i64, 0, 0), idx_a);
                    LLVMBasicBlockRef cond_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "io.cond");
                    LLVMBasicBlockRef body_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "io.body");
                    LLVMBasicBlockRef done_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "io.done");
                    LLVMBuildBr(g->builder, cond_bb);
                    LLVMPositionBuilderAtEnd(g->builder, cond_bb);
                    LLVMValueRef ci = LLVMBuildLoad2(g->builder, i64, idx_a, "ci");
                    LLVMValueRef cmp_d = zan_icmp(g->builder, LLVMIntUGE, ci, count, "cdone");
                    LLVMBuildCondBr(g->builder, cmp_d, done_bb, body_bb);
                    LLVMPositionBuilderAtEnd(g->builder, body_bb);
                    LLVMValueRef ci2 = LLVMBuildLoad2(g->builder, i64, idx_a, "ci2");
                    LLVMValueRef iw = slot_word_index(g, ci2, io_words);
                    LLVMValueRef slot = LLVMBuildGEP2(g->builder, i64, data, &iw, 1, "sl");
                    LLVMValueRef val = load_collection_slot_value(
                        g, elem_type, slot);
                    LLVMValueRef eq = emit_typed_equality(
                        g, elem_type, val, search);
                    LLVMBasicBlockRef found_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "io.found");
                    LLVMBasicBlockRef next_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "io.next");
                    LLVMBuildCondBr(g->builder, eq, found_bb, next_bb);
                    LLVMPositionBuilderAtEnd(g->builder, found_bb);
                    LLVMBuildStore(g->builder, ci2, res);
                    LLVMBuildBr(g->builder, done_bb);
                    LLVMPositionBuilderAtEnd(g->builder, next_bb);
                    LLVMValueRef ni = zan_add(g->builder, ci2, LLVMConstInt(i64, 1, 0), "ni");
                    LLVMBuildStore(g->builder, ni, idx_a);
                    LLVMBuildBr(g->builder, cond_bb);
                    LLVMPositionBuilderAtEnd(g->builder, done_bb);
                    return LLVMBuildLoad2(g->builder, i64, res, "iofres");
                }
            }
        }

        /* List.Contains(item) -> bool (uses IndexOf logic) */
        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS) {
            zan_ast_node_t *callee = expr->call.callee;
            zan_istr_t method_name = callee->member.name;
            if (method_name.len == 8 && memcmp(method_name.str, "Contains", 8) == 0 &&
                expr->call.args.count == 1) {
                zan_ast_node_t *lobj = callee->member.object;
                zan_type_t *ltype = infer_expr_type(g, lobj, locals);
                if (ltype && type_named(ltype, "List", 4)) {
                    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                    LLVMValueRef raw_ptr = emit_expr(g, lobj, locals);
                    LLVMValueRef list_ptr = LLVMBuildBitCast(g->builder, raw_ptr,
                        LLVMPointerType(g->list_struct_type, 0), "lptr");
                    LLVMValueRef count_ptr = LLVMBuildStructGEP2(g->builder, g->list_struct_type, list_ptr, 0, "cntp");
                    LLVMValueRef count = LLVMBuildLoad2(g->builder, i64, count_ptr, "cnt");
                    LLVMValueRef data_field = LLVMBuildStructGEP2(g->builder, g->list_struct_type, list_ptr, 2, "df");
                    LLVMValueRef data = LLVMBuildLoad2(g->builder, LLVMPointerType(i64, 0), data_field, "data");
                    zan_type_t *elem_type = concretize(g,
                        container_elem_type(ltype));
                    unsigned ct_words = elem_slot_words(g, elem_type);
                    LLVMValueRef search = emit_expr(g, expr->call.args.items[0], locals);
                    LLVMValueRef res = emit_entry_alloca(g, LLVMInt32TypeInContext(g->ctx), "cr");
                    LLVMBuildStore(g->builder, LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0), res);
                    LLVMValueRef idx_a = emit_entry_alloca(g, i64, "ci");
                    LLVMBuildStore(g->builder, LLVMConstInt(i64, 0, 0), idx_a);
                    LLVMBasicBlockRef cond_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "ct.cond");
                    LLVMBasicBlockRef body_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "ct.body");
                    LLVMBasicBlockRef done_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "ct.done");
                    LLVMBuildBr(g->builder, cond_bb);
                    LLVMPositionBuilderAtEnd(g->builder, cond_bb);
                    LLVMValueRef ci = LLVMBuildLoad2(g->builder, i64, idx_a, "ci");
                    LLVMValueRef cmp_d = zan_icmp(g->builder, LLVMIntUGE, ci, count, "cdone");
                    LLVMBuildCondBr(g->builder, cmp_d, done_bb, body_bb);
                    LLVMPositionBuilderAtEnd(g->builder, body_bb);
                    LLVMValueRef ci2 = LLVMBuildLoad2(g->builder, i64, idx_a, "ci2");
                    LLVMValueRef cw = slot_word_index(g, ci2, ct_words);
                    LLVMValueRef slot = LLVMBuildGEP2(g->builder, i64, data, &cw, 1, "sl");
                    LLVMValueRef val = load_collection_slot_value(
                        g, elem_type, slot);
                    LLVMValueRef eq = emit_typed_equality(
                        g, elem_type, val, search);
                    LLVMBasicBlockRef found_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "ct.found");
                    LLVMBasicBlockRef next_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "ct.next");
                    LLVMBuildCondBr(g->builder, eq, found_bb, next_bb);
                    LLVMPositionBuilderAtEnd(g->builder, found_bb);
                    LLVMBuildStore(g->builder, LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 1, 0), res);
                    LLVMBuildBr(g->builder, done_bb);
                    LLVMPositionBuilderAtEnd(g->builder, next_bb);
                    LLVMValueRef ni = zan_add(g->builder, ci2, LLVMConstInt(i64, 1, 0), "ni");
                    LLVMBuildStore(g->builder, ni, idx_a);
                    LLVMBuildBr(g->builder, cond_bb);
                    LLVMPositionBuilderAtEnd(g->builder, done_bb);
                    return LLVMBuildLoad2(g->builder, LLVMInt32TypeInContext(g->ctx), res, "ctres");
                }
            }
        }

        /* List.Insert(index, item) — shift elements right, insert at index */
        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS) {
            zan_ast_node_t *callee = expr->call.callee;
            zan_istr_t method_name = callee->member.name;
            if (method_name.len == 6 && memcmp(method_name.str, "Insert", 6) == 0 &&
                expr->call.args.count == 2) {
                zan_ast_node_t *lobj = callee->member.object;
                zan_type_t *ltype = infer_expr_type(g, lobj, locals);
                if (ltype && type_named(ltype, "List", 4)) {
                    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                    LLVMValueRef raw_ptr = emit_expr(g, lobj, locals);
                    LLVMValueRef list_ptr = LLVMBuildBitCast(g->builder, raw_ptr,
                        LLVMPointerType(g->list_struct_type, 0), "lptr");
                    LLVMValueRef count_ptr = LLVMBuildStructGEP2(g->builder, g->list_struct_type, list_ptr, 0, "cntp");
                    LLVMValueRef count = LLVMBuildLoad2(g->builder, i64, count_ptr, "cnt");
                    LLVMValueRef cap_ptr = LLVMBuildStructGEP2(g->builder, g->list_struct_type, list_ptr, 1, "capp");
                    LLVMValueRef cap = LLVMBuildLoad2(g->builder, i64, cap_ptr, "cap");
                    LLVMValueRef data_field = LLVMBuildStructGEP2(g->builder, g->list_struct_type, list_ptr, 2, "df");
                    LLVMValueRef data = LLVMBuildLoad2(g->builder, LLVMPointerType(i64, 0), data_field, "data");
                    zan_type_t *elem_type = concretize(g,
                        container_elem_type(ltype));
                    unsigned ins_words = elem_slot_words(g, elem_type);
                    LLVMValueRef idx = coerce_int_to(g,
                        emit_expr(g, expr->call.args.items[0], locals), i64);
                    emit_index_range_check(g, idx, count, true,
                                           expr->loc, "list");
                    /* Keep `item` in its natural type (pointer for string/class
                     * elements) so emit_collection_slot_store can retain it:
                     * emit_string_retain/emit_arc_retain no-op on non-pointer
                     * values, so pre-converting to i64 here would silently skip
                     * the +1 and leave the list holding a slot that is freed
                     * when the argument temp dies -> heap corruption. The store
                     * helper performs the pointer->i64 slot conversion itself. */
                    LLVMValueRef item = emit_expr(g, expr->call.args.items[1], locals);
                    /* grow if needed */
                    LLVMValueRef need = zan_icmp(g->builder, LLVMIntUGE, count, cap, "need");
                    LLVMBasicBlockRef grow_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "ins.grow");
                    LLVMBasicBlockRef shift_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "ins.shift");
                    LLVMBuildCondBr(g->builder, need, grow_bb, shift_bb);
                    LLVMPositionBuilderAtEnd(g->builder, grow_bb);
                    LLVMValueRef new_cap = zan_mul(g->builder, cap, LLVMConstInt(i64, 2, 0), "nc");
                    LLVMBuildStore(g->builder, new_cap, cap_ptr);
                    LLVMValueRef nbytes = zan_mul(g->builder, new_cap,
                        LLVMConstInt(i64, 8ULL * ins_words, 0), "nb");
                    LLVMValueRef old_data = LLVMBuildBitCast(g->builder, data, i8ptr, "od");
                    LLVMValueRef new_data = zan_call2(g->builder,
                        LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i64 }, 2, 0),
                        g->fn_realloc, (LLVMValueRef[]){ old_data, nbytes }, 2, "nd");
                    zan_irgen_emit_oom_check(g, g->current_fn, new_data);
                    LLVMValueRef new_data_i = LLVMBuildBitCast(g->builder, new_data, LLVMPointerType(i64, 0), "ndi");
                    LLVMBuildStore(g->builder, new_data_i, data_field);
                    LLVMBuildBr(g->builder, shift_bb);
                    /* shift elements right from count-1 down to idx */
                    LLVMPositionBuilderAtEnd(g->builder, shift_bb);
                    LLVMValueRef phi_data = LLVMBuildLoad2(g->builder, LLVMPointerType(i64, 0), data_field, "phid");
                    LLVMValueRef j_a = emit_entry_alloca(g, i64, "ij");
                    LLVMBuildStore(g->builder, count, j_a);
                    LLVMBasicBlockRef scond_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "ins.scond");
                    LLVMBasicBlockRef sbody_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "ins.sbody");
                    LLVMBasicBlockRef sdone_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "ins.sdone");
                    LLVMBuildBr(g->builder, scond_bb);
                    LLVMPositionBuilderAtEnd(g->builder, scond_bb);
                    LLVMValueRef j = LLVMBuildLoad2(g->builder, i64, j_a, "j");
                    LLVMValueRef cmp = zan_icmp(g->builder, LLVMIntUGT, j, idx, "cmp");
                    LLVMBuildCondBr(g->builder, cmp, sbody_bb, sdone_bb);
                    LLVMPositionBuilderAtEnd(g->builder, sbody_bb);
                    LLVMValueRef j2 = LLVMBuildLoad2(g->builder, i64, j_a, "j2");
                    LLVMValueRef prev = zan_sub(g->builder, j2, LLVMConstInt(i64, 1, 0), "prev");
                    LLVMValueRef prev_word = slot_word_index(g, prev, ins_words);
                    LLVMValueRef dst_word = slot_word_index(g, j2, ins_words);
                    for (unsigned wi = 0; wi < ins_words; wi++) {
                        LLVMValueRef off = LLVMConstInt(i64, wi, 0);
                        LLVMValueRef src_idx = zan_add(g->builder, prev_word, off, "ins.sw");
                        LLVMValueRef dst_idx = zan_add(g->builder, dst_word, off, "ins.dw");
                        LLVMValueRef src_slot = LLVMBuildGEP2(g->builder, i64, phi_data, &src_idx, 1, "ss");
                        LLVMValueRef sv = LLVMBuildLoad2(g->builder, i64, src_slot, "sv");
                        LLVMValueRef dst_slot = LLVMBuildGEP2(g->builder, i64, phi_data, &dst_idx, 1, "ds");
                        LLVMBuildStore(g->builder, sv, dst_slot);
                    }
                    LLVMBuildStore(g->builder, prev, j_a);
                    LLVMBuildBr(g->builder, scond_bb);
                    LLVMPositionBuilderAtEnd(g->builder, sdone_bb);
                    /* store item at index. overwrite_old must be 0: Insert
                     * shifts the previous occupant of this slot up to idx+1
                     * where it stays live, so it must not be released here
                     * (doing so frees a still-referenced element). The new
                     * item is still retained by emit_collection_slot_store. */
                    LLVMValueRef ins_word = slot_word_index(g, idx, ins_words);
                    LLVMValueRef ins_slot = LLVMBuildGEP2(g->builder, i64, phi_data, &ins_word, 1, "is");
                    emit_collection_slot_store(g, elem_type, i64, ins_slot,
                        item, expr->call.args.items[1], locals, 0);
                    LLVMValueRef nc = zan_add(g->builder, count, LLVMConstInt(i64, 1, 0), "nc");
                    LLVMBuildStore(g->builder, nc, count_ptr);
                    return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
                }
            }
        }

        /* List.Reverse() — in-place reverse */
        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS) {
            zan_ast_node_t *callee = expr->call.callee;
            zan_istr_t method_name = callee->member.name;
            if (method_name.len == 7 && memcmp(method_name.str, "Reverse", 7) == 0 &&
                expr->call.args.count == 0) {
                zan_ast_node_t *lobj = callee->member.object;
                zan_type_t *ltype = infer_expr_type(g, lobj, locals);
                if (ltype && type_named(ltype, "List", 4)) {
                    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                    LLVMValueRef raw_ptr = emit_expr(g, lobj, locals);
                    LLVMValueRef list_ptr = LLVMBuildBitCast(g->builder, raw_ptr,
                        LLVMPointerType(g->list_struct_type, 0), "lptr");
                    LLVMValueRef count_ptr = LLVMBuildStructGEP2(g->builder, g->list_struct_type, list_ptr, 0, "cntp");
                    LLVMValueRef count = LLVMBuildLoad2(g->builder, i64, count_ptr, "cnt");
                    LLVMValueRef data_field = LLVMBuildStructGEP2(g->builder, g->list_struct_type, list_ptr, 2, "df");
                    LLVMValueRef data = LLVMBuildLoad2(g->builder, LLVMPointerType(i64, 0), data_field, "data");
                    zan_type_t *elem_type = concretize(g,
                        container_elem_type(ltype));
                    unsigned rv_words = elem_slot_words(g, elem_type);
                    /* two-pointer swap: lo=0, hi=count-1 */
                    LLVMValueRef lo_a = emit_entry_alloca(g, i64, "lo");
                    LLVMValueRef hi_a = emit_entry_alloca(g, i64, "hi");
                    LLVMBuildStore(g->builder, LLVMConstInt(i64, 0, 0), lo_a);
                    LLVMValueRef count_zero = zan_icmp(g->builder, LLVMIntEQ,
                        count, LLVMConstInt(i64, 0, 0), "rv.empty");
                    LLVMValueRef hi_last = zan_sub(g->builder, count,
                        LLVMConstInt(i64, 1, 0), "hi.last");
                    LLVMValueRef hi_init = LLVMBuildSelect(g->builder, count_zero,
                        LLVMConstInt(i64, 0, 0), hi_last, "hi");
                    LLVMBuildStore(g->builder, hi_init, hi_a);
                    LLVMBasicBlockRef cond_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "rv.cond");
                    LLVMBasicBlockRef body_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "rv.body");
                    LLVMBasicBlockRef done_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "rv.done");
                    LLVMBuildBr(g->builder, cond_bb);
                    LLVMPositionBuilderAtEnd(g->builder, cond_bb);
                    LLVMValueRef lo = LLVMBuildLoad2(g->builder, i64, lo_a, "lo");
                    LLVMValueRef hi = LLVMBuildLoad2(g->builder, i64, hi_a, "hi");
                    LLVMValueRef cmp = zan_icmp(g->builder, LLVMIntULT, lo, hi, "cmp");
                    LLVMBuildCondBr(g->builder, cmp, body_bb, done_bb);
                    LLVMPositionBuilderAtEnd(g->builder, body_bb);
                    LLVMValueRef lo2 = LLVMBuildLoad2(g->builder, i64, lo_a, "lo2");
                    LLVMValueRef hi2 = LLVMBuildLoad2(g->builder, i64, hi_a, "hi2");
                    LLVMValueRef lo_word = slot_word_index(g, lo2, rv_words);
                    LLVMValueRef hi_word = slot_word_index(g, hi2, rv_words);
                    for (unsigned wi = 0; wi < rv_words; wi++) {
                        LLVMValueRef off = LLVMConstInt(i64, wi, 0);
                        LLVMValueRef li = zan_add(g->builder, lo_word, off, "rv.lw");
                        LLVMValueRef hi = zan_add(g->builder, hi_word, off, "rv.hw");
                        LLVMValueRef lo_slot = LLVMBuildGEP2(g->builder, i64, data, &li, 1, "ls");
                        LLVMValueRef hi_slot = LLVMBuildGEP2(g->builder, i64, data, &hi, 1, "hs");
                        LLVMValueRef lv = LLVMBuildLoad2(g->builder, i64, lo_slot, "lv");
                        LLVMValueRef hv = LLVMBuildLoad2(g->builder, i64, hi_slot, "hv");
                        LLVMBuildStore(g->builder, hv, lo_slot);
                        LLVMBuildStore(g->builder, lv, hi_slot);
                    }
                    LLVMBuildStore(g->builder, zan_add(g->builder, lo2, LLVMConstInt(i64, 1, 0), "lo3"), lo_a);
                    LLVMBuildStore(g->builder, zan_sub(g->builder, hi2, LLVMConstInt(i64, 1, 0), "hi3"), hi_a);
                    LLVMBuildBr(g->builder, cond_bb);
                    LLVMPositionBuilderAtEnd(g->builder, done_bb);
                    return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
                }
            }
        }


                /* Dict method calls: Add, ContainsKey, Clear. The receiver is
                 * resolved by its static type, not by name, so it works for
                 * locals AND fields (`dict.Add` / `this.dict.Add` /
                 * `obj.dict.Add`) alike. */
        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS) {
            zan_ast_node_t *callee_d = expr->call.callee;
            zan_istr_t mname = callee_d->member.name;
            zan_type_t *dict_type = infer_expr_type(g, callee_d->member.object, locals);
            if (dict_type && type_named(dict_type, "Dict", 4)) {
                    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                    LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
                    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                    LLVMValueRef raw = emit_expr(g, callee_d->member.object, locals);
                    LLVMValueRef dp = LLVMBuildBitCast(g->builder, raw,
                        LLVMPointerType(g->dict_struct_type, 0), "dp");

                    if (mname.len == 3 && memcmp(mname.str, "Add", 3) == 0 && expr->call.args.count == 2) {
                        zan_type_t *key_type = dict_key_type(g, dict_type);
                        LLVMValueRef key_val = coerce_dict_key(g,
                            emit_expr(g, expr->call.args.items[0], locals), key_type);
                        LLVMValueRef val_v = emit_expr(g, expr->call.args.items[1], locals);
                        emit_dict_value_set(g, dict_type, raw, key_val, val_v,
                            expr->call.args.items[0], expr->call.args.items[1], locals);
                        emit_release_owned_call_temp(g, callee_d->member.object, raw, locals);
                        return LLVMConstInt(i32t, 0, 0);
                    }

                    if (mname.len == 11 && memcmp(mname.str, "ContainsKey", 11) == 0 && expr->call.args.count == 1) {
                        LLVMValueRef search = coerce_dict_key(g, emit_expr(g, expr->call.args.items[0], locals), dict_key_type(g, dict_type));
                        LLVMValueRef found = emit_dict_find(g, dict_type, raw, search);
                        LLVMValueRef hit = zan_icmp(g->builder, LLVMIntSGE, found,
                            LLVMConstInt(i64, 0, 0), "ckhit");
                        emit_release_owned_call_temp(g, callee_d->member.object, raw, locals);
                        return LLVMBuildZExt(g->builder, hit, i32t, "ckres");
                    }
                    if (mname.len == 11 && memcmp(mname.str, "TryGetValue", 11) == 0 && expr->call.args.count == 2) {
                        /* bool TryGetValue(K key, out V value): probe for the
                         * key; on a hit store a RETAINED copy of the value into
                         * the out slot (the caller owns it) and return true,
                         * otherwise leave the slot untouched and return false. */
                        LLVMValueRef search = coerce_dict_key(g,
                            emit_expr(g, expr->call.args.items[0], locals),
                            dict_key_type(g, dict_type));
                        LLVMValueRef found = emit_dict_find(g, dict_type, raw, search);
                        LLVMValueRef hit = zan_icmp(g->builder, LLVMIntSGE, found,
                            LLVMConstInt(i64, 0, 0), "tgv.hit");
                        zan_ast_node_t *out_arg = expr->call.args.items[1];
                        LLVMValueRef out_ptr = NULL;
                        if (out_arg->kind == AST_REF_ARG) {
                            out_ptr = emit_expr(g, out_arg, locals);
                        } else if (out_arg->kind == AST_IDENTIFIER) {
                            local_var_t *ol = local_find(locals, out_arg->ident.name);
                            if (ol) { out_ptr = ol->alloca; }
                        }
                        zan_type_t *value_type = dict_value_type(dict_type);
                        LLVMTypeRef value_llvm = value_type ? map_type(g, value_type) : i64;
                        LLVMValueRef res_a = emit_entry_alloca(g, i32t, "tgv.res");
                        LLVMBasicBlockRef hit_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "tgv.hitbb");
                        LLVMBasicBlockRef miss_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "tgv.miss");
                        LLVMBasicBlockRef done_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "tgv.done");
                        LLVMBuildCondBr(g->builder, hit, hit_bb, miss_bb);
                        LLVMPositionBuilderAtEnd(g->builder, hit_bb);
                        LLVMValueRef vp = LLVMBuildStructGEP2(g->builder, g->dict_struct_type, dp, 3, "vp");
                        LLVMValueRef vs = LLVMBuildLoad2(g->builder, LLVMPointerType(i64, 0), vp, "vs");
                        LLVMValueRef word = zan_mul(g->builder, found,
                            load_dict_value_words(g, raw), "tgv.word");
                        LLVMValueRef vslot = LLVMBuildGEP2(g->builder, i64, vs,
                            &word, 1, "tgv.vsl");
                        LLVMValueRef vv = load_collection_slot_value(g, value_type, vslot);
                        if (out_ptr) {
                            emit_typed_out_store(g, value_type, out_ptr, vv);
                        }
                        LLVMBuildStore(g->builder, LLVMConstInt(i32t, 1, 0), res_a);
                        LLVMBuildBr(g->builder, done_bb);
                        LLVMPositionBuilderAtEnd(g->builder, miss_bb);
                        LLVMBuildStore(g->builder, LLVMConstInt(i32t, 0, 0), res_a);
                        LLVMBuildBr(g->builder, done_bb);
                        LLVMPositionBuilderAtEnd(g->builder, done_bb);
                        emit_release_owned_call_temp(g, callee_d->member.object, raw, locals);
                        return LLVMBuildLoad2(g->builder, i32t, res_a, "tgv.out");
                    }
                    if (mname.len == 5 && memcmp(mname.str, "Clear", 5) == 0 && expr->call.args.count == 0) {
                        LLVMValueRef cntp = LLVMBuildStructGEP2(g->builder, g->dict_struct_type, dp, 0, "cntp");
                        LLVMValueRef cnt = LLVMBuildLoad2(g->builder, i64, cntp, "cnt");
                        LLVMValueRef kp = LLVMBuildStructGEP2(g->builder, g->dict_struct_type, dp, 2, "kp");
                        LLVMValueRef ks = LLVMBuildLoad2(g->builder, LLVMPointerType(i8ptr, 0), kp, "ks");
                        LLVMValueRef vp = LLVMBuildStructGEP2(g->builder, g->dict_struct_type, dp, 3, "vp");
                        LLVMValueRef vs = LLVMBuildLoad2(g->builder, LLVMPointerType(i64, 0), vp, "vs");
                        LLVMValueRef idx_a = emit_entry_alloca(g, i64, "dc");
                        LLVMBuildStore(g->builder, LLVMConstInt(i64, 0, 0), idx_a);
                        LLVMBasicBlockRef cbb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "dc.cond");
                        LLVMBasicBlockRef bbb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "dc.body");
                        LLVMBasicBlockRef dbb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "dc.done");
                        LLVMBuildBr(g->builder, cbb);
                        LLVMPositionBuilderAtEnd(g->builder, cbb);
                        LLVMValueRef ci = LLVMBuildLoad2(g->builder, i64, idx_a, "ci");
                        LLVMValueRef more = zan_icmp(g->builder, LLVMIntULT, ci, cnt, "more");
                        LLVMBuildCondBr(g->builder, more, bbb, dbb);
                        LLVMPositionBuilderAtEnd(g->builder, bbb);
                        LLVMValueRef ci2 = LLVMBuildLoad2(g->builder, i64, idx_a, "ci2");
                        LLVMValueRef kslot = LLVMBuildGEP2(g->builder, i8ptr, ks, &ci2, 1, "ksl");
                        LLVMValueRef kv = LLVMBuildLoad2(g->builder, i8ptr, kslot, "kv");
                        emit_collection_release_raw_slot(g, dict_key_type(g, dict_type), kv, i8ptr);
                        zan_type_t *value_type = dict_value_type(dict_type);
                        LLVMValueRef value_words = load_dict_value_words(g, raw);
                        LLVMValueRef value_word = zan_mul(g->builder, ci2,
                            value_words, "dc.word");
                        LLVMValueRef vslot = LLVMBuildGEP2(g->builder, i64, vs,
                            &value_word, 1, "vsl");
                        LLVMValueRef vv = load_collection_slot_value(g, value_type, vslot);
                        emit_collection_release_raw_slot(g, value_type, vv, i64);
                        LLVMValueRef ni = zan_add(g->builder, ci2, LLVMConstInt(i64, 1, 0), "ni");
                        LLVMBuildStore(g->builder, ni, idx_a);
                        LLVMBuildBr(g->builder, cbb);
                        LLVMPositionBuilderAtEnd(g->builder, dbb);
                        LLVMBuildStore(g->builder, LLVMConstInt(i64, 0, 0), cntp);
                        LLVMBuildStore(g->builder, LLVMConstInt(i64, 0, 0),
                            LLVMBuildStructGEP2(g->builder, g->dict_struct_type,
                                dp, 5, "icapp"));
                        emit_release_owned_call_temp(g, callee_d->member.object, raw, locals);
                        return LLVMConstInt(i32t, 0, 0);
                    }
                }
        }

        /* Dict.Clear() — reset count to 0 */
        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS) {
            zan_ast_node_t *callee_d = expr->call.callee;
            zan_istr_t mname = callee_d->member.name;
            if (mname.len == 5 && memcmp(mname.str, "Clear", 5) == 0 && expr->call.args.count == 0) {
                if (callee_d->member.object->kind == AST_IDENTIFIER) {
                    local_var_t *dict_local = local_find(locals, callee_d->member.object->ident.name);
                    if (dict_local && dict_local->type && type_named(dict_local->type, "Dict", 4)) {
                        LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                        LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                        LLVMValueRef raw = LLVMBuildLoad2(g->builder, i8ptr, dict_local->alloca, "draw");
                        LLVMValueRef dp = LLVMBuildBitCast(g->builder, raw,
                            LLVMPointerType(g->dict_struct_type, 0), "dp");
                        LLVMValueRef cntp = LLVMBuildStructGEP2(g->builder, g->dict_struct_type, dp, 0, "cntp");
                        LLVMBuildStore(g->builder, LLVMConstInt(i64, 0, 0), cntp);
                        return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
                    }
                }
            }
        }

        /* Dict.Remove(key) — find and remove key, shift remaining entries.
         * Receiver resolved by static type (locals and fields alike). */
        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS) {
            zan_ast_node_t *callee_d = expr->call.callee;
            zan_istr_t mname = callee_d->member.name;
            zan_type_t *dict_type = infer_expr_type(g, callee_d->member.object, locals);
            if (mname.len == 6 && memcmp(mname.str, "Remove", 6) == 0 && expr->call.args.count == 1 &&
                dict_type && type_named(dict_type, "Dict", 4)) {
                        LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                        LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
                        LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                        LLVMValueRef raw = emit_expr(g, callee_d->member.object, locals);
                        LLVMValueRef dp = LLVMBuildBitCast(g->builder, raw,
                            LLVMPointerType(g->dict_struct_type, 0), "dp");
                        LLVMValueRef cntp = LLVMBuildStructGEP2(g->builder, g->dict_struct_type, dp, 0, "cntp");
                        LLVMValueRef cnt = LLVMBuildLoad2(g->builder, i64, cntp, "cnt");
                        LLVMValueRef kp = LLVMBuildStructGEP2(g->builder, g->dict_struct_type, dp, 2, "kp");
                        LLVMValueRef ks = LLVMBuildLoad2(g->builder, LLVMPointerType(i8ptr, 0), kp, "ks");
                        LLVMValueRef vp = LLVMBuildStructGEP2(g->builder, g->dict_struct_type, dp, 3, "vp");
                        LLVMValueRef vs = LLVMBuildLoad2(g->builder, LLVMPointerType(i64, 0), vp, "vs");
                        LLVMValueRef search = coerce_dict_key(g, emit_expr(g, expr->call.args.items[0], locals), dict_key_type(g, dict_type));
                        /* linear search for key */
                        LLVMValueRef idx_a = emit_entry_alloca(g, i64, "di");
                        LLVMBuildStore(g->builder, LLVMConstInt(i64, 0, 0), idx_a);
                        LLVMBasicBlockRef cond_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "dr.cond");
                        LLVMBasicBlockRef body_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "dr.body");
                        LLVMBasicBlockRef found_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "dr.found");
                        LLVMBasicBlockRef next_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "dr.next");
                        LLVMBasicBlockRef done_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "dr.done");
                        LLVMBuildBr(g->builder, cond_bb);
                        LLVMPositionBuilderAtEnd(g->builder, cond_bb);
                        LLVMValueRef ci = LLVMBuildLoad2(g->builder, i64, idx_a, "ci");
                        LLVMBuildCondBr(g->builder, zan_icmp(g->builder, LLVMIntUGE, ci, cnt, "cdone"), done_bb, body_bb);
                        LLVMPositionBuilderAtEnd(g->builder, body_bb);
                        LLVMValueRef ci2 = LLVMBuildLoad2(g->builder, i64, idx_a, "ci2");
                        LLVMValueRef kslot = LLVMBuildGEP2(g->builder, i8ptr, ks, &ci2, 1, "ksl");
                        LLVMValueRef kv = LLVMBuildLoad2(g->builder, i8ptr, kslot, "kv");
                        LLVMValueRef eq = emit_dict_key_eq(g, dict_type, kv, search);
                        LLVMBuildCondBr(g->builder, eq, found_bb, next_bb);
                        /* found: shift remaining entries left */
                        LLVMPositionBuilderAtEnd(g->builder, found_bb);
                        /* the hash index maps keys to entry slots, and both the
                         * slots and the count change here: drop it. */
                        LLVMBuildStore(g->builder, LLVMConstInt(i64, 0, 0),
                            LLVMBuildStructGEP2(g->builder, g->dict_struct_type, dp, 5, "icapp"));
                        LLVMValueRef fi = LLVMBuildLoad2(g->builder, i64, idx_a, "fi");
                        LLVMValueRef last = zan_sub(g->builder, cnt, LLVMConstInt(i64, 1, 0), "last");
                        LLVMValueRef rkey = LLVMBuildGEP2(g->builder, i8ptr, ks, &fi, 1, "rkey");
                        LLVMValueRef rkv = LLVMBuildLoad2(g->builder, i8ptr, rkey, "rkv");
                        emit_collection_release_raw_slot(g, dict_key_type(g, dict_type), rkv, i8ptr);
                        zan_type_t *value_type = dict_value_type(dict_type);
                        LLVMValueRef value_words = load_dict_value_words(g, raw);
                        LLVMValueRef removed_word = zan_mul(g->builder, fi,
                            value_words, "dr.word");
                        LLVMValueRef rvslot = LLVMBuildGEP2(g->builder, i64, vs,
                            &removed_word, 1, "rvslot");
                        LLVMValueRef rv = load_collection_slot_value(g, value_type, rvslot);
                        emit_collection_release_raw_slot(g, value_type, rv, i64);
                        LLVMValueRef move_entries = zan_sub(g->builder, last, fi, "move.n");
                        LLVMValueRef move_words = zan_mul(g->builder, move_entries,
                            value_words, "move.words");
                        LLVMValueRef move_bytes = zan_mul(g->builder, move_words,
                            LLVMConstInt(i64, 8, 0), "move.bytes");
                        LLVMValueRef source_word = zan_add(g->builder, removed_word,
                            value_words, "move.srcw");
                        LLVMValueRef value_source = LLVMBuildGEP2(g->builder, i64, vs,
                            &source_word, 1, "move.src");
                        LLVMTypeRef move_type = LLVMFunctionType(i8ptr,
                            (LLVMTypeRef[]){ i8ptr, i8ptr, i64 }, 3, 0);
                        LLVMValueRef move_fn = get_libc_fn(g, "memmove", move_type);
                        LLVMValueRef move_dst = LLVMBuildBitCast(g->builder, rvslot,
                            i8ptr, "move.dst8");
                        LLVMValueRef move_src = LLVMBuildBitCast(g->builder, value_source,
                            i8ptr, "move.src8");
                        zan_call2(g->builder, move_type, move_fn,
                            (LLVMValueRef[]){ move_dst, move_src, move_bytes }, 3, "");
                        LLVMValueRef j_a = emit_entry_alloca(g, i64, "fj");
                        LLVMBuildStore(g->builder, fi, j_a);
                        LLVMBasicBlockRef sc_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "dr.sc");
                        LLVMBasicBlockRef sb_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "dr.sb");
                        LLVMBasicBlockRef sd_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "dr.sd");
                        LLVMBuildBr(g->builder, sc_bb);
                        LLVMPositionBuilderAtEnd(g->builder, sc_bb);
                        LLVMValueRef j = LLVMBuildLoad2(g->builder, i64, j_a, "j");
                        LLVMBuildCondBr(g->builder, zan_icmp(g->builder, LLVMIntULT, j, last, "jlt"), sb_bb, sd_bb);
                        LLVMPositionBuilderAtEnd(g->builder, sb_bb);
                        LLVMValueRef j2 = LLVMBuildLoad2(g->builder, i64, j_a, "j2");
                        LLVMValueRef nxt = zan_add(g->builder, j2, LLVMConstInt(i64, 1, 0), "nxt");
                        /* shift key */
                        LLVMValueRef ksrc = LLVMBuildGEP2(g->builder, i8ptr, ks, &nxt, 1, "ksrc");
                        LLVMValueRef kdst = LLVMBuildGEP2(g->builder, i8ptr, ks, &j2, 1, "kdst");
                        LLVMBuildStore(g->builder, LLVMBuildLoad2(g->builder, i8ptr, ksrc, "ksv"), kdst);
                        LLVMBuildStore(g->builder, nxt, j_a);
                        LLVMBuildBr(g->builder, sc_bb);
                        LLVMPositionBuilderAtEnd(g->builder, sd_bb);
                        LLVMBuildStore(g->builder, last, cntp);
                        LLVMValueRef ktail = LLVMBuildGEP2(g->builder, i8ptr, ks, &last, 1, "ktail");
                        LLVMBuildStore(g->builder, LLVMConstNull(i8ptr), ktail);
                        LLVMBuildBr(g->builder, done_bb);
                        /* next: increment and loop */
                        LLVMPositionBuilderAtEnd(g->builder, next_bb);
                        LLVMValueRef ni = zan_add(g->builder, ci2, LLVMConstInt(i64, 1, 0), "ni");
                        LLVMBuildStore(g->builder, ni, idx_a);
                        LLVMBuildBr(g->builder, cond_bb);
                        LLVMPositionBuilderAtEnd(g->builder, done_bb);
                        emit_release_owned_call_temp(g, callee_d->member.object, raw, locals);
                        return LLVMConstInt(i32t, 0, 0);
            }
        }


        /* Delegate invocation. Unlike methods, a delegate callee can be a
         * local, an implicit/explicit instance field, a static field or a
         * nested field expression. Resolve the callee's value type first and
         * emit one indirect-call path for all of those forms. */
        {
            zan_type_t *delegate_type =
                infer_expr_type(g, expr->call.callee, locals);
            if (delegate_type &&
                delegate_type->kind == TYPE_DELEGATE) {
                LLVMValueRef fn_ptr =
                    emit_expr(g, expr->call.callee, locals);
                return emit_delegate_call(
                    g, delegate_type, fn_ptr, expr, locals);
            }
        }

        /* op_call operator: `obj(args)` on a class/struct instance lowers to
         * a call of the static `op_call(self, ...)` method, exactly as binary
         * operators lower to static op_add/op_sub/etc. The receiver may be
         * any expression of the class type (a local, field, index or a nested
         * call result), e.g. `py.GetAttr("math").GetAttr("floor")(2.5)`. */
        {
            zan_type_t *oc_ty = infer_expr_type(g, expr->call.callee, locals);
            if (oc_ty && (oc_ty->kind == TYPE_CLASS || oc_ty->kind == TYPE_STRUCT) &&
                oc_ty->sym) {
                zan_istr_t op_istr = {(char *)"op_call", 7};
                zan_symbol_t *op_sym = resolve_op_overload(g, oc_ty->sym,
                                                           op_istr, expr, locals);
                if (op_sym) {
                    pack_params_args(g, expr, op_sym, locals);
                    for (int fi = irgen_find_function(g, op_sym); fi >= 0; fi = -1) {
                        if (g->functions[fi].sym == op_sym) {
                            int argc = expr->call.args.count + 1;
                            LLVMValueRef *call_args = (LLVMValueRef *)calloc(
                                (size_t)argc, sizeof(LLVMValueRef));
                            LLVMValueRef recv_val = emit_expr(g, expr->call.callee, locals);
                            call_args[0] = recv_val;
                            /* a struct receiver that arrives by value (a call
                             * result, a field read, an element) has no address,
                             * but `self` is passed by pointer: spill it. */
                            if (LLVMGetTypeKind(LLVMTypeOf(recv_val)) == LLVMStructTypeKind) {
                                LLVMValueRef rslot = emit_entry_alloca(g,
                                    LLVMTypeOf(recv_val), "opc.recv");
                                LLVMBuildStore(g->builder, recv_val, rslot);
                                call_args[0] = rslot;
                            }
                            /* an owned receiver temp must survive a throwing
                             * callee: keep it on the EH temp stack so a catch
                             * can release it (longjmp skips the release below) */
                            int recv_eh_pushed = 0;
                            if (oc_ty->kind == TYPE_CLASS &&
                                !expr_is_local_ident(expr->call.callee, locals) &&
                                expr_yields_owned_rc_value(g, expr->call.callee, locals) &&
                                LLVMGetTypeKind(LLVMTypeOf(recv_val)) == LLVMPointerTypeKind) {
                                emit_eh_tmp_push(g, recv_val);
                                recv_eh_pushed = 1;
                            }
                            for (int k = 0; k < expr->call.args.count; k++) {
                                /* declared parameter k+self_off: slot 0 is the
                                 * injected receiver (static op_call's explicit
                                 * `self`, or `this` for an instance one) */
                                int self_off =
                                    (op_sym->modifiers & MOD_STATIC) ? 1 : 0;
                                call_args[k + 1] = emit_arg_typed(g,
                                    expr->call.args.items[k],
                                    method_param_type_at(g, op_sym,
                                        k + self_off, expr,
                                        expr->call.callee, locals), locals);
                            }
                            LLVMTypeRef mft = g->functions[fi].fn_type;
                            LLVMValueRef mfn = route_generic_method(g, oc_ty,
                                op_sym, g->functions[fi].fn, mft, &mft);
                            const char *cn = (LLVMGetTypeKind(LLVMGetReturnType(mft)) == LLVMVoidTypeKind) ? "" : "opcall";
                            LLVMValueRef result = emit_dispatch_call(g,
                                oc_ty->sym, op_sym, mfn, mft, call_args, argc, cn);
                            result = coerce_generic_result(g, result, op_sym, oc_ty);
                            if (recv_eh_pushed) emit_eh_tmp_pop(g);
                            emit_release_owned_call_temp(g, expr->call.callee,
                                recv_val, locals);
                            for (int k = 0; k < expr->call.args.count; k++) {
                                emit_release_owned_call_temp(g, expr->call.args.items[k],
                                    call_args[k + 1], locals);
                            }
                            free(call_args);
                            return result;
                        }
                    }
                }
            }
        }

                /* user-defined method call: obj.Method(args) or Type.StaticMethod(args) */
        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS) {
            zan_ast_node_t *callee = expr->call.callee;
            if (callee->member.object->kind == AST_IDENTIFIER) {
                /* first try as instance method: local_var.Method(args) */
                local_var_t *local = local_find(locals, callee->member.object->ident.name);
                if (local && local->type && local->type->sym) {
                    zan_symbol_t *type_sym = local->type->sym;
                    zan_symbol_t *method_sym = resolve_overload_typed(g, type_sym, callee->member.name, expr, locals);
                    if (method_sym) fill_default_args(g, expr, method_sym);
                    if (method_sym) pack_params_args(g, expr, method_sym, locals);
                    if (method_sym) {
                        /* a generic instance method monomorphizes with the
                         * receiver handed over as the implicit `this` */
                        int spec = try_method_spec(g, method_sym, expr,
                                                   callee->member.object, locals);
                        if (spec >= 0)
                            return emit_method_spec_call(g, spec, expr,
                                                         callee->member.object, locals);
                        for (int fi = irgen_find_function(g, method_sym); fi >= 0; fi = -1) {
                            if (g->functions[fi].sym == method_sym) {
                                int argc = expr->call.args.count + 1;
                                LLVMValueRef *call_args = (LLVMValueRef *)calloc((size_t)argc, sizeof(LLVMValueRef));
                                /* receiver: class refs hold the object pointer in
                                 * the local, so load it; struct value types pass
                                 * the storage address directly. */
                                LLVMTypeRef at = local_slot_type(g, local);
                                if (LLVMGetTypeKind(at) == LLVMPointerTypeKind) {
                                    call_args[0] = LLVMBuildLoad2(g->builder, at, local->alloca, "recv");
                                } else {
                                    call_args[0] = local->alloca;
                                }
                                for (int k = 0; k < expr->call.args.count; k++) {
                                    call_args[k + 1] = emit_arg_typed(g, expr->call.args.items[k],
                                        method_param_type_at(g, method_sym, k, expr,
                                                             callee->member.object, locals), locals);
                                }
                                LLVMTypeRef mft = g->functions[fi].fn_type;
                                LLVMValueRef mfn = route_generic_method(g, local->type,
                                    method_sym, g->functions[fi].fn, mft, &mft);
                                const char *cn = (LLVMGetTypeKind(LLVMGetReturnType(mft)) == LLVMVoidTypeKind) ? "" : "mcall";
                                LLVMValueRef result = emit_dispatch_call(g, type_sym, method_sym,
                                    mfn, mft, call_args, argc, cn);
                                result = coerce_generic_result(g, result, method_sym, local->type);
                                for (int k = 0; k < expr->call.args.count; k++) {
                                    emit_release_owned_call_temp(g, expr->call.args.items[k],
                                        call_args[k + 1], locals);
                                }
                                free(call_args);
                                return result;
                            }
                        }
                    }
                }

                /* try as static method: ClassName.Method(args).
                 * A field of the enclosing class shadows a type of the same
                 * name, exactly as a local does: in a class holding a field
                 * `Collapse`, `Collapse.Add(h)` calls that field's method and
                 * not a static of the widget class named Collapse. Without
                 * this the receiver silently became the type and the call was
                 * emitted against the wrong (instance) signature. */
                zan_symbol_t *type_sym = NULL;
                if (!ident_names_own_field(g, callee->member.object))
                    type_sym = zan_binder_lookup(g->binder,
                        callee->member.object->ident.name);
                if (type_sym && (type_sym->kind == SYM_CLASS || type_sym->kind == SYM_STRUCT)) {
                    zan_symbol_t *method_sym = resolve_overload_typed(g, type_sym, callee->member.name, expr, locals);
                    if (method_sym) fill_default_args(g, expr, method_sym);
                    if (method_sym) pack_params_args(g, expr, method_sym, locals);
                    if (method_sym) {
                        int spec = try_method_spec(g, method_sym, expr, NULL, locals);
                        if (spec >= 0)
                            return emit_method_spec_call(g, spec, expr, NULL, locals);
                        for (int fi = irgen_find_function(g, method_sym); fi >= 0; fi = -1) {
                            if (g->functions[fi].sym == method_sym) {
                                int argc = expr->call.args.count;
                                LLVMValueRef *call_args = (LLVMValueRef *)calloc((size_t)(argc > 0 ? argc : 1), sizeof(LLVMValueRef));
                                int *arg_eh_pushed = (int *)calloc(
                                    (size_t)(argc > 0 ? argc : 1), sizeof(int));
                                for (int k = 0; k < argc; k++) {
                                    zan_ast_node_t *arg = expr->call.args.items[k];
                                    call_args[k] = emit_arg_typed(g, arg,
                                        method_param_type_at(g, method_sym, k, expr, NULL, locals), locals);
                                    zan_type_t *at = infer_expr_type(g, arg, locals);
                                    if (at && is_rc_managed_type(at) &&
                                        !expr_is_local_ident(arg, locals) &&
                                        expr_yields_owned_rc_value(g, arg, locals) &&
                                        LLVMGetTypeKind(LLVMTypeOf(call_args[k])) == LLVMPointerTypeKind) {
                                        int ehk = eh_slot_kind_of(at);
                                        if (ehk != ZAN_EH_SLOT_OBJ) {
                                            LLVMValueRef slot = emit_entry_alloca(g,
                                                LLVMTypeOf(call_args[k]), "arg.eh");
                                            LLVMBuildStore(g->builder, call_args[k], slot);
                                            emit_eh_tmp_push_slot(g, slot, ehk);
                                        } else {
                                            emit_eh_tmp_push(g, call_args[k]);
                                        }
                                        arg_eh_pushed[k] = 1;
                                    }
                                }
                                /* `Box<int>.Create(x)` names the instantiation,
                                 * so call its specialization rather than the
                                 * erased body. */
                                LLVMTypeRef sft = g->functions[fi].fn_type;
                                LLVMValueRef sfn = route_generic_method(g,
                                    ident_inst_type(g, callee->member.object),
                                    method_sym, g->functions[fi].fn, sft, &sft);
                                const char *cn = (LLVMGetTypeKind(LLVMGetReturnType(sft)) == LLVMVoidTypeKind) ? "" : "scall";
                                coerce_args_to_params(g, sft, call_args, argc);
                                LLVMValueRef result = zan_call2(g->builder, sft,
                                    sfn, call_args, (unsigned)argc, cn);
                                zan_type_t *gret = generic_method_ret(g, method_sym, expr, locals);
                                if (!gret && method_ret_is_bare_tp(method_sym)) {
                                    zan_type_t *ir = method_ret_type_at(g, method_sym,
                                        expr, NULL, locals);
                                    if (ir && ir->kind != TYPE_TYPE_PARAM) gret = ir;
                                }
                                if (gret) {
                                    result = emit_boundary_coerce(g, result, map_type(g, gret));
                                    /* the erased body returned a borrowed +0
                                     * class reference; make it owned like
                                     * every other call result */
                                    if (gret->kind == TYPE_CLASS &&
                                        method_ret_is_bare_tp(method_sym))
                                        emit_arc_retain(g, result);
                                }
                                int consumes_free_arg =
                                    argc == 1 && call_consumes_free_arg(g->functions[fi].fn);
                                if (consumes_free_arg) {
                                    emit_invalidate_freed_string(g,
                                        expr->call.args.items[0], locals);
                                }
                                emit_extern_call_len_invalidate(g,
                                    g->functions[fi].fn, call_args, argc,
                                    expr, locals);
                                for (int k = argc - 1; k >= 0; k--) {
                                    if (arg_eh_pushed[k]) emit_eh_tmp_pop(g);
                                }
                                for (int k = 0; k < argc; k++) {
                                    if (!consumes_free_arg || k != 0) {
                                        emit_release_owned_call_temp(g, expr->call.args.items[k],
                                            call_args[k], locals);
                                    }
                                }
                                free(arg_eh_pushed);
                                free(call_args);
                                return result;
                            }
                        }
                    }
                }
            }
        }

        /* general instance method call: <expr>.Method(args) where <expr> is
         * any expression of class/struct type (a field, this.field, an index
         * or a call result) — not just a local variable or class name. */
        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS) {
            zan_ast_node_t *callee = expr->call.callee;
            zan_symbol_t *recv_cls = expr_class_sym(g, callee->member.object, locals);
            if (recv_cls) {
                zan_symbol_t *method_sym = resolve_overload_typed(g, recv_cls, callee->member.name, expr, locals);
                    if (method_sym) fill_default_args(g, expr, method_sym);
                    if (method_sym) pack_params_args(g, expr, method_sym, locals);
                if (method_sym) {
                    /* `expr.StaticMethod(args)` is legal (the generic path's
                     * recv_params handling already allows it): a static method
                     * has no receiver parameter, so the receiver expression is
                     * not passed — `this.F()` inside the class and `T.F()`
                     * both land here as plain static calls. */
                    bool callee_static = (method_sym->modifiers & MOD_STATIC) != 0;
                    int spec = try_method_spec(g, method_sym, expr,
                                               callee->member.object, locals);
                    if (spec >= 0)
                        return emit_method_spec_call(g, spec, expr,
                                                     callee->member.object, locals);
                    for (int fi = irgen_find_function(g, method_sym); fi >= 0; fi = -1) {
                        if (g->functions[fi].sym == method_sym) {
                            int recv_off = callee_static ? 0 : 1;
                            int argc = expr->call.args.count + recv_off;
                            LLVMValueRef *call_args = (LLVMValueRef *)calloc(
                                (size_t)(argc > 0 ? argc : 1), sizeof(LLVMValueRef));
                            LLVMValueRef recv_val = NULL;
                            int recv_eh_pushed = 0;
                            if (!callee_static) {
                                recv_val = emit_guarded_member_object(
                                    g, callee, locals);
                                /* receiver: the object pointer produced by the
                                 * expression (field load, index, call, ...). */
                                call_args[0] = recv_val;
                                /* A struct receiver that arrives by value (a call
                                 * result, a field read, an element) has no address,
                                 * but `this` is passed by pointer: spill it. */
                                if (LLVMGetTypeKind(LLVMTypeOf(recv_val)) == LLVMStructTypeKind) {
                                    LLVMValueRef rslot = emit_entry_alloca(g, LLVMTypeOf(recv_val), "recv.tmp");
                                    LLVMBuildStore(g->builder, recv_val, rslot);
                                    call_args[0] = rslot;
                                }
                                /* an owned receiver temp must survive a throwing
                                 * callee: keep it on the EH temp stack so a catch
                                 * can release it (longjmp skips the release below) */
                                {
                                    zan_type_t *rty = infer_expr_type(g, callee->member.object, locals);
                                    if (rty && rty->kind == TYPE_CLASS &&
                                        !expr_is_local_ident(callee->member.object, locals) &&
                                        expr_yields_owned_rc_value(g, callee->member.object, locals) &&
                                        LLVMGetTypeKind(LLVMTypeOf(recv_val)) == LLVMPointerTypeKind) {
                                        emit_eh_tmp_push(g, recv_val);
                                        recv_eh_pushed = 1;
                                    }
                                }
                            }
                            for (int k = 0; k < expr->call.args.count; k++) {
                                call_args[k + recv_off] = emit_arg_typed(g, expr->call.args.items[k],
                                    method_param_type_at(g, method_sym, k, expr,
                                                         callee->member.object, locals),
                                    locals);
                            }
                            zan_type_t *recv_ty = callee_static ? g->cur_inst
                                : infer_expr_type(g, callee->member.object, locals);
                            LLVMTypeRef mft = g->functions[fi].fn_type;
                            LLVMValueRef mfn = route_generic_method(g, recv_ty,
                                method_sym, g->functions[fi].fn, mft, &mft);
                            const char *cn = (LLVMGetTypeKind(LLVMGetReturnType(mft)) == LLVMVoidTypeKind) ? "" : "mcall";
                            /* `base.F()` binds to the nearest base implementation
                             * statically: vtable dispatch would re-enter this
                             * very override (slot still points at B_F) and
                             * recurse to a stack overflow. A NULL declared type
                             * makes emit_dispatch_call take the direct static
                             * branch instead. */
                            bool base_recv =
                                callee->member.object->kind == AST_BASE_EXPR;
                            LLVMValueRef result = emit_dispatch_call(g,
                                (callee_static || base_recv) ? NULL : recv_cls,
                                method_sym, mfn, mft, call_args, argc, cn);
                            result = coerce_generic_result(g, result, method_sym, recv_ty);
                            if (recv_eh_pushed) emit_eh_tmp_pop(g);
                            if (!callee_static) {
                                emit_release_owned_call_temp(g, callee->member.object, recv_val, locals);
                            }
                            for (int k = 0; k < expr->call.args.count; k++) {
                                emit_release_owned_call_temp(g, expr->call.args.items[k],
                                    call_args[k + recv_off], locals);
                            }
                            free(call_args);
                            return result;
                        }
                    }
                }
            }
        }

        /* interface method dispatch: <expr>.Method(args) where <expr>'s static
         * type is an interface. The concrete class is unknown at compile time,
         * so compare the object's field-0 vtable pointer (a per-class tag)
         * against every class implementing the interface and call the matching
         * implementation. */
        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS) {
            zan_ast_node_t *callee = expr->call.callee;
            zan_type_t *obj_ty = infer_expr_type(g, callee->member.object, locals);
            if (obj_ty && obj_ty->kind == TYPE_INTERFACE && obj_ty->sym && g->current_fn) {
                zan_symbol_t *iface = obj_ty->sym;
                zan_symbol_t *iface_m = resolve_iface_overload(iface,
                                            callee->member.name,
                                            expr->call.args.count);
                if (iface_m) {
                    LLVMContextRef c = g->ctx;
                    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(c), 0);
                    zan_type_t *rty = NULL;
                    if (iface_m->decl && iface_m->decl->kind == AST_METHOD_DECL &&
                        iface_m->decl->method_decl.return_type)
                        rty = zan_binder_resolve_type(g->binder,
                                  iface_m->decl->method_decl.return_type);
                    /* An async method's ramp hands back a coroutine frame, not
                     * the declared return value, so dispatching one has to keep
                     * the handle as a pointer for `await` to drive -- coercing
                     * it to the declared type would turn the frame into an
                     * integer and the await would yield that raw address. */
                    bool m_async = iface_m->decl &&
                        iface_m->decl->kind == AST_METHOD_DECL &&
                        (iface_m->decl->method_decl.modifiers & MOD_ASYNC) != 0;
                    bool has_res = m_async || (rty && rty->kind != TYPE_VOID);
                    LLVMTypeRef res_ty = m_async ? i8ptr
                                                 : (has_res ? map_type(g, rty) : NULL);

                    int uargc = expr->call.args.count;
                    /* emit receiver + argument values once, before branching */
                    LLVMValueRef recv = emit_guarded_member_object(g, callee, locals);
                    LLVMValueRef *avals = (LLVMValueRef *)calloc((size_t)(uargc > 0 ? uargc : 1),
                                                                sizeof(LLVMValueRef));
                    for (int k = 0; k < uargc; k++)
                        avals[k] = emit_arg_typed(g, expr->call.args.items[k],
                                                  method_param_type_at(g, iface_m, k, expr,
                                                      callee->member.object, locals),
                                                  locals);
                    LLVMValueRef recv_pp = LLVMBuildBitCast(g->builder, recv,
                                              LLVMPointerType(i8ptr, 0), "ifc.recvpp");
                    LLVMValueRef tag = LLVMBuildLoad2(g->builder, i8ptr, recv_pp, "ifc.tag");

                    LLVMBasicBlockRef merge = LLVMAppendBasicBlockInContext(c, g->current_fn, "ifc.merge");
                    int cap = g->struct_type_count + 1;
                    LLVMValueRef *phi_vals = (LLVMValueRef *)calloc((size_t)cap, sizeof(LLVMValueRef));
                    LLVMBasicBlockRef *phi_bbs = (LLVMBasicBlockRef *)calloc((size_t)cap, sizeof(LLVMBasicBlockRef));
                    int np = 0;

                    for (int si = 0; si < g->struct_type_count; si++) {
                        zan_symbol_t *cls = g->struct_types[si].sym;
                        if (!cls || !class_implements_iface(cls, iface)) continue;
                        zan_symbol_t *impl_m = resolve_overload(cls, callee->member.name, uargc);
                        if (!impl_m) continue;
                        LLVMValueRef ifn = NULL; LLVMTypeRef ifnty = NULL;
                        for (int fi = irgen_find_function(g, impl_m); fi >= 0; fi = -1)
                            if (g->functions[fi].sym == impl_m) {
                                ifn = g->functions[fi].fn; ifnty = g->functions[fi].fn_type; break;
                            }
                        if (!ifn || !ifnty) continue;

                        LLVMBasicBlockRef callbb = LLVMAppendBasicBlockInContext(c, g->current_fn, "ifc.call");
                        LLVMBasicBlockRef nextbb = LLVMAppendBasicBlockInContext(c, g->current_fn, "ifc.next");
                        LLVMValueRef tagc = LLVMConstBitCast(get_vtable_global(g, cls), i8ptr);
                        LLVMValueRef cmp = zan_icmp(g->builder, LLVMIntEQ, tag, tagc, "ifc.eq");
                        LLVMBuildCondBr(g->builder, cmp, callbb, nextbb);

                        LLVMPositionBuilderAtEnd(g->builder, callbb);
                        unsigned npar = LLVMCountParamTypes(ifnty);
                        LLVMTypeRef *pts = (LLVMTypeRef *)calloc((size_t)(npar > 0 ? npar : 1), sizeof(LLVMTypeRef));
                        LLVMGetParamTypes(ifnty, pts);
                        int cargc = uargc + 1;
                        LLVMValueRef *ca = (LLVMValueRef *)calloc((size_t)cargc, sizeof(LLVMValueRef));
                        ca[0] = (npar > 0) ? LLVMBuildBitCast(g->builder, recv, pts[0], "ifc.this") : recv;
                        for (int k = 0; k < uargc; k++)
                            ca[k + 1] = (k + 1 < (int)npar)
                                ? emit_boundary_coerce(g, avals[k], pts[k + 1]) : avals[k];
                        const char *cn = has_res ? "ifccall" : "";
                        LLVMValueRef r = zan_call2(g->builder, ifnty, ifn, ca, (unsigned)cargc, cn);
                        if (has_res) {
                            r = emit_boundary_coerce(g, r, res_ty);
                            phi_vals[np] = r; phi_bbs[np] = LLVMGetInsertBlock(g->builder); np++;
                        }
                        LLVMBuildBr(g->builder, merge);
                        free(ca); free(pts);
                        LLVMPositionBuilderAtEnd(g->builder, nextbb);
                    }

                    /* A well-formed interface value must have a matching
                     * implementation. Reaching this arm means the tag or the
                     * emitted method table is inconsistent; abort instead of
                     * returning a fabricated null/zero and swallowing the call. */
                    emit_runtime_check(g, LLVMConstInt(LLVMInt1TypeInContext(c), 1, 0),
                                       expr->loc, "interface dispatch has no implementation");
                    if (has_res) {
                        phi_vals[np] = LLVMConstNull(res_ty);
                        phi_bbs[np] = LLVMGetInsertBlock(g->builder); np++;
                    }
                    LLVMBuildBr(g->builder, merge);
                    LLVMPositionBuilderAtEnd(g->builder, merge);
                    LLVMValueRef result;
                    if (has_res) {
                        LLVMValueRef phi = LLVMBuildPhi(g->builder, res_ty, "ifc.res");
                        LLVMAddIncoming(phi, phi_vals, phi_bbs, (unsigned)np);
                        result = phi;
                    } else {
                        result = LLVMConstInt(LLVMInt32TypeInContext(c), 0, 0);
                    }
                    /* the merge block post-dominates every dispatch arm, so
                     * owned receiver/argument temps are released once here. */
                    for (int k = 0; k < uargc; k++)
                        emit_release_owned_call_temp(g, expr->call.args.items[k],
                                                     avals[k], locals);
                    emit_release_owned_call_temp(g, callee->member.object, recv,
                                                 locals);
                    free(avals); free(phi_vals); free(phi_bbs);
                    return result;
                }
            }
        }

        /* namespace-qualified static call: Foo.Bar.Widget.Method(args).
         * The callee's object is a name path (Foo.Bar.Widget) rather than a
         * bare class identifier, so the AST_IDENTIFIER static branch above
         * misses it. Types are registered by simple name, so the rightmost
         * path segment is the type name. Guard on the path head not being a
         * local so genuine instance chains (a.b.Method()) fall through to the
         * instance handlers above. */
        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS &&
            expr->call.callee->member.object->kind == AST_MEMBER_ACCESS) {
            zan_ast_node_t *callee = expr->call.callee;
            zan_ast_node_t *obj = callee->member.object;
            zan_ast_node_t *head = name_path_head(obj);
            if (is_name_path(obj) && head && !local_find(locals, head->ident.name)) {
                zan_symbol_t *type_sym = zan_binder_lookup(g->binder, obj->member.name);
                if (type_sym && (type_sym->kind == SYM_CLASS || type_sym->kind == SYM_STRUCT)) {
                    zan_symbol_t *method_sym = resolve_overload_typed(g, type_sym, callee->member.name, expr, locals);
                    if (method_sym) fill_default_args(g, expr, method_sym);
                    if (method_sym) pack_params_args(g, expr, method_sym, locals);
                    if (method_sym) {
                        for (int fi = irgen_find_function(g, method_sym); fi >= 0; fi = -1) {
                            if (g->functions[fi].sym == method_sym) {
                                int argc = expr->call.args.count;
                                LLVMValueRef *call_args = (LLVMValueRef *)calloc((size_t)(argc > 0 ? argc : 1), sizeof(LLVMValueRef));
                                for (int k = 0; k < argc; k++) {
                                    call_args[k] = emit_arg_typed(g, expr->call.args.items[k],
                                        method_param_type_at(g, method_sym, k, expr, NULL, locals), locals);
                                }
                                const char *cn = (LLVMGetTypeKind(LLVMGetReturnType(g->functions[fi].fn_type)) == LLVMVoidTypeKind) ? "" : "scall";
                                coerce_args_to_params(g, g->functions[fi].fn_type, call_args, argc);
                                LLVMValueRef result = zan_call2(g->builder, g->functions[fi].fn_type,
                                    g->functions[fi].fn, call_args, (unsigned)argc, cn);
                                zan_type_t *gret = generic_method_ret(g, method_sym, expr, locals);
                                if (!gret && method_ret_is_bare_tp(method_sym)) {
                                    zan_type_t *ir = method_ret_type_at(g, method_sym,
                                        expr, NULL, locals);
                                    if (ir && ir->kind != TYPE_TYPE_PARAM) gret = ir;
                                }
                                if (gret) {
                                    result = emit_boundary_coerce(g, result, map_type(g, gret));
                                    /* the erased body returned a borrowed +0
                                     * class reference; make it owned like
                                     * every other call result */
                                    if (gret->kind == TYPE_CLASS &&
                                        method_ret_is_bare_tp(method_sym))
                                        emit_arc_retain(g, result);
                                }
                                int consumes_free_arg =
                                    argc == 1 && call_consumes_free_arg(g->functions[fi].fn);
                                if (consumes_free_arg) {
                                    emit_invalidate_freed_string(g,
                                        expr->call.args.items[0], locals);
                                }
                                emit_extern_call_len_invalidate(g,
                                    g->functions[fi].fn, call_args, argc,
                                    expr, locals);
                                for (int k = 0; k < argc; k++) {
                                    if (!consumes_free_arg || k != 0) {
                                        emit_release_owned_call_temp(g, expr->call.args.items[k],
                                            call_args[k], locals);
                                    }
                                }
                                free(call_args);
                                return result;
                            }
                        }
                    }
                }
            }
        }

        /* extension method call: recv.M(args) lowers to the static method
         * Ext.M(recv, args) whose first parameter is declared `this T`. */
        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS) {
            zan_ast_node_t *callee = expr->call.callee;
            zan_type_t *recv_ty = infer_expr_type(g, callee->member.object, locals);
            zan_symbol_t *method_sym =
                find_extension_method(g, recv_ty, callee->member.name,
                                      expr->call.args.count,
                                      expr, callee->member.object, locals);
            if (method_sym) {
                int spec = try_method_spec(g, method_sym, expr,
                                           callee->member.object, locals);
                if (spec >= 0)
                    return emit_method_spec_call(g, spec, expr,
                                                 callee->member.object, locals);
                for (int fi = irgen_find_function(g, method_sym); fi >= 0; fi = -1) {
                    if (g->functions[fi].sym == method_sym) {
                        int argc = expr->call.args.count + 1;
                        LLVMValueRef *call_args = (LLVMValueRef *)calloc((size_t)argc, sizeof(LLVMValueRef));
                        call_args[0] = emit_arg_typed(g, callee->member.object,
                            method_param_type(g, method_sym, 0), locals);
                        for (int k = 0; k < expr->call.args.count; k++) {
                            call_args[k + 1] = emit_arg_typed(g, expr->call.args.items[k],
                                method_param_type_at(g, method_sym, k + 1, expr,
                                                     callee->member.object, locals), locals);
                        }
                        const char *cn = (LLVMGetTypeKind(LLVMGetReturnType(g->functions[fi].fn_type)) == LLVMVoidTypeKind) ? "" : "extcall";
                        coerce_args_to_params(g, g->functions[fi].fn_type, call_args, argc);
                        LLVMValueRef result = zan_call2(g->builder, g->functions[fi].fn_type,
                            g->functions[fi].fn, call_args, (unsigned)argc, cn);
                        /* generic ext method returning bare T: coerce the
                         * erased result to the inferred concrete type and, for
                         * classes, own the borrowed +0 reference the erased
                         * body returned */
                        if (method_ret_is_bare_tp(method_sym)) {
                            zan_type_t *gret = method_ret_type_at(g, method_sym,
                                expr, callee->member.object, locals);
                            if (gret && gret->kind != TYPE_TYPE_PARAM) {
                                result = emit_boundary_coerce(g, result, map_type(g, gret));
                                if (gret->kind == TYPE_CLASS)
                                    emit_arc_retain(g, result);
                            }
                        }
                        emit_release_owned_call_temp(g, callee->member.object, call_args[0], locals);
                        for (int k = 0; k < expr->call.args.count; k++) {
                            emit_release_owned_call_temp(g, expr->call.args.items[k],
                                call_args[k + 1], locals);
                        }
                        free(call_args);
                        return result;
                    }
                }
            }
        }

        /* bare function name call: Compute(21) → look up in current class then global */
        if (expr->call.callee && expr->call.callee->kind == AST_IDENTIFIER) {
            zan_istr_t fn_name = expr->call.callee->ident.name;

            /* try current class methods first */
            if (g->current_type_sym) {
                zan_symbol_t *method_sym = resolve_overload_typed(
            g, g->current_type_sym, fn_name, expr, locals);
                    if (method_sym) fill_default_args(g, expr, method_sym);
                    if (method_sym) pack_params_args(g, expr, method_sym, locals);
                if (method_sym) {
                    /* an unqualified call to a generic method of this class
                     * monomorphizes exactly like the qualified form */
                    int spec = try_method_spec(g, method_sym, expr, NULL, locals);
                    if (spec >= 0)
                        return emit_method_spec_call(g, spec, expr, NULL, locals);
                    for (int fi = irgen_find_function(g, method_sym); fi >= 0; fi = -1) {
                        if (g->functions[fi].sym == method_sym) {
                            int argc = expr->call.args.count;
                            bool is_static = (method_sym->modifiers & MOD_STATIC) != 0;
                            int extra = is_static ? 0 : 1;
                            LLVMValueRef *call_args = (LLVMValueRef *)calloc(
                                (size_t)(argc + extra > 0 ? argc + extra : 1), sizeof(LLVMValueRef));
                            if (!is_static && g->current_this) {
                                /* load `this` using the receiver's actual
                                 * pointer type, then make it agree with the
                                 * callee's first parameter type (they can
                                 * differ, e.g. struct* vs i8*). */
                                LLVMTypeRef this_ty = LLVMGetAllocatedType(g->current_this);
                                LLVMValueRef this_val = LLVMBuildLoad2(g->builder,
                                    this_ty, g->current_this, "this");
                                unsigned np = LLVMCountParamTypes(g->functions[fi].fn_type);
                                if (np > 0) {
                                    LLVMTypeRef *pts = (LLVMTypeRef *)calloc(np, sizeof(LLVMTypeRef));
                                    LLVMGetParamTypes(g->functions[fi].fn_type, pts);
                                    if (LLVMTypeOf(this_val) != pts[0]) {
                                        this_val = LLVMBuildBitCast(g->builder, this_val, pts[0], "thisc");
                                    }
                                    free(pts);
                                }
                                call_args[0] = this_val;
                            }
                            for (int k = 0; k < argc; k++) {
                                call_args[k + extra] = emit_arg_typed(g,
                                    expr->call.args.items[k],
                                    method_param_type_at(g, method_sym, k, expr,
                                                         NULL, locals),
                                    locals);
                            }
                            /* self-call inside a specialized variant stays in
                             * the same instantiation (receiver is `this`). */
                            LLVMTypeRef mft = g->functions[fi].fn_type;
                            LLVMValueRef mfn = route_generic_method(g, g->cur_inst,
                                method_sym, g->functions[fi].fn, mft, &mft);
                            const char *cn = (LLVMGetTypeKind(LLVMGetReturnType(mft)) == LLVMVoidTypeKind) ? "" : "bcall";
                            LLVMValueRef result = emit_dispatch_call(g,
                                is_static ? NULL : g->current_type_sym, method_sym,
                                mfn, mft, call_args, argc + extra, cn);
                            int consumes_free_arg =
                                argc == 1 && call_consumes_free_arg(g->functions[fi].fn);
                            if (consumes_free_arg) {
                                emit_invalidate_freed_string(g,
                                    expr->call.args.items[0], locals);
                            }
                            emit_extern_call_len_invalidate(g,
                                g->functions[fi].fn, call_args, argc + extra,
                                expr, locals);
                            for (int k = 0; k < argc; k++) {
                                if (!consumes_free_arg || k != 0) {
                                    emit_release_owned_call_temp(g, expr->call.args.items[k],
                                        call_args[k + extra], locals);
                                }
                            }
                            free(call_args);
                            return result;
                        }
                    }
                }
            }

            /* try global LLVM function by name */
            char name_buf[256];
            int nlen = fn_name.len < 255 ? fn_name.len : 255;
            memcpy(name_buf, fn_name.str, (size_t)nlen);
            name_buf[nlen] = '\0';
            LLVMValueRef global_fn = LLVMGetNamedFunction(g->mod, name_buf);
            if (global_fn) {
                int argc = expr->call.args.count;
                LLVMValueRef *call_args = (LLVMValueRef *)calloc(
                    (size_t)(argc > 0 ? argc : 1), sizeof(LLVMValueRef));
                LLVMTypeRef fn_type = LLVMGlobalGetValueType(global_fn);
                /* Bare literals mint i64, but an extern's formal may be a
                 * narrower int (or a differently-typed pointer). LLVM demands
                 * call operands match the declared signature exactly, so
                 * coerce each argument to the formal's type here. */
                unsigned nparams = LLVMCountParamTypes(fn_type);
                LLVMTypeRef ptypes[16];
                if (nparams > 16) { nparams = 16; }
                if (nparams > 0) {
                    LLVMGetParamTypes(fn_type, ptypes);
                }
                for (int k = 0; k < argc; k++) {
                    call_args[k] = emit_expr(g, expr->call.args.items[k], locals);
                    if ((unsigned)k >= nparams) { continue; }
                    LLVMTypeRef at = LLVMTypeOf(call_args[k]);
                    LLVMTypeRef pt = ptypes[k];
                    if (at == pt) { continue; }
                    if (LLVMGetTypeKind(at) == LLVMIntegerTypeKind
                        && LLVMGetTypeKind(pt) == LLVMIntegerTypeKind) {
                        unsigned aw = LLVMGetIntTypeWidth(at);
                        unsigned pw = LLVMGetIntTypeWidth(pt);
                        if (aw < pw) {
                            call_args[k] = (aw == 1)
                                ? LLVMBuildZExt(g->builder, call_args[k], pt,
                                                "arg.zext")
                                : LLVMBuildSExt(g->builder, call_args[k], pt,
                                                "arg.sext");
                        } else if (aw > pw) {
                            call_args[k] = LLVMBuildTrunc(g->builder,
                                call_args[k], pt, "arg.trunc");
                        }
                    } else if (LLVMGetTypeKind(at) == LLVMPointerTypeKind
                        && LLVMGetTypeKind(pt) == LLVMPointerTypeKind) {
                        call_args[k] = LLVMBuildBitCast(g->builder,
                            call_args[k], pt, "arg.ptrc");
                    }
                }
                const char *gcn = (LLVMGetTypeKind(LLVMGetReturnType(fn_type)) == LLVMVoidTypeKind) ? "" : "gcall";
                LLVMValueRef result = zan_call2(g->builder, fn_type,
                    global_fn, call_args, (unsigned)argc, gcn);
                int consumes_free_arg =
                    argc == 1 && call_consumes_free_arg(global_fn);
                if (consumes_free_arg) {
                    emit_invalidate_freed_string(g,
                        expr->call.args.items[0], locals);
                }
                emit_extern_call_len_invalidate(g, global_fn, call_args, argc,
                                               expr, locals);
                for (int k = 0; k < argc; k++) {
                    if (!consumes_free_arg || k != 0) {
                        emit_release_owned_call_temp(g, expr->call.args.items[k],
                            call_args[k], locals);
                    }
                }
                free(call_args);
                return result;
            }

            /* Robustness (A43-A15): an unqualified call that resolves to
             * neither a class method nor a global function used to fall
             * through every path and silently lower to an empty/zero
             * result -- the QueryBuilder BuildSelect incident. The
             * qualified form below already errors when the receiver type
             * lacks the member entirely; give the implicit-this form the
             * same contract. Name-only scan (arity ignored) so a genuine
             * overload/arity mismatch keeps reaching its own diagnostics,
             * and a local delegate/alias invocation stays exempt. */
            if (g->current_type_sym &&
                (g->current_type_sym->kind == SYM_CLASS ||
                 g->current_type_sym->kind == SYM_STRUCT)) {
                zan_symbol_t *cur = g->current_type_sym;
                int member_found = 0;
                while (cur && !member_found) {
                    for (int mi = 0; mi < cur->member_count; mi++) {
                        zan_symbol_t *m = cur->members[mi];
                        if (m && m->name.len == fn_name.len &&
                            memcmp(m->name.str, fn_name.str,
                                   (size_t)fn_name.len) == 0) {
                            member_found = 1;
                            break;
                        }
                    }
                    zan_symbol_t *base = (cur->type && cur->type->base_type)
                        ? cur->type->base_type->sym : NULL;
                    cur = (base && base != cur) ? base : NULL;
                }
                if (!member_found && !local_find(locals, expr->call.callee->ident.name)) {
                    zan_diag_emit(g->diag, DIAG_ERROR, expr->loc,
                        "'%.*s' has no member '%.*s'",
                        (int)g->current_type_sym->name.len,
                        g->current_type_sym->name.str,
                        (int)fn_name.len, fn_name.str);
                }
            }
        }

        /* Robustness: a call `obj.Method(...)` on a known class/struct type
         * where no member of that name exists anywhere in the class or its
         * base chain. Historically this silently lowered to a constant 0
         * (e.g. a missing IsOpen() "returned" false), which is very hard to
         * diagnose. Members that do exist (delegate fields, arity-mismatched
         * overloads) are left to the later paths / LLVM verification. */
        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS) {
            zan_ast_node_t *callee = expr->call.callee;
            zan_symbol_t *recv_cls = expr_class_sym(g, callee->member.object, locals);
            if (!recv_cls && callee->member.object->kind == AST_IDENTIFIER &&
                !local_find(locals, callee->member.object->ident.name)) {
                /* static call ClassName.Method(...) */
                zan_symbol_t *ts = zan_binder_lookup(g->binder, callee->member.object->ident.name);
                if (ts && (ts->kind == SYM_CLASS || ts->kind == SYM_STRUCT)) recv_cls = ts;
            }
            if (recv_cls && (recv_cls->kind == SYM_CLASS || recv_cls->kind == SYM_STRUCT)) {
                zan_istr_t mn = callee->member.name;
                int found = 0;
                zan_symbol_t *cur = recv_cls;
                while (cur && !found) {
                    for (int mi = 0; mi < cur->member_count; mi++) {
                        zan_symbol_t *m = cur->members[mi];
                        if (m && m->name.len == mn.len &&
                            memcmp(m->name.str, mn.str, mn.len) == 0) { found = 1; break; }
                    }
                    zan_symbol_t *base = (cur->type && cur->type->base_type)
                        ? cur->type->base_type->sym : NULL;
                    cur = (base && base != cur) ? base : NULL;
                }
                if (!found) {
                    zan_diag_emit(g->diag, DIAG_ERROR, expr->loc,
                        "'%.*s' has no member '%.*s'",
                        (int)recv_cls->name.len, recv_cls->name.str,
                        (int)mn.len, mn.str);
                }
            }
        }

        /* Robustness: a member-access call `X.Method(...)` where X is neither a
         * local variable nor any known symbol (class / struct / enum / namespace)
         * is an unresolved reference. This most often means the class was never
         * compiled — e.g. a stdlib file missing from the stdlib_map in main.c —
         * or a typo. Historically such a call silently lowered to a 0/null result
         * (a string method would then return "(null)"), which is very hard to
         * diagnose. Emit a hard compile error instead. Valid builtin calls
         * (Console.*, Math.*, ...) and resolved user methods return earlier, so
         * only genuinely unresolved references reach this point; keying on the
         * object being an unknown name (rather than a known class missing the
         * method) keeps this from firing on extern/DllImport members.
         *
         * `X` may itself be a dotted name path (`Foo.Bar.Baz.Quux(...)`) whose
         * head is a namespace and whose rightmost segment is the type name. The
         * AST nests such a path inside member accesses, so the single-identifier
         * check below would silently pass it through; walk the chain instead. A
         * chain rooted in a local (`a.b.c.M()`) is a genuine instance chain and
         * is left to the instance handlers above. */
        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS &&
            is_name_path(expr->call.callee->member.object)) {
            zan_ast_node_t *obj = expr->call.callee->member.object;
            zan_ast_node_t *head = name_path_head(obj);
            if (head && !local_find(locals, head->ident.name)) {
                zan_istr_t on = (obj->kind == AST_IDENTIFIER)
                    ? obj->ident.name : obj->member.name;
                zan_istr_t mn = expr->call.callee->member.name;
                zan_symbol_t *osym = zan_binder_lookup(g->binder, on);
                char path[256];
                int plen = format_name_path(expr->call.callee, path,
                                            (int)sizeof(path));
                /* `EnumType.Member.Method(...)`: the receiver folds to an
                 * enum constant, so its rightmost segment has no symbol of
                 * its own. When the member is declared and the method is one
                 * irgen lowers over the name table (ToString / TryParse),
                 * this is a resolved call, not an unresolved reference. */
                bool enum_const_recv = false;
                if (!osym && obj->kind == AST_MEMBER_ACCESS &&
                    obj->member.object->kind == AST_IDENTIFIER) {
                    zan_symbol_t *es = zan_binder_lookup(g->binder,
                        obj->member.object->ident.name);
                    if (es && es->kind == SYM_ENUM) {
                        for (int ei = 0; ei < es->member_count; ei++) {
                            zan_symbol_t *em = es->members[ei];
                            if (em && em->kind == SYM_ENUM_MEMBER &&
                                em->name.len == obj->member.name.len &&
                                memcmp(em->name.str, obj->member.name.str,
                                       (size_t)obj->member.name.len) == 0) {
                                enum_const_recv =
                                    (mn.len == 8 &&
                                     memcmp(mn.str, "ToString", 8) == 0) ||
                                    (mn.len == 8 &&
                                     memcmp(mn.str, "TryParse", 8) == 0);
                                break;
                            }
                        }
                    }
                }
                if (!osym && !enum_const_recv) {
                    zan_diag_emit(g->diag, DIAG_ERROR, expr->loc,
                        "unresolved call '%.*s': '%.*s' is not a known variable, "
                        "type, or namespace (is the class imported and registered "
                        "in the stdlib map?)",
                        plen, path, (int)on.len, on.str);
                }
                /* The type name is a known class/struct but the method does not
                 * exist on it (and no builtin lowering claimed the call earlier).
                 * This is the method-call twin of the check above: silently
                 * lowering to 0/null turns a typo or a missing stdlib method into
                 * a runtime null-pointer crash far from the call site. */
                else if (osym && (osym->kind == SYM_CLASS || osym->kind == SYM_STRUCT) &&
                         !get_method_sym(osym, mn)) {
                    zan_diag_emit(g->diag, DIAG_ERROR, expr->loc,
                        "unresolved call '%.*s': type '%.*s' has no method "
                        "'%.*s'",
                        plen, path, (int)on.len, on.str, (int)mn.len, mn.str);
                }
            }
        }

        /* Robustness: `recv.M(...)` where recv's static type is one of the
         * compiler's built-in types (string, List<T>, Dictionary<K,V>,
         * StringBuilder) and M is neither a member irgen lowers (builtin_api.c)
         * nor an extension method (tried above). Falling through to the
         * constant below made `s.PadLeft(4)` evaluate to 0 and `items.Sort()`
         * a no-op, with no diagnostic anywhere. */
        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS) {
            zan_ast_node_t *bcallee = expr->call.callee;
            zan_istr_t bmn = bcallee->member.name;
            zan_type_t *brt = infer_expr_type(g, bcallee->member.object, locals);
            const char *brecv = NULL;
            if (brt && brt->kind == TYPE_STRING) brecv = "string";
            else if (brt && type_named(brt, "List", 4)) brecv = "List";
            else if (brt && type_named(brt, "Dict", 4)) brecv = "Dict";
            else if (brt && type_named(brt, "StringBuilder", 13))
                brecv = "StringBuilder";
            if (brecv && !zan_builtin_has_member(brecv, bmn.str, (int)bmn.len)) {
                const zan_builtin_type_t *bt = zan_builtin_find(brecv);
                zan_diag_emit(g->diag, DIAG_ERROR, expr->loc,
                    "'%s' has no member '%.*s'",
                    bt ? bt->display : brecv, (int)bmn.len, bmn.str);
            }
            /* A property spelled with parentheses (`items.Count()`) parses and
             * type-checks, but no lowering claims it, so it used to fall
             * through to the constant 0 below -- a loop bounded by
             * `l.Count()` then silently did nothing. */
            else if (brecv &&
                     zan_builtin_member_kind(brecv, bmn.str, (int)bmn.len) == 'P') {
                const zan_builtin_type_t *bt = zan_builtin_find(brecv);
                zan_diag_emit(g->diag, DIAG_ERROR, expr->loc,
                    "'%s.%.*s' is a property, not a method: drop the '()'",
                    bt ? bt->display : brecv, (int)bmn.len, bmn.str);
            }
        }

        /* Nothing claimed this call, so it lowers to the constant below. When
         * the receiver is itself a chained call, the checks above cannot see
         * it: `Type.Gone().More()` leaves the receiver unemitted, so the
         * unresolved `Type.Gone` was never reported and the chain crashed at
         * runtime on the constant used as a pointer. Report the head of the
         * chain instead. */
        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS)
            diagnose_unresolved_static_chain(g, expr->call.callee->member.object);

        /* Reaching here means no lowering claimed the call: nothing was
         * emitted, so the call simply does not happen and the expression
         * becomes the zero below. Every specific shape checked above was added
         * after a bug where exactly that produced a silently wrong program, so
         * the general case has to report too -- a call the code generator
         * cannot resolve is a compile error, not a zero.
         *
         * Two contexts legitimately reach here and must stay quiet:
         *   - the ERASED body of a generic, where a receiver still typed as a
         *     type parameter has no methods to find. Calls are routed to the
         *     monomorphized copies (cur_inst / cur_mtps), so the erased body's
         *     zero is never executed.
         *   - anything downstream of an earlier error, where the cascade adds
         *     noise rather than information. */
        if (!zan_diag_has_errors(g->diag) &&
            !call_receiver_is_open_generic(g, expr, locals))
            zan_diag_emit(g->diag, DIAG_ERROR, expr->loc,
                          "this call could not be resolved to any callable "
                          "(no method, delegate, operator or builtin matches)");
        return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
    return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
}
