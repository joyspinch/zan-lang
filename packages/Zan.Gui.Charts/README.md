# Zan.Gui.Charts - 纯 Zan 专业级数据可视化与业务图表库

`Zan.Gui.Charts` 是面向 Zan GUI 的原生数据可视化与商业图表套件。完全用 Zan 语言标准库和确定性整数几何流水线绘制，**零 JS、零 DOM、零 WebView 依赖**，具备毫秒级冷启动与极低内存占用。

---

## 核心特性

- **17+ 种原生图表类型**：
  - **基础图表**：折线图 (`Line`)、柱状图/条形图 (`Bar`)、面积图 (`Area`)、饼图/环形图/嵌套环 (`Pie`)、散点图 (`Scatter`)。
  - **金融与统计**：K线图/蜡烛图 (`K` / `Candlestick`)、箱线图 (`Boxplot`)、瀑布图 (`Waterfall`)、误差棒 (`ErrorBar`)。
  - **业务与监控**：仪表盘 (`Gauge`)、漏斗图 (`Funnel`)、热力图 (`Heatmap`)、日历图 (`Calendar`)、事件河流图 (`EventRiver`)。
  - **关系与层级**：雷达图 (`Radar`)、力导向图 (`Force`)、和弦图 (`Chord`)、矩形树图 (`Treemap`)、树图 (`Tree`)、旭日图 (`Sunburst`)、桑基图 (`Sankey`)、韦恩图 (`Venn`)、词云 (`WordCloud`)、地图 (`Map`)。
- **高确定性与高可靠性**：内部采用确定性整数定点数计算，零系统时钟与浮点抖动，支持像素级自动化回归断言。
- **丰富的坐标轴能力**：类目轴 (`category`)、数值轴 (`value`)、时间轴 (`time`)、对数轴 (`log`)、双 Y 轴及断轴 (`axis.breaks`)。
- **开箱即用的交互体验**：悬浮工具提示 (Hover Tooltip)、图例点击过滤 (Legend Toggle)、数据区域缩放 (DataZoom)、标记线与标记点 (MarkLine / MarkPoint)。
- **精美预设主题**：内置 `blue`, `dark`, `echarts6`, `macarons`, `shine`, `mint`, `sakura`, `roma`, `green`, `red`, `helianthus` 等多种配色方案。
- **现代化正交架构**：
  - **策略渲染器分派（`SeriesRendererRegistry`）**：采用策略模式解耦 32 种系列绘制算法，支持用户通过 `SeriesRendererRegistry.Register` 无缝扩展自定义渲染策略；
  - **清晰工具层划分**：`ChartColor` 专注于色彩模型、透明度与色阶梯度；`ChartGeometry` 专注于网格盒模型、Nice 刻度划分与投影映射；`Chart.zan` 保留门面转发桩，100% 保持向后兼容。

---

## 快速上手

### 1. 窗口控件形态（`ChartHost`）

```zan
using System;
using System.Collections.Generic;
using Gui;
using Gui.Component.Chart;

class ChartDemo : Window {
    public ChartDemo() {
        ChartHost chart = new ChartHost();
        chart.SetBounds(20, 20, 600, 400);

        // 创建图表配置
        ChartOption opt = new ChartOption();
        opt.title = "月度销售额趋势";
        
        // 设置 X 轴（类目轴）
        opt.xAxisCategories = new List<string>{ "1月", "2月", "3月", "4月", "5月", "6月" };

        // 添加折线系列
        ChartSeries s1 = ChartSeries.Line("销售额", new List<int>{ 120, 200, 150, 280, 220, 310 });
        opt.series.Add(s1);

        // 应用配置并挂载至控件树
        chart.SetOption(opt);
        this.Add(chart);
    }
}
```

### 2. 即时渲染形态（`ChartView`）

```zan
// 即时模式帧渲染循环中直接调用
ChartOption opt = new ChartOption();
opt.title = "市场份额占比";

ChartSeries pie = new ChartSeries("份额", "pie");
pie.data = new List<ChartData>{
    new ChartData("产品 A", 45),
    new ChartData("产品 B", 25),
    new ChartData("产品 C", 20),
    new ChartData("其他", 10)
};
opt.series.Add(pie);

// 在 App 帧绘制中单行呈现
ChartView.Of(opt).Render(app, x, y, width, height);
```

### 3. 切换主题

```zan
// 支持在 ChartHost 控件上设置预设主题名称
chart.SetTheme("macarons"); // 或 "dark", "shine", "blue", "echarts6" 等

// 亦支持全局切换当前图表皮肤主题
ChartSkin.SetCurrent("echarts6");
```

### 4. 从表格数据出图（`ChartBridge`）

`ChartBridge` 把 `System.Data` 的 `DataFrame`（列存表格）接到图表上，
省掉"手写 source 数组 + 类目轴 + 系列数据"这一段。

```zan
using System;
using System.Data;
using Gui.Component.Chart;

DataFrame df = DataFrame.FromCsv(File.ReadAllText("sales.csv"));
// 表头：city,sales,group

// 单系列柱状图
ChartOption opt = new ChartOption();
opt.title = "各城市销售额";
ChartBridge.AddSeries(opt, df, "销售额", "city", "sales", ChartType.Bar);

// 一表多系列（group 列的每个取值一条系列，按首次出现顺序）
ChartOption opt2 = new ChartOption();
ChartBridge.AddGrouped(opt2, df, "group", "city", "sales", ChartType.Bar);

// 也可以只要中间产物，自己装配
ChartSeries s = ChartBridge.CategorySeries(df, "销售额", "city", "sales", ChartType.Bar);
ChartAxis ax = ChartBridge.CategoryAxis(df, "city");
opt.series.Add(s);
opt.xAxes.Add(ax);
opt.yAxes.Add(ChartAxis.Value());

// 需要 ECharts dataset 语义（transform / encode）时
ChartJsonDataset ds = ChartBridge.ToDataset(df, "sales");
opt.datasets.Add(ds);
```

**要点**：

- 依赖方向是 `Gui.Component.Chart → System.Data`（反向会让数据层多带
  297 个编译单元）。
- 桥直接填 `series.data`——ECharts 的 dataset→series 自动绑定发生在
  option **解析期**，程序化构造的 `ChartOption` 不走那条路。
- 数值走 `double` 通道，小数不被截断（整数道会抹平小数项）。
- 列名写错抛 `InvalidOperationException`（静默产出空图比报错难查）。
- 描述统计在 `Zan.DataFrame` 的 `Stats`/`DataFrameStats`，配套用例见
  `tests/conformance/analytics_bridge.zan`。
