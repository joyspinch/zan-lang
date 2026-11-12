        /* value */
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
                     rt_ty->kind == TYPE_DOUBLE || rt_ty->kind == TYPE_DECIMAL ||
                     rt_ty->kind == TYPE_CHAR ||
                     rt_ty->kind == TYPE_STRING || rt_ty->kind == TYPE_ENUM);
                if (handled_kind) {
                    LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
                    LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
                    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                    LLVMValueRef v = emit_expr(g, sc->member.object, locals);
                    LLVMTypeKind vk = LLVMGetTypeKind(LLVMTypeOf(v));
                    if (rt_ty->kind == TYPE_DECIMAL)
                        return emit_to_cstr(g, v);
                    if (rt_ty->kind == TYPE_BOOL && vk == LLVMIntegerTypeKind) {
                        /* 语义类型是 bool 就渲染成 "true"/"false"：泛型方法
                           返回值被提升成 i64 时也要认（按宽度判会渲染成 "1"） */
                        LLVMValueRef bc = emit_bool_cond(g, v,
                            sc->member.object, locals);
                        if (bc) return emit_bool_to_cstr(g, bc);
                    }
                    if (vk == LLVMPointerTypeKind) {
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
                    if (rt_ty->kind == TYPE_ENUM && rt_ty->sym &&
                        rt_ty->sym->member_count > 0) {
                        int cap = rt_ty->sym->member_count;
                        zan_symbol_t **mems = (zan_symbol_t **)calloc(
                            (size_t)cap, sizeof(*mems));
                        long long *vals = (long long *)calloc(
                            (size_t)cap, sizeof(*vals));
                        int n = irgen_enum_members(rt_ty->sym, mems, vals,
                                                   cap);
                        if (n == 0) {
                            free(mems);
                            free(vals);
                        }
                        if (n > 0) {
                            LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
                            LLVMValueRef v64 = emit_widen_i64_for_print(g, v);
                            LLVMValueRef fn = LLVMGetBasicBlockParent(
                                LLVMGetInsertBlock(g->builder));
                            LLVMBasicBlockRef joinbb =
                                LLVMAppendBasicBlockInContext(g->ctx, fn,
                                                              "em.join");
                            LLVMBasicBlockRef *hitbbs = (LLVMBasicBlockRef *)
                                calloc((size_t)n, sizeof(*hitbbs));
                            LLVMValueRef *lits = (LLVMValueRef *)calloc(
                                (size_t)n, sizeof(*lits));
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
                                /* 字面量在对应分支基本块内具化以保证支配 PHI 节点输入 */
                                LLVMPositionBuilderAtEnd(g->builder, hitbbs[i]);
                                lits[i] = emit_string_literal_rc(g,
                                    mems[i]->name);
                                LLVMBuildBr(g->builder, joinbb);
                                LLVMPositionBuilderAtEnd(g->builder, miss);
                                cur = miss;
                            }
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
                            free(hitbbs);
                            free(lits);
                            free(mems);
                            free(vals);
                            return phi;
                        }
                    }
                    if (rt_ty->kind == TYPE_CHAR &&
                        LLVMGetTypeKind(LLVMTypeOf(v)) == LLVMIntegerTypeKind) {
                        /* A523：char.ToString() 曾落通用 itoa 返回码点数字
                         * "65"（C# "A"）；WriteLine(char) 与拼接路径早就是
                         * 字符渲染——两条路对齐到同一个 UTF-8 编码帮手。 */
                        return emit_char_to_cstr(g, v);
                    }
                    LLVMValueRef buf = emit_string_alloc_rc(g,
                        LLVMConstInt(i64, 40, 0));
                    if (vk == LLVMDoubleTypeKind || vk == LLVMFloatTypeKind) {
                        emit_dbl_str(g, buf, LLVMConstInt(i64, 40, 0), v);
                        return buf;
                    }
                    emit_itoa_into(g, buf, emit_widen_i64_for_print(g, v),
                                   rt_ty->kind == TYPE_ULONG ? 1 : 0);
                    return buf;
                }
            }
        }

        /* A525 一件：标量 ToString("F2"/"D5"/"X4"/"E2"/"G3") 格式串——复用
         * 插值路径的 interp_format_to_printf（同一 spec→printf 映射，同一
         * 口径，含 D/X/F/E/G 与 0/# 形状）。数值接收者之外（bool/char/
         * 枚举的自定义格式）或不认识的说明符编译期显性拒绝，不静默。 */
        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS &&
            expr->call.args.count == 1 &&
            expr->call.args.items[0]->kind == AST_STRING_LITERAL) {
            zan_ast_node_t *scf = expr->call.callee;
            zan_istr_t smf = scf->member.name;
            if (smf.len == 8 && memcmp(smf.str, "ToString", 8) == 0) {
                zan_type_t *rf_ty = infer_expr_type(g, scf->member.object, locals);
                int numeric_kind = rf_ty &&
                    (rf_ty->kind == TYPE_BYTE ||
                     rf_ty->kind == TYPE_SHORT || rf_ty->kind == TYPE_INT ||
                     rf_ty->kind == TYPE_LONG || rf_ty->kind == TYPE_SBYTE ||
                     rf_ty->kind == TYPE_USHORT || rf_ty->kind == TYPE_UINT ||
                     rf_ty->kind == TYPE_ULONG || rf_ty->kind == TYPE_FLOAT ||
                     rf_ty->kind == TYPE_DOUBLE);
                if (numeric_kind) {
                    zan_istr_t spec = expr->call.args.items[0]->str_val;
                    bool want_float = false;
                    bool is_f = rf_ty->kind == TYPE_FLOAT ||
                                rf_ty->kind == TYPE_DOUBLE;
                    bool is_ul = rf_ty->kind == TYPE_ULONG;
                    char pfmt[64];
                    if (interp_format_to_printf(&spec, is_f, is_ul,
                                                &want_float, pfmt,
                                                sizeof(pfmt)) == 0) {
                        zan_diag_emit_code(g->diag, DIAG_ERROR, expr->loc, 3061,
                            "unsupported format specifier '%.*s' for numeric "
                            "ToString() (supported: D, X, F, E, G, 0/# shapes)",
                            (int)spec.len, spec.str);
                        return LLVMConstNull(LLVMPointerType(
                            LLVMInt8TypeInContext(g->ctx), 0));
                    }
                    LLVMValueRef vf = emit_expr(g, scf->member.object, locals);
                    LLVMValueRef arg = vf;
                    if (want_float) {
                        if (LLVMGetTypeKind(LLVMTypeOf(vf)) ==
                            LLVMFloatTypeKind)
                            arg = LLVMBuildFPExt(g->builder, vf,
                                                 LLVMDoubleTypeInContext(g->ctx),
                                                 "tos.f64");
                    } else {
                        arg = emit_widen_i64_for_print(g, vf);
                    }
                    LLVMTypeRef i8t = LLVMInt8TypeInContext(g->ctx);
                    LLVMTypeRef i8pt = LLVMPointerType(i8t, 0);
                    LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
                    LLVMValueRef pfmt_lit = zan_irgen_intern_string(g, pfmt);
                    LLVMValueRef buf = emit_string_alloc_rc(g,
                        LLVMConstInt(i64t, 640, 0));
                    LLVMValueRef snp_args[] = { buf,
                        LLVMConstInt(i64t, 640, 0), pfmt_lit, arg };
                    LLVMTypeRef snp_ty = LLVMFunctionType(i8pt,
                        (LLVMTypeRef[]){ i8pt, i64t, i8pt }, 3, 1);
                    zan_call2(g->builder, snp_ty, g->fn_snprintf, snp_args,
                              4, "");
                    return buf;
                }
            }
        }

        /* Environment */
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

        /* Environment */
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
            LLVMValueRef fopen_fn = LLVMGetNamedFunction(g->mod, "fopen");
            if (!fopen_fn) {
                LLVMTypeRef fopen_type = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0);
                fopen_fn = LLVMAddFunction(g->mod, "fopen", fopen_type);
            }
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
            LLVMValueRef mode = zan_irgen_intern_string(g, "rb");
            LLVMValueRef open_args[] = { path_arg, mode };
            LLVMValueRef fp = zan_call2(g->builder,
                LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0),
                fopen_fn, open_args, 2, "fp");
            emit_fopen_check(g, fp, "cannot read file\n");
            LLVMValueRef seek_end_args[] = { fp, LLVMConstInt(i64, 0, 0), LLVMConstInt(i32, 2, 0) };
            LLVMValueRef se_end = zan_call2(g->builder, LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr, i64, i32 }, 3, 0),
                fseek_fn, seek_end_args, 3, "se_end");
            emit_io_abort_if(g, zan_icmp(g->builder, LLVMIntNE, se_end, LLVMConstInt(i32, 0, 0), "se_end.err"),
                "cannot read file\n");
            LLVMValueRef size = zan_call2(g->builder,
                LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr }, 1, 0), ftell_fn, &fp, 1, "sz");
            emit_io_abort_if(g, zan_icmp(g->builder, LLVMIntSLT, size, LLVMConstInt(i64, 0, 0), "sz.err"),
                "cannot read file\n");
            LLVMValueRef seek_start_args[] = { fp, LLVMConstInt(i64, 0, 0), LLVMConstInt(i32, 0, 0) };
            LLVMValueRef se_start = zan_call2(g->builder, LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr, i64, i32 }, 3, 0),
                fseek_fn, seek_start_args, 3, "se_start");
            emit_io_abort_if(g, zan_icmp(g->builder, LLVMIntNE, se_start, LLVMConstInt(i32, 0, 0), "se_start.err"),
                "cannot read file\n");
            LLVMValueRef buf_size = zan_add(g->builder, size, LLVMConstInt(i64, 1, 0), "bsz");
            LLVMValueRef buf = emit_string_alloc_rc(g, buf_size);
            LLVMValueRef fread_args[] = { buf, LLVMConstInt(i64, 1, 0), size, fp };
            LLVMValueRef got = zan_call2(g->builder, LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr, i64, i64, i8ptr }, 4, 0),
                fread_fn, fread_args, 4, "got");
            emit_io_abort_if(g, zan_icmp(g->builder, LLVMIntNE, got, size, "got.err"),
                "cannot read file\n");
            LLVMValueRef end_ptr = LLVMBuildGEP2(g->builder, LLVMInt8TypeInContext(g->ctx), buf, &size, 1, "end");
            LLVMBuildStore(g->builder, LLVMConstInt(LLVMInt8TypeInContext(g->ctx), 0, 0), end_ptr);
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
            LLVMValueRef mode = zan_irgen_intern_string(g, "w");
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

        if (!zan_type_defines(g, "Path", "GetFileName") &&
            is_call_to(expr, "Path", "GetFileName") && expr->call.args.count == 1) {
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
            zan_ast_node_t *path_ast = expr->call.args.items[0];
            LLVMValueRef path_val = emit_expr(g, path_ast, locals);
            LLVMValueRef strrchr_fn = LLVMGetNamedFunction(g->mod, "strrchr");
            LLVMValueRef slash_args[] = { path_val, LLVMConstInt(i32t, '/', 0) };
            LLVMValueRef slash = zan_call2(g->builder,
                LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i32t }, 2, 0),
                strrchr_fn, slash_args, 2, "slash");
            LLVMValueRef bslash_args[] = { path_val, LLVMConstInt(i32t, 92, 0) }; /* 92 = backslash */
            LLVMValueRef bslash = zan_call2(g->builder,
                LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i32t }, 2, 0),
                strrchr_fn, bslash_args, 2, "bslash");
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
            LLVMTypeRef strlen_type = LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr }, 1, 0);
            LLVMValueRef len_a = zan_call2(g->builder, strlen_type, g->fn_strlen, &a, 1, "la");
            LLVMValueRef len_b = zan_call2(g->builder, strlen_type, g->fn_strlen, &b, 1, "lb");
            LLVMValueRef total = zan_add(g->builder, len_a, len_b, "t");
            total = zan_add(g->builder, total, LLVMConstInt(i64, 2, 0), "t2"); /* +separator+null */
            LLVMValueRef buf = emit_string_alloc_rc(g, total);
            LLVMValueRef strcpy_args[] = { buf, a };
            zan_call2(g->builder,
                LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0),
                g->fn_strcpy, strcpy_args, 2, "");
            LLVMValueRef sep = (g->target_is_windows ? zan_irgen_intern_string(g, "\\")
                                  : zan_irgen_intern_string(g, "/"));
            LLVMValueRef cat1_args[] = { buf, sep };
            zan_call2(g->builder,
                LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0),
                g->fn_strcat, cat1_args, 2, "");
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
            LLVMValueRef mode = zan_irgen_intern_string(g, "a");
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
            LLVMValueRef mode = zan_irgen_intern_string(g, "rb");
            LLVMValueRef open_args[] = { path_arg, mode };
            LLVMValueRef fp = zan_call2(g->builder,
                LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0),
                fopen_fn, open_args, 2, "fp");
            LLVMValueRef is_null = zan_icmp(g->builder, LLVMIntNE, fp,
                LLVMConstNull(i8ptr), "exists");
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

        if (!zan_type_defines(g, "File", "Copy") &&
            is_call_to(expr, "File", "Copy") && expr->call.args.count == 2 &&
            !src_method_takes_over(g, expr, locals)) {
            LLVMValueRef src = emit_expr(g, expr->call.args.items[0], locals);
            LLVMValueRef dst = emit_expr(g, expr->call.args.items[1], locals);
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
            LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
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
            LLVMValueRef rb = zan_irgen_intern_string(g, "rb");
            LLVMValueRef sargs[] = { src, rb };
            LLVMValueRef sfp = zan_call2(g->builder,
                LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0),
                fopen_fn, sargs, 2, "sfp");
            emit_fopen_check(g, sfp, "cannot read file\n");
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
            LLVMValueRef buf = zan_call2(g->builder,
                LLVMGlobalGetValueType(g->fn_malloc), g->fn_malloc,
                &sz, 1, "cp.buf");
            {
                LLVMTypeRef i8ptr_t = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                emit_io_abort_if(g, zan_icmp(g->builder, LLVMIntEQ, buf,
                    LLVMConstPointerNull(i8ptr_t), "cp.buf.null"),
                    "out of memory\n");
            }
            LLVMValueRef fread_args[] = { buf, LLVMConstInt(i64, 1, 0), sz, sfp };
            zan_call2(g->builder, LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr, i64, i64, i8ptr }, 4, 0),
                fread_fn, fread_args, 4, "");
            zan_call2(g->builder, LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr }, 1, 0),
                fclose_fn, &sfp, 1, "");
            LLVMValueRef wb = zan_irgen_intern_string(g, "wb");
            LLVMValueRef dargs[] = { dst, wb };
            LLVMValueRef dfp = zan_call2(g->builder,
                LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0),
                fopen_fn, dargs, 2, "dfp");
            emit_fopen_check(g, dfp, "cannot write file\n");
            LLVMValueRef fwrite_args[] = { buf, LLVMConstInt(i64, 1, 0), sz, dfp };
            zan_call2(g->builder, LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr, i64, i64, i8ptr }, 4, 0),
                fwrite_fn, fwrite_args, 4, "");
            zan_call2(g->builder, LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr }, 1, 0),
                fclose_fn, &dfp, 1, "");
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
            LLVMValueRef mode = zan_irgen_intern_string(g, "rb");
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
            /* Windows: GetFileAttributesA & FILE_ATTRIBUTE_DIRECTORY */
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

        /* Environment */
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

        /* Directory */
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
            LLVMValueRef len = zan_call2(g->builder,
                LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr }, 1, 0), g->fn_strlen, &path_val, 1, "plen");
            LLVMValueRef bsz = zan_add(g->builder, len, LLVMConstInt(i64, 1, 0), "bsz");
            LLVMValueRef buf = emit_string_alloc_rc(g, bsz);
            LLVMValueRef cpy_args[] = { buf, path_val };
            zan_call2(g->builder, LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0),
                g->fn_strcpy, cpy_args, 2, "");
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
            LLVMValueRef sl_null = zan_icmp(g->builder, LLVMIntEQ, slash, LLVMConstNull(i8ptr), "snul");
            LLVMValueRef pick = LLVMBuildSelect(g->builder, sl_null, bslash, slash, "pk1");
            LLVMValueRef bs_null = zan_icmp(g->builder, LLVMIntEQ, bslash, LLVMConstNull(i8ptr), "bnul");
            LLVMValueRef sep_ptr = LLVMBuildSelect(g->builder, bs_null, pick,
                LLVMBuildSelect(g->builder,
                    zan_icmp(g->builder, LLVMIntUGT, bslash, pick, "bgt"),
                    bslash, pick, "pk2"), "sep");
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
                LLVMValueRef key = zan_irgen_intern_string(g, "TMPDIR");
                LLVMValueRef env = zan_call2(g->builder,
                    LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr }, 1, 0),
                    getenv_fn, &key, 1, "tmpenv");
                LLVMValueRef isnull = zan_icmp(g->builder, LLVMIntEQ, env, LLVMConstNull(i8ptr), "tnull");
                LLVMValueRef deflt = zan_irgen_intern_string(g, "/tmp/");
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
            LLVMValueRef strrchr_fn = LLVMGetNamedFunction(g->mod, "strrchr");
            LLVMValueRef slash_args[] = { path_val, LLVMConstInt(i32t, '/', 0) };
            LLVMValueRef slash = zan_call2(g->builder,
                LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i32t }, 2, 0),
                strrchr_fn, slash_args, 2, "sl");
            LLVMValueRef bslash_args[] = { path_val, LLVMConstInt(i32t, 92, 0) };
            LLVMValueRef bslash = zan_call2(g->builder,
                LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i32t }, 2, 0),
                strrchr_fn, bslash_args, 2, "bsl");
            LLVMValueRef sl_null = zan_icmp(g->builder, LLVMIntEQ, slash, LLVMConstNull(i8ptr), "snul");
            LLVMValueRef bs_null = zan_icmp(g->builder, LLVMIntEQ, bslash, LLVMConstNull(i8ptr), "bnul");
            LLVMValueRef best = LLVMBuildSelect(g->builder, sl_null, bslash,
                LLVMBuildSelect(g->builder, bs_null, slash,
                    LLVMBuildSelect(g->builder, zan_icmp(g->builder, LLVMIntUGT, bslash, slash, "bgt"),
                        bslash, slash, "mx"), "pk"), "sep");
            LLVMValueRef has_sep = zan_icmp(g->builder, LLVMIntNE, best, LLVMConstNull(i8ptr), "hs");
            LLVMValueRef after = LLVMBuildGEP2(g->builder, i8, best, &(LLVMValueRef){LLVMConstInt(i64, 1, 0)}, 1, "aft");
            LLVMValueRef fname = LLVMBuildSelect(g->builder, has_sep, after, path_val, "fn");
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
