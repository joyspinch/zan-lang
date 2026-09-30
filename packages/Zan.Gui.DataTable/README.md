# Zan.Gui.DataTable - 企业级数据表格组件包

从 stdlib `Gui/Component/DataTable` 拆出的数据表格组件族（48 个 .zan +
`lang/` 参考语言包）。命名空间保持 `Gui.Component.DataTable` 不变，
`using` 语句无需改动；`--auto-stdlib` 按需从本包拉入。

## 能力

- **DataGrid/DataTable**：列模型、行缓存、选区、多排序、列选择器。
- **过滤**：过滤条、过滤树、过滤器构建器（FilterBuilder）与自然语言描述。
- **数据源**：本地 / HTTP / 服务端查询（QueryRequest/QueryPlan/QueryResult）。
- **计算**：计算列（Compute/CompCell）、公式、透视（PivotView）、转置。
- **业务**：主从（MasterDetail）、事务（Transaction）、实时（Realtime）。
- **导出**：Xlsx 导出（复用 stdlib System.Data.Excel）。
- **多语言**：`lang/en.json`、`lang/zh.json` 参考语言包（App 自带
  `assets/lang/` 时优先）。

## 依赖

仅 stdlib（Gui 核心/Widget、System.Data.Excel、System.Net.Http 等）。
无原生驱动；皮肤走全局 base.css 语义 class，无包内 css。
