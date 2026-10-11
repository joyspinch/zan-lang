/* genmeta */
#ifndef ZAN_GENMETA_H
#define ZAN_GENMETA_H

#include "arena.h"

struct zan_ast_node;
struct json_value;
struct zan_diag;

char *zan_genmeta_export(struct zan_ast_node *unit);

char *zan_genmeta_export_files(struct zan_ast_node *unit,
                               struct zan_diag *diag);

struct zan_ast_node *zan_genmeta_find_call(struct zan_ast_node *unit, int id);

int zan_genmeta_index_calls(struct zan_ast_node *unit,
                            struct zan_ast_node **nodes, int cap);

struct zan_ast_node *zan_genmeta_expr_from_json(struct json_value *j,
                                                zan_arena_t *arena);

#endif /* ZAN_GENMETA_H */
