# Guard: every globbed conformance case must have its golden <name>.out
# committed next to the source.
#
# Invoked as:
#   cmake -DSRC_DIR=<dir> [-DSKIP_CASES=a;b;c] -P run_golden_guard.cmake
#
# A case whose golden was never committed used to fail deep inside
# run_case.cmake with "file failed to open for reading: <name>.out", which
# reads like a program bug instead of "the golden is missing". Cases in
# SKIP_CASES have their own dedicated registration and therefore their own
# golden name (or none at all), so they are not checked here.
#
# Script mode carries no project policy version; without this, while(TRUE)
# treats TRUE as an (unset) variable and the run loop never executes.
cmake_policy(SET CMP0012 NEW)

if(NOT SRC_DIR)
  message(FATAL_ERROR "run_golden_guard.cmake: SRC_DIR is required")
endif()

file(GLOB _cases "${SRC_DIR}/*.zan")
set(_missing "")
foreach(_src ${_cases})
  get_filename_component(_name ${_src} NAME_WE)
  if(_name IN_LIST SKIP_CASES)
    continue()
  endif()
  if(NOT EXISTS "${SRC_DIR}/${_name}.out")
    list(APPEND _missing ${_name})
  endif()
endforeach()

if(_missing)
  string(REPLACE ";" ", " _list "${_missing}")
  message(FATAL_ERROR
    "conformance cases without a committed golden .out: ${_list}\n"
    "each case needs ${SRC_DIR}/<name>.out (the program's expected stdout)")
endif()

message(STATUS "OK: every conformance case has a golden")
