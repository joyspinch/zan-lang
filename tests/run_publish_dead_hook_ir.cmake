# Unreferenced On* methods must not independently root publish emission.
# The regular conformance golden checks the live entry point; this runner
# checks that dead hooks cannot pull their allocation graph into publish IR.
#
# Test name: publish_dead_hook_ir
# Invoke from any directory with absolute paths:
#   cmake -DZANC=<zanc> -DSRC=<publish_dead_hook.zan> -DWORKDIR=<repository>
#         -P tests/run_publish_dead_hook_ir.cmake
#
# --emit-ir writes IR to stdout without linking or running an executable.
# Keep the original fixture in place; all diagnostics go under build/test-out.
# Do not pass --obfuscate-strings: dead body markers must remain searchable.

if(NOT ZANC OR NOT SRC OR NOT WORKDIR)
  message(FATAL_ERROR
    "run_publish_dead_hook_ir: ZANC, SRC and WORKDIR are required")
endif()

get_filename_component(_case "${SRC}" NAME_WE)
set(_live_marker "publish dead hook PASS")
set(_dead_markers
  "PUBLISH_DEAD_HOOK_ONPAINT_MUST_BE_DEAD"
  "PUBLISH_DEAD_HOOK_ONUNUSED_MUST_BE_DEAD"
  "PUBLISH_DEAD_HOOK_PAYLOAD_MUST_BE_DEAD")
set(_dead_definitions
  "PublishDeadHookOwner_OnPaint"
  "PublishDeadHookOwner_OnUnusedAllocate"
  "PublishDeadHookPayload_[A-Za-z0-9_]+")
if(_case STREQUAL "publish_reflect_metadata_hook")
  set(_live_marker "publish reflect metadata hook PASS")
  set(_dead_markers
    "PUBLISH_REFLECT_METADATA_ONPAINT_MUST_BE_DEAD"
    "PUBLISH_REFLECT_METADATA_PAYLOAD_MUST_BE_DEAD")
  set(_dead_definitions
    "PublishReflectMetadataHookOwner_OnPaint"
    "PublishReflectMetadataHookPayload_[A-Za-z0-9_]+")
elseif(_case STREQUAL "publish_interface_candidates")
  set(_live_marker "publish interface candidates PASS")
  set(_dead_markers "PUBLISH_INTERFACE_CANDIDATE_MUST_BE_DEAD")
  set(_dead_definitions
    "PublishInterfaceDeadCandidate_[A-Za-z0-9_]+"
    "PublishInterfaceDeadPayload_[A-Za-z0-9_]+")
endif()

execute_process(
  COMMAND "${ZANC}" "${SRC}" --auto-stdlib --package-project "${WORKDIR}"
          --publish --no-check-leaks --no-arc-guard --emit-ir
  WORKING_DIRECTORY "${WORKDIR}"
  OUTPUT_VARIABLE _ir
  ERROR_VARIABLE _err
  RESULT_VARIABLE _rc)
string(REPLACE "\r" "" _ir "${_ir}")
set(_ir_dir "${WORKDIR}/build/test-out/${_case}")
file(MAKE_DIRECTORY "${_ir_dir}")
file(WRITE "${_ir_dir}/${_case}.ll" "${_ir}")
file(WRITE "${_ir_dir}/compiler.stderr.log" "${_err}")
if(NOT _rc EQUAL 0)
  message(FATAL_ERROR
    "run_publish_dead_hook_ir: zanc --publish --emit-ir failed (rc=${_rc})\n${_err}")
endif()

# Require both the fixture marker and a real Console call so an empty or
# unrelated IR stream cannot satisfy the negative assertions vacuously.
string(FIND "${_ir}" "${_live_marker}" _pos)
if(_pos EQUAL -1)
  message(FATAL_ERROR
    "run_publish_dead_hook_ir: publish IR lacks the live marker")
endif()
string(REGEX MATCH "call[^\n]*@(zan_rt_println|printf|puts)\\(" _live_call "${_ir}")
if(_live_call STREQUAL "")
  message(FATAL_ERROR
    "run_publish_dead_hook_ir: publish IR lacks the live Console call")
endif()

foreach(_forbidden IN LISTS _dead_markers)
  string(FIND "${_ir}" "${_forbidden}" _pos)
  if(NOT _pos EQUAL -1)
    message(FATAL_ERROR
      "run_publish_dead_hook_ir: publish IR retains dead marker: ${_forbidden}")
  endif()
endforeach()

# Check definitions as well as strings: a dead body replaced by an unreachable
# stub is still unwanted emission. Include the constructor reached only from
# the dead hook, rather than merely checking its caller.
foreach(_forbidden_definition IN LISTS _dead_definitions)
  string(REGEX MATCH "define[^\n]*@${_forbidden_definition}\\("
    _definition "${_ir}")
  if(NOT _definition STREQUAL "")
    message(FATAL_ERROR
      "run_publish_dead_hook_ir: publish IR retains dead definition: ${_definition}")
  endif()
endforeach()

message(STATUS
  "publish_dead_hook_ir: live marker/call retained; dead hooks and allocation graph absent")
