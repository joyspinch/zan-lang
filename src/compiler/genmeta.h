/* genmeta */
#ifndef ZAN_GENMETA_H
#define ZAN_GENMETA_H

#include "arena.h"

struct zan_ast_node;
struct json_value;
struct zan_diag;

/* 内部辅助逻辑 */
char *zan_genmeta_export(struct zan_ast_node *unit);

/* 内部辅助逻辑 */
char *zan_genmeta_export_files(struct zan_ast_node *unit,
                               struct zan_diag *diag);

/* 内部辅助逻辑 */
struct zan_ast_node *zan_genmeta_find_call(struct zan_ast_node *unit, int id);

/* 底层系统交互与数据协议契约 */
int zan_genmeta_index_calls(struct zan_ast_node *unit,
                            struct zan_ast_node **nodes, int cap);

/* 内部辅助逻辑 */
struct zan_ast_node *zan_genmeta_expr_from_json(struct json_value *j,
                                                zan_arena_t *arena);

#endif /* ZAN_GENMETA_H */
