/* routegen.h -- compile-time attribute-driven route generation.
 *
 * Scans the merged compilation unit for controller classes (name ends with
 * "Controller", derives from Controller/ApiController, or carries a [Route]/
 * [ApiController] attribute) and their action methods (carrying an HTTP-verb
 * attribute or a [Route]), evaluates the C#-style attributes attached to the
 * class and the actions at compile time (three-layer merge: attribute-class
 * defaults < class-level attributes < method-level attributes, method wins),
 * resolves [controller]/[action] route-template tokens, and generates a
 * synthetic `__AttrRoutes` class (per-action trampolines + a Register(app)
 * that populates the router). The generated source is parsed and merged into
 * the unit, so no reflection, no runtime type info, and no external tool are
 * needed. Runs after parsing and before binding (next to jsongen).
 */

#ifndef ZAN_ROUTEGEN_H
#define ZAN_ROUTEGEN_H

#include "zan.h"
#include "ast.h"

void zan_routegen_run(zan_ast_node_t *unit, zan_arena_t *arena,
                      zan_diag_t *diag);

#endif /* ZAN_ROUTEGEN_H */
