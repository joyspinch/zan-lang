# Hard size budgets for a minimal --publish build.
# See docs/archive/embedded_runtime_assessment.md — the point is to catch a
# reintroduced fixed static table or a cross link that lost --gc-sections
# (hello world cost ~20 KB of un-GC'd program text that way).
#
# Invoked as:
#   cmake -DZANC=<zanc> -DSRC=<file.zan> -DOUT=<artifact>
#         -DSIZE_TOOL=<llvm-size> -DMAX_TEXT=<bytes> -DMAX_FILE=<bytes>
#         [-DTARGET=<zan target>] [-DWORKDIR=<dir>]
#         -P run_size_budget.cmake
#
# Ceilings live in the registering CMakeLists block next to this script and
# carry ~1.5x headroom over the measured baseline, so LLVM-version drift does
# not false-fail while class-level regressions still trip them. When a budget
# is raised on purpose, update the baseline numbers in the doc as well.

cmake_policy(SET CMP0012 NEW)

if(NOT ZANC OR NOT SRC OR NOT OUT OR NOT SIZE_TOOL OR NOT MAX_TEXT OR NOT MAX_FILE)
  message(FATAL_ERROR "run_size_budget.cmake: ZANC, SRC, OUT, SIZE_TOOL, MAX_TEXT, MAX_FILE are required")
endif()

set(_target_args "")
if(TARGET)
  set(_target_args --target ${TARGET})
endif()

execute_process(
  COMMAND ${ZANC} ${SRC} --auto-stdlib --publish ${_target_args} -o ${OUT}
  RESULT_VARIABLE _rc
  OUTPUT_VARIABLE _out
  ERROR_VARIABLE _err)
if(NOT _rc EQUAL 0)
  message(FATAL_ERROR "size_budget: compile failed (rc=${_rc})\n${_out}${_err}")
endif()

file(SIZE ${OUT} _file_size)

execute_process(COMMAND ${SIZE_TOOL} -A ${OUT} OUTPUT_VARIABLE _sz)
set(_text_size "")
string(REPLACE "\n" ";" _lines "${_sz}")
foreach(_line ${_lines})
  # llvm-size -A prints the same decimal shape for ELF and PE:
  #   .text       44336   5368713216
  if(_line MATCHES "^\\.text[ \t]+([0-9]+)[ \t]+[0-9]+")
    set(_text_size ${CMAKE_MATCH_1})
  endif()
endforeach()
if(_text_size STREQUAL "")
  message(FATAL_ERROR "size_budget: no .text line in llvm-size output:\n${_sz}")
endif()

set(_verdict "")
if(_text_size GREATER MAX_TEXT)
  string(APPEND _verdict "  .text ${_text_size} > budget ${MAX_TEXT}\n")
endif()
if(_file_size GREATER MAX_FILE)
  string(APPEND _verdict "  file ${_file_size} > budget ${MAX_FILE}\n")
endif()
if(NOT _verdict STREQUAL "")
  message(FATAL_ERROR "size_budget: ${OUT} over budget\n${_verdict}(baseline: .text=${_text_size} file=${_file_size})")
endif()
message(STATUS "size_budget ok: ${OUT} .text=${_text_size} file=${_file_size}")
