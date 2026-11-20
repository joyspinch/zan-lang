/* Single-TU compatibility entry for native tests that inspect private CPU
 * predicates. Production archives compile hw/rt_hw_*.c independently so a
 * JSON/CRC/etc. reference cannot pull in unrelated algorithm families. */
#include "hw/rt_hw_internal.h"

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

/* 10. X25519 椭圆曲线与 RSA 大数模幂/CRT 运算 */
#include "hw/rt_hw_asym.c"
