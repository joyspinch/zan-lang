/* audio_mixer.c -- 多轨声道混音器、音量控制与声音实例管理 */

#ifndef ZAN_VOICE_SLOTS
#define ZAN_VOICE_SLOTS 256
#endif
#define ZAN_AUDIO_MAX_FILL 4096
#define ZAN_AUDIO_MAX_CHANNELS 8
#define ZAN_AUDIO_MAX_FILE (256 * 1024 * 1024)

typedef struct ZanAudioClip {
    short *pcm;   /* s16 interleaved */
    int frames;
    int freq;
    int channels;
} ZanAudioClip;

typedef struct ZanVoice {
    ZanAudioClip *clip;
    double cursor;
    double step;
    float gain;
    int loop;
    int gen;
    int active;
} ZanVoice;

static ZanVoice zan_voices[ZAN_VOICE_SLOTS];
static int zan_voice_gen = 1;
static float zan_audio_master = 1.0f;
static int zan_audio_ready;
static char zan_audio_err[192];

#ifdef _WIN32
static CRITICAL_SECTION zan_audio_cs;
static int zan_audio_cs_ok;
static HANDLE zan_audio_thread;
static HANDLE zan_audio_stop_evt;
static HANDLE zan_audio_fill_evt;
static HANDLE zan_audio_init_evt;
static IAudioClient *zan_audio_client;
static IAudioRenderClient *zan_audio_render;
static UINT32 zan_audio_buf_frames;
static int zan_audio_dev_freq;
static int zan_audio_dev_channels;
static int zan_audio_dev_fmt;
#endif

#ifndef _WIN32
#if defined(__wasi__)
static void zan_audio_lock(void) { }
static void zan_audio_unlock(void) { }
#else
static pthread_mutex_t zan_audio_mutex = PTHREAD_MUTEX_INITIALIZER;
static void zan_audio_lock(void) { pthread_mutex_lock(&zan_audio_mutex); }
static void zan_audio_unlock(void) { pthread_mutex_unlock(&zan_audio_mutex); }
#endif
static int zan_audio_dev_freq;
static int zan_audio_dev_channels;
#endif

#ifdef __ANDROID__
static AAudioStream *zan_audio_stream;
#endif

static void zan_audio_set_err(const char *msg) {
    if (msg) {
        size_t n = strlen(msg);
        if (n >= sizeof(zan_audio_err)) n = sizeof(zan_audio_err) - 1;
        memcpy(zan_audio_err, msg, n);
        zan_audio_err[n] = 0;
    } else {
        zan_audio_err[0] = 0;
    }
}

static void *zan_ptr_of(int64_t handle) {
    return (void *)(intptr_t)handle;
}

static int64_t zan_handle_of(const void *ptr) {
    return (int64_t)(intptr_t)ptr;
}

static int64_t zan_voice_pack(int slot, int gen) {
    return ((int64_t)gen << 8) | (int64_t)(slot + 1);
}

static ZanVoice *zan_voice_of(int64_t handle) {
    int slot = (int)((handle & 255) - 1);
    int gen = (int)(handle >> 8);
    if (slot < 0 || slot >= ZAN_VOICE_SLOTS) return NULL;
    {
        ZanVoice *v = &zan_voices[slot];
        if (!v->active || v->gen != gen) return NULL;
        return v;
    }
}

static void zan_voice_reset(ZanVoice *v) {
    memset(v, 0, sizeof(*v));
}

static void zan_voice_reap_locked(void) {
    int i;
    for (i = 0; i < ZAN_VOICE_SLOTS; i++) {
        ZanVoice *v = &zan_voices[i];
        if (!v->active || v->loop || !v->clip) continue;
        if (v->cursor >= (double)v->clip->frames) zan_voice_reset(v);
    }
}

/* Mixer (Windows) */

#ifdef _WIN32

static void zan_audio_mix(unsigned char *dst, UINT32 frames) {
    static float acc[ZAN_AUDIO_MAX_FILL * ZAN_AUDIO_MAX_CHANNELS];
    int f, ch, i;
    int devch = zan_audio_dev_channels;
    int total;

    if (frames > ZAN_AUDIO_MAX_FILL) frames = ZAN_AUDIO_MAX_FILL;
    if (devch < 1 || devch > ZAN_AUDIO_MAX_CHANNELS) devch = 2;
    total = (int)frames * devch;
    memset(acc, 0, sizeof(float) * (size_t)total);

    EnterCriticalSection(&zan_audio_cs);
    zan_voice_reap_locked();
    for (i = 0; i < ZAN_VOICE_SLOTS; i++) {
        ZanVoice *v = &zan_voices[i];
        ZanAudioClip *c;
        double cur, step, g;
        int nf, clipch;
        const short *pcm;
        if (!v->active || !v->clip) continue;
        c = v->clip;
        nf = c->frames;
        if (nf <= 0) { zan_voice_reset(v); continue; }
        clipch = c->channels;
        pcm = c->pcm;
        cur = v->cursor;
        step = v->step;
        if (!(step > 0.0)) step = 1.0;
        g = ((double)v->gain * (double)zan_audio_master) / 32767.0;
        if (g != 0.0) {
            for (f = 0; f < (int)frames; f++) {
                int i0, i1;
                double frac;
                if (v->loop) {
                    if (cur >= (double)nf) cur = fmod(cur, (double)nf);
                } else if (cur >= (double)nf) {
                    break;
                }
                i0 = (int)cur;
                frac = cur - (double)i0;
                i1 = i0 + 1;
                if (i1 >= nf) i1 = nf - 1;
                for (ch = 0; ch < devch; ch++) {
                    int cc = ch % clipch;
                    float s0 = (float)pcm[(size_t)i0 * clipch + cc];
                    float s1 = (float)pcm[(size_t)i1 * clipch + cc];
                    acc[(size_t)f * devch + ch] += (s0 + (s1 - s0) * (float)frac) * (float)g;
                }
                cur += step;
            }
        }
        v->cursor = cur;
        if (!v->loop && cur >= (double)nf) zan_voice_reset(v);
    }
    LeaveCriticalSection(&zan_audio_cs);

    switch (zan_audio_dev_fmt) {
    case 0: { /* float32 */
        float *d = (float *)dst;
        for (i = 0; i < total; i++) {
            float s = acc[i];
            if (s > 1.0f) s = 1.0f;
            if (s < -1.0f) s = -1.0f;
            d[i] = s;
        }
        break;
    }
    case 1: { /* s16 */
        short *d = (short *)dst;
        for (i = 0; i < total; i++) {
            float s = acc[i];
            if (s > 1.0f) s = 1.0f;
            if (s < -1.0f) s = -1.0f;
            d[i] = (short)(int)(s * 32767.0f);
        }
        break;
    }
    case 2: { /* s32 */
        int *d = (int *)dst;
        for (i = 0; i < total; i++) {
            float s = acc[i];
            if (s > 1.0f) s = 1.0f;
            if (s < -1.0f) s = -1.0f;
            d[i] = (int)((double)s * 2147483647.0);
        }
        break;
    }
    case 3: { /* s24 packed, little-endian */
        size_t k = 0;
        for (i = 0; i < total; i++) {
            float s = acc[i];
            int sv;
            if (s > 1.0f) s = 1.0f;
            if (s < -1.0f) s = -1.0f;
            sv = (int)((double)s * 8388607.0);
            dst[k++] = (unsigned char)(sv & 255);
            dst[k++] = (unsigned char)((sv >> 8) & 255);
            dst[k++] = (unsigned char)((sv >> 16) & 255);
        }
        break;
    }
    default:
        break;
    }
}

#endif /* _WIN32 */

/* Mixer (POSIX / Android / AAudio) */

#ifndef _WIN32

static void zan_audio_mix_s16(unsigned char *dst, int frames, int devch) {
    static float acc[ZAN_AUDIO_MAX_FILL * ZAN_AUDIO_MAX_CHANNELS];
    int f, ch, i;
    int total;
    short *d;

    if (frames > ZAN_AUDIO_MAX_FILL) frames = ZAN_AUDIO_MAX_FILL;
    if (devch < 1 || devch > ZAN_AUDIO_MAX_CHANNELS) devch = 2;
    total = (int)frames * devch;
    memset(acc, 0, sizeof(float) * (size_t)total);

    for (i = 0; i < ZAN_VOICE_SLOTS; i++) {
        ZanVoice *v = &zan_voices[i];
        ZanAudioClip *c;
        double cur, step, g;
        int nf, clipch;
        const short *pcm;
        if (!v->active || !v->clip) continue;
        c = v->clip;
        nf = c->frames;
        if (nf <= 0) { zan_voice_reset(v); continue; }
        clipch = c->channels;
        pcm = c->pcm;
        cur = v->cursor;
        step = v->step;
        if (!(step > 0.0)) step = 1.0;
        g = ((double)v->gain * (double)zan_audio_master) / 32767.0;
        if (g != 0.0) {
            for (f = 0; f < frames; f++) {
                int i0, i1;
                double frac;
                if (v->loop) {
                    if (cur >= (double)nf) cur = fmod(cur, (double)nf);
                } else if (cur >= (double)nf) {
                    break;
                }
                i0 = (int)cur;
                frac = cur - (double)i0;
                i1 = i0 + 1;
                if (i1 >= nf) i1 = nf - 1;
                for (ch = 0; ch < devch; ch++) {
                    int cc = ch % clipch;
                    float s0 = (float)pcm[(size_t)i0 * clipch + cc];
                    float s1 = (float)pcm[(size_t)i1 * clipch + cc];
                    acc[(size_t)f * devch + ch] += (s0 + (s1 - s0) * (float)frac) * (float)g;
                }
                cur += step;
            }
        }
        v->cursor = cur;
        if (!v->loop && cur >= (double)nf) zan_voice_reset(v);
    }

    /* Accumulator -> s16 with soft knee limiter */
    d = (short *)dst;
    for (i = 0; i < total; i++) {
        float s = acc[i];
        if (s > 0.5f) {
            s = 0.5f + 0.5f * tanhf((s - 0.5f) * 2.0f);
        } else if (s < -0.5f) {
            s = -0.5f - 0.5f * tanhf((-s - 0.5f) * 2.0f);
        }
        d[i] = (short)(int)(s * 32767.0f);
    }
}

static void zan_audio_mix_f32(float *dst, int frames, int devch) {
    static float acc[ZAN_AUDIO_MAX_FILL * ZAN_AUDIO_MAX_CHANNELS];
    int f, ch, i;
    int total;

    if (frames > ZAN_AUDIO_MAX_FILL) frames = ZAN_AUDIO_MAX_FILL;
    if (devch < 1 || devch > ZAN_AUDIO_MAX_CHANNELS) devch = 2;
    total = (int)frames * devch;
    memset(acc, 0, sizeof(float) * (size_t)total);

    for (i = 0; i < ZAN_VOICE_SLOTS; i++) {
        ZanVoice *v = &zan_voices[i];
        ZanAudioClip *c;
        double cur, step, g;
        int nf, clipch;
        const short *pcm;
        if (!v->active || !v->clip) continue;
        c = v->clip;
        nf = c->frames;
        if (nf <= 0) { zan_voice_reset(v); continue; }
        clipch = c->channels;
        pcm = c->pcm;
        cur = v->cursor;
        step = v->step;
        if (!(step > 0.0)) step = 1.0;
        g = ((double)v->gain * (double)zan_audio_master) / 32767.0;
        if (g != 0.0) {
            for (f = 0; f < frames; f++) {
                int i0, i1;
                double frac;
                if (v->loop) {
                    if (cur >= (double)nf) cur = fmod(cur, (double)nf);
                } else if (cur >= (double)nf) {
                    break;
                }
                i0 = (int)cur;
                frac = cur - (double)i0;
                i1 = i0 + 1;
                if (i1 >= nf) i1 = nf - 1;
                for (ch = 0; ch < devch; ch++) {
                    int cc = ch % clipch;
                    float s0 = (float)pcm[(size_t)i0 * clipch + cc];
                    float s1 = (float)pcm[(size_t)i1 * clipch + cc];
                    acc[(size_t)f * devch + ch] += (s0 + (s1 - s0) * (float)frac) * (float)g;
                }
                cur += step;
            }
        }
        v->cursor = cur;
        if (!v->loop && cur >= (double)nf) zan_voice_reset(v);
    }

    for (i = 0; i < total; i++) {
        float s = acc[i];
        if (s > 0.5f) {
            s = 0.5f + 0.5f * tanhf((s - 0.5f) * 2.0f);
        } else if (s < -0.5f) {
            s = -0.5f - 0.5f * tanhf((-s - 0.5f) * 2.0f);
        }
        dst[i] = s;
    }
}

#endif /* !_WIN32 */
