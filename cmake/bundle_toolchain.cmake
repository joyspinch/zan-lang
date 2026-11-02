# Assemble a self-contained MinGW linking toolchain next to zanc so that
# compiling  zanc app.zan -o app.exe  needs only zan: no external clang / gcc /
# MSVC / Windows SDK. zanc emits x86_64-w64-windows-gnu objects and links them
# using LLVM's ld.lld (GNU ld fallback) + the MinGW-w64 runtime placed here.
#
# Invoked from CMake as:
#   cmake -DMINGW_ROOT=<root> -DDEST=<dir> -P bundle_toolchain.cmake
# MINGW_ROOT empty means "probe": environment (ZAN_MINGW_ROOT / MINGW_ROOT /
# MINGW_HOME), then PATH, then the standard install roots from
# cmake/ZanToolchain.cmake. This script mode has no CMake generator, so the
# same probe module is included directly rather than duplicated here.
#
# Idempotent: if the bundle already exists it is left untouched (the MinGW
# runtime is ~90 MB, so we never re-copy it on incremental relinks).

include("${CMAKE_CURRENT_LIST_DIR}/ZanToolchain.cmake")
if(NOT MINGW_ROOT)
    zan_mingw_roots(_zan_mingw_roots)
    zan_resolve_root(_zan_mingw_probe
        NAME "MinGW-w64 linking toolchain"
        SENTINEL "bin/ld.exe"
        ENV ZAN_MINGW_ROOT MINGW_ROOT MINGW_HOME
        PATH_TOOLS x86_64-w64-mingw32-gcc gcc
        ROOTS ${_zan_mingw_roots})
    set(MINGW_ROOT "${_zan_mingw_probe}")
endif()

set(LIBDST "${DEST}/mingw/lib")

if(LLD_EXECUTABLE AND EXISTS "${LLD_EXECUTABLE}")
    configure_file("${LLD_EXECUTABLE}" "${DEST}/ld.lld.exe" COPYONLY)
endif()

if(EXISTS "${DEST}/ld.exe" AND EXISTS "${LIBDST}/crt2.o" AND EXISTS "${LIBDST}/libgcc.a")
    message(STATUS "toolchain bundle already present at ${DEST}; skipping")
    return()
endif()

if(NOT EXISTS "${MINGW_ROOT}/bin/ld.exe")
    if(_zan_mingw_probe_TRIED)
        zan_probe_failure_message(_zan_mingw_probe _zan_mingw_msg)
        message(WARNING "${_zan_mingw_msg}")
    endif()
    message(WARNING
        "Skipping the self-contained toolchain bundle: zanc will fall back to "
        "a system clang targeting the mingw ABI. Set -DZAN_MINGW_ROOT=<root> "
        "(or the ZAN_MINGW_ROOT environment variable) to enable it.")
    return()
endif()

file(MAKE_DIRECTORY "${LIBDST}")

# 1) The linker (GNU ld from binutils, ~1.7 MB, freely redistributable).
file(COPY "${MINGW_ROOT}/bin/ld.exe" DESTINATION "${DEST}")

# 2) The MinGW-w64 runtime: CRT startup objects + import/static libs.
file(GLOB SYSLIB "${MINGW_ROOT}/x86_64-w64-mingw32/lib/*")
file(COPY ${SYSLIB} DESTINATION "${LIBDST}")

# 3) libgcc + gcc's CRT objects (the gcc version dir varies, so glob it).
file(GLOB GCCDIRS "${MINGW_ROOT}/lib/gcc/x86_64-w64-mingw32/*")
list(GET GCCDIRS 0 GCCDIR)
file(COPY
    "${GCCDIR}/libgcc.a"
    "${GCCDIR}/crtbegin.o"
    "${GCCDIR}/crtend.o"
    DESTINATION "${LIBDST}")

message(STATUS "assembled self-contained MinGW linking toolchain at ${DEST}")
