# Zan Chart 与 ECharts 2.2.7 全示例可表达性对照

> 基准：ECharts 2.2.7 官方 `doc/example.html` 全部 134 个示例页（含内联 option
> 与外部数据示例）。抓取清单与逐页类型见任务期 `_scratch/echarts227/`
> （page_types.json + ledger.txt），本文件是其可表达性结论的落库版。
> 审计方式：逐示例页提取 option 键，映射到 `stdlib/Gui/Component/Chart/`
> 当前 API；`docs/CHART_VS_ECHARTS_224.md` 是 2.2.4 口径的**缺口账本**，本文件是
> 2.2.7 全示例的**可表达性账本**（六批补齐后，多数原缺口已闭合）。

## 图例
- **✅ 全表达**：该示例的视觉与交互均可用现有 option/API 复刻。
- **◑ 部分**：主图可表达，个别 2.2.7 子选项无对应字段（在备注列出）。
- **✅（原 ✗）**：`timeline` 动态时间轴与 `connect()` 多图联动曾单列「有意不做」，
  后续轮次已全部落地（ChartTimeline 组件 + connectGroup 状态重定向），见 §18。
- 括号内字段名是本封装的落点（`ChartSeries.*` 未注明前缀）。

## 1. 柱状（bar）——8 页
axis.html / bar.html / bar1-15.html / event.html / dynamicLineBar.html(柱侧) /
mix1-12(柱侧) / legend.html(柱侧) / tooltip.html / index.html(柱侧) / theme.html
- 直角柱 + 堆叠 + 双轴：`ChartSeries.Bar`，`axisIndex/stackName/axisType`。✅
- 负值零基线 / markLine/markPoint / 值域缩放 / smooth / 阶梯：`markLine`（含
  average/min/max/threshold 类型）、`markPoint`、`zoom`/`dataZoom`、`smooth`、
  `step`（超出 2.2.7）。✅
- symbol 装饰柱帽（bar13/14：`symbol:'circle'`, `symbolSize`）：批 1
  `symbol/symbolSize`。✅

## 2. 折线（line）——9 页
axis.html / line.html / line1-9.html / event.html / dynamicLineBar.html(线侧) /
mix1/2/6/8(线侧) / legend.html / tooltip.html
- 基础折线 / areaStyle / 多序列 / markLine（line1/7）：`areaStyle`、`markLine`、
  `lineStyle`。✅
- smooth / step / symbol 点形状：`smooth`、`step`、`symbol/symbolSize`（批 1）。✅
- 轴标签旋转/抽稀（axis.html `interval`/rotate）：批 2
  `ChartAxis.labelRotate/labelInterval`。✅
- 折线↔柱状切换（dynamicLineBar 的 `magicType`）：工具箱已有 line↔bar
  magicType（`toolboxShow`），语义对齐。✅

## 3. 散点（scatter）——9 页
scatter.html / scatter1-6.html / dynamicScatterK.html(scatter 侧) /
mix4/7(scatter 侧) / dataRange1.html
- 值域着色（dataRange1/5/6）：批 5 `dataRangeShow/Lo/Hi/Calculable/Split/List` +
  `rampColors/rampLo/rampHi`，散点消费 `dataRange` 过滤。✅
- 气泡尺寸（symbolSize 回调）：批 1 `ChartData.sizeSet/size`（逐点数值）+ 批 A2
  `symbolSizeFn`（"y"/"ySqrt"/"x"/"xSqrt" 公式化替代回调，半径随点坐标线性/开方，
  开方形面积∝值即气泡语义）。✅
- markLine/markPoint：✅

## 4. K 线（k / candle）——4 页
k.html / k1.html / dynamicScatterK.html / mix8/10(k 侧)
- 蜡烛本体 + 涨跌色（2.x `itemStyle.normal{color/color0,lineStyle}`）：`ChartSeries.K`
  + 涨跌色字段。✅
- dataZoom / markPoint：`zoom/dataZoom`、`markPoint`。✅

## 5. 饼 / 玫瑰（pie）——10 页
pie.html / pie1-7.html / lasagna.html / mix2/9/12(pie 侧) / dynamicPieRadar(pie 侧)
- 玫瑰（`roseType`）、起始角（startAngle）、内外径（donutPct）、标签引线
  （labelLine）、排序（sort）：`roseType/startAngle/donutPct/labelLine/sort`。✅
- 饼扇区点击选中偏移：批 4 `data[].selected` + PieSel 状态机 + selectedOffset。✅
- emphasis 高亮（hover 外偏）：批 4 `EffectiveColor/Border*` 接入扇区。✅

## 6. 漏斗（funnel）——5 页
funnel.html / funnel1-4.html / mix11(funnel 侧)
- 排序（sort asc/desc）、金字塔（Pyramid）：`sort`、`ChartSeries.Pyramid`。✅
- 水平对齐 funnelAlign（funnel4）：批 6 `funnelAlign:'left'|'right'|''`。✅
- 层间隙 / 逐层转化率：批 6 `funnelGap`、`funnelPercent`（label + tooltip）。✅

## 7. 雷达（radar）——5 页
radar.html / radar1-3.html / wormhole.html / dynamicPieRadar(radar 侧)
- 指标轴（indicator）、max、areaStyle 填充、polar 坐标：`radarMax`、`areaStyle`、
  极轴渲染器。✅
- 多边形 vs 圆（shape）、交替环带（splitArea）：批 4 `radarShape/radarSplitArea`。✅
- 虫洞雷达（wormhole，同心缩放）：`radarShape:'circle'` + splitArea。✅

## 8. 仪表（gauge）——6 页
gauge.html / gauge1-5.html / mix11(gauge 侧)
- 车速 / 进度 / 半圆 / 多仪表 / 分区色：`ChartSeries.Gauge` + `gaugeMax` +
  `GaugeColors`。✅
- `precision` 定点（2.2.7 仍在）：封装保留（超集）。✅

## 9. 热力 / 日历热力（heatmap）——3 页
heatmap.html / heatmap2.html / heatmap_map.html(map 侧)
- 矩阵热力 + 日历热力：`ChartSeries.Heatmap`、`CalendarHeat`。✅
- 值域过滤：批 5 dataRange（heatmap 消费值着色）。✅

## 10. 事件河流（eventRiver）——2 页
eventRiver1/2.html
- 主题带 + 时间轴排布：`ChartSeries.EventRiver` + `events[]`（布局纯函数
  chart_event_river_layout 已测）。✅

## 11. 和弦（chord）——5 页
chord.html / chord1-4.html / webkit-dep2(chord 侧)
- 缎带（ribbon）/ 非缎带（`ribbonType:false`/`innerPct`）：`innerPct` +
  批 6 `chordRibbonType:'path'`。✅
- 权重排序 sort / 子弧 sortSub / 节点间隙 / 外环宽：批 6
  `chordSort/chordSortSub/chordGapDeg/chordRingWidthPct`。✅
- markPoint（chord.html）：批 A3——权重最大节点弧外侧标 max pin
  （`markPoint` 通道，chord.html 的少见用法确定性落地）。✅

## 12. 力导向（force）——6 页
force.html / force1/2/4.html / webkit-dep.html / webkit-dep2(force 侧)
- 节点重数、自环、边权：`ForceLayout`（links 首现序、边去重）。✅
- scaling / gravity / min-maxSize / 节点连线配色：批 6
  `forceScaling/forceGravity/forceMinRadius/forceMaxRadius/forceNodeColor/forceLinkColor`。✅
- 节点 symbol 形状（`symbol` 逐节点）：`tree` 承载 ChartNode.Shaped +
  `ForceLayout.ApplyShapes`。✅
- roam（force1 拖拽漫游）：`roam`。✅
- 入场动画（force 节点自初始位收敛）：批 A5——圆周初值快照 + 按
  入场进度 g 的线性补间（确定性，缓存只读插值安全）。✅
- 注：2.2.7 force 的**随机**初始位不可逐帧确定复刻；本封装用圆周确定性初值 +
  固定轮数弹簧模拟，观感近似、结果可缓存（有意取舍，非能力缺口）。

## 13. 地图（map）——23 页
map.html / map1-22.html / dataRange.html / dataRange2.html / heatmap_map.html /
mix3/5(map 侧) / index.html(map 侧) / map14/map19(timeline)
- 省界多边形 + 挖洞（子区域）：`ChartMapRegion/ChartMapRing(hole)`。✅
- 值域着色 ramp + dataRange 过滤：`rampColors/rampLo/rampHi` + 批 5 dataRange。✅
- roam 漫游 / 区域选中（selectedMode single|multiple|false）：`roam` +
  批 4 `mapSelect` + `selectedMode`。✅
- 省界数据：`ChartGeoJson`（官方 china.json 运行时解码）+ 内嵌副本。✅
- markLine/markPoint 叠地图：批 A3——`marks[]` 显式 ChartMarkPoint.Point
  的 x/y 解释为图坐标系（随 roam/zoom 平移缩放），`o.markPoint` 自动
  max/min 质心标注，共用 DrawMapMark 图钉。✅
- map14/map19 timeline：`ChartTimeline`（见 §18）。✅

## 14. 树 / 矩形树（tree / treemap）——6 页
tree.html / tree1-2.html / treemap.html / treemap1-2.html
- 树向（LR/RL/TB/BT）、径向：`treeOrient`。✅
- treemap 下钻 / 面包屑 / 层级值：`ChartNode` 树 + Treemap 渲染器。✅
- roam（tree2）：`roam`。✅

## 15. 字符云（wordCloud）——1 页
wordCloud.html
- 字号按权重 + sizeRange：批 6 `wcMinFontSize/wcMaxFontSize`。✅
- textRotation 角度候选（2.2.7 是 `[0,90]` 二选列表）：批 6 `wcRotate:'vary'`
  + 批 A1 `textRotation:[...]` 任意角度候选集（JSON 数组归一化到 ±180，
  按词条序确定性取角，布局缓存随角度集失效）。✅

## 16. 韦恩（venn）——1 页
venn.html
- 集合圈 + 交叠标签：`ChartSeries.Venn` + `custom`（chart_venn_layout 已测）。✅

## 17. 交互组件（toolbox / legend / dataZoom / dataRange / tooltip）
dataZoom.html / dataZoom1.html / legend.html / dataRange*.html / tooltip.html
- dataZoom 百分比 start/end + 拖拽条：`zoomStart/zoomEnd` + `DataZoom()` +
  DrawZoomBar；toolbox `dataZoom` 型 = 现有 zOn + `DataZoom(start,end)` 参数化。✅
- legend formatter / orient 横纵 / selectedMode 多选单选关：批 5
  `legendFormatter/legendOrient/legendSelectedMode`。✅
- dataRange：连续 calculable 滑条 + 分段 splitList：批 5 全套。✅
- `hoverLink`（dataRange↔地图反向联动）：批 A4——悬停 dataRange 条置
  高亮键，地图区域/散点按当前值域窗口描边强调（`HoverHighlight` seam）。✅
- tooltip：`showTooltip` + 各渲染器 TooltipCard。✅

## 18. timeline 与 connect（原「有意不做」，已全部落地）
- **timeline 动态时间轴**（bar11/map14/map19/pie7/scatter4 的 `timeline`）：
  批 B `ChartTimeline`——多帧 option 序列 + 底部播放器条（播放/暂停、
  前/后帧、帧轴点选、帧标签）+ autoPlay（节拍判断用挂钟、帧号/播放态/
  上拍时刻全部持久化在 App 状态键，即时模式宿主每帧重建不丢）+
  `Merge` 帧合并（baseOption 外壳保留、同名系列逐帧换数据）+
  JSON `timeline.data[]/options[]` 解析；gui_charts pie7 已接线。✅
- **多实例 connect() 联动**（官方 mix 部分）：批 C `connectGroup`——组内
  图表的交互态键（dataZoom 窗口/图例开关/十字线悬停类别）经组长登记
  （`stateWid`）跨图同帧共享；`CartesianTooltip` 兼作联动 seam（指针不在
  本图上时画跟随十字线）；gui_charts mix9 已接线。✅
- 共享坐标系大重构（架构注记 A：整图族定位共存层）仍为独立后续项——
  它是 mix3/mix11 那种「跨图族同画布叠画」的重构，与 connect 的
  「多图状态联动」正交，2.2.7 的 connect 语义已由本批覆盖。

## 19. 汇总
- 134 页中：主视觉与示例级交互可表达 ✅ **134/134**（含 timeline 与 connect 两类原 ✗ 项）。这不是“所有 ECharts option、实例方法和事件回调完全等价”的声明；完整兼容性分层见 `docs/CHART_COMPATIBILITY_227.md`。
- 补齐批次：批 1 symbol 族、批 2 markLine 自动线 + 轴标签、批 3 HBarCore、
  批 4 emphasis + 选中 + radar、批 5 dataRange + legend + toolbox、
  批 6 force/chord/funnel/wordCloud 参数化；批 A1 词云角度集、A2 散点
  symbolSizeFn、A3 地图/弦图 markPoint、A4 dataRange hoverLink、A5 force
  入场补间、B timeline 子系统、C connect() 联动。
- 逐图族仍存的确定性取舍（非能力缺口）：force 随机初值 → 圆周确定性初值。
- typed 事件基础已经落地，line/bar/scatter/heatmap/pie/map 已有首批数据点/图元 payload；仍不等价于完整 ECharts DOM `event.params`、任意 formatter 或完整 JavaScript `on/un`。其他 L3 能力、可配置导出、以及跨图层共享坐标系仍需单独建设；当前实现的已知边界和差异以 `docs/CHART_COMPATIBILITY_227.md` 为准。
