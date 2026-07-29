# Struct-by-value FFI ABI check.
#
# Invoked by ctest as:
#   cmake -DZANC=<zanc> -DSRC=<case.zan> -DTARGET=<cross target> \
#         -DEXPECTED=<expected/<target>.decls> -DWORKDIR=<dir> -P run_abi_case.cmake
#
# EXPECTED holds one `declare` line per extern, in the register/memory form the
# platform C compiler uses (each line was diffed against the declaration clang
# emits for the same C signature). A struct crossing the FFI boundary in the
# wrong form is silently miscompiled, so the shapes are pinned per target
# rather than only on the host.

execute_process(
  COMMAND "${ZANC}" "${SRC}" --target "${TARGET}" --emit-ir
  OUTPUT_VARIABLE _ir
  ERROR_VARIABLE _err
  RESULT_VARIABLE _rc
  WORKING_DIRECTORY "${WORKDIR}")

if(NOT _rc EQUAL 0)
  message(FATAL_ERROR "run_abi_case: zanc --target ${TARGET} failed (rc=${_rc})\n${_err}")
endif()

string(REPLACE "\r" "" _ir "${_ir}")

file(STRINGS "${EXPECTED}" _needles)
foreach(_n ${_needles})
  if(_n STREQUAL "")
    continue()
  endif()
  string(FIND "${_ir}" "${_n}" _pos)
  if(_pos EQUAL -1)
    message(FATAL_ERROR
      "run_abi_case: ${TARGET} IR does not declare the extern with the C ABI shape:\n  ${_n}")
  endif()
endforeach()
