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
#include "arena.h"
#include "diag.h"
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
/* Maximum number of distinct `new` allocation sites tracked for per-site
 * leak reporting. Sites beyond this share the last bucket. */
#define ZAN_MAX_LEAK_SITES 4096

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
    if (fn && LLVMIsAFunction(fn) && LLVMGlobalGetValueType(fn) != ty)
        fn = LLVMConstBitCast(fn, LLVMPointerType(ty, 0));
    return LLVMBuildCall2(b, ty, fn, args, n, nm);
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
    g->function_count++;
}

/* Guard a malloc result inside runtime allocator `fn`: if it is null, print
 * "out of memory" and exit(1) rather than returning a header-offset pointer
 * into a null allocation (which would later be dereferenced). */
static void emit_oom_check(zan_irgen_t *g, LLVMValueRef fn, LLVMValueRef raw) {
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMValueRef isnull = LLVMBuildICmp(g->builder, LLVMIntEQ, raw,
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
    LLVMValueRef isbadnz = LLVMBuildICmp(g->builder, LLVMIntNE, bad,
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
    g->arena = arena;
    g->diag = diag;
    g->binder = binder;
    /* Set target before runtime codegen so Sleep/poll selection is correct. */
    if (target_triple && target_triple[0])
        snprintf(g->target_triple, sizeof(g->target_triple), "%s", target_triple);
    g->target_is_windows = target_is_windows;
    /* Must be set before the inline coroutine driver is (conditionally)
     * emitted below, so the multi-worker mode can skip it. */
    g->mt_scheduler = mt_scheduler;

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
        g->site_syms = (zan_symbol_t **)calloc(ZAN_MAX_LEAK_SITES, sizeof(zan_symbol_t *));
        g->site_coll = (int *)calloc(ZAN_MAX_LEAK_SITES, sizeof(int));
        g->site_coll_elem = (zan_type_t **)calloc(ZAN_MAX_LEAK_SITES, sizeof(zan_type_t *));
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

    /* declare zan_rt_print_uint(uint64) — unsigned (%llu) println for ulong */
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

    /* void exit(int) — used by runtime-check panics */
    LLVMTypeRef exit_args[] = { i32 };
    g->exit_type = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), exit_args, 1, 0);
    g->fn_exit = LLVMAddFunction(g->mod, "exit", g->exit_type);

    if (g->check_leaks) {
        /* int atexit(void(*)(void)) — used to schedule the leak report */
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

    /* Dict struct type: { i64 count, i64 capacity, i8** keys, i64* values } */
    LLVMTypeRef dict_fields[] = { i64, i64, LLVMPointerType(i8ptr, 0), LLVMPointerType(i64, 0) };
    g->dict_struct_type = LLVMStructCreateNamed(g->ctx, "Dict");
    LLVMStructSetBody(g->dict_struct_type, dict_fields, 4, 0);

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
     * emitted into the module, so produced programs are self-contained — see
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

        /* Timer list for time-based suspensions (`await Task.Delay(ms)`): a
         * singly-linked list of {next, deadline_ms, frame, step}. Deadlines use
         * a monotonic clock so timers registered at different scheduler turns
         * remain comparable. When the ready queue drains, the reactor is polled
         * with the time remaining until the earliest deadline, allowing socket
         * completions and timers to make progress in the same wait. */
        LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
        LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
        LLVMTypeRef tnode_fields[] = { i8ptr /*next*/, i64t /*deadline_ms*/, i8ptr /*frame*/, g->co_step_ptr /*step*/ };
        LLVMTypeRef tnode_ty = LLVMStructCreateNamed(g->ctx, "zan.co.timer");
        LLVMStructSetBody(tnode_ty, tnode_fields, 4, 0);
        LLVMValueRef g_timers = LLVMAddGlobal(g->mod, i8ptr, "__zan_co_timers");
        LLVMSetInitializer(g_timers, LLVMConstNull(i8ptr));
        LLVMSetLinkage(g_timers, LLVMInternalLinkage);

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

        /* Monotonic millisecond clock used for absolute timer deadlines. */
        LLVMTypeRef now_type = LLVMFunctionType(i64t, NULL, 0, 0);
        LLVMValueRef fn_now = LLVMAddFunction(g->mod, "__zan_co_now_ms", now_type);
        LLVMSetLinkage(fn_now, LLVMInternalLinkage);
        {
            LLVMBasicBlockRef bb = LLVMAppendBasicBlockInContext(g->ctx, fn_now, "entry");
            LLVMPositionBuilderAtEnd(g->builder, bb);
            if (g->target_is_windows) {
                LLVMTypeRef tick_type = LLVMFunctionType(i64t, NULL, 0, 0);
                LLVMValueRef fn_tick = LLVMAddFunction(g->mod, "GetTickCount64", tick_type);
                LLVMValueRef now = zan_call2(g->builder, tick_type, fn_tick, NULL, 0, "now");
                LLVMBuildRet(g->builder, now);
            } else {
                LLVMTypeRef ts_fields[] = { i64t, i64t };
                LLVMTypeRef ts_type = LLVMStructCreateNamed(g->ctx, "zan.timespec");
                LLVMStructSetBody(ts_type, ts_fields, 2, 0);
                LLVMTypeRef clock_args[] = { i32t, LLVMPointerType(ts_type, 0) };
                LLVMTypeRef clock_type = LLVMFunctionType(i32t, clock_args, 2, 0);
                LLVMValueRef fn_clock = LLVMAddFunction(g->mod, "clock_gettime", clock_type);
                int clock_monotonic = 1;
                if (strstr(g->target_triple, "apple") || strstr(g->target_triple, "darwin"))
                    clock_monotonic = 6;
                else if (strstr(g->target_triple, "freebsd"))
                    clock_monotonic = 4;
#if defined(__APPLE__)
                else if (!g->target_triple[0])
                    clock_monotonic = 6;
#elif defined(__FreeBSD__)
                else if (!g->target_triple[0])
                    clock_monotonic = 4;
#endif
                LLVMValueRef ts = LLVMBuildAlloca(g->builder, ts_type, "ts");
                zan_call2(g->builder, clock_type, fn_clock,
                    (LLVMValueRef[]){ LLVMConstInt(i32t, (unsigned)clock_monotonic, 0), ts }, 2, "");
                LLVMValueRef sec = LLVMBuildLoad2(g->builder, i64t,
                    LLVMBuildStructGEP2(g->builder, ts_type, ts, 0, "ts.sec.p"), "ts.sec");
                LLVMValueRef nsec = LLVMBuildLoad2(g->builder, i64t,
                    LLVMBuildStructGEP2(g->builder, ts_type, ts, 1, "ts.nsec.p"), "ts.nsec");
                LLVMValueRef sec_ms = LLVMBuildMul(g->builder, sec,
                    LLVMConstInt(i64t, 1000, 0), "sec.ms");
                LLVMValueRef nsec_ms = LLVMBuildSDiv(g->builder, nsec,
                    LLVMConstInt(i64t, 1000000, 0), "nsec.ms");
                LLVMBuildRet(g->builder, LLVMBuildAdd(g->builder, sec_ms, nsec_ms, "now"));
            }
        }

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
            LLVMValueRef positive = LLVMBuildICmp(g->builder, LLVMIntSGT, timeout,
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

        /* void zan_co_sched_init(void): reset the queue to empty. */
        {
            LLVMBasicBlockRef bb = LLVMAppendBasicBlockInContext(g->ctx, g->rt_co_sched_init, "entry");
            LLVMPositionBuilderAtEnd(g->builder, bb);
            LLVMBuildStore(g->builder, LLVMConstNull(i8ptr), g_head);
            LLVMBuildStore(g->builder, LLVMConstNull(i8ptr), g_tail);
            LLVMBuildStore(g->builder, LLVMConstNull(i8ptr), g_timers);
            LLVMBuildRetVoid(g->builder);
        }

        /* void zan_co_delay(i64 ms, i8* frame, step): register a one-shot timer
         * with an absolute deadline of now+ms, pushed on the front of the timer
         * list. The caller then performs the CPS suspend (state=k, ret void). */
        {
            LLVMBasicBlockRef bb = LLVMAppendBasicBlockInContext(g->ctx, g->rt_co_delay, "entry");
            LLVMPositionBuilderAtEnd(g->builder, bb);
            LLVMValueRef ms    = LLVMGetParam(g->rt_co_delay, 0);
            LLVMValueRef frame = LLVMGetParam(g->rt_co_delay, 1);
            LLVMValueRef step  = LLVMGetParam(g->rt_co_delay, 2);
            LLVMValueRef tn = zan_call2(g->builder, malloc_type, g->fn_malloc,
                (LLVMValueRef[]){ LLVMSizeOf(tnode_ty) }, 1, "tn");
            LLVMValueRef old = LLVMBuildLoad2(g->builder, i8ptr, g_timers, "t.head");
            LLVMBuildStore(g->builder, old, LLVMBuildStructGEP2(g->builder, tnode_ty, tn, 0, "tn.next"));
            LLVMValueRef now = zan_call2(g->builder, now_type, fn_now, NULL, 0, "now");
            LLVMValueRef positive = LLVMBuildICmp(g->builder, LLVMIntSGT, ms,
                LLVMConstInt(i64t, 0, 0), "positive");
            LLVMValueRef delay = LLVMBuildSelect(g->builder, positive, ms,
                LLVMConstInt(i64t, 0, 0), "delay");
            LLVMValueRef deadline = LLVMBuildAdd(g->builder, now, delay, "deadline");
            LLVMBuildStore(g->builder, deadline,
                LLVMBuildStructGEP2(g->builder, tnode_ty, tn, 1, "tn.deadline"));
            LLVMBuildStore(g->builder, frame, LLVMBuildStructGEP2(g->builder, tnode_ty, tn, 2, "tn.frame"));
            LLVMBuildStore(g->builder, step, LLVMBuildStructGEP2(g->builder, tnode_ty, tn, 3, "tn.step"));
            LLVMBuildStore(g->builder, tn, g_timers);
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
            LLVMValueRef step_null = LLVMBuildICmp(g->builder, LLVMIntEQ, step,
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
            LLVMValueRef is_empty = LLVMBuildICmp(g->builder, LLVMIntEQ, tail,
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
            LLVMBasicBlockRef scan_setup = LLVMAppendBasicBlockInContext(g->ctx, g->rt_co_sched_run, "scan.setup");
            LLVMBasicBlockRef scan_cond = LLVMAppendBasicBlockInContext(g->ctx, g->rt_co_sched_run, "scan.cond");
            LLVMBasicBlockRef scan_body = LLVMAppendBasicBlockInContext(g->ctx, g->rt_co_sched_run, "scan.body");
            LLVMBasicBlockRef scan_take = LLVMAppendBasicBlockInContext(g->ctx, g->rt_co_sched_run, "scan.take");
            LLVMBasicBlockRef scan_next = LLVMAppendBasicBlockInContext(g->ctx, g->rt_co_sched_run, "scan.next");
            LLVMBasicBlockRef scan_done = LLVMAppendBasicBlockInContext(g->ctx, g->rt_co_sched_run, "scan.done");
            LLVMBasicBlockRef timer_due = LLVMAppendBasicBlockInContext(g->ctx, g->rt_co_sched_run, "timer.due");
            LLVMBasicBlockRef wait_timer = LLVMAppendBasicBlockInContext(g->ctx, g->rt_co_sched_run, "timer.wait");
            LLVMBasicBlockRef unlink_head = LLVMAppendBasicBlockInContext(g->ctx, g->rt_co_sched_run, "unlink.head");
            LLVMBasicBlockRef unlink_mid = LLVMAppendBasicBlockInContext(g->ctx, g->rt_co_sched_run, "unlink.mid");
            LLVMBasicBlockRef after_unlink = LLVMAppendBasicBlockInContext(g->ctx, g->rt_co_sched_run, "after.unlink");
            LLVMBasicBlockRef io_bb = LLVMAppendBasicBlockInContext(g->ctx, g->rt_co_sched_run, "io.pump");
            LLVMBasicBlockRef exit_bb = LLVMAppendBasicBlockInContext(g->ctx, g->rt_co_sched_run, "exit");
            LLVMPositionBuilderAtEnd(g->builder, entry_bb);
            /* scan state (no opt pass runs, so plain allocas are fine) */
            LLVMValueRef a_best     = LLVMBuildAlloca(g->builder, i8ptr, "a.best");
            LLVMValueRef a_bestprev = LLVMBuildAlloca(g->builder, i8ptr, "a.bestprev");
            LLVMValueRef a_cur      = LLVMBuildAlloca(g->builder, i8ptr, "a.cur");
            LLVMValueRef a_curprev  = LLVMBuildAlloca(g->builder, i8ptr, "a.curprev");
            LLVMBuildBr(g->builder, head_bb);

            LLVMPositionBuilderAtEnd(g->builder, head_bb);
            LLVMValueRef head = LLVMBuildLoad2(g->builder, i8ptr, g_head, "head");
            LLVMValueRef empty = LLVMBuildICmp(g->builder, LLVMIntEQ, head,
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
            LLVMValueRef is_last = LLVMBuildICmp(g->builder, LLVMIntEQ, nx,
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

            /* Ready queue empty: find the earliest timer. It stays linked while
             * the reactor waits, so an IO wake cannot lose the timer. */
            LLVMPositionBuilderAtEnd(g->builder, timers_bb);
            LLVMValueRef t0 = LLVMBuildLoad2(g->builder, i8ptr, g_timers, "t.head");
            LLVMValueRef tnull = LLVMBuildICmp(g->builder, LLVMIntEQ, t0,
                LLVMConstNull(i8ptr), "t.empty");
            LLVMBuildCondBr(g->builder, tnull, io_bb, scan_setup);

            LLVMPositionBuilderAtEnd(g->builder, scan_setup);
            LLVMBuildStore(g->builder, t0, a_best);
            LLVMBuildStore(g->builder, LLVMConstNull(i8ptr), a_bestprev);
            LLVMValueRef t0next = LLVMBuildLoad2(g->builder, i8ptr,
                LLVMBuildStructGEP2(g->builder, tnode_ty, t0, 0, "t0.next"), "t0.nextv");
            LLVMBuildStore(g->builder, t0next, a_cur);
            LLVMBuildStore(g->builder, t0, a_curprev);
            LLVMBuildBr(g->builder, scan_cond);

            LLVMPositionBuilderAtEnd(g->builder, scan_cond);
            LLVMValueRef cur_c = LLVMBuildLoad2(g->builder, i8ptr, a_cur, "cur");
            LLVMValueRef cur_null = LLVMBuildICmp(g->builder, LLVMIntEQ, cur_c,
                LLVMConstNull(i8ptr), "cur.null");
            LLVMBuildCondBr(g->builder, cur_null, scan_done, scan_body);

            LLVMPositionBuilderAtEnd(g->builder, scan_body);
            LLVMValueRef cur_b = LLVMBuildLoad2(g->builder, i8ptr, a_cur, "cur.b");
            LLVMValueRef best_b = LLVMBuildLoad2(g->builder, i8ptr, a_best, "best.b");
            LLVMValueRef curdue = LLVMBuildLoad2(g->builder, i64t,
                LLVMBuildStructGEP2(g->builder, tnode_ty, cur_b, 1, "cur.due"), "curdue");
            LLVMValueRef bestdue = LLVMBuildLoad2(g->builder, i64t,
                LLVMBuildStructGEP2(g->builder, tnode_ty, best_b, 1, "best.due"), "bestdue");
            LLVMValueRef lt = LLVMBuildICmp(g->builder, LLVMIntULT, curdue, bestdue, "due.lt");
            LLVMBuildCondBr(g->builder, lt, scan_take, scan_next);

            LLVMPositionBuilderAtEnd(g->builder, scan_take);
            LLVMValueRef cur_t = LLVMBuildLoad2(g->builder, i8ptr, a_cur, "cur.t");
            LLVMValueRef curprev_t = LLVMBuildLoad2(g->builder, i8ptr, a_curprev, "curprev.t");
            LLVMBuildStore(g->builder, cur_t, a_best);
            LLVMBuildStore(g->builder, curprev_t, a_bestprev);
            LLVMBuildBr(g->builder, scan_next);

            LLVMPositionBuilderAtEnd(g->builder, scan_next);
            LLVMValueRef cur_n = LLVMBuildLoad2(g->builder, i8ptr, a_cur, "cur.n");
            LLVMBuildStore(g->builder, cur_n, a_curprev);
            LLVMValueRef curnext = LLVMBuildLoad2(g->builder, i8ptr,
                LLVMBuildStructGEP2(g->builder, tnode_ty, cur_n, 0, "cur.nextp"), "cur.nextv");
            LLVMBuildStore(g->builder, curnext, a_cur);
            LLVMBuildBr(g->builder, scan_cond);

            LLVMPositionBuilderAtEnd(g->builder, scan_done);
            LLVMValueRef best = LLVMBuildLoad2(g->builder, i8ptr, a_best, "best");
            LLVMValueRef bestprev = LLVMBuildLoad2(g->builder, i8ptr, a_bestprev, "bestprev");
            LLVMValueRef bestdue_done = LLVMBuildLoad2(g->builder, i64t,
                LLVMBuildStructGEP2(g->builder, tnode_ty, best, 1, "best.duep"), "best.due");
            LLVMValueRef now_done = zan_call2(g->builder, now_type, fn_now, NULL, 0, "now");
            LLVMValueRef due_now = LLVMBuildICmp(g->builder, LLVMIntSLE,
                bestdue_done, now_done, "due.now");
            LLVMBuildCondBr(g->builder, due_now, timer_due, wait_timer);

            LLVMPositionBuilderAtEnd(g->builder, wait_timer);
            LLVMValueRef remaining = LLVMBuildSub(g->builder, bestdue_done, now_done, "remaining");
            zan_call2(g->builder, g->rt_io_pump_timeout_type,
                g->rt_io_pump_timeout, (LLVMValueRef[]){ remaining }, 1, "");
            LLVMBuildBr(g->builder, head_bb);

            LLVMPositionBuilderAtEnd(g->builder, timer_due);
            LLVMValueRef bestnext = LLVMBuildLoad2(g->builder, i8ptr,
                LLVMBuildStructGEP2(g->builder, tnode_ty, best, 0, "best.nextp"), "best.nextv");
            LLVMValueRef bp_null = LLVMBuildICmp(g->builder, LLVMIntEQ, bestprev,
                LLVMConstNull(i8ptr), "bp.null");
            LLVMBuildCondBr(g->builder, bp_null, unlink_head, unlink_mid);

            LLVMPositionBuilderAtEnd(g->builder, unlink_head);
            LLVMBuildStore(g->builder, bestnext, g_timers);
            LLVMBuildBr(g->builder, after_unlink);

            LLVMPositionBuilderAtEnd(g->builder, unlink_mid);
            LLVMBuildStore(g->builder, bestnext,
                LLVMBuildStructGEP2(g->builder, tnode_ty, bestprev, 0, "bp.nextp"));
            LLVMBuildBr(g->builder, after_unlink);

            LLVMPositionBuilderAtEnd(g->builder, after_unlink);
            LLVMValueRef bframe = LLVMBuildLoad2(g->builder, i8ptr,
                LLVMBuildStructGEP2(g->builder, tnode_ty, best, 2, "best.framep"), "best.frame");
            LLVMValueRef bstep = LLVMBuildLoad2(g->builder, g->co_step_ptr,
                LLVMBuildStructGEP2(g->builder, tnode_ty, best, 3, "best.stepp"), "best.step");
            zan_call2(g->builder, g->rt_co_ready_type, g->rt_co_ready,
                (LLVMValueRef[]){ bframe, bstep }, 2, "");
            zan_call2(g->builder, free_type, g->fn_free,
                (LLVMValueRef[]){ best }, 1, "");
            LLVMBuildBr(g->builder, head_bb);

            /* No timers: block for IO indefinitely. The weak fallback returns
             * zero immediately, while the reactor returns zero only when there
             * is no pending IO. */
            LLVMPositionBuilderAtEnd(g->builder, io_bb);
            LLVMTypeRef legacy_pump_type = LLVMFunctionType(i32t, NULL, 0, 0);
            LLVMValueRef legacy_pump = LLVMGetNamedFunction(g->mod, "zan_io_pump");
            LLVMValueRef woke = zan_call2(g->builder, legacy_pump_type,
                legacy_pump, NULL, 0, "woke");
            LLVMValueRef more = LLVMBuildICmp(g->builder, LLVMIntSGT, woke,
                LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0), "io.more");
            LLVMBuildCondBr(g->builder, more, head_bb, exit_bb);

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
        i8ptr, g->co_step_ptr, i64
    };
    g->co_header_type = LLVMStructCreateNamed(g->ctx, "zan.co.header");
    LLVMStructSetBody(g->co_header_type, co_hdr_fields, 5, 0);
    g->current_async_frame = NULL;
    g->current_async_frame_type = NULL;
    g->current_async_resume_fn = NULL;
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
        LLVMValueRef is_null = LLVMBuildICmp(g->builder, LLVMIntEQ, obj,
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
        LLVMValueRef is_null = LLVMBuildICmp(g->builder, LLVMIntEQ, obj,
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
        LLVMValueRef rc1 = LLVMBuildSub(g->builder, rc_old, LLVMConstInt(i64, 1, 0), "rc1");
        /* if rc1 == 0, free the object (16-byte header precedes obj) */
        LLVMValueRef is_zero = LLVMBuildICmp(g->builder, LLVMIntEQ, rc1,
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
            LLVMValueRef lv = LLVMBuildLoad2(g->builder, i64, g->g_live, "live");
            LLVMValueRef lv1 = LLVMBuildSub(g->builder, lv, LLVMConstInt(i64, 1, 0), "live_dec");
            LLVMBuildStore(g->builder, lv1, g->g_live);
            LLVMValueRef gidx[2] = { LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0), site };
            LLVMValueRef sc_ptr = LLVMBuildGEP2(g->builder, g->site_live_type, g->g_site_live, gidx, 2, "scptr");
            LLVMValueRef sc = LLVMBuildLoad2(g->builder, i64, sc_ptr, "sc");
            LLVMValueRef sc1 = LLVMBuildSub(g->builder, sc, LLVMConstInt(i64, 1, 0), "sc_dec");
            LLVMBuildStore(g->builder, sc1, sc_ptr);
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
        LLVMValueRef is_null = LLVMBuildICmp(g->builder, LLVMIntEQ, obj, LLVMConstNull(i8ptr), "isnull");
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
        LLVMValueRef inrange = LLVMBuildICmp(g->builder, LLVMIntULT, site,
            LLVMConstInt(i64, ZAN_MAX_LEAK_SITES, 0), "inrange");
        LLVMBuildCondBr(g->builder, inrange, lookup, fb);
        LLVMPositionBuilderAtEnd(g->builder, lookup);
        LLVMValueRef z32 = LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
        LLVMValueRef gidx[2] = { z32, site };
        LLVMValueRef dpp = LLVMBuildGEP2(g->builder, g->site_dtors_type, g->g_site_dtors, gidx, 2, "dpp");
        LLVMValueRef dtor = LLVMBuildLoad2(g->builder, i8ptr, dpp, "dtor");
        LLVMValueRef hasd = LLVMBuildICmp(g->builder, LLVMIntNE, dtor, LLVMConstNull(i8ptr), "hasd");
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
        LLVMValueRef total = LLVMBuildAdd(g->builder, size, LLVMConstInt(i64, 16, 0), "total");
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
            LLVMValueRef lv = LLVMBuildLoad2(g->builder, i64, g->g_live, "live");
            LLVMValueRef lv1 = LLVMBuildAdd(g->builder, lv, LLVMConstInt(i64, 1, 0), "live_inc");
            LLVMBuildStore(g->builder, lv1, g->g_live);
            LLVMValueRef gidx[2] = { z32, site };
            LLVMValueRef sc_ptr = LLVMBuildGEP2(g->builder, g->site_live_type, g->g_site_live, gidx, 2, "scptr");
            LLVMValueRef sc = LLVMBuildLoad2(g->builder, i64, sc_ptr, "sc");
            LLVMValueRef sc1 = LLVMBuildAdd(g->builder, sc, LLVMConstInt(i64, 1, 0), "sc_inc");
            LLVMBuildStore(g->builder, sc1, sc_ptr);
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
        LLVMValueRef total = LLVMBuildAdd(g->builder, size, LLVMConstInt(i64, 16, 0), "total");
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
            LLVMValueRef lv = LLVMBuildLoad2(g->builder, i64, g->g_live, "live");
            LLVMValueRef lv1 = LLVMBuildAdd(g->builder, lv, LLVMConstInt(i64, 1, 0), "live_inc");
            LLVMBuildStore(g->builder, lv1, g->g_live);
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
        LLVMValueRef is_null = LLVMBuildICmp(g->builder, LLVMIntEQ, obj, LLVMConstNull(i8p), "isnull");
        LLVMBasicBlockRef ret_bb = LLVMAppendBasicBlockInContext(g->ctx, g->rt_str_retain, "ret");
        LLVMBasicBlockRef cont_bb = LLVMAppendBasicBlockInContext(g->ctx, g->rt_str_retain, "cont");
        LLVMBuildCondBr(g->builder, is_null, ret_bb, cont_bb);
        LLVMPositionBuilderAtEnd(g->builder, cont_bb);
        LLVMValueRef neg8 = LLVMConstInt(i64t, (uint64_t)-8, 1);
        LLVMValueRef magic_ptr = LLVMBuildGEP2(g->builder, LLVMInt8TypeInContext(g->ctx), obj, &neg8, 1, "magicp");
        emit_header_read_guard(g, g->rt_str_retain, magic_ptr, ret_bb);
        LLVMValueRef magic_iptr = LLVMBuildBitCast(g->builder, magic_ptr, LLVMPointerType(i64t, 0), "magicip");
        LLVMValueRef magic = LLVMBuildLoad2(g->builder, i64t, magic_iptr, "magic");
        LLVMValueRef has_magic = LLVMBuildICmp(g->builder, LLVMIntEQ, magic,
            LLVMConstInt(i64t, ZAN_STRING_MAGIC, 0), "hasmagic");
        LLVMBasicBlockRef retain_bb = LLVMAppendBasicBlockInContext(g->ctx, g->rt_str_retain, "retain");
        LLVMBuildCondBr(g->builder, has_magic, retain_bb, ret_bb);
        LLVMPositionBuilderAtEnd(g->builder, retain_bb);
        LLVMValueRef neg16 = LLVMConstInt(i64t, (uint64_t)-16, 1);
        LLVMValueRef rc_ptr = LLVMBuildGEP2(g->builder, LLVMInt8TypeInContext(g->ctx), obj, &neg16, 1, "rcptr");
        LLVMValueRef rc_iptr = LLVMBuildBitCast(g->builder, rc_ptr, LLVMPointerType(i64t, 0), "rciptr");
        LLVMValueRef rc = LLVMBuildLoad2(g->builder, i64t, rc_iptr, "rc");
        LLVMValueRef is_sent = LLVMBuildICmp(g->builder, LLVMIntEQ, rc,
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
        LLVMValueRef is_null = LLVMBuildICmp(g->builder, LLVMIntEQ, obj, LLVMConstNull(i8p), "isnull");
        LLVMBasicBlockRef ret_bb = LLVMAppendBasicBlockInContext(g->ctx, g->rt_str_release, "ret");
        LLVMBasicBlockRef cont_bb = LLVMAppendBasicBlockInContext(g->ctx, g->rt_str_release, "cont");
        LLVMBuildCondBr(g->builder, is_null, ret_bb, cont_bb);
        LLVMPositionBuilderAtEnd(g->builder, cont_bb);
        LLVMValueRef neg8 = LLVMConstInt(i64t, (uint64_t)-8, 1);
        LLVMValueRef magic_ptr = LLVMBuildGEP2(g->builder, LLVMInt8TypeInContext(g->ctx), obj, &neg8, 1, "magicp");
        emit_header_read_guard(g, g->rt_str_release, magic_ptr, ret_bb);
        LLVMValueRef magic_iptr = LLVMBuildBitCast(g->builder, magic_ptr, LLVMPointerType(i64t, 0), "magicip");
        LLVMValueRef magic = LLVMBuildLoad2(g->builder, i64t, magic_iptr, "magic");
        LLVMValueRef has_magic = LLVMBuildICmp(g->builder, LLVMIntEQ, magic,
            LLVMConstInt(i64t, ZAN_STRING_MAGIC, 0), "hasmagic");
        LLVMBasicBlockRef rel_bb = LLVMAppendBasicBlockInContext(g->ctx, g->rt_str_release, "release");
        LLVMBuildCondBr(g->builder, has_magic, rel_bb, ret_bb);
        LLVMPositionBuilderAtEnd(g->builder, rel_bb);
        LLVMValueRef neg16 = LLVMConstInt(i64t, (uint64_t)-16, 1);
        LLVMValueRef rc_ptr = LLVMBuildGEP2(g->builder, LLVMInt8TypeInContext(g->ctx), obj, &neg16, 1, "rcptr");
        LLVMValueRef rc_iptr = LLVMBuildBitCast(g->builder, rc_ptr, LLVMPointerType(i64t, 0), "rciptr");
        LLVMValueRef rc = LLVMBuildLoad2(g->builder, i64t, rc_iptr, "rc");
        LLVMValueRef is_sent = LLVMBuildICmp(g->builder, LLVMIntEQ, rc,
            LLVMConstInt(i64t, ZAN_STRING_SENTINEL_RC, 0), "issent");
        LLVMBasicBlockRef dec_bb = LLVMAppendBasicBlockInContext(g->ctx, g->rt_str_release, "dec");
        LLVMBuildCondBr(g->builder, is_sent, ret_bb, dec_bb);
        LLVMPositionBuilderAtEnd(g->builder, dec_bb);
        LLVMValueRef rc_old = LLVMBuildAtomicRMW(g->builder, LLVMAtomicRMWBinOpSub, rc_iptr,
            LLVMConstInt(i64t, 1, 0), LLVMAtomicOrderingAcquireRelease, 0);
        LLVMValueRef rc1 = LLVMBuildSub(g->builder, rc_old, LLVMConstInt(i64t, 1, 0), "rc1");
        LLVMValueRef is_zero = LLVMBuildICmp(g->builder, LLVMIntEQ, rc1, LLVMConstInt(i64t, 0, 0), "iszero");
        LLVMBasicBlockRef free_bb = LLVMAppendBasicBlockInContext(g->ctx, g->rt_str_release, "dofree");
        LLVMBuildCondBr(g->builder, is_zero, free_bb, ret_bb);
        LLVMPositionBuilderAtEnd(g->builder, free_bb);
        LLVMValueRef header_ptr = LLVMBuildGEP2(g->builder, LLVMInt8TypeInContext(g->ctx), obj, &neg16, 1, "hdr");
        LLVMTypeRef free_fn_type = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx),
            (LLVMTypeRef[]){ i8p }, 1, 0);
        zan_call2(g->builder, free_fn_type, g->fn_free, &header_ptr, 1, "");
        {
            LLVMValueRef lv = LLVMBuildLoad2(g->builder, i64t, g->g_live, "live");
            LLVMValueRef lv1 = LLVMBuildSub(g->builder, lv, LLVMConstInt(i64t, 1, 0), "live_dec");
            LLVMBuildStore(g->builder, lv1, g->g_live);
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
    free(g->generic_fns);
    g->generic_fns = NULL;
    g->generic_fn_count = g->generic_fn_cap = 0;
    free(g->generic_ctors);
    g->generic_ctors = NULL;
    g->generic_ctor_count = g->generic_ctor_cap = 0;
    free(g->generic_insts);
    g->generic_insts = NULL;
    g->generic_inst_count = g->generic_inst_cap = 0;
}

/* ---- type mapping ---- */

static LLVMTypeRef map_type(zan_irgen_t *g, zan_type_t *type) {
    if (!type) return LLVMVoidTypeInContext(g->ctx);
    switch (type->kind) {
    case TYPE_VOID:   return LLVMVoidTypeInContext(g->ctx);
    case TYPE_BOOL:   return LLVMInt1TypeInContext(g->ctx);
    case TYPE_BYTE:   return LLVMInt8TypeInContext(g->ctx);
    case TYPE_SHORT:  return LLVMInt16TypeInContext(g->ctx);
    case TYPE_INT:    return LLVMInt64TypeInContext(g->ctx);
    case TYPE_LONG:   return LLVMInt64TypeInContext(g->ctx);
    case TYPE_SBYTE:  return LLVMInt64TypeInContext(g->ctx);
    case TYPE_USHORT: return LLVMInt64TypeInContext(g->ctx);
    case TYPE_UINT:   return LLVMInt64TypeInContext(g->ctx);
    case TYPE_ULONG:  return LLVMInt64TypeInContext(g->ctx);
    case TYPE_NINT:   return LLVMInt64TypeInContext(g->ctx);
    case TYPE_FLOAT:  return LLVMFloatTypeInContext(g->ctx);
    case TYPE_DOUBLE: return LLVMDoubleTypeInContext(g->ctx);
    case TYPE_CHAR:   return LLVMInt64TypeInContext(g->ctx);
    case TYPE_STRING: return LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    case TYPE_OBJECT: return LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    case TYPE_ENUM:
        return LLVMInt64TypeInContext(g->ctx);
    case TYPE_DELEGATE: {
        /* delegate types map to function pointer types */
        int pc = type->delegate_param_count;
        LLVMTypeRef *param_types = (LLVMTypeRef *)calloc(
            (size_t)(pc > 0 ? pc : 1), sizeof(LLVMTypeRef));
        for (int i = 0; i < pc; i++) {
            param_types[i] = map_type(g, type->delegate_param_types[i]);
        }
        LLVMTypeRef ret = type->delegate_ret_type
            ? map_type(g, type->delegate_ret_type)
            : LLVMVoidTypeInContext(g->ctx);
        LLVMTypeRef fn_type = LLVMFunctionType(ret, param_types, (unsigned)pc, 0);
        free(param_types);
        return LLVMPointerType(fn_type, 0);
    }
    case TYPE_STRUCT:
    case TYPE_CLASS: {
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

static int get_field_index(zan_symbol_t *type_sym, zan_istr_t field_name) {
    int idx = class_vptr_offset(type_sym);
    for (int i = 0; i < type_sym->member_count; i++) {
        if (type_sym->members[i]->kind == SYM_FIELD ||
            type_sym->members[i]->kind == SYM_PROPERTY) {
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
    for (int i = 0; i < type_sym->member_count; i++) {
        if ((type_sym->members[i]->kind == SYM_FIELD ||
             type_sym->members[i]->kind == SYM_PROPERTY) &&
            type_sym->members[i]->name.len == field_name.len &&
            memcmp(type_sym->members[i]->name.str, field_name.str, field_name.len) == 0) {
            return type_sym->members[i];
        }
    }
    return NULL;
}

static zan_symbol_t *get_method_sym(zan_symbol_t *type_sym, zan_istr_t method_name) {
    /* search in current type first */
    for (int i = 0; i < type_sym->member_count; i++) {
        if (type_sym->members[i]->kind == SYM_METHOD &&
            type_sym->members[i]->name.len == method_name.len &&
            memcmp(type_sym->members[i]->name.str, method_name.str, method_name.len) == 0) {
            return type_sym->members[i];
        }
    }
    /* search in base type (inheritance) */
    if (type_sym->type && type_sym->type->base_type && type_sym->type->base_type->sym) {
        return get_method_sym(type_sym->type->base_type->sym, method_name);
    }
    return NULL;
}

/* Return the symbol that was created for a specific method-declaration AST node.
 * With method overloading, several same-named SYM_METHOD members coexist on a
 * type; each corresponds to exactly one AST_METHOD_DECL. Matching on the decl
 * back-pointer (not the name) is the only way to recover the right one. */
static zan_symbol_t *method_sym_for_decl(zan_symbol_t *type_sym, zan_ast_node_t *decl) {
    for (int i = 0; i < type_sym->member_count; i++) {
        if (type_sym->members[i]->kind == SYM_METHOD &&
            type_sym->members[i]->decl == decl) {
            return type_sym->members[i];
        }
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
    for (int i = 0; i < type_sym->member_count; i++) {
        zan_symbol_t *m = type_sym->members[i];
        if (m->kind != SYM_METHOD) continue;
        if (m->name.len != name.len ||
            memcmp(m->name.str, name.str, name.len) != 0) continue;
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

static void register_struct_type(zan_irgen_t *g, zan_symbol_t *sym) {
    if (g->struct_type_count >= 256) return;
    if (get_struct_llvm_type(g, sym)) return;

    /* Create and register the named struct *before* resolving field types,
     * so that a self- or mutually-referential class field (a pointer to this
     * type) resolves to `%struct.X*` via map_type instead of falling back to
     * i8* — which produced type-mismatched IR (rejected under typed pointers). */
    char name_buf[256];
    snprintf(name_buf, sizeof(name_buf), "struct.%.*s", (int)sym->name.len, sym->name.str);
    LLVMTypeRef st = LLVMStructCreateNamed(g->ctx, name_buf);
    g->struct_types[g->struct_type_count].sym = sym;
    g->struct_types[g->struct_type_count].llvm_type = st;
    g->struct_type_count++;

    /* count fields (including auto-property backing fields) */
    int field_count = 0;
    for (int i = 0; i < sym->member_count; i++) {
        if (sym->members[i]->kind == SYM_FIELD ||
            sym->members[i]->kind == SYM_PROPERTY) field_count++;
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
        if (sym->members[i]->kind == SYM_FIELD ||
            sym->members[i]->kind == SYM_PROPERTY) {
            field_types[fi++] = map_type(g, sym->members[i]->type);
        }
    }

    LLVMStructSetBody(st, field_types, (unsigned)(field_count + vptr), 0);
    free(field_types);
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

static void local_add(local_scope_t *scope, zan_istr_t name, LLVMValueRef alloca, zan_type_t *type) {
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
    scope->vars[scope->count].arr_len = NULL;
    scope->vars[scope->count].arr_len_slot = NULL;
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
 * reclaimed) so ARC must continue to exclude it by name — retaining/releasing a
 * header-less struct reads a refcount at obj-16 that lands in unrelated heap
 * memory and corrupts it. */
static int is_builtin_collection_type(zan_type_t *t) {
    if (!t || t->kind != TYPE_CLASS) return 0;
    zan_istr_t n = t->name;
    return (n.len == 4 && memcmp(n.str, "List", 4) == 0) ||
           (n.len == 4 && memcmp(n.str, "Dict", 4) == 0) ||
           (n.len == 13 && memcmp(n.str, "StringBuilder", 13) == 0);
}

/* List and StringBuilder are refcounted collections: they carry the rc header
 * and are freed (backing buffer + struct) via a per-site collection destructor.
 * Dict is deliberately excluded (still header-less). */
static int is_rc_collection_type(zan_type_t *t) {
    if (!t || t->kind != TYPE_CLASS) return 0;
    zan_istr_t n = t->name;
    return (n.len == 4 && memcmp(n.str, "List", 4) == 0) ||
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
    return t && (t->kind == TYPE_STRING || is_arc_managed_type(t));
}

static void emit_rc_release_for_type(zan_irgen_t *g, zan_type_t *type, LLVMValueRef v);
static int expr_yields_owned_rc_value(zan_irgen_t *g, zan_ast_node_t *e,
                                      local_scope_t *locals);
static void emit_rc_retain_for_type(zan_irgen_t *g, zan_type_t *type, LLVMValueRef v);
static void emit_arc_release_typed(zan_irgen_t *g, zan_type_t *type, LLVMValueRef v);
static LLVMValueRef get_class_release_decl(zan_irgen_t *g, zan_symbol_t *sym);
static void emit_list_release_elems(zan_irgen_t *g, zan_type_t *elem_type, LLVMValueRef col);
static void emit_array_release_elems(zan_irgen_t *g, zan_type_t *elem_type,
                                     LLVMValueRef arr, LLVMValueRef len);

/* Release all RC-managed local variables in scope (for throw/exception cleanup) */
static void release_all_arc_locals(zan_irgen_t *g, local_scope_t *locals) {
    for (int i = 0; i < locals->count; i++) {
        if (is_rc_managed_type(locals->vars[i].type)) {
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMValueRef val = LLVMBuildLoad2(g->builder, i8ptr,
                locals->vars[i].alloca, "arc_cleanup");
            emit_rc_release_for_type(g, locals->vars[i].type, val);
        }
    }
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
                                       int overwrite_old) {
    LLVMValueRef old = NULL;
    if (overwrite_old) {
        old = LLVMBuildLoad2(g->builder, slot_ty, slot_ptr, "arc.old");
    }
    if (!expr_yields_owned_rc_value(g, rhs, locals)) {
        emit_rc_retain_for_type(g, elem_type, value);
    }

    LLVMValueRef stored = value;
    LLVMTypeKind slot_k = LLVMGetTypeKind(slot_ty);
    LLVMTypeKind val_k = LLVMGetTypeKind(LLVMTypeOf(stored));
    if (slot_k == LLVMPointerTypeKind) {
        if (val_k == LLVMIntegerTypeKind) {
            stored = LLVMBuildIntToPtr(g->builder, stored, slot_ty, "slot.ip");
        } else if (val_k == LLVMPointerTypeKind && LLVMTypeOf(stored) != slot_ty) {
            stored = LLVMBuildBitCast(g->builder, stored, slot_ty, "slot.bc");
        }
    } else if (slot_k == LLVMIntegerTypeKind) {
        if (val_k == LLVMPointerTypeKind) {
            stored = LLVMBuildPtrToInt(g->builder, stored, slot_ty, "slot.pi");
        } else if (val_k == LLVMIntegerTypeKind &&
                   LLVMGetIntTypeWidth(LLVMTypeOf(stored)) < 64) {
            stored = LLVMBuildSExt(g->builder, stored, slot_ty, "slot.sx");
        } else if (val_k == LLVMDoubleTypeKind) {
            /* double element packed into an i64 slot: bitcast, not fptoint. */
            stored = LLVMBuildBitCast(g->builder, stored, slot_ty, "slot.fb");
        }
    }
    LLVMBuildStore(g->builder, stored, slot_ptr);
    if (overwrite_old) {
        if (LLVMGetTypeKind(slot_ty) == LLVMPointerTypeKind) {
            emit_rc_release_for_type(g, elem_type, old);
        } else if (LLVMGetTypeKind(slot_ty) == LLVMIntegerTypeKind && is_rc_managed_type(elem_type)) {
            LLVMTypeRef mt = map_type(g, elem_type);
            if (LLVMGetTypeKind(mt) == LLVMPointerTypeKind) {
                LLVMValueRef old_ptr = LLVMBuildIntToPtr(g->builder, old, mt, "slot.old");
                emit_rc_release_for_type(g, elem_type, old_ptr);
            }
        }
    }
}

static void emit_collection_release_raw_slot(zan_irgen_t *g, zan_type_t *elem_type,
                                             LLVMValueRef raw, LLVMTypeRef slot_ty) {
    if (!elem_type || !is_rc_managed_type(elem_type)) return;
    if (LLVMGetTypeKind(slot_ty) == LLVMPointerTypeKind) {
        emit_rc_release_for_type(g, elem_type, raw);
        return;
    }
    if (LLVMGetTypeKind(slot_ty) == LLVMIntegerTypeKind) {
        LLVMTypeRef mt = map_type(g, elem_type);
        if (LLVMGetTypeKind(mt) == LLVMPointerTypeKind) {
            LLVMValueRef ptr = LLVMBuildIntToPtr(g->builder, raw, mt, "slot.old");
            emit_rc_release_for_type(g, elem_type, ptr);
        }
    }
}

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

/* Allocate a refcounted built-in collection (List/StringBuilder) struct through
 * zan_rt_alloc so it carries the 16-byte rc header. Records the collection kind
 * (and, for List, the element type) at a fresh allocation site; the per-site
 * destructor emitted at finalize releases the elements and frees the backing
 * buffer before the struct itself. `coll_kind` is 1=List, 2=StringBuilder.
 * Returns the user pointer (i8*), i.e. the struct base past the header. */
static LLVMValueRef emit_alloc_rc_collection(zan_irgen_t *g, zan_ast_node_t *expr,
                                             long size, int coll_kind,
                                             zan_type_t *elem_type) {
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    int site_idx = g->leak_site_count;
    if (site_idx >= ZAN_MAX_LEAK_SITES) site_idx = ZAN_MAX_LEAK_SITES - 1;
    else g->leak_site_count++;
    if (g->site_coll) g->site_coll[site_idx] = coll_kind;
    if (g->site_coll_elem) g->site_coll_elem[site_idx] = elem_type;
    LLVMValueRef site_name = LLVMConstNull(i8ptr);
    if (g->check_leaks) {
        char site_buf[600];
        const char *sfile = g->src_file ? g->src_file : "<unknown>";
        const char *knm = (coll_kind == 2) ? "StringBuilder" : "List";
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

/* ---- expression codegen ---- */

static LLVMValueRef emit_expr(zan_irgen_t *g, zan_ast_node_t *expr, local_scope_t *locals);
static void emit_stmt(zan_irgen_t *g, zan_ast_node_t *stmt, local_scope_t *locals);
/* Emit a lambda literal, typing its parameters/return from `expected` (the
 * target delegate type) when the lambda omits annotations. Without this the
 * params default to `int`, so a class-typed parameter cannot resolve fields.
 * `expected` may be NULL/non-delegate, in which case i64/int defaults apply. */
static LLVMValueRef emit_lambda_typed(zan_irgen_t *g, zan_ast_node_t *expr,
                                      zan_type_t *expected, local_scope_t *locals);
/* Resolve the declared type of a method's idx-th parameter (NULL if unknown). */
static zan_type_t *method_param_type(zan_irgen_t *g, zan_symbol_t *msym, int idx);
/* Emit a call argument, typing a bare lambda from the target delegate param. */
static LLVMValueRef emit_arg_typed(zan_irgen_t *g, zan_ast_node_t *arg,
                                   zan_type_t *ptype, local_scope_t *locals);

/* async/await CPS helpers (defined below; forward-declared for use in the
 * AST_AWAIT_EXPR case of emit_expr). Frame header field indices are shared
 * across every async frame (see docs/ASYNC_CPS_DESIGN.md). */
enum {
    ASYNC_FRAME_STATE = 0,        /* i32: 0=start, k=resume-after-await-k, -1=done */
    ASYNC_FRAME_DONE = 1,         /* i32: 1 once result slot is valid */
    ASYNC_FRAME_AWAITER = 2,      /* i8*: frame waiting on this one (or null) */
    ASYNC_FRAME_AWAITER_STEP = 3, /* void(i8*)*: awaiter's resume fn (or null) */
    ASYNC_FRAME_RESULT = 4,       /* i64: return value (scalars are i64 here) */
    ASYNC_FRAME_FIRST_PARAM = 5
};
static LLVMValueRef coerce_to_i64(zan_irgen_t *g, LLVMValueRef v);
static void emit_async_save_slots(zan_irgen_t *g);
static void emit_async_reload_slots(zan_irgen_t *g);

/* A "name path" is a chain of identifiers joined by member access, e.g.
 * `Foo.Bar.Widget` — the syntactic form of a namespace-qualified type
 * reference. It contains no calls, indexes, `this`/`base`, etc. */
static bool is_name_path(zan_ast_node_t *node) {
    if (!node) return false;
    if (node->kind == AST_IDENTIFIER) return true;
    if (node->kind == AST_MEMBER_ACCESS) return is_name_path(node->member.object);
    return false;
}

/* Leftmost identifier of a name path (the outermost namespace segment). */
static zan_ast_node_t *name_path_head(zan_ast_node_t *node) {
    while (node && node->kind == AST_MEMBER_ACCESS) node = node->member.object;
    return (node && node->kind == AST_IDENTIFIER) ? node : NULL;
}

/* Resolve the declared type of an `obj.field` member access so that element
 * indexing on struct/class array fields (e.g. `b.data[i]`) can determine the
 * element LLVM type. Returns NULL when the field/type cannot be resolved. */
static zan_type_t *infer_expr_type(zan_irgen_t *g, zan_ast_node_t *e, local_scope_t *locals);

static zan_type_t *member_access_field_type(zan_irgen_t *g, local_scope_t *locals, zan_ast_node_t *member) {
    if (!member || member->kind != AST_MEMBER_ACCESS) return NULL;
    zan_ast_node_t *obj = member->member.object;
    if (obj->kind == AST_IDENTIFIER) {
        local_var_t *l = local_find(locals, obj->ident.name);
        if (l && l->type && l->type->sym) {
            zan_symbol_t *fsym = get_field_sym(l->type->sym, member->member.name);
            if (fsym) return fsym->type;
        }
        /* ClassName.StaticField: obj names a class (not a shadowing local) and
         * member is one of its static fields. */
        if (!l && g && g->binder) {
            zan_symbol_t *cs = zan_binder_lookup(g->binder, obj->ident.name);
            if (cs && (cs->kind == SYM_CLASS || cs->kind == SYM_STRUCT)) {
                zan_symbol_t *fsym = get_field_sym(cs, member->member.name);
                if (fsym) return fsym->type;
            }
        }
        /* not a local: could be an implicit `this` field whose own type is a
         * class, e.g. `field.subfield` inside a method. */
        if (g && g->current_type_sym) {
            zan_symbol_t *ofsym = get_field_sym(g->current_type_sym, obj->ident.name);
            if (ofsym && ofsym->type && ofsym->type->sym) {
                zan_symbol_t *fsym = get_field_sym(ofsym->type->sym, member->member.name);
                if (fsym) return fsym->type;
            }
        }
    }
    /* explicit `this.field` / `base.field` — resolve against the current type. */
    if ((obj->kind == AST_THIS_EXPR || obj->kind == AST_BASE_EXPR) &&
        g && g->current_type_sym) {
        zan_symbol_t *fsym = get_field_sym(g->current_type_sym, member->member.name);
        if (fsym) return fsym->type;
    }
    /* General case: the object is any expression whose static type is a
     * class/struct — e.g. `arr[i].field`, `a.b.field`, `make().field`. Infer
     * the object's type and look the field up on it. This is what lets chained
     * subscripts like `arr[i].values[j]` recover the element type (without it
     * the AST_INDEX codegen falls back to a zero constant). */
    {
        zan_type_t *ot = infer_expr_type(g, obj, locals);
        if (ot && ot->sym) {
            zan_symbol_t *fsym = get_field_sym(ot->sym, member->member.name);
            if (fsym) return fsym->type;
        }
    }
    return NULL;
}

static zan_type_t *infer_expr_type(zan_irgen_t *g, zan_ast_node_t *e, local_scope_t *locals);
static zan_type_t *container_elem_type(zan_type_t *t);
static zan_type_t *generic_method_ret(zan_irgen_t *g, zan_symbol_t *msym,
                                      zan_ast_node_t *call, local_scope_t *locals);

/* Render a type reference's display name (with generic args, [] and ?). */
static int render_type_ref_name(const zan_ast_node_t *t, char *buf, int cap) {
    int n = 0;
    if (t && t->kind == AST_TYPE_REF && cap > 1) {
        int len = (int)t->type_ref.name.len;
        if (len > cap - n - 1) len = cap - n - 1;
        memcpy(buf + n, t->type_ref.name.str, (size_t)len);
        n += len;
        if (t->type_ref.type_args.count > 0) {
            if (n < cap - 1) buf[n++] = '<';
            for (int i = 0; i < t->type_ref.type_args.count; i++) {
                if (i > 0) {
                    if (n < cap - 1) buf[n++] = ',';
                    if (n < cap - 1) buf[n++] = ' ';
                }
                n += render_type_ref_name(t->type_ref.type_args.items[i],
                                          buf + n, cap - n);
            }
            if (n < cap - 1) buf[n++] = '>';
        }
        if (t->type_ref.is_array) {
            if (n < cap - 1) buf[n++] = '[';
            if (n < cap - 1) buf[n++] = ']';
        }
        if (t->type_ref.is_nullable && n < cap - 1) buf[n++] = '?';
    }
    if (cap > 0) buf[n] = 0;
    return n;
}

/* Best-effort static test for whether an expression yields a `string` value.
 * Used to route `+` to concatenation and `==`/`!=` to strcmp rather than raw
 * pointer arithmetic/comparison. Reference (class) values are NOT strings. */
static bool is_string_expr(zan_irgen_t *g, zan_ast_node_t *e, local_scope_t *locals) {
    if (!e || !locals) return false;
    switch (e->kind) {
    case AST_STRING_LITERAL:
    case AST_STRING_INTERP:
    case AST_TYPEOF_EXPR:
        return true;
    case AST_IDENTIFIER: {
        local_var_t *l = local_find(locals, e->ident.name);
        if (l) return l->type && l->type->kind == TYPE_STRING;
        if (g->current_type_sym) {
            zan_symbol_t *fs = get_field_sym(g->current_type_sym, e->ident.name);
            if (fs) return fs->type && fs->type->kind == TYPE_STRING;
        }
        return false;
    }
    case AST_MEMBER_ACCESS: {
        zan_type_t *ft = member_access_field_type(g, locals, e);
        return ft && ft->kind == TYPE_STRING;
    }
    case AST_INDEX: {
        /* Indexing a List<string>/string[] yields a borrowed string element.
         * Recognising it keeps `a + list[i]` from releasing the element (which
         * is still owned by the container). */
        zan_type_t *et = container_elem_type(infer_expr_type(g, e->index.object, locals));
        return et && et->kind == TYPE_STRING;
    }
    case AST_BINARY:
        if (e->binary.op == TK_PLUS)
            return is_string_expr(g, e->binary.left, locals) ||
                   is_string_expr(g, e->binary.right, locals);
        return false;
    case AST_CALL: {
        zan_ast_node_t *callee = e->call.callee;
        if (callee && callee->kind == AST_MEMBER_ACCESS) {
            zan_istr_t m = callee->member.name;
            if ((m.len == 9 && memcmp(m.str, "Substring", 9) == 0) ||
                (m.len == 8 && memcmp(m.str, "ToString", 8) == 0) ||
                (m.len == 4 && memcmp(m.str, "Trim", 4) == 0) ||
                (m.len == 7 && memcmp(m.str, "Replace", 7) == 0) ||
                (m.len == 7 && memcmp(m.str, "ToUpper", 7) == 0) ||
                (m.len == 7 && memcmp(m.str, "ToLower", 7) == 0))
                return true;
        }
        /* user method returning string (bare, instance or static call) */
        {
            zan_type_t *rt = infer_expr_type(g, e, locals);
            return rt && rt->kind == TYPE_STRING;
        }
    }
    default:
        return false;
    }
}

/* Element type of a List<T>/array container type. */
static zan_type_t *container_elem_type(zan_type_t *t) {
    if (!t) return NULL;
    if (t->element_type) return t->element_type;
    if (t->type_args && t->type_arg_count > 0) return t->type_args[0];
    return NULL;
}

static zan_type_t *dict_key_type(zan_irgen_t *g, zan_type_t *t) {
    if (!t || !t->type_args || t->type_arg_count < 1) return g ? g->binder->type_string : NULL;
    return t->type_args[0];
}

static zan_type_t *dict_value_type(zan_type_t *t) {
    if (!t || !t->type_args || t->type_arg_count < 2) return NULL;
    return t->type_args[1];
}

/* Extension method lookup: a static method whose first parameter is declared
 * `this T` extends T; `recv.M(args)` resolves to it when no instance method
 * matches. Matches on method name, arity, and the receiver's static type. */
static zan_symbol_t *find_extension_method(zan_irgen_t *g, zan_type_t *recv_ty,
                                           zan_istr_t name, int argc) {
    if (!recv_ty) return NULL;
    for (int fi = 0; fi < g->function_count; fi++) {
        zan_symbol_t *m = g->functions[fi].sym;
        if (!m || m->kind != SYM_METHOD || !m->decl ||
            m->decl->kind != AST_METHOD_DECL)
            continue;
        if (m->name.len != name.len ||
            memcmp(m->name.str, name.str, (size_t)name.len) != 0)
            continue;
        zan_ast_list_t *ps = &m->decl->method_decl.params;
        if (ps->count != argc + 1) continue;
        zan_ast_node_t *p0 = ps->items[0];
        if (!p0 || p0->kind != AST_PARAM || !p0->param.is_this) continue;
        zan_type_t *pt = zan_binder_resolve_type(g->binder, p0->param.type);
        if (!pt || pt->kind != recv_ty->kind) continue;
        if ((pt->kind == TYPE_CLASS || pt->kind == TYPE_STRUCT ||
             pt->kind == TYPE_INTERFACE || pt->kind == TYPE_ENUM) &&
            pt->sym != recv_ty->sym)
            continue;
        return m;
    }
    return NULL;
}

/* Best-effort static inference of an expression's Zan type, composed over
 * identifiers, `this`, field access and element indexing so that patterns like
 * `list[i].field` or `a.b[i].c` resolve their class/struct symbol. */
static zan_type_t *infer_expr_type(zan_irgen_t *g, zan_ast_node_t *e,
                                   local_scope_t *locals) {
    if (!e) return NULL;
    switch (e->kind) {
    case AST_IDENTIFIER: {
        local_var_t *l = local_find(locals, e->ident.name);
        if (l) return l->type;
        if (g->current_type_sym) {
            zan_symbol_t *fs = get_field_sym(g->current_type_sym, e->ident.name);
            if (fs) return fs->type;
        }
        return NULL;
    }
    case AST_THIS_EXPR:
        return g->current_type_sym ? g->current_type_sym->type : NULL;
    case AST_QUERY_EXPR: {
        /* query yields List<select-type>; the range var is briefly registered
         * (type only) so the projection can be inferred through it */
        zan_type_t *src_ty = infer_expr_type(g, e->query.source, locals);
        zan_type_t *elem = container_elem_type(src_ty);
        if (!elem) elem = g->binder->type_int;
        int mark = locals->count;
        local_add(locals, e->query.var, NULL, elem);
        zan_type_t *sel = infer_expr_type(g, e->query.select, locals);
        locals->count = mark;
        if (!sel) sel = elem;
        return zan_binder_make_list_type(g->binder, sel);
    }
    case AST_MEMBER_ACCESS: {
        /* static field: ClassName.StaticField (class name, not a local). */
        if (e->member.object->kind == AST_IDENTIFIER &&
            !local_find(locals, e->member.object->ident.name)) {
            zan_symbol_t *cs = zan_binder_lookup(g->binder,
                                                 e->member.object->ident.name);
            if (cs && (cs->kind == SYM_CLASS || cs->kind == SYM_STRUCT)) {
                zan_symbol_t *fs = get_field_sym(cs, e->member.name);
                if (fs) return fs->type;
            }
        }
        zan_type_t *ot = infer_expr_type(g, e->member.object, locals);
        if (ot && ot->sym) {
            zan_symbol_t *fs = get_field_sym(ot->sym, e->member.name);
            if (fs) return fs->type;
        }
        return NULL;
    }
    case AST_INDEX:
        return container_elem_type(infer_expr_type(g, e->index.object, locals));
    case AST_CALL: {
        /* Resolve the static return type of a method/function call so that
         * chained member access (e.g. Next().field) can find the struct. */
        zan_ast_node_t *callee = e->call.callee;
        if (!callee) return NULL;
        /* Built-in string instance methods return string but have no symbol to
         * resolve through, so name-match them (mirrors is_string_expr) — lets
         * their owned result be released when passed straight into a call. */
        if (callee->kind == AST_MEMBER_ACCESS) {
            zan_istr_t mm = callee->member.name;
            if ((mm.len == 9 && memcmp(mm.str, "Substring", 9) == 0) ||
                (mm.len == 8 && memcmp(mm.str, "ToString", 8) == 0) ||
                (mm.len == 4 && memcmp(mm.str, "Trim", 4) == 0) ||
                (mm.len == 7 && memcmp(mm.str, "Replace", 7) == 0) ||
                (mm.len == 7 && memcmp(mm.str, "ToUpper", 7) == 0) ||
                (mm.len == 7 && memcmp(mm.str, "ToLower", 7) == 0))
                return g->binder->type_string;
            if (mm.len == 5 && memcmp(mm.str, "Split", 5) == 0) {
                zan_type_t *ot = infer_expr_type(g, callee->member.object, locals);
                if (ot && ot->kind == TYPE_STRING)
                    return zan_binder_make_list_type(g->binder, g->binder->type_string);
            }
        }
        if (callee->kind == AST_IDENTIFIER) {
            /* bare call: current class method, else global function */
            if (g->current_type_sym) {
                zan_symbol_t *m = get_method_sym(g->current_type_sym, callee->ident.name);
                if (m) return m->type;
            }
            zan_symbol_t *gf = zan_binder_lookup(g->binder, callee->ident.name);
            if (gf && gf->kind == SYM_METHOD) return gf->type;
            return NULL;
        }
        if (callee->kind == AST_MEMBER_ACCESS) {
            zan_ast_node_t *obj = callee->member.object;
            /* static: ClassName.Method() (class name, not a shadowing local) */
            if (obj->kind == AST_IDENTIFIER && !local_find(locals, obj->ident.name)) {
                zan_symbol_t *ts = zan_binder_lookup(g->binder, obj->ident.name);
                if (ts && (ts->kind == SYM_CLASS || ts->kind == SYM_STRUCT)) {
                    zan_symbol_t *m = get_method_sym(ts, callee->member.name);
                    if (m) {
                        zan_type_t *gr = generic_method_ret(g, m, e, locals);
                        return gr ? gr : m->type;
                    }
                }
            }
            /* static: Namespace.Path.ClassName.Method() -- the object is a
             * name path, so its rightmost segment is the type name. */
            if (obj->kind == AST_MEMBER_ACCESS && is_name_path(obj)) {
                zan_ast_node_t *head = name_path_head(obj);
                if (head && !local_find(locals, head->ident.name)) {
                    zan_symbol_t *ts = zan_binder_lookup(g->binder, obj->member.name);
                    if (ts && (ts->kind == SYM_CLASS || ts->kind == SYM_STRUCT)) {
                        zan_symbol_t *m = get_method_sym(ts, callee->member.name);
                        if (m) {
                            zan_type_t *gr = generic_method_ret(g, m, e, locals);
                            return gr ? gr : m->type;
                        }
                    }
                }
            }
            /* instance: <expr>.Method() -- resolve the receiver type
             * generally (local, this, field, index, or a nested call) so
             * that fluent chains a.M1().M2().M3() infer at any depth. */
            zan_type_t *rt = infer_expr_type(g, obj, locals);
            if (rt && rt->sym) {
                zan_symbol_t *m = get_method_sym(rt->sym, callee->member.name);
                if (m) return m->type;
            }
            /* extension method: recv.M(args) returns the static method's type */
            zan_symbol_t *xm = find_extension_method(g, rt, callee->member.name,
                                                     e->call.args.count);
            if (xm) return xm->type;
        }
        return NULL;
    }
    case AST_NEW_EXPR:
        /* `new T(...)` yields a T; array `new T[n]` is not a single rc object. */
        if (e->new_expr.is_array) return NULL;
        return zan_binder_resolve_type(g->binder, e->new_expr.type);
    case AST_BINARY:
        /* string concatenation (`a + b`) yields a freshly heap-allocated,
         * owned string; other binary operators produce non-rc scalars. */
        if (e->binary.op == TK_PLUS && is_string_expr(g, e, locals))
            return g->binder->type_string;
        switch (e->binary.op) {
        case TK_PLUS: case TK_MINUS: case TK_STAR: case TK_SLASH:
        case TK_PERCENT: case TK_AMP: case TK_PIPE: case TK_CARET:
        case TK_LESS_LESS: case TK_GREATER_GREATER: {
            zan_type_t *lt = infer_expr_type(g, e->binary.left, locals);
            zan_type_t *rt = infer_expr_type(g, e->binary.right, locals);
            if ((lt && lt->kind == TYPE_ULONG) || (rt && rt->kind == TYPE_ULONG))
                return g->binder->type_ulong;
            return NULL;
        }
        default:
            return NULL;
        }
    case AST_TYPEOF_EXPR:
    case AST_STRING_INTERP:
        return g->binder->type_string;
    case AST_CAST_EXPR:
        return zan_binder_resolve_type(g->binder, e->cast.type);
    default:
        return NULL;
    }
}

/* True when an expression's static type is the unsigned 64-bit `ulong`,
 * which selects unsigned division/remainder/shift/compare and %llu output. */
static bool expr_is_ulong(zan_irgen_t *g, zan_ast_node_t *e, local_scope_t *locals) {
    zan_type_t *t = infer_expr_type(g, e, locals);
    return t && t->kind == TYPE_ULONG;
}

/* Class/struct symbol of an expression's static type, or NULL. */
static zan_symbol_t *expr_class_sym(zan_irgen_t *g, zan_ast_node_t *e,
                                    local_scope_t *locals) {
    zan_type_t *t = infer_expr_type(g, e, locals);
    if (t && (t->kind == TYPE_CLASS || t->kind == TYPE_STRUCT)) return t->sym;
    return NULL;
}

/* True when class `cls` (or one of its base classes) declares `iface` — or an
 * interface that itself extends `iface` — in its implements list. */
static bool class_implements_iface(zan_symbol_t *cls, zan_symbol_t *iface) {
    if (!cls || !cls->type || !iface) return false;
    for (int i = 0; i < cls->type->interface_count; i++) {
        zan_type_t *it = cls->type->interfaces[i];
        if (!it || !it->sym) continue;
        if (it->sym == iface) return true;
        if (it->sym != cls && class_implements_iface(it->sym, iface)) return true;
    }
    if (cls->type->base_type && cls->type->base_type->sym &&
        cls->type->base_type->sym != cls)
        return class_implements_iface(cls->type->base_type->sym, iface);
    return false;
}

static bool is_call_to(zan_ast_node_t *expr, const char *obj, const char *method) {
    if (expr->kind != AST_CALL) return false;
    zan_ast_node_t *callee = expr->call.callee;
    if (callee->kind != AST_MEMBER_ACCESS) return false;
    if (callee->member.object->kind != AST_IDENTIFIER) return false;

    zan_istr_t obj_name = callee->member.object->ident.name;
    zan_istr_t method_name = callee->member.name;

    return ((int)obj_name.len == (int)strlen(obj) &&
            memcmp(obj_name.str, obj, (size_t)obj_name.len) == 0 &&
            (int)method_name.len == (int)strlen(method) &&
            memcmp(method_name.str, method, (size_t)method_name.len) == 0);
}

/* ===== ARC (automatic reference counting) for heap class objects ===========
 * Only TYPE_CLASS instances are heap-allocated with the 16-byte rc header
 * (zan_rt_alloc); struct values live on the stack and List/Dict/StringBuilder
 * are raw-malloc'd without a header, so retain/release apply strictly to class
 * pointers. Ownership model (classifier-based, no temp pool):
 *   - `new C(...)` and calls yield an already-owned (+1) reference;
 *   - identifier / member / index loads yield a borrowed reference.
 * A borrowed reference is retained when captured into an owning class slot
 * (local or field) or returned; owning class locals are released when
 * overwritten and at every function exit. A class local passed as a call
 * argument is conservatively treated as escaped (ownership handed off) and not
 * released, which keeps collections/stores that capture it use-after-free
 * safe. Strings, async frames and collection element release are follow-ups. */

static void emit_arc_retain(zan_irgen_t *g, LLVMValueRef v) {
    if (!v || LLVMGetTypeKind(LLVMTypeOf(v)) != LLVMPointerTypeKind) return;
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    if (LLVMTypeOf(v) != i8ptr) v = LLVMBuildBitCast(g->builder, v, i8ptr, "arc.rt");
    zan_call2(g->builder,
        LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), &i8ptr, 1, 0),
        g->rt_retain, &v, 1, "");
}

static void emit_arc_release(zan_irgen_t *g, LLVMValueRef v) {
    if (!v || LLVMGetTypeKind(LLVMTypeOf(v)) != LLVMPointerTypeKind) return;
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    if (LLVMTypeOf(v) != i8ptr) v = LLVMBuildBitCast(g->builder, v, i8ptr, "arc.rl");
    zan_call2(g->builder,
        LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), &i8ptr, 1, 0),
        g->rt_release, &v, 1, "");
}

static void emit_string_retain(zan_irgen_t *g, LLVMValueRef v) {
    if (!v || LLVMGetTypeKind(LLVMTypeOf(v)) != LLVMPointerTypeKind) return;
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    if (LLVMTypeOf(v) != i8ptr) v = LLVMBuildBitCast(g->builder, v, i8ptr, "str.rt");
    zan_call2(g->builder,
        LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), &i8ptr, 1, 0),
        g->rt_str_retain, &v, 1, "");
}

static void emit_string_release(zan_irgen_t *g, LLVMValueRef v) {
    if (!v || LLVMGetTypeKind(LLVMTypeOf(v)) != LLVMPointerTypeKind) return;
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    if (LLVMTypeOf(v) != i8ptr) v = LLVMBuildBitCast(g->builder, v, i8ptr, "str.rl");
    zan_call2(g->builder,
        LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), &i8ptr, 1, 0),
        g->rt_str_release, &v, 1, "");
}

static void emit_rc_retain_for_type(zan_irgen_t *g, zan_type_t *type, LLVMValueRef v) {
    if (!type) return;
    if (type->kind == TYPE_STRING) {
        emit_string_retain(g, v);
    } else if (is_arc_managed_type(type)) {
        emit_arc_retain(g, v);
    }
}

static void emit_rc_release_for_type(zan_irgen_t *g, zan_type_t *type, LLVMValueRef v) {
    if (!type) return;
    if (type->kind == TYPE_STRING) {
        emit_string_release(g, v);
    } else if (is_arc_managed_type(type)) {
        emit_arc_release_typed(g, type, v);
    }
}

/* ---- object-graph release (per-class destructors) ------------------------
 * User class instances carry a 16-byte rc header; their RC-managed fields
 * (strings, other class instances, and the elements held by a List field) are
 * retained on capture but were never released when the owning object died,
 * leaking the whole object graph. For each class we synthesise
 *   void __zan_release_<T>(i8* obj):
 *     if (obj == null) return;
 *     if (*refcount == 1)          // this release drops the last reference
 *         <release each RC-managed field>;
 *     zan_rt_release(obj);         // decrement + free (+ leak bookkeeping)
 * Peeking the refcount keeps field release aliasing-safe: fields are dropped
 * exactly once, on the release that brings the object to zero. */

static LLVMValueRef get_class_release_decl(zan_irgen_t *g, zan_symbol_t *sym) {
    if (!sym) return NULL;
    for (int i = 0; i < g->class_release_count; i++)
        if (g->class_release[i].sym == sym) return g->class_release[i].fn;
    if (g->class_release_count >= 256) return NULL;
    /* only classes with a registered struct layout can be walked */
    if (!get_struct_llvm_type(g, sym)) return NULL;
    char name[320];
    snprintf(name, sizeof(name), "__zan_release_%.*s_%d",
             (int)sym->name.len, sym->name.str, g->class_release_count);
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef ft = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), &i8ptr, 1, 0);
    LLVMValueRef fn = LLVMAddFunction(g->mod, name, ft);
    LLVMSetLinkage(fn, LLVMInternalLinkage);
    g->class_release[g->class_release_count].sym = sym;
    g->class_release[g->class_release_count].fn = fn;
    g->class_release_count++;
    return fn;
}

/* Release the RC-managed elements held by a List<T> value `col` (an i8* to the
 * bare List struct { i64 count, i64 cap, i64* data }). No-op for null lists or
 * non-RC element types. Only the tracked elements are released; the header-less
 * List struct/buffer are not rc-counted and are left as-is. */
static void emit_list_release_elems(zan_irgen_t *g, zan_type_t *elem_type, LLVMValueRef col) {
    if (!elem_type || !is_rc_managed_type(elem_type)) return;
    if (!col || LLVMGetTypeKind(LLVMTypeOf(col)) != LLVMPointerTypeKind) return;
    LLVMContextRef c = g->ctx;
    LLVMBuilderRef b = g->builder;
    LLVMTypeRef i64 = LLVMInt64TypeInContext(c);
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(c), 0);
    LLVMValueRef fn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(b));
    LLVMBasicBlockRef chk  = LLVMAppendBasicBlockInContext(c, fn, "lc.chk");
    LLVMBasicBlockRef head = LLVMAppendBasicBlockInContext(c, fn, "lc.head");
    LLVMBasicBlockRef body = LLVMAppendBasicBlockInContext(c, fn, "lc.body");
    LLVMBasicBlockRef done = LLVMAppendBasicBlockInContext(c, fn, "lc.done");
    LLVMValueRef c8 = (LLVMTypeOf(col) == i8ptr) ? col
                      : LLVMBuildBitCast(b, col, i8ptr, "lc.c8");
    LLVMValueRef isn = LLVMBuildICmp(b, LLVMIntEQ, c8, LLVMConstNull(i8ptr), "lc.isn");
    LLVMBuildCondBr(b, isn, done, chk);
    LLVMPositionBuilderAtEnd(b, chk);
    LLVMValueRef lp = LLVMBuildBitCast(b, c8, LLVMPointerType(g->list_struct_type, 0), "lp");
    LLVMValueRef cntp = LLVMBuildStructGEP2(b, g->list_struct_type, lp, 0, "cntp");
    LLVMValueRef cnt = LLVMBuildLoad2(b, i64, cntp, "cnt");
    LLVMValueRef datap = LLVMBuildStructGEP2(b, g->list_struct_type, lp, 2, "datap");
    LLVMValueRef data = LLVMBuildLoad2(b, LLVMPointerType(i64, 0), datap, "data");
    LLVMBuildBr(b, head);
    LLVMPositionBuilderAtEnd(b, head);
    LLVMValueRef iphi = LLVMBuildPhi(b, i64, "i");
    LLVMValueRef lt = LLVMBuildICmp(b, LLVMIntSLT, iphi, cnt, "lt");
    LLVMBuildCondBr(b, lt, body, done);
    LLVMPositionBuilderAtEnd(b, body);
    LLVMValueRef slot = LLVMBuildGEP2(b, i64, data, &iphi, 1, "slot");
    LLVMValueRef raw = LLVMBuildLoad2(b, i64, slot, "raw");
    emit_collection_release_raw_slot(g, elem_type, raw, i64);
    LLVMValueRef inext = LLVMBuildAdd(b, iphi, LLVMConstInt(i64, 1, 0), "inext");
    LLVMBasicBlockRef body_end = LLVMGetInsertBlock(b);
    LLVMBuildBr(b, head);
    LLVMValueRef vals[2] = { LLVMConstInt(i64, 0, 0), inext };
    LLVMBasicBlockRef blks[2] = { chk, body_end };
    LLVMAddIncoming(iphi, vals, blks, 2);
    LLVMPositionBuilderAtEnd(b, done);
}

/* Release the rc-managed elements of a bare `new T[n]` array buffer. Arrays
 * carry no length header, so the count is threaded in explicitly (captured at
 * the declaration). Null buffer is a no-op. */
static void emit_array_release_elems(zan_irgen_t *g, zan_type_t *elem_type,
                                     LLVMValueRef arr, LLVMValueRef len) {
    if (!elem_type || !is_rc_managed_type(elem_type)) return;
    if (!arr || LLVMGetTypeKind(LLVMTypeOf(arr)) != LLVMPointerTypeKind) return;
    if (!len) return;
    LLVMContextRef c = g->ctx;
    LLVMBuilderRef b = g->builder;
    LLVMTypeRef i64 = LLVMInt64TypeInContext(c);
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(c), 0);
    LLVMTypeRef elem_llvm = map_type(g, elem_type);
    LLVMValueRef fn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(b));
    LLVMBasicBlockRef chk  = LLVMAppendBasicBlockInContext(c, fn, "ac.chk");
    LLVMBasicBlockRef head = LLVMAppendBasicBlockInContext(c, fn, "ac.head");
    LLVMBasicBlockRef body = LLVMAppendBasicBlockInContext(c, fn, "ac.body");
    LLVMBasicBlockRef done = LLVMAppendBasicBlockInContext(c, fn, "ac.done");
    LLVMValueRef a8 = (LLVMTypeOf(arr) == i8ptr) ? arr
                      : LLVMBuildBitCast(b, arr, i8ptr, "ac.a8");
    LLVMValueRef isn = LLVMBuildICmp(b, LLVMIntEQ, a8, LLVMConstNull(i8ptr), "ac.isn");
    LLVMBuildCondBr(b, isn, done, chk);
    LLVMPositionBuilderAtEnd(b, chk);
    LLVMValueRef typed = LLVMBuildBitCast(b, a8, LLVMPointerType(elem_llvm, 0), "ac.tp");
    LLVMValueRef n = len;
    if (LLVMGetTypeKind(LLVMTypeOf(n)) == LLVMIntegerTypeKind &&
        LLVMGetIntTypeWidth(LLVMTypeOf(n)) < 64)
        n = LLVMBuildSExt(b, n, i64, "ac.n");
    LLVMBuildBr(b, head);
    LLVMPositionBuilderAtEnd(b, head);
    LLVMValueRef iphi = LLVMBuildPhi(b, i64, "i");
    LLVMValueRef lt = LLVMBuildICmp(b, LLVMIntSLT, iphi, n, "ac.lt");
    LLVMBuildCondBr(b, lt, body, done);
    LLVMPositionBuilderAtEnd(b, body);
    LLVMValueRef slot = LLVMBuildGEP2(b, elem_llvm, typed, &iphi, 1, "ac.slot");
    LLVMValueRef elem = LLVMBuildLoad2(b, elem_llvm, slot, "ac.elem");
    emit_rc_release_for_type(g, elem_type, elem);
    LLVMValueRef inext = LLVMBuildAdd(b, iphi, LLVMConstInt(i64, 1, 0), "ac.inext");
    LLVMBasicBlockRef body_end = LLVMGetInsertBlock(b);
    LLVMBuildBr(b, head);
    LLVMValueRef vals[2] = { LLVMConstInt(i64, 0, 0), inext };
    LLVMBasicBlockRef blks[2] = { chk, body_end };
    LLVMAddIncoming(iphi, vals, blks, 2);
    LLVMPositionBuilderAtEnd(b, done);
}

/* Emit the body of __zan_release_<T>: null-guard, peek the refcount, release the
 * RC-managed fields when it is about to hit zero, then hand off to
 * zan_rt_release for the decrement + free. */
static void build_class_release_body(zan_irgen_t *g, zan_symbol_t *sym, LLVMValueRef fn) {
    di_clear(g); /* synthetic fn: don't inherit a user fn's DISubprogram scope */
    LLVMContextRef c = g->ctx;
    LLVMBuilderRef b = g->builder;
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(c), 0);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(c);
    LLVMTypeRef structT = get_struct_llvm_type(g, sym);
    LLVMValueRef obj = LLVMGetParam(fn, 0);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(c, fn, "entry");
    LLVMBasicBlockRef cont  = LLVMAppendBasicBlockInContext(c, fn, "cont");
    LLVMBasicBlockRef relf  = LLVMAppendBasicBlockInContext(c, fn, "relf");
    LLVMBasicBlockRef dorel = LLVMAppendBasicBlockInContext(c, fn, "dorel");
    LLVMBasicBlockRef ret   = LLVMAppendBasicBlockInContext(c, fn, "ret");
    LLVMPositionBuilderAtEnd(b, entry);
    LLVMValueRef isnull = LLVMBuildICmp(b, LLVMIntEQ, obj, LLVMConstNull(i8ptr), "isnull");
    LLVMBuildCondBr(b, isnull, ret, cont);
    LLVMPositionBuilderAtEnd(b, cont);
    LLVMValueRef neg16 = LLVMConstInt(i64, (unsigned long long)-16, 1);
    LLVMValueRef rcp = LLVMBuildGEP2(b, LLVMInt8TypeInContext(c), obj, &neg16, 1, "rcp");
    LLVMValueRef rcip = LLVMBuildBitCast(b, rcp, LLVMPointerType(i64, 0), "rcip");
    LLVMValueRef rc = LLVMBuildLoad2(b, i64, rcip, "rc");
    LLVMValueRef is1 = LLVMBuildICmp(b, LLVMIntEQ, rc, LLVMConstInt(i64, 1, 0), "is1");
    LLVMBuildCondBr(b, is1, relf, dorel);
    LLVMPositionBuilderAtEnd(b, relf);
    LLVMValueRef self = LLVMBuildBitCast(b, obj, LLVMPointerType(structT, 0), "self");
    int fi = class_vptr_offset(sym);
    for (int i = 0; i < sym->member_count; i++) {
        zan_symbol_t *m = sym->members[i];
        if (m->kind != SYM_FIELD && m->kind != SYM_PROPERTY) continue;
        int idx = fi++;
        zan_type_t *ft = m->type;
        if (!ft) continue;
        /* weak fields are non-owning back-references: never released here. */
        if (m->modifiers & MOD_WEAK) continue;
        if (ft->kind == TYPE_STRING) {
            LLVMValueRef fp = LLVMBuildStructGEP2(b, structT, self, (unsigned)idx, "fp");
            LLVMValueRef s = LLVMBuildLoad2(b, i8ptr, fp, "fs");
            emit_string_release(g, s);
        } else if (is_arc_managed_type(ft)) {
            /* User class instances and the refcounted collections List/
             * StringBuilder: release via the recorded site destructor (which
             * releases elements and frees the backing buffer + struct). */
            LLVMValueRef fp = LLVMBuildStructGEP2(b, structT, self, (unsigned)idx, "fp");
            LLVMValueRef cv = LLVMBuildLoad2(b, map_type(g, ft), fp, "fc");
            emit_arc_release_typed(g, ft, cv);
        }
    }
    LLVMBuildBr(b, dorel);
    LLVMPositionBuilderAtEnd(b, dorel);
    zan_call2(b, LLVMFunctionType(LLVMVoidTypeInContext(c), &i8ptr, 1, 0),
                   g->rt_release, &obj, 1, "");
    LLVMBuildBr(b, ret);
    LLVMPositionBuilderAtEnd(b, ret);
    LLVMBuildRetVoid(b);
}

/* Emit the body of a per-site collection destructor (List/StringBuilder):
 * null-guard, peek the refcount, and when this release brings it to zero
 * release the RC-managed elements (List) and free the separately-malloc'd
 * backing buffer, then hand off to zan_rt_release for the struct decrement +
 * free. Peeking the refcount keeps buffer release aliasing-safe. */
static void build_collection_release_body(zan_irgen_t *g, int coll_kind,
                                          zan_type_t *elem_type, LLVMValueRef fn) {
    di_clear(g); /* synthetic fn: don't inherit a user fn's DISubprogram scope */
    LLVMContextRef c = g->ctx;
    LLVMBuilderRef b = g->builder;
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(c), 0);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(c);
    LLVMValueRef obj = LLVMGetParam(fn, 0);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(c, fn, "entry");
    LLVMBasicBlockRef cont  = LLVMAppendBasicBlockInContext(c, fn, "cont");
    LLVMBasicBlockRef relf  = LLVMAppendBasicBlockInContext(c, fn, "relf");
    LLVMBasicBlockRef dorel = LLVMAppendBasicBlockInContext(c, fn, "dorel");
    LLVMBasicBlockRef ret   = LLVMAppendBasicBlockInContext(c, fn, "ret");
    LLVMPositionBuilderAtEnd(b, entry);
    LLVMValueRef isnull = LLVMBuildICmp(b, LLVMIntEQ, obj, LLVMConstNull(i8ptr), "isnull");
    LLVMBuildCondBr(b, isnull, ret, cont);
    LLVMPositionBuilderAtEnd(b, cont);
    LLVMValueRef neg16 = LLVMConstInt(i64, (unsigned long long)-16, 1);
    LLVMValueRef rcp = LLVMBuildGEP2(b, LLVMInt8TypeInContext(c), obj, &neg16, 1, "rcp");
    LLVMValueRef rcip = LLVMBuildBitCast(b, rcp, LLVMPointerType(i64, 0), "rcip");
    LLVMValueRef rc = LLVMBuildLoad2(b, i64, rcip, "rc");
    LLVMValueRef is1 = LLVMBuildICmp(b, LLVMIntEQ, rc, LLVMConstInt(i64, 1, 0), "is1");
    LLVMBuildCondBr(b, is1, relf, dorel);
    LLVMPositionBuilderAtEnd(b, relf);
    LLVMTypeRef free_ty = LLVMFunctionType(LLVMVoidTypeInContext(c), &i8ptr, 1, 0);
    if (coll_kind == 1) {
        /* List: release RC-managed elements, then free the i64* data buffer.
         * emit_list_release_elems may split the block and leaves the builder at
         * its own terminator block; capture the data pointer beforehand. */
        LLVMValueRef lp = LLVMBuildBitCast(b, obj, LLVMPointerType(g->list_struct_type, 0), "lp");
        LLVMValueRef dp = LLVMBuildStructGEP2(b, g->list_struct_type, lp, 2, "dp");
        LLVMValueRef data = LLVMBuildLoad2(b, LLVMPointerType(i64, 0), dp, "data");
        emit_list_release_elems(g, elem_type, obj);
        LLVMValueRef d8 = LLVMBuildBitCast(b, data, i8ptr, "d8");
        zan_call2(b, free_ty, g->fn_free, &d8, 1, "");
    } else if (coll_kind == 2) {
        /* StringBuilder: free the i8* data buffer (free tolerates null). */
        LLVMValueRef sp = LLVMBuildBitCast(b, obj, LLVMPointerType(g->sb_struct_type, 0), "sp");
        LLVMValueRef dp = LLVMBuildStructGEP2(b, g->sb_struct_type, sp, 2, "dp");
        LLVMValueRef data = LLVMBuildLoad2(b, i8ptr, dp, "data");
        zan_call2(b, free_ty, g->fn_free, &data, 1, "");
    }
    LLVMBuildBr(b, dorel);
    LLVMPositionBuilderAtEnd(b, dorel);
    zan_call2(b, LLVMFunctionType(LLVMVoidTypeInContext(c), &i8ptr, 1, 0),
                   g->rt_release, &obj, 1, "");
    LLVMBuildBr(b, ret);
    LLVMPositionBuilderAtEnd(b, ret);
    LLVMBuildRetVoid(b);
}

/* Get (creating on first use) the per-site collection destructor for a List/
 * StringBuilder allocation site, building its body immediately. */
static LLVMValueRef get_collection_release_decl(zan_irgen_t *g, int site) {
    char name[64];
    snprintf(name, sizeof(name), "__zan_release_coll_%d", site);
    LLVMValueRef existing = LLVMGetNamedFunction(g->mod, name);
    if (existing) return existing;
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef ft = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), &i8ptr, 1, 0);
    LLVMValueRef fn = LLVMAddFunction(g->mod, name, ft);
    LLVMSetLinkage(fn, LLVMInternalLinkage);
    build_collection_release_body(g, g->site_coll[site], g->site_coll_elem[site], fn);
    return fn;
}

/* Release a class value `v` of static type `type` via its synthesised
 * __zan_release_<T> (which releases RC fields then frees). Falls back to the
 * plain rc decrement when no per-class function exists (unregistered type). */
static void emit_arc_release_typed(zan_irgen_t *g, zan_type_t *type, LLVMValueRef v) {
    (void)type;
    if (!v || LLVMGetTypeKind(LLVMTypeOf(v)) != LLVMPointerTypeKind) return;
    /* Dispatch on the object's recorded allocation site (its concrete type)
     * rather than the static type, so a base-typed reference still frees the
     * derived instance's fields. zan_rt_release_dyn falls back to a plain
     * decrement when the site has no per-class destructor. */
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    if (LLVMTypeOf(v) != i8ptr) v = LLVMBuildBitCast(g->builder, v, i8ptr, "arc.rlt");
    zan_call2(g->builder, LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), &i8ptr, 1, 0),
                   g->rt_release_dyn, &v, 1, "");
}

/* Build the bodies of every class release function. Called at finalize, once
 * all class types are registered. Iterating the registered struct types covers
 * every function get_class_release_decl may lazily create for nested fields. */
static void emit_all_class_releases(zan_irgen_t *g) {
    for (int i = 0; i < g->struct_type_count; i++) {
        zan_symbol_t *sym = g->struct_types[i].sym;
        if (!sym || !sym->type || !is_arc_managed_type(sym->type)) continue;
        LLVMValueRef fn = get_class_release_decl(g, sym);
        if (fn) build_class_release_body(g, sym, fn);
    }
}

/* Fill __zan_site_dtors[site] with the concrete per-class destructor recorded
 * for that allocation site, so zan_rt_release_dyn can dispatch on runtime
 * type. Must run after emit_all_class_releases (all destructors declared). */
static void emit_site_dtor_table(zan_irgen_t *g) {
    if (!g->site_syms) return;
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    int n = ZAN_MAX_LEAK_SITES;
    LLVMValueRef *elems = (LLVMValueRef *)calloc((size_t)n, sizeof(LLVMValueRef));
    for (int i = 0; i < n; i++) {
        LLVMValueRef e = LLVMConstNull(i8ptr);
        if (i < g->leak_site_count && g->site_coll && g->site_coll[i]) {
            LLVMValueRef fn = get_collection_release_decl(g, i);
            if (fn) e = LLVMConstBitCast(fn, i8ptr);
        } else if (i < g->leak_site_count && g->site_syms[i]) {
            LLVMValueRef fn = get_class_release_decl(g, g->site_syms[i]);
            if (fn) e = LLVMConstBitCast(fn, i8ptr);
        }
        elems[i] = e;
    }
    LLVMSetInitializer(g->g_site_dtors, LLVMConstArray(i8ptr, elems, (unsigned)n));
    free(elems);
}

/* ---- virtual dispatch: vtable globals + dynamic call ---------------------
 * Each class with virtual/override methods gets an internal global
 * __zan_vtable_<Class> : [N x i8*], one slot per virtual method (base-first
 * ordering shared across the hierarchy). Objects store &vtable[0] in field 0
 * at construction; a virtual call loads the slot and calls through it, so the
 * runtime (most-derived) implementation runs even via a base-typed reference
 * or a List<Base> element. */

static LLVMValueRef find_fn_for_sym(zan_irgen_t *g, zan_symbol_t *msym) {
    if (!msym) return NULL;
    for (int i = 0; i < g->function_count; i++)
        if (g->functions[i].sym == msym) return g->functions[i].fn;
    return NULL;
}

/* Enumerate the virtual *slot-defining* methods (MOD_VIRTUAL and not an
 * override) of a class hierarchy, base classes first, matching the slot
 * numbering used by get_virtual_method_index/count_virtual_methods. */
static void collect_vslot_decls(zan_symbol_t *sym, zan_symbol_t **out, int *n, int cap) {
    if (sym->type && sym->type->base_type && sym->type->base_type->sym)
        collect_vslot_decls(sym->type->base_type->sym, out, n, cap);
    for (int i = 0; i < sym->member_count; i++) {
        zan_symbol_t *m = sym->members[i];
        if (m->kind == SYM_METHOD &&
            (m->modifiers & MOD_VIRTUAL) && !(m->modifiers & MOD_OVERRIDE)) {
            if (*n < cap) out[(*n)++] = m;
        }
    }
}

static LLVMValueRef get_vtable_global(zan_irgen_t *g, zan_symbol_t *sym) {
    char name[320];
    snprintf(name, sizeof(name), "__zan_vtable_%.*s",
             (int)sym->name.len, sym->name.str);
    LLVMValueRef gv = LLVMGetNamedGlobal(g->mod, name);
    if (gv) return gv;
    int n = count_virtual_methods(sym);
    if (n < 1) n = 1;
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef at = LLVMArrayType(i8ptr, (unsigned)n);
    gv = LLVMAddGlobal(g->mod, at, name);
    LLVMSetInitializer(gv, LLVMConstNull(at));
    LLVMSetLinkage(gv, LLVMInternalLinkage);
    return gv;
}

/* Populate every instantiable class's vtable with the most-derived function
 * for each slot. Runs at finalize, after all methods are registered. */
static void emit_vtables(zan_irgen_t *g) {
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    for (int i = 0; i < g->struct_type_count; i++) {
        zan_symbol_t *sym = g->struct_types[i].sym;
        if (!sym || !class_has_virtual_methods(sym)) continue;
        int n = count_virtual_methods(sym);
        if (n < 1) continue;
        zan_symbol_t *decls[128];
        int nd = 0;
        collect_vslot_decls(sym, decls, &nd, 128);
        LLVMValueRef *elems = (LLVMValueRef *)calloc((size_t)n, sizeof(LLVMValueRef));
        for (int s = 0; s < n; s++) {
            LLVMValueRef e = LLVMConstNull(i8ptr);
            if (s < nd) {
                zan_symbol_t *decl = decls[s];
                int arity = decl->decl ? decl->decl->method_decl.params.count : 0;
                zan_symbol_t *impl = resolve_overload(sym, decl->name, arity);
                LLVMValueRef fn = find_fn_for_sym(g, impl);
                if (fn) e = LLVMConstBitCast(fn, i8ptr);
            }
            elems[s] = e;
        }
        LLVMValueRef gv = get_vtable_global(g, sym);
        LLVMSetInitializer(gv, LLVMConstArray(i8ptr, elems, (unsigned)n));
        free(elems);
    }
}

/* Reinterpret/convert a value to a target LLVM type across the generic
 * erased-pointer boundary. A generic type parameter T lowers to an opaque
 * pointer, so a value-type argument (i64/double/i1) passed where T is expected
 * — and the reverse, a T-typed field/return consumed as a concrete value —
 * must be bit-reinterpreted (mirrors the List<T> slot store/load). Matching
 * types pass through unchanged, so non-generic code is never affected. */
static LLVMValueRef coerce_int_to(zan_irgen_t *g, LLVMValueRef v, LLVMTypeRef target);

static LLVMValueRef emit_boundary_coerce(zan_irgen_t *g, LLVMValueRef v,
                                         LLVMTypeRef target) {
    if (!v || !target) return v;
    LLVMTypeRef vt = LLVMTypeOf(v);
    if (vt == target) return v;
    LLVMTypeKind vk = LLVMGetTypeKind(vt);
    LLVMTypeKind tk = LLVMGetTypeKind(target);
    LLVMContextRef c = g->ctx;
    LLVMTypeRef i32 = LLVMInt32TypeInContext(c);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(c);

    if (tk == LLVMPointerTypeKind) {
        if (vk == LLVMPointerTypeKind)
            return LLVMBuildBitCast(g->builder, v, target, "bc.pp");
        if (vk == LLVMIntegerTypeKind) {
            if (LLVMGetIntTypeWidth(vt) < 64)
                v = LLVMBuildSExt(g->builder, v, i64, "bc.ext");
            return LLVMBuildIntToPtr(g->builder, v, target, "bc.ip");
        }
        if (vk == LLVMFloatTypeKind) {
            LLVMValueRef iv = LLVMBuildBitCast(g->builder, v, i32, "bc.fi");
            return LLVMBuildIntToPtr(g->builder, iv, target, "bc.ip");
        }
        if (vk == LLVMDoubleTypeKind) {
            LLVMValueRef iv = LLVMBuildBitCast(g->builder, v, i64, "bc.di");
            return LLVMBuildIntToPtr(g->builder, iv, target, "bc.ip");
        }
    } else if (tk == LLVMIntegerTypeKind) {
        if (vk == LLVMPointerTypeKind)
            return LLVMBuildPtrToInt(g->builder, v, target, "bc.pi");
        if (vk == LLVMIntegerTypeKind)
            return coerce_int_to(g, v, target);
    } else if (tk == LLVMFloatTypeKind) {
        if (vk == LLVMPointerTypeKind) {
            LLVMValueRef iv = LLVMBuildPtrToInt(g->builder, v, i32, "bc.pi");
            return LLVMBuildBitCast(g->builder, iv, target, "bc.if");
        }
        if (vk == LLVMDoubleTypeKind)
            return LLVMBuildFPTrunc(g->builder, v, target, "bc.fptr");
    } else if (tk == LLVMDoubleTypeKind) {
        if (vk == LLVMPointerTypeKind) {
            LLVMValueRef iv = LLVMBuildPtrToInt(g->builder, v, i64, "bc.pi");
            return LLVMBuildBitCast(g->builder, iv, target, "bc.id");
        }
        if (vk == LLVMFloatTypeKind)
            return LLVMBuildFPExt(g->builder, v, target, "bc.fpext");
    }
    return v;
}

/* Coerce each argument to the callee's declared parameter type (generic
 * erased-pointer boundary). No-op when types already agree. */
static void coerce_args_to_params(zan_irgen_t *g, LLVMTypeRef fn_type,
                                  LLVMValueRef *call_args, int argc) {
    unsigned npt = LLVMCountParamTypes(fn_type);
    if (npt == 0 || argc <= 0) return;
    LLVMTypeRef *pts = (LLVMTypeRef *)calloc((size_t)npt, sizeof(LLVMTypeRef));
    LLVMGetParamTypes(fn_type, pts);
    int n = (argc < (int)npt) ? argc : (int)npt;
    for (int i = 0; i < n; i++)
        call_args[i] = emit_boundary_coerce(g, call_args[i], pts[i]);
    free(pts);
}

/* If `t` is a generic type parameter of `recv`'s instantiated class, resolve it
 * to the corresponding concrete type argument (e.g. T -> int for Box<int>);
 * otherwise return `t` unchanged. */
static zan_type_t *subst_type_param(zan_type_t *t, zan_type_t *recv) {
    if (!t || t->kind != TYPE_TYPE_PARAM || !recv || !recv->sym) return t;
    zan_ast_node_t *decl = recv->sym->decl;
    if (!decl) return t;
    zan_ast_list_t *tps = &decl->type_decl.type_params;
    for (int i = 0; i < tps->count && i < recv->type_arg_count; i++) {
        zan_ast_node_t *tp = tps->items[i];
        if (tp->kind != AST_IDENTIFIER) continue;
        if (tp->ident.name.len == t->name.len &&
            memcmp(tp->ident.name.str, t->name.str, (size_t)t->name.len) == 0)
            return recv->type_args[i];
    }
    return t;
}

/* For a call to a generic method (one declaring its own <T,...>), determine the
 * concrete return type at this call site when the declared return type is one
 * of those type parameters. Uses explicit type arguments (f<int>(...)) when
 * present, otherwise infers from the argument bound to that type parameter.
 * Returns NULL when the method is non-generic or its return type is not a bare
 * type parameter (e.g. bool / List<T>), in which case no boundary coercion of
 * the erased-pointer result is required. */
static zan_type_t *generic_method_ret(zan_irgen_t *g, zan_symbol_t *msym,
                                      zan_ast_node_t *call, local_scope_t *locals) {
    if (!msym || !msym->decl || msym->decl->kind != AST_METHOD_DECL) return NULL;
    if (!call || call->kind != AST_CALL) return NULL;
    zan_ast_list_t *tps = &msym->decl->method_decl.type_params;
    if (tps->count == 0) return NULL;
    zan_ast_node_t *ret_ref = msym->decl->method_decl.return_type;
    if (!ret_ref || ret_ref->kind != AST_TYPE_REF) return NULL;
    zan_istr_t rn = ret_ref->type_ref.name;
    int which = -1;
    for (int i = 0; i < tps->count; i++) {
        zan_istr_t tn = tps->items[i]->ident.name;
        if (tn.len == rn.len && memcmp(tn.str, rn.str, (size_t)rn.len) == 0) {
            which = i;
            break;
        }
    }
    if (which < 0) return NULL;
    if (call->call.type_args.count > which)
        return zan_binder_resolve_type(g->binder, call->call.type_args.items[which]);
    zan_ast_list_t *params = &msym->decl->method_decl.params;
    for (int j = 0; j < params->count && j < call->call.args.count; j++) {
        zan_ast_node_t *pref = params->items[j]->param.type;
        if (pref && pref->kind == AST_TYPE_REF &&
            pref->type_ref.name.len == rn.len &&
            memcmp(pref->type_ref.name.str, rn.str, (size_t)rn.len) == 0)
            return infer_expr_type(g, call->call.args.items[j], locals);
    }
    return NULL;
}

/* Structural equality of two resolved types, used to match a call site's
 * concrete type arguments against a discovered instantiation. Compares kind +
 * simple name + (recursively) generic type arguments; arrays/nullable compare
 * their element type. Good enough for the closed set of types that appear as
 * generic arguments (builtins, user classes/structs, nested generics). */
static bool types_equal(zan_type_t *a, zan_type_t *b) {
    if (a == b) return true;
    if (!a || !b) return false;
    if (a->kind != b->kind) return false;
    if (a->name.len != b->name.len ||
        (a->name.len && memcmp(a->name.str, b->name.str, (size_t)a->name.len) != 0))
        return false;
    if (a->kind == TYPE_ARRAY || a->kind == TYPE_NULLABLE)
        return types_equal(a->element_type, b->element_type);
    if (a->type_arg_count != b->type_arg_count) return false;
    for (int i = 0; i < a->type_arg_count; i++)
        if (!types_equal(a->type_args[i], b->type_args[i])) return false;
    return true;
}

static bool type_arglists_equal(zan_type_t **a, int an, zan_type_t **b, int bn) {
    if (an != bn) return false;
    for (int i = 0; i < an; i++)
        if (!types_equal(a[i], b[i])) return false;
    return true;
}

/* True when `t` mentions no unresolved generic type parameter (directly or in a
 * type argument / element type) — i.e. it is a fully concrete instantiation. */
static bool type_is_concrete(zan_type_t *t) {
    if (!t) return false;
    if (t->kind == TYPE_TYPE_PARAM || t->kind == TYPE_ERROR) return false;
    if (t->kind == TYPE_ARRAY || t->kind == TYPE_NULLABLE)
        return type_is_concrete(t->element_type);
    for (int i = 0; i < t->type_arg_count; i++)
        if (!type_is_concrete(t->type_args[i])) return false;
    return true;
}

/* Substitute a type parameter to its concrete argument using the instantiation
 * currently being specialized (`g->cur_inst`); no-op when not specializing or
 * when `t` is not one of this instantiation's parameters. */
static zan_type_t *concretize(zan_irgen_t *g, zan_type_t *t) {
    if (!g->cur_inst) return t;
    return subst_type_param(t, g->cur_inst);
}

/* A user generic class/struct is one declaring at least one type parameter and
 * not a built-in intrinsic (List/Dict/StringBuilder handled elsewhere). */
static bool is_user_generic_sym(zan_symbol_t *sym) {
    if (!sym || !sym->decl) return false;
    zan_ast_node_t *d = sym->decl;
    if (d->kind != AST_CLASS_DECL && d->kind != AST_STRUCT_DECL) return false;
    return d->type_decl.type_params.count > 0;
}

static void add_generic_fn(zan_irgen_t *g, zan_symbol_t *msym,
                           zan_type_t **args, int argc,
                           LLVMValueRef fn, LLVMTypeRef fn_type) {
    if (g->generic_fn_count >= g->generic_fn_cap) {
        int ncap = g->generic_fn_cap ? g->generic_fn_cap * 2 : 64;
        g->generic_fns = realloc(g->generic_fns,
                                 (size_t)ncap * sizeof(*g->generic_fns));
        g->generic_fn_cap = ncap;
    }
    g->generic_fns[g->generic_fn_count].msym = msym;
    g->generic_fns[g->generic_fn_count].args = args;
    g->generic_fns[g->generic_fn_count].argc = argc;
    g->generic_fns[g->generic_fn_count].fn = fn;
    g->generic_fns[g->generic_fn_count].fn_type = fn_type;
    g->generic_fn_count++;
}

/* Find the specialized function for `msym` at the instantiation carrying
 * `args`; NULL when none was emitted (caller falls back to the erased fn). */
static LLVMValueRef find_generic_fn(zan_irgen_t *g, zan_symbol_t *msym,
                                    zan_type_t **args, int argc,
                                    LLVMTypeRef *out_fn_type) {
    if (!msym || argc <= 0 || !args) return NULL;
    for (int i = 0; i < g->generic_fn_count; i++) {
        if (g->generic_fns[i].msym == msym &&
            type_arglists_equal(g->generic_fns[i].args, g->generic_fns[i].argc,
                                args, argc)) {
            if (out_fn_type) *out_fn_type = g->generic_fns[i].fn_type;
            return g->generic_fns[i].fn;
        }
    }
    return NULL;
}

static void add_generic_ctor(zan_irgen_t *g, zan_symbol_t *type_sym,
                             zan_type_t **args, int argc, int param_count,
                             LLVMValueRef fn, LLVMTypeRef fn_type) {
    if (g->generic_ctor_count >= g->generic_ctor_cap) {
        int ncap = g->generic_ctor_cap ? g->generic_ctor_cap * 2 : 64;
        g->generic_ctors = realloc(g->generic_ctors,
                                   (size_t)ncap * sizeof(*g->generic_ctors));
        g->generic_ctor_cap = ncap;
    }
    g->generic_ctors[g->generic_ctor_count].type_sym = type_sym;
    g->generic_ctors[g->generic_ctor_count].args = args;
    g->generic_ctors[g->generic_ctor_count].argc = argc;
    g->generic_ctors[g->generic_ctor_count].param_count = param_count;
    g->generic_ctors[g->generic_ctor_count].fn = fn;
    g->generic_ctors[g->generic_ctor_count].fn_type = fn_type;
    g->generic_ctor_count++;
}

static LLVMValueRef find_generic_ctor(zan_irgen_t *g, zan_symbol_t *type_sym,
                                      zan_type_t **args, int argc,
                                      int param_count, LLVMTypeRef *out_fn_type) {
    if (!type_sym || argc <= 0 || !args) return NULL;
    for (int i = 0; i < g->generic_ctor_count; i++) {
        if (g->generic_ctors[i].type_sym == type_sym &&
            g->generic_ctors[i].param_count == param_count &&
            type_arglists_equal(g->generic_ctors[i].args, g->generic_ctors[i].argc,
                                args, argc)) {
            if (out_fn_type) *out_fn_type = g->generic_ctors[i].fn_type;
            return g->generic_ctors[i].fn;
        }
    }
    return NULL;
}

/* Record a distinct concrete instantiation of a user generic class. */
static void add_generic_inst(zan_irgen_t *g, zan_type_t *inst) {
    if (!inst || !inst->sym || inst->type_arg_count <= 0) return;
    if (!is_user_generic_sym(inst->sym)) return;
    if (!type_is_concrete(inst)) return;
    for (int i = 0; i < g->generic_inst_count; i++)
        if (g->generic_insts[i].type_sym == inst->sym &&
            type_arglists_equal(g->generic_insts[i].inst->type_args,
                                g->generic_insts[i].inst->type_arg_count,
                                inst->type_args, inst->type_arg_count))
            return; /* already recorded */
    if (g->generic_inst_count >= g->generic_inst_cap) {
        int ncap = g->generic_inst_cap ? g->generic_inst_cap * 2 : 32;
        g->generic_insts = realloc(g->generic_insts,
                                   (size_t)ncap * sizeof(*g->generic_insts));
        g->generic_inst_cap = ncap;
    }
    g->generic_insts[g->generic_inst_count].type_sym = inst->sym;
    g->generic_insts[g->generic_inst_count].inst = inst;
    g->generic_inst_count++;
}

/* Append a readable, LLVM-symbol-safe token for a concrete type argument
 * (e.g. `string`, `int`, or `List_string` for a nested generic). */
static void mangle_type_token(char *buf, size_t n, size_t *off, zan_type_t *t) {
    if (!t) return;
    if (t->kind == TYPE_ARRAY || t->kind == TYPE_NULLABLE) {
        mangle_type_token(buf, n, off, t->element_type);
        int w = snprintf(buf + (*off < n ? *off : n), (*off < n) ? n - *off : 0, "A");
        if (w > 0) *off += (size_t)w;
        return;
    }
    int w = snprintf(buf + (*off < n ? *off : n), (*off < n) ? n - *off : 0,
                     "%.*s", (int)t->name.len, t->name.str);
    if (w > 0) *off += (size_t)w;
    for (int i = 0; i < t->type_arg_count; i++) {
        w = snprintf(buf + (*off < n ? *off : n), (*off < n) ? n - *off : 0, "_");
        if (w > 0) *off += (size_t)w;
        mangle_type_token(buf, n, off, t->type_args[i]);
    }
}

/* Build the function-name suffix for an instantiation, e.g. HashSet<string>
 * yields "$string" and Pair<int,string> yields "$int$string". */
static void mangle_inst_suffix(char *buf, size_t n, zan_type_t *inst) {
    size_t off = 0;
    buf[0] = '\0';
    for (int i = 0; i < inst->type_arg_count; i++) {
        int w = snprintf(buf + (off < n ? off : n), (off < n) ? n - off : 0, "$");
        if (w > 0) off += (size_t)w;
        mangle_type_token(buf, n, &off, inst->type_args[i]);
    }
    if (off < n) buf[off] = '\0'; else buf[n - 1] = '\0';
}

/* ---- generic instantiation discovery (monomorphization worklist seed) ----
 * Walk the whole compilation unit resolving every declared / constructed type,
 * recording each fully concrete instantiation of a user generic class. Types
 * mentioning a still-erased type parameter (e.g. List<T> inside a generic body)
 * resolve non-concrete and are ignored. */
static void collect_inst_type(zan_irgen_t *g, zan_type_t *t) {
    if (!t) return;
    add_generic_inst(g, t);
    if (t->kind == TYPE_ARRAY || t->kind == TYPE_NULLABLE)
        collect_inst_type(g, t->element_type);
    for (int i = 0; i < t->type_arg_count; i++)
        collect_inst_type(g, t->type_args[i]);
}

static void collect_inst_typeref(zan_irgen_t *g, zan_ast_node_t *tref) {
    if (!tref || tref->kind != AST_TYPE_REF) return;
    collect_inst_type(g, zan_binder_resolve_type(g->binder, tref));
}

static void collect_inst_stmt(zan_irgen_t *g, zan_ast_node_t *st);

static void collect_inst_expr(zan_irgen_t *g, zan_ast_node_t *e) {
    if (!e) return;
    switch (e->kind) {
    case AST_BINARY:
    case AST_ASSIGNMENT:
        collect_inst_expr(g, e->binary.left);
        collect_inst_expr(g, e->binary.right);
        break;
    case AST_UNARY:
    case AST_POSTFIX_UNARY:
        collect_inst_expr(g, e->unary.operand);
        break;
    case AST_AWAIT_EXPR:
        collect_inst_expr(g, e->await_expr.expr);
        break;
    case AST_CALL:
        collect_inst_expr(g, e->call.callee);
        for (int i = 0; i < e->call.args.count; i++)
            collect_inst_expr(g, e->call.args.items[i]);
        break;
    case AST_MEMBER_ACCESS:
        collect_inst_expr(g, e->member.object);
        break;
    case AST_INDEX:
        collect_inst_expr(g, e->index.object);
        collect_inst_expr(g, e->index.index);
        break;
    case AST_CONDITIONAL:
        collect_inst_expr(g, e->conditional.cond);
        collect_inst_expr(g, e->conditional.then_expr);
        collect_inst_expr(g, e->conditional.else_expr);
        break;
    case AST_NEW_EXPR:
        collect_inst_typeref(g, e->new_expr.type);
        for (int i = 0; i < e->new_expr.args.count; i++)
            collect_inst_expr(g, e->new_expr.args.items[i]);
        break;
    case AST_CAST_EXPR:
        collect_inst_typeref(g, e->cast.type);
        collect_inst_expr(g, e->cast.expr);
        break;
    case AST_IS_EXPR:
    case AST_AS_EXPR:
        collect_inst_typeref(g, e->type_test.type);
        collect_inst_expr(g, e->type_test.expr);
        break;
    default:
        break;
    }
}

static void collect_inst_stmt(zan_irgen_t *g, zan_ast_node_t *st) {
    if (!st) return;
    switch (st->kind) {
    case AST_BLOCK:
        for (int i = 0; i < st->block.stmts.count; i++)
            collect_inst_stmt(g, st->block.stmts.items[i]);
        break;
    case AST_VAR_DECL:
        collect_inst_typeref(g, st->var_decl.type);
        collect_inst_expr(g, st->var_decl.initializer);
        break;
    case AST_EXPR_STMT:
        collect_inst_expr(g, st->expr_stmt.expr);
        break;
    case AST_RETURN_STMT:
        collect_inst_expr(g, st->ret.value);
        break;
    case AST_IF_STMT:
        collect_inst_expr(g, st->if_stmt.cond);
        collect_inst_stmt(g, st->if_stmt.then_body);
        collect_inst_stmt(g, st->if_stmt.else_body);
        break;
    case AST_WHILE_STMT:
    case AST_DO_WHILE_STMT:
        collect_inst_expr(g, st->while_stmt.cond);
        collect_inst_stmt(g, st->while_stmt.body);
        break;
    case AST_FOR_STMT:
        collect_inst_stmt(g, st->for_stmt.init);
        collect_inst_expr(g, st->for_stmt.cond);
        collect_inst_expr(g, st->for_stmt.step);
        collect_inst_stmt(g, st->for_stmt.body);
        break;
    case AST_FOREACH_STMT:
        collect_inst_typeref(g, st->foreach_stmt.var_type);
        collect_inst_expr(g, st->foreach_stmt.collection);
        collect_inst_stmt(g, st->foreach_stmt.body);
        break;
    case AST_THROW_STMT:
        collect_inst_expr(g, st->throw_stmt.value);
        break;
    case AST_TRY_STMT:
        collect_inst_stmt(g, st->try_stmt.try_body);
        for (int i = 0; i < st->try_stmt.catches.count; i++)
            collect_inst_stmt(g, st->try_stmt.catches.items[i]->catch_clause.body);
        collect_inst_stmt(g, st->try_stmt.finally_body);
        break;
    case AST_SWITCH_STMT:
        collect_inst_expr(g, st->switch_stmt.expr);
        for (int i = 0; i < st->switch_stmt.cases.count; i++)
            collect_inst_stmt(g, st->switch_stmt.cases.items[i]->switch_case.body);
        break;
    default:
        break;
    }
}

static void collect_inst_member(zan_irgen_t *g, zan_ast_node_t *member) {
    if (!member) return;
    if (member->kind == AST_METHOD_DECL) {
        for (int k = 0; k < member->method_decl.params.count; k++)
            collect_inst_typeref(g, member->method_decl.params.items[k]->param.type);
        collect_inst_typeref(g, member->method_decl.return_type);
        collect_inst_stmt(g, member->method_decl.body);
    } else if (member->kind == AST_CONSTRUCTOR_DECL) {
        for (int k = 0; k < member->method_decl.params.count; k++)
            collect_inst_typeref(g, member->method_decl.params.items[k]->param.type);
        collect_inst_stmt(g, member->method_decl.body);
    } else if (member->kind == AST_FIELD_DECL) {
        collect_inst_typeref(g, member->field_decl.type);
    }
}

/* Repeatedly scan until no new instantiation appears, so a concrete generic
 * used only inside another specialized generic's body (transitive) is found. */
static void discover_generic_insts(zan_irgen_t *g, zan_ast_node_t *unit) {
    if (!unit || unit->kind != AST_COMPILATION_UNIT) return;
    int prev = -1;
    int guard = 0;
    while (g->generic_inst_count != prev && guard++ < 64) {
        prev = g->generic_inst_count;
        for (int i = 0; i < unit->comp_unit.decls.count; i++) {
            zan_ast_node_t *decl = unit->comp_unit.decls.items[i];
            if (decl->kind == AST_CLASS_DECL || decl->kind == AST_STRUCT_DECL ||
                decl->kind == AST_INTERFACE_DECL) {
                for (int j = 0; j < decl->type_decl.members.count; j++)
                    collect_inst_member(g, decl->type_decl.members.items[j]);
            }
        }
    }
}

/* Coerce a call result from the erased opaque pointer back to the concrete
 * type when the callee's declared return type is a generic type parameter of
 * the receiver's instantiated class. No-op otherwise. */
static LLVMValueRef coerce_generic_result(zan_irgen_t *g, LLVMValueRef result,
                                          zan_symbol_t *method_sym,
                                          zan_type_t *recv) {
    if (!result || !method_sym || !recv) return result;
    zan_type_t *rt = subst_type_param(method_sym->type, recv);
    if (rt && rt != method_sym->type)
        return emit_boundary_coerce(g, result, map_type(g, rt));
    return result;
}

/* When the receiver's static type is a concrete instantiation of a user generic
 * class that has a registered specialized method, return the specialized
 * function (and its type via *out_ty); otherwise return the erased function
 * unchanged. Signatures are identical, so this is a pure symbol swap. */
static LLVMValueRef route_generic_method(zan_irgen_t *g, zan_type_t *recv_ty,
                                         zan_symbol_t *method_sym,
                                         LLVMValueRef erased_fn,
                                         LLVMTypeRef erased_ty,
                                         LLVMTypeRef *out_ty) {
    if (out_ty) *out_ty = erased_ty;
    if (!recv_ty || recv_ty->type_arg_count <= 0 || !recv_ty->sym) return erased_fn;
    if (!is_user_generic_sym(recv_ty->sym)) return erased_fn;
    LLVMTypeRef st = NULL;
    LLVMValueRef sfn = find_generic_fn(g, method_sym, recv_ty->type_args,
                                       recv_ty->type_arg_count, &st);
    if (sfn) {
        if (out_ty) *out_ty = st;
        return sfn;
    }
    return erased_fn;
}

/* Emit a call that dispatches through the object's vtable when the target is a
 * virtual/override method invoked on a class instance; otherwise a plain
 * static call. `static_sym` is the receiver's *declared* type. */
static LLVMValueRef emit_dispatch_call(zan_irgen_t *g, zan_symbol_t *static_sym,
        zan_symbol_t *method_sym, LLVMValueRef static_fn, LLVMTypeRef fn_type,
        LLVMValueRef *call_args, int argc, const char *cn) {
    coerce_args_to_params(g, fn_type, call_args, argc);
    if (static_sym && method_sym &&
        (method_sym->modifiers & (MOD_VIRTUAL | MOD_OVERRIDE)) &&
        class_has_virtual_methods(static_sym) && argc >= 1 && call_args[0] &&
        LLVMGetTypeKind(LLVMTypeOf(call_args[0])) == LLVMPointerTypeKind) {
        int slot = get_virtual_method_index(static_sym, method_sym->name);
        LLVMTypeRef st = get_struct_llvm_type(g, static_sym);
        if (slot >= 0 && st) {
            LLVMBuilderRef b = g->builder;
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
            LLVMValueRef thisp = LLVMBuildBitCast(b, call_args[0], LLVMPointerType(st, 0), "vthis");
            LLVMValueRef vpf = LLVMBuildStructGEP2(b, st, thisp, 0, "vpf");
            LLVMValueRef vt8 = LLVMBuildLoad2(b, i8ptr, vpf, "vt8");
            LLVMValueRef vt = LLVMBuildBitCast(b, vt8, LLVMPointerType(i8ptr, 0), "vt");
            LLVMValueRef sidx = LLVMConstInt(i64, (unsigned long long)slot, 0);
            LLVMValueRef sp = LLVMBuildGEP2(b, i8ptr, vt, &sidx, 1, "vsp");
            LLVMValueRef fn8 = LLVMBuildLoad2(b, i8ptr, sp, "vfn8");
            LLVMValueRef fnp = LLVMBuildBitCast(b, fn8, LLVMPointerType(fn_type, 0), "vfnp");
            return zan_call2(b, fn_type, fnp, call_args, (unsigned)argc, cn);
        }
    }
    return zan_call2(g->builder, fn_type, static_fn, call_args, (unsigned)argc, cn);
}

/* A value already carrying an owned (+1) reference we may take over as-is;
 * anything else is a borrowed load that must be retained on capture. */
static int expr_yields_owned_ref(zan_ast_node_t *e) {
    return e && (e->kind == AST_NEW_EXPR || e->kind == AST_CALL ||
                 e->kind == AST_QUERY_EXPR);
}

static int expr_is_arc_object(zan_irgen_t *g, zan_ast_node_t *e, local_scope_t *locals) {
    zan_type_t *t = infer_expr_type(g, e, locals);
    return is_rc_managed_type(t);
}

static int expr_yields_owned_rc_value(zan_irgen_t *g, zan_ast_node_t *e,
                                      local_scope_t *locals) {
    if (!e) return 0;
    if (e->kind == AST_NEW_EXPR || e->kind == AST_CALL ||
        e->kind == AST_QUERY_EXPR) return 1;
    /* An awaited async call yields a freshly owned (+1) reference: the callee's
     * async `return` always retains the result before completing (see the
     * AST_RETURN_STMT async path), so the awaiter receives +1 and must NOT
     * retain it again. Treating it as owned lets the receiving local release it
     * on scope exit -- otherwise every awaited RC result leaks per await. */
    if (e->kind == AST_AWAIT_EXPR) return 1;
    /* An ANF await temp ($awN, generated by anf_hoist_await) holds the owned
     * (+1) result of an awaited async call. It is single-use, so copying it out
     * is a move: the destination takes the +1 and the temp is abandoned. Without
     * treating it as owned, `T x = $awN` retains a second reference and the
     * awaited result leaks. */
    if (e->kind == AST_IDENTIFIER && e->ident.name.len >= 3 &&
        e->ident.name.str[0] == '$' && e->ident.name.str[1] == 'a' &&
        e->ident.name.str[2] == 'w')
        return 1;
    if (e->kind == AST_STRING_INTERP) return 1;
    if (e->kind == AST_BINARY) {
        if (e->binary.op == TK_PLUS && is_string_expr(g, e, locals)) {
            return 1;
        }
        /* A binary op on a user class/struct lowers to a static op_add/op_sub/...
         * method call, which returns an owned (+1) value like any other call.
         * Reporting it as owned prevents the assignment target from retaining a
         * second reference (which would leak, e.g. `event += handler`). */
        const char *opn = NULL;
        switch (e->binary.op) {
        case TK_PLUS:    opn = "op_add"; break;
        case TK_MINUS:   opn = "op_sub"; break;
        case TK_STAR:    opn = "op_mul"; break;
        case TK_SLASH:   opn = "op_div"; break;
        case TK_PERCENT: opn = "op_mod"; break;
        default: break;
        }
        if (opn) {
            zan_type_t *lt = infer_expr_type(g, e->binary.left, locals);
            if (lt && (lt->kind == TYPE_CLASS || lt->kind == TYPE_STRUCT) && lt->sym) {
                zan_istr_t op_istr = { (char *)opn, (int)strlen(opn) };
                if (get_method_sym(lt->sym, op_istr)) {
                    return 1;
                }
            }
        }
    }
    return 0;
}

static int expr_is_local_ident(zan_ast_node_t *e, local_scope_t *locals) {
    return e && e->kind == AST_IDENTIFIER && locals && local_find(locals, e->ident.name);
}

static void emit_release_owned_call_temp(zan_irgen_t *g, zan_ast_node_t *arg,
                                         LLVMValueRef val, local_scope_t *locals) {
    if (!arg || !locals || !val) return;
    if (LLVMGetTypeKind(LLVMTypeOf(val)) != LLVMPointerTypeKind) return;
    if (expr_is_local_ident(arg, locals)) return;
    zan_type_t *t = infer_expr_type(g, arg, locals);
    if (!t || !is_rc_managed_type(t)) return;
    if (!expr_yields_owned_rc_value(g, arg, locals)) return;
    emit_rc_release_for_type(g, t, val);
}

static int call_consumes_free_arg(LLVMValueRef callee) {
    if (!callee) return 0;
    size_t name_len = 0;
    const char *name = LLVMGetValueName2(callee, &name_len);
    return name && name_len == 4 && memcmp(name, "free", 4) == 0;
}

static void emit_invalidate_freed_string(zan_irgen_t *g, zan_ast_node_t *arg,
                                         local_scope_t *locals);

static LLVMValueRef emit_delegate_call(zan_irgen_t *g,
                                       zan_type_t *delegate_type,
                                       LLVMValueRef fn_ptr,
                                       zan_ast_node_t *call,
                                       local_scope_t *locals) {
    int pc = delegate_type->delegate_param_count;
    LLVMTypeRef *param_types = (LLVMTypeRef *)calloc(
        (size_t)(pc > 0 ? pc : 1), sizeof(LLVMTypeRef));
    for (int k = 0; k < pc; k++) {
        param_types[k] = map_type(
            g, delegate_type->delegate_param_types[k]);
    }
    LLVMTypeRef ret = delegate_type->delegate_ret_type
        ? map_type(g, delegate_type->delegate_ret_type)
        : LLVMVoidTypeInContext(g->ctx);
    LLVMTypeRef fn_type = LLVMFunctionType(
        ret, param_types, (unsigned)pc, 0);
    int argc = call->call.args.count;
    LLVMValueRef *call_args = (LLVMValueRef *)calloc(
        (size_t)(argc > 0 ? argc : 1), sizeof(LLVMValueRef));
    for (int k = 0; k < argc; k++) {
        call_args[k] = emit_expr(
            g, call->call.args.items[k], locals);
        if (k < pc) {
            call_args[k] = emit_boundary_coerce(
                g, call_args[k], param_types[k]);
        }
    }
    const char *name =
        LLVMGetTypeKind(ret) == LLVMVoidTypeKind ? "" : "dlgcall";
    LLVMValueRef result = zan_call2(
        g->builder, fn_type, fn_ptr, call_args, (unsigned)argc, name);
    for (int k = 0; k < argc; k++) {
        emit_release_owned_call_temp(
            g, call->call.args.items[k], call_args[k], locals);
    }
    free(call_args);
    free(param_types);
    return result;
}

static void emit_leak_report_support(zan_irgen_t *g) {
    if (!g->check_leaks || g->fn_report_leaks) return;
    /* void __zan_report_leaks(void): at program exit, if any ARC object is
     * still live, print a summary line and then a per-allocation-site
     * breakdown ("file:line:col"). Scheduled via atexit when --check-leaks. */
    LLVMBuilderRef b = LLVMCreateBuilderInContext(g->ctx);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i8p  = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef rl_type = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), NULL, 0, 0);
    g->fn_report_leaks = LLVMAddFunction(g->mod, "__zan_report_leaks", rl_type);
    LLVMBasicBlockRef bb       = LLVMAppendBasicBlockInContext(g->ctx, g->fn_report_leaks, "entry");
    LLVMBasicBlockRef leak_bb  = LLVMAppendBasicBlockInContext(g->ctx, g->fn_report_leaks, "leak");
    LLVMBasicBlockRef head_bb  = LLVMAppendBasicBlockInContext(g->ctx, g->fn_report_leaks, "loop.head");
    LLVMBasicBlockRef body_bb  = LLVMAppendBasicBlockInContext(g->ctx, g->fn_report_leaks, "loop.body");
    LLVMBasicBlockRef print_bb = LLVMAppendBasicBlockInContext(g->ctx, g->fn_report_leaks, "loop.print");
    LLVMBasicBlockRef next_bb  = LLVMAppendBasicBlockInContext(g->ctx, g->fn_report_leaks, "loop.next");
    LLVMBasicBlockRef done_bb  = LLVMAppendBasicBlockInContext(g->ctx, g->fn_report_leaks, "done");

    LLVMPositionBuilderAtEnd(b, bb);
    LLVMValueRef live = LLVMBuildLoad2(b, i64, g->g_live, "live");
    LLVMValueRef leaked = LLVMBuildICmp(b, LLVMIntSGT, live,
        LLVMConstInt(i64, 0, 0), "leaked");
    LLVMBuildCondBr(b, leaked, leak_bb, done_bb);

    LLVMPositionBuilderAtEnd(b, leak_bb);
    LLVMValueRef msg = LLVMBuildGlobalStringPtr(b,
        "zan: memory leak detected: %lld object(s) still reachable at exit\n", "leak_fmt");
    LLVMValueRef pargs[] = { msg, live };
    zan_call2(b, g->printf_type, g->fn_printf, pargs, 2, "");
    LLVMBuildBr(b, head_bb);

    /* iterate the site buckets, printing those with a positive live count */
    LLVMPositionBuilderAtEnd(b, head_bb);
    LLVMValueRef idx = LLVMBuildPhi(b, i64, "i");
    LLVMValueRef in_range = LLVMBuildICmp(b, LLVMIntSLT, idx,
        LLVMConstInt(i64, ZAN_MAX_LEAK_SITES, 0), "inrange");
    LLVMBuildCondBr(b, in_range, body_bb, done_bb);

    LLVMPositionBuilderAtEnd(b, body_bb);
    LLVMValueRef z32 = LLVMConstInt(i32t, 0, 0);
    LLVMValueRef cidx[2] = { z32, idx };
    LLVMValueRef sc_ptr = LLVMBuildGEP2(b, g->site_live_type, g->g_site_live, cidx, 2, "scptr");
    LLVMValueRef sc = LLVMBuildLoad2(b, i64, sc_ptr, "sc");
    LLVMValueRef has = LLVMBuildICmp(b, LLVMIntSGT, sc, LLVMConstInt(i64, 0, 0), "has");
    LLVMBuildCondBr(b, has, print_bb, next_bb);

    LLVMPositionBuilderAtEnd(b, print_bb);
    LLVMValueRef nm_ptr = LLVMBuildGEP2(b, g->site_names_type, g->g_site_names, cidx, 2, "nmptr");
    LLVMValueRef nm = LLVMBuildLoad2(b, i8p, nm_ptr, "nm");
    LLVMValueRef dmsg = LLVMBuildGlobalStringPtr(b,
        "  %lld object(s) leaked, allocated at %s\n", "leak_site_fmt");
    LLVMValueRef dargs[] = { dmsg, sc, nm };
    zan_call2(b, g->printf_type, g->fn_printf, dargs, 3, "");
    LLVMBuildBr(b, next_bb);

    LLVMPositionBuilderAtEnd(b, next_bb);
    LLVMValueRef idx1 = LLVMBuildAdd(b, idx, LLVMConstInt(i64, 1, 0), "i.next");
    LLVMBuildBr(b, head_bb);

    LLVMValueRef phi_vals[2] = { LLVMConstInt(i64, 0, 0), idx1 };
    LLVMBasicBlockRef phi_bbs[2] = { leak_bb, next_bb };
    LLVMAddIncoming(idx, phi_vals, phi_bbs, 2);

    LLVMPositionBuilderAtEnd(b, done_bb);
    LLVMBuildRetVoid(b);
    LLVMDisposeBuilder(b);

    if (!g->fn_atexit) {
        LLVMTypeRef void_fn_type = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), NULL, 0, 0);
        LLVMTypeRef void_fn_ptr = LLVMPointerType(void_fn_type, 0);
        LLVMTypeRef atexit_args[] = { void_fn_ptr };
        g->atexit_type = LLVMFunctionType(LLVMInt32TypeInContext(g->ctx), atexit_args, 1, 0);
        g->fn_atexit = LLVMAddFunction(g->mod, "atexit", g->atexit_type);
    }
}

/* True when a local owns a class heap reference held as a pointer (excludes
 * borrowed params, stack-struct classes and non-class types). */
static int local_owns_arc(local_var_t *v) {
    if (!v || v->arc_owned != 1 || !v->type || !is_rc_managed_type(v->type)) return 0;
    return LLVMGetTypeKind(LLVMGetAllocatedType(v->alloca)) == LLVMPointerTypeKind;
}

static void emit_release_owned_locals(zan_irgen_t *g, local_scope_t *locals) {
    if (!locals) return;
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    for (int i = 0; i < locals->count; i++) {
        if (local_owns_arc(&locals->vars[i])) {
            LLVMValueRef cur = LLVMBuildLoad2(g->builder, i8ptr,
                                              locals->vars[i].alloca, "arc.rel");
            emit_rc_release_for_type(g, locals->vars[i].type, cur);
        } else if (locals->vars[i].arr_len && locals->vars[i].type) {
            LLVMValueRef a = LLVMBuildLoad2(g->builder, i8ptr,
                                            locals->vars[i].alloca, "arr.rel");
            LLVMValueRef nn = LLVMBuildLoad2(g->builder,
                LLVMInt64TypeInContext(g->ctx), locals->vars[i].arr_len, "arr.n");
            emit_array_release_elems(g, locals->vars[i].type->element_type, a, nn);
        }
    }
}

/* Release owned locals in [start, count) without dropping them from the
 * scope: used on `break`/`continue` edges, where the same locals remain in
 * scope on the fall-through path and are released there separately. */
static void emit_release_owned_locals_range(zan_irgen_t *g, local_scope_t *locals, int start) {
    if (!locals) return;
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    for (int i = locals->count - 1; i >= start; i--) {
        if (local_owns_arc(&locals->vars[i])) {
            LLVMValueRef cur = LLVMBuildLoad2(g->builder, i8ptr,
                                              locals->vars[i].alloca, "arc.rel");
            emit_rc_release_for_type(g, locals->vars[i].type, cur);
        } else if (locals->vars[i].arr_len && locals->vars[i].type) {
            LLVMValueRef a = LLVMBuildLoad2(g->builder, i8ptr,
                                            locals->vars[i].alloca, "arr.rel");
            LLVMValueRef nn = LLVMBuildLoad2(g->builder,
                LLVMInt64TypeInContext(g->ctx), locals->vars[i].arr_len, "arr.n");
            emit_array_release_elems(g, locals->vars[i].type->element_type, a, nn);
        }
    }
}

static void emit_release_owned_locals_from(zan_irgen_t *g, local_scope_t *locals, int start) {
    if (!locals || start >= locals->count) return;
    /* If the current block already ends in a terminator (e.g. the scope exited
     * via `return`/`break`/`continue`), emitting release calls here would append
     * dead instructions *after* the terminator — invalid IR ("basic block does
     * not have terminator"). A `return` has already released the owning locals
     * on its own path, so only the bookkeeping (drop ownership tracking, shrink
     * the scope) is needed in that case. */
    bool terminated =
        LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder)) != NULL;
    for (int i = locals->count - 1; i >= start; i--) {
        if (!terminated && local_owns_arc(&locals->vars[i])) {
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMValueRef cur = LLVMBuildLoad2(g->builder, i8ptr,
                                              locals->vars[i].alloca, "arc.rel");
            emit_rc_release_for_type(g, locals->vars[i].type, cur);
        } else if (!terminated && locals->vars[i].arr_len && locals->vars[i].type) {
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMValueRef a = LLVMBuildLoad2(g->builder, i8ptr,
                                            locals->vars[i].alloca, "arr.rel");
            LLVMValueRef nn = LLVMBuildLoad2(g->builder,
                LLVMInt64TypeInContext(g->ctx), locals->vars[i].arr_len, "arr.n");
            emit_array_release_elems(g, locals->vars[i].type->element_type, a, nn);
        }
        locals->vars[i].arr_len = NULL;
        locals->vars[i].arc_owned = 0;
    }
    locals->count = start;
}

/* Capture value `v` (from `rhs`) into an owning reference slot `slot_alloca`:
 * release the previous occupant, retain a borrowed reference, then store. The
 * slot must be null-initialised before its first capture so the release is a
 * no-op. */
static void emit_rc_capture_local(zan_irgen_t *g, zan_type_t *type,
                                  LLVMValueRef slot_alloca, LLVMValueRef v,
                                  zan_ast_node_t *rhs, local_scope_t *locals) {
    LLVMTypeRef slot_ty = LLVMIsAAllocaInst(slot_alloca)
        ? LLVMGetAllocatedType(slot_alloca) : map_type(g, type);
    LLVMValueRef old = LLVMBuildLoad2(g->builder, slot_ty, slot_alloca, "arc.old");
    if (!expr_yields_owned_rc_value(g, rhs, locals)) emit_rc_retain_for_type(g, type, v);
    if (LLVMTypeOf(v) != slot_ty &&
        LLVMGetTypeKind(LLVMTypeOf(v)) == LLVMPointerTypeKind)
        v = LLVMBuildBitCast(g->builder, v, slot_ty, "arc.bc");
    LLVMBuildStore(g->builder, v, slot_alloca);
    emit_rc_release_for_type(g, type, old);
}

/* Retain a borrowed value stored into an owning field, releasing the previous
 * occupant (fields are zero-initialised at object allocation). */
static void emit_rc_store_field(zan_irgen_t *g, zan_type_t *type,
                                LLVMValueRef field_ptr, LLVMValueRef v,
                                zan_ast_node_t *rhs, local_scope_t *locals,
                                int is_weak) {
    LLVMTypeRef vt = LLVMTypeOf(v);
    /* The slot's element type and the value's type may be different pointer
     * representations of the same object (e.g. `null` arrives as i8*, a
     * constructor result as %struct.T*); bitcast so the store verifies under
     * typed-pointer LLVM builds. */
    LLVMTypeRef slot_t = LLVMTypeOf(field_ptr);
    LLVMTypeRef elem_t = vt;
    /* Opaque-pointer LLVM builds (15+) have no pointer element type — and
     * every pointer already inter-stores freely there, so keep vt. */
#if !defined(LLVM_VERSION_MAJOR) || LLVM_VERSION_MAJOR < 15
    if (LLVMGetTypeKind(slot_t) == LLVMPointerTypeKind)
        elem_t = LLVMGetElementType(slot_t);
#elif LLVM_VERSION_MAJOR < 17
    if (LLVMGetTypeKind(slot_t) == LLVMPointerTypeKind &&
        !LLVMPointerTypeIsOpaque(slot_t))
        elem_t = LLVMGetElementType(slot_t);
#else
    (void)slot_t;
#endif
    if (LLVMGetTypeKind(vt) == LLVMPointerTypeKind &&
        LLVMGetTypeKind(elem_t) == LLVMPointerTypeKind && vt != elem_t) {
        v = LLVMBuildBitCast(g->builder, v, elem_t, "fld.cast");
        vt = elem_t;
    }
    /* weak field: non-owning store, no retain of new / release of old, so a
     * parent<->child back-reference does not form an ARC-uncollectable cycle. */
    if (is_weak || LLVMGetTypeKind(vt) != LLVMPointerTypeKind) {
        LLVMBuildStore(g->builder, v, field_ptr);
        return;
    }
    LLVMValueRef old = LLVMBuildLoad2(g->builder, elem_t, field_ptr, "arc.fold");
    if (!expr_yields_owned_rc_value(g, rhs, locals)) emit_rc_retain_for_type(g, type, v);
    LLVMBuildStore(g->builder, v, field_ptr);
    emit_rc_release_for_type(g, type, old);
}

/* Emit: when `cond` (an i1) is true at runtime, print `msg` and exit(1), then
 * continue in a fresh block. Shared by fopen and every other I/O return-value
 * guard so a failed syscall never falls through with an invalid result. */
static void emit_io_abort_if(zan_irgen_t *g, LLVMValueRef cond, const char *msg) {
    if (!g->current_fn) return;
    LLVMBasicBlockRef fail_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "io.fail");
    LLVMBasicBlockRef cont_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "io.cont");
    LLVMBuildCondBr(g->builder, cond, fail_bb, cont_bb);
    LLVMPositionBuilderAtEnd(g->builder, fail_bb);
    LLVMValueRef text = LLVMBuildGlobalStringPtr(g->builder, msg, "ioerr");
    LLVMValueRef fmt = LLVMBuildGlobalStringPtr(g->builder, "%s", "ioerr_fmt");
    LLVMValueRef pargs[] = { fmt, text };
    zan_call2(g->builder, g->printf_type, g->fn_printf, pargs, 2, "");
    LLVMValueRef code = LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 1, 0);
    zan_call2(g->builder, g->exit_type, g->fn_exit, &code, 1, "");
    LLVMBuildUnreachable(g->builder);
    LLVMPositionBuilderAtEnd(g->builder, cont_bb);
}

static void emit_fopen_check(zan_irgen_t *g, LLVMValueRef fp, const char *msg) {
    if (!g->current_fn) return;
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMValueRef isnull = LLVMBuildICmp(g->builder, LLVMIntEQ, fp,
        LLVMConstPointerNull(i8ptr), "fp.isnull");
    emit_io_abort_if(g, isnull, msg);
}

/* Emit a runtime guard: when `is_error` is true at runtime, print
 * "<file>:<line>:<col>: runtime error: <msg>" and exit(70). Execution
 * continues (in a fresh block) on the non-error path. No-op when runtime
 * checks are disabled. Must be called with the builder inside `current_fn`. */
static void emit_runtime_check(zan_irgen_t *g, LLVMValueRef is_error,
                               zan_loc_t loc, const char *msg) {
    if (!g->runtime_checks || !g->current_fn) return;

    LLVMBasicBlockRef panic_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "rt.panic");
    LLVMBasicBlockRef cont_bb  = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "rt.cont");
    LLVMBuildCondBr(g->builder, is_error, panic_bb, cont_bb);

    LLVMPositionBuilderAtEnd(g->builder, panic_bb);
    char buf[640];
    const char *file = g->src_file ? g->src_file : "<unknown>";
    snprintf(buf, sizeof(buf), "%s:%u:%u: runtime error: %s\n",
             file, loc.line, loc.col, msg);
    LLVMValueRef text = LLVMBuildGlobalStringPtr(g->builder, buf, "rterr");
    LLVMValueRef fmt = LLVMBuildGlobalStringPtr(g->builder, "%s", "rterr_fmt");
    LLVMValueRef pargs[] = { fmt, text };
    zan_call2(g->builder, g->printf_type, g->fn_printf, pargs, 2, "");
    LLVMValueRef code = LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 70, 0);
    zan_call2(g->builder, g->exit_type, g->fn_exit, &code, 1, "");
    LLVMBuildUnreachable(g->builder);

    LLVMPositionBuilderAtEnd(g->builder, cont_bb);
}

/* Resolve the base pointer to a struct/class instance held by a local.
 * A class local may be stored either inline (alloca of the struct itself,
 * a value-like representation) or as a pointer (alloca of a pointer to a
 * heap object). Inspect the allocated type and load through the pointer
 * only in the latter case, so field GEPs work for both representations. */
static LLVMValueRef struct_base_ptr(zan_irgen_t *g, local_var_t *local, LLVMTypeRef st) {
    LLVMValueRef base = local->alloca;
    if (LLVMIsAAllocaInst(base)) {
        LLVMTypeRef alloc_t = LLVMGetAllocatedType(base);
        if (LLVMGetTypeKind(alloc_t) == LLVMPointerTypeKind) {
            base = LLVMBuildLoad2(g->builder, LLVMPointerType(st, 0), base, "objld");
        }
    }
    return base;
}

/* Coerce two integer operands to their wider common width via sign extension.
 * No-op unless both operands are integers of differing widths (floats, pointers
 * and equal-width integers pass through). This lets mixed-width integer
 * expressions -- e.g. an i32 `int` loop variable compared against an i64
 * `List.Count`/`string.Length` -- generate valid IR. */
static void coerce_int_pair(zan_irgen_t *g, LLVMValueRef *a, LLVMValueRef *b) {
    LLVMTypeRef ta = LLVMTypeOf(*a), tb = LLVMTypeOf(*b);
    if (LLVMGetTypeKind(ta) != LLVMIntegerTypeKind ||
        LLVMGetTypeKind(tb) != LLVMIntegerTypeKind) return;
    unsigned wa = LLVMGetIntTypeWidth(ta), wb = LLVMGetIntTypeWidth(tb);
    if (wa == wb) return;
    if (wa < wb) *a = LLVMBuildSExt(g->builder, *a, tb, "iext");
    else         *b = LLVMBuildSExt(g->builder, *b, ta, "iext");
}

/* Normalize an integer value to a target integer type before storing it into a
 * slot of that type, so a width mismatch (e.g. an i32-producing helper feeding
 * an i64 `int` local) never produces invalid/undefined IR. Non-integer or
 * matching-width values, and non-integer targets, pass through unchanged. */
static LLVMValueRef coerce_int_to(zan_irgen_t *g, LLVMValueRef v, LLVMTypeRef target) {
    LLVMTypeRef vt = LLVMTypeOf(v);
    if (LLVMGetTypeKind(vt) != LLVMIntegerTypeKind ||
        LLVMGetTypeKind(target) != LLVMIntegerTypeKind) return v;
    unsigned wv = LLVMGetIntTypeWidth(vt), wt = LLVMGetIntTypeWidth(target);
    if (wv == wt) return v;
    if (wv < wt) return LLVMBuildSExt(g->builder, v, target, "sext");
    return LLVMBuildTrunc(g->builder, v, target, "trunc");
}

/* Widen a narrow integer to i64 for numeric formatting/printing. Signed
 * integers are sign-extended, but i1 (bool) is zero-extended so that `true`
 * formats as 1 rather than -1 (sext of an i1 set to 1 yields all-ones). */
static LLVMValueRef emit_widen_i64_for_print(zan_irgen_t *g, LLVMValueRef v) {
    LLVMTypeRef vt = LLVMTypeOf(v);
    if (LLVMGetTypeKind(vt) != LLVMIntegerTypeKind) return v;
    unsigned w = LLVMGetIntTypeWidth(vt);
    if (w >= 64) return v;
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    if (w == 1) return LLVMBuildZExt(g->builder, v, i64, "bext");
    return LLVMBuildSExt(g->builder, v, i64, "ext");
}

static LLVMValueRef emit_string_alloc_rc(zan_irgen_t *g, LLVMValueRef payload_size) {
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMValueRef thirty_one = LLVMConstInt(i64, 31, 0);
    LLVMValueRef padded = LLVMBuildAdd(g->builder, payload_size, thirty_one, "str.pad");
    LLVMValueRef aligned = LLVMBuildAnd(g->builder, padded,
        LLVMConstInt(i64, ~UINT64_C(31), 0), "str.align");
    LLVMValueRef min_payload = LLVMConstInt(i64, 32, 0);
    LLVMValueRef payload = LLVMBuildSelect(g->builder,
        LLVMBuildICmp(g->builder, LLVMIntULT, aligned, min_payload, "str.small"),
        min_payload, aligned, "str.payload");
    LLVMValueRef user_ptr = zan_call2(g->builder,
        LLVMFunctionType(LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0),
                         (LLVMTypeRef[]){ i64 }, 1, 0),
        g->rt_str_alloc, &payload, 1, "str.raw");
    return user_ptr;
}

static LLVMValueRef emit_widen_i64_for_print(zan_irgen_t *g, LLVMValueRef v);
static LLVMValueRef emit_string_literal_rc(zan_irgen_t *g, zan_istr_t text);

/* Convert a scalar (int/float/bool/char) to a NUL-terminated C string in a
 * stack buffer so it can feed byte-level string machinery (StringBuilder
 * append, etc.). Pointer (string) values are returned unchanged. */
static LLVMValueRef emit_value_as_cstr(zan_irgen_t *g, LLVMValueRef v) {
    LLVMTypeRef vt = LLVMTypeOf(v);
    if (LLVMGetTypeKind(vt) == LLVMPointerTypeKind) return v;
    LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    if (LLVMGetTypeKind(vt) == LLVMIntegerTypeKind && LLVMGetIntTypeWidth(vt) == 1) {
        zan_istr_t t = { "true", 4 }, f = { "false", 5 };
        return LLVMBuildSelect(g->builder, v,
            emit_string_literal_rc(g, t), emit_string_literal_rc(g, f), "b2s");
    }
    LLVMValueRef buf = LLVMBuildArrayAlloca(g->builder, i8,
        LLVMConstInt(i64, 40, 0), "v2s.buf");
    const char *fmt;
    LLVMValueRef arg;
    if (LLVMGetTypeKind(vt) == LLVMDoubleTypeKind || LLVMGetTypeKind(vt) == LLVMFloatTypeKind) {
        fmt = "%g";
        arg = (LLVMGetTypeKind(vt) == LLVMFloatTypeKind)
            ? LLVMBuildFPExt(g->builder, v, LLVMDoubleTypeInContext(g->ctx), "f2d") : v;
    } else {
        fmt = "%lld";
        arg = emit_widen_i64_for_print(g, v);
    }
    zan_istr_t fs = { fmt, (int)strlen(fmt) };
    LLVMValueRef fmt_ptr = emit_string_literal_rc(g, fs);
    LLVMTypeRef snp_ty = LLVMFunctionType(LLVMInt32TypeInContext(g->ctx),
        (LLVMTypeRef[]){ i8ptr, i64, i8ptr }, 3, 1);
    zan_call2(g->builder, snp_ty, g->fn_snprintf,
        (LLVMValueRef[]){ buf, LLVMConstInt(i64, 40, 0), fmt_ptr, arg }, 4, "");
    return buf;
}

/* Append slen bytes from s to the StringBuilder pointed to by sbp (typed
 * pointer to g->sb_struct_type), growing the backing buffer as needed. */
static void emit_sb_append_bytes(zan_irgen_t *g, LLVMValueRef sbp,
                                 LLVMValueRef s, LLVMValueRef slen) {
    LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMValueRef cptr = LLVMBuildStructGEP2(g->builder, g->sb_struct_type, sbp, 0, "sbcp");
    LLVMValueRef count = LLVMBuildLoad2(g->builder, i64, cptr, "sbcv");
    LLVMValueRef capptr = LLVMBuildStructGEP2(g->builder, g->sb_struct_type, sbp, 1, "sbcapp");
    LLVMValueRef cap = LLVMBuildLoad2(g->builder, i64, capptr, "sbcapv");
    LLVMValueRef need = LLVMBuildAdd(g->builder, count, slen, "sbneed");
    LLVMValueRef full = LLVMBuildICmp(g->builder, LLVMIntSGT, need, cap, "sbfull");
    LLVMBasicBlockRef grow_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "sb.grow");
    LLVMBasicBlockRef st_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "sb.st");
    LLVMBuildCondBr(g->builder, full, grow_bb, st_bb);
    /* grow: newcap = max(cap*2, need); realloc data */
    LLVMPositionBuilderAtEnd(g->builder, grow_bb);
    LLVMValueRef nc0 = LLVMBuildMul(g->builder, cap, LLVMConstInt(i64, 2, 0), "sbnc0");
    LLVMValueRef small = LLVMBuildICmp(g->builder, LLVMIntSLT, nc0, need, "sbsm");
    LLVMValueRef nc = LLVMBuildSelect(g->builder, small, need, nc0, "sbnc");
    LLVMValueRef dptr = LLVMBuildStructGEP2(g->builder, g->sb_struct_type, sbp, 2, "sbdp");
    LLVMValueRef olddata = LLVMBuildLoad2(g->builder, i8ptr, dptr, "sbod");
    LLVMValueRef newdata = zan_call2(g->builder,
        LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i64 }, 2, 0),
        g->fn_realloc, (LLVMValueRef[]){ olddata, nc }, 2, "sbnd");
    LLVMBuildStore(g->builder, newdata, dptr);
    LLVMBuildStore(g->builder, nc, capptr);
    LLVMBuildBr(g->builder, st_bb);
    /* store: memcpy(data+count, s, slen); count += slen */
    LLVMPositionBuilderAtEnd(g->builder, st_bb);
    LLVMValueRef dptr2 = LLVMBuildStructGEP2(g->builder, g->sb_struct_type, sbp, 2, "sbdp2");
    LLVMValueRef data = LLVMBuildLoad2(g->builder, i8ptr, dptr2, "sbdv");
    LLVMValueRef dest = LLVMBuildGEP2(g->builder, i8, data, &count, 1, "sbdest");
    LLVMValueRef memcpy_fn = LLVMGetNamedFunction(g->mod, "memcpy");
    LLVMTypeRef memcpy_ty = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr, i64 }, 3, 0);
    if (!memcpy_fn) memcpy_fn = LLVMAddFunction(g->mod, "memcpy", memcpy_ty);
    zan_call2(g->builder, memcpy_ty, memcpy_fn,
        (LLVMValueRef[]){ dest, s, slen }, 3, "");
    LLVMValueRef ncount = LLVMBuildAdd(g->builder, count, slen, "sbncount");
    LLVMBuildStore(g->builder, ncount, cptr);
}

static LLVMValueRef emit_string_literal_rc(zan_irgen_t *g, zan_istr_t text) {
    for (int i = 0; i < g->string_literal_count; i++) {
        if ((size_t)g->string_literals[i].text.len == (size_t)text.len &&
            memcmp(g->string_literals[i].text.str, text.str, (size_t)text.len) == 0) {
            LLVMValueRef global = g->string_literals[i].value;
            LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
            LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
            LLVMTypeRef lit_ty = LLVMArrayType(i8, (unsigned)(16u + (size_t)text.len + 1u));
            LLVMValueRef idxs[] = { LLVMConstInt(i64, 0, 0), LLVMConstInt(i64, 16, 0) };
            return LLVMBuildGEP2(g->builder, lit_ty, global, idxs, 2, "str.lit.ptr");
        }
    }

    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
    size_t total = 16u + (size_t)text.len + 1u;
    LLVMTypeRef lit_ty = LLVMArrayType(i8, (unsigned)total);
    char name[64];
    snprintf(name, sizeof(name), "__zan.strlit.%d", g->string_literal_count);
    LLVMValueRef global = LLVMAddGlobal(g->mod, lit_ty, name);
    LLVMSetLinkage(global, LLVMPrivateLinkage);
    LLVMSetGlobalConstant(global, 1);
    LLVMSetUnnamedAddr(global, LLVMGlobalUnnamedAddr);
    unsigned char *blob = (unsigned char *)calloc(total, 1);
    if (!blob) return LLVMConstNull(LLVMPointerType(i8, 0));
    memcpy(blob + 0, &((uint64_t){ ZAN_STRING_SENTINEL_RC }), 8);
    memcpy(blob + 8, &((uint64_t){ ZAN_STRING_MAGIC }), 8);
    if (text.len > 0) memcpy(blob + 16, text.str, (size_t)text.len);
    blob[16 + (size_t)text.len] = 0;
    LLVMValueRef *init_elems = (LLVMValueRef *)calloc(total, sizeof(LLVMValueRef));
    if (!init_elems) {
        free(blob);
        return LLVMConstNull(LLVMPointerType(i8, 0));
    }
    for (size_t i = 0; i < total; i++) {
        init_elems[i] = LLVMConstInt(i8, blob[i], 0);
    }
    LLVMValueRef init = LLVMConstArray(i8, init_elems, (unsigned)total);
    LLVMSetInitializer(global, init);
    free(init_elems);
    free(blob);

    LLVMValueRef idxs[] = { LLVMConstInt(i64, 0, 0), LLVMConstInt(i64, 16, 0) };
    LLVMValueRef user_ptr = LLVMBuildGEP2(g->builder, lit_ty, global, idxs, 2, "str.lit.ptr");

    if (g->string_literal_count < (int)(sizeof(g->string_literals) / sizeof(g->string_literals[0]))) {
        g->string_literals[g->string_literal_count].text = text;
        g->string_literals[g->string_literal_count].value = global;
        g->string_literal_count++;
    }
    return user_ptr;
}

/* Coerce a value to an i8* C string. Pointers pass through unchanged; integer
 * and floating operands are formatted into a fresh heap buffer via snprintf.
 * Lets `+` concatenate strings with numbers (e.g. "%t" + counter). */
static LLVMValueRef emit_to_cstr(zan_irgen_t *g, LLVMValueRef val) {
    LLVMTypeRef vt = LLVMTypeOf(val);
    LLVMTypeKind vtk = LLVMGetTypeKind(vt);
    if (vtk == LLVMPointerTypeKind) return val;

    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
    LLVMTypeRef snprintf_type = LLVMFunctionType(
        LLVMInt32TypeInContext(g->ctx), (LLVMTypeRef[]){ i8ptr, i64, i8ptr }, 3, 1);
    LLVMValueRef fmt;
    LLVMValueRef arg;
    if (vtk == LLVMDoubleTypeKind || vtk == LLVMFloatTypeKind) {
        fmt = LLVMBuildGlobalStringPtr(g->builder, "%g", "dfmt");
        arg = val;
    } else {
        fmt = LLVMBuildGlobalStringPtr(g->builder, "%lld", "ifmt");
        arg = emit_widen_i64_for_print(g, val);
    }
    LLVMValueRef tmp_sz = LLVMConstInt(i64, 1024, 0);
    LLVMValueRef tmp = LLVMBuildArrayAlloca(g->builder, i8, tmp_sz, "fmt.tmp");
    LLVMValueRef a1[] = { tmp, tmp_sz, fmt, arg };
    zan_call2(g->builder, snprintf_type, g->fn_snprintf, a1, 4, "");
    LLVMValueRef needed = zan_call2(g->builder,
        LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr }, 1, 0),
        LLVMGetNamedFunction(g->mod, "strlen"), &tmp, 1, "needed");
    LLVMValueRef bsz = LLVMBuildAdd(g->builder, needed, LLVMConstInt(i64, 1, 0), "bsz");
    LLVMValueRef buf = emit_string_alloc_rc(g, bsz);
    zan_call2(g->builder,
        LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0),
        LLVMGetNamedFunction(g->mod, "strcpy"),
        (LLVMValueRef[]){ buf, tmp }, 2, "");
    return buf;
}

/* Emit `a + b` for two string (i8*) operands as a heap-allocated concatenation:
 * malloc(strlen(a)+strlen(b)+1); memcpy left, memcpy right, NUL terminate. */
static LLVMValueRef emit_str_concat(zan_irgen_t *g, LLVMValueRef a, LLVMValueRef b) {
    LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef strlen_type = LLVMFunctionType(i64t, (LLVMTypeRef[]){ i8ptr }, 1, 0);
    LLVMValueRef la = zan_call2(g->builder, strlen_type, g->fn_strlen, &a, 1, "cla");
    LLVMValueRef lb = zan_call2(g->builder, strlen_type, g->fn_strlen, &b, 1, "clb");
    LLVMValueRef tot = LLVMBuildAdd(g->builder, la, lb, "ct");
    tot = LLVMBuildAdd(g->builder, tot, LLVMConstInt(i64t, 1, 0), "ct1");
    LLVMValueRef buf = emit_string_alloc_rc(g, tot);
    LLVMTypeRef memcpy_type = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr, i64t }, 3, 0);
    LLVMValueRef memcpy_fn = LLVMGetNamedFunction(g->mod, "memcpy");
    if (!memcpy_fn) memcpy_fn = LLVMAddFunction(g->mod, "memcpy", memcpy_type);
    zan_call2(g->builder, memcpy_type, memcpy_fn,
        (LLVMValueRef[]){ buf, a, la }, 3, "");
    LLVMValueRef dst_b = LLVMBuildGEP2(g->builder, LLVMInt8TypeInContext(g->ctx), buf, &la, 1, "dst.b");
    zan_call2(g->builder, memcpy_type, memcpy_fn,
        (LLVMValueRef[]){ dst_b, b, lb }, 3, "");
    LLVMValueRef end_off = LLVMBuildAdd(g->builder, la, lb, "slen");
    LLVMValueRef endp = LLVMBuildGEP2(g->builder, LLVMInt8TypeInContext(g->ctx), buf, &end_off, 1, "end");
    LLVMBuildStore(g->builder, LLVMConstInt(LLVMInt8TypeInContext(g->ctx), 0, 0), endp);
    return buf;
}

/* True when `e` is a string-yielding `+` node (either operand is a string),
 * i.e. a link of a concatenation chain. */
static bool is_str_concat_node(zan_irgen_t *g, zan_ast_node_t *e, local_scope_t *locals) {
    return e && e->kind == AST_BINARY && e->binary.op == TK_PLUS &&
        (is_string_expr(g, e->binary.left, locals) ||
         is_string_expr(g, e->binary.right, locals));
}

/* Collect the leaves of a `+` concatenation chain in evaluation order.
 * Stops descending once `ops` is nearly full; an overflowing sub-chain is
 * kept as a single leaf and flattened again when it is itself emitted. */
static void collect_concat_ops(zan_irgen_t *g, zan_ast_node_t *e, local_scope_t *locals,
                               zan_ast_node_t **ops, int *n, int max) {
    if (*n < max - 1 && is_str_concat_node(g, e, locals)) {
        collect_concat_ops(g, e->binary.left, locals, ops, n, max);
        collect_concat_ops(g, e->binary.right, locals, ops, n, max);
    } else {
        ops[(*n)++] = e;
    }
}

/* Emit an n-way string concatenation as a single allocation: sum the operand
 * lengths, allocate once, memcpy each part in order and NUL-terminate.
 * Replaces the O(n^2) copying of emitting a chain pairwise. */
static LLVMValueRef emit_str_concat_n(zan_irgen_t *g, zan_ast_node_t *expr,
                                      local_scope_t *locals) {
    enum { MAXOPS = 48 };
    zan_ast_node_t *ops[MAXOPS];
    int n = 0;
    collect_concat_ops(g, expr, locals, ops, &n, MAXOPS);

    LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i8t = LLVMInt8TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(i8t, 0);
    LLVMTypeRef strlen_type = LLVMFunctionType(i64t, (LLVMTypeRef[]){ i8ptr }, 1, 0);
    LLVMTypeRef memcpy_type = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr, i64t }, 3, 0);
    LLVMValueRef memcpy_fn = LLVMGetNamedFunction(g->mod, "memcpy");
    if (!memcpy_fn) memcpy_fn = LLVMAddFunction(g->mod, "memcpy", memcpy_type);

    LLVMValueRef vals[MAXOPS];
    LLVMValueRef lens[MAXOPS];
    bool owned[MAXOPS];
    LLVMValueRef total = LLVMConstInt(i64t, 1, 0); /* NUL */
    for (int i = 0; i < n; i++) {
        LLVMValueRef v = emit_expr(g, ops[i], locals);
        LLVMValueRef s = emit_to_cstr(g, v);
        vals[i] = s;
        owned[i] = !is_string_expr(g, ops[i], locals) ||
                   expr_yields_owned_rc_value(g, ops[i], locals);
        lens[i] = zan_call2(g->builder, strlen_type, g->fn_strlen, &s, 1, "cl");
        total = LLVMBuildAdd(g->builder, total, lens[i], "ct");
    }
    LLVMValueRef buf = emit_string_alloc_rc(g, total);
    LLVMValueRef off = LLVMConstInt(i64t, 0, 0);
    for (int i = 0; i < n; i++) {
        LLVMValueRef dst = LLVMBuildGEP2(g->builder, i8t, buf, &off, 1, "dst");
        zan_call2(g->builder, memcpy_type, memcpy_fn,
            (LLVMValueRef[]){ dst, vals[i], lens[i] }, 3, "");
        off = LLVMBuildAdd(g->builder, off, lens[i], "off");
    }
    LLVMValueRef endp = LLVMBuildGEP2(g->builder, i8t, buf, &off, 1, "end");
    LLVMBuildStore(g->builder, LLVMConstInt(i8t, 0, 0), endp);
    for (int i = 0; i < n; i++) {
        if (owned[i]) emit_string_release(g, vals[i]);
    }
    return buf;
}

/* Return the internal `__zan_co_reap` step function, creating it once per
 * module. It matches zan_co_step_t (void(i8*)) and simply frees the frame it is
 * handed. A detached (Task.Spawn) coroutine has no awaiter to hand its frame
 * back to, so it installs this as its own awaiter step: on completion
 * emit_async_complete re-enqueues (frame, __zan_co_reap), and the driver later
 * frees the frame -- otherwise every spawned coroutine leaks its heap frame,
 * the residual per-connection leak in long-running socket servers. */
static LLVMValueRef get_co_reap_fn(zan_irgen_t *g) {
    LLVMValueRef reap = LLVMGetNamedFunction(g->mod, "__zan_co_reap");
    if (reap) return reap;
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef reap_ty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), &i8ptr, 1, 0);
    reap = LLVMAddFunction(g->mod, "__zan_co_reap", reap_ty);
    LLVMSetLinkage(reap, LLVMInternalLinkage);
    LLVMBasicBlockRef saved = LLVMGetInsertBlock(g->builder);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, reap, "entry");
    LLVMPositionBuilderAtEnd(g->builder, entry);
    LLVMValueRef arg = LLVMGetParam(reap, 0);
    zan_call2(g->builder, LLVMGlobalGetValueType(g->fn_free), g->fn_free, &arg, 1, "");
    LLVMBuildRetVoid(g->builder);
    if (saved) LLVMPositionBuilderAtEnd(g->builder, saved);
    return reap;
}

/* ---- builtin string-method helpers (lazily emitted once per module) ----
 * Bodies use libc (strlen/strstr/memcpy/toupper) plus zan_rt_str_alloc, so
 * every returned string is an ordinary owned rc string. */

static LLVMValueRef get_libc_fn(zan_irgen_t *g, const char *name, LLVMTypeRef ty) {
    LLVMValueRef f = LLVMGetNamedFunction(g->mod, name);
    if (!f) f = LLVMAddFunction(g->mod, name, ty);
    return f;
}

/* __zan_str_last_index_of(s, sub) -> i64: byte index of the last occurrence
 * of `sub` in `s`, or -1. An empty needle yields strlen(s). */
static LLVMValueRef get_str_last_index_of_fn(zan_irgen_t *g) {
    LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "__zan_str_last_index_of");
    if (fn) return fn;
    LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef fnty = LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0);
    fn = LLVMAddFunction(g->mod, "__zan_str_last_index_of", fnty);
    LLVMSetLinkage(fn, LLVMInternalLinkage);
    LLVMTypeRef strlen_ty = LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr }, 1, 0);
    LLVMTypeRef strstr_ty = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0);
    LLVMValueRef strstr_fn = get_libc_fn(g, "strstr", strstr_ty);
    LLVMBasicBlockRef saved = LLVMGetInsertBlock(g->builder);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
    LLVMBasicBlockRef empty_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "empty");
    LLVMBasicBlockRef loop = LLVMAppendBasicBlockInContext(g->ctx, fn, "loop");
    LLVMBasicBlockRef found = LLVMAppendBasicBlockInContext(g->ctx, fn, "found");
    LLVMBasicBlockRef done = LLVMAppendBasicBlockInContext(g->ctx, fn, "done");
    LLVMPositionBuilderAtEnd(g->builder, entry);
    LLVMValueRef s = LLVMGetParam(fn, 0);
    LLVMValueRef sub = LLVMGetParam(fn, 1);
    LLVMValueRef best = LLVMBuildAlloca(g->builder, i64, "best");
    LLVMValueRef cur = LLVMBuildAlloca(g->builder, i8ptr, "cur");
    LLVMBuildStore(g->builder, LLVMConstInt(i64, (uint64_t)-1, 1), best);
    LLVMBuildStore(g->builder, s, cur);
    LLVMValueRef sub0 = LLVMBuildLoad2(g->builder, i8, sub, "sub0");
    LLVMValueRef sub_empty = LLVMBuildICmp(g->builder, LLVMIntEQ, sub0,
        LLVMConstInt(i8, 0, 0), "subempty");
    LLVMBuildCondBr(g->builder, sub_empty, empty_bb, loop);
    LLVMPositionBuilderAtEnd(g->builder, empty_bb);
    LLVMValueRef slen = zan_call2(g->builder, strlen_ty, g->fn_strlen, &s, 1, "slen");
    LLVMBuildRet(g->builder, slen);
    LLVMPositionBuilderAtEnd(g->builder, loop);
    LLVMValueRef p = LLVMBuildLoad2(g->builder, i8ptr, cur, "p");
    LLVMValueRef ss_args[] = { p, sub };
    LLVMValueRef hit = zan_call2(g->builder, strstr_ty, strstr_fn, ss_args, 2, "hit");
    LLVMValueRef miss = LLVMBuildICmp(g->builder, LLVMIntEQ, hit,
        LLVMConstNull(i8ptr), "miss");
    LLVMBuildCondBr(g->builder, miss, done, found);
    LLVMPositionBuilderAtEnd(g->builder, found);
    LLVMValueRef idx = LLVMBuildSub(g->builder,
        LLVMBuildPtrToInt(g->builder, hit, i64, "hi"),
        LLVMBuildPtrToInt(g->builder, s, i64, "si"), "idx");
    LLVMBuildStore(g->builder, idx, best);
    LLVMValueRef one = LLVMConstInt(i64, 1, 0);
    LLVMValueRef next = LLVMBuildGEP2(g->builder, i8, hit, &one, 1, "next");
    LLVMBuildStore(g->builder, next, cur);
    LLVMBuildBr(g->builder, loop);
    LLVMPositionBuilderAtEnd(g->builder, done);
    LLVMBuildRet(g->builder, LLVMBuildLoad2(g->builder, i64, best, "res"));
    if (saved) LLVMPositionBuilderAtEnd(g->builder, saved);
    return fn;
}

/* __zan_str_trim(s) -> i8*: fresh rc string with leading/trailing ASCII
 * whitespace (space, \t, \r, \n, \v, \f) removed. */
static LLVMValueRef get_str_trim_fn(zan_irgen_t *g) {
    LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "__zan_str_trim");
    if (fn) return fn;
    LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
    LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef fnty = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr }, 1, 0);
    fn = LLVMAddFunction(g->mod, "__zan_str_trim", fnty);
    LLVMSetLinkage(fn, LLVMInternalLinkage);
    LLVMTypeRef strlen_ty = LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr }, 1, 0);
    LLVMTypeRef isspace_ty = LLVMFunctionType(i32, (LLVMTypeRef[]){ i32 }, 1, 0);
    LLVMTypeRef memcpy_ty = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr, i64 }, 3, 0);
    LLVMValueRef isspace_fn = get_libc_fn(g, "isspace", isspace_ty);
    LLVMValueRef memcpy_fn = get_libc_fn(g, "memcpy", memcpy_ty);
    LLVMBasicBlockRef saved = LLVMGetInsertBlock(g->builder);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
    LLVMBasicBlockRef lead = LLVMAppendBasicBlockInContext(g->ctx, fn, "lead");
    LLVMBasicBlockRef lead_ws = LLVMAppendBasicBlockInContext(g->ctx, fn, "lead_ws");
    LLVMBasicBlockRef tail_init = LLVMAppendBasicBlockInContext(g->ctx, fn, "tail_init");
    LLVMBasicBlockRef tail = LLVMAppendBasicBlockInContext(g->ctx, fn, "tail");
    LLVMBasicBlockRef tail_chk = LLVMAppendBasicBlockInContext(g->ctx, fn, "tail_chk");
    LLVMBasicBlockRef tail_ws = LLVMAppendBasicBlockInContext(g->ctx, fn, "tail_ws");
    LLVMBasicBlockRef copy = LLVMAppendBasicBlockInContext(g->ctx, fn, "copy");
    LLVMPositionBuilderAtEnd(g->builder, entry);
    LLVMValueRef s = LLVMGetParam(fn, 0);
    LLVMValueRef pvar = LLVMBuildAlloca(g->builder, i8ptr, "pvar");
    LLVMValueRef nvar = LLVMBuildAlloca(g->builder, i64, "nvar");
    LLVMBuildStore(g->builder, s, pvar);
    LLVMBuildBr(g->builder, lead);
    LLVMPositionBuilderAtEnd(g->builder, lead);
    LLVMValueRef p = LLVMBuildLoad2(g->builder, i8ptr, pvar, "p");
    LLVMValueRef c = LLVMBuildLoad2(g->builder, i8, p, "c");
    LLVMValueRef c32 = LLVMBuildZExt(g->builder, c, i32, "c32");
    LLVMValueRef ws = zan_call2(g->builder, isspace_ty, isspace_fn, &c32, 1, "ws");
    LLVMValueRef nz = LLVMBuildICmp(g->builder, LLVMIntNE, ws,
        LLVMConstInt(i32, 0, 0), "isws");
    LLVMValueRef not_nul = LLVMBuildICmp(g->builder, LLVMIntNE, c,
        LLVMConstInt(i8, 0, 0), "notnul");
    LLVMValueRef adv = LLVMBuildAnd(g->builder, nz, not_nul, "adv");
    LLVMBuildCondBr(g->builder, adv, lead_ws, tail_init);
    LLVMPositionBuilderAtEnd(g->builder, lead_ws);
    LLVMValueRef one64 = LLVMConstInt(i64, 1, 0);
    LLVMValueRef p1 = LLVMBuildGEP2(g->builder, i8, p, &one64, 1, "p1");
    LLVMBuildStore(g->builder, p1, pvar);
    LLVMBuildBr(g->builder, lead);
    LLVMPositionBuilderAtEnd(g->builder, tail_init);
    LLVMValueRef base = LLVMBuildLoad2(g->builder, i8ptr, pvar, "base");
    LLVMValueRef n0 = zan_call2(g->builder, strlen_ty, g->fn_strlen, &base, 1, "n0");
    LLVMBuildStore(g->builder, n0, nvar);
    LLVMBuildBr(g->builder, tail);
    LLVMPositionBuilderAtEnd(g->builder, tail);
    LLVMValueRef n = LLVMBuildLoad2(g->builder, i64, nvar, "n");
    LLVMValueRef npos = LLVMBuildICmp(g->builder, LLVMIntSGT, n,
        LLVMConstInt(i64, 0, 0), "npos");
    LLVMBuildCondBr(g->builder, npos, tail_chk, copy);
    LLVMPositionBuilderAtEnd(g->builder, tail_chk);
    LLVMValueRef nm1 = LLVMBuildSub(g->builder, n, one64, "nm1");
    LLVMValueRef lastp = LLVMBuildGEP2(g->builder, i8, base, &nm1, 1, "lastp");
    LLVMValueRef lc = LLVMBuildLoad2(g->builder, i8, lastp, "lc");
    LLVMValueRef lc32 = LLVMBuildZExt(g->builder, lc, i32, "lc32");
    LLVMValueRef lws = zan_call2(g->builder, isspace_ty, isspace_fn, &lc32, 1, "lws");
    LLVMValueRef lnz = LLVMBuildICmp(g->builder, LLVMIntNE, lws,
        LLVMConstInt(i32, 0, 0), "lisws");
    LLVMBuildCondBr(g->builder, lnz, tail_ws, copy);
    LLVMPositionBuilderAtEnd(g->builder, tail_ws);
    LLVMBuildStore(g->builder, nm1, nvar);
    LLVMBuildBr(g->builder, tail);
    LLVMPositionBuilderAtEnd(g->builder, copy);
    LLVMValueRef fin_n = LLVMBuildLoad2(g->builder, i64, nvar, "finn");
    LLVMValueRef bufsz = LLVMBuildAdd(g->builder, fin_n, one64, "bsz");
    LLVMValueRef buf = emit_string_alloc_rc(g, bufsz);
    LLVMValueRef mcargs[] = { buf, base, fin_n };
    zan_call2(g->builder, memcpy_ty, memcpy_fn, mcargs, 3, "");
    LLVMValueRef endp = LLVMBuildGEP2(g->builder, i8, buf, &fin_n, 1, "endp");
    LLVMBuildStore(g->builder, LLVMConstInt(i8, 0, 0), endp);
    LLVMBuildRet(g->builder, buf);
    if (saved) LLVMPositionBuilderAtEnd(g->builder, saved);
    return fn;
}

/* __zan_str_to_upper / __zan_str_to_lower: per-byte toupper()/tolower() into
 * a fresh rc string. */
static LLVMValueRef get_str_case_fn(zan_irgen_t *g, int upper) {
    const char *fname = upper ? "__zan_str_to_upper" : "__zan_str_to_lower";
    LLVMValueRef fn = LLVMGetNamedFunction(g->mod, fname);
    if (fn) return fn;
    LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
    LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef fnty = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr }, 1, 0);
    fn = LLVMAddFunction(g->mod, fname, fnty);
    LLVMSetLinkage(fn, LLVMInternalLinkage);
    LLVMTypeRef strlen_ty = LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr }, 1, 0);
    LLVMTypeRef conv_ty = LLVMFunctionType(i32, (LLVMTypeRef[]){ i32 }, 1, 0);
    LLVMValueRef conv_fn = get_libc_fn(g, upper ? "toupper" : "tolower", conv_ty);
    LLVMBasicBlockRef saved = LLVMGetInsertBlock(g->builder);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
    LLVMBasicBlockRef loop = LLVMAppendBasicBlockInContext(g->ctx, fn, "loop");
    LLVMBasicBlockRef body = LLVMAppendBasicBlockInContext(g->ctx, fn, "body");
    LLVMBasicBlockRef done = LLVMAppendBasicBlockInContext(g->ctx, fn, "done");
    LLVMPositionBuilderAtEnd(g->builder, entry);
    LLVMValueRef s = LLVMGetParam(fn, 0);
    LLVMValueRef n = zan_call2(g->builder, strlen_ty, g->fn_strlen, &s, 1, "n");
    LLVMValueRef bufsz = LLVMBuildAdd(g->builder, n, LLVMConstInt(i64, 1, 0), "bsz");
    LLVMValueRef buf = emit_string_alloc_rc(g, bufsz);
    LLVMValueRef ivar = LLVMBuildAlloca(g->builder, i64, "ivar");
    LLVMBuildStore(g->builder, LLVMConstInt(i64, 0, 0), ivar);
    LLVMBuildBr(g->builder, loop);
    LLVMPositionBuilderAtEnd(g->builder, loop);
    LLVMValueRef i = LLVMBuildLoad2(g->builder, i64, ivar, "i");
    LLVMValueRef more = LLVMBuildICmp(g->builder, LLVMIntULT, i, n, "more");
    LLVMBuildCondBr(g->builder, more, body, done);
    LLVMPositionBuilderAtEnd(g->builder, body);
    LLVMValueRef sp = LLVMBuildGEP2(g->builder, i8, s, &i, 1, "sp");
    LLVMValueRef c = LLVMBuildLoad2(g->builder, i8, sp, "c");
    LLVMValueRef c32 = LLVMBuildZExt(g->builder, c, i32, "c32");
    LLVMValueRef conv = zan_call2(g->builder, conv_ty, conv_fn, &c32, 1, "conv");
    LLVMValueRef c8 = LLVMBuildTrunc(g->builder, conv, i8, "c8");
    LLVMValueRef dp = LLVMBuildGEP2(g->builder, i8, buf, &i, 1, "dp");
    LLVMBuildStore(g->builder, c8, dp);
    LLVMValueRef i1v = LLVMBuildAdd(g->builder, i, LLVMConstInt(i64, 1, 0), "i1");
    LLVMBuildStore(g->builder, i1v, ivar);
    LLVMBuildBr(g->builder, loop);
    LLVMPositionBuilderAtEnd(g->builder, done);
    LLVMValueRef endp = LLVMBuildGEP2(g->builder, i8, buf, &n, 1, "endp");
    LLVMBuildStore(g->builder, LLVMConstInt(i8, 0, 0), endp);
    LLVMBuildRet(g->builder, buf);
    if (saved) LLVMPositionBuilderAtEnd(g->builder, saved);
    return fn;
}

/* __zan_str_replace(s, from, to) -> i8*: fresh rc string with every
 * non-overlapping occurrence of `from` replaced by `to`. An empty `from`
 * returns a copy of `s`. */
static LLVMValueRef get_str_replace_fn(zan_irgen_t *g) {
    LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "__zan_str_replace");
    if (fn) return fn;
    LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef fnty = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr, i8ptr }, 3, 0);
    fn = LLVMAddFunction(g->mod, "__zan_str_replace", fnty);
    LLVMSetLinkage(fn, LLVMInternalLinkage);
    LLVMTypeRef strlen_ty = LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr }, 1, 0);
    LLVMTypeRef strstr_ty = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0);
    LLVMTypeRef memcpy_ty = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr, i64 }, 3, 0);
    LLVMValueRef strstr_fn = get_libc_fn(g, "strstr", strstr_ty);
    LLVMValueRef memcpy_fn = get_libc_fn(g, "memcpy", memcpy_ty);
    LLVMBasicBlockRef saved = LLVMGetInsertBlock(g->builder);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
    LLVMBasicBlockRef dup_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "dup");
    LLVMBasicBlockRef cnt_loop = LLVMAppendBasicBlockInContext(g->ctx, fn, "cnt_loop");
    LLVMBasicBlockRef cnt_hit = LLVMAppendBasicBlockInContext(g->ctx, fn, "cnt_hit");
    LLVMBasicBlockRef alloc_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "alloc");
    LLVMBasicBlockRef bld_loop = LLVMAppendBasicBlockInContext(g->ctx, fn, "bld_loop");
    LLVMBasicBlockRef bld_hit = LLVMAppendBasicBlockInContext(g->ctx, fn, "bld_hit");
    LLVMBasicBlockRef tail_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "tail");
    LLVMPositionBuilderAtEnd(g->builder, entry);
    LLVMValueRef s = LLVMGetParam(fn, 0);
    LLVMValueRef from = LLVMGetParam(fn, 1);
    LLVMValueRef to = LLVMGetParam(fn, 2);
    LLVMValueRef one64 = LLVMConstInt(i64, 1, 0);
    LLVMValueRef ls = zan_call2(g->builder, strlen_ty, g->fn_strlen, &s, 1, "ls");
    LLVMValueRef lf = zan_call2(g->builder, strlen_ty, g->fn_strlen, &from, 1, "lf");
    LLVMValueRef lt = zan_call2(g->builder, strlen_ty, g->fn_strlen, &to, 1, "lt");
    LLVMValueRef cntv = LLVMBuildAlloca(g->builder, i64, "cntv");
    LLVMValueRef pvar = LLVMBuildAlloca(g->builder, i8ptr, "pvar");
    LLVMValueRef dvar = LLVMBuildAlloca(g->builder, i8ptr, "dvar");
    LLVMBuildStore(g->builder, LLVMConstInt(i64, 0, 0), cntv);
    LLVMBuildStore(g->builder, s, pvar);
    LLVMValueRef from_empty = LLVMBuildICmp(g->builder, LLVMIntEQ, lf,
        LLVMConstInt(i64, 0, 0), "fempty");
    LLVMBuildCondBr(g->builder, from_empty, dup_bb, cnt_loop);
    LLVMPositionBuilderAtEnd(g->builder, dup_bb);
    LLVMValueRef dupsz = LLVMBuildAdd(g->builder, ls, one64, "dupsz");
    LLVMValueRef dupbuf = emit_string_alloc_rc(g, dupsz);
    LLVMValueRef dupargs[] = { dupbuf, s, dupsz };
    zan_call2(g->builder, memcpy_ty, memcpy_fn, dupargs, 3, "");
    LLVMBuildRet(g->builder, dupbuf);
    LLVMPositionBuilderAtEnd(g->builder, cnt_loop);
    LLVMValueRef cp = LLVMBuildLoad2(g->builder, i8ptr, pvar, "cp");
    LLVMValueRef cargs[] = { cp, from };
    LLVMValueRef chit = zan_call2(g->builder, strstr_ty, strstr_fn, cargs, 2, "chit");
    LLVMValueRef cmiss = LLVMBuildICmp(g->builder, LLVMIntEQ, chit,
        LLVMConstNull(i8ptr), "cmiss");
    LLVMBuildCondBr(g->builder, cmiss, alloc_bb, cnt_hit);
    LLVMPositionBuilderAtEnd(g->builder, cnt_hit);
    LLVMValueRef cnt = LLVMBuildLoad2(g->builder, i64, cntv, "cnt");
    LLVMBuildStore(g->builder, LLVMBuildAdd(g->builder, cnt, one64, "cnt1"), cntv);
    LLVMValueRef cnext = LLVMBuildGEP2(g->builder, i8, chit, &lf, 1, "cnext");
    LLVMBuildStore(g->builder, cnext, pvar);
    LLVMBuildBr(g->builder, cnt_loop);
    LLVMPositionBuilderAtEnd(g->builder, alloc_bb);
    LLVMValueRef total_cnt = LLVMBuildLoad2(g->builder, i64, cntv, "tcnt");
    LLVMValueRef delta = LLVMBuildSub(g->builder, lt, lf, "delta");
    LLVMValueRef grow = LLVMBuildMul(g->builder, total_cnt, delta, "grow");
    LLVMValueRef newlen = LLVMBuildAdd(g->builder, ls, grow, "newlen");
    LLVMValueRef bufsz = LLVMBuildAdd(g->builder, newlen, one64, "bsz");
    LLVMValueRef buf = emit_string_alloc_rc(g, bufsz);
    LLVMBuildStore(g->builder, s, pvar);
    LLVMBuildStore(g->builder, buf, dvar);
    LLVMBuildBr(g->builder, bld_loop);
    LLVMPositionBuilderAtEnd(g->builder, bld_loop);
    LLVMValueRef bp = LLVMBuildLoad2(g->builder, i8ptr, pvar, "bp");
    LLVMValueRef bargs[] = { bp, from };
    LLVMValueRef bhit = zan_call2(g->builder, strstr_ty, strstr_fn, bargs, 2, "bhit");
    LLVMValueRef bmiss = LLVMBuildICmp(g->builder, LLVMIntEQ, bhit,
        LLVMConstNull(i8ptr), "bmiss");
    LLVMBuildCondBr(g->builder, bmiss, tail_bb, bld_hit);
    LLVMPositionBuilderAtEnd(g->builder, bld_hit);
    LLVMValueRef d = LLVMBuildLoad2(g->builder, i8ptr, dvar, "d");
    LLVMValueRef pre = LLVMBuildSub(g->builder,
        LLVMBuildPtrToInt(g->builder, bhit, i64, "bhi"),
        LLVMBuildPtrToInt(g->builder, bp, i64, "bpi"), "pre");
    LLVMValueRef pre_args[] = { d, bp, pre };
    zan_call2(g->builder, memcpy_ty, memcpy_fn, pre_args, 3, "");
    LLVMValueRef d1 = LLVMBuildGEP2(g->builder, i8, d, &pre, 1, "d1");
    LLVMValueRef to_args[] = { d1, to, lt };
    zan_call2(g->builder, memcpy_ty, memcpy_fn, to_args, 3, "");
    LLVMValueRef d2 = LLVMBuildGEP2(g->builder, i8, d1, &lt, 1, "d2");
    LLVMBuildStore(g->builder, d2, dvar);
    LLVMValueRef bnext = LLVMBuildGEP2(g->builder, i8, bhit, &lf, 1, "bnext");
    LLVMBuildStore(g->builder, bnext, pvar);
    LLVMBuildBr(g->builder, bld_loop);
    LLVMPositionBuilderAtEnd(g->builder, tail_bb);
    LLVMValueRef tp = LLVMBuildLoad2(g->builder, i8ptr, pvar, "tp");
    LLVMValueRef td = LLVMBuildLoad2(g->builder, i8ptr, dvar, "td");
    LLVMValueRef rest = zan_call2(g->builder, strlen_ty, g->fn_strlen, &tp, 1, "rest");
    LLVMValueRef restsz = LLVMBuildAdd(g->builder, rest, one64, "restsz");
    LLVMValueRef rest_args[] = { td, tp, restsz };
    zan_call2(g->builder, memcpy_ty, memcpy_fn, rest_args, 3, "");
    LLVMBuildRet(g->builder, buf);
    if (saved) LLVMPositionBuilderAtEnd(g->builder, saved);
    return fn;
}

/* void __zan_list_push_strn(i8* lst, i8* p, i64 n): copy n bytes of p into a
 * fresh rc string and push it onto the List (growing the i64 data buffer). */
static LLVMValueRef get_list_push_strn_fn(zan_irgen_t *g) {
    LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "__zan_list_push_strn");
    if (fn) return fn;
    LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i64ptr = LLVMPointerType(i64, 0);
    LLVMTypeRef fnty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx),
        (LLVMTypeRef[]){ i8ptr, i8ptr, i64 }, 3, 0);
    fn = LLVMAddFunction(g->mod, "__zan_list_push_strn", fnty);
    LLVMSetLinkage(fn, LLVMInternalLinkage);
    LLVMTypeRef memcpy_ty = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr, i64 }, 3, 0);
    LLVMValueRef memcpy_fn = get_libc_fn(g, "memcpy", memcpy_ty);
    LLVMBasicBlockRef saved = LLVMGetInsertBlock(g->builder);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
    LLVMBasicBlockRef grow_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "grow");
    LLVMBasicBlockRef push_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "push");
    LLVMPositionBuilderAtEnd(g->builder, entry);
    LLVMValueRef lst = LLVMGetParam(fn, 0);
    LLVMValueRef p = LLVMGetParam(fn, 1);
    LLVMValueRef n = LLVMGetParam(fn, 2);
    LLVMValueRef one64 = LLVMConstInt(i64, 1, 0);
    /* fresh rc string with the segment bytes */
    LLVMValueRef ssz = LLVMBuildAdd(g->builder, n, one64, "ssz");
    LLVMValueRef str = emit_string_alloc_rc(g, ssz);
    zan_call2(g->builder, memcpy_ty, memcpy_fn,
        (LLVMValueRef[]){ str, p, n }, 3, "");
    LLVMValueRef endp = LLVMBuildGEP2(g->builder, i8, str, &n, 1, "endp");
    LLVMBuildStore(g->builder, LLVMConstInt(i8, 0, 0), endp);
    LLVMValueRef lp = LLVMBuildBitCast(g->builder, lst,
        LLVMPointerType(g->list_struct_type, 0), "lp");
    LLVMValueRef cntp = LLVMBuildStructGEP2(g->builder, g->list_struct_type, lp, 0, "cntp");
    LLVMValueRef count = LLVMBuildLoad2(g->builder, i64, cntp, "cnt");
    LLVMValueRef capp = LLVMBuildStructGEP2(g->builder, g->list_struct_type, lp, 1, "capp");
    LLVMValueRef cap = LLVMBuildLoad2(g->builder, i64, capp, "cap");
    LLVMValueRef full = LLVMBuildICmp(g->builder, LLVMIntSGE, count, cap, "full");
    LLVMBuildCondBr(g->builder, full, grow_bb, push_bb);
    LLVMPositionBuilderAtEnd(g->builder, grow_bb);
    LLVMValueRef cap0 = LLVMBuildICmp(g->builder, LLVMIntEQ, cap,
        LLVMConstInt(i64, 0, 0), "cap0");
    LLVMValueRef ncap = LLVMBuildSelect(g->builder, cap0,
        LLVMConstInt(i64, 4, 0),
        LLVMBuildMul(g->builder, cap, LLVMConstInt(i64, 2, 0), "cap2"), "ncap");
    LLVMValueRef datap = LLVMBuildStructGEP2(g->builder, g->list_struct_type, lp, 2, "datap");
    LLVMValueRef odata = LLVMBuildLoad2(g->builder, i64ptr, datap, "odata");
    LLVMValueRef odata8 = LLVMBuildBitCast(g->builder, odata, i8ptr, "odata8");
    LLVMValueRef nbytes = LLVMBuildMul(g->builder, ncap, LLVMConstInt(i64, 8, 0), "nbytes");
    LLVMValueRef ndata8 = zan_call2(g->builder,
        LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i64 }, 2, 0),
        g->fn_realloc, (LLVMValueRef[]){ odata8, nbytes }, 2, "ndata8");
    LLVMValueRef ndata = LLVMBuildBitCast(g->builder, ndata8, i64ptr, "ndata");
    LLVMBuildStore(g->builder, ndata, datap);
    LLVMBuildStore(g->builder, ncap, capp);
    LLVMBuildBr(g->builder, push_bb);
    LLVMPositionBuilderAtEnd(g->builder, push_bb);
    LLVMValueRef datap2 = LLVMBuildStructGEP2(g->builder, g->list_struct_type, lp, 2, "datap2");
    LLVMValueRef data = LLVMBuildLoad2(g->builder, i64ptr, datap2, "data");
    LLVMValueRef slot = LLVMBuildGEP2(g->builder, i64, data, &count, 1, "slot");
    LLVMBuildStore(g->builder, LLVMBuildPtrToInt(g->builder, str, i64, "sv"), slot);
    LLVMBuildStore(g->builder, LLVMBuildAdd(g->builder, count, one64, "ncnt"), cntp);
    LLVMBuildRetVoid(g->builder);
    if (saved) LLVMPositionBuilderAtEnd(g->builder, saved);
    return fn;
}

/* void __zan_str_split(i8* s, i8* sep, i8* lst): split s on every occurrence
 * of sep and push the segments (fresh rc strings) onto the List. An empty
 * separator yields the whole string as a single element. */
static LLVMValueRef get_str_split_fn(zan_irgen_t *g) {
    LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "__zan_str_split");
    if (fn) return fn;
    LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef fnty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx),
        (LLVMTypeRef[]){ i8ptr, i8ptr, i8ptr }, 3, 0);
    fn = LLVMAddFunction(g->mod, "__zan_str_split", fnty);
    LLVMSetLinkage(fn, LLVMInternalLinkage);
    LLVMTypeRef strlen_ty = LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr }, 1, 0);
    LLVMTypeRef strstr_ty = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0);
    LLVMValueRef strstr_fn = get_libc_fn(g, "strstr", strstr_ty);
    LLVMValueRef push_fn = get_list_push_strn_fn(g);
    LLVMTypeRef push_ty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx),
        (LLVMTypeRef[]){ i8ptr, i8ptr, i64 }, 3, 0);
    LLVMBasicBlockRef saved = LLVMGetInsertBlock(g->builder);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
    LLVMBasicBlockRef loop_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "loop");
    LLVMBasicBlockRef hit_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "hit");
    LLVMBasicBlockRef last_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "last");
    LLVMPositionBuilderAtEnd(g->builder, entry);
    LLVMValueRef s = LLVMGetParam(fn, 0);
    LLVMValueRef sep = LLVMGetParam(fn, 1);
    LLVMValueRef lst = LLVMGetParam(fn, 2);
    LLVMValueRef seplen = zan_call2(g->builder, strlen_ty, g->fn_strlen, &sep, 1, "seplen");
    LLVMValueRef pvar = LLVMBuildAlloca(g->builder, i8ptr, "pvar");
    LLVMBuildStore(g->builder, s, pvar);
    LLVMValueRef sep_empty = LLVMBuildICmp(g->builder, LLVMIntEQ, seplen,
        LLVMConstInt(i64, 0, 0), "sempty");
    LLVMBuildCondBr(g->builder, sep_empty, last_bb, loop_bb);
    LLVMPositionBuilderAtEnd(g->builder, loop_bb);
    LLVMValueRef cur = LLVMBuildLoad2(g->builder, i8ptr, pvar, "cur");
    LLVMValueRef hit = zan_call2(g->builder, strstr_ty, strstr_fn,
        (LLVMValueRef[]){ cur, sep }, 2, "hit");
    LLVMValueRef miss = LLVMBuildICmp(g->builder, LLVMIntEQ, hit,
        LLVMConstNull(i8ptr), "miss");
    LLVMBuildCondBr(g->builder, miss, last_bb, hit_bb);
    LLVMPositionBuilderAtEnd(g->builder, hit_bb);
    LLVMValueRef seg = LLVMBuildSub(g->builder,
        LLVMBuildPtrToInt(g->builder, hit, i64, "hi"),
        LLVMBuildPtrToInt(g->builder, cur, i64, "ci"), "seg");
    zan_call2(g->builder, push_ty, push_fn,
        (LLVMValueRef[]){ lst, cur, seg }, 3, "");
    LLVMValueRef next = LLVMBuildGEP2(g->builder, i8, hit, &seplen, 1, "next");
    LLVMBuildStore(g->builder, next, pvar);
    LLVMBuildBr(g->builder, loop_bb);
    LLVMPositionBuilderAtEnd(g->builder, last_bb);
    LLVMValueRef tp = LLVMBuildLoad2(g->builder, i8ptr, pvar, "tp");
    LLVMValueRef rest = zan_call2(g->builder, strlen_ty, g->fn_strlen, &tp, 1, "rest");
    zan_call2(g->builder, push_ty, push_fn,
        (LLVMValueRef[]){ lst, tp, rest }, 3, "");
    LLVMBuildRetVoid(g->builder);
    if (saved) LLVMPositionBuilderAtEnd(g->builder, saved);
    return fn;
}

/* i1 __zan_dict_key_eq(i8* a, i8* b, i64 is_str): key comparison for Dict
 * scans — strcmp equality for string keys, raw pointer/bit equality for
 * scalar keys (stored inttoptr'd in the i8** key slots). */
static LLVMValueRef get_dict_key_eq_fn(zan_irgen_t *g) {
    LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "__zan_dict_key_eq");
    if (fn) return fn;
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i1 = LLVMInt1TypeInContext(g->ctx);
    LLVMTypeRef fnty = LLVMFunctionType(i1, (LLVMTypeRef[]){ i8ptr, i8ptr, i64 }, 3, 0);
    fn = LLVMAddFunction(g->mod, "__zan_dict_key_eq", fnty);
    LLVMSetLinkage(fn, LLVMInternalLinkage);
    LLVMTypeRef strcmp_ty = LLVMFunctionType(i32t, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0);
    LLVMValueRef strcmp_fn = get_libc_fn(g, "strcmp", strcmp_ty);
    LLVMBasicBlockRef saved = LLVMGetInsertBlock(g->builder);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
    LLVMBasicBlockRef str_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "str");
    LLVMBasicBlockRef raw_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "raw");
    LLVMPositionBuilderAtEnd(g->builder, entry);
    LLVMValueRef a = LLVMGetParam(fn, 0);
    LLVMValueRef b2 = LLVMGetParam(fn, 1);
    LLVMValueRef is_str = LLVMGetParam(fn, 2);
    LLVMValueRef isv = LLVMBuildICmp(g->builder, LLVMIntNE, is_str,
        LLVMConstInt(i64, 0, 0), "isv");
    LLVMBuildCondBr(g->builder, isv, str_bb, raw_bb);
    LLVMPositionBuilderAtEnd(g->builder, str_bb);
    LLVMValueRef cmp = zan_call2(g->builder, strcmp_ty, strcmp_fn,
        (LLVMValueRef[]){ a, b2 }, 2, "cmp");
    LLVMBuildRet(g->builder, LLVMBuildICmp(g->builder, LLVMIntEQ, cmp,
        LLVMConstInt(i32t, 0, 0), "eq"));
    LLVMPositionBuilderAtEnd(g->builder, raw_bb);
    LLVMBuildRet(g->builder, LLVMBuildICmp(g->builder, LLVMIntEQ, a, b2, "peq"));
    if (saved) LLVMPositionBuilderAtEnd(g->builder, saved);
    return fn;
}

/* void __zan_dict_set(i8* draw, i8* key, i64 val, i64 is_str): upsert for the
 * Dict indexer. Replaces the value of an existing key, else appends the pair,
 * growing the parallel keys/values buffers when full. */
static LLVMValueRef get_dict_set_fn(zan_irgen_t *g) {
    LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "__zan_dict_set");
    if (fn) return fn;
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i64ptr = LLVMPointerType(i64, 0);
    LLVMTypeRef i8pp = LLVMPointerType(i8ptr, 0);
    LLVMTypeRef fnty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx),
        (LLVMTypeRef[]){ i8ptr, i8ptr, i64, i64 }, 4, 0);
    fn = LLVMAddFunction(g->mod, "__zan_dict_set", fnty);
    LLVMSetLinkage(fn, LLVMInternalLinkage);
    LLVMValueRef keq = get_dict_key_eq_fn(g);
    LLVMTypeRef keq_ty = LLVMGlobalGetValueType(keq);
    LLVMTypeRef realloc_ty = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i64 }, 2, 0);
    LLVMBasicBlockRef saved = LLVMGetInsertBlock(g->builder);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
    LLVMBasicBlockRef cond_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "cond");
    LLVMBasicBlockRef body_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "body");
    LLVMBasicBlockRef hit_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "hit");
    LLVMBasicBlockRef next_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "next");
    LLVMBasicBlockRef app_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "app");
    LLVMBasicBlockRef grow_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "grow");
    LLVMBasicBlockRef put_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "put");
    LLVMPositionBuilderAtEnd(g->builder, entry);
    LLVMValueRef draw = LLVMGetParam(fn, 0);
    LLVMValueRef key = LLVMGetParam(fn, 1);
    LLVMValueRef val = LLVMGetParam(fn, 2);
    LLVMValueRef is_str = LLVMGetParam(fn, 3);
    LLVMValueRef dp = LLVMBuildBitCast(g->builder, draw,
        LLVMPointerType(g->dict_struct_type, 0), "dp");
    LLVMValueRef cntp = LLVMBuildStructGEP2(g->builder, g->dict_struct_type, dp, 0, "cntp");
    LLVMValueRef cnt = LLVMBuildLoad2(g->builder, i64, cntp, "cnt");
    LLVMValueRef capp = LLVMBuildStructGEP2(g->builder, g->dict_struct_type, dp, 1, "capp");
    LLVMValueRef kp = LLVMBuildStructGEP2(g->builder, g->dict_struct_type, dp, 2, "kp");
    LLVMValueRef vp = LLVMBuildStructGEP2(g->builder, g->dict_struct_type, dp, 3, "vp");
    LLVMValueRef ks = LLVMBuildLoad2(g->builder, i8pp, kp, "ks");
    LLVMValueRef vs = LLVMBuildLoad2(g->builder, i64ptr, vp, "vs");
    LLVMValueRef idx_a = LLVMBuildAlloca(g->builder, i64, "di");
    LLVMBuildStore(g->builder, LLVMConstInt(i64, 0, 0), idx_a);
    LLVMBuildBr(g->builder, cond_bb);
    LLVMPositionBuilderAtEnd(g->builder, cond_bb);
    LLVMValueRef ci = LLVMBuildLoad2(g->builder, i64, idx_a, "ci");
    LLVMValueRef done = LLVMBuildICmp(g->builder, LLVMIntUGE, ci, cnt, "done");
    LLVMBuildCondBr(g->builder, done, app_bb, body_bb);
    LLVMPositionBuilderAtEnd(g->builder, body_bb);
    LLVMValueRef kslot = LLVMBuildGEP2(g->builder, i8ptr, ks, &ci, 1, "ksl");
    LLVMValueRef kv = LLVMBuildLoad2(g->builder, i8ptr, kslot, "kv");
    LLVMValueRef eq = zan_call2(g->builder, keq_ty, keq,
        (LLVMValueRef[]){ kv, key, is_str }, 3, "eq");
    LLVMBuildCondBr(g->builder, eq, hit_bb, next_bb);
    LLVMPositionBuilderAtEnd(g->builder, hit_bb);
    LLVMValueRef vslot = LLVMBuildGEP2(g->builder, i64, vs, &ci, 1, "vsl");
    LLVMBuildStore(g->builder, val, vslot);
    LLVMBuildRetVoid(g->builder);
    LLVMPositionBuilderAtEnd(g->builder, next_bb);
    LLVMBuildStore(g->builder,
        LLVMBuildAdd(g->builder, ci, LLVMConstInt(i64, 1, 0), "ni"), idx_a);
    LLVMBuildBr(g->builder, cond_bb);
    /* append: grow when full */
    LLVMPositionBuilderAtEnd(g->builder, app_bb);
    LLVMValueRef cap = LLVMBuildLoad2(g->builder, i64, capp, "cap");
    LLVMValueRef full = LLVMBuildICmp(g->builder, LLVMIntSGE, cnt, cap, "full");
    LLVMBuildCondBr(g->builder, full, grow_bb, put_bb);
    LLVMPositionBuilderAtEnd(g->builder, grow_bb);
    LLVMValueRef cap0 = LLVMBuildICmp(g->builder, LLVMIntEQ, cap,
        LLVMConstInt(i64, 0, 0), "cap0");
    LLVMValueRef ncap = LLVMBuildSelect(g->builder, cap0,
        LLVMConstInt(i64, 16, 0),
        LLVMBuildMul(g->builder, cap, LLVMConstInt(i64, 2, 0), "cap2"), "ncap");
    LLVMValueRef nbytes = LLVMBuildMul(g->builder, ncap, LLVMConstInt(i64, 8, 0), "nbytes");
    LLVMValueRef ks8 = LLVMBuildBitCast(g->builder, ks, i8ptr, "ks8");
    LLVMValueRef nks8 = zan_call2(g->builder, realloc_ty, g->fn_realloc,
        (LLVMValueRef[]){ ks8, nbytes }, 2, "nks8");
    LLVMBuildStore(g->builder, LLVMBuildBitCast(g->builder, nks8, i8pp, "nks"), kp);
    LLVMValueRef vs8 = LLVMBuildBitCast(g->builder, vs, i8ptr, "vs8");
    LLVMValueRef nvs8 = zan_call2(g->builder, realloc_ty, g->fn_realloc,
        (LLVMValueRef[]){ vs8, nbytes }, 2, "nvs8");
    LLVMBuildStore(g->builder, LLVMBuildBitCast(g->builder, nvs8, i64ptr, "nvs"), vp);
    LLVMBuildStore(g->builder, ncap, capp);
    LLVMBuildBr(g->builder, put_bb);
    LLVMPositionBuilderAtEnd(g->builder, put_bb);
    LLVMValueRef ks2 = LLVMBuildLoad2(g->builder, i8pp, kp, "ks2");
    LLVMValueRef vs2 = LLVMBuildLoad2(g->builder, i64ptr, vp, "vs2");
    LLVMValueRef kslot2 = LLVMBuildGEP2(g->builder, i8ptr, ks2, &cnt, 1, "ksl2");
    LLVMBuildStore(g->builder, key, kslot2);
    LLVMValueRef vslot2 = LLVMBuildGEP2(g->builder, i64, vs2, &cnt, 1, "vsl2");
    LLVMBuildStore(g->builder, val, vslot2);
    LLVMBuildStore(g->builder,
        LLVMBuildAdd(g->builder, cnt, LLVMConstInt(i64, 1, 0), "ncnt"), cntp);
    LLVMBuildRetVoid(g->builder);
    if (saved) LLVMPositionBuilderAtEnd(g->builder, saved);
    return fn;
}

/* Exception-handling globals: a jmp_buf stack (16 deep, 1024 bytes each,
 * covering every supported target's jmp_buf), its top index (-1 = empty),
 * and the in-flight exception object. */
static void get_eh_globals(zan_irgen_t *g, LLVMValueRef *top,
                           LLVMValueRef *bufs, LLVMValueRef *exc) {
    LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
    LLVMValueRef t = LLVMGetNamedGlobal(g->mod, "__zan_eh_top");
    if (!t) {
        t = LLVMAddGlobal(g->mod, i32t, "__zan_eh_top");
        LLVMSetLinkage(t, LLVMInternalLinkage);
        LLVMSetInitializer(t, LLVMConstInt(i32t, (unsigned long long)-1, 1));
    }
    LLVMValueRef b = LLVMGetNamedGlobal(g->mod, "__zan_eh_bufs");
    if (!b) {
        LLVMTypeRef buf_ty = LLVMArrayType(i8, 1024);
        LLVMTypeRef arr_ty = LLVMArrayType(buf_ty, 16);
        b = LLVMAddGlobal(g->mod, arr_ty, "__zan_eh_bufs");
        LLVMSetLinkage(b, LLVMInternalLinkage);
        LLVMSetInitializer(b, LLVMConstNull(arr_ty));
    }
    LLVMValueRef e = LLVMGetNamedGlobal(g->mod, "__zan_eh_exc");
    if (!e) {
        e = LLVMAddGlobal(g->mod, i8ptr, "__zan_eh_exc");
        LLVMSetLinkage(e, LLVMInternalLinkage);
        LLVMSetInitializer(e, LLVMConstNull(i8ptr));
    }
    *top = t; *bufs = b; *exc = e;
}

/* i32 setjmp on the current target: `_setjmp(buf, NULL)` on Windows (frame
 * NULL disables SEH unwinding), `_setjmp(buf)` elsewhere (no sigmask save). */
static LLVMValueRef emit_eh_setjmp(zan_irgen_t *g, LLVMValueRef bufp) {
    LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    if (g->target_is_windows) {
        LLVMTypeRef ty = LLVMFunctionType(i32t, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0);
        LLVMValueRef fn = get_libc_fn(g, "_setjmp", ty);
        LLVMValueRef call = zan_call2(g->builder, ty, fn,
            (LLVMValueRef[]){ bufp, LLVMConstNull(i8ptr) }, 2, "sj");
        return call;
    }
    LLVMTypeRef ty = LLVMFunctionType(i32t, (LLVMTypeRef[]){ i8ptr }, 1, 0);
    LLVMValueRef fn = get_libc_fn(g, "_setjmp", ty);
    return zan_call2(g->builder, ty, fn, &bufp, 1, "sj");
}

static void emit_eh_longjmp(zan_irgen_t *g, LLVMValueRef bufp) {
    LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef ty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx),
        (LLVMTypeRef[]){ i8ptr, i32t }, 2, 0);
    LLVMValueRef fn = get_libc_fn(g, "longjmp", ty);
    zan_call2(g->builder, ty, fn,
        (LLVMValueRef[]){ bufp, LLVMConstInt(i32t, 1, 0) }, 2, "");
}

/* i8* __zan_str_join(i8* sep, i8* lst): join a List<string>'s elements with
 * `sep` into a fresh RC string (two passes: measure, then copy). */
static LLVMValueRef get_str_join_fn(zan_irgen_t *g) {
    LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "__zan_str_join");
    if (fn) return fn;
    LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i64ptr = LLVMPointerType(i64, 0);
    LLVMTypeRef fnty = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0);
    fn = LLVMAddFunction(g->mod, "__zan_str_join", fnty);
    LLVMSetLinkage(fn, LLVMInternalLinkage);
    LLVMTypeRef strlen_ty = LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr }, 1, 0);
    LLVMTypeRef memcpy_ty = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr, i64 }, 3, 0);
    LLVMValueRef memcpy_fn = LLVMGetNamedFunction(g->mod, "memcpy");
    if (!memcpy_fn) memcpy_fn = LLVMAddFunction(g->mod, "memcpy", memcpy_ty);
    LLVMBasicBlockRef saved = LLVMGetInsertBlock(g->builder);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
    LLVMBasicBlockRef m_cond = LLVMAppendBasicBlockInContext(g->ctx, fn, "m.cond");
    LLVMBasicBlockRef m_body = LLVMAppendBasicBlockInContext(g->ctx, fn, "m.body");
    LLVMBasicBlockRef c_pre = LLVMAppendBasicBlockInContext(g->ctx, fn, "c.pre");
    LLVMBasicBlockRef c_cond = LLVMAppendBasicBlockInContext(g->ctx, fn, "c.cond");
    LLVMBasicBlockRef c_body = LLVMAppendBasicBlockInContext(g->ctx, fn, "c.body");
    LLVMBasicBlockRef c_sep = LLVMAppendBasicBlockInContext(g->ctx, fn, "c.sep");
    LLVMBasicBlockRef c_next = LLVMAppendBasicBlockInContext(g->ctx, fn, "c.next");
    LLVMBasicBlockRef done = LLVMAppendBasicBlockInContext(g->ctx, fn, "done");
    LLVMPositionBuilderAtEnd(g->builder, entry);
    LLVMValueRef sep = LLVMGetParam(fn, 0);
    LLVMValueRef lraw = LLVMGetParam(fn, 1);
    LLVMValueRef lp = LLVMBuildBitCast(g->builder, lraw,
        LLVMPointerType(g->list_struct_type, 0), "lp");
    LLVMValueRef cntp = LLVMBuildStructGEP2(g->builder, g->list_struct_type, lp, 0, "cntp");
    LLVMValueRef n = LLVMBuildLoad2(g->builder, i64, cntp, "n");
    LLVMValueRef dpp = LLVMBuildStructGEP2(g->builder, g->list_struct_type, lp, 2, "dpp");
    LLVMValueRef data = LLVMBuildLoad2(g->builder, i64ptr, dpp, "data");
    LLVMValueRef seplen = zan_call2(g->builder, strlen_ty, g->fn_strlen, &sep, 1, "seplen");
    LLVMValueRef idx_a = LLVMBuildAlloca(g->builder, i64, "i");
    LLVMValueRef tot_a = LLVMBuildAlloca(g->builder, i64, "tot");
    LLVMValueRef pos_a = LLVMBuildAlloca(g->builder, i64, "pos");
    LLVMBuildStore(g->builder, LLVMConstInt(i64, 0, 0), idx_a);
    /* total starts at seplen*(n-1), clamped at 0 for empty lists */
    LLVMValueRef nz = LLVMBuildICmp(g->builder, LLVMIntSGT, n, LLVMConstInt(i64, 0, 0), "nz");
    LLVMValueRef nm1 = LLVMBuildSub(g->builder, n, LLVMConstInt(i64, 1, 0), "nm1");
    LLVMValueRef sepsum = LLVMBuildSelect(g->builder, nz,
        LLVMBuildMul(g->builder, seplen, nm1, "ss"), LLVMConstInt(i64, 0, 0), "sepsum");
    LLVMBuildStore(g->builder, sepsum, tot_a);
    LLVMBuildBr(g->builder, m_cond);
    LLVMPositionBuilderAtEnd(g->builder, m_cond);
    LLVMValueRef mi = LLVMBuildLoad2(g->builder, i64, idx_a, "mi");
    LLVMBuildCondBr(g->builder,
        LLVMBuildICmp(g->builder, LLVMIntSLT, mi, n, "mlt"), m_body, c_pre);
    LLVMPositionBuilderAtEnd(g->builder, m_body);
    LLVMValueRef slot = LLVMBuildGEP2(g->builder, i64, data, &mi, 1, "slot");
    LLVMValueRef sv = LLVMBuildIntToPtr(g->builder,
        LLVMBuildLoad2(g->builder, i64, slot, "svi"), i8ptr, "sv");
    LLVMValueRef sl = zan_call2(g->builder, strlen_ty, g->fn_strlen, &sv, 1, "sl");
    LLVMBuildStore(g->builder,
        LLVMBuildAdd(g->builder, LLVMBuildLoad2(g->builder, i64, tot_a, "t0"), sl, "t1"), tot_a);
    LLVMBuildStore(g->builder,
        LLVMBuildAdd(g->builder, mi, LLVMConstInt(i64, 1, 0), "mi1"), idx_a);
    LLVMBuildBr(g->builder, m_cond);
    LLVMPositionBuilderAtEnd(g->builder, c_pre);
    LLVMValueRef total = LLVMBuildLoad2(g->builder, i64, tot_a, "total");
    LLVMValueRef buf = emit_string_alloc_rc(g,
        LLVMBuildAdd(g->builder, total, LLVMConstInt(i64, 1, 0), "tot1"));
    LLVMBuildStore(g->builder, LLVMConstInt(i64, 0, 0), idx_a);
    LLVMBuildStore(g->builder, LLVMConstInt(i64, 0, 0), pos_a);
    LLVMBuildBr(g->builder, c_cond);
    LLVMPositionBuilderAtEnd(g->builder, c_cond);
    LLVMValueRef ci = LLVMBuildLoad2(g->builder, i64, idx_a, "ci");
    LLVMBuildCondBr(g->builder,
        LLVMBuildICmp(g->builder, LLVMIntSLT, ci, n, "clt"), c_body, done);
    LLVMPositionBuilderAtEnd(g->builder, c_body);
    LLVMValueRef slot2 = LLVMBuildGEP2(g->builder, i64, data, &ci, 1, "slot2");
    LLVMValueRef sv2 = LLVMBuildIntToPtr(g->builder,
        LLVMBuildLoad2(g->builder, i64, slot2, "svi2"), i8ptr, "sv2");
    LLVMValueRef sl2 = zan_call2(g->builder, strlen_ty, g->fn_strlen, &sv2, 1, "sl2");
    LLVMValueRef pos = LLVMBuildLoad2(g->builder, i64, pos_a, "pos");
    LLVMValueRef dst = LLVMBuildGEP2(g->builder, i8, buf, &pos, 1, "dst");
    zan_call2(g->builder, memcpy_ty, memcpy_fn,
        (LLVMValueRef[]){ dst, sv2, sl2 }, 3, "");
    LLVMValueRef pos2 = LLVMBuildAdd(g->builder, pos, sl2, "pos2");
    LLVMBuildStore(g->builder, pos2, pos_a);
    LLVMValueRef last = LLVMBuildICmp(g->builder, LLVMIntSGE,
        LLVMBuildAdd(g->builder, ci, LLVMConstInt(i64, 1, 0), "ci1"), n, "last");
    LLVMBuildCondBr(g->builder, last, c_next, c_sep);
    LLVMPositionBuilderAtEnd(g->builder, c_sep);
    LLVMValueRef dst2 = LLVMBuildGEP2(g->builder, i8, buf, &pos2, 1, "dst2");
    zan_call2(g->builder, memcpy_ty, memcpy_fn,
        (LLVMValueRef[]){ dst2, sep, seplen }, 3, "");
    LLVMBuildStore(g->builder,
        LLVMBuildAdd(g->builder, pos2, seplen, "pos3"), pos_a);
    LLVMBuildBr(g->builder, c_next);
    LLVMPositionBuilderAtEnd(g->builder, c_next);
    LLVMBuildStore(g->builder,
        LLVMBuildAdd(g->builder, ci, LLVMConstInt(i64, 1, 0), "ni"), idx_a);
    LLVMBuildBr(g->builder, c_cond);
    LLVMPositionBuilderAtEnd(g->builder, done);
    LLVMValueRef endpos = LLVMBuildLoad2(g->builder, i64, pos_a, "endpos");
    LLVMValueRef endptr = LLVMBuildGEP2(g->builder, i8, buf, &endpos, 1, "endptr");
    LLVMBuildStore(g->builder, LLVMConstInt(i8, 0, 0), endptr);
    LLVMBuildRet(g->builder, buf);
    if (saved) LLVMPositionBuilderAtEnd(g->builder, saved);
    return fn;
}

/* Coerce a Dict key value to the i8* slot representation (scalar keys are
 * stored inttoptr'd; string keys are already pointers). */
static LLVMValueRef coerce_dict_key(zan_irgen_t *g, LLVMValueRef key) {
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    if (LLVMGetTypeKind(LLVMTypeOf(key)) == LLVMIntegerTypeKind) {
        if (LLVMGetIntTypeWidth(LLVMTypeOf(key)) < 64)
            key = LLVMBuildSExt(g->builder, key, i64, "k.sx");
        return LLVMBuildIntToPtr(g->builder, key, i8ptr, "k.ip");
    }
    if (LLVMTypeOf(key) != i8ptr)
        return LLVMBuildBitCast(g->builder, key, i8ptr, "k.bc");
    return key;
}

/* Emit `keys[i] == search` for a Dict scan via __zan_dict_key_eq. */
static LLVMValueRef emit_dict_key_eq(zan_irgen_t *g, zan_type_t *dict_type,
                                     LLVMValueRef kv, LLVMValueRef search) {
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    zan_type_t *kt = dict_key_type(g, dict_type);
    LLVMValueRef is_str = LLVMConstInt(i64,
        (!kt || kt->kind == TYPE_STRING) ? 1 : 0, 0);
    LLVMValueRef keq = get_dict_key_eq_fn(g);
    return zan_call2(g->builder, LLVMGlobalGetValueType(keq), keq,
        (LLVMValueRef[]){ kv, search, is_str }, 3, "keq");
}

/* Backing LLVM global for a static class/struct field `Class.field`.
 * Static fields are shared mutable storage (not compile-time constants):
 * they are lowered to an internal, zero-initialised module global. The
 * field's declared initializer is applied once at main() entry as a runtime
 * store (see emit_main_method), so any initializer expression works with
 * correct ordering. Returns NULL when `fsym` is not a static field. */
static LLVMValueRef get_static_field_global(zan_irgen_t *g, zan_symbol_t *class_sym,
                                            zan_symbol_t *fsym) {
    if (!class_sym || !fsym || !fsym->decl || fsym->decl->kind != AST_FIELD_DECL)
        return NULL;
    if (!((fsym->modifiers & MOD_STATIC) ||
          (fsym->decl->field_decl.modifiers & MOD_STATIC)))
        return NULL;
    char name[512];
    snprintf(name, sizeof(name), "__zan_sf_%.*s_%.*s",
             (int)class_sym->name.len, class_sym->name.str,
             (int)fsym->name.len, fsym->name.str);
    LLVMValueRef gv = LLVMGetNamedGlobal(g->mod, name);
    if (gv) return gv;
    LLVMTypeRef ft = fsym->type ? map_type(g, fsym->type)
                                : LLVMInt64TypeInContext(g->ctx);
    gv = LLVMAddGlobal(g->mod, ft, name);
    LLVMSetLinkage(gv, LLVMInternalLinkage);
    LLVMSetInitializer(gv, LLVMConstNull(ft));
    return gv;
}

static int is_stable_field_base(zan_ast_node_t *expr) {
    if (!expr) return 0;
    if (expr->kind == AST_IDENTIFIER || expr->kind == AST_THIS_EXPR ||
        expr->kind == AST_BASE_EXPR) return 1;
    return expr->kind == AST_MEMBER_ACCESS &&
           is_stable_field_base(expr->member.object);
}

static void emit_invalidate_freed_string(zan_irgen_t *g, zan_ast_node_t *arg,
                                         local_scope_t *locals) {
    if (!arg || !locals) return;
    if (arg->kind == AST_IDENTIFIER) {
        local_var_t *local = local_find(locals, arg->ident.name);
        if (local && local->type && local->type->kind == TYPE_STRING) {
            LLVMTypeRef slot_type = local_slot_type(g, local);
            if (LLVMGetTypeKind(slot_type) == LLVMPointerTypeKind) {
                LLVMBuildStore(g->builder, LLVMConstNull(slot_type), local->alloca);
            }
            return;
        }
        if (g->current_type_sym) {
            zan_symbol_t *field = get_field_sym(g->current_type_sym, arg->ident.name);
            if (!field || !field->type || field->type->kind != TYPE_STRING) return;
            LLVMValueRef global = get_static_field_global(g, g->current_type_sym, field);
            LLVMTypeRef field_type = map_type(g, field->type);
            if (global) {
                LLVMBuildStore(g->builder, LLVMConstNull(field_type), global);
            } else if (g->current_this) {
                int fi = get_field_index(g->current_type_sym, arg->ident.name);
                LLVMTypeRef st = get_struct_llvm_type(g, g->current_type_sym);
                if (fi >= 0 && st) {
                    LLVMValueRef this_ptr = LLVMBuildLoad2(g->builder,
                        LLVMPointerType(st, 0), g->current_this, "this");
                    LLVMValueRef field_ptr = LLVMBuildStructGEP2(g->builder, st,
                        this_ptr, (unsigned)fi, "freed.fld");
                    LLVMBuildStore(g->builder, LLVMConstNull(field_type), field_ptr);
                }
            }
        }
        return;
    }
    if (arg->kind != AST_MEMBER_ACCESS) return;
    zan_type_t *field_type = infer_expr_type(g, arg, locals);
    if (!field_type || field_type->kind != TYPE_STRING) return;
    zan_ast_node_t *obj = arg->member.object;
    zan_symbol_t *class_sym = NULL;
    LLVMValueRef object_ptr = NULL;
    if (obj->kind == AST_IDENTIFIER) {
        local_var_t *local = local_find(locals, obj->ident.name);
        if (local && local->type && local->type->sym) {
            class_sym = local->type->sym;
            LLVMTypeRef st = get_struct_llvm_type(g, class_sym);
            if (st) object_ptr = struct_base_ptr(g, local, st);
        } else {
            class_sym = zan_binder_lookup(g->binder, obj->ident.name);
            if (class_sym && (class_sym->kind == SYM_CLASS ||
                              class_sym->kind == SYM_STRUCT)) {
                zan_symbol_t *field = get_field_sym(class_sym, arg->member.name);
                LLVMValueRef global = get_static_field_global(g, class_sym, field);
                if (global) {
                    LLVMBuildStore(g->builder,
                        LLVMConstNull(map_type(g, field_type)), global);
                    return;
                }
            }
            class_sym = NULL;
        }
    } else if ((obj->kind == AST_THIS_EXPR || obj->kind == AST_BASE_EXPR) &&
               g->current_type_sym && g->current_this) {
        class_sym = g->current_type_sym;
        LLVMTypeRef st = get_struct_llvm_type(g, class_sym);
        if (st) {
            object_ptr = LLVMBuildLoad2(g->builder, LLVMPointerType(st, 0),
                                       g->current_this, "this");
        }
    } else if (is_stable_field_base(obj)) {
        class_sym = expr_class_sym(g, obj, locals);
        if (class_sym) object_ptr = emit_expr(g, obj, locals);
    }
    if (!class_sym || !object_ptr ||
        LLVMGetTypeKind(LLVMTypeOf(object_ptr)) != LLVMPointerTypeKind) return;
    int fi = get_field_index(class_sym, arg->member.name);
    LLVMTypeRef st = get_struct_llvm_type(g, class_sym);
    if (fi < 0 || !st) return;
    LLVMValueRef field_ptr = LLVMBuildStructGEP2(g->builder, st, object_ptr,
                                                 (unsigned)fi, "freed.fld");
    LLVMBuildStore(g->builder, LLVMConstNull(map_type(g, field_type)), field_ptr);
}

/* Null-conditional access `a?.b` / `a?.M(...)`: evaluate the receiver once,
 * bind it to a synthetic local, and only evaluate the member/call when it is
 * non-null; a null receiver yields the result type's zero value. The member
 * node is temporarily rewired to read the synthetic local so the ordinary
 * member/call codegen paths apply unchanged, then restored (the same AST may
 * be re-emitted, e.g. per generic instantiation). */
static LLVMValueRef emit_null_cond(zan_irgen_t *g, zan_ast_node_t *expr,
                                   zan_ast_node_t *qmem, local_scope_t *locals) {
    LLVMValueRef obj = emit_expr(g, qmem->member.object, locals);
    if (LLVMGetTypeKind(LLVMTypeOf(obj)) != LLVMPointerTypeKind) {
        /* value receiver can never be null: plain access */
        qmem->member.null_cond = 0;
        LLVMValueRef v = emit_expr(g, expr, locals);
        qmem->member.null_cond = 1;
        return v;
    }
    zan_type_t *oty = infer_expr_type(g, qmem->member.object, locals);
    char nm[32];
    snprintf(nm, sizeof(nm), "__qdot%d", g->qdot_counter++);
    char *nmp = (char *)zan_arena_alloc(g->arena, strlen(nm) + 1);
    strcpy(nmp, nm);
    zan_istr_t iname = { nmp, (int)strlen(nmp) };
    LLVMValueRef slot = emit_entry_alloca(g, LLVMTypeOf(obj), nm);
    LLVMBuildStore(g->builder, obj, slot);
    local_add(locals, iname, slot, oty);

    zan_ast_node_t *ident = zan_ast_new(g->arena, AST_IDENTIFIER, qmem->loc);
    ident->ident.name = iname;
    zan_ast_node_t *saved_obj = qmem->member.object;
    qmem->member.object = ident;
    qmem->member.null_cond = 0;

    LLVMValueRef fn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder));
    LLVMBasicBlockRef then_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "qdot.then");
    LLVMBasicBlockRef merge_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "qdot.end");
    LLVMValueRef isnull = LLVMBuildICmp(g->builder, LLVMIntEQ, obj,
        LLVMConstNull(LLVMTypeOf(obj)), "qdot.isnull");
    LLVMBasicBlockRef entry_bb = LLVMGetInsertBlock(g->builder);
    LLVMBuildCondBr(g->builder, isnull, merge_bb, then_bb);

    LLVMPositionBuilderAtEnd(g->builder, then_bb);
    LLVMValueRef v = emit_expr(g, expr, locals);
    LLVMBasicBlockRef then_end = LLVMGetInsertBlock(g->builder);
    LLVMBuildBr(g->builder, merge_bb);

    qmem->member.object = saved_obj;
    qmem->member.null_cond = 1;

    LLVMPositionBuilderAtEnd(g->builder, merge_bb);
    LLVMTypeRef vt = LLVMTypeOf(v);
    if (LLVMGetTypeKind(vt) == LLVMVoidTypeKind) {
        emit_release_owned_call_temp(g, saved_obj, obj, locals);
        return LLVMConstInt(LLVMInt64TypeInContext(g->ctx), 0, 0);
    }
    LLVMValueRef phi = LLVMBuildPhi(g->builder, vt, "qdot");
    LLVMValueRef dflt = LLVMConstNull(vt);
    LLVMValueRef vals[] = { dflt, v };
    LLVMBasicBlockRef bbs[] = { entry_bb, then_end };
    LLVMAddIncoming(phi, vals, bbs, 2);
    /* an owned receiver temp (e.g. `M()?.x`, where M returns +1) is consumed
     * by this access; the class release helper is null-safe */
    emit_release_owned_call_temp(g, saved_obj, obj, locals);
    return phi;
}

/* `params T[] rest` call-site packing. The trailing arguments are bundled into
 * a synthetic `new List<T>{ ... }` node (the parameter itself was lowered to
 * List<T> at parse time), so the callee sees a normal List. Passing a List
 * (or an explicit `new List<T>{...}`) as the sole trailing argument passes it
 * through unpacked. The call node is mutated in place; the rewrite is
 * idempotent so re-emission (e.g. per generic instantiation) is safe. */
static void pack_params_args(zan_irgen_t *g, zan_ast_node_t *call,
                             zan_symbol_t *method_sym, local_scope_t *locals) {
    if (!method_sym || !method_sym->decl ||
        method_sym->decl->kind != AST_METHOD_DECL) return;
    if (!method_is_params_variadic(method_sym)) return;
    zan_ast_list_t *ps = &method_sym->decl->method_decl.params;
    int fixed = ps->count - 1;
    int argc = call->call.args.count;
    if (argc < fixed) return;
    if (argc == ps->count) {
        zan_ast_node_t *la = call->call.args.items[argc - 1];
        if (la->kind == AST_NEW_EXPR) return; /* already packed / explicit list */
        zan_type_t *lt = infer_expr_type(g, la, locals);
        if (lt && lt->name.len == 4 && memcmp(lt->name.str, "List", 4) == 0) return;
    }
    zan_ast_node_t *last_p = ps->items[ps->count - 1];
    zan_ast_node_t *lst = zan_ast_new(g->arena, AST_NEW_EXPR, call->loc);
    lst->new_expr.type = last_p->param.type; /* List<T> */
    lst->new_expr.is_array = false;
    zan_ast_list_init(&lst->new_expr.args);
    for (int i = fixed; i < argc; i++)
        zan_ast_list_push(&lst->new_expr.args, call->call.args.items[i], g->arena);
    call->call.args.count = fixed;
    zan_ast_list_push(&call->call.args, lst, g->arena);
}

static LLVMValueRef emit_ref_arg(zan_irgen_t *g, zan_ast_node_t *arg,
                                 local_scope_t *locals);

/* ===== NativeMemory intrinsics =====================================
 * Raw off-heap memory for binary IO. An address is a plain nint (i64) that
 * never enters ARC. Every accessor lowers to direct (align-1) loads/stores or
 * a single libc call, so byte-level hot loops in Zan code (page encoding,
 * checksums, network framing) cost what the equivalent C would.
 *
 *   nint  Alloc(int size)                     zeroed malloc
 *   void  Free(nint p)
 *   void  Copy(nint dst, nint src, int n)     memmove (overlap-safe)
 *   void  Fill(nint p, int byte, int n)       memset
 *   int   Compare(nint a, nint b, int n)      memcmp
 *   int   GetByte/GetU16/GetU32/GetI64(nint p, int off)   little-endian
 *   void  SetByte/SetU16/SetU32/SetI64(nint p, int off, int v)
 *   string GetString(nint p, int off, int len)  copies into a real ARC string
 *   void  PutString(nint p, int off, string s, int len)
 *   int   Crc32(nint p, int len)              hardware-independent CRC32 (IEEE)
 */

/* i8* pointer to (base + off); base/off are i64 values. */
static LLVMValueRef nm_addr(zan_irgen_t *g, LLVMValueRef base, LLVMValueRef off) {
    LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
    LLVMValueRef p = LLVMBuildIntToPtr(g->builder, base, i8ptr, "nm.p");
    return LLVMBuildGEP2(g->builder, i8, p, &off, 1, "nm.at");
}

/* Widen/narrow an emitted argument to i64 (args arrive as iN or pointer). */
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

/* Little-endian load of `bits` at (p+off), zero-extended to i64 (i64 loads
 * are returned as-is). align 1: page/wire offsets are arbitrary. */
static LLVMValueRef nm_load(zan_irgen_t *g, LLVMValueRef base, LLVMValueRef off, int bits) {
    LLVMTypeRef it = LLVMIntTypeInContext(g->ctx, (unsigned)bits);
    LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
    LLVMValueRef ptr = LLVMBuildBitCast(g->builder, nm_addr(g, base, off),
        LLVMPointerType(it, 0), "nm.lp");
    LLVMValueRef v = LLVMBuildLoad2(g->builder, it, ptr, "nm.ld");
    LLVMSetAlignment(v, 1);
    if (bits < 64) v = LLVMBuildZExt(g->builder, v, i64t, "nm.zx");
    return v;
}

static void nm_store(zan_irgen_t *g, LLVMValueRef base, LLVMValueRef off,
                     LLVMValueRef val, int bits) {
    LLVMTypeRef it = LLVMIntTypeInContext(g->ctx, (unsigned)bits);
    LLVMValueRef ptr = LLVMBuildBitCast(g->builder, nm_addr(g, base, off),
        LLVMPointerType(it, 0), "nm.sp");
    if (bits < 64) val = LLVMBuildTrunc(g->builder, val, it, "nm.tr");
    LLVMValueRef st = LLVMBuildStore(g->builder, val, ptr);
    LLVMSetAlignment(st, 1);
}

/* __zan_nm_crc32(i8*, i64) -> i64: CRC32 (IEEE 802.3, reflected polynomial
 * 0xEDB88320), table-driven, one table lookup per byte. The 256-entry table
 * is computed at compile time and emitted as a constant global, so the
 * function is self-contained (no runtime object to link). */
static LLVMValueRef nm_crc32_fn(zan_irgen_t *g) {
    LLVMValueRef fn = LLVMGetNamedFunction(g->mod, "__zan_nm_crc32");
    if (fn) return fn;
    LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
    LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef fnty = LLVMFunctionType(i64t, (LLVMTypeRef[]){ i8ptr, i64t }, 2, 0);
    fn = LLVMAddFunction(g->mod, "__zan_nm_crc32", fnty);
    LLVMSetLinkage(fn, LLVMInternalLinkage);

    LLVMValueRef entries[256];
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int k = 0; k < 8; k++)
            c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        entries[i] = LLVMConstInt(i32t, c, 0);
    }
    LLVMTypeRef tab_ty = LLVMArrayType(i32t, 256);
    LLVMValueRef tab = LLVMAddGlobal(g->mod, tab_ty, "__zan_crc32_table");
    LLVMSetInitializer(tab, LLVMConstArray(i32t, entries, 256));
    LLVMSetGlobalConstant(tab, 1);
    LLVMSetLinkage(tab, LLVMInternalLinkage);

    LLVMBasicBlockRef saved = LLVMGetInsertBlock(g->builder);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
    LLVMBasicBlockRef loop = LLVMAppendBasicBlockInContext(g->ctx, fn, "loop");
    LLVMBasicBlockRef body = LLVMAppendBasicBlockInContext(g->ctx, fn, "body");
    LLVMBasicBlockRef done = LLVMAppendBasicBlockInContext(g->ctx, fn, "done");
    LLVMPositionBuilderAtEnd(g->builder, entry);
    LLVMValueRef data = LLVMGetParam(fn, 0);
    LLVMValueRef len = LLVMGetParam(fn, 1);
    LLVMBuildBr(g->builder, loop);

    LLVMPositionBuilderAtEnd(g->builder, loop);
    LLVMValueRef idx = LLVMBuildPhi(g->builder, i64t, "i");
    LLVMValueRef crc = LLVMBuildPhi(g->builder, i32t, "crc");
    LLVMValueRef in_range = LLVMBuildICmp(g->builder, LLVMIntSLT, idx, len, "inrange");
    LLVMBuildCondBr(g->builder, in_range, body, done);

    LLVMPositionBuilderAtEnd(g->builder, body);
    LLVMValueRef bp = LLVMBuildGEP2(g->builder, i8, data, &idx, 1, "bp");
    LLVMValueRef byte = LLVMBuildZExt(g->builder,
        LLVMBuildLoad2(g->builder, i8, bp, "b"), i32t, "b32");
    LLVMValueRef ti = LLVMBuildAnd(g->builder,
        LLVMBuildXor(g->builder, crc, byte, "x"),
        LLVMConstInt(i32t, 0xFF, 0), "ti");
    LLVMValueRef ti64 = LLVMBuildZExt(g->builder, ti, i64t, "ti64");
    LLVMValueRef gep_idx[] = { LLVMConstInt(i64t, 0, 0), ti64 };
    LLVMValueRef ep = LLVMBuildGEP2(g->builder, tab_ty, tab, gep_idx, 2, "ep");
    LLVMValueRef te = LLVMBuildLoad2(g->builder, i32t, ep, "te");
    LLVMValueRef next_crc = LLVMBuildXor(g->builder, te,
        LLVMBuildLShr(g->builder, crc, LLVMConstInt(i32t, 8, 0), "sh"), "nc");
    LLVMValueRef next_idx = LLVMBuildAdd(g->builder, idx, LLVMConstInt(i64t, 1, 0), "ni");
    LLVMBuildBr(g->builder, loop);

    LLVMAddIncoming(idx, (LLVMValueRef[]){ LLVMConstInt(i64t, 0, 0), next_idx },
        (LLVMBasicBlockRef[]){ entry, body }, 2);
    LLVMAddIncoming(crc, (LLVMValueRef[]){ LLVMConstInt(i32t, 0xFFFFFFFFu, 0), next_crc },
        (LLVMBasicBlockRef[]){ entry, body }, 2);

    LLVMPositionBuilderAtEnd(g->builder, done);
    LLVMValueRef fin = LLVMBuildXor(g->builder, crc,
        LLVMConstInt(i32t, 0xFFFFFFFFu, 0), "fin");
    LLVMBuildRet(g->builder, LLVMBuildZExt(g->builder, fin, i64t, "fin64"));
    if (saved) LLVMPositionBuilderAtEnd(g->builder, saved);
    return fn;
}

static bool emit_native_memory_call(zan_irgen_t *g, zan_ast_node_t *expr,
                                    local_scope_t *locals, LLVMValueRef *out) {
    LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
    LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
    LLVMValueRef zero64 = LLVMConstInt(i64t, 0, 0);

    if (is_call_to(expr, "NativeMemory", "Alloc") && expr->call.args.count == 1) {
        LLVMValueRef size = nm_arg(g, expr, 0, locals);
        LLVMTypeRef ty = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i64t, i64t }, 2, 0);
        LLVMValueRef fn = get_libc_fn(g, "calloc", ty);
        LLVMValueRef p = zan_call2(g->builder, ty, fn,
            (LLVMValueRef[]){ size, LLVMConstInt(i64t, 1, 0) }, 2, "nm.alloc");
        *out = LLVMBuildPtrToInt(g->builder, p, i64t, "nm.addr");
        return true;
    }
    if (is_call_to(expr, "NativeMemory", "Free") && expr->call.args.count == 1) {
        LLVMValueRef p = nm_arg(g, expr, 0, locals);
        LLVMTypeRef ty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx),
            (LLVMTypeRef[]){ i8ptr }, 1, 0);
        LLVMValueRef fn = get_libc_fn(g, "free", ty);
        LLVMValueRef pp = LLVMBuildIntToPtr(g->builder, p, i8ptr, "nm.fp");
        zan_call2(g->builder, ty, fn, &pp, 1, "");
        *out = zero64;
        return true;
    }
    if (is_call_to(expr, "NativeMemory", "Copy") && expr->call.args.count == 3) {
        LLVMValueRef dst = nm_arg(g, expr, 0, locals);
        LLVMValueRef src = nm_arg(g, expr, 1, locals);
        LLVMValueRef n = nm_arg(g, expr, 2, locals);
        LLVMTypeRef ty = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr, i64t }, 3, 0);
        LLVMValueRef fn = get_libc_fn(g, "memmove", ty);
        zan_call2(g->builder, ty, fn, (LLVMValueRef[]){
            nm_addr(g, dst, zero64), nm_addr(g, src, zero64), n }, 3, "");
        *out = zero64;
        return true;
    }
    if (is_call_to(expr, "NativeMemory", "Fill") && expr->call.args.count == 3) {
        LLVMValueRef p = nm_arg(g, expr, 0, locals);
        LLVMValueRef v = nm_arg(g, expr, 1, locals);
        LLVMValueRef n = nm_arg(g, expr, 2, locals);
        LLVMTypeRef ty = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i32t, i64t }, 3, 0);
        LLVMValueRef fn = get_libc_fn(g, "memset", ty);
        zan_call2(g->builder, ty, fn, (LLVMValueRef[]){
            nm_addr(g, p, zero64),
            LLVMBuildTrunc(g->builder, v, i32t, "nm.b"), n }, 3, "");
        *out = zero64;
        return true;
    }
    if (is_call_to(expr, "NativeMemory", "Compare") && expr->call.args.count == 3) {
        LLVMValueRef a = nm_arg(g, expr, 0, locals);
        LLVMValueRef b = nm_arg(g, expr, 1, locals);
        LLVMValueRef n = nm_arg(g, expr, 2, locals);
        LLVMTypeRef ty = LLVMFunctionType(i32t, (LLVMTypeRef[]){ i8ptr, i8ptr, i64t }, 3, 0);
        LLVMValueRef fn = get_libc_fn(g, "memcmp", ty);
        LLVMValueRef r = zan_call2(g->builder, ty, fn, (LLVMValueRef[]){
            nm_addr(g, a, zero64), nm_addr(g, b, zero64), n }, 3, "nm.cmp");
        *out = LLVMBuildSExt(g->builder, r, i64t, "nm.cmp64");
        return true;
    }

    static const struct { const char *get, *set; int bits; } nm_acc[] = {
        { "GetByte", "SetByte", 8 },
        { "GetU16",  "SetU16", 16 },
        { "GetU32",  "SetU32", 32 },
        { "GetI64",  "SetI64", 64 },
    };
    for (size_t i = 0; i < sizeof(nm_acc) / sizeof(nm_acc[0]); i++) {
        if (is_call_to(expr, "NativeMemory", nm_acc[i].get) && expr->call.args.count == 2) {
            LLVMValueRef p = nm_arg(g, expr, 0, locals);
            LLVMValueRef off = nm_arg(g, expr, 1, locals);
            *out = nm_load(g, p, off, nm_acc[i].bits);
            return true;
        }
        if (is_call_to(expr, "NativeMemory", nm_acc[i].set) && expr->call.args.count == 3) {
            LLVMValueRef p = nm_arg(g, expr, 0, locals);
            LLVMValueRef off = nm_arg(g, expr, 1, locals);
            LLVMValueRef v = nm_arg(g, expr, 2, locals);
            nm_store(g, p, off, v, nm_acc[i].bits);
            *out = zero64;
            return true;
        }
    }

    if (is_call_to(expr, "NativeMemory", "GetString") && expr->call.args.count == 3) {
        LLVMValueRef p = nm_arg(g, expr, 0, locals);
        LLVMValueRef off = nm_arg(g, expr, 1, locals);
        LLVMValueRef len = nm_arg(g, expr, 2, locals);
        LLVMValueRef one = LLVMConstInt(i64t, 1, 0);
        LLVMValueRef total = LLVMBuildAdd(g->builder, len, one, "nm.gs.sz");
        LLVMValueRef s = emit_string_alloc_rc(g, total);
        LLVMTypeRef ty = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr, i64t }, 3, 0);
        LLVMValueRef fn = get_libc_fn(g, "memcpy", ty);
        zan_call2(g->builder, ty, fn, (LLVMValueRef[]){
            s, nm_addr(g, p, off), len }, 3, "");
        LLVMValueRef endp = LLVMBuildGEP2(g->builder, i8, s, &len, 1, "nm.gs.end");
        LLVMBuildStore(g->builder, LLVMConstInt(i8, 0, 0), endp);
        *out = s;
        return true;
    }
    if (is_call_to(expr, "NativeMemory", "PutString") && expr->call.args.count == 4) {
        LLVMValueRef p = nm_arg(g, expr, 0, locals);
        LLVMValueRef off = nm_arg(g, expr, 1, locals);
        zan_ast_node_t *s_ast = expr->call.args.items[2];
        LLVMValueRef s = emit_expr(g, s_ast, locals);
        LLVMValueRef len = nm_arg(g, expr, 3, locals);
        LLVMTypeRef ty = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr, i64t }, 3, 0);
        LLVMValueRef fn = get_libc_fn(g, "memcpy", ty);
        zan_call2(g->builder, ty, fn, (LLVMValueRef[]){
            nm_addr(g, p, off), s, len }, 3, "");
        emit_release_owned_call_temp(g, s_ast, s, locals);
        *out = zero64;
        return true;
    }
    if (is_call_to(expr, "NativeMemory", "Crc32") && expr->call.args.count == 2) {
        LLVMValueRef p = nm_arg(g, expr, 0, locals);
        LLVMValueRef len = nm_arg(g, expr, 1, locals);
        LLVMTypeRef ty = LLVMFunctionType(i64t, (LLVMTypeRef[]){ i8ptr, i64t }, 2, 0);
        LLVMValueRef fn = nm_crc32_fn(g);
        *out = zan_call2(g->builder, ty, fn, (LLVMValueRef[]){
            nm_addr(g, p, zero64), len }, 2, "nm.crc");
        return true;
    }
    return false;
}

static LLVMValueRef emit_expr(zan_irgen_t *g, zan_ast_node_t *expr, local_scope_t *locals) {
    if (!expr) return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);

    if (expr->kind == AST_REF_ARG) {
        return emit_ref_arg(g, expr, locals);
    }
    if (expr->kind == AST_MEMBER_ACCESS && expr->member.null_cond) {
        return emit_null_cond(g, expr, expr, locals);
    }
    if (expr->kind == AST_CALL && expr->call.callee &&
        expr->call.callee->kind == AST_MEMBER_ACCESS &&
        expr->call.callee->member.null_cond) {
        return emit_null_cond(g, expr, expr->call.callee, locals);
    }

    switch (expr->kind) {
    case AST_INT_LITERAL:
        return LLVMConstInt(LLVMInt64TypeInContext(g->ctx), (uint64_t)expr->int_val, 1);

    case AST_FLOAT_LITERAL:
        return LLVMConstReal(LLVMDoubleTypeInContext(g->ctx), expr->float_val);

    case AST_STRING_LITERAL:
        return emit_string_literal_rc(g, expr->str_val);

    case AST_BOOL_LITERAL:
        return LLVMConstInt(LLVMInt1TypeInContext(g->ctx), expr->bool_val ? 1 : 0, 0);

    case AST_CHAR_LITERAL:
        return LLVMConstInt(LLVMInt64TypeInContext(g->ctx), (uint64_t)expr->int_val, 0);

    case AST_NULL_LITERAL:
        return LLVMConstNull(LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0));

    case AST_IDENTIFIER: {
        local_var_t *local = local_find(locals, expr->ident.name);
        if (local) {
            return LLVMBuildLoad2(g->builder, map_type(g, local->type),
                                 local->alloca, "load");
        }
        /* implicit this.Field access in method bodies */
        if (g->current_this && g->current_type_sym) {
            int fi = get_field_index(g->current_type_sym, expr->ident.name);
            if (fi >= 0) {
                LLVMTypeRef st = get_struct_llvm_type(g, g->current_type_sym);
                if (st) {
                    LLVMValueRef this_ptr = LLVMBuildLoad2(g->builder,
                        LLVMPointerType(st, 0), g->current_this, "this");
                    LLVMValueRef fptr = LLVMBuildStructGEP2(g->builder, st, this_ptr, (unsigned)fi, "fld");
                    zan_symbol_t *fsym = get_field_sym(g->current_type_sym, expr->ident.name);
                    LLVMTypeRef ft = fsym ? map_type(g, fsym->type) : LLVMInt64TypeInContext(g->ctx);
                    return LLVMBuildLoad2(g->builder, ft, fptr, "fval");
                }
            }
        }
        /* bare-name static field of the enclosing class: `field` -> global.
         * Works in both static and instance methods. */
        if (g->current_type_sym) {
            zan_symbol_t *fsym = get_field_sym(g->current_type_sym, expr->ident.name);
            LLVMValueRef gv = get_static_field_global(g, g->current_type_sym, fsym);
            if (gv) {
                LLVMTypeRef ft = fsym->type ? map_type(g, fsym->type)
                                            : LLVMInt64TypeInContext(g->ctx);
                return LLVMBuildLoad2(g->builder, ft, gv, "sfld");
            }
        }
        /* method reference as delegate value: MethodName used as a value
         * (not called) → return the function pointer */
        if (g->current_type_sym) {
            zan_symbol_t *method_sym = get_method_sym(g->current_type_sym, expr->ident.name);
            if (method_sym) {
                for (int fi = 0; fi < g->function_count; fi++) {
                    if (g->functions[fi].sym == method_sym) {
                        return g->functions[fi].fn;
                    }
                }
            }
        }
        /* try global LLVM function by name */
        {
            char nbuf[256];
            int nl = expr->ident.name.len < 255 ? expr->ident.name.len : 255;
            memcpy(nbuf, expr->ident.name.str, (size_t)nl);
            nbuf[nl] = '\0';
            LLVMValueRef gfn = LLVMGetNamedFunction(g->mod, nbuf);
            if (gfn) return gfn;
        }
        /* return 0 for unresolved — error was reported in checker */
        /* Genuinely unresolved bare name: not a local, field, static,
         * method, or global function, and the binder does not know it
         * either. Silently lowering to 0/null hides real bugs (an
         * out-of-scope variable compiled to null -> runtime AV), so fail
         * the build. Guarded on the binder not knowing the name so known
         * types/namespaces used in value position keep the old behavior. */
        if (!zan_binder_lookup(g->binder, expr->ident.name)) {
            zan_diag_emit(g->diag, DIAG_ERROR, expr->loc,
                "use of undeclared identifier '%.*s'",
                (int)expr->ident.name.len, expr->ident.name.str);
        }
        return LLVMConstInt(LLVMInt64TypeInContext(g->ctx), 0, 0);
    }

    case AST_BINARY: {
        /* Short-circuit logical operators must not evaluate the right operand
         * unconditionally (e.g. `i < len && arr[i]`). Handle them before the
         * eager left/right evaluation used by the arithmetic/relational ops. */
        if (expr->binary.op == TK_AMP_AMP || expr->binary.op == TK_PIPE_PIPE) {
            bool is_and = (expr->binary.op == TK_AMP_AMP);
            LLVMTypeRef i1 = LLVMInt1TypeInContext(g->ctx);
            LLVMValueRef fn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder));

            LLVMValueRef lval = emit_expr(g, expr->binary.left, locals);
            if (LLVMGetTypeKind(LLVMTypeOf(lval)) != LLVMIntegerTypeKind ||
                LLVMGetIntTypeWidth(LLVMTypeOf(lval)) != 1) {
                lval = LLVMBuildICmp(g->builder, LLVMIntNE, lval,
                                     LLVMConstNull(LLVMTypeOf(lval)), "tobool");
            }
            LLVMBasicBlockRef left_bb = LLVMGetInsertBlock(g->builder);
            LLVMBasicBlockRef rhs_bb  = LLVMAppendBasicBlockInContext(g->ctx, fn, "sc.rhs");
            LLVMBasicBlockRef merge   = LLVMAppendBasicBlockInContext(g->ctx, fn, "sc.end");
            /* AND: if left is true, test right; else short-circuit false.
             * OR:  if left is true, short-circuit true; else test right. */
            if (is_and)
                LLVMBuildCondBr(g->builder, lval, rhs_bb, merge);
            else
                LLVMBuildCondBr(g->builder, lval, merge, rhs_bb);

            LLVMPositionBuilderAtEnd(g->builder, rhs_bb);
            LLVMValueRef rval = emit_expr(g, expr->binary.right, locals);
            if (LLVMGetTypeKind(LLVMTypeOf(rval)) != LLVMIntegerTypeKind ||
                LLVMGetIntTypeWidth(LLVMTypeOf(rval)) != 1) {
                rval = LLVMBuildICmp(g->builder, LLVMIntNE, rval,
                                     LLVMConstNull(LLVMTypeOf(rval)), "tobool");
            }
            LLVMBasicBlockRef rhs_end = LLVMGetInsertBlock(g->builder);
            LLVMBuildBr(g->builder, merge);

            LLVMPositionBuilderAtEnd(g->builder, merge);
            LLVMValueRef phi = LLVMBuildPhi(g->builder, i1, "sc");
            /* short-circuit constant: AND -> false, OR -> true */
            LLVMValueRef sc_const = LLVMConstInt(i1, is_and ? 0 : 1, 0);
            LLVMValueRef vals[] = { sc_const, rval };
            LLVMBasicBlockRef bbs[] = { left_bb, rhs_end };
            LLVMAddIncoming(phi, vals, bbs, 2);
            return phi;
        }

        /* A chain of 3+ string `+` parts is emitted as one flattened
         * allocation instead of pairwise (see emit_str_concat_n). */
        if (expr->binary.op == TK_PLUS && is_str_concat_node(g, expr, locals) &&
            (is_str_concat_node(g, expr->binary.left, locals) ||
             is_str_concat_node(g, expr->binary.right, locals))) {
            return emit_str_concat_n(g, expr, locals);
        }

        LLVMValueRef left = emit_expr(g, expr->binary.left, locals);
        LLVMValueRef right = emit_expr(g, expr->binary.right, locals);

        /* Operator overloading: if left operand is a user class instance,
         * look for a static op_add/op_sub/etc method and call it. */
        {
            zan_type_t *ltype = infer_expr_type(g, expr->binary.left, locals);
            /* comparisons against the null literal stay reference compares,
             * so an op_eq body can null-check without recursing into itself */
            int null_cmp = expr->binary.left->kind == AST_NULL_LITERAL ||
                           expr->binary.right->kind == AST_NULL_LITERAL;
            if (!null_cmp && ltype && ltype->kind == TYPE_CLASS && ltype->sym) {
                const char *op_name = NULL;
                switch (expr->binary.op) {
                case TK_PLUS:       op_name = "op_add"; break;
                case TK_MINUS:      op_name = "op_sub"; break;
                case TK_STAR:       op_name = "op_mul"; break;
                case TK_SLASH:      op_name = "op_div"; break;
                case TK_PERCENT:    op_name = "op_mod"; break;
                case TK_EQ_EQ:      op_name = "op_eq"; break;
                case TK_BANG_EQ:    op_name = "op_neq"; break;
                case TK_LESS:       op_name = "op_lt"; break;
                case TK_GREATER:    op_name = "op_gt"; break;
                case TK_LESS_EQ:    op_name = "op_le"; break;
                case TK_GREATER_EQ: op_name = "op_ge"; break;
                default: break;
                }
                if (op_name) {
                    /* search for the operator method in the class */
                    zan_istr_t op_istr = { (char *)op_name, (int)strlen(op_name) };
                    zan_symbol_t *op_sym = get_method_sym(ltype->sym, op_istr);
                    if (op_sym) {
                        for (int fi = 0; fi < g->function_count; fi++) {
                            if (g->functions[fi].sym == op_sym) {
                                LLVMValueRef args[] = { left, right };
                                const char *cn = (LLVMGetTypeKind(LLVMGetReturnType(g->functions[fi].fn_type)) == LLVMVoidTypeKind) ? "" : "opcall";
                                return zan_call2(g->builder, g->functions[fi].fn_type,
                                    g->functions[fi].fn, args, 2, cn);
                            }
                        }
                    }
                }
            }
        }

        bool both_ptr =
            LLVMGetTypeKind(LLVMTypeOf(left)) == LLVMPointerTypeKind &&
            LLVMGetTypeKind(LLVMTypeOf(right)) == LLVMPointerTypeKind;
        bool str_operand = is_string_expr(g, expr->binary.left, locals) ||
                           is_string_expr(g, expr->binary.right, locals);

        /* string concatenation: `a + b` when either operand is a string. A
         * numeric operand is formatted to its decimal string first (e.g.
         * "%t" + counter), so concat is not limited to two pointers. The
         * numeric temporaries are released after the concat copies them. */
        if (expr->binary.op == TK_PLUS && str_operand) {
            LLVMValueRef ls = emit_to_cstr(g, left);
            LLVMValueRef rs = emit_to_cstr(g, right);
            LLVMValueRef out = emit_str_concat(g, ls, rs);
            if (!is_string_expr(g, expr->binary.left, locals) ||
                expr_yields_owned_rc_value(g, expr->binary.left, locals)) {
                emit_string_release(g, ls);
            }
            if (!is_string_expr(g, expr->binary.right, locals) ||
                expr_yields_owned_rc_value(g, expr->binary.right, locals)) {
                emit_string_release(g, rs);
            }
            return out;
        }

        /* string equality: route `==`/`!=` on strings through strcmp. `== null`
         * keeps pointer semantics. */
        if ((expr->binary.op == TK_EQ_EQ || expr->binary.op == TK_BANG_EQ) &&
            both_ptr && str_operand &&
            expr->binary.left->kind != AST_NULL_LITERAL &&
            expr->binary.right->kind != AST_NULL_LITERAL) {
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
            LLVMValueRef cmp_args[] = { left, right };
            LLVMValueRef r = zan_call2(g->builder,
                LLVMFunctionType(i32t, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0),
                g->fn_strcmp, cmp_args, 2, "scmp");
            LLVMValueRef seq = LLVMBuildICmp(g->builder,
                expr->binary.op == TK_EQ_EQ ? LLVMIntEQ : LLVMIntNE,
                r, LLVMConstInt(i32t, 0, 0), "seq");
            if (is_string_expr(g, expr->binary.left, locals) &&
                expr_yields_owned_rc_value(g, expr->binary.left, locals)) {
                emit_string_release(g, left);
            }
            if (is_string_expr(g, expr->binary.right, locals) &&
                expr_yields_owned_rc_value(g, expr->binary.right, locals)) {
                emit_string_release(g, right);
            }
            return seq;
        }

        /* string ordering: route `<`/`<=`/`>`/`>=` on strings through strcmp
         * and compare its result against 0. Without this the operands (i8*)
         * would be compared as raw pointer addresses rather than by content. */
        if ((expr->binary.op == TK_LESS || expr->binary.op == TK_LESS_EQ ||
             expr->binary.op == TK_GREATER || expr->binary.op == TK_GREATER_EQ) &&
            both_ptr && str_operand &&
            expr->binary.left->kind != AST_NULL_LITERAL &&
            expr->binary.right->kind != AST_NULL_LITERAL) {
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
            LLVMValueRef cmp_args[] = { left, right };
            LLVMValueRef r = zan_call2(g->builder,
                LLVMFunctionType(i32t, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0),
                g->fn_strcmp, cmp_args, 2, "scmp");
            LLVMIntPredicate pred =
                expr->binary.op == TK_LESS       ? LLVMIntSLT :
                expr->binary.op == TK_LESS_EQ    ? LLVMIntSLE :
                expr->binary.op == TK_GREATER    ? LLVMIntSGT :
                                                   LLVMIntSGE;
            LLVMValueRef sord = LLVMBuildICmp(g->builder, pred,
                r, LLVMConstInt(i32t, 0, 0), "sord");
            if (is_string_expr(g, expr->binary.left, locals) &&
                expr_yields_owned_rc_value(g, expr->binary.left, locals)) {
                emit_string_release(g, left);
            }
            if (is_string_expr(g, expr->binary.right, locals) &&
                expr_yields_owned_rc_value(g, expr->binary.right, locals)) {
                emit_string_release(g, right);
            }
            return sord;
        }

        /* Pointer equality (incl. `== null`): compare the raw pointers, then
         * release owned call-result temps so `obj.Get() != null` does not leak
         * the +1 value the callee returned. */
        if ((expr->binary.op == TK_EQ_EQ || expr->binary.op == TK_BANG_EQ) && both_ptr) {
            LLVMValueRef rcast = right;
            if (LLVMTypeOf(right) != LLVMTypeOf(left))
                rcast = LLVMBuildBitCast(g->builder, right, LLVMTypeOf(left), "pcast");
            LLVMValueRef pcmp = LLVMBuildICmp(g->builder,
                expr->binary.op == TK_EQ_EQ ? LLVMIntEQ : LLVMIntNE,
                left, rcast, "pcmp");
            emit_release_owned_call_temp(g, expr->binary.left, left, locals);
            emit_release_owned_call_temp(g, expr->binary.right, right, locals);
            return pcmp;
        }

        LLVMTypeRef left_type = LLVMTypeOf(left);
        bool is_float = (LLVMGetTypeKind(left_type) == LLVMDoubleTypeKind ||
                         LLVMGetTypeKind(left_type) == LLVMFloatTypeKind);

        /* reconcile mixed-width integer operands (e.g. i32 int vs i64 length) */
        coerce_int_pair(g, &left, &right);

        /* ulong operands select unsigned division/remainder/shift/compare */
        bool is_unsigned = !is_float &&
            (expr_is_ulong(g, expr->binary.left, locals) ||
             expr_is_ulong(g, expr->binary.right, locals));

        switch (expr->binary.op) {
        case TK_PLUS:
            return is_float ? LLVMBuildFAdd(g->builder, left, right, "add")
                            : LLVMBuildAdd(g->builder, left, right, "add");
        case TK_MINUS:
            return is_float ? LLVMBuildFSub(g->builder, left, right, "sub")
                            : LLVMBuildSub(g->builder, left, right, "sub");
        case TK_STAR:
            return is_float ? LLVMBuildFMul(g->builder, left, right, "mul")
                            : LLVMBuildMul(g->builder, left, right, "mul");
        case TK_SLASH:
            if (is_float) return LLVMBuildFDiv(g->builder, left, right, "div");
            {
                LLVMValueRef zero = LLVMConstInt(LLVMTypeOf(right), 0, 0);
                LLVMValueRef is_zero = LLVMBuildICmp(g->builder, LLVMIntEQ, right, zero, "divz");
                emit_runtime_check(g, is_zero, expr->loc, "division by zero");
            }
            return is_unsigned ? LLVMBuildUDiv(g->builder, left, right, "div")
                               : LLVMBuildSDiv(g->builder, left, right, "div");
        case TK_PERCENT:
            if (is_float) return LLVMBuildFRem(g->builder, left, right, "rem");
            {
                LLVMValueRef zero = LLVMConstInt(LLVMTypeOf(right), 0, 0);
                LLVMValueRef is_zero = LLVMBuildICmp(g->builder, LLVMIntEQ, right, zero, "remz");
                emit_runtime_check(g, is_zero, expr->loc, "division by zero (modulo)");
            }
            return is_unsigned ? LLVMBuildURem(g->builder, left, right, "rem")
                               : LLVMBuildSRem(g->builder, left, right, "rem");
        case TK_AMP:
            return LLVMBuildAnd(g->builder, left, right, "and");
        case TK_PIPE:
            return LLVMBuildOr(g->builder, left, right, "or");
        case TK_CARET:
            return LLVMBuildXor(g->builder, left, right, "xor");
        case TK_LESS_LESS:
            return LLVMBuildShl(g->builder, left, right, "shl");
        case TK_GREATER_GREATER:
            return is_unsigned ? LLVMBuildLShr(g->builder, left, right, "shr")
                               : LLVMBuildAShr(g->builder, left, right, "shr");
        case TK_EQ_EQ:
            return is_float ? LLVMBuildFCmp(g->builder, LLVMRealOEQ, left, right, "eq")
                            : LLVMBuildICmp(g->builder, LLVMIntEQ, left, right, "eq");
        case TK_BANG_EQ:
            return is_float ? LLVMBuildFCmp(g->builder, LLVMRealONE, left, right, "ne")
                            : LLVMBuildICmp(g->builder, LLVMIntNE, left, right, "ne");
        case TK_LESS:
            return is_float ? LLVMBuildFCmp(g->builder, LLVMRealOLT, left, right, "lt")
                            : LLVMBuildICmp(g->builder, is_unsigned ? LLVMIntULT : LLVMIntSLT, left, right, "lt");
        case TK_GREATER:
            return is_float ? LLVMBuildFCmp(g->builder, LLVMRealOGT, left, right, "gt")
                            : LLVMBuildICmp(g->builder, is_unsigned ? LLVMIntUGT : LLVMIntSGT, left, right, "gt");
        case TK_LESS_EQ:
            return is_float ? LLVMBuildFCmp(g->builder, LLVMRealOLE, left, right, "le")
                            : LLVMBuildICmp(g->builder, is_unsigned ? LLVMIntULE : LLVMIntSLE, left, right, "le");
        case TK_GREATER_EQ:
            return is_float ? LLVMBuildFCmp(g->builder, LLVMRealOGE, left, right, "ge")
                            : LLVMBuildICmp(g->builder, is_unsigned ? LLVMIntUGE : LLVMIntSGE, left, right, "ge");
        case TK_QUESTION_QUESTION: {
            /* ?? null coalescing: if left != 0/null, use left, else right */
            LLVMValueRef is_null;
            if (LLVMGetTypeKind(left_type) == LLVMPointerTypeKind) {
                LLVMValueRef null_ptr = LLVMConstNull(left_type);
                is_null = LLVMBuildICmp(g->builder, LLVMIntEQ, left, null_ptr, "isnull");
            } else {
                is_null = LLVMBuildICmp(g->builder, LLVMIntEQ, left,
                    LLVMConstInt(left_type, 0, 0), "isnull");
            }
            LLVMValueRef coal_fn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder));
            LLVMBasicBlockRef use_right = LLVMAppendBasicBlockInContext(g->ctx, coal_fn, "coal.r");
            LLVMBasicBlockRef merge = LLVMAppendBasicBlockInContext(g->ctx, coal_fn, "coal.m");
            LLVMBasicBlockRef left_bb = LLVMGetInsertBlock(g->builder);
            LLVMBuildCondBr(g->builder, is_null, use_right, merge);
            LLVMPositionBuilderAtEnd(g->builder, use_right);
            LLVMBuildBr(g->builder, merge);
            LLVMPositionBuilderAtEnd(g->builder, merge);
            LLVMValueRef phi = LLVMBuildPhi(g->builder, left_type, "coal");
            LLVMValueRef vals[] = { left, right };
            LLVMBasicBlockRef bbs[] = { left_bb, use_right };
            LLVMAddIncoming(phi, vals, bbs, 2);
            return phi;
        }
        default:
            return LLVMConstInt(LLVMInt64TypeInContext(g->ctx), 0, 0);
        }
    }

    case AST_UNARY: {
        LLVMValueRef operand = emit_expr(g, expr->unary.operand, locals);
        switch (expr->unary.op) {
        case TK_MINUS: {
            LLVMTypeRef t = LLVMTypeOf(operand);
            if (LLVMGetTypeKind(t) == LLVMDoubleTypeKind ||
                LLVMGetTypeKind(t) == LLVMFloatTypeKind) {
                return LLVMBuildFNeg(g->builder, operand, "neg");
            }
            return LLVMBuildNeg(g->builder, operand, "neg");
        }
        case TK_BANG: {
            /* Logical not: true iff the operand is zero/null. A bitwise not
             * would be wrong for bools materialized wider than i1 (e.g. an i8
             * `1` bitwise-nots to 0xFE, which is still truthy). */
            LLVMTypeRef ot = LLVMTypeOf(operand);
            LLVMTypeKind otk = LLVMGetTypeKind(ot);
            if (otk == LLVMIntegerTypeKind) {
                return LLVMBuildICmp(g->builder, LLVMIntEQ, operand,
                    LLVMConstInt(ot, 0, 0), "lnot");
            }
            if (otk == LLVMPointerTypeKind) {
                return LLVMBuildICmp(g->builder, LLVMIntEQ, operand,
                    LLVMConstNull(ot), "lnotp");
            }
            return LLVMBuildNot(g->builder, operand, "not");
        }
        case TK_TILDE:
            return LLVMBuildNot(g->builder, operand, "bnot");
        default:
            return operand;
        }
    }

    case AST_ASSIGNMENT: {
        LLVMValueRef right = emit_expr(g, expr->binary.right, locals);
        if (expr->binary.left->kind == AST_IDENTIFIER) {
            local_var_t *local = local_find(locals, expr->binary.left->ident.name);
            if (local && local_owns_arc(local)) {
                /* ARC: release the previous occupant and retain the new one. */
                emit_rc_capture_local(g, local->type, local->alloca, right, expr->binary.right, locals);
            } else if (local) {
                LLVMValueRef sv = coerce_int_to(g, right,
                    local_slot_type(g, local));
                LLVMBuildStore(g->builder, sv, local->alloca);
            } else if (g->current_type_sym &&
                       get_static_field_global(g, g->current_type_sym,
                           get_field_sym(g->current_type_sym, expr->binary.left->ident.name))) {
                /* bare-name static field of the enclosing class: `field = v` */
                zan_symbol_t *fs = get_field_sym(g->current_type_sym,
                    expr->binary.left->ident.name);
                LLVMValueRef gv = get_static_field_global(g, g->current_type_sym, fs);
                if (fs->type && is_rc_managed_type(fs->type)) {
                    emit_rc_store_field(g, fs->type, gv, right, expr->binary.right, locals,
                                        (fs->modifiers & MOD_WEAK) ? 1 : 0);
                } else {
                    LLVMTypeRef ft = fs->type ? map_type(g, fs->type)
                                              : LLVMInt64TypeInContext(g->ctx);
                    LLVMBuildStore(g->builder, coerce_int_to(g, right, ft), gv);
                }
            } else if (g->current_this && g->current_type_sym) {
                /* implicit this.Field assignment */
                int fi = get_field_index(g->current_type_sym, expr->binary.left->ident.name);
                if (fi >= 0) {
                    LLVMTypeRef st = get_struct_llvm_type(g, g->current_type_sym);
                    if (st) {
                        LLVMValueRef this_ptr = LLVMBuildLoad2(g->builder,
                            LLVMPointerType(st, 0), g->current_this, "this");
                        LLVMValueRef fptr = LLVMBuildStructGEP2(g->builder, st, this_ptr, (unsigned)fi, "fld");
                        /* type conversion if needed */
                        zan_symbol_t *fsym = get_field_sym(g->current_type_sym, expr->binary.left->ident.name);
                        if (fsym && fsym->type) {
                            LLVMTypeRef target_t = map_type(g, fsym->type);
                            LLVMTypeRef val_t = LLVMTypeOf(right);
                            if (target_t != val_t) {
                                if (LLVMGetTypeKind(target_t) == LLVMFloatTypeKind &&
                                    LLVMGetTypeKind(val_t) == LLVMDoubleTypeKind) {
                                    right = LLVMBuildFPTrunc(g->builder, right, target_t, "trunc");
                                } else if (LLVMGetTypeKind(target_t) == LLVMDoubleTypeKind &&
                                           LLVMGetTypeKind(val_t) == LLVMFloatTypeKind) {
                                    right = LLVMBuildFPExt(g->builder, right, target_t, "ext");
                                } else if (LLVMGetTypeKind(target_t) == LLVMIntegerTypeKind &&
                                           LLVMGetTypeKind(val_t) == LLVMIntegerTypeKind) {
                                    unsigned tw = LLVMGetIntTypeWidth(target_t);
                                    unsigned vw = LLVMGetIntTypeWidth(val_t);
                                    if (tw > vw) right = LLVMBuildSExt(g->builder, right, target_t, "ext");
                                    else if (tw < vw) right = LLVMBuildTrunc(g->builder, right, target_t, "trunc");
                                }
                            }
                        }
                        if (fsym && fsym->type && is_rc_managed_type(fsym->type)) {
                            emit_rc_store_field(g, fsym->type, fptr, right, expr->binary.right, locals,
                                                (fsym->modifiers & MOD_WEAK) ? 1 : 0);
                        } else {
                            LLVMBuildStore(g->builder, right, fptr);
                        }
                    }
                }
            }
        } else if (expr->binary.left->kind == AST_INDEX) {
            /* arr[i] = value */
            zan_ast_node_t *arr_expr = expr->binary.left->index.object;
            if (arr_expr->kind == AST_IDENTIFIER) {
                local_var_t *local = local_find(locals, arr_expr->ident.name);
                if (local && local->type && local->type->name.len == 4 &&
                    memcmp(local->type->name.str, "Dict", 4) == 0) {
                    /* dict[key] = value — upsert via the shared helper */
                    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                    LLVMValueRef raw = LLVMBuildLoad2(g->builder, i8ptr, local->alloca, "draw");
                    zan_type_t *kt = dict_key_type(g, local->type);
                    zan_type_t *vt = dict_value_type(local->type);
                    LLVMValueRef key = emit_expr(g, expr->binary.left->index.index, locals);
                    if (LLVMGetTypeKind(LLVMTypeOf(key)) == LLVMIntegerTypeKind) {
                        if (LLVMGetIntTypeWidth(LLVMTypeOf(key)) < 64)
                            key = LLVMBuildSExt(g->builder, key, i64, "k.sx");
                        key = LLVMBuildIntToPtr(g->builder, key, i8ptr, "k.ip");
                    } else if (LLVMTypeOf(key) != i8ptr) {
                        key = LLVMBuildBitCast(g->builder, key, i8ptr, "k.bc");
                    }
                    if (kt && is_rc_managed_type(kt) &&
                        !expr_yields_owned_rc_value(g, expr->binary.left->index.index, locals))
                        emit_rc_retain_for_type(g, kt, key);
                    LLVMValueRef val = right;
                    LLVMTypeKind vk = LLVMGetTypeKind(LLVMTypeOf(val));
                    if (vk == LLVMPointerTypeKind) {
                        if (vt && is_rc_managed_type(vt) &&
                            !expr_yields_owned_rc_value(g, expr->binary.right, locals))
                            emit_rc_retain_for_type(g, vt, val);
                        val = LLVMBuildPtrToInt(g->builder, val, i64, "v.pi");
                    } else if (vk == LLVMDoubleTypeKind) {
                        val = LLVMBuildBitCast(g->builder, val, i64, "v.bc");
                    } else if (vk == LLVMIntegerTypeKind &&
                               LLVMGetIntTypeWidth(LLVMTypeOf(val)) < 64) {
                        val = LLVMBuildSExt(g->builder, val, i64, "v.sx");
                    }
                    LLVMValueRef is_str = LLVMConstInt(i64,
                        (kt && kt->kind == TYPE_STRING) ? 1 : 0, 0);
                    LLVMValueRef sf = get_dict_set_fn(g);
                    zan_call2(g->builder, LLVMGlobalGetValueType(sf), sf,
                        (LLVMValueRef[]){ raw, key, val, is_str }, 4, "");
                } else if (local && local->type && local->type->name.len == 4 &&
                    memcmp(local->type->name.str, "List", 4) == 0) {
                    /* list[i] = value — store into the list's i64 data slots */
                    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                    LLVMValueRef raw = LLVMBuildLoad2(g->builder, i8ptr, local->alloca, "lraw");
                    LLVMValueRef list_ptr = LLVMBuildBitCast(g->builder, raw,
                        LLVMPointerType(g->list_struct_type, 0), "lptr");
                    LLVMValueRef data_field = LLVMBuildStructGEP2(g->builder, g->list_struct_type,
                        list_ptr, 2, "df");
                    LLVMValueRef data = LLVMBuildLoad2(g->builder, LLVMPointerType(LLVMInt64TypeInContext(g->ctx), 0),
                        data_field, "data");
                    LLVMValueRef idx = emit_expr(g, expr->binary.left->index.index, locals);
                    if (LLVMGetTypeKind(LLVMTypeOf(idx)) == LLVMIntegerTypeKind &&
                        LLVMGetIntTypeWidth(LLVMTypeOf(idx)) < 64) {
                        idx = LLVMBuildSExt(g->builder, idx, LLVMInt64TypeInContext(g->ctx), "idxext");
                    }
                    LLVMValueRef slot_ptr = LLVMBuildGEP2(g->builder, LLVMInt64TypeInContext(g->ctx), data, &idx, 1, "ep");
                    emit_collection_slot_store(g, container_elem_type(local->type),
                        LLVMInt64TypeInContext(g->ctx), slot_ptr,
                        right, expr->binary.right, locals, 1);
                } else if (local) {
                    LLVMValueRef arr_ptr = LLVMBuildLoad2(g->builder,
                        LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0),
                        local->alloca, "arrload");
                    LLVMValueRef idx = emit_expr(g, expr->binary.left->index.index, locals);
                    /* string (byte buffer): use i8 element type and truncate value */
                    if (local->type && local->type->kind == TYPE_STRING) {
                        LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
                        LLVMValueRef elem_ptr = LLVMBuildGEP2(g->builder, i8, arr_ptr, &idx, 1, "eidx");
                        LLVMValueRef val8 = LLVMBuildTrunc(g->builder, right, i8, "byte");
                        LLVMBuildStore(g->builder, val8, elem_ptr);
                    } else {
                        LLVMTypeRef elem_llvm = LLVMInt32TypeInContext(g->ctx);
                        if (local->type && local->type->element_type) {
                            zan_type_t *et = local->type->element_type;
                            elem_llvm = map_type(g, et);
                        }
                        LLVMValueRef typed_arr = LLVMBuildBitCast(g->builder, arr_ptr,
                            LLVMPointerType(elem_llvm, 0), "arrp");
                        LLVMValueRef elem_ptr = LLVMBuildGEP2(g->builder, elem_llvm, typed_arr, &idx, 1, "eidx");
                        zan_type_t *et = local->type ? local->type->element_type : NULL;
                        LLVMValueRef stored = right;
                        LLVMTypeKind slot_k = LLVMGetTypeKind(elem_llvm);
                        LLVMTypeKind val_k = LLVMGetTypeKind(LLVMTypeOf(stored));
                        if (et && is_rc_managed_type(et)) {
                            if (!expr_yields_owned_rc_value(g, expr->binary.right, locals)) {
                                emit_rc_retain_for_type(g, et, stored);
                            }
                            if (val_k == LLVMPointerTypeKind && LLVMTypeOf(stored) != elem_llvm) {
                                stored = LLVMBuildBitCast(g->builder, stored, elem_llvm, "slot.bc");
                            }
                            LLVMValueRef old = LLVMBuildLoad2(g->builder, elem_llvm, elem_ptr, "old");
                            LLVMBuildStore(g->builder, stored, elem_ptr);
                            emit_rc_release_for_type(g, et, old);
                        } else {
                            if (slot_k == LLVMPointerTypeKind) {
                                if (val_k == LLVMIntegerTypeKind) {
                                    stored = LLVMBuildIntToPtr(g->builder, stored, elem_llvm, "slot.ip");
                                } else if (val_k == LLVMPointerTypeKind && LLVMTypeOf(stored) != elem_llvm) {
                                    stored = LLVMBuildBitCast(g->builder, stored, elem_llvm, "slot.bc");
                                }
                            } else if (slot_k == LLVMIntegerTypeKind) {
                                if (val_k == LLVMPointerTypeKind) {
                                    stored = LLVMBuildPtrToInt(g->builder, stored, elem_llvm, "slot.pi");
                                } else if (val_k == LLVMIntegerTypeKind &&
                                           LLVMGetIntTypeWidth(LLVMTypeOf(stored)) < 64) {
                                    stored = LLVMBuildSExt(g->builder, stored, elem_llvm, "slot.sx");
                                }
                            }
                            LLVMBuildStore(g->builder, stored, elem_ptr);
                        }
                    }
                } else if (g->current_type_sym) {
                    /* implicit this.field[i] = value */
                    zan_symbol_t *fsym = get_field_sym(g->current_type_sym, arr_expr->ident.name);
                    if (fsym) {
                        LLVMValueRef arr_ptr = emit_expr(g, arr_expr, locals);
                        LLVMValueRef idx = emit_expr(g, expr->binary.left->index.index, locals);
                        if (fsym->type && fsym->type->name.len == 4 &&
                            memcmp(fsym->type->name.str, "List", 4) == 0) {
                            LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
                            LLVMValueRef list_ptr = LLVMBuildBitCast(g->builder, arr_ptr,
                                LLVMPointerType(g->list_struct_type, 0), "lptr");
                            LLVMValueRef data_field = LLVMBuildStructGEP2(g->builder, g->list_struct_type,
                                list_ptr, 2, "df");
                            LLVMValueRef data = LLVMBuildLoad2(g->builder, LLVMPointerType(i64t, 0),
                                data_field, "data");
                            if (LLVMGetTypeKind(LLVMTypeOf(idx)) == LLVMIntegerTypeKind &&
                                LLVMGetIntTypeWidth(LLVMTypeOf(idx)) < 64) {
                                idx = LLVMBuildSExt(g->builder, idx, i64t, "idxext");
                            }
                            LLVMValueRef slot_ptr = LLVMBuildGEP2(g->builder, i64t, data, &idx, 1, "ep");
                            emit_collection_slot_store(g, container_elem_type(fsym->type), i64t, slot_ptr,
                                right, expr->binary.right, locals, 1);
                        } else if (fsym->type && fsym->type->kind == TYPE_STRING) {
                            LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
                            LLVMValueRef elem_ptr = LLVMBuildGEP2(g->builder, i8, arr_ptr, &idx, 1, "eidx");
                            LLVMValueRef val8 = LLVMBuildTrunc(g->builder, right, i8, "byte");
                            LLVMBuildStore(g->builder, val8, elem_ptr);
                        } else {
                        LLVMTypeRef elem_llvm = LLVMInt32TypeInContext(g->ctx);
                        if (fsym->type && fsym->type->element_type) {
                            zan_type_t *et = fsym->type->element_type;
                            elem_llvm = map_type(g, et);
                        }
                        LLVMValueRef typed_arr = LLVMBuildBitCast(g->builder, arr_ptr,
                            LLVMPointerType(elem_llvm, 0), "arrp");
                        LLVMValueRef elem_ptr = LLVMBuildGEP2(g->builder, elem_llvm, typed_arr, &idx, 1, "eidx");
                        zan_type_t *et = fsym->type ? fsym->type->element_type : NULL;
                        LLVMValueRef stored = right;
                        LLVMTypeKind slot_k = LLVMGetTypeKind(elem_llvm);
                        LLVMTypeKind val_k = LLVMGetTypeKind(LLVMTypeOf(stored));
                        if (et && is_rc_managed_type(et)) {
                            if (!expr_yields_owned_rc_value(g, expr->binary.right, locals)) {
                                emit_rc_retain_for_type(g, et, stored);
                            }
                            if (val_k == LLVMPointerTypeKind && LLVMTypeOf(stored) != elem_llvm) {
                                stored = LLVMBuildBitCast(g->builder, stored, elem_llvm, "slot.bc");
                            }
                            LLVMValueRef old = LLVMBuildLoad2(g->builder, elem_llvm, elem_ptr, "old");
                            LLVMBuildStore(g->builder, stored, elem_ptr);
                            emit_rc_release_for_type(g, et, old);
                        } else {
                            if (slot_k == LLVMPointerTypeKind) {
                                if (val_k == LLVMIntegerTypeKind) {
                                    stored = LLVMBuildIntToPtr(g->builder, stored, elem_llvm, "slot.ip");
                                } else if (val_k == LLVMPointerTypeKind && LLVMTypeOf(stored) != elem_llvm) {
                                    stored = LLVMBuildBitCast(g->builder, stored, elem_llvm, "slot.bc");
                                }
                            } else if (slot_k == LLVMIntegerTypeKind) {
                                if (val_k == LLVMPointerTypeKind) {
                                    stored = LLVMBuildPtrToInt(g->builder, stored, elem_llvm, "slot.pi");
                                } else if (val_k == LLVMIntegerTypeKind &&
                                           LLVMGetIntTypeWidth(LLVMTypeOf(stored)) < 64) {
                                    stored = LLVMBuildSExt(g->builder, stored, elem_llvm, "slot.sx");
                                }
                            }
                            LLVMBuildStore(g->builder, stored, elem_ptr);
                        }
                        }
                    }
                }
            } else if (arr_expr->kind == AST_MEMBER_ACCESS) {
                /* obj.field[i] = value — array stored in a struct/class field */
                zan_type_t *at = member_access_field_type(g, locals, arr_expr);
                if (at) {
                    LLVMValueRef arr_ptr = emit_expr(g, arr_expr, locals);
                    LLVMValueRef idx = emit_expr(g, expr->binary.left->index.index, locals);
                    if (at->name.len == 4 && memcmp(at->name.str, "List", 4) == 0) {
                        /* List field: index into the data buffer, not the
                         * struct — otherwise the store clobbers count/cap/data. */
                        LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
                        LLVMValueRef list_ptr = LLVMBuildBitCast(g->builder, arr_ptr,
                            LLVMPointerType(g->list_struct_type, 0), "lptr");
                        LLVMValueRef data_field = LLVMBuildStructGEP2(g->builder, g->list_struct_type,
                            list_ptr, 2, "df");
                        LLVMValueRef data = LLVMBuildLoad2(g->builder, LLVMPointerType(i64t, 0),
                            data_field, "data");
                        if (LLVMGetTypeKind(LLVMTypeOf(idx)) == LLVMIntegerTypeKind &&
                            LLVMGetIntTypeWidth(LLVMTypeOf(idx)) < 64) {
                            idx = LLVMBuildSExt(g->builder, idx, i64t, "idxext");
                        }
                        LLVMValueRef slot_ptr = LLVMBuildGEP2(g->builder, i64t, data, &idx, 1, "ep");
                        emit_collection_slot_store(g, container_elem_type(at), i64t, slot_ptr,
                            right, expr->binary.right, locals, 1);
                    } else if (at->kind == TYPE_STRING) {
                        LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
                        LLVMValueRef elem_ptr = LLVMBuildGEP2(g->builder, i8, arr_ptr, &idx, 1, "eidx");
                        LLVMValueRef val8 = LLVMBuildTrunc(g->builder, right, i8, "byte");
                        LLVMBuildStore(g->builder, val8, elem_ptr);
                        } else {
                        LLVMTypeRef elem_llvm = LLVMInt32TypeInContext(g->ctx);
                        if (at->element_type) {
                            elem_llvm = map_type(g, at->element_type);
                        }
                        LLVMValueRef typed_arr = LLVMBuildBitCast(g->builder, arr_ptr,
                            LLVMPointerType(elem_llvm, 0), "arrp");
                        LLVMValueRef elem_ptr = LLVMBuildGEP2(g->builder, elem_llvm, typed_arr, &idx, 1, "eidx");
                        zan_type_t *et = at->element_type;
                        LLVMValueRef stored = right;
                        LLVMTypeKind slot_k = LLVMGetTypeKind(elem_llvm);
                        LLVMTypeKind val_k = LLVMGetTypeKind(LLVMTypeOf(stored));
                        if (et && is_rc_managed_type(et)) {
                            if (!expr_yields_owned_rc_value(g, expr->binary.right, locals)) {
                                emit_rc_retain_for_type(g, et, stored);
                            }
                            if (val_k == LLVMPointerTypeKind && LLVMTypeOf(stored) != elem_llvm) {
                                stored = LLVMBuildBitCast(g->builder, stored, elem_llvm, "slot.bc");
                            }
                            LLVMValueRef old = LLVMBuildLoad2(g->builder, elem_llvm, elem_ptr, "old");
                            LLVMBuildStore(g->builder, stored, elem_ptr);
                            emit_rc_release_for_type(g, et, old);
                        } else {
                            if (slot_k == LLVMPointerTypeKind) {
                                if (val_k == LLVMIntegerTypeKind) {
                                    stored = LLVMBuildIntToPtr(g->builder, stored, elem_llvm, "slot.ip");
                                } else if (val_k == LLVMPointerTypeKind && LLVMTypeOf(stored) != elem_llvm) {
                                    stored = LLVMBuildBitCast(g->builder, stored, elem_llvm, "slot.bc");
                                }
                            } else if (slot_k == LLVMIntegerTypeKind) {
                                if (val_k == LLVMPointerTypeKind) {
                                    stored = LLVMBuildPtrToInt(g->builder, stored, elem_llvm, "slot.pi");
                                } else if (val_k == LLVMIntegerTypeKind &&
                                           LLVMGetIntTypeWidth(LLVMTypeOf(stored)) < 64) {
                                    stored = LLVMBuildSExt(g->builder, stored, elem_llvm, "slot.sx");
                                }
                            }
                            LLVMBuildStore(g->builder, stored, elem_ptr);
                        }
                    }
                }
            }
        } else if (expr->binary.left->kind == AST_MEMBER_ACCESS) {
            /* obj.Field = value */
            zan_ast_node_t *obj_expr = expr->binary.left->member.object;
            bool stored = false;
            /* ClassName.StaticField = value — store into the backing global. */
            if (obj_expr->kind == AST_IDENTIFIER &&
                !local_find(locals, obj_expr->ident.name)) {
                zan_symbol_t *cs = zan_binder_lookup(g->binder, obj_expr->ident.name);
                if (cs && (cs->kind == SYM_CLASS || cs->kind == SYM_STRUCT)) {
                    zan_symbol_t *fs = get_field_sym(cs, expr->binary.left->member.name);
                    LLVMValueRef gv = get_static_field_global(g, cs, fs);
                    if (gv) {
                        if (fs->type && is_rc_managed_type(fs->type)) {
                            emit_rc_store_field(g, fs->type, gv, right,
                                                expr->binary.right, locals,
                                                (fs->modifiers & MOD_WEAK) ? 1 : 0);
                        } else {
                            LLVMTypeRef ft = fs->type ? map_type(g, fs->type)
                                                      : LLVMInt64TypeInContext(g->ctx);
                            LLVMBuildStore(g->builder, coerce_int_to(g, right, ft), gv);
                        }
                        stored = true;
                    }
                }
            }
            if (!stored && obj_expr->kind == AST_IDENTIFIER) {
                local_var_t *local = local_find(locals, obj_expr->ident.name);
                if (local && local->type && local->type->sym) {
                    int fi = get_field_index(local->type->sym, expr->binary.left->member.name);
                    if (fi >= 0) {
                        LLVMTypeRef st = get_struct_llvm_type(g, local->type->sym);
                        if (st) {
                            LLVMValueRef struct_ptr = struct_base_ptr(g, local, st);
                            LLVMValueRef fptr = LLVMBuildStructGEP2(g->builder, st, struct_ptr, (unsigned)fi, "fld");
                            zan_symbol_t *afsym = get_field_sym(local->type->sym, expr->binary.left->member.name);
                            if (afsym && afsym->type && is_rc_managed_type(afsym->type)) {
                                emit_rc_store_field(g, afsym->type, fptr, right, expr->binary.right, locals,
                                                    (afsym->modifiers & MOD_WEAK) ? 1 : 0);
                            } else {
                                LLVMBuildStore(g->builder, right, fptr);
                            }
                            stored = true;
                        }
                    }
                }
            }
            /* general: <expr>.field = value where <expr> yields a class pointer
             * (e.g. list[i].field, a.b.field, this.field). */
            if (!stored) {
                zan_symbol_t *cls = expr_class_sym(g, obj_expr, locals);
                if (cls) {
                    int fi = get_field_index(cls, expr->binary.left->member.name);
                    if (fi >= 0) {
                        LLVMTypeRef st = get_struct_llvm_type(g, cls);
                        LLVMValueRef obj_val = emit_expr(g, obj_expr, locals);
                        if (st && LLVMGetTypeKind(LLVMTypeOf(obj_val)) == LLVMPointerTypeKind) {
                            LLVMValueRef fptr = LLVMBuildStructGEP2(g->builder, st,
                                obj_val, (unsigned)fi, "gfld");
                            zan_symbol_t *gfsym = get_field_sym(cls, expr->binary.left->member.name);
                            if (gfsym && gfsym->type && is_rc_managed_type(gfsym->type)) {
                                emit_rc_store_field(g, gfsym->type, fptr, right, expr->binary.right, locals,
                                                    (gfsym->modifiers & MOD_WEAK) ? 1 : 0);
                            } else {
                                LLVMBuildStore(g->builder, right, fptr);
                            }
                        }
                    }
                }
            }
        }
        return right;
    }

    case AST_CALL: {
        /* Task.Spawn(<asyncCall>) — fire-and-forget: run an async call as an
         * independent coroutine WITHOUT awaiting it. Emits the callee's ramp
         * (heap frame) then schedules it on the cooperative driver with no
         * awaiter and without suspending the caller (contrast await, which
         * registers self as awaiter and suspends). This is the concurrency
         * primitive a server accept loop uses to handle each connection on its
         * own coroutine instead of serially. See docs/ASYNC_CPS_DESIGN.md. */
        if (is_call_to(expr, "Task", "Spawn") && expr->call.args.count == 1 &&
            expr->call.args.items[0]->kind == AST_CALL) {
            LLVMTypeRef sp_i64 = LLVMInt64TypeInContext(g->ctx);
            LLVMTypeRef sp_i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            zan_ast_node_t *sub_arg = expr->call.args.items[0];
            LLVMValueRef sub = emit_expr(g, sub_arg, locals);
            LLVMValueRef sub_resume = NULL;
            if (LLVMIsACallInst(sub)) {
                LLVMValueRef callee = LLVMGetCalledValue(sub);
                if (callee) {
                    size_t nl = 0;
                    const char *cn = LLVMGetValueName2(callee, &nl);
                    if (cn && nl > 0 && nl < 240) {
                        char rn[256];
                        memcpy(rn, cn, nl);
                        memcpy(rn + nl, "$resume", 8); /* includes NUL */
                        sub_resume = LLVMGetNamedFunction(g->mod, rn);
                    }
                }
            }
            if (sub_resume && LLVMGetTypeKind(LLVMTypeOf(sub)) == LLVMPointerTypeKind) {
                LLVMValueRef sub_i8 = LLVMBuildBitCast(g->builder, sub, sp_i8ptr, "spawn.sub");
                /* A spawned coroutine is detached: no caller awaits it, so
                 * nobody frees its heap frame when it completes. Install the
                 * reaper as its own awaiter step (awaiter = the frame itself, a
                 * non-null marker) so emit_async_complete's awaiter-wake path
                 * re-enqueues (frame, __zan_co_reap) and the driver frees the
                 * frame after the body finishes. */
                LLVMTypeRef hdr = g->co_header_type;
                LLVMBuildStore(g->builder, sub_i8,
                    LLVMBuildStructGEP2(g->builder, hdr, sub_i8, ASYNC_FRAME_AWAITER, "spawn.aw"));
                LLVMBuildStore(g->builder, get_co_reap_fn(g),
                    LLVMBuildStructGEP2(g->builder, hdr, sub_i8, ASYNC_FRAME_AWAITER_STEP, "spawn.aws"));
                LLVMValueRef sched_args[] = { sub_i8, sub_resume };
                zan_call2(g->builder, g->rt_co_ready_type, g->rt_co_ready, sched_args, 2, "");
            }
            emit_release_owned_call_temp(g, sub_arg, sub, locals);
            return LLVMConstInt(sp_i64, 0, 0);
        }

        /* StringBuilder.Append(s) / StringBuilder.ToString() — growable byte
         * buffer with amortised O(1) append (capacity doubling). */
        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS) {
            zan_ast_node_t *sbcallee = expr->call.callee;
            zan_istr_t sbm = sbcallee->member.name;
            zan_type_t *sbt = infer_expr_type(g, sbcallee->member.object, locals);
            if (sbt && sbt->name.len == 13 &&
                memcmp(sbt->name.str, "StringBuilder", 13) == 0) {
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
                        LLVMValueRef s = emit_value_as_cstr(g, v);
                        LLVMValueRef slen = zan_call2(g->builder,
                            LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr }, 1, 0),
                            g->fn_strlen, &s, 1, "sblen");
                        emit_sb_append_bytes(g, sbp, s, slen);
                        if (LLVMGetTypeKind(LLVMTypeOf(v)) == LLVMPointerTypeKind)
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
                    LLVMValueRef bsz = LLVMBuildAdd(g->builder, count, LLVMConstInt(i64, 1, 0), "sbbsz");
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
        if (is_call_to(expr, "Console", "WriteLine") ||
            is_call_to(expr, "Console", "PrintLine")) {
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
                    LLVMTypeRef fn_type = LLVMFunctionType(
                        LLVMVoidTypeInContext(g->ctx),
                        (LLVMTypeRef[]){ LLVMDoubleTypeInContext(g->ctx) },
                        1, 0);
                    zan_call2(g->builder, fn_type, g->rt_print_double, &arg, 1, "");
                } else {
                    /* ensure integer arg is i64 for print_int */
                    arg = emit_widen_i64_for_print(g, arg);
                    LLVMTypeRef fn_type = LLVMFunctionType(
                        LLVMVoidTypeInContext(g->ctx),
                        (LLVMTypeRef[]){ LLVMInt64TypeInContext(g->ctx) },
                        1, 0);
                    LLVMValueRef print_fn = expr_is_ulong(g, arg_ast, locals)
                        ? g->rt_print_uint : g->rt_print_int;
                    zan_call2(g->builder, fn_type, print_fn, &arg, 1, "");
                }
                emit_release_owned_call_temp(g, arg_ast, arg, locals);
            }
            return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
        }

        /* Console.Write (no newline) */
        if (is_call_to(expr, "Console", "Write")) {
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
        if (is_call_to(expr, "Console", "ReadLine")) {
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
            /* get stdin */
            LLVMValueRef stdin_fn = LLVMGetNamedFunction(g->mod, "__acrt_iob_func");
            if (!stdin_fn) {
                LLVMTypeRef iob_args[] = { LLVMInt32TypeInContext(g->ctx) };
                LLVMTypeRef iob_type = LLVMFunctionType(i8ptr, iob_args, 1, 0);
                stdin_fn = LLVMAddFunction(g->mod, "__acrt_iob_func", iob_type);
            }
            LLVMValueRef zero = LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
            LLVMValueRef stdin_ptr = zan_call2(g->builder,
                LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ LLVMInt32TypeInContext(g->ctx) }, 1, 0),
                stdin_fn, &zero, 1, "stdin");
            LLVMValueRef sz = LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 1024, 0);
            LLVMValueRef fgets_args[] = { buf, sz, stdin_ptr };
            zan_call2(g->builder,
                LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, LLVMInt32TypeInContext(g->ctx), i8ptr }, 3, 0),
                fgets_fn, fgets_args, 3, "");
            return buf;
        }

        /* ==== NativeMemory intrinsics ====
         * Raw off-heap memory access for binary IO (ByteBuffer, ZanDB pages,
         * network framing). Addresses travel as nint (i64); every operation
         * lowers to direct loads/stores or libc calls, so none of these values
         * ever enter the ARC string/object machinery. Multi-byte accessors are
         * little-endian and unaligned-safe (align 1). */
        {
            LLVMValueRef nm_out = NULL;
            if (emit_native_memory_call(g, expr, locals, &nm_out))
                return nm_out;
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
        if (is_call_to(expr, "Math", "Sqrt") && expr->call.args.count == 1) {
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
                if (is_call_to(expr, "Math", math1[mi]) && expr->call.args.count == 1) {
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
        if (is_call_to(expr, "Math", "Abs") && expr->call.args.count == 1) {
            LLVMValueRef arg = emit_expr(g, expr->call.args.items[0], locals);
            LLVMTypeRef arg_type = LLVMTypeOf(arg);
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
                /* integer abs: (x ^ (x >> 31)) - (x >> 31) */
                LLVMValueRef shift = LLVMBuildAShr(g->builder, arg,
                    LLVMConstInt(LLVMTypeOf(arg), 31, 0), "sh");
                LLVMValueRef xor = LLVMBuildXor(g->builder, arg, shift, "xor");
                return LLVMBuildSub(g->builder, xor, shift, "abs");
            }
        }

        /* Math.Max(a, b) */
        if (is_call_to(expr, "Math", "Max") && expr->call.args.count == 2) {
            LLVMValueRef a = emit_expr(g, expr->call.args.items[0], locals);
            LLVMValueRef b = emit_expr(g, expr->call.args.items[1], locals);
            LLVMValueRef cmp = LLVMBuildICmp(g->builder, LLVMIntSGT, a, b, "cmp");
            return LLVMBuildSelect(g->builder, cmp, a, b, "max");
        }

        /* Math.Min(a, b) */
        if (is_call_to(expr, "Math", "Min") && expr->call.args.count == 2) {
            LLVMValueRef a = emit_expr(g, expr->call.args.items[0], locals);
            LLVMValueRef b = emit_expr(g, expr->call.args.items[1], locals);
            LLVMValueRef cmp = LLVMBuildICmp(g->builder, LLVMIntSLT, a, b, "cmp");
            return LLVMBuildSelect(g->builder, cmp, a, b, "min");
        }

        /* Math.Pow(base, exp) */
        if (is_call_to(expr, "Math", "Pow") && expr->call.args.count == 2) {
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
        if (is_call_to(expr, "Math", "Floor") && expr->call.args.count == 1) {
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
        if (is_call_to(expr, "Math", "Ceiling") && expr->call.args.count == 1) {
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
        if (is_call_to(expr, "Math", "Round") &&
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
        if (is_call_to(expr, "Convert", "ToDouble") && expr->call.args.count == 1) {
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
                LLVMValueRef endp = LLVMBuildAlloca(g->builder, i8ptr, "endp");
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
                LLVMValueRef moved = LLVMBuildICmp(g->builder, LLVMIntNE, e, s, "moved");
                LLVMValueRef ch = LLVMBuildLoad2(g->builder, i8, e, "ch");
                LLVMValueRef at_end = LLVMBuildICmp(g->builder, LLVMIntEQ, ch,
                    LLVMConstInt(i8, 0, 0), "atend");
                LLVMValueRef ok = LLVMBuildAnd(g->builder, moved, at_end, "ok");
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
                LLVMValueRef isnull = LLVMBuildICmp(g->builder, LLVMIntEQ, s,
                    LLVMConstNull(i8ptr), "isnull");
                /* empty check must not deref null: select null ? true : *s==0 */
                LLVMBasicBlockRef chk_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "sne.chk");
                LLVMBasicBlockRef done_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "sne.done");
                LLVMBasicBlockRef cur_bb = LLVMGetInsertBlock(g->builder);
                LLVMBuildCondBr(g->builder, isnull, done_bb, chk_bb);
                LLVMPositionBuilderAtEnd(g->builder, chk_bb);
                LLVMValueRef ch = LLVMBuildLoad2(g->builder, i8, s, "ch");
                LLVMValueRef isempty = LLVMBuildICmp(g->builder, LLVMIntEQ, ch,
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
                 * placeholders and concatenate pieces with stringified args. */
                zan_istr_t f = expr->call.args.items[0]->str_val;
                LLVMValueRef res = NULL;
                int seg_start = 0;
                for (int i = 0; i < f.len; i++) {
                    if (f.str[i] == '{' && i + 2 < f.len + 1) {
                        int j = i + 1, idx = 0, has = 0;
                        while (j < f.len && f.str[j] >= '0' && f.str[j] <= '9') {
                            idx = idx * 10 + (f.str[j] - '0'); j++; has = 1;
                        }
                        if (has && j < f.len && f.str[j] == '}' &&
                            idx + 1 < expr->call.args.count) {
                            if (i > seg_start) {
                                zan_istr_t seg = { f.str + seg_start, i - seg_start };
                                LLVMValueRef sl = emit_string_literal_rc(g, seg);
                                res = res ? emit_str_concat(g, res, sl) : sl;
                            }
                            LLVMValueRef av = emit_expr(g,
                                expr->call.args.items[idx + 1], locals);
                            LLVMValueRef as = emit_to_cstr(g, av);
                            res = res ? emit_str_concat(g, res, as) : as;
                            i = j;
                            seg_start = j + 1;
                        }
                    }
                }
                if (seg_start < f.len || !res) {
                    zan_istr_t seg = { f.str + seg_start, f.len - seg_start };
                    LLVMValueRef sl = emit_string_literal_rc(g, seg);
                    res = res ? emit_str_concat(g, res, sl) : sl;
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
            
            /* Convert.ToInt32(str) -> atoi */
            if (callee->member.object->kind == AST_IDENTIFIER) {
                zan_istr_t obj_name = callee->member.object->ident.name;
                if (obj_name.len == 7 && memcmp(obj_name.str, "Convert", 7) == 0) {
                    if (((method_name.len == 7 && memcmp(method_name.str, "ToInt32", 7) == 0) ||
                         (method_name.len == 5 && memcmp(method_name.str, "ToInt", 5) == 0)) &&
                        expr->call.args.count == 1) {
                        LLVMValueRef arg = emit_expr(g, expr->call.args.items[0], locals);
                        LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                        LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
                        LLVMValueRef atoi_fn = LLVMGetNamedFunction(g->mod, "atoi");
                        LLVMTypeRef atoi_type;
                        if (!atoi_fn) {
                            atoi_type = LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr }, 1, 0);
                            atoi_fn = LLVMAddFunction(g->mod, "atoi", atoi_type);
                        } else {
                            /* a stdlib `static extern int atoi(...)` may have
                             * declared it already (with an i64 return); call
                             * through the existing declaration's type */
                            atoi_type = LLVMGlobalGetValueType(atoi_fn);
                        }
                        LLVMValueRef parsed = zan_call2(g->builder,
                            atoi_type, atoi_fn, &arg, 1, "atoi");
                        LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
                        if (LLVMTypeOf(parsed) != i64t &&
                            LLVMGetTypeKind(LLVMTypeOf(parsed)) == LLVMIntegerTypeKind)
                            parsed = LLVMBuildSExt(g->builder, parsed, i64t, "atoi64");
                        emit_release_owned_call_temp(
                            g, expr->call.args.items[0], arg, locals);
                        return parsed;
                    }
                    if (method_name.len == 8 && memcmp(method_name.str, "ToString", 8) == 0 &&
                        expr->call.args.count == 1) {
                        LLVMValueRef arg = emit_expr(g, expr->call.args.items[0], locals);
                        LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                        LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
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
                            fmt = LLVMBuildGlobalStringPtr(g->builder, "%lld", "itoa_fmt");
                            num_arg = emit_widen_i64_for_print(g, arg);
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
                LLVMValueRef slen;
                if (expr->call.args.count == 2) {
                    slen = coerce_int_to(g,
                        emit_expr(g, expr->call.args.items[1], locals), i64);
                } else {
                    LLVMValueRef total = zan_call2(g->builder,
                        LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr }, 1, 0),
                        g->fn_strlen, &s, 1, "slen");
                    slen = LLVMBuildSub(g->builder, total, start, "subl");
                }
                LLVMValueRef bufsz = LLVMBuildAdd(g->builder, slen, LLVMConstInt(i64, 1, 0), "bsz");
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
                is_string_expr(g, sc->member.object, locals)) {
                LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                LLVMValueRef s = emit_expr(g, sc->member.object, locals);
                LLVMValueRef sub = emit_expr(g, expr->call.args.items[0], locals);
                LLVMTypeRef strstr_type = LLVMFunctionType(i8ptr,
                    (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0);
                LLVMValueRef strstr_fn = LLVMGetNamedFunction(g->mod, "strstr");
                if (!strstr_fn)
                    strstr_fn = LLVMAddFunction(g->mod, "strstr", strstr_type);
                LLVMValueRef ss_args[] = { s, sub };
                LLVMValueRef hit = zan_call2(g->builder, strstr_type,
                    strstr_fn, ss_args, 2, "ss");
                LLVMValueRef res = LLVMBuildICmp(g->builder, LLVMIntNE, hit,
                    LLVMConstPointerNull(i8ptr), "ctn");
                emit_release_owned_call_temp(g, sc->member.object, s, locals);
                emit_release_owned_call_temp(g, expr->call.args.items[0], sub, locals);
                return res;
            }
        }

        /* str.IndexOf(sub) / str.LastIndexOf(sub) -> i64 byte index or -1. */
        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS) {
            zan_ast_node_t *sc = expr->call.callee;
            zan_istr_t sm = sc->member.name;
            int is_idx = (sm.len == 7 && memcmp(sm.str, "IndexOf", 7) == 0);
            int is_lidx = (sm.len == 11 && memcmp(sm.str, "LastIndexOf", 11) == 0);
            if ((is_idx || is_lidx) && expr->call.args.count == 1 &&
                is_string_expr(g, sc->member.object, locals) &&
                is_string_expr(g, expr->call.args.items[0], locals)) {
                LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                LLVMValueRef s = emit_expr(g, sc->member.object, locals);
                LLVMValueRef sub = emit_expr(g, expr->call.args.items[0], locals);
                LLVMValueRef res;
                if (is_idx) {
                    LLVMTypeRef strstr_type = LLVMFunctionType(i8ptr,
                        (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0);
                    LLVMValueRef strstr_fn = get_libc_fn(g, "strstr", strstr_type);
                    LLVMValueRef ss_args[] = { s, sub };
                    LLVMValueRef hit = zan_call2(g->builder, strstr_type,
                        strstr_fn, ss_args, 2, "ss");
                    LLVMValueRef diff = LLVMBuildSub(g->builder,
                        LLVMBuildPtrToInt(g->builder, hit, i64, "hi"),
                        LLVMBuildPtrToInt(g->builder, s, i64, "si"), "diff");
                    LLVMValueRef missed = LLVMBuildICmp(g->builder, LLVMIntEQ, hit,
                        LLVMConstPointerNull(i8ptr), "missed");
                    res = LLVMBuildSelect(g->builder, missed,
                        LLVMConstInt(i64, (uint64_t)-1, 1), diff, "idx");
                } else {
                    LLVMValueRef lif = get_str_last_index_of_fn(g);
                    LLVMValueRef li_args[] = { s, sub };
                    res = zan_call2(g->builder, LLVMGlobalGetValueType(lif),
                        lif, li_args, 2, "lidx");
                }
                emit_release_owned_call_temp(g, sc->member.object, s, locals);
                emit_release_owned_call_temp(g, expr->call.args.items[0], sub, locals);
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
                is_string_expr(g, expr->call.args.items[0], locals)) {
                LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
                LLVMTypeRef i8ptr = LLVMPointerType(i8, 0);
                LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
                LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                LLVMTypeRef strlen_ty = LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr }, 1, 0);
                LLVMValueRef s = emit_expr(g, sc->member.object, locals);
                LLVMValueRef p = emit_expr(g, expr->call.args.items[0], locals);
                LLVMValueRef lp = zan_call2(g->builder, strlen_ty, g->fn_strlen, &p, 1, "lp");
                LLVMValueRef res;
                if (is_sw) {
                    LLVMTypeRef strncmp_ty = LLVMFunctionType(i32,
                        (LLVMTypeRef[]){ i8ptr, i8ptr, i64 }, 3, 0);
                    LLVMValueRef strncmp_fn = get_libc_fn(g, "strncmp", strncmp_ty);
                    LLVMValueRef nc_args[] = { s, p, lp };
                    LLVMValueRef cmp = zan_call2(g->builder, strncmp_ty,
                        strncmp_fn, nc_args, 3, "swcmp");
                    res = LLVMBuildICmp(g->builder, LLVMIntEQ, cmp,
                        LLVMConstInt(i32, 0, 0), "sw");
                } else {
                    LLVMValueRef ls = zan_call2(g->builder, strlen_ty, g->fn_strlen, &s, 1, "ls");
                    LLVMValueRef off = LLVMBuildSub(g->builder, ls, lp, "off");
                    LLVMValueRef fits = LLVMBuildICmp(g->builder, LLVMIntSGE, off,
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
                    LLVMValueRef eq = LLVMBuildICmp(g->builder, LLVMIntEQ, cmp,
                        LLVMConstInt(i32, 0, 0), "eweq");
                    res = LLVMBuildAnd(g->builder, fits, eq, "ew");
                }
                emit_release_owned_call_temp(g, sc->member.object, s, locals);
                emit_release_owned_call_temp(g, expr->call.args.items[0], p, locals);
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
                is_string_expr(g, expr->call.args.items[0], locals) &&
                is_string_expr(g, expr->call.args.items[1], locals)) {
                LLVMValueRef s = emit_expr(g, sc->member.object, locals);
                LLVMValueRef from = emit_expr(g, expr->call.args.items[0], locals);
                LLVMValueRef to = emit_expr(g, expr->call.args.items[1], locals);
                LLVMValueRef rf = get_str_replace_fn(g);
                LLVMValueRef rargs[] = { s, from, to };
                LLVMValueRef res = zan_call2(g->builder,
                    LLVMGlobalGetValueType(rf), rf, rargs, 3, "repl");
                emit_release_owned_call_temp(g, sc->member.object, s, locals);
                emit_release_owned_call_temp(g, expr->call.args.items[0], from, locals);
                emit_release_owned_call_temp(g, expr->call.args.items[1], to, locals);
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
                is_string_expr(g, sc->member.object, locals)) {
                LLVMValueRef s = emit_expr(g, sc->member.object, locals);
                LLVMValueRef sep = emit_expr(g, expr->call.args.items[0], locals);
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
                emit_release_owned_call_temp(g, expr->call.args.items[0], sep, locals);
                return lst;
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
                        LLVMValueRef bufsz = LLVMBuildAdd(g->builder, n,
                            LLVMConstInt(i64, 1, 0), "bsz");
                        LLVMValueRef buf = emit_string_alloc_rc(g, bufsz);
                        LLVMValueRef mcargs[] = { buf, v, bufsz };
                        zan_call2(g->builder, memcpy_ty, memcpy_fn, mcargs, 3, "");
                        emit_release_owned_call_temp(g, sc->member.object, v, locals);
                        return buf;
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
                    } else if (rt_ty->kind == TYPE_ULONG) {
                        fmt = LLVMBuildGlobalStringPtr(g->builder, "%llu", "utoa_fmt");
                        num_arg = emit_widen_i64_for_print(g, v);
                    } else {
                        fmt = LLVMBuildGlobalStringPtr(g->builder, "%lld", "itoa_fmt");
                        num_arg = emit_widen_i64_for_print(g, v);
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
        if (is_call_to(expr, "Environment", "ArgCount") && expr->call.args.count == 0) {
            LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
            LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
            LLVMValueRef g_argc = LLVMGetNamedGlobal(g->mod, "__zan_argc");
            if (!g_argc) {
                g_argc = LLVMAddGlobal(g->mod, i32, "__zan_argc");
                LLVMSetInitializer(g_argc, LLVMConstInt(i32, 0, 0));
            }
            LLVMValueRef ac = LLVMBuildLoad2(g->builder, i32, g_argc, "argc");
            LLVMValueRef ac64 = LLVMBuildSExt(g->builder, ac, i64, "argc64");
            return LLVMBuildSub(g->builder, ac64, LLVMConstInt(i64, 1, 0), "nargs");
        }

        /* Environment.ArgAt(i) -> string : the (i+1)-th argv entry, so index 0
         * is the first user argument. */
        if (is_call_to(expr, "Environment", "ArgAt") && expr->call.args.count == 1) {
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMTypeRef i8ptrptr = LLVMPointerType(i8ptr, 0);
            LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
            zan_ast_node_t *idx_ast = expr->call.args.items[0];
            LLVMValueRef idx = emit_expr(g, idx_ast, locals);
            LLVMValueRef idx1 = LLVMBuildAdd(g->builder, idx, LLVMConstInt(i64, 1, 0), "argi");
            LLVMValueRef g_argv = LLVMGetNamedGlobal(g->mod, "__zan_argv");
            if (!g_argv) {
                g_argv = LLVMAddGlobal(g->mod, i8ptrptr, "__zan_argv");
                LLVMSetInitializer(g_argv, LLVMConstNull(i8ptrptr));
            }
            LLVMValueRef argv = LLVMBuildLoad2(g->builder, i8ptrptr, g_argv, "argv");
            LLVMValueRef slot = LLVMBuildGEP2(g->builder, i8ptr, argv, &idx1, 1, "argslot");
            emit_release_owned_call_temp(g, idx_ast, idx, locals);
            return LLVMBuildLoad2(g->builder, i8ptr, slot, "arg");
        }

        /* File.ReadAllText(path) -> string */
        if (is_call_to(expr, "File", "ReadAllText") && expr->call.args.count == 1) {
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
            emit_io_abort_if(g, LLVMBuildICmp(g->builder, LLVMIntNE, se_end, LLVMConstInt(i32, 0, 0), "se_end.err"),
                "cannot read file\n");
            LLVMValueRef size = zan_call2(g->builder,
                LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr }, 1, 0), ftell_fn, &fp, 1, "sz");
            emit_io_abort_if(g, LLVMBuildICmp(g->builder, LLVMIntSLT, size, LLVMConstInt(i64, 0, 0), "sz.err"),
                "cannot read file\n");
            /* seek back to start */
            LLVMValueRef seek_start_args[] = { fp, LLVMConstInt(i64, 0, 0), LLVMConstInt(i32, 0, 0) };
            LLVMValueRef se_start = zan_call2(g->builder, LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr, i64, i32 }, 3, 0),
                fseek_fn, seek_start_args, 3, "se_start");
            emit_io_abort_if(g, LLVMBuildICmp(g->builder, LLVMIntNE, se_start, LLVMConstInt(i32, 0, 0), "se_start.err"),
                "cannot read file\n");
            /* allocate buffer (size+1 for null terminator) */
            LLVMValueRef buf_size = LLVMBuildAdd(g->builder, size, LLVMConstInt(i64, 1, 0), "bsz");
            LLVMValueRef buf = emit_string_alloc_rc(g, buf_size);
            /* read file; require the full byte count */
            LLVMValueRef fread_args[] = { buf, LLVMConstInt(i64, 1, 0), size, fp };
            LLVMValueRef got = zan_call2(g->builder, LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr, i64, i64, i8ptr }, 4, 0),
                fread_fn, fread_args, 4, "got");
            emit_io_abort_if(g, LLVMBuildICmp(g->builder, LLVMIntNE, got, size, "got.err"),
                "cannot read file\n");
            /* null terminate */
            LLVMValueRef end_ptr = LLVMBuildGEP2(g->builder, LLVMInt8TypeInContext(g->ctx), buf, &size, 1, "end");
            LLVMBuildStore(g->builder, LLVMConstInt(LLVMInt8TypeInContext(g->ctx), 0, 0), end_ptr);
            /* close */
            LLVMValueRef rd_close = zan_call2(g->builder, LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr }, 1, 0),
                fclose_fn, &fp, 1, "rd_close");
            emit_io_abort_if(g, LLVMBuildICmp(g->builder, LLVMIntNE, rd_close, LLVMConstInt(i32, 0, 0), "rd_close.err"),
                "cannot read file\n");
            emit_release_owned_call_temp(g, path_ast, path_arg, locals);
            return buf;
        }

        /* File.WriteAllText(path, content) */
        if (is_call_to(expr, "File", "WriteAllText") && expr->call.args.count == 2) {
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
            emit_io_abort_if(g, LLVMBuildICmp(g->builder, LLVMIntSLT, put, LLVMConstInt(i32, 0, 0), "put.err"),
                "cannot write file\n");
            LLVMValueRef wr_close = zan_call2(g->builder, LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr }, 1, 0),
                fclose_fn, &fp, 1, "wr_close");
            emit_io_abort_if(g, LLVMBuildICmp(g->builder, LLVMIntNE, wr_close, LLVMConstInt(i32, 0, 0), "wr_close.err"),
                "cannot write file\n");
            emit_release_owned_call_temp(g, path_ast, path_arg, locals);
            emit_release_owned_call_temp(g, content_ast, content_arg, locals);
            return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
        }

        /* Path.GetFileName(path), Path.GetExtension(path), Path.Combine(a,b) */
        if (is_call_to(expr, "Path", "GetFileName") && expr->call.args.count == 1) {
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
            /* pick the one that's later (or non-null) */
            LLVMValueRef s_null = LLVMBuildICmp(g->builder, LLVMIntEQ, slash, LLVMConstNull(i8ptr), "snull");
            LLVMValueRef b_null = LLVMBuildICmp(g->builder, LLVMIntEQ, bslash, LLVMConstNull(i8ptr), "bnull");
            /* if both null, return path; else return max(slash,bslash)+1 */
            LLVMValueRef s_gt = LLVMBuildICmp(g->builder, LLVMIntUGT, slash, bslash, "sgt");
            LLVMValueRef best = LLVMBuildSelect(g->builder, s_gt, slash, bslash, "best");
            LLVMValueRef both_null = LLVMBuildAnd(g->builder, s_null, b_null, "bothnull");
            LLVMValueRef plus1 = LLVMBuildGEP2(g->builder, LLVMInt8TypeInContext(g->ctx), best,
                (LLVMValueRef[]){ LLVMConstInt(i32t, 1, 0) }, 1, "p1");
            return LLVMBuildSelect(g->builder, both_null, path_val, plus1, "fname");
        }

        if (is_call_to(expr, "Path", "GetExtension") && expr->call.args.count == 1) {
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
            zan_ast_node_t *path_ast = expr->call.args.items[0];
            LLVMValueRef path_val = emit_expr(g, path_ast, locals);
            LLVMValueRef strrchr_fn = LLVMGetNamedFunction(g->mod, "strrchr");
            LLVMValueRef dot_args[] = { path_val, LLVMConstInt(i32t, '.', 0) };
            LLVMValueRef dot = zan_call2(g->builder,
                LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i32t }, 2, 0),
                strrchr_fn, dot_args, 2, "dot");
            /* if no dot, return empty string */
            LLVMValueRef is_null = LLVMBuildICmp(g->builder, LLVMIntEQ, dot, LLVMConstNull(i8ptr), "dnull");
            LLVMValueRef empty = emit_string_literal_rc(g, (zan_istr_t){ "", 0 });
            return LLVMBuildSelect(g->builder, is_null, empty, dot, "ext");
        }

        if (is_call_to(expr, "Path", "Combine") && expr->call.args.count == 2) {
            LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMValueRef a = emit_expr(g, expr->call.args.items[0], locals);
            LLVMValueRef b = emit_expr(g, expr->call.args.items[1], locals);
            /* len = strlen(a) + 1 + strlen(b) + 1 */
            LLVMTypeRef strlen_type = LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr }, 1, 0);
            LLVMValueRef len_a = zan_call2(g->builder, strlen_type, g->fn_strlen, &a, 1, "la");
            LLVMValueRef len_b = zan_call2(g->builder, strlen_type, g->fn_strlen, &b, 1, "lb");
            LLVMValueRef total = LLVMBuildAdd(g->builder, len_a, len_b, "t");
            total = LLVMBuildAdd(g->builder, total, LLVMConstInt(i64, 2, 0), "t2"); /* +separator+null */
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
        if (is_call_to(expr, "File", "AppendAllText") && expr->call.args.count == 2) {
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
        if (is_call_to(expr, "File", "Exists") && expr->call.args.count == 1) {
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
            LLVMValueRef is_null = LLVMBuildICmp(g->builder, LLVMIntNE, fp,
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
        if (is_call_to(expr, "File", "Delete") && expr->call.args.count == 1) {
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
        if (is_call_to(expr, "File", "Move") && expr->call.args.count == 2) {
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
        if (is_call_to(expr, "File", "Copy") && expr->call.args.count == 2) {
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
            /* get size */
            LLVMValueRef seek_end[] = { sfp, LLVMConstInt(i64, 0, 0), LLVMConstInt(i32, 2, 0) };
            zan_call2(g->builder, LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr, i64, i32 }, 3, 0),
                fseek_fn, seek_end, 3, "");
            LLVMValueRef sz = zan_call2(g->builder,
                LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr }, 1, 0), ftell_fn, &sfp, 1, "sz");
            LLVMValueRef seek_start[] = { sfp, LLVMConstInt(i64, 0, 0), LLVMConstInt(i32, 0, 0) };
            zan_call2(g->builder, LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr, i64, i32 }, 3, 0),
                fseek_fn, seek_start, 3, "");
            /* allocate buffer */
            LLVMValueRef buf = emit_string_alloc_rc(g, sz);
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
        if (is_call_to(expr, "File", "GetSize") && expr->call.args.count == 1) {
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
        if (is_call_to(expr, "Directory", "Exists") && expr->call.args.count == 1) {
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
                LLVMValueRef not_invalid = LLVMBuildICmp(g->builder, LLVMIntNE, attrs,
                    LLVMConstInt(i32, 0xFFFFFFFF, 0), "noinv");
                LLVMValueRef is_dir = LLVMBuildAnd(g->builder, attrs,
                    LLVMConstInt(i32, 0x10, 0), "isdir");
                LLVMValueRef is_dir_bool = LLVMBuildICmp(g->builder, LLVMIntNE, is_dir,
                    LLVMConstInt(i32, 0, 0), "isdirb");
                result = LLVMBuildAnd(g->builder, not_invalid, is_dir_bool, "dexist");
            } else {
                LLVMValueRef opendir_fn = LLVMGetNamedFunction(g->mod, "opendir");
                if (!opendir_fn) {
                    LLVMTypeRef ft = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr }, 1, 0);
                    opendir_fn = LLVMAddFunction(g->mod, "opendir", ft);
                }
                LLVMValueRef dirp = zan_call2(g->builder,
                    LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr }, 1, 0),
                    opendir_fn, &path_arg, 1, "dirp");
                result = LLVMBuildICmp(g->builder, LLVMIntNE, dirp, LLVMConstNull(i8ptr), "dopen");
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
        if (is_call_to(expr, "Environment", "ExeDir") && expr->call.args.count == 0) {
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
        if (is_call_to(expr, "Directory", "ListNames") && expr->call.args.count == 1) {
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
        if (is_call_to(expr, "Directory", "CreateDirectory") && expr->call.args.count == 1) {
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
        if (is_call_to(expr, "Directory", "Delete") && expr->call.args.count == 1) {
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
        if (is_call_to(expr, "Directory", "GetCurrentDirectory") && expr->call.args.count == 0) {
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
        if (is_call_to(expr, "Directory", "SetCurrentDirectory") && expr->call.args.count == 1) {
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
        if (is_call_to(expr, "Path", "GetDirectoryName") && expr->call.args.count == 1) {
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
            LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
            LLVMValueRef path_val = emit_expr(g, expr->call.args.items[0], locals);
            /* strlen */
            LLVMValueRef len = zan_call2(g->builder,
                LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr }, 1, 0), g->fn_strlen, &path_val, 1, "plen");
            /* allocate copy */
            LLVMValueRef bsz = LLVMBuildAdd(g->builder, len, LLVMConstInt(i64, 1, 0), "bsz");
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
            LLVMValueRef sl_null = LLVMBuildICmp(g->builder, LLVMIntEQ, slash, LLVMConstNull(i8ptr), "snul");
            LLVMValueRef pick = LLVMBuildSelect(g->builder, sl_null, bslash, slash, "pk1");
            LLVMValueRef bs_null = LLVMBuildICmp(g->builder, LLVMIntEQ, bslash, LLVMConstNull(i8ptr), "bnul");
            LLVMValueRef sep_ptr = LLVMBuildSelect(g->builder, bs_null, pick,
                LLVMBuildSelect(g->builder,
                    LLVMBuildICmp(g->builder, LLVMIntUGT, bslash, pick, "bgt"),
                    bslash, pick, "pk2"), "sep");
            /* truncate at separator */
            LLVMValueRef found = LLVMBuildICmp(g->builder, LLVMIntNE, sep_ptr, LLVMConstNull(i8ptr), "fnd");
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
        if (is_call_to(expr, "Path", "HasExtension") && expr->call.args.count == 1) {
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
            LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
            LLVMValueRef path_val = emit_expr(g, expr->call.args.items[0], locals);
            LLVMValueRef strrchr_fn = LLVMGetNamedFunction(g->mod, "strrchr");
            LLVMValueRef dot_args[] = { path_val, LLVMConstInt(i32t, '.', 0) };
            LLVMValueRef dot = zan_call2(g->builder,
                LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i32t }, 2, 0),
                strrchr_fn, dot_args, 2, "dot");
            LLVMValueRef has = LLVMBuildICmp(g->builder, LLVMIntNE, dot, LLVMConstNull(i8ptr), "hasext");
            emit_release_owned_call_temp(g, expr->call.args.items[0], path_val, locals);
            return LLVMBuildZExt(g->builder, has, i64, "he");
        }

        /* Path.GetTempPath() -> string */
        if (is_call_to(expr, "Path", "GetTempPath") && expr->call.args.count == 0) {
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
                LLVMValueRef isnull = LLVMBuildICmp(g->builder, LLVMIntEQ, env, LLVMConstNull(i8ptr), "tnull");
                LLVMValueRef deflt = LLVMBuildGlobalStringPtr(g->builder, "/tmp/", "tmpdef");
                LLVMValueRef src = LLVMBuildSelect(g->builder, isnull, deflt, env, "tmpsrc");
                LLVMValueRef cpy_args[] = { buf, src };
                zan_call2(g->builder, LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0),
                    g->fn_strcpy, cpy_args, 2, "");
            }
            return buf;
        }

        /* Path.GetFileNameWithoutExtension(path) -> string */
        if (is_call_to(expr, "Path", "GetFileNameWithoutExtension") && expr->call.args.count == 1) {
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
            LLVMValueRef sl_null = LLVMBuildICmp(g->builder, LLVMIntEQ, slash, LLVMConstNull(i8ptr), "snul");
            LLVMValueRef bs_null = LLVMBuildICmp(g->builder, LLVMIntEQ, bslash, LLVMConstNull(i8ptr), "bnul");
            LLVMValueRef best = LLVMBuildSelect(g->builder, sl_null, bslash,
                LLVMBuildSelect(g->builder, bs_null, slash,
                    LLVMBuildSelect(g->builder, LLVMBuildICmp(g->builder, LLVMIntUGT, bslash, slash, "bgt"),
                        bslash, slash, "mx"), "pk"), "sep");
            LLVMValueRef has_sep = LLVMBuildICmp(g->builder, LLVMIntNE, best, LLVMConstNull(i8ptr), "hs");
            /* filename starts after separator+1, or is the whole path */
            LLVMValueRef after = LLVMBuildGEP2(g->builder, i8, best, &(LLVMValueRef){LLVMConstInt(i64, 1, 0)}, 1, "aft");
            LLVMValueRef fname = LLVMBuildSelect(g->builder, has_sep, after, path_val, "fn");
            /* make a copy, then truncate at last dot */
            LLVMValueRef flen = zan_call2(g->builder,
                LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr }, 1, 0), g->fn_strlen, &fname, 1, "flen");
            LLVMValueRef bsz = LLVMBuildAdd(g->builder, flen, LLVMConstInt(i64, 1, 0), "bsz");
            LLVMValueRef buf = emit_string_alloc_rc(g, bsz);
            LLVMValueRef cpy_args[] = { buf, fname };
            zan_call2(g->builder, LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0),
                g->fn_strcpy, cpy_args, 2, "");
            LLVMValueRef dot_args[] = { buf, LLVMConstInt(i32t, '.', 0) };
            LLVMValueRef dot = zan_call2(g->builder,
                LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i32t }, 2, 0),
                strrchr_fn, dot_args, 2, "dot");
            LLVMValueRef has_dot = LLVMBuildICmp(g->builder, LLVMIntNE, dot, LLVMConstNull(i8ptr), "hd");
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
                if (ltype && ltype->name.len == 4 &&
                    memcmp(ltype->name.str, "List", 4) == 0) {
                    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                    /* load list pointer (works for local vars and fields) */
                    LLVMValueRef raw_ptr = emit_expr(g, lobj, locals);
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
                    LLVMValueRef need_grow = LLVMBuildICmp(g->builder, LLVMIntUGE, count, cap, "grow");
                    LLVMBasicBlockRef grow_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "list.grow");
                    LLVMBasicBlockRef add_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "list.add");
                    LLVMBuildCondBr(g->builder, need_grow, grow_bb, add_bb);
                    /* grow block: double capacity, realloc */
                    LLVMPositionBuilderAtEnd(g->builder, grow_bb);
                    LLVMValueRef new_cap = LLVMBuildMul(g->builder, cap, LLVMConstInt(i64, 2, 0), "ncap");
                    LLVMBuildStore(g->builder, new_cap, cap_ptr);
                    LLVMValueRef data_field = LLVMBuildStructGEP2(g->builder, g->list_struct_type, list_ptr, 2, "df");
                    LLVMValueRef old_data = LLVMBuildLoad2(g->builder, LLVMPointerType(i64, 0), data_field, "od");
                    LLVMValueRef old_data_raw = LLVMBuildBitCast(g->builder, old_data, i8ptr, "odr");
                    LLVMValueRef new_size = LLVMBuildMul(g->builder, new_cap, LLVMConstInt(i64, 8, 0), "nsz");
                    LLVMValueRef realloc_args[] = { old_data_raw, new_size };
                    LLVMValueRef new_data_raw = zan_call2(g->builder,
                        LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i64 }, 2, 0),
                        g->fn_realloc, realloc_args, 2, "nd");
                    LLVMValueRef new_data = LLVMBuildBitCast(g->builder, new_data_raw, LLVMPointerType(i64, 0), "ndt");
                    LLVMBuildStore(g->builder, new_data, data_field);
                    LLVMBuildBr(g->builder, add_bb);
                    /* add block: store value at data[count], increment count */
                    LLVMPositionBuilderAtEnd(g->builder, add_bb);
                    LLVMValueRef data_field2 = LLVMBuildStructGEP2(g->builder, g->list_struct_type, list_ptr, 2, "df2");
                    LLVMValueRef data = LLVMBuildLoad2(g->builder, LLVMPointerType(i64, 0), data_field2, "d");
                    LLVMValueRef count2 = LLVMBuildLoad2(g->builder, i64, count_ptr, "cnt2");
                    LLVMValueRef elem_ptr = LLVMBuildGEP2(g->builder, i64, data, &count2, 1, "ep");
                    /* emit the value to add */
                    LLVMValueRef val = emit_expr(g, expr->call.args.items[0], locals);
                    emit_collection_slot_store(g, container_elem_type(ltype), i64, elem_ptr,
                        val, expr->call.args.items[0], locals, 0);
                    /* count++ */
                    LLVMValueRef new_count = LLVMBuildAdd(g->builder, count2, LLVMConstInt(i64, 1, 0), "nc");
                    LLVMBuildStore(g->builder, new_count, count_ptr);
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
                if (ltype && ltype->name.len == 4 &&
                    memcmp(ltype->name.str, "List", 4) == 0) {
                    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                    LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
                    LLVMTypeRef i64ptr = LLVMPointerType(i64, 0);
                    LLVMTypeRef list_pt = LLVMPointerType(g->list_struct_type, 0);
                    zan_type_t *elem_type = container_elem_type(ltype);
                    /* self + other list pointers */
                    LLVMValueRef self_raw = emit_expr(g, lobj, locals);
                    LLVMValueRef self_ptr = LLVMBuildBitCast(g->builder, self_raw, list_pt, "ar.self");
                    LLVMValueRef other_raw = emit_expr(g, expr->call.args.items[0], locals);
                    LLVMValueRef other_ptr = LLVMBuildBitCast(g->builder, other_raw, list_pt, "ar.other");
                    /* skip entirely when other is null */
                    LLVMValueRef other_i = LLVMBuildPtrToInt(g->builder, other_ptr, i64, "ar.oi");
                    LLVMValueRef is_null = LLVMBuildICmp(g->builder, LLVMIntEQ, other_i,
                        LLVMConstInt(i64, 0, 0), "ar.null");
                    LLVMBasicBlockRef ar_body0 = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "ar.enter");
                    LLVMBasicBlockRef ar_done = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "ar.done");
                    LLVMBuildCondBr(g->builder, is_null, ar_done, ar_body0);
                    LLVMPositionBuilderAtEnd(g->builder, ar_body0);
                    /* snapshot other's count (so self.AddRange(self) terminates) */
                    LLVMValueRef ocnt_ptr = LLVMBuildStructGEP2(g->builder, g->list_struct_type, other_ptr, 0, "ar.ocp");
                    LLVMValueRef ocnt = LLVMBuildLoad2(g->builder, i64, ocnt_ptr, "ar.ocnt");
                    LLVMValueRef idx_a = LLVMBuildAlloca(g->builder, i64, "ar.i");
                    LLVMBuildStore(g->builder, LLVMConstInt(i64, 0, 0), idx_a);
                    LLVMBasicBlockRef c_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "ar.cond");
                    LLVMBasicBlockRef b_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "ar.step");
                    LLVMBuildBr(g->builder, c_bb);
                    LLVMPositionBuilderAtEnd(g->builder, c_bb);
                    LLVMValueRef ci = LLVMBuildLoad2(g->builder, i64, idx_a, "ar.ci");
                    LLVMValueRef more = LLVMBuildICmp(g->builder, LLVMIntULT, ci, ocnt, "ar.more");
                    LLVMBuildCondBr(g->builder, more, b_bb, ar_done);
                    LLVMPositionBuilderAtEnd(g->builder, b_bb);
                    /* read other[i] (reload data each step in case other == self grew) */
                    LLVMValueRef ci2 = LLVMBuildLoad2(g->builder, i64, idx_a, "ar.ci2");
                    LLVMValueRef odata_field = LLVMBuildStructGEP2(g->builder, g->list_struct_type, other_ptr, 2, "ar.odf");
                    LLVMValueRef odata = LLVMBuildLoad2(g->builder, i64ptr, odata_field, "ar.od");
                    LLVMValueRef oslot = LLVMBuildGEP2(g->builder, i64, odata, &ci2, 1, "ar.os");
                    LLVMValueRef rawv = LLVMBuildLoad2(g->builder, i64, oslot, "ar.rv");
                    /* grow self if full (mirrors List.Add) */
                    LLVMValueRef s_cnt_ptr = LLVMBuildStructGEP2(g->builder, g->list_struct_type, self_ptr, 0, "ar.scp");
                    LLVMValueRef s_cnt = LLVMBuildLoad2(g->builder, i64, s_cnt_ptr, "ar.sc");
                    LLVMValueRef s_cap_ptr = LLVMBuildStructGEP2(g->builder, g->list_struct_type, self_ptr, 1, "ar.scapp");
                    LLVMValueRef s_cap = LLVMBuildLoad2(g->builder, i64, s_cap_ptr, "ar.scap");
                    LLVMValueRef need = LLVMBuildICmp(g->builder, LLVMIntUGE, s_cnt, s_cap, "ar.grow");
                    LLVMBasicBlockRef g_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "ar.grow");
                    LLVMBasicBlockRef s_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "ar.store");
                    LLVMBuildCondBr(g->builder, need, g_bb, s_bb);
                    LLVMPositionBuilderAtEnd(g->builder, g_bb);
                    /* newcap = cap == 0 ? 4 : cap * 2 */
                    LLVMValueRef cap_zero = LLVMBuildICmp(g->builder, LLVMIntEQ, s_cap,
                        LLVMConstInt(i64, 0, 0), "ar.cz");
                    LLVMValueRef dbl = LLVMBuildMul(g->builder, s_cap, LLVMConstInt(i64, 2, 0), "ar.dbl");
                    LLVMValueRef ncap = LLVMBuildSelect(g->builder, cap_zero,
                        LLVMConstInt(i64, 4, 0), dbl, "ar.ncap");
                    LLVMBuildStore(g->builder, ncap, s_cap_ptr);
                    LLVMValueRef s_df = LLVMBuildStructGEP2(g->builder, g->list_struct_type, self_ptr, 2, "ar.sdf");
                    LLVMValueRef old_data = LLVMBuildLoad2(g->builder, i64ptr, s_df, "ar.oldd");
                    LLVMValueRef old_raw = LLVMBuildBitCast(g->builder, old_data, i8ptr, "ar.oldr");
                    LLVMValueRef nsz = LLVMBuildMul(g->builder, ncap, LLVMConstInt(i64, 8, 0), "ar.nsz");
                    LLVMValueRef re_args[] = { old_raw, nsz };
                    LLVMValueRef nd_raw = zan_call2(g->builder,
                        LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i64 }, 2, 0),
                        g->fn_realloc, re_args, 2, "ar.nd");
                    LLVMValueRef nd = LLVMBuildBitCast(g->builder, nd_raw, i64ptr, "ar.ndt");
                    LLVMBuildStore(g->builder, nd, s_df);
                    LLVMBuildBr(g->builder, s_bb);
                    LLVMPositionBuilderAtEnd(g->builder, s_bb);
                    /* store raw value at self.data[count] */
                    LLVMValueRef s_df2 = LLVMBuildStructGEP2(g->builder, g->list_struct_type, self_ptr, 2, "ar.sdf2");
                    LLVMValueRef s_data = LLVMBuildLoad2(g->builder, i64ptr, s_df2, "ar.sd");
                    LLVMValueRef s_cnt2 = LLVMBuildLoad2(g->builder, i64, s_cnt_ptr, "ar.sc2");
                    LLVMValueRef s_slot = LLVMBuildGEP2(g->builder, i64, s_data, &s_cnt2, 1, "ar.ss");
                    LLVMBuildStore(g->builder, rawv, s_slot);
                    /* both lists now reference the element: retain if managed */
                    if (is_rc_managed_type(elem_type)) {
                        LLVMTypeRef mt = map_type(g, elem_type);
                        if (LLVMGetTypeKind(mt) == LLVMPointerTypeKind) {
                            LLVMValueRef pv = LLVMBuildIntToPtr(g->builder, rawv, mt, "ar.pv");
                            emit_rc_retain_for_type(g, elem_type, pv);
                        }
                    }
                    LLVMValueRef s_nc = LLVMBuildAdd(g->builder, s_cnt2, LLVMConstInt(i64, 1, 0), "ar.snc");
                    LLVMBuildStore(g->builder, s_nc, s_cnt_ptr);
                    LLVMValueRef ni = LLVMBuildAdd(g->builder, ci2, LLVMConstInt(i64, 1, 0), "ar.ni");
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
                if (ltype && ltype->name.len == 4 && memcmp(ltype->name.str, "List", 4) == 0) {
                    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                    LLVMValueRef raw_ptr = emit_expr(g, lobj, locals);
                    LLVMValueRef list_ptr = LLVMBuildBitCast(g->builder, raw_ptr,
                        LLVMPointerType(g->list_struct_type, 0), "lptr");
                    LLVMValueRef count_ptr = LLVMBuildStructGEP2(g->builder, g->list_struct_type, list_ptr, 0, "cntp");
                    LLVMValueRef count = LLVMBuildLoad2(g->builder, i64, count_ptr, "cnt");
                    LLVMValueRef data_field = LLVMBuildStructGEP2(g->builder, g->list_struct_type, list_ptr, 2, "df");
                    LLVMValueRef data = LLVMBuildLoad2(g->builder, LLVMPointerType(i64, 0), data_field, "data");
                    LLVMValueRef idx_a = LLVMBuildAlloca(g->builder, i64, "lc");
                    LLVMBuildStore(g->builder, LLVMConstInt(i64, 0, 0), idx_a);
                    LLVMBasicBlockRef c_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "lc.cond");
                    LLVMBasicBlockRef b_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "lc.body");
                    LLVMBasicBlockRef d_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "lc.done");
                    LLVMBuildBr(g->builder, c_bb);
                    LLVMPositionBuilderAtEnd(g->builder, c_bb);
                    LLVMValueRef ci = LLVMBuildLoad2(g->builder, i64, idx_a, "ci");
                    LLVMValueRef more = LLVMBuildICmp(g->builder, LLVMIntULT, ci, count, "more");
                    LLVMBuildCondBr(g->builder, more, b_bb, d_bb);
                    LLVMPositionBuilderAtEnd(g->builder, b_bb);
                    LLVMValueRef ci2 = LLVMBuildLoad2(g->builder, i64, idx_a, "ci2");
                    LLVMValueRef slot = LLVMBuildGEP2(g->builder, i64, data, &ci2, 1, "sl");
                    LLVMValueRef val = LLVMBuildLoad2(g->builder, i64, slot, "sv");
                    emit_collection_release_raw_slot(g, container_elem_type(ltype), val, i64);
                    LLVMValueRef ni = LLVMBuildAdd(g->builder, ci2, LLVMConstInt(i64, 1, 0), "ni");
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
                if (ltype && ltype->name.len == 4 && memcmp(ltype->name.str, "List", 4) == 0) {
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
                    LLVMValueRef removed_ptr = LLVMBuildGEP2(g->builder, i64, data, &idx, 1, "rmp");
                    LLVMValueRef removed = LLVMBuildLoad2(g->builder, i64, removed_ptr, "rmv");
                    emit_collection_release_raw_slot(g, container_elem_type(ltype), removed, i64);
                    /* shift loop: for j = idx; j < count-1; j++ : data[j] = data[j+1] */
                    LLVMValueRef j_a = LLVMBuildAlloca(g->builder, i64, "j");
                    LLVMBuildStore(g->builder, idx, j_a);
                    LLVMValueRef last = LLVMBuildSub(g->builder, count, LLVMConstInt(i64, 1, 0), "last");
                    LLVMBasicBlockRef cond_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "ra.cond");
                    LLVMBasicBlockRef body_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "ra.body");
                    LLVMBasicBlockRef done_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "ra.done");
                    LLVMBuildBr(g->builder, cond_bb);
                    LLVMPositionBuilderAtEnd(g->builder, cond_bb);
                    LLVMValueRef j = LLVMBuildLoad2(g->builder, i64, j_a, "j");
                    LLVMValueRef cmp = LLVMBuildICmp(g->builder, LLVMIntULT, j, last, "cmp");
                    LLVMBuildCondBr(g->builder, cmp, body_bb, done_bb);
                    LLVMPositionBuilderAtEnd(g->builder, body_bb);
                    LLVMValueRef j2 = LLVMBuildLoad2(g->builder, i64, j_a, "j2");
                    LLVMValueRef next = LLVMBuildAdd(g->builder, j2, LLVMConstInt(i64, 1, 0), "nxt");
                    LLVMValueRef src_slot = LLVMBuildGEP2(g->builder, i64, data, &next, 1, "ss");
                    LLVMValueRef val = LLVMBuildLoad2(g->builder, i64, src_slot, "sv");
                    LLVMValueRef dst_slot = LLVMBuildGEP2(g->builder, i64, data, &j2, 1, "ds");
                    LLVMBuildStore(g->builder, val, dst_slot);
                    LLVMValueRef j3 = LLVMBuildAdd(g->builder, j2, LLVMConstInt(i64, 1, 0), "j3");
                    LLVMBuildStore(g->builder, j3, j_a);
                    LLVMBuildBr(g->builder, cond_bb);
                    LLVMPositionBuilderAtEnd(g->builder, done_bb);
                    LLVMBuildStore(g->builder, last, count_ptr);
                    LLVMValueRef tail_ptr = LLVMBuildGEP2(g->builder, i64, data, &last, 1, "tail");
                    LLVMBuildStore(g->builder, LLVMConstInt(i64, 0, 0), tail_ptr);
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
                if (ltype && ltype->name.len == 4 && memcmp(ltype->name.str, "List", 4) == 0) {
                    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                    LLVMValueRef raw_ptr = emit_expr(g, lobj, locals);
                    LLVMValueRef list_ptr = LLVMBuildBitCast(g->builder, raw_ptr,
                        LLVMPointerType(g->list_struct_type, 0), "lptr");
                    LLVMValueRef count_ptr = LLVMBuildStructGEP2(g->builder, g->list_struct_type, list_ptr, 0, "cntp");
                    LLVMValueRef count = LLVMBuildLoad2(g->builder, i64, count_ptr, "cnt");
                    LLVMValueRef data_field = LLVMBuildStructGEP2(g->builder, g->list_struct_type, list_ptr, 2, "df");
                    LLVMValueRef data = LLVMBuildLoad2(g->builder, LLVMPointerType(i64, 0), data_field, "data");
                    LLVMValueRef search = emit_expr(g, expr->call.args.items[0], locals);
                    /* result alloca — -1 for not found */
                    LLVMValueRef res = LLVMBuildAlloca(g->builder, i64, "iofr");
                    LLVMBuildStore(g->builder, LLVMConstInt(i64, (uint64_t)-1LL, 1), res);
                    LLVMValueRef idx_a = LLVMBuildAlloca(g->builder, i64, "ii");
                    LLVMBuildStore(g->builder, LLVMConstInt(i64, 0, 0), idx_a);
                    LLVMBasicBlockRef cond_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "io.cond");
                    LLVMBasicBlockRef body_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "io.body");
                    LLVMBasicBlockRef done_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "io.done");
                    LLVMBuildBr(g->builder, cond_bb);
                    LLVMPositionBuilderAtEnd(g->builder, cond_bb);
                    LLVMValueRef ci = LLVMBuildLoad2(g->builder, i64, idx_a, "ci");
                    LLVMValueRef cmp_d = LLVMBuildICmp(g->builder, LLVMIntUGE, ci, count, "cdone");
                    LLVMBuildCondBr(g->builder, cmp_d, done_bb, body_bb);
                    LLVMPositionBuilderAtEnd(g->builder, body_bb);
                    LLVMValueRef ci2 = LLVMBuildLoad2(g->builder, i64, idx_a, "ci2");
                    LLVMValueRef slot = LLVMBuildGEP2(g->builder, i64, data, &ci2, 1, "sl");
                    LLVMValueRef val = LLVMBuildLoad2(g->builder, i64, slot, "sv");
                    /* compare: for ints use ==, for strings use strcmp */
                    LLVMValueRef eq;
                    zan_type_t *elem_type = ltype->type_arg_count > 0 ? ltype->type_args[0] : NULL;
                    /* Under monomorphization, a List<T> field of the generic
                     * class being specialized carries the erased type param T;
                     * substitute it to the concrete instantiation argument so
                     * reference elements (e.g. string) use content equality. */
                    elem_type = concretize(g, elem_type);
                    if (elem_type && elem_type->kind == TYPE_STRING) {
                        LLVMValueRef sval = LLVMBuildIntToPtr(g->builder, val, i8ptr, "sptr");
                        LLVMValueRef cmp_args[] = { sval, search };
                        LLVMValueRef c = zan_call2(g->builder,
                            LLVMFunctionType(LLVMInt32TypeInContext(g->ctx), (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0),
                            g->fn_strcmp, cmp_args, 2, "cmp");
                        eq = LLVMBuildICmp(g->builder, LLVMIntEQ, c, LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0), "eq");
                    } else {
                        LLVMValueRef sext = search;
                        if (LLVMGetTypeKind(LLVMTypeOf(search)) == LLVMPointerTypeKind)
                            sext = LLVMBuildPtrToInt(g->builder, search, i64, "sp");
                        else if (LLVMGetIntTypeWidth(LLVMTypeOf(search)) < 64)
                            sext = LLVMBuildSExt(g->builder, search, i64, "se");
                        eq = LLVMBuildICmp(g->builder, LLVMIntEQ, val, sext, "eq");
                    }
                    LLVMBasicBlockRef found_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "io.found");
                    LLVMBasicBlockRef next_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "io.next");
                    LLVMBuildCondBr(g->builder, eq, found_bb, next_bb);
                    LLVMPositionBuilderAtEnd(g->builder, found_bb);
                    LLVMBuildStore(g->builder, ci2, res);
                    LLVMBuildBr(g->builder, done_bb);
                    LLVMPositionBuilderAtEnd(g->builder, next_bb);
                    LLVMValueRef ni = LLVMBuildAdd(g->builder, ci2, LLVMConstInt(i64, 1, 0), "ni");
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
                if (ltype && ltype->name.len == 4 && memcmp(ltype->name.str, "List", 4) == 0) {
                    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                    LLVMValueRef raw_ptr = emit_expr(g, lobj, locals);
                    LLVMValueRef list_ptr = LLVMBuildBitCast(g->builder, raw_ptr,
                        LLVMPointerType(g->list_struct_type, 0), "lptr");
                    LLVMValueRef count_ptr = LLVMBuildStructGEP2(g->builder, g->list_struct_type, list_ptr, 0, "cntp");
                    LLVMValueRef count = LLVMBuildLoad2(g->builder, i64, count_ptr, "cnt");
                    LLVMValueRef data_field = LLVMBuildStructGEP2(g->builder, g->list_struct_type, list_ptr, 2, "df");
                    LLVMValueRef data = LLVMBuildLoad2(g->builder, LLVMPointerType(i64, 0), data_field, "data");
                    LLVMValueRef search = emit_expr(g, expr->call.args.items[0], locals);
                    LLVMValueRef res = LLVMBuildAlloca(g->builder, LLVMInt32TypeInContext(g->ctx), "cr");
                    LLVMBuildStore(g->builder, LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0), res);
                    LLVMValueRef idx_a = LLVMBuildAlloca(g->builder, i64, "ci");
                    LLVMBuildStore(g->builder, LLVMConstInt(i64, 0, 0), idx_a);
                    LLVMBasicBlockRef cond_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "ct.cond");
                    LLVMBasicBlockRef body_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "ct.body");
                    LLVMBasicBlockRef done_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "ct.done");
                    LLVMBuildBr(g->builder, cond_bb);
                    LLVMPositionBuilderAtEnd(g->builder, cond_bb);
                    LLVMValueRef ci = LLVMBuildLoad2(g->builder, i64, idx_a, "ci");
                    LLVMValueRef cmp_d = LLVMBuildICmp(g->builder, LLVMIntUGE, ci, count, "cdone");
                    LLVMBuildCondBr(g->builder, cmp_d, done_bb, body_bb);
                    LLVMPositionBuilderAtEnd(g->builder, body_bb);
                    LLVMValueRef ci2 = LLVMBuildLoad2(g->builder, i64, idx_a, "ci2");
                    LLVMValueRef slot = LLVMBuildGEP2(g->builder, i64, data, &ci2, 1, "sl");
                    LLVMValueRef val = LLVMBuildLoad2(g->builder, i64, slot, "sv");
                    LLVMValueRef eq;
                    zan_type_t *elem_type = ltype->type_arg_count > 0 ? ltype->type_args[0] : NULL;
                    /* Under monomorphization, a List<T> field of the generic
                     * class being specialized carries the erased type param T;
                     * substitute it to the concrete instantiation argument so
                     * reference elements (e.g. string) use content equality. */
                    elem_type = concretize(g, elem_type);
                    if (elem_type && elem_type->kind == TYPE_STRING) {
                        LLVMValueRef sval = LLVMBuildIntToPtr(g->builder, val, i8ptr, "sptr");
                        LLVMValueRef cmp_args[] = { sval, search };
                        LLVMValueRef c = zan_call2(g->builder,
                            LLVMFunctionType(LLVMInt32TypeInContext(g->ctx), (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0),
                            g->fn_strcmp, cmp_args, 2, "cmp");
                        eq = LLVMBuildICmp(g->builder, LLVMIntEQ, c, LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0), "eq");
                    } else {
                        LLVMValueRef sext = search;
                        if (LLVMGetTypeKind(LLVMTypeOf(search)) == LLVMPointerTypeKind)
                            sext = LLVMBuildPtrToInt(g->builder, search, i64, "sp");
                        else if (LLVMGetIntTypeWidth(LLVMTypeOf(search)) < 64)
                            sext = LLVMBuildSExt(g->builder, search, i64, "se");
                        eq = LLVMBuildICmp(g->builder, LLVMIntEQ, val, sext, "eq");
                    }
                    LLVMBasicBlockRef found_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "ct.found");
                    LLVMBasicBlockRef next_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "ct.next");
                    LLVMBuildCondBr(g->builder, eq, found_bb, next_bb);
                    LLVMPositionBuilderAtEnd(g->builder, found_bb);
                    LLVMBuildStore(g->builder, LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 1, 0), res);
                    LLVMBuildBr(g->builder, done_bb);
                    LLVMPositionBuilderAtEnd(g->builder, next_bb);
                    LLVMValueRef ni = LLVMBuildAdd(g->builder, ci2, LLVMConstInt(i64, 1, 0), "ni");
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
                if (ltype && ltype->name.len == 4 && memcmp(ltype->name.str, "List", 4) == 0) {
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
                    LLVMValueRef idx = emit_expr(g, expr->call.args.items[0], locals);
                    LLVMValueRef item = emit_expr(g, expr->call.args.items[1], locals);
                    if (LLVMGetTypeKind(LLVMTypeOf(item)) == LLVMPointerTypeKind)
                        item = LLVMBuildPtrToInt(g->builder, item, i64, "ip");
                    else if (LLVMGetTypeKind(LLVMTypeOf(item)) == LLVMIntegerTypeKind &&
                             LLVMGetIntTypeWidth(LLVMTypeOf(item)) < 64)
                        item = LLVMBuildSExt(g->builder, item, i64, "ie");
                    /* grow if needed */
                    LLVMValueRef need = LLVMBuildICmp(g->builder, LLVMIntUGE, count, cap, "need");
                    LLVMBasicBlockRef grow_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "ins.grow");
                    LLVMBasicBlockRef shift_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "ins.shift");
                    LLVMBuildCondBr(g->builder, need, grow_bb, shift_bb);
                    LLVMPositionBuilderAtEnd(g->builder, grow_bb);
                    LLVMValueRef new_cap = LLVMBuildMul(g->builder, cap, LLVMConstInt(i64, 2, 0), "nc");
                    LLVMBuildStore(g->builder, new_cap, cap_ptr);
                    LLVMValueRef nbytes = LLVMBuildMul(g->builder, new_cap, LLVMConstInt(i64, 8, 0), "nb");
                    LLVMValueRef old_data = LLVMBuildBitCast(g->builder, data, i8ptr, "od");
                    LLVMValueRef new_data = zan_call2(g->builder,
                        LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i64 }, 2, 0),
                        g->fn_realloc, (LLVMValueRef[]){ old_data, nbytes }, 2, "nd");
                    LLVMValueRef new_data_i = LLVMBuildBitCast(g->builder, new_data, LLVMPointerType(i64, 0), "ndi");
                    LLVMBuildStore(g->builder, new_data_i, data_field);
                    LLVMBuildBr(g->builder, shift_bb);
                    /* shift elements right from count-1 down to idx */
                    LLVMPositionBuilderAtEnd(g->builder, shift_bb);
                    LLVMValueRef phi_data = LLVMBuildLoad2(g->builder, LLVMPointerType(i64, 0), data_field, "phid");
                    LLVMValueRef j_a = LLVMBuildAlloca(g->builder, i64, "ij");
                    LLVMBuildStore(g->builder, count, j_a);
                    LLVMBasicBlockRef scond_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "ins.scond");
                    LLVMBasicBlockRef sbody_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "ins.sbody");
                    LLVMBasicBlockRef sdone_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "ins.sdone");
                    LLVMBuildBr(g->builder, scond_bb);
                    LLVMPositionBuilderAtEnd(g->builder, scond_bb);
                    LLVMValueRef j = LLVMBuildLoad2(g->builder, i64, j_a, "j");
                    LLVMValueRef cmp = LLVMBuildICmp(g->builder, LLVMIntUGT, j, idx, "cmp");
                    LLVMBuildCondBr(g->builder, cmp, sbody_bb, sdone_bb);
                    LLVMPositionBuilderAtEnd(g->builder, sbody_bb);
                    LLVMValueRef j2 = LLVMBuildLoad2(g->builder, i64, j_a, "j2");
                    LLVMValueRef prev = LLVMBuildSub(g->builder, j2, LLVMConstInt(i64, 1, 0), "prev");
                    LLVMValueRef src_slot = LLVMBuildGEP2(g->builder, i64, phi_data, &prev, 1, "ss");
                    LLVMValueRef sv = LLVMBuildLoad2(g->builder, i64, src_slot, "sv");
                    LLVMValueRef dst_slot = LLVMBuildGEP2(g->builder, i64, phi_data, &j2, 1, "ds");
                    LLVMBuildStore(g->builder, sv, dst_slot);
                    LLVMBuildStore(g->builder, prev, j_a);
                    LLVMBuildBr(g->builder, scond_bb);
                    LLVMPositionBuilderAtEnd(g->builder, sdone_bb);
                    /* store item at index. overwrite_old must be 0: Insert
                     * shifts the previous occupant of this slot up to idx+1
                     * where it stays live, so it must not be released here
                     * (doing so frees a still-referenced element). The new
                     * item is still retained by emit_collection_slot_store. */
                    LLVMValueRef ins_slot = LLVMBuildGEP2(g->builder, i64, phi_data, &idx, 1, "is");
                    emit_collection_slot_store(g, container_elem_type(ltype), i64, ins_slot,
                        item, expr->call.args.items[1], locals, 0);
                    LLVMValueRef nc = LLVMBuildAdd(g->builder, count, LLVMConstInt(i64, 1, 0), "nc");
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
                if (ltype && ltype->name.len == 4 && memcmp(ltype->name.str, "List", 4) == 0) {
                    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                    LLVMValueRef raw_ptr = emit_expr(g, lobj, locals);
                    LLVMValueRef list_ptr = LLVMBuildBitCast(g->builder, raw_ptr,
                        LLVMPointerType(g->list_struct_type, 0), "lptr");
                    LLVMValueRef count_ptr = LLVMBuildStructGEP2(g->builder, g->list_struct_type, list_ptr, 0, "cntp");
                    LLVMValueRef count = LLVMBuildLoad2(g->builder, i64, count_ptr, "cnt");
                    LLVMValueRef data_field = LLVMBuildStructGEP2(g->builder, g->list_struct_type, list_ptr, 2, "df");
                    LLVMValueRef data = LLVMBuildLoad2(g->builder, LLVMPointerType(i64, 0), data_field, "data");
                    /* two-pointer swap: lo=0, hi=count-1 */
                    LLVMValueRef lo_a = LLVMBuildAlloca(g->builder, i64, "lo");
                    LLVMValueRef hi_a = LLVMBuildAlloca(g->builder, i64, "hi");
                    LLVMBuildStore(g->builder, LLVMConstInt(i64, 0, 0), lo_a);
                    LLVMValueRef hi_init = LLVMBuildSub(g->builder, count, LLVMConstInt(i64, 1, 0), "hi");
                    LLVMBuildStore(g->builder, hi_init, hi_a);
                    LLVMBasicBlockRef cond_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "rv.cond");
                    LLVMBasicBlockRef body_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "rv.body");
                    LLVMBasicBlockRef done_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "rv.done");
                    LLVMBuildBr(g->builder, cond_bb);
                    LLVMPositionBuilderAtEnd(g->builder, cond_bb);
                    LLVMValueRef lo = LLVMBuildLoad2(g->builder, i64, lo_a, "lo");
                    LLVMValueRef hi = LLVMBuildLoad2(g->builder, i64, hi_a, "hi");
                    LLVMValueRef cmp = LLVMBuildICmp(g->builder, LLVMIntULT, lo, hi, "cmp");
                    LLVMBuildCondBr(g->builder, cmp, body_bb, done_bb);
                    LLVMPositionBuilderAtEnd(g->builder, body_bb);
                    LLVMValueRef lo2 = LLVMBuildLoad2(g->builder, i64, lo_a, "lo2");
                    LLVMValueRef hi2 = LLVMBuildLoad2(g->builder, i64, hi_a, "hi2");
                    LLVMValueRef lo_slot = LLVMBuildGEP2(g->builder, i64, data, &lo2, 1, "ls");
                    LLVMValueRef hi_slot = LLVMBuildGEP2(g->builder, i64, data, &hi2, 1, "hs");
                    LLVMValueRef lv = LLVMBuildLoad2(g->builder, i64, lo_slot, "lv");
                    LLVMValueRef hv = LLVMBuildLoad2(g->builder, i64, hi_slot, "hv");
                    LLVMBuildStore(g->builder, hv, lo_slot);
                    LLVMBuildStore(g->builder, lv, hi_slot);
                    LLVMBuildStore(g->builder, LLVMBuildAdd(g->builder, lo2, LLVMConstInt(i64, 1, 0), "lo3"), lo_a);
                    LLVMBuildStore(g->builder, LLVMBuildSub(g->builder, hi2, LLVMConstInt(i64, 1, 0), "hi3"), hi_a);
                    LLVMBuildBr(g->builder, cond_bb);
                    LLVMPositionBuilderAtEnd(g->builder, done_bb);
                    return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
                }
            }
        }


                /* Dict method calls: Add, ContainsKey */
        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS) {
            zan_ast_node_t *callee_d = expr->call.callee;
            zan_istr_t mname = callee_d->member.name;
            if (callee_d->member.object->kind == AST_IDENTIFIER) {
                local_var_t *dict_local = local_find(locals, callee_d->member.object->ident.name);
                if (dict_local && dict_local->type && dict_local->type->name.len == 4 &&
                    memcmp(dict_local->type->name.str, "Dict", 4) == 0) {
                    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                    LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
                    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                    LLVMValueRef raw = LLVMBuildLoad2(g->builder, i8ptr, dict_local->alloca, "draw");
                    LLVMValueRef dp = LLVMBuildBitCast(g->builder, raw,
                        LLVMPointerType(g->dict_struct_type, 0), "dp");

                    if (mname.len == 3 && memcmp(mname.str, "Add", 3) == 0 && expr->call.args.count == 2) {
                        LLVMValueRef cntp = LLVMBuildStructGEP2(g->builder, g->dict_struct_type, dp, 0, "cntp");
                        LLVMValueRef cnt = LLVMBuildLoad2(g->builder, i64, cntp, "cnt");
                        LLVMValueRef kp = LLVMBuildStructGEP2(g->builder, g->dict_struct_type, dp, 2, "kp");
                        LLVMValueRef ks = LLVMBuildLoad2(g->builder, LLVMPointerType(i8ptr, 0), kp, "ks");
                        LLVMValueRef vp = LLVMBuildStructGEP2(g->builder, g->dict_struct_type, dp, 3, "vp");
                        LLVMValueRef vs = LLVMBuildLoad2(g->builder, LLVMPointerType(i64, 0), vp, "vs");
                        LLVMValueRef key_val = emit_expr(g, expr->call.args.items[0], locals);
                        LLVMValueRef val_v = emit_expr(g, expr->call.args.items[1], locals);
                        LLVMValueRef kslot = LLVMBuildGEP2(g->builder, i8ptr, ks, &cnt, 1, "ksl");
                        LLVMValueRef vslot = LLVMBuildGEP2(g->builder, i64, vs, &cnt, 1, "vsl");
                        emit_collection_slot_store(g, dict_key_type(g, dict_local->type), i8ptr, kslot,
                            key_val, expr->call.args.items[0], locals, 0);
                        emit_collection_slot_store(g, dict_value_type(dict_local->type), i64, vslot,
                            val_v, expr->call.args.items[1], locals, 0);
                        LLVMValueRef nc = LLVMBuildAdd(g->builder, cnt, LLVMConstInt(i64, 1, 0), "nc");
                        LLVMBuildStore(g->builder, nc, cntp);
                        return LLVMConstInt(i32t, 0, 0);
                    }

                    if (mname.len == 11 && memcmp(mname.str, "ContainsKey", 11) == 0 && expr->call.args.count == 1) {
                        LLVMValueRef cntp = LLVMBuildStructGEP2(g->builder, g->dict_struct_type, dp, 0, "cntp");
                        LLVMValueRef cnt = LLVMBuildLoad2(g->builder, i64, cntp, "cnt");
                        LLVMValueRef kp = LLVMBuildStructGEP2(g->builder, g->dict_struct_type, dp, 2, "kp");
                        LLVMValueRef ks = LLVMBuildLoad2(g->builder, LLVMPointerType(i8ptr, 0), kp, "ks");
                        LLVMValueRef search = coerce_dict_key(g, emit_expr(g, expr->call.args.items[0], locals));
                        /* result alloca */
                        LLVMValueRef res = LLVMBuildAlloca(g->builder, i32t, "ckr");
                        LLVMBuildStore(g->builder, LLVMConstInt(i32t, 0, 0), res);
                        LLVMValueRef idx_a = LLVMBuildAlloca(g->builder, i64, "di");
                        LLVMBuildStore(g->builder, LLVMConstInt(i64, 0, 0), idx_a);
                        LLVMBasicBlockRef cond_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "ck.cond");
                        LLVMBasicBlockRef body_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "ck.body");
                        LLVMBasicBlockRef done_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "ck.done");
                        LLVMBuildBr(g->builder, cond_bb);
                        /* cond: i < count? */
                        LLVMPositionBuilderAtEnd(g->builder, cond_bb);
                        LLVMValueRef ci = LLVMBuildLoad2(g->builder, i64, idx_a, "ci");
                        LLVMValueRef cmp_done = LLVMBuildICmp(g->builder, LLVMIntUGE, ci, cnt, "cdone");
                        LLVMBuildCondBr(g->builder, cmp_done, done_bb, body_bb);
                        /* body: compare keys[i] with search key */
                        LLVMPositionBuilderAtEnd(g->builder, body_bb);
                        LLVMValueRef ci2 = LLVMBuildLoad2(g->builder, i64, idx_a, "ci2");
                        LLVMValueRef kslot = LLVMBuildGEP2(g->builder, i8ptr, ks, &ci2, 1, "ksl");
                        LLVMValueRef kv = LLVMBuildLoad2(g->builder, i8ptr, kslot, "kv");
                        LLVMValueRef eq = emit_dict_key_eq(g, dict_local->type, kv, search);
                        LLVMBasicBlockRef found_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "ck.found");
                        LLVMBasicBlockRef next_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "ck.next");
                        LLVMBuildCondBr(g->builder, eq, found_bb, next_bb);
                        /* found: set result = 1, jump to done */
                        LLVMPositionBuilderAtEnd(g->builder, found_bb);
                        LLVMBuildStore(g->builder, LLVMConstInt(i32t, 1, 0), res);
                        LLVMBuildBr(g->builder, done_bb);
                        /* next: i++, loop back */
                        LLVMPositionBuilderAtEnd(g->builder, next_bb);
                        LLVMValueRef ni = LLVMBuildAdd(g->builder, ci2, LLVMConstInt(i64, 1, 0), "ni");
                        LLVMBuildStore(g->builder, ni, idx_a);
                        LLVMBuildBr(g->builder, cond_bb);
                        /* done: return result */
                        LLVMPositionBuilderAtEnd(g->builder, done_bb);
                        return LLVMBuildLoad2(g->builder, i32t, res, "ckres");
                    }
                    if (mname.len == 5 && memcmp(mname.str, "Clear", 5) == 0 && expr->call.args.count == 0) {
                        LLVMValueRef cntp = LLVMBuildStructGEP2(g->builder, g->dict_struct_type, dp, 0, "cntp");
                        LLVMValueRef cnt = LLVMBuildLoad2(g->builder, i64, cntp, "cnt");
                        LLVMValueRef kp = LLVMBuildStructGEP2(g->builder, g->dict_struct_type, dp, 2, "kp");
                        LLVMValueRef ks = LLVMBuildLoad2(g->builder, LLVMPointerType(i8ptr, 0), kp, "ks");
                        LLVMValueRef vp = LLVMBuildStructGEP2(g->builder, g->dict_struct_type, dp, 3, "vp");
                        LLVMValueRef vs = LLVMBuildLoad2(g->builder, LLVMPointerType(i64, 0), vp, "vs");
                        LLVMValueRef idx_a = LLVMBuildAlloca(g->builder, i64, "dc");
                        LLVMBuildStore(g->builder, LLVMConstInt(i64, 0, 0), idx_a);
                        LLVMBasicBlockRef cbb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "dc.cond");
                        LLVMBasicBlockRef bbb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "dc.body");
                        LLVMBasicBlockRef dbb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "dc.done");
                        LLVMBuildBr(g->builder, cbb);
                        LLVMPositionBuilderAtEnd(g->builder, cbb);
                        LLVMValueRef ci = LLVMBuildLoad2(g->builder, i64, idx_a, "ci");
                        LLVMValueRef more = LLVMBuildICmp(g->builder, LLVMIntULT, ci, cnt, "more");
                        LLVMBuildCondBr(g->builder, more, bbb, dbb);
                        LLVMPositionBuilderAtEnd(g->builder, bbb);
                        LLVMValueRef ci2 = LLVMBuildLoad2(g->builder, i64, idx_a, "ci2");
                        LLVMValueRef kslot = LLVMBuildGEP2(g->builder, i8ptr, ks, &ci2, 1, "ksl");
                        LLVMValueRef kv = LLVMBuildLoad2(g->builder, i8ptr, kslot, "kv");
                        emit_collection_release_raw_slot(g, dict_key_type(g, dict_local->type), kv, i8ptr);
                        LLVMValueRef vslot = LLVMBuildGEP2(g->builder, i64, vs, &ci2, 1, "vsl");
                        LLVMValueRef vv = LLVMBuildLoad2(g->builder, i64, vslot, "vv");
                        emit_collection_release_raw_slot(g, dict_value_type(dict_local->type), vv, i64);
                        LLVMValueRef ni = LLVMBuildAdd(g->builder, ci2, LLVMConstInt(i64, 1, 0), "ni");
                        LLVMBuildStore(g->builder, ni, idx_a);
                        LLVMBuildBr(g->builder, cbb);
                        LLVMPositionBuilderAtEnd(g->builder, dbb);
                        LLVMBuildStore(g->builder, LLVMConstInt(i64, 0, 0), cntp);
                        return LLVMConstInt(i32t, 0, 0);
                    }
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
                    if (dict_local && dict_local->type && dict_local->type->name.len == 4 &&
                        memcmp(dict_local->type->name.str, "Dict", 4) == 0) {
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

        /* Dict.Remove(key) — find and remove key, shift remaining entries */
        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS) {
            zan_ast_node_t *callee_d = expr->call.callee;
            zan_istr_t mname = callee_d->member.name;
            if (mname.len == 6 && memcmp(mname.str, "Remove", 6) == 0 && expr->call.args.count == 1) {
                if (callee_d->member.object->kind == AST_IDENTIFIER) {
                    local_var_t *dict_local = local_find(locals, callee_d->member.object->ident.name);
                    if (dict_local && dict_local->type && dict_local->type->name.len == 4 &&
                        memcmp(dict_local->type->name.str, "Dict", 4) == 0) {
                        LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                        LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
                        LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                        LLVMValueRef raw = LLVMBuildLoad2(g->builder, i8ptr, dict_local->alloca, "draw");
                        LLVMValueRef dp = LLVMBuildBitCast(g->builder, raw,
                            LLVMPointerType(g->dict_struct_type, 0), "dp");
                        LLVMValueRef cntp = LLVMBuildStructGEP2(g->builder, g->dict_struct_type, dp, 0, "cntp");
                        LLVMValueRef cnt = LLVMBuildLoad2(g->builder, i64, cntp, "cnt");
                        LLVMValueRef kp = LLVMBuildStructGEP2(g->builder, g->dict_struct_type, dp, 2, "kp");
                        LLVMValueRef ks = LLVMBuildLoad2(g->builder, LLVMPointerType(i8ptr, 0), kp, "ks");
                        LLVMValueRef vp = LLVMBuildStructGEP2(g->builder, g->dict_struct_type, dp, 3, "vp");
                        LLVMValueRef vs = LLVMBuildLoad2(g->builder, LLVMPointerType(i64, 0), vp, "vs");
                        LLVMValueRef search = coerce_dict_key(g, emit_expr(g, expr->call.args.items[0], locals));
                        /* linear search for key */
                        LLVMValueRef idx_a = LLVMBuildAlloca(g->builder, i64, "di");
                        LLVMBuildStore(g->builder, LLVMConstInt(i64, 0, 0), idx_a);
                        LLVMBasicBlockRef cond_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "dr.cond");
                        LLVMBasicBlockRef body_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "dr.body");
                        LLVMBasicBlockRef found_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "dr.found");
                        LLVMBasicBlockRef next_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "dr.next");
                        LLVMBasicBlockRef done_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "dr.done");
                        LLVMBuildBr(g->builder, cond_bb);
                        LLVMPositionBuilderAtEnd(g->builder, cond_bb);
                        LLVMValueRef ci = LLVMBuildLoad2(g->builder, i64, idx_a, "ci");
                        LLVMBuildCondBr(g->builder, LLVMBuildICmp(g->builder, LLVMIntUGE, ci, cnt, "cdone"), done_bb, body_bb);
                        LLVMPositionBuilderAtEnd(g->builder, body_bb);
                        LLVMValueRef ci2 = LLVMBuildLoad2(g->builder, i64, idx_a, "ci2");
                        LLVMValueRef kslot = LLVMBuildGEP2(g->builder, i8ptr, ks, &ci2, 1, "ksl");
                        LLVMValueRef kv = LLVMBuildLoad2(g->builder, i8ptr, kslot, "kv");
                        LLVMValueRef eq = emit_dict_key_eq(g, dict_local->type, kv, search);
                        LLVMBuildCondBr(g->builder, eq, found_bb, next_bb);
                        /* found: shift remaining entries left */
                        LLVMPositionBuilderAtEnd(g->builder, found_bb);
                        LLVMValueRef fi = LLVMBuildLoad2(g->builder, i64, idx_a, "fi");
                        LLVMValueRef last = LLVMBuildSub(g->builder, cnt, LLVMConstInt(i64, 1, 0), "last");
                        LLVMValueRef rkey = LLVMBuildGEP2(g->builder, i8ptr, ks, &fi, 1, "rkey");
                        LLVMValueRef rkv = LLVMBuildLoad2(g->builder, i8ptr, rkey, "rkv");
                        emit_collection_release_raw_slot(g, dict_key_type(g, dict_local->type), rkv, i8ptr);
                        LLVMValueRef rvslot = LLVMBuildGEP2(g->builder, i64, vs, &fi, 1, "rvslot");
                        LLVMValueRef rv = LLVMBuildLoad2(g->builder, i64, rvslot, "rv");
                        emit_collection_release_raw_slot(g, dict_value_type(dict_local->type), rv, i64);
                        LLVMValueRef j_a = LLVMBuildAlloca(g->builder, i64, "fj");
                        LLVMBuildStore(g->builder, fi, j_a);
                        LLVMBasicBlockRef sc_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "dr.sc");
                        LLVMBasicBlockRef sb_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "dr.sb");
                        LLVMBasicBlockRef sd_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "dr.sd");
                        LLVMBuildBr(g->builder, sc_bb);
                        LLVMPositionBuilderAtEnd(g->builder, sc_bb);
                        LLVMValueRef j = LLVMBuildLoad2(g->builder, i64, j_a, "j");
                        LLVMBuildCondBr(g->builder, LLVMBuildICmp(g->builder, LLVMIntULT, j, last, "jlt"), sb_bb, sd_bb);
                        LLVMPositionBuilderAtEnd(g->builder, sb_bb);
                        LLVMValueRef j2 = LLVMBuildLoad2(g->builder, i64, j_a, "j2");
                        LLVMValueRef nxt = LLVMBuildAdd(g->builder, j2, LLVMConstInt(i64, 1, 0), "nxt");
                        /* shift key */
                        LLVMValueRef ksrc = LLVMBuildGEP2(g->builder, i8ptr, ks, &nxt, 1, "ksrc");
                        LLVMValueRef kdst = LLVMBuildGEP2(g->builder, i8ptr, ks, &j2, 1, "kdst");
                        LLVMBuildStore(g->builder, LLVMBuildLoad2(g->builder, i8ptr, ksrc, "ksv"), kdst);
                        /* shift value */
                        LLVMValueRef vsrc = LLVMBuildGEP2(g->builder, i64, vs, &nxt, 1, "vsrc");
                        LLVMValueRef vdst = LLVMBuildGEP2(g->builder, i64, vs, &j2, 1, "vdst");
                        LLVMBuildStore(g->builder, LLVMBuildLoad2(g->builder, i64, vsrc, "vsv"), vdst);
                        LLVMBuildStore(g->builder, nxt, j_a);
                        LLVMBuildBr(g->builder, sc_bb);
                        LLVMPositionBuilderAtEnd(g->builder, sd_bb);
                        LLVMBuildStore(g->builder, last, cntp);
                        LLVMValueRef ktail = LLVMBuildGEP2(g->builder, i8ptr, ks, &last, 1, "ktail");
                        LLVMBuildStore(g->builder, LLVMConstNull(i8ptr), ktail);
                        LLVMValueRef vtail = LLVMBuildGEP2(g->builder, i64, vs, &last, 1, "vtail");
                        LLVMBuildStore(g->builder, LLVMConstInt(i64, 0, 0), vtail);
                        LLVMBuildBr(g->builder, done_bb);
                        /* next: increment and loop */
                        LLVMPositionBuilderAtEnd(g->builder, next_bb);
                        LLVMValueRef ni = LLVMBuildAdd(g->builder, ci2, LLVMConstInt(i64, 1, 0), "ni");
                        LLVMBuildStore(g->builder, ni, idx_a);
                        LLVMBuildBr(g->builder, cond_bb);
                        LLVMPositionBuilderAtEnd(g->builder, done_bb);
                        return LLVMConstInt(i32t, 0, 0);
                    }
                }
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

                /* user-defined method call: obj.Method(args) or Type.StaticMethod(args) */
        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS) {
            zan_ast_node_t *callee = expr->call.callee;
            if (callee->member.object->kind == AST_IDENTIFIER) {
                /* first try as instance method: local_var.Method(args) */
                local_var_t *local = local_find(locals, callee->member.object->ident.name);
                if (local && local->type && local->type->sym) {
                    zan_symbol_t *type_sym = local->type->sym;
                    zan_symbol_t *method_sym = resolve_overload(type_sym, callee->member.name, expr->call.args.count);
                    if (method_sym) pack_params_args(g, expr, method_sym, locals);
                    if (method_sym) {
                        for (int fi = 0; fi < g->function_count; fi++) {
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
                                        method_param_type(g, method_sym, k), locals);
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

                /* try as static method: ClassName.Method(args) */
                zan_symbol_t *type_sym = zan_binder_lookup(g->binder, callee->member.object->ident.name);
                if (type_sym && (type_sym->kind == SYM_CLASS || type_sym->kind == SYM_STRUCT)) {
                    zan_symbol_t *method_sym = resolve_overload(type_sym, callee->member.name, expr->call.args.count);
                    if (method_sym) pack_params_args(g, expr, method_sym, locals);
                    if (method_sym) {
                        for (int fi = 0; fi < g->function_count; fi++) {
                            if (g->functions[fi].sym == method_sym) {
                                int argc = expr->call.args.count;
                                LLVMValueRef *call_args = (LLVMValueRef *)calloc((size_t)(argc > 0 ? argc : 1), sizeof(LLVMValueRef));
                                for (int k = 0; k < argc; k++) {
                                    call_args[k] = emit_arg_typed(g, expr->call.args.items[k],
                                        method_param_type(g, method_sym, k), locals);
                                }
                                const char *cn = (LLVMGetTypeKind(LLVMGetReturnType(g->functions[fi].fn_type)) == LLVMVoidTypeKind) ? "" : "scall";
                                coerce_args_to_params(g, g->functions[fi].fn_type, call_args, argc);
                                LLVMValueRef result = zan_call2(g->builder, g->functions[fi].fn_type,
                                    g->functions[fi].fn, call_args, (unsigned)argc, cn);
                                zan_type_t *gret = generic_method_ret(g, method_sym, expr, locals);
                                if (gret) result = emit_boundary_coerce(g, result, map_type(g, gret));
                                int consumes_free_arg =
                                    argc == 1 && call_consumes_free_arg(g->functions[fi].fn);
                                if (consumes_free_arg) {
                                    emit_invalidate_freed_string(g,
                                        expr->call.args.items[0], locals);
                                }
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

        /* general instance method call: <expr>.Method(args) where <expr> is
         * any expression of class/struct type (a field, this.field, an index
         * or a call result) — not just a local variable or class name. */
        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS) {
            zan_ast_node_t *callee = expr->call.callee;
            zan_symbol_t *recv_cls = expr_class_sym(g, callee->member.object, locals);
            if (recv_cls) {
                zan_symbol_t *method_sym = resolve_overload(recv_cls, callee->member.name, expr->call.args.count);
                    if (method_sym) pack_params_args(g, expr, method_sym, locals);
                if (method_sym) {
                    for (int fi = 0; fi < g->function_count; fi++) {
                        if (g->functions[fi].sym == method_sym) {
                            int argc = expr->call.args.count + 1;
                            LLVMValueRef *call_args = (LLVMValueRef *)calloc((size_t)argc, sizeof(LLVMValueRef));
                            LLVMValueRef recv_val = emit_expr(g, callee->member.object, locals);
                            /* receiver: the object pointer produced by the
                             * expression (field load, index, call, ...). */
                            call_args[0] = recv_val;
                            for (int k = 0; k < expr->call.args.count; k++) {
                                call_args[k + 1] = emit_arg_typed(g, expr->call.args.items[k],
                                    method_param_type(g, method_sym, k), locals);
                            }
                            zan_type_t *recv_ty = infer_expr_type(g, callee->member.object, locals);
                            LLVMTypeRef mft = g->functions[fi].fn_type;
                            LLVMValueRef mfn = route_generic_method(g, recv_ty,
                                method_sym, g->functions[fi].fn, mft, &mft);
                            const char *cn = (LLVMGetTypeKind(LLVMGetReturnType(mft)) == LLVMVoidTypeKind) ? "" : "mcall";
                            LLVMValueRef result = emit_dispatch_call(g, recv_cls, method_sym,
                                mfn, mft, call_args, argc, cn);
                            result = coerce_generic_result(g, result, method_sym, recv_ty);
                            emit_release_owned_call_temp(g, callee->member.object, recv_val, locals);
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
                zan_symbol_t *iface_m = resolve_overload(iface, callee->member.name,
                                                         expr->call.args.count);
                if (iface_m) {
                    LLVMContextRef c = g->ctx;
                    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(c), 0);
                    zan_type_t *rty = NULL;
                    if (iface_m->decl && iface_m->decl->kind == AST_METHOD_DECL &&
                        iface_m->decl->method_decl.return_type)
                        rty = zan_binder_resolve_type(g->binder,
                                  iface_m->decl->method_decl.return_type);
                    bool has_res = rty && rty->kind != TYPE_VOID;
                    LLVMTypeRef res_ty = has_res ? map_type(g, rty) : NULL;

                    int uargc = expr->call.args.count;
                    /* emit receiver + argument values once, before branching */
                    LLVMValueRef recv = emit_expr(g, callee->member.object, locals);
                    LLVMValueRef *avals = (LLVMValueRef *)calloc((size_t)(uargc > 0 ? uargc : 1),
                                                                sizeof(LLVMValueRef));
                    for (int k = 0; k < uargc; k++)
                        avals[k] = emit_arg_typed(g, expr->call.args.items[k],
                                                  method_param_type(g, iface_m, k), locals);
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
                        for (int fi = 0; fi < g->function_count; fi++)
                            if (g->functions[fi].sym == impl_m) {
                                ifn = g->functions[fi].fn; ifnty = g->functions[fi].fn_type; break;
                            }
                        if (!ifn || !ifnty) continue;

                        LLVMBasicBlockRef callbb = LLVMAppendBasicBlockInContext(c, g->current_fn, "ifc.call");
                        LLVMBasicBlockRef nextbb = LLVMAppendBasicBlockInContext(c, g->current_fn, "ifc.next");
                        LLVMValueRef tagc = LLVMConstBitCast(get_vtable_global(g, cls), i8ptr);
                        LLVMValueRef cmp = LLVMBuildICmp(g->builder, LLVMIntEQ, tag, tagc, "ifc.eq");
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

                    /* no matching class: yield a null/zero result */
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
                    zan_symbol_t *method_sym = resolve_overload(type_sym, callee->member.name, expr->call.args.count);
                    if (method_sym) pack_params_args(g, expr, method_sym, locals);
                    if (method_sym) {
                        for (int fi = 0; fi < g->function_count; fi++) {
                            if (g->functions[fi].sym == method_sym) {
                                int argc = expr->call.args.count;
                                LLVMValueRef *call_args = (LLVMValueRef *)calloc((size_t)(argc > 0 ? argc : 1), sizeof(LLVMValueRef));
                                for (int k = 0; k < argc; k++) {
                                    call_args[k] = emit_arg_typed(g, expr->call.args.items[k],
                                        method_param_type(g, method_sym, k), locals);
                                }
                                const char *cn = (LLVMGetTypeKind(LLVMGetReturnType(g->functions[fi].fn_type)) == LLVMVoidTypeKind) ? "" : "scall";
                                coerce_args_to_params(g, g->functions[fi].fn_type, call_args, argc);
                                LLVMValueRef result = zan_call2(g->builder, g->functions[fi].fn_type,
                                    g->functions[fi].fn, call_args, (unsigned)argc, cn);
                                zan_type_t *gret = generic_method_ret(g, method_sym, expr, locals);
                                if (gret) result = emit_boundary_coerce(g, result, map_type(g, gret));
                                int consumes_free_arg =
                                    argc == 1 && call_consumes_free_arg(g->functions[fi].fn);
                                if (consumes_free_arg) {
                                    emit_invalidate_freed_string(g,
                                        expr->call.args.items[0], locals);
                                }
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
                                      expr->call.args.count);
            if (method_sym) {
                for (int fi = 0; fi < g->function_count; fi++) {
                    if (g->functions[fi].sym == method_sym) {
                        int argc = expr->call.args.count + 1;
                        LLVMValueRef *call_args = (LLVMValueRef *)calloc((size_t)argc, sizeof(LLVMValueRef));
                        call_args[0] = emit_arg_typed(g, callee->member.object,
                            method_param_type(g, method_sym, 0), locals);
                        for (int k = 0; k < expr->call.args.count; k++) {
                            call_args[k + 1] = emit_arg_typed(g, expr->call.args.items[k],
                                method_param_type(g, method_sym, k + 1), locals);
                        }
                        const char *cn = (LLVMGetTypeKind(LLVMGetReturnType(g->functions[fi].fn_type)) == LLVMVoidTypeKind) ? "" : "extcall";
                        coerce_args_to_params(g, g->functions[fi].fn_type, call_args, argc);
                        LLVMValueRef result = zan_call2(g->builder, g->functions[fi].fn_type,
                            g->functions[fi].fn, call_args, (unsigned)argc, cn);
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
                zan_symbol_t *method_sym = resolve_overload(g->current_type_sym, fn_name, expr->call.args.count);
                    if (method_sym) pack_params_args(g, expr, method_sym, locals);
                if (method_sym) {
                    for (int fi = 0; fi < g->function_count; fi++) {
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
                                call_args[k + extra] = emit_expr(g, expr->call.args.items[k], locals);
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
                for (int k = 0; k < argc; k++) {
                    call_args[k] = emit_expr(g, expr->call.args.items[k], locals);
                }
                LLVMTypeRef fn_type = LLVMGlobalGetValueType(global_fn);
                const char *gcn = (LLVMGetTypeKind(LLVMGetReturnType(fn_type)) == LLVMVoidTypeKind) ? "" : "gcall";
                LLVMValueRef result = zan_call2(g->builder, fn_type,
                    global_fn, call_args, (unsigned)argc, gcn);
                int consumes_free_arg =
                    argc == 1 && call_consumes_free_arg(global_fn);
                if (consumes_free_arg) {
                    emit_invalidate_freed_string(g,
                        expr->call.args.items[0], locals);
                }
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
         * method) keeps this from firing on extern/DllImport members. */
        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS &&
            expr->call.callee->member.object->kind == AST_IDENTIFIER) {
            zan_istr_t on = expr->call.callee->member.object->ident.name;
            zan_istr_t mn = expr->call.callee->member.name;
            zan_symbol_t *osym = local_find(locals, on) ? NULL
                : zan_binder_lookup(g->binder, on);
            if (!local_find(locals, on) && !osym) {
                zan_diag_emit(g->diag, DIAG_ERROR, expr->loc,
                    "unresolved call '%.*s.%.*s': '%.*s' is not a known variable, "
                    "type, or namespace (is the class imported and registered in "
                    "the stdlib map?)",
                    (int)on.len, on.str, (int)mn.len, mn.str,
                    (int)on.len, on.str);
            }
            /* The object is a known class/struct but the method does not exist
             * on it (and no builtin lowering claimed the call earlier). This is
             * the method-call twin of the check above: silently lowering to
             * 0/null turns a typo or a missing stdlib method into a runtime
             * null-pointer crash far from the call site. */
            else if (osym && (osym->kind == SYM_CLASS || osym->kind == SYM_STRUCT) &&
                     !get_method_sym(osym, mn)) {
                zan_diag_emit(g->diag, DIAG_ERROR, expr->loc,
                    "unresolved call '%.*s.%.*s': type '%.*s' has no method "
                    "'%.*s'",
                    (int)on.len, on.str, (int)mn.len, mn.str,
                    (int)on.len, on.str, (int)mn.len, mn.str);
            }
        }

        /* generic function call — fallback */
        return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
    }

    case AST_STRING_INTERP: {
        /* String interpolation: convert each part to i8*, then concatenate.
         * Parts alternate: string_literal, expr, string_literal, expr, ... string_literal
         * Strategy: for each part, get an i8* string. Then compute total length,
         * malloc a buffer, strcpy+strcat all parts, return the buffer pointer. */
        LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
        LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);

        int n = expr->string_interp.parts.count;
        if (n == 0) return emit_string_literal_rc(g, (zan_istr_t){ "", 0 });

        /* convert each part to i8* */
        LLVMValueRef *strs = (LLVMValueRef *)calloc((size_t)n, sizeof(LLVMValueRef));
        LLVMValueRef *lens = (LLVMValueRef *)calloc((size_t)n, sizeof(LLVMValueRef));
        unsigned char *owns = (unsigned char *)calloc((size_t)n, sizeof(unsigned char));

        LLVMTypeRef strlen_type = LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr }, 1, 0);
        LLVMTypeRef snprintf_type = LLVMFunctionType(
            LLVMInt32TypeInContext(g->ctx),
            (LLVMTypeRef[]){ i8ptr, i64, i8ptr }, 3, 1);

        for (int i = 0; i < n; i++) {
            zan_ast_node_t *part = expr->string_interp.parts.items[i];
            if (part->kind == AST_STRING_LITERAL) {
                strs[i] = emit_string_literal_rc(g, part->str_val);
                lens[i] = LLVMConstInt(i64, (uint64_t)part->str_val.len, 0);
            } else {
                LLVMValueRef val = emit_expr(g, part, locals);
                LLVMTypeRef vt = LLVMTypeOf(val);
                LLVMTypeKind vtk = LLVMGetTypeKind(vt);

                if (vtk == LLVMPointerTypeKind) {
                    /* already a string */
                    strs[i] = val;
                    lens[i] = zan_call2(g->builder, strlen_type, g->fn_strlen, &val, 1, "len");
                    owns[i] = expr_yields_owned_rc_value(g, part, locals) ? 1 : 0;
                } else if (vtk == LLVMDoubleTypeKind || vtk == LLVMFloatTypeKind) {
                    /* snprintf(NULL, 0, "%g", val) to get length, then snprintf into buffer */
                    LLVMValueRef fmt = LLVMBuildGlobalStringPtr(g->builder, "%g", "dfmt");
                    LLVMValueRef null_ptr = LLVMConstNull(i8ptr);
                    LLVMValueRef zero = LLVMConstInt(i64, 0, 0);
                    LLVMValueRef snp_args1[] = { null_ptr, zero, fmt, val };
                    LLVMValueRef needed = zan_call2(g->builder, snprintf_type, g->fn_snprintf, snp_args1, 4, "needed");
                    LLVMValueRef needed64 = LLVMBuildSExt(g->builder, needed, i64, "n64");
                    LLVMValueRef buf_size = LLVMBuildAdd(g->builder, needed64, LLVMConstInt(i64, 1, 0), "bsz");
                    LLVMValueRef buf = emit_string_alloc_rc(g, buf_size);
                    LLVMValueRef snp_args2[] = { buf, buf_size, fmt, val };
                    zan_call2(g->builder, snprintf_type, g->fn_snprintf, snp_args2, 4, "");
                    strs[i] = buf;
                    lens[i] = needed64;
                    owns[i] = 1;
                } else {
                    /* integer types — format with %lld (%llu for ulong) */
                    LLVMValueRef val64 = val;
                    if (LLVMGetIntTypeWidth(vt) < 64) {
                        val64 = LLVMBuildSExt(g->builder, val, i64, "ext");
                    }
                    LLVMValueRef fmt = expr_is_ulong(g, part, locals)
                        ? LLVMBuildGlobalStringPtr(g->builder, "%llu", "ufmt")
                        : LLVMBuildGlobalStringPtr(g->builder, "%lld", "ifmt");
                    LLVMValueRef null_ptr = LLVMConstNull(i8ptr);
                    LLVMValueRef zero = LLVMConstInt(i64, 0, 0);
                    LLVMValueRef snp_args1[] = { null_ptr, zero, fmt, val64 };
                    LLVMValueRef needed = zan_call2(g->builder, snprintf_type, g->fn_snprintf, snp_args1, 4, "needed");
                    LLVMValueRef needed64 = LLVMBuildSExt(g->builder, needed, i64, "n64");
                    LLVMValueRef buf_size = LLVMBuildAdd(g->builder, needed64, LLVMConstInt(i64, 1, 0), "bsz");
                    LLVMValueRef buf = emit_string_alloc_rc(g, buf_size);
                    LLVMValueRef snp_args2[] = { buf, buf_size, fmt, val64 };
                    zan_call2(g->builder, snprintf_type, g->fn_snprintf, snp_args2, 4, "");
                    strs[i] = buf;
                    lens[i] = needed64;
                    owns[i] = 1;
                }
            }
        }

        /* compute total length */
        LLVMValueRef total_len = LLVMConstInt(i64, 0, 0);
        for (int i = 0; i < n; i++) {
            total_len = LLVMBuildAdd(g->builder, total_len, lens[i], "tlen");
        }
        LLVMValueRef alloc_size = LLVMBuildAdd(g->builder, total_len, LLVMConstInt(i64, 1, 0), "asz");

        /* allocate result buffer with rc header */
        LLVMValueRef result = emit_string_alloc_rc(g, alloc_size);

        /* strcpy first, strcat rest */
        LLVMTypeRef strcpy_type = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0);
        LLVMValueRef strcpy_args[] = { result, strs[0] };
        zan_call2(g->builder, strcpy_type, g->fn_strcpy, strcpy_args, 2, "");

        for (int i = 1; i < n; i++) {
            LLVMValueRef cat_args[] = { result, strs[i] };
            zan_call2(g->builder, strcpy_type, g->fn_strcat, cat_args, 2, "");
        }

        if (owns) {
            for (int i = 0; i < n; i++) {
                if (owns[i]) emit_string_release(g, strs[i]);
            }
        }
        free(strs);
        free(lens);
        free(owns);
        return result;
    }

    case AST_MEMBER_ACCESS: {
        /* Math.PI → constant */
        if (expr->member.object->kind == AST_IDENTIFIER) {
            zan_istr_t obj = expr->member.object->ident.name;
            if (obj.len == 4 && memcmp(obj.str, "Math", 4) == 0) {
                if (expr->member.name.len == 2 && memcmp(expr->member.name.str, "PI", 2) == 0) {
                    return LLVMConstReal(LLVMDoubleTypeInContext(g->ctx), 3.14159265358979323846);
                }
                if (expr->member.name.len == 4 && memcmp(expr->member.name.str, "Sqrt", 4) == 0) {
                    /* Math.Sqrt is handled as a call — shouldn't reach here */
                    return LLVMConstInt(LLVMInt64TypeInContext(g->ctx), 0, 0);
                }
            }
        }

        /* .Count property on List — read count field from list struct
         * (works for local vars and fields). */
        if (expr->member.name.len == 5 && memcmp(expr->member.name.str, "Count", 5) == 0) {
            zan_type_t *lt = infer_expr_type(g, expr->member.object, locals);
            if (lt && lt->name.len == 4 && memcmp(lt->name.str, "List", 4) == 0) {
                LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                LLVMValueRef raw_ptr = emit_expr(g, expr->member.object, locals);
                LLVMValueRef list_ptr = LLVMBuildBitCast(g->builder, raw_ptr,
                    LLVMPointerType(g->list_struct_type, 0), "lptr");
                LLVMValueRef count_ptr = LLVMBuildStructGEP2(g->builder, g->list_struct_type, list_ptr, 0, "cntp");
                LLVMValueRef count = LLVMBuildLoad2(g->builder, i64, count_ptr, "cnt");
                return count;
            }
        }

        /* Dict.Count — return number of entries */
        if (expr->member.name.len == 5 && memcmp(expr->member.name.str, "Count", 5) == 0 &&
            expr->member.object->kind == AST_IDENTIFIER) {
            local_var_t *dict_local = local_find(locals, expr->member.object->ident.name);
            if (dict_local && dict_local->type && dict_local->type->name.len == 4 &&
                memcmp(dict_local->type->name.str, "Dict", 4) == 0) {
                LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                LLVMValueRef raw = LLVMBuildLoad2(g->builder, i8ptr, dict_local->alloca, "draw");
                LLVMValueRef dp = LLVMBuildBitCast(g->builder, raw,
                    LLVMPointerType(g->dict_struct_type, 0), "dp");
                LLVMValueRef cntp = LLVMBuildStructGEP2(g->builder, g->dict_struct_type, dp, 0, "cntp");
                return LLVMBuildLoad2(g->builder, i64, cntp, "dcnt");
            }
        }

        /* array.Length: read the element count captured at declaration. */
        if (expr->member.name.len == 6 && memcmp(expr->member.name.str, "Length", 6) == 0 &&
            expr->member.object->kind == AST_IDENTIFIER) {
            local_var_t *arr_local = local_find(locals, expr->member.object->ident.name);
            if (arr_local && arr_local->type && arr_local->type->kind == TYPE_ARRAY &&
                arr_local->arr_len_slot) {
                return LLVMBuildLoad2(g->builder, LLVMInt64TypeInContext(g->ctx),
                                      arr_local->arr_len_slot, "arr.len");
            }
        }

        /* StringBuilder.Length: load the count field. */
        if (expr->member.name.len == 6 && memcmp(expr->member.name.str, "Length", 6) == 0) {
            zan_type_t *sbt = infer_expr_type(g, expr->member.object, locals);
            if (sbt && sbt->name.len == 13 &&
                memcmp(sbt->name.str, "StringBuilder", 13) == 0) {
                LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                LLVMValueRef raw = emit_expr(g, expr->member.object, locals);
                LLVMValueRef sbp = LLVMBuildBitCast(g->builder, raw,
                    LLVMPointerType(g->sb_struct_type, 0), "sbp");
                LLVMValueRef cptr = LLVMBuildStructGEP2(g->builder, g->sb_struct_type, sbp, 0, "sbcp");
                return LLVMBuildLoad2(g->builder, i64, cptr, "sblen");
            }
        }

        /* String.Length property — call strlen on string pointer */
        if (expr->member.name.len == 6 && memcmp(expr->member.name.str, "Length", 6) == 0) {
            LLVMValueRef obj_val = emit_expr(g, expr->member.object, locals);
            if (LLVMGetTypeKind(LLVMTypeOf(obj_val)) == LLVMPointerTypeKind) {
                LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                LLVMTypeRef strlen_type = LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr }, 1, 0);
                /* strlen returns i64; `int` is i64 so return it directly. */
                LLVMValueRef len = zan_call2(g->builder, strlen_type, g->fn_strlen, &obj_val, 1, "len");
                emit_release_owned_call_temp(g, expr->member.object, obj_val, locals);
                return len;
            }
        }

        /* Enum member access: EnumType.MemberName -> integer constant */
        if (expr->member.object->kind == AST_IDENTIFIER) {
            zan_symbol_t *enum_sym = zan_binder_lookup(g->binder, expr->member.object->ident.name);
            if (enum_sym && enum_sym->kind == SYM_ENUM) {
                int enum_val = 0;
                for (int ei = 0; ei < enum_sym->member_count; ei++) {
                    if (enum_sym->members[ei]->kind == SYM_ENUM_MEMBER) {
                        if (enum_sym->members[ei]->name.len == expr->member.name.len &&
                            memcmp(enum_sym->members[ei]->name.str, expr->member.name.str,
                                   (size_t)expr->member.name.len) == 0) {
                            zan_ast_node_t *em_decl = enum_sym->members[ei]->decl;
                            if (em_decl && em_decl->kind == AST_ENUM_MEMBER &&
                                em_decl->enum_member.value &&
                                em_decl->enum_member.value->kind == AST_INT_LITERAL) {
                                enum_val = (int)em_decl->enum_member.value->int_val;
                            }
                            return LLVMConstInt(LLVMInt64TypeInContext(g->ctx),
                                               (uint64_t)enum_val, 0);
                        }
                        zan_ast_node_t *em_decl = enum_sym->members[ei]->decl;
                        if (em_decl && em_decl->kind == AST_ENUM_MEMBER &&
                            em_decl->enum_member.value &&
                            em_decl->enum_member.value->kind == AST_INT_LITERAL) {
                            enum_val = (int)em_decl->enum_member.value->int_val + 1;
                        } else {
                            enum_val++;
                        }
                    }
                }
                return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
            }
        }

        /* Static class/struct field access: ClassName.StaticField.
         * Load from the field's backing global (shared mutable storage);
         * the initializer is applied at main() entry, not folded here. */
        if (expr->member.object->kind == AST_IDENTIFIER &&
            !local_find(locals, expr->member.object->ident.name)) {
            zan_symbol_t *cs = zan_binder_lookup(g->binder, expr->member.object->ident.name);
            if (cs && (cs->kind == SYM_CLASS || cs->kind == SYM_STRUCT)) {
                zan_symbol_t *fs = get_field_sym(cs, expr->member.name);
                LLVMValueRef gv = get_static_field_global(g, cs, fs);
                if (gv) {
                    LLVMTypeRef ft = fs->type ? map_type(g, fs->type)
                                              : LLVMInt64TypeInContext(g->ctx);
                    return LLVMBuildLoad2(g->builder, ft, gv, "sfld");
                }
            }
        }

        /* Static method reference used as a delegate value: ClassName.Method
         * (not immediately called) → the function pointer. Instance-method
         * groups are not bound here (they'd require a captured receiver). */
        if (expr->member.object->kind == AST_IDENTIFIER &&
            !local_find(locals, expr->member.object->ident.name)) {
            zan_symbol_t *cs = zan_binder_lookup(g->binder, expr->member.object->ident.name);
            if (cs && (cs->kind == SYM_CLASS || cs->kind == SYM_STRUCT)) {
                zan_symbol_t *ms = get_method_sym(cs, expr->member.name);
                if (ms) {
                    for (int fi = 0; fi < g->function_count; fi++) {
                        if (g->functions[fi].sym == ms) {
                            return g->functions[fi].fn;
                        }
                    }
                }
            }
        }

        /* struct field access: obj.Field */
        if (expr->member.object->kind == AST_IDENTIFIER) {
            local_var_t *local = local_find(locals, expr->member.object->ident.name);
            if (local && local->type && (local->type->kind == TYPE_STRUCT || local->type->kind == TYPE_CLASS)) {
                zan_symbol_t *type_sym = local->type->sym;
                if (type_sym) {
                    int fi = get_field_index(type_sym, expr->member.name);
                    if (fi >= 0) {
                        LLVMTypeRef st = get_struct_llvm_type(g, type_sym);
                        if (st) {
                            /* load struct pointer or value, then GEP to field */
                            LLVMValueRef struct_ptr = struct_base_ptr(g, local, st);
                            LLVMValueRef field_ptr = LLVMBuildStructGEP2(g->builder, st, struct_ptr, (unsigned)fi, "fld");
                            zan_symbol_t *fsym = get_field_sym(type_sym, expr->member.name);
                            LLVMTypeRef field_type = fsym ? map_type(g, fsym->type) : LLVMInt64TypeInContext(g->ctx);
                            LLVMValueRef fv = LLVMBuildLoad2(g->builder, field_type, field_ptr, "fval");
                            if (fsym) {
                                zan_type_t *ct = subst_type_param(fsym->type, local->type);
                                if (ct != fsym->type) fv = emit_boundary_coerce(g, fv, map_type(g, ct));
                            }
                            return fv;
                        }
                    }
                }
            }
        }

        /* general field access: <expr>.field where <expr> yields a class
         * instance pointer (e.g. list[i].field, a.b.field, foo().field). */
        {
            zan_symbol_t *cls = expr_class_sym(g, expr->member.object, locals);
            if (cls) {
                int fi = get_field_index(cls, expr->member.name);
                if (fi >= 0) {
                    LLVMTypeRef st = get_struct_llvm_type(g, cls);
                    LLVMValueRef obj_val = emit_expr(g, expr->member.object, locals);
                    if (st && LLVMGetTypeKind(LLVMTypeOf(obj_val)) == LLVMPointerTypeKind) {
                        LLVMValueRef field_ptr = LLVMBuildStructGEP2(g->builder, st,
                            obj_val, (unsigned)fi, "gfld");
                        zan_symbol_t *fsym = get_field_sym(cls, expr->member.name);
                        LLVMTypeRef ft = fsym ? map_type(g, fsym->type)
                                              : LLVMInt64TypeInContext(g->ctx);
                        LLVMValueRef gfv = LLVMBuildLoad2(g->builder, ft, field_ptr, "gfval");
                        if (fsym) {
                            zan_type_t *rct = infer_expr_type(g, expr->member.object, locals);
                            zan_type_t *ct = subst_type_param(fsym->type, rct);
                            if (ct != fsym->type) gfv = emit_boundary_coerce(g, gfv, map_type(g, ct));
                        }
                        return gfv;
                    }
                }
            }
        }
        return LLVMConstInt(LLVMInt64TypeInContext(g->ctx), 0, 0);
    }

    case AST_INDEX: {
        /* arr[i] — array/list element access */
        LLVMValueRef arr_ptr = NULL;
        zan_type_t *arr_type = NULL;
        int is_list = 0;
        if (expr->index.object->kind == AST_IDENTIFIER) {
            local_var_t *local = local_find(locals, expr->index.object->ident.name);
            if (local) {
                arr_ptr = LLVMBuildLoad2(g->builder, LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0),
                    local->alloca, "arrload");
                arr_type = local->type;
                /* detect List type by checking if type name is "List" */
                if (arr_type && arr_type->name.len == 4 && memcmp(arr_type->name.str, "List", 4) == 0) {
                    is_list = 1;
                }
            } else if (g->current_type_sym) {
                /* implicit this.field[i] — bare identifier naming an array field */
                zan_symbol_t *fsym = get_field_sym(g->current_type_sym, expr->index.object->ident.name);
                if (fsym) {
                    arr_type = fsym->type;
                    arr_ptr = emit_expr(g, expr->index.object, locals);
                    if (arr_type && arr_type->name.len == 4 && memcmp(arr_type->name.str, "List", 4) == 0) {
                        is_list = 1;
                    }
                }
            }
        } else if (expr->index.object->kind == AST_MEMBER_ACCESS) {
            /* obj.field[i] — array stored in a struct/class field. Load the
             * field value (the array data pointer) and recover its element
             * type from the field declaration. */
            arr_type = member_access_field_type(g, locals, expr->index.object);
            if (arr_type) {
                arr_ptr = emit_expr(g, expr->index.object, locals);
                if (arr_type->name.len == 4 && memcmp(arr_type->name.str, "List", 4) == 0) {
                    is_list = 1;
                }
            }
        } else {
            /* General case: the base is any other expression that produces an
             * array/string/list value — e.g. a method call `foo()[i]`, a
             * parenthesized expression, or a nested index `a[i][j]`. Without
             * this, arr_ptr stays NULL and the whole access silently folds to
             * the constant 0 below. Evaluate the base value and recover its
             * element/container type from the expression. */
            arr_type = infer_expr_type(g, expr->index.object, locals);
            if (arr_type) {
                arr_ptr = emit_expr(g, expr->index.object, locals);
                if (arr_type->name.len == 4 && memcmp(arr_type->name.str, "List", 4) == 0) {
                    is_list = 1;
                }
            }
        }
        /* List indexer: list[i] -> load data[i] from list struct */
        if (arr_ptr && is_list) {
            LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
            LLVMValueRef list_ptr = LLVMBuildBitCast(g->builder, arr_ptr,
                LLVMPointerType(g->list_struct_type, 0), "lptr");
            LLVMValueRef data_field = LLVMBuildStructGEP2(g->builder, g->list_struct_type, list_ptr, 2, "df");
            LLVMValueRef data = LLVMBuildLoad2(g->builder, LLVMPointerType(i64, 0), data_field, "data");
            LLVMValueRef idx = emit_expr(g, expr->index.index, locals);
            if (LLVMGetTypeKind(LLVMTypeOf(idx)) == LLVMIntegerTypeKind &&
                LLVMGetIntTypeWidth(LLVMTypeOf(idx)) < 64) {
                idx = LLVMBuildSExt(g->builder, idx, i64, "ext");
            }
            LLVMValueRef elem_ptr = LLVMBuildGEP2(g->builder, i64, data, &idx, 1, "ep");
            LLVMValueRef raw = LLVMBuildLoad2(g->builder, i64, elem_ptr, "elem");
            /* reinterpret non-integer elements: slots physically hold an i64,
             * so pointer (class/string) elements need inttoptr and floating
             * (double) elements need a bitcast back to the value type. */
            zan_type_t *et = container_elem_type(arr_type);
            if (et) {
                LLVMTypeRef m = map_type(g, et);
                LLVMTypeKind mk = LLVMGetTypeKind(m);
                if (mk == LLVMPointerTypeKind)
                    return LLVMBuildIntToPtr(g->builder, raw, m, "elp");
                if (mk == LLVMDoubleTypeKind)
                    return LLVMBuildBitCast(g->builder, raw, m, "elf");
            }
            return raw;
        }
        /* Dict indexer: dict[key] -> linear scan for key, return value */
        if (arr_ptr && arr_type && arr_type->name.len == 4 && memcmp(arr_type->name.str, "Dict", 4) == 0) {
            LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
            LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMValueRef dp = LLVMBuildBitCast(g->builder, arr_ptr,
                LLVMPointerType(g->dict_struct_type, 0), "dp");
            LLVMValueRef cntp = LLVMBuildStructGEP2(g->builder, g->dict_struct_type, dp, 0, "cntp");
            LLVMValueRef cnt = LLVMBuildLoad2(g->builder, i64, cntp, "cnt");
            LLVMValueRef kp = LLVMBuildStructGEP2(g->builder, g->dict_struct_type, dp, 2, "kp");
            LLVMValueRef ks = LLVMBuildLoad2(g->builder, LLVMPointerType(i8ptr, 0), kp, "ks");
            LLVMValueRef vp = LLVMBuildStructGEP2(g->builder, g->dict_struct_type, dp, 3, "vp");
            LLVMValueRef vs = LLVMBuildLoad2(g->builder, LLVMPointerType(i64, 0), vp, "vs");
            LLVMValueRef search = coerce_dict_key(g, emit_expr(g, expr->index.index, locals));
            /* loop to find key */
            LLVMValueRef res = LLVMBuildAlloca(g->builder, i64, "dres");
            LLVMBuildStore(g->builder, LLVMConstInt(i64, 0, 0), res);
            LLVMValueRef idx_a = LLVMBuildAlloca(g->builder, i64, "di");
            LLVMBuildStore(g->builder, LLVMConstInt(i64, 0, 0), idx_a);
            LLVMBasicBlockRef cond_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "di.cond");
            LLVMBasicBlockRef body_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "di.body");
            LLVMBasicBlockRef done_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "di.done");
            LLVMBuildBr(g->builder, cond_bb);
            LLVMPositionBuilderAtEnd(g->builder, cond_bb);
            LLVMValueRef ci = LLVMBuildLoad2(g->builder, i64, idx_a, "ci");
            LLVMValueRef cdone = LLVMBuildICmp(g->builder, LLVMIntUGE, ci, cnt, "cdone");
            LLVMBuildCondBr(g->builder, cdone, done_bb, body_bb);
            LLVMPositionBuilderAtEnd(g->builder, body_bb);
            LLVMValueRef ci2 = LLVMBuildLoad2(g->builder, i64, idx_a, "ci2");
            LLVMValueRef kslot = LLVMBuildGEP2(g->builder, i8ptr, ks, &ci2, 1, "ksl");
            LLVMValueRef kv = LLVMBuildLoad2(g->builder, i8ptr, kslot, "kv");
            LLVMValueRef eq = emit_dict_key_eq(g, arr_type, kv, search);
            LLVMBasicBlockRef found_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "di.found");
            LLVMBasicBlockRef next_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "di.next");
            LLVMBuildCondBr(g->builder, eq, found_bb, next_bb);
            LLVMPositionBuilderAtEnd(g->builder, found_bb);
            LLVMValueRef vslot = LLVMBuildGEP2(g->builder, i64, vs, &ci2, 1, "vsl");
            LLVMValueRef val = LLVMBuildLoad2(g->builder, i64, vslot, "val");
            LLVMBuildStore(g->builder, val, res);
            LLVMBuildBr(g->builder, done_bb);
            LLVMPositionBuilderAtEnd(g->builder, next_bb);
            LLVMValueRef ni = LLVMBuildAdd(g->builder, ci2, LLVMConstInt(i64, 1, 0), "ni");
            LLVMBuildStore(g->builder, ni, idx_a);
            LLVMBuildBr(g->builder, cond_bb);
            LLVMPositionBuilderAtEnd(g->builder, done_bb);
            LLVMValueRef dval = LLVMBuildLoad2(g->builder, i64, res, "dval");
            zan_type_t *dvt = dict_value_type(arr_type);
            if (dvt) {
                LLVMTypeRef m = map_type(g, dvt);
                LLVMTypeKind mk = LLVMGetTypeKind(m);
                if (mk == LLVMPointerTypeKind)
                    return LLVMBuildIntToPtr(g->builder, dval, m, "dvalp");
                if (mk == LLVMDoubleTypeKind)
                    return LLVMBuildBitCast(g->builder, dval, m, "dvalf");
            }
            return dval;
        }
        /* string[i] — load a single byte (i8) and zero-extend to i32 so that
         * indexing a string yields the character code, enabling char-level
         * lexing of real source text. */
        if (arr_ptr && arr_type && arr_type->kind == TYPE_STRING) {
            LLVMValueRef idx = emit_expr(g, expr->index.index, locals);
            LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
            if (LLVMGetTypeKind(LLVMTypeOf(idx)) == LLVMIntegerTypeKind &&
                LLVMGetIntTypeWidth(LLVMTypeOf(idx)) != 64) {
                idx = LLVMBuildSExt(g->builder, idx, i64, "idxext");
            }
            LLVMTypeRef i8t = LLVMInt8TypeInContext(g->ctx);
            LLVMValueRef ch_ptr = LLVMBuildGEP2(g->builder, i8t, arr_ptr, &idx, 1, "chp");
            LLVMValueRef ch = LLVMBuildLoad2(g->builder, i8t, ch_ptr, "ch");
            return LLVMBuildZExt(g->builder, ch, LLVMInt64TypeInContext(g->ctx), "chz");
        }
        if (arr_ptr && arr_type) {
            LLVMValueRef idx = emit_expr(g, expr->index.index, locals);
            LLVMTypeRef elem_llvm = LLVMInt64TypeInContext(g->ctx);
            if (arr_type->element_type) {
                elem_llvm = map_type(g, arr_type->element_type);
            }
            LLVMValueRef typed_arr = LLVMBuildBitCast(g->builder, arr_ptr,
                LLVMPointerType(elem_llvm, 0), "arrp");
            LLVMValueRef elem_ptr = LLVMBuildGEP2(g->builder, elem_llvm, typed_arr, &idx, 1, "eidx");
            return LLVMBuildLoad2(g->builder, elem_llvm, elem_ptr, "elem");
        }
        return LLVMConstInt(LLVMInt64TypeInContext(g->ctx), 0, 0);
    }

    case AST_QUERY_EXPR: {
        /* from x in src [where c]... select e — lowered to an eager loop that
         * filters and projects into a fresh List<sel> */
        static int query_counter = 0;
        LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
        LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
        LLVMValueRef fn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder));
        int mark = locals->count;

        LLVMValueRef collection = emit_expr(g, expr->query.source, locals);
        zan_type_t *src_ty = infer_expr_type(g, expr->query.source, locals);
        zan_type_t *elem = container_elem_type(src_ty);
        if (!elem) elem = g->binder->type_int;
        LLVMTypeRef elem_llvm = map_type(g, elem);

        /* select type, inferred with the range var briefly in scope */
        local_add(locals, expr->query.var, NULL, elem);
        zan_type_t *sel = infer_expr_type(g, expr->query.select, locals);
        locals->count = mark;
        if (!sel) sel = elem;

        /* result list: synthesize and emit `new List<sel>()` */
        zan_ast_node_t *sel_tr = zan_ast_new(g->arena, AST_TYPE_REF, expr->loc);
        sel_tr->type_ref.name = sel->name;
        zan_ast_list_init(&sel_tr->type_ref.type_args);
        zan_ast_node_t *list_tr = zan_ast_new(g->arena, AST_TYPE_REF, expr->loc);
        list_tr->type_ref.name = (zan_istr_t){"List", 4};
        zan_ast_list_init(&list_tr->type_ref.type_args);
        zan_ast_list_push(&list_tr->type_ref.type_args, sel_tr, g->arena);
        zan_ast_node_t *new_list = zan_ast_new(g->arena, AST_NEW_EXPR, expr->loc);
        new_list->new_expr.type = list_tr;
        new_list->new_expr.is_array = false;
        zan_ast_list_init(&new_list->new_expr.args);
        LLVMValueRef list_val = emit_expr(g, new_list, locals);

        /* hidden local holding the result so a synthetic `__q.Add(e)` call
         * resolves through the normal List.Add path */
        char qbuf[32];
        snprintf(qbuf, sizeof(qbuf), "__q%d", query_counter++);
        char *qn = zan_arena_strdup(g->arena, qbuf, strlen(qbuf));
        zan_istr_t qname = {qn, (int)strlen(qn)};
        LLVMValueRef list_alloc = LLVMBuildAlloca(g->builder, LLVMTypeOf(list_val), "q");
        LLVMBuildStore(g->builder, list_val, list_alloc);
        local_add(locals, qname, list_alloc,
                  zan_binder_make_list_type(g->binder, sel));

        /* iteration state */
        LLVMValueRef col = LLVMBuildBitCast(g->builder, collection,
            LLVMPointerType(g->list_struct_type, 0), "qcol");
        LLVMValueRef cnt_ptr = LLVMBuildStructGEP2(g->builder, g->list_struct_type,
            col, 0, "qcntp");
        LLVMValueRef count = LLVMBuildLoad2(g->builder, i64, cnt_ptr, "qcnt");
        LLVMValueRef data_ptr = LLVMBuildStructGEP2(g->builder, g->list_struct_type,
            col, 2, "qdatap");
        LLVMValueRef data = LLVMBuildLoad2(g->builder, LLVMPointerType(i64, 0),
            data_ptr, "qdata");
        LLVMValueRef idx_alloc = LLVMBuildAlloca(g->builder, i64, "qi");
        LLVMBuildStore(g->builder, LLVMConstInt(i64, 0, 0), idx_alloc);
        LLVMValueRef iter_alloc = LLVMBuildAlloca(g->builder, elem_llvm, "qv");
        local_add(locals, expr->query.var, iter_alloc, elem);

        LLVMBasicBlockRef cond_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "q.cond");
        LLVMBasicBlockRef body_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "q.body");
        LLVMBasicBlockRef inc_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "q.inc");
        LLVMBasicBlockRef end_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "q.end");

        LLVMBuildBr(g->builder, cond_bb);
        LLVMPositionBuilderAtEnd(g->builder, cond_bb);
        LLVMValueRef idx_val = LLVMBuildLoad2(g->builder, i64, idx_alloc, "qiv");
        LLVMValueRef cmp = LLVMBuildICmp(g->builder, LLVMIntSLT, idx_val, count, "qcmp");
        LLVMBuildCondBr(g->builder, cmp, body_bb, end_bb);

        LLVMPositionBuilderAtEnd(g->builder, body_bb);
        LLVMValueRef elem_ptr = LLVMBuildGEP2(g->builder, i64, data, &idx_val, 1, "qep");
        LLVMValueRef ev = LLVMBuildLoad2(g->builder, i64, elem_ptr, "qelem");
        LLVMTypeKind ek = LLVMGetTypeKind(elem_llvm);
        if (ek == LLVMPointerTypeKind)
            ev = LLVMBuildIntToPtr(g->builder, ev, elem_llvm, "qelp");
        else if (ek == LLVMDoubleTypeKind)
            ev = LLVMBuildBitCast(g->builder, ev, elem_llvm, "qelf");
        else if (ek == LLVMIntegerTypeKind && LLVMGetIntTypeWidth(elem_llvm) < 64)
            ev = LLVMBuildTrunc(g->builder, ev, elem_llvm, "qelt");
        LLVMBuildStore(g->builder, ev, iter_alloc);

        /* where clauses: any false condition skips to the increment */
        for (int wi = 0; wi < expr->query.wheres.count; wi++) {
            LLVMValueRef c = emit_expr(g, expr->query.wheres.items[wi], locals);
            if (LLVMGetTypeKind(LLVMTypeOf(c)) == LLVMIntegerTypeKind &&
                LLVMGetIntTypeWidth(LLVMTypeOf(c)) != 1)
                c = LLVMBuildICmp(g->builder, LLVMIntNE, c,
                                  LLVMConstNull(LLVMTypeOf(c)), "qw");
            LLVMBasicBlockRef pass_bb =
                LLVMAppendBasicBlockInContext(g->ctx, fn, "q.pass");
            LLVMBuildCondBr(g->builder, c, pass_bb, inc_bb);
            LLVMPositionBuilderAtEnd(g->builder, pass_bb);
        }

        /* __q.Add(select) through the normal lowering */
        zan_ast_node_t *qid = zan_ast_new(g->arena, AST_IDENTIFIER, expr->loc);
        qid->ident.name = qname;
        zan_ast_node_t *madd = zan_ast_new(g->arena, AST_MEMBER_ACCESS, expr->loc);
        madd->member.object = qid;
        madd->member.name = (zan_istr_t){"Add", 3};
        madd->member.null_cond = 0;
        zan_ast_node_t *addcall = zan_ast_new(g->arena, AST_CALL, expr->loc);
        addcall->call.callee = madd;
        zan_ast_list_init(&addcall->call.args);
        zan_ast_list_init(&addcall->call.type_args);
        zan_ast_list_push(&addcall->call.args, expr->query.select, g->arena);
        emit_expr(g, addcall, locals);
        LLVMBuildBr(g->builder, inc_bb);

        LLVMPositionBuilderAtEnd(g->builder, inc_bb);
        LLVMValueRef next = LLVMBuildAdd(g->builder,
            LLVMBuildLoad2(g->builder, i64, idx_alloc, "qi2"),
            LLVMConstInt(i64, 1, 0), "qnext");
        LLVMBuildStore(g->builder, next, idx_alloc);
        LLVMBuildBr(g->builder, cond_bb);

        LLVMPositionBuilderAtEnd(g->builder, end_bb);
        emit_release_owned_call_temp(g, expr->query.source, collection, locals);
        locals->count = mark;
        (void)i8ptr;
        return list_val;
    }

    case AST_NEW_EXPR: {
        /* new List<T>() — built-in dynamic list */
        if (expr->new_expr.type && expr->new_expr.type->kind == AST_TYPE_REF) {
            zan_istr_t tname = expr->new_expr.type->type_ref.name;
            if (tname.len == 4 && memcmp(tname.str, "List", 4) == 0) {
                LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                /* allocate List struct on heap with an rc header (24 bytes:
                 * 3 * i64), so ARC frees it and its data buffer on release. */
                zan_type_t *lelem = NULL;
                {
                    zan_type_t *lt = zan_binder_resolve_type(g->binder, expr->new_expr.type);
                    if (lt) lelem = container_elem_type(lt);
                }
                LLVMValueRef list_ptr = emit_alloc_rc_collection(g, expr, 24, 1, lelem);
                /* cast to List* */
                LLVMValueRef typed_ptr = LLVMBuildBitCast(g->builder, list_ptr,
                    LLVMPointerType(g->list_struct_type, 0), "lptr");
                /* collection initializer items: new List<T>{ a, b, c }. The
                 * parser stores them in new_expr.args (List has no ctor args). */
                int ninit = expr->new_expr.args.count;
                long long initcap = ninit > 8 ? (long long)ninit : 8;
                /* count = ninit */
                LLVMValueRef count_ptr = LLVMBuildStructGEP2(g->builder, g->list_struct_type, typed_ptr, 0, "cnt");
                LLVMBuildStore(g->builder, LLVMConstInt(i64, (unsigned long long)ninit, 0), count_ptr);
                /* capacity = max(8, item count) */
                LLVMValueRef cap_ptr = LLVMBuildStructGEP2(g->builder, g->list_struct_type, typed_ptr, 1, "cap");
                LLVMBuildStore(g->builder, LLVMConstInt(i64, (unsigned long long)initcap, 0), cap_ptr);
                /* allocate initial data buffer: capacity * sizeof(i64) */
                LLVMValueRef data_size = LLVMConstInt(i64, (unsigned long long)(initcap * 8), 0);
                LLVMValueRef data_ptr = zan_call2(g->builder,
                    LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i64, i64 }, 2, 0),
                    get_calloc_fn(g), (LLVMValueRef[]){ LLVMConstInt(i64, 1, 0), data_size }, 2, "data");
                LLVMValueRef data_typed = LLVMBuildBitCast(g->builder, data_ptr,
                    LLVMPointerType(i64, 0), "dptr");
                LLVMValueRef data_field = LLVMBuildStructGEP2(g->builder, g->list_struct_type, typed_ptr, 2, "df");
                LLVMBuildStore(g->builder, data_typed, data_field);
                /* store each initializer item (with proper rc retain semantics) */
                for (int ii = 0; ii < ninit; ii++) {
                    zan_ast_node_t *item = expr->new_expr.args.items[ii];
                    LLVMValueRef idxk = LLVMConstInt(i64, (unsigned long long)ii, 0);
                    LLVMValueRef slot = LLVMBuildGEP2(g->builder, i64, data_typed, &idxk, 1, "iis");
                    LLVMValueRef ival = emit_expr(g, item, locals);
                    emit_collection_slot_store(g, lelem, i64, slot, ival, item, locals, 0);
                }
                return LLVMBuildBitCast(g->builder, typed_ptr, i8ptr, "listv");
            }
        }

        /* new StringBuilder() — built-in growable byte buffer */
        if (expr->new_expr.type && expr->new_expr.type->kind == AST_TYPE_REF) {
            zan_istr_t sbname = expr->new_expr.type->type_ref.name;
            if (sbname.len == 13 && memcmp(sbname.str, "StringBuilder", 13) == 0) {
                LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                /* allocate StringBuilder struct { i64 count, i64 cap, i8* data }
                 * = 24 bytes, with an rc header so ARC frees the struct and its
                 * data buffer on release. */
                LLVMValueRef sb_raw = emit_alloc_rc_collection(g, expr, 24, 2, NULL);
                LLVMValueRef sb_ptr = LLVMBuildBitCast(g->builder, sb_raw,
                    LLVMPointerType(g->sb_struct_type, 0), "sbp");
                LLVMValueRef cnt_p = LLVMBuildStructGEP2(g->builder, g->sb_struct_type, sb_ptr, 0, "sbc");
                LLVMBuildStore(g->builder, LLVMConstInt(i64, 0, 0), cnt_p);
                LLVMValueRef cap_p = LLVMBuildStructGEP2(g->builder, g->sb_struct_type, sb_ptr, 1, "sbcap");
                LLVMBuildStore(g->builder, LLVMConstInt(i64, 0, 0), cap_p);
                LLVMValueRef data_p = LLVMBuildStructGEP2(g->builder, g->sb_struct_type, sb_ptr, 2, "sbd");
                LLVMBuildStore(g->builder, LLVMConstNull(i8ptr), data_p);
                return LLVMBuildBitCast(g->builder, sb_ptr, i8ptr, "sbv");
            }
        }

        /* new Dict<K,V>() — built-in hash map */
        if (expr->new_expr.type && expr->new_expr.type->kind == AST_TYPE_REF) {
            zan_istr_t tname2 = expr->new_expr.type->type_ref.name;
            if ((tname2.len == 4 && memcmp(tname2.str, "Dict", 4) == 0) ||
                (tname2.len == 10 && memcmp(tname2.str, "Dictionary", 10) == 0)) {
                LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                /* allocate Dict struct: { i64 count, i64 capacity, i8** keys, i64* values } = 32 bytes */
                LLVMValueRef dict_size = LLVMConstInt(i64, 32, 0);
                LLVMValueRef dict_raw = zan_call2(g->builder,
                    LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i64, i64 }, 2, 0),
                    get_calloc_fn(g), (LLVMValueRef[]){ LLVMConstInt(i64, 1, 0), dict_size }, 2, "dict");
                LLVMValueRef typed_ptr = LLVMBuildBitCast(g->builder, dict_raw,
                    LLVMPointerType(g->dict_struct_type, 0), "dptr");
                /* count = 0 */
                LLVMValueRef cnt_p = LLVMBuildStructGEP2(g->builder, g->dict_struct_type, typed_ptr, 0, "cnt");
                LLVMBuildStore(g->builder, LLVMConstInt(i64, 0, 0), cnt_p);
                /* capacity = 16 */
                LLVMValueRef cap_p = LLVMBuildStructGEP2(g->builder, g->dict_struct_type, typed_ptr, 1, "cap");
                LLVMBuildStore(g->builder, LLVMConstInt(i64, 16, 0), cap_p);
                /* keys = malloc(16 * 8) */
                LLVMValueRef keys_sz = LLVMConstInt(i64, 128, 0);
                LLVMValueRef keys_raw = zan_call2(g->builder,
                    LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i64, i64 }, 2, 0),
                    get_calloc_fn(g), (LLVMValueRef[]){ LLVMConstInt(i64, 1, 0), keys_sz }, 2, "keys");
                LLVMValueRef keys_typed = LLVMBuildBitCast(g->builder, keys_raw,
                    LLVMPointerType(i8ptr, 0), "kptr");
                LLVMValueRef kf = LLVMBuildStructGEP2(g->builder, g->dict_struct_type, typed_ptr, 2, "kf");
                LLVMBuildStore(g->builder, keys_typed, kf);
                /* values = malloc(16 * 8) */
                LLVMValueRef vals_sz = LLVMConstInt(i64, 128, 0);
                LLVMValueRef vals_raw = zan_call2(g->builder,
                    LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i64, i64 }, 2, 0),
                    get_calloc_fn(g), (LLVMValueRef[]){ LLVMConstInt(i64, 1, 0), vals_sz }, 2, "vals");
                LLVMValueRef vals_typed = LLVMBuildBitCast(g->builder, vals_raw,
                    LLVMPointerType(i64, 0), "vptr");
                LLVMValueRef vf = LLVMBuildStructGEP2(g->builder, g->dict_struct_type, typed_ptr, 3, "vf");
                LLVMBuildStore(g->builder, vals_typed, vf);
                return LLVMBuildBitCast(g->builder, typed_ptr, i8ptr, "dictv");
            }
        }

        /* new Type[size] — array creation */
        if (expr->new_expr.is_array && expr->new_expr.args.count > 0) {
            zan_type_t *elem_type = zan_binder_resolve_type(g->binder, expr->new_expr.type);
            if (!elem_type) elem_type = g->binder->type_int;
            LLVMTypeRef elem_llvm = map_type(g, elem_type);
            LLVMValueRef size_val = emit_expr(g, expr->new_expr.args.items[0], locals);

            /* calloc(size, sizeof(elem)) */
            LLVMValueRef elem_size = LLVMSizeOf(elem_llvm);
            LLVMValueRef total = LLVMBuildMul(g->builder,
                LLVMBuildZExt(g->builder, size_val, LLVMInt64TypeInContext(g->ctx), "zext"),
                elem_size, "total");
            LLVMValueRef arr = zan_call2(g->builder,
                LLVMFunctionType(LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0),
                    (LLVMTypeRef[]){LLVMInt64TypeInContext(g->ctx), LLVMInt64TypeInContext(g->ctx)}, 2, 0),
                get_calloc_fn(g), (LLVMValueRef[]){ total, LLVMConstInt(LLVMInt64TypeInContext(g->ctx), 1, 0) }, 2, "arr");
            return arr;
        }

        /* new ClassName(args) — allocate struct + call constructor */
        zan_istr_t type_name = {NULL, 0};
        if (expr->new_expr.type) {
            if (expr->new_expr.type->kind == AST_IDENTIFIER) {
                type_name = expr->new_expr.type->ident.name;
            } else if (expr->new_expr.type->kind == AST_TYPE_REF) {
                type_name = expr->new_expr.type->type_ref.name;
            }
        }
        if (type_name.str) {
            zan_symbol_t *sym = zan_binder_lookup(g->binder, type_name);
            if (sym && sym->type && (sym->type->kind == TYPE_STRUCT || sym->type->kind == TYPE_CLASS)) {
                LLVMTypeRef st = get_struct_llvm_type(g, sym);
                if (st) {
                    LLVMValueRef alloca;
                    if (sym->type->kind == TYPE_CLASS) {
                        /* reference type: heap-allocate via ARC so instances
                         * outlive the enclosing frame and are leak-tracked. */
                        LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                        LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                        LLVMValueRef sz = LLVMSizeOf(st);
                        LLVMValueRef site_name = LLVMConstNull(i8ptr);
                        LLVMValueRef site_val = LLVMConstInt(i64, 0, 0);
                        {
                            /* Assign a stable allocation-site index (always: it
                             * also keys dynamic release dispatch to this site's
                             * concrete class). The "file:line:col" descriptor is
                             * only needed for --check-leaks reporting. */
                            int site_idx = g->leak_site_count;
                            if (site_idx >= ZAN_MAX_LEAK_SITES) site_idx = ZAN_MAX_LEAK_SITES - 1;
                            else g->leak_site_count++;
                            if (g->site_syms) g->site_syms[site_idx] = sym;
                            site_val = LLVMConstInt(i64, (unsigned long long)site_idx, 0);
                            if (g->check_leaks) {
                                char site_buf[600];
                                const char *sfile = g->src_file ? g->src_file : "<unknown>";
                                snprintf(site_buf, sizeof(site_buf), "%s:%u:%u",
                                         sfile, expr->loc.line, expr->loc.col);
                                site_name = LLVMBuildGlobalStringPtr(g->builder, site_buf, "site");
                            }
                        }
                        LLVMTypeRef alloc_fn_type = LLVMFunctionType(i8ptr,
                            (LLVMTypeRef[]){ i64, i64, i8ptr }, 3, 0);
                        LLVMValueRef alloc_args3[] = { sz, site_val, site_name };
                        LLVMValueRef raw = zan_call2(g->builder, alloc_fn_type, g->rt_alloc, alloc_args3, 3, "newobj");
                        alloca = LLVMBuildBitCast(g->builder, raw, LLVMPointerType(st, 0), "objp");
                    } else {
                        /* value type: stack-allocate as before */
                        alloca = LLVMBuildAlloca(g->builder, st, "new");
                    }
                    LLVMBuildStore(g->builder, LLVMConstNull(st), alloca);

                    /* install the vtable pointer (field 0) before the ctor runs
                     * so virtual calls made during construction dispatch to the
                     * most-derived implementation. */
                    if (sym->type->kind == TYPE_CLASS && class_has_virtual_methods(sym)) {
                        LLVMTypeRef i8ptr_vt = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                        LLVMValueRef vtg = get_vtable_global(g, sym);
                        LLVMValueRef vpf0 = LLVMBuildStructGEP2(g->builder, st, alloca, 0, "vpf.init");
                        LLVMBuildStore(g->builder,
                            LLVMBuildBitCast(g->builder, vtg, i8ptr_vt, "vt.i8"), vpf0);
                    }

                    /* look for constructor. For a concrete instantiation of a
                     * user generic class, prefer the specialized constructor so
                     * the body runs with the instantiation context. */
                    zan_type_t *new_inst = zan_binder_resolve_type(g->binder, expr->new_expr.type);
                    LLVMValueRef ctor_fn = NULL;
                    LLVMTypeRef ctor_ft = NULL;
                    if (new_inst && new_inst->type_arg_count > 0)
                        ctor_fn = find_generic_ctor(g, sym, new_inst->type_args,
                                                    new_inst->type_arg_count,
                                                    expr->new_expr.args.count, &ctor_ft);
                    if (!ctor_fn) {
                        for (int ci = 0; ci < g->ctor_count; ci++) {
                            if (g->ctors[ci].type_sym == sym) {
                                ctor_fn = g->ctors[ci].fn;
                                ctor_ft = g->ctors[ci].fn_type;
                                break;
                            }
                        }
                    }
                    if (ctor_fn) {
                        int argc = expr->new_expr.args.count + 1;
                        LLVMValueRef *call_args = (LLVMValueRef *)calloc((size_t)argc, sizeof(LLVMValueRef));
                        call_args[0] = alloca; /* this ptr */
                        for (int k = 0; k < expr->new_expr.args.count; k++) {
                            call_args[k + 1] = emit_expr(g, expr->new_expr.args.items[k], locals);
                        }
                        coerce_args_to_params(g, ctor_ft, call_args, argc);
                        zan_call2(g->builder, ctor_ft, ctor_fn, call_args, (unsigned)argc, "");
                        for (int k = 0; k < expr->new_expr.args.count; k++) {
                            emit_release_owned_call_temp(g, expr->new_expr.args.items[k],
                                call_args[k + 1], locals);
                        }
                        free(call_args);
                    }

                    /* if no constructor, handle field initializers */
                    if (g->ctor_count == 0 || 1) {
                        for (int i = 0; i < expr->new_expr.args.count; i++) {
                            zan_ast_node_t *arg = expr->new_expr.args.items[i];
                            if (arg->kind == AST_ASSIGNMENT && arg->binary.left->kind == AST_IDENTIFIER) {
                                int fi = get_field_index(sym, arg->binary.left->ident.name);
                                if (fi >= 0) {
                                    LLVMValueRef fptr = LLVMBuildStructGEP2(g->builder, st, alloca, (unsigned)fi, "finit");
                                    zan_symbol_t *fsym = get_field_sym(sym, arg->binary.left->ident.name);
                                    LLVMValueRef fval =
                                        (fsym && fsym->type && fsym->type->kind == TYPE_DELEGATE &&
                                         arg->binary.right->kind == AST_LAMBDA)
                                            ? emit_lambda_typed(g, arg->binary.right, fsym->type, locals)
                                            : emit_expr(g, arg->binary.right, locals);
                                    if (fsym && fsym->type) {
                                        LLVMTypeRef target_t = map_type(g, fsym->type);
                                        LLVMTypeRef val_t = LLVMTypeOf(fval);
                                        if (LLVMGetTypeKind(target_t) == LLVMFloatTypeKind &&
                                            LLVMGetTypeKind(val_t) == LLVMDoubleTypeKind) {
                                            fval = LLVMBuildFPTrunc(g->builder, fval, target_t, "trunc");
                                        }
                                    }
                                    /* ARC: retain a borrowed RC value captured into the
                                     * field so it survives release of any source local. */
                                    if (fsym && fsym->type && is_rc_managed_type(fsym->type) &&
                                        !expr_yields_owned_ref(arg->binary.right)) {
                                        emit_rc_retain_for_type(g, fsym->type, fval);
                                    }
                                    LLVMBuildStore(g->builder, fval, fptr);
                                }
                            }
                        }
                    }
                    return alloca;
                }
            }
        }
        return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
    }

    case AST_CONDITIONAL: {
        /* ternary: condition ? then_expr : else_expr */
        LLVMValueRef cond = emit_expr(g, expr->conditional.cond, locals);
        /* normalize to i1 */
        if (LLVMTypeOf(cond) != LLVMInt1TypeInContext(g->ctx)) {
            cond = LLVMBuildICmp(g->builder, LLVMIntNE, cond,
                                 LLVMConstInt(LLVMTypeOf(cond), 0, 0), "cond");
        }
        LLVMBasicBlockRef then_bb = LLVMAppendBasicBlockInContext(g->ctx,
            LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder)), "tern.then");
        LLVMBasicBlockRef else_bb = LLVMAppendBasicBlockInContext(g->ctx,
            LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder)), "tern.else");
        LLVMBasicBlockRef merge_bb = LLVMAppendBasicBlockInContext(g->ctx,
            LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder)), "tern.merge");
        LLVMBuildCondBr(g->builder, cond, then_bb, else_bb);

        LLVMPositionBuilderAtEnd(g->builder, then_bb);
        LLVMValueRef then_val = emit_expr(g, expr->conditional.then_expr, locals);
        LLVMBasicBlockRef then_end = LLVMGetInsertBlock(g->builder);
        LLVMBuildBr(g->builder, merge_bb);

        LLVMPositionBuilderAtEnd(g->builder, else_bb);
        LLVMValueRef else_val = emit_expr(g, expr->conditional.else_expr, locals);
        LLVMBasicBlockRef else_end = LLVMGetInsertBlock(g->builder);
        LLVMBuildBr(g->builder, merge_bb);

        LLVMPositionBuilderAtEnd(g->builder, merge_bb);
        LLVMValueRef phi = LLVMBuildPhi(g->builder, LLVMTypeOf(then_val), "tern");
        LLVMValueRef incoming_vals[] = { then_val, else_val };
        LLVMBasicBlockRef incoming_bbs[] = { then_end, else_end };
        LLVMAddIncoming(phi, incoming_vals, incoming_bbs, 2);
        return phi;
    }

    case AST_POSTFIX_UNARY: {
        /* x++ or x-- */
        LLVMValueRef ptr = NULL;
        if (expr->unary.operand->kind == AST_IDENTIFIER) {
            local_var_t *lv = local_find(locals, expr->unary.operand->ident.name);
            if (lv) ptr = lv->alloca;
        }
        if (!ptr) return LLVMConstInt(LLVMInt64TypeInContext(g->ctx), 0, 0);
        LLVMValueRef old_val = LLVMBuildLoad2(g->builder,
            LLVMInt64TypeInContext(g->ctx), ptr, "post");
        LLVMValueRef one = LLVMConstInt(LLVMInt64TypeInContext(g->ctx), 1, 0);
        LLVMValueRef new_val;
        if (expr->unary.op == TK_PLUS_PLUS) {
            new_val = LLVMBuildAdd(g->builder, old_val, one, "inc");
        } else {
            new_val = LLVMBuildSub(g->builder, old_val, one, "dec");
        }
        LLVMBuildStore(g->builder, new_val, ptr);
        return old_val; /* postfix returns old value */
    }

    case AST_IS_EXPR: {
        /* x is Type — runtime type check (simplified: always true for matching static types) */
        return LLVMConstInt(LLVMInt1TypeInContext(g->ctx), 1, 0);
    }

    case AST_AS_EXPR: {
        /* x as Type — type cast (simplified: pass through value) */
        return emit_expr(g, expr->type_test.expr, locals);
    }

    case AST_CAST_EXPR: {
        /* (Type)x — explicit numeric cast honoring the target type. */
        LLVMValueRef val = emit_expr(g, expr->cast.expr, locals);
        zan_type_t *tt = zan_binder_resolve_type(g->binder, expr->cast.type);
        LLVMTypeRef target = tt ? map_type(g, tt) : LLVMInt64TypeInContext(g->ctx);
        LLVMTypeRef src = LLVMTypeOf(val);
        /* Unsigned/small targets share the i64 representation, so their cast
         * semantics are wrap-to-width masks (zero-extend for uint/ushort,
         * sign-extend from 8 bits for sbyte) applied on the i64 value. */
        if (tt && LLVMGetTypeKind(src) == LLVMIntegerTypeKind &&
            LLVMGetIntTypeWidth(src) == 64) {
            LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
            switch (tt->kind) {
            case TYPE_UINT:
                return LLVMBuildAnd(g->builder, val,
                    LLVMConstInt(i64t, 0xFFFFFFFFull, 0), "cast.u32");
            case TYPE_USHORT:
                return LLVMBuildAnd(g->builder, val,
                    LLVMConstInt(i64t, 0xFFFFull, 0), "cast.u16");
            case TYPE_SBYTE: {
                LLVMValueRef sh = LLVMConstInt(i64t, 56, 0);
                LLVMValueRef up = LLVMBuildShl(g->builder, val, sh, "cast.i8.l");
                return LLVMBuildAShr(g->builder, up, sh, "cast.i8");
            }
            case TYPE_ULONG:
                return val;
            default:
                break;
            }
        }
        if (src == target) return val;
        LLVMTypeKind sk = LLVMGetTypeKind(src);
        LLVMTypeKind tk = LLVMGetTypeKind(target);
        bool src_fp = (sk == LLVMDoubleTypeKind || sk == LLVMFloatTypeKind);
        bool tgt_fp = (tk == LLVMDoubleTypeKind || tk == LLVMFloatTypeKind);
        if (sk == LLVMIntegerTypeKind && tk == LLVMIntegerTypeKind) {
            unsigned sw = LLVMGetIntTypeWidth(src);
            unsigned tw = LLVMGetIntTypeWidth(target);
            if (tw < sw) return LLVMBuildTrunc(g->builder, val, target, "cast");
            if (tw > sw) return LLVMBuildSExt(g->builder, val, target, "cast");
            return val;
        }
        if (src_fp && tk == LLVMIntegerTypeKind)
            return LLVMBuildFPToSI(g->builder, val, target, "cast");
        if (sk == LLVMIntegerTypeKind && tgt_fp)
            return LLVMBuildSIToFP(g->builder, val, target, "cast");
        if (src_fp && tgt_fp) {
            if (sk == LLVMDoubleTypeKind && tk == LLVMFloatTypeKind)
                return LLVMBuildFPTrunc(g->builder, val, target, "cast");
            if (sk == LLVMFloatTypeKind && tk == LLVMDoubleTypeKind)
                return LLVMBuildFPExt(g->builder, val, target, "cast");
        }
        return val;
    }

    case AST_SIZEOF_EXPR: {
        /* sizeof(Type) — return size in bytes */
        return LLVMConstInt(LLVMInt64TypeInContext(g->ctx), 8, 0);
    }

    case AST_TYPEOF_EXPR: {
        /* typeof(T) — the type's display name as a string */
        char buf[256];
        int len = render_type_ref_name(expr->cast.type, buf, (int)sizeof(buf));
        char *copy = zan_arena_strdup(g->arena, buf, (size_t)len);
        return emit_string_literal_rc(g, (zan_istr_t){ copy, (uint32_t)len });
    }

    case AST_THIS_EXPR: {
        /* `this` — load from the receiver alloca (g->current_this) when set. That
         * alloca is the single source of truth for the receiver and, in an async
         * method, is reloaded from the heap frame at every state block. The
         * function's param 0 is NOT usable here for async methods: the resume
         * function's param 0 is the frame pointer, not the receiver. Fall back to
         * param 0 only when there is no receiver alloca (defensive). */
        if (g->current_this) {
            return LLVMBuildLoad2(g->builder, LLVMGetAllocatedType(g->current_this),
                g->current_this, "this");
        }
        LLVMValueRef fn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder));
        if (LLVMCountParams(fn) > 0) {
            return LLVMGetParam(fn, 0);
        }
        return LLVMConstNull(LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0));
    }

    case AST_BASE_EXPR: {
        /* base — same as this for single inheritance (see AST_THIS_EXPR). */
        if (g->current_this) {
            return LLVMBuildLoad2(g->builder, LLVMGetAllocatedType(g->current_this),
                g->current_this, "this");
        }
        LLVMValueRef fn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder));
        if (LLVMCountParams(fn) > 0) {
            return LLVMGetParam(fn, 0);
        }
        return LLVMConstNull(LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0));
    }

    case AST_AWAIT_EXPR: {
        /* await Task.Delay(ms) — time-based suspension (no sub-frame). Inside an
         * async body: register a one-shot timer that will re-ready this frame at
         * now+ms, then SUSPEND; the resume-k block just continues. At a non-async
         * root, sleep synchronously. Delay yields no value (returns i64 0). */
        if (is_call_to(expr->await_expr.expr, "Task", "Delay") &&
            expr->await_expr.expr->call.args.count == 1) {
            LLVMTypeRef di64 = LLVMInt64TypeInContext(g->ctx);
            LLVMTypeRef di32 = LLVMInt32TypeInContext(g->ctx);
            LLVMTypeRef di8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMValueRef ms = emit_expr(g, expr->await_expr.expr->call.args.items[0], locals);
            if (LLVMTypeOf(ms) != di64) {
                ms = LLVMBuildIntCast2(g->builder, ms, di64, 1, "ms64");
            }
            if (g->current_async_frame && g->current_async_switch) {
                int k = g->current_async_next_state++;
                LLVMValueRef selfframe = g->current_async_frame;
                LLVMTypeRef self_ft = g->current_async_frame_type;
                LLVMValueRef self_i8 = LLVMBuildBitCast(g->builder, selfframe, di8ptr, "self");
                emit_async_save_slots(g);
                LLVMBuildStore(g->builder, LLVMConstInt(di32, (unsigned)k, 0),
                    LLVMBuildStructGEP2(g->builder, self_ft, selfframe, ASYNC_FRAME_STATE, "self.state"));
                zan_call2(g->builder, g->rt_co_delay_type, g->rt_co_delay,
                    (LLVMValueRef[]){ ms, self_i8, g->current_async_resume_fn }, 3, "");
                LLVMBuildRetVoid(g->builder);

                LLVMBasicBlockRef rk = LLVMAppendBasicBlockInContext(g->ctx,
                    g->current_async_resume_fn, "co.resume");
                LLVMAddCase(g->current_async_switch, LLVMConstInt(di32, (unsigned)k, 0), rk);
                LLVMPositionBuilderAtEnd(g->builder, rk);
                emit_async_reload_slots(g);
                return LLVMConstInt(di64, 0, 0);
            }
            /* root: sleep synchronously (no scheduler frame to suspend). Use the
             * same platform primitive the scheduler uses (Sleep / poll), already
             * declared in the module by the runtime emission. */
            LLVMValueRef ms32 = LLVMBuildTrunc(g->builder, ms, di32, "ms32");
            if (g->target_is_windows) {
                LLVMValueRef fn_sleep = LLVMGetNamedFunction(g->mod, "Sleep");
                if (fn_sleep) {
                    LLVMTypeRef sl_args[] = { di32 };
                    LLVMTypeRef sl_ty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), sl_args, 1, 0);
                    zan_call2(g->builder, sl_ty, fn_sleep, (LLVMValueRef[]){ ms32 }, 1, "");
                }
            } else {
                LLVMValueRef fn_poll = LLVMGetNamedFunction(g->mod, "poll");
                if (fn_poll) {
                    LLVMTypeRef pl_args[] = { di8ptr, di64, di32 };
                    LLVMTypeRef pl_ty = LLVMFunctionType(di32, pl_args, 3, 0);
                    zan_call2(g->builder, pl_ty, fn_poll,
                        (LLVMValueRef[]){ LLVMConstNull(di8ptr), LLVMConstInt(di64, 0, 0), ms32 }, 3, "");
                }
            }
            return LLVMConstInt(di64, 0, 0);
        }

        /* await Gate.Park(handle) — event-driven coroutine wait. Mirrors the
         * Task.Delay lowering but suspends on a runtime gate rather than a
         * timer: the frame is re-readied by zan_gate_signal (see rt_io.c).
         * Yields no value; only valid inside an async body. */
        if (is_call_to(expr->await_expr.expr, "Gate", "Park") &&
            expr->await_expr.expr->call.args.count == 1) {
            LLVMTypeRef di64 = LLVMInt64TypeInContext(g->ctx);
            LLVMTypeRef di32 = LLVMInt32TypeInContext(g->ctx);
            LLVMTypeRef di8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            if (!(g->current_async_frame && g->current_async_switch)) {
                zan_diag_emit(g->diag, DIAG_ERROR, expr->loc,
                    "await Gate.Park is only supported inside an async method");
                return LLVMConstInt(di64, 0, 0);
            }
            LLVMValueRef handle = emit_expr(g, expr->await_expr.expr->call.args.items[0], locals);
            if (LLVMTypeOf(handle) != di64) {
                handle = LLVMBuildIntCast2(g->builder, handle, di64, 1, "gate64");
            }
            /* the gate runtime rides in the socket-async reactor object; force
             * it to be linked whenever a program parks on a gate. */
            g->uses_socket_async = true;
            LLVMTypeRef gate_park_type = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx),
                (LLVMTypeRef[]){ di64, di8ptr, g->co_step_ptr }, 3, 0);
            LLVMValueRef gate_park = LLVMGetNamedFunction(g->mod, "zan_gate_park");
            if (!gate_park) {
                gate_park = LLVMAddFunction(g->mod, "zan_gate_park", gate_park_type);
            }
            int k = g->current_async_next_state++;
            LLVMValueRef selfframe = g->current_async_frame;
            LLVMTypeRef self_ft = g->current_async_frame_type;
            LLVMValueRef self_i8 = LLVMBuildBitCast(g->builder, selfframe, di8ptr, "self");
            emit_async_save_slots(g);
            LLVMBuildStore(g->builder, LLVMConstInt(di32, (unsigned)k, 0),
                LLVMBuildStructGEP2(g->builder, self_ft, selfframe, ASYNC_FRAME_STATE, "self.state"));
            zan_call2(g->builder, gate_park_type, gate_park,
                (LLVMValueRef[]){ handle, self_i8, g->current_async_resume_fn }, 3, "");
            LLVMBuildRetVoid(g->builder);

            LLVMBasicBlockRef rk = LLVMAppendBasicBlockInContext(g->ctx,
                g->current_async_resume_fn, "co.resume");
            LLVMAddCase(g->current_async_switch, LLVMConstInt(di32, (unsigned)k, 0), rk);
            LLVMPositionBuilderAtEnd(g->builder, rk);
            emit_async_reload_slots(g);
            return LLVMConstInt(di64, 0, 0);
        }

        /* await Socket.ReadReady(fd) / Socket.WriteReady(fd) — readiness-based
         * suspension driven by the IO reactor (S4b-2, path A). Inside an async
         * body: register a one-shot fd watcher via zan_io_wait_co that re-readies
         * this frame when the fd becomes readable/writable, then SUSPEND; the
         * resume-k block continues once ready. The intrinsic yields no value
         * (returns i64 0). Only valid inside an async body — there is no CPS frame
         * to suspend at a non-async root. */
        {
            bool is_read  = is_call_to(expr->await_expr.expr, "Socket", "ReadReady");
            bool is_write = is_call_to(expr->await_expr.expr, "Socket", "WriteReady");
            if ((is_read || is_write) && expr->await_expr.expr->call.args.count == 1) {
                LLVMTypeRef di64 = LLVMInt64TypeInContext(g->ctx);
                LLVMTypeRef di32 = LLVMInt32TypeInContext(g->ctx);
                LLVMTypeRef di8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                if (!(g->current_async_frame && g->current_async_switch)) {
                    zan_diag_emit(g->diag, DIAG_ERROR, expr->loc,
                        "await Socket.%s is only supported inside an async method",
                        is_read ? "ReadReady" : "WriteReady");
                    return LLVMConstInt(di64, 0, 0);
                }
                LLVMValueRef fd = emit_expr(g, expr->await_expr.expr->call.args.items[0], locals);
                if (LLVMTypeOf(fd) != di64) {
                    fd = LLVMBuildIntCast2(g->builder, fd, di64, 1, "fd64");
                }
                /* ZAN_IO_READ = 1, ZAN_IO_WRITE = 2 (see rt_io.h) */
                LLVMValueRef interest = LLVMConstInt(di32, is_read ? 1 : 2, 0);
                g->uses_socket_async = true;

                int k = g->current_async_next_state++;
                LLVMValueRef selfframe = g->current_async_frame;
                LLVMTypeRef self_ft = g->current_async_frame_type;
                LLVMValueRef self_i8 = LLVMBuildBitCast(g->builder, selfframe, di8ptr, "self");
                emit_async_save_slots(g);
                LLVMBuildStore(g->builder, LLVMConstInt(di32, (unsigned)k, 0),
                    LLVMBuildStructGEP2(g->builder, self_ft, selfframe, ASYNC_FRAME_STATE, "self.state"));
                zan_call2(g->builder, g->rt_io_wait_co_type, g->rt_io_wait_co,
                    (LLVMValueRef[]){ fd, interest, self_i8, g->current_async_resume_fn }, 4, "");
                LLVMBuildRetVoid(g->builder);

                LLVMBasicBlockRef rk = LLVMAppendBasicBlockInContext(g->ctx,
                    g->current_async_resume_fn, "co.resume");
                LLVMAddCase(g->current_async_switch, LLVMConstInt(di32, (unsigned)k, 0), rk);
                LLVMPositionBuilderAtEnd(g->builder, rk);
                emit_async_reload_slots(g);
                return LLVMConstInt(di64, 0, 0);
            }
        }

        /* await Socket.RecvOv(fd, buf, len) — overlapped receive. Unlike
         * ReadReady (a bare readiness probe followed by a synchronous recv in
         * zan), this posts the real recv via zan_io_recv_co and SUSPENDS; the
         * byte count is written by the reactor into this frame's RESULT slot
         * before it is re-readied, and the resume-k block loads it as the await
         * value. Issuing the recv as one op removes the probe/recv window that
         * leaked completions under the multi-worker driver at high load. */
        {
            if (is_call_to(expr->await_expr.expr, "Socket", "RecvOv") &&
                expr->await_expr.expr->call.args.count == 3) {
                LLVMTypeRef di64 = LLVMInt64TypeInContext(g->ctx);
                LLVMTypeRef di32 = LLVMInt32TypeInContext(g->ctx);
                LLVMTypeRef di8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                if (!(g->current_async_frame && g->current_async_switch)) {
                    zan_diag_emit(g->diag, DIAG_ERROR, expr->loc,
                        "await Socket.RecvOv is only supported inside an async method");
                    return LLVMConstInt(di64, 0, 0);
                }
                LLVMValueRef fd = emit_expr(g, expr->await_expr.expr->call.args.items[0], locals);
                if (LLVMTypeOf(fd) != di64)
                    fd = LLVMBuildIntCast2(g->builder, fd, di64, 1, "fd64");
                LLVMValueRef buf = emit_expr(g, expr->await_expr.expr->call.args.items[1], locals);
                if (LLVMTypeOf(buf) != di8ptr)
                    buf = LLVMBuildBitCast(g->builder, buf, di8ptr, "recvbuf");
                LLVMValueRef len = emit_expr(g, expr->await_expr.expr->call.args.items[2], locals);
                if (LLVMTypeOf(len) != di32)
                    len = LLVMBuildIntCast2(g->builder, len, di32, 1, "len32");
                g->uses_socket_async = true;

                int k = g->current_async_next_state++;
                LLVMValueRef selfframe = g->current_async_frame;
                LLVMTypeRef self_ft = g->current_async_frame_type;
                LLVMValueRef self_i8 = LLVMBuildBitCast(g->builder, selfframe, di8ptr, "self");
                /* &self.result — the reactor stores the recv byte count here. */
                LLVMValueRef out_n = LLVMBuildStructGEP2(g->builder, self_ft, selfframe,
                    ASYNC_FRAME_RESULT, "self.iores");
                emit_async_save_slots(g);
                LLVMBuildStore(g->builder, LLVMConstInt(di32, (unsigned)k, 0),
                    LLVMBuildStructGEP2(g->builder, self_ft, selfframe, ASYNC_FRAME_STATE, "self.state"));
                zan_call2(g->builder, g->rt_io_recv_co_type, g->rt_io_recv_co,
                    (LLVMValueRef[]){ fd, buf, len, self_i8,
                        g->current_async_resume_fn, out_n }, 6, "");
                LLVMBuildRetVoid(g->builder);

                LLVMBasicBlockRef rk = LLVMAppendBasicBlockInContext(g->ctx,
                    g->current_async_resume_fn, "co.resume");
                LLVMAddCase(g->current_async_switch, LLVMConstInt(di32, (unsigned)k, 0), rk);
                LLVMPositionBuilderAtEnd(g->builder, rk);
                emit_async_reload_slots(g);
                /* recompute the result GEP: entry dominates rk, the pre-suspend
                 * block does not. */
                LLVMValueRef res_slot = LLVMBuildStructGEP2(g->builder, self_ft, selfframe,
                    ASYNC_FRAME_RESULT, "self.iores2");
                return LLVMBuildLoad2(g->builder, di64, res_slot, "recvn");
            }
        }

        /* await Socket.AcceptOv(fd) — Windows AcceptEx completion. The
         * accepted socket is written into the frame result slot before the
         * suspended state machine is re-readied. */
        {
            if (is_call_to(expr->await_expr.expr, "Socket", "AcceptOv") &&
                expr->await_expr.expr->call.args.count == 1) {
                LLVMTypeRef di64 = LLVMInt64TypeInContext(g->ctx);
                LLVMTypeRef di32 = LLVMInt32TypeInContext(g->ctx);
                LLVMTypeRef di8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                if (!(g->current_async_frame && g->current_async_switch)) {
                    zan_diag_emit(g->diag, DIAG_ERROR, expr->loc,
                        "await Socket.AcceptOv is only supported inside an async method");
                    return LLVMConstInt(di64, 0, 0);
                }
                LLVMValueRef fd = emit_expr(g, expr->await_expr.expr->call.args.items[0], locals);
                if (LLVMTypeOf(fd) != di64)
                    fd = LLVMBuildIntCast2(g->builder, fd, di64, 1, "fd64");
                g->uses_socket_async = true;

                int k = g->current_async_next_state++;
                LLVMValueRef selfframe = g->current_async_frame;
                LLVMTypeRef self_ft = g->current_async_frame_type;
                LLVMValueRef self_i8 = LLVMBuildBitCast(g->builder, selfframe, di8ptr, "self");
                LLVMValueRef out_fd = LLVMBuildStructGEP2(g->builder, self_ft, selfframe,
                    ASYNC_FRAME_RESULT, "self.acceptfd");
                emit_async_save_slots(g);
                LLVMBuildStore(g->builder, LLVMConstInt(di32, (unsigned)k, 0),
                    LLVMBuildStructGEP2(g->builder, self_ft, selfframe,
                        ASYNC_FRAME_STATE, "self.state"));
                zan_call2(g->builder, g->rt_io_accept_co_type,
                    g->rt_io_accept_co, (LLVMValueRef[]){ fd, self_i8,
                        g->current_async_resume_fn, out_fd }, 4, "");
                LLVMBuildRetVoid(g->builder);

                LLVMBasicBlockRef rk = LLVMAppendBasicBlockInContext(g->ctx,
                    g->current_async_resume_fn, "co.resume");
                LLVMAddCase(g->current_async_switch,
                    LLVMConstInt(di32, (unsigned)k, 0), rk);
                LLVMPositionBuilderAtEnd(g->builder, rk);
                emit_async_reload_slots(g);
                LLVMValueRef res_slot = LLVMBuildStructGEP2(g->builder, self_ft,
                    selfframe, ASYNC_FRAME_RESULT, "self.acceptfd2");
                return LLVMBuildLoad2(g->builder, di64, res_slot, "acceptfd");
            }
        }

        /* await <call> — the awaited expression is a call to an async method's
         * ramp, yielding an i8* task handle (heap frame). The sub's resume/step
         * fn is `<ramp>$resume`, resolved from the call target's name. Two
         * lowerings (see docs/ASYNC_CPS_DESIGN.md):
         *   - inside an async body: register self as the sub's awaiter, schedule
         *     the sub, SUSPEND (save live slots, state=k, ret void); a resume-k
         *     block reloads slots and reads sub.result.
         *   - at a non-async root (e.g. Main): drive the cooperative scheduler
         *     synchronously (init/ready/run) and then read sub.result. */
        LLVMValueRef sub = emit_expr(g, expr->await_expr.expr, locals);
        LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
        LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
        LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
        LLVMTypeRef hdr = g->co_header_type;

        LLVMValueRef sub_resume = NULL;
        if (LLVMIsACallInst(sub)) {
            LLVMValueRef callee = LLVMGetCalledValue(sub);
            if (callee) {
                size_t nl = 0;
                const char *cn = LLVMGetValueName2(callee, &nl);
                if (cn && nl > 0 && nl < 240) {
                    char rn[256];
                    memcpy(rn, cn, nl);
                    memcpy(rn + nl, "$resume", 8); /* includes NUL */
                    sub_resume = LLVMGetNamedFunction(g->mod, rn);
                }
            }
        }

        if (sub_resume && LLVMGetTypeKind(LLVMTypeOf(sub)) == LLVMPointerTypeKind) {
            LLVMValueRef sub_i8 = LLVMBuildBitCast(g->builder, sub, i8ptr, "sub");

            if (g->current_async_frame && g->current_async_switch) {
                int k = g->current_async_next_state++;
                int j = g->current_async_sub_next++;
                LLVMValueRef selfframe = g->current_async_frame;
                LLVMTypeRef self_ft = g->current_async_frame_type;

                /* stash sub handle in a frame slot (survives the suspension) */
                LLVMBuildStore(g->builder, sub_i8,
                    LLVMBuildStructGEP2(g->builder, self_ft, selfframe,
                        (unsigned)(g->current_async_sub_base + j), "sub.slot"));

                /* sub.awaiter = self; sub.awaiter_step = Self$resume */
                LLVMValueRef self_i8 = LLVMBuildBitCast(g->builder, selfframe, i8ptr, "self");
                LLVMBuildStore(g->builder, self_i8,
                    LLVMBuildStructGEP2(g->builder, hdr, sub_i8, ASYNC_FRAME_AWAITER, "sub.aw"));
                LLVMBuildStore(g->builder, g->current_async_resume_fn,
                    LLVMBuildStructGEP2(g->builder, hdr, sub_i8, ASYNC_FRAME_AWAITER_STEP, "sub.aws"));

                emit_async_save_slots(g);
                LLVMBuildStore(g->builder, LLVMConstInt(i32, (unsigned)k, 0),
                    LLVMBuildStructGEP2(g->builder, self_ft, selfframe, ASYNC_FRAME_STATE, "self.state"));
                LLVMValueRef sched_args[] = { sub_i8, sub_resume };
                zan_call2(g->builder, g->rt_co_ready_type, g->rt_co_ready, sched_args, 2, "");
                LLVMBuildRetVoid(g->builder);

                /* resume-k: re-entered by the driver once the sub completes */
                LLVMBasicBlockRef rk = LLVMAppendBasicBlockInContext(g->ctx,
                    g->current_async_resume_fn, "co.resume");
                LLVMAddCase(g->current_async_switch, LLVMConstInt(i32, (unsigned)k, 0), rk);
                LLVMPositionBuilderAtEnd(g->builder, rk);
                emit_async_reload_slots(g);
                /* recompute the sub-slot GEP here (entry dominates rk; the
                 * pre-suspend block does not). */
                LLVMValueRef sub_slot = LLVMBuildStructGEP2(g->builder, self_ft, selfframe,
                    (unsigned)(g->current_async_sub_base + j), "sub.slot2");
                LLVMValueRef sub_rl = LLVMBuildLoad2(g->builder, i8ptr, sub_slot, "sub.rl");
                LLVMValueRef rptr = LLVMBuildStructGEP2(g->builder, hdr, sub_rl,
                    ASYNC_FRAME_RESULT, "sub.result");
                LLVMValueRef awres = LLVMBuildLoad2(g->builder, i64, rptr, "awres");
                /* The awaited sub-coroutine has completed and we have copied its
                 * result out of its heap frame; free the frame now (the awaiter
                 * owns it once the sub is done). Without this, every awaited
                 * async call leaks its frame -- a per-request leak that grows a
                 * long-running socket server's memory without bound. */
                zan_call2(g->builder, LLVMGlobalGetValueType(g->fn_free),
                    g->fn_free, &sub_rl, 1, "");
                return awres;
            }

            /* root drive (non-async caller). The scheduler is initialized once
             * at program entry (see emit_main_method); re-initializing here
             * would reset the ready queue and discard any coroutines already
             * enqueued by Task.Spawn before this await -- e.g. a spawned server
             * in a concurrent client/server program would never run. */
            LLVMValueRef sched_args[] = { sub_i8, sub_resume };
            zan_call2(g->builder, g->rt_co_ready_type, g->rt_co_ready, sched_args, 2, "");
            zan_call2(g->builder, g->rt_co_sched_run_type, g->rt_co_sched_run, NULL, 0, "");
            LLVMValueRef rptr = LLVMBuildStructGEP2(g->builder, hdr, sub_i8,
                ASYNC_FRAME_RESULT, "sub.result");
            LLVMValueRef awres = LLVMBuildLoad2(g->builder, i64, rptr, "awres");
            /* Root-driven sub has run to completion; copy out its result then
             * free its heap frame (no awaiter will). */
            zan_call2(g->builder, LLVMGlobalGetValueType(g->fn_free),
                g->fn_free, &sub_i8, 1, "");
            return awres;
        }

        /* Fallback: awaiting a legacy Task struct (busy-wait) or a plain value. */
        if (LLVMGetTypeKind(LLVMTypeOf(sub)) == LLVMPointerTypeKind) {
            LLVMValueRef task_ptr = LLVMBuildBitCast(g->builder, sub,
                LLVMPointerType(g->task_struct_type, 0), "taskp");
            LLVMBasicBlockRef poll_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "aw.poll");
            LLVMBasicBlockRef done_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "aw.done");
            LLVMBuildBr(g->builder, poll_bb);
            LLVMPositionBuilderAtEnd(g->builder, poll_bb);
            LLVMValueRef comp_ptr = LLVMBuildStructGEP2(g->builder, g->task_struct_type, task_ptr, 0, "comp");
            LLVMValueRef comp = LLVMBuildLoad2(g->builder, i64, comp_ptr, "cv");
            LLVMValueRef is_done = LLVMBuildICmp(g->builder, LLVMIntNE, comp, LLVMConstInt(i64, 0, 0), "done");
            LLVMBuildCondBr(g->builder, is_done, done_bb, poll_bb);
            LLVMPositionBuilderAtEnd(g->builder, done_bb);
            LLVMValueRef res_ptr = LLVMBuildStructGEP2(g->builder, g->task_struct_type, task_ptr, 1, "resp");
            return LLVMBuildLoad2(g->builder, i64, res_ptr, "awres");
        }
        return sub;
    }

    case AST_LAMBDA:
        return emit_lambda_typed(g, expr, NULL, locals);

    default:
        return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
    }
}

static LLVMValueRef emit_lambda_typed(zan_irgen_t *g, zan_ast_node_t *expr,
                                      zan_type_t *expected, local_scope_t *locals) {
    (void)locals; /* lambdas are non-capturing */
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    int pc = expr->lambda.params.count;
    bool exp_delegate = expected && expected->kind == TYPE_DELEGATE;

    /* Resolve each parameter's zan type: prefer an explicit annotation on the
     * lambda, otherwise borrow it from the target delegate signature. */
    zan_type_t **ptypes = (zan_type_t **)calloc((size_t)(pc > 0 ? pc : 1), sizeof(zan_type_t *));
    LLVMTypeRef *param_types = (LLVMTypeRef *)calloc((size_t)(pc > 0 ? pc : 1), sizeof(LLVMTypeRef));
    for (int k = 0; k < pc; k++) {
        zan_ast_node_t *param = expr->lambda.params.items[k];
        zan_type_t *pt = NULL;
        if (param->param.type) {
            pt = zan_binder_resolve_type(g->binder, param->param.type);
        }
        if (!pt && exp_delegate && k < expected->delegate_param_count) {
            pt = expected->delegate_param_types[k];
        }
        ptypes[k] = pt;
        param_types[k] = pt ? map_type(g, pt) : i64;
    }
    zan_type_t *rett = exp_delegate ? expected->delegate_ret_type : NULL;
    LLVMTypeRef ret_type = rett ? map_type(g, rett) : i64;
    bool ret_void = rett && rett->kind == TYPE_VOID;
    if (ret_void) ret_type = LLVMVoidTypeInContext(g->ctx);
    LLVMTypeRef fn_type = LLVMFunctionType(ret_type, param_types, (unsigned)pc, 0);

    char lname[64];
    static int lambda_id = 0;
    snprintf(lname, sizeof(lname), "lambda_%d", lambda_id++);
    LLVMValueRef lambda_fn = LLVMAddFunction(g->mod, lname, fn_type);

    LLVMBasicBlockRef saved_bb = LLVMGetInsertBlock(g->builder);
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, lambda_fn, "entry");
    LLVMPositionBuilderAtEnd(g->builder, entry);

    /* The body must emit into the lambda's own function: control-flow blocks
     * append to current_fn and `return` coerces to current_fn_ret. Lambdas are
     * non-capturing (no `this`/enclosing locals), so clear the instance/async
     * context while keeping the enclosing type for static member access. */
    LLVMValueRef saved_fn = g->current_fn;
    LLVMTypeRef saved_fn_ret = g->current_fn_ret_type;
    LLVMValueRef saved_this = g->current_this;
    LLVMValueRef saved_async_frame = g->current_async_frame;
    g->current_fn = lambda_fn;
    g->current_fn_ret_type = ret_type;
    g->current_this = NULL;
    g->current_async_frame = NULL;

    local_scope_t lambda_locals;
    local_scope_init(&lambda_locals, g->arena);
    for (int k = 0; k < pc; k++) {
        zan_ast_node_t *param = expr->lambda.params.items[k];
        LLVMTypeRef lt = param_types[k];
        LLVMValueRef alloc = LLVMBuildAlloca(g->builder, lt, "lp");
        LLVMBuildStore(g->builder, LLVMGetParam(lambda_fn, (unsigned)k), alloc);
        local_add(&lambda_locals, param->param.name, alloc,
                  ptypes[k] ? ptypes[k] : g->binder->type_int);
    }

    if (expr->lambda.body && expr->lambda.body->kind == AST_BLOCK) {
        emit_stmt(g, expr->lambda.body, &lambda_locals);
        if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder))) {
            if (ret_void) LLVMBuildRetVoid(g->builder);
            else LLVMBuildRet(g->builder, LLVMConstNull(ret_type));
        }
    } else if (expr->lambda.body) {
        LLVMValueRef result = emit_expr(g, expr->lambda.body, &lambda_locals);
        if (ret_void) {
            LLVMBuildRetVoid(g->builder);
        } else if (rett) {
            result = emit_boundary_coerce(g, result, ret_type);
            LLVMBuildRet(g->builder, result);
        } else {
            result = coerce_to_i64(g, result);
            LLVMBuildRet(g->builder, result);
        }
    } else {
        if (ret_void) LLVMBuildRetVoid(g->builder);
        else LLVMBuildRet(g->builder, LLVMConstNull(ret_type));
    }

    g->current_fn = saved_fn;
    g->current_fn_ret_type = saved_fn_ret;
    g->current_this = saved_this;
    g->current_async_frame = saved_async_frame;
    LLVMPositionBuilderAtEnd(g->builder, saved_bb);
    free(param_types);
    free(ptypes);
    return lambda_fn;
}

static zan_type_t *method_param_type(zan_irgen_t *g, zan_symbol_t *msym, int idx) {
    if (!msym || !msym->decl || msym->decl->kind != AST_METHOD_DECL) return NULL;
    zan_ast_list_t *params = &msym->decl->method_decl.params;
    if (idx < 0 || idx >= params->count) return NULL;
    zan_ast_node_t *p = params->items[idx];
    if (!p || !p->param.type) return NULL;
    return zan_binder_resolve_type(g->binder, p->param.type);
}

static LLVMValueRef emit_arg_typed(zan_irgen_t *g, zan_ast_node_t *arg,
                                   zan_type_t *ptype, local_scope_t *locals) {
    if (arg && arg->kind == AST_LAMBDA && ptype && ptype->kind == TYPE_DELEGATE) {
        return emit_lambda_typed(g, arg, ptype, locals);
    }
    return emit_expr(g, arg, locals);
}

/* `ref x` / `out x` / `out T x` argument: pass the address of the local's
 * storage slot. An inline `out T x` declares a fresh zero-initialised local
 * in the caller's scope first. */
static LLVMValueRef emit_ref_arg(zan_irgen_t *g, zan_ast_node_t *arg,
                                 local_scope_t *locals) {
    zan_ast_node_t *tgt = arg->ref_arg.expr;
    if (arg->ref_arg.decl_type && tgt && tgt->kind == AST_IDENTIFIER) {
        zan_type_t *dt = zan_binder_resolve_type(g->binder, arg->ref_arg.decl_type);
        LLVMTypeRef lt = map_type(g, dt);
        LLVMValueRef a = LLVMBuildAlloca(g->builder, lt, "out");
        LLVMBuildStore(g->builder, LLVMConstNull(lt), a);
        local_add(locals, tgt->ident.name, a, dt);
        return a;
    }
    if (tgt && tgt->kind == AST_IDENTIFIER) {
        local_var_t *l = local_find(locals, tgt->ident.name);
        if (l) return l->alloca;
    }
    return emit_expr(g, tgt, locals);
}

/* ---- async/await CPS lowering helpers ---- */

/* Coerce an arbitrary scalar value to the i64 used by the frame result slot. */
static LLVMValueRef coerce_to_i64(zan_irgen_t *g, LLVMValueRef v) {
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef t = LLVMTypeOf(v);
    switch (LLVMGetTypeKind(t)) {
    case LLVMIntegerTypeKind: {
        unsigned bits = LLVMGetIntTypeWidth(t);
        if (bits < 64) return LLVMBuildSExt(g->builder, v, i64, "res.i64");
        if (bits > 64) return LLVMBuildTrunc(g->builder, v, i64, "res.i64");
        return v;
    }
    case LLVMPointerTypeKind:
        return LLVMBuildPtrToInt(g->builder, v, i64, "res.i64");
    case LLVMDoubleTypeKind:
        return LLVMBuildBitCast(g->builder, v, i64, "res.i64");
    case LLVMFloatTypeKind: {
        LLVMValueRef d = LLVMBuildFPExt(g->builder, v,
            LLVMDoubleTypeInContext(g->ctx), "res.f64");
        return LLVMBuildBitCast(g->builder, d, i64, "res.i64");
    }
    default:
        return LLVMConstInt(i64, 0, 0);
    }
}

/* Emit the completion epilogue of an async $resume body: store the result,
 * mark the frame done, wake a waiting awaiter (if any), then `ret void`.
 * `result_i64` may be NULL for a void async fn. Assumes the builder is
 * positioned at a block without a terminator.
 *
 * The awaiter-wake handshake schedules the frame that awaited us: when a
 * caller `await`s this task it stores itself + its own $resume into our
 * awaiter/awaiter_step header slots (see the await protocol). On completion we
 * re-enqueue that awaiter via zan_co_ready so the cooperative driver re-steps
 * it and it can read our result. A root (non-async) driver leaves awaiter null
 * and instead polls the result after zan_co_sched_run drains. */
static void emit_async_complete(zan_irgen_t *g, local_scope_t *locals, LLVMValueRef result_i64) {
    LLVMValueRef frame = g->current_async_frame;
    LLVMTypeRef ft = g->current_async_frame_type;
    LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);

    LLVMValueRef res_ptr = LLVMBuildStructGEP2(g->builder, ft, frame,
        ASYNC_FRAME_RESULT, "fr.result");
    LLVMBuildStore(g->builder, result_i64 ? result_i64 : LLVMConstInt(i64, 0, 0),
        res_ptr);

    LLVMValueRef done_ptr = LLVMBuildStructGEP2(g->builder, ft, frame,
        ASYNC_FRAME_DONE, "fr.done");
    LLVMBuildStore(g->builder, LLVMConstInt(i32, 1, 0), done_ptr);
    LLVMBuildStore(g->builder, LLVMConstInt(i32, -1, 1),
        LLVMBuildStructGEP2(g->builder, ft, frame, ASYNC_FRAME_STATE, "fr.state"));

    emit_release_owned_locals(g, locals);

    /* if (awaiter != null) zan_co_ready(awaiter, awaiter_step); */
    LLVMValueRef aw_ptr = LLVMBuildStructGEP2(g->builder, ft, frame,
        ASYNC_FRAME_AWAITER, "fr.awaiter");
    LLVMValueRef awaiter = LLVMBuildLoad2(g->builder, i8ptr, aw_ptr, "awaiter");
    LLVMValueRef has_awaiter = LLVMBuildICmp(g->builder, LLVMIntNE, awaiter,
        LLVMConstNull(i8ptr), "has.awaiter");
    LLVMValueRef fn = g->current_fn;
    LLVMBasicBlockRef wake_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "co.wake");
    LLVMBasicBlockRef ret_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "co.ret");
    LLVMBuildCondBr(g->builder, has_awaiter, wake_bb, ret_bb);

    LLVMPositionBuilderAtEnd(g->builder, wake_bb);
    LLVMValueRef aws_ptr = LLVMBuildStructGEP2(g->builder, ft, frame,
        ASYNC_FRAME_AWAITER_STEP, "fr.awaiter.step");
    LLVMValueRef aw_step = LLVMBuildLoad2(g->builder, g->co_step_ptr, aws_ptr, "awaiter.step");
    LLVMValueRef wake_args[] = { awaiter, aw_step };
    zan_call2(g->builder, g->rt_co_ready_type, g->rt_co_ready, wake_args, 2, "");
    LLVMBuildBr(g->builder, ret_bb);

    LLVMPositionBuilderAtEnd(g->builder, ret_bb);
    LLVMBuildRetVoid(g->builder);
}

/* Save all frame-resident slots (params + named scalar locals) from their
 * stack allocas back into the heap frame. Emitted right before a suspend so
 * live values survive across the `ret void`. */
static void emit_async_save_slots(zan_irgen_t *g) {
    LLVMTypeRef ft = g->current_async_frame_type;
    LLVMValueRef frame = g->current_async_frame;
    for (int i = 0; i < g->current_async_slot_count; i++) {
        LLVMValueRef v = LLVMBuildLoad2(g->builder, g->current_async_slots[i].llvm,
            g->current_async_slots[i].slot_alloca, "sv");
        LLVMValueRef slot = LLVMBuildStructGEP2(g->builder, ft, frame,
            (unsigned)g->current_async_slots[i].frame_index, "sv.slot");
        LLVMBuildStore(g->builder, v, slot);
    }
}

/* Reload all frame-resident slots from the heap frame into their stack allocas.
 * Emitted at the top of each state block (co.start and every resume-k) so the
 * body reads live values that survived a suspension. */
static void emit_async_reload_slots(zan_irgen_t *g) {
    LLVMTypeRef ft = g->current_async_frame_type;
    LLVMValueRef frame = g->current_async_frame;
    for (int i = 0; i < g->current_async_slot_count; i++) {
        LLVMValueRef slot = LLVMBuildStructGEP2(g->builder, ft, frame,
            (unsigned)g->current_async_slots[i].frame_index, "rl.slot");
        LLVMValueRef v = LLVMBuildLoad2(g->builder, g->current_async_slots[i].llvm,
            slot, "rl");
        LLVMBuildStore(g->builder, v, g->current_async_slots[i].slot_alloca);
    }
}

/* ---- await A-normal-form (ANF) normalization ----
 *
 * S3 keeps a value alive across a suspension only when it is a named scalar
 * local (those live in the heap frame and are reloaded at every state block).
 * An intermediate SSA temp produced *before* an await and consumed *after* it
 * does not survive: the resume-k block is entered from the entry switch, so a
 * value computed in the pre-suspend block does not dominate it and LLVM rejects
 * the module ("instruction does not dominate all uses"). This shows up for
 * compound / multiple awaits, e.g. `c + await f()`, `await a() + await b()`,
 * or `h(await a(), await b())`.
 *
 * This pass rewrites each async body into A-normal form for awaits: every
 * `await E` in a linearly-evaluated position becomes its own preceding
 * statement `int $awN = await E;` and the original occurrence is replaced by a
 * reference to `$awN`. Because `$awN` is a named scalar local it is made
 * frame-resident by async_scan and reloaded after the suspension, so no value
 * crosses a suspend point in a register. Awaits inside short-circuit (`&&`,
 * `||`) and conditional (`?:`) operands, and inside loop conditions/steps, are
 * left in place (their existing control-flow lowering handles them and hoisting
 * would change evaluation semantics). */

typedef struct {
    zan_irgen_t    *g;
    zan_ast_list_t *out;  /* hoisted statements, appended in evaluation order */
    int            *counter;
} anf_ctx_t;

static bool anf_expr_contains_await(zan_ast_node_t *e) {
    if (!e) return false;
    switch (e->kind) {
    case AST_AWAIT_EXPR: return true;
    case AST_BINARY:
    case AST_ASSIGNMENT:
        return anf_expr_contains_await(e->binary.left) ||
               anf_expr_contains_await(e->binary.right);
    case AST_UNARY:
    case AST_POSTFIX_UNARY:
        return anf_expr_contains_await(e->unary.operand);
    case AST_CALL: {
        if (anf_expr_contains_await(e->call.callee)) return true;
        for (int i = 0; i < e->call.args.count; i++)
            if (anf_expr_contains_await(e->call.args.items[i])) return true;
        return false;
    }
    case AST_MEMBER_ACCESS: return anf_expr_contains_await(e->member.object);
    case AST_INDEX:
        return anf_expr_contains_await(e->index.object) ||
               anf_expr_contains_await(e->index.index);
    case AST_CONDITIONAL:
        return anf_expr_contains_await(e->conditional.cond) ||
               anf_expr_contains_await(e->conditional.then_expr) ||
               anf_expr_contains_await(e->conditional.else_expr);
    case AST_NEW_EXPR: {
        for (int i = 0; i < e->new_expr.args.count; i++)
            if (anf_expr_contains_await(e->new_expr.args.items[i])) return true;
        return false;
    }
    case AST_CAST_EXPR: return anf_expr_contains_await(e->cast.expr);
    case AST_IS_EXPR:
    case AST_AS_EXPR:  return anf_expr_contains_await(e->type_test.expr);
    default: return false;
    }
}

/* A side-effecting operand (a call, assignment, or ++/--) evaluated *before* an
 * await in the same operand list would be reordered to run *after* the await if
 * we only hoist the await (it stays in the residual). Detect that to reject it
 * with a clear message instead of silently changing evaluation order. Awaits
 * themselves are hoisted in order, so they are not counted here. */
static bool anf_expr_has_side_effect(zan_ast_node_t *e) {
    if (!e) return false;
    switch (e->kind) {
    case AST_AWAIT_EXPR: return false; /* hoisted separately, order preserved */
    case AST_CALL: return true;
    case AST_ASSIGNMENT: return true;
    case AST_POSTFIX_UNARY: return true;
    case AST_UNARY:
        if (e->unary.op == TK_PLUS_PLUS || e->unary.op == TK_MINUS_MINUS) return true;
        return anf_expr_has_side_effect(e->unary.operand);
    case AST_BINARY:
        return anf_expr_has_side_effect(e->binary.left) ||
               anf_expr_has_side_effect(e->binary.right);
    case AST_MEMBER_ACCESS: return anf_expr_has_side_effect(e->member.object);
    case AST_INDEX:
        return anf_expr_has_side_effect(e->index.object) ||
               anf_expr_has_side_effect(e->index.index);
    case AST_CAST_EXPR: return anf_expr_has_side_effect(e->cast.expr);
    case AST_IS_EXPR:
    case AST_AS_EXPR:  return anf_expr_has_side_effect(e->type_test.expr);
    default: return false;
    }
}

static zan_ast_node_t *anf_expr(anf_ctx_t *c, zan_ast_node_t *e);

/* If operand `before` (evaluated first) has side effects and a later operand
 * `after` contains an await, hoisting only the await reorders them. Flag it. */
static void anf_check_order(anf_ctx_t *c, zan_ast_node_t *before, zan_ast_node_t *after) {
    if (after && before && anf_expr_contains_await(after) &&
        anf_expr_has_side_effect(before)) {
        zan_diag_emit(c->g->diag, DIAG_ERROR, before->loc,
            "async: an expression with side effects is evaluated before an "
            "await in the same expression; assign it to a local first");
    }
}

/* Hoist `await E` into `int $awN = await E;` and return a reference to $awN. */
static zan_ast_node_t *anf_hoist_await(anf_ctx_t *c, zan_ast_node_t *aw) {
    /* normalize any nested awaits inside the awaited expression first */
    aw->await_expr.expr = anf_expr(c, aw->await_expr.expr);

    char buf[32];
    int len = snprintf(buf, sizeof buf, "$aw%d", (*c->counter)++);
    char *nm = (char *)zan_arena_alloc(c->g->arena, (size_t)len + 1);
    memcpy(nm, buf, (size_t)len + 1);
    zan_istr_t name = { nm, (uint32_t)len };

    zan_ast_node_t *tref = zan_ast_new(c->g->arena, AST_TYPE_REF, aw->loc);
    static const char intname[] = "int";
    tref->type_ref.name = (zan_istr_t){ intname, 3 };

    zan_ast_node_t *vd = zan_ast_new(c->g->arena, AST_VAR_DECL, aw->loc);
    vd->var_decl.name = name;
    vd->var_decl.type = tref;
    vd->var_decl.initializer = aw;
    zan_ast_list_push(c->out, vd, c->g->arena);

    zan_ast_node_t *id = zan_ast_new(c->g->arena, AST_IDENTIFIER, aw->loc);
    id->ident.name = name;
    return id;
}

/* Recursively lift awaits out of a linearly-evaluated expression, appending
 * hoisted `$awN` declarations to c->out in evaluation order and returning the
 * residual expression (which no longer contains any hoistable await). */
static zan_ast_node_t *anf_expr(anf_ctx_t *c, zan_ast_node_t *e) {
    if (!e) return e;
    switch (e->kind) {
    case AST_AWAIT_EXPR:
        return anf_hoist_await(c, e);
    case AST_BINARY:
    case AST_ASSIGNMENT:
        if (e->binary.op == TK_AMP_AMP || e->binary.op == TK_PIPE_PIPE) {
            /* short-circuit: only the left operand is unconditional. */
            e->binary.left = anf_expr(c, e->binary.left);
            return e;
        }
        anf_check_order(c, e->binary.left, e->binary.right);
        e->binary.left  = anf_expr(c, e->binary.left);
        e->binary.right = anf_expr(c, e->binary.right);
        return e;
    case AST_UNARY:
    case AST_POSTFIX_UNARY:
        e->unary.operand = anf_expr(c, e->unary.operand);
        return e;
    case AST_CALL:
        for (int i = 0; i < e->call.args.count; i++) {
            for (int j = i + 1; j < e->call.args.count; j++)
                anf_check_order(c, e->call.args.items[i], e->call.args.items[j]);
        }
        e->call.callee = anf_expr(c, e->call.callee);
        for (int i = 0; i < e->call.args.count; i++)
            e->call.args.items[i] = anf_expr(c, e->call.args.items[i]);
        return e;
    case AST_MEMBER_ACCESS:
        e->member.object = anf_expr(c, e->member.object);
        return e;
    case AST_INDEX:
        anf_check_order(c, e->index.object, e->index.index);
        e->index.object = anf_expr(c, e->index.object);
        e->index.index  = anf_expr(c, e->index.index);
        return e;
    case AST_CONDITIONAL:
        /* only the guard is unconditional; leave branch awaits in place. */
        e->conditional.cond = anf_expr(c, e->conditional.cond);
        return e;
    case AST_CAST_EXPR:
        e->cast.expr = anf_expr(c, e->cast.expr);
        return e;
    case AST_NEW_EXPR:
        for (int i = 0; i < e->new_expr.args.count; i++) {
            for (int j = i + 1; j < e->new_expr.args.count; j++)
                anf_check_order(c, e->new_expr.args.items[i], e->new_expr.args.items[j]);
        }
        for (int i = 0; i < e->new_expr.args.count; i++)
            e->new_expr.args.items[i] = anf_expr(c, e->new_expr.args.items[i]);
        return e;
    case AST_IS_EXPR:
    case AST_AS_EXPR:
        e->type_test.expr = anf_expr(c, e->type_test.expr);
        return e;
    default:
        return e;
    }
}

static void anf_normalize_block(zan_irgen_t *g, zan_ast_node_t *block, int *counter);

/* Does a statement subtree contain any await? Used to decide whether a
 * single-statement body must be wrapped in a block for hoisting. */
static bool anf_stmt_contains_await(zan_ast_node_t *st) {
    if (!st) return false;
    switch (st->kind) {
    case AST_VAR_DECL:    return anf_expr_contains_await(st->var_decl.initializer);
    case AST_EXPR_STMT:   return anf_expr_contains_await(st->expr_stmt.expr);
    case AST_RETURN_STMT: return anf_expr_contains_await(st->ret.value);
    case AST_THROW_STMT:  return anf_expr_contains_await(st->throw_stmt.value);
    case AST_IF_STMT:
        return anf_expr_contains_await(st->if_stmt.cond) ||
               anf_stmt_contains_await(st->if_stmt.then_body) ||
               anf_stmt_contains_await(st->if_stmt.else_body);
    case AST_WHILE_STMT:
    case AST_DO_WHILE_STMT:
        return anf_expr_contains_await(st->while_stmt.cond) ||
               anf_stmt_contains_await(st->while_stmt.body);
    case AST_FOR_STMT:
        return anf_stmt_contains_await(st->for_stmt.init) ||
               anf_expr_contains_await(st->for_stmt.cond) ||
               anf_expr_contains_await(st->for_stmt.step) ||
               anf_stmt_contains_await(st->for_stmt.body);
    case AST_FOREACH_STMT:
        return anf_expr_contains_await(st->foreach_stmt.collection) ||
               anf_stmt_contains_await(st->foreach_stmt.body);
    case AST_BLOCK: {
        for (int i = 0; i < st->block.stmts.count; i++)
            if (anf_stmt_contains_await(st->block.stmts.items[i])) return true;
        return false;
    }
    default: return false;
    }
}

/* Ensure `*slot` is an AST_BLOCK so hoisted statements can be inserted before
 * the awaits it contains, then normalize it. Used for single-statement bodies
 * (e.g. `if (c) return await f();`). */
static void anf_normalize_body(zan_irgen_t *g, zan_ast_node_t **slot, int *counter) {
    zan_ast_node_t *body = *slot;
    if (!body) return;
    if (body->kind != AST_BLOCK) {
        if (!anf_stmt_contains_await(body)) return;
        zan_ast_node_t *blk = zan_ast_new(g->arena, AST_BLOCK, body->loc);
        zan_ast_list_init(&blk->block.stmts);
        zan_ast_list_push(&blk->block.stmts, body, g->arena);
        *slot = blk;
        body = blk;
    }
    anf_normalize_block(g, body, counter);
}

/* Normalize one statement, appending any hoisted declarations to `dst` (in
 * evaluation order) *before* the statement is pushed by the caller. Nested
 * statement bodies are normalized recursively. */
static void anf_normalize_stmt(zan_irgen_t *g, zan_ast_node_t *st,
                               zan_ast_list_t *dst, int *counter) {
    if (!st) return;
    anf_ctx_t c = { g, dst, counter };
    switch (st->kind) {
    case AST_VAR_DECL:
        st->var_decl.initializer = anf_expr(&c, st->var_decl.initializer);
        break;
    case AST_EXPR_STMT:
        st->expr_stmt.expr = anf_expr(&c, st->expr_stmt.expr);
        break;
    case AST_RETURN_STMT:
        st->ret.value = anf_expr(&c, st->ret.value);
        break;
    case AST_THROW_STMT:
        st->throw_stmt.value = anf_expr(&c, st->throw_stmt.value);
        break;
    case AST_IF_STMT:
        /* condition is evaluated once → safe to hoist */
        st->if_stmt.cond = anf_expr(&c, st->if_stmt.cond);
        anf_normalize_body(g, &st->if_stmt.then_body, counter);
        anf_normalize_body(g, &st->if_stmt.else_body, counter);
        break;
    case AST_WHILE_STMT:
    case AST_DO_WHILE_STMT:
        /* condition is re-evaluated each iteration → must NOT hoist it out */
        anf_normalize_body(g, &st->while_stmt.body, counter);
        break;
    case AST_FOR_STMT:
        anf_normalize_body(g, &st->for_stmt.body, counter);
        break;
    case AST_FOREACH_STMT:
        anf_normalize_body(g, &st->foreach_stmt.body, counter);
        break;
    case AST_BLOCK:
        anf_normalize_block(g, st, counter);
        break;
    case AST_TRY_STMT:
        anf_normalize_body(g, &st->try_stmt.try_body, counter);
        for (int i = 0; i < st->try_stmt.catches.count; i++)
            anf_normalize_body(g, &st->try_stmt.catches.items[i]->catch_clause.body, counter);
        anf_normalize_body(g, &st->try_stmt.finally_body, counter);
        break;
    case AST_SWITCH_STMT:
        for (int i = 0; i < st->switch_stmt.cases.count; i++)
            anf_normalize_body(g, &st->switch_stmt.cases.items[i]->switch_case.body, counter);
        break;
    default:
        break;
    }
}

/* Rebuild a block's statement list, inserting hoisted `$awN` declarations
 * before each statement that needed them. */
static void anf_normalize_block(zan_irgen_t *g, zan_ast_node_t *block, int *counter) {
    if (!block || block->kind != AST_BLOCK) return;
    zan_ast_list_t nl;
    zan_ast_list_init(&nl);
    for (int i = 0; i < block->block.stmts.count; i++) {
        zan_ast_node_t *st = block->block.stmts.items[i];
        anf_normalize_stmt(g, st, &nl, counter);
        zan_ast_list_push(&nl, st, g->arena);
    }
    block->block.stmts = nl;
}

/* A named scalar local of an async method that must live in the heap frame so
 * its value survives across suspensions. `frame_index` is assigned when the
 * frame struct is laid out (params first, then these locals). */
typedef struct {
    zan_istr_t   name;
    LLVMTypeRef  llvm;        /* slot element type */
    zan_type_t  *ztype;      /* zan type (for identifier load typing) */
    int          frame_index;
} async_local_t;

typedef struct {
    zan_irgen_t   *g;
    int            await_count;
    async_local_t *locals;
    int            local_count;
    int            local_cap;
} async_scan_t;

static bool async_type_is_scalar(LLVMTypeRef t) {
    switch (LLVMGetTypeKind(t)) {
    case LLVMIntegerTypeKind:
    case LLVMFloatTypeKind:
    case LLVMDoubleTypeKind:
        return true;
    default:
        return false;
    }
}

/* A local must be frame-resident (saved to / reloaded from the heap frame
 * around each suspension) if it can be live across an await. Scalars and any
 * pointer-shaped value (string, array, List/Dict, class instances — all lowered
 * to a pointer here) qualify; the save/reload just relocates the bits and never
 * touches a refcount, so ARC ownership (release on throw-cleanup / scope exit,
 * driven off the reloaded alloca) is unaffected. Aggregates passed by value are
 * left alone (they do not cross suspends in the current lowering). */
static bool async_type_is_frame_resident(LLVMTypeRef t) {
    return async_type_is_scalar(t) || LLVMGetTypeKind(t) == LLVMPointerTypeKind;
}

static void async_scan_expr(async_scan_t *s, zan_ast_node_t *e);
static void async_scan_stmt(async_scan_t *s, zan_ast_node_t *st);

static void async_scan_add_local(async_scan_t *s, zan_istr_t name, LLVMTypeRef llvm, zan_type_t *zt) {
    for (int i = 0; i < s->local_count; i++) {
        if (s->locals[i].name.len == name.len &&
            memcmp(s->locals[i].name.str, name.str, name.len) == 0) {
            return; /* dedup by name (flat scope) */
        }
    }
    if (s->local_count >= s->local_cap) {
        int nc = s->local_cap ? s->local_cap * 2 : 8;
        async_local_t *g2 = (async_local_t *)zan_arena_alloc(s->g->arena,
            sizeof(async_local_t) * (size_t)nc);
        if (s->local_count > 0) memcpy(g2, s->locals, sizeof(async_local_t) * (size_t)s->local_count);
        s->locals = g2;
        s->local_cap = nc;
    }
    s->locals[s->local_count].name = name;
    s->locals[s->local_count].llvm = llvm;
    s->locals[s->local_count].ztype = zt;
    s->locals[s->local_count].frame_index = -1;
    s->local_count++;
}

/* Walk expressions to count await points (state-machine transitions). */
static void async_scan_expr(async_scan_t *s, zan_ast_node_t *e) {
    if (!e) return;
    switch (e->kind) {
    case AST_AWAIT_EXPR:
        s->await_count++;
        async_scan_expr(s, e->await_expr.expr);
        break;
    case AST_BINARY:
    case AST_ASSIGNMENT:
        async_scan_expr(s, e->binary.left);
        async_scan_expr(s, e->binary.right);
        break;
    case AST_UNARY:
    case AST_POSTFIX_UNARY:
        async_scan_expr(s, e->unary.operand);
        break;
    case AST_CALL:
        async_scan_expr(s, e->call.callee);
        for (int i = 0; i < e->call.args.count; i++)
            async_scan_expr(s, e->call.args.items[i]);
        break;
    case AST_MEMBER_ACCESS:
        async_scan_expr(s, e->member.object);
        break;
    case AST_INDEX:
        async_scan_expr(s, e->index.object);
        async_scan_expr(s, e->index.index);
        break;
    case AST_CONDITIONAL:
        async_scan_expr(s, e->conditional.cond);
        async_scan_expr(s, e->conditional.then_expr);
        async_scan_expr(s, e->conditional.else_expr);
        break;
    case AST_NEW_EXPR:
        for (int i = 0; i < e->new_expr.args.count; i++)
            async_scan_expr(s, e->new_expr.args.items[i]);
        break;
    case AST_CAST_EXPR:
        async_scan_expr(s, e->cast.expr);
        break;
    case AST_IS_EXPR:
    case AST_AS_EXPR:
        async_scan_expr(s, e->type_test.expr);
        break;
    default:
        break;
    }
}

/* Walk statements to collect named scalar locals (which must live in the frame)
 * and count await points anywhere in the body. */
static void async_scan_stmt(async_scan_t *s, zan_ast_node_t *st) {
    if (!st) return;
    switch (st->kind) {
    case AST_BLOCK:
        for (int i = 0; i < st->block.stmts.count; i++)
            async_scan_stmt(s, st->block.stmts.items[i]);
        break;
    case AST_VAR_DECL:
        if (st->var_decl.type) {
            zan_type_t *t = zan_binder_resolve_type(s->g->binder, st->var_decl.type);
            if (t) {
                LLVMTypeRef lt = map_type(s->g, t);
                if (async_type_is_frame_resident(lt))
                    async_scan_add_local(s, st->var_decl.name, lt, t);
            }
        }
        async_scan_expr(s, st->var_decl.initializer);
        break;
    case AST_EXPR_STMT:
        async_scan_expr(s, st->expr_stmt.expr);
        break;
    case AST_RETURN_STMT:
        async_scan_expr(s, st->ret.value);
        break;
    case AST_IF_STMT:
        async_scan_expr(s, st->if_stmt.cond);
        async_scan_stmt(s, st->if_stmt.then_body);
        async_scan_stmt(s, st->if_stmt.else_body);
        break;
    case AST_WHILE_STMT:
    case AST_DO_WHILE_STMT:
        async_scan_expr(s, st->while_stmt.cond);
        async_scan_stmt(s, st->while_stmt.body);
        break;
    case AST_FOR_STMT:
        async_scan_stmt(s, st->for_stmt.init);
        async_scan_expr(s, st->for_stmt.cond);
        async_scan_expr(s, st->for_stmt.step);
        async_scan_stmt(s, st->for_stmt.body);
        break;
    case AST_FOREACH_STMT:
        async_scan_expr(s, st->foreach_stmt.collection);
        async_scan_stmt(s, st->foreach_stmt.body);
        break;
    case AST_THROW_STMT:
        async_scan_expr(s, st->throw_stmt.value);
        break;
    case AST_TRY_STMT:
        async_scan_stmt(s, st->try_stmt.try_body);
        for (int i = 0; i < st->try_stmt.catches.count; i++)
            async_scan_stmt(s, st->try_stmt.catches.items[i]->catch_clause.body);
        async_scan_stmt(s, st->try_stmt.finally_body);
        break;
    case AST_SWITCH_STMT:
        async_scan_expr(s, st->switch_stmt.expr);
        for (int i = 0; i < st->switch_stmt.cases.count; i++)
            async_scan_stmt(s, st->switch_stmt.cases.items[i]->switch_case.body);
        break;
    default:
        break;
    }
}

/* ---- rc-element array escape analysis ----
 * A `new T[n]` array of rc elements is released element-by-element at scope
 * exit. That is only sound when the buffer does not outlive the scope, i.e. it
 * never escapes: used only as an index base `a[i]` or a borrowed member read
 * `a.Length`. Any other use of the bare identifier (return, assignment RHS,
 * call argument, collection/field store, ...) shares the pointer, so we skip
 * the release for it (leak rather than risk a double-free). */
static int arr_ident_named(zan_ast_node_t *e, zan_istr_t nm) {
    return e && e->kind == AST_IDENTIFIER &&
           e->ident.name.len == nm.len &&
           (nm.len == 0 || memcmp(e->ident.name.str, nm.str, (size_t)nm.len) == 0);
}
static void arr_escape_stmt(zan_istr_t nm, zan_ast_node_t *st, int *esc);
static void arr_escape_expr(zan_istr_t nm, zan_ast_node_t *e, int *esc) {
    if (!e || *esc) return;
    switch (e->kind) {
    case AST_IDENTIFIER:
        if (arr_ident_named(e, nm)) *esc = 1;
        break;
    case AST_INDEX:
        if (!arr_ident_named(e->index.object, nm))
            arr_escape_expr(nm, e->index.object, esc);
        arr_escape_expr(nm, e->index.index, esc);
        break;
    case AST_MEMBER_ACCESS:
        if (!arr_ident_named(e->member.object, nm))
            arr_escape_expr(nm, e->member.object, esc);
        break;
    case AST_BINARY:
    case AST_ASSIGNMENT:
        arr_escape_expr(nm, e->binary.left, esc);
        arr_escape_expr(nm, e->binary.right, esc);
        break;
    case AST_UNARY:
    case AST_POSTFIX_UNARY:
        arr_escape_expr(nm, e->unary.operand, esc);
        break;
    case AST_CALL:
        arr_escape_expr(nm, e->call.callee, esc);
        for (int i = 0; i < e->call.args.count; i++)
            arr_escape_expr(nm, e->call.args.items[i], esc);
        break;
    case AST_CONDITIONAL:
        arr_escape_expr(nm, e->conditional.cond, esc);
        arr_escape_expr(nm, e->conditional.then_expr, esc);
        arr_escape_expr(nm, e->conditional.else_expr, esc);
        break;
    case AST_NEW_EXPR:
        for (int i = 0; i < e->new_expr.args.count; i++)
            arr_escape_expr(nm, e->new_expr.args.items[i], esc);
        break;
    case AST_CAST_EXPR:
        arr_escape_expr(nm, e->cast.expr, esc);
        break;
    case AST_IS_EXPR:
    case AST_AS_EXPR:
        arr_escape_expr(nm, e->type_test.expr, esc);
        break;
    case AST_AWAIT_EXPR:
        arr_escape_expr(nm, e->await_expr.expr, esc);
        break;
    default:
        break;
    }
}
static void arr_escape_stmt(zan_istr_t nm, zan_ast_node_t *st, int *esc) {
    if (!st || *esc) return;
    switch (st->kind) {
    case AST_BLOCK:
        for (int i = 0; i < st->block.stmts.count; i++)
            arr_escape_stmt(nm, st->block.stmts.items[i], esc);
        break;
    case AST_VAR_DECL:
        arr_escape_expr(nm, st->var_decl.initializer, esc);
        break;
    case AST_EXPR_STMT:
        arr_escape_expr(nm, st->expr_stmt.expr, esc);
        break;
    case AST_RETURN_STMT:
        arr_escape_expr(nm, st->ret.value, esc);
        break;
    case AST_IF_STMT:
        arr_escape_expr(nm, st->if_stmt.cond, esc);
        arr_escape_stmt(nm, st->if_stmt.then_body, esc);
        arr_escape_stmt(nm, st->if_stmt.else_body, esc);
        break;
    case AST_WHILE_STMT:
    case AST_DO_WHILE_STMT:
        arr_escape_expr(nm, st->while_stmt.cond, esc);
        arr_escape_stmt(nm, st->while_stmt.body, esc);
        break;
    case AST_FOR_STMT:
        arr_escape_stmt(nm, st->for_stmt.init, esc);
        arr_escape_expr(nm, st->for_stmt.cond, esc);
        arr_escape_expr(nm, st->for_stmt.step, esc);
        arr_escape_stmt(nm, st->for_stmt.body, esc);
        break;
    case AST_FOREACH_STMT:
        arr_escape_expr(nm, st->foreach_stmt.collection, esc);
        arr_escape_stmt(nm, st->foreach_stmt.body, esc);
        break;
    case AST_THROW_STMT:
        arr_escape_expr(nm, st->throw_stmt.value, esc);
        break;
    case AST_TRY_STMT:
        arr_escape_stmt(nm, st->try_stmt.try_body, esc);
        for (int i = 0; i < st->try_stmt.catches.count; i++)
            arr_escape_stmt(nm, st->try_stmt.catches.items[i]->catch_clause.body, esc);
        arr_escape_stmt(nm, st->try_stmt.finally_body, esc);
        break;
    case AST_SWITCH_STMT:
        arr_escape_expr(nm, st->switch_stmt.expr, esc);
        for (int i = 0; i < st->switch_stmt.cases.count; i++)
            arr_escape_stmt(nm, st->switch_stmt.cases.items[i]->switch_case.body, esc);
        break;
    default:
        break;
    }
}
static int rc_array_local_escapes(zan_irgen_t *g, zan_istr_t nm) {
    int esc = 0;
    if (g->current_fn_body) arr_escape_stmt(nm, g->current_fn_body, &esc);
    else esc = 1; /* unknown body: conservative */
    return esc;
}

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
                        int string_from_raw_source = type && type->kind == TYPE_STRING &&
                            stmt->var_decl.initializer &&
                            stmt->var_decl.initializer->kind == AST_CALL &&
                            stmt->var_decl.initializer->call.callee &&
                            stmt->var_decl.initializer->call.callee->kind == AST_IDENTIFIER;
                        /* Own every rc-managed local, including those declared
                         * inside loop/if/block bodies (arc_stmt_depth != 0):
                         * per-block release at scope exit (emit_release_owned_
                         * locals_from) frees them each iteration and truncates
                         * them out of scope, so there is no double-release at
                         * function exit. Gating on top-level only leaked one
                         * object per iteration for class-typed loop locals. */
                        int arc_own = (type && is_rc_managed_type(type) &&
                                       LLVMGetTypeKind(g->current_async_slots[i].llvm) == LLVMPointerTypeKind &&
                                       !string_from_raw_source);
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
                LLVMValueRef alloca = LLVMBuildAlloca(g->builder, ptr_type, "arr");
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
                    LLVMValueRef alloca = LLVMBuildAlloca(g->builder, ptr_type, "list");
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
                    LLVMValueRef alloca = LLVMBuildAlloca(g->builder, ptr_type, "sb");
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
                    LLVMValueRef alloca = LLVMBuildAlloca(g->builder, ptr_type, "dict");
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
                            LLVMValueRef alloca = LLVMBuildAlloca(g->builder, st, "var");
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
            LLVMValueRef alloca = LLVMBuildAlloca(g->builder, init_type, "var");
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
            : LLVMBuildAlloca(g->builder, llvm_type, "var");

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
         * expressions are not owned (expr_yields_owned_rc_value == 0). */
        if (ev && LLVMGetTypeKind(LLVMTypeOf(ev)) == LLVMPointerTypeKind &&
            expr_yields_owned_rc_value(g, e, locals)) {
            zan_type_t *et = infer_expr_type(g, e, locals);
            if (et && is_rc_managed_type(et)) {
                emit_rc_release_for_type(g, et, ev);
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
        int catch_start = locals->count;
        if (stmt->try_stmt.catches.count > 0) {
            zan_ast_node_t *cc = stmt->try_stmt.catches.items[0];
            if (cc->catch_clause.var_name.len > 0) {
                LLVMValueRef ev = LLVMBuildLoad2(g->builder, i8ptr, exc_g, "exc");
                LLVMValueRef ea = LLVMBuildAlloca(g->builder, i8ptr, "exc.var");
                LLVMBuildStore(g->builder, ev, ea);
                zan_type_t *et = cc->catch_clause.type
                    ? zan_binder_resolve_type(g->binder, cc->catch_clause.type)
                    : NULL;
                local_add(locals, cc->catch_clause.var_name, ea, et);
            }
            emit_stmt(g, cc->catch_clause.body, locals);
        }
        locals->count = catch_start;
        if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder)))
            LLVMBuildBr(g->builder, end_bb);

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
        LLVMValueRef idx_alloc = LLVMBuildAlloca(g->builder, i64, "fi");
        LLVMBuildStore(g->builder, LLVMConstInt(i64, 0, 0), idx_alloc);

        /* iteration variable */
        LLVMValueRef iter_alloc = LLVMBuildAlloca(g->builder, elem_llvm, "fv");
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

/* ---- top-level emission ---- */

static void emit_main_method(zan_irgen_t *g, zan_ast_node_t *method, zan_symbol_t *type_sym,
                             zan_ast_node_t *unit) {
    /* create main(i32 argc, i8** argv) so command-line args are available via
     * the Environment.ArgCount()/ArgAt() builtins. */
    LLVMTypeRef i32ty = LLVMInt32TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef i8ptrptr = LLVMPointerType(i8ptr, 0);
    LLVMTypeRef main_params[] = { i32ty, i8ptrptr };
    LLVMTypeRef main_type = LLVMFunctionType(i32ty, main_params, 2, 0);
    LLVMValueRef main_fn = LLVMAddFunction(g->mod, "main", main_type);

    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, main_fn, "entry");
    LLVMPositionBuilderAtEnd(g->builder, entry);
    di_clear(g);

    /* On Windows, switch the console code page to UTF-8 (65001) so that
     * non-ASCII output (e.g. CJK text) renders correctly instead of being
     * decoded with the legacy OEM/ANSI codepage. Our string data is UTF-8, and
     * this only affects console handles (a no-op when stdout is a pipe/file),
     * so redirected output keeps its raw UTF-8 bytes. */
    if (g->target_is_windows) {
        LLVMTypeRef uintt = LLVMInt32TypeInContext(g->ctx);
        LLVMTypeRef setcp_type = LLVMFunctionType(uintt, (LLVMTypeRef[]){ uintt }, 1, 0);
        LLVMValueRef fn_set_out = LLVMGetNamedFunction(g->mod, "SetConsoleOutputCP");
        if (!fn_set_out) fn_set_out = LLVMAddFunction(g->mod, "SetConsoleOutputCP", setcp_type);
        LLVMValueRef fn_set_in = LLVMGetNamedFunction(g->mod, "SetConsoleCP");
        if (!fn_set_in) fn_set_in = LLVMAddFunction(g->mod, "SetConsoleCP", setcp_type);
        LLVMValueRef cp_utf8 = LLVMConstInt(uintt, 65001, 0);
        zan_call2(g->builder, setcp_type, fn_set_out, &cp_utf8, 1, "");
        zan_call2(g->builder, setcp_type, fn_set_in, &cp_utf8, 1, "");
    }

    /* stash argc/argv into module globals for Environment.* builtins */
    LLVMValueRef g_argc = LLVMGetNamedGlobal(g->mod, "__zan_argc");
    if (!g_argc) {
        g_argc = LLVMAddGlobal(g->mod, i32ty, "__zan_argc");
        LLVMSetInitializer(g_argc, LLVMConstInt(i32ty, 0, 0));
    }
    LLVMValueRef g_argv = LLVMGetNamedGlobal(g->mod, "__zan_argv");
    if (!g_argv) {
        g_argv = LLVMAddGlobal(g->mod, i8ptrptr, "__zan_argv");
        LLVMSetInitializer(g_argv, LLVMConstNull(i8ptrptr));
    }
    LLVMBuildStore(g->builder, LLVMGetParam(main_fn, 0), g_argc);
    LLVMBuildStore(g->builder, LLVMGetParam(main_fn, 1), g_argv);

    /* Programs emit UTF-8 bytes; the Windows console defaults to the legacy
     * OEM/ANSI code page, which renders CJK/accented output as mojibake.
     * Switch the console to UTF-8 (65001). Only affects console rendering:
     * output redirected to a pipe/file keeps its raw UTF-8 bytes. */
    if (g->target_is_windows) {
        LLVMValueRef set_out_cp = LLVMGetNamedFunction(g->mod, "SetConsoleOutputCP");
        LLVMTypeRef setcp_type = LLVMFunctionType(i32ty, (LLVMTypeRef[]){ i32ty }, 1, 0);
        if (!set_out_cp)
            set_out_cp = LLVMAddFunction(g->mod, "SetConsoleOutputCP", setcp_type);
        LLVMValueRef set_in_cp = LLVMGetNamedFunction(g->mod, "SetConsoleCP");
        if (!set_in_cp)
            set_in_cp = LLVMAddFunction(g->mod, "SetConsoleCP", setcp_type);
        LLVMValueRef utf8cp = LLVMConstInt(i32ty, 65001, 0);
        zan_call2(g->builder, setcp_type, set_out_cp, &utf8cp, 1, "");
        zan_call2(g->builder, setcp_type, set_in_cp, &utf8cp, 1, "");
    }

    g->current_fn = main_fn;
    g->current_fn_ret_type = LLVMInt32TypeInContext(g->ctx);
    g->current_type_sym = type_sym;
    g->current_this = NULL;
    g->current_fn_body = method->method_decl.body;

    /* schedule the leak report to run at program exit */
    if (g->check_leaks) {
        emit_leak_report_support(g);
        zan_call2(g->builder, g->atexit_type, g->fn_atexit,
                       &g->fn_report_leaks, 1, "");
    }

    /* Initialize the coroutine scheduler exactly once, before the body runs.
     * Task.Spawn appends onto the ready queue; a root-level `await` used to call
     * zan_co_sched_init itself, which resets the queue and would discard any
     * coroutine spawned before that await. Doing it once here (dominating every
     * Task.Spawn and every root await) fixes concurrent client/server programs
     * where the server is spawned and a client is awaited. Harmless for
     * non-async programs (it just nulls an already-empty queue). */
    zan_call2(g->builder, g->rt_co_sched_init_type, g->rt_co_sched_init, NULL, 0, "");

    /* Apply static-field initializers once, at program entry, into their
     * backing globals. Runs before the Main body so every subsequent read
     * (in Main or in any method/coroutine) observes the initialized value. */
    if (unit && unit->kind == AST_COMPILATION_UNIT) {
        local_scope_t *sf_locals = local_scope_new(g->arena);
        zan_symbol_t *saved_type = g->current_type_sym;
        LLVMValueRef saved_this = g->current_this;
        g->current_this = NULL;
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
                if (!m->field_decl.initializer) continue;
                zan_symbol_t *fs = get_field_sym(csym, m->field_decl.name);
                LLVMValueRef gv = get_static_field_global(g, csym, fs);
                if (!gv) continue;
                LLVMValueRef v = emit_expr(g, m->field_decl.initializer, sf_locals);
                if (fs->type && is_rc_managed_type(fs->type)) {
                    emit_rc_store_field(g, fs->type, gv, v, m->field_decl.initializer, sf_locals,
                                        (fs->modifiers & MOD_WEAK) ? 1 : 0);
                } else {
                    LLVMTypeRef ft = fs->type ? map_type(g, fs->type)
                                              : LLVMInt64TypeInContext(g->ctx);
                    LLVMBuildStore(g->builder, coerce_int_to(g, v, ft), gv);
                }
            }
        }
        g->current_type_sym = saved_type;
        g->current_this = saved_this;
    }

    local_scope_t *locals = local_scope_new(g->arena);

    if (method->method_decl.body) {
        emit_stmt(g, method->method_decl.body, locals);
    }

    /* add return 0 if no terminator */
    if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder))) {
        emit_release_owned_locals(g, locals);
        emit_release_static_rc_fields(g, unit);
        LLVMBuildRet(g->builder, LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0));
    }
    g->current_type_sym = NULL;
    g->current_fn_body = NULL;
}

/* ---- emit user-defined methods ---- */

/* Deferred body-emission work item: captures everything needed to emit a
 * method/constructor body after ALL functions have been declared, so bodies
 * may make forward references to methods declared later. */
typedef struct {
    zan_ast_node_t *member;
    zan_symbol_t   *type_sym;
    LLVMValueRef    fn;
    LLVMTypeRef    *param_types;
    int             param_count;
    int             param_offset;
    bool            is_static;
    LLVMTypeRef     llvm_ret;
    zan_type_t     *ret_type;
    /* async CPS lowering: when is_async, `fn` is the ramp (returns the task
     * handle) and the body is emitted into `resume_fn` over `frame_type`. */
    bool            is_async;
    LLVMValueRef    resume_fn;
    LLVMTypeRef     frame_type;
    int             await_count;   /* await points in the body (states 1..N) */
    async_local_t  *alocals;       /* named scalar locals held in the frame */
    int             alocal_count;
    int             sub_base;       /* frame index of the first sub-task slot */
    zan_type_t     *cur_inst;       /* instantiation being specialized, or NULL */
} method_body_work_t;

/* Count how many discovered instantiations exist for a given generic type. */
static int generic_variant_count(zan_irgen_t *g, zan_symbol_t *type_sym) {
    int n = 0;
    for (int i = 0; i < g->generic_inst_count; i++)
        if (g->generic_insts[i].type_sym == type_sym) n++;
    return n;
}

static void emit_user_methods(zan_irgen_t *g, zan_ast_node_t *unit) {
    /* Discover every concrete instantiation of a user generic class up front so
     * Pass A can emit one specialized variant per instantiation (in addition to
     * the erased variant). */
    discover_generic_insts(g, unit);

    /* Size an upper bound for the deferred body work list, counting the erased
     * variant plus one per discovered instantiation for generic types. */
    int work_cap = 0;
    for (int i = 0; i < unit->comp_unit.decls.count; i++) {
        zan_ast_node_t *decl = unit->comp_unit.decls.items[i];
        if (decl->kind == AST_CLASS_DECL || decl->kind == AST_STRUCT_DECL) {
            zan_symbol_t *ts = zan_binder_lookup(g->binder, decl->type_decl.name);
            int variants = 1 + (ts ? generic_variant_count(g, ts) : 0);
            work_cap += decl->type_decl.members.count * variants;
        }
    }
    method_body_work_t *work = NULL;
    int work_count = 0;
    if (work_cap > 0) {
        work = (method_body_work_t *)calloc((size_t)work_cap, sizeof(method_body_work_t));
    }

    /* Pass A: declare & register every function/constructor. */
    for (int i = 0; i < unit->comp_unit.decls.count; i++) {
        zan_ast_node_t *decl = unit->comp_unit.decls.items[i];
        if (decl->kind != AST_CLASS_DECL && decl->kind != AST_STRUCT_DECL) continue;

        /* look up symbol for this type */
        zan_symbol_t *type_sym = zan_binder_lookup(g->binder, decl->type_decl.name);
        if (!type_sym) continue;

        /* Emit one variant per (erased + each concrete instantiation). The
         * erased variant (cur_variant == NULL) keeps the existing symbol names
         * and registration; specialized variants add a name suffix and register
         * into the generic fn/ctor tables. Signatures are identical across
         * variants (type parameters still lower to the erased representation);
         * only the body differs, via g->cur_inst set in Pass B. */
        zan_type_t *variants[64];
        int nvar = 0;
        variants[nvar++] = NULL;
        if (is_user_generic_sym(type_sym)) {
            for (int gi = 0; gi < g->generic_inst_count && nvar < 64; gi++)
                if (g->generic_insts[gi].type_sym == type_sym)
                    variants[nvar++] = g->generic_insts[gi].inst;
        }

        for (int vi = 0; vi < nvar; vi++) {
        zan_type_t *cur_variant = variants[vi];
        char vsuffix[256];
        vsuffix[0] = '\0';
        if (cur_variant) mangle_inst_suffix(vsuffix, sizeof(vsuffix), cur_variant);

        for (int j = 0; j < decl->type_decl.members.count; j++) {
            zan_ast_node_t *member = decl->type_decl.members.items[j];
            bool is_ctor = (member->kind == AST_CONSTRUCTOR_DECL);
            if (member->kind != AST_METHOD_DECL && !is_ctor) continue;
            /* extern/DllImport methods have no generic body: only the erased
             * variant declares them. */
            if (cur_variant && member->kind == AST_METHOD_DECL &&
                member->method_decl.extern_lib.str && !member->method_decl.body)
                continue;

            /* [DllImport] extern methods: generate extern declaration, skip body */
            if (member->kind == AST_METHOD_DECL && member->method_decl.extern_lib.str &&
                !member->method_decl.body) {
                /* build extern function declaration */
                int pc = member->method_decl.params.count;
                LLVMTypeRef *pt = (LLVMTypeRef *)calloc((size_t)(pc > 0 ? pc : 1), sizeof(LLVMTypeRef));
                for (int k = 0; k < pc; k++) {
                    zan_ast_node_t *param = member->method_decl.params.items[k];
                    zan_type_t *ptype = zan_binder_resolve_type(g->binder, param->param.type);
                    pt[k] = map_type(g, ptype);
                }
                zan_type_t *rt = member->method_decl.return_type
                    ? zan_binder_resolve_type(g->binder, member->method_decl.return_type)
                    : g->binder->type_void;
                LLVMTypeRef llvm_rt = map_type(g, rt);
                LLVMTypeRef ft = LLVMFunctionType(llvm_rt, pt, (unsigned)pc, 0);
                /* use entry_point if specified, otherwise method name */
                char ext_name[256];
                if (member->method_decl.entry_point.str) {
                    snprintf(ext_name, sizeof(ext_name), "%.*s",
                             (int)member->method_decl.entry_point.len,
                             member->method_decl.entry_point.str);
                } else {
                    snprintf(ext_name, sizeof(ext_name), "%.*s",
                             (int)member->method_decl.name.len,
                             member->method_decl.name.str);
                }
                if (strncmp(ext_name, "zan_atomic_int_", 15) == 0 ||
                    strncmp(ext_name, "zan_shared_table_", 17) == 0 ||
                    strncmp(ext_name, "zan_thread_", 11) == 0 ||
                    strncmp(ext_name, "zan_dispatch_", 13) == 0) {
                    g->uses_sync_runtime = true;
                }
                if (strncmp(ext_name, "zan_io_socket_", 14) == 0) {
                    g->uses_socket_async = true;
                }
                if (strncmp(ext_name, "zan_gate_", 9) == 0) {
                    g->uses_socket_async = true;
                }
                /* Reuse existing declaration if the symbol already exists in the module
                 * (e.g. built-in malloc/free/strlen, or duplicate DllImport across files). */
                LLVMValueRef efn = LLVMGetNamedFunction(g->mod, ext_name);
                if (!efn) {
                    efn = LLVMAddFunction(g->mod, ext_name, ft);
                }
                /* register as a static method so it can be called */
                zan_symbol_t *method_sym = method_sym_for_decl(type_sym, member);
                if (method_sym) {
                    irgen_register_function(g, method_sym, efn, ft);
                }
                /* store lib name for linker */
                if (g->extern_lib_count < 64) {
                    bool already = false;
                    for (int li = 0; li < g->extern_lib_count; li++) {
                        if (g->extern_libs[li].len == member->method_decl.extern_lib.len &&
                            memcmp(g->extern_libs[li].str, member->method_decl.extern_lib.str,
                                   member->method_decl.extern_lib.len) == 0) {
                            already = true;
                            break;
                        }
                    }
                    if (!already) {
                        g->extern_libs[g->extern_lib_count++] = member->method_decl.extern_lib;
                    }
                }
                /* record (lib, fn) so an unresolvable lib can be stubbed when
                 * cross-linking a static Linux binary */
                if (g->extern_fn_count < (int)(sizeof(g->extern_fns) / sizeof(g->extern_fns[0]))) {
                    zan_istr_t sym = member->method_decl.entry_point.str
                        ? member->method_decl.entry_point
                        : member->method_decl.name;
                    bool seen = false;
                    for (int fi = 0; fi < g->extern_fn_count; fi++) {
                        if (g->extern_fns[fi].name.len == sym.len &&
                            memcmp(g->extern_fns[fi].name.str, sym.str, sym.len) == 0) {
                            seen = true;
                            break;
                        }
                    }
                    if (!seen) {
                        g->extern_fns[g->extern_fn_count].lib = member->method_decl.extern_lib;
                        g->extern_fns[g->extern_fn_count].name = sym;
                        g->extern_fn_count++;
                    }
                }
                free(pt);
                continue;
            }

            if (!member->method_decl.body) continue;

            /* skip static Main — handled separately */
            bool is_static = !is_ctor && (member->method_decl.modifiers & MOD_STATIC) != 0;
            if (is_static && member->method_decl.name.len == 4 &&
                memcmp(member->method_decl.name.str, "Main", 4) == 0) continue;

            /* build function name: TypeName_MethodName or TypeName_ctor,
             * plus a per-instantiation suffix (e.g. HashSet_Add$string) for a
             * specialized variant so it does not collide with the erased one. */
            char fn_name[512];
            if (is_ctor) {
                snprintf(fn_name, sizeof(fn_name), "%.*s_ctor%s",
                         (int)decl->type_decl.name.len, decl->type_decl.name.str,
                         vsuffix);
            } else {
                snprintf(fn_name, sizeof(fn_name), "%.*s_%.*s%s",
                         (int)decl->type_decl.name.len, decl->type_decl.name.str,
                         (int)member->method_decl.name.len, member->method_decl.name.str,
                         vsuffix);
            }

            /* build function type */
            int param_count = member->method_decl.params.count;
            int total_params = is_static ? param_count : param_count + 1;
            LLVMTypeRef *param_types = (LLVMTypeRef *)calloc((size_t)(total_params > 0 ? total_params : 1), sizeof(LLVMTypeRef));

            int param_offset = 0;
            if (!is_static) {
                /* this pointer for instance methods */
                LLVMTypeRef struct_type = get_struct_llvm_type(g, type_sym);
                param_types[0] = struct_type ? LLVMPointerType(struct_type, 0)
                                             : LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                param_offset = 1;
            }

            for (int k = 0; k < param_count; k++) {
                zan_ast_node_t *param = member->method_decl.params.items[k];
                zan_type_t *pt = zan_binder_resolve_type(g->binder, param->param.type);
                param_types[k + param_offset] = param->param.by_ref
                    ? LLVMPointerType(map_type(g, pt), 0)
                    : map_type(g, pt);
            }

            zan_type_t *ret_type = is_ctor ? g->binder->type_void
                : (member->method_decl.return_type
                    ? zan_binder_resolve_type(g->binder, member->method_decl.return_type)
                    : g->binder->type_void);
            LLVMTypeRef llvm_ret = map_type(g, ret_type);
            /* async methods lower to a heap frame + ramp + resume (see
             * docs/ASYNC_CPS_DESIGN.md); ctors are never async. */
            bool is_async = !is_ctor && (member->method_decl.modifiers & MOD_ASYNC) != 0;

            LLVMTypeRef fn_type = NULL;
            LLVMValueRef fn = NULL;
            LLVMValueRef resume_fn = NULL;
            LLVMTypeRef frame_type = NULL;
            int a_await_count = 0;
            async_local_t *a_locals = NULL;
            int a_local_count = 0;
            int a_sub_base = 0;

            if (is_async) {
                LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
                LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);

                /* Normalize awaits into A-normal form so no intermediate value
                 * crosses a suspension in a register (compound / multiple
                 * awaits). This mutates the body in place; the scan and body
                 * emission below both see the rewritten tree. */
                {
                    int anf_counter = 0;
                    anf_normalize_block(g, member->method_decl.body, &anf_counter);
                }

                /* Scan the body: count await points (→ states / sub-task slots)
                 * and collect named scalar locals that must live in the frame
                 * so they survive across suspensions. */
                async_scan_t scan = { g, 0, NULL, 0, 0 };
                async_scan_stmt(&scan, member->method_decl.body);
                a_await_count = scan.await_count;
                a_locals = scan.locals;
                a_local_count = scan.local_count;

                /* frame = fixed 5-field header + params + frame locals +
                 * one i8* sub-task handle per await point. */
                int locals_base = ASYNC_FRAME_FIRST_PARAM + total_params;
                a_sub_base = locals_base + a_local_count;
                int nfields = a_sub_base + a_await_count;
                LLVMTypeRef *fields = (LLVMTypeRef *)calloc((size_t)nfields, sizeof(LLVMTypeRef));
                fields[ASYNC_FRAME_STATE] = i32;
                fields[ASYNC_FRAME_DONE] = i32;
                fields[ASYNC_FRAME_AWAITER] = i8ptr;
                fields[ASYNC_FRAME_AWAITER_STEP] = g->co_step_ptr;
                fields[ASYNC_FRAME_RESULT] = i64;
                for (int k = 0; k < total_params; k++) {
                    fields[ASYNC_FRAME_FIRST_PARAM + k] = param_types[k];
                }
                for (int k = 0; k < a_local_count; k++) {
                    a_locals[k].frame_index = locals_base + k;
                    fields[locals_base + k] = a_locals[k].llvm;
                }
                for (int k = 0; k < a_await_count; k++) {
                    fields[a_sub_base + k] = i8ptr;
                }
                char frame_name[560];
                snprintf(frame_name, sizeof(frame_name), "%s$frame", fn_name);
                frame_type = LLVMStructCreateNamed(g->ctx, frame_name);
                LLVMStructSetBody(frame_type, fields, (unsigned)nfields, 0);
                free(fields);

                /* ramp keeps the external param list but returns the task
                 * handle (i8*); the body runs later in resume(frame). */
                fn_type = LLVMFunctionType(i8ptr, param_types, (unsigned)total_params, 0);
                fn = LLVMAddFunction(g->mod, fn_name, fn_type);

                char resume_name[560];
                snprintf(resume_name, sizeof(resume_name), "%s$resume", fn_name);
                resume_fn = LLVMAddFunction(g->mod, resume_name, g->co_step_type);
            } else {
                fn_type = LLVMFunctionType(llvm_ret, param_types, (unsigned)total_params, 0);
                fn = LLVMAddFunction(g->mod, fn_name, fn_type);
            }

            /* register in function/ctor table. For async methods the ramp is
             * the callable symbol (a call site receives the task handle). The
             * erased variant registers in the ordinary tables; a specialized
             * variant registers in the generic tables keyed by (sym, args) so a
             * concrete call site can route to it. */
            if (is_ctor) {
                if (cur_variant) {
                    add_generic_ctor(g, type_sym, cur_variant->type_args,
                                     cur_variant->type_arg_count, param_count,
                                     fn, fn_type);
                } else if (g->ctor_count < 256) {
                    g->ctors[g->ctor_count].type_sym = type_sym;
                    g->ctors[g->ctor_count].fn = fn;
                    g->ctors[g->ctor_count].fn_type = fn_type;
                    g->ctors[g->ctor_count].param_count = param_count;
                    g->ctor_count++;
                }
            } else {
                zan_symbol_t *method_sym = method_sym_for_decl(type_sym, member);
                if (cur_variant) {
                    add_generic_fn(g, method_sym, cur_variant->type_args,
                                   cur_variant->type_arg_count, fn, fn_type);
                } else {
                    irgen_register_function(g, method_sym, fn, fn_type);
                }
            }

            /* defer body emission to Pass B so calls may forward-reference
             * methods declared later in this (or another) type */
            if (work && work_count < work_cap) {
                work[work_count].member = member;
                work[work_count].type_sym = type_sym;
                work[work_count].fn = fn;
                work[work_count].param_types = param_types;
                work[work_count].param_count = param_count;
                work[work_count].param_offset = param_offset;
                work[work_count].is_static = is_static;
                work[work_count].llvm_ret = llvm_ret;
                work[work_count].ret_type = ret_type;
                work[work_count].is_async = is_async;
                work[work_count].resume_fn = resume_fn;
                work[work_count].frame_type = frame_type;
                work[work_count].await_count = a_await_count;
                work[work_count].alocals = a_locals;
                work[work_count].alocal_count = a_local_count;
                work[work_count].sub_base = a_sub_base;
                work[work_count].cur_inst = cur_variant;
                work_count++;
            } else {
                free(param_types);
            }
        }
        } /* end variant loop */
    }

    /* Pass B: emit every deferred body now that all functions are declared. */
    for (int w = 0; w < work_count; w++) {
        zan_ast_node_t *member = work[w].member;
        zan_symbol_t *type_sym = work[w].type_sym;
        LLVMValueRef fn = work[w].fn;
        LLVMTypeRef *param_types = work[w].param_types;
        int param_count = work[w].param_count;
        int param_offset = work[w].param_offset;
        bool is_static = work[w].is_static;
        LLVMTypeRef llvm_ret = work[w].llvm_ret;
        zan_type_t *ret_type = work[w].ret_type;
        LLVMValueRef this_alloca = NULL;
        /* Activate the instantiation context for a specialized variant so that
         * intrinsic element comparisons in this body substitute the type
         * parameter to its concrete argument (NULL for erased/non-generic). */
        g->cur_inst = work[w].cur_inst;

        /* async method: emit ramp (allocate frame, stash params, hand out the
         * task handle) + resume (state-machine entry running the body). See
         * docs/ASYNC_CPS_DESIGN.md. */
        if (work[w].is_async) {
            LLVMValueRef ramp_fn = fn;
            LLVMValueRef resume_fn = work[w].resume_fn;
            LLVMTypeRef frame_type = work[w].frame_type;
            LLVMTypeRef frame_ptr_ty = LLVMPointerType(frame_type, 0);
            LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
            LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            int total_params = param_count + param_offset;

            /* ---- ramp ---- */
            LLVMBasicBlockRef ramp_entry = LLVMAppendBasicBlockInContext(g->ctx, ramp_fn, "entry");
            LLVMPositionBuilderAtEnd(g->builder, ramp_entry);
            di_clear(g);
            LLVMTypeRef malloc_ty = LLVMGlobalGetValueType(g->fn_malloc);
            LLVMValueRef fsize = LLVMSizeOf(frame_type);
            LLVMValueRef raw = zan_call2(g->builder, malloc_ty, g->fn_malloc, &fsize, 1, "frame.raw");
            /* Zero the frame so every owning (RC) local slot starts null. The
             * per-iteration capture of a loop-body local releases the reloaded
             * previous occupant of its slot; that requires the slot to be null
             * (not garbage) before its first write. */
            {
                LLVMTypeRef i8ptr0 = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                LLVMTypeRef memset_ty = LLVMFunctionType(i8ptr0,
                    (LLVMTypeRef[]){ i8ptr0, LLVMInt32TypeInContext(g->ctx),
                                     LLVMInt64TypeInContext(g->ctx) }, 3, 0);
                LLVMValueRef memset_fn = LLVMGetNamedFunction(g->mod, "memset");
                if (!memset_fn) memset_fn = LLVMAddFunction(g->mod, "memset", memset_ty);
                zan_call2(g->builder, memset_ty, memset_fn,
                    (LLVMValueRef[]){ raw, LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0),
                                      fsize }, 3, "");
            }
            LLVMValueRef rframe = LLVMBuildBitCast(g->builder, raw, frame_ptr_ty, "frame");
            LLVMBuildStore(g->builder, LLVMConstInt(i32, 0, 0),
                LLVMBuildStructGEP2(g->builder, frame_type, rframe, ASYNC_FRAME_STATE, "st"));
            LLVMBuildStore(g->builder, LLVMConstInt(i32, 0, 0),
                LLVMBuildStructGEP2(g->builder, frame_type, rframe, ASYNC_FRAME_DONE, "dn"));
            LLVMBuildStore(g->builder, LLVMConstNull(i8ptr),
                LLVMBuildStructGEP2(g->builder, frame_type, rframe, ASYNC_FRAME_AWAITER, "aw"));
            LLVMBuildStore(g->builder, LLVMConstNull(g->co_step_ptr),
                LLVMBuildStructGEP2(g->builder, frame_type, rframe, ASYNC_FRAME_AWAITER_STEP, "aws"));
            LLVMBuildStore(g->builder, LLVMConstInt(i64, 0, 0),
                LLVMBuildStructGEP2(g->builder, frame_type, rframe, ASYNC_FRAME_RESULT, "rs"));
            for (int k = 0; k < total_params; k++) {
                LLVMValueRef pv = LLVMGetParam(ramp_fn, (unsigned)k);
                LLVMValueRef slot = LLVMBuildStructGEP2(g->builder, frame_type, rframe,
                    (unsigned)(ASYNC_FRAME_FIRST_PARAM + k), "arg");
                LLVMBuildStore(g->builder, pv, slot);
                /* The caller passes arguments by borrow and releases owned temps
                 * right after the ramp returns, but the heap frame outlives that
                 * temp and the coroutine reads the argument after suspension. So
                 * the frame takes ownership of ARC-managed by-value arguments: it
                 * retains here (synchronously, before the caller's release) and
                 * releases them from emit_async_complete (see the matching
                 * arc_owned marking on the resume-body param locals). `this`
                 * (k < param_offset) stays a borrow, as in synchronous methods. */
                if (k >= param_offset) {
                    zan_ast_node_t *pn = member->method_decl.params.items[k - param_offset];
                    if (!pn->param.by_ref) {
                        zan_type_t *pt = zan_binder_resolve_type(g->binder, pn->param.type);
                        if (is_rc_managed_type(pt) &&
                            LLVMGetTypeKind(LLVMTypeOf(pv)) == LLVMPointerTypeKind) {
                            emit_rc_retain_for_type(g, pt, pv);
                        }
                    }
                }
            }
            LLVMBuildRet(g->builder, raw);

            /* ---- resume ---- */
            LLVMBasicBlockRef res_entry = LLVMAppendBasicBlockInContext(g->ctx, resume_fn, "entry");
            LLVMPositionBuilderAtEnd(g->builder, res_entry);
            di_clear(g);
            LLVMValueRef fparam = LLVMGetParam(resume_fn, 0);
            LLVMValueRef sframe = LLVMBuildBitCast(g->builder, fparam, frame_ptr_ty, "frame");

            local_scope_t *locals = local_scope_new(g->arena);

            /* Frame-resident slots: `this` (instance methods), params, and named
             * scalar locals. Their storage is stack allocas created here in the
             * entry block (so they dominate every state block); values are saved
             * to / reloaded from the heap frame around each suspension. */
            int alocal_count = work[w].alocal_count;
            int slot_total = total_params + alocal_count;
            zan_async_slot_t *slots = (zan_async_slot_t *)zan_arena_alloc(g->arena,
                sizeof(zan_async_slot_t) * (size_t)(slot_total > 0 ? slot_total : 1));
            int si = 0;
            LLVMValueRef res_this = NULL;
            if (!is_static) {
                res_this = LLVMBuildAlloca(g->builder, param_types[0], "this");
                slots[si].slot_alloca = res_this;
                slots[si].llvm = param_types[0];
                slots[si].frame_index = ASYNC_FRAME_FIRST_PARAM;
                si++;
            }
            for (int k = 0; k < param_count; k++) {
                zan_ast_node_t *param = member->method_decl.params.items[k];
                LLVMTypeRef pty = param_types[k + param_offset];
                LLVMValueRef pa = LLVMBuildAlloca(g->builder, pty, "p");
                zan_type_t *pt = zan_binder_resolve_type(g->binder, param->param.type);
                local_add(locals, param->param.name, pa, pt);
                /* Balances the retain the ramp performed for ARC-managed by-value
                 * params: the frame owns them, so release at coroutine completion. */
                if (!param->param.by_ref && is_rc_managed_type(pt) &&
                    LLVMGetTypeKind(pty) == LLVMPointerTypeKind) {
                    locals->vars[locals->count - 1].arc_owned = 1;
                }
                slots[si].slot_alloca = pa;
                slots[si].llvm = pty;
                slots[si].frame_index = ASYNC_FRAME_FIRST_PARAM + param_offset + k;
                si++;
            }
            for (int k = 0; k < alocal_count; k++) {
                LLVMValueRef la = LLVMBuildAlloca(g->builder, work[w].alocals[k].llvm, "fl");
                local_add(locals, work[w].alocals[k].name, la, work[w].alocals[k].ztype);
                slots[si].slot_alloca = la;
                slots[si].llvm = work[w].alocals[k].llvm;
                slots[si].frame_index = work[w].alocals[k].frame_index;
                si++;
            }

            LLVMValueRef state = LLVMBuildLoad2(g->builder, i32,
                LLVMBuildStructGEP2(g->builder, frame_type, sframe, ASYNC_FRAME_STATE, "st.ptr"),
                "state");
            LLVMBasicBlockRef body_bb = LLVMAppendBasicBlockInContext(g->ctx, resume_fn, "co.start");
            /* switch(state): case 0 (start) -> body; each await point appends a
             * resume-k case (see the AST_AWAIT_EXPR lowering). */
            LLVMValueRef sw = LLVMBuildSwitch(g->builder, state, body_bb, (unsigned)(work[w].await_count + 1));
            LLVMAddCase(sw, LLVMConstInt(i32, 0, 0), body_bb);

            LLVMValueRef saved_fn = g->current_fn;
            LLVMTypeRef saved_fn_ret = g->current_fn_ret_type;
            LLVMValueRef saved_this = g->current_this;
            zan_symbol_t *saved_type_sym = g->current_type_sym;
            LLVMValueRef saved_async_frame = g->current_async_frame;
            LLVMTypeRef saved_async_frame_type = g->current_async_frame_type;
            LLVMValueRef saved_async_resume_fn = g->current_async_resume_fn;
            LLVMValueRef saved_async_switch = g->current_async_switch;
            int saved_next_state = g->current_async_next_state;
            int saved_sub_base = g->current_async_sub_base;
            int saved_sub_next = g->current_async_sub_next;
            void *saved_slots = (void *)g->current_async_slots;
            int saved_slot_count = g->current_async_slot_count;

            g->current_fn = resume_fn;
            g->current_fn_ret_type = LLVMVoidTypeInContext(g->ctx);
            g->current_this = is_static ? NULL : res_this;
            g->current_type_sym = type_sym;
            g->current_async_frame = sframe;
            g->current_async_frame_type = frame_type;
            g->current_async_resume_fn = resume_fn;
            g->current_async_switch = sw;
            g->current_async_next_state = 1;
            g->current_async_sub_base = work[w].sub_base;
            g->current_async_sub_next = 0;
            g->current_async_slots = slots;
            g->current_async_slot_count = slot_total;

            LLVMPositionBuilderAtEnd(g->builder, body_bb);
            emit_async_reload_slots(g); /* load `this`/params from the frame */

            if (member->method_decl.body->kind == AST_BLOCK) {
                for (int k = 0; k < member->method_decl.body->block.stmts.count; k++) {
                    emit_stmt(g, member->method_decl.body->block.stmts.items[k], locals);
                }
            } else {
                /* expression body (=> expr): treat as `return expr`. */
                LLVMValueRef val = emit_expr(g, member->method_decl.body, locals);
                emit_async_complete(g, locals, coerce_to_i64(g, val));
            }

            /* fall off the end: implicit completion (void / default result). */
            if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder))) {
                emit_async_complete(g, locals, NULL);
            }

            g->current_fn = saved_fn;
            g->current_fn_ret_type = saved_fn_ret;
            g->current_this = saved_this;
            g->current_type_sym = saved_type_sym;
            g->current_async_frame = saved_async_frame;
            g->current_async_frame_type = saved_async_frame_type;
            g->current_async_resume_fn = saved_async_resume_fn;
            g->current_async_switch = saved_async_switch;
            g->current_async_next_state = saved_next_state;
            g->current_async_sub_base = saved_sub_base;
            g->current_async_sub_next = saved_sub_next;
            g->current_async_slots = saved_slots;
            g->current_async_slot_count = saved_slot_count;
            free(param_types);
            continue;
        }

        LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
        LLVMPositionBuilderAtEnd(g->builder, entry);
        di_clear(g);

        local_scope_t *locals = local_scope_new(g->arena);

        if (!is_static) {
            /* bind 'this' as first parameter */
            this_alloca = LLVMBuildAlloca(g->builder, param_types[0], "this");
            LLVMBuildStore(g->builder, LLVMGetParam(fn, 0), this_alloca);
        }

        /* bind method parameters */
        for (int k = 0; k < param_count; k++) {
            zan_ast_node_t *param = member->method_decl.params.items[k];
            zan_type_t *pt = zan_binder_resolve_type(g->binder, param->param.type);
            if (param->param.by_ref) {
                /* `ref`/`out`: the incoming pointer IS the storage slot, so
                 * reads/writes go straight through to the caller's variable. */
                local_add(locals, param->param.name,
                          LLVMGetParam(fn, (unsigned)(k + param_offset)), pt);
                continue;
            }
            LLVMValueRef param_alloca = LLVMBuildAlloca(g->builder, param_types[k + param_offset], "p");
            LLVMBuildStore(g->builder, LLVMGetParam(fn, (unsigned)(k + param_offset)), param_alloca);
            local_add(locals, param->param.name, param_alloca, pt);
        }

        LLVMValueRef saved_fn = g->current_fn;
        LLVMTypeRef saved_fn_ret = g->current_fn_ret_type;
        LLVMValueRef saved_this = g->current_this;
        zan_symbol_t *saved_type_sym = g->current_type_sym;
        zan_ast_node_t *saved_fn_body = g->current_fn_body;
        g->current_fn = fn;
        g->current_fn_ret_type = llvm_ret;
        g->current_this = is_static ? NULL : this_alloca;
        g->current_type_sym = type_sym;
        g->current_fn_body = member->method_decl.body;

        /* implicit base construction: a derived constructor first chains to
         * its base class's parameterless constructor so inherited fields are
         * initialised (base fields are laid out as a prefix of the derived
         * struct, so `this` upcasts by a plain bitcast). */
        if (member->kind == AST_CONSTRUCTOR_DECL && type_sym->type && type_sym->type->base_type &&
            type_sym->type->base_type->sym) {
            zan_symbol_t *base_sym = type_sym->type->base_type->sym;
            for (int ci = 0; ci < g->ctor_count; ci++) {
                if (g->ctors[ci].type_sym == base_sym && g->ctors[ci].param_count == 0) {
                    LLVMValueRef thisv = LLVMBuildLoad2(g->builder, param_types[0], this_alloca, "this.base");
                    LLVMTypeRef bst = get_struct_llvm_type(g, base_sym);
                    if (bst) thisv = LLVMBuildBitCast(g->builder, thisv, LLVMPointerType(bst, 0), "base.this");
                    zan_call2(g->builder, g->ctors[ci].fn_type, g->ctors[ci].fn, &thisv, 1, "");
                    break;
                }
            }
        }

        /* emit method body */
        if (member->method_decl.body->kind == AST_BLOCK) {
            for (int k = 0; k < member->method_decl.body->block.stmts.count; k++) {
                emit_stmt(g, member->method_decl.body->block.stmts.items[k], locals);
            }
        } else {
            /* expression body (=> expr) */
            LLVMValueRef val = emit_expr(g, member->method_decl.body, locals);
            /* convert return type if needed */
            LLVMTypeRef val_t = LLVMTypeOf(val);
            if (val_t != llvm_ret) {
                if (LLVMGetTypeKind(llvm_ret) == LLVMFloatTypeKind &&
                    LLVMGetTypeKind(val_t) == LLVMDoubleTypeKind) {
                    val = LLVMBuildFPTrunc(g->builder, val, llvm_ret, "trunc");
                } else if (LLVMGetTypeKind(llvm_ret) == LLVMDoubleTypeKind &&
                           LLVMGetTypeKind(val_t) == LLVMFloatTypeKind) {
                    val = LLVMBuildFPExt(g->builder, val, llvm_ret, "ext");
                }
            }
            if (is_rc_managed_type(ret_type) &&
                !expr_yields_owned_rc_value(g, member->method_decl.body, locals)) {
                emit_rc_retain_for_type(g, ret_type, val);
            }
            emit_release_owned_locals(g, locals);
            LLVMBuildRet(g->builder, val);
        }

        /* ensure function has terminator */
        LLVMBasicBlockRef cur_bb = LLVMGetInsertBlock(g->builder);
        if (!LLVMGetBasicBlockTerminator(cur_bb)) {
            emit_release_owned_locals(g, locals);
            if (ret_type->kind == TYPE_VOID) {
                LLVMBuildRetVoid(g->builder);
            } else {
                LLVMBuildRet(g->builder, LLVMConstNull(llvm_ret));
            }
        }

        g->current_fn = saved_fn;
        g->current_fn_ret_type = saved_fn_ret;
        g->current_this = saved_this;
        g->current_type_sym = saved_type_sym;
        g->current_fn_body = saved_fn_body;
        free(param_types);
    }

    g->cur_inst = NULL;
    free(work);
}

zan_status_t zan_irgen_emit(zan_irgen_t *g, zan_ast_node_t *unit) {
    if (!unit || unit->kind != AST_COMPILATION_UNIT) return ZAN_ERROR;
    g_di_emit_ctx = g; /* so local_add can forward variables to di_declare_var */

    /* Pass 1: register all struct/class types */
    for (int i = 0; i < unit->comp_unit.decls.count; i++) {
        zan_ast_node_t *decl = unit->comp_unit.decls.items[i];
        if (decl->kind == AST_STRUCT_DECL || decl->kind == AST_CLASS_DECL) {
            zan_symbol_t *sym = zan_binder_lookup(g->binder, decl->type_decl.name);
            if (sym) register_struct_type(g, sym);
        }
    }

    /* Pass 2: emit user-defined methods */
    emit_user_methods(g, unit);

    /* Pass 3: find and emit static Main method */
    for (int i = 0; i < unit->comp_unit.decls.count; i++) {
        zan_ast_node_t *decl = unit->comp_unit.decls.items[i];
        if (decl->kind != AST_CLASS_DECL && decl->kind != AST_STRUCT_DECL) continue;

        for (int j = 0; j < decl->type_decl.members.count; j++) {
            zan_ast_node_t *member = decl->type_decl.members.items[j];
            if (member->kind == AST_METHOD_DECL &&
                (member->method_decl.modifiers & MOD_STATIC) &&
                member->method_decl.name.len == 4 &&
                memcmp(member->method_decl.name.str, "Main", 4) == 0) {
                {
                    zan_symbol_t *main_type_sym = zan_binder_lookup(g->binder, decl->type_decl.name);
                    emit_main_method(g, member, main_type_sym, unit);
                }
                goto done;
            }
        }
    }
done:
    ;
    /* Synthesise per-class release functions now that every class type has
     * been registered (Pass 1) and referenced (Passes 2/3). */
    di_clear(g); /* the following are synthetic fns; no user source scope */
    emit_all_class_releases(g);
    emit_site_dtor_table(g);
    emit_vtables(g);
    /* An error diagnostic emitted during codegen (e.g. an unsupported await
     * form flagged by the ANF pass) must fail the build — the driver only
     * checks diagnostics before codegen, so surface it here. */
    if (zan_diag_has_errors(g->diag)) {
        return ZAN_ERROR;
    }
    /* Finalize DWARF metadata (resolves temporary nodes) before verification. */
    if (g->emit_debug && g->di_builder) {
        LLVMSetCurrentDebugLocation2(g->builder, NULL);
        LLVMDIBuilderFinalize(g->di_builder);
    }
    /* verify module */
    char *error = NULL;
    if (LLVMVerifyModule(g->mod, LLVMReturnStatusAction, &error)) {
        zan_diag_emit(g->diag, DIAG_ERROR, zan_loc(0, 0, 0, 0),
                      "LLVM verification failed: %s", error);
        LLVMDisposeMessage(error);
        return ZAN_ERROR;
    }
    if (error) LLVMDisposeMessage(error);

    return ZAN_OK;
}

/* ---- output ---- */

zan_status_t zan_irgen_write_ir(zan_irgen_t *g, const char *path) {
    char *ir = LLVMPrintModuleToString(g->mod);
    if (!path) {
        int rc = fputs(ir, stdout);
        LLVMDisposeMessage(ir);
        return rc < 0 ? ZAN_ERROR : ZAN_OK;
    }
    FILE *f = fopen(path, "w");
    if (!f) {
        LLVMDisposeMessage(ir);
        return ZAN_ERROR;
    }
    /* Report short writes (e.g. a full disk) instead of claiming success and
     * leaving a truncated .ll behind. */
    bool ok = (fputs(ir, f) >= 0);
    if (fclose(f) != 0) ok = false;
    LLVMDisposeMessage(ir);
    return ok ? ZAN_OK : ZAN_ERROR;
}

int zan_irgen_stub_extern_lib(zan_irgen_t *g, const char *lib, int lib_len) {
    int stubbed = 0;
    for (int i = 0; i < g->extern_fn_count; i++) {
        if ((int)g->extern_fns[i].lib.len != lib_len ||
            memcmp(g->extern_fns[i].lib.str, lib, (size_t)lib_len) != 0)
            continue;
        char nm[256];
        snprintf(nm, sizeof(nm), "%.*s", (int)g->extern_fns[i].name.len,
                 g->extern_fns[i].name.str);
        LLVMValueRef fn = LLVMGetNamedFunction(g->mod, nm);
        if (!fn) continue; /* optimized away: nothing references it */
        if (LLVMCountBasicBlocks(fn) > 0) continue; /* already defined */
        LLVMBasicBlockRef bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
        LLVMBuilderRef b = LLVMCreateBuilderInContext(g->ctx);
        LLVMPositionBuilderAtEnd(b, bb);
        LLVMTypeRef ft = LLVMGlobalGetValueType(fn);
        LLVMTypeRef rt = LLVMGetReturnType(ft);
        switch (LLVMGetTypeKind(rt)) {
        case LLVMVoidTypeKind:
            LLVMBuildRetVoid(b);
            break;
        case LLVMIntegerTypeKind:
            /* -1 doubles as SQL_ERROR / a generic nonzero failure code */
            LLVMBuildRet(b, LLVMConstInt(rt, (unsigned long long)-1, 1));
            break;
        case LLVMFloatTypeKind:
        case LLVMDoubleTypeKind:
            LLVMBuildRet(b, LLVMConstReal(rt, 0.0));
            break;
        default:
            LLVMBuildRet(b, LLVMConstNull(rt));
            break;
        }
        LLVMDisposeBuilder(b);
        stubbed++;
    }
    return stubbed;
}

/* wasm32 libc adapters (see zan_irgen_write_obj): define `fn` (which must be
 * a body-less function of type src_ft) as a thin wrapper that converts its
 * arguments to `lft` (the real 32-bit libc signature), calls `real`, and
 * converts the result back. Conversions are int<->ptr and int-width casts;
 * wasm pointers fit in 32 bits so the i64 handles Zan uses are safe to
 * truncate. */
static void w32_build_adapter_into(zan_irgen_t *g, LLVMValueRef fn,
                                   LLVMValueRef real, LLVMTypeRef lft) {
    LLVMTypeRef src_ft = LLVMGlobalGetValueType(fn);
    unsigned nparams = LLVMCountParamTypes(src_ft);
    if (nparams > 8 || LLVMCountParamTypes(lft) != nparams) return;
    LLVMTypeRef sps[8], lps[8];
    LLVMGetParamTypes(src_ft, sps);
    LLVMGetParamTypes(lft, lps);
    LLVMBasicBlockRef bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
    LLVMBuilderRef b = LLVMCreateBuilderInContext(g->ctx);
    LLVMPositionBuilderAtEnd(b, bb);
    LLVMValueRef args[8];
    for (unsigned p = 0; p < nparams; p++) {
        LLVMValueRef a = LLVMGetParam(fn, p);
        LLVMTypeKind sk = LLVMGetTypeKind(sps[p]);
        LLVMTypeKind lk = LLVMGetTypeKind(lps[p]);
        if (sps[p] == lps[p]) {
            args[p] = a;
        } else if (sk == LLVMIntegerTypeKind && lk == LLVMPointerTypeKind) {
            args[p] = LLVMBuildIntToPtr(b, a, lps[p], "");
        } else if (sk == LLVMPointerTypeKind && lk == LLVMIntegerTypeKind) {
            args[p] = LLVMBuildPtrToInt(b, a, lps[p], "");
        } else if (sk == LLVMIntegerTypeKind && lk == LLVMIntegerTypeKind) {
            args[p] = LLVMBuildIntCast2(b, a, lps[p], 1, "");
        } else if (sk == LLVMPointerTypeKind && lk == LLVMPointerTypeKind) {
            args[p] = LLVMBuildPointerCast(b, a, lps[p], "");
        } else {
            args[p] = a;
        }
    }
    LLVMValueRef rv = zan_call2(b, lft, real, args, nparams, "");
    LLVMTypeRef srt = LLVMGetReturnType(src_ft);
    LLVMTypeRef lrt = LLVMGetReturnType(lft);
    if (LLVMGetTypeKind(srt) == LLVMVoidTypeKind) {
        LLVMBuildRetVoid(b);
    } else if (srt == lrt) {
        LLVMBuildRet(b, rv);
    } else if (LLVMGetTypeKind(lrt) == LLVMVoidTypeKind) {
        LLVMBuildRet(b, LLVMConstNull(srt));
    } else if (LLVMGetTypeKind(lrt) == LLVMPointerTypeKind &&
               LLVMGetTypeKind(srt) == LLVMIntegerTypeKind) {
        LLVMBuildRet(b, LLVMBuildPtrToInt(b, rv, srt, ""));
    } else if (LLVMGetTypeKind(lrt) == LLVMIntegerTypeKind &&
               LLVMGetTypeKind(srt) == LLVMIntegerTypeKind) {
        LLVMBuildRet(b, LLVMBuildIntCast2(b, rv, srt, 1, ""));
    } else if (LLVMGetTypeKind(lrt) == LLVMIntegerTypeKind &&
               LLVMGetTypeKind(srt) == LLVMPointerTypeKind) {
        LLVMBuildRet(b, LLVMBuildIntToPtr(b, rv, srt, ""));
    } else {
        LLVMBuildRet(b, rv);
    }
    LLVMDisposeBuilder(b);
}

static LLVMValueRef w32_build_adapter(zan_irgen_t *g, const char *name,
                                      LLVMTypeRef src_ft, LLVMValueRef real,
                                      LLVMTypeRef lft) {
    if (LLVMCountParamTypes(src_ft) != LLVMCountParamTypes(lft) ||
        LLVMCountParamTypes(src_ft) > 8)
        return NULL;
    LLVMValueRef fn = LLVMAddFunction(g->mod, name, src_ft);
    LLVMSetLinkage(fn, LLVMInternalLinkage);
    w32_build_adapter_into(g, fn, real, lft);
    return fn;
}

zan_status_t zan_irgen_write_obj(zan_irgen_t *g, const char *path) {
    /* wasm32: libc size_t/long are 32-bit but the IR declares these libc
     * functions with i64 sizes (Zan int). Redirect the declarations to the
     * zan_w32_* wrappers (src/runtime/rt_wasm.c, shipped pre-compiled in the
     * wasm32 sysroot) whose signatures take i64 and forward to the real
     * 32-bit libc, so wasm's strict signature checking is satisfied. */
    if (strncmp(g->target_triple, "wasm32", 6) == 0) {
        /* wasi-libc's _start calls __main_argc_argv (the name clang gives a
         * two-argument main on wasm), not "main". */
        LLVMValueRef mainf = LLVMGetNamedFunction(g->mod, "main");
        if (mainf && LLVMCountBasicBlocks(mainf) > 0)
            LLVMSetValueName2(mainf, "__main_argc_argv",
                              strlen("__main_argc_argv"));
        /* Variadic snprintf cannot be adapted in IR (varargs cannot be
         * forwarded); route it to the C wrapper in rt_wasm.c instead. */
        {
            LLVMValueRef f = LLVMGetNamedFunction(g->mod, "snprintf");
            if (f && LLVMCountBasicBlocks(f) == 0)
                LLVMSetValueName2(f, "zan_w32_snprintf",
                                  strlen("zan_w32_snprintf"));
        }
        /* Zan IR declares libc functions with 64-bit ints (Zan int is i64,
         * and pointers passed through Zan `int` handles are i64 too), but
         * wasm32's size_t/long/pointers are 32-bit and wasm enforces exact
         * call signatures. For each such declaration, turn it into a thin
         * adapter that truncates/extends the values and calls the real
         * 32-bit libc function.
         * Signature codes: p=pointer, i=i32, j=i64, s=size_t(i32),
         * v=void; first char is the return, the rest are the params. */
        static const struct { const char *name; const char *sig; } w32adapt[] = {
            { "malloc", "ps" },      { "calloc", "pss" },
            { "realloc", "pps" },    { "free", "vp" },
            { "strlen", "sp" },      { "memcpy", "ppps" },
            { "memset", "ppis" },    { "memcmp", "ipps" },
            { "strcat", "ppp" },     { "strcpy", "ppp" },
            { "strncpy", "ppps" },   { "strcmp", "ipp" },
            { "strncmp", "ipps" },   { "strrchr", "ppi" },
            { "strstr", "ppp" },     { "atoi", "ip" },
            { "fopen", "ppp" },      { "fclose", "ip" },
            { "fgetc", "ip" },       { "fputc", "iip" },
            { "fputs", "ipp" },      { "fgets", "ppip" },
            { "fread", "spssp" },    { "fwrite", "spssp" },
            { "fseek", "ipii" },     { "ftell", "ip" },
            { "fflush", "ip" },      { "remove", "ip" },
            { "rename", "ipp" },     { "chdir", "ip" },
            { "getcwd", "pps" },     { "mkdir", "ipi" },
            { "rmdir", "ip" },       { "opendir", "pp" },
            { "readdir", "pp" },     { "closedir", "ip" },
            { "time", "jp" },        { "poll", "ipii" },
            { NULL, NULL }
        };
        LLVMTypeRef w_i32 = LLVMInt32TypeInContext(g->ctx);
        LLVMTypeRef w_i64 = LLVMInt64TypeInContext(g->ctx);
        LLVMTypeRef w_ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
        for (int i = 0; w32adapt[i].name; i++) {
            LLVMValueRef decl = LLVMGetNamedFunction(g->mod, w32adapt[i].name);
            if (!decl || LLVMCountBasicBlocks(decl) > 0) continue;
            LLVMTypeRef dft = LLVMGlobalGetValueType(decl);
            if (LLVMIsFunctionVarArg(dft)) continue;
            const char *sig = w32adapt[i].sig;
            int nparams = (int)strlen(sig) - 1;
            if (nparams > 8 || (int)LLVMCountParamTypes(dft) != nparams)
                continue;
            /* the real libc function's type */
            LLVMTypeRef lps[8];
            for (int p = 0; p < nparams; p++) {
                char c = sig[p + 1];
                lps[p] = (c == 'p') ? w_ptr : (c == 'j') ? w_i64 : w_i32;
            }
            char rc = sig[0];
            LLVMTypeRef lrt = (rc == 'v') ? LLVMVoidTypeInContext(g->ctx)
                              : (rc == 'p') ? w_ptr
                              : (rc == 'j') ? w_i64 : w_i32;
            LLVMTypeRef lft = LLVMFunctionType(lrt, lps, (unsigned)nparams, 0);
            /* rename the declaration; re-add the real libc function */
            char an[80];
            snprintf(an, sizeof(an), "__zan_w32ir_%s", w32adapt[i].name);
            LLVMSetValueName2(decl, an, strlen(an));
            LLVMValueRef real = LLVMAddFunction(g->mod, w32adapt[i].name, lft);
            /* Different call sites may use different signatures for the same
             * function (Zan reuses an existing declaration whatever its
             * type), so rewrite every direct call: give each distinct
             * call-site type its own adapter that converts the values and
             * calls the real 32-bit libc function. */
            LLVMTypeRef cts[8];
            LLVMValueRef cad[8];
            int ncts = 0;
            LLVMUseRef use = LLVMGetFirstUse(decl);
            while (use) {
                LLVMUseRef next = LLVMGetNextUse(use);
                LLVMValueRef user = LLVMGetUser(use);
                if (LLVMIsACallInst(user) &&
                    LLVMGetCalledValue(user) == decl) {
                    LLVMTypeRef cft = LLVMGetCalledFunctionType(user);
                    if (cft == lft) {
                        /* already the 32-bit ABI: call libc directly */
                        LLVMSetOperand(user,
                                       LLVMGetNumOperands(user) - 1, real);
                    } else if (!LLVMIsFunctionVarArg(cft) &&
                               (int)LLVMCountParamTypes(cft) == nparams) {
                        LLVMValueRef ad = NULL;
                        for (int k = 0; k < ncts; k++) {
                            if (cts[k] == cft) { ad = cad[k]; break; }
                        }
                        if (!ad) {
                            char nm[96];
                            snprintf(nm, sizeof(nm), "%s.v%d", an, ncts);
                            ad = w32_build_adapter(g, nm, cft, real, lft);
                            if (ad && ncts < 8) {
                                cts[ncts] = cft;
                                cad[ncts] = ad;
                                ncts++;
                            }
                        }
                        if (ad)
                            LLVMSetOperand(user,
                                           LLVMGetNumOperands(user) - 1, ad);
                    }
                }
                use = next;
            }
            /* Any remaining (indirect) uses go through the renamed
             * declaration itself: define it as an adapter too. */
            if (LLVMGetFirstUse(decl) && dft != lft) {
                w32_build_adapter_into(g, decl, real, lft);
                LLVMSetLinkage(decl, LLVMInternalLinkage);
            }
        }
    }
    /* Initialize every target family the build links (see CMakeLists.txt), so
     * zanc can emit code for the host regardless of architecture (e.g. arm64
     * macOS) as well as cross-compile to the advertised targets. */
    LLVMInitializeX86TargetInfo();
    LLVMInitializeX86Target();
    LLVMInitializeX86TargetMC();
    LLVMInitializeX86AsmParser();
    LLVMInitializeX86AsmPrinter();

    LLVMInitializeAArch64TargetInfo();
    LLVMInitializeAArch64Target();
    LLVMInitializeAArch64TargetMC();
    LLVMInitializeAArch64AsmParser();
    LLVMInitializeAArch64AsmPrinter();

#ifdef ZAN_HAVE_LLVM_ARM
    LLVMInitializeARMTargetInfo();
    LLVMInitializeARMTarget();
    LLVMInitializeARMTargetMC();
    LLVMInitializeARMAsmParser();
    LLVMInitializeARMAsmPrinter();
#endif

#ifdef ZAN_HAVE_LLVM_WEBASSEMBLY
    LLVMInitializeWebAssemblyTargetInfo();
    LLVMInitializeWebAssemblyTarget();
    LLVMInitializeWebAssemblyTargetMC();
    LLVMInitializeWebAssemblyAsmParser();
    LLVMInitializeWebAssemblyAsmPrinter();
#endif

#ifdef ZAN_HAVE_LLVM_RISCV
    LLVMInitializeRISCVTargetInfo();
    LLVMInitializeRISCVTarget();
    LLVMInitializeRISCVTargetMC();
    LLVMInitializeRISCVAsmParser();
    LLVMInitializeRISCVAsmPrinter();
#endif

    char *triple;
    if (g->target_triple[0]) {
        /* Cross-compilation: emit for the requested target triple verbatim
         * (e.g. x86_64-unknown-linux-musl). The X86/AArch64 backends produce
         * the right object format (ELF/Mach-O/COFF) from the triple's OS. */
        triple = LLVMCreateMessage(g->target_triple);
    } else {
        triple = LLVMGetDefaultTargetTriple();
#ifdef _WIN32
        /* Emit GNU-ABI (MinGW) objects so the produced code links against the
         * bundled ld.lld + mingw-w64 runtime, keeping zanc self-contained:
         * building an .exe needs only zan, no external clang / MSVC / Windows
         * SDK. Preserve the host architecture prefix and swap the vendor/abi. */
        {
            const char *dash = strchr(triple, '-');
            size_t archlen = dash ? (size_t)(dash - triple) : strlen(triple);
            char gnu[128];
            if (archlen > sizeof(gnu) - 20) archlen = sizeof(gnu) - 20;
            memcpy(gnu, triple, archlen);
            snprintf(gnu + archlen, sizeof(gnu) - archlen, "-w64-windows-gnu");
            LLVMDisposeMessage(triple);
            triple = LLVMCreateMessage(gnu);
        }
#endif
    }
    LLVMTargetRef target;
    char *error = NULL;

    if (LLVMGetTargetFromTriple(triple, &target, &error)) {
        zan_diag_emit(g->diag, DIAG_ERROR, zan_loc(0, 0, 0, 0),
                      "failed to get target: %s", error);
        LLVMDisposeMessage(error);
        LLVMDisposeMessage(triple);
        return ZAN_ERROR;
    }

    /* RISC-V: match the RV64GC / lp64d ABI the bundled musl sysroot uses
     * (the ABI must be recorded as a module flag for the backend to lower
     * doubles into FP registers). */
    const char *tm_cpu = "generic";
    const char *tm_features = "";
    if (strncmp(triple, "riscv64", 7) == 0) {
        tm_cpu = "generic-rv64";
        tm_features = "+m,+a,+f,+d,+c";
        LLVMAddModuleFlag(g->mod, LLVMModuleFlagBehaviorError,
                          "target-abi", strlen("target-abi"),
                          LLVMValueAsMetadata(LLVMMDStringInContext(
                              g->ctx, "lp64d", 5)));
    }
    LLVMTargetMachineRef tm = LLVMCreateTargetMachine(
        target, triple, tm_cpu, tm_features,
        LLVMCodeGenLevelDefault, LLVMRelocPIC, LLVMCodeModelDefault);

    LLVMSetTarget(g->mod, triple);
    LLVMTargetDataRef dl = LLVMCreateTargetDataLayout(tm);
    char *dl_str = LLVMCopyStringRepOfTargetData(dl);
    LLVMSetDataLayout(g->mod, dl_str);
    LLVMDisposeMessage(dl_str);
    LLVMDisposeTargetData(dl);

    if (LLVMTargetMachineEmitToFile(tm, g->mod, (char *)path,
                                     LLVMObjectFile, &error)) {
        zan_diag_emit(g->diag, DIAG_ERROR, zan_loc(0, 0, 0, 0),
                      "failed to emit object file: %s", error);
        LLVMDisposeMessage(error);
        LLVMDisposeTargetMachine(tm);
        LLVMDisposeMessage(triple);
        return ZAN_ERROR;
    }

    LLVMDisposeTargetMachine(tm);
    LLVMDisposeMessage(triple);
    return ZAN_OK;
}
