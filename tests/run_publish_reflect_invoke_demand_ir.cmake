# Callable hook demand must not change discovery or property access.
# Check body markers because optimization can inline the method symbols.
if(NOT ZANC OR NOT WORKDIR)
  message(FATAL_ERROR "run_publish_reflect_invoke_demand_ir: ZANC and WORKDIR are required")
endif()

foreach(_case literal dynamic)
  set(_src "${WORKDIR}/tests/conformance/publish_reflect_invoke_demand_${_case}.zan")
  execute_process(
    COMMAND "${ZANC}" "${_src}" --auto-stdlib --publish --emit-ir
    WORKING_DIRECTORY "${WORKDIR}"
    OUTPUT_VARIABLE _ir
    ERROR_VARIABLE _err
    RESULT_VARIABLE _rc)
  if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "invoke demand ${_case}: compiler failed (rc=${_rc})\n${_err}")
  endif()
  string(REPLACE "\r" "" _ir "${_ir}")
  set(_required "define;${_case} PASS")
  if(_case STREQUAL "literal")
    list(APPEND _required "chosen PASS" "late PASS")
  else()
    list(APPEND _required "INVOKE_UNKNOWN_UNUSED_BODY_MUST_STAY")
  endif()
  foreach(_marker IN LISTS _required)
    string(FIND "${_ir}" "${_marker}" _pos)
    if(_pos EQUAL -1)
      message(FATAL_ERROR "invoke demand ${_case}: missing positive control ${_marker}")
    endif()
  endforeach()
  if(_case STREQUAL "literal")
    string(FIND "${_ir}" "INVOKE_LITERAL_UNUSED_BODY_MUST_BE_DEAD" _pos)
    if(NOT _pos EQUAL -1)
      message(FATAL_ERROR "invoke demand literal: uncalled hook body retained")
    endif()
  endif()
endforeach()
message(STATUS "publish_reflect_invoke_demand_ir: literal hooks narrowed; unknown hooks retained")
