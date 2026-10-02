/* ===================================================================
 * zan_audio — native zero-dependency audio runtime, part of zan_gui.
 *
 * Native zero-dependency replacement for the old audio bridge:
 * zan_audio_open, every voice mixed by our own background thread, so
 * a clip can sound several times at once. Clips are fully decoded
 * into s16 PCM up front (WAV parsed here, OGG Vorbis via the vendored
 * stb_vorbis); the mixer resamples with linear interpolation and
 * converts to the device mix format.
 *
 * Platforms:
 *   Windows       WASAPI shared mode, event-driven, Windows 7+ (COM
 *                 activation only -- no mmdevapi.lib import, ole32 was
 *                 already linked for the shell).
 *   Android       AAudio (API 26+): the stream builder hands us the
 *                 platform's own callback thread, so there is no mixer
 *                 thread of ours to manage.
 *   Linux         ALSA (libasound.so.2 dynamic loading with worker thread).
 *   macOS         CoreAudio AudioUnit (AudioToolbox framework dynamic loading).
 *   OpenHarmony   OH Audio (libohaudio.so dynamic loading via OH_AudioStreamBuilder).
 *
 * Voice handles carry a generation number like the SDL bridge did:
 * a Zan AudioVoice outliving its sound answers "not playing" instead
 * of dereferencing a recycled slot.
 *
 * This file is #included at the end of gui_runtime.c (single-TU
 * convention -- see gui_gl_backend.c / gui_runtime_android.c), so it
 * relies on the host for windows.h and EXPORT, and re-includes the
 * standard headers idempotently anyway to stay readable standalone.
 * =================================================================== */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* Standalone-TU compile (the static driver archive builds this file as its
 * own member so the linker drops it for programs that never touch audio):
 * provide the two things the single-TU host used to supply. windows.h is
 * idempotent, so re-including it after the host is harmless. */
#ifdef _WIN32
#include <windows.h>
#endif
#ifndef EXPORT
#define EXPORT
#endif

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600 /* WASAPI is Vista+; runtime-checked path is Win7+ */
#endif
/* COBJMACROS/CINTERFACE: use WASAPI through the C macro wrappers
 * (IAudioClient_Initialize etc.) -- the runtime is plain C. */
#ifndef COBJMACROS
#define COBJMACROS
#endif
#ifndef CINTERFACE
#define CINTERFACE
#endif
#include <mmreg.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#endif

#ifndef _WIN32
#include <pthread.h>
#include <dlfcn.h>
#include <unistd.h>
#endif

#ifdef __ANDROID__
/* AAudio is API 26+; the driver targets android-28 so the header is
 * always available. Mixing runs on the stream's own callback thread,
 * serialized against the API thread by zan_audio_mutex. */
#include <aaudio/AAudio.h>
#endif

/* OGG Vorbis (background music): stb_vorbis single-file implementation,
 * whole clip decoded to memory PCM; the looping voice and WAV share the
 * same mixing path. pushdata streaming API and stdio file access are
 * unused -- off to keep size (files are read through zan_audio_read_file
 * so UTF-8 paths work on Windows). */
#define STB_VORBIS_NO_PUSHDATA_API
#define STB_VORBIS_NO_STDIO
#include "stb_vorbis.c"

#ifndef ZAN_VOICE_SLOTS
#define ZAN_VOICE_SLOTS 256
#endif
/* Upper bound of one mixer fill in device frames; larger availabilities
 * are filled in chunks of at most this. */
#define ZAN_AUDIO_MAX_FILL 4096
#define ZAN_AUDIO_MAX_CHANNELS 8
#define ZAN_AUDIO_MAX_FILE (256 * 1024 * 1024)

typedef struct ZanAudioClip {
    short *pcm;   /* s16 interleaved */
    int frames;   /* per-channel frame count */
    int freq;
    int channels;
} ZanAudioClip;

typedef struct ZanVoice {
    ZanAudioClip *clip;
    double cursor; /* read position in clip frames (fractional) */
    double step;   /* clip frames per device frame = clip->freq / dev_freq */
    float gain;
    int loop;
    int gen;       /* generation; bumped when the slot is handed out again */
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
static HANDLE zan_audio_stop_evt;   /* manual reset: mixer must exit */
static HANDLE zan_audio_fill_evt;   /* auto reset: WASAPI period event */
static HANDLE zan_audio_init_evt;   /* manual reset: thread startup report */
static IAudioClient *zan_audio_client;
static IAudioRenderClient *zan_audio_render;
static UINT32 zan_audio_buf_frames;
static int zan_audio_dev_freq;
static int zan_audio_dev_channels;
/* Output sample encoding of the mix format: 0 = float32, 1 = s16,
 * 2 = s32, 3 = s24 (packed, 3 bytes per sample). */
static int zan_audio_dev_fmt;
#endif

#ifndef _WIN32
static pthread_mutex_t zan_audio_mutex = PTHREAD_MUTEX_INITIALIZER;
static int zan_audio_dev_freq;      /* device sample rate */
static int zan_audio_dev_channels;  /* device channel count (1/2) */
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

/* The live voice a handle names, or NULL once its sound ended (or the
 * slot was handed to a newer voice). */
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

/* Caller holds zan_audio_mutex (the AAudio callback thread already
 * owns it while mixing; the API thread takes it for table edits). */
static void zan_voice_reap_locked(void) {
    int i;
    for (i = 0; i < ZAN_VOICE_SLOTS; i++) {
        ZanVoice *v = &zan_voices[i];
        if (!v->active || v->loop || !v->clip) continue;
        if (v->cursor >= (double)v->clip->frames) zan_voice_reset(v);
    }
}

/* ------------------------------------------------------------------
 * Whole-file reader with UTF-8 paths (same convention as the image
 * loader in gui_runtime.c: Windows fopen is ANSI-codepage and mangles
 * non-ASCII paths, so read through the wide API there).
 * =================================================================== */

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

/* ------------------------------------------------------------------
 * WAV parsing (RIFF/WAVE -> s16). Accepts PCM 8/16/24/32-bit, IEEE
 * float 32-bit, and WAVE_FORMAT_EXTENSIBLE carrying either subtype.
 * =================================================================== */

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
    if (bits == 24) { /* sign-extend, drop the low byte */
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

/* Parses a whole WAV file image into `c` (allocating c->pcm). Returns 1
 * on success, 0 with a reason in zan_audio_err otherwise. */
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
        /* Chunk lengths are u32 in the spec. Everything they feed -- the
         * in-bounds test, the data clamp and the word-aligned skip -- must
         * stay unsigned/wide: a (int) cast turns len >= 2^31 negative, the
         * fmt bound tautologically true and the skip lands pos near -2 GiB,
         * walking the loop off the front of the buffer (B-ID71). */
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
            if (pos + 8 + (int64_t)data_len > n) data_len = (int)(n - pos - 8); /* tolerate lying headers */
            data = b + pos + 8;
        }
        if (fmt && data) break;
        pos += 8 + (int64_t)((len + 1u) & ~1u); /* chunks are word-aligned */
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
        /* WAVE_FORMAT_EXTENSIBLE: the real format tag is the first two
         * bytes of the SubFormat GUID at offset 24. */
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
        int frames, i;
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
        pcm = (short *)malloc((size_t)frames * (size_t)fmt_ch * sizeof(short));
        if (!pcm) { zan_audio_set_err("out of memory"); return 0; }
        for (i = 0; i < frames * fmt_ch; i++) {
            const unsigned char *s = data + (size_t)i * (fmt_bits / 8);
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

/* Decodes a whole OGG Vorbis image to s16 PCM. stb_vorbis allocates on
 * the CRT heap and the clip is freed with free(), so the decoded buffer
 * is handed over directly -- no copy needed. Returns 1 on success. */
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

/* ------------------------------------------------------------------
 * Mixer (Windows). Runs on the audio thread; the critical section
 * covers the voice table and clip lifetimes. Mixing up to 64 voices at
 * a 10ms period is tens of microseconds, so holding the section
 * through the fill is fine and keeps lifetime reasoning trivial.
 * =================================================================== */

#ifdef _WIN32

static void zan_audio_mix(unsigned char *dst, UINT32 frames) {
    /* 4096 frames * 8 channels * 4 bytes = 128 KiB static accumulator;
     * the audio thread is the only user. */
    static float acc[ZAN_AUDIO_MAX_FILL * ZAN_AUDIO_MAX_CHANNELS];
    int f, ch, i;
    int devch = zan_audio_dev_channels;
    int total;

    if (frames > ZAN_AUDIO_MAX_FILL) frames = ZAN_AUDIO_MAX_FILL;
    /* Three-way clamp, same as mix_s16/mix_f32 below: a bogus device count
     * (0 or a hostile >MAX_CHANNELS) would otherwise size the static
     * accumulator past its ZAN_AUDIO_MAX_CHANNELS stride (B-ID71). */
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
                    while (cur >= (double)nf) cur -= (double)nf;
                } else if (cur >= (double)nf) {
                    break; /* played out; the rest of the buffer stays silent */
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

    /* Accumulator -> device encoding, with a hard clamp at full scale. */
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

static DWORD WINAPI zan_audio_thread_proc(LPVOID param) {
    /* GUIDs use explicit initializers so no uuid.lib import is needed
     * (keeps the GNU import-lib driver workflow unchanged). */
    static const CLSID clsid_mmdevice = {0xBCDE0395,0xE52F,0x467C,
        {0x8E,0x3D,0xC4,0x57,0x92,0x91,0x69,0x2E}};
    static const IID iid_immdevice_enum = {0xA95664D2,0x9614,0x4F35,
        {0xA7,0x46,0xDE,0x8D,0xB6,0x36,0x17,0xE6}};
    static const IID iid_iaudioclient = {0x1CB9AD4C,0xDBFA,0x4C32,
        {0xB1,0x78,0xC2,0xF5,0x68,0xA7,0x03,0xB2}};
    static const IID iid_irenderclient = {0xF294ACFC,0x3146,0x4483,
        {0xA7,0xBF,0xAD,0xDC,0xA7,0xC2,0x60,0xE2}};
    IMMDeviceEnumerator *enumr = NULL;
    IMMDevice *dev = NULL;
    WAVEFORMATEX *wf = NULL;
    HANDLE evs[2];
    (void)param;

    evs[0] = zan_audio_stop_evt;
    evs[1] = zan_audio_fill_evt;

    if (FAILED(CoInitializeEx(NULL, COINIT_MULTITHREADED))) {
        zan_audio_set_err("CoInitializeEx failed");
        SetEvent(zan_audio_init_evt);
        return 0;
    }
    if (FAILED(CoCreateInstance(&clsid_mmdevice, NULL, CLSCTX_ALL,
                                &iid_immdevice_enum, (void **)&enumr))
        || !enumr) {
        zan_audio_set_err("MMDeviceEnumerator unavailable");
        CoUninitialize();
        SetEvent(zan_audio_init_evt);
        return 0;
    }
    if (FAILED(IMMDeviceEnumerator_GetDefaultAudioEndpoint(
                enumr, eRender, eConsole, &dev)) || !dev) {
        zan_audio_set_err("no default audio output device");
        IMMDeviceEnumerator_Release(enumr);
        CoUninitialize();
        SetEvent(zan_audio_init_evt);
        return 0;
    }
    if (FAILED(IMMDevice_Activate(dev, &iid_iaudioclient, CLSCTX_ALL,
                                  NULL, (void **)&zan_audio_client))
        || !zan_audio_client) {
        zan_audio_set_err("IAudioClient activate failed");
        goto thread_fail;
    }
    if (FAILED(IAudioClient_GetMixFormat(zan_audio_client, &wf)) || !wf) {
        zan_audio_set_err("GetMixFormat failed");
        goto thread_fail;
    }
    /* Classify the mix format; shared mode on Vista+ is float32 in
     * practice, but accept the s16/s32/s24 encodings too. */
    {
        int tag = wf->wFormatTag;
        int bits = wf->wBitsPerSample;
        if (tag == WAVE_FORMAT_EXTENSIBLE
            && wf->cbSize >= sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX)) {
            unsigned int sub = ((WAVEFORMATEXTENSIBLE *)wf)->SubFormat.Data1;
            if (sub == 3) tag = 3;
            else if (sub == 1) tag = 1;
        }
        if (tag == 3 && bits == 32) zan_audio_dev_fmt = 0;
        else if (tag == 1 && bits == 16) zan_audio_dev_fmt = 1;
        else if (tag == 1 && bits == 32) zan_audio_dev_fmt = 2;
        else if (tag == 1 && bits == 24) zan_audio_dev_fmt = 3;
        else {
            zan_audio_set_err("unsupported device mix format");
            goto thread_fail;
        }
        zan_audio_dev_freq = (int)wf->nSamplesPerSec;
        zan_audio_dev_channels = (int)wf->nChannels;
    }
    /* 100ms buffer, event-driven: the event fires every device period and
     * the wait timeout below doubles as a poll fallback for quirky
     * drivers that never fire it. */
    if (FAILED(IAudioClient_Initialize(zan_audio_client,
                AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                1000000 /* 100ms in 100ns units */, 0, wf, NULL))) {
        zan_audio_set_err("IAudioClient Initialize failed");
        goto thread_fail;
    }
    if (FAILED(IAudioClient_SetEventHandle(zan_audio_client, zan_audio_fill_evt))) {
        zan_audio_set_err("SetEventHandle failed");
        goto thread_fail;
    }
    if (FAILED(IAudioClient_GetBufferSize(zan_audio_client, &zan_audio_buf_frames))
        || zan_audio_buf_frames == 0) {
        zan_audio_set_err("GetBufferSize failed");
        goto thread_fail;
    }
    if (FAILED(IAudioClient_GetService(zan_audio_client, &iid_irenderclient,
                                       (void **)&zan_audio_render))
        || !zan_audio_render) {
        zan_audio_set_err("IAudioRenderClient unavailable");
        goto thread_fail;
    }
    if (FAILED(IAudioClient_Start(zan_audio_client))) {
        zan_audio_set_err("IAudioClient Start failed");
        goto thread_fail;
    }
    CoTaskMemFree(wf);
    wf = NULL;

    SetEvent(zan_audio_init_evt); /* open() may proceed */
    for (;;) {
        DWORD w = WaitForMultipleObjects(2, evs, FALSE, 2000);
        UINT32 padding = 0, avail;
        BYTE *dst = NULL;
        if (w == WAIT_OBJECT_0) break;                 /* stop event */
        if (w != WAIT_OBJECT_0 + 1 && w != WAIT_TIMEOUT) break;
        if (!zan_audio_ready) break;                   /* torn down while waiting */
        if (FAILED(IAudioClient_GetCurrentPadding(zan_audio_client, &padding)))
            continue;
        if (padding >= zan_audio_buf_frames) continue;
        avail = zan_audio_buf_frames - padding;
        while (avail > 0) {
            UINT32 chunk = avail > ZAN_AUDIO_MAX_FILL ? ZAN_AUDIO_MAX_FILL : avail;
            if (FAILED(IAudioRenderClient_GetBuffer(zan_audio_render, chunk, &dst)))
                break;
            zan_audio_mix(dst, chunk);
            IAudioRenderClient_ReleaseBuffer(zan_audio_render, chunk, 0);
            avail -= chunk;
        }
    }
    IAudioClient_Stop(zan_audio_client);
    if (wf) CoTaskMemFree(wf);
    if (zan_audio_render) { IAudioRenderClient_Release(zan_audio_render); zan_audio_render = NULL; }
    if (zan_audio_client) { IAudioClient_Release(zan_audio_client); zan_audio_client = NULL; }
    IMMDevice_Release(dev);
    IMMDeviceEnumerator_Release(enumr);
    CoUninitialize();
    return 0;

thread_fail:
    if (wf) CoTaskMemFree(wf);
    zan_audio_client = NULL;
    zan_audio_render = NULL;
    IMMDevice_Release(dev);
    IMMDeviceEnumerator_Release(enumr);
    CoUninitialize();
    SetEvent(zan_audio_init_evt); /* open() will see the failure */
    return 0;
}

#endif /* _WIN32 */

/* ------------------------------------------------------------------
 * Mixer (Android/AAudio). The AAudio callback thread is the mixer:
 * it holds zan_audio_mutex for the whole fill, so table edits from
 * the game thread (play/stop/free) serialize against it the same way
 * the WASAPI critical section did. The stream is opened as s16 (the
 * one output format every OEM HAL takes; float paths on some devices
 * come out as heavy noise), so the mix lands in the stream buffer
 * directly with no conversion pass after it.
 * =================================================================== */

#ifndef _WIN32

static void zan_audio_mix_s16(unsigned char *dst, int frames, int devch) {
    /* 4096 frames * 8 channels * 4 bytes = 128 KiB static accumulator;
     * the audio callback thread is the only user. */
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
                    while (cur >= (double)nf) cur -= (double)nf;
                } else if (cur >= (double)nf) {
                    break; /* played out; the rest of the buffer stays silent */
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

    /* Accumulator -> s16. A soft knee (tanh over the top 6 dB) instead
     * of a hard clamp: stacked one-shots that sum past full scale come
     * out as loud-but-clean instead of square-wave clipping, which is
     * the "heavy static" heard when several 0.5-0.8 gain voices
     * overlap. */
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
                    while (cur >= (double)nf) cur -= (double)nf;
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

#ifdef __ANDROID__

/* AAudio callback thread: the stream was opened as s16 (see open), so
 * the mixed s16 frames go straight into the stream buffer. The device
 * format is read under the mutex alongside the mix, so an open/close
 * racing the callback can never hand it a half-updated format. */
static aaudio_data_callback_result_t zan_audio_aa_callback(
        AAudioStream *stream, void *userData, void *audioData,
        int32_t numFrames) {
    (void)stream; (void)userData;
    if (numFrames <= 0) return AAUDIO_CALLBACK_RESULT_CONTINUE;
    if (numFrames > ZAN_AUDIO_MAX_FILL) numFrames = ZAN_AUDIO_MAX_FILL;
    pthread_mutex_lock(&zan_audio_mutex);
    if (!zan_audio_ready || zan_audio_stream != stream) {
        pthread_mutex_unlock(&zan_audio_mutex);
        memset(audioData, 0,
               (size_t)numFrames * 2 * sizeof(short));
        return AAUDIO_CALLBACK_RESULT_CONTINUE;
    }
    zan_audio_mix_s16((unsigned char *)audioData, numFrames,
                      zan_audio_dev_channels);
    pthread_mutex_unlock(&zan_audio_mutex);
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
    /* s16 output, rate and channel count pinned: s16 is the one format
     * every OEM HAL accepts (the float path misrenders to heavy noise
     * on several devices), and leaving rate/channels unset lets the
     * builder hand back anything the platform fancies -- play() divides
     * clip rates by the reported rate, so a surprise value resamples
     * every clip to the wrong pitch. 48000 stereo is the universal
     * Android output shape. */
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
        pthread_mutex_lock(&zan_audio_mutex);
        if (!zan_audio_ready) {
            pthread_mutex_unlock(&zan_audio_mutex);
            break;
        }
        zan_audio_mix_s16((unsigned char *)buf, 1024, zan_audio_dev_channels);
        pthread_mutex_unlock(&zan_audio_mutex);

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

    pthread_mutex_lock(&zan_audio_mutex);
    if (!zan_audio_ready) {
        pthread_mutex_unlock(&zan_audio_mutex);
        memset(dst, 0, (size_t)inNumberFrames * 2 * sizeof(float));
        return 0;
    }
    zan_audio_mix_f32(dst, (int)inNumberFrames, zan_audio_dev_channels);
    pthread_mutex_unlock(&zan_audio_mutex);
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

    /* Set Stream Format: 44.1kHz, 2-ch float32 interleaved */
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

    /* Set Render Callback: kAudioUnitProperty_SetRenderCallback = 26, kAudioUnitScope_Input = 1 */
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
    pthread_mutex_lock(&zan_audio_mutex);
    if (!zan_audio_ready) {
        pthread_mutex_unlock(&zan_audio_mutex);
        memset(buffer, 0, (size_t)length);
        return 0;
    }
    zan_audio_mix_s16((unsigned char *)buffer, frames, zan_audio_dev_channels);
    pthread_mutex_unlock(&zan_audio_mutex);
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

/* ------------------------------------------------------------------
 * Exported API. Signatures match the original audio bridge's
 * audio entries so the Zan-side Audio module is a drop-in swap of the
 * DllImport target.
 * =================================================================== */

EXPORT int32_t zan_audio_open(void) {
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
    /* Fresh device session: drop any stale voice state. */
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
    /* Wait for the thread to report startup (success sets
     * zan_audio_client, failure sets the error string). */
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
    /* Reset voice state under the mutex: the callback may already run
     * once the stream starts. */
    pthread_mutex_lock(&zan_audio_mutex);
    for (int i = 0; i < ZAN_VOICE_SLOTS; i++) zan_voice_reset(&zan_voices[i]);
    zan_audio_dev_freq = AAudioStream_getSampleRate(s);
    zan_audio_dev_channels = AAudioStream_getChannelCount(s);
    if (zan_audio_dev_freq <= 0) zan_audio_dev_freq = 48000;
    if (zan_audio_dev_channels <= 0) zan_audio_dev_channels = 2;
    zan_audio_stream = s;
    zan_audio_ready = 1; /* visible to the callback before Start */
    pthread_mutex_unlock(&zan_audio_mutex);
    if (AAudioStream_requestStart(s) != AAUDIO_OK) {
        pthread_mutex_lock(&zan_audio_mutex);
        zan_audio_ready = 0;
        zan_audio_stream = NULL;
        pthread_mutex_unlock(&zan_audio_mutex);
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
    pthread_mutex_lock(&zan_audio_mutex);
    for (int i = 0; i < ZAN_VOICE_SLOTS; i++) zan_voice_reset(&zan_voices[i]);
    zan_audio_ready = 1;
    pthread_mutex_unlock(&zan_audio_mutex);
    return 1;
#elif defined(__APPLE__)
    if (zan_audio_ready) return 1;
    zan_audio_set_err(NULL);
    if (!zan_coreaudio_start()) {
        if (zan_audio_err[0] == 0) zan_audio_set_err("CoreAudio device open failed");
        return 0;
    }
    pthread_mutex_lock(&zan_audio_mutex);
    for (int i = 0; i < ZAN_VOICE_SLOTS; i++) zan_voice_reset(&zan_voices[i]);
    zan_audio_ready = 1;
    pthread_mutex_unlock(&zan_audio_mutex);
    return 1;
#elif defined(__OHOS__)
    if (zan_audio_ready) return 1;
    zan_audio_set_err(NULL);
    if (!zan_ohaudio_start()) {
        if (zan_audio_err[0] == 0) zan_audio_set_err("OH Audio device open failed");
        return 0;
    }
    pthread_mutex_lock(&zan_audio_mutex);
    for (int i = 0; i < ZAN_VOICE_SLOTS; i++) zan_voice_reset(&zan_voices[i]);
    zan_audio_ready = 1;
    pthread_mutex_unlock(&zan_audio_mutex);
    return 1;
#else
    zan_audio_set_err("audio backend not available on this platform yet (planned: OH Audio / OpenSL)");
    return 0;
#endif
}

EXPORT void zan_audio_close(void) {
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
    pthread_mutex_lock(&zan_audio_mutex);
    zan_audio_ready = 0;
    pthread_mutex_unlock(&zan_audio_mutex);
    if (zan_audio_stream) {
        /* close() blocks until the callback thread drains; the ready=0
         * above already silenced it, so the drain is silent too. */
        AAudioStream_close(zan_audio_stream);
        zan_audio_stream = NULL;
    }
    pthread_mutex_lock(&zan_audio_mutex);
    for (int i = 0; i < ZAN_VOICE_SLOTS; i++) zan_voice_reset(&zan_voices[i]);
    pthread_mutex_unlock(&zan_audio_mutex);
#elif defined(__linux__)
    pthread_mutex_lock(&zan_audio_mutex);
    zan_audio_ready = 0;
    pthread_mutex_unlock(&zan_audio_mutex);
    zan_alsa_stop();
    pthread_mutex_lock(&zan_audio_mutex);
    for (int i = 0; i < ZAN_VOICE_SLOTS; i++) zan_voice_reset(&zan_voices[i]);
    pthread_mutex_unlock(&zan_audio_mutex);
#elif defined(__APPLE__)
    pthread_mutex_lock(&zan_audio_mutex);
    zan_audio_ready = 0;
    pthread_mutex_unlock(&zan_audio_mutex);
    zan_coreaudio_stop();
    pthread_mutex_lock(&zan_audio_mutex);
    for (int i = 0; i < ZAN_VOICE_SLOTS; i++) zan_voice_reset(&zan_voices[i]);
    pthread_mutex_unlock(&zan_audio_mutex);
#elif defined(__OHOS__)
    pthread_mutex_lock(&zan_audio_mutex);
    zan_audio_ready = 0;
    pthread_mutex_unlock(&zan_audio_mutex);
    zan_ohaudio_stop();
    pthread_mutex_lock(&zan_audio_mutex);
    for (int i = 0; i < ZAN_VOICE_SLOTS; i++) zan_voice_reset(&zan_voices[i]);
    pthread_mutex_unlock(&zan_audio_mutex);
#else
    zan_audio_ready = 0;
#endif
}

EXPORT int32_t zan_audio_is_open(void) {
    return zan_audio_ready ? 1 : 0;
}

EXPORT void zan_audio_set_volume(double volume) {
    if (volume < 0.0) volume = 0.0;
    zan_audio_master = (float)volume;
}

EXPORT double zan_audio_volume(void) {
    return (double)zan_audio_master;
}

EXPORT const char *zan_audio_driver_name(void) {
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

EXPORT int32_t zan_audio_active_voices(void) {
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
    pthread_mutex_lock(&zan_audio_mutex);
    zan_voice_reap_locked();
    for (i = 0; i < ZAN_VOICE_SLOTS; i++)
        if (zan_voices[i].active && zan_voices[i].clip) n++;
    pthread_mutex_unlock(&zan_audio_mutex);
    return n;
#endif
    for (i = 0; i < ZAN_VOICE_SLOTS; i++)
        if (zan_voices[i].active && zan_voices[i].clip) n++;
    return n;
}

EXPORT void zan_audio_stop_all(void) {
    int i;
#if defined(_WIN32)
    if (zan_audio_cs_ok) {
        EnterCriticalSection(&zan_audio_cs);
        for (i = 0; i < ZAN_VOICE_SLOTS; i++) zan_voice_reset(&zan_voices[i]);
        LeaveCriticalSection(&zan_audio_cs);
        return;
    }
#else
    pthread_mutex_lock(&zan_audio_mutex);
    for (i = 0; i < ZAN_VOICE_SLOTS; i++) zan_voice_reset(&zan_voices[i]);
    pthread_mutex_unlock(&zan_audio_mutex);
    return;
#endif
    for (i = 0; i < ZAN_VOICE_SLOTS; i++) zan_voice_reset(&zan_voices[i]);
}

EXPORT int64_t zan_audio_load_wav(const char *path) {
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

EXPORT int64_t zan_audio_load_wav_mem(const void *data, int32_t len) {
    ZanAudioClip *c;
    int ok;
    zan_audio_set_err(NULL);
    if (!data || len <= 0) { zan_audio_set_err("no data"); return 0; }
    c = (ZanAudioClip *)calloc(1, sizeof(ZanAudioClip));
    if (!c) { zan_audio_set_err("out of memory"); return 0; }
    ok = zan_wav_parse((const unsigned char *)data, len, c);
    if (!ok) { free(c); return 0; }
    return zan_handle_of(c);
}

EXPORT int64_t zan_audio_load_ogg(const char *path) {
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

EXPORT int64_t zan_audio_load_ogg_mem(const void *data, int32_t len) {
    ZanAudioClip *c;
    int ok;
    zan_audio_set_err(NULL);
    if (!data || len <= 0) { zan_audio_set_err("no data"); return 0; }
    c = (ZanAudioClip *)calloc(1, sizeof(ZanAudioClip));
    if (!c) { zan_audio_set_err("out of memory"); return 0; }
    ok = zan_ogg_decode((const unsigned char *)data, len, c);
    if (!ok) { free(c); return 0; }
    return zan_handle_of(c);
}

EXPORT void zan_audio_free_clip(int64_t clip_handle) {
    ZanAudioClip *c = (ZanAudioClip *)zan_ptr_of(clip_handle);
    int i;
    if (!c) return;
#if defined(_WIN32)
    if (zan_audio_cs_ok) EnterCriticalSection(&zan_audio_cs);
#else
    pthread_mutex_lock(&zan_audio_mutex);
#endif
    /* Voices reading this clip's samples have to go first. */
    for (i = 0; i < ZAN_VOICE_SLOTS; i++)
        if (zan_voices[i].clip == c) zan_voice_reset(&zan_voices[i]);
#if defined(_WIN32)
    if (zan_audio_cs_ok) LeaveCriticalSection(&zan_audio_cs);
#else
    pthread_mutex_unlock(&zan_audio_mutex);
#endif
    free(c->pcm);
    free(c);
}

EXPORT int32_t zan_audio_clip_frequency(int64_t clip_handle) {
    ZanAudioClip *c = (ZanAudioClip *)zan_ptr_of(clip_handle);
    return c ? c->freq : 0;
}

EXPORT int32_t zan_audio_clip_channels(int64_t clip_handle) {
    ZanAudioClip *c = (ZanAudioClip *)zan_ptr_of(clip_handle);
    return c ? c->channels : 0;
}

EXPORT int32_t zan_audio_clip_duration_ms(int64_t clip_handle) {
    ZanAudioClip *c = (ZanAudioClip *)zan_ptr_of(clip_handle);
    if (!c || c->freq <= 0 || c->frames <= 0) return 0;
    return (int32_t)(((int64_t)c->frames * 1000) / (int64_t)c->freq);
}

/* Starts one voice for `clip`. `loop` re-queues the clip forever
 * (background music); a one-shot voice is reaped once it has played
 * out. Returns 0 when the device is closed, the pool is full or the
 * clip is invalid. */
EXPORT int64_t zan_audio_play(int64_t clip_handle, double gain, int32_t loop) {
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
    pthread_mutex_lock(&zan_audio_mutex);
    zan_voice_reap_locked();
#endif
    for (i = 0; i < ZAN_VOICE_SLOTS; i++) {
        if (!zan_voices[i].active && !zan_voices[i].clip) { slot = i; break; }
    }
    if (slot < 0) {
#if defined(_WIN32)
        LeaveCriticalSection(&zan_audio_cs);
#else
        pthread_mutex_unlock(&zan_audio_mutex);
#endif
        return 0;
    }
    v = &zan_voices[slot];
    zan_voice_reset(v);
    v->clip = c;
    v->loop = loop != 0 ? 1 : 0;
    v->gen = zan_voice_gen++;
    if (v->gen > 0x1FFFFF) v->gen = 1; /* keep the packed handle small */
    v->gain = gain < 0.0 ? 0.0f : (float)gain;
    v->cursor = 0.0;
    /* zan_audio_dev_freq is set by the open path that armed
     * zan_audio_ready (WASAPI thread startup / AAudio stream open / ALSA / CoreAudio), so
     * play() only reaches the resample step on a live device. The
     * fallback keeps a benign value instead of dividing by zero. */
    v->step = (zan_audio_dev_freq > 0) ? ((double)c->freq / (double)zan_audio_dev_freq) : 1.0;
    if (!(v->step > 0.0)) v->step = 1.0;
    v->active = 1;
#if defined(_WIN32)
    LeaveCriticalSection(&zan_audio_cs);
#else
    pthread_mutex_unlock(&zan_audio_mutex);
#endif
    return zan_voice_pack(slot, v->gen);
}

EXPORT int32_t zan_audio_voice_playing(int64_t voice) {
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
    pthread_mutex_lock(&zan_audio_mutex);
    {
        ZanVoice *v = zan_voice_of(voice);
        if (!v || !v->clip) playing = 0;
        else if (v->loop) playing = 1;
        else if (v->cursor < (double)v->clip->frames) playing = 1;
        else { zan_voice_reset(v); playing = 0; }
    }
    pthread_mutex_unlock(&zan_audio_mutex);
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

EXPORT void zan_audio_voice_stop(int64_t voice) {
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
    pthread_mutex_lock(&zan_audio_mutex);
    {
        ZanVoice *v = zan_voice_of(voice);
        if (v) zan_voice_reset(v);
    }
    pthread_mutex_unlock(&zan_audio_mutex);
    return;
#endif
    {
        ZanVoice *v = zan_voice_of(voice);
        if (v) zan_voice_reset(v);
    }
}

EXPORT void zan_audio_voice_set_gain(int64_t voice, double gain) {
#if defined(_WIN32)
    if (zan_audio_cs_ok) {
        ZanVoice *v;
        EnterCriticalSection(&zan_audio_cs);
        v = zan_voice_of(voice);
        if (v) {
            if (gain < 0.0) gain = 0.0;
            v->gain = (float)gain; /* master is applied at mix time */
        }
        LeaveCriticalSection(&zan_audio_cs);
        return;
    }
#else
    pthread_mutex_lock(&zan_audio_mutex);
    {
        ZanVoice *v = zan_voice_of(voice);
        if (v) {
            if (gain < 0.0) gain = 0.0;
            v->gain = (float)gain; /* master is applied at mix time */
        }
    }
    pthread_mutex_unlock(&zan_audio_mutex);
    return;
#endif
    {
        ZanVoice *v = zan_voice_of(voice);
        if (!v) return;
        if (gain < 0.0) gain = 0.0;
        v->gain = (float)gain; /* master is applied at mix time */
    }
}

EXPORT const char *zan_audio_last_error(void) {
    return zan_audio_err;
}
