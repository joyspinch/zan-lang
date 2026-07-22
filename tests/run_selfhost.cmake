# Self-hosting harness: build the self-hosted compiler with the C host (gen1),
# then exercise it end-to-end.
#
# Stage 1: gen0 ($ZANC) compiles all of src/selfhost/*.zan into gen1.
# Stage 2: gen1 compiles a real program ($PROG) to LLVM IR ($OUTLL); assert it
#          succeeds and the IR has an entry point.
# Stage 3: (when $CLANG is set) clang compiles $OUTLL to a native exe, runs it,
#          and diffs stdout against $EXPECTED -- proving the IR is valid and the
#          program is semantically correct, not merely that it "contains @main".
# Stage 4: (when $BADPROG is set) gen1 must reject a malformed program with a
#          non-zero exit code and must NOT write an .ll -- a regression guard
#          for the fail-closed diagnostics gate and parser recovery.
#
# Invoked as:
#   cmake -DZANC=<zanc> -DSELFHOST_DIR=<src/selfhost> -DPROG=<prog.zan> \
#         -DGEN1=<exe path> -DOUTLL=<out.ll> \
#         [-DCLANG=<clang>] [-DEXPECTED=<prog.out>] \
#         [-DBADPROG=<bad.zan>] [-DBADLL=<bad.ll>] -P run_selfhost.cmake

if(NOT ZANC OR NOT SELFHOST_DIR OR NOT PROG OR NOT GEN1 OR NOT OUTLL)
  message(FATAL_ERROR "run_selfhost.cmake: ZANC, SELFHOST_DIR, PROG, GEN1, OUTLL are required")
endif()

# The driver (main.zan) must come first so its Program.Main is the entry point.
set(_srcs
  ${SELFHOST_DIR}/main.zan
  ${SELFHOST_DIR}/irgen.zan
  ${SELFHOST_DIR}/checker.zan
  ${SELFHOST_DIR}/binder.zan
  ${SELFHOST_DIR}/diag.zan
  ${SELFHOST_DIR}/parser.zan
  ${SELFHOST_DIR}/jsongen.zan
  ${SELFHOST_DIR}/dbgen.zan
  ${SELFHOST_DIR}/lexer.zan
  ${SELFHOST_DIR}/ast.zan
  ${SELFHOST_DIR}/token.zan)

# ---- Stage 1: build gen1 (the self-hosted compiler) with the C host ----
list(GET _srcs 0 _first)
list(REMOVE_AT _srcs 0)
execute_process(
  COMMAND ${ZANC} ${_first} ${_srcs} -o ${GEN1}
  RESULT_VARIABLE build_rc
  OUTPUT_VARIABLE build_out
  ERROR_VARIABLE  build_err)
if(NOT build_rc EQUAL 0)
  message(FATAL_ERROR "gen1 build failed (rc=${build_rc})\n${build_out}${build_err}")
endif()

# ---- Stage 2: run gen1 to emit IR for a real program ----
file(REMOVE ${OUTLL})
execute_process(
  COMMAND ${GEN1} ${OUTLL} ${PROG}
  RESULT_VARIABLE gen_rc
  OUTPUT_VARIABLE gen_out
  ERROR_VARIABLE  gen_err)
if(NOT gen_rc EQUAL 0)
  message(FATAL_ERROR "gen1 failed to compile ${PROG} (rc=${gen_rc})\n${gen_out}${gen_err}")
endif()
if(NOT EXISTS ${OUTLL})
  message(FATAL_ERROR "gen1 produced no IR at ${OUTLL}")
endif()
file(READ ${OUTLL} _ir)
string(LENGTH "${_ir}" _irlen)
if(_irlen LESS 200)
  message(FATAL_ERROR "gen1 IR is suspiciously small (${_irlen} bytes)")
endif()
if(NOT _ir MATCHES "define i32 @main")
  message(FATAL_ERROR "gen1 IR is missing an entry point (define i32 @main)")
endif()

# ---- Stage 3: clang-validate the IR, run it, diff stdout ----
if(CLANG AND EXPECTED)
  get_filename_component(_exedir ${GEN1} DIRECTORY)
  get_filename_component(_ext ${GEN1} EXT)
  set(_prog_exe ${_exedir}/selfhost_prog${_ext})
  file(REMOVE ${_prog_exe})
  execute_process(
    COMMAND ${CLANG} ${OUTLL} -o ${_prog_exe}
    RESULT_VARIABLE cc_rc
    OUTPUT_VARIABLE cc_out
    ERROR_VARIABLE  cc_err)
  if(NOT cc_rc EQUAL 0)
    message(FATAL_ERROR "clang rejected gen1 IR for ${PROG} (rc=${cc_rc})\n${cc_out}${cc_err}")
  endif()
  execute_process(
    COMMAND ${_prog_exe}
    RESULT_VARIABLE run_rc
    OUTPUT_VARIABLE run_out
    ENCODING UTF-8)
  if(NOT run_rc EQUAL 0)
    message(FATAL_ERROR "gen1-compiled ${PROG} exited with ${run_rc}\noutput:\n${run_out}")
  endif()
  file(READ ${EXPECTED} _exp)
  string(REPLACE "\r\n" "\n" _exp "${_exp}")
  string(REPLACE "\r\n" "\n" run_out "${run_out}")
  string(REGEX REPLACE "[ \t\r\n]+$" "" _exp "${_exp}")
  string(REGEX REPLACE "[ \t\r\n]+$" "" run_out "${run_out}")
  if(NOT run_out STREQUAL _exp)
    message(FATAL_ERROR
      "gen1-compiled ${PROG} stdout mismatch\n--- expected ---\n${_exp}\n--- actual ---\n${run_out}")
  endif()
  message(STATUS "OK: gen1 IR clang-compiled, ran, and stdout matched for ${PROG}")
else()
  message(STATUS "SKIP: clang stdout stage (CLANG or EXPECTED not provided)")
endif()

# ---- Stage 4: malformed input must fail closed (non-zero exit, no IR) ----
if(BADPROG)
  if(NOT BADLL)
    set(BADLL ${OUTLL}.bad)
  endif()
  file(REMOVE ${BADLL})
  execute_process(
    COMMAND ${GEN1} ${BADLL} ${BADPROG}
    RESULT_VARIABLE bad_rc
    OUTPUT_VARIABLE bad_out
    ERROR_VARIABLE  bad_err)
  if(bad_rc EQUAL 0)
    message(FATAL_ERROR "gen1 accepted malformed ${BADPROG} (rc=0); expected non-zero")
  endif()
  if(EXISTS ${BADLL})
    message(FATAL_ERROR "gen1 wrote IR ${BADLL} for malformed ${BADPROG}; must not emit on error")
  endif()
  message(STATUS "OK: gen1 rejected malformed ${BADPROG} (rc=${bad_rc}, no IR emitted)")
endif()

message(STATUS "OK: self-hosted compiler built and emitted IR for ${PROG} (${_irlen} bytes)")
