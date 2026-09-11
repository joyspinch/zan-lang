---
name: testing-charts-gallery
description: Zan charts gallery (examples/gui_charts, 335 ECharts 官方对照 demo) 的代码级完整度对照与实机验证定式——scripts/chart_gap_audit.py 的未读配置键倒排/源引用率（未读键按父路径+源默认值分诊，别把官方导出的缺省当缺口）、源函数逐条对抄、数值 oracle（ECharts getLayout 归一化对比、bar 布局样板、scale 翻转对照——布尔语义只比方向不比端点）、三态/枚举类缺省（`'auto'`≠`false`，如 showAllSymbol）、颜色/字号预算闸门、recheck2 截图驱动、bindprobe 探针、--bench 帧计时、stdlib 快照同步坑、多 grid/dataZoom/定点数值三大契约。凡是要盘点图表迁移完整度、修复 stdlib/Gui/Component/Chart 引擎改动、核对某个 demo 与 ECharts 官方语义是否一致、排查"图表空板/缺元素/多面板窗口不同步"时使用。
---

# Charts gallery 验证与修复定式（Windows 实机）

## 口径：先比代码，后谈截图（2026-09-11 定调）

**截图不是完整度判据。** 引擎的完整性按 **ECharts 源函数**记账；
"跑 demo 看图"抓不到语义错误，而且这种账本会自己腐烂。

为什么（本仓实测，踩过的坑）：

- ECharts 6.1 有 **596** 个 `src/*.ts`；引擎 35 个文件里只有 **6 个**
  引用过源（共 14 个源文件），核心的 `barGrid.ts` / `LineSeries.ts` /
  `PieView.ts` / `Scale.ts` / `axisNiceTicks.ts` / `symbol.ts` /
  `LegendView.ts` / `ChartView.ts` 一个没引。大多数代码是 2.2.7 时代
  手写的（`ChartModel.zan` 的 `ChartType` 注释自己写着 2.2.7），v6 语义
  没抄——这就是"有些完全就是错的"的来源，不是玄学。
- `docs/CHART_PORT_AUDIT_2026-09-10.md` 的"82 例白板"基于 2026-09-10
  02:54 的构建，它引用的 Zan 侧截图 `_scratch/shots/`（335 张）**现在只剩
  5 张**，`render_audit.json` 早于其后 4 个修复提交。截图口径的账本
  既不自证正确，也留不住。
- 着色占比只能抓白板。已核实的三个语义错误在着色占比里全都"正常"：
  ① 柱宽公式 `ChartViewBar.zan:351-354` 手写 `step*7/10`、`Scale(2)` 像素
  间距，对照 `layout/barGrid.ts:206-349` 的 `barCategoryGap` 求解
  （**已修**：`ChartBarLayout.zan` 严格对抄，见"数值对照 oracle"节）；
  ② 折线符号漏了 `chart/line/LineView.ts:367-401` 的数值轴提前返回与
  `canShowAllSymbolForCategory()` 通过分支（**已修**：`showAllSymbol`
  三态 + `Chart.CanShowAllSymbolForCategory` + `ThinSymbolsFor`，
  见契约 11）；
  ③ `axis.scale` 从不读取，且 `AxisMinForF/AxisMaxForF` 初值为 0 →
  量程无条件并入 0，但 `coord/axisModelCommonMixin.ts:33` 只在
  `!scale` 时并入。**已修**：`ChartModel.zan` 读 `"scale"`、
  `Chart.zan` 四个 extent 函数增 `bool scale`、`NiceRange(lo,hi,scale)`
  做**单侧零并入**、`ChartViewScatter.zan` 的 `ScatterRangeX/Y` 成为
  绘制与命中唯一的域推导（见 oracle 节的"scale 翻转对照"）。

定式：

1. 动手前先跑 `python scripts/chart_gap_audit.py`（仓库根）。它列出官方
   option 用到、而引擎**从不通过 Json 访问器读取**的配置键（当前
   274/470），以及每个引擎文件引用了哪些源。**未读键 = 静默丢弃 =
   这张图不可能对**，先看这里再决定修哪个。
   **但"未读"不等于"该修"——先按父路径分类**（`python` 遍历 option 打印
   每个键的父路径计数，比按文件名猜快得多）：官方的 ECharts option 导出
   会把**引擎默认值也写进文件**，于是 `gridIndex:0`（69 次，几乎全是单
   grid 的 0）、`axisLine.onZero`、`animationDuration:1000` 这类"配置原文
   里的缺省"也会进未读榜。分辨方法：看父路径 + 查源里的默认值——
   - `readOnly` 全在 `/toolbox/feature/dataView` 下 → 只是 dataView 面板
     的只读开关，跳过。
   - `gridIndex` 多轴各出现一次 → 多 grid demo 才真需要（按
     `xAxisIndex`/`yAxisIndex` 已能路由时不急）。
   - `axisLine.onZero` 的源默认是 `'auto'`（**不是 `true`**，
     `coord/axisDefault.ts:62`），且 `Grid.ts:637-640` 注释写明：显式
     `undefined` = 不 onZero、**不写** = onZero，bar/candlestick 因
     `containShape` 被 `discourageOnAxisZero` 排除——语义在源码里是反直觉
     的，不看源就照着键名修必错。
   - `animation*` 全是进入动画，静态渲染器不适用。
   真正该修的是"父路径落在渲染语义上、源默认非平凡"的键：`labelLine`
   （饼/漏斗引线）、`rich`（富文本）、`showAllSymbol`（见契约 11）。
2. 引擎新增/修改的逻辑，注释必须写 ECharts 源 `文件:行`。没有出处 =
   这条无法审计，等于欠债（审计工具就是按这个引用率给结论的）。
   **"已修"必须能在工作区 grep 到那行代码 / 有 conformance 用例支撑**：
   台账/提交信息写着"加了守卫"而代码里没有，就是僵尸结论——空 points
   越界（B3）这样假报修好一次，直到 2026-09-11 实测复现出
   `Chart.zan:3031 list index out of bounds` 才补上。复现靠"直接调
   `Chart.AxisMaxForF(series, 0, 2, 4)`"这种最小探针（比开窗口截图快得多）。
3. 代码级账本：`docs/CHART_PORT_AUDIT_2026-09-10.md` 是**已提交**的
   完整度审计报告；逐条施工台账在 `_scratch/ECHARTS_PORT_LEDGER.md`
   （A=缺席 / B=已修 / C=待定位 / D=验证纪律）。按源函数记账，
   **修一条删一条**，不留已完成项。
4. 截图留给**渲染层**问题：控制点已与源一致、画出来仍不对的那种
   （如 smooth 的"麻花"，见下节——控制点手算与 JS 参考逐值相同，
   病在采样率/描边光栅器）。语义层用截图判等会误判，渲染层才是它的地盘。

## 数值对照 oracle：布局/量程这类纯计算，直接对 ECharts 自己算的数

"逐函数对抄"之后仍要证明抄对了。几何/布局/刻度这类**纯计算**不要靠
截图，走数值 oracle：让 ECharts 把它的中间结果吐出来，Zan 侧用**同一份
option** 复算，逐字段 diff。

- **权威钩子**：`seriesModel.getData().getLayout('offset'|'size'|'bandWidth')`
  ——即 `layout/barGrid.ts:calcBarWidthAndOffset` 的输出，由
  `createCrossSeriesLayoutHandler.overallReset` 经 `data.setLayout` 挂上。
  不要量 rect（首帧显示表里第一个矩形常是被裁剪的残片，会得到 3 个假
  不一致；柱溢出绘图区时尤其）。绘图区原点/跨度用 `grid.getRect()`；
  `axis.getExtent()` 返回的是相对 [0,span]，**不是**绝对像素。
- **归一化再比**：`offU = offset/band×1e4`、`wU = size/band×1e4`。两引擎
  绘图区尺寸不同也不影响；只有 px 形态的选项（`barWidth:20`、
  `barMinWidth:12`、`barMaxWidth:8`）才依赖 band，**把 ECharts 报的 band
  原样喂给 Zan 探针**。可直接复用的骨架在 `_scratch/chart_oracle/`
  （带 README，`clean_scratch` 跳过）：`bardemo.js` 出
  `bardemo.oracle.txt` → `bardemo.zan` 读 oracle 行取 band/n，走真实
  `ChartOption.FromJson` + 渲染层同一选取规则 + `ChartView.CalcBarCols`，
  输出同格式后 `diff --strip-trailing-cr`。探针必须用**工作区** stdlib
  编译（`--auto-stdlib`，别带 `--stdlib-path _scratch/stdlib_snap`，
  那份快照会落后——本地 6:13 的 gallery 构建就是拿 3:45 的旧快照跑的，
  新柱布局根本没进去）。
- **覆盖要分两层**：① 构造配置打边界（默认/显式 px/%/负 gap/堆叠/混
  堆叠/min-max 宽度/"末个声明系列覆盖"语义）——23 组；
  ② **真实 demo 打集成**（`examples/gui_charts/options/bar-*.json`：纵向
  11 / 横向 4 / 瀑布 2，含 `bar-tick-align` 的 `xAxis` 数组形态）——17 组。
  两层都 IDENTICAL 才算这条修完。真实 demo 层还能顺带查出类目数推导
  （Zan 侧 n 与 ECharts 的 n 不一致时输出 `MISMATCH-N`）。
- **ECharts SSR 进程不退**：`echarts.init(null,null,{renderer:'svg',ssr:true})`
  留下未清的帧句柄，脚本跑完仍挂住（`head` 管道下表现为"永远在跑"）。
  末尾 `process.exit(0)`；后台跑时重定向到文件再读。

### scale 翻转对照（布尔语义配置的通用手法）

`axis.scale` 这类**布尔开关**的正确性不是"算出一个数"就能验的，要比
"翻转后往哪边动"。做法（骨架 `_scratch/chart_oracle/axisdemo.js` →
`axisdemo.oracle.txt`）：把同一个真实 demo 文件喂 ECharts **两遍**
（原样 / `scale` 取反），各报 `axis.getExtent()` 的 lo,hi，输出
`written=T|t=lo,hi|f=lo,hi|diff=loDiff,hiDiff`；Zan 侧对同一文件跑同样
两遍（`axiswire.zan` 走真实 `Chart.BuildAxes`，`scatterwire.zan` 直接调
`ScatterRangeX/Y`）。

- **只比方向，不比绝对值**：本引擎 nice 步长是 ECharts 2.2.4
  `smartSteps` 的 1/2/2.5/5 候选，ECharts 6 是 `intervalScaleNiceTicks`
  的 1/2/5——`scatter-simple` scale:true 时 ECharts `y=[3,9]`、本引擎
  `y=[2,10]`，都"紧致"却是不同端点。**端点差异是噪声，`diff` 的符号
  才是语义**。这条同样适用于 min/max、inverse、stack、boundaryGap。
- 结果口径（2026-09-11）：`axisdemo` 20 条可比案例，Zan 14/15 行/柱
  方向一致；唯一例外 `line-gradient` 是多 grid demo（引擎不支持，记债）。
  散点侧原样值与 oracle **逐位相同**（`scatter-weight`/`scatter-effect`/
  `scatter-aggregate-bar` = `x[140,200] y[40,120]`），`scatter-simple`
  缺省 `y[0,10]` 也相同——同时证明了"旧代码硬编码 scale-true"确实是可见
  错误（旧值 `[3,9]` 形态）。
- **不可比的 demo 别硬比**：`custom-*`（ECharts 侧报 series.render 缺失）、
  ecStat 变换（`bar-histogram`/`data-transform-*`/`scatter-*-regression`）、
  中国地图/geo（`scatter-map-brush`/`effectScatter-map`）在 oracle 侧
  本身就跑不出来；标 `SKIP=原因` 而不是塞进对照。

### 名字/文本类几何的 oracle：读 SSR 出来的 SVG，并**关掉启发式**

轴名（`axis.name`）这类"文本放在哪"的问题没有数值钩子，只能读渲染产物。
ECharts SSR（`{renderer:'svg', ssr:true}`）出来的 SVG 里每个 `<text>` 都带
绝对 `transform="translate(x y)"`（或 `matrix(...)`）与 `text-anchor`，
正则扫出来即页面像素——zrender 把组变换**烘进了元素自身**，没有嵌套 `<g>`
可回溯（探针里写"向上找父组"是白费劲，`<g>` 只在少数元素上出现）。

- **先关启发式再量**：`nameMoveOverlap` 缺省 **true**（`AxisBuilder.ts:522-525`），
  轴名会被 `resolveAxisNameOverlapDefault` 从重合的刻度标签上推开——
  实测 `yAxis.nameLocation:'end'` 带系列时从 `(axisX, plotY-gap)` 被推到
  **绘图区水平中心**（`y@255,25` vs 无系列时 `y@50,25`）。要拿"基位"就
  给该轴加 `nameMoveOverlap:false`，剩下的才是 `AxisBuilder.ts:855-880`
  的纯数学。Zan 不移植这个启发式（确定性优先），只对基位。
- **探针要跑三态，不要猜缺省**：对同一轴强制 `start|middle|end` 各渲一遍，
  三态位置一次钉死；再用**不强制**的第四遍确认缺省落点与哪一态逐位相同
  （`nameLocation` 官方缺省 `coord/axisDefault.ts:34` = `'end'`）。
  骨架 `_scratch/chart_oracle/axisname.js`（输出即 `axisname.oracle.txt`）。
- **别用最小 option**：`series: []` 会让 `axis.getAxesOnZeroOf()` 仍然
  报 onZero（`canOnZeroToAxis` 只看对侧轴类型），但矩形/量程退化会把
  中间结论带偏。**rect 一律用 `grid.getRect()` 打出来**，探针输出的
  坐标必须能对着它念（expl: rect=100,100,400,200 ⇒ `x end` 页面 x =
  100+400+15 = 515）。
- **旋转态看 `matrix` 不看 `translate`**：y 轴 `middle` 是绕锚点
  **−90°**（自下而上读），SVG 里是 `matrix(0,-1,1,0,cx,cy)`；正则只抓
  `translate(...)` 会把它读成 `notranslate` 而整条漏掉（第一版探针就漏了，
  差点把"y 轴 middle 竖排"这个语义漏掉）。`DrawTextRot(x, y, …)` 的
  `(x,y)` 是未旋转行盒左上角、绕它刚性旋转，故 `matrix` 的 `(cx,cy)` 与
  Zan 的 `(x,y)` **不是**同一个点——映射见 `Chart.AxisNameY`（锚点下移半
  个文本宽）。
- **`base` 是 Zan 保留字**（`src/compiler/lexer.c:28` 的 `TK_BASE`）：
  写 `int base = …;` 报 `expected variable name`。改叫 `endY`/`startY`。
- **普查清单会自截断，结论前直渲染复核**：普查脚本里
  `slice(0,40)`/`slice(0,30)` 只打印清单头部，"不在打印清单里"≠"在
  两者都有桶"（126 个"都没有"绝大多数不打印）。给结论前用
  `onzero_lib.js` 的 `render()` 对可疑 demo **逐个直渲**看
  `#54555a` 线——pictorialBar-spirit/bar-race/pie-simple 这类被
  想当然归进"两根都画"的，官方 SSR 本就一根轴线都不画
  （2026-09-12 T3b 复核时抓的）。
- **轴线/轴名的随动矩阵（onZero，已实测钉死）**：骑线只搬
  ① 轴线本身、② start/end 轴名（贴线走）；**tick 标签不动**
  （`AxisBuilder.ts:1566` 的 labelOffset 抵偿）、**middle 轴名不动**
  （`AxisBuilder.ts:869` pos.y=labelOffset+nameDir×gap 抵回边界）。
  官方 `__getRawCfg().labelOffset` 在最终 resize 后是**陈旧值**
  （dynamic-data 报 -255 而 SVG 线在边界），判"有没有动"以 SVG
  线位置为准。
- **改完纯函数跑旧 golden**：给 `AxisNamePosPx` 加骑线覆盖时曾把
  middle 态的 axisLine 置 -1"回边界"，实际把 `-1` 喂进了
  `ay = axisLine + gap`（ay=14）——`chart_axis_name` 的旧 golden
  当场抓住。教训：新增"覆盖/回退"分支时，回退目标必须是**已换算
  好的基位变量**，不是哨兵值；且改共享几何函数必须先跑全部相关
  golden。
- **普查得 0 先怀疑 cwd，别急着信数据**：bash 工具的 cwd 跨调用
  持久——上一条命令 `cd _scratch/chart_oracle` 之后，普查脚本的
  相对 glob `examples/gui_charts/options/*.json` 落空 ⇒
  "total pie series: 0" 的彻底假阴性（饼 demo 明明在）。写完普查
  先 `pwd` 或一律绝对路径；"0 条"与"清单自截断"是两类假阴性，
  一个查路径一个查 slice。

### 别用像素当"数据墨迹"的探针（本仓库无头 App 下不可用）

想验量程紧不紧，直觉是"渲染两遍、diff 出数据像素的包围盒"。两条都
踩死过：

- **隐藏系列再 diff**：隐藏会连带改轴范围与图例，两遍**不可比**
  （得到 `botDelta=0|topDelta=0` 的假阴性）。
- **按饱和度筛数据点**：本构建无头 `App` 整屏铺高饱和主题底色
  （实测 `0x448C7B`），任意行带都命中，筛不出数据点（三个 demo 输出
  完全相同）。

正确做法：**把定义域提成纯函数 seam，直接调引擎函数**。为此
`ChartView.ScatterRangeX/Y(o, series, i0, i1)` 从 `DrawScatterCore` 与
`ScatterHover` **两处各写一遍**的重复块里提取出来——重复本身就是 bug
源（0 锚定只改了一边 → 命中错位），提取后探针和渲染读的是同一份代码。

## 构建（快照 stdlib，避开并发会话的在途编辑）

并发会话常在改 `stdlib/System/Net/...`（编译会挂）。图库构建走快照：

```bash
# 快照只需建一次；此后【每次改 stdlib 后必须重新同步】，否则构建
# 静默用旧代码——修好的 bug 看起来"还在"，排查浪费几轮（踩过 3 次）。
cp stdlib/Gui/Component/Chart/*.zan _scratch/stdlib_snap/Gui/Component/Chart/

./build/zanc.exe examples/gui_charts/gui_charts.zan examples/gui_gallery/MapChinaData.zan \
  --auto-stdlib --stdlib-path _scratch/stdlib_snap \
  --embed examples/gui_charts/options --embed examples/gui_charts/charts-registry.json=charts-registry \
  --embed examples/gui_charts/maps --libpath build \
  --link-lib zan_gui_charts_gnu --link-lib ws2_32 --link-lib mswsock --link-lib psapi \
  --link-lib advapi32 --link-lib dwmapi --link-lib gdi32 --link-lib imm32 --link-lib user32 \
  --link-lib rpcrt4 --link-lib ole32 -o _scratch/charts_pc.exe
```

- 快照是**双刃**：它让构建绕开并发会话的在途编辑，也会把"修好"
  静默吞掉——提交前必须 `diff stdlib/... _scratch/stdlib_snap/...`
  核对工作区与快照逐字节一致（Render.zan 曾在快照里有
  DrawPolyBatch 而工作区没有，`git status` 全绿、构建全过、改动丢了）。
  修完 Zan 源码后立刻 `diff -q` 校验；提交范围以**工作区**为准。
- `ole32` 是并发会话的 WASAPI 音频引入的；少它链接失败时先想依赖漂移。
- **实机坏了先做归属二分，再深挖自己的 diff**（2026-09-12 T4-1 踩的）：
  pie-nest 塌板（扇区消失、图例掉底）一度像标签重写弄坏渲染——把标签
  逻辑体 `if (false && …)` 屏蔽仍坏（**声明还在，不是真基线**）。
  正确一步：`git stash push -m t41-bisect -- <自己的4个文件>`（定向
  path spec，规则 11 许可的方式）→ 重编"HEAD 图表代码 + 当前环境"
  → 依旧坏 ⇒ 并发会话的调度器/帧调度在途回归，与本次改动无关，
  `git stash pop` 还原继续。屏蔽逻辑体会误导；stash 才是干净基线。
- **塌板形态速判**：扇区不可见但引线/标签落在**最终几何** =
  `animationType:'scale'`（elasticOut）入场停在中途（半径≈0 而布局
  已算完），不是布局崩——加长等待（5s 重拍）区分"截早了"与"永久
  停帧"；pie-nest/pie-roseType/pie-rich-text 塌、pie-simple/
  pie-legend（默认展开动画）不塌，即此特征。
- GUI 子程序**没有 stdout**：`Console.WriteLine` 不可见。追踪一律
  `System.IO.File.AppendAllText("D:/project/zan-lang/_scratch/dbg_xxx.txt", ...)`，
  绝对路径；收尾必须剥离。

## 截图单 demo（recheck2.ps1）

```bash
mkdir -p _scratch/shotsN   # OutDir 不存在时 GDI+ Save 直接抛异常
powershell -File _scratch/recheck2.ps1 -OutDir D:/project/zan-lang/_scratch/shotsN -Ids <demo-id>
```

- 参数是 **-Ids**（不是 -Demo）；bash 里 `powershell -File ... -Ids a,b,c`
  不会拆数组——逐个跑 for 循环。
- zanc 的进度杂音（"compiling code generators..."）走 stderr，PowerShell
  把它升级成 NativeCommandError，构建脚本会**假失败退出 1 且无真实诊断**。
  判定成败用独立探针跑同款 zanc 参数并显式打印 `$LASTEXITCODE`
  （如 `_scratch/zanc_charts_probe.ps1`），别信包装脚本的 throw。
- demo id 用 **registry 全名**（`scatter-anscombe-quartet` 而非
  `anscombe-quartet`）；打错 id 应用会静默回落到首个 demo，截图对不上号。
- recheck2 会杀旧进程→启动→最大化→截图；它把窗口临时 TOPMOST，
  并发会话的置顶工具窗可能压在截图上——重拍或最小化对方窗口，
  别隔着遮挡下结论。
- 看细节用 python PIL 裁剪放大，别靠整图目测。

## 探针

- **bindprobe**（`_scratch/bindprobe.zan`）：解析态 Dump——grids/axes/series
  的 data/points/candles 计数与首值。改 Dump 列表后用同款 embed 参数编译。
  「缺元素」先分清是**解析没进数据**还是**渲染画不出来**，探针定分界。
- **--bench**：`charts_pc.exe <demo> --bench` 渲 30 帧写 `_scratch/bench.txt`，
  卡顿量化用。

## 引擎三大契约（踩坑出处）

1. **Gap 不进极值、不连线**：ECharts data 项 `'-'` 解析为
   `ChartData.Gap()`（gap=true, val=0）。任何包络计算、折线段对 `s.Value(i)`
   的读取都必须先查 `s.data[i].gap`，否则 MA 前段把量程拖到 0，
   scale:true 紧致包络被撑爆（candlestick-touch 蜡烛压成细线 + 空成交量）。
2. **多 grid 子面板**：grid 矩形是纯绘图区（ECharts 语义）——
   标题/工具箱/图例是页级元素，派发层画一次；子面板若走 PanelContentTop
   会被 59px 头部吃掉（96px 成交量条带只剩 30px 绘图区）。
   dataZoom 窗口**全格生效**（官方 xAxisIndex:[0,1] 同窗联动），
   滑条条带只随最后一个 grid 画一次（`Chart.zoomBarHidden` 帧内静态）。
3. **×1000 定点只到 2.1e6**：柱/线渲染把值 ×1000 定点后走
   `YOfF/YOfFL`。亿级整值（成交量 8.6e7）×1000 = 8.6e10 **爆 int32**，
   柱子全体消失。大值路径一律 long：`YOfFL(long vF, int axis)` +
   调用方 `(long)(d * 1000.0 + 0.5)`。横向柱的 `v×g/1000` 链（ChartViewBar
   1083/1216 一带）同坑未修——值 >2.1e6 的横向柱要接 YOfFL 化。
4. **并行坐标 `layout:'vertical'`**：名字指**轴的排布方向**——vertical =
   轴从上到下堆叠、每根轴横向（官方 nutrients 样子）；默认 horizontal =
   轴从左到右、每根轴纵向（parallel-aqi）。不是"横着的轴叫 horizontal"。
   `parallelAxis[].dim` 显式绑定数据列（无 dim 按数组槽位），
   `visualMap` piecewise `categories` 模式按行在
   `dimension`（缺省 1）列找类目名取 `inRange.color`（官方 25 色由
   `echarts.color.modifyHSL('#5A94DF', hStep*i)`、hStep=round(300/(n-1))
   彩虹生成，末色 #5ADF8A）。
5. **密集折线图用 Canvas.DrawPolyBatch**：数千行平行坐标逐行
   `DrawPolyline` 在 GL 后端每行付一次完整覆盖缓冲清除+合成（45ms/帧
   主要来源）。`DrawPolyBatch(xs, counts, color, thickness)` 把 N 条
   同色互连路径并进**一次**覆盖缓冲周期（runtime `polybatch` vtable
   op，CPU 后端自动回落逐行）。行色互不相同的图先按色分桶再批量。
   配套流式渲染：ECharts `progressive` 语义 = restore 首帧快照 +
   每 24ms 预算增量画行 + 帧尾 SnapshotRect 累积。
6. **option JSON 一律单行紧凑**：`examples/gui_charts/options/*.json`
   全仓约定单行（separators=(',',':')）；pretty-print 过的 nutrients
   曾到 14.5 万行 1.17MB。改 option 用 Python json 重新序列化紧凑输出，
   diff 才能落在一行内可审。
7. **ChartOption 新增列表字段要拷三处**：`Create()`、`Clone()` 之外还有
   `ResolvedChart.DrawOption`（ChartResolved.zan 逐字段组装渲染用
   option）——漏第三处的症状极阴险：解析探针（直接 FromJson 后读字段）
   全对、渲染却回落缺省值。polar 落地时 angleAxes 漏拷 DrawOption，
   startAngle=0 探针打印正常、渲染整图转 90°（ax=null 走缺省 90）。
   新字段先 grep `o.polars = new List` 的三处落点再收工。
8. **极坐标/新坐标系投影全程保持 ×1000 milliunit**：points 存的是
   值×1000（pointG）。任何"先 PointV 除回整数再算"的写法都会双重
   失真——量程推导把 0..0.5 的小数域炸成 0..5（line-polar2 花瓣缩成
   点），角度 ×1000 当度数用再 mod 360 出锯齿螺旋（line-polar 心脏线
   两轮返工的根因）。定点参与运算、除回放最后一步；非整度角配
   SinDegX10/CosDegX10（整度值线性内插，1° 内曲率误差 <0.02%）。
9. **ECharts 极坐标角度语义**（对源 polarCreator.ts 核过）：
   startAngle = 轴值 0 所在的数学角（0=东、90=上，缺省 90），
   逆时针为正；angleAxis extent = [startAngle, startAngle+360]；
   屏幕 x = cx + r·cos(θ)，y = cy − r·sin(θ)（y 翻转）。
   line-polar 官方是 r=5+5sinθ 的心脏线、cusp 朝下——不是圆。
10. **布尔配置门控只加到"零并入"这一处，别顺手门控 nice 上界**：
   `scale:true` 的正确改法是把零并入（`NiceRange` 的 `!scale` 块 /
   `AxisLo`）关掉，**不是**把上界推导也门控。全负数据 + 显式固定 min 时
   门控会让 `NiceMax(-30)`（本仓库对 2.2.4 的旧近似，返回 `5`）当成上界
   ——比原来的 `hi=0` 锚更歪（ECharts 6 的 interval ceil 落在 0）。
   同理 `min == max` 的兜底在 ECharts 里位于 `_calculateValue`、
   `_reformValue`（零并入）**之前**，所以它**不受 scale 门控**——Zan 的
   `NiceRange` 也把这段留在 `!scale` 块外。
11. **三态/枚举类配置的缺省常是 `'auto'`，不是 `false`——把缺省当关会系统性
   丢元素**。`line.showAllSymbol` 的源默认是 `'auto'`（`LineSeries.ts:210`），
   `getIsIgnoreFunc`（`LineView.ts:367-401`）的语义是：`true` 全显；`'auto'`
   **能放下就全显**（`canShowAllSymbolForCategory`，源码注释 "we show all
   symbols as possible as we can"）；`false` 才一律抽稀；无类目轴也全显。
   Zan 此前把缺省当"按标签间隔抽稀"，于是 30 点这类"符号放得下、标签放
   不下"的类目折线丢了大半拐点。落地要**三态**存 `-1/0/1`（`'auto'` 字符
   串也要认）而不是 bool；判定函数对抄 `LineView.ts:472-492`——`availSize =
   plotW/n`、`step = n/5` 抽样至多 5 点、任一点 `size×1.5 > availSize` 即
   放不下（逐点 `data[i].size` 覆盖也要折成 device 像素：本引擎 plotW 是
   device 像素，option 的 symbolSize 是 CSS 像素，两边不同单位直接比会在
   高 DPI 下偏）。抽稀/不抽稀的**决策**要提成 per-series 函数给多个符号
   pass 共用（`ThinSymbolsFor`），否则又是一个"两处各写一遍"的漂移源。


## 已知刻意偏差（勿当 bug 修）

- candlestick-touch.json 的 grid px 已 ×2.4（适配本机更高的画布）。
- scatter-matrix.json 删了 parallel 系列（引擎无平行坐标系，记 TASKS 债）。
- media 响应式查询、graphic 元素不支持（data-transform-multiple-pie
  竖排是 base option 的样子，官方横排来自 media，不是 bug）。

## ctest 档位

stdlib Chart 改动 → `cd build && ctest -R conformance_chart`（26 例，~96s）；
calendar 相关再加 `ctest -R conformance_gui_chart_calendar`（离屏 Canvas
几何/墨迹断言）。Chart 目录还挂两条 **smoke 级预算闸门**，改颜色/字号时
必跑：`policy_theme_color_budget`（`stdlib/Gui/**` 里除 Theme/Style/
StyleBox/Fx 外不得直读 `t.textPrimary` 这类语义色）、
`gui_theme_font_budget`（不得直读 `t.fontSize*`）。预算逐文件为 0，
要取颜色/字号走 `Style.Part(app,"chart","label",...).FgOr(0)` /
`Style.FontFallback(app,"small")`——皮肤才覆盖得到。（2026-09-10 的
calendar 提交直读 `t.textPrimary`/`t.fontSizeSmall`，两条闸门常红到
2026-09-11 才修；闸门是逐行正则扫描，同一行出现两次算两个。）

**离屏"墨迹"判据看不见叠写文字**：`InkStats` 那种"非纯白即算墨"的计数
在**填色格**上失效——格子本身已被计入，往上写黑字不新增任何像素
（`conformance_gui_chart_calendar` 的"农历日名确实写出来"断言就这么假红
过，实现其实是对的）。判"文字画出来了"要数**深色像素**（RGB 三通道
< 96），且用"开/关 label 两趟之差"消掉网格自带的深色轴标签。

两个 golden（chart_option_behavior.out 的 sr、chart_pie_layout.out 的 pal）
曾在语义提交（symbolSize 直径、v6 色板）时没跟上，属欠账——引擎语义
提交必须连 golden 一起核对，否则 standard 档永远挂着看不见的失败。
断言里读**定点存储**字段（`points[].x` 存 值×pointG）要先
`Chart.PointV(v, s.pointG)` 除回，否则改定点倍率就把解析测试扫成假红
（chart_specialized_json 的 `points[0].x` 就这么红过）。

## 渲染帧克隆税（大数据 demo 卡顿排查顺序）

大数据 demo 卡顿先量化三处税源，别急着怀疑渲染器本体（`--bench` 对照）：
1. **逐项深拷**：`ChartSeries.Clone` 默认全量深拷 data/points/candles 等
   大集合。渲染帧对数据集合只读的类型走 `Clone(src, shareData:true)`
   共享引用（`ResolvedSeries.Materialize(allowShare)` 按类型门控）。
   **pie/funnel 系必须保持全量克隆**——它们经 `ApplyDataLegend` 写
   `data[].hidden/selected`，共享会把交互态漏写进长寿缓存的源 option。
   审计法：grep 渲染帧路径全部 `s.data`/集合写入点，逐个确认只在
   drawOption/克隆列表上写（ChartView ApplyDataLegend 是唯一例外源）。
2. **外壳标量漏抄**：`Clone(src, shareData)` 的早退共享分支会让
   "早退之后才赋值"的字段全部漏抄。force*/chord*/funnel*/wc*/label*
   等标量外壳字段必须在早退**之前**抄完——conformance drb6 探针
   （DrawOption 物化后核对批 6 字段）就是抓这个的，别删探针迁就。
3. **平滑细分步数**：BuildPathFxEx 固定 48 步/段在 600 段×2 边界的
   时间轴面积图上每帧近百万插值点。步数按段像素跨度自适应
   （定点域 ÷256，钳 2..48）——窄段亚像素插值由描边光栅化器采样，
   收到 2 步也无可见折角；宽段保持原平滑度。
另外 multi-grid 子面板曾每帧 `ChartOption.Clone` 整个 option（全系列
逐项深拷）：子面板改的只有 xIndex/axisIndex 两个整数，改成 Create 空壳
+显式重建 axes/grids/titles/visualMaps+系列共享原对象（渲染只读）。

## 快照也是灾备

并发会话 checkout/分支切换会静默覆写工作区（本会话 4 个 Chart 文件被
覆写、git 历史与全部 stash 均无痕迹）。每次改完 stdlib 同步进快照的那份
副本**就是最近一次验证过的现场**：发现工作区被覆写时先
`md5sum` 对比工作区与快照、`grep` 快照里的关键标记（如新增函数名），
从快照整文件恢复再 `git diff --numstat` 核对范围，能省掉全部重写。
快照恢复后必须重跑编译探针 + ctest 档位——被覆写可能同时吞掉后续
手工修复（本会话 drb6 标量漏抄修复就被快照回滚了一次）。

## 平滑曲线（`smooth: true`）的 ECharts 语义与实现坑

ECharts line 的平滑算法不是「单调 Hermite / Cardinal / Catmull-Rom」，
是 **`poly.ts:drawSegment`**（`src/chart/line/poly.ts:36-209`）的
自研贝塞尔。关键事实——从源码逐行确认：

- `LineView.ts:118-119` `getSmooth(s) = isNumber(s) ? s : (s ? 0.5 : 0)`
  — `smooth:true` ≠ 1，是 0.5（张力系数）。`smooth:0.3` ≠ smooth:true`。
  引擎若把 `smooth` 当 0/1 bool 处理，`smooth:0.3 / 0.5 / 0.8` 画出来一样。
- `LineView.ts:854` 把 `series.smoothMonotone` 透传给 polyline；
  `LineSeries.ts:200` 默认 `null`。`poly.ts:134/143/152` 的三分支中
  唯一走的是 `else`（无单调约束的通用贝塞尔）。
- `poly.ts:152-191` 七步：cp1/nextCp0 初算 → nextCp0 钳到 [x,nextX]×[y,nextY]
  → cp1 反算 → cp1 钳到 [prevX,x]×[prevY,y] → nextCp0 再反算（**此处不再钳**）。
  poly.ts 没有「切线归零」，所以极值点不会水平搁架——而是轻微过冲。
- `poly.ts:91-101` 跳过**严格重复点**（X 和 Y 都相同）。bump-chart
  的 Pasta 2002=2003=#1（**Y 相同但 X 不同**）不在此列。

**实现时的关键陷阱**（session 4 三次尝试都栽在这里）：

1. **「局部极值点的切线归零」是单调 Hermite 的特征，不是 ECharts 的。**
   把单调 Hermite 替成 ECharts 贝塞尔后，bump-chart 的「顶上变水平」
   变成「顶上过冲」——也是 ECharts 的真行为，不是 bug。
2. **控制点正确 ≠ 渲染正确。** 三次尝试后**手算控制点**已与 ECharts
   JS 参考完全一致，但**渲染仍然「麻花」**——根因在下游
   （bezier 采样率 `steps = segW/256` 在长斜线段可能降到 2，
   把贝塞尔退化成折线；或描边光栅器对子像素控制点的处理不同）。
   排查**必须**用探针把控制点和 `steps` 同时打印出来，
   至少验证：`cp0.x/cp0.y/cp1.x/cp1.y` 与 `_scratch/official_shots/`
   同一数据下采样后一致。
3. **Zan 窗口 3222 宽、官方 1200 宽——同一算法视觉差 2.7×。**
   同样 75px 过冲，官方看不见、Zan 显眼。**不要靠像素 diff 判等**，
   要把 Zan 用 1200×780 的等比裁切后再比（见 _scratch/compare_shot.ps1）。
4. **Zan 凸图（bump-chart）的「端点过冲」是 ECharts 行为**。
   如果产品想要不过冲，得改默认 `smooth` 张力（如 0.3），
   **或**用 `smoothMonotone:'x'` / `'y'`，**或**画分类型图（line 改 bar）。
   引擎层不修。
5. **「鼠标经过没响应」可能是 IME 抢焦点**——微软拼音/搜狗候选窗口
   出现在 Zan 截图中时，是 IME 在拦截输入，**不是 hover 不工作**。
   先 dismiss IME 再复现。
6. **会话中**改 smooth 算法超过两次没收敛——立即回退
   `git checkout HEAD -- stdlib/Gui/Component/Chart/ChartViewLine.zan`
   并把「无结论」写进 CHART_RESIDUAL。**不要**继续在 commit 之间
   反复猜测。ECharts 6.1 poly.ts 是一段精密但**对稀数据不稳定**的
   算法，凭眼睛和直觉调试不收敛。

**已验证 commit**：
- `ec24a91b` 修复了 Y-extent（dataZoom 窗口）、axisLabel.margin、
  time-axis `points.x` 去定点。**不**碰 smooth 算法。

## 富文本（label.rich）语义与实现坑（T4-2 slice1，全部 SSR 解码/实机踩出）

语义权威不是想象，是官方 SSR 的 SVG（`_scratch/chart_oracle/rich_nest.svg`）。
关键定则，每条都踩过坑：

1. **盒高 = 各行声明 lineHeight 之和，hr 不回落字体高**。pie-nest 官方
   盒高 55 = 22 + 0 + 33——`height:0` 的 hr 段贡献 0，**没有**
   "高度 0 就用字体高兜底"的逻辑。想当然兜底会多出一条空行高。
   文本在行内垂直居中（dominant-baseline central 于行中心）。
2. **align 是"整行内容跑位"，不是"该段自己变宽"**。无 width 且声明
   align 的段 → 该行全部内容在块内平移（pie-nest 标题居中：61 宽块和
   140 宽块都是内容中心对块中心）。把这种段"拉伸到块宽"会把表头
   （Weather|Days|Percent）挤出块外——踩过。
3. **percent/`width:'100%'` 段是覆盖层**：不进 contentW，永远锚在行
   x=0（pie-nest 的 abg 暗带从块左缘铺满，锚到 xShift 会右溢——踩过）。
4. **padding 在 width 之外**：zrender 段盒 = width + 水平 padding
   （pie-rich-text 值列 width:20 + padding:[0,20,0,30] → 实宽 70）。
   只按 width 画 → 值压进百分号（"202"叠"55.3%"——踩过）。
5. **两遍绘制**：先全部段背景（含 borderRadius 四角掩码
   `Corner.TL()|TR()|BR()|BL()`，mask==All 用 FillRoundRect），再全部
   文本——否则前景段的背景盖住上一段文字。
6. **分词器**：`\n` 后的尾随空文本段不发射，但**整行无任何段时**要发
   一个空段占行号（空行占高）；未知样式名 `{zz|x}` 按纯文本原样保留。
   换行把 `{name|…}` 的 name 段与下一段分行。
7. **新增 ChartTextStyle 字段必须扩展 `ChartResolved.Materialize`**：
   它 new 一个新 ChartTextStyle 逐字段搬旧值——rich/box/pad 忘搬时
   解析探针全对、实机标签整体消失（rich=0），排查烧了一轮。下一个
   给 label 加字段的人还会踩：加字段后 grep Materialize。
8. **饼图 bodyY 别被底部图例钳死**：图例循环逐项把 bodyY 推到图例底，
   底部浮层图例（ECharts 图例是浮层不占几何）会把全部外圈标签 clamp
   到面板底挤成一行（pie-nest 首次实机就是这样）。修法：图例外层
   `if (bodyY > y + h/2) { bodyY = top; }`——只有图例在上半板才占几何。
9. **负 padding 取整要远离零**：`(int)(-20.0+0.5)` = -19（C 向零截断），
   gauge-speed 的 padding:[0,0,-20,10] 需要 RoundOff(d) = d>=0 ?
   (int)(d+0.5) : (int)(d-0.5)。borderWidth/borderRadius 同理。
10. **单点解析**：rich/盒样式只在 `ParseTextStyle` 解析一次，
    series.label / data[i].label / 以后的 axisLabel、markPoint.label、
    gauge detail 都汇到这一处，别各自开分支。
11. **JSON 导出丢失函数型 formatter——别照 ported 语料对齐视觉**。官方
    demo 的 axisLabel.formatter 常是 JS 函数（bar-race-country 旗标、
    bar-rich-text 天气图标、intraday-breaks-1 周末置灰），gallery 的
    options/*.json 只剩 rich 样式表、没有令牌来源——照 JSON 渲染出
    "朴素标签"就是忠实；引擎要补的是**字符串模板 + `{name|…}` 壳**
    的通用路径，不是给单个 demo 硬编码图标。
12. **壳（rich 样式空 `{}`）≠ 有视觉**：bar-label-rotation 的
    `rich.name` 是空对象——官方与 plain 同像素。formatter 带壳时
    量宽/旋转/LabelText 走 `Chart.RichStrip` 剥壳，直接 DrawText 会
    把花括号画出来（s.labelFmt 修复的暴露面，踩过）。
13. **换绘制路径必须保几何**：带状类目标签原来走 DrawTextCentered
    （水平+垂直都居中），换成富文本块时 plain 分支也要逐像素同位
    （vertical center 差 2px 会扰动全部 bar/line demo 的 x 标签）。
14. **回归归属判定：HEAD 对照构建**。怀疑"我的改动把某 demo 搞空白"
    时：`for f in <改动文件>; do git show HEAD:"$f" >
    _scratch/chart_head/$(basename $f); done` → cp 进 stdlib_snap →
    探针脚本用 python replace 产出 `-o charts_pc_head.exe` 变体重编 →
    同 demo 对拍。HEAD 同样错 = 先前已存在，记账不修；HEAD 对而我错
    = 真回归。拍完**把工作区文件 cp 回 stdlib_snap**（快照恒留当前
    工作态）。B14/B16 两次用此法归因（bar-race-country 空白、
    scatter-linear-regression 挤压、line-race 崩溃）。
15. **对照两侧都要新鲜重拍**：第一张截图出现"本次改动不可能造成的
    差异"时，先重拍一次再排查代码——重拍即消失 = 截图竞态（旧 exe/
    旧进程画面）。HEAD 对照的两侧必须在同一会话、用各自验证过的
    exe 各拍各的（B15 踩过：把 HEAD 侧整板 raw 壳文本误判成新构建
    的回归，插桩后才发现新构建是干净的、spill 正是 HEAD 要修的病）。
16. **绘制路径插桩直接抓 stdout，别从截图反推几何**：绘制分支里临时
    `Console.WriteLine`（系列名/cx,cy/richN/走了哪个分支），
    `charts_pc.exe <id> --nomouse > out 2>&1`、sleep 数秒 taskkill，
    逐帧打印一次定位。附带两个小坑：ParseColor 恒带 0xFF 通道、
    未声明色哨兵是 0，debug 打印色值 **-1 = #FFF 白**，不是未解析；
    recheck2 `-Ids` 从 bash 一次只传一个 id（PowerShell string[]
    绑定把 `a,b,c` 并成一个 id，产出 "a,b,c.png"）。

## labelLayout / triggerOn 与截图基建（T4-2f 会话教训，全部实机踩出）

1. **窗口截图一律 PrintWindow(hWnd, hdc, PW_RENDERFULLCONTENT=3)，别
   CopyFromScreen**：屏幕区域抓取抓的是"那块屏幕"，有置顶/覆盖窗就
   拍到覆盖物——本会话"整板深蓝空白、进程活着、无 stderr"排查半天
   （HEAD A/B、重编 zanc 全试遍），其实是覆盖物；PrintWindow 一发即
   真身。EnumWindows 按标题找到窗口后直接 PrintWindow，不必
   BringToTop/SetForegroundWindow（非前台进程调用会静默失败）。
   recheck2.ps1 已改。
2. **PrintWindow 位图尺寸 ≠ 内容尺寸**：DPI 虚拟化下 PrintWindow 把
   app 自渲染尺寸的内容放进 DPI-aware 请求的大位图，右/下多出黑边
   ——内容完整即有效，裁剪按内容实际边界来。
3. **构建前先 grep 标记确认自己的编辑还在**：并发会话的 git 操作曾把
   本会话 ChartModel.zan 的补丁整段抹平（git diff 干净 = 被回签）。
   python 补丁锚点先查行尾：并发 checkout 后工作区是 CRLF，读入
   `.replace('\r\n','\n')` 归一、按 
 锚 patch、写回原风格。
4. **新增 conformance 文件必须 `cmake -B build`（只配置，安全）重新
   注册**：tests 的 `file(GLOB tests/conformance/*.zan)` 在 configure
   时求值，配完 chart 档 38→39。`test.ps1 -Match` 是**名字正则**
   （ctest -R），不是 label；档位用 -L。
5. **大块补丁别用 heredoc**：160 行 `python - << 'EOF'` 会被截断/吃
   分隔符；Write 到 `_scratch/xxx.py` 再执行，脚本内 `assert
   src.count(anchor)==1` 自校验。
6. **labelLayout 语义速记**（对抄 labelLayoutHelper.ts:324
   shiftLayoutOnXY）：按 rect 位排序后**只推不拉**（delta = pos −
   前项 end，负→推到前项 end）；hideOverlap 先到先得；x/y 容器系
   绝对像素、dx/dy 相对偏移、align 按块宽平移；moveOverlap 与
   hideOverlap 共用同一引擎只是维度不同。**散点 y 存 ×pointG 定点，
   标签文本别走 SeriesNumText**（data 表 frac 判定会把 57.7 印成
   "57,700"）——按 pointG 走 FracText/Commas。
7. **triggerOn:"none" 解析期门控 `showTooltip=false` 即忠实**：引擎无
   dispatchAction 通道，官方"只由 action 触发"在本引擎等价于不出
   提示框，渲染层零改动（--nomouse 截图模式本来也压掉 tooltip）。
