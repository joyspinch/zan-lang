/* irgen.c -- LLVM IR generation for the Zan language.
 *
 * For M1 this generates code for:
 *   - Static Main() method as program entry point
 *   - Console.WriteLine() calls → zan_rt_println / printf
 *   - Integer and floating-point arithmetic
 *   - Local variable declarations and assignments
 *   - Control flow (if, while, for)
 *   - String literals
 */

#include "irgen.h"
#include "builtin_api.h"
#include "arena.h"
#include "diag.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

/* ---- width-tolerant integer builders -------------------------------------
 * Zan's `int` lowers to i32 while lengths, counts, handles and every runtime
 * helper are i64, so mixed-width operands reach these builders constantly (an
 * i32 loop variable against an i64 `List.Count`, an i32 index plus an i64
 * offset). LLVM rejects that, and dozens of lowering sites would each have to
 * extend by hand. These wrappers sign-extend the narrower operand to the wider
 * one first and are otherwise the LLVM builders; irgen calls them instead. */
/* Widen an integer, choosing the extension its Zan type calls for. Of the
 * narrow LLVM widths only i1 (`bool`) and i8 (`byte`) occur, and both are
 * unsigned -- `sbyte` and `short` are wider in this lowering -- so anything
 * up to a byte zero-extends. Sign-extending a byte is what made `byte b =
 * 255` read back as -1. */
static LLVMValueRef zan_iwiden(LLVMBuilderRef b, LLVMValueRef v, LLVMTypeRef to) {
    LLVMTypeRef vt = LLVMTypeOf(v);
    if (LLVMGetTypeKind(vt) != LLVMIntegerTypeKind) return v;
    return LLVMGetIntTypeWidth(vt) <= 8 ? LLVMBuildZExt(b, v, to, "zx")
                                        : LLVMBuildSExt(b, v, to, "wx");
}

static void zan_ipair(LLVMBuilderRef b, LLVMValueRef *l, LLVMValueRef *r) {
    LLVMTypeRef tl = LLVMTypeOf(*l), tr = LLVMTypeOf(*r);
    if (LLVMGetTypeKind(tl) != LLVMIntegerTypeKind ||
        LLVMGetTypeKind(tr) != LLVMIntegerTypeKind) return;
    unsigned wl = LLVMGetIntTypeWidth(tl), wr = LLVMGetIntTypeWidth(tr);
    if (wl == wr) {
        /* C# promotes byte operands to int before arithmetic, so `b + 1`
         * with b = 255 is 256 and not a wrapped 0. */
        if (wl == 8) {
            LLVMTypeRef i64 = LLVMInt64TypeInContext(LLVMGetTypeContext(tl));
            *l = LLVMBuildZExt(b, *l, i64, "zx");
            *r = LLVMBuildZExt(b, *r, i64, "zx");
        }
        return;
    }
    if (wl < wr) *l = zan_iwiden(b, *l, tr);
    else         *r = zan_iwiden(b, *r, tl);
}

#define ZAN_IBIN(name, builder)                                              \
static LLVMValueRef name(LLVMBuilderRef b, LLVMValueRef l, LLVMValueRef r,    \
                         const char *n) {                                    \
    zan_ipair(b, &l, &r);                                                    \
    return builder(b, l, r, n);                                              \
}
ZAN_IBIN(zan_add,  LLVMBuildAdd)
ZAN_IBIN(zan_sub,  LLVMBuildSub)
ZAN_IBIN(zan_mul,  LLVMBuildMul)
ZAN_IBIN(zan_sdiv, LLVMBuildSDiv)
ZAN_IBIN(zan_udiv, LLVMBuildUDiv)
ZAN_IBIN(zan_srem, LLVMBuildSRem)
ZAN_IBIN(zan_urem, LLVMBuildURem)
ZAN_IBIN(zan_and,  LLVMBuildAnd)
ZAN_IBIN(zan_or,   LLVMBuildOr)
ZAN_IBIN(zan_xor,  LLVMBuildXor)
ZAN_IBIN(zan_shl,  LLVMBuildShl)
ZAN_IBIN(zan_lshr, LLVMBuildLShr)
ZAN_IBIN(zan_ashr, LLVMBuildAShr)
#undef ZAN_IBIN

static LLVMValueRef zan_icmp(LLVMBuilderRef b, LLVMIntPredicate p,
                             LLVMValueRef l, LLVMValueRef r, const char *n) {
    zan_ipair(b, &l, &r);
    return LLVMBuildICmp(b, p, l, r, n);
}

#include <llvm-c/Analysis.h>
#include <llvm-c/BitWriter.h>
#include <llvm-c/DebugInfo.h>
#if defined(__has_include)
#if __has_include(<llvm/Config/llvm-config.h>)
#include <llvm/Config/llvm-config.h>
#endif
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#define zan_getcwd _getcwd
#else
#include <unistd.h>
#define zan_getcwd getcwd
#endif

/* ---------------------------------------------------------------------------
 * DWARF debug information (opt-in via `zanc -g`).
 *
 * Emits DWARF line tables and one DISubprogram per emitted LLVM function so a
 * standard source-level debugger (gdb/lldb, driven by zan-dap) can set source
 * breakpoints, produce accurate call stacks and step through Zan code. Debug
 * metadata is only created when g->emit_debug is set, so default and --publish
 * builds are byte-for-byte unchanged.
 *
 * Subprograms are created lazily the first time a statement location is set for
 * a function, keyed off the LLVM function currently under the builder. The only
 * verifier-critical invariant is that an instruction's !dbg scope must belong to
 * the function containing it; since the builder keeps a *persistent* current
 * location, we clear it (di_clear) whenever we begin emitting a different
 * function's body, so instructions in synthetic/prologue positions never inherit
 * a neighbouring function's scope. Sloppy locations *within* one function are
 * harmless (same subprogram). */
static LLVMMetadataRef di_file_for(zan_irgen_t *g, uint32_t file_id);

static void di_ensure(zan_irgen_t *g) {
    if (!g->emit_debug || g->di_builder) return;
    g->di_builder = LLVMCreateDIBuilder(g->mod);

    /* Module flags required for a debugger to consume the info. DWARF (not
     * CodeView) because Zan links Windows binaries with the bundled GNU ld
     * (windows-gnu ABI), which gdb reads. */
    LLVMContextRef c = g->ctx;
    LLVMTypeRef i32 = LLVMInt32TypeInContext(c);
    LLVMAddModuleFlag(g->mod, LLVMModuleFlagBehaviorWarning,
                      "Debug Info Version", 18,
                      LLVMValueAsMetadata(LLVMConstInt(i32, 3, 0)));
    LLVMAddModuleFlag(g->mod, LLVMModuleFlagBehaviorWarning,
                      "Dwarf Version", 13,
                      LLVMValueAsMetadata(LLVMConstInt(i32, 4, 0)));

    LLVMMetadataRef file = di_file_for(g, 0);
    const char *producer = "zanc";
    g->di_cu = LLVMDIBuilderCreateCompileUnit(
        g->di_builder, LLVMDWARFSourceLanguageC, file,
        producer, strlen(producer),
        /*isOptimized*/ 0, /*Flags*/ "", 0, /*RuntimeVer*/ 0,
        /*SplitName*/ "", 0, LLVMDWARFEmissionFull,
        /*DWOId*/ 0, /*SplitDebugInlining*/ 0,
        /*DebugInfoForProfiling*/ 0, /*SysRoot*/ "", 0, /*SDK*/ "", 0);
}

static LLVMMetadataRef di_file_for(zan_irgen_t *g, uint32_t file_id) {
    if (!g->emit_debug) return NULL;
    if (file_id >= 256) file_id = 0;
    if (g->di_files[file_id]) return g->di_files[file_id];
    if (!g->di_builder) g->di_builder = LLVMCreateDIBuilder(g->mod);

    const char *path = NULL;
    if (g->diag && file_id < (uint32_t)g->diag->file_count && g->diag->file_names)
        path = g->diag->file_names[file_id];
    if (!path || !path[0]) path = g->src_file ? g->src_file : "<unknown>.zan";

    /* Split into directory + filename so comp_dir lets the debugger resolve
     * relative source paths. */
    char dir[1024];
    if (path[0] == '/' || (path[0] && path[1] == ':')) {
        /* absolute: keep the leading directory portion */
        const char *slash = strrchr(path, '/');
        const char *bslash = strrchr(path, '\\');
        const char *cut = slash > bslash ? slash : bslash;
        if (cut) {
            size_t n = (size_t)(cut - path);
            if (n >= sizeof(dir)) n = sizeof(dir) - 1;
            memcpy(dir, path, n);
            dir[n] = '\0';
            path = cut + 1;
        } else {
            dir[0] = '\0';
        }
    } else if (!zan_getcwd(dir, sizeof(dir))) {
        dir[0] = '\0';
    }

    LLVMMetadataRef f = LLVMDIBuilderCreateFile(
        g->di_builder, path, strlen(path), dir, strlen(dir));
    g->di_files[file_id] = f;
    return f;
}

/* Resolve the source file for a leak-site descriptor from a node's location.
 * Allocations that originate in an included file (notably the stdlib pulled in
 * by --auto-stdlib) carry their own loc.file_id, so attribute the leak to that
 * file instead of the top-level module (g->src_file), which mislabels every
 * site as the program being compiled. */
static const char *leak_site_file(zan_irgen_t *g, zan_loc_t loc) {
    if (g->diag && g->diag->file_names &&
        (int)loc.file_id < g->diag->file_count) {
        const char *p = g->diag->file_names[loc.file_id];
        if (p && p[0]) return p;
    }
    return g->src_file ? g->src_file : "<unknown>";
}

/* Return the DISubprogram of the function currently under the builder, creating
 * it lazily (keyed off the LLVM function) the first time it is needed. */
static LLVMMetadataRef di_ensure_sp(zan_irgen_t *g, uint32_t file_id, unsigned line) {
    if (!g->emit_debug || !g->builder) return NULL;
    LLVMBasicBlockRef bb = LLVMGetInsertBlock(g->builder);
    if (!bb) return NULL;
    LLVMValueRef fn = LLVMGetBasicBlockParent(bb);
    if (!fn) return NULL;
    di_ensure(g);
    LLVMMetadataRef sp = LLVMGetSubprogram(fn);
    if (sp) return sp;
    size_t nlen = 0;
    const char *name = LLVMGetValueName2(fn, &nlen);
    if (!name || nlen == 0) { name = "fn"; nlen = 2; }
    LLVMMetadataRef file = di_file_for(g, file_id);
    LLVMMetadataRef subty = LLVMDIBuilderCreateSubroutineType(
        g->di_builder, file, NULL, 0, LLVMDIFlagZero);
    unsigned l = line ? line : 1;
    sp = LLVMDIBuilderCreateFunction(
        g->di_builder, file, name, nlen, name, nlen, file, l, subty,
        /*IsLocalToUnit*/ 0, /*IsDefinition*/ 1, /*ScopeLine*/ l,
        LLVMDIFlagZero, /*IsOptimized*/ 0);
    LLVMSetSubprogram(fn, sp);
    return sp;
}

/* Clear the builder's current debug location. MUST be called when beginning to
 * emit a new function's body so prologue/synthetic instructions do not inherit a
 * neighbouring function's DISubprogram scope (a hard verifier error). No-op
 * unless debug info is enabled. */
static void di_clear(zan_irgen_t *g) {
    if (!g->emit_debug || !g->builder) return;
    LLVMSetCurrentDebugLocation2(g->builder, NULL);
    g->di_cur_line = 0;
    g->di_cur_file = 0;
}

/* Attach a source location (and, lazily, a DISubprogram) to the function
 * currently under the builder. Called once per statement from emit_stmt. */
static void di_set_loc(zan_irgen_t *g, zan_loc_t loc) {
    if (!g->emit_debug || !g->builder) return;
    LLVMMetadataRef sp = di_ensure_sp(g, loc.file_id, loc.line);
    if (!sp) return;
    g->di_cur_line = loc.line;
    g->di_cur_file = loc.file_id;
    unsigned line = loc.line ? loc.line : 1;
    LLVMMetadataRef dl = LLVMDIBuilderCreateDebugLocation(
        g->ctx, line, loc.col, sp, NULL);
    LLVMSetCurrentDebugLocation2(g->builder, dl);
}

/* Map an LLVM storage type to a DIType for a local/parameter. Returns NULL for
 * aggregates (struct/array by value), whose contents we do not describe yet, so
 * the caller skips emitting a declare rather than showing wrong bytes. */
static LLVMMetadataRef di_type_from_llvm(zan_irgen_t *g, LLVMTypeRef ty) {
    LLVMTypeKind k = LLVMGetTypeKind(ty);
    switch (k) {
    case LLVMIntegerTypeKind: {
        unsigned w = LLVMGetIntTypeWidth(ty);
        if (w == 1)
            return LLVMDIBuilderCreateBasicType(g->di_builder, "bool", 4, 8,
                                                /*DW_ATE_boolean*/ 0x02,
                                                LLVMDIFlagZero);
        char nm[16];
        int n = snprintf(nm, sizeof(nm), "i%u", w);
        return LLVMDIBuilderCreateBasicType(g->di_builder, nm, (size_t)n, w,
                                            /*DW_ATE_signed*/ 0x05,
                                            LLVMDIFlagZero);
    }
    case LLVMDoubleTypeKind:
        return LLVMDIBuilderCreateBasicType(g->di_builder, "f64", 3, 64,
                                            /*DW_ATE_float*/ 0x04, LLVMDIFlagZero);
    case LLVMFloatTypeKind:
        return LLVMDIBuilderCreateBasicType(g->di_builder, "f32", 3, 32,
                                            /*DW_ATE_float*/ 0x04, LLVMDIFlagZero);
    case LLVMPointerTypeKind: {
        LLVMMetadataRef byte = LLVMDIBuilderCreateBasicType(
            g->di_builder, "byte", 4, 8, /*DW_ATE_unsigned_char*/ 0x08,
            LLVMDIFlagZero);
        return LLVMDIBuilderCreatePointerType(g->di_builder, byte, 64, 0, 0,
                                              "ptr", 3);
    }
    default:
        return NULL;
    }
}

/* Emit an llvm.dbg.declare tying a named source variable to its stack slot, so
 * the debugger can list and read it. `storage` must be an alloca (frame-resident
 * async locals and non-alloca slots are skipped). Called for every local scope
 * entry via local_add; g comes from the file-static emit context. */
static void di_declare_var(zan_irgen_t *g, zan_istr_t name, LLVMValueRef storage) {
    if (!g || !g->emit_debug || !g->builder) return;
    if (!storage || !LLVMIsAAllocaInst(storage)) return;
    if (name.len == 0 || !name.str) return;
    LLVMBasicBlockRef bb = LLVMGetInsertBlock(g->builder);
    if (!bb) return;
    LLVMMetadataRef sp = di_ensure_sp(g, g->di_cur_file, g->di_cur_line);
    if (!sp) return;
    LLVMMetadataRef ty = di_type_from_llvm(g, LLVMGetAllocatedType(storage));
    if (!ty) return; /* aggregate: not described yet */
    LLVMMetadataRef file = di_file_for(g, g->di_cur_file);
    unsigned line = g->di_cur_line ? g->di_cur_line : 1;
    LLVMMetadataRef var = LLVMDIBuilderCreateAutoVariable(
        g->di_builder, sp, name.str, name.len, file, line, ty,
        /*AlwaysPreserve*/ 1, LLVMDIFlagZero, /*AlignInBits*/ 0);
    LLVMMetadataRef expr = LLVMDIBuilderCreateExpression(g->di_builder, NULL, 0);
    LLVMMetadataRef dl = LLVMDIBuilderCreateDebugLocation(g->ctx, line, 0, sp, NULL);
    /* The debug-record API (LLVMDIBuilderInsertDeclareRecordAtEnd) is LLVM 19+;
     * LLVM 18 and earlier only provide the intrinsic-based InsertDeclareAtEnd.
     * Both take the same arguments, so select by version. */
#if defined(LLVM_VERSION_MAJOR) && LLVM_VERSION_MAJOR < 19
    LLVMDIBuilderInsertDeclareAtEnd(g->di_builder, storage, var, expr, dl, bb);
#else
    LLVMDIBuilderInsertDeclareRecordAtEnd(g->di_builder, storage, var, expr, dl, bb);
#endif
}

/* The active codegen context, set for the duration of zan_irgen_emit so the
 * g-free local_add can forward variables to di_declare_var. Codegen is
 * single-threaded per module, so a file-static is safe here. */
static zan_irgen_t *g_di_emit_ctx = NULL;

#include "../common/host_oom.h"
/* Maximum number of distinct ARC destructor shapes tracked by the runtime.
 * Allocation sites with the same concrete class/generic/collection shape share
 * one slot; aliasing different shapes would dispatch the wrong destructor. */
#define ZAN_MAX_LEAK_SITES 4096

static bool types_equal(zan_type_t *a, zan_type_t *b);

static int reserve_arc_site(zan_irgen_t *g, zan_symbol_t *sym,
                            zan_type_t *inst, int coll_kind,
                            zan_type_t *coll_elem) {
    for (int i = 0; i < g->leak_site_count; i++) {
        int existing_kind = g->site_coll ? g->site_coll[i] : 0;
        if (existing_kind != coll_kind) continue;
        if (coll_kind != 0) {
            zan_type_t *existing_elem = g->site_coll_elem
                ? g->site_coll_elem[i] : NULL;
            if (types_equal(existing_elem, coll_elem)) return i;
        } else {
            zan_symbol_t *existing_sym = g->site_syms ? g->site_syms[i] : NULL;
            zan_type_t *existing_inst = g->site_inst ? g->site_inst[i] : NULL;
            if (existing_sym == sym && types_equal(existing_inst, inst)) return i;
        }
    }
    if (g->leak_site_count >= ZAN_MAX_LEAK_SITES) {
        fprintf(stderr,
            "zanc: too many distinct ARC destructor shapes (maximum %d)\n",
            ZAN_MAX_LEAK_SITES);
        exit(1);
    }
    int site_idx = g->leak_site_count++;
    if (g->site_syms) g->site_syms[site_idx] = sym;
    if (g->site_inst) g->site_inst[site_idx] = inst;
    if (g->site_coll) g->site_coll[site_idx] = coll_kind;
    if (g->site_coll_elem) g->site_coll_elem[site_idx] = coll_elem;
    return site_idx;
}

/* A closure record carries its own destructor in the record, so its site index
 * exists only for leak accounting -- and there it must be unique per lambda,
 * otherwise every closure in the program reports under one source location. */
static int reserve_closure_site(zan_irgen_t *g) {
    if (g->leak_site_count >= ZAN_MAX_LEAK_SITES) {
        fprintf(stderr,
            "zanc: too many distinct ARC destructor shapes (maximum %d)\n",
            ZAN_MAX_LEAK_SITES);
        exit(1);
    }
    int site_idx = g->leak_site_count++;
    if (g->site_syms) g->site_syms[site_idx] = NULL;
    if (g->site_inst) g->site_inst[site_idx] = NULL;
    if (g->site_coll) g->site_coll[site_idx] = 0;
    if (g->site_coll_elem) g->site_coll_elem[site_idx] = NULL;
    return site_idx;
}

/* String RC header magic and sentinel refcount.
 * The second header word doubles as a guard for tolerant retain/release. */
#define ZAN_STRING_MAGIC UINT64_C(0x5a414e5354524d47) /* ZANSTRMG */
#define ZAN_STRING_SENTINEL_RC UINT64_C(0xffffffffffffffff)

/* ---- initialization ---- */

/* Register a user-defined function so call sites can resolve it. The table
 * grows on demand: a fixed cap would silently drop functions in large
 * multi-file programs, leaving later calls unresolved and mis-typed. */

/* Build a call, tolerating a callee whose module-level declaration has a
 * different (ABI-compatible) signature than the call site expects. That
 * happens when a stdlib `static extern int fopen(...)` (Zan int = i64) and a
 * compiler-lowered builtin (`i8* fopen(...)`) both name the same libc symbol:
 * with typed-pointer LLVM builds the direct call would fail verification, so
 * route it through a bitcast of the function pointer instead. */
static LLVMValueRef zan_call2(LLVMBuilderRef b, LLVMTypeRef ty, LLVMValueRef fn,
                              LLVMValueRef *args, unsigned n, const char *nm) {
    LLVMValueRef callee = fn;
    if (fn && LLVMIsAFunction(fn) && LLVMGlobalGetValueType(fn) != ty)
        fn = LLVMConstBitCast(fn, LLVMPointerType(ty, 0));
    LLVMValueRef call = LLVMBuildCall2(b, ty, fn, args, n, nm);
    /* Carry the callee's sub-`int` promotions onto the call site: once the
     * callee is bitcast the backend sees an opaque pointer and has nothing
     * left to read the extension off. */
    if (callee && LLVMIsAFunction(callee)) {
        static const char *ext[2] = { "signext", "zeroext" };
        for (int e = 0; e < 2; e++) {
            unsigned kind = LLVMGetEnumAttributeKindForName(ext[e], strlen(ext[e]));
            if (!kind) continue;
            for (unsigned idx = 0; idx <= n; idx++) {
                unsigned at = idx == 0 ? (unsigned)LLVMAttributeReturnIndex : idx;
                if (!LLVMGetEnumAttributeAtIndex(callee, at, kind)) continue;
                LLVMAddCallSiteAttribute(call, at,
                    LLVMCreateEnumAttribute(LLVMGetTypeContext(ty), kind, 0));
            }
        }
    }
    return call;
}

/* Grow one of the IR registries to hold `need` entries. */
static void *irgen_grow(void *base, int *cap, int need, size_t elem) {
    if (need <= *cap) return base;
    int ncap = *cap ? *cap * 2 : 256;
    while (ncap < need) ncap *= 2;
    void *p = realloc(base, (size_t)ncap * elem);
    if (!p) {
        fprintf(stderr, "zanc: out of memory growing an IR registry\n");
        exit(1);
    }
    *cap = ncap;
    return p;
}

/* Bucket for `p` in a table of `cap` (a power of two) slots. */
static size_t irgen_ptr_bucket(const void *p, int cap) {
    uint64_t h = (uint64_t)(uintptr_t)p * 0x9E3779B97F4A7C15ull;
    return (size_t)((h >> 32) & (uint64_t)(cap - 1));
}

/* ---- function registry index (see zan_irgen.fn_index) ---- */

static size_t fn_index_hash(zan_symbol_t *sym, int cap) {
    return irgen_ptr_bucket(sym, cap);
}

/* Record sym -> idx, keeping the entry already stored for a duplicate sym. */
static void fn_index_put(zan_irgen_t *g, zan_symbol_t *sym, int idx) {
    size_t i = fn_index_hash(sym, g->fn_index_cap);
    while (g->fn_index[i].sym) {
        if (g->fn_index[i].sym == sym) return;
        i = (i + 1) & (size_t)(g->fn_index_cap - 1);
    }
    g->fn_index[i].sym = sym;
    g->fn_index[i].idx = idx;
}

/* Grow the index to `ncap` slots and reinsert every registered function. */
static void fn_index_rehash(zan_irgen_t *g, int ncap) {
    struct zan_fn_index_slot *slots =
        calloc((size_t)ncap, sizeof(*g->fn_index));
    if (!slots) {
        fprintf(stderr, "zanc: out of memory growing the function index\n");
        exit(1);
    }
    free(g->fn_index);
    g->fn_index = slots;
    g->fn_index_cap = ncap;
    for (int i = 0; i < g->function_count; i++)
        if (g->functions[i].sym) fn_index_put(g, g->functions[i].sym, i);
}

/* Index of `sym` in g->functions, or -1 when it was never registered. */
static int irgen_find_function(zan_irgen_t *g, zan_symbol_t *sym) {
    if (!sym || !g->fn_index) return -1;
    size_t i = fn_index_hash(sym, g->fn_index_cap);
    while (g->fn_index[i].sym) {
        if (g->fn_index[i].sym == sym) return g->fn_index[i].idx;
        i = (i + 1) & (size_t)(g->fn_index_cap - 1);
    }
    return -1;
}

static void irgen_register_function(zan_irgen_t *g, zan_symbol_t *sym,
                                    LLVMValueRef fn, LLVMTypeRef fn_type) {
    if (g->function_count >= g->function_cap) {
        int ncap = g->function_cap ? g->function_cap * 2 : 1024;
        g->functions = realloc(g->functions,
                               (size_t)ncap * sizeof(*g->functions));
        g->function_cap = ncap;
    }
    g->functions[g->function_count].sym = sym;
    g->functions[g->function_count].fn = fn;
    g->functions[g->function_count].fn_type = fn_type;
    /* Keep the index under 50% load so probe chains stay short. */
    if ((g->function_count + 1) * 2 >= g->fn_index_cap)
        fn_index_rehash(g, g->fn_index_cap ? g->fn_index_cap * 2 : 2048);
    if (sym) fn_index_put(g, sym, g->function_count);
    g->function_count++;
}

/* ---- per-class member index ------------------------------------------------
 * Member lookup by name (get_method_sym, resolve_overload, get_field_sym) and
 * by declaration node (method_sym_for_decl) used to scan a class's whole member
 * list, so a class with N methods called from N sites cost O(N^2) comparisons
 * and dominated irgen for large programs. Each class gets a lazily built hash
 * index of its members instead. Buckets chain in declaration order, so the
 * "first match wins" behaviour of the scans is preserved, and an index is
 * rebuilt when its class gained members (specialization appends members while
 * irgen runs). The table is file-static because the lookups are leaf helpers
 * without access to the irgen state; zan_irgen_init/destroy reset it. */

typedef struct {
    zan_symbol_t *owner;
    int built_count;    /* owner->member_count when the index was built */
    int cap;            /* bucket count, a power of two */
    int *name_buckets;  /* cap entries: first member index, or -1 */
    int *name_next;     /* per member: next index in its bucket, or -1 */
    int *decl_buckets;  /* cap entries: first member index, or -1 */
    int *decl_next;     /* per member: next index in its bucket, or -1 */
} zan_class_index_t;

static zan_class_index_t *g_class_indexes;
static int g_class_index_cap;   /* slots, a power of two */
static int g_class_index_count;

static uint64_t istr_hash(zan_istr_t s) {
    uint64_t h = 1469598103934665603ull;
    for (int i = 0; i < s.len; i++) {
        h ^= (unsigned char)s.str[i];
        h *= 1099511628211ull;
    }
    return h;
}

static void *class_index_alloc(size_t n, size_t elem) {
    void *p = malloc(n * elem);
    if (!p) {
        fprintf(stderr, "zanc: out of memory building a member index\n");
        exit(1);
    }
    return p;
}

static void class_index_free(zan_class_index_t *ci) {
    free(ci->name_buckets);
    free(ci->name_next);
    free(ci->decl_buckets);
    free(ci->decl_next);
    ci->name_buckets = ci->name_next = ci->decl_buckets = ci->decl_next = NULL;
}

static void class_index_build(zan_class_index_t *ci, zan_symbol_t *owner) {
    class_index_free(ci);
    int n = owner->member_count;
    int cap = 16;
    while (cap < n * 2) cap *= 2;
    ci->owner = owner;
    ci->built_count = n;
    ci->cap = cap;
    ci->name_buckets = class_index_alloc((size_t)cap, sizeof(int));
    ci->decl_buckets = class_index_alloc((size_t)cap, sizeof(int));
    ci->name_next = class_index_alloc((size_t)(n > 0 ? n : 1), sizeof(int));
    ci->decl_next = class_index_alloc((size_t)(n > 0 ? n : 1), sizeof(int));
    for (int i = 0; i < cap; i++) {
        ci->name_buckets[i] = -1;
        ci->decl_buckets[i] = -1;
    }
    /* Insert back to front so every chain runs in declaration order. */
    for (int i = n - 1; i >= 0; i--) {
        zan_symbol_t *m = owner->members[i];
        size_t nb = (size_t)(istr_hash(m->name) & (uint64_t)(cap - 1));
        ci->name_next[i] = ci->name_buckets[nb];
        ci->name_buckets[nb] = i;
        size_t db = irgen_ptr_bucket(m->decl, cap);
        ci->decl_next[i] = ci->decl_buckets[db];
        ci->decl_buckets[db] = i;
    }
}

static void class_index_table_grow(void) {
    int ncap = g_class_index_cap ? g_class_index_cap * 2 : 256;
    zan_class_index_t *slots = calloc((size_t)ncap, sizeof(*slots));
    if (!slots) {
        fprintf(stderr, "zanc: out of memory growing the member index table\n");
        exit(1);
    }
    for (int i = 0; i < g_class_index_cap; i++) {
        if (!g_class_indexes[i].owner) continue;
        size_t j = irgen_ptr_bucket(g_class_indexes[i].owner, ncap);
        while (slots[j].owner) j = (j + 1) & (size_t)(ncap - 1);
        slots[j] = g_class_indexes[i];
    }
    free(g_class_indexes);
    g_class_indexes = slots;
    g_class_index_cap = ncap;
}

static void class_index_reset(void) {
    for (int i = 0; i < g_class_index_cap; i++)
        if (g_class_indexes[i].owner) class_index_free(&g_class_indexes[i]);
    free(g_class_indexes);
    g_class_indexes = NULL;
    g_class_index_cap = g_class_index_count = 0;
}

static zan_class_index_t *class_index_for(zan_symbol_t *owner) {
    if ((g_class_index_count + 1) * 2 >= g_class_index_cap)
        class_index_table_grow();
    size_t i = irgen_ptr_bucket(owner, g_class_index_cap);
    while (g_class_indexes[i].owner && g_class_indexes[i].owner != owner)
        i = (i + 1) & (size_t)(g_class_index_cap - 1);
    zan_class_index_t *ci = &g_class_indexes[i];
    if (ci->owner != owner) {
        class_index_build(ci, owner);
        g_class_index_count++;
    } else if (ci->built_count != owner->member_count) {
        class_index_build(ci, owner);
    }
    return ci;
}

/* First member index in `owner`'s bucket for `name`; -1 when the bucket is
 * empty. Walk on with member_next_named. Names still have to be compared: a
 * bucket may hold members of other names. */
static int member_first_named(zan_class_index_t *ci, zan_istr_t name) {
    return ci->name_buckets[istr_hash(name) & (uint64_t)(ci->cap - 1)];
}

static int member_next_named(zan_class_index_t *ci, int i) {
    return ci->name_next[i];
}

static bool member_name_is(zan_symbol_t *m, zan_istr_t name) {
    return m->name.len == name.len &&
           memcmp(m->name.str, name.str, (size_t)name.len) == 0;
}

/* Guard a malloc result inside runtime allocator `fn`: if it is null, print
 * "out of memory" and exit(1) rather than returning a header-offset pointer
 * into a null allocation (which would later be dereferenced). */
static void emit_oom_check(zan_irgen_t *g, LLVMValueRef fn, LLVMValueRef raw) {
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMValueRef isnull = zan_icmp(g->builder, LLVMIntEQ, raw,
        LLVMConstPointerNull(i8ptr), "oom");
    LLVMBasicBlockRef oom_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "oom");
    LLVMBasicBlockRef ok_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "oom.ok");
    LLVMBuildCondBr(g->builder, isnull, oom_bb, ok_bb);
    LLVMPositionBuilderAtEnd(g->builder, oom_bb);
    LLVMValueRef text = LLVMBuildGlobalStringPtr(g->builder, "out of memory\n", "oomtxt");
    LLVMValueRef fmt = LLVMBuildGlobalStringPtr(g->builder, "%s", "oomfmt");
    LLVMValueRef pargs[] = { fmt, text };
    zan_call2(g->builder, g->printf_type, g->fn_printf, pargs, 2, "");
    LLVMValueRef code = LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 1, 0);
    zan_call2(g->builder, g->exit_type, g->fn_exit, &code, 1, "");
    LLVMBuildUnreachable(g->builder);
    LLVMPositionBuilderAtEnd(g->builder, ok_bb);
}

/* Add `delta` to a leak-tracking counter (`__zan_live` or a `__zan_site_live`
 * bucket). Allocation and release happen on every thread the scheduler runs --
 * `Thread.Start` bodies and coroutine workers alike -- so a load/add/store
 * trio loses updates whenever two threads allocate at once, which reported a
 * random handful of "still reachable" objects for programs that leaked
 * nothing. Relaxed ordering is enough: only the counter value matters, and the
 * report runs from atexit. */
static void emit_leak_counter_add(zan_irgen_t *g, LLVMValueRef ptr, long long delta) {
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMBuildAtomicRMW(g->builder,
        delta < 0 ? LLVMAtomicRMWBinOpSub : LLVMAtomicRMWBinOpAdd, ptr,
        LLVMConstInt(i64, (unsigned long long)(delta < 0 ? -delta : delta), 0),
        LLVMAtomicOrderingMonotonic, 0);
}

/* On Windows, guard an about-to-be-dereferenced string header pointer (obj-8,
 * the STRING_MAGIC slot) against freed/unmapped pages. The tolerant retain and
 * release probe [obj-8] to decide whether a pointer is a managed string; for a
 * bare buffer (e.g. a [DllImport] calloc result typed as string) that was
 * manually free()d, the page may already be unmapped, so the probe load itself
 * would fault. If IsBadReadPtr(ptr8, 8) is true we treat the pointer as
 * non-managed and branch to ret_bb. kernel32 is always linked on Windows, so
 * no extra runtime object is required. On Windows this leaves the builder
 * positioned in a fresh "readable" block; elsewhere it is a no-op. */
static void emit_header_read_guard(zan_irgen_t *g, LLVMValueRef fn,
                                   LLVMValueRef ptr8, LLVMBasicBlockRef ret_bb) {
    if (!g->target_is_windows) return;
    LLVMTypeRef i8p = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef args[] = { i8p, i64t };
    LLVMTypeRef fnty = LLVMFunctionType(i32t, args, 2, 0);
    LLVMValueRef isbad = LLVMGetNamedFunction(g->mod, "IsBadReadPtr");
    if (!isbad) isbad = LLVMAddFunction(g->mod, "IsBadReadPtr", fnty);
    LLVMValueRef cargs[] = { ptr8, LLVMConstInt(i64t, 8, 0) };
    LLVMValueRef bad = zan_call2(g->builder, fnty, isbad, cargs, 2, "badread");
    LLVMValueRef isbadnz = zan_icmp(g->builder, LLVMIntNE, bad,
        LLVMConstInt(i32t, 0, 0), "isbadnz");
    LLVMBasicBlockRef readable_bb =
        LLVMAppendBasicBlockInContext(g->ctx, fn, "readable");
    LLVMBuildCondBr(g->builder, isbadnz, ret_bb, readable_bb);
    LLVMPositionBuilderAtEnd(g->builder, readable_bb);
}

zan_status_t zan_irgen_init(zan_irgen_t *g, zan_arena_t *arena,
                            zan_diag_t *diag, zan_binder_t *binder,
                            const char *module_name,
                            const char *target_triple,
                            bool target_is_windows, bool mt_scheduler,
                            bool check_leaks) {
    memset(g, 0, sizeof(*g));
    class_index_reset();
    g->arena = arena;
    g->diag = diag;
    g->binder = binder;
    /* Set target before runtime codegen so Sleep/poll selection is correct. */
    if (target_triple && target_triple[0])
        snprintf(g->target_triple, sizeof(g->target_triple), "%s", target_triple);
    g->target_is_windows = target_is_windows;
    if (g->target_triple[0]) {
        g->target_is_macos = strstr(g->target_triple, "apple") != NULL
                             || strstr(g->target_triple, "darwin") != NULL;
    } else {
#ifdef __APPLE__
        g->target_is_macos = true;
#endif
    }
    /* Must be set before the inline coroutine driver is (conditionally)
     * emitted below, so the multi-worker mode can skip it. */
    g->mt_scheduler = mt_scheduler;

    /* --publish string obfuscation is off unless the driver turns it on; the
     * key is a fixed 16-byte pad (see emit_string_literal_rc / the .ctors
     * constructor). */
    {
        static const unsigned char zan_obf_default_key[16] = {
            0x5a, 0x67, 0xa3, 0x1c, 0xd9, 0x84, 0x2f, 0x70,
            0xbe, 0x11, 0x4d, 0xe6, 0x93, 0x28, 0xc5, 0x7a
        };
        memcpy(g->obf_key, zan_obf_default_key, 16);
    }
    g->obfuscate_strings = false;
    g->obf_literal_count = 0;

    g->ctx = LLVMContextCreate();
    g->mod = LLVMModuleCreateWithNameInContext(module_name, g->ctx);
    g->builder = LLVMCreateBuilderInContext(g->ctx);

    /* Debug info off by default; the driver flips emit_debug on for `-g` before
     * calling zan_irgen_emit. */
    g->emit_debug = false;
    g->di_builder = NULL;
    g->di_cu = NULL;
    memset(g->di_files, 0, sizeof(g->di_files));
    g->di_cur_line = 0;
    g->di_cur_file = 0;

    /* runtime-diagnostics defaults */
    g->src_file = module_name;
    g->runtime_checks = true;
    g->check_leaks = check_leaks;

    /* leak-tracking globals are always created; instrumentation and reporting
     * are gated on g->check_leaks, so normal builds still optimize them away. */
    g->g_live = LLVMAddGlobal(g->mod, LLVMInt64TypeInContext(g->ctx), "__zan_live");
    LLVMSetInitializer(g->g_live, LLVMConstInt(LLVMInt64TypeInContext(g->ctx), 0, 0));
    LLVMSetLinkage(g->g_live, LLVMInternalLinkage);

    g->leak_site_count = 0;
    {
        LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
        LLVMTypeRef i8p  = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
        g->site_live_type  = LLVMArrayType(i64t, ZAN_MAX_LEAK_SITES);
        g->site_names_type = LLVMArrayType(i8p,  ZAN_MAX_LEAK_SITES);
        g->g_site_live = LLVMAddGlobal(g->mod, g->site_live_type, "__zan_site_live");
        LLVMSetInitializer(g->g_site_live, LLVMConstNull(g->site_live_type));
        LLVMSetLinkage(g->g_site_live, LLVMInternalLinkage);
        g->g_site_names = LLVMAddGlobal(g->mod, g->site_names_type, "__zan_site_names");
        LLVMSetInitializer(g->g_site_names, LLVMConstNull(g->site_names_type));
        LLVMSetLinkage(g->g_site_names, LLVMInternalLinkage);
        /* dynamic release dispatch: per-site concrete destructor table */
        g->site_dtors_type = LLVMArrayType(i8p, ZAN_MAX_LEAK_SITES);
        g->g_site_dtors = LLVMAddGlobal(g->mod, g->site_dtors_type, "__zan_site_dtors");
        LLVMSetInitializer(g->g_site_dtors, LLVMConstNull(g->site_dtors_type));
        LLVMSetLinkage(g->g_site_dtors, LLVMInternalLinkage);
        /* per-site ancestor-name list pointers for runtime `is`/`as` checks */
        g->site_tynames_type = LLVMArrayType(i8p, ZAN_MAX_LEAK_SITES);
        g->g_site_tynames = LLVMAddGlobal(g->mod, g->site_tynames_type, "__zan_site_tynames");
        LLVMSetInitializer(g->g_site_tynames, LLVMConstNull(g->site_tynames_type));
        LLVMSetLinkage(g->g_site_tynames, LLVMInternalLinkage);
        g->site_syms = (zan_symbol_t **)calloc(ZAN_MAX_LEAK_SITES, sizeof(zan_symbol_t *));
        g->site_coll = (int *)calloc(ZAN_MAX_LEAK_SITES, sizeof(int));
        g->site_coll_elem = (zan_type_t **)calloc(ZAN_MAX_LEAK_SITES, sizeof(zan_type_t *));
        g->site_inst = (zan_type_t **)calloc(ZAN_MAX_LEAK_SITES, sizeof(zan_type_t *));
    }

    /* declare printf */
    LLVMTypeRef printf_args[] = { LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0) };
    LLVMTypeRef printf_type = LLVMFunctionType(
        LLVMInt32TypeInContext(g->ctx), printf_args, 1, 1 /* varargs */);
    LLVMValueRef printf_fn = LLVMAddFunction(g->mod, "printf", printf_type);
    g->fn_printf = printf_fn;
    g->printf_type = printf_type;

    /* declare zan_rt_println(const char*) → calls printf("%s\n", str) */
    LLVMTypeRef println_args[] = { LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0) };
    LLVMTypeRef println_type = LLVMFunctionType(
        LLVMVoidTypeInContext(g->ctx), println_args, 1, 0);
    g->rt_println = LLVMAddFunction(g->mod, "zan_rt_println", println_type);

    /* implement zan_rt_println */
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, g->rt_println, "entry");
    LLVMPositionBuilderAtEnd(g->builder, entry);

    LLVMValueRef fmt_str = LLVMBuildGlobalStringPtr(g->builder, "%s\n", "println_fmt");
    LLVMValueRef args[] = { fmt_str, LLVMGetParam(g->rt_println, 0) };
    zan_call2(g->builder, printf_type, printf_fn, args, 2, "");
    LLVMBuildRetVoid(g->builder);

    /* declare zan_rt_print_int(int64) */
    LLVMTypeRef pint_args[] = { LLVMInt64TypeInContext(g->ctx) };
    LLVMTypeRef pint_type = LLVMFunctionType(
        LLVMVoidTypeInContext(g->ctx), pint_args, 1, 0);
    g->rt_print_int = LLVMAddFunction(g->mod, "zan_rt_print_int", pint_type);

    LLVMBasicBlockRef pint_entry = LLVMAppendBasicBlockInContext(g->ctx, g->rt_print_int, "entry");
    LLVMPositionBuilderAtEnd(g->builder, pint_entry);
    LLVMValueRef int_fmt = LLVMBuildGlobalStringPtr(g->builder, "%lld\n", "int_fmt");
    LLVMValueRef iargs[] = { int_fmt, LLVMGetParam(g->rt_print_int, 0) };
    zan_call2(g->builder, printf_type, printf_fn, iargs, 2, "");
    LLVMBuildRetVoid(g->builder);

    /* declare zan_rt_print_uint(uint64) → unsigned (%llu) println for ulong */
    g->rt_print_uint = LLVMAddFunction(g->mod, "zan_rt_print_uint", pint_type);

    LLVMBasicBlockRef puint_entry = LLVMAppendBasicBlockInContext(g->ctx, g->rt_print_uint, "entry");
    LLVMPositionBuilderAtEnd(g->builder, puint_entry);
    LLVMValueRef uint_fmt = LLVMBuildGlobalStringPtr(g->builder, "%llu\n", "uint_fmt");
    LLVMValueRef uargs[] = { uint_fmt, LLVMGetParam(g->rt_print_uint, 0) };
    zan_call2(g->builder, printf_type, printf_fn, uargs, 2, "");
    LLVMBuildRetVoid(g->builder);

    /* declare zan_rt_print_double(double) */
    LLVMTypeRef pdbl_args[] = { LLVMDoubleTypeInContext(g->ctx) };
    LLVMTypeRef pdbl_type = LLVMFunctionType(
        LLVMVoidTypeInContext(g->ctx), pdbl_args, 1, 0);
    g->rt_print_double = LLVMAddFunction(g->mod, "zan_rt_print_double", pdbl_type);

    LLVMBasicBlockRef pdbl_entry = LLVMAppendBasicBlockInContext(g->ctx, g->rt_print_double, "entry");
    LLVMPositionBuilderAtEnd(g->builder, pdbl_entry);
    LLVMValueRef dbl_fmt = LLVMBuildGlobalStringPtr(g->builder, "%g\n", "dbl_fmt");
    LLVMValueRef dargs[] = { dbl_fmt, LLVMGetParam(g->rt_print_double, 0) };
    zan_call2(g->builder, printf_type, printf_fn, dargs, 2, "");
    LLVMBuildRetVoid(g->builder);

    /* declare C library functions for string interpolation */
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);

    /* int snprintf(char*, size_t, const char*, ...) */
    LLVMTypeRef snprintf_args[] = { i8ptr, i64, i8ptr };
    LLVMTypeRef snprintf_type = LLVMFunctionType(i32, snprintf_args, 3, 1);
    g->fn_snprintf = LLVMAddFunction(g->mod, "snprintf", snprintf_type);

    /* void *malloc(size_t) */
    LLVMTypeRef malloc_args[] = { i64 };
    LLVMTypeRef malloc_type = LLVMFunctionType(i8ptr, malloc_args, 1, 0);
    g->fn_malloc = LLVMAddFunction(g->mod, "malloc", malloc_type);

    /* void free(void*) */
    LLVMTypeRef free_args[] = { i8ptr };
    LLVMTypeRef free_type = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), free_args, 1, 0);
    g->fn_free = LLVMAddFunction(g->mod, "free", free_type);

    /* void exit(int) → used by runtime-check panics */
    LLVMTypeRef exit_args[] = { i32 };
    g->exit_type = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), exit_args, 1, 0);
    g->fn_exit = LLVMAddFunction(g->mod, "exit", g->exit_type);

    if (g->check_leaks) {
        /* int atexit(void(*)(void)) → used to schedule the leak report */
        LLVMTypeRef void_fn_type = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), NULL, 0, 0);
        LLVMTypeRef void_fn_ptr = LLVMPointerType(void_fn_type, 0);
        LLVMTypeRef atexit_args[] = { void_fn_ptr };
        g->atexit_type = LLVMFunctionType(i32, atexit_args, 1, 0);
        g->fn_atexit = LLVMAddFunction(g->mod, "atexit", g->atexit_type);
    }

    /* void* realloc(void*, size_t) */
    LLVMTypeRef realloc_args[] = { i8ptr, i64 };
    LLVMTypeRef realloc_type = LLVMFunctionType(i8ptr, realloc_args, 2, 0);
    g->fn_realloc = LLVMAddFunction(g->mod, "realloc", realloc_type);

    /* List struct type: { i64 count, i64 capacity, i64* data } */
    LLVMTypeRef list_fields[] = { i64, i64, LLVMPointerType(i64, 0) };
    g->list_struct_type = LLVMStructCreateNamed(g->ctx, "List");
    LLVMStructSetBody(g->list_struct_type, list_fields, 3, 0);

    /* Span<T> value type: { i8* base, i64 length }. A non-owning view over a
     * contiguous run of T (a compact array or raw memory); it is a value
     * struct, copied by value and never ARC-released. */
    LLVMTypeRef span_fields[] = { i8ptr, i64 };
    g->span_struct_type = LLVMStructCreateNamed(g->ctx, "Span");
    LLVMStructSetBody(g->span_struct_type, span_fields, 2, 0);

    /* Dict struct type:
     *   { i64 count, i64 capacity, i8** keys, i64* values,
     *     i64* index, i64 index_capacity, i64 indexed_count }
     * Entries stay in insertion order in the parallel keys/values buffers (so
     * enumeration order and the ARC release walk are unchanged); `index` is an
     * open-addressed hash index over them holding `entry + 1` per slot (0 =
     * empty), rebuilt lazily by __zan_dict_find whenever `indexed_count` no
     * longer matches `count` or a removal invalidated it (index_capacity 0). */
    LLVMTypeRef dict_fields[] = { i64, i64, LLVMPointerType(i8ptr, 0), LLVMPointerType(i64, 0),
                                  LLVMPointerType(i64, 0), i64, i64, i64 };
    g->dict_struct_type = LLVMStructCreateNamed(g->ctx, "Dict");
    LLVMStructSetBody(g->dict_struct_type, dict_fields, 8, 0);

    /* Task struct: { completed: i64, result: i64, thread_handle: i64 } */
    g->task_struct_type = LLVMStructCreateNamed(g->ctx, "Task");
    LLVMTypeRef task_fields[] = { i64, i64, i64 };
    LLVMStructSetBody(g->task_struct_type, task_fields, 3, 0);

    /* StringBuilder struct type: { i64 count, i64 capacity, i8* data } */
    LLVMTypeRef sb_fields[] = { i64, i64, i8ptr };
    g->sb_struct_type = LLVMStructCreateNamed(g->ctx, "StringBuilder");
    LLVMStructSetBody(g->sb_struct_type, sb_fields, 3, 0);

    /* async/await CPS driver ABI (see docs/ASYNC_CPS_DESIGN.md):
     *   typedef void (*zan_co_step_t)(void *frame);
     *   void zan_co_ready(void *frame, zan_co_step_t step);
     * A resume/step fn takes the heap frame (as i8*) and re-enters the state
     * machine. zan_co_ready enqueues (frame, step) on the cooperative driver. */
    LLVMTypeRef co_step_args[] = { i8ptr };
    g->co_step_type = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), co_step_args, 1, 0);
    g->co_step_ptr = LLVMPointerType(g->co_step_type, 0);
    LLVMTypeRef co_ready_args[] = { i8ptr, g->co_step_ptr };
    g->rt_co_ready_type = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), co_ready_args, 2, 0);
    g->rt_co_ready = LLVMAddFunction(g->mod, "zan_co_ready", g->rt_co_ready_type);
    /* void zan_co_sched_init(void) / void zan_co_sched_run(void): root drive */
    g->rt_co_sched_init_type = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), NULL, 0, 0);
    g->rt_co_sched_init = LLVMAddFunction(g->mod, "zan_co_sched_init", g->rt_co_sched_init_type);
    g->rt_co_sched_run_type = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), NULL, 0, 0);
    g->rt_co_sched_run = LLVMAddFunction(g->mod, "zan_co_sched_run", g->rt_co_sched_run_type);
    {
        LLVMTypeRef i64d = LLVMInt64TypeInContext(g->ctx);
        LLVMTypeRef delay_args[] = { i64d, i8ptr, g->co_step_ptr };
        g->rt_co_delay_type = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), delay_args, 3, 0);
        g->rt_co_delay = LLVMAddFunction(g->mod, "zan_co_delay", g->rt_co_delay_type);
    }

    /* Socket-async readiness reactor (S4b-2). zan_io_wait_co is an external
     * symbol resolved from the shipped zanrt_io object only when a program uses
     * socket await (otherwise unreferenced, so no link dependency).
     * zan_io_pump_timeout has a WEAK timer-only fallback; linking zanrt_io
     * overrides it with the real timeout-bounded reactor pump. */
    {
        LLVMTypeRef i64d = LLVMInt64TypeInContext(g->ctx);
        LLVMTypeRef i32d = LLVMInt32TypeInContext(g->ctx);
        LLVMTypeRef wc_args[] = { i64d, i32d, i8ptr, g->co_step_ptr };
        g->rt_io_wait_co_type = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), wc_args, 4, 0);
        g->rt_io_wait_co = LLVMAddFunction(g->mod, "zan_io_wait_co", g->rt_io_wait_co_type);
        LLVMTypeRef i64ptr = LLVMPointerType(i64d, 0);
        LLVMTypeRef rc_args[] = { i64d, i8ptr, i32d, i8ptr, g->co_step_ptr, i64ptr };
        g->rt_io_recv_co_type = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), rc_args, 6, 0);
        g->rt_io_recv_co = LLVMAddFunction(g->mod, "zan_io_recv_co", g->rt_io_recv_co_type);
        LLVMTypeRef ac_args[] = { i64d, i8ptr, g->co_step_ptr, i64ptr };
        g->rt_io_accept_co_type = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), ac_args, 4, 0);
        g->rt_io_accept_co = LLVMAddFunction(g->mod, "zan_io_accept_co", g->rt_io_accept_co_type);
        LLVMTypeRef pump_args[] = { i64d };
        g->rt_io_pump_timeout_type = LLVMFunctionType(i32d, pump_args, 1, 0);
        g->rt_io_pump_timeout = LLVMAddFunction(g->mod, "zan_io_pump_timeout",
            g->rt_io_pump_timeout_type);
        LLVMSetLinkage(g->rt_io_pump_timeout,
            g->mt_scheduler ? LLVMExternalLinkage : LLVMWeakAnyLinkage);

        LLVMTypeRef legacy_pump_type = LLVMFunctionType(i32d, NULL, 0, 0);
        LLVMValueRef legacy_pump = LLVMAddFunction(g->mod, "zan_io_pump",
            legacy_pump_type);
        LLVMSetLinkage(legacy_pump, LLVMWeakAnyLinkage);
        LLVMBasicBlockRef legacy_entry = LLVMAppendBasicBlockInContext(g->ctx,
            legacy_pump, "entry");
        LLVMPositionBuilderAtEnd(g->builder, legacy_entry);
        LLVMValueRef legacy_woke = zan_call2(g->builder,
            g->rt_io_pump_timeout_type, g->rt_io_pump_timeout,
            (LLVMValueRef[]){ LLVMConstInt(i64d, (uint64_t)-1, 1) }, 1, "woke");
        LLVMBuildRet(g->builder, legacy_woke);
    }
    g->uses_socket_async = false;

    /* Emit the cooperative coroutine driver inline (the whole Zan runtime is
     * emitted into the module, so produced programs are self-contained → see
     * the ARC/List helpers below). The ready queue is a singly-linked FIFO of
     * malloc'd nodes {next, frame, step}; zan_co_ready appends, zan_co_sched_run
     * drains, popping+freeing a node before invoking its step (which may itself
     * enqueue). Semantically equivalent to src/runtime/rt_co.c.
     *
     * Skipped under --async-workers (g->mt_scheduler): the driver symbols are
     * then left as external declarations and resolved from the multi-worker
     * reactor object (zanrt_io_mt) at link time. */
    if (!g->mt_scheduler) {
        LLVMTypeRef voidt = LLVMVoidTypeInContext(g->ctx);
        LLVMTypeRef node_fields[] = { i8ptr /*next*/, i8ptr /*frame*/, g->co_step_ptr /*step*/ };
        LLVMTypeRef node_ty = LLVMStructCreateNamed(g->ctx, "zan.co.node");
        LLVMStructSetBody(node_ty, node_fields, 3, 0);
        LLVMValueRef g_head = LLVMAddGlobal(g->mod, i8ptr, "__zan_co_head");
        LLVMSetInitializer(g_head, LLVMConstNull(i8ptr));
        LLVMSetLinkage(g_head, LLVMInternalLinkage);
        LLVMValueRef g_tail = LLVMAddGlobal(g->mod, i8ptr, "__zan_co_tail");
        LLVMSetInitializer(g_tail, LLVMConstNull(i8ptr));
        LLVMSetLinkage(g_tail, LLVMInternalLinkage);
        LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);

        /* The unified timer runtime owns a dynamic-array min-heap shared by
         * Task.Delay and public Timer entries. Public clear operations filter by
         * entry kind, so they cannot remove coroutine delays. */
        LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
        LLVMTypeRef timer_reset_type = LLVMFunctionType(voidt, NULL, 0, 0);
        LLVMValueRef timer_reset = LLVMAddFunction(g->mod, "zan_timer_runtime_reset", timer_reset_type);
        LLVMTypeRef timer_hook_type = LLVMFunctionType(voidt,
            (LLVMTypeRef[]){ g->rt_co_ready_type ? LLVMPointerType(g->rt_co_ready_type, 0) : i8ptr }, 1, 0);
        LLVMValueRef timer_set_hook = LLVMAddFunction(g->mod, "zan_timer_set_ready_hook", timer_hook_type);
        LLVMTypeRef timer_delay_type = LLVMFunctionType(voidt,
            (LLVMTypeRef[]){ i64t, i8ptr, g->co_step_ptr }, 3, 0);
        LLVMValueRef timer_delay = LLVMAddFunction(g->mod, "zan_timer_delay", timer_delay_type);
        LLVMTypeRef timer_next_type = LLVMFunctionType(i64t, NULL, 0, 0);
        LLVMValueRef timer_next = LLVMAddFunction(g->mod, "zan_timer_next_timeout", timer_next_type);
        LLVMValueRef timer_dispatch = LLVMAddFunction(g->mod, "zan_timer_dispatch_due", timer_next_type);

        /* Declare the platform sleep primitive for the *target* OS (not host):
         * Sleep (kernel32) on Windows, poll(NULL,0,ms) on POSIX/Linux. */
        LLVMTypeRef sleep_args[] = { i32t };
        LLVMTypeRef sleep_type = LLVMFunctionType(voidt, sleep_args, 1, 0);
        LLVMValueRef fn_sleep = NULL;
        LLVMTypeRef poll_args[] = { i8ptr, i64t, i32t };
        LLVMTypeRef poll_type = LLVMFunctionType(i32t, poll_args, 3, 0);
        LLVMValueRef fn_poll = NULL;
        if (g->target_is_windows)
            fn_sleep = LLVMAddFunction(g->mod, "Sleep", sleep_type);
        else
            fn_poll = LLVMAddFunction(g->mod, "poll", poll_type);

        /* Timer-only fallback for programs that do not link the IO reactor. */
        {
            LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx,
                g->rt_io_pump_timeout, "entry");
            LLVMBasicBlockRef sleep = LLVMAppendBasicBlockInContext(g->ctx,
                g->rt_io_pump_timeout, "sleep");
            LLVMBasicBlockRef ret = LLVMAppendBasicBlockInContext(g->ctx,
                g->rt_io_pump_timeout, "ret");
            LLVMPositionBuilderAtEnd(g->builder, entry);
            LLVMValueRef timeout = LLVMGetParam(g->rt_io_pump_timeout, 0);
            LLVMValueRef positive = zan_icmp(g->builder, LLVMIntSGT, timeout,
                LLVMConstInt(i64t, 0, 0), "positive");
            LLVMBuildCondBr(g->builder, positive, sleep, ret);

            LLVMPositionBuilderAtEnd(g->builder, sleep);
            LLVMValueRef timeout32 = LLVMBuildTrunc(g->builder, timeout, i32t, "timeout32");
            if (g->target_is_windows)
                zan_call2(g->builder, sleep_type, fn_sleep,
                    (LLVMValueRef[]){ timeout32 }, 1, "");
            else
                zan_call2(g->builder, poll_type, fn_poll,
                    (LLVMValueRef[]){ LLVMConstNull(i8ptr), LLVMConstInt(i64t, 0, 0), timeout32 }, 3, "");
            LLVMBuildBr(g->builder, ret);

            LLVMPositionBuilderAtEnd(g->builder, ret);
            LLVMBuildRet(g->builder, LLVMConstInt(i32t, 0, 0));
        }

        /* void zan_co_sched_init(void): reset queues and timer heap. */
        {
            LLVMBasicBlockRef bb = LLVMAppendBasicBlockInContext(g->ctx, g->rt_co_sched_init, "entry");
            LLVMPositionBuilderAtEnd(g->builder, bb);
            LLVMBuildStore(g->builder, LLVMConstNull(i8ptr), g_head);
            LLVMBuildStore(g->builder, LLVMConstNull(i8ptr), g_tail);
            zan_call2(g->builder, timer_reset_type, timer_reset, NULL, 0, "");
            zan_call2(g->builder, timer_hook_type, timer_set_hook,
                (LLVMValueRef[]){ g->rt_co_ready }, 1, "");
            LLVMBuildRetVoid(g->builder);
        }

        /* void zan_co_delay(i64 ms, i8* frame, step): register in the unified
         * timer runtime's min-heap. */
        {
            LLVMBasicBlockRef bb = LLVMAppendBasicBlockInContext(g->ctx, g->rt_co_delay, "entry");
            LLVMPositionBuilderAtEnd(g->builder, bb);
            zan_call2(g->builder, timer_delay_type, timer_delay,
                (LLVMValueRef[]){ LLVMGetParam(g->rt_co_delay, 0),
                                  LLVMGetParam(g->rt_co_delay, 1),
                                  LLVMGetParam(g->rt_co_delay, 2) }, 3, "");
            LLVMBuildRetVoid(g->builder);
        }

        /* void zan_co_ready(void* frame, step): if step, append a node. */
        {
            LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, g->rt_co_ready, "entry");
            LLVMBasicBlockRef cont  = LLVMAppendBasicBlockInContext(g->ctx, g->rt_co_ready, "cont");
            LLVMBasicBlockRef empty = LLVMAppendBasicBlockInContext(g->ctx, g->rt_co_ready, "empty");
            LLVMBasicBlockRef nonempty = LLVMAppendBasicBlockInContext(g->ctx, g->rt_co_ready, "append");
            LLVMBasicBlockRef done  = LLVMAppendBasicBlockInContext(g->ctx, g->rt_co_ready, "done");
            LLVMBasicBlockRef ret   = LLVMAppendBasicBlockInContext(g->ctx, g->rt_co_ready, "ret");
            LLVMPositionBuilderAtEnd(g->builder, entry);
            LLVMValueRef frame = LLVMGetParam(g->rt_co_ready, 0);
            LLVMValueRef step  = LLVMGetParam(g->rt_co_ready, 1);
            LLVMValueRef step_null = zan_icmp(g->builder, LLVMIntEQ, step,
                LLVMConstNull(g->co_step_ptr), "step.null");
            LLVMBuildCondBr(g->builder, step_null, ret, cont);

            LLVMPositionBuilderAtEnd(g->builder, cont);
            LLVMValueRef node = zan_call2(g->builder, malloc_type, g->fn_malloc,
                (LLVMValueRef[]){ LLVMSizeOf(node_ty) }, 1, "node");
            LLVMValueRef nnext = LLVMBuildStructGEP2(g->builder, node_ty, node, 0, "n.next");
            LLVMBuildStore(g->builder, LLVMConstNull(i8ptr), nnext);
            LLVMValueRef nframe = LLVMBuildStructGEP2(g->builder, node_ty, node, 1, "n.frame");
            LLVMBuildStore(g->builder, frame, nframe);
            LLVMValueRef nstep = LLVMBuildStructGEP2(g->builder, node_ty, node, 2, "n.step");
            LLVMBuildStore(g->builder, step, nstep);
            LLVMValueRef tail = LLVMBuildLoad2(g->builder, i8ptr, g_tail, "tail");
            LLVMValueRef is_empty = zan_icmp(g->builder, LLVMIntEQ, tail,
                LLVMConstNull(i8ptr), "q.empty");
            LLVMBuildCondBr(g->builder, is_empty, empty, nonempty);

            LLVMPositionBuilderAtEnd(g->builder, empty);
            LLVMBuildStore(g->builder, node, g_head);
            LLVMBuildBr(g->builder, done);

            LLVMPositionBuilderAtEnd(g->builder, nonempty);
            LLVMValueRef tail_next = LLVMBuildStructGEP2(g->builder, node_ty, tail, 0, "tail.next");
            LLVMBuildStore(g->builder, node, tail_next);
            LLVMBuildBr(g->builder, done);

            LLVMPositionBuilderAtEnd(g->builder, done);
            LLVMBuildStore(g->builder, node, g_tail);
            LLVMBuildBr(g->builder, ret);

            LLVMPositionBuilderAtEnd(g->builder, ret);
            LLVMBuildRetVoid(g->builder);
        }

        /* void zan_co_sched_run(void): drain the ready queue, invoking each
         * step. When it empties, poll IO up to the earliest timer deadline.
         * Re-check after every wake or timeout and exit only when ready work,
         * timers, and IO are all exhausted. */
        {
            LLVMBasicBlockRef entry_bb = LLVMAppendBasicBlockInContext(g->ctx, g->rt_co_sched_run, "entry");
            LLVMBasicBlockRef head_bb = LLVMAppendBasicBlockInContext(g->ctx, g->rt_co_sched_run, "loop");
            LLVMBasicBlockRef body_bb = LLVMAppendBasicBlockInContext(g->ctx, g->rt_co_sched_run, "body");
            LLVMBasicBlockRef last_bb = LLVMAppendBasicBlockInContext(g->ctx, g->rt_co_sched_run, "last");
            LLVMBasicBlockRef after_bb = LLVMAppendBasicBlockInContext(g->ctx, g->rt_co_sched_run, "after");
            LLVMBasicBlockRef timers_bb = LLVMAppendBasicBlockInContext(g->ctx, g->rt_co_sched_run, "timers");
            LLVMBasicBlockRef wait_timer = LLVMAppendBasicBlockInContext(g->ctx, g->rt_co_sched_run, "timer.wait");
            LLVMBasicBlockRef io_bb = LLVMAppendBasicBlockInContext(g->ctx, g->rt_co_sched_run, "io.pump");
            LLVMBasicBlockRef exit_bb = LLVMAppendBasicBlockInContext(g->ctx, g->rt_co_sched_run, "exit");
            LLVMPositionBuilderAtEnd(g->builder, entry_bb);
            LLVMBuildBr(g->builder, head_bb);

            LLVMPositionBuilderAtEnd(g->builder, head_bb);
            LLVMValueRef head = LLVMBuildLoad2(g->builder, i8ptr, g_head, "head");
            LLVMValueRef empty = zan_icmp(g->builder, LLVMIntEQ, head,
                LLVMConstNull(i8ptr), "q.empty");
            LLVMBuildCondBr(g->builder, empty, timers_bb, body_bb);

            LLVMPositionBuilderAtEnd(g->builder, body_bb);
            LLVMValueRef fr_ptr = LLVMBuildStructGEP2(g->builder, node_ty, head, 1, "n.frame");
            LLVMValueRef fr = LLVMBuildLoad2(g->builder, i8ptr, fr_ptr, "frame");
            LLVMValueRef st_ptr = LLVMBuildStructGEP2(g->builder, node_ty, head, 2, "n.step");
            LLVMValueRef st = LLVMBuildLoad2(g->builder, g->co_step_ptr, st_ptr, "step");
            LLVMValueRef nx_ptr = LLVMBuildStructGEP2(g->builder, node_ty, head, 0, "n.next");
            LLVMValueRef nx = LLVMBuildLoad2(g->builder, i8ptr, nx_ptr, "next");
            LLVMBuildStore(g->builder, nx, g_head);
            LLVMValueRef is_last = zan_icmp(g->builder, LLVMIntEQ, nx,
                LLVMConstNull(i8ptr), "is.last");
            LLVMBuildCondBr(g->builder, is_last, last_bb, after_bb);

            LLVMPositionBuilderAtEnd(g->builder, last_bb);
            LLVMBuildStore(g->builder, LLVMConstNull(i8ptr), g_tail);
            LLVMBuildBr(g->builder, after_bb);

            LLVMPositionBuilderAtEnd(g->builder, after_bb);
            zan_call2(g->builder, free_type, g->fn_free,
                (LLVMValueRef[]){ head }, 1, "");
            zan_call2(g->builder, g->co_step_type, st,
                (LLVMValueRef[]){ fr }, 1, "");
            LLVMBuildBr(g->builder, head_bb);

            LLVMPositionBuilderAtEnd(g->builder, timers_bb);
            LLVMValueRef dispatched = zan_call2(g->builder, timer_next_type,
                timer_dispatch, NULL, 0, "timer.dispatched");
            LLVMValueRef has_due = zan_icmp(g->builder, LLVMIntSGT, dispatched,
                LLVMConstInt(i64t, 0, 0), "timer.has_due");
            LLVMBuildCondBr(g->builder, has_due, head_bb, wait_timer);

            LLVMPositionBuilderAtEnd(g->builder, wait_timer);
            LLVMValueRef timeout = zan_call2(g->builder, timer_next_type,
                timer_next, NULL, 0, "timer.timeout");
            LLVMValueRef has_timer = zan_icmp(g->builder, LLVMIntSGE, timeout,
                LLVMConstInt(i64t, 0, 0), "timer.pending");
            LLVMBuildBr(g->builder, io_bb);

            LLVMPositionBuilderAtEnd(g->builder, io_bb);
            LLVMValueRef woke = zan_call2(g->builder,
                g->rt_io_pump_timeout_type, g->rt_io_pump_timeout,
                (LLVMValueRef[]){ timeout }, 1, "woke");
            LLVMValueRef more = zan_icmp(g->builder, LLVMIntSGT, woke,
                LLVMConstInt(i32t, 0, 0), "io.more");
            LLVMValueRef continue_run = LLVMBuildOr(g->builder, more, has_timer,
                "sched.more");
            LLVMBuildCondBr(g->builder, continue_run, head_bb, exit_bb);

            LLVMPositionBuilderAtEnd(g->builder, exit_bb);
            LLVMBuildRetVoid(g->builder);
        }
        (void)voidt;
    }
    /* Shared frame header, identical prefix of every async frame, so the await
     * protocol can touch a sub-frame's header fields through an i8* without
     * knowing its concrete frame type (see ASYNC_FRAME_* indices). */
    LLVMTypeRef co_hdr_fields[] = {
        LLVMInt32TypeInContext(g->ctx), LLVMInt32TypeInContext(g->ctx),
        i8ptr, g->co_step_ptr, i64,
        g->co_step_ptr, LLVMInt32TypeInContext(g->ctx),
        g->co_step_ptr, /* SELF_STEP: frame's own resume fn */
        i8ptr, i8ptr, LLVMInt32TypeInContext(g->ctx), /* pending exception */
        LLVMInt32TypeInContext(g->ctx), /* CANCEL: cancellation requested */
        i8ptr, /* CHILD: sub-frame currently awaited */
        i8ptr  /* LNEXT: live detached-frame list link */
    };
    /* Stops before ASYNC_FRAME_HSTACK: the per-handler arrays are sized per
     * function (one slot per try in that body), so they are not part of the
     * shared prefix. Only the fields above are reached through this type. */
    g->co_header_type = LLVMStructCreateNamed(g->ctx, "zan.co.header");
    LLVMStructSetBody(g->co_header_type, co_hdr_fields, 14, 0);
    g->current_async_frame = NULL;
    g->current_async_frame_type = NULL;
    g->current_async_resume_fn = NULL;
    g->current_async_ret_type = NULL;
    g->current_async_switch = NULL;
    g->current_async_next_state = 1;
    g->current_async_sub_base = 0;
    g->current_async_sub_next = 0;
    g->current_async_slots = NULL;
    g->current_async_slot_count = 0;

    /* int strcmp(const char*, const char*) */
    LLVMTypeRef strcmp_args[] = { i8ptr, i8ptr };
    LLVMTypeRef strcmp_type = LLVMFunctionType(LLVMInt32TypeInContext(g->ctx), strcmp_args, 2, 0);
    g->fn_strcmp = LLVMAddFunction(g->mod, "strcmp", strcmp_type);

    /* char *strrchr(const char*, int) */
    LLVMTypeRef strrchr_args[] = { i8ptr, LLVMInt32TypeInContext(g->ctx) };
    LLVMTypeRef strrchr_type = LLVMFunctionType(i8ptr, strrchr_args, 2, 0);
    LLVMAddFunction(g->mod, "strrchr", strrchr_type);

    /* size_t strlen(const char*) */
    LLVMTypeRef strlen_args[] = { i8ptr };
    LLVMTypeRef strlen_type = LLVMFunctionType(i64, strlen_args, 1, 0);
    g->fn_strlen = LLVMAddFunction(g->mod, "strlen", strlen_type);

    /* char *strcpy(char*, const char*) */
    LLVMTypeRef strcpy_args[] = { i8ptr, i8ptr };
    LLVMTypeRef strcpy_type = LLVMFunctionType(i8ptr, strcpy_args, 2, 0);
    g->fn_strcpy = LLVMAddFunction(g->mod, "strcpy", strcpy_type);

    /* char *strcat(char*, const char*) */
    LLVMTypeRef strcat_args[] = { i8ptr, i8ptr };
    LLVMTypeRef strcat_type = LLVMFunctionType(i8ptr, strcat_args, 2, 0);
    g->fn_strcat = LLVMAddFunction(g->mod, "strcat", strcat_type);

    /* ARC runtime: zan_rt_retain(void*) -> void */
    LLVMTypeRef retain_args[] = { i8ptr };
    LLVMTypeRef retain_type = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), retain_args, 1, 0);
    g->rt_retain = LLVMAddFunction(g->mod, "zan_rt_retain", retain_type);

    /* implement zan_rt_retain: atomically increment refcount at offset -16 */
    {
        LLVMBasicBlockRef bb = LLVMAppendBasicBlockInContext(g->ctx, g->rt_retain, "entry");
        LLVMPositionBuilderAtEnd(g->builder, bb);
        LLVMValueRef obj = LLVMGetParam(g->rt_retain, 0);
        /* null check */
        LLVMValueRef is_null = zan_icmp(g->builder, LLVMIntEQ, obj,
            LLVMConstNull(i8ptr), "isnull");
        LLVMBasicBlockRef do_retain = LLVMAppendBasicBlockInContext(g->ctx, g->rt_retain, "retain");
        LLVMBasicBlockRef ret_bb = LLVMAppendBasicBlockInContext(g->ctx, g->rt_retain, "ret");
        LLVMBuildCondBr(g->builder, is_null, ret_bb, do_retain);
        LLVMPositionBuilderAtEnd(g->builder, do_retain);
        /* refcount is the first header word, at (int64_t*)(obj - 16) */
        LLVMValueRef neg16 = LLVMConstInt(i64, (uint64_t)-16, 1);
        LLVMValueRef rc_ptr = LLVMBuildGEP2(g->builder, LLVMInt8TypeInContext(g->ctx), obj, &neg16, 1, "rcptr");
        LLVMValueRef rc_iptr = LLVMBuildBitCast(g->builder, rc_ptr,
            LLVMPointerType(i64, 0), "rciptr");
        LLVMBuildAtomicRMW(g->builder, LLVMAtomicRMWBinOpAdd, rc_iptr,
            LLVMConstInt(i64, 1, 0), LLVMAtomicOrderingMonotonic, 0);
        LLVMBuildBr(g->builder, ret_bb);
        LLVMPositionBuilderAtEnd(g->builder, ret_bb);
        LLVMBuildRetVoid(g->builder);
    }

    /* ARC runtime: zan_rt_release(void*) -> void */
    LLVMTypeRef release_args[] = { i8ptr };
    LLVMTypeRef release_type = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), release_args, 1, 0);
    g->rt_release = LLVMAddFunction(g->mod, "zan_rt_release", release_type);

    /* implement zan_rt_release: decrement refcount, free if 0 */
    {
        LLVMBasicBlockRef bb = LLVMAppendBasicBlockInContext(g->ctx, g->rt_release, "entry");
        LLVMPositionBuilderAtEnd(g->builder, bb);
        LLVMValueRef obj = LLVMGetParam(g->rt_release, 0);
        LLVMValueRef is_null = zan_icmp(g->builder, LLVMIntEQ, obj,
            LLVMConstNull(i8ptr), "isnull");
        LLVMBasicBlockRef do_release = LLVMAppendBasicBlockInContext(g->ctx, g->rt_release, "release");
        LLVMBasicBlockRef ret_bb = LLVMAppendBasicBlockInContext(g->ctx, g->rt_release, "ret");
        LLVMBuildCondBr(g->builder, is_null, ret_bb, do_release);
        LLVMPositionBuilderAtEnd(g->builder, do_release);
        LLVMValueRef neg16 = LLVMConstInt(i64, (uint64_t)-16, 1);
        LLVMValueRef neg8  = LLVMConstInt(i64, (uint64_t)-8, 1);
        LLVMValueRef rc_ptr = LLVMBuildGEP2(g->builder, LLVMInt8TypeInContext(g->ctx), obj, &neg16, 1, "rcptr");
        LLVMValueRef rc_iptr = LLVMBuildBitCast(g->builder, rc_ptr,
            LLVMPointerType(i64, 0), "rciptr");
        /* atomically decrement; LLVMBuildAtomicRMW returns the pre-op value */
        LLVMValueRef rc_old = LLVMBuildAtomicRMW(g->builder, LLVMAtomicRMWBinOpSub, rc_iptr,
            LLVMConstInt(i64, 1, 0), LLVMAtomicOrderingAcquireRelease, 0);
        LLVMValueRef rc1 = zan_sub(g->builder, rc_old, LLVMConstInt(i64, 1, 0), "rc1");
        /* if rc1 == 0, free the object (16-byte header precedes obj) */
        LLVMValueRef is_zero = zan_icmp(g->builder, LLVMIntEQ, rc1,
            LLVMConstInt(i64, 0, 0), "iszero");
        LLVMBasicBlockRef free_bb = LLVMAppendBasicBlockInContext(g->ctx, g->rt_release, "dofree");
        LLVMBuildCondBr(g->builder, is_zero, free_bb, ret_bb);
        LLVMPositionBuilderAtEnd(g->builder, free_bb);
        LLVMValueRef site = NULL;
        if (g->check_leaks) {
            /* read the allocation-site index while the object memory is still live */
            LLVMValueRef site_ptr = LLVMBuildGEP2(g->builder, LLVMInt8TypeInContext(g->ctx), obj, &neg8, 1, "sptr");
            LLVMValueRef site_iptr = LLVMBuildBitCast(g->builder, site_ptr, LLVMPointerType(i64, 0), "siptr");
            site = LLVMBuildLoad2(g->builder, i64, site_iptr, "site");
        }
        /* free(obj - 16) to include the header */
        LLVMValueRef header_ptr = LLVMBuildGEP2(g->builder, LLVMInt8TypeInContext(g->ctx), obj, &neg16, 1, "hdr");
        LLVMTypeRef free_fn_type = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx),
            (LLVMTypeRef[]){ i8ptr }, 1, 0);
        zan_call2(g->builder, free_fn_type, g->fn_free, &header_ptr, 1, "");
        if (g->check_leaks) {
            /* leak tracking: one fewer live object, and one fewer at this site */
            emit_leak_counter_add(g, g->g_live, -1);
            LLVMValueRef gidx[2] = { LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0), site };
            LLVMValueRef sc_ptr = LLVMBuildGEP2(g->builder, g->site_live_type, g->g_site_live, gidx, 2, "scptr");
            emit_leak_counter_add(g, sc_ptr, -1);
        }
        LLVMBuildBr(g->builder, ret_bb);
        LLVMPositionBuilderAtEnd(g->builder, ret_bb);
        LLVMBuildRetVoid(g->builder);
    }

    /* ARC runtime: zan_rt_release_dyn(void*) -> void
     * Read the allocation-site index recorded in the header and dispatch to
     * that site's concrete per-class destructor (releases the object's RC
     * fields, then decrements/frees). Falls back to a plain zan_rt_release
     * when the site has no destructor. Using the recorded (concrete) site
     * makes release follow the *runtime* type, so derived-instance fields are
     * freed even when the value is held through a base-typed reference. */
    {
        LLVMTypeRef reld_type = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), release_args, 1, 0);
        g->rt_release_dyn = LLVMAddFunction(g->mod, "zan_rt_release_dyn", reld_type);
        LLVMBasicBlockRef bb = LLVMAppendBasicBlockInContext(g->ctx, g->rt_release_dyn, "entry");
        LLVMPositionBuilderAtEnd(g->builder, bb);
        LLVMValueRef obj = LLVMGetParam(g->rt_release_dyn, 0);
        LLVMValueRef is_null = zan_icmp(g->builder, LLVMIntEQ, obj, LLVMConstNull(i8ptr), "isnull");
        LLVMBasicBlockRef cont = LLVMAppendBasicBlockInContext(g->ctx, g->rt_release_dyn, "cont");
        LLVMBasicBlockRef lookup = LLVMAppendBasicBlockInContext(g->ctx, g->rt_release_dyn, "lookup");
        LLVMBasicBlockRef calld = LLVMAppendBasicBlockInContext(g->ctx, g->rt_release_dyn, "calld");
        LLVMBasicBlockRef fb = LLVMAppendBasicBlockInContext(g->ctx, g->rt_release_dyn, "fallback");
        LLVMBasicBlockRef ret_bb = LLVMAppendBasicBlockInContext(g->ctx, g->rt_release_dyn, "ret");
        LLVMBuildCondBr(g->builder, is_null, ret_bb, cont);
        LLVMPositionBuilderAtEnd(g->builder, cont);
        LLVMValueRef neg8 = LLVMConstInt(i64, (uint64_t)-8, 1);
        LLVMValueRef sptr = LLVMBuildGEP2(g->builder, LLVMInt8TypeInContext(g->ctx), obj, &neg8, 1, "sptr");
        LLVMValueRef siptr = LLVMBuildBitCast(g->builder, sptr, LLVMPointerType(i64, 0), "siptr");
        LLVMValueRef site = LLVMBuildLoad2(g->builder, i64, siptr, "site");
        LLVMValueRef inrange = zan_icmp(g->builder, LLVMIntULT, site,
            LLVMConstInt(i64, ZAN_MAX_LEAK_SITES, 0), "inrange");
        LLVMBuildCondBr(g->builder, inrange, lookup, fb);
        LLVMPositionBuilderAtEnd(g->builder, lookup);
        LLVMValueRef z32 = LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
        LLVMValueRef gidx[2] = { z32, site };
        LLVMValueRef dpp = LLVMBuildGEP2(g->builder, g->site_dtors_type, g->g_site_dtors, gidx, 2, "dpp");
        LLVMValueRef dtor = LLVMBuildLoad2(g->builder, i8ptr, dpp, "dtor");
        LLVMValueRef hasd = zan_icmp(g->builder, LLVMIntNE, dtor, LLVMConstNull(i8ptr), "hasd");
        LLVMBuildCondBr(g->builder, hasd, calld, fb);
        LLVMPositionBuilderAtEnd(g->builder, calld);
        LLVMTypeRef dfnty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), &i8ptr, 1, 0);
        LLVMValueRef dfn = LLVMBuildBitCast(g->builder, dtor, LLVMPointerType(dfnty, 0), "dfn");
        zan_call2(g->builder, dfnty, dfn, &obj, 1, "");
        LLVMBuildBr(g->builder, ret_bb);
        LLVMPositionBuilderAtEnd(g->builder, fb);
        zan_call2(g->builder, LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), &i8ptr, 1, 0),
            g->rt_release, &obj, 1, "");
        LLVMBuildBr(g->builder, ret_bb);
        LLVMPositionBuilderAtEnd(g->builder, ret_bb);
        LLVMBuildRetVoid(g->builder);
    }

    /* ARC runtime: zan_rt_alloc(int64_t size, int64_t site, char *name) -> void*
     * Allocates (16 + size) bytes laid out as
     *   [i64 refcount][i64 site index][... user data ...]
     * sets refcount=1, records the site index in the header, bumps the total
     * and per-site live counts, remembers the site's "file:line:col" name, and
     * returns the pointer to the user data. */
    LLVMTypeRef alloc_args[] = { i64, i64, i8ptr };
    LLVMTypeRef alloc_type = LLVMFunctionType(i8ptr, alloc_args, 3, 0);
    g->rt_alloc = LLVMAddFunction(g->mod, "zan_rt_alloc", alloc_type);

    {
        LLVMBasicBlockRef bb = LLVMAppendBasicBlockInContext(g->ctx, g->rt_alloc, "entry");
        LLVMPositionBuilderAtEnd(g->builder, bb);
        LLVMValueRef size = LLVMGetParam(g->rt_alloc, 0);
        LLVMValueRef site = LLVMGetParam(g->rt_alloc, 1);
        LLVMValueRef name = LLVMGetParam(g->rt_alloc, 2);
        /* total = size + 16 (for the 16-byte header) */
        LLVMValueRef total = zan_add(g->builder, size, LLVMConstInt(i64, 16, 0), "total");
        LLVMTypeRef malloc_fn_type = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i64 }, 1, 0);
        LLVMValueRef raw = zan_call2(g->builder, malloc_fn_type, g->fn_malloc, &total, 1, "raw");
        emit_oom_check(g, g->rt_alloc, raw);
        /* refcount = 1 at raw[0..7] */
        LLVMValueRef rc_ptr = LLVMBuildBitCast(g->builder, raw, LLVMPointerType(i64, 0), "rcptr");
        LLVMBuildStore(g->builder, LLVMConstInt(i64, 1, 0), rc_ptr);
        /* site index at raw[8..15] */
        LLVMValueRef eight = LLVMConstInt(i64, 8, 0);
        LLVMValueRef site_ptr = LLVMBuildGEP2(g->builder, LLVMInt8TypeInContext(g->ctx), raw, &eight, 1, "sptr");
        LLVMValueRef site_iptr = LLVMBuildBitCast(g->builder, site_ptr, LLVMPointerType(i64, 0), "siptr");
        LLVMBuildStore(g->builder, site, site_iptr);
        /* user data = raw + 16 */
        LLVMValueRef sixteen = LLVMConstInt(i64, 16, 0);
        LLVMValueRef user_ptr = LLVMBuildGEP2(g->builder, LLVMInt8TypeInContext(g->ctx), raw, &sixteen, 1, "usr");
        if (g->check_leaks) {
            /* leak tracking: total + per-site count, and record the site name */
            LLVMValueRef z32 = LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
            emit_leak_counter_add(g, g->g_live, 1);
            LLVMValueRef gidx[2] = { z32, site };
            LLVMValueRef sc_ptr = LLVMBuildGEP2(g->builder, g->site_live_type, g->g_site_live, gidx, 2, "scptr");
            emit_leak_counter_add(g, sc_ptr, 1);
            LLVMValueRef nm_ptr = LLVMBuildGEP2(g->builder, g->site_names_type, g->g_site_names, gidx, 2, "nmptr");
            LLVMBuildStore(g->builder, name, nm_ptr);
        }
        LLVMBuildRet(g->builder, user_ptr);
    }

    /* ARC runtime for strings: zan_rt_str_alloc(int64_t size) -> void*
     * Allocates (16 + size) bytes laid out as
     *   [i64 refcount][i64 STRING_MAGIC][... user data ...]
     * sets refcount=1, bumps the global live count, and returns the pointer
     * to the user data. Strings intentionally do not participate in the
     * per-site leak table. */
    {
        LLVMTypeRef str_alloc_args[] = { i64 };
        LLVMTypeRef str_alloc_type = LLVMFunctionType(i8ptr, str_alloc_args, 1, 0);
        g->rt_str_alloc = LLVMAddFunction(g->mod, "zan_rt_str_alloc", str_alloc_type);
        LLVMBasicBlockRef bb = LLVMAppendBasicBlockInContext(g->ctx, g->rt_str_alloc, "entry");
        LLVMPositionBuilderAtEnd(g->builder, bb);
        LLVMValueRef size = LLVMGetParam(g->rt_str_alloc, 0);
        LLVMValueRef total = zan_add(g->builder, size, LLVMConstInt(i64, 16, 0), "total");
        LLVMTypeRef malloc_fn_type = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i64 }, 1, 0);
        LLVMValueRef raw = zan_call2(g->builder, malloc_fn_type, g->fn_malloc, &total, 1, "raw");
        emit_oom_check(g, g->rt_str_alloc, raw);
        LLVMValueRef rc_ptr = LLVMBuildBitCast(g->builder, raw, LLVMPointerType(i64, 0), "rcptr");
        LLVMBuildStore(g->builder, LLVMConstInt(i64, 1, 0), rc_ptr);
        LLVMValueRef eight = LLVMConstInt(i64, 8, 0);
        LLVMValueRef magic_ptr = LLVMBuildGEP2(g->builder, LLVMInt8TypeInContext(g->ctx), raw, &eight, 1, "magicp");
        LLVMValueRef magic_iptr = LLVMBuildBitCast(g->builder, magic_ptr, LLVMPointerType(i64, 0), "magicip");
        LLVMBuildStore(g->builder, LLVMConstInt(i64, ZAN_STRING_MAGIC, 0), magic_iptr);
        LLVMValueRef sixteen = LLVMConstInt(i64, 16, 0);
        LLVMValueRef user_ptr = LLVMBuildGEP2(g->builder, LLVMInt8TypeInContext(g->ctx), raw, &sixteen, 1, "usr");
        if (g->check_leaks) {
            emit_leak_counter_add(g, g->g_live, 1);
        }
        LLVMBuildRet(g->builder, user_ptr);
    }

    /* ARC runtime for strings: tolerant retain/release guarded by STRING_MAGIC.
     * Non-string/bare pointers and sentinel literals are ignored. */
    {
        LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
        LLVMTypeRef i8p = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
        LLVMTypeRef retain_type = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), (LLVMTypeRef[]){ i8p }, 1, 0);
        g->rt_str_retain = LLVMAddFunction(g->mod, "zan_rt_str_retain", retain_type);
        LLVMBasicBlockRef bb = LLVMAppendBasicBlockInContext(g->ctx, g->rt_str_retain, "entry");
        LLVMPositionBuilderAtEnd(g->builder, bb);
        LLVMValueRef obj = LLVMGetParam(g->rt_str_retain, 0);
        LLVMValueRef is_null = zan_icmp(g->builder, LLVMIntEQ, obj, LLVMConstNull(i8p), "isnull");
        LLVMBasicBlockRef ret_bb = LLVMAppendBasicBlockInContext(g->ctx, g->rt_str_retain, "ret");
        LLVMBasicBlockRef cont_bb = LLVMAppendBasicBlockInContext(g->ctx, g->rt_str_retain, "cont");
        LLVMBuildCondBr(g->builder, is_null, ret_bb, cont_bb);
        LLVMPositionBuilderAtEnd(g->builder, cont_bb);
        LLVMValueRef neg8 = LLVMConstInt(i64t, (uint64_t)-8, 1);
        LLVMValueRef magic_ptr = LLVMBuildGEP2(g->builder, LLVMInt8TypeInContext(g->ctx), obj, &neg8, 1, "magicp");
        emit_header_read_guard(g, g->rt_str_retain, magic_ptr, ret_bb);
        LLVMValueRef magic_iptr = LLVMBuildBitCast(g->builder, magic_ptr, LLVMPointerType(i64t, 0), "magicip");
        LLVMValueRef magic = LLVMBuildLoad2(g->builder, i64t, magic_iptr, "magic");
        LLVMValueRef has_magic = zan_icmp(g->builder, LLVMIntEQ, magic,
            LLVMConstInt(i64t, ZAN_STRING_MAGIC, 0), "hasmagic");
        LLVMBasicBlockRef retain_bb = LLVMAppendBasicBlockInContext(g->ctx, g->rt_str_retain, "retain");
        LLVMBuildCondBr(g->builder, has_magic, retain_bb, ret_bb);
        LLVMPositionBuilderAtEnd(g->builder, retain_bb);
        LLVMValueRef neg16 = LLVMConstInt(i64t, (uint64_t)-16, 1);
        LLVMValueRef rc_ptr = LLVMBuildGEP2(g->builder, LLVMInt8TypeInContext(g->ctx), obj, &neg16, 1, "rcptr");
        LLVMValueRef rc_iptr = LLVMBuildBitCast(g->builder, rc_ptr, LLVMPointerType(i64t, 0), "rciptr");
        LLVMValueRef rc = LLVMBuildLoad2(g->builder, i64t, rc_iptr, "rc");
        LLVMValueRef is_sent = zan_icmp(g->builder, LLVMIntEQ, rc,
            LLVMConstInt(i64t, ZAN_STRING_SENTINEL_RC, 0), "issent");
        LLVMBasicBlockRef add_bb = LLVMAppendBasicBlockInContext(g->ctx, g->rt_str_retain, "add");
        LLVMBuildCondBr(g->builder, is_sent, ret_bb, add_bb);
        LLVMPositionBuilderAtEnd(g->builder, add_bb);
        LLVMBuildAtomicRMW(g->builder, LLVMAtomicRMWBinOpAdd, rc_iptr,
            LLVMConstInt(i64t, 1, 0), LLVMAtomicOrderingMonotonic, 0);
        LLVMBuildBr(g->builder, ret_bb);
        LLVMPositionBuilderAtEnd(g->builder, ret_bb);
        LLVMBuildRetVoid(g->builder);
    }

    {
        LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
        LLVMTypeRef i8p = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
        LLVMTypeRef release_type = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), (LLVMTypeRef[]){ i8p }, 1, 0);
        g->rt_str_release = LLVMAddFunction(g->mod, "zan_rt_str_release", release_type);
        LLVMBasicBlockRef bb = LLVMAppendBasicBlockInContext(g->ctx, g->rt_str_release, "entry");
        LLVMPositionBuilderAtEnd(g->builder, bb);
        LLVMValueRef obj = LLVMGetParam(g->rt_str_release, 0);
        LLVMValueRef is_null = zan_icmp(g->builder, LLVMIntEQ, obj, LLVMConstNull(i8p), "isnull");
        LLVMBasicBlockRef ret_bb = LLVMAppendBasicBlockInContext(g->ctx, g->rt_str_release, "ret");
        LLVMBasicBlockRef cont_bb = LLVMAppendBasicBlockInContext(g->ctx, g->rt_str_release, "cont");
        LLVMBuildCondBr(g->builder, is_null, ret_bb, cont_bb);
        LLVMPositionBuilderAtEnd(g->builder, cont_bb);
        LLVMValueRef neg8 = LLVMConstInt(i64t, (uint64_t)-8, 1);
        LLVMValueRef magic_ptr = LLVMBuildGEP2(g->builder, LLVMInt8TypeInContext(g->ctx), obj, &neg8, 1, "magicp");
        emit_header_read_guard(g, g->rt_str_release, magic_ptr, ret_bb);
        LLVMValueRef magic_iptr = LLVMBuildBitCast(g->builder, magic_ptr, LLVMPointerType(i64t, 0), "magicip");
        LLVMValueRef magic = LLVMBuildLoad2(g->builder, i64t, magic_iptr, "magic");
        LLVMValueRef has_magic = zan_icmp(g->builder, LLVMIntEQ, magic,
            LLVMConstInt(i64t, ZAN_STRING_MAGIC, 0), "hasmagic");
        LLVMBasicBlockRef rel_bb = LLVMAppendBasicBlockInContext(g->ctx, g->rt_str_release, "release");
        LLVMBuildCondBr(g->builder, has_magic, rel_bb, ret_bb);
        LLVMPositionBuilderAtEnd(g->builder, rel_bb);
        LLVMValueRef neg16 = LLVMConstInt(i64t, (uint64_t)-16, 1);
        LLVMValueRef rc_ptr = LLVMBuildGEP2(g->builder, LLVMInt8TypeInContext(g->ctx), obj, &neg16, 1, "rcptr");
        LLVMValueRef rc_iptr = LLVMBuildBitCast(g->builder, rc_ptr, LLVMPointerType(i64t, 0), "rciptr");
        LLVMValueRef rc = LLVMBuildLoad2(g->builder, i64t, rc_iptr, "rc");
        LLVMValueRef is_sent = zan_icmp(g->builder, LLVMIntEQ, rc,
            LLVMConstInt(i64t, ZAN_STRING_SENTINEL_RC, 0), "issent");
        LLVMBasicBlockRef dec_bb = LLVMAppendBasicBlockInContext(g->ctx, g->rt_str_release, "dec");
        LLVMBuildCondBr(g->builder, is_sent, ret_bb, dec_bb);
        LLVMPositionBuilderAtEnd(g->builder, dec_bb);
        LLVMValueRef rc_old = LLVMBuildAtomicRMW(g->builder, LLVMAtomicRMWBinOpSub, rc_iptr,
            LLVMConstInt(i64t, 1, 0), LLVMAtomicOrderingAcquireRelease, 0);
        LLVMValueRef rc1 = zan_sub(g->builder, rc_old, LLVMConstInt(i64t, 1, 0), "rc1");
        LLVMValueRef is_zero = zan_icmp(g->builder, LLVMIntEQ, rc1, LLVMConstInt(i64t, 0, 0), "iszero");
        LLVMBasicBlockRef free_bb = LLVMAppendBasicBlockInContext(g->ctx, g->rt_str_release, "dofree");
        LLVMBuildCondBr(g->builder, is_zero, free_bb, ret_bb);
        LLVMPositionBuilderAtEnd(g->builder, free_bb);
        LLVMValueRef header_ptr = LLVMBuildGEP2(g->builder, LLVMInt8TypeInContext(g->ctx), obj, &neg16, 1, "hdr");
        LLVMTypeRef free_fn_type = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx),
            (LLVMTypeRef[]){ i8p }, 1, 0);
        zan_call2(g->builder, free_fn_type, g->fn_free, &header_ptr, 1, "");
        {
            emit_leak_counter_add(g, g->g_live, -1);
        }
        LLVMBuildBr(g->builder, ret_bb);
        LLVMPositionBuilderAtEnd(g->builder, ret_bb);
        LLVMBuildRetVoid(g->builder);
    }

    return ZAN_OK;
}

void zan_irgen_destroy(zan_irgen_t *g) {
    if (g->builder) LLVMDisposeBuilder(g->builder);
    if (g->mod) LLVMDisposeModule(g->mod);
    if (g->ctx) LLVMContextDispose(g->ctx);
    free(g->functions);
    g->functions = NULL;
    g->function_count = 0;
    g->function_cap = 0;
    free(g->fn_index);
    g->fn_index = NULL;
    g->fn_index_cap = 0;
    class_index_reset();
    free(g->struct_types);
    g->struct_types = NULL;
    g->struct_type_count = g->struct_type_cap = 0;
    free(g->site_inst);
    g->site_inst = NULL;
    free(g->class_release);
    g->class_release = NULL;
    g->class_release_count = g->class_release_cap = 0;
    free(g->ctors);
    g->ctors = NULL;
    g->ctor_count = g->ctor_cap = 0;
    free(g->generic_fns);
    g->generic_fns = NULL;
    g->generic_fn_count = g->generic_fn_cap = 0;
    free(g->generic_ctors);
    g->generic_ctors = NULL;
    g->generic_ctor_count = g->generic_ctor_cap = 0;
    free(g->generic_insts);
    g->generic_insts = NULL;
    g->generic_inst_count = g->generic_inst_cap = 0;
    free(g->method_specs);
    g->method_specs = NULL;
    g->method_spec_count = g->method_spec_cap = g->method_spec_emitted = 0;
    free(g->static_fields);
    g->static_fields = NULL;
    g->static_field_count = g->static_field_cap = 0;
    g->cur_mtps = NULL;
    g->cur_mbind = NULL;
}

/* ---- type mapping ---- */

/* forward decls into later-included parts of this translation unit */
static zan_type_t *subst_method_tp(zan_irgen_t *g, zan_type_t *t,
                                   zan_ast_list_t *tps, zan_type_t **bind);
static int get_or_create_method_spec(zan_irgen_t *g, zan_symbol_t *msym,
                                     zan_type_t **bind, int bindc,
                                     zan_type_t *owner_inst);
static void emit_pending_method_specs(zan_irgen_t *g);

/* Resolve a type reference appearing in the body currently being emitted,
 * substituting the active method specialization's type parameters (T -> the
 * concrete type bound at the call site that created the specialization). */
static zan_type_t *subst_type_param_deep(zan_irgen_t *g, zan_type_t *t,
                                         zan_type_t *recv);

static zan_type_t *resolve_type_ctx(zan_irgen_t *g, zan_ast_node_t *tref) {
    zan_type_t *t = zan_binder_resolve_type(g->binder, tref);
    if (g->cur_mtps && t) t = subst_method_tp(g, t, g->cur_mtps, g->cur_mbind);
    /* A type written in the source of a specialized body means the concrete
     * type: a local declared `T` in Box<Square> holds a Square and owns it like
     * one, and `new List<T>()` builds a list of Squares. Left as a type
     * parameter, such a local was not rc-managed, so it borrowed a value that
     * was released underneath it. */
    if (t && g->cur_inst) t = subst_type_param_deep(g, t, g->cur_inst);
    return t;
}

/* ---- nullable value types (`int?`, `Point?`) -------------------------------
 *
 * A nullable value type lowers to `{ payload, i1 }`: the value itself plus a
 * has-value flag. The struct is *named* (`zan.nullable.<payload>`) so that the
 * wrap/unwrap boundaries below can recognise it from the LLVM type alone
 * without ever mistaking a user struct that happens to have the same shape. */
#define ZAN_NULLABLE_PREFIX "zan.nullable."

static LLVMValueRef coerce_int_to(zan_irgen_t *g, LLVMValueRef v, LLVMTypeRef target);

static bool llvm_is_nullable(LLVMTypeRef t) {
    if (!t || LLVMGetTypeKind(t) != LLVMStructTypeKind) return false;
    const char *n = LLVMGetStructName(t);
    return n && strncmp(n, ZAN_NULLABLE_PREFIX,
                        sizeof(ZAN_NULLABLE_PREFIX) - 1) == 0;
}

static LLVMTypeRef nullable_payload_type(LLVMTypeRef t) {
    return LLVMStructGetTypeAtIndex(t, 0);
}

/* The nullable struct for a payload LLVM type, created once per payload. The
 * payload's own printed name keys the type, so `int?` written in two places is
 * one LLVM type and the values are interchangeable. */
static LLVMTypeRef nullable_type_of(zan_irgen_t *g, LLVMTypeRef payload) {
    char name[256];
    size_t off = 0;
    memcpy(name, ZAN_NULLABLE_PREFIX, sizeof(ZAN_NULLABLE_PREFIX) - 1);
    off = sizeof(ZAN_NULLABLE_PREFIX) - 1;
    char *pn = LLVMPrintTypeToString(payload);
    for (const char *p = pn; p && *p && off < sizeof(name) - 1; p++) {
        char c = (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
                 (*p >= '0' && *p <= '9') ? *p : '_';
        name[off++] = c;
    }
    name[off] = 0;
    if (pn) LLVMDisposeMessage(pn);
    LLVMTypeRef existing = LLVMGetTypeByName2(g->ctx, name);
    if (existing) return existing;
    LLVMTypeRef st = LLVMStructCreateNamed(g->ctx, name);
    LLVMTypeRef body[2] = { payload, LLVMInt1TypeInContext(g->ctx) };
    LLVMStructSetBody(st, body, 2, 0);
    return st;
}

/* The `null` of a nullable type: a zeroed payload with the flag clear. */
static LLVMValueRef nullable_none(LLVMTypeRef nty) {
    return LLVMConstNull(nty);
}

/* Wrap a payload value, fitting it to the payload slot first (an i64 literal
 * meeting an `int?` narrows exactly as it would meeting an `int`). Returns NULL
 * when the value cannot be a payload of this type, so callers can leave the
 * value alone instead of building invalid IR. */
static LLVMValueRef nullable_some(zan_irgen_t *g, LLVMTypeRef nty, LLVMValueRef v) {
    LLVMTypeRef pl = nullable_payload_type(nty);
    LLVMValueRef fit = coerce_int_to(g, v, pl);
    if (LLVMTypeOf(fit) != pl) return NULL;
    LLVMValueRef agg = LLVMBuildInsertValue(g->builder, LLVMGetUndef(nty), fit,
                                            0, "nv.val");
    return LLVMBuildInsertValue(g->builder, agg,
                                LLVMConstInt(LLVMInt1TypeInContext(g->ctx), 1, 0),
                                1, "nv.some");
}

static LLVMValueRef nullable_has_value(zan_irgen_t *g, LLVMValueRef v) {
    return LLVMBuildExtractValue(g->builder, v, 1, "nv.has");
}

static LLVMValueRef nullable_get_payload(zan_irgen_t *g, LLVMValueRef v) {
    return LLVMBuildExtractValue(g->builder, v, 0, "nv.get");
}

static LLVMTypeRef map_type(zan_irgen_t *g, zan_type_t *type) {
    if (!type) return LLVMVoidTypeInContext(g->ctx);
    switch (type->kind) {
    case TYPE_VOID:   return LLVMVoidTypeInContext(g->ctx);
    case TYPE_BOOL:   return LLVMInt1TypeInContext(g->ctx);
    case TYPE_BYTE:   return LLVMInt8TypeInContext(g->ctx);
    case TYPE_SHORT:  return LLVMInt16TypeInContext(g->ctx);
    case TYPE_INT:    return LLVMInt32TypeInContext(g->ctx);
    case TYPE_LONG:   return LLVMInt64TypeInContext(g->ctx);
    case TYPE_SBYTE:  return LLVMInt8TypeInContext(g->ctx);
    case TYPE_USHORT: return LLVMInt16TypeInContext(g->ctx);
    case TYPE_UINT:   return LLVMInt32TypeInContext(g->ctx);
    case TYPE_ULONG:  return LLVMInt64TypeInContext(g->ctx);
    case TYPE_NINT:   return LLVMInt64TypeInContext(g->ctx);
    case TYPE_FLOAT:  return LLVMFloatTypeInContext(g->ctx);
    case TYPE_DOUBLE: return LLVMDoubleTypeInContext(g->ctx);
    case TYPE_CHAR:   return LLVMInt64TypeInContext(g->ctx);
    case TYPE_STRING: return LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    case TYPE_OBJECT: return LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    case TYPE_ENUM:
        return LLVMInt64TypeInContext(g->ctx);
    case TYPE_NULLABLE:
        /* Only value types reach here: the binder leaves `T?` over a reference
         * type as plain T, which already admits null. */
        if (type->element_type)
            return nullable_type_of(g, map_type(g, type->element_type));
        return LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    case TYPE_DELEGATE: {
        /* delegate types map to function pointer types */
        int pc = type->delegate_param_count;
        LLVMTypeRef *param_types = (LLVMTypeRef *)calloc(
            (size_t)(pc > 0 ? pc : 1), sizeof(LLVMTypeRef));
        for (int i = 0; i < pc; i++) {
            param_types[i] = map_type(g, type->delegate_param_types[i]);
        }
        /* An `async delegate` lowers like an async method's ramp: invoking it
         * returns an i8* task handle (the coroutine frame), which `await` then
         * drives -- not the declared return type directly. */
        LLVMTypeRef ret = type->delegate_is_async
            ? LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0)
            : (type->delegate_ret_type
                ? map_type(g, type->delegate_ret_type)
                : LLVMVoidTypeInContext(g->ctx));
        LLVMTypeRef fn_type = LLVMFunctionType(ret, param_types, (unsigned)pc, 0);
        free(param_types);
        return LLVMPointerType(fn_type, 0);
    }
    case TYPE_STRUCT:
    case TYPE_CLASS: {
        if (type->name.len == 4 && memcmp(type->name.str, "Span", 4) == 0)
            return g->span_struct_type;
        /* look up registered struct type */
        for (int i = 0; i < g->struct_type_count; i++) {
            if (g->struct_types[i].sym == type->sym) {
                if (type->kind == TYPE_CLASS) {
                    return LLVMPointerType(g->struct_types[i].llvm_type, 0);
                }
                return g->struct_types[i].llvm_type;
            }
        }
        return LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    }
    default:          return LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    }
}

/* Widen a value just loaded from a slot of `type` back to the register form
 * the rest of irgen expects. Memory keeps every integer at its declared width,
 * but the extension a load needs is a property of the Zan type, not of the
 * LLVM width: i8 is `byte` (zero) or `sbyte` (sign), i16 is `short` (sign) or
 * `ushort` (zero), i32 is `int` (sign) or `uint` (zero). The width-driven
 * default handles the signed side and `byte`; the unsigned wide types and
 * `sbyte` are the cases it gets backwards, so they extend explicitly and land
 * in the 64-bit masked form that casts and arithmetic already assume. */
static LLVMValueRef promote_loaded(zan_irgen_t *g, LLVMValueRef v,
                                   zan_type_t *type) {
    if (!v || !type) return v;
    if (LLVMGetTypeKind(LLVMTypeOf(v)) != LLVMIntegerTypeKind) return v;
    if (LLVMGetIntTypeWidth(LLVMTypeOf(v)) >= 64) return v;
    LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
    switch (type->kind) {
    case TYPE_SBYTE:
        return LLVMBuildSExt(g->builder, v, i64t, "sx.i8");
    case TYPE_USHORT:
        return LLVMBuildZExt(g->builder, v, i64t, "zx.u16");
    case TYPE_UINT:
        return LLVMBuildZExt(g->builder, v, i64t, "zx.u32");
    default:
        return v;
    }
}

/* ---- struct type registry helpers ---- */

/* Forward decl: classes with any virtual/override method carry a hidden
 * vtable pointer as struct field 0, so field indexing and layout are shifted
 * by one for them. */
static bool class_has_virtual_methods(zan_symbol_t *sym);
static int class_vptr_offset(zan_symbol_t *sym) {
    return class_has_virtual_methods(sym) ? 1 : 0;
}

static LLVMTypeRef get_struct_llvm_type(zan_irgen_t *g, zan_symbol_t *sym) {
    for (int i = 0; i < g->struct_type_count; i++) {
        if (g->struct_types[i].sym == sym) return g->struct_types[i].llvm_type;
    }
    return NULL;
}

static struct zan_struct_type_entry *get_struct_entry(zan_irgen_t *g,
                                                      zan_symbol_t *sym) {
    for (int i = 0; i < g->struct_type_count; i++) {
        if (g->struct_types[i].sym == sym) return &g->struct_types[i];
    }
    return NULL;
}

/* Address a field of an instance. A sequential struct is laid out by LLVM
 * exactly like the C compiler would, so its fields are reached by index; an
 * explicit-layout struct is one opaque block, so the field's own offset is
 * applied and then re-typed (a GEP of the field type, so that a store to it
 * still sees what it is storing into). */
static LLVMValueRef emit_field_ptr(zan_irgen_t *g, zan_symbol_t *type_sym,
                                   LLVMTypeRef st, LLVMValueRef base,
                                   int fi, const char *name) {
    struct zan_struct_type_entry *e = type_sym ? get_struct_entry(g, type_sym) : NULL;
    if (e && e->explicit_layout && fi >= 0 && fi < e->field_count) {
        LLVMValueRef off = LLVMConstInt(LLVMInt64TypeInContext(g->ctx),
                                        e->field_offsets[fi], 0);
        LLVMValueRef raw = LLVMBuildInBoundsGEP2(g->builder,
            LLVMInt8TypeInContext(g->ctx), base, &off, 1, "fld.off");
        /* Re-typed as a one-field struct rather than as the bare field type:
         * a store to it then recovers what it is storing into the same way it
         * does for a sequential field, instead of writing the value's own
         * width over the neighbouring fields. */
        LLVMTypeRef wrap = LLVMStructTypeInContext(g->ctx, &e->field_llvm[fi], 1, 0);
        return LLVMBuildStructGEP2(g->builder, wrap, raw, 0, name);
    }
    return LLVMBuildStructGEP2(g->builder, st, base, (unsigned)fi, name);
}

/* Static fields live in module globals, not in the instance struct: they are
 * excluded from instance layout and field indexing. */
static bool field_member_is_static(zan_symbol_t *m) {
    if (m->modifiers & MOD_STATIC) return true;
    if (m->kind == SYM_FIELD && m->decl &&
        (m->decl->field_decl.modifiers & MOD_STATIC)) return true;
    return false;
}

static int get_field_index(zan_symbol_t *type_sym, zan_istr_t field_name) {
    int idx = class_vptr_offset(type_sym);
    for (int i = 0; i < type_sym->member_count; i++) {
        if (type_sym->members[i]->kind == SYM_FIELD ||
            type_sym->members[i]->kind == SYM_PROPERTY) {
            if (field_member_is_static(type_sym->members[i])) continue;
            if (type_sym->members[i]->name.len == field_name.len &&
                memcmp(type_sym->members[i]->name.str, field_name.str, field_name.len) == 0) {
                return idx;
            }
            idx++;
        }
    }
    return -1;
}

static zan_symbol_t *get_field_sym(zan_symbol_t *type_sym, zan_istr_t field_name) {
    zan_class_index_t *ci = class_index_for(type_sym);
    for (int i = member_first_named(ci, field_name); i >= 0;
         i = member_next_named(ci, i)) {
        zan_symbol_t *m = type_sym->members[i];
        if ((m->kind == SYM_FIELD || m->kind == SYM_PROPERTY) &&
            member_name_is(m, field_name)) {
            return m;
        }
    }
    return NULL;
}

static zan_symbol_t *get_method_sym(zan_symbol_t *type_sym, zan_istr_t method_name) {
    /* search in current type first */
    zan_class_index_t *ci = class_index_for(type_sym);
    for (int i = member_first_named(ci, method_name); i >= 0;
         i = member_next_named(ci, i)) {
        zan_symbol_t *m = type_sym->members[i];
        if (m->kind == SYM_METHOD && member_name_is(m, method_name))
            return m;
    }
    /* search in base type (inheritance) */
    if (type_sym->type && type_sym->type->base_type && type_sym->type->base_type->sym) {
        return get_method_sym(type_sym->type->base_type->sym, method_name);
    }
    /* An interface re-exports the members of the interfaces it extends, and
     * those sit in the extends list rather than in base_type. Without this the
     * call's static type is unknown at the call site, so e.g. an owned string
     * returned through the derived interface is never released. */
    if (type_sym->kind == SYM_INTERFACE && type_sym->type) {
        for (int i = 0; i < type_sym->type->interface_count; i++) {
            zan_type_t *it = type_sym->type->interfaces[i];
            if (!it || !it->sym || it->sym == type_sym) continue;
            zan_symbol_t *m = get_method_sym(it->sym, method_name);
            if (m) return m;
        }
    }
    return NULL;
}

/* Return the symbol that was created for a specific method-declaration AST node.
 * With method overloading, several same-named SYM_METHOD members coexist on a
 * type; each corresponds to exactly one AST_METHOD_DECL. Matching on the decl
 * back-pointer (not the name) is the only way to recover the right one. */
static zan_symbol_t *method_sym_for_decl(zan_symbol_t *type_sym, zan_ast_node_t *decl) {
    zan_class_index_t *ci = class_index_for(type_sym);
    for (int i = ci->decl_buckets[irgen_ptr_bucket(decl, ci->cap)]; i >= 0;
         i = ci->decl_next[i]) {
        zan_symbol_t *m = type_sym->members[i];
        if (m->kind == SYM_METHOD && m->decl == decl) return m;
    }
    return NULL;
}

/* Overload-aware method resolution used at call sites. Among same-named methods
 * pick the one whose declared parameter count matches the number of arguments
 * supplied at the call. When several remain (same arity) the first is returned;
 * when none match on arity the first same-named method is returned so that
 * behaviour degrades to the historical name-only lookup instead of failing. */
static int method_is_params_variadic(zan_symbol_t *m) {
    if (!m->decl || m->decl->method_decl.params.count == 0) return 0;
    zan_ast_node_t *last =
        m->decl->method_decl.params.items[m->decl->method_decl.params.count - 1];
    return last->kind == AST_PARAM && last->param.is_params;
}

static zan_symbol_t *resolve_overload(zan_symbol_t *type_sym, zan_istr_t name, int argc) {
    zan_symbol_t *first = NULL;
    zan_symbol_t *variadic = NULL;
    zan_class_index_t *ci = class_index_for(type_sym);
    for (int i = member_first_named(ci, name); i >= 0;
         i = member_next_named(ci, i)) {
        zan_symbol_t *m = type_sym->members[i];
        if (m->kind != SYM_METHOD) continue;
        if (!member_name_is(m, name)) continue;
        if (!first) first = m;
        if (m->decl && m->decl->method_decl.params.count == argc) {
            return m;
        }
        if (!variadic && method_is_params_variadic(m) &&
            argc >= m->decl->method_decl.params.count - 1) {
            variadic = m;
        }
    }
    if (variadic) return variadic;
    if (first) return first;
    if (type_sym->type && type_sym->type->base_type && type_sym->type->base_type->sym) {
        return resolve_overload(type_sym->type->base_type->sym, name, argc);
    }
    return NULL;
}

/* Overload resolution for a receiver whose static type is an interface. An
 * interface re-exports the members of the interfaces it extends
 * (`interface IDbConnection : IDbExecutor`), and those live in the extends
 * list rather than in base_type, so resolve_overload alone would miss an
 * inherited method and the call would dispatch to nothing. */
static zan_symbol_t *resolve_iface_overload(zan_symbol_t *iface, zan_istr_t name,
                                            int argc) {
    if (!iface) return NULL;
    zan_symbol_t *m = resolve_overload(iface, name, argc);
    if (m) return m;
    if (!iface->type) return NULL;
    for (int i = 0; i < iface->type->interface_count; i++) {
        zan_type_t *it = iface->type->interfaces[i];
        if (!it || !it->sym || it->sym == iface) continue;
        m = resolve_iface_overload(it->sym, name, argc);
        if (m) return m;
    }
    return NULL;
}

/* Defined in irgen_abi.c, which is part of this translation unit. */
static unsigned long abi_size_of(LLVMTypeRef t);
static unsigned long abi_align_of(LLVMTypeRef t);

static bool decl_is_explicit_layout(zan_symbol_t *sym) {
    return sym->decl &&
           (sym->decl->kind == AST_STRUCT_DECL || sym->decl->kind == AST_CLASS_DECL) &&
           sym->decl->type_decl.is_explicit_layout;
}

/* [FieldOffset(n)] -- the byte offset a field of an explicit-layout type sits
 * at. Two fields may name the same offset, which is how a union is written. */
static bool field_offset_attr(zan_symbol_t *field, unsigned long *out) {
    if (!field->decl) return false;
    for (int i = 0; i < field->decl->attributes.count; i++) {
        zan_ast_node_t *a = field->decl->attributes.items[i];
        if (a->kind != AST_ATTRIBUTE || !a->attribute.name) continue;
        zan_istr_t n = a->attribute.name->ident.name;
        if (!n.str || n.len != 11 || memcmp(n.str, "FieldOffset", 11) != 0) continue;
        if (a->attribute.args.count < 1) continue;
        zan_ast_node_t *v = a->attribute.args.items[0];
        if (v->kind != AST_INT_LITERAL || v->int_val < 0) continue;
        *out = (unsigned long)v->int_val;
        return true;
    }
    return false;
}

static void register_struct_type(zan_irgen_t *g, zan_symbol_t *sym) {
    if (get_struct_llvm_type(g, sym)) return;
    g->struct_types = irgen_grow(g->struct_types, &g->struct_type_cap,
                                 g->struct_type_count + 1,
                                 sizeof(*g->struct_types));

    /* Create and register the named struct *before* resolving field types,
     * so that a self- or mutually-referential class field (a pointer to this
     * type) resolves to `%struct.X*` via map_type instead of falling back to
     * i8* → which produced type-mismatched IR (rejected under typed pointers). */
    char name_buf[256];
    snprintf(name_buf, sizeof(name_buf), "struct.%.*s", (int)sym->name.len, sym->name.str);
    LLVMTypeRef st = LLVMStructCreateNamed(g->ctx, name_buf);
    g->struct_types[g->struct_type_count].sym = sym;
    g->struct_types[g->struct_type_count].llvm_type = st;
    g->struct_type_count++;

    /* count fields (including auto-property backing fields; statics excluded) */
    int field_count = 0;
    for (int i = 0; i < sym->member_count; i++) {
        if ((sym->members[i]->kind == SYM_FIELD ||
             sym->members[i]->kind == SYM_PROPERTY) &&
            !field_member_is_static(sym->members[i])) field_count++;
    }

    /* Reserve field 0 for a hidden vtable pointer on classes that participate
     * in virtual dispatch. Base fields are flattened in first, so a derived
     * instance stays layout-compatible with its base (vptr at 0, then base
     * fields, then derived fields). */
    int vptr = class_has_virtual_methods(sym) ? 1 : 0;
    LLVMTypeRef *field_types = (LLVMTypeRef *)calloc((size_t)(field_count + vptr), sizeof(LLVMTypeRef));
    int fi = 0;
    if (vptr) {
        field_types[fi++] = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    }
    for (int i = 0; i < sym->member_count; i++) {
        if ((sym->members[i]->kind == SYM_FIELD ||
             sym->members[i]->kind == SYM_PROPERTY) &&
            !field_member_is_static(sym->members[i])) {
            field_types[fi++] = map_type(g, sym->members[i]->type);
        }
    }

    struct zan_struct_type_entry *entry = &g->struct_types[g->struct_type_count - 1];
    entry->field_count = field_count + vptr;
    entry->field_llvm = field_types;
    entry->explicit_layout = decl_is_explicit_layout(sym);

    if (!entry->explicit_layout) {
        /* Sequential (the default, and what [StructLayout] asks for): LLVM
         * already pads and aligns a non-packed struct the way C does. */
        LLVMStructSetBody(st, field_types, (unsigned)(field_count + vptr), 0);
        return;
    }

    entry->field_offsets = (unsigned long *)calloc((size_t)(field_count + vptr),
                                                   sizeof(unsigned long));
    unsigned long size = 0, align = 1;
    int slot = vptr;
    if (vptr) {
        /* A vtable pointer has no [FieldOffset] to place it at, and a type
         * whose layout C code depends on has no business dispatching. */
        zan_diag_emit(g->diag, DIAG_ERROR, sym->decl ? sym->decl->loc : zan_loc(0, 0, 0, 0),
                      "explicit-layout type '%.*s' cannot have virtual methods",
                      (int)sym->name.len, sym->name.str);
    }
    for (int i = 0; i < sym->member_count; i++) {
        zan_symbol_t *m = sym->members[i];
        if ((m->kind != SYM_FIELD && m->kind != SYM_PROPERTY) ||
            field_member_is_static(m)) continue;
        unsigned long off = 0;
        if (!field_offset_attr(m, &off)) {
            zan_diag_emit(g->diag, DIAG_ERROR, m->decl ? m->decl->loc : sym->decl->loc,
                          "field '%.*s' of explicit-layout type '%.*s' needs [FieldOffset(n)]",
                          (int)m->name.len, m->name.str,
                          (int)sym->name.len, sym->name.str);
        }
        entry->field_offsets[slot] = off;
        unsigned long fa = abi_align_of(field_types[slot]);
        if (off % fa != 0) {
            zan_diag_emit(g->diag, DIAG_ERROR, m->decl ? m->decl->loc : sym->decl->loc,
                          "[FieldOffset(%lu)] on '%.*s' is not %lu-byte aligned",
                          off, (int)m->name.len, m->name.str, fa);
        }
        if (fa > align) align = fa;
        unsigned long end = off + abi_size_of(field_types[slot]);
        if (end > size) size = end;
        slot++;
    }
    if (size % align) size += align - size % align;

    /* One block, with a leading integer of the widest field's alignment so
     * that the block itself is aligned the way the C type is. */
    LLVMTypeRef body[2];
    unsigned nbody = 0;
    body[nbody++] = LLVMIntTypeInContext(g->ctx, (unsigned)(align * 8));
    if (size > align)
        body[nbody++] = LLVMArrayType(LLVMInt8TypeInContext(g->ctx),
                                      (unsigned)(size - align));
    LLVMStructSetBody(st, body, nbody, 0);
}

/* ---- virtual dispatch helpers ---- */

static bool class_has_virtual_methods(zan_symbol_t *sym) {
    for (int i = 0; i < sym->member_count; i++) {
        if (sym->members[i]->kind == SYM_METHOD &&
            (sym->members[i]->modifiers & (MOD_VIRTUAL | MOD_OVERRIDE))) {
            return true;
        }
    }
    /* a class implementing an interface needs a field-0 vtable pointer to serve
     * as a runtime type tag for interface (tag) dispatch, even with no virtual
     * methods of its own. */
    if (sym->type && sym->type->interface_count > 0) return true;
    /* check base class */
    if (sym->type && sym->type->base_type && sym->type->base_type->sym) {
        return class_has_virtual_methods(sym->type->base_type->sym);
    }
    return false;
}

static int count_virtual_methods(zan_symbol_t *sym) {
    int count = 0;
    /* count from base first */
    if (sym->type && sym->type->base_type && sym->type->base_type->sym) {
        count = count_virtual_methods(sym->type->base_type->sym);
    }
    /* add new virtual methods from this class */
    for (int i = 0; i < sym->member_count; i++) {
        if (sym->members[i]->kind == SYM_METHOD &&
            (sym->members[i]->modifiers & MOD_VIRTUAL) &&
            !(sym->members[i]->modifiers & MOD_OVERRIDE)) {
            count++;
        }
    }
    return count;
}

static int get_virtual_method_index(zan_symbol_t *type_sym, zan_istr_t method_name) {
    /* search base classes first for the method slot */
    if (type_sym->type && type_sym->type->base_type && type_sym->type->base_type->sym) {
        int idx = get_virtual_method_index(type_sym->type->base_type->sym, method_name);
        if (idx >= 0) return idx;
    }
    /* search current class virtual methods */
    int base_count = 0;
    if (type_sym->type && type_sym->type->base_type && type_sym->type->base_type->sym) {
        base_count = count_virtual_methods(type_sym->type->base_type->sym);
    }
    int idx = base_count;
    for (int i = 0; i < type_sym->member_count; i++) {
        if (type_sym->members[i]->kind == SYM_METHOD &&
            (type_sym->members[i]->modifiers & MOD_VIRTUAL) &&
            !(type_sym->members[i]->modifiers & MOD_OVERRIDE)) {
            if (type_sym->members[i]->name.len == method_name.len &&
                memcmp(type_sym->members[i]->name.str, method_name.str, method_name.len) == 0) {
                return idx;
            }
            idx++;
        }
    }
    return -1;
}

/* ---- local variables (simple stack-based storage) ---- */

#define MAX_LOCALS 256

typedef struct {
    zan_istr_t name;
    LLVMValueRef alloca;
    zan_type_t *type;
    /* ARC: 1 when this local owns a heap class reference that must be released
     * on overwrite and at function exit; 0 for params, borrowed, escaped or
     * non-class locals. See the ARC helpers below. */
    int arc_owned;
    /* For a local owning an rc-element array from `new T[n]`, the i64 alloca
     * holding its element count; NULL otherwise. Elements are released at
     * scope exit. */
    LLVMValueRef arr_len;
    /* For any local initialized with `new T[n]`, the i64 alloca holding the
     * element count so `a.Length` can read it; NULL when unknown. */
    LLVMValueRef arr_len_slot;
    /* 1 when this local's slot is registered on the unwind stack, so an
     * exception thrown below this frame releases it (A8-12). Scope exit pops
     * the entry along with the release it emits. */
    int eh_slot;
    /* A33-2b: for a local a lambda assigns to, the heap cell holding its value
     * (closure-record shaped, see the boxed-local comment in irgen_expr.c).
     * `alloca` then points *into* that cell, so the enclosing method and every
     * closure read and write the one variable. NULL for an ordinary local. */
    LLVMValueRef box_cell;
    /* 1 when this binding owns a reference to the cell (the declaring scope);
     * a lambda body's binding borrows the cell its closure record holds. */
    int          box_owned;
    /* An `object` slot holds whatever the language puts in a pointer-wide slot:
     * a heap class reference, a string literal in static storage, or an
     * unboxed scalar. Its static type therefore cannot decide ownership, so an
     * i1 slot records whether the *current* occupant is an owned heap
     * reference; the next store and scope exit release it only when set. NULL
     * for every other local. */
    LLVMValueRef obj_rc_flag;
} local_var_t;

/* A function's locals live in a single flat scope. The backing array grows
 * on demand: a fixed cap used to silently drop overflow locals, which turned
 * an over-large function into miscompiled code (unregistered locals resolved
 * to bogus storage, corrupting memory). Growth keeps large functions correct. */
typedef struct {
    local_var_t *vars;
    int count;
    int cap;
    zan_arena_t *arena;
} local_scope_t;

static void local_scope_init(local_scope_t *s, zan_arena_t *arena) {
    s->count = 0;
    s->cap = MAX_LOCALS;
    s->arena = arena;
    s->vars = (local_var_t *)zan_arena_alloc(arena, sizeof(local_var_t) * (size_t)s->cap);
}

static local_scope_t *local_scope_new(zan_arena_t *arena) {
    local_scope_t *s = (local_scope_t *)zan_arena_alloc(arena, sizeof(local_scope_t));
    local_scope_init(s, arena);
    return s;
}

/* Storage slot type of a local. A `ref`/`out` parameter's slot is the raw
 * incoming pointer parameter (not an alloca instruction), so derive the type
 * from the zan type instead of LLVMGetAllocatedType in that case. */
static LLVMTypeRef map_type(zan_irgen_t *g, zan_type_t *type);
static LLVMTypeRef local_slot_type(zan_irgen_t *g, local_var_t *v) {
    if (LLVMIsAAllocaInst(v->alloca)) return LLVMGetAllocatedType(v->alloca);
    return map_type(g, v->type);
}

/* Bumped whenever a local is declared: the expression type cache keys on it so
 * a changed binding can never be served a stale inference. */
static unsigned g_local_gen;

static void local_add(local_scope_t *scope, zan_istr_t name, LLVMValueRef alloca, zan_type_t *type) {
    g_local_gen++;
    if (scope->count >= scope->cap) {
        int new_cap = scope->cap > 0 ? scope->cap * 2 : MAX_LOCALS;
        local_var_t *grown = (local_var_t *)zan_arena_alloc(scope->arena,
            sizeof(local_var_t) * (size_t)new_cap);
        if (scope->count > 0 && scope->vars) {
            memcpy(grown, scope->vars, sizeof(local_var_t) * (size_t)scope->count);
        }
        scope->vars = grown;
        scope->cap = new_cap;
    }
    scope->vars[scope->count].name = name;
    scope->vars[scope->count].alloca = alloca;
    scope->vars[scope->count].type = type;
    scope->vars[scope->count].arc_owned = 0;
    scope->vars[scope->count].eh_slot = 0;
    scope->vars[scope->count].arr_len = NULL;
    scope->vars[scope->count].arr_len_slot = NULL;
    scope->vars[scope->count].box_cell = NULL;
    scope->vars[scope->count].box_owned = 0;
    scope->vars[scope->count].obj_rc_flag = NULL;
    scope->count++;
    /* Record the variable for the debugger (no-op unless building with -g). The
     * emit context supplies the compiler state; local_add itself is g-free. */
    di_declare_var(g_di_emit_ctx, name, alloca);
}

static LLVMValueRef emit_entry_alloca(zan_irgen_t *g, LLVMTypeRef ty, const char *name) {
    LLVMBasicBlockRef cur = LLVMGetInsertBlock(g->builder);
    LLVMValueRef fn = LLVMGetBasicBlockParent(cur);
    LLVMBasicBlockRef entry = LLVMGetEntryBasicBlock(fn);
    LLVMValueRef term = LLVMGetBasicBlockTerminator(entry);
    if (term) LLVMPositionBuilderBefore(g->builder, term);
    else LLVMPositionBuilderAtEnd(g->builder, entry);
    LLVMValueRef alloca = LLVMBuildAlloca(g->builder, ty, name);
    LLVMPositionBuilderAtEnd(g->builder, cur);
    return alloca;
}

static local_var_t *local_find(local_scope_t *scope, zan_istr_t name) {
    for (int i = scope->count - 1; i >= 0; i--) {
        if (scope->vars[i].name.len == name.len &&
            memcmp(scope->vars[i].name.str, name.str, (size_t)name.len) == 0) {
            return &scope->vars[i];
        }
    }
    return NULL;
}

/* The built-in generic collections (List/Dict) and StringBuilder share
 * TYPE_CLASS with user classes but are lowered to intrinsic structs, not user
 * classes with a member layout. List and StringBuilder now carry the same
 * 16-byte rc header (allocated via zan_rt_alloc) and participate in ARC like
 * classes; Dict remains header-less (its backing buffers are still not
 * reclaimed) so ARC must continue to exclude it by name → retaining/releasing a
 * header-less struct reads a refcount at obj-16 that lands in unrelated heap
 * memory and corrupts it. */
static int is_builtin_collection_type(zan_type_t *t) {
    if (!t || t->kind != TYPE_CLASS) return 0;
    zan_istr_t n = t->name;
    return (n.len == 4 && memcmp(n.str, "List", 4) == 0) ||
           (n.len == 4 && memcmp(n.str, "Dict", 4) == 0) ||
           (n.len == 13 && memcmp(n.str, "StringBuilder", 13) == 0);
}

/* List, Dict and StringBuilder are refcounted collections: they carry the rc
 * header and are freed (backing buffers + struct) via a per-site collection
 * destructor. Dict used to be header-less, which left ownership of a dict
 * undecidable -- returning one from a method either released its occupants
 * while the caller still held them, or leaked them. */
static int is_rc_collection_type(zan_type_t *t) {
    if (!t || t->kind != TYPE_CLASS) return 0;
    zan_istr_t n = t->name;
    return (n.len == 4 && memcmp(n.str, "List", 4) == 0) ||
           (n.len == 4 && memcmp(n.str, "Dict", 4) == 0) ||
           (n.len == 13 && memcmp(n.str, "StringBuilder", 13) == 0);
}

/* Helper: check if a type is ARC-managed (carries the zan_rt_alloc rc header:
 * user class instances plus the refcounted collections List/StringBuilder; not
 * string/int/struct/enum, and not the header-less Dict). */
static int is_arc_managed_type(zan_type_t *t) {
    if (!t) return 0;
    /* An interface-typed value is a heap class pointer carrying the same rc
     * header; retain/release apply, and release_dyn dispatches on the object's
     * recorded concrete type, so it is ARC-managed exactly like a class ref. */
    if (t->kind == TYPE_INTERFACE) return 1;
    if (t->kind != TYPE_CLASS) return 0;
    if (is_builtin_collection_type(t)) return is_rc_collection_type(t);
    return 1;
}

static int is_rc_managed_type(zan_type_t *t) {
    /* A delegate is rc-managed in the closure shape only; the retain/release
     * helpers test the tag bit, so a bare function pointer costs nothing. */
    return t && (t->kind == TYPE_STRING || t->kind == TYPE_DELEGATE ||
                 is_arc_managed_type(t));
}

static void emit_rc_release_for_type(zan_irgen_t *g, zan_type_t *type, LLVMValueRef v);
static int expr_yields_owned_rc_value(zan_irgen_t *g, zan_ast_node_t *e,
                                      local_scope_t *locals);
static void emit_rc_retain_for_type(zan_irgen_t *g, zan_type_t *type, LLVMValueRef v);
static void emit_arc_release_typed(zan_irgen_t *g, zan_type_t *type, LLVMValueRef v);
static LLVMValueRef get_class_release_decl(zan_irgen_t *g, zan_symbol_t *sym,
                                          zan_type_t *inst);
static void emit_list_release_elems(zan_irgen_t *g, zan_type_t *elem_type, LLVMValueRef col);
static void emit_dict_release_elems(zan_irgen_t *g, zan_type_t *dict_type, LLVMValueRef col);
static void emit_array_release_elems(zan_irgen_t *g, zan_type_t *elem_type,
                                     LLVMValueRef arr, LLVMValueRef len);
static void emit_release_obj_local(zan_irgen_t *g, local_var_t *v);
static LLVMValueRef zan_store_fit(zan_irgen_t *g, LLVMValueRef val, LLVMValueRef ptr);

/* Release all RC-managed local variables in scope (for throw/exception cleanup) */
static void release_all_arc_locals(zan_irgen_t *g, local_scope_t *locals) {
    for (int i = 0; i < locals->count; i++) {
        if (locals->vars[i].obj_rc_flag) {
            emit_release_obj_local(g, &locals->vars[i]);
        } else if (is_rc_managed_type(locals->vars[i].type)) {
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMValueRef val = LLVMBuildLoad2(g->builder, i8ptr,
                locals->vars[i].alloca, "arc_cleanup");
            emit_rc_release_for_type(g, locals->vars[i].type, val);
        }
    }
}

/* Byte size of a scalar-or-scalar-aggregate LLVM type, or 0 when it cannot be
 * computed here (no target data is available during irgen). Used only to decide
 * whether a value struct fits a collection's 8-byte slot. */
static unsigned llvm_scalar_size(LLVMTypeRef t) {
    switch (LLVMGetTypeKind(t)) {
    case LLVMIntegerTypeKind: return (LLVMGetIntTypeWidth(t) + 7) / 8;
    case LLVMPointerTypeKind: return 8;
    case LLVMFloatTypeKind:   return 4;
    case LLVMDoubleTypeKind:  return 8;
    case LLVMStructTypeKind: {
        unsigned total = 0;
        unsigned n = LLVMCountStructElementTypes(t);
        for (unsigned i = 0; i < n; i++) {
            unsigned fs = llvm_scalar_size(LLVMStructGetTypeAtIndex(t, i));
            if (!fs) return 0;
            if (fs > 1 && total % fs) total += fs - (total % fs);  /* natural align */
            total += fs;
        }
        return total;
    }
    default: return 0;
    }
}

/* A List element occupies a whole number of 8-byte words in the data buffer.
 * Everything the language can put in a collection is one word wide except a
 * value struct, which lives inline across ceil(size/8) words; capacity math
 * and every slot address scale by that stride (A15-4). */
static unsigned elem_slot_words(zan_irgen_t *g, zan_type_t *elem) {
    if (!elem || elem->kind != TYPE_STRUCT) return 1;
    LLVMTypeRef st = map_type(g, elem);
    if (!st || LLVMGetTypeKind(st) != LLVMStructTypeKind) return 1;
    unsigned sz = llvm_scalar_size(st);
    if (!sz) return 1;
    return (sz + 7) / 8;
}

/* Scale an element index to the word index of that element's first slot. */
static LLVMValueRef slot_word_index(zan_irgen_t *g, LLVMValueRef idx,
                                    unsigned words) {
    if (words <= 1) return idx;
    return zan_mul(g->builder, idx,
                   LLVMConstInt(LLVMTypeOf(idx), words, 0), "slot.wi");
}

/* A struct with a layout this pass cannot compute has no inline form. */
static void check_struct_fits_slot(zan_irgen_t *g, LLVMTypeRef st, zan_ast_node_t *at) {
    if (llvm_scalar_size(st)) return;
    zan_loc_t loc; memset(&loc, 0, sizeof(loc));
    if (at) loc = at->loc;
    zan_diag_emit(g->diag, DIAG_ERROR, loc,
        "this value struct cannot be a collection element: the slot allocator "
        "cannot compute its layout; use a class instead");
}

/* Operations that treat a slot as one raw word (equality search, swap, the
 * element-by-element copies) have no multi-word form yet: report that instead
 * of quietly reading half an element. */
static int wide_elem_unsupported(zan_irgen_t *g, zan_type_t *elem,
                                 zan_ast_node_t *at, const char *op) {
    if (elem_slot_words(g, elem) <= 1) return 0;
    zan_loc_t loc; memset(&loc, 0, sizeof(loc));
    if (at) loc = at->loc;
    zan_diag_emit(g->diag, DIAG_ERROR, loc,
        "'%s' is not supported yet for a list whose element is a value struct "
        "wider than 8 bytes; Add / [i] / foreach / RemoveAt / Clear are",
        op);
    return 1;
}

/* Store a struct element inline: the slot pointer addresses enough words for
 * the whole value, so it is written through a pointer of the struct's type. */
static void store_struct_in_slot(zan_irgen_t *g, LLVMValueRef v,
                                 LLVMValueRef slot_ptr, zan_ast_node_t *at) {
    LLVMTypeRef st = LLVMTypeOf(v);
    check_struct_fits_slot(g, st, at);
    LLVMValueRef sp = LLVMBuildBitCast(g->builder, slot_ptr,
        LLVMPointerType(st, 0), "slot.sp");
    LLVMBuildStore(g->builder, v, sp);
}

static LLVMValueRef load_struct_from_slot(zan_irgen_t *g, LLVMValueRef slot_ptr,
                                          LLVMTypeRef st) {
    LLVMValueRef sp = LLVMBuildBitCast(g->builder, slot_ptr,
        LLVMPointerType(st, 0), "slot.lp");
    return LLVMBuildLoad2(g->builder, st, sp, "slot.struct");
}

/* Widen a narrow integer element into an i64 collection slot: zero-extend
 * unsigned element types and sign-extend signed ones, so a stored value
 * round-trips with its numeric meaning (a bool stays 1, not the -1 that a bare
 * i1 sign-extension would yield). */
static LLVMValueRef extend_int_for_slot(zan_irgen_t *g, LLVMValueRef v,
                                        zan_type_t *elem_type,
                                        LLVMTypeRef slot_ty) {
    int uns = 0;
    if (elem_type) {
        switch (elem_type->kind) {
        case TYPE_BOOL: case TYPE_BYTE: case TYPE_USHORT:
        case TYPE_UINT: case TYPE_ULONG: case TYPE_CHAR:
            uns = 1; break;
        default: break;
        }
    }
    return uns ? LLVMBuildZExt(g->builder, v, slot_ty, "slot.zx")
               : LLVMBuildSExt(g->builder, v, slot_ty, "slot.sx");
}

/* Store `value` into a collection slot, retaining the new occupant when needed
 * and releasing the old occupant when overwrite_old is true.
 *
 * The slot may be typed (arrays) or i64/raw-pointer-encoded (List/Dict slots).
 * RC-managed values are class instances or strings. */
static void emit_collection_slot_store(zan_irgen_t *g, zan_type_t *elem_type,
                                       LLVMTypeRef slot_ty, LLVMValueRef slot_ptr,
                                       LLVMValueRef value,
                                       zan_ast_node_t *rhs, local_scope_t *locals,
                                       int overwrite_old);
static void emit_collection_release_raw_slot(zan_irgen_t *g, zan_type_t *elem_type,
                                             LLVMValueRef raw, LLVMTypeRef slot_ty);

static LLVMValueRef get_calloc_fn(zan_irgen_t *g) {
    LLVMValueRef f = LLVMGetNamedFunction(g->mod, "calloc");
    if (!f) {
        LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
        LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
        LLVMTypeRef ft = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i64, i64 }, 2, 0);
        f = LLVMAddFunction(g->mod, "calloc", ft);
    }
    return f;
}

/* Allocate a refcounted built-in collection struct through zan_rt_alloc so it
 * carries the 16-byte rc header. Records the collection kind (and, for List and
 * Dict, the element/dict type) at a fresh allocation site; the per-site
 * destructor emitted at finalize releases the occupants and frees the backing
 * buffers before the struct itself. `coll_kind` is 1=List, 2=StringBuilder,
 * 3=Dict.
 * Returns the user pointer (i8*), i.e. the struct base past the header. */
static LLVMValueRef emit_alloc_rc_collection(zan_irgen_t *g, zan_ast_node_t *expr,
                                             long size, int coll_kind,
                                             zan_type_t *elem_type) {
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    /* `new List<T>()` in a specialized body allocates a list of the concrete
     * argument: the site destructor has to release real elements, matching the
     * retain their stores emit. */
    if (g->cur_inst) elem_type = subst_type_param_deep(g, elem_type, g->cur_inst);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    int site_idx = reserve_arc_site(g, NULL, NULL, coll_kind, elem_type);
    LLVMValueRef site_name = LLVMConstNull(i8ptr);
    if (g->check_leaks) {
        char site_buf[600];
        const char *sfile = leak_site_file(g, expr->loc);
        const char *knm = (coll_kind == 2) ? "StringBuilder"
                        : (coll_kind == 3) ? "Dictionary" : "List";
        int elen = (elem_type && elem_type->name.len) ? elem_type->name.len : 1;
        const char *estr = (elem_type && elem_type->name.len) ? elem_type->name.str : "?";
        snprintf(site_buf, sizeof(site_buf), "%s:%u:%u [%s<%.*s>]",
                 sfile, expr->loc.line, expr->loc.col, knm, elen, estr);
        site_name = LLVMBuildGlobalStringPtr(g->builder, site_buf, "site");
    }
    LLVMTypeRef alloc_fn_type = LLVMFunctionType(i8ptr,
        (LLVMTypeRef[]){ i64, i64, i8ptr }, 3, 0);
    LLVMValueRef args[] = { LLVMConstInt(i64, (unsigned long long)size, 0),
                            LLVMConstInt(i64, (unsigned long long)site_idx, 0),
                            site_name };
    return zan_call2(g->builder, alloc_fn_type, g->rt_alloc, args, 3, "coll");
}

/* Unwind-stack entry flavours (see emit_eh_tmp_push_slot): a throw releases
 * what a registered variable slot holds, and the release differs by type --
 * strings carry their own header, a delegate may be a bare function pointer
 * that must not be released at all. */
#define ZAN_EH_SLOT_OBJ 0
#define ZAN_EH_SLOT_STR 1
#define ZAN_EH_SLOT_DLG 2

static int eh_slot_kind_of(zan_type_t *t) {
    if (!t) return ZAN_EH_SLOT_OBJ;
    if (t->kind == TYPE_STRING) return ZAN_EH_SLOT_STR;
    if (t->kind == TYPE_DELEGATE) return ZAN_EH_SLOT_DLG;
    return ZAN_EH_SLOT_OBJ;
}

/* ---- irgen translation-unit parts (order matters) ---------------------
 * The IR generator is split by concern into the files below; they are
 * plain #include'd here so every helper keeps internal (static) linkage
 * inside this single translation unit. Do not add them to CMake.
 */
#include "irgen_expr_core.c"
#include "irgen_arc.c"
#include "irgen_generics.c"
#include "irgen_builtins.c"
#include "irgen_expr.c"
#include "irgen_abi.c"
#include "irgen_call.c"
#include "irgen_async.c"
#include "irgen_stmt.c"
#include "irgen_emit.c"
