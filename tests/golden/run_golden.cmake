# Golden-file regression runner for the Zan compiler.
#
# Invoked by ctest as:
#   cmake -DZANC=<zanc> -DSRC=<case.zan> -DMODE=<tokens|ast|ir> \
#         -DEXPECTED=<golden-file> -DWORKDIR=<dir> -P run_golden.cmake
#
# tokens/ast : exact comparison against a frozen golden (after normalization).
# ir         : structural check — EXPECTED lists required substrings, one per
#              line (kept LLVM-version/platform agnostic on purpose).
#
# Regenerate the tokens/ast goldens with tests/golden/update_golden.cmake.

if(MODE STREQUAL "tokens")
  set(_flag "--dump-tokens")
elseif(MODE STREQUAL "ast")
  set(_flag "--dump-ast")
elseif(MODE STREQUAL "ir")
  set(_flag "--emit-ir")
else()
  message(FATAL_ERROR "run_golden: unknown MODE '${MODE}'")
endif()

execute_process(
  COMMAND "${ZANC}" "${SRC}" ${_flag}
  OUTPUT_VARIABLE _actual
  ERROR_VARIABLE _err
  RESULT_VARIABLE _rc
  WORKING_DIRECTORY "${WORKDIR}")

if(NOT _rc EQUAL 0)
  message(FATAL_ERROR "run_golden: zanc ${_flag} on ${SRC} failed (rc=${_rc})\n${_err}")
endif()

# Normalize compiler text output so goldens are stable across platforms:
#  - drop CR (Windows CRLF)
#  - drop the platform-specific "Compiled '<path>' -> '<path>'" success line
#  - strip trailing spaces on each line and trailing blank lines
function(_normalize var)
  set(s "${${var}}")
  string(REPLACE "\r" "" s "${s}")
  string(REGEX REPLACE "Compiled [^\n]*\n?" "" s "${s}")
  string(REGEX REPLACE " +\n" "\n" s "${s}")
  string(REGEX REPLACE "[ \t\r\n]+$" "" s "${s}")
  set(${var} "${s}" PARENT_SCOPE)
endfunction()

if(MODE STREQUAL "ir")
  # EXPECTED is a list of required substrings (one per line).
  file(STRINGS "${EXPECTED}" _needles)
  foreach(_n ${_needles})
    if(_n STREQUAL "")
      continue()
    endif()
    string(FIND "${_actual}" "${_n}" _pos)
    if(_pos EQUAL -1)
      message(FATAL_ERROR "run_golden: IR for ${SRC} is missing expected symbol:\n  ${_n}")
    endif()
  endforeach()
  return()
endif()

_normalize(_actual)
file(READ "${EXPECTED}" _expected)
_normalize(_expected)

if(NOT _actual STREQUAL _expected)
  file(WRITE "${EXPECTED}.actual" "${_actual}\n")
  message(FATAL_ERROR
    "Golden mismatch: ${SRC} (${MODE}).\n"
    "Actual written to ${EXPECTED}.actual\n"
    "If intended, regenerate: cmake -DZANC=<zanc> -P tests/golden/update_golden.cmake\n"
    "===== expected =====\n${_expected}\n===== actual =====\n${_actual}\n")
endif()
