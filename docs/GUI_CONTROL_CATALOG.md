# 控件目录（ControlCatalog）——编译器生成的控件清单（B-ID172 ① 小设计）

状态：已落地（2026-11-23，选项 A）。取代 `ControlBootstrap` / `DataTableBootstrap` /
`CefControlBootstrap` 等手工 `ControlFactory.Register` 注册梯子（2026-11-09 裁定：
`Install()` 类注册方案不可行，移除）。

## 现状与问题

- 按名构造控件（HTML 标签、设计器文档、`Html.Clone`、FormBuilder）走
  `ControlFactory.Create(kind)`，查的是运行期手工注册表。
- 注册表靠各包的 `XxxBootstrap.Install()` 手写 kind→构造器名单（Zan.Gui 一份 74 条，
  DataTable/Cef 各一份），仓内 87 处调用方要记得调用；漏调则按名构造静默返回 null。
- `ControlBootstrap` 在 Core 层引用全部 Widget/Component（向上依赖），并 `using Gui.Hmi`
  牵连兄弟包——这是 Zan.Gui 拆包（B-ID172 断包环）的直接障碍。
- `Install()` 同时承担「把全部标准控件拉入编译图」的职责（按需拉入下，
  只被字符串引用的类型不会被编译）。`Form.Show` 内部就调用了它，所以实际上
  所有 Form 应用都会拉入全部标准控件。

## 落地实现

- **合成**（`src/compiler/driver/main_synth.c` `zan_synth_control_catalog`）：编译单元含
  `Gui.ControlFactory` 时，扫描全部类声明，凡 public（非 internal/private）、非抽象、
  非静态、继承链上有 `Gui.Control`、有可访问无参构造器（或未声明构造器）的类按
  类名入目录；生成 `__ZanControlCatalog.Create(kind)`（逐 kind 字符串比较）与 `Kinds()`。
- **`[ControlKind]`**：`Name`（额外 kind，默认类名）、`Factory`（静态工厂方法名）、
  `Of`（泛型闭合实参）。泛型控件只有声明了 `Of` 才入目录。
- **拉入**（`src/compiler/driver/prune_reach.c` `pi_catalog_*`）：`zan.pkg` 顶层
  `catalog = "Ns1 Ns2"` 声明目录命名空间。程序引用了 `ControlFactory`、且某个已编译文件
  `using` 了该命名空间时，**声明包自己的**、声明命名空间恰为该命名空间的文件整体拉入。
  兄弟包写进同一命名空间的文件（如 Zan.Gui.Workplace 的 `Gui.Widget`）不随之拉入；
  「目录被扫描到」不等于「程序在用」——只认 `using`。
  - Zan.Gui：`Gui.Widget Gui.Component`（HTML 装载层 `using` 了两者，所有 HTML/Form 程序都有）。
  - Zan.Gui.DataTable：`Gui.Component.DataTable`；Zan.Gui.Browser：`Gui.Component.CefBrowser Gui.Component.WebView`
    ——应用 `using` 其命名空间即可在 HTML 里按名构造，取代原 `XxxBootstrap.Install()`。
- **ControlFactory**：`Create` 先查运行期 `Register`（动态组件，优先）、再查目录，
  精确匹配后大小写不敏感；未命中仍返回 null（HTML 装载据此回落通用 `Element`）。
  新增 `Require(kind)`：未命中抛异常并指明 kind 不在编译图目录。`Kinds()` 合并去重。
- **退役**：`ControlBootstrap`、`DataTableBootstrap`、`CefControlBootstrap` 删除，仓内
  `Install()` 调用全部移除（需要 DataTable/CEF 的入口改为 `using` 其命名空间）。
  `WebViewBootstrap.Install()` 只剩链接导航器注册。
- **体积**：`control_factory_equivalence` 编译图 806 → 857 个文件（`Gui.Widget` 目录里
  原名单外的 38 个文件及其依赖，如 PinchZoomView/GestureDetector/Wizard）。PinchZoomView
  双击反馈改为只触感，避免每个 Form 程序链接音频驱动。
- **新入目录控件的引用环**：Wizard/Prompt 原先不在名单里、从未被按名构造，子按钮 Click 闭包
  强捕获宿主成环（`html_props_equivalence` 逐 kind 构造后泄漏报出）；改为 `WizardAction` /
  `PromptAction` 持 `weak` 宿主（同 `TabsAction`）。
- **验证**：`conformance_gui_control_catalog`（用户控件自动入目录、别名工厂、泛型闭合、
  抽象/无无参构造/internal 排除、大小写、Require、Register 优先）。

## 设计

### 1. 编译期发现（照 `__AttrRoutes.Register` 控制器发现的先例）

checker 在编译单元里收集**控件类**：非抽象、继承链上有 `Gui.Control`、
有可访问的无参构造器（或带 `[ControlKind]` 工厂，见 3）。在唯一的调用点
`ControlCatalog.Synthesized()` 原地合成一个工厂委托实参（合成静态类
`__ZanControlCatalog`，`Create(string kind)` 是按 kind 的字符串分派、`Kinds()`
返回名单），与 `__ZanActionDispatch` 同一合成路径（`src/compiler/driver/main_synth.c`）。

- kind = 类名（与 `Control.Kind()` 默认值一致，见阶段 2b）；大小写不敏感匹配保留。
- 泛型控件不参与默认发现；需要时用 `[ControlKind]` 指定闭合类型（见 3）。
- 用户项目自己的控件（`class MyCard : Control`）同样自动入目录——不再需要注册。

### 2. 编译图范围（拉入策略）——已选 A

| 选项 | 行为 | 代价 |
|---|---|---|
| **A（推荐）声明式目录命名空间** | 包清单声明哪些目录是「控件目录」（如 Zan.Gui 的 `src/Gui/Widget`、DataTable 的组件目录）；程序一旦用到按名构造（引用了 `ControlFactory`/`Html` 装载/设计器），这些目录整体拉入编译图 | 与今天 `Form.Show → Install` 的实际效果等价，零行为变化；二进制体积不变 |
| B 严格编译图 | 只有程序代码里实际引用过的控件类型才可按名构造；按名构造未编译的 kind 抛异常 | 体积最小；但运行期装载 HTML/设计文档的应用要自己引用用到的控件类型，否则运行期报错 |

按名构造未命中：`Create` 保持返回 null（HTML 未知标签回落 `Element` 依赖它），
必须命中的调用方用新增的 `ControlFactory.Require`（抛异常，消息指明 kind 不在目录中）。

### 3. 别名与特殊构造：`[ControlKind]`

写在控件类上，取代注册梯子里的特例分支：

```
[ControlKind(Name = "RadioButton", Factory = "ButtonForm")]  // 额外 kind，静态工厂方法
public class Radio : InteractiveControl { … public static Radio ButtonForm() { … } }

[ControlKind(Of = "string")]                                 // 泛型控件按闭合类型入目录
public class ListView<T> : Control { … }
```

### 4. 退役与迁移

- 删除 `ControlBootstrap`、`DataTableBootstrap`、`CefControlBootstrap`；
  `ControlFactory.Register` 仅保留给运行期动态组件（用户组件 `UserComponentRegistry`）。
- 87 处 `XxxBootstrap.Install()` 调用删除（`WebViewBootstrap` 另有链接导航器安装，保留并改名为
  `WebViewLinks.Install()`）。
- Core 不再引用 Widget/Component（ControlBootstrap 的 74 处向上依赖与 `using Gui.Hmi` 一并消失）。

### 5. 验证

- conformance：用户自定义控件自动入目录；`[ControlKind]` 别名与泛型闭合；未命中抛异常；
  不同编译单元/包的控件均可发现。
- 等价：`control_factory_equivalence`（名单与构造结果逐 kind 对齐）、HTML/设计器相关用例。
- 选项 A 下二进制体积对照不变。
