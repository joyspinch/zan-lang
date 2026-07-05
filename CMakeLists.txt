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

# ---- Self-hosting regression: golden outputs for each bootstrap stage ----
foreach(_stage
    selfhost selfparse selfpipe selfvars selfctrl
    selfvm selfvmcf selfvmfn selfvmops)
  add_test(NAME selfhost_${_stage}
    COMMAND ${CMAKE_COMMAND}
      -DZANC=$<TARGET_FILE:zanc>
      -DSRC=${CMAKE_SOURCE_DIR}/src/self/${_stage}.zan
      -DEXPECTED=${CMAKE_SOURCE_DIR}/tests/selfhost/${_stage}.out
      -DOUT_EXE=${CMAKE_BINARY_DIR}/self_${_stage}${_exe_ext}
      -P ${CMAKE_SOURCE_DIR}/tests/run_case.cmake)
endforeach()

# ---- Runtime diagnostics: parser must not hang/OOM on malformed input ----
add_test(NAME diag_no_runaway
  COMMAND zanc ${CMAKE_SOURCE_DIR}/tests/diag/bad_member.zan --dump-ast)
set_tests_properties(diag_no_runaway PROPERTIES
  WILL_FAIL TRUE TIMEOUT 20)
