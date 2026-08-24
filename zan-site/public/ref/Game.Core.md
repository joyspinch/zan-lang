# Game.Core

> 源码: `stdlib/Game/Core/Anim.zan`, `stdlib/Game/Core/App.zan`


## AnimClip (class)

基于方向优先帧表的单个动画片段（
Mir 式布局：先是方向 0 的全部帧，再是方向 1，……）。
由 IDE 资源管理器写入的 `.anim` 旁置 JSON 加载。

- string image;

- int frameW;

- int frameH;

- int baseFrame;

- int dirs;

- int framesPerDir;

- int fps;

- int loopMode;

- int loopStart;

- AnimClip(int dirs, int framesPerDir, int fps, int loopMode, int loopStart)

- static AnimClip ParseJson(string json)
  - 解析 `.anim` JSON 文档；不是该格式则返回 null。

- static AnimClip Load(string path)
  - 从磁盘上的 `.anim` 文件加载片段（失败返回 null）。

- string Image()

- int Base()

- int FrameW()

- int FrameH()

- int Dirs()

- int FramesPerDir()

- int Fps()

- int LoopMode()

- int LoopStart()


## AnimPlayer (class)

播放 AnimClip：按流逝毫秒推进，可随时切换
朝向（帧位置保持不变，与经典 Mir 渲染器一致），
并读回帧表中要绘制的绝对帧。
循环模式：once（停在最后一帧，Done() 变为 true）、loop（回到
0）、section（先播放 0..loopStart-1 的前奏一次，再循环
loopStart..末尾——例如施法前摇后接持续施法）。

- AnimClip clip;

- int dir;

- int frame;

- int accMs;

- bool done;

- AnimPlayer()

- void Play(AnimClip c)
  - 从第 0 帧开始（或重新开始）播放片段，保持当前朝向。

- void SetDir(int d)
  - 切换朝向；片段内帧位置保持不变，
    转身的角色不会重新开始步伐。

- int Dir()

- bool Done()

- int Frame()

- void Update(int dtMs)
  - 按流逝毫秒推进。

- int SheetFrame()
  - 当前朝向与位置在帧表中的绝对帧索引
    ：base + dir * framesPerDir + frame。


## App (class)

Game.* 引擎的类型无关应用宿主：持有 SDL3 窗口和
一个 `SdlGpu` 渲染器，泵送事件队列并驱动简单的
帧循环。更上层的框架（Game.Arpg、Game.Zgm）构建于其上。

- SdlWindow window;

- SdlGpu gpu;

- int width;

- int height;

- bool running;

- App(string title, int width, int height)
  - 初始化 SDL，打开可调整大小的窗口和 GPU 上下文。

- bool IsValid()

- SdlGpu Gpu()

- SdlWindow Window()

- int Width()

- int Height()

- bool IsRunning()

- void Stop()

- void Pump()
  - 排空待处理事件；收到退出事件时请求关闭。

- int Ticks()

- void Delay(int milliseconds)

- void Shutdown()
