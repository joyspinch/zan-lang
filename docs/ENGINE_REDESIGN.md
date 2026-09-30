# 渲染引擎增强 + 游戏引擎重设计（两轨基座设计）

状态：**设计稿，未实施**（A356 挂账）。方向由 owner 于 2026-09-24 确定：

1. 放置类游戏 = **工具 + 皮肤**，留在 packages/Zan.Gui/src/Gui 轨，不进游戏引擎；
2. 工具轨缺**动画能力** → 渲染引擎整体能力增强（两轨共享）；
3. 游戏引擎**推倒重设计**，按大类（传奇/红警/帝国/魔兽/卡牌/独立游戏）建通用基座，
   而不是在现有 Kit 上逐模板打补丁。

## 0. 诊断：为什么必须重做（代码证据）

渲染栈的病根是**原语优先级错位**——它是按 UI 工具渲染器长出来的，游戏最吃的
原语恰好是它最缺的：

| # | 事实 | 出处 |
|---|------|------|
| 1 | GL 后端 vtable `blit_image = NULL`：贴图精灵在 GPU 路径未实现，每贴图走 CPU 光栅 + 帧缓冲上传；一帧混图片/模糊/影子即整体 CPU-GPU 同步 | `src/runtime/gui_gl_backend.c:1663`、`:527` |
| 2 | 批处理只覆盖几何与文本：rect/circle/radial 顶点批按 mode 聚合、文本有字形图集。传奇/RTS/卡牌的画面主体（成百上千图集精灵、图块）是全栈最弱原语 | `gui_gl_backend.c:857+` |
| 3 | CanvasPrims 每帧程序化重画光晕（FillRadial 逐像素径向衰减）；SDL 时代 bake 纹理一次每帧贴图，canvas 化丢了烘焙层 | `packages/Zan.Game/src/Game/Kit/CanvasPrims.zan` |
| 4 | Kit.SpriteBatch 是假批：计数器封装，逐图元 FFI 调 CDraw | `SpriteBatch.zan:10-60` |
| 5 | Gui 动画能力 = `Control.Transition(ms)` 样式过渡旋钮；无时间线/缓动驱动/帧动画；游戏包的 Tween（20+ 缓动曲线）过不去工具侧 | `packages/Zan.Gui/src/Gui/Core/Control.zan:324`、`Foundation/Tween.zan` |
| 6 | Gui 失效模型：脏区存在但一帧混请求即回退整窗重绘 | `packages/Zan.Gui/src/Gui/Core/App.zan:686`、`:2763` |

GuiHost 主循环本身健康（事件排空、定步积分、连续出帧、预算睡眠）——病在渲染
供给，不在循环。这解释了现状的形成：放置/工具类（legend 1.8 万行、wuwei）建在
Gui.Widget 上如鱼得水，因为工具界面用的矩形/文本恰是这套栈的主场；真游戏逆水
行舟，不是模板写得不好。

## 1. 轨 0：渲染引擎能力增强（两轨共享地基）

### 1.1 纹理化精灵批（R0，性价比核心）

zan_gui 运行时新增三个能力，GL 与 CPU 兜底路径同步实现：

- **图集注册**：内存像素/文件 → GPU 纹理，返回 int 句柄；运行时持引用计数，
  显式卸载。`blit_image` 用同一机制补齐——工具轨的 Image 部件随之直接受益。
- **精灵批提交**：`zan_gui_sprite_batch(atlas, quads[], n)`——调用方把一层的
  全部四边形（位置/uv/色/翻转打包进预分配数组）**一次 FFI 提交**；GL 侧新增
  textured-quad 顶点种类并入现有批管道（BLEND mode，与几何批共存、按状态切换
  flush）；CPU 兜底按图块裁剪逐个 blit。
- **层语义**：ground/entities/air/fog/ui 依次提交、依次 flush；批内零状态切换。

原语优先级反转：贴图四边形成为一等公民，rect/text 复用现有批。验收指标见 §4。

### 1.2 烘焙与资产（R1）

- **bake-to-atlas**：程序化效果（径向光晕、软影、图块纹理、渐变）一次绘制进
  图集条目，此后每帧只是贴一次批——恢复 SDL 时代的 bake 思想，落到 canvas 轨。
- **图集打包**：`Kit/Packed.zan` 已有雏形，补齐源图集 → 运行时图集条目的映射。
- **帧表动画**：`Arcade2D/Animation` 已有帧表模型，接到 1.1 的批上
  （uv 随帧号切换，零额外绘制成本）。

### 1.3 动画驱动器（A，工具轨缺口——放置类="工具+皮肤"的最后一块）

packages/Zan.Gui/src/Gui.Core 新增 **Timeline/Animator**：

- **值动画**：`Anim.From/to/duration/ease/onTick`，插值回调改控件属性；
  缓动曲线族与 `Foundation/Tween.zan` 的语义对齐（实现上提到共享处或双份对齐）。
- **帧动画**：图集序列帧驱动 Image/皮肤槽位（角色待机、宝箱开启、卡牌翻面）。
- **组合**：并行/串行/循环/往复；动画对象池化——Zan ARC 语义下热路径零分配。
- **驱动模型**：挂在 App 时钟上，**按需出帧**：有活跃动画才 RequestRepaint
  （尽量带区域），全部结束回到帧间休眠——遵守既有"重绘后才能 present、门控
  渲染"合同；不引入 60fps 常驻循环。放置类数值滚动、进度条、飘字、面板过渡
  全部由此承担。
- **皮肤+动画**：现有样式过渡（hover/press）扩展为可编程关键帧状态机。

## 2. 轨 1：游戏引擎重设计（大类通用基座）

### 2.1 设计原则

- **确定性优先**：定步模拟 + 整数/id 化实体 + 受控随机（种子化）——回放、
  锁步联网、bug 复现共用同一地基。跨机浮点一致性问题在引入锁步前必须给出
  决策（定点或受控浮点，见 §4 风险）。
- **零分配热路径**：池化 id、预分配顶点/指令数组。Zan 是 ARC 不是 GC，
  对象引用即原子计数——**实体一律 int id，不持对象引用**。
- **渲染与逻辑分离**（既有原则，落到接口）：sim 不碰 Canvas；render 侧消费
  sim 状态做插值绘制。

### 2.2 层次

```
L4 运行时服务   输入动作映射(已有 InputMap) · 音频总线(AudioBus) ·
               存档/回放 · 联网(RTS 锁步 | 传奇服务器权威, Arpg Net/Server 有雏形)
L3 大类 Kit     Arpg(传奇, 9.8k 行做底) · RTS(指令/编队/迷雾/小地图/生产队列, 新建) ·
               Cards(已有) · Board(已有) · Idle → 轨 0 的"工具+皮肤"(§1.3)
L2 模拟         id 实体池(组件数组 SoA; Arcade2D 池 id 化) · 空间哈希宽相 ·
               寻路(A* 自 Board 上提 + RTS 流场) · 战斗/Buff(Arpg 已有)
L1 世界         图块地图(chunk 化 + 视口裁剪) · 相机(世界↔屏幕/跟随/边界)
L0 渲染         §1 的批渲染 + 层/相机裁剪/批提交的薄封装
```

### 2.3 与现状的关系

- **GuiHost 保留**为主循环壳（健康），IGuiHostLoop 接口保留；Render 侧改走
  L0 批渲染。
- **CanvasPrims/SpriteBatch 退役**或降级为兼容层：CanvasPrims 的程序化绘制
  移入 R1 烘焙；SpriteBatch 变真批（§1.1 计数器变真指标）。
- **game-platformer 迁移**为 L1/L2 验收样板（顺带消除 A354 的幽灵引擎）。
- 传奇类：Arpg 模块即 L2/L3 骨架；legend 模板的公共层（表格/战斗/掉落）是否
  下沉为 Arpg 正式 API 属立项级决策，不阻塞 P0/P1。

## 3. 分期

| 期 | 内容 | 验收 |
|----|------|------|
| P0 | §1 全部：图集+精灵批+blit_image 补齐+烘焙层+Gui Timeline | 帧预算 gate：游戏侧万精灵 60fps；工具侧 Image 密集页帧耗时显著下降；放置类样板（数值滚动+帧动画+面板过渡）纯 Gui 跑通 |
| P1 | L1 世界 + L2 id 池/空间哈希/A* 上提；game-platformer 迁移为样板 | 平台跳跃样板达 60fps 帧预算；A354 消账 |
| P2 | L3：RTS kit 新建；Arpg 补齐与 legend 下沉评估 | RTS 微样板（选择/指令/迷雾/小地图）达帧预算 |
| P3 | L4 锁步联网 | 双机确定性回放一致 |

## 4. 风险与决策点

- **CPU/GPU 缝**：GL vtable 部分槽位永久留给 CPU（blur/shadow/snapshot，
  `gui_gl_backend.c:23`、`:1648`）。精灵批必须保证纯 GPU 路径可完成整帧，
  否则一次同步就吃掉批收益——混排帧的分界策略要在 P0 定型。
- **确定性浮点**：锁步要求跨机位级一致。P3 前必须决定：定点数学 or 受控浮点
  （限制到整数格/查表）。这影响 L2 的全部数值接口，P1 设计 id 池时预留。
- **兼容窗口**：现有 7 个已绿游戏模板依赖 CanvasPrims/GuiHost——过渡期保持
  可编译（兼容层），逐个迁 L0 后退役。
- **文档失真**：`packages/Zan.Game/README.md` 宣称的 Game.Zgm 模块不存在，
  重设计落地时一并修正。
