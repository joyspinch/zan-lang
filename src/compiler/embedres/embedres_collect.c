static int embed_push(zan_embed_list_t *l, char *name, unsigned char *data,
                      long long len) {
  if (l->n == l->cap) {
    int cap = l->cap ? l->cap * 2 : 32;
    zan_embed_file_t *v =
        (zan_embed_file_t *)realloc(l->v, (size_t)cap * sizeof(*v));
    if (!v)
      return 0;
    l->v = v;
    l->cap = cap;
  }
  l->v[l->n].name = name;
  l->v[l->n].data = data;
  l->v[l->n].len = len;
  l->n++;
  return 1;
}

static unsigned char *embed_read_file(const char *path, long long *out_len) {
  FILE *f = fopen(path, "rb");
  if (!f)
    return NULL;
  if (fseek(f, 0, SEEK_END) != 0) {
    fclose(f);
    return NULL;
  }
  long size = ftell(f);
  if (size < 0) {
    fclose(f);
    return NULL;
  }
  rewind(f);
  unsigned char *buf = (unsigned char *)malloc((size_t)size + 1);
  if (!buf) {
    fclose(f);
    return NULL;
  }
  size_t got = size > 0 ? fread(buf, 1, (size_t)size, f) : 0;
  fclose(f);
  buf[got] = 0;
  *out_len = (long long)got;
  return buf;
}

static char *embed_join(const char *a, const char *b) {
  size_t la = strlen(a), lb = strlen(b);
  char *s = (char *)malloc(la + lb + 2);
  if (!s)
    return NULL;
  if (la) {
    memcpy(s, a, la);
    s[la] = '/';
    memcpy(s + la + 1, b, lb + 1);
  } else {
    memcpy(s, b, lb + 1);
  }
  return s;
}

static int embed_add_file(zan_embed_list_t *l, const char *path,
                          const char *name) {
  long long len = 0;
  unsigned char *data = embed_read_file(path, &len);
  if (!data) {
    fprintf(stderr, "warning: cannot embed '%s' (unreadable)\n", path);
    return 0;
  }
  char *dup = (char *)malloc(strlen(name) + 1);
  if (!dup) {
    free(data);
    return 0;
  }
  memcpy(dup, name, strlen(name) + 1);
  if (!embed_push(l, dup, data, len)) {
    free(dup);
    free(data);
    return 0;
  }
  return 1;
}

#define EMBED_WALK_MAX_DEPTH 128

static void embed_walk_impl(zan_embed_list_t *l, const char *dir,
                            const char *name, int depth) {
  if (depth > EMBED_WALK_MAX_DEPTH)
    return;
#ifdef _WIN32
  char pattern[1024];
  snprintf(pattern, sizeof(pattern), "%s\\*", dir);
  wchar_t *wide_pattern = zan_utf8_to_wide_alloc(pattern);
  if (!wide_pattern)
    return;
  WIN32_FIND_DATAW fd;
  HANDLE h = FindFirstFileW(wide_pattern, &fd);
  if (h == INVALID_HANDLE_VALUE) {
    free(wide_pattern);
    return;
  }
  free(wide_pattern);
  do {
    char *file_name = zan_wide_to_utf8_alloc(fd.cFileName);
    if (!file_name)
      continue;
    if (strcmp(file_name, ".") == 0 || strcmp(file_name, "..") == 0) {
      free(file_name);
      continue;
    }
    char path[1024];
    snprintf(path, sizeof(path), "%s\\%s", dir, file_name);
    char *sub = embed_join(name, file_name);
    free(file_name);
    if (!sub)
      continue;
    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
      if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
        free(sub);
        continue;
      }
      embed_walk_impl(l, path, sub, depth + 1);
    } else {
      embed_add_file(l, path, sub);
    }
    free(sub);
  } while (FindNextFileW(h, &fd));
  FindClose(h);
#else
  DIR *d = opendir(dir);
  if (!d)
    return;
  struct dirent *e;
  while ((e = readdir(d)) != NULL) {
    if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0)
      continue;
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
    struct stat st;
    if (lstat(path, &st) != 0)
      continue;
    char *sub = embed_join(name, e->d_name);
    if (!sub)
      continue;
    if (S_ISDIR(st.st_mode))
      embed_walk_impl(l, path, sub, depth + 1);
    else if (S_ISREG(st.st_mode))
      embed_add_file(l, path, sub);
    free(sub);
  }
  closedir(d);
#endif
}

static void embed_walk(zan_embed_list_t *l, const char *dir, const char *name) {
  embed_walk_impl(l, dir, name, 1);
}

static void embed_walk_filtered_impl(zan_embed_list_t *l, const char *dir,
                                     const char *name, int depth,
                                     const char *const *filter,
                                     int filter_count) {
  if (depth > EMBED_WALK_MAX_DEPTH)
    return;
#ifdef _WIN32
  char pattern[1024];
  snprintf(pattern, sizeof(pattern), "%s\\*", dir);
  wchar_t *wide_pattern = zan_utf8_to_wide_alloc(pattern);
  if (!wide_pattern)
    return;
  WIN32_FIND_DATAW fd;
  HANDLE h = FindFirstFileW(wide_pattern, &fd);
  free(wide_pattern);
  if (h == INVALID_HANDLE_VALUE)
    return;
  do {
    char *file_name = zan_wide_to_utf8_alloc(fd.cFileName);
    if (!file_name)
      continue;
    if (strcmp(file_name, ".") == 0 || strcmp(file_name, "..") == 0) {
      free(file_name);
      continue;
    }
    char path[1024];
    snprintf(path, sizeof(path), "%s\\%s", dir, file_name);
    char *sub = embed_join(name, file_name);
    free(file_name);
    if (!sub)
      continue;
    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
      if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
        free(sub);
        continue;
      }
      int keep = depth == 1 ? 0 : 1;
      const char *leaf = strrchr(sub, '/');
      leaf = leaf ? leaf + 1 : sub;
      for (int f = 0; depth == 1 && f < filter_count; f++) {
        if (strcmp(filter[f], leaf) == 0) {
          keep = 1;
          break;
        }
      }
      if (keep) {
        if (depth == 1)
          embed_walk_impl(l, path, sub, depth + 1);
        else
          embed_walk_filtered_impl(l, path, sub, depth + 1, filter,
                                   filter_count);
      }
    } else {
      embed_add_file(l, path, sub);
    }
    free(sub);
  } while (FindNextFileW(h, &fd));
  FindClose(h);
#else
  DIR *d = opendir(dir);
  if (!d)
    return;
  struct dirent *e;
  while ((e = readdir(d)) != NULL) {
    if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0)
      continue;
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
    struct stat st;
    if (lstat(path, &st) != 0)
      continue;
    char *sub = embed_join(name, e->d_name);
    if (!sub)
      continue;
    if (S_ISDIR(st.st_mode)) {
      int keep = depth == 1 ? 0 : 1;
      const char *leaf = strrchr(sub, '/');
      leaf = leaf ? leaf + 1 : sub;
      for (int f = 0; depth == 1 && f < filter_count; f++) {
        if (strcmp(filter[f], leaf) == 0) {
          keep = 1;
          break;
        }
      }
      if (keep) {
        if (depth == 1)
          embed_walk_impl(l, path, sub, depth + 1);
        else
          embed_walk_filtered_impl(l, path, sub, depth + 1, filter,
                                   filter_count);
      }
    } else if (S_ISREG(st.st_mode))
      embed_add_file(l, path, sub);
    free(sub);
  }
  closedir(d);
#endif
}

static void embed_walk_filtered(zan_embed_list_t *l, const char *dir,
                                const char *name, const char *const *filter,
                                int filter_count) {
  embed_walk_filtered_impl(l, dir, name, 1, filter, filter_count);
}

static int embed_is_dir(const char *path) {
