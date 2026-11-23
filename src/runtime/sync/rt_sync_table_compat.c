/* Compatibility entry points used by the stdlib SharedTable policy methods. */

#include "rt_sync_table_edit.h"
#include "../rt_sync.h"
#include "../rt_timer.h"

static const char *const zan_rate_columns[] = {"count", "window_start"};
static const char *const zan_lease_columns[] = {"owner"};

typedef struct {
  int64_t now_ms;
  int64_t window_ms;
  int64_t limit;
} zan_rate_window;

static zan_table_edit_action zan_apply_rate_window(void *context, int exists,
                                                   int64_t *values,
                                                   int64_t *expires_at) {
  const zan_rate_window *window = (const zan_rate_window *)context;
  (void)exists;
  int64_t start = values[1];
  if (start <= 0 || window->now_ms - start >= window->window_ms) {
    values[0] = 1;
    values[1] = window->now_ms;
    *expires_at = window->now_ms + window->window_ms;
    return ZAN_TABLE_EDIT_WRITE;
  }
  if (values[0] >= window->limit)
    return ZAN_TABLE_EDIT_KEEP;
  values[0]++;
  return ZAN_TABLE_EDIT_WRITE;
}

int32_t zan_shared_table_rate_allow(int64_t handle, const char *key,
                                    int64_t now_ms, int64_t window_ms,
                                    int64_t limit) {
  if (limit <= 0)
    return 1;
  if (!handle || !key || window_ms <= 0)
    return 0;
  if (now_ms < 0)
    now_ms = zan_time_epoch_ms();
  if (now_ms < 0 || window_ms > INT64_MAX - now_ms)
    return 0;

  zan_rate_window window = {now_ms, window_ms, limit};
  return zan_shared_table_edit_ints(handle, key, now_ms, ZAN_PURGE_MAX_BATCH,
                                    zan_rate_columns, 2, zan_apply_rate_window,
                                    &window);
}

typedef struct {
  int64_t owner;
  int64_t expires_at;
} zan_table_lease;

static zan_table_edit_action zan_insert_lease(void *context, int exists,
                                              int64_t *values,
                                              int64_t *expires_at) {
  if (exists)
    return ZAN_TABLE_EDIT_KEEP;
  const zan_table_lease *lease = (const zan_table_lease *)context;
  values[0] = lease->owner;
  *expires_at = lease->expires_at;
  return ZAN_TABLE_EDIT_WRITE;
}

int32_t zan_shared_table_lock_acquire(int64_t handle, const char *key,
                                      int64_t owner, int64_t now_ms,
                                      int64_t lease_ms) {
  if (!handle || !key || owner == 0 || lease_ms <= 0)
    return 0;
  if (now_ms < 0)
    now_ms = zan_time_epoch_ms();
  if (now_ms < 0 || lease_ms > INT64_MAX - now_ms)
    return 0;

  zan_table_lease lease = {owner, now_ms + lease_ms};
  return zan_shared_table_edit_ints(handle, key, now_ms, UINT64_MAX,
                                    zan_lease_columns, 1, zan_insert_lease,
                                    &lease);
}

static zan_table_edit_action zan_delete_owned_lease(void *context, int exists,
                                                    int64_t *values,
                                                    int64_t *expires_at) {
  const int64_t owner = *(const int64_t *)context;
  (void)expires_at;
  return exists && values[0] == owner ? ZAN_TABLE_EDIT_DELETE
                                      : ZAN_TABLE_EDIT_KEEP;
}

int32_t zan_shared_table_lock_release(int64_t handle, const char *key,
                                      int64_t owner) {
  if (owner == 0)
    return 0;
  return zan_shared_table_edit_ints(handle, key, -1, ZAN_PURGE_MAX_BATCH,
                                    zan_lease_columns, 1,
                                    zan_delete_owned_lease, &owner);
}
