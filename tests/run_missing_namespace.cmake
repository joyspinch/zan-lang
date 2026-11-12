# Missing imports must fail both pull-in modes, including metadata-only runs.
if(NOT DEFINED ZANC OR NOT DEFINED WORK OR NOT DEFINED STDLIB)
  message(FATAL_ERROR "ZANC, WORK and STDLIB are required")
endif()
file(MAKE_DIRECTORY "${WORK}")
file(WRITE "${WORK}/zan.proj" "name=MissingNamespaceRegression\n")
file(WRITE "${WORK}/input.zan"
  "using Zan.CompilerRegression.NoSuchPackage;\nclass Program { static void Main() {} }\n")
foreach(mode filtered unfiltered)
  if(mode STREQUAL "unfiltered")
    set(ENV{ZAN_NO_PULLIN_FILTER} 1)
  else()
    unset(ENV{ZAN_NO_PULLIN_FILTER})
  endif()
  foreach(output metadata executable)
    set(meta "${WORK}/${mode}.json")
    set(exe "${WORK}/${mode}.exe")
    file(REMOVE "${meta}" "${exe}")
    if(output STREQUAL "metadata")
      set(args --gen-meta "${meta}")
    else()
      set(args -o "${exe}")
    endif()
    execute_process(COMMAND "${ZANC}" "${WORK}/input.zan"
      --stdlib-path "${STDLIB}" --auto-stdlib ${args}
      WORKING_DIRECTORY "${WORK}"
      RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 30)
    if(NOT "${rc}" STREQUAL "3" OR
       NOT err MATCHES "ZANPKG_MISSING namespace=Zan/CompilerRegression/NoSuchPackage" OR
       NOT err MATCHES "error:.*unresolved imported namespace")
      message(FATAL_ERROR "${mode}/${output}: expected missing namespace failure, got ${rc}: ${out}${err}")
    endif()
    if(EXISTS "${meta}" OR EXISTS "${exe}")
      message(FATAL_ERROR "${mode}/${output}: failed import published output")
    endif()
  endforeach()
endforeach()
message(STATUS "missing namespaces fail metadata and executable compilation")
