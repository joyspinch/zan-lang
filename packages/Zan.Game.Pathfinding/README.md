# Game.Pathfinding

`Game.Pathfinding` 是网格寻路层：加权 A*、跳跃点搜索（JPS）、视线拉直与
导航网格（漏斗平滑）。

它与仓库里已有的两处寻路是**互补**而不是重复：

- `Game.Board.GridPathfinder`：棋盘用的无权 BFS，按步数最短。
- `Game.Tactics.FlowField`：塔防用的流场，成千上万单位共享一个方向场。
- `Game.Pathfinding`：**加权**地图（地形代价）、连续坐标的单位、需要最短
  而非最快路径的场景。

## 能力

| 能力 | 入口 | 说明 |
|---|---|---|
| 网格 | `GridMap` | 通行 + 每格代价；`FromBlocked` / `FromWalkable` / `FromCosts` |
| 加权 A* | `AStar.Find(map, start, goal, options)` | 欧氏代价（对角 √2），八分距离启发式 |
| 跳跃点搜索 | `Jps.Find(map, start, goal)` | 均匀代价网格上比 A* 少一个数量级的展开节点 |
| 视线拉直 | `PathSmoother.Smooth(map, path)` | 去掉可以直连的中间点（严格不切角） |
| 导航网格 | `NavMesh.Build(map)` + `FindPath(start, goal)` | 凸矩形分解 + 门图 A* + 漏斗平滑折线 |
| 门面 | `Pathfinding.FindPath` / `FindPathJps` / `FindNavPath` | 常用组合的一行调用 |

## 用法

```zan
using System;
using Game.Pathfinding;

// 1 = 可通行，0 = 障碍
bool[] cells = new bool[10 * 10];
for (int i = 0; i < cells.Length; i = i + 1) { cells[i] = true; }
GridMap map = GridMap.FromWalkable(cells, 10, 10);

CellPath path = Pathfinding.FindPath(map, new GridPoint(0, 0), new GridPoint(9, 9));
Console.WriteLine(path.Count().ToString());

// 连续坐标的单位：导航网格路径
NavMesh mesh = NavMesh.Build(map);
List<Point2> polyline = Pathfinding.FindNavPath(mesh, new Point2(0.5, 0.5), new Point2(9.5, 9.5));
```

坐标约定：格子 `(x, y)` 覆盖世界矩形 `[x, x+1] × [y, y+1]`，中心是
`(x + 0.5, y + 0.5)`。

## 设计取舍

- **零反射、编译期定型**：网格是数组 + 整数下标，没有"按名字查格子"的
  运行时字典。
- **确定性**：开放表是自带的二叉堆，优先级相同时按次级键（h）再按入堆
  顺序打破平局；同样的地图与起终点必然给出同一条路径（回放 / 锁步的前提）。
- **JPS 只在均匀代价网格上成立**：非均匀代价时 `Jps.Find` 自动退回 A*——
  JPS 的剪枝规则在非均匀代价下不成立，硬用会给出次优路径。
- **JPS 不切角**：对角移动要求两个正交邻格都可通行，跳跃扫描同样遵守。
- **导航网格用贪心最大矩形分解**，不做过多边形裁剪：结果确定、实现简单，
  代价是矩形数比理论最优多（对寻路速度影响可忽略）。
- **漏斗的左右按行进方向定**：门序列围成的走廊里，漏斗算法给出拐点最少
  的折线；折线走在矩形边界上（贴墙），需要"单位有半径"时自行内缩。

## 已知边界

- 导航网格的矩形边界就是可行走边界，单位没有半径概念（贴墙走）。
- JPS 的跳跃扫描在最坏形状（狭长走廊）下可能比 A* 慢。
- 没有动态障碍（运行期改地图需要重建导航网格）。
- 没有多层（楼层）导航。

## 契约测试

`tests/conformance/pathfinding_grid.zan` 覆盖：A* 的代价与形状、绕墙、
加权地形、JPS 与 A* 的路径长度一致且展开节点更少、不可达、确定性、
视线拉直不穿墙、导航网格路径必经缺口。
