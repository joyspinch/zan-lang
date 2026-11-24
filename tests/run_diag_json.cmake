# S1（docs/ZAN_STANDARD.md §1）：zanc --diag=json 的契约用例。
#   1. stderr 每个非空行都是合法 JSON（纯 JSON Lines，无文本混入）；
#   2. 恰好一条 diagnostic：E0032，位置与 fix-it 与期望一致；
#   3. 末行 summary 的 errors 计数正确；
#   4. 把 fix-it 应用回源码后重新编译必须成功——补丁可机器直接应用。
# 参数：ZANC SRC OUT_EXE FIXED_SRC EXPECT_CODE EXPECT_LINE EXPECT_COL
#       EXPECT_FIX_LINE EXPECT_FIX_COL EXPECT_FIX_TEXT_ASCII
# 补丁文本以 ASCII 码传入（';' = 59）：分号穿过 add_test/-D 会被当成列表分隔符。
foreach(_v ZANC SRC OUT_EXE FIXED_SRC EXPECT_CODE EXPECT_LINE EXPECT_COL
           EXPECT_FIX_LINE EXPECT_FIX_COL EXPECT_FIX_TEXT_ASCII)
  if(NOT DEFINED ${_v})
    message(FATAL_ERROR "${_v} is required")
  endif()
endforeach()
string(ASCII ${EXPECT_FIX_TEXT_ASCII} EXPECT_FIX_TEXT)
get_filename_component(_fixed_dir "${FIXED_SRC}" DIRECTORY)
file(MAKE_DIRECTORY "${_fixed_dir}")

execute_process(
  COMMAND "${ZANC}" "${SRC}" --diag=json -o "${OUT_EXE}"
  RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(rc EQUAL 0)
  message(FATAL_ERROR "expected compilation to fail:\n${out}\n${err}")
endif()

string(REPLACE "\r" "" err "${err}")
string(REPLACE ";" "\;" err_escaped "${err}")
string(REPLACE "\n" ";" lines "${err_escaped}")
set(diag_count 0)
set(summary "")
foreach(line IN LISTS lines)
  if(line STREQUAL "")
    continue()
  endif()
  string(JSON kind ERROR_VARIABLE jerr GET "${line}" kind)
  if(jerr)
    message(FATAL_ERROR "stderr line is not JSON (${jerr}): ${line}\n--- full stderr ---\n${err}")
  endif()
  if(kind STREQUAL "diagnostic")
    math(EXPR diag_count "${diag_count} + 1")
    set(diag "${line}")
  elseif(kind STREQUAL "summary")
    set(summary "${line}")
  else()
    message(FATAL_ERROR "unexpected kind '${kind}': ${line}")
  endif()
endforeach()

if(NOT diag_count EQUAL 1)
  message(FATAL_ERROR "expected exactly 1 diagnostic, got ${diag_count}:\n${err}")
endif()
macro(expect_json json expected)
  string(JSON _got GET "${json}" ${ARGN})
  if(NOT _got STREQUAL "${expected}")
    message(FATAL_ERROR "JSON field '${ARGN}' = '${_got}', expected '${expected}':\n${json}")
  endif()
endmacro()
expect_json("${diag}" "error" severity)
expect_json("${diag}" "${EXPECT_CODE}" code)
expect_json("${diag}" "${EXPECT_LINE}" line)
expect_json("${diag}" "${EXPECT_COL}" col)
string(JSON nfix LENGTH "${diag}" fixits)
if(NOT nfix EQUAL 1)
  message(FATAL_ERROR "expected 1 fix-it, got ${nfix}:\n${diag}")
endif()
expect_json("${diag}" "${EXPECT_FIX_LINE}" fixits 0 line)
expect_json("${diag}" "${EXPECT_FIX_COL}" fixits 0 col)
expect_json("${diag}" "0" fixits 0 length)
expect_json("${diag}" "${EXPECT_FIX_TEXT}" fixits 0 text)
if(summary STREQUAL "")
  message(FATAL_ERROR "missing summary line:\n${err}")
endif()
expect_json("${summary}" "1" errors)

# 按 offset 把补丁应用回源码再编译，必须成功。CMake 的 string() 按字节计，
# 与 JSON 的 offset（0 起、字节）同口径，含 UTF-8 注释也成立。
string(JSON fix_off GET "${diag}" fixits 0 offset)
file(READ "${SRC}" src_text)
string(SUBSTRING "${src_text}" 0 ${fix_off} head_text)
string(SUBSTRING "${src_text}" ${fix_off} -1 tail_text)
file(WRITE "${FIXED_SRC}" "${head_text}${EXPECT_FIX_TEXT}${tail_text}")

execute_process(
  COMMAND "${ZANC}" "${FIXED_SRC}" --diag=json -o "${OUT_EXE}"
  RESULT_VARIABLE rc2 OUTPUT_VARIABLE out2 ERROR_VARIABLE err2)
if(NOT rc2 EQUAL 0)
  message(FATAL_ERROR "applying the fix-it did not make the source compile:\n${err2}")
endif()
