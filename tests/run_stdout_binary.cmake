# B-ID113 byte-exact pin: redirected stdout must NOT translate \n to \r\n.
# The generic run_case.cmake harness normalizes CRLF, which is exactly what
# this pin must catch — so this case runs byte-exact:
#   Console.Write("a\nb\n")        -> LF passthrough (C# semantics)
#   Console.WriteLine("...")       -> explicit CRLF (Environment.NewLine)
# Expected stdout bytes: "a\nb\n" + "len: 4\r\n" + "lf-raw: 1\r\n" + "end\r\n"
if(NOT ZANC OR NOT SRC OR NOT OUT_EXE)
  message(FATAL_ERROR "run_stdout_binary.cmake: ZANC, SRC and OUT_EXE are required")
endif()
execute_process(COMMAND ${ZANC} ${SRC} --auto-stdlib -o ${OUT_EXE}
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  RESULT_VARIABLE rc OUTPUT_VARIABLE oo ERROR_VARIABLE ee)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "compile failed: ${oo}${ee}")
endif()
set(_out ${OUT_EXE}.stdout)
execute_process(COMMAND ${OUT_EXE}
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
  RESULT_VARIABLE rc
  OUTPUT_FILE ${_out}
  ERROR_QUIET)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "run failed rc=${rc}")
endif()
file(READ ${_out} _bytes HEX)
string(TOLOWER "${_bytes}" _hex)
if(NOT _hex STREQUAL "610a620a6c656e3a20340d0a6c662d7261773a20310d0a656e640d0a")
  message(FATAL_ERROR "stdout bytes not C#-parity: ${_hex}")
endif()
if(EXISTS "${CMAKE_BINARY_DIR}/_stdio_pin.tmp")
  message(FATAL_ERROR "pin temp file leaked")
endif()
message(STATUS "stdout binary mode OK")
