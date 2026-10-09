/* zan_abi */

#ifndef ZAN_ABI_H
#define ZAN_ABI_H

#include <stdint.h>

/* 核心系统底层抽象与内存语义契约 */

/* 底层系统交互与数据协议契约 */
#define ZAN_OBJ_HDR_SIZE  16

/* 内部辅助逻辑 */
#define ZAN_OBJ_RC_OFF    (-16)

/* 内部辅助逻辑 */
#define ZAN_OBJ_SITE_OFF  (-8)

/* 底层系统交互与数据协议契约 */
#define ZAN_STRING_MAGIC       UINT64_C(0x5a414e5354524d47) /* "ZANSTRMG" */
#define ZAN_STRING_SENTINEL_RC UINT64_C(0xffffffffffffffff)

/* 内部辅助逻辑 */
#define ZAN_STRING_TAG         UINT64_C(0x5a414e53)         /* "ZANS" */
#define ZAN_STR_LEN_UNKNOWN    UINT64_C(0x54524d47)         /* "TRMG" */
#define ZAN_STR_LEN_MASK       UINT64_C(0xffffffff)
#define ZAN_STR_HDR_WORD(len)  (((ZAN_STRING_TAG) << 32) | \
                                ((uint64_t)(len) & ZAN_STR_LEN_MASK))

/* 内部辅助逻辑 */
#define ZAN_ARRAY_MAGIC        UINT64_C(0x5a414e4152524159) /* "ZANARRAY" */

/* 内部辅助逻辑 */
#define ZAN_ARR_HDR_SIZE       32
#define ZAN_ARR_RC_OFF         (-32)
#define ZAN_ARR_RC_MAGIC_OFF   (-24)
#define ZAN_ARRAY_RC_MAGIC     UINT64_C(0x5a414e41525243) /* "ZANARRC" */

/* 核心系统底层抽象与内存语义契约 */
#define ZAN_CLOSURE_TAG        1
#define ZAN_CLOSURE_FN_OFF     0
#define ZAN_CLOSURE_DTOR_OFF   8
#define ZAN_CLOSURE_TARGET_OFF 16

/* 2 */

#define ZAN_EH_CHUNKS      64     /* 核心系统底层抽象与内存语义契约 */
#define ZAN_EH_SLOT_SHIFT  6      /* 核心系统底层抽象与内存语义契约 */
#define ZAN_EH_SLOT_BYTES  1040   /* 核心系统底层抽象与内存语义契约 */
#define ZAN_EH_MARK_OFF    1024
#define ZAN_EH_TMP_SHIFT   12     /* 核心系统底层抽象与内存语义契约 */
#define ZAN_EH_THREADS     1024   /* 底层系统交互与数据协议契约 */
/* 内部辅助逻辑 */
#define ZAN_EH_TOMBSTONE   0xFFFFFFFFFFFFFFFFULL

/* 内部辅助逻辑 */
enum {
    ZAN_EH_SLOT_OBJ = 0,
    ZAN_EH_SLOT_STR = 1,
    ZAN_EH_SLOT_DLG = 2,
    ZAN_EH_SLOT_ARR = 3
};

#endif /* ZAN_ABI_H */
