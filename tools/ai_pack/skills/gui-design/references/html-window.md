# HTML 窗口开发定式（.html 设计稿 = 唯一格式）

窗口的布局/内容/样式进设计稿，交互与数据在 code-behind。旧 `.zform`
通道已删（GenForm 只认 .html）——别再写、别再提，见到旧稿用设计器
往返转一次即可。

## 两条建树通道（同语义，选一条）

- **编译期**：设计稿作为编译输入，GenHtml 合成 Main 与控件字段。
  **设计稿必须是命令行第一个文件**——zanc 从第一份设计文档合成入口，
  放后面 = 字段全缺（模板门配方：html 首参 + 全部 src/*.zan）。
- **运行时**：`App.LoadHtml(...)` 同一解析器动态建树。切页出口里
  装每页内容时用这条。

## 声明面五件套与硬边界

| 原语 | 作用 | 硬边界（超了就静默不生效，别猜） |
|---|---|---|
| `data-on-click="名字"` | 事件 → 处理器注册表 | 设计稿只落名字；逻辑在 code-behind |
| `data-arg="字面量"` | 带参事件（列表行共用处理器区分来源） | 只支持字面量，不求值表达式 |
| `data-bind="路径"` | 绑 JsonValue 模型字段 | Element 缺省绑 text；真控件缺省 value |
| `data-if="路径"` | 显隐插拔 | **只认路径真值**（null/假/"false" 为隐），不支持 `a==b` 比较表达式 |
| `<template data-for="路径">` | 数组行克隆 | 嵌套模板不支持；行内 bind/bindIf 以本项为第一作用域、回落根模型 |

坑出处：data-if 的真值语义见 ChildWindow.Truthy——想按 `page=="bag"`
切面板的人都在这里撞墙，解法见下节。模板原型子树靠 UA 样式
`template{display:none}` 隐藏；克隆行撤除走文档序表防幽灵占位。

## 多按钮控制中间区（切页三路）

1. **推荐：Nav.Embed 做骨架 + 每页 .html 内容**。`Gui/Nav.zan` 的
   Embed（Tabs-as-outlet）：路由表=按钮排，中间区=出口；每页首次
   选中才实例化（惰性）、默认切走保留（keepAlive）、可按路由配临态
   （切走销毁）。放置/面板类游戏的"切走保留"和它默认值天然一致。
2. **纯声明近似（页少面板轻）**：每按钮 `data-on-click="go"
   data-arg="bag"` + 一个处理器翻模型 + 面板挂 `data-if="showBag"`。
   页一多（十个布尔字段）就笨，别硬拼。
3. 声明式出口原语（`data-nav` 直连路由）v1 没有——想要"零接线"
   先确认它是否已落地，没有就走路 2。

## 布局：按浏览器等价子集写，别按浏览器全量写

引擎支持 flow / inline 行盒 / flex / grid / float / overflow 滚动，
经 Chrome getBoundingClientRect 逐盒对比（oracle）校准，多数场景
0px 偏差。CSS 方言之外的东西写上去不报错也不生效——动手前查支持度
审计脚本与 HTML_UI.md 的与浏览器差异清单；"改了没反应"先怀疑
没进方言，再怀疑优先级。

## CSS 两层与基线坑

- 皮肤基线（base.css，`flex{display:flex}` 等 UA 规则）+ 应用级
  （`app.UseAppCss(...)`）。颜色走 `var(--*)` 令牌，尺寸走阶梯。
- **项目自带 `skins/` 目录要连 base.css 一起带**：`--embed skins`
  见到同名目录就整体跳过 stdlib 基线内嵌，只放自家 skin.css 会让
  基线层整层丢失、flex 全退化成叠矩形（无为修仙传百艺页签全叠的根因，
  模板补带 base.css 后消除）。
- 运行时换肤是实例方法：`form.GetApp().UseSkin("neon")`——按静态
  `App.UseSkin(...)` 调会报 cannot call instance method（踩过）。

## 模板实例化（gui-* 模板带 `{{NAME}}` 占位符）

- `{{NAME}}` 在**两处**都要替换：`src/App.html`（根元素
  `id="{{NAME}}"` 与 `data-win-title`）和 `src/App.zan`
  （`partial class {{NAME}}`），且两个入口文件要改名为
  `<NAME>.html`/`<NAME>.zan`（zan.proj `entry = src/{{NAME}}.html`
  按名对上）。只换 .zan 不换 .html，GenForm 合成的
  `<NAME>.OnLoad` 找不到方法、模板内组件字段全裸（只复制
  App.zan/App.html 各一份编译 gui-wechat 这类多文件模板也会
  undefined type——整个 src 都要带）。
- 参考实例化脚本（仓库 `scripts/e2e_pipeline.ps1` 的做法）：整树
  拷贝 + 替换 + 用 zan.proj 编译，别手搓单文件。

## 验证定式

- 纯逻辑（Html.Parse/绑定/同步）headless 可跑，无窗口：conformance
  形态的探针 + 金样（CRLF）。
- 布局几何：oracle 逐盒对比（同一份 .html 喂 Chrome 与引擎，
  getBoundingClientRect vs Arrange）。
- 手工编 GUI 设计稿程序：设计稿 html 首参 + 全部 src + 
  `--subsystem windows`；跑起来截图对照（锚定 PID），交互用
  UiDriver 可重复驱动。

## 游戏接入边界

菜单/面板/商店这类低频交互面可嵌进游戏帧循环（同一引擎，脏区门控，
实测 clean/dirty 均值 3ms、峰值 23ms、预算 16.6ms，空闲 120 拍仅
3 帧重绘）；**每帧热路径 HUD（血条/伤害数字/小地图）仍走代码直绘
+ 门控渲染，不进 DOM**——那是性能边界，不是能力缺口。
