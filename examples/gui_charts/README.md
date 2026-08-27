# Zan Charts

Standalone gallery for `Gui.Component.Chart`, modelled on the
[ECharts 2.2.7 example site](http://static.dtws110.com/echarts-2.2.7/doc/example.html).

Each nav entry is one example with its own `ChartOption` and dataset. Deep-link
by id, English name, or Chinese title:

```
build\charts_test.exe line1
build\charts_test.exe "标准折线图"
build\charts_test.exe word1
```

Build (Windows):

```
powershell -File scripts\build_charts.ps1
```
