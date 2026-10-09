cmake_minimum_required(VERSION 3.20)
if(NOT ROOT OR NOT OUT_DIR)
  message(FATAL_ERROR "ROOT and OUT_DIR are required")
endif()
get_filename_component(ROOT "${ROOT}" ABSOLUTE)
get_filename_component(OUT_DIR "${OUT_DIR}" ABSOLUTE)
if(NOT CC)
  find_program(CC NAMES clang REQUIRED)
endif()
if(WIN32 AND NOT CXX)
  find_program(CXX NAMES clang++ REQUIRED)
endif()
file(MAKE_DIRECTORY "${OUT_DIR}")

function(run_checked)
  execute_process(COMMAND ${ARGV} RESULT_VARIABLE _rc
    OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
  if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "Command failed (${_rc}): ${ARGV}\n${_out}${_err}")
  endif()
endfunction()

set(_flags -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all
  -fno-omit-frame-pointer -Wall -Wextra)
if(WIN32)
  list(APPEND _flags -D_CRT_SECURE_NO_WARNINGS -mllvm -asan-realign-stack=16)
  run_checked("${CC}" ${_flags} -c "${ROOT}/tests/runtime/gui_surface_round_test.c"
    -o "${OUT_DIR}/surface.obj")
  run_checked("${CC}" ${_flags} -c "${ROOT}/src/runtime/gui_runtime.c"
    -o "${OUT_DIR}/gui.obj")
  run_checked("${CXX}" ${_flags} -c "${ROOT}/src/runtime/gui_runtime_dwrite.cpp"
    -o "${OUT_DIR}/dwrite.obj")
  set(_exe "${OUT_DIR}/gui_surface_san.exe")
  run_checked("${CXX}" ${_flags} -fuse-ld=lld "${OUT_DIR}/surface.obj"
    "${OUT_DIR}/gui.obj" "${OUT_DIR}/dwrite.obj" -o "${_exe}"
    -luser32 -lgdi32 -ldwmapi -lshcore -limm32 -lole32)
  execute_process(COMMAND "${CC}" -print-resource-dir OUTPUT_VARIABLE _resources
    OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
  file(GLOB _asan_dll "${_resources}/lib/windows/clang_rt.asan_dynamic-x86_64.dll")
  if(NOT _asan_dll)
    message(FATAL_ERROR "Windows x64 ASan runtime DLL not found in ${_resources}")
  endif()
  file(COPY ${_asan_dll} DESTINATION "${OUT_DIR}")
  # LeakSanitizer is unavailable on Windows; ARC tests cover Zan object leaks.
  set(_asan_options "detect_leaks=0")
else()
  set(_exe "${OUT_DIR}/gui_surface_san")
  run_checked("${CC}" ${_flags} -std=c11 -D_GNU_SOURCE
    "${ROOT}/tests/runtime/gui_surface_round_test.c"
    "${ROOT}/src/runtime/gui_runtime.c" -o "${_exe}" -pthread -lX11 -ldl -lm)
  set(_asan_options "detect_leaks=1")
endif()

execute_process(COMMAND "${CMAKE_COMMAND}" -E env
  "ASAN_OPTIONS=${_asan_options}" "UBSAN_OPTIONS=print_stacktrace=1"
  "${_exe}" WORKING_DIRECTORY "${OUT_DIR}" RESULT_VARIABLE _rc
  OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
if(NOT _rc EQUAL 0 OR "${_out}${_err}" MATCHES "AddressSanitizer:|runtime error:")
  message(FATAL_ERROR "GUI sanitizer regression failed (${_rc})\n${_out}${_err}")
endif()
message(STATUS "GUI ASan/UBSan: ${_out}")
