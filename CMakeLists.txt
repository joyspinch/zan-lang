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


# ---- IDE executable (Windows only) ----
if(WIN32)
    add_executable(zan-ide WIN32
        src/ide/ide_main.c
        src/ide/editor.c
        src/ide/piece_table.c
        src/ide/project.c
        src/ide/intellisense.c
        src/ide/debugger.c
    )
    target_include_directories(zan-ide PRIVATE src/ide)
    target_link_libraries(zan-ide PRIVATE user32 gdi32 comdlg32 shell32 ole32)
    target_compile_definitions(zan-ide PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

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
# cross-file references — the "host is strong enough" acceptance program.
add_test(NAME conformance_minic
  COMMAND ${CMAKE_COMMAND}
    -DZANC=$<TARGET_FILE:zanc>
    -DSRC=${CMAKE_SOURCE_DIR}/examples/minic/driver.zan
    "-DZANC_ARGS=${CMAKE_SOURCE_DIR}/examples/minic/lexer.zan;${CMAKE_SOURCE_DIR}/examples/minic/parser.zan"
    -DEXPECTED=${CMAKE_SOURCE_DIR}/examples/minic/expected.out
    -DOUT_EXE=${CMAKE_BINARY_DIR}/conf_minic${_exe_ext}
    -P ${CMAKE_SOURCE_DIR}/tests/run_case.cmake)

# ---- Gui.Stack layout engine: headless check of the layout cursor math ----
add_test(NAME conformance_gui_stack
  COMMAND ${CMAKE_COMMAND}
    -DZANC=$<TARGET_FILE:zanc>
    -DSRC=${CMAKE_SOURCE_DIR}/examples/layout_test.zan
    "-DZANC_ARGS=${CMAKE_SOURCE_DIR}/stdlib/Gui/Types.zan;${CMAKE_SOURCE_DIR}/stdlib/Gui/Stack.zan"
    -DEXPECTED=${CMAKE_SOURCE_DIR}/tests/conformance/gui_stack.out
    -DOUT_EXE=${CMAKE_BINARY_DIR}/conf_gui_stack${_exe_ext}
    -P ${CMAKE_SOURCE_DIR}/tests/run_case.cmake)

# ---- Runtime diagnostics: parser must not hang/OOM on malformed input ----
add_test(NAME diag_no_runaway
  COMMAND zanc ${CMAKE_SOURCE_DIR}/tests/diag/bad_member.zan --dump-ast)
set_tests_properties(diag_no_runaway PROPERTIES
  WILL_FAIL TRUE TIMEOUT 20)

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
