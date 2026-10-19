# Repeat-compile stability guard: the compiler must not crash on a given input.
#
# Invoked as:
#   cmake -DZANC=<zanc> -DSRC=<file.zan> -DREPEATS=<n> [-DZANC_ARGS=<a;b;c>]
#         [-DWORKDIR=<dir>] -P run_repeat_compile.cmake
#
# Some compiler defects are *intermittent*: they only fire for part of the
# runs, because the bad value they dereference happens to be readable memory
# at that moment. A single compile therefore proves nothing. This guard
# compiles the same source N times and fails if any run dies with a crash
# exit code (0xC0000005 access violation, 0xC000001D illegal instruction,
# 0xC00000FD stack overflow) or any other nonzero status.
#
# Script mode carries no project policy version; without this, while(TRUE)
# treats TRUE as an (unset) variable and the run loop never executes.
cmake_policy(SET CMP0012 NEW)

if(NOT ZANC OR NOT SRC)
  message(FATAL_ERROR "run_repeat_compile.cmake: ZANC and SRC are required")
endif()
if(NOT REPEATS)
  set(REPEATS 8)
endif()
if(NOT WORKDIR AND CMAKE_SCRIPT_MODE_FILE)
  get_filename_component(_script_dir "${CMAKE_SCRIPT_MODE_FILE}" DIRECTORY)
  get_filename_component(WORKDIR "${_script_dir}/.." ABSOLUTE)
endif()

# Crash exit codes as reported by a Windows process (negative in CMake).
set(_crash_codes -1073741819 -1073741824 -1073741571 -1073740791)

set(_failures 0)
set(_last_rc 0)
# foreach(... RANGE ...) rather than for(... RANGE ...): the for() command no
# longer parses under CMake 4.x, and foreach(RANGE) works on every version.
foreach(_i RANGE 1 ${REPEATS})
  execute_process(
    COMMAND ${ZANC} ${SRC} ${ZANC_ARGS} --emit-ir
    WORKING_DIRECTORY ${WORKDIR}
    RESULT_VARIABLE rc
    OUTPUT_QUIET
    ERROR_VARIABLE  err)
  set(_last_rc ${rc})
  if(NOT rc EQUAL 0)
    # A crashed child comes back as a descriptive string ("Access violation",
    # "Illegal instruction", "Stack overflow", ...), not as the numeric exit
    # code -- so classify by shape first, then by code.
    set(_in_crash 0)
    if(rc MATCHES "^[0-9]+$")
      math(EXPR _rc_num "${rc}")
      foreach(_c IN LISTS _crash_codes)
        if(_rc_num EQUAL _c)
          set(_in_crash 1)
        endif()
      endforeach()
    else()
      set(_in_crash 1)
    endif()
    if(_in_crash)
      message(SEND_ERROR
        "compiler crashed on ${SRC} (run ${_i}/${REPEATS}, exit ${rc})\n${err}")
    else()
      message(SEND_ERROR
        "compile failed on ${SRC} (run ${_i}/${REPEATS}, exit ${rc})\n${err}")
    endif()
    math(EXPR _failures "${_failures} + 1")
  endif()
endforeach()

if(_failures GREATER 0)
  message(FATAL_ERROR "${_failures}/${REPEATS} runs of ${SRC} failed")
endif()
message(STATUS "OK: ${REPEATS}/${REPEATS} runs of ${SRC} succeeded")
