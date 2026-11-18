# Initialization roots must be executable demand, not every loaded type,
# metadata-only constructor/accessor retention, or mutually referring inits.
if(NOT ZANC OR NOT WORKDIR)
  message(FATAL_ERROR "run_publish_static_init_demand_ir: ZANC and WORKDIR are required")
endif()

set(_ir_dir "${WORKDIR}/build/test-out/publish-static-init-demand")
file(MAKE_DIRECTORY "${_ir_dir}")
foreach(_mode publish ordinary)
  set(_mode_args)
  set(_suffix "")
  if(_mode STREQUAL "publish")
    set(_mode_args --publish)
  else()
    set(_suffix "-ordinary")
  endif()
  foreach(_case publish_static_init_demand publish_static_init_demand_reflect)
    execute_process(
      COMMAND "${ZANC}" "${WORKDIR}/tests/conformance/${_case}.zan"
              --auto-stdlib --package-project "${WORKDIR}"
              ${_mode_args} --no-check-leaks --no-arc-guard --emit-ir
      WORKING_DIRECTORY "${WORKDIR}"
      OUTPUT_VARIABLE _ir ERROR_VARIABLE _err RESULT_VARIABLE _rc)
    if(NOT _rc EQUAL 0)
      message(FATAL_ERROR "${_case} (${_mode}): IR compilation failed (rc=${_rc})\n${_err}")
    endif()
    string(REPLACE "\r" "" _ir "${_ir}")
    file(WRITE "${_ir_dir}/${_case}${_suffix}.ll" "${_ir}")
    if(_case STREQUAL "publish_static_init_demand")
      set(_required "static demand begin" "static demand PASS")
      set(_required_stores
        "__zan_sf_StaticDemandRoot_Value" "__zan_sf_StaticDemandLeaf_Value")
      set(_forbidden_stores
        "__zan_sf_StaticDemandDead_Value"
        "__zan_sf_StaticDemandDeadPeer_Value"
        "__zan_sf_StaticDemandMetadata_Label"
        "__zan_sf_StaticDemandMetadataState_Value")
      set(_forbidden
        "STATIC_DEMAND_DEAD_FIELD"
        "STATIC_DEMAND_DEAD_CCTOR"
        "STATIC_DEMAND_METADATA_FIELD"
        "STATIC_DEMAND_METADATA_CCTOR"
        "STATIC_DEMAND_METADATA_CTOR"
        "STATIC_DEMAND_METADATA_GETTER_STATE"
        "__zan_sinit_StaticDemandDead"
        "__zan_sinit_StaticDemandMetadata"
        "__zan_sf_StaticDemandMetadataState_Value")
      set(_diagnostic_only
        "STATIC_DEMAND_DEAD_FIELD"
        "STATIC_DEMAND_METADATA_CTOR"
        "STATIC_DEMAND_METADATA_GETTER_STATE"
        "__zan_sf_StaticDemandMetadataState_Value")
    else()
      set(_required "static reflect begin" "static reflect PASS")
      set(_required_stores
        "__zan_sf_StaticDemandReflectInitRoot_Value"
        "__zan_sf_StaticDemandReflectPropertyState_Value"
        "__zan_sf_StaticDemandReflectSetterState_Value"
        "__zan_sf_StaticDemandReflectInvokeState_Value")
      set(_forbidden_stores "__zan_sf_StaticDemandReflectNever_Value")
      set(_forbidden "STATIC_DEMAND_REFLECT_NEVER" "__zan_sinit_StaticDemandReflectNever")
      set(_diagnostic_only "STATIC_DEMAND_REFLECT_NEVER")
    endif()
    if(_mode STREQUAL "ordinary")
      # Diagnostic-only bodies may retain their strings and zero-initialized
      # field storage. They must not create startup initialization functions
      # or write the forbidden static fields. Check stores below as LLVM can
      # inline and remove the __zan_sinit_ functions in either mode.
      list(REMOVE_ITEM _forbidden ${_diagnostic_only})
    endif()
    foreach(_text IN LISTS _required)
      string(FIND "${_ir}" "${_text}" _pos)
      if(_pos EQUAL -1)
        message(FATAL_ERROR "${_case} (${_mode}): missing positive control ${_text}")
      endif()
    endforeach()
    foreach(_text IN LISTS _forbidden)
      string(FIND "${_ir}" "${_text}" _pos)
      if(NOT _pos EQUAL -1)
        message(FATAL_ERROR "${_case} (${_mode}): unreachable static initialization retained ${_text}")
      endif()
    endforeach()
    # --emit-ir exposes optimized IR: initializer functions may be inlined
    # into main and deleted. Assert their actual writes, rather than names of
    # temporary coordinator/cctor functions or mere zero-initialized globals.
    foreach(_field IN LISTS _required_stores)
      if(NOT _ir MATCHES "store [^\n]*, ptr @${_field}(,|[ \t\n])")
        message(FATAL_ERROR "${_case} (${_mode}): missing live static initialization store ${_field}")
      endif()
    endforeach()
    foreach(_field IN LISTS _forbidden_stores)
      if(_ir MATCHES "store [^\n]*, ptr @${_field}(,|[ \t\n])")
        message(FATAL_ERROR "${_case} (${_mode}): unreachable static initialization store retained ${_field}")
      endif()
    endforeach()
  endforeach()
endforeach()
message(STATUS "publish_static_init_demand_ir: publish and ordinary initialization demand verified; dead cycles and metadata-only initialization absent")
