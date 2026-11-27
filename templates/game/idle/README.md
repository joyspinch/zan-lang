# 放置矿工 (Idle Miner) 游戏模板

基于 Zan 标准库纯 `Gui` + `SpriteBatch` 批量精灵渲染 + `Gui.Animation` 动画时间轴打造的高性能放置挂机游戏模板。

## 特性亮点

1. **零闲置开销 (0% CPU Idle)**：平时界面不产生无谓的刷新循环，只有在点击、金币滚动、面板动画进行时才按需触发硬件渲染。
2. **硬件级合批精灵渲染**：金币抛射与火花特效使用 `SpriteBatch` + 离屏动态烘焙（`Canvas.BakeSprite`），一次 DrawCall 绘制成百上千个平滑渐变粒子。
3. **弹性缓动与时间轴驱动**：核心矿石点击触发 `Tween` + `EaseType.BackOut` 弹性回弹与挤压形变，手感丰满。
4. **纯标准库组件架构**：基于 `Form`、`Panel`、`Button`、`Label`、`Progress`、`Modal` 构建工业风界面（统计面板与离线收益弹窗都是标准 `Modal`，不用 Canvas 自绘）。
5. **存档与离线收益**：金币、累计收益、点击数、各矿机等级连同落盘时间一起存档；每 20 秒、窗口失焦与退出时自动保存。下次启动按 `OfflineRewardCalculator` 结算离线收益（上限 8 小时），弹出"欢迎回来：+X 金币"。
6. **挂机不暂停**：放置游戏在失焦/最小化时照常产出，结算按真实流逝毫秒补算，定时器被系统降频也不少给钱。
7. **音效与统计**：程序化音效（`SfxBank` + `SfxPresets`）——敲矿石（很轻、限频）、升级成功（金币）、钱不够（错误）、累计开采每过 10 倍里程碑（升级音）、离线收益到账（成功）；`M` 键或顶栏按钮静音，静音偏好记在玩家档案。玩家档案（`GameProfile`）记录累计开采、手动开采次数、最高秒产、上线次数，在"采矿统计"里查看。
8. **新手引导与反馈**：首次游玩在左栏显示玩法提示；买不起时按钮变灰但仍可点，点了会报"金币不足：还差 X"。

## 操作

| 操作 | 作用 |
|---|---|
| 点击矿晶 / "立即开采" | 手动开采一次（产出 = 单次挖掘力） |
| 右侧"升级" | 花金币升级矿机，提高每秒产出 |
| `M` / 顶栏"音效"按钮 | 开关音效（记住偏好） |
| "采矿统计" | 打开统计面板（`Esc` 或"关闭"收起） |
| `Esc` | 关闭当前弹窗 |

放置游戏没有暂停与"结束"：关掉窗口即存档，下次打开接着挖。

## 存档位置

根目录为 `GameProfile.DataRoot()`：macOS `~/Library/Application Support/ZanGames`、
Windows `%LOCALAPPDATA%\ZanGames`、Linux `~/.local/share/ZanGames`；设置环境变量
`ZAN_GAME_DATA_DIR` 可改到任意目录（测试/便携版）。其下 `idle/` 里：

| 文件 | 内容 |
|---|---|
| `economy_slot0.json`（+ `.bak`） | 经济存档：金币、累计收益、点击数、矿机等级、`savedAtSec` |
| `idle_slot0.json`（+ `.bak`） | 玩家档案：最高秒产、累计开采/点击/上线次数、静音开关 |

想从头开始，删掉这个 `idle/` 目录即可。绝不写工作目录。

## 代码分工

机制与内容分开：**换个挂机游戏还成立**的部分归包，模板只留"这个矿场有什么"。

| 位置 | 内容 |
|---|---|
| `packages/Zan.Game.Idle/src/Game/Idle/`（`namespace Game.Idle`） | `IdleState` / `IdleBuilding`：点击产出、升级成本曲线（1.15 倍率）、每秒产出、定步结算；另有离线收益、钱包、飘字 |
| `src/Model/MineCatalog.zan` | 内容表：六座矿机的名字、描述、基础价、基础产出 |
| `src/main.zan` | 界面搭建、点击反馈、渲染、音效/静音、自动存档与离线结算 |
| `src/IdleSaveGame.zan` | 经济存档（`GameSaveStore`：原子写 + `.bak` 回退） |
| `src/ParticleSystem.zan` | 金币/火花的离屏烘焙 + `SpriteBatch` 合批绘制 |

回放：`src/IdleSim.zan` + `replays/bot.replay` 只跑经济本身，不碰存档、音效与时钟。

契约测试：`tests/conformance/idle_state.zan`（点击产出、成本曲线、每秒产出、定步结算的数值全锁死）。
