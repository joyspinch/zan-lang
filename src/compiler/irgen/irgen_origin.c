/* irgen_origin.c -- where each emitted method body comes from.
 *
 * The partitioned backend (build_partition.h, docs/BUILD_CACHE.md stage 2)
 * groups functions by source unit so a user edit leaves library partitions
 * byte-identical and their objects cached. Only the grouping comes from
 * here; correctness never depends on it (a partition is keyed by its own
 * content), it only decides the hit rate.
 *
 * Unit of a file under a stdlib/ or packages/ directory: that
 * library-relative directory (the file's own name and any "src" component
 * dropped), cut to three levels:
 * stdlib/System/Collections/Generic/List.zan -> "stdlib:System/Collections/Generic",
 * packages/Zan.Gui/src/Gui/Widget/Tabs.zan -> "pkg:Zan.Gui/Gui/Widget".
 * Any other file is user code and a unit of its own ("user:<path>"): units
 * compile in parallel, and an edit recompiles only its file's unit.
 * A generic body instantiated over a user-declared type belongs to that
 * type's file: its code changes with that type. */

static bool origin_seg_eq(const char *s, size_t n, const char *lit) {
    return strlen(lit) == n && memcmp(s, lit, n) == 0;
}

static void origin_unit_of_path(const char *path, char *out, size_t cap) {
    snprintf(out, cap, "user:%s", path ? path : "");
    if (!path || !path[0]) return;
    /* Split into components (either separator), find the last stdlib/ or
     * packages/ component. */
    const char *seg[64];
    size_t len[64];
    int n = 0;
    for (const char *p = path; *p && n < 64;) {
        while (*p == '/' || *p == '\\') p++;
        if (!*p) break;
        const char *s = p;
        while (*p && *p != '/' && *p != '\\') p++;
        seg[n] = s;
        len[n] = (size_t)(p - s);
        n++;
    }
    int root = -1;
    const char *kind = NULL;
    for (int i = 0; i < n - 1; i++) {
        if (origin_seg_eq(seg[i], len[i], "stdlib")) root = i, kind = "stdlib:";
        else if (origin_seg_eq(seg[i], len[i], "packages")) root = i, kind = "pkg:";
    }
    if (root < 0) return;
    size_t at = (size_t)snprintf(out, cap, "%s", kind);
    int kept = 0;
    for (int i = root + 1; i < n - 1 && kept < 3 && at + 1 < cap; i++) {
        if (origin_seg_eq(seg[i], len[i], "src")) continue;
        int w = snprintf(out + at, cap - at, "%s%.*s", kept ? "/" : "", (int)len[i], seg[i]);
        if (w < 0) break;
        at += (size_t)w;
        if (at >= cap) at = cap - 1;
        kept++;
    }
    if (!kept) snprintf(out + at, cap - at, ".");
}

static int origin_intern(zan_irgen_t *g, const char *unit) {
    for (int i = 0; i < g->origin_unit_count; i++)
        if (!strcmp(g->origin_units[i], unit)) return i;
    char **nu =
        (char **)realloc(g->origin_units, (size_t)(g->origin_unit_count + 1) * sizeof(char *));
    if (!nu) return -1;
    g->origin_units = nu;
    char *copy = strdup(unit);
    if (!copy) return -1;
    g->origin_units[g->origin_unit_count] = copy;
    return g->origin_unit_count++;
}

/* Unit index of a file id, memoized; units are interned in g->origin_units. */
static int origin_unit_of_file(zan_irgen_t *g, uint32_t file_id) {
    if (!g->diag || file_id >= (uint32_t)g->diag->file_count) return -1;
    if ((int)file_id >= g->origin_file_cap) {
        int ncap = g->origin_file_cap ? g->origin_file_cap : 64;
        while (ncap <= (int)file_id) ncap *= 2;
        int *nf = (int *)realloc(g->origin_file_unit, (size_t)ncap * sizeof(int));
        if (!nf) return -1;
        for (int i = g->origin_file_cap; i < ncap; i++) nf[i] = -2;
        g->origin_file_unit = nf;
        g->origin_file_cap = ncap;
    }
    if (g->origin_file_unit[file_id] != -2) return g->origin_file_unit[file_id];
    char unit[256];
    origin_unit_of_path(g->diag->file_names ? g->diag->file_names[file_id] : NULL, unit,
                        sizeof(unit));
    int idx = origin_intern(g, unit);
    if (idx < 0) return -1;
    g->origin_file_unit[file_id] = idx;
    return idx;
}

static bool origin_is_user(zan_irgen_t *g, int unit) {
    return unit >= 0 && !strncmp(g->origin_units[unit], "user:", 5);
}

/* Unit of the first user-declared type `t` mentions, or -1. */
static int origin_user_type_unit(zan_irgen_t *g, zan_type_t *t, int depth) {
    if (!t || depth > 8) return -1;
    if (t->sym && t->sym->decl) {
        int u = origin_unit_of_file(g, t->sym->decl->loc.file_id);
        if (origin_is_user(g, u)) return u;
    }
    int u = origin_user_type_unit(g, t->element_type, depth + 1);
    for (int i = 0; u < 0 && i < t->type_arg_count; i++)
        u = origin_user_type_unit(g, t->type_args[i], depth + 1);
    if (u < 0) u = origin_user_type_unit(g, t->delegate_ret_type, depth + 1);
    for (int i = 0; u < 0 && i < t->delegate_param_count; i++)
        u = origin_user_type_unit(g, t->delegate_param_types[i], depth + 1);
    return u;
}

void zan_irgen_note_origin(zan_irgen_t *g, LLVMValueRef fn, zan_ast_node_t *decl, zan_type_t *inst,
                           zan_type_t **bind, int nbind) {
    if (!fn || !decl) return;
    int unit = origin_unit_of_file(g, decl->loc.file_id);
    if (unit < 0) return;
    if (!origin_is_user(g, unit)) {
        int u = origin_user_type_unit(g, inst, 0);
        for (int i = 0; i < nbind && bind && u < 0; i++) u = origin_user_type_unit(g, bind[i], 0);
        if (u >= 0) unit = u;
    }
    size_t nlen = 0;
    const char *name = LLVMGetValueName2(fn, &nlen);
    if (!name || !nlen) return;
    if (g->origin_count == g->origin_cap) {
        int ncap = g->origin_cap ? g->origin_cap * 2 : 1024;
        char **nn = (char **)realloc(g->origin_fn_names, (size_t)ncap * sizeof(char *));
        if (!nn) return;
        g->origin_fn_names = nn;
        int *nu = (int *)realloc(g->origin_fn_unit, (size_t)ncap * sizeof(int));
        if (!nu) return;
        g->origin_fn_unit = nu;
        g->origin_cap = ncap;
    }
    char *copy = (char *)malloc(nlen + 1);
    if (!copy) return;
    memcpy(copy, name, nlen);
    copy[nlen] = '\0';
    g->origin_fn_names[g->origin_count] = copy;
    g->origin_fn_unit[g->origin_count] = unit;
    g->origin_count++;
}

void zan_irgen_origin_free(zan_irgen_t *g) {
    for (int i = 0; i < g->origin_count; i++) free(g->origin_fn_names[i]);
    free(g->origin_fn_names);
    free(g->origin_fn_unit);
    for (int i = 0; i < g->origin_unit_count; i++) free(g->origin_units[i]);
    free(g->origin_units);
    free(g->origin_file_unit);
    g->origin_fn_names = NULL;
    g->origin_fn_unit = NULL;
    g->origin_units = NULL;
    g->origin_file_unit = NULL;
    g->origin_count = g->origin_cap = g->origin_unit_count = g->origin_file_cap = 0;
}
