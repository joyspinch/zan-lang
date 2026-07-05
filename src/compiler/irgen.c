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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Maximum number of distinct `new` allocation sites tracked for per-site
 * leak reporting. Sites beyond this share the last bucket. */
#define ZAN_MAX_LEAK_SITES 4096

/* ---- initialization ---- */

zan_status_t zan_irgen_init(zan_irgen_t *g, zan_arena_t *arena,
                            zan_diag_t *diag, zan_binder_t *binder,
                            const char *module_name) {
    memset(g, 0, sizeof(*g));
    g->arena = arena;
    g->diag = diag;
    g->binder = binder;

    g->ctx = LLVMContextCreate();
    g->mod = LLVMModuleCreateWithNameInContext(module_name, g->ctx);
    g->builder = LLVMCreateBuilderInContext(g->ctx);

    /* runtime-diagnostics defaults */
    g->src_file = module_name;
    g->runtime_checks = true;
    g->check_leaks = false;

    /* net live-allocation counter for leak detection (internal global) */
    g->g_live = LLVMAddGlobal(g->mod, LLVMInt64TypeInContext(g->ctx), "__zan_live");
    LLVMSetInitializer(g->g_live, LLVMConstInt(LLVMInt64TypeInContext(g->ctx), 0, 0));
    LLVMSetLinkage(g->g_live, LLVMInternalLinkage);

    /* per-allocation-site leak tracking: fixed-capacity parallel tables
     * mapping a site index -> (live count, "file:line:col" descriptor).
     * Each `new` expression is assigned a stable site index. */
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
    LLVMBuildCall2(g->builder, printf_type, printf_fn, args, 2, "");
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
    LLVMBuildCall2(g->builder, printf_type, printf_fn, iargs, 2, "");
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
    LLVMBuildCall2(g->builder, printf_type, printf_fn, dargs, 2, "");
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

    /* int atexit(void(*)(void)) — used to schedule the leak report */
    LLVMTypeRef void_fn_type = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), NULL, 0, 0);
    LLVMTypeRef void_fn_ptr = LLVMPointerType(void_fn_type, 0);
    LLVMTypeRef atexit_args[] = { void_fn_ptr };
    g->atexit_type = LLVMFunctionType(i32, atexit_args, 1, 0);
    g->fn_atexit = LLVMAddFunction(g->mod, "atexit", g->atexit_type);

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

    /* implement zan_rt_retain: increment refcount at offset -8 */
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
        /* refcount is at (int64_t*)(obj - 8) */
        LLVMValueRef neg8 = LLVMConstInt(i64, (uint64_t)-8, 1);
        LLVMValueRef rc_ptr = LLVMBuildGEP2(g->builder, LLVMInt8TypeInContext(g->ctx), obj, &neg8, 1, "rcptr");
        LLVMValueRef rc_iptr = LLVMBuildBitCast(g->builder, rc_ptr,
            LLVMPointerType(i64, 0), "rciptr");
        LLVMValueRef rc = LLVMBuildLoad2(g->builder, i64, rc_iptr, "rc");
        LLVMValueRef rc1 = LLVMBuildAdd(g->builder, rc, LLVMConstInt(i64, 1, 0), "rc1");
        LLVMBuildStore(g->builder, rc1, rc_iptr);
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
        LLVMValueRef rc = LLVMBuildLoad2(g->builder, i64, rc_iptr, "rc");
        LLVMValueRef rc1 = LLVMBuildSub(g->builder, rc, LLVMConstInt(i64, 1, 0), "rc1");
        LLVMBuildStore(g->builder, rc1, rc_iptr);
        /* if rc1 == 0, free the object (16-byte header precedes obj) */
        LLVMValueRef is_zero = LLVMBuildICmp(g->builder, LLVMIntEQ, rc1,
            LLVMConstInt(i64, 0, 0), "iszero");
        LLVMBasicBlockRef free_bb = LLVMAppendBasicBlockInContext(g->ctx, g->rt_release, "dofree");
        LLVMBuildCondBr(g->builder, is_zero, free_bb, ret_bb);
        LLVMPositionBuilderAtEnd(g->builder, free_bb);
        /* read the allocation-site index stored at obj-8 */
        LLVMValueRef site_ptr = LLVMBuildGEP2(g->builder, LLVMInt8TypeInContext(g->ctx), obj, &neg8, 1, "sptr");
        LLVMValueRef site_iptr = LLVMBuildBitCast(g->builder, site_ptr, LLVMPointerType(i64, 0), "siptr");
        LLVMValueRef site = LLVMBuildLoad2(g->builder, i64, site_iptr, "site");
        /* free(obj - 16) to include the header */
        LLVMValueRef header_ptr = LLVMBuildGEP2(g->builder, LLVMInt8TypeInContext(g->ctx), obj, &neg16, 1, "hdr");
        LLVMTypeRef free_fn_type = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx),
            (LLVMTypeRef[]){ i8ptr }, 1, 0);
        LLVMBuildCall2(g->builder, free_fn_type, g->fn_free, &header_ptr, 1, "");
        /* leak tracking: one fewer live object, and one fewer at this site */
        {
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
        LLVMValueRef raw = LLVMBuildCall2(g->builder, malloc_fn_type, g->fn_malloc, &total, 1, "raw");
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
        /* leak tracking: total + per-site count, and record the site name */
        {
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

    /* void __zan_report_leaks(void): at program exit, if any ARC object is
     * still live, print a summary line and then a per-allocation-site
     * breakdown ("file:line:col"). Scheduled via atexit when --check-leaks. */
    {
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

        LLVMPositionBuilderAtEnd(g->builder, bb);
        LLVMValueRef live = LLVMBuildLoad2(g->builder, i64, g->g_live, "live");
        LLVMValueRef leaked = LLVMBuildICmp(g->builder, LLVMIntSGT, live,
            LLVMConstInt(i64, 0, 0), "leaked");
        LLVMBuildCondBr(g->builder, leaked, leak_bb, done_bb);

        LLVMPositionBuilderAtEnd(g->builder, leak_bb);
        LLVMValueRef msg = LLVMBuildGlobalStringPtr(g->builder,
            "zan: memory leak detected: %lld object(s) still reachable at exit\n", "leak_fmt");
        LLVMValueRef pargs[] = { msg, live };
        LLVMBuildCall2(g->builder, g->printf_type, g->fn_printf, pargs, 2, "");
        LLVMBuildBr(g->builder, head_bb);

        /* iterate the site buckets, printing those with a positive live count */
        LLVMPositionBuilderAtEnd(g->builder, head_bb);
        LLVMValueRef idx = LLVMBuildPhi(g->builder, i64, "i");
        LLVMValueRef in_range = LLVMBuildICmp(g->builder, LLVMIntSLT, idx,
            LLVMConstInt(i64, ZAN_MAX_LEAK_SITES, 0), "inrange");
        LLVMBuildCondBr(g->builder, in_range, body_bb, done_bb);

        LLVMPositionBuilderAtEnd(g->builder, body_bb);
        LLVMValueRef z32 = LLVMConstInt(i32t, 0, 0);
        LLVMValueRef cidx[2] = { z32, idx };
        LLVMValueRef sc_ptr = LLVMBuildGEP2(g->builder, g->site_live_type, g->g_site_live, cidx, 2, "scptr");
        LLVMValueRef sc = LLVMBuildLoad2(g->builder, i64, sc_ptr, "sc");
        LLVMValueRef has = LLVMBuildICmp(g->builder, LLVMIntSGT, sc, LLVMConstInt(i64, 0, 0), "has");
        LLVMBuildCondBr(g->builder, has, print_bb, next_bb);

        LLVMPositionBuilderAtEnd(g->builder, print_bb);
        LLVMValueRef nm_ptr = LLVMBuildGEP2(g->builder, g->site_names_type, g->g_site_names, cidx, 2, "nmptr");
        LLVMValueRef nm = LLVMBuildLoad2(g->builder, i8p, nm_ptr, "nm");
        LLVMValueRef dmsg = LLVMBuildGlobalStringPtr(g->builder,
            "  %lld object(s) leaked, allocated at %s\n", "leak_site_fmt");
        LLVMValueRef dargs[] = { dmsg, sc, nm };
        LLVMBuildCall2(g->builder, g->printf_type, g->fn_printf, dargs, 3, "");
        LLVMBuildBr(g->builder, next_bb);

        LLVMPositionBuilderAtEnd(g->builder, next_bb);
        LLVMValueRef idx1 = LLVMBuildAdd(g->builder, idx, LLVMConstInt(i64, 1, 0), "i.next");
        LLVMBuildBr(g->builder, head_bb);

        /* phi: start at 0 from leak_bb, then idx+1 from next_bb */
        LLVMValueRef phi_vals[2] = { LLVMConstInt(i64, 0, 0), idx1 };
        LLVMBasicBlockRef phi_bbs[2] = { leak_bb, next_bb };
        LLVMAddIncoming(idx, phi_vals, phi_bbs, 2);

        LLVMPositionBuilderAtEnd(g->builder, done_bb);
        LLVMBuildRetVoid(g->builder);
    }

    return ZAN_OK;
}

void zan_irgen_destroy(zan_irgen_t *g) {
    if (g->builder) LLVMDisposeBuilder(g->builder);
    if (g->mod) LLVMDisposeModule(g->mod);
    if (g->ctx) LLVMContextDispose(g->ctx);
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
    case TYPE_NINT:   return LLVMInt64TypeInContext(g->ctx);
    case TYPE_FLOAT:  return LLVMFloatTypeInContext(g->ctx);
    case TYPE_DOUBLE: return LLVMDoubleTypeInContext(g->ctx);
    case TYPE_CHAR:   return LLVMInt64TypeInContext(g->ctx);
    case TYPE_STRING: return LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    case TYPE_OBJECT: return LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    case TYPE_ENUM:
        return LLVMInt64TypeInContext(g->ctx);
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

static LLVMTypeRef get_struct_llvm_type(zan_irgen_t *g, zan_symbol_t *sym) {
    for (int i = 0; i < g->struct_type_count; i++) {
        if (g->struct_types[i].sym == sym) return g->struct_types[i].llvm_type;
    }
    return NULL;
}

static int get_field_index(zan_symbol_t *type_sym, zan_istr_t field_name) {
    int idx = 0;
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

    LLVMTypeRef *field_types = (LLVMTypeRef *)calloc((size_t)field_count, sizeof(LLVMTypeRef));
    int fi = 0;
    for (int i = 0; i < sym->member_count; i++) {
        if (sym->members[i]->kind == SYM_FIELD ||
            sym->members[i]->kind == SYM_PROPERTY) {
            field_types[fi++] = map_type(g, sym->members[i]->type);
        }
    }

    LLVMStructSetBody(st, field_types, (unsigned)field_count, 0);
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
} local_var_t;

typedef struct {
    local_var_t vars[MAX_LOCALS];
    int count;
} local_scope_t;

static local_scope_t *local_scope_new(zan_arena_t *arena) {
    local_scope_t *s = (local_scope_t *)zan_arena_alloc(arena, sizeof(local_scope_t));
    s->count = 0;
    return s;
}

static void local_add(local_scope_t *scope, zan_istr_t name, LLVMValueRef alloca, zan_type_t *type) {
    if (scope->count < MAX_LOCALS) {
        scope->vars[scope->count].name = name;
        scope->vars[scope->count].alloca = alloca;
        scope->vars[scope->count].type = type;
        scope->count++;
    }
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

/* ---- expression codegen ---- */

static LLVMValueRef emit_expr(zan_irgen_t *g, zan_ast_node_t *expr, local_scope_t *locals);
static void emit_stmt(zan_irgen_t *g, zan_ast_node_t *stmt, local_scope_t *locals);

/* Resolve the declared type of an `obj.field` member access so that element
 * indexing on struct/class array fields (e.g. `b.data[i]`) can determine the
 * element LLVM type. Returns NULL when the field/type cannot be resolved. */
static zan_type_t *member_access_field_type(local_scope_t *locals, zan_ast_node_t *member) {
    if (!member || member->kind != AST_MEMBER_ACCESS) return NULL;
    zan_ast_node_t *obj = member->member.object;
    if (obj->kind == AST_IDENTIFIER) {
        local_var_t *l = local_find(locals, obj->ident.name);
        if (l && l->type && l->type->sym) {
            zan_symbol_t *fsym = get_field_sym(l->type->sym, member->member.name);
            if (fsym) return fsym->type;
        }
    }
    return NULL;
}

/* Best-effort static test for whether an expression yields a `string` value.
 * Used to route `+` to concatenation and `==`/`!=` to strcmp rather than raw
 * pointer arithmetic/comparison. Reference (class) values are NOT strings. */
static bool is_string_expr(zan_irgen_t *g, zan_ast_node_t *e, local_scope_t *locals) {
    if (!e) return false;
    switch (e->kind) {
    case AST_STRING_LITERAL:
    case AST_STRING_INTERP:
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
        zan_type_t *ft = member_access_field_type(locals, e);
        return ft && ft->kind == TYPE_STRING;
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
                (m.len == 7 && memcmp(m.str, "Replace", 7) == 0))
                return true;
        }
        return false;
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
    case AST_MEMBER_ACCESS: {
        zan_type_t *ot = infer_expr_type(g, e->member.object, locals);
        if (ot && ot->sym) {
            zan_symbol_t *fs = get_field_sym(ot->sym, e->member.name);
            if (fs) return fs->type;
        }
        return NULL;
    }
    case AST_INDEX:
        return container_elem_type(infer_expr_type(g, e->index.object, locals));
    default:
        return NULL;
    }
}

/* Class/struct symbol of an expression's static type, or NULL. */
static zan_symbol_t *expr_class_sym(zan_irgen_t *g, zan_ast_node_t *e,
                                    local_scope_t *locals) {
    zan_type_t *t = infer_expr_type(g, e, locals);
    if (t && (t->kind == TYPE_CLASS || t->kind == TYPE_STRUCT)) return t->sym;
    return NULL;
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
    LLVMBuildCall2(g->builder, g->printf_type, g->fn_printf, pargs, 2, "");
    LLVMValueRef code = LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 70, 0);
    LLVMBuildCall2(g->builder, g->exit_type, g->fn_exit, &code, 1, "");
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

/* Emit `a + b` for two string (i8*) operands as a heap-allocated concatenation:
 * malloc(strlen(a)+strlen(b)+1); strcpy; strcat. Returns the new buffer ptr. */
static LLVMValueRef emit_str_concat(zan_irgen_t *g, LLVMValueRef a, LLVMValueRef b) {
    LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef strlen_type = LLVMFunctionType(i64t, (LLVMTypeRef[]){ i8ptr }, 1, 0);
    LLVMValueRef la = LLVMBuildCall2(g->builder, strlen_type, g->fn_strlen, &a, 1, "cla");
    LLVMValueRef lb = LLVMBuildCall2(g->builder, strlen_type, g->fn_strlen, &b, 1, "clb");
    LLVMValueRef tot = LLVMBuildAdd(g->builder, la, lb, "ct");
    tot = LLVMBuildAdd(g->builder, tot, LLVMConstInt(i64t, 1, 0), "ct1");
    LLVMValueRef buf = LLVMBuildCall2(g->builder,
        LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i64t }, 1, 0),
        g->fn_malloc, &tot, 1, "cbuf");
    LLVMValueRef cp[] = { buf, a };
    LLVMBuildCall2(g->builder,
        LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0),
        g->fn_strcpy, cp, 2, "");
    LLVMValueRef ct[] = { buf, b };
    LLVMBuildCall2(g->builder,
        LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0),
        g->fn_strcat, ct, 2, "");
    return buf;
}

static LLVMValueRef emit_expr(zan_irgen_t *g, zan_ast_node_t *expr, local_scope_t *locals) {
    if (!expr) return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);

    switch (expr->kind) {
    case AST_INT_LITERAL:
        return LLVMConstInt(LLVMInt64TypeInContext(g->ctx), (uint64_t)expr->int_val, 1);

    case AST_FLOAT_LITERAL:
        return LLVMConstReal(LLVMDoubleTypeInContext(g->ctx), expr->float_val);

    case AST_STRING_LITERAL:
        return LLVMBuildGlobalStringPtr(g->builder, expr->str_val.str, "str");

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
        /* return 0 for unresolved — error was reported in checker */
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

        LLVMValueRef left = emit_expr(g, expr->binary.left, locals);
        LLVMValueRef right = emit_expr(g, expr->binary.right, locals);

        bool both_ptr =
            LLVMGetTypeKind(LLVMTypeOf(left)) == LLVMPointerTypeKind &&
            LLVMGetTypeKind(LLVMTypeOf(right)) == LLVMPointerTypeKind;
        bool str_operand = is_string_expr(g, expr->binary.left, locals) ||
                           is_string_expr(g, expr->binary.right, locals);

        /* string concatenation: `a + b` on string operands */
        if (expr->binary.op == TK_PLUS && both_ptr && str_operand) {
            return emit_str_concat(g, left, right);
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
            LLVMValueRef r = LLVMBuildCall2(g->builder,
                LLVMFunctionType(i32t, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0),
                g->fn_strcmp, cmp_args, 2, "scmp");
            return LLVMBuildICmp(g->builder,
                expr->binary.op == TK_EQ_EQ ? LLVMIntEQ : LLVMIntNE,
                r, LLVMConstInt(i32t, 0, 0), "seq");
        }

        LLVMTypeRef left_type = LLVMTypeOf(left);
        bool is_float = (LLVMGetTypeKind(left_type) == LLVMDoubleTypeKind ||
                         LLVMGetTypeKind(left_type) == LLVMFloatTypeKind);

        /* reconcile mixed-width integer operands (e.g. i32 int vs i64 length) */
        coerce_int_pair(g, &left, &right);

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
            return LLVMBuildSDiv(g->builder, left, right, "div");
        case TK_PERCENT:
            if (is_float) return LLVMBuildFRem(g->builder, left, right, "rem");
            {
                LLVMValueRef zero = LLVMConstInt(LLVMTypeOf(right), 0, 0);
                LLVMValueRef is_zero = LLVMBuildICmp(g->builder, LLVMIntEQ, right, zero, "remz");
                emit_runtime_check(g, is_zero, expr->loc, "division by zero (modulo)");
            }
            return LLVMBuildSRem(g->builder, left, right, "rem");
        case TK_AMP:
            return LLVMBuildAnd(g->builder, left, right, "and");
        case TK_PIPE:
            return LLVMBuildOr(g->builder, left, right, "or");
        case TK_CARET:
            return LLVMBuildXor(g->builder, left, right, "xor");
        case TK_LESS_LESS:
            return LLVMBuildShl(g->builder, left, right, "shl");
        case TK_GREATER_GREATER:
            return LLVMBuildAShr(g->builder, left, right, "shr");
        case TK_EQ_EQ:
            return is_float ? LLVMBuildFCmp(g->builder, LLVMRealOEQ, left, right, "eq")
                            : LLVMBuildICmp(g->builder, LLVMIntEQ, left, right, "eq");
        case TK_BANG_EQ:
            return is_float ? LLVMBuildFCmp(g->builder, LLVMRealONE, left, right, "ne")
                            : LLVMBuildICmp(g->builder, LLVMIntNE, left, right, "ne");
        case TK_LESS:
            return is_float ? LLVMBuildFCmp(g->builder, LLVMRealOLT, left, right, "lt")
                            : LLVMBuildICmp(g->builder, LLVMIntSLT, left, right, "lt");
        case TK_GREATER:
            return is_float ? LLVMBuildFCmp(g->builder, LLVMRealOGT, left, right, "gt")
                            : LLVMBuildICmp(g->builder, LLVMIntSGT, left, right, "gt");
        case TK_LESS_EQ:
            return is_float ? LLVMBuildFCmp(g->builder, LLVMRealOLE, left, right, "le")
                            : LLVMBuildICmp(g->builder, LLVMIntSLE, left, right, "le");
        case TK_GREATER_EQ:
            return is_float ? LLVMBuildFCmp(g->builder, LLVMRealOGE, left, right, "ge")
                            : LLVMBuildICmp(g->builder, LLVMIntSGE, left, right, "ge");
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
        case TK_BANG:
            return LLVMBuildNot(g->builder, operand, "not");
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
            if (local) {
                LLVMBuildStore(g->builder, right, local->alloca);
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
                        LLVMBuildStore(g->builder, right, fptr);
                    }
                }
            }
        } else if (expr->binary.left->kind == AST_INDEX) {
            /* arr[i] = value */
            zan_ast_node_t *arr_expr = expr->binary.left->index.object;
            if (arr_expr->kind == AST_IDENTIFIER) {
                local_var_t *local = local_find(locals, arr_expr->ident.name);
                if (local && local->type && local->type->name.len == 4 &&
                    memcmp(local->type->name.str, "List", 4) == 0) {
                    /* list[i] = value — store into the list's i64 data slots */
                    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                    LLVMValueRef raw = LLVMBuildLoad2(g->builder, i8ptr, local->alloca, "lraw");
                    LLVMValueRef list_ptr = LLVMBuildBitCast(g->builder, raw,
                        LLVMPointerType(g->list_struct_type, 0), "lptr");
                    LLVMValueRef data_field = LLVMBuildStructGEP2(g->builder,
                        g->list_struct_type, list_ptr, 2, "df");
                    LLVMValueRef data = LLVMBuildLoad2(g->builder,
                        LLVMPointerType(i64, 0), data_field, "data");
                    LLVMValueRef idx = emit_expr(g, expr->binary.left->index.index, locals);
                    if (LLVMGetTypeKind(LLVMTypeOf(idx)) == LLVMIntegerTypeKind &&
                        LLVMGetIntTypeWidth(LLVMTypeOf(idx)) < 64) {
                        idx = LLVMBuildSExt(g->builder, idx, i64, "ext");
                    }
                    LLVMValueRef elem_ptr = LLVMBuildGEP2(g->builder, i64, data, &idx, 1, "ep");
                    LLVMValueRef v = right;
                    LLVMTypeKind vk = LLVMGetTypeKind(LLVMTypeOf(v));
                    if (vk == LLVMPointerTypeKind) {
                        v = LLVMBuildPtrToInt(g->builder, v, i64, "p2i");
                    } else if (vk == LLVMIntegerTypeKind &&
                               LLVMGetIntTypeWidth(LLVMTypeOf(v)) < 64) {
                        v = LLVMBuildSExt(g->builder, v, i64, "sx");
                    }
                    LLVMBuildStore(g->builder, v, elem_ptr);
                } else if (local) {
                    LLVMValueRef arr_ptr = LLVMBuildLoad2(g->builder,
                        LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0),
                        local->alloca, "arrload");
                    LLVMValueRef idx = emit_expr(g, expr->binary.left->index.index, locals);
                    LLVMTypeRef elem_llvm = LLVMInt32TypeInContext(g->ctx);
                    if (local->type && local->type->element_type) {
                        elem_llvm = map_type(g, local->type->element_type);
                    }
                    LLVMValueRef elem_ptr = LLVMBuildGEP2(g->builder, elem_llvm, arr_ptr, &idx, 1, "eidx");
                    LLVMBuildStore(g->builder, right, elem_ptr);
                } else if (g->current_type_sym) {
                    /* implicit this.field[i] = value */
                    zan_symbol_t *fsym = get_field_sym(g->current_type_sym, arr_expr->ident.name);
                    if (fsym) {
                        LLVMValueRef arr_ptr = emit_expr(g, arr_expr, locals);
                        LLVMValueRef idx = emit_expr(g, expr->binary.left->index.index, locals);
                        LLVMTypeRef elem_llvm = LLVMInt32TypeInContext(g->ctx);
                        if (fsym->type && fsym->type->element_type) {
                            elem_llvm = map_type(g, fsym->type->element_type);
                        }
                        LLVMValueRef elem_ptr = LLVMBuildGEP2(g->builder, elem_llvm, arr_ptr, &idx, 1, "eidx");
                        LLVMBuildStore(g->builder, right, elem_ptr);
                    }
                }
            } else if (arr_expr->kind == AST_MEMBER_ACCESS) {
                /* obj.field[i] = value — array stored in a struct/class field */
                zan_type_t *at = member_access_field_type(locals, arr_expr);
                if (at) {
                    LLVMValueRef arr_ptr = emit_expr(g, arr_expr, locals);
                    LLVMValueRef idx = emit_expr(g, expr->binary.left->index.index, locals);
                    LLVMTypeRef elem_llvm = LLVMInt32TypeInContext(g->ctx);
                    if (at->element_type) {
                        elem_llvm = map_type(g, at->element_type);
                    }
                    LLVMValueRef elem_ptr = LLVMBuildGEP2(g->builder, elem_llvm, arr_ptr, &idx, 1, "eidx");
                    LLVMBuildStore(g->builder, right, elem_ptr);
                }
            }
        } else if (expr->binary.left->kind == AST_MEMBER_ACCESS) {
            /* obj.Field = value */
            zan_ast_node_t *obj_expr = expr->binary.left->member.object;
            bool stored = false;
            if (obj_expr->kind == AST_IDENTIFIER) {
                local_var_t *local = local_find(locals, obj_expr->ident.name);
                if (local && local->type && local->type->sym) {
                    int fi = get_field_index(local->type->sym, expr->binary.left->member.name);
                    if (fi >= 0) {
                        LLVMTypeRef st = get_struct_llvm_type(g, local->type->sym);
                        if (st) {
                            LLVMValueRef struct_ptr = struct_base_ptr(g, local, st);
                            LLVMValueRef fptr = LLVMBuildStructGEP2(g->builder, st, struct_ptr, (unsigned)fi, "fld");
                            LLVMBuildStore(g->builder, right, fptr);
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
                            LLVMBuildStore(g->builder, right, fptr);
                        }
                    }
                }
            }
        }
        return right;
    }

    case AST_CALL: {
        /* special-case Console.WriteLine */
        if (is_call_to(expr, "Console", "WriteLine") ||
            is_call_to(expr, "Console", "PrintLine")) {
            if (expr->call.args.count > 0) {
                LLVMValueRef arg = emit_expr(g, expr->call.args.items[0], locals);
                LLVMTypeRef arg_type = LLVMTypeOf(arg);

                if (LLVMGetTypeKind(arg_type) == LLVMPointerTypeKind) {
                    LLVMTypeRef fn_type = LLVMFunctionType(
                        LLVMVoidTypeInContext(g->ctx),
                        (LLVMTypeRef[]){ LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0) },
                        1, 0);
                    LLVMBuildCall2(g->builder, fn_type, g->rt_println, &arg, 1, "");
                } else if (LLVMGetTypeKind(arg_type) == LLVMDoubleTypeKind ||
                           LLVMGetTypeKind(arg_type) == LLVMFloatTypeKind) {
                    LLVMTypeRef fn_type = LLVMFunctionType(
                        LLVMVoidTypeInContext(g->ctx),
                        (LLVMTypeRef[]){ LLVMDoubleTypeInContext(g->ctx) },
                        1, 0);
                    LLVMBuildCall2(g->builder, fn_type, g->rt_print_double, &arg, 1, "");
                } else {
                    /* ensure integer arg is i64 for print_int */
                    if (LLVMGetTypeKind(arg_type) == LLVMIntegerTypeKind &&
                        LLVMGetIntTypeWidth(arg_type) < 64) {
                        arg = LLVMBuildSExt(g->builder, arg,
                                            LLVMInt64TypeInContext(g->ctx), "ext");
                    }
                    LLVMTypeRef fn_type = LLVMFunctionType(
                        LLVMVoidTypeInContext(g->ctx),
                        (LLVMTypeRef[]){ LLVMInt64TypeInContext(g->ctx) },
                        1, 0);
                    LLVMBuildCall2(g->builder, fn_type, g->rt_print_int, &arg, 1, "");
                }
            }
            return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
        }

        /* Console.Write (no newline) */
        if (is_call_to(expr, "Console", "Write")) {
            if (expr->call.args.count > 0) {
                LLVMValueRef arg = emit_expr(g, expr->call.args.items[0], locals);
                LLVMTypeRef arg_type = LLVMTypeOf(arg);
                LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                LLVMTypeRef printf_args[] = { i8ptr };
                LLVMTypeRef printf_type = LLVMFunctionType(
                    LLVMInt32TypeInContext(g->ctx), printf_args, 1, 1);
                LLVMValueRef printf_fn = LLVMGetNamedFunction(g->mod, "printf");

                if (LLVMGetTypeKind(arg_type) == LLVMPointerTypeKind) {
                    LLVMValueRef fmt = LLVMBuildGlobalStringPtr(g->builder, "%s", "wfmt_s");
                    LLVMValueRef args[] = { fmt, arg };
                    LLVMBuildCall2(g->builder, printf_type, printf_fn, args, 2, "");
                } else if (LLVMGetTypeKind(arg_type) == LLVMDoubleTypeKind ||
                           LLVMGetTypeKind(arg_type) == LLVMFloatTypeKind) {
                    LLVMValueRef fmt = LLVMBuildGlobalStringPtr(g->builder, "%g", "wfmt_d");
                    LLVMValueRef dbl_arg = arg;
                    if (LLVMGetTypeKind(arg_type) == LLVMFloatTypeKind)
                        dbl_arg = LLVMBuildFPExt(g->builder, arg, LLVMDoubleTypeInContext(g->ctx), "ext");
                    LLVMValueRef args[] = { fmt, dbl_arg };
                    LLVMBuildCall2(g->builder, printf_type, printf_fn, args, 2, "");
                } else {
                    LLVMValueRef fmt = LLVMBuildGlobalStringPtr(g->builder, "%lld", "wfmt_i");
                    LLVMValueRef int_arg = arg;
                    if (LLVMGetTypeKind(arg_type) == LLVMIntegerTypeKind &&
                        LLVMGetIntTypeWidth(arg_type) < 64) {
                        int_arg = LLVMBuildSExt(g->builder, arg, LLVMInt64TypeInContext(g->ctx), "ext");
                    }
                    LLVMValueRef args[] = { fmt, int_arg };
                    LLVMBuildCall2(g->builder, printf_type, printf_fn, args, 2, "");
                }
            }
            return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
        }

        /* Console.ReadLine() -> reads a line from stdin, returns i8* */
        if (is_call_to(expr, "Console", "ReadLine")) {
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
            /* allocate 1024 byte buffer */
            LLVMValueRef buf_size = LLVMConstInt(i64, 1024, 0);
            LLVMTypeRef malloc_type = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i64 }, 1, 0);
            LLVMValueRef buf = LLVMBuildCall2(g->builder, malloc_type, g->fn_malloc, &buf_size, 1, "rdbuf");
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
            LLVMValueRef stdin_ptr = LLVMBuildCall2(g->builder,
                LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ LLVMInt32TypeInContext(g->ctx) }, 1, 0),
                stdin_fn, &zero, 1, "stdin");
            LLVMValueRef sz = LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 1024, 0);
            LLVMValueRef fgets_args[] = { buf, sz, stdin_ptr };
            LLVMBuildCall2(g->builder,
                LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, LLVMInt32TypeInContext(g->ctx), i8ptr }, 3, 0),
                fgets_fn, fgets_args, 3, "");
            return buf;
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
            return LLVMBuildCall2(g->builder, sqrt_type, sqrt_fn, &arg, 1, "sqrt");
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
                return LLVMBuildCall2(g->builder, LLVMFunctionType(dbl, (LLVMTypeRef[]){ dbl }, 1, 0),
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
            return LLVMBuildCall2(g->builder, LLVMFunctionType(dbl, (LLVMTypeRef[]){ dbl, dbl }, 2, 0),
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
            return LLVMBuildCall2(g->builder, LLVMFunctionType(dbl, (LLVMTypeRef[]){ dbl }, 1, 0),
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
            return LLVMBuildCall2(g->builder, LLVMFunctionType(dbl, (LLVMTypeRef[]){ dbl }, 1, 0),
                ceil_fn, &arg, 1, "ceil");
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
                    if (method_name.len == 7 && memcmp(method_name.str, "ToInt32", 7) == 0 &&
                        expr->call.args.count == 1) {
                        LLVMValueRef arg = emit_expr(g, expr->call.args.items[0], locals);
                        LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                        LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
                        LLVMValueRef atoi_fn = LLVMGetNamedFunction(g->mod, "atoi");
                        if (!atoi_fn) {
                            LLVMTypeRef atoi_type = LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr }, 1, 0);
                            atoi_fn = LLVMAddFunction(g->mod, "atoi", atoi_type);
                        }
                        return LLVMBuildCall2(g->builder,
                            LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr }, 1, 0),
                            atoi_fn, &arg, 1, "atoi");
                    }
                    if (method_name.len == 8 && memcmp(method_name.str, "ToString", 8) == 0 &&
                        expr->call.args.count == 1) {
                        LLVMValueRef arg = emit_expr(g, expr->call.args.items[0], locals);
                        LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                        LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                        /* allocate buffer and sprintf */
                        LLVMValueRef buf_size = LLVMConstInt(i64, 32, 0);
                        LLVMValueRef buf = LLVMBuildCall2(g->builder,
                            LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i64 }, 1, 0),
                            g->fn_malloc, &buf_size, 1, "buf");
                        LLVMValueRef fmt = LLVMBuildGlobalStringPtr(g->builder, "%lld", "itoa_fmt");
                        LLVMValueRef int_arg = arg;
                        if (LLVMGetTypeKind(LLVMTypeOf(arg)) == LLVMIntegerTypeKind &&
                            LLVMGetIntTypeWidth(LLVMTypeOf(arg)) < 64) {
                            int_arg = LLVMBuildSExt(g->builder, arg, i64, "ext");
                        }
                        LLVMValueRef sn_args[] = { buf, LLVMConstInt(i64, 32, 0), fmt, int_arg };
                        LLVMBuildCall2(g->builder,
                            LLVMFunctionType(LLVMInt32TypeInContext(g->ctx),
                                (LLVMTypeRef[]){ i8ptr, i64, i8ptr }, 3, 1),
                            g->fn_snprintf, sn_args, 4, "");
                        return buf;
                    }
                }
            }
        }

        /* File.ReadAllText(path) -> string */
        if (is_call_to(expr, "File", "ReadAllText") && expr->call.args.count == 1) {
            LLVMValueRef path_arg = emit_expr(g, expr->call.args.items[0], locals);
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
            LLVMValueRef fp = LLVMBuildCall2(g->builder,
                LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0),
                fopen_fn, open_args, 2, "fp");
            /* seek to end, get size */
            LLVMValueRef seek_end_args[] = { fp, LLVMConstInt(i64, 0, 0), LLVMConstInt(i32, 2, 0) };
            LLVMBuildCall2(g->builder, LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr, i64, i32 }, 3, 0),
                fseek_fn, seek_end_args, 3, "");
            LLVMValueRef size = LLVMBuildCall2(g->builder,
                LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr }, 1, 0), ftell_fn, &fp, 1, "sz");
            /* seek back to start */
            LLVMValueRef seek_start_args[] = { fp, LLVMConstInt(i64, 0, 0), LLVMConstInt(i32, 0, 0) };
            LLVMBuildCall2(g->builder, LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr, i64, i32 }, 3, 0),
                fseek_fn, seek_start_args, 3, "");
            /* allocate buffer (size+1 for null terminator) */
            LLVMValueRef buf_size = LLVMBuildAdd(g->builder, size, LLVMConstInt(i64, 1, 0), "bsz");
            LLVMValueRef buf = LLVMBuildCall2(g->builder,
                LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i64 }, 1, 0), g->fn_malloc, &buf_size, 1, "buf");
            /* read file */
            LLVMValueRef fread_args[] = { buf, LLVMConstInt(i64, 1, 0), size, fp };
            LLVMBuildCall2(g->builder, LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr, i64, i64, i8ptr }, 4, 0),
                fread_fn, fread_args, 4, "");
            /* null terminate */
            LLVMValueRef end_ptr = LLVMBuildGEP2(g->builder, LLVMInt8TypeInContext(g->ctx), buf, &size, 1, "end");
            LLVMBuildStore(g->builder, LLVMConstInt(LLVMInt8TypeInContext(g->ctx), 0, 0), end_ptr);
            /* close */
            LLVMBuildCall2(g->builder, LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr }, 1, 0),
                fclose_fn, &fp, 1, "");
            return buf;
        }

        /* File.WriteAllText(path, content) */
        if (is_call_to(expr, "File", "WriteAllText") && expr->call.args.count == 2) {
            LLVMValueRef path_arg = emit_expr(g, expr->call.args.items[0], locals);
            LLVMValueRef content_arg = emit_expr(g, expr->call.args.items[1], locals);
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
            LLVMValueRef fp = LLVMBuildCall2(g->builder,
                LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0),
                fopen_fn, open_args, 2, "fp");
            LLVMValueRef fputs_args[] = { content_arg, fp };
            LLVMBuildCall2(g->builder, LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0),
                fputs_fn, fputs_args, 2, "");
            LLVMBuildCall2(g->builder, LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr }, 1, 0),
                fclose_fn, &fp, 1, "");
            return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
        }

        /* Path.GetFileName(path), Path.GetExtension(path), Path.Combine(a,b) */
        if (is_call_to(expr, "Path", "GetFileName") && expr->call.args.count == 1) {
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
            LLVMValueRef path_val = emit_expr(g, expr->call.args.items[0], locals);
            /* call strrchr(path, '/') then strrchr(path, '\\') and pick later one */
            LLVMValueRef strrchr_fn = LLVMGetNamedFunction(g->mod, "strrchr");
            LLVMValueRef slash_args[] = { path_val, LLVMConstInt(i32t, '/', 0) };
            LLVMValueRef slash = LLVMBuildCall2(g->builder,
                LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i32t }, 2, 0),
                strrchr_fn, slash_args, 2, "slash");
            LLVMValueRef bslash_args[] = { path_val, LLVMConstInt(i32t, 92, 0) }; /* 92 = backslash */
            LLVMValueRef bslash = LLVMBuildCall2(g->builder,
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
            LLVMValueRef path_val = emit_expr(g, expr->call.args.items[0], locals);
            LLVMValueRef strrchr_fn = LLVMGetNamedFunction(g->mod, "strrchr");
            LLVMValueRef dot_args[] = { path_val, LLVMConstInt(i32t, '.', 0) };
            LLVMValueRef dot = LLVMBuildCall2(g->builder,
                LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i32t }, 2, 0),
                strrchr_fn, dot_args, 2, "dot");
            /* if no dot, return empty string */
            LLVMValueRef is_null = LLVMBuildICmp(g->builder, LLVMIntEQ, dot, LLVMConstNull(i8ptr), "dnull");
            LLVMValueRef empty = LLVMBuildGlobalStringPtr(g->builder, "", "empty");
            return LLVMBuildSelect(g->builder, is_null, empty, dot, "ext");
        }

        if (is_call_to(expr, "Path", "Combine") && expr->call.args.count == 2) {
            LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMValueRef a = emit_expr(g, expr->call.args.items[0], locals);
            LLVMValueRef b = emit_expr(g, expr->call.args.items[1], locals);
            /* len = strlen(a) + 1 + strlen(b) + 1 */
            LLVMTypeRef strlen_type = LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr }, 1, 0);
            LLVMValueRef len_a = LLVMBuildCall2(g->builder, strlen_type, g->fn_strlen, &a, 1, "la");
            LLVMValueRef len_b = LLVMBuildCall2(g->builder, strlen_type, g->fn_strlen, &b, 1, "lb");
            LLVMValueRef total = LLVMBuildAdd(g->builder, len_a, len_b, "t");
            total = LLVMBuildAdd(g->builder, total, LLVMConstInt(i64, 2, 0), "t2"); /* +separator+null */
            LLVMValueRef buf = LLVMBuildCall2(g->builder,
                LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i64 }, 1, 0),
                g->fn_malloc, &total, 1, "pbuf");
            /* strcpy(buf, a) */
            LLVMValueRef strcpy_args[] = { buf, a };
            LLVMBuildCall2(g->builder,
                LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0),
                g->fn_strcpy, strcpy_args, 2, "");
            /* strcat(buf, "/") */
            LLVMValueRef sep = LLVMBuildGlobalStringPtr(g->builder, "/", "sep");
            LLVMValueRef cat1_args[] = { buf, sep };
            LLVMBuildCall2(g->builder,
                LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0),
                g->fn_strcat, cat1_args, 2, "");
            /* strcat(buf, b) */
            LLVMValueRef cat2_args[] = { buf, b };
            LLVMBuildCall2(g->builder,
                LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0),
                g->fn_strcat, cat2_args, 2, "");
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
                    LLVMValueRef new_data_raw = LLVMBuildCall2(g->builder,
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
                    if (LLVMGetTypeKind(LLVMTypeOf(val)) == LLVMIntegerTypeKind &&
                        LLVMGetIntTypeWidth(LLVMTypeOf(val)) < 64) {
                        val = LLVMBuildSExt(g->builder, val, i64, "ext");
                    }
                    LLVMBuildStore(g->builder, val, elem_ptr);
                    /* count++ */
                    LLVMValueRef new_count = LLVMBuildAdd(g->builder, count2, LLVMConstInt(i64, 1, 0), "nc");
                    LLVMBuildStore(g->builder, new_count, count_ptr);
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
                        if (LLVMGetTypeKind(LLVMTypeOf(val_v)) == LLVMIntegerTypeKind &&
                            LLVMGetIntTypeWidth(LLVMTypeOf(val_v)) < 64) {
                            val_v = LLVMBuildSExt(g->builder, val_v, i64, "ext");
                        }
                        LLVMValueRef kslot = LLVMBuildGEP2(g->builder, i8ptr, ks, &cnt, 1, "ksl");
                        LLVMBuildStore(g->builder, key_val, kslot);
                        LLVMValueRef vslot = LLVMBuildGEP2(g->builder, i64, vs, &cnt, 1, "vsl");
                        LLVMBuildStore(g->builder, val_v, vslot);
                        LLVMValueRef nc = LLVMBuildAdd(g->builder, cnt, LLVMConstInt(i64, 1, 0), "nc");
                        LLVMBuildStore(g->builder, nc, cntp);
                        return LLVMConstInt(i32t, 0, 0);
                    }

                    if (mname.len == 11 && memcmp(mname.str, "ContainsKey", 11) == 0 && expr->call.args.count == 1) {
                        LLVMValueRef cntp = LLVMBuildStructGEP2(g->builder, g->dict_struct_type, dp, 0, "cntp");
                        LLVMValueRef cnt = LLVMBuildLoad2(g->builder, i64, cntp, "cnt");
                        LLVMValueRef kp = LLVMBuildStructGEP2(g->builder, g->dict_struct_type, dp, 2, "kp");
                        LLVMValueRef ks = LLVMBuildLoad2(g->builder, LLVMPointerType(i8ptr, 0), kp, "ks");
                        LLVMValueRef search = emit_expr(g, expr->call.args.items[0], locals);
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
                        LLVMValueRef cmp_args[] = { kv, search };
                        LLVMValueRef cmp = LLVMBuildCall2(g->builder,
                            LLVMFunctionType(i32t, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0),
                            g->fn_strcmp, cmp_args, 2, "cmp");
                        LLVMValueRef eq = LLVMBuildICmp(g->builder, LLVMIntEQ, cmp, LLVMConstInt(i32t, 0, 0), "eq");
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
                }
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
                    zan_symbol_t *method_sym = get_method_sym(type_sym, callee->member.name);
                    if (method_sym) {
                        for (int fi = 0; fi < g->function_count; fi++) {
                            if (g->functions[fi].sym == method_sym) {
                                int argc = expr->call.args.count + 1;
                                LLVMValueRef *call_args = (LLVMValueRef *)calloc((size_t)argc, sizeof(LLVMValueRef));
                                /* receiver: class refs hold the object pointer in
                                 * the local, so load it; struct value types pass
                                 * the storage address directly. */
                                LLVMTypeRef at = LLVMGetAllocatedType(local->alloca);
                                if (LLVMGetTypeKind(at) == LLVMPointerTypeKind) {
                                    call_args[0] = LLVMBuildLoad2(g->builder, at, local->alloca, "recv");
                                } else {
                                    call_args[0] = local->alloca;
                                }
                                for (int k = 0; k < expr->call.args.count; k++) {
                                    call_args[k + 1] = emit_expr(g, expr->call.args.items[k], locals);
                                }
                                const char *cn = (LLVMGetTypeKind(LLVMGetReturnType(g->functions[fi].fn_type)) == LLVMVoidTypeKind) ? "" : "mcall";
                                LLVMValueRef result = LLVMBuildCall2(g->builder, g->functions[fi].fn_type,
                                    g->functions[fi].fn, call_args, (unsigned)argc, cn);
                                free(call_args);
                                return result;
                            }
                        }
                    }
                }

                /* try as static method: ClassName.Method(args) */
                zan_symbol_t *type_sym = zan_binder_lookup(g->binder, callee->member.object->ident.name);
                if (type_sym && (type_sym->kind == SYM_CLASS || type_sym->kind == SYM_STRUCT)) {
                    zan_symbol_t *method_sym = get_method_sym(type_sym, callee->member.name);
                    if (method_sym) {
                        for (int fi = 0; fi < g->function_count; fi++) {
                            if (g->functions[fi].sym == method_sym) {
                                int argc = expr->call.args.count;
                                LLVMValueRef *call_args = (LLVMValueRef *)calloc((size_t)(argc > 0 ? argc : 1), sizeof(LLVMValueRef));
                                for (int k = 0; k < argc; k++) {
                                    call_args[k] = emit_expr(g, expr->call.args.items[k], locals);
                                }
                                const char *cn = (LLVMGetTypeKind(LLVMGetReturnType(g->functions[fi].fn_type)) == LLVMVoidTypeKind) ? "" : "scall";
                                LLVMValueRef result = LLVMBuildCall2(g->builder, g->functions[fi].fn_type,
                                    g->functions[fi].fn, call_args, (unsigned)argc, cn);
                                free(call_args);
                                return result;
                            }
                        }
                    }
                }
            }
        }

        /* bare function name call: Compute(21) → look up in current class then global */
        if (expr->call.callee && expr->call.callee->kind == AST_IDENTIFIER) {
            zan_istr_t fn_name = expr->call.callee->ident.name;

            /* try current class methods first */
            if (g->current_type_sym) {
                zan_symbol_t *method_sym = get_method_sym(g->current_type_sym, fn_name);
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
                            const char *cn = (LLVMGetTypeKind(LLVMGetReturnType(g->functions[fi].fn_type)) == LLVMVoidTypeKind) ? "" : "bcall";
                            LLVMValueRef result = LLVMBuildCall2(g->builder, g->functions[fi].fn_type,
                                g->functions[fi].fn, call_args, (unsigned)(argc + extra), cn);
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
                LLVMValueRef result = LLVMBuildCall2(g->builder, fn_type,
                    global_fn, call_args, (unsigned)argc, "gcall");
                free(call_args);
                return result;
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
        if (n == 0) return LLVMBuildGlobalStringPtr(g->builder, "", "empty");

        /* convert each part to i8* */
        LLVMValueRef *strs = (LLVMValueRef *)calloc((size_t)n, sizeof(LLVMValueRef));
        LLVMValueRef *lens = (LLVMValueRef *)calloc((size_t)n, sizeof(LLVMValueRef));

        LLVMTypeRef strlen_type = LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr }, 1, 0);
        LLVMTypeRef snprintf_type = LLVMFunctionType(
            LLVMInt32TypeInContext(g->ctx),
            (LLVMTypeRef[]){ i8ptr, i64, i8ptr }, 3, 1);

        for (int i = 0; i < n; i++) {
            zan_ast_node_t *part = expr->string_interp.parts.items[i];
            if (part->kind == AST_STRING_LITERAL) {
                strs[i] = LLVMBuildGlobalStringPtr(g->builder, part->str_val.str, "seg");
                lens[i] = LLVMConstInt(i64, (uint64_t)part->str_val.len, 0);
            } else {
                LLVMValueRef val = emit_expr(g, part, locals);
                LLVMTypeRef vt = LLVMTypeOf(val);
                LLVMTypeKind vtk = LLVMGetTypeKind(vt);

                if (vtk == LLVMPointerTypeKind) {
                    /* already a string */
                    strs[i] = val;
                    lens[i] = LLVMBuildCall2(g->builder, strlen_type, g->fn_strlen, &val, 1, "len");
                } else if (vtk == LLVMDoubleTypeKind || vtk == LLVMFloatTypeKind) {
                    /* snprintf(NULL, 0, "%g", val) to get length, then snprintf into buffer */
                    LLVMValueRef fmt = LLVMBuildGlobalStringPtr(g->builder, "%g", "dfmt");
                    LLVMValueRef null_ptr = LLVMConstNull(i8ptr);
                    LLVMValueRef zero = LLVMConstInt(i64, 0, 0);
                    LLVMValueRef snp_args1[] = { null_ptr, zero, fmt, val };
                    LLVMValueRef needed = LLVMBuildCall2(g->builder, snprintf_type, g->fn_snprintf, snp_args1, 4, "needed");
                    LLVMValueRef needed64 = LLVMBuildSExt(g->builder, needed, i64, "n64");
                    LLVMValueRef buf_size = LLVMBuildAdd(g->builder, needed64, LLVMConstInt(i64, 1, 0), "bsz");
                    LLVMTypeRef malloc_type = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i64 }, 1, 0);
                    LLVMValueRef buf = LLVMBuildCall2(g->builder, malloc_type, g->fn_malloc, &buf_size, 1, "buf");
                    LLVMValueRef snp_args2[] = { buf, buf_size, fmt, val };
                    LLVMBuildCall2(g->builder, snprintf_type, g->fn_snprintf, snp_args2, 4, "");
                    strs[i] = buf;
                    lens[i] = needed64;
                } else {
                    /* integer types — format with %lld */
                    LLVMValueRef val64 = val;
                    if (LLVMGetIntTypeWidth(vt) < 64) {
                        val64 = LLVMBuildSExt(g->builder, val, i64, "ext");
                    }
                    LLVMValueRef fmt = LLVMBuildGlobalStringPtr(g->builder, "%lld", "ifmt");
                    LLVMValueRef null_ptr = LLVMConstNull(i8ptr);
                    LLVMValueRef zero = LLVMConstInt(i64, 0, 0);
                    LLVMValueRef snp_args1[] = { null_ptr, zero, fmt, val64 };
                    LLVMValueRef needed = LLVMBuildCall2(g->builder, snprintf_type, g->fn_snprintf, snp_args1, 4, "needed");
                    LLVMValueRef needed64 = LLVMBuildSExt(g->builder, needed, i64, "n64");
                    LLVMValueRef buf_size = LLVMBuildAdd(g->builder, needed64, LLVMConstInt(i64, 1, 0), "bsz");
                    LLVMTypeRef malloc_type = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i64 }, 1, 0);
                    LLVMValueRef buf = LLVMBuildCall2(g->builder, malloc_type, g->fn_malloc, &buf_size, 1, "buf");
                    LLVMValueRef snp_args2[] = { buf, buf_size, fmt, val64 };
                    LLVMBuildCall2(g->builder, snprintf_type, g->fn_snprintf, snp_args2, 4, "");
                    strs[i] = buf;
                    lens[i] = needed64;
                }
            }
        }

        /* compute total length */
        LLVMValueRef total_len = LLVMConstInt(i64, 0, 0);
        for (int i = 0; i < n; i++) {
            total_len = LLVMBuildAdd(g->builder, total_len, lens[i], "tlen");
        }
        LLVMValueRef alloc_size = LLVMBuildAdd(g->builder, total_len, LLVMConstInt(i64, 1, 0), "asz");

        /* malloc result buffer */
        LLVMTypeRef malloc_type = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i64 }, 1, 0);
        LLVMValueRef result = LLVMBuildCall2(g->builder, malloc_type, g->fn_malloc, &alloc_size, 1, "interp");

        /* strcpy first, strcat rest */
        LLVMTypeRef strcpy_type = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0);
        LLVMValueRef strcpy_args[] = { result, strs[0] };
        LLVMBuildCall2(g->builder, strcpy_type, g->fn_strcpy, strcpy_args, 2, "");

        for (int i = 1; i < n; i++) {
            LLVMValueRef cat_args[] = { result, strs[i] };
            LLVMBuildCall2(g->builder, strcpy_type, g->fn_strcat, cat_args, 2, "");
        }

        free(strs);
        free(lens);
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

        /* String.Length property — call strlen on string pointer */
        if (expr->member.name.len == 6 && memcmp(expr->member.name.str, "Length", 6) == 0) {
            LLVMValueRef obj_val = emit_expr(g, expr->member.object, locals);
            if (LLVMGetTypeKind(LLVMTypeOf(obj_val)) == LLVMPointerTypeKind) {
                LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                LLVMTypeRef strlen_type = LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr }, 1, 0);
                LLVMValueRef len = LLVMBuildCall2(g->builder, strlen_type, g->fn_strlen, &obj_val, 1, "len");
                /* truncate to i32 for compatibility */
                return LLVMBuildTrunc(g->builder, len, LLVMInt32TypeInContext(g->ctx), "len32");
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
                            return LLVMBuildLoad2(g->builder, field_type, field_ptr, "fval");
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
                        return LLVMBuildLoad2(g->builder, ft, field_ptr, "gfval");
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
            arr_type = member_access_field_type(locals, expr->index.object);
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
            /* reinterpret pointer (class/string) elements: slots hold i64. */
            zan_type_t *et = container_elem_type(arr_type);
            if (et) {
                LLVMTypeRef m = map_type(g, et);
                if (LLVMGetTypeKind(m) == LLVMPointerTypeKind)
                    return LLVMBuildIntToPtr(g->builder, raw, m, "elp");
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
            LLVMValueRef search = emit_expr(g, expr->index.index, locals);
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
            LLVMValueRef cmp_args[] = { kv, search };
            LLVMValueRef cmp = LLVMBuildCall2(g->builder,
                LLVMFunctionType(i32t, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0),
                g->fn_strcmp, cmp_args, 2, "cmp");
            LLVMValueRef eq = LLVMBuildICmp(g->builder, LLVMIntEQ, cmp, LLVMConstInt(i32t, 0, 0), "eq");
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
            return LLVMBuildLoad2(g->builder, i64, res, "dval");
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
            LLVMValueRef elem_ptr = LLVMBuildGEP2(g->builder, elem_llvm, arr_ptr, &idx, 1, "eidx");
            return LLVMBuildLoad2(g->builder, elem_llvm, elem_ptr, "elem");
        }
        return LLVMConstInt(LLVMInt64TypeInContext(g->ctx), 0, 0);
    }

    case AST_NEW_EXPR: {
        /* new List<T>() — built-in dynamic list */
        if (expr->new_expr.type && expr->new_expr.type->kind == AST_TYPE_REF) {
            zan_istr_t tname = expr->new_expr.type->type_ref.name;
            if (tname.len == 4 && memcmp(tname.str, "List", 4) == 0) {
                LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                /* allocate List struct on heap */
                LLVMValueRef list_size = LLVMConstInt(i64, 24, 0); /* 3 * i64 = 24 bytes */
                LLVMValueRef list_ptr = LLVMBuildCall2(g->builder,
                    LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i64 }, 1, 0),
                    g->fn_malloc, &list_size, 1, "list");
                /* cast to List* */
                LLVMValueRef typed_ptr = LLVMBuildBitCast(g->builder, list_ptr,
                    LLVMPointerType(g->list_struct_type, 0), "lptr");
                /* set count = 0 */
                LLVMValueRef count_ptr = LLVMBuildStructGEP2(g->builder, g->list_struct_type, typed_ptr, 0, "cnt");
                LLVMBuildStore(g->builder, LLVMConstInt(i64, 0, 0), count_ptr);
                /* set capacity = 8 (initial) */
                LLVMValueRef cap_ptr = LLVMBuildStructGEP2(g->builder, g->list_struct_type, typed_ptr, 1, "cap");
                LLVMBuildStore(g->builder, LLVMConstInt(i64, 8, 0), cap_ptr);
                /* allocate initial data buffer: 8 * sizeof(i64) = 64 bytes */
                LLVMValueRef data_size = LLVMConstInt(i64, 64, 0);
                LLVMValueRef data_ptr = LLVMBuildCall2(g->builder,
                    LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i64 }, 1, 0),
                    g->fn_malloc, &data_size, 1, "data");
                LLVMValueRef data_typed = LLVMBuildBitCast(g->builder, data_ptr,
                    LLVMPointerType(i64, 0), "dptr");
                LLVMValueRef data_field = LLVMBuildStructGEP2(g->builder, g->list_struct_type, typed_ptr, 2, "df");
                LLVMBuildStore(g->builder, data_typed, data_field);
                return LLVMBuildBitCast(g->builder, typed_ptr, i8ptr, "listv");
            }
        }

        /* new Dict<K,V>() — built-in hash map */
        if (expr->new_expr.type && expr->new_expr.type->kind == AST_TYPE_REF) {
            zan_istr_t tname2 = expr->new_expr.type->type_ref.name;
            if (tname2.len == 4 && memcmp(tname2.str, "Dict", 4) == 0) {
                LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                /* allocate Dict struct: { i64 count, i64 capacity, i8** keys, i64* values } = 32 bytes */
                LLVMValueRef dict_size = LLVMConstInt(i64, 32, 0);
                LLVMValueRef dict_raw = LLVMBuildCall2(g->builder,
                    LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i64 }, 1, 0),
                    g->fn_malloc, &dict_size, 1, "dict");
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
                LLVMValueRef keys_raw = LLVMBuildCall2(g->builder,
                    LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i64 }, 1, 0),
                    g->fn_malloc, &keys_sz, 1, "keys");
                LLVMValueRef keys_typed = LLVMBuildBitCast(g->builder, keys_raw,
                    LLVMPointerType(i8ptr, 0), "kptr");
                LLVMValueRef kf = LLVMBuildStructGEP2(g->builder, g->dict_struct_type, typed_ptr, 2, "kf");
                LLVMBuildStore(g->builder, keys_typed, kf);
                /* values = malloc(16 * 8) */
                LLVMValueRef vals_sz = LLVMConstInt(i64, 128, 0);
                LLVMValueRef vals_raw = LLVMBuildCall2(g->builder,
                    LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i64 }, 1, 0),
                    g->fn_malloc, &vals_sz, 1, "vals");
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

            /* malloc(size * sizeof(elem)) */
            LLVMValueRef elem_size = LLVMSizeOf(elem_llvm);
            LLVMValueRef total = LLVMBuildMul(g->builder,
                LLVMBuildZExt(g->builder, size_val, LLVMInt64TypeInContext(g->ctx), "zext"),
                elem_size, "total");
            LLVMTypeRef malloc_type = LLVMFunctionType(
                LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0),
                (LLVMTypeRef[]){LLVMInt64TypeInContext(g->ctx)}, 1, 0);
            LLVMValueRef arr = LLVMBuildCall2(g->builder, malloc_type, g->fn_malloc, &total, 1, "arr");
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
                        /* assign a stable allocation-site index and a
                         * "file:line:col" descriptor for leak reporting. */
                        int site_idx = g->leak_site_count;
                        if (site_idx >= ZAN_MAX_LEAK_SITES) site_idx = ZAN_MAX_LEAK_SITES - 1;
                        else g->leak_site_count++;
                        char site_buf[600];
                        const char *sfile = g->src_file ? g->src_file : "<unknown>";
                        snprintf(site_buf, sizeof(site_buf), "%s:%u:%u",
                                 sfile, expr->loc.line, expr->loc.col);
                        LLVMValueRef site_name = LLVMBuildGlobalStringPtr(g->builder, site_buf, "site");
                        LLVMValueRef site_val = LLVMConstInt(i64, (unsigned long long)site_idx, 0);
                        LLVMTypeRef alloc_fn_type = LLVMFunctionType(i8ptr,
                            (LLVMTypeRef[]){ i64, i64, i8ptr }, 3, 0);
                        LLVMValueRef alloc_args3[] = { sz, site_val, site_name };
                        LLVMValueRef raw = LLVMBuildCall2(g->builder, alloc_fn_type, g->rt_alloc, alloc_args3, 3, "newobj");
                        alloca = LLVMBuildBitCast(g->builder, raw, LLVMPointerType(st, 0), "objp");
                    } else {
                        /* value type: stack-allocate as before */
                        alloca = LLVMBuildAlloca(g->builder, st, "new");
                    }
                    LLVMBuildStore(g->builder, LLVMConstNull(st), alloca);

                    /* look for constructor */
                    for (int ci = 0; ci < g->ctor_count; ci++) {
                        if (g->ctors[ci].type_sym == sym) {
                            int argc = expr->new_expr.args.count + 1;
                            LLVMValueRef *call_args = (LLVMValueRef *)calloc((size_t)argc, sizeof(LLVMValueRef));
                            call_args[0] = alloca; /* this ptr */
                            for (int k = 0; k < expr->new_expr.args.count; k++) {
                                call_args[k + 1] = emit_expr(g, expr->new_expr.args.items[k], locals);
                            }
                            LLVMBuildCall2(g->builder, g->ctors[ci].fn_type,
                                g->ctors[ci].fn, call_args, (unsigned)argc, "");
                            free(call_args);
                            break;
                        }
                    }

                    /* if no constructor, handle field initializers */
                    if (g->ctor_count == 0 || 1) {
                        for (int i = 0; i < expr->new_expr.args.count; i++) {
                            zan_ast_node_t *arg = expr->new_expr.args.items[i];
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
                                        }
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
        /* typeof(x) — return type name as string */
        return LLVMBuildGlobalStringPtr(g->builder, "object", "tname");
    }

    case AST_THIS_EXPR: {
        /* this — first parameter of instance method */
        LLVMValueRef fn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder));
        if (LLVMCountParams(fn) > 0) {
            return LLVMGetParam(fn, 0);
        }
        return LLVMConstNull(LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0));
    }

    case AST_BASE_EXPR: {
        /* base — same as this for single inheritance */
        LLVMValueRef fn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder));
        if (LLVMCountParams(fn) > 0) {
            return LLVMGetParam(fn, 0);
        }
        return LLVMConstNull(LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0));
    }

    case AST_AWAIT_EXPR: {
        /* await expr — simplified: evaluate synchronously */
        return emit_expr(g, expr->await_expr.expr, locals);
    }

    case AST_LAMBDA: {
        /* lambda expression — generate anonymous function */
        LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
        int pc = expr->lambda.params.count;
        LLVMTypeRef *param_types = (LLVMTypeRef *)calloc((size_t)(pc > 0 ? pc : 1), sizeof(LLVMTypeRef));
        for (int k = 0; k < pc; k++) {
            param_types[k] = i64;
        }
        LLVMTypeRef ret_type = i64;
        LLVMTypeRef fn_type = LLVMFunctionType(ret_type, param_types, (unsigned)pc, 0);

        char lname[64];
        static int lambda_id = 0;
        snprintf(lname, sizeof(lname), "lambda_%d", lambda_id++);
        LLVMValueRef lambda_fn = LLVMAddFunction(g->mod, lname, fn_type);

        /* emit lambda body */
        LLVMBasicBlockRef saved_bb = LLVMGetInsertBlock(g->builder);
        LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, lambda_fn, "entry");
        LLVMPositionBuilderAtEnd(g->builder, entry);

        local_scope_t lambda_locals;
        memset(&lambda_locals, 0, sizeof(lambda_locals));
        for (int k = 0; k < pc; k++) {
            zan_ast_node_t *param = expr->lambda.params.items[k];
            LLVMValueRef alloc = LLVMBuildAlloca(g->builder, i64, "lp");
            LLVMBuildStore(g->builder, LLVMGetParam(lambda_fn, (unsigned)k), alloc);
            local_add(&lambda_locals, param->param.name, alloc, g->binder->type_int);
        }

        if (expr->lambda.body) {
            if (expr->lambda.body->kind == AST_BLOCK) {
                emit_stmt(g, expr->lambda.body, &lambda_locals);
                if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder))) {
                    LLVMBuildRet(g->builder, LLVMConstInt(i64, 0, 0));
                }
            } else {
                LLVMValueRef result = emit_expr(g, expr->lambda.body, &lambda_locals);
                LLVMBuildRet(g->builder, result);
            }
        } else {
            LLVMBuildRet(g->builder, LLVMConstInt(i64, 0, 0));
        }

        LLVMPositionBuilderAtEnd(g->builder, saved_bb);
        free(param_types);
        return lambda_fn;
    }

    default:
        return LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0);
    }
}

/* ---- statement codegen ---- */

static void emit_stmt(zan_irgen_t *g, zan_ast_node_t *stmt, local_scope_t *locals) {
    if (!stmt) return;

    switch (stmt->kind) {
    case AST_BLOCK:
        for (int i = 0; i < stmt->block.stmts.count; i++) {
            emit_stmt(g, stmt->block.stmts.items[i], locals);
        }
        break;

    case AST_VAR_DECL: {
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
                    return;
                }
                if (tname.len == 4 && memcmp(tname.str, "Dict", 4) == 0) {
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
                                    LLVMBuildCall2(g->builder, g->ctors[ci].fn_type,
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
            local_add(locals, stmt->var_decl.name, alloca, type);
            return;
        }

        LLVMTypeRef llvm_type = map_type(g, type);
        LLVMValueRef alloca = LLVMBuildAlloca(g->builder, llvm_type, "var");

        if (stmt->var_decl.initializer) {
            LLVMValueRef init_val = emit_expr(g, stmt->var_decl.initializer, locals);
            LLVMBuildStore(g->builder, init_val, alloca);
        }

        local_add(locals, stmt->var_decl.name, alloca, type);
        break;
    }

    case AST_EXPR_STMT:
        emit_expr(g, stmt->expr_stmt.expr, locals);
        break;

    case AST_RETURN_STMT:
        if (stmt->ret.value) {
            LLVMValueRef val = emit_expr(g, stmt->ret.value, locals);
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
                }
            }
            LLVMBuildRet(g->builder, val);
        } else {
            LLVMBuildRetVoid(g->builder);
        }
        break;

    case AST_IF_STMT: {
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
            LLVMBuildBr(g->builder, merge_bb);
        }

        LLVMPositionBuilderAtEnd(g->builder, else_bb);
        if (stmt->if_stmt.else_body) {
            emit_stmt(g, stmt->if_stmt.else_body, locals);
        }
        if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder))) {
            LLVMBuildBr(g->builder, merge_bb);
        }

        LLVMPositionBuilderAtEnd(g->builder, merge_bb);
        break;
    }

    case AST_WHILE_STMT: {
        LLVMBasicBlockRef cond_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "while.cond");
        LLVMBasicBlockRef body_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "while.body");
        LLVMBasicBlockRef end_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "while.end");

        LLVMBasicBlockRef saved_break = g->break_target;
        LLVMBasicBlockRef saved_cont = g->continue_target;
        g->break_target = end_bb;
        g->continue_target = cond_bb;

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
            LLVMBuildBr(g->builder, cond_bb);
        }

        g->break_target = saved_break;
        g->continue_target = saved_cont;

        LLVMPositionBuilderAtEnd(g->builder, end_bb);
        break;
    }

    case AST_FOR_STMT: {
        if (stmt->for_stmt.init) emit_stmt(g, stmt->for_stmt.init, locals);

        LLVMBasicBlockRef cond_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "for.cond");
        LLVMBasicBlockRef body_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "for.body");
        LLVMBasicBlockRef step_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "for.step");
        LLVMBasicBlockRef end_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "for.end");

        LLVMBasicBlockRef saved_break = g->break_target;
        LLVMBasicBlockRef saved_cont = g->continue_target;
        g->break_target = end_bb;
        g->continue_target = step_bb;

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
            LLVMBuildBr(g->builder, step_bb);
        }

        LLVMPositionBuilderAtEnd(g->builder, step_bb);
        if (stmt->for_stmt.step) emit_expr(g, stmt->for_stmt.step, locals);
        LLVMBuildBr(g->builder, cond_bb);

        g->break_target = saved_break;
        g->continue_target = saved_cont;

        LLVMPositionBuilderAtEnd(g->builder, end_bb);
        break;
    }

    case AST_BREAK_STMT:
        if (g->break_target) {
            LLVMBuildBr(g->builder, g->break_target);
        }
        break;

    case AST_CONTINUE_STMT:
        if (g->continue_target) {
            LLVMBuildBr(g->builder, g->continue_target);
        }
        break;

    case AST_SWITCH_STMT: {
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
                LLVMBuildBr(g->builder, end_bb);
            }
        }

        /* emit default block */
        if (default_case) {
            LLVMPositionBuilderAtEnd(g->builder, default_bb);
            emit_stmt(g, default_case->switch_case.body, locals);
            if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder))) {
                LLVMBuildBr(g->builder, end_bb);
            }
        }

        LLVMPositionBuilderAtEnd(g->builder, end_bb);
        break;
    }

    case AST_TRY_STMT: {
        /* M2 basic try/catch: emit try body, skip catch/finally for now
         * (full exception handling requires personality function + landingpad) */
        emit_stmt(g, stmt->try_stmt.try_body, locals);
        /* TODO: M3+ full LLVM exception handling with landingpad */
        break;
    }

    case AST_DO_WHILE_STMT: {
        /* do { body } while (cond); */
        LLVMValueRef fn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(g->builder));
        LLVMBasicBlockRef body_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "do.body");
        LLVMBasicBlockRef cond_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "do.cond");
        LLVMBasicBlockRef end_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "do.end");
        LLVMBuildBr(g->builder, body_bb);

        LLVMPositionBuilderAtEnd(g->builder, body_bb);
        emit_stmt(g, stmt->while_stmt.body, locals);
        if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder))) {
            LLVMBuildBr(g->builder, cond_bb);
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

        /* evaluate collection */
        LLVMValueRef collection = emit_expr(g, stmt->foreach_stmt.collection, locals);

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
        LLVMValueRef iter_alloc = LLVMBuildAlloca(g->builder, i64, "fv");
        local_add(locals, stmt->foreach_stmt.var_name, iter_alloc, g->binder->type_int);

        LLVMBasicBlockRef cond_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "fe.cond");
        LLVMBasicBlockRef body_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "fe.body");
        LLVMBasicBlockRef end_bb = LLVMAppendBasicBlockInContext(g->ctx, fn, "fe.end");

        LLVMBuildBr(g->builder, cond_bb);
        LLVMPositionBuilderAtEnd(g->builder, cond_bb);
        LLVMValueRef idx_val = LLVMBuildLoad2(g->builder, i64, idx_alloc, "i");
        LLVMValueRef cmp = LLVMBuildICmp(g->builder, LLVMIntSLT, idx_val, count, "fcmp");
        LLVMBuildCondBr(g->builder, cmp, body_bb, end_bb);

        LLVMPositionBuilderAtEnd(g->builder, body_bb);
        /* load current element */
        LLVMValueRef elem_ptr = LLVMBuildGEP2(g->builder, i64, data, &idx_val, 1, "ep");
        LLVMValueRef elem = LLVMBuildLoad2(g->builder, i64, elem_ptr, "elem");
        LLVMBuildStore(g->builder, elem, iter_alloc);

        emit_stmt(g, stmt->foreach_stmt.body, locals);

        if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder))) {
            /* increment index */
            LLVMValueRef next = LLVMBuildAdd(g->builder,
                LLVMBuildLoad2(g->builder, i64, idx_alloc, "i2"),
                LLVMConstInt(i64, 1, 0), "next");
            LLVMBuildStore(g->builder, next, idx_alloc);
            LLVMBuildBr(g->builder, cond_bb);
        }

        LLVMPositionBuilderAtEnd(g->builder, end_bb);
        break;
    }

    case AST_THROW_STMT: {
        /* throw expr; — simplified: print error and exit */
        LLVMValueRef val = emit_expr(g, stmt->throw_stmt.value, locals);
        LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
        LLVMValueRef fmt = LLVMBuildGlobalStringPtr(g->builder,
            "Unhandled exception\n", "exfmt");
        LLVMValueRef args[] = { fmt };
        LLVMValueRef printf_fn = LLVMGetNamedFunction(g->mod, "printf");
        if (printf_fn) {
            LLVMBuildCall2(g->builder, LLVMFunctionType(LLVMInt32TypeInContext(g->ctx),
                &i8ptr, 1, 1), printf_fn, args, 1, "");
        }
        /* call exit(1) */
        LLVMTypeRef exit_args[] = { LLVMInt32TypeInContext(g->ctx) };
        LLVMTypeRef exit_type = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx),
            exit_args, 1, 0);
        LLVMValueRef exit_fn = LLVMGetNamedFunction(g->mod, "exit");
        if (!exit_fn) {
            exit_fn = LLVMAddFunction(g->mod, "exit", exit_type);
        }
        LLVMValueRef exit_arg = LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 1, 0);
        LLVMBuildCall2(g->builder, exit_type, exit_fn, &exit_arg, 1, "");
        LLVMBuildUnreachable(g->builder);
        break;
    }

    default:
        break;
    }
}

/* ---- top-level emission ---- */

static void emit_main_method(zan_irgen_t *g, zan_ast_node_t *method, zan_symbol_t *type_sym) {
    /* create main() function */
    LLVMTypeRef main_type = LLVMFunctionType(
        LLVMInt32TypeInContext(g->ctx), NULL, 0, 0);
    LLVMValueRef main_fn = LLVMAddFunction(g->mod, "main", main_type);

    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, main_fn, "entry");
    LLVMPositionBuilderAtEnd(g->builder, entry);

    g->current_fn = main_fn;
    g->current_fn_ret_type = LLVMInt32TypeInContext(g->ctx);
    g->current_type_sym = type_sym;
    g->current_this = NULL;

    /* schedule the leak report to run at program exit */
    if (g->check_leaks) {
        LLVMBuildCall2(g->builder, g->atexit_type, g->fn_atexit,
                       &g->fn_report_leaks, 1, "");
    }

    local_scope_t *locals = local_scope_new(g->arena);

    if (method->method_decl.body) {
        emit_stmt(g, method->method_decl.body, locals);
    }

    /* add return 0 if no terminator */
    if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder))) {
        LLVMBuildRet(g->builder, LLVMConstInt(LLVMInt32TypeInContext(g->ctx), 0, 0));
    }
    g->current_type_sym = NULL;
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
} method_body_work_t;

static void emit_user_methods(zan_irgen_t *g, zan_ast_node_t *unit) {
    /* Size an upper bound for the deferred body work list. */
    int work_cap = 0;
    for (int i = 0; i < unit->comp_unit.decls.count; i++) {
        zan_ast_node_t *decl = unit->comp_unit.decls.items[i];
        if (decl->kind == AST_CLASS_DECL || decl->kind == AST_STRUCT_DECL) {
            work_cap += decl->type_decl.members.count;
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

        for (int j = 0; j < decl->type_decl.members.count; j++) {
            zan_ast_node_t *member = decl->type_decl.members.items[j];
            bool is_ctor = (member->kind == AST_CONSTRUCTOR_DECL);
            if (member->kind != AST_METHOD_DECL && !is_ctor) continue;

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
                LLVMValueRef efn = LLVMAddFunction(g->mod, ext_name, ft);
                /* register as a static method so it can be called */
                zan_symbol_t *method_sym = NULL;
                for (int mi = 0; mi < type_sym->member_count; mi++) {
                    if (type_sym->members[mi]->name.len == member->method_decl.name.len &&
                        memcmp(type_sym->members[mi]->name.str, member->method_decl.name.str,
                               member->method_decl.name.len) == 0) {
                        method_sym = type_sym->members[mi];
                        break;
                    }
                }
                if (method_sym && g->function_count < 1024) {
                    g->functions[g->function_count].sym = method_sym;
                    g->functions[g->function_count].fn = efn;
                    g->functions[g->function_count].fn_type = ft;
                    g->function_count++;
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
                free(pt);
                continue;
            }

            if (!member->method_decl.body) continue;

            /* skip static Main — handled separately */
            bool is_static = !is_ctor && (member->method_decl.modifiers & MOD_STATIC) != 0;
            if (is_static && member->method_decl.name.len == 4 &&
                memcmp(member->method_decl.name.str, "Main", 4) == 0) continue;

            /* build function name: TypeName_MethodName or TypeName_ctor */
            char fn_name[512];
            if (is_ctor) {
                snprintf(fn_name, sizeof(fn_name), "%.*s_ctor",
                         (int)decl->type_decl.name.len, decl->type_decl.name.str);
            } else {
                snprintf(fn_name, sizeof(fn_name), "%.*s_%.*s",
                         (int)decl->type_decl.name.len, decl->type_decl.name.str,
                         (int)member->method_decl.name.len, member->method_decl.name.str);
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
                param_types[k + param_offset] = map_type(g, pt);
            }

            zan_type_t *ret_type = is_ctor ? g->binder->type_void
                : (member->method_decl.return_type
                    ? zan_binder_resolve_type(g->binder, member->method_decl.return_type)
                    : g->binder->type_void);
            LLVMTypeRef llvm_ret = map_type(g, ret_type);
            LLVMTypeRef fn_type = LLVMFunctionType(llvm_ret, param_types, (unsigned)total_params, 0);
            LLVMValueRef fn = LLVMAddFunction(g->mod, fn_name, fn_type);

            /* register in function/ctor table */
            if (is_ctor) {
                if (g->ctor_count < 256) {
                    g->ctors[g->ctor_count].type_sym = type_sym;
                    g->ctors[g->ctor_count].fn = fn;
                    g->ctors[g->ctor_count].fn_type = fn_type;
                    g->ctors[g->ctor_count].param_count = param_count;
                    g->ctor_count++;
                }
            } else if (g->function_count < 1024) {
                zan_symbol_t *method_sym = get_method_sym(type_sym, member->method_decl.name);
                g->functions[g->function_count].sym = method_sym;
                g->functions[g->function_count].fn = fn;
                g->functions[g->function_count].fn_type = fn_type;
                g->function_count++;
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
                work_count++;
            } else {
                free(param_types);
            }
        }
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

        LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(g->ctx, fn, "entry");
        LLVMPositionBuilderAtEnd(g->builder, entry);

        local_scope_t *locals = local_scope_new(g->arena);

        if (!is_static) {
            /* bind 'this' as first parameter */
            this_alloca = LLVMBuildAlloca(g->builder, param_types[0], "this");
            LLVMBuildStore(g->builder, LLVMGetParam(fn, 0), this_alloca);
        }

        /* bind method parameters */
        for (int k = 0; k < param_count; k++) {
            zan_ast_node_t *param = member->method_decl.params.items[k];
            LLVMValueRef param_alloca = LLVMBuildAlloca(g->builder, param_types[k + param_offset], "p");
            LLVMBuildStore(g->builder, LLVMGetParam(fn, (unsigned)(k + param_offset)), param_alloca);
            zan_type_t *pt = zan_binder_resolve_type(g->binder, param->param.type);
            local_add(locals, param->param.name, param_alloca, pt);
        }

        LLVMValueRef saved_fn = g->current_fn;
        LLVMTypeRef saved_fn_ret = g->current_fn_ret_type;
        LLVMValueRef saved_this = g->current_this;
        zan_symbol_t *saved_type_sym = g->current_type_sym;
        g->current_fn = fn;
        g->current_fn_ret_type = llvm_ret;
        g->current_this = is_static ? NULL : this_alloca;
        g->current_type_sym = type_sym;

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
            LLVMBuildRet(g->builder, val);
        }

        /* ensure function has terminator */
        LLVMBasicBlockRef cur_bb = LLVMGetInsertBlock(g->builder);
        if (!LLVMGetBasicBlockTerminator(cur_bb)) {
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
        free(param_types);
    }

    free(work);
}

zan_status_t zan_irgen_emit(zan_irgen_t *g, zan_ast_node_t *unit) {
    if (!unit || unit->kind != AST_COMPILATION_UNIT) return ZAN_ERROR;

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
                    emit_main_method(g, member, main_type_sym);
                }
                goto done;
            }
        }
    }
done:
    ;
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
        printf("%s", ir);
        LLVMDisposeMessage(ir);
        return ZAN_OK;
    }
    FILE *f = fopen(path, "w");
    if (!f) {
        LLVMDisposeMessage(ir);
        return ZAN_ERROR;
    }
    fputs(ir, f);
    fclose(f);
    LLVMDisposeMessage(ir);
    return ZAN_OK;
}

zan_status_t zan_irgen_write_obj(zan_irgen_t *g, const char *path) {
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

    LLVMInitializeARMTargetInfo();
    LLVMInitializeARMTarget();
    LLVMInitializeARMTargetMC();
    LLVMInitializeARMAsmParser();
    LLVMInitializeARMAsmPrinter();

    LLVMInitializeWebAssemblyTargetInfo();
    LLVMInitializeWebAssemblyTarget();
    LLVMInitializeWebAssemblyTargetMC();
    LLVMInitializeWebAssemblyAsmParser();
    LLVMInitializeWebAssemblyAsmPrinter();

    char *triple = LLVMGetDefaultTargetTriple();
    LLVMTargetRef target;
    char *error = NULL;

    if (LLVMGetTargetFromTriple(triple, &target, &error)) {
        zan_diag_emit(g->diag, DIAG_ERROR, zan_loc(0, 0, 0, 0),
                      "failed to get target: %s", error);
        LLVMDisposeMessage(error);
        LLVMDisposeMessage(triple);
        return ZAN_ERROR;
    }

    LLVMTargetMachineRef tm = LLVMCreateTargetMachine(
        target, triple, "generic", "",
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
