> **已归档（一次性审计快照，2026-09-10，不再维护）。**
> 本报告是对 `ready: true` 的 335 个示例做的首次官方渲染全量对照，其结论已并入
> `docs/CHART_COMPATIBILITY_5.md`（迁移账本）与 `docs/CHART_CODE_GAP_LEDGER.md`
> （代码级对照），两者冲突时以后者为准；下方正文保留为历史审计依据。

# Zan 图表引擎移植完整度审计报告

**审计日期**：2026-09-10
**审计范围**：`examples/gui_charts` 注册表中 `ready: true` 的 **335** 个示例
**官方基准**：ECharts **v6.1.0**（`echarts-master`，与本地下载目录下的同名源码树同源，MD5 一致）
**实现**：`stdlib/Gui/Component/Chart/`

---

## 一、审计口径（先对齐，再看数字）

| 概念 | 定义 |
|---|---|
| `ready: true` | 注册表标记为"可实现"的示例 = **本报告的全量审计对象**，差异即缺陷 |
| `ready: false`（12 条） | 主动标记未实现，**不计入缺陷** |
| 注册表缺失（30 条） | d3 / bmap / matrix 等依赖不可移植，主动剔除，**不计入缺陷** |

**结论的前提**：这 335 条双侧 option JSON 逐字一致（`options/<id>.json` 由官方源码转换而来），
所以任何渲染差异都指向**引擎**，而不是数据。

---

## 二、方法可信性（为什么这组数字可以信）

前两次度量方案**被自己证伪并废弃**，记录如下：

| 方案 | 自检结果 | 结论 |
|---|---|---|
| 12×12 墨迹网格距离 | 同一张图重采样后距离 **0.25** | ❌ 得到"95.5% 严重差异"是噪声，**废弃** |
| 主色**面积**阈值 | 细线仅占 0.15% 像素，重采样后集合坍缩为空 | ❌ 不可用，**废弃** |
| 主色**集合**最近邻匹配 | 重采样自检 **1.000 / 0.923 / 1.000** | ✅ 采用 |
| 着色像素占比（固定步长 3） | 步长统一后 `line-simple` / `treemap-simple` 结论稳定 | ✅ 采用（见下） |

**当前方案的判据**（固定采样步长 3，饱和度阈值 40）：

```
ZAN_MISSING   zan < 0.003 且 off ≥ 0.02   → 绘图区无内容，图根本没画出来
ZAN_SPARSE    zan < 0.40 × off            → 画了，但内容远少于官方
ZAN_THIN      zan < 0.75 × off            → 明显偏少
OK            其余
OFF_THIN      off < 0.02                  → 官方本身内容极淡（细折线），不做判定
```

**抽样人工核验**（3/3 确认）：

| 示例 | 官方着色 | Zan 着色 | 人工查看 Zan 截图 |
|---|---|---|---|
| `treemap-simple` | 0.8424 | 0.0011 | ✅ 确认绘图区全白 |
| `candlestick-simple` | 0.0764 | 0.0010 | ✅ 确认绘图区全白 |
| `bar-race-country` | 0.3614 | 0.0025 | ✅ 确认仅有空坐标轴，无柱子 |
| `graph` | — | — | ✅ 确认文字提示 "(no data)" |

---

## 三、量化结论

### 3.1 总览（335 例）

| 判定 | 数量 | 占比 |
|---|---|---|
| `ZAN_MISSING` 未渲染 | **82** | 24.5% |
| `ZAN_SPARSE` 远少于官方 | **38** | 11.3% |
| `ZAN_THIN` 明显偏少 | 14 | 4.2% |
| `OK` 基本一致 | 70 | 20.9% |
| `OFF_THIN` 官方本身过淡，不判定 | 131 | 39.1% |

**在 204 例可比样本中**（剔除官方过淡的折线类）：

| 判定 | 数量 | 占可比样本 |
|---|---|---|
| `ZAN_MISSING` | 82 | **40.2%** |
| `ZAN_SPARSE` | 38 | **18.6%** |
| `ZAN_THIN` | 14 | 6.9% |
| `OK` | 70 | 34.3% |

> **核心数字：在内容量可比的 204 个官方示例中，58.8% 存在"图没画出来"或"内容大幅缺失"级别的缺陷。**

### 3.2 分类失败率（`ZAN_MISSING` + `ZAN_SPARSE`，仅列 ≥3 例的类别）

| 类别 | 失败/总数 | 失败率 |
|---|---|---|
| treemap 矩形树图 | 7/7 | **100%** |
| sunburst 旭日图 | 7/7 | **100%** |
| pictorialBar 象形柱图 | 7/7 | **100%** |
| dataset 数据集 | 7/7 | **100%** |
| parallel 平行坐标 | 3/3 | **100%** |
| graph 关系图 | 5/5 | **100%** |
| matrix 矩阵图 | 8/9 | 89% |
| candlestick K 线图 | 5/6 | 83% |
| scatter 散点图 | 8/10 | 80% |
| calendar 日历图 | 6/8 | 75% |
| radar 雷达图 | 2/3 | 67% |
| heatmap 热力图 | 2/3 | 67% |
| other（多为 doc-example） | 29/47 | 62% |
| line 折线图 | 6/10 | 60% |
| bar 柱状图 | 11/38 | 29% |
| sankey 桑基图 | 2/6 | 33% |
| custom 自定义 | 1/3 | 33% |
| chord 和弦图 | 1/4 | 25% |
| pie 饼图 | 1/13 | **8%** |
| funnel 漏斗图 | 0/4 | **0%** |

**读法**：饼图、漏斗图、普通柱状图**基本可用**；而**矩形树图、旭日图、象形柱图、数据集、平行坐标、关系图是整类失效**。

### 3.3 另发现的确定性缺陷（已肉眼确认，不依赖统计）

| # | 缺陷 | 证据示例 | 影响 |
|---|---|---|---|
| 1 | **`title` 组件缺失** — option 的 `title.{text,subtext}` 被当成窗口标题栏，图内不绘制 | `pie-simple`（官方图内居中大标题+副标题，Zan 无）；`area-simple`、`line-marker` 同 | 全量（凡带 title 的示例） |
| 2 | **双 y 轴未生效** | `line-marker`：官方蓝线走左轴 9~15°C、绿线走右轴 -3~6°C；Zan 两线共用一个 0~16 轴 | 所有双轴示例 |
| 3 | **markPoint 未实现** | `line-marker`：官方有蓝色 "1"、"9"、绿色 "2" 圆标，Zan 全无 | markPoint 系列 |
| 4 | **markLine 未实现** | `line-marker`：官方虚线+端点值 "11.14"/"1.57"，Zan 全无 | markLine 系列 |
| 5 | **面积渐变填充失效** | `area-simple`：官方红橙渐变，Zan 纯粉平涂 | 所有渐变面积 |
| 6 | **y 轴 nice 分割与官方不一致** | `scatter-simple`：官方 0~10 步长 2，Zan 4~8 步长 1 | 全量散点/数值轴 |
| 7 | **时间轴标签密度失控** | `area-simple`：Zan 时间标签重叠成一团 | 时间轴系列 |
| 8 | **折线默认 symbol 未开启** | `line-simple`：官方每点带白色填充圆点，Zan 无 | 折线系列 |
| 9 | **轴标签字号偏大** | 交叉验证：`line-simple` Zan 着色 0.0067 vs 官方 0.0030（细线图反而更"重"） | 全量 |
| 10 | **图例默认位置计算错误** | `pie-simple`：官方图内左下，Zan 跑到左上角压导航栏 | 全量 |

---

## 四、ZAN_MISSING 全清单（82 例）

### 整类失效（100%）

- **treemap（7）**：`treemap-simple`、`treemap-disk`、`treemap-obama`、`treemap-visual`、`treemap-drill-down`、`treemap-show-parent`、`treemap-sunburst-transition`
- **sunburst（7）**：`sunburst-simple`、`sunburst-book`、`sunburst-drink`、`sunburst-monochrome`、`sunburst-borderRadius`、`sunburst-label-rotate`、`sunburst-visualMap`
- **pictorialBar（6）**：`pictorialBar-spirit`、`pictorialBar-hill`、`pictorialBar-body-fill`、`pictorialBar-vehicle`、`pictorialBar-velocity`、`pictorialBar-bar-transition`
- **graph（5）**：`graph`、`graph-npm`、`graph-circular-layout`、`graph-label-overlap`、`graph-webkit-dep`
- **parallel（3）**：`parallel-aqi`、`parallel-nutrients`、`scatter-matrix`
- **dataset（3）**：`dataset-default`、`dataset-link`、`data-transform-sort-bar`

### matrix（7）

`matrix-simple`、`matrix-stock`、`matrix-mbti`、`matrix-pie`、`matrix-covariance`、`matrix-correlation-heatmap`、`matrix-correlation-scatter`

### candlestick（4）

`candlestick-simple`、`candlestick-sh`、`candlestick-large`、`candlestick-brush`

### calendar（4）

`calendar-simple`、`calendar-heatmap`、`calendar-charts`、`calendar-graph`

### 其它

- **heatmap**：`heatmap-large`、`heatmap-large-piecewise`
- **themeRiver**：`themeRiver-basic`、`themeRiver-lastfm`
- **radar**：`radar-custom`、`radar2`
- **bar**：`bar-race-country`、`bar-drilldown`、`bar-multi-drilldown`、`bar-polar-label-radial`、`bar-polar-label-tangential`、`bar-stack-normalization-and-variation`
- **line**：`line-aqi`
- **scatter**：`scatter-effect`
- **sankey**：`sankey-simple`
- **doc-example（19）**：`axis-label-align-min-max`、`bar-media-timeline`、`candlestick-axisPointer`、`data-transform-multiple-sort-bar`、`mix-timeline-all`、`parallel-all`、`pictorialBar-clip`、`pictorialBar-graphicType`、`pictorialBar-position`、`pictorialBar-repeat`、`pictorialBar-repeatDirection`、`pictorialBar-repeatLayout`、`pictorialBar-symbolBoundingDataArray`、`pictorialBar-symbolSize`、`pie-media`、`sunburst-color`、`sunburst-highlight-ancestor`、`sunburst-highlight-descendant`、`sunburst-label-align`、`sunburst-simple`、`timeline-dynamic-series`、`treemap-borderColor`

---

## 五、优先级排序（按影响面 × 修复成本）

### P0 — 影响面最大，且高度可复用

| 项 | 影响示例数 | 说明 |
|---|---|---|
| **1. `title` 组件** | 全量（>200） | 组件级缺失，一次实现覆盖绝大多数示例的"图内标题"差异 |
| **2. 轴标签字号 / 默认样式对齐官方 12px** | 全量 | 全局默认值，改动集中 |
| **3. 图例默认位置计算** | 全量 | 默认值修复 |
| **4. 折线默认 symbol** | 全部折线 | 默认值修复 |

### P1 — 整类图表失效（有明确实现路径）

| 项 | 影响示例数 | 说明 |
|---|---|---|
| **5. treemap 矩形树图** | 7 | 整类未渲染，含 drill-down 交互 |
| **6. sunburst 旭日图** | 7 | 整类未渲染 |
| **7. graph 关系图 + force 布局** | 5 | 整类未渲染（当前显示 "no data"），依赖力导向布局算法 |
| **8. pictorialBar 象形柱图** | 6 | 整类未渲染 |
| **9. dataset + dataTransform** | 7 | 数据集声明式装配未实现 |
| **10. candlestick K 线** | 4 | 整类未渲染 |
| **11. calendar 日历坐标系** | 4 | 整类未渲染 |

### P2 — 局部能力缺口

12. markPoint / markLine（`line-marker` 等）
13. 双 y 轴 `yAxisIndex` 路由
14. 面积图 `LinearGradient` 解析
15. 时间轴标签密度控制
16. matrix（依赖 custom series，8 例）
17. parallel 平行坐标（3 例）
18. y 轴 nice 分割算法对齐

---

## 六、可复现的审计流水线

全部脚本在 `_scratch/`（git-ignored）：

| 脚本 | 作用 |
|---|---|
| `gen_official_html.js` | `options/<id>.json` → 自包含 HTML（还原 `__grad` 渐变标记） |
| `batch_official.sh` | headless chrome 并行渲染 335 张官方基准图 |
| `render_audit.py` | **主审计脚本**：固定步长着色占比 + 分级判定 |
| `palette_diff.py` | 主色集合匹配（辅助，用于交叉验证） |
| `compare_shot.ps1` | 生成单例上下并排对比图 |

**官方截图命令**：
```bash
CH=".../chrome-headless-shell.exe"
"$CH" --headless --disable-gpu --no-sandbox --hide-scrollbars \
  --window-size=1200,780 --virtual-time-budget=2500 \
  --screenshot=official_shots/<id>.png file:///.../official_html/<id>.html
```

**产物**：
- `_scratch/official_shots/` — 官方基准 335 张
- `_scratch/shots/` — Zan 侧 375 张（含 335 ready + 40 历史）
- `_scratch/render_audit.json` / `.txt` — 逐例判定结果
- `_scratch/cmp_<id>.png` — 并排对比图

---

## 七、结论

用户的判断成立，且可以定量表述：

> **在 335 个标记为"可实现"的官方示例中，有 204 个的内容量足以客观比较；
> 其中 120 个（58.8%）存在"完全没画出来"或"内容大幅缺失"级别的缺陷。
> 另有 4 类缺陷（title 组件、双 y 轴、markPoint/markLine、渐变填充）影响几乎全部示例。**

需要修正的一个措辞：**不是"几乎所有演示都有差异"** —— 饼图（8% 失败率）、漏斗图（0%）、
普通柱状图（29%）**大面积可用**；差异是**高度集中在特定图表类型与特定组件**上。
这比"普遍不一致"的判断更精确，也更容易定出修复顺序。
