/* suggest.c: S1 fix-it 的拼写建议与名字定位（见 suggest.h）。 */
#include "suggest.h"

#include <ctype.h>
#include <string.h>

static int sg_lower(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }

/* 最优字符串对齐距离（Damerau-Levenshtein 受限版）；超过 cap 提前返回 cap+1 */
static int sg_distance(const char *a, size_t al, const char *b, size_t bl, int cap) {
    if (al > 64 || bl > 64) return cap + 1;
    size_t diff = al > bl ? al - bl : bl - al;
    if ((int)diff > cap) return cap + 1;
    int d[65][65];
    for (size_t i = 0; i <= al; i++) d[i][0] = (int)i;
    for (size_t j = 0; j <= bl; j++) d[0][j] = (int)j;
    for (size_t i = 1; i <= al; i++) {
        int row_min = cap + 1;
        for (size_t j = 1; j <= bl; j++) {
            int cost = sg_lower((unsigned char)a[i - 1]) == sg_lower((unsigned char)b[j - 1]) ? 0 : 1;
            int v = d[i - 1][j] + 1;
            if (d[i][j - 1] + 1 < v) v = d[i][j - 1] + 1;
            if (d[i - 1][j - 1] + cost < v) v = d[i - 1][j - 1] + cost;
            if (i > 1 && j > 1 && a[i - 1] == b[j - 2] && a[i - 2] == b[j - 1] &&
                d[i - 2][j - 2] + 1 < v)
                v = d[i - 2][j - 2] + 1;
            d[i][j] = v;
            if (v < row_min) row_min = v;
        }
        if (row_min > cap) return cap + 1;
    }
    return d[al][bl];
}

void zan_suggest_init(zan_suggest_t *s, const char *typo, size_t len) {
    memset(s, 0, sizeof(*s));
    s->typo = typo;
    s->len = len;
    s->best_d = (len <= 4 ? 1 : 2) + 1;
}

void zan_suggest_offer(zan_suggest_t *s, const char *name, size_t len) {
    if (!name || !len || !s->typo || !s->len) return;
    if (len == s->len && memcmp(name, s->typo, len) == 0) return;
    /* 编译器合成的名字不当建议 */
    if ((len > 4 && (memcmp(name, "get_", 4) == 0 || memcmp(name, "set_", 4) == 0)) ||
        (len > 3 && memcmp(name, "op_", 3) == 0) || (len > 1 && name[0] == '_' && name[1] == '_'))
        return;
    int cap = s->len <= 4 ? 1 : 2;
    int dist = sg_distance(s->typo, s->len, name, len, cap);
    if (dist > cap) return;
    if (dist < s->best_d) {
        s->best = name;
        s->best_len = len;
        s->best_d = dist;
        s->tie = false;
    } else if (dist == s->best_d && s->best &&
               !(len == s->best_len && memcmp(name, s->best, len) == 0)) {
        s->tie = true;
    }
}

bool zan_suggest_result(const zan_suggest_t *s, const char **name, size_t *len) {
    if (!s->best || s->tie) return false;
    *name = s->best;
    *len = s->best_len;
    return true;
}

static bool sg_ident_char(int c) { return isalnum(c) || c == '_' || c >= 0x80; }

bool zan_diag_locate_word(const zan_diag_t *d, zan_loc_t from, const char *word,
                          size_t len, zan_loc_t *out) {
    if (!d || !d->file_sources || (int)from.file_id >= d->file_count || !len) return false;
    const char *src = d->file_sources[from.file_id];
    if (!src) return false;
    size_t n = strlen(src);
    if (from.offset >= n) return false;
    zan_loc_t cur = from;
    for (size_t k = 0; k < 256 && cur.offset + len <= n; k++) {
        const char *p = src + cur.offset;
        if (memcmp(p, word, len) == 0 &&
            (cur.offset == 0 || !sg_ident_char((unsigned char)p[-1])) &&
            !sg_ident_char((unsigned char)p[len])) {
            *out = cur;
            return true;
        }
        if (*p == '\n') { cur.line++; cur.col = 1; }
        else cur.col++;
        cur.offset++;
    }
    return false;
}
