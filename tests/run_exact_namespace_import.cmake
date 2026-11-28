# Exact-namespace import contract: `using Parent;` MUST NOT implicitly cascade
# into descendant namespaces (`Parent.Child`) in either pull-in mode.
if(NOT DEFINED ZANC OR NOT DEFINED WORK OR NOT DEFINED STDLIB)
  message(FATAL_ERROR "ZANC, WORK and STDLIB are required")
endif()

file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}/packages/Fixture.ExactNs/src/ExactNs/Sub" "${WORK}/app")
file(WRITE "${WORK}/packages/Fixture.ExactNs/zan.pkg"
  "name = \"Fixture.ExactNs\"\nversion = \"1.0.0\"\n")
file(WRITE "${WORK}/packages/Fixture.ExactNs/src/ExactNs/Root.zan" [=[
namespace ExactNs;
public class RootType {
    public static int Value() { return 10; }
}
]=])
file(WRITE "${WORK}/packages/Fixture.ExactNs/src/ExactNs/Sub/Child.zan" [=[
namespace ExactNs.Sub;
public class ChildType {
    public static int Value() { return 32; }
}
]=])
file(WRITE "${WORK}/zan.proj" "name=ExactNsRegression\n")
file(WRITE "${WORK}/app/no_cascade.zan" [=[
using ExactNs;
class Program {
    static int Main() { return ExactNs.Sub.ChildType.Value(); }
}
]=])
file(WRITE "${WORK}/app/explicit_ok.zan" [=[
using ExactNs;
using ExactNs.Sub;
class Program {
    static int Main() { return RootType.Value() + ChildType.Value() - 42; }
}
]=])

foreach(mode filtered unfiltered)
  if(mode STREQUAL "unfiltered")
    set(ENV{ZAN_NO_PULLIN_FILTER} 1)
  else()
    unset(ENV{ZAN_NO_PULLIN_FILTER})
  endif()
  set(neg_exe "${WORK}/neg_${mode}.exe")
  file(REMOVE "${neg_exe}")
  execute_process(COMMAND "${ZANC}" "${WORK}/app/no_cascade.zan"
    --stdlib-path "${STDLIB}" --auto-stdlib -o "${neg_exe}"
    WORKING_DIRECTORY "${WORK}"
    RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 60)
  if("${rc}" STREQUAL "0" OR EXISTS "${neg_exe}")
    message(FATAL_ERROR "${mode}: using ExactNs unexpectedly cascaded into ExactNs.Sub:\n${out}${err}")
  endif()

  set(pos_exe "${WORK}/pos_${mode}.exe")
  file(REMOVE "${pos_exe}")
  execute_process(COMMAND "${ZANC}" "${WORK}/app/explicit_ok.zan"
    --stdlib-path "${STDLIB}" --auto-stdlib -o "${pos_exe}"
    WORKING_DIRECTORY "${WORK}"
    RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 60)
  if(NOT "${rc}" STREQUAL "0" OR NOT EXISTS "${pos_exe}")
    message(FATAL_ERROR "${mode}: explicit using ExactNs + ExactNs.Sub failed (${rc}):\n${out}${err}")
  endif()
  execute_process(COMMAND "${pos_exe}" RESULT_VARIABLE run_rc TIMEOUT 15)
  if(NOT "${run_rc}" STREQUAL "0")
    message(FATAL_ERROR "${mode}: explicit_ok executable exited ${run_rc}")
  endif()
endforeach()
message(STATUS "exact-namespace import contract verified (filtered & unfiltered)")
