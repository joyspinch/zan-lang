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

if(NOT ZANC OR NOT SRC OR NOT OUT_EXE)
  message(FATAL_ERROR "run_leakcheck.cmake: ZANC, SRC and OUT_EXE are required")
endif()

# ---- compile with leak instrumentation ----
execute_process(
  COMMAND ${ZANC} ${SRC} -o ${OUT_EXE} --check-leaks ${ZANC_ARGS}
  RESULT_VARIABLE compile_rc
  OUTPUT_VARIABLE compile_out
  ERROR_VARIABLE  compile_err)
if(NOT compile_rc EQUAL 0)
  message(FATAL_ERROR "compile failed (rc=${compile_rc})\n${compile_out}${compile_err}")
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

if(run_out MATCHES "memory leak detected" OR run_err MATCHES "memory leak detected")
  message(FATAL_ERROR "leak detected in ${SRC}:\n${run_out}${run_err}")
endif()

# A non-zero exit that is *not* an ordinary program failure (the leak checker
# aborts with a non-zero status) still indicates a problem worth surfacing.
if(NOT run_rc EQUAL 0)
  message(FATAL_ERROR "program exited with ${run_rc}\nstdout:\n${run_out}\nstderr:\n${run_err}")
endif()

message(STATUS "leak-clean: ${SRC}")
