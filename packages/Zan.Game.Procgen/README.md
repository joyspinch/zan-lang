# Game.Procgen

`Game.Procgen` 是游戏关卡的**程序化生成**层：**WFC 波函数坍缩**拼图关卡、
按接口标签**关卡拼接**、**房间-走廊**地牢布局。

补的是 `Game.Kit` 那批静态资源（手画的关卡 JSON、预烘的图块集）缺的那一半——
关卡布局在运行期算出来，而不是每人手画一遍。

## 设计取舍

- **域不用位掩码**：每格一个 `List<int>`（候选瓦片 id）。Zan 的 `long`
  不支持 `<<`，做不了 64 位掩码；位掩码方案还会把**瓦片数封顶在 64**。
  列表求交是 O(n)，但 WFC 网格通常几百格、瓦片几十个，不是瓶颈。
- **规则是有向的**：`A` 的右边可以是 `B`，`B` 的右边不一定是 `A`
  ——单向箭头、单向门都需要这种不对称。
- **不对称规则在构造期就报错**：`A` 的 dir 允许 `B`，但 `B` 的反方向
  不允许 `A` —— 这种规则**永远无解**（传播时两边互相排除）。
  在 `Validate()` 里报错，比留到求解中途变成"莫名其妙的矛盾"好得多。
- **传播的判据是"交集比当前域小"**，不是"允许集比当前域小"。后者在
  "邻居域已缩到 1、允许集有 3"时会误判成"有变化"并重新入队，
  两个相邻格互相触发、**无限乒乓死循环**。
- **房间连通用最小生成树**（朴素 Prim）而不是"随机连最近"：后者产生
  长短不一的走廊和大量绕路。树化之后另挖**额外回路**（默认房间数 / 4）
  ——回路只加地板、不拆地板，连通性不会被破坏；纯树关卡让玩家频繁原路返回。
- **失败不抛异常**：拼接失败返回带 `error` 的结果（"这批素材拼不出来"
  是正常情况，调用方要决定换一批还是降级）；WFC 矛盾返回 `false`
  并给出 `conflict` / `conflictCell` 定位。
- **确定性**：随机源带种子且固定取 LCG 高位（低位周期极短，直接用会出现
  可见的带状分布）；坍缩平局取索引最小、传播顺序按格子索引，
  不依赖哈希遍历顺序。同 seed → 同一张图，可回放。

## 能力

| 能力 | 入口 |
|---|---|
| WFC 瓦片 | `WfcTile`、`WfcDir`、`WfcDirs`（取反 / 偏移） |
| WFC 规则 | `WfcTileSet`：`Add` / `Allow` / `Validate` / `PickWeighted` |
| WFC 求解 | `WfcSolver`：`Solve` / `TileAt` / `Result` / `DomainOf` / `CollapsedCount` |
| 关卡拼接 | `LevelStitcher.Chunk` + `LevelStitcher.StitchHorizontal` → `Stitched` |
| 房间地牢 | `RoomCorridorGenerator`：`Generate`（`Rect` 房间矩形） |
| 随机源 | `Rng`：`Unit` / `Range` / `RangeInt` |

## 用法

WFC 拼图关卡：

```zan
using System;
using System.Collections.Generic;
using Game.Procgen;

WfcTileSet ts = new WfcTileSet();
ts.Add(0, "wall", 1);
ts.Add(1, "floor", 6);
ts.Add(2, "grass", 2);

// 有向规则：floor 的右边可以是 floor / grass，grass 的右边只可以是 grass
List<int> fromFloor = new List<int>();
fromFloor.Add(1);
fromFloor.Add(2);
ts.Allow(1, 1, fromFloor);

List<int> fromGrass = new List<int>();
fromGrass.Add(2);
ts.Allow(2, 1, fromGrass);

ts.Validate();   // 不对称规则在这里就报错

WfcSolver solver = new WfcSolver(ts, 32, 32, 20241020L);
if (!solver.Solve()) {
    Console.WriteLine("contradiction: " + solver.conflict);
    return;
}
int[] grid = solver.Result();   // 行优先，未坍缩的格子是 -1
```

房间-走廊地牢：

```zan
RoomCorridorGenerator gen = new RoomCorridorGenerator(1, 2, 0);
int[] map = gen.Generate(64, 48, 8, 6, 400, 12345L);
// map 里：1 = 地板，2 = 墙，0 = 空
```

关卡拼接：

```zan
// exits[0] = 左出口标签，exits[1] = 右出口标签（null = 该方向没有接口）
LevelStitcher.Chunk a = new LevelStitcher.Chunk(
    "a", 4, 4, tilesA, new string[] { null, "road" });
LevelStitcher.Chunk b = new LevelStitcher.Chunk(
    "b", 6, 4, tilesB, new string[] { "road", null });

List<LevelStitcher.Chunk> chunks = new List<LevelStitcher.Chunk>();
chunks.Add(a);
chunks.Add(b);
LevelStitcher.Stitched st = LevelStitcher.StitchHorizontal(chunks);
if (st.error.Length > 0) {
    Console.WriteLine(st.error);
}
```

## 已知边界

- **WFC 没有回溯**：矛盾即失败，不会换一种坍缩顺序重试。想要"重试到成功"，
  得由调用方换 seed 再来一轮。
- **没有边界 / 接缝规则**：首行、末行不能用同一套瓦片（比如地牢入口那格
  要固定是门）。目前的做法是在规则里用权重近似，或由调用方后处理。
- **没有多样本生成**：同 seed 只能出一张图，想换一张得换 seed。
- **房间走廊只有 L 形**：先横后竖，不拐第二种形状；走廊宽度固定 1。
- **拼接只支持横向一行**：各块高度不等时下方补空（-1）。
- **补墙只贴地板**：`WallAround` 不做整圈外扩，地图边缘不会多出一圈
  无意义的墙。

## 验证

`tests/conformance/procgen_wfc.zan` 覆盖：WFC 求解成功与全部坍缩、
瓦片合法、邻接规则成立、同 seed 同结果 / 不同 seed 不同结果、
不对称规则拒绝、重复 id 拒绝、未知瓦片拒绝、拼接的标签匹配 / 宽度累加 /
顺序 / 拼不出报错 / 空输入报错、房间墙紧贴地板、确定性、
洪水填充验证全部地板连通、额外回路只加地板且保持连通。

## 放置

游戏包约定：`packages/Zan.Game.Procgen/src/Game/Procgen/*.zan`。
引擎级能力（物理 / 渲染 / ECS / 资源）走独立包并单独版本化，不进 stdlib。
