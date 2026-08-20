cmake_policy(SET CMP0012 NEW)

if(NOT ZANC OR NOT SRC OR NOT WORK)
  message(FATAL_ERROR "run_driver_packaging: ZANC, SRC and WORK are required")
endif()

file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}/static" "${WORK}/shared")

set(_common --auto-stdlib --publish)
execute_process(
  COMMAND "${ZANC}" "${SRC}" ${_common} --link-mode static
          -o "${WORK}/static/sqlite_crud"
  RESULT_VARIABLE _static_rc
  OUTPUT_VARIABLE _static_out
  ERROR_VARIABLE _static_err)
if(NOT _static_rc EQUAL 0)
  message(FATAL_ERROR
    "run_driver_packaging: static publish failed (rc=${_static_rc})\n"
    "${_static_out}${_static_err}")
endif()

file(GLOB _static_shared "${WORK}/static/*.so*")
if(NOT _static_shared STREQUAL "")
  message(FATAL_ERROR
    "run_driver_packaging: static publish copied shared drivers: "
    "${_static_shared}")
endif()

execute_process(
  COMMAND "${WORK}/static/sqlite_crud"
  RESULT_VARIABLE _static_run_rc
  OUTPUT_VARIABLE _static_run_out
  ERROR_VARIABLE _static_run_err)
if(NOT _static_run_rc EQUAL 0 OR NOT _static_run_out MATCHES "done")
  message(FATAL_ERROR
    "run_driver_packaging: static program failed (rc=${_static_run_rc})\n"
    "${_static_run_out}${_static_run_err}")
endif()

execute_process(
  COMMAND "${ZANC}" "${SRC}" ${_common}
          -o "${WORK}/shared/sqlite_crud"
  RESULT_VARIABLE _shared_rc
  OUTPUT_VARIABLE _shared_out
  ERROR_VARIABLE _shared_err)
if(NOT _shared_rc EQUAL 0)
  message(FATAL_ERROR
    "run_driver_packaging: shared publish failed (rc=${_shared_rc})\n"
    "${_shared_out}${_shared_err}")
endif()

if(NOT EXISTS "${WORK}/shared/libsqlite3.so")
  message(FATAL_ERROR
    "run_driver_packaging: shared publish did not copy libsqlite3.so")
endif()

execute_process(
  COMMAND "${WORK}/shared/sqlite_crud"
  RESULT_VARIABLE _shared_run_rc
  OUTPUT_VARIABLE _shared_run_out
  ERROR_VARIABLE _shared_run_err)
if(NOT _shared_run_rc EQUAL 0 OR NOT _shared_run_out MATCHES "done")
  message(FATAL_ERROR
    "run_driver_packaging: shared program failed (rc=${_shared_run_rc})\n"
    "${_shared_run_out}${_shared_run_err}")
endif()
