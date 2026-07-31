/* ast.c -- AST utilities. */

#include "ast.h"
#include "arena.h"
#include <stdlib.h>
#include <string.h>

zan_ast_node_t *zan_ast_new(zan_arena_t *arena, zan_ast_kind_t kind, zan_loc_t loc) {
    zan_ast_node_t *node = (zan_ast_node_t *)zan_arena_alloc(arena, sizeof(zan_ast_node_t));
    node->kind = kind;
    node->loc = loc;
    zan_ast_list_init(&node->attributes);
    node->ns_name.str = NULL; node->ns_name.len = 0;
    node->orig_name.str = NULL; node->orig_name.len = 0;
    node->ns_usings = NULL;
    node->inst_type_ref = NULL;
    node->rt_type = NULL;
    node->rt_scope = NULL;
    return node;
}

/* True when `decl` carries the bare attribute `[name]`. */
bool zan_ast_has_attr(const zan_ast_node_t *decl, const char *name) {
    if (!decl) return false;
    size_t n = strlen(name);
    for (int i = 0; i < decl->attributes.count; i++) {
        zan_ast_node_t *a = decl->attributes.items[i];
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
