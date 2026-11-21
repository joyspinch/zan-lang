# Included by run_selfhost_driver.cmake for the bundled Windows compiler.
set(_pkg "${OUTDIR}/package-and-symbols")
set(_pkg_source "${_pkg}/source")
set(_pkg_project "${_pkg}/project")
set(_pkg_env ${_gen1_env} "LOCALAPPDATA=${_pkg}/local")
file(REMOVE_RECURSE "${_pkg}")
file(MAKE_DIRECTORY "${_pkg_source}/src/arbitrary" "${_pkg_source}/wwwroot" "${_pkg_project}")
file(WRITE "${_pkg_source}/zan.pkg" "name = \"Fixture.Local\"\nversion = \"1.2\"\nplugin_id = \"test-plugin\"\n")
file(WRITE "${_pkg_source}/src/arbitrary/Value.zan" [=[
namespace Fixture.Local;
public class PackageValue {
    public static int Read() { return 29; }
}
]=])
file(WRITE "${_pkg_source}/wwwroot/resource.txt" "package resource")
file(WRITE "${_pkg_project}/main.zan" [=[
using Fixture.Local;
using Fixture;
using Project.Local;
namespace Project.Local;
class Program {
    static int Main() { return PackageValue.Read() - 29; }
}
]=])

execute_process(COMMAND ${_pkg_env} "${GEN1}" "${_pkg_project}/main.zan" --package-list-missing
  -o "${_pkg_project}/scan.exe" WORKING_DIRECTORY "${_pkg_project}" RESULT_VARIABLE rc
  OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr TIMEOUT 30)
if(NOT "${rc}" STREQUAL "3" OR NOT stderr MATCHES "ZANPKG_MISSING namespace=Fixture/Local\n"
    OR NOT stderr MATCHES "ZANPKG_MISSING namespace=Fixture\n" OR stderr MATCHES "namespace=Project"
    OR NOT stderr MATCHES "ZANPKG_STATUS action=list-missing status=ok count=2" OR EXISTS "${_pkg_project}/scan.exe")
  message(FATAL_ERROR "Missing-package scan returned unexpected results, rc=${rc}\n${stdout}${stderr}")
endif()
execute_process(COMMAND ${_pkg_env} "${GEN1}" "${_pkg_project}/main.zan" -o "${_pkg_project}/missing.exe"
  WORKING_DIRECTORY "${_pkg_project}" RESULT_VARIABLE rc OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr TIMEOUT 30)
if(NOT "${rc}" STREQUAL "3" OR NOT stderr MATCHES "ZANPKG_MISSING namespace=Fixture/Local\n"
    OR EXISTS "${_pkg_project}/missing.exe")
  message(FATAL_ERROR "Compile did not report the missing package, rc=${rc}\n${stdout}${stderr}")
endif()

set(_project_install ${_pkg_env} "${GEN1}" --package-install "${_pkg_source}" --package-name Fixture.Local)
file(WRITE "${_pkg_source}/zan.pkg" "name = Fixture.Local\n")
execute_process(COMMAND ${_project_install} WORKING_DIRECTORY "${_pkg_project}" RESULT_VARIABLE rc
  OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr TIMEOUT 30)
if(NOT "${rc}" STREQUAL "1" OR NOT stderr MATCHES "status=missing_version package=Fixture.Local"
    OR EXISTS "${_pkg_project}/.zan-packages/Fixture.Local")
  message(FATAL_ERROR "Incomplete manifest was accepted, rc=${rc}\n${stdout}${stderr}")
endif()
file(WRITE "${_pkg_source}/zan.pkg" "name = \"Fixture.Local\"\nversion = \"1.2\"\nplugin_id = \"test-plugin\"\n")

file(TO_NATIVE_PATH "${_pkg_source}/wwwroot/linked" _link_path)
file(TO_NATIVE_PATH "${_pkg_project}" _link_target)
execute_process(COMMAND cmd.exe /d /c mklink /j "${_link_path}" "${_link_target}"
  RESULT_VARIABLE rc OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr TIMEOUT 15)
if(NOT "${rc}" STREQUAL "0")
  message(FATAL_ERROR "Unable to create package reparse fixture, rc=${rc}\n${stdout}${stderr}")
endif()
execute_process(COMMAND ${_project_install} WORKING_DIRECTORY "${_pkg_project}" RESULT_VARIABLE rc
  OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr TIMEOUT 30)
execute_process(COMMAND cmd.exe /d /c rmdir "${_link_path}" RESULT_VARIABLE remove_rc TIMEOUT 15)
file(GLOB _leftovers "${_pkg_project}/.zan-packages/*")
if(NOT "${rc}" STREQUAL "1" OR NOT stderr MATCHES "status=unsafe_or_io package=Fixture.Local"
    OR NOT "${remove_rc}" STREQUAL "0" OR _leftovers OR NOT EXISTS "${_pkg_project}/main.zan")
  message(FATAL_ERROR "Package with a directory junction was not rejected cleanly, rc=${rc}\n${stdout}${stderr}")
endif()

foreach(scope IN ITEMS project global)
  if(scope STREQUAL "project")
    set(_scope_args)
    set(_installed "${_pkg_project}/.zan-packages/Fixture.Local")
  else()
    set(_scope_args --package-scope global)
    set(_installed "${_pkg}/local/Zan/packages/Fixture.Local")
    file(WRITE "${_pkg_source}/zan.pkg" [=[{"name":"Fixture.Local","version":"1.2.3","plugin_id":"test-plugin"}]=])
  endif()
  set(_install ${_pkg_env} "${GEN1}" --package-install "${_pkg_source}" --package-name Fixture.Local ${_scope_args})
  execute_process(COMMAND ${_install} WORKING_DIRECTORY "${_pkg_project}" RESULT_VARIABLE rc
    OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr TIMEOUT 30)
  if(NOT "${rc}" STREQUAL "0" OR NOT stderr MATCHES "ZANPKG_STATUS action=install status=installed package=Fixture.Local scope=${scope}"
      OR NOT stderr MATCHES "ZANPKG_USAGE plugin_id=test-plugin package=Fixture.Local scope=${scope}")
    message(FATAL_ERROR "Package ${scope} installation failed, rc=${rc}\n${stdout}${stderr}")
  endif()
  file(SHA256 "${_pkg_source}/wwwroot/resource.txt" _source_hash)
  file(SHA256 "${_installed}/wwwroot/resource.txt" _installed_hash)
  if(NOT _source_hash STREQUAL _installed_hash)
    message(FATAL_ERROR "Installed package resource differs from source")
  endif()
  execute_process(COMMAND ${_install} WORKING_DIRECTORY "${_pkg_project}" RESULT_VARIABLE rc
    OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr TIMEOUT 30)
  if(NOT "${rc}" STREQUAL "1" OR NOT stderr MATCHES "status=exists package=Fixture.Local")
    message(FATAL_ERROR "Duplicate ${scope} installation was not rejected, rc=${rc}\n${stdout}${stderr}")
  endif()
  execute_process(COMMAND ${_pkg_env} "${GEN1}" "${_pkg_project}/main.zan" -o "${_pkg_project}/app.exe"
    WORKING_DIRECTORY "${_pkg_project}" RESULT_VARIABLE rc OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr TIMEOUT 120)
  string(REGEX MATCHALL "ZANPKG_USAGE plugin_id=test-plugin package=Fixture.Local action=build" _usage "${stderr}")
  list(LENGTH _usage _usage_count)
  if(NOT "${rc}" STREQUAL "0" OR NOT _usage_count EQUAL 1 OR stderr MATCHES "ZANPKG_MISSING")
    message(FATAL_ERROR "Installed ${scope} package discovery or usage failed, rc=${rc}\n${stdout}${stderr}")
  endif()
  execute_process(COMMAND ${_pkg_env} "${GEN1}" "${_pkg_project}/main.zan" --package-list-missing
    -o "${_pkg_project}/scan.exe" WORKING_DIRECTORY "${_pkg_project}" RESULT_VARIABLE rc
    OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr TIMEOUT 30)
  if(NOT "${rc}" STREQUAL "0" OR stderr MATCHES "ZANPKG_MISSING" OR EXISTS "${_pkg_project}/scan.exe"
      OR NOT stderr MATCHES "ZANPKG_STATUS action=list-missing status=ok count=0")
    message(FATAL_ERROR "Installed ${scope} package not found by scan, rc=${rc}\n${stdout}${stderr}")
  endif()
  execute_process(COMMAND "${_pkg_project}/app.exe" RESULT_VARIABLE rc TIMEOUT 15)
  if(NOT "${rc}" STREQUAL "0")
    message(FATAL_ERROR "Installed ${scope} package consumer failed, rc=${rc}")
  endif()
  execute_process(COMMAND ${_pkg_env} "${GEN1}" "${_pkg_project}/main.zan" --no-packages -o "${_pkg_project}/disabled.exe"
    WORKING_DIRECTORY "${_pkg_project}" RESULT_VARIABLE rc OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr TIMEOUT 30)
  if("${rc}" STREQUAL "0" OR EXISTS "${_pkg_project}/disabled.exe")
    message(FATAL_ERROR "--no-packages unexpectedly resolved the ${scope} package")
  endif()
  file(REMOVE_RECURSE "${_installed}")
endforeach()

file(WRITE "${_pkg}/symbols.zan" [=[
namespace Fixture.Symbols;
class Catalog {
    public static int Apply(ref int count, out string label) { return BodyNeedNotBind(); }
    [System.Runtime.InteropServices.DllImport("kernel32")]
    public static extern int GetLastError();
}
]=])
execute_process(COMMAND ${_gen1_env} "${GEN1}" --emit-symbols "${_pkg}/symbols.tsv" "${_pkg}/symbols.zan"
  RESULT_VARIABLE rc OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr TIMEOUT 30)
if(NOT "${rc}" STREQUAL "0" OR EXISTS "${_pkg}/symbols.exe")
  message(FATAL_ERROR "Declaration symbol export failed, rc=${rc}\n${stdout}${stderr}")
endif()
file(READ "${_pkg}/symbols.tsv" _symbols)
if(NOT _symbols MATCHES "^# zan-symbols 1\n" OR NOT _symbols MATCHES "T\tclass\tFixture.Symbols\tCatalog\t"
    OR NOT _symbols MATCHES "M\tCatalog\tApply\tM\t1\tint Apply\\(ref int count, out string label\\)"
    OR NOT _symbols MATCHES "M\tCatalog\tGetLastError\tM\t1\tint GetLastError\\(\\)\tx\n"
    OR NOT _symbols MATCHES "T\tclass\t\\*builtin\\*\tstring\t")
  message(FATAL_ERROR "Unexpected IDE symbol index format")
endif()
file(REMOVE_RECURSE "${_pkg}")
