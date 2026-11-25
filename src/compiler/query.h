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

/* S4 效果列：全程序一遍算出每个方法可能抛出的异常类型（上界）与用到的能力，
 * describe 写出 "throws A, B; uses File"（都没有则空串）。 */
typedef struct zan_effects zan_effects_t;
zan_effects_t *zan_effects_build(zan_ast_node_t *unit, zan_binder_t *binder);
int  zan_effects_describe(const zan_effects_t *E, const zan_ast_node_t *method_decl,
                          char *buf, size_t cap);
void zan_effects_free(zan_effects_t *E);

#endif /* ZAN_QUERY_H */
