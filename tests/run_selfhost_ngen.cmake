# Self-hosting native backend (ngen) smoke gate.
#
# The ngen path emits machine code directly (arm64 object / wasm). It has no
# differential gate -- there is no host ngen to compare against -- so this
# gate asserts the properties that stand on their own:
#
#   1. a program the native backend can lower produces a non-empty object;
#   2. a program it must refuse produces NO object and a diagnostic
#      (fail closed: never a broken object, never a wrong answer);
#   3. the type check runs BEFORE the native backend, exactly like the C
#      host: a program with an undefined type is rejected with the host's
#      wording and no object is written.
#
# The programs live in tests/selfhost/ngen/.
#
# Invoked as:
#   cmake -DZANC=<zanc> -DSELFHOST_SOURCES=<list> -DGEN1=<exe> -DOUTDIR=<dir>
#         -DOK_PROG=<zan> -DBAD_MEMBER_PROG=<zan> -DBAD_TYPE_PROG=<zan>
#         -P run_selfhost_ngen.cmake

cmake_policy(SET CMP0012 NEW)
if(NOT ZANC OR NOT GEN1 OR NOT OUTDIR)
  message(FATAL_ERROR "run_selfhost_ngen.cmake: ZANC, GEN1 and OUTDIR are required")
endif()
if(NOT OK_PROG OR NOT BAD_MEMBER_PROG OR NOT BAD_TYPE_PROG)
  message(FATAL_ERROR "run_selfhost_ngen.cmake: OK_PROG, BAD_MEMBER_PROG and BAD_TYPE_PROG are required")
endif()

file(MAKE_DIRECTORY ${OUTDIR})

# ---- build gen1 (the C host compiles the self-hosted compiler) ----
list(GET SELFHOST_SOURCES 0 _first)
list(REMOVE_AT SELFHOST_SOURCES 0)
execute_process(
  COMMAND ${ZANC} ${_first} ${SELFHOST_SOURCES} --auto-stdlib -o ${GEN1}
  RESULT_VARIABLE build_rc
  OUTPUT_VARIABLE build_out
  ERROR_VARIABLE build_err)
if(NOT build_rc EQUAL 0)
  message(FATAL_ERROR "gen1 build failed (rc=${build_rc})\n${build_out}${build_err}")
endif()

set(_failures 0)

# ---- 1. a program the native backend can lower ----
set(_okobj "${OUTDIR}/ngen_ok.obj")
file(REMOVE ${_okobj})
execute_process(
  COMMAND ${GEN1} ${_okobj} ${OK_PROG}
  RESULT_VARIABLE ok_rc
  OUTPUT_VARIABLE ok_out
  ERROR_VARIABLE ok_err)
if(NOT ok_rc EQUAL 0)
  math(EXPR _failures "${_failures} + 1")
  message(SEND_ERROR "FAIL ngen: the native backend refused a program it must lower (rc=${ok_rc}): ${ok_out}${ok_err}")
elseif(NOT EXISTS ${_okobj})
  math(EXPR _failures "${_failures} + 1")
  message(SEND_ERROR "FAIL ngen: the native backend wrote no object for a program it must lower")
else()
  file(SIZE ${_okobj} _oklen)
  if(_oklen LESS 64)
    math(EXPR _failures "${_failures} + 1")
    message(SEND_ERROR "FAIL ngen: the object is suspiciously small (${_oklen} bytes)")
  else()
    message(STATUS "ngen: lowered a program into a ${_oklen}-byte object")
  endif()
endif()

# ---- 2. a call the backend does not know: fail closed ----
set(_badobj "${OUTDIR}/ngen_bad_member.obj")
file(REMOVE ${_badobj})
execute_process(
  COMMAND ${GEN1} ${_badobj} ${BAD_MEMBER_PROG}
  RESULT_VARIABLE bad_rc
  OUTPUT_VARIABLE bad_out
  ERROR_VARIABLE bad_err)
if(bad_rc EQUAL 0)
  math(EXPR _failures "${_failures} + 1")
  message(SEND_ERROR "FAIL ngen: the native backend accepted a call it cannot lower")
elseif(EXISTS ${_badobj})
  math(EXPR _failures "${_failures} + 1")
  message(SEND_ERROR "FAIL ngen: the native backend wrote an object for a program it must refuse")
else()
  message(STATUS "ngen: refused an unknown member (fail closed)")
endif()

# ---- 3. a type error: the type check comes first ----
set(_badtyobj "${OUTDIR}/ngen_bad_type.obj")
file(REMOVE ${_badtyobj})
execute_process(
  COMMAND ${GEN1} ${_badtyobj} ${BAD_TYPE_PROG}
  RESULT_VARIABLE badty_rc
  OUTPUT_VARIABLE badty_out
  ERROR_VARIABLE badty_err)
if(badty_rc EQUAL 0)
  math(EXPR _failures "${_failures} + 1")
  message(SEND_ERROR "FAIL ngen: the native backend accepted a program with an undefined type")
elseif(EXISTS ${_badtyobj})
  math(EXPR _failures "${_failures} + 1")
  message(SEND_ERROR "FAIL ngen: the native backend wrote an object for a program with a type error")
else()
  string(FIND "${badty_out}${badty_err}" "undefined type" _tyhit)
  if(_tyhit EQUAL -1)
    math(EXPR _failures "${_failures} + 1")
    message(SEND_ERROR "FAIL ngen: the type error was not reported as 'undefined type'")
  else()
    message(STATUS "ngen: refused a program with an undefined type")
  endif()
endif()

message(STATUS "selfhost ngen smoke: ${_failures} failure(s)")
if(_failures GREATER 0)
  message(FATAL_ERROR "selfhost ngen smoke gate: ${_failures} failure(s)")
endif()
