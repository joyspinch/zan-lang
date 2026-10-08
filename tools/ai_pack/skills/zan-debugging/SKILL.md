---
name: zan-debugging
description: How to diagnose a failing Zan program with the Zan SDK — compiler diagnostics, ARC leak reports, the language server (zan-lsp) and the debug adapter (zan-dap) over stdio. Use it when a build fails, a program misbehaves, or memory looks wrong.
---

# Debugging Zan

Escalate in this order; most problems die at step 1 or 2.

## 1. Compiler diagnostics

```
zan_build_project()        # MCP: {ok, exitCode, diagnostics[], raw}
zan_compile(content)       # MCP: same, for one snippet
<ZAN_SDK>/toolchain/zanc <entry.zan> --auto-stdlib -o build/app
```

Fix the **first** diagnostic first — the rest are often its fallout. Each one
carries file, line, column and message. When a diagnostic surprises you, cut the
construct down to a few lines and `zan_compile` that: a minimal snippet tells you
whether your assumption or the code is wrong.

Common ones:

* "can return null; accessing … faults at runtime" → store the value, check for
  null (or `?.`) before using it.
* unknown member → the API does not exist as written: `zan_api_search` it.

## 2. Runtime behaviour

Build and run it: `run_command("build/app")`. Print state at the boundary you
suspect (`Console.WriteLine`) — cheap and decisive. Note that `Console.Error`
does not exist; error text also goes through `Console.WriteLine`.

### Crashes with no symbolized stack (the `0xC0000005` ritual)

A native crash (Windows: `build/zan_crash.log` with register dump; `rcx` =
`DEAD0000...` means ARC use-after-free) may print only bare addresses — the
toolchain does not bundle `addr2line`/`objdump`. Do **not** do what one session
did: 4 blind compile-run-probe rounds (v3→v6) guessing at the culprit. Instead:

1. **Read the crash log first**: the register dump plus the faulting address
   often identifies the poison (`DEAD0000DEAD0000` = freed memory reused).
2. **Bisect with probe output, not rebuilds**: put `Console.WriteLine` markers
   between suspicious statements so one run localizes the fault, rather than
   one rebuild per hypothesis.
3. **Suspect type mismatches across `await`**: a stdlib method that returns
   `int` assigned to an object variable compiles silently (compiler defect —
   report it with a `zan_compile` snippet) and dereferences the integer as a
   pointer on first use. Check the real return type in the stdlib source
   before debugging anything downstream.

## 3. Memory / ARC

```
<ZAN_SDK>/toolchain/zanc <entry.zan> --auto-stdlib --check-leaks -o build/app
build/app
```

At exit it reports objects still reachable and the `file:line:col` of the `new`
that allocated each one. An unbroken reference cycle is the usual cause — ARC
does not collect cycles.

Runtime guards are on by default (e.g. integer division by zero traps with a
source location); `--no-runtime-checks` turns them off, so keep them on while
debugging.

## 4. Semantic questions: `zan-lsp`

`<ZAN_SDK>/toolchain/zan-lsp` speaks **LSP over stdio with `Content-Length`
framing**. Point your editor's language client at it (`command: zan-lsp`,
`transport: stdio`, language id `zan`) and use hover / go-to-definition /
references / completion instead of reading the standard library by hand.
It advertises: diagnostics (on open/change/save), completion, hover, signature
help, definition, references, rename, workspace and document symbols, code
actions, `workspace/executeCommand`.

There is no MCP wrapper around it: it is a protocol server, so it is the editor
(or a client you drive yourself) that talks to it.

## 5. Stepping: `zan-dap`

`<ZAN_SDK>/toolchain/zan-dap` speaks **DAP over stdio with `Content-Length`
framing**, and drives a real native session through gdb: the bundled
`toolchain/debugger/bin/gdb`, else `ZAN_GDB`, else a system gdb. If the bundle
omitted gdb (it is not always relocatable), set `ZAN_GDB` to a gdb you have.

Supported: `initialize`, `launch`/`attach`, `configurationDone`,
`setBreakpoints` (conditional, hit-count, logpoints), exception and function
breakpoints, `threads`, `stackTrace`, `scopes`, `variables`, `setVariable`,
`evaluate`, `exceptionInfo`, `continue`, `next`, `stepIn`, `stepOut`, `pause`,
`terminate`, `disconnect`.

VS Code launch config:

```jsonc
{ "type": "zan", "request": "launch", "name": "Debug Zan program",
  "program": "${workspaceFolder}/build/app.exe" }
```

Build with debug info (default build, i.e. no `--publish`) before stepping.

## Honesty rule

Report what you observed — the diagnostic text, the leak report, the variable
value. A hypothesis you did not check is a hypothesis, and must be labelled as
one.

## GUI 程序挂死在事件泵里：先核对 exe 与 zan_gui.dll 的代际配对（2026-09-30，chatview 回归实测）

- 症状：窗口创建成功且"已响应"，stdout 零输出、UiErrorLog 无异常
  （异常被 PumpSafe 吞进内存环），进程停在 `App.PumpGuarded()` 内部。
- 先做一步：用**当前** `build/zan_gui.dll` 覆盖 exe 旁的
  `zan_gui.dll` 重跑。zanc 会把 `stdlib/Gui/drivers/<plat>/zan_gui.dll`
  （提交版二进制）暂存到 exe 旁；src/runtime 重编后若这批提交版驱动
  没跟着刷新，新编译的 exe 配旧驱动就挂在泵里——与用户代码、与
  stdlib/包布局全都无关（2026-09-30 实测 HEAD 布局与拆包布局同样复现，
  换新 dll 双双 PASS）。
- 为什么：exe 的运行时对象与驱动 dll 必须同一代际（运行时帧布局/
  协程约定的改动会进 dll ABI）；ctest/conformance 编译产物旁边落的
  是 stdlib 里那份静态驱动，它是最容易过期的一环。

## 协议双端联调挂死：服务器先证伪、客户端文件级打点、警惕僵尸监听进程（2026-10-08，协议驱动联调实测）

- 两端都可疑时先用脚本语言原始字节客户端（如 Python `socket + struct`）
  把 mock 服务器整个跑通，再查 Zan 客户端——先证明服务器端正确，
  把问题域砍掉一半。
- 挂死时往 stdout 打点是靠不住的：`timeout` 杀进程时 stdio 缓冲全丢，
  连已经执行到的 PASS 行都不剩。进度用 `File.WriteAllText` 落盘——
  写完即在磁盘上，进程被杀也不丢；每步覆盖写正好留下「最后到达点」。
- Windows 上 kill 掉监听进程后必须 `netstat` 确认端口真的释放：
  `SO_REUSEADDR` 在 Windows 允许新进程与没死干净的旧进程同时 bind
  同一端口且不报错，客户端连接随机落到其中一个——表现为「客户端明明
  发了 N 字节、服务器却收不到」，两端日志互相矛盾，其实是两个服务器
  进程把日志混写进了同一文件。taskkill 之后核对监听消失再开测。
- 二进制协议的长度域必须回填实际写入量，不能拍脑袋估算：估算差一字节，
  服务器按声明的长度等一个永远不来的尾字节、客户端按实际发完等一个
  永远不来的回复——双端死锁且无任何报错。定式：长度域先 `put32(0)`
  占位，消息体写完后把总长（含长度域自身）回填 `buf[0..3]`
  （PG StartupMessage 实测：估算 79、实际 77，两字节之差=互等挂死）。
