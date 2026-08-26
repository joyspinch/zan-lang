if(NOT DEFINED ZANC OR NOT DEFINED SRC OR NOT DEFINED OUT_EXE OR
   NOT DEFINED EXPECT_REGEX)
  message(FATAL_ERROR "ZANC, SRC, OUT_EXE, and EXPECT_REGEX are required")
endif()

# Optional extra zanc inputs (semicolon-list), e.g. a second source file so an
# access-control case can compile the declaring module alongside the abuser.
set(_zanc_cmd "${ZANC}" "${SRC}")
if(DEFINED ZANC_ARGS)
  foreach(_arg ${ZANC_ARGS})
    list(APPEND _zanc_cmd "${_arg}")
  endforeach()
endif()

execute_process(
  COMMAND ${_zanc_cmd} -o "${OUT_EXE}"
  RESULT_VARIABLE compile_result
  OUTPUT_VARIABLE compile_stdout
  ERROR_VARIABLE compile_stderr)

set(compile_log "${compile_stdout}\n${compile_stderr}")
if(compile_result EQUAL 0)
  message(FATAL_ERROR
    "expected compilation to fail, but it succeeded:\n${compile_log}")
endif()
if(NOT compile_log MATCHES "${EXPECT_REGEX}")
  message(FATAL_ERROR
    "compiler failed without the expected diagnostic '${EXPECT_REGEX}':\n${compile_log}")
endif()
