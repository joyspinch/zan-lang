# Assemble a self-contained MinGW linking toolchain next to zanc so that
# compiling  zanc app.zan -o app.exe  needs only zan: no external clang / gcc /
# MSVC / Windows SDK. zanc emits x86_64-w64-windows-gnu objects and links them
# in-process with the GNU ld + MinGW-w64 runtime placed here (see main.c).
#
# Invoked from CMake as:
#   cmake -DMINGW_ROOT=<root> -DDEST=<dir> -P bundle_toolchain.cmake
#
# Idempotent: if the bundle already exists it is left untouched (the MinGW
# runtime is ~90 MB, so we never re-copy it on incremental relinks).

if(NOT MINGW_ROOT)
    set(MINGW_ROOT "C:/TDM-GCC-64")
endif()

set(LIBDST "${DEST}/mingw/lib")

if(EXISTS "${DEST}/ld.exe" AND EXISTS "${LIBDST}/crt2.o" AND EXISTS "${LIBDST}/libgcc.a")
    message(STATUS "toolchain bundle already present at ${DEST}; skipping")
    return()
endif()

if(NOT EXISTS "${MINGW_ROOT}/bin/ld.exe")
    message(WARNING
        "MinGW-w64 not found at ${MINGW_ROOT}; skipping toolchain bundle. "
        "zanc will fall back to a system clang targeting the mingw ABI. "
        "Set -DZAN_MINGW_ROOT=<path> to enable self-contained linking.")
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
