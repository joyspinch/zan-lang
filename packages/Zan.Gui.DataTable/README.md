# Zan.Gui.DataTable - 企业级数据表格组件包

从 stdlib `Gui/Component/DataTable` 拆出的高性能数据表格组件族。命名空间保持 `Gui.Component.DataTable` 不变，`using` 语句无需改动；`--auto-stdlib` 按需从本包拉入。

## 现代管线化架构（Pipeline Architecture）

全面消灭早期 3900+ 行与 1100+ 行巨石单体文件，采用职责高度内聚的正交渲染流水线：

```
                [DataTable.Render 调度中枢 (~260 行)]
                                  │
       ┌──────────────┬───────────┴───────────┬──────────────┐
       ▼              ▼                       ▼              ▼
[RenderHeader]  [RenderFrozen]          [RenderBody]   [RenderFooter]
(Band/分组拖拽)   (冻结带裁剪/固定行)     (虚拟循环/主从挂载) (汇总/分页/状态栏)
       │              │                       │              │
       └──────────────┴───────────┬───────────┴──────────────┘
                                  ▼
                        [RenderInteraction]
                    (命中几何/Tooltip/键盘导航/选区)
                                  │
                                  ▼
                             [RenderCell]
                    (图元绘制/文本折行/迷你图)
```

### 1. 单职责分层渲染管线
- **`DataTable.Render.zan`**：精简为主干生命周期调度中枢，统一度量计算与流程编排。
- **`DataTable.RenderCell.zan`**：单元格图元绘制、富文本断行、条件格式（数据条/色阶/图标）与 Sparkline 迷你图。
- **`DataTable.RenderHeader.zan`**：多级 Band 跨度表头、分组面板芯片拖拽、排序箭头与列宽调整手柄。
- **`DataTable.RenderFrozen.zan`**：冻结列坐标几何推算、双向区域裁剪（`PushBandClip`）与置顶固定行。
- **`DataTable.RenderBody.zan`**：虚拟滚动行循环、主从明细挂载（`DetailProvider`）、组件列控件池与选区高亮。
- **`DataTable.RenderFooter.zan`**：底部置底固定行、总计汇总行（Summary）、分页器（Pagination）与底部聚合状态栏。
- **`DataTable.RenderInteraction.zan`**：指针几何映射、截断 Tooltip 悬停探测、查找条与快速筛选行键盘拦截、多选区拖拽与右键上下文菜单。

### 2. 解耦就地编辑与筛选菜单
- **`DataTable.FilterMenu.zan`**：列头漏斗筛选菜单专用模块，包含唯一值列表去重、组合规则条件与日历选择。
- **`DataTable.InPlaceEditor.zan`**：单元格就地编辑浮层、外部编辑器控件宿主、原生选择下拉列表与单元格日历。
- **`DataTable.MicroWidgets.zan`**：轻量级微控件原语（`TabBtn`, `IconBtn`, `TextBtn`, `EditBox`, `Chip`, `DrawMiniCheck`），消除重复画框逻辑。
- **`DataTable.NavKey.zan`**：全量键盘导航与剪贴板快捷键（上下左右、PageUp/Down、Home/End、Tab/Enter、Ctrl+C/V/X/A）。

### 3. 结构化运行时状态实体（State Encapsulation）
`DataTableState` 收敛为结构化组合设计，保持 100% 现有属性向后兼容：
- **`DataTablePivotState`**：透视表交叉聚合矩阵、行列维度映射与物化源状态。
- **`DataTableUndoState`**：行级别撤销/重做日志堆栈、事务分组与回滚。
- **`DataTableFillState`**：Excel 式选区填充柄拖拽状态与源/目标矩形跟踪。

### 4. 深度 Visual Tree 互嵌与无冲突作用域
- **原生 Visual Tree 控件节点支持**：除强类型 `DataGrid<T>` 外，新增非泛型原生控件包装器 `DataTableControl`，允许将任意动态列与数据源作为一等公民挂入 Visual Tree（自动完成子控件度量与拓扑对接）。
- 主从明细通过 `ProvideControl` 与父级 Visual Tree 拓扑无缝缝合，支持事件双向冒泡（Bubbling）与隧道拦截（Tunneling）。
- 控件池与动态项全面依托 `WidgetId.Push(name/index)` 作用域栈，彻底消灭硬编码 ID 魔数。

### 5. 高阶企业级功能增强
- **帧级样式快照缓存（DataTableStyleSnapshot）**：帧开始时统一解析并缓存当前主题全部所需 `StyleBox`，消除虚拟滚动行循环与各管道子阶段中重复触发的样式表字典查找。
- **列宽实时指引线与像素指示胶囊**：拖拽列宽调整手柄时，沿网格全高绘制实时对齐引导线，并在表头浮动显示带有当前像素宽度的半透明指示胶囊（对标 ag-Grid & Excel）。
- **TreeGrid 递归 Rollup 汇总引擎**：`TreeAdapter` 与 `IdTreeAdapter` 开箱支持层级递归汇总计算（`Rollup` / `RollupSum` / `RollupAvg` / `RollupMin` / `RollupMax` / `RollupCount`），自动将后代叶子节点的度量值向上汇聚至父级分组节点。
- **通用标准 MessagePack 二进制与 Token 布局持久化**：采用通用成熟的 MessagePack 协议（`Zan.MsgPack`），提供 `SaveLayoutMsgPack` / `LoadLayoutMsgPack` 纯二进制流存取，以及 `SaveLayoutToken` / `LoadLayoutToken` 超紧凑单行令牌存取。体积相比传统 JSON 骤降 60%~80%，亚微秒级解析，彻底消灭字符串 GC 碎片，并完整保真自定义派生公式（`aggFormula`）、多列联合排序、分组与冻结带。
- **开箱即用富媒体单元格预设**：`WidgetComps` 与 `DataTable` 统一暴露常用高质感图元原语（`PaintTagPill` 状态胶囊标签、`PaintUserPill` 头像缩写与用户卡片、`PaintMultiTags` 自动测宽与 `+N` 溢出裁剪标签组）。

---

## 核心能力

- **DataGrid / DataTable**：强类型列模型、多字段排序、列显隐选择器（Column Chooser）、多级 Band 表头。
- **过滤与查找**：漏斗菜单、筛选行、筛选面板芯片条、组合筛选树（FilterBuilder）、Ctrl+F 高亮查找条。
- **数据源与分页**：本地数据源、HTTP 异步数据源、服务端视口分页（QueryRequest / QueryPlan / QueryResult）。
- **计算与视图**：计算列（CompCell 控件列）、公式引擎、透视交叉表（PivotView）、行列转置（Transpose）。
- **企业级交互**：主从嵌套（Master-Detail）、事务日志（Undo/Redo）、Excel 拖拽填充柄（Fill Handle）、多区域选区（Ctrl 多选并集聚合统计）、Xlsx 导入导出。
- **多语言**：内置英文与中文参考语言包，跟随应用主题与 DPI 缩放。

## 依赖

仅 stdlib（Gui 核心/Widget、System.Data.Excel、System.Net.Http 等）。
无原生驱动依赖；外观样式由全局 base.css 语义类驱动。
