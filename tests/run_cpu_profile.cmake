if(NOT DEFINED ZANC OR NOT DEFINED WORK OR NOT DEFINED ROOT)
  message(FATAL_ERROR "ZANC, WORK and ROOT required")
endif()
file(MAKE_DIRECTORY "${WORK}")
set(src "${WORK}/profile.zan")
file(WRITE "${src}" "using System;\nclass Program { static void Main() { Console.WriteLine(42); } }\n")
set(ENV{ZAN_SHARD} 0)
unset(ENV{ZAN_OPT_PIPELINE})
unset(ENV{ZAN_EARLY_PASSES})
foreach(profile default baseline avx2)
  set(args)
  if(NOT profile STREQUAL "default")
    list(APPEND args --cpu-profile ${profile})
  endif()
  execute_process(COMMAND "${ZANC}" "${src}" --auto-stdlib -O0 --emit-ir ${args}
    RESULT_VARIABLE rc OUTPUT_VARIABLE ir ERROR_VARIABLE err TIMEOUT 60)
  if(NOT "${rc}" STREQUAL "0")
    message(FATAL_ERROR "${profile} ordinary stdlib compile failed (${rc}): ${err}")
  endif()
  file(WRITE "${WORK}/${profile}.ll" "${ir}")
  if(profile STREQUAL "baseline")
    if(NOT ir MATCHES "zanc-target-features: .*\\+sse2,-sse3" OR ir MATCHES "zanc-target-features: [^\n]*\\+avx")
      message(FATAL_ERROR "baseline IR does not describe SSE2 requirements")
    endif()
  elseif(NOT ir MATCHES "zanc-target-features: [^\n]*\\+avx2")
    message(FATAL_ERROR "${profile}: historical AVX2 features missing")
  endif()
endforeach()
foreach(args "--cpu-profile;invalid" "--cpu-profile" "--target;wasm32;--cpu-profile;baseline")
  execute_process(COMMAND "${ZANC}" "${src}" ${args} --emit-ir
    RESULT_VARIABLE rc OUTPUT_VARIABLE ir ERROR_VARIABLE err TIMEOUT 30)
  if(NOT "${rc}" MATCHES "^[1-9][0-9]*$" OR NOT err MATCHES "--cpu-profile" OR NOT ir STREQUAL "")
    message(FATAL_ERROR "bad profile/target accepted (${rc}): ${args}: ${err}")
  endif()
endforeach()
# Existing nontrivial Vector256 coverage must compile and execute on SSE2.
set(exe "${WORK}/simd.exe")
execute_process(COMMAND "${ZANC}" "${ROOT}/tests/conformance/vector_advanced_simd.zan"
  --auto-stdlib --cpu-profile baseline -O1 -o "${exe}"
  RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 60)
if(NOT "${rc}" STREQUAL "0")
  message(FATAL_ERROR "baseline SIMD compile failed (${rc}): ${err}")
endif()
execute_process(COMMAND "${exe}" RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 30)
file(READ "${ROOT}/tests/conformance/vector_advanced_simd.out" expected)
string(REPLACE "\r\n" "\n" out "${out}")
string(REPLACE "\r\n" "\n" expected "${expected}")
if(NOT "${rc}" STREQUAL "0" OR NOT out STREQUAL expected)
  message(FATAL_ERROR "baseline SIMD execution failed (${rc}): ${out}\n${err}")
endif()
file(WRITE "${src}" [=[using System.Runtime.Intrinsics;
using System.Runtime.Intrinsics.X86;
class Program {
    static int Main() {
        Vector128 x = Vector128.Create((byte)1);
        Vector128 y = Aes.Encrypt(x, x);
        return Vector128.ExtractMostSignificantBits(y);
    }
}
]=])
execute_process(COMMAND "${ZANC}" "${src}" --auto-stdlib --cpu-profile baseline -O0 --emit-ir
  RESULT_VARIABLE rc OUTPUT_VARIABLE ir ERROR_VARIABLE err TIMEOUT 60)
if(NOT "${rc}" MATCHES "^[1-9][0-9]*$" OR NOT ir STREQUAL "" OR
   NOT err MATCHES "baseline does not support hardware intrinsic 'llvm.x86.aesni")
  message(FATAL_ERROR "baseline AES must fail explicitly before backend (${rc}): ${err}")
endif()
message(STATUS "CPU profiles: defaults, SSE2 SIMD fallback, invalid targets and unsupported AES checked")
