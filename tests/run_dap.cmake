# Compile a Zan program with debug info (`zanc -g`) and drive the zan-dap
# adapter against it with the DAP integration harness.
#
# Invoked as:
#   cmake -DZANC=<zanc> -DDAP=<zan-dap> -DHARNESS=<harness exe> \
#         -DSRC=<dbgtarget.zan> -DOUT_EXE=<exe path> \
#         [-DZANC_ARGS=<extra;args>] -P run_dap.cmake
#
# The harness itself prints SKIP and succeeds when no usable gdb is present,
# so machines without a debugger do not fail the suite.

if(NOT ZANC OR NOT DAP OR NOT HARNESS OR NOT SRC OR NOT OUT_EXE)
  message(FATAL_ERROR "run_dap.cmake: ZANC, DAP, HARNESS, SRC and OUT_EXE are required")
endif()

# ---- compile with debug info ----
execute_process(
  COMMAND ${ZANC} ${SRC} -g -o ${OUT_EXE} ${ZANC_ARGS}
  RESULT_VARIABLE compile_rc
  OUTPUT_VARIABLE compile_out
  ERROR_VARIABLE  compile_err)
if(NOT compile_rc EQUAL 0)
  message(FATAL_ERROR "debug compile failed (rc=${compile_rc})\n${compile_out}${compile_err}")
endif()

# ---- drive the debug session ----
execute_process(
  COMMAND ${HARNESS} ${DAP} ${OUT_EXE} ${SRC}
  RESULT_VARIABLE test_rc)
if(NOT test_rc EQUAL 0)
  message(FATAL_ERROR "DAP integration test failed (rc=${test_rc})")
endif()

message(STATUS "OK: DAP integration")
