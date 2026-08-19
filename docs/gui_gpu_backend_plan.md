# GUI 渲染后端切分方案（自研 GPU 光栅，零外部依赖，跨平台）

目标：从根源解决渲染性能，同时保持正确性（不依赖控件手写脏区申报）、不引入任何第三方运行时、Windows/Linux/macOS 一份实现。

## 0. 现状（已核实，不是推测）

- 绘制原语全部通过 `stdlib/Gui/Render.zan` 里 **45 个 `zan_gui_*` C extern** 落到运行时；
  Zan 侧只有 `Canvas` 一个类持有 `surfaceId`（int 句柄），没有任何控件直接摸像素。
- 运行时 `src/runtime/gui_runtime.c` 里这些导出直接对 `zan_surface_t{pixels,stride,clip}` 做 CPU 光栅。
- 窗口/事件外壳：Windows = 原生 Win32；Linux = 原生 X11（`gui_runtime_x11.c`）；
  macOS = 原生 Cocoa（`gui_runtime_mac.m`）；另有**可选**的 SDL3 统一外壳
  （`gui_runtime_sdl.c`，CMake 选项 `ZAN_GUI_SDL`，默认 OFF）。
  发布版 Windows IDE 不使用 SDL3。

**结论：后端抽象的接缝已经存在，就在这 45 个 C 导出上。** 不需要改 Canvas 的 Zan API，
不需要改任何控件，不需要改 ABI —— 只需要在运行时内部按 surface 类型分派。

## 1. 后端分派（第 1 步）

在 `gui_runtime.c` 内引入后端 vtable：

```c
typedef struct zan_gui_backend {
    const char *name;                 /* "cpu" / "gl" */
    int  (*create_surface)(int w, int h);
    void (*fill_rect)(zan_surface_t*, int,int,int,int, u32);
    void (*fill_round)(zan_surface_t*, ..., int radius, int corner_mask);
    void (*fill_grad)(zan_surface_t*, ..., int dir, u32 a, u32 b);
    void (*blur)(zan_surface_t*, ..., int radius, int slot, int dirty);
    void (*shadow_round)(zan_surface_t*, ...);
    void (*glyph_run)(zan_surface_t*, const zan_glyph_run*);
    void (*push_clip)(zan_surface_t*, int,int,int,int);
    void (*pop_clip)(zan_surface_t*);
    void (*flush)(zan_surface_t*);    /* GPU: 提交批次；CPU: 空 */
    void (*read_pixels)(zan_surface_t*, void *dst, int stride);  /* 分层窗口回读 */
} zan_gui_backend;
```

- 每个 `zan_gui_*` 导出改为「参数校验 + 统计计数 + 调 `s->be->xxx()`」，
  CPU 实现就是现在的函数体原地搬过去，行为字节级不变。
- 原语归并：45 个导出里有大量同族重载（`fill_rounded_rect` / `_mask`、
  `fill_vgrad` / `_mask` / `fill_grad_mask`），后端只实现归并后的 **11 个原语**，
  导出层负责把重载展开成参数。
- 验证方式：`raster_bench` + `conformance_gui_*` + `runtime_gui` 在 CPU 后端下必须
  逐像素一致（这一步不改画面，是纯搬家）。

## 2. 文本 / 字形图集（第 2 步）

现状文本是唯一真正平台相关的部分：`gui_runtime_font.c` + `gui_runtime_text.c`
（Windows GDI / Linux FreeType+Fontconfig / macOS CoreText）。

- 保留现有「度量 + 栅格化成 8bit 覆盖率位图」这一层不动；
- 上面加一层 `zan_glyph_atlas`：按 (font, size, codepoint) 缓存覆盖率位图，
  CPU 后端直接混合，GPU 后端把位图上传到一张 `GL_R8` 图集纹理；
- `draw_text` 从「逐字形直接画」改为「产出 `zan_glyph_run`（图集坐标 + 目标坐标数组）」，
  一次 draw call 画完一行。

## 3. OpenGL 3.3 core 后端（第 3 步）

只用系统自带 GL，函数指针自取，**不引入 GLEW / GLFW / SDL / ANGLE，二进制不增加**：

| 平台 | 上下文 | 取函数指针 |
|---|---|---|
| Windows | `opengl32.dll` + WGL（`wglCreateContextAttribsARB`） | `wglGetProcAddress` + `GetProcAddress` |
| Linux | 系统 `libGL.so.1` + GLX（`glXCreateContextAttribsARB`） | `glXGetProcAddress` |
| macOS | 系统 OpenGL 4.1（NSOpenGLContext，`gui_runtime_mac.m`） | 直接静态可用 |

平台相关代码只有「建上下文 + 交换缓冲」这一层，各约 100 行；上层 shader 与几何全平台共用。

原语实现：

- 矩形/圆角/边框/圆/扇形：**SDF 片元着色器**，一个 instanced quad 通吃，抗锯齿免费；
- 渐变：顶点插值或 SDF 内 mix；
- 阴影：SDF + 高斯近似（`erf` 近似），不需要离屏；
- 玻璃模糊：FBO 两趟可分离高斯（当前 CPU 上最贵的一项）；
- 图片/字形：纹理采样 + 图集；
- clip：`glScissor`（矩形）；圆角 clip 走 SDF 遮罩。

### 3.1 已落地状态（第一批）

`gui_gl_context.c`（WGL / GLX，函数指针自取，失败即回落 CPU）+ `gui_gl_backend.c`
（每个 surface 一张离屏 FBO，`ZGL_MODE_*` 三种混合状态，一个 quad 批）：

- 已上 GPU：`clear` / `clear_rect` / 矩形与边框 / 圆角填充与描边 / 三向渐变 /
  圆（填充与描边）/ 径向光晕 / 扇形 / 线段与折线（不透明）/ glyph run（R8 与
  RGBA8 两张图集，双源混合出逐通道覆盖率）/ 矩形 clip / flush / 回读；
- 仍走 CPU（后端表里留空，分派时自动 GPU→CPU 同步）：阴影、模糊、快照/恢复、
  图片 blit、半透明折线（CPU 是一趟覆盖率，逐段胶囊会在接缝重复混合）；
- 混合按 `blend_over` 的直通 alpha 语义配：颜色 `SRC_ALPHA/ONE_MINUS_SRC_ALPHA`，
  alpha 通道 `ONE/ONE_MINUS_SRC_ALPHA`，透明底 surface 的 alpha 才能累积成分层
  窗口要的值。

与 CPU 的像素一致性（`tests/_scratch/gui_gl_compare.c`，640×480 混合场景）：
RGB 平均差 0.76，最大 46，>32 的像素占 0.10%，alpha 通道完全一致。差异集中在
**1 像素细线**：CPU 用 Wu 算法（按列把覆盖率分给上下两个像素），GPU 用胶囊 SDF，
AA 分布本来就不同。圆环、扇形、圆角、文本、渐变逐像素差 ≤1（`draw_circle`
的圆心/半径取样点必须和 CPU 一样落在像素左上角，差半个像素就是整条环偏一格）。

CPU 路径不受影响：切到 GPU 再切回来，比对场景的 CPU 哈希与切换前完全相同；
`tests/_scratch/gui_pixel_hash.c` 的基准哈希仍是 `5e2cdcd570653bf8`。

## 4. 呈现路径（第 4 步）

- Win32 普通窗口：GL 直接 `SwapBuffers`，无 CPU→GDI 上传；
- Win32 分层窗口（玻璃/异形）：`UpdateLayeredWindow` 需要 CPU 位图 →
  走 `read_pixels`（`glReadPixels` 到 PBO）；或改 `DwmEnableBlurBehind` + 普通窗口彻底摆脱回读；
- X11：GLX `glXSwapBuffers`；macOS：`[NSOpenGLContext flushBuffer]`；
- 无可用 GL（远程桌面、虚拟机、CI）：自动回退 CPU 后端。

当前状态：普通窗口已经直通，分层窗口仍回读。

- 后端 vtable 多了两个接缝：`present(s, native_window)` 与 `drop_window(native_window)`，
  运行时导出 `zan_gui_present_window(surfaceId, nativeWindow)` 和
  `zan_gui_release_window(nativeWindow)`；CPU 后端两者都是空的（`present == NULL`），
  于是外壳照旧贴位图，行为与改动前逐字节一致。
- GL 后端的 `present` 在外壳窗口下挂一个 GL 子窗口（Win32 `WS_CHILD` +
  `SetPixelFormat`，X11 `XCreateWindow` + `glXCreateWindow`），把后端 FBO
  `glBlitFramebuffer` 到子窗口后缓冲再 `SwapBuffers` / `glXSwapBuffers`。
  子窗口不吃输入（Win32 `WM_NCHITTEST` → `HTTRANSPARENT`，X11 `event_mask = 0`），
  输入仍由外壳窗口处理；外壳窗口自己的像素格式/GDI 路径没有被动过。
- FBO 和窗口后缓冲在 GL 里都是自下而上的（着色器已经把 surface 坐标翻过来了），
  所以直通这一 blit 不再翻转；翻转只发生在 `read_pixels` 写自上而下的 CPU 位图时。
- 分层窗口（玻璃/异形）永远不走直通：`UpdateLayeredWindow` 要 CPU 位图，
  外壳在 `glassOn` 时直接跳过 `present`，继续 `flush` + `read_pixels`。
  窗口比 surface 大的补边帧、截图、以及任何仍在 CPU 上的图元也一样回读。
- 拿不到 GL 子窗口（没有双缓冲 + window 的 FBConfig、`SetPixelFormat` 失败、
  `MakeCurrent` 失败）时 `present` 返回 0，外壳原样贴位图；切回 CPU 后端或窗口销毁时
  子窗口被销毁（`zan_gui_release_window` / 后端切换 / 上下文析构），
  不会把最后一帧 GPU 画面留在屏幕上盖住 CPU 帧。

X11（Xvfb + llvmpipe）实测：真窗口直通后 `XGetImage` 读回的屏幕像素与 CPU 路径
读回的一致（红/绿矩形与背景三处取样全等），切回 CPU 后端后屏幕跟着变成新的 CPU 帧。
Win32 直通只经过代码审查，屏幕验证靠本机 ZanIDE。

### macOS（当前进度，未在真机验证）

- `gui_gl_context.c` 增加了 Apple 分支：`dlopen` 系统 `OpenGL.framework`，
  用 CGL（`CGLChoosePixelFormat` / `CGLCreateContext` / `CGLSetCurrentContext`）
  先试 GL4 core、再试 3.2 core，拿到一个离屏 core-profile 上下文，
  GL 函数指针照 Win32/X11 那样查表；拿不到就返回 0，应用留在 CPU 后端。
- **直通还没做**：Apple 分支的 `present_begin()` 返回 0，所以 macOS 仍走
  `flush` + `read_pixels`，由 Cocoa 外壳把 surface 拷进双 `IOSurface`、
  交给 `CALayer.contents` 合成（`gui_runtime_mac.m` 原有路径，没有改动）。
  真正的直通需要把 FBO 画到 `IOSurface` 背衬的纹理、或改用 `NSOpenGLContext`
  的 `flushBuffer`，是下一步的事。
- 云端与本机都没有 macOS SDK/Cocoa 头，这一支只做到「结构与另两支一致、
  逻辑走查过、拆出来用 C 编译器单独编过」，**编译与画面必须在 Mac 上验**。
  Apple 已弃用 OpenGL，所以 CPU 兜底在 macOS 上尤其不能删。

## 5. 后端选择（按应用，不用环境变量）

环境变量是「一台机器上所有 Zan 程序共用一份配置」，不作为产品开关。改为应用自己选：

```zan
app.SetRenderBackend(RenderBackend.Auto());  // 默认：能拿到 GL 上下文就用 GPU，否则 CPU
app.SetRenderBackend(RenderBackend.Cpu());   // 强制 CPU（兜底/回归对比）
string be = app.RenderBackend();             // 实际生效的后端，供设置页/日志显示
```

已落地的形态（`stdlib/Gui/Render.zan`、`stdlib/Gui/App.zan`）：

```zan
RenderBackend.Cpu()/Gpu()/Auto()   // 0 / 1 / 2
int got = app.SetRenderBackend(mode);  // 返回是否真的装上了 GPU 后端（0 = 仍是 CPU）
int want = app.RenderBackendMode();    // 本应用要求的模式
string be = app.RenderBackendName();   // 实际生效的后端名（"cpu" / "gl"）
```

默认仍是 CPU：要求 GPU 的应用自己写一行，没写的程序像素结果一个字节都不变。
切换会把已有 surface 先 `flush`/`read_pixels` 再换后端指针，并强制下一帧整窗重画，
所以切换过程中窗口不会闪白或丢当前内容。

ZanIDE 把它接到自己的 profile（`config.cfg` 的 `renderBackend`，默认 0）上，
与别的 Zan 程序互不影响。
同一模式已用于局部帧开关：`app.SetPartialFrames(bool)` + `partialFrames` 配置项，
已取代原先的 `ZAN_GUI_PARTIAL_FRAMES` 环境变量。

## 6. 正确性底线（不回退到手写脏区）

- 整窗帧仍是正确性基线：GPU 后端每帧重画整窗，天然没有「上一帧残留」；
- 需要省上传时，从**像素/tile 比对**得出真实变化区域，而不是相信控件申报；
- CPU 后端永久保留，两个后端各跑一遍 `frame_budget`，预算分别校准。

已落地：

- **tile 比对省上传**：GL 后端给每个 target 留一份「上次上传/回读的像素」影子，
  `zgl_upload()` 按 64×64 tile 与影子 `memcmp`，只 `TexSubImage2D` 真变了的 tile
  （子矩形上传要设 `GL_UNPACK_ROW_LENGTH`，传完复位 0）。回读之后影子跟着更新，
  否则下一帧每个 tile 都会被判成脏。分配影子失败就退回整面上传，结果一样。
  混合 CPU/GPU 图元的帧（模糊、阴影、图片）实测 800 个 tile 只上传 116 个（14.5%）；
  llvmpipe 下墙钟时间看不出差别（那里「上传」就是一次进程内 memcpy），
  收益要在真 GPU 上才体现。
- **surface 销毁接缝**：vtable 多了 `drop_surface(s)`，`zan_gui_destroy_surface()`
  会调用它。surface id 会被回收，GL 后端的纹理/FBO/影子按 id 存放，
  不释放的话下一个占用同一 id 的 surface 会从上一个的像素开始、
  还会把陈旧 tile 判成「没变」。
- **两个后端各自的 frame_budget**：`tests/run_frame_budget.cmake` 接受
  `-DRENDER_BACKEND=0|1|2`（写进 fixture profile 的 `renderBackend`）与
  `-DBUDGET_FILE=`，CPU 读 `tests/perf/frame_budget.txt`，GL 读
  `tests/perf/frame_budget_gl.txt`；`perf_frame_budget_gl` 已注册。
  GL 预算文件还不存在——GPU 路径的像素计数器几乎归零、`render_ms` 量的是
  上传/批次/交换，数值只能在真 GPU 上测，所以现在跑到那个测试是 skip
  （`FRAME_BUDGET_UNCALIBRATED`）而不是拿编出来的上限去卡。
  校准命令（本机、有 ZanIDE.exe 的构建目录）：

```powershell
cmake -DROOT=<repo> -DBINARY_DIR=<build> -DZANIDE=<build>\ZanIDE.exe `
      -DRENDER_BACKEND=1 -DCALIBRATE=1 -P <repo>\tests\run_frame_budget.cmake
```

  输出的 `FRAME_BUDGET_MEASURED` 段就是预算文件语法，加上余量存成
  `tests/perf/frame_budget_gl.txt` 即可。

## 7. 工作量与顺序

| 步 | 内容 | 估计 | 可单独上线 |
|---|---|---|---|
| 1 | 后端 vtable + CPU 后端搬家 | 1 会话 | 是（画面不变） |
| 2 | 字形图集 + glyph run | 1 会话 | 是 |
| 3 | GL 后端（上下文 + SDF + FBO 模糊 + 图集） | 2 会话 | 是（Auto 回退兜底） |
| 4 | 呈现路径（含分层窗口） | 0.5 会话 | 是 |

## 8. 顺带可做的依赖清理

`ZAN_GUI_SDL`（可选的 SDL3 统一外壳）在原生 Win32/X11/Cocoa 外壳都齐全的前提下是重复实现。
若确认产品不需要「IDE 窗口与游戏共用 SDL 外壳」，可删除 `gui_runtime_sdl.c` 与该 CMake 选项 ——
这与 `stdlib/SDL3`（给用户游戏工程用的模块）是两件事，后者删除意味着放弃游戏目标支持。
