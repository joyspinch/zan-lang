#ifndef ZAN_LIST_WIDTH_HOIST_H
#define ZAN_LIST_WIDTH_HOIST_H

#include <llvm-c/Types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Post-pipeline: move List<T> slot-width tag checks (and their cold
 * __zan_list_repack calls) out of loops that cannot change the width.
 * Returns the number of checks hoisted; the caller runs a cleanup pipeline
 * when it is non-zero. See list_width_hoist.cpp. */
unsigned zan_opt_hoist_list_width_checks(LLVMModuleRef mod);

#ifdef __cplusplus
}
#endif

#endif
