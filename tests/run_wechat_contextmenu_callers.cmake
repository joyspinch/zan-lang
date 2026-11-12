# Real WeChat caller regression: adapt only App.zan into build, use all other
# template sources and runtime HTML directly from the repository.
# Inputs: ROOT, ZANC, OUT_EXE; optional LOG_DIR and ZANC_ARGS (driver flags).
cmake_minimum_required(VERSION 3.20)

if(NOT ROOT OR NOT ZANC OR NOT OUT_EXE)
  message(FATAL_ERROR "ROOT, ZANC and OUT_EXE are required")
endif()
get_filename_component(ROOT "${ROOT}" ABSOLUTE)
get_filename_component(OUT_EXE "${OUT_EXE}" ABSOLUTE)
get_filename_component(_case_dir "${OUT_EXE}" DIRECTORY)
get_filename_component(_build_dir "${_case_dir}" DIRECTORY)
set(_generated "${_case_dir}/wechat_contextmenu_callers")
set(_template "${ROOT}/templates/gui/gui-wechat")
set(_src "${ROOT}/tests/gui/wechat_contextmenu_callers_test.zan")
set(_expected "${ROOT}/tests/gui/wechat_contextmenu_callers_test.out")
if(NOT LOG_DIR)
  set(LOG_DIR "${_build_dir}/test-out/wechat-contextmenu-callers")
endif()
# Each invocation owns a new directory; a pointer identifies the latest attempt
# without pairing an older result.json with overwritten run logs.
set(_log_root "${LOG_DIR}")
string(TIMESTAMP _attempt_time "%Y%m%dT%H%M%SZ" UTC)
string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef _attempt_nonce)
set(_attempt "${_attempt_time}-${_attempt_nonce}")
set(LOG_DIR "${_log_root}/attempts/${_attempt}")
set(_generated "${_generated}/${_attempt}")
file(MAKE_DIRECTORY "${_generated}" "${LOG_DIR}")
file(WRITE "${_log_root}/latest-attempt.txt" "${LOG_DIR}\n")
message(STATUS "WeChat caller attempt logs: ${LOG_DIR}")

file(READ "${_template}/src/App.zan" _app)
# This is the only production-source adaptation: preserve the real Pick body.
string(REGEX MATCHALL "static void Main\\(\\) \\{" _main_matches "${_app}")
list(LENGTH _main_matches _main_count)
if(NOT _main_count EQUAL 1)
  message(FATAL_ERROR "App.zan must have exactly one expected Main signature")
endif()
string(FIND "${_app}" "{{NAME}}" _token)
if(_token LESS 0)
  message(FATAL_ERROR "App.zan no longer has the expected template NAME token")
endif()
string(REPLACE "{{NAME}}" "WechatMenuCandidateShell" _app "${_app}")
string(REPLACE "static void Main() {" "static void CandidateEntry() {" _app "${_app}")
set(_adapter "${_generated}/App.test-input.zan")
file(WRITE "${_adapter}" "${_app}")

file(GLOB_RECURSE _sources "${_template}/src/*.zan")
list(REMOVE_ITEM _sources "${_template}/src/App.zan")
list(SORT _sources)
list(APPEND _sources "${_adapter}" "${ROOT}/tests/gui/WechatLifecycleWitness.zan")
# Log the exact live sources; do not copy page/network/package/HTML trees.
string(REPLACE ";" "\n" _source_log "${_sources}")
file(WRITE "${LOG_DIR}/sources.txt" "${_src}\n${_source_log}\n")
file(WRITE "${LOG_DIR}/run.stdout.log" "")
file(WRITE "${LOG_DIR}/run.stderr.log" "")
file(WRITE "${LOG_DIR}/result.txt" "status=compiling\n")
file(REMOVE "${OUT_EXE}")
execute_process(
  COMMAND "${ZANC}" "${_src}" ${_sources} ${ZANC_ARGS}
    --auto-stdlib --package-project "${ROOT}" --subsystem console
    --check-leaks --arc-guard --strict-runtime -o "${OUT_EXE}"
  WORKING_DIRECTORY "${ROOT}"
  RESULT_VARIABLE _compile_rc
  OUTPUT_VARIABLE _compile_out ERROR_VARIABLE _compile_err
  TIMEOUT 600 ENCODING UTF-8)
file(WRITE "${LOG_DIR}/compile.stdout.log" "${_compile_out}")
file(WRITE "${LOG_DIR}/compile.stderr.log" "${_compile_err}")
if(NOT "${_compile_rc}" STREQUAL "0")
  file(WRITE "${LOG_DIR}/result.txt" "status=compile-failed\ncompileExit=${_compile_rc}\n")
  message(FATAL_ERROR "WeChat caller compile failed (${_compile_rc})\n${_compile_out}${_compile_err}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env "ZAN_RT_HARD=1" "${OUT_EXE}"
  WORKING_DIRECTORY "${_template}"
  RESULT_VARIABLE _run_rc
  OUTPUT_VARIABLE _actual ERROR_VARIABLE _stderr
  TIMEOUT 120 ENCODING UTF-8)
file(WRITE "${LOG_DIR}/run.stdout.log" "${_actual}")
file(WRITE "${LOG_DIR}/run.stderr.log" "${_stderr}")
file(READ "${_expected}" _golden)
string(REPLACE "\r\n" "\n" _golden "${_golden}")
string(REPLACE "\r\n" "\n" _actual "${_actual}")
set(_golden_ok FALSE)
if("${_actual}" STREQUAL "${_golden}")
  set(_golden_ok TRUE)
endif()
set(_stderr_empty FALSE)
if("${_stderr}" STREQUAL "")
  set(_stderr_empty TRUE)
endif()
set(_leak FALSE)
if("${_actual}${_stderr}" MATCHES "memory leak detected")
  set(_leak TRUE)
endif()
file(WRITE "${LOG_DIR}/result.txt"
  "compileExit=0\nrunExit=${_run_rc}\ngolden=${_golden_ok}\nstderrEmpty=${_stderr_empty}\nleakDetected=${_leak}\n")
if(NOT "${_run_rc}" STREQUAL "0" OR NOT _golden_ok OR NOT _stderr_empty)
  message(FATAL_ERROR
    "WeChat caller failed: rc=${_run_rc}, golden=${_golden_ok}, stderrEmpty=${_stderr_empty}, leak=${_leak}\nstdout:\n${_actual}\nstderr:\n${_stderr}")
endif()
message(STATUS "WeChat caller: compile, golden, strict runtime, leak and ARC checks passed")
