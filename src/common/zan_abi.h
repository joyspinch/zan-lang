/* zan_abi.h - object-model ABI shared by the compiler's emitted code
 * (src/compiler/irgen*.c) and the runtime allocator (src/runtime/rt_mem.c).
 *
 * Every heap object -- class instance, string, or array -- is a 16-byte
 * header followed by the payload, and the pointer a program holds points at
 * the payload so it can be handed to C unchanged:
 *
 *   obj - 16: i64 first header word -- refcount (classes/strings), or the
 *             element count (arrays)
 *   obj -  8: i64 second header word -- leak-site index, or
 *             ZAN_STRING_MAGIC on strings / ZAN_ARRAY_MAGIC on arrays
 *   obj +  0: user data
 *
 * These numbers are the ONLY layout facts shared across the two sides: the
 * compiler emits the header writes/reads inline as LLVM IR, the allocator
 * only has to keep payloads 16-byte aligned (its own 16-byte header, see
 * ZAN_MEM_HDR in rt_mem.c, is asserted against ZAN_OBJ_HDR_SIZE). Keep them
 * in sync here, never restate them as literals at call sites.
 *
 * Section 2 holds the exception-handling ABI, which is private to the code
 * the compiler emits (no runtime counterpart today) but kept here so the
 * whole generated-code object model lives in one place. */

#ifndef ZAN_ABI_H
#define ZAN_ABI_H

#include <stdint.h>

/* ---- 1. runtime-visible object ABI ---- */

/* Size of the header in front of every object's payload. */
#define ZAN_OBJ_HDR_SIZE  16

/* First header word: i64 refcount for RC-managed objects, element count for
 * arrays (see zan_array_alloc/zan_array_len). */
#define ZAN_OBJ_RC_OFF    (-16)

/* Second header word: i64 leak-site index (or ZAN_STRING_MAGIC on strings).
 * Tolerant retain/release probe this slot to tell a managed string from a
 * bare buffer before touching the refcount. */
#define ZAN_OBJ_SITE_OFF  (-8)

/* String RC header magic and sentinel refcount. The second header word
 * doubles as a guard for tolerant retain/release. */
#define ZAN_STRING_MAGIC       UINT64_C(0x5a414e5354524d47) /* "ZANSTRMG" */
#define ZAN_STRING_SENTINEL_RC UINT64_C(0xffffffffffffffff)

/* Array header magic, in the same slot (arrays keep their element count in the
 * first word and take no leak-site index). It is what tells a byte[] apart
 * from a bare `char*` an extern handed back: both reach code typed `string`
 * with no string magic, but only the array actually has a count word in front
 * of its payload. Rectangular arrays (`int[,]`) spend this word on their rank
 * instead, so they are not classifiable this way -- they never reach a
 * `string`. */
#define ZAN_ARRAY_MAGIC        UINT64_C(0x5a414e4152524159) /* "ZANARRAY" */

/* Delegate values. One pointer with two shapes, told apart by bit 0:
 *   even -- a bare function pointer (static method or non-capturing lambda),
 *           so it can be handed to C as a plain callback;
 *   odd  -- a tagged pointer to a heap closure record
 *           { void *fn, void *dtor, void *target, <captured values> },
 *           allocated with the object allocator above (so it carries the
 *           16-byte rc header) and invoked as fn(record, args...).
 * Retain is an increment of the record's refcount; release goes through the
 * record's own dtor, which drops what the record captured and frees it at
 * zero. Runtime code that *keeps* a delegate past the call it arrived on (the
 * UI dispatch queue in rt_sync.c) must retain it, or the closure is freed
 * while its owner -- an event's handler list -- still holds it. */
#define ZAN_CLOSURE_TAG        1
#define ZAN_CLOSURE_FN_OFF     0
#define ZAN_CLOSURE_DTOR_OFF   8
#define ZAN_CLOSURE_TARGET_OFF 16

/* ---- 2. emitted-code ABI (no runtime counterpart yet) ----
 * Exception-handling chunk tables. The state is per-thread, reached through
 * __zan_eh_state(); the full design rationale lives in
 * src/compiler/irgen_builtins.c above emit_eh_state_ty. */

#define ZAN_EH_CHUNKS      64     /* chunk-table entries, both stacks */
#define ZAN_EH_SLOT_SHIFT  6      /* 64 handler slots per chunk */
#define ZAN_EH_SLOT_BYTES  1040   /* jmp_buf (1024) + mark, 16-byte aligned */
#define ZAN_EH_MARK_OFF    1024
#define ZAN_EH_TMP_SHIFT   12     /* 4096 unwind-stack entries per chunk */
#define ZAN_EH_THREADS     1024   /* live threads that can raise exceptions */
/* A slot whose thread released its block: claimable again, but a probe must
 * pass through it or it would cut the chain of the keys stored after it. */
#define ZAN_EH_TOMBSTONE   0xFFFFFFFFFFFFFFFFULL

/* Unwind-stack entry flavours: the flavour of the skipped local decides how
 * a throw releases it while unwinding (see emit_eh_unwind_to_handler). */
enum {
    ZAN_EH_SLOT_OBJ = 0,
    ZAN_EH_SLOT_STR = 1,
    ZAN_EH_SLOT_DLG = 2
};

#endif /* ZAN_ABI_H */
