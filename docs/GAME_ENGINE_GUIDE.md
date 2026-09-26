# Zan 游戏引擎全新架构与品类开发指南

本文档全面阐述 Zan 现代化游戏引擎（`packages/Zan.Game`）的设计哲学、架构分层、主流品类子系统、2.5D 纯帧素材管理与 2.5D+3D 混合渲染方案，以及工业级素材工具链。

---

## 一、 架构设计哲学与分层

针对过往游戏开发中“逻辑与绘制纠缠、性能瓶颈严重、品类支撑单一”的问题，全新 Zan 游戏引擎确立了三大核心底线：

1. **逻辑仿真与渲染管线绝对解耦**：
   - 游戏世界逻辑（寻路、碰撞、战斗、状态机）运行在确定性的逻辑时钟上，坐标为精确的浮点数或定长网格。
   - 渲染系统只充当世界状态的“观察者与投影器”，通过 Camera/Viewport 实施视锥剔除与坐标投影，绝不在 OnPaint/Render 内部驱动游戏逻辑。
2. **零 GC 与紧凑内存布局**：
   - 核心系统（粒子系统、飘字系统、战利品散落、定步帧命令桶）全部采用**预分配紧凑对象池**与**结构数组（SoA / 平行数组）**，杜绝运行时堆内存高频分配与碎片抖动。
3. **多品类专用基座引擎矩阵**：
   - 为传奇类 ARPG、红警/帝国/魔兽 RTS、独立塔防、策略卡牌、放置挂机五大主流品类提供量身定制的原生算法级子系统。

```
+-------------------------------------------------------------------------+
|                              游戏应用层                                  |
| (templates/game/legend, idle, snake, RTS/TD/Card/ARPG 独立游戏工程)     |
+-------------------------------------------------------------------------+
                                    |
                                    v
+-------------------------------------------------------------------------+
|                  Zan 游戏基座引擎 (packages/Zan.Game)                     |
|  +--------------------+  +---------------------+  +------------------+  |
|  |     Game.Core      |  |    Game.Graphics    |  |     Game.Net     |  |
|  | Scene/SceneManager |  | 5Dir SpriteAnim     |  | LockstepManager  |  |
|  | GameViewport/Camera|  | PaperdollRenderer   |  | FrameBucket      |  |
|  | SpatialHash2D      |  | Mesh3DProjection    |  | PlayerCommand    |  |
|  +--------------------+  +---------------------+  +------------------+  |
|  +--------------------+  +---------------------+  +------------------+  |
|  |     Game.Arpg      |  |      Game.Rts       |  |    Game.Idle     |  |
|  | LootScatter 爆装   |  | FlowField 流场寻路  |  | FloatingTextMgr  |  |
|  | YSortLayer 遮挡深度|  | UnitFormation 编队  |  | SquashAndStretch |  |
|  | ArpgCombatSystem   |  | FogOfWar 视野迷雾   |  | OfflineRewardCalc|  |
|  +--------------------+  +---------------------+  +------------------+  |
+-------------------------------------------------------------------------+
                                    |
                                    v
+-------------------------------------------------------------------------+
|                     底层驱动与渲染 (Gui / Canvas / GL)                  |
+-------------------------------------------------------------------------+
```

---

## 二、 五大主流品类子系统

### 1. 经典 ARPG / 传奇类 (`Game.Arpg`)
- **YSortLayer**：等轴测视角下的动态深度排序，以实体的地表接触点 $Y$ 轴坐标为基准建立插入排序，彻底消除角色、怪物、地物之间的遮挡穿模。
- **LootScatter**：经典“怪物大爆”物理散落系统。模拟多件装备与金币向四周 360° 抛射，并在重力加速度下产生多次真实弹性跳跃轨迹，落地后触发品阶冲天光柱。
- **ArpgCombatSystem**：精确扇形、矩形和环形攻击判定与数值命中结算。

### 2. 即时战略 RTS（红警 / 帝国 / 魔兽） (`Game.Rts`)
- **FlowField**：基于 Dijkstra 与梯度向量场的万人流场寻路系统。当数百甚至数千个作战单位涌向同一目标或集结点时，只需单次计算目标热力场，所有单位采样向量即可前行，计算开销从 $O(N \cdot \text{A}^*)$ 直降为 $O(1)$。
- **UnitFormation**：方阵、横排、楔形、环形智能编队几何算子，自动保持行军阵型。
- **FogOfWar**：分层战争迷雾（未探索黑雾、已探索迷雾、实时视野透亮），支持圆形视距与障碍遮挡。

### 3. 塔防与网格策略 (`Game.Td`)
- **TdGridMap & TowerPathFinder**：动态迷宫造路与阻塞检测，确保怪兽路径不被死封，支持实时动态重规划。
- **TowerTargeting**：包含 First（首位）、Last（末位）、Strongest（最高血量）、Weakest（残血）、Closest（最近）等多维度智能索敌算法。

### 4. 策略卡牌与回合对决 (`Game.Cards`)
- **CardDeck & DiscardPile**：带确定性洗牌算法的牌库与弃牌堆管理。
- **HandArcLayout**：手牌扇形曲率与悬停上浮展开算法，自适应根据手牌张数计算弧度与偏转角。
- **CardActionQueue**：事件驱动的效果结算链条，支持打断、反击与亡语触发。

### 5. 放置与挂机休闲 (`Game.Idle`)
- **FloatingTextManager**：零 GC 对象池飘字系统，支持暴击金黄膨胀、伤害跳字与金币喷涌，单屏并发数百跳字毫无卡顿。
- **SquashAndStretch**：弹簧阻尼物理振荡器，为按钮点击和怪物受击赋予富有弹性的果冻打击形变感。
- **OfflineRewardCalculator**：离线挂机收益截断计算与人性化时长格式化。

---

## 三、 2.5D 精品素材爆炸解决方案

在传统的 8 方向 2.5D ARPG（如传奇、暗黑）中，每个角色拥有 8 个方向、多种装备状态与十几种动作，如果直接为每个状态画满 8 方向序列帧，素材量呈爆炸级增长（$8 \times 动作 \times 装备$），造成巨大的美术工期与显存包体浪费。

### 1. 5 方向对称镜像与 GPU 零开销翻转 (`SpriteAnimation`)
- **原理**：人体和怪物在左右方向具有极高对称性。只需绘制 **正南(0)、东南(1)、正东(2)、东北(3)、正北(4)** 共 5 个方向的序列帧。
- **映射规则**：
  - 西南(SW) $\to$ 镜像复用 东南(SE)
  - 正西(W) $\to$ 镜像复用 正东(E)
  - 西北(NW) $\to$ 镜像复用 东北(NE)
- **显存与工作量节省**：直接削减 **37.5%** 的美术绘制工期与贴图内存占用！
- **GPU 零开销翻转**：在 Quad/SpriteBatch 提交顶点时，只需将纹理水平坐标交换：
  $$u_0 \leftrightarrow u_1$$
  CPU/GPU 无额外翻转缓冲区与内存拷贝。

### 2. Paperdoll 纸娃娃多层部件与防穿模动态层深 (`PaperdollRenderer`)
- **多层解耦**：将角色解构为 `body`（身体）、`cloth`（衣服）、`weapon`（武器）、`wing`（翅膀）等分层。
- **主从帧同步**：由单一 `masterAnimator` 统一驱动各层时间轴与帧序列，杜绝各部件动作帧率漂移脱节。
- **8方向防穿模层深重排**：
  各部件在不同方向下的遮挡关系截然不同。系统为每个部件配置 8 个方向的深度偏移矩阵 `orderPerDir[8]`：
  - **面朝正南（正面）**：武器需在最外层（$+10$），翅膀在身体背面（$-20$）；
  - **面朝正北（背面）**：武器被身体遮挡（$-30$），翅膀完全覆盖在后背最外层（$+30$）；
  系统每帧根据当前朝向执行轻量插入排序，彻底消灭装备前后穿模瑕疵。

---

## 四、 2.5D 瓦片背景 + 3D 角色模型混合方案 (`Mesh3DProjection`)

对于需要 360° 无级流畅转向、无限动作融合但希望保留 2.5D 经典手绘/瓦片地图质感的游戏，引擎提供了无缝混合方案：

1. **逻辑与视口标定（PPU - Pixels Per Unit）**：
   - 游戏世界逻辑依然运行在 2D 平面（$X, Y$）与寻路网格中。
   - 通过固定的像素比例（如 64px = 1米），建立 2D 屏幕坐标与 3D 世界空间的刚体映射：
     $$X_{3D} = \frac{X_{2D}}{\text{PPU}}, \quad Z_{3D} = \frac{Y_{2D}}{\text{PPU}}, \quad Y_{3D} = \frac{\text{Height}_{2D}}{\text{PPU}}$$
2. **360° 连续平滑旋转（Yaw Smoothing）**：
   - 告别 2D 序列帧只能固定 8 个方向切帧的顿挫感，3D 模型支持任意角度连续旋转，内置角速度阻尼插值（Shortest-Arc Lerp）。
3. **Cross-Fade 动作平滑过渡**：
   - 内置双动作混合轨道（Track A 与 Track B），在切换动作（如 Run $\to$ Attack）时，以权重 $0.0 \to 1.0$ 平滑过渡，彻底告别动作生硬瞬切。
4. **骨骼挂点（Bone Socket）**：
   - 追踪角色骨骼关键点（如 `weapon_r` 右手、`wing_mount` 背部挂点），挂点随角色朝向与动作矩阵实时偏转，使得 2D 特效或 3D 武器能够牢固吸附。

---

## 五、 场景管理与视听无缝过渡 (`SceneManager`)

1. **场景状态栈（Push / Pop）**：
   - 支持多层场景堆叠。进入副本地下城时，只需 `Push(dungeonScene)`，城镇场景挂起但不释放；退出副本时 `Pop()` 即可瞬间回到城镇原位，零读条等待。
2. **电影级黑屏遮罩切场**：
   - `FadeOut` (渐暗 $0.0 \to 1.0$) $\to$ `Switching` (完全遮蔽阶段) $\to$ `FadeIn` (渐亮 $1.0 \to 0.0$)。
   - **安全换图契约**：旧场景资源销毁、GC 与新场景资源预热全部在 `Switching`（黑屏完全遮蔽）状态下原子完成，玩家在视觉上感受不到任何掉帧与卡顿。
3. **音频总线平滑联动**：
   - `SwitchScene` 自动联动 `AudioBus`，在遮罩渐暗时淡出背景音乐，在新场景渐亮时淡入新场景 BGM。

---

## 六、 确定性网络定步同步 (`LockstepManager`)

专为红警/魔兽等 RTS、多人攻沙与联机对战打造：
1. **固定逻辑帧频**：默认 20Hz（每 50ms 一帧），与动态渲染帧率（60fps/120fps）彻底解耦。
2. **确定性命令桶（FrameBucket）**：收集该帧各玩家上报的原子操作（移动、施法、攻击），在所有客户端上以完全一致的顺序执行。
3. **断线与延迟极速追帧（Fast-Forward Catch-Up）**：
   - 当客户端网络卡顿导致落后服务器超过 5 帧时，自动开启高速追帧模式，单帧跳过渲染连续执行多次逻辑步进，快速赶上最新战局。

---

## 七、 自动化素材管线工具链 (`AssetPipeline`)

引擎提供了完整的 CLI 素材工具 `packages/Zan.Game/tools/game_asset_tool.zan` 与辅助脚本 `scripts/game_asset_tool.ps1`：

1. **图集尺寸与 UV 自动计算**：
   - 给定精灵帧尺寸与帧数，自动计算最佳紧凑二次幂（POT: 256/512/1024/2048/4096）贴图尺寸，导出标准图集 JSON 元数据。
2. **5 方向转 8 方向镜像映射表导出**：
   - 自动生成 8 个方向对应的行偏移与水平镜像标记。
3. **纸娃娃部件优先级元数据配置**：
   - 生成带 8 方向深度偏移矩阵的部件元数据。

使用命令：
```powershell
# 执行素材工具查看示例与图集规划
powershell -ExecutionPolicy Bypass -File scripts/game_asset_tool.ps1 -Atlas 64 64 32
```

---

## 八、 RTS 微观群体避障与 RVO2 互斥运动学 (`RvoSimulator`)

在万单位红警/魔兽/塔防同屏中，宏观由 `FlowField` 向量场进行 $O(1)$ 指路，微观则由 `RvoSimulator` 杜绝堵塞穿模：
1. **相互速度障碍区（ORCA/RVO2 原理）**：
   - 当两单位在探测视界内迎面或斜交时，计算速度障碍圆锥（Velocity Obstacle Cone），双方各承担 $50\%$ 的垂直相对位移偏移量，平滑擦肩而过。
2. **物理硬核穿透排斥**：
   - 若单位已被强行挤压重叠（$d < r_1 + r_2$），激活反比强斥力弹开，杜绝重叠堆死。
3. **空间局部网格哈希（`SpatialHash2D`）加速**：
   - 采用大素数网格桶替代 $O(N^2)$ 全局遍历，几千个单位同屏仅消耗数毫秒 CPU。

---

## 九、 确定性战局录像回放与快进控制 (`ReplaySystem`)

为红警/星际/魔兽等竞技战局提供轻量回放能力：
1. **轻量录像格式**：
   - 记录 `ReplayHeader`（幻数、初始随机种子、玩家数、地图名）及各逻辑帧的操作指令桶 `ReplayFrameRecord`。一局 30 分钟的高强度战局文件仅几百 KB。
2. **倍速快进与跳帧**：
   - `ReplayPlayer` 支持 $1\times, 2\times, 4\times, 8\times$ 无损倍速播放、暂停与快进追帧，完美复现精彩瞬间。

---

## 十、 2.5D 空间立体声与衰减平滑 (`SpatialAudio2D`)

将平面的 2D 音效升级为沉浸式立体战场声相：
1. **声相横向偏转（Stereo Panning）**：
   - 根据音源相对于听者（通常为屏幕中心或英雄）的横向偏差 $\Delta x$，映射为 $-1.0$ (极左声道) 到 $+1.0$ (极右声道) 的立体声像。
2. **平滑二次衰减与听觉视界裁剪**：
   - 距离 $d \le r_{\min}$ 时保真无损输出，在 $r_{\min} < d < r_{\max}$ 范围内按平滑二次曲线平滑衰减。超出 $r_{\max}$ 自动剔除静音，节省混合器算力。

---

## 十一、 官方游戏模板生态矩阵与命令快速上手

所有模板均位于 `templates/game/` 并在 `packages/Zan.Game` 统一底座上构建：

| 模板路径 | 游戏类型 | 核心集成引擎特性 |
|---|---|---|
| `templates/game/rts` | 即时战略 (RTS / 红警 / 魔兽) | `FlowField` 宏观寻路 + `RvoSimulator` 群体避障 + 小地图雷达 + 框选移动 |
| `templates/game/legend` | 2.5D 动作 RPG (传奇 / 暗黑) | 八方向朝向 + 5方向镜像 + 纸娃娃图层排序 + 怪物巡逻 + 掉落物散射 |
| `templates/game/card` | 策略卡牌 (杀戮尖塔 / 炉石) | 卡牌堆栈手牌布局 + 能量点数结算 + 回合流转 + 战斗飘字与抖动反馈 |
| `templates/game/towerdefense` | 塔防策略 (保卫萝卜 / 兽人) | 多波次刷怪 + 预设路线多段移动 + 防御塔范围自动索敌 + 穿透弹道 |
| `templates/game/idle` | 放置挂机 (暗黑挂机 / 放置骑士) | `OfflineRewardCalculator` 离线收益 + `SquashAndStretch` 弹性反馈 + `FloatingText` 飘字池 |

一键体验与打包发布：
```powershell
# 编译并打包 RTS 游戏为独立绿色安装包
powershell -ExecutionPolicy Bypass -File scripts/pack_game.ps1 -Project templates/game/rts -Name ZanRTS
```

---

## 附录：资产管线（原 ASSET_PIPELINE.md，2026-09-26 并入）

How assets flow from source art into a shipped game:

```
export tool ──(assets.manifest.json)──► IDE Asset Manager ──► project assets/ ──► publish ──► .zrp packs
```

* The **Asset Manager** (IDE ribbon → Config → Asset Manager) browses the
  project's `assets/` tree, imports single files or whole folders
  (recursively, keeping directory structure), creates/renames/deletes
  entries, and authors `.anim` clip metadata for 8-direction frame sheets.
* At publish time assets are bundled with the app; the encrypted
  `System.Resources.ResourcePack` (`.zrp`) format packs entries by name and
  reads them back individually (indexed, per-entry AES-256-GCM keys, signed
  index), so grouping many assets into one large pack per *loading unit*
  (base UI pack, per-map pack, per-monster-module pack) is the recommended
  layout.

### Import manifest: `assets.manifest.json`

Any external converter/export tool can make its output one-click importable
by writing an `assets.manifest.json` next to the exported files. When the
Asset Manager's *Import folder…* is pointed at a directory containing this
file, it imports exactly what the manifest lists (instead of copying the
raw tree).

```json
{
  "version": 1,
  "entries": [
    {
      "src": "out/skeleton_walk.png",
      "name": "mon/skeleton/walk",
      "type": "anim8",
      "anim": {
        "frameW": 96, "frameH": 96,
        "dirs": 8, "framesPerDir": 6,
        "fps": 12,
        "loop": "loop", "loopStart": 0,
        "base": 0
      }
    },
    { "src": "out/skeleton_die.png", "name": "mon/skeleton/die",
      "type": "anim8",
      "anim": { "dirs": 8, "framesPerDir": 8, "fps": 10, "loop": "once" } },
    { "src": "ui/btn.png",  "name": "ui/btn",  "type": "image" },
    { "src": "sfx/hit.wav", "name": "sfx/hit", "type": "audio" }
  ]
}
```

Fields per entry:

| field  | required | meaning |
|--------|----------|---------|
| `src`  | yes | source file, relative to the manifest's folder |
| `name` | no  | target path inside `assets/` (no extension needed — the source extension is appended; defaults to `src`) |
| `type` | no  | `image` / `audio` / `font` / `data` / `anim8` / `misc` — informational except `anim8` |
| `anim` | for `anim8` | clip metadata; written as a side-car `assets/<name>.anim` |

Unknown files without a manifest are still importable: a plain folder
import copies every file recursively, preserving relative paths.

### Clip metadata: `.anim`

A `.anim` file sits next to its frame-sheet image and describes one action
(run, slash, die, cast, …) laid out direction-major — all frames of
direction 0 first, then direction 1, and so on (the classic Mir layout):

```json
{
  "version": 1,
  "image": "walk.png",
  "frameW": 96, "frameH": 96,
  "base": 0,
  "dirs": 8,
  "framesPerDir": 6,
  "fps": 12,
  "loop": "loop",
  "loopStart": 0
}
```

* `base` — first frame of this action inside a larger multi-action sheet
  (0 when each action has its own sheet).
* `loop` — `once` (attack/death: hold the last frame), `loop`
  (walk/run: wrap to frame 0), or `section` (play frames
  `0..loopStart-1` once as a wind-up, then repeat `loopStart..last` —
  e.g. sustained casting).

The frame actually drawn is always:

```
sheetFrame = base + direction * framesPerDir + currentFrame
```

Turning a character changes only `direction`; `currentFrame` progress is
preserved, so the stride continues seamlessly in the new facing.

### Runtime: `Game.Core.AnimClip` / `AnimPlayer`

```zan
AnimClip run = AnimClip.Load(dir + "/mon/skeleton/walk.anim");
AnimPlayer p = AnimPlayer.Create();
p.Play(run);
p.SetDir(3);          // facing; progress is kept when turning
p.Update(frameMs);    // advance by elapsed milliseconds
int f = p.SheetFrame();  // absolute frame index in the sheet to draw
if (p.Done()) { /* a "once" clip finished -> back to idle */ }
```

### Publishing: encrypted `assets.zrp`

The Asset Manager's `Publish: encrypted .zrp` toggle opts the project into
packed publishing (stored as `packassets = 1` in `zan.proj`):

* Enabling generates two random 32-byte XOR key shards (`assetkey_a` /
  `assetkey_b` in `zan.proj`) and writes an `AssetPack.zan` helper into the
  project, so the reassembled master key never appears whole in the binary.
* Publishing then packs the whole `assets/` tree (plus linked resources)
  into `publish/assets/assets.zrp` — AES-256-GCM per entry, HKDF-derived
  per-entry keys, hashed entry names — instead of copying loose files.
* Game code opens it with the generated helper:

```zan
List<int> err = new List<int>(); err.Add(0);
ResourcePack pack = AssetPack.Open(exeDir, err);   // err[0] == 0 on success
List<int> n = new List<int>(); n.Add(0);
string bytes = pack.Read("mon/skeleton/walk.png", n);  // n[0] = byte length
```

Entry names are the paths relative to `assets/` (e.g. `mon/skel/run.anim`).
Toggling back to `Publish: plain copy` restores loose-file publishing; the
key shards are kept so re-enabling reuses them.
