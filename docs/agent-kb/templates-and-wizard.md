# 模板与"新建项目"向导

## 数据流（一条链，改哪一环都要顺着看下去）

```text
templates/<group>/<id>/template.manifest
  └─ ZanIDE.AddDiskTemplate()  → Tpl（IDE 内部模板描述）      [ZanIDE.Workspace.zan]
       └─ ZanIDE.WizTemplates() → WizardTemplate（向导展示用）  [ZanIDE.Workspace.zan]
            └─ Wizard.Categorized(...) / Wizard.RenderCategorized(...)  [Gui/Widget/Wizard.zan]
                 ├─ 左：分类列表   中：模板列表 + 名称/位置/平台/设备尺寸
                 └─ 右：预览（真实截图，或按 sketch 画线框）
       └─ ZanIDE.CreateProjectT(tpl, parent, name, platforms, devW, devH, log)
            ├─ 复制模板树，App.zan/App.zform 重命名为项目名
            ├─ 写 zan.proj（type/target/entry/platforms）
            └─ RetargetForms(): 按所选设备尺寸改写 .zform 的 winW/winH 并把控件夹回画布内
```

## manifest 字段

必填/常用：

| 字段 | 含义 |
| --- | --- |
| `name`, `name.zh` | 显示名（中英） |
| `desc`, `desc.zh` | 描述（中英） |
| `category`, `category.zh` | **分类**（向导左栏分组）：`Console/控制台`、`Window App/窗口应用`、`HMI / SCADA/工控 HMI`、`Server/服务端`、`Library/类库` |
| `icon` | 图标 id（`Gui/Icon.zan`） |
| `kind` | `copy`（复制模板树） |
| `type` | 程序类型：`console` / `gui` / `server` / `library` |
| `target` | `exe` / `dll` |
| `entry` | 入口源文件（相对项目根） |
| `order` | 同分类内排序 |

尺寸与预览（本轮新增，用于向导右侧预览和"设备尺寸"行）：

| 字段 | 含义 |
| --- | --- |
| `sizeable` | `1` = 这个模板可以在创建时选设备尺寸（向导显示尺寸行，并改写生成的 `.zform`） |
| `size` | 默认设计尺寸，`宽x高`，如 `1000x680` |
| `shape` | `round` = 圆形设计区（手表表盘） |
| `preview` | 相对模板目录的截图路径（有就优先画真实截图） |
| `sketch` | 无截图时 IDE 画的线框构成，逗号分隔 token |

`sketch` 可用 token（在 `Wizard.RenderSketch` 里实现）：
`titlebar` `ribbon` `toolbar` `tabs` `statusbar` `sidebar` `alarms` `console` `webview`
`chart` `gauges` `list` `server` `library` `form` `cards`。加新 token 就在
`RenderSketch` 里补一个分支。

## 分类原则：尺寸不是模板

**教训**：曾经存在 `gui-desktop(1280x720)`、`gui-phone(390x844)`、`gui-tablet(768x1024)`、
`gui-qvga(320x240)`、`gui-screen(480x320)`、`gui-wvga(800x480)` 六个模板，内容与
`gui-free` 完全相同，只有 `.zform` 里的宽高不同 —— 20 个 GUI 模板全挤在同一个
"Window App" 分类里，用户根本看不出区别。

现在的划分是**三个正交维度**，各自用不同的 UI 元素表达，不再混进模板名：

| 维度 | 表达方式 |
| --- | --- |
| **程序模式**（控制台/窗口/工控 HMI/服务端/类库） | 向导左栏**分类**，来自 `category` |
| **窗口结构**（空白/自由布局/工具条/Ribbon/侧边导航/多标签/组件展示/WebView） | 分类内的**模板**，来自 `name`/`desc` |
| **设备尺寸**（手机/平板/桌面/嵌入式屏/手表） | 模板选中后出现的**设备尺寸行**（预设 chip + 自定义宽高），来自 `sizeable`/`size` |

所以"手机版自由布局窗口" = 模板 `Free Layout Window` + 尺寸 chip `Phone 390x844`，
不再是一个单独的模板。`gui-watch` 保留为独立模板，因为**圆形表盘**是形状差异
（`shape=round`），不是尺寸差异。

尺寸预设列表在 `Wizard.SizePresetW()/SizePresetH()/SizePresetName()`（12 个：QVGA 320x240、
480x320、800x480、1024x600、Phone 390x844、Tablet 768x1024、1280x720、1366x768、
1920x1080、Watch 466x466、368x448、194x368）。要加设备就改这三个函数。

## 向导实现要点（`stdlib/Gui/Widget/Wizard.zan`）

- `WizardTemplate` 除 `cat/name/icon/desc` 外带元数据，用链式设置：
  `new WizardTemplate(cat, name, icon, desc).Shape(sizeable, w, h, round).Look(preview, sketch)`
- `Wizard.Categorized(...)` 有两个重载：旧的（分类+平台）和新的（多带
  `SignalInt sizeSel, Input sizeW, Input sizeH`）。渲染侧 `RenderCategorized` 同样保留
  旧签名转发到新签名，**不要删旧重载**，其它调用点还在用。
- 右侧预览列宽约 `app.Scale(300)`，窗口不够宽时自动不显示（表单列让位）。
- 预览逻辑 `Wizard.RenderPreview`：有 `preview` 且 `Canvas.ImageWidth(path) > 0` → 画截图；
  否则按 `sketch` 画线框；`shape=round` 画圆；没有声明尺寸就按 16:10 画并标注"自由布局"。
- 设备尺寸行只在 `selTpl != null && selTpl.sizeable && sizeSel != null` 时出现——
  控制台/类库/服务端模板不显示无意义的控件。
- IDE 侧状态：`pSizeSel` / `pSizeWIn` / `pSizeHIn` / `pSizeSeededFor`
  （`ZanIDE.State.zan`，初始化在 `ZanIDE.zan` 的 `InitDialogState` 附近）。
  选中模板变化时用模板自身 `size` **种子填充**输入框，保证"直接点创建"= 预览所见。

## 脚手架要点（`ZanIDE.Workspace.zan`）

- `CreateProjectT(tpl, parent, name, platforms, devW, devH, log)` 是带尺寸的新签名，
  内部先调用原来的 `CreateProjectT(...)` 复制模板，再 `RetargetForms(dir + "/src", w, h, log)`。
- `RetargetForms` 反序列化 `FormDoc`、改 `winW/winH`、把每个 `FormFieldDoc` 的
  `fx/fy/fw/fh` 夹回新画布（留 16px 边距）再写回。**只改尺寸不重排布局**：
  1280x720 的控件直接搬到 390x844 会跑到画布外，夹一下是底线，真正的响应式断点布局
  要在设计器里做。
- 项目名校验：`IsValidProjectName`（非法字符/保留名）、`IsGuiReservedName`
  （与内置 `Gui`/`System` 类型冲突）。

## 改模板后必须做的验证

```powershell
scripts\e2e_pipeline.ps1     # 每个模板：脚手架 → 编译 → 发布 → 冒烟
```

- `.zform` 设计器模板会被它 **SKIP**（`.g.zan` 由 IDE 生成），所以窗体模板还要在
  IDE 里真的新建一次项目、编一次、跑起来看一眼。
- 删/改模板前先 `git grep <模板目录名>`：`scripts/e2e_pipeline.ps1`、文档、测试可能引用。
