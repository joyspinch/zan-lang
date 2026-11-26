---
name: zan-debugging
description: How to diagnose a failing Zan program — compiler diagnostics, ARC leak reports, zan-lsp and zan-dap over stdio. Use when a build fails, a program misbehaves, or memory looks wrong.
---

# Debugging Zan

Escalate in this order; most problems die at step 1 or 2.

## 1. Compiler diagnostics

```
zan_build_project        # MCP: {ok, exitCode, diagnostics[], raw}
zan_compile(content)       # MCP: same, for one snippet
build/zanc.exe <entry.zan> --auto-stdlib -o build/app
```

Fix the **first** diagnostic first — the rest are often its fallout. Each one
carries file, line, column and message.

For a machine-readable loop, compile with `--diag=json`: one JSON object per
diagnostic, and many carry `fixits[]` — byte-offset patches you can apply
directly (apply from the highest `offset` down). Typos in member/method/type
names come back as "did you mean 'X'?" with a replacement patch (only when one
candidate is clearly closest), a missing `;` as an insertion, `=` in
a condition as `==`, and a class-to-class reference cycle (W2111) as `weak `. When a diagnostic surprises you, cut the
construct down to a few lines and `zan_compile` that: a minimal snippet tells you
whether your assumption or the code is wrong.

Common ones:

* "can return null; accessing … faults at runtime" → store the value, check for
  null (or `?.`) before using it.
* unknown member → the API does not exist as written: `zan_api_search` it.
* "cannot concatenate 'string' and 'X'" → the operand is a class/enum, not a
  primitive: call `.ToStr()` (or read the field) and concatenate that. Same
  family: "cannot apply unary '~' to 'Enum'" → cast to int first
  (`(int)flag`), or use the type's own `UnsetFlag` helper.

### `out` parameters: the call site must spell `out`, and don't overload on them

* A method with an `out` parameter must be called as `f(a, out b)`. Calling
  `f(a, b)` passes by value: the write-back lands on a temporary and the
  program faults **at the call expression** — the callee's body never runs,
  so the stack trace points at the caller, not at the real culprit.
* **Do not overload a method on the presence of `out`.** Zan overload
  resolution with `out` parameters is unreliable. Give the variant a distinct
  name instead (`IsBlocked` / `IsBlockedWithUnit`) rather than relying on
  overload resolution to pick the right one.

## 2. Runtime behaviour

Build and run it: `run_command("build/app")`. Print state at the boundary you
suspect (`Console.WriteLine`) — cheap and decisive. Note that `Console.Error`
does not exist; use `Console.ErrorWriteLine(string)` for stderr. `Console.WriteLine`
writes to stdout, so it must not replace stderr in protocols or golden-output tests.

### 字符串、原始字节与可空元数据

- **检 NUL 要在解码前检查原始字节**：Zan 的 `Contains("\0")` 经 C 字符串搜索时，
  needle 在 NUL 处终止，等同搜索空串，会匹配正常文本；不能用它判断输入是否含零字节。
  对文件、归档或协议载荷，按实际数据长度检查 `byte[]`，再解码，防止 NUL 后缀被隐藏。
- **C 字符串与字节缓冲区分别验证边界**：按实际调用的实现和契约确认终止符、显式长度、
  编码及切片范围；字符串视图能承载字节，不代表后续搜索、拼接或发送仍保留全部字节。
  涉及两种表示互转时验证 NUL 前后内容与消费长度，不能把某个 API 的结论泛化到所有 API。
- **业务可空元数据先判空再取成员**：数据库或外部输入中的可选字符串先检查 `!= null`，
  再读 `.Length` 或调用字符串方法；仅在业务契约允许时把缺席映射为空串或默认值。
  历史行与 ORM 可空列可能保留 null，构造时赋过默认值也不能证明读回后非空。

### `await` 原生调用每次 10–40ms：先怀疑完成唤醒丢了，不是调用本身慢（rt_io_dns 实测，2026-11-23）

`await extern(...)` 走运行时阻塞卸载：工作线程跑完后经唤醒通道通知 reactor。唤醒
丢了不会挂死——20ms 扫描（`ZAN_IO_SWEEP_MS`）照样收割——只表现为每次往返稳定的
十几毫秒，极易被当成「SQLite/驱动慢」。判据：同一调用同步版只要微秒级。最小探针
`await getpid()`（或跨平台的 `sqlite3_libversion_number`）应在几十微秒内。pipe 型唤醒
通道（kqueue/select）读端登记轮询器、**写端**才可写；写读端 EBADF 静默失败。eventfd
（Linux）一个 fd 读写皆可，IOCP 用投递包，均不受这类错影响——只在 macOS/BSD 复现。

**代价补充：** 卸载本身不是免费的——旧运行时每个阻塞作业新建一个 OS 线程，SQLite
`QueryAsync` 又每行 await 一次 `sqlite3_step`，20 行一页 = 21 次建线程 + 线程往返。逐行
FFI 不要逐次卸载：首个（重活）卸载、后续按时间片内联，并让作业复用常驻线程。

### 多 worker 吞吐上不去、CPU 却很高：先看锁等待归因（Web 压测实测，2026-11-23）

`sample <pid> 5` 的「Sort by top of stack」里 `__psynch_mutexwait` 占大头时，把调用图里
`pthread_mutex_lock` 的上一帧按采样数聚合，直接得出是谁在抢锁。本仓库实测两例：只返回
常量指针的 `zan_rt_soft_scratch`、只读计数的 `zan_rt_defer_pending`，都在每次调用时拿全局
锁，而它们被编译器插进几乎所有函数/release 出口——单把锁吃掉 40% CPU。修法：已发布值
acquire 无锁读，写方锁内原子更新。另：对比基准务必用干净树（HEAD + 自己的改动）构建，
共享 build/zanc 会带上并行会话未提交的运行时改动，热点会被别人的半成品改写。
锁本身之外还要看锁里做什么：协程任务登记表在全局自旋锁下按帧指针**线性**查找，
O(活跃协程数) × 每次创建/销毁，并发越高越慢；自旋时间算在调用方自身采样里（如
`zan_task_child`），sample 里看不到 mutexwait，要看调用方 self time 异常高。
**并发翻倍吞吐反降**（list 32 连接 4.1k < 8 连接 9.7k）就是这类"代价随在途量涨"的信号：
编译器在**每个异步帧完成**处插 `zan_timer_cancel_delay`，它曾持全局定时器锁全堆扫描+重建；
连接池排队时每次门等挂 250 ms 看门狗，堆常驻上千条 → 每次帧完成扫上千条。改帧索引后
32 连接 4.1k → 8.3k。同轮第二例：`co_run` 每步末尾只要有非 worker 线程在 `co_ext_wait`
（服务端主线程终身在等根任务）就拿锁广播；改成只在刚跑的帧正是被等的帧时广播。排查法：
对 `zan_` 运行时入口按"每步/每帧完成/每次 await 都会调"过一遍，凡在锁里做 O(n) 或
无条件拿锁的都是嫌疑；用"并发 ×1 与 ×4"两档对比暴露。

**测试自身的数据竞争也会表现成"运行时偶发挂死/崩溃"**（2026-11-23 两例）：M:N 调度下
同一程序里的多个协程跑在不同 OS 线程上，`static int done; done = done + 1` 会丢更新
（`while (done < 4)` 永远转，tdengine_pool ~5% 挂死）；共享 `List.Add` 并发扩容会空指针
（db_pool CloseWakesWaiters ~15% SIGSEGV）。先在原运行时上复现（A/B 同样失败即非回归），
再加探针打印卡住时的计数——"服务端全部应答了、计数却差 1"即丢更新。修法：`AtomicInt`
计数、`lock (list) { ... }` 包住共享集合写。

### 多路复用尾延迟：请求/应答头里互打单调时钟戳拆单程，再看读循环是停泊还是积压（h2 实测，2026-11-23）

单连接多流 p99 远高于单流、服务端各阶段（派发→处理→发送）计时又都不慢时，延迟在两端
之间。同机两进程的 `Stopwatch.GetMicroseconds()` 是同一单调钟：客户端在请求头塞发出时刻、
服务端在应答头塞发出时刻，各自在对端收到处比对，直接得出慢的是哪个方向。再在读循环里记
"读到这一帧前等了多久"：等得久 = 停泊没被唤醒（查运行时唤醒）；几乎没等 = **串行读循环积压**，
帧在套接字里排队。本仓库实测是后者：`ReadExactAsync` 以 `FillTo(count)` 每次只收恰好 count
字节（帧头 9 字节、载荷各一次接收），加上客户端每个 DATA 帧回一个 WINDOW_UPDATE、服务端
HEADERS 与 DATA 分两次发——修后 8×8 p99 59 → 13 ms。探针只在一次性构建里加，别带进提交。

### sample 顶栈落在运行时小函数上：先怀疑锁，用计数探针量"每请求几次、几成争用"（协程注册表实测，2026-11-23）

`sample` 的 "Sort by top of stack" 里 `zan_task_child`/`zan_task_frame_config` 这类几行的
运行时函数占几百上千样本，不是函数慢，是内联进去的**忙等锁**在转。一次性构建里给锁加三个
原子计数（总获取、首次失败、自旋次数）+ 构造函数起线程每 3 s 打到 stderr，压一轮即得
"每请求获取次数 × 争用率"：本仓库全进程注册表锁每 HTTP 请求约 110 次、47% 争用——换更好
的锁也到顶，只能分片。分片时把跨条目的反向边换成"子帧化身戳"校验，让每个操作只持一把
分片锁；改后同探针复测争用 0.2%。探针别带进提交。

### 压测数字低：先剖析**客户端**——标准库客户端自己就可能是瓶颈（协议压测实测，2026-11-23）

压测端也是 Zan 写的、走标准库客户端时，数字同时量的是客户端。两例：WebSocket 回显只有
HTTP/1.1 的 1/3，`sample` 压测端进程，top-of-stack 是 `__read_nocancel`/`__open_nocancel`——
每帧 4 字节掩码经 `RandomNumberGenerator.Fill`，POSIX 实现每次 `fopen` + stdio 整块缓冲
`fread` + `fclose` /dev/urandom（修后 4.6k → 15.6k msg/s）；h2 客户端等流完成用
`Task.Delay(2)` 轮询，单流每请求 ≥2 ms 底（改等流的门后 3.0k → 15.7k rps）。判据：
单连接延迟远高于同机 HTTP/1.1 裸套接字客户端，且服务端 CPU 大半在 kevent 空等。排查：
`grep -n "Task.Delay" ` 查客户端等待循环；每消息调用链上找 open/read 系统调用。另：
`sample` 时别在同一 shell 里裸 `wait`（会连服务端一起等），只 `wait` 采样作业的 PID。

### 同步上下文 `await` 每次 ~1ms、有定时器在途时卡到定时器截止：worker 停泊收不到唤醒（rt_io_co_sched 实测，2026-11-23）

M:N 调度器 worker 空闲停泊必须阻塞在**可被 `co_notify` 叫醒**的原语上：POSIX 是本分片
kqueue/epoll（wake fd 登记其中），Windows 是 IOCP。曾在无登记 I/O 时退化成裸
`nanosleep(下一个定时器截止)`——主线程注入的任务要等定时器到期才被取走（Delay(60000)
在途时同步 await 像挂死 60s）；空闲判停的观察窗口用 `Sleep(1)` 同理。非 worker 线程
`zan_co_sched_run_until` 等 DONE 用 `Sleep(1)` 轮询，每个 await 固定 ~1ms，现改条件变量
（`co_run` 每步后有登记等待者才广播）。最小探针：主线程循环 100 次 await 两层 async，
起 0/4/32 个 `Task.Delay` 后台协程，应都在 ~1ms 量级且与定时器无关。另：任何"唤醒在途"
计数（曾有 `g_co_wake`）都会漂——改认领式（CAS `w->parked` 1→0 再写它的 wake fd）。
池重启计数 `g_co_pool_out` 不可在建线程后整体改写（先起的 worker 可能已 retire 扣过），
否则池子标着 live 却无线程：lldb 看 `g_co_pool_live=1 g_co_pool_out>0 g_inj_len>0` 且无 worker 线程即此症。

### `[DllImport]` 绑定变参 C 函数（fcntl/ioctl/open/printf…）：Apple arm64 上静默错参（套接字阻塞实测，2026-11-23）

Apple arm64 ABI 的变参一律走栈；按普通 N 参 extern 声明调用时实参放在寄存器里，被调方
从栈上读到垃圾。`fcntl(fd, F_SETFL, flags|O_NONBLOCK)` 照样返回 0，`O_NONBLOCK` 却没设上——
x86-64/Linux 上同样写法恰好能跑，所以只在 macOS/iOS arm64 现形。症状绕得很远：监听套接字
阻塞 accept 卡死一个调度 worker → 它负责的 IO 分片事件无人收割 → keep-alive 连接 1/4 的后续
请求永不应答。判据：调用后用无变参的 `F_GETFL` 读回校验。修法：变参函数只经运行时 C 包装
调用（如 `zan_io_set_nonblocking`），Zan 侧绝不直接 DllImport 变参 libc 函数。

### macOS 外部工具按 locale 选编码；用例写死平台命令（2026-11-24）

- `pbcopy`/`pbpaste` 按 locale 环境变量决定文本编码。ctest、launchd 和 IDE 起的子进程常常
  **没有 `LANG`**，于是工具退回 `__CF_USER_TEXT_ENCODING` 里的旧系统编码（中文系统是
  MacChineseSimp），UTF-8 正文在写-读往返里被改写或读回空串：`clipboard_roundtrip` 只在
  `SetText→GetText` 两步红。复现时**清掉 LANG** 再跑（`env -u LANG …`）；交互 shell 里设了
  UTF-8 locale 就复现不了。修法：库里调这类工具时显式带 `LC_ALL=en_US.UTF-8`（见
  `Clipboard.PasteboardLocale`），不要指望继承来的环境。
- 跨平台用例里写死 `cmd.exe /c …` 在 POSIX 上必然失败，看起来像 Process API 坏了
  （`res_ok=false`/`async_ok=false`）。先用 `/bin/sh -c` 写一个探针单跑同一 API：通了就是用例
  不可移植，用 `#if WINDOWS` 选平台 shell（`process_lifecycle_api` 的 `Shell()/ShellArgs()`），
  别改金样、也别在库里把 `cmd.exe` 翻译成 sh。
- `#if WINDOWS` 守卫要包住**所有**引用 Win32 extern 的私有辅助函数：辅助函数漏在守卫外，
  非 Windows 目标直接报 `E2084 has no method 'WinXxx'`，整个包在 macOS/Linux 编译不过。

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

### 跑可能挂死的 exe：必须带超时保护

- 前台 `& exe` 一旦撞上死循环就**永久挂住会话**，而且看不到任何输出
  （stdio 缓冲在进程被杀时全丢，连已经打印的 PASS 行都不剩）。
- Windows 上包一层超时保护再跑：`Start-Process -PassThru -RedirectStandardOutput
  <file>` → `WaitForExit(ms)` → 超时则 `Kill()`。这样挂死只花掉一次超时，
  而不是把整个会话拖死。
- 实例：WFC 求解器的传播循环判据写错（拿"允许集大小"当"是否收紧"的
  判据），相邻两格无限乒乓，exe 永不退出。换成"交集比当前域小"之后
  正常坍缩——先怀疑**循环的重入判据**，再怀疑算法本身。

### gdb 定位协程/异步崩溃：能拿到源级回溯，但硬件观察点会静默失效

- TDM-GCC 自带 gdb，对 `-g` 构建的 Zan 程序能给出**带 Zan 源文件行号的
  回溯**（`gdb -batch -ex run -ex bt --args app.exe`）——协程崩溃第一时
  间先拿回溯，别靠猜。
- **Windows gdb 的硬件观察点（`watch`/`awatch`）对新起线程的写入不可靠**：
  协程调度器 worker 线程晚于观察点创建时，写入不触发、也不报错——观察点
  "全程未命中"**不能**当作"没人写"的证据（本会话两次被它误导）。要对"谁
  改了这块内存"下结论，改用：断点抓好值 → 崩溃点/下次到达再读值对比。
- 汇编绕不出来时直接上 IR：`zanc --emit-ir > ir.ll`，按崩溃地址的指令
  （常量偏移、GEP 索引）回 IR 找对应语句——IR 里的类型/索引比反汇编直读
  可靠得多。实例（A549）：`eh.pending[7]` 数组被写死索引 11，越界 16 字节
  踩进相邻槽位的高 32 位，指针值呈"低 32 位不变、高 32 位变 1"的形状——
  这种"半截指针"直接指向**重叠写入**，是帧/槽位布局账目错了的指纹。

## 3. Memory / ARC

```
build/zanc.exe <entry.zan> --auto-stdlib --check-leaks -o build/app
build/app
```

At exit it reports objects still reachable and the `file:line:col` of the `new`
that allocated each one. An unbroken reference cycle is the usual cause — ARC
does not collect cycles.

The report then runs trial deletion over the leftovers and prints every
**orphan cycle** as a path to break — mark one field on it `weak`, unsubscribe
the handler, or Dispose:

```
cycle: Node@a.zan:5:35 .Next -> List<Node>@a.zan:3:57 [0] -> Node@a.zan:5:35
```

If the process ends through `Environment.Exit` (before `Main` returns), frames
and statics are still alive, so only orphan cycles count and the header says
`in unreachable cycles at exit`. A strict "nothing left" verdict needs a normal
return from `Main`.

Runtime guards are on by default (e.g. integer division by zero traps with a
source location); `--no-runtime-checks` turns them off, so keep them on while
debugging.

## 4. Semantic questions: `zan-lsp`

`build/zan-lsp.exe` speaks **LSP over stdio with `Content-Length`
framing**. Point your editor's language client at it (`command: zan-lsp`,
`transport: stdio`, language id `zan`) and use hover / go-to-definition /
references / completion instead of reading the standard library by hand.
It advertises: diagnostics (on open/change/save), completion, hover, signature
help, definition, references, rename, workspace and document symbols, code
actions, `workspace/executeCommand`.

There is no MCP wrapper around it: it is a protocol server, so it is the editor
(or a client you drive yourself) that talks to it.

## 5. Stepping: `zan-dap`

`build/zan-dap.exe` speaks **DAP over stdio with `Content-Length`
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

## GUI 程序挂死在事件泵里：先核对 exe 与 zan_gui.dll 的代际配对（chatview 回归实测）

- 症状：窗口创建成功且"已响应"，stdout 零输出、UiErrorLog 无异常
  （异常被 PumpSafe 吞进内存环），进程停在 `App.PumpGuarded` 内部。
- 先做一步：用**当前** `build/zan_gui.dll` 覆盖 exe 旁的
  `zan_gui.dll` 重跑。zanc 会把 `packages/Zan.Gui/src/Gui/drivers/<plat>/zan_gui.dll`
  （提交版二进制）暂存到 exe 旁；src/runtime 重编后若这批提交版驱动
  没跟着刷新，新编译的 exe 配旧驱动就挂在泵里——与用户代码、与
  stdlib/包布局全都无关（实测 HEAD 布局与拆包布局同样复现，
  换新 dll 双双 PASS）。
- 为什么：exe 的运行时对象与驱动 dll 必须同一代际（运行时帧布局/
  协程约定的改动会进 dll ABI）；ctest/conformance 编译产物旁边落的
  是 stdlib 里那份静态驱动，它是最容易过期的一环。

## 协议双端联调挂死：服务器先证伪、客户端文件级打点、警惕僵尸监听进程（协议驱动联调实测）

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

## 服务端 HTTP 全被 RST、访问日志无记录：先查控制端口被谁占着（Zan.Net master/worker 实测）

- 症状：master 打印 `Start success`、路由表有 N 条，但每个 HTTP 请求发出去
  就被 RST（"Connection was aborted"），**服务端访问日志里根本没有这次
  请求**；换成单 worker 模式一样失败。这时查业务代码没有意义——请求压根
  没进到应用层。
- 先看端口归属：控制端口默认是业务端口 + 10000。用
  `Get-NetTCPConnection -LocalPort <ctlPort>` 找出 Established 连接的
  `OwningProcess`，再 `Get-CimInstance Win32_Process -Filter "ProcessId=<pid>"`
  看是哪个 exe 占着——实测是**另一个项目**两天前残留的服务进程。
- 机制：master 认领控制端口前先探测一次，"连上了但应答不是本应用的
  PING/PONG 协议"**不是"空闲"**。旧实现把它判成空闲，于是用
  `SO_REUSEADDR` 再绑一次同一端口——Windows 允许与在监听的端口并存
  绑定，于是本应用的 worker 把 HELLO/CHAN 握手发给外来的占用者，
  master 永远等不到通道，master 受理的每个请求都被直接 RST。
- 定式：控制端口上"连得上但应答不是 PONG" = **被占** → 改用邻近端口，
  绝不复用（实现见 `packages/Zan.Net/src/System/Net/Worker.zan` 的
  `ProbeControlPort`；回归用例 `tests/conformance/ctl_port_foreign.zan`
  把这条语义钉死）。
- 这类故障没有别的证据面：服务端没有日志、客户端只见 RST，靠
  `ZAN_WORKER_DEBUG=1` 打 master/worker 交接轨迹（ctl accepted /
  ctl handshake / worker sent CHAN）才定位得到。
