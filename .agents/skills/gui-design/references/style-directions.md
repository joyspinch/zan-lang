# 风格配方卡

九张主流风格方向的现成配方:选一张 → 复制 `:root` 覆写 → 落地成皮肤 →
整屏验收。**先读"选型法"和"能力边界"两节再动手。**

## 用法

1. **选卡**:按下面的选型三问挑一张最贴近题材的;没有完全对的,就选气质
   最近的卡改值——所有卡都是可粘贴的起点,不是标准答案。
2. **落地**:皮肤包 = 一个目录一个 `skin.css`,只覆写 `:root`,零代码。
   两条路:
   - 随应用发布(推荐给用户):在应用工程根放 `skins/<风格名>/skin.css`
     (应用自带皮肤优先于 stdlib 内置皮肤,按 `Skin.zan` 的 Roots 顺序)。
   - 作为 Zan 内置皮肤贡献(在 zan-lang 仓库内):新建
     `stdlib/Gui/skins/<风格名>/skin.css`,会被自动发现;可选配
     `banner.png`(皮肤选择器预览图,fortune 有先例)。
3. **验收**:每张卡末尾 2-3 条 + SKILL.md 的 12 条自查;在 zan-lang 仓库内
   用 gallery 深链 `./gui_gallery <组件名> <皮肤名>` 逐皮肤看。

## 选型三问(决定风格方向前先回答)

1. 这个产品是什么题材、谁在用、这屏要完成什么?——从题材自己的身体记忆
   (材质、器物、行话)里找方案,不从"好看的模板"里找。
2. 签名元素放哪?——一个画面只有一处大胆(一块发光数据 / 一面超大标题 /
   一个插画位),其余全部安静。
3. 这是不是题材真正需要的,还是 AI 模板脸?——米色+衬线+赤陶、纯黑+荧光绿、
   报纸细线三张"默认脸"出现时,先问第 1 题的答案。

## 能力边界(卡里的配方不会越界,但你要知道)

- **无字体家族 token**:字体个性只能靠字重(`font-thin..font-black`)、
  字距(`tracking-*`)、大小写(`uppercase`)和字号档来表达。
- 渐变只有 4 个方向、最多 3 个色标;阴影是单层(不是浏览器双层)。
- 动效关键帧只有内置 8 个:`animate-spin/pulse/breath/shimmer/float/glow/aurora/motes`。
- 有 token 可调的:全套语义色、圆角三档(`--border-radius-*`)、边框宽、
  阴影色、风格基因开关(`--brutal/--neu/--glass/--gradient/--fx`)、
  玻璃全家(`--glass-blur/--glass-tint/--glass-sheen/...`)、
  以及 `--font-size-*/--height-*/--gap-*` 三套档位(皮肤可整体改值)。
- **永远不要用 Tailwind 调色板色**(`bg-slate-500` 这类):绕开主题,换肤即脏。

---

### 1. 商务专业 Corporate

干净、可信、零情绪。报表/后台/SaaS 管理端的默认选择。

```css
/* 基线:light */
--dark: 0; --base: light;
--primary: #2b5fd9; --primary-hover: #3d6ee0; --primary-pressed: #1f4bb8;
--info: #2b5fd9; --success: #1f8a4c; --warning: #b7791f; --error: #c0392b;
--text-primary: #1c2430; --text-secondary: #46536b; --text-tertiary: #77839b;
--bg-primary: #f5f7fa; --bg-secondary: #ffffff; --bg-tertiary: #eef1f6;
--border-primary: #d7dde8; --border-secondary: #e4e9f1; --divider: #e4e9f1;
--border-radius-small: 4; --border-radius-medium: 8; --border-radius-large: 12;
--border-width: 1; --shadow-color: #1c2430;
```

排版:标题 `font-semibold`+`text-primary`,正文默认 medium,说明文字一律
text-secondary;不用 uppercase/加宽字距。动效:只有 hover 的
`transition duration-150`。文案:完整句式、零语气词。
验收:除 primary 外全画面不出现第二块彩色;阴影只在弹层。

### 2. 街头/嘻哈 Streetwear

粗野主义骨架:直角、粗边框、高对比、硬阴影。球鞋/潮牌/音乐类。

```css
/* 基线:brutalism */
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

排版:标题 `uppercase tracking-widest font-black`,只一处(`text-2xl` 级);
按钮 `uppercase`。动效:无入场,hover 硬切(`duration-75` +
`translate-x-0.5`)。文案:短促、口号式,但动作名仍写具体("下单"不是"冲")。
验收:圆角必须全 0;边框 2px 全画面统一;签名 = 一块贴纸式大标题。

### 3. 赛博朋克 Cyberpunk

近黑底、荧光青主色、品红点缀。数据监控/游戏/极客工具。

```css
/* 基线:neon */
--dark: 1; --base: dark;
--primary: #00f0ff; --primary-hover: #4df5ff; --primary-pressed: #00b8cc;
--info: #ff2bd6; --warning: #ffe600; --error: #ff2b4e; --success: #00ff9d;
--text-primary: #e8f6ff; --text-secondary: #9fb8cc; --text-tertiary: #5c7488;
--bg-primary: #070b12; --bg-secondary: #0d1420; --bg-tertiary: #131d2e;
--border-primary: #1f3247; --border-secondary: #16222f; --divider: #16222f;
--border-radius-small: 0; --border-radius-medium: 2; --border-radius-large: 4;
--border-width: 1; --fx: 1; --shadow-color: #00f0ff;
```

排版:小字密排(`text-xs`/tiny 档大量)+ 一处超大数字,天然等宽气质
(无字体 token,用字距和字号落差顶)。动效:`animate-glow` 一处或
加载位 `animate-shimmer`,别处静止。文案:冷峻、术语密度高、不用感叹号。
验收:荧光色只能当 accent/描边,永远不许当大面积底;玻璃关闭。

### 4. 暗金豪华 Luxe

近黑 + 金线 + 大字距。高端消费/金融/收藏展示。

```css
/* 基线:darkgold */
--dark: 1; --base: dark;
--primary: #d4af37; --primary-hover: #e0c05a; --primary-pressed: #b8952b;
--text-primary: #f2ead6; --text-secondary: #c9bfa4; --text-tertiary: #8f866f;
--bg-primary: #12100c; --bg-secondary: #1a1712; --bg-tertiary: #242017;
--border-primary: #3a3223; --border-secondary: #322c1f; --divider: #322c1f;
--border-radius-small: 2; --border-radius-medium: 4; --border-radius-large: 6;
--border-width: 1; --gradient: 1; --bg-grad-top: #26200f; --bg-grad-bottom: #12100c;
--shadow-color: #000000;
```

排版:eyebrow 小标签 `uppercase tracking-widest`,大标题 `font-light` 级
(细字重=高级),行距放宽。动效:至多一处 `animate-shimmer`(金线扫过)。
文案:名词性、低声、不用惊叹号与口语。
验收:金色只出现在强调位与细描边;大面积底永远近黑;圆角 ≤6。

### 5. 国风 Guofeng

宣纸底 + 朱砂 + 黛青,大留白多分隔线。文化/茶饮/中式品牌。

```css
/* 基线:chinese */
--dark: 0; --base: light;
--primary: #b03a2e; --primary-hover: #c1483c; --primary-pressed: #8f2e24;
--info: #34557a; --success: #4a7c59; --warning: #c98a1b; --error: #b03a2e;
--text-primary: #2b2620; --text-secondary: #5a5248; --text-tertiary: #8c8377;
--bg-primary: #f7f3e9; --bg-secondary: #fffdf7; --bg-tertiary: #efe8d8;
--border-primary: #d8cfbc; --border-secondary: #e5ddca; --divider: #d8cfbc;
--border-radius-small: 4; --border-radius-medium: 8; --border-radius-large: 12;
--border-width: 1; --shadow-color: #8f2e24;
```

排版:标题 `tracking-widest`(汉字加宽字距是国风关键),标点全角;
区块题用分隔线+字距替代边框。动效:`animate-breath` 一处(印章式徽标)。
文案:四字短语、敬语克制;数字场景慎用花哨图表配色。
验收:朱砂只当 accent 与印章 badge;留白比默认大一级(`p-4`、`gap-large`
起步);divider 线多用。

### 6. 极简日式 Zen

灰阶 + 一点朱红,大留白,无阴影无动效。阅读/笔记/专注工具。

```css
/* 基线:mono —— 这张卡展示皮肤连间距档都能整体换 */
--dark: 0; --base: light;
--primary: #c92a2a; --primary-hover: #d63c3c; --primary-pressed: #a81f1f;
--text-primary: #1c1c1c; --text-secondary: #555555; --text-tertiary: #9a9a9a;
--bg-primary: #fafaf8; --bg-secondary: #ffffff; --bg-tertiary: #f0f0ec;
--border-primary: #e2e2de; --border-secondary: #ebebe7; --divider: #e2e2de;
--border-radius-small: 4; --border-radius-medium: 6; --border-radius-large: 8;
--border-width: 1; --shadow-color: #1c1c1c;
--gap-small: 8; --gap-medium: 12; --gap-large: 20;
```

排版:正文降一档用(small 做正文),标题不加粗(`font-normal`);
层级完全交给字色和间距。动效:零。文案:极短,名词结尾。
验收:除朱红一点外无彩色;阴影不可见;整屏"空"是对的,不是没做完。

### 7. 玻璃拟态 Frosted

半透明面板压在壁纸/渐变上。展示型首页、启动器、控制中心。

```css
/* 基线:liquidglass —— glass 家族 token 从基线继承,只动这些 */
--dark: 1; --base: dark;
--primary: #7cc7ff; --primary-hover: #9ad5ff; --primary-pressed: #58a8e6;
--text-primary: #ffffff;
--border-radius-small: 8; --border-radius-medium: 14; --border-radius-large: 18;
--border-width: 1; --fx: 1;
```

`--glass-blur/--glass-tint/--glass-sheen/--glass-shadow-*` 照 liquidglass
基线微调:想更雾加 blur,想更"高级灰"降 tint。
排版:玻璃上的字禁止 tiny 档(可读性优先);面板层级用圆角档拉开。
验收:玻璃后面必须是壁纸/渐变/背景特效——纯单色底上开玻璃是白开;
逐面板检查文字对比度。

### 8. 新拟态 Neumorphism

控件从同色底面"鼓出来",无边框。玩具/家居/温和场景,慎用:
可访问性天然弱。

```css
/* 基线:light(无内置拟物皮肤,从零覆写) */
--dark: 0; --base: light; --neu: 1;
--primary: #5b7cfa; --primary-hover: #6d8bfb; --primary-pressed: #4a69e8;
--text-primary: #3d4a5c; --text-secondary: #6b7889; --text-tertiary: #97a2b3;
--bg-primary: #e6eaf1; --bg-secondary: #e6eaf1; --bg-tertiary: #dde2eb;
--bg-hover: #eceff5;
--border-primary: #e6eaf1; --border-secondary: #e6eaf1; --divider: #d7dce6;
--border-width: 0;
--border-radius-small: 10; --border-radius-medium: 14; --border-radius-large: 18;
--shadow-color: #b8c0cf;
```

诚实注:引擎阴影单层,拟物经典"亮暗双影"只能靠 `--neu` 基因近似,
效果以预览为准;不到位就接受"软扁"取向,别硬造。
验收:`bg-primary` 必须等于 `bg-secondary`(同色是命);无边框;
禁用此风格的场景清单:数据密集表、需要高对比的用户。

### 9. 卡通波普 Pop

高撞色、粗黑边、大圆角、硬阴影。儿童/活动页/品牌周边。

```css
/* 基线:light(无内置波普皮肤,从零覆写)—— 这张卡展示字号档也能换 */
--dark: 0; --base: light;
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

排版:标题 `font-black`;badge/tag 大量使用(走皮肤状态类 success/warning
等,不是 Tailwind 调色板)。动效:`animate-float` 一处
(吉祥物/图标),`animate-breath` 给在线状态。文案:短句 + 轻语气词,
动作按钮仍写具体动作。
验收:2px 粗边框全画面统一;硬黑阴影统一;签名 = 一个插画位/超大图形。
