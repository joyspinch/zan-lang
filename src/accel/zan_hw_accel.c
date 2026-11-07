/* zan_hw_accel.c: 硬件指令集加速与密码学/SIMD 运行时（组合分发入口） */

#include "zan_hw_accel.h"
#include <string.h>
#include <stdlib.h>

#if defined(_WIN32)
  #include <windows.h>
#endif

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
  #include <immintrin.h>
  #include <wmmintrin.h>
  #include <emmintrin.h>
  #include <tmmintrin.h>
  #include <smmintrin.h>
#endif
#if defined(__aarch64__) || defined(_M_ARM64)
  #include <arm_neon.h>
  #if defined(__APPLE__)
    #include <sys/sysctl.h>
  #elif !defined(_WIN32)
    #include <sys/auxv.h>
  #endif
#endif

/* 1. CPU 特性探针与 KAT 自检门禁 */
#include "hw/rt_hw_cpu.c"

/* 2. SHA-256 与 SHA-1 硬件加速族 */
#include "hw/rt_hw_hash.c"

/* 3. AES 基础对称分组密码 (ECB/CBC/CTR 与软件回退) */
#include "hw/rt_hw_aes.c"

/* 4. GCM 认证加密与 GHASH 向量加速族 */
#include "hw/rt_hw_gcm.c"

/* 5. CRC32 与 CRC32C 硬件切片与指令加速族 */
#include "hw/rt_hw_crc.c"

/* 6. SIMD 像素混合与色彩通道变换 */
#include "hw/rt_hw_pixel.c"

/* 7. 国密 SM3、SHA-512 与 SM4 硬件加速族 */
#include "hw/rt_hw_sm.c"

/* 8. Base64 快速查表与编解码向量加速 */
#include "hw/rt_hw_b64.c"

/* 9. AVX2 向量化 JSON 扫描器与极速解析器 */
#include "hw/rt_hw_json.c"

/* 10. X25519 椭圆曲线与 RSA 大数模幂/CRT 运算 */
#include "hw/rt_hw_asym.c"
