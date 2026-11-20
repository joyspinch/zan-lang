# Included by run_selfhost_driver.cmake with the freshly built compiler.
set(_metadata "${OUTDIR}/declarations.json")
foreach(_mode IN ITEMS filtered unfiltered)
  set(_args --auto-stdlib)
  if(_mode STREQUAL "unfiltered")
    list(APPEND _args --no-pullin-filter)
  endif()
  file(REMOVE "${_metadata}")
  execute_process(COMMAND "${CMAKE_COMMAND}" -E env "ZAN_STDLIB_PATH=${_pullin}/stdlib"
    "${GEN1}" "${_pullin}/main.zan" ${_args} --gen-meta "${_metadata}"
    WORKING_DIRECTORY "${_root}" RESULT_VARIABLE rc
    OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr TIMEOUT 45)
  if(NOT "${rc}" STREQUAL "0" OR NOT EXISTS "${_metadata}")
    message(FATAL_ERROR "metadata ${_mode}: export failed, rc=${rc}\n${stdout}${stderr}")
  endif()
  file(READ "${_metadata}" _json)
  if(NOT _json MATCHES "\"name\":\"ReferencedController\"" OR _json MATCHES "\"from_stdlib\"")
    message(FATAL_ERROR "metadata ${_mode}: expected the referenced type and declarations only")
  endif()
  foreach(_name IN ITEMS UnusedController RoutedResource DerivedDevice)
    if((_mode STREQUAL "filtered" AND _json MATCHES "\"name\":\"${_name}\"") OR
       (_mode STREQUAL "unfiltered" AND NOT _json MATCHES "\"name\":\"${_name}\""))
      message(FATAL_ERROR "metadata ${_mode}: unexpected visibility for ${_name}")
    endif()
  endforeach()
endforeach()

set(_fixture "${_root}/tests/selfhost/metadata")
set(_args "${_fixture}/packages/Fixture/Part.zan" "${_fixture}/main.zan"
  --package-project "${_fixture}" --gen-meta-bodies)
file(REMOVE "${_metadata}")
execute_process(COMMAND "${GEN1}" ${_args} --gen-meta "${_metadata}"
  WORKING_DIRECTORY "${_root}" RESULT_VARIABLE rc
  OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr TIMEOUT 45)
if(NOT "${rc}" STREQUAL "0" OR NOT EXISTS "${_metadata}")
  message(FATAL_ERROR "metadata declarations: export failed, rc=${rc}\n${stdout}${stderr}")
endif()
file(READ "${_metadata}" _json)
if(NOT _json MATCHES "\"version\":1" OR NOT _json MATCHES "\"bases\":\\[\"IMarker\",\"Base\"\\]"
   OR NOT _json MATCHES "\"k\":\"typeof\",\"t\":\"GeneratedType\""
   OR NOT _json MATCHES "\"by_ref\":2" OR NOT _json MATCHES "\"name\":\"Mark\"")
  message(FATAL_ERROR "metadata declarations: merged bases, attributes, parameters or typeof missing")
endif()

# CMake 3.19 adds semantic JSON comparison. Older releases still run the
# driver and declaration checks above.
if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.19 AND ZANC)
  set(_host_metadata "${OUTDIR}/host-declarations.json")
  file(REMOVE "${_host_metadata}")
  execute_process(COMMAND "${ZANC}" ${_args} --gen-meta "${_host_metadata}"
    WORKING_DIRECTORY "${_root}" RESULT_VARIABLE rc
    OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr TIMEOUT 45)
  if(NOT "${rc}" STREQUAL "0" OR NOT EXISTS "${_host_metadata}")
    message(FATAL_ERROR "host metadata: export failed, rc=${rc}\n${stdout}${stderr}")
  endif()
  file(READ "${_host_metadata}" _host_json)
  string(JSON _classes GET "${_json}" classes)
  string(JSON _host_classes GET "${_host_json}" classes)
  string(JSON _same EQUAL "${_classes}" "${_host_classes}")
  if(NOT _same)
    message(FATAL_ERROR "declaration metadata differs from the host\n--- gen1 ---\n${_classes}\n--- gen0 ---\n${_host_classes}")
  endif()
  file(REMOVE "${_host_metadata}")
endif()

file(SHA256 "${_metadata}" _before)
set(_bad "${OUTDIR}/invalid-exponent.zan")
file(WRITE "${_bad}" "class Invalid { double value = 1e+; }\n")
execute_process(COMMAND "${GEN1}" "${_bad}" --gen-meta "${_metadata}"
  WORKING_DIRECTORY "${_root}" RESULT_VARIABLE rc
  OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr TIMEOUT 45)
file(REMOVE "${_bad}")
file(SHA256 "${_metadata}" _after)
if("${rc}" STREQUAL "0" OR NOT _before STREQUAL _after OR NOT "${stdout}${stderr}" MATCHES "exponent requires")
  message(FATAL_ERROR "invalid metadata input must fail and preserve the previous output, rc=${rc}\n${stdout}${stderr}")
endif()
file(REMOVE "${_metadata}")
