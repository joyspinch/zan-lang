# Zan Chart 与 ECharts 2.2.7 兼容性合同

## 目标与范围

`Gui.Component.Chart` 是 Zan 原生、立即模式、Canvas 渲染的 ECharts 2.2.7 风格图表组件。本文是兼容性口径：它描述**已建模的兼容子集**，不承诺任意 JavaScript option 可以无转换运行。

兼容性分为三层：

- **L1：视觉可表达**：图表类型、主要布局、样式和官方示例中的主视觉可以用 Zan API/JSON 表达。
- **L2：交互行为**：图例、tooltip、dataZoom、dataRange、地图漫游、时间轴和多图联动等已建模交互可操作，状态在立即模式重建后保持。
- **L3：实例/API 语义**：option 更新、事件订阅、生命周期、导出和扩展注册等行为与 ECharts 实例 API 对齐。L3 仍在持续建设，使用前应以本文件的状态表为准。

## 当前已覆盖的能力

### 图表类型

已建模的原生类型包括：

`line`、`bar`、`scatter`、`k`/`candlestick`、`pie`、`radar`、`chord`、`force`、`map`、`gauge`、`funnel`、`eventRiver`、`treemap`、`tree`、`wordCloud`、`heatmap`。

此外提供 Zan 扩展渲染器：

`box`、`error`、`sankey`、`waterfall`、`sunburst`、`radialBars`、`venn`。

### 组件与交互

当前 option/API 已覆盖或部分覆盖：

- title、legend、tooltip、grid、xAxis/yAxis、polar；
- toolbox 的数据视图、line/bar 内置切换、还原、保存和区域缩放；
- dataZoom、dataRange/visualMap 式连续色域、markPoint、markLine、markArea；
- timeline 播放/暂停/切帧；
- 地图 GeoJSON、UTF8Encoding 压缩坐标、区域选择和漫游；
- `connectGroup` 对多图的 dataZoom、图例和 Cartesian hover 联动；
- `ChartController` 的 option、系列数据替换和追加；
- `ChartController` / `ChartView` 的快照读取、Restore/Clear、Loading/Empty/Error 状态和 Dispose 语义；
- `ChartBig` 面向大数据量折线/面积图的抽样与缓存。

## 与 ECharts 2.2.7 的明确差异

### 配置执行模型

ECharts 的 `formatter`、动态颜色、`symbolSize`、位置和 tooltip 回调可以是 JavaScript 函数。Zan Chart 优先提供静态字段、模板和内置规则；JSON 中无法执行的 JavaScript 函数字段不视为已兼容。后续如提供 Zan delegate 回调，会按 Zan 的类型和线程模型定义语义，不复制 JavaScript 闭包运行时。

### 实例生命周期与事件

Chart 的立即模式入口是 `ChartView.Render(app, x, y, w, h)`，状态使用稳定 key 保存在 `App.StateGet/StateSet`。保留的 `ChartView` 已提供 `GetOption`、`SetOption`、`Restore`、`Clear`、`Refresh`、`ShowLoading`/`HideLoading`、`SetEmptyMessage`、`SetErrorMessage` 和 `Dispose`，并可订阅 Rendered/Resized/Updated/Disposed 的无参数生命周期事件。

带载荷的交互事件通过 `ChartEventType`、`ChartInteractionEvent` 和 `ChartInteractionHandler` 提供。`ChartView` 与 `ChartController` 都支持 `On(type, handler)`、`Off(type, handler)`、`ClearEvents()` 以及 `OnClick`、`OnDoubleClick`、`OnHover`、`OnMouseOut`、`OnLegendSelected`、`OnDataZoom`、`OnDataRange`、`OnRestore`、`OnDataChanged` 便捷入口。当前已接入的事件包括图表级点击/进入/离开、图例切换、dataZoom/dataRange 状态变化、toolbox magicType、restore、resize 和 controller 数据变更；`seriesIndex`、`seriesName`、坐标、选中状态和 zoom 范围在适用时填入，其余字段使用明确的空值哨兵。事件回调是 Zan typed delegate，不是可执行任意 JavaScript 的 ECharts DOM `on/un`。

这些回调在 Render seam 或状态更新点触发，不是完整 ECharts DOM 事件载荷；扇区、地图区域、层级节点和 force 布局的专用命中载荷仍在后续接入。与 ECharts 的 `resize()`、完整 `on/un` 模型仍有差异，调用方不应假定所有 ECharts 实例方法已经存在。

### 动画

当前动画主要是确定时长的入场/状态动画；ECharts 2.2.7 的更新动画、缓动函数、`addDataAnimation` 和 `animationThreshold` 不保证逐项等价。

### Force 布局

Force 使用确定性圆周初值和固定轮数弹簧模拟，以便缓存和回归测试。ECharts 的随机初始位置和逐帧轨迹不会逐帧一致；这是有意取舍，不是随机性 bug。

### ChartBig

`ChartBig` 是高数据量 Cartesian 折线/面积专用路径，不是普通 `ChartView` 的所有图表类型的替代实现。pie、map、tree 等非 Cartesian 类型不应按 ChartBig 的能力判断。

### 混合图层

当前 line/bar/scatter 等 Cartesian 混合已经建模；不同图族在同一画布中共享坐标系和图层的完整 ECharts 语义仍属于后续 `ChartLayer`/多坐标系架构，不以 `LeadType()` 的主导布局冒充完全兼容。

### JSON 与扩展

`ChartOption.FromJson` 支持已建模的 ECharts 风格字段。未知字段、第三方扩展字段、实例级 `notMerge`/`lazyUpdate` 等参数不保证保留。地图解析支持 GeoJSON、Polygon/MultiPolygon、洞和 ECharts UTF8Encoding；通用地图注册和地理坐标转换 API 仍按后续版本提供。

## 状态表

| 能力 | 状态 | 说明 |
|---|---|---|
| 官方 2.2.7 示例主视觉 | 已覆盖 | 仓库账本以 134 个示例为基线 |
| 主要图表类型与专用布局 | 已覆盖/扩展 | 扩展类型不等同于 ECharts 原生模块 |
| 图例、tooltip、dataZoom、dataRange | 已建模 | 边界轴值、事件 API 持续增强 |
| timeline、connect | 已建模 | 立即模式状态持久化 |
| GeoJSON 与压缩地图坐标 | 已建模 | 运行时解析，见 `ChartGeoJson` |
| 任意 JavaScript formatter | 不支持 | 使用模板/内置 formatter/后续 Zan delegate |
| typed Chart 交互事件 | 部分覆盖 | `ChartEventType`/`ChartInteractionEvent` 支持点击、悬停、图例、zoom、dataRange、地图/饼图选择、漫游、timeline 等已接入路径；专用命中载荷仍在建设 |
| 完整 `on/un` 与实例生命周期 | 建设中 | 当前 API 是 Zan typed delegate + Render/Controller 入口，不承诺任意 ECharts DOM 实例方法 |
| 任意 magicType 类型互换 | 不支持 | 当前提供有限 line/bar 切换 |
| calculable 数据孤岛工作流 | 部分 | dataRange 拖拽不等同完整数据孤岛 |
| SVG/PDF/多格式导出 | 未承诺 | 当前导出路径以 PNG 为主 |
| 多图族同画布共享坐标系 | 后续架构 | 与 connect 联动正交 |

## 兼容性承诺原则

1. 新增字段必须同时更新模型、JSON 解析、resolved/fingerprint、渲染器和 conformance 测试。
2. “官方示例可表达”只证明 L1/L2 的目标场景，不自动证明 L3 实例 API 等价。
3. 有意取舍必须记录在本文件和 `docs/CHART_VS_ECHARTS_227.md`，避免把近似行为描述成逐帧兼容。
4. 未建模字段应保持安全忽略或给出诊断，不应静默改变已支持字段的语义。
