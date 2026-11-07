/* audio_api.c -- 平台音频生命周期管理与公开导出 API */

ZAN_AUDIO_EXPORT int32_t zan_audio_open(void) {
#if defined(_WIN32)
    HANDLE th;
    if (zan_audio_ready) return 1;
    if (!zan_audio_cs_ok) {
        InitializeCriticalSection(&zan_audio_cs);
        zan_audio_cs_ok = 1;
    }
    zan_audio_set_err(NULL);
    zan_audio_stop_evt = CreateEventW(NULL, TRUE, FALSE, NULL);
    zan_audio_fill_evt = CreateEventW(NULL, FALSE, FALSE, NULL);
    zan_audio_init_evt = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!zan_audio_stop_evt || !zan_audio_fill_evt || !zan_audio_init_evt) {
        zan_audio_set_err("CreateEvent failed");
        goto open_fail;
    }
    {
        int i;
        for (i = 0; i < ZAN_VOICE_SLOTS; i++) zan_voice_reset(&zan_voices[i]);
    }
    th = CreateThread(NULL, 0, zan_audio_thread_proc, NULL, 0, NULL);
    if (!th) {
        zan_audio_set_err("CreateThread failed");
        goto open_fail;
    }
    zan_audio_thread = th;
    if (WaitForSingleObject(zan_audio_init_evt, 10000) != WAIT_OBJECT_0
        || !zan_audio_client) {
        SetEvent(zan_audio_stop_evt);
        WaitForSingleObject(th, 3000);
        CloseHandle(th);
        zan_audio_thread = NULL;
        goto open_fail;
    }
    zan_audio_ready = 1;
    return 1;

open_fail:
    if (zan_audio_stop_evt) { CloseHandle(zan_audio_stop_evt); zan_audio_stop_evt = NULL; }
    if (zan_audio_fill_evt) { CloseHandle(zan_audio_fill_evt); zan_audio_fill_evt = NULL; }
    if (zan_audio_init_evt) { CloseHandle(zan_audio_init_evt); zan_audio_init_evt = NULL; }
    if (zan_audio_err[0] == 0) zan_audio_set_err("audio device open failed");
    return 0;
#elif defined(__ANDROID__)
    AAudioStream *s;
    if (zan_audio_ready) return 1;
    zan_audio_set_err(NULL);
    s = zan_audio_aa_open_stream();
    if (!s) {
        if (zan_audio_err[0] == 0) zan_audio_set_err("audio device open failed");
        return 0;
    }
    zan_audio_lock();
    for (int i = 0; i < ZAN_VOICE_SLOTS; i++) zan_voice_reset(&zan_voices[i]);
    zan_audio_dev_freq = AAudioStream_getSampleRate(s);
    zan_audio_dev_channels = AAudioStream_getChannelCount(s);
    if (zan_audio_dev_freq <= 0) zan_audio_dev_freq = 48000;
    if (zan_audio_dev_channels <= 0) zan_audio_dev_channels = 2;
    zan_audio_stream = s;
    zan_audio_ready = 1;
    zan_audio_unlock();
    if (AAudioStream_requestStart(s) != AAUDIO_OK) {
        zan_audio_lock();
        zan_audio_ready = 0;
        zan_audio_stream = NULL;
        zan_audio_unlock();
        AAudioStream_close(s);
        zan_audio_set_err("AAudio requestStart failed");
        return 0;
    }
    return 1;
#elif defined(__linux__)
    if (zan_audio_ready) return 1;
    zan_audio_set_err(NULL);
    if (!zan_alsa_start()) {
        if (zan_audio_err[0] == 0) zan_audio_set_err("ALSA device open failed");
        return 0;
    }
    zan_audio_lock();
    for (int i = 0; i < ZAN_VOICE_SLOTS; i++) zan_voice_reset(&zan_voices[i]);
    zan_audio_ready = 1;
    zan_audio_unlock();
    return 1;
#elif defined(__APPLE__)
    if (zan_audio_ready) return 1;
    zan_audio_set_err(NULL);
    if (!zan_coreaudio_start()) {
        if (zan_audio_err[0] == 0) zan_audio_set_err("CoreAudio device open failed");
        return 0;
    }
    zan_audio_lock();
    for (int i = 0; i < ZAN_VOICE_SLOTS; i++) zan_voice_reset(&zan_voices[i]);
    zan_audio_ready = 1;
    zan_audio_unlock();
    return 1;
#elif defined(__OHOS__)
    if (zan_audio_ready) return 1;
    zan_audio_set_err(NULL);
    if (!zan_ohaudio_start()) {
        if (zan_audio_err[0] == 0) zan_audio_set_err("OH Audio device open failed");
        return 0;
    }
    zan_audio_lock();
    for (int i = 0; i < ZAN_VOICE_SLOTS; i++) zan_voice_reset(&zan_voices[i]);
    zan_audio_ready = 1;
    zan_audio_unlock();
    return 1;
#else
    zan_audio_set_err("audio backend not available on this platform yet (planned: OH Audio / OpenSL)");
    return 0;
#endif
}

ZAN_AUDIO_EXPORT void zan_audio_close(void) {
#if defined(_WIN32)
    int i;
    if (zan_audio_thread) {
        SetEvent(zan_audio_stop_evt);
        WaitForSingleObject(zan_audio_thread, 5000);
        CloseHandle(zan_audio_thread);
        zan_audio_thread = NULL;
    }
    if (zan_audio_cs_ok) {
        EnterCriticalSection(&zan_audio_cs);
        for (i = 0; i < ZAN_VOICE_SLOTS; i++) zan_voice_reset(&zan_voices[i]);
        LeaveCriticalSection(&zan_audio_cs);
    }
    if (zan_audio_stop_evt) { CloseHandle(zan_audio_stop_evt); zan_audio_stop_evt = NULL; }
    if (zan_audio_fill_evt) { CloseHandle(zan_audio_fill_evt); zan_audio_fill_evt = NULL; }
    if (zan_audio_init_evt) { CloseHandle(zan_audio_init_evt); zan_audio_init_evt = NULL; }
    zan_audio_ready = 0;
#elif defined(__ANDROID__)
    zan_audio_lock();
    zan_audio_ready = 0;
    zan_audio_unlock();
    if (zan_audio_stream) {
        AAudioStream_close(zan_audio_stream);
        zan_audio_stream = NULL;
    }
    zan_audio_lock();
    for (int i = 0; i < ZAN_VOICE_SLOTS; i++) zan_voice_reset(&zan_voices[i]);
    zan_audio_unlock();
#elif defined(__linux__)
    zan_audio_lock();
    zan_audio_ready = 0;
    zan_audio_unlock();
    zan_alsa_stop();
    zan_audio_lock();
    for (int i = 0; i < ZAN_VOICE_SLOTS; i++) zan_voice_reset(&zan_voices[i]);
    zan_audio_unlock();
#elif defined(__APPLE__)
    zan_audio_lock();
    zan_audio_ready = 0;
    zan_audio_unlock();
    zan_coreaudio_stop();
    zan_audio_lock();
    for (int i = 0; i < ZAN_VOICE_SLOTS; i++) zan_voice_reset(&zan_voices[i]);
    zan_audio_unlock();
#elif defined(__OHOS__)
    zan_audio_lock();
    zan_audio_ready = 0;
    zan_audio_unlock();
    zan_ohaudio_stop();
    zan_audio_lock();
    for (int i = 0; i < ZAN_VOICE_SLOTS; i++) zan_voice_reset(&zan_voices[i]);
    zan_audio_unlock();
#else
    zan_audio_ready = 0;
#endif
}

ZAN_AUDIO_EXPORT int32_t zan_audio_is_open(void) {
    return zan_audio_ready ? 1 : 0;
}

ZAN_AUDIO_EXPORT void zan_audio_set_volume(double volume) {
    if (volume < 0.0) volume = 0.0;
    zan_audio_master = (float)volume;
}

ZAN_AUDIO_EXPORT double zan_audio_volume(void) {
    return (double)zan_audio_master;
}

ZAN_AUDIO_EXPORT const char *zan_audio_driver_name(void) {
#if defined(_WIN32)
    return zan_audio_ready ? "wasapi" : "";
#elif defined(__ANDROID__)
    return zan_audio_ready ? "aaudio" : "";
#elif defined(__linux__)
    return zan_audio_ready ? "alsa" : "";
#elif defined(__APPLE__)
    return zan_audio_ready ? "coreaudio" : "";
#elif defined(__OHOS__)
    return zan_audio_ready ? "ohaudio" : "";
#else
    return "";
#endif
}

ZAN_AUDIO_EXPORT int32_t zan_audio_active_voices(void) {
    int n = 0, i;
#if defined(_WIN32)
    if (zan_audio_cs_ok) {
        EnterCriticalSection(&zan_audio_cs);
        zan_voice_reap_locked();
        for (i = 0; i < ZAN_VOICE_SLOTS; i++)
            if (zan_voices[i].active && zan_voices[i].clip) n++;
        LeaveCriticalSection(&zan_audio_cs);
        return n;
    }
#else
    zan_audio_lock();
    zan_voice_reap_locked();
    for (i = 0; i < ZAN_VOICE_SLOTS; i++)
        if (zan_voices[i].active && zan_voices[i].clip) n++;
    zan_audio_unlock();
    return n;
#endif
    for (i = 0; i < ZAN_VOICE_SLOTS; i++)
        if (zan_voices[i].active && zan_voices[i].clip) n++;
    return n;
}

ZAN_AUDIO_EXPORT void zan_audio_stop_all(void) {
    int i;
#if defined(_WIN32)
    if (zan_audio_cs_ok) {
        EnterCriticalSection(&zan_audio_cs);
        for (i = 0; i < ZAN_VOICE_SLOTS; i++) zan_voice_reset(&zan_voices[i]);
        LeaveCriticalSection(&zan_audio_cs);
        return;
    }
#else
    zan_audio_lock();
    for (i = 0; i < ZAN_VOICE_SLOTS; i++) zan_voice_reset(&zan_voices[i]);
    zan_audio_unlock();
    return;
#endif
    for (i = 0; i < ZAN_VOICE_SLOTS; i++) zan_voice_reset(&zan_voices[i]);
}

ZAN_AUDIO_EXPORT int64_t zan_audio_load_wav(const char *path) {
    ZanAudioClip *c;
    unsigned char *bytes;
    int n, ok;
    zan_audio_set_err(NULL);
    bytes = zan_audio_read_file(path, &n);
    if (!bytes) return 0;
    c = (ZanAudioClip *)calloc(1, sizeof(ZanAudioClip));
    if (!c) { free(bytes); zan_audio_set_err("out of memory"); return 0; }
    ok = zan_wav_parse(bytes, n, c);
    free(bytes);
    if (!ok) { free(c); return 0; }
    return zan_handle_of(c);
}

ZAN_AUDIO_EXPORT int64_t zan_audio_load_wav_mem(const void *data, int32_t len) {
    ZanAudioClip *c;
    int ok;
    zan_audio_set_err(NULL);
    if (!data || len <= 0) { zan_audio_set_err("no data"); return 0; }
    c = (ZanAudioClip *)calloc(1, sizeof(ZanAudioClip));
    if (!c) { free(c); zan_audio_set_err("out of memory"); return 0; }
    ok = zan_wav_parse((const unsigned char *)data, len, c);
    if (!ok) { free(c); return 0; }
    return zan_handle_of(c);
}

ZAN_AUDIO_EXPORT int64_t zan_audio_load_ogg(const char *path) {
    ZanAudioClip *c;
    unsigned char *bytes;
    int n, ok;
    zan_audio_set_err(NULL);
    bytes = zan_audio_read_file(path, &n);
    if (!bytes) return 0;
    c = (ZanAudioClip *)calloc(1, sizeof(ZanAudioClip));
    if (!c) { free(bytes); zan_audio_set_err("out of memory"); return 0; }
    ok = zan_ogg_decode(bytes, n, c);
    free(bytes);
    if (!ok) { free(c); return 0; }
    return zan_handle_of(c);
}

ZAN_AUDIO_EXPORT int64_t zan_audio_load_ogg_mem(const void *data, int32_t len) {
    ZanAudioClip *c;
    int ok;
    zan_audio_set_err(NULL);
    if (!data || len <= 0) { zan_audio_set_err("no data"); return 0; }
    c = (ZanAudioClip *)calloc(1, sizeof(ZanAudioClip));
    if (!c) { free(c); zan_audio_set_err("out of memory"); return 0; }
    ok = zan_ogg_decode((const unsigned char *)data, len, c);
    if (!ok) { free(c); return 0; }
    return zan_handle_of(c);
}

ZAN_AUDIO_EXPORT void zan_audio_free_clip(int64_t clip_handle) {
    ZanAudioClip *c = (ZanAudioClip *)zan_ptr_of(clip_handle);
    int i;
    if (!c) return;
#if defined(_WIN32)
    if (zan_audio_cs_ok) EnterCriticalSection(&zan_audio_cs);
#else
    zan_audio_lock();
#endif
    for (i = 0; i < ZAN_VOICE_SLOTS; i++)
        if (zan_voices[i].clip == c) zan_voice_reset(&zan_voices[i]);
#if defined(_WIN32)
    if (zan_audio_cs_ok) LeaveCriticalSection(&zan_audio_cs);
#else
    zan_audio_unlock();
#endif
    free(c->pcm);
    free(c);
}

ZAN_AUDIO_EXPORT int32_t zan_audio_clip_frequency(int64_t clip_handle) {
    ZanAudioClip *c = (ZanAudioClip *)zan_ptr_of(clip_handle);
    return c ? c->freq : 0;
}

ZAN_AUDIO_EXPORT int32_t zan_audio_clip_channels(int64_t clip_handle) {
    ZanAudioClip *c = (ZanAudioClip *)zan_ptr_of(clip_handle);
    return c ? c->channels : 0;
}

ZAN_AUDIO_EXPORT int32_t zan_audio_clip_duration_ms(int64_t clip_handle) {
    ZanAudioClip *c = (ZanAudioClip *)zan_ptr_of(clip_handle);
    if (!c || c->freq <= 0 || c->frames <= 0) return 0;
    return (int32_t)(((int64_t)c->frames * 1000) / (int64_t)c->freq);
}

ZAN_AUDIO_EXPORT int64_t zan_audio_play(int64_t clip_handle, double gain, int32_t loop) {
    ZanAudioClip *c = (ZanAudioClip *)zan_ptr_of(clip_handle);
    ZanVoice *v;
    int slot = -1, i;
    if (!c || !c->pcm || c->frames <= 0) return 0;
    if (!zan_audio_ready) return 0;
#if defined(_WIN32)
    if (!zan_audio_cs_ok) return 0;
    EnterCriticalSection(&zan_audio_cs);
    zan_voice_reap_locked();
#else
    zan_audio_lock();
    zan_voice_reap_locked();
#endif
    for (i = 0; i < ZAN_VOICE_SLOTS; i++) {
        if (!zan_voices[i].active && !zan_voices[i].clip) { slot = i; break; }
    }
    if (slot < 0) {
#if defined(_WIN32)
        LeaveCriticalSection(&zan_audio_cs);
#else
        zan_audio_unlock();
#endif
        return 0;
    }
    v = &zan_voices[slot];
    zan_voice_reset(v);
    v->clip = c;
    v->loop = loop != 0 ? 1 : 0;
    v->gen = zan_voice_gen++;
    if (v->gen > 0x1FFFFF) v->gen = 1;
    v->gain = gain < 0.0 ? 0.0f : (float)gain;
    v->cursor = 0.0;
    v->step = (zan_audio_dev_freq > 0) ? ((double)c->freq / (double)zan_audio_dev_freq) : 1.0;
    if (!(v->step > 0.0)) v->step = 1.0;
    v->active = 1;
#if defined(_WIN32)
    LeaveCriticalSection(&zan_audio_cs);
#else
    zan_audio_unlock();
#endif
    return zan_voice_pack(slot, v->gen);
}

ZAN_AUDIO_EXPORT int32_t zan_audio_voice_playing(int64_t voice) {
#if defined(_WIN32)
    int playing;
    if (zan_audio_cs_ok) {
        ZanVoice *v;
        EnterCriticalSection(&zan_audio_cs);
        v = zan_voice_of(voice);
        if (!v || !v->clip) playing = 0;
        else if (v->loop) playing = 1;
        else if (v->cursor < (double)v->clip->frames) playing = 1;
        else { zan_voice_reset(v); playing = 0; }
        LeaveCriticalSection(&zan_audio_cs);
        return playing;
    }
#else
    int playing;
    zan_audio_lock();
    {
        ZanVoice *v = zan_voice_of(voice);
        if (!v || !v->clip) playing = 0;
        else if (v->loop) playing = 1;
        else if (v->cursor < (double)v->clip->frames) playing = 1;
        else { zan_voice_reset(v); playing = 0; }
    }
    zan_audio_unlock();
    return playing;
#endif
    {
        ZanVoice *v = zan_voice_of(voice);
        if (!v || !v->clip) return 0;
        if (v->loop) return 1;
        if (v->cursor < (double)v->clip->frames) return 1;
        zan_voice_reset(v);
        return 0;
    }
}

ZAN_AUDIO_EXPORT void zan_audio_voice_stop(int64_t voice) {
#if defined(_WIN32)
    if (zan_audio_cs_ok) {
        ZanVoice *v;
        EnterCriticalSection(&zan_audio_cs);
        v = zan_voice_of(voice);
        if (v) zan_voice_reset(v);
        LeaveCriticalSection(&zan_audio_cs);
        return;
    }
#else
    zan_audio_lock();
    {
        ZanVoice *v = zan_voice_of(voice);
        if (v) zan_voice_reset(v);
    }
    zan_audio_unlock();
    return;
#endif
    {
        ZanVoice *v = zan_voice_of(voice);
        if (v) zan_voice_reset(v);
    }
}

ZAN_AUDIO_EXPORT void zan_audio_voice_set_gain(int64_t voice, double gain) {
#if defined(_WIN32)
    if (zan_audio_cs_ok) {
        ZanVoice *v;
        EnterCriticalSection(&zan_audio_cs);
        v = zan_voice_of(voice);
        if (v) {
            if (gain < 0.0) gain = 0.0;
            v->gain = (float)gain;
        }
        LeaveCriticalSection(&zan_audio_cs);
        return;
    }
#else
    zan_audio_lock();
    {
        ZanVoice *v = zan_voice_of(voice);
        if (v) {
            if (gain < 0.0) gain = 0.0;
            v->gain = (float)gain;
        }
    }
    zan_audio_unlock();
    return;
#endif
    {
        ZanVoice *v = zan_voice_of(voice);
        if (!v) return;
        if (gain < 0.0) gain = 0.0;
        v->gain = (float)gain;
    }
}

ZAN_AUDIO_EXPORT const char *zan_audio_last_error(void) {
    return zan_audio_err;
}
