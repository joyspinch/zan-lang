---
name: gui-design
description: Zan GUI (stdlib/Gui) 的审美与排版规范——对齐、间距、尺寸统一、层级与克制,让智能体不必每次提醒也能写出精美界面。凡是用 stdlib/Gui 写界面(gui_gallery 演示页、examples、IDE 页面、HMI、模板、自定义组件)、改皮肤,或用户提到 好看/美观/精致/精美/对齐/间距/尺寸统一/风格/审美 时使用;界面写完收尾自查也用它。
---

# Zan GUI 界面审美规范

**精美 = 一致 + 克制。** 一致靠档位:库里已经内置了字号、高度、间距三套
全库统一的阶梯 token,你只要永远从档位里取值,界面就自动"齐";克制靠
取舍:一个画面只有一个视觉重心,装饰服务内容。不要手调像素去"看起来对",
要从档位里选,选不出来就说明设计有问题。

深度资料(按需读,本文件是操作主干):

- 立即模式心智模型 / WidgetId / 控件目录:`docs/agent-kb/gui-development.md`
- 样式解析与三条预算棘轮:`docs/GUI_STYLE_RESOLUTION.md`
- Tailwind 原子类全集:`docs/GUI_TAILWIND.md`
- 常见页面搭配模式与反模式:`references/composition.md`(写页面布局前先读)

## 三条尺寸阶梯(硬规则)

token 在 `stdlib/Gui/Theme.zan:223-241` 定义、`Style.zan:746-764` 导出为
`:root` 变量,皮肤可整体改值,所以**永远不要写死数字**。

**字号**:`var(--font-size-tiny/small/medium/large/huge)` = 12/13/14/16/20。
正文 medium(14),辅助/标签 small(13)或 tiny(12),区块标题 large(16),
页面标题 huge(20)。更大的展示数字用 Tailwind 原子类 `text-xl..text-3xl`,
但一个画面至多出现一个超档大字。

**控件高度**:`var(--height-tiny/small/medium/large)` = 22/28/34/40。
按钮/输入框/选择框用 `.tiny/.small/.medium/.large` 皮肤档位类
(`skins/base.css:89-94` 已消费这些 token)。**同一行的操作控件必须同档**;
整块画面主按钮统一 medium,工具条统一 small,别混。

**间距**:一切间距是 4 的倍数(命中 7/13/17 这类值就是错)。
- flex/grid 容器的 gap 用档位类:`gap-none/small/medium/large` = 0/6/8/12
  (`skins/base.css:1771-1776`,值取 `var(--gap-*)`),换肤时整套留白跟着变。
- padding/margin 用 Tailwind 原子类:`p-2`=8px、`p-3`=12px、`p-4`=16px
  (刻度 = n×4px,见 `docs/GUI_TAILWIND.md`)。
- 惯例:卡片内边距 `p-3`/`p-4`,紧凑工具条 `px-2 py-1`,节与节之间
  `gap-large`(12)再往上只有 16/24,不要发明中间值。

## 对齐规则

- **表单行**:标签列固定宽(`Prefer(90, 0)` 之类),输入列吃剩余空间;
  多行表单用同一列宽,所有输入框同高同档。参考 `references/composition.md`。
- **数字右对齐**(金额/计数/尺寸),**标题左对齐**,**操作按钮右对齐**
  (工具条、弹窗脚部一律右侧,主按钮在最右)。
- **垂直居中**:图标 + 文字 + 徽标的同行组合挂 `flex items-center gap-2`;
  控件混排时靠高度档一致来保证基线齐,不靠逐个调 y。
- **网格对齐**:等宽卡片/缩略图用 `grid`(`grid-cols-3 gap-3` 原子类或
  `Grid.Of(n)`),不要手摆 x/y;间隙走 gap 档。
- 容器边缘:相邻区块共享同一条左边界。每个面板的左 padding 必须同值,
  出现两栏标题不共线就是错。

## 层级与颜色

- **层级靠中性色 + 字号阶梯**,不靠加粗和彩色堆砌:
  正文 `var(--text-primary)`,次级说明 `text-secondary`,弱提示/占位
  `text-tertiary`,禁用 `text-disabled`。标题只升字号,不变色。
- **强调色只有一种**:`var(--primary)`。主按钮一个,其余用
  `.secondary/.ghost/.text` 变体;success/warning/error 只表状态,不当装饰。
- **颜色必须走样式层**:控件代码里禁直读 `t.*` 语义色、禁裸
  `0xAARRGGBB`、禁直读字号(三条预算棘轮守门)。Tailwind 原子类只用于
  布局/间距/圆角/阴影,**不用它的调色板**(`bg-slate-500` 这类会绕开
  皮肤主题,换肤即脏);要颜色就写语义类或 `var(--token)`。
- 圆角同档:卡片 `rounded-lg`,按钮/输入框随皮肤(3px 基线);同一画面
  出现 3 种圆角就是没设计。阴影只用 `shadow-sm/md`,弹层才允许 `shadow-lg`。

## 克制(借鉴 anthropics/frontend-design)

- **把大胆花在一处**:一个画面一个签名元素(一块大数字仪表、一张主图、
  一个大标题),其余安静。删掉不服务内容的装饰——"出门前照镜子,
  摘掉一件配饰"。
- **留白是材料**:分组靠间距与分隔线,不靠框套框;拿不准时多留 4px。
  挤满边框的界面 = 层级失败。
- **结构编码信息**:分隔线、编号、眉标只在内容真是序列/分组时用,
  不当装饰贴。
- **空态与错误给方向**:空态用 `Empty` 组件 + 一句"下一步做什么";
  错误说清原因与修法,不道歉不含糊。文案写用户视角的动作——
  "保存更改"而不是"提交",一个动作从头到尾同名(按钮"发布"→
  toast"已发布")。
- **动效一处点睛**:引擎内置 `animate-spin/pulse/breath/shimmer/float/glow`
  关键帧,一个画面至多一处氛围动效;hover 微反馈可以普遍,入场动画不要。
- **拒绝模板脸**:米色底+衬线+赤陶橙、纯黑底+荧光绿这类"AI 默认审美"
  出现时,先问一句这是不是这个题材真正需要的方向。

## 组件封装的反哺

发现问题往库里面修,不在使用处打补丁:

- **铁律:控件自己负责绘制,使用处只配置**(实例化→配属性→喂数据→摆位置)。
  守门测试 `policy_no_widget_drawing` 扫 examples/templates,自绘控件元素
  直接失败。仪表画错了去改 `stdlib/Gui/Component/Chart/ChartViewPie.zan`
  的 DrawGauge,不改 demo。
- 觉得某控件"不精美"→ 修 `stdlib/Gui/Widget/` 或 `skins/base.css` 里
  的规则,让所有使用处一起变好;新皮肤值写进皮肤包的 `:root`,不散落。
- 新增组件遵循 `docs/STDLIB_COMPONENT_STANDARDS.md`(三层架构、链式可选
  配置、API 命名对齐 .NET、必带 conformance 测试);落地清单见
  `docs/agent-kb/gui-development.md`(`Widget/<Name>.zan` → gallery 演示 →
  PropSpec/ControlFactory → 重生 `zform.controls.txt`)。

## 收尾自查(逐条过,任何一条不过就回去改)

1. 全画面字号只来自字号阶梯(含 Tailwind `text-*` 档),没有 13px 之外的即兴值。
2. 同排/同组控件高度同档;按钮不再三种高度并存。
3. 一切间距 ∈ 4 的倍数;gap 用档位类;卡片/区块间隙全画面一致。
4. 相邻区块左边界共线;表单列宽全表统一。
5. 数字列右对齐;操作按钮集中在右侧;主按钮只有一个且最右。
6. 颜色只来自 token/语义类,没有 `slate-500`/裸色值/直读主题字段。
7. 中性色三档承担了全部次级信息,没有用加粗/彩色冒充层级。
8. 圆角、阴影、图标尺寸全画面同档。
9. 至多一个签名元素 + 至多一处氛围动效;其余安静。
10. 空态/加载/错误都有下文(Empty/Spin/具体错误文案),不是白板。
11. 文案:动词具体、全程同名、句式一致(中文界面不中英混排标签)。
12. 皮肤验证:换 dark/light/liquidglass 三个皮肤各看一眼,没有写死的
    颜色残留;玻璃效果用 gallery 深链确认(见下)。

## 验证

- Windows:`scripts\build_gallery.ps1` 必须过;IDE 相关改动再过
  `scripts\build_ide.ps1`(第二道回归网)。
- 视觉检查:构建并深链启动 gallery(`examples/gui_gallery` 支持按组件名/
  皮肤/glass 参数直达),截图对照上面的自查清单——完整流程见
  `testing-gui-gallery` skill;交互类改动用 UiDriver 驱动(见
  `testing-zanide-uidriver` skill),不要手点一次就算完。
- 涉及几何/像素的组件改动,加离屏像素回归(参照
  `tests/conformance/conformance_gui_chart_symbol` 的做法)。
