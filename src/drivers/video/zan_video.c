/* zan_video.c -- 独立动态加载 FFmpeg (libav*) 视频解码与抽帧驱动 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#elif !defined(__wasi__) && !defined(__wasm__)
#include <dlfcn.h>
#include <pthread.h>
#endif

#if defined(ZAN_VIDEO_STATIC)
#define ZAN_VIDEO_EXPORT
#elif defined(_WIN32)
#define ZAN_VIDEO_EXPORT __declspec(dllexport)
#else
#define ZAN_VIDEO_EXPORT __attribute__((visibility("default")))
#endif

#include "zan_video.h"
#include "zan_video_ff.c"

/* FFmpeg 常量（跨 5/6/7/8 稳定） */
#define ZAN_AVMEDIA_TYPE_VIDEO 0
#define ZAN_AVMEDIA_TYPE_AUDIO 1
#define ZAN_AV_PIX_FMT_BGRA    28  /* little-endian memory B,G,R,A == uint32 0xAARRGGBB */
#define ZAN_AV_SAMPLE_FMT_S16  1   /* interleaved signed 16-bit PCM */
#define ZAN_SWS_FAST_BILINEAR  1
#define ZAN_SWS_BILINEAR       2
#define ZAN_AVSEEK_FLAG_BACKWARD 1
#define ZAN_AV_NOPTS_VALUE     ((int64_t)UINT64_C(0x8000000000000000))
#define ZAN_AVERROR_EAGAIN     (-11)

typedef struct zan_video_ctx {
    void *fmt;
    int v_stream;
    int a_stream;
    void *v_ctx;
    void *a_ctx;
    void *pkt;
    void *v_frame;
    void *a_frame;
    void *sws;
    void *swr;

    int32_t width;
    int32_t height;
    int64_t duration_ms;
    int32_t fps_num;
    int32_t fps_den;
    int64_t bitrate;
    int64_t frame_count;
    char v_codec_name[48];
    char a_codec_name[48];

    zan_ff_rational_t v_tb;
    zan_ff_rational_t a_tb;
    int32_t a_sample_rate;
    int32_t a_channels;

    int has_v_frame;
    int64_t v_pts_ms;

    int16_t *a_pcm_buf;
    int32_t a_pcm_cap_samples;
    int32_t a_pcm_samples;
    int64_t a_pts_ms;

    int pending_v_drain;
    int pending_a_drain;
    int eof_reached;
} zan_video_ctx_t;

static int64_t zan_pts_to_ms(int64_t pts, zan_ff_rational_t tb) {
    if (pts == ZAN_AV_NOPTS_VALUE || tb.den <= 0 || tb.num <= 0) return 0;
    zan_ff_rational_t ms_tb = { 1, 1000 };
    return g_ff.av_rescale_q(pts, tb, ms_tb);
}

ZAN_VIDEO_EXPORT int32_t zan_video_set_ffmpeg_dir(const char *dir) {
    if (!dir) return 0;
    zan_ff_lock();
    size_t n = strlen(dir);
    if (n >= sizeof(g_ff.custom_dir)) n = sizeof(g_ff.custom_dir) - 1;
    memcpy(g_ff.custom_dir, dir, n);
    g_ff.custom_dir[n] = 0;
    if (!g_ff.loaded) g_ff.tried = 0;
    zan_ff_unlock();
    return 1;
}

ZAN_VIDEO_EXPORT int32_t zan_video_is_available(void) {
    return zan_ff_ensure_loaded() ? 1 : 0;
}

ZAN_VIDEO_EXPORT const char *zan_video_version(void) {
    if (!zan_ff_ensure_loaded()) return "";
    return g_ff.version_str;
}

ZAN_VIDEO_EXPORT const char *zan_video_last_error(void) {
    return g_vid_err;
}

static void zan_video_destroy_ctx(zan_video_ctx_t *c) {
    if (!c) return;
    if (c->sws) g_ff.sws_freeContext(c->sws);
    if (c->swr) g_ff.swr_free(&c->swr);
    if (c->v_frame) g_ff.av_frame_free(&c->v_frame);
    if (c->a_frame) g_ff.av_frame_free(&c->a_frame);
    if (c->pkt) g_ff.av_packet_free(&c->pkt);
    if (c->v_ctx) g_ff.avcodec_free_context(&c->v_ctx);
    if (c->a_ctx) g_ff.avcodec_free_context(&c->a_ctx);
    if (c->fmt) g_ff.avformat_close_input(&c->fmt);
    free(c->a_pcm_buf);
    free(c);
}

ZAN_VIDEO_EXPORT intptr_t zan_video_open(const char *url) {
    if (!url || !url[0]) { zan_vid_set_err("empty url"); return 0; }
    if (!zan_ff_ensure_loaded()) return 0;

    zan_video_ctx_t *c = (zan_video_ctx_t *)calloc(1, sizeof(zan_video_ctx_t));
    if (!c) { zan_vid_set_err("oom"); return 0; }
    c->v_stream = -1;
    c->a_stream = -1;

    if (g_ff.avformat_open_input(&c->fmt, url, NULL, NULL) < 0 || !c->fmt) {
        zan_vid_set_err("avformat_open_input failed");
        zan_video_destroy_ctx(c);
        return 0;
    }
    if (g_ff.avformat_find_stream_info(c->fmt, NULL) < 0) {
        zan_vid_set_err("avformat_find_stream_info failed");
        zan_video_destroy_ctx(c);
        return 0;
    }

    int64_t dur_us = zan_ff_fmt_duration_us(c->fmt);
    if (dur_us > 0 && dur_us != ZAN_AV_NOPTS_VALUE) {
        c->duration_ms = dur_us / 1000;
    }
    c->bitrate = zan_ff_fmt_bitrate(c->fmt);

    uint32_t nb_streams = zan_ff_fmt_nb_streams(c->fmt);
    void **streams = zan_ff_fmt_streams(c->fmt);

    const void *vdec = NULL;
    int vs = g_ff.av_find_best_stream(c->fmt, ZAN_AVMEDIA_TYPE_VIDEO, -1, -1, &vdec, 0);
    if (vs >= 0 && (uint32_t)vs < nb_streams && streams && vdec) {
        const void *st = streams[vs];
        const void *cp = zan_ff_stream_codecpar(st);
        if (cp) {
            c->v_ctx = g_ff.avcodec_alloc_context3(vdec);
            if (c->v_ctx && g_ff.avcodec_parameters_to_context(c->v_ctx, cp) >= 0) {
                /* ABI 安全方式启用 FFmpeg 多核帧级/片级自动并发解码（4K/高帧率吞吐提升 3~6x） */
                if (g_ff.av_opt_set) {
                    g_ff.av_opt_set(c->v_ctx, "threads", "auto", 0);
                    g_ff.av_opt_set(c->v_ctx, "thread_type", "frame+slice", 0);
                }
            }
            if (c->v_ctx && g_ff.avcodec_open2(c->v_ctx, vdec, NULL) >= 0) {
                c->v_stream = vs;
                c->v_tb = zan_ff_stream_time_base(st);
                zan_ff_rational_t fps = zan_ff_stream_avg_fps(st);
                c->fps_num = fps.num;
                c->fps_den = fps.den > 0 ? fps.den : 1;
                c->frame_count = zan_ff_stream_nb_frames(st);
                c->width = zan_ff_cp_width(cp);
                c->height = zan_ff_cp_height(cp);
                const char *cname = g_ff.avcodec_get_name(zan_ff_cp_codec_id(cp));
                if (cname) {
                    size_t n = strlen(cname);
                    if (n >= sizeof(c->v_codec_name)) n = sizeof(c->v_codec_name) - 1;
                    memcpy(c->v_codec_name, cname, n);
                }
            } else if (c->v_ctx) {
                g_ff.avcodec_free_context(&c->v_ctx);
            }
        }
    }

    const void *adec = NULL;
    int as = g_ff.av_find_best_stream(c->fmt, ZAN_AVMEDIA_TYPE_AUDIO, -1, -1, &adec, 0);
    if (as >= 0 && (uint32_t)as < nb_streams && streams && adec) {
        const void *st = streams[as];
        const void *cp = zan_ff_stream_codecpar(st);
        if (cp) {
            c->a_ctx = g_ff.avcodec_alloc_context3(adec);
            if (c->a_ctx && g_ff.avcodec_parameters_to_context(c->a_ctx, cp) >= 0
                && g_ff.avcodec_open2(c->a_ctx, adec, NULL) >= 0) {
                c->a_stream = as;
                c->a_tb = zan_ff_stream_time_base(st);
                c->a_sample_rate = zan_ff_cp_sample_rate(cp);
                if (c->a_sample_rate <= 0) c->a_sample_rate = 48000;
                const zan_ff_ch_layout_t *in_ch = zan_ff_cp_ch_layout(cp);
                c->a_channels = (in_ch && in_ch->nb_channels > 0) ? in_ch->nb_channels : 2;
                int in_fmt = zan_ff_cp_format(cp);
                const char *aname = g_ff.avcodec_get_name(zan_ff_cp_codec_id(cp));
                if (aname) {
                    size_t n = strlen(aname);
                    if (n >= sizeof(c->a_codec_name)) n = sizeof(c->a_codec_name) - 1;
                    memcpy(c->a_codec_name, aname, n);
                }
                /* 统一配置 libswresample 输出为立体声 s16 PCM（供 AudioStream 直接消费） */
                zan_ff_ch_layout_t out_layout = {0};
                zan_ff_ch_layout_t fallback_in = {0};
                g_ff.av_channel_layout_default(&out_layout, 2);
                const zan_ff_ch_layout_t *src_layout = in_ch;
                if (!src_layout || src_layout->nb_channels <= 0) {
                    g_ff.av_channel_layout_default(&fallback_in, c->a_channels);
                    src_layout = &fallback_in;
                }
                if (g_ff.swr_alloc_set_opts2(&c->swr,
                        &out_layout, ZAN_AV_SAMPLE_FMT_S16, c->a_sample_rate,
                        src_layout, in_fmt, c->a_sample_rate, 0, NULL) >= 0 && c->swr) {
                    if (g_ff.swr_init(c->swr) < 0) {
                        g_ff.swr_free(&c->swr);
                    }
                }
                g_ff.av_channel_layout_uninit(&out_layout);
                if (fallback_in.nb_channels > 0) g_ff.av_channel_layout_uninit(&fallback_in);
            } else if (c->a_ctx) {
                g_ff.avcodec_free_context(&c->a_ctx);
            }
        }
    }

    if (c->v_stream < 0 && c->a_stream < 0) {
        zan_vid_set_err("no decodable video or audio stream found");
        zan_video_destroy_ctx(c);
        return 0;
    }

    c->pkt = g_ff.av_packet_alloc();
    c->v_frame = g_ff.av_frame_alloc();
    c->a_frame = g_ff.av_frame_alloc();
    if (!c->pkt || !c->v_frame || !c->a_frame) {
        zan_vid_set_err("av_packet_alloc/av_frame_alloc failed");
        zan_video_destroy_ctx(c);
        return 0;
    }

    zan_vid_set_err(NULL);
    return (intptr_t)c;
}

ZAN_VIDEO_EXPORT void zan_video_close(intptr_t handle) {
    zan_video_destroy_ctx((zan_video_ctx_t *)handle);
}

ZAN_VIDEO_EXPORT int32_t zan_video_width(intptr_t handle) {
    zan_video_ctx_t *c = (zan_video_ctx_t *)handle;
    return c ? c->width : 0;
}
ZAN_VIDEO_EXPORT int32_t zan_video_height(intptr_t handle) {
    zan_video_ctx_t *c = (zan_video_ctx_t *)handle;
    return c ? c->height : 0;
}
ZAN_VIDEO_EXPORT int64_t zan_video_duration_ms(intptr_t handle) {
    zan_video_ctx_t *c = (zan_video_ctx_t *)handle;
    return c ? c->duration_ms : 0;
}
ZAN_VIDEO_EXPORT int32_t zan_video_fps_num(intptr_t handle) {
    zan_video_ctx_t *c = (zan_video_ctx_t *)handle;
    return c ? c->fps_num : 0;
}
ZAN_VIDEO_EXPORT int32_t zan_video_fps_den(intptr_t handle) {
    zan_video_ctx_t *c = (zan_video_ctx_t *)handle;
    return c ? c->fps_den : 1;
}
ZAN_VIDEO_EXPORT int64_t zan_video_bitrate(intptr_t handle) {
    zan_video_ctx_t *c = (zan_video_ctx_t *)handle;
    return c ? c->bitrate : 0;
}
ZAN_VIDEO_EXPORT int64_t zan_video_frame_count(intptr_t handle) {
    zan_video_ctx_t *c = (zan_video_ctx_t *)handle;
    return c ? c->frame_count : 0;
}
ZAN_VIDEO_EXPORT int32_t zan_video_has_video(intptr_t handle) {
    zan_video_ctx_t *c = (zan_video_ctx_t *)handle;
    return (c && c->v_stream >= 0) ? 1 : 0;
}
ZAN_VIDEO_EXPORT int32_t zan_video_has_audio(intptr_t handle) {
    zan_video_ctx_t *c = (zan_video_ctx_t *)handle;
    return (c && c->a_stream >= 0) ? 1 : 0;
}
ZAN_VIDEO_EXPORT int32_t zan_video_audio_sample_rate(intptr_t handle) {
    zan_video_ctx_t *c = (zan_video_ctx_t *)handle;
    return c ? c->a_sample_rate : 0;
}
ZAN_VIDEO_EXPORT int32_t zan_video_audio_channels(intptr_t handle) {
    zan_video_ctx_t *c = (zan_video_ctx_t *)handle;
    /* 输出 PCM 经 swresample 统一为立体声 2 声道 */
    return (c && c->a_stream >= 0) ? 2 : 0;
}
ZAN_VIDEO_EXPORT const char *zan_video_video_codec(intptr_t handle) {
    zan_video_ctx_t *c = (zan_video_ctx_t *)handle;
    return c ? c->v_codec_name : "";
}
ZAN_VIDEO_EXPORT const char *zan_video_audio_codec(intptr_t handle) {
    zan_video_ctx_t *c = (zan_video_ctx_t *)handle;
    return c ? c->a_codec_name : "";
}

static int zan_video_convert_audio_frame(zan_video_ctx_t *c) {
    int nb = zan_ff_frame_nb_samples(c->a_frame);
    if (nb <= 0 || !c->swr) {
        c->a_pcm_samples = 0;
        return 0;
    }
    int64_t delay = g_ff.swr_get_delay(c->swr, (int64_t)c->a_sample_rate);
    int max_out = (int)(delay + nb + 64);
    if (max_out > c->a_pcm_cap_samples) {
        int16_t *nbuf = (int16_t *)realloc(c->a_pcm_buf, (size_t)max_out * 2 * sizeof(int16_t));
        if (!nbuf) { c->a_pcm_samples = 0; return 0; }
        c->a_pcm_buf = nbuf;
        c->a_pcm_cap_samples = max_out;
    }
    uint8_t *out_planes[1] = { (uint8_t *)c->a_pcm_buf };
    const uint8_t **in_planes = (const uint8_t **)zan_ff_frame_data(c->a_frame);
    int out_samples = g_ff.swr_convert(c->swr, out_planes, max_out, in_planes, nb);
    if (out_samples < 0) out_samples = 0;
    c->a_pcm_samples = out_samples;
    c->a_pts_ms = zan_pts_to_ms(zan_ff_frame_pts(c->a_frame), c->a_tb);
    return out_samples > 0 ? 1 : 0;
}

ZAN_VIDEO_EXPORT int32_t zan_video_step(intptr_t handle) {
    zan_video_ctx_t *c = (zan_video_ctx_t *)handle;
    if (!c || !c->fmt) return -1;

    for (int guard = 0; guard < 4096; guard++) {
        if (c->pending_v_drain && c->v_ctx) {
            int r = g_ff.avcodec_receive_frame(c->v_ctx, c->v_frame);
            if (r == 0) {
                int fw = zan_ff_frame_width(c->v_frame);
                int fh = zan_ff_frame_height(c->v_frame);
                if (fw > 0 && fh > 0) { c->width = fw; c->height = fh; }
                c->has_v_frame = 1;
                c->v_pts_ms = zan_pts_to_ms(zan_ff_frame_pts(c->v_frame), c->v_tb);
                return 1;
            }
            c->pending_v_drain = 0;
        }
        if (c->pending_a_drain && c->a_ctx) {
            int r = g_ff.avcodec_receive_frame(c->a_ctx, c->a_frame);
            if (r == 0) {
                if (zan_video_convert_audio_frame(c)) return 2;
                continue;
            }
            c->pending_a_drain = 0;
        }

        if (c->eof_reached) return 0;

        int rr = g_ff.av_read_frame(c->fmt, c->pkt);
        if (rr < 0) {
            /* 发送 NULL packet 冲刷解码器尾帧 */
            c->eof_reached = 1;
            if (c->v_ctx) { g_ff.avcodec_send_packet(c->v_ctx, NULL); c->pending_v_drain = 1; }
            if (c->a_ctx) { g_ff.avcodec_send_packet(c->a_ctx, NULL); c->pending_a_drain = 1; }
            continue;
        }

        int si = zan_ff_pkt_stream_index(c->pkt);
        if (si == c->v_stream && c->v_ctx) {
            if (g_ff.avcodec_send_packet(c->v_ctx, c->pkt) == 0) {
                c->pending_v_drain = 1;
            }
        } else if (si == c->a_stream && c->a_ctx) {
            if (g_ff.avcodec_send_packet(c->a_ctx, c->pkt) == 0) {
                c->pending_a_drain = 1;
            }
        }
        g_ff.av_packet_unref(c->pkt);
    }
    return 0;
}

ZAN_VIDEO_EXPORT int32_t zan_video_copy_video_argb(intptr_t handle,
                                                   uint32_t *dst, int32_t dst_w, int32_t dst_h,
                                                   int32_t dst_stride) {
    zan_video_ctx_t *c = (zan_video_ctx_t *)handle;
    if (!c || !c->has_v_frame || !dst || dst_w <= 0 || dst_h <= 0 || dst_stride < dst_w)
        return 0;
    int src_w = zan_ff_frame_width(c->v_frame);
    int src_h = zan_ff_frame_height(c->v_frame);
    int src_fmt = zan_ff_frame_format(c->v_frame);
    if (src_w <= 0 || src_h <= 0 || src_fmt < 0) return 0;

    c->sws = g_ff.sws_getCachedContext(c->sws, src_w, src_h, src_fmt,
                                       dst_w, dst_h, ZAN_AV_PIX_FMT_BGRA,
                                       ZAN_SWS_FAST_BILINEAR, NULL, NULL, NULL);
    if (!c->sws) return 0;

    uint8_t *dst_planes[4] = { (uint8_t *)dst, NULL, NULL, NULL };
    int dst_linesize[4] = { dst_stride * 4, 0, 0, 0 };
    int rows = g_ff.sws_scale(c->sws,
                              (const uint8_t *const *)zan_ff_frame_data(c->v_frame),
                              zan_ff_frame_linesize(c->v_frame),
                              0, src_h, dst_planes, dst_linesize);
    return (rows > 0) ? 1 : 0;
}

ZAN_VIDEO_EXPORT int64_t zan_video_video_pts_ms(intptr_t handle) {
    zan_video_ctx_t *c = (zan_video_ctx_t *)handle;
    return c ? c->v_pts_ms : 0;
}

ZAN_VIDEO_EXPORT const int16_t *zan_video_audio_pcm_ptr(intptr_t handle) {
    zan_video_ctx_t *c = (zan_video_ctx_t *)handle;
    return (c && c->a_pcm_samples > 0) ? c->a_pcm_buf : NULL;
}

ZAN_VIDEO_EXPORT int32_t zan_video_audio_samples(intptr_t handle) {
    zan_video_ctx_t *c = (zan_video_ctx_t *)handle;
    return c ? c->a_pcm_samples : 0;
}

ZAN_VIDEO_EXPORT int64_t zan_video_audio_pts_ms(intptr_t handle) {
    zan_video_ctx_t *c = (zan_video_ctx_t *)handle;
    return c ? c->a_pts_ms : 0;
}

ZAN_VIDEO_EXPORT int32_t zan_video_seek_ms(intptr_t handle, int64_t target_ms) {
    zan_video_ctx_t *c = (zan_video_ctx_t *)handle;
    if (!c || !c->fmt) return 0;
    if (target_ms < 0) target_ms = 0;

    int stream_idx = -1;
    int64_t ts = target_ms * 1000; /* AV_TIME_BASE = 1,000,000 us */
    if (c->v_stream >= 0 && c->v_tb.num > 0 && c->v_tb.den > 0) {
        stream_idx = c->v_stream;
        zan_ff_rational_t ms_tb = { 1, 1000 };
        ts = g_ff.av_rescale_q(target_ms, ms_tb, c->v_tb);
    }
    if (g_ff.av_seek_frame(c->fmt, stream_idx, ts, ZAN_AVSEEK_FLAG_BACKWARD) < 0) {
        return 0;
    }
    if (c->v_ctx) g_ff.avcodec_flush_buffers(c->v_ctx);
    if (c->a_ctx) g_ff.avcodec_flush_buffers(c->a_ctx);
    c->pending_v_drain = 0;
    c->pending_a_drain = 0;
    c->eof_reached = 0;
    c->has_v_frame = 0;
    c->a_pcm_samples = 0;
    return 1;
}

ZAN_VIDEO_EXPORT int32_t zan_video_extract_frame_argb(const char *url,
                                                      int64_t timestamp_ms,
                                                      uint32_t *dst,
                                                      int32_t dst_w, int32_t dst_h) {
    if (!url || !dst || dst_w <= 0 || dst_h <= 0) return 0;
    intptr_t h = zan_video_open(url);
    if (!h) return 0;
    int32_t ok = 0;
    if (zan_video_has_video(h)) {
        if (timestamp_ms > 0) {
            zan_video_seek_ms(h, timestamp_ms);
        }
        int got_any = 0;
        for (int i = 0; i < 600; i++) {
            int st = zan_video_step(h);
            if (st == 1) {
                got_any = 1;
                if (timestamp_ms <= 0 || zan_video_video_pts_ms(h) >= timestamp_ms) {
                    ok = zan_video_copy_video_argb(h, dst, dst_w, dst_h, dst_w);
                    break;
                }
            } else if (st <= 0) {
                break;
            }
        }
        if (!ok && got_any) {
            ok = zan_video_copy_video_argb(h, dst, dst_w, dst_h, dst_w);
        }
    }
    zan_video_close(h);
    return ok;
}
