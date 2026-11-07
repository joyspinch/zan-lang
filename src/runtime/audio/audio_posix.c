/* audio_posix.c -- POSIX / Android / Linux / macOS / OpenHarmony 平台音频后端驱动 */

#ifdef __ANDROID__

static aaudio_data_callback_result_t zan_audio_aa_callback(
        AAudioStream *stream, void *userData, void *audioData,
        int32_t numFrames) {
    (void)stream; (void)userData;
    if (numFrames <= 0) return AAUDIO_CALLBACK_RESULT_CONTINUE;
    if (numFrames > ZAN_AUDIO_MAX_FILL) numFrames = ZAN_AUDIO_MAX_FILL;
    zan_audio_lock();
    if (!zan_audio_ready || zan_audio_stream != stream) {
        zan_audio_unlock();
        memset(audioData, 0,
               (size_t)numFrames * 2 * sizeof(short));
        return AAUDIO_CALLBACK_RESULT_CONTINUE;
    }
    zan_audio_mix_s16((unsigned char *)audioData, numFrames,
                      zan_audio_dev_channels);
    zan_audio_unlock();
    return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

static void zan_audio_aa_error(AAudioStream *stream, void *userData,
                               aaudio_result_t error) {
    (void)stream; (void)userData;
    zan_audio_set_err(AAudio_convertResultToText(error));
}

static AAudioStream *zan_audio_aa_open_stream(void) {
    AAudioStreamBuilder *b = NULL;
    AAudioStream *s = NULL;
    if (AAudio_createStreamBuilder(&b) != AAUDIO_OK || !b) {
        zan_audio_set_err("AAudio_createStreamBuilder failed");
        return NULL;
    }
    AAudioStreamBuilder_setDirection(b, AAUDIO_DIRECTION_OUTPUT);
    AAudioStreamBuilder_setSharingMode(b, AAUDIO_SHARING_MODE_SHARED);
    AAudioStreamBuilder_setFormat(b, AAUDIO_FORMAT_PCM_I16);
    AAudioStreamBuilder_setSampleRate(b, 48000);
    AAudioStreamBuilder_setChannelCount(b, 2);
    AAudioStreamBuilder_setPerformanceMode(b,
                                           AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
    AAudioStreamBuilder_setDataCallback(b, zan_audio_aa_callback, NULL);
    AAudioStreamBuilder_setErrorCallback(b, zan_audio_aa_error, NULL);
    if (AAudioStreamBuilder_openStream(b, &s) != AAUDIO_OK || !s) {
        AAudioStreamBuilder_delete(b);
        zan_audio_set_err("AAudio openStream failed");
        return NULL;
    }
    AAudioStreamBuilder_delete(b);
    return s;
}

#endif /* __ANDROID__ */

#if defined(__linux__) && !defined(__ANDROID__)

typedef struct {
    void *handle;
    int (*open)(void **pcm, const char *name, int stream, int mode);
    int (*set_params)(void *pcm, int format, int access, unsigned int channels,
                      unsigned int rate, int soft_resample, unsigned int latency);
    long (*writei)(void *pcm, const void *buffer, unsigned long size);
    int (*recover)(void *pcm, int err, int silent);
    int (*close)(void *pcm);
    int (*drain)(void *pcm);
} zan_alsa_api_t;

static zan_alsa_api_t zan_alsa;
static void *zan_alsa_pcm = NULL;
static pthread_t zan_alsa_th;
static int zan_alsa_th_running = 0;

static void *zan_alsa_worker(void *arg) {
    (void)arg;
    short buf[1024 * 2];
    while (zan_audio_ready) {
        zan_audio_lock();
        if (!zan_audio_ready) {
            zan_audio_unlock();
            break;
        }
        zan_audio_mix_s16((unsigned char *)buf, 1024, zan_audio_dev_channels);
        zan_audio_unlock();

        long written = zan_alsa.writei(zan_alsa_pcm, buf, 1024);
        if (written < 0) {
            zan_alsa.recover(zan_alsa_pcm, (int)written, 1);
        }
    }
    return NULL;
}

static int zan_alsa_init(void) {
    if (zan_alsa.handle) return 1;
    zan_alsa.handle = dlopen("libasound.so.2", RTLD_NOW | RTLD_LOCAL);
    if (!zan_alsa.handle) {
        zan_alsa.handle = dlopen("libasound.so", RTLD_NOW | RTLD_LOCAL);
    }
    if (!zan_alsa.handle) {
        zan_audio_set_err("ALSA driver: libasound.so.2 not found");
        return 0;
    }
    zan_alsa.open = (int (*)(void **, const char *, int, int))dlsym(zan_alsa.handle, "snd_pcm_open");
    zan_alsa.set_params = (int (*)(void *, int, int, unsigned int, unsigned int, int, unsigned int))dlsym(zan_alsa.handle, "snd_pcm_set_params");
    zan_alsa.writei = (long (*)(void *, const void *, unsigned long))dlsym(zan_alsa.handle, "snd_pcm_writei");
    zan_alsa.recover = (int (*)(void *, int, int))dlsym(zan_alsa.handle, "snd_pcm_recover");
    zan_alsa.close = (int (*)(void *))dlsym(zan_alsa.handle, "snd_pcm_close");
    zan_alsa.drain = (int (*)(void *))dlsym(zan_alsa.handle, "snd_pcm_drain");
    if (!zan_alsa.open || !zan_alsa.set_params || !zan_alsa.writei ||
        !zan_alsa.recover || !zan_alsa.close) {
        dlclose(zan_alsa.handle);
        zan_alsa.handle = NULL;
        zan_audio_set_err("ALSA driver: missing required symbols in libasound");
        return 0;
    }
    return 1;
}

static int zan_alsa_start(void) {
    if (!zan_alsa_init()) return 0;
    int err = zan_alsa.open(&zan_alsa_pcm, "default", 0 /* SND_PCM_STREAM_PLAYBACK */, 0);
    if (err < 0 || !zan_alsa_pcm) {
        zan_audio_set_err("ALSA driver: snd_pcm_open failed on default device");
        return 0;
    }
    zan_audio_dev_freq = 44100;
    zan_audio_dev_channels = 2;
    err = zan_alsa.set_params(zan_alsa_pcm, 2 /* SND_PCM_FORMAT_S16_LE */, 3 /* SND_PCM_ACCESS_RW_INTERLEAVED */,
                              (unsigned int)zan_audio_dev_channels, (unsigned int)zan_audio_dev_freq, 1, 50000);
    if (err < 0) {
        zan_audio_dev_freq = 48000;
        err = zan_alsa.set_params(zan_alsa_pcm, 2, 3, (unsigned int)zan_audio_dev_channels,
                                  (unsigned int)zan_audio_dev_freq, 1, 50000);
    }
    if (err < 0) {
        zan_alsa.close(zan_alsa_pcm);
        zan_alsa_pcm = NULL;
        zan_audio_set_err("ALSA driver: snd_pcm_set_params failed");
        return 0;
    }
    zan_alsa_th_running = (pthread_create(&zan_alsa_th, NULL, zan_alsa_worker, NULL) == 0);
    if (!zan_alsa_th_running) {
        zan_alsa.close(zan_alsa_pcm);
        zan_alsa_pcm = NULL;
        zan_audio_set_err("ALSA driver: failed to spawn worker thread");
        return 0;
    }
    return 1;
}

static void zan_alsa_stop(void) {
    if (zan_alsa_th_running) {
        pthread_join(zan_alsa_th, NULL);
        zan_alsa_th_running = 0;
    }
    if (zan_alsa_pcm && zan_alsa.close) {
        zan_alsa.close(zan_alsa_pcm);
        zan_alsa_pcm = NULL;
    }
}
#endif /* __linux__ */

#if defined(__APPLE__)

typedef uint32_t zan_ca_OSType;
typedef int32_t zan_ca_OSStatus;
typedef void *zan_ca_AudioComponent;
typedef void *zan_ca_AudioComponentInstance;

typedef struct {
    zan_ca_OSType componentType;
    zan_ca_OSType componentSubType;
    zan_ca_OSType componentManufacturer;
    uint32_t componentFlags;
    uint32_t componentFlagsMask;
} zan_ca_AudioComponentDescription;

typedef struct {
    uint32_t mNumberChannels;
    uint32_t mDataByteSize;
    void *mData;
} zan_ca_AudioBuffer;

typedef struct {
    uint32_t mNumberBuffers;
    zan_ca_AudioBuffer mBuffers[1];
} zan_ca_AudioBufferList;

typedef struct {
    double mSampleRate;
    uint32_t mFormatID;
    uint32_t mFormatFlags;
    uint32_t mBytesPerPacket;
    uint32_t mFramesPerPacket;
    uint32_t mBytesPerFrame;
    uint32_t mChannelsPerFrame;
    uint32_t mBitsPerChannel;
    uint32_t mReserved;
} zan_ca_AudioStreamBasicDescription;

typedef zan_ca_OSStatus (*zan_ca_AURenderCallback)(
    void *inRefCon,
    uint32_t *ioActionFlags,
    const void *inTimeStamp,
    uint32_t inBusNumber,
    uint32_t inNumberFrames,
    zan_ca_AudioBufferList *ioData);

typedef struct {
    zan_ca_AURenderCallback inputProc;
    void *inputProcRefCon;
} zan_ca_AURenderCallbackStruct;

typedef struct {
    void *handle;
    zan_ca_AudioComponent (*FindNext)(zan_ca_AudioComponent inComponent, const zan_ca_AudioComponentDescription *inDesc);
    zan_ca_OSStatus (*InstanceNew)(zan_ca_AudioComponent inComponent, zan_ca_AudioComponentInstance *outInstance);
    zan_ca_OSStatus (*InstanceDispose)(zan_ca_AudioComponentInstance inInstance);
    zan_ca_OSStatus (*UnitInitialize)(zan_ca_AudioComponentInstance inUnit);
    zan_ca_OSStatus (*UnitUninitialize)(zan_ca_AudioComponentInstance inUnit);
    zan_ca_OSStatus (*OutputStart)(zan_ca_AudioComponentInstance ci);
    zan_ca_OSStatus (*OutputStop)(zan_ca_AudioComponentInstance ci);
    zan_ca_OSStatus (*SetProperty)(zan_ca_AudioComponentInstance inUnit,
                                   uint32_t inID, uint32_t inScope, uint32_t inElement,
                                   const void *inData, uint32_t inDataSize);
} zan_coreaudio_api_t;

static zan_coreaudio_api_t zan_ca;
static zan_ca_AudioComponentInstance zan_ca_unit = NULL;

static zan_ca_OSStatus zan_coreaudio_render_cb(
    void *inRefCon,
    uint32_t *ioActionFlags,
    const void *inTimeStamp,
    uint32_t inBusNumber,
    uint32_t inNumberFrames,
    zan_ca_AudioBufferList *ioData) {
    (void)inRefCon; (void)ioActionFlags; (void)inTimeStamp; (void)inBusNumber;
    if (inNumberFrames <= 0 || !ioData || ioData->mNumberBuffers == 0) return 0;
    float *dst = (float *)ioData->mBuffers[0].mData;
    if (!dst) return 0;

    zan_audio_lock();
    if (!zan_audio_ready) {
        zan_audio_unlock();
        memset(dst, 0, (size_t)inNumberFrames * 2 * sizeof(float));
        return 0;
    }
    zan_audio_mix_f32(dst, (int)inNumberFrames, zan_audio_dev_channels);
    zan_audio_unlock();
    return 0;
}

static int zan_coreaudio_init(void) {
    if (zan_ca.handle) return 1;
    const char *paths[] = {
        "/System/Library/Frameworks/AudioToolbox.framework/AudioToolbox",
        "AudioToolbox.framework/AudioToolbox",
        NULL
    };
    for (int i = 0; paths[i]; i++) {
        zan_ca.handle = dlopen(paths[i], RTLD_NOW | RTLD_LOCAL);
        if (zan_ca.handle) break;
    }
    if (!zan_ca.handle) {
        zan_audio_set_err("CoreAudio driver: AudioToolbox framework not found");
        return 0;
    }
    zan_ca.FindNext = (zan_ca_AudioComponent (*)(zan_ca_AudioComponent, const zan_ca_AudioComponentDescription *))dlsym(zan_ca.handle, "AudioComponentFindNext");
    zan_ca.InstanceNew = (zan_ca_OSStatus (*)(zan_ca_AudioComponent, zan_ca_AudioComponentInstance *))dlsym(zan_ca.handle, "AudioComponentInstanceNew");
    zan_ca.InstanceDispose = (zan_ca_OSStatus (*)(zan_ca_AudioComponentInstance))dlsym(zan_ca.handle, "AudioComponentInstanceDispose");
    zan_ca.UnitInitialize = (zan_ca_OSStatus (*)(zan_ca_AudioComponentInstance))dlsym(zan_ca.handle, "AudioUnitInitialize");
    zan_ca.UnitUninitialize = (zan_ca_OSStatus (*)(zan_ca_AudioComponentInstance))dlsym(zan_ca.handle, "AudioUnitUninitialize");
    zan_ca.OutputStart = (zan_ca_OSStatus (*)(zan_ca_AudioComponentInstance))dlsym(zan_ca.handle, "AudioOutputUnitStart");
    zan_ca.OutputStop = (zan_ca_OSStatus (*)(zan_ca_AudioComponentInstance))dlsym(zan_ca.handle, "AudioOutputUnitStop");
    zan_ca.SetProperty = (zan_ca_OSStatus (*)(zan_ca_AudioComponentInstance, uint32_t, uint32_t, uint32_t, const void *, uint32_t))dlsym(zan_ca.handle, "AudioUnitSetProperty");
    if (!zan_ca.FindNext || !zan_ca.InstanceNew || !zan_ca.UnitInitialize ||
        !zan_ca.OutputStart || !zan_ca.OutputStop || !zan_ca.SetProperty) {
        dlclose(zan_ca.handle);
        zan_ca.handle = NULL;
        zan_audio_set_err("CoreAudio driver: missing required symbols in AudioToolbox");
        return 0;
    }
    return 1;
}

static int zan_coreaudio_start(void) {
    if (!zan_coreaudio_init()) return 0;

    zan_ca_AudioComponentDescription desc;
    desc.componentType = 0x61756f75;        /* 'auou' = kAudioUnitType_Output */
    desc.componentSubType = 0x64656620;     /* 'def ' = kAudioUnitSubType_DefaultOutput */
    desc.componentManufacturer = 0x6170706c;/* 'appl' = kAudioUnitManufacturer_Apple */
    desc.componentFlags = 0;
    desc.componentFlagsMask = 0;

    zan_ca_AudioComponent comp = zan_ca.FindNext(NULL, &desc);
    if (!comp) {
        zan_audio_set_err("CoreAudio driver: default output audio component not found");
        return 0;
    }

    if (zan_ca.InstanceNew(comp, &zan_ca_unit) != 0 || !zan_ca_unit) {
        zan_audio_set_err("CoreAudio driver: AudioComponentInstanceNew failed");
        return 0;
    }

    zan_audio_dev_freq = 44100;
    zan_audio_dev_channels = 2;

    zan_ca_AudioStreamBasicDescription fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.mSampleRate = (double)zan_audio_dev_freq;
    fmt.mFormatID = 0x6c70636d; /* 'lpcm' = kAudioFormatLinearPCM */
    fmt.mFormatFlags = (1U << 0) | (1U << 3); /* kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked */
    fmt.mChannelsPerFrame = (uint32_t)zan_audio_dev_channels;
    fmt.mBitsPerChannel = 32;
    fmt.mBytesPerFrame = (uint32_t)(zan_audio_dev_channels * sizeof(float));
    fmt.mFramesPerPacket = 1;
    fmt.mBytesPerPacket = fmt.mBytesPerFrame;

    /* kAudioUnitProperty_StreamFormat = 8, kAudioUnitScope_Input = 1 */
    zan_ca.SetProperty(zan_ca_unit, 8, 1, 0, &fmt, sizeof(fmt));

    zan_ca_AURenderCallbackStruct cb;
    cb.inputProc = zan_coreaudio_render_cb;
    cb.inputProcRefCon = NULL;
    zan_ca.SetProperty(zan_ca_unit, 26, 1, 0, &cb, sizeof(cb));

    if (zan_ca.UnitInitialize(zan_ca_unit) != 0) {
        zan_ca.InstanceDispose(zan_ca_unit);
        zan_ca_unit = NULL;
        zan_audio_set_err("CoreAudio driver: AudioUnitInitialize failed");
        return 0;
    }

    if (zan_ca.OutputStart(zan_ca_unit) != 0) {
        zan_ca.UnitUninitialize(zan_ca_unit);
        zan_ca.InstanceDispose(zan_ca_unit);
        zan_ca_unit = NULL;
        zan_audio_set_err("CoreAudio driver: AudioOutputUnitStart failed");
        return 0;
    }
    return 1;
}

static void zan_coreaudio_stop(void) {
    if (zan_ca_unit) {
        if (zan_ca.OutputStop) zan_ca.OutputStop(zan_ca_unit);
        if (zan_ca.UnitUninitialize) zan_ca.UnitUninitialize(zan_ca_unit);
        if (zan_ca.InstanceDispose) zan_ca.InstanceDispose(zan_ca_unit);
        zan_ca_unit = NULL;
    }
}
#endif /* __APPLE__ */

#if defined(__OHOS__)

typedef void *zan_oh_stream_builder_t;
typedef void *zan_oh_renderer_t;

typedef struct {
    int32_t (*OnWriteData)(zan_oh_renderer_t renderer, void *userData, void *buffer, int32_t length);
    int32_t (*OnStreamEvent)(zan_oh_renderer_t renderer, void *userData, int32_t event);
    int32_t (*OnInterruptEvent)(zan_oh_renderer_t renderer, void *userData, int32_t interruptType, int32_t interruptHint);
    int32_t (*OnError)(zan_oh_renderer_t renderer, void *userData, int32_t error);
} zan_oh_renderer_callbacks_t;

typedef struct {
    void *handle;
    int32_t (*Builder_Create)(zan_oh_stream_builder_t *builder, int32_t type);
    int32_t (*Builder_Destroy)(zan_oh_stream_builder_t builder);
    int32_t (*Builder_SetSamplingRate)(zan_oh_stream_builder_t builder, int32_t rate);
    int32_t (*Builder_SetChannelCount)(zan_oh_stream_builder_t builder, int32_t channelCount);
    int32_t (*Builder_SetSampleFormat)(zan_oh_stream_builder_t builder, int32_t format);
    int32_t (*Builder_SetRendererCallback)(zan_oh_stream_builder_t builder, zan_oh_renderer_callbacks_t callbacks, void *userData);
    int32_t (*Builder_GenerateRenderer)(zan_oh_stream_builder_t builder, zan_oh_renderer_t *renderer);
    int32_t (*Renderer_Start)(zan_oh_renderer_t renderer);
    int32_t (*Renderer_Stop)(zan_oh_renderer_t renderer);
    int32_t (*Renderer_Release)(zan_oh_renderer_t renderer);
} zan_ohos_audio_api_t;

static zan_ohos_audio_api_t zan_ohaudio;
static zan_oh_renderer_t zan_ohaudio_renderer = NULL;

static int32_t zan_ohaudio_on_write(zan_oh_renderer_t renderer, void *userData, void *buffer, int32_t length) {
    (void)renderer; (void)userData;
    int frames = length / (2 * (int)sizeof(short));
    if (frames > ZAN_AUDIO_MAX_FILL) frames = ZAN_AUDIO_MAX_FILL;
    zan_audio_lock();
    if (!zan_audio_ready) {
        zan_audio_unlock();
        memset(buffer, 0, (size_t)length);
        return 0;
    }
    zan_audio_mix_s16((unsigned char *)buffer, frames, zan_audio_dev_channels);
    zan_audio_unlock();
    return 0;
}

static int zan_ohaudio_init(void) {
    if (zan_ohaudio.handle) return 1;
    zan_ohaudio.handle = dlopen("libohaudio.so", RTLD_NOW | RTLD_LOCAL);
    if (!zan_ohaudio.handle) {
        zan_audio_set_err("OH Audio driver: libohaudio.so not found");
        return 0;
    }
    zan_ohaudio.Builder_Create = (int32_t (*)(zan_oh_stream_builder_t *, int32_t))dlsym(zan_ohaudio.handle, "OH_AudioStreamBuilder_Create");
    zan_ohaudio.Builder_Destroy = (int32_t (*)(zan_oh_stream_builder_t))dlsym(zan_ohaudio.handle, "OH_AudioStreamBuilder_Destroy");
    zan_ohaudio.Builder_SetSamplingRate = (int32_t (*)(zan_oh_stream_builder_t, int32_t))dlsym(zan_ohaudio.handle, "OH_AudioStreamBuilder_SetSamplingRate");
    zan_ohaudio.Builder_SetChannelCount = (int32_t (*)(zan_oh_stream_builder_t, int32_t))dlsym(zan_ohaudio.handle, "OH_AudioStreamBuilder_SetChannelCount");
    zan_ohaudio.Builder_SetSampleFormat = (int32_t (*)(zan_oh_stream_builder_t, int32_t))dlsym(zan_ohaudio.handle, "OH_AudioStreamBuilder_SetSampleFormat");
    zan_ohaudio.Builder_SetRendererCallback = (int32_t (*)(zan_oh_stream_builder_t, zan_oh_renderer_callbacks_t, void *))dlsym(zan_ohaudio.handle, "OH_AudioStreamBuilder_SetRendererCallback");
    zan_ohaudio.Builder_GenerateRenderer = (int32_t (*)(zan_oh_stream_builder_t, zan_oh_renderer_t *))dlsym(zan_ohaudio.handle, "OH_AudioStreamBuilder_GenerateRenderer");
    zan_ohaudio.Renderer_Start = (int32_t (*)(zan_oh_renderer_t))dlsym(zan_ohaudio.handle, "OH_AudioRenderer_Start");
    zan_ohaudio.Renderer_Stop = (int32_t (*)(zan_oh_renderer_t))dlsym(zan_ohaudio.handle, "OH_AudioRenderer_Stop");
    zan_ohaudio.Renderer_Release = (int32_t (*)(zan_oh_renderer_t))dlsym(zan_ohaudio.handle, "OH_AudioRenderer_Release");

    if (!zan_ohaudio.Builder_Create || !zan_ohaudio.Builder_Destroy ||
        !zan_ohaudio.Builder_SetSamplingRate || !zan_ohaudio.Builder_SetChannelCount ||
        !zan_ohaudio.Builder_SetSampleFormat || !zan_ohaudio.Builder_SetRendererCallback ||
        !zan_ohaudio.Builder_GenerateRenderer || !zan_ohaudio.Renderer_Start ||
        !zan_ohaudio.Renderer_Release) {
        dlclose(zan_ohaudio.handle);
        zan_ohaudio.handle = NULL;
        zan_audio_set_err("OH Audio driver: missing required symbols in libohaudio");
        return 0;
    }
    return 1;
}

static int zan_ohaudio_start(void) {
    if (!zan_ohaudio_init()) return 0;
    zan_oh_stream_builder_t builder = NULL;
    if (zan_ohaudio.Builder_Create(&builder, 1 /* AUDIOSTREAM_TYPE_RENDERER */) != 0 || !builder) {
        zan_audio_set_err("OH Audio driver: OH_AudioStreamBuilder_Create failed");
        return 0;
    }
    zan_audio_dev_freq = 48000;
    zan_audio_dev_channels = 2;
    zan_ohaudio.Builder_SetSamplingRate(builder, zan_audio_dev_freq);
    zan_ohaudio.Builder_SetChannelCount(builder, zan_audio_dev_channels);
    zan_ohaudio.Builder_SetSampleFormat(builder, 1 /* AUDIOSTREAM_SAMPLE_S16LE */);

    zan_oh_renderer_callbacks_t cbs;
    memset(&cbs, 0, sizeof(cbs));
    cbs.OnWriteData = zan_ohaudio_on_write;
    zan_ohaudio.Builder_SetRendererCallback(builder, cbs, NULL);

    if (zan_ohaudio.Builder_GenerateRenderer(builder, &zan_ohaudio_renderer) != 0 || !zan_ohaudio_renderer) {
        zan_ohaudio.Builder_Destroy(builder);
        zan_audio_set_err("OH Audio driver: OH_AudioStreamBuilder_GenerateRenderer failed");
        return 0;
    }
    zan_ohaudio.Builder_Destroy(builder);

    if (zan_ohaudio.Renderer_Start(zan_ohaudio_renderer) != 0) {
        zan_ohaudio.Renderer_Release(zan_ohaudio_renderer);
        zan_ohaudio_renderer = NULL;
        zan_audio_set_err("OH Audio driver: OH_AudioRenderer_Start failed");
        return 0;
    }
    return 1;
}

static void zan_ohaudio_stop(void) {
    if (zan_ohaudio_renderer) {
        if (zan_ohaudio.Renderer_Stop) zan_ohaudio.Renderer_Stop(zan_ohaudio_renderer);
        if (zan_ohaudio.Renderer_Release) zan_ohaudio.Renderer_Release(zan_ohaudio_renderer);
        zan_ohaudio_renderer = NULL;
    }
}

#endif /* __OHOS__ */
