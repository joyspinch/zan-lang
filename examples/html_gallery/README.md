# html_gallery —— HTML 设计稿组件全量画廊

用 **.html 设计稿**（原生 HTML 语义 + `data-kind` 协议）声明组件库全部
可声明组件的演示：**77 张卡片、7 个分类页**（组件目录 78 个中的 75 个 +
目录漏登的 CheckboxGroup/ListView），顶部 Tabs 点分类切换（带当前选中
高亮）。窗口 1560×920，四列卡片网格，每张卡片按组件自身特性配置演示
尺寸。

## 运行

```bash
build/zanc examples/html_gallery/App.html examples/html_gallery/App.zan \
    --auto-stdlib -o html_gallery.exe
html_gallery.exe
```

- **设计稿必须是编译输入的第一个文件**（zanc 从第一份设计文档合成
  窗口与控件字段；漏了它 GenForm 不运行，报一片 undeclared identifier）。
- 编译没加 `--subsystem windows`，故意保留控制台：启动自检会逐组件
  打印结果，最后一行 `SELFTEST fails=0 of 77` 即全部建成且类型正确
  （`data-kind` 拼错会静默落 Element 占位，自检让它变成可机检事实）。
  正式发布想要纯窗口，加 `--subsystem windows` 重编即可。
- DPI 自动处理：设计坐标是逻辑像素，150% 缩放下窗口即 2362×1436 物理，
  无需任何代码。

## 覆盖面

组件目录 78 个，本画廊 75 个 + CheckboxGroup/ListView（目录漏登）= 77。
剔除 2 个并说明原因：

| 组件 | 原因 |
|---|---|
| `WebViewBox` / `CefBrowserBox` | 浏览器组件按需求不进画廊 |
| `ChoiceGroup` | 抽象基类：无零参初始化，具体子类 RadioGroup/CheckboxGroup 已各有一张卡 |

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

1. 顶部 Tabs 分类切换（`TabChanged` 驱动 7 个 Page 的 SetShown）；
2. **9 个声明通道喂不活的组件**（泛型 `ListView<T>`、需运行时模型的
   DataGrid/Transfer/Trend、立即模式助手 Wizard/Popover、设计器文档
   节点 FormField 等）在 HTML 里落 Panel 占位壳，由 code-behind 构造
   真控件填进去。其中 Wizard/FormField 卡演示的是组件的真实用法：
   Wizard 的全窗 Render/Show 不是可 Dock 的子控件，卡内嵌它的
   `Wizard.RenderList` 子件 + 实时描述；FormField 是设计器文档节点
   本身无运行时绘制，运行时形态是 `FormBuilder.Build(设计 JSON)`
   实例化的真控件树——卡里按同样方式喂一张迷你表单。
3. 启动自检（`Check(字段, 期望 Kind, id)`）逐组件断言构造正确。

## 这个示例教的事

- `data-kind="组件类名"` 直接实例化注册组件；原生标签按上节语义映射。
- code-behind 是 `partial class <设计稿 body id>` + `static void
  OnLoad(Form form)`：设计稿里每个带 id 的节点都是生成的同名字段
  （`Cats`、`Page0..6`、`DemoButton`…），直接引用即可。
- 自检通道是把"HTML 能不能渲染出组件"变成断言的定式，值得抄。
- 已知命名细节：`SelectBox` 的 `Kind()` 返回 `"Select"`（类名与
  Kind 名不一致）。
- 生成器脚本：画廊设计稿由 `python` 脚本按目录清单批量生成
  （见提交历史 `gen_html_gallery`），逐卡声明组件、尺寸与属性，
  新增组件卡改脚本重新生成即可。
