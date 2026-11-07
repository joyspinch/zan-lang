static void symbol_add_member(zan_arena_t *arena, zan_symbol_t *parent, zan_symbol_t *child) {
    if (parent->member_count >= parent->member_cap) {
        int new_cap = parent->member_cap < 4 ? 4 : parent->member_cap * 2;
        zan_symbol_t **new_members = (zan_symbol_t **)zan_arena_alloc(
            arena, sizeof(zan_symbol_t *) * (size_t)new_cap);
        if (parent->members) {
            memcpy(new_members, parent->members,
                   sizeof(zan_symbol_t *) * (size_t)parent->member_count);
        }
        parent->members = new_members;
        parent->member_cap = new_cap;
    }
    child->parent = parent;
    parent->members[parent->member_count++] = child;
}

static uint32_t istr_hash(zan_istr_t name) {
    uint32_t h = 2166136261u;
    for (uint32_t i = 0; i < name.len; i++) {
        h ^= (unsigned char)name.str[i];
        h *= 16777619u;
    }
    return h;
}

static zan_scope_t *scope_new(zan_arena_t *arena, zan_scope_t *parent) {
    zan_scope_t *s = (zan_scope_t *)zan_arena_alloc(arena, sizeof(zan_scope_t));
    s->parent = parent;
    s->symbols = NULL;
    s->sym_count = 0;
    s->sym_cap = 0;
    s->buckets = NULL;
    s->bucket_count = 0;
    return s;
}

static void scope_index_grow(zan_arena_t *arena, zan_scope_t *scope) {
    int nc = scope->bucket_count < 16 ? 16 : scope->bucket_count * 2;
    zan_symbol_t **nb = (zan_symbol_t **)zan_arena_alloc(
        arena, sizeof(*nb) * (size_t)nc);
    memset(nb, 0, sizeof(*nb) * (size_t)nc);
    for (int i = 0; i < scope->sym_count; i++) {
        zan_symbol_t *sym = scope->symbols[i];
        zan_symbol_t **tail = &nb[sym->name_hash & (uint32_t)(nc - 1)];
        while (*tail) tail = &(*tail)->hash_next;
        *tail = sym;
        sym->hash_next = NULL;
    }
    scope->buckets = nb;
    scope->bucket_count = nc;
}

static void scope_add(zan_arena_t *arena, zan_scope_t *scope, zan_symbol_t *sym) {
    if (scope->sym_count >= scope->sym_cap) {
        int new_cap = scope->sym_cap < 8 ? 8 : scope->sym_cap * 2;
        zan_symbol_t **new_syms = (zan_symbol_t **)zan_arena_alloc(
            arena, sizeof(zan_symbol_t *) * (size_t)new_cap);
        if (scope->symbols) {
            memcpy(new_syms, scope->symbols,
                   sizeof(zan_symbol_t *) * (size_t)scope->sym_count);
        }
        scope->symbols = new_syms;
        scope->sym_cap = new_cap;
    }
    scope->symbols[scope->sym_count++] = sym;

    sym->name_hash = istr_hash(sym->name);
    if (scope->sym_count > scope->bucket_count)
        scope_index_grow(arena, scope);
    zan_symbol_t **tail =
        &scope->buckets[sym->name_hash & (uint32_t)(scope->bucket_count - 1)];
    while (*tail) tail = &(*tail)->hash_next;
    *tail = sym;
    sym->hash_next = NULL;
}

static zan_symbol_t *scope_find(zan_scope_t *scope, zan_istr_t name) {
    uint32_t h = istr_hash(name);
    for (zan_scope_t *s = scope; s; s = s->parent) {
        if (!s->bucket_count) continue;
        zan_symbol_t *sym = s->buckets[h & (uint32_t)(s->bucket_count - 1)];
        for (; sym; sym = sym->hash_next) {
            if (sym->name.len == name.len &&
                memcmp(sym->name.str, name.str, (size_t)name.len) == 0) {
                return sym;
            }
        }
    }
    return NULL;
}

static bool sym_is_type(zan_symbol_t *sym) {
    if (!sym) return false;
    return sym->kind == SYM_CLASS ||
           sym->kind == SYM_STRUCT ||
           sym->kind == SYM_INTERFACE ||
           sym->kind == SYM_ENUM ||
           sym->kind == SYM_TYPE_PARAM ||
           sym->kind == SYM_DELEGATE;
}

static zan_symbol_t *scope_find_type(zan_scope_t *scope, zan_istr_t name) {
    uint32_t h = istr_hash(name);
    for (zan_scope_t *s = scope; s; s = s->parent) {
        if (!s->bucket_count) continue;
        zan_symbol_t *sym = s->buckets[h & (uint32_t)(s->bucket_count - 1)];
        for (; sym; sym = sym->hash_next) {
            if (sym->name.len == name.len &&
                memcmp(sym->name.str, name.str, (size_t)name.len) == 0) {
                if (sym_is_type(sym)) {
                    return sym;
                }
            }
        }
    }
    return NULL;
}

void zan_binder_init(zan_binder_t *b, zan_arena_t *arena, zan_diag_t *diag) {
    memset(b, 0, sizeof(*b));
    b->arena = arena;
    b->diag = diag;
    b->current_scope = scope_new(arena, NULL);

    b->type_void   = make_type(arena, TYPE_VOID,   "void",   4);
    b->type_bool   = make_type(arena, TYPE_BOOL,   "bool",   4);
    b->type_byte   = make_type(arena, TYPE_BYTE,   "byte",   4);
    b->type_short  = make_type(arena, TYPE_SHORT,  "short",  5);
    b->type_int    = make_type(arena, TYPE_INT,    "int",    3);
    b->type_long   = make_type(arena, TYPE_LONG,   "long",   4);
    b->type_sbyte  = make_type(arena, TYPE_SBYTE,  "sbyte",  5);
    b->type_ushort = make_type(arena, TYPE_USHORT, "ushort", 6);
    b->type_uint   = make_type(arena, TYPE_UINT,   "uint",   4);
    b->type_ulong  = make_type(arena, TYPE_ULONG,  "ulong",  5);
    b->type_float  = make_type(arena, TYPE_FLOAT,  "float",  5);
    b->type_double = make_type(arena, TYPE_DOUBLE, "double", 6);
    b->type_decimal = make_type(arena, TYPE_DECIMAL, "decimal", 7);
    b->type_char   = make_type(arena, TYPE_CHAR,   "char",   4);
    b->type_string = make_type(arena, TYPE_STRING, "string", 6);
    b->type_object = make_type(arena, TYPE_OBJECT, "object", 6);
    b->type_nint   = make_type(arena, TYPE_NINT,   "nint",   4);
    b->type_error  = make_type(arena, TYPE_ERROR,  "<error>", 7);
    b->type_typeinfo = make_type(arena, TYPE_STRUCT, "TypeInfo", 8);
}

static bool binder_type_equiv(const zan_type_t *a, const zan_type_t *b) {
    if (a == b) return true;
    if (!a || !b) return false;
    if (a->kind != b->kind) return false;
    if (a->name.len != b->name.len ||
        memcmp(a->name.str, b->name.str, (size_t)a->name.len) != 0)
        return false;
    if (a->type_arg_count != b->type_arg_count) return false;
    for (int i = 0; i < a->type_arg_count; i++) {
        if (!binder_type_equiv(a->type_args[i], b->type_args[i])) return false;
    }
    return true;
}

static bool binder_params_equiv(zan_symbol_t *a, zan_symbol_t *b) {
    if (a->member_count != b->member_count) return false;
    for (int i = 0; i < a->member_count; i++) {
        zan_symbol_t *pa = a->members[i];
        zan_symbol_t *pb = b->members[i];
        if (!pa || !pb || pa->kind != SYM_PARAM || pb->kind != SYM_PARAM)
            return false;
        if (!binder_type_equiv(pa->type, pb->type)) return false;
    }
    return true;
}

static bool binder_type_params_equiv(zan_symbol_t *a, zan_symbol_t *b) {
    int atp = (a->decl && a->decl->kind == AST_METHOD_DECL)
        ? a->decl->method_decl.type_params.count : 0;
    int btp = (b->decl && b->decl->kind == AST_METHOD_DECL)
        ? b->decl->method_decl.type_params.count : 0;
    return atp == btp;
}


typedef struct {
    int member_idx;
    int next;
} member_name_node_t;

struct zan_member_name_index {
    int *buckets;
    int bucket_cap;
    member_name_node_t *nodes;
    int node_count;
    int node_cap;
};

static uint64_t member_name_hash(zan_istr_t name) {
    uint64_t h = 1469598103934665603ULL;
    for (uint32_t i = 0; i < name.len; i++) {
        h ^= (unsigned char)name.str[i];
        h *= 1099511628211ULL;
    }
    return h;
}

static void midx_buckets_rehash(zan_symbol_t *type,
                                struct zan_member_name_index *ix, int ncap) {
    int *grown = (int *)malloc((size_t)ncap * sizeof(*grown));
    if (!grown) {
        fprintf(stderr, "error: out of memory tracking member names\n");
        exit(1);
    }
    for (int i = 0; i < ncap; i++) grown[i] = -1;
    for (int n = 0; n < ix->node_count; n++) {
        zan_symbol_t *m = type->members[ix->nodes[n].member_idx];
        size_t j = (size_t)member_name_hash(m->name) & (size_t)(ncap - 1);
        while (grown[j] >= 0) j = (j + 1) & (size_t)(ncap - 1);
        grown[j] = n;
    }
    free(ix->buckets);
    ix->buckets = grown;
    ix->bucket_cap = ncap;
}

static void midx_catch_up(zan_symbol_t *type,
                          struct zan_member_name_index *ix) {
    while (ix->node_count < type->member_count) {
        if ((ix->node_count + 1) * 2 > ix->bucket_cap)
            midx_buckets_rehash(type, ix, ix->bucket_cap * 2);
        int n = ix->node_count;
        if (n == ix->node_cap) {
            int ncap = ix->node_cap ? ix->node_cap * 2 : 16;
            member_name_node_t *grown = (member_name_node_t *)realloc(
                ix->nodes, (size_t)ncap * sizeof(*grown));
            if (!grown) {
                fprintf(stderr, "error: out of memory tracking member names\n");
                exit(1);
            }
            ix->nodes = grown;
            ix->node_cap = ncap;
        }
        zan_symbol_t *m = type->members[n];
        size_t j = (size_t)member_name_hash(m->name) & (size_t)(ix->bucket_cap - 1);
        ix->nodes[n].member_idx = n;
        ix->nodes[n].next = ix->buckets[j];
        ix->buckets[j] = n;
        ix->node_count++;
    }
}

static uint64_t member_type_ptr_hash(const void *p) {
    uint64_t h = (uint64_t)(uintptr_t)p;
    h ^= h >> 33;
    h *= 0xff51afd7ed558ccdULL;
    h ^= h >> 29;
    return h;
}

static struct zan_member_name_index *midx_for(zan_binder_t *b,
                                              zan_symbol_t *type_sym) {
    if (b->member_idx_cap == 0) {
        int ncap = 64;
        b->member_idx = (struct zan_member_idx_slot *)calloc(
            (size_t)ncap, sizeof(*b->member_idx));
        if (!b->member_idx) {
            fprintf(stderr, "error: out of memory tracking member names\n");
            exit(1);
        }
        b->member_idx_cap = ncap;
        b->member_idx_count = 0;
    } else if ((b->member_idx_count + 1) * 2 >= b->member_idx_cap) {
        int ncap = b->member_idx_cap * 2;
        struct zan_member_idx_slot *grown =
            (struct zan_member_idx_slot *)calloc((size_t)ncap,
                                                 sizeof(*grown));
        if (!grown) {
            fprintf(stderr, "error: out of memory tracking member names\n");
            exit(1);
        }
        for (int i = 0; i < b->member_idx_cap; i++) {
            if (!b->member_idx[i].type) continue;
            size_t j = (size_t)member_type_ptr_hash(b->member_idx[i].type)
                       & (size_t)(ncap - 1);
            while (grown[j].type) j = (j + 1) & (size_t)(ncap - 1);
            grown[j] = b->member_idx[i];
        }
        free(b->member_idx);
        b->member_idx = grown;
        b->member_idx_cap = ncap;
    }
    size_t j = (size_t)member_type_ptr_hash(type_sym)
               & (size_t)(b->member_idx_cap - 1);
    while (b->member_idx[j].type) {
        if (b->member_idx[j].type == type_sym) return b->member_idx[j].idx;
        j = (j + 1) & (size_t)(b->member_idx_cap - 1);
    }
    struct zan_member_name_index *ix =
        (struct zan_member_name_index *)calloc(1, sizeof(*ix));
    if (!ix) {
        fprintf(stderr, "error: out of memory tracking member names\n");
        exit(1);
    }
    ix->bucket_cap = 16;
    ix->buckets = (int *)malloc((size_t)ix->bucket_cap * sizeof(*ix->buckets));
    if (!ix->buckets) {
        fprintf(stderr, "error: out of memory tracking member names\n");
        exit(1);
    }
    for (int i = 0; i < ix->bucket_cap; i++) ix->buckets[i] = -1;
    b->member_idx[j].type = type_sym;
    b->member_idx[j].idx = ix;
    b->member_idx_count++;
    midx_catch_up(type_sym, ix);
    return ix;
}

static bool member_clash_one(zan_binder_t *b, zan_symbol_t *type_sym,
                             zan_symbol_t *added, zan_symbol_t *m) {
    bool m_is_code = m->kind == SYM_METHOD;
    bool a_is_code = added->kind == SYM_METHOD;
    bool m_is_data = m->kind == SYM_FIELD || m->kind == SYM_PROPERTY;
    bool a_is_data = added->kind == SYM_FIELD || added->kind == SYM_PROPERTY;
    if ((m_is_code && a_is_data) || (m_is_data && a_is_code)) {
        zan_symbol_t *data = a_is_data ? added : m;
        zan_diag_emit_code(b->diag, DIAG_ERROR, added->decl->loc, 1000,
                      "'%.*s' is declared both as a %s and as a method in "
                      "'%.*s'; give one of them a different name",
                      added->name.len, added->name.str,
                      data->kind == SYM_PROPERTY ? "property" : "field",
                      type_sym->name.len, type_sym->name.str);
        return true;
    }
    if (m_is_data && a_is_data) {
        zan_ast_node_t *ad = added->decl;
        zan_ast_node_t *md = m->decl;
        if (ad && ad->kind == AST_PROPERTY_DECL &&
            ad->field_decl.indexer_params &&
            md && md->kind == AST_PROPERTY_DECL &&
            md->field_decl.indexer_params) {
            return false;
        }
        zan_diag_emit_code(b->diag, DIAG_ERROR, added->decl->loc, 1001,
                      "duplicate '%.*s' in '%.*s': a %s with this name "
                      "is already declared",
                      added->name.len, added->name.str,
                      type_sym->name.len, type_sym->name.str,
                      added->kind == SYM_PROPERTY ? "property" : "field");
        return true;
    }
    if (m->kind == SYM_ENUM_MEMBER && added->kind == SYM_ENUM_MEMBER) {
        zan_diag_emit_code(b->diag, DIAG_ERROR, added->decl->loc, 1002,
                      "duplicate enum member '%.*s' in '%.*s'",
                      added->name.len, added->name.str,
                      type_sym->name.len, type_sym->name.str);
        return true;
    }
    if ((m_is_code && a_is_code) &&
        binder_params_equiv(m, added) &&
        binder_type_params_equiv(m, added)) {
        zan_diag_emit_code(b->diag, DIAG_ERROR, added->decl->loc, 1003,
                      "duplicate method '%.*s' in '%.*s': a method with "
                      "the same parameter types is already declared",
                      added->name.len, added->name.str,
                      type_sym->name.len, type_sym->name.str);
        return true;
    }
    if (m->kind == SYM_CONSTRUCTOR && added->kind == SYM_CONSTRUCTOR &&
        binder_params_equiv(m, added)) {
        zan_diag_emit_code(b->diag, DIAG_ERROR, added->decl->loc, 1004,
                      "duplicate constructor in '%.*s': a constructor "
                      "with the same parameter types is already declared",
                      type_sym->name.len, type_sym->name.str);
        return true;
    }
    return false;
}

static void clash_scan_full(zan_binder_t *b, zan_symbol_t *type_sym,
                            zan_symbol_t *added) {
    for (int i = 0; i < type_sym->member_count; i++) {
        zan_symbol_t *m = type_sym->members[i];
        if (m == added) continue;
        if (m->name.len != added->name.len ||
            memcmp(m->name.str, added->name.str, (size_t)added->name.len) != 0)
            continue;
        if (member_clash_one(b, type_sym, added, m)) return;
    }
}

static void check_member_name_clash(zan_binder_t *b, zan_symbol_t *type_sym,
                                    zan_symbol_t *added) {
    struct zan_member_name_index *ix = midx_for(b, type_sym);
    if (!ix) { clash_scan_full(b, type_sym, added); return; }
    midx_catch_up(type_sym, ix);
    int added_idx = type_sym->member_count - 1;
    if (added_idx < 0 || type_sym->members[added_idx] != added) {
        clash_scan_full(b, type_sym, added);
        return;
    }
    int matches[64];
    int nmatch = 0;
    bool overflow = false;
    size_t j = (size_t)member_name_hash(added->name)
               & (size_t)(ix->bucket_cap - 1);
    for (int n = ix->buckets[j]; n >= 0; n = ix->nodes[n].next) {
        int mi = ix->nodes[n].member_idx;
        if (mi == added_idx) continue;
        zan_symbol_t *m = type_sym->members[mi];
        if (m->name.len == added->name.len &&
            memcmp(m->name.str, added->name.str, (size_t)added->name.len) == 0) {
            if (nmatch < (int)(sizeof(matches) / sizeof(matches[0])))
                matches[nmatch++] = mi;
            else { overflow = true; break; }
        }
    }
    if (overflow) { clash_scan_full(b, type_sym, added); return; }
    for (int i = 1; i < nmatch; i++) {
        int v = matches[i];
        int k = i - 1;
        while (k >= 0 && matches[k] > v) { matches[k + 1] = matches[k]; k--; }
        matches[k + 1] = v;
    }
    for (int i = 0; i < nmatch; i++) {
        if (member_clash_one(b, type_sym, added, type_sym->members[matches[i]]))
            return;
    }
}


static bool istr_eq(zan_istr_t a, const char *b, uint32_t len) {
    return a.len == len && memcmp(a.str, b, (size_t)len) == 0;
}

zan_type_t *zan_binder_make_list_type(zan_binder_t *b, zan_type_t *elem) {
    zan_type_t *t = make_type(b->arena, TYPE_CLASS, "List", 4);
    t->type_args =
        (zan_type_t **)zan_arena_alloc(b->arena, sizeof(zan_type_t *));
    t->type_args[0] = elem;
    t->type_arg_count = 1;
    return t;
}

zan_type_t *zan_binder_make_span_type(zan_binder_t *b, zan_type_t *elem) {
    zan_type_t *t = make_type(b->arena, TYPE_STRUCT, "Span", 4);
    t->type_args =
        (zan_type_t **)zan_arena_alloc(b->arena, sizeof(zan_type_t *));
    t->type_args[0] = elem;
    t->type_arg_count = 1;
    return t;
}

zan_type_t *zan_binder_make_readonly_span_type(zan_binder_t *b, zan_type_t *elem) {
    zan_type_t *t = make_type(b->arena, TYPE_STRUCT, "ReadOnlySpan", 12);
    t->type_args =
        (zan_type_t **)zan_arena_alloc(b->arena, sizeof(zan_type_t *));
    t->type_args[0] = elem;
    t->type_arg_count = 1;
    return t;
}

zan_type_t *zan_binder_make_array_type(zan_binder_t *b, zan_type_t *elem) {
    zan_type_t *t = make_type(b->arena, TYPE_ARRAY, elem->name.str, elem->name.len);
    t->element_type = elem;
    return t;
}

zan_type_t *zan_binder_make_grouping_type(zan_binder_t *b, zan_type_t *elem) {
    zan_type_t *t = make_type(b->arena, TYPE_CLASS, "Grouping", 8);
    t->type_args =
        (zan_type_t **)zan_arena_alloc(b->arena, sizeof(zan_type_t *));
    t->type_args[0] = elem;
    t->type_arg_count = 1;
    /* 绑定标准库 System.Linq.Grouping 泛型分组类型符号 */
    zan_istr_t nm = { "Grouping", 8 };
    zan_symbol_t *sym = zan_binder_lookup(b, nm);
    if (sym && sym->kind == SYM_CLASS) t->sym = sym;
    return t;
}

/* Task<T>：内建任务句柄类型。T 为结果类型时是"带结果"的任务，
 * 无类型实参的 Task 是"无结果"任务（只有等待语义）。 */
zan_type_t *zan_binder_make_task_type(zan_binder_t *b, zan_type_t *elem) {
    zan_type_t *t = make_type(b->arena, TYPE_TASK, "Task", 4);
    if (!elem) return t;
    t->type_args =
        (zan_type_t **)zan_arena_alloc(b->arena, sizeof(zan_type_t *));
    t->type_args[0] = elem;
    t->type_arg_count = 1;
    return t;
}

/* ---------- 面向用户的类型名渲染（A463） ---------- */

/* 往 buf 尾部追加一段文本。装不下就截断，但始终给结尾的 '\0' 留位置。 */
static size_t display_append(char *buf, size_t cap, size_t used,
                             const char *s, size_t n) {
    if (cap == 0) return 0;
    for (size_t i = 0; i < n; i++) {
        if (used + 1 >= cap) break;
        buf[used++] = s[i];
    }
    buf[used] = '\0';
    return used;
}

/* 递归渲染：可空 → `T?`，数组 → `T[]`（多维 `T[,]`），泛型 → `Name<A,B>`。
 * 名字本身取 t->name——那才是类型身份（`List`、`Task`、`Box`）。 */
static size_t type_display_rec(const zan_type_t *t, char *buf, size_t cap,
                               size_t used) {
    if (!t) return used;
    if (t->kind == TYPE_NULLABLE) {
        if (t->element_type)
            used = type_display_rec(t->element_type, buf, cap, used);
        else if (t->name.str)
            used = display_append(buf, cap, used, t->name.str, t->name.len);
        return display_append(buf, cap, used, "?", 1);
    }
    if (t->kind == TYPE_ARRAY) {
        if (t->element_type)
            used = type_display_rec(t->element_type, buf, cap, used);
        else if (t->name.str)
            used = display_append(buf, cap, used, t->name.str, t->name.len);
        used = display_append(buf, cap, used, "[", 1);
        int rank = t->array_rank > 1 ? t->array_rank : 1;
        for (int r = 1; r < rank; r++)
            used = display_append(buf, cap, used, ",", 1);
        used = display_append(buf, cap, used, "]", 1);
        if (t->is_nullable) {
            used = display_append(buf, cap, used, "?", 1);
        }
        return used;
    }
    if (t->name.str)
        used = display_append(buf, cap, used, t->name.str, t->name.len);
    if (t->type_arg_count > 0 && t->type_args) {
        used = display_append(buf, cap, used, "<", 1);
        for (int i = 0; i < t->type_arg_count; i++) {
            if (i > 0) used = display_append(buf, cap, used, ",", 1);
            used = type_display_rec(t->type_args[i], buf, cap, used);
        }
        used = display_append(buf, cap, used, ">", 1);
    }
    if (t->is_nullable) {
        used = display_append(buf, cap, used, "?", 1);
    }
    return used;
}

size_t zan_type_display(const zan_type_t *t, char *buf, size_t cap) {
    if (!buf || cap == 0) return 0;
    buf[0] = '\0';
    if (!t) {
        display_append(buf, cap, 0, "?", 1);
        return 1;
    }
    return type_display_rec(t, buf, cap, 0);
}

static bool tuple_sig_type(zan_type_t *t, char *buf, size_t cap) {
    if (!t || cap == 0) return true;
    size_t used = strlen(buf);
    if (used + 1 >= cap) return false;
    if (t->kind == TYPE_ARRAY && t->element_type) {
        bool ok = tuple_sig_type(t->element_type, buf, cap);
        int rank = t->array_rank > 1 ? t->array_rank : 1;
        /* 房间按 rank 精确预留：曾只留 +4，rank>=4 时逗号被内层静默跳过
         * 仍返 ok=true——两个不同 rank 的数组型签名可碰撞 */
        if (strlen(buf) + 3 + (size_t)(rank - 1) * 2 < cap) {
            strcat(buf, "[");
            for (int r = 1; r < rank; r++) strcat(buf, ",");
            strcat(buf, "]");
        } else {
            ok = false;
        }
        return ok;
    }
    if (t->kind == TYPE_NULLABLE && t->element_type) {
        bool ok = tuple_sig_type(t->element_type, buf, cap);
        if (strlen(buf) + 1 < cap) strcat(buf, "?");
        else ok = false;
        return ok;
    }
    const char *nm = t->name.str ? t->name.str : "?";
    int nl = (int)t->name.len;
    if (nl <= 0) nl = (int)strlen(nm);
    if (nl <= 0) nl = 1;
    if (used + (size_t)nl + 8 >= cap) return false;
    strncat(buf, nm, (size_t)nl);
    bool ok = true;
    if (t->type_arg_count > 0) {
        if (strlen(buf) + 2 < cap) strcat(buf, "<");
        else ok = false;
        for (int i = 0; i < t->type_arg_count; i++) {
            if (i > 0) {
                if (strlen(buf) + 2 < cap) strcat(buf, ",");
                else ok = false;
            }
            if (!tuple_sig_type(t->type_args[i], buf, cap)) ok = false;
        }
        if (strlen(buf) + 1 < cap) strcat(buf, ">");
        else ok = false;
    }
    return ok;
}

static uint64_t tuple_type_hash(zan_type_t *t) {
    uint64_t h = 0xcbf29ce484222325ULL;
    while (t) {
        unsigned char kind = (unsigned char)t->kind;
        h ^= kind; h *= 0x100000001b3ULL;
        uint16_t nlen = (uint16_t)t->name.len;
        const unsigned char *nb = (const unsigned char *)&nlen;
        for (size_t i = 0; i < sizeof(nlen); i++) { h ^= nb[i]; h *= 0x100000001b3ULL; }
        if (t->name.str)
            for (uint32_t i = 0; i < t->name.len; i++) {
                h ^= (unsigned char)t->name.str[i]; h *= 0x100000001b3ULL;
            }
        for (int i = 0; i < t->type_arg_count; i++) {
            uint64_t ah = tuple_type_hash(t->type_args[i]);
            const unsigned char *ab = (const unsigned char *)&ah;
            for (size_t k = 0; k < sizeof(ah); k++) { h ^= ab[k]; h *= 0x100000001b3ULL; }
        }
        t = t->element_type;
    }
    unsigned char end = ';';
    h ^= end; h *= 0x100000001b3ULL;
    return h;
}

static uint64_t tuple_sig_hash(const char *s, int len) {
    uint64_t h = 0xcbf29ce484222325ULL;
    for (int i = 0; i < len; i++) {
        h ^= (unsigned char)s[i];
        h *= 0x100000001b3ULL;
    }
    return h;
}

/* C# tuples `(T1, T2, */
zan_type_t *zan_binder_make_tuple_type(zan_binder_t *b, zan_type_t **elems,
                                       int count) {
    if (!b || !elems || count <= 0) return b ? b->type_error : NULL;
    char sig[4096];
    snprintf(sig, sizeof sig, "__tuple%d:", count);
    bool complete = true;
    for (int i = 0; i < count; i++) {
        if (i > 0) {
            if (strlen(sig) < sizeof sig - 2) strcat(sig, ",");
            else complete = false;
        }
        if (!tuple_sig_type(elems[i], sig, sizeof sig)) complete = false;
    }
    if (!complete) {
        uint64_t h = 0xcbf29ce484222325ULL;
        for (int i = 0; i < count; i++) {
            uint64_t eh = tuple_type_hash(elems[i]);
            const unsigned char *eb = (const unsigned char *)&eh;
            for (size_t k = 0; k < sizeof(eh); k++) { h ^= eb[k]; h *= 0x100000001b3ULL; }
            unsigned char sep = ',';
            h ^= sep; h *= 0x100000001b3ULL;
        }
        char tail[24];
        snprintf(tail, sizeof tail, "#%016llx", (unsigned long long)h);
        size_t l = strlen(sig);
        size_t room = sizeof sig - strlen(tail) - 1;
        if (l > room) { sig[room] = '\0'; l = room; }
        memcpy(sig + l, tail, strlen(tail) + 1);
    }
    zan_istr_t sig_istr = { (char *)sig, (uint32_t)strlen(sig) };

    if (b->tuple_hash_cap) {
        size_t j = (size_t)tuple_sig_hash(sig_istr.str, (int)sig_istr.len)
                   & (size_t)(b->tuple_hash_cap - 1);
        while (b->tuple_hash[j]) {
            zan_type_t *t = b->tuple_hash[j];
            if (t->name.len == sig_istr.len &&
                memcmp(t->name.str, sig_istr.str, (size_t)sig_istr.len) == 0)
                return t;
            j = (j + 1) & (size_t)(b->tuple_hash_cap - 1);
        }
    }

    char *name = zan_arena_strdup(b->arena, sig, (int)strlen(sig));
    zan_type_t *t = make_type(b->arena, TYPE_STRUCT, name, (int)strlen(name));
    zan_symbol_t *sym = make_symbol(b->arena, SYM_STRUCT,
        (zan_istr_t){ name, (uint32_t)strlen(name) }, t, NULL, MOD_PUBLIC);
    t->sym = sym;
    for (int i = 0; i < count; i++) {
        char fname[16];
        if (count > 1) {
            snprintf(fname, sizeof fname, "Item%d", i + 1);
        } else {
            snprintf(fname, sizeof fname, "Item1");
        }
        zan_symbol_t *fs = make_symbol(b->arena, SYM_FIELD,
            (zan_istr_t){ zan_arena_strdup(b->arena, fname, (int)strlen(fname)),
                          (uint32_t)strlen(fname) },
            elems[i], NULL, MOD_PUBLIC);
        symbol_add_member(b->arena, sym, fs);
    }

    if (b->tuple_type_count >= b->tuple_type_cap) {
        int new_cap = b->tuple_type_cap == 0 ? 8 : b->tuple_type_cap * 2;
        zan_type_t **grown = (zan_type_t **)zan_arena_alloc(
            b->arena, sizeof(zan_type_t *) * (size_t)new_cap);
        if (b->tuple_types) {
            memcpy(grown, b->tuple_types,
                   sizeof(zan_type_t *) * (size_t)b->tuple_type_count);
        }
        b->tuple_types = grown;
        b->tuple_type_cap = new_cap;
    }
    if ((b->tuple_type_count + 1) * 2 >= b->tuple_hash_cap) {
        int ncap = b->tuple_hash_cap ? b->tuple_hash_cap * 2 : 64;
        zan_type_t **grown = (zan_type_t **)zan_arena_alloc(
            b->arena, sizeof(zan_type_t *) * (size_t)ncap);
        memset(grown, 0, sizeof(zan_type_t *) * (size_t)ncap);
        for (int i = 0; i < b->tuple_type_count; i++) {
            zan_type_t *e = b->tuple_types[i];
            size_t j = (size_t)tuple_sig_hash(e->name.str, (int)e->name.len)
                       & (size_t)(ncap - 1);
            while (grown[j]) j = (j + 1) & (size_t)(ncap - 1);
            grown[j] = e;
        }
        b->tuple_hash = grown;
        b->tuple_hash_cap = ncap;
    }
    b->tuple_types[b->tuple_type_count++] = t;
    size_t j = (size_t)tuple_sig_hash(sig_istr.str, (int)sig_istr.len)
               & (size_t)(b->tuple_hash_cap - 1);
    while (b->tuple_hash[j]) j = (j + 1) & (size_t)(b->tuple_hash_cap - 1);
    b->tuple_hash[j] = t;
    return t;
}

zan_type_t *zan_binder_subst_named(zan_binder_t *b, zan_type_t *t,
                                   zan_ast_list_t *tps, zan_type_t **args) {
    if (!t) return t;
    if (t->kind == TYPE_TYPE_PARAM) {
        for (int i = 0; i < tps->count; i++) {
            zan_istr_t tn = tps->items[i]->ident.name;
            if (args[i] && tn.len == t->name.len &&
                memcmp(tn.str, t->name.str, (size_t)t->name.len) == 0)
                return args[i];
        }
        return t;
    }
    bool mentions = false;
    if ((t->kind == TYPE_ARRAY || t->kind == TYPE_NULLABLE) && t->element_type)
        mentions = true;
    if (t->kind == TYPE_DELEGATE) mentions = true;
    if (t->type_arg_count > 0) mentions = true;
    if (!mentions) return t;
    zan_type_t *nt = (zan_type_t *)zan_arena_alloc(b->arena, sizeof(zan_type_t));
    *nt = *t;
    if (t->kind == TYPE_ARRAY || t->kind == TYPE_NULLABLE) {
        nt->element_type = zan_binder_subst_named(b, t->element_type, tps, args);
        return nt;
    }
    if (t->kind == TYPE_DELEGATE) {
        nt->delegate_ret_type = zan_binder_subst_named(b, t->delegate_ret_type, tps, args);
        if (t->delegate_param_count > 0) {
            nt->delegate_param_types = (zan_type_t **)zan_arena_alloc(
                b->arena, sizeof(zan_type_t *) * (size_t)t->delegate_param_count);
            for (int i = 0; i < t->delegate_param_count; i++)
                nt->delegate_param_types[i] =
                    zan_binder_subst_named(b, t->delegate_param_types[i], tps, args);
        }
    }
    if (t->type_arg_count > 0) {
        nt->type_args = (zan_type_t **)zan_arena_alloc(
            b->arena, sizeof(zan_type_t *) * (size_t)t->type_arg_count);
        for (int i = 0; i < t->type_arg_count; i++)
            nt->type_args[i] = zan_binder_subst_named(b, t->type_args[i], tps, args);
    }
    return nt;
}

static bool type_is_value_kind(zan_type_t *t) {
    if (!t) return false;
    switch (t->kind) {
    case TYPE_BOOL: case TYPE_BYTE: case TYPE_SBYTE:
    case TYPE_SHORT: case TYPE_USHORT: case TYPE_INT: case TYPE_UINT:
    case TYPE_LONG: case TYPE_ULONG: case TYPE_NINT:
    case TYPE_FLOAT: case TYPE_DOUBLE:
    case TYPE_CHAR: case TYPE_ENUM: case TYPE_STRUCT:
        return true;
    default:
        return false;
    }
}

/* null 字面量实参 → 形参类型的匹配分。<0 表示不兼容（null 绑不上去）。
 *
 * 踩过的坑：`Arg.Text(null)`——Text 有 int/long/double/bool/string 五个重载——
 * 以前会走到 `Text(int)`：null 被当成 0 传下去，调用点看不出任何异常，
 * 断言却永远对不上。根因是**两侧各自分解一次**：checker 的打分把 null 的
 * 静态类型（object）当成"可赋值给引用类型"，而 irgen 的 `concrete_arg_score`
 * 对 null 实参推断不出类型、一律给 0 分，于是**第一个**重载（值类型）被选中。
 *
 * 现在两侧共用这一条规则：
 *   - null 只能绑到接得住 null 的形参（引用类型 / 可空类型），
 *   - object 最不具体（1 分），具体引用类型与可空类型同分（3 分），
 *   - 值类型形参接不住 null（-1，判为不兼容）。
 */
int zan_null_arg_score(zan_type_t *pt) {
    if (!pt) return 0;
    switch (pt->kind) {
    case TYPE_OBJECT:
        return 1;
    case TYPE_NULLABLE:
    case TYPE_STRING: case TYPE_CLASS: case TYPE_INTERFACE: case TYPE_ARRAY:
    case TYPE_DELEGATE: case TYPE_TASK:
        return 3;
    case TYPE_TYPE_PARAM:
        return 0;   /* 泛型形参：既可能装值也可能装引用，不在此处判决 */
    default:
        return -1;  /* 值类型形参接不住 null */
    }
}

zan_type_t *zan_binder_make_nullable_type(zan_binder_t *b, zan_type_t *elem) {
    if (!elem || elem == b->type_error || elem->kind == TYPE_NULLABLE || elem->is_nullable)
        return elem;
    if (type_is_value_kind(elem)) {
        zan_type_t *t = make_type(b->arena, TYPE_NULLABLE, elem->name.str, elem->name.len);
        t->element_type = elem;
        return t;
    } else {
        zan_type_t *t = make_type(b->arena, elem->kind, elem->name.str, elem->name.len);
        *t = *elem;
        t->is_nullable = 1;
        return t;
    }
}

static inline int binder_decl_type_param_count(const zan_ast_node_t *d) {
    if (!d) return 0;
    if (d->kind == AST_DELEGATE_DECL) return d->method_decl.type_params.count;
    return d->type_decl.type_params.count;
}

zan_type_t *zan_binder_resolve_type(zan_binder_t *b, zan_ast_node_t *type_ref) {
    if (!type_ref) return b->type_error;
    if (type_ref->kind == AST_TUPLE_TYPE) {
        int n = type_ref->tuple_type.elems.count;
        zan_type_t **elems = (zan_type_t **)zan_arena_alloc(
            b->arena, sizeof(zan_type_t *) * (size_t)(n > 0 ? n : 1));
        for (int i = 0; i < n; i++)
            elems[i] = zan_binder_resolve_type(b, type_ref->tuple_type.elems.items[i]);
        return zan_binder_make_tuple_type(b, elems, n);
    }
    if (type_ref->kind != AST_TYPE_REF) {
        return b->type_error;
    }

    if (b->binding_done && type_ref->type_ref.rt_type &&
        type_ref->type_ref.rt_scope == (void *)b->current_scope)
        return (zan_type_t *)type_ref->type_ref.rt_type;

    zan_istr_t name = type_ref->type_ref.name;

    zan_type_t *base = NULL;

    if (istr_eq(name, "void",   4)) base = b->type_void;
    else if (istr_eq(name, "bool",   4)) base = b->type_bool;
    else if (istr_eq(name, "byte",   4)) base = b->type_byte;
    else if (istr_eq(name, "short",  5)) base = b->type_short;
    else if (istr_eq(name, "int",    3)) base = b->type_int;
    else if (istr_eq(name, "long",   4)) base = b->type_long;
    else if (istr_eq(name, "sbyte",  5)) base = b->type_sbyte;
    else if (istr_eq(name, "ushort", 6)) base = b->type_ushort;
    else if (istr_eq(name, "uint",   4)) base = b->type_uint;
    else if (istr_eq(name, "ulong",  5)) base = b->type_ulong;
    else if (istr_eq(name, "float",  5)) base = b->type_float;
    else if (istr_eq(name, "double", 6)) base = b->type_double;
    else if (istr_eq(name, "decimal", 7)) base = b->type_decimal;
    else if (istr_eq(name, "char",   4)) base = b->type_char;
    else if (istr_eq(name, "string", 6)) base = b->type_string;
    else if (istr_eq(name, "object", 6)) base = b->type_object;
    else if (istr_eq(name, "nint",   4)) base = b->type_nint;
    else if (istr_eq(name, "List", 4)) base = make_type(b->arena, TYPE_CLASS, "List", 4);
    else if (istr_eq(name, "Dict", 4) || istr_eq(name, "Dictionary", 10))
        base = make_type(b->arena, TYPE_CLASS, "Dict", 4);
    else if (istr_eq(name, "StringBuilder", 13))
        base = make_type(b->arena, TYPE_CLASS, "StringBuilder", 13);
    else if (istr_eq(name, "Span", 4))
        base = make_type(b->arena, TYPE_STRUCT, "Span", 4);
    else if (istr_eq(name, "ReadOnlySpan", 12))
        base = make_type(b->arena, TYPE_STRUCT, "ReadOnlySpan", 12);
    else if (istr_eq(name, "TypeInfo", 8)) base = b->type_typeinfo;
    else if (istr_eq(name, "Task", 4))
        base = make_type(b->arena, TYPE_TASK, "Task", 4);
    /*  */
    else if (istr_eq(name, "Func", 4) || istr_eq(name, "Action", 6)) {
        /* 用户声明的同名 delegate 遮蔽内建委托形状（C# 语义，B-ID53）；
         * 其余形状仍短路到内建。 */
        zan_symbol_t *sym = scope_find_type(b->current_scope, name);
        if (sym && sym->kind == SYM_DELEGATE && sym->type)
            base = sym->type;
        else
            base = make_type(b->arena, TYPE_DELEGATE, name.str, name.len);
    }
    else {
        zan_symbol_t *sym = scope_find_type(b->current_scope, name);
        if (sym) base = sym->type;
    }

    if (!base) {
        zan_diag_emit_code(b->diag, DIAG_ERROR, type_ref->loc, 1005,
                      "undefined type '%.*s'", name.len, name.str);
        return b->type_error;
    }

    if (type_ref->type_ref.type_args.count > 0 && base->kind == TYPE_DELEGATE &&
        base->sym && base->sym->decl &&
        base->sym->decl->method_decl.type_params.count ==
            type_ref->type_ref.type_args.count) {
        int nargs = type_ref->type_ref.type_args.count;
        zan_type_t **args = (zan_type_t **)zan_arena_alloc(
            b->arena, sizeof(zan_type_t *) * (size_t)nargs);
        for (int i = 0; i < nargs; i++)
            args[i] = zan_binder_resolve_type(b, type_ref->type_ref.type_args.items[i]);
        zan_type_t *inst = make_type(b->arena, TYPE_DELEGATE,
                                     base->name.str, base->name.len);
        *inst = *base;
        inst->type_args = args;
        inst->type_arg_count = nargs;
        zan_ast_list_t *dtps = &base->sym->decl->method_decl.type_params;
        inst->delegate_ret_type =
            zan_binder_subst_named(b, base->delegate_ret_type, dtps, args);
        if (base->delegate_param_count > 0) {
            inst->delegate_param_types = (zan_type_t **)zan_arena_alloc(
                b->arena, sizeof(zan_type_t *) * (size_t)base->delegate_param_count);
            for (int i = 0; i < base->delegate_param_count; i++)
                inst->delegate_param_types[i] =
                    zan_binder_subst_named(b, base->delegate_param_types[i], dtps, args);
        }
        base = inst;
    } else
    if (type_ref->type_ref.type_args.count > 0) {
        bool builtin_generic = istr_eq(name, "List", 4) || istr_eq(name, "Dict", 4) ||
                               istr_eq(name, "Dictionary", 10) || istr_eq(name, "Span", 4) ||
                               istr_eq(name, "ReadOnlySpan", 12) ||
                               istr_eq(name, "Task", 4);
        bool user_generic = !builtin_generic &&
            (base->kind == TYPE_CLASS || base->kind == TYPE_STRUCT ||
             base->kind == TYPE_INTERFACE);
        if (builtin_generic || user_generic) {
            int nargs = type_ref->type_ref.type_args.count;
            int expected_tps = -1;
            if (istr_eq(name, "List", 4) || istr_eq(name, "Span", 4) || istr_eq(name, "ReadOnlySpan", 12) || istr_eq(name, "Task", 4))
                expected_tps = 1;
            else if (istr_eq(name, "Dict", 4) || istr_eq(name, "Dictionary", 10))
                expected_tps = 2;
            else if (user_generic && base->sym && base->sym->decl)
                expected_tps = binder_decl_type_param_count(base->sym->decl);

            if (expected_tps >= 0 && nargs != expected_tps) {
                zan_diag_emit_code(b->diag, DIAG_ERROR, type_ref->loc, 1006,
                              "using the generic type '%.*s' requires %d type arguments (got %d)",
                              (int)name.len, name.str, expected_tps, nargs);
                return b->type_error;
            }

            zan_type_t *inst = base;
            if (user_generic) {
                inst = make_type(b->arena, base->kind, base->name.str, base->name.len);
                inst->sym = base->sym;
                inst->base_type = base->base_type;
                inst->element_type = base->element_type;
            }
            inst->type_args = (zan_type_t **)zan_arena_alloc(
                b->arena, sizeof(zan_type_t *) * (size_t)nargs);
            inst->type_arg_count = nargs;
            for (int i = 0; i < nargs; i++) {
                inst->type_args[i] = zan_binder_resolve_type(
                    b, type_ref->type_ref.type_args.items[i]);
            }
            /* 实例化类型必须带着按实参替换后的接口列表走：Box<T> : IBox<T>
             * 实例化成 Box<Dog> 后 interfaces 应是 IBox<Dog>，而不是随
             * sym 一起指向开泛 IBox<T>（或干脆为空）。否则 derives 走查
             * 进不了接口方差分支，类→变型接口的协变/逆变转换
             * （Box<Dog> -> IBox<Animal>）被误拒 E2024（B-ID63）。 */
            if (user_generic && base->interface_count > 0 && base->sym &&
                base->sym->decl) {
                zan_ast_list_t *dtps = NULL;
                if (base->sym->decl->kind == AST_CLASS_DECL ||
                    base->sym->decl->kind == AST_STRUCT_DECL ||
                    base->sym->decl->kind == AST_INTERFACE_DECL)
                    dtps = &base->sym->decl->type_decl.type_params;
                if (dtps && dtps->count == nargs) {
                    inst->interfaces = (zan_type_t **)zan_arena_alloc(
                        b->arena,
                        sizeof(zan_type_t *) * (size_t)base->interface_count);
                    inst->interface_count = base->interface_count;
                    for (int i = 0; i < base->interface_count; i++)
                        inst->interfaces[i] = zan_binder_subst_named(
                            b, base->interfaces[i], dtps, inst->type_args);
                }
            }
            base = inst;
        } else if (base->sym && base->sym->decl) {
            int expected_tps = binder_decl_type_param_count(base->sym->decl);
            /* Action/Func 是可遮蔽的内建 delegate：用户声明的同名 delegate
             * 只在类型参数数量匹配时生效；数量不匹配时跳过错误，让
             * 后面的内建合成路径（Func/Action synthesize）构造正确的
             * 泛型类型覆盖 base。——与 nsresolve 的 arity filter 配合
             * 实现跨命名空间同名不同元数的正确分派。 */
            bool is_shadowable_builtin =
                istr_eq(name, "Action", 6) || istr_eq(name, "Func", 4);
            if (expected_tps == 0) {
                if (!is_shadowable_builtin) {
                    zan_diag_emit_code(b->diag, DIAG_ERROR, type_ref->loc, 1007,
                                  "the non-generic type '%.*s' cannot be used with type arguments",
                                  (int)name.len, name.str);
                    return b->type_error;
                }
            } else if (type_ref->type_ref.type_args.count != expected_tps) {
                if (!is_shadowable_builtin) {
                    zan_diag_emit_code(b->diag, DIAG_ERROR, type_ref->loc, 1006,
                                  "using the generic type '%.*s' requires %d type arguments (got %d)",
                                  (int)name.len, name.str, expected_tps,
                                  type_ref->type_ref.type_args.count);
                    return b->type_error;
                }
            }
        }
    }

    /* Synthesize a Func< */
    if (base && base->kind == TYPE_DELEGATE && base->name.len == 4 &&
        memcmp(base->name.str, "Func", 4) == 0) {
        if (type_ref->type_ref.type_args.count < 1) {
            zan_diag_emit_code(b->diag, DIAG_ERROR, type_ref->loc, 1008,
                          "'Func' requires at least one type argument "
                          "(the return type)");
            return b->type_error;
        }
        int nargs = type_ref->type_ref.type_args.count;
        zan_type_t *fn = make_type(b->arena, TYPE_DELEGATE, "Func", 4);
        fn->type_args = (zan_type_t **)zan_arena_alloc(
            b->arena, sizeof(zan_type_t *) * (size_t)nargs);
        fn->type_arg_count = nargs;
        for (int i = 0; i < nargs; i++)
            fn->type_args[i] = zan_binder_resolve_type(
                b, type_ref->type_ref.type_args.items[i]);
        fn->delegate_param_count = nargs - 1;
        if (fn->delegate_param_count > 0) {
            fn->delegate_param_types = (zan_type_t **)zan_arena_alloc(
                b->arena, sizeof(zan_type_t *) * (size_t)fn->delegate_param_count);
            for (int i = 0; i < fn->delegate_param_count; i++)
                fn->delegate_param_types[i] = fn->type_args[i];
        }
        fn->delegate_ret_type = fn->type_args[nargs - 1];
        base = fn;
    } else if (base && base->kind == TYPE_DELEGATE && base->name.len == 6 &&
        memcmp(base->name.str, "Action", 6) == 0) {
        int nargs = type_ref->type_ref.type_args.count;
        zan_type_t *fn = make_type(b->arena, TYPE_DELEGATE, "Action", 6);
        if (nargs > 0) {
            fn->type_args = (zan_type_t **)zan_arena_alloc(
                b->arena, sizeof(zan_type_t *) * (size_t)nargs);
            fn->type_arg_count = nargs;
            for (int i = 0; i < nargs; i++)
                fn->type_args[i] = zan_binder_resolve_type(
                    b, type_ref->type_ref.type_args.items[i]);
        }
        fn->delegate_param_count = nargs;
        if (nargs > 0) {
            fn->delegate_param_types = (zan_type_t **)zan_arena_alloc(
                b->arena, sizeof(zan_type_t *) * (size_t)nargs);
            for (int i = 0; i < nargs; i++)
                fn->delegate_param_types[i] = fn->type_args[i];
        }
        fn->delegate_ret_type = b->type_void;
        base = fn;
    }

    zan_type_t *resolved = base;
    if (type_ref->type_ref.is_array) {
        zan_type_t *elem = resolved;
        if (type_ref->type_ref.array_element)
            elem = zan_binder_resolve_type(b, type_ref->type_ref.array_element);
        if (!elem) elem = b->type_error;
        zan_type_t *arr = make_type(b->arena, TYPE_ARRAY, elem->name.str, elem->name.len);
        arr->element_type = elem;
        arr->array_rank = type_ref->type_ref.array_rank > 0
            ? type_ref->type_ref.array_rank : 1;
        resolved = arr;
    }
    if (type_ref->type_ref.is_nullable) {
        if (resolved && resolved != b->type_error) {
            if (type_is_value_kind(resolved)) {
                zan_type_t *nullable = make_type(b->arena, TYPE_NULLABLE, resolved->name.str, resolved->name.len);
                nullable->element_type = resolved;
                resolved = nullable;
            } else if (!resolved->is_nullable) {
                zan_type_t *nullable = make_type(b->arena, resolved->kind, resolved->name.str, resolved->name.len);
                *nullable = *resolved;
                nullable->is_nullable = 1;
                resolved = nullable;
            }
        }
    }
    if (b->binding_done && resolved && resolved != b->type_error) {
        type_ref->type_ref.rt_type = resolved;
        type_ref->type_ref.rt_scope = (void *)b->current_scope;
    }
    return resolved;
}

zan_symbol_t *zan_binder_lookup(zan_binder_t *b, zan_istr_t name) {
    return scope_find(b->current_scope, name);
}

/* 类型参数按名字统一（A453 定性）：同名 T 在根作用域先到先得只保留一个
 * 对象，但全部消费方（binder_type_params_equiv 只比 arity、
 * checker_unify_method_tp / type_mentions_tp_checker / checker_type_equal
 * 按 名字 memcmp + kind、irgen 的 di_subst_param 按名字）从不比对对象
 * 指针，所以"谁的对象被留下"不可观测——文件解析顺序不改变行为
 * （tests/conformance/type_param_same_name 钉形）。
 * 这份根作用域注册是 checking 阶段的承重墙：checker 全程不设置
 * current_scope，resolve_type 的 rt_scope 缓存守卫让方法体内新出现的
 * 类型引用（局部变量、new、调用实参）全部落回根作用域解析——没有它，
 * 泛型方法体一律 E1005。不要做"按层隔离"重构：那需要给 checker 全程
 * 作用域感知，而同名类型参数在按名字统一架构下语义本就相同，隔离
 * 不产生任何行为差异。 */
static void register_type_param_list(zan_binder_t *b, zan_ast_list_t *tps) {
    for (int j = 0; j < tps->count; j++) {
        zan_ast_node_t *tp = tps->items[j];
        if (tp->kind != AST_IDENTIFIER) continue;
        if (scope_find(b->current_scope, tp->ident.name)) continue;
        zan_type_t *tp_type = make_type(b->arena, TYPE_TYPE_PARAM,
                                        tp->ident.name.str, tp->ident.name.len);
        zan_symbol_t *tp_sym = make_symbol(b->arena, SYM_TYPE_PARAM,
                                           tp->ident.name, tp_type, tp, 0);
        scope_add(b->arena, b->current_scope, tp_sym);
    }
}

static void register_type_params(zan_binder_t *b, zan_ast_list_t *decls) {
    for (int i = 0; i < decls->count; i++) {
        zan_ast_node_t *decl = decls->items[i];
        if (decl->kind == AST_DELEGATE_DECL) {
            register_type_param_list(b, &decl->method_decl.type_params);
            continue;
        }
        if (decl->kind != AST_CLASS_DECL && decl->kind != AST_STRUCT_DECL &&
            decl->kind != AST_INTERFACE_DECL && decl->kind != AST_ENUM_DECL) {
            continue;
        }
        register_type_param_list(b, &decl->type_decl.type_params);
        for (int m = 0; m < decl->type_decl.members.count; m++) {
            zan_ast_node_t *member = decl->type_decl.members.items[m];
            if (member->kind == AST_METHOD_DECL)
                register_type_param_list(b, &member->method_decl.type_params);
        }
    }
}

