#ifndef ZAN_SUGGEST_H
#define ZAN_SUGGEST_H

/* S1 fix-it 的拼写建议（docs/ZAN_STANDARD.md §1）：在候选名里挑与写错的名字
 * 编辑距离最小的一个（相邻换位算一次），距离上限按长度（≤4 字符 1 次，更长
 * 2 次）。最小距离上有两个不同候选时不给建议——猜错的补丁比没有补丁更糟。 */
#include <stdbool.h>
#include <stddef.h>
#include "zan.h"
#include "diag.h"

typedef struct {
    const char *typo;
    size_t      len;
    const char *best;
    size_t      best_len;
    int         best_d;
    bool        tie;
} zan_suggest_t;

void zan_suggest_init(zan_suggest_t *s, const char *typo, size_t len);
void zan_suggest_offer(zan_suggest_t *s, const char *name, size_t len);
/* 有唯一最佳候选时返回 true 并给出名字 */
bool zan_suggest_result(const zan_suggest_t *s, const char **name, size_t *len);

/* 从 from 起在同一源文件里向前找标识符 word（整词匹配，最多 256 字节），
 * 找到时把它的位置写进 *out。诊断位置常落在 '.' 或表达式开头，补丁要精确
 * 落在名字上。 */
bool zan_diag_locate_word(const zan_diag_t *d, zan_loc_t from, const char *word,
                          size_t len, zan_loc_t *out);

#endif /* ZAN_SUGGEST_H */
