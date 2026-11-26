#ifndef ZAN_VIDEO_H
#define ZAN_VIDEO_H

#include <stdint.h>

#ifndef ZAN_VIDEO_EXPORT
#define ZAN_VIDEO_EXPORT
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* 动态加载 FFmpeg (libavformat/libavcodec/libavutil/libswscale/libswresample) 状态与配置 */
extern ZAN_VIDEO_EXPORT int32_t zan_video_set_ffmpeg_dir(const char *dir);
extern ZAN_VIDEO_EXPORT int32_t zan_video_is_available(void);
extern ZAN_VIDEO_EXPORT const char *zan_video_version(void);
extern ZAN_VIDEO_EXPORT const char *zan_video_last_error(void);

/* 打开/关闭视频文件或流（支持本地路径、HTTP/HLS/RTSP 等 FFmpeg 协议），返回非 0 句柄 */
extern ZAN_VIDEO_EXPORT intptr_t zan_video_open(const char *url);
extern ZAN_VIDEO_EXPORT void zan_video_close(intptr_t handle);

/* 元数据查询 */
extern ZAN_VIDEO_EXPORT int32_t zan_video_width(intptr_t handle);
extern ZAN_VIDEO_EXPORT int32_t zan_video_height(intptr_t handle);
extern ZAN_VIDEO_EXPORT int64_t zan_video_duration_ms(intptr_t handle);
extern ZAN_VIDEO_EXPORT int32_t zan_video_fps_num(intptr_t handle);
extern ZAN_VIDEO_EXPORT int32_t zan_video_fps_den(intptr_t handle);
extern ZAN_VIDEO_EXPORT int64_t zan_video_bitrate(intptr_t handle);
extern ZAN_VIDEO_EXPORT int64_t zan_video_frame_count(intptr_t handle);
extern ZAN_VIDEO_EXPORT int32_t zan_video_has_video(intptr_t handle);
extern ZAN_VIDEO_EXPORT int32_t zan_video_has_audio(intptr_t handle);
extern ZAN_VIDEO_EXPORT int32_t zan_video_audio_sample_rate(intptr_t handle);
extern ZAN_VIDEO_EXPORT int32_t zan_video_audio_channels(intptr_t handle);
extern ZAN_VIDEO_EXPORT const char *zan_video_video_codec(intptr_t handle);
extern ZAN_VIDEO_EXPORT const char *zan_video_audio_codec(intptr_t handle);

/* 推进一步解码：
 *   1 = 新的视频帧就绪（可通过 zan_video_copy_video_argb / zan_video_video_pts_ms 读取）
 *   2 = 新的音频帧就绪（已转为立体声 s16 PCM，可通过 zan_video_audio_pcm_ptr / samples / pts 读取）
 *   0 = EOF（播放完毕）
 *  <0 = 错误 */
extern ZAN_VIDEO_EXPORT int32_t zan_video_step(intptr_t handle);

/* 将当前解码出的视频帧经 swscale 转为 ARGB32 (0xAARRGGBB) 并缩放到 dst_w x dst_h。
 * dst_stride 为像素数（>= dst_w）。成功返回 1，失败返回 0。 */
extern ZAN_VIDEO_EXPORT int32_t zan_video_copy_video_argb(intptr_t handle,
                                      uint32_t *dst, int32_t dst_w, int32_t dst_h,
                                      int32_t dst_stride);
extern ZAN_VIDEO_EXPORT int64_t zan_video_video_pts_ms(intptr_t handle);

/* 当前解码出的音频帧（立体声 16-bit signed interleaved PCM，采样率 = 输出采样率） */
extern ZAN_VIDEO_EXPORT const int16_t *zan_video_audio_pcm_ptr(intptr_t handle);
extern ZAN_VIDEO_EXPORT int32_t zan_video_audio_samples(intptr_t handle);
extern ZAN_VIDEO_EXPORT int64_t zan_video_audio_pts_ms(intptr_t handle);

/* 跳转到目标毫秒位置（关键帧回退并清空解码器队列），成功返回 1 */
extern ZAN_VIDEO_EXPORT int32_t zan_video_seek_ms(intptr_t handle, int64_t target_ms);

/* 一键抽取指定时间点的单帧 ARGB32（适合缩略图/封面批处理），成功返回 1 */
extern ZAN_VIDEO_EXPORT int32_t zan_video_extract_frame_argb(const char *url,
                                      int64_t timestamp_ms,
                                      uint32_t *dst, int32_t dst_w, int32_t dst_h);

#ifdef __cplusplus
}
#endif

#endif
