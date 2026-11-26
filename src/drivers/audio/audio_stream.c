/* audio_stream.c -- 流式 PCM 环形缓冲推流与音频主时钟（供视频播放/实时语音使用） */

#ifndef ZAN_STREAM_SLOTS
#define ZAN_STREAM_SLOTS 16
#endif

typedef struct ZanPcmStream {
    short *ring;            /* s16 interleaved, capacity = cap_frames * channels */
    int cap_frames;
    int freq;
    int channels;
    uint64_t read_pos;      /* 已消费的整帧索引（单调递增，取模 cap_frames 访问 ring） */
    uint64_t write_pos;     /* 已写入的整帧索引 */
    double frac;            /* [0, 1) 小数相位，支持任意采样率重采样与倍速 */
    double played_frames;   /* 自 create/flush 以来已消费源帧数（音频主时钟） */
    float gain;
    float speed;
    int paused;
    int gen;
    int active;
} ZanPcmStream;

static ZanPcmStream zan_streams[ZAN_STREAM_SLOTS];
static int zan_stream_gen = 1;

static void zan_stream_lock(void) {
#ifdef _WIN32
    if (zan_audio_cs_ok) EnterCriticalSection(&zan_audio_cs);
#else
    zan_audio_lock();
#endif
}

static void zan_stream_unlock(void) {
#ifdef _WIN32
    if (zan_audio_cs_ok) LeaveCriticalSection(&zan_audio_cs);
#else
    zan_audio_unlock();
#endif
}

static int64_t zan_stream_pack(int slot, int gen) {
    return ((int64_t)gen << 8) | (int64_t)(slot + 1);
}

static ZanPcmStream *zan_stream_of_locked(int64_t handle) {
    int slot = (int)((handle & 255) - 1);
    int gen = (int)(handle >> 8);
    if (slot < 0 || slot >= ZAN_STREAM_SLOTS) return NULL;
    ZanPcmStream *s = &zan_streams[slot];
    if (!s->active || s->gen != gen) return NULL;
    return s;
}

static void zan_stream_free_locked(ZanPcmStream *s) {
    if (s->ring) {
        free(s->ring);
        s->ring = NULL;
    }
    memset(s, 0, sizeof(*s));
}

static void zan_audio_streams_close_all_locked(void) {
    for (int i = 0; i < ZAN_STREAM_SLOTS; i++) {
        if (zan_streams[i].active || zan_streams[i].ring) {
            zan_stream_free_locked(&zan_streams[i]);
        }
    }
}

static void zan_audio_mix_streams_locked(float *acc, int frames, int devch) {
    int dev_freq = zan_audio_dev_freq > 0 ? zan_audio_dev_freq : 48000;
    for (int i = 0; i < ZAN_STREAM_SLOTS; i++) {
        ZanPcmStream *s = &zan_streams[i];
        if (!s->active || s->paused || !s->ring || s->cap_frames <= 0) continue;
        if (s->write_pos <= s->read_pos) continue;

        double step = ((double)s->freq / (double)dev_freq) * (double)s->speed;
        if (!(step > 0.0)) step = 1.0;
        double g = ((double)s->gain * (double)zan_audio_master) / 32767.0;
        int sch = s->channels;
        int cap = s->cap_frames;

        for (int f = 0; f < frames; f++) {
            uint64_t avail = s->write_pos - s->read_pos;
            if (avail == 0) break;

            uint64_t idx0 = s->read_pos % (uint64_t)cap;
            uint64_t idx1 = (avail > 1) ? ((s->read_pos + 1) % (uint64_t)cap) : idx0;
            float frac = (float)s->frac;

            if (g != 0.0) {
                for (int ch = 0; ch < devch; ch++) {
                    int cc = ch % sch;
                    float s0 = (float)s->ring[(size_t)idx0 * (size_t)sch + (size_t)cc];
                    float s1 = (float)s->ring[(size_t)idx1 * (size_t)sch + (size_t)cc];
                    acc[(size_t)f * (size_t)devch + (size_t)ch] += (s0 + (s1 - s0) * frac) * (float)g;
                }
            }

            double next_pos = s->frac + step;
            uint64_t adv = (uint64_t)next_pos;
            if (adv >= avail) {
                s->played_frames += (double)avail - s->frac;
                s->read_pos = s->write_pos;
                s->frac = 0.0;
                break;
            } else {
                s->read_pos += adv;
                s->frac = next_pos - (double)adv;
                s->played_frames += step;
            }
        }
    }
}

ZAN_AUDIO_EXPORT int64_t zan_audio_stream_create(int32_t sample_rate,
                                                 int32_t channels,
                                                 int32_t buffer_ms) {
    if (sample_rate < 8000 || sample_rate > 192000) return 0;
    if (channels < 1 || channels > ZAN_AUDIO_MAX_CHANNELS) return 0;
    if (buffer_ms < 100) buffer_ms = 500;
    if (buffer_ms > 30000) buffer_ms = 30000;

    int64_t cap64 = ((int64_t)sample_rate * (int64_t)buffer_ms) / 1000;
    if (cap64 < 1024) cap64 = 1024;
    if (cap64 > 192000 * 30) cap64 = 192000 * 30;
    int cap_frames = (int)cap64;

    short *ring = (short *)calloc((size_t)cap_frames * (size_t)channels, sizeof(short));
    if (!ring) return 0;

    zan_stream_lock();
    int slot = -1;
    for (int i = 0; i < ZAN_STREAM_SLOTS; i++) {
        if (!zan_streams[i].active) { slot = i; break; }
    }
    if (slot < 0) {
        zan_stream_unlock();
        free(ring);
        return 0;
    }
    int gen = zan_stream_gen++;
    if (gen <= 0) { gen = 1; zan_stream_gen = 2; }
    ZanPcmStream *s = &zan_streams[slot];
    memset(s, 0, sizeof(*s));
    s->ring = ring;
    s->cap_frames = cap_frames;
    s->freq = sample_rate;
    s->channels = channels;
    s->gain = 1.0f;
    s->speed = 1.0f;
    s->paused = 0;
    s->gen = gen;
    s->active = 1;
    zan_stream_unlock();
    return zan_stream_pack(slot, gen);
}

ZAN_AUDIO_EXPORT void zan_audio_stream_destroy(int64_t handle) {
    zan_stream_lock();
    ZanPcmStream *s = zan_stream_of_locked(handle);
    if (s) zan_stream_free_locked(s);
    zan_stream_unlock();
}

ZAN_AUDIO_EXPORT int32_t zan_audio_stream_write(int64_t handle,
                                                const int16_t *pcm,
                                                int32_t frames) {
    if (!pcm || frames <= 0) return 0;
    zan_stream_lock();
    ZanPcmStream *s = zan_stream_of_locked(handle);
    if (!s || !s->ring) {
        zan_stream_unlock();
        return 0;
    }
    uint64_t used = s->write_pos - s->read_pos;
    uint64_t free_f = (used < (uint64_t)s->cap_frames) ? ((uint64_t)s->cap_frames - used) : 0;
    int32_t to_write = (uint64_t)frames < free_f ? frames : (int32_t)free_f;
    if (to_write > 0) {
        int cap = s->cap_frames;
        int ch = s->channels;
        int w_idx = (int)(s->write_pos % (uint64_t)cap);
        int first = cap - w_idx;
        if (first > to_write) first = to_write;
        memcpy(s->ring + (size_t)w_idx * (size_t)ch,
               pcm,
               (size_t)first * (size_t)ch * sizeof(int16_t));
        int second = to_write - first;
        if (second > 0) {
            memcpy(s->ring,
                   pcm + (size_t)first * (size_t)ch,
                   (size_t)second * (size_t)ch * sizeof(int16_t));
        }
        s->write_pos += (uint64_t)to_write;
    }
    zan_stream_unlock();
    return to_write;
}

ZAN_AUDIO_EXPORT void zan_audio_stream_pause(int64_t handle, int32_t paused) {
    zan_stream_lock();
    ZanPcmStream *s = zan_stream_of_locked(handle);
    if (s) s->paused = paused ? 1 : 0;
    zan_stream_unlock();
}

ZAN_AUDIO_EXPORT void zan_audio_stream_flush(int64_t handle) {
    zan_stream_lock();
    ZanPcmStream *s = zan_stream_of_locked(handle);
    if (s) {
        s->read_pos = 0;
        s->write_pos = 0;
        s->frac = 0.0;
        s->played_frames = 0.0;
    }
    zan_stream_unlock();
}

ZAN_AUDIO_EXPORT void zan_audio_stream_set_gain(int64_t handle, double gain) {
    if (gain < 0.0) gain = 0.0;
    if (gain > 4.0) gain = 4.0;
    zan_stream_lock();
    ZanPcmStream *s = zan_stream_of_locked(handle);
    if (s) s->gain = (float)gain;
    zan_stream_unlock();
}

ZAN_AUDIO_EXPORT void zan_audio_stream_set_speed(int64_t handle, double speed) {
    if (speed < 0.25) speed = 0.25;
    if (speed > 4.0) speed = 4.0;
    zan_stream_lock();
    ZanPcmStream *s = zan_stream_of_locked(handle);
    if (s) s->speed = (float)speed;
    zan_stream_unlock();
}

ZAN_AUDIO_EXPORT int32_t zan_audio_stream_buffered_ms(int64_t handle) {
    zan_stream_lock();
    ZanPcmStream *s = zan_stream_of_locked(handle);
    int32_t ms = 0;
    if (s && s->freq > 0) {
        uint64_t avail = s->write_pos - s->read_pos;
        ms = (int32_t)((avail * 1000ULL) / (uint64_t)s->freq);
    }
    zan_stream_unlock();
    return ms;
}

ZAN_AUDIO_EXPORT int64_t zan_audio_stream_played_ms(int64_t handle) {
    zan_stream_lock();
    ZanPcmStream *s = zan_stream_of_locked(handle);
    int64_t ms = 0;
    if (s && s->freq > 0) {
        ms = (int64_t)((s->played_frames * 1000.0) / (double)s->freq);
    }
    zan_stream_unlock();
    return ms;
}

ZAN_AUDIO_EXPORT int32_t zan_audio_stream_free_frames(int64_t handle) {
    zan_stream_lock();
    ZanPcmStream *s = zan_stream_of_locked(handle);
    int32_t free_f = 0;
    if (s) {
        uint64_t used = s->write_pos - s->read_pos;
        if (used < (uint64_t)s->cap_frames) free_f = (int32_t)((uint64_t)s->cap_frames - used);
    }
    zan_stream_unlock();
    return free_f;
}
