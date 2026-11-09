/* embedres */

#ifndef ZAN_EMBEDRES_H
#define ZAN_EMBEDRES_H

#include <stddef.h>

#include "irgen.h"

typedef struct {
    const char *spec;
    const char *const *subdirs;
    int subdir_count;
} zan_embed_spec_t;

int zan_embed_emit_spec_list(zan_irgen_t *g, const zan_embed_spec_t *specs, int count);

int zan_embed_emit_specs(zan_irgen_t *g, const char *const *specs, int count);

int zan_embed_emit_specs_filtered(zan_irgen_t *g, const char *const *specs,
                                  int count, const char *const *filter,
                                  int filter_count);

#define ZAN_EMBED_DRIVER_PREFIX "zan-drivers"

int zan_embed_driver_spec(const char *path, const char *file, char *out,
                          size_t out_sz);

/* 发布态设计稿自动发现：扫 <project_root>/src 下的 *.html/*.htm，
 * 产出 "<绝对路径>=<工程根相对路径>" 形式的 embed spec。返回条数，
 * 超过 max_specs 截断并返回 -1（调用方负责告警）。 */
int zan_embed_collect_design_html(const char *project_root,
                                  char (*specs)[1300], int max_specs);

/* 发布态设计稿事件处理器名收集：扫同一批 html 的 @x / v-on:x / z-on:x /
 * data-on-x / onx 属性（口径同 Html.Parse 建树期归一），去重后写入
 * *out_names（malloc，进程期持有）。这些方法运行期只经 Invoke 按名调用，
 * 静态上不可见，publish 须据此保留反射条目与 thunk。返回个数。 */
int zan_embed_collect_html_handlers(const char *project_root, char ***out_names);

#endif /* ZAN_EMBEDRES_H */
