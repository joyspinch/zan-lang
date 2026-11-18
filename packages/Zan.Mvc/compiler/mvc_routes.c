#include "mvc_compiler.h"
#include "arena.h"
#include <string.h>

/* Shared driver/checker protocol. Framework exposure is the explicit typeof
 * allowlist in __AttrRoutes.RegisterFramework, never a namespace heuristic. */
static bool mvc_name_eq(zan_istr_t name, const char *text) {
    return name.str && name.len == strlen(text) &&
           memcmp(name.str, text, name.len) == 0;
}

bool zan_mvc_has_controller_suffix(zan_istr_t name) {
    const char suffix[] = "Controller";
    size_t len = sizeof(suffix) - 1;
    return name.str && name.len >= len &&
           memcmp(name.str + name.len - len, suffix, len) == 0;
}

bool zan_mvc_is_route_pullin_attribute(zan_istr_t name) {
    return mvc_name_eq(name, "Route") || mvc_name_eq(name, "ApiController") ||
           mvc_name_eq(name, "Custom") || mvc_name_eq(name, "HttpGet") ||
           mvc_name_eq(name, "HttpPost") || mvc_name_eq(name, "HttpPut") ||
           mvc_name_eq(name, "HttpDelete") || mvc_name_eq(name, "HttpPatch");
}

static bool mvc_is_base_controller(zan_istr_t name) {
    return mvc_name_eq(name, "Controller") || mvc_name_eq(name, "AppController") ||
           mvc_name_eq(name, "AdminController") || mvc_name_eq(name, "ApiController");
}

bool zan_mvc_keep_namespace_decl(zan_ast_node_t *decl) {
    if (decl->kind != AST_CLASS_DECL || !decl->type_decl.name.str) return false;
    zan_istr_t name = decl->type_decl.name;
    if (zan_mvc_has_controller_suffix(name)) return !mvc_is_base_controller(name);
    return zan_ast_has_attr(decl, "Route");
}

zan_istr_t zan_mvc_simple_name(zan_ast_node_t *decl) {
    zan_istr_t orig = zan_ast_orig_name(decl);
    return orig.len ? orig : decl->type_decl.name;
}

static bool mvc_route_receiver(zan_ast_node_t *obj) {
    if (!obj) return false;
    if (obj->kind == AST_IDENTIFIER)
        return mvc_name_eq(obj->ident.name, "__AttrRoutes");
    return obj->kind == AST_MEMBER_ACCESS &&
           mvc_name_eq(obj->member.name, "__AttrRoutes");
}

zan_mvc_registration_t zan_mvc_registration(zan_ast_node_t *call) {
    if (!call || call->kind != AST_CALL || !call->call.callee ||
        call->call.callee->kind != AST_MEMBER_ACCESS ||
        !mvc_route_receiver(call->call.callee->member.object)) return ZAN_MVC_REGISTER_NONE;
    zan_istr_t name = call->call.callee->member.name;
    if (mvc_name_eq(name, "Register")) return ZAN_MVC_REGISTER_APP;
    if (mvc_name_eq(name, "RegisterFramework")) return ZAN_MVC_REGISTER_FRAMEWORK;
    return ZAN_MVC_REGISTER_NONE;
}

typedef struct {
    zan_ast_node_t *decl;
    bool found;
} mvc_type_probe_t;

static zan_walk_action_t mvc_probe_type(zan_ast_node_t *node, void *data) {
    mvc_type_probe_t *p = (mvc_type_probe_t *)data;
    if (!node || node->kind != AST_TYPEOF_EXPR || !node->cast.type ||
        node->cast.type->kind != AST_TYPE_REF) return ZAN_WALK_CONTINUE;
    zan_istr_t ref = node->cast.type->type_ref.name;
    zan_istr_t name = p->decl->type_decl.name;
    bool same = ref.str && ref.len == name.len &&
                memcmp(ref.str, name.str, name.len) == 0;
    /* Also accept a qualified ref before namespace rewriting. */
    zan_istr_t ns = zan_ast_ns_name(p->decl);
    zan_istr_t simple = zan_mvc_simple_name(p->decl);
    if (!same && ns.len && ref.len == ns.len + 1 + simple.len)
        same = memcmp(ref.str, ns.str, ns.len) == 0 &&
               ref.str[ns.len] == '.' &&
               memcmp(ref.str + ns.len + 1, simple.str, simple.len) == 0;
    if (!same) return ZAN_WALK_CONTINUE;
    p->found = true;
    return ZAN_WALK_ABORT;
}

bool zan_mvc_framework_controller(zan_ast_node_t *unit, zan_ast_node_t *decl) {
    mvc_type_probe_t probe = { decl, false };
    for (int i = 0; i < unit->comp_unit.decls.count; i++) {
        zan_ast_node_t *owner = unit->comp_unit.decls.items[i];
        if (!owner || owner->kind != AST_CLASS_DECL ||
            !mvc_name_eq(zan_mvc_simple_name(owner), "__AttrRoutes"))
            continue;
        for (int k = 0; k < owner->type_decl.members.count; k++) {
            zan_ast_node_t *m = owner->type_decl.members.items[k];
            if (!m || m->kind != AST_METHOD_DECL ||
                !mvc_name_eq(m->method_decl.name, "RegisterFramework"))
                continue;
            zan_ast_walk(m->method_decl.body, mvc_probe_type, NULL, &probe);
            if (probe.found) return true;
        }
    }
    return false;
}

static zan_istr_t mvc_istr_lit(zan_arena_t *arena, const char *text) {
    size_t len = strlen(text);
    return (zan_istr_t){zan_arena_strdup(arena, text, len), (uint32_t)len};
}

static zan_ast_node_t *mvc_type_ref(zan_arena_t *arena, zan_istr_t name,
                                    zan_loc_t loc) {
    zan_ast_node_t *type = zan_ast_new(arena, AST_TYPE_REF, loc);
    type->type_ref.name = name;
    type->type_ref.is_nullable = false;
    type->type_ref.is_array = false;
    zan_ast_list_init(&type->type_ref.type_args);
    return type;
}

/* ---------- __AttrRoutes.Register 的编译期控制器发现 ----------
 *
 * 预扫描 compilation unit 里的 Controller 类，在 __AttrRoutes.Register(app)
 * 调用点原地合成 `new TypeInfo[] { typeof(C1), typeof(C2), ... }` 实参，
 * 由框架动态约定路由引擎遍历计算并载入共享内存表。 */

void zan_mvc_collect_controllers(zan_mvc_route_state_t *routes,
                                 zan_ast_node_t *unit, zan_arena_t *arena) {
    if (routes->collected || !unit || unit->kind != AST_COMPILATION_UNIT) {
        return;
    }
    routes->collected = true;
    for (int i = 0; i < unit->comp_unit.decls.count; i++) {
        zan_ast_node_t *decl = unit->comp_unit.decls.items[i];
        if (decl->kind != AST_CLASS_DECL || !decl->type_decl.name.str) continue;
        if (decl->type_decl.modifiers & MOD_ABSTRACT) continue;
        if (decl->type_decl.type_params.count > 0) continue;

        /* B-ID110：库侧（stdlib / packages）自带控制器不参与隐式发现——
         * 否则 Zan.Mvc 拉入编译集的 200+ 框架控制器被用户项目的
         * __AttrRoutes.Register 全量吞掉；框架页归 Boot/显式注册管。
         *
         * 判定按**来源**而不是命名空间：模板/示例项目（用户自己的代码）
         * 也统一使用 Zan.Web.* 命名空间，按命名空间跳过会把项目自己的
         * 控制器一并吞掉（server-legend 全体 API 404 就是这么来的）。 */
        if (decl->from_stdlib || decl->from_package ||
            zan_mvc_framework_controller(unit, decl)) continue;

        zan_istr_t name = decl->type_decl.name;
        if (mvc_is_base_controller(name) || mvc_name_eq(name, "CrudScreenController"))
            continue;

        /* 判定是否为控制器：以 Controller 结尾，或带有 [Route]/[ApiController]，或继承自 Controller 家族 */
        bool is_ctrl = false;
        if (zan_mvc_has_controller_suffix(name)) {
            is_ctrl = true;
        } else if (zan_ast_has_attr(decl, "Route") || zan_ast_has_attr(decl, "ApiController")) {
            is_ctrl = true;
        } else {
            for (int b = 0; b < decl->type_decl.bases.count; b++) {
                zan_ast_node_t *base = decl->type_decl.bases.items[b];
                if (base && base->kind == AST_TYPE_REF &&
                    zan_mvc_has_controller_suffix(base->type_ref.name)) {
                    is_ctrl = true;
                    break;
                }
            }
        }
        if (!is_ctrl) continue;

        int dup = 0;
        for (int k = 0; k < routes->count; k++) {
            if (routes->names[k].len == decl->type_decl.name.len &&
                memcmp(routes->names[k].str, decl->type_decl.name.str,
                       (size_t)decl->type_decl.name.len) == 0) { dup = 1; break; }
        }
        if (dup) continue;
        if (routes->count >= routes->capacity) {
            int ncap = routes->capacity ? routes->capacity * 2 : 32;
            zan_istr_t *nm = (zan_istr_t *)zan_arena_alloc(
                arena, (size_t)ncap * sizeof(zan_istr_t));
            for (int k = 0; k < routes->count; k++) nm[k] = routes->names[k];
            routes->names = nm;
            routes->capacity = ncap;
        }
        routes->names[routes->count++] = decl->type_decl.name;
    }
}

/* __ZanActionDispatch.Dispatch：main.c 为含 __AttrRoutes.Register 的编译单元
 * 合成动作派发类；这里把它的方法组作为 Register 的第三实参传进去。 */
static zan_ast_node_t *mvc_dispatcher_ref(zan_arena_t *arena, zan_loc_t loc,
                                          bool framework) {
    zan_ast_node_t *id = zan_ast_new(arena, AST_IDENTIFIER, loc);
    id->ident.name = mvc_istr_lit(arena, "__ZanActionDispatch");
    id->ident.inst_type_ref = NULL;
    zan_ast_node_t *m = zan_ast_new(arena, AST_MEMBER_ACCESS, loc);
    m->member.object = id;
    m->member.name = mvc_istr_lit(arena,
                                    framework ? "DispatchFramework" : "Dispatch");
    m->member.null_cond = 0;
    return m;
}

/* new TypeInfo[] { typeof(C1), typeof(C2), ... } */
static zan_ast_node_t *mvc_controllers_array(const zan_mvc_route_state_t *routes,
                                             zan_arena_t *arena, zan_loc_t loc) {
    zan_ast_node_t *tref = zan_ast_new(arena, AST_TYPE_REF, loc);
    tref->type_ref.name = mvc_istr_lit(arena, "TypeInfo");
    tref->type_ref.is_nullable = false;
    tref->type_ref.is_array = true;
    zan_ast_list_init(&tref->type_ref.type_args);
    zan_ast_node_t *n = zan_ast_new(arena, AST_NEW_EXPR, loc);
    n->new_expr.type = tref;
    n->new_expr.call_init = NULL;
    zan_ast_list_init(&n->new_expr.args);
    zan_ast_list_init(&n->new_expr.arg_inits);
    n->new_expr.is_array = true;
    n->new_expr.array_init = true;
    n->new_expr.array_rank = 0;
    n->new_expr.list_copy = false;
    for (int i = 0; i < routes->count; i++) {
        zan_ast_node_t *tof = zan_ast_new(arena, AST_TYPEOF_EXPR, loc);
        tof->cast.type = mvc_type_ref(arena, routes->names[i], loc);
        zan_ast_list_push(&n->new_expr.args, tof, arena);
    }
    return n;
}

void zan_mvc_rewrite_route_call(const zan_mvc_route_state_t *routes,
                                zan_ast_node_t *call, zan_arena_t *arena) {
    zan_mvc_registration_t registration = zan_mvc_registration(call);
    if (registration == ZAN_MVC_REGISTER_APP &&
        (call->call.args.count == 1 || call->call.args.count == 2)) {
        if (call->call.args.count == 1) {
            zan_ast_list_push(&call->call.args,
                              mvc_controllers_array(routes, arena, call->loc), arena);
        }
        zan_ast_list_push(&call->call.args,
                          mvc_dispatcher_ref(arena, call->loc, false), arena);
    } else if (registration == ZAN_MVC_REGISTER_FRAMEWORK &&
               call->call.args.count == 1) {
        /* Framework opt-in has its own dispatcher. */
        zan_ast_list_push(&call->call.args,
                          mvc_dispatcher_ref(arena, call->loc, true), arena);
    }
}
