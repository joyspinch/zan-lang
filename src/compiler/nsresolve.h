#ifndef ZAN_NSRESOLVE_H
#define ZAN_NSRESOLVE_H

#include "ast.h"
#include "arena.h"
#include "diag.h"

/* Stamp every top-level declaration in a freshly parsed compilation unit with
 * its file's namespace and `using` list, so that after all files are merged
 * into one flat unit the namespace context of each declaration is still known.
 * Call once per parsed file, before merging. */
void zan_nsresolve_stamp(zan_ast_node_t *unit, zan_arena_t *arena);

/* Namespace-aware type resolution pass.  Runs on the merged unit before the
 * binder.  Renames declarations whose simple name collides across namespaces
 * to a unique mangled name (original kept in node->orig_name) and rewrites all
 * type references to the resolved type's name, honoring the referring
 * declaration's namespace, its `using` imports and explicit qualified names.
 * Non-conflicting single-namespace code is left untouched. */
void zan_nsresolve_run(zan_ast_node_t *unit, zan_arena_t *arena, zan_diag_t *diag);

/* Reachability prune.  Runs right after zan_nsresolve_run on the merged unit.
 * Drops stdlib-authored top-level declarations (from_stdlib stamped by main.c)
 * that nothing reachable references: `using Gui.Widget;` globs in 79 widget
 * files even when the program instantiates one button, and every phase after
 * the parse (resolve/bind/check/irgen -- the dominant cost of a stdlib-heavy
 * build) then processes definitions that can never be called.  The closure
 * walks the same node kinds nsresolve walks (type refs, static receivers,
 * bases, attributes, generic arguments), so the declaration set the binder
 * consumes only loses definitions it could never resolve to.  User-authored
 * declarations are always kept: the program is the root set, which also keeps
 * compiler magic that reflects over the entry file (ORM bindings, generated
 * __JsonBind classes, genrun rewrites) working unchanged. */
void zan_nsresolve_prune(zan_ast_node_t *unit, zan_arena_t *arena,
                         zan_diag_t *diag);

#endif /* ZAN_NSRESOLVE_H */
