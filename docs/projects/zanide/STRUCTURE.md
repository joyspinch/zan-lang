# ZanIDE 项目结构规范

本文件是 ZanIDE 的结构约定，也是**其他 Zan GUI 商业项目的参考模板**。
新增文件前先读这里；`scripts\check_structure.ps1` 会在构建/提交前做守门检查。

## 1. 一个可视单元 = 一个 `.html` 设计稿 + 同名 code-behind + 运行期投影半件

| 文件 | 职责 |
|------|------|
| `Xxx.html` | 声明式设计（body 带 data-zan-design）：控件树、布局、事件名。**唯一** UI 声明来源 |
| `Xxx.zan`   | code-behind：`partial class Xxx`，只放数据绑定与事件逻辑 |
| `Xxx.design.zan` | 运行期投影半件：由 `scripts/gen_design_halves.py` 自动生成，暴露类型化控件访问器 |

* **运行期投影单通道**：编译期 HTML 通道已彻底废除（zanc 显式拒绝 `.html` 作为编译源文件）。所有设计稿（`.html`）通过 `--embed` 烘入可执行文件（内嵌优先读回、发布自动压缩加密），程序启动时在内存中经 `DesignerHtml.ToJsonDoc` + `FormBuilder` 投影。
* **程序入口**：`zan.proj` 的 `entry` 指向主 `.zan` 文件（如 `src/IdeForm.zan`）。程序 `Main()` 落在手写 Zan 代码里，启动时解析主窗体设计稿、建树、挂载 `OnLoad` 后进入消息循环。
* **可嵌入控件（role="control"）**：面板 / 页 / 卡片等可嵌入单元在设计稿中声明 `data-role="control"`，其半件继承 `DesignedControl`（`partial class Xxx : DesignedControl`）。构造时自动调 `this.Design(kind, docPath)` 将设计文档投影进自身控件树，命名控件以强类型属性暴露（`this.Tree` 等），并在建树后调用 code-behind 的 `void OnInit()`。可以在其它设计稿中直接以其类名作为 `data-kind` 嵌套引用：

  ```json
  { "kind": "ExplorerPanel", "name": "Explorer", "dock": 3, "fw": 260 }
  ```

* **禁止自绘**：控件树只能由标准库 GUI 组件（`Gui` / `Gui.Widget` / `Gui.Component`）构成。

## 2. 目录分层

```text
src/ide_zan/
  zan.proj              项目清单（entry / name / kind）
  ide.css               页面样式表
  src/
    IdeForm.html        主窗体（入口，窗口文档）
    IdeForm.zan
    shell/              外壳：ribbon 命令、停靠注册、快捷键、帧循环
    panels/             停靠面板（每个一对 .html + .zan，role: control）
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

* 每个 `.html` 设计稿都有同名 `.zan` code-behind；
* `src/ide_zan/src` 下的一级目录都在白名单内（见第 2 节）；
* 没有新的超过 800 行的文件（对照 baseline）；
* `services/` 与 `models/` 不 `using Gui`。

## 5. 迁移批次（P0–P5）

| 批次 | 内容 | 状态 |
|------|------|------|
| P0 | 构建收集全部设计稿；本规范 + 守门脚本；编译器 `role: control` | 已完成 |
| P1 | 把 `ZanIDE` 巨类里的状态迁到 `services/` 与面板类 | 进行中 |
| P2 | 对话框各自独立设计稿 + code-behind | 待办 |
| P3 | 停靠面板改 `.html` 设计稿 + 保留式控件树，去掉即时绘制 | 待办 |
| P4 | 页面（Start / Docs / Team / Debug / Asset）改 `.html` 设计稿 | 待办 |
| P5 | `ZanIDE.zan` 只留外壳编排；文档收口 | 待办 |

每批必须可编译、可运行、可单独提交（smoke 必须绿）。
