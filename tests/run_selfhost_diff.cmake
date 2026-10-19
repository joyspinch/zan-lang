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
# Invoked as:
#   cmake -DZANC=<zanc> -DSELFHOST_DIR=<src/selfhost> -DSELFHOST_SOURCES=<list> \
#         -DGEN1=<exe> -DOUTDIR=<dir> -DCLANG=<clang> -DRTOBJS=<objs> \
#         -DSYSLIBS=<libs> -DCASES=<cases.txt> -DGAPS=<gaps.txt> \
#         [-DWORKDIR=<dir>] -P run_selfhost_diff.cmake

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
set(_gaps)
if(GAPS)
  if(NOT EXISTS ${GAPS})
    message(FATAL_ERROR "run_selfhost_diff.cmake: the known-gaps file '${GAPS}' does not exist (pass an absolute path)")
  endif()
  file(STRINGS ${GAPS} _gap_lines)
  foreach(_g IN LISTS _gap_lines)
    string(STRIP "${_g}" _g)
    if(_g STREQUAL "" OR _g MATCHES "^#")
      continue()
    endif()
    # "case -- reason": the reason is documentation, the name is the key
    string(FIND "${_g}" " --" _sep)
    if(NOT _sep EQUAL -1)
      string(SUBSTRING "${_g}" 0 ${_sep} _g)
      string(STRIP "${_g}" _g)
    endif()
    list(APPEND _gaps "${_g}")
  endforeach()
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

# ---- the case list ---------------------------------------------------------
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
set(_gap_hits 0)
set(_passed 0)

foreach(_case IN LISTS _cases)
  set(_src "${CASE_DIR}/${_case}.zan")
  set(_golden "${CASE_DIR}/${_case}.out")
  if(NOT EXISTS ${_src})
    message(FATAL_ERROR "case source not found: ${_src}")
  endif()
  set(_ll "${OUTDIR}/${_case}.ll")
  set(_exe "${OUTDIR}/${_case}.exe")
  file(REMOVE ${_ll} ${_exe})

  set(_why "")
  execute_process(
    COMMAND ${GEN1} ${_ll} ${_src}
    WORKING_DIRECTORY ${WORKDIR}
    RESULT_VARIABLE gen_rc
    OUTPUT_VARIABLE gen_out
    ERROR_VARIABLE gen_err)
  if(NOT gen_rc EQUAL 0)
    set(_why "gen1 failed to compile it (rc=${gen_rc}): ${gen_out}")
  else()
    if(CLANG AND RTOBJS)
      execute_process(
        COMMAND ${CLANG} ${_ll} ${RTOBJS} ${SYSLIBS} -o ${_exe}
        RESULT_VARIABLE cc_rc
        OUTPUT_VARIABLE cc_out
        ERROR_VARIABLE cc_err)
      if(NOT cc_rc EQUAL 0)
        set(_why "clang rejected the emitted IR (rc=${cc_rc}): ${cc_out}${cc_err}")
      endif()
    endif()
  endif()

  if(_why STREQUAL "")
    execute_process(
      COMMAND ${_exe}
      WORKING_DIRECTORY ${WORKDIR}
      RESULT_VARIABLE run_rc
      OUTPUT_VARIABLE run_out
      ERROR_VARIABLE run_err)
    if(NOT run_rc EQUAL 0)
      set(_why "the program exited with ${run_rc}: ${run_out}${run_err}")
    elseif(EXISTS ${_golden})
      file(READ ${_golden} _exp)
      string(REPLACE "\r\n" "\n" _exp "${_exp}")
      string(REPLACE "\r\n" "\n" run_out "${run_out}")
      string(REGEX REPLACE "[ \t\r\n]+$" "" _exp "${_exp}")
      string(REGEX REPLACE "[ \t\r\n]+$" "" run_out "${run_out}")
      if(NOT run_out STREQUAL _exp)
        set(_why "stdout differs from the C host's golden output\n--- expected ---\n${_exp}\n--- actual ---\n${run_out}")
      endif()
    endif()
  endif()

  if(_why STREQUAL "")
    math(EXPR _passed "${_passed} + 1")
    message(STATUS "PASS ${_case}")
    if("${_case}" IN_LIST _gaps)
      # the gap is closed but the list still names it: report it so the list
      # gets pruned (a warning, not a failure -- the behaviour is correct now)
      message(WARNING "known gap '${_case}' now passes: delete it from ${GAPS}")
    endif()
  elseif("${_case}" IN_LIST _gaps)
    math(EXPR _gap_hits "${_gap_hits} + 1")
    message(STATUS "GAP  ${_case}: ${_why}")
  else()
    math(EXPR _failures "${_failures} + 1")
    message(SEND_ERROR "FAIL ${_case}: ${_why}")
  endif()
endforeach()

message(STATUS "selfhost differential: ${_passed} passed, ${_gap_hits} known gaps, ${_failures} failures")
if(_failures GREATER 0)
  message(FATAL_ERROR "selfhost differential gate: ${_failures} case(s) disagree with the C host")
endif()
