# html_gallery —— HTML 设计稿组件全量画廊

用 **.html 设计稿**（原生 HTML 语义 + `data-kind` 协议）声明组件库全部
可声明组件的演示：**86 张卡片、8 个分类页**（组件目录 78 个 + 目录漏登的
CheckboxGroup/ListView + 目录外可独立演示的 Dropdown/RichText/ChatView/
PropertyGrid/Grid/FileTree/ScrollColumn/Flex/FormGroup），顶部 Tabs 点
分类切换（带当前选中高亮）。窗口 1560×920，四列**高卡降序瀑布流**（每列
独立记账、卡片落最矮位置、高卡片先落位保证页底收平），DataGrid/Transfer/
ChatView 等大型组件跨两列，每张卡片按组件自身特性配置演示尺寸。

## 运行

```bash
build/zanc examples/html_gallery/App.html examples/html_gallery/App.zan \
    --auto-stdlib -o html_gallery.exe
html_gallery.exe
```

- **设计稿必须是编译输入的第一个文件**（zanc 从第一份设计文档合成
  窗口与控件字段；漏了它 GenForm 不运行，报一片 undeclared identifier）。
- 编译没加 `--subsystem windows`，故意保留控制台：启动自检会逐组件
  打印结果，最后一行 `SELFTEST fails=0 of 86` 即全部建成且类型正确
  （`data-kind` 拼错会静默落 Element 占位，自检让它变成可机检事实）。
  正式发布想要纯窗口，加 `--subsystem windows` 重编即可。
- DPI 自动处理：设计坐标是逻辑像素，150% 缩放下窗口即 2362×1436 物理，
  无需任何代码。

## 覆盖面

组件目录 78 个 + 目录漏登 2 个（CheckboxGroup/ListView）+ 目录外 9 个
（Dropdown/RichText/ChatView/PropertyGrid/Grid/FileTree/ScrollColumn/
Flex/FormGroup，都是可独立实例化演示的真控件）。其中 Flex/FormGroup 与
Grid/GridItem/ScrollColumn/CheckboxGroup/ChartHost 一样没登进
zform.controls.txt 目录——该目录是 `GenKnowledge --write-controls` 的
生成物，只收声明了 `override Props()` 的控件类；这些布局容器/宿主类
没有 Props() 可声明，但 `data-kind` 声明与 ControlFactory 创建都认它们。
剔除的组件与原因：

| 组件 | 原因 |
|---|---|
| `WebViewBox` / `CefBrowserBox` | 浏览器组件按需求不进画廊 |
| `ChoiceGroup` | 抽象基类：无零参初始化，具体子类 RadioGroup/CheckboxGroup 已各有一张卡 |
| `Layer` / `ContextMenu` / `Menu` / `Tooltip` / `LogView` | **宿主驱动或即时模式形态**，没有可 Dock 进卡片的 retained 实例：Layer 是 `LayerState` + 静态 `Alert/Confirm/Choose` 由宿主帧循环渲染（目前只有 IDE 壳接入）；ContextMenu/Menu/Tooltip 在 Paint 期即时模式绘制；LogView 是 IDE 日志面板的宿主件。演示它们的正确方式是接了宿主的应用（IDE/游戏），不是一张静态卡片 |
| `ScrollView` / `StyledText` | **每帧驱动的宿主件**（私有构造、无 `Kind()`/子件 Add）：ScrollView 是 `Begin(app, area, contentH)/End` 逐帧调用的滚动助手（`ScrollColumn` 卡已演示它的 retained 包装）；StyledText 是 IDE 聊天区每帧 `Bind` 的行网格渲染件。判别法：类头注释写"每帧由调用方…"、只有 Begin/End/Bind 没有 Dock/Add 的，就是喂不活的宿主件 |
| `CodeEditor` / `Wizard` 全窗形态 / `FormField` 文档节点 | CodeEditor 与 Dock 布局系依赖 IDE 宿主环境；Wizard 的全窗 `Render/Show` 不可 Dock（卡内已嵌它的 `RenderList` 子件）；FormField 是设计器文档节点，运行时形态 `FormBuilder.Build` 已在卡中演示 |

## 原生 HTML 语义

设计稿不是"全 div + data-* 的类 HTML"，原生标签直接映射组件语义，
与 `data-*` 通道等价、可混用：

- `<button class="primary">点我</button>` —— 文本内容即组件 text；
- `<input placeholder="请输入用户名">`、`<textarea>…</textarea>`；
- `<img src="…" alt="图">` —— 图片内容放 src（data-uri 也行）；
- `<label>普通标签文本</label>`；
- 复合属性仍走 `data-x-<键>='<JSON>'`（如 `data-x-props`、
  `data-x-options`），几何走 `data-fx/fy/fw/fh`。

## code-behind 职责分界

结构全部在 `App.html` 声明；`App.zan` 只做三类事：

1. 顶部 Tabs 分类切换（`TabChanged` 驱动 8 个 Page 的 SetShown）；
2. **声明通道喂不活的组件**（泛型 `ListView<T>`、需运行时模型的
   DataGrid/Transfer/Trend、立即模式助手 Wizard/Popover、设计器文档
   节点 FormField、「更多组件」页的 Dropdown/RichText/ChatView/
   PropertyGrid/FileTree/ScrollColumn/Flex/FormGroup 等）在 HTML 里落
   Panel 占位壳，由 code-behind 按"本地构造 → 喂数据 → Dock → Add 进壳"
   的定式填进真控件。其中 Wizard/FormField 卡演示的是组件的真实用法：
   Wizard 的全窗 Render/Show 不是可 Dock 的子控件，卡内嵌它的
   `Wizard.RenderList` 子件 + 实时描述；FormField 是设计器文档节点
   本身无运行时绘制，运行时形态是 `FormBuilder.Build(设计 JSON)`
   实例化的真控件树——卡里按同样方式喂一张迷你表单。
3. 启动自检（`Check(字段, 期望 Kind, id)`）逐组件断言构造正确。

## 这个示例教的事

- `data-kind="组件类名"` 直接实例化注册组件；原生标签按上节语义映射。
- code-behind 是 `partial class <设计稿 body id>` + `static void
  OnLoad(Form form)`：设计稿里每个带 id 的节点都是生成的同名字段
  （`Cats`、`Page0..7`、`DemoButton`…），直接引用即可。
- 「喂不活的控件」定式：壳字段类型是 Panel（只有 Add 没有 Bind），
  在 code-behind 里 `new 真控件()` → 喂数据（Bind/SetMarkup/…）→
  `Dock(Dock.Fill())` → `壳.Add(控件)`。
- 瀑布流定式：**高卡片降序先行**（次级键跨列卡优先），单卡抢先落位
  会顶高跨列卡的落位窗、页底收不平；每列独立记账 `col_h`、落最矮
  窗口，收口断言 `max(col_h)-GAP ≤ CONTENT_H`。
- 页签条几何要对齐 `theme.heightMedium`（34）：Tabs 的条底线画在控件
  整框底部，控件fh 比页签条自然高大出几像素，就会出现"两条下划线"。
- 文件系统演示数据别写死相对子目录（`FileTree.Nodes("tools",…)` 从
  别的工作目录启动就是空树）；扫 `"."`（启动目录）到哪都有真内容。
- 富文本演示挑亮底可见的快捷色（#R/#B/#H/#L）；#W 白、#Y 纯黄是深色
  游戏底的色，亮色卡片上一画就隐形。未写色码的段落用皮肤
  `richtext { color: … }` 规则的前景（base.css 已补，组件兜底白色是
  深色底习惯）。
- 自检通道是把"HTML 能不能渲染出组件"变成断言的定式，值得抄。
- 已知命名细节：`SelectBox` 的 `Kind()` 返回 `"Select"`（类名与
  Kind 名不一致）。
- 生成器脚本：画廊设计稿由 `python` 脚本按目录清单批量生成
  （见提交历史 `gen_html_gallery`），逐卡声明组件、尺寸与属性，
  新增组件卡改脚本重新生成即可。
