/* Generic embedded-resource read API.
 *
 * A generated data object (see scripts/gen_embed.ps1) registers its
 * name->bytes table here via a constructor before main runs; skins, DB drivers
 * and other extension assets are then read straight from the executable's
 * memory by name -- no self-extract, no external files. When no table has been
 * registered every read returns empty, so callers transparently fall back to
 * the filesystem.
 *
 * A registration REPLACES the previous one (as does the copy zanc emits into an
 * embedding program, src/compiler/embedres.c), so a program links exactly one
 * generated object: gen_embed.ps1 takes several -Group arguments to put every
 * resource group in that one table.
 *
 * Kept as its own translation unit (not baked into a specific runtime) so it is
 * a single definition every consumer links against, while the *data* lives in
 * the per-program generated object. */
#include <string.h>
#include <stdlib.h>

#include "../common/host_oom.h"

typedef struct {
    const char*          name;
    const unsigned char* data;
    long long            len;
} zan_embed_ent;

static const zan_embed_ent* g_tab = 0;
static long long            g_cnt = 0;

/* Called by the generated data object's constructor. */
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

/* NUL-terminated payload (text resources); "" when absent. */
const char* zan_embed_read(const char* name) {
    const zan_embed_ent* e = zan_embed_find(name);
    return e ? (const char*)e->data : "";
}

int zan_embed_has(const char* name) {
    return zan_embed_find(name) ? 1 : 0;
}

/* Raw pointer + length (binary resources such as driver blobs / images). */
const unsigned char* zan_embed_bytes(const char* name, int* outLen) {
    const zan_embed_ent* e = zan_embed_find(name);
    if (!e) { if (outLen) *outLen = 0; return 0; }
    if (outLen) *outLen = (int)e->len;
    return e->data;
}

/* '\n'-joined names carrying the given prefix (for enumeration). */
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
