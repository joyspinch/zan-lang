
#ifndef ZAN_SYMBOLS_H
#define ZAN_SYMBOLS_H

#include "ast.h"

int zan_symbols_emit(zan_ast_node_t *unit, const char *path);

/* S4：zanc --summary —— 显式输入源文件的公开接口摘要（file_id < explicit_count），
 * 每成员一行 C# 形态签名 + 一行文档。diag 提供源码文本取文档注释。 */
#include <stdio.h>
#include "diag.h"
int zan_summary_emit(zan_ast_node_t *unit, const zan_diag_t *diag, int explicit_count,
                     FILE *out);

#endif /* ZAN_SYMBOLS_H */
