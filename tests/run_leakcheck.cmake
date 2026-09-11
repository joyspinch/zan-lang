# Compile a Zan program with --check-leaks, run it, and fail if the runtime
# leak checker reports any object still reachable at exit.
#
# Invoked as:
#   cmake -DZANC=<zanc> -DSRC=<file.zan> -DOUT_EXE=<exe path> \
#         [-DZANC_ARGS=<extra;args>] -P run_leakcheck.cmake
#
# The --check-leaks build tracks every rc-managed allocation and, at program
# exit, prints "memory leak detected: N object(s) still reachable" (plus a
# per-site breakdown) to stderr. A clean program prints nothing and exits 0.

# Script mode carries no project policy version; without this, while(TRUE)
# treats TRUE as an (unset) variable and the run loop never executes.
cmake_policy(SET CMP0012 NEW)

if(NOT ZANC OR NOT SRC OR NOT OUT_EXE)
  message(FATAL_ERROR "run_leakcheck.cmake: ZANC, SRC and OUT_EXE are required")
endif()


# ---- up-to-date check ------------------------------------------------------
# Re-running the suite must not recompile programs whose inputs did not change:
# the artifact is a pure function of (source, compiler, stdlib, ZANC_ARGS), so
# a target newer than all three is reused. STDLIB_STAMP is touched by the build
# whenever any stdlib source changes. The compile args are remembered in a
# sidecar file: the args come from CMakeLists loops that have grown special
# cases over time (e.g. --embed for file_embed_bytes), and reusing an artifact
# built under older args silently keeps failing the case forever.
function(zan_artifact_is_current out_var artifact)
  set(${out_var} FALSE PARENT_SCOPE)
  if(NOT EXISTS ${artifact})
    return()
  endif()
  if(EXISTS "${artifact}.args")
    file(READ "${artifact}.args" _recorded_args)
  else()
    set(_recorded_args "<none>")
  endif()
  if(NOT _recorded_args STREQUAL "<args:${ZANC_ARGS}>")
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


# A reused artifact must never make a failure sticky: if the program crashed or
# its output did not match, drop the executable so the next run recompiles it
# from scratch (an image damaged by a transient build/AV race would otherwise be
# considered "current" forever).
function(zan_drop_artifact)
  file(REMOVE ${OUT_EXE})
  file(REMOVE "${OUT_EXE}.args")
endfunction()

# ---- compile with leak instrumentation ----
zan_artifact_is_current(_current ${OUT_EXE})
if(NOT _current)
  execute_process(
    COMMAND ${ZANC} ${SRC} -o ${OUT_EXE} --check-leaks ${ZANC_ARGS}
    RESULT_VARIABLE compile_rc
    OUTPUT_VARIABLE compile_out
    ERROR_VARIABLE  compile_err)
  if(NOT compile_rc EQUAL 0)
    message(FATAL_ERROR "compile failed (rc=${compile_rc})\n${compile_out}${compile_err}")
  endif()
  file(WRITE "${OUT_EXE}.args" "<args:${ZANC_ARGS}>")
endif()

# ---- run and capture the leak report (the checker may print to either stream) ----
# On Windows a freshly linked executable can transiently fail to launch with
# STATUS_SHARING_VIOLATION (0xC0000043) or STATUS_ACCESS_DENIED (0xC0000022)
# when antivirus/the loader briefly holds the new image open -- common under
# parallel ctest. Retry the launch a few times before treating it as a failure.
set(_run_attempt 0)
while(TRUE)
  math(EXPR _run_attempt "${_run_attempt} + 1")
  execute_process(
    COMMAND ${OUT_EXE}
    RESULT_VARIABLE run_rc
    OUTPUT_VARIABLE run_out
    ERROR_VARIABLE  run_err)
  if((run_rc MATCHES "[cC]0000043" OR run_rc MATCHES "[cC]0000022")
     AND _run_attempt LESS 6)
    execute_process(COMMAND ${CMAKE_COMMAND} -E sleep 0.4)
    continue()
  endif()
  break()
endwhile()

# EXPECT_LEAK_SITE flips the polarity for cases that keep an object alive on
# purpose: the run must print a leak report AND that report must name the
# expected site (a "file.zan:LINE:" fragment), pinning the report's per-site
# attribution instead of just the total count.
if(EXPECT_LEAK_SITE)
  if(NOT run_out MATCHES "memory leak detected" AND NOT run_err MATCHES "memory leak detected")
    zan_drop_artifact()
    message(FATAL_ERROR "expected a leak report in ${SRC} but none was printed\nstdout:\n${run_out}\nstderr:\n${run_err}")
  endif()
  if(NOT run_out MATCHES "${EXPECT_LEAK_SITE}" AND NOT run_err MATCHES "${EXPECT_LEAK_SITE}")
    zan_drop_artifact()
    message(FATAL_ERROR "leak report in ${SRC} does not name the expected site '${EXPECT_LEAK_SITE}':\nstdout:\n${run_out}\nstderr:\n${run_err}")
  endif()
  message(STATUS "leak-site ok: ${SRC}")
  return()
endif()

if(run_out MATCHES "memory leak detected" OR run_err MATCHES "memory leak detected")
  zan_drop_artifact()
  message(FATAL_ERROR "leak detected in ${SRC}:\n${run_out}${run_err}")
endif()

# A non-zero exit that is *not* an ordinary program failure (the leak checker
# aborts with a non-zero status) still indicates a problem worth surfacing.
if(NOT run_rc EQUAL 0)
  zan_drop_artifact()
  message(FATAL_ERROR "program exited with ${run_rc}\nstdout:\n${run_out}\nstderr:\n${run_err}")
endif()

message(STATUS "leak-clean: ${SRC}")
