/* Physical reflection ABI. TypeInfo retains its string header and 15 slots.
 * Each member table owns {record-pool pointer, payload-pool pointer, i32 IDs[]}.
 * Decoding follows the table's pools, including TypeInfo returned by a library;
 * no consumer may substitute its own module's pools. Logical rows exist only
 * until demand projection and are removed before the module is optimized. */
static LLVMValueRef refl_hdr_ptr(zan_irgen_t *g, LLVMValueRef ti, int off,
                                const char *name);

static LLVMTypeRef refl_compact_row_type(zan_irgen_t *g, bool method) {
    LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef elems[9] = { i32, i32, i32, i32, i32, i32,
        LLVMInt64TypeInContext(g->ctx), i32, i32 };
    if (method) {
        elems[6] = i32;
        elems[7] = i32;
    }
    return LLVMStructTypeInContext(g->ctx, elems, method ? 8 : 9, 0);
}

static LLVMTypeRef refl_compact_table_type(zan_irgen_t *g, unsigned n) {
    LLVMTypeRef ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    return LLVMStructTypeInContext(g->ctx, (LLVMTypeRef[]){ ptr, ptr,
        LLVMArrayType(LLVMInt32TypeInContext(g->ctx), n) }, 3, 0);
}

/* The caller checks index bounds/table presence before emitting this load. */
static LLVMValueRef refl_member_row(zan_irgen_t *g, LLVMValueRef table,
                                    LLVMValueRef idx, bool method) {
    LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef header = refl_compact_table_type(g, 0);
    LLVMValueRef ids = LLVMBuildStructGEP2(g->builder, header, table, 2, "refl.ids");
    LLVMValueRef ip = LLVMBuildGEP2(g->builder, i32, ids, &idx, 1, "refl.idp");
    LLVMValueRef id = LLVMBuildLoad2(g->builder, i32, ip, "refl.id");
    LLVMValueRef pool = refl_hdr_ptr(g, table, 0, "refl.pool");
    LLVMValueRef wide = LLVMBuildZExt(g->builder, id,
        LLVMInt64TypeInContext(g->ctx), "refl.id64");
    return LLVMBuildGEP2(g->builder, refl_compact_row_type(g, method),
        pool, &wide, 1, "refl.row");
}

static LLVMValueRef refl_member_u32(zan_irgen_t *g, LLVMValueRef row,
                                    bool method, unsigned slot) {
    LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
    LLVMValueRef p = LLVMBuildStructGEP2(g->builder,
        refl_compact_row_type(g, method), row, slot, "refl.u32p");
    return LLVMBuildZExt(g->builder,
        LLVMBuildLoad2(g->builder, i32, p, "refl.u32"),
        LLVMInt64TypeInContext(g->ctx), "refl.u64");
}

static LLVMValueRef refl_member_ptr(zan_irgen_t *g, LLVMValueRef table,
                                    LLVMValueRef row, bool method,
                                    LLVMValueRef slot) {
    LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef header = refl_compact_table_type(g, 0);
    LLVMValueRef pp = LLVMBuildStructGEP2(g->builder, header, table, 1, "refl.vpp");
    LLVMValueRef pool = LLVMBuildLoad2(g->builder, ptr, pp, "refl.values");
    LLVMValueRef idp = LLVMBuildGEP2(g->builder, i32, row, &slot, 1, "refl.vidp");
    LLVMValueRef id = LLVMBuildZExt(g->builder,
        LLVMBuildLoad2(g->builder, i32, idp, "refl.vid"),
        LLVMInt64TypeInContext(g->ctx), "refl.vid64");
    LLVMValueRef p = LLVMBuildGEP2(g->builder, ptr, pool, &id, 1, "refl.valuep");
    (void)method; /* All payload IDs precede the first non-i32 field. */
    return LLVMBuildLoad2(g->builder, ptr, p, "refl.value");
}

static LLVMValueRef refl_field_ptr(zan_irgen_t *g, LLVMValueRef table,
                                   LLVMValueRef row, unsigned slot) {
    return refl_member_ptr(g, table, row, false,
        LLVMConstInt(LLVMInt64TypeInContext(g->ctx), slot, 0));
}

static LLVMValueRef refl_field_offset(zan_irgen_t *g, LLVMValueRef row) {
    LLVMValueRef p = LLVMBuildStructGEP2(g->builder,
        refl_compact_row_type(g, false), row, 6, "refl.offp");
    return LLVMBuildLoad2(g->builder, LLVMInt64TypeInContext(g->ctx), p, "refl.off");
}

static unsigned refl_method_slot(int logical_off) {
    switch (logical_off) {
    case ZAN_REFL_MO_NAME: return 0;
    case ZAN_REFL_MO_RET: return 1;
    case ZAN_REFL_MO_PTYPES: return 2;
    case ZAN_REFL_MO_THUNK: return 3;
    case ZAN_REFL_MO_ATTRS: return 4;
    case ZAN_REFL_MO_RETKIND: return 5;
    case ZAN_REFL_MO_PCOUNT: return 6;
    default: return 7; /* flags */
    }
}

static LLVMValueRef refl_method_ptr(zan_irgen_t *g, LLVMValueRef table,
                                    LLVMValueRef row, int logical_off) {
    return refl_member_ptr(g, table, row, true,
        LLVMConstInt(LLVMInt64TypeInContext(g->ctx),
                     refl_method_slot(logical_off), 0));
}

static LLVMValueRef refl_method_int(zan_irgen_t *g, LLVMValueRef row,
                                    int logical_off) {
    return refl_member_u32(g, row, true, refl_method_slot(logical_off));
}

/* TypeInfo's direct indexed scalar APIs share this guarded entry. Invalid
 * indices/empty tables never form a load through a null table or payload pool. */
static LLVMValueRef refl_field_scalar_fn(zan_irgen_t *g) {
    const char *name = "__zan_refl_fscalar";
    LLVMValueRef fn = LLVMGetNamedFunction(g->mod, name);
    if (fn) return fn;
    LLVMTypeRef ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef type = LLVMFunctionType(i64, (LLVMTypeRef[]){ptr, i64, i64}, 3, 0);
    fn = LLVMAddFunction(g->mod, name, type);
    LLVMSetLinkage(fn, LLVMInternalLinkage);
    refl_list_point_t saved = refl_list_save(g);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
    LLVMBasicBlockRef load = LLVMAppendBasicBlockInContext(g->ctx, fn, "load");
    LLVMBasicBlockRef flags = LLVMAppendBasicBlockInContext(g->ctx, fn, "flags");
    LLVMBasicBlockRef typeinfo = LLVMAppendBasicBlockInContext(g->ctx, fn, "typeinfo");
    LLVMBasicBlockRef offset = LLVMAppendBasicBlockInContext(g->ctx, fn, "offset");
    LLVMBasicBlockRef bad = LLVMAppendBasicBlockInContext(g->ctx, fn, "bad");
    g->current_fn = fn;
    LLVMPositionBuilderAtEnd(g->builder, entry);
    di_clear(g);
    LLVMValueRef ti = LLVMGetParam(fn, 0), idx = LLVMGetParam(fn, 1);
    LLVMValueRef table = refl_hdr_ptr(g, ti, ZAN_REFL_FIELDS_OFF, "refl.table");
    LLVMValueRef count = refl_hdr_i64(g, ti, ZAN_REFL_COUNT_OFF, "refl.count");
    LLVMValueRef valid = LLVMBuildAnd(g->builder,
        LLVMBuildICmp(g->builder, LLVMIntSGE, idx, LLVMConstInt(i64, 0, 0), "refl.ge"),
        LLVMBuildICmp(g->builder, LLVMIntSLT, idx, count, "refl.lt"), "refl.valid");
    valid = LLVMBuildAnd(g->builder, valid,
        LLVMBuildIsNotNull(g->builder, table, "refl.has"), "refl.live");
    LLVMBuildCondBr(g->builder, valid, load, bad);
    LLVMPositionBuilderAtEnd(g->builder, load);
    LLVMValueRef row = refl_member_row(g, table, idx, false);
    LLVMValueRef sw = LLVMBuildSwitch(g->builder, LLVMGetParam(fn, 2), offset, 2);
    LLVMAddCase(sw, LLVMConstInt(i64, 0, 0), flags);
    LLVMAddCase(sw, LLVMConstInt(i64, 1, 0), typeinfo);
    LLVMPositionBuilderAtEnd(g->builder, flags);
    LLVMBuildRet(g->builder, refl_member_u32(g, row, false, 8));
    LLVMPositionBuilderAtEnd(g->builder, typeinfo);
    LLVMBuildRet(g->builder,
        LLVMBuildPtrToInt(g->builder, refl_field_ptr(g, table, row, 4), i64, "refl.ti"));
    LLVMPositionBuilderAtEnd(g->builder, offset);
    LLVMBuildRet(g->builder, refl_field_offset(g, row));
    LLVMPositionBuilderAtEnd(g->builder, bad);
    LLVMBuildRet(g->builder, LLVMConstInt(i64, 0, 0));
    refl_list_restore(g, saved);
    return fn;
}

/* Intern LLVM-uniqued complete constants. Hash collisions always compare the
 * original LLVM value; neither a name nor the hash establishes equality. */
typedef struct {
    LLVMValueRef *values;
    unsigned *buckets;
    unsigned count, cap, bucket_cap;
} refl_compact_pool_t;

static unsigned refl_compact_hash(LLVMValueRef value) {
    uintptr_t n = (uintptr_t)value;
    n ^= n >> 17;
    n *= (uintptr_t)0xed5ad4bbU;
    n ^= n >> 11;
    return (unsigned)n;
}

static void refl_compact_fail(zan_irgen_t *g, const char *reason) {
    zan_diag_emit_code(g->diag, DIAG_ERROR, (zan_loc_t){0}, 3101,
                      "reflection descriptor compaction failed: %s", reason);
}

static bool refl_compact_intern(refl_compact_pool_t *pool, LLVMValueRef value,
                                unsigned *out) {
    if (pool->count == UINT_MAX) return false;
    if (!pool->bucket_cap || pool->count >= pool->bucket_cap / 2) {
        unsigned cap = pool->bucket_cap ? pool->bucket_cap * 2 : 64;
        if (cap < pool->bucket_cap || cap > SIZE_MAX / sizeof(unsigned)) return false;
        unsigned *buckets = (unsigned *)calloc(cap, sizeof(*buckets));
        if (!buckets) return false;
        for (unsigned i = 0; i < pool->count; i++) {
            unsigned b = refl_compact_hash(pool->values[i]) & (cap - 1);
            while (buckets[b]) b = (b + 1) & (cap - 1);
            buckets[b] = i + 1;
        }
        free(pool->buckets);
        pool->buckets = buckets;
        pool->bucket_cap = cap;
    }
    unsigned b = refl_compact_hash(value) & (pool->bucket_cap - 1);
    while (pool->buckets[b]) {
        unsigned i = pool->buckets[b] - 1;
        if (pool->values[i] == value) { *out = i; return true; }
        b = (b + 1) & (pool->bucket_cap - 1);
    }
    if (pool->count == pool->cap) {
        unsigned cap = pool->cap ? pool->cap * 2 : 32;
        if (cap < pool->cap || cap > SIZE_MAX / sizeof(LLVMValueRef)) return false;
        LLVMValueRef *values = (LLVMValueRef *)realloc(pool->values,
            (size_t)cap * sizeof(*values));
        if (!values) return false;
        pool->values = values;
        pool->cap = cap;
    }
    *out = pool->count;
    pool->values[pool->count++] = value;
    pool->buckets[b] = pool->count;
    return true;
}

static void refl_compact_dispose(refl_compact_pool_t *pool) {
    free(pool->values);
    free(pool->buckets);
}

static LLVMValueRef refl_compact_global(zan_irgen_t *g, LLVMTypeRef type,
                                       LLVMValueRef init, const char *name) {
    LLVMValueRef gv = LLVMAddGlobal(g->mod, type, name);
    LLVMSetInitializer(gv, init);
    LLVMSetLinkage(gv, LLVMPrivateLinkage);
    LLVMSetGlobalConstant(gv, 1);
    LLVMSetUnnamedAddress(gv, LLVMGlobalUnnamedAddr);
    return gv;
}

static LLVMValueRef refl_compact_source(LLVMValueRef value) {
    while (value && LLVMIsAConstantExpr(value)) {
        LLVMOpcode op = LLVMGetConstOpcode(value);
        if (op != LLVMBitCast && op != LLVMAddrSpaceCast) return NULL;
        value = LLVMGetOperand(value, 0);
    }
    return value && LLVMIsAGlobalVariable(value) ? value : NULL;
}

/* Canonicalize parameter arrays by their whole initializer. They otherwise have
 * fresh global addresses for each inherited copy of the same method. */
static LLVMValueRef refl_compact_params(zan_irgen_t *g, LLVMValueRef value,
                                       refl_compact_pool_t *params,
                                       refl_compact_pool_t *param_globals) {
    LLVMValueRef gv = refl_compact_source(value);
    if (!gv && LLVMIsAConstantExpr(value) && LLVMGetConstOpcode(value) == LLVMGetElementPtr)
        gv = refl_compact_source(LLVMGetOperand(value, 0));
    if (!gv || !LLVMIsGlobalConstant(gv)) return value;
    LLVMValueRef init = LLVMGetInitializer(gv);
    if (!init || LLVMGetTypeKind(LLVMTypeOf(init)) != LLVMArrayTypeKind) return value;
    unsigned id;
    if (!refl_compact_intern(params, init, &id)) return NULL;
    if (id == param_globals->count) {
        unsigned ignored;
        if (!refl_compact_intern(param_globals, gv, &ignored)) return NULL;
    }
    LLVMTypeRef ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    return LLVMConstBitCast(param_globals->values[id], ptr);
}

static LLVMValueRef refl_compact_offset(zan_irgen_t *g, LLVMValueRef value) {
    if (LLVMIsAConstantInt(value)) return value;
    /* Logical field offsets use ptrtoint(getelementptr T, null, 0, slot).
     * Distinct derived struct types describe the same inherited byte offset;
     * reduce with the module's target DataLayout before comparing whole rows. */
    if (!LLVMIsAConstantExpr(value) || LLVMGetConstOpcode(value) != LLVMPtrToInt)
        return value;
    LLVMValueRef gep = LLVMGetOperand(value, 0);
    if (!LLVMIsAConstantExpr(gep) || LLVMGetConstOpcode(gep) != LLVMGetElementPtr ||
        LLVMGetNumOperands(gep) != 3 || !LLVMIsNull(LLVMGetOperand(gep, 0))) return value;
    LLVMValueRef first = LLVMGetOperand(gep, 1), slot = LLVMGetOperand(gep, 2);
    LLVMTypeRef st = LLVMGetGEPSourceElementType(gep);
    if (!LLVMIsAConstantInt(first) || LLVMConstIntGetZExtValue(first) != 0 ||
        !LLVMIsAConstantInt(slot) || LLVMGetTypeKind(st) != LLVMStructTypeKind) return value;
    unsigned long long index = LLVMConstIntGetZExtValue(slot);
    if (index >= LLVMCountStructElementTypes(st)) return value;
    LLVMTargetDataRef layout = LLVMGetModuleDataLayout(g->mod);
    return LLVMConstInt(LLVMInt64TypeInContext(g->ctx),
        LLVMOffsetOfElement(layout, st, (unsigned)index), 0);
}

static bool refl_compact_table(zan_irgen_t *g, LLVMValueRef source, bool method,
                               refl_compact_pool_t *rows,
                               refl_compact_pool_t *payloads,
                               refl_compact_pool_t *params,
                               refl_compact_pool_t *param_globals,
                               LLVMValueRef *result) {
    LLVMValueRef init = LLVMGetInitializer(source);
    LLVMTypeRef type = LLVMGlobalGetValueType(source);
    if (!init || LLVMGetTypeKind(type) != LLVMArrayTypeKind ||
        LLVMGetElementType(type) != (method ? refl_method_type(g) : refl_field_type(g))) {
        refl_compact_fail(g, "unexpected logical member table");
        return false;
    }
    unsigned n = LLVMGetArrayLength(type);
    LLVMValueRef *ids = (LLVMValueRef *)calloc(n ? n : 1, sizeof(*ids));
    if (!ids) return false;
    LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef physical = refl_compact_row_type(g, method);
    const unsigned field_ptrs[] = {0, 1, 4, 6, 7, 8};
    const unsigned method_ptrs[] = {0, 1, 4, 5, 7};
    for (unsigned i = 0; i < n; i++) {
        LLVMValueRef row = LLVMGetAggregateElement(init, i);
        LLVMValueRef values[9];
        unsigned ptrs = method ? 5 : 6;
        for (unsigned j = 0; j < ptrs; j++) {
            unsigned src = method ? method_ptrs[j] : field_ptrs[j];
            LLVMValueRef value = LLVMGetAggregateElement(row, src);
            if (method && src == 4)
                value = refl_compact_params(g, value, params, param_globals);
            unsigned id;
            if (!value || !refl_compact_intern(payloads, value, &id)) goto fail;
            values[j] = LLVMConstInt(i32, id, 0);
        }
        const unsigned narrow_method[] = {2, 3, 6};
        const unsigned narrow_field[] = {2, 5};
        for (unsigned j = 0; j < (method ? 3U : 2U); j++) {
            LLVMValueRef value = LLVMGetAggregateElement(row,
                method ? narrow_method[j] : narrow_field[j]);
            if (!LLVMIsAConstantInt(value) || LLVMConstIntGetZExtValue(value) > UINT32_MAX) {
                refl_compact_fail(g, "member scalar exceeds compact ABI range");
                goto fail;
            }
        }
        if (method) {
            values[5] = LLVMConstTrunc(LLVMGetAggregateElement(row, 2), i32);
            values[6] = LLVMConstTrunc(LLVMGetAggregateElement(row, 3), i32);
            values[7] = LLVMConstTrunc(LLVMGetAggregateElement(row, 6), i32);
        } else {
            values[6] = refl_compact_offset(g, LLVMGetAggregateElement(row, 3));
            values[7] = LLVMConstTrunc(LLVMGetAggregateElement(row, 2), i32);
            values[8] = LLVMConstTrunc(LLVMGetAggregateElement(row, 5), i32);
        }
        unsigned id;
        if (!refl_compact_intern(rows,
                LLVMConstNamedStruct(physical, values, method ? 8 : 9), &id)) goto fail;
        ids[i] = LLVMConstInt(i32, id, 0);
    }
    LLVMTypeRef table_type = refl_compact_table_type(g, n);
    LLVMTypeRef ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMValueRef pool = method ? g->refl_method_pool : g->refl_field_pool;
    *result = refl_compact_global(g, table_type,
        LLVMConstNamedStruct(table_type, (LLVMValueRef[]){
            LLVMConstBitCast(pool, ptr), LLVMConstBitCast(g->refl_value_pool, ptr),
            LLVMConstArray(i32, ids, n) }, 3), "__zan.refl.compact.table");
    free(ids);
    return true;
fail:
    free(ids);
    return false;
}

static bool refl_compact_contains(const refl_compact_pool_t *pool, LLVMValueRef value) {
    if (!pool->bucket_cap) return false;
    unsigned b = refl_compact_hash(value) & (pool->bucket_cap - 1);
    while (pool->buckets[b]) {
        if (pool->values[pool->buckets[b] - 1] == value) return true;
        b = (b + 1) & (pool->bucket_cap - 1);
    }
    return false;
}

static bool refl_compact_is_root(LLVMValueRef value) {
    LLVMLinkage linkage = LLVMGetLinkage(value);
    if (LLVMGetDLLStorageClass(value) == LLVMDLLExportStorageClass) return true;
    return linkage != LLVMPrivateLinkage && linkage != LLVMInternalLinkage &&
        linkage != LLVMAvailableExternallyLinkage &&
        linkage != LLVMLinkOnceAnyLinkage && linkage != LLVMLinkOnceODRLinkage &&
        linkage != LLVMWeakODRLinkage;
}

/* A shared pool must never make an otherwise dead metadata cycle live. Trace
 * forward from emitted/exported definitions and appending roots before adding
 * any pool edges. Constants/functions are visited once, so cycles do not root
 * themselves. This mirrors GlobalDCE's conservative symbol reachability; later
 * instruction optimization can still discard roots, but no unrooted descriptor
 * is introduced into a live pool by merely having been generated earlier. */
static bool refl_compact_live_values(zan_irgen_t *g, refl_compact_pool_t *live) {
    unsigned ignored;
    for (LLVMValueRef f = LLVMGetFirstFunction(g->mod); f; f = LLVMGetNextFunction(f))
        if (!LLVMIsDeclaration(f) && refl_compact_is_root(f) &&
            !refl_compact_intern(live, f, &ignored)) return false;
    for (LLVMValueRef v = LLVMGetFirstGlobal(g->mod); v; v = LLVMGetNextGlobal(v))
        if (LLVMGetInitializer(v) && refl_compact_is_root(v) &&
            !refl_compact_intern(live, v, &ignored)) return false;
    for (LLVMValueRef a = LLVMGetFirstGlobalAlias(g->mod); a; a = LLVMGetNextGlobalAlias(a))
        if (refl_compact_is_root(a) && !refl_compact_intern(live, a, &ignored)) return false;
    for (unsigned cursor = 0; cursor < live->count; cursor++) {
        LLVMValueRef v = live->values[cursor];
        if (LLVMIsAFunction(v)) {
            for (LLVMBasicBlockRef bb = LLVMGetFirstBasicBlock(v); bb; bb = LLVMGetNextBasicBlock(bb))
                for (LLVMValueRef inst = LLVMGetFirstInstruction(bb); inst;
                     inst = LLVMGetNextInstruction(inst)) {
                    int n = LLVMGetNumOperands(inst);
                    for (int i = 0; i < n; i++) {
                        LLVMValueRef operand = LLVMGetOperand(inst, (unsigned)i);
                        if (LLVMIsConstant(operand) &&
                            !refl_compact_intern(live, operand, &ignored)) return false;
                    }
                }
        } else if (LLVMIsAGlobalVariable(v)) {
            LLVMValueRef init = LLVMGetInitializer(v);
            if (init && !refl_compact_intern(live, init, &ignored)) return false;
        } else if (LLVMIsAConstantExpr(v) || LLVMIsAConstantArray(v) ||
                   LLVMIsAConstantStruct(v) || LLVMIsAConstantVector(v) ||
                   LLVMIsAGlobalAlias(v) || LLVMIsABlockAddress(v)) {
            int n = LLVMGetNumOperands(v);
            for (int i = 0; i < n; i++) {
                LLVMValueRef operand = LLVMGetOperand(v, (unsigned)i);
                if (!refl_compact_intern(live, operand, &ignored)) return false;
            }
        }
    }
    return true;
}

void refl_compact_member_tables(zan_irgen_t *g) {
    if (!g->refl_meta_count) return;
    LLVMTypeRef ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    /* Forward globals keep cyclic field TypeInfo references constant. RAUW below
     * replaces these declarations with the final, exactly sized arrays. */
    g->refl_field_pool = LLVMAddGlobal(g->mod,
        refl_compact_row_type(g, false), "__zan.refl.compact.fields.pending");
    g->refl_method_pool = LLVMAddGlobal(g->mod,
        refl_compact_row_type(g, true), "__zan.refl.compact.methods.pending");
    g->refl_value_pool = LLVMAddGlobal(g->mod, ptr, "__zan.refl.compact.values.pending");
    refl_compact_pool_t fields = {0}, methods = {0}, payloads = {0};
    refl_compact_pool_t params = {0}, param_globals = {0}, sources = {0}, targets = {0};
    refl_compact_pool_t live = {0};
    unsigned ignored;
    bool ok = refl_compact_live_values(g, &live) &&
        refl_compact_intern(&payloads, LLVMConstNull(ptr), &ignored);
    for (int i = 0; ok && i < g->refl_meta_count; i++) {
        if (!refl_compact_contains(&live, g->refl_metas[i].gv)) continue;
        LLVMValueRef init = LLVMGetInitializer(g->refl_metas[i].gv);
        if (!init || LLVMIsNull(init)) continue;
        const unsigned slots[] = {9, 3, 5};
        for (unsigned k = 0; ok && k < 3; k++) {
            LLVMValueRef value = LLVMGetAggregateElement(init, slots[k]);
            if (LLVMIsNull(value)) continue;
            LLVMValueRef source = refl_compact_source(value), target;
            if (!source) { ok = false; break; }
            unsigned id;
            ok = refl_compact_intern(&sources, source, &id);
            if (!ok || id < targets.count) continue;
            bool method = k != 0;
            ok = refl_compact_table(g, source, method, method ? &methods : &fields,
                &payloads, &params, &param_globals, &target);
            if (ok) ok = refl_compact_intern(&targets, target, &ignored);
        }
    }
    if (ok) {
        /* Sever only proven-unrooted metadata. Keep the string-shaped blob so
         * compiler-side caches never retain a deleted TypeInfo LLVM handle. */
        for (int i = 0; i < g->refl_meta_count; i++) {
            LLVMValueRef gv = g->refl_metas[i].gv;
            if (!refl_compact_contains(&live, gv))
                LLVMSetInitializer(gv, LLVMConstNull(LLVMGlobalGetValueType(gv)));
        }
        refl_compact_pool_t *pools[] = { &fields, &methods, &payloads };
        LLVMValueRef *globals[] = { &g->refl_field_pool, &g->refl_method_pool,
                                    &g->refl_value_pool };
        const char *names[] = { "__zan.refl.compact.fields", "__zan.refl.compact.methods",
                                "__zan.refl.compact.values" };
        LLVMTypeRef types[] = { refl_compact_row_type(g, false),
                                refl_compact_row_type(g, true), ptr };
        for (unsigned i = 0; i < 3; i++) {
            LLVMTypeRef array = LLVMArrayType(types[i], pools[i]->count);
            LLVMValueRef gv = refl_compact_global(g, array,
                LLVMConstArray(types[i], pools[i]->values, pools[i]->count), names[i]);
            LLVMReplaceAllUsesWith(*globals[i], LLVMConstBitCast(gv, LLVMTypeOf(*globals[i])));
            LLVMDeleteGlobal(*globals[i]);
            *globals[i] = gv;
        }
        for (unsigned i = 0; i < sources.count; i++) {
            LLVMReplaceAllUsesWith(sources.values[i],
                LLVMConstBitCast(targets.values[i], LLVMTypeOf(sources.values[i])));
            LLVMDeleteGlobal(sources.values[i]);
        }
        for (int i = 0; i < g->refl_mtab_count; i++) {
            LLVMValueRef old = g->refl_mtabs[i].gv;
            for (unsigned j = 0; j < sources.count; j++)
                if (old == sources.values[j]) {
                    g->refl_mtabs[i].gv = targets.values[j];
                    break;
                }
        }
        /* Clear unreachable logical rows and parameter arrays, including
         * constants in dead cycles. GetFirstUse is deliberately not a liveness
         * criterion: the forward root closure above already made that decision. */
        for (LLVMValueRef gv = LLVMGetFirstGlobal(g->mod); gv; ) {
            LLVMValueRef next = LLVMGetNextGlobal(gv);
            LLVMTypeRef type = LLVMGlobalGetValueType(gv);
            const char *name = LLVMGetValueName(gv);
            bool old_rows = LLVMGetTypeKind(type) == LLVMArrayTypeKind &&
                (LLVMGetElementType(type) == refl_field_type(g) ||
                 LLVMGetElementType(type) == refl_method_type(g));
            bool old_params = name &&
                (strncmp(name, "__zan.refl.ptypes.", 18) == 0 ||
                 strncmp(name, "__zan.refl.pnames.", 18) == 0) &&
                !refl_compact_contains(&param_globals, gv);
            if (old_rows || old_params) {
                for (int i = 0; i < g->refl_mtab_count; i++)
                    if (g->refl_mtabs[i].gv == gv) g->refl_mtabs[i].gv = NULL;
                LLVMReplaceAllUsesWith(gv, LLVMConstNull(LLVMTypeOf(gv)));
                LLVMDeleteGlobal(gv);
            }
            gv = next;
        }
    } else {
        refl_compact_fail(g, "out of memory or invalid member table");
    }
    refl_compact_pool_t *all[] = { &fields, &methods, &payloads, &params,
                                  &param_globals, &sources, &targets, &live };
    for (unsigned i = 0; i < sizeof(all) / sizeof(all[0]); i++)
        refl_compact_dispose(all[i]);
}
