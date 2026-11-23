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

int zan_embed_emit_spec_list(zan_irgen_t *g, const zan_embed_spec_t *specs,
                             int count);

/* Visits the same names and bytes the emitter uses for a resource spec. */
typedef bool (*zan_embed_visit_fn)(const char *name, const char *text,
                                   size_t length, void *context);
bool zan_embed_visit_spec(const char *spec, zan_embed_visit_fn visit,
                          void *context);

int zan_embed_emit_specs(zan_irgen_t *g, const char *const *specs, int count);

int zan_embed_emit_specs_filtered(zan_irgen_t *g, const char *const *specs,
                                  int count, const char *const *filter,
                                  int filter_count);

#define ZAN_EMBED_DRIVER_PREFIX "zan-drivers"

int zan_embed_driver_spec(const char *path, const char *file, char *out,
                          size_t out_sz);

#endif /* ZAN_EMBEDRES_H */
