/* irgen.h -- LLVM IR generation for the Zan language. */

#ifndef ZAN_IRGEN_H
#define ZAN_IRGEN_H

#include "zan.h"
#include "ast.h"
#include "binder.h"
#include <llvm-c/Core.h>
#include <llvm-c/DebugInfo.h>
#include <llvm-c/Target.h>
#include <llvm-c/TargetMachine.h>

/* A frame-resident slot of an async $resume body: the stack alloca that holds
 * the value while executing, and the heap-frame field it is saved to / reloaded
 * from around each suspension. */
typedef struct {
    LLVMValueRef slot_alloca;
    LLVMTypeRef  llvm;
    int          frame_index;
} zan_async_slot_t;

struct zan_irgen {
    zan_arena_t *arena;
    zan_diag_t *diag;
    zan_binder_t *binder;

    LLVMContextRef ctx;
    LLVMModuleRef mod;
    LLVMBuilderRef builder;

    /* current function being compiled */
    LLVMValueRef current_fn;
    LLVMTypeRef current_fn_ret_type;

    /* unique-name counter for null-conditional (`?.`) receiver temps */
    int qdot_counter;

    /* current 'this' context for method bodies */
    LLVMValueRef current_this;       /* alloca for 'this' pointer */
    zan_symbol_t *current_type_sym;  /* type symbol for 'this' */
    zan_ast_node_t *current_fn_body; /* root AST body of the fn being compiled */

    /* runtime function declarations */
    LLVMValueRef rt_println;   /* zan_rt_println(const char*) */
    LLVMValueRef rt_print_int; /* zan_rt_print_int(int64) */
    LLVMValueRef rt_print_uint; /* zan_rt_print_uint(uint64) */
    LLVMValueRef rt_print_double; /* zan_rt_print_double(double) */

    /* C library functions for string interpolation */
    LLVMValueRef fn_snprintf;
    LLVMValueRef fn_malloc;
    LLVMValueRef fn_free;
    LLVMValueRef fn_strlen;
    LLVMValueRef fn_strcpy;
    LLVMValueRef fn_strcat;

    /* struct type registry */
    struct {
        zan_symbol_t *sym;
        LLVMTypeRef llvm_type;
    } struct_types[256];
    int struct_type_count;

    /* per-class ARC release functions: __zan_release_<T>(i8*) releases the
     * object's RC-managed fields when its refcount reaches zero, then frees it
     * via zan_rt_release. Built lazily and cached by class symbol. */
    struct {
        zan_symbol_t *sym;
        LLVMValueRef  fn;
    } class_release[256];
    int class_release_count;

    /* user-defined functions (dynamically grown) */
    struct zan_fn_entry {
        zan_symbol_t *sym;
        LLVMValueRef fn;
        LLVMTypeRef fn_type;
    } *functions;
    int function_count;
    int function_cap;

    /* break/continue targets */
    LLVMBasicBlockRef break_target;
    LLVMBasicBlockRef continue_target;
    /* first body-scope local of the innermost loop: `break`/`continue`
     * release owned locals from this index before leaving the body */
    int loop_locals_base;

    /* constructors */
    struct {
        zan_symbol_t *type_sym;
        LLVMValueRef fn;
        LLVMTypeRef fn_type;
        int param_count;
    } ctors[256];
    int ctor_count;

    /* generic monomorphization: specialized copies of a user generic class's
     * methods/constructors, one per concrete instantiation (e.g. HashSet<string>).
     * Signatures are IDENTICAL to the erased versions (type params still lower to
     * the erased representation); the only behavioural difference is that the
     * body is emitted with `cur_inst` set, so intrinsic element comparisons
     * (List/Dict) substitute the type parameter to its concrete argument and use
     * content equality (e.g. strcmp) instead of erased identity. Routing a call
     * to a specialized symbol is therefore a pure symbol swap. */
    zan_type_t *cur_inst;   /* active instantiation while emitting a specialized
                             * body (a class type carrying concrete type_args);
                             * NULL when emitting erased/non-generic code. */
    struct zan_generic_fn {
        zan_symbol_t *msym;      /* the (erased) generic method symbol */
        zan_type_t  **args;      /* concrete type args of the instantiation */
        int           argc;
        LLVMValueRef  fn;
        LLVMTypeRef   fn_type;
    } *generic_fns;
    int generic_fn_count;
    int generic_fn_cap;
    struct zan_generic_ctor {
        zan_symbol_t *type_sym;
        zan_type_t  **args;
        int           argc;
        int           param_count;
        LLVMValueRef  fn;
        LLVMTypeRef   fn_type;
    } *generic_ctors;
    int generic_ctor_count;
    int generic_ctor_cap;
    /* distinct concrete instantiations discovered in the unit (worklist seed) */
    struct zan_generic_inst {
        zan_symbol_t *type_sym;  /* the generic class/struct symbol */
        zan_type_t   *inst;      /* instantiation type (sym + concrete type_args) */
    } *generic_insts;
    int generic_inst_count;
    int generic_inst_cap;

    /* method-level monomorphization: specialized copies of a *generic method*
     * (one declaring its own <T,...>), keyed by the concrete types bound to
     * those parameters at a call site. Unlike the class-level table above,
     * a specialized method's SIGNATURE uses the concrete types (no erasure),
     * so type-specific semantics (string/double comparison, ARC releases of
     * replaced values) hold inside the body. Bodies are emitted from a pending
     * queue drained after the main passes; emission may enqueue further
     * specializations (a generic method calling another generic method). */
    struct zan_method_spec {
        zan_symbol_t   *msym;      /* the generic method symbol */
        zan_symbol_t   *type_sym;  /* declaring class */
        zan_ast_node_t *member;    /* AST_METHOD_DECL */
        zan_type_t    **bind;      /* concrete type per declared type param */
        int             bindc;
        LLVMValueRef    fn;
        LLVMTypeRef     fn_type;
    } *method_specs;
    int method_spec_count;
    int method_spec_cap;
    int method_spec_emitted;   /* queue cursor: bodies [0..emitted) are done */
    /* active method specialization while emitting its body (else NULL): the
     * declared type-param list and the bound concrete types, applied when
     * resolving type refs in the body (see resolve_type_ctx). */
    zan_ast_list_t *cur_mtps;
    zan_type_t    **cur_mbind;

    /* ARC runtime functions */
    LLVMValueRef rt_retain;      /* zan_rt_retain(void*) */
    LLVMValueRef rt_release;     /* zan_rt_release(void*) */
    LLVMValueRef rt_release_dyn; /* zan_rt_release_dyn(void*): RTTI dispatch */
    LLVMValueRef rt_alloc;       /* zan_rt_alloc(int64_t size) -> void* */
    LLVMValueRef rt_str_retain;  /* zan_rt_str_retain(void*) */
    LLVMValueRef rt_str_release; /* zan_rt_str_release(void*) */
    LLVMValueRef rt_str_alloc;   /* zan_rt_str_alloc(int64_t size) -> void* */

    /* runtime diagnostics & leak detection */
    LLVMValueRef fn_printf;       /* int printf(const char*, ...) */
    LLVMTypeRef  printf_type;
    LLVMValueRef fn_exit;         /* void exit(int) */
    LLVMTypeRef  exit_type;
    LLVMValueRef fn_atexit;       /* int atexit(void(*)(void)) */
    LLVMTypeRef  atexit_type;
    LLVMValueRef g_live;          /* i64 global: net live ARC allocations */
    LLVMValueRef g_site_live;     /* [N x i64] global: live count per alloc site */
    LLVMValueRef g_site_names;    /* [N x i8*] global: "file:line:col" per site */
    LLVMTypeRef  site_live_type;  /* [N x i64] array type */
    LLVMTypeRef  site_names_type; /* [N x i8*] array type */
    LLVMValueRef g_site_dtors;    /* [N x i8*] global: release fn per alloc site */
    LLVMTypeRef  site_dtors_type; /* [N x i8*] array type */
    zan_symbol_t **site_syms;    /* concrete class symbol per alloc site */
    int          *site_coll;     /* per site: 0=class, 1=List, 2=StringBuilder */
    zan_type_t   **site_coll_elem; /* per site: List element type (for release) */
    int          leak_site_count; /* number of distinct `new` sites assigned */
    LLVMValueRef fn_report_leaks; /* void __zan_report_leaks(void) */
    const char  *src_file;        /* source path, for runtime diagnostics */
    bool         runtime_checks;  /* insert div-by-zero (etc.) guards; default true */
    bool         check_leaks;     /* emit a leak report at program exit */

    /* vtable registry (for virtual dispatch) */
    struct {
        zan_symbol_t *type_sym;
        LLVMValueRef vtable_global;  /* global constant array */
        int method_count;
    } vtables[256];
    int vtable_count;

    /* built-in List<T> runtime support */
    LLVMValueRef fn_realloc;     /* realloc(void*, size_t) -> void* */
    LLVMTypeRef list_struct_type; /* { i64 count, i64 capacity, i64* data } */
    LLVMTypeRef dict_struct_type; /* { i64 count, i64 capacity, i8** keys, i64* values } */
    LLVMTypeRef sb_struct_type;   /* StringBuilder { i64 count, i64 capacity, i8* data } */
    LLVMTypeRef task_struct_type; /* Task { i64 completed, i64 result, i64 thread_handle } */
    LLVMValueRef fn_strcmp;       /* strcmp(s1, s2) -> int */

    /* string literal cache (dedup same-content globals) */
    struct {
        zan_istr_t text;
        LLVMValueRef value;
    } string_literals[2048];
    int string_literal_count;

    /* async/await CPS lowering (see docs/ASYNC_CPS_DESIGN.md) */
    LLVMTypeRef  co_step_type;    /* void(i8*) — a frame's resume/step fn */
    LLVMTypeRef  co_step_ptr;     /* void(i8*)* — pointer to a step fn */
    LLVMTypeRef  co_header_type;  /* shared frame header {i32,i32,i8*,step*,i64} */
    LLVMValueRef rt_co_ready;     /* void zan_co_ready(void* frame, step) */
    LLVMTypeRef  rt_co_ready_type;
    LLVMValueRef rt_co_sched_init;/* void zan_co_sched_init(void) */
    LLVMTypeRef  rt_co_sched_init_type;
    LLVMValueRef rt_co_sched_run; /* void zan_co_sched_run(void) */
    LLVMTypeRef  rt_co_sched_run_type;
    LLVMValueRef rt_co_delay;     /* void zan_co_delay(i64 ms, void* frame, step) */
    LLVMTypeRef  rt_co_delay_type;
    /* socket async (S4b-2): the readiness reactor, provided by the shipped
     * zanrt_io object (built from src/runtime/rt_io.c). zan_io_wait_co registers
     * a one-shot fd watcher that re-readies (frame, step) when ready;
     * zan_io_pump_timeout blocks for IO up to the next timer deadline. A weak
     * inline fallback sleeps for timer-only programs; the reactor object's
     * strong definition overrides it for socket-async programs. */
    LLVMValueRef rt_io_wait_co;   /* void zan_io_wait_co(i64 fd,i32 interest,i8* frame,step) */
    LLVMTypeRef  rt_io_wait_co_type;
    LLVMValueRef rt_io_recv_co;   /* void zan_io_recv_co(i64 fd,i8* buf,i32 len,i8* frame,step,i64* out_n) */
    LLVMTypeRef  rt_io_recv_co_type;
    LLVMValueRef rt_io_accept_co; /* void zan_io_accept_co(i64 fd,i8* frame,step,i64* out_fd) */
    LLVMTypeRef  rt_io_accept_co_type;
    LLVMValueRef rt_io_pump_timeout;      /* i32 zan_io_pump_timeout(i64 timeout_ms) */
    LLVMTypeRef  rt_io_pump_timeout_type;
    bool         uses_socket_async; /* set when a socket await is lowered */
    bool         uses_sync_runtime; /* set by AtomicInt/SharedTable externs */
    /* goto/label support: label blocks keyed by (function, name), created on
     * first reference from either the label statement or a goto */
    struct {
        zan_istr_t        name;
        LLVMValueRef      fn;
        LLVMBasicBlockRef bb;
    } goto_labels[256];
    int goto_label_count;
    /* set while emitting an async function's $resume body: the current heap
     * frame pointer and its struct type, so `return` stores into the frame's
     * result slot + notifies the awaiter instead of a plain ret. NULL when not
     * lowering an async body. */
    LLVMValueRef current_async_frame;
    LLVMTypeRef  current_async_frame_type;
    LLVMValueRef current_async_resume_fn; /* the $resume fn being emitted */
    /* await state-machine context, valid only when current_async_frame is set
     * and the body contains awaits: the entry switch (new resume-k cases are
     * added here), the next state number to hand out, and the frame slots that
     * must be saved before a suspend and reloaded after (params + named
     * scalar locals live across suspensions). */
    LLVMValueRef current_async_switch;
    int          current_async_next_state;
    int          current_async_sub_base; /* frame index of first sub-task slot */
    int          current_async_sub_next;
    zan_async_slot_t *current_async_slots;
    int          current_async_slot_count;

    /* DllImport: tracked extern libraries for linker */
    zan_istr_t extern_libs[64];
    int extern_lib_count;
    /* DllImport: every extern declaration with its owning lib, so a lib that
     * cannot be resolved when cross-linking a fully static Linux binary can
     * have its functions stubbed out (see zan_irgen_stub_extern_lib). */
    struct {
        zan_istr_t lib;
        zan_istr_t name; /* symbol name; looked up at stub time because
                            optimization may delete unused declarations */
    } extern_fns[512];
    int extern_fn_count;

    /* cross-compilation target. When target_triple[0] is set, write_obj emits
     * an object for that LLVM triple verbatim (e.g. x86_64-unknown-linux-musl)
     * instead of applying the host's default/windows-gnu triple. Empty means
     * "use the host default" (unchanged legacy behaviour). */
    char target_triple[128];
    bool target_is_windows;   /* true when emitting for Windows (Sleep vs poll) */
    bool mt_scheduler;        /* --async-workers: skip the inline single-thread
                               * coroutine driver and link the multi-worker one
                               * from the zanrt_io_mt reactor object instead. */

    /* DWARF debug info (opt-in via `zanc -g`). When emit_debug is false these
     * remain NULL and no debug metadata is produced (default/--publish builds
     * are unchanged). See the di_* helpers in irgen.c. */
    bool             emit_debug;
    LLVMDIBuilderRef di_builder;
    LLVMMetadataRef  di_cu;
    LLVMMetadataRef  di_files[256]; /* DIFile per source file_id */
    uint32_t         di_cur_line;   /* source line of the statement in progress */
    uint32_t         di_cur_file;   /* its file_id (for local-variable declares) */

    /* ARC: nesting depth of the statement currently being emitted, counting
     * only control-flow bodies (if/loop/switch/try). A class-typed local is
     * tracked as an owning reference (released at function exit) only when it
     * is declared at depth 0, so its stack slot dominates every exit block. */
    int arc_stmt_depth;
};

zan_status_t zan_irgen_init(zan_irgen_t *g, zan_arena_t *arena,
                            zan_diag_t *diag, zan_binder_t *binder,
                            const char *module_name,
                            const char *target_triple,
                            bool target_is_windows, bool mt_scheduler,
                            bool check_leaks);
void zan_irgen_destroy(zan_irgen_t *g);

zan_status_t zan_irgen_emit(zan_irgen_t *g, zan_ast_node_t *unit);
zan_status_t zan_irgen_write_ir(zan_irgen_t *g, const char *path);
zan_status_t zan_irgen_write_obj(zan_irgen_t *g, const char *path);

/* Turns every bodyless [DllImport] declaration owned by `lib` into a strong
 * definition returning -1/null/0. Used before write_obj when cross-linking a
 * static Linux binary and no static archive for the lib is bundled: the
 * program still links, and the stubbed calls fail at runtime instead of the
 * whole publish failing. Returns the number of functions stubbed. */
int zan_irgen_stub_extern_lib(zan_irgen_t *g, const char *lib, int lib_len);

#endif /* ZAN_IRGEN_H */
