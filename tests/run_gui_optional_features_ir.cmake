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

function(check_scoped_html source marker)
  execute_process(
    COMMAND "${ZANC}" "${WORKDIR}/tests/gui/${source}"
      --auto-stdlib --package-project "${WORKDIR}" --publish --emit-ir ${ARGN}
    WORKING_DIRECTORY "${WORKDIR}"
    OUTPUT_VARIABLE _ir ERROR_VARIABLE _err RESULT_VARIABLE _rc)
  if(NOT "${_rc}" STREQUAL "0")
    message(FATAL_ERROR "${source}: scoped HTML GUI publish IR failed (${_rc})\n${_err}")
  endif()
  string(FIND "${_ir}" "${marker}" _main)
  if(_main EQUAL -1)
    message(FATAL_ERROR "${source}: ${marker} is missing")
  endif()
  foreach(_symbol IN ITEMS ImageHttp HttpClient Tls ControlBootstrap SelectBox Upload Ribbon
      Html_CreateNative Html_ParseDesign UserComponentRegistry_CreateRegistered
      UserComponentRegistry_RegisterFactory ${_scoped_extra_exclusions})
    string(REGEX MATCH "define[^\n]*@${_symbol}[^\n(]*[(]" _definition "${_ir}")
    if(NOT _definition STREQUAL "")
      message(FATAL_ERROR "${source}: scoped HTML GUI retains optional implementation: ${_definition}")
    endif()
  endforeach()
  message(STATUS "${source}: scoped HTML GUI has no compatibility factory, remote transport or unused widgets")
endfunction()

check_scoped_html(docapp_test.zan GUI_DOCAPP_OK
  -DGUI_SCOPED_FACTORY --embed "${WORKDIR}/tests/gui/docapp_test.html")
check_scoped_html(compref_designer_test.zan GUI_SCOPED_COMPONENTS_OK -DGUI_SCOPED_COMPONENTS)
check_scoped_html(form_explicit_publish/Program.zan GUI_FORM_EXPLICIT_PUBLISH_OK
  --embed "${WORKDIR}/tests/gui/form_explicit_publish/main.html=publish-fixture/views/editor.html"
  --embed "${WORKDIR}/tests/gui/form_explicit_publish/actions.html=publish-fixture/fragments/actions.html")

set(_scoped_extra_exclusions ModelBinder_ DocWire_AutoWire Form_assembleDoc)
check_scoped_html(form_static_publish/Program.zan GUI_FORM_STATIC_PUBLISH_OK
  --embed "${WORKDIR}/tests/gui/form_static_publish/window.html=static-fixture/views/window.html"
  --embed "${WORKDIR}/tests/gui/form_static_publish/actions.html=static-fixture/fragments/actions.html"
  --embed "${WORKDIR}/tests/gui/form_static_publish/bound.html=static-fixture/bound.html")
