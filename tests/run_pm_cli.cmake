# run_pm_cli.cmake: tests zan-pm CLI end-to-end
if(NOT DEFINED ZAN_PM OR NOT DEFINED WORK_DIR)
    message(FATAL_ERROR "ZAN_PM and WORK_DIR must be defined")
endif()

file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")

# 1. zan-pm init
execute_process(
    COMMAND "${ZAN_PM}" init SampleApp 1.0.0 --project "${WORK_DIR}"
    RESULT_VARIABLE res
    OUTPUT_VARIABLE out
    ERROR_VARIABLE err
)
if(NOT res EQUAL 0)
    message(FATAL_ERROR "zan-pm init failed (${res}): ${out}\n${err}")
endif()

if(NOT EXISTS "${WORK_DIR}/zan.pkg")
    message(FATAL_ERROR "zan.pkg was not generated")
endif()

# 2. zan-pm add
execute_process(
    COMMAND "${ZAN_PM}" add Zan.Linq "^1.2.0" --project "${WORK_DIR}"
    RESULT_VARIABLE res
    OUTPUT_VARIABLE out
    ERROR_VARIABLE err
)
if(NOT res EQUAL 0)
    message(FATAL_ERROR "zan-pm add failed (${res}): ${out}\n${err}")
endif()

# 3. zan-pm list
execute_process(
    COMMAND "${ZAN_PM}" list --project "${WORK_DIR}"
    RESULT_VARIABLE res
    OUTPUT_VARIABLE out
    ERROR_VARIABLE err
)
if(NOT res EQUAL 0)
    message(FATAL_ERROR "zan-pm list failed (${res}): ${out}\n${err}")
endif()
if(NOT out MATCHES "Zan\\.Linq")
    message(FATAL_ERROR "zan-pm list did not output Zan.Linq: ${out}")
endif()

# 4. zan-pm lock
execute_process(
    COMMAND "${ZAN_PM}" lock --project "${WORK_DIR}"
    RESULT_VARIABLE res
    OUTPUT_VARIABLE out
    ERROR_VARIABLE err
)
if(NOT res EQUAL 0)
    message(FATAL_ERROR "zan-pm lock failed (${res}): ${out}\n${err}")
endif()

if(NOT EXISTS "${WORK_DIR}/zan.lock")
    message(FATAL_ERROR "zan.lock was not generated")
endif()

# 5. zan-pm check
execute_process(
    COMMAND "${ZAN_PM}" check --project "${WORK_DIR}"
    RESULT_VARIABLE res
    OUTPUT_VARIABLE out
    ERROR_VARIABLE err
)
if(NOT res EQUAL 0)
    message(FATAL_ERROR "zan-pm check failed (${res}): ${out}\n${err}")
endif()

# 6. zan-pm remove
execute_process(
    COMMAND "${ZAN_PM}" remove Zan.Linq --project "${WORK_DIR}"
    RESULT_VARIABLE res
    OUTPUT_VARIABLE out
    ERROR_VARIABLE err
)
if(NOT res EQUAL 0)
    message(FATAL_ERROR "zan-pm remove failed (${res}): ${out}\n${err}")
endif()

file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")

# 7. zan-pm init --json (JSON manifest workflow)
execute_process(
    COMMAND "${ZAN_PM}" init JsonApp 2.0.0 --json --project "${WORK_DIR}"
    RESULT_VARIABLE res
    OUTPUT_VARIABLE out
    ERROR_VARIABLE err
)
if(NOT res EQUAL 0)
    message(FATAL_ERROR "zan-pm init --json failed (${res}): ${out}\n${err}")
endif()

if(NOT EXISTS "${WORK_DIR}/zan.json")
    message(FATAL_ERROR "zan.json was not generated")
endif()

# 8. zan-pm add with zan.json
execute_process(
    COMMAND "${ZAN_PM}" add Zan.Data "^0.2.0" --project "${WORK_DIR}"
    RESULT_VARIABLE res
    OUTPUT_VARIABLE out
    ERROR_VARIABLE err
)
if(NOT res EQUAL 0)
    message(FATAL_ERROR "zan-pm add on zan.json failed (${res}): ${out}\n${err}")
endif()

# 9. zan-pm list with zan.json
execute_process(
    COMMAND "${ZAN_PM}" list --project "${WORK_DIR}"
    RESULT_VARIABLE res
    OUTPUT_VARIABLE out
    ERROR_VARIABLE err
)
if(NOT res EQUAL 0)
    message(FATAL_ERROR "zan-pm list on zan.json failed (${res}): ${out}\n${err}")
endif()
if(NOT out MATCHES "Zan\\.Data")
    message(FATAL_ERROR "zan-pm list did not output Zan.Data: ${out}")
endif()

# 10. zan-pm restore with zan.json
execute_process(
    COMMAND "${ZAN_PM}" restore --project "${WORK_DIR}"
    RESULT_VARIABLE res
    OUTPUT_VARIABLE out
    ERROR_VARIABLE err
)
if(NOT res EQUAL 0)
    message(FATAL_ERROR "zan-pm restore failed (${res}): ${out}\n${err}")
endif()

# 11. zan-pm publish with zan.json
execute_process(
    COMMAND "${ZAN_PM}" publish --project "${WORK_DIR}"
    RESULT_VARIABLE res
    OUTPUT_VARIABLE out
    ERROR_VARIABLE err
)
if(NOT res EQUAL 0)
    message(FATAL_ERROR "zan-pm publish failed (${res}): ${out}\n${err}")
endif()

# Clean up
file(REMOVE_RECURSE "${WORK_DIR}")
message(STATUS "zan-pm CLI test passed successfully")
