# Zan.Game.Debug

游戏调试工具：帧剖析覆盖层 + 实体检查器（纯 Zan，无原生依赖）。
`using Game.Debug;`

## 内容

| 文件 | 内容 |
|---|---|
| `Models/DebugEntityRow.zan` | 实体快照行（id / 标签 / 位置 / 尺寸 / 状态 / 组件数）+ 摘要与定点格式化 |
| `Models/DebugFieldRow.zan` | 字段行（字段名 + 值文本） |
| `Models/DebugComponentRow.zan` | 组件行（组件名 + 字段行 + 展开态） |
| `Models/DebugEntitySort.zan` | 排序键枚举（id / 标签 / 坐标 x / 组件数） |
| `Models/DebugOverlayKind.zan` | 覆盖层行种类（标题 / 文本 / 带条） |
| `Models/DebugOverlayRow.zan` | 覆盖层行（标签 / 数值 / 占比 / 是否超预算）+ 字符条呈现 |
| `EntityInspector.zan` | 实体检查器：灌快照、选中、过滤（id 前缀 / 标签 / 字段值）、确定性排序 |
| `ProfileOverlay.zan` | 帧剖析覆盖层：把 Profiler 的帧样本算成一行行能画的东西 |
| `DebugText.zan` | 文本呈现：行模型 → 多行文本（无 GUI 环境下的等价物） |
| `DebugPanel.zan` | Gui 面板：把行模型摆成控件（**唯一依赖 `Zan.Gui` 的文件**） |

## 数据与绘制分层

除 `DebugPanel.zan` 外**全部零 Gui 依赖**。检查器与覆盖层先算成"行"
（标签 / 数值 / 占比 / 是否超预算），绘制层只负责摆控件。

这样分的理由：覆盖层最该被测的恰恰是"哪一行被标成超预算、占比算对没有"
——这些**不需要像素**，可以在无头环境里逐条断言；反过来去比像素，断言既
脆又测不到逻辑。同理，`DebugText` 让同一份行模型在 stdout / 日志 / CI 里
有等价呈现（带条的行用字符条，占比信息不丢）。

## 典型用法

```zan
using Game.Debug;
using Game.Profiler;

// 帧剖析覆盖层：喂帧 → 算成行 → 摆上面板。
ProfileOverlay overlay = new ProfileOverlay(FrameBudget.Sixty());
// 每帧结束时：
overlay.Push(frameSample);
Control panel = DebugPanel.BuildOverlay(overlay.Build());
hudLayer.Add(panel);

// 实体检查器：引擎侧显式填快照（Zan 没有反射，字段拿不到）。
EntityInspector inspector = new EntityInspector();
inspector.SetEntities(entityRows);              // List<DebugEntityRow>
inspector.Select(42, componentRows);             // 选中实体 + 它的组件字段
List<DebugEntityRow> visible = inspector.Visible("enemy");
List<DebugComponentRow> comps = inspector.VisibleComponents("hp");
```

## 硬取舍

1. **不反射**。Zan 没有反射，引擎对象的字段拿不到——实体快照与组件字段由
   调用方显式填好后交给检查器。检查器负责的是"组织、过滤、搜索、排序、
   呈现"，不是"知道引擎里有什么"。
2. **排序是确定的**。主键相同时按 id 升序兜底；降序只翻转主键，兜底 id
   始终升序。调试工具的输出顺序不能飘——同一场景两次打开看到不同顺序，
   人就没法对比了。
3. **帧剖析数据不重做**。账单格式、统计与热点分析都在 `Zan.Game.Profiler`，
   本包只把它的报告算成行。
4. **占比语义分两种**。帧条的 `overBudget` 是"这一帧超了预算"；热点条是
   "这个分段**累计**烧掉的时间超过一帧预算"——单帧 3ms 不卡，每帧都烧
   3ms 才卡。
5. **过滤串命中 id 前缀与标签子串**（大小写不敏感）。id 用前缀而不是子串：
   "1" 不该命中 "21"，子串匹配会让实体一多就满屏都是"看起来相关"的行。
   组件过滤则连**字段值**一起命中——组件名是写死的，值才是变化的。

## 已知边界

- **不做内存精灵缓存可视化**：那要 `Zan.Game.Assets` 的缓存统计，是另一件事。
- **不做火焰图 / 采样器 / 硬件指标**：只分析显式插桩出来的分段。
- **不接 ECS 的类型化组件池**：`Zan.Game.Ecs` 的 `World` 没有"按实体枚举
  组件"的接口（且 Zan 无反射），检查器吃的是调用方填好的快照。
- 依赖 `Zan.Gui` 编译时会看到 `ZANPKG_MISSING` 警告（来自 `Zan.Gui`
  自身的无效 `using`，见 `TASKS.md`），只走 stderr，不影响构建。

## 消费者

- conformance `game_debug`（44 项断言，纯逻辑层）
- 线程安全：全部是无状态静态方法与调用方持有的实例，可并发调用。
