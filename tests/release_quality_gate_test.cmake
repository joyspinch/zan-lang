cmake_minimum_required(VERSION 3.20)
if(NOT DEFINED GATE OR NOT DEFINED TEST_DIR)
  message(FATAL_ERROR "GATE and TEST_DIR are required")
endif()
find_program(PYTHON_COMMAND NAMES python3 python REQUIRED)
# Pure synthetic Git/files/process fixture. No real compiler, build or CTest.
execute_process(COMMAND "${PYTHON_COMMAND}" -I -B
  "${CMAKE_CURRENT_LIST_DIR}/test_release_source_manifest.py"
  --gate "${GATE}" --test-dir "${TEST_DIR}" --cmake "${CMAKE_COMMAND}"
  RESULT_VARIABLE _rc OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
if(NOT _rc EQUAL 0)
  message(FATAL_ERROR "Release provenance/gate fixture failed: ${_out}${_err}")
endif()
message(STATUS "${_out}")
