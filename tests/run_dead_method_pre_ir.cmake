# Run with cmake -DZANC=<compiler> -DSRC=<dead_method_pre_ir.zan>
#   -DWORKDIR=<repository> -P tests/run_dead_method_pre_ir.cmake
# The conformance .out covers behavior; --emit-ir must expose even the 32
# uncalled user methods, not replace their bodies with unreachable stubs.
execute_process(
  COMMAND "${ZANC}" "${SRC}" --emit-ir
  WORKING_DIRECTORY "${WORKDIR}"
  OUTPUT_VARIABLE _ir
  ERROR_VARIABLE _err
  RESULT_VARIABLE _rc)
if(NOT _rc EQUAL 0)
  message(FATAL_ERROR "run_dead_method_pre_ir: zanc failed (${_rc})\n${_err}")
endif()
string(REPLACE "\r" "" _ir "${_ir}")
foreach(_i RANGE 0 31)
  if(_i LESS 10)
    set(_name "Unused0${_i}")
  else()
    set(_name "Unused${_i}")
  endif()
  string(REGEX MATCH "define[^\n]*@Methods_${_name}\\([^\n]*\\) \\{\nentry:" _body "${_ir}")
  if(_body STREQUAL "")
    message(FATAL_ERROR "run_dead_method_pre_ir: @Methods_${_name} lost its user body")
  endif()
endforeach()
# Positive controls: named-call and static-init roots have real bodies.
foreach(_name Methods_ViaCall Boot_cctor)
  string(REGEX MATCH "define[^\n]*@${_name}\\([^\n]*\\) \\{\nentry:" _body "${_ir}")
  if(_body STREQUAL "")
    message(FATAL_ERROR "run_dead_method_pre_ir: @${_name} lost its body")
  endif()
endforeach()
message(STATUS "run_dead_method_pre_ir: 32 uncalled user bodies observable; live roots retained")
