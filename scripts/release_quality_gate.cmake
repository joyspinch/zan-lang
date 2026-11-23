cmake_minimum_required(VERSION 3.20)

# Validate an observed source/build identity. Missing bootstrap gates fail
# closed instead of silently shrinking the release acceptance surface.
if(NOT DEFINED BUILD_DIR)
  message(FATAL_ERROR "BUILD_DIR is required")
endif()
get_filename_component(BUILD_DIR "${BUILD_DIR}" ABSOLUTE)
# An interrupted/failed invocation must not leave a prior acceptance receipt.
file(REMOVE "${BUILD_DIR}/test-out/release-source-before.json"
  "${BUILD_DIR}/test-out/release-source-after.json")
if(NOT EXISTS "${BUILD_DIR}/CMakeCache.txt")
  message(FATAL_ERROR "No configured release build at ${BUILD_DIR}")
endif()
file(STRINGS "${BUILD_DIR}/CMakeCache.txt" strict_runtime
  REGEX "^ZAN_REQUIRE_CROSS_RUNTIME:BOOL=ON$")
if(NOT strict_runtime)
  message(FATAL_ERROR "Release requires configuring with -DZAN_REQUIRE_CROSS_RUNTIME=ON")
endif()
find_program(CTEST_COMMAND ctest REQUIRED)
find_program(PYTHON_COMMAND NAMES python3 python REQUIRED)
get_filename_component(_source_root "${CMAKE_CURRENT_LIST_DIR}/.." REALPATH)
# SOURCE_ROOT supports a tiny synthetic Git worktree in protocol tests. The
# manifest tool still enforces CMAKE_HOME_DIRECTORY and the observed build root.
if(DEFINED SOURCE_ROOT)
  get_filename_component(_source_root "${SOURCE_ROOT}" REALPATH)
endif()
set(_provenance_tool "${CMAKE_CURRENT_LIST_DIR}/release_source_manifest.py")
file(REMOVE "${BUILD_DIR}/test-out/release-source-after.json")
function(_verify_release_source _phase)
  execute_process(COMMAND "${PYTHON_COMMAND}" -I -B "${_provenance_tool}" verify
    --repo "${_source_root}" --build "${BUILD_DIR}"
    --receipt "${BUILD_DIR}/test-out/release-source-${_phase}.json"
    RESULT_VARIABLE _rc OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
  if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "Release build provenance unavailable or changed: ${_out}${_err}")
  endif()
endfunction()
_verify_release_source(before)
execute_process(
  COMMAND "${CTEST_COMMAND}" ${CTEST_ARGUMENTS} --test-dir "${BUILD_DIR}" -C Release
          --show-only=json-v1
  RESULT_VARIABLE list_result OUTPUT_VARIABLE test_json ERROR_VARIABLE list_error)
if(NOT list_result EQUAL 0)
  message(FATAL_ERROR "Cannot enumerate release tests: ${list_error}")
endif()
file(MAKE_DIRECTORY "${BUILD_DIR}/test-out")
set(_ctest_list_json "${BUILD_DIR}/test-out/release-ctest-list.json")
file(WRITE "${_ctest_list_json}" "${test_json}")
set(bootstrap_pattern "^selfhost_(fixed_point|diff|production_diff|gen1|ir_golden|exception_ownership)$")
execute_process(
  COMMAND "${PYTHON_COMMAND}" -I -c
    "import json, sys; data = json.load(open(sys.argv[1], encoding='utf-8')); tests = data.get('tests', []); names = {t.get('name') for t in tests}; req = ['selfhost_fixed_point', 'selfhost_diff', 'selfhost_production_diff', 'selfhost_gen1', 'selfhost_ir_golden', 'selfhost_exception_ownership']; missing = [r for r in req if r not in names]; full = sum(1 for t in tests if any(p.get('name') == 'LABELS' and 'full' in (p.get('value') or []) for p in t.get('properties', []))); print('MISSING=' + ';'.join(missing)); print(f'FULL_COUNT={full}')"
    "${_ctest_list_json}"
  RESULT_VARIABLE _parse_rc
  OUTPUT_VARIABLE _parse_out
  ERROR_VARIABLE _parse_err)
file(REMOVE "${_ctest_list_json}")
if(NOT _parse_rc EQUAL 0)
  message(FATAL_ERROR "Cannot parse release test list: ${_parse_err}")
endif()
set(required)
if(_parse_out MATCHES "MISSING=([^\r\n]+)")
  set(required "${CMAKE_MATCH_1}")
endif()
set(full_count 0)
if(_parse_out MATCHES "FULL_COUNT=([0-9]+)")
  set(full_count "${CMAKE_MATCH_1}")
endif()
if(required)
  message(FATAL_ERROR "Required release tests are missing: ${required}")
endif()
if(full_count EQUAL 0)
  message(FATAL_ERROR "The full release suite is empty")
endif()
# Full source manifests and named tool/native/artifact hashes are retained in
# release-source-before/after.json. HEAD alone is never an input identity.
# Bound concurrent compiler processes while allowing the full suite to finish
# within hosted-runner limits. Bootstrap runs separately below.
include(ProcessorCount)
ProcessorCount(release_jobs)
if(release_jobs LESS 1)
  set(release_jobs 1)
elseif(release_jobs GREATER 4)
  set(release_jobs 4)
endif()
execute_process(
  COMMAND "${CTEST_COMMAND}" ${CTEST_ARGUMENTS} --test-dir "${BUILD_DIR}" -C Release -L full
          -E "${bootstrap_pattern}" --parallel "${release_jobs}"
          --output-on-failure --no-tests=error
          --output-junit "${BUILD_DIR}/test-out/release-full.xml"
  RESULT_VARIABLE full_result)
if(NOT full_result EQUAL 0)
  message(FATAL_ERROR "Full release gate failed: ${full_result}")
endif()
# Bootstrap tests are explicitly required even if their tier labels change.
execute_process(
  COMMAND "${CTEST_COMMAND}" ${CTEST_ARGUMENTS} --test-dir "${BUILD_DIR}" -C Release
          -R "${bootstrap_pattern}"
          --output-on-failure --no-tests=error
          --output-junit "${BUILD_DIR}/test-out/release-bootstrap.xml"
  RESULT_VARIABLE bootstrap_result)
if(NOT bootstrap_result EQUAL 0)
  message(FATAL_ERROR "Bootstrap release gate failed: ${bootstrap_result}")
endif()
# CTest can return success when a test is explicitly skipped. A release gate
# requires executed passes, so inspect the reports as well as the exit status.
foreach(report release-full.xml release-bootstrap.xml)
  file(READ "${BUILD_DIR}/test-out/${report}" junit)
  if(junit MATCHES "<skipped([ />])" OR junit MATCHES "skipped=\"[1-9][0-9]*\"")
    message(FATAL_ERROR "Release tests were skipped: ${report}")
  endif()
endforeach()
_verify_release_source(after)
file(SHA256 "${BUILD_DIR}/test-out/release-source-before.json" _identity_before)
file(SHA256 "${BUILD_DIR}/test-out/release-source-after.json" _identity_after)
if(NOT _identity_before STREQUAL _identity_after)
  file(REMOVE "${BUILD_DIR}/test-out/release-source-after.json")
  message(FATAL_ERROR "Release build identity changed during validation")
endif()
message(STATUS "Release quality gate passed (${full_count} full-suite tests; observed source/build identity stable)")
