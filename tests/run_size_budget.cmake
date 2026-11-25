# Hard size budgets for a minimal --publish build.
# See tests/size/hello_budget.zan — the point is to catch a
# reintroduced fixed static table or a cross link that lost --gc-sections
# (hello world cost ~20 KB of un-GC'd program text that way).
#
# Invoked as:
#   cmake -DZANC=<zanc> -DSRC=<file.zan> -DOUT=<artifact>
#         -DSIZE_TOOL=<llvm-size> -DMAX_TEXT=<bytes> -DMAX_FILE=<bytes>
#         [-DTARGET=<zan target>] [-DWORKDIR=<dir>]
#         [-DPUBLISH=OFF]   (measure a default dev build instead of --publish)
#         [-DNM_TOOL=<llvm-nm> -DFORBID_SYMBOLS=<sym;sym>]
#         -P run_size_budget.cmake
#
# FORBID_SYMBOLS names runtime pieces a minimal program must not carry (for
# example the M:N scheduler's _co_worker): a size ceiling with page-rounded
# Mach-O segments cannot see a 15 KB regression that still fits the padding.
#
# Ceilings live in the registering CMakeLists block next to this script and
# carry ~1.5x headroom over the measured baseline, so LLVM-version drift does
# not false-fail while class-level regressions still trip them. When a budget
# is raised on purpose, update the baseline numbers in the doc as well.

cmake_policy(SET CMP0012 NEW)

if(NOT ZANC OR NOT SRC OR NOT OUT OR NOT SIZE_TOOL OR NOT MAX_TEXT OR NOT MAX_FILE)
  message(FATAL_ERROR "run_size_budget.cmake: ZANC, SRC, OUT, SIZE_TOOL, MAX_TEXT, MAX_FILE are required")
endif()

set(_publish_args --publish)
if(DEFINED PUBLISH AND NOT PUBLISH)
  set(_publish_args "")
endif()

set(_target_args "")
if(TARGET)
  set(_target_args --target ${TARGET})
endif()

# --no-icon: the built-in Windows icon is a shipped product asset (~106 KB
# since the 2026-10-20 icon fix), not runtime payload. This budget exists to
# catch a reintroduced fixed static table or a lost --gc-sections, so the
# measurement must not move when the icon is regenerated.
execute_process(
  COMMAND ${ZANC} ${SRC} --auto-stdlib ${_publish_args} --no-icon ${_target_args} -o ${OUT}
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
  # llvm-size -A prints the same decimal shape for ELF, PE and Mach-O:
  #   .text       44336   5368713216     (Mach-O: __text)
  if(_line MATCHES "^(\\.text|__text)[ \t]+([0-9]+)[ \t]+[0-9]+")
    set(_text_size ${CMAKE_MATCH_2})
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
if(FORBID_SYMBOLS)
  if(NOT NM_TOOL)
    message(FATAL_ERROR "size_budget: FORBID_SYMBOLS needs NM_TOOL")
  endif()
  execute_process(COMMAND ${NM_TOOL} ${OUT} OUTPUT_VARIABLE _syms RESULT_VARIABLE _nm_rc)
  if(NOT _nm_rc EQUAL 0)
    message(FATAL_ERROR "size_budget: ${NM_TOOL} failed on ${OUT}")
  endif()
  foreach(_sym ${FORBID_SYMBOLS})
    if(_syms MATCHES "[ \t]${_sym}\n")
      string(APPEND _verdict "  links ${_sym} (runtime piece the program never uses)\n")
    endif()
  endforeach()
endif()
if(NOT _verdict STREQUAL "")
  message(FATAL_ERROR "size_budget: ${OUT} over budget\n${_verdict}(measured: .text=${_text_size} file=${_file_size}; icon excluded via --no-icon)")
endif()
message(STATUS "size_budget ok: ${OUT} .text=${_text_size} file=${_file_size}")
