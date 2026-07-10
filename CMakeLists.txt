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

# ---- Find LLVM ----
find_package(LLVM REQUIRED CONFIG)
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

llvm_map_components_to_libnames(LLVM_LIBS
    core support analysis bitwriter target
    x86codegen x86asmparser x86desc x86info
    aarch64codegen aarch64asmparser aarch64desc aarch64info
    webassemblycodegen webassemblyasmparser webassemblydesc webassemblyinfo
    armcodegen armasmparser armdesc arminfo
    passes
)

# ---- Compiler executable ----
add_executable(zanc
    src/compiler/main.c
    src/compiler/arena.c
    src/compiler/diag.c
    src/compiler/lexer.c
    src/compiler/ast.c
    src/compiler/parser.c
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
                    -DDEST=$<TARGET_FILE_DIR:zanc>/toolchain
                    -P ${CMAKE_SOURCE_DIR}/cmake/bundle_toolchain.cmake
            COMMENT "Assembling self-contained MinGW linking toolchain"
            VERBATIM)
    endif()
endif()

# ---- Linux cross-compilation sysroot ----
# Copy the bundled musl sysroot (committed at toolchain/linux-musl/) next to
# zanc so that --target linux-musl / linux-x64 can static-link ELF binaries
# without any external toolchain.  The sysroot contains crt*.o, libc.a, and
# a pre-built zanrt_io.o for socket-async programs.
set(LINUX_MUSL_SYS "${CMAKE_SOURCE_DIR}/toolchain/linux-musl")
if(EXISTS "${LINUX_MUSL_SYS}/libc.a")
    add_custom_command(TARGET zanc POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:zanc>/toolchain/linux-musl
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${LINUX_MUSL_SYS}/crt1.o" "${LINUX_MUSL_SYS}/crti.o"
                "${LINUX_MUSL_SYS}/crtn.o" "${LINUX_MUSL_SYS}/libc.a"
                "${LINUX_MUSL_SYS}/zanrt_io.o"
                $<TARGET_FILE_DIR:zanc>/toolchain/linux-musl/
        COMMENT "Copying Linux musl sysroot for cross-compilation"
        VERBATIM)
endif()


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
    src/ide/intellisense.c
)
target_include_directories(zan-lsp PRIVATE src/compiler src/common src/ide)
if(MSVC)
    target_compile_definitions(zan-lsp PRIVATE _CRT_SECURE_NO_WARNINGS)
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
    src/ide/debugger.c
)
target_include_directories(zan-dap PRIVATE src/common src/ide)
if(MSVC)
    target_compile_definitions(zan-dap PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_link_libraries(zan-dap PRIVATE m)
endif()


# ---- Native GUI runtime (zan_gui) ----
# Cross-platform software renderer + windowing shell used by the Gui stdlib via
# [DllImport("zan_gui")]: Win32 (full), X11 (Linux), Cocoa (macOS). Built
# as a shared library so its platform system deps (X11 / user32+gdi32) are
# self-contained and Zan programs only need `-lzan_gui`.
option(ZAN_BUILD_GUI "Build the native zan_gui runtime library" ON)
set(ZAN_GUI_AVAILABLE FALSE)
if(ZAN_BUILD_GUI)
    if(WIN32)
        add_library(zan_gui SHARED src/runtime/gui_runtime.c)
        target_link_libraries(zan_gui PRIVATE user32 gdi32 dwmapi shcore)
        set(ZAN_GUI_AVAILABLE TRUE)
    elseif(APPLE)
        # Real Cocoa backend (Objective-C) + shared software renderer. The C file
        # is compiled with ZAN_GUI_COCOA so its macOS stub branch is disabled and
        # gui_runtime_mac.m supplies the native windowing/window-management ABI.
        add_library(zan_gui SHARED src/runtime/gui_runtime.c src/runtime/gui_runtime_mac.m)
        set_source_files_properties(src/runtime/gui_runtime.c
            PROPERTIES COMPILE_DEFINITIONS ZAN_GUI_COCOA)
        target_link_libraries(zan_gui PRIVATE m "-framework Cocoa")
        set(ZAN_GUI_AVAILABLE TRUE)
    else()
        find_package(X11)
        if(X11_FOUND)
            add_library(zan_gui SHARED src/runtime/gui_runtime.c)
            target_include_directories(zan_gui PRIVATE ${X11_INCLUDE_DIR})
            target_link_libraries(zan_gui PRIVATE ${X11_LIBRARIES} m)
            set(ZAN_GUI_AVAILABLE TRUE)
        else()
            message(WARNING "libX11 not found; skipping zan_gui (Gui conformance tests will be disabled)")
        endif()
    endif()
endif()

# ---- IDE ----
# The IDE is built from src/ide_zan/ide_demo.zan using the Zan compiler (zanc).
# It is compiled separately: zanc src/ide_zan/ide_demo.zan -o build/ide_demo.exe
# The old C-based zan-ide target has been removed in favour of ide_demo.

# ---- Tests ----
enable_testing()

add_test(NAME test_lex_hello COMMAND zanc ${CMAKE_SOURCE_DIR}/examples/hello.zan --dump-tokens)
add_test(NAME test_parse_hello COMMAND zanc ${CMAKE_SOURCE_DIR}/examples/hello.zan --dump-ast)
add_test(NAME test_emit_ir_hello COMMAND zanc ${CMAKE_SOURCE_DIR}/examples/hello.zan --emit-ir)
add_test(NAME test_compile_hello COMMAND zanc ${CMAKE_SOURCE_DIR}/examples/hello.zan -o ${CMAKE_BINARY_DIR}/hello_test)

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
set(_known_leaky async_asocket_echo encoding_base64 list_class object_array)
foreach(_src ${_conf_srcs})
  get_filename_component(_name ${_src} NAME_WE)
  add_test(NAME leakcheck_${_name}
    COMMAND ${CMAKE_COMMAND}
      -DZANC=$<TARGET_FILE:zanc>
      -DSRC=${_src}
      -DOUT_EXE=${CMAKE_BINARY_DIR}/leak_${_name}${_exe_ext}
      -P ${CMAKE_SOURCE_DIR}/tests/run_leakcheck.cmake)
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

# ---- Skeletal multi-file Zan compiler: lexer + parser + driver ----
# A tiny expression compiler/evaluator written in Zan across three files,
# exercising classes, enum/switch, List<class>, string char-lexing, casts and
# cross-file references �?the "host is strong enough" acceptance program.
add_test(NAME conformance_minic
  COMMAND ${CMAKE_COMMAND}
    -DZANC=$<TARGET_FILE:zanc>
    -DSRC=${CMAKE_SOURCE_DIR}/examples/minic/driver.zan
    "-DZANC_ARGS=${CMAKE_SOURCE_DIR}/examples/minic/lexer.zan;${CMAKE_SOURCE_DIR}/examples/minic/parser.zan"
    -DEXPECTED=${CMAKE_SOURCE_DIR}/examples/minic/expected.out
    -DOUT_EXE=${CMAKE_BINARY_DIR}/conf_minic${_exe_ext}
    -P ${CMAKE_SOURCE_DIR}/tests/run_case.cmake)

# ---- Gui conformance: headless layout/text math (needs the zan_gui lib) ----
# These programs import Gui modules that reference [DllImport("zan_gui")], so the
# produced executable must link zan_gui. zanc resolves [DllImport] libs against
# $ZAN_LIB_PATH, so the tests point that (and the OS loader path) at the freshly
# built zan_gui. Enabled wherever zan_gui is available; the Windows CI job builds
# only zanc, so it passes -DZAN_BUILD_GUI_TESTS=OFF.
option(ZAN_BUILD_GUI_TESTS "Run the Gui conformance tests (requires zan_gui)" ON)
if(ZAN_BUILD_GUI_TESTS AND ZAN_GUI_AVAILABLE)
  add_test(NAME conformance_gui_stack
    COMMAND ${CMAKE_COMMAND}
      -DZANC=$<TARGET_FILE:zanc>
      -DSRC=${CMAKE_SOURCE_DIR}/examples/layout_test.zan
      # Types.zan is pulled in automatically by 'using Gui;' (auto-stdlib), so
      # only the non-auto-included Stack.zan is passed explicitly here.
      "-DZANC_ARGS=${CMAKE_SOURCE_DIR}/stdlib/gui/Stack.zan"
      -DEXPECTED=${CMAKE_SOURCE_DIR}/tests/conformance/gui_stack.out
      -DOUT_EXE=${CMAKE_BINARY_DIR}/conf_gui_stack${_exe_ext}
      -P ${CMAKE_SOURCE_DIR}/tests/run_case.cmake)

  add_test(NAME conformance_gui_text
    COMMAND ${CMAKE_COMMAND}
      -DZANC=$<TARGET_FILE:zanc>
      -DSRC=${CMAKE_SOURCE_DIR}/examples/text_test.zan
      "-DZANC_ARGS=${CMAKE_SOURCE_DIR}/stdlib/gui/Text.zan"
      -DEXPECTED=${CMAKE_SOURCE_DIR}/tests/conformance/gui_text.out
      -DOUT_EXE=${CMAKE_BINARY_DIR}/conf_gui_text${_exe_ext}
      -P ${CMAKE_SOURCE_DIR}/tests/run_case.cmake)

  set_tests_properties(conformance_gui_stack conformance_gui_text PROPERTIES
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
