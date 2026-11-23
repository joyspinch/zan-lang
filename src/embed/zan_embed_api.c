#include <string.h>
#include <stdlib.h>
#include <stdint.h>

#include "../common/host_oom.h"
#include "../common/zan_abi.h"
#include "zan_embed_string.h"

typedef struct {
  const char *name;
  const unsigned char *data;
  long long len;
} zan_embed_ent;

static const zan_embed_ent *g_tab = 0;
static long long g_cnt = 0;

void zan_embed_register(const zan_embed_ent *tbl, long long n) {
  g_tab = tbl;
  g_cnt = n;
}

static const zan_embed_ent *zan_embed_find(const char *name) {
  long long i;
  if (!name || !g_tab)
    return 0;
  for (i = 0; i < g_cnt; i++)
    if (g_tab[i].name && strcmp(g_tab[i].name, name) == 0)
      return &g_tab[i];
  return 0;
}

/* Registration may supply raw bytes with no readable prefix. Only the text
 * API wraps them; the binary API continues returning the original pointer. */
const char *zan_embed_read(const char *name) {
  const zan_embed_ent *e = zan_embed_find(name);
  if (!e || e->len < 0)
    return zan_embed_string_empty();
  return zan_embed_string_view(e->data, (uint64_t)e->len);
}

int zan_embed_has(const char *name) { return zan_embed_find(name) ? 1 : 0; }

const unsigned char *zan_embed_bytes(const char *name, int *outLen) {
  const zan_embed_ent *e = zan_embed_find(name);
  if (!e) {
    if (outLen)
      *outLen = 0;
    return 0;
  }
  if (outLen)
    *outLen = (int)e->len;
  return e->data;
}

/* The nint result is a caller-owned, NUL-terminated buffer. */
int64_t zan_embed_list_alloc(const char *prefix) {
  const zan_embed_ent *table = g_tab;
  long long count = g_cnt;
  if (!table)
    return 0;
  size_t prefix_len = prefix ? strlen(prefix) : 0;
  size_t len = 0;
  for (long long i = 0; i < count; i++) {
    const char *name = table[i].name;
    if (!name || (prefix_len && strncmp(name, prefix, prefix_len) != 0))
      continue;
    size_t name_len = strlen(name);
    if (name_len >= INT32_MAX - len)
      return 0;
    len += name_len + 1;
  }
  if (!len)
    return 0;
  char *names = malloc(len + 1);
  if (!names)
    return 0;
  size_t offset = 0;
  for (long long i = 0; i < count; i++) {
    const char *name = table[i].name;
    if (!name || (prefix_len && strncmp(name, prefix, prefix_len) != 0))
      continue;
    size_t name_len = strlen(name);
    memcpy(names + offset, name, name_len);
    offset += name_len;
    names[offset++] = '\n';
  }
  names[offset] = '\0';
  return (int64_t)(uintptr_t)names;
}
