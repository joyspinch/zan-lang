#include "orm_adapter.h"
#include <string.h>

static zan_istr_t istr(const char *text) {
    return (zan_istr_t){text, (uint32_t)strlen(text)};
}

static bool name_eq(zan_istr_t lhs, zan_istr_t rhs) {
    return lhs.len == rhs.len && (!lhs.len || memcmp(lhs.str, rhs.str, lhs.len) == 0);
}

bool zan_data_orm_is_table_marker(zan_istr_t name) {
    return name_eq(name, istr("Table"));
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
