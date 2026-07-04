# Zan Compiler Development Standards

## 1. Language & Tooling

### 1.1 Implementation Language

The compiler is written in **C11** (ISO/IEC 9899:2011):
- No C++ constructs
- No GCC/Clang extensions in headers (implementation files may use `__attribute__` for optimization)
- Must compile cleanly with MSVC, GCC, and Clang
- `-Wall -Wextra -Werror` (GCC/Clang) or `/W4 /WX` (MSVC)

### 1.2 Build System

- **CMake** 3.16+ (only build system)
- All source files listed explicitly in CMakeLists.txt (no globbing)
- Out-of-source builds only (`build/` directory)
- Support Debug and Release configurations

### 1.3 Dependencies

- **LLVM 17+** — only external dependency for the compiler
- Use LLVM C API exclusively (no C++ API)
- Standard C library only (no third-party C libraries in compiler)
- Runtime may use platform-specific APIs (Win32, POSIX)

---

## 2. Code Style

### 2.1 Naming Conventions

| Element | Convention | Example |
|---------|-----------|---------|
| Functions | `zan_module_verb_noun` | `zan_lexer_next_token` |
| Types (struct) | `zan_name_t` | `zan_token_t` |
| Types (enum) | `zan_name_kind_t` | `zan_token_kind_t` |
| Enum values | `ZAN_PREFIX_NAME` | `TK_INT_LIT` |
| Macros | `ZAN_UPPER_CASE` | `ZAN_ARENA_BLOCK_SIZE` |
| Local variables | `snake_case` | `token_count` |
| Parameters | `snake_case` | `source_text` |
| Global constants | `ZAN_UPPER_CASE` | `ZAN_MAX_ERRORS` |
| File-static functions | `module_verb_noun` (no `zan_` prefix) | `lexer_skip_whitespace` |
| File-static variables | `s_name` | `s_keyword_table` |

### 2.2 File Organization

```c
/* file_name.c — Brief description
 *
 * Longer description if needed.
 */

#include "own_header.h"     /* Own header first */

#include "zan.h"            /* Project headers */
#include "other_module.h"

#include <stdio.h>          /* System headers */
#include <stdlib.h>

/* ---- Section name ---- */

/* Constants */

/* Types (file-local) */

/* File-static variables */

/* File-static functions */

/* Public API functions */
```

### 2.3 Formatting

- **Indentation:** 4 spaces (no tabs)
- **Line length:** 100 characters maximum
- **Braces:** K&R style (opening brace on same line)
- **Pointer `*`:** attached to variable name: `int *ptr`, not `int* ptr`

```c
/* Function definition */
zan_token_t zan_lexer_next(zan_lexer_t *lex) {
    if (lex->pos >= lex->source_len) {
        return (zan_token_t){
            .kind = TK_EOF,
            .loc = lexer_loc(lex),
        };
    }

    char ch = lex->source[lex->pos];
    switch (ch) {
    case '(':
        return lexer_single(lex, TK_LPAREN);
    case ')':
        return lexer_single(lex, TK_RPAREN);
    default:
        break;
    }

    return lexer_error(lex, "unexpected character");
}
```

### 2.4 Comments

```c
/* Block comments for sections and complex explanations */

/* Short inline comments for non-obvious code */
int offset = (size + 7) & ~7;  /* align to 8 bytes */

/* No comments for obvious code — good naming is enough */
```

- No `//` comments (C11 allows them, but we use `/* */` for consistency)
- No comment noise ("increment i", "return result")
- Document WHY, not WHAT

### 2.5 Error Handling

```c
/* Pattern: return status code, out-parameter for result */
zan_status_t zan_parser_parse_expr(zan_parser_t *parser, zan_ast_node_t **out_node) {
    if (parser == NULL || out_node == NULL) {
        return ZAN_STATUS_INVALID_ARG;
    }

    *out_node = NULL;

    zan_token_t token = zan_lexer_next(parser->lexer);
    if (token.kind == TK_ERROR) {
        return ZAN_STATUS_PARSE_ERROR;
    }

    /* ... */
    *out_node = node;
    return ZAN_STATUS_OK;
}

/* Pattern: emit diagnostic and continue (error recovery) */
static void parser_error(zan_parser_t *parser, const char *message) {
    zan_diag_emit(parser->diag, DIAG_ERROR, parser->current.loc, message);
    parser->had_error = true;
    parser_sync(parser);  /* skip to next synchronization point */
}
```

---

## 3. Architecture Rules

### 3.1 Module Dependencies

```
main.c → driver.c → parser.c → lexer.c
                   → binder.c → ast.c
                   → checker.c → ast.c
                   → irgen.c → ast.c, LLVM
                   → diag.c

All modules → zan.h (common types)
```

**Rules:**
- No circular dependencies between modules
- Each module has a `.h` (public API) and `.c` (implementation)
- Module internals are `static` functions/variables
- Only `zan.h` types appear in public interfaces (no LLVM types in parser.h, etc.)

### 3.2 Memory Management

**In the compiler:**
- Use arena allocators for all AST/type/symbol allocations
- No individual `malloc`/`free` for compiler data structures
- LLVM objects use LLVM's own memory management
- Temporary buffers: stack-allocate when size is bounded, arena-allocate otherwise

**In the runtime:**
- ARC for reference types
- `malloc`/`free` for runtime internals (with tracking in debug mode)
- No global mutable state except the scheduler

### 3.3 No Global Mutable State in Compiler

```c
/* BAD */
static int g_error_count = 0;
void report_error(const char *msg) { g_error_count++; ... }

/* GOOD */
typedef struct {
    int error_count;
    int warning_count;
    int max_errors;
} zan_diag_ctx_t;

void zan_diag_emit(zan_diag_ctx_t *ctx, ...) { ctx->error_count++; ... }
```

All state lives in explicitly passed context structs. This enables:
- Thread-safe parallel compilation (future)
- Testing without global setup/teardown
- Clear ownership and lifetime

---

## 4. Testing Requirements

### 4.1 Test Coverage

| Module | Minimum Coverage | Notes |
|--------|-----------------|-------|
| Lexer | 95% | Every token type, edge cases |
| Parser | 90% | Every grammar production |
| Binder | 85% | Forward refs, scoping, overloads |
| Checker | 85% | Type errors, inference, generics |
| IRGen | 80% | Each construct + optimization |
| Runtime | 90% | ARC, collections, FFI |

### 4.2 Test Types

1. **Unit tests** — test individual functions
2. **Snapshot tests** — compare output (tokens, AST dump, IR) against golden files
3. **Integration tests** — compile and run `.zan` programs, check stdout
4. **Negative tests** — programs that should produce specific errors
5. **Stress tests** — large inputs, many generics, deep nesting
6. **Fuzz tests** — random input to find crashes (libFuzzer)

### 4.3 Test File Format

```csharp
// TEST: descriptive name
// EXPECT: expected stdout output
// ERROR: expected error message (for negative tests)
// SKIP: platform (if platform-specific)

class Program {
    static void Main() {
        Console.WriteLine("expected output");
    }
}
```

### 4.4 Running Tests

```bash
zan test                        # All tests
zan test tests/lexer/           # Specific directory
zan test --filter "generic"     # Name filter
zan test --verbose              # Show details
zan test --update-snapshots     # Update golden files
```

---

## 5. Git Workflow

### 5.1 Branch Strategy

- `main` — stable, always compiles, tests pass
- `dev` — integration branch for features
- `feature/xxx` — individual features
- `fix/xxx` — bug fixes
- `docs/xxx` — documentation only

### 5.2 Commit Messages

```
<type>: <short description>

<optional body>

Types:
  feat     — new language feature or compiler capability
  fix      — bug fix
  refactor — code restructuring without behavior change
  perf     — performance improvement
  test     — adding or improving tests
  docs     — documentation only
  build    — build system changes
  chore    — maintenance tasks
```

Examples:
```
feat: implement generic type monomorphization
fix: lexer handles multi-line string literals correctly
refactor: extract expression parsing into separate functions
perf: use hash map for keyword lookup (3x faster lexing)
test: add negative tests for type checker errors
```

### 5.3 Code Review Checklist

- [ ] Compiles without warnings on all platforms
- [ ] All tests pass
- [ ] New tests added for new functionality
- [ ] No global mutable state introduced
- [ ] Memory allocated from arenas (no raw malloc for compiler data)
- [ ] Public API documented in header file
- [ ] Error messages are actionable (show source location, suggest fix)
- [ ] No TODO/FIXME without linked issue

---

## 6. Performance Guidelines

### 6.1 Compiler Performance

- Lexing: > 100 MB/s throughput
- Parsing: > 50 MB/s throughput
- Full compilation (lex → parse → bind → check → codegen): > 10K lines/sec
- Incremental rebuild: < 1 second for single-file change (future)

### 6.2 Generated Code Performance

- Within 10% of equivalent C code for numeric computation
- ARC overhead < 5% for typical programs (vs manual memory management)
- Zero-cost abstractions: struct methods, generics over value types
- Comparable to Go for concurrent workloads

### 6.3 Profiling

```bash
zan build --time-report       # Show time per compilation phase
zan build --mem-report        # Show memory usage per phase
zan build --stats             # Show statistics (tokens, AST nodes, types, etc.)
```

---

## 7. Release Process

### 7.1 Version Numbering

Semantic versioning: `MAJOR.MINOR.PATCH`

- **MAJOR** — breaking language changes
- **MINOR** — new features, backward compatible
- **PATCH** — bug fixes only

### 7.2 Release Checklist

- [ ] All tests pass on Windows, Linux, macOS
- [ ] Changelog updated
- [ ] Version number updated in CMakeLists.txt
- [ ] Binary size within target (< 20MB with IDE, < 5MB compiler only)
- [ ] Performance benchmarks within targets
- [ ] Self-hosting verification passes (when applicable)
- [ ] Documentation updated for new features

