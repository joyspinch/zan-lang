/* audio_clip.c -- 音频片段文件读取与 WAV/OGG 解码器 */

static unsigned char *zan_audio_read_file(const char *path, int *out_len) {
    unsigned char *bytes = NULL;
    *out_len = 0;
    if (!path || !path[0]) { zan_audio_set_err("path is empty"); return NULL; }
#ifdef _WIN32
    {
        int wn = MultiByteToWideChar(CP_UTF8, 0, path, -1, NULL, 0);
        wchar_t *wp;
        HANDLE fh;
        LARGE_INTEGER fsz;
        DWORD got = 0;
        if (wn <= 0) { zan_audio_set_err("bad path encoding"); return NULL; }
        wp = (wchar_t *)malloc((size_t)wn * sizeof(wchar_t));
        if (!wp) { zan_audio_set_err("out of memory"); return NULL; }
        MultiByteToWideChar(CP_UTF8, 0, path, -1, wp, wn);
        fh = CreateFileW(wp, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                         FILE_ATTRIBUTE_NORMAL, NULL);
        free(wp);
        if (fh == INVALID_HANDLE_VALUE) { zan_audio_set_err("file not found"); return NULL; }
        if (!GetFileSizeEx(fh, &fsz) || fsz.QuadPart <= 0
            || fsz.QuadPart > ZAN_AUDIO_MAX_FILE) {
            CloseHandle(fh);
            zan_audio_set_err("file too large or unreadable");
            return NULL;
        }
        bytes = (unsigned char *)malloc((size_t)fsz.QuadPart);
        if (!bytes) { CloseHandle(fh); zan_audio_set_err("out of memory"); return NULL; }
        if (!ReadFile(fh, bytes, (DWORD)fsz.QuadPart, &got, NULL)
            || got != (DWORD)fsz.QuadPart) {
            free(bytes);
            CloseHandle(fh);
            zan_audio_set_err("file read failed");
            return NULL;
        }
        CloseHandle(fh);
        *out_len = (int)got;
        return bytes;
    }
#else
    {
        FILE *f = fopen(path, "rb");
        long sz;
        if (!f) { zan_audio_set_err("file not found"); return NULL; }
        if (fseek(f, 0, SEEK_END) != 0 || (sz = ftell(f)) < 0
            || fseek(f, 0, SEEK_SET) != 0 || sz > ZAN_AUDIO_MAX_FILE) {
            fclose(f);
            zan_audio_set_err("file too large or unreadable");
            return NULL;
        }
        bytes = (unsigned char *)malloc(sz > 0 ? (size_t)sz : 1);
        if (!bytes) { fclose(f); zan_audio_set_err("out of memory"); return NULL; }
        if (sz > 0 && fread(bytes, 1, (size_t)sz, f) != (size_t)sz) {
            free(bytes);
            fclose(f);
            zan_audio_set_err("file read failed");
            return NULL;
        }
        fclose(f);
        *out_len = (int)sz;
        return bytes;
    }
#endif
}

static unsigned int zan_wav_u16(const unsigned char *p) {
    return (unsigned int)p[0] | ((unsigned int)p[1] << 8);
}

static unsigned int zan_wav_u32(const unsigned char *p) {
    return (unsigned int)p[0] | ((unsigned int)p[1] << 8)
         | ((unsigned int)p[2] << 16) | ((unsigned int)p[3] << 24);
}

static short zan_wav_int_to_s16(int bits, const unsigned char *p) {
    if (bits == 8) return (short)(((int)p[0] - 128) * 257);
    if (bits == 16) return (short)(unsigned short)zan_wav_u16(p);
    if (bits == 24) {
        int v = (int)p[0] | ((int)p[1] << 8) | ((int)p[2] << 16);
        if (v & 0x800000) v -= 0x1000000;
        return (short)(v >> 8);
    }
    /* bits == 32: int32 */
    return (short)(int)zan_wav_u32(p) >> 16;
}

static short zan_wav_f32_to_s16(const unsigned char *p) {
    union { unsigned int u; float f; } cvt;
    double d;
    cvt.u = zan_wav_u32(p);
    d = (double)cvt.f;
    if (d > 1.0) d = 1.0;
    if (d < -1.0) d = -1.0;
    return (short)(int)(d * 32767.0);
}

static int zan_wav_parse(const unsigned char *b, int n, ZanAudioClip *c) {
    const unsigned char *fmt = NULL;
    const unsigned char *data = NULL;
    int fmt_len = 0, data_len = 0;
    int fmt_tag = 0, fmt_bits = 0, fmt_ch = 0, fmt_freq = 0;
    int64_t pos;

    if (n < 12 || memcmp(b, "RIFF", 4) != 0 || memcmp(b + 8, "WAVE", 4) != 0) {
        zan_audio_set_err("not a RIFF/WAVE file");
        return 0;
    }
    pos = 12;
    while (pos + 8 <= n) {
        uint32_t len = zan_wav_u32(b + pos + 4);
        if (len >= 0x80000000u) {
            zan_audio_set_err("wav chunk length out of range");
            return 0;
        }
        if (memcmp(b + pos, "fmt ", 4) == 0 && !fmt && len >= 16
            && pos + 8 + (int64_t)len <= n) {
            fmt = b + pos + 8;
            fmt_len = (int)len;
        } else if (memcmp(b + pos, "data", 4) == 0 && !data) {
            data_len = (int)len;
            if (pos + 8 + (int64_t)data_len > n) data_len = (int)(n - pos - 8);
            data = b + pos + 8;
        }
        if (fmt && data) break;
        pos += 8 + (int64_t)((len + 1u) & ~1u);
    }
    if (!fmt || !data || data_len <= 0) {
        zan_audio_set_err("wav fmt/data chunk missing or empty");
        return 0;
    }
    fmt_tag = (int)zan_wav_u16(fmt);
    fmt_ch = (int)zan_wav_u16(fmt + 2);
    fmt_freq = (int)zan_wav_u32(fmt + 4);
    fmt_bits = (int)zan_wav_u16(fmt + 14);
    if (fmt_tag == 0xFFFE) {
        unsigned int sub;
        if (fmt_len < 40) {
            zan_audio_set_err("wav extensible fmt chunk too small");
            return 0;
        }
        sub = zan_wav_u32(fmt + 24);
        if (sub == 1) fmt_tag = 1;
        else if (sub == 3) fmt_tag = 3;
        else {
            zan_audio_set_err("wav extensible subtype unsupported");
            return 0;
        }
    }
    if (fmt_tag == 3 && fmt_bits != 32) {
        zan_audio_set_err("wav float must be 32-bit");
        return 0;
    }
    if (fmt_tag == 1 && fmt_bits != 8 && fmt_bits != 16 && fmt_bits != 24 && fmt_bits != 32) {
        zan_audio_set_err("wav pcm bit depth unsupported");
        return 0;
    }
    if (fmt_ch < 1 || fmt_ch > ZAN_AUDIO_MAX_CHANNELS) {
        zan_audio_set_err("wav channel count unsupported");
        return 0;
    }
    if (fmt_freq < 1 || fmt_freq > 384000) {
        zan_audio_set_err("wav sample rate out of range");
        return 0;
    }
    {
        int frame_bytes = (fmt_bits / 8) * fmt_ch;
        int frames;
        int64_t total_samples;
        short *pcm;
        if (frame_bytes <= 0 || data_len % frame_bytes != 0) {
            zan_audio_set_err("wav data size not frame aligned");
            return 0;
        }
        frames = data_len / frame_bytes;
        if (frames <= 0) {
            zan_audio_set_err("wav has no samples");
            return 0;
        }
        total_samples = (int64_t)frames * (int64_t)fmt_ch;
        if (total_samples <= 0 || total_samples > (int64_t)(SIZE_MAX / sizeof(short)) || total_samples > 0x7FFFFFFFLL) {
            zan_audio_set_err("wav sample count out of range");
            return 0;
        }
        pcm = (short *)malloc((size_t)total_samples * sizeof(short));
        if (!pcm) { zan_audio_set_err("out of memory"); return 0; }
        for (int64_t i = 0; i < total_samples; i++) {
            const unsigned char *s = data + (size_t)i * (size_t)(fmt_bits / 8);
            pcm[i] = (fmt_tag == 3) ? zan_wav_f32_to_s16(s)
                                    : zan_wav_int_to_s16(fmt_bits, s);
        }
        c->pcm = pcm;
        c->frames = frames;
        c->freq = fmt_freq;
        c->channels = fmt_ch;
        return 1;
    }
}

static int zan_ogg_decode(const unsigned char *b, int n, ZanAudioClip *c) {
    int channels = 0, freq = 0;
    short *decoded = NULL;
    int total = stb_vorbis_decode_memory(b, n, &channels, &freq, &decoded);
    if (total <= 0 || !decoded || channels <= 0 || freq <= 0) {
        if (decoded) free(decoded);
        zan_audio_set_err("ogg decode failed");
        return 0;
    }
    c->pcm = decoded;
    c->frames = total / channels;
    c->freq = freq;
    c->channels = channels;
    return 1;
}
