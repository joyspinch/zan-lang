# Game.Profiler

帧剖析：把 prof 走廊逐帧打出来的**分段账单**变成能用的**帧预算分析**。
纯逻辑，无 GUI 依赖，无引擎依赖。

对标 LiteMonitor / LibreHardwareMonitor 的"性能走廊"那一层，但**只做帧剖析
这一件事**：不做硬件指标采集（要 OS 原生读数）、不做内存条可视化、不做
D3D/GPU 抓帧、不做采样器的火焰图 UI。

## 它解决什么

现在的做法是：逐段计时用**环境变量开关的 prof 走廊**（如 `XXX_PROF=1`），
从那一帧起连打 N 帧**分段账单到 stdout**。复现"首次操作卡"这类问题全靠它，
但账单只打给人看——没有格式契约、没法进 CI、没法回归。

本包给账单一个**稳定格式**，并把账单变成结论：

```
frames=5 budget=16.000ms
total=94.000ms mean=18.800 median=16.000 p90=30.000 p95=30.000 p99=30.000 max=30.000
over budget: 2/5 (40.0%)  stutter runs: 1 (longest 2)
untracked: 14.9% of total
hotspots:
  render  total=56.000ms share=59.6% median=9.000 p95=20.000 frames=5 calls=5
  update  total=24.000ms share=25.5% median=5.000 p95=6.000 frames=5 calls=5
over-budget attribution:
  render  total=29.000ms share=60.4% median=9.000 p95=20.000 frames=2
  update  total=11.000ms share=22.9% median=5.000 p95=5.000 frames=2
stutter runs:
  frames 2-3 count=2 worst=30.000ms excess=16.000ms
```

## 设计取舍

### 1. 账单格式是契约，写端与读端是同一份定义

```
prof frame=12 scope=update ms=3.210
prof frame=12 scope=render ms=9.870
prof frame=12 total=16.700
```

- 前缀固定 `prof `，后面空格分隔 `key=value`；
- `frame` 必填；一行要么是"某个分段"（`scope` + `ms`），要么是"整帧"（`total`），不能同时；
- 分段名限 `[A-Za-z0-9_.:-]`（账单是空格分隔的，名字带空格就解析不开）；
- **未知 key 忽略**——以后加 key 不用改旧解析器；
- 毫秒固定 3 位小数：账单与报告要能对得上，位数不能是"最短往返表示"。

采集端 `ProfCorridor` 与解析端 `ProfBillParser` 共用 `ProfBill` 这份定义，
所以"打出来的行"与"认得的行"不可能漂移。

### 2. 坏行跳过并计数，不让整份账单作废

账单是往 stdout 打的，里面混着游戏自己的日志是常态，某一行的数值写坏也常见。
解析器的策略：不是 `prof ` 开头的行**忽略**；是 `prof ` 开头但格式不对的
**跳过并计数**（`ProfParseResult.skipped` + `firstError`）；好行照常解析。

### 3. 数值解析自己写，不用 `Convert.ToDouble`

Zan 的 `Convert.ToDouble("abc")` **静默返回 0**（仓库已记过这个坑）。账单解析
不能用它——一个拼错的毫秒数会变成"0 毫秒"，把热点排序整个带偏，而账单恰恰是
"机器写、给人看"的诊断数据，**错的值比没有值更坏**。

`ProfNum.TryParse` 严格校验 `[+-]?digits[.digits][(e|E)[+-]?digits]`，失败一律
判失败（不抛异常，调用方按"跳过并计数"处理）。

### 4. 分位数用最近秩，不用插值

`p = ceil(q × n)` 的那个样本。插值出来的 p95 是"某个不存在的帧"的虚构耗时，
看报告的人会去找那一帧。最近秩给的是**真实出现过的某一帧**。

### 5. 热点按累计耗时排，不按单帧最慢排

单帧最慢的那一次常常是一次异常（首次进场的资源加载、GC），它不告诉你"每帧都在
花时间的是谁"。累计耗时才是每帧都要付的税。`median` 与 `p95` 一起看：median
低而 p95 高 = 偶发尖刺；两者都高 = 稳定的开销。

同耗时按名字升序——输出必须确定，不能靠字典遍历顺序。

### 6. 归因只看超预算的帧

整体热点常常是"稳定开销"，而真正让玩家觉得卡的是**超预算帧里的那个大头**。
两者常常不是同一个分段，所以 `OverBudgetAttribution` 单独给一份。

### 7. 未覆盖部分单独报出来

`untracked = total - 各分段之和`。这个数大就说明**有代码没插桩**——全部分段都
很快但整帧很慢，答案通常就在这个差值里。

### 8. 走廊关着时所有调用都是空操作

`GAME_PROF=1` 才开。关着的时候 `BeginFrame` / `BeginScope` / `EndScope` /
`EndFrame` 全是空操作——调用点不必写 `if (prof.Enabled)`，省得每个分段都写
一遍条件（写漏一个就插桩不一致，账单对不上）。

开着的代价是每个分段两次取单调钟，比被测的分段本身小得多。

## 能力表

| 能力 | 入口 |
|---|---|
| 账单格式（写） | `ProfBill.ScopeLine` / `ProfBill.TotalLine` / `ProfBill.IsScopeName` |
| 账单解析（读） | `ProfBillParser.Parse` → `ProfParseResult` |
| 整体统计 | `FrameAnalyzer.Stats` → `FrameStats`（mean / median / p90 / p95 / p99 / min / max / 超预算占比 / 卡顿连片） |
| 热点 | `FrameAnalyzer.Hotspots` → `List<Hotspot>` |
| 超预算归因 | `FrameAnalyzer.OverBudgetAttribution` |
| 卡顿连片 | `FrameAnalyzer.StutterRuns` → `List<StutterRun>` |
| 未覆盖占比 | `FrameAnalyzer.UntrackedShare` |
| 报告 | `ProfileReport.Analyze` → `ToText()` / `ToJson()` |
| 最近 N 帧 | `FrameRing`（满了覆盖最老的一帧） |
| 帧预算 | `FrameBudget.Sixty()` / `Thirty()` / `HundredTwenty()` / `new FrameBudget(ms)` |
| 采集 | `ProfCorridor`（环境变量门控） |
| 错误码 | `ProfError`（7 种）+ `ProfilerException`（`error` 字段 + `ErrorName()`） |

## 用法

采集端（游戏侧，插桩）：

```zan
using Game.Profiler;

ProfCorridor prof = new ProfCorridor("GAME_PROF");
// prof.Enabled 由环境变量决定；关着时下面这些调用都是空操作。

while (running) {
    prof.BeginFrame();
    prof.BeginScope("update");
    Update();
    prof.EndScope();
    prof.BeginScope("render");
    Render();
    prof.EndScope();
    prof.EndFrame();
}
```

解析端（离线分析）：

```zan
using Game.Profiler;

ProfParseResult res = ProfBillParser.Parse(billText);
if (res.skipped > 0) {
    Console.WriteLine("账单里有 " + res.skipped.ToString() + " 行坏了");
}
ProfileReport rep = ProfileReport.Analyze(res.frames, FrameBudget.Sixty(), 5);
Console.WriteLine(rep.ToText());
Console.WriteLine(rep.ToJson());
```

## 并发边界

**非线程安全**。采集与分析都在游戏主线程做：`ProfCorridor` 靠单调钟计时，
跨线程用会把两个线程的耗时混进同一帧。需要多线程采样时，每个线程各用一个
`ProfCorridor`，收工时把各自的账单分开解析。

`ProfParseResult` / `FrameSample` / `Hotspot` 等都是**值语义的数据载体**，
解析完可以自由传递；只有 `ProfCorridor` 有状态。

## 验证

- 判定器：`tests/conformance/game_profiler.zan`（+ `.out`）——合成账单全覆盖：
  严格数值、账单解析（坏行 / 杂音 / 前向兼容 / 乱序 / 合并 / 缺 total）、
  统计与分位数、热点与归因、环形缓冲、走廊门控与往返自解析、报告文本与 JSON、
  七种错误码各自可区分。
- 验证命令：
  `cmake -DZANC=build/zanc.exe -DSRC=tests/conformance/game_profiler.zan
  -DEXPECTED=tests/conformance/game_profiler.out -DOUT_EXE=_scratch/out/game_profiler.exe
  -P tests/run_case.cmake`

## 已知边界

- **不做分配热点**：运行期分配量统计在编译器/运行时侧缺一环（`TASKS.md` A464），
  本包拿不到"这一帧分配了多少字节"。`Zan.Bench` 同样不提供分配量列。
- **不做硬件指标**：CPU / 内存 / 温度要 OS 原生读数，本包是纯逻辑，不碰。
- **不做火焰图 UI**：只出文本与 JSON 报告，渲染交给调用方。
- **不做采样器**：只分析"显式插桩出来的分段"，不做无插桩的采样归因。
- **不依赖引擎**：不碰 `Zan.Game` 的任何类型，帧循环怎么转由调用方决定。

## 放置

包在 `packages/Zan.Game.Profiler/`，命名空间 `Game.Profiler`，无依赖
（只用 `System` / `System.Collections.Generic` / `System.Json`）。
游戏向增强，按 `docs/ECOSYSTEM_ROADMAP.md` §1.26 记录。
