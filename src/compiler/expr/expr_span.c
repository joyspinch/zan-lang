#include "irgen_internal.h"
/* expr_span.c: Span<T> 发射——存储布局、Slice 窗口越界检查与 span 调用分派。
 * 拼接片段（fragment）：由 irgen_expr.c #include，非独立编译单元。 */
LLVMValueRef get_async_result_ptr(zan_irgen_t *g);
LLVMValueRef get_async_sub_slot_ptr(zan_irgen_t *g);

/* 实参里"拥有型临时量"的 EH 临时槽登记（定义在 irgen_call.c，晚于本文件
   被包含）：普通方法调用路径用它保证"调用抛异常时由展开器释放临时量"，
   构造路径此前漏了这一步，构造函数一抛，实参里的临时量就泄漏了。 */
int emit_call_arg_eh_push(zan_irgen_t *g, zan_ast_node_t *arg, LLVMValueRef val,
                          local_scope_t *locals);

/* NativeMemory 原语：堆外内存操作，地址使用 nint (i64)，不计入 ARC */

/* 将实例接收者绑定至方法组闭包记录；recv 为 NULL 时生成静态方法组 */
LLVMValueRef emit_method_group_closure(zan_irgen_t *g, zan_symbol_t *msym,
                                       LLVMValueRef recv, zan_loc_t loc);

/* 静态方法组记录构建器 */
LLVMValueRef build_static_mg_record(zan_irgen_t *g, zan_loc_t loc,
                                    const char *lname, LLVMTypeRef rec_ty,
                                    LLVMValueRef thunk);

/* 合成的 Binding<T> 访问器委托：原生平台使用纯函数指针，wasm32
 * 采用静态方法组记录 */
LLVMValueRef emit_binding_acc_delegate(zan_irgen_t *g, LLVMValueRef acc,
                                       LLVMTypeRef acc_ty);

/* 检查类或其基类中是否声明了指定名称的成员（字段/方法/属性/事件/常量） */
int type_declares_member(zan_symbol_t *cls, zan_istr_t name) {
  while (cls) {
    for (int i = 0; i < cls->member_count; i++) {
      zan_symbol_t *m = cls->members[i];
      if (m && m->name.len == name.len &&
          memcmp(m->name.str, name.str, (size_t)name.len) == 0)
        return 1;
    }
    zan_symbol_t *base =
        (cls->type && cls->type->base_type) ? cls->type->base_type->sym : NULL;
    cls = (base && base != cls) ? base : NULL;
  }
  return 0;
}

/* 字符串边界安全判断：字面量与非透明局部变量可信任 NUL 终止符作为有效边界 */
int expr_has_reliable_string_bounds(zan_ast_node_t *expr,
                                    local_scope_t *locals) {
  if (!expr)
    return 0;
  if (expr->kind == AST_STRING_LITERAL)
    return 1;
  if (expr->kind == AST_IDENTIFIER) {
    local_var_t *local = local_find(locals, expr->ident.name);
    return local && local->type && local->type->kind == TYPE_STRING &&
           !local->opaque_string;
  }
  return 0;
}

/* 计算内存地址偏移指针 (base + offset) */
static LLVMValueRef nm_addr(zan_irgen_t *g, LLVMValueRef base,
                            LLVMValueRef off) {
  LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
  LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
  LLVMValueRef p = LLVMBuildIntToPtr(g->builder, base, i8ptr, "nm.p");
  return LLVMBuildGEP2(g->builder, i8, p, &off, 1, "nm.at");
}

/* 将发射的实参整型符号扩展或截断至 i64 */
static LLVMValueRef nm_to_i64(zan_irgen_t *g, LLVMValueRef v) {
  LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
  LLVMTypeRef t = LLVMTypeOf(v);
  if (LLVMGetTypeKind(t) == LLVMPointerTypeKind)
    return LLVMBuildPtrToInt(g->builder, v, i64t, "nm.pi");
  if (LLVMGetTypeKind(t) == LLVMIntegerTypeKind && LLVMGetIntTypeWidth(t) < 64)
    return LLVMBuildSExt(g->builder, v, i64t, "nm.sx");
  return v;
}

static LLVMValueRef nm_arg(zan_irgen_t *g, zan_ast_node_t *expr, int i,
                           local_scope_t *locals) {
  return nm_to_i64(g, emit_expr(g, expr->call.args.items[i], locals));
}

/* __zan_crc32c_table: 256 项 CRC32C 预计算查找表 */
static LLVMValueRef crc32c_table_global(zan_irgen_t *g) {
  LLVMValueRef tab = LLVMGetNamedGlobal(g->mod, "__zan_crc32c_table");
  if (tab)
    return tab;
  LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
  LLVMValueRef entries[256];
  for (uint32_t i = 0; i < 256; i++) {
    uint32_t c = i;
    for (int k = 0; k < 8; k++)
      c = (c & 1) ? (0x82F63B78u ^ (c >> 1)) : (c >> 1);
    entries[i] = LLVMConstInt(i32t, c, 0);
  }
  LLVMTypeRef tab_ty = LLVMArrayType(i32t, 256);
  tab = LLVMAddGlobal(g->mod, tab_ty, "__zan_crc32c_table");
  LLVMSetInitializer(tab, LLVMConstArray(i32t, entries, 256));
  LLVMSetGlobalConstant(tab, 1);
  LLVMSetLinkage(tab, LLVMInternalLinkage);
  return tab;
}

/* __zan_crc32c_stepN: 非 x86 目标平台的逐字节/多字节 CRC32C 软降解实现 */
static LLVMValueRef crc32c_step_fn(zan_irgen_t *g, int nbytes) {
  char name[40];
  snprintf(name, sizeof(name), "__zan_crc32c_step%d", nbytes);
  LLVMValueRef fn = LLVMGetNamedFunction(g->mod, name);
  if (fn)
    return fn;
  LLVMTypeRef i8t = LLVMInt8TypeInContext(g->ctx);
  LLVMTypeRef i16t = LLVMInt16TypeInContext(g->ctx);
  LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
  LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
  LLVMTypeRef rt = (nbytes == 8) ? i64t : i32t;
  LLVMTypeRef dt = (nbytes == 1)   ? i8t
                   : (nbytes == 2) ? i16t
                   : (nbytes == 4) ? i32t
                                   : i64t;
  LLVMTypeRef fnty = LLVMFunctionType(rt, (LLVMTypeRef[]){rt, dt}, 2, 0);
  fn = LLVMAddFunction(g->mod, name, fnty);
  LLVMSetLinkage(fn, LLVMInternalLinkage);

  LLVMValueRef tab = crc32c_table_global(g);
  LLVMTypeRef tab_ty = LLVMArrayType(i32t, 256);

  LLVMBasicBlockRef saved = LLVMGetInsertBlock(g->builder);
  LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
  LLVMPositionBuilderAtEnd(g->builder, entry);

  LLVMValueRef crc =
      LLVMBuildTrunc(g->builder, LLVMGetParam(fn, 0), i32t, "crc32");
  LLVMValueRef data;
  if (dt == i64t)
    data = LLVMGetParam(fn, 1);
  else
    data = LLVMBuildZExt(g->builder, LLVMGetParam(fn, 1), i64t, "data64");

  for (int k = 0; k < nbytes; k++) {
    char pn[16];
    snprintf(pn, sizeof(pn), "b%d", k);
    LLVMValueRef sh = LLVMConstInt(i64t, 8 * k, 0);
    LLVMValueRef b64 = zan_and(g->builder, zan_lshr(g->builder, data, sh, pn),
                               LLVMConstInt(i64t, 0xFF, 0), "bm");
    LLVMValueRef b32 = LLVMBuildTrunc(g->builder, b64, i32t, pn);
    LLVMValueRef ti = zan_and(g->builder, zan_xor(g->builder, crc, b32, "x"),
                              LLVMConstInt(i32t, 0xFF, 0), "ti");
    LLVMValueRef ti64 = LLVMBuildZExt(g->builder, ti, i64t, "ti64");
    LLVMValueRef gep_idx[] = {LLVMConstInt(i64t, 0, 0), ti64};
    LLVMValueRef ep = LLVMBuildGEP2(g->builder, tab_ty, tab, gep_idx, 2, "ep");
    LLVMValueRef te = LLVMBuildLoad2(g->builder, i32t, ep, "te");
    crc = zan_xor(g->builder, te,
                  zan_lshr(g->builder, crc, LLVMConstInt(i32t, 8, 0), "sh"),
                  "nc");
  }

  if (nbytes == 8)
    LLVMBuildRet(g->builder, LLVMBuildZExt(g->builder, crc, i64t, "ret64"));
  else
    LLVMBuildRet(g->builder, crc);
  if (saved)
    LLVMPositionBuilderAtEnd(g->builder, saved);
  return fn;
}

/* 2D 跨步内存复制：按行拷贝指定行数与行宽字节 */
static LLVMValueRef nm_copy2d_fn(zan_irgen_t *g) {
  LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "__zan_nm_copy2d");
  if (fn)
    return fn;
  LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
  LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
  LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
  LLVMTypeRef void_ty = LLVMVoidTypeInContext(g->ctx);

  LLVMTypeRef fn_ty = LLVMFunctionType(
      void_ty, (LLVMTypeRef[]){i8ptr, i64t, i8ptr, i64t, i64t, i64t}, 6, 0);
  fn = LLVMAddFunction(g->mod, "__zan_nm_copy2d", fn_ty);
  LLVMSetLinkage(fn, LLVMInternalLinkage);

  LLVMBasicBlockRef saved = LLVMGetInsertBlock(g->builder);
  LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
  LLVMBasicBlockRef check_fast =
      LLVMAppendBasicBlockInContext(g->ctx, fn, "check_fast");
  LLVMBasicBlockRef fast_path =
      LLVMAppendBasicBlockInContext(g->ctx, fn, "fast");
  LLVMBasicBlockRef slow_loop =
      LLVMAppendBasicBlockInContext(g->ctx, fn, "loop");
  LLVMBasicBlockRef body = LLVMAppendBasicBlockInContext(g->ctx, fn, "body");
  LLVMBasicBlockRef done = LLVMAppendBasicBlockInContext(g->ctx, fn, "done");

  LLVMPositionBuilderAtEnd(g->builder, entry);
  LLVMValueRef dst = LLVMGetParam(fn, 0);
  LLVMValueRef dst_stride = LLVMGetParam(fn, 1);
  LLVMValueRef src = LLVMGetParam(fn, 2);
  LLVMValueRef src_stride = LLVMGetParam(fn, 3);
  LLVMValueRef row_bytes = LLVMGetParam(fn, 4);
  LLVMValueRef height = LLVMGetParam(fn, 5);

  LLVMValueRef zero64 = LLVMConstInt(i64t, 0, 0);
  LLVMValueRef has_bytes =
      zan_icmp(g->builder, LLVMIntSGT, row_bytes, zero64, "has_b");
  LLVMValueRef has_lines =
      zan_icmp(g->builder, LLVMIntSGT, height, zero64, "has_h");
  LLVMValueRef can_copy = zan_and(g->builder, has_bytes, has_lines, "can_cp");
  LLVMBuildCondBr(g->builder, can_copy, check_fast, done);

  /* 连续内存块判断：步长与行宽一致时走高速拷贝 */
  LLVMPositionBuilderAtEnd(g->builder, check_fast);
  LLVMValueRef eq_dst =
      zan_icmp(g->builder, LLVMIntEQ, dst_stride, row_bytes, "eq_dst");
  LLVMValueRef eq_src =
      zan_icmp(g->builder, LLVMIntEQ, src_stride, row_bytes, "eq_src");
  LLVMValueRef is_fast = zan_and(g->builder, eq_dst, eq_src, "is_fast");
  LLVMBuildCondBr(g->builder, is_fast, fast_path, slow_loop);

  LLVMTypeRef memmove_ty =
      LLVMFunctionType(i8ptr, (LLVMTypeRef[]){i8ptr, i8ptr, i64t}, 3, 0);
  LLVMValueRef memmove_fn = get_libc_fn(g, "memmove", memmove_ty);

  /* 快路径：单次 memmove 连续拷贝整块像素数据 */
  LLVMPositionBuilderAtEnd(g->builder, fast_path);
  LLVMValueRef total_bytes = zan_mul(g->builder, row_bytes, height, "total_b");
  zan_call2(g->builder, memmove_ty, memmove_fn,
            (LLVMValueRef[]){dst, src, total_bytes}, 3, "");
  LLVMBuildBr(g->builder, done);

  /* 慢路径：逐行步进跨步拷贝 */
  LLVMPositionBuilderAtEnd(g->builder, slow_loop);
  LLVMValueRef row = LLVMBuildPhi(g->builder, i64t, "row");
  LLVMValueRef in_range =
      zan_icmp(g->builder, LLVMIntSLT, row, height, "in_rng");
  LLVMBuildCondBr(g->builder, in_range, body, done);

  LLVMPositionBuilderAtEnd(g->builder, body);
  LLVMValueRef dst_off = zan_mul(g->builder, row, dst_stride, "dst_off");
  LLVMValueRef src_off = zan_mul(g->builder, row, src_stride, "src_off");
  LLVMValueRef row_dst =
      LLVMBuildGEP2(g->builder, i8, dst, &dst_off, 1, "rdst");
  LLVMValueRef row_src =
      LLVMBuildGEP2(g->builder, i8, src, &src_off, 1, "rsrc");
  zan_call2(g->builder, memmove_ty, memmove_fn,
            (LLVMValueRef[]){row_dst, row_src, row_bytes}, 3, "");
  LLVMValueRef next_row =
      zan_add(g->builder, row, LLVMConstInt(i64t, 1, 0), "next_row");
  LLVMBuildBr(g->builder, slow_loop);

  LLVMAddIncoming(row, (LLVMValueRef[]){zero64, next_row},
                  (LLVMBasicBlockRef[]){check_fast, body}, 2);

  LLVMPositionBuilderAtEnd(g->builder, done);
  LLVMBuildRetVoid(g->builder);

  if (saved)
    LLVMPositionBuilderAtEnd(g->builder, saved);
  return fn;
}
LLVMTypeRef span_elem_storage_type(zan_irgen_t *g, zan_type_t *elem_type) {
  if (!elem_type)
    return LLVMInt32TypeInContext(g->ctx);
  if (elem_type->kind == TYPE_CHAR)
    return LLVMInt8TypeInContext(g->ctx);
  return map_type(g, elem_type);
}

unsigned zan_span_elem_align(zan_irgen_t *g, LLVMTypeRef elem_llvm) {
  if (!elem_llvm)
    return 1;
  if (!g->tbaa_td && g->mod) {
    const char *dl = LLVMGetDataLayoutStr(g->mod);
    if (dl && dl[0])
      g->tbaa_td = LLVMCreateTargetData(dl);
  }
  if (g->tbaa_td) {
    unsigned a = LLVMABIAlignmentOfType(g->tbaa_td, elem_llvm);
    if (a >= 1 && a <= 16)
      return a;
    if (a > 16)
      return 16;
  }
  LLVMTypeKind tk = LLVMGetTypeKind(elem_llvm);
  if (tk == LLVMIntegerTypeKind) {
    unsigned bits = LLVMGetIntTypeWidth(elem_llvm);
    if (bits <= 8) return 1;
    if (bits <= 16) return 2;
    if (bits <= 32) return 4;
    return 8;
  }
  if (tk == LLVMFloatTypeKind) return 4;
  if (tk == LLVMDoubleTypeKind || tk == LLVMPointerTypeKind) return 8;
  return 1;
}

void zan_fn_reset_alias_scopes(zan_irgen_t *g) {
  g->fn_alias_domain = NULL;
  g->fn_alias_scope_count = 0;
}

int zan_fn_alloc_alias_scope(zan_irgen_t *g) {
  if (g->fn_alias_scope_count >= 16)
    return 0;
  if (!g->fn_alias_domain) {
    char dbuf[48];
    int dlen = snprintf(dbuf, sizeof(dbuf), "zan.alias.dom.%d", ++g->fn_alias_domain_seq);
    LLVMMetadataRef dstr = LLVMMDStringInContext2(g->ctx, dbuf, (size_t)dlen);
    g->fn_alias_domain = LLVMMDNodeInContext2(g->ctx, &dstr, 1);
  }
  int idx = g->fn_alias_scope_count;
  char sbuf[64];
  int slen = snprintf(sbuf, sizeof(sbuf), "zan.alias.scope.%d.%d",
                      g->fn_alias_domain_seq, idx);
  LLVMMetadataRef sops[2] = {
      LLVMMDStringInContext2(g->ctx, sbuf, (size_t)slen),
      g->fn_alias_domain
  };
  g->fn_alias_scopes[idx] = LLVMMDNodeInContext2(g->ctx, sops, 2);
  g->fn_alias_scope_count++;
  return idx + 1; /* 1-based scope id */
}

void zan_attach_noalias_md(zan_irgen_t *g, LLVMValueRef mem_inst, int scope_id) {
  if (!mem_inst || scope_id <= 0 || scope_id > g->fn_alias_scope_count ||
      g->fn_alias_scope_count < 2)
    return;
  int idx = scope_id - 1;
  LLVMMetadataRef my_scope = g->fn_alias_scopes[idx];
  LLVMMetadataRef scope_list = LLVMMDNodeInContext2(g->ctx, &my_scope, 1);
  unsigned k_as = LLVMGetMDKindIDInContext(g->ctx, "alias.scope", 11);
  LLVMSetMetadata(mem_inst, k_as, LLVMMetadataAsValue(g->ctx, scope_list));

  LLVMMetadataRef others[16];
  int oc = 0;
  for (int j = 0; j < g->fn_alias_scope_count; j++) {
    if (j != idx)
      others[oc++] = g->fn_alias_scopes[j];
  }
  if (oc > 0) {
    LLVMMetadataRef noalias_list = LLVMMDNodeInContext2(g->ctx, others, (size_t)oc);
    unsigned k_na = LLVMGetMDKindIDInContext(g->ctx, "noalias", 7);
    LLVMSetMetadata(mem_inst, k_na, LLVMMetadataAsValue(g->ctx, noalias_list));
  }
}

int zan_local_noalias_scope(zan_ast_node_t *expr, local_scope_t *locals) {
  if (!expr || expr->kind != AST_IDENTIFIER || !locals)
    return 0;
  local_var_t *lv = local_find(locals, expr->ident.name);
  return lv ? lv->noalias_scope_id : 0;
}

/* Span<T> 原语：将切片操作降解为 { i8* base, i64 len } 胖指针视图 */
bool emit_span_call(zan_irgen_t *g, zan_ast_node_t *expr, local_scope_t *locals,
                    LLVMValueRef *out) {
  zan_ast_node_t *callee = expr->call.callee;
  if (!callee || callee->kind != AST_MEMBER_ACCESS)
    return false;
  zan_istr_t mm = callee->member.name;
  LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
  LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
  LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
  if ((mm.len == 6 && memcmp(mm.str, "AsSpan", 6) == 0) ||
      (mm.len == 14 && memcmp(mm.str, "AsReadOnlySpan", 14) == 0)) {
    zan_type_t *ot = infer_expr_type(g, callee->member.object, locals);
    if (!ot)
      return false;
    LLVMValueRef base = NULL;
    LLVMValueRef len = NULL;
    LLVMTypeRef elem_llvm = NULL;
    if (ot->kind == TYPE_ARRAY && ot->element_type) {
      LLVMValueRef arr = emit_expr(g, callee->member.object, locals);
      base = LLVMBuildBitCast(g->builder, arr, i8ptr, "span.base");
      len = zan_array_len(g, arr);
      elem_llvm = span_elem_storage_type(g, ot->element_type);
    } else if (ot->kind == TYPE_STRING) {
      LLVMValueRef sobj = emit_expr(g, callee->member.object, locals);
      base = emit_str_nonnull(g, sobj);
      len = emit_string_length(g, base, callee->loc);
      elem_llvm = span_elem_storage_type(g, g->binder->type_char);
    } else if (is_inline_array_type(ot)) {
      int64_t arr_n = inline_array_len(ot);
      zan_type_t *et = inline_array_elem(ot);
      elem_llvm = span_elem_storage_type(g, et);
      LLVMValueRef ptr = emit_ref_lvalue_ptr(g, callee->member.object, locals);
      if (!ptr) {
        LLVMTypeRef arr_llvm =
            LLVMArrayType(elem_llvm, (unsigned)(arr_n > 0 ? arr_n : 1));
        LLVMValueRef val = emit_expr(g, callee->member.object, locals);
        ptr = emit_entry_alloca(g, arr_llvm, "iasp.tmp");
        LLVMBuildStore(g->builder, val, ptr);
      }
      base = LLVMBuildBitCast(g->builder, ptr, i8ptr, "iasp.base");
      len = LLVMConstInt(i64t, (unsigned long long)arr_n, 0);
    } else {
      return false;
    }
    if (expr->call.args.count >= 1) {
      LLVMValueRef start = coerce_int_to(
          g, emit_expr(g, expr->call.args.items[0], locals), i64t);
      LLVMValueRef window =
          (expr->call.args.count >= 2)
              ? coerce_int_to(g, emit_expr(g, expr->call.args.items[1], locals),
                              i64t)
              : LLVMBuildSub(g->builder, len, start, "span.len");
      emit_span_window_check(g, start, window, len, expr->loc, "span");
      emit_span_safe_window(g, &start, &window, len, expr->loc, "span");
      LLVMValueRef off =
          LLVMBuildMul(g->builder, start, LLVMSizeOf(elem_llvm), "span.boff");
      base = LLVMBuildGEP2(g->builder, i8, base, &off, 1, "span.base2");
      len = window;
    }
    LLVMValueRef v = LLVMGetUndef(g->span_struct_type);
    v = LLVMBuildInsertValue(g->builder, v, base, 0, "span.iv0");
    v = LLVMBuildInsertValue(g->builder, v, len, 1, "span.iv1");
    *out = v;
    return true;
  }
  if (mm.len == 5 && memcmp(mm.str, "Slice", 5) == 0) {
    zan_type_t *ot = infer_expr_type(g, callee->member.object, locals);
    if (!is_span_type(ot) || expr->call.args.count < 1)
      return false;
    zan_type_t *elem = container_elem_type(ot);
    LLVMTypeRef elem_llvm = span_elem_storage_type(g, elem);
    LLVMValueRef span_val = emit_expr(g, callee->member.object, locals);
    LLVMValueRef base =
        LLVMBuildExtractValue(g->builder, span_val, 0, "sl.base");
    LLVMValueRef len = LLVMBuildExtractValue(g->builder, span_val, 1, "sl.len");
    LLVMValueRef start =
        coerce_int_to(g, emit_expr(g, expr->call.args.items[0], locals), i64t);
    LLVMValueRef nlen =
        (expr->call.args.count >= 2)
            ? coerce_int_to(g, emit_expr(g, expr->call.args.items[1], locals),
                            i64t)
            : LLVMBuildSub(g->builder, len, start, "sl.len2");
    emit_span_window_check(g, start, nlen, len, expr->loc, "span");
    emit_span_safe_window(g, &start, &nlen, len, expr->loc, "span");
    LLVMValueRef off =
        LLVMBuildMul(g->builder, start, LLVMSizeOf(elem_llvm), "sl.boff");
    LLVMValueRef nbase =
        LLVMBuildGEP2(g->builder, i8, base, &off, 1, "sl.base2");
    LLVMValueRef v = LLVMGetUndef(g->span_struct_type);
    v = LLVMBuildInsertValue(g->builder, v, nbase, 0, "sl.iv0");
    v = LLVMBuildInsertValue(g->builder, v, nlen, 1, "sl.iv1");
    *out = v;
    return true;
  }
  return false;
}

/* byte[] 与 string 零拷贝互转桥接原语（ToBytes / ToStr） */
bool emit_bytes_call(zan_irgen_t *g, zan_ast_node_t *expr,
                     local_scope_t *locals, LLVMValueRef *out) {
  zan_ast_node_t *callee = expr->call.callee;
  if (!callee || callee->kind != AST_MEMBER_ACCESS)
    return false;
  zan_istr_t mm = callee->member.name;
  bool to_bytes = mm.len == 7 && memcmp(mm.str, "ToBytes", 7) == 0;
  bool to_str = mm.len == 5 && memcmp(mm.str, "ToStr", 5) == 0;
  if (!to_bytes && !to_str)
    return false;

  zan_type_t *ot = infer_expr_type(g, callee->member.object, locals);
  if (!ot)
    return false;
  LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
  LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
  LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
  LLVMTypeRef memcpy_ty =
      LLVMFunctionType(i8ptr, (LLVMTypeRef[]){i8ptr, i8ptr, i64t}, 3, 0);
  LLVMValueRef memcpy_fn = get_libc_fn(g, "memcpy", memcpy_ty);

  if (to_bytes) {
    if (ot->kind != TYPE_STRING || expr->call.args.count != 0)
      return false;
    LLVMValueRef obj_v = emit_expr(g, callee->member.object, locals);
    LLVMValueRef s = emit_str_nonnull(g, obj_v);
    LLVMValueRef len = emit_string_length(g, s, callee->loc);
    LLVMValueRef arr = zan_array_alloc(g, len, len);
    zan_call2(g->builder, memcpy_ty, memcpy_fn, (LLVMValueRef[]){arr, s, len},
              3, "");
    /* 链式接收者临时对象需在转换后按 RC 规则及时释放 */
    emit_release_owned_call_temp(g, callee->member.object, obj_v, locals);
    *out = arr;
    return true;
  }

  if (ot->kind != TYPE_ARRAY)
    return false;
  if (expr->call.args.count != 0 && expr->call.args.count != 2)
    return false;
  LLVMValueRef arr = emit_expr(g, callee->member.object, locals);
  LLVMValueRef src = arr, len = zan_array_len(g, arr);
  if (expr->call.args.count == 2) {
    LLVMValueRef off =
        coerce_int_to(g, emit_expr(g, expr->call.args.items[0], locals), i64t);
    len =
        coerce_int_to(g, emit_expr(g, expr->call.args.items[1], locals), i64t);
    /* 窗口区间边界截断：越界请求安全返回空字符串 */
    LLVMValueRef zero64 = LLVMConstInt(i64t, 0, 0);
    LLVMValueRef alen = zan_array_len(g, arr);
    off = LLVMBuildSelect(
        g->builder, zan_icmp(g->builder, LLVMIntSLT, off, zero64, "ts.oneg"),
        zero64, off, "ts.off");
    off = LLVMBuildSelect(
        g->builder, zan_icmp(g->builder, LLVMIntSGT, off, alen, "ts.obig"),
        alen, off, "ts.off2");
    LLVMValueRef avail = zan_sub(g->builder, alen, off, "ts.avail");
    len = LLVMBuildSelect(
        g->builder, zan_icmp(g->builder, LLVMIntSLT, len, zero64, "ts.lneg"),
        zero64, len, "ts.len");
    len = LLVMBuildSelect(
        g->builder, zan_icmp(g->builder, LLVMIntSGT, len, avail, "ts.lbig"),
        avail, len, "ts.len2");
    src = LLVMBuildGEP2(g->builder, i8, arr, &off, 1, "ts.src");
  }
  LLVMTypeRef alloc_ty = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){i64t}, 1, 0);
  LLVMValueRef total =
      LLVMBuildAdd(g->builder, len, LLVMConstInt(i64t, 1, 0), "ts.n");
  LLVMValueRef s =
      zan_call2(g->builder, alloc_ty, g->rt_str_alloc, &total, 1, "ts.str");
  zan_call2(g->builder, memcpy_ty, memcpy_fn, (LLVMValueRef[]){s, src, len}, 3,
            "");
  LLVMValueRef endp = LLVMBuildGEP2(g->builder, i8, s, &len, 1, "ts.end");
  LLVMBuildStore(g->builder, LLVMConstInt(i8, 0, 0), endp);
  /* 标记调用方显式声明的字节长度，避免后续扫描判断失真 */
  emit_string_len_set(g, s, len);
  /* 释放接收者数组所有权，防止链式调用内存泄漏 */
  emit_release_owned_call_temp(g, callee->member.object, arr, locals);
  *out = s;
  return true;
}

/* Portable SIMD Vec<N, T> lowering helpers (<N x T> LLVM vectors) */
static LLVMValueRef vec_coerce_scalar(zan_irgen_t *g, LLVMValueRef v,
                                      LLVMTypeRef elt) {
  if (LLVMTypeOf(v) == elt)
    return v;
  LLVMTypeKind ek = LLVMGetTypeKind(elt);
  LLVMTypeKind vk = LLVMGetTypeKind(LLVMTypeOf(v));
  if (ek == LLVMFloatTypeKind || ek == LLVMDoubleTypeKind) {
    if (vk == LLVMIntegerTypeKind)
      return LLVMBuildSIToFP(g->builder, v, elt, "v.fp");
    return LLVMBuildFPCast(g->builder, v, elt, "v.fpc");
  }
  return coerce_int_to(g, v, elt);
}

static LLVMValueRef vec_splat_val(zan_irgen_t *g, LLVMValueRef scalar,
                                  LLVMTypeRef vec_ty, unsigned nlanes) {
  LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
  LLVMValueRef zero = LLVMConstInt(i32t, 0, 0);
  LLVMValueRef ins = LLVMBuildInsertElement(g->builder, LLVMGetUndef(vec_ty),
                                            scalar, zero, "v.splat0");
  LLVMValueRef *mask = (LLVMValueRef *)calloc(nlanes, sizeof(LLVMValueRef));
  for (unsigned i = 0; i < nlanes; i++)
    mask[i] = zero;
  LLVMValueRef res = LLVMBuildShuffleVector(
      g->builder, ins, LLVMGetUndef(vec_ty), LLVMConstVector(mask, nlanes),
      "v.splat");
  free(mask);
  return res;
}

static void vec_intrinsic_suffix(LLVMTypeRef elt, unsigned nlanes, char *buf,
                                 size_t cap) {
  LLVMTypeKind ek = LLVMGetTypeKind(elt);
  if (ek == LLVMFloatTypeKind)
    snprintf(buf, cap, "v%uf32", nlanes);
  else if (ek == LLVMDoubleTypeKind)
    snprintf(buf, cap, "v%uf64", nlanes);
  else
    snprintf(buf, cap, "v%ui%u", nlanes, LLVMGetIntTypeWidth(elt));
}

static bool vec_extract_mem_base(zan_irgen_t *g, zan_ast_node_t *src_expr,
                                 local_scope_t *locals, LLVMValueRef *out_base,
                                 LLVMValueRef *out_len) {
  zan_type_t *st = infer_expr_type(g, src_expr, locals);
  if (!st)
    return false;
  LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
  LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
  if (is_span_type(st)) {
    LLVMValueRef sv = emit_expr(g, src_expr, locals);
    *out_base = LLVMBuildExtractValue(g->builder, sv, 0, "vl.base");
    *out_len = LLVMBuildExtractValue(g->builder, sv, 1, "vl.len");
    return true;
  }
  if (st->kind == TYPE_ARRAY) {
    LLVMValueRef arr = emit_expr(g, src_expr, locals);
    *out_base = LLVMBuildBitCast(g->builder, arr, i8ptr, "vl.abase");
    *out_len = zan_array_len(g, arr);
    return true;
  }
  if (is_inline_array_type(st)) {
    int64_t arr_n = inline_array_len(st);
    LLVMValueRef ptr = emit_ref_lvalue_ptr(g, src_expr, locals);
    if (!ptr)
      return false;
    *out_base = LLVMBuildBitCast(g->builder, ptr, i8ptr, "vl.iabase");
    *out_len = LLVMConstInt(i64t, (unsigned long long)arr_n, 0);
    return true;
  }
  return false;
}

bool emit_vec_call(zan_irgen_t *g, zan_ast_node_t *expr, local_scope_t *locals,
                   LLVMValueRef *out) {
  zan_ast_node_t *callee = expr->call.callee;
  if (!callee || callee->kind != AST_MEMBER_ACCESS)
    return false;
  zan_type_t *vt = infer_expr_type(g, callee->member.object, locals);
  if (!is_vec_type(vt))
    return false;
  int64_t vn = vec_len(vt);
  zan_type_t *et = vec_elem(vt);
  if (!et)
    return false;
  if (et->kind == TYPE_TYPE_PARAM)
    et = concretize(g, et);
  unsigned nlanes = (unsigned)(vn > 0 ? vn : 1);
  LLVMTypeRef elt =
      (et && et->kind != TYPE_TYPE_PARAM)
          ? span_elem_storage_type(g, et)
          : LLVMFloatTypeInContext(g->ctx);
  LLVMTypeRef vec_ty = LLVMVectorType(elt, nlanes);
  LLVMTypeKind ek = LLVMGetTypeKind(elt);
  bool is_fp = (ek == LLVMFloatTypeKind || ek == LLVMDoubleTypeKind);
  bool is_unsigned =
      (et->kind == TYPE_BYTE || et->kind == TYPE_USHORT ||
       et->kind == TYPE_UINT || et->kind == TYPE_ULONG);
  bool is_static_recv = (callee->member.object->kind == AST_IDENTIFIER &&
                         callee->member.object->ident.inst_type_ref != NULL);
  zan_istr_t mm = callee->member.name;
  int argc = expr->call.args.count;

  if (mm.len == 4 && memcmp(mm.str, "Zero", 4) == 0 && argc == 0) {
    *out = LLVMConstNull(vec_ty);
    return true;
  }
  if (mm.len == 5 && memcmp(mm.str, "Splat", 5) == 0 && argc == 1) {
    LLVMValueRef sv =
        vec_coerce_scalar(g, emit_expr(g, expr->call.args.items[0], locals), elt);
    *out = vec_splat_val(g, sv, vec_ty, nlanes);
    return true;
  }
  if (mm.len == 4 && memcmp(mm.str, "Load", 4) == 0 &&
      (argc == 1 || argc == 2)) {
    LLVMValueRef base = NULL, slen = NULL;
    if (!vec_extract_mem_base(g, expr->call.args.items[0], locals, &base, &slen))
      return false;
    LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
    LLVMValueRef off =
        argc == 2
            ? coerce_int_to(g, emit_expr(g, expr->call.args.items[1], locals),
                            i64t)
            : LLVMConstInt(i64t, 0, 0);
    emit_span_window_check(g, off, LLVMConstInt(i64t, nlanes, 0), slen,
                           expr->loc, "Vec.Load");
    LLVMValueRef typed =
        LLVMBuildBitCast(g->builder, base, LLVMPointerType(elt, 0), "vl.ep");
    LLVMValueRef ep =
        LLVMBuildInBoundsGEP2(g->builder, elt, typed, &off, 1, "vl.ptr");
    LLVMValueRef vp =
        LLVMBuildBitCast(g->builder, ep, LLVMPointerType(vec_ty, 0), "vl.vp");
    LLVMValueRef ld = LLVMBuildLoad2(g->builder, vec_ty, vp, "vl.vec");
    LLVMSetAlignment(ld, zan_span_elem_align(g, elt));
    *out = ld;
    return true;
  }
  if (mm.len == 5 && memcmp(mm.str, "Store", 5) == 0 &&
      (argc == 1 || argc == 2)) {
    LLVMValueRef vv = emit_expr(g, callee->member.object, locals);
    LLVMValueRef base = NULL, slen = NULL;
    if (!vec_extract_mem_base(g, expr->call.args.items[0], locals, &base, &slen))
      return false;
    LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
    LLVMValueRef off =
        argc == 2
            ? coerce_int_to(g, emit_expr(g, expr->call.args.items[1], locals),
                            i64t)
            : LLVMConstInt(i64t, 0, 0);
    emit_span_window_check(g, off, LLVMConstInt(i64t, nlanes, 0), slen,
                           expr->loc, "Vec.Store");
    LLVMValueRef typed =
        LLVMBuildBitCast(g->builder, base, LLVMPointerType(elt, 0), "vs.ep");
    LLVMValueRef ep =
        LLVMBuildInBoundsGEP2(g->builder, elt, typed, &off, 1, "vs.ptr");
    LLVMValueRef vp =
        LLVMBuildBitCast(g->builder, ep, LLVMPointerType(vec_ty, 0), "vs.vp");
    LLVMValueRef st = LLVMBuildStore(g->builder, vv, vp);
    LLVMSetAlignment(st, zan_span_elem_align(g, elt));
    *out = LLVMConstNull(LLVMInt32TypeInContext(g->ctx));
    return true;
  }
  if (mm.len == 8 && memcmp(mm.str, "WithLane", 8) == 0 && argc == 2) {
    LLVMValueRef vv = emit_expr(g, callee->member.object, locals);
    LLVMValueRef idx = coerce_int_to(
        g, emit_expr(g, expr->call.args.items[0], locals),
        LLVMInt32TypeInContext(g->ctx));
    LLVMValueRef sv =
        vec_coerce_scalar(g, emit_expr(g, expr->call.args.items[1], locals), elt);
    *out = LLVMBuildInsertElement(g->builder, vv, sv, idx, "v.with");
    return true;
  }
  if (mm.len == 3 && memcmp(mm.str, "Sum", 3) == 0 && argc == 0) {
    LLVMValueRef vv = emit_expr(g, callee->member.object, locals);
    char suf[32], fname[64];
    vec_intrinsic_suffix(elt, nlanes, suf, sizeof(suf));
    if (is_fp) {
      snprintf(fname, sizeof(fname), "llvm.vector.reduce.fadd.%s", suf);
      LLVMTypeRef fty =
          LLVMFunctionType(elt, (LLVMTypeRef[]){elt, vec_ty}, 2, 0);
      LLVMValueRef fn = LLVMGetNamedFunction(g->mod, fname);
      if (!fn)
        fn = LLVMAddFunction(g->mod, fname, fty);
      LLVMValueRef z0 = LLVMConstReal(elt, 0.0);
      LLVMValueRef res =
          zan_call2(g->builder, fty, fn, (LLVMValueRef[]){z0, vv}, 2, "v.sum");
      *out = promote_loaded(g, res, et);
    } else {
      snprintf(fname, sizeof(fname), "llvm.vector.reduce.add.%s", suf);
      LLVMTypeRef fty = LLVMFunctionType(elt, (LLVMTypeRef[]){vec_ty}, 1, 0);
      LLVMValueRef fn = LLVMGetNamedFunction(g->mod, fname);
      if (!fn)
        fn = LLVMAddFunction(g->mod, fname, fty);
      LLVMValueRef res =
          zan_call2(g->builder, fty, fn, &vv, 1, "v.sum");
      *out = promote_loaded(g, res, et);
    }
    return true;
  }
  if (mm.len == 3 && memcmp(mm.str, "Dot", 3) == 0 && argc == 1) {
    LLVMValueRef a = emit_expr(g, callee->member.object, locals);
    LLVMValueRef b = emit_expr(g, expr->call.args.items[0], locals);
    LLVMValueRef prod = is_fp ? LLVMBuildFMul(g->builder, a, b, "v.dotm")
                              : LLVMBuildMul(g->builder, a, b, "v.dotm");
    char suf[32], fname[64];
    vec_intrinsic_suffix(elt, nlanes, suf, sizeof(suf));
    if (is_fp) {
      snprintf(fname, sizeof(fname), "llvm.vector.reduce.fadd.%s", suf);
      LLVMTypeRef fty =
          LLVMFunctionType(elt, (LLVMTypeRef[]){elt, vec_ty}, 2, 0);
      LLVMValueRef fn = LLVMGetNamedFunction(g->mod, fname);
      if (!fn)
        fn = LLVMAddFunction(g->mod, fname, fty);
      LLVMValueRef z0 = LLVMConstReal(elt, 0.0);
      *out = promote_loaded(
          g, zan_call2(g->builder, fty, fn, (LLVMValueRef[]){z0, prod}, 2, "v.dot"),
          et);
    } else {
      snprintf(fname, sizeof(fname), "llvm.vector.reduce.add.%s", suf);
      LLVMTypeRef fty = LLVMFunctionType(elt, (LLVMTypeRef[]){vec_ty}, 1, 0);
      LLVMValueRef fn = LLVMGetNamedFunction(g->mod, fname);
      if (!fn)
        fn = LLVMAddFunction(g->mod, fname, fty);
      *out = promote_loaded(
          g, zan_call2(g->builder, fty, fn, &prod, 1, "v.dot"), et);
    }
    return true;
  }
  if (((mm.len == 4 && (memcmp(mm.str, "HMin", 4) == 0 ||
                        memcmp(mm.str, "HMax", 4) == 0)) ||
       (argc == 0 && !is_static_recv && mm.len == 3 &&
        (memcmp(mm.str, "Min", 3) == 0 || memcmp(mm.str, "Max", 3) == 0))) &&
      argc == 0) {
    bool is_max = (mm.str[mm.len - 1] == 'x');
    LLVMValueRef vv = emit_expr(g, callee->member.object, locals);
    char suf[32], fname[64];
    vec_intrinsic_suffix(elt, nlanes, suf, sizeof(suf));
    const char *op = is_fp        ? (is_max ? "fmax" : "fmin")
                     : is_unsigned ? (is_max ? "umax" : "umin")
                                   : (is_max ? "smax" : "smin");
    snprintf(fname, sizeof(fname), "llvm.vector.reduce.%s.%s", op, suf);
    LLVMTypeRef fty = LLVMFunctionType(elt, (LLVMTypeRef[]){vec_ty}, 1, 0);
    LLVMValueRef fn = LLVMGetNamedFunction(g->mod, fname);
    if (!fn)
      fn = LLVMAddFunction(g->mod, fname, fty);
    *out = promote_loaded(g, zan_call2(g->builder, fty, fn, &vv, 1, "v.hred"), et);
    return true;
  }
  if ((mm.len == 3 && (memcmp(mm.str, "Min", 3) == 0 ||
                       memcmp(mm.str, "Max", 3) == 0)) &&
      argc == 2) {
    bool is_max = (mm.str[2] == 'x');
    LLVMValueRef a = emit_expr(g, expr->call.args.items[0], locals);
    LLVMValueRef b = emit_expr(g, expr->call.args.items[1], locals);
    LLVMValueRef cmp =
        is_fp ? LLVMBuildFCmp(g->builder, is_max ? LLVMRealOGT : LLVMRealOLT, a,
                              b, "v.cmp")
              : LLVMBuildICmp(
                    g->builder,
                    is_unsigned ? (is_max ? LLVMIntUGT : LLVMIntULT)
                                : (is_max ? LLVMIntSGT : LLVMIntSLT),
                    a, b, "v.cmp");
    *out = LLVMBuildSelect(g->builder, cmp, a, b, is_max ? "v.max" : "v.min");
    return true;
  }
  if (mm.len == 5 && memcmp(mm.str, "Clamp", 5) == 0 && argc == 3) {
    LLVMValueRef v = emit_expr(g, expr->call.args.items[0], locals);
    LLVMValueRef lo = emit_expr(g, expr->call.args.items[1], locals);
    LLVMValueRef hi = emit_expr(g, expr->call.args.items[2], locals);
    LLVMValueRef c1 =
        is_fp ? LLVMBuildFCmp(g->builder, LLVMRealOLT, v, lo, "vc.lo")
              : LLVMBuildICmp(g->builder, is_unsigned ? LLVMIntULT : LLVMIntSLT,
                              v, lo, "vc.lo");
    LLVMValueRef t1 = LLVMBuildSelect(g->builder, c1, lo, v, "vc.m1");
    LLVMValueRef c2 =
        is_fp ? LLVMBuildFCmp(g->builder, LLVMRealOGT, t1, hi, "vc.hi")
              : LLVMBuildICmp(g->builder, is_unsigned ? LLVMIntUGT : LLVMIntSGT,
                              t1, hi, "vc.hi");
    *out = LLVMBuildSelect(g->builder, c2, hi, t1, "vc.res");
    return true;
  }
  if (mm.len == 3 && memcmp(mm.str, "Fma", 3) == 0 && argc == 3 && is_fp) {
    LLVMValueRef a = emit_expr(g, expr->call.args.items[0], locals);
    LLVMValueRef b = emit_expr(g, expr->call.args.items[1], locals);
    LLVMValueRef c = emit_expr(g, expr->call.args.items[2], locals);
    char suf[32], fname[64];
    vec_intrinsic_suffix(elt, nlanes, suf, sizeof(suf));
    snprintf(fname, sizeof(fname), "llvm.fma.%s", suf);
    LLVMTypeRef fty =
        LLVMFunctionType(vec_ty, (LLVMTypeRef[]){vec_ty, vec_ty, vec_ty}, 3, 0);
    LLVMValueRef fn = LLVMGetNamedFunction(g->mod, fname);
    if (!fn)
      fn = LLVMAddFunction(g->mod, fname, fty);
    *out = zan_call2(g->builder, fty, fn, (LLVMValueRef[]){a, b, c}, 3, "v.fma");
    return true;
  }
  if (mm.len == 4 && memcmp(mm.str, "Sqrt", 4) == 0 && argc == 1 && is_fp) {
    LLVMValueRef a = emit_expr(g, expr->call.args.items[0], locals);
    char suf[32], fname[64];
    vec_intrinsic_suffix(elt, nlanes, suf, sizeof(suf));
    snprintf(fname, sizeof(fname), "llvm.sqrt.%s", suf);
    LLVMTypeRef fty = LLVMFunctionType(vec_ty, (LLVMTypeRef[]){vec_ty}, 1, 0);
    LLVMValueRef fn = LLVMGetNamedFunction(g->mod, fname);
    if (!fn)
      fn = LLVMAddFunction(g->mod, fname, fty);
    *out = zan_call2(g->builder, fty, fn, &a, 1, "v.sqrt");
    return true;
  }
  if (mm.len == 3 && memcmp(mm.str, "Abs", 3) == 0 && argc == 1) {
    LLVMValueRef a = emit_expr(g, expr->call.args.items[0], locals);
    if (is_fp) {
      char suf[32], fname[64];
      vec_intrinsic_suffix(elt, nlanes, suf, sizeof(suf));
      snprintf(fname, sizeof(fname), "llvm.fabs.%s", suf);
      LLVMTypeRef fty = LLVMFunctionType(vec_ty, (LLVMTypeRef[]){vec_ty}, 1, 0);
      LLVMValueRef fn = LLVMGetNamedFunction(g->mod, fname);
      if (!fn)
        fn = LLVMAddFunction(g->mod, fname, fty);
      *out = zan_call2(g->builder, fty, fn, &a, 1, "v.fabs");
    } else if (is_unsigned) {
      *out = a;
    } else {
      LLVMValueRef neg = LLVMBuildNeg(g->builder, a, "v.neg");
      LLVMValueRef z0 = LLVMConstNull(vec_ty);
      LLVMValueRef cmp = LLVMBuildICmp(g->builder, LLVMIntSLT, a, z0, "v.lt0");
      *out = LLVMBuildSelect(g->builder, cmp, neg, a, "v.abs");
    }
    return true;
  }
  if (mm.len == 6 && memcmp(mm.str, "Select", 6) == 0 && argc == 3) {
    LLVMValueRef m = emit_expr(g, expr->call.args.items[0], locals);
    LLVMValueRef a = emit_expr(g, expr->call.args.items[1], locals);
    LLVMValueRef b = emit_expr(g, expr->call.args.items[2], locals);
    LLVMTypeRef mt = LLVMTypeOf(m);
    LLVMValueRef cond = m;
    if (LLVMGetTypeKind(mt) == LLVMVectorTypeKind) {
      LLVMTypeRef me = LLVMGetElementType(mt);
      if (LLVMGetTypeKind(me) == LLVMFloatTypeKind ||
          LLVMGetTypeKind(me) == LLVMDoubleTypeKind)
        cond = LLVMBuildFCmp(g->builder, LLVMRealONE, m, LLVMConstNull(mt),
                             "v.selc");
      else if (LLVMGetIntTypeWidth(me) != 1)
        cond = LLVMBuildICmp(g->builder, LLVMIntNE, m, LLVMConstNull(mt),
                             "v.selc");
    }
    *out = LLVMBuildSelect(g->builder, cond, a, b, "v.sel");
    return true;
  }
  return false;
}

