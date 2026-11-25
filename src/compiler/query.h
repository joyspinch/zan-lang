#ifndef ZAN_QUERY_H
#define ZAN_QUERY_H

/* S2（docs/ZAN_STANDARD.md §1）：zanc --query <符号>——类型检查后对匹配的方法输出
 * 分配、捕获、可能抛出、能力、调用者（JSON Lines，见 query.c 文件头）。
 * 返回 0 = 有匹配，1 = 无匹配。 */
#include <stdio.h>
#include "ast.h"
#include "binder.h"
#include "diag.h"

int zan_query_emit(zan_ast_node_t *unit, zan_binder_t *binder, const zan_diag_t *diag,
                   const char *query, FILE *out);

#endif /* ZAN_QUERY_H */
