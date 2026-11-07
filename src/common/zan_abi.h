/* zan_abi */

#ifndef ZAN_ABI_H
#define ZAN_ABI_H

#include <stdint.h>


#define ZAN_OBJ_HDR_SIZE  16

#define ZAN_OBJ_RC_OFF    (-16)

#define ZAN_OBJ_SITE_OFF  (-8)

#define ZAN_STRING_MAGIC       UINT64_C(0x5a414e5354524d47) /* "ZANSTRMG" */
#define ZAN_STRING_SENTINEL_RC UINT64_C(0xffffffffffffffff)

#define ZAN_STRING_TAG         UINT64_C(0x5a414e53)         /* "ZANS" */
#define ZAN_STR_LEN_UNKNOWN    UINT64_C(0x54524d47)         /* "TRMG" */
#define ZAN_STR_LEN_MASK       UINT64_C(0xffffffff)
#define ZAN_STR_HDR_WORD(len)  (((ZAN_STRING_TAG) << 32) | \
                                ((uint64_t)(len) & ZAN_STR_LEN_MASK))

#define ZAN_ARRAY_MAGIC        UINT64_C(0x5a414e4152524159) /* "ZANARRAY" */

#define ZAN_ARR_HDR_SIZE       32
#define ZAN_ARR_RC_OFF         (-32)
#define ZAN_ARR_RC_MAGIC_OFF   (-24)
#define ZAN_ARRAY_RC_MAGIC     UINT64_C(0x5a414e41525243) /* "ZANARRC" */

#define ZAN_CLOSURE_TAG        1
#define ZAN_CLOSURE_FN_OFF     0
#define ZAN_CLOSURE_DTOR_OFF   8
#define ZAN_CLOSURE_TARGET_OFF 16

/* 2 */

#define ZAN_EH_CHUNKS      64
#define ZAN_EH_SLOT_SHIFT  6
#define ZAN_EH_SLOT_BYTES  1040
#define ZAN_EH_MARK_OFF    1024
#define ZAN_EH_TMP_SHIFT   12
#define ZAN_EH_THREADS     1024
#define ZAN_EH_TOMBSTONE   0xFFFFFFFFFFFFFFFFULL

enum {
    ZAN_EH_SLOT_OBJ = 0,
    ZAN_EH_SLOT_STR = 1,
    ZAN_EH_SLOT_DLG = 2,
    ZAN_EH_SLOT_ARR = 3
};

/* 堆内存生产级可观测指标 */
typedef struct {
    size_t slab_count;        /* 当前分配的 1MB Slab 数量 */
    size_t slab_bytes;        /* Slab 总占用字节数 (slab_count * 1MB) */
    size_t small_alloc_count; /* 小对象总分配次数 (原子累计) */
    size_t trimmed_bytes;     /* 已向操作系统归还的物理内存字节数 */
} zan_mem_stats_t;

/* 字段走图表元数据与描述符扩展 (B-ID89) */
typedef struct {
    uint32_t offset;  /* 对象内部字节偏移 */
    uint32_t is_rc;   /* 1=RC指针, 0=非RC */
} zan_field_ref_t;

typedef struct {
    uint32_t kind;             /* 0=标准对象字段偏移, 1=List<T>, 2=Dict<K,V>, 3=叶子/无引用 */
    uint32_t ref_count;        /* RC 字段数量 (或 Dict 标志位) */
    uint32_t instance_size;    /* 对象体尺寸 (字节) */
    uint32_t reserved;
    zan_field_ref_t refs[];    /* 各字段偏移列表 */
} zan_field_graph_t;

typedef struct {
    void *dtor_fn;
    void *tynames_arr;
    void *meta_ptr;
    zan_field_graph_t *field_graph;
} zan_arc_desc_t;

void zan_mem_get_stats(zan_mem_stats_t *out);
size_t zan_mem_trim(void);

/* 运行时活对象注册表与孤儿环探测 (B-ID89) */
void zan_rt_live_register(void *obj, size_t size);
void zan_rt_live_unregister(void *obj);
void zan_rt_register_site_field_graphs(void **graphs, void **tynames, int64_t count);
int64_t zan_gc_detect_cycles(void);
void zan_rt_dump_leaks(void);
int64_t zan_gc_write_heap_snapshot(const char *path);

#endif /* ZAN_ABI_H */
