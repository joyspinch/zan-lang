# Inspect real generated instructions: Android x86_64 does not guarantee AVX,
# AES, BMI or FMA. Aggregate initialization must work before runtime CPU checks.
execute_process(
  COMMAND "${ZANC}" "${SRC}" --auto-stdlib --publish --target android-x64 -o "${OUT}"
  RESULT_VARIABLE _rc OUTPUT_VARIABLE _out ERROR_VARIABLE _err
  WORKING_DIRECTORY "${WORKDIR}")
if(NOT _rc EQUAL 0 OR NOT EXISTS "${OUT}")
  message(FATAL_ERROR "Android baseline compile failed (${_rc})\n${_out}\n${_err}")
endif()
execute_process(
  COMMAND "${OBJDUMP}" -d "${OUT}"
  RESULT_VARIABLE _rc OUTPUT_VARIABLE _asm ERROR_VARIABLE _err)
if(NOT _rc EQUAL 0)
  message(FATAL_ERROR "Android baseline disassembly failed (${_rc})\n${_err}")
endif()
file(WRITE "${OUT}.disassembly" "${_asm}")
if(NOT _asm MATCHES "elf64-x86-64" OR NOT _asm MATCHES "<AndroidBaselineCodegen_Make>:" OR NOT _asm MATCHES "<AndroidBaselineCodegen_AverageMask>:")
  message(FATAL_ERROR "Expected exported x86_64 aggregate construction is missing")
endif()
# VEX/EVEX vector instructions start with v; BMI/AES instructions need separate
# checks. Require actual instructions so an empty disassembly cannot pass.
if(NOT _asm MATCHES "[ \t]callq[ \t]")
  message(FATAL_ERROR "No executable construction instructions found")
endif()
if(_asm MATCHES "[ \t](v[a-z0-9]+|aes[a-z0-9]*|andn[qwl]?|bextr[qwl]?|blsi[qwl]?|blsmsk[qwl]?|blsr[qwl]?|bzhi[qwl]?|mulx[qwl]?|pdep[qwl]?|pext[qwl]?|rorx[qwl]?|sarx[qwl]?|shlx[qwl]?|shrx[qwl]?)[ \t\r\n]")
  message(FATAL_ERROR "Android x86_64 emitted a non-baseline instruction: ${CMAKE_MATCH_1}")
endif()
message(STATUS "Android x86_64 aggregate initialization uses baseline instructions")
