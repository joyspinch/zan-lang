#include "irgen_internal.h"

#define CHECK(condition) do {                                             \
    if (!(condition)) {                                                   \
        fprintf(stderr, "%s:%d: %s\n", __func__, __LINE__, #condition);      \
        exit(EXIT_FAILURE);                                               \
    }                                                                     \
} while (0)

static LLVMTypeRef record(LLVMContextRef ctx, LLVMTypeRef field, unsigned n) {
    LLVMTypeRef fields[3] = {field, field, field};
    return LLVMStructTypeInContext(ctx, fields, n, 0);
}

static LLVMValueRef make_thunk(zan_irgen_t *g, const char *name, LLVMTypeRef ret,
                               LLVMTypeRef *params, unsigned count) {
    LLVMTypeRef ft = LLVMFunctionType(ret, params, count, 0);
    LLVMValueRef thunk = abi_extern_thunk(g, name, ft);
    CHECK(thunk && LLVMGlobalGetValueType(thunk) == ft);
    return thunk;
}

static LLVMValueRef thunk_call(LLVMValueRef thunk) {
    for (LLVMValueRef inst = LLVMGetFirstInstruction(LLVMGetEntryBasicBlock(thunk));
         inst; inst = LLVMGetNextInstruction(inst)) {
        if (LLVMIsACallInst(inst)) return inst;
    }
    CHECK(false);
    return NULL;
}

static void check_type_attr(LLVMValueRef call, unsigned index, const char *name,
                            LLVMTypeRef type, unsigned align) {
    unsigned kind = LLVMGetEnumAttributeKindForName(name, strlen(name));
    unsigned align_kind = LLVMGetEnumAttributeKindForName("align", 5);
    LLVMValueRef decl = LLVMGetCalledValue(call);
    LLVMAttributeRef a = LLVMGetEnumAttributeAtIndex(decl, index, kind);
    CHECK(a && LLVMGetTypeAttributeValue(a) == type);
    a = LLVMGetCallSiteEnumAttribute(call, index, kind);
    CHECK(a && LLVMGetTypeAttributeValue(a) == type);
    a = LLVMGetEnumAttributeAtIndex(decl, index, align_kind);
    CHECK(a && LLVMGetEnumAttributeValue(a) == align);
    a = LLVMGetCallSiteEnumAttribute(call, index, align_kind);
    CHECK(a && LLVMGetEnumAttributeValue(a) == align);
}

static void check_sysv(zan_irgen_t *g) {
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef f32 = LLVMFloatTypeInContext(g->ctx);
    LLVMTypeRef f64 = LLVMDoubleTypeInContext(g->ctx);
    LLVMTypeRef ptr = LLVMPointerTypeInContext(g->ctx, 0);
    LLVMTypeRef big = record(g->ctx, i64, 3);
    LLVMTypeRef pair = record(g->ctx, i64, 2);
    LLVMTypeRef single = record(g->ctx, i64, 1);
    LLVMTypeRef f2 = record(g->ctx, f32, 2);
    LLVMTypeRef f3 = record(g->ctx, f32, 3);
    LLVMTypeRef f2_coerce = LLVMVectorType(f32, 2);
    LLVMTypeRef mixed_fields[] = {f64, i64};
    LLVMTypeRef mixed = LLVMStructTypeInContext(g->ctx, mixed_fields, 2, 0);

    const struct {
        const char *name;
        unsigned integers, sse;
        LLVMTypeRef ret, c_ret;
        LLVMTypeRef args[3];
        LLVMTypeRef lowered[3];
        LLVMTypeRef byval[3];
        unsigned nargs, nlowered;
    } cases[] = {
        {"integer_rollback", 5, 0, i64, i64, {pair, single, single},
            {ptr, i64, i64}, {pair}, 3, 3},
        {"sse_rollback", 0, 7, i64, i64, {f3, f2, f2},
            {ptr, f2_coerce, ptr}, {f3, NULL, f2}, 3, 3},
        {"mixed_integer_pressure", 6, 7, i64, i64, {mixed, f2},
            {ptr, f2_coerce}, {mixed}, 2, 2},
        {"mixed_sse_pressure", 5, 8, mixed, mixed, {mixed, single},
            {ptr, i64}, {mixed}, 2, 2},
        {"hidden_sret", 4, 0, big, LLVMVoidTypeInContext(g->ctx), {pair, single},
            {ptr, i64}, {pair}, 2, 2},
        {"memory_arg", 4, 0, pair, pair, {big, pair},
            {ptr, i64, i64}, {big}, 2, 3},
    };
    for (unsigned c = 0; c < sizeof(cases) / sizeof(cases[0]); c++) {
        LLVMTypeRef params[17], expected[18];
        unsigned n = 0, cn = 0;
        bool sret = cases[c].ret == big;
        if (sret) expected[cn++] = ptr;
        for (unsigned i = 0; i < cases[c].integers; i++)
            expected[cn++] = params[n++] = i == 0 ? ptr : i64;
        for (unsigned i = 0; i < cases[c].sse; i++)
            expected[cn++] = params[n++] = i == 0 ? f32 : f64;
        unsigned first = cn;
        for (unsigned i = 0; i < cases[c].nargs; i++)
            params[n++] = cases[c].args[i];
        for (unsigned i = 0; i < cases[c].nlowered; i++)
            expected[cn++] = cases[c].lowered[i];
        LLVMValueRef thunk = make_thunk(g, cases[c].name, cases[c].ret, params, n);
        LLVMValueRef call = thunk_call(thunk);
        LLVMTypeRef ft = LLVMFunctionType(cases[c].c_ret, expected, cn, 0);
        CHECK(LLVMGlobalGetValueType(LLVMGetCalledValue(call)) == ft);
        CHECK(LLVMGetCalledFunctionType(call) == ft);
        if (sret) check_type_attr(call, 1, "sret", big, 8);
        for (unsigned i = 0; i < cases[c].nlowered; i++) {
            if (cases[c].byval[i])
                check_type_attr(call, first + i + 1, "byval", cases[c].byval[i], 8);
        }
    }
}

static void check_aarch64(zan_irgen_t *g) {
    LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
    LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef wide = LLVMArrayType(i64, 2);
    LLVMTypeRef s3 = record(g->ctx, i8, 3);
    LLVMTypeRef s9 = record(g->ctx, LLVMArrayType(i8, 9), 1);
    LLVMTypeRef p12 = record(g->ctx, i32, 3);
    LLVMTypeRef hfa = record(g->ctx, LLVMDoubleTypeInContext(g->ctx), 3);
    LLVMTypeRef hfa_arg = LLVMArrayType(LLVMDoubleTypeInContext(g->ctx), 3);

    const struct {
        const char *name;
        LLVMTypeRef ty, arg, ret, arg_slot, ret_slot;
    } cases[] = {
        {"take_return_s3", s3, i64, LLVMIntTypeInContext(g->ctx, 24), i64, s3},
        {"take_return_s9", s9, wide, wide, wide, wide},
        {"take_return_p12", p12, wide, wide, wide, wide},
        {"take_return_hfa", hfa, hfa_arg, hfa, hfa, NULL},
    };
    for (unsigned c = 0; c < sizeof(cases) / sizeof(cases[0]); c++) {
        LLVMTypeRef ty = cases[c].ty;
        LLVMTypeRef arg = cases[c].arg;
        LLVMValueRef thunk = make_thunk(g, cases[c].name, ty, &ty, 1);
        LLVMValueRef call = thunk_call(thunk);
        CHECK(LLVMGetCalledFunctionType(call) == LLVMFunctionType(cases[c].ret, &arg, 1, 0));
        LLVMValueRef arg_load = LLVMGetOperand(call, 0);
        CHECK(LLVMIsALoadInst(arg_load));
        LLVMValueRef arg_slot = LLVMGetOperand(arg_load, 0);
        CHECK(LLVMIsAAllocaInst(arg_slot));
        CHECK(LLVMGetAllocatedType(arg_slot) == cases[c].arg_slot);
        if (cases[c].arg_slot != ty) {
            LLVMValueRef init = LLVMGetNextInstruction(arg_slot);
            CHECK(LLVMIsAStoreInst(init) && LLVMGetOperand(init, 1) == arg_slot);
            LLVMValueRef value = LLVMGetOperand(init, 0);
            CHECK(LLVMTypeOf(value) == arg && LLVMIsNull(value));
        }
        LLVMValueRef ret = LLVMGetBasicBlockTerminator(LLVMGetEntryBasicBlock(thunk));
        LLVMValueRef ret_value = LLVMGetOperand(ret, 0);
        if (cases[c].ret_slot) {
            CHECK(LLVMIsALoadInst(ret_value));
            LLVMValueRef ret_slot = LLVMGetOperand(ret_value, 0);
            CHECK(LLVMIsAAllocaInst(ret_slot));
            CHECK(LLVMGetAllocatedType(ret_slot) == cases[c].ret_slot);
        } else {
            CHECK(ret_value == call);
        }
    }
}

int main(void) {
    const char *triples[] = {"x86_64-unknown-linux-gnu", "aarch64-unknown-linux-gnu",
                             "arm64-apple-darwin"};
    for (unsigned i = 0; i < sizeof(triples) / sizeof(triples[0]); i++) {
        zan_irgen_t *g = calloc(1, sizeof(*g));
        CHECK(g);
        snprintf(g->target_triple, sizeof(g->target_triple), "%s", triples[i]);
        g->ctx = LLVMContextCreate();
        g->mod = LLVMModuleCreateWithNameInContext("irgen_abi_test", g->ctx);
        g->builder = LLVMCreateBuilderInContext(g->ctx);
        LLVMSetTarget(g->mod, triples[i]);
        if (i == 0) check_sysv(g);
        else check_aarch64(g);
        CHECK(!LLVMVerifyModule(g->mod, LLVMPrintMessageAction, NULL));
        LLVMDisposeBuilder(g->builder);
        LLVMDisposeModule(g->mod);
        LLVMContextDispose(g->ctx);
        free(g);
    }
    return EXIT_SUCCESS;
}
