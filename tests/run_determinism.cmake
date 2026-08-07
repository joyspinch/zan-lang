# Deterministic-codegen check for a single Zan program.
#
# Invoked as:
#   cmake -DZANC=<zanc> -DSRC=<file.zan> [-DZANC_ARGS=<extra;args>] -P run_determinism.cmake
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

# ---- up-to-date check ------------------------------------------------------
# Re-running the suite must not recompile programs whose inputs did not change:
# the artifact is a pure function of (source, compiler, stdlib), so a target
# newer than all three is reused. STDLIB_STAMP is touched by the build whenever
# any stdlib source changes.
function(zan_artifact_is_current out_var artifact)
  set(${out_var} FALSE PARENT_SCOPE)
  if(NOT EXISTS ${artifact})
    return()
  endif()
  if(${SRC} IS_NEWER_THAN ${artifact})
    return()
  endif()
  if(${ZANC} IS_NEWER_THAN ${artifact})
    return()
  endif()
  if(STDLIB_STAMP AND EXISTS ${STDLIB_STAMP} AND ${STDLIB_STAMP} IS_NEWER_THAN ${artifact})
    return()
  endif()
  set(${out_var} TRUE PARENT_SCOPE)
endfunction()

# Determinism depends only on (source, compiler, stdlib): once verified for a
# given triple, re-verifying it on every suite run just burns two compiles.
if(STAMP)
  zan_artifact_is_current(_current ${STAMP})
  if(_current)
    message(STATUS "deterministic (cached): ${SRC}")
    return()
  endif()
endif()

function(emit_ir out_var)
  execute_process(
    COMMAND ${ZANC} ${SRC} ${ZANC_ARGS} --emit-ir
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

if(STAMP)
  file(WRITE ${STAMP} "ok\n")
endif()

message(STATUS "deterministic: ${SRC}")
