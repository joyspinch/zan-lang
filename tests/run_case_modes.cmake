# One case, one binary, several verdicts.
#
# This harness replaces four twins that all read the *same* program:
#   conformance  stdout matches the golden
#   leakcheck    the run reports no "memory leak detected"
#   arcguard     the run reports no "ARC integrity failure"
#   determinism  two codegen runs are byte-for-byte identical
#
# Why they are no longer four ctest entries: the twins compiled the same
# source three times (plain, --check-leaks, --arc-guard) and ran it three
# times, so one case cost three executables and three runs -- a full sweep
# left 3500+ throwaway exes and 4700 files behind, while each program itself
# runs in milliseconds. The waste was in the repeated compiles and the
# repeated files, not in the coverage.
#
# Two modes, one file each:
#
#   program mode     (EXPECTED + OUT_EXE)
#       One instrumented build (--check-leaks --arc-guard) and one run.
#       The instruments print nothing extra while the program is clean, so
#       the instrumented stdout is BOTH the golden comparison and the
#       leak / ARC verdict -- three verdicts, one compile, one run, one
#       file. (Verified byte-for-byte against the plain build.)
#
#       Two knobs cover the cases the instruments do change:
#       - PLAIN_OUT_EXE: the golden comes from an uninstrumented build. The
#         leak report goes to stdout, so a case that leaks (on purpose or by
#         a known stdlib debt) cannot match its golden with the instrumented
#         stdout.
#       - EXPECT_LEAK: a known leaker must still leak -- the allow-list the
#         old leakcheck tier carried as WILL_FAIL. Fixing the leak turns the
#         case red, which is the reminder to drop it from the list.
#       - EXPECT_LEAK_SITE: like EXPECT_LEAK, plus the report must name the
#         expected site ("file.zan:LINE:"), pinning per-site attribution.
#
#   determinism mode (DET_STAMP)
#       Codegen purity needs a *second* codegen to compare against, so it
#       stays its own entry: it produces no executable, only a small stamp
#       that remembers the verdict for an unchanged (source, compiler,
#       stdlib, args) tuple.
#
# One case needs a plain build as well: the leak report goes to stdout, so
# a case that leaks on purpose cannot compare the instrumented stdout with
# its golden. Such a case passes PLAIN_OUT_EXE (uninstrumented) and its
# golden is compared against that; the instrumented run is used only for
# the leak verdict.
#
# Usage:
#   program mode:
#     cmake -DZANC=<zanc> -DSRC=<file.zan> -DEXPECTED=<file.out> \
#           -DOUT_EXE=<exe> [-DZANC_ARGS=<args>] [-DRUN_ARGS=<args>] \
#           [-DPLAIN_OUT_EXE=<exe>] [-DPLAIN_ZANC_ARGS=<args>] \
#           [-DEXPECT_LEAK_SITE=<site>] [-DWORKDIR=<dir>] \
#           [-DSTDLIB_STAMP=<stamp>] -P run_case_modes.cmake
#   determinism mode:
#     cmake -DZANC=<zanc> -DSRC=<file.zan> -DDET_STAMP=<stamp> \
#           [-DZANC_ARGS=<args>] [-DSTDLIB_STAMP=<stamp>] \
#           -P run_case_modes.cmake

cmake_policy(SET CMP0012 NEW)

if(NOT ZANC OR NOT SRC)
  message(FATAL_ERROR "run_case_modes.cmake: ZANC and SRC are required")
endif()
if(NOT EXPECTED AND NOT DET_STAMP)
  message(FATAL_ERROR
    "run_case_modes.cmake: EXPECTED (program mode) or DET_STAMP (determinism mode) is required")
endif()

# The compiled program runs from the repo root by default (see run_case.cmake):
# the generic add_test registrations pass no WORKDIR of their own, and ctest's
# build-dir default breaks any case that opens repo-relative fixture paths.
if(NOT WORKDIR AND CMAKE_SCRIPT_MODE_FILE)
  get_filename_component(_script_dir "${CMAKE_SCRIPT_MODE_FILE}" DIRECTORY)
  get_filename_component(WORKDIR "${_script_dir}/.." ABSOLUTE)
endif()

# ---- up-to-date check ------------------------------------------------------
# The artifact is a pure function of (source, compiler, stdlib, args); a target
# newer than all of them is reused. STDLIB_STAMP is touched by the build
# whenever a stdlib or package source changes. The harness itself is part of
# the input: it owns the compiler flags, so a change to it invalidates the
# cached binary (a plain binary reused for an instrumented verdict would
# silently skip the leak and ARC checks).
set(_compiler ${ZANC})
if(MVC_TOOL)
  set(_compiler ${MVC_TOOL} ${ZANC} ${MVC_MODE})
endif()

# Plain conformance runs at -O0 (see CMakeLists.txt); cases needing optimization
# pass -O1 / --publish / -g explicitly. Defaulting heavy packages (Zan.Gui,
# Zan.Web, Zan.Mvc, Zan.Data) to -O0 avoids multi-minute LLVM O2 passes.
set(_default_opt_args)
if(NOT ";${ZANC_ARGS};" MATCHES ";(--publish|-O0|-O1|-O2|-O3|-Os|-Oz|-g);")
  set(_default_opt_args -O0)
endif()
set(_plain_default_opt_args)
if(NOT ";${PLAIN_ZANC_ARGS};" MATCHES ";(--publish|-O0|-O1|-O2|-O3|-Os|-Oz|-g);")
  set(_plain_default_opt_args -O0)
endif()

function(zan_artifact_is_current out_var artifact)
  set(${out_var} FALSE PARENT_SCOPE)
  if(NOT EXISTS ${artifact})
    return()
  endif()
  if(${SRC} IS_NEWER_THAN ${artifact})
    return()
  endif()
  if("${ZANC}" IS_NEWER_THAN "${artifact}")
    return()
  endif()
  if(MVC_TOOL AND "${MVC_TOOL}" IS_NEWER_THAN "${artifact}")
    return()
  endif()
  if(STDLIB_STAMP AND EXISTS ${STDLIB_STAMP} AND ${STDLIB_STAMP} IS_NEWER_THAN ${artifact})
    return()
  endif()
  if(CMAKE_SCRIPT_MODE_FILE AND ${CMAKE_SCRIPT_MODE_FILE} IS_NEWER_THAN ${artifact})
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

# A reused artifact must never make a failure sticky: drop the executable so
# the next run recompiles it from scratch (an image damaged by a transient
# build/AV race would otherwise be considered "current" forever).
function(zan_drop_artifact)
  file(REMOVE ${OUT_EXE})
  if(PLAIN_OUT_EXE)
    file(REMOVE ${PLAIN_OUT_EXE})
  endif()
endfunction()

# ---- run (shared by the instrumented and the plain build) ------------------
# On Windows a freshly linked executable can transiently fail to launch with
# STATUS_SHARING_VIOLATION (0xC0000043) or STATUS_ACCESS_DENIED (0xC0000022)
# when antivirus/the loader briefly holds the new image open -- common under
# parallel ctest. These are image-load races, not program failures, so retry
# the launch a few times before giving up.
function(run_program out_exe out_rc out_stdout out_stderr)
  set(_attempt 0)
  while(TRUE)
    math(EXPR _attempt "${_attempt} + 1")
    execute_process(
      COMMAND ${out_exe} ${RUN_ARGS}
      WORKING_DIRECTORY ${WORKDIR}
      RESULT_VARIABLE rc
      OUTPUT_VARIABLE stdout
      ERROR_VARIABLE  stderr
      ENCODING UTF-8)
    if((rc MATCHES "[cC]0000043" OR rc MATCHES "[cC]0000022")
       AND _attempt LESS 6)
      execute_process(COMMAND ${CMAKE_COMMAND} -E sleep 0.4)
      continue()
    endif()
    break()
  endwhile()
  set(${out_rc} "${rc}" PARENT_SCOPE)
  set(${out_stdout} "${stdout}" PARENT_SCOPE)
  set(${out_stderr} "${stderr}" PARENT_SCOPE)
endfunction()

# ============================================================================
# program mode: conformance + leak + ARC in one compile and one run
# ============================================================================
if(EXPECTED AND OUT_EXE)
  # The runtime appends its leak report to $ZAN_LEAK_LOG (default zan_leaks.log
  # in the working directory = repo root); keep it under build/test-out (S10).
  get_filename_component(_cases_dir "${OUT_EXE}" DIRECTORY)
  get_filename_component(_build_dir "${_cases_dir}" DIRECTORY)
  get_filename_component(_case_stem "${OUT_EXE}" NAME_WE)
  set(_leak_log "${_build_dir}/test-out/leaks/${_case_stem}.log")
  file(REMOVE "${_leak_log}")
  file(MAKE_DIRECTORY "${_build_dir}/test-out/leaks")
  set(ENV{ZAN_LEAK_LOG} "${_leak_log}")

  # ---- compile the instrumented build (leak check + ARC guard in one image) -
  # Both instruments answer different questions (a retain without a release vs.
  # a release without a retain) but they read the same program, so one image
  # answers both. Verified: with the guard on, the leak checker still reports
  # leaks and does not produce false positives on clean programs.
  zan_artifact_is_current(_current ${OUT_EXE})
  if(NOT _current)
    execute_process(
      COMMAND ${_compiler} ${SRC} -o ${OUT_EXE} --check-leaks --arc-guard ${ZANC_ARGS} ${_default_opt_args}
              --package-project ${WORKDIR}
      RESULT_VARIABLE compile_rc
      OUTPUT_VARIABLE compile_out
      ERROR_VARIABLE  compile_err)
    if(NOT compile_rc EQUAL 0)
      message(FATAL_ERROR "compile failed (rc=${compile_rc})\n${compile_out}${compile_err}")
    endif()
  endif()

  run_program(${OUT_EXE} run_rc actual actual_err)
  set(_both "${actual}${actual_err}")

  # ---- verdict 1/3: ARC guard ---------------------------------------------
  # The guard's own report is the signal worth naming explicitly: a bare
  # non-zero exit says only "it died", while this says which reference
  # outlived its object.
  if(_both MATCHES "ARC integrity failure")
    zan_drop_artifact()
    message(FATAL_ERROR "arcguard: ARC integrity failure in ${SRC}:\n${_both}")
  endif()

  # ---- verdict 2/3: leak check --------------------------------------------
  # EXPECT_LEAK_SITE flips the polarity for cases that keep an object alive on
  # purpose: the run must print a leak report AND that report must name the
  # expected site (a "file.zan:LINE:" fragment), pinning the report's per-site
  # attribution instead of just the total count.
  #
  # EXPECT_LEAK flips it without the site pin: a known leaker must still leak.
  # That is the allow-list the old leakcheck tier used (WILL_FAIL): the case
  # stays green while it leaks, and turns red the moment the leak is fixed --
  # the reminder to drop it from the list.
  if(EXPECT_LEAK_SITE)
    if(NOT _both MATCHES "memory leak detected")
      zan_drop_artifact()
      message(FATAL_ERROR
        "leakcheck: expected a leak report in ${SRC} but none was printed\n${_both}")
    endif()
    if(NOT _both MATCHES "${EXPECT_LEAK_SITE}")
      zan_drop_artifact()
      message(FATAL_ERROR
        "leakcheck: leak report in ${SRC} does not name the expected site '${EXPECT_LEAK_SITE}':\n${_both}")
    endif()
  elseif(EXPECT_LEAK)
    if(NOT _both MATCHES "memory leak detected")
      zan_drop_artifact()
      message(FATAL_ERROR
        "leakcheck: ${SRC} is on the known-leaker list but no leak was reported\n${_both}")
    endif()
  else()
    if(_both MATCHES "memory leak detected")
      zan_drop_artifact()
      message(FATAL_ERROR "leakcheck: leak detected in ${SRC}:\n${_both}")
    endif()
    if(NOT run_rc EQUAL 0)
      zan_drop_artifact()
      message(FATAL_ERROR "program exited with ${run_rc}\nstdout:\n${actual}\nstderr:\n${actual_err}")
    endif()
  endif()

  # ---- verdict 3/3: conformance (stdout vs golden) -------------------------
  # A case that cannot run in this environment (no interactive clipboard on a
  # headless agent session, no GPU, no network) prints a single line starting
  # with "SKIP: " and exits 0. The harness reports it as skipped instead of
  # failing: the environment lacks the resource, the code under test is not
  # at fault.
  if(PLAIN_OUT_EXE)
    # The case leaks on purpose: the leak report goes to stdout, so the golden
    # has to come from an uninstrumented build.
    execute_process(
      COMMAND ${_compiler} ${SRC} -o ${PLAIN_OUT_EXE} ${PLAIN_ZANC_ARGS} ${_plain_default_opt_args}
              --package-project ${WORKDIR}
      RESULT_VARIABLE plain_rc
      OUTPUT_VARIABLE plain_out
      ERROR_VARIABLE  plain_err)
    if(NOT plain_rc EQUAL 0)
      message(FATAL_ERROR "plain compile failed (rc=${plain_rc})\n${plain_out}${plain_err}")
    endif()
    run_program(${PLAIN_OUT_EXE} plain_run_rc plain_actual plain_actual_err)
    if(NOT plain_run_rc EQUAL 0)
      zan_drop_artifact()
      message(FATAL_ERROR "program exited with ${plain_run_rc}\nstdout:\n${plain_actual}\nstderr:\n${plain_actual_err}")
    endif()
    set(actual "${plain_actual}")
  endif()

  if(actual MATCHES "^SKIP: ")
    message(STATUS "SKIPPED: ${SRC} (${actual})")
  else()
    file(READ ${EXPECTED} expected)
    string(REPLACE "\r\n" "\n" expected "${expected}")
    string(REPLACE "\r\n" "\n" actual   "${actual}")
    string(REGEX REPLACE "[ \t\r\n]+$" "" expected "${expected}")
    string(REGEX REPLACE "[ \t\r\n]+$" "" actual   "${actual}")
    if(NOT actual STREQUAL expected)
      zan_drop_artifact()
      message(FATAL_ERROR
        "conformance: output mismatch for ${SRC}\n--- expected ---\n${expected}\n--- actual ---\n${actual}")
    endif()
  endif()
endif()

# ============================================================================
# determinism mode: codegen must be a pure function of the source
# ============================================================================
if(DET_STAMP)
  # Two --emit-ir runs must be byte-for-byte identical. This is the C-hosted
  # analogue of the self-hosting gen2==gen3 diff: it guarantees the compiler's
  # output is a pure function of its input.
  #
  # The verdict is memoised in DET_STAMP, because re-verifying it on every
  # suite run burns two compiles for nothing. The stamp records the args it
  # was computed with, so changing them invalidates the verdict instead of
  # silently keeping the old answer.
  get_filename_component(_det_dir "${DET_STAMP}" DIRECTORY)
  file(MAKE_DIRECTORY "${_det_dir}")
  set(_det_marker "ok|args:${ZANC_ARGS}")
  if(MVC_TOOL)
    string(APPEND _det_marker "|mvc:${MVC_MODE}")
  endif()
  set(_det_current FALSE)
  if(EXISTS ${DET_STAMP})
    file(READ ${DET_STAMP} _det_body)
    if(_det_body STREQUAL "${_det_marker}")
      zan_artifact_is_current(_det_ok ${DET_STAMP})
      if(_det_ok)
        set(_det_current TRUE)
      endif()
    endif()
  endif()

  if(_det_current)
    message(STATUS "deterministic (cached): ${SRC}")
  else()
    function(emit_ir out_var)
      set(_output_args "")
      if(MVC_TOOL)
        set(_output_args -o ${DET_STAMP}.ir)
      endif()
      execute_process(
        COMMAND ${_compiler} ${SRC} ${ZANC_ARGS} ${_default_opt_args} --emit-ir ${_output_args}
        RESULT_VARIABLE rc
        OUTPUT_VARIABLE ir
        ERROR_VARIABLE  err)
      if(NOT rc EQUAL 0)
        message(FATAL_ERROR "emit-ir failed for ${SRC} (rc=${rc})\n${ir}${err}")
      endif()
      if(MVC_TOOL AND NOT ir MATCHES "(^|\n)target triple = ")
        message(FATAL_ERROR "emit-ir produced no LLVM module for ${SRC}\n${ir}${err}")
      endif()
      set(${out_var} "${ir}" PARENT_SCOPE)
    endfunction()
    emit_ir(ir_a)
    emit_ir(ir_b)
    if(NOT ir_a STREQUAL ir_b)
      message(FATAL_ERROR
        "determinism: non-deterministic codegen for ${SRC}: two --emit-ir runs differ")
    endif()
    file(WRITE ${DET_STAMP} "${_det_marker}")
    message(STATUS "deterministic: ${SRC}")
  endif()
endif()

message(STATUS "OK: ${SRC}")
