# Directory-input harness: proves `zanc <dir>` compiles every .zan under a
# directory tree.
#
# Invoked as:
#   cmake -DZANC=<zanc> -DDIR=<dir> -DEXPECTED=<file.out> \
#         -DOUT_EXE=<exe path> -DEMPTYDIR=<empty dir> \
#         -P run_dir_input.cmake
#
# Why this is a contract: the whole-project build has to survive a project with
# thousands of source files. Passing every path on the command line exceeds the
# Windows command-line limit (~32K) and the process then never starts at all --
# silently, because the failure text contains no compiler diagnostic. Letting the
# compiler walk the tree itself keeps the command line constant-length and makes
# the enumeration rule live in exactly one place.
#
# Also checks the fail-closed side: a directory with no .zan files must be an
# error, not an empty compilation unit.

if(NOT ZANC OR NOT DIR OR NOT EXPECTED OR NOT OUT_EXE OR NOT EMPTYDIR)
  message(FATAL_ERROR "run_dir_input.cmake: ZANC, DIR, EXPECTED, OUT_EXE and EMPTYDIR are required")
endif()

file(REMOVE ${OUT_EXE})

execute_process(
  COMMAND ${ZANC} ${DIR} -o ${OUT_EXE}
  RESULT_VARIABLE compile_rc
  OUTPUT_VARIABLE compile_out
  ERROR_VARIABLE  compile_err)
if(NOT compile_rc EQUAL 0)
  message(FATAL_ERROR "directory compile failed (rc=${compile_rc})\n${compile_out}${compile_err}")
endif()
if(NOT EXISTS ${OUT_EXE})
  message(FATAL_ERROR "directory compile produced no ${OUT_EXE}")
endif()

execute_process(
  COMMAND ${OUT_EXE}
  RESULT_VARIABLE run_rc
  OUTPUT_VARIABLE actual
  ENCODING UTF-8)
if(NOT run_rc EQUAL 0)
  file(REMOVE ${OUT_EXE})
  message(FATAL_ERROR "program exited with ${run_rc}\noutput:\n${actual}")
endif()

file(READ ${EXPECTED} expected)
string(REPLACE "\r\n" "\n" expected "${expected}")
string(REPLACE "\r\n" "\n" actual   "${actual}")
string(REGEX REPLACE "[ \t\r\n]+$" "" expected "${expected}")
string(REGEX REPLACE "[ \t\r\n]+$" "" actual   "${actual}")
if(NOT actual STREQUAL expected)
  file(REMOVE ${OUT_EXE})
  message(FATAL_ERROR
    "output mismatch for ${DIR}\n--- expected ---\n${expected}\n--- actual ---\n${actual}")
endif()

# A directory without a single .zan file must fail closed with a readable
# diagnostic -- never a silent success that looks like "everything is fine".
file(MAKE_DIRECTORY "${EMPTYDIR}")
execute_process(
  COMMAND ${ZANC} ${EMPTYDIR} -o ${OUT_EXE}.empty
  RESULT_VARIABLE empty_rc
  OUTPUT_VARIABLE empty_out
  ERROR_VARIABLE  empty_err)
if(empty_rc EQUAL 0)
  message(FATAL_ERROR "empty directory was accepted as a source directory")
endif()
if(NOT "${empty_out}${empty_err}" MATCHES "no .zan source files")
  message(FATAL_ERROR
    "empty directory failed without the expected diagnostic:\n${empty_out}${empty_err}")
endif()

message(STATUS "OK: directory input ${DIR}")
