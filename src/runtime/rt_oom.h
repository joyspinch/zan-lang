#ifndef ZAN_RT_OOM_H
#define ZAN_RT_OOM_H

/* OOM 注入钩子（B-ID98）：给运行时内部分配路径一个确定性的失败开关，
 * 让 fail-fast/降级/恢复路径有 conformance 直钉，不再只靠代码审查覆盖。
 *
 * 用法：环境变量 ZAN_OOM_INJECT_SITE=<site>[:<count>][,<site2>[:<count2>]…]
 *   - <site> 是调用点标签（如 "sync.monitor_block"）；
 *   - [:K] 可选，表示该点前 K 次分配返回 NULL（缺省 1，一次性）；
 *   - 未命中/未设环境变量时零额外开销（一次整数判断直达真分配器）。
 *
 * 约束：仅本头所在的 TU 生效（宏把 malloc/calloc 换成本文件闸门）；
 * (malloc)(x) 括号形式绕过宏取真分配器，避免递归。 */

#include <stdlib.h>
#include <string.h>

#define ZAN_OOM_MAX_SITES 8

typedef struct {
    const char *site;
    int remaining; /* 剩余注入次数；<=0 表示不再注错 */
} zan_oom_site_t;

static zan_oom_site_t zan_oom_sites[ZAN_OOM_MAX_SITES];
static int zan_oom_site_count = -1; /* -1 = 尚未解析环境变量 */

__attribute__((constructor)) static void zan_oom_env_parse(void) {
    const char *e = getenv("ZAN_OOM_INJECT_SITE");
    zan_oom_site_count = 0;
    if (!e || !*e) return;
    /* 解析 "site[:k],site2[:k2]"——strtok 式手工扫描，容忍空段 */
    char buf[256];
    size_t n = strlen(e);
    if (n >= sizeof(buf)) n = sizeof(buf) - 1;
    memcpy(buf, e, n);
    buf[n] = '\0';
    char *p = buf;
    while (*p && zan_oom_site_count < ZAN_OOM_MAX_SITES) {
        while (*p == ',') p++;
        if (!*p) break;
        char *seg = p;
        while (*p && *p != ',') p++;
        if (*p) *p++ = '\0';
        char *colon = strchr(seg, ':');
        int k = 1;
        if (colon) {
            *colon = '\0';
            k = atoi(colon + 1);
            if (k < 0) k = 0;
        }
        if (*seg) {
            size_t slen = strlen(seg);
            char *copy = (char *)(malloc)(slen + 1);
            if (copy) {
                memcpy(copy, seg, slen + 1);
                zan_oom_sites[zan_oom_site_count].site = copy;
                zan_oom_sites[zan_oom_site_count].remaining = k;
                zan_oom_site_count++;
            }
        }
    }
}

/* 命中返回 1（调用方把 NULL 当分配结果），未命中返回 0 */
static int zan_oom_hit(const char *site) {
    if (zan_oom_site_count <= 0) return 0;
    for (int i = 0; i < zan_oom_site_count; i++) {
        if (zan_oom_sites[i].remaining > 0 &&
            strcmp(zan_oom_sites[i].site, site) == 0) {
            zan_oom_sites[i].remaining--;
            return 1;
        }
    }
    return 0;
}

/* 闸门：site 命中 → 返回 NULL（不触碰真分配器）；否则直通。
 * 用宏展开点带 site 字面量，避免每次调用拼参数。 */
#define ZAN_OOM_MALLOC(site, n) \
    (zan_oom_hit(site) ? (void *)NULL : (malloc)(n))
#define ZAN_OOM_CALLOC(site, n, sz) \
    (zan_oom_hit(site) ? (void *)NULL : (calloc)(n, sz))

#endif /* ZAN_RT_OOM_H */
