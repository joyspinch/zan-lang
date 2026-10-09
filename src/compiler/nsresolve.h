#ifndef ZAN_NSRESOLVE_H
#define ZAN_NSRESOLVE_H

#include "ast.h"
#include "arena.h"
#include "diag.h"

/* 标记顶层声明所属文件命名空间与 using 列表，供多文件合并后保留命名空间上下文 */
void zan_nsresolve_stamp(zan_ast_node_t *unit, zan_arena_t *arena);

/* 命名空间类型解析：消解跨命名空间同名类型冲突，重写类型引用 */
void zan_nsresolve_run(zan_ast_node_t *unit, zan_arena_t *arena, zan_diag_t *diag);

/* 可达性裁剪：按需剔除未被引用的标准库顶层声明，保留用户声明 */
void zan_nsresolve_prune(zan_ast_node_t *unit, zan_arena_t *arena,
                         zan_diag_t *diag);

#endif /* ZAN_NSRESOLVE_H */
