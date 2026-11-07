static unsigned pkg_namespace_hash(const char *name, size_t len) {
    unsigned h = 2166136261u;
    for (size_t i = 0; i < len; i++)
        h = (h ^ (unsigned char)name[i]) * 16777619u;
    return h & (PKG_NAMESPACE_BUCKETS - 1);
}

static pkg_index_namespace_t *pkg_index_find_namespace(
    const zan_pkg_source_index_t *index, const char *name, size_t len) {
    unsigned bucket = pkg_namespace_hash(name, len);
    for (pkg_index_namespace_t *ns = index->namespaces[bucket]; ns; ns = ns->next)
        if (strlen(ns->name) == len && memcmp(ns->name, name, len) == 0)
            return ns;
    return NULL;
}

static void pkg_index_add_namespace(zan_pkg_source_index_t *index,
                                    const char *name, size_t len,
                                    pkg_index_source_t *source, int descendant) {
    if (!len) return;
    pkg_index_namespace_t *ns = pkg_index_find_namespace(index, name, len);
    if (!ns) {
        unsigned bucket = pkg_namespace_hash(name, len);
        ns = (pkg_index_namespace_t *)calloc(1, sizeof(*ns));
        memcpy(ns->name, name, len);
        ns->name[len] = 0;
        ns->next = index->namespaces[bucket];
        index->namespaces[bucket] = ns;
    }
    if (ns->count == ns->capacity) {
        int cap = ns->capacity ? ns->capacity * 2 : 8;
        ns->sources = (pkg_namespace_source_t *)realloc(
            ns->sources, (size_t)cap * sizeof(*ns->sources));
        ns->capacity = cap;
    }
    ns->sources[ns->count++] = (pkg_namespace_source_t){source, descendant};
}

static void pkg_rel_dir_ns(const char *dir, const char *src_root,
                           char *out, size_t cap) {
    size_t rl = strlen(src_root);
    const char *rel = strlen(dir) >= rl ? dir + rl : dir;
    if (*rel == '/' || *rel == '\\') rel++;
    size_t w = 0;
    for (; *rel && w + 1 < cap; rel++) {
        char c = *rel;
        if (c == '/' || c == '\\') c = '.';
        out[w++] = c;
    }
    out[w] = '\0';
}

static void pkg_index_source_tree_collect(pkg_probe_list_t *list,
                                           pkg_index_package_t *package,
                                           const char *dir,
                                           zan_pkg_namespace_probe_t probe,
                                           int depth, const char *src_root,
                                           int project_owned) {
    if (depth > 64) return;
    pkg_dir_entries_t *ent = pkg_dir_entries_sorted(dir);
    if (!ent) return;

    /* 目录统一命名空间快路径：排序后的首个 .zan 若声明命名空间恰等于
     * 目录推导命名空间，则整目录免逐文件 probe（仓库约定「目录即命名
     * 空间」，stdlib glob 同款假设）。首文件无声明或与目录推导不符 →
     * 整目录进并行 probe 队列，语义与旧实现一致。 */
    char dir_ns[256];
    pkg_rel_dir_ns(dir, src_root, dir_ns, sizeof(dir_ns));
    int uniform = 0;
    g_pkg_dirs_total++;
    if (dir_ns[0]) {
        for (int i = 0; i < ent->count; i++) {
            if (ent->flags[i] & 3) continue;  /* 目录/重解析点不参与 */
            if (!pkg_safe_component(ent->names[i])) continue;
            size_t nl = strlen(ent->names[i]);
            if (nl < 5 || strcmp(ent->names[i] + nl - 4, ".zan") != 0) continue;
            char first_path[1024];
            if (snprintf(first_path, sizeof(first_path),
                         "%s" PATH_SEP "%s", dir, ent->names[i])
                < (int)sizeof(first_path)) {
                char declared0[256] = {0};
                g_pkg_probe_calls++;
                uniform = probe(first_path, declared0, sizeof(declared0)) &&
                          declared0[0] &&
                          strcmp(declared0, dir_ns) == 0;
            }
            break;  /* 只看排序后的首个 .zan */
        }
        if (uniform) g_pkg_dirs_uniform++;
    }

    for (int i = 0; i < ent->count; i++) {
        const char *name = ent->names[i];
        if (!pkg_safe_component(name)) continue;
        char path[1024];
        if (snprintf(path, sizeof(path), "%s" PATH_SEP "%s", dir, name)
            >= (int)sizeof(path)) continue;
        if (ent->flags[i] & 2) continue;  /* reparse point 不入树 */
        if (ent->flags[i] & 1) {
            pkg_index_source_tree_collect(list, package, path, probe, depth + 1,
                                           src_root, project_owned);
            continue;
        }
        size_t len = strlen(name);
        if (len < 5 || strcmp(name + len - 4, ".zan") != 0) continue;
        pkg_probe_item_t it;
        memset(&it, 0, sizeof(it));
        it.path = pkg_index_copy(path);
        it.src_root = pkg_index_copy(src_root);
        it.package = package;
        it.project_owned = project_owned;
        it.uniform = uniform;
        snprintf(it.dir_ns, sizeof(it.dir_ns), "%s", dir_ns);
        if (uniform)
            snprintf(it.declared, sizeof(it.declared), "%s", dir_ns);
        it.has_ns = uniform;
        pkg_probe_list_push(list, &it);
    }
    pkg_dir_entries_free(ent);
}

typedef struct {
    char **names;
    int count;
    int capacity;
} pkg_seen_names_t;

static bool pkg_mark_seen(pkg_seen_names_t *seen, const char *name) {
    for (int i = 0; i < seen->count; i++)
        if (strcmp(seen->names[i], name) == 0) return false;
    if (seen->count == seen->capacity) {
        int cap = seen->capacity ? seen->capacity * 2 : 16;
        char **grown = (char **)realloc(seen->names, (size_t)cap * sizeof(*seen->names));
        if (!grown) {
            zan_host_oom();
        }
        seen->names = grown;
        seen->capacity = cap;
    }
    seen->names[seen->count++] = strdup(name);
    return true;
}

static void pkg_index_store(zan_pkg_source_index_t *index, const char *store,
                             zan_pkg_namespace_probe_t probe,
                             pkg_seen_names_t *seen, int project_owned,
                             pkg_probe_list_t *list) {
    if (!pkg_is_dir(store)) return;
    int n = 0;
    char **names = pkg_dir_names_sorted(store, &n);
    if (!names) return;
    for (int i = 0; i < n; i++) {
        const char *name = names[i];
        if (!pkg_safe_component(name)) continue;
        char root[1024], source[1024];
        if (snprintf(root, sizeof(root), "%s" PATH_SEP "%s", store, name)
            >= (int)sizeof(root)) continue;
#ifdef _WIN32
        DWORD attr = GetFileAttributesA(root);
        if (attr == INVALID_FILE_ATTRIBUTES ||
            (attr & FILE_ATTRIBUTE_REPARSE_POINT) ||
            !(attr & FILE_ATTRIBUTE_DIRECTORY)) continue;
#else
        struct stat st;
        if (lstat(root, &st) != 0 || !S_ISDIR(st.st_mode)) continue;
#endif
        if (snprintf(source, sizeof(source), "%s" PATH_SEP "src", root)
            >= (int)sizeof(source)) continue;
        if (!pkg_is_dir(source)) {
            if (snprintf(source, sizeof(source), "%s" PATH_SEP "stdlib", root)
                >= (int)sizeof(source)) continue;
            if (!pkg_is_dir(source)) snprintf(source, sizeof(source), "%s", root);
        }
        if (!pkg_mark_seen(seen, name)) continue;
        pkg_index_package_t *package = (pkg_index_package_t *)malloc(sizeof(*package));
        package->store = pkg_index_copy(store);
        package->name = pkg_index_copy(name);
        package->next = index->packages;
        index->packages = package;
        pkg_index_source_tree_collect(list, package, source, probe, 0, source,
                                       project_owned);
    }
    pkg_dir_names_free(names, n);
}

/* ---- 并行 probe：纯只读工作（fopen + 头 1KB 扫描），线程间无共享写，
 * 结果各写各的 item 槽位。静态分块，无锁。 ---- */
typedef struct {
    pkg_probe_item_t *items;
    int *pending;
    int from;
    int to;
    zan_pkg_namespace_probe_t probe;
    long probe_calls;
} pkg_probe_task_t;

static void pkg_probe_run_chunk(pkg_probe_task_t *t) {
    for (int i = t->from; i < t->to; i++) {
        pkg_probe_item_t *it = &t->items[t->pending[i]];
        char declared[256] = {0};
        t->probe_calls++;
        it->has_ns = t->probe(it->path, declared, sizeof(declared)) && declared[0];
        if (it->has_ns)
            snprintf(it->declared, sizeof(it->declared), "%s", declared);
    }
}

#ifdef _WIN32
static DWORD WINAPI pkg_probe_worker_win(LPVOID arg) {
    pkg_probe_run_chunk((pkg_probe_task_t *)arg);
    return 0;
}
#else
static void *pkg_probe_worker_posix(void *arg) {
    pkg_probe_run_chunk((pkg_probe_task_t *)arg);
    return NULL;
}
#endif

/* 并行 probe 全部待定 item，然后按走树原次序注册进索引。 */
static void pkg_index_build_from_items(zan_pkg_source_index_t *index,
                                        pkg_probe_list_t *list,
                                        zan_pkg_namespace_probe_t probe) {
    double __b0 = zan_pkg_now_ms();
    /* 待 probe 的 item 下标（保持走树次序中的相对顺序） */
    int pending_n = 0;
    for (int i = 0; i < list->count; i++)
        if (!list->items[i].uniform) pending_n++;
    int *pending = NULL;
    if (pending_n > 0) {
        pending = (int *)malloc((size_t)pending_n * sizeof(int));
        if (!pending) {
            zan_host_oom();
        }
        int k = 0;
        for (int i = 0; i < list->count; i++)
            if (!list->items[i].uniform) pending[k++] = i;
    }

    if (pending_n > 0) {
        double __p0 = zan_pkg_now_ms();
        int nthreads = pkg_cpu_count();
        if (nthreads > 16) nthreads = 16;
        if (nthreads > pending_n) nthreads = pending_n;
        if (nthreads < 1) nthreads = 1;
        int per = pending_n / nthreads;
        int rem = pending_n % nthreads;
        pkg_probe_task_t *tasks = (pkg_probe_task_t *)calloc(
            (size_t)nthreads, sizeof(*tasks));
        if (!tasks) {
            zan_host_oom();
        }
        int off = 0;
        for (int t = 0; t < nthreads; t++) {
            tasks[t].items = list->items;
            tasks[t].pending = pending;
            tasks[t].from = off;
            tasks[t].to = off + per + (t < rem ? 1 : 0);
            tasks[t].probe = probe;
            off = tasks[t].to;
        }
#ifdef _WIN32
        HANDLE *threads = (HANDLE *)malloc((size_t)nthreads * sizeof(HANDLE));
        if (!threads) {
            zan_host_oom();
        }
        int live = 0;
        for (int t = 0; t < nthreads; t++) {
            HANDLE h = CreateThread(NULL, 0, pkg_probe_worker_win, &tasks[t],
                                    0, NULL);
            if (h) {
                threads[live++] = h;
            } else {
                /* 句柄耗尽等失败——该块退化为当前线程直接跑 */
                pkg_probe_run_chunk(&tasks[t]);
            }
        }
        if (live > 0)
            WaitForMultipleObjects((DWORD)live, threads, TRUE, INFINITE);
        for (int t = 0; t < live; t++)
            CloseHandle(threads[t]);
        free(threads);
#else
        pthread_t *threads = (pthread_t *)malloc((size_t)nthreads * sizeof(pthread_t));
        if (!threads) {
            zan_host_oom();
        }
        int live = 0;
        for (int t = 0; t < nthreads; t++) {
            if (pthread_create(&threads[live], NULL, pkg_probe_worker_posix,
                               &tasks[t]) == 0) {
                live++;
            } else {
                /* EAGAIN / ENOMEM——该块退化为当前线程直接跑 */
                pkg_probe_run_chunk(&tasks[t]);
            }
        }
        for (int t = 0; t < live; t++)
            pthread_join(threads[t], NULL);
        free(threads);
#endif
        for (int t = 0; t < nthreads; t++)
            g_pkg_probe_calls += tasks[t].probe_calls;
        free(tasks);
        zan_compile_trace("pkg probe: pending=%d threads=%d parallel=%.1fms", pending_n, nthreads, zan_pkg_now_ms() - __p0);
    }
    free(pending);
    zan_compile_trace("pkg build: probe+register total=%.1fms", zan_pkg_now_ms() - __b0);

    /* 按走树原次序注册：与旧逐文件实现注册次序逐项一致 */
    for (int i = 0; i < list->count; i++) {
        pkg_probe_item_t *it = &list->items[i];
        char declared[256];
        int has_namespace;
        if (it->uniform) {
            snprintf(declared, sizeof(declared), "%s", it->dir_ns);
            has_namespace = 1;
        } else if (it->has_ns) {
            snprintf(declared, sizeof(declared), "%s", it->declared);
            has_namespace = 1;
        } else {
            snprintf(declared, sizeof(declared), "%s", it->dir_ns);
            has_namespace = 0;
        }
        if (!declared[0]) continue;
        pkg_index_source_t *source = (pkg_index_source_t *)malloc(sizeof(*source));
        source->path = it->path;
        it->path = NULL;  /* 所有权移交索引 */
        source->package = it->package;
        source->next = index->sources;
        index->sources = source;
        pkg_index_add_namespace(index, declared, strlen(declared), source, 0);
        if (has_namespace && it->project_owned) {
            for (size_t k = 0; declared[k]; k++)
                if (declared[k] == '.')
                    pkg_index_add_namespace(index, declared, k, source, 1);
        }
    }
}

static void pkg_store_canonical(const char *path, char *out, size_t cap) {
#ifdef _WIN32
    if (!GetFullPathNameA(path, (DWORD)cap, out, NULL)) snprintf(out, cap, "%s", path);
    for (char *c = out; *c; c++) {
        if (*c == '/') *c = '\\';
        else if (*c >= 'A' && *c <= 'Z') *c = (char)(*c - 'A' + 'a');
    }
#else
    if (!realpath(path, out)) snprintf(out, cap, "%s", path);
#endif
}

/* 规范化候选 store 根并去重：重复（返回 1）不再走整树枚举。
 * 未重复（返回 0）时 canon[*count] 已写入规范化路径。 */
static int pkg_canon_dup(char canon[][1024], int count, const char *raw) {
    pkg_store_canonical(raw, canon[count], 1024);
    for (int i = 0; i < count; i++)
        if (strcmp(canon[count], canon[i]) == 0) return 1;
    return 0;
}

zan_pkg_source_index_t *zan_pkg_source_index_create(
    const char *project_dir, zan_pkg_namespace_probe_t probe) {
    if (!project_dir || !project_dir[0] || !probe) return NULL;
    zan_pkg_source_index_t *index = (zan_pkg_source_index_t *)calloc(1, sizeof(*index));
    char store[1024];
    /* 候选 store 根常互相重合（exe 目录旁的 ..\packages 就是项目的
     * packages/）：先规范化再去重，重复根不再做整树枚举。 */
    char canon[6][1024];
    int canon_count = 0;
    pkg_seen_names_t seen = {0};
    pkg_probe_list_t list = {0};
    snprintf(store, sizeof(store), "%s" PATH_SEP "packages", project_dir);
    if (!pkg_canon_dup(canon, canon_count, store)) {
        canon_count++;
        pkg_index_store(index, store, probe, &seen, 1, &list);
    }
    snprintf(store, sizeof(store), "%s" PATH_SEP ".zan-packages", project_dir);
    if (!pkg_canon_dup(canon, canon_count, store)) {
        canon_count++;
        pkg_index_store(index, store, probe, &seen, 1, &list);
    }
    char exe_dir[1024] = {0};
#ifdef _WIN32
    if (GetModuleFileNameA(NULL, exe_dir, sizeof(exe_dir))) {
        char *sep = strrchr(exe_dir, '\\'); if (sep) *sep = 0;
        snprintf(store, sizeof(store), "%s\\..\\packages", exe_dir);
        if (!pkg_canon_dup(canon, canon_count, store)) {
            canon_count++;
            pkg_index_store(index, store, probe, &seen, 1, &list);
        }
        snprintf(store, sizeof(store), "%s\\packages", exe_dir);
        if (!pkg_canon_dup(canon, canon_count, store)) {
            canon_count++;
            pkg_index_store(index, store, probe, &seen, 1, &list);
        }
    }
#elif defined(__APPLE__)
    uint32_t size = sizeof(exe_dir);
    if (_NSGetExecutablePath(exe_dir, &size) == 0) {
        char *sep = strrchr(exe_dir, '/'); if (sep) *sep = 0;
        snprintf(store, sizeof(store), "%s/../packages", exe_dir);
        if (!pkg_canon_dup(canon, canon_count, store)) {
            canon_count++;
            pkg_index_store(index, store, probe, &seen, 1, &list);
        }
    }
#else
    ssize_t len = readlink("/proc/self/exe", exe_dir, sizeof(exe_dir) - 1);
    if (len > 0) {
        exe_dir[len] = 0;
        char *sep = strrchr(exe_dir, '/'); if (sep) *sep = 0;
        snprintf(store, sizeof(store), "%s/../packages", exe_dir);
        if (!pkg_canon_dup(canon, canon_count, store)) {
            canon_count++;
            pkg_index_store(index, store, probe, &seen, 1, &list);
        }
    }
#endif
    if (zan_pkg_global_store(store, sizeof(store)))
        pkg_index_store(index, store, probe, &seen, 0, &list);
    /* 阶段二：并行 probe + 按走树原次序注册 */
    pkg_index_build_from_items(index, &list, probe);
    pkg_probe_list_free(&list);
    zan_compile_trace("pkg index: dirs=%d uniform=%d probes=%d",
                      g_pkg_dirs_total, g_pkg_dirs_uniform, g_pkg_probe_calls);
    g_pkg_dirs_total = 0;
    g_pkg_dirs_uniform = 0;
    g_pkg_probe_calls = 0;
    for (int i = 0; i < seen.count; i++) free(seen.names[i]);
    free(seen.names);
    return index;
}

int zan_pkg_source_index_visit(const zan_pkg_source_index_t *index,
                                const char *namespace_path,
                                zan_pkg_source_visitor_t visitor, void *context,
                                int hierarchical) {
    if (!index || !pkg_safe_namespace_path(namespace_path) || !visitor) return 0;
    char target_ns[256];
    size_t n = strlen(namespace_path);
    if (n >= sizeof(target_ns)) return 0;
    for (size_t i = 0; i <= n; i++)
        target_ns[i] = (namespace_path[i] == '/' || namespace_path[i] == '\\') ? '.' : namespace_path[i];
    pkg_index_namespace_t *ns = pkg_index_find_namespace(index, target_ns, n);
    if (!ns) return 0;
    int found = 0;
    pkg_index_package_t *used = NULL;
    for (int i = 0; i < ns->count; i++) {
        const pkg_namespace_source_t *ref = &ns->sources[i];
        if (ref->descendant && !hierarchical) continue;
        pkg_index_package_t *package = ref->source->package;
        if (used && used != package)
            zan_pkg_note_usage(used->store, used->name);
        used = package;
        visitor(ref->source->path, context);
        found++;
    }
    if (used) zan_pkg_note_usage(used->store, used->name);
    return found;
}

void zan_pkg_source_index_destroy(zan_pkg_source_index_t *index) {
    if (!index) return;
    for (int i = 0; i < PKG_NAMESPACE_BUCKETS; i++) {
        pkg_index_namespace_t *ns = index->namespaces[i];
        while (ns) {
            pkg_index_namespace_t *next = ns->next;
            free(ns->sources);
            free(ns);
            ns = next;
        }
    }
    while (index->sources) {
        pkg_index_source_t *source = index->sources;
        index->sources = source->next;
        free(source->path);
        free(source);
    }
    while (index->packages) {
        pkg_index_package_t *package = index->packages;
        index->packages = package->next;
        free(package->store);
        free(package->name);
        free(package);
    }
    free(index);
}

int zan_pkg_visit_namespace(const char *project_dir, const char *namespace_path,
                            zan_pkg_namespace_probe_t probe,
                            zan_pkg_source_visitor_t visitor, void *context,
                            int hierarchical) {
    if (!pkg_safe_namespace_path(namespace_path) || !visitor ||
        strlen(namespace_path) >= 256) return 0;
    zan_pkg_source_index_t *index = zan_pkg_source_index_create(project_dir, probe);
    int found = zan_pkg_source_index_visit(index, namespace_path, visitor, context,
                                           hierarchical);
    zan_pkg_source_index_destroy(index);
    return found;
}

static int pkg_add_package_root(const char *store, const char *name,
                                char (*out_roots)[1024], int count,
                                int max_roots) {
    char root[1024], cand[1024];
    snprintf(root, sizeof(root), "%s/%s", store, name);
    snprintf(cand, sizeof(cand), "%s/src", root);
    if (!pkg_is_dir(cand)) {
        snprintf(cand, sizeof(cand), "%s/stdlib", root);
    }
    if (!pkg_is_dir(cand)) {
        snprintf(cand, sizeof(cand), "%s", root);
    }
    if (!pkg_is_dir(cand) || count >= max_roots ||
        pkg_dir_in_list(out_roots, count, cand))
        return count;
    snprintf(out_roots[count++], 1024, "%s", cand);
    return count;
}

static int pkg_store_source_roots(const char *store,
                                  char (*out_roots)[1024], int count,
                                  int max_roots) {
    if (!pkg_is_dir(store)) return count;
    int n = 0;
    char **names = pkg_dir_names_sorted(store, &n);
    if (!names) return count;
    for (int i = 0; i < n; i++) {
        const char *name = names[i];
        if (!pkg_safe_component(name)) continue;
        char pkg[1024];
        if (snprintf(pkg, sizeof(pkg), "%s" PATH_SEP "%s", store, name)
            >= (int)sizeof(pkg)) continue;
#ifdef _WIN32
        DWORD attr = GetFileAttributesA(pkg);
        if (attr == INVALID_FILE_ATTRIBUTES ||
            (attr & FILE_ATTRIBUTE_REPARSE_POINT) ||
            !(attr & FILE_ATTRIBUTE_DIRECTORY)) continue;
#else
        struct stat st;
        if (lstat(pkg, &st) != 0 || !S_ISDIR(st.st_mode) ||
            S_ISLNK(st.st_mode)) continue;
#endif
        count = pkg_add_package_root(store, name, out_roots,
                                     count, max_roots);
    }
    pkg_dir_names_free(names, n);
    return count;
}

int zan_pkg_all_source_roots(const char *project_dir,
                             char (*out_roots)[1024], int max_roots) {
    if (!project_dir || !project_dir[0] || !out_roots || max_roots <= 0)
        return 0;
    char store[1024];
    int count = 0;
    if (project_dir && project_dir[0]) {
        snprintf(store, sizeof(store), "%s/packages", project_dir);
        count = pkg_store_source_roots(store, out_roots, count, max_roots);
        snprintf(store, sizeof(store), "%s/.zan-packages", project_dir);
        count = pkg_store_source_roots(store, out_roots, count, max_roots);
    }
    char exe_dir[1024] = {0};
#ifdef _WIN32
    if (GetModuleFileNameA(NULL, exe_dir, sizeof(exe_dir))) {
        char *sep = strrchr(exe_dir, '\\'); if (sep) *sep = 0;
        snprintf(store, sizeof(store), "%s/../packages", exe_dir);
        count = pkg_store_source_roots(store, out_roots, count, max_roots);
        snprintf(store, sizeof(store), "%s/packages", exe_dir);
        count = pkg_store_source_roots(store, out_roots, count, max_roots);
    }
#elif defined(__APPLE__)
    uint32_t size = sizeof(exe_dir);
    if (_NSGetExecutablePath(exe_dir, &size) == 0) {
        char *sep = strrchr(exe_dir, '/'); if (sep) *sep = 0;
        snprintf(store, sizeof(store), "%s/../packages", exe_dir);
        count = pkg_store_source_roots(store, out_roots, count, max_roots);
    }
#else
    ssize_t len = readlink("/proc/self/exe", exe_dir, sizeof(exe_dir) - 1);
    if (len > 0) {
        exe_dir[len] = 0;
        char *sep = strrchr(exe_dir, '/'); if (sep) *sep = 0;
        snprintf(store, sizeof(store), "%s/../packages", exe_dir);
        count = pkg_store_source_roots(store, out_roots, count, max_roots);
    }
#endif
    if (zan_pkg_global_store(store, sizeof(store)))
        count = pkg_store_source_roots(store, out_roots, count, max_roots);
    return count;
}

int zan_pkg_find_namespace(const char *project_dir, const char *namespace_path,
                           char (*out_dirs)[1024], int max_dirs) {
    if (!project_dir || !project_dir[0] ||
        !pkg_safe_namespace_path(namespace_path) ||
        !out_dirs || max_dirs <= 0) return 0;
    char store[1024]; int count = 0;
    snprintf(store, sizeof(store), "%s" PATH_SEP "packages", project_dir);
    count = pkg_scan_store(store, namespace_path, out_dirs, count, max_dirs);
    snprintf(store, sizeof(store), "%s" PATH_SEP ".zan-packages", project_dir);
    count = pkg_scan_store(store, namespace_path, out_dirs, count, max_dirs);
    {
        char exe_dir[1024] = {0};
#ifdef _WIN32
        if (GetModuleFileNameA(NULL, exe_dir, sizeof(exe_dir))) {
            char *last_sep = strrchr(exe_dir, '\\');
            if (last_sep) *last_sep = '\0';
            snprintf(store, sizeof(store), "%s\\..\\packages", exe_dir);
            if (pkg_is_dir(store))
                count = pkg_scan_store(store, namespace_path, out_dirs, count, max_dirs);
            snprintf(store, sizeof(store), "%s\\packages", exe_dir);
            if (pkg_is_dir(store))
                count = pkg_scan_store(store, namespace_path, out_dirs, count, max_dirs);
        }
#elif defined(__APPLE__)
        uint32_t exe_sz = sizeof(exe_dir);
        if (_NSGetExecutablePath(exe_dir, &exe_sz) == 0) {
            char *last_sep = strrchr(exe_dir, '/');
            if (last_sep) *last_sep = '\0';
            snprintf(store, sizeof(store), "%s/../packages", exe_dir);
            if (pkg_is_dir(store))
                count = pkg_scan_store(store, namespace_path, out_dirs, count, max_dirs);
        }
#else
        ssize_t len = readlink("/proc/self/exe", exe_dir, sizeof(exe_dir) - 1);
        if (len > 0) {
            exe_dir[len] = '\0';
            char *last_sep = strrchr(exe_dir, '/');
            if (last_sep) *last_sep = '\0';
            snprintf(store, sizeof(store), "%s/../packages", exe_dir);
            if (pkg_is_dir(store))
                count = pkg_scan_store(store, namespace_path, out_dirs, count, max_dirs);
        }
#endif
    }
    if (zan_pkg_global_store(store, sizeof(store)))
        count = pkg_scan_store(store, namespace_path, out_dirs, count, max_dirs);
    return count;
}

static bool pkg_mkdirs(const char *path) {
    char tmp[1024]; size_t len = strlen(path);
    if (len == 0 || len >= sizeof(tmp)) return false;
    memcpy(tmp, path, len + 1);
    for (size_t i = 1; i <= len; i++) {
        if (tmp[i] != '/' && tmp[i] != '\\' && tmp[i] != 0) continue;
#ifdef _WIN32
        if (i == 2 && tmp[1] == ':') continue;
#endif
        char save = tmp[i]; tmp[i] = 0;
        if (*tmp && !pkg_is_dir(tmp)) {
#ifdef _WIN32
            if (!CreateDirectoryA(tmp, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) return false;
#else
            if (mkdir(tmp, 0755) != 0 && errno != EEXIST) return false;
#endif
        }
        tmp[i] = save;
    }
    return true;
}

static bool pkg_copy_tree(const char *src, const char *dst, int depth) {
    if (depth > 64 || !pkg_mkdirs(dst)) return false;
#ifdef _WIN32
    char pattern[1024]; WIN32_FIND_DATAA fd;
    snprintf(pattern, sizeof(pattern), "%s\\*", src);
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return false;
    bool ok = true;
    do {
        if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) continue;
        if (!pkg_safe_component(fd.cFileName) ||
            (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) { ok = false; break; }
        char a[1024], b[1024];
        snprintf(a, sizeof(a), "%s\\%s", src, fd.cFileName);
        snprintf(b, sizeof(b), "%s\\%s", dst, fd.cFileName);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ok = pkg_copy_tree(a, b, depth + 1);
        else ok = CopyFileA(a, b, TRUE) != 0;
    } while (ok && FindNextFileA(h, &fd));
    FindClose(h); return ok;
#else
    DIR *d = opendir(src); if (!d) return false;
    bool ok = true; struct dirent *e;
    while (ok && (e = readdir(d)) != NULL) {
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
        if (!pkg_safe_component(e->d_name)) { ok = false; break; }
        char a[1024], b[1024]; struct stat st;
        snprintf(a, sizeof(a), "%s/%s", src, e->d_name);
        snprintf(b, sizeof(b), "%s/%s", dst, e->d_name);
        if (lstat(a, &st) != 0 || S_ISLNK(st.st_mode)) { ok = false; break; }
        if (S_ISDIR(st.st_mode)) ok = pkg_copy_tree(a, b, depth + 1);
        else if (S_ISREG(st.st_mode)) {
            FILE *in = fopen(a, "rb"), *out = in ? fopen(b, "wb") : NULL;
            if (!out) { if (in) fclose(in); ok = false; break; }
            char buf[16384]; size_t n;
            while ((n = fread(buf, 1, sizeof(buf), in)) != 0)
                if (fwrite(buf, 1, n, out) != n) { ok = false; break; }
            if (ferror(in) || fclose(out) != 0) ok = false;
            fclose(in);
        } else ok = false;
    }
    closedir(d); return ok;
#endif
}

static void pkg_remove_tree(const char *path) {
#ifdef _WIN32
    char pattern[1024]; WIN32_FIND_DATAA fd;
    snprintf(pattern, sizeof(pattern), "%s\\*", path);
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) continue;
            char p[1024]; snprintf(p, sizeof(p), "%s\\%s", path, fd.cFileName);
            if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
                !(fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) pkg_remove_tree(p);
            else DeleteFileA(p);
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
    RemoveDirectoryA(path);
#else
    DIR *d = opendir(path); struct dirent *e;
    if (d) { while ((e = readdir(d)) != NULL) {
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
        char p[1024]; struct stat st; snprintf(p, sizeof(p), "%s/%s", path, e->d_name);
        if (lstat(p, &st) == 0 && S_ISDIR(st.st_mode) && !S_ISLNK(st.st_mode)) pkg_remove_tree(p);
        else unlink(p);
    } closedir(d); }
    rmdir(path);
#endif
}

bool zan_pkg_install_local(const char *source_dir, const char *package_name,
                           zan_pkg_scope_t scope, const char *project_dir,
                           char *status, size_t status_size) {
    if (!source_dir || !pkg_safe_component(package_name) || !pkg_is_dir(source_dir)) {
        snprintf(status, status_size, "ZANPKG_STATUS action=install status=invalid_source"); return false;
    }
    char manifest[1024];
    snprintf(manifest, sizeof(manifest), "%s" PATH_SEP "zan.pkg", source_dir);
    zan_package_t pkg;
    if (!zan_pkg_load(&pkg, manifest) || !pkg.name[0] || strcmp(pkg.name, package_name) != 0) {
        snprintf(status, status_size, "ZANPKG_STATUS action=install status=invalid_manifest package=%s", package_name);
        return false;
    }
    if (!pkg.has_version) {
        snprintf(status, status_size, "ZANPKG_STATUS action=install status=missing_version package=%s", package_name);
        zan_pkg_destroy(&pkg);
        return false;
    }
    char pkg_src[1024], pkg_stdlib[1024];
    snprintf(pkg_src, sizeof(pkg_src), "%s" PATH_SEP "src", source_dir);
    snprintf(pkg_stdlib, sizeof(pkg_stdlib), "%s" PATH_SEP "stdlib", source_dir);
    if (!pkg_is_dir(pkg_src) && !pkg_is_dir(pkg_stdlib)) {
        snprintf(status, status_size, "ZANPKG_STATUS action=install status=no_stdlib_layout package=%s", package_name);
        zan_pkg_destroy(&pkg);
        return false;
    }
    zan_pkg_destroy(&pkg);
    char store[1024];
    if (scope == ZAN_PKG_SCOPE_GLOBAL) {
        if (!zan_pkg_global_store(store, sizeof(store))) {
            snprintf(status, status_size, "ZANPKG_STATUS action=install status=no_global_store package=%s", package_name); return false;
        }
    } else {
        if (!project_dir || !*project_dir) {
            snprintf(status, status_size, "ZANPKG_STATUS action=install status=no_project package=%s", package_name); return false;
        }
        snprintf(store, sizeof(store), "%s" PATH_SEP ".zan-packages", project_dir);
    }
    if (!pkg_mkdirs(store)) {
        snprintf(status, status_size, "ZANPKG_STATUS action=install status=io_error package=%s", package_name); return false;
    }
    char dst[1024], stage[1024];
    snprintf(dst, sizeof(dst), "%s" PATH_SEP "%s", store, package_name);
#ifdef _WIN32
    snprintf(stage, sizeof(stage), "%s" PATH_SEP ".%s.tmp.%lu", store, package_name, (unsigned long)GetCurrentProcessId());
#else
    snprintf(stage, sizeof(stage), "%s/.%s.tmp.%ld", store, package_name, (long)getpid());
#endif
    if (pkg_is_dir(dst)) {
        snprintf(status, status_size, "ZANPKG_STATUS action=install status=exists package=%s", package_name); return false;
    }
    pkg_remove_tree(stage);
    if (!pkg_copy_tree(source_dir, stage, 0)) {
        pkg_remove_tree(stage);
        snprintf(status, status_size, "ZANPKG_STATUS action=install status=unsafe_or_io package=%s", package_name); return false;
    }
#ifdef _WIN32
    bool renamed = MoveFileExA(stage, dst, MOVEFILE_WRITE_THROUGH) != 0;
#else
    bool renamed = rename(stage, dst) == 0;
#endif
    if (!renamed) { pkg_remove_tree(stage); snprintf(status, status_size, "ZANPKG_STATUS action=install status=io_error package=%s", package_name); return false; }
    snprintf(status, status_size, "ZANPKG_STATUS action=install status=installed package=%s scope=%s",
             package_name, scope == ZAN_PKG_SCOPE_GLOBAL ? "global" : "project");
    return true;
}

bool zan_pkg_api_validate(const char *api_url, char *status, size_t status_size) {
    bool secure = api_url && (strncmp(api_url, "https://", 8) == 0 ||
        strncmp(api_url, "http://localhost", 16) == 0 ||
        strncmp(api_url, "http://127.0.0.1", 16) == 0 ||
        strncmp(api_url, "http://[::1]", 12) == 0);
    if (!secure) {
        snprintf(status, status_size, "ZANPKG_STATUS action=api status=insecure_url");
        return false;
    }
    snprintf(status, status_size, "ZANPKG_STATUS action=remote status=unsupported capability=http_archive_signature");
    return true;
}

void zan_pkg_destroy(zan_package_t *pkg) {
    free(pkg->deps);
    free(pkg->source_dirs);
    memset(pkg, 0, sizeof(*pkg));
}

void zan_pkg_registry_destroy(zan_pkg_registry_t *reg) {
    free(reg->cache_dir);
    free(reg->lock_file);
    for (int i = 0; i < reg->resolved_count; i++) { zan_pkg_destroy(reg->resolved[i]); free(reg->resolved[i]); }
    free(reg->resolved);
    memset(reg, 0, sizeof(*reg));
}


typedef struct {
    char pkg[128];
    char plugin_id[64];
} zan_usage_seen_t;

static zan_usage_seen_t zan_usage_seen[1024];
static int zan_usage_seen_count;

void zan_pkg_note_usage(const char *store, const char *package_name) {
    if (!store || !package_name || !*package_name) return;
    for (int i = 0; i < zan_usage_seen_count; i++)
        if (strcmp(zan_usage_seen[i].pkg, package_name) == 0) return;
    if (zan_usage_seen_count >= (int)(sizeof(zan_usage_seen) / sizeof(zan_usage_seen[0])))
        return;
    char manifest_path[1200];
    snprintf(manifest_path, sizeof(manifest_path), "%s" PATH_SEP "%s" PATH_SEP "zan.pkg",
             store, package_name);
    zan_package_t pkg;
    memset(&pkg, 0, sizeof(pkg));
    if (!zan_pkg_load(&pkg, manifest_path)) return;
    if (pkg.plugin_id[0]) {
        strncpy(zan_usage_seen[zan_usage_seen_count].pkg, package_name,
                sizeof(zan_usage_seen[0].pkg) - 1);
        strncpy(zan_usage_seen[zan_usage_seen_count].plugin_id, pkg.plugin_id,
                sizeof(zan_usage_seen[0].plugin_id) - 1);
        zan_usage_seen_count++;
        fprintf(stderr, "ZANPKG_USAGE plugin_id=%s package=%s action=build\n",
                pkg.plugin_id, package_name);
    }
    zan_pkg_destroy(&pkg);
}
