static bool ident_names_own_field(zan_irgen_t *g, zan_ast_node_t *e) {
    if (!e || e->kind != AST_IDENTIFIER) return false;
    if (!g->current_type_sym) return false;
    if (g->current_this &&
        get_field_index(g->current_type_sym, e->ident.name) >= 0)
        return true;
    zan_symbol_t *fs = get_field_sym(g->current_type_sym, e->ident.name);
    return fs && fs->kind == SYM_FIELD && (fs->modifiers & MOD_STATIC) != 0;
}

static bool type_mentions_type_param(zan_type_t *t, int depth) {
    if (!t || depth > 8) return false;
    if (t->kind == TYPE_TYPE_PARAM || t->kind == TYPE_ERROR) return true;
    if (t->element_type && type_mentions_type_param(t->element_type, depth + 1))
        return true;
    for (int i = 0; i < t->type_arg_count; i++)
        if (type_mentions_type_param(t->type_args[i], depth + 1)) return true;
    return false;
}

static bool call_receiver_is_open_generic(zan_irgen_t *g, zan_ast_node_t *call,
                                         local_scope_t *locals) {
    if (g->current_type_sym && g->current_type_sym->decl &&
        (g->current_type_sym->decl->kind == AST_CLASS_DECL ||
         g->current_type_sym->decl->kind == AST_STRUCT_DECL) &&
        g->current_type_sym->decl->type_decl.type_params.count > 0)
        return true;
    zan_ast_node_t *callee = call->call.callee;
    if (!callee) return false;
    if (callee->kind == AST_MEMBER_ACCESS)
        return type_mentions_type_param(
            infer_expr_type(g, callee->member.object, locals), 0);
    for (int i = 0; i < call->call.args.count; i++)
        if (type_mentions_type_param(
                infer_expr_type(g, call->call.args.items[i], locals), 0))
            return true;
    return false;
}

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
                zan_diag_emit_code(g->diag, DIAG_ERROR, recv->loc, 3050,
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

static int try_method_spec(zan_irgen_t *g, zan_symbol_t *msym,
                           zan_ast_node_t *call, zan_ast_node_t *recv_expr,
                           local_scope_t *locals) {
    if (!msym || !msym->decl || msym->decl->kind != AST_METHOD_DECL) return -1;
    zan_ast_list_t *tps = &msym->decl->method_decl.type_params;
    if (tps->count == 0 || tps->count > 8) return -1;
    bool m_static = (msym->decl->method_decl.modifiers & MOD_STATIC) != 0;
    int recv_params = (recv_expr && m_static) ? 1 : 0;
    if (msym->decl->method_decl.params.count !=
        call->call.args.count + recv_params) return -1;
    zan_type_t *owner_inst = NULL;
    if (!m_static) {
        zan_type_t *rt = recv_expr ? infer_expr_type(g, recv_expr, locals)
                                   : (g->current_type_sym ? g->current_type_sym->type
                                                          : NULL);
        if (!rt || rt->kind != TYPE_CLASS) return -1;
        if (!recv_expr && !g->current_this) return -1;
        /* `Pool<string> */
        if (rt->type_arg_count > 0) owner_inst = rt;
        else if (g->cur_inst && (!rt->sym || rt->sym == g->cur_inst->sym))
            owner_inst = g->cur_inst;
    }
    for (int j = 0; j < msym->decl->method_decl.params.count; j++)
        if (msym->decl->method_decl.params.items[j]->param.by_ref) return -1;
    zan_type_t *bind[8] = { NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL };
    infer_method_tp_bindings(g, msym, call, recv_expr, locals, bind);
    zan_compile_trace("method spec %.*s.%.*s bindc=%d bind0=%d bind0args=%d",
                      (int)(msym->parent ? msym->parent->name.len : 0),
                      msym->parent ? msym->parent->name.str : "",
                      (int)msym->name.len, msym->name.str, tps->count,
                      bind[0] ? (int)bind[0]->kind : -1,
                      bind[0] ? bind[0]->type_arg_count : -1);
    return get_or_create_method_spec(g, msym, bind, tps->count, owner_inst);
}

/* 实参入异常展开栈（临时量的所有权在调用方，调用抛异常时要释放） */
static int emit_call_arg_eh_push(zan_irgen_t *g, zan_ast_node_t *arg,
                                 LLVMValueRef val, local_scope_t *locals);

static LLVMValueRef emit_method_spec_call(zan_irgen_t *g, int idx,
                                          zan_ast_node_t *call,
                                          zan_ast_node_t *recv_expr,
                                          local_scope_t *locals) {
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
            bool is_async = (sp.msym && sp.msym->decl &&
                             sp.msym->decl->kind == AST_METHOD_DECL &&
                             (sp.msym->decl->method_decl.modifiers & MOD_ASYNC) != 0);
            if (call && call->kind == AST_CALL && call->call.callee &&
                call->call.callee->kind == AST_MEMBER_ACCESS) {
                self = emit_guarded_member_object_ex(g, call->call.callee, locals, is_async);
            } else {
                self = emit_expr(g, recv_expr, locals);
            }
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
        arg_eh_pushed[j + this_off] = emit_call_arg_eh_push(g, arg,
            call_args[j + this_off], locals);
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

/* 检查 Lambda 函数体内是否包含显式带值返回 (return <expr>;) */
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

static int is_string_like_expr(zan_irgen_t *g, zan_ast_node_t *e,
                               local_scope_t *locals) {
    return is_string_expr(g, e, locals) || expr_is_char(g, e, locals);
}

/* 发射`e` as a C string */
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

static int emit_call_arg_eh_push(zan_irgen_t *g, zan_ast_node_t *arg,
                                 LLVMValueRef val, local_scope_t *locals) {
    zan_type_t *at = infer_expr_type(g, arg, locals);
    if (!call_arg_is_owned_temp(g, arg, at, locals) ||
        LLVMGetTypeKind(LLVMTypeOf(val)) != LLVMPointerTypeKind) return 0;
    if (expr_yields_delegate_value(g, arg, locals)) {
        /* 委托实参：闭包在 IR 里是函数指针，而展开器按 i8ptr 取回闭包
           指针来跑它的析构，所以槽里存的是位转换后的 i8ptr。 */
        LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
        LLVMValueRef slot = emit_entry_alloca(g, i8ptr, "arg.eh");
        LLVMBuildStore(g->builder,
            LLVMBuildBitCast(g->builder, val, i8ptr, "arg.dlg"), slot);
        emit_eh_tmp_push_slot(g, slot, ZAN_EH_SLOT_DLG);
        return 1;
    }
    int ehk = eh_slot_kind_of(at);
    if (ehk != ZAN_EH_SLOT_OBJ) {
        LLVMValueRef slot = emit_entry_alloca(g,
            LLVMTypeOf(val), "arg.eh");
        LLVMBuildStore(g->builder, val, slot);
        emit_eh_tmp_push_slot(g, slot, ehk);
    } else {
        emit_eh_tmp_push(g, val);
    }
    return 1;
}

/* 调用无法解析时的兜底返回值。
 *
 * 开放泛型场景（泛型类的擦除变体里，类型实参还没定下来）会走到这里：
 * 诊断被有意压掉，但代码还得能编过。返回值必须与声明的返回类型一致——
 * 早先这里固定返回 i32 0，于是调用点拿 i32 去和字符串指针比较，
 * LLVM 校验直接报 "Both operands to ICmp instruction are not of the same
 * type"，整份程序编不出来。这里按返回类型给同类型的默认值。 */
static LLVMValueRef emit_unresolved_call_result(zan_irgen_t *g,
                                                 zan_ast_node_t *expr,
                                                 local_scope_t *locals) {
    LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
    zan_type_t *rt = infer_expr_type(g, expr, locals);
    if (!rt || rt->kind == TYPE_ERROR || rt->kind == TYPE_VOID) {
        return LLVMConstInt(i32t, 0, 0);
    }
    LLVMTypeRef lt = map_type(g, rt);
    switch (LLVMGetTypeKind(lt)) {
    case LLVMPointerTypeKind:
    case LLVMStructTypeKind:
    case LLVMArrayTypeKind:
    case LLVMVectorTypeKind:
        return LLVMConstNull(lt);
    case LLVMFloatTypeKind:
        return LLVMConstReal(lt, 0.0f);
    case LLVMDoubleTypeKind:
        return LLVMConstReal(lt, 0.0);
    case LLVMIntegerTypeKind:
        return LLVMConstInt(lt, 0, 0);
    default:
        return LLVMConstInt(i32t, 0, 0);
    }
}

/* Convert.ToString 这条内建只接受"真正有字符串形态"的值。类实例 / 数组 /
   委托 / object / 结构体都不行：
     - 把指针当 C 字符串读是**越界读**——读的是对象头里的指针字节，一路扫到
       进程映像里的某个 0 字节为止（实测打印出过进程里的符号名，并把程序
       带崩，0x80000003）；
     - 结构体落进 itoa 分支则生成 %struct.T 传给 i64 形参的非法 IR，LLVM
       校验直接失败（"Call parameter type does not match function signature"）。
   白名单与检查器的 type_is_concatable 同源；这里多放行三项"推不出来"的
   情形：错误类型（不追加二次报错）、类型参数（泛型实例化后才确定具体
   类型，此时按旧的宽松路径走，不要误伤泛型代码）、object（带标签的引用，
   编译期看不出里面装的是字符串还是类实例——object 上连通用 ToString() 都
   调不通，见 TASKS.md A444）。
   放行 object 的代价是它仍按 C 字符串读：里面装的是字符串时结果正确，装
   的是类实例时仍会越界读；治本要等 object 的通用 ToString 或把
   Convert.ToString 声明进 stdlib（A443），两边一起做。 */
static bool type_is_textual_or_numeric(zan_type_t *t) {
    if (!t) return true;
    switch (t->kind) {
    case TYPE_STRING: case TYPE_CHAR: case TYPE_ENUM: case TYPE_BOOL:
    case TYPE_BYTE: case TYPE_SHORT: case TYPE_INT: case TYPE_LONG:
    case TYPE_SBYTE: case TYPE_USHORT: case TYPE_UINT: case TYPE_ULONG:
    case TYPE_FLOAT: case TYPE_DOUBLE: case TYPE_DECIMAL: case TYPE_NINT:
    case TYPE_ERROR: case TYPE_TYPE_PARAM: case TYPE_OBJECT:
        return true;
    case TYPE_NULLABLE:
        return t->element_type ? type_is_textual_or_numeric(t->element_type) : true;
    default:
        return false;
    }
}

/* 纯文本解析型内建（int.Parse / double.Parse / TryParse）的实参必须是字符串。
   指针一路走到 strtoll/strtod 就是把对象的字节当文本读；数值实参更糟——
   strtoll 收 i8*，传 i64 进去直接生成非法 IR（LLVM 校验失败）。 */
static bool type_is_text_arg(zan_type_t *t) {
    if (!t) return true;
    switch (t->kind) {
    case TYPE_STRING: case TYPE_OBJECT: case TYPE_ERROR: case TYPE_TYPE_PARAM:
        return true;
    case TYPE_NULLABLE:
        return t->element_type ? type_is_text_arg(t->element_type) : true;
    default:
        return false;
    }
}

/* Math.* 的实参必须是数值。指针/结构体落进 SIToFP 会生成非法 IR——LLVM 校验
   失败（"Call parameter type does not match function signature"），编译直接
   死在代码生成阶段，用户看到的是内部错误而不是人话诊断。 */
static bool math_arg_is_numeric(LLVMValueRef v) {
    LLVMTypeKind k = LLVMGetTypeKind(LLVMTypeOf(v));
    /* bool 降成 i1：C# 里 bool 没有到 double 的隐式转换，Math.Sqrt(true)
     * 应编译期拒绝（i1 的 SIToFP 是符号扩展，true 曾被当成 -1.0 →
     * Sqrt 得 NaN，TASKS.md A498）。 */
    if (k == LLVMIntegerTypeKind &&
        LLVMGetIntTypeWidth(LLVMTypeOf(v)) == 1)
        return false;
    return k == LLVMIntegerTypeKind || k == LLVMDoubleTypeKind ||
           k == LLVMFloatTypeKind;
}

/* Math.* 的实参不能是 bool。判据是**语义类型**不是 LLVM 宽度：一个从泛型
 * 方法回来的 bool 走的是 i64 槽位（extend_int_for_slot 的 zeroext），
 * 按宽度判会漏过它 —— 同一个 bool 在两条路径上得到不同的判决
 * （TASKS.md A498；与 A492 的「宽度不是类型」同源）。C# 里 bool 没有到
 * double 的隐式转换，Math.Sqrt(true) 是编译错误。 */
static bool math_arg_is_bool(zan_irgen_t *g, zan_ast_node_t *arg_ast,
                              local_scope_t *locals) {
    /* infer_expr_type does not take a null node */
    if (!arg_ast) return false;
    return expr_is_bool(g, arg_ast, locals);
}

/* 数值实参统一成 double（int 走 sitofp，float 走 fpext）。 */
static LLVMValueRef math_arg_to_double(zan_irgen_t *g, LLVMValueRef v) {
    LLVMTypeRef dbl = LLVMDoubleTypeInContext(g->ctx);
    LLVMTypeKind k = LLVMGetTypeKind(LLVMTypeOf(v));
    if (k == LLVMFloatTypeKind) return LLVMBuildFPExt(g->builder, v, dbl, "ext");
    if (k == LLVMIntegerTypeKind) return LLVMBuildSIToFP(g->builder, v, dbl, "tofp");
    return v;
}

/* 不是数值就报人话诊断（fail-closed），返回 false 让调用方回一个 0 值。
 * arg_ast 是实参的 AST 节点：bool 的判决按语义类型做（见 math_arg_is_bool），
 * 宽度判 i1 只是兜底（expr_is_bool 判不出的类型仍按降级后的宽度拒）。 */
static bool math_require_numeric(zan_irgen_t *g, zan_ast_node_t *expr,
                                  zan_ast_node_t *arg_ast, LLVMValueRef v,
                                  local_scope_t *locals, const char *name) {
    if (math_arg_is_numeric(v) && !math_arg_is_bool(g, arg_ast, locals))
        return true;
    zan_diag_emit_code(g->diag, DIAG_ERROR, expr->loc, 3051,
                  "Math.%s requires an int or floating-point argument", name);
    return false;
}

/* A512：解析型转换的失败检查帮手（TASKS.md）。X.Parse 曾用 NULL endptr
 * 直落 strtoll/strtod——"abc" 返 0 静默继续，"2147483648" 溢出静默接受。
 * 失败语义与 TryParse 对齐：软诊断（stderr 一次 + file:line）+ 安全值 0，
 * --strict-runtime 下 exit 70。 */
static LLVMValueRef emit_parse_bad_pred(zan_irgen_t *g, LLVMValueRef s,
                                        LLVMValueRef ep) {
    if (!g->runtime_checks || !g->current_fn)
        return LLVMConstInt(LLVMInt1TypeInContext(g->ctx), 0, 0);
    LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
    LLVMValueRef ep_null = zan_icmp(g->builder, LLVMIntEQ, ep,
        LLVMConstNull(LLVMTypeOf(ep)), "parse.epnull");
    LLVMValueRef empty = zan_irgen_intern_string(g, "");
    LLVMValueRef ep_safe = LLVMBuildSelect(g->builder, ep_null,
        empty, ep, "parse.epsafe");
    LLVMValueRef ch = LLVMBuildLoad2(g->builder, i8, ep_safe, "parse.c");
    LLVMValueRef trailing = zan_icmp(g->builder, LLVMIntNE, ch,
        LLVMConstInt(i8, 0, 0), "parse.trail");
    LLVMValueRef none = zan_icmp(g->builder, LLVMIntEQ, ep, s,
        "parse.none");
    LLVMValueRef b1 = LLVMBuildOr(g->builder, ep_null, trailing, "parse.b1");
    return LLVMBuildOr(g->builder, b1, none, "parse.bad");
}

/* null 串进 strtoll 是 UB：先 guard（软诊断 + 空串替身） */
static LLVMValueRef emit_parse_str_guard(zan_irgen_t *g, LLVMValueRef s,
                                         zan_loc_t loc) {
    if (!g->runtime_checks || !g->current_fn) return s;
    LLVMValueRef isnull = zan_icmp(g->builder, LLVMIntEQ, s,
        LLVMConstNull(LLVMTypeOf(s)), "parse.snull");
    emit_runtime_check(g, isnull, loc,
        "null string passed to a parse function");
    return LLVMBuildSelect(g->builder, isnull,
        zan_irgen_intern_string(g, ""), s, "parse.ssafe");
}
/* A555：检查类边是否与泛型实参匹配以实现目标接口 */
static bool ifc_edge_matches_inst(zan_ast_node_t *edge_ref, zan_ast_list_t *tps,
                                   zan_type_t **args, zan_type_t *sup) {
    if (!edge_ref || edge_ref->kind != AST_TYPE_REF || !sup || !sup->sym)
        return false;
    zan_istr_t en = edge_ref->type_ref.name;
    if (en.len != sup->sym->name.len ||
        memcmp(en.str, sup->sym->name.str, (size_t)en.len) != 0)
        return false;
    zan_ast_list_t *eargs = &edge_ref->type_ref.type_args;
    int want = sup->type_arg_count;
    if (eargs->count != want) return false;
    for (int j = 0; j < want; j++) {
        zan_ast_node_t *ea = eargs->items[j];
        if (!ea) return false;
        if (ea->kind == AST_TYPE_REF && ea->type_ref.type_args.count > 0)
            return false;
        zan_istr_t ename;
        if (ea->kind == AST_IDENTIFIER) ename = ea->ident.name;
        else if (ea->kind == AST_TYPE_REF) ename = ea->type_ref.name;
        else return false;
        int k = 0;
        int tps_count = tps ? tps->count : 0;
        for (; k < tps_count; k++) {
            zan_istr_t tn = tps->items[k]->ident.name;
            if (tn.len == ename.len &&
                memcmp(tn.str, ename.str, (size_t)tn.len) == 0)
                break;
        }
        if (k < tps_count) {
            if (!args || !args[k] || !types_equal(args[k], sup->type_args[j]))
                return false;
        } else {
            zan_type_t *sa = sup->type_args[j];
            if (!sa || ename.len != sa->name.len ||
                memcmp(ename.str, sa->name.str, (size_t)sa->name.len) != 0)
                return false;
        }
    }
    return true;
}

static bool class_inst_implements_iface(zan_irgen_t *g, zan_symbol_t *cls,
                                        zan_type_t *inst, zan_type_t *obj_ty) {
    if (!cls || !obj_ty || !obj_ty->sym) return false;
    if (!class_implements_iface(cls, obj_ty->sym)) return false;
    if (obj_ty->type_arg_count == 0) return true;
    if (cls->decl && (cls->decl->kind == AST_CLASS_DECL ||
                      cls->decl->kind == AST_STRUCT_DECL ||
                      cls->decl->kind == AST_INTERFACE_DECL)) {
        zan_ast_list_t *tps = &cls->decl->type_decl.type_params;
        zan_ast_list_t *bases = &cls->decl->type_decl.bases;
        zan_type_t **args = inst ? inst->type_args : NULL;
        for (int i = 0; i < bases->count; i++) {
            if (ifc_edge_matches_inst(bases->items[i], tps, args, obj_ty))
                return true;
            if (bases->items[i] && bases->items[i]->kind == AST_TYPE_REF && g && g->binder) {
                zan_symbol_t *isym = zan_binder_lookup(g->binder,
                                                       bases->items[i]->type_ref.name);
                if (isym && isym->kind == SYM_INTERFACE &&
                    class_inst_implements_iface(g, isym, inst, obj_ty))
                    return true;
            }
        }
    }
    if (cls->type && cls->type->base_type && cls->type->base_type->sym &&
        cls->type->base_type->sym != cls) {
        zan_type_t *binst = inst;
        if (!binst && cls->type->base_type->type_arg_count > 0)
            binst = cls->type->base_type;
        return class_inst_implements_iface(g, cls->type->base_type->sym, binst, obj_ty);
    }
    return false;
}


/* ---- A558：System.Object 方法族 ---------------------------------------
 * object/接口/类接收者上的 ToString()、Equals(o)、GetHashCode()。object
 * 没有成员表，这三者此前连纯类接收者都绑不上（E3068）。
 * 分派沿用接口分派的"身份 tag 候选链"：堆对象首字 = 类型 vtable 全局
 * （类 vptr / A556 结构体箱 tag），逐候选比对命中后直呼该类**最派生**
 * 实现（resolve_overload 沿基类链解析，覆写生效；泛型类走实例 vtable，
 * 同 ifc；结构体候选 this = 载荷 +8，同 A556-2）。
 * 字符串接收者首字不是 vtable：ToString 返还自身，Equals 按内容 strcmp
 * （String.CompareOrdinal 同口径，Zan 字符串保证 NUL 结尾），
 * GetHashCode djb2。
 * 未命中候选 = 没写覆写 → object 缺省：ToString 返回运行期类型名（分配
 * 点 tynames 首项，与 `is` 同一描述符机制；无描述符/数组回落 "object"），
 * Equals 按引用相等，GetHashCode 指针混排。
 * 静态类型（含基类链）声明了同名成员时由普通成员调用路径处理，不进这里；
 * Equals 候选只收引用形参的 1 参覆写（值形参重载经 object 接收者不可达，
 * C# 同款是装箱走 Equals(object)）。 */
static int objfam_ref_kind(zan_type_t *t) {
    return t && (t->kind == TYPE_OBJECT || t->kind == TYPE_CLASS ||
                 t->kind == TYPE_INTERFACE || t->kind == TYPE_STRING ||
                 t->kind == TYPE_ARRAY || t->kind == TYPE_DELEGATE ||
                 t->kind == TYPE_TASK);
}

static LLVMValueRef emit_object_family_call(zan_irgen_t *g,
                                            zan_ast_node_t *expr, int fam,
                                            local_scope_t *locals) {
    LLVMContextRef c = g->ctx;
    LLVMTypeRef i8 = LLVMInt8TypeInContext(c);
    LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(c);
    LLVMTypeRef i32 = LLVMInt32TypeInContext(c);
    LLVMTypeRef i1 = LLVMInt1TypeInContext(c);
    /* size_t 随目标位宽（wasm32/arm32 上是 i32），strlen/memcpy 按它发 */
    unsigned psz = LLVMPointerSize(LLVMGetModuleDataLayout(g->mod));
    LLVMTypeRef szt = (psz == 8) ? i64 : i32;
    zan_ast_node_t *callee = expr->call.callee;
    zan_istr_t fmn = callee->member.name;

    zan_type_t *res_zt = (fam == 0) ? g->binder->type_string
                       : (fam == 1) ? g->binder->type_bool
                       : g->binder->type_int;
    LLVMTypeRef res_ty = map_type(g, res_zt);

    LLVMValueRef recv = emit_guarded_member_object_ex(g, callee, locals, false);
    LLVMValueRef aval = NULL;
    int n_eh_args = 0;
    if (fam == 1) {
        aval = emit_arg_typed(g, expr->call.args.items[0],
                              g->binder->type_object, locals);
        n_eh_args += emit_call_arg_eh_push(g, expr->call.args.items[0],
                                           aval, locals);
    }

    typedef struct {
        zan_symbol_t *cls;
        zan_symbol_t *impl_m;
        LLVMValueRef  fn;
        LLVMTypeRef   fnty;
        LLVMValueRef  tagc;
    } objfam_candidate_t;

    int cand_cap = g->struct_type_count + g->generic_inst_count + 4;
    objfam_candidate_t *cands = (objfam_candidate_t *)calloc(
        (size_t)cand_cap, sizeof(objfam_candidate_t));
    int cand_count = 0;
    int want_argc = (fam == 1) ? 1 : 0;

    /* 非泛型类/结构体候选：0 参（ToString/GetHashCode 返回 string/int）
     * 或 1 参引用形参（Equals 返回 bool），非静态。resolve_overload 沿
     * 基类链解析，同一覆写在基类、派生类各产一个候选，tag 比对天然按
     * 运行期类型命中其一。 */
    for (int si = 0; si < g->struct_type_count; si++) {
        zan_symbol_t *cls = g->struct_types[si].sym;
        if (!cls || is_user_generic_sym(cls)) continue;
        zan_symbol_t *m = resolve_overload(cls, fmn, want_argc, 0);
        if (!m || (m->modifiers & MOD_STATIC)) continue;
        if (!m->decl || m->decl->kind != AST_METHOD_DECL ||
            !m->decl->method_decl.return_type)
            continue;
        zan_type_t *mret = zan_binder_resolve_type(g->binder,
            m->decl->method_decl.return_type);
        zan_type_t *want_ret = (fam == 0) ? g->binder->type_string
                             : (fam == 1) ? g->binder->type_bool
                             : g->binder->type_int;
        if (!mret || mret->kind != want_ret->kind) continue;
        if (fam == 1) {
            zan_type_t *mp = method_param_type_at(g, m, 0, expr,
                                                  callee->member.object,
                                                  locals);
            if (mp && !objfam_ref_kind(mp)) continue;
        }
        LLVMValueRef ifn = NULL; LLVMTypeRef ifnty = NULL;
        for (int fi = irgen_find_function(g, m); fi >= 0; fi = -1)
            if (g->functions[fi].sym == m) {
                ifn = g->functions[fi].fn;
                ifnty = g->functions[fi].fn_type;
                break;
            }
        if (!ifn || !ifnty || cand_count >= cand_cap) continue;
        cands[cand_count].cls = cls;
        cands[cand_count].impl_m = m;
        cands[cand_count].fn = ifn;
        cands[cand_count].fnty = ifnty;
        cands[cand_count].tagc =
            LLVMConstBitCast(get_vtable_global(g, cls), i8ptr);
        cand_count++;
    }
    /* 泛型实例化类候选（实例 vtable 作 tag，特化方法体作实现，同 ifc） */
    for (int gi = 0; gi < g->generic_inst_count; gi++) {
        zan_symbol_t *cls = g->generic_insts[gi].type_sym;
        zan_type_t *inst = g->generic_insts[gi].inst;
        if (!cls || !inst || !is_user_generic_sym(cls)) continue;
        zan_symbol_t *impl_m = resolve_overload(cls, fmn, want_argc, 0);
        if (!impl_m || (impl_m->modifiers & MOD_STATIC)) continue;
        zan_symbol_t *msym_owner = impl_m->parent ? impl_m->parent : cls;
        zan_type_t *owner = is_user_generic_sym(msym_owner)
            ? vtable_owner_inst(g, inst, msym_owner) : NULL;
        LLVMValueRef ifn = NULL; LLVMTypeRef ifnty = NULL;
        if (owner && owner->type_arg_count > 0) {
            ifn = find_generic_fn(g, impl_m, owner->type_args,
                                  owner->type_arg_count, &ifnty);
        } else if (!is_user_generic_sym(msym_owner)) {
            for (int fi = irgen_find_function(g, impl_m); fi >= 0; fi = -1)
                if (g->functions[fi].sym == impl_m) {
                    ifn = g->functions[fi].fn;
                    ifnty = g->functions[fi].fn_type;
                    break;
                }
        }
        if (!ifn || !ifnty || cand_count >= cand_cap) continue;
        cands[cand_count].cls = cls;
        cands[cand_count].impl_m = impl_m;
        cands[cand_count].fn = ifn;
        cands[cand_count].fnty = ifnty;
        cands[cand_count].tagc = LLVMConstBitCast(
            get_vtable_global_inst(g, cls, inst), i8ptr);
        cand_count++;
    }

    LLVMValueRef result = LLVMConstNull(res_ty);
    if (LLVMGetTypeKind(LLVMTypeOf(recv)) == LLVMPointerTypeKind) {
        LLVMBasicBlockRef merge = LLVMAppendBasicBlockInContext(c,
            g->current_fn, "objf.m");
        LLVMValueRef *phi_vals = (LLVMValueRef *)calloc(
            (size_t)(cand_count + 4), sizeof(LLVMValueRef));
        LLVMBasicBlockRef *phi_bbs = (LLVMBasicBlockRef *)calloc(
            (size_t)(cand_count + 4), sizeof(LLVMBasicBlockRef));
        int np = 0;
        LLVMBasicBlockRef str_bb = LLVMAppendBasicBlockInContext(c,
            g->current_fn, "objf.s");
        LLVMBasicBlockRef tag_bb = LLVMAppendBasicBlockInContext(c,
            g->current_fn, "objf.t");
        LLVMBasicBlockRef def_bb = LLVMAppendBasicBlockInContext(c,
            g->current_fn, "objf.d");

        /* 接收者 site 字：高 32 位 = ZAN 字符串 tag 即字符串对象 */
        LLVMValueRef neg8 = LLVMConstInt(i64, (uint64_t)ZAN_OBJ_SITE_OFF, 1);
        LLVMValueRef sptr = LLVMBuildGEP2(g->builder, i8, recv, &neg8, 1,
                                          "objf.sptr");
        LLVMValueRef site = LLVMBuildLoad2(g->builder, i64,
            LLVMBuildBitCast(g->builder, sptr, LLVMPointerType(i64, 0),
                             "objf.sp"), "objf.site");
        LLVMValueRef isstr = zan_hdr_is_string(g, site, "objf.str");
        LLVMBasicBlockRef entry_bb = LLVMGetInsertBlock(g->builder);
        LLVMBuildCondBr(g->builder, isstr, str_bb, tag_bb);

        /* -- 字符串接收者 ------------------------------------------------ */
        LLVMPositionBuilderAtEnd(g->builder, str_bb);
        if (fam == 0) {
            emit_string_retain(g, recv);
            phi_vals[np] = recv;
            phi_bbs[np] = str_bb;
            np++;
            LLVMBuildBr(g->builder, merge);
        } else if (fam == 1) {
            LLVMBasicBlockRef arg_str_bb = LLVMAppendBasicBlockInContext(c,
                g->current_fn, "objf.as");
            LLVMBasicBlockRef eq_bb = LLVMAppendBasicBlockInContext(c,
                g->current_fn, "objf.eq");
            LLVMBasicBlockRef false_bb = LLVMAppendBasicBlockInContext(c,
                g->current_fn, "objf.f");
            LLVMValueRef argnn = zan_icmp(g->builder, LLVMIntNE, aval,
                LLVMConstNull(LLVMTypeOf(aval)), "objf.ann");
            LLVMBuildCondBr(g->builder, argnn, arg_str_bb, false_bb);
            LLVMPositionBuilderAtEnd(g->builder, arg_str_bb);
            LLVMValueRef site2 = LLVMBuildLoad2(g->builder, i64,
                LLVMBuildBitCast(g->builder,
                    LLVMBuildGEP2(g->builder, i8, aval, &neg8, 1, "objf.a2"),
                    LLVMPointerType(i64, 0), "objf.a2p"), "objf.site2");
            LLVMValueRef isstr2 = zan_hdr_is_string(g, site2, "objf.str2");
            LLVMBuildCondBr(g->builder, isstr2, eq_bb, false_bb);
            LLVMPositionBuilderAtEnd(g->builder, eq_bb);
            LLVMTypeRef strcmp_ty = LLVMFunctionType(i32,
                (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0);
            LLVMValueRef cmp = zan_call2(g->builder, strcmp_ty,
                get_libc_fn(g, "strcmp", strcmp_ty),
                (LLVMValueRef[]){ recv, aval }, 2, "objf.scmp");
            phi_vals[np] = zan_icmp(g->builder, LLVMIntEQ, cmp,
                LLVMConstInt(i32, 0, 0), "objf.seq");
            phi_bbs[np] = eq_bb;
            np++;
            LLVMBuildBr(g->builder, merge);
            LLVMPositionBuilderAtEnd(g->builder, false_bb);
            phi_vals[np] = LLVMConstInt(i1, 0, 0);
            phi_bbs[np] = false_bb;
            np++;
            LLVMBuildBr(g->builder, merge);
        } else {
            /* djb2：hash = hash*33 + byte（i64 环绕），NUL 截断 */
            LLVMBasicBlockRef body_bb = LLVMAppendBasicBlockInContext(c,
                g->current_fn, "objf.hb");
            LLVMValueRef hash = LLVMBuildPhi(g->builder, i64, "objf.h");
            LLVMValueRef idx = LLVMBuildPhi(g->builder, i64, "objf.i");
            LLVMValueRef chp = LLVMBuildGEP2(g->builder, i8, recv, &idx, 1,
                                             "objf.chp");
            LLVMValueRef ch = LLVMBuildLoad2(g->builder, i8, chp, "objf.ch");
            LLVMBasicBlockRef done_bb = LLVMAppendBasicBlockInContext(c,
                g->current_fn, "objf.hd");
            LLVMBuildCondBr(g->builder,
                zan_icmp(g->builder, LLVMIntEQ, ch, LLVMConstInt(i8, 0, 0),
                         "objf.hz"),
                done_bb, body_bb);
            LLVMPositionBuilderAtEnd(g->builder, body_bb);
            LLVMValueRef hash2 = zan_add(g->builder,
                LLVMBuildMul(g->builder, hash, LLVMConstInt(i64, 33, 0),
                             "objf.h33"),
                LLVMBuildZExt(g->builder, ch, i64, "objf.ch6"), "objf.h2");
            LLVMValueRef idx2 = zan_add(g->builder, idx,
                LLVMConstInt(i64, 1, 0), "objf.i2");
            LLVMBuildBr(g->builder, str_bb);
            LLVMPositionBuilderAtEnd(g->builder, done_bb);
            phi_vals[np] = LLVMBuildTrunc(g->builder, hash, i32, "objf.h32");
            phi_bbs[np] = done_bb;
            np++;
            LLVMBuildBr(g->builder, merge);
            LLVMValueRef h_in[2] = { LLVMConstInt(i64, 5381, 0), hash2 };
            LLVMBasicBlockRef h_bb[2] = { entry_bb, body_bb };
            LLVMAddIncoming(hash, h_in, h_bb, 2);
            LLVMValueRef i_in[2] = { LLVMConstInt(i64, 0, 0), idx2 };
            LLVMAddIncoming(idx, i_in, h_bb, 2);
        }

        /* -- 堆对象：tag 候选链 ------------------------------------------ */
        LLVMPositionBuilderAtEnd(g->builder, tag_bb);
        LLVMValueRef tag = LLVMBuildLoad2(g->builder, i8ptr,
            LLVMBuildBitCast(g->builder, recv, LLVMPointerType(i8ptr, 0),
                             "objf.vpp"), "objf.tag");
        for (int ci = 0; ci < cand_count; ci++) {
            LLVMBasicBlockRef callbb = LLVMAppendBasicBlockInContext(c,
                g->current_fn, "objf.call");
            LLVMBasicBlockRef nextbb = LLVMAppendBasicBlockInContext(c,
                g->current_fn, "objf.n");
            LLVMValueRef cmp = zan_icmp(g->builder, LLVMIntEQ, tag,
                cands[ci].tagc, "objf.eq");
            LLVMBuildCondBr(g->builder, cmp, callbb, nextbb);
            LLVMPositionBuilderAtEnd(g->builder, callbb);
            unsigned npar = LLVMCountParamTypes(cands[ci].fnty);
            LLVMTypeRef *pts = (LLVMTypeRef *)calloc(
                (size_t)(npar > 0 ? npar : 1), sizeof(LLVMTypeRef));
            LLVMGetParamTypes(cands[ci].fnty, pts);
            int cargc = want_argc + 1;
            LLVMValueRef *ca = (LLVMValueRef *)calloc((size_t)cargc,
                                                      sizeof(LLVMValueRef));
            /* A556-2：结构体箱载荷在首字 tag 之后（+8） */
            LLVMValueRef thisv = recv;
            if (cands[ci].cls->kind == SYM_STRUCT)
                thisv = LLVMBuildGEP2(g->builder, i8, recv,
                    (LLVMValueRef[]){ LLVMConstInt(i64, 8, 0) }, 1,
                    "objf.p8");
            ca[0] = (npar > 0)
                ? LLVMBuildBitCast(g->builder, thisv, pts[0], "objf.this")
                : thisv;
            if (fam == 1 && npar > 1)
                ca[1] = emit_boundary_coerce(g, aval, pts[1]);
            LLVMValueRef r = zan_call2(g->builder, cands[ci].fnty,
                cands[ci].fn, ca, (unsigned)cargc, "objf.call");
            r = emit_boundary_coerce(g, r, res_ty);
            phi_vals[np] = r;
            phi_bbs[np] = LLVMGetInsertBlock(g->builder);
            np++;
            LLVMBuildBr(g->builder, merge);
            free(ca);
            free(pts);
            LLVMPositionBuilderAtEnd(g->builder, nextbb);
        }
        LLVMBuildBr(g->builder, def_bb);

        /* -- object 缺省 ------------------------------------------------- */
        LLVMPositionBuilderAtEnd(g->builder, def_bb);
        if (fam == 0) {
            LLVMBasicBlockRef def_name_bb = LLVMAppendBasicBlockInContext(c,
                g->current_fn, "objf.dn");
            /* 运行期类型名：分配点 tynames 首项（与 `is` 同一描述符机制），
             * 越界/无描述符/数组 → "object"。 */
            LLVMValueRef nm_vals[4];
            LLVMBasicBlockRef nm_bbs[4];
            int nmi = 0;
            if (g->desc_hdr) {
                LLVMValueRef ok = zan_and(g->builder,
                    zan_and(g->builder,
                        zan_icmp(g->builder, LLVMIntNE, site,
                                 LLVMConstInt(i64, 0, 0), "objf.nz"),
                        zan_icmp(g->builder, LLVMIntNE, site,
                                 LLVMConstInt(i64, ZAN_ARRAY_MAGIC, 0),
                                 "objf.narr"), "objf.ok2"),
                    zan_icmp(g->builder, LLVMIntUGE, site,
                             LLVMConstInt(i64, 4096, 0), "objf.big"),
                    "objf.ok");
                LLVMBasicBlockRef dsc_bb = LLVMAppendBasicBlockInContext(c,
                    g->current_fn, "objf.dsc");
                LLVMBuildCondBr(g->builder, ok, dsc_bb, def_name_bb);
                nm_vals[nmi] = LLVMConstNull(i8ptr);
                nm_bbs[nmi] = def_bb;
                nmi++;
                LLVMPositionBuilderAtEnd(g->builder, dsc_bb);
                LLVMValueRef dsc = LLVMBuildIntToPtr(g->builder, site, i8ptr,
                                                     "objf.dsc.p");
                LLVMValueRef list = LLVMBuildLoad2(g->builder, i8ptr,
                    LLVMBuildBitCast(g->builder,
                        LLVMBuildGEP2(g->builder, i8, dsc,
                            (LLVMValueRef[]){ LLVMConstInt(i64, 8, 0) }, 1,
                            "objf.tnp"),
                        LLVMPointerType(i8ptr, 0), "objf.tnpp"), "objf.tn");
                LLVMValueRef lnn = zan_icmp(g->builder, LLVMIntNE, list,
                    LLVMConstNull(i8ptr), "objf.lnn");
                LLVMBasicBlockRef nm_bb = LLVMAppendBasicBlockInContext(c,
                    g->current_fn, "objf.nm");
                LLVMBuildCondBr(g->builder, lnn, nm_bb, def_name_bb);
                nm_vals[nmi] = LLVMConstNull(i8ptr);
                nm_bbs[nmi] = dsc_bb;
                nmi++;
                LLVMPositionBuilderAtEnd(g->builder, nm_bb);
                LLVMValueRef nm0 = LLVMBuildLoad2(g->builder, i8ptr, list,
                                                  "objf.nm0");
                LLVMBuildBr(g->builder, def_name_bb);
                nm_vals[nmi] = nm0;
                nm_bbs[nmi] = nm_bb;
                nmi++;
            } else if (g->g_site_tynames && g->g_site_count) {
                LLVMValueRef bound = LLVMBuildLoad2(g->builder, i64,
                    g->g_site_count, "objf.bound");
                LLVMValueRef ok = zan_and(g->builder,
                    zan_icmp(g->builder, LLVMIntSGE, site,
                             LLVMConstInt(i64, 0, 0), "objf.neg"),
                    zan_icmp(g->builder, LLVMIntSLT, site, bound,
                             "objf.oob"), "objf.ok");
                LLVMBasicBlockRef tbl_bb = LLVMAppendBasicBlockInContext(c,
                    g->current_fn, "objf.tbl");
                LLVMBuildCondBr(g->builder, ok, tbl_bb, def_name_bb);
                nm_vals[nmi] = LLVMConstNull(i8ptr);
                nm_bbs[nmi] = def_bb;
                nmi++;
                LLVMPositionBuilderAtEnd(g->builder, tbl_bb);
                LLVMValueRef tbl = LLVMBuildLoad2(g->builder,
                    LLVMPointerType(i8ptr, 0), g->g_site_tynames,
                    "objf.tbl.p");
                LLVMValueRef list = LLVMBuildLoad2(g->builder, i8ptr,
                    LLVMBuildGEP2(g->builder, i8ptr, tbl, &site, 1,
                                  "objf.lp"), "objf.list");
                LLVMValueRef lnn = zan_icmp(g->builder, LLVMIntNE, list,
                    LLVMConstNull(i8ptr), "objf.lnn");
                LLVMBasicBlockRef nm_bb = LLVMAppendBasicBlockInContext(c,
                    g->current_fn, "objf.nm");
                LLVMBuildCondBr(g->builder, lnn, nm_bb, def_name_bb);
                nm_vals[nmi] = LLVMConstNull(i8ptr);
                nm_bbs[nmi] = tbl_bb;
                nmi++;
                LLVMPositionBuilderAtEnd(g->builder, nm_bb);
                LLVMValueRef nm0 = LLVMBuildLoad2(g->builder, i8ptr, list,
                                                  "objf.nm0");
                LLVMBuildBr(g->builder, def_name_bb);
                nm_vals[nmi] = nm0;
                nm_bbs[nmi] = nm_bb;
                nmi++;
            } else {
                LLVMBuildBr(g->builder, def_name_bb);
                nm_vals[nmi] = LLVMConstNull(i8ptr);
                nm_bbs[nmi] = def_bb;
                nmi++;
            }
            LLVMPositionBuilderAtEnd(g->builder, def_name_bb);
            LLVMValueRef name0 = LLVMBuildPhi(g->builder, i8ptr, "objf.nmphi");
            LLVMAddIncoming(name0, nm_vals, nm_bbs, (unsigned)nmi);
            LLVMValueRef nm_null = zan_icmp(g->builder, LLVMIntEQ, name0,
                LLVMConstNull(i8ptr), "objf.nmn");
            LLVMValueRef lit_object = zan_irgen_intern_string(g, "object");
            LLVMValueRef nm = LLVMBuildSelect(g->builder, nm_null,
                lit_object, name0, "objf.nm.sel");
            LLVMTypeRef strlen_ty = LLVMFunctionType(szt,
                (LLVMTypeRef[]){ i8ptr }, 1, 0);
            LLVMValueRef len = zan_call2(g->builder, strlen_ty,
                get_libc_fn(g, "strlen", strlen_ty),
                (LLVMValueRef[]){ nm }, 1, "objf.len");
            LLVMValueRef len64 = (psz == 8)
                ? len : LLVMBuildZExt(g->builder, len, i64, "objf.len64");
            LLVMValueRef payload = zan_add(g->builder, len64,
                LLVMConstInt(i64, 1, 0), "objf.pay");
            LLVMValueRef s = zan_call2(g->builder,
                LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i64 }, 1, 0),
                g->rt_str_alloc, &payload, 1, "objf.s");
            LLVMBuildMemCpy(g->builder, s, 1, nm, 1, len);
            LLVMValueRef nul_p = LLVMBuildGEP2(g->builder, i8, s, &len64, 1,
                                               "objf.nulp");
            LLVMBuildStore(g->builder, LLVMConstInt(i8, 0, 0), nul_p);
            /* 长度字回填：ZANS<<32 | len（rt_str_alloc 只写了 LEN_UNKNOWN） */
            LLVMValueRef hdrw = zan_or(g->builder,
                LLVMBuildShl(g->builder,
                    LLVMConstInt(i64, ZAN_STRING_TAG, 0),
                    LLVMConstInt(i64, 32, 0), "objf.tag32"),
                LLVMBuildAnd(g->builder, len64,
                    LLVMConstInt(i64, ZAN_STR_LEN_MASK, 0), "objf.len32"),
                "objf.hdrw");
            LLVMValueRef sm8 = LLVMBuildGEP2(g->builder, i8, s, &neg8, 1,
                                             "objf.sm8");
            LLVMBuildStore(g->builder, hdrw,
                LLVMBuildBitCast(g->builder, sm8, LLVMPointerType(i64, 0),
                                 "objf.smp"));
            phi_vals[np] = s;
            phi_bbs[np] = def_name_bb;
            np++;
            LLVMBuildBr(g->builder, merge);
        } else if (fam == 1) {
            LLVMValueRef a8 = aval;
            LLVMValueRef r8 = recv;
            if (LLVMTypeOf(a8) != i8ptr)
                a8 = LLVMBuildBitCast(g->builder, a8, i8ptr, "objf.a8");
            if (LLVMTypeOf(r8) != i8ptr)
                r8 = LLVMBuildBitCast(g->builder, r8, i8ptr, "objf.r8");
            phi_vals[np] = zan_icmp(g->builder, LLVMIntEQ, r8, a8,
                                    "objf.req");
            phi_bbs[np] = def_bb;
            np++;
            LLVMBuildBr(g->builder, merge);
        } else {
            LLVMValueRef pi = LLVMBuildPtrToInt(g->builder, recv, i64,
                                                "objf.pi");
            LLVMValueRef sh = LLVMBuildLShr(g->builder, pi,
                LLVMConstInt(i64, 4, 0), "objf.sh");
            LLVMValueRef mix = LLVMBuildMul(g->builder, sh,
                LLVMConstInt(i64, UINT64_C(0x9E3779B9), 0), "objf.mix");
            phi_vals[np] = LLVMBuildTrunc(g->builder, mix, i32, "objf.mix32");
            phi_bbs[np] = def_bb;
            np++;
            LLVMBuildBr(g->builder, merge);
        }

        LLVMPositionBuilderAtEnd(g->builder, merge);
        LLVMValueRef phi = LLVMBuildPhi(g->builder, res_ty, "objf.res");
        LLVMAddIncoming(phi, phi_vals, phi_bbs, (unsigned)np);
        result = phi;
        free(phi_vals);
        free(phi_bbs);
    }
    free(cands);

    for (int k = 0; k < n_eh_args; k++)
        emit_eh_tmp_pop(g);
    if (fam == 1)
        emit_release_owned_call_temp(g, expr->call.args.items[0], aval,
                                     locals);
    emit_release_owned_call_temp(g, callee->member.object, recv, locals);
    return result;
}
