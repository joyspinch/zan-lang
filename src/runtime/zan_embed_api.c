/* 核心系统底层抽象与内存语义契约 */
#include <string.h>
#include <stdlib.h>

#include "rt_timer.h"   /* 底层系统交互与数据协议契约 */

#include "../common/host_oom.h"

typedef struct {
    const char*          name;
    const unsigned char* data;
    long long            len;
} zan_embed_ent;

static const zan_embed_ent* g_tab = 0;
static long long            g_cnt = 0;

/* 底层系统交互与数据协议契约 */
void zan_embed_register(const zan_embed_ent* tbl, long long n) {
    g_tab = tbl;
    g_cnt = n;
}

static const zan_embed_ent* zan_embed_find(const char* name) {
    long long i;
    if (!name || !g_tab) return 0;
    for (i = 0; i < g_cnt; i++)
        if (g_tab[i].name && strcmp(g_tab[i].name, name) == 0)
            return &g_tab[i];
    return 0;
}

/* 底层系统交互与数据协议契约 */
const char* zan_embed_read(const char* name) {
    const zan_embed_ent* e = zan_embed_find(name);
    return e ? (const char*)e->data : "";
}

int zan_embed_has(const char* name) {
    return zan_embed_find(name) ? 1 : 0;
}

/* 底层系统交互与数据协议契约 */
const unsigned char* zan_embed_bytes(const char* name, int* outLen) {
    const zan_embed_ent* e = zan_embed_find(name);
    if (!e) { if (outLen) *outLen = 0; return 0; }
    if (outLen) *outLen = (int)e->len;
    return e->data;
}

/* 底层系统交互与数据协议契约 */
const char* zan_embed_list(const char* prefix) {
    static char* buf = 0;
    static size_t cap = 0;
    size_t pl = prefix ? strlen(prefix) : 0;
    size_t need = 1;
    long long i;
    for (i = 0; i < g_cnt; i++) {
        const char* nm = g_tab[i].name;
        if (!nm) continue;
        if (pl && strncmp(nm, prefix, pl) != 0) continue;
        need += strlen(nm) + 1;
    }
    if (need > cap) {
        char* nb = (char*)realloc(buf, need);
        if (!nb) return "";
        buf = nb; cap = need;
    }
    {
        size_t o = 0;
        for (i = 0; i < g_cnt; i++) {
            const char* nm = g_tab[i].name;
            if (!nm) continue;
            if (pl && strncmp(nm, prefix, pl) != 0) continue;
            {
                size_t l = strlen(nm);
                memcpy(buf + o, nm, l); o += l;
                buf[o++] = '\n';
            }
        }
        buf[o] = '\0';
    }
    return buf;
}
