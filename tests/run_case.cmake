# Portable compile-run-compare harness for a single Zan program.
#
# Invoked as:
#   cmake -DZANC=<zanc> -DSRC=<file.zan> -DEXPECTED=<file.out> \
#         -DOUT_EXE=<exe path> [-DZANC_ARGS=<extra;args>] [-DRUN_ARGS=<args>] \
#         [-DWORKDIR=<dir>] \
#         -P run_case.cmake
#
# Fails (non-zero) if compilation fails or the program's stdout does not
# match the golden file (line-ending normalized). When WORKDIR is given the
# compiled program is run with that working directory (so it can resolve
# relative paths, e.g. a self-hosting compiler reading a source file).

# Script mode carries no project policy version; without this, while(TRUE)
# treats TRUE as an (unset) variable and the run loop never executes.
cmake_policy(SET CMP0012 NEW)

if(NOT ZANC OR NOT SRC OR NOT EXPECTED OR NOT OUT_EXE)
  message(FATAL_ERROR "run_case.cmake: ZANC, SRC, EXPECTED and OUT_EXE are required")
endif()


# ---- up-to-date check ------------------------------------------------------
# Re-running the suite must not recompile programs whose inputs did not change:
# the artifact is a pure function of the source, compiler, stdlib and any
# explicit Zan source arguments, so a target newer than all of them is reused.
# STDLIB_STAMP is touched by the build whenever any stdlib source changes.
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
  foreach(_arg IN LISTS ZANC_ARGS)
    if(_arg MATCHES "\\.(zan|zform)$" AND EXISTS "${_arg}"
       AND "${_arg}" IS_NEWER_THAN "${artifact}")
      return()
    endif()
  endforeach()
  set(${out_var} TRUE PARENT_SCOPE)
endfunction()


# A reused artifact must never make a failure sticky: if the program crashed or
# its output did not match, drop the executable so the next run recompiles it
# from scratch (an image damaged by a transient build/AV race would otherwise be
# considered "current" forever).
function(zan_drop_artifact)
  file(REMOVE ${OUT_EXE})
endfunction()

# ---- compile ----
zan_artifact_is_current(_current ${OUT_EXE})
if(NOT _current)
  execute_process(
    COMMAND ${ZANC} ${SRC} -o ${OUT_EXE} ${ZANC_ARGS}
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
# parallel ctest. These are image-load races, not program failures, so retry
# the launch a few times before giving up.
set(_run_attempt 0)
while(TRUE)
  math(EXPR _run_attempt "${_run_attempt} + 1")
  if(WORKDIR)
    execute_process(
      COMMAND ${OUT_EXE} ${RUN_ARGS}
      WORKING_DIRECTORY ${WORKDIR}
      RESULT_VARIABLE run_rc
      OUTPUT_VARIABLE actual
      ENCODING UTF-8)
  else()
    execute_process(
      COMMAND ${OUT_EXE} ${RUN_ARGS}
      RESULT_VARIABLE run_rc
      OUTPUT_VARIABLE actual
      ENCODING UTF-8)
  endif()
  if((run_rc MATCHES "[cC]0000043" OR run_rc MATCHES "[cC]0000022")
     AND _run_attempt LESS 6)
    execute_process(COMMAND ${CMAKE_COMMAND} -E sleep 0.4)
    continue()
  endif()
  break()
endwhile()
if(NOT run_rc EQUAL 0)
  zan_drop_artifact()
  message(FATAL_ERROR "program exited with ${run_rc}\noutput:\n${actual}")
endif()

# ---- compare (normalize CRLF and trailing whitespace) ----
file(READ ${EXPECTED} expected)
string(REPLACE "\r\n" "\n" expected "${expected}")
string(REPLACE "\r\n" "\n" actual   "${actual}")
string(REGEX REPLACE "[ \t\r\n]+$" "" expected "${expected}")
string(REGEX REPLACE "[ \t\r\n]+$" "" actual   "${actual}")

if(NOT actual STREQUAL expected)
  zan_drop_artifact()
  message(FATAL_ERROR
    "output mismatch for ${SRC}\n--- expected ---\n${expected}\n--- actual ---\n${actual}")
endif()

message(STATUS "OK: ${SRC}")
