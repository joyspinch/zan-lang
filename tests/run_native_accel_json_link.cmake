# Link against the production archive without GC. A CPU/KAT back-reference that
# extracts an unrelated feature member must be observable in the final symbols.
cmake_minimum_required(VERSION 3.16)
foreach(_required CC NM ARCHIVE SRC OUT_EXE HOST_SYSTEM)
  if(NOT DEFINED ${_required} OR "${${_required}}" STREQUAL "")
    message(FATAL_ERROR "native_accel_json_link: ${_required} is required")
  endif()
endforeach()
foreach(_input "${CC}" "${NM}" "${ARCHIVE}" "${SRC}")
  if(NOT EXISTS "${_input}")
    message(FATAL_ERROR "native_accel_json_link: input not found: ${_input}")
  endif()
endforeach()

get_filename_component(_out_dir "${OUT_EXE}" DIRECTORY)
file(MAKE_DIRECTORY "${_out_dir}")
file(REMOVE "${OUT_EXE}")
# GCC/clang's ELF and GNU PE linkers both accept this. Mach-O does not enable
# dead stripping unless requested, so do not pass -dead_strip there.
if(HOST_SYSTEM STREQUAL "Darwin")
  set(_keep_symbols)
else()
  set(_keep_symbols -Wl,--no-gc-sections)
endif()
execute_process(
  COMMAND "${CC}" -std=c11 -O2 ${CC_DEFS} "${SRC}" "${ARCHIVE}"
          ${_keep_symbols} -o "${OUT_EXE}"
  RESULT_VARIABLE _rc OUTPUT_VARIABLE _out ERROR_VARIABLE _err
  TIMEOUT 90)
if(NOT "${_rc}" STREQUAL "0")
  message(FATAL_ERROR "JSON-only archive link failed (${_rc})\n${_out}${_err}")
endif()
execute_process(
  COMMAND "${NM}" -P "${OUT_EXE}"
  RESULT_VARIABLE _rc OUTPUT_VARIABLE _symbols ERROR_VARIABLE _err
  TIMEOUT 30)
if(NOT "${_rc}" STREQUAL "0")
  message(FATAL_ERROR "JSON-only archive symbols failed (${_rc})\n${_symbols}${_err}")
endif()

set(_json_symbols)
set(_cpu_symbols)
set(_forbidden_symbols)
string(REPLACE "\r\n" "\n" _symbols "${_symbols}")
string(REPLACE "\n" ";" _symbol_lines "${_symbols}")
foreach(_line IN LISTS _symbol_lines)
  if(NOT _line MATCHES "^_?(zan_[A-Za-z0-9_]+)[ \t]+[TtDdBbRrSsCcVvWwGg][ \t]+[0-9a-fA-F]+")
    continue()
  endif()
  set(_symbol "${CMAKE_MATCH_1}")
  if(_symbol MATCHES "^zan_hw_json_|^zan_ctz(32|64)_local$|^zan_(parse_double_fast|dbl_to_i64)$")
    list(APPEND _json_symbols "${_symbol}")
  elseif(_symbol MATCHES "^zan_cpu_feature$|^zan_(sysctl_i|arm_probe|xgetbv_safe)$|^zan_hw_(has_|arm_|kat_state$|init_cpu_features|soft_forced$|x86_can$|features_cover$|gate($|_))")
    list(APPEND _cpu_symbols "${_symbol}")
  else()
    list(APPEND _forbidden_symbols "${_symbol}")
  endif()
endforeach()
foreach(_required zan_hw_json_skip_whitespace zan_hw_json_scan_string)
  if(NOT "${_required}" IN_LIST _json_symbols)
    message(FATAL_ERROR "JSON entry missing from linked consumer: ${_required}")
  endif()
endforeach()
# The two queries prove the scanner linked its real runtime-dispatch dependency.
# At least one must survive on a SIMD host; portable scalar targets need neither.
if(HOST_PROCESSOR MATCHES "^(x86_64|AMD64|amd64|i.86|aarch64|arm64|ARM64)$")
  if(NOT "zan_hw_has_avx2" IN_LIST _cpu_symbols AND
     NOT "zan_hw_has_neon" IN_LIST _cpu_symbols)
    message(FATAL_ERROR "JSON consumer did not link its CPU dispatch query")
  endif()
endif()
if(_forbidden_symbols)
  message(FATAL_ERROR "JSON-only archive link extracted unrelated algorithm symbols: ${_forbidden_symbols}")
endif()

execute_process(
  COMMAND "${OUT_EXE}"
  RESULT_VARIABLE _rc OUTPUT_VARIABLE _out ERROR_VARIABLE _err
  TIMEOUT 30)
string(REPLACE "\r\n" "\n" _out "${_out}")
if(NOT "${_rc}" STREQUAL "0" OR NOT _out STREQUAL "JSON archive isolation OK\n")
  message(FATAL_ERROR "JSON-only consumer failed (${_rc})\n${_out}${_err}")
endif()
message(STATUS "JSON-only archive consumer: scan results correct; only JSON/CPU members linked")
