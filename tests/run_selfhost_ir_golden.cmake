# Raw function fingerprint gate: scripts/normalize_ir.py --fnhash hashes
# each original function signature and body, including names and whitespace.
# It does not call normalize(); pure SSA/label renames also cause differences.
# Module-level globals, types, declarations and attributes are not covered.
# Fingerprints detect textual emission changes even when stdout matches;
# they do not prove semantic equivalence or complete module equivalence.
#
# Cross-compiler IR diffing (gen0 against gen1) was evaluated first (B-ID185)
# and is not tractable: their pull strategies and lowering models differ.
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

# Check the complete acceptance surface before starting an expensive gen1 build.
# Explicit BLESS may create goldens, but it still requires every case source.
file(STRINGS ${CASES} _case_lines)
set(_cases)
set(_missing_sources)
set(_missing_goldens)
foreach(_c IN LISTS _case_lines)
  string(STRIP "${_c}" _c)
  if(_c STREQUAL "" OR _c MATCHES "^#")
    continue()
  endif()
  list(APPEND _cases "${_c}")
  if(NOT EXISTS "${CASE_DIR}/${_c}.zan")
    list(APPEND _missing_sources "${_c}")
  endif()
  if(NOT BLESS AND NOT EXISTS "${GOLDEN_DIR}/${_c}.hashes")
    list(APPEND _missing_goldens "${_c}")
  endif()
endforeach()
if(_missing_sources OR _missing_goldens)
  set(_incomplete "IR golden preflight failed:")
  if(_missing_sources)
    list(JOIN _missing_sources ", " _names)
    string(APPEND _incomplete "\nMissing case sources: ${_names}")
  endif()
  if(_missing_goldens)
    list(JOIN _missing_goldens ", " _names)
    string(APPEND _incomplete "\nMissing golden files: ${_names}")
  endif()
  message(FATAL_ERROR "${_incomplete}")
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

set(_runner "${WORKDIR}/scripts/run_ir_golden_parallel.py")
if(NOT EXISTS "${_runner}")
  get_filename_component(_repo_dir "${CMAKE_SCRIPT_MODE_FILE}" DIRECTORY)
  get_filename_component(_repo_dir "${_repo_dir}/.." ABSOLUTE)
  set(_runner "${_repo_dir}/scripts/run_ir_golden_parallel.py")
endif()

set(_runner_args
  -I -B "${_runner}"
  --gen1 "${GEN1}"
  --outdir "${OUTDIR}"
  --case-dir "${CASE_DIR}"
  --cases "${CASES}"
  --golden-dir "${GOLDEN_DIR}"
  --norm "${NORM}"
  --workdir "${WORKDIR}")
if(JOBS)
  list(APPEND _runner_args --jobs "${JOBS}")
endif()
if(REPORT_JSON)
  list(APPEND _runner_args --report-json "${REPORT_JSON}")
endif()
if(BLESS)
  list(APPEND _runner_args --bless --prune-orphans)
endif()

execute_process(
  COMMAND ${PYTHON} ${_runner_args}
  WORKING_DIRECTORY "${WORKDIR}"
  RESULT_VARIABLE _gate_rc)
if(NOT _gate_rc EQUAL 0)
  message(FATAL_ERROR "selfhost ir golden: gate failed (rc=${_gate_rc})")
endif()
