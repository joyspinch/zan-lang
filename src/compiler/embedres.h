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

#endif /* ZAN_EMBEDRES_H */
