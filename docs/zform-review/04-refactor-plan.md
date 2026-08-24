# `.zform` + 组件封装 改造优化计划

结论先行：**能承载 `_devin_ref_tmp` 这类工具软件，但要动的不是格式语法，是「组件封装契约」和「运行期脚手架」。**
下面每条诊断都有文件行号；计划按「先止血 → 再补契约 → 再补子项/参数 → 最后才谈语法糖」排序。

---

## 一、诊断：现在的封装到底哪里不合理（全部已核实）

### A1 同一份 `.zform`，编译期与运行期的能力不对等（这是「脚手架缺失」的根因）

| 通路 | 能造出的控件 | 证据 |
| --- | --- | --- |
| 编译期 `GenForm` | **任意类名**，直接 `new <kind>()` | `GenForm.zan:599` `body.Append(vn + " = new " + kind + "();")` |
| 运行期 `FormBuilder` → `ControlFactory` | **24 种**（+ 构建期生成的项目注册表） | `ControlFactory.zan:31-57` 一条 switch |
| 设计器调色板 | **61 种** | `FormField.KindForType` |

后果：一份 `.zform` 编译进项目能跑，交给运行期 JSON 加载器（设计器预览、动态加载、热更、AI 生成后即时预览）就**静默返回 null**。
`ControlFactory` 自己也不自洽：`Create` 能造 `ScrollColumn`/`Divider`，`Kinds()` 里却没有它们（`ControlFactory.zan:17-21` vs `51-53`）。

### A2 `MakeControl` 只搬 5 个属性，其余全丢

`FormBuilder.zan:156-173` 只处理 `name`/`class`/`options`/`placeholder`/`defOn`/`columns`。
**`props` 属性包、`label`、`bind`、`bindProp`、`childTab`、`gap`、`pad` 一个都不传**。
所以「设计器/预览里看到的」和「编译出来的」本来就不是同一个界面 —— 界面数量一多，这个差异就是每天都在坏的那类 bug。

### A3 `Kind()` 与类名不一致，其中一处会丢信息

全 stdlib 只有 3 处不一致，但都在要害上：

| 类 | `Kind()` | 问题 |
| --- | --- | --- |
| `SelectBox` | `"Select"` | `.zform` 写类名、序列化写 Kind()，两套命名 |
| `ScrollColumn` | `"Panel"` | **两个类同一个 Kind()**：存盘后再读回来就变成 `Panel`，滚动能力丢失 |
| `FormField` | `"Field"` | 同 SelectBox |

### A4 子项只能是扁平字符串数组

唯一通道是 `options: ["..."]`（`MakeControl` 里 `JoinOpts`）。
证据：`MainForm.zform` 的文档页签 `"kind":"DocumentTabHost","options":["推广软件"]`，状态栏 `"options":["就绪 · 假数据"]`。
而这套软件的子项是**带属性的对象**：列要 field/width/align/cellType/summary/可编辑，菜单项要图标/快捷键/分隔符/二级子项/enabled。
`DataColumn`（`DataTableModel.zan:10-30`）这些字段**早就齐了**，缺的只是 JSON 化的通道。

### A5 61 种可放置的 kind 里，18 种没有 `Props()` 契约

没有的是：`Table`、`DataGrid`、`Tabs`、`Ribbon`、`Menu`、`ContextMenu`、`ListView`、`VirtualList`、`Wizard`、`Chart`、`Dropdown`、`DatePicker`、`Rate`、`Layer`、`Scrollbar`、`Ellipsis`、`AlarmBanner`、`AlarmList` —— 正好是工具软件的主体。
一个控件没有 `Props()`，就同时失去四样东西：检查器可编辑项、`props` 序列化、`zan_form_schema` 索引条目（AI 只好回去翻源码）、画布上的真实预览。

### A6 项目组件注册表的「无参构造」要求，直接生产了重复代码

注册表由构建期 `scripts/scan_components.ps1` 扫描生成，要求 `@component` + **无参构造** + `Kind()` 匹配类名。
于是泛型/带参组件进不了注册表：`GridPane<AdGroupRow>("主体","onebpSearch","adGroup",GridColumns.AdGroups(),...)` 有 6 个构造参数（含两个 lambda），
只能各写一个无参壳来包 —— 这就是 `AdGroupObjectHost`/`KeywordObjectHost`/`WordPackageObjectHost`/`CrowdObjectHost`/`AdzoneObjectHost`/`CreativeObjectHost` 这 **6 个同构类**的由来，
以及 `ReferenceToolbar`/`WideReferenceToolbar`/`AdzoneReferenceToolbar` 三份复制版工具条的由来。
**不是开发者偷懒，是封装契约不支持传参。** 而 `oneplus/app` 里真正打了 `@component` 的只有 1 处（`src/pages/BrowserPage.zan:7`），其余 13 个自定义 kind 在运行期都造不出来。

### A7 事件无 sender，逼出 13 个 `static current`

设计控件的事件按**名字**打到宿主 Form 的 handler 表（`GenForm.zan:295-296`），回调是无参 `Action`。
多实例复用同一组件时无法识别触发者 → `ReferenceToolbar.current` 这种单例；而 `ReferenceToolbar` 在 Crowd/Keyword 两页各有一个实例，
点「加载报表」读到的可能是另一页的日期控件（代码推断，未上机验证）。

### A8 设计器把画布解算的几何回写进源文档

`Designer.Form.zan:596-640` 的 `ApplyDockLayer` 直接改 `FormField` 的 `fx/fy/fw/fh`。
工具条那 7 个 `dock:4 fw:0`、全员 `fh:360` 就是它生产的。**作者声明的约束和当前画布算出的矩形被存进了同一组字段。**

---

## 二、改造计划

### 阶段 0：止血（不加新特性，1 个会话）

| 项 | 动作 | 验收 |
| --- | --- | --- |
| A8 | `ApplyDockLayer` 改成只写运行时布局缓存（新增 `lx/ly/lw/lh` 或本地变量），源字段只在用户拖拽时改 | 打开→保存一份 `.zform`，`git diff` 为空 |
| A3 | `ScrollColumn.Kind()` 改回 `"ScrollColumn"` 并进 `ControlFactory`；`SelectBox`/`FormField` 的历史别名保留读、只写新名 | 存盘再读回，类型不变；旧文档仍能读 |
| A1 | `ControlFactory.Kinds()` 与 `Create()` 对齐，加守门测试断言两者集合相等 | 新增 policy 测试；故意漏一个应变红 |

风险最低，且不做这三条，后面的工作都会被「保存即污染」抵消。

### 阶段 1：补齐 18 个控件的声明式契约（2 个会话，可分批）

每个控件加 `Props()`/`Events()`/`GetProp`/`SetProp`，顺序按这套软件的用量：
`DataGrid`/`Table` → `Tabs` → `Ribbon`/`Menu`/`ContextMenu` → `ListView`/`VirtualList` → `DatePicker`/`Dropdown` → 其余。

- 只暴露**已经存在的**配置能力（不发明新特性 —— 复用现有为荣）；
- 每批跑一次 `gen_knowledge` + `policy_zform_schema`，schema 条目从 43 涨到 61；
- 验收：`zan_form_schema("DataGrid")` 能查到列/行为属性；检查器里能改；`.zform` 写进 `props` 后编译期与运行期表现一致。

### 阶段 2：子项对象数组 + 组件参数（2 个会话）

1. **`columns: [{field,title,width,align,cellType,sortable,filterable,summary,...}]`** —— 字段对齐 `DataColumn`，等于把已有模型 JSON 化。设计器给一个表格式列编辑器（比逐属性面板更顺手）。
2. **`items: [{label,icon,shortcut,kind:"separator",enabled,items:[...]}]`** —— 菜单/Ribbon/工具条通用，支持二级。
3. **组件参数**：`.zform` 的 `props` 能喂给自定义组件；注册表从「无参构造」放宽为「无参构造 + `SetProp` 初始化」或带参工厂。

验收：把 `GridColumns.zan`(206 行) / `menu/`(290 行) 的静态部分搬进 `.zform`；6 个 `*ObjectHost` 塌成 1 个带参组件；3 份工具条塌成 1 份。
这两条合起来是**收益最大的一步**：现项目 2786 行 Zan 里约 595 行（21%）就是这类「本可声明」的样板。

### 阶段 3：统一建树 + 事件带 sender（2 个会话）

- **A2**：`FormBuilder.MakeControl` 补齐 `props`/`label`/`bind`/`bindProp`/`childTab`/`gap`/`pad`，`fw==0`/DPI 语义与 `GenForm` 对齐；抽出一份共享的「字段→控件」映射，让编译期与运行期共用同一张表，从此**不可能再分叉**。
- **A7**：事件回调带 sender（`Action<Control>` 或 handler 收到触发实例），`static current` 全部可删。这是破坏性变更，需要一次全仓库迁移，但拖得越久代价越大。
- 验收：同一份 `.zform` 走编译期与运行期，截图逐像素比对；`ReferenceToolbar` 在两页各自独立工作（**必须上机实测**）。

### 阶段 4：规模化（按需）

- 写出时省略默认值（现在 39% 的键是默认值；一个 234 控件的窗体会写出 2600+ 行几乎全默认的 JSON）；
- 自定义组件进 `gallery`/`zform.json`，让 AI 查得到项目自己的组件；
- 注册表脱离构建期 PowerShell（跟之前那轮一样改成 Zan 侧生成）。

### 明确不做

**声明式条件键（`showIf`）继续排最后。** 原工程的动态性是「换内容/换页/建树」，用 code-behind 的真 `if` 表达更合适；
在阶段 1-3 完成前做它，只是给一个还没对齐的三条建树路径再加一条要同步的语义。

---

## 三、必须上机实测才能定论的部分

1. `fw==0` 时各控件的 fallback 首选宽度实际表现；
2. 补了 `Props()` 之后设计器画布对 `DataGrid`/`Ribbon`/`Wizard` 的真实预览是否可用（这些控件目前只有占位绘制）；
3. `ReferenceToolbar` 多实例串页的推断；
4. 大数据量下虚表 + 声明式列的性能；
5. 设计器保存后 `.zform` 是否真的稳定（阶段 0 的验收）。

## 四、总代价

阶段 0-3 合计约 **7 个会话**，其中阶段 2 收益最大、阶段 0 风险最低。
做完这四阶段，`.zform` 才是「能描述这套软件的界面」，而不是像现在只描述一层外壳（1276 行 `.zform` : 2786 行 `.zan`）。
