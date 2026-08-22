/* embedres.h -- baking project files into the produced executable. */

#ifndef ZAN_EMBEDRES_H
#define ZAN_EMBEDRES_H

#include <stddef.h>

#include "irgen.h"

/* Bakes the files named by `specs` (each "<path>" or "<path>=<prefix>", a file
 * or a directory that is walked recursively) into the module as embedded
 * resources, registered with the runtime's zan_embed_* registry before main
 * runs. Resources are looked up by "<prefix>/<path relative to the spec root>",
 * with the spec root's own name as the default prefix. Returns the number of
 * embedded files, or -1 when a spec names nothing readable. */
int zan_embed_emit_specs(zan_irgen_t *g, const char *const *specs, int count);

/* Resource-name prefix under which a run-time loaded native driver travels
 * inside the executable ("zan-drivers/<fp>/zan_cef.dll"). The stdlib module that
 * dlopens such a driver writes it out from this name, so the two sides must
 * agree; see Gui.Component.CefBrowser.CefBackend. */
#define ZAN_EMBED_DRIVER_PREFIX "zan-drivers"

/* Writes the --embed spec for the run-time driver at `path` (whose loadable
 * file name is `file`) into `out`. The resource name carries a fingerprint of
 * the driver's bytes -- "zan-drivers/<fp>/<file>" -- so the copy one build
 * extracts on the target is never mistaken for another build's, and the loading
 * module finds the name via zan_embed_list. Returns 0 on success. */
int zan_embed_driver_spec(const char *path, const char *file, char *out,
                          size_t out_sz);

#endif /* ZAN_EMBEDRES_H */
