# 风格配方卡

十三张主流风格方向的现成配方:选一张 → 复制 `:root` 覆写与关键规则 → 落地成皮肤 →
整屏验收。**先读"选型法"、"能力边界"与 `references/skin-materials.md` 再动手。**

## 用法

1. **选卡**:按下面的选型三问挑一张最贴近题材的;先对照 `references/skin-materials.md`
   的 16 套内置皮肤目录,已有贴近风格直接复用或作基线——所有卡都是可粘贴的起点,不是标准答案。
2. **落地**:皮肤包 = 一个目录一个 `skin.css`(以 `:root` 令牌为主,按需补少量受限选择器规则),零业务代码。
   两条路:
   - 随应用发布(推荐给用户):在应用工程根放 `skins/<风格名>/skin.css`
     (应用自带皮肤优先于内置皮肤,按 `Skin.zan` 的 Roots 顺序)。
   - 作为 Zan 内置皮肤贡献(在 zan-lang 仓库内):需四处同步——新建
     `packages/Zan.Gui/skins/<风格名>/skin.css`、登记到
     `packages/Zan.Gui/src/Gui/Styling/Skin.zan`(`BuiltInNames`)与
     `packages/Zan.Gui/zan.pkg`(`baseline=`),并补齐契约/抽屉像素测试(详见 `skin-materials.md` §8)。
3. **验收**:每张卡末尾 2-3 条 + SKILL.md 自查 + `composed.Lint() == 0`;在 zan-lang 仓库内
   用 gallery 深链 `./gui_gallery <组件名> <皮肤名>` 或 16 皮肤样板逐张肉眼验收。

## 选型三问(决定风格方向前先回答)

1. 这个产品是什么题材、谁在用、这屏要完成什么?——从题材自己的身体记忆
   (材质、器物、行话)里找方案,不从"好看的模板"里找。
2. 签名元素放哪?——一个画面只有一处大胆(一块发光数据 / 一面超大标题 /
   一个插画位),其余全部安静。
3. 这是不是题材真正需要的,还是 AI 模板脸?——米色+衬线+赤陶、纯黑+荧光绿、
   报纸细线三张"默认脸"出现时,先问第 1 题的答案。

## 能力边界(卡里的配方不会越界,但你要知道)

- **无字体家族 token**:字体个性靠字重(`font-weight: 300..900`)、
  字距(`letter-spacing`)、大小写(`text-transform: uppercase`)和字号阶梯表达。
- **渐变与感知派生**:`linear-gradient` 支持 4 个方向、最多 3 个色标;主题派生渐变令牌
  (`--primary-grad-end`/`--surface-grad-end`/`--titlebar-grad-end` 等)由引擎用 **OKLab**
  感知插值自动推导,不会出现 sRGB 灰泥中间色。
- **双层阴影与折光棱**:`box-shadow` 原生支持逗号分隔的**两层阴影**
  (Contact+Ambient 物理抬升影,或柔陶/拟物左上高光+右下投影双侧浮雕);
  `-zan-sheen: <0..400> [<color>]` 绘制顶沿折光棱,`>=220` 时自动追加底部光学焦散折光棱。
- **明暗双玻璃壁纸**:`--glass: 1; --gradient: 1;` 按背景明暗自动切换暗色极光(`frosted`)
  或亮色日光流彩(`liquid`)静态缓存折射底板。
- 动效关键帧内置 8 个:`spin/pulse/breath/shimmer/float/glow/aurora/motes`。
- 有 token 可调的:全套语义色、圆角三档(`--border-radius-*`)、边框宽、
  阴影色、风格基因开关(`--brutal/--neu/--glass/--gradient/--fx`)、
  玻璃全家(`--glass-blur/--glass-tint/--glass-chrome-tint/--glass-sheen/...`)、
  以及 `--font-size-*/--height-*/--gap-*` 三套档位(皮肤可整体改值)。
- **永远不要写死裸调色板色**:颜色走 `var(--*)` 与语义类,避免换肤即脏;皮肤覆写选择器严守
  `skin-materials.md` §7 四大级联红线。

---

### 1. 商务专业 Corporate

干净、可信、零情绪。报表/后台/SaaS 管理端的默认选择。

```css
/* 参考内置皮肤: porcelain */
--dark: 0; --base: light;
--primary: #2b5fd9; --primary-hover: #3d6ee0; --primary-pressed: #1f4bb8;
--info: #2b5fd9; --success: #1f8a4c; --warning: #b7791f; --error: #c0392b;
--text-primary: #1c2430; --text-secondary: #46536b; --text-tertiary: #77839b;
--bg-primary: #f5f7fa; --bg-secondary: #ffffff; --bg-tertiary: #eef1f6;
--border-primary: #d7dde8; --border-secondary: #e4e9f1; --divider: #e4e9f1;
--border-radius-small: 4; --border-radius-medium: 8; --border-radius-large: 12;
--border-width: 1; --shadow-color: #1c2430;
```

排版:标题 `font-weight: 600` + `color: var(--text-primary)`,正文默认 medium,说明文字一律
`var(--text-secondary)`;不用全大写/加宽字距。动效:只有 hover 的 `transition: var(--anim-fast) ease-out`。
文案:完整句式、零语气词。
验收:除 primary 外全画面不出现第二块彩色;卡片轻 Contact+Ambient 影,大阴影只在弹层。

### 2. 街头/嘻哈 Streetwear

粗野主义骨架:直角、粗边框、高对比、硬阴影。球鞋/潮牌/音乐类。

```css
/* 参考内置皮肤: brutal */
--dark: 0; --base: light; --brutal: 1;
--primary: #ffd400; --primary-hover: #ffe14d; --primary-pressed: #e6bf00;
--info: #2d5cff; --success: #00c853; --warning: #ff9100; --error: #ff1744;
--text-primary: #0a0a0a; --text-secondary: #1f1f1f; --text-tertiary: #4d4d4d;
--bg-primary: #f2ede0; --bg-secondary: #ffffff;
--border-primary: #0a0a0a; --border-secondary: #0a0a0a;
--border-hover: #0a0a0a; --border-focus: #0a0a0a;
--border-radius-small: 0; --border-radius-medium: 0; --border-radius-large: 0;
--border-width: 2; --shadow-color: #0a0a0a;
```

排版:标题 `text-transform: uppercase; letter-spacing: 2px; font-weight: 900`,只一处(`var(--font-size-display)` 级);
按钮大写。动效:无入场,hover 硬切(`var(--anim-fast)` + 微位移)。文案:短促、口号式,但动作名仍写具体("下单"不是"冲")。
验收:圆角必须全 0;边框 2px 全画面统一;签名 = 一块贴纸式大标题。

### 3. 赛博朋克 Cyberpunk

近黑底、荧光青主色、品红点缀。数据监控/游戏/极客工具。

```css
/* 参考内置皮肤: cyber */
--dark: 1; --base: dark;
--primary: #00f0ff; --primary-hover: #4df5ff; --primary-pressed: #00b8cc;
--info: #ff2bd6; --warning: #ffe600; --error: #ff2b4e; --success: #00ff9d;
--text-primary: #e8f6ff; --text-secondary: #9fb8cc; --text-tertiary: #5c7488;
--text-inverse: #0a1420;
--bg-primary: #070b12; --bg-secondary: #0d1420; --bg-tertiary: #131d2e;
--border-primary: #1f3247; --border-secondary: #16222f; --divider: #16222f;
--border-radius-small: 0; --border-radius-medium: 2; --border-radius-large: 4;
--border-width: 1; --shadow-color: #34000000;
```

排版:小字密排(`var(--font-size-tiny)` 大量)+ 一处超大数字,天然等宽气质
(用字距和字号落差顶)。动效:`glow` 一处或加载位 `shimmer`,别处静止。文案:冷峻、术语密度高、不用感叹号。
验收:荧光色只能当 accent/描边/辉光,永远不许当大面积底;亮青主按钮文字必须深色反色(`--text-inverse`)。

### 4. 暗金豪华 Luxe

近黑 + 金线 + 大字距。高端消费/金融/收藏展示。

```css
/* 参考内置皮肤: luxe */
--dark: 1; --base: dark;
--primary: #d4af37; --primary-hover: #e0c05a; --primary-pressed: #b8952b;
--text-primary: #f2ead6; --text-secondary: #c9bfa4; --text-tertiary: #8f866f;
--text-inverse: #141008;
--bg-primary: #12100c; --bg-secondary: #1a1712; --bg-tertiary: #242017;
--border-primary: #3a3223; --border-secondary: #322c1f; --divider: #322c1f;
--border-radius-small: 2; --border-radius-medium: 4; --border-radius-large: 6;
--border-width: 1; --shadow-color: #38000000;
```

排版:eyebrow 小标签 `text-transform: uppercase; letter-spacing: 2px`,大标题 `font-weight: 300`
(细字重=高级),行距放宽。动效:至多一处 `shimmer`(金线扫过)。
文案:名词性、低声、不用惊叹号与口语。
验收:金色只出现在强调位、微金 `-zan-sheen` 与细描边;大面积底永远近黑;圆角 ≤6。

### 5. 国风 Guofeng

宣纸底 + 朱砂 + 黛青,大留白多分隔线。文化/茶饮/中式品牌。

```css
/* 参考内置皮肤: paper / terracotta */
--dark: 0; --base: light;
--primary: #b03a2e; --primary-hover: #c1483c; --primary-pressed: #8f2e24;
--info: #34557a; --success: #4a7c59; --warning: #c98a1b; --error: #b03a2e;
--text-primary: #2b2620; --text-secondary: #5a5248; --text-tertiary: #8c8377;
--bg-primary: #f7f3e9; --bg-secondary: #fffdf7; --bg-tertiary: #efe8d8;
--border-primary: #d8cfbc; --border-secondary: #e5ddca; --divider: #d8cfbc;
--border-radius-small: 4; --border-radius-medium: 8; --border-radius-large: 12;
--border-width: 1; --shadow-color: #208f2e24;
```

排版:标题加宽字距(`letter-spacing: 2px`,汉字加宽字距是国风关键),标点全角;
区块题用分隔线+字距替代边框。动效:`breath` 一处(印章式徽标)。
文案:四字短语、敬语克制;数字场景慎用花哨图表配色。
验收:朱砂只当 accent 与印章 badge;留白比默认大一级(`var(--padding-large)`、`var(--gap-large)`
起步);divider 线多用。

### 6. 极简日式 Zen

灰阶 + 一点朱红,大留白,无阴影无动效。阅读/笔记/专注工具。

```css
/* 参考内置皮肤: paper / sage —— 这张卡展示皮肤连间距档都能整体换 */
--dark: 0; --base: light;
--primary: #c92a2a; --primary-hover: #d63c3c; --primary-pressed: #a81f1f;
--text-primary: #1c1c1c; --text-secondary: #555555; --text-tertiary: #9a9a9a;
--bg-primary: #fafaf8; --bg-secondary: #ffffff; --bg-tertiary: #f0f0ec;
--border-primary: #e2e2de; --border-secondary: #ebebe7; --divider: #e2e2de;
--border-radius-small: 4; --border-radius-medium: 6; --border-radius-large: 8;
--border-width: 1; --shadow-color: #141c1c1c;
--gap-small: 8; --gap-medium: 12; --gap-large: 20;
```

排版:正文降一档用(`var(--font-size-small)` 做正文),标题不加粗(`font-weight: 400`);
层级完全交给字色和间距。动效:零。文案:极短,名词结尾。
验收:除朱红一点外无彩色;阴影不可见;整屏"空"是对的,不是没做完。

### 7. 暗色磨砂玻璃 Frosted

半透明深海磨砂面板压在极光渐变底板上。展示型首页、启动器、暗色控制中心。

```css
/* 参考内置皮肤: frosted */
--dark: 1; --base: dark;
--primary: #7cc4ff; --primary-hover: #a0d6ff; --primary-pressed: #5ea9eb;
--text-primary: #eff6ff; --text-inverse: #0d1828;
--glass: 1; --gradient: 1; --fx: 0;
--glass-blur: 32; --glass-tint: 380; --glass-chrome-tint: 580; --glass-sheen: 270;
--bg-grad-top: #0d1829; --bg-grad-bottom: #1f3652;
```

`--glass-blur/--glass-tint/--glass-sheen` 按场景微调:想更雾加 blur,想文字更稳加 tint;
`-zan-sheen: 270` 同时点亮顶沿与底沿光学焦散折光棱。
排版:玻璃上的字禁止 tiny 档(可读性优先);面板层级用双层阴影 + 亮边区分。
验收:玻璃后面必须是渐变/光斑/壁纸——纯单色底上开玻璃是白开;逐面板检查文字对比度。

### 8. 新拟态 Neumorphism

控件从同色底面"鼓出来",靠左上暖白高光与右下投影双层阴影塑造软浮雕。家居/音频/温和工具,慎用:可访问性偏弱。

```css
/* 参考内置皮肤: clay(现代柔陶增强版,可读性远优于纯新拟态) */
--dark: 0; --base: light; --neu: 1;
--primary: #5b7cfa; --primary-hover: #6d8bfb; --primary-pressed: #4a69e8;
--text-primary: #3d4a5c; --text-secondary: #6b7889; --text-tertiary: #97a2b3;
--bg-primary: #e6eaf1; --bg-secondary: #e6eaf1; --bg-tertiary: #dde2eb;
--bg-hover: #eceff5;
--border-primary: #d8dee9; --border-secondary: #e0e5ee; --divider: #d7dce6;
--border-radius-small: 10; --border-radius-medium: 14; --border-radius-large: 18;
/* 卡片配合双层阴影: box-shadow: -3 -3 9 #dcffffff, 4 5 14 #2620283a; */
```

现代实践优先选卡 13 **柔陶浮雕（Claymorphism）**:保留双侧高光+投影触感,同时加 1px 半透明微描边与高对比文字,兼顾质感与清晰度。
验收:`bg-primary` 与 `bg-secondary` 保持极近同色温;数据密集表与低视力场景禁用纯无边新拟态。

### 9. 卡通波普 Pop

高撞色、粗黑边、大圆角、硬阴影。儿童/活动页/品牌周边。

```css
/* 参考内置皮肤: brutal(把圆角加大即得卡通波普)—— 展示字号档也能换 */
--dark: 0; --base: light; --brutal: 1;
--primary: #ff6b35; --primary-hover: #ff8354; --primary-pressed: #e5561f;
--info: #00b8d9; --success: #2ecc71; --warning: #ffc428; --error: #ff4757;
--text-primary: #22223a; --text-secondary: #4a4a68; --text-tertiary: #8d8db0;
--bg-primary: #fff8f0; --bg-secondary: #ffffff; --bg-tertiary: #ffefd9;
--border-primary: #22223a; --border-secondary: #22223a;
--border-hover: #ff6b35; --border-focus: #00b8d9;
--border-radius-small: 10; --border-radius-medium: 16; --border-radius-large: 22;
--border-width: 2; --shadow-color: #22223a;
--font-size-large: 18; --font-size-huge: 26;
```

排版:标题 `font-weight: 900`;badge/tag 走皮肤状态类(`success`/`warning`/`error`)。
动效:`float` 一处(吉祥物/图标),`breath` 给在线状态。文案:短句 + 轻语气词,动作按钮仍写具体动作。
验收:2px 粗边框全画面统一;硬黑阴影统一;签名 = 一个插画位/超大图形。

### 10. 液态玻璃 Liquid Glass

macOS Tahoe / Apple Liquid Glass 亮色通透折射美学:日光流彩光斑底板 + 高透磨砂卡片 + 顶底双沿光学焦散折光棱 + 柔和 Contact+Ambient 投影。现代创作工具、桌面主控台、精致消费应用。

```css
/* 参考内置皮肤: liquid */
--dark: 0; --base: light;
--primary: #0a7aff; --primary-hover: #2d90ff; --primary-pressed: #005fd1;
--text-primary: #0f172a; --text-secondary: #3b4b63; --text-tertiary: #64748b;
--text-inverse: #ffffff;
--bg-primary: #e6effa; --bg-secondary: #f4f8ff; --bg-tertiary: #dbe7f7;
--border-primary: #9fb8d8; --border-secondary: #c5d7ee;
--glass: 1; --gradient: 1; --fx: 0;
--glass-blur: 30; --glass-tint: 680; --glass-chrome-tint: 800; --glass-sheen: 290;
--bg-grad-top: #dcecff; --bg-grad-bottom: #f3e8ff;
--surface-sheen: 290; --surface-sheen-color: #ffffff;
/* 卡片配合: border: 1 #ccffffff; box-shadow: 0 1 3 #181a2c4a, 0 10 28 #201a2c4a; */
```

排版:深石板灰正文(`#0f172a`)保高对比,禁用 tiny 灰字压在透明层上。动效:hover 微提 `-zan-sheen` 与主按钮轻辉光。
验收:底板必须呈现柔和日光流彩光斑;卡片顶/底两条焦散折光棱清晰可见;抽屉保持实底不透明(`:not(.drawer-surface)`)。

### 11. 极夜工程 Obsidian

Linear / Vercel / Raycast 式精加工暗色工程风:微蓝炭黑明度阶梯 + 1px 半透明白光棱 + 靛蓝聚焦环。IDE、开发者工具、AI 工作台、专业 SaaS。

```css
/* 参考内置皮肤: obsidian */
--dark: 1; --base: dark;
--primary: #7c8cff; --primary-hover: #9aa7ff; --primary-pressed: #5d6ee8;
--text-primary: #f3f5f9; --text-secondary: #a1a8b8; --text-tertiary: #6e7688;
--text-inverse: #0a0c12;
--bg-primary: #0d0f13; --bg-secondary: #14171e; --bg-tertiary: #1c2029;
--bg-hover: #242936; --bg-active: #232a42;
--border-primary: #2d3342; --border-secondary: #222733; --divider: #202530;
--surface-sheen: 100; --surface-sheen-color: #ffffff;
/* 卡片配合: border: 1 #20ffffff; box-shadow: 0 1 2 #58000000, 0 8 22 #44000000; */
```

排版:紧凑清晰,标题与正文靠明度与字号阶梯分层;零花哨装饰,强调信息密度与锐利边界。
验收:严禁纯 `#000000` 死黑底与刺眼高饱和边框;焦点环(`0 0 8 #667c8cff`)与暗色凹槽输入框清晰可辨。

### 12. 极光氛围 Aurora

深邃暗色极光渐变壁纸 + 不透明丝绒卡片 + 翡翠青高光棱。无需实时模糊开销即可获得高氛围感,适合仪表盘、音视频/创作工作台、低配 GPU 环境。

```css
/* 参考内置皮肤: aurora */
--dark: 1; --base: dark;
--primary: #34d399; --primary-hover: #5be0ae; --primary-pressed: #1fb57e;
--text-primary: #ecfdf5; --text-secondary: #a7c9bf; --text-tertiary: #73998f;
--text-inverse: #062118;
--bg-primary: #0a141c; --bg-secondary: #11202b; --bg-tertiary: #182c3a;
--border-primary: #2b4a5e; --border-secondary: #1f3747;
--glass: 0; --gradient: 1; --fx: 0;
--bg-grad-top: #071923; --bg-grad-bottom: #132c3d;
--surface-sheen: 130; --surface-sheen-color: #9bf5d8;
```

排版:数据指标与状态标签采用翡翠/琥珀/珊瑚高辨识状态色,正文保持温和偏冷微白。
验收:关闭 `--glass` 保证纯静态高帧率;卡片与背景梯度反差明确,不糊成一团。

### 13. 柔陶浮雕 Claymorphism

Dieter Rams 工业设计 + 现代陶釉触感:暖陶灰底板 + 左上暖白高光影与右下冷灰投影双侧浮雕 + 珊瑚陶釉强调色。硬件配套应用、创意工具、智能家居中控、个人效率工具。

```css
/* 参考内置皮肤: clay */
--dark: 0; --base: light;
--primary: #e85d3f; --primary-hover: #f27457; --primary-pressed: #cc472b;
--text-primary: #252833; --text-secondary: #585e6f; --text-tertiary: #848b9e;
--text-inverse: #ffffff;
--bg-primary: #e9e6e0; --bg-secondary: #f2efe9; --bg-tertiary: #dfd9d0;
--border-primary: #ccc5ba; --border-secondary: #dbd5cb;
--surface-sheen: 150; --surface-sheen-color: #ffffff;
/* 卡片配合: border: 1 #99ffffff; box-shadow: -3 -3 9 #dcffffff, 4 5 14 #2620283a; */
```

排版:温润清晰,按钮与卡片同属大圆角暖陶家族,输入框采用浅凹槽底色与珊瑚聚焦环形成按压反馈。
验收:左上亮影与右下暗影方向全画面一致(统一左上光源);文字对比度仍 ≥ 7:1,不可退化成模糊灰团。
