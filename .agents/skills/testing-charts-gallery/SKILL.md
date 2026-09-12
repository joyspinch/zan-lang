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

## 轴声明 min/max 与值对系列定点贯通（B17 会话教训）

1. **单位契约先于像素**：值对系列（[[x,y],...]）解析后 points 存
   值×pointG（恒 1000）、data[] 存 值×g、YOfF 吃 值×1000、散点映射走
   ValueXF/ValueYF。量程与映射**必须同一单位**，错一处就是 1000 倍
   （line-in-cartesian y 轴 0..100,000）或全线塌 0（area-time-axis 把
   除式写成 `g>1?g:1000`，pointG=1 时 255÷1000≈0）。定点换算恒为
   `raw×1000/pointG`，没有特例。
2. **JsonValue 取键值用 `Get(key)`+`AsDouble(dflt)` 或
   `Double(key,dflt)`**：`v.AsDouble(dflt)` 是值节点方法，作用在
   OBJECT 上恒返回 dflt——axis min/max 声明因此静默全废（34 demo
   249 条），探针（打印 minD/maxD/min/max）一发就现形。
3. **extent 环与映射环都要过 PointV**：AxisMinFor/MaxFor 第一环
   `Value()` 会把 points 系列的定点原值混进量程（第二环除回了也
   白除，raw 赢 max）——points-only 系列第一环必须跳过
   （`data.Count>0` 守卫）。
4. **frac 判定也要认 points**：SeriesFrac 只查 data[]，points 小数
   系列全漏 → ×1000 定点轴管线（含声明的 −0.4..1.4 双固定精确
   等分）永不接管，PointV 取整把曲线压扁。判定式
   `pointG>1 && y%pointG!=0`。
5. **映射别先取整**：`XOfValue(PointV(x,g))` 把 1/30 步长采样坍缩成
   左右两根竖线；ValueXF/YOfFL(raw, g) 分母按 span×g 缩放，亚单位
   精度直通（散点早就是这个式子，折线 geoMode-1 补齐即可）。
6. **快照构建后打补丁 = 用旧代码验证**：_scratch/stdlib_snap 是
   构建时拷贝，改完工作区必须重 cp 三个 Chart 文件再编——本会话
   首轮 XEXT 探针打印的还是修复前数值，白跑一轮。探针输出与预期
   不符时，第一反应查"exe 是不是旧的"。
7. **`pwsh -File` 不拆逗号数组**：`-Ids a,b` 整串成一个 id，匹配
   不到就静默拍默认 demo（面包屑还显示旧图）；一次一个 id 或用
   数组语法 `-Ids a -Ids b`。recheck2 的 OutDir 不存在时**不落盘
   还打印 OK**——先 mkdir。
8. **新官方对照的回归疑点先 HEAD A/B 再动手**：本会话 area-time-axis
   平线一度疑似"既有缺口"，HEAD 变体构建（三文件 swap：
   git show HEAD:... > 工作区，cp 进 snap，编 charts_pc_head.exe，
   恢复）证明是本次回归，修除式后像素同 HEAD。swap 期间别让别的
   会话碰 Chart 文件（并发树纪律：用完立刻恢复）。

## 散点族小数定义域与数值核对（B18 会话教训）

1. **轴扫描器分 X/Y，别拿 Y 扫描器算 X 域**：`FracAxisLoF/HiF` 内部
   走 `AxisMinForF/AxisMaxForF`——那是 **Y 列**扫描器（读 points.y，
   还夹带堆叠柱逻辑），拿它算 X 域会得到"按 Y 值算出来的 X 域"。
   本会话首轮 `ScatterRangeX` 这么写，x 域直接垃圾（scatter-simple
   出 0..200 定点）。正确做法：X 域取该轴自己的精确极值。
2. **取整极值不够用，seam 要同时出"精确定点极值"**：`ScatterSpan`
   原先经 `PointV` 除回取整（0.03→0、6.95→7），frac 域管线需要
   `raw×1000/pointG` 的真值。给 seam 加 4 个 out（fMnX/fMxX/fMnY/
   fMxY）而**保留**原 4 个整数 out 供整数路——两条路各吃各的单位，
   别指望一个 out 兼顾。
3. **frac 判定按轴分开查**：`ScatterAxisFrac(series, xAxis)`——散点
   x 列也要判（此前只判 y）。判定条件 `pointG>1 && data.Count==0`
   ＋逐点 `%pointG!=0`：CatIdx1000 的类目下标恰是整倍数不误报，
   时间对 pointG=1 不进。
4. **NiceRange 是自动端的包络，不是声明端的取整器**：单侧声明
   （`yAxis.min=-40`）经 NiceRange 会被 nice 成 -200，官方与整数路
   （AxisLo/AxisHi 直接采用固定值）都是 -40。规则：NiceRange 之后
   把声明端**钉回**精确值（`PinDeclaredF`）。本会话靠 HEAD A/B 抓到
   （polynomial-regression y 域 −40 → −200）。
5. **A/B 探针别调新 API**：要给 HEAD 快照编同一份探针，探针只能调
   两版都有的函数（ScatterRangeX/Y），否则 HEAD 侧直接编不过，
   整个 A/B 白做。新 API 的单测留给 conformance。
6. **conformance golden 是 CRLF，但别重复转换**：Zan 的
   `Console.WriteLine` 输出**已经是 CRLF**，再 `.replace(b'\n',b'\r\n')`
   会变 `\r\r\n`（`cat -A` 见 `^M^M$`，与已提交 golden 不符）。
   正确姿势：先归一到 LF 再统一转 CRLF。
7. **窗口截图不一定能看，数值核对才是主证**：本会话 Read 图片被
   过滤（模型不支持图像），"实机看着对"这条路直接断了——但用户
   方法本来就是"代码层对比"。可靠替代：官方 SSR oracle 取 extent
   （`_scratch/b18_sweep_oracle.js`）＋引擎侧无头探针（
   `_scratch/b18_engine_extent.zan`）逐行 diff；HEAD A/B 给每一行
   归因。**没有数值证据就不要在汇报里写"实机已过"**。
8. **`DispatchKind` 看的是首个可见系列的 type**：geo 系 demo 若
   series[0] 是 `coordinateSystem:'geo'` 的 scatter，kind 判成
   scatter → `DrawMap`/`DrawGeoOverlay` 不跑，底图与 geo 投影点
   整体丢失（`geo-choropleth-scatter`/`geo-map-scatter` 即此，
   前置缺陷）。判分派前先用无头探针打印
   `DispatchKind/LeadSeries.type/isGeoBase/regions`，别凭 demo 名字
   猜渲染器（`_scratch/geo_probe.zan`）。

## 无头像素用例的三条硬规矩（A5 会话教训）

1. **离屏探针必须 `KeyedStatic`，不能用 `Of`**：`ChartView.Of()` 走增长
   入场动画，`Render` 里 `g = app.AnimIntroIn(...)` 首帧可能为 0，柱条
   一根都长不出来。本会话 A5 首轮用 `Of()`，量到"未声明 min 时柱高 0"
   就当成缺口证据——其实两边都是 0，**断言量的是空图**。改用
   `ChartView.KeyedStatic(o, key)`（`staticFrame = true` 跳过动画）后
   数字才有意义。凡是"渲染后数像素"的用例，先问一句：量的是最终形态吗？
2. **诊断只在失败时打印，且像素计数不进 golden**：golden 与 stdout
   逐字节比对，像素数随 DPI/字体缩放浮动，钉进 golden 会让用例在别的
   机器上假红。写法：`if (fails > 0) { Console.WriteLine(...) }`，
   golden 只留一行 `xxx:1`。首轮把 6 行计数都 `WriteLine` 出去，
   ctest 直接 FAIL（输出与 golden 不符）。
3. **量"最小尺寸"要挑真的低于阈值的输入**：A5 的水平柱首版用
   `xAxis.max:1000` + 值 1，量出柱宽 71px——已远超 40 的 min，钳制
   是无操作，断言 `nHMin > nHNo` 永远红。改用"一个大值撑开量程 +
   一个极小值"（`data:[1,100000]`）才造出亚像素柱宽。另一个坑：
   **HBarCore 不读声明的 axis.min/max**（lo/hi 只由数据算），
   想靠 `xAxis.max` 压窄量程是压不动的（见台账 B17 残留）。

## 堆叠柱负值段整体不绘制（A5 会话发现并修复）

`ChartViewBar.zan` 堆叠分支算段矩形用的是
`yTop = YOfFL((segBase+vF)*g/1000)`、`yBot = YOfFL(segBase*g/1000)`、
`segH = yBot - yTop`，然后 `BarCellR4(c, bx, drawTop, barW, segH, ...)`。
负值段（`segBase=0`、`vF<0`）在"Y 向下增大"的映射下 `yTop > yBot`，
于是 **segH 为负**，而 `BarCell` 开头就是 `if (h <= 0 || w <= 0) { return; }`
——负值段整根不画。无头探针（`_scratch/stackneg4.zan`，同图只换数据）：

| 形态 | 墨量（绘图区内非白像素） |
|------|--------------------------|
| 正值不堆叠 | 49884 |
| 负值不堆叠 | 49236 |
| 正值堆叠 | 34397 |
| 负值堆叠 | **2262** |

不堆叠路（`y0 = py<zeroY?py:zeroY; bh = |py-zeroY|`）已归一化所以正常；
堆叠路的 `segY0/segY1` 只用在命中与描边，**没用来修正绘制矩形**。
修法：绘制矩形改用同分支描边早就在用的归一化值
（`drawTop = segY0; drawH = segY1 - segY0;`），`segNeg` 的累加逻辑
不动。正值段 `segY0 == yTop`、`segY1-segY0 == segH`，逐像素不变。
修复后 neg-stack 墨量 2262 → 33882（与 pos-stack 34397 同量级），
已并入 `tests/gui/chart_barminheight_test.zan` 第 5 组断言；
只回退这一处、保留 barMinHeight 的 A/B 显示该断言单独变红。
影响 `bar-negative`（stack:"Total" 负段）等 demo。

## 极坐标柱：方向、跨度、域三者是独立的（A6/A7 会话，整族曾反向）

移植 `layout/barPolar.ts` 时最贵的三个坑，每个都让**整族** 8 个 demo 错，
且都能在"看起来对"的局部探针里蒙混过关：

1. **角度方向：`angleAxis` 缺省顺时针**。ECharts `clockwise: true`（缺省）
   ⇒ `axis.inverse = inverse !== clockwise` ⇒ 缺省 `endAngle = startAngle − 360`。
   值增大 ⇒ 屏幕角**减小**。Zan 的 `PolarAngleAt(startA, spanX10, frac)`
   里 `spanX10` **带符号**正是为此，别把它取绝对值。
2. **正负号取自原始数据，不取自坐标方向**。源是
   `const sign = value >= 0 ? 'p' : 'n'`（`barPolar.ts:126`）。在顺时针
   角度轴上正值使坐标**减小**，所以按 `vc < valStart` 判号会把**每条正柱
   都塞进负累加器**——堆叠全塌。判号一律回原始数据
   （`double rawV = bars[si].Number(i); bool negSign = rawV < 0.0;`）。
3. **轴跨度 ≠ 值轴数据域，两个量勿混**。轴几何跨度恒 ±360°（类目轴
   `offBand` 再缩一个槽位，`PolarCatSpan`）；而值轴的**数据域**来自数据
   （bar-polar-stack 的角度域是堆叠和 `0..16`，**不是** 360）。
   派生结论：切向柱（值轴 = 角度轴）里**不做** offBand 类目收缩——那根
   轴自己不是类目轴（基轴才是），拿 `baseCount` 去收缩会把 360° 缩成
   330°。只有径向柱（基轴 = 角度轴）才 `PolarCatSpan`。

配套两条小的：

- **`roundCap` 缺省是 `false`**（`BarSeries.ts:149`）。源里
  `get('roundCap', true)` 的第二个参数只是"取不到时的兜底"，被
  `defaultOption` 覆盖——照抄字面量会得到**相反**缺省。它同时是
  `clampLayout` 的开关（`baseAxis.dim !== 'radius' || !roundCap`）：
  径向柱恒钳，切向圆头柱不钳（端帽要伸出外环，`polar-roundCap` 的
  With Round Cap 正是 720° 满扫）。
- **`barMinHeight`/`barMinAngle` 按系列取**（`seriesModel.get(...)`），
  不是按轴——一个轴上两个系列可以各有各的最小尺寸。`calcRadialBar` 的
  极坐标缺省是**硬编码** `categoryGap '20%'`/`barGap '30%'`（不读主题），
  且取**最后一个**声明 barGap/barCategoryGap 的系列。

**验证手法**：极坐标柱是纯几何，走数值 oracle——ECharts 6.1 SSR 跑真实
demo，把每根柱的 `{r0, r1, a0, a1}` 与 `PolarBarSolve` 的输出逐项比。
`ChartView.PolarBarSolve(bars, siOf, f, out valueIsRadius)` 是渲染与用例
**共用**的纯解算入口（渲染器 `DrawPolarBarSeries` 也调它），这样用例测的
就是真路径，不会出现"探针自己对、渲染器另一套"。

**多 polar 必须逐 polar 过滤**：`polar-endAngle` 有两个 polar，把两者塞进
同一个 frame 会得出"不吻合"的假结论。`PolarBarsValueExtentAt(o, isAngle,
pi, ...)` 按 `s.polarIndex != pi` 过滤系列，轴用 `PolarAxisFor(list, pi)`
按 `polarIndex` 绑（带槽位回退）。先怀疑探针的过滤，再怀疑库。

### conformance 用例不许读仓库相对路径（踩过一次假红）

`tests/run_case.cmake` 从 **build 目录**跑编译产物，用例里
`File.ReadAllText("examples/gui_charts/options/xxx.json")` 在 ctest 下直接
`FileNotFoundException`（手动从仓库根跑却是绿的——最容易骗过自己）。
option JSON 一律**内联**进用例源码。

## 极坐标柱顺带修出的编译器缺陷：`out`/`ref` 目标是"位置"时取到了值（A6/A7 会话）

写渲染器时用 `PolarBarSolve(..., out bool valueIsRadius)` 崩了，根因不在
图库：`emit_ref_arg`（`src/compiler/irgen_expr.c`）只在目标是
**标识符局部变量**时返回 `alloca`，其余一律 `emit_expr` —— 于是
`out obj.field` / `out arr[i]` / `out lst[i]` 把**字段里存的值**当地址
传出去，被调方首次写入就段错误。修法（rule 10，改编译器不改调用方）：

- 新增 `emit_ref_lvalue_ptr(g, tgt, locals)` 解析"位置"：
  `ClassName.StaticField` 取 backing global（global 本身就是槽）；
  `obj.Field` 走 `get_field_index` + `emit_field_ptr`；
  `AST_INDEX` 走 `emit_struct_elem_ptr`。
- `emit_ref_arg` 的标识符分支补**裸名解析**（`out field` → `out this.field`），
  末尾接 place 解析，解析不出来就发显式诊断
  `ref/out argument must be a variable, a field, or an element`（旧行为是
  静默传一个错地址，只有运行期崩才暴露）。
- 用例 `tests/conformance/out_param_lvalue.zan`（8 项：实例字段 out/ref、
  静态字段、嵌套 `out b.inr.v`、数组元素、List 元素、裸名、普通局部、
  重复写）。

**排查纪律**：这个缺陷当时是**潜伏**的（仓库里只有新代码用这个形状），
所以"附近用例都绿"不能证明新代码没引缺陷。遇到"新写的形状一跑就崩"，
先用 `_scratch/` 最小复现区分"我的代码错"还是"编译器错"，再决定改哪边。

## 组件级坐标系的分派与"过河"（C5 会话，geo 系 4 处同源缺陷）

geo 系 demo 底图整体不画的根因是**四处**，只修一处不够，而且前两处
能在"直接调 API"的探针里假装正常——**必须走完整 Render 才算验过**：

1. `DispatchKind` 按首个可见系列分发。geo 底图是**面板骨架**（与
   calendar/polar 同级），挂在它上面的 scatter/lines/graph/custom/pie
   都只是投影装饰。真实示例的 series[0] 就是 `coordinateSystem:"geo"`
   的 scatter，于是 kind=scatter，`DrawMap`/`DrawGeoOverlay` 永不执行。
   判据必须是**组件级**：`if (o.geos.Count > 0) { return "map"; }`。
   加坐标系骨架时先想"这个坐标系是面板还是系列"，日历/极坐标已踩过。
2. `DrawMap` 取几何载体不能用 `LeadSeries`——它取到的是那个没有
   `regions` 的 scatter。加 `MapBaseSeries(o)`：优先可见 `isGeoBase`，
   其次首个可见且**真带 regions** 的 Map 系列。
3. **`ResolvedChart.DrawOption` 漏拷 `geos`**：渲染期分派读的是
   `drawOption` 而非 `source`，`geos.Count` 恒 0，第 1 条的组件级判定
   永不命中。这是最阴的一处——直接调 `ChartView.DispatchKind(o2)` 返回
   "map"，但 `v.Render(...)` 画出来还是空散点坐标系。**教训：任何
   "解析期新字段"都要同时问一句"DrawOption 拷了吗"**，同类的还有
   `calendars`/`angleAxes`/`radiusAxes`（后两者已拷，`calendars` 是
   另一条路径）。
4. 真实示例的 map 系列写 `{type:'map', geoIndex:0}` 而 **`map` 缺省或
   空串**：ECharts `MapSeries.getMapType()` 是
   `(getHostGeoModel() || this).option.map`——有宿主 geo 时 option.map
   被忽略。判定必须用 **`s.Has("geoIndex")` 显式声明**，不能看字段值：
   `geoIndex` 缺省 0 与显式 0 数值上无法区分，而 ECharts 的
   `getHostGeoModel()` 走 `getReferringComponents(..., {useDefault:false})`，
   没写 geoIndex 的 map 系列自建独占 geo、自己的 map 优先。

**A/B 的做法**：`MapBaseSeries` 的调用点与定义在同一处，单独回退
`ChartView.zan` 会**编译失败**（`has no member 'MapBaseSeries'`），
拿不到干净的 A/B。改做**外科 A/B**：只把 `if (o.geos.Count>0)` 那行
注释掉，其余三处保留 → 断言 `B: geo+map dispatches to map` 变红、
`B lead regions=0`、绘图区墨迹 3680（A 形态基准 68981）。恢复即过。
**回退整文件拿不到 A/B 时，回退那一行。**

**真图 A/B 别只看自己造的形态**：`_scratch/c5_realdemos.zan` 逐 demo
数像素指纹（ink + sum）对比 HEAD——`geo-choropleth-scatter` HEAD
kind=scatter ink=9978 → 修后 kind=map ink=268800，`geo-map-scatter`
HEAD kind=scatter ink=16988 → 修后 kind=map ink=100122，**其余 10 个
geo/map demo 指纹逐字节同 HEAD**。只跑自造的小地图会漏掉"底图名来自
注册表（china 走 `MapChinaData.Regions()` 而非 JSON 文件）"这类差别：
探针里没注册 `china` 时 `regions=0`，底图当然不画，读数是假的。

**本条暴露的下一层（已由 C6 修掉）**：`o.rampColors` 只从
`dataRange.color`（2.x 遗留）填，`visualMap.inRange.color` 只进
`visualMaps[]`，地图渲染器读 `o.rampColors`/`MapRamp` 的缺省浅蓝→深蓝。

## 全局色带只有一条：visualMap 与 dataRange 必须汇到同一处（C6 会话）

**现象**：29 个带 `visualMap.inRange.color` 的官方 demo（continuous 22 /
piecewise 7）分级着色全丢，地图走缺省浅蓝→深蓝、日历走系列色。

**根因**：`o.rampColors` + `o.rampLo/rampHi` 是**单条全局色带**，被
`ChartViewMap` / `ChartViewHeatmap` / `ChartViewScatter` / `ChartViewCalendar`
四个渲染器共读；但它此前只从 `dataRange.color`（ECharts 2.x 遗留）填，
`visualMap.inRange.color` 只进 `visualMaps[]`。**两条路从不交汇**——
写 visualMap 的现代 demo 一个都吃不到自己的色带。

修法：`FromJsonValue` 在 `ParseVisualMap` 之后接线，取**首个**
`type==0 && colorsExplicit && rangeColors.Count>=2` 的 visualMap 灌进
`o.rampColors`；`o.rampColors` 已被 dataRange 填时不覆盖（dataRange 更具体）。
语料普查先确认**没有任何 demo 声明 ≥2 条带色 visualMap**，"首个"才无歧义。

### 接线必须认"显式声明"，不能认"有没有值"

`ParseVisualMapOne` 在 continuous 且未声明颜色时会**填官方缺省彩虹带**
（11 色 `#313695..a50026`），所以 `rangeColors.Count>0` 根本区分不出
"用户声明了色带"和"我们替他填了缺省"。`scatter-nutrients` 的 visualMap
只有 `inRange.symbolSize`，若照 `Count>0` 接，它的分组色会被按值彩虹
覆盖（官方是 piecewise 分组色）。**加 `colorsExplicit` 字段，只在
`inRange.color` 或 2.x `color` 真正存在时置真。**

同型坑：C5 的 `geoIndex` 判定用 `s.Has("geoIndex")` 而非比较字段值——
缺省 0 与显式 0 数值不可区分，而语义不同（`useDefault:false`）。
**"显式声明过没有"是配置语义，字段值往往不是它的代理。**

### 定点域 vs 原值域：别把 ×1000 的边界喂给按原值比较的消费者

`ChartVisualMap.minV/maxV` 是 **×1000 定点**（给 `VisualColorAt` 的
`tF` 归一用），而 `o.rampLo/rampHi` 的四个消费者都拿**原始数据值**比。
`map-usa` 的 `max:38000000` ×1000 是 3.8e10，`(int)` 强转直接溢出成
INT_MIN——图上变成"值域下界巨大"，全部区域一个色。

修法：`ChartVisualMap` 另存 `minRaw/maxRaw`（`ChartOption.RawBound`：
`|d|>2e9` 记 `Auto()` 按未声明退回"从数据推导"，否则四舍五入成 int），
接线只读 `minRaw/maxRaw`。**写断言时先算清哪个域**：我一开始把
`max:38000000` 断言成 `Auto()`（以为触发了护栏），实际 38000000 在
int 内、rampHi 就该拿到 38000000；护栏只在 1.7e10 这种真超界时才生效。
**测试断言错了和实现错了长得一模一样——先算一遍再写。**

### 色名解析成 0 ≠ 无色，是"整片透明"（本条独立第二缺陷）

`StyleSheet.NamedColor` 只有 42 个常用名（CSS Color Module Level 4
共 148 个）。`map-HK` 色带里的 `lightskyblue` / `orangered` 落空解析成
**0**，而 0 在渲染期经 `Chart.WithAlpha(0, 255)` 等于"未着色"——
**区域整片透明不画**。

**这条是"拒绝接受异常读数"挖出来的**：接线后 `map-HK` 墨迹从 HEAD 的
80898 暴跌到 8805（9×）。色带接上了反而更空，说明不是接线错，是色带
本身有解析不出颜色的项。顺着查才挖出具名色表缺陷。修完 `map-HK`
ramp=3、墨迹 84915（回到 HEAD 量级），`lightskyblue`(8900346) 在
n=66567 个像素上落地。

**规矩：A/B 里出现方向相反的大幅变化，先当"新缺陷"查，别当"接线的
副作用"接受。** 顺带把表补全到 148 名全覆盖（139 显式 + 9 拼写别名
aqua/cyan、fuchsia/magenta、gray/grey、dark*/dimgray/dimgrey、
lightgray/lightgrey、slate*/darkslate*/lightslate* 的 gray/grey 对），
并**逐条比对 CSS 规范零错值**。

### 真 demo 像素普查的排除法

`_scratch/c6_demos.zan` 跑 20 个 demo 的 `ramp=<色带长度> ink=<非白像素> sum=<色值和>`
指纹。判断接线对不对靠**两类对照**：
- 该接的接上了：16 个 demo 的 ramp 从 0 变正（`map-usa`/`heatmap-large`/
  `matrix-covariance` 11、`scatter-map`/`heatmap-map`/`dataset-encode0`/
  `parallel-aqi` 3、`geo-map-scatter` 等 2）；
- **不该接的保持 0**：`scatter-world-population`/`scatter-nutrients`
  （symbolSize-only）与 `bar-simple`/`line-simple`/`pie-simple`/
  `heatmap-cartesian`（无 visualMap）6 个 ramp 恒 0。

只数"变了多少个"不够，**必须同时确认"不该变的没变"**，否则一次
过度接线（比如照 `Count>0` 接）会被"变了 22 个"当成成功。

## 轴刻度钳制/对齐/细分：三个键静默失效（A11/A12 会话）

**现象**：`yAxis.minInterval` / `maxInterval` / `alignTicks` / `minorTick` /
`minorSplitLine` 全部无效果。grep `stdlib/Gui/Component/Chart/` 发现
`minInterval` 只出现在两个**死函数**里（`AxisNiceRange` / `IntervalScaleNiceTicks`），
真实管线 `NiceRange`/`BuildAxesR` 从不读它——**配置字段存在、解析代码没有，
就是静默失效**。查缺口先 grep 字段名是否被 `ParseAxisOne` 读，别先看渲染。

### 钳制（minInterval/maxInterval）

- 只对 **interval/time** 刻度生效（`axisNiceTicks.ts:263-264`
  `isIntervalOrTime ? model.get(...) : null`），log/类目轴不适用。
- 顺序：`interval = nice(span/splitNumber)` → `if (minInterval != null && interval < minInterval) interval = minInterval` → max 对称。
- nice extent 落在**数据域内侧**（`ceil(e0/iv)·iv` / `floor(e1/iv)·iv`），
  但 `Interval.ts:238-282` 会给超出部分**补 tick**。**所以钳制后必须保证
  extent 不收缩**：`if (nlo > dataLo) nlo -= iv; if (nhi < dataHi) nhi += iv;`
  （我第一版漏了这步，`minInterval:2` 在数据 0..3 上给出 0..2 而非 0..4，
  直接把数据切了）。
- `minInterval` 是 `null` 有语义（未声明）而 **0 是合法值**，所以解析必须用
  `Double(key, AutoD())` 哨兵，不能用 `Has(key)` 判定（同 C6 的
  "接线必须认显式声明"）。小数轴（×1000 定点）的 minInterval 按数据单位
  ×1000 换算，**哨兵不能参与乘法**。

### alignTicks

- alignTo 由 `Grid.ts:738-756` **逆序扫描**挑"不要求对齐的最后一条"；全都要
  求则第一条被 pop 当 alignTo。本引擎只有左/右两槽，第 3 条以上 y 轴不参与
  （记债）。
- 两个必踩的坑：
  1. 必须用**原始 extent**（`axisAlignTicks.ts:176 targetExtent`），不是
     nice 后的——拿 nice 域去对齐会多补一格。
  2. `mayEnhanceZero = targetExtentInfo.incl0`，而 `incl0 = !option.scale`：
     **全正数据在 `scale:false`（缺省）时下界就是 0**。不做这步零锚定，
     `2000..23400` 会被对成 `-10000..30000`（官方 `0..40000`）——
     这是"官方为什么从不给正数据负刻度"的答案。
- `nice(x, NICE_MODE_MIN)` 的 `NICE_MODE_MIN` ⇒ `nf = 1`，**结果就是 10 的幂**，
  不是 1/2/5 那套。
- 段数（`nseg`）是可移植判据，端点不是（本引擎 nice 是 2.2.4 smartSteps 的
  1/2/2.5/5，ECharts 6 是 1/2/5）。

### minorTick / minorSplitLine

- `splitNumber` 缺省 5，**不在 `(0,100)` 内回落 5**（`Axis.ts:207` 的保护）。
- 逐对相邻刻度在**原始值空间**等分取内部点，且严格
  `mt > extent[0] && mt < extent[1]`（`minorTicks.ts:50`）——**首末段越界的
  点被丢弃**：−20..20 / 4 段 / splitNumber 5 得 **16** 点而不是 20。
- **类目轴返回空**（`Axis.ts:204 isOrdinalScale`）——别给类目轴画细分线。
- **log 轴也在原始值空间插值**（`Log.ts:146-154` 把 `intervalStub` 传进
  `getMinorTicks`）：line-log 的 g0 实测 `2.8/4.6/6.4/8.2`，**不是对数均分**
  （对数均分会得 1.58 之类）。所以 log 细分要用"逐对刻度"版而不是"等分段"版。
- 非整值细分点（2.8/4.6）在 log 轴上落像素需要 ×1000 定点对数映射
  （新增 `Log10Fx`/`YOfLogF`），整数版 `YOfLog` 会把它们全挤到同一像素。
- 颜色 `tokens.color.axisMinorSplitLine = neutral05 = '#f4f7fd'`——按项目
  规矩**进 CSS**（`stdlib/Gui/skins/base.css` 的 `chart::minor-grid`），
  不在代码里写字面色。
- **残留**：`minorTick.show`（短刻度 stub）未渲染，本引擎没有 cartesian
  axisTick 渲染通道，只做了 `minorSplitLine`（记债）。

### 钳制/对齐要覆盖到"轴有几种"，别只修看见的那一条

A11 第一版只改了 y 轴（左/右）的四处量程点，漏了 **x 数值轴**——同一个
`axisNiceTicks.ts:263-264` 的 `isIntervalOrTime` 判定**不分轴**，
`xAxis.minInterval` 一样该生效。修完 y 要回头数一遍这个键在源码里
覆盖哪些轴/路径，再逐条接（x 值轴已补，**time 轴记债**）。

**time 轴为什么不能照抄**：`scale/Time.ts:745-775 calcNiceForTimeScale`
是把钳制加在中间量 `approxInterval = span/splitNumber` 上，再去
`scaleIntervals` 表里取档；本引擎的 `Chart.TimeTicks(t0,t1,maxTicks)`
是按**刻度条数**自增步长（1→2→5→10 天），没有 `approxInterval` 这个
中间量。**在现结构上硬套 = 臆想**——先把 `TimeTicks` 改成"先解
`approxInterval` 再查档"，才谈得上接线。而且实测唯一用它的 demo
（wind-barb，`maxInterval` 1 天）本身就是 no-op（引擎步长已 2 天），
所以"没接"当前不产生可见错误，记债即可。
