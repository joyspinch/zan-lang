#include "irgen_internal.h"
#include "optimizer.h"

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
    bool            is_async;
    LLVMValueRef    resume_fn;
    LLVMTypeRef     frame_type;
    int             await_count;
    async_local_t  *alocals;
    int             alocal_count;
    int             sub_base;
    int             rval_slot;
    int             handler_cap;
    int             try_count;
    int             fin_depth_max;
    zan_type_t     *cur_inst;
    LLVMTypeRef     fn_type;
    zan_ast_list_t *mtps;
    zan_type_t    **mbind;
} method_body_work_t;


/* Composite Dispatcher Submodules */
#include "emit/emit_main.c"
#include "emit/emit_async.c"
#include "emit/emit_methods.c"
#include "emit/emit_target.c"
