# SDL3

> 源码: `stdlib/SDL3/Audio.zan`, `stdlib/SDL3/Core.zan`, `stdlib/SDL3/Event.zan`, `stdlib/SDL3/Gamepad.zan`, `stdlib/SDL3/Gpu.zan`, `stdlib/SDL3/Native.zan`, `stdlib/SDL3/Renderer.zan`, `stdlib/SDL3/Texture.zan`, `stdlib/SDL3/Touch.zan`, `stdlib/SDL3/Window.zan`


## Sdl (class)

SDL3 进程生命周期与计时。

- static bool Init(int flags)

- static void Quit()

- static string Error()

- static int Version()
  - 返回 SDL 的打包数值版本，例如 3004012。

- static int Ticks()

- static void Delay(int milliseconds)


## SdlAudio (class)

音频设备：一次打开，之后所有声音都混到这一个设备上。

`SdlAudioClip` 是解码好的 WAV 采样，
`SdlVoice` 是它的一次播放；同一个 clip 可以同时起多个
voice（叠加音效），混音由 SDL 完成。

- static bool Open()
  - 打开默认播放设备（已打开时直接返回 true）。

- static bool IsOpen()

- static void SetVolume(double volume)
  - 主音量（0..1，可放大到 1 以上）。对已经在响的声音同样
    生效，所以静音/淡出立即听得到。

- static double Volume()

- static string DriverName()
  - SDL 选中的音频驱动名（wasapi、pulseaudio、dummy…）。

- static int ActiveVoices()
  - 还在响的声音数：一次性音效播完即回收，不用调用方登记。

- static void StopAll()
  - 立刻停掉所有声音（切场景/退出时用）。

- static void Close()
  - 关闭设备，并停掉设备上剩下的声音。


## SdlAudioClip (class)

加载到内存的 WAV 采样。`Play` 每次返回一个新的
`SdlVoice`，所以同一个 clip 可以叠着响。

- nint handle;

- static SdlAudioClip LoadWav(string path)
  - 加载 WAV 文件。失败时返回的对象 IsValid() 为 false
    （原因见 <c>Sdl.Error()</c>），不返回 null。

- bool IsValid()

- int Frequency()
  - 采样率（Hz）。

- int Channels()

- int DurationMs()

- SdlVoice Play()
  - 用默认音量播一次。

- SdlVoice Play(double gain, int loop)
  - 播一次。<paramref name="gain"/> 是这一个声音的音量，
    <paramref name="loop"/> 非 0 表示循环（背景音乐）。

- SdlVoice PlayLooping(double gain)
  - 循环播放，直到 `SdlVoice.Stop`。

- void Close()
  - 释放采样，并停掉还在读它的声音。


## SdlEvent (class)

访问最近轮询到的 SDL 事件。
在主线程上轮询并读取事件。

- static bool Poll()

- static int Type()

- static int Timestamp()

- static int WindowId()

- static int Data1()

- static int Data2()

- static int Scancode()

- static int Keycode()

- static int Keymod()

- static bool IsRepeat()

- static double MouseX()

- static double MouseY()

- static double MouseDeltaX()

- static double MouseDeltaY()

- static int MouseButton()

- static int MouseClicks()

- static string Text()

- static int GamepadWhich()
  - 手柄实例 id：ADDED/REMOVED/按键/轴事件都用它区分手柄。

- static int GamepadButton()

- static int GamepadAxis()

- static int GamepadAxisValue()
  - 轴原始值：摇杆 -32768..32767，扳机 0..32767。

- static long FingerId()
  - 手指 id：多点触控靠它把 DOWN/MOTION/UP 串成一根手指。

- static long TouchDevice()

- static double FingerX()
  - 手指位置，窗口内归一化 0..1（乘窗口尺寸得像素）。

- static double FingerY()

- static double FingerDeltaX()

- static double FingerDeltaY()

- static double FingerPressure()
  - 压感 0..1；不支持压感的设备恒为 1。

- static bool IsQuit()


## SdlEventType (class)

- static int Quit()

- static int WindowShown()

- static int WindowHidden()

- static int WindowMoved()

- static int WindowResized()

- static int WindowPixelSizeChanged()

- static int WindowMinimized()

- static int WindowMaximized()

- static int WindowRestored()

- static int WindowFocusGained()

- static int WindowFocusLost()

- static int WindowCloseRequested()

- static int KeyDown()

- static int KeyUp()

- static int TextEditing()

- static int TextInput()

- static int MouseMotion()

- static int MouseButtonDown()

- static int MouseButtonUp()

- static int MouseWheel()

- static int GamepadAxisMotion()

- static int GamepadButtonDown()

- static int GamepadButtonUp()

- static int GamepadAdded()

- static int GamepadRemoved()

- static int GamepadRemapped()

- static int FingerDown()

- static int FingerUp()

- static int FingerMotion()

- static int FingerCanceled()


## SdlGamepad (class)

已打开的手柄。

工厂方法永远返回对象：没插手柄时 `IsOpen` 为 false，
读按键/轴返回 0，因此手柄热插拔的代码不需要判空。

- nint handle;

- static bool Init()
  - 初始化手柄子系统（Sdl.Init 已带 SdlInit.Gamepad() 时可省）。

- static int Count()
  - 当前接入的手柄数量。

- static int IdAt(int index)
  - 第 <paramref name="index"/> 个手柄的实例 id（事件里的
    which），越界返回 0。

- static SdlGamepad Open(int which)
  - 按实例 id 打开手柄（GAMEPAD_ADDED 事件的 which）。

- static SdlGamepad OpenFirst()
  - 打开第一个手柄；没有手柄时返回未打开的对象。

- bool IsOpen()

- string Name()
  - 手柄名（未打开时为空串）。

- int Id()
  - 实例 id，和事件里的 which 对得上。

- bool IsDown(int button)

- int Axis(int axis)
  - 轴原始值：摇杆 -32768..32767，扳机 0..32767。

- double AxisNormalized(int axis)
  - 轴归一化到 -1..1（扳机为 0..1）。

- bool Rumble(int low, int high, int milliseconds)
  - 震动。<paramref name="low"/>/<paramref name="high"/> 是
    低频/高频马达强度（0..65535），持续 <paramref name="milliseconds"/>。

- void Close()


## SdlGamepadAxis (class)

手柄轴编号。

- static int LeftX()

- static int LeftY()

- static int RightX()

- static int RightY()

- static int LeftTrigger()

- static int RightTrigger()


## SdlGamepadButton (class)

手柄按键编号（SDL 的位置命名：South 是 Xbox 的 A）。

- static int South()

- static int East()

- static int West()

- static int North()

- static int Back()

- static int Guide()

- static int Start()

- static int LeftStick()

- static int RightStick()

- static int LeftShoulder()

- static int RightShoulder()

- static int DpadUp()

- static int DpadDown()

- static int DpadLeft()

- static int DpadRight()


## SdlGpu (class)

SDL_GPU 精灵渲染器：获取交换链（或离屏目标），
每帧批量绘制带纹理四边形并提交到 GPU。后端
（macOS 为 Metal、Linux 为 Vulkan、Windows 为 D3D12）由 SDL 选择。

- nint handle;

- List<SdlGpuTexture> textures;

- static SdlGpu Create(SdlWindow window)
  - 创建向 <paramref name="window"/> 呈现的 GPU 上下文。

- static SdlGpu CreateOffscreen(int width, int height)
  - 创建无窗口上下文，渲染到 width x height
    目标，可通过 `ReadPixel` 读取。适用于无头测试。

- bool IsValid()

- nint Handle()

- void Track(SdlGpuTexture tex)
  - 在上下文有效时记录子纹理，便于
    `Destroy` 按顺序释放。

- void Untrack(SdlGpuTexture tex)

- SdlGpuTexture MakeTexture()

- SdlGpuTexture LoadTexture(int width, int height, string pixels)
  - 以紧凑的 RGBA32 像素上传为采样纹理。

- SdlGpuTexture SolidTexture(int width, int height)
  - 创建纯白纹理；通过绘制颜色着色。

- SdlGpuTexture LoadBmp(string path)
  - 加载 BMP 文件并转换为 RGBA32。

- SdlGpuTexture LoadImage(string path)
  - 将 PNG/JPEG 图像数据加载为 RGBA32 采样纹理。

- string DriverName()
  - SDL 选中的 GPU 后端名（metal、vulkan、direct3d12…）。

- string ShaderFormat()
  - 该后端吃的着色器格式（msl、spirv、dxil…）。

- bool Begin(int red, int green, int blue, int alpha)
  - 开始一帧并设置清除颜色（每通道 0-255）。

- void Draw(SdlGpuTexture texture, int x, int y, int width, int height, int red, int green, int blue, int alpha)
  - 将整个纹理绘制到目标矩形中，并着色。

- void DrawRegion(SdlGpuTexture texture, int dstX, int dstY, int dstW, int dstH, int srcX, int srcY, int srcW, int srcH, int red, int green, int blue, int alpha)
  - 将源区域（纹理像素）绘制到目标矩形，
    并着色。源宽/高为 0 表示整个纹理。

- void End()
  - 将批处理的一帧提交到 GPU。

- int ReadPixel(int x, int y)
  - 从最后的离屏帧读取一个像素，打包为 0xRRGGBBAA。

- void Destroy()


## SdlGpuNative (class)

SDL_GPU 精灵管线的 ABI 安全原生桥接（Metal/Vulkan/D3D12）。

句柄为指针大小（<c>nint</c>），坐标是 32 位，
与 `SdlNative` 的 C ABI 匹配；像素缓冲区为原始字节字符串。

- [DllImport("zan_sdl3")]static extern nint zan_gpu_create(nint window);

- [DllImport("zan_sdl3")]static extern nint zan_gpu_create_offscreen(int width, int height);

- [DllImport("zan_sdl3")]static extern void zan_gpu_destroy(nint handle);

- [DllImport("zan_sdl3")]static extern nint zan_gpu_load_texture(nint handle, int width, int height, string pixels);

- [DllImport("zan_sdl3")]static extern nint zan_gpu_solid_texture(nint handle, int width, int height);

- [DllImport("zan_sdl3")]static extern nint zan_gpu_load_bmp(nint handle, string path);

- [DllImport("zan_sdl3")]static extern nint zan_gpu_load_image(nint handle, string path);

- [DllImport("zan_sdl3")]static extern void zan_gpu_free_texture(nint handle, nint texture);

- [DllImport("zan_sdl3")]static extern int zan_gpu_texture_width(nint texture);

- [DllImport("zan_sdl3")]static extern int zan_gpu_texture_height(nint texture);

- [DllImport("zan_sdl3")]static extern int zan_gpu_begin(nint handle, int red, int green, int blue, int alpha);

- [DllImport("zan_sdl3")]static extern void zan_gpu_draw(nint handle, nint texture, int dstX, int dstY, int dstW, int dstH, int srcX, int srcY, int srcW, int srcH, int red, int green, int blue, int alpha);

- [DllImport("zan_sdl3")]static extern void zan_gpu_end(nint handle);

- [DllImport("zan_sdl3")]static extern int zan_gpu_read_pixel(nint handle, int x, int y);

- [DllImport("zan_sdl3")]static extern string zan_gpu_driver_name(nint handle);

- [DllImport("zan_sdl3")]static extern string zan_gpu_shader_format(nint handle);


## SdlGpuTexture (class)

由 `SdlGpu` 上下文拥有的 GPU 纹理。

- nint context;

- nint handle;

- SdlGpu owner;

- bool IsValid()

- nint Handle()

- int Width()

- int Height()

- void Free()


## SdlInit (class)

- static int Timer()

- static int Audio()

- static int Video()

- static int Joystick()

- static int Haptic()

- static int Gamepad()

- static int Events()

- static int Sensor()

- static int Camera()

- static int Game()


## SdlKeymod (class)

- static int LeftShift()

- static int RightShift()

- static int LeftControl()

- static int RightControl()

- static int LeftAlt()

- static int RightAlt()

- static bool HasShift(int modifiers)

- static bool HasControl(int modifiers)

- static bool HasAlt(int modifiers)


## SdlLogicalPresentation (class)

- static int Disabled()

- static int Stretch()

- static int Letterbox()

- static int Overscan()

- static int IntegerScale()


## SdlMouseButton (class)

- static int Left()

- static int Middle()

- static int Right()

- static int Side1()

- static int Side2()


## SdlNative (class)

SDL3 的 ABI 安全原生桥接。

SDL 对象是指针，句柄以 <c>nint</c> 跨边界传递；
坐标、颜色和标志是 32 位 <c>int</c>，SDL_Event 是
平台 ABI 联合体，桥接将其展平为稳定的标量访问器。

- [DllImport("zan_sdl3")]static extern int zan_sdl_init(int flags);

- [DllImport("zan_sdl3")]static extern void zan_sdl_quit();

- [DllImport("zan_sdl3")]static extern string zan_sdl_get_error();

- [DllImport("zan_sdl3")]static extern int zan_sdl_version();

- [DllImport("zan_sdl3")]static extern long zan_sdl_ticks();

- [DllImport("zan_sdl3")]static extern void zan_sdl_delay(int milliseconds);

- [DllImport("zan_sdl3")]static extern nint zan_sdl_create_window(string title, int width, int height, int flags);

- [DllImport("zan_sdl3")]static extern void zan_sdl_destroy_window(nint window);

- [DllImport("zan_sdl3")]static extern int zan_sdl_window_id(nint window);

- [DllImport("zan_sdl3")]static extern int zan_sdl_window_width(nint window);

- [DllImport("zan_sdl3")]static extern int zan_sdl_window_height(nint window);

- [DllImport("zan_sdl3")]static extern int zan_sdl_set_window_title(nint window, string title);

- [DllImport("zan_sdl3")]static extern int zan_sdl_set_window_fullscreen(nint window, int enabled);

- [DllImport("zan_sdl3")]static extern int zan_sdl_show_window(nint window);

- [DllImport("zan_sdl3")]static extern int zan_sdl_hide_window(nint window);

- [DllImport("zan_sdl3")]static extern nint zan_sdl_create_renderer(nint window, string name);

- [DllImport("zan_sdl3")]static extern void zan_sdl_destroy_renderer(nint renderer);

- [DllImport("zan_sdl3")]static extern string zan_sdl_renderer_name(nint renderer);

- [DllImport("zan_sdl3")]static extern int zan_sdl_set_render_vsync(nint renderer, int enabled);

- [DllImport("zan_sdl3")]static extern int zan_sdl_set_logical_size(nint renderer, int width, int height, int mode);

- [DllImport("zan_sdl3")]static extern double zan_sdl_window_to_render_x(nint renderer, double windowX, double windowY);

- [DllImport("zan_sdl3")]static extern double zan_sdl_window_to_render_y(nint renderer, double windowX, double windowY);

- [DllImport("zan_sdl3")]static extern int zan_sdl_set_draw_color(nint renderer, int red, int green, int blue, int alpha);

- [DllImport("zan_sdl3")]static extern int zan_sdl_clear(nint renderer);

- [DllImport("zan_sdl3")]static extern int zan_sdl_present(nint renderer);

- [DllImport("zan_sdl3")]static extern int zan_sdl_read_pixels(nint renderer, string rgbaOut, int width, int height);

- [DllImport("zan_sdl3")]static extern int zan_sdl_render_output_size(nint renderer);

- [DllImport("zan_sdl3")]static extern int zan_sdl_draw_point(nint renderer, int x, int y);

- [DllImport("zan_sdl3")]static extern int zan_sdl_draw_line(nint renderer, int x1, int y1, int x2, int y2);

- [DllImport("zan_sdl3")]static extern int zan_sdl_draw_rect(nint renderer, int x, int y, int width, int height);

- [DllImport("zan_sdl3")]static extern int zan_sdl_fill_rect(nint renderer, int x, int y, int width, int height);

- [DllImport("zan_sdl3")]static extern nint zan_sdl_create_texture_rgba32(nint renderer, int width, int height, int streaming);

- [DllImport("zan_sdl3")]static extern nint zan_sdl_load_bmp_texture(nint renderer, string path);

- [DllImport("zan_sdl3")]static extern nint zan_sdl_load_bmp_texture_colorkey(nint renderer, string path, int red, int green, int blue);

- [DllImport("zan_sdl3")]static extern nint zan_sdl_load_image_texture(nint renderer, string path);

- [DllImport("zan_sdl3")]static extern void zan_sdl_destroy_texture(nint texture);

- [DllImport("zan_sdl3")]static extern int zan_sdl_update_texture(nint texture, string pixels, int pitch);

- [DllImport("zan_sdl3")]static extern int zan_sdl_set_texture_nearest(nint texture, int nearest);

- [DllImport("zan_sdl3")]static extern int zan_sdl_render_texture(nint renderer, nint texture, int x, int y, int width, int height);

- [DllImport("zan_sdl3")]static extern nint zan_sdl_create_target_texture(nint renderer, int width, int height);

- [DllImport("zan_sdl3")]static extern int zan_sdl_set_render_target(nint renderer, nint texture);

- [DllImport("zan_sdl3")]static extern int zan_sdl_render_texture_region(nint renderer, nint texture, int sx, int sy, int sw, int sh, int dx, int dy, int dw, int dh);

- [DllImport("zan_sdl3")]static extern int zan_sdl_set_texture_alpha(nint texture, int alpha);

- [DllImport("zan_sdl3")]static extern int zan_sdl_set_texture_color(nint texture, int red, int green, int blue);

- [DllImport("zan_sdl3")]static extern int zan_sdl_set_texture_blend(nint texture, int mode);

- [DllImport("zan_sdl3")]static extern int zan_sdl_set_draw_blend(nint renderer, int mode);

- [DllImport("zan_sdl3")]static extern int zan_sdl_set_render_clip(nint renderer, int x, int y, int width, int height);

- [DllImport("zan_sdl3")]static extern int zan_sdl_clear_render_clip(nint renderer);

- [DllImport("zan_sdl3")]static extern int zan_sdl_poll_event();

- [DllImport("zan_sdl3")]static extern int zan_sdl_event_type();

- [DllImport("zan_sdl3")]static extern long zan_sdl_event_timestamp();

- [DllImport("zan_sdl3")]static extern int zan_sdl_event_window_id();

- [DllImport("zan_sdl3")]static extern int zan_sdl_event_data1();

- [DllImport("zan_sdl3")]static extern int zan_sdl_event_data2();

- [DllImport("zan_sdl3")]static extern int zan_sdl_event_scancode();

- [DllImport("zan_sdl3")]static extern int zan_sdl_event_keycode();

- [DllImport("zan_sdl3")]static extern int zan_sdl_event_keymod();

- [DllImport("zan_sdl3")]static extern int zan_sdl_event_repeat();

- [DllImport("zan_sdl3")]static extern double zan_sdl_event_mouse_x();

- [DllImport("zan_sdl3")]static extern double zan_sdl_event_mouse_y();

- [DllImport("zan_sdl3")]static extern double zan_sdl_event_mouse_dx();

- [DllImport("zan_sdl3")]static extern double zan_sdl_event_mouse_dy();

- [DllImport("zan_sdl3")]static extern int zan_sdl_event_mouse_button();

- [DllImport("zan_sdl3")]static extern int zan_sdl_event_mouse_clicks();

- [DllImport("zan_sdl3")]static extern string zan_sdl_event_text();

- [DllImport("zan_sdl3")]static extern int zan_sdl_key_down(int scancode);

- [DllImport("zan_sdl3")]static extern int zan_audio_open();

- [DllImport("zan_sdl3")]static extern void zan_audio_close();

- [DllImport("zan_sdl3")]static extern int zan_audio_is_open();

- [DllImport("zan_sdl3")]static extern void zan_audio_set_volume(double volume);

- [DllImport("zan_sdl3")]static extern double zan_audio_volume();

- [DllImport("zan_sdl3")]static extern string zan_audio_driver_name();

- [DllImport("zan_sdl3")]static extern int zan_audio_active_voices();

- [DllImport("zan_sdl3")]static extern nint zan_audio_load_wav(string path);

- [DllImport("zan_sdl3")]static extern void zan_audio_free_clip(nint clip);

- [DllImport("zan_sdl3")]static extern int zan_audio_clip_frequency(nint clip);

- [DllImport("zan_sdl3")]static extern int zan_audio_clip_channels(nint clip);

- [DllImport("zan_sdl3")]static extern int zan_audio_clip_duration_ms(nint clip);

- [DllImport("zan_sdl3")]static extern long zan_audio_play(nint clip, double gain, int loop);

- [DllImport("zan_sdl3")]static extern int zan_audio_voice_playing(long voice);

- [DllImport("zan_sdl3")]static extern void zan_audio_voice_stop(long voice);

- [DllImport("zan_sdl3")]static extern void zan_audio_voice_set_gain(long voice, double gain);

- [DllImport("zan_sdl3")]static extern void zan_audio_stop_all();

- [DllImport("zan_sdl3")]static extern int zan_gamepad_init();

- [DllImport("zan_sdl3")]static extern int zan_gamepad_count();

- [DllImport("zan_sdl3")]static extern int zan_gamepad_id_at(int index);

- [DllImport("zan_sdl3")]static extern nint zan_gamepad_open(int which);

- [DllImport("zan_sdl3")]static extern nint zan_gamepad_open_first();

- [DllImport("zan_sdl3")]static extern void zan_gamepad_close(nint pad);

- [DllImport("zan_sdl3")]static extern string zan_gamepad_name(nint pad);

- [DllImport("zan_sdl3")]static extern int zan_gamepad_id(nint pad);

- [DllImport("zan_sdl3")]static extern int zan_gamepad_button(nint pad, int button);

- [DllImport("zan_sdl3")]static extern int zan_gamepad_axis(nint pad, int axis);

- [DllImport("zan_sdl3")]static extern int zan_gamepad_rumble(nint pad, int low, int high, int milliseconds);

- [DllImport("zan_sdl3")]static extern int zan_sdl_event_gamepad_which();

- [DllImport("zan_sdl3")]static extern int zan_sdl_event_gamepad_button();

- [DllImport("zan_sdl3")]static extern int zan_sdl_event_gamepad_axis();

- [DllImport("zan_sdl3")]static extern int zan_sdl_event_gamepad_axis_value();

- [DllImport("zan_sdl3")]static extern int zan_touch_device_count();

- [DllImport("zan_sdl3")]static extern long zan_touch_device_at(int index);

- [DllImport("zan_sdl3")]static extern string zan_touch_device_name(long device);

- [DllImport("zan_sdl3")]static extern long zan_sdl_event_finger_id();

- [DllImport("zan_sdl3")]static extern long zan_sdl_event_touch_device();

- [DllImport("zan_sdl3")]static extern double zan_sdl_event_finger_x();

- [DllImport("zan_sdl3")]static extern double zan_sdl_event_finger_y();

- [DllImport("zan_sdl3")]static extern double zan_sdl_event_finger_dx();

- [DllImport("zan_sdl3")]static extern double zan_sdl_event_finger_dy();

- [DllImport("zan_sdl3")]static extern double zan_sdl_event_finger_pressure();


## SdlRenderer (class)

持有 SDL_Renderer 句柄，带整数坐标辅助方法。

- nint handle;

- int sclN;

- int sclD;

- static SdlRenderer Create(SdlWindow window)

- static SdlRenderer CreateNamed(SdlWindow window, string driverName)

- void SetUiScale(int num, int den)

- int ScaleNum()

- int ScaleDen()

- int Sc(int v)

- bool IsValid()

- nint Handle()

- string Name()

- bool SetVSync(bool enabled)

- bool SetLogicalSize(int width, int height, int mode)

- int LogicalX(double windowX, double windowY)

- int LogicalY(double windowX, double windowY)

- bool SetColor(int red, int green, int blue, int alpha)

- bool SetClip(int x, int y, int width, int height)

- bool ResetClip()

- bool Clear()

- bool Present()

- bool ReadPixels(string rgbaOut, int width, int height)
  - 将渲染器后备缓冲读回 byte[] RGBA 缓冲区，
    大小为 width*height*4 字节。成功返回 true。

- int OutputSize()
  - 渲染器当前的输出尺寸（像素），打包为
    (height << 16) | width。

- bool DrawPoint(int x, int y)

- bool DrawLine(int x1, int y1, int x2, int y2)

- bool DrawRect(int x, int y, int width, int height)

- bool FillRect(int x, int y, int width, int height)

- bool FillRectRaw(int x, int y, int width, int height)

- bool DrawTexture(SdlTexture texture, int x, int y, int width, int height)

- bool DrawTextureRegion(SdlTexture texture, int sourceX, int sourceY, int sourceWidth, int sourceHeight, int x, int y, int width, int height)

- bool DrawTextureRotated(SdlTexture texture, int x, int y, int width, int height, int angle)

- bool SetTarget(SdlTexture texture)

- bool ResetTarget()

- bool SetBlend(int mode)

- bool ClearClip()

- void Close()


## SdlScancode (class)

- static int A()

- static int C()

- static int D()

- static int E()

- static int I()

- static int K()

- static int Q()

- static int S()

- static int W()

- static int Enter()

- static int Escape()

- static int Space()

- static int F1()

- static int F5()

- static int F11()

- static int Delete()

- static int Right()

- static int Left()

- static int Down()

- static int Up()

- static bool IsDown(int scancode)


## SdlTexture (class)

持有 SDL_Texture 句柄。

- nint handle;

- static SdlTexture CreateRgba32(SdlRenderer renderer, int width, int height, bool streaming)

- static SdlTexture LoadBmp(SdlRenderer renderer, string path)

- static SdlTexture LoadImage(SdlRenderer renderer, string path)
  - 将 PNG、JPEG 或其他 stb_image 支持的格式加载为 RGBA32。
    保留 PNG 的 Alpha 并启用 alpha 混合。

- static SdlTexture LoadBmpColorKey(SdlRenderer renderer, string path, int red, int green, int blue)
  - 加载 RGB BMP，并将一种颜色视为透明。

- static SdlTexture CreateTarget(SdlRenderer renderer, int width, int height)
  - 创建离屏渲染目标纹理（线性过滤），
    渲染器可通过 `SdlRenderer.SetTarget` 绘制到其中。
    这是降采样模糊和缓存背景的基础。

- bool IsValid()

- nint Handle()

- bool Update(string rgbaPixels, int pitch)

- bool SetNearest(bool nearest)

- bool SetBlendMode(int mode)
  - 设置 SDL 纹理混合模式。模式 1 为 alpha 混合。

- bool SetColor(int red, int green, int blue)

- bool ResetColor()

- bool SetOpacity(int alpha)

- void Close()


## SdlTouch (class)

触摸设备。手指事件本身从 `SdlEvent` 上读
（FingerId / FingerX …），这里只做设备枚举。

- static int DeviceCount()
  - 接入的触摸设备数量（桌面上通常为 0）。

- static long DeviceAt(int index)
  - 第 <paramref name="index"/> 个触摸设备 id，越界返回 0。

- static string DeviceName(long device)


## SdlVoice (class)

一次播放（voice）。

句柄带世代号：声音播完后句柄失效，`IsPlaying` 老实返回
false、`Stop` 什么也不做，因此一个存活时间比声音长的
SdlVoice 变量是安全的，不会碰到被回收的槽位。

- long handle;

- static SdlVoice Of(long handle)
  - 包装一个原生 voice 句柄；0 表示没起来的声音，此时对象
    依然可用（IsPlaying 为 false），调用方不需要判空。

- bool IsValid()
  - 声音是否成功起播（设备没开、voice 池满时为 false）。

- bool IsPlaying()

- void SetGain(double gain)
  - 这一个声音的音量（会再乘上主音量）。

- void Stop()


## SdlWindow (class)

持有 SDL_Window 句柄。

- nint handle;

- static SdlWindow Create(string title, int width, int height, int flags)

- bool IsValid()

- nint Handle()

- int Id()

- int Width()

- int Height()

- bool SetTitle(string title)

- bool SetFullscreen(bool enabled)

- bool Show()

- bool Hide()

- void Close()


## SdlWindowFlags (class)

- static int Fullscreen()

- static int OpenGL()

- static int Hidden()

- static int Borderless()

- static int Resizable()

- static int Minimized()

- static int Maximized()

- static int MouseGrabbed()

- static int HighPixelDensity()

- static int AlwaysOnTop()

- static int Vulkan()
