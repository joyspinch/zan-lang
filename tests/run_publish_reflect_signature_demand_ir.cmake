# Lookup retains names. Descriptors follow the queries the compiler emits.
if(NOT ZANC OR NOT WORKDIR)
  message(FATAL_ERROR "run_publish_reflect_signature_demand_ir: ZANC and WORKDIR are required")
endif()
foreach(_case names late)
  execute_process(
    COMMAND "${ZANC}" "${WORKDIR}/tests/conformance/publish_reflect_signature_demand_${_case}.zan"
      --auto-stdlib --publish --emit-ir
    WORKING_DIRECTORY "${WORKDIR}"
    OUTPUT_VARIABLE _ir ERROR_VARIABLE _err RESULT_VARIABLE _rc)
  if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "signature demand ${_case}: compiler failed (rc=${_rc})\n${_err}")
  endif()
  if(_case STREQUAL "names")
    set(_required "define;names PASS;Describe;field name query")
    set(_forbidden "SignatureNamesReturn;SignatureNamesParameter;SIGNATURE_NAMES_ATTR_MUST_BE_DEAD;__zan.refl.ctors.")
    list(APPEND _forbidden SignatureNamesFieldType SIGNATURE_NAMES_FIELD_ALIAS_DEAD
      SIGNATURE_NAMES_FIELD_DEFAULT_DEAD SIGNATURE_NAMES_FIELD_ATTR_DEAD)
  else()
    set(_required "define;late signatures PASS;SignatureLateReturn;SignatureLateParameter;SIGNATURE_LATE_ATTR_MUST_STAY")
    list(APPEND _required SignatureLateFieldType SIGNATURE_LATE_FIELD_ALIAS_STAYS
      SIGNATURE_LATE_FIELD_DEFAULT_STAYS SIGNATURE_LATE_FIELD_ATTR_STAYS)
    list(APPEND _required "A11:DescriptionS0:29:SIGNATURE_LATE_ATTR_MUST_STAY")
    set(_forbidden "Description=SIGNATURE_LATE_ATTR_MUST_STAY")
  endif()
  foreach(_marker IN LISTS _required)
    string(FIND "${_ir}" "${_marker}" _pos)
    if(_pos EQUAL -1)
      message(FATAL_ERROR "signature demand ${_case}: missing positive control ${_marker}")
    endif()
  endforeach()
  foreach(_marker IN LISTS _forbidden)
    string(FIND "${_ir}" "${_marker}" _pos)
    if(NOT _pos EQUAL -1)
      message(FATAL_ERROR "signature demand ${_case}: unused metadata retained: ${_marker}")
    endif()
  endforeach()
endforeach()
message(STATUS "publish_reflect_signature_demand_ir: names retained; signature details follow emitted queries")
