# Metadata-only reflection must not retain callable constructor thunks or
# allocation-site metadata for unrelated normally allocated classes and Lists.
# Dead CreateInstance/GetType helpers must not enable either capability.
#
# Invoke from any directory with absolute paths:
#   cmake -DZANC=<zanc> -DSRC=<publish_reflect_ctor_demand_metadata.zan>
#         -DWORKDIR=<repository> -P tests/run_publish_reflect_ctor_demand_ir.cmake
#
# Compiler command (IR goes to stdout; no executable is linked or run):
#   <zanc> <source> --auto-stdlib --package-project <repository>
#          --publish --no-check-leaks --no-arc-guard --emit-ir
# The regular conformance registration separately checks the runtime metadata
# count/parameter golden. Do not pass --obfuscate-strings: the plaintext marker
# is a positive signal for accidental constructor-body emission.

if(NOT ZANC OR NOT SRC OR NOT WORKDIR)
  message(FATAL_ERROR
    "run_publish_reflect_ctor_demand_ir: ZANC, SRC and WORKDIR are required")
endif()

execute_process(
  COMMAND "${ZANC}" "${SRC}" --auto-stdlib --package-project "${WORKDIR}"
          --publish --no-check-leaks --no-arc-guard --emit-ir
  WORKING_DIRECTORY "${WORKDIR}"
  OUTPUT_VARIABLE _ir
  ERROR_VARIABLE _err
  RESULT_VARIABLE _rc)
if(NOT _rc EQUAL 0)
  message(FATAL_ERROR
    "run_publish_reflect_ctor_demand_ir: zanc --publish --emit-ir failed (rc=${_rc})\n${_err}")
endif()
string(REPLACE "\r" "" _ir "${_ir}")
set(_ir_dir "${WORKDIR}/build/test-out/publish-reflect-ctor-demand")
file(MAKE_DIRECTORY "${_ir_dir}")
file(WRITE "${_ir_dir}/metadata.ll" "${_ir}")

# Positive controls prevent a missing/empty IR stream or an unrelated source
# from making every negative assertion pass vacuously. Constructor queries can
# fold to constants and their physical tables can disappear in publish IR;
# their behavior is checked by the matching runtime conformance golden.
foreach(_required
    "define"
    "metadata PASS")
  string(FIND "${_ir}" "${_required}" _pos)
  if(_pos EQUAL -1)
    message(FATAL_ERROR
      "run_publish_reflect_ctor_demand_ir: metadata IR lacks positive control: ${_required}")
  endif()
endforeach()

# Search symbol prefixes, not only definitions: unresolved references are also
# regressions. Type names remain valid ARC identity data; use the unrelated
# field-name payload and hook marker to detect metadata and callable retention.
foreach(_forbidden
    "__zan.refl.thunk.new."
    "__zan.refl.thunk.list_ctor."
    "__zan_refl_obj_type"
    "__zan_site_meta_tbl"
    "c\"SiteMetadataUnrelatedField\\00\""
    "CTOR_DEMAND_METADATA_BODY_MUST_BE_DEAD"
    "SITE_METADATA_UNRELATED_HOOK_MUST_BE_DEAD")
  string(FIND "${_ir}" "${_forbidden}" _pos)
  if(NOT _pos EQUAL -1)
    message(FATAL_ERROR
      "run_publish_reflect_ctor_demand_ir: metadata-only publish IR retains ${_forbidden}")
  endif()
endforeach()

message(STATUS
  "publish_reflect_ctor_demand_ir: explicit metadata checks retained; constructor thunks, dynamic type helper and unrelated hook absent")
