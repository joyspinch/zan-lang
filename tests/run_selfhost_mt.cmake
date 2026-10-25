# Self-hosting gate: a self-hosted program must run on the SHIPPED runtime.
#
# The C host's coroutine frames start with the scheduler's header
# (`zan_co_fhdr`: the CO_* bits and the pending step), and the shipped
# multi-worker runtime (zanrt_io_mt.o) drives those frames. The self-hosted
# backend used to lay its frames out differently, so a self-hosted program
# could not be linked against the shipped runtime at all -- its own (weak)
# cooperative scheduler had to be used instead.
#
# This gate links a self-hosted program against the SHIPPED runtime objects
# (the same way the C host links a win-x64 program: ld.lld plus the bundled
# MinGW runtime) and compares its output with the C host's. The shipped
# runtime's zan_co_ready / zan_co_sched_init are strong symbols, so they win
# over the self-host's weak fallback: the frames really are driven by the
# shipped multi-worker scheduler, and the timer ready hook is the runtime's.
#
# The programs live in tests/selfhost/mt/.
#
# Invoked as:
#   cmake -DZANC=<zanc> -DSELFHOST_SOURCES=<list> -DGEN1=<exe> -DOUTDIR=<dir>
#         -DCLANG=<clang> -DLLD=<ld.lld> -DRUNTIME_DIR=<dir with the .o files>
#         -DMINGW_LIB=<bundled mingw lib dir> -DCASE_DIR=<dir> -DCASES=<list file>
#         [-DWORKDIR=<dir>] -P run_selfhost_mt.cmake
#
# CASES is a text file with one case stem per line (like the differential
# gate's list); the program is <CASE_DIR>/<stem>.zan.

cmake_policy(SET CMP0012 NEW)
if(NOT ZANC OR NOT GEN1 OR NOT OUTDIR OR NOT CLANG OR NOT LLD OR NOT RUNTIME_DIR)
  message(FATAL_ERROR "run_selfhost_mt.cmake: ZANC, GEN1, OUTDIR, CLANG, LLD and RUNTIME_DIR are required")
endif()
if(NOT MINGW_LIB)
  message(FATAL_ERROR "run_selfhost_mt.cmake: MINGW_LIB is required")
endif()
if(NOT CASES)
  message(FATAL_ERROR "run_selfhost_mt.cmake: CASES is required")
endif()
if(NOT CASE_DIR)
  message(FATAL_ERROR "run_selfhost_mt.cmake: CASE_DIR is required")
endif()
if(NOT EXISTS ${CASES})
  message(FATAL_ERROR "run_selfhost_mt.cmake: the case list '${CASES}' does not exist (pass an absolute path)")
endif()

file(MAKE_DIRECTORY ${OUTDIR})

if(NOT WORKDIR)
  get_filename_component(_script_dir "${CMAKE_SCRIPT_MODE_FILE}" DIRECTORY)
  get_filename_component(WORKDIR "${_script_dir}/.." ABSOLUTE)
endif()

# ---- build gen1 (the C host compiles the self-hosted compiler) ----
list(GET SELFHOST_SOURCES 0 _first)
list(REMOVE_AT SELFHOST_SOURCES 0)
execute_process(
  COMMAND ${ZANC} ${_first} ${SELFHOST_SOURCES} --auto-stdlib -o ${GEN1}
  RESULT_VARIABLE build_rc
  OUTPUT_VARIABLE build_out
  ERROR_VARIABLE build_err)
if(NOT build_rc EQUAL 0)
  message(FATAL_ERROR "gen1 build failed (rc=${build_rc})\n${build_out}${build_err}")
endif()

set(_failures 0)

file(STRINGS ${CASES} _case_lines)
set(_cases)
foreach(_c IN LISTS _case_lines)
  string(STRIP "${_c}" _c)
  if(_c STREQUAL "" OR _c MATCHES "^#")
    continue()
  endif()
  set(_case "${CASE_DIR}/${_c}.zan")
  if(NOT EXISTS ${_case})
    message(FATAL_ERROR "run_selfhost_mt.cmake: no such case program: ${_case}")
  endif()
  list(APPEND _cases ${_case})
endforeach()
if(_cases STREQUAL "")
  message(FATAL_ERROR "run_selfhost_mt.cmake: the case list '${CASES}' is empty")
endif()

foreach(_case ${_cases})
  get_filename_component(_name "${_case}" NAME_WE)
  set(_ref "${OUTDIR}/${_name}.host.out")
  set(_self "${OUTDIR}/${_name}.self.out")
  set(_ll "${OUTDIR}/${_name}.self.ll")
  set(_obj "${OUTDIR}/${_name}.self.obj")
  set(_exe "${OUTDIR}/${_name}.self.exe")
  set(_host_exe "${OUTDIR}/${_name}.host.exe")

  # ---- the C host is the reference ----
  execute_process(
    COMMAND ${ZANC} ${_case} --auto-stdlib -o ${_host_exe}
    RESULT_VARIABLE hrc
    OUTPUT_VARIABLE hout
    ERROR_VARIABLE herr
    WORKING_DIRECTORY ${WORKDIR})
  if(NOT hrc EQUAL 0)
    math(EXPR _failures "${_failures} + 1")
    message(SEND_ERROR "FAIL mt: the C host refused ${_case} (rc=${hrc}): ${hout}${herr}")
    continue()
  endif()
  execute_process(
    COMMAND ${_host_exe}
    RESULT_VARIABLE hrun
    OUTPUT_VARIABLE hstdout
    ERROR_VARIABLE hstderr
    WORKING_DIRECTORY ${WORKDIR})
  file(WRITE "${_ref}" "${hstdout}")

  # ---- the self-hosted compiler emits the IR ----
  file(REMOVE ${_ll})
  execute_process(
    COMMAND ${GEN1} ${_ll} ${_case}
    RESULT_VARIABLE grc
    OUTPUT_VARIABLE gout
    ERROR_VARIABLE gerr)
  if(NOT grc EQUAL 0)
    math(EXPR _failures "${_failures} + 1")
    message(SEND_ERROR "FAIL mt: the self-hosted compiler refused ${_case} (rc=${grc}): ${gout}${gerr}")
    continue()
  endif()
  if(NOT EXISTS ${_ll})
    math(EXPR _failures "${_failures} + 1")
    message(SEND_ERROR "FAIL mt: the self-hosted compiler wrote no IR for ${_case}")
    continue()
  endif()

  # ---- the shipped runtime is a MinGW build: compile the IR for that target ----
  execute_process(
    COMMAND ${CLANG} --target=x86_64-w64-windows-gnu -c ${_ll} -o ${_obj}
    RESULT_VARIABLE crc
    OUTPUT_VARIABLE cout
    ERROR_VARIABLE cerr)
  if(NOT crc EQUAL 0)
    math(EXPR _failures "${_failures} + 1")
    message(SEND_ERROR "FAIL mt: the emitted IR did not compile (rc=${crc}): ${cout}${cerr}")
    continue()
  endif()

  set(_crypto_lib "")
  if(EXISTS "${RUNTIME_DIR}/libzancrypto.a")
    set(_crypto_lib "${RUNTIME_DIR}/libzancrypto.a")
  endif()

  # ---- link against the shipped runtime, the way the C host links win-x64 ----
  file(REMOVE ${_exe})
  execute_process(
    COMMAND ${LLD} -m i386pep --stack 268435456 -o ${_exe}
            "${MINGW_LIB}/crt2.o" "${MINGW_LIB}/crtbegin.o" ${_obj}
            "${RUNTIME_DIR}/zanrt_timer.o" "${RUNTIME_DIR}/zanrt_io_mt.o"
            "${RUNTIME_DIR}/zanrt_sync.o" "${RUNTIME_DIR}/zanrt_file.o"
            "${RUNTIME_DIR}/zan_inflate.o" "${RUNTIME_DIR}/zan_embed_api.o"
            ${_crypto_lib}
            "-L${MINGW_LIB}"
            --start-group
            -lmingw32 -lmoldname -lmingwex -lmsvcrt
            -lkernel32 -ladvapi32 -lshell32 -luser32 -lws2_32
            -lgcc -lgcc_eh
            --end-group
    RESULT_VARIABLE lrc
    OUTPUT_VARIABLE lout
    ERROR_VARIABLE lerr)
  if(NOT lrc EQUAL 0)
    math(EXPR _failures "${_failures} + 1")
    message(SEND_ERROR "FAIL mt: the link against the shipped runtime failed (rc=${lrc}): ${lout}${lerr}")
    continue()
  endif()

  # ---- run it and compare with the host ----
  execute_process(
    COMMAND ${_exe}
    RESULT_VARIABLE srun
    OUTPUT_VARIABLE sstdout
    ERROR_VARIABLE sstderr
    WORKING_DIRECTORY ${WORKDIR})
  file(WRITE "${_self}" "${sstdout}")
  if(NOT sstdout STREQUAL hstdout)
    math(EXPR _failures "${_failures} + 1")
    message(SEND_ERROR "FAIL mt: ${_case} on the shipped runtime differs from the C host\n--- host ---\n${hstdout}--- self ---\n${sstdout}")
  else()
    message(STATUS "mt: ${_name} runs on the shipped runtime and matches the C host")
  endif()
endforeach()

message(STATUS "selfhost shipped-runtime gate: ${_failures} failure(s)")
if(_failures GREATER 0)
  message(FATAL_ERROR "selfhost shipped-runtime gate: ${_failures} failure(s)")
endif()
