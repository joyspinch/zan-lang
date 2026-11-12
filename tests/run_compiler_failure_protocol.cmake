# Negative protocol tests: success-looking IR/objects must never escape a failed
# parser or LLVM pass pipeline. Each CASE gets its own build/cases directory.
if(NOT DEFINED ZANC OR NOT DEFINED WORK OR NOT DEFINED CASE)
  message(FATAL_ERROR "ZANC, WORK and CASE are required")
endif()
file(MAKE_DIRECTORY "${WORK}")
set(source "${WORK}/input.zan")
set(output "${WORK}/output.exe")
set(valid [=[class Program {
    static int Sum(int n) { int r = 0; while (n > 0) { r += n; n--; } return r; }
    static int Product(int n) { int r = 1; while (n > 1) { r *= n; n--; } return r; }
    static int Main() { return Sum(3) + Product(3); }
}
]=])
# Isolate inherited debug switches so the test selects its intended path.
foreach(name ZAN_OPT_PIPELINE ZAN_EARLY_PASSES ZAN_SHARD_OPT ZAN_NO_SHARD
             ZAN_NO_OPT_EARLY ZAN_NO_OPT_ESCAPE ZAN_NO_OPT_ARC
             ZAN_NO_OPT_DEVIRT ZAN_NO_OPT_CONSTFOLD ZAN_NO_OPT_DCE
             ZAN_NO_OPT_INLINE ZAN_DUMP_OPT_IR ZAN_CODEGEN_MANIFEST_JSON)
  unset(ENV{${name}})
endforeach()
set(ENV{ZAN_SHARD} 0)
file(WRITE "${source}" "${valid}")
# A healthy control must reach IR output under the same executable.
execute_process(COMMAND "${ZANC}" "${source}" -O0 --emit-ir
  RESULT_VARIABLE rc OUTPUT_VARIABLE ir ERROR_VARIABLE err TIMEOUT 30)
if(NOT "${rc}" STREQUAL "0" OR NOT ir MATCHES "define")
  message(FATAL_ERROR "control compile failed (${rc}): ${err}")
endif()

function(expect_failure label expected)
  file(REMOVE "${output}" "${output}.o")
  execute_process(COMMAND "${ZANC}" "${source}" ${ARGN}
    RESULT_VARIABLE rc OUTPUT_VARIABLE ir ERROR_VARIABLE err TIMEOUT 30)
  file(WRITE "${WORK}/${label}.log" "exit=${rc}\n${err}\nstdout:\n${ir}")
  if(NOT "${rc}" MATCHES "^[0-9]+$" OR "${rc}" STREQUAL "0")
    message(FATAL_ERROR "${label}: expected normal nonzero exit, got '${rc}': ${err}")
  endif()
  if(NOT err MATCHES "${expected}")
    message(FATAL_ERROR "${label}: missing '${expected}': ${err}")
  endif()
  if(NOT ir STREQUAL "")
    message(FATAL_ERROR "${label}: failed compilation published stdout/IR")
  endif()
  file(GLOB leftovers "${output}*.o" "${output}*.frag.ll")
  if(EXISTS "${output}" OR leftovers)
    message(FATAL_ERROR "${label}: failed compilation left artifacts: ${leftovers}")
  endif()
endfunction()

if(CASE STREQUAL "parser")
  foreach(shape leading repeated file_namespace block_namespace trailing)
    if(shape STREQUAL "leading")
      set(text "}\n${valid}")
    elseif(shape STREQUAL "repeated")
      set(text "} }\n${valid}")
    elseif(shape STREQUAL "file_namespace")
      set(text "namespace Test;\n}\n${valid}")
    elseif(shape STREQUAL "block_namespace")
      set(text "namespace Test {}\n}\n${valid}")
    else()
      set(text "${valid}\n}")
    endif()
    file(WRITE "${source}" "${text}")
    expect_failure("${shape}" "unexpected '}' at top level" -O0 --emit-ir)
  endforeach()
elseif(CASE STREQUAL "pipeline" OR CASE STREQUAL "early")
  if(CASE STREQUAL "pipeline")
    set(ENV{ZAN_OPT_PIPELINE} "zan-invalid-pass")
  else()
    set(ENV{ZAN_EARLY_PASSES} "zan-invalid-pass")
  endif()
  expect_failure(ir "error: LLVM pass pipeline.*zan-invalid-pass" -O1 --emit-ir)
  expect_failure(object "error: LLVM pass pipeline.*zan-invalid-pass" -O1 -o "${output}")
elseif(CASE STREQUAL "shard")
  set(ENV{ZAN_SHARD} 1)
  set(ENV{ZAN_SHARD_MAX_FN} 1)
  set(ENV{ZAN_SHARD_MAX_INSN} 1)
  set(ENV{ZAN_SHARD_OPT} 1)
  set(ENV{ZAN_OPT_PIPELINE} "zan-invalid-pass")
  # O0 skips the coordinator's optimizer: failure must originate in a shard,
  # and must not fall back to successfully emitting the intact coordinator.
  expect_failure(shard "error: shard: shard [0-9]+: LLVM optimization failed"
    -O0 -o "${output}")
else()
  message(FATAL_ERROR "unknown CASE '${CASE}'")
endif()
message(STATUS "compiler failure protocol ${CASE}: passed")
