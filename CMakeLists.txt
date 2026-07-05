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
    selfvm selfvmcf selfvmfn selfvmops selfvmarr selfvmelse)
  add_test(NAME selfhost_${_stage}
    COMMAND ${CMAKE_COMMAND}
      -DZANC=$<TARGET_FILE:zanc>
      -DSRC=${CMAKE_SOURCE_DIR}/src/self/${_stage}.zan
      -DEXPECTED=${CMAKE_SOURCE_DIR}/tests/selfhost/${_stage}.out
      -DOUT_EXE=${CMAKE_BINARY_DIR}/self_${_stage}${_exe_ext}
      -P ${CMAKE_SOURCE_DIR}/tests/run_case.cmake)
endforeach()

# ---- Real self-hosting: a Zan-written lexer scanning real Zan source ----
# zlex.zan reads src/self/zlex_input.zan from disk and tokenizes it, so the
# program is run with WORKDIR set to the source tree (relative File.ReadAllText).
add_test(NAME selfhost_zlex
  COMMAND ${CMAKE_COMMAND}
    -DZANC=$<TARGET_FILE:zanc>
    -DSRC=${CMAKE_SOURCE_DIR}/src/self/zlex.zan
    -DEXPECTED=${CMAKE_SOURCE_DIR}/tests/selfhost/zlex.out
    -DOUT_EXE=${CMAKE_BINARY_DIR}/self_zlex${_exe_ext}
    -DWORKDIR=${CMAKE_SOURCE_DIR}
    -P ${CMAKE_SOURCE_DIR}/tests/run_case.cmake)

# ---- Real self-hosting: a Zan-written compiler emitting real LLVM IR ----
# zc0.zan lexes + parses a real Zan source and emits LLVM IR to stdout.
# (a) golden-IR check: the emitted IR text is stable.
add_test(NAME selfhost_zc0_ir
  COMMAND ${CMAKE_COMMAND}
    -DZANC=$<TARGET_FILE:zanc>
    -DSRC=${CMAKE_SOURCE_DIR}/src/self/zc0.zan
    -DEXPECTED=${CMAKE_SOURCE_DIR}/tests/selfhost/zc0.ll
    -DOUT_EXE=${CMAKE_BINARY_DIR}/self_zc0_ir${_exe_ext}
    -DWORKDIR=${CMAKE_SOURCE_DIR}
    -P ${CMAKE_SOURCE_DIR}/tests/run_case.cmake)
# (b) end-to-end: clang lowers the emitted IR to a native exe whose output
#     must equal the same source compiled by the C zanc. Requires clang.
find_program(CLANG_EXE NAMES clang clang.exe)
if(CLANG_EXE)
  add_test(NAME selfhost_zc0
    COMMAND ${CMAKE_COMMAND}
      -DZANC=$<TARGET_FILE:zanc>
      -DZC0=${CMAKE_SOURCE_DIR}/src/self/zc0.zan
      -DINPUT=${CMAKE_SOURCE_DIR}/src/self/zc0_input.zan
      -DWORKDIR=${CMAKE_SOURCE_DIR}
      -DBINDIR=${CMAKE_BINARY_DIR}
      -DCLANG=${CLANG_EXE}
      -DEXE_EXT=${_exe_ext}
      -P ${CMAKE_SOURCE_DIR}/tests/run_selfcompile.cmake)
endif()

# zc1.zan: adds integer variables, assignment, if/while, comparisons.
add_test(NAME selfhost_zc1_ir
  COMMAND ${CMAKE_COMMAND}
    -DZANC=$<TARGET_FILE:zanc>
    -DSRC=${CMAKE_SOURCE_DIR}/src/self/zc1.zan
    -DEXPECTED=${CMAKE_SOURCE_DIR}/tests/selfhost/zc1.ll
    -DOUT_EXE=${CMAKE_BINARY_DIR}/self_zc1_ir${_exe_ext}
    -DWORKDIR=${CMAKE_SOURCE_DIR}
    -P ${CMAKE_SOURCE_DIR}/tests/run_case.cmake)
if(CLANG_EXE)
  add_test(NAME selfhost_zc1
    COMMAND ${CMAKE_COMMAND}
      -DZANC=$<TARGET_FILE:zanc>
      -DZC0=${CMAKE_SOURCE_DIR}/src/self/zc1.zan
      -DINPUT=${CMAKE_SOURCE_DIR}/src/self/zc1_input.zan
      -DWORKDIR=${CMAKE_SOURCE_DIR}
      -DBINDIR=${CMAKE_BINARY_DIR}
      -DCLANG=${CLANG_EXE}
      -DEXE_EXT=${_exe_ext}
      -P ${CMAKE_SOURCE_DIR}/tests/run_selfcompile.cmake)
endif()

# zc2.zan: adds user-defined functions, parameters, return, calls (recursion).
add_test(NAME selfhost_zc2_ir
  COMMAND ${CMAKE_COMMAND}
    -DZANC=$<TARGET_FILE:zanc>
    -DSRC=${CMAKE_SOURCE_DIR}/src/self/zc2.zan
    -DEXPECTED=${CMAKE_SOURCE_DIR}/tests/selfhost/zc2.ll
    -DOUT_EXE=${CMAKE_BINARY_DIR}/self_zc2_ir${_exe_ext}
    -DWORKDIR=${CMAKE_SOURCE_DIR}
    -P ${CMAKE_SOURCE_DIR}/tests/run_case.cmake)
if(CLANG_EXE)
  add_test(NAME selfhost_zc2
    COMMAND ${CMAKE_COMMAND}
      -DZANC=$<TARGET_FILE:zanc>
      -DZC0=${CMAKE_SOURCE_DIR}/src/self/zc2.zan
      -DINPUT=${CMAKE_SOURCE_DIR}/src/self/zc2_input.zan
      -DWORKDIR=${CMAKE_SOURCE_DIR}
      -DBINDIR=${CMAKE_BINARY_DIR}
      -DCLANG=${CLANG_EXE}
      -DEXE_EXT=${_exe_ext}
      -P ${CMAKE_SOURCE_DIR}/tests/run_selfcompile.cmake)
endif()

# zc3.zan: adds if/else if/else chains and logical && / || in conditions.
add_test(NAME selfhost_zc3_ir
  COMMAND ${CMAKE_COMMAND}
    -DZANC=$<TARGET_FILE:zanc>
    -DSRC=${CMAKE_SOURCE_DIR}/src/self/zc3.zan
    -DEXPECTED=${CMAKE_SOURCE_DIR}/tests/selfhost/zc3.ll
    -DOUT_EXE=${CMAKE_BINARY_DIR}/self_zc3_ir${_exe_ext}
    -DWORKDIR=${CMAKE_SOURCE_DIR}
    -P ${CMAKE_SOURCE_DIR}/tests/run_case.cmake)
if(CLANG_EXE)
  add_test(NAME selfhost_zc3
    COMMAND ${CMAKE_COMMAND}
      -DZANC=$<TARGET_FILE:zanc>
      -DZC0=${CMAKE_SOURCE_DIR}/src/self/zc3.zan
      -DINPUT=${CMAKE_SOURCE_DIR}/src/self/zc3_input.zan
      -DWORKDIR=${CMAKE_SOURCE_DIR}
      -DBINDIR=${CMAKE_BINARY_DIR}
      -DCLANG=${CLANG_EXE}
      -DEXE_EXT=${_exe_ext}
      -P ${CMAKE_SOURCE_DIR}/tests/run_selfcompile.cmake)
endif()

# zc4.zan: adds string literals (byte-array globals) and string output via puts.
add_test(NAME selfhost_zc4_ir
  COMMAND ${CMAKE_COMMAND}
    -DZANC=$<TARGET_FILE:zanc>
    -DSRC=${CMAKE_SOURCE_DIR}/src/self/zc4.zan
    -DEXPECTED=${CMAKE_SOURCE_DIR}/tests/selfhost/zc4.ll
    -DOUT_EXE=${CMAKE_BINARY_DIR}/self_zc4_ir${_exe_ext}
    -DWORKDIR=${CMAKE_SOURCE_DIR}
    -P ${CMAKE_SOURCE_DIR}/tests/run_case.cmake)
if(CLANG_EXE)
  add_test(NAME selfhost_zc4
    COMMAND ${CMAKE_COMMAND}
      -DZANC=$<TARGET_FILE:zanc>
      -DZC0=${CMAKE_SOURCE_DIR}/src/self/zc4.zan
      -DINPUT=${CMAKE_SOURCE_DIR}/src/self/zc4_input.zan
      -DWORKDIR=${CMAKE_SOURCE_DIR}
      -DBINDIR=${CMAKE_BINARY_DIR}
      -DCLANG=${CLANG_EXE}
      -DEXE_EXT=${_exe_ext}
      -P ${CMAKE_SOURCE_DIR}/tests/run_selfcompile.cmake)
endif()

# zc5.zan: adds local integer arrays (alloca [N x i64], getelementptr read/write).
add_test(NAME selfhost_zc5_ir
  COMMAND ${CMAKE_COMMAND}
    -DZANC=$<TARGET_FILE:zanc>
    -DSRC=${CMAKE_SOURCE_DIR}/src/self/zc5.zan
    -DEXPECTED=${CMAKE_SOURCE_DIR}/tests/selfhost/zc5.ll
    -DOUT_EXE=${CMAKE_BINARY_DIR}/self_zc5_ir${_exe_ext}
    -DWORKDIR=${CMAKE_SOURCE_DIR}
    -P ${CMAKE_SOURCE_DIR}/tests/run_case.cmake)
if(CLANG_EXE)
  add_test(NAME selfhost_zc5
    COMMAND ${CMAKE_COMMAND}
      -DZANC=$<TARGET_FILE:zanc>
      -DZC0=${CMAKE_SOURCE_DIR}/src/self/zc5.zan
      -DINPUT=${CMAKE_SOURCE_DIR}/src/self/zc5_input.zan
      -DWORKDIR=${CMAKE_SOURCE_DIR}
      -DBINDIR=${CMAKE_BINARY_DIR}
      -DCLANG=${CLANG_EXE}
      -DEXE_EXT=${_exe_ext}
      -P ${CMAKE_SOURCE_DIR}/tests/run_selfcompile.cmake)
endif()

# zc6.zan: adds class/struct fields (instance as [Nfields x i64], getelementptr).
add_test(NAME selfhost_zc6_ir
  COMMAND ${CMAKE_COMMAND}
    -DZANC=$<TARGET_FILE:zanc>
    -DSRC=${CMAKE_SOURCE_DIR}/src/self/zc6.zan
    -DEXPECTED=${CMAKE_SOURCE_DIR}/tests/selfhost/zc6.ll
    -DOUT_EXE=${CMAKE_BINARY_DIR}/self_zc6_ir${_exe_ext}
    -DWORKDIR=${CMAKE_SOURCE_DIR}
    -P ${CMAKE_SOURCE_DIR}/tests/run_case.cmake)
if(CLANG_EXE)
  add_test(NAME selfhost_zc6
    COMMAND ${CMAKE_COMMAND}
      -DZANC=$<TARGET_FILE:zanc>
      -DZC0=${CMAKE_SOURCE_DIR}/src/self/zc6.zan
      -DINPUT=${CMAKE_SOURCE_DIR}/src/self/zc6_input.zan
      -DWORKDIR=${CMAKE_SOURCE_DIR}
      -DBINDIR=${CMAKE_BINARY_DIR}
      -DCLANG=${CLANG_EXE}
      -DEXE_EXT=${_exe_ext}
      -P ${CMAKE_SOURCE_DIR}/tests/run_selfcompile.cmake)
endif()

# ---- Determinism: codegen must be a pure function of the source ----
# Compiling the same program twice must yield byte-identical LLVM IR. This is
# the reproducible-build / correctness net that stands in for a self-hosting
# gen2==gen3 diff (zanc itself is written in C, not Zan).
foreach(_src ${_conf_srcs})
  get_filename_component(_name ${_src} NAME_WE)
  add_test(NAME determinism_${_name}
    COMMAND ${CMAKE_COMMAND}
      -DZANC=$<TARGET_FILE:zanc>
      -DSRC=${_src}
      -P ${CMAKE_SOURCE_DIR}/tests/run_determinism.cmake)
endforeach()
foreach(_stage
    selfhost selfparse selfpipe selfvars selfctrl
    selfvm selfvmcf selfvmfn selfvmops selfvmarr selfvmelse)
  add_test(NAME determinism_${_stage}
    COMMAND ${CMAKE_COMMAND}
      -DZANC=$<TARGET_FILE:zanc>
      -DSRC=${CMAKE_SOURCE_DIR}/src/self/${_stage}.zan
      -P ${CMAKE_SOURCE_DIR}/tests/run_determinism.cmake)
endforeach()
add_test(NAME determinism_zlex
  COMMAND ${CMAKE_COMMAND}
    -DZANC=$<TARGET_FILE:zanc>
    -DSRC=${CMAKE_SOURCE_DIR}/src/self/zlex.zan
    -P ${CMAKE_SOURCE_DIR}/tests/run_determinism.cmake)
add_test(NAME determinism_zc0
  COMMAND ${CMAKE_COMMAND}
    -DZANC=$<TARGET_FILE:zanc>
    -DSRC=${CMAKE_SOURCE_DIR}/src/self/zc0.zan
    -P ${CMAKE_SOURCE_DIR}/tests/run_determinism.cmake)
add_test(NAME determinism_zc1
  COMMAND ${CMAKE_COMMAND}
    -DZANC=$<TARGET_FILE:zanc>
    -DSRC=${CMAKE_SOURCE_DIR}/src/self/zc1.zan
    -P ${CMAKE_SOURCE_DIR}/tests/run_determinism.cmake)
add_test(NAME determinism_zc2
  COMMAND ${CMAKE_COMMAND}
    -DZANC=$<TARGET_FILE:zanc>
    -DSRC=${CMAKE_SOURCE_DIR}/src/self/zc2.zan
    -P ${CMAKE_SOURCE_DIR}/tests/run_determinism.cmake)
add_test(NAME determinism_zc3
  COMMAND ${CMAKE_COMMAND}
    -DZANC=$<TARGET_FILE:zanc>
    -DSRC=${CMAKE_SOURCE_DIR}/src/self/zc3.zan
    -P ${CMAKE_SOURCE_DIR}/tests/run_determinism.cmake)
add_test(NAME determinism_zc4
  COMMAND ${CMAKE_COMMAND}
    -DZANC=$<TARGET_FILE:zanc>
    -DSRC=${CMAKE_SOURCE_DIR}/src/self/zc4.zan
    -P ${CMAKE_SOURCE_DIR}/tests/run_determinism.cmake)
add_test(NAME determinism_zc5
  COMMAND ${CMAKE_COMMAND}
    -DZANC=$<TARGET_FILE:zanc>
    -DSRC=${CMAKE_SOURCE_DIR}/src/self/zc5.zan
    -P ${CMAKE_SOURCE_DIR}/tests/run_determinism.cmake)
add_test(NAME determinism_zc6
  COMMAND ${CMAKE_COMMAND}
    -DZANC=$<TARGET_FILE:zanc>
    -DSRC=${CMAKE_SOURCE_DIR}/src/self/zc6.zan
    -P ${CMAKE_SOURCE_DIR}/tests/run_determinism.cmake)

# ---- Runtime diagnostics: parser must not hang/OOM on malformed input ----
add_test(NAME diag_no_runaway
  COMMAND zanc ${CMAKE_SOURCE_DIR}/tests/diag/bad_member.zan --dump-ast)
set_tests_properties(diag_no_runaway PROPERTIES
  WILL_FAIL TRUE TIMEOUT 20)
