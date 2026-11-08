# Golden-IR regression gate: gen1's emitted IR per case is pinned by a
# normalized golden file, so any commit that changes the emission path shows
# up as a diff -- including changes that keep the program's stdout identical
# (the stdout differential gate cannot see those; B-ID180's fptosi poison was
# exactly such a divergence).
#
# Why normalized goldens and not raw .ll: every gen1 refactor renames
# mnemonic SSA temps, labels and internal globals without changing semantics;
# scripts/normalize_ir.py erases exactly that noise (names become positions),
# so a pure rename is invisible and a real change to the instruction stream
# survives. The cross-compiler variant of this idea (diffing gen0's IR against
# gen1's) was evaluated first (B-ID185) and is not tractable: for the same
# program gen0 emits ~7.5k lines and gen1 ~84k (different pull strategy,
# runtime inlining, lowering model) -- a byte diff there is 100% noise.
#
# A mismatch is a FAILURE, not a report: the goldens are the recorded shape of
# the current emitter. Changing them deliberately (emitter improvement, twin
# fix) blesses them in the same commit:
#   cmake -D... -DBLESS=1 -P run_selfhost_ir_golden.cmake   # rewrite goldens
#
# Invoked as:
#   cmake -DZANC=<zanc> -DSELFHOST_SOURCES=<list> -DGEN1=<exe> -DOUTDIR=<dir> \
#         -DCASE_DIR=<dir> -DCASES=<cases.txt> -DGOLDEN_DIR=<dir> \
#         -DNORM=<normalize_ir.py> [-DBLESS=1] [-DWORKDIR=<dir>] -P <this>

cmake_policy(SET CMP0012 NEW)

if(NOT GEN1 OR NOT OUTDIR OR NOT CASES OR NOT GOLDEN_DIR OR NOT NORM)
  message(FATAL_ERROR "run_selfhost_ir_golden.cmake: GEN1, OUTDIR, CASES, GOLDEN_DIR and NORM are required")
endif()
if(NOT SKIP_GEN1_BUILD AND NOT ZANC)
  message(FATAL_ERROR "run_selfhost_ir_golden.cmake: ZANC is required unless SKIP_GEN1_BUILD=1")
endif()

if(NOT PYTHON)
  set(PYTHON python)
endif()

if(NOT EXISTS ${CASES})
  message(FATAL_ERROR "run_selfhost_ir_golden.cmake: the case list '${CASES}' does not exist (pass an absolute path)")
endif()

if(NOT WORKDIR)
  get_filename_component(_script_dir "${CMAKE_SCRIPT_MODE_FILE}" DIRECTORY)
  get_filename_component(WORKDIR "${_script_dir}/.." ABSOLUTE)
endif()

file(MAKE_DIRECTORY ${OUTDIR})

# ---- build gen1 once -------------------------------------------------------
# SKIP_GEN1_BUILD=1 reuses a caller-provided GEN1 (same escape hatch as
# run_selfhost_repo.cmake: validation against a gen1 from another tree).
# ctest never sets it -- the gate always rebuilds from the tree.
set(_srcs ${SELFHOST_SOURCES})
if(NOT _srcs AND NOT SKIP_GEN1_BUILD)
  message(FATAL_ERROR "run_selfhost_ir_golden.cmake: SELFHOST_SOURCES is required (scripts/selfhost_sources.txt)")
endif()
if(NOT SKIP_GEN1_BUILD)
  list(GET _srcs 0 _first)
  list(REMOVE_AT _srcs 0)
  execute_process(
    COMMAND ${ZANC} ${_first} ${_srcs} --auto-stdlib -O0 -o ${GEN1}
    RESULT_VARIABLE build_rc
    OUTPUT_VARIABLE build_out
    ERROR_VARIABLE build_err)
  if(NOT build_rc EQUAL 0)
    message(FATAL_ERROR "gen1 build failed (rc=${build_rc})\n${build_out}${build_err}")
  endif()
endif()

file(STRINGS ${CASES} _case_lines)
set(_cases)
foreach(_c IN LISTS _case_lines)
  string(STRIP "${_c}" _c)
  if(_c STREQUAL "" OR _c MATCHES "^#")
    continue()
  endif()
  list(APPEND _cases "${_c}")
endforeach()

set(_failures 0)
set(_blessed 0)
set(_passed 0)

foreach(_case IN LISTS _cases)
  set(_src "${CASE_DIR}/${_case}.zan")
  if(NOT EXISTS ${_src})
    math(EXPR _failures "${_failures} + 1")
    message(SEND_ERROR "FAIL ${_case}: case source not found under ${CASE_DIR}")
    continue()
  endif()
  set(_ll "${OUTDIR}/${_case}.ll")
  file(REMOVE ${_ll})
  execute_process(
    COMMAND ${GEN1} ${_ll} ${_src}
    WORKING_DIRECTORY ${WORKDIR}
    RESULT_VARIABLE gen_rc
    OUTPUT_VARIABLE gen_out
    ERROR_VARIABLE gen_err
    TIMEOUT 600)
  if(NOT gen_rc EQUAL 0 OR NOT EXISTS ${_ll})
    math(EXPR _failures "${_failures} + 1")
    message(SEND_ERROR "FAIL ${_case}: gen1 failed to compile it (rc=${gen_rc})")
    continue()
  endif()

  set(_golden "${GOLDEN_DIR}/${_case}.hashes")
  if(BLESS)
    execute_process(
      COMMAND ${PYTHON} ${NORM} --fnhash ${_ll} ${_golden}
      RESULT_VARIABLE norm_rc)
    if(NOT norm_rc EQUAL 0)
      math(EXPR _failures "${_failures} + 1")
      message(SEND_ERROR "FAIL ${_case}: normalizer errored (rc=${norm_rc})")
    else()
      math(EXPR _blessed "${_blessed} + 1")
      message(STATUS "BLESS ${_case}")
    endif()
    continue()
  endif()

  if(NOT EXISTS ${_golden})
    math(EXPR _failures "${_failures} + 1")
    message(SEND_ERROR "FAIL ${_case}: no golden at ${_golden} (bless it deliberately: rerun with -DBLESS=1 and commit it)")
    continue()
  endif()
  set(_got "${OUTDIR}/${_case}.hashes")
  execute_process(
    COMMAND ${PYTHON} ${NORM} --fnhash ${_ll} ${_got}
    RESULT_VARIABLE norm_rc)
  if(NOT norm_rc EQUAL 0)
    math(EXPR _failures "${_failures} + 1")
    message(SEND_ERROR "FAIL ${_case}: normalizer errored (rc=${norm_rc})")
    continue()
  endif()
  file(READ ${_golden} _exp)
  file(READ ${_got} _act)
  string(STRIP "${_exp}" _exp)
  string(STRIP "${_act}" _act)
  if(NOT _act STREQUAL _exp)
    math(EXPR _failures "${_failures} + 1")
    message(SEND_ERROR "FAIL ${_case}: emitted IR differs from the golden (per-function diff: python scripts/normalize_ir.py --fnhash <ll> then diff vs ${_golden})")
  else()
    math(EXPR _passed "${_passed} + 1")
    message(STATUS "PASS ${_case}")
  endif()
endforeach()

if(BLESS)
  message(STATUS "ir golden bless: ${_blessed} rewritten, ${_failures} errors")
else()
  message(STATUS "ir golden gate: ${_passed} passed, ${_failures} failures")
endif()
if(NOT _failures EQUAL 0)
  message(FATAL_ERROR "selfhost ir golden: ${_failures} case(s) failed")
endif()
