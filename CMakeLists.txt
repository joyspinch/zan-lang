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
    src/compiler/jsongen.c
    src/compiler/formgen.c
    src/compiler/dbgen.c
    src/compiler/routegen.c
    src/compiler/nsresolve.c
    src/compiler/binder.c
    src/compiler/checker.c
    src/compiler/irgen.c
    src/compiler/optimizer.c
    src/compiler/incremental.c
    src/compiler/package.c
    src/compiler/stdlib_ext.c
    src/compiler/crosscomp.c
    src/compiler/winres.c
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
foreach(_linux_sub linux-musl linux-arm64 linux-riscv64)
    set(_linux_sys "${CMAKE_SOURCE_DIR}/toolchain/${_linux_sub}")
    if(EXISTS "${_linux_sys}/libc.a")
        add_custom_command(TARGET zanc POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:zanc>/${_linux_sub}
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${_linux_sys}/crt1.o" "${_linux_sys}/crti.o"
                    "${_linux_sys}/crtn.o" "${_linux_sys}/libc.a"
                    "${_linux_sys}/zanrt_io.o" "${_linux_sys}/zanrt_sync.o"
                    "${_linux_sys}/zanrt_timer.o"
                    $<TARGET_FILE_DIR:zanc>/${_linux_sub}/
            COMMENT "Copying ${_linux_sub} sysroot for cross-compilation"
            VERBATIM)
        if(EXISTS "${_linux_sys}/zanrt_mem.o")
            add_custom_command(TARGET zanc POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${_linux_sys}/zanrt_mem.o"
                        $<TARGET_FILE_DIR:zanc>/${_linux_sub}/
                VERBATIM)
        endif()
        if(EXISTS "${_linux_sys}/libgcc.a")
            add_custom_command(TARGET zanc POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${_linux_sys}/libgcc.a"
                        $<TARGET_FILE_DIR:zanc>/${_linux_sub}/
                VERBATIM)
        endif()
    endif()
endforeach()

# ---- Windows cross-compilation runtimes ----
# Copy the bundled mingw-w64 runtimes (committed at toolchain/win-x64/ and
# toolchain/win-arm64/) next to zanc so that --target win-x64 / win-arm64 can
# link PE executables from any host with ld.lld.
foreach(_win_sub win-x64 win-arm64)
    set(_win_sys "${CMAKE_SOURCE_DIR}/toolchain/${_win_sub}")
    if(EXISTS "${_win_sys}/mingw/lib/crt2.o")
        add_custom_command(TARGET zanc POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                    "${_win_sys}" $<TARGET_FILE_DIR:zanc>/${_win_sub}
            COMMENT "Copying ${_win_sub} mingw runtime for cross-compilation"
            VERBATIM)
    endif()
endforeach()

# ---- macOS libSystem stub (macos-x64/macos-arm64 cross-compilation) ----
if(EXISTS "${CMAKE_SOURCE_DIR}/toolchain/macos/libSystem.tbd")
    add_custom_command(TARGET zanc POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_SOURCE_DIR}/toolchain/macos"
                $<TARGET_FILE_DIR:zanc>/macos
        COMMENT "Copying macOS libSystem stub for cross-compilation"
        VERBATIM)
endif()

# ---- WASI sysroot (wasm32 cross-compilation) ----
if(EXISTS "${CMAKE_SOURCE_DIR}/toolchain/wasm32/libc.a")
    add_custom_command(TARGET zanc POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_SOURCE_DIR}/toolchain/wasm32"
                $<TARGET_FILE_DIR:zanc>/wasm32
        COMMENT "Copying wasm32 wasi sysroot for cross-compilation"
        VERBATIM)
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

# ---- IDE ----
# The IDE is built from src/ide_zan/ZanIDE.zan using the Zan compiler (zanc).
# It is compiled separately: zanc src/ide_zan/ZanIDE.zan -o build/ZanIDE.exe
# The old C-based zan-ide target has been removed in favour of ZanIDE.

# ---- Tests ----
enable_testing()

add_executable(zanrt_sync_test
    tests/runtime/rt_sync_test.c
    src/runtime/rt_sync.c
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

# ---- Conformance suite: compile each program, run it, diff stdout vs golden ----
if(WIN32)
  set(_exe_ext ".exe")
else()
  set(_exe_ext "")
endif()

file(GLOB _conf_srcs ${CMAKE_SOURCE_DIR}/tests/conformance/*.zan)

# Every test program is a pure function of (its source, zanc, the stdlib), so
# the suite reuses the binary it built last time unless one of those changed.
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

foreach(_src ${_conf_srcs})
  get_filename_component(_name ${_src} NAME_WE)
  add_test(NAME conformance_${_name}
    COMMAND ${CMAKE_COMMAND}
      -DZANC=$<TARGET_FILE:zanc>
      -DSRC=${_src}
      -DEXPECTED=${CMAKE_SOURCE_DIR}/tests/conformance/${_name}.out
      -DOUT_EXE=${CMAKE_BINARY_DIR}/conf_${_name}${_exe_ext}
      -DSTDLIB_STAMP=${_stdlib_stamp}
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
  add_test(NAME determinism_${_name}
    COMMAND ${CMAKE_COMMAND}
      -DZANC=$<TARGET_FILE:zanc>
      -DSRC=${_src}
      -DSTDLIB_STAMP=${_stdlib_stamp}
      -DSTAMP=${CMAKE_BINARY_DIR}/det_${_name}.stamp
      -P ${CMAKE_SOURCE_DIR}/tests/run_determinism.cmake)
  set_tests_properties(determinism_${_name} PROPERTIES TIMEOUT 120)
endforeach()

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
  add_test(NAME leakcheck_${_name}
    COMMAND ${CMAKE_COMMAND}
      -DZANC=$<TARGET_FILE:zanc>
      -DSRC=${_src}
      -DOUT_EXE=${CMAKE_BINARY_DIR}/leak_${_name}${_exe_ext}
      -DSTDLIB_STAMP=${_stdlib_stamp}
      -P ${CMAKE_SOURCE_DIR}/tests/run_leakcheck.cmake)
  set_tests_properties(leakcheck_${_name} PROPERTIES
    RESOURCE_LOCK src_${_name} TIMEOUT 120)
  if(_name IN_LIST _known_leaky)
    set_tests_properties(leakcheck_${_name} PROPERTIES WILL_FAIL TRUE)
  endif()
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
    -DBADPROG=${CMAKE_SOURCE_DIR}/tests/diag/bad_member.zan
    -DBADLL=${CMAKE_BINARY_DIR}/selfhost_bad.ll
    -P ${CMAKE_SOURCE_DIR}/tests/run_selfhost.cmake)
set_tests_properties(selfhost_gen1 PROPERTIES TIMEOUT 600)

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
      "-DZANC_ARGS=${CMAKE_SOURCE_DIR}/stdlib/Gui/Stack.zan;--driver-dir;$<TARGET_FILE_DIR:zan_gui>"
      -DEXPECTED=${CMAKE_SOURCE_DIR}/tests/conformance/gui_stack.out
      -DOUT_EXE=${CMAKE_BINARY_DIR}/conf_gui_stack${_exe_ext}
      -P ${CMAKE_SOURCE_DIR}/tests/run_case.cmake)

  # Closing a tab removes it from the list the strip is iterating; the widget
  # must not be left pointing at an entry that is gone.
  add_test(NAME conformance_gui_tabs
    COMMAND ${CMAKE_COMMAND}
      -DZANC=$<TARGET_FILE:zanc>
      -DSRC=${CMAKE_SOURCE_DIR}/tests/gui/tabs_test.zan
      "-DZANC_ARGS=${CMAKE_SOURCE_DIR}/stdlib/Gui/Widget/Tabs.zan;--auto-stdlib;--driver-dir;$<TARGET_FILE_DIR:zan_gui>"
      -DEXPECTED=${CMAKE_SOURCE_DIR}/tests/conformance/gui_tabs.out
      -DOUT_EXE=${CMAKE_BINARY_DIR}/conf_gui_tabs${_exe_ext}
      -P ${CMAKE_SOURCE_DIR}/tests/run_case.cmake)

  add_test(NAME conformance_gui_icon
    COMMAND ${CMAKE_COMMAND}
      -DZANC=$<TARGET_FILE:zanc>
      -DSRC=${CMAKE_SOURCE_DIR}/tests/gui/icon_test.zan
      "-DZANC_ARGS=${CMAKE_SOURCE_DIR}/stdlib/Gui/Icon.zan;--auto-stdlib;--driver-dir;$<TARGET_FILE_DIR:zan_gui>"
      -DEXPECTED=${CMAKE_SOURCE_DIR}/tests/conformance/gui_icon.out
      -DOUT_EXE=${CMAKE_BINARY_DIR}/conf_gui_icon${_exe_ext}
      -P ${CMAKE_SOURCE_DIR}/tests/run_case.cmake)

  add_test(NAME conformance_gui_text
    COMMAND ${CMAKE_COMMAND}
      -DZANC=$<TARGET_FILE:zanc>
      -DSRC=${CMAKE_SOURCE_DIR}/tests/gui/text_test.zan
      "-DZANC_ARGS=${CMAKE_SOURCE_DIR}/stdlib/Gui/Text.zan;--driver-dir;$<TARGET_FILE_DIR:zan_gui>"
      -DEXPECTED=${CMAKE_SOURCE_DIR}/tests/conformance/gui_text.out
      -DOUT_EXE=${CMAKE_BINARY_DIR}/conf_gui_text${_exe_ext}
      -P ${CMAKE_SOURCE_DIR}/tests/run_case.cmake)

  add_test(NAME conformance_gui_codeeditor
    COMMAND ${CMAKE_COMMAND}
      -DZANC=$<TARGET_FILE:zanc>
      -DSRC=${CMAKE_SOURCE_DIR}/tests/gui/codeeditor_test.zan
      "-DZANC_ARGS=${CMAKE_SOURCE_DIR}/stdlib/Gui/Component/CodeEditor/CodeEditor.zan;--auto-stdlib;--driver-dir;$<TARGET_FILE_DIR:zan_gui>"
      -DEXPECTED=${CMAKE_SOURCE_DIR}/tests/conformance/gui_codeeditor.out
      -DOUT_EXE=${CMAKE_BINARY_DIR}/conf_gui_codeeditor${_exe_ext}
      -P ${CMAKE_SOURCE_DIR}/tests/run_case.cmake)

  add_test(NAME conformance_gui_datatable
    COMMAND ${CMAKE_COMMAND}
      -DZANC=$<TARGET_FILE:zanc>
      -DSRC=${CMAKE_SOURCE_DIR}/tests/gui/datatable_test.zan
      "-DZANC_ARGS=${CMAKE_SOURCE_DIR}/stdlib/Gui/Component/DataTable/DataTable.zan;--auto-stdlib;--driver-dir;$<TARGET_FILE_DIR:zan_gui>"
      -DEXPECTED=${CMAKE_SOURCE_DIR}/tests/conformance/gui_datatable.out
      -DOUT_EXE=${CMAKE_BINARY_DIR}/conf_gui_datatable${_exe_ext}
      -P ${CMAKE_SOURCE_DIR}/tests/run_case.cmake)

  # The property protocol (Props() specs -> Control.GetProp/SetProp) is what
  # the designer and the serializer talk to, and it is pure computation.
  add_test(NAME conformance_gui_props
    COMMAND ${CMAKE_COMMAND}
      -DZANC=$<TARGET_FILE:zanc>
      -DSRC=${CMAKE_SOURCE_DIR}/tests/gui/prop_test.zan
      "-DZANC_ARGS=--auto-stdlib;--driver-dir;$<TARGET_FILE_DIR:zan_gui>"
      -DEXPECTED=${CMAKE_SOURCE_DIR}/tests/conformance/gui_props.out
      -DOUT_EXE=${CMAKE_BINARY_DIR}/conf_gui_props${_exe_ext}
      -P ${CMAKE_SOURCE_DIR}/tests/run_case.cmake)

  # The CSS skin pipeline (Css parser -> StyleSheet -> StyleBox) is pure
  # computation, so it is verified headlessly against a golden dump.
  add_test(NAME conformance_gui_css
    COMMAND ${CMAKE_COMMAND}
      -DZANC=$<TARGET_FILE:zanc>
      -DSRC=${CMAKE_SOURCE_DIR}/tests/gui/css_test.zan
      "-DZANC_ARGS=${CMAKE_SOURCE_DIR}/stdlib/Gui/Css.zan;${CMAKE_SOURCE_DIR}/stdlib/Gui/StyleSheet.zan;${CMAKE_SOURCE_DIR}/stdlib/Gui/StyleBox.zan;--auto-stdlib;--driver-dir;$<TARGET_FILE_DIR:zan_gui>"
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
      "-DZANC_ARGS=${CMAKE_SOURCE_DIR}/stdlib/Gui/Component/WebView/WebView2.zan;--auto-stdlib;--driver-dir;$<TARGET_FILE_DIR:zan_gui>"
      -DEXPECTED=${CMAKE_SOURCE_DIR}/tests/conformance/gui_webview_interop.out
      -DOUT_EXE=${CMAKE_BINARY_DIR}/conf_gui_webview_interop${_exe_ext}
      -P ${CMAKE_SOURCE_DIR}/tests/run_case.cmake)

  set_tests_properties(runtime_gui conformance_gui_stack conformance_gui_text conformance_gui_codeeditor conformance_gui_datatable conformance_gui_css conformance_gui_props conformance_gui_webview_interop PROPERTIES
    ENVIRONMENT "ZAN_LIB_PATH=$<TARGET_FILE_DIR:zan_gui>;LD_LIBRARY_PATH=$<TARGET_FILE_DIR:zan_gui>;DYLD_LIBRARY_PATH=$<TARGET_FILE_DIR:zan_gui>")
endif()

# ---- IDE: assistant task state machine ----
# The twelve-phase delivery run (understand -> ... -> knowledge), its bounded
# retries, the diagnose-and-reverify loop and the checkpoint round-trip are pure
# computation, so the whole lifecycle is verified headlessly -- including that a
# restored checkpoint resumes at the phase that failed instead of restarting.
add_test(NAME conformance_ide_airun
  COMMAND ${CMAKE_COMMAND}
    -DZANC=$<TARGET_FILE:zanc>
    -DSRC=${CMAKE_SOURCE_DIR}/tests/ide/airun_test.zan
    "-DZANC_ARGS=${CMAKE_SOURCE_DIR}/src/ide_zan/AiRun.zan;--auto-stdlib"
    -DEXPECTED=${CMAKE_SOURCE_DIR}/tests/ide/airun_test.out
    -DOUT_EXE=${CMAKE_BINARY_DIR}/conf_ide_airun${_exe_ext}
    -P ${CMAKE_SOURCE_DIR}/tests/run_case.cmake)

# ---- IDE: Help & Feedback local diagnostics store ----
# Parsing compiler diagnostic lines, de-dup counting, redaction (absolute paths,
# secrets, connection strings, e-mail), the JSONL round-trip and "no events in
# the payload unless the user consents to logs" are all pure computation, so the
# aggregation layer is verified headlessly with no window.
add_test(NAME conformance_ide_diagstore
  COMMAND ${CMAKE_COMMAND}
    -DZANC=$<TARGET_FILE:zanc>
    -DSRC=${CMAKE_SOURCE_DIR}/tests/ide/diagstore_test.zan
    "-DZANC_ARGS=${CMAKE_SOURCE_DIR}/src/ide_zan/DiagStore.zan;--auto-stdlib"
    -DEXPECTED=${CMAKE_SOURCE_DIR}/tests/ide/diagstore_test.out
    -DOUT_EXE=${CMAKE_BINARY_DIR}/conf_ide_diagstore${_exe_ext}
    -P ${CMAKE_SOURCE_DIR}/tests/run_case.cmake)

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
add_test(NAME dap_integration
  COMMAND ${CMAKE_COMMAND}
    -DZANC=$<TARGET_FILE:zanc>
    -DDAP=$<TARGET_FILE:zan-dap>
    -DHARNESS=$<TARGET_FILE:dap_integration_test>
    -DSRC=${CMAKE_SOURCE_DIR}/tests/dap/dbgtarget.zan
    -DOUT_EXE=${CMAKE_BINARY_DIR}/dbgtarget${_exe_ext}
    -DZANC_ARGS=--auto-stdlib
    -P ${CMAKE_SOURCE_DIR}/tests/run_dap.cmake)

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
  if(_t MATCHES "^(golden_|diag_|abi_|runtime_|tool_)" OR _t MATCHES "^conformance_gui_")
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
