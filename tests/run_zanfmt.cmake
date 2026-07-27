# Runs zanfmt over a fixture and checks both output modes.
#
#   cmake -DZANFMT=<exe> -DSRC=<messy.zan> -DEXPECTED=<messy.expected> \
#         -DWORKFILE=<scratch copy> -P run_zanfmt.cmake
#
# Checks that --stdout matches the golden, that --check reports the unformatted
# fixture (exit 1), that an in-place format produces the golden, and that
# --check then accepts the result (exit 0, i.e. formatting is idempotent).

if(NOT ZANFMT OR NOT SRC OR NOT EXPECTED OR NOT WORKFILE)
  message(FATAL_ERROR "run_zanfmt.cmake: ZANFMT, SRC, EXPECTED and WORKFILE are required")
endif()

file(READ ${EXPECTED} expected)
string(REPLACE "\r\n" "\n" expected "${expected}")

execute_process(COMMAND ${ZANFMT} --stdout ${SRC}
  RESULT_VARIABLE rc OUTPUT_VARIABLE actual ENCODING UTF-8)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "zanfmt --stdout failed (rc=${rc})")
endif()
string(REPLACE "\r\n" "\n" actual "${actual}")
if(NOT actual STREQUAL expected)
  message(FATAL_ERROR "--stdout mismatch\n--- expected ---\n${expected}\n--- actual ---\n${actual}")
endif()

execute_process(COMMAND ${ZANFMT} --check ${SRC} RESULT_VARIABLE rc OUTPUT_QUIET)
if(rc EQUAL 0)
  message(FATAL_ERROR "--check accepted an unformatted file")
endif()

configure_file(${SRC} ${WORKFILE} COPYONLY)
execute_process(COMMAND ${ZANFMT} ${WORKFILE} RESULT_VARIABLE rc OUTPUT_QUIET)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "in-place format failed (rc=${rc})")
endif()
file(READ ${WORKFILE} formatted)
string(REPLACE "\r\n" "\n" formatted "${formatted}")
if(NOT formatted STREQUAL expected)
  message(FATAL_ERROR "in-place mismatch\n--- expected ---\n${expected}\n--- actual ---\n${formatted}")
endif()

execute_process(COMMAND ${ZANFMT} --check ${WORKFILE} RESULT_VARIABLE rc OUTPUT_QUIET)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "--check rejected an already formatted file (rc=${rc})")
endif()

message(STATUS "OK: zanfmt")
