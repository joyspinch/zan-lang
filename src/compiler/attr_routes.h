#ifndef ZAN_ATTR_ROUTES_H
#define ZAN_ATTR_ROUTES_H

#include "ast.h"
#include <string.h>

/* Shared driver/checker protocol. Framework exposure is the explicit typeof
 * allowlist in __AttrRoutes.RegisterFramework, never a namespace heuristic. */
static inline bool zan_routes_name_eq(zan_istr_t name, const char *text) {
    return name.str && name.len == strlen(text) &&
           memcmp(name.str, text, name.len) == 0;
}

static inline zan_istr_t zan_routes_simple_name(zan_ast_node_t *decl) {
    zan_istr_t orig = zan_ast_orig_name(decl);
    return orig.len ? orig : decl->type_decl.name;
}

static inline bool zan_routes_receiver(zan_ast_node_t *obj) {
    if (!obj) return false;
    if (obj->kind == AST_IDENTIFIER)
        return zan_routes_name_eq(obj->ident.name, "__AttrRoutes");
    return obj->kind == AST_MEMBER_ACCESS &&
           zan_routes_name_eq(obj->member.name, "__AttrRoutes");
}

/* 1 = application registration, 2 = explicit framework registration. */
static inline int zan_routes_registration(zan_ast_node_t *call) {
    if (!call || call->kind != AST_CALL || !call->call.callee ||
        call->call.callee->kind != AST_MEMBER_ACCESS ||
        !zan_routes_receiver(call->call.callee->member.object)) return 0;
    zan_istr_t name = call->call.callee->member.name;
    if (zan_routes_name_eq(name, "Register")) return 1;
    if (zan_routes_name_eq(name, "RegisterFramework")) return 2;
    return 0;
}

typedef struct {
    zan_ast_node_t *decl;
    bool found;
} zan_routes_type_probe_t;

static inline zan_walk_action_t zan_routes_probe_type(zan_ast_node_t *node,
                                                      void *data) {
    zan_routes_type_probe_t *p = (zan_routes_type_probe_t *)data;
    if (!node || node->kind != AST_TYPEOF_EXPR || !node->cast.type ||
        node->cast.type->kind != AST_TYPE_REF) return ZAN_WALK_CONTINUE;
    zan_istr_t ref = node->cast.type->type_ref.name;
    zan_istr_t name = p->decl->type_decl.name;
    bool same = ref.str && ref.len == name.len &&
                memcmp(ref.str, name.str, name.len) == 0;
    /* Also accept a qualified ref before namespace rewriting. */
    zan_istr_t ns = zan_ast_ns_name(p->decl);
    zan_istr_t simple = zan_routes_simple_name(p->decl);
    if (!same && ns.len && ref.len == ns.len + 1 + simple.len)
        same = memcmp(ref.str, ns.str, ns.len) == 0 &&
               ref.str[ns.len] == '.' &&
               memcmp(ref.str + ns.len + 1, simple.str, simple.len) == 0;
    if (!same) return ZAN_WALK_CONTINUE;
    p->found = true;
    return ZAN_WALK_ABORT;
}

static inline bool zan_routes_framework_controller(zan_ast_node_t *unit,
                                                   zan_ast_node_t *decl) {
    zan_routes_type_probe_t probe = { decl, false };
    for (int i = 0; i < unit->comp_unit.decls.count; i++) {
        zan_ast_node_t *owner = unit->comp_unit.decls.items[i];
        if (!owner || owner->kind != AST_CLASS_DECL ||
            !zan_routes_name_eq(zan_routes_simple_name(owner), "__AttrRoutes"))
            continue;
        for (int k = 0; k < owner->type_decl.members.count; k++) {
            zan_ast_node_t *m = owner->type_decl.members.items[k];
            if (!m || m->kind != AST_METHOD_DECL ||
                !zan_routes_name_eq(m->method_decl.name, "RegisterFramework"))
                continue;
            zan_ast_walk(m->method_decl.body, zan_routes_probe_type, NULL, &probe);
            if (probe.found) return true;
        }
    }
    return false;
}

#endif
