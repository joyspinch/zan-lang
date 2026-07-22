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
message(STATUS "Zan version: ${ZAN_VERSION}")

# ---- Find LLVM ----
find_package(LLVM REQUIRED CONFIG)
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

# Cross-compile backends beyond x86/aarch64 are optional: not every LLVM
# distribution builds them (e.g. mozbuild clang omits RISCV). Link ¡ª and
# initialize, via HAVE_LLVM_* defines ¡ª only the ones this LLVM provides.
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
    src/compiler/dbgen.c
    src/compiler/binder.c
    src/compiler/checker.c
    src/compiler/irgen.c
    src/compiler/optimizer.c
    src/compiler/incremental.c
    src/compiler/package.c
    src/compiler/stdlib_ext.c
    src/compiler/crosscomp.c
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
# pre-built zanrt_io.o for socket-async programs, and zanrt_sync.o for
# atomics/shared-memory programs.
foreach(_linux_sub linux-musl linux-arm64 riscv64)
    set(_linux_sys "${CMAKE_SOURCE_DIR}/toolchain/${_linux_sub}")
    if(EXISTS "${_linux_sys}/libc.a")
        add_custom_command(TARGET zanc POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:zanc>/${_linux_sub}
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${_linux_sys}/crt1.o" "${_linux_sys}/crti.o"
                    "${_linux_sys}/crtn.o" "${_linux_sys}/libc.a"
                    "${_linux_sys}/zanrt_io.o" "${_linux_sys}/zanrt_sync.o"
                    $<TARGET_FILE_DIR:zanc>/${_linux_sub}/
            COMMENT "Copying ${_linux_sub} sysroot for cross-compilation"
            VERBATIM)
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
        target_link_libraries(zan_gui PRIVATE user32 gdi32 dwmapi shcore imm32)
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
foreach(_src ${_conf_srcs})
  get_filename_component(_name ${_src} NAME_WE)
  add_test(NAME conformance_${_name}
    COMMAND ${CMAKE_COMMAND}
      -DZANC=$<TARGET_FILE:zanc>
      -DSRC=${_src}
      -DEXPECTED=${CMAKE_SOURCE_DIR}/tests/conformance/${_name}.out
      -DOUT_EXE=${CMAKE_BINARY_DIR}/conf_${_name}${_exe_ext}
      -P ${CMAKE_SOURCE_DIR}/tests/run_case.cmake)
  # A program and its leakcheck twin are built from the same source, so any
  # hardcoded listen port (e.g. the async networking conformance tests) is
  # shared between them. Serialize the runnable variants of one source via a
  # per-source resource lock so parallel ctest can't collide on that port.
  set_tests_properties(conformance_${_name} PROPERTIES RESOURCE_LOCK src_${_name})
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
      -P ${CMAKE_SOURCE_DIR}/tests/run_determinism.cmake)
endforeach()

# ---- Leak detection: run each program under --check-leaks, fail on leaks ----
# The --check-leaks build tracks rc-managed allocations and reports any object
# still reachable at exit. Programs known to leak (rc objects owned by arrays,
# List/Dict or object fields are not yet released when their owner dies -- a
# documented ARC follow-up) are marked WILL_FAIL so this gate stays green while
# still catching *new* leaks; fixing such a leak flips its test to failing, a
# reminder to drop it from the allow-list below.
set(_known_leaky
  lang_fixes        # rc objects owned by Dict/List slots (documented ARC follow-up)
  exceptions_basic  # thrown exception objects are not yet released after catch
)
foreach(_src ${_conf_srcs})
  get_filename_component(_name ${_src} NAME_WE)
  add_test(NAME leakcheck_${_name}
    COMMAND ${CMAKE_COMMAND}
      -DZANC=$<TARGET_FILE:zanc>
      -DSRC=${_src}
      -DOUT_EXE=${CMAKE_BINARY_DIR}/leak_${_name}${_exe_ext}
      -P ${CMAKE_SOURCE_DIR}/tests/run_leakcheck.cmake)
  set_tests_properties(leakcheck_${_name} PROPERTIES RESOURCE_LOCK src_${_name})
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
      "-DZANC_ARGS=${CMAKE_SOURCE_DIR}/stdlib/Gui/Widget/CodeEditor.zan;--auto-stdlib;--driver-dir;$<TARGET_FILE_DIR:zan_gui>"
      -DEXPECTED=${CMAKE_SOURCE_DIR}/tests/conformance/gui_codeeditor.out
      -DOUT_EXE=${CMAKE_BINARY_DIR}/conf_gui_codeeditor${_exe_ext}
      -P ${CMAKE_SOURCE_DIR}/tests/run_case.cmake)

  set_tests_properties(runtime_gui conformance_gui_stack conformance_gui_text conformance_gui_codeeditor PROPERTIES
    ENVIRONMENT "ZAN_LIB_PATH=$<TARGET_FILE_DIR:zan_gui>;LD_LIBRARY_PATH=$<TARGET_FILE_DIR:zan_gui>;DYLD_LIBRARY_PATH=$<TARGET_FILE_DIR:zan_gui>")
endif()

# ---- Runtime diagnostics: parser must not hang/OOM on malformed input ----
add_test(NAME diag_no_runaway
  COMMAND zanc ${CMAKE_SOURCE_DIR}/tests/diag/bad_member.zan --dump-ast)
set_tests_properties(diag_no_runaway PROPERTIES
  WILL_FAIL TRUE TIMEOUT 20)

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

# ---- zanpkg (package manager CLI) ----
add_executable(zanpkg
    src/pkg/zanpkg_main.c
)
if(WIN32)
    target_link_libraries(zanpkg PRIVATE)
else()
    target_link_libraries(zanpkg PRIVATE m)
endif()

# ---- zanfmt (code formatter) ----
add_executable(zanfmt
    src/fmt/zanfmt.c
)

# ---- zandoc (documentation generator) ----
add_executable(zandoc
    src/doc/zandoc.c
)
