# GUI 用例的「运行期不得报错」门禁。
#
# 为什么不用 run_case.cmake：它只比对 stdout，而运行期守卫（除零、
# 越界这类软错误）把错误写到 stderr，程序本身照样退出 0 —— 用例"绿"了，
# 错误却已经发生。设计器窄区除零就是这么溜过去的：IDE 里只留下一行
# stderr 与一条 zan_crash.log 记录，画布照旧刷新，看着像没事。
#
# 用法：
#   cmake -DZANC=<zanc> -DSRC=<file.zan> -DEXPECTED=<file.out> \
#         -DOUT_EXE=<exe path> [-DZANC_ARGS=<extra;args>] \
#         -P run_gui_no_runtime_error.cmake

cmake_policy(SET CMP0012 NEW)

if(NOT ZANC OR NOT SRC OR NOT EXPECTED OR NOT OUT_EXE)
  message(FATAL_ERROR
    "run_gui_no_runtime_error.cmake: ZANC, SRC, EXPECTED and OUT_EXE are required")
endif()

if(NOT WORKDIR AND CMAKE_SCRIPT_MODE_FILE)
  get_filename_component(_script_dir "${CMAKE_SCRIPT_MODE_FILE}" DIRECTORY)
  get_filename_component(WORKDIR "${_script_dir}/.." ABSOLUTE)
endif()

# 每次都重编：本门禁盯的是运行期行为，产物旧了就说明跑的不是当前源码
# （run_case.cmake 的 up-to-date 判定不看包源码，改了包源码必须删产物）。
execute_process(
  COMMAND ${ZANC} ${SRC} -o ${OUT_EXE} ${ZANC_ARGS} --package-project ${WORKDIR}
  RESULT_VARIABLE compile_rc
  OUTPUT_VARIABLE compile_out
  ERROR_VARIABLE compile_err)
if(NOT compile_rc EQUAL 0)
  message(FATAL_ERROR "compile failed (rc=${compile_rc})\n${compile_out}${compile_err}")
endif()

execute_process(
  COMMAND ${OUT_EXE}
  WORKING_DIRECTORY ${WORKDIR}
  RESULT_VARIABLE run_rc
  OUTPUT_VARIABLE actual
  ERROR_VARIABLE stderr_text
  ENCODING UTF-8)

set(log "${actual}\n${stderr_text}")

# 运行期守卫的错误行：位置 + "runtime error: ..."。
if(log MATCHES "runtime error:")
  message(FATAL_ERROR "runtime error during run:\n${log}")
endif()

if(NOT run_rc EQUAL 0)
  message(FATAL_ERROR "program exited with ${run_rc}\n${log}")
endif()

file(READ ${EXPECTED} expected)
string(REPLACE "\r\n" "\n" expected "${expected}")
string(REPLACE "\r\n" "\n" actual "${actual}")
string(REGEX REPLACE "[ \t\r\n]+$" "" expected "${expected}")
string(REGEX REPLACE "[ \t\r\n]+$" "" actual "${actual}")

if(NOT actual STREQUAL expected)
  message(FATAL_ERROR
    "output mismatch for ${SRC}\n--- expected ---\n${expected}\n--- actual ---\n${actual}")
endif()

message(STATUS "OK: ${SRC}")
