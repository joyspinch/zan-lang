# Compile and run a Zan program that must fail with a runtime check.
# Required: ZANC, SRC, OUT_EXE, EXPECT_REGEX.
# STRICT=1 compiles with --strict-runtime and verifies exit(70) happens with
# NO ZAN_RT_HARD env var (the program-baked flag, audit D7-family).
# EXPECT_EXIT overrides the expected exit code (default 70, the guard-hard
# contract); B-ID163③ unhandled-exception cases exit 1.

cmake_policy(SET CMP0012 NEW)
if(NOT ZANC OR NOT SRC OR NOT OUT_EXE OR NOT EXPECT_REGEX)
  message(FATAL_ERROR "run_runtime_error.cmake: ZANC, SRC, OUT_EXE and EXPECT_REGEX are required")
endif()

if(STRICT)
  set(_strict_flag "--strict-runtime")
else()
  set(_strict_flag "")
endif()
execute_process(
  COMMAND ${ZANC} ${SRC} -o ${OUT_EXE} ${ZANC_ARGS} ${_strict_flag}
  RESULT_VARIABLE compile_rc
  OUTPUT_VARIABLE compile_out
  ERROR_VARIABLE compile_err)
if(NOT compile_rc EQUAL 0)
  message(FATAL_ERROR "compile failed (rc=${compile_rc})\n${compile_out}${compile_err}")
endif()

# Runtime-error cases pin the historical fail-fast contract. Production keeps
# the fail-soft default, so opt this harness into hard mode explicitly —
# except STRICT mode, which must fail fast with no env var at all.
if(NOT STRICT)
  set(ENV{ZAN_RT_HARD} "1")
endif()
if(NOT EXPECT_EXIT)
  set(EXPECT_EXIT 70)
endif()
execute_process(
  COMMAND ${OUT_EXE}
  RESULT_VARIABLE run_rc
  OUTPUT_VARIABLE run_out
  ERROR_VARIABLE run_err)
if(NOT run_rc EQUAL ${EXPECT_EXIT})
  message(FATAL_ERROR "expected runtime-check exit ${EXPECT_EXIT}, got ${run_rc}\n${run_out}${run_err}")
endif()
set(runtime_text "${run_out}${run_err}")
if(NOT runtime_text MATCHES "${EXPECT_REGEX}")
  message(FATAL_ERROR "runtime output did not match '${EXPECT_REGEX}':\n${runtime_text}")
endif()
message(STATUS "runtime check passed: ${SRC}")
