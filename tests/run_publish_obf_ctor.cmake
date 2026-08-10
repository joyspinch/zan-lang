# --publish string de-obfuscation: how the startup constructor is registered.
#
# Invoked by ctest as:
#   cmake -DZANC=<zanc> -DSRC=<case.zan> -DTARGET=<cross target> -DKIND=<init_array|global_ctors> \
#         -DWORKDIR=<dir> -P run_publish_obf_ctor.cmake
#
# --publish XORs every string literal in the image and restores it before main
# from `@__zan.deobf`. If that constructor never runs the program prints pure
# garbage, so the registration mechanism has to match what the target's startup
# path actually iterates:
#
#   ELF   -> `.init_array`. llvm.global_ctors is NOT usable here: the LLVM C API
#            builds every TargetMachine with UseInitArray=false, so it lowers to
#            a legacy `.ctors` section, and zanc's static musl link (crt1/crti/
#            crtn, no crtbegin/crtend) never walks `.ctors`.
#   other -> llvm.global_ctors (mingw CRT runs .ctors, Darwin __mod_init_func,
#            wasm __wasm_call_ctors).
#
# Checked on the IR rather than a linked image so it runs on every host.

execute_process(
  COMMAND "${ZANC}" "${SRC}" --target "${TARGET}" --publish --emit-ir
  OUTPUT_VARIABLE _ir
  ERROR_VARIABLE _err
  RESULT_VARIABLE _rc
  WORKING_DIRECTORY "${WORKDIR}")

if(NOT _rc EQUAL 0)
  message(FATAL_ERROR "run_publish_obf_ctor: zanc --target ${TARGET} --publish failed (rc=${_rc})\n${_err}")
endif()

string(REPLACE "\r" "" _ir "${_ir}")

# Sanity: the case must actually have been obfuscated, otherwise the checks
# below would pass vacuously on a build where --publish stopped scrambling.
string(FIND "${_ir}" "@__zan.deobf" _pos)
if(_pos EQUAL -1)
  message(FATAL_ERROR
    "run_publish_obf_ctor: ${TARGET} --publish IR has no @__zan.deobf; "
    "string obfuscation did not run")
endif()

if(KIND STREQUAL "init_array")
  set(_want "@__zan.init_array.deobf" "section \".init_array\"" "@llvm.used")
  set(_unwanted "@llvm.global_ctors")
else()
  set(_want "@llvm.global_ctors")
  set(_unwanted "section \".init_array\"")
endif()

foreach(_n ${_want})
  string(FIND "${_ir}" "${_n}" _pos)
  if(_pos EQUAL -1)
    message(FATAL_ERROR
      "run_publish_obf_ctor: ${TARGET} expects the ${KIND} registration but the IR lacks:\n  ${_n}")
  endif()
endforeach()

foreach(_n ${_unwanted})
  string(FIND "${_ir}" "${_n}" _pos)
  if(NOT _pos EQUAL -1)
    message(FATAL_ERROR
      "run_publish_obf_ctor: ${TARGET} uses the ${KIND} registration but the IR also carries:\n  ${_n}\n"
      "Running the constructor twice re-scrambles every literal.")
  endif()
endforeach()
