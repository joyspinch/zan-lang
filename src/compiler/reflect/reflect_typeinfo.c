static LLVMValueRef refl_emit_get_type(zan_irgen_t *g, zan_type_t *st,
                                       LLVMValueRef obj) {
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMValueRef fallback = refl_meta_for(g, st, NULL, 0);
    if (!st || (st->kind != TYPE_CLASS && st->kind != TYPE_INTERFACE &&
                st->kind != TYPE_OBJECT))
        return fallback;
    if (!obj || LLVMGetTypeKind(LLVMTypeOf(obj)) != LLVMPointerTypeKind)
        return fallback;
    obj = LLVMBuildBitCast(g->builder, obj, i8ptr, "refl.obj");
    LLVMValueRef fn = refl_obj_type_fn(g);
    return zan_call2(g->builder,
        LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0),
        fn, (LLVMValueRef[]){ obj, fallback }, 2, "refl.ty");
}

static LLVMValueRef refl_receiver_ptr(zan_irgen_t *g, zan_type_t *rt,
                                      LLVMValueRef v) {
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    if (!v) return LLVMConstNull(i8ptr);
    LLVMTypeRef vt = LLVMTypeOf(v);
    if (LLVMGetTypeKind(vt) == LLVMPointerTypeKind)
        return LLVMBuildBitCast(g->builder, v, i8ptr, "refl.recv");
    if (rt && rt->kind == TYPE_STRUCT) {
        LLVMValueRef tmp = LLVMBuildAlloca(g->builder, vt, "refl.spill");
        LLVMBuildStore(g->builder, v, tmp);
        return LLVMBuildBitCast(g->builder, tmp, i8ptr, "refl.recv");
    }
    return LLVMConstNull(i8ptr);
}

static LLVMValueRef refl_mi64(zan_irgen_t *g, LLVMValueRef ti, int tbl,
                              LLVMValueRef idx, int which) {
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    return zan_call2(g->builder,
        LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr, i64, i64, i64 }, 4, 0),
        refl_mi64_fn(g), (LLVMValueRef[]){ ti,
            LLVMConstInt(i64, (unsigned long long)tbl, 0), idx,
            LLVMConstInt(i64, (unsigned long long)which, 0) }, 4, "refl.mi");
}

static LLVMValueRef refl_mstr(zan_irgen_t *g, LLVMValueRef ti, int tbl,
                              LLVMValueRef idx, int which, LLVMValueRef k) {
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    return zan_call2(g->builder,
        LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i64, i64, i64, i64 },
                         5, 0),
        refl_mstr_fn(g), (LLVMValueRef[]){ ti,
            LLVMConstInt(i64, (unsigned long long)tbl, 0), idx,
            LLVMConstInt(i64, (unsigned long long)which, 0), k }, 5, "refl.ms");
}

static LLVMValueRef refl_mfind(zan_irgen_t *g, LLVMValueRef ti,
                               LLVMValueRef name, int flags) {
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    return zan_call2(g->builder,
        LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr, i8ptr, i64 }, 3, 0),
        refl_mfind_fn(g), (LLVMValueRef[]){ ti, name,
            LLVMConstInt(i64, (unsigned long long)flags, 0) }, 3, "refl.mfi");
}

static LLVMValueRef refl_arg_index(zan_irgen_t *g, zan_ast_node_t *arg,
                                   local_scope_t *locals) {
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    if (!arg) return LLVMConstInt(i64, 0, 0);
    return emit_index_i64(g, emit_expr(g, arg, locals), "refl.i");
}

static LLVMValueRef refl_arg_str(zan_irgen_t *g, zan_ast_node_t *arg,
                                 local_scope_t *locals) {
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMValueRef v = arg ? emit_expr(g, arg, locals) : refl_empty_string(g);
    return LLVMBuildBitCast(g->builder, v, i8ptr, "refl.nm");
}

typedef struct {
    LLVMValueRef values[ZAN_REFL_ARG_SLOTS];
    int pushed;
} refl_arg_temps_t;

static LLVMValueRef refl_pack_args(zan_irgen_t *g, zan_ast_node_t **args,
                                   int argc, int from,
                                   local_scope_t *locals,
                                   refl_arg_temps_t *temps) {
    memset(temps, 0, sizeof(*temps));
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMValueRef slots = LLVMBuildArrayAlloca(g->builder, i64,
        LLVMConstInt(i64, ZAN_REFL_ARG_SLOTS, 0), "refl.args");
    for (int i = 0; i < ZAN_REFL_ARG_SLOTS; i++) {
        LLVMValueRef off = LLVMConstInt(i64, (unsigned long long)i, 0);
        LLVMValueRef p = LLVMBuildGEP2(g->builder, i64, slots, &off, 1,
                                       "refl.as");
        int ai = from + i;
        LLVMValueRef v = LLVMConstInt(i64, 0, 0);
        if (args && ai < argc) {
            LLVMValueRef value = emit_expr(g, args[ai], locals);
            temps->values[i] = value;
            /* Reuse normal calls' exception protocol: keep each fresh argument
             * alive across evaluation of later arguments and the thunk call. */
            temps->pushed += emit_call_arg_eh_push(g, args[ai], value, locals);
            v = refl_to_slot(g, value);
        }
        LLVMBuildStore(g->builder, v, p);
    }
    return slots;
}

static void refl_release_args(zan_irgen_t *g, zan_ast_node_t **args,
                              int argc, int from, local_scope_t *locals,
                              refl_arg_temps_t *temps) {
    for (int i = 0; i < temps->pushed; i++) emit_eh_tmp_pop(g);
    for (int i = ZAN_REFL_ARG_SLOTS - 1; i >= 0; i--) {
        if (args && from + i < argc && temps->values[i])
            emit_release_owned_call_temp(g, args[from + i], temps->values[i], locals);
    }
}

/* `ti */
bool refl_emit_typeinfo_member(zan_irgen_t *g, LLVMValueRef ti,
                                      zan_istr_t name,
                                      zan_ast_node_t **args, int argc,
                                      local_scope_t *locals,
                                      LLVMValueRef *out) {
    int code = 0;
    if (!zan_refl_typeinfo_member(name, &code)) return false;
    LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
    LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i64ptr = LLVMPointerType(i64, 0);
    zan_ast_node_t *arg0 = argc > 0 ? args[0] : NULL;
    zan_ast_node_t *arg1 = argc > 1 ? args[1] : NULL;
    ti = LLVMBuildBitCast(g->builder, ti, i8ptr, "refl.ti");
    switch (code) {
    case ZAN_REFL_M_MCOUNT:      /* MethodCount / ConstructorCount */
    case ZAN_REFL_M_CCOUNT: {
        LLVMValueRef c = refl_mi64(g, ti,
            code == ZAN_REFL_M_MCOUNT ? ZAN_REFL_TBL_METHOD : ZAN_REFL_TBL_CTOR,
            LLVMConstInt(i64, 0, 0), ZAN_REFL_MI_COUNT);
        *out = LLVMBuildTrunc(g->builder, c, i32, "refl.mc32");
        return true;
    }
    case ZAN_REFL_M_MNAME:       /* GetMethodName / GetMethodReturnType / GetMethodCustomAttributes */
    case ZAN_REFL_M_MRET:
    case ZAN_REFL_M_MATTRS: {
        LLVMValueRef idx = refl_arg_index(g, arg0, locals);
        int mw = code == ZAN_REFL_M_MNAME ? ZAN_REFL_MW_NAME :
                 (code == ZAN_REFL_M_MRET ? ZAN_REFL_MW_RET : ZAN_REFL_MW_ATTRS);
        *out = refl_mstr(g, ti, ZAN_REFL_TBL_METHOD, idx, mw,
            LLVMConstInt(i64, 0, 0));
        return true;
    }
    case ZAN_REFL_M_MPCOUNT:     /* GetMethodParamCount / GetCtorParamCount */
    case ZAN_REFL_M_CPCOUNT: {
        LLVMValueRef idx = refl_arg_index(g, arg0, locals);
        LLVMValueRef c = refl_mi64(g, ti,
            code == ZAN_REFL_M_MPCOUNT ? ZAN_REFL_TBL_METHOD : ZAN_REFL_TBL_CTOR,
            idx, ZAN_REFL_MI_PCOUNT);
        *out = LLVMBuildTrunc(g->builder, c, i32, "refl.pc32");
        return true;
    }
    case ZAN_REFL_M_MPTYPE:      /* GetMethodParamType / GetCtorParamType */
    case ZAN_REFL_M_CPTYPE: {
        LLVMValueRef idx = refl_arg_index(g, arg0, locals);
        LLVMValueRef k = refl_arg_index(g, arg1, locals);
        *out = refl_mstr(g, ti,
            code == ZAN_REFL_M_MPTYPE ? ZAN_REFL_TBL_METHOD : ZAN_REFL_TBL_CTOR,
            idx, ZAN_REFL_MW_PARAM, k);
        return true;
    }
    case ZAN_REFL_M_MHAS: {      /* HasMethod(name) */
        LLVMValueRef nm = refl_arg_str(g, arg0, locals);
        *out = LLVMBuildICmp(g->builder, LLVMIntSGE,
            refl_mfind(g, ti, nm, 0), LLVMConstInt(i64, 0, 0), "refl.mhas");
        /* 实参 temp 归属：拼接/调用产出的 fresh owned 名串在 mfind 消费完
         * 释放；局部变量/字段/字面量是借用，不得动（否则双重释放）。
         * 曾经不管不问——ORM 的 `HasField(a + b)` 每次调用漏一个串
         * （Include 循环里每轮 3 个，probe pQL5/pQL8）。 */
        if (arg0) emit_release_owned_call_temp(g, arg0, nm, locals);
        return true;
    }
    case ZAN_REFL_M_MSTATIC: {   /* IsMethodStatic(i) */
        LLVMValueRef idx = refl_arg_index(g, arg0, locals);
        LLVMValueRef fl = refl_mi64(g, ti, ZAN_REFL_TBL_METHOD, idx,
                                    ZAN_REFL_MI_FLAGS);
        *out = LLVMBuildICmp(g->builder, LLVMIntNE,
            LLVMBuildAnd(g->builder, fl,
                LLVMConstInt(i64, ZAN_REFL_MF_STATIC, 0), "refl.stand"),
            LLVMConstInt(i64, 0, 0), "refl.isst");
        return true;
    }
    case ZAN_REFL_M_CNEW: {      /* CreateInstance(...) */
        refl_arg_temps_t temps;
        LLVMValueRef slots = refl_pack_args(g, args, argc, 0, locals, &temps);
        LLVMValueRef idx = zan_call2(g->builder,
            LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr, i64 }, 2, 0),
            refl_cfind_fn(g), (LLVMValueRef[]){ ti,
                LLVMConstInt(i64, (unsigned long long)argc, 0) }, 2, "refl.ci");
        LLVMValueRef kout = LLVMBuildAlloca(g->builder, i64, "refl.nk");
        LLVMValueRef v = zan_call_or_invoke(g, g->builder,
            LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr, i64, i64, i8ptr,
                                                   i64ptr, i64ptr }, 6, 0),
            refl_invoke_fn(g), (LLVMValueRef[]){ ti,
                LLVMConstInt(i64, ZAN_REFL_TBL_CTOR, 0), idx,
                LLVMConstNull(i8ptr), slots, kout }, 6, "refl.new");
        refl_release_args(g, args, argc, 0, locals, &temps);
        *out = LLVMBuildIntToPtr(g->builder,
            LLVMBuildSelect(g->builder,
                LLVMBuildICmp(g->builder, LLVMIntEQ,
                    LLVMBuildLoad2(g->builder, i64, kout, "refl.nkv"),
                    LLVMConstInt(i64, 0, 0), "refl.nofail"),
                LLVMConstInt(i64, 0, 0), v, "refl.nsel"), i8ptr, "refl.nobj");
        return true;
    }
    case ZAN_REFL_M_ISARRAY:     /* IsArray / IsNullable / IsInterface */
    case ZAN_REFL_M_ISNULLABLE:
    case ZAN_REFL_M_ISIFACE: {
        int want = code == ZAN_REFL_M_ISARRAY    ? ZAN_REFL_TK_ARRAY
                 : code == ZAN_REFL_M_ISNULLABLE ? ZAN_REFL_TK_NULLABLE
                                                 : ZAN_REFL_TK_INTERFACE;
        *out = LLVMBuildICmp(g->builder, LLVMIntEQ,
            refl_hdr_i64(g, ti, ZAN_REFL_KIND_OFF, "refl.tk"),
            LLVMConstInt(i64, (unsigned long long)want, 0), "refl.iskind");
        return true;
    }
    case ZAN_REFL_M_ISGENERIC:   /* IsGeneric */
        *out = LLVMBuildIsNotNull(g->builder,
            refl_hdr_ptr(g, ti, ZAN_REFL_TARGS_OFF, "refl.ta"), "refl.isgen");
        return true;
    case ZAN_REFL_M_ELEMTYPE: {  /* ElementType */
        LLVMValueRef e = refl_hdr_ptr(g, ti, ZAN_REFL_ELEM_OFF, "refl.el");
        *out = LLVMBuildSelect(g->builder,
            LLVMBuildIsNull(g->builder, e, "refl.elnull"),
            refl_meta_for(g, NULL, NULL, 0), e, "refl.elt");
        return true;
    }
    case ZAN_REFL_M_TACOUNT: {   /* TypeArgCount */
        LLVMValueRef c = zan_call2(g->builder,
            LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr, i64, i64 }, 3, 0),
            refl_tainfo_fn(g), (LLVMValueRef[]){ ti, LLVMConstInt(i64, 0, 0),
                LLVMConstInt(i64, 0, 0) }, 3, "refl.tac");
        *out = LLVMBuildTrunc(g->builder, c, i32, "refl.tac32");
        return true;
    }
    case ZAN_REFL_M_TARG: {      /* GetTypeArg(i) */
        LLVMValueRef idx = refl_arg_index(g, arg0, locals);
        LLVMValueRef v = zan_call2(g->builder,
            LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr, i64, i64 }, 3, 0),
            refl_tainfo_fn(g), (LLVMValueRef[]){ ti, idx,
                LLVMConstInt(i64, 1, 0) }, 3, "refl.tav");
        *out = LLVMBuildSelect(g->builder,
            LLVMBuildICmp(g->builder, LLVMIntEQ, v, LLVMConstInt(i64, 0, 0),
                          "refl.tanull"),
            refl_meta_for(g, NULL, NULL, 0),
            LLVMBuildIntToPtr(g->builder, v, i8ptr, "refl.tap"), "refl.targ");
        return true;
    }
    }
    switch (code) {
    case ZAN_REFL_M_NAME:
        *out = ti;
        return true;
    case ZAN_REFL_M_FULLNAME: /* B-ID107：全限定名，-112 槽 */
        *out = refl_hdr_ptr(g, ti, ZAN_REFL_FULLNAME_OFF, "refl.full");
        return true;
    case ZAN_REFL_M_TABLENAME:
        *out = refl_hdr_ptr(g, ti, ZAN_REFL_TABLE_OFF, "refl.tbl");
        return true;
    case ZAN_REFL_M_TATTRS:
        *out = refl_hdr_ptr(g, ti, ZAN_REFL_ATTRS_OFF, "refl.tattrs");
        return true;
    case ZAN_REFL_M_KIND: {   /* Kind */
        LLVMValueRef k = refl_hdr_i64(g, ti, ZAN_REFL_KIND_OFF, "refl.tk");
        const char *names[] = { "other", "class", "struct", "enum",
                                "interface", "primitive", "other",
                                "array", "nullable", "delegate" };
        LLVMValueRef v = refl_const_string(g, names[0], 5);
        for (int i = 1; i <= ZAN_REFL_TK_LAST; i++) {
            LLVMValueRef s = refl_const_string(g, names[i], (int)strlen(names[i]));
            v = LLVMBuildSelect(g->builder,
                LLVMBuildICmp(g->builder, LLVMIntEQ, k,
                              LLVMConstInt(i64, (unsigned long long)i, 0), "refl.kq"),
                s, v, "refl.kn");
        }
        *out = v;
        return true;
    }
    case ZAN_REFL_M_FCOUNT: { /* FieldCount */
        LLVMValueRef c = refl_hdr_i64(g, ti, ZAN_REFL_COUNT_OFF, "refl.fc");
        *out = LLVMBuildTrunc(g->builder, c, i32, "refl.fc32");
        return true;
    }
    case ZAN_REFL_M_FNAME:
    case ZAN_REFL_M_FTYPE:
    case ZAN_REFL_M_FATTRS:
    case ZAN_REFL_M_FCUSTOMNAME:
    case ZAN_REFL_M_FDEFAULT: {  /* GetFieldName / GetFieldType / GetFieldCustomName / GetFieldDefault */
        LLVMValueRef idx = arg0 ? emit_expr(g, arg0, locals)
                                : LLVMConstInt(i64, 0, 0);
        idx = emit_index_i64(g, idx, "refl.i");
        int which = ZAN_REFL_WHICH_NAME;
        if (code == ZAN_REFL_M_FTYPE) which = ZAN_REFL_WHICH_TYPE;
        else if (code == ZAN_REFL_M_FCUSTOMNAME) which = ZAN_REFL_WHICH_CUSTOM;
        else if (code == ZAN_REFL_M_FDEFAULT) which = ZAN_REFL_WHICH_DEFAULT;
        else if (code == ZAN_REFL_M_FATTRS) which = ZAN_REFL_WHICH_ATTRS;
        *out = zan_call2(g->builder,
            LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i64, i64 }, 3, 0),
            refl_fname_fn(g), (LLVMValueRef[]){ ti, idx,
                LLVMConstInt(i64, (unsigned long long)which, 0) },
            3, "refl.fname");
        return true;
    }
    case ZAN_REFL_M_FIGNORED:
    case ZAN_REFL_M_FREQUIRED:
    case ZAN_REFL_M_FPK:
    case ZAN_REFL_M_FIDENT: {    /* IsFieldIgnored / IsFieldRequired / IsFieldPk / IsFieldIdentity */
        LLVMValueRef idx = arg0 ? emit_expr(g, arg0, locals)
                                : LLVMConstInt(i64, 0, 0);
        idx = emit_index_i64(g, idx, "refl.i");
        LLVMValueRef fields = refl_hdr_ptr(g, ti, ZAN_REFL_FIELDS_OFF, "refl.fl");
        LLVMValueRef count = refl_hdr_i64(g, ti, ZAN_REFL_COUNT_OFF, "refl.fc");
        LLVMValueRef ok = LLVMBuildAnd(g->builder,
            LLVMBuildICmp(g->builder, LLVMIntSGE, idx,
                          LLVMConstInt(i64, 0, 0), "refl.ge"),
            LLVMBuildICmp(g->builder, LLVMIntSLT, idx, count, "refl.lt"),
            "refl.ok");
        LLVMValueRef safe = LLVMBuildSelect(g->builder, ok, idx,
                                            LLVMConstInt(i64, 0, 0), "refl.si");
        LLVMValueRef off = LLVMBuildAdd(g->builder,
            LLVMBuildMul(g->builder, safe,
                         LLVMConstInt(i64, ZAN_REFL_FIELD_SIZE, 0), "refl.fo"),
            LLVMConstInt(i64, 40, 0), "refl.flgo");
        LLVMValueRef zero_slot = LLVMBuildAlloca(g->builder, i64, "refl.z");
        LLVMBuildStore(g->builder, LLVMConstInt(i64, 0, 0), zero_slot);
        LLVMValueRef p = LLVMBuildGEP2(g->builder, i8, fields, &off, 1, "refl.flgp");
        p = LLVMBuildBitCast(g->builder, p, LLVMPointerType(i64, 0), "refl.flgpc");
        LLVMValueRef live = LLVMBuildAnd(g->builder, ok,
            LLVMBuildIsNotNull(g->builder, fields, "refl.fl.nn"), "refl.live");
        p = LLVMBuildSelect(g->builder, live, p, zero_slot, "refl.flgsel");
        LLVMValueRef flags = LLVMBuildLoad2(g->builder, i64, p, "refl.flgv");
        unsigned long long bit = 1;
        if (code == ZAN_REFL_M_FREQUIRED) bit = 2;
        else if (code == ZAN_REFL_M_FPK) bit = 4;
        else if (code == ZAN_REFL_M_FIDENT) bit = 8;
        LLVMValueRef masked = LLVMBuildAnd(g->builder, flags,
            LLVMConstInt(i64, bit, 0), "refl.masked");
        *out = LLVMBuildICmp(g->builder, LLVMIntNE, masked,
            LLVMConstInt(i64, 0, 0), "refl.flagbool");
        return true;
    }
    case ZAN_REFL_M_FTYPEINFO: {  /* GetFieldTypeInfo(idx) -> TypeInfo */
        LLVMValueRef idx = arg0 ? emit_expr(g, arg0, locals)
                                : LLVMConstInt(i64, 0, 0);
        idx = emit_index_i64(g, idx, "refl.i");
        LLVMValueRef fields = refl_hdr_ptr(g, ti, ZAN_REFL_FIELDS_OFF, "refl.fl");
        LLVMValueRef count = refl_hdr_i64(g, ti, ZAN_REFL_COUNT_OFF, "refl.fc");
        LLVMValueRef ok = LLVMBuildAnd(g->builder,
            LLVMBuildICmp(g->builder, LLVMIntSGE, idx,
                          LLVMConstInt(i64, 0, 0), "refl.ge"),
            LLVMBuildICmp(g->builder, LLVMIntSLT, idx, count, "refl.lt"),
            "refl.ok");
        LLVMValueRef safe = LLVMBuildSelect(g->builder, ok, idx,
                                            LLVMConstInt(i64, 0, 0), "refl.si");
        LLVMValueRef off = LLVMBuildAdd(g->builder,
            LLVMBuildMul(g->builder, safe,
                         LLVMConstInt(i64, ZAN_REFL_FIELD_SIZE, 0), "refl.fo"),
            LLVMConstInt(i64, 56, 0), "refl.tio");
        LLVMValueRef zero_slot = LLVMBuildAlloca(g->builder, i8ptr, "refl.zt");
        LLVMBuildStore(g->builder, LLVMConstNull(i8ptr), zero_slot);
        LLVMValueRef p = LLVMBuildGEP2(g->builder, i8, fields, &off, 1, "refl.tip");
        p = LLVMBuildBitCast(g->builder, p, LLVMPointerType(i8ptr, 0), "refl.tipc");
        LLVMValueRef live = LLVMBuildAnd(g->builder, ok,
            LLVMBuildIsNotNull(g->builder, fields, "refl.fl.nn"), "refl.live");
        p = LLVMBuildSelect(g->builder, live, p, zero_slot, "refl.tsel");
        *out = LLVMBuildLoad2(g->builder, i8ptr, p, "refl.fti");
        return true;
    }
    case ZAN_REFL_M_HAS: {    /* HasField(name) */
        LLVMValueRef nm = arg0 ? emit_expr(g, arg0, locals)
                               : refl_empty_string(g);
        LLVMValueRef found = zan_call2(g->builder,
            LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0),
            refl_find_fn(g), (LLVMValueRef[]){ ti,
                LLVMBuildBitCast(g->builder, nm, i8ptr, "refl.nm") }, 2, "refl.f");
        /* 实参 temp 归属：同 M_MHAS——fresh owned 名串消费完释放，
         * 借用实参不动。 */
        if (arg0) emit_release_owned_call_temp(g, arg0, nm, locals);
        *out = LLVMBuildICmp(g->builder, LLVMIntSGE, found,
                             LLVMConstInt(i64, 0, 0), "refl.has");
        return true;
    }
    default: {         /* GetMemberValue(i) */
        LLVMValueRef idx = arg0 ? emit_expr(g, arg0, locals)
                                : LLVMConstInt(i64, 0, 0);
        idx = emit_index_i64(g, idx, "refl.i");
        LLVMValueRef fields = refl_hdr_ptr(g, ti, ZAN_REFL_FIELDS_OFF, "refl.fl");
        LLVMValueRef count = refl_hdr_i64(g, ti, ZAN_REFL_COUNT_OFF, "refl.fc");
        LLVMValueRef ok = LLVMBuildAnd(g->builder,
            LLVMBuildICmp(g->builder, LLVMIntSGE, idx,
                          LLVMConstInt(i64, 0, 0), "refl.ge"),
            LLVMBuildICmp(g->builder, LLVMIntSLT, idx, count, "refl.lt"),
            "refl.ok");
        LLVMValueRef safe = LLVMBuildSelect(g->builder, ok, idx,
                                            LLVMConstInt(i64, 0, 0), "refl.si");
        LLVMValueRef off = LLVMBuildAdd(g->builder,
            LLVMBuildMul(g->builder, safe,
                         LLVMConstInt(i64, ZAN_REFL_FIELD_SIZE, 0), "refl.fo"),
            LLVMConstInt(i64, 24, 0), "refl.vo");
        LLVMValueRef zero_slot = LLVMBuildAlloca(g->builder, i64, "refl.z");
        LLVMBuildStore(g->builder, LLVMConstInt(i64, 0, 0), zero_slot);
        LLVMValueRef p = LLVMBuildGEP2(g->builder, i8, fields, &off, 1, "refl.vp");
        p = LLVMBuildBitCast(g->builder, p, LLVMPointerType(i64, 0), "refl.vpc");
        LLVMValueRef live = LLVMBuildAnd(g->builder, ok,
            LLVMBuildIsNotNull(g->builder, fields, "refl.fl.nn"), "refl.live");
        p = LLVMBuildSelect(g->builder, live, p, zero_slot, "refl.vsel");
        *out = LLVMBuildLoad2(g->builder, i64, p, "refl.mv");
        return true;
    }
    }
}

/* `obj */
static bool refl_emit_instance_call_1(zan_irgen_t *g, zan_type_t *rt,
                                      LLVMValueRef recv, zan_istr_t name,
                                      zan_ast_node_t **args, int argc,
                                      local_scope_t *locals, LLVMValueRef *out,
                                      LLVMValueRef *nm_tmp,
                                      LLVMValueRef *val_tmp) {
    int code = 0;
    if (!zan_refl_instance_method(name, &code)) return false;
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i64ptr = LLVMPointerType(i64, 0);
    LLVMTypeRef dbl = LLVMDoubleTypeInContext(g->ctx);
    zan_ast_node_t *arg0 = argc > 0 ? args[0] : NULL;
    zan_ast_node_t *arg1 = argc > 1 ? args[1] : NULL;
    if (code == ZAN_REFL_M_GETTYPE) {
        *out = refl_emit_get_type(g, rt, recv);
        return true;
    }
    LLVMValueRef ti = refl_emit_get_type(g, rt, recv);
    LLVMValueRef obj = refl_receiver_ptr(g, rt, recv);
    LLVMValueRef nm = arg0 ? emit_expr(g, arg0, locals) : refl_empty_string(g);
    if (arg0) *nm_tmp = nm;
    nm = LLVMBuildBitCast(g->builder, nm, i8ptr, "refl.nm");
    LLVMTypeRef three[3] = { i8ptr, i8ptr, i8ptr };
    if (code == ZAN_REFL_M_SETINT || code == ZAN_REFL_M_SETLONG ||
        code == ZAN_REFL_M_SETBOOL || code == ZAN_REFL_M_SETDOUBLE ||
        code == ZAN_REFL_M_SETSTRING || code == ZAN_REFL_M_SETOBJECT) {
        LLVMValueRef v = arg1 ? emit_expr(g, arg1, locals) : NULL;
        if (arg1) *val_tmp = v;
        int vkind = code == ZAN_REFL_M_SETDOUBLE ? ZAN_REFL_VK_DOUBLE
                  : (code == ZAN_REFL_M_SETSTRING || code == ZAN_REFL_M_SETOBJECT) ? ZAN_REFL_VK_PTR
                                                 : ZAN_REFL_VK_INT;
        LLVMValueRef slot = v ? refl_to_slot(g, v) : LLVMConstInt(i64, 0, 0);
        LLVMValueRef ok = zan_call2(g->builder,
            LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr, i8ptr, i8ptr, i64,
                                                   i64 }, 5, 0),
            refl_set_fn(g), (LLVMValueRef[]){ ti, obj, nm, slot,
                LLVMConstInt(i64, (unsigned long long)vkind, 0) }, 5, "refl.set");
        *out = LLVMBuildICmp(g->builder, LLVMIntNE, ok,
                             LLVMConstInt(i64, 0, 0), "refl.setok");
        return true;
    }
    if (code == ZAN_REFL_M_OMHAS) {   /* HasMethod(name) */
        *out = LLVMBuildICmp(g->builder, LLVMIntSGE, refl_mfind(g, ti, nm, 0),
                             LLVMConstInt(i64, 0, 0), "refl.omhas");
        return true;
    }
    if (code == ZAN_REFL_M_IVOID || code == ZAN_REFL_M_IINT ||
        code == ZAN_REFL_M_ILONG || code == ZAN_REFL_M_IDOUBLE ||
        code == ZAN_REFL_M_ISTRING || code == ZAN_REFL_M_IOBJECT) {
        LLVMValueRef idx = refl_mfind(g, ti, nm, 0);
        refl_arg_temps_t temps;
        LLVMValueRef slots = refl_pack_args(g, args, argc, 1, locals, &temps);
        LLVMValueRef kout = LLVMBuildAlloca(g->builder, i64, "refl.ik");
        LLVMValueRef v = zan_call_or_invoke(g, g->builder,
            LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr, i64, i64, i8ptr,
                                                   i64ptr, i64ptr }, 6, 0),
            refl_invoke_fn(g), (LLVMValueRef[]){ ti,
                LLVMConstInt(i64, ZAN_REFL_TBL_METHOD, 0), idx, obj, slots,
                kout }, 6, "refl.iv");
        refl_release_args(g, args, argc, 1, locals, &temps);
        LLVMValueRef kv = LLVMBuildLoad2(g->builder, i64, kout, "refl.ikv");
        LLVMValueRef called = LLVMBuildICmp(g->builder, LLVMIntNE, kv,
            LLVMConstInt(i64, 0, 0), "refl.icalled");
        if (code == ZAN_REFL_M_IVOID) { *out = called; return true; }
        if (code == ZAN_REFL_M_IOBJECT) {
            LLVMValueRef obj_ret = LLVMBuildIntToPtr(g->builder, v, i8ptr, "refl.iobj");
            *out = LLVMBuildSelect(g->builder, called, obj_ret,
                                   LLVMConstNull(i8ptr), "refl.ires");
            return true;
        }
        LLVMValueRef k = LLVMBuildSub(g->builder, kv, LLVMConstInt(i64, 1, 0),
                                      "refl.ik1");
        if (code == ZAN_REFL_M_ISTRING) {
            LLVMValueRef s = LLVMBuildIntToPtr(g->builder, v, i8ptr, "refl.isv");
            LLVMValueRef isstr = LLVMBuildAnd(g->builder, called,
                LLVMBuildICmp(g->builder, LLVMIntEQ, k,
                    LLVMConstInt(i64, ZAN_REFL_FK_STRING, 0), "refl.isk"),
                "refl.isok");
            isstr = LLVMBuildAnd(g->builder, isstr,
                LLVMBuildIsNotNull(g->builder, s, "refl.isnn"), "refl.isok2");
            *out = LLVMBuildSelect(g->builder, isstr, s, refl_empty_string(g),
                                   "refl.istr");
            return true;
        }
        if (code == ZAN_REFL_M_IDOUBLE) {
            *out = LLVMBuildSelect(g->builder, called,
                refl_pget_as_f64(g, v, k), LLVMConstReal(dbl, 0.0), "refl.idv");
            return true;
        }
        LLVMValueRef iv = LLVMBuildSelect(g->builder, called,
            refl_pget_as_i64(g, v, k), LLVMConstInt(i64, 0, 0), "refl.iiv");
        *out = code == ZAN_REFL_M_IINT
             ? LLVMBuildTrunc(g->builder, iv, i32, "refl.ii32") : iv;
        return true;
    }
    if (code == ZAN_REFL_M_OBOOL) {   /* GetFieldBool(name) */
        LLVMValueRef v = zan_call2(g->builder,
            LLVMFunctionType(i64, three, 3, 0), refl_get_i64_fn(g),
            (LLVMValueRef[]){ ti, obj, nm }, 3, "refl.b64");
        *out = LLVMBuildICmp(g->builder, LLVMIntNE, v,
                             LLVMConstInt(i64, 0, 0), "refl.bool");
        return true;
    }
    if (code == ZAN_REFL_M_OHAS) {
        LLVMValueRef found = zan_call2(g->builder,
            LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0),
            refl_find_fn(g), (LLVMValueRef[]){ ti, nm }, 2, "refl.f");
        *out = LLVMBuildICmp(g->builder, LLVMIntSGE, found,
                             LLVMConstInt(i64, 0, 0), "refl.has");
        return true;
    }
    if (code == ZAN_REFL_M_ODOUBLE) {
        *out = zan_call2(g->builder, LLVMFunctionType(dbl, three, 3, 0),
            refl_get_f64_fn(g), (LLVMValueRef[]){ ti, obj, nm }, 3, "refl.f64");
        return true;
    }
    if (code == ZAN_REFL_M_OSTRING) {
        *out = zan_call2(g->builder, LLVMFunctionType(i8ptr, three, 3, 0),
            refl_get_str_fn(g), (LLVMValueRef[]){ ti, obj, nm }, 3, "refl.str");
        return true;
    }
    if (code == ZAN_REFL_M_OOBJECT) {
        *out = zan_call2(g->builder, LLVMFunctionType(i8ptr, three, 3, 0),
            refl_get_obj_fn(g), (LLVMValueRef[]){ ti, obj, nm }, 3, "refl.obj");
        return true;
    }
    LLVMValueRef v = zan_call2(g->builder, LLVMFunctionType(i64, three, 3, 0),
        refl_get_i64_fn(g), (LLVMValueRef[]){ ti, obj, nm }, 3, "refl.i64");
    *out = code == ZAN_REFL_M_OINT ? LLVMBuildTrunc(g->builder, v, i32, "refl.i32") : v;
    return true;
}
bool refl_emit_instance_call(zan_irgen_t *g, zan_type_t *rt,
                                    LLVMValueRef recv, zan_istr_t name,
                                    zan_ast_node_t **args, int argc,
                                    local_scope_t *locals, LLVMValueRef *out) {
    LLVMValueRef nm_tmp = NULL, val_tmp = NULL;
    if (!refl_emit_instance_call_1(g, rt, recv, name, args, argc, locals, out,
                                   &nm_tmp, &val_tmp))
        return false;
    if (nm_tmp)
        emit_release_owned_call_temp(g, args[0], nm_tmp, locals);
    if (val_tmp)
        emit_release_owned_call_temp(g, args[1], val_tmp, locals);
    return true;
}
void emit_site_meta_table(zan_irgen_t *g) {
    if (!g->refl_used || !g->g_site_meta || !g->site_syms) return;
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    int n = g->leak_site_count;
    if (n <= 0) return;
    LLVMValueRef *elems = (LLVMValueRef *)calloc((size_t)n, sizeof(LLVMValueRef));
    if (!elems) return;
    for (int i = 0; i < n; i++) {
        elems[i] = LLVMConstNull(i8ptr);
        if (g->site_coll && g->site_coll[i]) {
            if (g->site_coll[i] == 1) {
                zan_type_t *elem_type = g->site_coll_elem ? g->site_coll_elem[i] : NULL;
                zan_type_t *lt = zan_binder_make_list_type(g->binder, elem_type);
                elems[i] = refl_meta_for(g, lt, NULL, 0);
            }
            continue;
        }
        zan_symbol_t *sym = g->site_syms[i];
        if (!sym || !sym->type) continue;
        zan_type_t *t = (g->site_inst && g->site_inst[i])
            ? g->site_inst[i] : sym->type;
        elems[i] = refl_meta_for(g, t, NULL, 0);
    }
    LLVMTypeRef at = LLVMArrayType(i8ptr, (unsigned)n);
    LLVMValueRef arr = LLVMAddGlobal(g->mod, at, "__zan_site_meta_tbl");
    LLVMSetInitializer(arr, LLVMConstArray(i8ptr, elems, (unsigned)n));
    LLVMSetLinkage(arr, LLVMInternalLinkage);
    LLVMSetInitializer(g->g_site_meta,
        LLVMConstBitCast(arr, LLVMPointerType(i8ptr, 0)));
    free(elems);
}
bool refl_type_needs_site_meta(zan_irgen_t *g, zan_symbol_t *sym) {
    if (!g->publish_mode) return true;
    if (!sym || !sym->type) return false;
    /* 1. 带注解特性的类型必须保留 */
    zan_ast_list_t *attrs = sym->decl ? zan_ast_attributes(sym->decl) : NULL;
    if (attrs && attrs->count > 0) return true;
    /* 2. 具备虚方法表的多态类型必须保留（如 Control 控件家族） */
    if (class_has_virtual_methods(sym)) return true;
    /* 3. 继承自非 object 复杂基类的派生类必须保留 */
    if (sym->type->base_type && sym->type->base_type->sym) {
        zan_symbol_t *bsym = sym->type->base_type->sym;
        bool is_obj = (bsym->name.len == 6 &&
                       (memcmp(bsym->name.str, "object", 6) == 0 ||
                        memcmp(bsym->name.str, "Object", 6) == 0));
        if (!is_obj) return true;
    }
    /* 4. 实现了接口的类型必须保留 */
    if (sym->type->interface_count > 0) return true;
    return false;
}

void zan_irgen_emit_arc_desc_init(zan_irgen_t *g) {
    if (!g->desc_hdr || !g->desc_gv) return;
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    for (int i = 0; i < g->leak_site_count; i++) {
        LLVMValueRef dg = g->desc_gv[i];
        if (!dg) continue;
        LLVMTypeRef dt = LLVMGlobalGetValueType(dg);
        LLVMValueRef dtor = LLVMConstNull(i8ptr);
        LLVMValueRef tynames = LLVMConstNull(i8ptr);
        LLVMValueRef meta = LLVMConstNull(i8ptr);
        if (g->site_coll && g->site_coll[i]) {
            LLVMValueRef fn = get_collection_release_decl(g, i);
            if (fn) dtor = LLVMConstBitCast(fn, i8ptr);
            const char *cn = arc_collection_type_name(g, g->site_coll[i],
                g->site_coll_elem ? g->site_coll_elem[i] : NULL);
            LLVMValueRef nptr[2] = {
                zan_irgen_intern_string(g, cn),
                LLVMConstNull(i8ptr) };
            LLVMTypeRef at = LLVMArrayType(i8ptr, 2);
            char gname[64];
            snprintf(gname, sizeof(gname), "__zan_tynames_%d", i);
            LLVMValueRef gv = LLVMAddGlobal(g->mod, at, gname);
            LLVMSetInitializer(gv, LLVMConstArray(i8ptr, nptr, 2));
            LLVMSetLinkage(gv, LLVMInternalLinkage);
            LLVMSetGlobalConstant(gv, 1);
            LLVMValueRef idxs[] = { LLVMConstInt(i64, 0, 0),
                                    LLVMConstInt(i64, 0, 0) };
            tynames = LLVMBuildGEP2(g->builder, at, gv, idxs, 2, "tn.ptr");
            if (g->refl_used && g->site_coll[i] == 1) {
                zan_type_t *elem_type = g->site_coll_elem ? g->site_coll_elem[i] : NULL;
                zan_type_t *lt = zan_binder_make_list_type(g->binder, elem_type);
                meta = refl_meta_for(g, lt, NULL, 0);
            }
        } else if (g->site_syms[i]) {
            zan_symbol_t *sym = g->site_syms[i];
            LLVMValueRef fn = get_class_release_decl(g, sym,
                                  g->site_inst ? g->site_inst[i] : NULL);
            if (fn) dtor = LLVMConstBitCast(fn, i8ptr);
            const char *names[64];
            int k = 0;
            zan_symbol_t *cur = sym;
            while (cur && k < 63) {
                char buf[320];
                int len = (int)cur->name.len;
                if (len > 319) len = 319;
                memcpy(buf, cur->name.str, (size_t)len);
                buf[len] = 0;
                names[k++] = zan_arena_strdup(g->arena, buf, (size_t)len);
                if (cur->type) {
                    for (int ifi = 0; ifi < cur->type->interface_count && k < 63; ifi++) {
                        zan_type_t *iface = cur->type->interfaces[ifi];
                        if (iface && iface->sym) {
                            int ilen = (int)iface->sym->name.len;
                            if (ilen > 319) ilen = 319;
                            char ibuf[320];
                            memcpy(ibuf, iface->sym->name.str, (size_t)ilen);
                            ibuf[ilen] = 0;
                            names[k++] = zan_arena_strdup(g->arena, ibuf, (size_t)ilen);
                        }
                    }
                }
                cur = (cur->type && cur->type->base_type)
                          ? cur->type->base_type->sym : NULL;
            }
            if (k > 0) {
                LLVMValueRef *nptr = (LLVMValueRef *)calloc((size_t)k + 1,
                                                            sizeof(LLVMValueRef));
                for (int j = 0; j < k; j++)
                    nptr[j] = zan_irgen_intern_string(g, names[j]);
                nptr[k] = LLVMConstNull(i8ptr);
                LLVMTypeRef at = LLVMArrayType(i8ptr, (unsigned)k + 1);
                char gname[64];
                snprintf(gname, sizeof(gname), "__zan_tynames_%d", i);
                LLVMValueRef gv = LLVMAddGlobal(g->mod, at, gname);
                LLVMSetInitializer(gv, LLVMConstArray(i8ptr, nptr, (unsigned)k + 1));
                LLVMSetLinkage(gv, LLVMInternalLinkage);
                LLVMSetGlobalConstant(gv, 1);
                free(nptr);
                LLVMValueRef idxs[] = { LLVMConstInt(i64, 0, 0),
                                        LLVMConstInt(i64, 0, 0) };
                tynames = LLVMBuildGEP2(g->builder, at, gv, idxs, 2, "tn.ptr");
            }
            if (g->refl_used && sym->type && refl_type_needs_site_meta(g, sym)) {
                zan_type_t *t = (g->site_inst && g->site_inst[i])
                    ? g->site_inst[i] : sym->type;
                meta = refl_meta_for(g, t, NULL, 0);
            }
        }
        LLVMValueRef fg = build_site_field_graph(g, i);
        LLVMValueRef fields[4] = { dtor, tynames, meta, fg };
        LLVMSetInitializer(dg, LLVMConstNamedStruct(dt, fields, 4));
    }
}
