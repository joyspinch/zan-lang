# System.Media

> 源码: `packages/Zan.Desktop/src/System/Media/Audio.zan`


## Audio (class)

原生音频设备：一次打开，之后所有声音都混到这一个设备上。

零依赖原生实现（WASAPI 先行），取代原 SDL3 的音频桥。
`AudioClip` 是解码好的采样（WAV/OGG），
`AudioVoice` 是它的一次播放；同一个 clip 可以同时起多个
voice（叠加音效），混音由运行时后台线程完成。

- static bool Open()
  - 打开默认播放设备（已打开时直接返回 true）。

- static bool IsOpen()
  - 播放设备已打开时为真。

- static void SetVolume(double volume)
  - 主音量（0..1，可放大到 1 以上）。对已经在响的声音同样
    生效，所以静音/淡出立即听得到。

- static double Volume()
  - 当前主音量（0..1）。

- static string DriverName()
  - 原生音频后端名（Windows: "wasapi", Android: "aaudio", Linux: "alsa",
    macOS: "coreaudio", OpenHarmony: "ohaudio"；未打开或不支持返回空串）。

- static int ActiveVoices()
  - 还在响的声音数：一次性音效播完即回收，不用调用方登记。

- static void StopAll()
  - 立刻停掉所有声音（切场景/退出时用）。

- static void Close()
  - 关闭设备，并停掉设备上剩下的声音。

- static string LastError()
  - 最近一次失败的原因（设备打开失败/解码失败等）；
    没有失败记录时为空串。


## AudioClip (class)

加载到内存的采样（s16 PCM）。`Play` 每次返回
一个新的 `AudioVoice`，所以同一个 clip 可以叠着响。

- nint handle;

- static AudioClip LoadWav(string path)
  - 加载 WAV 文件（PCM 8/16/24/32 位与 32 位浮点，含
    WAVE_FORMAT_EXTENSIBLE）。失败时返回的对象 IsValid() 为 false
    （原因见 <c>Audio.LastError()</c>），不返回 null。

- static AudioClip LoadOgg(string path)
  - 加载 OGG Vorbis 文件（背景音乐）。失败时返回的对象
    IsValid() 为 false，不返回 null。

- static AudioClip LoadWavFromMem(string data, int len)
  - 从内存字节解析 WAV。<paramref name="data"/> 是完整的
    WAV 文件字节（如从加密资源包解密出来的内容），全程不落盘。
    字节只在本次调用内同步读取（PCM 会被拷出），返回后即可释放
    缓冲区，不转移所有权。失败时返回的对象 IsValid() 为 false
    （原因见 <c>Audio.LastError()</c>），不返回 null。

- static AudioClip LoadOggFromMem(string data, int len)
  - 从内存字节解码 OGG Vorbis（背景音乐）。字节只在本次
    调用内同步读取（PCM 会被拷出），返回后即可释放缓冲区。
    失败时返回的对象 IsValid() 为 false。

- bool IsValid()
  - 加载成功时为真（失败时为假对象，不返回 null）。

- int Frequency()
  - 采样率（Hz）。

- int Channels()
  - 声道数（1 单声道、2 立体声）。

- int DurationMs()
  - 时长（毫秒）。

- AudioVoice Play()
  - 用默认音量播一次。

- AudioVoice Play(double gain, int loop)
  - 播一次。<paramref name="gain"/> 是这一个声音的音量，
    <paramref name="loop"/> 非 0 表示循环（背景音乐）。

- AudioVoice PlayLooping(double gain)
  - 循环播放，直到 `AudioVoice.Stop`。

- void Close()
  - 释放采样，并停掉还在读它的声音。

- void Dispose()
  - 释放采样资源（实现 IDisposable，等同于 Close）。


## AudioNative (class)

zan_audio 原生桥（音频随 zan_gui 运行时导出）。

- [DllImport("zan_gui")]static extern int zan_audio_open();

- [DllImport("zan_gui")]static extern void zan_audio_close();

- [DllImport("zan_gui")]static extern int zan_audio_is_open();

- [DllImport("zan_gui")]static extern void zan_audio_set_volume(double volume);

- [DllImport("zan_gui")]static extern double zan_audio_volume();

- [DllImport("zan_gui")]static extern string zan_audio_driver_name();

- [DllImport("zan_gui")]static extern int zan_audio_active_voices();

- [DllImport("zan_gui")]static extern void zan_audio_stop_all();

- [DllImport("zan_gui")]static extern string zan_audio_last_error();

- [DllImport("zan_gui")]static extern nint zan_audio_load_wav(string path);

- [DllImport("zan_gui")]static extern nint zan_audio_load_ogg(string path);

- [DllImport("zan_gui")]static extern nint zan_audio_load_wav_mem(string data, int len);

- [DllImport("zan_gui")]static extern nint zan_audio_load_ogg_mem(string data, int len);

- [DllImport("zan_gui")]static extern void zan_audio_free_clip(nint clip);

- [DllImport("zan_gui")]static extern int zan_audio_clip_frequency(nint clip);

- [DllImport("zan_gui")]static extern int zan_audio_clip_channels(nint clip);

- [DllImport("zan_gui")]static extern int zan_audio_clip_duration_ms(nint clip);

- [DllImport("zan_gui")]static extern long zan_audio_play(nint clip, double gain, int loop);

- [DllImport("zan_gui")]static extern int zan_audio_voice_playing(long voice);

- [DllImport("zan_gui")]static extern void zan_audio_voice_stop(long voice);

- [DllImport("zan_gui")]static extern void zan_audio_voice_set_gain(long voice, double gain);


## AudioVoice (class)

一次播放（voice）。

句柄带世代号：声音播完后句柄失效，`IsPlaying` 老实返回
false、`Stop` 什么也不做，因此一个存活时间比声音长的
AudioVoice 变量是安全的，不会碰到被回收的槽位。

- long handle;

- static AudioVoice Of(long handle)
  - 包装一个原生 voice 句柄；0 表示没起来的声音，此时对象
    依然可用（IsPlaying 为 false），调用方不需要判空。

- bool IsValid()
  - 声音是否成功起播（设备没开、voice 池满时为 false）。

- bool IsPlaying()
  - 声音仍在响时为真；播完或已 Stop 即 false（句柄按世代号失效）。

- void SetGain(double gain)
  - 这一个声音的音量（会再乘上主音量）。

- void Stop()
  - 停掉这一个声音并使句柄失效；对已失效句柄无操作。
