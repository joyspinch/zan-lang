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
     * socket await (otherwise unreferenced, so no link dependency). zan_io_pump
     * is given a WEAK inline definition returning 0 so ordinary programs link
     * and the scheduler's idle path is a no-op; linking zanrt_io overrides it
     * with the real blocking reactor pump. */
    {
        LLVMTypeRef i64d = LLVMInt64TypeInContext(g->ctx);
        LLVMTypeRef i32d = LLVMInt32TypeInContext(g->ctx);
        LLVMTypeRef wc_args[] = { i64d, i32d, i8ptr, g->co_step_ptr };
        g->rt_io_wait_co_type = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), wc_args, 4, 0);
        g->rt_io_wait_co = LLVMAddFunction(g->mod, "zan_io_wait_co", g->rt_io_wait_co_type);
        g->rt_io_pump_type = LLVMFunctionType(i32d, NULL, 0, 0);
        g->rt_io_pump = LLVMAddFunction(g->mod, "zan_io_pump", g->rt_io_pump_type);
        LLVMSetLinkage(g->rt_io_pump, LLVMWeakAnyLinkage);
        LLVMBasicBlockRef pbb = LLVMAppendBasicBlockInContext(g->ctx, g->rt_io_pump, "entry");
        LLVMPositionBuilderAtEnd(g->builder, pbb);
        LLVMBuildRet(g->builder, LLVMConstInt(i32d, 0, 0));
    }
    g->uses_socket_async = false;

    /* Emit the cooperative coroutine driver inline (the whole Zan runtime is
     * emitted into the module, so produced programs are self-contained — see
     * the ARC/List helpers below). The ready queue is a singly-linked FIFO of
     * malloc'd nodes {next, frame, step}; zan_co_ready appends, zan_co_sched_run
     * drains, popping+freeing a node before invoking its step (which may itself
     * enqueue). Semantically equivalent to src/runtime/rt_co.c. */
    {
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
         * singly-linked list of {next, ms, frame, step}. zan_co_delay registers
         * a one-shot timer; zan_co_sched_run, when the ready queue is empty,
         * sleeps for the smallest pending delay, then readies its frame — the
         * "reactor wakes a suspended coroutine later" path. The current (still
         * sequential) await model has at most one pending timer at a time, so a
         * relative millisecond delay is stored directly and no monotonic clock
         * is needed. The blocking sleep is the one platform primitive here, and
         * since zanc's target equals its host it is selected at build time:
         * Sleep (kernel32) on Windows, poll(NULL,0,ms) on POSIX. */
        LLVMTypeRef i64t = LLVMInt64TypeInContext(g->ctx);
        LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
        LLVMTypeRef tnode_fields[] = { i8ptr /*next*/, i64t /*ms*/, i8ptr /*frame*/, g->co_step_ptr /*step*/ };
        LLVMTypeRef tnode_ty = LLVMStructCreateNamed(g->ctx, "zan.co.timer");
        LLVMStructSetBody(tnode_ty, tnode_fields, 4, 0);
        LLVMValueRef g_timers = LLVMAddGlobal(g->mod, i8ptr, "__zan_co_timers");
        LLVMSetInitializer(g_timers, LLVMConstNull(i8ptr));
        LLVMSetLinkage(g_timers, LLVMInternalLinkage);

#if defined(_WIN32)
        LLVMTypeRef sleep_args[] = { i32t };
        LLVMTypeRef sleep_type = LLVMFunctionType(voidt, sleep_args, 1, 0);
        LLVMValueRef fn_sleep = LLVMAddFunction(g->mod, "Sleep", sleep_type);
#else
        LLVMTypeRef poll_args[] = { i8ptr, i64t, i32t };
        LLVMTypeRef poll_type = LLVMFunctionType(i32t, poll_args, 3, 0);
        LLVMValueRef fn_poll = LLVMAddFunction(g->mod, "poll", poll_type);
#endif

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
            LLVMValueRef tn = LLVMBuildCall2(g->builder, malloc_type, g->fn_malloc,
                (LLVMValueRef[]){ LLVMSizeOf(tnode_ty) }, 1, "tn");
            LLVMValueRef old = LLVMBuildLoad2(g->builder, i8ptr, g_timers, "t.head");
            LLVMBuildStore(g->builder, old, LLVMBuildStructGEP2(g->builder, tnode_ty, tn, 0, "tn.next"));
            LLVMBuildStore(g->builder, ms, LLVMBuildStructGEP2(g->builder, tnode_ty, tn, 1, "tn.ms"));
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
            LLVMValueRef node = LLVMBuildCall2(g->builder, malloc_type, g->fn_malloc,
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
         * step. When the queue empties but timers remain, sleep until the
         * earliest deadline, ready that timer's frame, and continue; exit only
         * when both the ready queue and the timer list are empty. */
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
            LLVMBasicBlockRef unlink_head = LLVMAppendBasicBlockInContext(g->ctx, g->rt_co_sched_run, "unlink.head");
            LLVMBasicBlockRef unlink_mid = LLVMAppendBasicBlockInContext(g->ctx, g->rt_co_sched_run, "unlink.mid");
            LLVMBasicBlockRef after_unlink = LLVMAppendBasicBlockInContext(g->ctx, g->rt_co_sched_run, "after.unlink");
            LLVMBasicBlockRef do_sleep = LLVMAppendBasicBlockInContext(g->ctx, g->rt_co_sched_run, "do.sleep");
            LLVMBasicBlockRef after_sleep = LLVMAppendBasicBlockInContext(g->ctx, g->rt_co_sched_run, "after.sleep");
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
            LLVMBuildCall2(g->builder, free_type, g->fn_free,
                (LLVMValueRef[]){ head }, 1, "");
            LLVMBuildCall2(g->builder, g->co_step_type, st,
                (LLVMValueRef[]){ fr }, 1, "");
            LLVMBuildBr(g->builder, head_bb);

            /* ready queue empty: if no timers, we're done; else find the
             * earliest-due timer, unlink it, sleep to its deadline, ready it. */
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
            LLVMValueRef ms_v = LLVMBuildLoad2(g->builder, i64t,
                LLVMBuildStructGEP2(g->builder, tnode_ty, best, 1, "best.msp"), "best.ms");
            LLVMValueRef ms_pos = LLVMBuildICmp(g->builder, LLVMIntSGT, ms_v,
                LLVMConstInt(i64t, 0, 0), "ms.pos");
            LLVMBuildCondBr(g->builder, ms_pos, do_sleep, after_sleep);

            LLVMPositionBuilderAtEnd(g->builder, do_sleep);
            LLVMValueRef ms32 = LLVMBuildTrunc(g->builder, ms_v, i32t, "ms32");
#if defined(_WIN32)
            LLVMBuildCall2(g->builder, sleep_type, fn_sleep,
                (LLVMValueRef[]){ ms32 }, 1, "");
#else
            LLVMBuildCall2(g->builder, poll_type, fn_poll,
                (LLVMValueRef[]){ LLVMConstNull(i8ptr), LLVMConstInt(i64t, 0, 0), ms32 }, 3, "");
#endif
            LLVMBuildBr(g->builder, after_sleep);

            LLVMPositionBuilderAtEnd(g->builder, after_sleep);
            LLVMValueRef bframe = LLVMBuildLoad2(g->builder, i8ptr,
                LLVMBuildStructGEP2(g->builder, tnode_ty, best, 2, "best.framep"), "best.frame");
            LLVMValueRef bstep = LLVMBuildLoad2(g->builder, g->co_step_ptr,
                LLVMBuildStructGEP2(g->builder, tnode_ty, best, 3, "best.stepp"), "best.step");
            LLVMBuildCall2(g->builder, g->rt_co_ready_type, g->rt_co_ready,
                (LLVMValueRef[]){ bframe, bstep }, 2, "");
            LLVMBuildCall2(g->builder, free_type, g->fn_free,
                (LLVMValueRef[]){ best }, 1, "");
            LLVMBuildBr(g->builder, head_bb);

            /* ready queue and timers both empty: block in the IO reactor for
             * the next fd event, which re-readies suspended frames. zan_io_pump
             * returns the number of frames it woke (0 when there is no pending
             * IO — the weak no-op for non-socket programs), so a positive count
             * loops back to drain them and zero means the program is done. */
            LLVMPositionBuilderAtEnd(g->builder, io_bb);
            LLVMValueRef woke = LLVMBuildCall2(g->builder, g->rt_io_pump_type,
                g->rt_io_pump, NULL, 0, "woke");
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
    scope->count++;
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

/* Helper: check if a type is ARC-managed (class instance, not string/int/struct/enum) */
static int is_arc_managed_type(zan_type_t *t) {
    if (!t) return 0;
    if (t->kind == TYPE_CLASS) return 1;
    /* List and Dict are also ARC-managed (heap-allocated with rc header) */
    if (t->name.len == 4 && memcmp(t->name.str, "List", 4) == 0) return 1;
    if (t->name.len == 4 && memcmp(t->name.str, "Dict", 4) == 0) return 1;
    if (t->name.len == 13 && memcmp(t->name.str, "StringBuilder", 13) == 0) return 1;
    return 0;
}

/* Release all ARC-managed local variables in scope (for throw/exception cleanup) */
static void release_all_arc_locals(zan_irgen_t *g, local_scope_t *locals) {
    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    for (int i = 0; i < locals->count; i++) {
        if (is_arc_managed_type(locals->vars[i].type)) {
            LLVMValueRef val = LLVMBuildLoad2(g->builder, i8ptr,
                locals->vars[i].alloca, "arc_cleanup");
            LLVMBuildCall2(g->builder,
                LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), &i8ptr, 1, 0),
                g->rt_release, &val, 1, "");
        }
    }
}

/* Store `value` into element `idx` of a List object. A List is a heap struct
 * { i64 count, i64 cap, i64* data }; elements live in the `data` buffer (field
 * 2), NOT in the struct itself. Every `list[i] = v` form (local, this.field,
 * obj.field) must go through here so it doesn't index into the struct and
 * clobber the count/cap/data words. Elements are i64 slots; pointer and narrow
 * integer values are coerced to i64. */
static void emit_list_element_store(zan_irgen_t *g, LLVMValueRef arr_ptr,
                                    LLVMValueRef idx, LLVMValueRef value) {
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMValueRef list_ptr = LLVMBuildBitCast(g->builder, arr_ptr,
        LLVMPointerType(g->list_struct_type, 0), "lptr");
    LLVMValueRef data_field = LLVMBuildStructGEP2(g->builder,
        g->list_struct_type, list_ptr, 2, "df");
    LLVMValueRef data = LLVMBuildLoad2(g->builder,
        LLVMPointerType(i64, 0), data_field, "data");
    if (LLVMGetTypeKind(LLVMTypeOf(idx)) == LLVMIntegerTypeKind &&
        LLVMGetIntTypeWidth(LLVMTypeOf(idx)) < 64) {
        idx = LLVMBuildSExt(g->builder, idx, i64, "ext");
    }
    LLVMValueRef elem_ptr = LLVMBuildGEP2(g->builder, i64, data, &idx, 1, "ep");
    LLVMValueRef v = value;
    LLVMTypeKind vk = LLVMGetTypeKind(LLVMTypeOf(v));
    if (vk == LLVMPointerTypeKind) {
        v = LLVMBuildPtrToInt(g->builder, v, i64, "p2i");
    } else if (vk == LLVMIntegerTypeKind &&
               LLVMGetIntTypeWidth(LLVMTypeOf(v)) < 64) {
        v = LLVMBuildSExt(g->builder, v, i64, "sx");
    }
    LLVMBuildStore(g->builder, v, elem_ptr);
}

/* ---- expression codegen ---- */

static LLVMValueRef emit_expr(zan_irgen_t *g, zan_ast_node_t *expr, local_scope_t *locals);
static void emit_stmt(zan_irgen_t *g, zan_ast_node_t *stmt, local_scope_t *locals);

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

static zan_type_t *infer_expr_type(zan_irgen_t *g, zan_ast_node_t *e, local_scope_t *locals);

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
    case AST_CALL: {
        /* Resolve the static return type of a method/function call so that
         * chained member access (e.g. Next().field) can find the struct. */
        zan_ast_node_t *callee = e->call.callee;
        if (!callee) return NULL;
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
        if (callee->kind == AST_MEMBER_ACCESS &&
            callee->member.object->kind == AST_IDENTIFIER) {
            /* instance: local.Method() */
            local_var_t *lv = local_find(locals, callee->member.object->ident.name);
            if (lv && lv->type && lv->type->sym) {
                zan_symbol_t *m = get_method_sym(lv->type->sym, callee->member.name);
                if (m) return m->type;
            }
            /* static: ClassName.Method() */
            zan_symbol_t *ts = zan_binder_lookup(g->binder, callee->member.object->ident.name);
            if (ts && (ts->kind == SYM_CLASS || ts->kind == SYM_STRUCT)) {
                zan_symbol_t *m = get_method_sym(ts, callee->member.name);
                if (m) return m->type;
            }
        }
        return NULL;
    }
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

/* Coerce a value to an i8* C string. Pointers pass through unchanged; integer
 * and floating operands are formatted into a fresh heap buffer via snprintf.
 * Lets `+` concatenate strings with numbers (e.g. "%t" + counter). */
static LLVMValueRef emit_to_cstr(zan_irgen_t *g, LLVMValueRef val) {
    LLVMTypeRef vt = LLVMTypeOf(val);
    LLVMTypeKind vtk = LLVMGetTypeKind(vt);
    if (vtk == LLVMPointerTypeKind) return val;

    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
    LLVMTypeRef snprintf_type = LLVMFunctionType(
        LLVMInt32TypeInContext(g->ctx), (LLVMTypeRef[]){ i8ptr, i64, i8ptr }, 3, 1);
    LLVMValueRef fmt;
    LLVMValueRef arg;
    if (vtk == LLVMDoubleTypeKind || vtk == LLVMFloatTypeKind) {
        fmt = LLVMBuildGlobalStringPtr(g->builder, "%g", "dfmt");
        arg = val;
    } else {
        fmt = LLVMBuildGlobalStringPtr(g->builder, "%lld", "ifmt");
        arg = val;
        if (LLVMGetIntTypeWidth(vt) < 64) arg = LLVMBuildSExt(g->builder, val, i64, "ext");
    }
    LLVMValueRef null_ptr = LLVMConstNull(i8ptr);
    LLVMValueRef zero = LLVMConstInt(i64, 0, 0);
    LLVMValueRef a1[] = { null_ptr, zero, fmt, arg };
    LLVMValueRef needed = LLVMBuildCall2(g->builder, snprintf_type, g->fn_snprintf, a1, 4, "needed");
    LLVMValueRef needed64 = LLVMBuildSExt(g->builder, needed, i64, "n64");
    LLVMValueRef bsz = LLVMBuildAdd(g->builder, needed64, LLVMConstInt(i64, 1, 0), "bsz");
    LLVMValueRef buf = LLVMBuildCall2(g->builder,
        LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i64 }, 1, 0), g->fn_malloc, &bsz, 1, "nbuf");
    LLVMValueRef a2[] = { buf, bsz, fmt, arg };
    LLVMBuildCall2(g->builder, snprintf_type, g->fn_snprintf, a2, 4, "");
    return buf;
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

        /* Operator overloading: if left operand is a user class instance,
         * look for a static op_add/op_sub/etc method and call it. */
        {
            zan_type_t *ltype = infer_expr_type(g, expr->binary.left, locals);
            if (ltype && ltype->kind == TYPE_CLASS && ltype->sym) {
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
                                return LLVMBuildCall2(g->builder, g->functions[fi].fn_type,
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
         * "%t" + counter), so concat is not limited to two pointers. */
        if (expr->binary.op == TK_PLUS && str_operand) {
            return emit_str_concat(g, emit_to_cstr(g, left), emit_to_cstr(g, right));
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
                LLVMValueRef sv = coerce_int_to(g, right,
                    LLVMGetAllocatedType(local->alloca));
                LLVMBuildStore(g->builder, sv, local->alloca);
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
                    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                    LLVMValueRef raw = LLVMBuildLoad2(g->builder, i8ptr, local->alloca, "lraw");
                    LLVMValueRef idx = emit_expr(g, expr->binary.left->index.index, locals);
                    emit_list_element_store(g, raw, idx, right);
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
                            elem_llvm = map_type(g, local->type->element_type);
                        }
                        LLVMValueRef elem_ptr = LLVMBuildGEP2(g->builder, elem_llvm, arr_ptr, &idx, 1, "eidx");
                        LLVMBuildStore(g->builder, right, elem_ptr);
                    }
                } else if (g->current_type_sym) {
                    /* implicit this.field[i] = value */
                    zan_symbol_t *fsym = get_field_sym(g->current_type_sym, arr_expr->ident.name);
                    if (fsym) {
                        LLVMValueRef arr_ptr = emit_expr(g, arr_expr, locals);
                        LLVMValueRef idx = emit_expr(g, expr->binary.left->index.index, locals);
                        if (fsym->type && fsym->type->name.len == 4 &&
                            memcmp(fsym->type->name.str, "List", 4) == 0) {
                            emit_list_element_store(g, arr_ptr, idx, right);
                        } else if (fsym->type && fsym->type->kind == TYPE_STRING) {
                            LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
                            LLVMValueRef elem_ptr = LLVMBuildGEP2(g->builder, i8, arr_ptr, &idx, 1, "eidx");
                            LLVMValueRef val8 = LLVMBuildTrunc(g->builder, right, i8, "byte");
                            LLVMBuildStore(g->builder, val8, elem_ptr);
                        } else {
                            LLVMTypeRef elem_llvm = LLVMInt32TypeInContext(g->ctx);
                            if (fsym->type && fsym->type->element_type) {
                                elem_llvm = map_type(g, fsym->type->element_type);
                            }
                            LLVMValueRef elem_ptr = LLVMBuildGEP2(g->builder, elem_llvm, arr_ptr, &idx, 1, "eidx");
                            LLVMBuildStore(g->builder, right, elem_ptr);
                        }
                    }
                }
            } else if (arr_expr->kind == AST_MEMBER_ACCESS) {
                /* obj.field[i] = value — array stored in a struct/class field */
                zan_type_t *at = member_access_field_type(locals, arr_expr);
                if (at) {
                    LLVMValueRef arr_ptr = emit_expr(g, arr_expr, locals);
                    LLVMValueRef idx = emit_expr(g, expr->binary.left->index.index, locals);
                    if (at->name.len == 4 && memcmp(at->name.str, "List", 4) == 0) {
                        /* List field: index into the data buffer, not the
                         * struct — otherwise the store clobbers count/cap/data. */
                        emit_list_element_store(g, arr_ptr, idx, right);
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
                        LLVMValueRef elem_ptr = LLVMBuildGEP2(g->builder, elem_llvm, arr_ptr, &idx, 1, "eidx");
                        LLVMBuildStore(g->builder, right, elem_ptr);
                    }
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
                if (sbm.len == 6 && memcmp(sbm.str, "Append", 6) == 0 &&
                    expr->call.args.count == 1) {
                    LLVMValueRef s = emit_expr(g, expr->call.args.items[0], locals);
                    LLVMValueRef slen = LLVMBuildCall2(g->builder,
                        LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr }, 1, 0),
                        g->fn_strlen, &s, 1, "sblen");
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
                    LLVMValueRef newdata = LLVMBuildCall2(g->builder,
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
                    if (!memcpy_fn) {
                        memcpy_fn = LLVMAddFunction(g->mod, "memcpy",
                            LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr, i64 }, 3, 0));
                    }
                    LLVMBuildCall2(g->builder,
                        LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr, i64 }, 3, 0),
                        memcpy_fn, (LLVMValueRef[]){ dest, s, slen }, 3, "");
                    LLVMValueRef ncount = LLVMBuildAdd(g->builder, count, slen, "sbncount");
                    LLVMBuildStore(g->builder, ncount, cptr);
                    return raw;
                }
                if (sbm.len == 8 && memcmp(sbm.str, "ToString", 8) == 0 &&
                    expr->call.args.count == 0) {
                    LLVMValueRef cptr = LLVMBuildStructGEP2(g->builder, g->sb_struct_type, sbp, 0, "sbcp");
                    LLVMValueRef count = LLVMBuildLoad2(g->builder, i64, cptr, "sbcv");
                    LLVMValueRef dptr = LLVMBuildStructGEP2(g->builder, g->sb_struct_type, sbp, 2, "sbdp");
                    LLVMValueRef data = LLVMBuildLoad2(g->builder, i8ptr, dptr, "sbdv");
                    LLVMValueRef bsz = LLVMBuildAdd(g->builder, count, LLVMConstInt(i64, 1, 0), "sbbsz");
                    LLVMValueRef buf = LLVMBuildCall2(g->builder,
                        LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i64 }, 1, 0),
                        g->fn_malloc, &bsz, 1, "sbbuf");
                    LLVMValueRef memcpy_fn = LLVMGetNamedFunction(g->mod, "memcpy");
                    if (!memcpy_fn) {
                        memcpy_fn = LLVMAddFunction(g->mod, "memcpy",
                            LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr, i64 }, 3, 0));
                    }
                    LLVMBuildCall2(g->builder,
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
                    LLVMValueRef total = LLVMBuildCall2(g->builder,
                        LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr }, 1, 0),
                        g->fn_strlen, &s, 1, "slen");
                    slen = LLVMBuildSub(g->builder, total, start, "subl");
                }
                LLVMValueRef bufsz = LLVMBuildAdd(g->builder, slen, LLVMConstInt(i64, 1, 0), "bsz");
                LLVMValueRef buf = LLVMBuildCall2(g->builder,
                    LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i64 }, 1, 0),
                    g->fn_malloc, &bufsz, 1, "sub");
                LLVMValueRef srcp = LLVMBuildGEP2(g->builder, i8, s, &start, 1, "srcp");
                LLVMValueRef memcpy_fn = LLVMGetNamedFunction(g->mod, "memcpy");
                if (!memcpy_fn) {
                    memcpy_fn = LLVMAddFunction(g->mod, "memcpy",
                        LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr, i64 }, 3, 0));
                }
                LLVMValueRef mcargs[] = { buf, srcp, slen };
                LLVMBuildCall2(g->builder,
                    LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr, i64 }, 3, 0),
                    memcpy_fn, mcargs, 3, "");
                LLVMValueRef endp = LLVMBuildGEP2(g->builder, i8, buf, &slen, 1, "endp");
                LLVMBuildStore(g->builder, LLVMConstInt(i8, 0, 0), endp);
                return buf;
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
            LLVMValueRef idx = emit_expr(g, expr->call.args.items[0], locals);
            LLVMValueRef idx1 = LLVMBuildAdd(g->builder, idx, LLVMConstInt(i64, 1, 0), "argi");
            LLVMValueRef g_argv = LLVMGetNamedGlobal(g->mod, "__zan_argv");
            if (!g_argv) {
                g_argv = LLVMAddGlobal(g->mod, i8ptrptr, "__zan_argv");
                LLVMSetInitializer(g_argv, LLVMConstNull(i8ptrptr));
            }
            LLVMValueRef argv = LLVMBuildLoad2(g->builder, i8ptrptr, g_argv, "argv");
            LLVMValueRef slot = LLVMBuildGEP2(g->builder, i8ptr, argv, &idx1, 1, "argslot");
            return LLVMBuildLoad2(g->builder, i8ptr, slot, "arg");
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
            LLVMValueRef fp = LLVMBuildCall2(g->builder,
                LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0),
                fopen_fn, open_args, 2, "fp");
            LLVMValueRef is_null = LLVMBuildICmp(g->builder, LLVMIntNE, fp,
                LLVMConstNull(i8ptr), "exists");
            /* close if opened */
            LLVMBasicBlockRef close_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "fexist.close");
            LLVMBasicBlockRef end_bb = LLVMAppendBasicBlockInContext(g->ctx, g->current_fn, "fexist.end");
            LLVMBuildCondBr(g->builder, is_null, close_bb, end_bb);
            LLVMPositionBuilderAtEnd(g->builder, close_bb);
            LLVMBuildCall2(g->builder, LLVMFunctionType(LLVMInt32TypeInContext(g->ctx), (LLVMTypeRef[]){ i8ptr }, 1, 0),
                fclose_fn, &fp, 1, "");
            LLVMBuildBr(g->builder, end_bb);
            LLVMPositionBuilderAtEnd(g->builder, end_bb);
            LLVMValueRef result = LLVMBuildZExt(g->builder, is_null, LLVMInt64TypeInContext(g->ctx), "fex");
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
            LLVMBuildCall2(g->builder, LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr }, 1, 0),
                remove_fn, &path_arg, 1, "");
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
            LLVMBuildCall2(g->builder, LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0),
                rename_fn, args, 2, "");
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
            LLVMValueRef sfp = LLVMBuildCall2(g->builder,
                LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0),
                fopen_fn, sargs, 2, "sfp");
            /* get size */
            LLVMValueRef seek_end[] = { sfp, LLVMConstInt(i64, 0, 0), LLVMConstInt(i32, 2, 0) };
            LLVMBuildCall2(g->builder, LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr, i64, i32 }, 3, 0),
                fseek_fn, seek_end, 3, "");
            LLVMValueRef sz = LLVMBuildCall2(g->builder,
                LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr }, 1, 0), ftell_fn, &sfp, 1, "sz");
            LLVMValueRef seek_start[] = { sfp, LLVMConstInt(i64, 0, 0), LLVMConstInt(i32, 0, 0) };
            LLVMBuildCall2(g->builder, LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr, i64, i32 }, 3, 0),
                fseek_fn, seek_start, 3, "");
            /* allocate buffer */
            LLVMValueRef buf = LLVMBuildCall2(g->builder,
                LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i64 }, 1, 0), g->fn_malloc, &sz, 1, "cbuf");
            /* read */
            LLVMValueRef fread_args[] = { buf, LLVMConstInt(i64, 1, 0), sz, sfp };
            LLVMBuildCall2(g->builder, LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr, i64, i64, i8ptr }, 4, 0),
                fread_fn, fread_args, 4, "");
            LLVMBuildCall2(g->builder, LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr }, 1, 0),
                fclose_fn, &sfp, 1, "");
            /* open dest for writing */
            LLVMValueRef wb = LLVMBuildGlobalStringPtr(g->builder, "wb", "wb");
            LLVMValueRef dargs[] = { dst, wb };
            LLVMValueRef dfp = LLVMBuildCall2(g->builder,
                LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0),
                fopen_fn, dargs, 2, "dfp");
            /* write */
            LLVMValueRef fwrite_args[] = { buf, LLVMConstInt(i64, 1, 0), sz, dfp };
            LLVMBuildCall2(g->builder, LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr, i64, i64, i8ptr }, 4, 0),
                fwrite_fn, fwrite_args, 4, "");
            LLVMBuildCall2(g->builder, LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr }, 1, 0),
                fclose_fn, &dfp, 1, "");
            /* free buffer */
            LLVMBuildCall2(g->builder, LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), (LLVMTypeRef[]){ i8ptr }, 1, 0),
                g->fn_free, &buf, 1, "");
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
            LLVMValueRef fp = LLVMBuildCall2(g->builder,
                LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0),
                fopen_fn, open_args, 2, "fp");
            LLVMValueRef seek_end[] = { fp, LLVMConstInt(i64, 0, 0), LLVMConstInt(i32, 2, 0) };
            LLVMBuildCall2(g->builder, LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr, i64, i32 }, 3, 0),
                fseek_fn, seek_end, 3, "");
            LLVMValueRef sz = LLVMBuildCall2(g->builder,
                LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr }, 1, 0), ftell_fn, &fp, 1, "fsz");
            LLVMBuildCall2(g->builder, LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr }, 1, 0),
                fclose_fn, &fp, 1, "");
            return sz;
        }

        /* Directory.Exists(path) -> bool */
        if (is_call_to(expr, "Directory", "Exists") && expr->call.args.count == 1) {
            LLVMValueRef path_arg = emit_expr(g, expr->call.args.items[0], locals);
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
            LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
            /* Use GetFileAttributesA on Windows, stat on POSIX */
            LLVMValueRef gfa_fn = LLVMGetNamedFunction(g->mod, "GetFileAttributesA");
            if (!gfa_fn) {
                LLVMTypeRef gfa_type = LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr }, 1, 0);
                gfa_fn = LLVMAddFunction(g->mod, "GetFileAttributesA", gfa_type);
            }
            LLVMValueRef attrs = LLVMBuildCall2(g->builder,
                LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr }, 1, 0),
                gfa_fn, &path_arg, 1, "attrs");
            /* INVALID_FILE_ATTRIBUTES = -1, FILE_ATTRIBUTE_DIRECTORY = 0x10 */
            LLVMValueRef not_invalid = LLVMBuildICmp(g->builder, LLVMIntNE, attrs,
                LLVMConstInt(i32, 0xFFFFFFFF, 0), "noinv");
            LLVMValueRef is_dir = LLVMBuildAnd(g->builder, attrs,
                LLVMConstInt(i32, 0x10, 0), "isdir");
            LLVMValueRef is_dir_bool = LLVMBuildICmp(g->builder, LLVMIntNE, is_dir,
                LLVMConstInt(i32, 0, 0), "isdirb");
            LLVMValueRef result = LLVMBuildAnd(g->builder, not_invalid, is_dir_bool, "dexist");
            return LLVMBuildZExt(g->builder, result, i64, "dex");
        }

        /* Directory.CreateDirectory(path) */
        if (is_call_to(expr, "Directory", "CreateDirectory") && expr->call.args.count == 1) {
            LLVMValueRef path_arg = emit_expr(g, expr->call.args.items[0], locals);
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
            LLVMValueRef mkdir_fn = LLVMGetNamedFunction(g->mod, "_mkdir");
            if (!mkdir_fn) {
                LLVMTypeRef ft = LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr }, 1, 0);
                mkdir_fn = LLVMAddFunction(g->mod, "_mkdir", ft);
            }
            LLVMBuildCall2(g->builder, LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr }, 1, 0),
                mkdir_fn, &path_arg, 1, "");
            return LLVMConstInt(i32, 0, 0);
        }

        /* Directory.Delete(path) */
        if (is_call_to(expr, "Directory", "Delete") && expr->call.args.count == 1) {
            LLVMValueRef path_arg = emit_expr(g, expr->call.args.items[0], locals);
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
            LLVMValueRef rmdir_fn = LLVMGetNamedFunction(g->mod, "_rmdir");
            if (!rmdir_fn) {
                LLVMTypeRef ft = LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr }, 1, 0);
                rmdir_fn = LLVMAddFunction(g->mod, "_rmdir", ft);
            }
            LLVMBuildCall2(g->builder, LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr }, 1, 0),
                rmdir_fn, &path_arg, 1, "");
            return LLVMConstInt(i32, 0, 0);
        }

        /* Directory.GetCurrentDirectory() -> string */
        if (is_call_to(expr, "Directory", "GetCurrentDirectory") && expr->call.args.count == 0) {
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
            LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
            LLVMValueRef buf = LLVMBuildCall2(g->builder,
                LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i64 }, 1, 0),
                g->fn_malloc, &(LLVMValueRef){LLVMConstInt(i64, 4096, 0)}, 1, "cwdbuf");
            LLVMValueRef getcwd_fn = LLVMGetNamedFunction(g->mod, "_getcwd");
            if (!getcwd_fn) {
                LLVMTypeRef ft = LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i32 }, 2, 0);
                getcwd_fn = LLVMAddFunction(g->mod, "_getcwd", ft);
            }
            LLVMValueRef cwd_args[] = { buf, LLVMConstInt(i32, 4096, 0) };
            LLVMBuildCall2(g->builder, LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i32 }, 2, 0),
                getcwd_fn, cwd_args, 2, "");
            return buf;
        }

        /* Directory.SetCurrentDirectory(path) */
        if (is_call_to(expr, "Directory", "SetCurrentDirectory") && expr->call.args.count == 1) {
            LLVMValueRef path_arg = emit_expr(g, expr->call.args.items[0], locals);
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
            LLVMValueRef chdir_fn = LLVMGetNamedFunction(g->mod, "_chdir");
            if (!chdir_fn) {
                LLVMTypeRef ft = LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr }, 1, 0);
                chdir_fn = LLVMAddFunction(g->mod, "_chdir", ft);
            }
            LLVMBuildCall2(g->builder, LLVMFunctionType(i32, (LLVMTypeRef[]){ i8ptr }, 1, 0),
                chdir_fn, &path_arg, 1, "");
            return LLVMConstInt(i32, 0, 0);
        }

        /* Path.GetDirectoryName(path) -> string */
        if (is_call_to(expr, "Path", "GetDirectoryName") && expr->call.args.count == 1) {
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
            LLVMTypeRef i8 = LLVMInt8TypeInContext(g->ctx);
            LLVMValueRef path_val = emit_expr(g, expr->call.args.items[0], locals);
            /* strlen */
            LLVMValueRef len = LLVMBuildCall2(g->builder,
                LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr }, 1, 0), g->fn_strlen, &path_val, 1, "plen");
            /* allocate copy */
            LLVMValueRef bsz = LLVMBuildAdd(g->builder, len, LLVMConstInt(i64, 1, 0), "bsz");
            LLVMValueRef buf = LLVMBuildCall2(g->builder,
                LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i64 }, 1, 0), g->fn_malloc, &bsz, 1, "dnbuf");
            LLVMValueRef cpy_args[] = { buf, path_val };
            LLVMBuildCall2(g->builder, LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0),
                g->fn_strcpy, cpy_args, 2, "");
            /* find last separator */
            LLVMTypeRef i32t = LLVMInt32TypeInContext(g->ctx);
            LLVMValueRef strrchr_fn = LLVMGetNamedFunction(g->mod, "strrchr");
            LLVMValueRef slash_args[] = { buf, LLVMConstInt(i32t, '/', 0) };
            LLVMValueRef slash = LLVMBuildCall2(g->builder,
                LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i32t }, 2, 0),
                strrchr_fn, slash_args, 2, "sl");
            LLVMValueRef bslash_args[] = { buf, LLVMConstInt(i32t, 92, 0) };
            LLVMValueRef bslash = LLVMBuildCall2(g->builder,
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
            LLVMValueRef dot = LLVMBuildCall2(g->builder,
                LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i32t }, 2, 0),
                strrchr_fn, dot_args, 2, "dot");
            LLVMValueRef has = LLVMBuildICmp(g->builder, LLVMIntNE, dot, LLVMConstNull(i8ptr), "hasext");
            return LLVMBuildZExt(g->builder, has, i64, "he");
        }

        /* Path.GetTempPath() -> string */
        if (is_call_to(expr, "Path", "GetTempPath") && expr->call.args.count == 0) {
            LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
            LLVMTypeRef i32 = LLVMInt32TypeInContext(g->ctx);
            LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
            LLVMValueRef buf = LLVMBuildCall2(g->builder,
                LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i64 }, 1, 0),
                g->fn_malloc, &(LLVMValueRef){LLVMConstInt(i64, 260, 0)}, 1, "tmpbuf");
            LLVMValueRef gtp_fn = LLVMGetNamedFunction(g->mod, "GetTempPathA");
            if (!gtp_fn) {
                LLVMTypeRef ft = LLVMFunctionType(i32, (LLVMTypeRef[]){ i32, i8ptr }, 2, 0);
                gtp_fn = LLVMAddFunction(g->mod, "GetTempPathA", ft);
            }
            LLVMValueRef gtp_args[] = { LLVMConstInt(i32, 260, 0), buf };
            LLVMBuildCall2(g->builder, LLVMFunctionType(i32, (LLVMTypeRef[]){ i32, i8ptr }, 2, 0),
                gtp_fn, gtp_args, 2, "");
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
            LLVMValueRef slash = LLVMBuildCall2(g->builder,
                LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i32t }, 2, 0),
                strrchr_fn, slash_args, 2, "sl");
            LLVMValueRef bslash_args[] = { path_val, LLVMConstInt(i32t, 92, 0) };
            LLVMValueRef bslash = LLVMBuildCall2(g->builder,
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
            LLVMValueRef flen = LLVMBuildCall2(g->builder,
                LLVMFunctionType(i64, (LLVMTypeRef[]){ i8ptr }, 1, 0), g->fn_strlen, &fname, 1, "flen");
            LLVMValueRef bsz = LLVMBuildAdd(g->builder, flen, LLVMConstInt(i64, 1, 0), "bsz");
            LLVMValueRef buf = LLVMBuildCall2(g->builder,
                LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i64 }, 1, 0), g->fn_malloc, &bsz, 1, "fnbuf");
            LLVMValueRef cpy_args[] = { buf, fname };
            LLVMBuildCall2(g->builder, LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0),
                g->fn_strcpy, cpy_args, 2, "");
            LLVMValueRef dot_args[] = { buf, LLVMConstInt(i32t, '.', 0) };
            LLVMValueRef dot = LLVMBuildCall2(g->builder,
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
                    LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                    LLVMValueRef raw_ptr = emit_expr(g, lobj, locals);
                    LLVMValueRef list_ptr = LLVMBuildBitCast(g->builder, raw_ptr,
                        LLVMPointerType(g->list_struct_type, 0), "lptr");
                    LLVMValueRef count_ptr = LLVMBuildStructGEP2(g->builder, g->list_struct_type, list_ptr, 0, "cntp");
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
                    if (elem_type && elem_type->kind == TYPE_STRING) {
                        LLVMValueRef sval = LLVMBuildIntToPtr(g->builder, val, i8ptr, "sptr");
                        LLVMValueRef cmp_args[] = { sval, search };
                        LLVMValueRef c = LLVMBuildCall2(g->builder,
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
                    if (elem_type && elem_type->kind == TYPE_STRING) {
                        LLVMValueRef sval = LLVMBuildIntToPtr(g->builder, val, i8ptr, "sptr");
                        LLVMValueRef cmp_args[] = { sval, search };
                        LLVMValueRef c = LLVMBuildCall2(g->builder,
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
                    LLVMValueRef new_data = LLVMBuildCall2(g->builder,
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
                    /* store item at index */
                    LLVMValueRef ins_slot = LLVMBuildGEP2(g->builder, i64, phi_data, &idx, 1, "is");
                    LLVMBuildStore(g->builder, item, ins_slot);
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
                        LLVMValueRef search = emit_expr(g, expr->call.args.items[0], locals);
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
                        LLVMValueRef cmp_args[] = { kv, search };
                        LLVMValueRef cmp = LLVMBuildCall2(g->builder,
                            LLVMFunctionType(i32t, (LLVMTypeRef[]){ i8ptr, i8ptr }, 2, 0),
                            g->fn_strcmp, cmp_args, 2, "cmp");
                        LLVMBuildCondBr(g->builder, LLVMBuildICmp(g->builder, LLVMIntEQ, cmp, LLVMConstInt(i32t, 0, 0), "eq"),
                            found_bb, next_bb);
                        /* found: shift remaining entries left */
                        LLVMPositionBuilderAtEnd(g->builder, found_bb);
                        LLVMValueRef fi = LLVMBuildLoad2(g->builder, i64, idx_a, "fi");
                        LLVMValueRef last = LLVMBuildSub(g->builder, cnt, LLVMConstInt(i64, 1, 0), "last");
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

        /* general instance method call: <expr>.Method(args) where <expr> is
         * any expression of class/struct type (a field, this.field, an index
         * or a call result) — not just a local variable or class name. */
        if (expr->call.callee && expr->call.callee->kind == AST_MEMBER_ACCESS) {
            zan_ast_node_t *callee = expr->call.callee;
            zan_symbol_t *recv_cls = expr_class_sym(g, callee->member.object, locals);
            if (recv_cls) {
                zan_symbol_t *method_sym = get_method_sym(recv_cls, callee->member.name);
                if (method_sym) {
                    for (int fi = 0; fi < g->function_count; fi++) {
                        if (g->functions[fi].sym == method_sym) {
                            int argc = expr->call.args.count + 1;
                            LLVMValueRef *call_args = (LLVMValueRef *)calloc((size_t)argc, sizeof(LLVMValueRef));
                            /* receiver: the object pointer produced by the
                             * expression (field load, index, call, ...). */
                            call_args[0] = emit_expr(g, callee->member.object, locals);
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
        }

        /* bare function name call: Compute(21) → look up in current class then global */
        if (expr->call.callee && expr->call.callee->kind == AST_IDENTIFIER) {
            zan_istr_t fn_name = expr->call.callee->ident.name;

            /* delegate variable invocation: handler(args) where handler is a local
             * of delegate type → load function pointer and do indirect call */
            {
                local_var_t *lv = local_find(locals, fn_name);
                if (lv && lv->type && lv->type->kind == TYPE_DELEGATE) {
                    LLVMTypeRef fn_ptr_type = map_type(g, lv->type);
                    LLVMValueRef fn_ptr = LLVMBuildLoad2(g->builder, fn_ptr_type, lv->alloca, "dlg_ptr");
                    int pc = lv->type->delegate_param_count;
                    LLVMTypeRef *param_types = (LLVMTypeRef *)calloc(
                        (size_t)(pc > 0 ? pc : 1), sizeof(LLVMTypeRef));
                    for (int k = 0; k < pc; k++) {
                        param_types[k] = map_type(g, lv->type->delegate_param_types[k]);
                    }
                    LLVMTypeRef ret = lv->type->delegate_ret_type
                        ? map_type(g, lv->type->delegate_ret_type)
                        : LLVMVoidTypeInContext(g->ctx);
                    LLVMTypeRef fn_type = LLVMFunctionType(ret, param_types, (unsigned)pc, 0);
                    int argc = expr->call.args.count;
                    LLVMValueRef *call_args = (LLVMValueRef *)calloc(
                        (size_t)(argc > 0 ? argc : 1), sizeof(LLVMValueRef));
                    for (int k = 0; k < argc; k++) {
                        call_args[k] = emit_expr(g, expr->call.args.items[k], locals);
                    }
                    const char *cn = (LLVMGetTypeKind(ret) == LLVMVoidTypeKind) ? "" : "dlgcall";
                    LLVMValueRef result = LLVMBuildCall2(g->builder, fn_type,
                        fn_ptr, call_args, (unsigned)argc, cn);
                    free(call_args);
                    free(param_types);
                    return result;
                }
            }

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
                /* strlen returns i64; `int` is i64 so return it directly. */
                return LLVMBuildCall2(g->builder, strlen_type, g->fn_strlen, &obj_val, 1, "len");
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

        /* new StringBuilder() — built-in growable byte buffer */
        if (expr->new_expr.type && expr->new_expr.type->kind == AST_TYPE_REF) {
            zan_istr_t sbname = expr->new_expr.type->type_ref.name;
            if (sbname.len == 13 && memcmp(sbname.str, "StringBuilder", 13) == 0) {
                LLVMTypeRef i64 = LLVMInt64TypeInContext(g->ctx);
                LLVMTypeRef i8ptr = LLVMPointerType(LLVMInt8TypeInContext(g->ctx), 0);
                /* allocate StringBuilder struct: { i64 count, i64 cap, i8* data } = 24 bytes */
                LLVMValueRef sb_size = LLVMConstInt(i64, 24, 0);
                LLVMValueRef sb_raw = LLVMBuildCall2(g->builder,
                    LLVMFunctionType(i8ptr, (LLVMTypeRef[]){ i64 }, 1, 0),
                    g->fn_malloc, &sb_size, 1, "sb");
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
                LLVMBuildCall2(g->builder, g->rt_co_delay_type, g->rt_co_delay,
                    (LLVMValueRef[]){ ms, self_i8, g->current_async_resume_fn }, 3, "");
                emit_async_save_slots(g);
                LLVMBuildStore(g->builder, LLVMConstInt(di32, (unsigned)k, 0),
                    LLVMBuildStructGEP2(g->builder, self_ft, selfframe, ASYNC_FRAME_STATE, "self.state"));
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
#if defined(_WIN32)
            LLVMValueRef fn_sleep = LLVMGetNamedFunction(g->mod, "Sleep");
            if (fn_sleep) {
                LLVMTypeRef sl_args[] = { di32 };
                LLVMTypeRef sl_ty = LLVMFunctionType(LLVMVoidTypeInContext(g->ctx), sl_args, 1, 0);
                LLVMBuildCall2(g->builder, sl_ty, fn_sleep, (LLVMValueRef[]){ ms32 }, 1, "");
            }
#else
            LLVMValueRef fn_poll = LLVMGetNamedFunction(g->mod, "poll");
            if (fn_poll) {
                LLVMTypeRef pl_args[] = { di8ptr, di64, di32 };
                LLVMTypeRef pl_ty = LLVMFunctionType(di32, pl_args, 3, 0);
                LLVMBuildCall2(g->builder, pl_ty, fn_poll,
                    (LLVMValueRef[]){ LLVMConstNull(di8ptr), LLVMConstInt(di64, 0, 0), ms32 }, 3, "");
            }
#endif
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
                LLVMBuildCall2(g->builder, g->rt_io_wait_co_type, g->rt_io_wait_co,
                    (LLVMValueRef[]){ fd, interest, self_i8, g->current_async_resume_fn }, 4, "");
                emit_async_save_slots(g);
                LLVMBuildStore(g->builder, LLVMConstInt(di32, (unsigned)k, 0),
                    LLVMBuildStructGEP2(g->builder, self_ft, selfframe, ASYNC_FRAME_STATE, "self.state"));
                LLVMBuildRetVoid(g->builder);

                LLVMBasicBlockRef rk = LLVMAppendBasicBlockInContext(g->ctx,
                    g->current_async_resume_fn, "co.resume");
                LLVMAddCase(g->current_async_switch, LLVMConstInt(di32, (unsigned)k, 0), rk);
                LLVMPositionBuilderAtEnd(g->builder, rk);
                emit_async_reload_slots(g);
                return LLVMConstInt(di64, 0, 0);
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

                /* schedule the sub, then SUSPEND self */
                LLVMValueRef sched_args[] = { sub_i8, sub_resume };
                LLVMBuildCall2(g->builder, g->rt_co_ready_type, g->rt_co_ready, sched_args, 2, "");
                emit_async_save_slots(g);
                LLVMBuildStore(g->builder, LLVMConstInt(i32, (unsigned)k, 0),
                    LLVMBuildStructGEP2(g->builder, self_ft, selfframe, ASYNC_FRAME_STATE, "self.state"));
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
                return LLVMBuildLoad2(g->builder, i64, rptr, "awres");
            }

            /* root drive (non-async caller) */
            LLVMBuildCall2(g->builder, g->rt_co_sched_init_type, g->rt_co_sched_init, NULL, 0, "");
            LLVMValueRef sched_args[] = { sub_i8, sub_resume };
            LLVMBuildCall2(g->builder, g->rt_co_ready_type, g->rt_co_ready, sched_args, 2, "");
            LLVMBuildCall2(g->builder, g->rt_co_sched_run_type, g->rt_co_sched_run, NULL, 0, "");
            LLVMValueRef rptr = LLVMBuildStructGEP2(g->builder, hdr, sub_i8,
                ASYNC_FRAME_RESULT, "sub.result");
            return LLVMBuildLoad2(g->builder, i64, rptr, "awres");
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
        local_scope_init(&lambda_locals, g->arena);
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
static void emit_async_complete(zan_irgen_t *g, LLVMValueRef result_i64) {
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
    LLVMBuildCall2(g->builder, g->rt_co_ready_type, g->rt_co_ready, wake_args, 2, "");
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
                if (async_type_is_scalar(lt))
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
        /* In an async $resume body, named scalar locals were pre-allocated in
         * the entry block and their storage lives in the heap frame (so they
         * survive suspensions). Reuse that slot instead of a fresh alloca:
         * just evaluate the initializer and store into it. */
        if (g->current_async_frame && g->current_async_slot_count > 0) {
            local_var_t *pre = local_find(locals, stmt->var_decl.name);
            if (pre) {
                for (int i = 0; i < g->current_async_slot_count; i++) {
                    if (g->current_async_slots[i].slot_alloca == pre->alloca) {
                        if (stmt->var_decl.initializer) {
                            LLVMValueRef iv = emit_expr(g, stmt->var_decl.initializer, locals);
                            iv = coerce_int_to(g, iv, g->current_async_slots[i].llvm);
                            LLVMBuildStore(g->builder, iv, pre->alloca);
                        }
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
            init_val = coerce_int_to(g, init_val, llvm_type);
            LLVMBuildStore(g->builder, init_val, alloca);
        }

        local_add(locals, stmt->var_decl.name, alloca, type);
        break;
    }

    case AST_EXPR_STMT:
        emit_expr(g, stmt->expr_stmt.expr, locals);
        break;

    case AST_RETURN_STMT:
        /* Inside an async $resume body, `return e` completes the state machine:
         * store the result into the frame, mark it done, and `ret void` — the
         * frame pointer (Task) was already handed out by the ramp. (The awaiter
         * wake is added in S3 with the await protocol.) */
        if (g->current_async_frame) {
            LLVMValueRef ri = NULL;
            if (stmt->ret.value) {
                ri = coerce_to_i64(g, emit_expr(g, stmt->ret.value, locals));
            }
            emit_async_complete(g, ri);
            break;
        }
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
        /* try/catch: emit try body; if a catch clause exists, emit catch body.
         * Currently uses simple structured approach: try body executes normally,
         * catch is emitted after try body (for structured exception flow).
         * Full LLVM landingpad-based unwinding is deferred to M3+. */
        emit_stmt(g, stmt->try_stmt.try_body, locals);
        /* emit catch body if present (reached via structured jump) */
        if (stmt->try_stmt.catches.count > 0) {
            LLVMBasicBlockRef try_end_bb = LLVMAppendBasicBlockInContext(g->ctx,
                g->current_fn, "try.end");
            if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder))) {
                LLVMBuildBr(g->builder, try_end_bb);
            }
            /* catch block: skip for now (no runtime path reaches it without landing pad),
             * but at least emit finally if present */
            LLVMPositionBuilderAtEnd(g->builder, try_end_bb);
        }
        /* emit finally body if present */
        if (stmt->try_stmt.finally_body) {
            emit_stmt(g, stmt->try_stmt.finally_body, locals);
        }
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
        /* throw expr; — release ARC locals, print error, and exit */
        LLVMValueRef val = emit_expr(g, stmt->throw_stmt.value, locals);
        /* ARC cleanup: release all managed locals before aborting */
        release_all_arc_locals(g, locals);
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
    /* async CPS lowering: when is_async, `fn` is the ramp (returns the task
     * handle) and the body is emitted into `resume_fn` over `frame_type`. */
    bool            is_async;
    LLVMValueRef    resume_fn;
    LLVMTypeRef     frame_type;
    int             await_count;   /* await points in the body (states 1..N) */
    async_local_t  *alocals;       /* named scalar locals held in the frame */
    int             alocal_count;
    int             sub_base;       /* frame index of the first sub-task slot */
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
                /* Reuse existing declaration if the symbol already exists in the module
                 * (e.g. built-in malloc/free/strlen, or duplicate DllImport across files). */
                LLVMValueRef efn = LLVMGetNamedFunction(g->mod, ext_name);
                if (!efn) {
                    efn = LLVMAddFunction(g->mod, ext_name, ft);
                }
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
             * the callable symbol (a call site receives the task handle). */
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
                work[work_count].is_async = is_async;
                work[work_count].resume_fn = resume_fn;
                work[work_count].frame_type = frame_type;
                work[work_count].await_count = a_await_count;
                work[work_count].alocals = a_locals;
                work[work_count].alocal_count = a_local_count;
                work[work_count].sub_base = a_sub_base;
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
            LLVMTypeRef malloc_ty = LLVMGlobalGetValueType(g->fn_malloc);
            LLVMValueRef fsize = LLVMSizeOf(frame_type);
            LLVMValueRef raw = LLVMBuildCall2(g->builder, malloc_ty, g->fn_malloc, &fsize, 1, "frame.raw");
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
                LLVMValueRef slot = LLVMBuildStructGEP2(g->builder, frame_type, rframe,
                    (unsigned)(ASYNC_FRAME_FIRST_PARAM + k), "arg");
                LLVMBuildStore(g->builder, LLVMGetParam(ramp_fn, (unsigned)k), slot);
            }
            LLVMBuildRet(g->builder, raw);

            /* ---- resume ---- */
            LLVMBasicBlockRef res_entry = LLVMAppendBasicBlockInContext(g->ctx, resume_fn, "entry");
            LLVMPositionBuilderAtEnd(g->builder, res_entry);
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
                emit_async_complete(g, coerce_to_i64(g, val));
            }

            /* fall off the end: implicit completion (void / default result). */
            if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(g->builder))) {
                emit_async_complete(g, NULL);
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
    /* An error diagnostic emitted during codegen (e.g. an unsupported await
     * form flagged by the ANF pass) must fail the build — the driver only
     * checks diagnostics before codegen, so surface it here. */
    if (zan_diag_has_errors(g->diag)) {
        return ZAN_ERROR;
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
#ifdef _WIN32
    /* Emit GNU-ABI (MinGW) objects so the produced code links against the
     * bundled ld.lld + mingw-w64 runtime, keeping zanc self-contained: building
     * an .exe needs only zan, no external clang / MSVC / Windows SDK. Preserve
     * the host architecture prefix (e.g. x86_64) and swap the vendor/abi. */
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
