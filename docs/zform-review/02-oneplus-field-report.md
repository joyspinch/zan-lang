# `.zform` 线上缺陷实测报告（依据 `_devin_ref_tmp\One` 与 `oneplus\app`）

结论先行：这个项目暴露的缺陷**几乎没有一条来自「JSON 还是 HTML」**，也没有一条能被 `showIf` 解决。
真正的伤口是三处：**同一份文档有三条互不一致的建树路径**、**设计器会把画布尺寸回写进源文档**、**自定义组件包没有一等公民的表达方式**。

---

## 1. 两侧的真实体量

原 WinForms 工程（迁移来源）：

| 项 | 数字 |
| --- | --- |
| `.cs` | 336 |
| `.Designer.cs` | 201（`One\` 下 167） |
| 设计器代码总字节 | 4.85 MB |
| 单个最大窗体 | `One\Search\FormOptimizeConfigEdit.Designer.cs` 186 KB / 3143 行 |
| 该窗体控件实例 | 234 个 `new System.Windows.Forms.*`，227 次 `Controls.Add` |
| 触碰 `.Visible =` 的文件 | 118 个 |

Zan 侧现状：

| 项 | 数字 |
| --- | --- |
| `.zform` 文档 | 16 |
| 字段（含嵌套 kids） | 89 |
| 字段键总数 | 1002，其中 **39% 是默认值** |
| 全部 `.zform` | 约 28 KB / 1500 行 |
| `"bind"` 非空 | **0** |
| `on<Event>` 键 | **0** |
| 代码里 `SetShown` | **1**（`BrowserPage.zan:57`） |
| 代码里 `.Add(/Clear()/Remove` | 289 处 |

读法很直接：**`.zform` 在这个项目里只承担了「静态外壳 + 具名控件」**，事件、状态、可见性、内容全部落在 Zan 代码里。
`.zform` 唯一的声明式动态能力（`bind`/`bindProp`）**一次都没用**。所以「给格式加条件键」对这个项目的收益是零 —— `showIf` 的语义要挂在 `bind` 的状态模型上，而这个项目根本没进那个模型。

---

## 2. 缺陷清单（按严重度）

### D1（P0）同一份 `.zform` 有三条建树路径，语义不一致

三个消费者：设计器画布（`Designer.Form.zan` 自己手绘）、`FormBuilder`（运行时 JSON 载入 / 预览）、`GenForm`（编译期生成代码）。

字段键消费对照（grep 得到，非推测）：

| 字段键 | GenForm | FormBuilder |
| --- | --- | --- |
| `bind` / `bindProp` | 读 | **不读** |
| `required` | 读 | **不读** |
| `childTab` | 读（`SlotHost(i)`） | **不读** |
| `props` | 读 | **不读**（只硬编码 options/placeholder/defOn/columns） |
| `gap` / `pad` / `of` | 读 | **不读** |
| `activeTab` | **不读** | **不读**（只有设计器 Inspector 读写） |

同一语义两处实现还不一样：
- `dock:3/4` 且 `fw==0`：`GenForm` **跳过** `Prefer`（用控件自身首选宽），`FormBuilder` 无条件 `Prefer(fw,0)` → **宽度 0**。
- DPI：`GenForm` 每个几何量乘 `__dp/100`，`FormBuilder` 直接用原始像素 → JSON 载入路径不缩放。

后果：`childTab` 被忽略意味着放在 Tabs / 分栏「某一页」的子控件在运行时载入路径下全部重叠或不参与布局 —— 这正是 `GenForm` 自己在注释里警告的那种失败。`props` 被忽略意味着所有透传属性在该路径下静默丢失。

而设计器画布只对 **10 种基础输入控件**（`ftype 0..9`）用真控件预览（`Designer.Form.zan:2160` 起，走 `FormBuilder.FromField`），容器、工具条、表格、图表、自定义组件全是手绘仿真。这个项目的页面**全部**由自定义组件 + 容器构成，所以画布对它是 100% 仿真 —— 注释里写的「所见即所得天然成立」在这类工程上不成立。

### D2（P0）设计器的 dock 布局会把画布尺寸回写进源文档

`Designer.Form.zan:596-640` 的 `ApplyDockLayer` 直接改字段：`f.fx = rx; f.fw = tw; f.fh = rh;`。
即「打开 + 布局」这一步就已经把画布解算出来的几何写回了 `FormField`，保存即落盘。证据在项目里肉眼可见：

`ReferenceToolbar.zform`（`layoutMode: 1`）九个字段全是 `fh: 360`（画布高度被盖进去了），且右侧 7 项 `fw: 0`：

```
Label      SelectedPlan   dock 3  fw 280  fh 360
SelectBox  DeviceSelect   dock 3  fw 150  fh 360
Button     ConfigButton   dock 4  fw 40   fh 360
Button     QuickButton    dock 4  fw 0    fh 360
Button     LoadButton     dock 4  fw 0    fh 360
DatePicker EndDate        dock 4  fw 0    fh 360
Label      ToLabel        dock 4  fw 0    fh 360
DatePicker StartDate      dock 4  fw 0    fh 360
Label      DateLabel      dock 4  fw 0    fh 360
```

`fw:0` 时 `ApplyDockLayer` 分到 `tw = 0`，把 0 又写回去 —— 项目审查记录里的 P1「dock:4 + fw:0 可能零宽不可点」不是巧合，是设计器自己生产并自我固化的结果。编译路径靠控件自身首选宽侥幸救回来（`GenForm` 跳过 `Prefer`），JSON 载入路径救不回来（见 D1）。

### D3（P1）10 个根键在编译产物里无人消费

设计器写、也在 `.zform` 里落盘，但 `GenForm`/`FormBuilder` 都不读：
`winTool`、`winZoom`、`formSize`、`hideStar`、`showSubmit`、`showReset`、`submit`（完全惰性）；
`labelPos`、`labelWidth`（**只影响设计器画布** → 在画布上调标签位置/宽度，编译后没有效果）；
`activeTab`（Tabs 初始页在成品里丢失）。

11/16 个文档都带着这批键。对 AI 更糟：它会以为这些键有效。

### D4（P1）自定义组件包没有一等公民表达方式

项目里的组件包：`ObjectPane`、`GridPane<T>`、`CampGridHost`、`CampGroupGridHost`、以及 6 个 `*ObjectHost`、3 个 `*ReferenceToolbar`。stdlib 侧有两套机制，都不完整：

1. **用户组件（画布拖拽）**：IDE 从 `<project>/components/*.zform` 读进来（`ZanIDE.CodeNav.zan:389-396`），落地时 `Designer.zan:557` 的 `AddUserComponent` 是 **inline 复制**（`ParseComponentFields` 出来的字段直接插进当前文档）。所以是「图章」不是「引用」：改组件不会更新已落地的实例。
2. **项目组件（`kind` = 类名）**：编译期 `new Kind()` 可用，但
   - 画板上只画成 **图标 + 类名占位盒**（`Designer.Form.zan:1699-1703`）；
   - 属性只有一个 `customKind` 文本框（`FormField.zan:935`）—— **无法给实例传任何参数**；
   - 注册表 `Gui/ProjectComponents.zan` 是 `scripts\scan_components.ps1` 在 **IDE 构建期**生成的，仓库里提交的是空版本，IDE 运行时没有任何地方为用户工程重新扫描 → 用户工程的组件既进不了调色板，`ControlFactory.Create` 也解析不出来（运行时 JSON 载入路径直接拿不到控件）；
   - 该项目 12+ 个 `: Control` 类里只有 1 个带 `/// @component` 标记（`BrowserPage.zan:7`）。

「无法传参」是类爆炸的直接原因：6 个 `*ObjectHost` 类除了字符串和列定义**完全同构**（`ReferenceObjectHosts.zan`），只为了让 `.zform` 有个名字能写。同理 `ReferenceToolbar.zform` 与 `WideReferenceToolbar.zform` 的 diff 只有 `name` 一行 —— 4.1 KB 的复制体，`.zan` 也跟着复制。

### D5（P1）设计控件的事件没有 sender，逼出单例，并且已经错了

`role:control` 的 `on<Event>` 生成 `this.__Dispatch(name)`，而 `__Dispatch` 转给**宿主 Form 的按名 handler 表**（`GenForm.zan:295-296` → `Control.Call(string)`，`Action` 无参、无 sender）。同一个设计控件被实例化多次时，处理器无法知道是哪个实例触发的。

项目的应对是 `static current` 单例，全库 13 处。而 `ReferenceToolbar` 被 `CrowdPage.zform` + `KeywordPage.zform` 各实例化一次，`WideReferenceToolbar` 被 `AdGroupPage.zform` + `CreativePage.zform` 各一次，`OnInit` 里 `ReferenceToolbar.current = this` 后构造的覆盖先构造的 → **在人群页点「加载报表」读的是关键词页的日期控件**。这是格式/代码生成模型逼出来的缺陷，不是作者手滑。

（顺带解释了 D-0 现象：`on<Event>` 在 16 份 `.zform` 里一次都没用 —— 声明式事件这条路对可复用控件不好用，作者全部改手工 `Click +=`。）

### D6（P2）默认值不省略，成本落在 AI 和 diff 上

字段键 39% 是默认值；自定义组件字段照样带 `"label": "Text field"`、`"placeholder": "Please enter"`、`"options": []`、`"bind": ""`。
按现在的写法投影到原工程那种 234 控件的窗体：约 234 × 11 个键 ≈ **2600+ 行几乎全默认的 JSON**。这才是你感觉「大 JSON 要整段改」的真实来源 —— 不是格式是 JSON，是**写出来的键太多且无语义**。

---

## 3. 动态判断，按真实需求分类

| 需求 | 项目里的实现 | `.zform` 该不该管 |
| --- | --- | --- |
| 状态 → 控件值 | 未使用 `bind`（0 处） | 已有能力，先补范例 |
| 显示/隐藏 | 1 处 `SetShown` | 代码即可，收益极低 |
| 启用/禁用 | 0 处 | 同上 |
| 动态增删内容 | 289 处 `Add/Clear/Remove` | **格式做不了**，本就该在代码 |
| 换组件/换页（对象路由） | `SearchModulePage.zan` / `MainForm.zan` 动态装配 Ribbon/Body/页面 | **格式做不了** |
| 整窗手写 | `DailyBudgetWindow.zan`、`GridConfigWindow.zan` 完全弃用 `.zform` | 保留立即模式 |

所以：**`showIf` 在这个项目的收益实测为零**。它解决的是「表单里某个字段按状态隐藏」，而这个项目的动态性 100% 是「换内容/换页/建树」。上一份评估里 A 档「≈1 个会话，值得」的结论，在有了这批证据之后我下调为**先不做**。

---

## 4. 建议的优先级（都不动格式语法）

1. **P0 统一建树路径**：`FormBuilder` 补 `props`/`bind`/`bindProp`/`childTab`/`gap`/`pad`、对齐 `fw==0` 与 DPI 语义；理想终态是 `GenForm` 与 `FormBuilder` 共用一份 setup 描述，二者差异用守门测试锁死（对每份 `.zform` 比对两条路径产出的控件树）。
2. **P0 停止破坏性回写**：`ApplyDockLayer` 算布局用临时矩形，不写回 `f.fx/fw/fh`；dock 项的 `fw/fh` 要么保留作者输入，要么明确「0 = 用控件首选尺寸」并在两条路径一致实现。
3. **P1 自定义组件做成一等公民**：组件声明自己的 `props`（复用 `PropSpec`/`Props()` 那套，正好能进上一轮做的 `zform.json`）+ 画布真控件预览 + 运行时注册表不依赖重建 IDE（改成扫描项目源码或工程文件声明）。有了 props，6 个 `*ObjectHost` 塌成 1 个、3 个工具条塌成 1 个。
4. **P1 事件带 sender**：`Action` → 允许 `Action<Control>` 或生成器把实例引用一起绑进闭包，消掉 13 个 `static current` 与那个错实例 bug。
5. **P2 写出时省略默认值**（含清掉 D3 那批惰性键），JSON 体积和 AI 改动面直接减一半以上。
6. **P3 声明式条件键**：等 1-4 做完、`bind` 真的被用起来再评估。

---

## 5. 仍需 UI 实测才能定论的

- 右侧 dock + `fw:0` 的 `DatePicker`/`Label` 在编译路径下的实际宽度（取决于各控件首选宽实现），即项目审查里那条 P1 是否已经在成品上可见。
- `childTab`/`props` 在 `FormBuilder` 路径的具体崩法（需要一个走 JSON 载入的窗口实测）。
- `ReferenceToolbar` 双实例的错实例行为在 UI 上的表现（我从代码推断为「读到另一页的日期」，未上机验证）。
