#ifndef ZAN_RT_SYNC_TABLE_EDIT_H
#define ZAN_RT_SYNC_TABLE_EDIT_H

#include <stdint.h>

#define ZAN_PURGE_MAX_BATCH 64

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  ZAN_TABLE_EDIT_KEEP,
  ZAN_TABLE_EDIT_WRITE,
  ZAN_TABLE_EDIT_DELETE
} zan_table_edit_action;

/* The callback runs synchronously under the shared table locks. It must not
 * call table APIs, block, let control escape, or retain the staging pointers.
 * Values follow the requested column order. Missing rows have zero values and
 * expiry. WRITE stores values and expiry together, inserting if necessary.
 * Zero expiry persists the row. KEEP discards changes. DELETE removes an
 * existing row and ignores the staged values. */
typedef zan_table_edit_action (*zan_table_int_edit_fn)(void *context,
                                                       int exists,
                                                       int64_t *values,
                                                       int64_t *expires_at_ms);

/* Use an open table handle and distinct integer column names. Pass zero
 * columns to edit only the expiry or row existence. Purge up to purge_limit
 * rows as of now_ms (negative uses the wall clock) before invoking edit.
 * Returns 1 for a committed write or delete, otherwise 0. Invalid keys and
 * columns skip the callback. A write with negative expiry or a rejected
 * insert discards the staged changes. Any preceding purge still takes effect.
 */
int32_t zan_shared_table_edit_ints(int64_t handle, const char *key,
                                   int64_t now_ms, uint64_t purge_limit,
                                   const char *const *columns,
                                   uint32_t column_count,
                                   zan_table_int_edit_fn edit, void *context);

#ifdef __cplusplus
}
#endif

#endif
