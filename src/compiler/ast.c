/* 核心系统底层抽象与内存语义契约 */

#include "ast.h"
#include "arena.h"
#include <stdlib.h>
#include <string.h>

_Static_assert(sizeof(zan_ast_node_t) <= 120, "AST node layout regressed");

static size_t g_ast_node_count;

size_t zan_ast_node_count(void) { return g_ast_node_count; }

zan_method_ext_t *zan_ast_ensure_method_ext(zan_ast_node_t *n, zan_arena_t *arena) {
    if (!n) return NULL;
    if (!n->method_decl.ext) {
        n->method_decl.ext = (zan_method_ext_t *)zan_arena_alloc(arena, sizeof(zan_method_ext_t));
        memset(n->method_decl.ext, 0, sizeof(zan_method_ext_t));
    }
    return n->method_decl.ext;
}

zan_decl_meta_t *zan_ast_ensure_decl_meta(zan_ast_node_t *n, zan_arena_t *arena) {
    if (!n) return NULL;
    if (!n->meta) {
        n->meta = (zan_decl_meta_t *)zan_arena_alloc(arena, sizeof(zan_decl_meta_t));
        memset(n->meta, 0, sizeof(zan_decl_meta_t));
    }
    return n->meta;
}

zan_ast_node_t *zan_ast_new(zan_arena_t *arena, zan_ast_kind_t kind, zan_loc_t loc) {
    zan_ast_node_t *node = (zan_ast_node_t *)zan_arena_alloc(arena, sizeof(zan_ast_node_t));
    g_ast_node_count++;
    node->kind = kind;
    node->loc = loc;
    node->lit_suffix = 0;
    node->meta = NULL;
    if (kind == AST_ASSIGNMENT || kind == AST_BINARY) {
        /* 底层系统交互与数据协议契约 */
        node->binary.compound_base = TK_EOF;
    }
    if (kind == AST_METHOD_DECL || kind == AST_CONSTRUCTOR_DECL) {
        node->method_decl.is_task_return = false;
        node->method_decl.ext = NULL;
    } else if (kind == AST_CLASS_DECL || kind == AST_STRUCT_DECL ||
               kind == AST_INTERFACE_DECL || kind == AST_ENUM_DECL) {
        node->type_decl.where_clauses = NULL;
    }
    return node;
}

/* 底层系统交互与数据协议契约 */
bool zan_ast_has_attr(const zan_ast_node_t *decl, const char *name) {
    if (!decl || !decl->meta) return false;
    size_t n = strlen(name);
    for (int i = 0; i < decl->meta->attributes.count; i++) {
        zan_ast_node_t *a = decl->meta->attributes.items[i];
        if (!a || a->kind != AST_ATTRIBUTE || !a->attribute.name) continue;
        if (a->attribute.name->kind != AST_IDENTIFIER) continue;
        zan_istr_t s = a->attribute.name->ident.name;
        if (s.len == (uint32_t)n && memcmp(s.str, name, n) == 0) return true;
    }
    return false;
}

void zan_ast_list_init(zan_ast_list_t *list) {
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}

void zan_ast_list_push(zan_ast_list_t *list, zan_ast_node_t *node, zan_arena_t *arena) {
    if (list->count >= list->capacity) {
        int new_cap = list->capacity < 4 ? 4 : list->capacity * 2;
        zan_ast_node_t **new_items = (zan_ast_node_t **)zan_arena_alloc(
            arena, sizeof(zan_ast_node_t *) * (size_t)new_cap);
        if (list->items && list->count > 0) {
            memcpy(new_items, list->items, sizeof(zan_ast_node_t *) * (size_t)list->count);
        }
        list->items = new_items;
        list->capacity = new_cap;
    }
    list->items[list->count++] = node;
}
