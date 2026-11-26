# S4（docs/ZAN_STANDARD.md §1）：zanc --summary 金样对账。
#   zanc SRC --summary --auto-stdlib 的 stdout 必须与 EXPECTED 一致（换行归一）。
# 参数：ZANC SRC EXPECTED
foreach(_v ZANC SRC EXPECTED)
  if(NOT DEFINED ${_v})
    message(FATAL_ERROR "${_v} is required")
  endif()
endforeach()
get_filename_component(_src_dir "${SRC}" DIRECTORY)
get_filename_component(_src_name "${SRC}" NAME)
execute_process(
  COMMAND "${ZANC}" "${_src_name}" --summary --auto-stdlib
  WORKING_DIRECTORY "${_src_dir}"
  RESULT_VARIABLE rc OUTPUT_VARIABLE actual ERROR_VARIABLE err ENCODING UTF-8)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "zanc --summary failed (rc=${rc})\n${err}")
endif()
file(READ "${EXPECTED}" expected)
foreach(_v expected actual)
  string(REPLACE "\r\n" "\n" ${_v} "${${_v}}")
  string(REGEX REPLACE "[ \t\r\n]+$" "" ${_v} "${${_v}}")
endforeach()
if(NOT actual STREQUAL expected)
  message(FATAL_ERROR "summary mismatch\n--- expected ---\n${expected}\n--- actual ---\n${actual}")
endif()
message(STATUS "OK: ${SRC}")
