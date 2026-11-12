# Build one current gen1 for all exception ownership oracles. CTest holds the
# shared build gate; source stability is checked across build and execution.
if(DEFINED OUTDIR AND NOT "${OUTDIR}" STREQUAL "")
  file(REMOVE "${OUTDIR}/passed.txt"
    "${OUTDIR}/allocation_string/passed.txt"
    "${OUTDIR}/allocation_nullable/passed.txt")
endif()
foreach(_required ZANC CLANG LLVM_AS OUTDIR)
  if(NOT DEFINED ${_required} OR "${${_required}}" STREQUAL "")
    message(FATAL_ERROR "${_required} is required")
  endif()
endforeach()
file(MAKE_DIRECTORY "${OUTDIR}")
file(REMOVE "${OUTDIR}/passed.txt")
include("${CMAKE_CURRENT_LIST_DIR}/exception_ownership_provenance.cmake")
ownership_pin_stdlib()
get_filename_component(_root "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(_manifest "${_root}/scripts/selfhost_sources.txt")
file(STRINGS "${_manifest}" _entries)
set(_sources)
foreach(_entry IN LISTS _entries)
  string(STRIP "${_entry}" _entry)
  if(NOT _entry STREQUAL "" AND NOT _entry MATCHES "^#")
    list(APPEND _sources "${_root}/src/${_entry}.zan")
  endif()
endforeach()
if(NOT _sources)
  message(FATAL_ERROR "Selfhost source manifest is empty")
endif()
set(_inputs "${ZANC}" "${CLANG}" "${LLVM_AS}" "${_manifest}"
  "${CMAKE_CURRENT_LIST_FILE}" ${_sources} ${RT_OBJS})
foreach(_lib IN LISTS SYSLIBS)
  if(EXISTS "${_lib}" AND NOT IS_DIRECTORY "${_lib}")
    list(APPEND _inputs "${_lib}")
  endif()
endforeach()
foreach(_oracle sync_pending_scope async_exception_identity async_exception_natural exception_payload_ownership)
  list(APPEND _inputs "${CMAKE_CURRENT_LIST_DIR}/${_oracle}.zan"
    "${CMAKE_CURRENT_LIST_DIR}/${_oracle}.out")
endforeach()
foreach(_runner run_sync_pending_scope run_async_exception_identity run_exception_payload_ownership)
  list(APPEND _inputs "${CMAKE_CURRENT_LIST_DIR}/${_runner}.cmake")
endforeach()
list(APPEND _inputs
  "${CMAKE_CURRENT_LIST_DIR}/string_allocation_balance.zan"
  "${CMAKE_CURRENT_LIST_DIR}/nullable_allocation_balance.zan"
  "${CMAKE_CURRENT_LIST_DIR}/string_allocation_monitor.c"
  "${CMAKE_CURRENT_LIST_DIR}/run_string_allocation_balance.cmake"
  "${CMAKE_CURRENT_LIST_DIR}/exception_ownership_provenance.cmake")
ownership_capture(before _before)
set(GEN1 "${OUTDIR}/gen1${CMAKE_EXECUTABLE_SUFFIX}")
if(WIN32)
  set(GEN1 "${OUTDIR}/gen1.exe")
endif()
file(REMOVE "${GEN1}")
execute_process(COMMAND "${ZANC}" ${_sources} --auto-stdlib
  --stdlib-path "${STDLIB_ROOT}" --no-packages -O0 -o "${GEN1}"
  WORKING_DIRECTORY "${OWNERSHIP_WORKDIR}"
  RESULT_VARIABLE _rc OUTPUT_VARIABLE _out ERROR_VARIABLE _err TIMEOUT 300)
file(WRITE "${OUTDIR}/build.exit" "${_rc}\n")
file(WRITE "${OUTDIR}/build.stdout" "${_out}")
file(WRITE "${OUTDIR}/build.stderr" "${_err}")
if(NOT _rc STREQUAL "0" OR NOT EXISTS "${GEN1}")
  message(FATAL_ERROR "Exception suite gen1 build failed: ${_rc}\n${_out}${_err}")
endif()
file(SHA256 "${GEN1}" _gen1_before)
file(WRITE "${OUTDIR}/gen1.sha256" "${_gen1_before}\n")
foreach(_runner run_sync_pending_scope run_async_exception_identity run_exception_payload_ownership)
  execute_process(COMMAND "${CMAKE_COMMAND}"
    "-DGEN1=${GEN1}" "-DCLANG=${CLANG}" "-DLLVM_AS=${LLVM_AS}"
    "-DSTDLIB_ROOT=${STDLIB_ROOT}"
    "-DOUTDIR=${OUTDIR}/${_runner}" "-DRT_OBJS=${RT_OBJS}" "-DSYSLIBS=${SYSLIBS}"
    -P "${CMAKE_CURRENT_LIST_DIR}/${_runner}.cmake"
    RESULT_VARIABLE _rc OUTPUT_VARIABLE _out ERROR_VARIABLE _err TIMEOUT 900)
  file(WRITE "${OUTDIR}/${_runner}.log" "exit=${_rc}\n${_out}${_err}")
  if(NOT _rc STREQUAL "0")
    message(FATAL_ERROR "Exception suite ${_runner} failed: ${_rc}\n${_out}${_err}")
  endif()
  message(STATUS "${_out}")
endforeach()
foreach(_fixture string nullable)
  set(_allocation_out "${OUTDIR}/allocation_${_fixture}")
  execute_process(COMMAND "${CMAKE_COMMAND}"
    "-DGEN1=${GEN1}" "-DCLANG=${CLANG}" "-DLLVM_AS=${LLVM_AS}"
    "-DSTDLIB_ROOT=${STDLIB_ROOT}" "-DFIXTURE=${_fixture}"
    "-DOUTDIR=${_allocation_out}" "-DRT_OBJS=${RT_OBJS}" "-DSYSLIBS=${SYSLIBS}"
    -P "${CMAKE_CURRENT_LIST_DIR}/run_string_allocation_balance.cmake"
    WORKING_DIRECTORY "${OWNERSHIP_WORKDIR}"
    RESULT_VARIABLE _rc OUTPUT_VARIABLE _out ERROR_VARIABLE _err TIMEOUT 900)
  file(WRITE "${OUTDIR}/allocation_${_fixture}.log" "exit=${_rc}\n${_out}${_err}")
  if(NOT _rc STREQUAL "0" OR NOT EXISTS "${_allocation_out}/passed.txt")
    message(FATAL_ERROR "Exception suite allocation ${_fixture} failed: ${_rc}\n${_out}${_err}")
  endif()
  message(STATUS "${_out}")
endforeach()
ownership_capture(after _after)
file(SHA256 "${GEN1}" _gen1_after)
if(NOT _before STREQUAL _after OR NOT _gen1_before STREQUAL _gen1_after)
  message(FATAL_ERROR "Exception suite input drift; evidence is diagnostic only")
endif()
file(WRITE "${OUTDIR}/passed.txt" "One freshly built gen1 passed pending scope, async identity/natural lifetime, payload ownership and string/nullable allocation balance with stable inputs.\n")
message(STATUS "Selfhost exception ownership suite passed")
