/* zan_abi */

#ifndef ZAN_ABI_H
#define ZAN_ABI_H

#include <stddef.h>
#include <stdint.h>

/* ARC 头（2026-11-24 第二版，设计见 docs/OBJECT_LAYOUT.md）：引用计数是
 * 32 位，住在 [p-8]——对象与字符串同址同宽，通用 retain/release 不必先
 * 分辨是哪一种。[p-8] 的 u64 叫"头字"：
 *   对象：  [-8] u32 rc | [-4] u32 tw          头 8 字节，块基址 p-8
 *           头字 = (tw<<32)|rc；tw = bit 31 栈标记 | bit 30 对象标记 |
 *           bit 29 前垫 8 字节（对齐 16 的类，块基址 p-16）| 站点号（bit 0..28）
 *   字符串：[-12] u32 len | [-8] u32 rc | [-4] "ZANS"   头 12 字节
 *           头字 = (ZANS<<32)|rc
 *   数组：  不变；[-8] 是 ZAN_ARRAY_MAGIC、秩（1..4095）或数组描述符指针
 * 对象标记让四种头字互不混淆：用户态指针高 32 位 < 0x8000，ZANS/数组魔数
 * 的高 32 位解出的站点号（0x1a41xxxx）远超任何程序的站点数，按表长比界即
 * 查不中。于是"头字高 32 位 == ZANS 即字符串""头字 < 0 即栈上提升对象"
 * 两条判定对新旧代码都成立。静态（immortal）数据的 rc 是 0xffffffff。
 * 字符串从 [-16] 起的 4 字节可能不属于它（串页把串块排在 4 mod 8 地址上，
 * 好让 [-8] 头字仍 8 字节对齐），故绝不能读写串的 [-16]；释放串仍传
 * p-16（分配器认得这个"虚基址"），释放对象传 p-8。 */
#define ZAN_OBJ_HDR_SIZE 8
#define ZAN_STR_HDR_SIZE 12
#define ZAN_STR_VBASE 16 /* 串的虚基址 = p - 16（free 的实参） */

#define ZAN_RC_OFF (-8)
#define ZAN_RC_IMMORTAL UINT32_C(0xffffffff)
/* C 侧读对象/字符串 RC：有符号读出，immortal 读作 -1（与旧 i64 哨兵同口径） */
#define ZAN_RC_PTR(obj) ((uint32_t *)((char *)(obj) + ZAN_RC_OFF))
#define ZAN_RC_LOAD(obj) ((int64_t)(int32_t)*ZAN_RC_PTR(obj))

/* 头字（u64）所在偏移；对象的 tw 是它的高 32 位 */
#define ZAN_HDR_WORD_OFF (-8)
#define ZAN_OBJ_TW_OFF (-4)
#define ZAN_OBJ_TW_MARK UINT32_C(0x40000000)
/* 对齐 16 的类（i128/decimal/SIMD 字段）：块前多垫 8 字节让 p 16 对齐，
 * 块基址 = p-16；释放按这一位算基址 */
#define ZAN_OBJ_TW_PAD16 UINT32_C(0x20000000)
#define ZAN_OBJ_TW_SITE_MASK UINT32_C(0x1fffffff)
#define ZAN_OBJ_STACK_FLAG                                                     \
  UINT64_C(0x8000000000000000) /* 头字里的栈上提升标记 (免 free) */
/* 新对象的头字：对象标记 + 站点号在高 32 位，rc = 1 */
#define ZAN_OBJ_HDR_INIT(site)                                                 \
  ((((uint64_t)(ZAN_OBJ_TW_MARK | (uint32_t)(site))) << 32) | UINT64_C(1))
/* 头字是不是对象头（去掉栈标记后看对象标记；字符串的 ZANS 也带这一位，先判串） */
#define ZAN_HDR_IS_OBJ(word) ((((word) >> 32) & ZAN_OBJ_TW_MARK) != 0)
/* 头字 → 站点号（去掉栈标记与对象标记） */
#define ZAN_HDR_SITE(word) ((uint64_t)(((word) >> 32) & ZAN_OBJ_TW_SITE_MASK))

/* 字符串长度：[-12] 的 u32；ZAN_STR_LEN_UNKNOWN 表示超 32 位、回退 strlen */
#define ZAN_STR_LEN_OFF (-12)
/* 静态字符串字面量/哨兵串的两个前缀 i64（从 p-16 起）：
 *   W0 = (len<<32)|0xffffffff（[-16] 4 字节填充写全 1，[-12] len），
 *   W1 = (ZANS<<32)|immortal rc。填充全 1 是静态串的可辨认前缀（分片常量
 *   blob 据此收录），运行期没有代码读它。 */
#define ZAN_STR_LIT_W0(len)                                                    \
  ((((uint64_t)(len) & ZAN_STR_LEN_MASK) << 32) | UINT64_C(0xffffffff))
#define ZAN_STR_LIT_W1 ((ZAN_STRING_TAG << 32) | UINT64_C(0xffffffff))
/* 堆字符串的头字（[-8] u64）：ZANS 标签 + rc */
#define ZAN_STR_TAG_WORD(rc)                                                   \
  ((ZAN_STRING_TAG << 32) | ((uint64_t)(rc) & UINT64_C(0xffffffff)))

#define ZAN_STRING_MAGIC UINT64_C(0x5a414e5354524d47) /* "ZANSTRMG" */
#define ZAN_STRING_SENTINEL_RC UINT64_C(0xffffffffffffffff)

#define ZAN_STRING_TAG UINT64_C(0x5a414e53)      /* "ZANS" */
#define ZAN_STR_LEN_UNKNOWN UINT64_C(0x54524d47) /* "TRMG" */
#define ZAN_STR_LEN_MASK UINT64_C(0xffffffff)

#define ZAN_ARRAY_MAGIC UINT64_C(0x5a414e4152524159) /* "ZANARRAY" */

#define ZAN_ARR_HDR_SIZE 32
#define ZAN_ARR_COUNT_OFF (-16) /* 数组元素个数（i64） */
#define ZAN_ARR_RC_OFF (-32)
#define ZAN_ARR_RC_MAGIC_OFF (-24)
#define ZAN_ARRAY_RC_MAGIC UINT64_C(0x5a414e41525243) /* "ZANARRC" */

/* Soft-failure scratch (zan_rt_soft_scratch, rt_timer_diag.c). Outside a try a
 * null receiver continues on scratch memory: an object at the returned base P
 * (object header at P-32..P), and an empty array at P + ZAN_SOFT_ARR_BASE_OFF.
 * They cannot share a base: an array's i64 count at [-16] overlaps an object's
 * u32 rc at [-12], and the rc must stay large (a retained scratch object must
 * never be freed) while a null array must read count 0. The array's header and
 * ZAN_SOFT_ARR_SPAN zero bytes (element 0 of a clamped read) sit below the
 * object header, out of reach of object field writes (always >= P). */
#define ZAN_SOFT_ARR_SPAN 4064
#define ZAN_SOFT_ARR_BASE_OFF (-(ZAN_SOFT_ARR_SPAN + ZAN_ARR_HDR_SIZE))
#define ZAN_SOFT_PREFIX_SIZE (ZAN_ARR_HDR_SIZE + ZAN_SOFT_ARR_SPAN + 32)

#define ZAN_CLOSURE_TAG 1
#define ZAN_CLOSURE_FN_OFF 0
#define ZAN_CLOSURE_DTOR_OFF 8
#define ZAN_CLOSURE_TARGET_OFF 16

/* 2 */

#define ZAN_EH_CHUNKS 64
#define ZAN_EH_SLOT_SHIFT 6
#define ZAN_EH_SLOT_BYTES 1040
#define ZAN_EH_MARK_OFF 1024
#define ZAN_EH_TMP_SHIFT 12
#define ZAN_EH_THREADS 1024
#define ZAN_EH_TOMBSTONE 0xFFFFFFFFFFFFFFFFULL

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
  uint32_t offset; /* 对象内部字节偏移 */
  uint32_t is_rc;  /* 1=RC指针, 0=非RC */
} zan_field_ref_t;

typedef struct {
  uint32_t kind; /* 0=标准对象字段偏移, 1=List<T>, 2=Dict<K,V>, 3=叶子/无引用 */
  uint32_t ref_count;     /* RC 字段数量 (或 Dict 标志位) */
  uint32_t instance_size; /* 对象体尺寸 (字节) */
  uint32_t reserved;
  zan_field_ref_t refs[]; /* 各字段偏移列表 */
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
/* Optional NUL-terminated source label in immutable, process-lifetime storage.
 */
void zan_rt_live_register_named(void *obj, uint64_t size, const char *source);
void zan_rt_live_register_array(void *obj, uint64_t size, uint64_t data_offset,
                                int32_t elements_are_refs);
void zan_rt_live_unregister(void *obj);
/* 开启活对象注册表（GC.DetectCycles / CollectCycles / WriteHeapSnapshot 的程序由编译器在启动时调用）。 */
void zan_rt_live_enable(void);
void zan_rt_register_site_field_graphs(void **graphs, void **tynames,
                                       int64_t count);
/* --check-leaks 站点表模式的站点释放函数表（GC.CollectCycles 用） */
void zan_rt_register_site_dtors(void **dtors, int64_t count);
/* 默认（描述符）模式的站点号 → 描述符表（main 入口登记） */
void zan_rt_register_desc_table(void **tbl, int64_t count);
/* Enable only after all heap access is covered by paired mutator intervals.
 * Diagnostics return -1 when a scan cannot acquire a quiescent heap. */
void zan_rt_heap_diagnostics_enable(void);
void zan_rt_heap_mutator_enter(void);
void zan_rt_heap_mutator_leave(void);
int64_t zan_gc_detect_cycles(void);
/* 回收孤儿强引用环，返回释放的对象数（GC.CollectCycles；需调用方保证无并发改图） */
int64_t zan_gc_collect_cycles(void);
void zan_rt_dump_leaks(void);
int64_t zan_gc_write_heap_snapshot(const char *path);

#endif /* ZAN_ABI_H */
