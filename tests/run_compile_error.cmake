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
# 可选：断言某条诊断不出现（A382③ 抑制类用例：被 #pragma/--suppress 静默
# 的告警不得出现在日志里）。留空则不检查。
if(DEFINED EXPECT_REGEX_NOT)
  if(compile_log MATCHES "${EXPECT_REGEX_NOT}")
    message(FATAL_ERROR
      "compiler log must not contain '${EXPECT_REGEX_NOT}' but does:\n${compile_log}")
  endif()
endif()
# 可选：锁住错误条数。用来防"诊断点名了，但后面还跟着一串级联报错"——
# 级联报错会把真正的原因淹没。留空则不检查条数。
if(DEFINED EXPECT_ERROR_COUNT)
  if(NOT compile_log MATCHES "([0-9]+) error\\(s\\)")
    message(FATAL_ERROR
      "compiler output has no error count line:\n${compile_log}")
  endif()
  if(NOT CMAKE_MATCH_1 STREQUAL "${EXPECT_ERROR_COUNT}")
    message(FATAL_ERROR
      "expected ${EXPECT_ERROR_COUNT} error(s), got ${CMAKE_MATCH_1}:\n${compile_log}")
  endif()
endif()
