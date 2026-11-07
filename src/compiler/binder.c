/* binder */

#include "binder.h"
#include "arena.h"
#include "diag.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static zan_type_t *make_type(zan_arena_t *arena, zan_type_kind_t kind, const char *name, int len) {
    zan_type_t *t = (zan_type_t *)zan_arena_alloc(arena, sizeof(zan_type_t));
    t->kind = kind;
    t->name.str = name;
    t->name.len = len;
    t->interfaces = NULL;
    t->interface_count = 0;
    return t;
}

static zan_symbol_t *make_symbol(zan_arena_t *arena, zan_sym_kind_t kind,
                                 zan_istr_t name, zan_type_t *type,
                                 zan_ast_node_t *decl, uint32_t modifiers) {
    zan_symbol_t *s = (zan_symbol_t *)zan_arena_alloc(arena, sizeof(zan_symbol_t));
    s->kind = kind;
    s->name = name;
    s->type = type;
    s->decl = decl;
    s->modifiers = modifiers;
    s->parent = NULL;
    s->members = NULL;
    s->member_count = 0;
    s->member_cap = 0;
    s->name_hash = 0;
    s->hash_next = NULL;
    return s;
}

/* 把类型参数替换成实例的类型实参（Base<string> 里的 T → string）。 */
static zan_type_t *binder_subst_type_param(zan_type_t *t, zan_type_t *recv);


/* Composite Dispatcher Submodules */
#include "binder/binder_sym.c"
#include "binder/binder_decl.c"
