if(NOT ZANC OR NOT WORKDIR)
  message(FATAL_ERROR "ZANC and WORKDIR are required")
endif()

foreach(_mode IN ITEMS demand conservative)
  if(_mode STREQUAL "conservative")
    set(_command "${CMAKE_COMMAND}" -E env ZAN_NO_PUBLISH_VSLOT_DEMAND=1 "${ZANC}")
  else()
    set(_command "${CMAKE_COMMAND}" -E env ZAN_NO_PUBLISH_VSLOT_DEMAND=0 "${ZANC}")
  endif()
  execute_process(
    COMMAND ${_command}
      "${WORKDIR}/tests/conformance/publish_virtual_slot_demand.zan"
      --auto-stdlib --publish --emit-ir
    WORKING_DIRECTORY "${WORKDIR}"
    OUTPUT_VARIABLE _ir ERROR_VARIABLE _err RESULT_VARIABLE _rc)
  if(NOT "${_rc}" STREQUAL "0")
    message(FATAL_ERROR "${_mode}: publish IR failed (${_rc})\n${_err}")
  endif()
  string(FIND "${_ir}" "virtual slot demand PASS" _main)
  if(_main EQUAL -1)
    message(FATAL_ERROR "${_mode}: missing live Main marker")
  endif()
  foreach(_marker IN ITEMS VSLOT_BASE_UNUSED_MUST_BE_DEAD VSLOT_CHILD_UNUSED_MUST_BE_DEAD)
    string(FIND "${_ir}" "${_marker}" _pos)
    if(_mode STREQUAL "demand" AND NOT _pos EQUAL -1)
      message(FATAL_ERROR "unused virtual body retained: ${_marker}")
    elseif(_mode STREQUAL "conservative" AND _pos EQUAL -1)
      message(FATAL_ERROR "fallback did not retain the virtual body: ${_marker}")
    endif()
  endforeach()
endforeach()

message(STATUS "virtual slot demand prunes unused bodies; fallback preserves full slots")
