# 真游戏标准（Zan 游戏项目的交付门槛）

> 这份文档回答一个问题：**一个项目什么时候才算"真游戏"，什么时候只是 demo？**
> 判据来自 `D:\project\games\mahjong`（第一个通过全部门槛的样板）与
> `scripts/audit_game_projects.ps1`（体检口径）。

## 0. 一句话

**基础设施门槛 ≠ 游戏品质门槛。**

编译通过、存档能读回、设置浮层能打开——这些只说明"项目是活的"。
它们和"这是个游戏"是两件事。两件事都得过。

## 1. 三道关卡

一个项目要被称为真游戏，必须同时过下面三关。缺任何一关就是原型（PROTO）。

### 关卡一：真实位图资产

- 至少 3 张**真实位图**（PNG/JPG），且**游戏画面里真的画出来了**。
  - "资产目录里有图"不算——渲染路径没走到那张图，就是摆设。
  - 判据：渲染代码里有 `CDraw.BlitImage(...)` 指向该文件（不是常量字符串拼接的假引用）。
- 位图是**美术资产**，不是几何色块：
  - 禁止用 `FillRect`/`FillCircle` 拼出"主体"再贴一张底板当美术；
  - 角色、敌人、道具、可交互物这些"主体"必须有各自的位图。
- 几何绘制只允许用在：UI 面板/血条/边框/文字/进度条这类**界面部件**。
- 角色类资产**优先用程序化花纹而不是逐张生成位图**（判定见 §4）。

### 关卡二：可玩核心闭环

玩家做一件事 → 产生可感知的反馈 → 有目标 → 有失败或成长。缺一环就是 demo。

具体到每个项目，必须同时具备：

| 要素 | 要求 |
|------|------|
| 玩家输入 | 至少一种**持续输入**（移动/瞄准/点击）+ 至少一种**离散动作**（射击/跳跃/出牌/放置） |
| 游戏状态 | 有明确的目标（得分/清怪/到达/通关/击败对手） |
| 反馈闭环 | 输入 → 状态变化 → 画面变化 **+ 音效**，玩家能感到因果 |
| 失败/成长 | 有失败条件（死亡/超时/判负）或成长曲线（分数/等级/进度被记录） |
| 对手或难度 | 至少一个非玩家因素（AI/关卡/随机），不是纯摆设 |
| 结算 | 一局结束后有结算画面或结算数据（分数/波次/回合/用时） |

**反例（这些是 demo，不是游戏）**：
- 玩家角色绕圈自动跑，没有任何输入（arena 原型就是这样）；
- 只有调试文字显示 `tick=123`，没有目标；
- 玩家和敌人互不干涉，敌人在背景里飘。

### 关卡三：音画反馈

- 用公共音效库 `Game.Foundation.SfxBank`（在 `packages/Zan.Game`）：
  - 加载：`SfxBank.LoadAll(dir, names)`（磁盘优先 + 内嵌兜底）
  - 播放：`bank.Play(name)` / `Play(name, gain)` / `Play(name, gain, cooldownMs)`
  - 音效文件：WAV，命名 `assets/sfx/<name>.wav`
- **至少 3 个事件**有各自不同的音效（射击/命中/受伤/拾取/跳跃/胜利/失败……）。
- 没有音频设备时（无头 CI、没声卡）游戏照常跑，不崩不卡——SfxBank 已保证这点。
- 音效必须**真的被事件调用**，不是加载了从不播。

### 附加：存档里要有值得存的东西

存档不是"把一个数字写进 json"。至少要能积累**跨局可见**的东西：
最高分 / 通关数 / 最佳记录（用时、关卡）/ 收集进度 / 累计场次。
并且这个数字要在游戏里被看见（菜单或结算面板显示）。

## 2. 交付物清单

每个真游戏项目必须有：

```
client/src/          玩法与渲染代码（≥ 5 个 .zan，分模块，不是单文件 300 行）
client/assets/       ≥ 3 张位图 + assets/sfx/*.wav（音效）
client/tests/        ≥ 1 个玩法断言用例（rules/gameplay_test.zan）
client/tools/        build.ps1 / test.ps1（test.ps1 三段：玩法断言 + 无头冒烟 + 存档两遍）
client/ASSETS.md     资产清单（每张图是什么、尺寸、来源）
PROJECT.md           项目说明（玩法、怎么玩、怎么跑、怎么测）
```

## 3. 验证口径（不要跑 tier）

按仓库规则 8，验证是**直接且便宜**的：

1. 编译：`pwsh -File client/tools/build.ps1`
2. 玩法断言用例：`pwsh -File client/tools/test.ps1` → 至少三段全绿：
   - `xxx_test_OK`（玩法/规则断言）
   - `xxx_SAVE_OK runs=1 -> runs=2`（存档写两遍读得到）
   - `xxx_SMOKE_OK`（无头冒烟跑满帧退出）
3. 截图人眼验收：`scripts/shot_window.py` 抓窗口像素，**人眼看**画面是否像游戏。
   （GuiHost 游戏必须抓窗口像素，不能用 `WritePixels` 抓后端画布。）
4. 体检：`pwsh -File scripts/audit_game_projects.ps1` → 该项目 `art>0` 且不再是 PROTO。

**不跑** `smoke`/`standard`/`full` tier。提交前把 `git status` 收干净。

## 4. 美术资产路线（省时间又不糊）

**不要逐张给角色/牌面/图标生成位图。** 理由：34 种牌不可能逐张生成；AI 生成的中文字形会糊；
每张图 30~90 秒且体积失控。

正确的路线是 **底板位图 + 程序化花纹**：

| 资产 | 做法 |
|------|------|
| 背景/地面/桌布 | 生成一张大图（1024 级），运行时缩放 |
| 角色/敌人/道具 | **一张主体位图**（含轮廓与阴影），叠加程序化细节（描边、高光、编号、颜色） |
| 牌面/图标类 | 一张空白底板位图 + 程序化符号（几何、文字、数字） |
| 面板/HUD/按钮 | 一张可九宫格拉伸的面板位图，或纯几何绘制 |

位图生成链（Windows）：

```powershell
# 生成（gpt-image-2.5；gpt-image-2 不支持透明背景）
& "$HOME\.codex\skills\local-image2\scripts\image2.ps1" generate --model gpt-image-2.5 `
  --prompt "<英文提示词>" --size 1024x1024 --quality high --output-format png --out "<path>.png" --force
```

生成后必须后处理：AI 图默认**没有 alpha 通道**（纯黑背景），要用 PIL 洪泛抠图抠掉黑底，
再裁边、缩放到**显示尺寸的 2 倍**入库。

```python
# 抠 alpha（黑底洪泛）
img = Image.open(src).convert("RGBA")
px = img.load()
w, h = img.size
for y in range(h):
    for x in range(w):
        r, g, b, a = px[x, y]
        if r < 12 and g < 12 and b < 12:
            px[x, y] = (r, g, b, 0)
```

体量口径：单项目素材 0.3~1.5MB，按显示尺寸 2 倍入库，不要塞原图。

## 5. 音频怎么做

- 音效：WAV（`AudioClip.LoadWav`），短促（<1s），命名 `sfx/<event>.wav`
- 背景音乐：OGG（`AudioClip.LoadOgg`）
- 采样率 22050 或 44100、单声道或立体声 16bit
- 驱动由 zanc 按引用自动打包（引用 `System.Media.Audio` 即带 `zan_audio.dll`）
- 限频：`SfxBank.Play(name, gain, cooldownMs)`，连打/多敌人同帧触发要限频，否则爆音

## 6. 参考实现

- **完整样板**：`D:\project\games\mahjong`
  - 规则引擎分模块（`MjTiles/MjShanten/MjWin/MjHand/MjTable/MjAi/MjPlay`）
  - 画面层 `MjView` + 绘制原语 `MjPaint` + 花纹 `MjGlyph`
  - 玩法断言 `client/tests/rules_test.zan`
- **音效范式**：`D:\project\games\goldminer\client\src\Entities\Snd.zan`
- **公共底座**：`packages/Zan.Game/src/Game/Foundation/`（`SfxBank` / `AudioBus` / `GameSaveStore` / `GameSettings`）

## 7. 常见坑

- **Zan 保留字**：`base` / `out` / `where` 是关键字，标识符不能以它们开头
  （`baseKind` 会被切成 `base` + `Kind`）。
- **接口实现**用 `public class X : IGuiHostLoop {`，不是 `implements`。
- **无头冒烟的帧计数放 `Update()`**：GuiHost 在窗口不可见时跳过 `Render()`。
- **贴图用 `CDraw.BlitImage(c, path, x, y, w, h)`**（逻辑坐标，自动做视口映射）；
  视差取景用 `CDraw.BlitImageSrc(...)` 裁源矩形，别"平移两张接起来"（会有竖向接缝）。
- **找不到贴图就退回纯色绘制，不崩**：`Assets.Find(rel)` 返回空 → 走 fallback 分支。
- **`File.EmbedExists(path)` + `File.ReadAllBytes`** 是内嵌资源的兜底读法；
  `byte[].ToStr(0, len)` 是二进制安全取字节。
- **发布后资源在 exe 里**：磁盘找不到必须能退回内嵌表，否则正式版全是空白。
