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

/* Nesting depth of catch bodies a single function body may be inside. */
#define ZAN_MAX_CATCH_DEPTH 16

/* Nesting depth of try/finally regions a single function body may be inside. */
#define ZAN_MAX_FINALLY_DEPTH 16

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
    zan_type_t *current_fn_zan_ret_type; /* declared source-language return type */

    /* unique-name counter for null-conditional (`?.`) receiver temps */
    int qdot_counter;

    /* current 'this' context for method bodies */
    LLVMValueRef current_this;       /* alloca for 'this' pointer */
    zan_symbol_t *current_type_sym;  /* type symbol for 'this' */
    zan_ast_node_t *current_fn_body; /* root AST body of the fn being compiled */
    bool current_fn_no_runtime;      /* [NoRuntime]: emit no ARC in this body */
    /* >0 while emitting a lambda body: lambdas are non-capturing, so current_this
     * is NULL inside them and a `this`/`base` reference would silently load a
     * garbage receiver (A33). Emitting AST_THIS_EXPR checks this to reject. */
    int lambda_depth;

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

    /* struct type registry (grown on demand: a class whose layout does not fit
     * would silently lower to a non-pointer and fail LLVM verification) */
    struct zan_struct_type_entry {
        zan_symbol_t *sym;
        LLVMTypeRef llvm_type;
        /* [StructLayout(LayoutKind.Explicit)]: the body is one opaque block
         * and every field is addressed by its own [FieldOffset(n)], so two
         * fields may overlap -- that is how a C union is written. Indexed
         * like get_field_index (a vptr slot, if any, is index 0). */
        bool explicit_layout;
        unsigned long *field_offsets;
        LLVMTypeRef *field_llvm;
        int field_count;
    } *struct_types;
    int struct_type_count;
    int struct_type_cap;

    /* per-class ARC release functions: __zan_release_<T>(i8*) releases the
     * object's RC-managed fields when its refcount reaches zero, then frees it
     * via zan_rt_release. Built lazily and cached by class symbol. */
    struct zan_class_release_entry {
        zan_symbol_t *sym;
        zan_type_t   *inst;  /* instantiation walked (Acc<Node>), NULL if none */
        LLVMValueRef  fn;
    } *class_release;
    int class_release_count;
    int class_release_cap;

    /* user-defined functions (dynamically grown) */
    struct zan_fn_entry {
        zan_symbol_t *sym;
        LLVMValueRef fn;
        LLVMTypeRef fn_type;
    } *functions;
    int function_count;
    int function_cap;
    /* symbol -> index into `functions`, so a call site resolves its callee in
     * O(1). Scanning the registry made irgen quadratic in the number of
     * functions (48k lines of code spent ~4.5 s of a 6 s build in irgen).
     * Open addressing with a power-of-two capacity; sym == NULL marks a free
     * slot, and a symbol registered twice keeps its first index (the scan it
     * replaces stopped at the first match). */
    struct zan_fn_index_slot {
        zan_symbol_t *sym;
        int idx;
    } *fn_index;
    int fn_index_cap;

    /* break/continue targets */
    LLVMBasicBlockRef break_target;
    LLVMBasicBlockRef continue_target;
    /* first body-scope local of the innermost loop: `break`/`continue`
     * release owned locals from this index before leaving the body */
    int loop_locals_base;
    /* first local whose scope-exit release a `throw` would skip: the longjmp
     * lands in the innermost enclosing try of *this* function (so its body's
     * locals, from this index up, must be released at the throw site) or, with
     * no enclosing try, leaves the function altogether (index 0). */
    int throw_locals_base;
    /* catch bodies currently being emitted, innermost last. A handler owns the
     * caught exception (see the exc.push/exc.hrel pair in the try lowering) and
     * releases it in the try's epilogue -- which `return`, `break`, `continue`
     * and `throw` jump past, so those paths release it from here instead. */
    struct {
        LLVMValueRef exc_slot;   /* i8* slot holding the caught exception */
        LLVMValueRef owned_slot; /* i32 slot: non-zero when the handler owns it */
        LLVMValueRef tid_slot;   /* i8* slot: its class type descriptor, used by
                                  * a bare `throw;` to rethrow with the original
                                  * dynamic type */
    } catch_cleanups[ZAN_MAX_CATCH_DEPTH];
    int catch_cleanup_count;
    /* catch_cleanups entries entered inside the innermost loop: `break` and
     * `continue` leave only those */
    int loop_catch_base;
    /* catch_cleanups entries entered inside the innermost enclosing try body:
     * a `throw` unwinds past exactly those handlers */
    int throw_catch_base;

    /* `finally` bodies of the try statements currently being emitted, innermost
     * last. C# runs a finally on EVERY way out of its try, but this lowering
     * has no landing pads to hang cleanups off, so each exit path emits the
     * body inline: `return` runs all of them, `break`/`continue` the ones
     * entered inside the loop (from finally_loop_base up), and an exception
     * with no matching clause runs this try's own before rethrowing. */
    struct {
        zan_ast_node_t *body;   /* the finally block's AST */
        LLVMValueRef monitor_obj; /* set instead of `body` by `lock (obj)`: the
                                   * alloca holding the locked object, whose
                                   * monitor every exit path must release */
        bool in_try_body;       /* emitting the guarded body: a throw here is
                                 * taken by this try's own handler, which runs
                                 * the finally itself. False while emitting a
                                 * catch (or the finally), where a throw leaves
                                 * the region and must run it at the throw site. */
    } finallys[ZAN_MAX_FINALLY_DEPTH];
    int finally_count;
    /* finallys entered inside the innermost loop: break/continue run only those */
    int finally_loop_base;

    /* constructors */
    struct zan_ctor_entry {
        zan_symbol_t *type_sym;
        zan_ast_node_t *decl;
        LLVMValueRef fn;
        LLVMTypeRef fn_type;
        int param_count;
    } *ctors;
    int ctor_count;
    int ctor_cap;

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
        zan_ast_node_t *decl;
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
    /* RC-managed static fields, registered as their backing globals are
     * created, so program-exit cleanup can release them across EVERY
     * compilation unit — the unit containing main() only sees its own
     * declarations, and stdlib singletons (Pinyin.cache, ...) would leak. */
    struct zan_static_field_ref {
        zan_type_t   *type;  /* the field's declared (rc-managed) type */
        LLVMValueRef  gv;    /* backing global */
    } *static_fields;
    int static_field_count;
    int static_field_cap;
    LLVMValueRef g_live;          /* i64 global: net live ARC allocations */
    LLVMValueRef g_site_live;     /* [N x i64] global: live count per alloc site */
    LLVMValueRef g_site_names;    /* [N x i8*] global: "file:line:col" per site */
    LLVMTypeRef  site_live_type;  /* [N x i64] array type */
    LLVMTypeRef  site_names_type; /* [N x i8*] array type */
    LLVMValueRef g_site_dtors;    /* [N x i8*] global: release fn per alloc site */
    LLVMTypeRef  site_dtors_type; /* [N x i8*] array type */
    LLVMValueRef g_site_tynames;  /* [N x i8*] global: ancestor-name list ptr
                                   * per site, for runtime `is`/`as` checks */
    LLVMTypeRef  site_tynames_type; /* [N x i8*] array type */
    zan_symbol_t **site_syms;    /* concrete class symbol per alloc site */
    zan_type_t   **site_inst;    /* per site: the instantiated class type, so a
                                  * generic class's destructor releases the
                                  * fields its type arguments really hold */
    int          *site_coll;     /* per site: 0=class, 1=List, 2=StringBuilder */
    zan_type_t   **site_coll_elem; /* per site: List element type (for release) */
    int          leak_site_count; /* number of distinct `new` sites assigned */
    LLVMValueRef fn_report_leaks; /* void __zan_report_leaks(void) */
    const char  *src_file;        /* source path, for runtime diagnostics */
    bool         runtime_checks;  /* insert div-by-zero (etc.) guards; default true */
    bool         check_leaks;     /* emit a leak report at program exit */
    bool         fast_codegen;    /* machine codegen at -O0 (fast turnaround) */
    bool         emit_lib;        /* library output: keep `public` members as
                                     exported (external-linkage) symbols */
    bool         emit_shared;     /* shared library (not static archive): emit a
                                     real entry point for the platform (DllMain) */

    /* Binding<T> lowering: synthesized per-(class,field) accessor functions
     * (see emit_binding_value in irgen_expr.c), cached so each field pair is
     * emitted once per module. */
    struct {
        zan_symbol_t *cls;
        zan_symbol_t *field;
        LLVMValueRef get_fn;
        LLVMValueRef set_fn;
    } bind_accs[512];
    int bind_acc_count;

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
    LLVMTypeRef span_struct_type; /* Span<T> value: { i8* base, i64 len } */
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

    /* --publish string obfuscation. Literal text is stored XOR-scrambled in
     * the image (emit_string_literal_rc); a .ctors constructor un-scrambles it
     * in place before main, so a static `strings`/grep over the exe finds no
     * user text. Not cryptography -- the key ships in the binary -- it only
     * defeats trivial static extraction. */
    bool obfuscate_strings;
    unsigned char obf_key[16];
    struct { LLVMValueRef global; uint32_t len; } obf_literals[2048];
    int obf_literal_count;

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
     * strong definition overrides it for socket-async programs.
     * The `fd` parameters are C `intptr_t` (a Windows SOCKET is a UINT_PTR),
     * lowered as i64 because our targets are 64-bit; A0-2 makes that a real
     * pointer-width lowering. */
    LLVMValueRef rt_io_wait_co;   /* void zan_io_wait_co(iptr fd,i32 interest,i8* frame,step) */
    LLVMTypeRef  rt_io_wait_co_type;
    LLVMValueRef rt_io_recv_co;   /* void zan_io_recv_co(iptr fd,i8* buf,i32 len,i8* frame,step,i64* out_n) */
    LLVMTypeRef  rt_io_recv_co_type;
    LLVMValueRef rt_io_accept_co; /* void zan_io_accept_co(iptr fd,i8* frame,step,iptr* out_fd) */
    LLVMTypeRef  rt_io_accept_co_type;
    LLVMValueRef rt_io_pump_timeout;      /* i32 zan_io_pump_timeout(i64 timeout_ms) */
    LLVMTypeRef  rt_io_pump_timeout_type;
    bool         uses_socket_async; /* set when a socket await is lowered */
    bool         uses_timer_runtime; /* set by Timer API externs */
    bool         uses_sync_runtime; /* set by AtomicInt/SharedTable externs */
    bool         uses_embed_api;    /* set by zan_embed_* extern references */
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
    /* declared return type of the async method being emitted: the frame result
     * slot is encoded/decoded against it (see coerce_to_frame_result) */
    zan_type_t  *current_async_ret_type;
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
    /* async exception handling: the eh-stack depth on entry to the $resume
     * invocation being emitted (an alloca), the block that completes the frame
     * with a pending exception, the switch that re-enters the catch of a
     * handler armed by an earlier invocation, and the next handler id. */
    LLVMValueRef current_async_eh_entry;
    LLVMBasicBlockRef current_async_exc_bb;
    LLVMValueRef current_async_rearm_switch;
    /* re-arm time: per-handler block that restores the eh bookkeeping the
     * try's entry wrote in the invocation that armed it */
    LLVMValueRef current_async_rearm_init_switch;
    LLVMBasicBlockRef current_async_rearm_next_bb;
    int          current_async_handler_next;
    /* how many per-handler slots this frame has: the number of try statements
     * the body lowers (counted by the async scan, which sees the finally-body
     * copies too), so `current_async_handler_next` can never run past it */
    int          current_async_handler_cap;
    /* per-function id of the next `foreach` emitted inside an async body;
     * indexes its frame-resident iteration state (see AST_FOREACH_STMT) */
    int          current_async_foreach_next;

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

    /* Per-thread exception-handling state (see irgen_builtins.c). The block
     * pointer and the field addresses derived from it are materialized once
     * per function, in its entry block: EH lowering hands these pointers to
     * blocks the async CPS split moves out of the defining block's dominance,
     * exactly like emit_entry_alloca's slots. */
    LLVMTypeRef  eh_state_ty;
    LLVMValueRef eh_state_owner;   /* function the cache below belongs to */
    LLVMValueRef eh_state_cached;
    LLVMValueRef eh_state_fields[8];

    /* cross-compilation target. When target_triple[0] is set, write_obj emits
     * an object for that LLVM triple verbatim (e.g. x86_64-unknown-linux-musl)
     * instead of applying the host's default/windows-gnu triple. Empty means
     * "use the host default" (unchanged legacy behaviour). */
    char target_triple[128];
    bool target_is_windows;   /* true when emitting for Windows (Sleep vs poll) */
    bool target_is_macos;     /* true when emitting for Darwin: libSystem exports
                               * the stdio streams as __std{in,out,err}p, not as
                               * the ELF libc `stdin`/`stdout`/`stderr` globals */
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

/* Zan compiles a whole program (every reachable stdlib and user file) into one
 * LLVM module and links an executable, so nothing outside the module can call a
 * Zan function: `main` is the only symbol the C runtime needs by name. Giving
 * every other definition internal linkage is what lets LLVM's GlobalDCE delete
 * the ones no live code, vtable or delegate refers to -- with external linkage
 * the linker has to keep them all (a layout-only demo still carried the whole
 * code editor and data grid). Address-taken functions stay alive through the
 * reference itself, so delegates, WndProcs and vtable slots are unaffected. */
static inline void zan_set_module_local(LLVMValueRef fn) {
    if (fn) LLVMSetLinkage(fn, LLVMInternalLinkage);
}

zan_status_t zan_irgen_init(zan_irgen_t *g, zan_arena_t *arena,
                            zan_diag_t *diag, zan_binder_t *binder,
                            const char *module_name,
                            const char *target_triple,
                            bool target_is_windows, bool mt_scheduler,
                            bool check_leaks);
void zan_irgen_destroy(zan_irgen_t *g);

zan_status_t zan_irgen_emit(zan_irgen_t *g, zan_ast_node_t *unit);
/* --publish only: emit the .ctors constructor that un-scrambles string
 * literals. No-op unless g->obfuscate_strings and at least one literal was
 * recorded. Call after all codegen, before module verification. */
void zan_irgen_emit_string_deobf(zan_irgen_t *g);
zan_status_t zan_irgen_write_ir(zan_irgen_t *g, const char *path);
zan_status_t zan_irgen_write_obj(zan_irgen_t *g, const char *path);

/* Turns every bodyless [DllImport] declaration owned by `lib` into a strong
 * definition returning -1/null/0. Used before write_obj when cross-linking a
 * static Linux binary and no static archive for the lib is bundled: the
 * program still links, and the stubbed calls fail at runtime instead of the
 * whole publish failing. Returns the number of functions stubbed. */
int zan_irgen_stub_extern_lib(zan_irgen_t *g, const char *lib, int lib_len);

/* True when the module defines a function whose (mangled `Class_Member`) name
 * starts with `prefix`. Lets a driver bundle a dependency only for programs
 * that actually use the feature owning it (see the `if` clause of a
 * `<driver>.bundle` manifest). */
bool zan_irgen_defines_prefix(zan_irgen_t *g, const char *prefix);

#endif /* ZAN_IRGEN_H */
