# 渲染质量与动画——毛刺/动画缓存/DPI 逐坑

> 三部分：毛刺防治（斜线/曲线/圆角的抗锯齿）、动画与保留树缓存、缩放纪律（DPI）。
## 毛刺防治(斜线/曲线/圆角的抗锯齿)

**毛刺 = 数据边被量化到整像素。** 斜线/曲线在光栅化器眼里只有"每像素覆盖
多少";只要把 1/256 定点的小数坐标交给它,AA 就自动正确。标准库的折线原语
(`DrawLine`/`DrawPolyline`/`DrawPolylineFx`)与 Chart 系列填充
(`FillColumnAA`/`FillBandFx`/`FillSpanAlpha`/`FillPolyAlpha`)都已是亚像素
的;界面出现毛刺,几乎总是绕开了它们自己拼:

- **数据边定点进光栅化器**:图表的线/面积边界这类数据驱动边,计算时保留
  1/256 定点(`yF = base*256 - num*256/den`),填充用带小数覆盖率混合的
  列填充(`ChartView.FillColumnAA`,边界行按 frac 混合)。把边量化成整数 y
  再用 1px 竖条 `FillRect(x, y, 1, h, ...)` 逐列拼,缓坡上每列硬跳一整行
  = 阶梯锯齿,条顶与 AA 折线之间还会露月牙缝(真实案例:DataTable spark
  面积、ChartBig 面积,修复均收口到 FillColumnAA)。
- **折线一笔连成**:整条线一次 `DrawPolyline`/`DrawPolylineFx`;逐段
  `DrawLine` 在拐角处两端各落一次实心像素,双重混合亮一像素、还可能留缝。
- **多边形填充用亚像素扫描线**:半透明多边形(雷达/弦图)用
  `ChartView.FillPolyAlpha`(过圆心自交的非零环绕用 `FillPolyWinding`),
  行中心采样、1/256 定点求交、段首末列小数覆盖混合;不要自己写整数交点
  配整行 FillRect,斜边会成整列硬跳的阶梯。
- **叠画月牙**:两段重合圆弧叠画,下层形状的 AA 边缘会在上层弧外露出一条
  浅色月牙(真实案例:异形窗口关闭钮悬停红 × 标题栏圆角)。修法是上层沿
  下层轮廓内收 1–2 逻辑像素(半径 -2),把下层 AA 带盖进去;不要试图用
  更多叠画去补月牙。
- **一次成形**:圆角控件禁止"方角画完再叠圆角盖"——克隆样式改
  Radius/Corners,一次画对;覆盖裁剪的原语自己拥有边缘,叠补必留边。
- **AA 渐变带固定 1 物理像素**:抗锯齿过渡带宽度是原语内部固定的,别随
  线宽/DPI 自行加宽;要更柔的效果用半透明描边,不动 AA。
- **像素夹具先验证不透明标记**：Canvas.FillRect 接收 `0xAARRGGBB`；例如
  `unchecked((int)0xFFEA00D4)` 才是不透明色，省略 FF 会得到 alpha=0 的透明标记。
  DataTable clip 探针曾因此假报“没画/越界”；先确认基准标记可见，再断言 clip，
  在 Present 前采样，不能把透明夹具问题当成生产渲染失败。
- **半透明量程 0–255**:`Chart.WithAlpha` 直写 alpha 字节,传 256 会整型
  溢出成全透明——画了但看不见,极难排查。
- **验证仪式**:离屏 `new Canvas(w, h)` + `GetPixel` 统计"纯色直贴纯背景"
  的硬相接对数做阈值断言(仓库范式 `tests/gui/chart_fillaa_test.zan`、
  `chart_stackedarea_aa_test.zan`:同一图形整数实现 56/152 处硬相接,
  亚像素实现 0/4);肉眼收尾用 PrintWindow 截图放大 6× 看角与斜边。

## 动画与保留树缓存

- 测量与绘制必须以同一当前控件和祖先链解析 CSS。无关系选择器时不拼祖先
  签名；有关系选择器时两个阶段都建立 styleCtx 并在 finally 恢复。为什么：
  上下文键不一致曾让 201 个静态节点在 120 帧中重复解析 48,240 次，后代
  选择器尺寸在测量时也不正确。缓存仍须按 DPI、主题和结构变化失效，不能
  共享可变 StyleBox；验证结构重挂、异常恢复和 Clone 独立性。
- 连续视觉动画使用自适应 RequestAnimationFrame/In 重载，显式毫秒间隔用于
  定时内容。相位必须按真实时间连续计算，并用实际像素变化断言验证；只把
  请求改快，90ms 整步的加载圆点仍会一格格跳。性能统计分开冷启动与预热后
  的 render/present，并按目标刷新率预算检查尾延迟。
- 只绘制、不占流式尺寸的叠层仍须保存真实绘制几何，动画损伤覆盖旧/新像素。
  为什么：零测量高度的精灵叠层在姿态命令哈希变化时报告高度 0 的损伤，
  把局部动画交替升级成整窗帧。修复实际 bx/by/bw/bh，并用真实 partial 比例
  及整帧像素对照验证；不能关闭 paint hash 来掩盖损伤错误。
- **嵌套绘制 hash 按 owner 分段，恢复放 finally**：用值 token 保存父累加器与 armed 状态，成功后才提交本段 fingerprint；non-tree 子绘制显式建立稳定 owner scope，祖先自身变化仍须检测。
  原因：单个保存槽会让嵌套子段覆盖父 prefix，子命令也可能被错误归到全窗祖先。空/负尺寸 damage 在调度与并集前忽略；当前覆盖只查冻结 present clip，pending 并集不是已画证明。
  判定器 `conformance_gui_paint_hash_scope` 已正式通过三层嵌套、异常恢复、真实祖先变化、零高祖先、多 pending 与 full/partial 逐像素比较；没有修前红证据，不据 profile 总数逐帧归因 owner，也不由此宣称 IDE 性能通过。
- **有界滚动损伤须覆盖全部变化**：仅在损伤边界覆盖全部变化时使用 bounded damage；它已安排重绘，不预先无范围 RequestRedraw，保留 unknown/full 优先级。
  提示含阴影溢出时须保障旧完整区域在下一自然帧清除；body 后才扩大损伤不能证明当前帧已修，仍须整幅像素对照验证。
- **首次出现在 partial 帧的悬浮提示不能只靠 RequireFullFrame 保完整**：该调用只武装下一自然帧，
  不调度重绘、也不改当前帧已捕获的裁剪；能否本帧绘制须按捕获的 present 矩形判覆盖
  （`FrameDamageContains`）。未覆盖整个提示（含阴影）时整层延期不绘，并 RequestRedraw 一次
  等 full 后继帧；确认完整包含时才保持 partial，不得为保险扩成全帧，也不得轮询 busy 重绘。

- 页面动画与背景特效同一显示拍先合成再提交一次，局部页帧仍推进条带外
  特效，并合并旧/新特效损伤。为什么：FxPresent 先提交、PresentFrame 再提交
  曾在同拍执行两次原生呈现。宽条带也须更新干净快照；用真实提交日志和
  整帧像素参考验证，否则下一特效拍会恢复旧页面。任何仍标记有效的干净
  快照都须补齐重画条带；复制面积阈值只能触发快照失效，不能跳过补丁仍
  宣称有效。

### 静态玻璃组合缓存的来源与计账（2026-11-15 实证）

- 最终 blur/fill/sheen 合成缓存须同时核对有效样式、surface 身份和真实 straight ARGB 来源；
  原 blur 的抽样 hash 不能证明来源等价。为什么：单个未抽样背景像素变化也必须使组合失效，
  dirty exact miss 还须避免旧 blur 的抽样命中。只在完整 clip 覆盖真实输出 bounds 时捕获；
  partial/GPU/外扩或动态效果不支持时保守回退，不把旧前景收进来源或漏掉阴影。
- 缓存复用现有有界 slot/销毁生命周期，并报告来源+结果缓冲成本、容量上限及 first-cold
  捕获成本；命中 copy 计实际 restore 面积，输入/结果 copy 计 snapshot。为什么：缓存可
  降低混合却增加复制，不能用漏计工作量或像素回归通过代替真实 IDE 帧预算验收。
  判定器：`static_glass_cache_test` 的冷暖全像素/计数、背景单像素变化、clip、多 surface
  与销毁复用；2026-11-15 正式单项通过131.07s，性能结论仍须独立正式测量。
- **旧 blur-hit 的访问优化（2026-11-16 实证）**：验证 surface/几何/radius/圆角等缓存键后，
  dirty=false 可跳过来源 checksum；命中时仅逐行复制 panel 与 clip 交集内已含 coverage 的
  straight ARGB，不扫描整 panel 逐像素再裁剪。为什么：31×1 clip 曾触发32768次访问，
  保护页 red 证实读取 clip 外缓存；green 保持全部目标像素与 guard 不变。保留 dirty=true、
  miss/deferred 和组合缓存完整 clip 门，**blurhit 仍按原完整面板面积计账**，不降低统计口径
  冒充收益。扩展 static_glass_cache_test 正式通过95.22s；cold 与 IDE 预算须独立验收。

<!-- experience:initial-shape-fill-2026-11-16 -->
- **透明首层只能省掉真实混合，不能只改账**：完整帧、无 shape shadow、单实体非椭圆背景可走
  internal 初层入口；多区域、shadow、partial miss 保留旧 painter。仍检查真实目标 alpha：
  da=0且有效sa>0可直接赋 straight源RGB，sa=0完全不写（含隐藏RGB）；AA先按原coverage求
  floor(sa*cov/255)，再决定赋值或原source-over。共用原round扫描/coverage，不复制近似算法。
  直接赋值计fill（结果可半透明），真实混合计blend，round调用/extent保留；新入口AA corner
  也记真实像素，旧corner未计fill/blend，不能声称分类严格等额搬账。mixed rectangle不伪造
  单面积top；区间去重top不是首帧分项。融合blur/fill/sheen仅省遍历时，逐层floor与混合仍须计入。
  判定器：initial_shape_fill_test全source/destination alpha、隐藏RGB、AA/clip、真实计数及
  multi/shadow/partial/ellipse回退、多DPI。前轮E2004编译红保留；测试按既有模式放namespace Gui
  访问internal，禁止扩大生产public API。2026-11-16正式PASS110.30s，同轮4/4，日志
  targeted-20261116-011717.log（SHA256 cb45ad7ae3fc3a7fde49d364092673b51731c2f7954190089a4d96b527c00088）。
  PowerShell GUI exe探针需console或可靠等待/输出；正式run_case.cmake同步execute_process捕获stdout，
  不需强制console。组件exact通过不等于fresh IDE首帧/整体固定预算通过，禁止以warm替cold。
<!-- /experience:initial-shape-fill-2026-11-16 -->

## 缩放纪律(DPI:为什么界面忽大忽小)

框架样式路径负责自动缩放一次:主题 token (字号/高度/间距)由
`App.ScaleThemeMetrics` 按"基线×密度档×DPI"重算;CSS 字面长度在共享
解析层换算。来源必须随每个标量及级联胜出值保留,不能凭数值等于主题值
猜测已经缩放。跨 DPI 异常也可能是框架缺陷:曾复现绝对行高、字距、边框
漏乘 DPI,设计器与运行时共用错误仍能像素相等。验收要同时断言逻辑长度
的预期换算和同目标 DPI 的布局/像素,发现缺陷修共享层,不在应用里补乘。
`Canvas.DrawText` 的 fontSize、手算的坐标间距不经过样式自动缩放。

- **主题重算后重建已安装的应用 CSS**:主题 token 展开成物理值后会留在样式表里;
  原生窗口显示时才获知 DPI,若只缩放 theme,会出现按钮尺寸正确而文字仍为旧字号。
  验证须先安装 CSS,再改变 DPI/密度,同时断言全局、部件及控件缓存的结果。
- **响应式视口取实际逻辑客户区**:原生窗口的媒体查询及 vw/vh 随客户区变化,
  离屏设计预览使用声明的逻辑尺寸。换视口还须作废控件缓存,包括没有 @media 的
  vw/vh 样式;固定读初始设计尺寸曾使手机断点失效,只改全局键又会留下旧布局。
- **每个独立属性独立保留来源**:width/min-width、四侧 padding/margin、
  gap/row-gap/column-gap 不共用缩放位;shorthand 与 calc 按分量保留来源,
  `!important` 的值与来源同取胜出声明。共用来源曾使未缩放长度被误当物理值。
- **内联 style 保留原文,在解析上下文中重放**:不要同时烤进控件标量;
  替换/清空样式后旧字号、边框与背景曾因此残留。用实际
  `SetProp("style", ...)`→`ResolveStyle`→量测路径验证初次、替换、清空。
- **Clone 复制单位环境,文本 fallback 与 rem 基准分别保存**:补拷贝单位
  环境曾把历史 16px 默认文字悄悄换成主题 14px,改变未声明字号的布局;
  单位求值应读根字号,文字 fallback 保持原契约,各自只缩放一次。

从截图还原界面时的倍数判定是另一类坑:先按 `references/screenshot-restore.md`
1.5 节"三票定倍数"判出原图 DPI 档,换算只在布局账本里发生一次;把截图
物理像素直接抄进代码是双重缩放的头号来源。

硬规则只有一条:**每个尺寸值必须明确属于下面两条路径之一,全项目不得第三种**:

1. **样式路径(自动缩放,禁止再乘)**:CSS 声明、控件属性、`FontOr/Width`
   等 StyleBox 取值——写 token/档位值即可,框架已缩放,手再乘一遍就是
   "150% 显示下按钮大 1.5 倍"的双重缩放。
2. **控件内部绘制/游戏世界路径(手动缩放,禁止忘记)**:`Canvas.DrawText` 字号、内部绘制的
   坐标与边距——必须过 `app.Scale`;且**封装成项目内单一 helper**(如
   `Ui.Dp`),禁止在使用处散落内联 `* dpiScale / 100`。

自绘文字字号不许裸写数字:取主题字号阶梯(`Style.FontFallback(app,
"medium")` 等)或经 helper 缩放的档位值。审计手段:grep 直写字号数字
(如 `DrawText(..., 20)` 这类)、内联 `dpiScale`/`Scale(` 乘法、裸
`0xAARRGGBB`——每处命中都是"忽大忽小"的候选。

悬停/点击命中是缩放纪律的第三条战线:**鼠标坐标与画布坐标必须同一
空间,且 DPI 缩放全链只准发生一次**。150% DPI 下窗口客户区是逻辑
尺寸 ×1.5 的物理像素,画布 1:1 画在物理像素上,应用层鼠标坐标也是
物理客户像素——三者本就同空间(进程内探针实证:鼠标坐标与
GetCursorPos+ScreenToClient 的物理落点逐像素相等,实机悬停卡、轴
指针全钉在光标处)。**遇到"悬停/命中整体偏移 ×DPI"的疑云,先怀疑
测量工具**:DPI 不感知进程(默认的 PowerShell/脚本宿主)里
SetCursorPos/GetCursorPos/GetClientRect 被 OS 按 DPI 虚拟化——拿
它摆光标、算窗口原点,会伪造出恰 ×1.5 的"错位",引擎如实上报却被
记成缺陷(真实案例:"全图表车道悬停右下偏 1.5 倍"挂账一轮,进程内
对照定谳为工具伪影)。外部光标自动化必须先声明 DPI 感知;鼠标
空间疑云的判据 = 探针进程内直接调用 GetCursorPos + ScreenToClient
与应用鼠标坐标同帧对照,不用注入式 SetCursorPos 当证据。

