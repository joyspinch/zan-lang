# Portable compile-run-compare harness for a single Zan program.
#
# Invoked as:
#   cmake -DZANC=<zanc> -DSRC=<file.zan> -DEXPECTED=<file.out> \
#         -DOUT_EXE=<exe path> [-DZANC_ARGS=<extra;args>] [-DRUN_ARGS=<args>] \
#         [-DWORKDIR=<dir>] \
#         -P run_case.cmake
#
# Fails (non-zero) if compilation fails or the program's stdout does not
# match the golden file (line-ending normalized). When WORKDIR is given the
# compiled program is run with that working directory (so it can resolve
# relative paths, e.g. a self-hosting compiler reading a source file).

# Script mode carries no project policy version; without this, while(TRUE)
# treats TRUE as an (unset) variable and the run loop never executes.
cmake_policy(SET CMP0012 NEW)

if(NOT ZANC OR NOT SRC OR NOT EXPECTED OR NOT OUT_EXE)
  message(FATAL_ERROR "run_case.cmake: ZANC, SRC, EXPECTED and OUT_EXE are required")
endif()

# The compiled program runs from the repo root by default (this script lives in
# tests/), so a case can read repo-relative fixtures no matter where ctest was
# launched from — the generic add_test registrations pass no WORKDIR of their
# own, and ctest's build-dir default breaks any case that opens "tests/..."
# or "stdlib/..." relative paths.
if(NOT WORKDIR AND CMAKE_SCRIPT_MODE_FILE)
  get_filename_component(_script_dir "${CMAKE_SCRIPT_MODE_FILE}" DIRECTORY)
  get_filename_component(WORKDIR "${_script_dir}/.." ABSOLUTE)
endif()


# ---- leak gate (S10, docs/ZAN_STANDARD.md §3) ------------------------------
# Every case runs instrumented (--check-leaks) unless it compiles with
# --publish / --no-check-leaks or passes -DLEAK_GATE=OFF. The leak report the
# runtime prints at exit is cut off stdout before the golden comparison and
# judged on its own: a case must exit with zero reachable objects, unless its
# executable stem is listed in tests/leak_baseline.txt -- the debt recorded
# when the gate went live. The list only shrinks: a listed case that stops
# leaking turns red until its line is removed (same ratchet as EXPECT_LEAK in
# run_case_modes.cmake).
set(_leak_gate ON)
if(DEFINED LEAK_GATE AND NOT LEAK_GATE)
  set(_leak_gate OFF)
endif()
foreach(_arg IN LISTS ZANC_ARGS)
  if(_arg STREQUAL "--publish" OR _arg STREQUAL "--no-check-leaks")
    set(_leak_gate OFF)
  endif()
endforeach()
if(_leak_gate AND NOT "--check-leaks" IN_LIST ZANC_ARGS)
  list(APPEND ZANC_ARGS --check-leaks)
endif()
get_filename_component(_case_stem "${OUT_EXE}" NAME_WE)

# ---- up-to-date check ------------------------------------------------------
# Re-running the suite must not recompile programs whose inputs did not change:
# the artifact is a pure function of the source, compiler, stdlib/packages and any
# explicit Zan source arguments, so a target newer than all of them is reused.
# STDLIB_STAMP is touched by the build whenever a stdlib or package source changes.
function(zan_artifact_is_current out_var artifact)
  set(${out_var} FALSE PARENT_SCOPE)
  if(NOT EXISTS ${artifact})
    return()
  endif()
  if(${SRC} IS_NEWER_THAN ${artifact})
    return()
  endif()
  if(${ZANC} IS_NEWER_THAN ${artifact})
    return()
  endif()
  # The compile flags are part of the artifact's identity (the leak gate adds
  # --check-leaks to images built before it existed).
  if(NOT EXISTS "${artifact}.args")
    return()
  endif()
  file(READ "${artifact}.args" _prev_args)
  if(NOT _prev_args STREQUAL "${ZANC_ARGS}")
    return()
  endif()
  if(STDLIB_STAMP AND EXISTS ${STDLIB_STAMP} AND ${STDLIB_STAMP} IS_NEWER_THAN ${artifact})
    return()
  endif()
  foreach(_arg IN LISTS ZANC_ARGS)
    if(_arg MATCHES "\\.(zan|html|zcomp)$" AND EXISTS "${_arg}"
       AND "${_arg}" IS_NEWER_THAN "${artifact}")
      return()
    endif()
  endforeach()
  set(${out_var} TRUE PARENT_SCOPE)
endfunction()


# A reused artifact must never make a failure sticky: if the program crashed or
# its output did not match, drop the executable so the next run recompiles it
# from scratch (an image damaged by a transient build/AV race would otherwise be
# considered "current" forever).
function(zan_drop_artifact)
  file(REMOVE ${OUT_EXE})
endfunction()

# ---- compile ----
zan_artifact_is_current(_current ${OUT_EXE})
if(NOT _current)
  execute_process(
    COMMAND ${ZANC} ${SRC} -o ${OUT_EXE} ${ZANC_ARGS}
            --package-project ${WORKDIR}
    RESULT_VARIABLE compile_rc
    OUTPUT_VARIABLE compile_out
    ERROR_VARIABLE  compile_err)
  if(NOT compile_rc EQUAL 0)
    message(FATAL_ERROR "compile failed (rc=${compile_rc})\n${compile_out}${compile_err}")
  endif()
  file(WRITE "${OUT_EXE}.args" "${ZANC_ARGS}")
endif()

# The runtime appends its leak report to $ZAN_LEAK_LOG (default: zan_leaks.log
# in the working directory, i.e. the repo root). Keep it under build/test-out.
get_filename_component(_cases_dir "${OUT_EXE}" DIRECTORY)
get_filename_component(_build_dir "${_cases_dir}" DIRECTORY)
set(_leak_log "${_build_dir}/test-out/leaks/${_case_stem}.log")
file(REMOVE "${_leak_log}")
get_filename_component(_leak_log_dir "${_leak_log}" DIRECTORY)
file(MAKE_DIRECTORY "${_leak_log_dir}")
set(ENV{ZAN_LEAK_LOG} "${_leak_log}")

# ---- run ----
# On Windows a freshly linked executable can transiently fail to launch with
# STATUS_SHARING_VIOLATION (0xC0000043) or STATUS_ACCESS_DENIED (0xC0000022)
# when antivirus/the loader briefly holds the new image open -- common under
# parallel ctest. These are image-load races, not program failures, so retry
# the launch a few times before giving up.
set(_run_attempt 0)
while(TRUE)
  math(EXPR _run_attempt "${_run_attempt} + 1")
  execute_process(
    COMMAND ${OUT_EXE} ${RUN_ARGS}
    WORKING_DIRECTORY ${WORKDIR}
    RESULT_VARIABLE run_rc
    OUTPUT_VARIABLE actual
    ENCODING UTF-8)
  if((run_rc MATCHES "[cC]0000043" OR run_rc MATCHES "[cC]0000022")
     AND _run_attempt LESS 6)
    execute_process(COMMAND ${CMAKE_COMMAND} -E sleep 0.4)
    continue()
  endif()
  break()
endwhile()
  # 运行期诊断类用例（fail-fast 开窗对账等）以非零码退出属预期：
  # -DEXPECT_RC=<n> 声明期望退出码（缺省 0），输出仍按 golden 对账。
  set(_expect_rc 0)
  if(EXPECT_RC)
    set(_expect_rc ${EXPECT_RC})
  endif()
  if(NOT run_rc EQUAL _expect_rc)
    zan_drop_artifact()
    message(FATAL_ERROR "program exited with ${run_rc}\noutput:\n${actual}")
  endif()

# ---- split the leak report off stdout ----
# It is printed by an atexit hook, so it is always the tail of stdout.
set(_leak_report "")
string(FIND "${actual}" "zan: memory leak detected:" _leak_pos)
if(_leak_pos GREATER_EQUAL 0)
  string(SUBSTRING "${actual}" ${_leak_pos} -1 _leak_report)
  string(SUBSTRING "${actual}" 0 ${_leak_pos} actual)
endif()

# ---- compare (normalize CRLF and trailing whitespace) ----
file(READ ${EXPECTED} expected)
string(REPLACE "\r\n" "\n" expected "${expected}")
string(REPLACE "\r\n" "\n" actual   "${actual}")
string(REGEX REPLACE "[ \t\r\n]+$" "" expected "${expected}")
string(REGEX REPLACE "[ \t\r\n]+$" "" actual   "${actual}")

# ---- skip convention -------------------------------------------------------
# A case that cannot run in this environment (no interactive clipboard on a
# headless agent session, no GPU, no network) prints a single line starting
# with "SKIP: " and exits 0. The harness reports it as skipped instead of
# failing: the environment lacks the resource, the code under test is not
# at fault. Such a case must print nothing else -- the marker is only
# honoured when it is the whole output.
if(actual MATCHES "^SKIP: ")
  message(STATUS "SKIPPED: ${SRC} (${actual})")
  return()
endif()

if(NOT actual STREQUAL expected)
  zan_drop_artifact()
  message(FATAL_ERROR
    "output mismatch for ${SRC}\n--- expected ---\n${expected}\n--- actual ---\n${actual}")
endif()

# ---- leak verdict ----
if(_leak_gate)
  set(_baseline_listed FALSE)
  set(_baseline "${CMAKE_CURRENT_LIST_DIR}/leak_baseline.txt")
  if(EXISTS "${_baseline}")
    file(STRINGS "${_baseline}" _baseline_lines)
    foreach(_line IN LISTS _baseline_lines)
      string(REGEX REPLACE "#.*$" "" _line "${_line}")
      string(STRIP "${_line}" _line)
      if(_line STREQUAL _case_stem)
        set(_baseline_listed TRUE)
      endif()
    endforeach()
  endif()
  if(_leak_report AND NOT _baseline_listed)
    zan_drop_artifact()
    message(FATAL_ERROR
      "leakcheck: ${SRC} leaves objects reachable at exit (S10 leak gate).\n"
      "Fix the leak (break the cycle with 'weak', unsubscribe, Dispose). The "
      "baseline tests/leak_baseline.txt only records debt from when the gate "
      "went live and must not grow.\n${_leak_report}")
  endif()
  if(NOT _leak_report AND _baseline_listed)
    message(FATAL_ERROR
      "leakcheck: ${SRC} no longer leaks -- remove '${_case_stem}' from "
      "tests/leak_baseline.txt (the baseline only shrinks).")
  endif()
  if(_leak_report)
    message(STATUS "KNOWN LEAK (tests/leak_baseline.txt): ${_case_stem}\n${_leak_report}")
  endif()
endif()

message(STATUS "OK: ${SRC}")
