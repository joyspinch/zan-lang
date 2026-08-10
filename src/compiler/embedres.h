/* embedres.h -- baking project files into the produced executable. */

#ifndef ZAN_EMBEDRES_H
#define ZAN_EMBEDRES_H

#include "irgen.h"

/* Bakes the files named by `specs` (each "<path>" or "<path>=<prefix>", a file
 * or a directory that is walked recursively) into the module as embedded
 * resources, registered with the runtime's zan_embed_* registry before main
 * runs. Resources are looked up by "<prefix>/<path relative to the spec root>",
 * with the spec root's own name as the default prefix. Returns the number of
 * embedded files, or -1 when a spec names nothing readable. */
int zan_embed_emit_specs(zan_irgen_t *g, const char *const *specs, int count);

#endif /* ZAN_EMBEDRES_H */
