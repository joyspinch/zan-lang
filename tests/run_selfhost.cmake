# Self-hosting smoke test.
#
# Stage 1: use the C host compiler (gen0, $ZANC) to compile the entire
#          self-hosted Zan compiler (all of src/selfhost/*.zan) into a native
#          executable -- this is "gen1".
# Stage 2: run gen1 on a real Zan program ($PROG), asking it to emit LLVM IR
#          text to $OUTLL, and assert it succeeds and produces IR that contains
#          the expected top-level definitions.
#
# This proves the host can build the whole self-hosted compiler and that gen1
# lexes/parses/binds/checks/lowers a non-trivial program end-to-end. The full
# byte-identical gen2==gen3 closure is reproduced by scripts/bootstrap.* and
# documented in docs/BOOTSTRAP.md (it additionally needs clang to turn the
# emitted .ll into a native exe, which is not assumed present in CI).
#
# Invoked as:
#   cmake -DZANC=<zanc> -DSELFHOST_DIR=<src/selfhost> -DPROG=<prog.zan> \
#         -DGEN1=<exe path> -DOUTLL=<out.ll> -P run_selfhost.cmake

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

message(STATUS "OK: self-hosted compiler built and emitted IR for ${PROG} (${_irlen} bytes)")
