# End-to-end self-hosting harness.
#
# Proves that the Zan-written compiler (ZC0) compiles a real Zan source (INPUT)
# to a native executable whose output matches the same source compiled by the
# reference C `zanc`:
#
#   1. C zanc compiles the Zan-written compiler        (ZC0    -> zc0.exe)
#   2. run it; its stdout is real LLVM IR              (zc0.exe -> out.ll)
#   3. clang lowers that IR to a native executable     (out.ll  -> prog.exe)
#   4. run prog.exe                                     -> zan_out
#   5. C zanc compiles INPUT directly                   (INPUT  -> ref.exe)
#   6. run ref.exe                                      -> ref_out
#   7. assert zan_out == ref_out
#
# Invoked as:
#   cmake -DZANC=<zanc> -DZC0=<zc0.zan> -DINPUT=<input.zan> \
#         -DWORKDIR=<dir> -DBINDIR=<dir> -DCLANG=<clang> [-DEXE_EXT=.exe] \
#         -P run_selfcompile.cmake

if(NOT ZANC OR NOT ZC0 OR NOT INPUT OR NOT WORKDIR OR NOT BINDIR OR NOT CLANG)
  message(FATAL_ERROR "run_selfcompile.cmake: ZANC, ZC0, INPUT, WORKDIR, BINDIR, CLANG required")
endif()

set(zc0_exe ${BINDIR}/self_zc0_compiler${EXE_EXT})
set(ll      ${BINDIR}/self_zc0_out.ll)
set(prog    ${BINDIR}/self_zc0_prog${EXE_EXT})
set(ref     ${BINDIR}/self_zc0_ref${EXE_EXT})

# 1. build the Zan-written compiler with the C zanc
execute_process(
  COMMAND ${ZANC} ${ZC0} -o ${zc0_exe}
  RESULT_VARIABLE rc OUTPUT_VARIABLE o ERROR_VARIABLE e)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "compile zc0 failed (rc=${rc})\n${o}${e}")
endif()

# 2. run it: stdout is the emitted LLVM IR
execute_process(
  COMMAND ${zc0_exe}
  WORKING_DIRECTORY ${WORKDIR}
  OUTPUT_FILE ${ll}
  RESULT_VARIABLE rc ERROR_VARIABLE e)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "emit IR failed (rc=${rc})\n${e}")
endif()

# 3. clang lowers the IR to a native executable
execute_process(
  COMMAND ${CLANG} ${ll} -o ${prog}
  RESULT_VARIABLE rc OUTPUT_VARIABLE o ERROR_VARIABLE e)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "clang lowering IR failed (rc=${rc})\n${o}${e}")
endif()

# 4. run the Zan-compiled program
execute_process(
  COMMAND ${prog}
  RESULT_VARIABLE rc OUTPUT_VARIABLE zan_out)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "Zan-compiled program exited with ${rc}")
endif()

# 5+6. reference: C zanc compiles the same source directly
execute_process(
  COMMAND ${ZANC} ${INPUT} -o ${ref}
  RESULT_VARIABLE rc OUTPUT_VARIABLE o ERROR_VARIABLE e)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "compile reference failed (rc=${rc})\n${o}${e}")
endif()
execute_process(
  COMMAND ${ref}
  RESULT_VARIABLE rc OUTPUT_VARIABLE ref_out)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "reference program exited with ${rc}")
endif()

# 7. compare (normalize CRLF and trailing whitespace)
string(REPLACE "\r\n" "\n" zan_out "${zan_out}")
string(REPLACE "\r\n" "\n" ref_out "${ref_out}")
string(REGEX REPLACE "[ \t\r\n]+$" "" zan_out "${zan_out}")
string(REGEX REPLACE "[ \t\r\n]+$" "" ref_out "${ref_out}")

if(NOT zan_out STREQUAL ref_out)
  message(FATAL_ERROR
    "self-hosting mismatch for ${INPUT}\n--- Zan-compiled ---\n${zan_out}\n--- C zanc ---\n${ref_out}")
endif()

message(STATUS "OK self-compile: Zan-compiled output == C-zanc output\n${zan_out}")
