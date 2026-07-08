# Regenerate the tokens/ast goldens for every case under tests/golden/cases.
#
#   cmake -DZANC=<path-to-zanc> [-DWORKDIR=<dir>] -P tests/golden/update_golden.cmake
#
# Run this deliberately after an intended compiler change, then review the diff.

if(NOT DEFINED ZANC)
  message(FATAL_ERROR "update_golden: pass -DZANC=<path-to-zanc>")
endif()
if(NOT DEFINED WORKDIR)
  set(WORKDIR "${CMAKE_CURRENT_BINARY_DIR}")
endif()

set(_here "${CMAKE_CURRENT_LIST_DIR}")
file(GLOB _cases RELATIVE "${_here}/cases" "${_here}/cases/*.zan")

function(_normalize var)
  set(s "${${var}}")
  string(REPLACE "\r" "" s "${s}")
  string(REGEX REPLACE "Compiled [^\n]*\n?" "" s "${s}")
  string(REGEX REPLACE " +\n" "\n" s "${s}")
  string(REGEX REPLACE "[ \t\r\n]+$" "" s "${s}")
  set(${var} "${s}" PARENT_SCOPE)
endfunction()

foreach(_case ${_cases})
  string(REGEX REPLACE "\\.zan$" "" _name "${_case}")
  foreach(_mode tokens ast)
    if(_mode STREQUAL "tokens")
      set(_flag "--dump-tokens")
    else()
      set(_flag "--dump-ast")
    endif()
    execute_process(
      COMMAND "${ZANC}" "${_here}/cases/${_case}" ${_flag}
      OUTPUT_VARIABLE _out RESULT_VARIABLE _rc
      WORKING_DIRECTORY "${WORKDIR}")
    if(NOT _rc EQUAL 0)
      message(FATAL_ERROR "update_golden: zanc ${_flag} on ${_case} failed (rc=${_rc})")
    endif()
    _normalize(_out)
    file(WRITE "${_here}/expected/${_name}.${_mode}" "${_out}\n")
    message(STATUS "wrote expected/${_name}.${_mode}")
  endforeach()
endforeach()
