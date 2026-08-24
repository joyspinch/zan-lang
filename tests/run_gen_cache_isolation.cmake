# A generator compiled from one stdlib worktree must never be reused for
# another worktree merely because its files have older timestamps.

if(NOT ZANC OR NOT STDLIB OR NOT SRC OR NOT WORK)
  message(FATAL_ERROR
    "run_gen_cache_isolation.cmake: ZANC, STDLIB, SRC and WORK are required")
endif()

file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}/stdlib-a" "${WORK}/stdlib-b" "${WORK}/cache")

file(GLOB_RECURSE _stdlib_sources RELATIVE "${STDLIB}" "${STDLIB}/*.zan")
foreach(_rel ${_stdlib_sources})
  get_filename_component(_dir "${_rel}" DIRECTORY)
  file(MAKE_DIRECTORY "${WORK}/stdlib-a/${_dir}" "${WORK}/stdlib-b/${_dir}")
  file(COPY_FILE "${STDLIB}/${_rel}" "${WORK}/stdlib-a/${_rel}")
  file(COPY_FILE "${STDLIB}/${_rel}" "${WORK}/stdlib-b/${_rel}")
endforeach()
file(APPEND "${WORK}/stdlib-b/System/Compiler/GenDbEmit.zan" "\n")

set(_env
  "LOCALAPPDATA=${WORK}/cache"
  "XDG_CACHE_HOME=${WORK}/cache")

foreach(_name a b)
  execute_process(
    COMMAND ${CMAKE_COMMAND} -E env ${_env}
      ${ZANC} --stdlib-path "${WORK}/stdlib-${_name}" "${SRC}"
      -o "${WORK}/case-${_name}"
    RESULT_VARIABLE _rc
    OUTPUT_VARIABLE _out
    ERROR_VARIABLE _err)
  if(NOT _rc EQUAL 0)
    message(FATAL_ERROR
      "generator cache isolation compile ${_name} failed (${_rc})\n${_out}${_err}")
  endif()
endforeach()

if(WIN32)
  file(GLOB _cached "${WORK}/cache/Zan/gen/ZanGen_*.exe")
else()
  file(GLOB _cached "${WORK}/cache/zan/gen/ZanGen_*")
endif()
list(LENGTH _cached _count)
if(NOT _count EQUAL 2)
  message(FATAL_ERROR
    "expected two isolated generator cache images, found ${_count}: ${_cached}")
endif()

message(STATUS "OK: generator caches are isolated by compiler and stdlib")
