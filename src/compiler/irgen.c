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
        const char *sfile = leak_site_file(g, expr->loc);
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
#include "irgen_async.c"
#include "irgen_stmt.c"
#include "irgen_emit.c"
