LLVMValueRef emit_user_conversion(zan_irgen_t *g, zan_type_t *from_type,
                                         zan_type_t *to_type, const char *op_name,
                                         LLVMValueRef val, zan_ast_node_t *val_expr,
                                         local_scope_t *locals) {
    zan_symbol_t *op = find_user_conversion(g, from_type, to_type, op_name);
    if (!op) return val;
    int fi = irgen_find_function(g, op);
    if (fi < 0) return val;
    LLVMTypeRef mft = g->functions[fi].fn_type;
    LLVMValueRef mfn = route_generic_method(g, from_type, op,
        g->functions[fi].fn, mft, &mft);
    LLVMValueRef arg = val;
    zan_type_t *p0 = method_param_type_at(g, op, 0, val_expr, val_expr, locals);
    if (p0) {
        LLVMTypeRef p0t = map_type(g, p0);
        if (LLVMTypeOf(arg) != p0t) arg = coerce_int_to(g, arg, p0t);
    }
    return zan_call2(g->builder, mft, mfn, &arg, 1, "uc");
}

/* (nint)SomeMethod 原生回调函数指针转换 */
static LLVMValueRef emit_raw_fn_for_cb_cast(zan_irgen_t *g, zan_ast_node_t *e,
                                            local_scope_t *locals) {
    zan_symbol_t *msym = NULL;
    if (e->kind == AST_IDENTIFIER && g->current_type_sym) {
        msym = get_method_sym(g->current_type_sym, e->ident.name);
    } else if (e->kind == AST_MEMBER_ACCESS &&
               e->member.object->kind == AST_IDENTIFIER &&
               !local_find(locals, e->member.object->ident.name)) {
        zan_symbol_t *cs = zan_binder_lookup(g->binder, e->member.object->ident.name);
        if (cs && (cs->kind == SYM_CLASS || cs->kind == SYM_STRUCT))
            msym = get_method_sym(cs, e->member.name);
    }
    if (msym && msym->decl && msym->decl->kind == AST_METHOD_DECL &&
        (msym->decl->method_decl.modifiers & MOD_STATIC) != 0) {
        for (int fi = irgen_find_function(g, msym); fi >= 0; fi = -1)
            if (g->functions[fi].sym == msym)
                return g->functions[fi].fn;
    }
    if (e->kind == AST_IDENTIFIER && !local_find(locals, e->ident.name)) {
        char nbuf[256];
        size_t nl = e->ident.name.len < 255 ? e->ident.name.len : 255;
        memcpy(nbuf, e->ident.name.str, nl);
        nbuf[nl] = '\0';
        LLVMValueRef gfn = LLVMGetNamedFunction(g->mod, nbuf);
        if (gfn && LLVMIsAFunction(gfn)) return gfn;
    }
    return NULL;
}

/* (nint)Method 回调函数指针跨语言传递给宿主 C 代码 */
static LLVMValueRef emit_wasm_cb_thunk(zan_irgen_t *g, LLVMValueRef fn) {
    char name[300];
    snprintf(name, sizeof(name), "__zan_cb_thunk.%s", LLVMGetValueName(fn));
    LLVMValueRef existing = LLVMGetNamedFunction(g->mod, name);
    if (existing && LLVMIsAFunction(existing)) return existing;
    LLVMTypeRef i32T = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i64T = LLVMInt64TypeInContext(g->ctx);
    unsigned nparams = LLVMCountParams(fn);
    LLVMTypeRef *param_types = (LLVMTypeRef *)malloc((nparams > 0 ? nparams : 1) * sizeof(LLVMTypeRef));
    for (unsigned i = 0; i < nparams; i++) {
        param_types[i] = i32T;
    }
    LLVMTypeRef ret_ty = LLVMGetReturnType(LLVMGlobalGetValueType(fn));
    LLVMTypeRef thunk_ty = LLVMFunctionType(ret_ty, param_types, nparams, 0);
    free(param_types);

    LLVMValueRef thunk = LLVMAddFunction(g->mod, name, thunk_ty);
    LLVMBasicBlockRef saved_bb = LLVMGetInsertBlock(g->builder);
    LLVMBasicBlockRef bb = LLVMAppendBasicBlockInContext(g->ctx, thunk, "entry");
    LLVMPositionBuilderAtEnd(g->builder, bb);

    LLVMTypeRef fn_ty = LLVMGlobalGetValueType(fn);
    LLVMValueRef *call_args = NULL;
    if (nparams > 0) {
        call_args = (LLVMValueRef *)malloc(nparams * sizeof(LLVMValueRef));
        for (unsigned i = 0; i < nparams; i++) {
            LLVMValueRef p = LLVMGetParam(thunk, i);
            LLVMTypeRef target_param_ty = LLVMTypeOf(LLVMGetParam(fn, i));
            if (LLVMGetTypeKind(target_param_ty) == LLVMIntegerTypeKind) {
                unsigned w = LLVMGetIntTypeWidth(target_param_ty);
                if (w == 64) {
                    call_args[i] = LLVMBuildZExt(g->builder, p, i64T, "arg64");
                } else if (w == 32) {
                    call_args[i] = p;
                } else if (w < 32) {
                    call_args[i] = LLVMBuildTrunc(g->builder, p, target_param_ty, "argtrunc");
                } else {
                    call_args[i] = LLVMBuildZExt(g->builder, p, target_param_ty, "argext");
                }
            } else if (LLVMGetTypeKind(target_param_ty) == LLVMPointerTypeKind) {
                call_args[i] = LLVMBuildIntToPtr(g->builder, p, target_param_ty, "argptr");
            } else {
                call_args[i] = p;
            }
        }
    }
    LLVMValueRef ret_val = LLVMBuildCall2(g->builder, fn_ty, fn, call_args, nparams, "");
    if (call_args) free(call_args);

    if (LLVMGetTypeKind(ret_ty) == LLVMVoidTypeKind) {
        LLVMBuildRetVoid(g->builder);
    } else {
        LLVMBuildRet(g->builder, ret_val);
    }
    LLVMPositionBuilderAtEnd(g->builder, saved_bb);
    return thunk;
}
int type_is_or_derives(zan_type_t *s, zan_type_t *t);
LLVMValueRef emit_runtime_is_check_name(zan_irgen_t *g, LLVMValueRef x,
                                               zan_type_t *tt);
LLVMValueRef emit_runtime_is_check_name(zan_irgen_t *g, LLVMValueRef x,
                                               zan_type_t *tt);

/* A454 装箱三助手（定义在 emit_runtime_is_check_name 之后） */
bool irgen_boxed_eligible_struct(zan_irgen_t *g, zan_type_t *t);
LLVMValueRef emit_box_struct(zan_irgen_t *g, zan_type_t *st,
                                    zan_ast_node_t *src_expr,
                                    LLVMValueRef val, local_scope_t *locals);
static LLVMValueRef emit_unbox_struct(zan_irgen_t *g, zan_type_t *tt,
                                      LLVMValueRef val, zan_ast_node_t *expr);
LLVMValueRef emit_expr_cast_expr(zan_irgen_t *g, zan_ast_node_t *expr,
        local_scope_t *locals) {
        zan_type_t *tt0 = resolve_type_ctx(g, expr->cast.type);
        /* 传递裸函数指针供外部原生代码调用 */
        if (tt0 && expr->cast.expr &&
            (expr->cast.expr->kind == AST_IDENTIFIER ||
             expr->cast.expr->kind == AST_MEMBER_ACCESS) &&
            tt0->kind != TYPE_DELEGATE) {
            LLVMTypeRef tgt0 = map_type(g, tt0);
            if (LLVMGetTypeKind(tgt0) == LLVMIntegerTypeKind) {
                LLVMValueRef raw = emit_raw_fn_for_cb_cast(g, expr->cast.expr, locals);
                if (raw) {
                    if (target_is_wasm32(g))
                        raw = emit_wasm_cb_thunk(g, raw);
                    return LLVMBuildPtrToInt(g->builder, raw, tgt0, "cast.fn");
                }
            }
        }
        LLVMValueRef val = emit_expr(g, expr->cast.expr, locals);
        zan_type_t *tt = resolve_type_ctx(g, expr->cast.type);
        LLVMTypeRef target = tt ? map_type(g, tt) : LLVMInt64TypeInContext(g->ctx);
        LLVMTypeRef src = LLVMTypeOf(val);
        {
            zan_type_t *st = infer_expr_type(g, expr->cast.expr, locals);
            if (st && find_user_conversion(g, st, tt, "op_explicit"))
                return emit_user_conversion(g, st, tt, "op_explicit", val,
                                            expr->cast.expr, locals);
            /* `(object)` 对值形状的分流（A454）：无托管字段的 struct 走真
             * 装箱（分配带头部堆对象、写载荷、site 注册三件事都在
             * emit_box_struct）；其余值形状仍报错兜住泛型特化（类型参数
             * 在 checker 阶段还看不出来）——硬转只会把整数值当成对象指针
             * （下面走 IntToPtr），运行期拿野指针、退出时 ARC 回收再崩。 */
            if (tt && tt->kind == TYPE_OBJECT && st) {
                if (irgen_boxed_eligible_struct(g, st))
                    return emit_box_struct(g, st, expr, val, locals);
                if (irgen_type_is_value_kind(st))
                    zan_diag_emit_code(g->diag, DIAG_ERROR, expr->loc, 3040,
                              "cannot convert '%s' to 'object': value types "
                              "cannot be boxed (Zan has no boxing facility)",
                              st->name.str ? st->name.str : "?");
            }
            /* 拆箱（A454）：object → 装箱过的结构体。来源不限于 OBJECT
             * 形状（string / 接口 / 引用类都可能是箱的实际内容）；tag
             * 检查走 `is` 的描述符名表通路（对 string magic 亦正确），
             * 失败按 A509 软失败（stderr + 零值继续）。 */
            if (tt && tt->kind == TYPE_STRUCT && st &&
                st->kind != TYPE_STRUCT && st->kind != TYPE_NULLABLE &&
                LLVMGetTypeKind(src) == LLVMPointerTypeKind &&
                irgen_boxed_eligible_struct(g, tt))
                return emit_unbox_struct(g, tt, val, expr);
        }
        if (llvm_is_nullable(src) && !llvm_is_nullable(target)) {
            emit_runtime_check(g,
                LLVMBuildNot(g->builder, nullable_has_value(g, val), "nv.novalue"),
                expr->loc, "Nullable object must have a value");
            val = nullable_get_payload(g, val);
            src = LLVMTypeOf(val);
        } else if (llvm_is_nullable(target) && src != target) {
            return coerce_int_to(g, val, target);
        }
        /* 无符号整型目标转换保持 64 位寄存器表示 */
        if (tt && LLVMGetTypeKind(src) == LLVMIntegerTypeKind &&
            LLVMGetIntTypeWidth(src) < 64 &&
            (tt->kind == TYPE_UINT || tt->kind == TYPE_USHORT ||
             tt->kind == TYPE_SBYTE || tt->kind == TYPE_ULONG)) {
            val = zan_iwiden(g->builder, val, LLVMInt64TypeInContext(g->ctx));
            src = LLVMTypeOf(val);
        }
        if (tt && LLVMGetTypeKind(src) == LLVMIntegerTypeKind &&
            LLVMGetIntTypeWidth(src) == 64) {
            LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
            switch (tt->kind) {
            case TYPE_UINT:
                return zan_and(g->builder, val,
                    LLVMConstInt(i64t, 0xFFFFFFFFull, 0), "cast.u32");
            case TYPE_USHORT:
                return zan_and(g->builder, val,
                    LLVMConstInt(i64t, 0xFFFFull, 0), "cast.u16");
            case TYPE_SBYTE: {
                LLVMValueRef sh = LLVMConstInt(i64t, 56, 0);
                LLVMValueRef up = zan_shl(g->builder, val, sh, "cast.i8.l");
                return zan_ashr(g->builder, up, sh, "cast.i8");
            }
            case TYPE_ULONG:
                return val;
            default:
                break;
            }
        }
        /* 引用下转的运行时类型检查（TASKS.md A509）：引用→引用曾直接返回
         * 原值——失败的下转把 string 指针当 User 用（静默类型混淆）。
         * 必须放在 `src == target` 早退之前：LLVM 不透明指针下所有指针
         * 同型（ptr），那道等值检查对引用恒真。
         * C# 抛 InvalidCastException（可 catch）；catchable 通道落地前
         * （A507 族）用软诊断 + 安全值 null：stderr 一次、值变 null、继续，
         * --strict-runtime 下 exit 70。静态可证安全的上转/同型/object 不查。 */
        if (LLVMGetTypeKind(src) == LLVMPointerTypeKind &&
            LLVMGetTypeKind(target) == LLVMPointerTypeKind && tt) {
            bool ref_target = tt->kind == TYPE_CLASS ||
                tt->kind == TYPE_INTERFACE || tt->kind == TYPE_STRING ||
                tt->kind == TYPE_ARRAY || tt->kind == TYPE_OBJECT;
            if (ref_target) {
                zan_type_t *st2 = infer_expr_type(g, expr->cast.expr, locals);
                bool static_ok = !st2 || st2->kind == TYPE_ERROR ||
                    types_equal(st2, tt) || type_is_or_derives(st2, tt) ||
                    tt->kind == TYPE_OBJECT;
                if (!static_ok) {
                    LLVMValueRef cfn = LLVMGetBasicBlockParent(
                        LLVMGetInsertBlock(g->builder));
                    LLVMValueRef ok = emit_runtime_is_check_name(g, val, tt);
                    LLVMBasicBlockRef cast_ok = LLVMGetInsertBlock(g->builder);
                    LLVMBasicBlockRef cast_bad = LLVMAppendBasicBlockInContext(
                        g->ctx, cfn, "cast.bad");
                    LLVMBasicBlockRef cast_m = LLVMAppendBasicBlockInContext(
                        g->ctx, cfn, "cast.m");
                    LLVMBuildCondBr(g->builder, ok, cast_m, cast_bad);
                    /* B-ID163③：失败一律抛 InvalidCastException（C# 同语义：
                     * try 内可 catch，try 外未捕获终止进程）。旧实现 try 外
                     * 走 A509 软失败——stderr 一次后拿 null 继续跑，正是
                     * "强转失败静默给默认值"缺陷本体。cast_m 现在只有
                     * cast_ok 单前驱，直接返回 val，不再需要 phi。 */
                    LLVMPositionBuilderAtEnd(g->builder, cast_bad);
                    emit_cast_throw(g, expr->loc,
                        "InvalidCastException: object is not an instance of the target type");
                    if (!LLVMGetBasicBlockTerminator(
                            LLVMGetInsertBlock(g->builder)))
                        LLVMBuildUnreachable(g->builder);
                    LLVMPositionBuilderAtEnd(g->builder, cast_m);
                    return val;
                }
            }
        }

        if (src == target) return val;

        /* decimal 显式强转支持（整型与浮点型互转） */
        if (target == get_decimal_type(g)) {
            if (LLVMGetTypeKind(src) == LLVMIntegerTypeKind) {
                return coerce_to_decimal(g, val);
            }
            if (LLVMGetTypeKind(src) == LLVMDoubleTypeKind ||
                LLVMGetTypeKind(src) == LLVMFloatTypeKind) {
                if (LLVMGetTypeKind(src) == LLVMFloatTypeKind) {
                    val = LLVMBuildFPExt(g->builder, val, LLVMDoubleTypeInContext(g->ctx), "dec.fpext");
                }
                LLVMValueRef slot = emit_entry_alloca(g, target, "dec.fslot");
                LLVMTypeRef ptr_ty = LLVMPointerType(target, 0);
                LLVMTypeRef fn_ty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx),
                    (LLVMTypeRef[]){ LLVMDoubleTypeInContext(g->ctx), ptr_ty }, 2, 0);
                LLVMValueRef fn = get_libc_fn(g, "__zan_decimal_from_double", fn_ty);
                zan_call2(g->builder, fn_ty, fn, (LLVMValueRef[]){ val, slot }, 2, "");
                return LLVMBuildLoad2(g->builder, target, slot, "dec.fval");
            }
        }
        if (src == get_decimal_type(g)) {
            if (LLVMGetTypeKind(target) == LLVMIntegerTypeKind) {
                LLVMValueRef slot = emit_entry_alloca(g, src, "dec.tslot");
                LLVMBuildStore(g->builder, val, slot);
                LLVMTypeRef ptr_ty = LLVMPointerType(src, 0);
                LLVMTypeRef fn_ty = LLVMFunctionType(LLVMInt64TypeInContext(g->ctx),
                    (LLVMTypeRef[]){ ptr_ty }, 1, 0);
                LLVMValueRef fn = get_libc_fn(g, "__zan_decimal_to_int64", fn_ty);
                LLVMValueRef i64_val = zan_call2(g->builder, fn_ty, fn, (LLVMValueRef[]){ slot }, 1, "dec.i64");
                if (LLVMGetIntTypeWidth(target) < 64) {
                    return LLVMBuildTrunc(g->builder, i64_val, target, "dec.trunc");
                }
                return i64_val;
            }
            if (LLVMGetTypeKind(target) == LLVMDoubleTypeKind ||
                LLVMGetTypeKind(target) == LLVMFloatTypeKind) {
                LLVMValueRef slot = emit_entry_alloca(g, src, "dec.tslot");
                LLVMBuildStore(g->builder, val, slot);
                LLVMTypeRef ptr_ty = LLVMPointerType(src, 0);
                LLVMTypeRef fn_ty = LLVMFunctionType(LLVMDoubleTypeInContext(g->ctx),
                    (LLVMTypeRef[]){ ptr_ty }, 1, 0);
                LLVMValueRef fn = get_libc_fn(g, "__zan_decimal_to_double", fn_ty);
                LLVMValueRef dbl = zan_call2(g->builder, fn_ty, fn, (LLVMValueRef[]){ slot }, 1, "dec.dbl");
                if (LLVMGetTypeKind(target) == LLVMFloatTypeKind) {
                    return LLVMBuildFPTrunc(g->builder, dbl, target, "dec.flt");
                }
                return dbl;
            }
        }

        LLVMTypeKind sk = LLVMGetTypeKind(src);
        LLVMTypeKind tk = LLVMGetTypeKind(target);
        /* 地址与委托互转：(WndProc)addr 动态转换运行时回调 */
        if (sk == LLVMIntegerTypeKind && tk == LLVMPointerTypeKind)
            return LLVMBuildIntToPtr(g->builder, val, target, "cast.p");
        if (sk == LLVMPointerTypeKind && tk == LLVMIntegerTypeKind)
            return LLVMBuildPtrToInt(g->builder, val, target, "cast.a");
        bool src_fp = (sk == LLVMDoubleTypeKind || sk == LLVMFloatTypeKind);
        bool tgt_fp = (tk == LLVMDoubleTypeKind || tk == LLVMFloatTypeKind);
        if (sk == LLVMIntegerTypeKind && tk == LLVMIntegerTypeKind) {
            unsigned sw = LLVMGetIntTypeWidth(src);
            unsigned tw = LLVMGetIntTypeWidth(target);
            if (tw < sw) {
                LLVMValueRef narrow = LLVMBuildTrunc(g->builder, val, target, "cast");
                if (g->irgen_checked_depth > 0 && tw > 1) {
                    /* checked 下窄化强转越界抛溢出（B-ID76）：按目标符号性回拓宽，
                     * 与原值不等即丢位。 */
                    bool tgt_uns = tt && (tt->kind == TYPE_BYTE || tt->kind == TYPE_USHORT ||
                                          tt->kind == TYPE_UINT || tt->kind == TYPE_ULONG ||
                                          tt->kind == TYPE_CHAR);
                    LLVMValueRef back = tgt_uns
                        ? LLVMBuildZExt(g->builder, narrow, src, "ck.cast.zx")
                        : LLVMBuildSExt(g->builder, narrow, src, "ck.cast.sx");
                    LLVMValueRef ovf = zan_icmp(g->builder, LLVMIntNE, back, val, "ck.cast.ovf");
                    emit_runtime_check(g, ovf, expr->loc,
                        "integer overflow in checked operation");
                }
                return narrow;
            }
            if (tw > sw) return zan_iwiden(g->builder, val, target);
            return val;
        }
        if (src_fp && tk == LLVMIntegerTypeKind)
            return emit_fptosi_guarded(g, val, target, expr->loc, NULL);
        if (sk == LLVMIntegerTypeKind && tgt_fp)
            return LLVMBuildSIToFP(g->builder, val, target, "cast");
        if (src_fp && tgt_fp) {
            if (sk == LLVMDoubleTypeKind && tk == LLVMFloatTypeKind)
                return LLVMBuildFPTrunc(g->builder, val, target, "cast");
            if (sk == LLVMFloatTypeKind && tk == LLVMDoubleTypeKind)
                return LLVMBuildFPExt(g->builder, val, target, "cast");
        }
        return val;
    return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
}
int type_is_or_derives(zan_type_t *s, zan_type_t *t) {
    if (!s || !t) return 0;
    if (types_equal(s, t)) return 1;
    if (s->kind != TYPE_CLASS || t->kind != TYPE_CLASS) return 0;
    zan_symbol_t *cur = s->sym;
    while (cur && cur->type && cur->type->base_type &&
           cur->type->base_type->sym) {
        cur = cur->type->base_type->sym;
        if (cur == t->sym) return 1;
    }
    return 0;
}

/* 运行时 x is T 严格祖先类型继承检查 */
LLVMValueRef emit_runtime_is_check(zan_irgen_t *g, LLVMValueRef x,
                                          const char *tname);

/* 运行时 x is T 类型检查辅助封装 */
LLVMValueRef emit_runtime_is_check_name(zan_irgen_t *g, LLVMValueRef x,
                                               zan_type_t *tt) {
    char tbuf[320];
    const char *ns = NULL;
    int nlen = 0;
    if (tt && tt->name.str) {
        ns = tt->name.str;
        nlen = (int)tt->name.len;
    } else if (tt && tt->sym && tt->sym->name.str) {
        ns = tt->sym->name.str;
        nlen = (int)tt->sym->name.len;
    }
    if (nlen > 319) nlen = 319;
    if (nlen > 0) memcpy(tbuf, ns, (size_t)nlen);
    tbuf[nlen] = 0;
    return emit_runtime_is_check(g, x, tbuf);
}
LLVMValueRef emit_runtime_is_check(zan_irgen_t *g, LLVMValueRef x,
                                          const char *tname) {
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i1 = LLVMInt1TypeInContext(g->ctx);
    if (!g->desc_hdr && !g->g_site_tynames) return LLVMConstInt(i1, 0, 0);
    LLVMValueRef fn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder));
    LLVMBasicBlockRef cur = LLVMGetInsertBlock(g->builder);
    LLVMBasicBlockRef false_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "is.f");
    LLVMBasicBlockRef true_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "is.t");
    LLVMBasicBlockRef merge = LLVMAppendBasicBlockInContext(g->ctx, fn, "is.m");
    LLVMBasicBlockRef head = LLVMAppendBasicBlockInContext(g->ctx, fn, "is.h");
    LLVMBasicBlockRef str_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "is.s");
    LLVMBasicBlockRef oob_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "is.o");
    LLVMBasicBlockRef loop = LLVMAppendBasicBlockInContext(g->ctx, fn, "is.l");
    LLVMBasicBlockRef body = LLVMAppendBasicBlockInContext(g->ctx, fn, "is.b");
    LLVMBasicBlockRef next = LLVMAppendBasicBlockInContext(g->ctx, fn, "is.n");
    (void)cur;

    LLVMValueRef isnull = zan_icmp(g->builder, LLVMIntEQ, x,
        LLVMConstNull(LLVMTypeOf(x)), "is.nul");
    LLVMBuildCondBr(g->builder, isnull, false_bb, head);

    /* 对象头部 -8 处分配点标识字（支持字符串/对象运行时类型识别） */
    LLVMPositionBuilderAtEnd(g->builder, head);
    LLVMValueRef tstr = zan_irgen_intern_string(g, tname);
    LLVMValueRef neg8 = LLVMConstInt(i64, (uint64_t)ZAN_OBJ_SITE_OFF, 1);
    LLVMValueRef sptr = LLVMBuildGEP2(g->builder, LLVMInt8TypeInContext(g->ctx),
        x, &neg8, 1, "is.sptr");
    LLVMValueRef site = LLVMBuildLoad2(g->builder, i64,
        LLVMBuildBitCast(g->builder, sptr, LLVMPointerType(i64, 0), "is.sp"),
        "is.site");
    LLVMValueRef isstr = zan_hdr_is_string(g, site, "is.str");
    LLVMBuildCondBr(g->builder, isstr, str_bb, oob_bb);

    LLVMPositionBuilderAtEnd(g->builder, str_bb);
    LLVMValueRef ststr = zan_irgen_intern_string(g, "string");
    LLVMTypeRef sstrcmp_ty = LLVMFunctionType(i32,
        (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0);
    LLVMValueRef sstrcmp_fn = get_libc_fn(g, "strcmp", sstrcmp_ty);
    LLVMValueRef scmp = zan_call2(g->builder, sstrcmp_ty, sstrcmp_fn,
        (LLVMValueRef[]){ ststr, tstr }, 2, "is.scmp");
    LLVMValueRef seq = zan_icmp(g->builder, LLVMIntEQ, scmp,
        LLVMConstInt(i32, 0, 0), "is.seq");
    LLVMBuildCondBr(g->builder, seq, true_bb, false_bb);

    /* 越界分配点索引判定为非目标类型 */
    LLVMPositionBuilderAtEnd(g->builder, oob_bb);
    LLVMValueRef oob;
    if (!g->desc_hdr) {
        LLVMValueRef bound = LLVMBuildLoad2(g->builder, i64,
            g->g_site_count, "is.bound");
        oob = zan_or(g->builder,
            zan_icmp(g->builder, LLVMIntSLT, site, LLVMConstInt(i64, 0, 0), "is.neg"),
            zan_icmp(g->builder, LLVMIntSGE, site, bound, "is.oob"),
            "is.oob2");
    } else {
        /* 描述符模式：头部字作为元数据记录指针 */
        oob = zan_or(g->builder,
            zan_or(g->builder,
                zan_icmp(g->builder, LLVMIntEQ, site, LLVMConstInt(i64, 0, 0),
                         "is.nodsc"),
                zan_icmp(g->builder, LLVMIntEQ, site,
                         LLVMConstInt(i64, ZAN_ARRAY_MAGIC, 0), "is.arrdsc"),
                "is.notdsc"),
            zan_icmp(g->builder, LLVMIntULT, site, LLVMConstInt(i64, 4096, 0),
                     "is.tinydsc"),
            "is.oob2");
    }
    LLVMBuildCondBr(g->builder, oob, false_bb, loop);

    LLVMPositionBuilderAtEnd(g->builder, loop);
    LLVMValueRef idx = LLVMBuildPhi(g->builder, i64, "is.i");
    LLVMValueRef list;
    if (!g->desc_hdr) {
        LLVMValueRef tbl = LLVMBuildLoad2(g->builder,
            LLVMPointerType(i8ptr, 0), g->g_site_tynames, "is.tbl");
        list = LLVMBuildLoad2(g->builder, i8ptr,
            LLVMBuildGEP2(g->builder, i8ptr, tbl, &site, 1, "is.lp"),
            "is.list");
    } else {
        LLVMValueRef dp = LLVMBuildIntToPtr(g->builder, site,
            LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0), "is.dsc");
        LLVMValueRef tn_off = LLVMConstInt(i64, 8, 0);
        LLVMValueRef tn_p = LLVMBuildGEP2(g->builder,
            LLVMInt8TypeInContext(g->ctx), dp, &tn_off, 1, "is.tnp");
        list = LLVMBuildLoad2(g->builder, i8ptr, tn_p, "is.list");
    }
    LLVMValueRef lnull = zan_icmp(g->builder, LLVMIntEQ, list,
        LLVMConstNull(i8ptr), "is.ln");
    LLVMBuildCondBr(g->builder, lnull, false_bb, body);

    LLVMPositionBuilderAtEnd(g->builder, body);
    LLVMValueRef name_p = LLVMBuildGEP2(g->builder, i8ptr, list, &idx, 1, "is.np");
    LLVMValueRef name = LLVMBuildLoad2(g->builder, i8ptr, name_p, "is.name");
    LLVMValueRef nnull = zan_icmp(g->builder, LLVMIntEQ, name,
        LLVMConstNull(i8ptr), "is.nn");
    LLVMBuildCondBr(g->builder, nnull, false_bb, next);

    LLVMPositionBuilderAtEnd(g->builder, next);
    LLVMTypeRef strcmp_ty = LLVMFunctionType(i32,
        (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0);
    LLVMValueRef strcmp_fn = get_libc_fn(g, "strcmp", strcmp_ty);
    LLVMValueRef cmp = zan_call2(g->builder, strcmp_ty, strcmp_fn,
        (LLVMValueRef[]){ name, tstr }, 2, "is.cmp");
    LLVMValueRef eq = zan_icmp(g->builder, LLVMIntEQ, cmp,
        LLVMConstInt(i32, 0, 0), "is.eq");
    LLVMValueRef idx2 = zan_add(g->builder, idx, LLVMConstInt(i64, 1, 0), "is.i2");
    LLVMBuildCondBr(g->builder, eq, true_bb, loop);
    LLVMAddIncoming(idx, (LLVMValueRef[]){ LLVMConstInt(i64, 0, 0), idx2 },
                    (LLVMBasicBlockRef[]){ oob_bb, next }, 2);

    LLVMPositionBuilderAtEnd(g->builder, true_bb);
    LLVMBuildBr(g->builder, merge);
    LLVMPositionBuilderAtEnd(g->builder, false_bb);
    LLVMBuildBr(g->builder, merge);
    LLVMPositionBuilderAtEnd(g->builder, merge);
    LLVMValueRef res = LLVMBuildPhi(g->builder, i1, "is.res");
    LLVMAddIncoming(res, (LLVMValueRef[]){ LLVMConstInt(i1, 1, 0),
                                           LLVMConstInt(i1, 0, 0) },
                    (LLVMBasicBlockRef[]){ true_bb, false_bb }, 2);
    return res;
}

/* A454 装箱资格：用户声明的 struct、无显式布局、字段（含嵌套）不含
 * 托管引用。判定与 checker 侧 checker_struct_boxable 同口径——irgen 侧
 * 复用现成的 type_contains_collection_rc（含嵌套展开与 weak 跳过）。 */
bool irgen_boxed_eligible_struct(zan_irgen_t *g, zan_type_t *t) {
    if (!t || t->kind != TYPE_STRUCT || !t->sym) return false;
    zan_symbol_t *sym = t->sym;
    if (!sym->decl || sym->decl->kind != AST_STRUCT_DECL) return false;
    if (decl_is_explicit_layout(sym)) return false;
    return true;
}

/* 装箱：值结构体 → 堆对象。布局契约（A556-2 基座）：首字 = 结构体
 * vtable 全局地址——接口分派按接收者首字做身份 tag 比对（类对象是
 * vptr，箱此前没有这个字，tag 是载荷前 8 字节当指针，命中不了任何
 * 候选）；载荷随之后移到 +8，拆箱与箱释放函数按 +8 取载荷
 * （emit_unbox_struct / build_class_release_body 的 SYM_STRUCT 分支）。
 * `is`/拆箱的 tag 走描述符名表（装箱 site 的 tynames 首项就是结构体名，
 * 见 zan_irgen_emit_arc_desc_init 的继承链遍历），基于头部不受载荷
 * 位移影响。头部由 zan_rt_alloc 统一布置：raw[0..7]=引用计数、
 * raw[8..15]=site 字。托管字段结构体（A556-3）同一协议：来源非 owned
 * 时对托管字段 retain，箱的释放函数按字段 walk 释放（含嵌套结构体
 * 下钻）。 */
LLVMValueRef emit_box_struct(zan_irgen_t *g, zan_type_t *st,
                                    zan_ast_node_t *src_expr,
                                    LLVMValueRef val, local_scope_t *locals) {
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
    LLVMTypeRef lt = map_type(g, st);
    LLVMValueRef sval = coerce_struct_value(g, val, st);
    if (!lt || LLVMGetTypeKind(lt) != LLVMStructTypeKind ||
        LLVMGetTypeKind(LLVMTypeOf(sval)) != LLVMStructTypeKind) {
        zan_diag_emit_code(g->diag, DIAG_ERROR, src_expr->loc, 3040,
                      "cannot convert '%s' to 'object': value types "
                      "cannot be boxed (Zan has no boxing facility)",
                      st->name.str ? st->name.str : "?");
        return LLVMConstNull(i8ptr);
    }
    int site = reserve_arc_site(g, st->sym, st, 0, NULL);
    char nbuf[320];
    snprintf(nbuf, sizeof(nbuf), "box:%.*s",
             (int)st->name.len, st->name.str ? st->name.str : "?");
    LLVMTypeRef alloc_ty = LLVMGlobalGetValueType(g->rt_alloc);
    LLVMValueRef sz8 = LLVMConstAdd(LLVMSizeOf(lt), LLVMConstInt(i64, 8, 0));
    LLVMValueRef raw = zan_call2(g->builder, alloc_ty, g->rt_alloc,
        (LLVMValueRef[]){ sz8, arc_site_arg(g, site),
                          zan_irgen_intern_string(g, nbuf) }, 3, "box.alloc");
    LLVMValueRef tag_slot = LLVMBuildBitCast(g->builder, raw,
        LLVMPointerType(i8ptr, 0), "box.vps");
    LLVMBuildStore(g->builder, get_vtable_global(g, st->sym), tag_slot);
    LLVMValueRef eight = LLVMConstInt(i64, 8, 0);
    /* 空载荷结构体（{}）跳过 store：零大小 store 非法 */
    if (LLVMCountStructElementTypes(lt) > 0) {
        LLVMValueRef p8 = LLVMBuildGEP2(g->builder, i8, raw, &eight, 1, "box.pl");
        LLVMValueRef slot = LLVMBuildBitCast(g->builder, p8,
            LLVMPointerType(lt, 0), "box.slot");
        LLVMBuildStore(g->builder, sval, slot);
        /* 载荷里的引用归堆对象所有；来源是既有变量（非构造临时）时
         * 补 +1——与 emit_struct_local_capture 同一捕获协议。注意这里
         * 判定的是**操作数**的 owned 性（装箱 cast 本身在 helper 里
         * 已定义为 owned，不能用它判字段来源）。 */
        zan_ast_node_t *operand = (src_expr && src_expr->kind == AST_CAST_EXPR)
                                      ? src_expr->cast.expr : src_expr;
        if (!expr_yields_owned_rc_value(g, operand, locals))
            emit_collection_value_retain(g, st, sval, 0);
    }
    return raw;
}

/* 拆箱：object → 装箱过的结构体。tag 检查复用 `is` 的描述符名表
 * 通路；失败按 A509 同款软失败（stderr 一次 + 零值继续，
 * --strict-runtime 下 exit 70）。C# 同款拷贝语义：载荷按值拷出，
 * 目的地（局部捕获/赋值协议）对托管字段 retain，箱保有自己的份。 */
static LLVMValueRef emit_unbox_struct(zan_irgen_t *g, zan_type_t *tt,
                                      LLVMValueRef val, zan_ast_node_t *expr) {
    LLVMTypeRef lt = map_type(g, tt);
    if (!lt || LLVMGetTypeKind(lt) != LLVMStructTypeKind) return val;
    LLVMValueRef ok = emit_runtime_is_check_name(g, val, tt);
    LLVMValueRef cfn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder));
    LLVMBasicBlockRef ok_bb = LLVMAppendBasicBlockInContext(g->ctx, cfn, "unbox.ok");
    LLVMBasicBlockRef bad_bb = LLVMAppendBasicBlockInContext(g->ctx, cfn, "unbox.bad");
    LLVMBasicBlockRef m_bb = LLVMAppendBasicBlockInContext(g->ctx, cfn, "unbox.m");
    LLVMBuildCondBr(g->builder, ok, ok_bb, bad_bb);
    LLVMPositionBuilderAtEnd(g->builder, ok_bb);
    /* A556-2：箱首字是 vtable tag，载荷在 +8（emit_box_struct 布局契约） */
    LLVMValueRef p8 = LLVMBuildGEP2(g->builder,
        LLVMInt8TypeInContext(g->ctx), val,
        (LLVMValueRef[]){ LLVMConstInt(LLVMInt64TypeInContext(g->ctx), 8, 0) },
        1, "unbox.p8");
    LLVMValueRef slot = LLVMBuildBitCast(g->builder, p8,
        LLVMPointerType(lt, 0), "unbox.p");
    LLVMValueRef good = LLVMBuildLoad2(g->builder, lt, slot, "unbox.v");
    LLVMBuildBr(g->builder, m_bb);
    LLVMPositionBuilderAtEnd(g->builder, bad_bb);
    /* B-ID163③：拆箱失败同强转口径——无条件抛 InvalidCastException，
     * 不再软失败拿零值结构体继续跑。m_bb 只剩 ok_bb 单前驱，无需 phi。 */
    emit_cast_throw(g, expr->loc,
        "InvalidCastException: object is not an instance of the target struct type");
    if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder)))
        LLVMBuildUnreachable(g->builder);
    LLVMPositionBuilderAtEnd(g->builder, m_bb);
    return good;
}

