
#ifndef ZAN_SYMBOLS_H
#define ZAN_SYMBOLS_H

#include "ast.h"

int zan_symbols_emit(zan_ast_node_t *unit, const char *path);

/* S4：zanc --summary —— 显式输入源文件的公开接口摘要（file_id < explicit_count），
 * 每成员一行 C# 形态签名 + [效果] + 一行文档。diag 提供源码文本取文档注释；
 * fx（zan_effects_build，可为 NULL）给方法补 "throws …; uses …" 效果列。 */
#include <stdio.h>
#include "diag.h"
struct zan_effects;
int zan_summary_emit(zan_ast_node_t *unit, const zan_diag_t *diag, int explicit_count,
                     const struct zan_effects *fx, FILE *out);

/* 签名打印（S2 query 复用）：类型引用节点 → "List<int>[]?"；方法 → "ret Name(T a, ...)" */
int  zan_symbols_type_name(const zan_ast_node_t *t, char *buf, int cap);
void zan_symbols_method_sig(const zan_ast_node_t *m, char *buf, int cap);

#endif /* ZAN_SYMBOLS_H */
