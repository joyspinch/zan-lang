bool zan_dllimport_name_is_safe(const char *name, int len) {
    if (!name || len <= 0) return false;
    for (int i = 0; i < len; i++) {
        unsigned char c = (unsigned char)name[i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9')) continue;
        if (c == '.' || c == '_' || c == '+' || c == '-') continue;
        return false;
    }
    return true;
}

static const char *zan_dllimport_lname(const char *lib, int lib_len,
                                       int *out_len) {
    if (lib_len == 3 && memcmp(lib, "crt", 3) == 0) return NULL;
    if (lib_len == 6 && memcmp(lib, "msvcrt", 6) == 0) return NULL;
    const char *name = lib;
    int n = lib_len;
    if (n > 3 && memcmp(name, "lib", 3) == 0) { name += 3; n -= 3; }
    if (n == 1 && (name[0] == 'c' || name[0] == 'm')) return NULL;
    if (!zan_dllimport_name_is_safe(name, n)) {
        fprintf(stderr,
                "error: [DllImport] library name '%.*s' contains characters "
                "that are not allowed on a linker command line "
                "(letters, digits, '.', '_', '+', '-' only)\n",
                n, name);
        return NULL;
    }
    *out_len = n;
    return name;
}

static bool zan_win_system_lib(const char *lib, int lib_len) {
    static const char *const names[] = {
        "kernel32", "user32", "gdi32", "advapi32", "shell32", "shlwapi",
        "ole32", "oleaut32", "comdlg32", "comctl32", "gdiplus", "dwmapi",
        "shcore", "uxtheme", "msimg32", "winmm", "ws2_32", "opengl32",
        "psapi", "version", "setupapi", "secur32", "iphlpapi", "crypt32",
        "bcrypt", "wininet", "winhttp", "urlmon", "userenv", "uuid",
        "rpcrt4", "imm32", NULL };
    for (int i = 0; names[i]; i++)
        if ((int)strlen(names[i]) == lib_len
            && memcmp(names[i], lib, (size_t)lib_len) == 0)
            return true;
    return false;
}

#define ZAN_MAX_DRIVERS 1024
typedef struct {
    char lib[64];      /* normalized -l basename, e.g. "sqlite3", "zan_gui" */
    char module[512];
    char sym[64];
    char root[1024];
} zan_driver_entry_t;
typedef struct {
    zan_driver_entry_t entries[ZAN_MAX_DRIVERS];
    int count;
} zan_driver_registry_t;

static void zan_registry_add(zan_driver_registry_t *reg,
                             const char *lib, const char *module,
                             const char *sym, const char *root) {
    if (reg->count >= ZAN_MAX_DRIVERS) return;
    for (int i = 0; i < reg->count; i++)
        if (strcmp(reg->entries[i].lib, lib) == 0) return;
    snprintf(reg->entries[reg->count].lib,
             sizeof(reg->entries[0].lib), "%s", lib);
    snprintf(reg->entries[reg->count].module,
             sizeof(reg->entries[0].module), "%s", module);
    snprintf(reg->entries[reg->count].sym,
             sizeof(reg->entries[0].sym), "%s", sym ? sym : "");
    snprintf(reg->entries[reg->count].root,
             sizeof(reg->entries[0].root), "%s", root ? root : "");
    reg->count++;
}

static void zan_read_driver_manifest(const char *manifest_path,
                                     const char *module,
                                     zan_driver_registry_t *reg,
                                     const char *root) {
    FILE *f = fopen(manifest_path, "rb");
    if (!f) return;
    char line[128];
    while (fgets(line, sizeof(line), f)) {
        char *s = line;
        while (*s == ' ' || *s == '\t') s++;
        size_t len = strlen(s);
        while (len > 0 && (s[len - 1] == '\n' || s[len - 1] == '\r' ||
                           s[len - 1] == ' ' || s[len - 1] == '\t'))
            s[--len] = '\0';
        if (s[0] == '\0' || s[0] == '#') continue;
        const char *sym = "";
        char *cond = strstr(s, " if ");
        if (cond) {
            *cond = '\0';
            sym = cond + 4;
            while (*sym == ' ' || *sym == '\t') sym++;
            size_t ll = strlen(s);
            while (ll > 0 && (s[ll - 1] == ' ' || s[ll - 1] == '\t'))
                s[--ll] = '\0';
            if (s[0] == '\0') continue;
        }
        zan_registry_add(reg, s, module, sym, root);
    }
    fclose(f);
}

typedef void (*zan_stdlib_dir_callback)(const char *dir_full,
                                        const char *rel, void *ctx);
static bool zan_file_exists(const char *path);
static bool zan_is_safe_bundle_name(const char *name);

static void zan_walk_stdlib_dirs(const char *dir_full, const char *rel,
                                 int depth, zan_stdlib_dir_callback visit,
                                 void *ctx) {
    if (depth > 8) return;
    visit(dir_full, rel, ctx);
#ifdef _WIN32
    char glob[1200];
    snprintf(glob, sizeof(glob), "%s\\*", dir_full);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(glob, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
            const char *nm = fd.cFileName;
            if (nm[0] == '.') continue;
            if (strcmp(nm, "drivers") == 0 || strcmp(nm, "native") == 0) continue;
            char sub[1200];
            snprintf(sub, sizeof(sub), "%s\\%s", dir_full, nm);
            char subrel[512];
            if (rel[0]) snprintf(subrel, sizeof(subrel), "%s/%s", rel, nm);
            else snprintf(subrel, sizeof(subrel), "%s", nm);
            zan_walk_stdlib_dirs(sub, subrel, depth + 1, visit, ctx);
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
#else
    DIR *d = opendir(dir_full);
    if (d) {
        struct dirent *ent;
        while ((ent = readdir(d)) != NULL) {
            const char *nm = ent->d_name;
            if (nm[0] == '.') continue;
            if (strcmp(nm, "drivers") == 0 || strcmp(nm, "native") == 0) continue;
            char sub[1200];
            snprintf(sub, sizeof(sub), "%s/%s", dir_full, nm);
            struct stat st;
            if (stat(sub, &st) != 0 || !S_ISDIR(st.st_mode)) continue;
            char subrel[512];
            if (rel[0]) snprintf(subrel, sizeof(subrel), "%s/%s", rel, nm);
            else snprintf(subrel, sizeof(subrel), "%s", nm);
            zan_walk_stdlib_dirs(sub, subrel, depth + 1, visit, ctx);
        }
        closedir(d);
    }
#endif
}

typedef struct {
    zan_driver_registry_t *reg;
    const char *root;
} zan_driver_root_scan_t;

static void zan_scan_driver_module(const char *dir_full, const char *rel,
                                   void *ctx) {
    if (!rel[0]) return;
    zan_driver_root_scan_t *scan = (zan_driver_root_scan_t *)ctx;
    char manifest[1200];
    snprintf(manifest, sizeof(manifest), "%s/drivers/driver.manifest", dir_full);
    zan_read_driver_manifest(manifest, rel, scan->reg, scan->root);
}

typedef char zan_package_source_root_t[1024];

static zan_package_source_root_t *zan_collect_package_source_roots(int *count) {
    int cap = 32;
    zan_package_source_root_t *roots = NULL;
    for (;;) {
        zan_package_source_root_t *grown = realloc(roots, (size_t)cap * sizeof(*roots));
        if (!grown) {
            free(roots);
            fprintf(stderr, "error: out of memory discovering package source roots\n");
            exit(1);
        }
        roots = grown;
        *count = zan_pkg_all_source_roots(package_project_root, roots, cap);
        if (*count < cap) return roots;
        if (cap > INT32_MAX / 2 || (size_t)cap > SIZE_MAX / sizeof(*roots) / 2) {
            free(roots);
            fprintf(stderr, "error: too many package source roots\n");
            exit(1);
        }
        cap *= 2;
    }
}

static void zan_discover_drivers(const char *stdlib_root,
                                 zan_driver_registry_t *reg) {
    reg->count = 0;
    zan_driver_root_scan_t scan;
    if (stdlib_root && stdlib_root[0]) {
        scan.reg = reg;
        scan.root = stdlib_root;
        zan_walk_stdlib_dirs(stdlib_root, "", 0, zan_scan_driver_module, &scan);
    }
    int pkg_n;
    zan_package_source_root_t *pkg_roots = zan_collect_package_source_roots(&pkg_n);
    for (int i = 0; i < pkg_n; i++) {
        scan.reg = reg;
        scan.root = pkg_roots[i];
        zan_walk_stdlib_dirs(pkg_roots[i], "", 0, zan_scan_driver_module,
                             &scan);
    }
    free(pkg_roots);
}

static int zan_driver_find(const zan_driver_registry_t *reg,
                           const char *lname, int len) {
    for (int i = 0; i < reg->count; i++)
        if ((int)strlen(reg->entries[i].lib) == len &&
            memcmp(reg->entries[i].lib, lname, (size_t)len) == 0) return i;
    return -1;
}

typedef struct {
    const char *target_subdir;
    const char *library_name;
    char result[1200];
} zan_static_library_search_t;

static void zan_find_static_library_dir(const char *dir_full, const char *rel,
                                        void *ctx) {
    (void)rel;
    zan_static_library_search_t *search = (zan_static_library_search_t *)ctx;
    if (search->result[0]) return;
    char archive[1400];
    int n = snprintf(archive, sizeof(archive),
                     "%s/drivers/%s/static/lib%s.a", dir_full,
                     search->target_subdir, search->library_name);
    if (n < 0 || (size_t)n >= sizeof(archive)) return;
    if (!zan_file_exists(archive)) return;
    n = snprintf(search->result, sizeof(search->result),
                 "%s/drivers/%s/static", dir_full, search->target_subdir);
    if (n < 0 || (size_t)n >= sizeof(search->result))
        search->result[0] = '\0';
}

static bool zan_find_static_library(const char *stdlib_root,
                                    const char *target_subdir,
                                    const char *library_name,
                                    char *out, size_t outsz) {
    if (!stdlib_root || !target_subdir || !library_name ||
        !zan_is_safe_bundle_name(library_name))
        return false;
    zan_static_library_search_t search = {
        .target_subdir = target_subdir,
        .library_name = library_name,
        .result = {0}
    };
    zan_walk_stdlib_dirs(stdlib_root, "", 0,
                         zan_find_static_library_dir, &search);
    if (!search.result[0]) {
        int pkg_n;
        zan_package_source_root_t *pkg_roots = zan_collect_package_source_roots(&pkg_n);
        for (int i = 0; i < pkg_n && !search.result[0]; i++)
            zan_walk_stdlib_dirs(pkg_roots[i], "", 0,
                                 zan_find_static_library_dir, &search);
        free(pkg_roots);
    }
    if (!search.result[0] || strlen(search.result) >= outsz) return false;
    snprintf(out, outsz, "%s", search.result);
    return true;
}

static bool zan_resolve_gui_resource_dir(const char *stdlib_root,
                                        const char *rel, char *out,
                                        size_t cap) {
    if (stdlib_root && stdlib_root[0]) {
        snprintf(out, cap, "%s/%s", stdlib_root, rel);
        if (zan_file_exists(out)) return true;
    }
    int pkg_n;
    zan_package_source_root_t *pkg_roots = zan_collect_package_source_roots(&pkg_n);
    for (int i = 0; i < pkg_n; i++) {
        snprintf(out, cap, "%s/%s", pkg_roots[i], rel);
        if (zan_file_exists(out)) {
            free(pkg_roots);
            return true;
        }
    }
    free(pkg_roots);
    out[0] = '\0';
    return false;
}

static int zan_copy_file_ex(const char *src, const char *dst, char *err_buf, size_t err_cap) {
    if (err_buf && err_cap > 0) err_buf[0] = '\0';
#ifdef _WIN32
    if (_stricmp(src, dst) == 0) return 0;
    { WIN32_FILE_ATTRIBUTE_DATA sa, da;
      if (GetFileAttributesExA(src, GetFileExInfoStandard, &sa) &&
          GetFileAttributesExA(dst, GetFileExInfoStandard, &da)) {
          HANDLE hs = CreateFileA(src, 0, FILE_SHARE_READ | FILE_SHARE_WRITE,
              NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
          HANDLE hd = CreateFileA(dst, 0, FILE_SHARE_READ | FILE_SHARE_WRITE,
              NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
          if (hs != INVALID_HANDLE_VALUE && hd != INVALID_HANDLE_VALUE) {
              BY_HANDLE_FILE_INFORMATION is, id;
              if (GetFileInformationByHandle(hs, &is) &&
                  GetFileInformationByHandle(hd, &id) &&
                  is.dwVolumeSerialNumber == id.dwVolumeSerialNumber &&
                  is.nFileIndexHigh == id.nFileIndexHigh &&
                  is.nFileIndexLow == id.nFileIndexLow) {
                  CloseHandle(hs); CloseHandle(hd);
                  return 0;
              }
          }
          if (hs != INVALID_HANDLE_VALUE) CloseHandle(hs);
          if (hd != INVALID_HANDLE_VALUE) CloseHandle(hd);
      } }
#else
    if (strcmp(src, dst) == 0) return 0;
    { struct stat ss, ds;
      if (stat(src, &ss) == 0 && stat(dst, &ds) == 0 &&
          ss.st_dev == ds.st_dev && ss.st_ino == ds.st_ino)
          return 0; }
#endif
    FILE *in = fopen(src, "rb");
    if (!in) {
        if (err_buf && err_cap > 0) {
#ifdef _WIN32
            DWORD err = GetLastError();
            snprintf(err_buf, err_cap, "cannot open source '%s' (errno %d, winerr %lu)",
                     src, errno, (unsigned long)err);
#else
            snprintf(err_buf, err_cap, "cannot open source '%s' (errno %d: %s)",
                     src, errno, strerror(errno));
#endif
        }
        return -1;
    }
    FILE *out = fopen(dst, "wb");
    if (!out) {
        if (err_buf && err_cap > 0) {
#ifdef _WIN32
            DWORD err = GetLastError();
            snprintf(err_buf, err_cap, "cannot open destination '%s' for writing (errno %d, winerr %lu)",
                     dst, errno, (unsigned long)err);
#else
            snprintf(err_buf, err_cap, "cannot open destination '%s' for writing (errno %d: %s)",
                     dst, errno, strerror(errno));
#endif
        }
        fclose(in);
        return -1;
    }
    char buf[65536]; size_t n; int rc = 0;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) {
            rc = -1;
            if (err_buf && err_cap > 0) {
#ifdef _WIN32
                DWORD err = GetLastError();
                snprintf(err_buf, err_cap, "failed writing to destination '%s' (errno %d, winerr %lu)",
                         dst, errno, (unsigned long)err);
#else
                snprintf(err_buf, err_cap, "failed writing to destination '%s' (errno %d: %s)",
                         dst, errno, strerror(errno));
#endif
            }
            break;
        }
    }
    fclose(in);
    if (fclose(out) != 0) {
        rc = -1;
        if (err_buf && err_cap > 0 && err_buf[0] == '\0') {
#ifdef _WIN32
            DWORD err = GetLastError();
            snprintf(err_buf, err_cap, "failed flushing/closing destination '%s' (errno %d, winerr %lu)",
                     dst, errno, (unsigned long)err);
#else
            snprintf(err_buf, err_cap, "failed flushing/closing destination '%s' (errno %d: %s)",
                     dst, errno, strerror(errno));
#endif
        }
    }
    return rc;
}

static bool zan_is_safe_bundle_name(const char *name) {
    if (!name || !name[0]) return false;
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) return false;
    for (const char *p = name; *p; p++) {
        char c = *p;
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') || c == '.' || c == '_' ||
                  c == '-' || c == '+';
        if (!ok) return false;
    }
    return true;
}

static int zan_read_static_libs(const char *path, char out[][128], int max,
                                zan_os_t target_os) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    int count = 0;
    char line[128];
    while (count < max && fgets(line, sizeof(line), f)) {
        size_t n = strlen(line);
        while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r' ||
                         line[n - 1] == ' ' || line[n - 1] == '\t'))
            line[--n] = '\0';
        char *comment = strchr(line, '#');
        if (comment) {
            *comment = '\0';
            n = strlen(line);
            while (n > 0 && (line[n - 1] == ' ' || line[n - 1] == '\t'))
                line[--n] = '\0';
        }
        char *first = line;
        while (*first == ' ' || *first == '\t') first++;
        if (first != line) {
            memmove(line, first, strlen(first) + 1);
            n = strlen(line);
        }
        if (n == 0) continue;
        const char *framework_tag = "@framework/";
        size_t framework_tag_len = strlen(framework_tag);
        if (strncmp(line, framework_tag, framework_tag_len) == 0) {
            if (target_os != ZAN_OS_MACOS) continue;
            const char *framework = line + framework_tag_len;
            if (framework[0] == '-' || !zan_is_safe_bundle_name(framework)) {
                fprintf(stderr, "warning: ignoring unsafe framework entry '%s' in %s "
                        "(expected @framework/<bare framework name>)\n", line, path);
                continue;
            }
            if (max - count < 2) {
                fclose(f);
                link_cap_exceeded("static driver link arguments", max);
            }
            snprintf(out[count++], sizeof(out[0]), "%s", "-framework");
            snprintf(out[count++], sizeof(out[0]), "%s", framework);
            continue;
        }
        char *name = line;
        if (n >= 3 && line[0] == '-' && line[1] == 'l') {
            name = line + 2;
        }
        if (!zan_is_safe_bundle_name(name)) {
            fprintf(stderr, "warning: ignoring unsafe entry '%s' in %s "
                            "(expected a bare library basename)\n", line, path);
            continue;
        }
        snprintf(out[count], sizeof(out[0]), "-l%s", name);
        count++;
    }
    fclose(f);
    return count;
}

static const char *zan_driver_subdir(const zan_target_t *t) {
    if (t->os == ZAN_OS_LINUX)
        return (t->arch == ZAN_ARCH_AARCH64) ? "linux-arm64"
             : (t->arch == ZAN_ARCH_RISCV64) ? "linux-riscv64" : "linux-x64";
    if (t->os == ZAN_OS_MACOS)
        return (t->arch == ZAN_ARCH_AARCH64) ? "macos-arm64" : "macos-x64";
    if (t->os == ZAN_OS_IOS)
        return "ios-arm64";
    if (t->os == ZAN_OS_ANDROID)
        return (t->arch == ZAN_ARCH_AARCH64) ? "android-arm64" : "android-x64";
    if (t->os == ZAN_OS_OHOS)
        return (t->arch == ZAN_ARCH_AARCH64) ? "ohos-arm64" : "ohos-x64";
    return (t->arch == ZAN_ARCH_AARCH64) ? "win-arm64" : "win-x64";
}

typedef struct zan_driver_prefix_snapshot_entry {
    const char *prefix;
    bool defined;
    struct zan_driver_prefix_snapshot_entry *next;
} zan_driver_prefix_snapshot_entry_t;
typedef struct {
    zan_driver_prefix_snapshot_entry_t *head;
    bool captured;
} zan_driver_prefix_snapshot_t;

static bool zan_driver_prefix_live(zan_irgen_t *g,
                                   const zan_driver_prefix_snapshot_t *snapshot,
                                   const char *prefix) {
    if (snapshot) {
        for (const zan_driver_prefix_snapshot_entry_t *p = snapshot->head; p;
             p = p->next) {
            if (p->prefix && strcmp(p->prefix, prefix) == 0) return p->defined;
        }
    }
    return (!snapshot || !snapshot->captured) && zan_irgen_defines_prefix(g, prefix);
}

static void zan_driver_prefix_record(zan_irgen_t *g, zan_arena_t *arena,
                                      zan_driver_prefix_snapshot_t *snapshot,
                                      const char *prefix) {
    if (!prefix[0]) return;
    for (zan_driver_prefix_snapshot_entry_t *p = snapshot->head; p; p = p->next)
        if (strcmp(p->prefix, prefix) == 0) return;
    zan_driver_prefix_snapshot_entry_t *p = zan_arena_alloc(arena, sizeof(*p));
    p->prefix = zan_arena_strdup(arena, prefix, strlen(prefix));
    p->defined = zan_irgen_defines_prefix(g, prefix);
    p->next = snapshot->head;
    snapshot->head = p;
}

static char *zan_driver_bundle_entry(char *line, const char **prefix) {
    while (*line == ' ' || *line == '\t') line++;
    size_t len = strlen(line);
    while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r' ||
                       line[len - 1] == ' ' || line[len - 1] == '\t'))
        line[--len] = '\0';
    *prefix = NULL;
    if (!line[0] || line[0] == '#') return NULL;
    char *cond = strstr(line, " if ");
    if (cond) {
        *cond = '\0';
        const char *pfx = cond + 4;
        while (*pfx == ' ' || *pfx == '\t') pfx++;
        *prefix = pfx;
        len = strlen(line);
        while (len > 0 && (line[len - 1] == ' ' || line[len - 1] == '\t'))
            line[--len] = '\0';
    }
    return line[0] ? line : NULL;
}

static void zan_driver_capture_conditions(zan_irgen_t *g, zan_arena_t *arena,
                                          const zan_driver_registry_t *reg,
                                          const zan_target_t *target,
                                          const char *override_dir,
                                          zan_driver_prefix_snapshot_t *snapshot) {
    if (snapshot->captured) return;
    static const char *resource_prefixes[] = {
        "Gui_", "IconSvgData_", "Pinyin_", "Skin_", "Chart_"
    };
    for (size_t p = 0; p < sizeof(resource_prefixes) / sizeof(resource_prefixes[0]); p++)
        zan_driver_prefix_record(g, arena, snapshot, resource_prefixes[p]);
    for (int d = 0; d < reg->count; d++) {
        const zan_driver_entry_t *driver = &reg->entries[d];
        zan_driver_prefix_record(g, arena, snapshot, driver->sym);
        for (int pass = 0; pass < (override_dir ? 2 : 1); pass++) {
            char dir[1200], manifest[1400];
            if (pass == 1)
                snprintf(dir, sizeof(dir), "%s", override_dir);
            else
                snprintf(dir, sizeof(dir), "%s/%s/drivers/%s", driver->root,
                         driver->module, zan_driver_subdir(target));
            snprintf(manifest, sizeof(manifest), "%s/%s.bundle", dir, driver->lib);
            FILE *f = fopen(manifest, "rb");
            if (!f) continue;
            char line[128];
            while (fgets(line, sizeof(line), f)) {
                const char *prefix;
                if (zan_driver_bundle_entry(line, &prefix) && prefix)
                    zan_driver_prefix_record(g, arena, snapshot, prefix);
            }
            fclose(f);
        }
    }
    snapshot->captured = true;
}

typedef struct {
    const zan_driver_registry_t *reg;
    zan_irgen_t *irgen;
    const zan_driver_prefix_snapshot_t *prefixes;
    const char *subdir;
    const char *outdir;
    bool quiet;
    bool visiting[ZAN_MAX_DRIVERS];
} zan_driver_bundle_context_t;

static int zan_bundle_dependency(zan_driver_bundle_context_t *ctx,
                                 const char *dep, const char *from_manifest,
                                 int *copy_failed_count) {
    int idx = zan_driver_find(ctx->reg, dep, (int)strlen(dep));
    if (idx < 0 || !ctx->reg->entries[idx].root[0]) {
        fprintf(stderr, "warning: @driver/%s listed in %s has no registered owner\n",
                dep, from_manifest);
        return 0;
    }
    if (ctx->visiting[idx]) {
        fprintf(stderr, "warning: ignoring cyclic @driver/%s in %s\n",
                dep, from_manifest);
        return 0;
    }
    const zan_driver_entry_t *owner = &ctx->reg->entries[idx];
    char dir[1200], manifest[1400];
    snprintf(dir, sizeof(dir), "%s/%s/drivers/%s", owner->root,
             owner->module, ctx->subdir);
    snprintf(manifest, sizeof(manifest), "%s/%s.bundle", dir, dep);
    FILE *f = fopen(manifest, "rb");
    if (!f) {
        fprintf(stderr, "warning: @driver/%s listed in %s has no bundle under "
                "its owning module's drivers/%s\n", dep, from_manifest, ctx->subdir);
        return 0;
    }
    ctx->visiting[idx] = true;
    int copied = 0, entries = 0;
    char line[128];
    while (fgets(line, sizeof(line), f)) {
        const char *prefix;
        char *entry = zan_driver_bundle_entry(line, &prefix);
        if (!entry || (prefix &&
            !zan_driver_prefix_live(ctx->irgen, ctx->prefixes, prefix))) continue;
        if (++entries > 64) {
            fclose(f);
            link_cap_exceeded("files in a driver bundle manifest", 64);
        }
        if (strncmp(entry, "@driver/", 8) == 0) {
            const char *next = entry + 8;
            if (strlen(next) < 64 && zan_is_safe_bundle_name(next))
                copied += zan_bundle_dependency(ctx, next, manifest, copy_failed_count);
            else
                fprintf(stderr, "warning: ignoring unsafe @driver entry '%s' in %s\n",
                        entry, manifest);
            continue;
        }
        if (!zan_is_safe_bundle_name(entry)) {
            fprintf(stderr, "warning: ignoring unsafe entry '%s' in %s "
                    "(must be a bare filename)\n", entry, manifest);
            continue;
        }
        char src[1400], dst[1300], errbuf[256];
        snprintf(src, sizeof(src), "%s/%s", dir, entry);
        snprintf(dst, sizeof(dst), "%s/%s", ctx->outdir, entry);
        if (zan_copy_file_ex(src, dst, errbuf, sizeof(errbuf)) == 0) {
            if (!ctx->quiet)
                printf("  bundled driver '%s' ? %s (via @driver/%s)\n", dep, entry, dep);
            copied++;
        } else {
            fprintf(stderr, "warning: failed to copy bundled driver file "
                    "'%s' -> '%s': %s\n", src, dst, errbuf);
            (*copy_failed_count)++;
        }
    }
    fclose(f);
    ctx->visiting[idx] = false;
    return copied;
}

static void zan_exe_dir(char *out, size_t outsz) {
    out[0] = '\0';
#ifdef _WIN32
    GetModuleFileNameA(NULL, out, (DWORD)outsz);
    { char *s = strrchr(out, '\\'); if (s) *s = '\0'; }
#elif defined(__APPLE__)
    { uint32_t sz = (uint32_t)outsz;
      if (_NSGetExecutablePath(out, &sz) != 0) out[0] = '\0';
      char *s = strrchr(out, '/'); if (s) *s = '\0'; }
#else
    { ssize_t n = readlink("/proc/self/exe", out, outsz - 1);
      if (n > 0) { out[n] = '\0'; char *s = strrchr(out, '/'); if (s) *s = '\0'; } }
#endif
}

static bool zan_file_exists(const char *path) {
#ifdef _WIN32
    return zan_utf8_get_file_attributes(path) != INVALID_FILE_ATTRIBUTES;
#else
    return access(path, F_OK) == 0;
#endif
}

/* ---------- 发布态设计稿自动内嵌（门控与工程根解析） ---------- */

/* 输入源里出现 Gui 窗口类记号（" : Form"，B-ID149 窗口即对象形态）
 * 才启用自动内嵌：服务端/web 程序的 html 是运行期盘上资产，绝不能
 * 烘死进 exe。 */
static bool zan_inputs_use_design_doc(const char *const *input_files,
                                      int input_count) {
    for (int fi = 0; fi < input_count; fi++) {
        if (zan_is_zcomp_path(input_files[fi])) continue;
        size_t len = 0;
        char *src = read_file(input_files[fi], &len);
        if (!src) continue;
        bool hit = strstr(src, " : Form") != NULL ||
                   strstr(src, " : ChildWindow") != NULL;
        free(src);
        if (hit) return true;
    }
    return false;
}

/* 输入文件所属工程根：向上找最近的 zan.proj 祖先目录；散文件编译
 * 退回输入所在目录。不向上吞 packages/ 祖先（package_project_root
 * 的规则会把仓库内构建的整个仓库指成设计根）。 */
static void zan_design_root_for(const char *input, char *out, size_t sz) {
    /* 先解析成绝对路径：相对输入（如 src\App.zan）剥掉文件名后，
     * "src" 这样的单段目录无法在字符串上继续向上行。 */
    char abs[1024];
#ifdef _WIN32
    if (!zan_utf8_full_path(input, abs, sizeof(abs)))
        snprintf(abs, sizeof(abs), "%s", input);
#else
    if (realpath(input, abs) == NULL)
        snprintf(abs, sizeof(abs), "%s", input);
#endif
    char dir[1024];
    snprintf(dir, sizeof(dir), "%s", abs);
    char *fwd = strrchr(dir, '/');
    char *back = strrchr(dir, '\\');
    char *sep = (fwd && fwd > back) ? fwd : back;
    if (sep) *sep = '\0';
    else snprintf(dir, sizeof(dir), ".");
    for (;;) {
        char probe[1200];
        snprintf(probe, sizeof(probe), "%s/zan.proj", dir);
        if (zan_file_exists(probe)) break;
        fwd = strrchr(dir, '/');
        back = strrchr(dir, '\\');
        char *last = (fwd && fwd > back) ? fwd : back;
        if (!last) break;
        if (last == dir) {
            if (dir[1] == '\0') break;      /* 已到文件系统根 "/" */
            dir[1] = '\0';                  /* 收敛到 "/" 再试一次 */
            continue;
        }
        *last = '\0';
    }
    snprintf(out, sz, "%s", dir);
}

static void zan_extract_json_str(const char *line, const char *key, char *out, size_t outsz) {
    out[0] = '\0';
    char needle[64];
    snprintf(needle, sizeof(needle), "\"%s\":\"", key);
    const char *p = strstr(line, needle);
    if (!p) return;
    p += strlen(needle);
    size_t i = 0;
    while (*p && *p != '"' && i + 1 < outsz) {
        if (*p == '\\' && *(p + 1)) p++;
        out[i++] = *p++;
    }
    out[i] = '\0';
}

static int zan_extract_json_int(const char *line, const char *key) {
    char needle[64];
    snprintf(needle, sizeof(needle), "\"%s\":", key);
    const char *p = strstr(line, needle);
    if (!p) return 0;
    p += strlen(needle);
    while (*p == ' ' || *p == '\t') p++;
    return atoi(p);
}

static const char *zan_strcasestr(const char *haystack, const char *needle) {
    if (!haystack || !needle) return NULL;
    if (!*needle) return haystack;
    for (; *haystack; haystack++) {
        if (tolower((unsigned char)*haystack) == tolower((unsigned char)*needle)) {
            const char *h = haystack;
            const char *n = needle;
            while (*h && *n && tolower((unsigned char)*h) == tolower((unsigned char)*n)) {
                h++;
                n++;
            }
            if (!*n) return haystack;
        }
    }
    return NULL;
}

int zan_query_api(const char *symbol_query) {
    if (!symbol_query || !*symbol_query) {
        fprintf(stderr, "error: --api requires a symbol name or fragment (e.g. 'Button' or 'File.ReadAllText')\n");
        return 1;
    }
    char exe_dir[1024];
    zan_exe_dir(exe_dir, sizeof(exe_dir));

    char cand1[1200], cand2[1200], cand3[256], cand4[256];
    snprintf(cand1, sizeof(cand1), "%s/knowledge/symbols.json", exe_dir);
    snprintf(cand2, sizeof(cand2), "%s/../knowledge/symbols.json", exe_dir);
    snprintf(cand3, sizeof(cand3), ".zanmap/symbols.json");
    snprintf(cand4, sizeof(cand4), "knowledge/symbols.json");

    const char *paths[] = { cand1, cand2, cand3, cand4 };
    const char *found_path = NULL;
    for (size_t i = 0; i < sizeof(paths) / sizeof(paths[0]); i++) {
        if (zan_file_exists(paths[i])) {
            found_path = paths[i];
            break;
        }
    }

    if (!found_path) {
        fprintf(stderr, "error: API symbol index not found (checked knowledge/symbols.json, .zanmap/symbols.json).\n");
        fprintf(stderr, "Hint: Run scripts/gen_knowledge.ps1 to generate the offline API index.\n");
        return 1;
    }

    FILE *f = fopen(found_path, "r");
    if (!f) {
        fprintf(stderr, "error: could not open %s\n", found_path);
        return 1;
    }

    char line[4096];
    int count = 0;
    const int max_results = 50;
    printf("=== API search for '%s' (from %s) ===\n", symbol_query, found_path);

    while (fgets(line, sizeof(line), f)) {
        if (!zan_strcasestr(line, symbol_query)) continue;

        char name[256], kind[64], sig[512], file[512];
        zan_extract_json_str(line, "name", name, sizeof(name));
        zan_extract_json_str(line, "kind", kind, sizeof(kind));
        zan_extract_json_str(line, "sig", sig, sizeof(sig));
        zan_extract_json_str(line, "file", file, sizeof(file));
        int line_no = zan_extract_json_int(line, "line");

        if (!name[0]) continue;

        if (sig[0]) {
            printf("  [%s] %s :: %s  (%s:%d)\n", kind[0] ? kind : "symbol", name, sig, file, line_no);
        } else {
            printf("  [%s] %s  (%s:%d)\n", kind[0] ? kind : "type", name, file, line_no);
        }
        count++;
        if (count >= max_results) {
            printf("  ... (capped at %d results)\n", max_results);
            break;
        }
    }
    fclose(f);
    printf("Total found: %d\n", count);
    return 0;
}

static bool zan_read_first_bundle_name(const char *path,
                                       char *out, size_t outsz) {
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    char line[128];
    while (fgets(line, sizeof(line), f)) {
        size_t n = strlen(line);
        while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r' ||
                         line[n - 1] == ' ' || line[n - 1] == '\t'))
            line[--n] = '\0';
        char *first = line;
        while (*first == ' ' || *first == '\t') first++;
        if (first != line) {
            memmove(line, first, strlen(first) + 1);
            n = strlen(line);
        }
        if (n == 0 || !zan_is_safe_bundle_name(line)) continue;
        if (n >= outsz) continue;
        snprintf(out, outsz, "%s", line);
        fclose(f);
        return true;
    }
    fclose(f);
    return false;
}

static bool zan_find_macos_driver_dylib(const char *dir,
                                        const char *name, int len,
                                        char *out, size_t outsz) {
    char direct[1200];
    snprintf(direct, sizeof(direct), "%s/lib%.*s.dylib", dir, len, name);
    if (zan_file_exists(direct)) {
        snprintf(out, outsz, "%s", direct);
        return true;
    }
    char manifest[1200], bundled[128];
    snprintf(manifest, sizeof(manifest), "%s/%.*s.bundle", dir, len, name);
    if (!zan_read_first_bundle_name(manifest, bundled, sizeof(bundled)))
        return false;
    snprintf(out, outsz, "%s/%s", dir, bundled);
    return zan_file_exists(out);
}

static const char *zan_path_basename(const char *p) {
    const char *b = p;
    for (const char *q = p; *q; q++)
        if (*q == '/' || *q == '\\') b = q + 1;
    return b;
}

static int zan_path_is_under(const char *path, const char *root) {
    if (!path || !root || !root[0]) return 0;
    size_t rl = strlen(root);
    while (rl > 0 && (root[rl - 1] == '/' || root[rl - 1] == '\\')) rl--;
#ifdef _WIN32
    if (_strnicmp(path, root, (size_t)rl) != 0) return 0;
#else
    if (strncmp(path, root, rl) != 0) return 0;
#endif
    char nc = path[rl];
    if (nc == '\0') return 1;
    return nc == '/' || nc == '\\';
}

/* 包源根（packages/ 仓、.zan-packages、exe 旁与全局包仓）一次性解析并缓存：
 * 编译集里的包内源文件要标成 from_package，隐式控制器发现据此跳过库自带
 * 控制器。根由包仓路径拼出，被比对的文件路径来自目录遍历，两边的分隔符
 * 写法可能一个 '/' 一个 '\\'，故这里按分隔符等价折叠后再比前缀。 */
static zan_package_source_root_t *g_pkg_roots;
static int g_pkg_root_count = -1;

static int zan_path_is_package_source(const char *path) {
    if (!path || !path[0]) return 0;
    if (g_pkg_root_count < 0)
        g_pkg_roots = zan_collect_package_source_roots(&g_pkg_root_count);
    for (int i = 0; i < g_pkg_root_count; i++) {
        const char *root = g_pkg_roots[i];
        size_t rl = strlen(root);
        while (rl > 0 && (root[rl - 1] == '/' || root[rl - 1] == '\\')) rl--;
        size_t k = 0;
        for (; k < rl; k++) {
            char a = path[k], b = root[k];
            if (a == '\0') break;
            if (a == '\\') a = '/';
            if (b == '\\') b = '/';
#ifdef _WIN32
            if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
            if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
#endif
            if (a != b) break;
        }
        if (k < rl) continue;
        char nc = path[k];
        if (nc == '\0' || nc == '/' || nc == '\\') return 1;
    }
    return 0;
}

static int wasm_obj_refs_any(const char *obj, const char *const *prefixes) {
    char nmout[1300];
    snprintf(nmout, sizeof(nmout), "%s.nm", obj);
    char nmcmd[1600];
    snprintf(nmcmd, sizeof(nmcmd), "llvm-nm -u \"%s\" > \"%s\" 2>&1", obj, nmout);
    if (system(nmcmd) != 0) { remove(nmout); return 0; }
    int found = 0;
    FILE *f = fopen(nmout, "rb");
    if (f) {
        char line[512];
        while (fgets(line, sizeof(line), f)) {
            for (int pi = 0; prefixes[pi]; pi++) {
                if (strstr(line, prefixes[pi])) { found = 1; break; }
            }
            if (found) break;
        }
        fclose(f);
    }
    remove(nmout);
    return found;
}

static int wasm_obj_vec_refs_any(const generated_object_vec_t *objects,
                                 const char *const *prefixes) {
    for (int i = 0; i < objects->count; i++) {
        if (wasm_obj_refs_any(objects->paths[i], prefixes)) return 1;
    }
    return 0;
}

static void zan_runtime_fallback(zan_irgen_t *g, LLVMValueRef value,
                                  bool use_comdat) {
    if (!value || LLVMIsDeclaration(value)) return;
    LLVMSetLinkage(value, LLVMLinkOnceAnyLinkage);
    if (use_comdat) {
        LLVMComdatRef group = LLVMGetOrInsertComdat(g->mod,
                                                     LLVMGetValueName(value));
        LLVMSetComdatSelectionKind(group, LLVMAnyComdatSelectionKind);
        LLVMSetComdat(value, group);
    }
}

static void zan_prepare_static_runtime(zan_irgen_t *g,
                                        const zan_target_t *target) {
    if (g->emit_lib && g->emit_shared) return;
    bool use_comdat = target->os != ZAN_OS_MACOS && target->os != ZAN_OS_IOS;

    LLVMValueRef weak_state[] = {
        g->weak_buckets, g->weak_lock, g->weak_count
    };
    for (size_t i = 0; i < sizeof(weak_state) / sizeof(weak_state[0]); i++)
        zan_runtime_fallback(g, weak_state[i], use_comdat);

    static const char *const co_state[] = {
        "__zan_co_head", "__zan_co_tail", "__zan_co_nodes",
        "__zan_co_slice_start", "__zan_co_poll_tick", "__zan_co_quantum_us"
    };
    if (!g->external_async_executor) {
        for (size_t i = 0; i < sizeof(co_state) / sizeof(co_state[0]); i++)
            zan_runtime_fallback(g, LLVMGetNamedGlobal(g->mod, co_state[i]),
                                 use_comdat);
    }

    LLVMValueRef io_fallbacks[] = {
        g->rt_io_pump_timeout, g->rt_io_has_pending,
        LLVMGetNamedFunction(g->mod, "zan_io_pump")
    };
    for (size_t i = 0; i < sizeof(io_fallbacks) / sizeof(io_fallbacks[0]); i++)
        zan_runtime_fallback(g, io_fallbacks[i], use_comdat);

    if (!g->emit_lib) return;
    LLVMValueRef co_fallbacks[] = {
        g->rt_co_ready, g->rt_co_poll, g->rt_co_frame_free,
        g->rt_co_sched_init, g->rt_co_sched_run, g->rt_co_sched_run_until
    };
    for (size_t i = 0; i < sizeof(co_fallbacks) / sizeof(co_fallbacks[0]); i++)
        zan_runtime_fallback(g, co_fallbacks[i], use_comdat);

    LLVMValueRef helpers[] = {
        g->rt_println, g->rt_print_int, g->rt_print_uint, g->rt_print_double,
        g->rt_print_float,
        g->rt_retain, g->rt_release, g->rt_release_dyn, g->rt_alloc,
        g->rt_str_retain, g->rt_str_release, g->rt_str_alloc,
        g->rt_arr_retain, g->rt_arr_release,
        g->rt_weak_store, g->rt_weak_nil_all, g->rt_weak_load_retain,
        g->rt_weak_destroy_begin
    };
    for (size_t i = 0; i < sizeof(helpers) / sizeof(helpers[0]); i++) {
        if (!helpers[i] || LLVMIsDeclaration(helpers[i])) continue;
        LLVMSetLinkage(helpers[i], LLVMInternalLinkage);
    }
}

