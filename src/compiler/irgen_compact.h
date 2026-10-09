/* 底层系统交互与数据协议契约 */

#ifndef ZAN_IRGEN_COMPACT_H
#define ZAN_IRGEN_COMPACT_H

#include <stddef.h>
#include <llvm-c/Types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct zan_irgen_compactor zan_irgen_compactor_t;

/* 底层系统交互与数据协议契约 */
zan_irgen_compactor_t *zan_irgen_compactor_create(LLVMModuleRef module,
                                                char *errbuf, size_t errbuf_size);

/* 底层系统交互与数据协议契约 */
int zan_irgen_compactor_run(zan_irgen_compactor_t *compactor,
                            LLVMValueRef function,
                            char *errbuf, size_t errbuf_size);

/* 底层系统交互与数据协议契约 */
void zan_irgen_compactor_destroy(zan_irgen_compactor_t *compactor);

#ifdef __cplusplus
}
#endif

#endif /* ZAN_IRGEN_COMPACT_H */
