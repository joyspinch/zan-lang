/* formgen.h -- .zform design-document translation.
 *
 * `zanc App.zform App.zan` compiles the visual design as ordinary code: the
 * .zform JSON is translated to a synthetic `partial class <Name>` -- strongly
 * typed widget fields, a __BuildForm() that assembles the real control tree,
 * event wiring through the Form handler registry, and a Main() -- then parsed
 * and merged into the compilation unit exactly like the other input files.
 *
 * No generated source file is written or kept: the .zform is the single source
 * of truth and the synthetic class is a compile-time projection, so the design
 * and the code cannot drift. (Same pattern as jsongen's synthetic __JsonBind
 * class.) The IDE therefore needs no .g.zan anymore: the designer writes the
 * .zform, the business file references the typed fields, and the compiler
 * synthesises the rest on every build.
 */
#ifndef ZAN_FORMGEN_H
#define ZAN_FORMGEN_H

#include <stddef.h>

struct zan_diag;

/* Translate a .zform design document (JSON text) into synthetic .zan source.
 * Returns a malloc'd NUL-terminated buffer (caller frees), or NULL when the
 * document is not valid design JSON. `file_name` is used in diagnostics and
 * in the generated header comment. `emit_main` adds the Main() entry point:
 * pass it for the primary input only, so a multi-window project can compile
 * every design at once (the others are opened with `<Name>.Show();`). */
char *zan_formgen_translate(const char *json, size_t json_len,
                            const char *file_name, struct zan_diag *diag,
                            int emit_main);

#endif /* ZAN_FORMGEN_H */
