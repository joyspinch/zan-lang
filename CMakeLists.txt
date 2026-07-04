cmake_minimum_required(VERSION 3.16)
project(zan-lang C)

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
)
target_include_directories(zanc PRIVATE src/compiler ${LLVM_INCLUDE_DIRS})
target_link_libraries(zanc PRIVATE ${LLVM_LIBS} stdc++ m)
target_compile_definitions(zanc PRIVATE ${LLVM_DEFINITIONS})

# ---- Tests ----
enable_testing()

# Test: dump tokens for hello.zan
add_test(
    NAME test_lex_hello
    COMMAND zanc ${CMAKE_SOURCE_DIR}/examples/hello.zan --dump-tokens
)

# Test: dump AST for hello.zan
add_test(
    NAME test_parse_hello
    COMMAND zanc ${CMAKE_SOURCE_DIR}/examples/hello.zan --dump-ast
)

# Test: compile hello.zan to IR
add_test(
    NAME test_emit_ir_hello
    COMMAND zanc ${CMAKE_SOURCE_DIR}/examples/hello.zan --emit-ir
)

# Test: compile hello.zan to executable
add_test(
    NAME test_compile_hello
    COMMAND zanc ${CMAKE_SOURCE_DIR}/examples/hello.zan -o ${CMAKE_BINARY_DIR}/hello_test
)
