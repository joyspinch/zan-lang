
static bool pkg_token_is_shell_safe(const char *s) {
    if (!s) return true;
    for (const char *p = s; *p; p++) {
        unsigned char c = (unsigned char)*p;
        if (isalnum(c)) continue;
        if (c == '%') {
            if (isxdigit((unsigned char)p[1]) && isxdigit((unsigned char)p[2])) {
                p += 2;
                continue;
            }
            return false;
        }
        if (strchr(":/@._~+-", c) != NULL) continue;
        return false;
    }
    return true;
}

static bool pkg_safe_component(const char *s);
static bool pkg_mkdirs(const char *path);
static bool pkg_copy_tree(const char *src, const char *dst, int depth);

bool zan_pkg_fetch(zan_pkg_registry_t *reg, const zan_dependency_t *dep) {
    char pkg_dir[1024];
    snprintf(pkg_dir, sizeof(pkg_dir), "%s" PATH_SEP "%s", reg->cache_dir, dep->name);
    if (pkg_is_dir(pkg_dir)) return true;
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
        char cmd[2048]; char ver_buf[64];
        zan_version_format(&dep->min_ver, ver_buf, sizeof(ver_buf));
        if (!pkg_token_is_shell_safe(dep->name) ||
            !pkg_token_is_shell_safe(dep->source) ||
            !pkg_token_is_shell_safe(ver_buf)) {
            fprintf(stderr, "error: refusing to fetch '%s': unsafe characters in package, source or version\n", dep->name);
            return false;
        }
        snprintf(cmd, sizeof(cmd), "git clone --depth 1 --branch v%s \"%s\" \"%s\" 2>&1", ver_buf, dep->source, pkg_dir);
        int ret = system(cmd);
        if (ret != 0) {
            snprintf(cmd, sizeof(cmd), "git clone --depth 1 \"%s\" \"%s\" 2>&1", dep->source, pkg_dir);
            ret = system(cmd);
        }
        return ret == 0;
    } else if (dep->source[0] && pkg_is_dir(dep->source)) {
        if (!pkg_mkdirs(pkg_dir)) return false;
        return pkg_copy_tree(dep->source, pkg_dir, 0);
    } else if (dep->source[0] == 0) {
        /* 1. 尝试从全局包存储恢复 */
        char gstore[1024];
        if (zan_pkg_global_store(gstore, sizeof(gstore))) {
            char src_dir[1024];
            snprintf(src_dir, sizeof(src_dir), "%s" PATH_SEP "%s", gstore, dep->name);
            if (pkg_is_dir(src_dir)) {
                if (!pkg_mkdirs(pkg_dir)) return false;
                return pkg_copy_tree(src_dir, pkg_dir, 0);
            }
        }
        /* 2. 尝试从当前工程向上层目录回退寻找 packages/<dep->name> (Monorepo 溯源，最多遍历 8 层) */
        char curr[1024];
        snprintf(curr, sizeof(curr), "%s", reg->cache_dir);
        char *sep = strrchr(curr, '\\');
        char *sep2 = strrchr(curr, '/');
        if (sep2 && (!sep || sep2 > sep)) sep = sep2;
        if (sep) *sep = '\0';

        for (int depth = 0; depth < 8; depth++) {
            char cand[1024];
            snprintf(cand, sizeof(cand), "%s" PATH_SEP "packages" PATH_SEP "%s", curr, dep->name);
            if (pkg_is_dir(cand)) {
                if (!pkg_mkdirs(pkg_dir)) return false;
                return pkg_copy_tree(cand, pkg_dir, 0);
            }
            char next[1024];
            snprintf(next, sizeof(next), "%s" PATH_SEP "..", curr);
            if (!pkg_is_dir(next)) break;
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

static zan_solver_entry_t *solver_find_entry(zan_diamond_solver_t *solver, const char *name) {
    if (!solver || !name) return NULL;
    for (int i = 0; i < solver->entry_count; i++) {
        if (strcmp(solver->entries[i].name, name) == 0) {
            return &solver->entries[i];
        }
    }
    return NULL;
}

static zan_solver_entry_t *solver_get_or_create_entry(zan_diamond_solver_t *solver, const char *name) {
    zan_solver_entry_t *e = solver_find_entry(solver, name);
    if (e) return e;
    if (solver->entry_count >= 256) return NULL;
    e = &solver->entries[solver->entry_count++];
    memset(e, 0, sizeof(*e));
    strncpy(e->name, name, sizeof(e->name) - 1);
    return e;
}

static bool solver_add_constraint(zan_diamond_solver_t *solver, const char *pkg_name,
                                  const char *parent_name, const zan_dependency_t *dep) {
    zan_solver_entry_t *entry = solver_get_or_create_entry(solver, pkg_name);
    if (!entry) {
        fprintf(stderr, "error: dependency solver exceeded maximum package capacity (256)\n");
        return false;
    }
    if (dep->source[0] && !entry->source[0]) {
        strncpy(entry->source, dep->source, sizeof(entry->source) - 1);
    }
    zan_version_range_t new_range;
    zan_dep_to_range(dep, &new_range);

    if (entry->source_count == 0) {
        entry->merged_range = new_range;
        strncpy(entry->sources[0], parent_name ? parent_name : "root", sizeof(entry->sources[0]) - 1);
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
        fprintf(stderr, "       '%s' requires %s %s\n",
                entry->sources[0], pkg_name, prev_buf);
        fprintf(stderr, "       '%s' requires %s %s\n",
                parent_name ? parent_name : "root", pkg_name, new_buf);
        fprintf(stderr, "       no compatible version satisfies both constraints (empty range intersection)\n");
        return false;
    }

    entry->merged_range = intersected;
    if (entry->source_count < 8) {
        strncpy(entry->sources[entry->source_count], parent_name ? parent_name : "root", sizeof(entry->sources[0]) - 1);
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
            fprintf(stderr, "error: failed to fetch package '%s' from %s\n", dep->name, dep->source);
            all_ok = false;
            continue;
        }

        char manifest_path[1024];
        snprintf(manifest_path, sizeof(manifest_path), "%s" PATH_SEP "%s" PATH_SEP "zan.pkg", reg->cache_dir, dep->name);
        char pkg_dir[1024];
        snprintf(pkg_dir, sizeof(pkg_dir), "%s" PATH_SEP "%s", reg->cache_dir, dep->name);
        zan_package_t fetched_pkg;
        if (zan_pkg_load(&fetched_pkg, manifest_path)) {
            char computed_hash[65] = {0};
            /* B-ID90: 校验对象从 manifest 升级为整包源码树（防源码篡改） */
            bool has_hash = zan_pkg_compute_tree_checksum(pkg_dir, computed_hash);

            /* 3. 针对累计交集约束验证版本满足性 */
            if (!zan_version_satisfies_range(&fetched_pkg.version, &entry->merged_range)) {
                char ver_buf[64], range_buf[128];
                zan_version_format(&fetched_pkg.version, ver_buf, sizeof(ver_buf));
                zan_range_format(&entry->merged_range, range_buf, sizeof(range_buf));
                fprintf(stderr, "warning: package '%s' version %s does not satisfy merged constraint %s\n",
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
                    if (reg->resolved[r] && strcmp(reg->resolved[r]->name, fetched_pkg.name) == 0) {
                        already_resolved = true;
                        /* 钻石依赖仲裁升级：更新为当前仲裁选取的满足版本 */
                        reg->resolved[r]->version = fetched_pkg.version;
                        if (reg->resolved[r]->checksum[0] && has_hash) {
                            if (strcmp(reg->resolved[r]->checksum, computed_hash) != 0) {
                                fprintf(stderr, "error: checksum mismatch for package '%s': expected %s, got %s\n",
                                        fetched_pkg.name, reg->resolved[r]->checksum, computed_hash);
                                zan_pkg_destroy(&fetched_pkg);
                                return false;
                            }
                        } else if (has_hash) {
                            strncpy(reg->resolved[r]->checksum, computed_hash, sizeof(reg->resolved[r]->checksum) - 1);
                        }
                        break;
                    }
                }
                if (!already_resolved) {
                    zan_package_t *rp = (zan_package_t *)calloc(1, sizeof(zan_package_t));
                    strncpy(rp->name, fetched_pkg.name, sizeof(rp->name) - 1);
                    if (dep->source[0]) strncpy(rp->source, dep->source, sizeof(rp->source) - 1);
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
                    if (strcmp(seen[s], fetched_pkg.name) == 0) { seen_before = true; break; }
                }
                if (seen_before) {
                    fprintf(stderr, "note: '%s' is already resolved (shared dependency "
                                    "or dependency cycle); skipping its subtree\n",
                            fetched_pkg.name);
                } else if (*seen_count < 256) {
                    snprintf(seen[*seen_count], 128, "%s", fetched_pkg.name);
                    (*seen_count)++;
                    if (!zan_pkg_resolve_seen(reg, &fetched_pkg, seen, seen_count, solver))
                        all_ok = false;
                } else {
                    fprintf(stderr, "error: dependency chain deeper than 256 packages\n");
                    all_ok = false;
                }
            }
            zan_pkg_destroy(&fetched_pkg);
        } else {
            fprintf(stderr, "error: failed to read manifest for package '%s'\n", dep->name);
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
    if (!reg || !reg->lock_file) return false;
    FILE *f = fopen(reg->lock_file, "w");
    if (!f) return false;
    fprintf(f, "# Zan lock file - auto-generated\n\n");
    for (int i = 0; i < reg->resolved_count; i++) {
        zan_package_t *pkg = reg->resolved[i];
        if (!pkg) continue;
        char ver_buf[64]; zan_version_format(&pkg->version, ver_buf, sizeof(ver_buf));
        fprintf(f, "[[package]]\nname = \"%s\"\nversion = \"%s\"\n", pkg->name, ver_buf);
        if (pkg->source[0]) {
            fprintf(f, "source = \"%s\"\n", pkg->source);
        }
        if (pkg->checksum[0]) {
            fprintf(f, "checksum = \"%s\"\n", pkg->checksum);
        }
        fprintf(f, "\n");
    }
    bool ok = (ferror(f) == 0);
    if (fclose(f) != 0) ok = false;
    return ok;
}

static bool add_locked_pkg(zan_pkg_registry_t *reg, const char *name, const char *ver_str,
                           const char *source, const char *checksum) {
    if (!name || !name[0] || !ver_str || !ver_str[0]) return false;
    if (!pkg_safe_component(name)) {
        fprintf(stderr, "error: unsafe package name '%s' in lock file\n", name);
        return false;
    }
    zan_version_t v;
    if (!zan_version_parse(ver_str, &v)) {
        fprintf(stderr, "error: invalid version '%s' for package '%s' in lock file\n", ver_str, name);
        return false;
    }
    if (!reg->resolved) {
        reg->resolved = (zan_package_t **)calloc(256, sizeof(zan_package_t *));
        reg->resolved_count = 0;
    }
    for (int i = 0; i < reg->resolved_count; i++) {
        if (reg->resolved[i] && strcmp(reg->resolved[i]->name, name) == 0) {
            if (source && source[0]) strncpy(reg->resolved[i]->source, source, sizeof(reg->resolved[i]->source) - 1);
            if (checksum && checksum[0]) strncpy(reg->resolved[i]->checksum, checksum, sizeof(reg->resolved[i]->checksum) - 1);
            return zan_version_compare(&reg->resolved[i]->version, &v) == 0;
        }
    }
    if (reg->resolved_count >= 256) {
        fprintf(stderr, "error: lock file contains too many packages (>256)\n");
        return false;
    }
    zan_package_t *pkg = (zan_package_t *)calloc(1, sizeof(zan_package_t));
    strncpy(pkg->name, name, sizeof(pkg->name) - 1);
    pkg->version = v;
    pkg->has_version = true;
    if (source && source[0]) strncpy(pkg->source, source, sizeof(pkg->source) - 1);
    if (checksum && checksum[0]) strncpy(pkg->checksum, checksum, sizeof(pkg->checksum) - 1);
    reg->resolved[reg->resolved_count++] = pkg;
    return true;
}

bool zan_pkg_read_lock(zan_pkg_registry_t *reg) {
    if (!reg || !reg->lock_file) return false;
    FILE *f = fopen(reg->lock_file, "r");
    if (!f) return false;

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
        if (*p == '#' || *p == '\n' || *p == '\r' || *p == 0) continue;
        if (*p == '[') {
            if (strncmp(p, "[[package]]", 11) == 0) {
                if (in_pkg) {
                    if (!cur_name[0] || !cur_ver[0] || !add_locked_pkg(reg, cur_name, cur_ver, cur_source, cur_checksum)) {
                        ok = false;
                        break;
                    }
                }
                in_pkg = true;
                cur_name[0] = 0;
                cur_ver[0] = 0;
                cur_source[0] = 0;
                cur_checksum[0] = 0;
            }
            continue;
        }
        if (!in_pkg) continue;
        char key[128] = {0};
        int ki = 0;
        while (*p && *p != '=' && !isspace((unsigned char)*p) && ki < 127) { key[ki++] = *p; p++; }
        key[ki] = 0;
        skip_ws(&p);
        if (*p == '=') p++;
        skip_ws(&p);
        char val[512] = {0};
        if (*p == '"') { read_qstr(&p, val, sizeof(val)); }
        else { int vi = 0; while (*p && *p != '\n' && *p != '\r' && vi < 511) { val[vi++] = *p; p++; } val[vi] = 0; }

        if (strcmp(key, "name") == 0) {
            strncpy(cur_name, val, sizeof(cur_name) - 1);
        } else if (strcmp(key, "version") == 0) {
            strncpy(cur_ver, val, sizeof(cur_ver) - 1);
        } else if (strcmp(key, "source") == 0) {
            strncpy(cur_source, val, sizeof(cur_source) - 1);
        } else if (strcmp(key, "checksum") == 0) {
            strncpy(cur_checksum, val, sizeof(cur_checksum) - 1);
        }
    }
    if (ok && in_pkg) {
        if (!cur_name[0] || !cur_ver[0] || !add_locked_pkg(reg, cur_name, cur_ver, cur_source, cur_checksum)) {
            ok = false;
        }
    }
    fclose(f);
    return ok;
}

