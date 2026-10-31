/* embedres */

#ifndef ZAN_EMBEDRES_H
#define ZAN_EMBEDRES_H

#include <stddef.h>

#include "irgen.h"

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

#endif /* ZAN_EMBEDRES_H */
