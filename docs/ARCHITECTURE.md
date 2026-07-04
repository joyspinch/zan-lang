# Zan Compiler Architecture

## 1. Overview

The Zan compiler (`zanc`) is a single-pass, ahead-of-time compiler written in C11. It transforms `.zan` source files into native executables via LLVM.

```
 .zan source files
        │
        ▼
┌──────────────┐
│   Driver     │  Orchestrates the full pipeline
└──────┬───────┘
       │
       ▼
┌──────────────┐
│   Lexer      │  Source text → Token stream
│  (lexer.c)   │  Handles: keywords, operators, literals, strings, comments
└──────┬───────┘
       │ Token stream
       ▼
┌──────────────┐
│   Parser     │  Tokens → Abstract Syntax Tree (AST)
│  (parser.c)  │  Recursive descent, pratt parsing for expressions
└──────┬───────┘
       │ AST
       ▼
┌──────────────┐
│   Binder     │  AST → Bound Tree (resolved names, symbols)
│  (binder.c)  │  Name resolution, scope management, symbol table
└──────┬───────┘
       │ Bound Tree
       ▼
┌──────────────┐
│   Checker    │  Type checking, inference, constraint solving
│  (checker.c) │  Generic instantiation, interface conformance
└──────┬───────┘
       │ Typed Tree
       ▼
┌──────────────┐
│   IRGen      │  Typed Tree → LLVM IR
│  (irgen.c)   │  ARC insertion, vtable generation, monomorphization
└──────┬───────┘
       │ LLVM IR Module
       ▼
┌──────────────┐
│   LLVM       │  Optimization passes + Machine code generation
│  (libLLVM)   │  mem2reg, SROA, GVN, inlining, vectorization
└──────┬───────┘
       │ Object file (.o / .obj)
       ▼
┌──────────────┐
│   Linker     │  Link runtime + stdlib + native deps → executable
│  (system)    │  Uses platform linker (ld/lld/link.exe)
└──────────────┘
```

---

## 2. Module Responsibilities

### 2.1 Driver (`driver.c` / `driver.h`)

The driver orchestrates the entire compilation pipeline:

```c
typedef struct {
    const char *input_file;      /* Main source file */
    const char *output_file;     /* Output executable path */
    const char *stdlib_path;     /* Path to stdlib/ directory */
    const char *target_triple;   /* LLVM target triple */
    int  opt_level;              /* 0=debug, 1=default, 2=release, 3=aggressive */
    bool emit_llvm_ir;           /* Dump .ll file */
    bool emit_object;            /* Stop at .o file */
    bool debug_info;             /* Generate debug info */
    bool check_only;             /* Type check without codegen */
} zan_options_t;

int zan_compile(zan_options_t *opts);
```

Responsibilities:
- Parse command-line arguments
- Load source files (recursively resolve `using` declarations)
- Run each pipeline stage in order
- Report diagnostics (errors, warnings)
- Invoke LLVM and linker

### 2.2 Lexer (`lexer.c` / `lexer.h`)

Converts source text to a stream of tokens.

**Key design decisions:**
- Hand-written (not generated) — faster, better error recovery
- Single-pass, streaming — `zan_lexer_next()` returns one token at a time
- Handles string interpolation as nested token sequences
- All string data stored in arena (no allocation per token)

**Token structure:**
```c
typedef struct {
    zan_token_kind_t kind;   /* Token type */
    zan_str_t text;          /* Source text slice (non-owning) */
    zan_loc_t loc;           /* File, line, column */
    union {
        int64_t int_val;     /* Parsed integer value */
        double  float_val;   /* Parsed float value */
    };
} zan_token_t;
```

**Keyword recognition:** Binary search on sorted keyword table.

### 2.3 Parser (`parser.c` / `parser.h`)

Recursive descent parser producing an AST.

**Key design decisions:**
- Predictive LL(1) for declarations, Pratt parsing for expressions
- AST nodes allocated in arena (freed all at once after compilation)
- Error recovery: sync to next semicolon/brace on error, continue parsing
- No separate preprocessor phase

**AST node hierarchy:**
```c
typedef enum {
    /* Top-level */
    AST_COMPILATION_UNIT,
    AST_USING_DECL,
    AST_NAMESPACE_DECL,

    /* Type declarations */
    AST_CLASS_DECL,
    AST_STRUCT_DECL,
    AST_INTERFACE_DECL,
    AST_ENUM_DECL,
    AST_DELEGATE_DECL,

    /* Members */
    AST_FIELD_DECL,
    AST_METHOD_DECL,
    AST_CONSTRUCTOR_DECL,
    AST_DESTRUCTOR_DECL,
    AST_PROPERTY_DECL,
    AST_OPERATOR_DECL,

    /* Statements */
    AST_BLOCK,
    AST_VAR_DECL_STMT,
    AST_EXPR_STMT,
    AST_IF_STMT,
    AST_WHILE_STMT,
    AST_FOR_STMT,
    AST_FOREACH_STMT,
    AST_SWITCH_STMT,
    AST_RETURN_STMT,
    AST_BREAK_STMT,
    AST_CONTINUE_STMT,
    AST_THROW_STMT,
    AST_TRY_STMT,

    /* Expressions */
    AST_LITERAL_EXPR,
    AST_IDENT_EXPR,
    AST_BINARY_EXPR,
    AST_UNARY_EXPR,
    AST_CALL_EXPR,
    AST_MEMBER_EXPR,
    AST_INDEX_EXPR,
    AST_CAST_EXPR,
    AST_NEW_EXPR,
    AST_LAMBDA_EXPR,
    AST_TERNARY_EXPR,
    AST_ASSIGN_EXPR,
    AST_IS_EXPR,
    AST_AS_EXPR,
    AST_INTERPOLATION_EXPR,
    AST_SWITCH_EXPR,
    AST_NULL_COALESCE_EXPR,
    AST_NULL_CONDITIONAL_EXPR,
    AST_TYPEOF_EXPR,
    AST_SIZEOF_EXPR,
    AST_DEFAULT_EXPR,

    /* Types */
    AST_NAMED_TYPE,
    AST_ARRAY_TYPE,
    AST_NULLABLE_TYPE,
    AST_TUPLE_TYPE,
    AST_GENERIC_TYPE,
} zan_ast_kind_t;

typedef struct zan_ast_node {
    zan_ast_kind_t kind;
    zan_loc_t loc;
    /* Kind-specific fields follow via union or tagged struct */
} zan_ast_node_t;
```

### 2.4 Binder (`binder.c` / `binder.h`)

Resolves names and builds the symbol table.

**Symbol table structure:**
```c
typedef enum {
    SYM_NAMESPACE,
    SYM_CLASS,
    SYM_STRUCT,
    SYM_INTERFACE,
    SYM_ENUM,
    SYM_FUNCTION,
    SYM_VARIABLE,
    SYM_PARAMETER,
    SYM_FIELD,
    SYM_PROPERTY,
    SYM_TYPE_PARAM,
} zan_symbol_kind_t;

typedef struct zan_symbol {
    zan_symbol_kind_t kind;
    zan_str_t name;
    zan_str_t full_name;            /* Fully qualified name */
    struct zan_scope *scope;         /* Containing scope */
    zan_ast_node_t *decl;           /* Declaration AST node */
    struct zan_type *type;          /* Resolved type */
    int flags;                       /* Access modifiers, static, etc. */
} zan_symbol_t;

typedef struct zan_scope {
    struct zan_scope *parent;
    zan_symbol_t **symbols;          /* Hash table of symbols */
    size_t symbol_count;
    size_t symbol_capacity;
} zan_scope_t;
```

**Binding pass:**
1. First pass: collect all type declarations (classes, structs, interfaces, enums)
2. Second pass: resolve member signatures (fields, method parameters, return types)
3. Third pass: resolve method bodies (local variables, expressions)

This two/three-pass approach handles forward references and mutual recursion.

### 2.5 Checker (`checker.c` / `checker.h`)

Type checking and inference.

**Responsibilities:**
- Verify type compatibility of assignments, arguments, returns
- Infer types for `var` declarations and lambda parameters
- Resolve overloaded methods
- Check generic constraints
- Verify interface conformance
- Check exhaustiveness of switch/match expressions
- Detect definite assignment (variables used before initialization)
- Check accessibility (public/private/protected)

**Type representation:**
```c
typedef enum {
    TYPE_VOID,
    TYPE_BOOL,
    TYPE_INT,
    TYPE_FLOAT,
    TYPE_DOUBLE,
    TYPE_CHAR,
    TYPE_STRING,
    TYPE_STRUCT,
    TYPE_CLASS,
    TYPE_INTERFACE,
    TYPE_ENUM,
    TYPE_ARRAY,
    TYPE_NULLABLE,
    TYPE_TUPLE,
    TYPE_DELEGATE,
    TYPE_TYPE_PARAM,
    TYPE_ERROR,          /* Sentinel for error recovery */
} zan_type_kind_t;

typedef struct zan_type {
    zan_type_kind_t kind;
    zan_str_t name;
    zan_symbol_t *symbol;           /* Back-reference to symbol table */
    struct zan_type **type_args;    /* For generic instantiations */
    int type_arg_count;
    bool is_value_type;
    size_t size;                    /* Computed size in bytes */
    size_t align;                   /* Alignment requirement */
} zan_type_t;
```

### 2.6 IRGen (`irgen.c` / `irgen.h`)

Generates LLVM IR from the typed AST.

**Key responsibilities:**
- Generate LLVM IR for all constructs
- Insert ARC retain/release calls for reference types
- Generate vtables for virtual dispatch
- Monomorphize generic types and functions for value type arguments
- Generate type descriptors for RTTI
- Generate debug info (DWARF/CodeView)
- Handle FFI (DllImport, C function calls)

**ARC insertion strategy:**
```
For each reference-type variable:
  - On assignment: retain new value, release old value
  - On scope exit: release
  - On parameter entry: retain (caller already owns a reference)
  - On return: transfer ownership (no retain/release)
  - Optimization: if variable is last-used, move instead of copy
```

**Object layout:**
```
┌─────────────────┐
│ refcount (i64)  │  offset 0: atomic reference count
├─────────────────┤
│ type_info (ptr) │  offset 8: pointer to type descriptor
├─────────────────┤
│ field_1         │  offset 16+: instance fields
│ field_2         │
│ ...             │
└─────────────────┘
```

**Virtual dispatch:**
```
Type Descriptor:
┌─────────────────┐
│ type_name (ptr) │  Name string for reflection
├─────────────────┤
│ type_size (i64) │  sizeof(instance)
├─────────────────┤
│ destructor(ptr) │  ~T() function pointer
├─────────────────┤
│ vtable[0]       │  First virtual method
│ vtable[1]       │  Second virtual method
│ ...             │
└─────────────────┘
```

### 2.7 Diagnostics (`diag.c` / `diag.h`)

Error and warning reporting with source locations.

```c
typedef enum {
    DIAG_ERROR,
    DIAG_WARNING,
    DIAG_NOTE,
} zan_diag_level_t;

typedef struct {
    zan_diag_level_t level;
    const char *code;       /* e.g., "ZAN0001" */
    const char *message;
    zan_loc_t loc;
    const char *source_line;
} zan_diagnostic_t;
```

**Output format:**
```
src/main.zan:10:5: error ZAN0042: cannot convert 'string' to 'int'
   10 |     int x = "hello";
      |         ^~~~~~~~~~~
```

Features:
- Colored output (when terminal supports it)
- Source line display with caret pointing to error
- Error codes for each diagnostic (searchable)
- Maximum error count (stop after N errors)
- Warning suppression via pragmas

---

## 3. Memory Management in the Compiler

### 3.1 Arena Allocator

All AST nodes, symbols, types, and strings are allocated from arenas:

```c
zan_arena_t ast_arena;      /* Freed after IRGen */
zan_arena_t type_arena;     /* Freed after compilation */
zan_arena_t string_arena;   /* Interned strings, freed at end */
```

Benefits:
- No individual `free()` calls needed
- Cache-friendly (sequential allocation)
- Fast allocation (bump pointer)
- No memory leaks (entire arena freed at once)
- No use-after-free (arena outlives all users)

### 3.2 String Interning

All identifier strings are interned in a hash table:

```c
typedef struct {
    char **entries;
    size_t count;
    size_t capacity;
    zan_arena_t *arena;
} zan_intern_table_t;

const char *zan_intern(zan_intern_table_t *table, const char *str, size_t len);
```

Benefits:
- String comparison is pointer comparison (O(1))
- Reduced memory usage (each unique string stored once)
- All strings live in the arena (no individual allocation)

---

## 4. LLVM Integration

### 4.1 API Choice

Use the **LLVM C API** (`llvm-c/Core.h`, `llvm-c/Target.h`, etc.):
- Stable across LLVM versions
- No C++ dependency
- Sufficient for all IR generation needs

### 4.2 Initialization

```c
void zan_llvm_init(void) {
    LLVMInitializeAllTargetInfos();
    LLVMInitializeAllTargets();
    LLVMInitializeAllTargetMCs();
    LLVMInitializeAllAsmParsers();
    LLVMInitializeAllAsmPrinters();
}
```

### 4.3 Compilation Flow

```c
LLVMModuleRef module = LLVMModuleCreateWithName("zan_module");
LLVMBuilderRef builder = LLVMCreateBuilder();

/* ... generate IR ... */

/* Verify */
char *error = NULL;
LLVMVerifyModule(module, LLVMAbortProcessAction, &error);

/* Optimize */
LLVMPassManagerRef pm = LLVMCreatePassManager();
LLVMAddPromoteMemoryToRegisterPass(pm);
LLVMAddInstructionCombiningPass(pm);
LLVMAddReassociatePass(pm);
LLVMAddGVNPass(pm);
LLVMAddCFGSimplificationPass(pm);
LLVMRunPassManager(pm, module);

/* Emit object file */
LLVMTargetMachineEmitToFile(target_machine, module,
    output_path, LLVMObjectFile, &error);
```

### 4.4 Target Support

| Platform | Triple | Linker |
|----------|--------|--------|
| Windows x64 | `x86_64-pc-windows-msvc` | `link.exe` or `lld-link` |
| Linux x64 | `x86_64-unknown-linux-gnu` | `ld` or `lld` |
| macOS x64 | `x86_64-apple-darwin` | `ld64` |
| macOS ARM | `aarch64-apple-darwin` | `ld64` |
| Linux ARM | `aarch64-unknown-linux-gnu` | `ld` or `lld` |

---

## 5. Build System

### 5.1 Compiler Build

The compiler is built with CMake:

```cmake
cmake_minimum_required(VERSION 3.16)
project(zan VERSION 2.0.0 LANGUAGES C)
set(CMAKE_C_STANDARD 11)

find_package(LLVM REQUIRED CONFIG)
include_directories(${LLVM_INCLUDE_DIRS})

add_executable(zanc
    src/compiler/main.c
    src/compiler/zan.c
    src/compiler/lexer.c
    src/compiler/parser.c
    src/compiler/ast.c
    src/compiler/binder.c
    src/compiler/checker.c
    src/compiler/irgen.c
    src/compiler/diag.c
    src/compiler/driver.c
)

llvm_map_components_to_libnames(LLVM_LIBS core support ...)
target_link_libraries(zanc ${LLVM_LIBS})
```

### 5.2 Self-Hosting Plan

Long-term, the compiler will be rewritten in Zan itself (self-hosting):

1. **Phase 1**: C compiler (`zanc`) — current, bootstrapping
2. **Phase 2**: Zan compiler (`zan_compiler.zan`) compiled by `zanc`
3. **Phase 3**: Self-hosted — Zan compiler compiles itself

The self-hosting compiler will match C compiler output (gen2 == gen3 verification).

---

## 6. Testing Strategy

### 6.1 Test Categories

| Category | Directory | Description |
|----------|-----------|-------------|
| Lexer tests | `tests/lexer/` | Token output for each source snippet |
| Parser tests | `tests/parser/` | AST dump comparison |
| Type check tests | `tests/checker/` | Expected errors/warnings |
| Codegen tests | `tests/codegen/` | Compile + run, check output |
| Integration tests | `tests/integration/` | Full programs, end-to-end |
| Negative tests | `tests/errors/` | Programs that should fail |

### 6.2 Test Format

Each test is a `.zan` file with expected output in comments:

```csharp
// TEST: basic arithmetic
// EXPECT: 42
class Program {
    static void Main() {
        Console.WriteLine(40 + 2);
    }
}
```

### 6.3 Test Runner

```bash
zan test                    # Run all tests
zan test tests/lexer/       # Run lexer tests only
zan test --filter "arith"   # Run matching tests
```
