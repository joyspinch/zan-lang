# Compile a Zan source into a library (.dll/.so/.dylib/.a) and validate it.
#
# Unlike the runnable conformance cases, a library has no entry point, so this
# harness checks the produced artifact instead of running it:
#   - MODE=dll:    PE header (MZ) + optional consumer round-trip
#   - MODE=so:     ELF magic
#   - MODE=dylib:  Mach-O magic
#   - MODE=static: ar archive magic (!<arch>)
#
# Invoked as:
#   cmake -DZANC=<zanc> -DSRC=<file.zan> -DOUT=<lib path> -DMODE=<dll|so|dylib|static>
#         [-DTARGET=<cross target name>]
#         [-DCONSUMER=<consumer.zan> -DEXPECTED=<expected.out>]   (dll mode)
#         [-DSTDLIB_STAMP=<stamp>]
#         -P run_emit_lib.cmake
#
# The consumer round-trip (dll mode) compiles a program that links the library
# via [DllImport] and compares its stdout to the golden file, proving the
# public method is exported and the import library works end to end.

cmake_policy(SET CMP0012 NEW)

if(NOT ZANC OR NOT SRC OR NOT OUT OR NOT MODE)
  message(FATAL_ERROR "run_emit_lib.cmake: ZANC, SRC, OUT and MODE are required")
endif()


# ---- up-to-date check (same rule as run_case.cmake) ----
function(zan_artifact_is_current out_var artifact)
  set(${out_var} FALSE PARENT_SCOPE)
  if(NOT EXISTS ${artifact})
    return()
  endif()
  if(${SRC} IS_NEWER_THAN ${artifact})
    return()
  endif()
  if(${ZANC} IS_NEWER_THAN ${artifact})
    return()
  endif()
  if(STDLIB_STAMP AND EXISTS ${STDLIB_STAMP} AND ${STDLIB_STAMP} IS_NEWER_THAN ${artifact})
    return()
  endif()
  set(${out_var} TRUE PARENT_SCOPE)
endfunction()


# ---- compile the library ----
zan_artifact_is_current(_current ${OUT})
if(NOT _current)
  set(_args ${ZANC} ${SRC} --auto-stdlib)
  if(TARGET)
    list(APPEND _args --target ${TARGET})
  endif()
  list(APPEND _args -o ${OUT})
  execute_process(
    COMMAND ${_args}
    RESULT_VARIABLE compile_rc
    OUTPUT_VARIABLE compile_out
    ERROR_VARIABLE  compile_err)
  if(NOT compile_rc EQUAL 0)
    message(FATAL_ERROR
      "library compile failed (rc=${compile_rc})\n${compile_out}${compile_err}")
  endif()
endif()
if(NOT EXISTS ${OUT})
  message(FATAL_ERROR "library artifact missing: ${OUT}")
endif()


# ---- format magic check ----
file(READ ${OUT} _hex HEX)
if(MODE STREQUAL "dll")
  string(SUBSTRING ${_hex} 0 4 _magic)
  if(NOT _magic STREQUAL "4d5a")
    message(FATAL_ERROR "DLL ${OUT} does not start with MZ (got ${_magic})")
  endif()
elseif(MODE STREQUAL "so")
  string(SUBSTRING ${_hex} 0 8 _magic)
  if(NOT _magic STREQUAL "7f454c46")
    message(FATAL_ERROR "${OUT} is not an ELF file (got ${_magic})")
  endif()
elseif(MODE STREQUAL "dylib")
  string(SUBSTRING ${_hex} 0 8 _magic)
  if(NOT _magic STREQUAL "cffaedfe" AND NOT _magic STREQUAL "feedfacf")
    message(FATAL_ERROR "${OUT} is not a Mach-O file (got ${_magic})")
  endif()
elseif(MODE STREQUAL "static")
  string(SUBSTRING ${_hex} 0 14 _magic)
  if(NOT _magic STREQUAL "213c617263683e")
    message(FATAL_ERROR "${OUT} is not an ar archive (got ${_magic})")
  endif()
else()
  message(FATAL_ERROR "run_emit_lib.cmake: unknown MODE '${MODE}'")
endif()


# ---- consumer round-trip (dll mode only) ----
if(MODE STREQUAL "dll" AND CONSUMER AND EXPECTED)
  get_filename_component(_libdir ${OUT} DIRECTORY)
  set(_cexe ${OUT}.consumer.exe)
  # Always relink the consumer: its output depends on the import library too,
  # and library tests are cheap (the library compile itself is cached above).
  execute_process(
    COMMAND ${ZANC} ${CONSUMER} --auto-stdlib -o ${_cexe} -L${_libdir}
    RESULT_VARIABLE crc
    OUTPUT_VARIABLE cout
    ERROR_VARIABLE  cerr)
  if(NOT crc EQUAL 0)
    message(FATAL_ERROR "consumer compile failed (rc=${crc})\n${cout}${cerr}")
  endif()

  # Windows launch retry: AV / loader races can transiently hold a fresh image.
  set(_run_attempt 0)
  while(TRUE)
    math(EXPR _run_attempt "${_run_attempt} + 1")
    execute_process(
      COMMAND ${_cexe}
      RESULT_VARIABLE run_rc
      OUTPUT_VARIABLE actual
      ENCODING UTF-8)
    if((run_rc MATCHES "[cC]0000043" OR run_rc MATCHES "[cC]0000022")
       AND _run_attempt LESS 6)
      execute_process(COMMAND ${CMAKE_COMMAND} -E sleep 0.4)
      continue()
    endif()
    break()
  endwhile()
  if(NOT run_rc EQUAL 0)
    file(REMOVE ${_cexe})
    message(FATAL_ERROR "consumer exited with ${run_rc}\noutput:\n${actual}")
  endif()

  file(READ ${EXPECTED} expected)
  string(REPLACE "\r\n" "\n" expected "${expected}")
  string(REPLACE "\r\n" "\n" actual   "${actual}")
  string(REGEX REPLACE "[ \t\r\n]+$" "" expected "${expected}")
  string(REGEX REPLACE "[ \t\r\n]+$" "" actual   "${actual}")
  if(NOT actual STREQUAL expected)
    file(REMOVE ${_cexe})
    message(FATAL_ERROR
      "consumer output mismatch for ${CONSUMER}\n--- expected ---\n${expected}"
      "\n--- actual ---\n${actual}")
  endif()
endif()

message(STATUS "OK: ${MODE} library ${OUT}")
