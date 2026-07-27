# Runs zandoc over a fixture and compares both renderings with their goldens.
#
#   cmake -DZANDOC=<exe> -DSRC=<sample.zan> -DEXPECTED_MD=<sample.md> \
#         -DEXPECTED_HTML=<sample.html> -DOUTFILE=<scratch file> \
#         -P run_zandoc.cmake
#
# Also checks -o, which writes the rendering to a file instead of stdout.

if(NOT ZANDOC OR NOT SRC OR NOT EXPECTED_MD OR NOT EXPECTED_HTML OR NOT OUTFILE)
  message(FATAL_ERROR "run_zandoc.cmake: ZANDOC, SRC, EXPECTED_MD, EXPECTED_HTML and OUTFILE are required")
endif()

function(zandoc_check mode expected_file)
  execute_process(COMMAND ${ZANDOC} ${SRC} ${mode}
    RESULT_VARIABLE rc OUTPUT_VARIABLE actual ENCODING UTF-8)
  if(NOT rc EQUAL 0)
    message(FATAL_ERROR "zandoc ${mode} failed (rc=${rc})")
  endif()
  file(READ ${expected_file} expected)
  string(REPLACE "\r\n" "\n" expected "${expected}")
  string(REPLACE "\r\n" "\n" actual "${actual}")
  string(REGEX REPLACE "[ \t\r\n]+$" "" expected "${expected}")
  string(REGEX REPLACE "[ \t\r\n]+$" "" actual "${actual}")
  if(NOT actual STREQUAL expected)
    message(FATAL_ERROR "zandoc ${mode} mismatch\n--- expected ---\n${expected}\n--- actual ---\n${actual}")
  endif()
endfunction()

zandoc_check(--md ${EXPECTED_MD})
zandoc_check(--html ${EXPECTED_HTML})

execute_process(COMMAND ${ZANDOC} ${SRC} --md -o ${OUTFILE}
  RESULT_VARIABLE rc OUTPUT_QUIET)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "zandoc -o failed (rc=${rc})")
endif()
file(READ ${OUTFILE} written)
file(READ ${EXPECTED_MD} expected)
string(REPLACE "\r\n" "\n" written "${written}")
string(REPLACE "\r\n" "\n" expected "${expected}")
string(REGEX REPLACE "[ \t\r\n]+$" "" written "${written}")
string(REGEX REPLACE "[ \t\r\n]+$" "" expected "${expected}")
if(NOT written STREQUAL expected)
  message(FATAL_ERROR "zandoc -o wrote something other than its stdout rendering")
endif()

message(STATUS "OK: zandoc")
