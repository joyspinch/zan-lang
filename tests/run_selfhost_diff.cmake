# Differential gate: the self-hosted compiler must agree with the C host.
#
# The fixed-point gate proves gen2 == g3 -- the self-hosted compiler agrees
# with *itself*. It says nothing about whether gen1 compiles a program the
# same way the C host (gen0) does. This gate closes that hole: for every
# case, the program is compiled by gen1, linked with the same runtime the
# host links, run, and its stdout is compared with the golden file the C
# host produced (tests/conformance/<case>.out).
#
# A case the C host compiles and runs must also compile and run under gen1
# with the same output. Cases in the known-gaps file are reported but do
# not fail the gate -- they are the recorded self-host gaps, and a gap that
# starts passing is reported as stale so the list gets pruned.
#
# A "reject" case is the mirror image: a program the self-hosted compiler
# must refuse (non-zero exit, no IR written). It guards a fail-closed
# boundary -- the C host may accept the same program, but the self-host must
# report a diagnostic instead of emitting IR it cannot lower.
#
# Invoked as:
#   cmake -DZANC=<zanc> -DSELFHOST_DIR=<src/selfhost> -DSELFHOST_SOURCES=<list> \
#         -DGEN1=<exe> -DOUTDIR=<dir> -DCLANG=<clang> -DRTOBJS=<objs> \
#         -DSYSLIBS=<libs> -DCASES=<cases.txt> -DGAPS=<gaps.txt> \
#         [-DREJECTS=<reject_cases.txt> -DREJECT_DIR=<dir>] \
#         [-DWORKDIR=<dir>] [-DJOBS=<n>] -P run_selfhost_diff.cmake
#
# The per-case loop (emit -> link -> run -> compare) runs on a Python thread
# pool (scripts/run_diff_parallel.py) -- 230 independent clang links were the
# gate's whole wall-clock cost, and CMake script mode has no job control. The
# summary line and the exit boundary are unchanged.

cmake_policy(SET CMP0012 NEW)

if(NOT ZANC OR NOT SELFHOST_DIR OR NOT GEN1 OR NOT OUTDIR OR NOT CASES)
  message(FATAL_ERROR "run_selfhost_diff.cmake: ZANC, SELFHOST_DIR, GEN1, OUTDIR and CASES are required")
endif()

# A -D value that looks like a *relative* path loses its extension on some
# CMake command lines (the ctest registration always passes absolute paths).
# Fail with a clear message instead of the cryptic file(STRINGS) error.
if(NOT EXISTS ${CASES})
  message(FATAL_ERROR "run_selfhost_diff.cmake: the case list '${CASES}' does not exist (pass an absolute path)")
endif()

if(SELFHOST_SOURCES)
  set(_srcs ${SELFHOST_SOURCES})
else()
  message(FATAL_ERROR "run_selfhost_diff.cmake: SELFHOST_SOURCES is required (scripts/selfhost_sources.txt)")
endif()

# The program runs from the repo root so a case can read repo-relative files.
if(NOT WORKDIR)
  get_filename_component(_script_dir "${CMAKE_SCRIPT_MODE_FILE}" DIRECTORY)
  get_filename_component(WORKDIR "${_script_dir}/.." ABSOLUTE)
endif()

file(MAKE_DIRECTORY ${OUTDIR})

# ---- known gaps ------------------------------------------------------------
# The driver (scripts/run_diff_parallel.py) parses the gap/reject lists from
# the same files; cmake only fail-fasts on a missing path.
if(GAPS AND NOT EXISTS ${GAPS})
  message(FATAL_ERROR "run_selfhost_diff.cmake: the known-gaps file '${GAPS}' does not exist (pass an absolute path)")
endif()

# ---- build gen1 once -------------------------------------------------------
list(GET _srcs 0 _first)
list(REMOVE_AT _srcs 0)
execute_process(
  COMMAND ${ZANC} ${_first} ${_srcs} --auto-stdlib -o ${GEN1}
  RESULT_VARIABLE build_rc
  OUTPUT_VARIABLE build_out
  ERROR_VARIABLE build_err)
if(NOT build_rc EQUAL 0)
  message(FATAL_ERROR "gen1 build failed (rc=${build_rc})\n${build_out}${build_err}")
endif()

# ---- hand the per-case loop to the parallel driver --------------------------
# CMake keeps the front half (gen1 build) and owns the exit boundary: a
# non-zero driver exit is fatal, mirroring the sequential SEND_ERROR ->
# FATAL_ERROR flow. A "reject" case (a program the self-host must refuse,
# fail-closed, optionally with the diagnostic pinned on both compilers) is
# parsed and checked by the driver.
if(REJECTS AND NOT EXISTS ${REJECTS})
  message(FATAL_ERROR "run_selfhost_diff.cmake: the reject list '${REJECTS}' does not exist (pass an absolute path)")
endif()

find_program(PYTHON_EXE python)
if(NOT PYTHON_EXE)
  message(FATAL_ERROR "run_selfhost_diff.cmake: python is required for the parallel differential loop (scripts/run_diff_parallel.py)")
endif()

get_filename_component(_script_dir "${CMAKE_SCRIPT_MODE_FILE}" DIRECTORY)
set(_driver "${_script_dir}/../scripts/run_diff_parallel.py")
if(NOT EXISTS "${_driver}")
  message(FATAL_ERROR "run_selfhost_diff.cmake: the parallel driver '${_driver}' is missing")
endif()

set(_drv_args
  --zanc "${ZANC}" --gen1 "${GEN1}" --outdir "${OUTDIR}"
  --case-dir "${CASE_DIR}" --cases "${CASES}" --workdir "${WORKDIR}")
if(CLANG)
  list(APPEND _drv_args --clang "${CLANG}")
endif()
if(RTOBJS)
  list(APPEND _drv_args --rtobjs ${RTOBJS})
endif()
# SYSLIBS is not threaded through: the driver derives the same per-OS list
# itself (the cmake->python argv boundary mangles ';'-separated values).
if(GAPS)
  list(APPEND _drv_args --gaps "${GAPS}")
endif()
if(REJECTS)
  list(APPEND _drv_args --rejects "${REJECTS}" --reject-dir "${REJECT_DIR}")
endif()
if(JOBS)
  list(APPEND _drv_args --jobs "${JOBS}")
endif()

execute_process(
  COMMAND ${PYTHON_EXE} ${_driver} ${_drv_args}
  WORKING_DIRECTORY ${WORKDIR}
  RESULT_VARIABLE _drv_rc)
if(NOT _drv_rc EQUAL 0)
  message(FATAL_ERROR "selfhost differential gate: the parallel driver exited with ${_drv_rc} (see the failures above)")
endif()
