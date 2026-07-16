# Portable compile-run-compare harness for a single Zan program.
#
# Invoked as:
#   cmake -DZANC=<zanc> -DSRC=<file.zan> -DEXPECTED=<file.out> \
#         -DOUT_EXE=<exe path> [-DZANC_ARGS=<extra;args>] [-DWORKDIR=<dir>] \
#         -P run_case.cmake
#
# Fails (non-zero) if compilation fails or the program's stdout does not
# match the golden file (line-ending normalized). When WORKDIR is given the
# compiled program is run with that working directory (so it can resolve
# relative paths, e.g. a self-hosting compiler reading a source file).

if(NOT ZANC OR NOT SRC OR NOT EXPECTED OR NOT OUT_EXE)
  message(FATAL_ERROR "run_case.cmake: ZANC, SRC, EXPECTED and OUT_EXE are required")
endif()

# ---- compile ----
execute_process(
  COMMAND ${ZANC} ${SRC} -o ${OUT_EXE} ${ZANC_ARGS}
  RESULT_VARIABLE compile_rc
  OUTPUT_VARIABLE compile_out
  ERROR_VARIABLE  compile_err)
if(NOT compile_rc EQUAL 0)
  message(FATAL_ERROR "compile failed (rc=${compile_rc})\n${compile_out}${compile_err}")
endif()

# ---- run ----
if(WORKDIR)
  execute_process(
    COMMAND ${OUT_EXE}
    WORKING_DIRECTORY ${WORKDIR}
    RESULT_VARIABLE run_rc
    OUTPUT_VARIABLE actual
    ENCODING UTF-8)
else()
  execute_process(
    COMMAND ${OUT_EXE}
    RESULT_VARIABLE run_rc
    OUTPUT_VARIABLE actual
    ENCODING UTF-8)
endif()
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
