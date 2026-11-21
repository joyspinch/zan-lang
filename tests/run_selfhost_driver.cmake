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
reject_cli(subsystem_missing "option '--subsystem' requires a value" --subsystem)
reject_cli(subsystem_invalid "invalid PE subsystem 'invalid'" --subsystem invalid)
reject_cli(driver_dir_missing "option '--driver-dir' requires a value" --driver-dir)
reject_cli(driver_dir_absent "driver directory does not exist" --driver-dir "${OUTDIR}/absent drivers")
reject_cli(icon_missing "option '--icon' requires a value" --icon)
if(WIN32)
  reject_cli(icon_ir "--icon requires Windows executable output" --icon "${_root}/templates/game/snake/assets/icon.ico")
endif()
reject_cli(target_flag_value "option '--target' requires a value" --target --auto-stdlib)
reject_cli(target_cross "target 'wasm32-wasi' is not supported" --target wasm32-wasi)
reject_cli(target_invalid "target 'batch4-invalid' is not supported" --target batch4-invalid)
reject_cli(embed_missing "option '--embed' requires a value" --embed)
foreach(flag IN ITEMS -c -S --emit-ir)
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
  if(mode STREQUAL "deny")
    list(APPEND _args "@${_root}/tests/selfhost/driver_response_options.rsp")
  elseif(mode STREQUAL "clean_deny" OR mode STREQUAL "initializer_clean_deny")
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

set(_response "${OUTDIR}/outer options.rsp")
set(_nested_response "${OUTDIR}/inner options.rsp")
set(_response_output "${OUTDIR}/response output.ll")
set(_overridden_output "${OUTDIR}/response overridden.ll")
string(ASCII 239 187 191 _bom)
file(WRITE "${_response}" "${_bom}# compiler options\n--target host \"@${_nested_response}\"\n")
file(WRITE "${_nested_response}" "-o \"${_overridden_output}\" \"${_clean}\"\n")
file(REMOVE "${_response_output}" "${_overridden_output}")
execute_process(COMMAND "${GEN1}" "@${_response}" -o "${_response_output}"
  WORKING_DIRECTORY "${_root}" RESULT_VARIABLE rc
  OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr TIMEOUT 45)
if(NOT "${rc}" STREQUAL "0" OR NOT EXISTS "${_response_output}" OR EXISTS "${_overridden_output}")
  message(FATAL_ERROR "nested response: expected only the last output path, rc=${rc}\n${stdout}${stderr}")
endif()

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
  get_filename_component(_gen1_dir "${GEN1}" DIRECTORY)
  set(_gen1_env "${CMAKE_COMMAND}" -E env "ZAN_CLANG=${CLANG}")
  if(arch STREQUAL "x64" AND EXISTS "${_gen1_dir}/mingw/lib/crt2.o")
    set(_gen1_env "${CMAKE_COMMAND}" -E env --unset=ZAN_CLANG --unset=ZAN_LLVM_ROOT
      --unset=LLVM_ROOT --unset=LLVM_HOME --unset=USERPROFILE --unset=ProgramFiles
      --unset=ProgramW6432 "PATH=$ENV{SystemRoot}/System32")
    set(_bundled_link TRUE)
  endif()
  set(icon_args)
  set(expected_exit 73)
  if(_bundled_link OR triple MATCHES "-msvc")
    list(APPEND icon_args --icon "${_root}/templates/game/snake/assets/icon.ico")
    set(expected_exit 74)
  endif()
  set(stdlib "${OUTDIR}/native-stdlib")
  set(stdlib_driver "${stdlib}/Fixture/Nested/drivers")
  set(driver_override "${stdlib}/override drivers")
  file(MAKE_DIRECTORY "${stdlib_driver}" "${driver_override}")
  file(WRITE "${stdlib_driver}/driver.manifest" "stdlib_fixture if main\n")
  file(WRITE "${driver_override}/stdlib_fixture.bundle" "stdlib-payload.dat\n")
  file(WRITE "${driver_override}/stdlib-payload.dat" "standard library driver payload")
  file(WRITE "${_default_src}" [=[
namespace DriverFixture {
    class Options {
        public static int Value = 4;
        public static int Read() { return Value; }
    }
}

class Shadow {
    public int Value = 1;
    public int Read() { return Value; }
}

class Program {
    Shadow Options = new Shadow();
    int ReadOptions() {
        DriverFixture.Options.Value = 37;
        return DriverFixture.Options.Read() + DriverFixture.Options.Value - Options.Read();
    }

    [DllImport("kernel32")]
    static extern nint GetCurrentProcess();
    [DllImport("psapi")]
    static extern int GetProcessMemoryInfo(nint process, byte[] counters, int size);
    [DllImport("kernel32")]
    static extern nint FindResourceW(nint module, nint name, nint type);
    static int Main() {
        byte[] counters = new byte[72];
        if (GetProcessMemoryInfo(GetCurrentProcess(), counters, counters.Length) == 0) { return 1; }
        bool group = FindResourceW(0, 1, 14) != 0;
        bool image = FindResourceW(0, 1, 3) != 0;
        if (group != image) { return 2; }
        return new Program().ReadOptions() + (group ? 1 : 0);
    }
}
]=])
  file(REMOVE "${_default_exe}" "${OUTDIR}/stdlib-payload.dat")
  execute_process(COMMAND ${_gen1_env} "ZAN_STDLIB_PATH=${stdlib}"
    "${GEN1}" "${_default_src}" --time --subsystem windows --driver-dir "${driver_override}" ${icon_args}
    WORKING_DIRECTORY "${_root}" RESULT_VARIABLE rc OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr TIMEOUT 45)
  string(FIND "${stderr}" "[time] IR generation:" timing)
  if(NOT "${rc}" STREQUAL "0" OR NOT EXISTS "${_default_exe}" OR NOT EXISTS "${OUTDIR}/stdlib-payload.dat"
      OR timing EQUAL -1)
    message(FATAL_ERROR "default executable output or stdlib bundle override failed, rc=${rc}\n${stdout}${stderr}")
  endif()
  # e_lfanew points to the PE signature. Subsystem is 68 bytes into the optional header.
  file(READ "${_default_exe}" pe_le OFFSET 60 LIMIT 4 HEX)
  string(REGEX REPLACE "^(..)(..)(..)(..)$" "\\4\\3\\2\\1" pe_be "${pe_le}")
  math(EXPR subsystem_offset "0x${pe_be} + 24 + 68")
  file(READ "${_default_exe}" subsystem OFFSET ${subsystem_offset} LIMIT 2 HEX)
  if(NOT subsystem STREQUAL "0200")
    message(FATAL_ERROR "expected Windows GUI subsystem, found '${subsystem}'")
  endif()
  execute_process(COMMAND "${_default_exe}" RESULT_VARIABLE rc TIMEOUT 15)
  if(NOT "${rc}" STREQUAL "${expected_exit}")
    message(FATAL_ERROR "default executable returned ${rc}, expected ${expected_exit}")
  endif()
  set(bad_icon "${OUTDIR}/invalid icon.ico")
  file(WRITE "${bad_icon}" "not an ICO file")
  file(SHA256 "${_default_exe}" before_bad_icon)
  execute_process(COMMAND ${_gen1_env} "ZAN_STDLIB_PATH=${stdlib}"
    "${GEN1}" "${_default_src}" --icon "${bad_icon}"
    WORKING_DIRECTORY "${_root}" RESULT_VARIABLE rc OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr TIMEOUT 45)
  file(SHA256 "${_default_exe}" after_bad_icon)
  if("${rc}" STREQUAL "0" OR NOT "${stdout}${stderr}" MATCHES "invalid ICO file"
      OR NOT before_bad_icon STREQUAL after_bad_icon)
    message(FATAL_ERROR "invalid icon must preserve the previous executable, rc=${rc}\n${stdout}${stderr}")
  endif()
  file(REMOVE "${bad_icon}" "${_default_src}" "${_default_exe}" "${OUTDIR}/stdlib-payload.dat")
  file(REMOVE_RECURSE "${stdlib}")

  set(_partial_main "${OUTDIR}/partial-main.zan")
  set(_partial_contract "${OUTDIR}/partial-contract.zan")
  set(_partial_base "${OUTDIR}/partial-base.zan")
  set(_partial_exe "${OUTDIR}/partial-main.exe")
  file(WRITE "${_partial_main}" [=[
namespace Other {
    public interface IAttach<T> { int Attach(T value); }
}
namespace Fixture {
    public partial class Host {
        public int Start() { return 7; }
        public int Attach(int value) { return value + BaseValue(); }
    }
    class Program {
        static int Main() {
            Host host = new Host();
            Contracts.IStart start = host;
            Contracts.IAttach<int> attach = host;
            return start.Start() == 7 && attach.Attach(10) == 42 ? 0 : 1;
        }
    }
}
]=])
  file(WRITE "${_partial_contract}" [=[
using Contracts;
namespace Contracts {
    public interface IStart { int Start(); }
    public interface IAttach<T> { int Attach(T value); }
}
namespace Fixture {
    public partial class Host : IStart, IAttach<int> { }
}
]=])
  file(WRITE "${_partial_base}" [=[
namespace Fixture {
    public class Base { public int BaseValue() { return 32; } }
    public partial class Host : Base, Contracts.IAttach<int> { }
}
]=])
  execute_process(COMMAND ${_gen1_env} "${GEN1}" "${_partial_main}" "${_partial_contract}"
    "${_partial_base}" -O0 -o "${_partial_exe}" WORKING_DIRECTORY "${_root}"
    RESULT_VARIABLE rc OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr TIMEOUT 60)
  if(NOT "${rc}" STREQUAL "0")
    message(FATAL_ERROR "partial interface fixture failed to compile, rc=${rc}\n${stdout}${stderr}")
  endif()
  execute_process(COMMAND "${_partial_exe}" RESULT_VARIABLE rc TIMEOUT 15)
  if(NOT "${rc}" STREQUAL "0")
    message(FATAL_ERROR "partial interface dispatch failed, rc=${rc}")
  endif()
  file(APPEND "${_partial_base}" [=[
namespace Fixture {
    public class Incompatible { }
    public partial class Host : Incompatible { }
}
]=])
  execute_process(COMMAND ${_gen1_env} "${GEN1}" "${_partial_main}" "${_partial_contract}"
    "${_partial_base}" -o "${OUTDIR}/partial-invalid.ll" WORKING_DIRECTORY "${_root}"
    RESULT_VARIABLE rc OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr TIMEOUT 60)
  if("${rc}" STREQUAL "0" OR NOT "${stdout}${stderr}" MATCHES "has conflicting base classes")
    message(FATAL_ERROR "expected conflicting partial base diagnosis, rc=${rc}\n${stdout}${stderr}")
  endif()
  file(REMOVE "${_partial_main}" "${_partial_contract}" "${_partial_base}" "${_partial_exe}")

  set(_sqlite_case "${_root}/tests/conformance/sqlite_params")
  set(_sqlite_out "${OUTDIR}/sqlite-default")
  file(MAKE_DIRECTORY "${_sqlite_out}")
  set(_sqlite_exe "${_sqlite_out}/sqlite_params.exe")
  set(_sqlite_dll "${_sqlite_out}/libsqlite3-0.dll")
  file(REMOVE "${_sqlite_exe}" "${_sqlite_dll}")
  execute_process(COMMAND ${_gen1_env} "${GEN1}" "${_sqlite_case}.zan" -o "${_sqlite_exe}" WORKING_DIRECTORY "${_root}"
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

  if(_bundled_link)
    set(_arc "${OUTDIR}/arc-diagnostics")
    file(MAKE_DIRECTORY "${_arc}")
    file(WRITE "${_arc}/main.zan" [=[
using System;
class Link {
    public Link Next;
}
class Program {
    [DllImport("arc_fixture")]
    static extern void ArcReleaseTwice();

    static Link Root = new Link();
    static int Main(string[] args) {
        if (args.Length != 0 && args[0] == "guard") {
            ArcReleaseTwice();
            return 0;
        }
        if (args.Length != 0) {
            Link cycle = new Link();
            cycle.Next = cycle;
        }
        return Root == null ? 1 : 0;
    }
}
]=])
    file(WRITE "${_arc}/fault.c" [=[
__declspec(dllimport) unsigned int SetErrorMode(unsigned int mode);
extern void *zalloc(long long size, void *dtor, void *class_id);
extern void zrelease(void *object);

void ArcReleaseTwice(void) {
    SetErrorMode(2); /* SEM_NOGPFAULTERRORBOX */
    void *object = zalloc(8, 0, 0);
    zrelease(object);
    zrelease(object);
}
]=])
    execute_process(COMMAND "${CLANG}" -c "${_arc}/fault.c" -o "${_arc}/fault.obj"
      RESULT_VARIABLE rc OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr TIMEOUT 45)
    if(NOT "${rc}" STREQUAL "0")
      message(FATAL_ERROR "ARC fault fixture compilation failed, rc=${rc}\n${stdout}${stderr}")
    endif()
    file(REMOVE "${_arc}/zan_leaks.log")
    foreach(mode IN ITEMS strict disabled)
      set(_arc_flags --arc-guard --check-leaks)
      if(mode STREQUAL "disabled")
        list(APPEND _arc_flags --no-arc-guard --no-check-leaks)
      endif()
      execute_process(COMMAND ${_gen1_env} "${GEN1}" "${_arc}/main.zan"
        -g --strict-runtime ${_arc_flags} --link-input "${_arc}/fault.obj" -o "${_arc}/app.exe"
        WORKING_DIRECTORY "${_arc}" RESULT_VARIABLE rc
        OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr TIMEOUT 120)
      if(NOT "${rc}" STREQUAL "0")
        message(FATAL_ERROR "ARC ${mode} compilation failed, rc=${rc}\n${stdout}${stderr}")
      endif()
      execute_process(COMMAND "${_arc}/app.exe" WORKING_DIRECTORY "${_arc}"
        RESULT_VARIABLE rc OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr TIMEOUT 15)
      if(NOT "${rc}" STREQUAL "0" OR stdout MATCHES "memory leak detected" OR EXISTS "${_arc}/zan_leaks.log")
        message(FATAL_ERROR "ARC ${mode} clean exit failed, rc=${rc}\n${stdout}${stderr}")
      endif()
      execute_process(COMMAND "${_arc}/app.exe" cycle WORKING_DIRECTORY "${_arc}"
        RESULT_VARIABLE rc OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr TIMEOUT 15)
      if(NOT "${rc}" STREQUAL "0")
        message(FATAL_ERROR "ARC ${mode} cycle execution failed, rc=${rc}\n${stdout}${stderr}")
      endif()
      if(mode STREQUAL "strict")
        if(NOT stdout MATCHES "memory leak detected: 1 object" OR NOT EXISTS "${_arc}/zan_leaks.log")
          message(FATAL_ERROR "ARC leak report missing or incorrect\n${stdout}${stderr}")
        endif()
        file(REMOVE "${_arc}/zan_leaks.log")
        execute_process(COMMAND "${_arc}/app.exe" guard WORKING_DIRECTORY "${_arc}"
          RESULT_VARIABLE rc OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr TIMEOUT 15)
        if("${rc}" STREQUAL "0" OR NOT stderr MATCHES "ARC guard: release of an already-freed object")
          message(FATAL_ERROR "ARC guard failed to diagnose a duplicate release, rc=${rc}\n${stdout}${stderr}")
        endif()
      elseif(stdout MATCHES "memory leak detected" OR EXISTS "${_arc}/zan_leaks.log")
        message(FATAL_ERROR "ARC leak report ignored --no-check-leaks\n${stdout}${stderr}")
      endif()
    endforeach()
    file(REMOVE_RECURSE "${_arc}")

    include("${CMAKE_CURRENT_LIST_DIR}/run_selfhost_package_symbols.cmake")

    set(_lib "${OUTDIR}/native-library")
    file(MAKE_DIRECTORY "${_lib}")
    set(_lib_src "${_lib}/library.zan")
    file(WRITE "${_lib_src}" [=[
class EmitLib {
    static int Offset = Init();
    private static int Init() { return 1; }
    public static int Add(int a, int b) { return a + b + Offset; }
}
]=])
    set(_consumer "${_root}/tests/emit_lib/consumer.zan")
    foreach(ext IN ITEMS dll lib a)
      set(_artifact "${_lib}/emitlib_test.${ext}")
      execute_process(COMMAND ${_gen1_env} "${GEN1}" "${_lib_src}" --publish -o "${_artifact}"
        RESULT_VARIABLE rc OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr TIMEOUT 120)
      if(NOT "${rc}" STREQUAL "0")
        message(FATAL_ERROR "Native ${ext} compilation failed, rc=${rc}\n${stdout}${stderr}")
      endif()
      if(ext STREQUAL "dll")
        file(READ "${_artifact}" _magic LIMIT 2 HEX)
        if(NOT _magic STREQUAL "4d5a" OR NOT EXISTS "${_lib}/libemitlib_test.dll.a")
          message(FATAL_ERROR "Missing Windows DLL or import library")
        endif()
        set(_consume_args "-L${_lib}")
      else()
        file(READ "${_artifact}" _magic LIMIT 8 HEX)
        if(NOT _magic STREQUAL "213c617263683e0a")
          message(FATAL_ERROR "Invalid ${ext} archive format: ${_magic}")
        endif()
        set(_consume_args --link-input "${_artifact}")
      endif()
      execute_process(COMMAND ${_gen1_env} "${GEN1}" "${_consumer}" ${_consume_args} -o "${_lib}/consumer.exe"
        RESULT_VARIABLE rc OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr TIMEOUT 120)
      if(NOT "${rc}" STREQUAL "0")
        message(FATAL_ERROR "Native ${ext} consumer compilation failed, rc=${rc}\n${stdout}${stderr}")
      endif()
      execute_process(COMMAND "${_lib}/consumer.exe" WORKING_DIRECTORY "${_lib}"
        RESULT_VARIABLE rc OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr OUTPUT_STRIP_TRAILING_WHITESPACE TIMEOUT 15)
      if(NOT "${rc}" STREQUAL "0" OR NOT stdout STREQUAL "8")
        message(FATAL_ERROR "Native ${ext} consumer or static initialization failed, rc=${rc}\n${stdout}\n${stderr}")
      endif()
      if(ext STREQUAL "dll")
        file(REMOVE "${_artifact}")
      endif()
    endforeach()
    file(MAKE_DIRECTORY "${_lib}/blocked.dll")
    file(WRITE "${_lib}/libblocked.dll.a" "previous import library")
    execute_process(COMMAND ${_gen1_env} "${GEN1}" "${_lib_src}" -o "${_lib}/blocked.dll"
      RESULT_VARIABLE rc OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr TIMEOUT 120)
    file(READ "${_lib}/libblocked.dll.a" _previous_import)
    file(GLOB _library_temps "${_lib}/.zanc-*" "${_lib}/*.lock")
    if("${rc}" STREQUAL "0" OR NOT "${stdout}${stderr}" MATCHES "cannot publish DLL"
        OR NOT _previous_import STREQUAL "previous import library" OR _library_temps)
      message(FATAL_ERROR "Failed DLL publication did not restore its import library, rc=${rc}\n${stdout}${stderr}")
    endif()
    file(REMOVE_RECURSE "${_lib}")

    set(_project "${OUTDIR}/publish-resources")
    set(_source "${_project}/main.zan")
    set(_release "${_project}/app.exe")
    file(REMOVE_RECURSE "${_project}")
    file(MAKE_DIRECTORY "${_project}/input/pack" "${_project}/run")
    file(WRITE "${_project}/input/pack/note.txt" "res-ok")
    file(WRITE "${_project}/input/pack/empty.txt" "")
    file(WRITE "${_source}" [=[
using System.IO;
class Program {
    static string AtStartup = File.ReadAllText("virtual/pack/note.txt");
    static int Main() {
#if PUBLISH && RELEASE && !DEBUG
        if (AtStartup != "res-ok" || File.GetSize("virtual/pack/empty.txt") != 0) { return 1; }
        byte[] items = File.ReadAllBytes("virtual/pack/note.txt");
        if (items.Length != 6 || items[5] != 'k') { return 2; }
        return Directory.GetFiles("virtual/pack").Count == 2 ? 0 : 3;
#else
        return 4;
#endif
    }
}
]=])
    execute_process(COMMAND ${_gen1_env} "${GEN1}" "${_source}" --publish --no-runtime-checks
      --embed "${_project}/input=virtual" -o "${_release}" WORKING_DIRECTORY "${_root}"
      RESULT_VARIABLE rc OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr TIMEOUT 120)
    if(NOT "${rc}" STREQUAL "0" OR NOT EXISTS "${_release}")
      message(FATAL_ERROR "embedded release compilation failed, rc=${rc}\n${stdout}${stderr}")
    endif()
    execute_process(COMMAND "${_release}" WORKING_DIRECTORY "${_project}/run"
      RESULT_VARIABLE rc OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr TIMEOUT 15)
    if(NOT "${rc}" STREQUAL "0")
      message(FATAL_ERROR "embedded release failed, rc=${rc}\n${stdout}${stderr}")
    endif()
    file(READ "${_release}" pe_le OFFSET 60 LIMIT 4 HEX)
    string(REGEX REPLACE "^(..)(..)(..)(..)$" "\\4\\3\\2\\1" pe_be "${pe_le}")
    math(EXPR symbol_table_offset "0x${pe_be} + 12")
    file(READ "${_release}" symbols OFFSET ${symbol_table_offset} LIMIT 8 HEX)
    if(NOT symbols STREQUAL "0000000000000000")
      message(FATAL_ERROR "published executable retains a COFF symbol table")
    endif()
    file(REMOVE_RECURSE "${_project}")

    set(_ir "${OUTDIR}/module input.ll")
    set(_obj "${OUTDIR}/module output.obj")
    set(_backend ${_gen1_env} "${ZANC}" --compile-ir "${_ir}"
      --target x86_64-w64-windows-gnu -o "${_obj}")
    file(WRITE "${_ir}" "define i32 @probe() { ret i32 42 }\n")
    execute_process(COMMAND ${_backend} RESULT_VARIABLE rc
      OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr TIMEOUT 15)
    if(NOT "${rc}" STREQUAL "0" OR NOT EXISTS "${_obj}")
      message(FATAL_ERROR "IR object compilation failed, rc=${rc}\n${stdout}${stderr}")
    endif()
    file(SHA256 "${_obj}" _before)
    file(WRITE "${_ir}" "invalid IR\n")
    execute_process(COMMAND ${_backend} RESULT_VARIABLE rc
      OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr TIMEOUT 15)
    file(SHA256 "${_obj}" _after)
    file(GLOB _staging "${OUTDIR}/.zanc-*")
    file(REMOVE "${_ir}" "${_obj}")
    if("${rc}" STREQUAL "0" OR NOT _before STREQUAL _after OR _staging)
      message(FATAL_ERROR "invalid IR altered the previous object or retained staging files")
    endif()

    foreach(_case IN ITEMS async_throw exception_threads)
      set(_exe "${OUTDIR}/${_case}.exe")
      execute_process(COMMAND ${_gen1_env} "${GEN1}" "${_root}/tests/conformance/${_case}.zan"
        -Oz -o "${_exe}" WORKING_DIRECTORY "${_root}"
        RESULT_VARIABLE rc OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr TIMEOUT 60)
      if(NOT "${rc}" STREQUAL "0")
        message(FATAL_ERROR "bundled ${_case}: compilation failed, rc=${rc}\n${stdout}${stderr}")
      endif()
      execute_process(COMMAND "${_exe}" RESULT_VARIABLE rc OUTPUT_VARIABLE stdout
        ERROR_VARIABLE stderr TIMEOUT 15)
      file(REMOVE "${_exe}")
      file(READ "${_root}/tests/conformance/${_case}.out" expected)
      string(STRIP "${expected}" expected)
      string(STRIP "${stdout}" actual)
      if(NOT "${rc}" STREQUAL "0" OR NOT actual STREQUAL expected)
        message(FATAL_ERROR "bundled ${_case}: execution failed, rc=${rc}\n${stdout}${stderr}")
      endif()
    endforeach()
  endif()

  set(_exe "${OUTDIR}/driver output.exe")
  set(_native "${_root}/tests/selfhost/driver_native.zan")
  set(_obj "${OUTDIR}/driver native.obj")
  execute_process(COMMAND "${CLANG}" -c "${_root}/tests/selfhost/driver_native.c" -o "${_obj}"
    RESULT_VARIABLE rc OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr TIMEOUT 45)
  if(NOT "${rc}" STREQUAL "0")
    message(FATAL_ERROR "native fixture compilation failed, rc=${rc}\n${stdout}${stderr}")
  endif()
  execute_process(COMMAND ${_gen1_env} "${GEN1}" -o "${_exe}" "${_native}" --auto-stdlib -O0 --link-input "driver native.obj"
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
  execute_process(COMMAND ${_gen1_env} "${GEN1}" -o "${_exe}" "${_native}" --auto-stdlib -O0
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
  set(compile ${_gen1_env} "${GEN1}"
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
