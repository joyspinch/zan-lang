# Compile and run a Zan program that must fail with a runtime check.
# Required: ZANC, SRC, OUT_EXE, EXPECT_REGEX.

cmake_policy(SET CMP0012 NEW)
if(NOT ZANC OR NOT SRC OR NOT OUT_EXE OR NOT EXPECT_REGEX)
  message(FATAL_ERROR "run_runtime_error.cmake: ZANC, SRC, OUT_EXE and EXPECT_REGEX are required")
endif()

execute_process(
  COMMAND ${ZANC} ${SRC} -o ${OUT_EXE} ${ZANC_ARGS}
  RESULT_VARIABLE compile_rc
  OUTPUT_VARIABLE compile_out
  ERROR_VARIABLE compile_err)
if(NOT compile_rc EQUAL 0)
  message(FATAL_ERROR "compile failed (rc=${compile_rc})\n${compile_out}${compile_err}")
endif()

execute_process(
  COMMAND ${OUT_EXE}
  RESULT_VARIABLE run_rc
  OUTPUT_VARIABLE run_out
  ERROR_VARIABLE run_err)
if(NOT run_rc EQUAL 70)
  message(FATAL_ERROR "expected runtime-check exit 70, got ${run_rc}\n${run_out}${run_err}")
endif()
set(runtime_text "${run_out}${run_err}")
if(NOT runtime_text MATCHES "${EXPECT_REGEX}")
  message(FATAL_ERROR "runtime output did not match '${EXPECT_REGEX}':\n${runtime_text}")
endif()
message(STATUS "runtime check passed: ${SRC}")
