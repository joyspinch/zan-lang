# ZanIDE 项目结构规范

本文件是 ZanIDE 的结构约定，也是**其他 Zan GUI 商业项目的参考模板**。
新增文件前先读这里；`scripts\check_structure.ps1` 会在构建/提交前做守门检查。

## 1. 一个可视单元 = 一个 `.zform` + 同名 code-behind

| 文件 | 职责 |
|------|------|
| `Xxx.zform` | 声明式设计（FormDoc，JSON）：控件树、布局、事件名。**唯一** UI 声明来源 |
| `Xxx.zan`   | code-behind：`partial class Xxx`，只放数据绑定与事件逻辑 |

* `.zform` 由构建时的 `System.Compiler.GenForm` 生成 `partial class`，**不要手改生成物**。
* 窗口文档（缺省，或 `"role": "window"`）生成 `__CreateWindow()` / `Show()`；
  项目入口（`zan.proj` 的 `entry`）必须是窗口文档，且必须是 zanc 的第一个输入。
* 面板 / 页 / 卡片等可嵌入单元写 `"role": "control"`，生成
  `partial class Xxx : Control`，可以直接作为别的 `.zform` 里的 `kind`：

  ```json
  { "kind": "ExplorerPanel", "name": "Explorer", "dock": 3, "fw": 260 }
  ```

  控件文档的 code-behind 提供 `void OnInit()`，由生成的构造函数在建树后调用。
* 禁止自绘：控件树只能由标准库 GUI 组件（`Gui` / `Gui.Widget` / `Gui.Hmi`）构成。

## 2. 目录分层

```text
src/ide_zan/
  zan.proj              项目清单（entry / name / kind）
  ide.css               页面样式表
  src/
    IdeForm.zform       主窗体（入口，窗口文档）
    IdeForm.zan
    shell/              外壳：ribbon 命令、停靠注册、快捷键、帧循环
    panels/             停靠面板（每个一对 .zform + .zan，role: control）
    editor/             编辑器宿主、标签条、查找栏、代码导航、诊断存储
    dialogs/            模态/子窗口（窗口文档）
    pages/              全页视图（Start / Docs / Team / Debug / Asset）
    services/           无 UI 的领域服务（项目、构建、运行、调试、LSP、Git、AI…）
    models/             纯数据模型（DTO / 记录类型），无逻辑、无 UI 依赖
```

规则：

1. **UI 不直接做 IO/进程/网络**：一律走 `services/`；服务不 `using Gui`。
2. **面板之间不互相引用**：通过 shell 或服务通信。
3. **models/ 只放数据**：一个文件一个模型族，无副作用。
4. **命名**：类型 `PascalCase`，文件名与主类型同名；面板以 `Panel` 结尾，
   对话框/子窗口以 `Window` 或 `Dialog` 结尾，服务以 `Service` 结尾。
5. **一个文件一个主类型**（同名 `partial` 除外）。

## 3. 文件规模上限

* 单个 `.zan` 文件 **≤ 800 行**。超限就按职责继续拆。
* 现存超限文件登记在 `docs/projects/zanide/oversize.baseline.txt`，
  该清单只允许变短：迁移过程中逐个清空，**不允许新增条目**。

## 4. 守门

```powershell
powershell -ExecutionPolicy Bypass -File scripts\check_structure.ps1
```

检查项：

* 每个 `.zform` 都有同名 `.zan` code-behind；
* `src/ide_zan/src` 下的一级目录都在白名单内（见第 2 节）；
* 没有新的超过 800 行的文件（对照 baseline）；
* `services/` 与 `models/` 不 `using Gui`。

## 5. 迁移批次（P0–P5）

| 批次 | 内容 | 状态 |
|------|------|------|
| P0 | 构建收集全部 `.zform`；本规范 + 守门脚本；编译器 `role: control` | 已完成 |
| P1 | 把 `ZanIDE` 巨类里的状态迁到 `services/` 与面板类 | 进行中 |
| P2 | 对话框各自独立 `.zform` + code-behind | 待办 |
| P3 | 停靠面板改 `.zform` + 保留式控件树，去掉即时绘制 | 待办 |
| P4 | 页面（Start / Docs / Team / Debug / Asset）改 `.zform` | 待办 |
| P5 | `ZanIDE.zan` 只留外壳编排；文档收口 | 待办 |

每批必须可编译、可运行、可单独提交（smoke 必须绿）。
