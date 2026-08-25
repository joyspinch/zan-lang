# Zan Chart 与 ECharts 2.2.4 能力对照 · 变体矩阵

> 基准：ECharts 2.2.4（用户指定 doc/example 文档为权威参考）。
> 审计方式：stdlib/Gui/Component/Chart/ 全部 21 个文件（≈8.6k 行）逐文件通读 +
> 缺失项 grep 反证。标注口径：**已实现**=有代码路径且可从 option/API 表达；
> **部分实现**=有入口但语义缩水；**未实现**=全目录 grep 无任何消费点。

## ⚠ 口径修订：以 2.2.4 为准的重新定性

以下经官方 example 目录（130 个 demo 全量解析）与 GitHub tag 源码核对后修正：

**我们已超越 2.2.4 的能力（不是缺口，勿"补齐"）：**
- `markArea`——2.2.4 没有 markLine 之外的三区标注；我们已实现。
- 阶梯线 `step`——2.2.4 折线无 step 字段（3.x 能力）；我们的 step:start 已超出，
  三态分化属于超纲增强而非对齐缺口。
- 热力图 Heatmap——`src/chart/` 在 2.2.4 不存在（3.x 核心）；我们是净增类型。
- sankey / box / error / waterfall / sunburst / radialBars 六个扩展渲染器同理。
- gauge 的 `precision` 定点格式：2.2.1 已从官方删除，我们保留是超集。

**2.x 命名对照（对齐时用 2.x 字段名）：**
- 3.x 的 `visualMap` 在 2.2.4 叫 **`dataRange`**（含 `splitList` 不等距分割、
  `calculable` 值域漫游、`hoverLink` 与地图反向联动）。地图 ramp 两端硬编码、
  散点无值域映射的对齐目标是 dataRange 语义。
- K线涨跌色在 2.x 是 `itemStyle.normal{color(阳线填充), color0(阴线填充),
  lineStyle{color(阳线框), color0(阴线框)}}`——不是 3.x 的顶层 color/color0。
- 漏斗水平对齐字段是 **`funnelAlign:'left'|'right'|'center'`**（2.1.8+），
  我们缺的是这个而不是泛泛的"label 位置"。
- 树图连线 2.2.4 支持 `'curve'|'broken'` 两种；径向 = `orient:'radial'`。

**混搭的真实边界（官方 mix 分类 11 个 demo 证实）：**
跨族共存靠各自定位项（`pie.center`、`map.mapLocation`、`gauge.center`、
`funnel.x/y`）+ 轴绑定（line/bar/scatter/k/eventRiver 的 xAxisIndex/yAxisIndex）。
官方代表组合：折柱双数值轴(mix1)、散饼值轴(mix7)、折K(mix10)、地图选器+饼(mix3)、
仪表+嵌套漏斗 BI(mix11)、多图联动 connect()(mix8/9)。我们的直角系内混搭已覆盖
mix1/mix6/mix10 的核心；缺的是整图族定位共存层与多图联动。
另注：`toolbox.magicType`（line↔bar↔stack↔tiled 切换）是 2.2.4 混搭体验的一部分，
我们没有对应物。

**其余确认对齐项：** dataZoom 百分比 start/end 语义一致（无 startValue/endValue
是 2.2.4 本来的限制）；饼图 label 默认外部+labelLine、startAngle、selectedMode/
selectedOffset、roseType 两态为 2.2.4 标配（我们的缺口判定不变）；treemap 下钻+
面包屑(2.2.3)+root(2.2.4)、tree 径向、wordCloud textRotation 角度候选列表均为
2.2.4 内能力，缺口判定不变。

## 0. 总体架构事实（矩阵的公共前提）

- **分发**：`ChartView.DispatchKind` 按**第一个可见系列**的类型选整图渲染键
  （ChartView.zan:308-361，`LeadType` :297-302）；`DispatchRender` 显式分支到
  26 个渲染器（ChartView.zan:438-501）。未知 kind 画空状态不回退折线。
- **emphasis 死代码确认**：解析于 ChartModel.zan:1385，解析进
  `ResolvedSeries.emphColor/emphBorderColor/emphBorderWidth`
  （ChartResolved.zan:41-46）并提供了 `EffectiveColor/EffectiveBorderColor/
  EffectiveBorderWidth`（ChartResolved.zan:63-74），但**全目录零调用点**——
  `itemStyle.normal.borderColor/borderWidth` 同样只进缓存指纹
  （ChartView.zan:210-211）无人绘制。
- **图例交互覆盖不全**：可点击隐藏图例只在直角坐标系与雷达（走 `DrawPanelI`，
  Chart.zan:554-622）；pie/gauge/funnel/rose/chord/force/map/eventRiver/
  treemap/tree/candle/scatter 用无交互的 `PanelHead`（ChartViewShared.zan:22-33）
  或自绘图例。wordCloud 传空系列列表给 DrawPanelI（ChartViewHier.zan:625）。

## 1. 折线 line（ChartViewLine.zan）

| 变体 | 状态 | 证据 / 缺口 |
|---|---|---|
| 基础 | 已实现 | DrawLines ChartViewLine.zan:222-393 |
| smooth 平滑 | 已实现 | 逐系列覆盖全局（:315-316），Catmull-Rom 亚像素 :57-93 |
| areaStyle 面积 | 已实现 | `series.areaStyle`（ChartModel.zan:602）→ FillUnder :341-343 |
| stack 堆叠面积 | 部分实现* | 引导系列 `stack!=0` 切 `stackedArea`（ChartView.zan:358-359）。*堆叠模式退回 `DrawFrame(maxV)`（:419）：**双轴/时间轴/对数轴失效** |
| step(start\|middle\|end) | 部分实现 | 只判 `step.Length>0`（:318），三态同形恒为 step:start（:345-362） |
| lineStyle.type 虚线 | 已实现 | solid/dashed/dotted（:321-326）；虚线放弃平滑属取舍 |
| 双 Y 轴 yAxisIndex | 已实现 | `axisIndex`（ChartModel.zan:596）→ `f.YOf(v,ax)`；右轴 Chart.zan:875-884 |
| symbol / showSymbol | 未实现 | 数据点无条件画固定圆点（:369-370） |
| symbolSize 数值映射 | 未实现 | 无 |
| connectNulls / null | 未实现 | 数据模型无 null（ChartModel.zan:86-135）；缺索引跳过直接连线 ≡ 永远 connectNulls:true 且无法表达断点 |
| sampling 降采样 | 未实现 | ChartBig 包络管线（ChartBig.zan:57-116）只服务虚拟 ChartSource，与 ChartOption 不互通 |

## 2. 柱状 bar（ChartViewBar.zan）

| 变体 | 状态 | 证据 / 缺口 |
|---|---|---|
| 分组 | 已实现 | DrawBars :144-220 |
| 堆叠 stack | 已实现 | stackName 分组堆叠、顶层段圆角（:70-125）；JSON stack→稳定 id（ChartModel.zan:1696-1703） |
| 条形横向 hbar | 部分实现 | HBarCore（:17-27, 276-397）。自算坐标、**无负值/tooltip/markLine/dataZoom** |
| 负值 | 已实现 | 零基线下垂（:192-203）；横向条负值未处理（:390） |
| barWidth/barGap | 未实现 | 硬编码 `step*7/10` 等（:146-147, :83） |
| itemStyle borderRadius | 未实现 | 圆角仅全局 `o.rounded`（:59-60） |
| showBackground | 未实现 | 无此概念 |
| 瀑布(扩展) | 已实现 | kind `waterfall`（ChartViewBar.zan:400-537），比 ECharts 透明堆叠做法更直接 |

## 3. 散点 scatter / 气泡 bubble

| 变体 | 状态 | 证据 / 缺口 |
|---|---|---|
| 双值轴 | 已实现 | value-value 框架（ChartViewScatter.zan:43-82） |
| symbolSize 映射 | 部分实现 | 散点半径固定 Scale(4)（:88）；气泡 weight→半径唯一映射（ChartViewFinance.zan:215-227） |
| 逐点颜色 | 未实现 | `data[i].color` 散点路径不读（:110） |
| effectScatter 涟漪 | 未实现 | 无 |
| 大数据 LOD | 部分实现 | 气泡 stride 采样（:218-219）；普通散点无 LOD |

## 4. K线 candlestick（ChartViewFinance.zan:10-170）

| 变体 | 状态 | 缺口 |
|---|---|---|
| 蜡烛 | 已实现 | 影线+实体+大数据分桶（:65-102） |
| MA 均线叠加 | 已实现 | 折线族覆盖共享价格轴（:38-51,114-133） |
| 成交量副图 | 未实现 | 单窗格模型，无第二 grid |
| 涨跌色配置 | 未实现 | upC/dnC 硬编码（:69-70） |
| 十字光标 | 未实现 | 只有悬停 OHLC 卡片（:135-169） |

## 5. 饼 pie（ChartViewPie.zan）

| 变体 | 状态 | 缺口 |
|---|---|---|
| 基础 / 环形 donut | 已实现 | innerPct/donutPct/radius:[i,o]；环心总值（:260-267） |
| 多环嵌套 pieRings | 已实现 | 每可见 Pie 系列一环（PieLayout :66-96） |
| roseType radius\|area | 已实现 | radius=角∝占比+半径线性；area=等角+半径∝√值；startAngle 生效，命中按预计算边界+每扇区半径（DrawRose） |
| labelLine 引导线 | 已实现 | series.label.show 切外置：两段引导线（中角出弧+水平收尾）+名称百分比，色随扇区/label.color |
| startAngle | 已实现(pie) | 数学角缺省 90=12 点起，PieLayout.rot 同域平移绘制/扫掠/命中/标签 |
| selectedMode 偏移 | 部分 | data[].selected 静态外偏 selectedOffset；点击切换未做 |
| sector 圆角/边框 | 部分 | FillSector 平边；itemStyle normal/emphasis 边框已消费，圆角无 |

## 6. 雷达 radar（ChartViewPolar.zan:27-127）

| 变体 | 状态 |
|---|---|
| 多系列 / indicator max | 已实现（:79-100; RadarAxisMax :13-25） |
| areaStyle 填充 | 未实现（只描边+顶点点） |
| shape polygon/circle | 未实现（恒多边形环） |
| splitArea | 未实现（统一 grid 色） |

## 7. 和弦 chord（ChartViewRelation.zan + ChartLayoutRelation.zan:52-176）

- 节点弧加权分配（最大余数法）、节点/ribbon 双层命中 tooltip：已实现。
- ribbon 弧带：部分实现——实为子弧中心直线粗线（:42-47），无贝塞尔弧带几何。
- 子弧排序优化、标签旋转：未实现。

## 8. 力导向 force（ChartLayoutRelation.zan:214-357）

- 弹簧模拟（斥力/边吸引/中心引力）：部分实现——k/rest/系数全硬编码，无 repulsion/gravity 字段。
- 确定性初始布局：已实现；固定 60 轮。
- 拖拽节点、edgeSymbol 箭头、分类 categories 模型与图例联动：未实现。

## 9. 地图 map（ChartViewMap.zan）

- 区域着色（value→蓝阶 ramp 或显式色）、洞环挖空+多部件、外环质心标签：已实现。
- visualMap 联动（ramp 两端硬编码）、roam 缩放平移、emphasis：未实现。

## 10. 仪表盘 gauge（ChartViewPie.zan:397-667）

- 标准/半圆/分段色带/刻度细节/{value} detail formatter/axisLabel.formatter/innerRadius：
  **全部已实现**（含 ECharts 默认配色兜底 ChartModel.zan:450-457）。
- 多指针：部分实现——每系列一个完整表盘共用矩形，同心叠加出多针外观，
  非"一表盘多 data 指针"语义。

## 11. 漏斗 funnel（ChartViewPie.zan:779-880）

- sort ascending/descending、多系列装配 GatherStages：已实现。
- gap 配置：部分实现（硬编码 rowH-Scale(3)）。
- label 位置选项、转化率 tooltip：未实现。

## 12. 事件河流 eventRiver

- 时间带布局（稳定排序+贪心行分配最优行数）、权重高度正弦带：已实现
  （ChartLayoutSpecial.zan:167-281）。
- 图例点击联动：未实现（纯展示 ≤8 条）。

## 13. 韦恩 venn

- 两集（r∝sqrt 面积比）：已实现。三集：未实现（只读 data[0..2]）。
- 包含形态：部分实现（圆心距 dr*3/4 近似非精确内切）。

## 14. 矩形树图 treemap（ChartViewHier.zan:68-227）

- 逐层交替切片：部分实现（vert=depth%2，:130）；squarify 长宽比优化缺。
- 下钻 leafDepth、breadcrumb、upperLabel：未实现。gapWidth 固定。
- 深度混色、悬停描边、命中镜像：已实现。

## 15. 树图 tree（ChartViewHier.zan:674-918）

- 正交 broken 连接线（三段式，注释明引 ECharts）、orient 双向、滚轮缩放+
  拖拽平移+首帧 fit：已实现。
- 径向 radial、initialTreeDepth、节点折叠：未实现。

## 16. 字符云 wordCloud（ChartViewHier.zan:619-672）

- 字号线性映射、黄金角放置游走：已实现。
- rotate 字形旋转（Canvas 无旋转文本原语）、mask 形状、自定义字体族管道：未实现。

## 17. 混搭 mix

| 组合 | 状态 |
|---|---|
| 直角系内逐系列混用（线↔柱↔散点，双向） | **已实现**（DrawLines :277-302 / DrawBars :126-179 内嵌族分派） |
| 双 Y 轴 index 绑定 | 已实现 |
| K线+MA 覆盖 | 已实现（共享价格轴） |
| 堆叠柱+合计线 | 已实现 |
| series[].type 通用共存分派 | **部分实现**——DispatchKind 按 LeadType 选一个整图渲染器；引导为 pie/gauge/k/map 等整图族时其余类型被静默忽略 |
| K线+成交量副图 | 未实现（无第二 grid） |

---

## 架构注记 A：通用混搭的渲染器签名

签名 `(app,x,y,w,h,option,g)` 可保留，但需增加"共享坐标系上下文"：

1. 无共享 ChartFrame / 缩放窗口入参——每个直角系渲染器各自调 ZoomI0/I1、
   各自 BuildAxes（ChartViewLine.zan:231-244、ChartViewBar.zan:32-53、
   ChartViewScatter.zan:30-41、ChartViewFinance.zan:20-28,183-192）。
2. 轴范围语义冲突：MaxOfWindow vs MaxStackW/MaxStackAllW vs AxisMaxFor。
   `BuildAxesR` 已留 `leftDataMax/rightDataMax` 注入钩子（Chart.zan:857-859）。
3. 自算范围、无法共享轴的函数清单（收编对象）：
   - `DrawScatterCore`（ChartViewScatter.zan:43-82）——数值 x 轴与 category 假定冲突
   - `ScatterHover`（:132-167）——私有坐标第二份拷贝
   - `DrawBubbles`（ChartViewFinance.zan:196-230）
   - `DrawCandles`（:32-63）与 `DrawBox`（:284-296）——私有 lo/hi
   - `DrawError`（:355-362）
   - `HBarCore`（ChartViewBar.zan:286-320）——连 DrawFrame 都不用
   - `DrawStackedArea`（ChartViewLine.zan:417-419）——旧 DrawFrame，丢双轴根因
   - `DrawWaterfall`（ChartViewBar.zan:444-449）——渲染前改写 o.yAxes 的副作用式
   - `ChartBig.Render`（设计如此，可不并入）

最小改造：分发层先解析窗口 [i0,i1]、按堆叠和/逐点值并集算好各轴 dataMax，
构造一次 ChartFrame，签名扩为 `(…,frame,i0,i1)` 或 render ctx 打包下传；
再逐一迁移上述 8 个函数。

## 架构注记 B：加新图表类型的改动点清单

枚举 ChartModel.zan:8-11 → TypeOf :19-37（TypeName 反向 :47-65）→
CustomOf :41-45 → DispatchKind ChartView.zan:308-361 → DispatchRender :438-501
→ FingerprintCore 折叠 :163-282（否则 RenderCached 陈旧快照）→
锚点测试 tests/conformance/chart_kinds_complete.zan:14-56 更新 goldens。

## 变体补齐 Top 10（性价比 × 视觉影响；经 2.2.4 口径修订后）

1. **emphasis 消费闭环**——Effective* 接进 pie 扇区/map 区域/bar 单柱悬停；
   顺带让 normal.borderColor/borderWidth 生效。2.2.4 的 hoverable/selectedMode
   体验地基。
2. **pie labelLine + startAngle + selectedOffset**——2.2.4 标配三件套，
   都在 DrawPie/PieLayout 角度装配层，labelLine 两点折线即可。
3. **roseType radius/area 区分**（radius=角∝占比+半径∝√值；area=等角+半径∝面积比）
4. **bar 的 barWidth/barGap/showBackground/itemStyle barBorderRadius**
   （2.x 字段名 barBorderColor/barBorderRadius/barBorderWidth，radius 支持 4 角数组）
5. **K线涨跌色配置**（2.x 写法 normal.color/color0 + lineStyle.color/color0）
   + 十字光标（复用 CartesianTooltip 竖线）
6. **treemap squarify + 下钻 leafDepth + breadcrumb + root**——2.2.3/2.2.4 已有
   的官方能力，我们是零
7. **tree orient:'radial' + initialTreeDepth + 节点折叠**
8. **dataRange（=visualMap 前身）组件**：min/max/splitList/calculable/hoverLink，
   先接 map ramp 与 scatter 值映射两个消费方
9. **null 数据模型 + 断点折线**（表达力地基，超纲但值得）
10. **scatter symbolSize 数值映射推广 + 逐点颜色**（气泡 ISqrt 映射泛化）

次优先：funnelAlign 三态、chord 贝塞尔弧带(ribbonType:false 语义)与 sort/sortSub、
force 参数字段(scaling/gravity/linkSymbol/draggable)、eventRiver 图例点击、
wordCloud textRotation（依赖 Canvas 旋转文本原语）、HBarCore/DrawScatterCore
收编共享坐标系、toolbox.magicType 图表切换。
