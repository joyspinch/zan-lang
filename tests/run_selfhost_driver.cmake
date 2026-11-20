# Tests an already-built gen1; this script never builds the compiler.
# Registration: add_test(NAME selfhost_driver COMMAND ${CMAKE_COMMAND}
#   -DGEN1=<gen1 executable> -DOUTDIR=<build>/test-out/selfhost-driver
#   -P ${CMAKE_SOURCE_DIR}/tests/run_selfhost_driver.cmake)
if(NOT GEN1 OR NOT EXISTS "${GEN1}" OR NOT OUTDIR)
  message(FATAL_ERROR "GEN1 (existing executable) and OUTDIR are required")
endif()
get_filename_component(_root "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
file(MAKE_DIRECTORY "${OUTDIR}")
set(_clean "${_root}/tests/selfhost/driver_clean.zan")
set(_warning "${_root}/tests/selfhost/driver_warning.zan")

function(reject_cli name expected)
  set(_out "${OUTDIR}/${name}.ll")
  file(REMOVE "${_out}")
  execute_process(COMMAND "${GEN1}" -o "${_out}" "${_clean}" ${ARGN}
    WORKING_DIRECTORY "${_root}" RESULT_VARIABLE rc
    OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr TIMEOUT 45)
  string(FIND "${stdout}${stderr}" "${expected}" found)
  if("${rc}" STREQUAL "0" OR EXISTS "${_out}" OR found EQUAL -1)
    message(FATAL_ERROR "${name}: expected rejection '${expected}', rc=${rc}\n${stdout}${stderr}")
  endif()
endfunction()

reject_cli(unknown_long "unknown option '--batch4-unknown'" --batch4-unknown)
reject_cli(unknown_short "unknown option '-Q'" -Q)
reject_cli(target_missing "option '--target' requires a value" --target)
reject_cli(output_missing "option '-o' requires a value" -o)
reject_cli(project_missing "option '--package-project' requires a value" --package-project)
reject_cli(metadata_missing "option '--gen-meta' requires a value" --gen-meta)
reject_cli(metadata_bodies_without_output "--gen-meta-bodies requires --gen-meta" --gen-meta-bodies)
reject_cli(link_input_missing "option '--link-input' requires a value" --link-input)
reject_cli(link_input_absent "link input file does not exist" --link-input "${OUTDIR}/absent.obj")
reject_cli(link_input_ir "--link-input requires executable output" --link-input "${_clean}")
reject_cli(target_flag_value "option '--target' requires a value" --target --auto-stdlib)
reject_cli(target_cross "target 'wasm32-wasi' is not supported" --target wasm32-wasi)
reject_cli(target_invalid "target 'batch4-invalid' is not supported" --target batch4-invalid)
reject_cli(response_options "options in response files are not supported" "@${_root}/tests/selfhost/driver_response_options.rsp")
foreach(flag IN ITEMS -c -S -g --debug --publish --emit-ir)
  string(MAKE_C_IDENTIFIER "${flag}" name)
  if(flag STREQUAL "--emit-ir")
    reject_cli("reject_${name}" "unknown option '${flag}'" "${flag}")
  else()
    reject_cli("reject_${name}" "option '${flag}' is not supported" "${flag}")
  endif()
endforeach()

foreach(mode IN ITEMS host warning deny clean_deny initializer_clean_deny)
  set(_out "${OUTDIR}/${mode}.ll")
  set(_input "${_clean}")
  set(_args --auto-stdlib)
  if(mode STREQUAL "host")
    list(APPEND _args --target host -O0)
  elseif(mode STREQUAL "warning" OR mode STREQUAL "deny")
    set(_input "${_warning}")
  elseif(mode STREQUAL "initializer_clean_deny")
    set(_input "${_root}/tests/selfhost/method_group_initializer_clean.zan")
  endif()
  if(mode STREQUAL "deny" OR mode STREQUAL "clean_deny" OR mode STREQUAL "initializer_clean_deny")
    list(APPEND _args --deny-warnings)
  endif()
  file(REMOVE "${_out}")
  execute_process(COMMAND "${GEN1}" -o "${_out}" "${_input}" ${_args}
    WORKING_DIRECTORY "${_root}" RESULT_VARIABLE rc
    OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr TIMEOUT 45)
  if(mode STREQUAL "deny")
    string(FIND "${stdout}${stderr}" "warnings denied by --deny-warnings" found)
    if("${rc}" STREQUAL "0" OR EXISTS "${_out}" OR found EQUAL -1)
      message(FATAL_ERROR "deny: warning must prevent output, rc=${rc}\n${stdout}${stderr}")
    endif()
  else()
    if(NOT "${rc}" STREQUAL "0" OR NOT EXISTS "${_out}")
      message(FATAL_ERROR "${mode}: expected IR, rc=${rc}\n${stdout}${stderr}")
    endif()
    file(READ "${_out}" ir)
    string(REGEX MATCHALL "(^|\n)(declare|define)[^\n]*@zan_co_sched_run_until\\(" scheduler_entries "${ir}")
    list(LENGTH scheduler_entries scheduler_count)
    string(REGEX MATCH "(^|\n)define weak void @zan_co_sched_run_until\\(i32\\*" scheduler_definition "${ir}")
    if(NOT scheduler_count EQUAL 1 OR scheduler_definition STREQUAL "")
      message(FATAL_ERROR "${mode}: scheduler needs exactly one weak definition and no declaration, got ${scheduler_count}")
    endif()
    if(mode STREQUAL "warning")
      string(FIND "${stdout}${stderr}" "potential strong reference cycle" found)
      if(found EQUAL -1)
        message(FATAL_ERROR "warning fixture did not produce its warning\n${stdout}${stderr}")
      endif()
    endif()
  endif()
endforeach()

# The isolated stdlib contains one referenced type, one test declaration and
# three unrelated types that used to be pulled in by MVC naming rules.
set(_pullin "${_root}/tests/selfhost/pullin")
set(_pullin_ir "${OUTDIR}/pullin.ll")
file(REMOVE "${_pullin_ir}")
execute_process(COMMAND "${CMAKE_COMMAND}" -E env "ZAN_STDLIB_PATH=${_pullin}/stdlib"
  "${GEN1}" -o "${_pullin_ir}" "${_pullin}/main.zan" --auto-stdlib -O0
  WORKING_DIRECTORY "${_root}" RESULT_VARIABLE rc
  OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr TIMEOUT 45)
file(REMOVE "${_pullin_ir}")
if(NOT "${rc}" STREQUAL "0" OR NOT stdout MATCHES "Compiled 3 files ->")
  message(FATAL_ERROR "pull-in: expected the input, the referenced type and the test declaration, rc=${rc}\n${stdout}${stderr}")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/run_selfhost_metadata.cmake")

if(WIN32 AND CLANG)
  set(_default_src "${OUTDIR}/default output.zan")
  set(_default_exe "${OUTDIR}/default output.exe")
  execute_process(COMMAND "${CLANG}" -print-target-triple OUTPUT_VARIABLE triple
    OUTPUT_STRIP_TRAILING_WHITESPACE RESULT_VARIABLE rc TIMEOUT 15)
  if(NOT "${rc}" STREQUAL "0")
    message(FATAL_ERROR "could not determine Clang target, rc=${rc}")
  endif()
  set(arch x64)
  if(triple MATCHES "^(aarch64|arm64)-")
    set(arch arm64)
  endif()
  set(stdlib "${OUTDIR}/native-stdlib")
  set(stdlib_driver "${stdlib}/Fixture/Nested/drivers")
  file(MAKE_DIRECTORY "${stdlib_driver}/win-${arch}")
  file(WRITE "${stdlib_driver}/driver.manifest" "stdlib_fixture if main\n")
  file(WRITE "${stdlib_driver}/win-${arch}/stdlib_fixture.bundle" "stdlib-payload.dat\n")
  file(WRITE "${stdlib_driver}/win-${arch}/stdlib-payload.dat" "standard library driver payload")
  file(WRITE "${_default_src}" [=[
class Program {
    [DllImport("kernel32")]
    static extern nint GetCurrentProcess();
    [DllImport("psapi")]
    static extern int GetProcessMemoryInfo(nint process, byte[] counters, int size);
    static int Main() {
        byte[] counters = new byte[72];
        return GetProcessMemoryInfo(GetCurrentProcess(), counters, counters.Length) != 0 ? 37 : 1;
    }
}
]=])
  file(REMOVE "${_default_exe}" "${OUTDIR}/stdlib-payload.dat")
  execute_process(COMMAND "${CMAKE_COMMAND}" -E env "ZAN_CLANG=${CLANG}" "ZAN_STDLIB_PATH=${stdlib}"
    "${GEN1}" "${_default_src}" WORKING_DIRECTORY "${_root}"
    RESULT_VARIABLE rc OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr TIMEOUT 45)
  if(NOT "${rc}" STREQUAL "0" OR NOT EXISTS "${_default_exe}" OR NOT EXISTS "${OUTDIR}/stdlib-payload.dat")
    message(FATAL_ERROR "default executable output or stdlib bundle failed, rc=${rc}\n${stdout}${stderr}")
  endif()
  execute_process(COMMAND "${_default_exe}" RESULT_VARIABLE rc TIMEOUT 15)
  file(REMOVE "${_default_src}" "${_default_exe}" "${OUTDIR}/stdlib-payload.dat")
  file(REMOVE_RECURSE "${stdlib}")
  if(NOT "${rc}" STREQUAL "37")
    message(FATAL_ERROR "default executable returned ${rc}, expected 37")
  endif()

  set(_sqlite_case "${_root}/tests/conformance/sqlite_params")
  set(_sqlite_out "${OUTDIR}/sqlite-default")
  file(MAKE_DIRECTORY "${_sqlite_out}")
  set(_sqlite_exe "${_sqlite_out}/sqlite_params.exe")
  set(_sqlite_dll "${_sqlite_out}/libsqlite3-0.dll")
  file(REMOVE "${_sqlite_exe}" "${_sqlite_dll}")
  execute_process(COMMAND "${CMAKE_COMMAND}" -E env "ZAN_CLANG=${CLANG}"
    "${GEN1}" "${_sqlite_case}.zan" -o "${_sqlite_exe}" WORKING_DIRECTORY "${_root}"
    RESULT_VARIABLE rc OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr TIMEOUT 60)
  if(NOT "${rc}" STREQUAL "0" OR NOT EXISTS "${_sqlite_exe}" OR NOT EXISTS "${_sqlite_dll}")
    message(FATAL_ERROR "automatic SQLite link or bundle failed, rc=${rc}\n${stdout}${stderr}")
  endif()
  execute_process(COMMAND "${_sqlite_exe}" WORKING_DIRECTORY "${_sqlite_out}"
    RESULT_VARIABLE rc OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr TIMEOUT 15)
  file(READ "${_sqlite_case}.out" expected)
  string(STRIP "${expected}" expected)
  string(STRIP "${stdout}" actual)
  file(REMOVE "${_sqlite_exe}" "${_sqlite_dll}")
  if(NOT "${rc}" STREQUAL "0" OR NOT actual STREQUAL expected)
    message(FATAL_ERROR "automatic SQLite executable failed, rc=${rc}\n${stdout}${stderr}")
  endif()

  set(_exe "${OUTDIR}/driver output.exe")
  set(_native "${_root}/tests/selfhost/driver_native.zan")
  set(_obj "${OUTDIR}/driver native.obj")
  execute_process(COMMAND "${CLANG}" -c "${_root}/tests/selfhost/driver_native.c" -o "${_obj}"
    RESULT_VARIABLE rc OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr TIMEOUT 45)
  if(NOT "${rc}" STREQUAL "0")
    message(FATAL_ERROR "native fixture compilation failed, rc=${rc}\n${stdout}${stderr}")
  endif()
  execute_process(COMMAND "${CMAKE_COMMAND}" -E env "ZAN_CLANG=${CLANG}"
    "${GEN1}" -o "${_exe}" "${_native}" --auto-stdlib -O0 --link-input "driver native.obj"
    WORKING_DIRECTORY "${OUTDIR}" RESULT_VARIABLE rc
    OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr TIMEOUT 45)
  if(NOT "${rc}" STREQUAL "0" OR NOT EXISTS "${_exe}")
    message(FATAL_ERROR "direct executable link failed, rc=${rc}\n${stdout}${stderr}")
  endif()
  execute_process(COMMAND "${_exe}" RESULT_VARIABLE rc TIMEOUT 15)
  if(NOT "${rc}" STREQUAL "0")
    message(FATAL_ERROR "direct executable failed, rc=${rc}")
  endif()
  file(SHA256 "${_exe}" _before)
  execute_process(COMMAND "${CMAKE_COMMAND}" -E env "ZAN_CLANG=${CLANG}"
    "${GEN1}" -o "${_exe}" "${_native}" --auto-stdlib -O0
    WORKING_DIRECTORY "${_root}" RESULT_VARIABLE rc
    OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr TIMEOUT 45)
  file(REMOVE "${_obj}")
  if("${rc}" STREQUAL "0" OR NOT "${stdout}${stderr}" MATCHES "driver_fixture")
    message(FATAL_ERROR "expected a missing native import library, rc=${rc}\n${stdout}${stderr}")
  endif()
  file(SHA256 "${_exe}" _after)
  file(GLOB _staging "${OUTDIR}/.zanc-*")
  if(NOT _before STREQUAL _after OR _staging)
    message(FATAL_ERROR "failed link altered the executable or retained staging files")
  endif()
  file(REMOVE "${_exe}")

  # Explicit objects jointly cover a registered driver while SQLite still
  # links automatically. The bundle-only package has no imported namespace.
  set(project "${OUTDIR}/native-bundles")
  set(app_driver "${project}/drivers/win-${arch}")
  set(dep_module "${project}/.zan-packages/Zan.DriverFixture/src/Fixture")
  set(dep_driver "${dep_module}/drivers/win-${arch}")
  set(_exe "${project}/app.exe")
  set(_obj "${project}/explicit native.obj")
  file(MAKE_DIRECTORY "${app_driver}" "${dep_driver}")
  file(REMOVE "${_exe}" "${project}/libsqlite3-0.dll" "${project}/payload.dat" "${project}/conditional.dat")
  file(WRITE "${project}/app.zan" [=[
using System;
using System.Data.Sqlite;
class Program {
    [DllImport("libdriver_fixture", EntryPoint = "driver_native_twice")]
    static extern int Twice(int value);
    [DllImport("libdriver_fixture", EntryPoint = "driver_fixture_offset")]
    static extern int Offset();
    static int BundleReady() { return Twice(20) + Offset(); }
    static int DeadBundle() { return 0; }
    static int Main() {
        SqliteConnection db = SqliteConnection.Open(":memory:");
        db.Close();
        return BundleReady();
    }
}
]=])
  file(WRITE "${project}/drivers/driver.manifest" "driver_fixture if Program_DeadBundle\noptional_fixture if Program_BundleReady\n")
  file(WRITE "${app_driver}/driver_fixture.bundle" "@driver/dep_fixture if Program_BundleReady\nabsent.dat if Program_DeadBundle\n")
  file(WRITE "${app_driver}/optional_fixture.bundle" "conditional.dat\n")
  file(WRITE "${app_driver}/conditional.dat" "activated by a retained definition")
  file(WRITE "${dep_module}/drivers/driver.manifest" "dep_fixture\n")
  file(WRITE "${dep_driver}/dep_fixture.bundle" "payload.dat\n")
  file(WRITE "${dep_driver}/payload.dat" "recursive bundle payload")
  execute_process(COMMAND "${CLANG}" -c "${_root}/tests/selfhost/driver_native.c" -o "${_obj}"
    COMMAND_ERROR_IS_FATAL ANY TIMEOUT 45)
  file(WRITE "${project}/offset.c" "int driver_fixture_offset(void) { return 2; }\n")
  execute_process(COMMAND "${CLANG}" -c "${project}/offset.c" -o "${project}/offset.obj"
    RESULT_VARIABLE rc OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr TIMEOUT 45)
  if(NOT "${rc}" STREQUAL "0")
    message(FATAL_ERROR "second native fixture compilation failed, rc=${rc}\n${stdout}${stderr}")
  endif()
  set(compile "${CMAKE_COMMAND}" -E env "ZAN_CLANG=${CLANG}" "${GEN1}"
    "${project}/app.zan" -o "${_exe}" --package-project "${project}")
  set(explicit_inputs --link-input "explicit native.obj" --link-input offset.obj)
  execute_process(COMMAND ${compile} ${explicit_inputs}
    WORKING_DIRECTORY "${project}" RESULT_VARIABLE rc
    OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr TIMEOUT 60)
  if(NOT "${rc}" STREQUAL "0" OR NOT EXISTS "${project}/libsqlite3-0.dll"
      OR NOT EXISTS "${project}/payload.dat" OR NOT EXISTS "${project}/conditional.dat"
      OR EXISTS "${project}/absent.dat")
    message(FATAL_ERROR "combined explicit and automatic dependencies failed, rc=${rc}\n${stdout}${stderr}")
  endif()
  execute_process(COMMAND "${_exe}" WORKING_DIRECTORY "${project}" RESULT_VARIABLE rc TIMEOUT 15)
  if(NOT "${rc}" STREQUAL "42")
    message(FATAL_ERROR "combined native executable returned ${rc}, expected 42")
  endif()
  file(SHA256 "${_exe}" _before)
  file(REMOVE "${dep_driver}/payload.dat")
  foreach(failure IN ITEMS library file owner cycle)
    set(args ${explicit_inputs})
    if(failure STREQUAL "library")
      set(args --link-input "explicit native.obj")
      set(expected "missing driver link library for 'driver_fixture'")
    elseif(failure STREQUAL "file")
      set(expected "missing driver bundle file.*payload.dat")
    elseif(failure STREQUAL "owner")
      file(WRITE "${dep_driver}/dep_fixture.bundle" "@driver/unregistered_fixture\n")
      set(expected "no registered owner for '@driver/unregistered_fixture'")
    else()
      file(WRITE "${dep_driver}/dep_fixture.bundle" "@driver/driver_fixture\n")
      set(expected "cyclic driver bundle reference")
    endif()
    execute_process(COMMAND ${compile} ${args} WORKING_DIRECTORY "${project}"
      RESULT_VARIABLE rc OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr TIMEOUT 60)
    file(SHA256 "${_exe}" _after)
    file(GLOB _staging "${project}/.zanc-*")
    if("${rc}" STREQUAL "0" OR NOT "${stdout}${stderr}" MATCHES "${expected}"
        OR NOT _before STREQUAL _after OR _staging)
      message(FATAL_ERROR "${failure}: expected '${expected}' and preserved output, rc=${rc}\n${stdout}${stderr}")
    endif()
  endforeach()
  file(REMOVE_RECURSE "${project}")
endif()
message(STATUS "selfhost driver option, warning and link contracts passed")
