# 音频示例

`audio_demo.zan` 演示 `SDL3` 模块的音频播放：

- `SdlAudio.Open()` / `SetVolume()` / `Close()`：打开默认播放设备与主音量，
  `DriverName()` 看 SDL 实际选中的后端；
- `SdlAudioClip.LoadWav(path)`：把 WAV 解码进内存，`Frequency()` /
  `Channels()` / `DurationMs()` 读取格式；
- `clip.Play()`、`clip.Play(volume, loops)`、`clip.PlayLooping(volume)`：
  同一个 clip 可以叠加播放，每次返回一个 `SdlVoice`；
- `voice.IsPlaying()` / `Stop()` / `SetPaused()` / `SetVolume()` /
  `SetLoops()`：单个声音的控制，`SetLoops(0)` 让循环中的背景音乐在本遍
  结束时自然收尾。

例子不带音频素材：它先合成一段 440 Hz 正弦音写成 `beep.wav`，再加载播放，
所以顺带也是一份最小的 WAV 写出代码。

在仓库根目录编译运行：

```powershell
build\zanc.exe examples\audio\audio_demo.zan --auto-stdlib -o build\audio_demo.exe
build\audio_demo.exe
```

输出形如：

```text
driver: wasapi
clip: 44100 Hz, 1 ch, 700 ms
played in 715 ms
voices: 3
voices after stop: 0
```

混音由 SDL 完成：每个声音是一条绑定到共享播放设备的 `SDL_AudioStream`，
循环续播在音频线程的回调里完成，因此主线程阻塞时播放也不会断。同时播放的
声音上限是 64 个，播完的声音会自动回收（`SdlAudio.ActiveVoices()` 会顺带
触发回收）。

没有可用音频设备时 `SdlAudio.Open()` 返回 false，原因见 `Sdl.Error()`；
可以设 `SDL_AUDIO_DRIVER=dummy` 在无声卡的机器上跑通流程。
