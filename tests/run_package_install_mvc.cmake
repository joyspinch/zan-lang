# Run with -DZANC=<zanc> -DSRC=<packages/Zan.Mvc> -DWORK=<_scratch/...>
# and -P tests/run_package_install_mvc.cmake.
if(NOT ZANC OR NOT SRC OR NOT WORK)
  message(FATAL_ERROR "ZANC, SRC and WORK are required")
endif()
get_filename_component(_repo "${SRC}/../.." ABSOLUTE)
set(_stdlib_args --stdlib-path "${_repo}/stdlib")

file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}/project/src" "${WORK}/legacy/stdlib/Legacy"
                    "${WORK}/source/src/SourceLayout" "${WORK}/shadow/src/ZanWeb/Ai"
                    "${WORK}/empty")
file(WRITE "${WORK}/project/zan.proj" "name = \"PackageInstallTest\"\n")
file(WRITE "${WORK}/project/src/main.zan"
  "using System;\nusing ZanWeb.Ai;\nclass Program { static void Main() { Console.WriteLine(AiEndpointPolicy.IsAllowed(\"https://api.openai.com/v1\") ? \"installed\" : \"missing\"); } }\n")

execute_process(
  COMMAND "${ZANC}" --package-install "${SRC}" --package-name Zan.Mvc
          --package-scope project --package-project "${WORK}/project"
  RESULT_VARIABLE _rc OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
if(NOT _rc EQUAL 0 OR NOT _err MATCHES "status=installed package=Zan.Mvc scope=project")
  message(FATAL_ERROR "src-layout MVC install failed (${_rc}): ${_out}${_err}")
endif()
set(_installed "${WORK}/project/.zan-packages/Zan.Mvc")
foreach(_path zan.pkg src/ZanWeb/Framework/Ai/AiEndpointPolicy.zan
              views/Account/layout.html wwwroot/css/app.css)
  if(NOT EXISTS "${_installed}/${_path}")
    message(FATAL_ERROR "MVC install lost ${_path}")
  endif()
endforeach()
file(SHA256 "${SRC}/src/ZanWeb/Framework/Ai/AiEndpointPolicy.zan" _source_hash)
file(SHA256 "${_installed}/src/ZanWeb/Framework/Ai/AiEndpointPolicy.zan" _installed_hash)
if(NOT _source_hash STREQUAL _installed_hash)
  message(FATAL_ERROR "installed MVC source differs from source package")
endif()
file(GLOB_RECURSE _source_files "${SRC}/src/*.zan")
file(GLOB_RECURSE _installed_files "${_installed}/src/*.zan")
list(LENGTH _source_files _source_count)
list(LENGTH _installed_files _installed_count)
if(NOT _source_count EQUAL 119 OR NOT _installed_count EQUAL _source_count)
  message(FATAL_ERROR "MVC sources were duplicated or lost: ${_source_count} -> ${_installed_count}")
endif()

# Declared ZanWeb.Ai must resolve from Framework/Ai without aliases or copies.
execute_process(
  COMMAND "${ZANC}" "${WORK}/project/src/main.zan" --auto-stdlib ${_stdlib_args}
          -o "${WORK}/project/automatic.exe"
  RESULT_VARIABLE _rc OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
if(NOT _rc EQUAL 0)
  message(FATAL_ERROR "installed MVC automatic namespace import failed (${_rc}): ${_out}${_err}")
endif()
execute_process(COMMAND "${WORK}/project/automatic.exe"
  RESULT_VARIABLE _rc OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
if(NOT _rc EQUAL 0 OR NOT _out STREQUAL "installed\n")
  message(FATAL_ERROR "installed MVC automatic namespace import did not run (${_rc}): ${_out}${_err}")
endif()
# A sibling package can provide the same namespace without making MVC's
# physical Framework/Ai directory invisible or repeating the same source.
file(WRITE "${WORK}/shadow/zan.pkg" "name = \"Shadow\"\nversion = \"1.0.0\"\n")
file(WRITE "${WORK}/shadow/src/ZanWeb/Ai/Shadow.zan"
  "namespace ZanWeb.Ai; class Shadow { static string Value() { return \"shadow\"; } }\n")
file(WRITE "${WORK}/shadow/src/ZanWeb/Ai/WrongNamespace.zan"
  "namespace Other; class AiEndpointPolicy { static bool IsAllowed(string s) { return false; } }\n")
execute_process(COMMAND "${ZANC}" --package-install "${WORK}/shadow"
  --package-name Shadow --package-scope project --package-project "${WORK}/project"
  RESULT_VARIABLE _rc OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
if(NOT _rc EQUAL 0)
  message(FATAL_ERROR "same-namespace package install failed (${_rc}): ${_out}${_err}")
endif()
# A second declared namespace in the same MVC tree must also resolve.
file(WRITE "${WORK}/project/src/two_namespaces.zan"
  "using System; using ZanWeb.Ai; using ZanWeb.Web; class Program { static void Main() { Console.WriteLine(AiEndpointPolicy.IsAllowed(\"https://api.openai.com/v1\") && Prose.Runes(Shadow.Value()) > 0 ? Shadow.Value() : \"missing\"); } }\n")
execute_process(
  COMMAND "${ZANC}" "${WORK}/project/src/two_namespaces.zan" --auto-stdlib ${_stdlib_args}
          -o "${WORK}/project/two_namespaces.exe"
  RESULT_VARIABLE _rc OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
if(NOT _rc EQUAL 0)
  message(FATAL_ERROR "multiple MVC namespaces did not compile (${_rc}): ${_out}${_err}")
endif()
execute_process(COMMAND "${WORK}/project/two_namespaces.exe"
  RESULT_VARIABLE _rc OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
if(NOT _rc EQUAL 0 OR NOT _out STREQUAL "shadow\n")
  message(FATAL_ERROR "multiple package namespaces did not run (${_rc}): ${_out}${_err}")
endif()
# The same installed source may also be passed explicitly. Discovery must
# deduplicate it instead of registering the type twice.
execute_process(
  COMMAND "${ZANC}" "${WORK}/project/src/main.zan"
          "${_installed}/src/ZanWeb/Framework/Ai/AiEndpointPolicy.zan"
          --auto-stdlib ${_stdlib_args} -o "${WORK}/project/installed.exe"
  RESULT_VARIABLE _rc OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
if(NOT _rc EQUAL 0)
  message(FATAL_ERROR "installed MVC explicit + automatic source failed (${_rc}): ${_out}${_err}")
endif()
execute_process(COMMAND "${WORK}/project/installed.exe"
  RESULT_VARIABLE _rc OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
if(NOT _rc EQUAL 0 OR NOT _out STREQUAL "installed\n")
  message(FATAL_ERROR "installed MVC explicit source did not run (${_rc}): ${_out}${_err}")
endif()
# Project packages/ takes priority over an installed copy with the same name;
# scanning both absolute paths would register the same declarations twice.
file(MAKE_DIRECTORY "${WORK}/project/packages/Shadow/src/ZanWeb/Ai")
file(WRITE "${WORK}/project/packages/Shadow/src/ZanWeb/Ai/Shadow.zan"
  "namespace ZanWeb.Ai; class Shadow { static string Value() { return \"project\"; } }\n")
execute_process(
  COMMAND "${ZANC}" "${WORK}/project/src/two_namespaces.zan" --auto-stdlib ${_stdlib_args}
          -o "${WORK}/project/priority.exe"
  RESULT_VARIABLE _rc OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
if(NOT _rc EQUAL 0)
  message(FATAL_ERROR "duplicate package precedence failed (${_rc}): ${_out}${_err}")
endif()
execute_process(COMMAND "${WORK}/project/priority.exe"
  RESULT_VARIABLE _rc OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
if(NOT _rc EQUAL 0 OR NOT _out STREQUAL "project\n")
  message(FATAL_ERROR "project package did not take precedence (${_rc}): ${_out}${_err}")
endif()
# A normal namespace-aligned src layout must also remain discoverable.
file(WRITE "${WORK}/source/zan.pkg" "name = \"SourceLayout\"\nversion = \"1.0.0\"\n")
file(WRITE "${WORK}/source/src/SourceLayout/Api.zan"
  "namespace SourceLayout; class Api { static string Hello() { return \"source\"; } }\n")
execute_process(
  COMMAND "${ZANC}" --package-install "${WORK}/source" --package-name SourceLayout
          --package-scope project --package-project "${WORK}/project"
  RESULT_VARIABLE _rc OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
if(NOT _rc EQUAL 0 OR NOT EXISTS "${WORK}/project/.zan-packages/SourceLayout/src/SourceLayout/Api.zan")
  message(FATAL_ERROR "namespace-aligned src-layout install failed (${_rc}): ${_out}${_err}")
endif()
file(WRITE "${WORK}/project/src/aligned.zan"
  "using System; using SourceLayout; class Program { static void Main() { Console.WriteLine(Api.Hello()); } }\n")
execute_process(
  COMMAND "${ZANC}" "${WORK}/project/src/aligned.zan" --auto-stdlib ${_stdlib_args}
          -o "${WORK}/project/aligned.exe"
  RESULT_VARIABLE _rc OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
if(NOT _rc EQUAL 0)
  message(FATAL_ERROR "installed src-layout namespace did not compile (${_rc}): ${_out}${_err}")
endif()
execute_process(COMMAND "${WORK}/project/aligned.exe"
  RESULT_VARIABLE _rc OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
if(NOT _rc EQUAL 0 OR NOT _out STREQUAL "source\n")
  message(FATAL_ERROR "installed src-layout namespace did not run (${_rc}): ${_out}${_err}")
endif()

# Keep the pre-existing stdlib-layout path usable and reject packages without
# either explicit source root (an unrelated views/wwwroot tree is not enough).
file(WRITE "${WORK}/legacy/zan.pkg" "name = \"Legacy\"\nversion = \"1.0.0\"\n")
file(WRITE "${WORK}/legacy/stdlib/Legacy/Api.zan" "namespace Legacy; class Api {}\n")
execute_process(
  COMMAND "${ZANC}" --package-install "${WORK}/legacy" --package-name Legacy
          --package-scope project --package-project "${WORK}/project"
  RESULT_VARIABLE _rc OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
if(NOT _rc EQUAL 0 OR NOT EXISTS "${WORK}/project/.zan-packages/Legacy/stdlib/Legacy/Api.zan")
  message(FATAL_ERROR "legacy stdlib-layout install failed (${_rc}): ${_out}${_err}")
endif()
file(WRITE "${WORK}/empty/zan.pkg" "name = \"Empty\"\nversion = \"1.0.0\"\n")
execute_process(
  COMMAND "${ZANC}" --package-install "${WORK}/empty" --package-name Empty
          --package-scope project --package-project "${WORK}/project"
  RESULT_VARIABLE _rc OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
if(_rc EQUAL 0 OR NOT _err MATCHES "status=no_stdlib_layout package=Empty"
   OR EXISTS "${WORK}/project/.zan-packages/Empty")
  message(FATAL_ERROR "source-less package unexpectedly installed (${_rc}): ${_out}${_err}")
endif()
message(STATUS "MVC declared-namespace imports, explicit source, sibling package, src/legacy installs and empty rejection passed")
