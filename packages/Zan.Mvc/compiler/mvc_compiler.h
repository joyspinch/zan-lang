#ifndef ZAN_MVC_COMPILER_H
#define ZAN_MVC_COMPILER_H

#include "ast.h"

/* Internal C compiler adapter. The driver and checker share the AST protocol. */
typedef enum {
    ZAN_MVC_REGISTER_NONE,
    ZAN_MVC_REGISTER_APP,
    ZAN_MVC_REGISTER_FRAMEWORK,
} zan_mvc_registration_t;

zan_mvc_registration_t zan_mvc_registration(zan_ast_node_t *call);
zan_istr_t zan_mvc_simple_name(zan_ast_node_t *decl);
bool zan_mvc_framework_controller(zan_ast_node_t *unit, zan_ast_node_t *decl);

/* Initialize to zero with the checker. Controller names retain the identity
 * assigned by namespace resolution and use the checker's arena. */
typedef struct {
    zan_istr_t *names;
    int count;
    int capacity;
    bool collected;
} zan_mvc_route_state_t;

void zan_mvc_collect_controllers(zan_mvc_route_state_t *routes,
                                 zan_ast_node_t *unit, zan_arena_t *arena);
void zan_mvc_rewrite_route_call(const zan_mvc_route_state_t *routes,
                                zan_ast_node_t *call, zan_arena_t *arena);

/* Pull-in recognizes these source spellings. Namespace pruning passes a
 * resolved declaration and keeps the MVC roots selected by its own policy. */
bool zan_mvc_has_controller_suffix(zan_istr_t name);
bool zan_mvc_is_route_pullin_attribute(zan_istr_t name);
bool zan_mvc_keep_namespace_decl(zan_ast_node_t *decl);

/* Append the MVC Param= payload to the NUL-terminated method attributes. */
void zan_mvc_append_method_params(zan_binder_t *binder, zan_ast_node_t *decl,
                                  char *buf, size_t cap);

/* Driver hook between source pull-in and final namespace resolution. */
void zan_mvc_synthesize_dispatch(zan_ast_node_t *unit, zan_arena_t *arena,
                                 zan_diag_t *diag);

#endif
