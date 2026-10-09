# Run with cmake -DZANC=<compiler> -DWORKDIR=<repository>
#   -DSCRATCH=<repository>/_scratch/async_ir_scaling
#   -DMAX_GROWTH_PERCENT=<calibrated limit> -P tests/run_async_ir_scaling.cmake
# Set the limit using an old/new compiler comparison. Both named slots and
# waitpoints double by default: linear IR approaches 200%, quadratic IR 400%.
# This checks instruction counts before whole-module optimization and real
# published behavior, without depending on individual LLVM instruction names.
cmake_policy(SET CMP0054 NEW)

if(NOT ZANC OR NOT WORKDIR OR NOT SCRATCH OR NOT DEFINED MAX_GROWTH_PERCENT)
  message(FATAL_ERROR "ZANC, WORKDIR, SCRATCH and MAX_GROWTH_PERCENT are required")
endif()
if(NOT DEFINED SMALL_SCALE)
  set(SMALL_SCALE 48)
endif()
if(NOT DEFINED LARGE_SCALE)
  set(LARGE_SCALE 96)
endif()
foreach(_key SMALL_SCALE LARGE_SCALE MAX_GROWTH_PERCENT)
  if(NOT "${${_key}}" MATCHES "^[1-9][0-9]*$")
    message(FATAL_ERROR "${_key} must be a positive integer")
  endif()
endforeach()
if(NOT LARGE_SCALE GREATER SMALL_SCALE)
  message(FATAL_ERROR "LARGE_SCALE must exceed SMALL_SCALE")
endif()
get_filename_component(SCRATCH "${SCRATCH}" ABSOLUTE BASE_DIR "${WORKDIR}")
file(MAKE_DIRECTORY "${SCRATCH}")

function(measure_async_ir scale out_insns)
  set(_src "${SCRATCH}/async_scale_${scale}.zan")
  set(_exe "${SCRATCH}/async_scale_${scale}${CMAKE_EXECUTABLE_SUFFIX}")
  if(WIN32 AND NOT CMAKE_EXECUTABLE_SUFFIX)
    string(APPEND _exe ".exe")
  endif()
  set(_manifest "${SCRATCH}/async_scale_${scale}.json")
  set(_source "using System;\nclass AsyncScale {\n    static async int Run(int seed) {\n        int sum = 0;\n")
  math(EXPR _last "${scale} - 1")
  foreach(_i RANGE 0 ${_last})
    string(APPEND _source "        int slot${_i} = seed + ${_i};\n")
  endforeach()
  foreach(_i RANGE 0 ${_last})
    string(APPEND _source "        await Task.Yield();\n        slot${_i} = slot${_i} + 1;\n")
  endforeach()
  foreach(_i RANGE 0 ${_last})
    string(APPEND _source "        sum = sum + slot${_i};\n")
  endforeach()
  string(APPEND _source "        return sum;\n    }\n    static async void Main() {\n        Console.WriteLine(\"sum=\" + Convert.ToString(await AsyncScale.Run(3)));\n    }\n}\n")
  file(WRITE "${_src}" "${_source}")
  # Never let a failed compiler invocation reuse a previous executable/manifest.
  file(REMOVE "${_exe}" "${_manifest}")
  execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "ZAN_CODEGEN_MANIFEST_JSON=${_manifest}"
            "${ZANC}" "${_src}" --auto-stdlib --publish
            --package-project "${WORKDIR}" -o "${_exe}" ${ZANC_ARGS}
    WORKING_DIRECTORY "${WORKDIR}"
    RESULT_VARIABLE _compile_rc
    OUTPUT_VARIABLE _compile_out
    ERROR_VARIABLE _compile_err
    TIMEOUT 180)
  file(WRITE "${SCRATCH}/async_scale_${scale}.compile.log" "${_compile_out}${_compile_err}")
  if(NOT "${_compile_rc}" STREQUAL "0" OR NOT EXISTS "${_exe}" OR NOT EXISTS "${_manifest}")
    message(FATAL_ERROR "async scale ${scale}: publish failed (${_compile_rc})\n${_compile_out}${_compile_err}")
  endif()

  file(READ "${_manifest}" _json)
  string(JSON _schema GET "${_json}" schema)
  if(NOT _schema STREQUAL "zan-cg-manifest-v1")
    message(FATAL_ERROR "async scale ${scale}: unsupported manifest schema ${_schema}")
  endif()
  string(JSON _count LENGTH "${_json}" functions)
  set(_matches 0)
  set(_insns 0)
  if(_count GREATER 0)
    math(EXPR _last_fn "${_count} - 1")
    foreach(_i RANGE 0 ${_last_fn})
      string(JSON _name GET "${_json}" functions ${_i} name)
      string(JSON _kind GET "${_json}" functions ${_i} kind)
      if(_name STREQUAL "AsyncScale_Run$resume" AND _kind STREQUAL "async-resume")
        string(JSON _insns GET "${_json}" functions ${_i} insns)
        math(EXPR _matches "${_matches} + 1")
      endif()
    endforeach()
  endif()
  if(NOT _matches EQUAL 1 OR NOT _insns GREATER 0)
    message(FATAL_ERROR "async scale ${scale}: expected one nonempty Run resume, found ${_matches}")
  endif()

  execute_process(
    COMMAND "${_exe}"
    WORKING_DIRECTORY "${WORKDIR}"
    RESULT_VARIABLE _run_rc
    OUTPUT_VARIABLE _run_out
    ERROR_VARIABLE _run_err
    TIMEOUT 30)
  file(WRITE "${SCRATCH}/async_scale_${scale}.run.log" "${_run_out}${_run_err}")
  if(NOT "${_run_rc}" STREQUAL "0")
    message(FATAL_ERROR "async scale ${scale}: program failed (${_run_rc})\n${_run_out}${_run_err}")
  endif()
  math(EXPR _expected "${scale} * (${scale} + 7) / 2")
  string(REPLACE "\r\n" "\n" _run_out "${_run_out}")
  string(STRIP "${_run_out}" _run_out)
  if(NOT _run_out STREQUAL "sum=${_expected}")
    message(FATAL_ERROR "async scale ${scale}: expected sum=${_expected}, got ${_run_out}")
  endif()
  message(STATUS "async scale ${scale}: Run resume ${_insns} instructions; sum=${_expected}")
  set(${out_insns} "${_insns}" PARENT_SCOPE)
endfunction()

measure_async_ir(${SMALL_SCALE} _small_insns)
measure_async_ir(${LARGE_SCALE} _large_insns)
math(EXPR _growth_percent "(100 * ${_large_insns} + ${_small_insns} - 1) / ${_small_insns}")
file(WRITE "${SCRATCH}/async_scale_growth.txt"
  "small_scale=${SMALL_SCALE}\nsmall_insns=${_small_insns}\nlarge_scale=${LARGE_SCALE}\nlarge_insns=${_large_insns}\ngrowth_percent=${_growth_percent}\nmax_growth_percent=${MAX_GROWTH_PERCENT}\n")
# Compare the products, so integer rounding cannot admit an over-budget result.
math(EXPR _actual "100 * ${_large_insns}")
math(EXPR _limit "${MAX_GROWTH_PERCENT} * ${_small_insns}")
if(_actual GREATER _limit)
  message(FATAL_ERROR "async IR grew ${_growth_percent}% (${_small_insns} -> ${_large_insns}), limit ${MAX_GROWTH_PERCENT}%")
endif()
message(STATUS "async IR growth ${_growth_percent}% <= ${MAX_GROWTH_PERCENT}%")
