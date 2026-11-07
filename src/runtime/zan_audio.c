#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
#include "../common/host_oom.h"

#if defined(ZAN_AUDIO_STATIC)
#define ZAN_AUDIO_EXPORT
#elif defined(_WIN32)
#define ZAN_AUDIO_EXPORT __declspec(dllexport)
#else
#define ZAN_AUDIO_EXPORT __attribute__((visibility("default")))
#endif

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#ifndef COBJMACROS
#define COBJMACROS
#endif
#ifndef CINTERFACE
#define CINTERFACE
#endif
#include <windows.h>
#include <mmreg.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#endif

#if !defined(_WIN32) && !defined(__wasi__)
#include <pthread.h>
#include <dlfcn.h>
#include <unistd.h>
#endif

#ifdef __ANDROID__
#include <aaudio/AAudio.h>
#endif

#define STB_VORBIS_NO_PUSHDATA_API
#define STB_VORBIS_NO_STDIO
#include "stb_vorbis.c"

/* 组合分发器（Composite Dispatcher）子模块按职责拓扑引入：
 * 1. audio/audio_mixer.c:  多轨混音器核心数据结构、音量控制与软件混音逻辑
 * 2. audio/audio_clip.c:   文件读取与 WAV/OGG 音频解码引擎
 * 3. audio/audio_wasapi.c: Windows WASAPI 低延迟硬件音频输出后端
 * 4. audio/audio_posix.c:  POSIX/Android(AAudio)/Linux(ALSA)/macOS(CoreAudio)/OHOS 驱动
 * 5. audio/audio_api.c:    跨平台公开导出 zan_audio_* API
 */
#include "audio/audio_mixer.c"
#include "audio/audio_clip.c"
#include "audio/audio_wasapi.c"
#include "audio/audio_posix.c"
#include "audio/audio_api.c"
