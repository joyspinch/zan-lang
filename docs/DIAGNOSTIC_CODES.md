# 诊断编号规范（A382）

每条编译诊断在原文案前加一个稳定编号前缀：`[E0032] expected '%s', got '%s'`。
编号是给工具（IDE/LSP/测试）与人对照文档用的稳定锚点；原文案保持不变。

## 规则（定版）

1. **格式**：`[E1234] ` / `[W1234] ` + 原文案。字母取**升级前**的原始级别
   （warning 即便被 `-Werror` 升级成 error，前缀仍是 W），编号四位零填充十进制。
2. **分段**：0001–0999 语法/词法；1000–1999 名字/绑定；2000–2999 类型/约束；
   3000–3999 代码生成/后端；4000–4999 互操作。新诊断取所在段内下一个未用号。
3. **一条语义一处一号**：编号键在**消息语义**上，不在触发站点上——同一文案
   的多个触发点共享一个号（如 E0033 保留字、E0056 声明过大）。
4. **编号只加不改**：已发布的号永不复用、永不换语义。文案可以改措辞，
   号不变；golden 期望值随文案一次性重生成。
5. **两侧一致**：C 宿主（`src/compiler`）与自举编译器（`src/selfhost`）对
   同一语义错误使用同一编号。selfhost 的 `Diag` 暂无 code 字段，编号内联在
   消息文本里；后续段次落地时再决定是否提字段。

## 机制

- C 侧：`zan_diag_emit_code(diag, level, loc, code, fmt, ...)`（`src/compiler/diag.c`）
  在格式化时拼前缀；`zan_diag_emit(...)` 是 code=0（不编号）的便捷入口。
- 捕获模式（LSP）：编号随 `zan_diag_entry_t.code` 结构化携带，前缀只是打印层。
- 前缀计入 512 字节消息缓冲，文案加号后仍受同一上限约束。
- 级别开关（A382②）：`--warn-level none|default|error` 与
  `--suppress=W0003,E0030`（按编号匹配，级别字母可省略）。
- 源内局部抑制（A382③）：`#pragma warning disable 0003, W0030` 从所在行起
  静默指定告警，`#pragma warning restore 0003` 恢复；disable 不带编号 = 通配
  全部告警。区间按 file+line 半开判定，只作用于 WARNING 级；未知 pragma
  静默吞行。自举侧 lexer 同样吞行（其诊断无编号过滤，语义本就分叉）。

## 0001–0999 语法/词法段（本段全部位于 C 前端）

### 词法（src/compiler/lexer.c）

| 号  | 级 | 消息 |
|-----|----|------|
| E0001 | E | conditional-compilation nesting too deep (max %d) |
| E0002 | E | #error %s |
| E0003 | W | #warning %s |
| E0004 | E | unterminated multi-line comment |
| E0005 | E | integer literal is too large for 'ulong' |
| E0006 | E | integer literal is too large for 'long' |
| E0007 | E | hexadecimal literal requires at least one digit after '0x' |
| E0008 | E | integer literal is too long |
| E0009 | E | binary literal requires at least one digit after '0b' |
| E0010 | E | octal literal requires digits 0-7 after '0o' |
| E0011 | E | octal literal requires at least one digit after '0o' |
| E0012 | E | exponent requires at least one digit after 'e' |
| E0013 | E | floating-point/decimal literal is too long |
| E0014 | E | '\x' escape requires at least one hex digit |
| E0015 | E | '\u' escape requires %d hex digits |
| E0016 | E | '\%c' escape value 0x%X is a surrogate code point |
| E0017 | E | invalid escape sequence '\%c' |
| E0018 | E | out of memory while lexing string literal |
| E0019 | E | unterminated string literal |
| E0020 | E | unterminated character literal |
| E0021 | E | unterminated interpolated string |
| E0022 | E | interpolated strings nested more than %d deep |
| E0023 | E | interpolation format specifier exceeds %d characters and was truncated |
| E0024 | E | too many closing quotes in raw string literal |
| E0025 | E | insufficient indentation for raw string literal |
| E0026 | E | unterminated raw interpolated string |
| E0027 | E | unterminated verbatim interpolated string |
| E0028 | E | unterminated verbatim string literal |
| E0029 | E | unterminated raw string literal |
| E0030 | W | unterminated #if/#ifdef (missing #endif) |
| E0031 | E | unexpected character '%c' (0x%02x) |

### 语法（src/compiler/parser.c）

| 号  | 级 | 消息 |
|-----|----|------|
| E0032 | E | expected '%s', got '%s' |
| E0033 | E | '%s' is a reserved word and cannot be used as a name here |
| E0034 | E | （动态文案：`parser_diag_expected_name` 兜底，如 expected variable name） |
| E0035 | E | expected type declaration (class, struct, interface, enum, or delegate) |
| E0036 | E | type nesting too deep (max %d) |
| E0037 | E | expected type |
| E0038 | E | array rank specifier is too deep (max 16) |
| E0039 | E | expected 'on' in join clause |
| E0040 | E | expected 'equals' in join clause |
| E0041 | E | expected 'by' in group clause |
| E0042 | E | a group clause without 'into' must be the final clause of the query |
| E0043 | E | expected 'select' in query expression |
| E0044 | E | nameof argument must be an identifier or member access |
| E0045 | E | unexpected token '%s' in expression |
| E0046 | E | expression nesting too deep (max %d) |
| E0047 | E | expression chain too long (max %d operators); split it into statements |
| E0048 | E | statement nesting too deep (max %d) |
| E0049 | E | unsupported tuple deconstruction form |
| E0050 | E | a declaration cannot be used as a single-statement body; enclose it in braces |
| E0051 | E | expected local function body |
| E0052 | E | iterator method '%.*s' must return IEnumerable<T> or List<T> |
| E0053 | E | expected class, struct, interface, or enum |
| E0054 | E | unexpected end of file inside namespace block; missing '}' |
| E0055 | E | unexpected '}' at top level |
| E0056 | E | declaration too large to lower: generated source exceeds %d bytes |
| E0057 | E | event field requires a non-generic delegate type |
| E0058 | E | event type '%.*s' is not a declared delegate |

表中 `%s`/`%d` 等为文案里的格式占位符，非文档转义。

### 名字/绑定（src/compiler/binder.c，段 1000–1999）

| 号  | 级 | 消息 |
|-----|----|------|
| E1000 | E | '%.*s' is declared both as a %s and as a method in '%.*s'; give one of them a different name |
| E1001 | E | duplicate '%.*s' in '%.*s': a %s with this name is already declared |
| E1002 | E | duplicate enum member '%.*s' in '%.*s' |
| E1003 | E | duplicate method '%.*s' in '%.*s': a method with the same parameter types is already declared |
| E1004 | E | duplicate constructor in '%.*s': a constructor with the same parameter types is already declared |
| E1005 | E | undefined type '%.*s' |
| E1006 | E | using the generic type '%.*s' requires %d type arguments (got %d) |
| E1007 | E | the non-generic type '%.*s' cannot be used with type arguments |
| E1008 | E | 'Func' requires at least one type argument (the return type) |
| E1009 | E | type declaration '%.*s' collides with a built-in type name |
| E1010 | E | duplicate type declaration '%.*s' |
| E1011 | E | cyclic inheritance involving '%.*s' |
| E1012 | E | invalid base type |
| E1013 | E | undefined base type '%.*s' |
| E1014 | E | cannot derive from sealed type '%.*s' |
| E1015 | E | type '%.*s' does not implement interface method '%.*s' |
| E1016 | E | destructors ('~%.*s()') are not supported: the ARC runtime has no destructor hook on release -- do deterministic cleanup explicitly (using statement or a Close/Dispose method) instead of relying on '~%.*s()' |
| E1017 | E | enum member '%.*s' value %lld does not fit in 'int' (enum members are 32-bit) |
| E1018 | E | enum member '%.*s' initializer must be an integer constant expression |

### 类型/约束（src/compiler/checker.c，段 2000–2999）

| 号  | 级 | 消息 |
|-----|----|------|
| E2000 | E | %s is not allowed in a [NoRuntime] method |
| E2001 | W | call in a [NoRuntime] method returns an ARC-managed value with an owned +1 reference, but [NoRuntime] emits no ARC; the +1 will not be released and will leak. Move the call out of the [NoRuntime] method or use a non-owning return |
| E2002 | E | '%.*s.%.*s' is private and cannot be accessed from '%.*s' |
| E2003 | E | '%.*s.%.*s' is protected and cannot be accessed from '%.*s' |
| E2004 | E | '%.*s.%.*s' is internal to '%.*s' and cannot be accessed from '%.*s' |
| E2005 | E | cannot assign to indexing expression of ReadOnlySpan |
| E2006 | E | property '%.*s' has no setter and cannot be assigned |
| E2007 | E | property '%.*s' has an init accessor and can only be assigned in an object initializer or a constructor of '%.*s' |
| E2008 | E | cannot assign to readonly field '%.*s' outside a constructor of '%.*s' |
| E2009 | W | closure captures 'this' while assigned to a member of 'this'; this creates a potential strong reference cycle under ARC |
| E2010 | E | '%s' must have a public parameterless constructor to satisfy 'new()' constraint for '%.*s' of '%s' |
| E2011 | E | unknown constraint '%.*s' in 'where %.*s : ...' for '%s' |
| E2012 | E | type '%s' does not satisfy constraint '%s' for type parameter '%.*s' of '%s' |
| E2013 | E | extern method '%.*s' must be static; a native function has no receiver, so drop 'extern' or mark it 'static' |
| E2014 | E | weak field '%.*s' inside value-type struct '%.*s' cannot be tracked safely; move it to a class or remove 'weak' |
| E2015 | E | weak field '%.*s' has type '%s'; weak requires an ARC-managed class or interface reference, so use a class/interface type or remove 'weak' |
| E2016 | E | the builtin type '%s' has no constructor -- use literals, casts or the conversion functions |
| E2017 | E | no constructor of '%s' accepts %d arguments |
| E2018 | E | no constructor of '%s' takes %d argument(s); a class that declares constructors has no implicit parameterless one |
| E2019 | E | cannot apply unary '%c' to '%s' |
| E2020 | E | operator cannot be applied to operands of type '%s' and '%s': mixed signed/unsigned comparison needs an explicit cast (C# rules) |
| E2021 | E | cannot convert 'void' to '%s' in %s: no implicit conversion |
| E2022 | E | narrowing conversion from '%s' to '%s' in %s needs an explicit cast (C# rules) |
| E2023 | E | cannot convert '%s' to '%s' in %s: a task started from a lambda/Action carries no result; declare 'Task' (not 'Task<T>') and await it or join with Task.WhenAll/JoinWait |
| E2024 | E | cannot convert '%s' to '%s' in %s: no implicit conversion |
| E2025 | E | cannot convert 'null' to '%s' in argument %d of '%.*s': null cannot be assigned to a value type |
| E2026 | E | type '%s' must have a public parameterless constructor to satisfy 'new()' constraint for type parameter '%.*s' of method '%.*s' |
| E2027 | E | type '%s' does not satisfy constraint '%s' for type parameter '%.*s' of method '%.*s' |
| E2028 | E | '%.*s.%.*s' takes at least %d argument%s, but %d given |
| E2029 | E | no overload of '%.*s.%.*s' takes %d argument%s |
| E2030 | E | '%.*s.%.*s' takes %d argument%s, but %d given |
| E2031 | E | '%.*s.%.*s' takes %d to %d arguments, but %d given |
| E2032 | E | cannot convert 'null' to '%s' in argument %d of '%s.%.*s': no implicit conversion |
| E2033 | E | cannot convert '%s' to '%s' in argument %d of '%s.%.*s': no implicit conversion |
| E2034 | E | cannot use a 'void' value in argument %d of '%.*s': a void call has no result |
| E2035 | E | cannot convert '%s' to '%s' in argument %d of '%.*s': no implicit conversion |
| E2036 | E | cannot use a 'void' value in argument %d of '%s' constructor: a void call has no result |
| E2037 | E | cannot convert '%s' to '%s' in argument %d of '%s' constructor: no implicit conversion |
| E2038 | E | no overload of constructor '%s' matches the given argument types |
| E2039 | E | '%.*s' is a method, not a value; call it as '%.*s()' |
| E2040 | E | cannot concatenate 'string' and '%s' -- convert the operand explicitly (e.g. call .ToStr()/read the field) instead of concatenating the reference |
| E2041 | E | cannot apply operator to 'decimal' and '%s' -- convert explicitly (e.g. Decimal.FromDouble) |
| E2042 | E | delegates are single-cast: '+='/'-=' combination is not supported -- assign directly (`d = h`) or subscribe on a multicast event type (`UiEvent`, `e += h`) |
| E2043 | E | cannot apply operator to '%s' and '%s' |
| E2044 | E | cannot compare '%s' and '%s' -- call .Get() on the binding to compare the bound value |
| E2045 | E | cannot compare '%s' and '%s' |
| E2046 | E | cannot compare '%s' and '%s' with a relational operator: no implicit numeric conversion |
| E2047 | E | cannot apply logical operator to '%s' and '%s': both operands must be bool |
| E2048 | E | cannot apply '?' '?' to '%s' and '%s' |
| E2049 | E | cannot apply unary '!' to '%s': the operand must be a bool |
| E2050 | E | nameof requires a single identifier or member access |
| E2051 | E | cannot use a 'void' value in argument %d: a void call has no result |
| E2052 | E | cannot interpolate '%s' -- convert the operand explicitly instead of formatting the reference |
| E2053 | E | '%.*s' indexer takes %d index%s, but %d given |
| E2054 | E | no '%.*s' indexer takes an index of type '%.*s' |
| E2055 | E | rank-%d array indexed with %d indices |
| E2056 | E | no '%.*s' indexer accepts this assignment |
| E2057 | E | cannot instantiate a type parameter '%s' |
| E2058 | E | cannot instantiate abstract class '%s' |
| E2059 | E | an array creation with an initializer requires constant dimensions |
| E2060 | E | array initializer has %d element%s but the dimensions describe %lld |
| E2061 | E | '%s' has no member '%.*s' |
| E2062 | E | type '%s' has no Add method for a collection initializer |
| E2063 | E | cannot apply '?:' operator to operands of type '%s' and '%s': neither converts to the other and they share no common type |
| E2064 | W | unreachable switch arm: previous discard arm '_' matches all remaining values |
| E2065 | E | duplicate pattern in switch expression |
| E2066 | E | duplicate discard arm '_' in switch expression |
| E2067 | W | switch expression does not handle enum member '%.*s' of '%.*s'; add missing case or '_' discard arm |
| E2068 | E | 'with' requires a record (class/struct) receiver, not '%s' |
| E2069 | E | '%s' has no field '%.*s' for 'with' |
| E2070 | E | cannot convert '%s' to 'object': value types cannot be boxed (Zan has no boxing facility) |
| E2071 | E | cannot convert '%s' to '%s': no such conversion |
| E2072 | W | returned closure captures 'this' and escapes method scope; ensure the caller does not retain it indefinitely to prevent ARC memory leaks |
| E2073 | E | assignment used as a condition (did you mean '=='? assignment (=) has no boolean value |
| E2074 | W | condition should be bool, got '%s' |
| E2075 | W | switch expression has non-switchable type '%s' |
| E2076 | W | unreachable switch case: previous default/catch-all case matches all remaining values |
| E2077 | E | duplicate case label in switch statement |
| E2078 | E | duplicate default label in switch statement |
| E2079 | W | switch statement does not handle enum member '%.*s' of '%.*s'; add missing case or default label |
| E2080 | W | switch statement on bool does not handle 'true' case; add missing case or default label |
| E2081 | W | switch statement on bool does not handle 'false' case; add missing case or default label |
| E2082 | E | cannot access member '%.*s' on 'null'; null reference dereference faults at runtime |
| E2083 | E | '%.*s' can return null; accessing '%.*s' on its result faults at runtime -- use '?.', or store it and check for null first |
| E2084 | E | type '%.*s.%.*s' %s '%.*s' |
| E2085 | E | '%.*s' %s '%.*s' |
| E2086 | E | enum type '%.*s' has no member '%.*s' |
| E2087 | E | cannot access member '%.*s' directly on nullable type '%s'; access via '.Value' or check '.HasValue' first |
| E2088 | E | '%.*s' is a static field of '%.*s'; qualify it with the type name ('%.*s.%.*s'), not an instance |
| E2089 | E | type '%s' has no member '%.*s' |
| E2090 | E | type '%s' has no method '%.*s' |
| E2091 | E | struct '%.*s' exceeds maximum nesting depth |
| E2092 | E | struct member '%.*s' of type '%.*s' causes a cycle in the struct layout (recursive struct has infinite size) |
| E2093 | E | non-abstract class '%.*s' does not implement abstract method '%.*s' inherited from '%.*s' |
| E2094 | E | an abstract method cannot be static: '%.*s' |
| E2095 | E | an abstract method cannot be async: '%.*s' |
| E2096 | E | class '%.*s' declares an abstract method and must be declared 'abstract' |
| E2097 | W | '%.*s' is declared 'virtual' but hides an inherited virtual with the same name and arity; calls through base-class references will run the base implementation -- use 'override' to replace it |

### selfhost 侧现状

selfhost 前端（`src/selfhost/lexer.zan` + `parser.zan`）不重放 C 前端的逐条
报错：lexer 无诊断出口，parser 只有 16 个报错点且多为笼统的
`syntax error: unexpected ...`。其中与 C 侧**逐字同文案**的 6 处已带同号：
E0005 / E0006（整型溢出）、E0039 / E0040（join）、E0041（group）、
E0043（select）；E1016（析构器不支持）同号。其余 selfhost 报错没有 C 侧
孪生，不编号，留待其语义所属段次（名字/绑定、类型）落地时统一对齐。
