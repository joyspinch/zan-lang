
#include "symbols.h"
#include "builtin_api.h"
#include "win_utf8.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define fopen zan_utf8_fopen
#endif

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

static int is_extern_decl(const zan_ast_node_t *m) {
    return m->kind == AST_METHOD_DECL && !m->method_decl.body &&
           (zan_ast_method_extern_lib(m).str != NULL ||
            (m->method_decl.modifiers & MOD_EXTERN) != 0);
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
    zan_istr_t ns = zan_ast_ns_name(d);
    fprintf(f, "T\t%s\t%.*s\t%.*s\t%s\n", kind,
            (int)ns.len, ns.len ? ns.str : "",
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
            fprintf(f, "M\t%.*s\t%.*s\tM\t%d\t%s\t%s\n",
                    (int)name.len, name.str,
                    (int)m->method_decl.name.len, m->method_decl.name.str,
                    is_static, sig, is_extern_decl(m) ? "x" : "");
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
        fprintf(f, "T\tclass\t*builtin*\t%s\t\n", bts[i].name_public);
        for (int j = 0; j < bts[i].member_count; j++) {
            fprintf(f, "M\t%s\t%s\t%c\t%d\t%s\n", bts[i].name_public,
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

/* ===================================================================
 * S4（docs/ZAN_STANDARD.md §1）：zanc --summary —— 紧凑接口摘要
 *
 * 只摘命令行显式给出的源文件（目录已展开；file_id < explicit_count），
 * 不含 --auto-stdlib/包发现拉进来的库。每个公开类型一个块、每个公开成员
 * 一行，C# 形态的签名后跟一行文档（声明上方紧邻的 `//`/`///` 注释块的
 * 第一句，<summary> 等标签剥掉）。供 AI 一次装入上下文，不生成代码。
 * =================================================================== */

typedef struct {
    FILE              *out;
    const zan_diag_t  *diag;
    int                explicit_count;
    int                types;
    int                members;
} zan_summary_ctx_t;

/* 声明上方紧邻的注释块的第一句（跳过特性行 `[...]` 与空注释行） */
static void summary_doc(const zan_summary_ctx_t *cx, zan_loc_t loc, char *buf, size_t cap) {
    buf[0] = '\0';
    if (!cx->diag || !cx->diag->file_sources || (int)loc.file_id >= cx->diag->file_count ||
        loc.line < 2) return;
    const char *src = cx->diag->file_sources[loc.file_id];
    if (!src) return;
    /* 找到第 loc.line 行的起点 */
    const char *p = src;
    for (uint32_t ln = 1; ln < loc.line && *p; p++) if (*p == '\n') ln++;
    const char *line_start = p;
    const char *first = NULL;
    size_t first_len = 0;
    /* 逐行向上：注释行记下（越靠上越"第一句"），特性行跳过，其他行停止 */
    for (int guard = 0; guard < 40 && line_start > src; guard++) {
        const char *e = line_start - 1;            /* 上一行的 '\n' */
        const char *b = e;
        while (b > src && b[-1] != '\n') b--;
        const char *t = b;
        while (t < e && (*t == ' ' || *t == '\t')) t++;
        size_t n = (size_t)(e - t);
        while (n > 0 && (t[n - 1] == '\r' || t[n - 1] == ' ')) n--;
        if (n >= 2 && t[0] == '/' && t[1] == '/') {
            const char *c = t + 2;
            while (c < t + n && (*c == '/' || *c == ' ')) c++;
            size_t cl = (size_t)(t + n - c);
            if (cl > 0 && !(cl >= 9 && memcmp(c, "</summary", 9) == 0)) {
                first = c;
                first_len = cl;
            }
        } else if (!(n > 0 && t[0] == '[')) {
            break;
        }
        line_start = b;
    }
    if (!first) return;
    size_t o = 0;
    for (size_t i = 0; i < first_len && o + 1 < cap; i++) {
        if (first[i] == '<') {                     /* 剥 XML 标签 */
            while (i < first_len && first[i] != '>') i++;
            continue;
        }
        buf[o++] = first[i];
    }
    while (o > 0 && buf[o - 1] == ' ') o--;
    buf[o] = '\0';
    /* 只留第一句：句号/分号之后截断 */
    char *cut = strstr(buf, "。");
    if (cut) cut[3] = '\0';
    else if ((cut = strstr(buf, ". ")) != NULL) cut[1] = '\0';
}

static const char *summary_access(uint32_t mods) {
    if (mods & MOD_PUBLIC) return "public ";
    if (mods & MOD_PROTECTED) return "protected ";
    return "";
}

static void summary_mods(uint32_t mods, char *buf, size_t cap) {
    snprintf(buf, cap, "%s%s%s%s%s%s%s%s",
             summary_access(mods),
             (mods & MOD_STATIC) ? "static " : "",
             (mods & MOD_ABSTRACT) ? "abstract " : "",
             (mods & MOD_VIRTUAL) ? "virtual " : "",
             (mods & MOD_OVERRIDE) ? "override " : "",
             (mods & MOD_ASYNC) ? "async " : "",
             (mods & MOD_EVENT) ? "event " : "",
             (mods & MOD_WEAK) ? "weak " : "");
}

static void summary_type_params(const zan_ast_list_t *tps, char *buf, size_t cap) {
    buf[0] = '\0';
    if (!tps || tps->count == 0) return;
    size_t n = 0;
    n += (size_t)snprintf(buf + n, cap - n, "<");
    for (int i = 0; i < tps->count && n < cap - 2; i++) {
        zan_ast_node_t *tp = tps->items[i];
        n += (size_t)snprintf(buf + n, cap - n, "%s%.*s", i ? ", " : "",
                              tp ? (int)tp->ident.name.len : 1,
                              tp && tp->ident.name.str ? tp->ident.name.str : "?");
    }
    if (n < cap - 1) snprintf(buf + n, cap - n, ">");
}

static void summary_line(zan_summary_ctx_t *cx, const char *indent, const char *sig,
                         zan_loc_t loc) {
    char doc[256];
    summary_doc(cx, loc, doc, sizeof(doc));
    if (doc[0]) fprintf(cx->out, "%s%s  // %s\n", indent, sig, doc);
    else fprintf(cx->out, "%s%s\n", indent, sig);
}

static bool summary_member_visible(const zan_ast_node_t *owner, uint32_t mods) {
    if (owner->kind == AST_INTERFACE_DECL || owner->kind == AST_ENUM_DECL) return true;
    return (mods & (MOD_PUBLIC | MOD_PROTECTED)) != 0;
}

/* 属性降解出的 get_X / set_X 访问器方法：同类型里有属性 X 时不重复列出 */
static bool summary_is_accessor(const zan_ast_node_t *d, const zan_ast_node_t *m) {
    zan_istr_t nm = m->method_decl.name;
    if (nm.len <= 4 || (memcmp(nm.str, "get_", 4) != 0 && memcmp(nm.str, "set_", 4) != 0))
        return false;
    for (int i = 0; i < d->type_decl.members.count; i++) {
        const zan_ast_node_t *p = d->type_decl.members.items[i];
        if (p && p->kind == AST_PROPERTY_DECL &&
            p->field_decl.name.len == nm.len - 4 &&
            memcmp(p->field_decl.name.str, nm.str + 4, nm.len - 4) == 0)
            return true;
    }
    return false;
}

static void summary_type(zan_summary_ctx_t *cx, zan_ast_node_t *d) {
    const char *kind = type_kind_name(d->kind);
    if (!kind) return;
    if ((int)d->loc.file_id >= cx->explicit_count) return;
    if (d->type_decl.modifiers & (MOD_PRIVATE | MOD_INTERNAL)) return;

    char mods[128], tps[128], bases[512], head[1024];
    summary_mods(d->type_decl.modifiers & ~(uint32_t)MOD_PUBLIC, mods, sizeof(mods));
    summary_type_params(&d->type_decl.type_params, tps, sizeof(tps));
    int bn = 0;
    bases[0] = '\0';
    if (d->kind != AST_ENUM_DECL) {
        for (int i = 0; i < d->type_decl.bases.count && bn < (int)sizeof(bases) - 1; i++) {
            char b[256];
            type_name(d->type_decl.bases.items[i], b, (int)sizeof(b));
            bn += snprintf(bases + bn, sizeof(bases) - (size_t)bn, "%s%s",
                           i ? ", " : " : ", b);
        }
    }
    snprintf(head, sizeof(head), "%s%s%s %.*s%s%s",
             (d->type_decl.modifiers & MOD_PUBLIC) ? "public " : "", mods, kind,
             (int)d->type_decl.name.len, d->type_decl.name.str, tps, bases);
    zan_istr_t ns = zan_ast_ns_name(d);
    char ns_buf[256];
    snprintf(ns_buf, sizeof(ns_buf), "%.*s", (int)ns.len, ns.len ? ns.str : "");
    fprintf(cx->out, "\n");
    summary_line(cx, "", head, d->loc);
    if (ns_buf[0]) fprintf(cx->out, "  // namespace %s\n", ns_buf);
    cx->types++;

    for (int i = 0; i < d->type_decl.members.count; i++) {
        zan_ast_node_t *m = d->type_decl.members.items[i];
        if (!m) continue;
        char sig[1200], mm[128], mt[128];
        switch (m->kind) {
        case AST_METHOD_DECL:
            if (!summary_member_visible(d, m->method_decl.modifiers)) continue;
            if (summary_is_accessor(d, m)) continue;
            summary_mods(m->method_decl.modifiers, mm, sizeof(mm));
            summary_type_params(&m->method_decl.type_params, mt, sizeof(mt));
            {
                char ms[1024];
                method_sig(m, ms, (int)sizeof(ms));
                /* method_sig 形如 "ret Name(params)"：把方法类型参数插到名字后 */
                char *paren = strchr(ms, '(');
                if (mt[0] && paren) {
                    snprintf(sig, sizeof(sig), "%s%.*s%s%s", mm,
                             (int)(paren - ms), ms, mt, paren);
                } else {
                    snprintf(sig, sizeof(sig), "%s%s", mm, ms);
                }
            }
            break;
        case AST_CONSTRUCTOR_DECL: {
            if (!summary_member_visible(d, m->method_decl.modifiers)) continue;
            summary_mods(m->method_decl.modifiers, mm, sizeof(mm));
            char ms[1024];
            method_sig(m, ms, (int)sizeof(ms));
            char *paren = strchr(ms, '(');
            snprintf(sig, sizeof(sig), "%s%.*s%s", mm,
                     (int)d->type_decl.name.len, d->type_decl.name.str,
                     paren ? paren : "()");
            break;
        }
        case AST_PROPERTY_DECL:
        case AST_FIELD_DECL: {
            if (!summary_member_visible(d, m->field_decl.modifiers)) continue;
            char ft[256];
            type_name(m->field_decl.type, ft, (int)sizeof(ft));
            summary_mods(m->field_decl.modifiers, mm, sizeof(mm));
            if (m->kind == AST_PROPERTY_DECL) {
                snprintf(sig, sizeof(sig), "%s%s %.*s { %s%s}", mm, ft,
                         (int)m->field_decl.name.len, m->field_decl.name.str,
                         m->field_decl.has_getter || m->field_decl.getter_body ? "get; " : "",
                         m->field_decl.has_setter || m->field_decl.setter_body ? "set; " :
                         (m->field_decl.has_init ? "init; " : ""));
            } else {
                snprintf(sig, sizeof(sig), "%s%s %.*s", mm, ft,
                         (int)m->field_decl.name.len, m->field_decl.name.str);
            }
            break;
        }
        case AST_ENUM_MEMBER:
            snprintf(sig, sizeof(sig), "%.*s",
                     (int)m->enum_member.name.len, m->enum_member.name.str);
            break;
        default:
            continue;
        }
        summary_line(cx, "  ", sig, m->loc);
        cx->members++;
    }
}

static void summary_decls(zan_summary_ctx_t *cx, zan_ast_list_t *decls) {
    for (int i = 0; i < decls->count; i++) {
        zan_ast_node_t *d = decls->items[i];
        if (!d) continue;
        if (d->kind == AST_NAMESPACE_DECL) summary_decls(cx, &d->namespace_decl.members);
        else summary_type(cx, d);
    }
}

int zan_summary_emit(zan_ast_node_t *unit, const zan_diag_t *diag, int explicit_count,
                     FILE *out) {
    zan_summary_ctx_t cx = { out, diag, explicit_count, 0, 0 };
    fprintf(out, "// zan-summary 1: public surface of %d source file(s)\n", explicit_count);
    if (unit && unit->kind == AST_COMPILATION_UNIT) {
        if (unit->comp_unit.ns)
            summary_decls(&cx, &unit->comp_unit.ns->namespace_decl.members);
        summary_decls(&cx, &unit->comp_unit.decls);
    }
    fprintf(out, "\n// %d type(s), %d member(s)\n", cx.types, cx.members);
    return 0;
}
