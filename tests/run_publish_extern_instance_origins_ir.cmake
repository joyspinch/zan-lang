# Extern instance origins must retain interface bodies without a Zan allocation
# site; scalar-only extern results must not root metadata-only On* hooks.
# Keep each origin in a separate compilation so one fallback cannot mask another.
#
# Invoke from any directory with absolute paths:
#   cmake -DZANC=<zanc> -DWORKDIR=<repository>
#         -P tests/run_publish_extern_instance_origins_ir.cmake
#
# Sources stay at their fixed tests/conformance/helpers paths. --emit-ir writes
# stdout without linking or running, so no native DLL is required. Do not pass
# --obfuscate-strings: the body markers must remain searchable.

if(NOT ZANC OR NOT WORKDIR)
  message(FATAL_ERROR
    "run_publish_extern_instance_origins_ir: ZANC and WORKDIR are required")
endif()

# Publishing may inline Worker.Run into Main. Require the body marker as the
# argument of a real output call, so neither a bare constant nor a dead stub passes.
function(_require_worker_body _ir _case)
  string(REGEX MATCH
    "(^|\n)(@[^ \t\n=]+)[ \t]*=[^\n]*EXTERN_INSTANCE_BODY_RETAINED[^\n]*"
    _literal "${_ir}")
  if(_literal STREQUAL "")
    message(FATAL_ERROR "${_case}: publish IR lacks the Worker_Run body marker")
  endif()
  set(_marker_symbol "${CMAKE_MATCH_2}")
  string(REGEX MATCHALL "call[^\n]*@(zan_rt_println|printf|puts)\\([^\n]*"
    _calls "${_ir}")
  set(_has_body_call FALSE)
  foreach(_call IN LISTS _calls)
    string(FIND "${_call}" "${_marker_symbol}" _marker_use)
    if(NOT _marker_use EQUAL -1)
      set(_has_body_call TRUE)
    endif()
  endforeach()
  if(NOT _has_body_call)
    message(FATAL_ERROR
      "${_case}: publish IR lacks a live output call using the Worker_Run marker")
  endif()
endfunction()

set(_source_dir
  "${WORKDIR}/tests/conformance/helpers/publish_extern_instance_origins")
set(_ir_dir "${WORKDIR}/build/test-out/publish-extern-instance-origins")
file(MAKE_DIRECTORY "${_ir_dir}")

foreach(_case IN ITEMS struct_return array_return struct_out delegate_return callback_argument scalar_control)
  set(_src "${_source_dir}/${_case}.zan")
  if(NOT EXISTS "${_src}")
    message(FATAL_ERROR "${_case}: missing fixed fixture ${_src}")
  endif()
  execute_process(
    COMMAND "${ZANC}" "${_src}" --auto-stdlib --package-project "${WORKDIR}"
            --publish --no-check-leaks --no-arc-guard --emit-ir
    WORKING_DIRECTORY "${WORKDIR}"
    OUTPUT_VARIABLE _ir
    ERROR_VARIABLE _err
    RESULT_VARIABLE _rc)
  string(REPLACE "\r" "" _ir "${_ir}")
  file(WRITE "${_ir_dir}/${_case}.ll" "${_ir}")
  file(WRITE "${_ir_dir}/${_case}.stderr.log" "${_err}")
  if(NOT "${_rc}" STREQUAL "0")
    message(FATAL_ERROR
      "${_case}: zanc --publish --emit-ir failed (rc=${_rc})\n${_err}")
  endif()

  # Prevent empty or unrelated IR from satisfying the negative assertions.
  string(FIND "${_ir}" "EXTERN_INSTANCE_MAIN" _main_marker)
  string(REGEX MATCH "call[^\n]*@(zan_rt_println|printf|puts)\\("
    _live_call "${_ir}")
  if(_main_marker EQUAL -1 OR _live_call STREQUAL "")
    message(FATAL_ERROR
      "${_case}: publish IR lacks the Main marker or a live Console call")
  endif()

  # The imported function must be called, not merely declared or silently
  # substituted by a default value. The scalar control uses all three results.
  if(_case STREQUAL "struct_return")
    set(_native_symbols ExternInstanceStructReturn)
  elseif(_case STREQUAL "array_return")
    set(_native_symbols ExternInstanceArrayReturn)
  elseif(_case STREQUAL "struct_out")
    set(_native_symbols ExternInstanceStructOut)
  elseif(_case STREQUAL "delegate_return")
    set(_native_symbols ExternInstanceDelegateReturn)
  elseif(_case STREQUAL "callback_argument")
    set(_native_symbols ExternInstanceCallback)
  else()
    set(_native_symbols ExternScalarBytes ExternScalarString ExternScalarStruct ExternScalarAction)
  endif()
  foreach(_symbol IN LISTS _native_symbols)
    string(REGEX MATCH "call[^\n]*@${_symbol}\\(" _native_call "${_ir}")
    if(_native_call STREQUAL "")
      message(FATAL_ERROR "${_case}: publish IR lacks the extern call ${_symbol}")
    endif()
  endforeach()

  if(_case STREQUAL "scalar_control")
    string(FIND "${_ir}" "EXTERN_SCALAR_HOOK_MUST_BE_DEAD" _dead_marker)
    string(REGEX MATCH "define[^\n]*@DeadOwner_OnPaint\\("
      _dead_definition "${_ir}")
    if(NOT _dead_marker EQUAL -1 OR NOT _dead_definition STREQUAL "")
      message(FATAL_ERROR
        "${_case}: scalar extern results retain the metadata-only OnPaint hook")
    endif()
  else()
    _require_worker_body("${_ir}" "${_case}")
  endif()
endforeach()

message(STATUS
  "publish_extern_instance_origins_ir: extern instance bodies retained; scalar metadata-only hook absent")
