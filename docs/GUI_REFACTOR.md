# Zan.Gui 深度改造计划

状态：进行中（2026-11-22 立项）。决策：**破坏式迁移**（不保留旧 API 兼容层，
仓内全部调用方同步改）、**五个方向全做**、**单会话串行**（各阶段都改
Control/PropSpec 基类，并行必冲突）。

## 目标

1. 类型化：去掉逐控件的字符串分派与魔法数字，名字错在编译期暴露。
2. 唯一事实来源：属性只经 PropSpec、事件只经 UiEvent 字段，各一条路径。
3. 单通道事件：`+=` 按 lambda 元数同时接受 `() => …` 与 `(Control s) => …`，
   删除 `*S` 平行方法族（AddS/RaiseS/BindEventS/OnClickS…）。
4. 拆包：核心框架 / 控件库 / 设计器 / 自动化分包。
5. 组合替代继承：App 子系统服务化，Control 能力组件化。

字符串路径的边界：HTML 标记与设计器天然按名字工作，**保留一条由元数据
驱动的通用按名路径**（基类 `SetProp`/`BindEvent` 查 PropSpec 与 UiEvent
字段），删除的是逐控件的字符串重写与平行 API。

## 已验证的语言能力（探针，LLVM 23 / macOS）

- 反射写属性（`SetFieldString("Label", …)`）会调用属性 setter → 逐控件
  `SetProp`/`GetExtra` 重写可改为普通属性 + PropSpec。
- `op_add` 按 lambda 元数选重载（`Action` vs `delegate void(Base)`）→ 单通道 `+=`。
- `GetType().Name` 给短类名 → `Kind()` 基类默认实现。
- 反射可按名取 UiEvent 字段（`GetFieldObject("Change") is UiEvent`）→ 通用事件查找。

## 阶段

| 阶段 | 内容 | 主要删除面 |
|---|---|---|
| 0 ✅ | Switch 原型：`PropSpec.*Field` 工厂、事件下沉基类 | Switch 195→135 行 |
| 1 ✅ | 事件模型：UiEvent 单通道；`EventTable` 按类型反射 `[Event]`/`[Event(Alias/Hidden/Display)]` 标注；删逐控件 `Events/BindEvent/BindEventS/TriggerEvent` 重写与 `On*S` 族；DataGrid/浏览器盒以事件属性公开、state 持 `weak host` 作 sender | ~1000 行样板 |
| 2 | 属性模型：`PropKind`/`ValueKind` 枚举替代魔法数；`Field(name)` 反射推断值类型；`SetProp/GetProp/GetExtra/SetExtra` 重写改属性 setter；`Kind()` 基类默认 `GetType().Name`；全量 DefineProps 改写 | ~140 处重写、~100 处 Kind |
| 3 | 拆包：`Zan.Gui`（控件树/布局/样式/渲染/标记/后端）、`Zan.Gui.Widgets`（控件目录）、设计器逻辑归 `Zan.Gui.Designer`、UiDriver 归 `Zan.Gui.Automation` | 包依赖与模板/IDE 引用 |
| 4 | App 服务化：Automation/Glass/ThemeDrawer/Offscreen/Diagnostics 抽成服务；控件经 `this.RequestRedraw()` 而非 `FindApp().RequestRedraw()` | App 146 个方法 |
| 5 | Control 组件化：滚动、命中测试、布局检查等能力拆为可挂载组件 | Control 176 个方法 |

每阶段收口：编译探针 + 锚定单跑受影响 GUI 用例 + IDE 源码类型检查，验证过即提交。

## 验证手段（macOS）

- 构建：`cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DLLVM_DIR=$(brew --prefix llvm)/lib/cmake/llvm`，
  目标 `zanc zan_gui zan_image`。
- 用例：`ctest -R "^conformance_gui_<name>$"`（单跑，遵守 WC §8 命中面上限）。
- IDE：直接用 zanc 编 `src/ide_zan` 源码（Windows 链接步骤在 macOS 上预期失败，只看前端报错）。

## 阶段 1 落地记录

- 编译器缺陷（已修根因，`case_delegate_var_overload`）：`types_concrete_equal` 不比较委托签名，
  任意两个非泛型委托判"同一具体类型"，委托**变量**实参 `ev.Add(h)` 在 `Add(Action)`/`Add(ControlEvent)`
  间打平、先声明者获胜。单通道 UiEvent 的重载转发因此依赖该修复。
- 惰性分配契约：捕获形参的闭包须放在独立方法里（捕获形参在函数入口装箱），否则无订阅的
  `Post` 也分配；`EventTable` 每类型首次按名查找构建一次。
- 设计器事件名单列出可见事件的别名（字段类型按别名承诺事件，如 `Toggle`、`SelectionChanged`）。

## 已知既有问题（非本改造引入）

- `conformance_gui_switch` 引用已不存在的 `Switch.TrackRadius` 等。
- `conformance_gui_props_contract` 报 `Chart: no rows in BuiltinPropRows`。
- 一批 GUI 用例引用已删除的 API（`Label.Of`/`Column.Create`/`Menu.ClickTree`/`Rate.baseId` 等），
  类型检查即失败，与本改造无关。
