---
name: zan-development
description: The fixed workflow for writing or changing Zan code with the Zan SDK — orient, look the API up, edit, compile, run — plus the language facts and the exact commands. Use it for any Zan coding task.
---

# Writing Zan code

## 0. Orient (one call)

MCP connected: `zan_start_here` → layout, entry point, build/test commands,
which optional tools this installation actually has.
No MCP: read `AGENTS.md` and `docs/AI_ONBOARDING.md` at the SDK root (shipped from this pack).

## 1. Locate before you read

* `search_text(query)` for a symbol or string; `find_files(pattern)` for a name.
  Both answer with `path:line`, which is the address you edit at.
* `read_file(path, from_line, max_lines)` — the numbered window around that
  line, not the file. (`offset`/`limit` reads bytes, for binaries.)

Reading a whole tree "to understand the project" is what `zan_start_here` and
the symbol index exist to replace — and reading the same window twice is worse
still: a tool result stays true until you change it, so use the one you have
instead of asking again.

## 2. Look up every API you are about to call

```
zan_api_search("File.ReadAllText")   → static string ReadAllText(string path)
zan_api_search("Http")               → the surface of the HTTP client/server
zan_example()                        → catalog of shipped, build-verified programs
zan_example("server-mvc")            → files of one of them, verbatim
```

Copy the shape from an example; adapt names, not structure. Guessing a
signature that "should" exist is the top cause of a broken build here.

Without MCP the same index is a file: `knowledge/symbols.json` next to the SDK
(one JSON record per line: name, kind, file, line, sig) — grep it.

## 3. Language facts that decide whether it compiles

* Static types, C#-like syntax, **ARC** — no GC and no manual free.
* Static members are reached through the class: `Foo.Bar()`, including inside
  `Foo` itself.
* Nullability is enforced: `x.Get("k").Put(...)` is an error when `Get` may
  return null. Store it, check for null, then use it (or use `?.`).
* `await` only inside an `async` member; the scheduler is real, not cooperative
  sugar.
* Platform-specific code: `#if WINDOWS` / `#else`; native symbols via
  `[DllImport("crt", EntryPoint = "...")]`.
* Strings concatenate with `+`; `Convert.ToString(n)` for numbers.
* Ternary `cond ? a : b` and type tests compose: `x is T ? a : b` parses
  as the conditional (C# rule — `?` starts the true-arm when a top-level
  `:` closes the construct). The nullable marker still wins when no `:`
  follows: `x is int? i` tests against `int?`. **Unwrap patterns are not
  supported**: `maybe is int n` (int? -> int) does not compile — test
  `maybe is int?` instead. `as` on failure yields an empty string for
  value-shaped targets, not null: check `!= ""`. (2026-09-13: the parse
  ambiguity was fixed in zanc; the old advice to write `(x is T) ? a : b`
  is obsolete.)
* Never list attribute names with `*/` inside a `/* ... */` comment —
  `data-on-*/data-if` closes the comment mid-sentence and the rest becomes
  code (209 cascading errors). Separate with `、` or spaces.
* No C#-style collection initializers: `string[] xs = { "a", "b" };` fails
  to parse (`unexpected token '{'`). Build a `List<string>` with `Add`
  calls instead. (2026-10-07, test-probe compile)
* `new T[N]` is zero-initialized — a non-zero sentinel needs an explicit
  constructor pass. Every slot reads `0` right after allocation; if an
  incremental `Clear()` only resets slots it has recorded (dirty buckets,
  a used-slots list), the constructor — where nothing is recorded yet —
  must loop-initialize all N slots itself, or the first insert chains onto
  slot 0: self-loops, duplicated query hits, nearest-search hangs.
  (2026-10-07, spatial-hash probe hang)
* TLS client trust roots differ per platform (2026-10-08, MySQL/PG self-signed
  interop): Windows verifies via Crypt32 SSL chain policy against the system
  store; Linux auto-loads the distro CA bundle (`/etc/ssl/certs/
  ca-certificates.crt` and siblings); macOS loads the system bundle
  (`/etc/ssl/cert.pem` + Homebrew OpenSSL copies). Self-signed / private-CA
  servers should use an explicit anchor, not disabled verification: PG
  connection string `sslrootcert=`, `MySqlConnection.OpenSecureParamsAsync`
  sixth arg `sslCa` (PEM path; unreadable file fails explicitly).
  `HttpClient` has only a verify on/off switch and no CA hook yet — against
  self-signed servers its only option is disabling verification.

## 4. Edit

`edit_file(path, old, new)` (MCP) replaces an exact snippet in place — the
default, because it needs the snippet and not the file. `old` must occur once,
so extend it with neighbouring lines until it is unique; the call refuses a
fuzzy match rather than guessing. `write_file(path, content)` is for a new file
or a wholesale replacement.

Make the whole change in one call per site — edit, re-read, edit the next line
is how a five-minute fix becomes twenty. Match the surrounding style; prefer
extending an existing class over adding a parallel one.

## 5. Compile — every time, before any claim

```
zan_build_project()            # whole project, structured diagnostics
zan_compile(content)           # one snippet in an isolated dir
```

Direct:

```
<ZAN_SDK>/toolchain/zanc <entry.zan> --auto-stdlib -o build/app
<ZAN_SDK>/toolchain/zanc <entry.zan> --auto-stdlib --publish -o app   # release
<ZAN_SDK>/toolchain/zanc <entry.zan> --auto-stdlib --check-leaks -o build/app
```

**多文件项目必须枚举全部 .zan 源**（2026-09-25 实证）：`--auto-stdlib` 只自动
发现 stdlib/包命名空间，**不拉取同项目的兄弟 .zan**——单入口
`zanc src/main.zan ...` 报 `unresolved call 'X': X is not a known variable,
type, or namespace`（X 是项目内另一文件的类）时，先数源文件齐不齐：
`zanc $(find src -name "*.zan") --auto-stdlib ...`（模板 e2e 套件就是这么
枚举的）。别急着判编译器回归。

Read `diagnostics[]` (file, line, column, message) and fix from the first one
down: later errors are usually fallout.

## 6. Run / test

`run_command("build/app")`, or the project's own test entry point if it has one
(`zan_start_here` reports it when present). In the SDK repo itself the tiers are
`scripts/test.ps1 smoke | standard | full` — take the smallest tier that covers
the change.

**ctest green ≠ 当前工作区已验证**（2026-09-13 实证）：conformance 用例的
`run_case.cmake` 有 up-to-date check——STDLIB_STAMP 不变就复用旧 exe 直接
回放，工作区**未提交**的 stdlib 改动不碰 stamp，于是出现"ctest passed 但
同一源文件手编失败"的假绿。多会话并行的仓库里，凡结论依赖"刚改过的东西
过没过"，必须手编直跑（`build/zanc.exe <case>.zan --auto-stdlib -o
_scratch/x.exe && _scratch/x.exe`）当真值，ctest 只当台账。

**`System.Threading.Timer` 构造后不自启**（2026-09-22 实证）：`new Timer(50, cb)`
只是注册，必须再调 `t.Start()`，否则共享泵根本不投 tick——全程无任何报错，
依赖它的轮询/消息泵整个静默失效（gui-webapp 模板的桥消息泵就因此死过，
前端 RPC 与窗口控制全无响应）。构造后紧跟 Start，并实跑验证回调确实在触发。

## 6b. Writing windows in HTML (Gui P5)

Windows can be described in plain HTML + CSS instead of hand-built control
trees — use it whenever the user says "用网页/HTML 写界面" or the layout is
web-shaped. Two paths, same parser, pixel-identical geometry:

- **Runtime**: `Control root = app.LoadHtmlWith(html, handlers, baseDir);`
  (fragments are fine — a missing body gets an implicit one).
- **Compile time**: pass the `.html` file to zanc next to your `.zan`
  sources; it expands to `UiHtml.Build(handlers)` + `UiHtml.Css` (call
  `app.UseAppCss(UiHtml.Css)` first). The shipped exe carries no HTML text
  and no parser.

Protocols: containers map to `Element` (UA stylesheet supplies web defaults
— never write `display: block` by hand), `button`/`textarea`/`input`/`img`
map to real widgets, `select` is a placeholder box. Events:
`data-on-click="save"` wires the handler registered as
`handlers.Add("save", ...)`; unknown names silently no-op by design.
Handler args: `data-arg="apple"` on the same node passes the literal
`"apple"` to an `handlers.AddArg("save", (string a) => ...)` entry
(creation-time value snapshot — row identity goes through `data-bind`,
not the arg; template rows share the prototype's arg).
`style` attributes become `.zgen-N` class rules (class-level specificity,
not browser inline specificity). Idempotent pitfalls: Button routes "Click"
to its dedicated `Click` field — assert `((Button)b).Click.Count()`, not
`b.On.Click`; engine ledgers (inline x stepping ±3px, line-height rounding,
block strut font-size) apply — write explicit `line-height`/`font-size`
in fixtures. Lists and conditional visibility are declarative too
(P8): `<template data-for="items">` clones its element children per array
item (rows inserted right after the template; row binds scope to the item
first, so `<span data-bind="name">` shows `item.name` — Element's default
bind property is `text`), and `data-if="flag"` toggles `SetShown` by the
path's truthiness (null/false/0/"" are false). Both are **consumed only by
a model-hosting ChildWindow** (`SetRoot(tree, model)` + `Wire()`); without
a model they are inert (prototype stays hidden, conditions stay visible).
Nested templates are not supported v1.
Full spec: `docs/HTML_UI.md` in the SDK.

## 6c. Designer documents are .html (Gui P7a)

The visual window designer's storage format is HTML — a doc whose
`<body>` carries the bare `data-zan-design` marker. Every shipped
template and all IDE-internal forms ARE .html (the legacy .html compile
channel was removed in P8-4: zanc rejects .html input with a targeted
error; convert with `DesignerHtml.FromJsonDoc`, the canonical
.html→.html converter); the JSON doc model is an internal
representation (undo/redo snapshots, the JSON drawer, the LSP
feed) — don't hand-write it, and write new designs as .html.

- **Compile channel**: zanc sends a `body[data-zan-design]` .html to the
  same GenForm projection the old .html used (typed partial class); a plain .html
  goes to the GenHtml build-tree class instead. Keep the marker intact —
  it is the routing bit.
- **Round-trip is key-faithful** (tested): doc-level keys become body
  `data-<kebab>` attrs, `on<Event>` becomes `data-on-<kebab>` (Pascal
  restored on read), pass-through objects (`props`, `columns`) ride
  `data-x-<kebab>` as compact JSON. Bare attributes mean `true` on read.
- **Designer API**: `Designer.SaveHtml()` / `LoadHtmlText(text)`. Loading
  a non-design HTML imports it (tags fall back to kinds) — HTML has no
  "corrupt" form, so `loadError` is nearly unreachable for .html input.
- **Zan trap hit here**: `((char)c).ToString()` returns the code point
  ("M" -> "77") — capitalize via `char + string` concatenation instead
  (`(char)(c - 32) + rest`).

## 6d. Per-control inline CSS (Gui P7b)

Any control takes inline CSS declarations via `ctl.SetProp("style",
"background:#c00; padding:8px; border-radius:6px")` — one call applies them
immediately (visual keys win over class rules; colors/functions reuse the
full CSS parser). `GetProp`-driven docs and the HTML `style` attribute land
on the same channel. Designer fields carry the same thing as the `style`
key / element `style` attr, editable line-by-line in the Inspector's STYLE
section.

- **Design geometry owns layout**: for designer-placed fields the emitted
  `logW`/`Prefer`/`logPad` (from X/Y/W/H, span, dock rows) is re-applied
  every measure, so `width`/`height`/`x`/`y`/`dock`/`gap`/`pad` inside
  `style` only stick where the design is silent. Visual keys (background/
  color/border/border-radius/box-shadow/font-size/transition) always win
  over class rules. Don't fight this by re-applying style after geometry.

## 7. Evidence discipline (the anti-rework rules)

Two real projects (a 420-file gateway port, a 634-file WinForms port) lost days
to the loops below. These rules are what closed them.

**Success must leave an artifact on disk.** A claim of "the repro passed" or
"the server responded" is false until a log file with content, a screenshot, or
a marker file proves it. One project burned hours on 8 repro variants whose
logs were all empty — the "success" was assumed, then "fixes" were built on it.
Before you treat a run as evidence, check the log is non-empty; before you fix
based on a failure, make sure the failure is the one you think.

**Compile after every coherent unit — not in batches.** A 600-file port that
compiles only once per batch spends a whole unverified epoch per batch; when a
build breaks there, every edit since the last green build is suspect at once.
Compile per module/window and fix forward immediately. If a build is blocked by
a file you must not touch (concurrent session, frozen stdlib), surface the
blocker and stop changing that batch — do not "keep editing, compile later."

**One command template, kept correct.** In one session `zanc` failed 4 times in
a row with `cannot open file 'src/App.html'` because the working directory had
drifted to `build/`. Commands that compile your project are project assets:
keep the exact working form in the project's AGENTS.md/README and always paste
it, including the `cd`, instead of retyping it from memory. Same for tool
quirks: a `File has not been read yet` error is a full wasted round trip — read
the window first, then edit.

**Search the file, not the memory of the name.** 5 consecutive greps for a
class that turned out to be named differently is 5 lost rounds. `find_files`
for the file name first, read it, and take the API shapes you need from a
sibling file that already does the same thing.

**Claiming a compiler defect has a price tag.** If code compiles silently but
is wrong (e.g. an `int` returned from a stdlib call assigned to an object
variable, then dereferenced — crashes with a poison-value pointer), the defect
itself is in scope: reduce it to a minimal snippet with `zan_compile` and
report it. Do not spend session hours tiptoeing around an unreported defect;
each later session re-pays the same discovery cost.

## 8. Report

State the command you ran and what it printed. Separate "compiled", "ran" and
"not verified". Never present an unverified change as working.

## Traps

* Do not invent APIs — look them up (step 2). A stdlib method's real return
  type is part of the API: check it before assigning to anything but `var`
  (assigning an `int` to an object variable once crashed a port on its first
  request — silent at compile time, poison-value pointer at runtime).
* `HttpClient.GetAsync/PostAsync` 只返回响应体，**拿不到 HTTP 状态码**——
  把 403/429 映射成领域异常的 SDK 必须走 `SendAsync`（返回解析好的
  `HttpResponse`，statusCode/body 一次拿全）。不要拿 body 再
  `HttpResponse.Parse` 一次：GetAsync 返回的已经是剥掉头部的正文，
  二次解析 statusCode 恒 200、body 变空。
* `JsonValue.Get`/`PathGet` 可空性是编译期强制的：`x.Get(k) != null &&
  x.Get(k).AsInt()` 这种"调两次"写法直接编译错误，必须先存局部变量再判
  （每处一次 `Get` + null check）。
* `JsonValue.Put` 是**追加不去重**，`Get` 命中返回**第一个**同名键——
  更新已存在的键必须用 `Set`（替换或追加）。"整表重写后 Put 回对象"
  （如 extra["columns"] 数组重排）会留下重复键，之后所有 Get 都读到
  旧值，表现为"明明写进去了、读回来还是旧的"。Put 只用于确认首次
  创建的键。
* Steam 等 64 位 ID 的 JSON 约定是**字符串形态**（"76561197960287930"），
  且个别字段文档写数字、线上回字符串（如 AuthenticateUserTicket 的
  `result:"OK"`）——解析层两种形态都要接住。
* 需要本地 HTTP 假网关自测的 SDK（微信/京东/Steam 同款套路）：
  `HttpServer` 回放官方 JSON 形态 + `ExternalCallPolicy.Default()
  .AllowLocalHttp()` 放行 loopback。曾有的坑（stdlib 已修，旧工具链仍会
  踩）：TLS 客户端对明文服务器握手会永久挂死——`TlsStream.PumpInAsync`
  对 `Recv<0` 不退出循环重挂 `ReadReady`，而 shutdown 后 readiness 只有一
  次；给 SDK 留 `PlainHttpMode` 之类的明文开关是防御性设计。
* `HttpClient` 请求行的 `path` 会原样进报文：调用方可控的 path 里带
  CR/LF 就能把一行撕成多行走私第二个请求（与 `SetHeader` 的头注入同一
  族）。stdlib 已修（`BuildRequestHead` 拒 CR/LF/SP/NUL/DEL，下载通道
  同步把关）；自建 HTTP 客户端或旧工具链要自己校验。
* 并行会话共享工作树时，"测试+stdlib 成对"的修复批**必须核对 stdlib 侧
  文件真的进了提交**：实测某提交只带上了三个 conformance 测试而配套的
  stdlib 半（HttpFramer/CookieJar/HttpClient 防线）全部留在工作树，TASKS
  却记"已修"——`git log -S "<新增符号>"` 全历史查一遍 + `git show
  <commit>:<stdlib文件> | grep <符号>` 是 30 秒的事，漏了就是标准库
  裸奔一个版本周期。
* Do not hard-code hosts, ports, credentials or business limits: they belong in
  the project config (`config/app.json` for server projects), read at run time.
* Do not hand-draw GUI widgets: use the standard library's components
  (`zan_example("gui-window")`, `zan_example("gui-form-components")`).
* Do not leave probes in the project: `_scratch/` for throwaway work, `build/`
  for output.
* If the compiler itself looks wrong, reduce it to a minimal snippet with
  `zan_compile` and report the snippet — do not contort the code around it.
* 给 `Binding<T>` 属性赋值（`Label.Text = ...` 同族）时，右值**别用裸字段左值**（`someObj.field`）：会合成活绑定逐帧读源对象，源是本次调用里新建的临时就活不过返回，绑定的 target 悬空，下一帧 MeasureText 读坏串崩（gui-wechat 名片页点击路径 30-50% 概率崩实测）。**先快照进局部变量再赋**——局部 → 常量绑定；右值本就被持有（`data[i]`、`cur = c`）则安全。
* `out` 实参的目标是**实例字段**时（`Fill(out v)`），编译干净但字段没被写穿，后续读它 = 空指针崩。定式：先给字段赋值，再把字段当普通实参传。
* `TryGetValue(key, out v)` **未命中时不写 out 参数**——调用后 `v` 还是调用前的旧值（未初始化则是 null），拿它当"没找到"的信号必错。定式：调用前先赋哨兵值（`string v = "";`），命中与否用返回布尔判断，别用 out 值判空。
* 闭包（delegate/lambda）捕获方法的 **`out` 参数不回传**——捕获的是副本，delegate 里的累加/赋值调用方看不见，编译不报错（ZanDb `MinField` 首版在 `ScanRows` 回调里直接写 out 累计值，调用方永远拿到初值 0）。定式：delegate 里只操作局部变量，扫描结束后一次写回 out 参数。
* `Dictionary.Keys` 返回的是**内部 List 本体**，不是副本——对它原地排序/增删会把键与值的配对打乱（排序后 `keys[i]` 对应的值还是旧槽位的）。需要排序先拷贝到新 List 再排。
* `.html` 的设计名（或文件基名）**别叫 `App`**：与 stdlib `Gui.App` 同名时 nsresolve 会把 Chart 全家的 `App` 形参引用改坏（100 个 `undefined type 'App'`，100% 复现，TASKS A311）。模板 raw 编译要把 `{{NAME}}` 占位符换成真实项目名（IDE 建项目时自动替换+重命名）。要和并行会话彻底隔离：`git archive HEAD stdlib` 解到快照目录 + 复制 `zanc.exe` 和 `build/zanrt_*.obj` 进去（stdlib 按 exe 相对定位），再把 `LOCALAPPDATA` 指到私有目录隔离生成器缓存。
