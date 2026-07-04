/* ast.c -- AST utilities. */

#include "ast.h"
#include "arena.h"
#include <stdlib.h>
#include <string.h>

zan_ast_node_t *zan_ast_new(zan_arena_t *arena, zan_ast_kind_t kind, zan_loc_t loc) {
    zan_ast_node_t *node = (zan_ast_node_t *)zan_arena_alloc(arena, sizeof(zan_ast_node_t));
    node->kind = kind;
    node->loc = loc;
    return node;
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
