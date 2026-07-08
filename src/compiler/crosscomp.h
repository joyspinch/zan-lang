/* crosscomp.h -- Cross-compilation target support for Zan. */

#ifndef ZAN_CROSSCOMP_H
#define ZAN_CROSSCOMP_H

#include <stdbool.h>

/* ---- Target architecture ---- */

typedef enum {
    ZAN_ARCH_X86_64,
    ZAN_ARCH_AARCH64,
    ZAN_ARCH_RISCV64,
    ZAN_ARCH_WASM32,
} zan_arch_t;

/* ---- Target OS ---- */

typedef enum {
    ZAN_OS_WINDOWS,
    ZAN_OS_LINUX,
    ZAN_OS_MACOS,
    ZAN_OS_WASI,
    ZAN_OS_FREESTANDING,
} zan_os_t;

/* ---- Target ABI ---- */

typedef enum {
    ZAN_ABI_MSVC,
    ZAN_ABI_GNU,
    ZAN_ABI_MUSL,
    ZAN_ABI_APPLE,
    ZAN_ABI_WASM,
} zan_abi_t;

/* ---- Target triple ---- */

typedef struct {
    zan_arch_t arch;
    zan_os_t os;
    zan_abi_t abi;
    char triple[128];       /* LLVM target triple string */
    char cpu[64];           /* target CPU (e.g. "generic", "apple-m1") */
    char features[256];     /* target features (e.g. "+sse2,+avx") */
    int pointer_size;       /* in bytes: 4 or 8 */
    bool pic;               /* position-independent code */
} zan_target_t;

/* Parse a target triple string like "x86_64-windows-msvc" */
bool zan_target_parse(const char *triple_str, zan_target_t *out);

/* Get the host platform target */
void zan_target_host(zan_target_t *out);

/* Get LLVM triple string for target */
const char *zan_target_llvm_triple(const zan_target_t *target);

/* Get list of available targets */
typedef struct {
    const char *name;       /* short name: "win-x64", "linux-arm64" */
    const char *triple;     /* LLVM triple */
    const char *desc;       /* human description */
} zan_target_info_t;

int zan_target_list(const zan_target_info_t **out);

/* Configure LLVM target machine for cross-compilation */
void *zan_target_create_machine(const zan_target_t *target, int opt_level);

#endif /* ZAN_CROSSCOMP_H */
