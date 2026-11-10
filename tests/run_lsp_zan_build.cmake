# tool_lsp_zan: the built pure-Zan LSP server binary (see the lsp_zan_tool
# custom target in the root CMakeLists) exists and exits cleanly on EOF
# stdin -- with no frames the Run() loop breaks immediately with rc 0.
if(NOT EXISTS "${EXE}")
  message(FATAL_ERROR "pure-Zan LSP binary missing: ${EXE}")
endif()
set(_stdin_file "${CMAKE_BINARY_DIR}/cases/lsp_zan_empty_stdin.txt")
file(WRITE "${_stdin_file}" "")
execute_process(COMMAND "${EXE}"
  INPUT_FILE "${_stdin_file}"
  RESULT_VARIABLE _rc
  TIMEOUT 30
  OUTPUT_QUIET ERROR_QUIET)
if(NOT _rc EQUAL 0)
  message(FATAL_ERROR "pure-Zan LSP binary exited ${_rc} on EOF stdin")
endif()
message(STATUS "tool_lsp_zan: binary OK, EOF-stdin exit 0")
