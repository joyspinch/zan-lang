# Compile a Zan program with the --publish over-release net (A52-5) active,
# run it, and require a clean, net-silent run:
#   1. exit code 0 (the net must not change the exit status),
#   2. stdout matches the golden (a correct program's output is untouched),
#   3. NO net report anywhere (a correct program must not fire the net).
# The firing behavior (report + continue) is pinned by the hand-run injection
# probe recorded in TASKS.md.
#
# Invoked as:
#   cmake -DZANC=<zanc> -DSRC=<file.zan> -DEXPECTED=<golden> -DOUT_EXE=<path> \
#         [-DWORKDIR=<dir>] -P run_arcnet.cmake

cmake_policy(SET CMP0012 NEW)
if(NOT ZANC OR NOT SRC OR NOT EXPECTED OR NOT OUT_EXE)
  message(FATAL_ERROR "run_arcnet.cmake: ZANC, SRC, EXPECTED and OUT_EXE are required")
endif()

if(NOT WORKDIR AND CMAKE_SCRIPT_MODE_FILE)
  get_filename_component(_script_dir "${CMAKE_SCRIPT_MODE_FILE}" DIRECTORY)
  get_filename_component(WORKDIR "${_script_dir}/.." ABSOLUTE)
endif()

execute_process(
  COMMAND ${ZANC} ${SRC} --publish -o ${OUT_EXE}
  RESULT_VARIABLE compile_rc
  OUTPUT_VARIABLE compile_out
  ERROR_VARIABLE  compile_err)
if(NOT compile_rc EQUAL 0)
  message(FATAL_ERROR "compile failed (rc=${compile_rc})\n${compile_out}${compile_err}")
endif()

execute_process(
  COMMAND ${OUT_EXE}
  WORKING_DIRECTORY ${WORKDIR}
  RESULT_VARIABLE run_rc
  OUTPUT_VARIABLE run_out
  ERROR_VARIABLE  run_err)
if(NOT run_rc EQUAL 0)
  message(FATAL_ERROR "the net must not change the exit status, got ${run_rc}\nstdout:\n${run_out}\nstderr:\n${run_err}")
endif()
if(run_out MATCHES "ARC over-release net" OR run_err MATCHES "ARC over-release net")
  message(FATAL_ERROR "the net fired on a correct program:\nstdout:\n${run_out}\nstderr:\n${run_err}")
endif()
file(READ ${EXPECTED} expected)
string(REPLACE "\r\n" "\n" expected "${expected}")
string(REPLACE "\r\n" "\n" actual   "${run_out}")
string(REGEX REPLACE "[ \t\r\n]+$" "" expected "${expected}")
string(REGEX REPLACE "[ \t\r\n]+$" "" actual   "${actual}")
if(NOT actual STREQUAL expected)
  message(FATAL_ERROR "output mismatch for ${SRC}\n--- expected ---\n${expected}\n--- actual ---\n${actual}")
endif()
message(STATUS "arc net silent on correct program: ${SRC}")
