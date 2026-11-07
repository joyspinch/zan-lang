/* rt_sync_table_ops.c: 共享内存表强类型列值存取与哈希位置检索器 */

int32_t zan_shared_table_set_int(
    int64_t handle, const char *key, const char *column_name, int64_t value) {
    zan_shared_table *table = (zan_shared_table *)(intptr_t)handle;
    if (!table) return 0;
    zan_shared_column *column = zan_find_column(
        table->header, column_name, ZAN_TABLE_INT);
    unsigned char *row = column ? zan_row_for(table->header, key, 1) : NULL;
    if (!row) return 0;
    zan_row_lock(row);
    if (!zan_row_revalidate(row, zan_hash_bytes(key), key,
                            table->header->key_size)) {
        zan_row_unlock(row);
        return 0;
    }
    memcpy(row + column->offset, &value, sizeof(value));
    zan_row_unlock(row);
    return 1;
}

int64_t zan_shared_table_get_int(
    int64_t handle, const char *key, const char *column_name) {
    zan_shared_table *table = (zan_shared_table *)(intptr_t)handle;
    if (!table) return 0;
    int64_t value = 0;
    zan_shared_column *column = zan_find_column(
        table->header, column_name, ZAN_TABLE_INT);
    unsigned char *row = column ? zan_row_for(table->header, key, 0) : NULL;
    if (!row) return 0;
    zan_row_lock(row);
    if (!zan_row_revalidate(row, zan_hash_bytes(key), key,
                            table->header->key_size)) {
        zan_row_unlock(row);
        return 0;
    }
    memcpy(&value, row + column->offset, sizeof(value));
    zan_row_unlock(row);
    return value;
}

/* get_int 的容错版：命中写出 *out_value 并返回 1；键不存在/列缺失/
 * 行失效返回 0（不写出）。区分"值恰好是 0"与"没读到"。 */
int32_t zan_shared_table_try_get_int(
    int64_t handle, const char *key, const char *column_name,
    int64_t *out_value) {
    zan_shared_table *table = (zan_shared_table *)(intptr_t)handle;
    if (!table || !out_value) return 0;
    zan_shared_column *column = zan_find_column(
        table->header, column_name, ZAN_TABLE_INT);
    unsigned char *row = column ? zan_row_for(table->header, key, 0) : NULL;
    if (!row) return 0;
    zan_row_lock(row);
    if (!zan_row_revalidate(row, zan_hash_bytes(key), key,
                            table->header->key_size)) {
        zan_row_unlock(row);
        return 0;
    }
    memcpy(out_value, row + column->offset, sizeof(*out_value));
    zan_row_unlock(row);
    return 1;
}

int32_t zan_shared_table_set_float(
    int64_t handle, const char *key, const char *column_name, double value) {
    zan_shared_table *table = (zan_shared_table *)(intptr_t)handle;
    if (!table) return 0;
    zan_shared_column *column = zan_find_column(
        table->header, column_name, ZAN_TABLE_FLOAT);
    unsigned char *row = column ? zan_row_for(table->header, key, 1) : NULL;
    if (!row) return 0;
    zan_row_lock(row);
    if (!zan_row_revalidate(row, zan_hash_bytes(key), key,
                            table->header->key_size)) {
        zan_row_unlock(row);
        return 0;
    }
    memcpy(row + column->offset, &value, sizeof(value));
    zan_row_unlock(row);
    return 1;
}

double zan_shared_table_get_float(
    int64_t handle, const char *key, const char *column_name) {
    zan_shared_table *table = (zan_shared_table *)(intptr_t)handle;
    if (!table) return 0.0;
    double value = 0.0;
    zan_shared_column *column = zan_find_column(
        table->header, column_name, ZAN_TABLE_FLOAT);
    unsigned char *row = column ? zan_row_for(table->header, key, 0) : NULL;
    if (!row) return 0.0;
    zan_row_lock(row);
    if (!zan_row_revalidate(row, zan_hash_bytes(key), key,
                            table->header->key_size)) {
        zan_row_unlock(row);
        return 0.0;
    }
    memcpy(&value, row + column->offset, sizeof(value));
    zan_row_unlock(row);
    return value;
}

int32_t zan_shared_table_set_string(
    int64_t handle, const char *key, const char *column_name, const char *value) {
    zan_shared_table *table = (zan_shared_table *)(intptr_t)handle;
    if (!table || !value) return 0;
    zan_shared_column *column = zan_find_column(
        table->header, column_name, ZAN_TABLE_STRING);
    size_t value_len = column ? strlen(value) : 0;
    unsigned char *row =
        column && value_len < column->size
            ? zan_row_for(table->header, key, 1)
            : NULL;
    if (!row) return 0;
    zan_row_lock(row);
    if (!zan_row_revalidate(row, zan_hash_bytes(key), key,
                            table->header->key_size)) {
        zan_row_unlock(row);
        return 0;
    }
    char *destination = (char *)(row + column->offset);
    memcpy(destination, value, value_len);
    destination[value_len] = '\0';
    zan_row_unlock(row);
    return 1;
}

const char *zan_shared_table_get_string(
    int64_t handle, const char *key, const char *column_name) {
    zan_shared_table *table = (zan_shared_table *)(intptr_t)handle;
    char *result = zan_get_shared_string();
    result[0] = '\0';
    if (!table) return result;
    zan_shared_column *column = zan_find_column(
        table->header, column_name, ZAN_TABLE_STRING);
    unsigned char *row = column ? zan_row_for(table->header, key, 0) : NULL;
    if (!row) return result;
    zan_row_lock(row);
    size_t max_len = 0;
    if (zan_row_revalidate(row, zan_hash_bytes(key), key,
                           table->header->key_size)) {
        max_len = column->size - 1u;
        size_t len = zan_strnlen((const char *)(row + column->offset), max_len);
        memcpy(result, row + column->offset, len);
        result[len] = '\0';
    }
    zan_row_unlock(row);
    return result;
}

int64_t zan_shared_table_increment(
    int64_t handle, const char *key, const char *column_name, int64_t delta) {
    zan_shared_table *table = (zan_shared_table *)(intptr_t)handle;
    if (!table) return 0;
    int64_t value = 0;
    zan_shared_column *column = zan_find_column(
        table->header, column_name, ZAN_TABLE_INT);
    unsigned char *row = column ? zan_row_for(table->header, key, 1) : NULL;
    if (!row) return 0;
    zan_row_lock(row);
    if (!zan_row_revalidate(row, zan_hash_bytes(key), key,
                            table->header->key_size)) {
        zan_row_unlock(row);
        return 0;
    }
    memcpy(&value, row + column->offset, sizeof(value));
    value += delta;
    memcpy(row + column->offset, &value, sizeof(value));
    zan_row_unlock(row);
    return value;
}

int64_t zan_shared_table_hash(const char *value) {
    return (int64_t)zan_hash_bytes(value);
}

int32_t zan_shared_table_set_int_at(
    int64_t handle, int64_t key_hash, const char *column_name, int64_t value) {
    zan_shared_table *table = (zan_shared_table *)(intptr_t)handle;
    if (!table) return 0;
    zan_shared_column *column = zan_find_column(
        table->header, column_name, ZAN_TABLE_INT);
    unsigned char *row =
        column ? zan_row_for_hash(table->header, (uint64_t)key_hash, 1) : NULL;
    if (!row) return 0;
    zan_row_lock(row);
    if (!zan_row_revalidate(row, (uint64_t)key_hash, NULL, 0)) {
        zan_row_unlock(row);
        return 0;
    }
    memcpy(row + column->offset, &value, sizeof(value));
    zan_row_unlock(row);
    return 1;
}

int64_t zan_shared_table_get_int_at(
    int64_t handle, int64_t key_hash, const char *column_name) {
    zan_shared_table *table = (zan_shared_table *)(intptr_t)handle;
    if (!table) return 0;
    int64_t value = 0;
    zan_shared_column *column = zan_find_column(
        table->header, column_name, ZAN_TABLE_INT);
    unsigned char *row =
        column ? zan_row_for_hash(table->header, (uint64_t)key_hash, 0) : NULL;
    if (!row) return 0;
    zan_row_lock(row);
    if (!zan_row_revalidate(row, (uint64_t)key_hash, NULL, 0)) {
        zan_row_unlock(row);
        return 0;
    }
    memcpy(&value, row + column->offset, sizeof(value));
    zan_row_unlock(row);
    return value;
}

/* get_int_at 的容错版，口径同 zan_shared_table_try_get_int。 */
int32_t zan_shared_table_try_get_int_at(
    int64_t handle, int64_t key_hash, const char *column_name,
    int64_t *out_value) {
    zan_shared_table *table = (zan_shared_table *)(intptr_t)handle;
    if (!table || !out_value) return 0;
    zan_shared_column *column = zan_find_column(
        table->header, column_name, ZAN_TABLE_INT);
    unsigned char *row =
        column ? zan_row_for_hash(table->header, (uint64_t)key_hash, 0) : NULL;
    if (!row) return 0;
    zan_row_lock(row);
    if (!zan_row_revalidate(row, (uint64_t)key_hash, NULL, 0)) {
        zan_row_unlock(row);
        return 0;
    }
    memcpy(out_value, row + column->offset, sizeof(*out_value));
    zan_row_unlock(row);
    return 1;
}

int64_t zan_shared_table_increment_at(
    int64_t handle, int64_t key_hash, const char *column_name, int64_t delta) {
    zan_shared_table *table = (zan_shared_table *)(intptr_t)handle;
    if (!table) return 0;
    int64_t value = 0;
    zan_shared_column *column = zan_find_column(
        table->header, column_name, ZAN_TABLE_INT);
    unsigned char *row =
        column ? zan_row_for_hash(table->header, (uint64_t)key_hash, 1) : NULL;
    if (!row) return 0;
    zan_row_lock(row);
    if (!zan_row_revalidate(row, (uint64_t)key_hash, NULL, 0)) {
        zan_row_unlock(row);
        return 0;
    }
    memcpy(&value, row + column->offset, sizeof(value));
    value += delta;
    memcpy(row + column->offset, &value, sizeof(value));
    zan_row_unlock(row);
    return value;
}

int64_t zan_shared_table_extreme_at(
    int64_t handle, int64_t key_hash, const char *column_name, int64_t value,
    int64_t keep_larger) {
    zan_shared_table *table = (zan_shared_table *)(intptr_t)handle;
    if (!table) return 0;
    int64_t stored = 0;
    zan_shared_column *column = zan_find_column(
        table->header, column_name, ZAN_TABLE_INT);
    unsigned char *row =
        column ? zan_row_for_hash(table->header, (uint64_t)key_hash, 1) : NULL;
    if (!row) return 0;
    zan_row_lock(row);
    if (!zan_row_revalidate(row, (uint64_t)key_hash, NULL, 0)) {
        zan_row_unlock(row);
        return 0;
    }
    memcpy(&stored, row + column->offset, sizeof(stored));
    int replace = keep_larger ? (value > stored) : (stored == 0 || value < stored);
    if (replace) {
        memcpy(row + column->offset, &value, sizeof(value));
        stored = value;
    }
    zan_row_unlock(row);
    return stored;
}

int32_t zan_shared_table_set_string_at(
    int64_t handle, int64_t key_hash, const char *column_name,
    const char *value) {
    zan_shared_table *table = (zan_shared_table *)(intptr_t)handle;
    if (!table || !value) return 0;
    zan_shared_column *column = zan_find_column(
        table->header, column_name, ZAN_TABLE_STRING);
    size_t value_len = column ? strlen(value) : 0;
    unsigned char *row =
        column && value_len < column->size
            ? zan_row_for_hash(table->header, (uint64_t)key_hash, 1)
            : NULL;
    if (!row) return 0;
    zan_row_lock(row);
    if (!zan_row_revalidate(row, (uint64_t)key_hash, NULL, 0)) {
        zan_row_unlock(row);
        return 0;
    }
    char *destination = (char *)(row + column->offset);
    memcpy(destination, value, value_len);
    destination[value_len] = '\0';
    zan_row_unlock(row);
    return 1;
}

const char *zan_shared_table_get_string_at(
    int64_t handle, int64_t key_hash, const char *column_name) {
    zan_shared_table *table = (zan_shared_table *)(intptr_t)handle;
    char *result = zan_get_shared_string();
    result[0] = '\0';
    if (!table) return result;
    zan_shared_column *column = zan_find_column(
        table->header, column_name, ZAN_TABLE_STRING);
    unsigned char *row =
        column ? zan_row_for_hash(table->header, (uint64_t)key_hash, 0) : NULL;
    if (!row) return result;
    zan_row_lock(row);
    size_t max_len = 0;
    if (zan_row_revalidate(row, (uint64_t)key_hash, NULL, 0)) {
        max_len = column->size - 1u;
        size_t len = zan_strnlen((const char *)(row + column->offset), max_len);
        memcpy(result, row + column->offset, len);
        result[len] = '\0';
    }
    zan_row_unlock(row);
    return result;
}

int32_t zan_shared_table_match_at(
    int64_t handle, int64_t key_hash, const char *column_name,
    const char *text) {
    zan_shared_table *table = (zan_shared_table *)(intptr_t)handle;
    if (!table || !text) return 0;
    zan_shared_column *column = zan_find_column(
        table->header, column_name, ZAN_TABLE_STRING);
    unsigned char *row =
        column ? zan_find_row_hash(table->header, (uint64_t)key_hash, 0) : NULL;
    if (!row) return 0;
    zan_row_lock(row);
    int equal = 0;
    if (zan_row_revalidate(row, (uint64_t)key_hash, NULL, 0))
        equal = strncmp(
            (const char *)(row + column->offset), text, column->size) == 0;
    zan_row_unlock(row);
    return equal ? 1 : 0;
}

int32_t zan_shared_table_exists_at(int64_t handle, int64_t key_hash) {
    zan_shared_table *table = (zan_shared_table *)(intptr_t)handle;
    if (!table) return 0;
    zan_purge_expired(table->header);
    return zan_find_row_hash(table->header, (uint64_t)key_hash, 0) != NULL;
}

int32_t zan_shared_table_delete_at(int64_t handle, int64_t key_hash) {
    zan_shared_table *table = (zan_shared_table *)(intptr_t)handle;
    if (!table) return 0;
    zan_struct_lock(table->header);
    zan_purge_expired_locked(table->header, zan_wall_now_ms(), ZAN_PURGE_MAX_BATCH);
    unsigned char *row = zan_find_row_hash(
        table->header, (uint64_t)key_hash, 0);
    if (row) {
        zan_row_lock(row);
        zan_expiry_remove(table->header, row);
        memset(row + 8, 0, table->header->row_stride - 8);
        zan_publish_state(row, ZAN_SLOT_TOMBSTONE);
        table->header->count--;
        zan_row_unlock(row);
    }
    zan_struct_unlock(table->header);
    return row != NULL;
}
