# System.Media

> 源码: `packages/Zan.Desktop/src/System/Media/Audio.zan`


## Audio (class)

- static bool Open()

- static bool IsOpen()

- static void SetVolume(double volume)

- static double Volume()

- static string DriverName()

- static int ActiveVoices()

- static void StopAll()

- static void Close()

- static string LastError()


## AudioClip (class)

- nint handle;

- static AudioClip LoadWav(string path)

- static AudioClip LoadOgg(string path)

- static AudioClip LoadWavFromMem(string data, int len)

- static AudioClip LoadOggFromMem(string data, int len)

- bool IsValid()

- int Frequency()

- int Channels()

- int DurationMs()

- AudioVoice Play()

- AudioVoice Play(double gain, int loop)

- AudioVoice PlayLooping(double gain)

- void Close()

- void Dispose()


## AudioNative (class)

- [DllImport("zan_audio")]static extern int zan_audio_open();

- [DllImport("zan_audio")]static extern void zan_audio_close();

- [DllImport("zan_audio")]static extern int zan_audio_is_open();

- [DllImport("zan_audio")]static extern void zan_audio_set_volume(double volume);

- [DllImport("zan_audio")]static extern double zan_audio_volume();

- [DllImport("zan_audio")]static extern string zan_audio_driver_name();

- [DllImport("zan_audio")]static extern int zan_audio_active_voices();

- [DllImport("zan_audio")]static extern void zan_audio_stop_all();

- [DllImport("zan_audio")]static extern string zan_audio_last_error();

- [DllImport("zan_audio")]static extern nint zan_audio_load_wav(string path);

- [DllImport("zan_audio")]static extern nint zan_audio_load_ogg(string path);

- [DllImport("zan_audio")]static extern nint zan_audio_load_wav_mem(string data, int len);

- [DllImport("zan_audio")]static extern nint zan_audio_load_ogg_mem(string data, int len);

- [DllImport("zan_audio")]static extern void zan_audio_free_clip(nint clip);

- [DllImport("zan_audio")]static extern int zan_audio_clip_frequency(nint clip);

- [DllImport("zan_audio")]static extern int zan_audio_clip_channels(nint clip);

- [DllImport("zan_audio")]static extern int zan_audio_clip_duration_ms(nint clip);

- [DllImport("zan_audio")]static extern long zan_audio_play(nint clip, double gain, int loop);

- [DllImport("zan_audio")]static extern int zan_audio_voice_playing(long voice);

- [DllImport("zan_audio")]static extern void zan_audio_voice_stop(long voice);

- [DllImport("zan_audio")]static extern void zan_audio_voice_set_gain(long voice, double gain);


## AudioVoice (class)

- long handle;

- static AudioVoice Of(long handle)

- bool IsValid()

- bool IsPlaying()

- void SetGain(double gain)

- void Stop()
