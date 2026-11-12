cmake_minimum_required(VERSION 3.20)
# Standalone: cmake -DTEST_DIR=<isolated directory under build/test-out> -P <this>
# No compiler or normalizer is run; CMake itself is the fake compiler sentinel.
if(NOT TEST_DIR)
  message(FATAL_ERROR "TEST_DIR is required")
endif()
if(NOT GATE)
  set(GATE "${CMAKE_CURRENT_LIST_DIR}/run_selfhost_ir_golden.cmake")
endif()
if(EXISTS "${TEST_DIR}")
  message(FATAL_ERROR "TEST_DIR must be a new directory: ${TEST_DIR}")
endif()
file(MAKE_DIRECTORY "${TEST_DIR}/cases" "${TEST_DIR}/goldens")
file(WRITE "${TEST_DIR}/cases.txt" "# fixture\nalpha\nbeta\n")
set(marker "${TEST_DIR}/compiler-started")
file(WRITE "${TEST_DIR}/fake-compiler.cmake"
  "file(WRITE \"${marker}\" \"started\")\nmessage(FATAL_ERROR \"FAKE_COMPILER_REACHED\")\n")
function(check name bless should_start expected)
  file(REMOVE "${marker}")
  execute_process(COMMAND "${CMAKE_COMMAND}"
    "-DZANC=${CMAKE_COMMAND}"
    "-DSELFHOST_SOURCES=-P;${TEST_DIR}/fake-compiler.cmake"
    "-DGEN1=${TEST_DIR}/unused-gen1"
    "-DOUTDIR=${TEST_DIR}/output"
    "-DCASE_DIR=${TEST_DIR}/cases"
    "-DCASES=${TEST_DIR}/cases.txt"
    "-DGOLDEN_DIR=${TEST_DIR}/goldens"
    "-DNORM=${TEST_DIR}/unused-normalizer.py"
    "-DBLESS=${bless}" -P "${GATE}"
    RESULT_VARIABLE rc OUTPUT_VARIABLE output ERROR_VARIABLE error)
  if(rc EQUAL 0)
    message(FATAL_ERROR "${name}: expected nonzero sentinel/preflight exit")
  endif()
  if(should_start)
    if(NOT EXISTS "${marker}" OR NOT error MATCHES "FAKE_COMPILER_REACHED")
      message(FATAL_ERROR "${name}: compiler not reached: ${output}\n${error}")
    endif()
  elseif(EXISTS "${marker}")
    message(FATAL_ERROR "${name}: preflight incorrectly started compiler")
  endif()
  foreach(pattern IN LISTS expected)
    if(NOT error MATCHES "${pattern}")
      message(FATAL_ERROR "${name}: missing diagnostic ${pattern}: ${error}")
    endif()
  endforeach()
  message(STATUS "PASS ${name}")
endfunction()
# All missing entries from both categories must be reported in one invocation.
check(all_missing OFF FALSE "Missing case sources: alpha, beta;Missing golden files: alpha, beta")
file(WRITE "${TEST_DIR}/cases/alpha.zan" "// synthetic\n")
file(WRITE "${TEST_DIR}/cases/beta.zan" "// synthetic\n")
check(missing_goldens OFF FALSE "Missing golden files: alpha, beta")
# Explicit BLESS permits absent goldens and enters the ordinary build path.
check(bless_missing_goldens ON TRUE "FAKE_COMPILER_REACHED")
file(WRITE "${TEST_DIR}/goldens/alpha.hashes" "fixture\n")
file(WRITE "${TEST_DIR}/goldens/beta.hashes" "fixture\n")
file(REMOVE "${TEST_DIR}/cases/beta.zan")
check(missing_source OFF FALSE "Missing case sources: beta")
check(bless_missing_source ON FALSE "Missing case sources: beta")
file(WRITE "${TEST_DIR}/cases/beta.zan" "// synthetic\n")
check(complete_inputs OFF TRUE "FAKE_COMPILER_REACHED")
file(REMOVE_RECURSE "${TEST_DIR}")
message(STATUS "IR golden preflight regression passed")
