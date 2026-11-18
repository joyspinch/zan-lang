if(NOT ZANC OR NOT WORKDIR)
  message(FATAL_ERROR "ZANC and WORKDIR are required")
endif()

execute_process(
  COMMAND "${ZANC}" "${WORKDIR}/tests/conformance/gui_optional_features_base.zan"
    --auto-stdlib --package-project "${WORKDIR}" --publish --emit-ir
  WORKING_DIRECTORY "${WORKDIR}"
  OUTPUT_VARIABLE _ir ERROR_VARIABLE _err RESULT_VARIABLE _rc)
if(NOT "${_rc}" STREQUAL "0")
  message(FATAL_ERROR "base GUI publish IR failed (${_rc})\n${_err}")
endif()
string(FIND "${_ir}" "GUI_OPTIONAL_FEATURES_BASE_OK" _main)
if(_main EQUAL -1)
  message(FATAL_ERROR "base GUI Main marker is missing")
endif()
foreach(_type IN ITEMS ModelBinder UiDriver UiDriverSession ThemeDrawer ThemeDrawerRelay GuiFrameProfiler)
  string(REGEX MATCH "define[^\n]*@${_type}_[^(\n]*\\(" _definition "${_ir}")
  if(NOT _definition STREQUAL "")
    message(FATAL_ERROR "base GUI retains optional implementation: ${_definition}")
  endif()
endforeach()
foreach(_marker IN ITEMS "zan_frame_perf.log" "firstframe whole_ms=" "fxp full=" "pframe present=")
  string(FIND "${_ir}" "${_marker}" _pos)
  if(NOT _pos EQUAL -1)
    message(FATAL_ERROR "base GUI retains profiler implementation: ${_marker}")
  endif()
endforeach()
message(STATUS "base GUI has no binding, automation, theme-drawer or profiler implementation")
