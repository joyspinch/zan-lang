# Reuse the standard compile/run/golden harness with an outer deadline.
# cmake -DZANC=<build/zanc> -DBUILD_DIR=<build> -P tests/run_process_capture_timeout.cmake
cmake_minimum_required(VERSION 3.20)
if(NOT ZANC OR NOT BUILD_DIR)
  message(FATAL_ERROR "ZANC and BUILD_DIR are required")
endif()
get_filename_component(_root "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
file(MAKE_DIRECTORY "${BUILD_DIR}/cases" "${BUILD_DIR}/test-out")
if(WIN32)
  set(_ext .exe)
endif()
execute_process(
  COMMAND "${CMAKE_COMMAND}" "-DZANC=${ZANC}"
    "-DSRC=${_root}/tests/conformance/process_capture_timeout.zan"
    "-DEXPECTED=${_root}/tests/conformance/process_capture_timeout.out"
    "-DOUT_EXE=${BUILD_DIR}/cases/process_capture_timeout${_ext}"
    "-DZANC_ARGS=--auto-stdlib" "-DWORKDIR=${_root}"
    "-DSTDLIB_STAMP=${STDLIB_STAMP}"
    -P "${CMAKE_CURRENT_LIST_DIR}/run_case.cmake"
  TIMEOUT 45
  RESULT_VARIABLE _rc OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
file(WRITE "${BUILD_DIR}/test-out/process_capture_timeout.log" "${_out}${_err}")
if(NOT _rc STREQUAL "0")
  message(FATAL_ERROR "process_capture_timeout failed (${_rc})\n${_out}${_err}")
endif()
message(STATUS "${_out}")
