# Chart 引擎改造与渲染探针——逐坑手册

> SKILL.md 主干只留入口；图表渲染改造、探针方法与官方语义对齐的逐坑在此。

## Chart 引擎改造与渲染探针(硬规则)

- **ChartSeries/ChartOption 新增字段五处同改**:字段声明、Create/Of 缺省、
  深 Clone、`Clone(src, shareData: true)` 浅路径**早退之前**的标量抄写块、
  浅路径的集合引用共享(levels/tree 等)。渲染管线 MaterializeSeries 每帧
  都走 share 路径,漏抄一处 = 字段静默丢失:模型层解析断言全绿,渲染却
  全吃缺省(真实案例:treemap/sunburst 的 levels 全链解析正确、渲染整图
  缺省配色,根因是 levels 在浅路径早退处被丢,配置断言测不出这种丢法)。
- **渲染探针两条铁律**:要看首帧静态效果用 `ChartView.KeyedStatic`
  (staticFrame 把动画进度钉在终点;普通 `Keyed` 首帧动画进度≈0,
  不在引擎免动画名单里的图型整板近黑);GetPixel 采样必须在 `Render`
  之后、`PresentFrame` 之前——Present 后读到的已是清屏面,每张 demo
  扫出同一单色(真实案例:探针首版把两种错各踩了一遍才定位)。
- **用户实机截图 ≠ HEAD 行为,先核对构建新旧再立项**:引擎修复不断
  落地,用户跑的 gallery 二进制可能落后几天;把 stale binary 的症状
  当活缺陷修会白走一趟(真实案例:用户截图刻度 `#0/#2/#4` + smooth
  狂野过冲,HEAD 探针里 y 轴声明域/平滑包络全是对的——min/max 接线
  是当天的提交;正确动作 = 先在 HEAD 复现,复现不了就重建 gallery
  (`scripts\build_charts.ps1`)再要截图)。
- **自写曲线采样先验两件事:基和=1、t=0/1 精确落端点**。平滑折线的
  控制点是绝对贝塞尔控制点,必须配 Bernstein 基 `(1-t)³/3(1-t)²t/
  3(1-t)t²/t³`;把 Hermite 基的千分幂直接配 1e6 常数项(单位错乱)
  得 b0≈0.9995 恒成立——每段几十个采样全部坍缩在段起点,平滑曲线
  退化成首尾直联折线、末段"整根消失"(真实案例:line-smooth 七点
  图 Sun 悬空,根因是基函数与配对逻辑缺失)。
  配对也别抄旧代码:`qx = b0·P0 + b1·C0 + b2·C1 + b3·P1`,旧 Hermite
  配对残留会让 t=1 落在控制点上而不是端点。
- **bbox-IoU 比较器对结构性缺陷全盲**:缺末段、多余竖网格线、整带
  错位 19px,IoU 照样 ≥0.5 PASS——"比较器全绿"不等于"图对"。数值
  oracle(SSR 出 SVG 提取包围盒/解剖)+ 逐图目检缺一不可,用户报
  "基本没对得上"时先目检再信指标(真实案例:折线族 36 图 27 PASS
  的同一天,用户点名的前两张图都是结构性错的)。
- **官方缺省值一律 SSR 实测,不凭文档或旧版记忆**:splitLine 缺省按
  **维度**(xAxis show:false、yAxis show:true,与轴类型无关),ECharts2
  "类目轴竖线默认开"的旧注释会误导出多余竖线;axisPointer.show 缺省
  是字符串 **'auto'**(声明了 value 就显示),按 bool false 处理会整根
  漏画静态指示线。SSR 一行 `getComponent(axis).get(key)` 拿到的就是
  合并后真值。
- **主题/CSS 调色板只填未声明的槽**:系列自带 color/itemStyle.color、
  根级 option.color(整表替换主题调色板)都必须跳过 chart::series-N
  的 CSS 覆写——常驻基线主题包一上,不带守卫的覆写通道会把
  multiple-x-axis 这类自带调色板的图逐槽盖回默认色,而且渲染不报
  错、只有颜色对不上(真实案例:echarts6.css 设为 App 缺省包当天)。
- **值对系列([[ts,v],...])落在 points 而不是 data,分发判定要照顾**:
  "点系列→lines"的判定若排在堆叠判定之前,stack+time 图永远走不到
  堆叠面积渲染器,两条同名 stack 系列各自按原始值画(真实案例:
  line-tooltip-touch 粉带不堆叠,DispatchKind 里换两行顺序即修)。
- **Keyed 复用保留式 ChartView 实例,跨帧悬停状态归属实例字段**:宿主
  通过 `ChartView.CachedOwner(key, ...)` 或持久持有的 `ChartView` 实例跨帧
  渲染,`lastHoverHit`/`lastHoverOver` 等悬停状态直接保存在 `ChartView`
  实例上(并在 `Dispose()` 中清理),不再使用全局 `HoverSlot*` 静态槽表。
  注意:若每帧直接 `new ChartView(o)` 而不保留/缓存实例,fadeFocus 渐隐
  读"上一帧命中"仍会丢失,必须复用保留实例或通过 `CachedOwner` 按 key 持有。
- **GUI 重绘是按需的:hitTester hover 变化才调度,图表内部悬停要自己
  补帧**:App 的 mousemove 只在 hitTester hover id 变化时置
  needsRedraw,而系列命中(currentHit)不在 hitTester 里——光标停在
  数据点上后没有任何下一帧,晚一帧的渐隐/item 卡永远不渲染。实测:
  同一时刻导航行 CSS :hover 会亮(hitTester 链活),图表拾取却"死";
  修法是 DispatchPointerEvents 在 hitChanged/entered/left 时补
  `needsRedraw = true`。
- **悬停命中要对齐官方 linePrecision:类目折线拾取含线段距离,不只
  数据点半径**:官方 trigger:'item' 沿整条折线可悬停;只拿"最近数据
  点 < 抓取半径"判定时,线段中段悬停毫无反应(真实案例:bump 图圆点
  悬停修完当天,用户指出"鼠标经过线就有效果,不只是圆点"——光标离
  最近点 300+px 仍应命中)。实现 = 指针到相邻两点连线(数据点弦)的
  SegDist2 也进最近候选,命中归属较近端点;启动参数路径(SelectDemo
  带 demo 参数)会早于容器惰性初始化,重掷/替换缓存前先判空建表。
- **数据值直接进像素公式的通道必须过显式域映射或硬帽,大值域会画出
  天文数字图元刷满画布**:geo 投影散点旧启发式 `r=Scale(3)+z*Scale(14)/100`
  拿数据值当尺寸——scatter-world-population 人口 1.35e9 算出半径
  1.9e8px,整张画布被一个圆刷成系列调色板色(geo-choropleth-scatter
  同炸)。官方语义是 visualMap inRange.symbolSize [lo,hi] 按声明的
  min/max(未声明则数据域)线性映射直径;凡 z 驱动半径一律再过
  Scale(60) 硬帽兜底(真实案例:全量 sweep 的 BLANK 分诊里,两张
  "满屏纯色"图都是它)。
- **全量 demo 审查靠 sweep 机器落 id,不靠人眼扫**:gallery 加 `--sweep`
  启动参数,逐 demo FromJson+KeyedStatic 渲一帧 Canvas.WritePixels 落
  ZPX1 + manifest(时延/异常按例捕获不中断),python 解码 png + 启发
  FLAG(非背景占比/调色板命中/内容 bbox/边缘接触/轴线暗游程)。用户
  "有的缺轴/有的空白/有的刷色"式模糊指控,一晚上落成 demo id 清单。
  三类启发误报要先看图再信 FLAG:treemap/sunburst 满幅 EDGE-CLIP
  正当、polar 类本无 X/Y 轴、ECharts6 浅色轴线过不了暗游程阈值。
- **SVG path 命令字母的大小写就是绝对/相对语义,解析不得归一化**:
  小写 l/c/s/q/a 是相对坐标,解析时把命令字母统一转大写
  (`cmd = toupper(ch)`),相对命令全部被当绝对执行,一条路径的后续
  点漂移成横跨全图的弦多边形,渲染与命中双双炸穿不报错(真实案例:
  geo-svg 冰岛底图悬停海面蒙上巨大半透明三角,插桩打 ring bbox
  前后对比一锤定音:漂移 bbox 跨全图 → 正确 bbox 60px)。命令只存
  原字母,绝对/相对在消费处按 `rel = ch >= 97` 推导。
- **SVG 底图的交互区域只来自命名形状,fill="none" 线稿不可区域命中**:
  官方语义是 SVG 原样渲染(光栅/矢量皆可)、区域只叠加交互,区域名
  优先级 形状 name > data-name > 祖先 `<g name>` > id——组名要向
  子形状传播(flight-seats 座位名全在 `<g name>` 上,漏了 regions
  收 0 个、整图连底图都不画)。冰岛 3061 个形状仅 2 个命名
  (trip1/trip2 且是 stroke-only 航线):未命名形状(海面/装饰)跳过
  不命中,`fill:none/transparent`、`fill-opacity<=0`、`style fill:`
  的线稿只画不做实心区域命中——否则悬停海面误触航线 ring 的
  emphasis 填充,整图蒙色(真实案例:"鼠标经过对 SVG 的干扰")。
  零命名区域的纯装饰素材也要照画光栅底图,早退分支先判 svgMode。
- **同一份数据不许两条解析车道齐收**:geo 系列的 [[lng,lat,z]] 数值对
  被通用数值对车道(×1000 定点)与 geo 专用车道(GeoQ ×100、y 取反
  纬度向上)各收一遍,同一系列两套点一半屏外一半屏内,悬停命中框与
  可见点错位(真实案例:geo-svg effectScatter 六个点消失/错位,根因
  是解析分支没先判 coordinateSystem)。通用数值对车道必须先排除
  geo;同理 SVG 底图(用户单位 ×1)与矢量地图(度 ×100)两套坐标
  尺度的投影必须分开,封装成带单位旗的单一投影函数,禁止在调用处
  散落换算。
- **同一配置键两条解析道先后各收一遍时,后跑的"整表重建"会覆盖先
  跑的正确结果**:graph 系列 links 先被通用道收好(数字端点=节点
  序号),专用道随后按「节点名表」重建整个列表,而新形态系列的节点
  没有名字、名表全退化成值串,数字端点全被改写成同一个名字——边
  全并到一对节点上,图"渲染成功"但连线全错(真实案例:matrix-graph
  七条链接探针实锤全变 '1'→'1')。给通用解析道补新形态时,必须
  查专用道是否会用自己那份重建覆盖;修法是让专用道在新型坐标系下
  跳过重建,保留通用道的原始端点,渲染端按「纯数字=序号,否则名字」
  解析。渲染还错时先探针打印解析产物再怀疑渲染端——产物在渲染
  前就被换掉是这类坑的共同形态。
- **复用整幅渲染器画"子面板"时,先过查三道隐形壳带与动画门**:
  把渲染器当成子矩形里的黑盒重入,是复合布局(grid 多面板/矩阵锚格/
  sparkline 宫格)的通用打法,但有四个坑都以「渲染了但看不见」出现
  (真实案例:matrix-mini-bar-geo 40 柱 + 10 地图全灭,连修四轮):
  ①主题 padding 在整幅上是留白,在 ~40px 迷你格里上下各吃一行后
  绘图区算成负值/几像素,整格内容静默早退或退化成细条——迷你面板
  按格高收缩 pad;②轴标签带/图例带等 gutter 不看 show 声明照占位
  ——gutter 跟随 axisLabel.show;③渲染器把「正式填色」整个包在
  `g >= 1000`(终帧)分支里,g<1000 只填底——复用时为防快照互踩传
  小 g,结果区域全无;无入场动画的子面板必须传终帧值;④快照槽按
  widget 全局编址,同帧多实例要么各给独立指纹(随矩形互异),要么
  干脆绕过快照。
- **给 spec 结构加字段,Clone/过河路径必须同提交逐条过查**:源
  option 探针全绿、渲染端字段全默认——resolved drawOption 的克隆
  函数没拷新字段(真实案例:ChartGeo.Clone 漏 matAnchor/mx0..my1,
  geo 锚格全灭,而按引用过河的 titles/grids 全正常)。验证「解析成
  功」必须走到渲染端可见为止;新字段过河要么进 Clone,要么像
  titles 一样改成按引用共享并写明「只读共享」契约。
- **像素验收必须用刚刷新的转换产物**:离屏 sweep 落 ZPX 后还有一
  步转换成 PNG,忘了跑就拿着旧图判断——「修好了但看着还空」白绕
  四轮构建。看完渲染先问自己:这张图是这一版二进制画的吗?
- **接参考引擎的系列先读它的 defaultOption,缺省 coordinateSystem
  和遗留键别名都是引擎缺口**:LinesSeries 的 coordinateSystem 缺省
  即 'geo'——迁徙/航线 demo 的 lines 系列大都不写它,只认显式声明
  的解析器让整条车道静默不画(真实案例:geo-lines 迁徙航线消失);
  补缺省要带条件(option 里真有 geo 组件),纯 lines 兜底车道不能
  陪葬。同理 ECharts2 遗留键:geo/map 的 itemStyle.color 与
  areaColor 同义,官方 Flights demo 写的就是 color:"#005",只认
  新键会把深色大陆渲染成默认浅蓝 ramp。对拍前先拿官方 option
  原文(含遗留键)过一遍解析。
- **同色折线大批量必须走批量入口,且批量入口要链接期验真**:逐条
  polyline 是每条一次覆盖缓冲清+合成+一次 FFI 跨界,3.2 万条航线
  秒级卡死;批量=扁平交错顶点数组+每路径顶点数,每帧每色一次,
  GL 后端整个批次共享一次清+采样+合成(真实案例:lines-airline
  World Flights 接上解析后秒卡,批量后 sweep 单帧 ~0.7s)。坑:
  批量 API 可能只有头文件槽位和托管侧 extern,原生实现从未接线
  ——extern 悬空到链接期才爆 undefined symbol,首次使用先 grep
  原生导出是否存在。官方 large 模式(lines 声明 large 且条数超
  largeThreshold)跳过事件层,逐线命中要做同等豁免,否则每帧白付
  数万次线段距离。option.backgroundColor 是整只图容器的底,地图
  车道不能再用主题白把绘图区盖回去。

- **官方图型的数据项可以是"值向量"，标量车道会静默取 0**:radar 每个
  data 项是一条独立多边形(对象 {value:[每轴值],name} 或裸数组)——
  拿 Double("value") 读数组得 0,整图塌成圆心一个点,而解析/渲染全绿
  不报错。接入新图型先看官方数据形态;多边形语义的值数组在解析期
  物化成"每项一个系列"(图例取数据项名,未命名继承系列名——"同名系列
  随主项一起开关"的图例语义正好成立),值 ×1000 定点、显式 max 同步
  放大(渲染式 val×g×r/(max×1000) 的 g≈1000 会约掉),tooltip 显示走
  FracText 除回,0.46 这类小数才不丢(真实案例:radar 主力 demo 双
  多边形消失+轴标签错拿图例文案+radar-aqi 星爆全炸,一批修)。
- **组件级 option(只有坐标系没有系列)官方画骨架不画 no data**:
  分发链"0 系列→空状态"之前先认组件 radar/polar——两渲染器的骨架
  路径 0 系列本就安全,白白被空状态短路(真实案例:doc-example/radar
  的纯雷达组件落 "(no data)",官方画五轴网格+指示器名)。
- **渲染形态不对,先 dump 物化 json 的对应键再定责**:官方例 TS 的
  组件数组会在 json 里完整保留(radar-multiple 三联雷达的 center/
  radius/indicator.text 一直在案),缺的可能是引擎解析入口(radar
  数组形态没解析,只认对象形态)——不 dump 就动手,会白查一轮数据侧。
  口诀:json 有=引擎缺,json 无=物化缺;ECharts5 radar[]与 ECharts2
  polar[] 字段同构,消费端齐全时补的只是解析(真实案例:三雷达全画
  一个圈,以为是双 polar 圆心语义没实现)。
- **逐系列物化块里的 option 级字段只准改一次**:物化循环逐系列进,
  里面的全局放大/归一会连乘(真实案例:多雷达 3 个系列把 radarMax
  ×1000 连乘三次成 1e8,单系列 demo 永远不暴露)——用已置位的旗
  (radarScaled)守卫"只做一次",旗天然标记"首轮已做"。
- **热循环先审被调函数的收敛成本,再怀疑容器**:力导向 30 轮 × nn²
  的内循环 8.3s/帧,把对象 List 访问换平铺表毫无变化——真凶是被调的
  整型开方:牛顿迭代初值取 r=v 本身,要 ~17 次带 long 除法的对折才到
  √v,每对 ~1μs。修法 = Math.Sqrt(硬件 sqrt)+平方域剪枝(d²≥k² 直接
  跳,与 d<k 分支语义等价)+原生 int[]/CSR 邻接——8111→78ms(真实案例:
  graph-webkit-dep nn=513 首帧卡 8s)。插桩标记必须放进嫌疑函数内部
  (入口/出参两侧),只框调用侧会把热点错钉到别的段,白改一轮。
- **重活下放后台的现成定式 = Thread.Start + app.Post(捕获闭包)**:
  worker 只做纯计算(不触 UI 状态),成品经 `app.Post( => {收货})`
  整只移交——App.Post 自带 window.Wake(唤醒阻塞中的 WaitEvent),
  DrainPosts 执行后自动置 needsRedraw,不用自己搭互斥队列;UI 侧用
    pending 槽画「加载中…」占位(范例 ImageHttp.zan)。--nomouse/
    回放/审查这类"单帧截图必须终态"的路径保持同步装载。
- **PropertyGrid 编辑器是共享行几何下的真实子控件**：Input、SelectBox、Switch
  与操作 Button 由行容器 Add/Remove，继承同一 App、裁剪与输入归属。布局期先
  `MeasureTree(app)`，再将编辑槽位的完整矩形交给 `Arrange(ex, ey, ew, eh)`；
  绘制沿现有树执行，不在 Paint 临时挂树。SelectBox 历史兼容签名为
  `void Render(App,int,int,int)`，转发共享 RenderAt，不负责安装输入所有权。
  编辑槽高须受行内可用高度约束，不能让默认自然高度撑破紧凑行或覆盖下一行。
- **长属性检查器的分组折叠走 retained children 与 routed Click**：
  `PropSpec.Section`（`kind == 5`）对应真实分组 Button，经标准 Click 调用
  `ToggleSection`；折叠状态同时更新可见行、`ContentHeight` 与滚动量程。
  移出可见集合的编辑器经树操作退休输入；需要拦截冒泡时在路由处理器调用
  `e.Handle()`，不用 `Ui.ClickedIn` 几何轮询，图标继续使用标准 IconView。
- **HTML+CSS+Zan 设计稿零胶水规范与纯矢量图标体系（从截图还原精美客户端铁律）**:
- **HTML + CSS + Zan 的装配边界**：窗口子类使用同名文档与 `Show()`；
  页面使用 `Gui.Html.Load(app, html, handlers, baseDir)` 返回树，再由宿主挂载。
  HTML 只在运行期解析；发布用 `--embed` 携带设计稿，业务代码管理状态与事件。
  文档文本、图标与容器的映射以当前解析器和控件属性协议为准。
  - **无边框窗口右上角系统控制按钮保护区（SetCaptionZone）**:声明 `data-win-chrome="false"` 沉浸式无边框窗口时，若标题栏高度生效，Win32 的 `WM_NCHITTEST` 会将整个窗口顶栏判定为 `HTCAPTION (2)` 导致拖拽吞掉右上角按钮点击；必须在 `OnLoad` 中调用 `form.GetApp().SetCaptionZone(width)`（如 `form.GetApp().Scale(160)`）为右上角功能区（菜单/最小化/最大化/关闭）保留非拖拽客户区。
  - **Tabler SVG 纯矢量图标铁律(杜绝伪 Unicode 字符)**:按钮与图标控件一律声明标准 SVG 矢量名(如 `icon="player-play"`、`icon="player-skip-back"`、`icon="player-skip-forward"`、`icon="heart"`、`icon="volume"`、`icon="reload"`、`icon="playlist"`、`icon="chevron-down"` 等),走内置 997+ Tabler SVG 矢量图标集经 nanosvg 矢量光栅化与 GPU 贴图缓存。严禁使用 `▶`、`⏮`、`❤` 等 Unicode 假字符冒充图标。
  - **Nanosvg 矢量图元约束与动态透明 SVG 规范**:
    1. 颜色 Hex 解析：nanosvg 只接受 3 位或 6 位 Hex（`#RGB` / `#RRGGBB`），不支持 8 位 Hex（`#AARRGGBB` 会解析失败并静默回落为灰色 `#808080`）；透明度必须使用标准 XML 属性：`opacity="0.5"`、`fill-opacity="0.3"` 或 `stroke-opacity="0.8"`。
    2. 几何图元限制：nanosvg 不解析 `<text>` 标签，动态矢量芯片、HUD 刻度等指示图形必须由 `<path>`、`<polygon>`、`<line>`、`<circle>`、`<rect>` 等几何图元绘制。
    3. 动态透明 SVG 承载：通过 `Image.Src.Set("data:image/svg+xml;utf8," + svg)` 动态帧刷新时，图片控件必须在 CSS 中显式声明 `background: transparent; border: none;`，消除基础皮肤默认占位灰底。
  - **高性能帧循环动画（FrameHook）**:复杂 2.5D/动态视觉（如雷达扫描、呼吸光晕、状态气泡）通过 `Form.FrameHook(OnFrameAnimation)` 注册帧回调，利用 `Stopwatch.GetMicroseconds() / 1000` 实施帧率节流（如 90ms / ~11 FPS）；每帧计算离散步进增量构造动态 SVG 并更新，调用 `form.GetApp().RequestRedraw()` 实现丝滑低占用原生动画。
  - **HTML 文本与布局容器语义映射**:原生 `div` 标签映射为纯无格式布局容器(`Panel`,不绘制自身文本)。段落与标题必须使用语义标签 `<p>` 或 `<h1>`~`<h6>`(映射为 `Typography` 排版控件),行内文案使用 `<label>` 或 `<span>`(映射为 `Label` 控件)。当 `div` 元素承载动态单行文本胶囊时，声明 `display: block; white-space: nowrap;` 触发 `ArrangeFlow` 块流完成排版绘制。容器排版纵向排列时必须显式声明 `display: flex; flex-direction: column;` 避免子元素横向挤占。
- **分段单选按钮（Segmented Buttons）文字安全内边距阈值**:
  判定是否由下拉框升格为单行分段按钮时，文字宽度测量 `tw` 必须预留至少 `app.Scale(16)`
  的安全 padding（左右各 8px），否则在高 DPI 或多字宽下极易贴边或溢出截断；放不下时应
  平滑自动回退为下拉单选框。
- **设计器画布双击与代码跳转防误触机制**:
  画布组件双击默认优先进入原地行内文案编辑（`StartInlineRename`，按钮/标签等全量包含），
  严禁将普通双击硬编码为跳转代码；只有在显式按下 `Ctrl+双击` 或属性面板点击“定位代码”
  时才触发 `RequestOpenCodeBehindHandler`，杜绝用户点选/拖拽组件时误跳后置代码打断思路。
- **组件大纲树与画布双向精准联动与深层路径自动展开**:
  画布点选任意组件时必须同步通知大纲树（`SyncOutlineSelection`）；若选中的组件深层嵌套
  在容器/折叠目录内，必须顺沿向上将其所有祖先节点全部自动展开（`expanded = true`），
  确保选中项在树视图中立即可见且处于焦点，消除深层组件在树中“失踪”的交互断层。
- **插桩轮次先核产物 mtime 再信输出**:一次编译失败(比如往表达式
  续行中间插了语句)会让 sweep 跑旧 exe,新加的打印零输出,诊断被旧
  二进制牵着走;跑前核对 build 产物 mtime 晚于最后一次源码编辑。
  python 批量插桩只准插在语句起始行前(上一非空行以 `;` `{` `}`
  结尾),`+ Call(...)` 续行中间插语句 = 编译失败。
- **option 里的颜色声明走全链才生效**:legend.textStyle.color、
  radar.axisName.color 这类字段要过五处:ChartOption 字段+Create/
  Clone+DrawOption 拷贝+渲染器传参/消费端。漏最后一级(渲染器读
  主题色没读声明)时解析全绿、渲染死灰(真实案例:radar-aqi 深底
  图例白字画成主题灰字,金轴名根本没接)。
- **组件级 dispatch 通道(整块坐标系/骨架类图表)必须随 DrawOption
  过河,新组件入库先核对 DrawOption 拷贝清单**:渲染分派读的是
  ResolvedChart.DrawOption 产物而非 source,它逐字段手工过河——
  漏拷组件 = DispatchKind 的组件级判定(o.calendars.Count>0 /
  o.matrix!=null 这类)在 drawOption 上永不命中,系列级残留(数据
  已解析进 heatX/points)会被别的车道捡走画出"错而不空"的图,比
  空板更难发现(真实案例:matrix 首版被直角热力图车道抢走填色、
  维度树头带全丢;calendar 漏拷 calendars 后整族条纹化)。定位法:
  对 PNG 量剖面反推几何——填色起点/带宽与盒布局预期对不上,一眼
  锁定"画图的是另一条车道"。修法 = DrawOption 里补
  `o.组件 = s.组件`(解析后不可变的规格只读共享,zooms 同款;
  可变组件走 Clone,geos 同款)。
- **相邻填充区域的接缝要按官方"区域描边"语义封,不能用同色双描
  workaround**:官方每个 region path 是 fill+stroke 同帧落盘
  (MapSeries/GeoModel defaultOption itemStyle borderWidth 0.5 +
  borderColor neutral30 #b7b9be,SSR oracle 实证),相邻区域共享
  边各描一次天然密封;只画填充时共享边单像素 AA 合成后透背景成
  "虫洞",画一遍自己填充色压不住。描边车道独立于
  ChartItemStateStyle(显式 0=关边框是合法值,通用结构缺省 0 分
  不清"没声明",ChartSeries 用 -1 哨兵字段 mapBorderW/mapBorderC;
  borderWidth 0.5 这类小数用 JsonValue.Double 取回再取整,Has/
  Int 哨兵会混淆);渲染时描边必须压在所有填充之后画——zrender
  同 path 先 fill 后 stroke,提前画会被后画的相邻填充盖掉(真实
  案例:map-usa 相邻州界放大透背景缝,首版描边画在填充循环前
  全部消失)。闭多边形轮廓助手补宽度参数 PolyOutlineW(DrawPolyline
  对 <1 宽钳到 1px,官方 0.5px 缺省落 1px 网格)。geoIndex 挂载
  系列的边框取宿主 geo 的 itemStyle(getHostGeoModel 语义)。
- **接缝修了描边还在,真根因多半在数据:逐要素独立简化的多边形
  集合,共享边必然分叉成楔**:多边形数据若逐区域独立做抽稀/量化
  (如 Douglas-Peucker + 坐标取整),相邻区域对同一段共享边界各自
  走样,锚点之间路径分叉,放大后成楔形透底——描边 1px 盖不住
  几像素宽的透镜(真实案例:china 数据逐省独立抽稀,湖北/重庆一段
  中点偏差 15 单位,scatter-map 深底下放大透底;修了官方描边后
  用户实机仍报)。先对拍相邻环:精确共享顶点/边一档、近邻顶点
  (量化噪声)一档;修法在数据侧——分叉弦换成邻省同锚点的细分链
  (选链以弦偏差为准,链提取沿环 walk 且支持 wrap,否则会拿到绕
  整环的大弯链),量化漂移的近邻锚点用并查集聚类后取中位数对齐。
  验证用像素十字邻居探测(内部透底=真缝)与外侧轮廓/文字 AA 分辨
  开,余量要与基线持平才算非回归。
- **即时模式渲染器"每帧全量重画"是大网格的隐形杀手,离屏单帧
  测不出无响应**:实时路径每帧都走完整渲染器,20301 格热力图首版
  逐帧重画格子 + 4 万列表分配 + 数百次文本测宽 + 双份全量扫描,
  实机秒级无响应;而离屏审查单帧(36ms)全绿看不出问题。帧内快照
  缓存是现成定式:静态内容按指纹 SnapshotRect/RestoreRect,指纹
  必须覆盖**全部取像素输入**(几何/缩放/逐格数据/系列开关位/色带
  与值域声明/主题色,漏一路就是主题切换后出僵尸像素);命中帧只付
  一趟 O(n) 指纹 fold。悬停覆盖层与核心层**共用的几何推导**必须
  缓存或早退——指针不在本图面板直接 return,几何用"option 引用+
  矩形+缩放"为键的备忘复用,否则缓存了像素缓存不了卡顿(每帧照样
  重扫/重测一遍)。验收用帧耗时插桩量稳态帧:稳态帧低于"重画一遍
  的单项成本"即命中实锤(真实案例:20301 格热力图用户实机"一次性
  同步加载无响应",离屏审查全绿,稳态帧 36ms→7-15ms 定谳)。
- **同一数据形态在不同坐标系下语义不同,分流护栏是坐标系统计
  落字**:热力图的 [x,y,v] 裸三元组在直角系是(类目下标,类目下标,
  值),在地图系是(经度,纬度,值),矩阵系首两位还是类目名字符串
  ——新车道按"类型+数据形态"拦截之前,先把 option 里声明的
  coordinateSystem 落进系列的坐标系统计字段再判(没落字的坐标系
  值是空串,会被直角车道误吞,把经纬度当类目下标)。附带语义:
  类目 y 轴声明序自上而下(行 0 在顶),类目 x 轴左→右;分片
  (piecewise)上色 = 连续色带的桶中点量化。
