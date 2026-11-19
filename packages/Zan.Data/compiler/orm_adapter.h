#ifndef ZAN_DATA_ORM_ADAPTER_H
#define ZAN_DATA_ORM_ADAPTER_H

#include "checker.h"

bool zan_data_orm_is_table_marker(zan_istr_t name);

/* Legacy reflection fields are also available to code that uses no database. */
const char *zan_data_orm_reflect_table(const zan_symbol_t *symbol);
bool zan_data_orm_reflect_column(const zan_ast_node_t *attribute, zan_istr_t name,
                                 const char **custom_name, unsigned long long *flags);

#endif
