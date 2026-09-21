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

---

## 快速上手

### 1. 折线图 / 柱状图示例

```zan
using System;
using System.Collections.Generic;
using Gui;
using Gui.Component.Chart;

class ChartDemo : Window {
    public ChartDemo() {
        Chart chart = new Chart();
        chart.SetBounds(20, 20, 600, 400);

        // 创建图表配置
        ChartOption opt = new ChartOption();
        opt.title = "月度销售额趋势";
        
        // 设置 X 轴（类目轴）
        opt.xAxisCategories = new List<string>{ "1月", "2月", "3月", "4月", "5月", "6月" };

        // 添加折线系列
        ChartSeries s1 = ChartSeries.Line("销售额", new List<int>{ 120, 200, 150, 280, 220, 310 });
        opt.series.Add(s1);

        // 应用配置并刷新
        chart.SetOption(opt);
        this.Add(chart);
    }
}
```

### 2. 饼图 / 环形图示例

```zan
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
chart.SetOption(opt);
```

### 3. 切换主题

```zan
// 支持直接设置预设主题名称
chart.SetTheme("macarons"); // 或 "dark", "shine", "blue" 等
```
