/* Procedural audio generation, RIFF/WAV spec, and AudioHaptics regression test. */
#define _CRT_SECURE_NO_WARNINGS
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>

#define CHECK(cond, message) do { if (!(cond)) { \
    fprintf(stderr, "FAIL: %s (line %d)\n", message, __LINE__); exit(1); \
} } while (0)

typedef int32_t i32;
typedef int16_t i16;
typedef uint8_t u8;

/* 模拟 ToneGenerator C-ABI 算法 */
static void write_i16(u8 *buf, i32 offset, i32 val) {
    buf[offset] = (u8)(val & 0xFF);
    buf[offset + 1] = (u8)((val >> 8) & 0xFF);
}

static void write_i32(u8 *buf, i32 offset, i32 val) {
    buf[offset] = (u8)(val & 0xFF);
    buf[offset + 1] = (u8)((val >> 8) & 0xFF);
    buf[offset + 2] = (u8)((val >> 16) & 0xFF);
    buf[offset + 3] = (u8)((val >> 24) & 0xFF);
}

static u8* create_tick_wav(i32 duration_ms, i32 freq, i32 *out_size) {
    if (duration_ms <= 0) duration_ms = 25;
    if (freq <= 0) freq = 1200;
    i32 sample_rate = 44100;
    i32 num_samples = (sample_rate * duration_ms) / 1000;
    i32 pcm_bytes = num_samples * 2;
    i32 total_bytes = 44 + pcm_bytes;
    u8 *wav = (u8*)malloc(total_bytes);
    if (!wav) return NULL;

    // RIFF header
    memcpy(wav + 0, "RIFF", 4);
    write_i32(wav, 4, pcm_bytes + 36);
    memcpy(wav + 8, "WAVEfmt ", 8);
    write_i32(wav, 16, 16);             // subchunk1 size
    write_i16(wav, 20, 1);              // PCM format
    write_i16(wav, 22, 1);              // mono
    write_i32(wav, 24, sample_rate);     // sample rate
    write_i32(wav, 28, sample_rate * 2); // byte rate
    write_i16(wav, 32, 2);              // block align
    write_i16(wav, 34, 16);             // bits per sample
    memcpy(wav + 36, "data", 4);
    write_i32(wav, 40, pcm_bytes);

    float angular_freq = 6.2831853f * (float)freq / (float)sample_rate;
    for (i32 i = 0; i < num_samples; i++) {
        float progress = (float)i / (float)num_samples;
        float envelope = 1.0f - progress;
        envelope = envelope * envelope * envelope; // 立方衰减
        float phase = angular_freq * (float)i;
        float sample = sinf(phase) * envelope * 24000.0f;
        i32 s16 = (i32)sample;
        if (s16 > 32767) s16 = 32767;
        else if (s16 < -32768) s16 = -32768;
        write_i16(wav, 44 + i * 2, s16);
    }

    *out_size = total_bytes;
    return wav;
}

int main(void) {
    /* 1. 验证生成 Tick WAV 的 RIFF 头合法性 */
    i32 size = 0;
    u8 *wav = create_tick_wav(25, 1200, &size);
    CHECK(wav != NULL, "WAV buffer allocation failed");
    CHECK(size > 44, "WAV size too small");

    CHECK(memcmp(wav + 0, "RIFF", 4) == 0, "RIFF magic mismatch");
    CHECK(memcmp(wav + 8, "WAVE", 4) == 0, "WAVE magic mismatch");
    CHECK(memcmp(wav + 12, "fmt ", 4) == 0, "fmt magic mismatch");
    CHECK(memcmp(wav + 36, "data", 4) == 0, "data magic mismatch");

    // 验证采样率 44100
    i32 sample_rate = (i32)(wav[24] | (wav[25] << 8) | (wav[26] << 16) | (wav[27] << 24));
    CHECK(sample_rate == 44100, "sample rate must be 44100");

    // 验证声道数 1 (Mono)
    i32 channels = (i32)(wav[22] | (wav[23] << 8));
    CHECK(channels == 1, "channel count must be 1 (mono)");

    // 验证采样位深 16-bit
    i32 bits_per_sample = (i32)(wav[34] | (wav[35] << 8));
    CHECK(bits_per_sample == 16, "bit depth must be 16");

    /* 2. 验证 PCM 数据未溢出与立方衰减平滑收拢 */
    i32 pcm_bytes = size - 44;
    i32 num_samples = pcm_bytes / 2;
    CHECK(num_samples == (44100 * 25) / 1000, "sample count mismatch for 25ms");

    // 末尾样本必须平滑收拢接近 0（防爆音爆破声）
    i16 last_sample = (int16_t)(wav[size - 2] | (wav[size - 1] << 8));
    CHECK(abs(last_sample) < 50, "last sample must taper to 0 to prevent audio pop");

    free(wav);
    printf("PASS: gui_audio_haptics_test passed all checks.\n");
    return 0;
}
