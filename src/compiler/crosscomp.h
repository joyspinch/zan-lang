
#ifndef ZAN_CROSSCOMP_H
#define ZAN_CROSSCOMP_H

#include <stdbool.h>

typedef enum {
    ZAN_ARCH_X86_64,
    ZAN_ARCH_AARCH64,
    ZAN_ARCH_RISCV64,
    ZAN_ARCH_RISCV32,
    ZAN_ARCH_WASM32,
} zan_arch_t;

typedef enum {
    ZAN_OS_WINDOWS,
    ZAN_OS_LINUX,
    ZAN_OS_MACOS,
    ZAN_OS_IOS,
    ZAN_OS_WASI,
    ZAN_OS_ANDROID,
    ZAN_OS_OHOS,
    ZAN_OS_FREESTANDING,
} zan_os_t;

typedef enum {
    ZAN_ABI_MSVC,
    ZAN_ABI_GNU,
    ZAN_ABI_MUSL,
    ZAN_ABI_APPLE,
    ZAN_ABI_WASM,
} zan_abi_t;

typedef struct {
    zan_arch_t arch;
    zan_os_t os;
    zan_abi_t abi;
    char triple[128];
    char cpu[64];
    char features[256];
    int pointer_size;       /* in bytes: 4 or 8 */
    bool pic;
} zan_target_t;

bool zan_target_parse(const char *triple_str, zan_target_t *out);

void zan_target_host(zan_target_t *out);

const char *zan_target_llvm_triple(const zan_target_t *target);

typedef struct {
    const char *name;
    const char *triple;
    const char *desc;
} zan_target_info_t;

int zan_target_list(const zan_target_info_t **out);

void *zan_target_create_machine(const zan_target_t *target, int opt_level);

#endif /* ZAN_CROSSCOMP_H */
