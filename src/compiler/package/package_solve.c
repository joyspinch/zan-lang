
static bool pkg_token_is_shell_safe(const char *s) {
  if (!s)
    return true;
  for (const char *p = s; *p; p++) {
    unsigned char c = (unsigned char)*p;
    if (isalnum(c))
      continue;
    if (c == '%') {
      if (isxdigit((unsigned char)p[1]) && isxdigit((unsigned char)p[2])) {
        p += 2;
        continue;
      }
      return false;
    }
    if (strchr(":/@._~+-", c) != NULL)
      continue;
    return false;
  }
  return true;
}

static bool pkg_safe_component(const char *s);
static bool pkg_mkdirs(const char *path);
static bool pkg_copy_tree(const char *src, const char *dst, int depth);

bool zan_pkg_fetch(zan_pkg_registry_t *reg, const zan_dependency_t *dep) {
  char pkg_dir[1024];
  snprintf(pkg_dir, sizeof(pkg_dir), "%s" PATH_SEP "%s", reg->cache_dir,
           dep->name);
  if (pkg_is_dir(pkg_dir))
    return true;
  if (!pkg_safe_component(dep->name)) {
    fprintf(stderr,
            "error: refusing to fetch '%s': package name must be a single "
            "path component (letters, digits, '.', '_', '-'; no '..', "
            "'/', '\\\\')\n",
            dep->name);
    return false;
  }
  if (dep->source[0] && (strncmp(dep->source, "http", 4) == 0 ||
                         strncmp(dep->source, "git@", 4) == 0)) {
    char cmd[2048];
    char ver_buf[64];
    zan_version_format(&dep->min_ver, ver_buf, sizeof(ver_buf));
    if (!pkg_token_is_shell_safe(dep->name) ||
        !pkg_token_is_shell_safe(dep->source) ||
        !pkg_token_is_shell_safe(ver_buf)) {
      fprintf(stderr,
              "error: refusing to fetch '%s': unsafe characters in package, "
              "source or version\n",
              dep->name);
      return false;
    }
    snprintf(cmd, sizeof(cmd),
             "git clone --depth 1 --branch v%s \"%s\" \"%s\" 2>&1", ver_buf,
             dep->source, pkg_dir);
    int ret = system(cmd);
    if (ret != 0) {
      snprintf(cmd, sizeof(cmd), "git clone --depth 1 \"%s\" \"%s\" 2>&1",
               dep->source, pkg_dir);
      ret = system(cmd);
    }
    return ret == 0;
  } else if (dep->source[0] && pkg_is_dir(dep->source)) {
    if (!pkg_mkdirs(pkg_dir))
      return false;
    return pkg_copy_tree(dep->source, pkg_dir, 0);
  } else if (dep->source[0] == 0) {
    /* 1. 尝试从全局包存储恢复 */
    char gstore[1024];
    if (zan_pkg_global_store(gstore, sizeof(gstore))) {
      char src_dir[1024];
      snprintf(src_dir, sizeof(src_dir), "%s" PATH_SEP "%s", gstore, dep->name);
      if (pkg_is_dir(src_dir)) {
        if (!pkg_mkdirs(pkg_dir))
          return false;
        return pkg_copy_tree(src_dir, pkg_dir, 0);
      }
    }
    /* 2. 尝试从当前工程向上层目录回退寻找 packages/<dep->name> (Monorepo
     * 溯源，最多遍历 8 层) */
    char curr[1024];
    snprintf(curr, sizeof(curr), "%s", reg->cache_dir);
    char *sep = strrchr(curr, '\\');
    char *sep2 = strrchr(curr, '/');
    if (sep2 && (!sep || sep2 > sep))
      sep = sep2;
    if (sep)
      *sep = '\0';

    for (int depth = 0; depth < 8; depth++) {
      char cand[1024];
      snprintf(cand, sizeof(cand), "%s" PATH_SEP "packages" PATH_SEP "%s", curr,
               dep->name);
      if (pkg_is_dir(cand)) {
        if (!pkg_mkdirs(pkg_dir))
          return false;
        return pkg_copy_tree(cand, pkg_dir, 0);
      }
      char next[1024];
      snprintf(next, sizeof(next), "%s" PATH_SEP "..", curr);
      if (!pkg_is_dir(next))
        break;
      snprintf(curr, sizeof(curr), "%s", next);
    }
  }
  return false;
}

typedef struct {
  char name[128];
  char source[512];
  zan_version_range_t merged_range;
  zan_version_t resolved_ver;
  bool has_resolved_ver;
  char checksum[65];
  char sources[8][128];
  zan_version_range_t source_ranges[8];
  int source_count;
} zan_solver_entry_t;

typedef struct {
  zan_solver_entry_t entries[256];
  int entry_count;
} zan_diamond_solver_t;

static zan_solver_entry_t *solver_find_entry(zan_diamond_solver_t *solver,
                                             const char *name) {
  if (!solver || !name)
    return NULL;
  for (int i = 0; i < solver->entry_count; i++) {
    if (strcmp(solver->entries[i].name, name) == 0) {
      return &solver->entries[i];
    }
  }
  return NULL;
}

static zan_solver_entry_t *
solver_get_or_create_entry(zan_diamond_solver_t *solver, const char *name) {
  zan_solver_entry_t *e = solver_find_entry(solver, name);
  if (e)
    return e;
  if (solver->entry_count >= 256)
    return NULL;
  e = &solver->entries[solver->entry_count++];
  memset(e, 0, sizeof(*e));
  strncpy(e->name, name, sizeof(e->name) - 1);
  return e;
}

static bool solver_add_constraint(zan_diamond_solver_t *solver,
                                  const char *pkg_name, const char *parent_name,
                                  const zan_dependency_t *dep) {
  zan_solver_entry_t *entry = solver_get_or_create_entry(solver, pkg_name);
  if (!entry) {
    fprintf(
        stderr,
        "error: dependency solver exceeded maximum package capacity (256)\n");
    return false;
  }
  if (dep->source[0] && !entry->source[0]) {
    strncpy(entry->source, dep->source, sizeof(entry->source) - 1);
  }
  zan_version_range_t new_range;
  zan_dep_to_range(dep, &new_range);

  if (entry->source_count == 0) {
    entry->merged_range = new_range;
    strncpy(entry->sources[0], parent_name ? parent_name : "root",
            sizeof(entry->sources[0]) - 1);
    entry->source_ranges[0] = new_range;
    entry->source_count = 1;
    return true;
  }

  zan_version_range_t intersected;
  if (!zan_range_intersect(&entry->merged_range, &new_range, &intersected)) {
    char prev_buf[128], new_buf[128];
    zan_range_format(&entry->merged_range, prev_buf, sizeof(prev_buf));
    zan_range_format(&new_range, new_buf, sizeof(new_buf));
    fprintf(stderr, "error: version conflict for package '%s':\n", pkg_name);
    fprintf(stderr, "       '%s' requires %s %s\n", entry->sources[0], pkg_name,
            prev_buf);
    fprintf(stderr, "       '%s' requires %s %s\n",
            parent_name ? parent_name : "root", pkg_name, new_buf);
    fprintf(stderr, "       no compatible version satisfies both constraints "
                    "(empty range intersection)\n");
    return false;
  }

  entry->merged_range = intersected;
  if (entry->source_count < 8) {
    strncpy(entry->sources[entry->source_count],
            parent_name ? parent_name : "root", sizeof(entry->sources[0]) - 1);
    entry->source_ranges[entry->source_count] = new_range;
    entry->source_count++;
  }
  return true;
}

static bool zan_pkg_resolve_seen(zan_pkg_registry_t *reg, zan_package_t *root,
                                 char (*seen)[128], int *seen_count,
                                 zan_diamond_solver_t *solver) {
  bool all_ok = true;
  for (int i = 0; i < root->dep_count; i++) {
    zan_dependency_t *dep = &root->deps[i];
    fprintf(stderr, "Resolving: %s\n", dep->name);

    /* 1. 约束合并与冲突判定（PubGrub 风格因果链） */
    if (!solver_add_constraint(solver, dep->name, root->name, dep)) {
      all_ok = false;
      continue;
    }

    zan_solver_entry_t *entry = solver_find_entry(solver, dep->name);

    /* 2. 获取包源 */
    if (!zan_pkg_fetch(reg, dep)) {
      fprintf(stderr, "error: failed to fetch package '%s' from %s\n",
              dep->name, dep->source);
      all_ok = false;
      continue;
    }

    char manifest_path[1024];
    snprintf(manifest_path, sizeof(manifest_path),
             "%s" PATH_SEP "%s" PATH_SEP "zan.pkg", reg->cache_dir, dep->name);
    char pkg_dir[1024];
    snprintf(pkg_dir, sizeof(pkg_dir), "%s" PATH_SEP "%s", reg->cache_dir,
             dep->name);
    zan_package_t fetched_pkg;
    if (zan_pkg_load(&fetched_pkg, manifest_path)) {
      char computed_hash[65] = {0};
      /* B-ID90: 校验对象从 manifest 升级为整包源码树（防源码篡改） */
      bool has_hash = zan_pkg_compute_tree_checksum(pkg_dir, computed_hash);

      /* 3. 针对累计交集约束验证版本满足性 */
      if (!zan_version_satisfies_range(&fetched_pkg.version,
                                       &entry->merged_range)) {
        char ver_buf[64], range_buf[128];
        zan_version_format(&fetched_pkg.version, ver_buf, sizeof(ver_buf));
        zan_range_format(&entry->merged_range, range_buf, sizeof(range_buf));
        fprintf(stderr,
                "warning: package '%s' version %s does not satisfy merged "
                "constraint %s\n",
                dep->name, ver_buf, range_buf);
      }

      /* 4. 记录或更新仲裁解析结果 */
      entry->resolved_ver = fetched_pkg.version;
      entry->has_resolved_ver = true;
      if (has_hash) {
        strncpy(entry->checksum, computed_hash, sizeof(entry->checksum) - 1);
      }

      if (reg->resolved && reg->resolved_count < 256) {
        bool already_resolved = false;
        for (int r = 0; r < reg->resolved_count; r++) {
          if (reg->resolved[r] &&
              strcmp(reg->resolved[r]->name, fetched_pkg.name) == 0) {
            already_resolved = true;
            /* 钻石依赖仲裁升级：更新为当前仲裁选取的满足版本 */
            reg->resolved[r]->version = fetched_pkg.version;
            if (reg->resolved[r]->checksum[0] && has_hash) {
              if (strcmp(reg->resolved[r]->checksum, computed_hash) != 0) {
                fprintf(stderr,
                        "error: checksum mismatch for package '%s': expected "
                        "%s, got %s\n",
                        fetched_pkg.name, reg->resolved[r]->checksum,
                        computed_hash);
                zan_pkg_destroy(&fetched_pkg);
                return false;
              }
            } else if (has_hash) {
              strncpy(reg->resolved[r]->checksum, computed_hash,
                      sizeof(reg->resolved[r]->checksum) - 1);
            }
            break;
          }
        }
        if (!already_resolved) {
          zan_package_t *rp = (zan_package_t *)calloc(1, sizeof(zan_package_t));
          strncpy(rp->name, fetched_pkg.name, sizeof(rp->name) - 1);
          if (dep->source[0])
            strncpy(rp->source, dep->source, sizeof(rp->source) - 1);
          rp->version = fetched_pkg.version;
          rp->has_version = true;
          if (has_hash) {
            strncpy(rp->checksum, computed_hash, sizeof(rp->checksum) - 1);
          }
          reg->resolved[reg->resolved_count++] = rp;
        }
      }

      /* 5. 递归遍历子依赖树 */
      if (fetched_pkg.dep_count > 0) {
        bool seen_before = false;
        for (int s = 0; s < *seen_count; s++) {
          if (strcmp(seen[s], fetched_pkg.name) == 0) {
            seen_before = true;
            break;
          }
        }
        if (seen_before) {
          fprintf(stderr,
                  "note: '%s' is already resolved (shared dependency "
                  "or dependency cycle); skipping its subtree\n",
                  fetched_pkg.name);
        } else if (*seen_count < 256) {
          snprintf(seen[*seen_count], 128, "%s", fetched_pkg.name);
          (*seen_count)++;
          if (!zan_pkg_resolve_seen(reg, &fetched_pkg, seen, seen_count,
                                    solver))
            all_ok = false;
        } else {
          fprintf(stderr, "error: dependency chain deeper than 256 packages\n");
          all_ok = false;
        }
      }
      zan_pkg_destroy(&fetched_pkg);
    } else {
      fprintf(stderr, "error: failed to read manifest for package '%s'\n",
              dep->name);
      all_ok = false;
    }
  }
  return all_ok;
}

bool zan_pkg_resolve(zan_pkg_registry_t *reg, zan_package_t *root) {
  char seen[256][128];
  int seen_count = 0;
  if (root->name[0]) {
    snprintf(seen[seen_count++], 128, "%s", root->name);
  }
  zan_diamond_solver_t solver;
  memset(&solver, 0, sizeof(solver));
  return zan_pkg_resolve_seen(reg, root, seen, &seen_count, &solver);
}

char **zan_pkg_get_sources(zan_pkg_registry_t *reg, int *out_count) {
  (void)reg;
  *out_count = 0;
  return NULL;
}

bool zan_pkg_write_lock(zan_pkg_registry_t *reg) {
  if (!reg || !reg->lock_file)
    return false;
  FILE *f = fopen(reg->lock_file, "w");
  if (!f)
    return false;
  fprintf(f, "# Zan lock file - auto-generated\n\n");
  for (int i = 0; i < reg->resolved_count; i++) {
    zan_package_t *pkg = reg->resolved[i];
    if (!pkg)
      continue;
    char ver_buf[64];
    zan_version_format(&pkg->version, ver_buf, sizeof(ver_buf));
    fprintf(f, "[[package]]\nname = \"%s\"\nversion = \"%s\"\n", pkg->name,
            ver_buf);
    if (pkg->source[0]) {
      fprintf(f, "source = \"%s\"\n", pkg->source);
    }
    if (pkg->checksum[0]) {
      fprintf(f, "checksum = \"%s\"\n", pkg->checksum);
    }
    fprintf(f, "\n");
  }
  bool ok = (ferror(f) == 0);
  if (fclose(f) != 0)
    ok = false;
  return ok;
}

static bool add_locked_pkg(zan_pkg_registry_t *reg, const char *name,
                           const char *ver_str, const char *source,
                           const char *checksum) {
  if (!name || !name[0] || !ver_str || !ver_str[0])
    return false;
  if (!pkg_safe_component(name)) {
    fprintf(stderr, "error: unsafe package name '%s' in lock file\n", name);
    return false;
  }
  zan_version_t v;
  if (!zan_version_parse(ver_str, &v)) {
    fprintf(stderr,
            "error: invalid version '%s' for package '%s' in lock file\n",
            ver_str, name);
    return false;
  }
  if (!reg->resolved) {
    reg->resolved = (zan_package_t **)calloc(256, sizeof(zan_package_t *));
    if (!reg->resolved)
      zan_host_oom();
    reg->resolved_count = 0;
  }
  for (int i = 0; i < reg->resolved_count; i++) {
    if (reg->resolved[i] && strcmp(reg->resolved[i]->name, name) == 0) {
      fprintf(stderr, "error: duplicate package '%s' in lock file\n", name);
      return false;
    }
  }
  if (reg->resolved_count >= 256) {
    fprintf(stderr, "error: lock file contains too many packages (>256)\n");
    return false;
  }
  zan_package_t *pkg = (zan_package_t *)calloc(1, sizeof(zan_package_t));
  if (!pkg)
    zan_host_oom();
  strncpy(pkg->name, name, sizeof(pkg->name) - 1);
  pkg->version = v;
  pkg->has_version = true;
  if (source && source[0])
    strncpy(pkg->source, source, sizeof(pkg->source) - 1);
  if (checksum && checksum[0])
    strncpy(pkg->checksum, checksum, sizeof(pkg->checksum) - 1);
  reg->resolved[reg->resolved_count++] = pkg;
  return true;
}

bool zan_pkg_read_lock(zan_pkg_registry_t *reg) {
  if (!reg || !reg->lock_file)
    return false;
#ifdef _WIN32
  FILE *f = zan_utf8_fopen(reg->lock_file, "r");
#else
  FILE *f = fopen(reg->lock_file, "r");
#endif
  if (!f)
    return false;

  char line[1024];
  char cur_name[128] = {0};
  char cur_ver[64] = {0};
  char cur_source[512] = {0};
  char cur_checksum[65] = {0};
  bool in_pkg = false;
  bool ok = true;

  while (fgets(line, sizeof(line), f)) {
    const char *p = line;
    skip_ws(&p);
    if (*p == '#' || *p == '\n' || *p == '\r' || *p == 0)
      continue;
    if (*p == '[') {
      if (strncmp(p, "[[package]]", 11) != 0) {
        ok = false;
        break;
      }
      p += 11;
      skip_ws(&p);
      if (*p && *p != '#') {
        ok = false;
        break;
      }
      if (in_pkg &&
          !add_locked_pkg(reg, cur_name, cur_ver, cur_source, cur_checksum)) {
        ok = false;
        break;
      }
      in_pkg = true;
      cur_name[0] = 0;
      cur_ver[0] = 0;
      cur_source[0] = 0;
      cur_checksum[0] = 0;
      continue;
    }
    if (!in_pkg) {
      ok = false;
      break;
    }
    char key[128] = {0};
    int ki = 0;
    while (*p && *p != '=' && !isspace((unsigned char)*p) && ki < 127) {
      key[ki++] = *p;
      p++;
    }
    key[ki] = 0;
    skip_ws(&p);
    if (!ki || *p != '=') {
      ok = false;
      break;
    }
    p++;
    skip_ws(&p);
    char *field;
    size_t cap;
    if (strcmp(key, "name") == 0) {
      field = cur_name;
      cap = sizeof(cur_name);
    } else if (strcmp(key, "version") == 0) {
      field = cur_ver;
      cap = sizeof(cur_ver);
    } else if (strcmp(key, "source") == 0) {
      field = cur_source;
      cap = sizeof(cur_source);
    } else if (strcmp(key, "checksum") == 0) {
      field = cur_checksum;
      cap = sizeof(cur_checksum);
    } else {
      continue;
    }
    const char *end;
    if (*p == '"') {
      end = strchr(++p, '"');
      if (!end) {
        ok = false;
        break;
      }
      const char *tail = end + 1;
      skip_ws(&tail);
      if (*tail && *tail != '#') {
        ok = false;
        break;
      }
    } else {
      end = p + strcspn(p, "\r\n");
      while (end > p && isspace((unsigned char)end[-1]))
        end--;
    }
    size_t len = (size_t)(end - p);
    if (len >= cap) {
      ok = false;
      break;
    }
    memcpy(field, p, len);
    field[len] = 0;
  }
  if (ferror(f))
    ok = false;
  if (ok && in_pkg) {
    if (!cur_name[0] || !cur_ver[0] ||
        !add_locked_pkg(reg, cur_name, cur_ver, cur_source, cur_checksum)) {
      ok = false;
    }
  }
  fclose(f);
  return ok;
}

static char *locked_project_dir;
static zan_pkg_registry_t locked_project;

static const zan_pkg_registry_t *pkg_project_lock(const char *project_dir) {
  return locked_project_dir && strcmp(locked_project_dir, project_dir) == 0
             ? &locked_project
             : NULL;
}

static void pkg_clear_project_lock(void) {
  free(locked_project_dir);
  locked_project_dir = NULL;
  zan_pkg_registry_destroy(&locked_project);
}

static bool pkg_path_present(const char *path) {
#ifdef _WIN32
  wchar_t *wide = zan_utf8_to_wide_alloc(path);
  if (!wide)
    return true;
  DWORD attr = GetFileAttributesW(wide);
  DWORD error = GetLastError();
  free(wide);
  if (attr != INVALID_FILE_ATTRIBUTES)
    return true;
  return error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND;
#else
  struct stat st;
  return lstat(path, &st) == 0 || (errno != ENOENT && errno != ENOTDIR);
#endif
}

bool zan_pkg_project_has_lock(const char *project_dir) {
  if (!project_dir || !project_dir[0])
    return false;
  char path[4200];
  int n = snprintf(path, sizeof(path), "%s" PATH_SEP "zan.lock", project_dir);
  return n >= 0 && (size_t)n < sizeof(path) && pkg_path_present(path);
}

static bool pkg_plain_dir(const char *dir) {
#ifdef _WIN32
  DWORD attr = GetFileAttributesA(dir);
  return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) &&
         !(attr & FILE_ATTRIBUTE_REPARSE_POINT);
#else
  struct stat st;
  return lstat(dir, &st) == 0 && S_ISDIR(st.st_mode);
#endif
}

static bool pkg_locked_dir(const zan_pkg_registry_t *reg,
                           const zan_package_t *pkg, char *out, size_t cap) {
  int n = snprintf(out, cap, "%s" PATH_SEP "%s", reg->cache_dir, pkg->name);
  return n >= 0 && (size_t)n < cap;
}

static void pkg_package_source_dir(const char *root, char *out, size_t cap) {
  int n = snprintf(out, cap, "%s" PATH_SEP "src", root);
  if (n >= 0 && (size_t)n < cap && pkg_is_dir(out))
    return;
  n = snprintf(out, cap, "%s" PATH_SEP "stdlib", root);
  if (n >= 0 && (size_t)n < cap && pkg_is_dir(out))
    return;
  snprintf(out, cap, "%s", root);
}

static bool pkg_locked_dependencies_match(const zan_pkg_registry_t *reg,
                                          const zan_package_t *pkg) {
  for (int i = 0; i < pkg->dep_count; i++) {
    const zan_dependency_t *dep = &pkg->deps[i];
    const zan_package_t *locked = NULL;
    for (int j = 0; j < reg->resolved_count; j++) {
      if (strcmp(reg->resolved[j]->name, dep->name) == 0) {
        locked = reg->resolved[j];
        break;
      }
    }
    if (!locked || !zan_pkg_version_satisfies(dep, &locked->version)) {
      fprintf(
          stderr,
          "error: dependency '%s' of '%s' missing or incompatible in '%s'\n",
          dep->name, pkg->name, reg->lock_file);
      return false;
    }
    if (dep->source[0] && strcmp(dep->source, locked->source) != 0) {
      fprintf(stderr,
              "error: source mismatch for dependency '%s' of '%s' in '%s'\n",
              dep->name, pkg->name, reg->lock_file);
      return false;
    }
  }
  return true;
}

static bool pkg_validate_locked_package(const zan_pkg_registry_t *reg,
                                        const zan_package_t *locked) {
  char dir[1024], source[1024], manifest[1100];
  if (!pkg_locked_dir(reg, locked, dir, sizeof(dir)) || !pkg_plain_dir(dir)) {
    fprintf(stderr, "error: locked package '%s' is missing from '%s'\n",
            locked->name, reg->cache_dir);
    return false;
  }
  pkg_package_source_dir(dir, source, sizeof(source));
  if (!pkg_plain_dir(source)) {
    fprintf(stderr,
            "error: invalid source directory '%s' for locked package '%s'\n",
            source, locked->name);
    return false;
  }
  snprintf(manifest, sizeof(manifest), "%s" PATH_SEP "zan.pkg", dir);
  zan_package_t pkg = {0};
  bool ok = zan_pkg_load(&pkg, manifest);
  if (!ok || strcmp(pkg.name, locked->name) != 0) {
    fprintf(stderr,
            "error: manifest for locked package '%s' is missing or invalid at "
            "'%s'\n",
            locked->name, manifest);
    ok = false;
  } else if (!pkg.has_version ||
             zan_version_compare(&pkg.version, &locked->version) != 0) {
    char expected[64], actual[64];
    zan_version_format(&locked->version, expected, sizeof(expected));
    fprintf(stderr,
            "error: version mismatch for locked package '%s': expected %s, got "
            "%s\n",
            locked->name, expected,
            pkg.has_version
                ? zan_version_format(&pkg.version, actual, sizeof(actual))
                : "<invalid>");
    ok = false;
  }
  if (ok && locked->checksum[0]) {
    char actual[65];
    if (!zan_pkg_compute_tree_checksum(dir, actual)) {
      fprintf(stderr, "error: cannot verify checksum for locked package '%s'\n",
              locked->name);
      ok = false;
    } else if (strcmp(actual, locked->checksum) != 0) {
      fprintf(
          stderr,
          "error: checksum mismatch for package '%s': expected %s, got %s\n",
          locked->name, locked->checksum, actual);
      ok = false;
    }
  }
  if (ok)
    ok = pkg_locked_dependencies_match(reg, &pkg);
  zan_pkg_destroy(&pkg);
  return ok;
}

bool zan_pkg_prepare_project(const char *project_dir) {
  pkg_clear_project_lock();
  if (!zan_pkg_project_has_lock(project_dir))
    return true;

  zan_pkg_registry_t reg;
  pkg_registry_init(&reg, project_dir);
  bool ok = zan_pkg_read_lock(&reg);
  if (!ok) {
    fprintf(stderr, "error: cannot read valid package lock '%s'\n",
            reg.lock_file);
  } else if (reg.resolved_count && !pkg_plain_dir(reg.cache_dir)) {
    fprintf(stderr, "error: package cache '%s' is missing for lock '%s'\n",
            reg.cache_dir, reg.lock_file);
    ok = false;
  }
  for (int i = 0; ok && i < reg.resolved_count; i++)
    ok = pkg_validate_locked_package(&reg, reg.resolved[i]);

  if (ok) {
    char manifest[1200];
    snprintf(manifest, sizeof(manifest), "%s" PATH_SEP "zan.json", project_dir);
    if (!pkg_path_present(manifest))
      snprintf(manifest, sizeof(manifest), "%s" PATH_SEP "zan.pkg",
               project_dir);
    if (pkg_path_present(manifest)) {
      zan_package_t pkg = {0};
      if (zan_pkg_load(&pkg, manifest)) {
        ok = pkg_locked_dependencies_match(&reg, &pkg);
      } else {
        fprintf(stderr,
                "error: cannot read package manifest '%s' for lock '%s'\n",
                manifest, reg.lock_file);
        ok = false;
      }
      zan_pkg_destroy(&pkg);
    }
  }
  if (!ok) {
    zan_pkg_registry_destroy(&reg);
    return false;
  }

  static bool cleanup_registered;
  if (!cleanup_registered) {
    atexit(pkg_clear_project_lock);
    cleanup_registered = true;
  }
  locked_project_dir = strdup(project_dir);
  if (!locked_project_dir)
    zan_host_oom();
  locked_project = reg;
  return true;
}
