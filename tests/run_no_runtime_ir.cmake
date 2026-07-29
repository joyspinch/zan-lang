# [NoRuntime] must emit no ARC calls.
#
# Invoked by ctest as:
#   cmake -DZANC=<zanc> -DSRC=<case.zan> -DWORKDIR=<dir> -P run_no_runtime_ir.cmake
#
# The case holds two identical string-shuffling bodies. The managed one proves
# the check is looking at the right IR (it must retain/release); the
# [NoRuntime] one must contain no ARC call, because the runtime it would call
# is exactly what such a method cannot rely on.

execute_process(
  COMMAND "${ZANC}" "${SRC}" --emit-ir
  OUTPUT_VARIABLE _ir
  ERROR_VARIABLE _err
  RESULT_VARIABLE _rc
  WORKING_DIRECTORY "${WORKDIR}")

if(NOT _rc EQUAL 0)
  message(FATAL_ERROR "run_no_runtime_ir: zanc failed (rc=${_rc})\n${_err}")
endif()

string(REPLACE "\r" "" _ir "${_ir}")

function(body_of _fn _out)
  string(REGEX MATCH "define[^\n]*@${_fn}\\(([^\n]*)\n(.*)" _m "${_ir}")
  if(_m STREQUAL "")
    message(FATAL_ERROR "run_no_runtime_ir: no definition of @${_fn} in the IR")
  endif()
  string(FIND "${CMAKE_MATCH_2}" "\n}" _end)
  string(SUBSTRING "${CMAKE_MATCH_2}" 0 ${_end} _body)
  set(${_out} "${_body}" PARENT_SCOPE)
endfunction()

body_of(Program_Managed _managed)
body_of(Program_Bare _bare)

string(FIND "${_managed}" "@zan_rt_str_retain" _pos)
if(_pos EQUAL -1)
  message(FATAL_ERROR
    "run_no_runtime_ir: the managed body has no ARC call, so this test proves nothing")
endif()

foreach(_arc zan_rt_str_retain zan_rt_str_release zan_rt_retain zan_rt_release)
  string(FIND "${_bare}" "@${_arc}" _pos)
  if(NOT _pos EQUAL -1)
    message(FATAL_ERROR
      "run_no_runtime_ir: the [NoRuntime] body still calls @${_arc}")
  endif()
endforeach()
