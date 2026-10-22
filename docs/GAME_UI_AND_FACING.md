# 游戏 UI 图形化与朝向规范（第二轮品质改造）

> 这份文档是 `docs/REAL_GAME_STANDARD.md` 的补充：第一轮把"是游戏"立起来了，
> 这一轮把"看起来像游戏"和"动起来对"补上。判据来自 11 个游戏项目的实拍截图
> 与引擎 API 实查。

## 一、为什么会出现"系统风 UI"

不是审美问题，是**没用到引擎已经有的能力**：

- 引擎有**皮肤包机制**：`skins/<name>/skin.css`（`:root` 令牌 + 每控件规则）
  + 美术资源，`app.UseSkin(name)` 一次应用。查找位置包含
  `exe 同级/skins/`、`exe 同级/assets/skins/`（见 `Gui.Styling.Skin.Roots`）。
- 改造前 11 个游戏**一个都没带皮肤包**，于是所有标准库控件（设置浮层的
  Panel/Flex/Button/Label）用的是系统默认外观——就是那张黑卡片和灰按钮。
- 游戏内画布上的按钮是"圆角矩形 + 纯色 + 系统风文字"，没有美术底板。

## 二、UI 图形化（硬判据）

### 1. 每个游戏自带皮肤包

- 位置：`client/assets/skins/<game>/skin.css`（+ 同目录的美术资源）。
- 构建：拷到 `build/<game>/skins/<game>/`（exe 同级）。
- 启动：`host.App().UseSkin("<game>")`，**在 `UseSkin("dark")` 之后调用**，
  皮肤包的规则与令牌覆盖默认皮肤。
- CSS 至少覆写三处，缺一处就会露系统风：
  - `:root` 令牌：`--primary` / `--bg-secondary` / `--text-primary` /
    `--border-primary` / `--radius-small` / `--font-size-*` …
    **令牌名照 `packages/Zan.Gui/src/Gui/skins/base.css` 的清单抄，不要自造**。
  - `button` 规则：背景、边框、圆角、字色，以及 `button:hover` / `button:active`。
  - `panel.card` 规则：设置浮层那张卡（`SettingsOverlay` 的内容盒 class 是 `card`）。
- 没有 `border-image`、没有九宫格 CSS：皮肤贴图只能整幅贴，**要九宫格得自己切**
  （见下条）。

### 2. 画布上的按钮/面板用美术底板，不用"圆角矩形 + 纯色"

- 每个项目出 1 张 UI 底板图（按钮常态 + 悬停 + 按下三态，或一张 + 配色变体）。
- 九宫格自己切：源矩形拼 9 块（角固定、边横/竖拉伸、中间平铺）。引擎没有
  border-image，别指望 CSS 那套。**切的写法见第六节**（`Canvas.BlitImage` 的
  源矩形在最后）。
- 文字用主题色 + 描边/阴影，保证压在游戏画面上也能读。

### 3. 颜色收进一个色板

每个项目一个色板（类或常量组）：底色/前景/强调/危险/成功/面板底/描边。
**不允许**同一个语义在三个文件里挑三个颜色。

## 三、朝向（硬判据）

### 1. 引擎限制：运行时转不了图

`Canvas.BlitImage(path, dx, dy, dw, dh, sx, sy, sw, sh)` 只有源矩形，
**没有旋转、没有水平翻转**；`Gui.SpriteBatch` 的每精灵也只有
`dx,dy,dw,dh,sx,sy,sw,sh,tint`。所以"运行时把角色转个方向"做不到。

### 2. 做法：离线预烘焙方向帧

用 PIL 在素材处理阶段把主体旋/镜像成多帧，运行时按方向取帧：

| 视角 | 帧方案 | 取帧 |
|------|--------|------|
| 俯视（任意角度） | 16 或 24 方向横条精灵表（一张 PNG，帧宽=帧高=贴图边长） | `Canvas.BlitImage` 裁第 n 帧 |
| 横版 / 网格（左右） | 原图 + 镜像图（两张文件或一张两帧精灵表） | 同上 |
| 载具 | 同俯视多角度 | 同上 |

- 帧数、帧尺寸、角度→索引的映射规则**写进 `client/ASSETS.md`**。
- 角度→索引的映射必须是纯函数（`index = ((int)((deg + 11.25) % 360 / 22.5))` 这种），
  并在断言用例里钉住（"逻辑朝向 → 取帧索引"的映射不回归）。

### 3. 谁必须有朝向

- 俯视射击/竞速：玩家按瞄准方向取帧；追击敌人面朝玩家；炮塔炮管朝玩家。
- 横版平台：角色面朝移动方向；巡逻敌人面朝巡逻方向。
- 网格回合制：角色面朝最后移动方向。
- 列车/载具：车头朝行进方向。

**验收**：截图里能看出"往左走时角色面朝左"；断言用例里有"朝向状态 → 取帧索引"
的映射断言。只算朝向状态、渲染不用的（第一轮 platformer 就是这样）算不合格。

## 四、主体细节（同一轮一起做）

- 主体与背景要分离：加落地投影（椭圆暗影）或描边，别让角色糊在背景里。
- 受击/死亡要有画面反馈（闪白、抖动、粒子），不能只改数字。
- 场景里的可交互物（掩体、站台、箱子、树木）也要有位图，不能是纯色块。

## 五、不能破坏的东西

- 玩法断言条数不减少，三段测试（用例 / 存档两遍 / 无头冒烟）全绿。
- 体检 `scripts/audit_game_projects.ps1` 不回归（art>0、wired、playtest）。
- 存档兼容：老档能读（新增字段按 0 处理）。
- 提交只动自己项目的文件，`git status` 干净。

## 六、皮肤包与内嵌：哪些必须落磁盘，哪些已经内嵌

- 编译器会**自动把 `Gui/skins` 整目录烤进 exe**：`src/compiler/main.c` 在
  "GUI 程序且 `Skin_` 驱动在链"时自动加一条 `--embed <包根>/Gui/skins=skins`。
- 内嵌的**过滤器只作用于 depth-1 的皮肤包目录**（`src/compiler/embedres.c`）：
  只保留 `dark` / `light`，外加 `zan.proj` 里 `skinlist=` 声明过的包。
  **`base.css` 是文件、不受这个过滤——它一定被内嵌。**
- 因此：**游戏自己的皮肤包 `<game>/skin.css` 不会被内嵌**，必须拷到 exe 同级
  `skins/<game>/skin.css`（除非项目在 `zan.proj` 里 `skins=1` + `skinlist=<game>`）。
- 运行时 `Skin.BaseCss()`（`packages/Zan.Gui/src/Gui/Styling/Skin.zan`）是
  **磁盘优先、内嵌兜底**：先在 `Skin.Roots()` 的各根目录找 `<root>/base.css`，
  找不到才读内嵌副本。所以磁盘上再放一份 base.css 是"同源覆盖"（发布目录自洽、
  可运行期改基线），**不是必需项**——缺了它控件照样有基线。
- 坑：有任务书写成"Gui 只在磁盘上找皮肤、没有内嵌兜底"，被 15 份 worker 继承，
  又被抄进 7 个项目的 build.ps1 注释。**引擎事实自己读源码确认，别抄任务书。**

## 七、常见坑

- **`UseSkin` 必须在 `UseSkin("dark")` 之后**：否则被默认皮肤盖回去。
- **皮肤目录必须与 exe 同级**：`build/<game>/skins/<game>/skin.css`，
  放 `assets/` 下不会被 `Skin.Roots` 找到（除非 exe 同级也有 `assets/skins/`）。
- **九宫格与精灵表取帧用 `CDraw.BlitImageSrc(c, path, dx, dy, dw, dh, sx, sy, sw, sh)`**：
  **源矩形在最后**，`sw=0,sh=0` 表示整幅。目标矩形走视口映射、源矩形按源图像素
  裁，两组坐标单位不同（最容易搞错）。底层是
  `Canvas.BlitImage(key, dx, dy, dw, dh, sx, sy, sw, sh)`，key 用
  `CDraw.BlitSource(c, path)` 换内存键。**别再各项目自己写一份 BlitSrc**——
  `voxel/client/src/VoxelPaint.zan` 等三处旧样板应收敛到包里的这一个。
- **`CDraw.BlitImageSrc` 是 2026-10-22 补进包的，此前该方法在本文件里从未
  存在过**——属**包 API 缺失**，不是被并发会话删掉。取证命令（输出为空）：
  ```
  git log -S 'BlitImageSrc' 6b1ef3803^ -- packages/Zan.Game/src/Game/Kit/CDraw.zan
  ```
  若属"被删"，这条命令会命中删除它的那次提交。它此前只在**文档与 snake
  模板**里当"用法说明"出现过（模板直接调它 → 模板编译不过），于是一批 worker
  把一个不存在的函数当成真 API 抄了下去。
- **精灵表取帧的源坐标是原图像素**，不是显示像素——帧宽按入库图的实际宽度算。
- **视差背景用 `CKitBackdrop.Draw(c, path, dx, dy, dw, dh, offsetX, offsetY, coverPercent)`**：
  背景图按 `coverPercent` 放大（125 = 放大到 125%，于是一定盖住视口），视口
  窗口折算回源图像素（`sw = dw*100/pct`），相机偏移同样折算回源图像素后
  **夹取**到 `[0, iw-sw]`——取景永远落在图内。一次 blit，不拼接，没有竖直接缝。
  取景算术单独拆成纯函数 `CKitBackdrop.SourceRect(...)`，可被 conformance 逐值钉住。
  素材缺失直接跳过不抛异常（矢量兜底由调用方铺底色负责）。
- **`CKitBackdrop` 也是 2026-10-22 补进包的，此前从未存在过**——与
  `CDraw.BlitImageSrc` 同源：`df6d0a642` 那次"共享美术层"把**怎么用**写进了
  模板与技能，却没把**用什么**写进包。取证命令（输出为空）：
  ```
  git log --all -S 'CKitBackdrop' -- packages/
  ```
  后果是 platformer 模板编译不过（`unresolved call 'CKitBackdrop.Draw'`）。
- **模板门禁要分清两个数**：`game-template check: N template(s), 0 finding(s)`
  是**规则检查**（样式/规范违规），`templates=N failed=M` 才是**编译结果**——
  只看前一个数会漏掉"模板引用了包里根本没有的 API"这类编译失败。
- **旋转帧会让画布变脏**：旋转后要重新抠 alpha（旋转会带出黑边）。
