/* jsongen.h -- compile-time entity mapper generation for System.Json.
 *
 * Scans the merged compilation unit for Json.Deserialize<T>(...) /
 * Json.Serialize<T>(...) call sites, generates per-class binder/serializer
 * methods (source-generation, like the event-holder desugar), and rewrites
 * the call sites to target the generated methods. Runs after parsing and
 * before binding.
 */

#ifndef ZAN_JSONGEN_H
#define ZAN_JSONGEN_H

#include "zan.h"
#include "ast.h"

void zan_jsongen_run(zan_ast_node_t *unit, zan_arena_t *arena,
                     zan_diag_t *diag);

#endif /* ZAN_JSONGEN_H */
