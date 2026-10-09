/* binder */

#include "binder.h"
#include "arena.h"
#include "diag.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static zan_type_t *make_type(zan_arena_t *arena, zan_type_kind_t kind, const char *name, int len) {
    zan_type_t *t = (zan_type_t *)zan_arena_alloc(arena, sizeof(zan_type_t));
    t->kind = kind;
    t->name.str = name;
    t->name.len = len;
    t->interfaces = NULL;
    t->interface_count = 0;
    return t;
}

static zan_symbol_t *make_symbol(zan_arena_t *arena, zan_sym_kind_t kind,
                                 zan_istr_t name, zan_type_t *type,
                                 zan_ast_node_t *decl, uint32_t modifiers) {
    zan_symbol_t *s = (zan_symbol_t *)zan_arena_alloc(arena, sizeof(zan_symbol_t));
    s->kind = kind;
    s->name = name;
    s->type = type;
    s->decl = decl;
    s->modifiers = modifiers;
    s->parent = NULL;
    s->members = NULL;
    s->member_count = 0;
    s->member_cap = 0;
    s->name_hash = 0;
    s->hash_next = NULL;
    return s;
}

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

/* 内部辅助逻辑 */
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

/* 内部辅助逻辑 */
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

    /* 内部辅助实现 */
    sym->name_hash = istr_hash(sym->name);
    if (scope->sym_count > scope->bucket_count)   /* load factor 1 */
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
        if (!s->bucket_count) continue;      /* empty scope: no index yet */
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
    b->type_char   = make_type(arena, TYPE_CHAR,   "char",   4);
    b->type_string = make_type(arena, TYPE_STRING, "string", 6);
    b->type_object = make_type(arena, TYPE_OBJECT, "object", 6);
    b->type_nint   = make_type(arena, TYPE_NINT,   "nint",   4);
    b->type_error  = make_type(arena, TYPE_ERROR,  "<error>", 7);
    /* reflection: opaque handle on a type record (irgen_reflect.c) */
    b->type_typeinfo = make_type(arena, TYPE_STRUCT, "TypeInfo", 8);
}

/* 内部辅助逻辑 */
/* 内部辅助逻辑 */
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

/* Full parameter-list identity: count plus each parameter's declared type */
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

/* 内部辅助逻辑 */
static bool binder_type_params_equiv(zan_symbol_t *a, zan_symbol_t *b) {
    int atp = (a->decl && a->decl->kind == AST_METHOD_DECL)
        ? a->decl->method_decl.type_params.count : 0;
    int btp = (b->decl && b->decl->kind == AST_METHOD_DECL)
        ? b->decl->method_decl.type_params.count : 0;
    return atp == btp;
}

/* Per-type member-name index (struct zan_binder.member_idx). */

typedef struct {
    int member_idx; /* index into the type symbol's members[] */
    int next;       /* next node in the bucket chain, -1 = end */
} member_name_node_t;

struct zan_member_name_index {
    int *buckets; /* head node per bucket, -1 = empty */
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

/* 内部辅助逻辑 */
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

/* 内部辅助逻辑 */
static bool member_clash_one(zan_binder_t *b, zan_symbol_t *type_sym,
                             zan_symbol_t *added, zan_symbol_t *m) {
    bool m_is_code = m->kind == SYM_METHOD;
    bool a_is_code = added->kind == SYM_METHOD;
    bool m_is_data = m->kind == SYM_FIELD || m->kind == SYM_PROPERTY;
    bool a_is_data = added->kind == SYM_FIELD || added->kind == SYM_PROPERTY;
    if ((m_is_code && a_is_data) || (m_is_data && a_is_code)) {
        zan_symbol_t *data = a_is_data ? added : m;
        zan_diag_emit(b->diag, DIAG_ERROR, added->decl->loc,
                      "'%.*s' is declared both as a %s and as a method in "
                      "'%.*s'; give one of them a different name",
                      added->name.len, added->name.str,
                      data->kind == SYM_PROPERTY ? "property" : "field",
                      type_sym->name.len, type_sym->name.str);
        return true;
    }
    /* Two members of the same kind with the same name cannot coexist */
    if (m_is_data && a_is_data) {
        /* 内部辅助实现 */
        zan_ast_node_t *ad = added->decl;
        zan_ast_node_t *md = m->decl;
        if (ad && ad->kind == AST_PROPERTY_DECL &&
            ad->field_decl.indexer_params &&
            md && md->kind == AST_PROPERTY_DECL &&
            md->field_decl.indexer_params) {
            return false;
        }
        zan_diag_emit(b->diag, DIAG_ERROR, added->decl->loc,
                      "duplicate '%.*s' in '%.*s': a %s with this name "
                      "is already declared",
                      added->name.len, added->name.str,
                      type_sym->name.len, type_sym->name.str,
                      added->kind == SYM_PROPERTY ? "property" : "field");
        return true;
    }
    if (m->kind == SYM_ENUM_MEMBER && added->kind == SYM_ENUM_MEMBER) {
        zan_diag_emit(b->diag, DIAG_ERROR, added->decl->loc,
                      "duplicate enum member '%.*s' in '%.*s'",
                      added->name.len, added->name.str,
                      type_sym->name.len, type_sym->name.str);
        return true;
    }
    if ((m_is_code && a_is_code) &&
        binder_params_equiv(m, added) &&
        binder_type_params_equiv(m, added)) {
        zan_diag_emit(b->diag, DIAG_ERROR, added->decl->loc,
                      "duplicate method '%.*s' in '%.*s': a method with "
                      "the same parameter types is already declared",
                      added->name.len, added->name.str,
                      type_sym->name.len, type_sym->name.str);
        return true;
    }
    if (m->kind == SYM_CONSTRUCTOR && added->kind == SYM_CONSTRUCTOR &&
        binder_params_equiv(m, added)) {
        zan_diag_emit(b->diag, DIAG_ERROR, added->decl->loc,
                      "duplicate constructor in '%.*s': a constructor "
                      "with the same parameter types is already declared",
                      type_sym->name.len, type_sym->name.str);
        return true;
    }
    return false;
}

/* 内部辅助逻辑 */
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
    /* 内部辅助逻辑 */
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
    /* 内部辅助逻辑 */
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

/* ---- type resolution ---- */

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

/* 内部辅助逻辑 */
zan_type_t *zan_binder_make_span_type(zan_binder_t *b, zan_type_t *elem) {
    zan_type_t *t = make_type(b->arena, TYPE_STRUCT, "Span", 4);
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

/* Grouping<T> is the System */
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

/* 内部辅助实现 */
static bool tuple_sig_type(zan_type_t *t, char *buf, size_t cap) {
    if (!t || cap == 0) return true;
    size_t used = strlen(buf);
    if (used + 1 >= cap) return false;
    if (t->kind == TYPE_ARRAY && t->element_type) {
        bool ok = tuple_sig_type(t->element_type, buf, cap);
        if (strlen(buf) + 4 < cap) {
            strcat(buf, "[");
            for (int r = 1; r < (t->array_rank > 1 ? t->array_rank : 1); r++)
                if (strlen(buf) + 2 < cap) strcat(buf, ",");
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

/* FNV-1a over a canonical encoding of a type structure */
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

/* 内部辅助逻辑 */
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
    /* canonical signature: "__tuple<N>:<sig1>,<sig2>,..." */
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
        /* 内部辅助实现 */
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

    /* cache hit: structurally-identical tuple types are one struct */
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

/* 内部辅助逻辑 */
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

/* 内部辅助实现 */
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

/* `T?` for a type no type reference spells, such as the result of `a? */
zan_type_t *zan_binder_make_nullable_type(zan_binder_t *b, zan_type_t *elem) {
    if (!elem || elem == b->type_error || elem->kind == TYPE_NULLABLE ||
        !type_is_value_kind(elem))
        return elem;
    zan_type_t *t = make_type(b->arena, TYPE_NULLABLE, elem->name.str, elem->name.len);
    t->element_type = elem;
    return t;
}

/* 内部辅助逻辑 */
static inline int binder_decl_type_param_count(const zan_ast_node_t *d) {
    if (!d) return 0;
    if (d->kind == AST_DELEGATE_DECL) return d->method_decl.type_params.count;
    return d->type_decl.type_params.count;
}

zan_type_t *zan_binder_resolve_type(zan_binder_t *b, zan_ast_node_t *type_ref) {
    if (!type_ref) return b->type_error;
    if (type_ref->kind == AST_TUPLE_TYPE) {
        /* 内部辅助逻辑 */
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

    /* 内部辅助实现 */
    if (b->binding_done && type_ref->type_ref.rt_type &&
        type_ref->type_ref.rt_scope == (void *)b->current_scope)
        return (zan_type_t *)type_ref->type_ref.rt_type;

    zan_istr_t name = type_ref->type_ref.name;

    /* 内部辅助逻辑 */
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
    /* 内部辅助逻辑 */
    else if (istr_eq(name, "decimal", 7)) {
        zan_diag_emit(b->diag, DIAG_ERROR, type_ref->loc,
                      "'decimal' is not supported: use 'long' (minor units), "
                      "'double', or a string column instead");
        base = b->type_error;
    }
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
    /* TypeInfo: what typeof(T)/obj.GetType() yields (see binder.h) */
    else if (istr_eq(name, "TypeInfo", 8)) base = b->type_typeinfo;
    /* Task / Task<T>: a coroutine handle (opaque i64 at codegen) */
    else if (istr_eq(name, "Task", 4))
        base = make_type(b->arena, TYPE_TASK, "Task", 4);
    /*  */
    else if (istr_eq(name, "Func", 4) || istr_eq(name, "Action", 6))
        base = make_type(b->arena, TYPE_DELEGATE, name.str, name.len);
    else {
        zan_symbol_t *sym = scope_find(b->current_scope, name);
        if (sym) base = sym->type;
    }

    if (!base) {
        zan_diag_emit(b->diag, DIAG_ERROR, type_ref->loc,
                      "undefined type '%.*s'", name.len, name.str);
        return b->type_error;
    }

    /* carry generic arguments (e */
    /* A generic delegate instantiation (e */
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
                               istr_eq(name, "Task", 4);
        bool user_generic = !builtin_generic &&
            (base->kind == TYPE_CLASS || base->kind == TYPE_STRUCT ||
             base->kind == TYPE_INTERFACE);
        if (builtin_generic || user_generic) {
            int nargs = type_ref->type_ref.type_args.count;
            int expected_tps = -1;
            if (istr_eq(name, "List", 4) || istr_eq(name, "Span", 4) || istr_eq(name, "Task", 4))
                expected_tps = 1;
            else if (istr_eq(name, "Dict", 4) || istr_eq(name, "Dictionary", 10))
                expected_tps = 2;
            else if (user_generic && base->sym && base->sym->decl)
                expected_tps = binder_decl_type_param_count(base->sym->decl);

            if (expected_tps >= 0 && nargs != expected_tps) {
                zan_diag_emit(b->diag, DIAG_ERROR, type_ref->loc,
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
            base = inst;
        } else if (base->sym && base->sym->decl) {
            int expected_tps = binder_decl_type_param_count(base->sym->decl);
            if (expected_tps == 0) {
                zan_diag_emit(b->diag, DIAG_ERROR, type_ref->loc,
                              "the non-generic type '%.*s' cannot be used with type arguments",
                              (int)name.len, name.str);
                return b->type_error;
            } else if (type_ref->type_ref.type_args.count != expected_tps) {
                zan_diag_emit(b->diag, DIAG_ERROR, type_ref->loc,
                              "using the generic type '%.*s' requires %d type arguments (got %d)",
                              (int)name.len, name.str, expected_tps,
                              type_ref->type_ref.type_args.count);
                return b->type_error;
            }
        }
    }

    /* Synthesize a Func< */
    if (base && base->kind == TYPE_DELEGATE && base->name.len == 4 &&
        memcmp(base->name.str, "Func", 4) == 0) {
        if (type_ref->type_ref.type_args.count < 1) {
            zan_diag_emit(b->diag, DIAG_ERROR, type_ref->loc,
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
        /* Func<TResult> is a zero-parameter delegate */
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
    if (type_ref->type_ref.is_nullable) {
        /* `T?` over a reference type is just T: the reference already carries null */
        if (resolved && resolved != b->type_error &&
            type_is_value_kind(resolved)) {
            zan_type_t *nullable = make_type(b->arena, TYPE_NULLABLE, name.str, name.len);
            nullable->element_type = resolved;
            resolved = nullable;
        }
        /* Reference types are left unwrapped: `A?` is A, which already admits null */
    }
    /* 内部辅助逻辑 */
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
    if (b->binding_done && resolved && resolved != b->type_error) {
        type_ref->type_ref.rt_type = resolved;
        type_ref->type_ref.rt_scope = (void *)b->current_scope;
    }
    return resolved;
}

zan_symbol_t *zan_binder_lookup(zan_binder_t *b, zan_istr_t name) {
    return scope_find(b->current_scope, name);
}

/* 内部辅助实现 */
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
        /* delegate declarations may themselves be generic: delegate R F<T>( */
        if (decl->kind == AST_DELEGATE_DECL) {
            register_type_param_list(b, &decl->method_decl.type_params);
            continue;
        }
        if (decl->kind != AST_CLASS_DECL && decl->kind != AST_STRUCT_DECL &&
            decl->kind != AST_INTERFACE_DECL && decl->kind != AST_ENUM_DECL) {
            continue;
        }
        register_type_param_list(b, &decl->type_decl.type_params);
        /* Method-level type parameters (static T Id<T>( */
        for (int m = 0; m < decl->type_decl.members.count; m++) {
            zan_ast_node_t *member = decl->type_decl.members.items[m];
            if (member->kind == AST_METHOD_DECL)
                register_type_param_list(b, &member->method_decl.type_params);
        }
    }
}

static zan_sym_kind_t ast_kind_to_sym_kind(zan_ast_kind_t kind) {
    switch (kind) {
    case AST_CLASS_DECL:     return SYM_CLASS;
    case AST_STRUCT_DECL:    return SYM_STRUCT;
    case AST_INTERFACE_DECL: return SYM_INTERFACE;
    case AST_ENUM_DECL:      return SYM_ENUM;
    case AST_DELEGATE_DECL:  return SYM_DELEGATE;
    default:                 return SYM_CLASS;
    }
}

static zan_type_kind_t ast_kind_to_type_kind(zan_ast_kind_t kind) {
    switch (kind) {
    case AST_CLASS_DECL:     return TYPE_CLASS;
    case AST_STRUCT_DECL:    return TYPE_STRUCT;
    case AST_INTERFACE_DECL: return TYPE_INTERFACE;
    case AST_ENUM_DECL:      return TYPE_ENUM;
    case AST_DELEGATE_DECL:  return TYPE_DELEGATE;
    default:                 return TYPE_CLASS;
    }
}

/* Pass 1: register type declarations */
static void bind_type_decls(zan_binder_t *b, zan_ast_list_t *decls) {
    /* 内部辅助逻辑 */
    zan_symbol_t **syms = (zan_symbol_t **)zan_arena_alloc(
        b->arena, sizeof(zan_symbol_t *) *
                      (size_t)(decls->count > 0 ? decls->count : 1));
    for (int i = 0; i < decls->count; i++) {
        syms[i] = NULL;
        zan_ast_node_t *node = decls->items[i];

        if (node->kind == AST_DELEGATE_DECL) {
            zan_istr_t name = node->method_decl.name;
            zan_symbol_t *existing = scope_find(b->current_scope, name);
            if (existing) {
                zan_diag_emit(b->diag, DIAG_ERROR, node->loc,
                              "duplicate type declaration '%.*s'", name.len, name.str);
                continue;
            }
            zan_type_t *type = make_type(b->arena, TYPE_DELEGATE, name.str, name.len);
            zan_symbol_t *sym = make_symbol(b->arena, SYM_DELEGATE,
                name, type, node, node->method_decl.modifiers);
            type->sym = sym;
            scope_add(b->arena, b->current_scope, sym);
            continue;
        }

        if (node->kind == AST_CLASS_DECL || node->kind == AST_STRUCT_DECL ||
            node->kind == AST_INTERFACE_DECL || node->kind == AST_ENUM_DECL) {

            zan_istr_t name = node->type_decl.name;

            zan_symbol_t *existing = scope_find(b->current_scope, name);
            if (existing) {
                zan_diag_emit(b->diag, DIAG_ERROR, node->loc,
                              "duplicate type declaration '%.*s'", name.len, name.str);
                continue;
            }

            zan_type_t *type = make_type(b->arena,
                ast_kind_to_type_kind(node->kind), name.str, name.len);
            zan_symbol_t *sym = make_symbol(b->arena,
                ast_kind_to_sym_kind(node->kind),
                name, type, node, node->type_decl.modifiers);
            type->sym = sym;
            type->base_type = NULL;

            scope_add(b->arena, b->current_scope, sym);
            syms[i] = sym;
        }
    }

    /* 内部辅助实现 */
    for (int i = 0; i < decls->count; i++) {
        zan_ast_node_t *node = decls->items[i];
        if (node->kind != AST_CLASS_DECL && node->kind != AST_STRUCT_DECL &&
            node->kind != AST_INTERFACE_DECL && node->kind != AST_ENUM_DECL)
            continue;
        zan_ast_node_t *host = node->type_decl.nested_host;
        if (!host) continue;
        zan_symbol_t *sym = syms[i];
        zan_symbol_t *host_sym = NULL;
        for (int j = 0; j < decls->count; j++) {
            if (decls->items[j] == host) { host_sym = syms[j]; break; }
        }
        if (sym && host_sym) symbol_add_member(b->arena, host_sym, sym);
    }

    /* Resolve delegate signatures only after every named type is registered */
    for (int i = 0; i < decls->count; i++) {
        zan_ast_node_t *node = decls->items[i];
        if (node->kind != AST_DELEGATE_DECL) continue;

        zan_symbol_t *sym = scope_find(b->current_scope, node->method_decl.name);
        if (!sym || sym->decl != node) continue;
        zan_type_t *type = sym->type;

        register_type_param_list(b, &node->method_decl.type_params);
        type->delegate_is_async = (node->method_decl.modifiers & MOD_ASYNC) != 0;
        type->delegate_ret_type = node->method_decl.return_type
            ? zan_binder_resolve_type(b, node->method_decl.return_type)
            : b->type_void;
        int pc = node->method_decl.params.count;
        type->delegate_param_count = pc;
        if (pc > 0) {
            type->delegate_param_types = (zan_type_t **)zan_arena_alloc(
                b->arena, sizeof(zan_type_t *) * (size_t)pc);
            for (int j = 0; j < pc; j++) {
                zan_ast_node_t *param = node->method_decl.params.items[j];
                type->delegate_param_types[j] =
                    zan_binder_resolve_type(b, param->param.type);
            }
        } else {
            type->delegate_param_types = NULL;
        }
    }
}

/* Pass 3: resolve base types and inherit members */
/* 内部辅助实现 */
static bool base_args_resolvable(zan_binder_t *b, zan_ast_node_t *type_ref) {
    if (!type_ref || type_ref->kind != AST_TYPE_REF) return false;
    zan_ast_list_t *args = &type_ref->type_ref.type_args;
    for (int i = 0; i < args->count; i++) {
        zan_ast_node_t *arg = args->items[i];
        if (!arg || arg->kind != AST_TYPE_REF) return false;
        zan_istr_t n = arg->type_ref.name;
        if (istr_eq(n, "void", 4) || istr_eq(n, "bool", 4) ||
            istr_eq(n, "byte", 4) || istr_eq(n, "short", 5) ||
            istr_eq(n, "int", 3) || istr_eq(n, "long", 4) ||
            istr_eq(n, "sbyte", 5) || istr_eq(n, "ushort", 6) ||
            istr_eq(n, "uint", 4) || istr_eq(n, "ulong", 5) ||
            istr_eq(n, "float", 5) || istr_eq(n, "double", 6) ||
            istr_eq(n, "decimal", 7) || istr_eq(n, "char", 4) ||
            istr_eq(n, "string", 6) || istr_eq(n, "object", 6) ||
            istr_eq(n, "nint", 4))
            continue;
        if (!scope_find(b->current_scope, n)) return false;
        /* 内部辅助实现 */
        if (!base_args_resolvable(b, arg)) return false;
    }
    return true;
}

static void resolve_bases(zan_binder_t *b, zan_ast_node_t *type_node) {
    zan_symbol_t *type_sym = scope_find(b->current_scope, type_node->type_decl.name);
    if (!type_sym || !type_sym->type || type_sym->decl != type_node) return;
    if (type_sym->type->bases_resolved == 2) return;   /* done */
    if (type_sym->type->bases_resolved == 1) {
        /* 内部辅助逻辑 */
        zan_diag_emit(b->diag, DIAG_ERROR, type_node->loc,
                      "cyclic inheritance involving '%.*s'",
                      (int)type_node->type_decl.name.len,
                      type_node->type_decl.name.str);
        return;
    }
    type_sym->type->bases_resolved = 1;

    if (type_node->type_decl.bases.count > 0) {
        int nbases = type_node->type_decl.bases.count;
        zan_type_t **ifaces = (zan_type_t **)zan_arena_alloc(
            b->arena, sizeof(zan_type_t *) * (size_t)nbases);
        int nif = 0;
        for (int bx = 0; bx < nbases; bx++) {
            zan_ast_node_t *base_ref = type_node->type_decl.bases.items[bx];
            /* parse_type_ref can return a tuple-type or error node (e */
            if (base_ref->kind != AST_TYPE_REF) {
                zan_diag_emit(b->diag, DIAG_ERROR, base_ref->loc,
                              "invalid base type");
                continue;
            }
            zan_istr_t base_name = base_ref->type_ref.name;
            zan_symbol_t *base_sym = scope_find(b->current_scope, base_name);
            if (!base_sym) {
                zan_diag_emit(b->diag, DIAG_ERROR, base_ref->loc,
                              "undefined base type '%.*s'",
                              (int)base_name.len, base_name.str);
                continue;
            }
            if ((base_sym->kind == SYM_CLASS || base_sym->kind == SYM_STRUCT) &&
                !type_sym->type->base_type) {
                /* 内部辅助逻辑 */
                if ((base_sym->modifiers & MOD_SEALED) != 0) {
                    zan_diag_emit(b->diag, DIAG_ERROR, base_ref->loc,
                                  "cannot derive from sealed type '%.*s'",
                                  (int)base_name.len, base_name.str);
                    continue;
                }
                /* make sure the base has its own inherited fields first */
                if (base_sym->decl &&
                    (base_sym->decl->kind == AST_CLASS_DECL ||
                     base_sym->decl->kind == AST_STRUCT_DECL)) {
                    resolve_bases(b, base_sym->decl);
                }
                /* 内部辅助实现 */
                if (base_sym->type->bases_resolved != 2) continue;
                if (base_args_resolvable(b, base_ref)) {
                    zan_type_t *inst = zan_binder_resolve_type(b, base_ref);
                    if (inst && (inst->kind == TYPE_CLASS || inst->kind == TYPE_STRUCT) &&
                        inst != b->type_error) {
                        type_sym->type->base_type = inst;
                    } else {
                        type_sym->type->base_type = base_sym->type;
                    }
                } else {
                    type_sym->type->base_type = base_sym->type;
                }
                /* 内部辅助逻辑 */
                int ninherit = 0;
                for (int bi = 0; bi < base_sym->member_count; bi++) {
                    if (base_sym->members[bi]->kind == SYM_FIELD ||
                        base_sym->members[bi]->kind == SYM_PROPERTY)
                        ninherit++;
                }
                if (ninherit > 0) {
                    int total = ninherit + type_sym->member_count;
                    zan_symbol_t **merged = (zan_symbol_t **)zan_arena_alloc(
                        b->arena, sizeof(zan_symbol_t *) * (size_t)total);
                    int mi = 0;
                    for (int bi = 0; bi < base_sym->member_count; bi++) {
                        if (base_sym->members[bi]->kind == SYM_FIELD ||
                            base_sym->members[bi]->kind == SYM_PROPERTY) {
                            merged[mi++] = base_sym->members[bi];
                        }
                    }
                    for (int oi = 0; oi < type_sym->member_count; oi++) {
                        merged[mi++] = type_sym->members[oi];
                    }
                    type_sym->members = merged;
                    type_sym->member_count = total;
                    type_sym->member_cap = total;
                }
            } else if (base_sym->kind == SYM_INTERFACE) {
                if (base_sym->decl) resolve_bases(b, base_sym->decl);
                if (base_sym->type->bases_resolved != 2) continue;
                /* 内部辅助实现 */
                if (base_args_resolvable(b, base_ref)) {
                    zan_type_t *inst = zan_binder_resolve_type(b, base_ref);
                    if (inst && inst->kind == TYPE_INTERFACE &&
                        inst != b->type_error) {
                        ifaces[nif++] = inst;
                        continue;
                    }
                }
                ifaces[nif++] = base_sym->type;
            }
        }
        if (nif > 0) {
            type_sym->type->interfaces = ifaces;
            type_sym->type->interface_count = nif;
        }
    }
    type_sym->type->bases_resolved = 2;
}

/* 内部辅助实现 */
static bool binder_type_equal(zan_type_t *a, zan_type_t *b, int depth) {
    if (!a || !b) return a == b;
    if (a == b) return true;
    if (depth > 64 || a->kind != b->kind) return false;
    if (a->kind == TYPE_ARRAY || a->kind == TYPE_NULLABLE) {
        if (a->kind == TYPE_ARRAY && a->array_rank != b->array_rank)
            return false; /* int[,] is not int[] */
        return binder_type_equal(a->element_type, b->element_type, depth + 1);
    }
    if (a->kind == TYPE_DELEGATE) {
        if (a->delegate_is_async != b->delegate_is_async ||
            a->delegate_param_count != b->delegate_param_count ||
            !binder_type_equal(a->delegate_ret_type, b->delegate_ret_type, depth + 1))
            return false;
        for (int i = 0; i < a->delegate_param_count; i++)
            if (!binder_type_equal(a->delegate_param_types[i],
                                   b->delegate_param_types[i], depth + 1))
                return false;
        return true;
    }
    if (a->type_arg_count != b->type_arg_count) return false;
    if (a->sym && b->sym && a->sym != b->sym) return false;
    if ((!a->sym || !b->sym) &&
        (a->name.len != b->name.len ||
         memcmp(a->name.str, b->name.str, (size_t)a->name.len) != 0))
        return false;
    for (int i = 0; i < a->type_arg_count; i++)
        if (!binder_type_equal(a->type_args[i], b->type_args[i], depth + 1))
            return false;
    return true;
}

/* 类型兼容性判定：t 继承自 sup 或实现了 sup 接口 */
static bool binder_type_derives(zan_type_t *t, zan_type_t *sup, int depth) {
    if (!t || !sup || depth > 64) return false;
    if (t == sup) return true;
    if (t->sym && sup->sym && t->sym == sup->sym)
        return binder_type_equal(t, sup, 0);
    for (int i = 0; i < t->interface_count; i++)
        if (binder_type_derives(t->interfaces[i], sup, depth + 1))
            return true;
    if (t->base_type && t->base_type != t)
        return binder_type_derives(t->base_type, sup, depth + 1);
    return false;
}

static bool method_signature_equal(zan_binder_t *b, zan_ast_node_t *a,
                                   zan_ast_node_t *other,
                                   zan_ast_list_t *bind_tps,
                                   zan_type_t **bind_args) {
    if (!a || !other || a->kind != AST_METHOD_DECL ||
        other->kind != AST_METHOD_DECL ||
        a->method_decl.name.len != other->method_decl.name.len ||
        memcmp(a->method_decl.name.str, other->method_decl.name.str,
               (size_t)a->method_decl.name.len) != 0)
        return false;
    if (((a->method_decl.modifiers ^ other->method_decl.modifiers) &
         (MOD_STATIC | MOD_ASYNC)) != 0)
        return false;
    if (a->method_decl.params.count != other->method_decl.params.count)
        return false;
    zan_type_t *ar = a->method_decl.return_type
        ? zan_binder_resolve_type(b, a->method_decl.return_type) : b->type_void;
    zan_type_t *br = other->method_decl.return_type
        ? zan_binder_resolve_type(b, other->method_decl.return_type) : b->type_void;
    /* 内部辅助实现 */
    bool ret_subst = false;
    if (bind_tps && bind_tps->count > 0 && bind_args) {
        zan_type_t *sr = zan_binder_subst_named(b, br, bind_tps, bind_args);
        if (sr && sr != br) { br = sr; ret_subst = true; }
    }
    /* 内部辅助逻辑 */
    bool a_tp = ar && ar->kind == TYPE_TYPE_PARAM;
    bool b_tp = br && br->kind == TYPE_TYPE_PARAM && !ret_subst;
    if (!(a_tp || b_tp) && !binder_type_equal(ar, br, 0) &&
        !binder_type_derives(ar, br, 0))
        return false;
    for (int i = 0; i < a->method_decl.params.count; i++) {
        zan_ast_node_t *ap = a->method_decl.params.items[i];
        zan_ast_node_t *bp = other->method_decl.params.items[i];
        if (!ap || !bp || ap->kind != AST_PARAM || bp->kind != AST_PARAM ||
            ap->param.by_ref != bp->param.by_ref ||
            ap->param.is_params != bp->param.is_params ||
            ap->param.is_this != bp->param.is_this)
            return false;
        zan_type_t *at = zan_binder_resolve_type(b, ap->param.type);
        zan_type_t *bt = zan_binder_resolve_type(b, bp->param.type);
        bool param_subst = false;
        if (bind_tps && bind_tps->count > 0 && bind_args) {
            zan_type_t *st = zan_binder_subst_named(b, bt, bind_tps, bind_args);
            if (st && st != bt) { bt = st; param_subst = true; }
        }
        bool at_tp = at && at->kind == TYPE_TYPE_PARAM;
        bool bt_tp = bt && bt->kind == TYPE_TYPE_PARAM && !param_subst;
        if (!(at_tp || bt_tp) && !binder_type_equal(at, bt, 0)) return false;
    }
    return true;
}

static bool type_declares_method(zan_binder_t *b, zan_ast_node_t *type_node,
                                 zan_ast_node_t *wanted) {
    for (int i = 0; i < type_node->type_decl.members.count; i++) {
        zan_ast_node_t *m = type_node->type_decl.members.items[i];
        if (method_signature_equal(b, m, wanted, NULL, NULL)) return true;
    }
    return false;
}

static void inherit_default_methods(zan_binder_t *b, zan_ast_node_t *type_node,
                                    zan_ast_node_t *iface_node, int depth) {
    if (!iface_node || iface_node->kind != AST_INTERFACE_DECL || depth > 8) return;
    for (int i = 0; i < iface_node->type_decl.members.count; i++) {
        zan_ast_node_t *m = iface_node->type_decl.members.items[i];
        if (m->kind != AST_METHOD_DECL || !m->method_decl.body) continue;
        if ((m->method_decl.modifiers & MOD_STATIC) != 0) continue;
        if (type_declares_method(b, type_node, m)) continue;
        /* 内部辅助逻辑 */
        zan_ast_node_t *copy = zan_ast_new(b->arena, AST_METHOD_DECL, m->loc);
        *copy = *m;
        zan_ast_list_push(&type_node->type_decl.members, copy, b->arena);
    }
    /* an interface may itself extend interfaces carrying defaults */
    for (int bx = 0; bx < iface_node->type_decl.bases.count; bx++) {
        zan_ast_node_t *bref = iface_node->type_decl.bases.items[bx];
        if (bref->kind != AST_TYPE_REF) continue;   /* invalid base: rejected at resolve time */
        zan_symbol_t *bs = scope_find(b->current_scope, bref->type_ref.name);
        if (bs && bs->kind == SYM_INTERFACE && bs->decl)
            inherit_default_methods(b, type_node, bs->decl, depth + 1);
    }
}

static void bind_default_interface_methods(zan_binder_t *b, zan_ast_list_t *decls) {
    for (int i = 0; i < decls->count; i++) {
        zan_ast_node_t *decl = decls->items[i];
        if (decl->kind != AST_CLASS_DECL && decl->kind != AST_STRUCT_DECL) continue;
        for (int bx = 0; bx < decl->type_decl.bases.count; bx++) {
            zan_ast_node_t *bref = decl->type_decl.bases.items[bx];
            if (bref->kind != AST_TYPE_REF) continue;   /* invalid base: rejected at resolve time */
            zan_symbol_t *bs = scope_find(b->current_scope, bref->type_ref.name);
            if (bs && bs->kind == SYM_INTERFACE && bs->decl)
                inherit_default_methods(b, decl, bs->decl, 0);
        }
    }
}

static bool class_has_interface_method(zan_binder_t *b, zan_symbol_t *cls,
                                       zan_ast_node_t *wanted,
                                       zan_ast_list_t *bind_tps,
                                       zan_type_t **bind_args) {
    for (zan_symbol_t *s = cls; s; s = (s->type && s->type->base_type)
             ? s->type->base_type->sym : NULL) {
        for (int i = 0; i < s->member_count; i++) {
            zan_symbol_t *m = s->members[i];
            if (m && m->kind == SYM_METHOD && m->decl &&
                method_signature_equal(b, m->decl, wanted, bind_tps,
                                       bind_args))
                return true;
        }
    }
    return false;
}

static void validate_interface_contract(zan_binder_t *b, zan_symbol_t *cls,
                                        zan_ast_node_t *iface,
                                        zan_type_t *inst, int depth) {
    /* 内部辅助实现 */
    zan_ast_list_t *bind_tps = NULL;
    zan_type_t **bind_args = NULL;
    if (inst && inst->type_arg_count > 0 &&
        inst->type_arg_count == iface->type_decl.type_params.count) {
        bind_tps = &iface->type_decl.type_params;
        bind_args = inst->type_args;
    }
    if (!iface || depth > 64) return;
    /* 内部辅助逻辑 */
    zan_scope_t *saved = b->current_scope;
    int tp_count = iface->type_decl.type_params.count;
    if (tp_count > 0) {
        b->current_scope = scope_new(b->arena, saved);
        register_type_param_list(b, &iface->type_decl.type_params);
    }
    for (int i = 0; i < iface->type_decl.members.count; i++) {
        zan_ast_node_t *im = iface->type_decl.members.items[i];
        if (im->kind != AST_METHOD_DECL ||
            (im->method_decl.modifiers & MOD_STATIC) != 0 ||
            im->method_decl.body)
            continue;
        if (!class_has_interface_method(b, cls, im, bind_tps, bind_args)) {
            zan_diag_emit(b->diag, DIAG_ERROR, im->loc,
                          "type '%.*s' does not implement interface method '%.*s'",
                          (int)cls->name.len, cls->name.str,
                          (int)im->method_decl.name.len,
                          im->method_decl.name.str);
        }
    }
    for (int i = 0; i < iface->type_decl.bases.count; i++) {
        zan_ast_node_t *bref = iface->type_decl.bases.items[i];
        if (bref->kind != AST_TYPE_REF) continue;   /* invalid base: rejected at resolve time */
        zan_symbol_t *base = scope_find(b->current_scope, bref->type_ref.name);
        if (base && base->kind == SYM_INTERFACE && base->decl)
            validate_interface_contract(b, cls, base->decl, NULL, depth + 1);
    }
    b->current_scope = saved;
}

static void validate_interface_contracts(zan_binder_t *b, zan_ast_list_t *decls) {
    for (int i = 0; i < decls->count; i++) {
        zan_ast_node_t *decl = decls->items[i];
        if (decl->kind != AST_CLASS_DECL && decl->kind != AST_STRUCT_DECL) continue;
        zan_symbol_t *cls = scope_find(b->current_scope, decl->type_decl.name);
        if (!cls || !cls->type) continue;
        for (int j = 0; j < cls->type->interface_count; j++) {
            zan_type_t *iface = cls->type->interfaces[j];
            if (iface && iface->sym && iface->sym->decl)
                validate_interface_contract(b, cls, iface->sym->decl, iface, 0);
        }
    }
}

/* Pass 2: bind member declarations */
/* Fold an enum member initializer to its integer value */
static bool fold_enum_member_value(zan_binder_t *b, zan_symbol_t *type_sym,
                                   zan_ast_node_t *e, int64_t *out) {
    if (!e) return false;
    switch (e->kind) {
    case AST_INT_LITERAL:
        *out = e->int_val;
        return true;
    case AST_UNARY: {
        int64_t v;
        if (!fold_enum_member_value(b, type_sym, e->unary.operand, &v))
            return false;
        uint64_t uv = (uint64_t)v;
        switch (e->unary.op) {
        case TK_MINUS: *out = (int64_t)(0ULL - uv); return true;
        case TK_PLUS:  *out = v; return true;
        case TK_TILDE: *out = ~v; return true;
        default: return false;
        }
    }
    case AST_BINARY: {
        int64_t l, r;
        if (!fold_enum_member_value(b, type_sym, e->binary.left, &l))
            return false;
        if (!fold_enum_member_value(b, type_sym, e->binary.right, &r))
            return false;
        uint64_t ul = (uint64_t)l, ur = (uint64_t)r;
        switch (e->binary.op) {
        case TK_PLUS:  *out = (int64_t)(ul + ur); return true;
        case TK_MINUS: *out = (int64_t)(ul - ur); return true;
        case TK_STAR:  *out = (int64_t)(ul * ur); return true;
        case TK_SLASH:
            if (!r || (l == INT64_MIN && r == -1)) return false;
            *out = l / r; return true;
        case TK_LESS_LESS: *out = (int64_t)((uint64_t)l << (r & 63)); return true;
        case TK_AMP:   *out = l & r; return true;
        case TK_PIPE:  *out = l | r; return true;
        default: return false;
        }
    }
    case AST_IDENTIFIER: {
        for (int i = 0; i < type_sym->member_count; i++) {
            zan_symbol_t *m = type_sym->members[i];
            if (m->kind != SYM_ENUM_MEMBER) continue;
            if (m->name.len != e->ident.name.len ||
                memcmp(m->name.str, e->ident.name.str, m->name.len))
                continue;
            /* 内部辅助逻辑 */
            zan_ast_node_t *md = m->decl;
            if (!md || md->kind != AST_ENUM_MEMBER ||
                !md->enum_member.value ||
                md->enum_member.value->kind != AST_INT_LITERAL)
                return false;
            *out = md->enum_member.value->int_val;
            return true;
        }
        return false;
    }
    default:
        return false;
    }
}

/* 内部辅助逻辑 */
static bool enum_value_fits_int(int64_t v) {
    return v >= INT32_MIN && v <= INT32_MAX;
}

static void bind_members(zan_binder_t *b, zan_ast_node_t *type_node) {
    zan_istr_t type_name = type_node->type_decl.name;
    zan_symbol_t *type_sym = scope_find(b->current_scope, type_name);
    if (!type_sym) return;

    zan_scope_t *saved = b->current_scope;
    b->current_scope = scope_new(b->arena, saved);

    for (int i = 0; i < type_node->type_decl.type_params.count; i++) {
        zan_ast_node_t *tp = type_node->type_decl.type_params.items[i];
        zan_type_t *tp_type = make_type(b->arena, TYPE_TYPE_PARAM,
                                        tp->ident.name.str, tp->ident.name.len);
        zan_symbol_t *tp_sym = make_symbol(b->arena, SYM_TYPE_PARAM,
                                           tp->ident.name, tp_type, tp, 0);
        scope_add(b->arena, b->current_scope, tp_sym);
        symbol_add_member(b->arena, type_sym, tp_sym);
    }

    for (int i = 0; i < type_node->type_decl.members.count; i++) {
        zan_ast_node_t *member = type_node->type_decl.members.items[i];

        switch (member->kind) {
        case AST_FIELD_DECL:
        case AST_PROPERTY_DECL: {
            zan_type_t *field_type = zan_binder_resolve_type(b, member->field_decl.type);
            zan_symbol_t *field_sym = make_symbol(b->arena,
                member->kind == AST_PROPERTY_DECL ? SYM_PROPERTY : SYM_FIELD,
                member->field_decl.name, field_type, member,
                member->field_decl.modifiers);
            scope_add(b->arena, b->current_scope, field_sym);
            symbol_add_member(b->arena, type_sym, field_sym);
            check_member_name_clash(b, type_sym, field_sym);
            break;
        }
        case AST_METHOD_DECL: {
            /* generic method: make its type params resolvable in the signature */
            register_type_param_list(b, &member->method_decl.type_params);
            zan_type_t *ret_type = zan_binder_resolve_type(b, member->method_decl.return_type);
            zan_symbol_t *method_sym = make_symbol(b->arena, SYM_METHOD,
                member->method_decl.name, ret_type, member,
                member->method_decl.modifiers);

            /* 内部辅助逻辑 */
            for (int j = 0; j < member->method_decl.params.count; j++) {
                zan_ast_node_t *param = member->method_decl.params.items[j];
                zan_type_t *param_type = zan_binder_resolve_type(b, param->param.type);
                zan_symbol_t *param_sym = make_symbol(b->arena, SYM_PARAM,
                    param->param.name, param_type, param, 0);
                symbol_add_member(b->arena, method_sym, param_sym);
            }
            scope_add(b->arena, b->current_scope, method_sym);
            symbol_add_member(b->arena, type_sym, method_sym);
            check_member_name_clash(b, type_sym, method_sym);
            break;
        }
        case AST_CONSTRUCTOR_DECL: {
            zan_symbol_t *ctor_sym = make_symbol(b->arena, SYM_CONSTRUCTOR,
                member->method_decl.name, type_sym->type, member,
                member->method_decl.modifiers);

            for (int j = 0; j < member->method_decl.params.count; j++) {
                zan_ast_node_t *param = member->method_decl.params.items[j];
                zan_type_t *param_type = zan_binder_resolve_type(b, param->param.type);
                zan_symbol_t *param_sym = make_symbol(b->arena, SYM_PARAM,
                    param->param.name, param_type, param, 0);
                symbol_add_member(b->arena, ctor_sym, param_sym);
            }
            scope_add(b->arena, b->current_scope, ctor_sym);
            symbol_add_member(b->arena, type_sym, ctor_sym);
            check_member_name_clash(b, type_sym, ctor_sym);
            break;
        }
        case AST_ENUM_MEMBER: {
            /* 内部辅助逻辑 */
            if (member->enum_member.value) {
                int64_t v;
                if (fold_enum_member_value(b, type_sym,
                                           member->enum_member.value, &v)) {
                    if (!enum_value_fits_int(v)) {
                        zan_diag_emit(b->diag, DIAG_ERROR, member->loc,
                            "enum member '%.*s' value %lld does not fit in "
                            "'int' (enum members are 32-bit)",
                            member->enum_member.name.len,
                            member->enum_member.name.str, (long long)v);
                    }
                    zan_ast_node_t *lit = zan_ast_new(b->arena,
                        AST_INT_LITERAL, member->loc);
                    lit->int_val = v;
                    member->enum_member.value = lit;
                } else {
                    zan_diag_emit(b->diag, DIAG_ERROR, member->loc,
                        "enum member '%.*s' initializer must be an integer "
                        "constant expression",
                        member->enum_member.name.len,
                        member->enum_member.name.str);
                    member->enum_member.value = NULL;
                }
            }
            zan_symbol_t *em_sym = make_symbol(b->arena, SYM_ENUM_MEMBER,
                member->enum_member.name, type_sym->type, member, MOD_PUBLIC);
            scope_add(b->arena, b->current_scope, em_sym);
            symbol_add_member(b->arena, type_sym, em_sym);
            check_member_name_clash(b, type_sym, em_sym);
            break;
        }
        default:
            break;
        }
    }

    b->current_scope = saved;
}

void zan_binder_bind(zan_binder_t *b, zan_ast_node_t *unit) {
    if (!unit || unit->kind != AST_COMPILATION_UNIT) return;

    /* pass 1: collect type declarations */
    bind_type_decls(b, &unit->comp_unit.decls);

    /* Type parameters must be resolvable by every later pass: pass 1 */
    register_type_params(b, &unit->comp_unit.decls);

    /* pass 1 */
    bind_default_interface_methods(b, &unit->comp_unit.decls);

    /* pass 2: bind members of each type */
    for (int i = 0; i < unit->comp_unit.decls.count; i++) {
        zan_ast_node_t *decl = unit->comp_unit.decls.items[i];
        if (decl->kind == AST_CLASS_DECL || decl->kind == AST_STRUCT_DECL ||
            decl->kind == AST_INTERFACE_DECL || decl->kind == AST_ENUM_DECL) {
            bind_members(b, decl);
        }
    }

    /* 内部辅助逻辑 */
    for (int i = 0; i < unit->comp_unit.decls.count; i++) {
        zan_ast_node_t *decl = unit->comp_unit.decls.items[i];
        if (decl->kind == AST_CLASS_DECL || decl->kind == AST_STRUCT_DECL ||
            decl->kind == AST_INTERFACE_DECL) {
            resolve_bases(b, decl);
        }
    }

    /* 内部辅助逻辑 */
    validate_interface_contracts(b, &unit->comp_unit.decls);

    b->binding_done = true;
}
