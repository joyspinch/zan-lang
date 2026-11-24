
#ifndef ZAN_IRGEN_COMPACT_H
#define ZAN_IRGEN_COMPACT_H

#include <stddef.h>
#include <llvm-c/Types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct zan_irgen_compactor zan_irgen_compactor_t;

zan_irgen_compactor_t *zan_irgen_compactor_create(LLVMModuleRef module,
                                                char *errbuf, size_t errbuf_size);

int zan_irgen_compactor_run(zan_irgen_compactor_t *compactor,
                            LLVMValueRef function,
                            char *errbuf, size_t errbuf_size);

void zan_irgen_compactor_destroy(zan_irgen_compactor_t *compactor);

/* Drop a function's body, keeping its linkage (Function::deleteBody resets it
 * to external). The function becomes a declaration. */
void zan_llvm_delete_body(LLVMValueRef function);

#ifdef __cplusplus
}
#endif

#endif /* ZAN_IRGEN_COMPACT_H */
