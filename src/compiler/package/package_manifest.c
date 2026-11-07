static void skip_ws(const char **p) {
    while (**p && isspace((unsigned char)**p)) (*p)++;
}

static void zan_dep_parse_constraint(const char *str, zan_dependency_t *dep) {
    if (!dep) return;
    if (!str || !*str) {
        dep->kind = ZAN_DEP_ANY;
        return;
    }
    const char *p = str;
    skip_ws(&p);
    if (!*p || *p == '*' || *p == 'x' || *p == 'X') {
        dep->kind = ZAN_DEP_ANY;
        return;
    }
    if (*p == '^') {
        dep->kind = ZAN_DEP_COMPAT;
        p++;
        skip_ws(&p);
        zan_version_parse(p, &dep->min_ver);
    } else if (*p == '~') {
        dep->kind = ZAN_DEP_TILDE;
        p++;
        skip_ws(&p);
        zan_version_parse(p, &dep->min_ver);
    } else if (*p == '>' && *(p + 1) == '=') {
        dep->kind = ZAN_DEP_MINIMUM;
        p += 2;
        skip_ws(&p);
        char ver1[64] = {0};
        int vi = 0;
        while (*p && !isspace((unsigned char)*p) && *p != ',' && vi < 63) {
            ver1[vi++] = *p++;
        }
        ver1[vi] = 0;
        zan_version_parse(ver1, &dep->min_ver);
        skip_ws(&p);
        if (*p == ',') { p++; skip_ws(&p); }
        if (*p == '<') {
            dep->kind = ZAN_DEP_RANGE;
            if (*(p + 1) == '=') p += 2;
            else p++;
            skip_ws(&p);
            zan_version_parse(p, &dep->max_ver);
        }
    } else if (*p == '<' && *(p + 1) == '=') {
        dep->kind = ZAN_DEP_MAXIMUM;
        p += 2;
        skip_ws(&p);
        zan_version_parse(p, &dep->min_ver);
    } else if (*p == '=') {
        dep->kind = ZAN_DEP_EXACT;
        p++;
        skip_ws(&p);
        zan_version_parse(p, &dep->min_ver);
    } else {
        dep->kind = ZAN_DEP_COMPAT;
        zan_version_parse(p, &dep->min_ver);
    }
}

static bool read_qstr(const char **p, char *out, int max_len) {
    skip_ws(p);
    if (**p != '"') return false;
    (*p)++;
    int i = 0;
    while (**p && **p != '"' && i < max_len - 1) { out[i++] = **p; (*p)++; }
    out[i] = 0;
    if (**p == '"') (*p)++;
    return true;
}

static bool path_ends_with_json(const char *path) {
    if (!path) return false;
    size_t len = strlen(path);
    if (len >= 5 && (strcmp(path + len - 5, ".json") == 0 || strcmp(path + len - 5, ".JSON") == 0)) {
        return true;
    }
    return false;
}

static void parse_dep_spec(const char *spec, char *out_name, size_t name_cap, char *out_constraint, size_t constr_cap) {
    if (!spec || !out_name || !out_constraint) return;
    out_name[0] = 0;
    out_constraint[0] = 0;
    while (*spec && isspace((unsigned char)*spec)) spec++;
    size_t ni = 0;
    while (*spec && !isspace((unsigned char)*spec) && *spec != '@' && *spec != '=' && *spec != '^' && *spec != '~' && *spec != '>' && *spec != '<' && ni + 1 < name_cap) {
        out_name[ni++] = *spec++;
    }
    out_name[ni] = 0;
    while (*spec && isspace((unsigned char)*spec)) spec++;
    if (*spec == '@') spec++;
    while (*spec && isspace((unsigned char)*spec)) spec++;
    size_t ci = 0;
    while (*spec && ci + 1 < constr_cap) {
        out_constraint[ci++] = *spec++;
    }
    out_constraint[ci] = 0;
    if (out_constraint[0] == 0) {
        strncpy(out_constraint, "*", constr_cap - 1);
    }
}

bool zan_pkg_load(zan_package_t *pkg, const char *manifest_path) {
    FILE *f = fopen(manifest_path, "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0 || sz > 10 * 1024 * 1024) { fclose(f); return false; }
    char *buf = (char *)malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return false; }
    size_t nr = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[nr] = 0;

    memset(pkg, 0, sizeof(*pkg));
    pkg->dep_cap = 16;
    pkg->deps = (zan_dependency_t *)calloc((size_t)pkg->dep_cap, sizeof(zan_dependency_t));

    const char *scan = buf;
    skip_ws(&scan);
    if (*scan == '{') {
        json_value *root = json_parse(buf);
        free(buf);
        if (!root || root->type != JSON_OBJ) {
            if (root) json_free(root);
            return false;
        }

        const char *name_str = json_get_str(json_obj_get(root, "name"));
        if (name_str) strncpy(pkg->name, name_str, sizeof(pkg->name) - 1);

        const char *ver_str = json_get_str(json_obj_get(root, "version"));
        if (ver_str) {
            pkg->has_version = zan_version_parse(ver_str, &pkg->version);
        }

        const char *desc_str = json_get_str(json_obj_get(root, "description"));
        if (desc_str) strncpy(pkg->description, desc_str, sizeof(pkg->description) - 1);

        const char *author_str = json_get_str(json_obj_get(root, "author"));
        if (author_str) strncpy(pkg->author, author_str, sizeof(pkg->author) - 1);

        const char *lic_str = json_get_str(json_obj_get(root, "license"));
        if (lic_str) strncpy(pkg->license, lic_str, sizeof(pkg->license) - 1);

        const char *entry_str = json_get_str(json_obj_get(root, "entry"));
        if (!entry_str) entry_str = json_get_str(json_obj_get(root, "entry_point"));
        if (entry_str) strncpy(pkg->entry_point, entry_str, sizeof(pkg->entry_point) - 1);

        const char *plugin_str = json_get_str(json_obj_get(root, "plugin_id"));
        if (plugin_str) strncpy(pkg->plugin_id, plugin_str, sizeof(pkg->plugin_id) - 1);

        json_value *deps = json_obj_get(root, "dependencies");
        if (!deps) deps = json_obj_get(root, "deps");
        if (deps && deps->type == JSON_OBJ) {
            for (int i = 0; i < deps->as.obj.count; i++) {
                const char *dep_name = deps->as.obj.keys[i];
                json_value *dep_val = deps->as.obj.vals[i];
                const char *dep_source = "";
                const char *dep_constraint = "*";

                if (dep_val && dep_val->type == JSON_STR) {
                    dep_constraint = dep_val->as.str;
                } else if (dep_val && dep_val->type == JSON_OBJ) {
                    const char *src = json_get_str(json_obj_get(dep_val, "source"));
                    if (!src) src = json_get_str(json_obj_get(dep_val, "git"));
                    if (src) dep_source = src;

                    const char *c = json_get_str(json_obj_get(dep_val, "version"));
                    if (c) dep_constraint = c;
                }
                zan_pkg_add_dep(pkg, dep_name, dep_source, dep_constraint);
            }
        } else if (deps && deps->type == JSON_ARR) {
            for (int i = 0; i < deps->as.arr.count; i++) {
                json_value *dep_item = deps->as.arr.items[i];
                if (dep_item && dep_item->type == JSON_STR) {
                    char dname[128] = {0};
                    char dconstraint[128] = {0};
                    parse_dep_spec(dep_item->as.str, dname, sizeof(dname), dconstraint, sizeof(dconstraint));
                    if (dname[0]) {
                        zan_pkg_add_dep(pkg, dname, "", dconstraint);
                    }
                }
            }
        }

        json_free(root);
        return true;
    }

    /* INI / zan.pkg 格式解析 */
    const char *p = buf;
    bool in_deps = false;
    bool in_deps_array = false;
    while (*p) {
        char line[1024];
        int li = 0;
        while (*p && *p != '\n' && *p != '\r' && li < (int)sizeof(line) - 1) {
            line[li++] = *p++;
        }
        line[li] = 0;
        while (*p == '\n' || *p == '\r') p++;

        const char *lp = line;
        skip_ws(&lp);
        if (*lp == '#' || *lp == 0) continue;
        if (*lp == '[') {
            in_deps = (strncmp(lp, "[deps]", 6) == 0 || strncmp(lp, "[dependencies]", 14) == 0);
            in_deps_array = false;
            continue;
        }

        if (in_deps_array) {
            while (*lp) {
                skip_ws(&lp);
                if (*lp == ']') { in_deps_array = false; break; }
                if (*lp == ',' || *lp == ';') { lp++; continue; }
                if (*lp == '"') {
                    char dep_spec[256] = {0};
                    read_qstr(&lp, dep_spec, sizeof(dep_spec));
                    char dname[128] = {0};
                    char dconstraint[128] = {0};
                    parse_dep_spec(dep_spec, dname, sizeof(dname), dconstraint, sizeof(dconstraint));
                    if (dname[0]) {
                        zan_pkg_add_dep(pkg, dname, "", dconstraint);
                    }
                } else {
                    lp++;
                }
            }
            continue;
        }

        char key[128] = {0};
        int ki = 0;
        while (*lp && *lp != '=' && !isspace((unsigned char)*lp) && ki < 127) { key[ki++] = *lp; lp++; }
        key[ki] = 0;
        skip_ws(&lp);
        if (*lp == '=') lp++;
        skip_ws(&lp);

        if (!in_deps) {
            if ((strcmp(key, "deps") == 0 || strcmp(key, "dependencies") == 0) && *lp == '[') {
                lp++;
                in_deps_array = true;
                while (*lp) {
                    skip_ws(&lp);
                    if (*lp == ']') { in_deps_array = false; break; }
                    if (*lp == ',' || *lp == ';') { lp++; continue; }
                    if (*lp == '"') {
                        char dep_spec[256] = {0};
                        read_qstr(&lp, dep_spec, sizeof(dep_spec));
                        char dname[128] = {0};
                        char dconstraint[128] = {0};
                        parse_dep_spec(dep_spec, dname, sizeof(dname), dconstraint, sizeof(dconstraint));
                        if (dname[0]) {
                            zan_pkg_add_dep(pkg, dname, "", dconstraint);
                        }
                    } else {
                        lp++;
                    }
                }
                continue;
            }
            char val[256] = {0};
            if (*lp == '"') { read_qstr(&lp, val, sizeof(val)); }
            else { int vi = 0; while (*lp && *lp != '\n' && *lp != '\r' && vi < 255) { val[vi++] = *lp; lp++; } val[vi] = 0; }
            if (strcmp(key, "name") == 0) strncpy(pkg->name, val, sizeof(pkg->name) - 1);
            else if (strcmp(key, "version") == 0) {
                pkg->has_version = zan_version_parse(val, &pkg->version);
            }
            else if (strcmp(key, "description") == 0) strncpy(pkg->description, val, sizeof(pkg->description) - 1);
            else if (strcmp(key, "author") == 0) strncpy(pkg->author, val, sizeof(pkg->author) - 1);
            else if (strcmp(key, "license") == 0) strncpy(pkg->license, val, sizeof(pkg->license) - 1);
            else if (strcmp(key, "entry") == 0) strncpy(pkg->entry_point, val, sizeof(pkg->entry_point) - 1);
            else if (strcmp(key, "plugin_id") == 0) strncpy(pkg->plugin_id, val, sizeof(pkg->plugin_id) - 1);
        } else {
            if (pkg->dep_count >= pkg->dep_cap) {
                int ncap = pkg->dep_cap * 2;
                zan_dependency_t *grown = (zan_dependency_t *)realloc(pkg->deps, sizeof(zan_dependency_t) * (size_t)ncap);
                if (!grown) {
                    zan_host_oom();
                }
                pkg->deps = grown;
                pkg->dep_cap = ncap;
            }
            zan_dependency_t *dep = &pkg->deps[pkg->dep_count];
            memset(dep, 0, sizeof(*dep));
            strncpy(dep->name, key, sizeof(dep->name) - 1);
            if (*lp == '{') {
                lp++;
                while (*lp && *lp != '}') {
                    skip_ws(&lp);
                    char dkey[64] = {0}; int di = 0;
                    while (*lp && *lp != '=' && !isspace((unsigned char)*lp) && *lp != '}' && di < 63) { dkey[di++] = *lp; lp++; }
                    dkey[di] = 0; skip_ws(&lp);
                    if (*lp == '=') lp++; skip_ws(&lp);
                    char dval[512] = {0};
                    if (*lp == '"') { read_qstr(&lp, dval, sizeof(dval)); }
                    skip_ws(&lp); if (*lp == ',') lp++;
                    if (strcmp(dkey, "source") == 0 || strcmp(dkey, "git") == 0) strncpy(dep->source, dval, sizeof(dep->source) - 1);
                    else if (strcmp(dkey, "version") == 0) {
                        zan_dep_parse_constraint(dval, dep);
                    }
                }
                if (*lp == '}') lp++;
            } else if (*lp == '"') {
                char dval[512] = {0};
                read_qstr(&lp, dval, sizeof(dval));
                zan_dep_parse_constraint(dval, dep);
            } else {
                char dval[512] = {0};
                int vi = 0;
                while (*lp && *lp != '\n' && *lp != '\r' && vi < 511) { dval[vi++] = *lp++; }
                dval[vi] = 0;
                const char *dp = dval;
                skip_ws(&dp);
                if (dp[0]) {
                    zan_dep_parse_constraint(dp, dep);
                } else {
                    dep->kind = ZAN_DEP_ANY;
                }
            }
            pkg->dep_count++;
        }
    }
    free(buf);
    return true;
}

bool zan_pkg_save(const zan_package_t *pkg, const char *manifest_path) {
    FILE *f = fopen(manifest_path, "w");
    if (!f) return false;
    char ver_buf[64];
    zan_version_format(&pkg->version, ver_buf, sizeof(ver_buf));

    if (path_ends_with_json(manifest_path)) {
        fprintf(f, "{\n");
        fprintf(f, "  \"name\": \"%s\",\n", pkg->name);
        fprintf(f, "  \"version\": \"%s\"", ver_buf);
        if (pkg->description[0]) fprintf(f, ",\n  \"description\": \"%s\"", pkg->description);
        if (pkg->author[0]) fprintf(f, ",\n  \"author\": \"%s\"", pkg->author);
        if (pkg->license[0]) fprintf(f, ",\n  \"license\": \"%s\"", pkg->license);
        if (pkg->entry_point[0]) fprintf(f, ",\n  \"entry\": \"%s\"", pkg->entry_point);
        if (pkg->plugin_id[0]) fprintf(f, ",\n  \"plugin_id\": \"%s\"", pkg->plugin_id);
        if (pkg->dep_count > 0) {
            fprintf(f, ",\n  \"dependencies\": {\n");
            for (int i = 0; i < pkg->dep_count; i++) {
                const zan_dependency_t *d = &pkg->deps[i];
                char dep_ver[64]; zan_version_format(&d->min_ver, dep_ver, sizeof(dep_ver));
                const char *prefix = "";
                switch (d->kind) {
                case ZAN_DEP_COMPAT: prefix = "^"; break;
                case ZAN_DEP_TILDE: prefix = "~"; break;
                case ZAN_DEP_MINIMUM: prefix = ">="; break;
                case ZAN_DEP_MAXIMUM: prefix = "<="; break;
                case ZAN_DEP_EXACT: prefix = "="; break;
                case ZAN_DEP_RANGE: prefix = ">="; break;
                case ZAN_DEP_ANY: prefix = "*"; break;
                }
                if (d->source[0]) {
                    fprintf(f, "    \"%s\": { \"source\": \"%s\", \"version\": \"%s%s\" }%s\n",
                            d->name, d->source, prefix, dep_ver, (i + 1 < pkg->dep_count) ? "," : "");
                } else {
                    fprintf(f, "    \"%s\": \"%s%s\"%s\n",
                            d->name, prefix, dep_ver, (i + 1 < pkg->dep_count) ? "," : "");
                }
            }
            fprintf(f, "  }\n");
        } else {
            fprintf(f, ",\n  \"dependencies\": {}\n");
        }
        fprintf(f, "}\n");
    } else {
        fprintf(f, "# Zan Package Manifest\n");
        fprintf(f, "name = \"%s\"\n", pkg->name);
        fprintf(f, "version = \"%s\"\n", ver_buf);
        if (pkg->description[0]) fprintf(f, "description = \"%s\"\n", pkg->description);
        if (pkg->author[0]) fprintf(f, "author = \"%s\"\n", pkg->author);
        if (pkg->license[0]) fprintf(f, "license = \"%s\"\n", pkg->license);
        if (pkg->entry_point[0]) fprintf(f, "entry = \"%s\"\n", pkg->entry_point);
        if (pkg->plugin_id[0]) fprintf(f, "plugin_id = \"%s\"\n", pkg->plugin_id);
        fprintf(f, "\n[deps]\n");
        for (int i = 0; i < pkg->dep_count; i++) {
            const zan_dependency_t *d = &pkg->deps[i];
            char dep_ver[64]; zan_version_format(&d->min_ver, dep_ver, sizeof(dep_ver));
            const char *prefix = "";
            switch (d->kind) {
            case ZAN_DEP_COMPAT: prefix = "^"; break;
            case ZAN_DEP_TILDE: prefix = "~"; break;
            case ZAN_DEP_MINIMUM: prefix = ">="; break;
            case ZAN_DEP_MAXIMUM: prefix = "<="; break;
            case ZAN_DEP_EXACT: prefix = "="; break;
            case ZAN_DEP_RANGE: prefix = ">="; break;
            case ZAN_DEP_ANY: prefix = "*"; break;
            }
            fprintf(f, "%s = { source = \"%s\", version = \"%s%s\" }\n", d->name, d->source, prefix, dep_ver);
        }
    }
    bool ok = (ferror(f) == 0);
    if (fclose(f) != 0) ok = false;
    return ok;
}

void zan_pkg_new(zan_package_t *pkg, const char *name, const char *version) {
    memset(pkg, 0, sizeof(*pkg));
    strncpy(pkg->name, name, sizeof(pkg->name) - 1);
    zan_version_parse(version, &pkg->version);
    pkg->dep_cap = 16;
    pkg->deps = (zan_dependency_t *)calloc((size_t)pkg->dep_cap, sizeof(zan_dependency_t));
}

void zan_pkg_add_dep(zan_package_t *pkg, const char *name, const char *source, const char *version_constraint) {
    if (pkg->dep_count >= pkg->dep_cap) {
        int ncap = pkg->dep_cap * 2;
        zan_dependency_t *grown = (zan_dependency_t *)realloc(pkg->deps, sizeof(zan_dependency_t) * (size_t)ncap);
        if (!grown) {
            zan_host_oom();
        }
        pkg->deps = grown;
        pkg->dep_cap = ncap;
    }
    zan_dependency_t *dep = &pkg->deps[pkg->dep_count++];
    memset(dep, 0, sizeof(*dep));
    strncpy(dep->name, name, sizeof(dep->name) - 1);
    strncpy(dep->source, source, sizeof(dep->source) - 1);
    zan_dep_parse_constraint(version_constraint, dep);
}

bool zan_pkg_remove_dep(zan_package_t *pkg, const char *name) {
    for (int i = 0; i < pkg->dep_count; i++) {
        if (strcmp(pkg->deps[i].name, name) == 0) {
            memmove(&pkg->deps[i], &pkg->deps[i + 1], sizeof(zan_dependency_t) * (size_t)(pkg->dep_count - i - 1));
            pkg->dep_count--;
            return true;
        }
    }
    return false;
}

static bool pkg_is_dir(const char *path);

static void ensure_dir_pkg(const char *path) {
#ifdef _WIN32
    CreateDirectoryA(path, NULL);
#else
    mkdir(path, 0755);
#endif
}

void zan_pkg_init(zan_pkg_registry_t *reg, const char *project_dir) {
    memset(reg, 0, sizeof(*reg));
    size_t dlen = strlen(project_dir);
    reg->cache_dir = (char *)malloc(dlen + 32);
    snprintf(reg->cache_dir, dlen + 32, "%s" PATH_SEP ".zan-packages", project_dir);
    ensure_dir_pkg(reg->cache_dir);
    reg->lock_file = (char *)malloc(dlen + 16);
    snprintf(reg->lock_file, dlen + 16, "%s" PATH_SEP "zan.lock", project_dir);
    reg->resolved = (zan_package_t **)calloc(256, sizeof(zan_package_t *));
    reg->resolved_count = 0;
}

bool zan_pkg_version_satisfies(const zan_dependency_t *dep, const zan_version_t *ver) {
    if (!dep || !ver) return false;
    zan_version_range_t range;
    zan_dep_to_range(dep, &range);
    return zan_version_satisfies_range(ver, &range);
}
