/* genmeta.h -- generic compilation-unit metadata exporter.
 *
 * Feeds the Zan-scripted code generators (see genrun.h). The whole unit is
 * reduced to plain JSON -- type shapes, attribute usages, and every
 * member-access call site with its argument shapes (string/integer literals,
 * identifiers, lambdas with a full expression tree) and file:line locations.
 * The exporter deliberately knows nothing about any framework: the
 * generators decide which shapes mean something. Rewrite directives the
 * generators return are keyed by the call-site `id` assigned here. */
#ifndef ZAN_GENMETA_H
#define ZAN_GENMETA_H

#include "arena.h"

struct zan_ast_node;
struct json_value;
struct zan_diag;

/* Serialize the unit's metadata to a malloc'd JSON string (caller frees),
 * or NULL on allocation failure. */
char *zan_genmeta_export(struct zan_ast_node *unit);

/* Same, plus a "files" array naming every source file, so the `file` ids the
 * export carries on locations resolve to paths. Tooling that indexes a project
 * ("which file declares this entity") needs the table; the generators do not,
 * and zan_genmeta_export keeps their input unchanged. */
char *zan_genmeta_export_files(struct zan_ast_node *unit,
                               struct zan_diag *diag);

/* Locate the `id`-th member call site (ids start at 1, in the same traversal
 * order the export walks) -- the node the generators' rewrite directives
 * address. Returns NULL when no such call site exists. */
struct zan_ast_node *zan_genmeta_find_call(struct zan_ast_node *unit, int id);

/* Snapshot the call-site nodes: nodes[k-1] gets the node of id k, for
 * k <= cap. Returns the total call-site count (call twice: once with
 * nodes=NULL to size, once to fill). Unlike find_call, the snapshot is
 * immune to earlier rewrites in the same pass. */
int zan_genmeta_index_calls(struct zan_ast_node *unit,
                            struct zan_ast_node **nodes, int cap);

/* Rebuild an AST expression subtree from the JSON tree a rewrite directive
 * embeds (gm_expr_tree's inverse; see genmeta.c for the node shape). Returns
 * NULL for an unknown shape. New nodes are allocated from `arena`; their
 * source location is empty (0:0:0). */
struct zan_ast_node *zan_genmeta_expr_from_json(struct json_value *j,
                                                zan_arena_t *arena);

#endif /* ZAN_GENMETA_H */
