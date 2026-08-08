# Zan Compiler Error Catalog

## Error Code Format

```
ZAN<category><number>
```

- Category 0: Syntax / Lexer
- Category 1: Memory / Lifetime
- Category 2: Concurrency
- Category 3: Type System
- Category 4: FFI / Interop
- Category 5: Name Resolution
- Category 6: Generics
- Category 7: Code Generation
- Category 8: Project / Build
- Category 9: Internal Compiler

---

## Category 0: Syntax Errors

| Code | Severity | Message | Example |
|------|----------|---------|---------|
| ZAN0001 | Error | Unexpected token `{tok}`, expected `{expected}` | `if x > 0 {` (missing `(`) |
| ZAN0002 | Error | Unterminated string literal | `"hello` |
| ZAN0003 | Error | Unterminated multi-line comment | `/* comment` |
| ZAN0004 | Error | Invalid escape sequence `{seq}` in string | `"\q"` |
| ZAN0005 | Error | Invalid numeric literal `{lit}` | `0x` (no digits) |
| ZAN0006 | Error | Expected expression | `int x = ;` |
| ZAN0007 | Error | Expected statement | `class { }` (missing name) |
| ZAN0008 | Error | Expected type | `var x: = 5;` |
| ZAN0009 | Error | Unexpected end of file | Missing closing `}` |
| ZAN0010 | Error | Invalid character `{ch}` | Non-ASCII in identifier (except Unicode letters) |
| ZAN0011 | Warning | Semicolon after closing brace is unnecessary | `} ;` |
| ZAN0012 | Error | Duplicate modifier `{mod}` | `public public void F()` |
| ZAN0013 | Error | Conflicting modifiers `{a}` and `{b}` | `public private void F()` |

---

## Category 1: Memory / Lifetime Errors

| Code | Severity | Message |
|------|----------|---------|
| ZAN1001 | Error | Use of uninitialized variable `{name}` |
| ZAN1002 | Error | Cannot use nullable `{type}` without null check |
| ZAN1003 | Warning | Potential use-after-free: weak reference `{name}` used without null check |
| ZAN1004 | Warning | Potential reference cycle: `{type_a}` and `{type_b}` hold strong references to each other |
| ZAN1005 | Warning | Buffer size mismatch: expected `{expected}` bytes, got `{actual}` |
| ZAN1006 | Error | Cannot take address of managed object outside `unsafe` block |
| ZAN1007 | Error | Cannot store reference type in `[repr("C")]` struct; use `nint` |
| ZAN1008 | Error | Unsafe operation requires `unsafe` block |
| ZAN1009 | Warning | Variable `{name}` is never used |
| ZAN1010 | Warning | Value assigned to `{name}` is never read |
| ZAN1011 | Error | Cannot return reference to local variable `{name}` |

---

## Category 2: Concurrency Errors

| Code | Severity | Message |
|------|----------|---------|
| ZAN2001 | Warning | Potential data race: mutable `{name}` shared between tasks without synchronization |
| ZAN2002 | Warning | Non-sendable type `{type}` captured by task closure |
| ZAN2003 | Warning | Potential deadlock: inconsistent lock ordering |
| ZAN2004 | Error | Cannot send on closed channel |
| ZAN2005 | Warning | Channel `{name}` is never closed (potential goroutine leak) |
| ZAN2006 | Warning | Task result is never awaited |

---

## Category 3: Type System Errors

| Code | Severity | Message |
|------|----------|---------|
| ZAN3001 | Error | Type mismatch: expected `{expected}`, got `{actual}` |
| ZAN3002 | Error | Cannot implicitly convert `{from}` to `{to}` |
| ZAN3003 | Warning | Lossy numeric conversion from `{from}` to `{to}` |
| ZAN3004 | Error | Operator `{op}` cannot be applied to `{type_a}` and `{type_b}` |
| ZAN3005 | Error | No matching overload for `{method}({arg_types})` |
| ZAN3006 | Error | Ambiguous overload for `{method}({arg_types})` |
| ZAN3007 | Error | Cannot access `{member}` on type `{type}` |
| ZAN3008 | Error | Property `{name}` has no setter |
| ZAN3009 | Error | Property `{name}` has no getter |
| ZAN3010 | Error | Cannot instantiate abstract class `{type}` |
| ZAN3011 | Error | Class `{type}` does not implement interface method `{method}` |
| ZAN3012 | Error | Cannot use `{feature}` outside of async context |
| ZAN3013 | Error | Return type mismatch: method returns `{expected}`, expression is `{actual}` |
| ZAN3014 | Error | Not all code paths return a value |
| ZAN3015 | Error | Cannot assign to immutable variable `{name}` (declared with `let`) |
| ZAN3016 | Error | Switch expression is not exhaustive |
| ZAN3017 | Warning | Implicit nullable-to-non-nullable conversion |
| ZAN3018 | Error | Cannot use `void` as a value type |
| ZAN3019 | Error | Circular type dependency: `{chain}` |

---

## Category 4: FFI / Interop Errors

| Code | Severity | Message |
|------|----------|---------|
| ZAN4001 | Warning | Missing null check on native return value |
| ZAN4002 | Warning | String lifetime may exceed native call duration |
| ZAN4003 | Error | Managed reference in `[repr("C")]` struct; use `nint` instead |
| ZAN4004 | Error | Cannot find native library `{name}` |
| ZAN4005 | Error | Cannot find entry point `{name}` in `{library}` |
| ZAN4006 | Warning | Callback delegate `{name}` may be collected while native code holds reference |
| ZAN4007 | Error | Incompatible parameter type for native call: `{zan_type}` cannot marshal to `{native_type}` |
| ZAN4008 | Warning | `[StructSize({n})]` assertion failed: actual size is `{actual}` |

---

## Category 5: Name Resolution Errors

| Code | Severity | Message |
|------|----------|---------|
| ZAN5001 | Error | Undeclared identifier `{name}` |
| ZAN5002 | Error | Duplicate declaration `{name}` in current scope |
| ZAN5003 | Error | Cannot access `{member}`: it is `{visibility}` |
| ZAN5004 | Error | Namespace `{name}` not found |
| ZAN5005 | Error | Circular using dependency: `{chain}` |
| ZAN5006 | Error | Ambiguous reference `{name}`: could be `{a}` or `{b}` |
| ZAN5007 | Warning | Using directive `{name}` is unused |
| ZAN5008 | Error | Type `{name}` is not defined in namespace `{namespace}` |

---

## Category 6: Generic Errors

| Code | Severity | Message |
|------|----------|---------|
| ZAN6001 | Error | Type `{type}` does not satisfy constraint `{constraint}` |
| ZAN6002 | Error | Wrong number of type arguments: expected `{expected}`, got `{actual}` |
| ZAN6003 | Error | Cannot infer type arguments for `{method}` |
| ZAN6004 | Error | Constraint conflict: `{a}` and `{b}` are incompatible |
| ZAN6005 | Error | Recursive generic constraint: `{chain}` |
| ZAN6006 | Warning | Generic type `{type}` is never used with a concrete type |

---

## Category 7: Code Generation Errors

| Code | Severity | Message |
|------|----------|---------|
| ZAN7001 | Error | LLVM error: `{message}` |
| ZAN7002 | Error | Unsupported target: `{target}` |
| ZAN7003 | Error | Linker error: `{message}` |
| ZAN7004 | Internal | IR generation failed for `{construct}` |

---

## Category 8: Project / Build Errors

| Code | Severity | Message |
|------|----------|---------|
| ZAN8001 | Error | Cannot find project file `project.zan` |
| ZAN8002 | Error | Invalid project configuration: `{detail}` |
| ZAN8003 | Error | Cannot find source file `{path}` |
| ZAN8004 | Error | Cannot find module `{module}` |
| ZAN8005 | Error | Circular module dependency: `{chain}` |
| ZAN8006 | Error | Dependency `{name}` version conflict |
| ZAN8007 | Warning | Unused dependency `{name}` |

---

## Category 9: Internal Compiler Errors

| Code | Severity | Message |
|------|----------|---------|
| ZAN9001 | ICE | Internal compiler error: `{message}` — please report this bug |
| ZAN9002 | ICE | Assertion failed: `{condition}` at `{location}` |
| ZAN9003 | ICE | Stack overflow in compiler |

ICE = Internal Compiler Error. These should never occur and indicate a compiler bug.

---

## Error Message Format

```
<file>:<line>:<col>: error ZAN3001: Type mismatch: expected 'int', got 'string'
    5 │     int x = "hello";
      │             ^^^^^^^
      │             this is type 'string'
  help: use Convert.ToInt32() to convert
```

**Requirements:**
- Show source location (file:line:col)
- Show the relevant source line with caret pointing to the error
- Include "help" suggestion when possible
- Color output: red for errors, yellow for warnings, cyan for help
