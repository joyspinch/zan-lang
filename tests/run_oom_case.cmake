# OOM-injection harness for runtime fail-fast / recovery paths (B-ID98).
#
# Invoked as:
#   cmake -DZANC=<zanc> -DSRC=<file.zan> -DEXPECTED=<file.out> \
#         -DOUT_EXE=<exe path> -DSTDLIB_STAMP=<stamp> \
#         -DOOM_ENV=<ZAN_OOM_INJECT_SITE value> \
#         [-DEXPECT_RC=nonzero] [-DERR_MARKER=<stderr substring>] \
#         [-DZANC_ARGS=<extra;args>] -P run_oom_case.cmake
#
# Compiles like run_case.cmake, then runs the program with
# ZAN_OOM_INJECT_SITE set. Default expectation: exit 0 and stdout matches the
# golden. With EXPECT_RC=nonzero the process must NOT exit 0 (fatal paths);
# with ERR_MARKER the given substring must appear on stderr.

cmake_policy(SET CMP0012 NEW)

if(NOT ZANC OR NOT SRC OR NOT EXPECTED OR NOT OUT_EXE OR NOT OOM_ENV)
  message(FATAL_ERROR "run_oom_case.cmake: ZANC, SRC, EXPECTED, OUT_EXE and OOM_ENV are required")
endif()

if(NOT WORKDIR AND CMAKE_SCRIPT_MODE_FILE)
  get_filename_component(_script_dir "${CMAKE_SCRIPT_MODE_FILE}" DIRECTORY)
  get_filename_component(WORKDIR "${_script_dir}/.." ABSOLUTE)
endif()

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
    if(_arg MATCHES "\\.(zan|html|zcomp)$" AND EXISTS "${_arg}"
       AND "${_arg}" IS_NEWER_THAN "${artifact}")
      return()
    endif()
  endforeach()
  set(${out_var} TRUE PARENT_SCOPE)
endfunction()

function(zan_drop_artifact)
  file(REMOVE ${OUT_EXE})
endfunction()

zan_artifact_is_current(_current ${OUT_EXE})
if(NOT _current)
  execute_process(
    COMMAND ${ZANC} ${SRC} -o ${OUT_EXE} ${ZANC_ARGS}
            --package-project ${WORKDIR}
    RESULT_VARIABLE compile_rc
    OUTPUT_VARIABLE compile_out
    ERROR_VARIABLE  compile_err)
  if(NOT compile_rc EQUAL 0)
    message(FATAL_ERROR "compile failed (rc=${compile_rc})\n${compile_out}${compile_err}")
  endif()
endif()

# ---- run under injection ----------------------------------------------------
set(_run_attempt 0)
while(TRUE)
  math(EXPR _run_attempt "${_run_attempt} + 1")
  set(ENV{ZAN_OOM_INJECT_SITE} "${OOM_ENV}")
  execute_process(
    COMMAND ${OUT_EXE}
    WORKING_DIRECTORY ${WORKDIR}
    RESULT_VARIABLE run_rc
    OUTPUT_VARIABLE actual
    ERROR_VARIABLE  actual_err
    TIMEOUT 120
    ENCODING UTF-8)
  unset(ENV{ZAN_OOM_INJECT_SITE})
  if((run_rc MATCHES "[cC]0000043" OR run_rc MATCHES "[cC]0000022")
     AND _run_attempt LESS 6)
    execute_process(COMMAND ${CMAKE_COMMAND} -E sleep 0.4)
    continue()
  endif()
  break()
endwhile()

# ---- exit-code expectation --------------------------------------------------
if(DEFINED EXPECT_RC AND EXPECT_RC STREQUAL "nonzero")
  if(run_rc EQUAL 0)
    zan_drop_artifact()
    message(FATAL_ERROR "OOM case expected a nonzero exit (fatal path), got 0\nstderr:\n${actual_err}")
  endif()
else()
  if(NOT run_rc EQUAL 0)
    zan_drop_artifact()
    message(FATAL_ERROR "program exited with ${run_rc} under OOM injection\noutput:\n${actual}\nstderr:\n${actual_err}")
  endif()
endif()

# ---- stderr marker (fatal-path fingerprint) ---------------------------------
if(DEFINED ERR_MARKER AND NOT ERR_MARKER STREQUAL "")
  string(FIND "${actual_err}" "${ERR_MARKER}" _pos)
  if(_pos EQUAL -1)
    zan_drop_artifact()
    message(FATAL_ERROR "stderr marker not found: '${ERR_MARKER}'\nstderr:\n${actual_err}")
  endif()
endif()

# ---- stdout compare (normalize CRLF and trailing whitespace) -----------------
file(READ ${EXPECTED} expected)
string(REPLACE "\r\n" "\n" expected "${expected}")
string(REPLACE "\r\n" "\n" actual   "${actual}")
string(REGEX REPLACE "[ \t\r\n]+$" "" expected "${expected}")
string(REGEX REPLACE "[ \t\r\n]+$" "" actual   "${actual}")

if(NOT actual STREQUAL expected)
  zan_drop_artifact()
  message(FATAL_ERROR
    "output mismatch for ${SRC} under OOM injection\n--- expected ---\n${expected}\n--- actual ---\n${actual}\n--- stderr ---\n${actual_err}")
endif()

message(STATUS "OK (oom): ${SRC} [${OOM_ENV}]")
