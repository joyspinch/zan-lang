/* definite.h -- Definite-assignment analysis for method bodies. */

#ifndef ZAN_DEFINITE_H
#define ZAN_DEFINITE_H

#include "zan.h"
#include "ast.h"

/* Reports every read of a local that no execution path has written yet, and
 * every `out` parameter left unwritten on a path that leaves the method.
 * `method` is an AST_METHOD_DECL / AST_CONSTRUCTOR_DECL / AST_DESTRUCTOR_DECL
 * with a body; anything else is ignored. */
void zan_definite_check(zan_diag_t *diag, zan_ast_node_t *method);

#endif /* ZAN_DEFINITE_H */
