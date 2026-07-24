/* routegen.c -- compile-time attribute-driven route generation.  See
 * routegen.h for the high-level contract.  Modeled on jsongen.c: scan the
 * merged unit, generate Zan source into a growable buffer, lex/parse it, and
 * merge the resulting declarations back into the unit.
 */

#include "routegen.h"
#include "arena.h"
#include "diag.h"
#include "lexer.h"
#include "parser.h"
#include "token.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- growable source buffer ---- */

typedef struct {
    char *buf;
    size_t len;
    size_t cap;
} rg_buf_t;

static void rg_putf(rg_buf_t *b, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    va_list ap2;
    va_copy(ap2, ap);
    int need = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (need < 0) { va_end(ap2); return; }
    if (b->len + (size_t)need + 1 > b->cap) {
        size_t ncap = b->cap ? b->cap * 2 : 4096;
        while (b->len + (size_t)need + 1 > ncap) ncap *= 2;
        b->buf = (char *)realloc(b->buf, ncap);
        b->cap = ncap;
    }
    vsnprintf(b->buf + b->len, b->cap - b->len, fmt, ap2);
    va_end(ap2);
    b->len += (size_t)need;
}

/* Append `s`, escaping it for a Zan double-quoted string literal. */
static void rg_put_qstr(rg_buf_t *b, const char *s) {
    for (const char *p = s; *p; p++) {
        unsigned char ch = (unsigned char)*p;
        if (ch == '\\' || ch == '"') { rg_putf(b, "\\%c", ch); }
        else if (ch == '\n') { rg_putf(b, "\\n"); }
        else if (ch == '\r') { rg_putf(b, "\\r"); }
        else if (ch == '\t') { rg_putf(b, "\\t"); }
        else { rg_putf(b, "%c", ch); }
    }
}

/* ---- istr helpers ---- */

static bool istr_is(zan_istr_t s, const char *lit) {
    size_t n = strlen(lit);
    return s.str && s.len == (uint32_t)n && memcmp(s.str, lit, n) == 0;
}

static void istr_to(char *dst, size_t cap, zan_istr_t s) {
    size_t n = (s.str && s.len < cap - 1) ? s.len : (cap ? cap - 1 : 0);
    if (s.str && n) memcpy(dst, s.str, n);
    dst[n] = 0;
}

static void lower_inplace(char *s) {
    for (; *s; s++) if (*s >= 'A' && *s <= 'Z') *s = (char)(*s + 32);
}

/* ---- attribute helpers ---- */

static zan_istr_t attr_name(zan_ast_node_t *attr) {
    zan_istr_t z = {NULL, 0};
    if (!attr || attr->kind != AST_ATTRIBUTE || !attr->attribute.name) return z;
    if (attr->attribute.name->kind == AST_IDENTIFIER)
        return attr->attribute.name->ident.name;
    return z;
}

/* HTTP verb for an [HttpGet]/[HttpPost]/... attribute, or NULL. */
static const char *attr_http_verb(zan_istr_t n) {
    if (istr_is(n, "HttpGet")) return "GET";
    if (istr_is(n, "HttpPost")) return "POST";
    if (istr_is(n, "HttpPut")) return "PUT";
    if (istr_is(n, "HttpDelete")) return "DELETE";
    if (istr_is(n, "HttpPatch")) return "PATCH";
    if (istr_is(n, "HttpHead")) return "HEAD";
    if (istr_is(n, "HttpOptions")) return "OPTIONS";
    return NULL;
}

/* True for attributes the pass interprets structurally (not custom metadata). */
static bool attr_is_structural(zan_istr_t n) {
    return attr_http_verb(n) != NULL || istr_is(n, "Route") ||
           istr_is(n, "ApiController") || istr_is(n, "NonAction") ||
           istr_is(n, "Description") || istr_is(n, "AttributeUsage") ||
           istr_is(n, "DllImport") || istr_is(n, "StructLayout") ||
           istr_is(n, "Obsolete");
}

/* ---- declaration lookup ---- */

static zan_ast_node_t *find_type(zan_ast_node_t *unit, const char *name,
                                 zan_ast_kind_t kind) {
    size_t nl = strlen(name);
    for (int i = 0; i < unit->comp_unit.decls.count; i++) {
        zan_ast_node_t *d = unit->comp_unit.decls.items[i];
        if (d->kind != kind) continue;
        zan_istr_t dn = d->type_decl.name;
        if (dn.str && dn.len == (uint32_t)nl && memcmp(dn.str, name, nl) == 0)
            return d;
    }
    return NULL;
}

/* Resolve an enum member to its integer value (explicit int literal or its
 * ordinal position, C#-style auto-increment approximation). */
static long long enum_value(zan_ast_node_t *unit, const char *enum_name,
                            const char *member) {
    zan_ast_node_t *e = find_type(unit, enum_name, AST_ENUM_DECL);
    if (!e) return 0;
    long long running = 0;
    for (int i = 0; i < e->type_decl.members.count; i++) {
        zan_ast_node_t *m = e->type_decl.members.items[i];
        if (m->kind != AST_ENUM_MEMBER) continue;
        long long v = running;
        if (m->enum_member.value && m->enum_member.value->kind == AST_INT_LITERAL)
            v = m->enum_member.value->int_val;
        char mn[128]; istr_to(mn, sizeof(mn), m->enum_member.name);
        if (strcmp(mn, member) == 0) return v;
        running = v + 1;
    }
    return 0;
}

/* ---- compile-time constant value ---- */

typedef struct {
    int ok;
    int kind;          /* 0 int, 1 bool, 2 string, 3 enum-member */
    long long ival;    /* int / bool / enum numeric */
    char sval[256];    /* string text / enum member name */
} rg_cval;

static rg_cval eval_const(zan_ast_node_t *unit, zan_ast_node_t *e) {
    rg_cval r; memset(&r, 0, sizeof(r));
    if (!e) return r;
    switch (e->kind) {
        case AST_INT_LITERAL:
            r.ok = 1; r.kind = 0; r.ival = e->int_val;
            snprintf(r.sval, sizeof(r.sval), "%lld", (long long)e->int_val);
            return r;
        case AST_BOOL_LITERAL:
            r.ok = 1; r.kind = 1; r.ival = e->bool_val ? 1 : 0;
            snprintf(r.sval, sizeof(r.sval), "%s", e->bool_val ? "true" : "false");
            return r;
        case AST_STRING_LITERAL:
            r.ok = 1; r.kind = 2; istr_to(r.sval, sizeof(r.sval), e->str_val);
            return r;
        case AST_MEMBER_ACCESS: {
            zan_ast_node_t *o = e->member.object;
            char et[128];
            if (o && o->kind == AST_IDENTIFIER) {
                istr_to(et, sizeof(et), o->ident.name);
                istr_to(r.sval, sizeof(r.sval), e->member.name);
                r.ok = 1; r.kind = 3;
                r.ival = enum_value(unit, et, r.sval);
            }
            return r;
        }
        case AST_IDENTIFIER:
            r.ok = 1; r.kind = 3; istr_to(r.sval, sizeof(r.sval), e->ident.name);
            return r;
        case AST_UNARY:
            if (e->unary.op == TK_MINUS) {
                rg_cval inner = eval_const(unit, e->unary.operand);
                if (inner.ok && inner.kind == 0) {
                    r.ok = 1; r.kind = 0; r.ival = -inner.ival;
                    snprintf(r.sval, sizeof(r.sval), "%lld", (long long)r.ival);
                }
            }
            return r;
        default:
            return r;
    }
}

/* ---- merged metadata map (prop name -> value) ---- */

typedef struct { char key[64]; rg_cval val; } rg_ent;
typedef struct { rg_ent items[48]; int count; } rg_meta;

static rg_ent *meta_get(rg_meta *m, const char *k) {
    for (int i = 0; i < m->count; i++)
        if (strcmp(m->items[i].key, k) == 0) return &m->items[i];
    return NULL;
}

static void meta_set(rg_meta *m, const char *k, rg_cval v, int only_absent) {
    rg_ent *e = meta_get(m, k);
    if (e) { if (!only_absent) e->val = v; return; }
    if (m->count >= (int)(sizeof(m->items) / sizeof(m->items[0]))) return;
    e = &m->items[m->count++];
    snprintf(e->key, sizeof(e->key), "%s", k);
    e->val = v;
}

/* Build param-name -> property-name map for a ctor by scanning simple
 * `Prop = param;` assignments in its body (covers the common attribute ctor). */
typedef struct { char param[64]; char prop[64]; } rg_pmap_ent;
typedef struct { rg_pmap_ent items[32]; int count; } rg_pmap;

static void scan_ctor_assigns(zan_ast_node_t *ctor, rg_pmap *pm) {
    if (!ctor || !ctor->method_decl.body) return;
    zan_ast_node_t *body = ctor->method_decl.body;
    if (body->kind != AST_BLOCK) return;
    for (int i = 0; i < body->block.stmts.count; i++) {
        zan_ast_node_t *st = body->block.stmts.items[i];
        if (!st || st->kind != AST_EXPR_STMT) continue;
        zan_ast_node_t *ex = st->expr_stmt.expr;
        if (!ex || ex->kind != AST_ASSIGNMENT) continue;
        zan_ast_node_t *l = ex->binary.left;
        zan_ast_node_t *rt = ex->binary.right;
        const char *propn = NULL, *paramn = NULL;
        char lb[64], rb[64];
        if (l && l->kind == AST_IDENTIFIER) { istr_to(lb, sizeof(lb), l->ident.name); propn = lb; }
        else if (l && l->kind == AST_MEMBER_ACCESS && l->member.object &&
                 (l->member.object->kind == AST_THIS_EXPR ||
                  (l->member.object->kind == AST_IDENTIFIER &&
                   istr_is(l->member.object->ident.name, "this")))) {
            istr_to(lb, sizeof(lb), l->member.name); propn = lb;
        }
        if (rt && rt->kind == AST_IDENTIFIER) { istr_to(rb, sizeof(rb), rt->ident.name); paramn = rb; }
        if (propn && paramn && pm->count < (int)(sizeof(pm->items)/sizeof(pm->items[0]))) {
            snprintf(pm->items[pm->count].param, 64, "%s", paramn);
            snprintf(pm->items[pm->count].prop, 64, "%s", propn);
            pm->count++;
        }
    }
}

static const char *pmap_prop(rg_pmap *pm, const char *param) {
    for (int i = 0; i < pm->count; i++)
        if (strcmp(pm->items[i].param, param) == 0) return pm->items[i].prop;
    return NULL;
}

/* Apply one attribute usage into the merged map: attribute-class defaults
 * (only for absent keys) then the usage's positional/named arguments (always
 * override). */
static void apply_attr(zan_ast_node_t *unit, rg_meta *m, zan_ast_node_t *attr) {
    zan_istr_t an = attr_name(attr);
    char base[128]; istr_to(base, sizeof(base), an);
    char withsuf[160]; snprintf(withsuf, sizeof(withsuf), "%sAttribute", base);

    zan_ast_node_t *cls = find_type(unit, base, AST_CLASS_DECL);
    if (!cls) cls = find_type(unit, withsuf, AST_CLASS_DECL);

    rg_pmap pm; pm.count = 0;
    zan_ast_node_t *ctor = NULL;
    if (cls) {
        for (int i = 0; i < cls->type_decl.members.count; i++) {
            zan_ast_node_t *mem = cls->type_decl.members.items[i];
            if (mem->kind == AST_CONSTRUCTOR_DECL && !ctor) ctor = mem;
        }
        if (ctor) scan_ctor_assigns(ctor, &pm);
        /* property / field initializer defaults (absent-only) */
        for (int i = 0; i < cls->type_decl.members.count; i++) {
            zan_ast_node_t *mem = cls->type_decl.members.items[i];
            if ((mem->kind == AST_PROPERTY_DECL || mem->kind == AST_FIELD_DECL) &&
                mem->field_decl.initializer) {
                rg_cval v = eval_const(unit, mem->field_decl.initializer);
                if (v.ok) {
                    char pn[64]; istr_to(pn, sizeof(pn), mem->field_decl.name);
                    meta_set(m, pn, v, 1);
                }
            }
        }
        /* ctor parameter defaults mapped through Prop=param (absent-only) */
        if (ctor) {
            for (int i = 0; i < ctor->method_decl.params.count; i++) {
                zan_ast_node_t *p = ctor->method_decl.params.items[i];
                if (p->kind != AST_PARAM || !p->param.default_val) continue;
                char paramn[64]; istr_to(paramn, sizeof(paramn), p->param.name);
                const char *prop = pmap_prop(&pm, paramn);
                if (!prop) continue;
                rg_cval v = eval_const(unit, p->param.default_val);
                if (v.ok) meta_set(m, prop, v, 1);
            }
        }
    }

    /* usage arguments (override) */
    int posi = 0;
    for (int i = 0; i < attr->attribute.args.count; i++) {
        zan_ast_node_t *a = attr->attribute.args.items[i];
        if (a->kind == AST_ASSIGNMENT && a->binary.left &&
            a->binary.left->kind == AST_IDENTIFIER) {
            char pn[64]; istr_to(pn, sizeof(pn), a->binary.left->ident.name);
            rg_cval v = eval_const(unit, a->binary.right);
            if (v.ok) meta_set(m, pn, v, 0);
        } else {
            /* positional -> ctor param -> property */
            const char *prop = NULL;
            if (ctor && posi < ctor->method_decl.params.count) {
                zan_ast_node_t *p = ctor->method_decl.params.items[posi];
                if (p->kind == AST_PARAM) {
                    char paramn[64]; istr_to(paramn, sizeof(paramn), p->param.name);
                    prop = pmap_prop(&pm, paramn);
                }
            }
            rg_cval v = eval_const(unit, a);
            if (prop && v.ok) meta_set(m, prop, v, 0);
            posi++;
        }
    }
}

/* ---- controller / action detection ---- */

static bool class_has_attr(zan_ast_node_t *cls, const char *nm) {
    for (int i = 0; i < cls->attributes.count; i++)
        if (istr_is(attr_name(cls->attributes.items[i]), nm)) return true;
    return false;
}

static bool class_route_template(zan_ast_node_t *cls, char *out, size_t cap) {
    for (int i = 0; i < cls->attributes.count; i++) {
        zan_ast_node_t *a = cls->attributes.items[i];
        if (istr_is(attr_name(a), "Route") && a->attribute.args.count >= 1 &&
            a->attribute.args.items[0]->kind == AST_STRING_LITERAL) {
            istr_to(out, cap, a->attribute.args.items[0]->str_val);
            return true;
        }
    }
    out[0] = 0;
    return false;
}

static bool derives_from(zan_ast_node_t *cls, const char *basename) {
    for (int i = 0; i < cls->type_decl.bases.count; i++) {
        zan_ast_node_t *b = cls->type_decl.bases.items[i];
        if (b && b->kind == AST_TYPE_REF && istr_is(b->type_ref.name, basename))
            return true;
    }
    return false;
}

static bool is_controller(zan_ast_node_t *cls) {
    char nm[128]; istr_to(nm, sizeof(nm), cls->type_decl.name);
    size_t l = strlen(nm);
    if (l >= 10 && strcmp(nm + l - 10, "Controller") == 0) return true;
    if (derives_from(cls, "Controller") || derives_from(cls, "ApiController"))
        return true;
    if (class_has_attr(cls, "Route") || class_has_attr(cls, "ApiController"))
        return true;
    return false;
}

/* method-level route template (first [Route] string arg) */
static bool method_route_template(zan_ast_node_t *m, char *out, size_t cap) {
    for (int i = 0; i < m->attributes.count; i++) {
        zan_ast_node_t *a = m->attributes.items[i];
        if (istr_is(attr_name(a), "Route") && a->attribute.args.count >= 1 &&
            a->attribute.args.items[0]->kind == AST_STRING_LITERAL) {
            istr_to(out, cap, a->attribute.args.items[0]->str_val);
            return true;
        }
    }
    out[0] = 0;
    return false;
}

static const char *method_verb(zan_ast_node_t *m) {
    for (int i = 0; i < m->attributes.count; i++) {
        const char *v = attr_http_verb(attr_name(m->attributes.items[i]));
        if (v) return v;
    }
    return NULL;
}

static bool method_has_route_or_verb(zan_ast_node_t *m) {
    for (int i = 0; i < m->attributes.count; i++) {
        zan_istr_t n = attr_name(m->attributes.items[i]);
        if (attr_http_verb(n) || istr_is(n, "Route")) return true;
    }
    return false;
}

static bool method_has_attr(zan_ast_node_t *m, const char *nm) {
    for (int i = 0; i < m->attributes.count; i++)
        if (istr_is(attr_name(m->attributes.items[i]), nm)) return true;
    return false;
}

/* first [Description] text on a decl */
static bool decl_description(zan_ast_node_t *d, char *out, size_t cap) {
    for (int i = 0; i < d->attributes.count; i++) {
        zan_ast_node_t *a = d->attributes.items[i];
        if (istr_is(attr_name(a), "Description") && a->attribute.args.count >= 1 &&
            a->attribute.args.items[0]->kind == AST_STRING_LITERAL) {
            istr_to(out, cap, a->attribute.args.items[0]->str_val);
            return true;
        }
    }
    out[0] = 0;
    return false;
}

/* original (pre-mangling) simple class name; nsresolve renames duplicate
 * controllers (e.g. two module-local `Index`) but keeps the source name in
 * orig_name so routes/view keys stay human-readable. */
static void controller_display(zan_ast_node_t *cls, char *out, size_t cap) {
    zan_istr_t n = cls->orig_name.len ? cls->orig_name : cls->type_decl.name;
    istr_to(out, cap, n);
}

/* module = last segment of the controller's namespace ("" if global). It
 * mirrors the module directory so view keys match View.zan's path-derived
 * keys and duplicate controllers get distinct keys. */
static void controller_module(zan_ast_node_t *cls, char *out, size_t cap) {
    char ns[256]; istr_to(ns, sizeof(ns), cls->ns_name);
    const char *last = ns;
    for (char *q = ns; *q; q++) if (*q == '.') last = q + 1;
    snprintf(out, cap, "%s", last);
}

/* controller token = original class name minus trailing "Controller", lowercased */
static void controller_token(zan_ast_node_t *cls, char *out, size_t cap) {
    char nm[128]; controller_display(cls, nm, sizeof(nm));
    size_t l = strlen(nm);
    if (l >= 10 && strcmp(nm + l - 10, "Controller") == 0) nm[l - 10] = 0;
    snprintf(out, cap, "%s", nm);
    lower_inplace(out);
}

/* Combine class + method route templates, substitute [controller]/[action],
 * normalize slashes. */
static void build_path(const char *cls_tpl, const char *m_tpl,
                       const char *ctrl_tok, const char *action_tok,
                       char *out, size_t cap) {
    char tpl[512];
    if (m_tpl[0] == '/') {
        snprintf(tpl, sizeof(tpl), "%s", m_tpl);
    } else if (m_tpl[0]) {
        if (cls_tpl[0]) snprintf(tpl, sizeof(tpl), "%s/%s", cls_tpl, m_tpl);
        else snprintf(tpl, sizeof(tpl), "%s", m_tpl);
    } else {
        snprintf(tpl, sizeof(tpl), "%s", cls_tpl);
    }
    if (!strstr(tpl, "[action]") && m_tpl[0] != '/') {
        size_t tl = strlen(tpl);
        if (tl && tpl[tl - 1] == '/') snprintf(tpl + tl, sizeof(tpl) - tl, "[action]");
        else snprintf(tpl + tl, sizeof(tpl) - tl, "/[action]");
    }
    /* token substitution + slash normalization into out */
    char sub[512]; size_t si = 0;
    for (size_t i = 0; tpl[i] && si < sizeof(sub) - 1;) {
        if (strncmp(tpl + i, "[controller]", 12) == 0) {
            for (const char *p = ctrl_tok; *p && si < sizeof(sub) - 1; p++) sub[si++] = *p;
            i += 12;
        } else if (strncmp(tpl + i, "[action]", 8) == 0) {
            for (const char *p = action_tok; *p && si < sizeof(sub) - 1; p++) sub[si++] = *p;
            i += 8;
        } else {
            sub[si++] = tpl[i++];
        }
    }
    sub[si] = 0;
    /* ensure leading slash, collapse duplicate slashes, lower nothing else */
    size_t oi = 0;
    if (cap) out[oi++] = '/';
    for (size_t i = 0; sub[i] && oi < cap - 1; i++) {
        if (sub[i] == '/') {
            if (oi > 0 && out[oi - 1] == '/') continue;
            out[oi++] = '/';
        } else {
            out[oi++] = sub[i];
        }
    }
    while (oi > 1 && out[oi - 1] == '/') oi--;   /* drop trailing slash (keep root) */
    out[oi] = 0;
}

/* ---- fluent metadata emission ---- */

static void emit_fluent(rg_buf_t *out, rg_meta *m, const char *title) {
    if (title && title[0]) { rg_putf(out, ".Title(\""); rg_put_qstr(out, title); rg_putf(out, "\")"); }

    rg_ent *e;
    /* Authorization enum -> login/auth enforcement */
    e = meta_get(m, "Authorization");
    if (e && e->val.kind == 3) {
        if (strcmp(e->val.sval, "Login") == 0) rg_putf(out, ".Login()");
        else if (strcmp(e->val.sval, "Auth") == 0) rg_putf(out, ".Login()");
        else if (strcmp(e->val.sval, "ApiAuth") == 0) rg_putf(out, ".Auth()");
    }
    /* IsMenu -> surface in admin menu (boolean flag on the route) */
    e = meta_get(m, "IsMenu");
    if (e && e->val.kind == 1 && e->val.ival) rg_putf(out, ".Menu()");
    /* optional route-only conventions (not part of the user attribute class):
     * Lock -> per-request lock scope, Upload -> stream body to disk.  Rank /
     * ApiMax / Component / Icon / ContentType are menu/response metadata and
     * are carried faithfully via .Meta(...) below, NOT turned into auth gates. */
    e = meta_get(m, "Lock");
    if (e && e->val.kind == 2 && e->val.sval[0]) {
        rg_putf(out, ".Lock(\""); rg_put_qstr(out, e->val.sval); rg_putf(out, "\")");
    }
    e = meta_get(m, "Upload");
    if (e && e->val.kind == 1 && e->val.ival) rg_putf(out, ".Upload()");

    /* faithful generic metadata for everything evaluated */
    for (int i = 0; i < m->count; i++) {
        rg_putf(out, ".Meta(\"");
        rg_put_qstr(out, m->items[i].key);
        rg_putf(out, "\", \"");
        rg_put_qstr(out, m->items[i].val.sval);
        rg_putf(out, "\")");
    }
}

/* ---- entry point ---- */

void zan_routegen_run(zan_ast_node_t *unit, zan_arena_t *arena,
                      zan_diag_t *diag) {
    if (zan_diag_has_errors(diag)) return;

    rg_buf_t handlers; memset(&handlers, 0, sizeof(handlers));
    rg_buf_t reg; memset(&reg, 0, sizeof(reg));
    int nroutes = 0;

    for (int i = 0; i < unit->comp_unit.decls.count; i++) {
        zan_ast_node_t *cls = unit->comp_unit.decls.items[i];
        if (cls->kind != AST_CLASS_DECL) continue;
        if (!is_controller(cls)) continue;

        char cname[128]; istr_to(cname, sizeof(cname), cls->type_decl.name);
        char ctrltok[128]; controller_token(cls, ctrltok, sizeof(ctrltok));
        char cmod[128]; controller_module(cls, cmod, sizeof(cmod));
        char cdisp[128]; controller_display(cls, cdisp, sizeof(cdisp));
        char cls_tpl[256]; class_route_template(cls, cls_tpl, sizeof(cls_tpl));
        char cls_desc[256]; decl_description(cls, cls_desc, sizeof(cls_desc));
        bool api_controller = class_has_attr(cls, "ApiController");

        for (int mi = 0; mi < cls->type_decl.members.count; mi++) {
            zan_ast_node_t *m = cls->type_decl.members.items[mi];
            if (m->kind != AST_METHOD_DECL) continue;
            if (method_has_attr(m, "NonAction")) continue;

            bool tagged = method_has_route_or_verb(m);
            if (!tagged && !api_controller) continue;

            /* only public actions become routes */
            if (m->method_decl.modifiers & (MOD_PRIVATE | MOD_PROTECTED)) continue;

            /* supported shapes: 0 params, or single HttpContext param */
            int np = m->method_decl.params.count;
            bool ctx_param = false;
            if (np == 1) {
                zan_ast_node_t *p0 = m->method_decl.params.items[0];
                if (p0->kind == AST_PARAM && p0->param.type &&
                    p0->param.type->kind == AST_TYPE_REF &&
                    istr_is(p0->param.type->type_ref.name, "HttpContext"))
                    ctx_param = true;
            }
            if (np != 0 && !ctx_param) continue;

            char mname[128]; istr_to(mname, sizeof(mname), m->method_decl.name);
            char actiontok[128]; snprintf(actiontok, sizeof(actiontok), "%s", mname);
            lower_inplace(actiontok);

            const char *verb = method_verb(m);
            if (!verb) verb = "GET";

            char m_tpl[256]; method_route_template(m, m_tpl, sizeof(m_tpl));
            char path[600];
            build_path(cls_tpl, m_tpl, ctrltok, actiontok, path, sizeof(path));

            /* three-layer metadata merge: class then method (method overrides) */
            rg_meta meta; meta.count = 0;
            for (int a = 0; a < cls->attributes.count; a++) {
                zan_ast_node_t *at = cls->attributes.items[a];
                if (!attr_is_structural(attr_name(at))) apply_attr(unit, &meta, at);
            }
            for (int a = 0; a < m->attributes.count; a++) {
                zan_ast_node_t *at = m->attributes.items[a];
                if (!attr_is_structural(attr_name(at))) apply_attr(unit, &meta, at);
            }
            char m_desc[256]; bool has_md = decl_description(m, m_desc, sizeof(m_desc));
            const char *title = has_md ? m_desc : cls_desc;

            bool is_static = (m->method_decl.modifiers & MOD_STATIC) != 0;
            bool is_async = (m->method_decl.modifiers & MOD_ASYNC) != 0;
            const char *hname_fmt = "__h_%s_%s";
            char hname[300]; snprintf(hname, sizeof(hname), hname_fmt, cname, mname);

            /* Trampoline. It is `async` so it satisfies the async HttpHandler
             * delegate (WebApp.Dispatch awaits it), letting the action await
             * I/O without blocking the connection coroutine. An async action is
             * awaited; a plain action is called directly inside the async body. */
            const char *aw = is_async ? "await " : "";
            rg_putf(&handlers, "    static async void %s(HttpContext ctx) {\n", hname);
            if (is_static) {
                rg_putf(&handlers, "        %s%s.%s(%s);\n", aw, cname, mname,
                        ctx_param ? "ctx" : "");
            } else {
                rg_putf(&handlers, "        %s __c = new %s();\n", cname, cname);
                rg_putf(&handlers, "        __c.__Bind(ctx);\n");
                char vkey[320];
                if (cmod[0]) snprintf(vkey, sizeof(vkey), "%s.%s.%s", cmod, cdisp, mname);
                else snprintf(vkey, sizeof(vkey), "%s.%s", cdisp, mname);
                rg_putf(&handlers, "        __c.__SetView(\"%s\");\n", vkey);
                rg_putf(&handlers, "        if (__c.__Before()) {\n");
                rg_putf(&handlers, "            %s__c.%s(%s);\n", aw, mname, ctx_param ? "ctx" : "");
                rg_putf(&handlers, "            __c.__After();\n");
                rg_putf(&handlers, "        }\n");
            }
            rg_putf(&handlers, "    }\n");

            /* registration */
            rg_putf(&reg, "        app.Map(\"%s\", \"", verb);
            rg_put_qstr(&reg, path);
            rg_putf(&reg, "\", __AttrRoutes.%s)\n            .Named(\"", hname);
            rg_put_qstr(&reg, cname); rg_putf(&reg, "."); rg_put_qstr(&reg, mname);
            rg_putf(&reg, "\")");
            emit_fluent(&reg, &meta, title);
            rg_putf(&reg, ";\n");
            nroutes++;
        }
    }

    if (nroutes == 0 || zan_diag_has_errors(diag)) {
        free(handlers.buf); free(reg.buf);
        return;
    }

    rg_buf_t out; memset(&out, 0, sizeof(out));
    rg_putf(&out, "class __AttrRoutes {\n");
    if (handlers.len) rg_putf(&out, "%.*s", (int)handlers.len, handlers.buf);
    rg_putf(&out, "    static void Register(WebApp app) {\n");
    if (reg.len) rg_putf(&out, "%.*s", (int)reg.len, reg.buf);
    rg_putf(&out, "    }\n}\n");
    free(handlers.buf); free(reg.buf);

    char *gsrc = zan_arena_strdup(arena, out.buf, out.len);
    size_t glen = out.len;
    free(out.buf);

    zan_lexer_t lex;
    zan_lexer_init(&lex, gsrc, glen, 0, arena, diag);
    zan_parser_t gp;
    zan_parser_init(&gp, &lex, arena, diag);
    zan_ast_node_t *gu = zan_parser_parse(&gp);
    if (!gu) return;
    for (int i = 0; i < gu->comp_unit.decls.count; i++)
        zan_ast_list_push(&unit->comp_unit.decls, gu->comp_unit.decls.items[i], arena);
}
