/* crosscomp.c -- Cross-compilation target support implementation. */

#include "crosscomp.h"
#include <llvm-c/Core.h>
#include <llvm-c/Target.h>
#include <llvm-c/TargetMachine.h>
#include <string.h>
#include <stdio.h>

/* ---- Available targets ---- */

static const zan_target_info_t s_targets[] = {
    { "win-x64",     "x86_64-pc-windows-msvc",    "Windows x86-64 (MSVC ABI)" },
    { "win-arm64",   "aarch64-pc-windows-msvc",   "Windows ARM64 (MSVC ABI)" },
    { "linux-x64",   "x86_64-unknown-linux-gnu",  "Linux x86-64 (glibc)" },
    { "linux-musl",  "x86_64-unknown-linux-musl", "Linux x86-64 (musl static)" },
    { "linux-arm64", "aarch64-unknown-linux-gnu",  "Linux ARM64 (glibc)" },
    { "macos-x64",   "x86_64-apple-macosx",       "macOS x86-64" },
    { "macos-arm64", "aarch64-apple-macosx",       "macOS ARM64 (Apple Silicon)" },
    { "wasm32",      "wasm32-unknown-wasi",        "WebAssembly 32-bit (WASI)" },
    { "riscv64",     "riscv64-unknown-linux-musl",  "RISC-V 64-bit Linux" },
};

#define NUM_TARGETS (int)(sizeof(s_targets) / sizeof(s_targets[0]))

int zan_target_list(const zan_target_info_t **out) {
    *out = s_targets;
    return NUM_TARGETS;
}

/* ---- Parse target triple ---- */

static zan_arch_t parse_arch(const char *s) {
    if (strncmp(s, "x86_64", 6) == 0 || strncmp(s, "x86-64", 6) == 0) return ZAN_ARCH_X86_64;
    if (strncmp(s, "aarch64", 7) == 0 || strncmp(s, "arm64", 5) == 0) return ZAN_ARCH_AARCH64;
    if (strncmp(s, "riscv64", 7) == 0) return ZAN_ARCH_RISCV64;
    if (strncmp(s, "wasm32", 6) == 0) return ZAN_ARCH_WASM32;
    return ZAN_ARCH_X86_64;
}

static zan_os_t parse_os(const char *s) {
    if (strstr(s, "windows") || strstr(s, "win32")) return ZAN_OS_WINDOWS;
    if (strstr(s, "linux")) return ZAN_OS_LINUX;
    if (strstr(s, "macos") || strstr(s, "darwin") || strstr(s, "apple")) return ZAN_OS_MACOS;
    if (strstr(s, "wasi")) return ZAN_OS_WASI;
    return ZAN_OS_FREESTANDING;
}

static zan_abi_t parse_abi(const char *s, zan_os_t os) {
    if (strstr(s, "msvc")) return ZAN_ABI_MSVC;
    if (strstr(s, "musl")) return ZAN_ABI_MUSL;
    if (strstr(s, "gnu")) return ZAN_ABI_GNU;
    if (os == ZAN_OS_MACOS) return ZAN_ABI_APPLE;
    if (os == ZAN_OS_WASI) return ZAN_ABI_WASM;
    if (os == ZAN_OS_WINDOWS) return ZAN_ABI_MSVC;
    return ZAN_ABI_GNU;
}

bool zan_target_parse(const char *triple_str, zan_target_t *out) {
    memset(out, 0, sizeof(*out));

    /* Check if it's a short name first */
    for (int i = 0; i < NUM_TARGETS; i++) {
        if (strcmp(triple_str, s_targets[i].name) == 0) {
            triple_str = s_targets[i].triple;
            break;
        }
    }

    strncpy(out->triple, triple_str, sizeof(out->triple) - 1);
    out->arch = parse_arch(triple_str);
    out->os = parse_os(triple_str);
    out->abi = parse_abi(triple_str, out->os);
    snprintf(out->cpu, sizeof(out->cpu), "%s", "generic");
    out->features[0] = 0;
    out->pointer_size = (out->arch == ZAN_ARCH_WASM32) ? 4 : 8;
    out->pic = (out->os == ZAN_OS_LINUX || out->os == ZAN_OS_MACOS);

    return true;
}

void zan_target_host(zan_target_t *out) {
    memset(out, 0, sizeof(*out));
#if defined(_WIN32) && defined(_M_X64)
    zan_target_parse("x86_64-pc-windows-msvc", out);
#elif defined(_WIN32) && defined(_M_ARM64)
    zan_target_parse("aarch64-pc-windows-msvc", out);
#elif defined(__APPLE__) && defined(__aarch64__)
    zan_target_parse("aarch64-apple-macosx", out);
#elif defined(__APPLE__) && defined(__x86_64__)
    zan_target_parse("x86_64-apple-macosx", out);
#elif defined(__linux__) && defined(__aarch64__)
    zan_target_parse("aarch64-unknown-linux-gnu", out);
#elif defined(__linux__) && defined(__x86_64__)
    zan_target_parse("x86_64-unknown-linux-gnu", out);
#else
    zan_target_parse("x86_64-unknown-linux-gnu", out);
#endif
}

const char *zan_target_llvm_triple(const zan_target_t *target) {
    return target->triple;
}

/* ---- LLVM target machine creation ---- */

void *zan_target_create_machine(const zan_target_t *target, int opt_level) {
    char *error = NULL;
    LLVMTargetRef llvm_target = NULL;

    if (LLVMGetTargetFromTriple(target->triple, &llvm_target, &error) != 0) {
        if (error) {
            fprintf(stderr, "error: cannot get LLVM target for '%s': %s\n", target->triple, error);
            LLVMDisposeMessage(error);
        }
        return NULL;
    }

    LLVMCodeGenOptLevel cgopt;
    switch (opt_level) {
    case 0: cgopt = LLVMCodeGenLevelNone; break;
    case 1: cgopt = LLVMCodeGenLevelLess; break;
    case 3: cgopt = LLVMCodeGenLevelAggressive; break;
    default: cgopt = LLVMCodeGenLevelDefault; break;
    }

    LLVMRelocMode reloc = target->pic ? LLVMRelocPIC : LLVMRelocDefault;

    LLVMTargetMachineRef tm = LLVMCreateTargetMachine(
        llvm_target,
        target->triple,
        target->cpu,
        target->features,
        cgopt,
        reloc,
        LLVMCodeModelDefault
    );

    return (void *)tm;
}
