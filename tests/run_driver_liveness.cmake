# Focused native compiler/publish regression. Reuses the emit_lib producer and
# the driver-packaging script's compile/run/file assertions; no GUI is started.
# cmake -DZANC=<build/zanc> -DWORK=<_scratch/driver_liveness>
#       [-DMODE=sharded|unsharded|static] -P tests/run_driver_liveness.cmake
cmake_policy(SET CMP0012 NEW)

if(NOT ZANC OR NOT WORK)
  message(FATAL_ERROR "run_driver_liveness: ZANC and WORK are required")
endif()
get_filename_component(_repo "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
get_filename_component(WORK "${WORK}" ABSOLUTE)
file(TO_CMAKE_PATH "${WORK}" WORK)
# The harness recreates only its own disposable directory.
if(NOT WORK MATCHES "/_scratch/[^/]+$")
  message(FATAL_ERROR "run_driver_liveness: WORK must be a direct child of _scratch")
endif()
if(MODE AND NOT MODE STREQUAL "sharded" AND NOT MODE STREQUAL "unsharded"
        AND NOT MODE STREQUAL "static")
  message(FATAL_ERROR "run_driver_liveness: MODE must be sharded, unsharded or static")
endif()

if(CMAKE_HOST_WIN32)
  set(_sub win-x64)
  set(_runtime zan_test_live.dll)
elseif(CMAKE_HOST_APPLE)
  set(_sub macos-x64)
  set(_runtime libzan_test_live.dylib)
else()
  set(_sub linux-x64)
  set(_runtime libzan_test_live.so)
endif()
# Match the native arch using CMake's host probe, including arm64 hosts.
cmake_host_system_information(RESULT _arch QUERY OS_PLATFORM)
if(_arch MATCHES "^(ARM64|arm64|aarch64)$")
  string(REPLACE "x64" "arm64" _sub "${_sub}")
endif()

file(REMOVE_RECURSE "${WORK}")
set(_project "${WORK}/project")
set(_live "${_project}/packages/Live/src/Fixture/Live/drivers/${_sub}")
set(_image "${_project}/packages/Image/src/System/Drawing/Imaging/drivers/${_sub}")
set(_gui "${_project}/packages/Gui/src/Gui/drivers/${_sub}")
file(MAKE_DIRECTORY "${_live}" "${_image}" "${_gui}")
file(WRITE "${_project}/zan.proj" "name = \"DriverLiveness\"\n")
foreach(_pkg Live Image Gui)
  file(WRITE "${_project}/packages/${_pkg}/zan.pkg"
    "name = \"${_pkg}\"\nversion = \"1.0.0\"\n")
endforeach()
# Keep the actual owners beyond both the old 24-root cap and the initial
# dynamic capacity. Discovery must enumerate all packages without importing
# any of their namespaces before it can resolve the driver closure.
foreach(_index RANGE 0 39)
  file(MAKE_DIRECTORY "${_project}/packages/A${_index}/src")
  file(WRITE "${_project}/packages/A${_index}/zan.pkg"
    "name = \"A${_index}\"\nversion = \"1.0.0\"\n")
endforeach()
file(WRITE "${_live}/../driver.manifest"
  "zan_test_live\nzan_test_dead\nzan_test_static_runtime\n")
file(WRITE "${_image}/../driver.manifest" "zan_test_image\n")
file(WRITE "${_gui}/../driver.manifest"
  "zan_test_gui\nzan_test_nested\nzan_test_runtime if PublishFeature_\nzan_test_dead_runtime if MissingFeature_\n")
# Two direct dependencies, like zan_game -> zan_image + zan_gui. Image's own
# bundle also reaches another Gui-owned driver conditionally, so recursive
# parsing and owner roots must work independently of the two direct edges.
# No using/import of these package namespaces is present.
file(WRITE "${_live}/zan_test_live.bundle"
  "# primary bundle\n${_runtime}\n@driver/zan_test_image\n@driver/zan_test_gui if PublishFeature_\nprimary-live.asset if PublishFeature_\nprimary-dead.asset if MissingFeature_\n")
file(WRITE "${_live}/zan_test_dead.bundle" "dead-extern.asset\n")
file(WRITE "${_image}/zan_test_image.bundle"
  "image.asset\ndepB.dll\nimage-live.asset if PublishFeature_\nimage-dead.asset if MissingFeature_\n@driver/zan_test_nested if PublishFeature_\n@driver/zan_test_dead_runtime if MissingFeature_\n")
file(WRITE "${_gui}/zan_test_gui.bundle"
  "gui.asset\ngui-live.asset if PublishFeature_\ngui-dead.asset if MissingFeature_\n")
file(WRITE "${_gui}/zan_test_nested.bundle"
  "nested.asset\nnested-live.asset if PublishFeature_\nnested-dead.asset if MissingFeature_\n")
# The full registry can contain more prefixes than irgen's 32-slot cache.
# A distinct live prefix encountered after those misses must remain available
# after LLVM release, including when its only definitions moved into shards.
foreach(_index RANGE 0 39)
  file(APPEND "${_live}/zan_test_live.bundle"
    "unused-${_index}.asset if MissingPrefix${_index}_\n")
endforeach()
file(APPEND "${_live}/zan_test_live.bundle"
  "primary-specific.asset if PublishFeature_First\n")
file(WRITE "${_gui}/zan_test_runtime.bundle" "runtime-live.asset\n")
file(WRITE "${_gui}/zan_test_dead_runtime.bundle" "runtime-dead.asset\n")
foreach(_file primary-live.asset primary-specific.asset primary-dead.asset dead-extern.asset)
  file(WRITE "${_live}/${_file}" "live-package:${_file}\n")
endforeach()
foreach(_file image.asset depB.dll image-live.asset image-dead.asset)
  file(WRITE "${_image}/${_file}" "image-package:${_file}\n")
endforeach()
foreach(_file gui.asset gui-live.asset gui-dead.asset nested.asset nested-live.asset
              nested-dead.asset runtime-live.asset runtime-dead.asset)
  file(WRITE "${_gui}/${_file}" "gui-package:${_file}\n")
endforeach()

# Use the existing Zan DLL/shared-library fixture instead of depending on a
# system C compiler. Its public EmitLib_Add must survive and be callable from
# an extracted shard; the dead library has no binary at all.
set(_common --auto-stdlib --stdlib-path "${_repo}/stdlib"
            --package-project "${_project}" --no-icon --no-fast-alloc
            --no-obfuscate-strings)
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env ZAN_SHARD=0 ZAN_NO_SHARD=1
          "${ZANC}" "${_repo}/tests/emit_lib/lib.zan" ${_common}
          --emit-lib -o "${_live}/${_runtime}"
  WORKING_DIRECTORY "${_project}"
  RESULT_VARIABLE _rc OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
if(NOT _rc EQUAL 0)
  message(FATAL_ERROR "run_driver_liveness: fixture library failed (${_rc})\n${_out}${_err}")
endif()
file(WRITE "${_project}/main.zan" [=[
using System;
class Native {
    [DllImport("zan_test_live")]
    public static extern int EmitLib_Add(int a, int b);
    [DllImport("zan_test_dead")]
    static extern int MissingDriver();
    static int Unused() { return MissingDriver(); }
}
// These short wrappers deliberately remain inline candidates at --publish's
// default -Os: condition snapshots must precede LLVM removing their names.
class PublishFeature {
    public static int First(int value) { return Native.EmitLib_Add(value, 1); }
    public static int Second(int value) { return Native.EmitLib_Add(value, 2); }
    public static int Third(int value) { return Native.EmitLib_Add(value, 3); }
}
class Program {
    static void Main() {
        Console.WriteLine(PublishFeature.First(2) + PublishFeature.Second(2)
                          + PublishFeature.Third(2));
    }
}
]=])

if(MODE STREQUAL "static")
  set(_modes)
elseif(MODE)
  set(_modes "${MODE}")
else()
  set(_modes unsharded sharded)
endif()
foreach(_mode IN LISTS _modes)
  set(_outdir "${WORK}/${_mode}")
  file(MAKE_DIRECTORY "${_outdir}")
  # Publishing into an existing directory must refresh an upgraded @driver
  # dependency. The owner-root hash check below must reject this stale DLL.
  file(WRITE "${_outdir}/depB.dll" "stale-dependency-version\n")
  if(_mode STREQUAL "sharded")
    set(_shard_env ZAN_SHARD=1 ZAN_NO_SHARD=0 ZAN_SHARD_MAX_FN=1 ZAN_SHARD_MAX_INSN=1)
  else()
    set(_shard_env ZAN_SHARD=0 ZAN_NO_SHARD=1)
  endif()
  execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env ${_shard_env}
            "ZAN_SHARD_DUMP=${_outdir}/last-shard.ll" ZAN_LINK_ECHO=1
            "${ZANC}" "${_project}/main.zan" ${_common} --publish
            --link-mode shared --subsystem console -o "${_outdir}/app.exe"
    WORKING_DIRECTORY "${_project}"
    RESULT_VARIABLE _rc OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
  if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "run_driver_liveness: ${_mode} publish failed (${_rc})\n${_out}${_err}")
  endif()
  # ZAN_LINK_ECHO is implemented by the native Windows executable link paths.
  # Other hosts prove the same property by linking the missing-dead-library
  # program and running its live imported call, plus the bundle assertions.
  if(CMAKE_HOST_WIN32)
    if("${_out}${_err}" MATCHES "-lzan_test_dead([ \r\n\"]|$)")
      message(FATAL_ERROR "run_driver_liveness: ${_mode} linked the dead extern library")
    endif()
    if(NOT "${_out}${_err}" MATCHES "-lzan_test_live([ \r\n\"]|$)")
      message(FATAL_ERROR "run_driver_liveness: ${_mode} lost the live extern library")
    endif()
  endif()
  if(_mode STREQUAL "sharded")
    if(NOT "${_out}${_err}" MATCHES "shard: [1-9][0-9]* objects emitted" OR
       NOT EXISTS "${_outdir}/last-shard.ll")
      message(FATAL_ERROR "run_driver_liveness: sharding fell back; regression was not exercised\n${_out}${_err}")
    endif()
    file(READ "${_outdir}/last-shard.ll" _shard_ir)
    if(NOT _shard_ir MATCHES "define[^\n]*@PublishFeature_" OR
       NOT _shard_ir MATCHES "call[^\n]*@EmitLib_Add")
      message(FATAL_ERROR "run_driver_liveness: extracted shard did not contain the live extern caller")
    endif()
  endif()
  foreach(_file dead-extern.asset primary-dead.asset image-dead.asset gui-dead.asset
                nested-dead.asset runtime-dead.asset)
    if(EXISTS "${_outdir}/${_file}")
      message(FATAL_ERROR "run_driver_liveness: ${_mode} published dead dependency ${_file}")
    endif()
  endforeach()
  foreach(_file "${_runtime}" primary-live.asset primary-specific.asset image.asset
                depB.dll image-live.asset gui.asset gui-live.asset nested.asset nested-live.asset
                runtime-live.asset)
    if(NOT EXISTS "${_outdir}/${_file}")
      message(FATAL_ERROR "run_driver_liveness: ${_mode} lost live dependency ${_file}\n${_out}${_err}")
    endif()
    if(_file MATCHES "^image" OR _file STREQUAL "depB.dll")
      set(_origin "${_image}")
    elseif(_file MATCHES "^(gui|nested|runtime)")
      set(_origin "${_gui}")
    else()
      set(_origin "${_live}")
    endif()
    file(SHA256 "${_origin}/${_file}" _source_hash)
    file(SHA256 "${_outdir}/${_file}" _published_hash)
    if(NOT _source_hash STREQUAL _published_hash)
      message(FATAL_ERROR "run_driver_liveness: ${_mode} did not publish ${_file} from its owning package root")
    endif()
  endforeach()
  execute_process(COMMAND "${_outdir}/app.exe"
    WORKING_DIRECTORY "${_outdir}"
    RESULT_VARIABLE _rc OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
  string(REPLACE "\r\n" "\n" _out "${_out}")
  if(NOT _rc EQUAL 0 OR NOT _out STREQUAL "12\n")
    message(FATAL_ERROR "run_driver_liveness: ${_mode} extern call failed (${_rc})\n${_out}${_err}")
  endif()
endforeach()

if(NOT MODE OR MODE STREQUAL "static")
  # Exercise the real static-driver .libs parser. Frameworks are target
  # dependencies, not -l basenames; the same manifest must be harmless on
  # Windows/Linux and provide Cocoa on macOS. Whitespace/comments use the
  # existing library-manifest grammar.
  file(MAKE_DIRECTORY "${_live}/static" "${WORK}/static")
  file(WRITE "${_live}/static/zan_test_live.libs"
    "# target-specific framework fixture\n  @framework/Cocoa # native macOS dependency\n@framework/CoreFoundation\n")
  set(_static_source "${_repo}/tests/emit_lib/lib.zan")
  if(CMAKE_HOST_APPLE)
    # NSPageSize belongs to Foundation (provided by Cocoa), not libc. A real
    # unresolved symbol ensures that merely ignoring @framework cannot pass.
    set(_static_source "${_project}/static_lib.zan")
    file(WRITE "${_static_source}" [=[
class EmitLib {
    [DllImport("crt")]
    static extern long NSPageSize();
    public static int Add(int a, int b) {
        if (NSPageSize() <= 0) return -100;
        return a + b;
    }
}
]=])
  endif()
  execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env ZAN_SHARD=0 ZAN_NO_SHARD=1
            "${ZANC}" "${_static_source}" ${_common} --emit-lib
            -o "${_live}/static/libzan_test_live.a"
    WORKING_DIRECTORY "${_project}"
    RESULT_VARIABLE _rc OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
  if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "run_driver_liveness: static fixture failed (${_rc})\n${_out}${_err}")
  endif()
  execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env ZAN_SHARD=0 ZAN_NO_SHARD=1 ZAN_LINK_ECHO=1
            "${ZANC}" "${_project}/main.zan" ${_common} --publish
            --link-mode static --subsystem console -o "${WORK}/static/app.exe"
    WORKING_DIRECTORY "${_project}"
    RESULT_VARIABLE _rc OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
  if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "run_driver_liveness: static framework publish failed (${_rc})\n${_out}${_err}")
  endif()
  if("${_out}${_err}" MATCHES "-l(@framework/|Cocoa|CoreFoundation)" OR
     "${_err}" MATCHES "ignoring unsafe entry '@framework/")
    message(FATAL_ERROR "run_driver_liveness: framework manifest was treated as library basenames\n${_out}${_err}")
  endif()
  if(NOT CMAKE_HOST_APPLE AND "${_out}${_err}" MATCHES "-framework[ \r\n]")
    message(FATAL_ERROR "run_driver_liveness: macOS framework flags leaked to a non-macOS target\n${_out}${_err}")
  endif()
  if(EXISTS "${WORK}/static/${_runtime}")
    message(FATAL_ERROR "run_driver_liveness: static test used the shared driver instead of the .libs fixture")
  endif()
  execute_process(COMMAND "${WORK}/static/app.exe"
    WORKING_DIRECTORY "${WORK}/static"
    RESULT_VARIABLE _rc OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
  string(REPLACE "\r\n" "\n" _out "${_out}")
  if(NOT _rc EQUAL 0 OR NOT _out STREQUAL "12\n")
    message(FATAL_ERROR "run_driver_liveness: static framework call failed (${_rc})\n${_out}${_err}")
  endif()

  # A library's private ARC helpers must remain callable, while a weak slot
  # registered inside it must be cleared when the application frees its target.
  # Both modules describe the same pointer ABI without exporting the app's class.
  set(_runtime_source "${_project}/static_runtime.zan")
  file(WRITE "${_runtime_source}" [=[
class RuntimeNode { public int Value; }
class RuntimeHolder { public weak RuntimeNode Target; }
class RuntimeProbe {
    static RuntimeHolder holder;
    public static int Exercise(int value) {
        RuntimeNode node = new RuntimeNode();
        node.Value = value;
        int[] values = new int[2];
        values[0] = node.Value;
        values[1] = 1;
        return values[0] + values[1];
    }
    public static void Remember(RuntimeNode target) {
        if (holder == null) holder = new RuntimeHolder();
        holder.Target = target;
    }
    public static int Alive() { return holder.Target == null ? 0 : 1; }
}
]=])
  file(WRITE "${_project}/static_runtime_main.zan" [=[
using System;
class RuntimeNode { public int Value; }
class RuntimeNative {
    [DllImport("zan_test_live")]
    public static extern int EmitLib_Add(int a, int b);
    [DllImport("zan_test_static_runtime")]
    public static extern int RuntimeProbe_Exercise(int value);
    [DllImport("zan_test_static_runtime")]
    public static extern void RuntimeProbe_Remember(RuntimeNode target);
    [DllImport("zan_test_static_runtime")]
    public static extern int RuntimeProbe_Alive();
}
class Program {
    static int RegisterTarget() {
        RuntimeNode node = new RuntimeNode();
        node.Value = 4;
        RuntimeNative.RuntimeProbe_Remember(node);
        return RuntimeNative.RuntimeProbe_Alive();
    }
    static void Main() {
        Console.WriteLine(RuntimeNative.EmitLib_Add(3, 4));
        Console.WriteLine(RuntimeNative.RuntimeProbe_Exercise(8));
        Console.WriteLine(RegisterTarget());
        Console.WriteLine(RuntimeNative.RuntimeProbe_Alive());
    }
}
]=])
  execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env ZAN_SHARD=0 ZAN_NO_SHARD=1
            "${ZANC}" "${_runtime_source}" ${_common} --emit-lib
            -o "${_live}/static/libzan_test_static_runtime.a"
    WORKING_DIRECTORY "${_project}"
    RESULT_VARIABLE _rc OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
  if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "run_driver_liveness: static runtime fixture failed (${_rc})\n${_out}${_err}")
  endif()
  execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env ZAN_SHARD=0 ZAN_NO_SHARD=1
            "${ZANC}" "${_project}/static_runtime_main.zan" ${_common} --publish
            --link-mode static --subsystem console -o "${WORK}/static/runtime.exe"
    WORKING_DIRECTORY "${_project}"
    RESULT_VARIABLE _rc OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
  if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "run_driver_liveness: static runtime link failed (${_rc})\n${_out}${_err}")
  endif()
  execute_process(COMMAND "${WORK}/static/runtime.exe"
    WORKING_DIRECTORY "${WORK}/static"
    RESULT_VARIABLE _rc OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
  string(REPLACE "\r\n" "\n" _out "${_out}")
  if(NOT _rc EQUAL 0 OR NOT _out STREQUAL "7\n9\n1\n0\n")
    message(FATAL_ERROR "run_driver_liveness: static runtime ownership failed (${_rc})\n${_out}${_err}")
  endif()

  # Both genuine archives are needed above. Giving the second archive the same
  # public user export must still fail, rather than silently coalescing user code.
  file(APPEND "${_runtime_source}" [=[
class EmitLib {
    public static int Add(int a, int b) { return a + b + 100; }
}
]=])
  execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env ZAN_SHARD=0 ZAN_NO_SHARD=1
            "${ZANC}" "${_runtime_source}" ${_common} --emit-lib
            -o "${_live}/static/libzan_test_static_runtime.a"
    WORKING_DIRECTORY "${_project}"
    RESULT_VARIABLE _rc OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
  if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "run_driver_liveness: duplicate-export fixture failed (${_rc})\n${_out}${_err}")
  endif()
  execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env ZAN_SHARD=0 ZAN_NO_SHARD=1
            "${ZANC}" "${_project}/static_runtime_main.zan" ${_common} --publish
            --link-mode static --subsystem console -o "${WORK}/static/duplicate.exe"
    WORKING_DIRECTORY "${_project}"
    RESULT_VARIABLE _rc OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
  if(_rc EQUAL 0 OR NOT "${_out}${_err}" MATCHES "EmitLib_Add" OR
     NOT "${_out}${_err}" MATCHES "(duplicate symbol|multiple definition|already defined)")
    message(FATAL_ERROR "run_driver_liveness: duplicate public export was not diagnosed (${_rc})\n${_out}${_err}")
  endif()
endif()
message(STATUS "run_driver_liveness: driver liveness, owner roots, refreshed dependencies, static runtime ownership and target-specific manifests preserved")
