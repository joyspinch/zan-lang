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

### selfhost 侧现状

selfhost 前端（`src/selfhost/lexer.zan` + `parser.zan`）不重放 C 前端的逐条
报错：lexer 无诊断出口，parser 只有 16 个报错点且多为笼统的
`syntax error: unexpected ...`。其中与 C 侧**逐字同文案**的 6 处已带同号：
E0005 / E0006（整型溢出）、E0039 / E0040（join）、E0041（group）、
E0043（select）。其余 selfhost 报错没有 C 侧孪生，不编号，留待其语义所属
段次（名字/绑定、类型）落地时统一对齐。
