# Compile a Zan program with --arc-guard, run it, and fail if the runtime
# reports an ARC integrity failure.
#
# Invoked as:
#   cmake -DZANC=<zanc> -DSRC=<file.zan> -DOUT_EXE=<exe path> \
#         [-DZANC_ARGS=<extra;args>] [-DWORKDIR=<dir>] -P run_arcguard.cmake
#
# --arc-guard quarantines a freed object by stamping its refcount slot with a
# marker instead of letting the block be recycled, and traps any later
# retain/release reaching it. That catches the bug class plain refcounting
# cannot see: a *missing* retain, where the counts balance but a reference
# outlives the object it points at. The report names both ends -- the stale use
# and the release that ended the block's life.
#
# Why this tier and not a sanitizer: ASan only sees libc malloc/free, so it
# catches a double free but never a use-after-free *read*, because the object
# code zanc emits is not instrumented and so never checks shadow memory. The
# ARC guard checks at the one place every stale reference must pass through
# (retain/release), which is exactly where the bug class shows up.

cmake_policy(SET CMP0012 NEW)

if(NOT ZANC OR NOT SRC OR NOT OUT_EXE)
  message(FATAL_ERROR "run_arcguard.cmake: ZANC, SRC and OUT_EXE are required")
endif()

# ---- up-to-date check ------------------------------------------------------
# The artifact is a pure function of (source, compiler, stdlib); reuse it when
# it is newer than all three. STDLIB_STAMP is touched whenever a stdlib source
# changes.
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

# A reused artifact must never make a failure sticky: drop the executable so
# the next run recompiles from scratch.
function(zan_drop_artifact)
  file(REMOVE ${OUT_EXE})
endfunction()

# ---- compile with the ARC guard ----
zan_artifact_is_current(_current ${OUT_EXE})
if(NOT _current)
  execute_process(
    COMMAND ${ZANC} ${SRC} -o ${OUT_EXE} --arc-guard ${ZANC_ARGS}
    RESULT_VARIABLE compile_rc
    OUTPUT_VARIABLE compile_out
    ERROR_VARIABLE  compile_err)
  if(NOT compile_rc EQUAL 0)
    message(FATAL_ERROR "compile failed (rc=${compile_rc})\n${compile_out}${compile_err}")
  endif()
endif()

# ---- run ----
# On Windows a freshly linked executable can transiently fail to launch with
# STATUS_SHARING_VIOLATION (0xC0000043) or STATUS_ACCESS_DENIED (0xC0000022)
# when antivirus/the loader briefly holds the new image open -- common under
# parallel ctest. Retry the launch a few times before treating it as a failure.
set(_run_attempt 0)
while(TRUE)
  math(EXPR _run_attempt "${_run_attempt} + 1")
  if(WORKDIR)
    execute_process(
      COMMAND ${OUT_EXE}
      WORKING_DIRECTORY ${WORKDIR}
      RESULT_VARIABLE run_rc
      OUTPUT_VARIABLE run_out
      ERROR_VARIABLE  run_err)
  else()
    execute_process(
      COMMAND ${OUT_EXE}
      RESULT_VARIABLE run_rc
      OUTPUT_VARIABLE run_out
      ERROR_VARIABLE  run_err)
  endif()
  if((run_rc MATCHES "[cC]0000043" OR run_rc MATCHES "[cC]0000022")
     AND _run_attempt LESS 6)
    execute_process(COMMAND ${CMAKE_COMMAND} -E sleep 0.4)
    continue()
  endif()
  break()
endwhile()

# The guard's own report is the signal worth naming explicitly: a bare non-zero
# exit says only "it died", while this says which reference outlived its object.
if(run_out MATCHES "ARC integrity failure" OR run_err MATCHES "ARC integrity failure")
  zan_drop_artifact()
  message(FATAL_ERROR "ARC integrity failure in ${SRC}:\n${run_out}${run_err}")
endif()

if(NOT run_rc EQUAL 0)
  zan_drop_artifact()
  message(FATAL_ERROR "program exited with ${run_rc}\nstdout:\n${run_out}\nstderr:\n${run_err}")
endif()

message(STATUS "arc-clean: ${SRC}")
