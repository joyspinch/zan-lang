/* dbgen.h -- compile-time typed ORM query generation for System.Data. */

#ifndef ZAN_DBGEN_H
#define ZAN_DBGEN_H

#include "ast.h"
#include "arena.h"
#include "diag.h"

/* Lowers `db.Query<T>()` fluent chains: generates per-entity query classes
 * (`__DbQ_T`) with row mapping, translates Where/OrderBy/Include lambdas
 * into parameterized SQL, and rewrites the call sites. Runs after parsing
 * (before binding), like jsongen. */
void zan_dbgen_run(zan_ast_node_t *unit, zan_arena_t *arena, zan_diag_t *diag);

#endif /* ZAN_DBGEN_H */
