# Response-file harness: proves `zanc @file` reads its arguments from a file.
#
# Invoked as:
#   cmake -DZANC=<zanc> -DSRC=<file.zan> -DEXTRA=<extra.zan> \
#         -DEXPECTED=<file.out> -DOUT_EXE=<exe path> -DRSP=<rsp path> \
#         -P run_argfile.cmake
#
# Long command lines are what makes this a contract: a whole-project build
# exceeds the 8191-character limit cmd.exe imposes, so the inputs have to be
# passable through a file. Also checks that a missing response file fails
# closed instead of compiling something partial.

if(NOT ZANC OR NOT SRC OR NOT EXTRA OR NOT EXPECTED OR NOT OUT_EXE OR NOT RSP)
  message(FATAL_ERROR "run_argfile.cmake: ZANC, SRC, EXTRA, EXPECTED, OUT_EXE and RSP are required")
endif()

file(REMOVE ${OUT_EXE})
# Comment lines, quoted paths and several arguments per line all have to parse.
# Copy inputs below a non-ASCII directory: IDE response files are UTF-8, and on
# Windows these paths must behave exactly like paths passed directly in argv.
get_filename_component(_rsp_dir "${RSP}" DIRECTORY)
set(_unicode_dir "${_rsp_dir}/响应文件中文路径")
file(REMOVE_RECURSE "${_unicode_dir}")
file(MAKE_DIRECTORY "${_unicode_dir}")
get_filename_component(_src_name "${SRC}" NAME)
file(COPY "${SRC}" DESTINATION "${_unicode_dir}")
set(_unicode_src "${_unicode_dir}/${_src_name}")
set(_body "# inputs\n\"${_unicode_src}\"\n")
foreach(_extra ${EXTRA})
  get_filename_component(_extra_name "${_extra}" NAME)
  file(COPY "${_extra}" DESTINATION "${_unicode_dir}")
  set(_body "${_body}\"${_unicode_dir}/${_extra_name}\"\n")
endforeach()
file(WRITE ${RSP} "${_body}-o \"${OUT_EXE}\"\n")

execute_process(
  COMMAND ${ZANC} @${RSP}
  RESULT_VARIABLE compile_rc
  OUTPUT_VARIABLE compile_out
  ERROR_VARIABLE  compile_err)
if(NOT compile_rc EQUAL 0)
  message(FATAL_ERROR "compile via response file failed (rc=${compile_rc})\n${compile_out}${compile_err}")
endif()
if(NOT EXISTS ${OUT_EXE})
  message(FATAL_ERROR "response-file compile produced no ${OUT_EXE}")
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
    "output mismatch for ${SRC}\n--- expected ---\n${expected}\n--- actual ---\n${actual}")
endif()

# A response file that does not exist must be an error, not a silent skip.
execute_process(
  COMMAND ${ZANC} @${RSP}.missing
  RESULT_VARIABLE missing_rc
  OUTPUT_QUIET
  ERROR_QUIET)
if(missing_rc EQUAL 0)
  message(FATAL_ERROR "missing response file was accepted")
endif()

message(STATUS "OK: response file ${RSP}")
