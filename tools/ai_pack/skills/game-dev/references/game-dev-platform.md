# Zan 游戏开发——平台专项

> 从 game-dev SKILL.md 拆出：WorkerW 桌面壁纸钉嵌、GPU 档 3D 与 NVIDIA 死锁、移动端与触屏。

## 动态桌面壁纸：WorkerW 钉嵌定式（examples/gui_wallpaper）

- 钉嵌序列：`App.CreateDarkStage` 无框窗（客户区=物理 1:1）→ user32
  `FindWindowA("Progman")` 发 `0x052C` → `FindWindowExA(progman,0,"SHELLDLL_DefView")`
  → `FindWindowExA(0,defview,"WorkerW")` → `SetParent` 过去 →
  `SetWindowPos` 拉满。DefView 不在 Progman 直下时退回直接挂 Progman。
- **DPI 感知是外壳启动时才提升的**：`GetSystemMetrics` 必须在
  CreateDarkStage **之后**读才是物理像素；先读后建拿到的是 DPI 虚拟化
  尺寸，壁纸差出一截。
- SetParent 成 WorkerW 子窗后，**顶层枚举（EnumWindows/win-shot）再也
  看不到它**——按 PID 抓窗会 NO-WINDOW。验证钉嵌靠全屏 CopyFromScreen
  （图标应浮在动画上）+ NO_PIN 对照组抓窗口自身。
- 壁纸省电门控判"前台是否盖住桌面"要**按面积 ≥93%**，不能按"完整
  覆盖"：最大化窗口只露一条任务栏（面积 98.7%），完整覆盖判漏掉它，
  为一条 38px 的条空转烧 0.6 核。放行
  Progman/WorkerW/自己，否则桌面空闲时误判成被盖住。
- 单张静图让"人物动起来"：72 条水平带 `BlitImage` 逐条位移——摆幅
  smoothstep 包络自下而上（坐姿底部钉死、头部最大）、相位随高度偏移
  成鞭梢拖曳、肩部叠加高斯窗呼吸；条带重叠 1px 防缝、整幅外扩 32px
  防摆动露黑边。真·眨眼微笑需要图生视频模型（常用 image API 端点没有）。
- 粒子要**避开人脸框**（金币飘过脸像一颗痣），币径按屏高定标，
  xorshift 固定种子让每次启动分布一致、可回归对比。

## GPU 档 3D（DrawMesh3D）平台事实与 NVIDIA 死锁定式

- **平台可用性查 `gui_gl_context.c` 的分支**：GPU 后端只接了
  `_WIN32`(WGL) 与 Linux/GLX；`#else` 全 stub 段让 Android/OHOS 上
  `zan_gl_ctx_create` 返回 0、GPU 后端根本不装——`SetRenderBackend(1)`
  返回 0 是**预期行为**，应用按 demo 定式落 2D 线框回退（同一份相机
  数学画 2D 投影，HUD 标 "CPU wireframe"），不算失败也别为它改平台
  stub。
- **NVIDIA wedge 根因（已修，勿回退）**：帧中途对"新纹理名"调
  TexImage2D 分配存储，1-2 帧后死锁 `nvoglv64!DrvPresentBuffers`
  （present 线程与渲染线程在 swap 在飞时互等）。修法在
  `gui_gl_backend.c`：新纹理先入 pending 队列，**present 时**才
  dataless TexImage2D + TexSubImage2D swizzle 上传 + Finish；且每次
  3D draw 尾部 `gl.Finish` 排干 3D pass（每帧一次的代价由帧门控
  吸收）。症状识别：一上真纹理就整机卡死、纯色几何没事、换 NVIDIA
  才炸——是时序实现问题，不是显卡问题。
- **声明性 API 的平台分支缺失=静默失效**：`Native.PresentFull` 曾只写
  `#if WINDOWS` 臂，非 Windows 整窗帧声明丢成空操作，Android 画面冻在
  第一帧。新加平台
  分支 API 时把所有 `#if` 臂抄全，缺臂不报错。

## 移动端与触屏

- 触屏：Gui 运行时把手指合成鼠标按下（GuiHost 循环里就是 kind 2
  code 0），SDL 版场景层另接 FingerDown/Up 直发的定式在 GuiHost 里
  不需要——接口根本没有 Finger 事件。手机点不动先查外壳的合成路径。
  （模拟器验证 tap→放钩→抓取→计分→HUD 全链路同桌面。）
- **Android 返回键先交给场景，退出还须完成 Activity 生命周期**：GuiHost
  在 Android 启动时设置 `ZAN_GUI_BACK_AS_ESCAPE=1`，原生驱动将 Back
  转为 kind 4/5、VK 27；长按重复的 down 不重投，避免一次按键先回菜单
  又退出。普通 GUI 保留窗口关闭语义。Zan Main 返回后必须请求
  `ANativeActivity_finish` 并继续泵 Looper，处理 `APP_CMD_DESTROY` 后
  原生工作线程才能返回；新 Activity 启动要清 `closed`、销毁等待和旧输入队列。
  坑出处：游戏返回键直接结束原生线程，Java Activity 仍等待输入回执，
  PID 未变却已 ANR；补生命周期后又暴露 `closed` 残留导致重开即退出。
  验证须覆盖失败/通关按钮返回、系统 Back、菜单退出与再次打开，并核对
  画面、Activity 状态和日志，不能只检查 `pidof`。
- **画布对象会被整体换掉，Start 里抓引用必死**：Android 上表面晚于
  Start 到达（转向、后台往返亦然），App.SwapCanvas 换新 Canvas 对象，
  Start 时 `g.c = host.App.canvas` 抓的旧引用画进已销毁表面、永不
  present——症状是壳 chrome/标题正常、游戏场景全黑只剩 Clear 色，
  logcat 无任何错误、循环照跑。定式：Render 每帧把传入参数同步给
  游戏对象（`this.g.c = c;`），别在 Start 抓。坑出处：goldminer
  APK 黑屏，探针逐段排除（v1 参数画全亮 → v2 复刻 goldminer 原语
  全亮）才定位到画布身份，不是图元/字体/资产问题。
- **视口适配（StageViewport 契约，零黑边）**：GuiHost.Run 每帧在
  BeginFrame 后调 `CDraw.StageViewport(canvas, ContentTop, 设计宽,
  设计高, marginColor)`——短轴贴设计、长轴延展逻辑空间；模板 Render
  开头取 `Data.W = host.StageWidth` 当帧值、布局全部锚定活的
  `Data.W`（锚点如 HOOK_X=W/2 帧首随 W 更新），指针用
  `host.MouseX/Y`（内含 Unmap 反变换）。**物理状态绝不能在 Render
  里归位/钳制**：Render 跑在 FixedUpdate 之后，帧内推进的摆角会被拍
  回、摆钩冻死。坑出处：用户报"改窗口后显示位置变了实际位置没变、
  摆幅太小绳子太短抓物品还在原位"——旧代码在 Render 里
  `clawLen=110` 重置 + 物理坐标用 Start 时的私有副本，显示跟随新锚
  而物理留在旧锚；修法=物理全部锚定每帧刷新的 Data.HOOK_X/HOOK_Y
  单源，Render 只读不写物理。桌面 1200x800 与安卓竖屏 1080x2400 双
  端数值扫描验证（绳像素跨伸→收变化、灯=44 恒定）。
- **"右边留一截"两类根因（2400x1080 模拟器）**：①壳窗口几何——
  只靠 decor `setSystemUiVisibility(0x1806)` 在 API 30+ 拦不住
  decor fit system windows，窗口 frame=[136,0][2400,1080]（刘海
  cutout 内缩）→ 左/右空条。修法=JNI 动态解析（Android 8 也能加载
  同一个 .a）：`Window.setDecorFitsSystemWindows(false)` +
  `layoutInDisplayCutoutMode=ALWAYS`，且**每次 APP_CMD_INIT_WINDOW
  都重新断言**（首次 INIT_WINDOW 早于 create_window，转屏/后台往返
  也会重建窗口）。②模板画死 1280×720——StageViewport 把逻辑舞台
  延展到 1600×720，模板仍只画 1280 宽，剩余 320 逻辑宽（480 设备
  px）落 marginColor 空条（(24,24,28) 带就是它）。修法=定式
  `SyncStage`：VW/VH 静态属性返回帧首同步的 stage 尺寸
  （`vw>0?vw:1280` 兜底设计值），IGuiHostLoop.Render 开头
  `g.SyncStage(host)` 把 `host.StageWidth/StageHeight` 写入。
  验证=装包后 `dumpsys window` 查 frame=[0,0][2400,1080] +
  截图 PIL 左右 8px 边带颜色普查（出现 (0,0,0) 黑带=根因①、
  (24,24,28) margin 带=根因②），menu+对局各截一张。
- **注意 GUI 目录下有同名 SKILL.md 时以项目级为准**——
  zan-lang 仓库内的 game-dev/app-migration/gui-design 会覆盖
  用户目录版本，改前先确认动的是哪份。
- **APK 启动即崩 `UnsatisfiedLinkError: cannot locate symbol
  "zan_audio_load_wav"`**：gui_runtime.c 单 TU 末尾 `#include
  "zan_audio.c"`，其 WASAPI 静态量（`zan_audio_dev_freq` 等）收在
  `#ifdef _WIN32` 块内，但 `zan_audio_play` 有行在守卫外引用了它——
  Windows 能编，Android NDK 交叉编译时整个 zan_audio.c 静默缺符号
  （llvm-nm 看 .o：0 个 zan_audio 符号），打包出的 libmain.so dlopen
  失败、NativeActivity 秒退。**APK 能装上≠能启动**，装完必须
  `am start` + `pidof` 确认进程活着；崩了先 `logcat -d | grep
  LoadNativeLibrary`。修法=守卫外的引用收进 `#ifdef _WIN32`（非
  Windows 设备永不 open，play 早已 return 0，该行不可达）。坑出处：
  goldminer v3 APK 装上即退，桌面全绿毫无征兆。
- **Android 没声音 = AAudio 后端 + 链接行 + 库存根三处都要落**：zan_audio 原生混音此前只有 WASAPI（Windows），Android 侧
  `zan_audio_open` 直接不开。定式：①zan_audio.c 增 AAudio 后端
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
  系统即转竖屏，别在模板层调 Android API；②`static bool Portrait
  { return VW < VH; }` + 全部几何 getter（棋盘原点/格距/按钮行/
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
- **Android 上 Assets.Find 只回相对路径且 File.Exists=false**——纹理资产解析不通，BlitImage 拿不到路径，模板的矢量
  兜底就是真机上的实际画面；内嵌资产链路缺口已记 TASKS.md。验证
  场景渲染时别被"贴图缺失"骗过去，矢量兜底亮了就算渲染链路通。

