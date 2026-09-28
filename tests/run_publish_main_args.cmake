if(NOT ZANC OR NOT SRC OR NOT EXPECTED OR NOT OUT_EXE)
  message(FATAL_ERROR "ZANC, SRC, EXPECTED and OUT_EXE are required")
endif()

execute_process(
  COMMAND "${ZANC}" "${SRC}" --publish -o "${OUT_EXE}"
  RESULT_VARIABLE compile_rc
  OUTPUT_VARIABLE compile_out
  ERROR_VARIABLE compile_err)
if(NOT compile_rc EQUAL 0)
  message(FATAL_ERROR "publish link failed (${compile_rc}):\n${compile_out}${compile_err}")
endif()

execute_process(
  COMMAND "${OUT_EXE}" alpha "beta gamma"
  RESULT_VARIABLE run_rc
  OUTPUT_VARIABLE run_out
  ERROR_VARIABLE run_err)
if(NOT run_rc EQUAL 0)
  message(FATAL_ERROR "published program failed (${run_rc}):\n${run_out}${run_err}")
endif()
file(READ "${EXPECTED}" expected)
string(REPLACE "\r\n" "\n" expected "${expected}")
string(REPLACE "\r\n" "\n" run_out "${run_out}")
string(STRIP "${expected}" expected)
string(STRIP "${run_out}" run_out)
if(NOT run_out STREQUAL expected)
  message(FATAL_ERROR "published argv output mismatch:\nexpected: ${expected}\nactual: ${run_out}")
endif()
