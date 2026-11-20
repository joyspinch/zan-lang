/* Included by main.c for access to the driver's atomic output helpers. */
#include <llvm-c/IRReader.h>
#include <llvm-c/TargetMachine.h>

static int compile_ir_object(int argc, char **argv) {
    const char *input = argc > 2 ? argv[2] : NULL;
    const char *output = NULL;
    const char *requested_target = NULL;
    zan_opt_level_t level = ZAN_OPT_FULL;
    LLVMCodeGenOptLevel codegen = LLVMCodeGenLevelDefault;

    for (int i = 3; i < argc; i++) {
        const char *arg = argv[i];
        if (strcmp(arg, "-o") == 0 && !output && i + 1 < argc && argv[i + 1][0] != '-') {
            output = argv[++i];
        } else if (strcmp(arg, "--target") == 0 && !requested_target && i + 1 < argc && argv[i + 1][0] != '-') {
            requested_target = argv[++i];
        } else if (strcmp(arg, "-O0") == 0) {
            level = ZAN_OPT_NONE;
            codegen = LLVMCodeGenLevelNone;
        } else if (strcmp(arg, "-O1") == 0) {
            level = ZAN_OPT_BASIC;
            codegen = LLVMCodeGenLevelLess;
        } else if (strcmp(arg, "-O2") == 0) {
            level = ZAN_OPT_FULL;
            codegen = LLVMCodeGenLevelDefault;
        } else if (strcmp(arg, "-O3") == 0) {
            level = ZAN_OPT_AGGRESSIVE;
            codegen = LLVMCodeGenLevelAggressive;
        } else if (strcmp(arg, "-Os") == 0 || strcmp(arg, "-Oz") == 0) {
            level = arg[2] == 's' ? ZAN_OPT_SIZE : ZAN_OPT_SIZE_MIN;
            codegen = LLVMCodeGenLevelDefault;
        } else {
            fprintf(stderr, "error: invalid --compile-ir argument '%s'\n", arg);
            return 1;
        }
    }
    if (!input || !*input || input[0] == '-' || !output || !*output ||
        (requested_target && !*requested_target)) {
        fprintf(stderr, "usage: zanc --compile-ir <input.ll> -o <object> [--target <LLVM triple>] [-O0|-O1|-O2|-O3|-Os|-Oz]\n");
        return 1;
    }

    int rc = 1;
    char *error = NULL;
    char *triple = NULL;
    LLVMContextRef context = LLVMContextCreate();
    LLVMModuleRef module = NULL;
    LLVMTargetMachineRef machine = NULL;
    LLVMMemoryBufferRef buffer;
    if (LLVMCreateMemoryBufferWithContentsOfFile(input, &buffer, &error) ||
        LLVMParseIRInContext(context, buffer, &module, &error)) {
        goto done;
    }

    const char *module_target = LLVMGetTarget(module);
    triple = *module_target ? LLVMNormalizeTargetTriple(module_target) : LLVMGetDefaultTargetTriple();
    if (requested_target) {
        char *requested = LLVMNormalizeTargetTriple(requested_target);
        if (*module_target && strcmp(triple, requested) != 0) {
            fprintf(stderr, "error: IR target '%s' conflicts with requested target '%s'\n",
                    module_target, requested_target);
            LLVMDisposeMessage(requested);
            goto done;
        }
        LLVMDisposeMessage(triple);
        triple = requested;
    }
    LLVMTargetRef target;
    zan_irgen_init_llvm_targets();
    if (LLVMGetTargetFromTriple(triple, &target, &error)) goto done;
    machine = LLVMCreateTargetMachine(target, triple, "generic", "", codegen,
                                      LLVMRelocPIC, LLVMCodeModelDefault);
    if (!machine) {
        fprintf(stderr, "error: failed to create target machine for '%s'\n", triple);
        goto done;
    }
    LLVMSetTarget(module, triple);
    /* Keep the ABI layout of a module that already declares one. */
    if (!*LLVMGetDataLayoutStr(module)) {
        LLVMTargetDataRef layout = LLVMCreateTargetDataLayout(machine);
        LLVMSetModuleDataLayout(module, layout);
        LLVMDisposeTargetData(layout);
    }
    if (zan_opt_run_passes_on_module(module, machine, level, NULL) != ZAN_OK) goto done;

    if (!create_link_work_dir(output)) {
        fprintf(stderr, "error: cannot create a staging directory for '%s'\n", output);
        goto done;
    }
    snprintf(link_work_output, sizeof(link_work_output), "%s/object", link_work_dir);
    if (LLVMTargetMachineEmitToFile(machine, module, link_work_output, LLVMObjectFile, &error)) goto done;
    if (!publish_link_output(link_work_output, output)) {
        fprintf(stderr, "error: cannot publish object '%s'\n", output);
        goto done;
    }
    rc = 0;

done:
    if (error) {
        fprintf(stderr, "error: %s\n", error);
        LLVMDisposeMessage(error);
    }
    if (machine) LLVMDisposeTargetMachine(machine);
    if (triple) LLVMDisposeMessage(triple);
    if (module) LLVMDisposeModule(module);
    LLVMContextDispose(context);
    clear_link_work_dir();
    return rc;
}
