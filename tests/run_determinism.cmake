# Deterministic-codegen check for a single Zan program.
#
# Invoked as:
#   cmake -DZANC=<zanc> -DSRC=<file.zan> -P run_determinism.cmake
#
# Compiles SRC to LLVM IR twice and fails (non-zero) unless the two emissions
# are byte-for-byte identical. This is the C-hosted analogue of a self-hosting
# gen2==gen3 diff: it guarantees the compiler's output is a pure function of its
# input, catching nondeterminism / undefined behaviour in code generation
# (iteration over unordered containers, uninitialised memory, pointer-value
# leakage into output, etc.).

if(NOT ZANC OR NOT SRC)
  message(FATAL_ERROR "run_determinism.cmake: ZANC and SRC are required")
endif()

function(emit_ir out_var)
  execute_process(
    COMMAND ${ZANC} ${SRC} --emit-ir
    RESULT_VARIABLE rc
    OUTPUT_VARIABLE ir
    ERROR_VARIABLE  err)
  if(NOT rc EQUAL 0)
    message(FATAL_ERROR "emit-ir failed for ${SRC} (rc=${rc})\n${err}")
  endif()
  set(${out_var} "${ir}" PARENT_SCOPE)
endfunction()

emit_ir(ir_a)
emit_ir(ir_b)

if(NOT ir_a STREQUAL ir_b)
  message(FATAL_ERROR
    "non-deterministic codegen for ${SRC}: two --emit-ir runs differ")
endif()

message(STATUS "deterministic: ${SRC}")
