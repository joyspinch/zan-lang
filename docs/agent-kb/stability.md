# stability.md — 不闪退：异常边界、错误日志与长期运行

目标：**IDE 和用它做出来的程序，都不因为一处局部错误整体退出。** 一次坏帧、一个坏控件、
一个坏文件、一次失败的构建，都应该变成一条可读的错误记录 + 一个可继续的状态，
而不是一个消失的窗口。

## 1. 谁负责兜底（自下而上四层）

| 层 | 机制 | 位置 / 关键字 |
| --- | --- | --- |
| 原生硬崩（访问违例、堆损坏） | Windows unhandled-exception filter，写 `<exe目录>\zan_crash.log`（时间、模块+偏移、寄存器、回溯） | `src/runtime/rt_crash.h`，由 `rt_io.c` 与 `gui_runtime.c` 包含 |
| 一帧内抛出的 Zan 异常 | `App.SafeFrame(FrameBody)`：`BeginFrame → body → PresentFrame` 包在 try 里，异常记账后丢掉这一帧并请求全量重绘 | `stdlib/Gui/App.zan`，grep `SafeFrame` |
| 事件分发里抛出的异常 | `App.PumpSafe()`：`ProcessEvent()` 的受保护版本，异常不再终止进程 | 同上，grep `PumpSafe` |
| 界面错误记录 | `Gui.UiErrorLog`：内存环（最近 100 条，面板直接读）+ `<exe目录>\zan_ui_errors.log`；同一条错误按 1/2/4/8… 次落盘，避免坏帧每帧刷爆日志 | `stdlib/Gui/UiErrorLog.zan` |

写自己事件循环的应用（IDE、gallery、示例）有两种接法：

```zan
// 1) 便捷：整循环托管
app.RunLoop(() => { RenderPage(app); });

// 2) 已有复杂循环：只把绘制段包起来
app.SafeFrame(() => { RenderPage(app); });
```

IDE 走第二种：主循环里那段 `app.BeginFrame() … app.PresentFrame()` 整体包在 try 里，
`catch` 调 `app.NoteFrameError("ide-frame", ex.Message)` → 记录 → `CancelPartialFrame()`
→ `RequestRedraw()`，下一帧从干净状态重画（grep `ide-frame`）。

## 2. 已知的"闪退根因"类型（都要修根因，不要只加 try）

try/catch 只兜住**已经发生**的错误；能修的根因必须修掉，否则错误只是变成每帧一条日志。

| 症状 | 真正的根因 | 修法 |
| --- | --- | --- |
| 拖入某个控件立刻退出，日志只有 `exception unwind stack exhausted` | 编译器的 typed overload 只在当前类里找候选，派生类的 `Add(string)` 遮蔽了继承来的 `Add(T)`，于是自己调自己无限递归 | 沿继承链解析重载（`resolve_overload_typed`，`src/compiler/irgen_expr_core.c`）；回归：`tests/conformance/overload_inherited_arity.zan` |
| 读一个未赋值的字符串局部变量就 0xC0000005 | 未赋值的 `string` 局部是空指针，任何读取（`== ""`、`.Length`、拼接）都在解引用 null | 未赋值的字符串局部默认空串，回归：`tests/conformance/string_default_empty.zan` |
| 读一个没在构造函数里赋值的 `string` 字段就 0xC0000005 | **字段保留 null 作为"未设置"**（ORM 靠它把未设置的字符串列绑成 SQL NULL，见 `tests/conformance/mysql_async_nonblocking.zan`），所以字段不会自动变空串 | 构造函数里显式赋 `""`（或声明处 `= ""`）。这是规约，不是编译器会替你做的事 |
| 界面元素画不出来 / 尺寸为 0 | 缩放因子字段没初始化（`pvScale == 0`），所有几何乘 0 | 构造函数里给全部缩放/DPI 字段赋初值；任何"比例"字段的默认值必须是恒等值 |
| 运行时才崩，编译期没报 | 编译器把"读不存在的成员"静默折成 0 | 补诊断（`tests/diag/`），不要在调用侧绕开 |

**判断顺序**：先看 `zan_crash.log`（有记录 = 原生硬崩，看模块+偏移）→ 没有就看
`zan_ui_errors.log`（Zan 异常被兜住了）→ 都没有就用 `_scratch/` 最小探针复现
（见 [debugging-playbook.md](debugging-playbook.md)）。

## 3. 写代码时的稳定性规约

1. **任何"比例/缩放"字段在构造函数里赋恒等值**（`pvScale = 1000`、`dpiScale = 100`），
   不要依赖零初始化。
2. **写盘/上报/网络失败不得反噬调用方**：`UiErrorLog.Append` 自己 try 住，内存记录仍可用。
3. **不要用 try/catch 把异常降级成 `0` 或空值**——那是把"读不存在成员静默折 0"那类
   缺陷手工复刻一遍。捕获后必须记录（`NoteFrameError` / `UiErrorLog.Record`）。
4. **子进程隔离**：编译、测试、运行用户程序一律另起进程，退出码 + stdout/stderr 回收成
   结构化诊断；被测程序崩溃不影响 IDE。
5. **保留可恢复状态**：长任务（构建、批量修改）每步落一次检查点，异常后能重试/跳过/继续。
6. **每个新兜底都要有对应回归测试**，测"抛异常后进程还活着且错误被记下来"，
   而不只是"正常路径能跑"。

## 4. 发布前门禁（做不到就不算完成）

- `scripts\test.ps1 standard` 全绿（失败项必须解释清楚，不允许改期望值掩盖）。
- 编译器、IDE、gallery 三个构建都过：`cmake --build build`、`scripts\build_ide.ps1`、
  `scripts\build_gallery.ps1`。
- 目标 DPI 覆盖 100/125/150/175/200%，不针对某台显示器硬编码。
- 真实窗口里跑一遍主路径（不能只有"能编过"）。
- `zan_ui_errors.log` 在主路径跑完后应为空；有内容就是还有坏帧。
