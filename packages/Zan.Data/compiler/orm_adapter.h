#ifndef ZAN_DATA_ORM_ADAPTER_H
#define ZAN_DATA_ORM_ADAPTER_H

#include "checker.h"

void zan_data_orm_prepare(zan_checker_t *c, zan_ast_node_t *unit);
/* NULL leaves normal member diagnostics to the checker. */
zan_type_t *zan_data_orm_try_accessor(zan_checker_t *c, zan_ast_node_t *expr,
                                      zan_type_t *receiver);
/* Before binding, pull-in uses these spellings to keep candidate sources. */
bool zan_data_orm_connection_hook(zan_istr_t name);
void zan_data_orm_seed_accessor_types(void (*flag)(zan_istr_t name));
void zan_data_orm_seed_member_dao(zan_istr_t name, void (*flag)(zan_istr_t name));
bool zan_data_orm_is_table_marker(zan_istr_t name);
bool zan_data_orm_keep_pullin_decl(zan_istr_t name);
bool zan_data_orm_keep_pullin_base(zan_istr_t name);

/* Legacy reflection fields are also available to code that uses no database. */
const char *zan_data_orm_reflect_table(const zan_symbol_t *symbol);
bool zan_data_orm_reflect_column(const zan_ast_node_t *attribute, zan_istr_t name,
                                 const char **custom_name, unsigned long long *flags);

#endif
