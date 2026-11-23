/* rt_sync_table_life.c: 共享内存表生命周期、挂载、内存驻留统计与原子行操作 */

typedef struct {
  uint64_t capacity;
  uint32_t key_size;
  uint32_t column_count;
  uint32_t row_stride;
  size_t total_size;
  zan_shared_column columns[ZAN_TABLE_MAX_COLUMNS];
} zan_table_layout;

static int zan_table_layout_of(int32_t capacity_value, int32_t key_size_value,
                               const char *schema, zan_table_layout *out) {
  if (capacity_value <= 0 || capacity_value > (int32_t)ZAN_TABLE_MAX_CAPACITY ||
      key_size_value <= 0 || key_size_value > ZAN_TABLE_MAX_KEY) {
    return 0;
  }
  memset(out, 0, sizeof(*out));
  out->capacity = zan_round_capacity((uint64_t)capacity_value);
  out->key_size = (uint32_t)key_size_value;
  if (!zan_parse_schema(schema, out->key_size, out->columns, &out->column_count,
                        &out->row_stride)) {
    return 0;
  }

  size_t rows_offset = zan_align8(sizeof(zan_shared_header));
  if ((size_t)out->capacity > (SIZE_MAX - rows_offset) / out->row_stride) {
    return 0;
  }
  size_t rows_size = (size_t)out->capacity * out->row_stride;
  if ((size_t)out->capacity >
      (SIZE_MAX - rows_offset - rows_size) / sizeof(uint64_t)) {
    return 0;
  }
  out->total_size =
      rows_offset + rows_size + (size_t)out->capacity * sizeof(uint64_t);
  return 1;
}

static int zan_header_geometry_ok(const zan_shared_header *header) {
  if (header->capacity == 0 || header->capacity > ZAN_TABLE_MAX_CAPACITY ||
      (header->capacity & (header->capacity - 1)) != 0)
    return 0;
  if (header->key_size == 0 || header->key_size > ZAN_TABLE_MAX_KEY)
    return 0;
  if (header->column_count > ZAN_TABLE_MAX_COLUMNS)
    return 0;
  size_t rows_offset = zan_align8(sizeof(zan_shared_header));
  size_t min_stride = zan_align8(ZAN_SLOT_PREFIX + (size_t)header->key_size);
  if (header->row_stride < min_stride)
    return 0;
  for (uint32_t i = 0; i < header->column_count; i++) {
    const zan_shared_column *c = &header->columns[i];
    if (c->type != ZAN_TABLE_INT && c->type != ZAN_TABLE_STRING &&
        c->type != ZAN_TABLE_FLOAT)
      return 0;
    if (c->size == 0 || (uint64_t)c->offset + c->size > header->row_stride)
      return 0;
    if (!memchr(c->name, '\0', sizeof(c->name)))
      return 0;
  }
  uint64_t expected = (uint64_t)rows_offset +
                      header->capacity * (uint64_t)header->row_stride +
                      header->capacity * sizeof(uint64_t);
  return expected == header->total_size;
}

/* 初始化并发布共享内存表头部结构 */
static void zan_table_init_header(zan_shared_table *table,
                                  const zan_table_layout *layout) {
  table->mapped_size = layout->total_size;
  memset(table->header, 0, sizeof(zan_shared_header));
  table->header->magic = ZAN_TABLE_MAGIC;
  table->header->version = ZAN_TABLE_VERSION;
  table->header->total_size = layout->total_size;
  table->header->capacity = layout->capacity;
  table->header->key_size = layout->key_size;
  table->header->row_stride = layout->row_stride;
  table->header->column_count = layout->column_count;
  memcpy(table->header->columns, layout->columns,
         sizeof(table->header->columns));
#ifdef _WIN32
  MemoryBarrier();
#else
  __sync_synchronize();
#endif
  table->header->ready = 1;
}

static zan_shared_table *zan_table_alloc(void) {
  zan_shared_table *table = (zan_shared_table *)calloc(1, sizeof(*table));
  if (!table)
    return NULL;
#ifndef _WIN32
  table->fd = -1;
  if (pthread_mutex_init(&table->local_mutex, NULL) != 0) {
    free(table);
    return NULL;
  }
  table->local_mutex_ready = 1;
#endif
  return table;
}

int64_t zan_shared_table_create_anon(int32_t capacity_value,
                                     int32_t key_size_value,
                                     const char *schema) {
  zan_table_layout layout;
  if (!zan_table_layout_of(capacity_value, key_size_value, schema, &layout)) {
    return 0;
  }

  zan_shared_table *table = zan_table_alloc();
  if (!table)
    return 0;
  table->anonymous = 1;

#ifdef _WIN32
  SECURITY_ATTRIBUTES sa;
  memset(&sa, 0, sizeof(sa));
  sa.nLength = sizeof(sa);
  sa.bInheritHandle = TRUE;
  DWORD size_high = (DWORD)(((uint64_t)layout.total_size) >> 32);
  DWORD size_low = (DWORD)((uint64_t)layout.total_size & UINT32_MAX);
  table->mapping = CreateFileMappingA(INVALID_HANDLE_VALUE, &sa, PAGE_READWRITE,
                                      size_high, size_low, NULL);
  if (!table->mapping) {
    zan_shared_table_free(table);
    return 0;
  }
  table->header = (zan_shared_header *)MapViewOfFile(
      table->mapping, FILE_MAP_ALL_ACCESS, 0, 0, layout.total_size);
  if (!table->header) {
    zan_shared_table_free(table);
    return 0;
  }
#else
  static uint32_t anon_seq = 0;
  char shm_name[64];
  uint32_t seq = __atomic_fetch_add(&anon_seq, 1, __ATOMIC_RELAXED);
  snprintf(shm_name, sizeof(shm_name), "/zan_table_%ld_%u_%u", (long)getpid(),
           (unsigned)time(NULL), seq);
  int fd = shm_open(shm_name, O_RDWR | O_CREAT | O_EXCL, 0600);
  if (fd < 0) {
    zan_shared_table_free(table);
    return 0;
  }
  shm_unlink(shm_name);
  if (fcntl(fd, F_SETFD, 0) != 0) {
    close(fd);
    zan_shared_table_free(table);
    return 0;
  }
  table->fd = fd;
  if (ftruncate(fd, (off_t)layout.total_size) != 0) {
    zan_shared_table_free(table);
    return 0;
  }
  table->header = (zan_shared_header *)mmap(
      NULL, layout.total_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (table->header == MAP_FAILED) {
    table->header = NULL;
    zan_shared_table_free(table);
    return 0;
  }
#endif

  zan_table_init_header(table, &layout);
  return (int64_t)(intptr_t)table;
}

int64_t zan_shared_table_handle(int64_t handle) {
  zan_shared_table *table = (zan_shared_table *)(intptr_t)handle;
  if (!table || !table->anonymous)
    return 0;
#ifdef _WIN32
  return (int64_t)(intptr_t)table->mapping;
#else
  return (int64_t)table->fd;
#endif
}

int64_t zan_shared_table_attach(int64_t os_handle) {
  if (os_handle <= 0)
    return 0;
  zan_shared_table *table = zan_table_alloc();
  if (!table)
    return 0;
  table->anonymous = 1;
  table->attached = 1;

#ifdef _WIN32
  table->mapping = (HANDLE)(intptr_t)os_handle;
  table->header = (zan_shared_header *)MapViewOfFile(
      table->mapping, FILE_MAP_ALL_ACCESS, 0, 0, 0);
  if (!table->header) {
    zan_shared_table_free(table);
    return 0;
  }
  {
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery(table->header, &mbi, sizeof(mbi)) ||
        mbi.RegionSize < sizeof(zan_shared_header)) {
      zan_shared_table_free(table);
      return 0;
    }
    table->mapped_size = mbi.RegionSize;
  }
#else
  table->fd = (int)os_handle;
  struct stat stat_buf;
  if (fstat(table->fd, &stat_buf) != 0 || stat_buf.st_size <= 0) {
    zan_shared_table_free(table);
    return 0;
  }
  table->mapped_size = (size_t)stat_buf.st_size;
  table->header = (zan_shared_header *)mmap(NULL, table->mapped_size,
                                            PROT_READ | PROT_WRITE, MAP_SHARED,
                                            table->fd, 0);
  if (table->header == MAP_FAILED) {
    table->header = NULL;
    zan_shared_table_free(table);
    return 0;
  }
#endif

#ifdef _WIN32
  MemoryBarrier();
#else
  __sync_synchronize();
#endif
  if (table->header->ready != 1 || table->header->magic != ZAN_TABLE_MAGIC ||
      table->header->version != ZAN_TABLE_VERSION ||
      table->header->total_size < sizeof(zan_shared_header) ||
      table->header->total_size > table->mapped_size ||
      !zan_header_geometry_ok(table->header)) {
    zan_shared_table_free(table);
    return 0;
  }
  return (int64_t)(intptr_t)table;
}

int64_t zan_shared_table_create(const char *name, int32_t capacity_value,
                                int32_t key_size_value, const char *schema) {
  if (!name || !*name)
    return 0;
  zan_table_layout layout;
  if (!zan_table_layout_of(capacity_value, key_size_value, schema, &layout)) {
    return 0;
  }
  size_t total_size = layout.total_size;

  zan_shared_table *table = zan_table_alloc();
  if (!table)
    return 0;
  zan_make_names(name, table->map_name);

#ifdef _WIN32
  DWORD size_high = (DWORD)(((uint64_t)total_size) >> 32);
  DWORD size_low = (DWORD)((uint64_t)total_size & UINT32_MAX);
  table->mapping =
      CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, size_high,
                         size_low, table->map_name);
  if (!table->mapping || GetLastError() == ERROR_ALREADY_EXISTS) {
    zan_shared_table_free(table);
    return 0;
  }
  table->header = (zan_shared_header *)MapViewOfFile(
      table->mapping, FILE_MAP_ALL_ACCESS, 0, 0, total_size);
  if (!table->header) {
    zan_shared_table_free(table);
    return 0;
  }
#else
  table->fd =
      open(table->map_name, O_RDWR | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
  if (table->fd < 0) {
    zan_shared_table_free(table);
    return 0;
  }
  if (ftruncate(table->fd, (off_t)total_size) != 0) {
    unlink(table->map_name);
    zan_shared_table_free(table);
    return 0;
  }
  table->header = (zan_shared_header *)mmap(
      NULL, total_size, PROT_READ | PROT_WRITE, MAP_SHARED, table->fd, 0);
  if (table->header == MAP_FAILED) {
    table->header = NULL;
    unlink(table->map_name);
    zan_shared_table_free(table);
    return 0;
  }
#endif

  zan_table_init_header(table, &layout);
  return (int64_t)(intptr_t)table;
}

int64_t zan_shared_table_open(const char *name) {
  if (!name || !*name)
    return 0;
  zan_shared_table *table = (zan_shared_table *)calloc(1, sizeof(*table));
  if (!table)
    return 0;
#ifndef _WIN32
  table->fd = -1;
  if (pthread_mutex_init(&table->local_mutex, NULL) != 0) {
    free(table);
    return 0;
  }
  table->local_mutex_ready = 1;
#endif
  zan_make_names(name, table->map_name);

#ifdef _WIN32
  table->mapping =
      OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, table->map_name);
  if (!table->mapping) {
    zan_shared_table_free(table);
    return 0;
  }
  table->header = (zan_shared_header *)MapViewOfFile(
      table->mapping, FILE_MAP_ALL_ACCESS, 0, 0, 0);
  if (!table->header) {
    zan_shared_table_free(table);
    return 0;
  }
  {
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery(table->header, &mbi, sizeof(mbi)) ||
        mbi.RegionSize < sizeof(zan_shared_header)) {
      zan_shared_table_free(table);
      return 0;
    }
    table->mapped_size = mbi.RegionSize;
  }
#else
  table->fd = open(table->map_name, O_RDWR | O_NOFOLLOW);
  if (table->fd < 0) {
    zan_shared_table_free(table);
    return 0;
  }
  struct stat stat_buf;
  if (fstat(table->fd, &stat_buf) != 0 || stat_buf.st_size <= 0) {
    zan_shared_table_free(table);
    return 0;
  }
  table->mapped_size = (size_t)stat_buf.st_size;
  table->header = (zan_shared_header *)mmap(NULL, table->mapped_size,
                                            PROT_READ | PROT_WRITE, MAP_SHARED,
                                            table->fd, 0);
  if (table->header == MAP_FAILED) {
    table->header = NULL;
    zan_shared_table_free(table);
    return 0;
  }
#endif

#ifdef _WIN32
  MemoryBarrier();
#else
  __sync_synchronize();
#endif
  if (table->header->ready != 1 || table->header->magic != ZAN_TABLE_MAGIC ||
      table->header->version != ZAN_TABLE_VERSION ||
      table->header->total_size < sizeof(zan_shared_header) ||
      table->header->total_size > table->mapped_size ||
      !zan_header_geometry_ok(table->header)) {
    zan_shared_table_free(table);
    return 0;
  }
#ifndef _WIN32
  if (table->header->total_size != table->mapped_size) {
    zan_shared_table_free(table);
    return 0;
  }
#endif
  return (int64_t)(intptr_t)table;
}

void zan_shared_table_close(int64_t handle) {
  zan_shared_table_free((zan_shared_table *)(intptr_t)handle);
}

int32_t zan_shared_table_destroy(int64_t handle) {
  zan_shared_table *table = (zan_shared_table *)(intptr_t)handle;
  if (!table)
    return 0;
  int32_t result = 1;
#ifndef _WIN32
  if (!table->anonymous) {
    if (unlink(table->map_name) != 0 && errno != ENOENT)
      result = 0;
  }
#endif
  zan_shared_table_free(table);
  return result;
}

#ifdef _WIN32
typedef struct {
  void *VirtualAddress;
  ULONG_PTR VirtualAttributes;
} zan_ws_ex_info;

typedef BOOL(WINAPI *zan_query_ws_ex_fn)(HANDLE, zan_ws_ex_info *, DWORD);
#endif

static int64_t zan_table_resident_bytes(const zan_shared_table *table) {
  if (!table->header || !table->mapped_size)
    return 0;
#ifdef _WIN32
  static zan_query_ws_ex_fn query_ws_ex = NULL;
  static int query_ws_ex_resolved = 0;
  if (!query_ws_ex_resolved) {
    HMODULE kernel = GetModuleHandleA("kernel32.dll");
    if (kernel) {
      query_ws_ex = (zan_query_ws_ex_fn)(void *)GetProcAddress(
          kernel, "K32QueryWorkingSetEx");
    }
    query_ws_ex_resolved = 1;
  }
  if (!query_ws_ex)
    return -1;
  SYSTEM_INFO info;
  GetSystemInfo(&info);
  size_t page_size = info.dwPageSize ? (size_t)info.dwPageSize : 4096u;
  size_t pages = (table->mapped_size + page_size - 1) / page_size;
  enum { ZAN_WS_BATCH = 4096 };
  zan_ws_ex_info *batch =
      (zan_ws_ex_info *)calloc(ZAN_WS_BATCH, sizeof(zan_ws_ex_info));
  if (!batch)
    return -1;
  HANDLE self = GetCurrentProcess();
  unsigned char *base = (unsigned char *)table->header;
  int64_t resident = 0;
  for (size_t first = 0; first < pages; first += ZAN_WS_BATCH) {
    size_t n = pages - first;
    if (n > ZAN_WS_BATCH)
      n = ZAN_WS_BATCH;
    for (size_t i = 0; i < n; i++) {
      batch[i].VirtualAddress = base + (first + i) * page_size;
      batch[i].VirtualAttributes = 0;
    }
    if (!query_ws_ex(self, batch, (DWORD)(n * sizeof(zan_ws_ex_info)))) {
      free(batch);
      return -1;
    }
    for (size_t i = 0; i < n; i++) {
      if (batch[i].VirtualAttributes & 1u)
        resident += (int64_t)page_size;
    }
  }
  free(batch);
  if (resident > (int64_t)table->mapped_size) {
    resident = (int64_t)table->mapped_size;
  }
  return resident;
#else
  long page_conf = sysconf(_SC_PAGESIZE);
  size_t page_size = page_conf > 0 ? (size_t)page_conf : 4096u;
  enum { ZAN_MINCORE_BATCH = 16384 };
  unsigned char *vec = (unsigned char *)malloc(ZAN_MINCORE_BATCH);
  if (!vec)
    return -1;
  unsigned char *base = (unsigned char *)table->header;
  size_t pages = (table->mapped_size + page_size - 1) / page_size;
  int64_t resident = 0;
  for (size_t first = 0; first < pages; first += ZAN_MINCORE_BATCH) {
    size_t n = pages - first;
    if (n > ZAN_MINCORE_BATCH)
      n = ZAN_MINCORE_BATCH;
    size_t offset = first * page_size;
    size_t length = n * page_size;
    if (offset + length > table->mapped_size) {
      length = table->mapped_size - offset;
    }
#if defined(__APPLE__)
    if (mincore((void *)(base + offset), length, (char *)vec) != 0) {
#else
    if (mincore((void *)(base + offset), length, vec) != 0) {
#endif
      free(vec);
      return -1;
    }
    for (size_t i = 0; i < n; i++) {
      if (vec[i] & 1u)
        resident += (int64_t)page_size;
    }
  }
  free(vec);
  if (resident > (int64_t)table->mapped_size) {
    resident = (int64_t)table->mapped_size;
  }
  return resident;
#endif
}

int64_t zan_shared_table_stat(int64_t handle, int32_t what) {
  zan_shared_table *table = (zan_shared_table *)(intptr_t)handle;
  if (!table || !table->header)
    return 0;
  switch (what) {
  case ZAN_TABLE_STAT_RESERVED:
    return (int64_t)table->mapped_size;
  case ZAN_TABLE_STAT_RESIDENT:
    return zan_table_resident_bytes(table);
  case ZAN_TABLE_STAT_CAPACITY:
    return (int64_t)table->header->capacity;
  case ZAN_TABLE_STAT_COUNT:
    return (int64_t)table->header->count;
  case ZAN_TABLE_STAT_ROW_STRIDE:
    return (int64_t)table->header->row_stride;
  case ZAN_TABLE_STAT_KEY_SIZE:
    return (int64_t)table->header->key_size;
  case ZAN_TABLE_STAT_COLUMNS:
    return (int64_t)table->header->column_count;
  default:
    return 0;
  }
}

int32_t zan_shared_table_expire(int64_t handle, const char *key,
                                int64_t ttl_ms) {
  if (ttl_ms < 0)
    return 0;
  int64_t now_ms = zan_time_epoch_ms();
  if (ttl_ms > INT64_MAX - now_ms)
    return 0;
  return zan_shared_table_expire_at(handle, key, now_ms + ttl_ms);
}

int32_t zan_shared_table_expire_at(int64_t handle, const char *key,
                                   int64_t expires_at_ms) {
  zan_shared_table *table = (zan_shared_table *)(intptr_t)handle;
  if (!table || !key || expires_at_ms < 0)
    return 0;
  zan_struct_lock(table->header);
  zan_purge_expired_locked(table->header, zan_time_epoch_ms(),
                           ZAN_PURGE_MAX_BATCH);
  unsigned char *row = zan_find_row(table->header, key, 0);
  if (row) {
    zan_row_lock(row);
    zan_expiry_set(table->header, zan_row_slot(table->header, row), row,
                   expires_at_ms);
    zan_row_unlock(row);
  }
  zan_struct_unlock(table->header);
  return row != NULL;
}

int64_t zan_shared_table_expires_at(int64_t handle, const char *key) {
  zan_shared_table *table = (zan_shared_table *)(intptr_t)handle;
  if (!table || !key)
    return -1;
  zan_purge_expired(table->header);
  unsigned char *row = zan_find_row(table->header, key, 0);
  if (!row)
    return -1;
  zan_row_lock(row);
  int64_t expires_at = -1;
  if (zan_row_revalidate(row, zan_hash_bytes(key), key,
                         table->header->key_size)) {
    expires_at = *zan_row_expires_at(row);
  }
  zan_row_unlock(row);
  return expires_at;
}

int64_t zan_shared_table_purge_expired(int64_t handle, int64_t now_ms) {
  zan_shared_table *table = (zan_shared_table *)(intptr_t)handle;
  if (!table)
    return 0;
  if (now_ms < 0)
    now_ms = zan_time_epoch_ms();
  zan_struct_lock(table->header);
  uint64_t before = table->header->count;
  zan_purge_expired_locked(table->header, now_ms, UINT64_MAX);
  uint64_t removed = before - table->header->count;
  zan_struct_unlock(table->header);
  return (int64_t)removed;
}

int32_t zan_shared_table_edit_ints(int64_t handle, const char *key,
                                   int64_t now_ms, uint64_t purge_limit,
                                   const char *const *columns,
                                   uint32_t column_count,
                                   zan_table_int_edit_fn edit, void *context) {
  zan_shared_table *table = (zan_shared_table *)(intptr_t)handle;
  if (!table || !key || !edit || column_count > ZAN_TABLE_MAX_COLUMNS ||
      (column_count && !columns))
    return 0;

  zan_shared_header *header = table->header;
  uint32_t offsets[ZAN_TABLE_MAX_COLUMNS];
  uint64_t selected = 0;
  for (uint32_t i = 0; i < column_count; i++) {
    zan_shared_column *column =
        zan_find_column(header, columns[i], ZAN_TABLE_INT);
    if (!column)
      return 0;
    uint64_t bit = UINT64_C(1) << (column - header->columns);
    if (selected & bit)
      return 0;
    selected |= bit;
    offsets[i] = column->offset;
  }

  size_t key_len = zan_strnlen(key, header->key_size + 1u);
  zan_struct_lock(header);
  if (now_ms < 0)
    now_ms = zan_time_epoch_ms();
  zan_purge_expired_locked(header, now_ms, purge_limit);
  if (key_len == 0 || key_len > header->key_size) {
    zan_struct_unlock(header);
    return 0;
  }

  uint64_t hash = zan_hash_bytes(key);
  unsigned char *row = zan_find_key_slot(header, key, hash, 0);
  int exists = row != NULL;
  int64_t values[ZAN_TABLE_MAX_COLUMNS];
  int64_t expires_at = 0;
  if (row) {
    zan_row_lock(row);
    for (uint32_t i = 0; i < column_count; i++)
      memcpy(&values[i], row + offsets[i], sizeof(values[i]));
    expires_at = *zan_row_expires_at(row);
  } else {
    memset(values, 0, column_count * sizeof(*values));
  }

  int committed = 0;
  zan_table_edit_action action = edit(context, exists, values, &expires_at);
  if (action == ZAN_TABLE_EDIT_WRITE && expires_at >= 0) {
    if (!row) {
      row = zan_find_key_slot(header, key, hash, 1);
      if (row)
        zan_row_lock(row);
    }
    if (row) {
      /* After publication, typed readers still wait on the row lock. */
      if (!exists)
        zan_publish_key_row(header, row, key, key_len, hash);
      for (uint32_t i = 0; i < column_count; i++)
        memcpy(row + offsets[i], &values[i], sizeof(values[i]));
      if (expires_at != *zan_row_expires_at(row))
        zan_expiry_set(header, zan_row_slot(header, row), row, expires_at);
      committed = 1;
    }
  } else if (action == ZAN_TABLE_EDIT_DELETE && row) {
    zan_delete_row_locked(header, row);
    committed = 1;
  }
  if (row)
    zan_row_unlock(row);
  zan_struct_unlock(header);
  return committed;
}

int32_t zan_shared_table_delete(int64_t handle, const char *key) {
  zan_shared_table *table = (zan_shared_table *)(intptr_t)handle;
  if (!table)
    return 0;
  zan_struct_lock(table->header);
  zan_purge_expired_locked(table->header, zan_time_epoch_ms(),
                           ZAN_PURGE_MAX_BATCH);
  unsigned char *row = zan_find_row(table->header, key, 0);
  if (row) {
    zan_row_lock(row);
    zan_delete_row_locked(table->header, row);
    zan_row_unlock(row);
  }
  zan_struct_unlock(table->header);
  return row != NULL;
}

int32_t zan_shared_table_exists(int64_t handle, const char *key) {
  zan_shared_table *table = (zan_shared_table *)(intptr_t)handle;
  if (!table)
    return 0;
  zan_purge_expired(table->header);
  return zan_find_row(table->header, key, 0) != NULL;
}

int64_t zan_shared_table_count(int64_t handle) {
  zan_shared_table *table = (zan_shared_table *)(intptr_t)handle;
  if (!table)
    return 0;
  zan_purge_expired(table->header);
  return (int64_t)table->header->count;
}

void zan_shared_table_clear(int64_t handle) {
  zan_shared_table *table = (zan_shared_table *)(intptr_t)handle;
  if (!table)
    return;
  zan_struct_lock(table->header);
  for (uint64_t i = 0; i < table->header->capacity; i++) {
    unsigned char *row = zan_row_at(table->header, i);
    uint32_t state = zan_load_state(row);
    if (state == ZAN_SLOT_EMPTY)
      continue;
    zan_row_lock(row);
    if (zan_load_state(row) != ZAN_SLOT_EMPTY) {
      if (zan_load_state(row) == ZAN_SLOT_USED)
        memset(row + 8, 0, table->header->row_stride - 8);
      zan_publish_state(row, ZAN_SLOT_EMPTY);
    }
    zan_row_unlock(row);
  }
  memset(zan_expiry_heap(table->header), 0,
         (size_t)table->header->capacity * sizeof(uint64_t));
  table->header->count = 0;
  table->header->expiry_count = 0;
  zan_struct_unlock(table->header);
}

/* Keep the stdlib's native compatibility symbols in the existing sync object.
 */
#include "rt_sync_table_compat.c"
