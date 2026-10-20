# 游戏美术基线（共享层）

这里是**跨模板共享的美术层**：色板、绘制规则、可平铺纹理、UI 底板。
它只放"换个游戏还成立"的素材——各模板的玩法专属插画（角色、卡面、战场）仍然是
每个模板自己的资产，放在 `templates/game/<名字>/assets/` 里。

> 边界：**共享资源，不共享模板。**
> 任何模板都不许从别的模板目录拷文件、也不许有一个"公共模板"让各项目套用。
> 共享只发生在 `packages/`（机制）和 `assets/game/`（通用资源）两处。

## 风格串（给生图脚本用）

`scripts/gen_game_art.ps1` 默认读这里的 `STYLE` 行，拼在 prompt 前面。
同一批生成的素材因此天然成一套——**统一风格靠共享串，不靠谁去抄谁**。

```
STYLE: cohesive modern 2D game art, hand-painted digital illustration, bold clean outlines, soft rim light from the top-left, gentle ambient occlusion, saturated but not neon-blown palette, centered subject with generous margin, transparent or flat studio background
```

各品类可以在后面追加自己的限定词（追加在 `STYLE` 之后，仍共享底色规则）。
下表的机器可读版本在下面一行 `GENRES:` 里，`scripts/gen_game_art.ps1` 用 `-Genre` 取：

| 品类 | 追加限定词 |
|---|---|
| 街机 / 平台 | `playful arcade energy, chunky readable silhouette` |
| 卡牌 | `ornate card illustration, single centered icon, no text` |
| 策略 / 塔防 | `tactical top-down clarity, distinct unit silhouettes` |
| 棋类 / 解谜 | `clean puzzle-piece iconography, flat readable shapes` |
| 放置挂机 | `cozy idle-game charm, warm inviting lighting` |

```
GENRES: arcade=playful arcade energy, chunky readable silhouette | card=ornate card illustration, single centered icon, no text | tactics=tactical top-down clarity, distinct unit silhouettes | puzzle=clean puzzle-piece iconography, flat readable shapes | idle=cozy idle-game charm, warm inviting lighting
```

## 色板（`palette.json`）

所有 UI、HUD、提示色都从这套色板取，**不要在模板里另起一套**：

| 语义 | 颜色 | 用途 |
|---|---|---|
| `bg_deep` | `#0d1117` | 最底层背景 |
| `bg_panel` | `#161b22` | 面板底 |
| `ink` | `#e6edf3` | 主文字 |
| `ink_soft` | `#8b949e` | 次文字 |
| `accent` | `#3fb950` | 主强调 / 可交互 |
| `accent2` | `#58a6ff` | 次强调 / 信息 |
| `warn` | `#d29922` | 警告 / 资源 |
| `danger` | `#f85149` | 危险 / 失败 |
| `gold` | `#e3b341` | 金币 / 奖励 |
| `line` | `#30363d` | 分隔线 / 描边 |

## 绘制规则（"不能是色块"的判据）

一张图要进模板，至少满足：

1. **不是纯色块**：唯一颜色数 ≥ 64（PIL `getcolors` 判定），且有明确的明暗层次。
2. **不是空图**：宽高都 ≥ 32，文件 ≥ 4 KiB。
3. **有主体**：主体占画面 ≥ 8%（避免"满屏噪点"或"只有边框"）。
4. **描边统一**：描边宽度 2~4 px，颜色偏深（`#1a1a1a`~`#2a2018`），不用纯黑。
5. **光向一致**：高光在上、投影在下。

这五条由 `scripts/check_game_art.py` 自动检查（模板门禁会调）。

## 共享纹理与底板

`textures/` 与 `ui/` 下是**程序化生成**的可平铺素材（免 API、确定性、可复现），
各模板需要时**拷贝一份进自己的 `assets/`**——拷贝是复制，不是引用，
这样每个模板项目仍然独立自包含、可单独发布。

| 文件 | 用途 |
|---|---|
| `textures/paper.png` | 纸纹底（卡牌、面板、菜单背景叠加） |
| `textures/wood.png` | 木纹（UI 边框、摊位、牌桌） |
| `textures/metal.png` | 拉丝金属（机械、载具、按钮描边） |
| `textures/noise.png` | 半透明噪点（叠在纯色上破色块） |
| `textures/spark.png` | 粒子光点（爆炸、拾取、加速尾迹） |
| `textures/vignette.png` | 暗角（聚焦中央玩法区） |
| `ui/panel.png` | 九宫格面板底（弹窗、HUD 底板） |

生成方式（改动规则请同步改脚本，别手改图）：

```powershell
python scripts/gen_shared_art.py            # 全量重生成
python scripts/gen_shared_art.py --only textures/spark.png
```

## 各模板的搭配建议

| 模板 | 插画风格 | 可搭配的共享件 |
|---|---|---|
| snake | 霓虹 3D 卡通 | `textures/vignette.png`（菜单暗角） |
| pin-circle | 玉石 + 黄铜质感 | `textures/metal.png` |
| roguelike | 卡面插画 | `textures/paper.png` |
| rts | 俯视战场 | `textures/noise.png` |
| trap-cat | 扁平解谜图标 | `textures/paper.png`、`textures/spark.png` |
| platformer | 横版卡通 | `textures/noise.png` |
| idle | 放置挂机 | 由并行开发补齐 |

> 共享件**按需拷进模板自己的 `assets/`**再使用——拷进去之后它就是该模板的
> 私有资产了（复制，不是引用），模板依旧独立自包含。
> 别在模板代码里写仓库路径去读 `assets/game/`——那样项目拷出去就崩了。
