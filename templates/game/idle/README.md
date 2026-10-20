# 放置矿工 (Idle Miner) 游戏模板

基于 Zan 标准库纯 `Gui` + `SpriteBatch` 批量精灵渲染 + `Gui.Animation` 动画时间轴打造的高性能放置挂机游戏模板。

## 特性亮点

1. **零闲置开销 (0% CPU Idle)**：平时界面不产生无谓的刷新循环，只有在点击、金币滚动、面板动画进行时才按需触发硬件渲染。
2. **硬件级合批精灵渲染**：金币抛射与火花特效使用 `SpriteBatch` + 离屏动态烘焙（`Canvas.BakeSprite`），一次 DrawCall 绘制成百上千个平滑渐变粒子。
3. **弹性缓动与时间轴驱动**：核心矿石点击触发 `Tween` + `EaseType.BackOut` 弹性回弹，统计弹窗平滑浮动，手感丰满。
4. **纯标准库组件架构**：基于 `Form`、`Panel`、`Button`、`Label`、`Progress` 快速构建企业级工业风界面。

## 代码分工

机制与内容分开：**换个挂机游戏还成立**的部分归包，模板只留"这个矿场有什么"。

| 位置 | 内容 |
|---|---|
| `packages/Zan.Game/src/Game/Idle/`（`namespace Game.Idle`） | `IdleState` / `IdleBuilding`：点击产出、升级成本曲线（1.15 倍率）、每秒产出、定步结算；另有离线收益、钱包、飘字 |
| `src/Model/MineCatalog.zan` | 内容表：六座矿机的名字、描述、基础价、基础产出 |
| `src/Program.zan` | 界面搭建、点击反馈、渲染 |
| `src/ParticleSystem.zan` | 金币/火花的离屏烘焙 + `SpriteBatch` 合批绘制 |

契约测试：`tests/conformance/idle_state.zan`（点击产出、成本曲线、每秒产出、定步结算的数值全锁死）。
