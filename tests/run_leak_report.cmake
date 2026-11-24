# S13（docs/ZAN_STANDARD.md §3）：--check-leaks 退出报告的契约用例。
#   以 --check-leaks 编译 SRC 并运行，stdout 必须逐条包含 EXPECT 文件里的每一行
#   （字面子串，不计顺序——环的打印顺序随对象地址哈希而变）；EXPECT 里没有 "zan: memory leak detected"
#   行时，stdout 不得出现泄漏报告——用来钉住"栈帧/静态字段持有的对象不误报"。
# 参数：ZANC SRC EXPECT OUT_EXE
foreach(_v ZANC SRC EXPECT OUT_EXE)
  if(NOT DEFINED ${_v})
    message(FATAL_ERROR "${_v} is required")
  endif()
endforeach()
get_filename_component(_script_dir "${CMAKE_SCRIPT_MODE_FILE}" DIRECTORY)
get_filename_component(_root "${_script_dir}/.." ABSOLUTE)

execute_process(
  COMMAND "${ZANC}" "${SRC}" --auto-stdlib --check-leaks -o "${OUT_EXE}"
  RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "compile failed (rc=${rc})\n${out}${err}")
endif()

set(ENV{ZAN_LEAK_LOG} "")
execute_process(
  COMMAND "${OUT_EXE}"
  WORKING_DIRECTORY "${_root}"
  RESULT_VARIABLE rc OUTPUT_VARIABLE actual ENCODING UTF-8)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "program exited with ${rc}\n${actual}")
endif()
string(REPLACE "\r\n" "\n" actual "${actual}")

file(STRINGS "${EXPECT}" _expect_lines ENCODING UTF-8)
set(_expects_report FALSE)
foreach(_line IN LISTS _expect_lines)
  if(_line MATCHES "^zan: memory leak detected")
    set(_expects_report TRUE)
  endif()
  string(FIND "${actual}" "${_line}" _at)
  if(_at LESS 0)
    message(FATAL_ERROR "missing expected text: '${_line}'\n--- actual ---\n${actual}")
  endif()
endforeach()
if(NOT _expects_report AND actual MATCHES "zan: memory leak detected")
  message(FATAL_ERROR "unexpected leak report\n--- actual ---\n${actual}")
endif()
message(STATUS "OK: ${SRC}")
