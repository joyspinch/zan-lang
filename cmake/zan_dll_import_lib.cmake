# Regenerates the GNU import library for a Windows DLL from its own export
# table, so consumers linking with GNU ld (zanc's bundled linker resolves
# [DllImport] libs this way) get a lib in step with the DLL's exports. The
# MSVC toolchain only emits an MSVC-format .lib, which that ld cannot read.
#
#   cmake -DZAN_OBJDUMP=<llvm-objdump> -DDLL=<dll> -DOUT=<libzan_gui.dll.a>
#         -P zan_dll_import_lib.cmake
#
# Mirrors the second half of scripts/build_gui_driver.ps1 (dump exports ->
# .def -> llvm-dlltool) but writes only the import library, next to the DLL.

if(NOT ZAN_OBJDUMP OR NOT DLL OR NOT OUT)
  message(FATAL_ERROR "zan_dll_import_lib.cmake: ZAN_OBJDUMP, DLL and OUT are required")
endif()

set(_def "${OUT}.def")
execute_process(COMMAND ${ZAN_OBJDUMP} -p ${DLL} OUTPUT_VARIABLE _dump RESULT_VARIABLE _rc)
if(NOT _rc EQUAL 0 OR _dump STREQUAL "")
  message(WARNING "zan_dll_import_lib: cannot dump ${DLL}; keeping any existing import lib")
  return()
endif()

set(_names "")
set(_in_exports FALSE)
# CMake lists split on semicolons, so turn the dump's newlines into list
# separators before iterating (and drop CR first).
string(REPLACE "\r" "" _dump "${_dump}")
string(REPLACE "\n" ";" _lines "${_dump}")
foreach(_line IN LISTS _lines)
  if(_line MATCHES "Export Table:")
    set(_in_exports TRUE)
    continue()
  endif()
  if(_in_exports AND _line MATCHES "^ +[0-9]+ +0x[0-9a-fA-F]+ +([A-Za-z0-9_]+) *$")
    list(APPEND _names "${CMAKE_MATCH_1}")
  endif()
endforeach()
if(_names STREQUAL "")
  message(WARNING "zan_dll_import_lib: no exports found in ${DLL}")
  return()
endif()

get_filename_component(_dllname ${DLL} NAME)
file(WRITE ${_def} "LIBRARY ${_dllname}\nEXPORTS\n")
foreach(_n IN LISTS _names)
  file(APPEND ${_def} "${_n}\n")
endforeach()

find_program(ZAN_DLLTOOL llvm-dlltool)
if(NOT ZAN_DLLTOOL)
  message(WARNING "zan_dll_import_lib: llvm-dlltool not found; keeping any existing import lib")
  return()
endif()
execute_process(COMMAND ${ZAN_DLLTOOL} -m i386:x86-64 -d ${_def} -l ${OUT} -D ${_dllname}
                RESULT_VARIABLE _rc)
if(NOT _rc EQUAL 0)
  message(WARNING "zan_dll_import_lib: llvm-dlltool failed; keeping any existing import lib")
  return()
endif()
file(REMOVE ${_def})
list(LENGTH _names _count)
message(STATUS "zan_dll_import_lib: ${OUT} (${_dllname}, ${_count} exports)")
