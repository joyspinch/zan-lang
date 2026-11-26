# List<T> slot-width tag checks must leave plain loops (list_width_hoist.cpp).
#
# Invoked by ctest as:
#   cmake -DZANC=<zanc> -DSRC=<case.zan> -DWORKDIR=<dir> -P run_list_width_hoist_ir.cmake
#
# Builds the case at the default -O2 with the post-optimisation IR dumped and
# requires the hoisted preheader check (`lw.esz.hoist`). A second build with
# ZAN_NO_LIST_HOIST=1 must not contain it, so the marker really comes from the
# pass and not from some other emission. The case's stdout is covered by the
# conformance run of the same file (hoisted vs. unhoisted must agree).

set(_ir_on "${WORKDIR}/list_width_hoist_on.ll")
set(_ir_off "${WORKDIR}/list_width_hoist_off.ll")
file(REMOVE "${_ir_on}" "${_ir_off}")

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env "ZAN_DUMP_OPT_IR=${_ir_on}"
          "${ZANC}" "${SRC}" --no-build-cache -O2 -o "${WORKDIR}/list_width_hoist_on"
  ERROR_VARIABLE _err RESULT_VARIABLE _rc WORKING_DIRECTORY "${WORKDIR}")
if(NOT _rc EQUAL 0)
  message(FATAL_ERROR "run_list_width_hoist_ir: zanc failed (rc=${_rc})\n${_err}")
endif()
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env "ZAN_DUMP_OPT_IR=${_ir_off}" ZAN_NO_LIST_HOIST=1
          "${ZANC}" "${SRC}" --no-build-cache -O2 -o "${WORKDIR}/list_width_hoist_off"
  ERROR_VARIABLE _err RESULT_VARIABLE _rc WORKING_DIRECTORY "${WORKDIR}")
if(NOT _rc EQUAL 0)
  message(FATAL_ERROR "run_list_width_hoist_ir: zanc (ZAN_NO_LIST_HOIST) failed (rc=${_rc})\n${_err}")
endif()

file(READ "${_ir_on}" _on)
file(READ "${_ir_off}" _off)
string(FIND "${_on}" "lw.esz.hoist" _pos_on)
string(FIND "${_off}" "lw.esz.hoist" _pos_off)
if(_pos_on EQUAL -1)
  message(FATAL_ERROR "run_list_width_hoist_ir: no width check was hoisted out of a loop")
endif()
if(NOT _pos_off EQUAL -1)
  message(FATAL_ERROR "run_list_width_hoist_ir: ZAN_NO_LIST_HOIST=1 still hoisted (marker not from the pass)")
endif()
message(STATUS "run_list_width_hoist_ir: ok")
