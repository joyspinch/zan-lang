#include "orm_adapter.h"
#include "arena.h"
#include "diag.h"
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include "win_utf8.h"
#endif

struct zan_data_orm_state {
    zan_ast_node_t *unit;
    zan_ast_list_t entities;
    zan_symbol_t *connection;
    zan_symbol_t *executor;
    zan_symbol_t *repository;
    zan_symbol_t *sql;
    zan_symbol_t *extensions;
    zan_symbol_t *meta;
};

static zan_istr_t istr(const char *text) {
    return (zan_istr_t){text, (uint32_t)strlen(text)};
}

static bool name_eq(zan_istr_t lhs, zan_istr_t rhs) {
    return lhs.len == rhs.len && (!lhs.len || memcmp(lhs.str, rhs.str, lhs.len) == 0);
}

static zan_istr_t simple_name(const zan_ast_node_t *decl) {
    zan_istr_t name = zan_ast_orig_name(decl);
    return name.len ? name : decl->type_decl.name;
}

static char path_char(char c) {
#ifdef _WIN32
    if (c == '\\') return '/';
    if (c >= 'A' && c <= 'Z') return (char)(c + 'a' - 'A');
#endif
    return c;
}

static bool path_ends_in(const char *path, size_t length, const char *suffix) {
    size_t suffix_len = strlen(suffix);
    if (length < suffix_len) return false;
    size_t start = length - suffix_len;
    if (start && path_char(path[start - 1]) != '/') return false;
    for (size_t i = 0; i < suffix_len; i++) {
        if (path_char(path[start + i]) != path_char(suffix[i])) return false;
    }
    return true;
}

static bool is_package_source(const zan_diag_t *diag, const zan_ast_node_t *decl,
                              const char *source) {
    if (decl->loc.file_id >= (uint32_t)diag->file_count) return false;
    const char *path = diag->file_names[decl->loc.file_id];
    if (!path) return false;
    size_t len = strlen(path);
    size_t source_len = strlen(source);
    if (len > source_len && path_ends_in(path, len, source) &&
        path_ends_in(path, len - source_len - 1, "Zan.Data/src")) return true;

    /* Explicit compiler inputs may be relative to the package directory. */
    char full_path[4096];
#ifdef _WIN32
    if (!zan_utf8_full_path(path, full_path, sizeof(full_path))) return false;
#else
    if (!realpath(path, full_path)) return false;
#endif
    len = strlen(full_path);
    return len > source_len && path_ends_in(full_path, len, source) &&
           path_ends_in(full_path, len - source_len - 1, "Zan.Data/src");
}

void zan_data_orm_prepare(zan_checker_t *c, zan_ast_node_t *unit) {
    struct zan_data_orm_state state = { .unit = unit };
    const struct {
        const char *name;
        const char *namespace_name;
        const char *source;
        zan_ast_kind_t kind;
        zan_symbol_t **symbol;
    } types[] = {
        {"IDbConnection", "System.Data", "System/Data/IDbConnection.zan",
         AST_INTERFACE_DECL, &state.connection},
        {"IDbExecutor", "System.Data", "System/Data/IDbExecutor.zan",
         AST_INTERFACE_DECL, &state.executor},
        {"BaseRepository", "System.Data.Orm", "System/Data/Orm/BaseRepository.zan",
         AST_CLASS_DECL, &state.repository},
        {"ZanSql", "System.Data.Orm", "System/Data/Orm/ZanSql.zan",
         AST_CLASS_DECL, &state.sql},
        {"DbExtensions", "System.Data.Orm", "System/Data/Orm/DbExtensions.zan",
         AST_CLASS_DECL, &state.extensions},
        {"Meta", "System.Data.Orm", "System/Data/Orm/Meta.zan",
         AST_CLASS_DECL, &state.meta},
    };
    if (c->data_orm) return;
    const zan_ast_list_t *decls = &unit->comp_unit.decls;
    for (int i = 0; i < decls->count; i++) {
        zan_ast_node_t *decl = decls->items[i];
        if (decl->kind != AST_CLASS_DECL && decl->kind != AST_INTERFACE_DECL) continue;
        zan_istr_t ns = zan_ast_ns_name(decl);
        zan_istr_t name = simple_name(decl);
        for (size_t j = 0; j < sizeof(types) / sizeof(types[0]); j++) {
            if (decl->kind != types[j].kind || !name_eq(name, istr(types[j].name)) ||
                !name_eq(ns, istr(types[j].namespace_name)) ||
                !is_package_source(c->diag, decl, types[j].source)) continue;
            zan_symbol_t *sym = zan_binder_lookup(c->binder, decl->type_decl.name);
            if (sym && sym->decl == decl) *types[j].symbol = sym;
            break;
        }
    }
    if (!state.connection && !state.executor) return;
    for (int i = 0; i < decls->count; i++) {
        zan_ast_node_t *decl = decls->items[i];
        if (decl->kind == AST_CLASS_DECL && decl->type_decl.name.str &&
            zan_ast_has_attr(decl, "Table"))
            zan_ast_list_push(&state.entities, decl, c->arena);
    }
    c->data_orm = zan_arena_alloc(c->arena, sizeof(state));
    *c->data_orm = state;
}

static bool connection_matches(const zan_type_t *type, zan_symbol_t *iface) {
    return type && type->sym && iface &&
           (type->sym == iface ||
            ((type->kind == TYPE_CLASS || type->kind == TYPE_STRUCT) &&
             zan_binder_class_implements_iface(type->sym, iface)));
}

static zan_ast_node_t *type_ref(zan_arena_t *arena, zan_istr_t name, zan_loc_t loc) {
    zan_ast_node_t *ref = zan_ast_new(arena, AST_TYPE_REF, loc);
    ref->type_ref.name = name;
    return ref;
}

static zan_ast_node_t *type_receiver(zan_arena_t *arena, zan_symbol_t *symbol,
                                     zan_loc_t loc) {
    zan_ast_node_t *id = zan_ast_new(arena, AST_IDENTIFIER, loc);
    id->ident.name = symbol->name;
    id->ident.is_type_receiver = true;
    return id;
}

static zan_ast_node_t *member_access(zan_arena_t *arena, zan_ast_node_t *receiver,
                                    zan_istr_t name, zan_loc_t loc) {
    zan_ast_node_t *member = zan_ast_new(arena, AST_MEMBER_ACCESS, loc);
    member->member.object = receiver;
    member->member.name = name;
    return member;
}

static zan_ast_node_t *member_call(zan_arena_t *arena, zan_ast_node_t *receiver,
                                  zan_istr_t name, zan_loc_t loc) {
    zan_ast_node_t *call = zan_ast_new(arena, AST_CALL, loc);
    call->call.callee = member_access(arena, receiver, name, loc);
    return call;
}

static bool accessor_name(zan_istr_t name) {
    return name.len > 0 && name.len <= 100 && name.str[0] >= 'A' && name.str[0] <= 'Z';
}

bool zan_data_orm_connection_hook(zan_istr_t name) {
    return name_eq(name, istr("__Conn"));
}

void zan_data_orm_seed_accessor_types(void (*flag)(zan_istr_t name)) {
    flag(istr("BaseRepository"));
    flag(istr("ZanSql"));
}

static zan_istr_t dao_name_for(zan_istr_t name, char *buffer) {
    memcpy(buffer, name.str, name.len);
    memcpy(buffer + name.len, "Dao", sizeof("Dao"));
    return (zan_istr_t){buffer, name.len + sizeof("Dao") - 1};
}

void zan_data_orm_seed_member_dao(zan_istr_t name, void (*flag)(zan_istr_t name)) {
    if (!accessor_name(name) || name.len == 100) return;
    char buffer[104];
    flag(dao_name_for(name, buffer));
}

bool zan_data_orm_is_table_marker(zan_istr_t name) {
    return name_eq(name, istr("Table"));
}

bool zan_data_orm_keep_pullin_decl(zan_istr_t name) {
    return name.len >= 3 && memcmp(name.str + name.len - 3, "Dao", 3) == 0;
}

bool zan_data_orm_keep_pullin_base(zan_istr_t name) {
    return name_eq(name, istr("BaseRepository")) || name_eq(name, istr("BaseDao"));
}

static zan_ast_node_t *find_entity(const zan_checker_t *c, zan_istr_t name) {
    zan_istr_t context_ns = c->current_type_sym
        ? zan_ast_ns_name(c->current_type_sym->decl) : (zan_istr_t){0};
    const zan_ast_list_t *entities = &c->data_orm->entities;
    zan_ast_node_t *found = NULL;
    bool ambiguous = false;
    for (int i = 0; i < entities->count; i++) {
        zan_ast_node_t *decl = entities->items[i];
        if (!name_eq(name, simple_name(decl))) continue;
        if (name_eq(context_ns, zan_ast_ns_name(decl))) return decl;
        if (found) ambiguous = true;
        found = decl;
    }
    return ambiguous ? NULL : found;
}

static zan_symbol_t *find_dao(zan_checker_t *c, const zan_ast_node_t *entity,
                              zan_istr_t name) {
    zan_istr_t ns = zan_ast_ns_name(entity);
    const zan_ast_list_t *decls = &c->data_orm->unit->comp_unit.decls;
    zan_symbol_t *found = NULL;
    bool ambiguous = false;
    for (int i = 0; i < decls->count; i++) {
        zan_ast_node_t *decl = decls->items[i];
        if (decl->kind != AST_CLASS_DECL || !name_eq(simple_name(decl), name)) continue;
        zan_symbol_t *sym = zan_binder_lookup(c->binder, decl->type_decl.name);
        if (!sym || sym->kind != SYM_CLASS || sym->decl != decl) continue;
        if (name_eq(ns, zan_ast_ns_name(decl))) return sym;
        if (found) ambiguous = true;
        found = sym;
    }
    return ambiguous ? NULL : found;
}

static zan_ast_node_t *accessor_connection(zan_checker_t *c, zan_ast_node_t *expr,
                                          zan_type_t *receiver, zan_type_t **type) {
    zan_symbol_t *iface = c->data_orm->connection;
    if (connection_matches(receiver, iface)) {
        *type = receiver;
        return expr->member.object;
    }
    if (receiver->kind != TYPE_CLASS) return NULL;
    for (zan_symbol_t *cls = receiver->sym; cls;
         cls = cls->type && cls->type->base_type ? cls->type->base_type->sym : NULL) {
        for (int i = 0; i < cls->member_count; i++) {
            zan_symbol_t *method = cls->members[i];
            if (method->kind != SYM_METHOD || !zan_data_orm_connection_hook(method->name) ||
                !method->decl || method->decl->kind != AST_METHOD_DECL ||
                method->decl->method_decl.params.count != 0 ||
                !connection_matches(method->type, iface)) continue;
            *type = method->type;
            return member_call(c->arena, expr->member.object, method->name, expr->loc);
        }
    }
    return NULL;
}

static zan_type_t *accessor_new(zan_checker_t *c, zan_ast_node_t *expr,
                                zan_ast_node_t *ref, zan_ast_node_t *arg) {
    /* The member and new expression share storage. */
    memset(&expr->new_expr, 0, sizeof(expr->new_expr));
    expr->kind = AST_NEW_EXPR;
    expr->new_expr.type = ref;
    zan_ast_list_push(&expr->new_expr.args, arg, c->arena);
    return zan_checker_check_expr(c, expr);
}

zan_type_t *zan_data_orm_try_accessor(zan_checker_t *c, zan_ast_node_t *expr,
                                      zan_type_t *receiver) {
    struct zan_data_orm_state *state = c->data_orm;
    zan_istr_t name = expr->member.name;
    if (!state || !state->connection || !accessor_name(name)) return NULL;
    zan_ast_node_t *entity = find_entity(c, name);
    if (!entity) return NULL;
    zan_type_t *connection_type = NULL;
    zan_ast_node_t *conn = accessor_connection(c, expr, receiver, &connection_type);
    if (!conn) return NULL;

    char dao_chars[104];
    zan_istr_t dao_name = dao_name_for(name, dao_chars);
    zan_symbol_t *dao = find_dao(c, entity, dao_name);
    if (dao) {
        for (int i = 0; i < dao->member_count; i++) {
            zan_symbol_t *ctor = dao->members[i];
            if (ctor->kind != SYM_CONSTRUCTOR || ctor->member_count != 1) continue;
            zan_type_t *param = ctor->members[0]->type;
            if (!param || !param->sym ||
                (connection_type->sym != param->sym &&
                 (param->kind != TYPE_INTERFACE ||
                  !connection_matches(connection_type, param->sym)))) continue;
            return accessor_new(c, expr, type_ref(c->arena, dao->name, expr->loc), conn);
        }
        zan_diag_emit_code(c->diag, DIAG_ERROR, expr->loc, 2095,
            "'%.*s' accessor needs a single-parameter constructor on "
            "'%.*s' taking the connection",
            (int)name.len, name.str, (int)dao_name.len, dao_name.str);
        return c->binder->type_error;
    }

    if (!state->repository || !state->sql) return NULL;
    zan_loc_t loc = expr->loc;
    zan_ast_node_t *ref = type_ref(c->arena, state->repository->name, loc);
    zan_ast_list_push(&ref->type_ref.type_args,
                      type_ref(c->arena, entity->type_decl.name, loc), c->arena);
    zan_ast_node_t *factory = member_call(c->arena, type_receiver(c->arena, state->sql, loc),
                                          istr("FromConnection"), loc);
    zan_ast_list_push(&factory->call.args, conn, c->arena);
    return accessor_new(c, expr, ref, factory);
}

bool zan_data_orm_schema_candidate(const zan_checker_t *c,
                                   const zan_ast_node_t *call) {
    if (!c->data_orm || !c->data_orm->extensions || !c->data_orm->meta ||
        !call->call.callee || call->call.callee->kind != AST_MEMBER_ACCESS ||
        call->call.args.count || call->call.type_args.count) return false;
    zan_istr_t name = call->call.callee->member.name;
    return name_eq(name, istr("SyncStructureAll")) ||
           name_eq(name, istr("SyncStructureAllAsync"));
}

static zan_ast_node_t *schema_table_array(zan_checker_t *c, zan_loc_t loc) {
    struct zan_data_orm_state *state = c->data_orm;
    zan_ast_node_t *ref = type_ref(c->arena, state->meta->name, loc);
    ref->type_ref.is_array = true;
    zan_ast_node_t *array = zan_ast_new(c->arena, AST_NEW_EXPR, loc);
    array->new_expr.type = ref;
    array->new_expr.is_array = true;
    array->new_expr.array_init = true;
    for (int i = 0; i < state->entities.count; i++) {
        zan_ast_node_t *entity = state->entities.items[i];
        zan_ast_node_t *call = member_call(c->arena,
            type_receiver(c->arena, state->meta, loc), istr("For"), loc);
        zan_ast_list_push(&call->call.type_args,
                           type_ref(c->arena, entity->type_decl.name, loc), c->arena);
        zan_ast_list_push(&array->new_expr.args, call, c->arena);
    }
    return array;
}

bool zan_data_orm_lower_schema(zan_checker_t *c, zan_ast_node_t *call,
                               zan_type_t *receiver) {
    struct zan_data_orm_state *state = c->data_orm;
    if (!connection_matches(receiver, state->connection) &&
        !connection_matches(receiver, state->executor)) return false;

    zan_ast_node_t *old = call->call.callee;
    bool package_overload = false;
    for (int i = 0; i < c->binder->ext_method_count; i++) {
        const zan_ext_method_t *entry = &c->binder->ext_methods[i];
        zan_symbol_t *method = entry->method;
        if (!name_eq(method->name, old->member.name)) continue;
        zan_type_t *target = entry->recv_type;
        bool matches = target->kind == TYPE_TYPE_PARAM || target->sym == receiver->sym ||
            (target->kind == TYPE_INTERFACE &&
             zan_binder_class_implements_iface(receiver->sym, target->sym));
        if (!matches) continue;
        /* An ordinary zero-argument extension takes precedence. */
        if (method->member_count == 1) return false;
        if (method->parent != state->extensions || method->member_count != 2 ||
            !(method->modifiers & MOD_STATIC)) continue;
        zan_type_t *metas = method->members[1]->type;
        if (metas && metas->kind == TYPE_ARRAY && metas->element_type &&
            metas->element_type->sym == state->meta) package_overload = true;
    }
    if (!package_overload) return false;

    /* Use a static call to keep async calls on the normal overload/await path. */
    call->call.callee = member_access(c->arena,
        type_receiver(c->arena, state->extensions, call->loc), old->member.name, call->loc);
    zan_ast_list_push(&call->call.args, old->member.object, c->arena);
    zan_ast_list_push(&call->call.args, schema_table_array(c, call->loc), c->arena);
    return true;
}

const char *zan_data_orm_reflect_table(const zan_symbol_t *symbol) {
    if (!symbol || !symbol->decl || !symbol->decl->meta) return NULL;
    const zan_ast_list_t *attrs = &symbol->decl->meta->attributes;
    for (int i = 0; i < attrs->count; i++) {
        const zan_ast_node_t *attr = attrs->items[i];
        if (!attr || attr->kind != AST_ATTRIBUTE || !attr->attribute.name ||
            attr->attribute.name->kind != AST_IDENTIFIER) continue;
        zan_istr_t name = attr->attribute.name->ident.name;
        if (!zan_data_orm_is_table_marker(name) && !name_eq(name, istr("TableAttribute")))
            continue;
        for (int j = 0; j < attr->attribute.args.count; j++) {
            const zan_ast_node_t *arg = attr->attribute.args.items[j];
            if (!arg) continue;
            if (arg->kind == AST_STRING_LITERAL) return arg->str_val.str;
            if (arg->kind != AST_ASSIGNMENT || !arg->binary.left ||
                arg->binary.left->kind != AST_IDENTIFIER || !arg->binary.right ||
                arg->binary.right->kind != AST_STRING_LITERAL) continue;
            zan_istr_t key = arg->binary.left->ident.name;
            if (name_eq(key, istr("Name")) || name_eq(key, istr("TableName")))
                return arg->binary.right->str_val.str;
        }
        return symbol->name.str;
    }
    return NULL;
}

bool zan_data_orm_reflect_column(const zan_ast_node_t *attribute, zan_istr_t name,
                                 const char **custom_name, unsigned long long *flags) {
    if (!name_eq(name, istr("Column")) && !name_eq(name, istr("ColumnAttribute")))
        return false;
    for (int i = 0; i < attribute->attribute.args.count; i++) {
        const zan_ast_node_t *arg = attribute->attribute.args.items[i];
        if (!arg) continue;
        if (arg->kind == AST_STRING_LITERAL) {
            *custom_name = arg->str_val.str;
            continue;
        }
        if (arg->kind != AST_ASSIGNMENT || !arg->binary.left ||
            arg->binary.left->kind != AST_IDENTIFIER || !arg->binary.right) continue;
        zan_istr_t key = arg->binary.left->ident.name;
        const zan_ast_node_t *value = arg->binary.right;
        if (name_eq(key, istr("Name")) && value->kind == AST_STRING_LITERAL)
            *custom_name = value->str_val.str;
        if (value->kind == AST_BOOL_LITERAL && value->bool_val) {
            /* Match the public TypeInfo field-flag layout. */
            if (name_eq(key, istr("Ignore"))) *flags |= 1ULL;
            if (name_eq(key, istr("Required"))) *flags |= 2ULL;
            if (name_eq(key, istr("IsPrimary"))) *flags |= 4ULL;
            if (name_eq(key, istr("IsIdentity"))) *flags |= 8ULL;
        }
    }
    return true;
}
