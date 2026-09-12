---
name: zan-development
description: The fixed workflow for writing or changing Zan code with the Zan SDK — orient, look the API up, edit, compile, run — plus the language facts and the exact commands. Use it for any Zan coding task.
---

# Writing Zan code

## 0. Orient (one call)

MCP connected: `zan_start_here` → layout, entry point, build/test commands,
which optional tools this installation actually has.
No MCP: read `AGENTS.md` and `docs/AI_ONBOARDING.md` at the SDK root.

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

Read `diagnostics[]` (file, line, column, message) and fix from the first one
down: later errors are usually fallout.

## 6. Run / test

`run_command("build/app")`, or the project's own test entry point if it has one
(`zan_start_here` reports it when present). In the SDK repo itself the tiers are
`scripts/test.ps1 smoke | standard | full` — take the smallest tier that covers
the change.

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
`style` attributes become `.zgen-N` class rules (class-level specificity,
not browser inline specificity). Idempotent pitfalls: Button routes "Click"
to its dedicated `Click` field — assert `((Button)b).Click.Count()`, not
`b.On.Click`; engine ledgers (inline x stepping ±3px, line-height rounding,
block strut font-size) apply — write explicit `line-height`/`font-size`
in fixtures. Full spec: `docs/HTML_UI.md` in the SDK.

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
a row with `cannot open file 'src/App.zform'` because the working directory had
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
* `.zform` + `--auto-stdlib` 偶发把 stdlib Chart 全家报 `undefined type 'App'`（与输入内容无关，绿红交替，重试至绿即可）。要和并行会话彻底隔离：`git archive HEAD stdlib` 解到快照目录 + 复制 `zanc.exe` 和 `build/zanrt_*.obj` 进去（stdlib 按 exe 相对定位），再把 `LOCALAPPDATA` 指到私有目录隔离生成器缓存。
