/* scenegen.h -- .zscene design-document translation.
 *
 * `zanc Level1.zscene Level1.zan` compiles a scene design as ordinary code:
 * the .zscene JSON is translated to a synthetic `partial class <Name>` --
 * typed SceneElement fields, a BuildScene() that rebuilds the designed
 * SceneDoc, a Run() that renders it through Game.Scene.SceneView, and a
 * Main() -- then parsed and merged into the compilation unit like any source
 * file.
 *
 * No generated source file is written: the .zscene is the single source of
 * truth (same two-file model as .zform/formgen), so a scene and its code
 * cannot drift and the IDE keeps no Name.g.zan around.
 */
#ifndef ZAN_SCENEGEN_H
#define ZAN_SCENEGEN_H

#include <stddef.h>

struct zan_diag;

/* Translate a .zscene design document (JSON text) into synthetic .zan source.
 * Returns a malloc'd NUL-terminated buffer (caller frees), or NULL when the
 * document is not valid scene JSON. `file_name` supplies the class name when
 * the document has none. `emit_main` adds the Main() entry point: pass it for
 * the primary input only, so several designs can compile together with
 * exactly one entry (the others are opened with `<Name>.Run();`). */
char *zan_scenegen_translate(const char *json, size_t json_len,
                             const char *file_name, struct zan_diag *diag,
                             int emit_main);

#endif /* ZAN_SCENEGEN_H */
