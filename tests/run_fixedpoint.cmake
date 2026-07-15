# Self-hosting fixed-point gate: reproduce the three-generation closure and
# assert gen2 == gen3 byte-for-byte. This is the strongest self-hosting check --
# it proves the self-hosted backend is a fixed point of itself, not merely that
# gen1 emits plausible IR.
#
#   gen0 ($ZANC, the C host) compiles src/selfhost/*.zan -> gen1
#   gen1 self-compiles the same sources        -> g2.ll
#   clang links g2.ll                           -> gen2
#   gen2 self-compiles the same sources         -> g3.ll
#   assert g2.ll and g3.ll are byte-identical
#
# clang is required (it links the emitted IR), so CMakeLists only registers this
# test when clang is found. STACK_ARGS carries the platform link flags the host
# bakes into its own link step (a large stack reservation on Windows).
#
# Invoked as:
#   cmake -DZANC=<zanc> -DSELFHOST_DIR=<src/selfhost> -DCLANG=<clang> \
#         -DGEN1=<exe> -DGEN2=<exe> -DG2LL=<g2.ll> -DG3LL=<g3.ll> \
#         [-DSTACK_ARGS=<;-separated link args>] -P run_fixedpoint.cmake

if(NOT ZANC OR NOT SELFHOST_DIR OR NOT CLANG OR NOT GEN1 OR NOT GEN2 OR NOT G2LL OR NOT G3LL)
  message(FATAL_ERROR "run_fixedpoint.cmake: ZANC, SELFHOST_DIR, CLANG, GEN1, GEN2, G2LL, G3LL are required")
endif()

# The driver (main.zan) must come first so its Program.Main is the entry point.
set(_srcs
  ${SELFHOST_DIR}/main.zan
  ${SELFHOST_DIR}/irgen.zan
  ${SELFHOST_DIR}/checker.zan
  ${SELFHOST_DIR}/binder.zan
  ${SELFHOST_DIR}/diag.zan
  ${SELFHOST_DIR}/parser.zan
  ${SELFHOST_DIR}/lexer.zan
  ${SELFHOST_DIR}/ast.zan
  ${SELFHOST_DIR}/token.zan)

# ---- gen0 -> gen1 ----
list(GET _srcs 0 _first)
set(_rest ${_srcs})
list(REMOVE_AT _rest 0)
execute_process(
  COMMAND ${ZANC} ${_first} ${_rest} -o ${GEN1}
  RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "gen0 -> gen1 failed (rc=${rc})\n${out}${err}")
endif()

# ---- gen1 -> g2.ll ----
file(REMOVE ${G2LL})
execute_process(
  COMMAND ${GEN1} ${G2LL} ${_srcs}
  RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "gen1 -> g2.ll failed (rc=${rc})\n${out}${err}")
endif()
if(NOT EXISTS ${G2LL})
  message(FATAL_ERROR "gen1 produced no g2.ll")
endif()

# ---- clang g2.ll -> gen2 ----
file(REMOVE ${GEN2})
execute_process(
  COMMAND ${CLANG} ${G2LL} -o ${GEN2} ${STACK_ARGS}
  RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "clang g2.ll -> gen2 failed (rc=${rc})\n${out}${err}")
endif()

# ---- gen2 -> g3.ll ----
file(REMOVE ${G3LL})
execute_process(
  COMMAND ${GEN2} ${G3LL} ${_srcs}
  RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "gen2 -> g3.ll failed (rc=${rc})\n${out}${err}")
endif()
if(NOT EXISTS ${G3LL})
  message(FATAL_ERROR "gen2 produced no g3.ll")
endif()

# ---- compare ----
file(READ ${G2LL} _g2 HEX)
file(READ ${G3LL} _g3 HEX)
if(NOT _g2 STREQUAL _g3)
  string(LENGTH "${_g2}" _l2)
  string(LENGTH "${_g3}" _l3)
  math(EXPR _b2 "${_l2} / 2")
  math(EXPR _b3 "${_l3} / 2")
  message(FATAL_ERROR
    "FIXED-POINT BROKEN: g2.ll (${_b2} bytes) != g3.ll (${_b3} bytes)")
endif()
string(LENGTH "${_g2}" _l2)
math(EXPR _b2 "${_l2} / 2")
message(STATUS "OK: self-hosting fixed point holds -- g2.ll == g3.ll (${_b2} bytes)")
