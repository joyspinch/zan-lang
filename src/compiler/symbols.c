/* symbols.c -- see symbols.h. */

#include "symbols.h"
#include "builtin_api.h"

#include <stdio.h>
#include <string.h>

/* Renders a type reference ("List<string>", "byte[]", "int?") into buf. */
static int type_name(const zan_ast_node_t *t, char *buf, int cap) {
    int n = 0;
    if (!t || cap < 2) { if (cap > 0) buf[0] = 0; return 0; }
    if (t->kind == AST_QUALIFIED_NAME) {
        for (int i = 0; i < t->qualified_name.parts.count; i++) {
            zan_ast_node_t *p = t->qualified_name.parts.items[i];
            if (i > 0 && n < cap - 1) buf[n++] = '.';
            int len = (int)p->ident.name.len;
            if (len > cap - n - 1) len = cap - n - 1;
            if (len > 0) { memcpy(buf + n, p->ident.name.str, (size_t)len); n += len; }
        }
        buf[n] = 0;
        return n;
    }
    if (t->kind != AST_TYPE_REF) { buf[0] = 0; return 0; }
    int len = (int)t->type_ref.name.len;
    if (len > cap - n - 1) len = cap - n - 1;
    if (len > 0) { memcpy(buf + n, t->type_ref.name.str, (size_t)len); n += len; }
    if (t->type_ref.type_args.count > 0) {
        if (n < cap - 1) buf[n++] = '<';
        for (int i = 0; i < t->type_ref.type_args.count; i++) {
            if (i > 0) {
                if (n < cap - 1) buf[n++] = ',';
                if (n < cap - 1) buf[n++] = ' ';
            }
            n += type_name(t->type_ref.type_args.items[i], buf + n, cap - n);
        }
        if (n < cap - 1) buf[n++] = '>';
    }
    if (t->type_ref.is_array) {
        if (n < cap - 1) buf[n++] = '[';
        if (n < cap - 1) buf[n++] = ']';
    }
    if (t->type_ref.is_nullable && n < cap - 1) buf[n++] = '?';
    buf[n] = 0;
    return n;
}

/* "string Substring(int start, int length)" for a method declaration. */
static void method_sig(const zan_ast_node_t *m, char *buf, int cap) {
    char ret[256];
    type_name(m->method_decl.return_type, ret, (int)sizeof(ret));
    int n = snprintf(buf, (size_t)cap, "%s%s%.*s(",
                     m->method_decl.return_type ? ret : "void",
                     m->method_decl.return_type ? " " : " ",
                     (int)m->method_decl.name.len, m->method_decl.name.str);
    for (int i = 0; i < m->method_decl.params.count && n < cap - 2; i++) {
        zan_ast_node_t *p = m->method_decl.params.items[i];
        if (!p || p->kind != AST_PARAM) continue;
        char pt[256];
        type_name(p->param.type, pt, (int)sizeof(pt));
        n += snprintf(buf + n, (size_t)(cap - n), "%s%s%s%s %.*s",
                      i > 0 ? ", " : "",
                      p->param.is_this ? "this " : "",
                      p->param.by_ref == 1 ? "ref " : (p->param.by_ref == 2 ? "out " : ""),
                      pt,
                      (int)p->param.name.len, p->param.name.str);
    }
    if (n < cap - 1) { buf[n++] = ')'; buf[n] = 0; }
}

static const char *type_kind_name(zan_ast_kind_t k) {
    switch (k) {
    case AST_CLASS_DECL:     return "class";
    case AST_STRUCT_DECL:    return "struct";
    case AST_INTERFACE_DECL: return "interface";
    case AST_ENUM_DECL:      return "enum";
    default:                 return NULL;
    }
}

static void emit_type(FILE *f, zan_ast_node_t *d) {
    const char *kind = type_kind_name(d->kind);
    if (!kind) return;

    char bases[512];
    int bn = 0;
    bases[0] = 0;
    if (d->kind != AST_ENUM_DECL) {
        for (int i = 0; i < d->type_decl.bases.count; i++) {
            char b[256];
            type_name(d->type_decl.bases.items[i], b, (int)sizeof(b));
            bn += snprintf(bases + bn, sizeof(bases) - (size_t)bn, "%s%s",
                           i > 0 ? "," : "", b);
            if (bn >= (int)sizeof(bases) - 1) break;
        }
    }

    zan_istr_t name = (d->kind == AST_ENUM_DECL) ? d->type_decl.name
                                                 : d->type_decl.name;
    fprintf(f, "T\t%s\t%.*s\t%.*s\t%s\n", kind,
            (int)d->ns_name.len, d->ns_name.len ? d->ns_name.str : "",
            (int)name.len, name.str, bases);

    for (int i = 0; i < d->type_decl.members.count; i++) {
        zan_ast_node_t *m = d->type_decl.members.items[i];
        if (!m) continue;
        int is_static;
        char sig[1024];
        switch (m->kind) {
        case AST_METHOD_DECL:
            is_static = (m->method_decl.modifiers & MOD_STATIC) ? 1 : 0;
            method_sig(m, sig, (int)sizeof(sig));
            fprintf(f, "M\t%.*s\t%.*s\tM\t%d\t%s\n",
                    (int)name.len, name.str,
                    (int)m->method_decl.name.len, m->method_decl.name.str,
                    is_static, sig);
            break;
        case AST_CONSTRUCTOR_DECL:
            method_sig(m, sig, (int)sizeof(sig));
            fprintf(f, "M\t%.*s\t%.*s\tC\t0\t%s\n",
                    (int)name.len, name.str,
                    (int)name.len, name.str, sig);
            break;
        case AST_PROPERTY_DECL:
        case AST_FIELD_DECL: {
            char ft[256];
            type_name(m->field_decl.type, ft, (int)sizeof(ft));
            is_static = (m->field_decl.modifiers & MOD_STATIC) ? 1 : 0;
            char mk = (m->kind == AST_PROPERTY_DECL) ? 'P' : 'F';
            if (m->field_decl.modifiers & MOD_EVENT) mk = 'E';
            fprintf(f, "M\t%.*s\t%.*s\t%c\t%d\t%s %.*s\n",
                    (int)name.len, name.str,
                    (int)m->field_decl.name.len, m->field_decl.name.str,
                    mk, is_static, ft,
                    (int)m->field_decl.name.len, m->field_decl.name.str);
            break;
        }
        case AST_ENUM_MEMBER:
            fprintf(f, "M\t%.*s\t%.*s\tF\t1\t%.*s %.*s\n",
                    (int)name.len, name.str,
                    (int)m->enum_member.name.len, m->enum_member.name.str,
                    (int)name.len, name.str,
                    (int)m->enum_member.name.len, m->enum_member.name.str);
            break;
        default:
            break;
        }
    }
}

static void emit_decls(FILE *f, zan_ast_list_t *decls) {
    for (int i = 0; i < decls->count; i++) {
        zan_ast_node_t *d = decls->items[i];
        if (!d) continue;
        if (d->kind == AST_NAMESPACE_DECL) {
            emit_decls(f, &d->namespace_decl.members);
        } else {
            emit_type(f, d);
        }
    }
}

int zan_symbols_emit(zan_ast_node_t *unit, const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) return 1;

    fprintf(f, "# zan-symbols 1\n");

    int bcount = 0;
    const zan_builtin_type_t *bts = zan_builtin_types(&bcount);
    for (int i = 0; i < bcount; i++) {
        fprintf(f, "T\tclass\t*builtin*\t%s\t\n", bts[i].type);
        for (int j = 0; j < bts[i].member_count; j++) {
            fprintf(f, "M\t%s\t%s\t%c\t%d\t%s\n", bts[i].type,
                    bts[i].members[j].name, bts[i].members[j].kind,
                    bts[i].is_static ? 1 : 0, bts[i].members[j].sig);
        }
    }

    if (unit && unit->kind == AST_COMPILATION_UNIT) {
        if (unit->comp_unit.ns)
            emit_decls(f, &unit->comp_unit.ns->namespace_decl.members);
        emit_decls(f, &unit->comp_unit.decls);
    }

    fclose(f);
    return 0;
}
