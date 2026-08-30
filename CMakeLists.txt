cmake_minimum_required(VERSION 3.16)
# CXX is enabled so that targets linking LLVM's C++ static libraries can be
# driven by the C++ linker where needed (see the MinGW case below).
project(zan-lang C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Compiler warnings
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# ---- Version (single source of truth: the root VERSION file) ----
# Every binary reports this string (zanc --version, LSP serverInfo, About box).
# Edit VERSION only; the generated zan_version.h propagates it into the build.
file(READ "${CMAKE_SOURCE_DIR}/VERSION" ZAN_VERSION)
string(STRIP "${ZAN_VERSION}" ZAN_VERSION)
configure_file(
    "${CMAKE_SOURCE_DIR}/cmake/zan_version.h.in"
    "${CMAKE_BINARY_DIR}/generated/zan_version.h" @ONLY)
include_directories("${CMAKE_BINARY_DIR}/generated")
# The standard library ships as source, so its copy of the version lives in the
# source tree (System.ZanVersion; printed by the server start-up banner).
configure_file(
    "${CMAKE_SOURCE_DIR}/stdlib/System/ZanVersion.zan.in"
    "${CMAKE_SOURCE_DIR}/stdlib/System/ZanVersion.zan" @ONLY)
message(STATUS "Zan version: ${ZAN_VERSION}")

# ---- Default Windows executable icon ----
# assets/zan.ico is compiled into zanc as a byte array, so every Windows
# executable it produces carries an icon even when no --icon is passed (the
# IDE's Run/Build/Publish flows and hand-written zanc invocations alike). The
# icon travels inside the compiler, so no loose assets folder has to ship with
# a redistributable toolchain.
set(ZAN_DEFAULT_ICON "${CMAKE_SOURCE_DIR}/assets/zan.ico")
set(ZAN_DEFAULT_ICON_H "${CMAKE_BINARY_DIR}/generated/zan_default_icon.h")
if(EXISTS "${ZAN_DEFAULT_ICON}")
    file(READ "${ZAN_DEFAULT_ICON}" ZAN_DEFAULT_ICON_HEX HEX)
    string(REGEX REPLACE "(..)" "0x\\1," ZAN_DEFAULT_ICON_BYTES "${ZAN_DEFAULT_ICON_HEX}")
else()
    message(WARNING "assets/zan.ico missing; produced .exe files get no default icon")
    set(ZAN_DEFAULT_ICON_BYTES "")
endif()
file(WRITE "${ZAN_DEFAULT_ICON_H}"
"/* Generated from assets/zan.ico by CMake -- do not edit. */\n"
"#ifndef ZAN_DEFAULT_ICON_H\n"
"#define ZAN_DEFAULT_ICON_H\n"
"static const unsigned char zan_default_icon[] = { ${ZAN_DEFAULT_ICON_BYTES} 0 };\n"
"#define ZAN_DEFAULT_ICON_LEN (sizeof(zan_default_icon) - 1)\n"
"#endif\n")


# ---- Find LLVM ----
find_package(LLVM REQUIRED CONFIG)
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
# irgen emits IR for opaque pointers; typed-pointer LLVM (<= 14) rejects nearly
# every generic/closure/field store in the verifier, so fail loudly at configure
# time instead of at the first compile.
if(LLVM_PACKAGE_VERSION VERSION_LESS 15.0)
    message(FATAL_ERROR
        "LLVM ${LLVM_PACKAGE_VERSION} is too old: zanc requires LLVM >= 15 "
        "(opaque pointers). Point -DLLVM_DIR at a newer LLVM, e.g. "
        "-DLLVM_DIR=/usr/lib/llvm-15/cmake -- see docs/BUILD_TOOLCHAIN.md.")
endif()
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

# Cross-compile backends beyond x86/aarch64 are optional: not every LLVM
# distribution builds them (e.g. mozbuild clang omits RISCV). Link �� and
# initialize, via HAVE_LLVM_* defines �� only the ones this LLVM provides.
set(ZAN_LLVM_COMPONENTS
    core support analysis bitwriter target
    x86codegen x86asmparser x86desc x86info
    aarch64codegen aarch64asmparser aarch64desc aarch64info
    passes
)
foreach(_opt_target ARM WebAssembly RISCV)
    if("${_opt_target}" IN_LIST LLVM_TARGETS_TO_BUILD)
        string(TOLOWER "${_opt_target}" _t)
        list(APPEND ZAN_LLVM_COMPONENTS
            ${_t}codegen ${_t}asmparser ${_t}desc ${_t}info)
        string(TOUPPER "${_opt_target}" _T)
        add_compile_definitions(ZAN_HAVE_LLVM_${_T}=1)
    else()
        message(STATUS "LLVM target ${_opt_target} not available; cross-compilation to it is disabled")
    endif()
endforeach()
llvm_map_components_to_libnames(LLVM_LIBS ${ZAN_LLVM_COMPONENTS})

# ---- Compiler executable ----
add_executable(zanc
    src/compiler/main.c
    src/compiler/arena.c
    src/compiler/diag.c
    src/compiler/lexer.c
    src/compiler/ast.c
    src/compiler/parser.c
    src/compiler/genrun.c
    src/compiler/genmeta.c
    src/common/json.c
    src/compiler/nsresolve.c
    src/compiler/binder.c
    src/compiler/checker.c
    src/compiler/definite.c
    src/compiler/irgen.c
    src/compiler/optimizer.c
    src/compiler/incremental.c
    src/compiler/package.c
    src/compiler/stdlib_ext.c
    src/compiler/crosscomp.c
    src/compiler/winres.c
    src/compiler/embedres.c
    src/compiler/builtin_api.c
    src/compiler/symbols.c
)
target_include_directories(zanc PRIVATE src/compiler ${LLVM_INCLUDE_DIRS})
if(MSVC)
    target_link_libraries(zanc PRIVATE ${LLVM_LIBS} winhttp)
elseif(WIN32)
    # MinGW: LLVM's static libs are C++ and pull in system libraries. Link with
    # the C++ driver so libstdc++ is resolved correctly, and add the LLVM system
    # libraries explicitly.
    target_link_libraries(zanc PRIVATE ${LLVM_LIBS} ${LLVM_SYSTEM_LIBS} winhttp)
    set_target_properties(zanc PROPERTIES LINKER_LANGUAGE CXX)
else()
    target_link_libraries(zanc PRIVATE ${LLVM_LIBS} ${LLVM_SYSTEM_LIBS} stdc++ m pthread)
endif()
target_compile_definitions(zanc PRIVATE ${LLVM_DEFINITIONS})

# ---- Socket-async reactor object (S4b-2) ----
# Programs that use socket await (await Socket.ReadReady/WriteReady) link a
# small readiness reactor built from src/runtime/rt_io.c (epoll/kqueue/IOCP).
# It is compiled here as a standalone object (ZAN_IO_STACKLESS_ONLY drops the
# rt_sched fiber half, so it depends only on zan_co_ready, which the produced
# program provides inline). zanc injects this object into the link only when a
# program actually uses socket await (see src/compiler/main.c). The object must
# match the ABI of zanc's produced objects: on Windows zanc emits
# x86_64-w64-windows-gnu (MinGW) objects and links them with the bundled GNU ld,
# so compile the reactor with a MinGW gcc (NOT clang-cl/MSVC); elsewhere the
# system C compiler already matches.
if(WIN32)
    find_program(ZAN_RT_IO_CC NAMES gcc x86_64-w64-mingw32-gcc
                 HINTS ${ZAN_MINGW_ROOT}/bin C:/TDM-GCC-64/bin)
    if(NOT ZAN_RT_IO_CC)
        set(ZAN_RT_IO_CC ${CMAKE_C_COMPILER})
    endif()
else()
    set(ZAN_RT_IO_CC ${CMAKE_C_COMPILER})
endif()
set(ZAN_RT_IO_OBJ "${CMAKE_BINARY_DIR}/zanrt_io${CMAKE_C_OUTPUT_EXTENSION}")
add_custom_command(
    OUTPUT ${ZAN_RT_IO_OBJ}
    COMMAND ${ZAN_RT_IO_CC} -DZAN_IO_STACKLESS_ONLY
            -I ${CMAKE_SOURCE_DIR}/src/runtime -O2
            -c ${CMAKE_SOURCE_DIR}/src/runtime/rt_io.c -o ${ZAN_RT_IO_OBJ}
    DEPENDS ${CMAKE_SOURCE_DIR}/src/runtime/rt_io.c
            ${CMAKE_SOURCE_DIR}/src/runtime/rt_io.h
            ${CMAKE_SOURCE_DIR}/src/runtime/rt_co.h
            ${CMAKE_SOURCE_DIR}/src/runtime/rt_crash.h
    COMMENT "Building socket-async reactor object (zanrt_io)"
    VERBATIM)
add_custom_target(zanrt_io ALL DEPENDS ${ZAN_RT_IO_OBJ})
add_dependencies(zanc zanrt_io)
target_compile_definitions(zanc PRIVATE ZAN_RT_IO_OBJ="${ZAN_RT_IO_OBJ}")

# ---- Unified timer runtime (S4b-2: Task.Delay / Timer min-heap) ----
# irgen emits zan_timer_* calls from every program's inline coroutine driver
# (and rt_io.c calls them too), so every native-linked program needs this
# object; zanc injects it unconditionally on native builds (see main.c).
# Compiled with the same compiler as the reactor so the object matches the
# ABI of zanc's emitted objects.
set(ZAN_RT_TIMER_OBJ "${CMAKE_BINARY_DIR}/zanrt_timer${CMAKE_C_OUTPUT_EXTENSION}")
add_custom_command(
    OUTPUT ${ZAN_RT_TIMER_OBJ}
    COMMAND ${ZAN_RT_IO_CC} -std=c11
            -I ${CMAKE_SOURCE_DIR}/src/runtime -O2
            -c ${CMAKE_SOURCE_DIR}/src/runtime/rt_timer.c -o ${ZAN_RT_TIMER_OBJ}
    DEPENDS ${CMAKE_SOURCE_DIR}/src/runtime/rt_timer.c
            ${CMAKE_SOURCE_DIR}/src/runtime/rt_timer.h
            ${CMAKE_SOURCE_DIR}/src/runtime/rt_crash.h
    COMMENT "Building unified timer runtime object (zanrt_timer)"
    VERBATIM)
add_custom_target(zanrt_timer ALL DEPENDS ${ZAN_RT_TIMER_OBJ})
add_dependencies(zanc zanrt_timer)
target_compile_definitions(zanc PRIVATE ZAN_RT_TIMER_OBJ="${ZAN_RT_TIMER_OBJ}")
add_custom_command(TARGET zanc POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${ZAN_RT_TIMER_OBJ} $<TARGET_FILE_DIR:zanc>/zanrt_timer${CMAKE_C_OUTPUT_EXTENSION}
    VERBATIM)

# Generic embedded-resource read API (src/runtime/zan_embed_api.c). The data
# table is registered by a generated object (scripts/gen_embed.ps1) when a
# program embeds resources; the API object itself is injected whenever a
# program *references* the zan_embed_* functions (e.g. any GUI program calling
# Skin.EmbedRead), so linking succeeds even with nothing embedded (reads return
# empty). Mirrors the zanrt_io object pattern; compiled with the same MinGW C
# compiler so the object matches the ABI of zanc's emitted objects.
set(ZAN_EMBED_OBJ "${CMAKE_BINARY_DIR}/zan_embed_api${CMAKE_C_OUTPUT_EXTENSION}")
add_custom_command(
    OUTPUT ${ZAN_EMBED_OBJ}
    COMMAND ${ZAN_RT_IO_CC} -O2
            -c ${CMAKE_SOURCE_DIR}/src/runtime/zan_embed_api.c
            -o ${ZAN_EMBED_OBJ}
    DEPENDS ${CMAKE_SOURCE_DIR}/src/runtime/zan_embed_api.c
    COMMENT "Building embedded-resource API object (zan_embed_api)"
    VERBATIM)
add_custom_target(zan_embed_api ALL DEPENDS ${ZAN_EMBED_OBJ})
add_dependencies(zanc zan_embed_api)
target_compile_definitions(zanc PRIVATE ZAN_EMBED_OBJ="${ZAN_EMBED_OBJ}")

# Multi-worker variant of the reactor: same source compiled with -DZAN_CO_DRIVER
# so it also supplies the thread-pool cooperative coroutine driver. zanc links
# this object (instead of the single-thread zanrt_io) when a program is built
# with --async-workers; the emitted program then omits its inline driver.
set(ZAN_RT_IO_MT_OBJ "${CMAKE_BINARY_DIR}/zanrt_io_mt${CMAKE_C_OUTPUT_EXTENSION}")
add_custom_command(
    OUTPUT ${ZAN_RT_IO_MT_OBJ}
    COMMAND ${ZAN_RT_IO_CC} -DZAN_IO_STACKLESS_ONLY -DZAN_CO_DRIVER
            -I ${CMAKE_SOURCE_DIR}/src/runtime -O2
            -c ${CMAKE_SOURCE_DIR}/src/runtime/rt_io.c -o ${ZAN_RT_IO_MT_OBJ}
    DEPENDS ${CMAKE_SOURCE_DIR}/src/runtime/rt_io.c
            ${CMAKE_SOURCE_DIR}/src/runtime/rt_io.h
            ${CMAKE_SOURCE_DIR}/src/runtime/rt_co.h
            ${CMAKE_SOURCE_DIR}/src/runtime/rt_crash.h
    COMMENT "Building multi-worker reactor object (zanrt_io_mt)"
    VERBATIM)
add_custom_target(zanrt_io_mt ALL DEPENDS ${ZAN_RT_IO_MT_OBJ})
add_dependencies(zanc zanrt_io_mt)
target_compile_definitions(zanc PRIVATE ZAN_RT_IO_MT_OBJ="${ZAN_RT_IO_MT_OBJ}")
add_custom_command(TARGET zanc POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${ZAN_RT_IO_MT_OBJ} $<TARGET_FILE_DIR:zanc>/zanrt_io_mt${CMAKE_C_OUTPUT_EXTENSION}
    VERBATIM)
# Also keep a copy next to the zanc binary so a shipped compiler can find it.
add_custom_command(TARGET zanc POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${ZAN_RT_IO_OBJ} $<TARGET_FILE_DIR:zanc>/zanrt_io${CMAKE_C_OUTPUT_EXTENSION}
    VERBATIM)

# ---- Small-object allocator (--fast-alloc) ----
# Per-thread caches, size-class free lists and owner-directed cross-thread
# frees in front of the CRT's malloc (src/runtime/rt_mem.c). zanc links this
# object -- with --wrap=malloc/free/calloc/realloc -- only for a program built
# with --fast-alloc, because the wrap applies to the whole image.
set(ZAN_RT_MEM_OBJ "${CMAKE_BINARY_DIR}/zanrt_mem${CMAKE_C_OUTPUT_EXTENSION}")
add_custom_command(
    OUTPUT ${ZAN_RT_MEM_OBJ}
    COMMAND ${ZAN_RT_IO_CC} -std=c11
            -I ${CMAKE_SOURCE_DIR}/src/runtime -O2
            -c ${CMAKE_SOURCE_DIR}/src/runtime/rt_mem.c -o ${ZAN_RT_MEM_OBJ}
    DEPENDS ${CMAKE_SOURCE_DIR}/src/runtime/rt_mem.c
    COMMENT "Building small-object allocator object (zanrt_mem)"
    VERBATIM)
add_custom_target(zanrt_mem ALL DEPENDS ${ZAN_RT_MEM_OBJ})
add_dependencies(zanc zanrt_mem)
target_compile_definitions(zanc PRIVATE ZAN_RT_MEM_OBJ="${ZAN_RT_MEM_OBJ}")
add_custom_command(TARGET zanc POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${ZAN_RT_MEM_OBJ} $<TARGET_FILE_DIR:zanc>/zanrt_mem${CMAKE_C_OUTPUT_EXTENSION}
    VERBATIM)

# ---- Atomics and cross-process shared-memory table runtime ----
set(ZAN_RT_SYNC_OBJ "${CMAKE_BINARY_DIR}/zanrt_sync${CMAKE_C_OUTPUT_EXTENSION}")
add_custom_command(
    OUTPUT ${ZAN_RT_SYNC_OBJ}
    COMMAND ${ZAN_RT_IO_CC} -std=c11
            -I ${CMAKE_SOURCE_DIR}/src/runtime -O2
            -c ${CMAKE_SOURCE_DIR}/src/runtime/rt_sync.c -o ${ZAN_RT_SYNC_OBJ}
    DEPENDS ${CMAKE_SOURCE_DIR}/src/runtime/rt_sync.c
            ${CMAKE_SOURCE_DIR}/src/runtime/rt_sync.h
    COMMENT "Building atomic/shared-memory runtime object (zanrt_sync)"
    VERBATIM)
add_custom_target(zanrt_sync ALL DEPENDS ${ZAN_RT_SYNC_OBJ})
add_dependencies(zanc zanrt_sync)
target_compile_definitions(zanc PRIVATE ZAN_RT_SYNC_OBJ="${ZAN_RT_SYNC_OBJ}")
add_custom_command(TARGET zanc POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${ZAN_RT_SYNC_OBJ} $<TARGET_FILE_DIR:zanc>/zanrt_sync${CMAKE_C_OUTPUT_EXTENSION}
    VERBATIM)

# ---- File metadata + stream-IO runtime (System.IO.FileInfo/FileStream) ----
# Split out of rt_sync.c so a file-IO program links this small object instead
# of dragging in the atomics/threads/shared-table runtime. The split is what
# lets a wasm32 (WASI) cross-build link file-IO programs at all: this file is
# pure libc + a mutex (see toolchain/wasm32/zanrt_file.o), while rt_sync.c
# cannot be built for wasm (no pthread/shm).
set(ZAN_RT_FILE_OBJ "${CMAKE_BINARY_DIR}/zanrt_file${CMAKE_C_OUTPUT_EXTENSION}")
add_custom_command(
    OUTPUT ${ZAN_RT_FILE_OBJ}
    COMMAND ${ZAN_RT_IO_CC} -std=c11
            -I ${CMAKE_SOURCE_DIR}/src/runtime -O2
            -c ${CMAKE_SOURCE_DIR}/src/runtime/rt_file.c -o ${ZAN_RT_FILE_OBJ}
    DEPENDS ${CMAKE_SOURCE_DIR}/src/runtime/rt_file.c
    COMMENT "Building file IO runtime object (zanrt_file)"
    VERBATIM)
add_custom_target(zanrt_file ALL DEPENDS ${ZAN_RT_FILE_OBJ})
add_dependencies(zanc zanrt_file)
target_compile_definitions(zanc PRIVATE ZAN_RT_FILE_OBJ="${ZAN_RT_FILE_OBJ}")
add_custom_command(TARGET zanc POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${ZAN_RT_FILE_OBJ} $<TARGET_FILE_DIR:zanc>/zanrt_file${CMAKE_C_OUTPUT_EXTENSION}
    VERBATIM)

# ---- Self-contained linking toolchain (Windows) ----
# Bundle a small GNU ld + the redistributable MinGW-w64 runtime next to zanc so
# that `zanc app.zan -o app.exe` needs only zan: no external clang / gcc / MSVC /
# Windows SDK. Objects are emitted with the x86_64-w64-windows-gnu ABI and linked
# in-process against this bundle (see src/compiler/main.c). If the bundle is
# absent at runtime, zanc falls back to a system clang targeting the same ABI.
if(WIN32)
    set(ZAN_MINGW_ROOT "C:/TDM-GCC-64" CACHE PATH
        "MinGW-w64 root used to assemble the bundled linking toolchain")
    option(ZAN_BUNDLE_TOOLCHAIN "Bundle GNU ld + MinGW runtime next to zanc" ON)
    if(ZAN_BUNDLE_TOOLCHAIN)
        add_custom_command(TARGET zanc POST_BUILD
            COMMAND ${CMAKE_COMMAND}
                    -DMINGW_ROOT=${ZAN_MINGW_ROOT}
                    -DDEST=$<TARGET_FILE_DIR:zanc>
                    -P ${CMAKE_SOURCE_DIR}/cmake/bundle_toolchain.cmake
            COMMENT "Assembling self-contained MinGW linking toolchain"
            VERBATIM)
    endif()
endif()

# ---- Linux cross-compilation sysroots ----
# Copy the bundled musl sysroots (committed at toolchain/linux-musl/ for
# x86_64 and toolchain/linux-arm64/ for aarch64) next to zanc so that
# --target linux-musl / linux-x64 / linux-arm64 can static-link ELF binaries
# without any external toolchain.  Each sysroot contains crt*.o, libc.a, a
# pre-built zanrt_io.o for socket-async programs, zanrt_sync.o for
# atomics/shared-memory programs, and zanrt_timer.o, which every program needs
# (its inline coroutine driver calls zan_timer_*).
#
# These copies are collected into one ALL target instead of zanc POST_BUILD
# commands: POST_BUILD runs only when zanc itself relinks, so rebuilding a
# runtime object (scripts/build_linux_rt.sh, scripts/build_cross_rt.cmd) left
# the stale copy next to zanc and cross-links kept failing on symbols the
# rebuilt object had (e.g. "undefined symbol: zan_co_live_add"). An ALL target
# re-checks copy_if_different on every build.
set(_cross_sysroot_cmds "")
foreach(_linux_sub linux-musl linux-arm64 linux-riscv64)
    set(_linux_sys "${CMAKE_SOURCE_DIR}/toolchain/${_linux_sub}")
    if(EXISTS "${_linux_sys}/libc.a")
        list(APPEND _cross_sysroot_cmds
            COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:zanc>/${_linux_sub}
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${_linux_sys}/crt1.o" "${_linux_sys}/crti.o"
                    "${_linux_sys}/crtn.o" "${_linux_sys}/libc.a"
                    "${_linux_sys}/zanrt_io.o" "${_linux_sys}/zanrt_sync.o"
                    "${_linux_sys}/zanrt_timer.o"
                    $<TARGET_FILE_DIR:zanc>/${_linux_sub}/)
        foreach(_linux_opt zanrt_file.o zanrt_mem.o libgcc.a)
            if(EXISTS "${_linux_sys}/${_linux_opt}")
                list(APPEND _cross_sysroot_cmds
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                            "${_linux_sys}/${_linux_opt}"
                            $<TARGET_FILE_DIR:zanc>/${_linux_sub}/)
            endif()
        endforeach()
    endif()
endforeach()

# ---- Windows cross-compilation runtimes ----
# Copy the bundled mingw-w64 runtimes (committed at toolchain/win-x64/ and
# toolchain/win-arm64/) next to zanc so that --target win-x64 / win-arm64 can
# link PE executables from any host with ld.lld.
foreach(_win_sub win-x64 win-arm64)
    set(_win_sys "${CMAKE_SOURCE_DIR}/toolchain/${_win_sub}")
    if(EXISTS "${_win_sys}/mingw/lib/crt2.o")
        list(APPEND _cross_sysroot_cmds
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                    "${_win_sys}" $<TARGET_FILE_DIR:zanc>/${_win_sub})
    endif()
endforeach()

# ---- macOS libSystem stub (macos-x64/macos-arm64 cross-compilation) ----
if(EXISTS "${CMAKE_SOURCE_DIR}/toolchain/macos/libSystem.tbd")
    list(APPEND _cross_sysroot_cmds
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_SOURCE_DIR}/toolchain/macos"
                $<TARGET_FILE_DIR:zanc>/macos)
endif()

# ---- WASI sysroot (wasm32 cross-compilation) ----
if(EXISTS "${CMAKE_SOURCE_DIR}/toolchain/wasm32/libc.a")
    list(APPEND _cross_sysroot_cmds
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_SOURCE_DIR}/toolchain/wasm32"
                $<TARGET_FILE_DIR:zanc>/wasm32)
endif()

# Stage every cross sysroot on each build, so a rebuilt runtime object reaches
# the copy zanc actually links against (see the Linux sysroot note above).
if(_cross_sysroot_cmds)
    add_custom_target(cross_sysroots ALL ${_cross_sysroot_cmds}
        COMMENT "Staging cross-compilation sysroots next to zanc"
        VERBATIM)
    add_dependencies(cross_sysroots zanc)
endif()

# ---- Native database drivers (publish-time bundling) ----
# Each native DB driver now lives inside the stdlib module that owns it
# (e.g. stdlib/System/Data/Sqlite/drivers/<target>/), so it travels with stdlib
# and needs no separate staging next to zanc: zanc resolves it from
# <stdlib_root>/<module>/drivers/<target>/. Driver binaries are gitignored and
# dropped in per target by the developer / release-assembly step; only the
# layout + manifests are tracked.


# ---- Language Server (M8) ----
# Reuses the compiler front-end (no LLVM dependency) plus the intellisense
# engine to serve the Language Server Protocol over stdio.
add_executable(zan-lsp
    src/lsp/lsp_main.c
    src/common/json.c
    src/common/rpc.c
    src/compiler/arena.c
    src/compiler/diag.c
    src/compiler/lexer.c
    src/compiler/ast.c
    src/compiler/parser.c
    src/compiler/binder.c
    src/compiler/checker.c
    src/compiler/definite.c
    src/lsp/intellisense.c
)
target_include_directories(zan-lsp PRIVATE src/compiler src/common src/lsp)
if(MSVC)
    target_compile_definitions(zan-lsp PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
if(WIN32)
    # TCP server transport (--port) uses winsock.
    target_link_libraries(zan-lsp PRIVATE ws2_32)
else()
    # json.c uses floor(); libm is separate from libc on most Unix toolchains.
    target_link_libraries(zan-lsp PRIVATE m)
endif()

# ---- Debug Adapter (M9) ----
# Exposes the IDE debugger engine over the Debug Adapter Protocol (stdio).
add_executable(zan-dap
    src/dap/dap_main.c
    src/common/json.c
    src/common/rpc.c
    src/dap/debugger.c
)
target_include_directories(zan-dap PRIVATE src/common src/dap)
if(MSVC)
    target_compile_definitions(zan-dap PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
if(WIN32)
    # TCP server transport (--port) uses winsock.
    target_link_libraries(zan-dap PRIVATE ws2_32)
else()
    target_link_libraries(zan-dap PRIVATE m)
endif()


# ---- Native GUI runtime (zan_gui) ----
# Cross-platform software renderer + windowing shell used by the Gui stdlib via
# [DllImport("zan_gui")]: Win32 (full), X11 (Linux), Cocoa (macOS). Built
# as a shared library so its platform system deps (X11 / user32+gdi32) are
# self-contained and Zan programs only need `-lzan_gui`.
option(ZAN_BUILD_GUI "Build the native zan_gui runtime library" ON)
# Unified SDL3 windowing shell: when ON, Linux/macOS drive windowing, input and
# present through SDL3 (the same stack as zan_sdl3 / Game.*) instead of the
# native X11/Cocoa shells, so the IDE and games share one window/render path.
# The software rasterizer and text rendering are unchanged. Default OFF keeps
# the native X11/Cocoa backends (and their conformance tests) as the tested
# path; Windows enables it separately via scripts/build_ide.ps1.
option(ZAN_GUI_SDL "Use the unified SDL3 windowing backend for zan_gui (Linux/macOS)" OFF)
set(ZAN_GUI_AVAILABLE FALSE)
if(ZAN_BUILD_GUI)
    if(WIN32)
        add_library(zan_gui SHARED src/runtime/gui_runtime.c)
        target_link_libraries(zan_gui PRIVATE user32 gdi32 dwmapi shcore imm32 ole32)
        # Embedded WebView needs nothing here: the Edge WebView2 backend is
        # written in Zan (stdlib/Gui/WebView2.zan) against its COM interfaces,
        # and resolves WebView2Loader.dll (shipped in the zan_gui driver
        # bundle) at run time, so a machine without the WebView2 runtime still
        # runs GUI apps -- the widget falls back to its placeholder.
        set(ZAN_GUI_AVAILABLE TRUE)
    elseif(ZAN_GUI_SDL)
        # SDL owns the window/event pump/present; the shared software rasterizer
        # draws every widget into one CPU surface shown as a streaming texture.
        find_package(PkgConfig QUIET)
        if(PkgConfig_FOUND)
            pkg_check_modules(SDL3 QUIET sdl3)
        endif()
        if(SDL3_FOUND)
            add_library(zan_gui SHARED src/runtime/gui_runtime.c)
            target_compile_definitions(zan_gui PRIVATE ZAN_GUI_SDL)
            target_include_directories(zan_gui PRIVATE ${SDL3_INCLUDE_DIRS})
            target_link_directories(zan_gui PRIVATE ${SDL3_LIBRARY_DIRS})
            target_link_libraries(zan_gui PRIVATE ${SDL3_LIBRARIES} m)
            # Text still uses the shared FreeType path where available (Linux);
            # elsewhere it falls back to the built-in software bitmap font.
            find_package(Freetype QUIET)
            find_package(Fontconfig QUIET)
            if(Freetype_FOUND AND Fontconfig_FOUND)
                target_compile_definitions(zan_gui PRIVATE ZAN_GUI_FREETYPE)
                target_link_libraries(zan_gui PRIVATE
                    Freetype::Freetype Fontconfig::Fontconfig)
            endif()
            set(ZAN_GUI_AVAILABLE TRUE)
        else()
            message(WARNING "ZAN_GUI_SDL requested but SDL3 (sdl3.pc) not found; skipping zan_gui")
        endif()
    elseif(APPLE)
        # Real Cocoa backend (Objective-C) + shared software renderer. The C file
        # is compiled with ZAN_GUI_COCOA so its macOS stub branch is disabled and
        # gui_runtime_mac.m supplies the native windowing/window-management ABI.
        add_library(zan_gui SHARED src/runtime/gui_runtime.c src/runtime/gui_runtime_mac.m)
        set_source_files_properties(src/runtime/gui_runtime.c
            PROPERTIES COMPILE_DEFINITIONS ZAN_GUI_COCOA)
        target_link_libraries(zan_gui PRIVATE m "-framework Cocoa" "-framework CoreText" "-framework QuartzCore" "-framework IOSurface" "-framework WebKit")
        set(ZAN_GUI_AVAILABLE TRUE)
    else()
        find_package(X11)
        if(X11_FOUND)
            add_library(zan_gui SHARED src/runtime/gui_runtime.c)
            target_include_directories(zan_gui PRIVATE ${X11_INCLUDE_DIR})
            target_link_libraries(zan_gui PRIVATE ${X11_LIBRARIES} m)
            find_package(Freetype QUIET)
            find_package(Fontconfig QUIET)
            if(Freetype_FOUND AND Fontconfig_FOUND)
                target_compile_definitions(zan_gui PRIVATE ZAN_GUI_FREETYPE)
                target_link_libraries(zan_gui PRIVATE
                    Freetype::Freetype Fontconfig::Fontconfig)
            endif()
            set(ZAN_GUI_AVAILABLE TRUE)
        else()
            message(WARNING "libX11 not found; skipping zan_gui (Gui conformance tests will be disabled)")
        endif()
    endif()
endif()

# Native SDL3 runtime used by the SDL3 + Game.* stdlib via
# [DllImport("zan_sdl3")]: windowing, 2D renderer, input, timing and the
# SDL_GPU sprite pipeline (Metal/Vulkan/D3D12). Requires SDL3 (>= 3.2) located
# through pkg-config. On Windows use scripts/stage_sdl3.ps1 (MinGW) instead.
option(ZAN_BUILD_SDL3 "Build the native zan_sdl3 runtime library" ON)
set(ZAN_SDL3_AVAILABLE FALSE)
if(ZAN_BUILD_SDL3 AND NOT WIN32)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(SDL3 QUIET sdl3)
    endif()
    if(SDL3_FOUND)
        add_library(zan_sdl3 SHARED stdlib/SDL3/native/zan_sdl3.c)
        target_include_directories(zan_sdl3 PRIVATE ${SDL3_INCLUDE_DIRS})
        target_link_directories(zan_sdl3 PRIVATE ${SDL3_LIBRARY_DIRS})
        target_link_libraries(zan_sdl3 PRIVATE ${SDL3_LIBRARIES} m)
        set(ZAN_SDL3_AVAILABLE TRUE)
    else()
        message(WARNING "SDL3 (sdl3.pc) not found; skipping zan_sdl3 runtime")
    endif()
endif()

# ---- Native CEF drivers (zan_cef / zan_cef109) ----
# Chromium-backed browser component (Gui.Component.CefBrowser), independent of
# the system-engine WebView component. The drivers are thin C shims over CEF's
# C API that dlopen the libcef of the runtime downloaded on the user's machine,
# so building them needs only the CEF headers -- fetched into the build tree
# from the official minimal archive by cmake/ZanCef.cmake. OFF by default
# because that fetch is a few hundred MB.
option(ZAN_BUILD_CEF "Build the native zan_cef browser drivers" OFF)
set(ZAN_CEF_AVAILABLE FALSE)
if(ZAN_BUILD_CEF)
    include(cmake/ZanCef.cmake)
    zan_cef_add_variant(zan_cef "${ZAN_CEF_VERSION}" current)
    # CEF 109 is the last branch supporting Windows 7/8/8.1, and only there is
    # the legacy variant of any use -- elsewhere the current branch runs
    # everywhere, so off Windows building 109 costs a download for nothing
    # (still switchable on, to keep the legacy source path compilable there).
    if(WIN32)
        set(ZAN_CEF_LEGACY_DEFAULT ON)
    else()
        set(ZAN_CEF_LEGACY_DEFAULT OFF)
    endif()
    option(ZAN_BUILD_CEF_LEGACY "Also build the CEF 109 driver (Win7/8/8.1)"
        ${ZAN_CEF_LEGACY_DEFAULT})
    if(ZAN_BUILD_CEF_LEGACY)
        zan_cef_add_variant(zan_cef109 "${ZAN_CEF_VERSION_LEGACY}" legacy)
    endif()
endif()

# ---- IDE ----
# The IDE is built from src/ide_zan/ZanIDE.zan using the Zan compiler (zanc).
# It is compiled separately: zanc src/ide_zan/ZanIDE.zan -o build/ZanIDE.exe
# The old C-based zan-ide target has been removed in favour of ZanIDE.

# ---- Tests ----
enable_testing()

add_executable(zanrt_sync_test
    tests/runtime/rt_sync_test.c
    src/runtime/rt_sync.c
    src/runtime/rt_file.c
)
target_include_directories(zanrt_sync_test PRIVATE ${CMAKE_SOURCE_DIR})
set_target_properties(zanrt_sync_test PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
)
if(WIN32)
    target_link_libraries(zanrt_sync_test PRIVATE)
else()
    find_package(Threads REQUIRED)
    target_link_libraries(zanrt_sync_test PRIVATE Threads::Threads)
    if(NOT APPLE)
        target_link_libraries(zanrt_sync_test PRIVATE rt)
    endif()
endif()
add_test(NAME runtime_sync COMMAND zanrt_sync_test)
add_dependencies(zanc zanrt_sync_test)

# A hung-up peer (a closed SSE tab) must not kill the process with SIGPIPE.
# POSIX only: Windows has no such signal.
if(NOT WIN32)
    add_executable(zanrt_sigpipe_test
        tests/runtime/rt_sigpipe_test.c
        src/runtime/rt_io.c
        src/runtime/rt_co.c
        src/runtime/rt_timer.c
    )
    target_compile_definitions(zanrt_sigpipe_test PRIVATE ZAN_IO_STACKLESS_ONLY)
    target_include_directories(zanrt_sigpipe_test PRIVATE ${CMAKE_SOURCE_DIR})
    set_target_properties(zanrt_sigpipe_test PROPERTIES
        C_STANDARD 11
        C_STANDARD_REQUIRED YES
    )
    find_package(Threads REQUIRED)
    target_link_libraries(zanrt_sigpipe_test PRIVATE Threads::Threads)
    add_test(NAME runtime_sigpipe COMMAND zanrt_sigpipe_test)
    set_tests_properties(runtime_sigpipe PROPERTIES TIMEOUT 30)
    add_dependencies(zanc zanrt_sigpipe_test)
endif()

# ---- json OOM recovery test (A1) ----
# json.c is built with -DZAN_ALLOC_INJECT so its abort-on-OOM policy is replaced
# by NULL-return and the Nth allocation can be forced to fail; the test then
# verifies json_obj_set/json_free stay consistent and leak-free across a failed
# mid-grow realloc (the dangling keys-pointer / leaked-val scenario).
add_executable(zan_json_oom_test
    tests/unit/json_oom_test.c
    src/common/json.c
)
target_include_directories(zan_json_oom_test PRIVATE ${CMAKE_SOURCE_DIR})
target_compile_definitions(zan_json_oom_test PRIVATE ZAN_ALLOC_INJECT)
set_target_properties(zan_json_oom_test PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
)
if(NOT WIN32)
    target_link_libraries(zan_json_oom_test PRIVATE m)
endif()
add_test(NAME unit_json_oom COMMAND zan_json_oom_test)

# ---- json trailing-data rejection test (A2) ----
add_executable(zan_json_trailing_test
    tests/unit/json_trailing_test.c
    src/common/json.c
)
target_include_directories(zan_json_trailing_test PRIVATE ${CMAKE_SOURCE_DIR})
set_target_properties(zan_json_trailing_test PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
)
if(NOT WIN32)
    target_link_libraries(zan_json_trailing_test PRIVATE m)
endif()
add_test(NAME unit_json_trailing COMMAND zan_json_trailing_test)

# A .zform node must name a real Zan Control class. Invalid kinds fail at
# formgen time instead of silently becoming a Panel or Label.
add_test(NAME diag_invalid_zform_kind
  COMMAND ${CMAKE_COMMAND}
    -DZANC=$<TARGET_FILE:zanc>
    -DSRC=${CMAKE_SOURCE_DIR}/tests/diag/invalid_zform_kind.zform
    -DOUT_EXE=${CMAKE_BINARY_DIR}/diag_invalid_zform_kind${_exe_ext}
    "-DEXPECT_REGEX=invalid_zform_kind\\.zform: \.zform field 0 requires a valid Zan Control kind"
    -P ${CMAKE_SOURCE_DIR}/tests/run_compile_error.cmake)
set_tests_properties(diag_invalid_zform_kind PROPERTIES TIMEOUT 60)

# A class that declares constructors has no implicit parameterless one, so
# `new T()` on it has nothing to run and irgen would hand back a zero-filled
# object that faults on its first use (the Help page's designed Card).
add_test(NAME diag_ctor_missing_parameterless
  COMMAND ${CMAKE_COMMAND}
    -DZANC=$<TARGET_FILE:zanc>
    -DSRC=${CMAKE_SOURCE_DIR}/tests/diag/ctor_missing_parameterless.zan
    -DOUT_EXE=${CMAKE_BINARY_DIR}/diag_ctor_missing_parameterless${_exe_ext}
    "-DEXPECT_REGEX=no constructor of .Box. takes 0 argument"
    -P ${CMAKE_SOURCE_DIR}/tests/run_compile_error.cmake)
set_tests_properties(diag_ctor_missing_parameterless PROPERTIES TIMEOUT 60)

# A complete object initializer builds the object itself, so it may stand in
# for the parameterless constructor the class does not have; a partial one
# leaves the members it skips zero-filled and must still be rejected.
add_test(NAME diag_ctor_initializer_partial
  COMMAND ${CMAKE_COMMAND}
    -DZANC=$<TARGET_FILE:zanc>
    -DSRC=${CMAKE_SOURCE_DIR}/tests/diag/ctor_initializer_partial.zan
    -DOUT_EXE=${CMAKE_BINARY_DIR}/diag_ctor_initializer_partial${_exe_ext}
    "-DEXPECT_REGEX=no constructor of .Box. takes 0 argument"
    -P ${CMAKE_SOURCE_DIR}/tests/run_compile_error.cmake)
set_tests_properties(diag_ctor_initializer_partial PROPERTIES TIMEOUT 60)

# A43-A16: a duplicate method (same name, same parameter types) used to
# compile with first-wins semantics -- zero diagnostics for a silent
# semantics swap. Same-signature duplicates must be a hard error while
# legal overloads stay untouched.
add_test(NAME diag_duplicate_member
  COMMAND ${CMAKE_COMMAND}
    -DZANC=$<TARGET_FILE:zanc>
    -DSRC=${CMAKE_SOURCE_DIR}/tests/diag/duplicate_member.zan
    -DOUT_EXE=${CMAKE_BINARY_DIR}/diag_duplicate_member${_exe_ext}
    "-DEXPECT_REGEX=duplicate method .Val. in .Dup."
    -P ${CMAKE_SOURCE_DIR}/tests/run_compile_error.cmake)
set_tests_properties(diag_duplicate_member PROPERTIES TIMEOUT 60)

# A43-A15: an unqualified call to a member that exists nowhere in the class
# chain used to lower silently to an empty/zero result instead of erroring.
add_test(NAME diag_implicit_ghost_call
  COMMAND ${CMAKE_COMMAND}
    -DZANC=$<TARGET_FILE:zanc>
    -DSRC=${CMAKE_SOURCE_DIR}/tests/diag/implicit_ghost_call.zan
    -DOUT_EXE=${CMAKE_BINARY_DIR}/diag_implicit_ghost_call${_exe_ext}
    "-DEXPECT_REGEX=.Ghost. has no member .MissingHelper."
    -P ${CMAKE_SOURCE_DIR}/tests/run_compile_error.cmake)
set_tests_properties(diag_implicit_ghost_call PROPERTIES TIMEOUT 60)

# Regression (audit H4): a named argument bound past a defaulted parameter
# used to write a NULL into the call's argument list and crash zanc with no
# diagnostic. The rebinder must reject the shape up front.
add_test(NAME diag_named_args_gap
  COMMAND ${CMAKE_COMMAND}
    -DZANC=$<TARGET_FILE:zanc>
    -DSRC=${CMAKE_SOURCE_DIR}/tests/diag/named_args_gap.zan
    -DOUT_EXE=${CMAKE_BINARY_DIR}/diag_named_args_gap${_exe_ext}
    "-DEXPECT_REGEX=named arguments must bind to the leading parameters"
    -P ${CMAKE_SOURCE_DIR}/tests/run_compile_error.cmake)
set_tests_properties(diag_named_args_gap PROPERTIES TIMEOUT 60)

# Regression (audit H8): an array initializer whose element count does not
# match the dimension product used to overflow the allocation in the built
# program; it must be a compile error.
add_test(NAME diag_array_init_count_mismatch
  COMMAND ${CMAKE_COMMAND}
    -DZANC=$<TARGET_FILE:zanc>
    -DSRC=${CMAKE_SOURCE_DIR}/tests/diag/array_init_count_mismatch.zan
    -DOUT_EXE=${CMAKE_BINARY_DIR}/diag_array_init_count_mismatch${_exe_ext}
    "-DEXPECT_REGEX=array initializer has"
    -P ${CMAKE_SOURCE_DIR}/tests/run_compile_error.cmake)
set_tests_properties(diag_array_init_count_mismatch PROPERTIES TIMEOUT 60)

# Regression (gallery funnel_pair 闪退): checker_arg_type_mismatch skipped
# every call with a type argument, so a concrete generic container reaching a
# value parameter (List<Cat> into Cat) -- and the mirror direction -- compiled
# silently and the runtime stored the wrong carrier in the slot, faulting on
# first use. Both directions must be a hard error; same-container variance
# (List<int> -> List<Box>) stays with irgen, and Binding<T>/generic-instantiation
# sites stay deferred (checked at the use site after substitution).
add_test(NAME diag_arg_list_to_class
  COMMAND ${CMAKE_COMMAND}
    -DZANC=$<TARGET_FILE:zanc>
    -DSRC=${CMAKE_SOURCE_DIR}/tests/diag/arg_list_to_class.zan
    -DOUT_EXE=${CMAKE_BINARY_DIR}/diag_arg_list_to_class${_exe_ext}
    "-DEXPECT_REGEX=cannot convert .List. to .Cat. in argument 1 of .Count."
    -P ${CMAKE_SOURCE_DIR}/tests/run_compile_error.cmake)
set_tests_properties(diag_arg_list_to_class PROPERTIES TIMEOUT 60)

add_test(NAME diag_arg_class_to_list
  COMMAND ${CMAKE_COMMAND}
    -DZANC=$<TARGET_FILE:zanc>
    -DSRC=${CMAKE_SOURCE_DIR}/tests/diag/arg_class_to_list.zan
    -DOUT_EXE=${CMAKE_BINARY_DIR}/diag_arg_class_to_list${_exe_ext}
    "-DEXPECT_REGEX=cannot convert .Cat. to .List. in argument 1 of .TakeList."
    -P ${CMAKE_SOURCE_DIR}/tests/run_compile_error.cmake)
set_tests_properties(diag_arg_class_to_list PROPERTIES TIMEOUT 60)


# ---- Declared access control ----
# public/private/internal/protected were parsed into metadata but the checker
# never enforced them. Unmarked members stay public; an explicit modifier now
# rejects out-of-scope uses.

add_test(NAME diag_access_private_field
  COMMAND ${CMAKE_COMMAND}
    -DZANC=$<TARGET_FILE:zanc>
    -DSRC=${CMAKE_SOURCE_DIR}/tests/diag/access_private_field.zan
    -DOUT_EXE=${CMAKE_BINARY_DIR}/diag_access_private_field${_exe_ext}
    "-DEXPECT_REGEX=.Vault.secret. is private"
    -P ${CMAKE_SOURCE_DIR}/tests/run_compile_error.cmake)
set_tests_properties(diag_access_private_field PROPERTIES TIMEOUT 60)

add_test(NAME diag_access_protected_unrelated
  COMMAND ${CMAKE_COMMAND}
    -DZANC=$<TARGET_FILE:zanc>
    -DSRC=${CMAKE_SOURCE_DIR}/tests/diag/access_protected_unrelated.zan
    -DOUT_EXE=${CMAKE_BINARY_DIR}/diag_access_protected_unrelated${_exe_ext}
    "-DEXPECT_REGEX=.Base.shield. is protected"
    -P ${CMAKE_SOURCE_DIR}/tests/run_compile_error.cmake)
set_tests_properties(diag_access_protected_unrelated PROPERTIES TIMEOUT 60)

add_test(NAME diag_access_private_ctor
  COMMAND ${CMAKE_COMMAND}
    -DZANC=$<TARGET_FILE:zanc>
    -DSRC=${CMAKE_SOURCE_DIR}/tests/diag/access_private_ctor.zan
    -DOUT_EXE=${CMAKE_BINARY_DIR}/diag_access_private_ctor${_exe_ext}
    "-DEXPECT_REGEX=.Safe.Safe. is private"
    -P ${CMAKE_SOURCE_DIR}/tests/run_compile_error.cmake)
set_tests_properties(diag_access_private_ctor PROPERTIES TIMEOUT 60)

# internal is module-scoped (first two namespace segments); the case needs
# the declaring file on the command line next to the abuser.
add_test(NAME diag_access_internal_module
  COMMAND ${CMAKE_COMMAND}
    -DZANC=$<TARGET_FILE:zanc>
    -DSRC=${CMAKE_SOURCE_DIR}/tests/diag/access_internal_module.zan
    "-DZANC_ARGS=${CMAKE_SOURCE_DIR}/tests/diag/access_internal_module_helper.zan"
    -DOUT_EXE=${CMAKE_BINARY_DIR}/diag_access_internal_module${_exe_ext}
    "-DEXPECT_REGEX=.Core.Tag. is internal to .Acc.Inner."
    -P ${CMAKE_SOURCE_DIR}/tests/run_compile_error.cmake)
set_tests_properties(diag_access_internal_module PROPERTIES TIMEOUT 60)


# A cast is the one expression whose operand and target the checker used to
# skip entirely, so an impossible conversion reinterpreted the operand's bits
# instead of failing: `(int)"abc"` handed back the string pointer.
add_test(NAME diag_cast_no_conversion
  COMMAND ${CMAKE_COMMAND}
    -DZANC=$<TARGET_FILE:zanc>
    -DSRC=${CMAKE_SOURCE_DIR}/tests/diag/cast_no_conversion.zan
    -DOUT_EXE=${CMAKE_BINARY_DIR}/diag_cast_no_conversion${_exe_ext}
    "-DEXPECT_REGEX=cannot convert .string. to .int."
    -P ${CMAKE_SOURCE_DIR}/tests/run_compile_error.cmake)
set_tests_properties(diag_cast_no_conversion PROPERTIES TIMEOUT 60)

# The builtin collections lower every member they have by name, so one they
# do not have used to reach the string fallback and answer the strlen of the
# List's own storage -- a plausible-looking number from a member that does
# not exist.
add_test(NAME diag_collection_unknown_member
  COMMAND ${CMAKE_COMMAND}
    -DZANC=$<TARGET_FILE:zanc>
    -DSRC=${CMAKE_SOURCE_DIR}/tests/diag/collection_unknown_member.zan
    -DOUT_EXE=${CMAKE_BINARY_DIR}/diag_collection_unknown_member${_exe_ext}
    "-DEXPECT_REGEX=.List. has no member .Length."
    -P ${CMAKE_SOURCE_DIR}/tests/run_compile_error.cmake)
set_tests_properties(diag_collection_unknown_member PROPERTIES TIMEOUT 60)

# An unresolved method call whose result is then read (`t.Describe().Name`) never
# emitted the inner call, so the chain answered the member fallback's 0 instead
# of reporting the missing method -- the unchained `var g = t.Describe();` was
# diagnosed all along.
add_test(NAME diag_unresolved_call_member_chain
  COMMAND ${CMAKE_COMMAND}
    -DZANC=$<TARGET_FILE:zanc>
    -DSRC=${CMAKE_SOURCE_DIR}/tests/diag/unresolved_call_member_chain.zan
    -DOUT_EXE=${CMAKE_BINARY_DIR}/diag_unresolved_call_member_chain${_exe_ext}
    "-DEXPECT_REGEX=.Thing. has no member .Describe."
    -P ${CMAKE_SOURCE_DIR}/tests/run_compile_error.cmake)
set_tests_properties(diag_unresolved_call_member_chain PROPERTIES TIMEOUT 60)

# A `"role": "control"` .zform is an embeddable Control, not a window: it has
# no Main/Show, so naming one as the project entry has to fail at formgen time
# instead of linking a program with no entry point.
add_test(NAME diag_control_zform_entry
  COMMAND ${CMAKE_COMMAND}
    -DZANC=$<TARGET_FILE:zanc>
    -DSRC=${CMAKE_SOURCE_DIR}/tests/diag/control_zform_entry.zform
    -DOUT_EXE=${CMAKE_BINARY_DIR}/diag_control_zform_entry${_exe_ext}
    "-DEXPECT_REGEX=control_zform_entry\\.zform: a \\.zform with .role.: .control. cannot be the project entry"
    -P ${CMAKE_SOURCE_DIR}/tests/run_compile_error.cmake)
set_tests_properties(diag_control_zform_entry PROPERTIES TIMEOUT 60)

# A member name denotes storage (field/property) or code (method), never both:
# `Class.Name` in a value context reads the slot, so a same-named method is
# unreachable and a delegate built from it calls a data pointer.
add_test(NAME diag_member_name_clash
  COMMAND ${CMAKE_COMMAND}
    -DZANC=$<TARGET_FILE:zanc>
    -DSRC=${CMAKE_SOURCE_DIR}/tests/diag/member_name_clash.zan
    -DOUT_EXE=${CMAKE_BINARY_DIR}/diag_member_name_clash${_exe_ext}
    "-DEXPECT_REGEX=.Row. is declared both as a field and as a method in .Design."
    -P ${CMAKE_SOURCE_DIR}/tests/run_compile_error.cmake)
set_tests_properties(diag_member_name_clash PROPERTIES TIMEOUT 60)

# A static field is reachable only through its type name: through a value
# receiver the access used to fold to the constant 0 (crashing irgen on the
# resulting int-vs-pointer compare).
add_test(NAME diag_static_field_via_instance
  COMMAND ${CMAKE_COMMAND}
    -DZANC=$<TARGET_FILE:zanc>
    -DSRC=${CMAKE_SOURCE_DIR}/tests/diag/static_field_via_instance.zan
    -DOUT_EXE=${CMAKE_BINARY_DIR}/diag_static_field_via_instance${_exe_ext}
    "-DEXPECT_REGEX=.other. is a static field of .Holder."
    -P ${CMAKE_SOURCE_DIR}/tests/run_compile_error.cmake)
set_tests_properties(diag_static_field_via_instance PROPERTIES TIMEOUT 60)

# Definite assignment: a local read on a path that never assigned it is a
# garbage stack slot at runtime (a null control field, a count nobody set), so
# the checker rejects the read instead of emitting the load.
add_test(NAME diag_unassigned_local
  COMMAND ${CMAKE_COMMAND}
    -DZANC=$<TARGET_FILE:zanc>
    -DSRC=${CMAKE_SOURCE_DIR}/tests/diag/unassigned_local.zan
    -DOUT_EXE=${CMAKE_BINARY_DIR}/diag_unassigned_local${_exe_ext}
    "-DEXPECT_REGEX=use of unassigned local variable .total."
    -P ${CMAKE_SOURCE_DIR}/tests/run_compile_error.cmake)
set_tests_properties(diag_unassigned_local PROPERTIES TIMEOUT 60)

# An `out` parameter is a promise that the slot is written before the method
# returns; an early return that skips the write hands the caller an
# uninitialised value.
add_test(NAME diag_unassigned_out_param
  COMMAND ${CMAKE_COMMAND}
    -DZANC=$<TARGET_FILE:zanc>
    -DSRC=${CMAKE_SOURCE_DIR}/tests/diag/unassigned_out_param.zan
    -DOUT_EXE=${CMAKE_BINARY_DIR}/diag_unassigned_out_param${_exe_ext}
    "-DEXPECT_REGEX=out parameter .v. must be assigned before control leaves"
    -P ${CMAKE_SOURCE_DIR}/tests/run_compile_error.cmake)
set_tests_properties(diag_unassigned_out_param PROPERTIES TIMEOUT 60)

# The same rule for a string local, which irgen still starts empty so a stray
# read cannot dereference null: the default is a backstop, not a licence to
# read a variable no path has written.
add_test(NAME diag_unassigned_local_string
  COMMAND ${CMAKE_COMMAND}
    -DZANC=$<TARGET_FILE:zanc>
    -DSRC=${CMAKE_SOURCE_DIR}/tests/diag/unassigned_local_string.zan
    -DOUT_EXE=${CMAKE_BINARY_DIR}/diag_unassigned_local_string${_exe_ext}
    "-DEXPECT_REGEX=use of unassigned local variable .plain."
    -P ${CMAKE_SOURCE_DIR}/tests/run_compile_error.cmake)
set_tests_properties(diag_unassigned_local_string PROPERTIES TIMEOUT 60)

# ---- RPC framing test (A3) ----
add_executable(zan_rpc_framing_test
    tests/unit/rpc_framing_test.c
    src/common/rpc.c
)
target_include_directories(zan_rpc_framing_test PRIVATE ${CMAKE_SOURCE_DIR})
set_target_properties(zan_rpc_framing_test PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
)
add_test(NAME unit_rpc_framing COMMAND zan_rpc_framing_test)

# ---- Runtime: allocator double-free detection (rt_mem.c) ----
# Produced programs link the size-class allocator with
# `ld --wrap=malloc,free,calloc,realloc`; a second free of a live block is an
# ARC aliasing bug the allocator turns into an immediate abort. The slab path
# is mmap-based and therefore POSIX-only (Windows falls through to the libc
# heap, where the guard never runs), and ld64 has no --wrap, so the Linux CI
# job is where this runs.
if(NOT WIN32 AND NOT APPLE)
  add_executable(zan_rt_mem_dblfree_test
    tests/runtime/rt_mem_dblfree_test.c
    src/runtime/rt_mem.c
  )
  target_include_directories(zan_rt_mem_dblfree_test PRIVATE ${CMAKE_SOURCE_DIR})
  set_target_properties(zan_rt_mem_dblfree_test PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
  )
  target_link_options(zan_rt_mem_dblfree_test PRIVATE
    -Wl,--wrap=malloc -Wl,--wrap=free -Wl,--wrap=calloc -Wl,--wrap=realloc
  )
  add_test(NAME runtime_mem_dblfree COMMAND zan_rt_mem_dblfree_test)
  set_tests_properties(runtime_mem_dblfree PROPERTIES TIMEOUT 30)

  # Cross-thread frees: a block freed by a thread that did not allocate it must
  # go back to the owning cache, or a server's allocate-here/free-there traffic
  # drifts every block one way and the allocating thread maps slabs forever.
  add_executable(zan_rt_mem_remote_test
    tests/runtime/rt_mem_remote_test.c
    src/runtime/rt_mem.c
  )
  target_include_directories(zan_rt_mem_remote_test PRIVATE ${CMAKE_SOURCE_DIR})
  set_target_properties(zan_rt_mem_remote_test PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
  )
  find_package(Threads REQUIRED)
  target_link_libraries(zan_rt_mem_remote_test PRIVATE Threads::Threads)
  target_link_options(zan_rt_mem_remote_test PRIVATE
    -Wl,--wrap=malloc -Wl,--wrap=free -Wl,--wrap=calloc -Wl,--wrap=realloc
  )
  add_test(NAME runtime_mem_remote COMMAND zan_rt_mem_remote_test)
  set_tests_properties(runtime_mem_remote PROPERTIES TIMEOUT 60)
endif()

# ---- Conformance suite: compile each program, run it, diff stdout vs golden ----
if(WIN32)
  set(_exe_ext ".exe")
else()
  set(_exe_ext "")
endif()

file(GLOB _conf_srcs ${CMAKE_SOURCE_DIR}/tests/conformance/*.zan)

# Every test program is a pure function of its source, zanc, the stdlib and any
# explicit Zan source arguments, so the suite reuses the binary it built last
# time unless one of those changed.
# This stamp carries the stdlib's mtime into that comparison; the test scripts
# do the rest. Without it a plain re-run recompiled and relinked all ~165
# programs three times over -- minutes of work with nothing to show for it.
set(_stdlib_stamp ${CMAKE_BINARY_DIR}/stdlib.stamp)
file(GLOB_RECURSE _stdlib_srcs ${CMAKE_SOURCE_DIR}/stdlib/*.zan)
add_custom_command(OUTPUT ${_stdlib_stamp}
  COMMAND ${CMAKE_COMMAND} -E touch ${_stdlib_stamp}
  DEPENDS ${_stdlib_srcs}
  COMMENT "Stamping stdlib sources for the test suite")
add_custom_target(stdlib_stamp ALL DEPENDS ${_stdlib_stamp})

# AI outbound targets are an exact allowlist, not an arbitrary URL setting.
# Keep this policy test independent of GUI availability and real providers.
add_test(NAME policy_server_mvc_ai_endpoint
  COMMAND ${CMAKE_COMMAND}
    -DZANC=$<TARGET_FILE:zanc>
    -DSRC=${CMAKE_SOURCE_DIR}/tests/server_mvc/ai_endpoint_allowlist.zan
    -DEXPECTED=${CMAKE_SOURCE_DIR}/tests/server_mvc/ai_endpoint_allowlist.out
    -DOUT_EXE=${CMAKE_BINARY_DIR}/policy_server_mvc_ai_endpoint${_exe_ext}
    -DSTDLIB_STAMP=${_stdlib_stamp}
    "-DZANC_ARGS=${CMAKE_SOURCE_DIR}/templates/server/server-mvc/src/Feature/AiEndpointPolicy.zan"
    -P ${CMAKE_SOURCE_DIR}/tests/run_case.cmake)
set_tests_properties(policy_server_mvc_ai_endpoint PROPERTIES TIMEOUT 120)

add_test(NAME policy_external_target
  COMMAND ${CMAKE_COMMAND}
    -DZANC=$<TARGET_FILE:zanc>
    -DSRC=${CMAKE_SOURCE_DIR}/tests/server_mvc/external_policy.zan
    -DEXPECTED=${CMAKE_SOURCE_DIR}/tests/server_mvc/external_policy.out
    -DOUT_EXE=${CMAKE_BINARY_DIR}/policy_external_target${_exe_ext}
    -DSTDLIB_STAMP=${_stdlib_stamp}
    -P ${CMAKE_SOURCE_DIR}/tests/run_case.cmake)
set_tests_properties(policy_external_target PROPERTIES TIMEOUT 120)

add_test(NAME policy_http_client
  COMMAND ${CMAKE_COMMAND}
    -DZANC=$<TARGET_FILE:zanc>
    -DSRC=${CMAKE_SOURCE_DIR}/tests/server_mvc/http_client_policy.zan
    -DEXPECTED=${CMAKE_SOURCE_DIR}/tests/server_mvc/http_client_policy.out
    -DOUT_EXE=${CMAKE_BINARY_DIR}/policy_http_client${_exe_ext}
    -DSTDLIB_STAMP=${_stdlib_stamp}
    -P ${CMAKE_SOURCE_DIR}/tests/run_case.cmake)
set_tests_properties(policy_http_client PROPERTIES TIMEOUT 120)

add_test(NAME policy_sse_sink
  COMMAND ${CMAKE_COMMAND}
    -DZANC=$<TARGET_FILE:zanc>
    -DSRC=${CMAKE_SOURCE_DIR}/tests/server_mvc/sse_sink_policy.zan
    -DEXPECTED=${CMAKE_SOURCE_DIR}/tests/server_mvc/sse_sink_policy.out
    -DOUT_EXE=${CMAKE_BINARY_DIR}/policy_sse_sink${_exe_ext}
    -DSTDLIB_STAMP=${_stdlib_stamp}
    -P ${CMAKE_SOURCE_DIR}/tests/run_case.cmake)
set_tests_properties(policy_sse_sink PROPERTIES TIMEOUT 120)


# The RA2 format tests were moved out of stdlib into
# examples/game/ra2/stdlib_rts; they need its Formats sources on the command
# line so the compiler can resolve Ra2.StdlibRts.Formats.
set(_rts_stdlib_srcs
  ${CMAKE_SOURCE_DIR}/examples/game/ra2/stdlib_rts/Formats/Binary.zan
  ${CMAKE_SOURCE_DIR}/examples/game/ra2/stdlib_rts/Formats/Csf.zan
  ${CMAKE_SOURCE_DIR}/examples/game/ra2/stdlib_rts/Formats/Ini.zan
  ${CMAKE_SOURCE_DIR}/examples/game/ra2/stdlib_rts/Formats/Pal.zan
  ${CMAKE_SOURCE_DIR}/examples/game/ra2/stdlib_rts/Formats/Shp.zan
  ${CMAKE_SOURCE_DIR}/examples/game/ra2/stdlib_rts/Formats/Tmp.zan)
string(JOIN ";" _rts_stdlib_zanc_args ${_rts_stdlib_srcs})

# A conformance case that uses the Gui namespace reaches the
# [DllImport("zan_gui")] exports, so it must link the library this build just
# produced: the driver bundle committed under stdlib/Gui/drivers/ lags behind
# newly added exports (the tests/gui/* cases below pass --driver-dir for the
# same reason). Which cases those are is read from the sources rather than
# guessed from their names, because Gui.Component.Chart and friends link the
# same exports without a gui_ prefix.
#
# Only where the build output is what the linker consumes: on Windows that is
# the GNU import library (libzan_gui.dll.a), which scripts/build_gui_driver.ps1
# generates into stdlib/Gui/drivers/win-x64 -- the build tree holds the MSVC
# zan_gui.lib, which zanc's bundled GNU ld cannot read. So Windows keeps the
# bundled lookup and the driver script stays the step that refreshes it.
set(_gui_driver_args "")
if(TARGET zan_gui AND NOT WIN32)
  set(_gui_driver_args "--driver-dir;$<TARGET_FILE_DIR:zan_gui>")
endif()
# Same override, spelled as a tail to append to a ZANC_ARGS string (an empty
# list element would reach zanc as an empty argument).
set(_gui_driver_tail "")
if(_gui_driver_args)
  set(_gui_driver_tail ";--driver-dir;$<TARGET_FILE_DIR:zan_gui>")
endif()

# --driver-dir redirects the lookup of *every* driver the case links, so a case
# that also needs another module's driver (Gui.Component.CefBrowser ships its
# own) stays on the bundled directories.
function(zan_uses_gui_driver _src _out)
  file(READ ${_src} _text)
  if(_text MATCHES "using Gui[.;]"
     AND NOT _text MATCHES "using Gui\\.Component\\.CefBrowser")
    set(${_out} TRUE PARENT_SCOPE)
  else()
    set(${_out} FALSE PARENT_SCOPE)
  endif()
endfunction()

foreach(_src ${_conf_srcs})
  get_filename_component(_name ${_src} NAME_WE)
  zan_uses_gui_driver(${_src} _needs_gui_driver)
  if(_name MATCHES "^rts_")
    set(_extra_args "${_rts_stdlib_zanc_args}")
  elseif(_name MATCHES "^async_mt")
    # ZAN_CO_DRIVER tests: exercise the multi-worker coroutine driver.
    set(_extra_args "--async-workers")
  elseif(_name STREQUAL "server_mvc_timezone")
    set(_extra_args
      "${CMAKE_SOURCE_DIR}/templates/server/server-mvc/src/Framework/Fmt.zan")
  elseif(_needs_gui_driver)
    set(_extra_args "${_gui_driver_args}")
  else()
    set(_extra_args "")
  endif()
  if(_name STREQUAL "environment_unicode_args")
    set(_run_args "中文;参数")
  else()
    set(_run_args "")
  endif()
  add_test(NAME conformance_${_name}
    COMMAND ${CMAKE_COMMAND}
      -DZANC=$<TARGET_FILE:zanc>
      -DSRC=${_src}
      -DEXPECTED=${CMAKE_SOURCE_DIR}/tests/conformance/${_name}.out
      -DOUT_EXE=${CMAKE_BINARY_DIR}/conf_${_name}${_exe_ext}
      -DSTDLIB_STAMP=${_stdlib_stamp}
      "-DZANC_ARGS=${_extra_args}"
      "-DRUN_ARGS=${_run_args}"
      -P ${CMAKE_SOURCE_DIR}/tests/run_case.cmake)
  # A program and its leakcheck twin are built from the same source, so any
  # hardcoded listen port (e.g. the async networking conformance tests) is
  # shared between them. Serialize the runnable variants of one source via a
  # per-source resource lock so parallel ctest can't collide on that port.
  # TIMEOUT: a single case compiles in well under a second and runs in
  # milliseconds, so anything past two minutes is a hang (a coroutine parked on
  # a dead socket, a server that never stops). Without it ctest waits out its
  # 1500s default and one hung case alone costs 25 minutes.
  set_tests_properties(conformance_${_name} PROPERTIES
    RESOURCE_LOCK src_${_name} TIMEOUT 120)
endforeach()

# ---- Determinism: codegen must be a pure function of the source ----
# Compiling the same program twice must yield byte-identical LLVM IR
# (reproducible-build / correctness net).
foreach(_src ${_conf_srcs})
  get_filename_component(_name ${_src} NAME_WE)
  if(_name MATCHES "^rts_")
    set(_extra_args "${_rts_stdlib_zanc_args}")
  elseif(_name MATCHES "^async_mt")
    set(_extra_args "--async-workers")
  else()
    set(_extra_args "")
  endif()
  add_test(NAME determinism_${_name}
    COMMAND ${CMAKE_COMMAND}
      -DZANC=$<TARGET_FILE:zanc>
      -DSRC=${_src}
      -DSTDLIB_STAMP=${_stdlib_stamp}
      -DSTAMP=${CMAKE_BINARY_DIR}/det_${_name}.stamp
      "-DZANC_ARGS=${_extra_args}"
      -P ${CMAKE_SOURCE_DIR}/tests/run_determinism.cmake)
  set_tests_properties(determinism_${_name} PROPERTIES TIMEOUT 120)
endforeach()

# ---- Generator suite: drive the Zan-scripted generators through zanc and
#      assert on the reply (rewrite directives + generated sources) ----
# Each case is a tiny program that must (a) compile with the generator reply
# captured, (b) produce the expected rewrite directives / generated classes,
# (c) run to a golden output. This pins the generator contract the way the
# deleted C unit tests did, from outside the compiler process.
file(GLOB _gen_srcs ${CMAKE_SOURCE_DIR}/tests/gen/*.zan)
foreach(_src ${_gen_srcs})
  get_filename_component(_name ${_src} NAME_WE)
  if(_name STREQUAL "db_repository")
    set(_gen_regex
      "\"op\": \"db_repo_call\""
      "\"k\": \"new\""
      "\"t\": \"UserDao\""
      "\"n\": \"ById\""
      "class __DbQ_User")
  else()
    set(_gen_regex
      "\"op\": \"db_root\","
      "\"name\": \"Q_Order\""
      "\"op\": \"db_expr_arg\","
      "\"v\": \"SUM\\(t\\.amount\\)\""
      "\"op\": \"db_acc_head\","
      "class __DbQ_Order"
      "class __DbU_Order")
  endif()
  set(_gen_forbid_regex
    "GetField(Int|Long|Double|String|Bool)"
    "SetField(Int|Long|Double|String|Bool)")
  add_test(NAME gen_${_name}
    COMMAND ${CMAKE_COMMAND}
      -DZANC=$<TARGET_FILE:zanc>
      -DSRC=${_src}
      -DEXPECTED=${CMAKE_SOURCE_DIR}/tests/gen/${_name}.out
      -DOUT_EXE=${CMAKE_BINARY_DIR}/gen_${_name}${_exe_ext}
      -DREPLY=${CMAKE_BINARY_DIR}/gen_${_name}_reply.json
      "-DREPLY_REGEX=${_gen_regex}"
      "-DREPLY_FORBID_REGEX=${_gen_forbid_regex}"
      -P ${CMAKE_SOURCE_DIR}/tests/run_gen_case.cmake)
  set_tests_properties(gen_${_name} PROPERTIES TIMEOUT 120)
endforeach()

add_test(NAME gen_cache_isolation
  COMMAND ${CMAKE_COMMAND}
    -DZANC=$<TARGET_FILE:zanc>
    -DSTDLIB=${CMAKE_SOURCE_DIR}/stdlib
    -DSRC=${CMAKE_SOURCE_DIR}/tests/conformance/json_entity_mapping.zan
    -DWORK=${CMAKE_BINARY_DIR}/gen_cache_isolation
    -P ${CMAKE_SOURCE_DIR}/tests/run_gen_cache_isolation.cmake)
set_tests_properties(gen_cache_isolation PROPERTIES
  TIMEOUT 240
  LABELS "full;standard;smoke")

# ---- Leak detection: run each program under --check-leaks, fail on leaks ----
# The --check-leaks build tracks rc-managed allocations and reports any object
# still reachable at exit. Programs known to leak (rc objects owned by arrays,
# List/Dict or object fields are not yet released when their owner dies -- a
# documented ARC follow-up) are marked WILL_FAIL so this gate stays green while
# still catching *new* leaks; fixing such a leak flips its test to failing, a
# reminder to drop it from the allow-list below.
set(_known_leaky
)
foreach(_src ${_conf_srcs})
  get_filename_component(_name ${_src} NAME_WE)
  zan_uses_gui_driver(${_src} _needs_gui_driver)
  if(_name MATCHES "^rts_")
    set(_extra_args "${_rts_stdlib_zanc_args}")
  elseif(_name MATCHES "^async_mt")
    set(_extra_args "--async-workers")
  elseif(_needs_gui_driver)
    set(_extra_args "${_gui_driver_args}")
  else()
    set(_extra_args "")
  endif()
  add_test(NAME leakcheck_${_name}
    COMMAND ${CMAKE_COMMAND}
      -DZANC=$<TARGET_FILE:zanc>
      -DSRC=${_src}
      -DOUT_EXE=${CMAKE_BINARY_DIR}/leak_${_name}${_exe_ext}
      -DSTDLIB_STAMP=${_stdlib_stamp}
      "-DZANC_ARGS=${_extra_args}"
      -P ${CMAKE_SOURCE_DIR}/tests/run_leakcheck.cmake)
  set_tests_properties(leakcheck_${_name} PROPERTIES
    RESOURCE_LOCK src_${_name} TIMEOUT 120)
  if(_name IN_LIST _known_leaky)
    set_tests_properties(leakcheck_${_name} PROPERTIES WILL_FAIL TRUE)
  endif()
endforeach()

# ---- ARC guard: every conformance program, with freed objects quarantined ----
# --arc-guard stamps a freed object's refcount slot with a marker instead of
# letting the block be recycled, and traps any later retain/release that reaches
# it. That is the one bug class refcounting cannot notice on its own: a
# *missing* retain, where the counts balance but a reference outlives its
# object. The leakcheck twin above is the mirror image (a retain with no
# release), so the two together bracket ARC ownership from both sides.
#
# This tier is why it earns its runtime: the A53 heap corruption (an
# RC-managed parameter reassigned in the callee released the caller's object)
# ran the plain conformance suite to a bare STATUS_HEAP_CORRUPTION exit with no
# output, and in the field showed up only as misordered `orderby` results.
# Under the guard the same program names the fault: "retain through a stale
# reference (object was freed: missing retain when stored)", with the object
# address and the releasing site.
foreach(_src ${_conf_srcs})
  get_filename_component(_name ${_src} NAME_WE)
  zan_uses_gui_driver(${_src} _needs_gui_driver)
  if(_name MATCHES "^rts_")
    set(_extra_args "${_rts_stdlib_zanc_args}")
  elseif(_name MATCHES "^async_mt")
    set(_extra_args "--async-workers")
  elseif(_name STREQUAL "server_mvc_timezone")
    set(_extra_args
      "${CMAKE_SOURCE_DIR}/templates/server/server-mvc/src/Framework/Fmt.zan")
  elseif(_needs_gui_driver)
    set(_extra_args "${_gui_driver_args}")
  else()
    set(_extra_args "")
  endif()
  add_test(NAME arcguard_${_name}
    COMMAND ${CMAKE_COMMAND}
      -DZANC=$<TARGET_FILE:zanc>
      -DSRC=${_src}
      -DOUT_EXE=${CMAKE_BINARY_DIR}/arc_${_name}${_exe_ext}
      -DSTDLIB_STAMP=${_stdlib_stamp}
      "-DZANC_ARGS=${_extra_args}"
      -P ${CMAKE_SOURCE_DIR}/tests/run_arcguard.cmake)
  set_tests_properties(arcguard_${_name} PROPERTIES
    RESOURCE_LOCK src_${_name} TIMEOUT 120)
endforeach()

# ---- Multi-file compilation: compile several .zan files into one program ----
# Sources live in a subdirectory so the single-file glob above ignores them.
add_test(NAME conformance_multifile
  COMMAND ${CMAKE_COMMAND}
    -DZANC=$<TARGET_FILE:zanc>
    -DSRC=${CMAKE_SOURCE_DIR}/tests/conformance/multifile/main.zan
    "-DZANC_ARGS=${CMAKE_SOURCE_DIR}/tests/conformance/multifile/box.zan;${CMAKE_SOURCE_DIR}/tests/conformance/multifile/util.zan"
    -DEXPECTED=${CMAKE_SOURCE_DIR}/tests/conformance/multifile/expected.out
    -DOUT_EXE=${CMAKE_BINARY_DIR}/conf_multifile${_exe_ext}
    -P ${CMAKE_SOURCE_DIR}/tests/run_case.cmake)

# ---- Response file: `zanc @file` takes its arguments from a file, so a
#      whole-project build is not capped by the shell's command-line limit
#      (cmd.exe truncates at 8191 characters). ----
add_test(NAME conformance_argfile
  COMMAND ${CMAKE_COMMAND}
    -DZANC=$<TARGET_FILE:zanc>
    -DSRC=${CMAKE_SOURCE_DIR}/tests/conformance/multifile/main.zan
    "-DEXTRA=${CMAKE_SOURCE_DIR}/tests/conformance/multifile/box.zan;${CMAKE_SOURCE_DIR}/tests/conformance/multifile/util.zan"
    -DEXPECTED=${CMAKE_SOURCE_DIR}/tests/conformance/multifile/expected.out
    -DOUT_EXE=${CMAKE_BINARY_DIR}/conf_argfile${_exe_ext}
    -DRSP=${CMAKE_BINARY_DIR}/conf_argfile.rsp
    -P ${CMAKE_SOURCE_DIR}/tests/run_argfile.cmake)
set_tests_properties(conformance_argfile PROPERTIES TIMEOUT 120)

# ---- Namespace collisions: two namespaces declaring the same simple names,
#      including static calls through them (see nsresolve.c). ----
add_test(NAME conformance_nsdup
  COMMAND ${CMAKE_COMMAND}
    -DZANC=$<TARGET_FILE:zanc>
    -DSRC=${CMAKE_SOURCE_DIR}/tests/conformance/nsdup/main.zan
    "-DZANC_ARGS=${CMAKE_SOURCE_DIR}/tests/conformance/nsdup/alpha.zan;${CMAKE_SOURCE_DIR}/tests/conformance/nsdup/beta.zan"
    -DEXPECTED=${CMAKE_SOURCE_DIR}/tests/conformance/nsdup/expected.out
    -DOUT_EXE=${CMAKE_BINARY_DIR}/conf_nsdup${_exe_ext}
    -P ${CMAKE_SOURCE_DIR}/tests/run_case.cmake)

# ---- Library output: static archive + shared libraries (.dll/.so/.dylib) ----
# A library has no entry point, so these bypass the compile-run-compare GLOB:
# the artifact is validated by format magic (and, on Windows, a consumer that
# links the DLL via [DllImport] and runs, proving the public method is exported
# and the generated import library resolves). .so/.dylib are cross-produced;
# they cannot execute on the host, so format + successful link is the gate.
set(_emitlib_src ${CMAKE_SOURCE_DIR}/tests/emit_lib/lib.zan)
set(_emitlib_consumer ${CMAKE_SOURCE_DIR}/tests/emit_lib/consumer.zan)
set(_emitlib_expected ${CMAKE_SOURCE_DIR}/tests/emit_lib/expected.out)

add_test(NAME emit_lib_static
  COMMAND ${CMAKE_COMMAND}
    -DZANC=$<TARGET_FILE:zanc>
    -DSRC=${_emitlib_src}
    -DOUT=${CMAKE_BINARY_DIR}/emitlib_test.a
    -DMODE=static
    -DSTDLIB_STAMP=${_stdlib_stamp}
    -P ${CMAKE_SOURCE_DIR}/tests/run_emit_lib.cmake)
set_tests_properties(emit_lib_static PROPERTIES TIMEOUT 120)

if(WIN32)
  add_test(NAME emit_lib_windows_dll
    COMMAND ${CMAKE_COMMAND}
      -DZANC=$<TARGET_FILE:zanc>
      -DSRC=${_emitlib_src}
      -DOUT=${CMAKE_BINARY_DIR}/emitlib_test.dll
      -DMODE=dll
      -DCONSUMER=${_emitlib_consumer}
      -DEXPECTED=${_emitlib_expected}
      -DSTDLIB_STAMP=${_stdlib_stamp}
      -P ${CMAKE_SOURCE_DIR}/tests/run_emit_lib.cmake)
  set_tests_properties(emit_lib_windows_dll PROPERTIES TIMEOUT 120)
endif()

add_test(NAME emit_lib_linux_so
  COMMAND ${CMAKE_COMMAND}
    -DZANC=$<TARGET_FILE:zanc>
    -DSRC=${_emitlib_src}
    -DOUT=${CMAKE_BINARY_DIR}/emitlib_test.so
    -DMODE=so
    -DTARGET=linux-musl
    -DSTDLIB_STAMP=${_stdlib_stamp}
    -P ${CMAKE_SOURCE_DIR}/tests/run_emit_lib.cmake)
set_tests_properties(emit_lib_linux_so PROPERTIES TIMEOUT 120)

add_test(NAME emit_lib_macos_dylib
  COMMAND ${CMAKE_COMMAND}
    -DZANC=$<TARGET_FILE:zanc>
    -DSRC=${_emitlib_src}
    -DOUT=${CMAKE_BINARY_DIR}/emitlib_test.dylib
    -DMODE=dylib
    -DTARGET=macos-x64
    -DSTDLIB_STAMP=${_stdlib_stamp}
    -P ${CMAKE_SOURCE_DIR}/tests/run_emit_lib.cmake)
set_tests_properties(emit_lib_macos_dylib PROPERTIES TIMEOUT 120)

if(UNIX AND NOT APPLE)
  add_test(NAME driver_packaging_sqlite
    COMMAND ${CMAKE_COMMAND}
      -DZANC=$<TARGET_FILE:zanc>
      -DSRC=${CMAKE_SOURCE_DIR}/examples/db/sqlite_crud.zan
      -DWORK=${CMAKE_BINARY_DIR}/driver_packaging_sqlite
      -P ${CMAKE_SOURCE_DIR}/tests/run_driver_packaging.cmake)
  set_tests_properties(driver_packaging_sqlite PROPERTIES TIMEOUT 120)
endif()

# ---- Self-hosting: build gen1 with the C host, emit IR for a real program,
#      clang-compile + run + diff stdout, and verify malformed input fails
#      closed (non-zero, no .ll). clang is optional -- without it the stdout
#      stage is skipped and the harness still proves gen1 builds and emits IR. ----
find_program(SELFHOST_CLANG NAMES clang)
add_test(NAME selfhost_gen1
  COMMAND ${CMAKE_COMMAND}
    -DZANC=$<TARGET_FILE:zanc>
    -DSELFHOST_DIR=${CMAKE_SOURCE_DIR}/src/selfhost
    -DPROG=${CMAKE_SOURCE_DIR}/tests/selfhost/prog1.zan
    -DEXPECTED=${CMAKE_SOURCE_DIR}/tests/selfhost/prog1.out
    -DGEN1=${CMAKE_BINARY_DIR}/selfhost_gen1${_exe_ext}
    -DOUTLL=${CMAKE_BINARY_DIR}/selfhost_gen1.ll
    -DCLANG=${SELFHOST_CLANG}
    -DBADPROG=${CMAKE_SOURCE_DIR}/tests/selfhost/bad_unknown_type.zan
    -DBADLL=${CMAKE_BINARY_DIR}/selfhost_bad.ll
    -P ${CMAKE_SOURCE_DIR}/tests/run_selfhost.cmake)
set_tests_properties(selfhost_gen1 PROPERTIES TIMEOUT 600)

# ---- Self-hosting: jsongen must fail closed on unsupported field types ----
# The ok program maps a class whose int[] field is [JsonIgnore]-annotated
# (compiles, runs, serializes without the field); the bad program leaves the
# same field unannotated and must be rejected with a compile error and no IR.
# One gen1 build serves both directions (PROG + BADPROG in one invocation).
add_test(NAME selfhost_json_ignore
  COMMAND ${CMAKE_COMMAND}
    -DZANC=$<TARGET_FILE:zanc>
    -DSELFHOST_DIR=${CMAKE_SOURCE_DIR}/src/selfhost
    -DPROG=${CMAKE_SOURCE_DIR}/tests/selfhost/json_ignore_ok.zan
    -DEXPECTED=${CMAKE_SOURCE_DIR}/tests/selfhost/json_ignore_ok.out
    -DGEN1=${CMAKE_BINARY_DIR}/selfhost_jsongen1${_exe_ext}
    -DOUTLL=${CMAKE_BINARY_DIR}/selfhost_json_ok.ll
    -DCLANG=${SELFHOST_CLANG}
    -DBADPROG=${CMAKE_SOURCE_DIR}/tests/selfhost/json_ignore_bad.zan
    -DBADLL=${CMAKE_BINARY_DIR}/selfhost_json_bad.ll
    -P ${CMAKE_SOURCE_DIR}/tests/run_selfhost.cmake)
set_tests_properties(selfhost_json_ignore PROPERTIES TIMEOUT 600)

# ---- Self-hosting fixed point: full gen0->gen1->gen2->gen3 closure, asserting
#      g2.ll == g3.ll byte-for-byte. Requires clang to link the emitted IR, so
#      it is only registered when clang is found; the Windows link step needs a
#      large stack reservation (matching what the host bakes into its own link).
#      Skipped automatically wherever clang is absent. ----
if(SELFHOST_CLANG)
  # The selfhost driver calls the zan_exe_dir_into/zan_dir_list_into runtime
  # helpers (stdlib auto-include), so gen2 links an rt_sync object built with
  # the same clang that links the test (the host-toolchain object may target a
  # different C runtime, e.g. mingw vs MSVC).
  set(ZAN_RT_SYNC_SELFHOST_OBJ "${CMAKE_BINARY_DIR}/zanrt_sync_selfhost.obj")
  add_custom_command(
    OUTPUT ${ZAN_RT_SYNC_SELFHOST_OBJ}
    COMMAND ${SELFHOST_CLANG} -O2 -c ${CMAKE_SOURCE_DIR}/src/runtime/rt_sync.c
            -o ${ZAN_RT_SYNC_SELFHOST_OBJ}
    DEPENDS ${CMAKE_SOURCE_DIR}/src/runtime/rt_sync.c
            ${CMAKE_SOURCE_DIR}/src/runtime/rt_sync.h
    COMMENT "Building rt_sync object for selfhost tests (zanrt_sync_selfhost)")
  add_custom_target(zanrt_sync_selfhost ALL DEPENDS ${ZAN_RT_SYNC_SELFHOST_OBJ})
  if(WIN32)
    # Reserve a large stack so gen2 (which self-compiles into g3.ll) does not
    # overflow. The flag depends on the linker clang drives: MSVC-targeting
    # clang uses lld-link (/STACK:N); mingw clang uses GNU ld (--stack N).
    execute_process(COMMAND ${SELFHOST_CLANG} -print-target-triple
                    OUTPUT_VARIABLE _clang_triple
                    OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    if(_clang_triple MATCHES "msvc")
      set(_fixedpoint_stack "-Xlinker;/STACK:268435456")
    else()
      set(_fixedpoint_stack "-Wl,--stack,268435456")
    endif()
  else()
    set(_fixedpoint_stack "")
  endif()
  add_test(NAME selfhost_fixed_point
    COMMAND ${CMAKE_COMMAND}
      -DZANC=$<TARGET_FILE:zanc>
      -DSELFHOST_DIR=${CMAKE_SOURCE_DIR}/src/selfhost
      -DCLANG=${SELFHOST_CLANG}
      -DGEN1=${CMAKE_BINARY_DIR}/fixedpoint_gen1${_exe_ext}
      -DGEN2=${CMAKE_BINARY_DIR}/fixedpoint_gen2${_exe_ext}
      -DG2LL=${CMAKE_BINARY_DIR}/fixedpoint_g2.ll
      -DG3LL=${CMAKE_BINARY_DIR}/fixedpoint_g3.ll
      "-DSTACK_ARGS=${_fixedpoint_stack}"
      "-DRT_OBJS=${ZAN_RT_SYNC_SELFHOST_OBJ}"
      -P ${CMAKE_SOURCE_DIR}/tests/run_fixedpoint.cmake)
  set_tests_properties(selfhost_fixed_point PROPERTIES TIMEOUT 1200)
endif()

# ---- Gui conformance: headless layout/text math (needs the zan_gui lib) ----
# These programs import Gui modules that reference [DllImport("zan_gui")], so the
# produced executable must link zan_gui. zanc resolves [DllImport] libs against
# $ZAN_LIB_PATH, so the tests point that (and the OS loader path) at the freshly
# built zan_gui. Enabled wherever zan_gui is available; the Windows CI job builds
# only zanc, so it passes -DZAN_BUILD_GUI_TESTS=OFF.
option(ZAN_BUILD_GUI_TESTS "Run the Gui conformance tests (requires zan_gui)" ON)
if(ZAN_BUILD_GUI_TESTS AND ZAN_GUI_AVAILABLE)
  add_executable(zan_gui_runtime_test tests/runtime/gui_runtime_test.c)
  target_link_libraries(zan_gui_runtime_test PRIVATE zan_gui)
  add_test(NAME runtime_gui COMMAND zan_gui_runtime_test)

  # A4: surface dimension validation -- rejects non-positive / oversize dims.
  add_executable(zan_gui_surface_dims_test tests/runtime/gui_surface_dims_test.c)
  target_link_libraries(zan_gui_surface_dims_test PRIVATE zan_gui)
  add_test(NAME runtime_gui_surface_dims COMMAND zan_gui_surface_dims_test)

  if(UNIX AND NOT APPLE AND NOT ZAN_GUI_SDL)
    find_program(XVFB_RUN_EXECUTABLE xvfb-run)
    if(XVFB_RUN_EXECUTABLE)
      add_executable(zan_gui_x11_test tests/runtime/gui_x11_test.c)
      target_link_libraries(zan_gui_x11_test PRIVATE zan_gui ${X11_LIBRARIES})
      add_test(NAME runtime_gui_x11
        COMMAND ${XVFB_RUN_EXECUTABLE} -a $<TARGET_FILE:zan_gui_x11_test>)
      set_tests_properties(runtime_gui_x11 PROPERTIES TIMEOUT 15)

      # Borderless-window caption hit-testing needs synthetic pointer events.
      if(X11_XTest_FOUND)
        add_executable(zan_gui_titlebar_test tests/runtime/gui_titlebar_test.c)
        target_link_libraries(zan_gui_titlebar_test
          PRIVATE zan_gui ${X11_LIBRARIES} ${X11_XTest_LIB})
        add_test(NAME runtime_gui_titlebar
          COMMAND ${XVFB_RUN_EXECUTABLE} -a $<TARGET_FILE:zan_gui_titlebar_test>)
        set_tests_properties(runtime_gui_titlebar PROPERTIES TIMEOUT 20)

        # Multi-window event routing also drives synthetic pointer events.
        add_executable(zan_gui_multiwindow_test tests/runtime/gui_multiwindow_test.c)
        target_link_libraries(zan_gui_multiwindow_test
          PRIVATE zan_gui ${X11_LIBRARIES} ${X11_XTest_LIB})
        add_test(NAME runtime_gui_multiwindow
          COMMAND ${XVFB_RUN_EXECUTABLE} -a $<TARGET_FILE:zan_gui_multiwindow_test>)
        set_tests_properties(runtime_gui_multiwindow PROPERTIES TIMEOUT 20)
      endif()
    endif()
  endif()

  add_test(NAME conformance_gui_stack
    COMMAND ${CMAKE_COMMAND}
      -DZANC=$<TARGET_FILE:zanc>
      -DSRC=${CMAKE_SOURCE_DIR}/tests/gui/layout_test.zan
      # Types.zan is pulled in automatically by 'using Gui;' (auto-stdlib), so
      # only the non-auto-included Stack.zan is passed explicitly here.
      # --driver-dir: link the freshly built zan_gui, not the committed
      # driver bundle (which may lag behind newly added native exports).
      "-DZANC_ARGS=${CMAKE_SOURCE_DIR}/stdlib/Gui/Stack.zan${_gui_driver_tail}"
      -DEXPECTED=${CMAKE_SOURCE_DIR}/tests/conformance/gui_stack.out
      -DOUT_EXE=${CMAKE_BINARY_DIR}/conf_gui_stack${_exe_ext}
      -P ${CMAKE_SOURCE_DIR}/tests/run_case.cmake)

  # Closing a tab removes it from the list the strip is iterating; the widget
  # must not be left pointing at an entry that is gone.
  add_test(NAME conformance_gui_tabs
    COMMAND ${CMAKE_COMMAND}
      -DZANC=$<TARGET_FILE:zanc>
      -DSRC=${CMAKE_SOURCE_DIR}/tests/gui/tabs_test.zan
      "-DZANC_ARGS=${CMAKE_SOURCE_DIR}/stdlib/Gui/Widget/Tabs.zan;--auto-stdlib${_gui_driver_tail}"
      -DEXPECTED=${CMAKE_SOURCE_DIR}/tests/conformance/gui_tabs.out
      -DOUT_EXE=${CMAKE_BINARY_DIR}/conf_gui_tabs${_exe_ext}
      -P ${CMAKE_SOURCE_DIR}/tests/run_case.cmake)

  # Dynamic tags model bookkeeping: add/remove raise exactly one Change each,
  # Max gates the interactive trigger only, and the `|`-joined options text
  # round-trips through GetExtra/SetExtra for the designer.
  add_test(NAME conformance_gui_dynamictags
    COMMAND ${CMAKE_COMMAND}
      -DZANC=$<TARGET_FILE:zanc>
      -DSRC=${CMAKE_SOURCE_DIR}/tests/gui/dynamictags_test.zan
      "-DZANC_ARGS=${CMAKE_SOURCE_DIR}/stdlib/Gui/Widget/DynamicTags.zan;--auto-stdlib${_gui_driver_tail}"
      -DEXPECTED=${CMAKE_SOURCE_DIR}/tests/conformance/gui_dynamictags.out
      -DOUT_EXE=${CMAKE_BINARY_DIR}/conf_gui_dynamictags${_exe_ext}
      -P ${CMAKE_SOURCE_DIR}/tests/run_case.cmake)

  # Switch 增强回归：SizeClsFor 四档映射（tiny 是 Switch 独有的真实
  # 档位）、圆/方轨道几何、内嵌内容的轨道加宽、spinner 相位、新属性
  # （checkedText/Icon/Color、unchecked*、checkedValue/uncheckedValue）
  # 的 GetProp/SetProp 往返，bool 轴 toggle，以及 int 轴（BindValue）
  # 「值==选中值」语义与 value 键的值语义拦截。
  add_test(NAME conformance_gui_switch
    COMMAND ${CMAKE_COMMAND}
      -DZANC=$<TARGET_FILE:zanc>
      -DSRC=${CMAKE_SOURCE_DIR}/tests/gui/switch_test.zan
      "-DZANC_ARGS=--auto-stdlib${_gui_driver_tail}"
      -DEXPECTED=${CMAKE_SOURCE_DIR}/tests/conformance/gui_switch.out
      -DOUT_EXE=${CMAKE_BINARY_DIR}/conf_gui_switch${_exe_ext}
      -P ${CMAKE_SOURCE_DIR}/tests/run_case.cmake)

  # 滚轮路由回归（用户报告“窗口有滚动条时滚轮跳跃”）：逐格线性不重
  # 放不丢格；指针瞬移进滚动区时泵期 cap 判据过期仍兜底出整帧（旧代
  # 码静默吞格→步幅忽大忽小）；嵌套声明者独占一格、外层冻结（修复
  # 前图表 hover 消费与外层翻页双触发）。
  add_test(NAME conformance_gui_wheel_route
    COMMAND ${CMAKE_COMMAND}
      -DZANC=$<TARGET_FILE:zanc>
      -DSRC=${CMAKE_SOURCE_DIR}/tests/gui/wheel_route_test.zan
      "-DZANC_ARGS=--auto-stdlib${_gui_driver_tail}"
      -DEXPECTED=${CMAKE_SOURCE_DIR}/tests/conformance/gui_wheel_route.out
      -DOUT_EXE=${CMAKE_BINARY_DIR}/conf_gui_wheel_route${_exe_ext}
      -P ${CMAKE_SOURCE_DIR}/tests/run_case.cmake)

  # ScrollView 拖拽重锚定回归（用户报告“滚动条滚动时界面卡死/页面自
  # 己跑”）：拖拽期间内容高度变化（自动量高、图片加载）时旧锚点公式
  # off = dragOffset0 + dyPix*mo/travel 用新 mo 配旧锚点 —— 按住不动
  # 也会自己滚，mo 越大正反馈越强。修复后 mo 变化即以当前位置重锚。
  add_test(NAME conformance_gui_scroll_drag_reanchor
    COMMAND ${CMAKE_COMMAND}
      -DZANC=$<TARGET_FILE:zanc>
      -DSRC=${CMAKE_SOURCE_DIR}/tests/gui/scroll_drag_reanchor_test.zan
      "-DZANC_ARGS=--auto-stdlib${_gui_driver_tail}"
      -DEXPECTED=${CMAKE_SOURCE_DIR}/tests/conformance/gui_scroll_drag_reanchor.out
      -DOUT_EXE=${CMAKE_BINARY_DIR}/conf_gui_scroll_drag_reanchor${_exe_ext}
      -P ${CMAKE_SOURCE_DIR}/tests/run_case.cmake)

  # VirtualList template rows: rows are built lazily (never at SetRows, only
  # when a frame needs them in the viewport) and the string-mode bookkeeping
  # stays intact across the mode switch.
  add_test(NAME conformance_gui_virtuallist
    COMMAND ${CMAKE_COMMAND}
      -DZANC=$<TARGET_FILE:zanc>
      -DSRC=${CMAKE_SOURCE_DIR}/tests/gui/virtuallist_test.zan
      "-DZANC_ARGS=${CMAKE_SOURCE_DIR}/stdlib/Gui/Widget/VirtualList.zan;--auto-stdlib${_gui_driver_tail}"
      -DEXPECTED=${CMAKE_SOURCE_DIR}/tests/conformance/gui_virtuallist.out
      -DOUT_EXE=${CMAKE_BINARY_DIR}/conf_gui_virtuallist${_exe_ext}
      -P ${CMAKE_SOURCE_DIR}/tests/run_case.cmake)

  # The New Project wizard rescales a template's .zform designs to the chosen
  # device. Retargeting must touch geometry only: an earlier round-trip through
  # a geometry-only model dropped every control's "kind", so a scaffolded
  # project came out with all-Input controls and would not compile.
  # Every template the New Project wizard offers must produce a project that
  # compiles; nothing else in the suite compiles templates/, so a stdlib API
  # change rots them silently until a user hits the error in the IDE.
  add_test(NAME templates_build
    COMMAND ${CMAKE_COMMAND}
      -DZANC=$<TARGET_FILE:zanc>
      -DTEMPLATES=${CMAKE_SOURCE_DIR}/templates
      -DSTDLIB=${CMAKE_SOURCE_DIR}/stdlib
      -DDRIVER_DIR=$<TARGET_FILE_DIR:zan_gui>
      -DWORK=${CMAKE_BINARY_DIR}/templates_build
      -DEXE_EXT=${_exe_ext}
      -P ${CMAKE_SOURCE_DIR}/tests/run_templates.cmake)
  set_tests_properties(templates_build PROPERTIES TIMEOUT 900)

  add_test(NAME conformance_gui_retarget
    COMMAND ${CMAKE_COMMAND}
      -DZANC=$<TARGET_FILE:zanc>
      -DSRC=${CMAKE_SOURCE_DIR}/tests/gui/retarget_test.zan
      "-DZANC_ARGS=--auto-stdlib${_gui_driver_tail}"
      -DEXPECTED=${CMAKE_SOURCE_DIR}/tests/conformance/gui_retarget.out
      -DOUT_EXE=${CMAKE_BINARY_DIR}/conf_gui_retarget${_exe_ext}
      -P ${CMAKE_SOURCE_DIR}/tests/run_case.cmake)

  add_test(NAME conformance_gui_icon
    COMMAND ${CMAKE_COMMAND}
      -DZANC=$<TARGET_FILE:zanc>
      -DSRC=${CMAKE_SOURCE_DIR}/tests/gui/icon_test.zan
      "-DZANC_ARGS=${CMAKE_SOURCE_DIR}/stdlib/Gui/Icon.zan;--auto-stdlib${_gui_driver_tail}"
      -DEXPECTED=${CMAKE_SOURCE_DIR}/tests/conformance/gui_icon.out
      -DOUT_EXE=${CMAKE_BINARY_DIR}/conf_gui_icon${_exe_ext}
      -P ${CMAKE_SOURCE_DIR}/tests/run_case.cmake)

  add_test(NAME conformance_gui_image
    COMMAND ${CMAKE_COMMAND}
      -DZANC=$<TARGET_FILE:zanc>
      -DSRC=${CMAKE_SOURCE_DIR}/tests/gui/image_test.zan
      "-DZANC_ARGS=--auto-stdlib${_gui_driver_tail}"
      -DEXPECTED=${CMAKE_SOURCE_DIR}/tests/conformance/gui_image.out
      -DOUT_EXE=${CMAKE_BINARY_DIR}/conf_gui_image${_exe_ext}
      -P ${CMAKE_SOURCE_DIR}/tests/run_case.cmake)

  add_test(NAME conformance_gui_countdown
    COMMAND ${CMAKE_COMMAND}
      -DZANC=$<TARGET_FILE:zanc>
      -DSRC=${CMAKE_SOURCE_DIR}/tests/gui/countdown_test.zan
      "-DZANC_ARGS=--auto-stdlib${_gui_driver_tail}"
      -DEXPECTED=${CMAKE_SOURCE_DIR}/tests/conformance/gui_countdown.out
      -DOUT_EXE=${CMAKE_BINARY_DIR}/conf_gui_countdown${_exe_ext}
      -P ${CMAKE_SOURCE_DIR}/tests/run_case.cmake)

  # 上传组件：模型往返（Kind/15 Props/Events/工厂）、门与状态机
  # （Accept/Max/BeforeUpload/Disabled、CustomRequest、Submit、
  #  SetItem* 与 Finish/Error/Remove/Clear 计数、LastItem、Upload
  # 别名路由），以及无头渲染一帧后的触发/行/链接/下载命中区、
  # 拖放落区与 ::trigger 主题色非回退断言。进度全部显式驱动。
  add_test(NAME conformance_gui_upload
    COMMAND ${CMAKE_COMMAND}
      -DZANC=$<TARGET_FILE:zanc>
      -DSRC=${CMAKE_SOURCE_DIR}/tests/gui/upload_test.zan
      "-DZANC_ARGS=--auto-stdlib${_gui_driver_tail}"
      -DEXPECTED=${CMAKE_SOURCE_DIR}/tests/conformance/gui_upload.out
      -DOUT_EXE=${CMAKE_BINARY_DIR}/conf_gui_upload${_exe_ext}
      -P ${CMAKE_SOURCE_DIR}/tests/run_case.cmake)

  # 分页组件：Element Plus 页码窗口算法（省略号 = 0 占位，领起/
  # 居中/收尾三态与 slots 边界）、item-count 计页与钳制、SetPage
  # 的 Change 恰好一次、sizes/段顺序/自定义文案往返，以及组件
  # 模型（Props/GetProp/SetProp/SetExtra/Events/ControlFactory）。
  add_test(NAME conformance_gui_pagination
    COMMAND ${CMAKE_COMMAND}
      -DZANC=$<TARGET_FILE:zanc>
      -DSRC=${CMAKE_SOURCE_DIR}/tests/gui/pagination_test.zan
      "-DZANC_ARGS=--auto-stdlib${_gui_driver_tail}"
      -DEXPECTED=${CMAKE_SOURCE_DIR}/tests/conformance/gui_pagination.out
      -DOUT_EXE=${CMAKE_BINARY_DIR}/conf_gui_pagination${_exe_ext}
      -P ${CMAKE_SOURCE_DIR}/tests/run_case.cmake)

  # 数值滚动动画（countup.js 风格）：ValueAt 插值与 Format 千分位/
  # 国际化纯函数，tick 注入驱动的走动/暂停恢复无跳变/重摆/Finish
  # 单次触发，以及组件模型（Kind/Props/Events/ControlFactory）。
  add_test(NAME conformance_gui_numberanimation
    COMMAND ${CMAKE_COMMAND}
      -DZANC=$<TARGET_FILE:zanc>
      -DSRC=${CMAKE_SOURCE_DIR}/tests/gui/numberanimation_test.zan
      "-DZANC_ARGS=--auto-stdlib${_gui_driver_tail}"
      -DEXPECTED=${CMAKE_SOURCE_DIR}/tests/conformance/gui_numberanimation.out
      -DOUT_EXE=${CMAKE_BINARY_DIR}/conf_gui_numberanimation${_exe_ext}
      -P ${CMAKE_SOURCE_DIR}/tests/run_case.cmake)

  # 步骤指示器（Naive UI n-steps）：StatusNorm 白名单、StateClass
  # 位置→状态类、items 串往返（"A|B=desc"）、GetProp/SetProp 往返
  # （placement/status/finishIcon/errorIcon/clickable + class/name
  # 走基类）、GetExtra/SetExtra("items")、Change 事件接线。
  add_test(NAME conformance_gui_steps
    COMMAND ${CMAKE_COMMAND}
      -DZANC=$<TARGET_FILE:zanc>
      -DSRC=${CMAKE_SOURCE_DIR}/tests/gui/steps_test.zan
      "-DZANC_ARGS=--auto-stdlib${_gui_driver_tail}"
      -DEXPECTED=${CMAKE_SOURCE_DIR}/tests/conformance/gui_steps.out
      -DOUT_EXE=${CMAKE_BINARY_DIR}/conf_gui_steps${_exe_ext}
      -P ${CMAKE_SOURCE_DIR}/tests/run_case.cmake)

  # 时间线强化：TimelineItem 构造重载（icon/pending 字段）、SizeClass
  # 映射、horizontal/right 形态判定（token 匹配）、items 串往返
  # （`;` 分段——时间自带冒号；pending 尾段）、Of/AddItemIcon、
  # Props 键序（size/class）与 Size→SizeCls 联动。
  add_test(NAME conformance_gui_timeline
    COMMAND ${CMAKE_COMMAND}
      -DZANC=$<TARGET_FILE:zanc>
      -DSRC=${CMAKE_SOURCE_DIR}/tests/gui/timeline_test.zan
      "-DZANC_ARGS=--auto-stdlib${_gui_driver_tail}"
      -DEXPECTED=${CMAKE_SOURCE_DIR}/tests/conformance/gui_timeline.out
      -DOUT_EXE=${CMAKE_BINARY_DIR}/conf_gui_timeline${_exe_ext}
      -P ${CMAKE_SOURCE_DIR}/tests/run_case.cmake)

  # DatePicker 十一种形式的文本往返（起止颠倒交换、week 周一起始、
  # 季度小写归一、非法文本保留原值），以及弹层指针捕获：弹层展开后
  # 压住邻居的触发按钮，鼠标在弹层里滑动不得让邻居抢走 current 把
  # 正开着的日历关掉（归属判定必须走 Ui.Over/PointerCaptured）。
  add_test(NAME conformance_gui_datepicker
    COMMAND ${CMAKE_COMMAND}
      -DZANC=$<TARGET_FILE:zanc>
      -DSRC=${CMAKE_SOURCE_DIR}/tests/gui/datepicker_test.zan
      "-DZANC_ARGS=--auto-stdlib${_gui_driver_tail}"
      -DEXPECTED=${CMAKE_SOURCE_DIR}/tests/conformance/gui_datepicker.out
      -DOUT_EXE=${CMAKE_BINARY_DIR}/conf_gui_datepicker${_exe_ext}
      -P ${CMAKE_SOURCE_DIR}/tests/run_case.cmake)

  add_test(NAME conformance_gui_watermark
    COMMAND ${CMAKE_COMMAND}
      -DZANC=$<TARGET_FILE:zanc>
      -DSRC=${CMAKE_SOURCE_DIR}/tests/gui/watermark_test.zan
      "-DZANC_ARGS=--auto-stdlib${_gui_driver_tail}"
      -DEXPECTED=${CMAKE_SOURCE_DIR}/tests/conformance/gui_watermark.out
      -DOUT_EXE=${CMAKE_BINARY_DIR}/conf_gui_watermark${_exe_ext}
      -P ${CMAKE_SOURCE_DIR}/tests/run_case.cmake)

  add_test(NAME conformance_gui_chart_symbol
    COMMAND ${CMAKE_COMMAND}
      -DZANC=$<TARGET_FILE:zanc>
      -DSRC=${CMAKE_SOURCE_DIR}/tests/gui/chart_symbol_test.zan
      "-DZANC_ARGS=--auto-stdlib${_gui_driver_tail}"
      -DEXPECTED=${CMAKE_SOURCE_DIR}/tests/conformance/gui_chart_symbol.out
      -DOUT_EXE=${CMAKE_BINARY_DIR}/conf_gui_chart_symbol${_exe_ext}
      -P ${CMAKE_SOURCE_DIR}/tests/run_case.cmake)

  add_test(NAME conformance_gui_layer
    COMMAND ${CMAKE_COMMAND}
      -DZANC=$<TARGET_FILE:zanc>
      -DSRC=${CMAKE_SOURCE_DIR}/tests/gui/layer_test.zan
      "-DZANC_ARGS=--auto-stdlib${_gui_driver_tail}"
      -DEXPECTED=${CMAKE_SOURCE_DIR}/tests/conformance/gui_layer.out
      -DOUT_EXE=${CMAKE_BINARY_DIR}/conf_gui_layer${_exe_ext}
      -P ${CMAKE_SOURCE_DIR}/tests/run_case.cmake)

  add_test(NAME conformance_gui_text
    COMMAND ${CMAKE_COMMAND}
      -DZANC=$<TARGET_FILE:zanc>
      -DSRC=${CMAKE_SOURCE_DIR}/tests/gui/text_test.zan
      "-DZANC_ARGS=${CMAKE_SOURCE_DIR}/stdlib/Gui/Text.zan${_gui_driver_tail}"
      -DEXPECTED=${CMAKE_SOURCE_DIR}/tests/conformance/gui_text.out
      -DOUT_EXE=${CMAKE_BINARY_DIR}/conf_gui_text${_exe_ext}
      -P ${CMAKE_SOURCE_DIR}/tests/run_case.cmake)

  add_test(NAME conformance_gui_codeeditor
    COMMAND ${CMAKE_COMMAND}
      -DZANC=$<TARGET_FILE:zanc>
      -DSRC=${CMAKE_SOURCE_DIR}/tests/gui/codeeditor_test.zan
      "-DZANC_ARGS=${CMAKE_SOURCE_DIR}/stdlib/Gui/Component/CodeEditor/CodeEditor.zan;--auto-stdlib${_gui_driver_tail}"
      -DEXPECTED=${CMAKE_SOURCE_DIR}/tests/conformance/gui_codeeditor.out
      -DOUT_EXE=${CMAKE_BINARY_DIR}/conf_gui_codeeditor${_exe_ext}
      -P ${CMAKE_SOURCE_DIR}/tests/run_case.cmake)

  add_test(NAME conformance_gui_datatable
    COMMAND ${CMAKE_COMMAND}
      -DZANC=$<TARGET_FILE:zanc>
      -DSRC=${CMAKE_SOURCE_DIR}/tests/gui/datatable_test.zan
      "-DZANC_ARGS=${CMAKE_SOURCE_DIR}/stdlib/Gui/Component/DataTable/DataTable.zan;--auto-stdlib${_gui_driver_tail}"
      -DEXPECTED=${CMAKE_SOURCE_DIR}/tests/conformance/gui_datatable.out
      -DOUT_EXE=${CMAKE_BINARY_DIR}/conf_gui_datatable${_exe_ext}
      -P ${CMAKE_SOURCE_DIR}/tests/run_case.cmake)

  add_test(NAME conformance_gui_datatable_identity
    COMMAND ${CMAKE_COMMAND}
      -DZANC=$<TARGET_FILE:zanc>
      -DSRC=${CMAKE_SOURCE_DIR}/tests/gui/datatable_identity_test.zan
      "-DZANC_ARGS=${CMAKE_SOURCE_DIR}/stdlib/Gui/Component/DataTable/DataTable.zan;--auto-stdlib${_gui_driver_tail}"
      -DEXPECTED=${CMAKE_SOURCE_DIR}/tests/conformance/gui_datatable_identity.out
      -DOUT_EXE=${CMAKE_BINARY_DIR}/conf_gui_datatable_identity${_exe_ext}
      -P ${CMAKE_SOURCE_DIR}/tests/run_case.cmake)

  add_test(NAME conformance_gui_datatable_query
    COMMAND ${CMAKE_COMMAND}
      -DZANC=$<TARGET_FILE:zanc>
      -DSRC=${CMAKE_SOURCE_DIR}/tests/gui/datatable_query_test.zan
      "-DZANC_ARGS=${CMAKE_SOURCE_DIR}/stdlib/Gui/Component/DataTable/DataTable.zan;--auto-stdlib${_gui_driver_tail}"
      -DEXPECTED=${CMAKE_SOURCE_DIR}/tests/conformance/gui_datatable_query.out
      -DOUT_EXE=${CMAKE_BINARY_DIR}/conf_gui_datatable_query${_exe_ext}
      -P ${CMAKE_SOURCE_DIR}/tests/run_case.cmake)

  add_test(NAME conformance_gui_datatable_topn
    COMMAND ${CMAKE_COMMAND}
      -DZANC=$<TARGET_FILE:zanc>
      -DSRC=${CMAKE_SOURCE_DIR}/tests/gui/datatable_topn_test.zan
      "-DZANC_ARGS=${CMAKE_SOURCE_DIR}/stdlib/Gui/Component/DataTable/DataTable.zan;--auto-stdlib${_gui_driver_tail}"
      -DEXPECTED=${CMAKE_SOURCE_DIR}/tests/conformance/gui_datatable_topn.out
      -DOUT_EXE=${CMAKE_BINARY_DIR}/conf_gui_datatable_topn${_exe_ext}
      -P ${CMAKE_SOURCE_DIR}/tests/run_case.cmake)

  add_test(NAME conformance_gui_datatable_multisort
    COMMAND ${CMAKE_COMMAND}
      -DZANC=$<TARGET_FILE:zanc>
      -DSRC=${CMAKE_SOURCE_DIR}/tests/gui/datatable_multisort_test.zan
      "-DZANC_ARGS=${CMAKE_SOURCE_DIR}/stdlib/Gui/Component/DataTable/DataTable.zan;--auto-stdlib${_gui_driver_tail}"
      -DEXPECTED=${CMAKE_SOURCE_DIR}/tests/conformance/gui_datatable_multisort.out
      -DOUT_EXE=${CMAKE_BINARY_DIR}/conf_gui_datatable_multisort${_exe_ext}
      -P ${CMAKE_SOURCE_DIR}/tests/run_case.cmake)

  add_test(NAME conformance_gui_datatable_sparse_page
    COMMAND ${CMAKE_COMMAND}
      -DZANC=$<TARGET_FILE:zanc>
      -DSRC=${CMAKE_SOURCE_DIR}/tests/gui/datatable_sparse_page_test.zan
      "-DZANC_ARGS=${CMAKE_SOURCE_DIR}/stdlib/Gui/Component/DataTable/DataTable.zan;--auto-stdlib${_gui_driver_tail}"
      -DEXPECTED=${CMAKE_SOURCE_DIR}/tests/conformance/gui_datatable_sparse_page.out
      -DOUT_EXE=${CMAKE_BINARY_DIR}/conf_gui_datatable_sparse_page${_exe_ext}
      -P ${CMAKE_SOURCE_DIR}/tests/run_case.cmake)

  add_test(NAME conformance_gui_datatable_query_source
    COMMAND ${CMAKE_COMMAND}
      -DZANC=$<TARGET_FILE:zanc>
      -DSRC=${CMAKE_SOURCE_DIR}/tests/gui/datatable_query_source_test.zan
      "-DZANC_ARGS=${CMAKE_SOURCE_DIR}/stdlib/Gui/Component/DataTable/DataTable.zan;--auto-stdlib${_gui_driver_tail}"
      -DEXPECTED=${CMAKE_SOURCE_DIR}/tests/conformance/gui_datatable_query_source.out
      -DOUT_EXE=${CMAKE_BINARY_DIR}/conf_gui_datatable_query_source${_exe_ext}
      -P ${CMAKE_SOURCE_DIR}/tests/run_case.cmake)

  add_test(NAME conformance_gui_datatable_local_source
    COMMAND ${CMAKE_COMMAND}
      -DZANC=$<TARGET_FILE:zanc>
      -DSRC=${CMAKE_SOURCE_DIR}/tests/gui/datatable_local_source_test.zan
      "-DZANC_ARGS=${CMAKE_SOURCE_DIR}/stdlib/Gui/Component/DataTable/DataTable.zan;--auto-stdlib${_gui_driver_tail}"
      -DEXPECTED=${CMAKE_SOURCE_DIR}/tests/conformance/gui_datatable_local_source.out
      -DOUT_EXE=${CMAKE_BINARY_DIR}/conf_gui_datatable_local_source${_exe_ext}
      -P ${CMAKE_SOURCE_DIR}/tests/run_case.cmake)

  # The property protocol (Props() specs -> Control.GetProp/SetProp) is what
  # the designer and the serializer talk to, and it is pure computation.
  # A `"role": "control"` .zform must build an embeddable Control: the designed
  # tree lands under the control itself and the designer's `label` reaches every
  # control publishing a `text` property (containers keep it as a design note).
  add_test(NAME conformance_gui_zform_control
    COMMAND ${CMAKE_COMMAND}
      -DZANC=$<TARGET_FILE:zanc>
      -DSRC=${CMAKE_SOURCE_DIR}/tests/gui/zform_control_test.zan
      "-DZANC_ARGS=${CMAKE_SOURCE_DIR}/tests/gui/zform_control.zform;--auto-stdlib${_gui_driver_tail}"
      -DEXPECTED=${CMAKE_SOURCE_DIR}/tests/conformance/gui_zform_control.out
      -DOUT_EXE=${CMAKE_BINARY_DIR}/conf_gui_zform_control${_exe_ext}
      -P ${CMAKE_SOURCE_DIR}/tests/run_case.cmake)

  # Declarative typed grid: a DataGrid field with "of" + "columns" projects
  # typed accessor lambdas onto the generic column API, binds the caller's
  # List<T> directly (no DataRow), and "props"/"rowFactory" reach the state.
  add_test(NAME conformance_gui_zform_grid
    COMMAND ${CMAKE_COMMAND}
      -DZANC=$<TARGET_FILE:zanc>
      -DSRC=${CMAKE_SOURCE_DIR}/tests/gui/zform_grid_test.zan
      "-DZANC_ARGS=${CMAKE_SOURCE_DIR}/tests/gui/zform_grid.zform;--auto-stdlib${_gui_driver_tail}"
      -DEXPECTED=${CMAKE_SOURCE_DIR}/tests/conformance/gui_zform_grid.out
      -DOUT_EXE=${CMAKE_BINARY_DIR}/conf_gui_zform_grid${_exe_ext}
      -P ${CMAKE_SOURCE_DIR}/tests/run_case.cmake)

  add_test(NAME conformance_gui_props
    COMMAND ${CMAKE_COMMAND}
      -DZANC=$<TARGET_FILE:zanc>
      -DSRC=${CMAKE_SOURCE_DIR}/tests/gui/prop_test.zan
      "-DZANC_ARGS=--auto-stdlib${_gui_driver_tail}"
      -DEXPECTED=${CMAKE_SOURCE_DIR}/tests/conformance/gui_props.out
      -DOUT_EXE=${CMAKE_BINARY_DIR}/conf_gui_props${_exe_ext}
      -P ${CMAKE_SOURCE_DIR}/tests/run_case.cmake)

  # FormGroup/FormRule: caption+control+error-line rows, rule evaluation
  # (required/length/email/range/checked/equals-field) and the dependent-form
  # semantics -- hidden/disabled rows skip validation.
  add_test(NAME conformance_gui_formgroup
    COMMAND ${CMAKE_COMMAND}
      -DZANC=$<TARGET_FILE:zanc>
      -DSRC=${CMAKE_SOURCE_DIR}/tests/gui/formgroup_test.zan
      "-DZANC_ARGS=--auto-stdlib${_gui_driver_tail}"
      -DEXPECTED=${CMAKE_SOURCE_DIR}/tests/conformance/gui_formgroup.out
      -DOUT_EXE=${CMAKE_BINARY_DIR}/conf_gui_formgroup${_exe_ext}
      -P ${CMAKE_SOURCE_DIR}/tests/run_case.cmake)

  # Calendar: the pure-logic surface without a window -- strict YYYY-MM-DD
  # parsing, the 7x6 grid's cell->date mapping (leading/trailing
  # adjacent-month cells), the prop protocol (text/mode), the
  # is-date-disabled filter, mark normalization to midnight, and how
  # change / panel-change fire on picks within and across months.
  add_test(NAME conformance_gui_calendar
    COMMAND ${CMAKE_COMMAND}
      -DZANC=$<TARGET_FILE:zanc>
      -DSRC=${CMAKE_SOURCE_DIR}/tests/gui/calendar_test.zan
      "-DZANC_ARGS=--auto-stdlib${_gui_driver_tail}"
      -DEXPECTED=${CMAKE_SOURCE_DIR}/tests/conformance/gui_calendar.out
      -DOUT_EXE=${CMAKE_BINARY_DIR}/conf_gui_calendar${_exe_ext}
      -P ${CMAKE_SOURCE_DIR}/tests/run_case.cmake)

  # The tree invariant every container shares: one place per control, so a
  # re-parent moves it, and a control never becomes its own descendant (a cycle
  # makes every recursive walk overflow the stack).
  add_test(NAME conformance_gui_tree
    COMMAND ${CMAKE_COMMAND}
      -DZANC=$<TARGET_FILE:zanc>
      -DSRC=${CMAKE_SOURCE_DIR}/tests/gui/tree_test.zan
      "-DZANC_ARGS=--auto-stdlib${_gui_driver_tail}"
      -DEXPECTED=${CMAKE_SOURCE_DIR}/tests/conformance/gui_tree.out
      -DOUT_EXE=${CMAKE_BINARY_DIR}/conf_gui_tree${_exe_ext}
      -P ${CMAKE_SOURCE_DIR}/tests/run_case.cmake)

  # CSS flex wrapping and the equal-column Grid are pure geometry: the case
  # feeds computed style boxes straight in and diffs the resolved rectangles,
  # so line breaking, align-content, flex-shrink/basis, align-self,
  # aspect-ratio and the grid's non-accumulating column edges are all pinned
  # without a window.
  add_test(NAME conformance_gui_flex
    COMMAND ${CMAKE_COMMAND}
      -DZANC=$<TARGET_FILE:zanc>
      -DSRC=${CMAKE_SOURCE_DIR}/tests/gui/flex_test.zan
      "-DZANC_ARGS=--auto-stdlib${_gui_driver_tail}"
      -DEXPECTED=${CMAKE_SOURCE_DIR}/tests/conformance/gui_flex.out
      -DOUT_EXE=${CMAKE_BINARY_DIR}/conf_gui_flex${_exe_ext}
      -P ${CMAKE_SOURCE_DIR}/tests/run_case.cmake)

  # The Carousel's slide placement is pure geometry: horizontal slides along
  # x, vertical along y (Direction("vertical")); the chrome overlay must stay
  # the LAST child -- lazily mounted on first Arrange and re-appended when a
  # slide view is added later -- so RenderTree paints it above image slides.
  # Pinned headlessly, no window needed.
  add_test(NAME conformance_gui_carousel
    COMMAND ${CMAKE_COMMAND}
      -DZANC=$<TARGET_FILE:zanc>
      -DSRC=${CMAKE_SOURCE_DIR}/tests/gui/carousel_test.zan
      "-DZANC_ARGS=--auto-stdlib${_gui_driver_tail}"
      -DEXPECTED=${CMAKE_SOURCE_DIR}/tests/conformance/gui_carousel.out
      -DOUT_EXE=${CMAKE_BINARY_DIR}/conf_gui_carousel${_exe_ext}
      -P ${CMAKE_SOURCE_DIR}/tests/run_case.cmake)

  # Rate reserves 2x its star count of hit-test ids: allowHalf paints a
  # left/right rect pair per star (baseId+i and baseId+maxStars+i), and
  # allowHalf is set after construction, so the reservation can't depend on
  # the flag. A plain Block(stars) lets the right-half ids run into the next
  # control's id space -- two Rates then hijack each other's hover/click
  # (hover row A previews on row B; a click on row B rewrites row A's value).
  # Pinned headlessly, no window needed.
  add_test(NAME conformance_gui_rate
    COMMAND ${CMAKE_COMMAND}
      -DZANC=$<TARGET_FILE:zanc>
      -DSRC=${CMAKE_SOURCE_DIR}/tests/gui/rate_test.zan
      "-DZANC_ARGS=--auto-stdlib${_gui_driver_tail}"
      -DEXPECTED=${CMAKE_SOURCE_DIR}/tests/conformance/gui_rate.out
      -DOUT_EXE=${CMAKE_BINARY_DIR}/conf_gui_rate${_exe_ext}
      -P ${CMAKE_SOURCE_DIR}/tests/run_case.cmake)

  # The Slider's value math is pure computation: step / step="mark"
  # quantization and keyboard steps, the orientation x reverse geometry
  # mapping (ValAt/PxAt are inverses), range-thumb read/write with two-way
  # bindings, the tooltip format template, and the designer marks
  # round-trip string. Pinned headlessly, no window needed.
  add_test(NAME conformance_gui_slider
    COMMAND ${CMAKE_COMMAND}
      -DZANC=$<TARGET_FILE:zanc>
      -DSRC=${CMAKE_SOURCE_DIR}/tests/gui/slider_test.zan
      "-DZANC_ARGS=--auto-stdlib${_gui_driver_tail}"
      -DEXPECTED=${CMAKE_SOURCE_DIR}/tests/conformance/gui_slider.out
      -DOUT_EXE=${CMAKE_BINARY_DIR}/conf_gui_slider${_exe_ext}
      -P ${CMAKE_SOURCE_DIR}/tests/run_case.cmake)

  # 穿梭框的无窗口回归：Bind 实时视图与勾选集合、MoveRight/MoveLeft 的
  # 行序搬移（倒序删不漂移、目标侧追加、移过去默认不勾）、过滤只搬命中
  # 行（Order 缓存按 计数+关键词 失效，SetQuery 驱动）、外部改列表 +
  # Refresh 后缓存重建、ClearChecked 全量/过滤两态、oneWay，及组件模型
  # （Kind/StyleType/Props 经 Binding 活写入往返/Events/ControlFactory）。
  add_test(NAME conformance_gui_transfer
    COMMAND ${CMAKE_COMMAND}
      -DZANC=$<TARGET_FILE:zanc>
      -DSRC=${CMAKE_SOURCE_DIR}/tests/gui/transfer_test.zan
      "-DZANC_ARGS=--auto-stdlib${_gui_driver_tail}"
      -DEXPECTED=${CMAKE_SOURCE_DIR}/tests/conformance/gui_transfer.out
      -DOUT_EXE=${CMAKE_BINARY_DIR}/conf_gui_transfer${_exe_ext}
      -P ${CMAKE_SOURCE_DIR}/tests/run_case.cmake)

  # ColorPicker 的无窗口回归：32 位负打包色的通道提取/构造/比较（
  # 0xAARRGGBB 回绕成负 int），HSV/HSL 纯色与灰阶边界，HEX/函数串
  # 解析（parseOk 成功标志——高 alpha 打包色回绕成负数后 -1 不能再
  # 当失败哨兵），modes 缩小后的取值格式跟随，designer Extra 往返，
  # 以及弹层帧推演：blocker 登记、透明度条 id 在色相条与输入行之间
  # 命中可见（rowY 双扣曾让输入行压住透明度条）、弹层外点击关闭、
  # 触发条重开、Change 事件与双向绑定。
  add_test(NAME conformance_gui_colorpicker
    COMMAND ${CMAKE_COMMAND}
      -DZANC=$<TARGET_FILE:zanc>
      -DSRC=${CMAKE_SOURCE_DIR}/tests/gui/colorpicker_test.zan
      "-DZANC_ARGS=--auto-stdlib${_gui_driver_tail}"
      -DEXPECTED=${CMAKE_SOURCE_DIR}/tests/conformance/gui_colorpicker.out
      -DOUT_EXE=${CMAKE_BINARY_DIR}/conf_gui_colorpicker${_exe_ext}
      -P ${CMAKE_SOURCE_DIR}/tests/run_case.cmake)

  # SelectBox 级联增强：父子指针链（单选路径/重开自动展开/联动关闭
  # 选分支）、多选勾选模型（联动带动、祖链回推、半勾）、勾选策略
  # 取值（all/parent/child）、级联过滤平铺，以及弹层 blocker、悬停
  # 行到平铺下标的映射和列滚动钳制。
  add_test(NAME conformance_gui_selectbox_cascade
    COMMAND ${CMAKE_COMMAND}
      -DZANC=$<TARGET_FILE:zanc>
      -DSRC=${CMAKE_SOURCE_DIR}/tests/gui/selectbox_cascade_test.zan
      "-DZANC_ARGS=--auto-stdlib${_gui_driver_tail}"
      -DEXPECTED=${CMAKE_SOURCE_DIR}/tests/conformance/gui_selectbox_cascade.out
      -DOUT_EXE=${CMAKE_BINARY_DIR}/conf_gui_selectbox_cascade${_exe_ext}
      -P ${CMAKE_SOURCE_DIR}/tests/run_case.cmake)

  # SelectBox 弹层选项点击的基线命中回归：弹层「内部控件让行」的
  # 判定只看弹层基线（OnPaintOverlay 入口）之后注册的区域——按真实
  # 帧序重放内容滑杆/全窗+弹层 blocker/内嵌滚动条的注册序列，锁住
  # 基线跳过内容区、滚动条仍让行、blocker 不算控件、顶层优先。
  add_test(NAME conformance_gui_selectbox_popup_hit
    COMMAND ${CMAKE_COMMAND}
      -DZANC=$<TARGET_FILE:zanc>
      -DSRC=${CMAKE_SOURCE_DIR}/tests/gui/selectbox_popup_hit_test.zan
      "-DZANC_ARGS=--auto-stdlib${_gui_driver_tail}"
      -DEXPECTED=${CMAKE_SOURCE_DIR}/tests/conformance/gui_selectbox_popup_hit.out
      -DOUT_EXE=${CMAKE_BINARY_DIR}/conf_gui_selectbox_popup_hit${_exe_ext}
      -P ${CMAKE_SOURCE_DIR}/tests/run_case.cmake)

  # Menu v2 (tree menu): pure-model flatten/default-expand asserts plus a
  # headless-frame pass over hit registration and the three batch render hooks.
  add_test(NAME conformance_gui_menu
    COMMAND ${CMAKE_COMMAND}
      -DZANC=$<TARGET_FILE:zanc>
      -DSRC=${CMAKE_SOURCE_DIR}/tests/gui/menu_test.zan
      "-DZANC_ARGS=--auto-stdlib${_gui_driver_tail}"
      -DEXPECTED=${CMAKE_SOURCE_DIR}/tests/conformance/gui_menu.out
      -DOUT_EXE=${CMAKE_BINARY_DIR}/conf_gui_menu${_exe_ext}
      -P ${CMAKE_SOURCE_DIR}/tests/run_case.cmake)

  # InputNumber's value math is pure computation too: the fixed-point
  # display/parse pair (NumberText / ParseScaled incl. half-up rounding and
  # comma-grouping), the Naive-UI format/parse hooks, the validator's error
  # flag + Invalid event, step/clamp, and the designer Extra round-trip
  # (precision / group / loading / prefix / suffix / placement).
  # Pinned headlessly, no window needed.
  add_test(NAME conformance_gui_inputnumber
    COMMAND ${CMAKE_COMMAND}
      -DZANC=$<TARGET_FILE:zanc>
      -DSRC=${CMAKE_SOURCE_DIR}/tests/gui/inputnumber_test.zan
      "-DZANC_ARGS=--auto-stdlib${_gui_driver_tail}"
      -DEXPECTED=${CMAKE_SOURCE_DIR}/tests/conformance/gui_inputnumber.out
      -DOUT_EXE=${CMAKE_BINARY_DIR}/conf_gui_inputnumber${_exe_ext}
      -P ${CMAKE_SOURCE_DIR}/tests/run_case.cmake)

  # The CSS skin pipeline (Css parser -> StyleSheet -> StyleBox) is pure
  # computation, so it is verified headlessly against a golden dump.
  add_test(NAME conformance_gui_css
    COMMAND ${CMAKE_COMMAND}
      -DZANC=$<TARGET_FILE:zanc>
      -DSRC=${CMAKE_SOURCE_DIR}/tests/gui/css_test.zan
      "-DZANC_ARGS=${CMAKE_SOURCE_DIR}/stdlib/Gui/Css.zan;${CMAKE_SOURCE_DIR}/stdlib/Gui/StyleSheet.zan;${CMAKE_SOURCE_DIR}/stdlib/Gui/StyleBox.zan;--auto-stdlib${_gui_driver_tail}"
      -DEXPECTED=${CMAKE_SOURCE_DIR}/tests/conformance/gui_css.out
      -DOUT_EXE=${CMAKE_BINARY_DIR}/conf_gui_css${_exe_ext}
      -P ${CMAKE_SOURCE_DIR}/tests/run_case.cmake)

  # The Zan-side COM interop the WebView2 backend is built from (GUID
  # marshalling, vtable objects, slot dispatch, URL/profile helpers) is pure
  # computation; embedding a live browser is covered by the smoke test.
  add_test(NAME conformance_gui_webview_interop
    COMMAND ${CMAKE_COMMAND}
      -DZANC=$<TARGET_FILE:zanc>
      -DSRC=${CMAKE_SOURCE_DIR}/tests/gui/webview_interop_test.zan
      "-DZANC_ARGS=${CMAKE_SOURCE_DIR}/stdlib/Gui/Component/WebView/WebView2.zan;--auto-stdlib${_gui_driver_tail}"
      -DEXPECTED=${CMAKE_SOURCE_DIR}/tests/conformance/gui_webview_interop.out
      -DOUT_EXE=${CMAKE_BINARY_DIR}/conf_gui_webview_interop${_exe_ext}
      -P ${CMAKE_SOURCE_DIR}/tests/run_case.cmake)

  # Button.Color's CSS injection must be idempotent per color per sheet,
  # keyed on the rule's presence in the sheet. The old guard keyed on
  # bgThemeGen (which the injection itself bumps) and per-widget fields
  # (which immediate-mode UIs reset every frame by rebuilding their
  # widgets), so any frame that rendered a Color button bumped the
  # generation again: the whole style cache and baseSheets were dropped
  # and base.css re-parsed several times per frame until the app froze.
  # Also pins that the selector engine matches [class*="frag"] rules.
  add_test(NAME conformance_gui_colorinject
    COMMAND ${CMAKE_COMMAND}
      -DZANC=$<TARGET_FILE:zanc>
      -DSRC=${CMAKE_SOURCE_DIR}/tests/gui/colorinject_test.zan
      "-DZANC_ARGS=--auto-stdlib${_gui_driver_tail}"
      -DEXPECTED=${CMAKE_SOURCE_DIR}/tests/conformance/gui_colorinject.out
      -DOUT_EXE=${CMAKE_BINARY_DIR}/conf_gui_colorinject${_exe_ext}
      -P ${CMAKE_SOURCE_DIR}/tests/run_case.cmake)

  set_tests_properties(runtime_gui conformance_gui_stack conformance_gui_flex conformance_gui_carousel conformance_gui_text conformance_gui_codeeditor conformance_gui_datatable conformance_gui_css conformance_gui_props conformance_gui_tree conformance_gui_menu conformance_gui_webview_interop conformance_gui_colorinject conformance_gui_calendar conformance_gui_rate PROPERTIES
    ENVIRONMENT "ZAN_LIB_PATH=$<TARGET_FILE_DIR:zan_gui>;LD_LIBRARY_PATH=$<TARGET_FILE_DIR:zan_gui>;DYLD_LIBRARY_PATH=$<TARGET_FILE_DIR:zan_gui>")
endif()

# ---- Policy: components own their drawing, examples only use them ----
# A gauge drawn a second time inside a demo drifts from the component the moment
# the component is fixed -- so the arc/sector primitives a dial is made of are
# banned outside stdlib (games excepted; they render their own scene).
add_test(NAME policy_no_widget_drawing
  COMMAND ${CMAKE_COMMAND} -DROOT=${CMAKE_SOURCE_DIR}
    -P ${CMAKE_SOURCE_DIR}/tests/run_no_widget_drawing.cmake)

add_test(NAME policy_gallery_coverage
  COMMAND ${CMAKE_COMMAND} -DROOT=${CMAKE_SOURCE_DIR}
    -P ${CMAKE_SOURCE_DIR}/tests/run_gallery_coverage.cmake)

add_test(NAME policy_zform_schema
  COMMAND ${CMAKE_COMMAND} -DROOT=${CMAKE_SOURCE_DIR}
    -P ${CMAKE_SOURCE_DIR}/tests/run_zform_schema.cmake)

add_test(NAME policy_control_factory
  COMMAND ${CMAKE_COMMAND} -DROOT=${CMAKE_SOURCE_DIR}
    -P ${CMAKE_SOURCE_DIR}/tests/run_control_factory.cmake)

# ---- Policy: widget draw paths must not gain direct semantic-color reads ----
# Colors belong to the style layer (base.css + Style/StyleBox/Fx); each direct
# `t.<color>` read in a draw path is frozen CSS debt with a per-file budget
# that may only go down.
add_test(NAME policy_theme_color_budget
  COMMAND ${CMAKE_COMMAND} -DROOT=${CMAKE_SOURCE_DIR}
    -P ${CMAKE_SOURCE_DIR}/tests/run_theme_color_budget.cmake)

# ---- Policy: a CSS comment body must not contain `*/` ----
# Comments do not nest, so a `*/` inside one ends it early and the remaining
# prose glues itself onto the next rule's first selector -- which is how the
# `input` base rule silently vanished (no border, no field background) while
# `textarea`/`select` in the same selector list kept theirs.
add_test(NAME policy_css_comment_hygiene
  COMMAND ${CMAKE_COMMAND} -DROOT=${CMAKE_SOURCE_DIR}
    -P ${CMAKE_SOURCE_DIR}/tests/run_css_comment_hygiene.cmake)
set_tests_properties(policy_css_comment_hygiene PROPERTIES
  LABELS "full;standard;smoke;gui")

# ---- Policy: every gallery demo that is written must be reachable ----
# A demo is lost silently in two ways: an overlay demo missing from the
# IsOverlayComp routing list (the base pass then paints a placeholder over it --
# Compose and DataTable), or a render branch whose preview id no Comp registers
# (dead code -- Alert.types / Alert.queue). Both look like an empty preview box.
add_test(NAME policy_gallery_demo_wiring
  COMMAND ${CMAKE_COMMAND} -DROOT=${CMAKE_SOURCE_DIR}
    -P ${CMAKE_SOURCE_DIR}/tests/run_gallery_demo_wiring.cmake)
set_tests_properties(policy_gallery_demo_wiring PROPERTIES
  LABELS "full;standard;smoke;gui")

# ---- Policy: widget draw paths must not gain direct Theme font-size reads ----
add_test(NAME gui_theme_font_budget
  COMMAND ${CMAKE_COMMAND} -DROOT=${CMAKE_SOURCE_DIR}
    -P ${CMAKE_SOURCE_DIR}/tests/run_theme_font_budget.cmake)
set_tests_properties(gui_theme_font_budget PROPERTIES
  LABELS "full;standard;smoke;gui;perf")

# ---- Policy: widget draw paths must not gain raw 0xAARRGGBB literals ----
# The semantic-color budget above cannot see a color spelled as a hex literal;
# this one can. Native-interop flag files are skipped outright, and every
# frozen budget line documents why its literal is not a skin color.
add_test(NAME policy_raw_color_budget
  COMMAND ${CMAKE_COMMAND} -DROOT=${CMAKE_SOURCE_DIR}
    -P ${CMAKE_SOURCE_DIR}/tests/run_raw_color_budget.cmake)
set_tests_properties(policy_raw_color_budget PROPERTIES
  LABELS "full;standard;smoke;gui;perf")

# ---- Performance: retained-mode IDE frame budget ----
# Requires the separately-built ZanIDE executable and a desktop session.
if(WIN32 AND EXISTS "${CMAKE_BINARY_DIR}/ZanIDE.exe")
  add_test(NAME perf_frame_budget
    COMMAND ${CMAKE_COMMAND}
      -DROOT=${CMAKE_SOURCE_DIR}
      -DBINARY_DIR=${CMAKE_BINARY_DIR}
      -DZANIDE=${CMAKE_BINARY_DIR}/ZanIDE.exe
      -P ${CMAKE_SOURCE_DIR}/tests/run_frame_budget.cmake)
  set_tests_properties(perf_frame_budget PROPERTIES
    LABELS perf
    TIMEOUT 240
    # Shares the frame_budget_run directory with the GL budget below.
    RUN_SERIAL TRUE
    SKIP_REGULAR_EXPRESSION "FRAME_BUDGET_SKIPPED")

  # Same run under the GL backend (renderBackend=1). It reads
  # tests/perf/frame_budget_gl.txt, which only exists once someone has
  # calibrated it on a machine with a real GPU (-DCALIBRATE=1 prints the
  # numbers); until then the test skips instead of asserting invented limits,
  # and on a machine where the GL context cannot be created the IDE stays on
  # the CPU path, which the surface/backend line in the output makes visible.
  add_test(NAME perf_frame_budget_gl
    COMMAND ${CMAKE_COMMAND}
      -DROOT=${CMAKE_SOURCE_DIR}
      -DBINARY_DIR=${CMAKE_BINARY_DIR}
      -DZANIDE=${CMAKE_BINARY_DIR}/ZanIDE.exe
      -DRENDER_BACKEND=1
      -P ${CMAKE_SOURCE_DIR}/tests/run_frame_budget.cmake)
  set_tests_properties(perf_frame_budget_gl PROPERTIES
    LABELS perf
    TIMEOUT 240
    # Both budgets drive the same run directory, so they cannot overlap.
    RUN_SERIAL TRUE
    SKIP_REGULAR_EXPRESSION "FRAME_BUDGET_SKIPPED|FRAME_BUDGET_UNCALIBRATED")
endif()

# ---- Runtime diagnostics: parser must not hang/OOM on malformed input ----
add_test(NAME diag_no_runaway
  COMMAND zanc ${CMAKE_SOURCE_DIR}/tests/diag/bad_member.zan --dump-ast)
set_tests_properties(diag_no_runaway PROPERTIES
  WILL_FAIL TRUE TIMEOUT 20)

# A property called as a method (`list.Count()`) must be a compile error, not a
# silent constant 0.
add_test(NAME diag_property_call
  COMMAND zanc ${CMAKE_SOURCE_DIR}/tests/diag/property_call.zan
    -o ${CMAKE_BINARY_DIR}/diag_property_call${_exe_ext})
set_tests_properties(diag_property_call PROPERTIES
  WILL_FAIL TRUE TIMEOUT 60)

# A fully-qualified call whose whole name path is unknown (`Foo.Bar.Baz.Quux`)
# must be a compile error, not a silent no-op. The single-segment check that
# catches `X.Method()` used to miss the deeper path.
add_test(NAME diag_fully_qualified_unresolved
  COMMAND zanc ${CMAKE_SOURCE_DIR}/tests/diag/fully_qualified_unresolved.zan
    -o ${CMAKE_BINARY_DIR}/diag_fully_qualified_unresolved${_exe_ext})
set_tests_properties(diag_fully_qualified_unresolved PROPERTIES
  WILL_FAIL TRUE TIMEOUT 60)

# A write to a getter-only property (no setter) -- plain `=`, `+=`, `++`, or an
# object-initializer entry -- must be a compile error, not a silent store into
# the backing slot that a custom getter never reads back.
add_test(NAME diag_readonly_property_write
  COMMAND zanc ${CMAKE_SOURCE_DIR}/tests/diag/readonly_property_write.zan
    -o ${CMAKE_BINARY_DIR}/diag_readonly_property_write${_exe_ext})
set_tests_properties(diag_readonly_property_write PROPERTIES
  WILL_FAIL TRUE TIMEOUT 60)

# An `init` accessor is a setter restricted to object initialization; a plain
# `obj.Prop = v` after construction must be a compile error.
add_test(NAME diag_init_only_write
  COMMAND zanc ${CMAKE_SOURCE_DIR}/tests/diag/init_only_write.zan
    -o ${CMAKE_BINARY_DIR}/diag_init_only_write${_exe_ext})
set_tests_properties(diag_init_only_write PROPERTIES
  WILL_FAIL TRUE TIMEOUT 60)

# A tuple literal is a fresh synthesized struct; assigning through it
# (`(a, b) = rhs`) would write to a discarded temp. It must be a compile error
# pointing at deconstruction, not a silent no-op.
add_test(NAME diag_tuple_assignment
  COMMAND zanc ${CMAKE_SOURCE_DIR}/tests/diag/tuple_assignment.zan
    -o ${CMAKE_BINARY_DIR}/diag_tuple_assignment${_exe_ext})
set_tests_properties(diag_tuple_assignment PROPERTIES
  WILL_FAIL TRUE TIMEOUT 60)

# A shared table column wider than the runtime's ceiling is refused where it is
# declared: at run time Create() would merely answer false and the program would
# carry on without the table.
add_test(NAME diag_shared_table_column_width
  COMMAND zanc ${CMAKE_SOURCE_DIR}/tests/diag/shared_table_column_width.zan
    -o ${CMAKE_BINARY_DIR}/diag_shared_table_column_width${_exe_ext})
set_tests_properties(diag_shared_table_column_width PROPERTIES
  WILL_FAIL TRUE TIMEOUT 60)

# A [NoRuntime] method may not use anything that calls into the managed
# runtime; allocation is the headline case.
add_test(NAME diag_no_runtime_alloc
  COMMAND zanc ${CMAKE_SOURCE_DIR}/tests/diag/no_runtime_alloc.zan
    -o ${CMAKE_BINARY_DIR}/diag_no_runtime_alloc${_exe_ext})
set_tests_properties(diag_no_runtime_alloc PROPERTIES
  WILL_FAIL TRUE TIMEOUT 60)

# ...and irgen must not slip ARC calls into its body either.
add_test(NAME diag_no_runtime_arc COMMAND ${CMAKE_COMMAND}
  -DZANC=$<TARGET_FILE:zanc>
  -DSRC=${CMAKE_SOURCE_DIR}/tests/diag/no_runtime_arc.zan
  -DWORKDIR=${CMAKE_BINARY_DIR}
  -P ${CMAKE_SOURCE_DIR}/tests/run_no_runtime_ir.cmake)
set_tests_properties(diag_no_runtime_arc PROPERTIES TIMEOUT 60)

# A pointer-width native handle must never be implicitly truncated into a
# fixed 32-bit int. Exercise every conversion boundary independently so a new
# lowering path cannot bypass the source-language diagnostic.
foreach(_nint_boundary initializer assignment argument return expr_return async_return lambda_return generic_return)
  if(_nint_boundary MATCHES "return$")
    set(_nint_what return)
  else()
    set(_nint_what ${_nint_boundary})
  endif()
  add_test(NAME diag_nint_to_int_${_nint_boundary}
    COMMAND ${CMAKE_COMMAND}
      -DZANC=$<TARGET_FILE:zanc>
      -DSRC=${CMAKE_SOURCE_DIR}/tests/diag/nint_to_int_${_nint_boundary}.zan
      -DOUT_EXE=${CMAKE_BINARY_DIR}/diag_nint_to_int_${_nint_boundary}${_exe_ext}
      "-DEXPECT_REGEX=narrowing conversion from 'nint' to 'int' in ${_nint_what}"
      -P ${CMAKE_SOURCE_DIR}/tests/run_compile_error.cmake)
  set_tests_properties(diag_nint_to_int_${_nint_boundary} PROPERTIES TIMEOUT 60)
endforeach()

foreach(_field_init_kind static instance generic)
  add_test(NAME diag_field_initializer_narrowing_${_field_init_kind}
    COMMAND ${CMAKE_COMMAND}
      -DZANC=$<TARGET_FILE:zanc>
      -DSRC=${CMAKE_SOURCE_DIR}/tests/diag/field_initializer_narrowing_${_field_init_kind}.zan
      -DOUT_EXE=${CMAKE_BINARY_DIR}/diag_field_initializer_narrowing_${_field_init_kind}${_exe_ext}
      "-DEXPECT_REGEX=narrowing conversion from 'nint' to 'int' in field initializer"
      -P ${CMAKE_SOURCE_DIR}/tests/run_compile_error.cmake)
  set_tests_properties(diag_field_initializer_narrowing_${_field_init_kind}
    PROPERTIES TIMEOUT 60)
endforeach()

# A readonly field is writable only from a constructor of its declaring type.
# It used to parse and then be assignable from anywhere.
foreach(_ro_case method outside)
  add_test(NAME diag_readonly_assign_${_ro_case}
    COMMAND ${CMAKE_COMMAND}
      -DZANC=$<TARGET_FILE:zanc>
      -DSRC=${CMAKE_SOURCE_DIR}/tests/diag/readonly_assign_${_ro_case}.zan
      -DOUT_EXE=${CMAKE_BINARY_DIR}/diag_readonly_assign_${_ro_case}${_exe_ext}
      "-DEXPECT_REGEX=cannot assign to readonly field"
      -P ${CMAKE_SOURCE_DIR}/tests/run_compile_error.cmake)
  set_tests_properties(diag_readonly_assign_${_ro_case} PROPERTIES TIMEOUT 60)
endforeach()

# Typed-ORM queries must reject literal/column type mismatches at compile
# time. dbgen used to lower `p.num == "abc"` / `p.name == 123` to a bound
# parameter of the wrong shape, silently running a type-coerced comparison.
foreach(_mismatch int_string string_int)
  add_test(NAME diag_orm_where_${_mismatch}
    COMMAND ${CMAKE_COMMAND}
      -DZANC=$<TARGET_FILE:zanc>
      -DSRC=${CMAKE_SOURCE_DIR}/tests/diag/orm_where_${_mismatch}.zan
      -DOUT_EXE=${CMAKE_BINARY_DIR}/diag_orm_where_${_mismatch}${_exe_ext}
      "-DEXPECT_REGEX=type mismatch"
      -P ${CMAKE_SOURCE_DIR}/tests/run_compile_error.cmake)
  set_tests_properties(diag_orm_where_${_mismatch} PROPERTIES TIMEOUT 60)
endforeach()

add_test(NAME diag_orm_set_string_int
  COMMAND ${CMAKE_COMMAND}
    -DZANC=$<TARGET_FILE:zanc>
    -DSRC=${CMAKE_SOURCE_DIR}/tests/diag/orm_set_string_int.zan
    -DOUT_EXE=${CMAKE_BINARY_DIR}/diag_orm_set_string_int${_exe_ext}
    "-DEXPECT_REGEX=type mismatch"
    -P ${CMAKE_SOURCE_DIR}/tests/run_compile_error.cmake)
set_tests_properties(diag_orm_set_string_int PROPERTIES TIMEOUT 60)

# An enum member access naming a member that does not exist must be a compile
# error: irgen used to fold it to the constant 0, hiding stale references.
add_test(NAME diag_enum_unknown_member
  COMMAND ${CMAKE_COMMAND}
    -DZANC=$<TARGET_FILE:zanc>
    -DSRC=${CMAKE_SOURCE_DIR}/tests/diag/enum_unknown_member.zan
    -DOUT_EXE=${CMAKE_BINARY_DIR}/diag_enum_unknown_member${_exe_ext}
    "-DEXPECT_REGEX=enum type 'Kind' has no member 'NoSuchMember'"
    -P ${CMAKE_SOURCE_DIR}/tests/run_compile_error.cmake)
set_tests_properties(diag_enum_unknown_member PROPERTIES TIMEOUT 60)

# A conditional's branches must share a common type (identical, implicitly
# convertible, or a common base class). The checker used to type it as the
# then branch alone, so `cond ? new A() : 123` only failed as a sourceless
# LLVM verification error when the ternary PHI mixed a pointer and an integer.
add_test(NAME diag_conditional_branch_mismatch
  COMMAND ${CMAKE_COMMAND}
    -DZANC=$<TARGET_FILE:zanc>
    -DSRC=${CMAKE_SOURCE_DIR}/tests/diag/conditional_branch_mismatch.zan
    -DOUT_EXE=${CMAKE_BINARY_DIR}/diag_conditional_branch_mismatch${_exe_ext}
    "-DEXPECT_REGEX=share no common type"
    -P ${CMAKE_SOURCE_DIR}/tests/run_compile_error.cmake)
set_tests_properties(diag_conditional_branch_mismatch PROPERTIES TIMEOUT 60)

# A return value / assignment RHS must be implicitly convertible to the
# declared target type: the checker used to compare nothing, so
# `string F() { return 123; }` compiled and then dereferenced 123 as a string
# pointer at runtime. The check runs where the checker can resolve the target
# (declared return type, this.field / static field assignments).
add_test(NAME diag_return_type_mismatch
  COMMAND ${CMAKE_COMMAND}
    -DZANC=$<TARGET_FILE:zanc>
    -DSRC=${CMAKE_SOURCE_DIR}/tests/diag/return_type_mismatch.zan
    -DOUT_EXE=${CMAKE_BINARY_DIR}/diag_return_type_mismatch${_exe_ext}
    "-DEXPECT_REGEX=cannot convert 'int' to 'string' in return statement"
    -P ${CMAKE_SOURCE_DIR}/tests/run_compile_error.cmake)
set_tests_properties(diag_return_type_mismatch PROPERTIES TIMEOUT 60)

# An unknown type named in a `where` constraint used to be silently ignored,
# so the generic instantiation proceeded as if the constraint did not exist.
add_test(NAME diag_generic_constraint_unknown
  COMMAND ${CMAKE_COMMAND}
    -DZANC=$<TARGET_FILE:zanc>
    -DSRC=${CMAKE_SOURCE_DIR}/tests/diag/generic_constraint_unknown.zan
    -DOUT_EXE=${CMAKE_BINARY_DIR}/diag_generic_constraint_unknown${_exe_ext}
    "-DEXPECT_REGEX=unknown constraint 'NoSuchConstraint' in 'where T"
    -P ${CMAKE_SOURCE_DIR}/tests/run_compile_error.cmake)
set_tests_properties(diag_generic_constraint_unknown PROPERTIES TIMEOUT 60)

# Generic type arguments are invariant: List<int> is not a List<Box>, even
# though both erase to the same object pointer in IR. Passing a mismatched
# container used to compile and then dereference the elements as the wrong
# type at runtime (the A45 crash), so both the call-argument and the
# assignment/initializer paths must reject it.
foreach(_invariant_case arg assign)
  add_test(NAME diag_generic_invariant_${_invariant_case}
    COMMAND ${CMAKE_COMMAND}
      -DZANC=$<TARGET_FILE:zanc>
      -DSRC=${CMAKE_SOURCE_DIR}/tests/diag/generic_invariant_${_invariant_case}.zan
      -DOUT_EXE=${CMAKE_BINARY_DIR}/diag_generic_invariant_${_invariant_case}${_exe_ext}
      "-DEXPECT_REGEX=generic type arguments are invariant"
      -P ${CMAKE_SOURCE_DIR}/tests/run_compile_error.cmake)
  set_tests_properties(diag_generic_invariant_${_invariant_case} PROPERTIES TIMEOUT 60)
endforeach()

# A member access on a builtin scalar type (string/int/...) must be a compile
# error: irgen used to silently lower it to the constant 0, which crashed at
# runtime when the zero was used as a pointer (`tabs[i].path.path`, the IDE's
# help-page close crash).
add_test(NAME diag_scalar_member_access
  COMMAND ${CMAKE_COMMAND}
    -DZANC=$<TARGET_FILE:zanc>
    -DSRC=${CMAKE_SOURCE_DIR}/tests/diag/scalar_member_access.zan
    -DOUT_EXE=${CMAKE_BINARY_DIR}/diag_scalar_member_access${_exe_ext}
    "-DEXPECT_REGEX=type 'string' has no member 'path'"
    -P ${CMAKE_SOURCE_DIR}/tests/run_compile_error.cmake)
set_tests_properties(diag_scalar_member_access PROPERTIES TIMEOUT 60)

# Reading `obj.name` where the class (or its base chain) declares no such
# member must be a compile error: irgen used to fold the read to the constant
# 0, so a renamed or deleted field kept compiling and produced a wrong number
# or a null-pointer crash at runtime. Only the assignment side was diagnosed.
foreach(_member_read_case class_member_read class_member_read_inherited)
  add_test(NAME diag_${_member_read_case}
    COMMAND ${CMAKE_COMMAND}
      -DZANC=$<TARGET_FILE:zanc>
      -DSRC=${CMAKE_SOURCE_DIR}/tests/diag/${_member_read_case}.zan
      -DOUT_EXE=${CMAKE_BINARY_DIR}/diag_${_member_read_case}${_exe_ext}
      "-DEXPECT_REGEX=has no member"
      -P ${CMAKE_SOURCE_DIR}/tests/run_compile_error.cmake)
  set_tests_properties(diag_${_member_read_case} PROPERTIES TIMEOUT 60)
endforeach()

# Assigning through a name nothing declares (`app.field = v` left behind when a
# static factory became a constructor) dropped the store without a diagnostic,
# while the same name read in a value position was already an error.
add_test(NAME diag_undeclared_member_target
  COMMAND ${CMAKE_COMMAND}
    -DZANC=$<TARGET_FILE:zanc>
    -DSRC=${CMAKE_SOURCE_DIR}/tests/diag/undeclared_member_target.zan
    -DOUT_EXE=${CMAKE_BINARY_DIR}/diag_undeclared_member_target${_exe_ext}
    "-DEXPECT_REGEX=undeclared identifier"
    -P ${CMAKE_SOURCE_DIR}/tests/run_compile_error.cmake)
set_tests_properties(diag_undeclared_member_target PROPERTIES TIMEOUT 60)

# An unresolved static factory at the head of a method chain must be a compile
# error: the unclaimed outer call never emitted its receiver, so the missing
# method went unreported and the chain crashed at runtime.
add_test(NAME diag_unresolved_static_chain
  COMMAND ${CMAKE_COMMAND}
    -DZANC=$<TARGET_FILE:zanc>
    -DSRC=${CMAKE_SOURCE_DIR}/tests/diag/unresolved_static_chain.zan
    -DOUT_EXE=${CMAKE_BINARY_DIR}/diag_unresolved_static_chain${_exe_ext}
    "-DEXPECT_REGEX=unresolved call 'Fluent.Create'"
    -P ${CMAKE_SOURCE_DIR}/tests/run_compile_error.cmake)
set_tests_properties(diag_unresolved_static_chain PROPERTIES TIMEOUT 60)

# Source-level type and interface regressions: reject malformed inheritance,
# mixed comparisons, invalid reference/delegate assignments, local mismatches,
# narrowing conversions, and incomplete interface contracts before IR emission.
set(_new_diag_cases
  cyclic_inheritance
  mixed_comparison
  unrelated_reference_assignment
  delegate_invalid_assignment
  local_assignment_type_mismatch
  float_to_int_narrowing
  integer_narrowing
  interface_missing_method
  interface_bad_signature
  unrelated_argument_type
  scalar_argument_to_reference
  call_not_callable
  expression_nesting_depth
  return_call_type_mismatch)
foreach(_case ${_new_diag_cases})
  if(_case STREQUAL cyclic_inheritance)
    set(_diag_regex "cyclic inheritance")
  elseif(_case STREQUAL mixed_comparison)
    set(_diag_regex "cannot compare")
  elseif(_case STREQUAL unrelated_reference_assignment)
    set(_diag_regex "cannot convert 'B' to 'A'")
  elseif(_case STREQUAL delegate_invalid_assignment)
    set(_diag_regex "cannot convert 'int' to 'Handler'")
  elseif(_case STREQUAL local_assignment_type_mismatch)
    set(_diag_regex "cannot convert 'string' to 'int' in assignment")
  elseif(_case STREQUAL return_call_type_mismatch)
    set(_diag_regex "cannot convert 'string' to 'int' in return statement")
  elseif(_case STREQUAL unrelated_argument_type)
    set(_diag_regex "in argument 1 of 'Paint'")
  elseif(_case STREQUAL scalar_argument_to_reference)
    set(_diag_regex "cannot convert 'int' to 'object'")
  elseif(_case STREQUAL call_not_callable)
    set(_diag_regex "could not be resolved to any callable")
  elseif(_case STREQUAL expression_nesting_depth)
    set(_diag_regex "expression nesting too deep")
  elseif(_case STREQUAL interface_missing_method OR
         _case STREQUAL interface_bad_signature)
    set(_diag_regex "does not implement interface method")
  else()
    set(_diag_regex "narrowing conversion")
  endif()
  add_test(NAME diag_${_case}
    COMMAND ${CMAKE_COMMAND}
      -DZANC=$<TARGET_FILE:zanc>
      -DSRC=${CMAKE_SOURCE_DIR}/tests/diag/${_case}.zan
      -DOUT_EXE=${CMAKE_BINARY_DIR}/diag_${_case}${_exe_ext}
      "-DEXPECT_REGEX=${_diag_regex}"
      -P ${CMAKE_SOURCE_DIR}/tests/run_compile_error.cmake)
  set_tests_properties(diag_${_case} PROPERTIES TIMEOUT 60)
endforeach()

foreach(_runtime_case IN ITEMS
    array_oob
    list_removeat_oob
    list_insert_oob
    member_list_field_oob
    substring_start_oob
    substring_window_oob
    span_window_oob
    span_negative_length
    member_array_field_oob)
  if(_runtime_case STREQUAL list_removeat_oob OR
     _runtime_case STREQUAL list_insert_oob OR
     _runtime_case STREQUAL member_list_field_oob)
    set(_runtime_regex "list index out of bounds")
  elseif(_runtime_case STREQUAL span_window_oob)
    set(_runtime_regex "span range out of bounds")
  elseif(_runtime_case STREQUAL span_negative_length)
    set(_runtime_regex "negative span length")
  elseif(_runtime_case STREQUAL substring_start_oob)
    set(_runtime_regex "substring index out of bounds")
  elseif(_runtime_case STREQUAL substring_window_oob)
    set(_runtime_regex "substring range out of bounds")
  else()
    set(_runtime_regex "array index out of bounds")
  endif()
  add_test(NAME runtime_${_runtime_case}
    COMMAND ${CMAKE_COMMAND}
      -DZANC=$<TARGET_FILE:zanc>
      -DSRC=${CMAKE_SOURCE_DIR}/tests/runtime/${_runtime_case}.zan
      -DOUT_EXE=${CMAKE_BINARY_DIR}/runtime_${_runtime_case}${_exe_ext}
      "-DEXPECT_REGEX=${_runtime_regex}"
      -P ${CMAKE_SOURCE_DIR}/tests/run_runtime_error.cmake)
  set_tests_properties(runtime_${_runtime_case} PROPERTIES TIMEOUT 60)
endforeach()

# ---- Golden compiler regression tests ----
# Freeze the lexer token stream and parse tree for a set of hermetic (stdlib-free)
# .zan inputs so any lexer/parser/AST regression is caught in CI on every
# platform. IR is verified structurally (required @symbol defines present)
# rather than by exact text, to stay robust across LLVM versions/targets.
# Regenerate goldens with: cmake -DZANC=<zanc> -P tests/golden/update_golden.cmake
set(_golden_dir ${CMAKE_SOURCE_DIR}/tests/golden)
file(GLOB _golden_cases RELATIVE ${_golden_dir}/cases ${_golden_dir}/cases/*.zan)
foreach(_case ${_golden_cases})
  string(REGEX REPLACE "\\.zan$" "" _gname ${_case})
  add_test(NAME golden_${_gname}_tokens COMMAND ${CMAKE_COMMAND}
    -DZANC=$<TARGET_FILE:zanc> -DMODE=tokens
    -DSRC=${_golden_dir}/cases/${_case}
    -DEXPECTED=${_golden_dir}/expected/${_gname}.tokens
    -DWORKDIR=${CMAKE_BINARY_DIR}
    -P ${_golden_dir}/run_golden.cmake)
  add_test(NAME golden_${_gname}_ast COMMAND ${CMAKE_COMMAND}
    -DZANC=$<TARGET_FILE:zanc> -DMODE=ast
    -DSRC=${_golden_dir}/cases/${_case}
    -DEXPECTED=${_golden_dir}/expected/${_gname}.ast
    -DWORKDIR=${CMAKE_BINARY_DIR}
    -P ${_golden_dir}/run_golden.cmake)
  if(EXISTS ${_golden_dir}/expected/${_gname}.ir-contains)
    add_test(NAME golden_${_gname}_ir COMMAND ${CMAKE_COMMAND}
      -DZANC=$<TARGET_FILE:zanc> -DMODE=ir
      -DSRC=${_golden_dir}/cases/${_case}
      -DEXPECTED=${_golden_dir}/expected/${_gname}.ir-contains
      -DWORKDIR=${CMAKE_BINARY_DIR}
      -P ${_golden_dir}/run_golden.cmake)
  endif()
endforeach()

# ---- Struct-by-value FFI ABI: the C signature per target ----
# Cross targets only need the LLVM backends zanc already registers; nothing is
# linked, so these run everywhere the compiler builds.
foreach(_abi_target win-x64 linux-x64 linux-arm64 macos-arm64)
  add_test(NAME abi_signatures_${_abi_target} COMMAND ${CMAKE_COMMAND}
    -DZANC=$<TARGET_FILE:zanc>
    -DSRC=${CMAKE_SOURCE_DIR}/tests/abi/struct_abi.zan
    -DTARGET=${_abi_target}
    -DEXPECTED=${CMAKE_SOURCE_DIR}/tests/abi/expected/${_abi_target}.decls
    -DWORKDIR=${CMAKE_BINARY_DIR}
    -P ${CMAKE_SOURCE_DIR}/tests/run_abi_case.cmake)
  set_tests_properties(abi_signatures_${_abi_target} PROPERTIES TIMEOUT 120)
endforeach()

# ---- Sub-`int` FFI promotion: signext / zeroext per target ----
# C promotes anything narrower than `int` at the boundary and leaves the work
# to the caller, so the shapes are pinned per target like the struct cases.
foreach(_abi_target win-x64 linux-x64 linux-arm64 macos-arm64)
  add_test(NAME abi_int_ext_${_abi_target} COMMAND ${CMAKE_COMMAND}
    -DZANC=$<TARGET_FILE:zanc>
    -DSRC=${CMAKE_SOURCE_DIR}/tests/abi/int_ext.zan
    -DTARGET=${_abi_target}
    -DEXPECTED=${CMAKE_SOURCE_DIR}/tests/abi/expected/int-ext.decls
    -DWORKDIR=${CMAKE_BINARY_DIR}
    -P ${CMAKE_SOURCE_DIR}/tests/run_abi_case.cmake)
  set_tests_properties(abi_int_ext_${_abi_target} PROPERTIES TIMEOUT 120)
endforeach()

# ---- --publish string de-obfuscation: startup constructor registration ----
# The constructor that un-scrambles string literals must be registered the way
# the target's startup path actually walks, or a published binary prints pure
# garbage (ELF needs .init_array; llvm.global_ctors lowers to a dead legacy
# .ctors there). Nothing is linked, so these run wherever the compiler builds.
foreach(_obf_case linux-x64:init_array linux-arm64:init_array
                  win-x64:global_ctors macos-arm64:global_ctors)
  string(REPLACE ":" ";" _obf_parts ${_obf_case})
  list(GET _obf_parts 0 _obf_target)
  list(GET _obf_parts 1 _obf_kind)
  add_test(NAME publish_obf_ctor_${_obf_target} COMMAND ${CMAKE_COMMAND}
    -DZANC=$<TARGET_FILE:zanc>
    -DSRC=${CMAKE_SOURCE_DIR}/tests/publish/obf_ctor.zan
    -DTARGET=${_obf_target}
    -DKIND=${_obf_kind}
    -DWORKDIR=${CMAKE_BINARY_DIR}
    -P ${CMAKE_SOURCE_DIR}/tests/run_publish_obf_ctor.cmake)
  set_tests_properties(publish_obf_ctor_${_obf_target} PROPERTIES TIMEOUT 180)
endforeach()

# ---- DAP integration test (real gdb-backed debug session) ----
add_executable(dap_integration_test
    tests/dap/dap_integration_test.c
)
set_target_properties(dap_integration_test PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
)
if(WIN32)
    target_compile_definitions(dap_integration_test PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
# The debug target is compiled with `zanc -g` at build time (same custom
# command pattern as zanfmt) so the harness runs directly as the ctest
# command and can propagate its skip code.
set(_dap_dbg ${CMAKE_BINARY_DIR}/dbgtarget${_exe_ext})
add_custom_command(OUTPUT ${_dap_dbg}
    COMMAND $<TARGET_FILE:zanc> ${CMAKE_SOURCE_DIR}/tests/dap/dbgtarget.zan
            -g --auto-stdlib -o ${_dap_dbg}
    DEPENDS zanc ${CMAKE_SOURCE_DIR}/tests/dap/dbgtarget.zan)
# The target cannot be called `dbgtarget`: on platforms with no executable
# suffix that collides with the custom command's output file (see the zanfmt
# comment above).
add_custom_target(dap_debug_target ALL DEPENDS ${_dap_dbg})
# The harness FAILS unless a real gdb debug session happens; environments
# without gdb must opt out via ZAN_ALLOW_SKIP_DAP=1, which exits 77. CTest
# then reports the run as "skipped" (not "passed") via SKIP_RETURN_CODE.
add_test(NAME dap_integration
  COMMAND dap_integration_test $<TARGET_FILE:zan-dap> ${_dap_dbg}
                              ${CMAKE_SOURCE_DIR}/tests/dap/dbgtarget.zan)
set_tests_properties(dap_integration PROPERTIES SKIP_RETURN_CODE 77)

# ---- LSP integration test (real zan-lsp over stdio; A5 UTF-16 positions) ----
add_executable(lsp_integration_test
    tests/lsp/lsp_integration_test.c
    src/common/json.c
)
target_include_directories(lsp_integration_test PRIVATE ${CMAKE_SOURCE_DIR})
set_target_properties(lsp_integration_test PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
)
if(WIN32)
    target_compile_definitions(lsp_integration_test PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_link_libraries(lsp_integration_test PRIVATE m)
endif()
add_test(NAME lsp_integration_utf16
  COMMAND lsp_integration_test $<TARGET_FILE:zan-lsp>)

# ---- zanfmt (code formatter, written in Zan) ----
set(_zanfmt_exe ${CMAKE_BINARY_DIR}/zanfmt${_exe_ext})
add_custom_command(OUTPUT ${_zanfmt_exe}
    COMMAND $<TARGET_FILE:zanc> ${CMAKE_SOURCE_DIR}/src/fmt/zanfmt.zan
            --auto-stdlib -o ${_zanfmt_exe}
    DEPENDS zanc ${CMAKE_SOURCE_DIR}/src/fmt/zanfmt.zan
    COMMENT "Building zanfmt from Zan source")
# The target cannot be called `zanfmt`: on platforms with no executable suffix
# that is also the name of the file the custom command writes, and Ninja then
# has two rules generating `zanfmt` ("multiple rules generate zanfmt", which
# stops generation before any target builds).
add_custom_target(zanfmt_tool ALL DEPENDS ${_zanfmt_exe})

add_test(NAME tool_zanfmt
  COMMAND ${CMAKE_COMMAND}
    -DZANFMT=${_zanfmt_exe}
    -DSRC=${CMAKE_SOURCE_DIR}/tests/fmt/messy.zan
    -DEXPECTED=${CMAKE_SOURCE_DIR}/tests/fmt/messy.expected
    -DWORKFILE=${CMAKE_BINARY_DIR}/zanfmt_work.zan
    -P ${CMAKE_SOURCE_DIR}/tests/run_zanfmt.cmake)

# ---- zandoc (documentation generator, written in Zan) ----
set(_zandoc_exe ${CMAKE_BINARY_DIR}/zandoc${_exe_ext})
add_custom_command(OUTPUT ${_zandoc_exe}
    COMMAND $<TARGET_FILE:zanc> ${CMAKE_SOURCE_DIR}/src/doc/zandoc.zan
            --auto-stdlib -o ${_zandoc_exe}
    DEPENDS zanc ${CMAKE_SOURCE_DIR}/src/doc/zandoc.zan
    COMMENT "Building zandoc from Zan source")
add_custom_target(zandoc_tool ALL DEPENDS ${_zandoc_exe})

# ---- GenIndex (project semantic index, written in Zan) ----
# The .zanmap index is what AI clients query instead of reading source, so its
# shape is a contract: this case pins route/entity/manifest fields against a
# trimmed, committed `--gen-meta` document.
add_test(NAME tool_genindex
  COMMAND ${CMAKE_COMMAND}
    -DZANC=$<TARGET_FILE:zanc>
    -DSRC=${CMAKE_SOURCE_DIR}/tests/index/genindex_case.zan
    -DEXPECTED=${CMAKE_SOURCE_DIR}/tests/index/genindex.out
    -DOUT_EXE=${CMAKE_BINARY_DIR}/tool_genindex${_exe_ext}
    -DSTDLIB_STAMP=${_stdlib_stamp}
    "-DRUN_ARGS=${CMAKE_SOURCE_DIR}/tests/index/keys.meta.json;${CMAKE_BINARY_DIR}/genindex_out"
    -P ${CMAKE_SOURCE_DIR}/tests/run_case.cmake)
set_tests_properties(tool_genindex PROPERTIES TIMEOUT 120)

add_test(NAME tool_zandoc
  COMMAND ${CMAKE_COMMAND}
    -DZANDOC=${_zandoc_exe}
    -DSRC=${CMAKE_SOURCE_DIR}/tests/doc/sample.zan
    -DEXPECTED_MD=${CMAKE_SOURCE_DIR}/tests/doc/sample.md
    -DEXPECTED_HTML=${CMAKE_SOURCE_DIR}/tests/doc/sample.html
    -DOUTFILE=${CMAKE_BINARY_DIR}/zandoc_out.md
    -P ${CMAKE_SOURCE_DIR}/tests/run_zandoc.cmake)

# ---- Test tiers -------------------------------------------------------------
# The suite is over a thousand tests because every conformance program is also
# built for determinism and leak checking; running all of it for a one-line
# change is minutes of waiting for nothing. Each test therefore carries a tier
# label so a run can be scoped to the work at hand:
#
#   ctest -L smoke      compiler gates only: goldens, diagnostics, ABI,
#                       runtimes, tools, GUI cases -- seconds, run it always
#   ctest -L standard   smoke + every conformance program + library emission
#                       -- the pre-commit gate
#   ctest -L full       everything, including the determinism and leakcheck
#                       twins and self-hosting -- release / CI
#
# Labels are derived from the test names so new tests are classified the moment
# they are added, without touching the add_test() calls above.
get_property(_all_tests DIRECTORY PROPERTY TESTS)
foreach(_t ${_all_tests})
  set(_labels "full")
  if(_t MATCHES "^(golden_|diag_|abi_|runtime_|tool_|policy_)" OR _t MATCHES "^conformance_gui_")
    list(APPEND _labels "standard" "smoke")
  elseif(_t MATCHES "^(conformance_|emit_lib_|dap_)")
    list(APPEND _labels "standard")
  endif()
  set_property(TEST ${_t} APPEND PROPERTY LABELS ${_labels})
  # Cases that drive the real desktop (tray icon, screen capture, UI
  # automation, mouse/keyboard, clipboard) contend for one machine-wide
  # resource, so they pass alone and fail at random under -j. One shared lock
  # serializes them against each other without serializing the suite.
  if(_t MATCHES "(win_tray|win_screen|win_automation|win_device|input_mouse|input_keyboard|input_background|clipboard)")
    set_property(TEST ${_t} APPEND PROPERTY RESOURCE_LOCK desktop)
  endif()
endforeach()
