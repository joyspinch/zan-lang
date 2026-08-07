# Generator regression harness: compile a Zan program while capturing the
# code-generator reply (ZAN_GEN_REPLY), assert that the reply carries the
# expected rewrite directives and generated sources, then run the program and
# diff its stdout against a golden file.
#
# Invoked as:
#   cmake -DZANC=<zanc> -DSRC=<file.zan> -DEXPECTED=<file.out>
#         -DOUT_EXE=<exe path> -DREPLY=<reply json path>
#         "-DREPLY_REGEX=<re1>;<re2>;..." [-DZANC_ARGS=<extra;args>]
#         -P run_gen_case.cmake
#
# Unlike run_case.cmake this always recompiles: the reply file IS the input
# under test, so artifact reuse would skip the assertion.

cmake_policy(SET CMP0012 NEW)

if(NOT ZANC OR NOT SRC OR NOT EXPECTED OR NOT OUT_EXE OR NOT REPLY)
  message(FATAL_ERROR
    "run_gen_case.cmake: ZANC, SRC, EXPECTED, OUT_EXE and REPLY are required")
endif()

# ---- compile (capturing the generator reply) ----
execute_process(
  COMMAND ${CMAKE_COMMAND} -E env ZAN_GEN_REPLY=${REPLY}
          ${ZANC} ${SRC} -o ${OUT_EXE} ${ZANC_ARGS}
  RESULT_VARIABLE compile_rc
  OUTPUT_VARIABLE compile_out
  ERROR_VARIABLE  compile_err)
if(NOT compile_rc EQUAL 0)
  message(FATAL_ERROR "compile failed (rc=${compile_rc})\n${compile_out}${compile_err}")
endif()
if(NOT EXISTS ${REPLY})
  message(FATAL_ERROR "generator reply missing: ${REPLY}")
endif()
file(READ ${REPLY} reply)

# ---- assert on the reply (rewrite directives + generated sources) ----
foreach(_re ${REPLY_REGEX})
  string(REGEX MATCH "${_re}" _m "${reply}")
  if(NOT _m)
    message(FATAL_ERROR
      "generator reply missing expected pattern:\n  ${_re}\n--- reply head ---\n${reply}")
  endif()
endforeach()

# ---- run (with the same retry run_case.cmake uses for image-load races) ----
set(_run_attempt 0)
while(TRUE)
  math(EXPR _run_attempt "${_run_attempt} + 1")
  execute_process(
    COMMAND ${OUT_EXE}
    RESULT_VARIABLE run_rc
    OUTPUT_VARIABLE actual
    ENCODING UTF-8)
  if((run_rc MATCHES "[cC]0000043" OR run_rc MATCHES "[cC]0000022")
     AND _run_attempt LESS 6)
    execute_process(COMMAND ${CMAKE_COMMAND} -E sleep 0.4)
    continue()
  endif()
  break()
endwhile()
if(NOT run_rc EQUAL 0)
  message(FATAL_ERROR "program exited with ${run_rc}\noutput:\n${actual}")
endif()

# ---- compare (normalize CRLF and trailing whitespace) ----
file(READ ${EXPECTED} expected)
string(REPLACE "\r\n" "\n" expected "${expected}")
string(REPLACE "\r\n" "\n" actual   "${actual}")
string(REGEX REPLACE "[ \t\r\n]+$" "" expected "${expected}")
string(REGEX REPLACE "[ \t\r\n]+$" "" actual   "${actual}")

if(NOT actual STREQUAL expected)
  message(FATAL_ERROR
    "output mismatch for ${SRC}\n--- expected ---\n${expected}\n--- actual ---\n${actual}")
endif()

message(STATUS "OK: ${SRC}")
