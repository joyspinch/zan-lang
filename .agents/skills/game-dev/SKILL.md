---
name: game-dev
description: Zan 上做 2D 游戏(templates/game/* 与 stdlib/Game)的帧循环、HUD 合成、性能与手感方法论——帧节奏全段预算、门控渲染与帧率档位、"重绘后才能 present"合成契约、外设惰性初始化预热、面板 Dock 手动摆位、手感=参数曲线对齐参照、无头仿真+截图+像素复核验证仪式、移动端触屏与出包。凡是用 Zan 写实时游戏(帧循环、动作、手感、HUD 合成)、改游戏模板、调游戏帧率/卡顿/失焦/HUD 顶栏/结算面板,或提到 手感/掉帧/闪屏/失焦/首次操作卡 时使用;HUD 字号/度量忽大忽小、DPI 缩放路径混用也用它;文字/棋类/回合制/放置等控件驱动、无实时帧循环的游戏不适用本文件,走 gui-design。
---

# Zan 游戏开发：帧循环、HUD 与手感

> 提炼自一个游戏模板从"能玩"到"手感对齐原版"的完整迭代（每轮修复都有
> 根因分析与验证记录）。框架侧看 `stdlib/Game/Kit/Host.zan`
> （LimitFps/SetIdleFps/SetRedrawInterval/ShouldRender/Pace/Focused/
> Begin/End），范例看 `templates/game/` 各游戏。

## 帧循环结构：每段有名字，预算算整帧

- 主循环固定分段：**事件 → 世界步进 → 场景绘制 → HUD 合成 → 帧尾 pacing**，
  每段夹计时点。性能问题先分段测量，不要凭感觉猜哪段慢。
- **pacing 必须睡在帧尾、覆盖全部段**。在场景段末尾就睡满帧预算、把 HUD
  的回读+栅格化+纹理上传+合成呈现留到睡眠之后，实际帧周期=预算+HUD 段，
  帧率恒被拖慢且抖动（Kit 的 `Pace()` 在帧尾，不要绕开它自己睡）。
- 逐段计时用**环境变量开关的 prof 走廊**（如 `XXX_PROF=1`）：启动后定时
  自动触发一次玩家操作（自动发射/自动点击），从那一帧起连打 N 帧分段
  账单到 stdout。复现"首次操作卡"这类问题全靠它，人手点永远复现不齐。
- **周期动画"几秒才走一拍"，先查计时源、别查步进逻辑**。`Form.Every(ms)`
  这类周期任务在**空闲窗口**上会拿着"上一渲染帧的钟"反复判"未到期"：窗口
  不重绘 → 帧钟不前进 → 任务重挂唤醒，表现是动画慢 N 倍、日志永不推进，
  一直等到有输入事件逼出一帧才追一步（legend 页 31 实测：4 秒只走 8px =
  一拍；键盘一动立刻补上）。修复在框架侧——`Form.PumpTasks` 改读
  `Window.GetTickMs()` 真实单调钟（仍服从 `Window.FreezeTick`，确定性截图
  不受影响）。**自制帧循环同理：计时一律取真实单调钟，不要复用"渲染帧
  时间戳"**；后者只该用于帧间隔统计，不该当"现在几点"。

## 首次操作卡顿：惰性初始化必须预热

- 声卡设备首次播放时才同步打开（WASAPI 上实测 ~500ms）、首帧纹理/字体
  图集/着色器编译同理——这些开销会**正好落在玩家第一次操作的那一帧**。
  启动期预热：以 0 音量播一声、预绘一帧，把初始化移出对局。
- 定位方法就是 prof 走廊：账单显示卡的那段在哪，修复就是把哪段挪到开局。

## 门控渲染：按状态定帧率档位

- 游戏帧与呈现帧分离：世界每帧都动（摆钩/倒计时）就到点即画；纯空转的
  帧只推进步进，**跳过整段重绘+回读+合成**（`ShouldRender()` 门控）。
- HUD/合成贵（一次=全画布回读+软件栅格+双纹理上传+合成，1080p 可到
  6-10ms）：按状态分档——动作期逐帧、空闲期两帧一拍、失焦再放宽
  （`Focused()`）；**状态切换帧与有输入的帧立即合成**，反馈不受节拍影响。
- **鼠标移动不得击穿重绘门**：MouseMotion 每秒上百条，只记坐标，不标记
  需重绘——否则轻轻晃一下鼠标，门控失效，恒定满帧率。
- 失焦"几秒一刷新"的教训：放宽档位的同时必须保留**唤醒源**（状态切换、
  输入事件立即合成的那条路）。节流节掉唤醒源，窗口就像死了一样。

## 动态桌面壁纸：WorkerW 钉嵌定式（examples/gui_wallpaper）

- 钉嵌序列：`App.CreateDarkStage` 无框窗（客户区=物理 1:1）→ user32
  `FindWindowA("Progman")` 发 `0x052C` → `FindWindowExA(progman,0,"SHELLDLL_DefView")`
  → `FindWindowExA(0,defview,"WorkerW")` → `SetParent` 过去 →
  `SetWindowPos` 拉满。DefView 不在 Progman 直下时退回直接挂 Progman。
- **DPI 感知是外壳启动时才提升的**：`GetSystemMetrics` 必须在
  CreateDarkStage **之后**读才是物理像素；先读后建拿到的是 DPI 虚拟化
  尺寸，壁纸差出一截（2026-09 实测）。
- SetParent 成 WorkerW 子窗后，**顶层枚举（EnumWindows/win-shot）再也
  看不到它**——按 PID 抓窗会 NO-WINDOW。验证钉嵌靠全屏 CopyFromScreen
  （图标应浮在动画上）+ NO_PIN 对照组抓窗口自身。
- 壁纸省电门控判"前台是否盖住桌面"要**按面积 ≥93%**，不能按"完整
  覆盖"：最大化窗口只露一条任务栏（面积 98.7%），完整覆盖判漏掉它，
  为一条 38px 的条空转烧 0.6 核（2026-09 实测）。放行
  Progman/WorkerW/自己，否则桌面空闲时误判成被盖住。
- 单张静图让"人物动起来"：72 条水平带 `BlitImage` 逐条位移——摆幅
  smoothstep 包络自下而上（坐姿底部钉死、头部最大）、相位随高度偏移
  成鞭梢拖曳、肩部叠加高斯窗呼吸；条带重叠 1px 防缝、整幅外扩 32px
  防摆动露黑边。真·眨眼微笑需要图生视频模型（常用 image API 端点没有）。
- 粒子要**避开人脸框**（金币飘过脸像一颗痣），币径按屏高定标，
  xorshift 固定种子让每次启动分布一致、可回归对比。

## 合成契约：重绘后才能 present

- 画布内容在 present 后不保留。**拿旧画布凑帧会把空 HUD 贴上屏**（整条
  顶栏闪烁）。任何"跳过一帧不画但照常 present"的优化都违反契约，症状
  就是周期性闪屏。
- 场景帧与 HUD 帧解耦时，场景帧保留在后备缓冲，中间拍不回读。
- HUD 不显示/不置顶/画布错位，先查三条：合成循环是否根本没跑（空闲
  死锁）、是否绕过了置顶链、锚点算的是窗口还是画布坐标。

## GPU 档 3D（DrawMesh3D）平台事实与 NVIDIA 死锁定式

- **平台可用性查 `gui_gl_context.c` 的分支**：GPU 后端只接了
  `_WIN32`(WGL) 与 Linux/GLX；`#else` 全 stub 段让 Android/OHOS 上
  `zan_gl_ctx_create()` 返回 0、GPU 后端根本不装——`SetRenderBackend(1)`
  返回 0 是**预期行为**，应用按 demo 定式落 2D 线框回退（同一份相机
  数学画 2D 投影，HUD 标 "CPU wireframe"），不算失败也别为它改平台
  stub。
- **NVIDIA wedge 根因（已修，勿回退）**：帧中途对"新纹理名"调
  TexImage2D 分配存储，1-2 帧后死锁 `nvoglv64!DrvPresentBuffers`
  （present 线程与渲染线程在 swap 在飞时互等）。修法在
  `gui_gl_backend.c`：新纹理先入 pending 队列，**present 时**才
  dataless TexImage2D + TexSubImage2D swizzle 上传 + Finish；且每次
  3D draw 尾部 `gl.Finish()` 排干 3D pass（每帧一次的代价由帧门控
  吸收）。症状识别：一上真纹理就整机卡死、纯色几何没事、换 NVIDIA
  才炸——是时序实现问题，不是显卡问题。
- **声明性 API 的平台分支缺失=静默失效**：`Native.PresentFull()` 曾只写
  `#if WINDOWS` 臂，非 Windows 整窗帧声明丢成空操作，Android 画面冻在
  第一帧（详见 testing-android-native"画面冻结排查定式"）。新加平台
  分支 API 时把所有 `#if` 臂抄全，缺臂不报错。

## 面板/弹层摆位：Dock 与手动 Place 的边界

- Panel 默认停靠是 fill——**只调 Place() 不切到手动停靠，面板会被 fill
  分支撑满画布**，实测框位能漂移几十像素。手动摆位三件套一起上：
  切手动停靠 + 定尺寸（Prefer）+ 内部用列布局三段式（标题/数据/提示）
  居中排版。
- 游戏内面板的间距同样走 4 的倍数档位（gui-design 的阶梯在这里照样适用，
  只是画布绝对坐标替代了文档流）。缩放路径的划分见"自绘度量的缩放边界"。
- 验收是**像素复核**：按预期公式算出面板框的坐标与颜色，从截图中量测
  断言（边框色在第几像素、标题带中心 x=面板几何中心），不靠"看着行"。

## 自绘度量的缩放边界(D16:字号忽大忽小的根因)

游戏画布上有两套绘制,缩放路径完全不同——每个尺寸值必须明确归属其一,
**全项目不得出现第三种写法**(这是"有的特大有的特小"的唯一根因):

1. **Gui 控件路径(自动缩放,禁止再乘)**:Panel/Label/按钮等控件,尺寸
   写 token/档位值(`app.theme.gap*`、字号阶梯、`.small/.medium` 档位类)。
   框架已按 DPI×密度缩放,代码里再乘 `app.Scale()` 就是双重放大。
2. **Canvas 自绘路径(手动缩放,禁止忘记)**:HUD 的 `Canvas.DrawText`
   字号、手算的坐标/边距,不经过任何自动缩放——**必须过项目内单一缩放
   helper**(如 `Hud.Dp(v)`,内部一行 `app.Scale(v)`),禁止在使用处散落
   内联 `* dpiScale / 100`(漏一处=150% 屏上特小,乘两处=特大)。

**混排判定**:一个画面里同时有自绘 HUD 和 Gui 面板时,两边的字号/间距
必须同源——自绘文字取主题字号阶梯(`Style.FontFallback(app, "medium")`
等已缩放值)或 helper 缩放后的档位值,不许裸写;Gui 面板不许手乘缩放。
虚拟设计分辨率的项目(整幅画面按固定设计稿放大)必须二选一:全走 Gui
逻辑像素(推荐,DPI 免费),或全走"虚拟坐标×单一缩放因子";一半控件
一半自绘各用各的缩放是最忌讳的形态。

审计:grep `DrawText(`、内联 `dpiScale`/`Scale(` 乘法、裸字号数字——
每处命中都是错乱候选;HUD 截图里量一遍字号,与 theme 阶梯对照。

## 游戏舞台的统一 DPI 契约（GuiHost，脱 SDL 定式）

固定分辨率游戏走 `Game.Foundation.Gui.GuiHost` + `Game.Kit.CanvasPrims`，
**不要自创"物理像素窗口"路径**——三条坑都踩过：①普通 `CreateDark` 按显示
器 DPI 放大客户区，150% 屏上 1280x720 舞台只占 1920x1080 画布的左上角；
②自建物理像素窗口又丢了标题栏，且撞上工作区 82% 钳制（副屏把 1280x720
钳成 868x517）；③chrome 字号跟 dpiScale、标题条高度跟设备 DPI，两套来源
在舞台窗口里对不齐（32px 字挤 48px 条）。统一契约（已在 stdlib 实现，
模板只需遵守）：

- 窗口:`App.CreateDarkStage(title, w, h)`——客户区 = 舞台 w×h + 标准标题
  条（设备像素），不做 DPI 放大、不参与工作区/最小尺寸钳制、不做
  AdjustWindowRect 补偿（NCCALCSIZE 已把客户区扩成整窗）。
- 绘制:模板按 0 基舞台坐标作画，帧首 `CDraw.Origin(0, host.ContentTop())`，
  清屏用 `CDraw.Clear`（只铺内容区）；所有 C* 助手自动叠加原点，绕开 C*
  直调 canvas 的绘制会漏偏移。chrome 由宿主在 `loop.Render` 之后
  `RenderChrome` 叠画，皮肤按钮默认关；全屏（ContentTop==0）自然退化。
- 输入:鼠标是客户区坐标，y 减 `host.ContentTop()` 换回舞台坐标。
- 验证:客户区物理尺寸应为 `w × (h + 32*dpi/96)`。

**非 DPI 感知进程的测量是假象**:150% 屏上 1280x768 物理窗会报成
853x512（÷1.5 虚拟化），别拿它反推"钳制/缩放 bug"——截图/测量脚本先
`SetProcessDpiAwarenessContext(-4)`（见 `_scratch/GuiHostProbe/shotpid.ps1`）。
同理，ctest 冒烟在并行会话构建时会假失败（共享 build\zanc.exe），单独
重跑一次再定论。

## GuiHost 输入事件契约：kind 1=移动 2=按下（文档曾写反）

IGuiHostLoop.Event 的 kind 编码与 Win32Shell/App 控件分发是同一套：
**1=鼠标移动、2=鼠标按下、3=鼠标释放、4=键按下、5=键抬起**。GuiHost
接口注释曾把 1/2 写反，六个游戏模板照错文档编码——鼠标移动被当点击、
真实点击被忽略：goldminer 移植实测"无输入自动放钩"暴露（一次游离
WM_MOUSEMOVE 就放一钩），gomoku/ddz 真机鼠标操作等于乱落子/乱选牌。
修的是文档 + 同批修模板（2026-09）。键盘 keycode = Windows VK
（WM_KEYDOWN wParam）：空格 32、回车 13、Esc 27、方向键 37..40、
字母=大写 ASCII（P=80、Q=81、C=67）。接输入前先对 Win32Shell 的
Post 调用核对编码，别信二手注释。

## 追逐平衡：吸力/拉力必须压过目标速度

"每帧向移动目标收拢"的磁吸（糖果吸向蛇头、相机跟角色、吸附对齐），
若吸力是**固定值**且 ≤ 目标速度，目标一跑起来吸附物会吊在触发圈边缘
一路跟跑、永远到不了——观感即"糖果粘在身上跟着跑"。吸力绑定目标当前
速度取倍数（如 `1.5×速度 + 常数`），保证圈内确定性捕获；蛇蛇乐磁吸
260px/s 固定值 vs 冲刺 348px/s 即踩坑。

## 手感：参数曲线，不是常数

- "速度"是一个**按对象属性分档的曲线**（如收线速度=重的慢轻的快，轻重差
  拉开到 3 倍以上才有"吃力/轻松"的手感差异），整体档位另调。对照参照
  原版逐段校：先整体放慢一档，再调比值，玩 30 秒就能 felt-diff。
- AI/自动玩家的决策阈值同属手感：挡道的垃圾必抓（别浪费收线时间）、
  值不值当的分数线随剩余时间放宽——曲线写参数，不写死散落各处。

## 移动端与触屏

- 触屏：Gui 运行时把手指合成鼠标按下（GuiHost 循环里就是 kind 2
  code 0），SDL 版场景层另接 FingerDown/Up 直发的定式在 GuiHost 里
  不需要——接口根本没有 Finger 事件。手机点不动先查外壳的合成路径。
  （模拟器实测：tap→放钩→抓取→计分→HUD 全链路同桌面。）
- **画布对象会被整体换掉，Start 里抓引用必死**：Android 上表面晚于
  Start 到达（转向、后台往返亦然），App.SwapCanvas 换新 Canvas 对象，
  Start 时 `g.c = host.App().canvas` 抓的旧引用画进已销毁表面、永不
  present——症状是壳 chrome/标题正常、游戏场景全黑只剩 Clear 色，
  logcat 无任何错误、循环照跑。定式：Render 每帧把传入参数同步给
  游戏对象（`this.g.c = c;`），别在 Start 抓。坑出处：goldminer
  APK 黑屏，探针逐段排除（v1 参数画全亮 → v2 复刻 goldminer 原语
  全亮）才定位到画布身份，不是图元/字体/资产问题。
- **视口适配（StageViewport 契约，零黑边）**：GuiHost.Run 每帧在
  BeginFrame 后调 `CDraw.StageViewport(canvas, ContentTop, 设计宽,
  设计高, marginColor)`——短轴贴设计、长轴延展逻辑空间；模板 Render
  开头取 `Data.W = host.StageWidth()` 当帧值、布局全部锚定活的
  `Data.W`（锚点如 HOOK_X=W/2 帧首随 W 更新），指针用
  `host.MouseX/Y()`（内含 Unmap 反变换）。**物理状态绝不能在 Render
  里归位/钳制**：Render 跑在 FixedUpdate 之后，帧内推进的摆角会被拍
  回、摆钩冻死。坑出处：用户报"改窗口后显示位置变了实际位置没变、
  摆幅太小绳子太短抓物品还在原位"——旧代码在 Render 里
  `clawLen=110` 重置 + 物理坐标用 Start 时的私有副本，显示跟随新锚
  而物理留在旧锚；修法=物理全部锚定每帧刷新的 Data.HOOK_X/HOOK_Y
  单源，Render 只读不写物理。桌面 1200x800 与安卓竖屏 1080x2400 双
  端数值扫描验证（绳像素跨伸→收变化、灯=44 恒定）。
- **"右边留一截"两类根因（2400x1080 模拟器实测）**：①壳窗口几何——
  只靠 decor `setSystemUiVisibility(0x1806)` 在 API 30+ 拦不住
  decor fit system windows，窗口 frame=[136,0][2400,1080]（刘海
  cutout 内缩）→ 左/右空条。修法=JNI 动态解析（Android 8 也能加载
  同一个 .a）：`Window.setDecorFitsSystemWindows(false)` +
  `layoutInDisplayCutoutMode=ALWAYS`，且**每次 APP_CMD_INIT_WINDOW
  都重新断言**（首次 INIT_WINDOW 早于 create_window，转屏/后台往返
  也会重建窗口）。②模板画死 1280×720——StageViewport 把逻辑舞台
  延展到 1600×720，模板仍只画 1280 宽，剩余 320 逻辑宽（480 设备
  px）落 marginColor 空条（(24,24,28) 带就是它）。修法=定式
  `SyncStage`：VW()/VH() 静态属性返回帧首同步的 stage 尺寸
  （`vw>0?vw:1280` 兜底设计值），IGuiHostLoop.Render 开头
  `g.SyncStage(host)` 把 `host.StageWidth()/StageHeight()` 写入。
  验证=装包后 `dumpsys window` 查 frame=[0,0][2400,1080] +
  截图 PIL 左右 8px 边带颜色普查（出现 (0,0,0) 黑带=根因①、
  (24,24,28) margin 带=根因②），menu+对局各截一张。
- **注意 GUI 目录下有同名 SKILL.md 时以项目级为准**——
  zan-lang 仓库内的 game-dev/app-migration/gui-design 会覆盖
  用户目录版本，改前先确认动的是哪份。
- **APK 启动即崩 `UnsatisfiedLinkError: cannot locate symbol
  "zan_audio_load_wav"`**：gui_runtime.c 单 TU 末尾 `#include
  "zan_audio.c"`，其 WASAPI 静态量（`zan_audio_dev_freq` 等）收在
  `#ifdef _WIN32` 块内，但 `zan_audio_play()` 有行在守卫外引用了它——
  Windows 能编，Android NDK 交叉编译时整个 zan_audio.c 静默缺符号
  （llvm-nm 看 .o：0 个 zan_audio 符号），打包出的 libmain.so dlopen
  失败、NativeActivity 秒退。**APK 能装上≠能启动**，装完必须
  `am start` + `pidof` 确认进程活着；崩了先 `logcat -d | grep
  LoadNativeLibrary`。修法=守卫外的引用收进 `#ifdef _WIN32`（非
  Windows 设备永不 open，play 早已 return 0，该行不可达）。坑出处：
  goldminer v3 APK 装上即退，桌面全绿毫无征兆。
- **Android 没声音 = AAudio 后端 + 链接行 + 库存根三处都要落**（2026-09
  实测）：zan_audio 原生混音此前只有 WASAPI（Windows），Android 侧
  `zan_audio_open()` 直接不开。定式：①zan_audio.c 增 AAudio 后端
  （`__ANDROID_API__ >= 26` 全机可用，AAudioStreamBuilder 回调驱动
  混音线程，与 WASAPI 同一套 voice 状态）；②`src/compiler/main.c`
  Android 链接行补 `libaaudio.so`（NDK sysroot 有系统存根，链接期
  解析符号）；③确认 `toolchain/android-{arm64,x64}/` 与
  `build/android-{arm64,x64}/` 都有该存根（发布子集从 toolchain
  拷贝）。验证：`adb logcat -d | grep -E "AAudio.*openStream"` 出
  `returns 0 = AAUDIO_OK` + `requestStart returned 0` 即流已起
  （模拟器无声卡也能看流状态）。坑出处：用户报"安卓手机没声音"。
- **竖屏棋类布局定式（ GuiHost 逻辑宽高决定转向 + 绘制/命中同源）**：
  ①转向由壳驱动——`ant_set_orientation(width > height)` 按 GuiHost
  舞台逻辑宽高比定横竖屏，模板只要按竖屏传逻辑尺寸（如 720x1280）
  系统即转竖屏，别在模板层调 Android API；②`static bool Portrait()
  { return VW() < VH(); }` + 全部几何 getter（棋盘原点/格距/按钮行/
  手牌 Y）在 Portrait 分支给竖屏值，**Draw 与 OnDown 共用同一批
  getter**，命中永不漂移；③菜单竖屏单列居中、对局顶部对手条+底部
  按钮行（菜单/重玩），照手机棋牌惯例。验证：`screencap` 后 PIL
  按色扫描（绿按钮 bbox 横向居中、棋子色 bbox 中心≈屏宽/2），
  再 2.5x 增亮整页截图肉眼确认。坑出处：用户报"棋类应该可以竖屏
  操作"+参考截图，此前全部锁横屏。
- **深色主题截图别信"黑屏"直觉——PIL 带状统计定生死**：深色背景
  （如 (9,8,10)）的截图 Read 出来一片黑，6x 增亮/gamma 0.45 也
  没用（JPEG 近黑还是黑），`r+g+b>24` 阈值又被背景本身 defeats
  （9+8+10=27 过阈）。有效方法：按水平带（0-150/150-600/…）
  统计 max 亮像素坐标+最常见颜色，或定向色扫描（主题绿
  `g>r+20 and g>b+20 and g>60`、红子 `r>140 and g<70 and b<70`）
  出 bbox 判居中/判内容；最后 2.5x 增亮整页缩图肉眼复核。坑出处：
  wq_menu.png 被疑"渲染坏/转屏失败"，实际是暗色主题正常渲染。
- 出包：`--publish --target android-arm64 --emit-apk` 一条命令；assets
  自动内嵌，加载路径保持"磁盘优先、内嵌兜底"。窗口要可自适应（横竖屏/
  任意尺寸），布局别写死像素。
- **Android 上 Assets.Find 只回相对路径且 File.Exists=false**（实测
  2026-09）——纹理资产解析不通，BlitImage 拿不到路径，模板的矢量
  兜底就是真机上的实际画面；内嵌资产链路缺口已记 TASKS.md。验证
  场景渲染时别被"贴图缺失"骗过去，矢量兜底亮了就算渲染链路通。

## 资源：内嵌内存加载

- 内嵌资源全程内存加载（ReadAllBytes→内存解码），不落盘解压。字节链要
  显式长度——**内嵌 NUL 会截断**，音频/图片"随机坏一块"先查这里。

## 微信小游戏车道：4MB 主包定死，引擎 wasm 走 CDN；壳子坑先记（2026-09-11，进行中）

> H5 画廊 wasm 模块 4.2MB 起，微信小游戏主包上限 4MB——引擎 wasm 永远进
> 不了主包。定向（用户拍板）：主包只放壳子（game.js/game.json/配置），
> 引擎 wasm + 字体走 CDN `wx.downloadFile` 落 USER_DATA_PATH 再
> `WXWebAssembly.instantiate`；文本后续接宿主 fillText 后端后连字体也省。

- **WASI fsBackend 五个方法一个不能少**：statSync/readFileSync/writeFileSync/
  listDirSync/mkdirSync，缺任何一个，wasm 在首次对应调用处直接 unreachable
  陷阱（node 里就是 "unreachable"，看似编译问题实则垫片缺方法）。fsData 用
  path→base64 map（mkfsdata.json 格式），写入再叠 files 覆盖层。
- **project.config.json 带 `"libVersion":"latest"` 会打不开项目**：touristappid
  下报"模拟器启动失败 app.json 未找到"（game 项目根本没有 app.json，是
  libVersion 解析带崩的）。删掉即好，compileType:"game" + appid
  touristappid 即可开。
- **miniprogram-automator 的 screenshot() 在小游戏项目上挂死**（连接成功、
  40s+ 无返回）。截图走 Win32 PrintWindow + PW_RENDERFULLCONTENT(2) 按
  PID 抓窗口（NW.js 窗口可抓）。CLI：`cli.bat open --project <dir>` /
  `auto` / `close`。
- **登录墙**：devtools cli close+open 会把会话打成 LOGOUT 弹登录窗，合成
  鼠标点不动 NW.js 的"微信快捷登录"按钮——自动化止步于此，必须人点一次。
- **壳子事件契约与 H5 worker 同构**（zan_env 8 槽事件 0 wake/1 move/2 down/
  3 up/4 keydown/5 keyup/6 char/7 resize/14 attached；host 先喂 7+14 再
  _start），差异只在泵：微信主线程没有 Worker+SAB 阻塞，用 JS 队列 +
  `Atomics.wait` 回退 busy-wait，present 同步 BGRA→RGBA putImageData。
  **devtools 实机验证还没过（登录墙）**，node 垫片跑到 frames=2 有一个
  未定位的协程重抛崩溃（二进制在真浏览器同契约下干净，疑 node 时钟垫片
  差异）——别把这条壳子当已验证。

## 验证仪式（每轮全做）

- 编译零错误 → **无头仿真**：模拟一个"会连点的中等玩家"打关，多种子
  （seed 可覆盖）跑经济快照 + 不变量检查，PASS 才算逻辑没坏。
- **HUD 截图通道**：无头渲染一帧 HUD 并存图，供像素复核。
- 真机跑一遍真实交互。三个通道（仿真/截图/真机）**必须是同一条代码
  路径**——截图路径单独直调而主循环漏调，就会出"截图里有、游玩看不到"
  的分叉，且被兜底渲染长期掩盖。每加一个功能，先问：三条通道都走到它吗？
- 资产定位是相对 exe 目录/工作目录向上 4 级（Assets.Find）：从
  _scratch 深目录直接跑模板 exe 时 assets 全找不到、看到的"贴图"
  其实是矢量兜底——**验证贴图要 cd 到模板目录再启动**（ddz 酒馆
  贴图整场没加载才发现，此前五个模板的"贴图正常"都是兜底画）。
- **UiDriver 像素 dump 是唯一真相，窗口截图会抓到没重绘的空帧**（2026-09-11
  传奇排行榜）：同一构建，窗口截图整片空白，而同一次运行的 `dump pixels` /
  `dump tree` 都完整。看到空白先别怀疑页面构建，用 `dump pixels`（ZPX1→PNG）
  做 A/B。`ZAN_UI_SCRIPT` 的驱动文件只传裸文件名（launcher 会拼目录，带路径
  就静默不跑）；点击后 ≥3s 再 dump，否则 tree 全零。
- **ctest 管道到 `tail` 会吃掉退出码**：后台跑 `test.ps1 … | tail` 得到 exit 0，
  日志尾却是 `TEST_FAIL`。判定看日志里的 `tests passed` / `TEST_FAIL` 文本，
  不看管道退出码；失败项先按名字归因（网络类 / 其他会话未提交的 stdlib
  改动 / 属性计数漂移），再决定是不是自己的。
