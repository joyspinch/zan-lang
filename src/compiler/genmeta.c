/* genmeta.c -- generic compilation-unit metadata exporter (see genmeta.h).
 *
 * Output shape:
 *   { "version": 1,
 *     "files":   [ "src/main.zan", ... ],   // only in _export_files
 *     "classes": [ { "name","ns","kind","file","line","bases":[...],
 *                    "attrs":[...],
 *                    "fields":[{ "name","type","file","line","attrs" }],
 *                    "methods":[{ "name","static","async","file","line",
 *                                 "params":[{ "name","type" }],"attrs" }],
 *                    "members":[ { "name","value" } ] } ],     // enums
 *     "calls":  [ { "id","file","line","col","name","recv",
 *                   "targs":[...],"args":[...] } ] }
 *
 * `recv` is a short receiver shape ("this", "id:<name>", "mem:a.b",
 * "call", "other"); `args` items are shape objects -- literals, ids, null,
 * lambda (with an expression tree), or other. The calls array contains only
 * generator entry points and their fluent receiver chains; ids still refer
 * to the complete compilation-unit traversal and can therefore be sparse.
 * Every expression-tree node is { "k": kind, ... } with "k" in bin/uni/mem/
 * id/lit/call/cond/cast/is/idx/assign/new/lam/null/this/base/await/other. */

#include "genmeta.h"

#include "ast.h"
#include "zan.h"
#include "diag.h"
#include "lexer.h"
#include "../common/json.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../common/host_oom.h"

typedef struct {
    json_value *calls;   /* json array */
    int call_id;
    char cur_cls[128];   /* enclosing class name (call owner) */
    char cur_fn[128];    /* enclosing method name ("" for field initializers) */
    /* node -> id, filled when a call is recorded. The walk is children-first
     * (the same order the old C dbgen visited), so a receiver call is recorded before
     * the outer call that names it as `call#<id>`. Grows as needed. */
    zan_ast_node_t **rec_nodes;
    int *rec_ids;
    int rec_count;
    int rec_cap;
    /* receiver-is-a-call placeholders (defensive fallback only) */
    zan_ast_node_t **pend_nodes;
    int *pend_ids;
    int pend_count;
    int pend_cap;
} gm_ctx_t;

static json_value *gm_arg_json(zan_ast_node_t *n);

/* ---- interned-string helpers (same semantics as the generator files) ---- */

static void gm_istr_to(char *dst, size_t cap, zan_istr_t s) {
    size_t n = (s.str && s.len < cap - 1) ? s.len : (cap ? cap - 1 : 0);
    if (s.str && n) memcpy(dst, s.str, n);
    dst[n] = 0;
}

static bool gm_istr_is(zan_istr_t s, const char *lit) {
    size_t n = strlen(lit);
    return s.str && s.len == (uint32_t)n && memcmp(s.str, lit, n) == 0;
}

/* ---- type reference -> string ---- */

/* Append `fmt` at `pos` into `out`/`cap`, keeping the cursor inside the
 * buffer: snprintf returns the *would-be* length, so accumulating it raw
 * let a truncated write send `out + n` past the caller's stack buffer
 * (with `outsz - n` wrapping to a huge size_t). Saturates at cap-1. */
static size_t gm_snpcat(char *out, size_t cap, size_t pos,
                        const char *fmt, ...) {
    if (cap == 0) return 0;
    if (pos >= cap) pos = cap - 1;
    va_list ap;
    va_start(ap, fmt);
    int w = vsnprintf(out + pos, cap - pos, fmt, ap);
    va_end(ap);
    if (w < 0 || (size_t)w >= cap - pos) return cap - 1;
    return pos + (size_t)w;
}

static void gm_type_str(zan_ast_node_t *t, char *out, size_t outsz) {
    if (!t) { out[0] = '\0'; return; }
    switch (t->kind) {
    case AST_TYPE_REF: {
        size_t n = gm_snpcat(out, outsz, 0, "%.*s",
                             (int)t->type_ref.name.len,
                             t->type_ref.name.str ? t->type_ref.name.str : "");
        if (t->type_ref.type_args.count > 0) {
            n = gm_snpcat(out, outsz, n, "<");
            for (int i = 0; i < t->type_ref.type_args.count; i++) {
                if (i) n = gm_snpcat(out, outsz, n, ",");
                char tmp[256];
                gm_type_str(t->type_ref.type_args.items[i], tmp, sizeof(tmp));
                n = gm_snpcat(out, outsz, n, "%s", tmp);
            }
            n = gm_snpcat(out, outsz, n, ">");
        }
        if (t->type_ref.is_array)
            n = gm_snpcat(out, outsz, n, "[]");
        if (t->type_ref.is_nullable)
            gm_snpcat(out, outsz, n, "?");
        break;
    }
    case AST_ARRAY_TYPE:
    case AST_NULLABLE_TYPE:
        /* bare marker nodes (no union fields): leave unnamed */
        out[0] = '\0';
        break;
    case AST_QUALIFIED_NAME: {
        size_t n = 0;
        for (int i = 0; i < t->qualified_name.parts.count; i++) {
            zan_ast_node_t *p = t->qualified_name.parts.items[i];
            if (!p || p->kind != AST_IDENTIFIER) continue;
            if (n) n = gm_snpcat(out, outsz, n, ".");
            n = gm_snpcat(out, outsz, n, "%.*s", (int)p->ident.name.len,
                          p->ident.name.str ? p->ident.name.str : "");
        }
        break;
    }
    default:
        out[0] = '\0';
        break;
    }
}

/* ---- attributes ---- */

static void gm_attr_json(zan_ast_node_t *attr, json_value *obj) {
    char nm[256];
    nm[0] = '\0';
    if (attr->kind == AST_ATTRIBUTE) {
        if (attr->attribute.name->kind == AST_IDENTIFIER)
            snprintf(nm, sizeof(nm), "%.*s",
                     (int)attr->attribute.name->ident.name.len,
                     attr->attribute.name->ident.name.str);
        else
            gm_type_str(attr->attribute.name, nm, sizeof(nm));
        json_obj_set(obj, "name", json_new_str(nm));
        json_value *args = json_new_arr();
        for (int i = 0; i < attr->attribute.args.count; i++) {
            json_value *a = gm_arg_json(attr->attribute.args.items[i]);
            if (!a) a = json_new_str("?");
            json_arr_add(args, a);
        }
        json_obj_set(obj, "args", args);
    } else {
        json_obj_set(obj, "name", json_new_str("?"));
        json_obj_set(obj, "args", json_new_arr());
    }
}

static void gm_attrs_json(zan_ast_list_t *attrs, json_value *arr) {
    for (int i = 0; i < attrs->count; i++) {
        json_value *a = json_new_obj();
        gm_attr_json(attrs->items[i], a);
        json_arr_add(arr, a);
    }
}

/* ---- expression trees ---- */


static json_value *gm_expr_tree(zan_ast_node_t *n) {
    json_value *o = json_new_obj();
    if (!n) { json_obj_set(o, "k", json_new_str("null")); return o; }
    switch (n->kind) {
    case AST_INT_LITERAL:
        json_obj_set(o, "k", json_new_str("lit"));
        json_obj_set(o, "t", json_new_str("int"));
        json_obj_set(o, "v", json_new_num((double)n->int_val));
        break;
    case AST_FLOAT_LITERAL:
        json_obj_set(o, "k", json_new_str("lit"));
        json_obj_set(o, "t", json_new_str("double"));
        json_obj_set(o, "v", json_new_num(n->float_val));
        break;
    case AST_BOOL_LITERAL:
        json_obj_set(o, "k", json_new_str("lit"));
        json_obj_set(o, "t", json_new_str("bool"));
        json_obj_set(o, "v", json_new_bool(n->bool_val));
        break;
    case AST_CHAR_LITERAL:
        json_obj_set(o, "k", json_new_str("lit"));
        json_obj_set(o, "t", json_new_str("char"));
        json_obj_set(o, "v", json_new_num((double)n->int_val));
        break;
    case AST_STRING_LITERAL:
        json_obj_set(o, "k", json_new_str("lit"));
        json_obj_set(o, "t", json_new_str("str"));
        json_obj_set(o, "v", json_new_str(
            n->str_val.str ? (const char *)n->str_val.str : ""));
        break;
    case AST_NULL_LITERAL:
        json_obj_set(o, "k", json_new_str("null"));
        break;
    case AST_IDENTIFIER:
        json_obj_set(o, "k", json_new_str("id"));
        json_obj_set(o, "n", json_new_str(
            n->ident.name.str ? (const char *)n->ident.name.str : ""));
        break;
    case AST_THIS_EXPR:
        json_obj_set(o, "k", json_new_str("this"));
        break;
    case AST_BASE_EXPR:
        json_obj_set(o, "k", json_new_str("base"));
        break;
    case AST_BINARY:
        json_obj_set(o, "k", json_new_str("bin"));
        json_obj_set(o, "op", json_new_str(zan_token_kind_name(n->binary.op)));
        json_obj_set(o, "l", gm_expr_tree(n->binary.left));
        json_obj_set(o, "r", gm_expr_tree(n->binary.right));
        break;
    case AST_UNARY:
    case AST_POSTFIX_UNARY:
        json_obj_set(o, "k", json_new_str("uni"));
        json_obj_set(o, "op", json_new_str(zan_token_kind_name(n->unary.op)));
        json_obj_set(o, "o", gm_expr_tree(n->unary.operand));
        break;
    case AST_MEMBER_ACCESS: {
        json_obj_set(o, "k", json_new_str("mem"));
        json_obj_set(o, "o", gm_expr_tree(n->member.object));
        json_obj_set(o, "n", json_new_str(
            n->member.name.str ? (const char *)n->member.name.str : ""));
        break;
    }
    case AST_CALL: {
        json_obj_set(o, "k", json_new_str("call"));
        json_obj_set(o, "f", gm_expr_tree(n->call.callee));
        json_value *args = json_new_arr();
        for (int i = 0; i < n->call.args.count; i++)
            json_arr_add(args, gm_expr_tree(n->call.args.items[i]));
        json_obj_set(o, "a", args);
        break;
    }
    case AST_CONDITIONAL:
        json_obj_set(o, "k", json_new_str("cond"));
        json_obj_set(o, "c", gm_expr_tree(n->conditional.cond));
        json_obj_set(o, "t", gm_expr_tree(n->conditional.then_expr));
        json_obj_set(o, "e", gm_expr_tree(n->conditional.else_expr));
        break;
    case AST_CAST_EXPR:
        json_obj_set(o, "k", json_new_str("cast"));
        { char t[256];
          gm_type_str(n->cast.type, t, sizeof(t));
          json_obj_set(o, "t", json_new_str(t)); }
        json_obj_set(o, "e", gm_expr_tree(n->cast.expr));
        break;
    case AST_IS_EXPR:
    case AST_AS_EXPR:
        json_obj_set(o, "k", json_new_str("is"));
        json_obj_set(o, "e", gm_expr_tree(n->type_test.expr));
        { char t[256];
          gm_type_str(n->type_test.type, t, sizeof(t));
          json_obj_set(o, "t", json_new_str(t)); }
        break;
    case AST_INDEX:
        json_obj_set(o, "k", json_new_str("idx"));
        json_obj_set(o, "o", gm_expr_tree(n->index.object));
        json_obj_set(o, "i", gm_expr_tree(n->index.index));
        break;
    case AST_ASSIGNMENT:
        json_obj_set(o, "k", json_new_str("assign"));
        json_obj_set(o, "op", json_new_str(zan_token_kind_name(n->binary.op)));
        json_obj_set(o, "l", gm_expr_tree(n->binary.left));
        json_obj_set(o, "r", gm_expr_tree(n->binary.right));
        break;
    case AST_NEW_EXPR:
        json_obj_set(o, "k", json_new_str("new"));
        { char t[256];
          gm_type_str(n->new_expr.type, t, sizeof(t));
          json_obj_set(o, "t", json_new_str(t));
          json_value *args = json_new_arr();
          for (int i = 0; i < n->new_expr.args.count; i++)
              json_arr_add(args, gm_expr_tree(n->new_expr.args.items[i]));
          json_obj_set(o, "a", args); }
        break;
    case AST_BLOCK:
        /* a lambda with a block body: only the shape matters */
        json_obj_set(o, "k", json_new_str("block"));
        break;
    case AST_LAMBDA: {
        json_obj_set(o, "k", json_new_str("lam"));
        json_value *ps = json_new_arr();
        for (int i = 0; i < n->lambda.params.count; i++) {
            zan_ast_node_t *p = n->lambda.params.items[i];
            if (p->kind == AST_PARAM)
                json_arr_add(ps, json_new_str(
                    p->param.name.str ? (const char *)p->param.name.str : ""));
        }
        json_obj_set(o, "p", ps);
        json_obj_set(o, "b", gm_expr_tree(n->lambda.body));
        break;
    }
    case AST_AWAIT_EXPR:
        json_obj_set(o, "k", json_new_str("await"));
        json_obj_set(o, "o", gm_expr_tree(n->await_expr.expr));
        break;
    case AST_REF_ARG:
        json_obj_set(o, "k", json_new_str("ref"));
        json_obj_set(o, "o", gm_arg_json(n->ref_arg.expr));
        break;
    default:
        json_obj_set(o, "k", json_new_str("other"));
        break;
    }
    return o;
}

static json_value *gm_arg_json(zan_ast_node_t *n) {
    if (!n) { json_value *o = json_new_obj();
              json_obj_set(o, "k", json_new_str("null")); return o; }
    return gm_expr_tree(n);
}

/* ---- call-site recording ---- */

static void gm_recv_str(zan_ast_node_t *obj, char *out, size_t outsz) {
    if (!obj) { snprintf(out, outsz, "other"); return; }
    switch (obj->kind) {
    case AST_THIS_EXPR: snprintf(out, outsz, "this"); break;
    case AST_IDENTIFIER:
        snprintf(out, outsz, "id:%.*s", (int)obj->ident.name.len,
                 obj->ident.name.str);
        break;
    case AST_MEMBER_ACCESS: {
        char pre[128];
        gm_recv_str(obj->member.object, pre, sizeof(pre));
        snprintf(out, outsz, "mem:%s.%.*s", pre, (int)obj->member.name.len,
                 obj->member.name.str);
        break;
    }
    case AST_CALL: snprintf(out, outsz, "call"); break;
    case AST_BASE_EXPR: snprintf(out, outsz, "base"); break;
    default: snprintf(out, outsz, "other"); break;
    }
}

/* Reserve (or find) the placeholder id of a receiver call node. Only hit as
 * a fallback: the children-first walk records a receiver before the outer
 * call names it, so gm_record_call finds it in the rec table first. */
static int gm_recv_reserve(zan_ast_node_t *recv, gm_ctx_t *c) {
    for (int i = 0; i < c->pend_count; i++)
        if (c->pend_nodes[i] == recv) return c->pend_ids[i];
    if (c->pend_count >= c->pend_cap) {
        int ncap = c->pend_cap ? c->pend_cap * 2 : 64;
        zan_ast_node_t **nn = (zan_ast_node_t **)realloc(
            c->pend_nodes, (size_t)ncap * sizeof(*nn));
        if (nn) c->pend_nodes = nn;
        int *ni = nn ? (int *)realloc(c->pend_ids, (size_t)ncap * sizeof(*ni))
                     : NULL;
        if (!nn || !ni) return 0;
        c->pend_ids = ni;
        c->pend_cap = ncap;
    }
    c->pend_nodes[c->pend_count] = recv;
    c->pend_ids[c->pend_count] = ++c->call_id;
    return c->pend_ids[c->pend_count++];
}

/* Look up the id a call node was recorded with (children-first walk). */
static int gm_rec_lookup(zan_ast_node_t *call, gm_ctx_t *c) {
    for (int i = 0; i < c->rec_count; i++)
        if (c->rec_nodes[i] == call) return c->rec_ids[i];
    return 0;
}

static void gm_record_call(zan_ast_node_t *call, gm_ctx_t *c) {
    zan_ast_node_t *callee = call->call.callee;
    json_value *o = json_new_obj();
    /* children-first: a placeholder id may still exist if this node was
     * reserved by an outer call before it was walked (defensive path) */
    int id = 0;
    for (int i = 0; i < c->pend_count; i++) {
        if (c->pend_nodes[i] == call) {
            id = c->pend_ids[i];
            c->pend_nodes[i] = c->pend_nodes[c->pend_count - 1];
            c->pend_ids[i] = c->pend_ids[c->pend_count - 1];
            c->pend_count--;
            break;
        }
    }
    if (!id) id = ++c->call_id;
    if (c->rec_count >= c->rec_cap) {
        int ncap = c->rec_cap ? c->rec_cap * 2 : 256;
        zan_ast_node_t **nn =
            (zan_ast_node_t **)realloc(c->rec_nodes, (size_t)ncap * sizeof(*nn));
        int *ni = (int *)realloc(c->rec_ids, (size_t)ncap * sizeof(*ni));
        if (nn && ni) {
            c->rec_nodes = nn;
            c->rec_ids = ni;
            c->rec_cap = ncap;
        }
    }
    if (c->rec_count < c->rec_cap) {
        c->rec_nodes[c->rec_count] = call;
        c->rec_ids[c->rec_count] = id;
        c->rec_count++;
    }
    json_obj_set(o, "id", json_new_num((double)id));
    json_obj_set(o, "file", json_new_num((double)call->loc.file_id));
    json_obj_set(o, "line", json_new_num((double)call->loc.line));
    json_obj_set(o, "col", json_new_num((double)call->loc.col));
    json_obj_set(o, "name", json_new_str(
        callee->kind == AST_MEMBER_ACCESS
            ? (callee->member.name.str
                   ? (const char *)callee->member.name.str : "")
            : (callee->ident.name.str ? (const char *)callee->ident.name.str
                                      : "")));
    json_obj_set(o, "cls", json_new_str(c->cur_cls));
    json_obj_set(o, "fn", json_new_str(c->cur_fn));
    zan_ast_node_t *robj = (callee->kind == AST_MEMBER_ACCESS)
                               ? callee->member.object
                               : NULL;
    int recv_id = 0;
    char recv[160];
    if (robj && robj->kind == AST_CALL) {
        recv_id = gm_rec_lookup(robj, c);
        if (!recv_id) recv_id = gm_recv_reserve(robj, c);
        snprintf(recv, sizeof(recv), "call#%d", recv_id);
    } else if (robj) {
        gm_recv_str(robj, recv, sizeof(recv));
    } else {
        snprintf(recv, sizeof(recv), "other");
    }
    json_obj_set(o, "recv", json_new_str(recv));
    json_obj_set(o, "recv_id", json_new_num((double)recv_id));
    json_obj_set(o, "recvx", gm_expr_tree(robj));
    json_value *targs = json_new_arr();
    for (int i = 0; i < call->call.type_args.count; i++) {
        char t[256];
        gm_type_str(call->call.type_args.items[i], t, sizeof(t));
        json_arr_add(targs, json_new_str(t));
    }
    json_obj_set(o, "targs", targs);
    json_value *args = json_new_arr();
    for (int i = 0; i < call->call.args.count; i++)
        json_arr_add(args, gm_arg_json(call->call.args.items[i]));
    json_obj_set(o, "args", args);
    json_arr_add(c->calls, o);
}

static bool gm_name_in(const char *name, const char *const *names, int count) {
    if (!name) return false;
    for (int i = 0; i < count; i++)
        if (strcmp(name, names[i]) == 0) return true;
    return false;
}

/* Table entity names of the unit: `[Table] class Order` -- accessor members
 * named after one of them (`ctx.Order.Where(...)`) start a fluent DB chain. */
static const char **gm_table_entity_names(json_value *classes, int *out_count) {
    const char **names = NULL;
    int count = 0, cap = 0;
    *out_count = 0;
    if (!classes || classes->type != JSON_ARR) return NULL;
    for (int ci = 0; ci < classes->as.arr.count; ci++) {
        json_value *cls = classes->as.arr.items[ci];
        json_value *attrs = json_obj_get(cls, "attrs");
        if (!attrs || attrs->type != JSON_ARR) continue;
        bool table = false;
        for (int ai = 0; ai < attrs->as.arr.count; ai++) {
            const char *an =
                json_get_str(json_obj_get(attrs->as.arr.items[ai], "name"));
            if (an && strcmp(an, "Table") == 0) {
                table = true;
                break;
            }
        }
        if (!table) continue;
        const char *name = json_get_str(json_obj_get(cls, "name"));
        if (!name || !name[0] || gm_name_in(name, names, count)) continue;
        if (count >= cap) {
            int next = cap ? cap * 2 : 8;
            const char **grown =
                (const char **)realloc(names,
                                       sizeof(const char *) * (size_t)next);
            if (!grown) zan_host_oom();
            names = grown;
            cap = next;
        }
        names[count++] = name;
    }
    *out_count = count;
    return names;
}

static bool gm_is_codegen_seed(json_value *call, const char *const *expr_names,
                               int expr_count, const char *const *table_names,
                               int table_count) {
    static const char *const seeds[] = {
        /* route request readers */
        "In", "InText", "InRaw", "InInt", "InLong", "InDouble", "InBool",
        "HasIn", "Need", "NeedText", "NeedInt", "NeedLong", "Param", "Paged",
        /* database roots/accessors; fluent descendants are retained below */
        "Query", "Select", "Insert", "Update", "Delete", "SyncStructure",
        "SyncStructureAsync", "SyncStructureAll", "SyncStructureAllAsync",
        "Read", "ReadAsync"
    };
    const char *name = json_get_str(json_obj_get(call, "name"));
    const char *recv = json_get_str(json_obj_get(call, "recv"));
    if (recv && strcmp(recv, "id:Json") == 0 &&
        name && (strcmp(name, "Serialize") == 0 ||
                 strcmp(name, "Deserialize") == 0))
        return true;
    if (gm_name_in(name, seeds, (int)(sizeof(seeds) / sizeof(seeds[0]))))
        return true;
    if (gm_name_in(name, expr_names, expr_count)) return true;
    /* accessor chain head: the receiver is `<obj>.<Entity>`, so the call has
     * no receiving call site the walk above could reach it through. */
    {
        json_value *recvx = json_obj_get(call, "recvx");
        if (recvx && recvx->type == JSON_OBJ) {
            const char *k = json_get_str(json_obj_get(recvx, "k"));
            const char *n = json_get_str(json_obj_get(recvx, "n"));
            if (k && strcmp(k, "mem") == 0 &&
                gm_name_in(n, table_names, table_count))
                return true;
        }
    }
    return false;
}

static const char **gm_expr_method_names(json_value *classes, int *out_count) {
    const char **names = NULL;
    int count = 0, cap = 0;
    if (!classes || classes->type != JSON_ARR) {
        *out_count = 0;
        return NULL;
    }
    for (int ci = 0; ci < classes->as.arr.count; ci++) {
        json_value *methods =
            json_obj_get(classes->as.arr.items[ci], "methods");
        if (!methods || methods->type != JSON_ARR) continue;
        for (int mi = 0; mi < methods->as.arr.count; mi++) {
            json_value *method = methods->as.arr.items[mi];
            json_value *params = json_obj_get(method, "params");
            if (!params || params->type != JSON_ARR) continue;
            bool has_expr = false;
            for (int pi = 0; pi < params->as.arr.count; pi++) {
                const char *type =
                    json_get_str(json_obj_get(params->as.arr.items[pi],
                                              "type"));
                size_t n = type ? strlen(type) : 0;
                if (n > 6 && strncmp(type, "Expr<", 5) == 0 &&
                    type[n - 1] == '>') {
                    has_expr = true;
                    break;
                }
            }
            if (!has_expr) continue;
            const char *name = json_get_str(json_obj_get(method, "name"));
            if (!name || gm_name_in(name, names, count)) continue;
            if (count >= cap) {
                int next = cap ? cap * 2 : 8;
                const char **grown =
                    (const char **)realloc(names,
                                          sizeof(const char *) * (size_t)next);
                if (!grown) zan_host_oom();
                names = grown;
                cap = next;
            }
            names[count++] = name;
        }
    }
    *out_count = count;
    return names;
}

static void gm_prune_calls(json_value *calls, json_value *classes) {
    if (!calls || calls->type != JSON_ARR || calls->as.arr.count == 0) return;
    int max_id = 0;
    for (int i = 0; i < calls->as.arr.count; i++) {
        int id = (int)json_get_num(json_obj_get(calls->as.arr.items[i], "id"),
                                   0);
        if (id > max_id) max_id = id;
    }
    bool *keep = (bool *)calloc((size_t)max_id + 1, sizeof(bool));
    if (!keep) zan_host_oom();
    int expr_count = 0;
    const char **expr_names = gm_expr_method_names(classes, &expr_count);
    int table_count = 0;
    const char **table_names = gm_table_entity_names(classes, &table_count);
    for (int i = 0; i < calls->as.arr.count; i++) {
        json_value *call = calls->as.arr.items[i];
        int id = (int)json_get_num(json_obj_get(call, "id"), 0);
        if (id > 0 && gm_is_codegen_seed(call, expr_names, expr_count,
                                         table_names, table_count))
            keep[id] = true;
    }

    /* DB metadata follows fluent receivers in both directions: ancestors
     * locate the root and descendants carry every supported chain method. */
    bool changed;
    do {
        changed = false;
        for (int i = 0; i < calls->as.arr.count; i++) {
            json_value *call = calls->as.arr.items[i];
            int id =
                (int)json_get_num(json_obj_get(call, "id"), 0);
            int recv_id =
                (int)json_get_num(json_obj_get(call, "recv_id"), 0);
            if (id <= 0 || id > max_id || recv_id <= 0 ||
                recv_id > max_id)
                continue;
            if (keep[id] && !keep[recv_id]) {
                keep[recv_id] = true;
                changed = true;
            }
            if (keep[recv_id] && !keep[id]) {
                keep[id] = true;
                changed = true;
            }
        }
    } while (changed);

    int out = 0;
    for (int i = 0; i < calls->as.arr.count; i++) {
        json_value *call = calls->as.arr.items[i];
        int id = (int)json_get_num(json_obj_get(call, "id"), 0);
        if (id > 0 && id <= max_id && keep[id])
            calls->as.arr.items[out++] = call;
        else
            json_free(call);
    }
    calls->as.arr.count = out;
    free(expr_names);
    free(table_names);
    free(keep);
}

/* ---- expression walk (finds call sites) ---- */

static void gm_walk_expr(zan_ast_node_t *n, gm_ctx_t *c) {
    if (!n) return;
    switch (n->kind) {
    case AST_CALL:
        /* children first, exactly like the old C dbgen's dg_visit_call: a chain
         * root (`Query<T>`) is recorded before the chain methods above it,
         * so their `recv` can name it as call#<id> */
        gm_walk_expr(n->call.callee, c);
        for (int i = 0; i < n->call.args.count; i++)
            gm_walk_expr(n->call.args.items[i], c);
        for (int i = 0; i < n->call.type_args.count; i++)
            gm_walk_expr(n->call.type_args.items[i], c);
        if (n->call.callee &&
            (n->call.callee->kind == AST_MEMBER_ACCESS ||
             n->call.callee->kind == AST_IDENTIFIER))
            gm_record_call(n, c);
        break;
    case AST_BINARY:
    case AST_ASSIGNMENT:
        gm_walk_expr(n->binary.left, c);
        gm_walk_expr(n->binary.right, c);
        break;
    case AST_UNARY:
    case AST_POSTFIX_UNARY:
        gm_walk_expr(n->unary.operand, c);
        break;
    case AST_MEMBER_ACCESS:
        gm_walk_expr(n->member.object, c);
        break;
    case AST_INDEX:
        gm_walk_expr(n->index.object, c);
        gm_walk_expr(n->index.index, c);
        break;
    case AST_CONDITIONAL:
        gm_walk_expr(n->conditional.cond, c);
        gm_walk_expr(n->conditional.then_expr, c);
        gm_walk_expr(n->conditional.else_expr, c);
        break;
    case AST_NEW_EXPR:
        for (int i = 0; i < n->new_expr.args.count; i++)
            gm_walk_expr(n->new_expr.args.items[i], c);
        for (int i = 0; i < n->new_expr.arg_inits.count; i++)
            gm_walk_expr(n->new_expr.arg_inits.items[i], c);
        break;
    case AST_COLL_INIT:
        for (int i = 0; i < n->coll_init.items.count; i++)
            gm_walk_expr(n->coll_init.items.items[i], c);
        break;
    case AST_CAST_EXPR:
        gm_walk_expr(n->cast.expr, c);
        break;
    case AST_IS_EXPR:
    case AST_AS_EXPR:
        gm_walk_expr(n->type_test.expr, c);
        break;
    case AST_LAMBDA:
        gm_walk_expr(n->lambda.body, c);
        break;
    case AST_AWAIT_EXPR:
        gm_walk_expr(n->await_expr.expr, c);
        break;
    case AST_REF_ARG:
        gm_walk_expr(n->ref_arg.expr, c);
        break;
    case AST_STRING_INTERP:
        for (int i = 0; i < n->string_interp.parts.count; i++)
            gm_walk_expr(n->string_interp.parts.items[i], c);
        break;
    default:
        break;
    }
}

static void gm_walk_stmt(zan_ast_node_t *n, gm_ctx_t *c) {
    if (!n) return;
    switch (n->kind) {
    case AST_BLOCK:
        for (int i = 0; i < n->block.stmts.count; i++)
            gm_walk_stmt(n->block.stmts.items[i], c);
        break;
    case AST_VAR_DECL:
        gm_walk_expr(n->var_decl.initializer, c);
        break;
    case AST_EXPR_STMT:
        gm_walk_expr(n->expr_stmt.expr, c);
        break;
    case AST_RETURN_STMT:
        gm_walk_expr(n->ret.value, c);
        break;
    case AST_IF_STMT:
        gm_walk_expr(n->if_stmt.cond, c);
        gm_walk_stmt(n->if_stmt.then_body, c);
        gm_walk_stmt(n->if_stmt.else_body, c);
        break;
    case AST_WHILE_STMT:
    case AST_DO_WHILE_STMT:
        gm_walk_expr(n->while_stmt.cond, c);
        gm_walk_stmt(n->while_stmt.body, c);
        break;
    case AST_FOR_STMT:
        gm_walk_stmt(n->for_stmt.init, c);
        gm_walk_expr(n->for_stmt.cond, c);
        gm_walk_expr(n->for_stmt.step, c);
        gm_walk_stmt(n->for_stmt.body, c);
        break;
    case AST_FOREACH_STMT:
        gm_walk_expr(n->foreach_stmt.collection, c);
        gm_walk_stmt(n->foreach_stmt.body, c);
        break;
    case AST_THROW_STMT:
        gm_walk_expr(n->throw_stmt.value, c);
        break;
    case AST_TRY_STMT:
        gm_walk_stmt(n->try_stmt.try_body, c);
        for (int i = 0; i < n->try_stmt.catches.count; i++)
            gm_walk_stmt(n->try_stmt.catches.items[i], c);
        gm_walk_stmt(n->try_stmt.finally_body, c);
        break;
    case AST_CATCH_CLAUSE:
        gm_walk_stmt(n->catch_clause.body, c);
        break;
    case AST_SWITCH_STMT:
        gm_walk_expr(n->switch_stmt.expr, c);
        for (int i = 0; i < n->switch_stmt.cases.count; i++)
            gm_walk_stmt(n->switch_stmt.cases.items[i], c);
        break;
    case AST_SWITCH_CASE:
        gm_walk_expr(n->switch_case.pattern, c);
        gm_walk_stmt(n->switch_case.body, c);
        break;
    case AST_LOCK_STMT:
        gm_walk_expr(n->lock_stmt.expr, c);
        gm_walk_stmt(n->lock_stmt.body, c);
        break;
    case AST_YIELD_STMT:
        gm_walk_expr(n->yield_stmt.value, c);
        break;
    case AST_LABEL_STMT:
    case AST_GOTO_STMT:
        /* plain name nodes, nothing to walk */
        break;
    case AST_QUERY_EXPR:
        gm_walk_expr(n->query.source, c);
        for (int i = 0; i < n->query.clauses.count; i++) {
            zan_ast_node_t *cl = n->query.clauses.items[i];
            gm_walk_expr(cl->query_clause.expr, c);
            if (cl->kind == AST_QUERY_JOIN) {
                gm_walk_expr(cl->query_clause.source, c);
                gm_walk_expr(cl->query_clause.left_key, c);
                gm_walk_expr(cl->query_clause.right_key, c);
            }
        }
        gm_walk_expr(n->query.group_expr, c);
        gm_walk_expr(n->query.group_key, c);
        gm_walk_expr(n->query.select, c);
        break;
    default:
        break;
    }
}

/* ---- top-level type shapes ---- */

static void gm_export_type(zan_ast_node_t *decl, json_value *classes) {
    json_value *o = json_new_obj();
    switch (decl->kind) {
    case AST_CLASS_DECL:
    case AST_STRUCT_DECL:
    case AST_INTERFACE_DECL: {
        json_obj_set(o, "name", json_new_str(
            decl->type_decl.name.str ? (const char *)decl->type_decl.name.str
                                     : ""));
        json_obj_set(o, "ns", json_new_str(
            decl->ns_name.str ? (const char *)decl->ns_name.str : ""));
        json_obj_set(o, "kind", json_new_str(
            decl->kind == AST_CLASS_DECL ? "class" :
            decl->kind == AST_STRUCT_DECL ? "struct" : "interface"));
        if (decl->orig_name.len)
            json_obj_set(o, "orig", json_new_str(
                decl->orig_name.str ? (const char *)decl->orig_name.str : ""));
        json_obj_set(o, "file", json_new_num((double)decl->loc.file_id));
        json_obj_set(o, "line", json_new_num((double)decl->loc.line));
        json_value *bases = json_new_arr();
        for (int i = 0; i < decl->type_decl.bases.count; i++) {
            char b[256];
            gm_type_str(decl->type_decl.bases.items[i], b, sizeof(b));
            json_arr_add(bases, json_new_str(b));
        }
        json_obj_set(o, "bases", bases);
        json_value *attrs = json_new_arr();
        gm_attrs_json(&decl->attributes, attrs);
        json_obj_set(o, "attrs", attrs);
        json_value *fields = json_new_arr();
        json_value *methods = json_new_arr();
        json_value *props = json_new_arr();
        json_value *ctors = json_new_arr();
        for (int i = 0; i < decl->type_decl.members.count; i++) {
            zan_ast_node_t *m = decl->type_decl.members.items[i];
            if (m->kind == AST_FIELD_DECL) {
                json_value *f = json_new_obj();
                json_obj_set(f, "name", json_new_str(
                    m->field_decl.name.str ? (const char *)m->field_decl.name.str
                                           : ""));
                json_obj_set(f, "static", json_new_bool(
                    (m->field_decl.modifiers & MOD_STATIC) != 0));
                json_obj_set(f, "file", json_new_num((double)m->loc.file_id));
                json_obj_set(f, "line", json_new_num((double)m->loc.line));
                json_obj_set(f, "col", json_new_num((double)m->loc.col));
                char t[256];
                gm_type_str(m->field_decl.type, t, sizeof(t));
                json_obj_set(f, "type", json_new_str(t));
                json_value *fa = json_new_arr();
                gm_attrs_json(&m->attributes, fa);
                json_obj_set(f, "attrs", fa);
                json_arr_add(fields, f);
            } else if (m->kind == AST_METHOD_DECL ||
                       m->kind == AST_CONSTRUCTOR_DECL) {
                json_value *mm = json_new_obj();
                json_obj_set(mm, "name", json_new_str(
                    m->method_decl.name.str ? (const char *)m->method_decl.name.str
                                            : ""));
                json_obj_set(mm, "ctor", json_new_bool(
                    m->kind == AST_CONSTRUCTOR_DECL));
                json_obj_set(mm, "static", json_new_bool(
                    (m->method_decl.modifiers & MOD_STATIC) != 0));
                json_obj_set(mm, "async", json_new_bool(
                    (m->method_decl.modifiers & MOD_ASYNC) != 0));
                json_obj_set(mm, "private", json_new_bool(
                    (m->method_decl.modifiers & MOD_PRIVATE) != 0));
                json_obj_set(mm, "protected", json_new_bool(
                    (m->method_decl.modifiers & MOD_PROTECTED) != 0));
                json_obj_set(mm, "file", json_new_num((double)m->loc.file_id));
                json_obj_set(mm, "line", json_new_num((double)m->loc.line));
                json_value *ps = json_new_arr();
                for (int j = 0; j < m->method_decl.params.count; j++) {
                    zan_ast_node_t *p = m->method_decl.params.items[j];
                    if (p->kind != AST_PARAM) continue;
                    json_value *pv = json_new_obj();
                    json_obj_set(pv, "name", json_new_str(
                        p->param.name.str ? (const char *)p->param.name.str
                                          : ""));
                    char pt[256];
                    gm_type_str(p->param.type, pt, sizeof(pt));
                    json_obj_set(pv, "type", json_new_str(pt));
                    if (p->param.default_val)
                        json_obj_set(pv, "def",
                                     gm_expr_tree(p->param.default_val));
                    json_arr_add(ps, pv);
                }
                json_obj_set(mm, "params", ps);
                json_value *ma = json_new_arr();
                gm_attrs_json(&m->attributes, ma);
                json_obj_set(mm, "attrs", ma);
                json_arr_add(methods, mm);
                /* constructor: the `Prop = param;` assignments that map ctor
                 * parameters onto property names (compile-time attr classes) */
                if (m->kind == AST_CONSTRUCTOR_DECL) {
                    json_value *co = json_new_obj();
                    /* params cannot be shared with the methods entry: the
                     * JSON tree is freed recursively, so each child has one
                     * owner */
                    json_value *cps = json_new_arr();
                    for (int j = 0; j < m->method_decl.params.count; j++) {
                        zan_ast_node_t *p = m->method_decl.params.items[j];
                        if (p->kind != AST_PARAM) continue;
                        json_value *pv = json_new_obj();
                        json_obj_set(pv, "name", json_new_str(
                            p->param.name.str ? (const char *)p->param.name.str
                                              : ""));
                        char pt[256];
                        gm_type_str(p->param.type, pt, sizeof(pt));
                        json_obj_set(pv, "type", json_new_str(pt));
                        if (p->param.default_val)
                            json_obj_set(pv, "def",
                                         gm_expr_tree(p->param.default_val));
                        json_arr_add(cps, pv);
                    }
                    json_obj_set(co, "params", cps);
                    json_value *as = json_new_arr();
                    zan_ast_node_t *body = m->method_decl.body;
                    if (body && body->kind == AST_BLOCK) {
                        for (int k = 0; k < body->block.stmts.count; k++) {
                            zan_ast_node_t *st = body->block.stmts.items[k];
                            if (!st || st->kind != AST_EXPR_STMT) continue;
                            zan_ast_node_t *ex = st->expr_stmt.expr;
                            if (!ex || ex->kind != AST_ASSIGNMENT) continue;
                            zan_ast_node_t *l = ex->binary.left;
                            zan_ast_node_t *rt = ex->binary.right;
                            const char *propn = NULL, *paramn = NULL;
                            char lb[64], rb[64];
                            if (l && l->kind == AST_IDENTIFIER) {
                                gm_istr_to(lb, sizeof(lb), l->ident.name);
                                propn = lb;
                            } else if (l && l->kind == AST_MEMBER_ACCESS &&
                                       l->member.object &&
                                       (l->member.object->kind == AST_THIS_EXPR ||
                                        (l->member.object->kind == AST_IDENTIFIER &&
                                         gm_istr_is(l->member.object->ident.name,
                                                 "this")))) {
                                gm_istr_to(lb, sizeof(lb), l->member.name);
                                propn = lb;
                            }
                            if (rt && rt->kind == AST_IDENTIFIER) {
                                gm_istr_to(rb, sizeof(rb), rt->ident.name);
                                paramn = rb;
                            }
                            if (propn && paramn) {
                                json_value *a = json_new_obj();
                                json_obj_set(a, "prop", json_new_str(propn));
                                json_obj_set(a, "param", json_new_str(paramn));
                                json_arr_add(as, a);
                            }
                        }
                    }
                    json_obj_set(co, "assigns", as);
                    json_arr_add(ctors, co);
                }
            } else if ((m->kind == AST_PROPERTY_DECL) &&
                       m->field_decl.initializer) {
                /* property initializer defaults (`T X { get; set; } = v;`) */
                json_value *p = json_new_obj();
                json_obj_set(p, "name", json_new_str(
                    m->field_decl.name.str ? (const char *)m->field_decl.name.str
                                           : ""));
                json_obj_set(p, "init",
                             gm_expr_tree(m->field_decl.initializer));
                json_arr_add(props, p);
            }
        }
        json_obj_set(o, "fields", fields);
        json_obj_set(o, "methods", methods);
        json_obj_set(o, "props", props);
        json_obj_set(o, "ctors", ctors);
        break;
    }
    case AST_ENUM_DECL: {
        json_obj_set(o, "name", json_new_str(
            decl->type_decl.name.str ? (const char *)decl->type_decl.name.str
                                     : ""));
        json_obj_set(o, "ns", json_new_str(
            decl->ns_name.str ? (const char *)decl->ns_name.str : ""));
        json_obj_set(o, "kind", json_new_str("enum"));
        json_value *members = json_new_arr();
        for (int i = 0; i < decl->type_decl.members.count; i++) {
            zan_ast_node_t *m = decl->type_decl.members.items[i];
            if (m->kind != AST_ENUM_MEMBER) continue;
            json_value *em = json_new_obj();
            json_obj_set(em, "name", json_new_str(
                m->enum_member.name.str ? (const char *)m->enum_member.name.str
                                        : ""));
            if (m->enum_member.value && m->enum_member.value->kind == AST_INT_LITERAL)
                json_obj_set(em, "value", json_new_num(
                    (double)m->enum_member.value->int_val));
            json_arr_add(members, em);
        }
        json_obj_set(o, "members", members);
        break;
    }
    default:
        json_free(o);
        return;
    }
    json_arr_add(classes, o);
}

/* ---- call-site finder (for rewrite application) ---- */

/* Walk with the exact same order as the export and stop at the `id`-th
 * member call (ids start at 1, matching gm_record_call). */
typedef struct {
    int want;
    int seen;
    zan_ast_node_t *found;
    /* collect mode: store the first `cap` call-site nodes in `nodes`
     * (index 0 == call site 1). Rewrites mutate node contents but never
     * replace nodes, so the pointers stay valid for the whole pass. */
    zan_ast_node_t **nodes;
    int cap;
} gm_find_ctx_t;

static void gm_find_expr(zan_ast_node_t *n, gm_find_ctx_t *f);
static void gm_find_stmt(zan_ast_node_t *n, gm_find_ctx_t *f);

static void gm_find_expr(zan_ast_node_t *n, gm_find_ctx_t *f) {
    if (!n || f->found) return;
    switch (n->kind) {
    case AST_CALL:
        gm_find_expr(n->call.callee, f);
        for (int i = 0; i < n->call.args.count; i++)
            gm_find_expr(n->call.args.items[i], f);
        for (int i = 0; i < n->call.type_args.count; i++)
            gm_find_expr(n->call.type_args.items[i], f);
        if (n->call.callee &&
            (n->call.callee->kind == AST_MEMBER_ACCESS ||
             n->call.callee->kind == AST_IDENTIFIER)) {
            f->seen++;
            if (f->nodes && f->seen <= f->cap) f->nodes[f->seen - 1] = n;
            if (f->seen == f->want) { f->found = n; return; }
        }
        break;
    case AST_BINARY:
    case AST_ASSIGNMENT:
        gm_find_expr(n->binary.left, f);
        gm_find_expr(n->binary.right, f);
        break;
    case AST_UNARY:
    case AST_POSTFIX_UNARY:
        gm_find_expr(n->unary.operand, f);
        break;
    case AST_MEMBER_ACCESS:
        gm_find_expr(n->member.object, f);
        break;
    case AST_INDEX:
        gm_find_expr(n->index.object, f);
        gm_find_expr(n->index.index, f);
        break;
    case AST_CONDITIONAL:
        gm_find_expr(n->conditional.cond, f);
        gm_find_expr(n->conditional.then_expr, f);
        gm_find_expr(n->conditional.else_expr, f);
        break;
    case AST_NEW_EXPR:
        for (int i = 0; i < n->new_expr.args.count; i++)
            gm_find_expr(n->new_expr.args.items[i], f);
        for (int i = 0; i < n->new_expr.arg_inits.count; i++)
            gm_find_expr(n->new_expr.arg_inits.items[i], f);
        break;
    case AST_COLL_INIT:
        for (int i = 0; i < n->coll_init.items.count; i++)
            gm_find_expr(n->coll_init.items.items[i], f);
        break;
    case AST_CAST_EXPR:
        gm_find_expr(n->cast.expr, f);
        break;
    case AST_IS_EXPR:
    case AST_AS_EXPR:
        gm_find_expr(n->type_test.expr, f);
        break;
    case AST_LAMBDA:
        gm_find_expr(n->lambda.body, f);
        break;
    case AST_AWAIT_EXPR:
        gm_find_expr(n->await_expr.expr, f);
        break;
    case AST_REF_ARG:
        gm_find_expr(n->ref_arg.expr, f);
        break;
    case AST_STRING_INTERP:
        for (int i = 0; i < n->string_interp.parts.count; i++)
            gm_find_expr(n->string_interp.parts.items[i], f);
        break;
    default:
        break;
    }
}

static void gm_find_stmt(zan_ast_node_t *n, gm_find_ctx_t *f) {
    if (!n || f->found) return;
    switch (n->kind) {
    case AST_BLOCK:
        for (int i = 0; i < n->block.stmts.count; i++)
            gm_find_stmt(n->block.stmts.items[i], f);
        break;
    case AST_VAR_DECL:
        gm_find_expr(n->var_decl.initializer, f);
        break;
    case AST_EXPR_STMT:
        gm_find_expr(n->expr_stmt.expr, f);
        break;
    case AST_RETURN_STMT:
        gm_find_expr(n->ret.value, f);
        break;
    case AST_IF_STMT:
        gm_find_expr(n->if_stmt.cond, f);
        gm_find_stmt(n->if_stmt.then_body, f);
        gm_find_stmt(n->if_stmt.else_body, f);
        break;
    case AST_WHILE_STMT:
    case AST_DO_WHILE_STMT:
        gm_find_expr(n->while_stmt.cond, f);
        gm_find_stmt(n->while_stmt.body, f);
        break;
    case AST_FOR_STMT:
        gm_find_stmt(n->for_stmt.init, f);
        gm_find_expr(n->for_stmt.cond, f);
        gm_find_expr(n->for_stmt.step, f);
        gm_find_stmt(n->for_stmt.body, f);
        break;
    case AST_FOREACH_STMT:
        gm_find_expr(n->foreach_stmt.collection, f);
        gm_find_stmt(n->foreach_stmt.body, f);
        break;
    case AST_THROW_STMT:
        gm_find_expr(n->throw_stmt.value, f);
        break;
    case AST_TRY_STMT:
        gm_find_stmt(n->try_stmt.try_body, f);
        for (int i = 0; i < n->try_stmt.catches.count; i++)
            gm_find_stmt(n->try_stmt.catches.items[i], f);
        gm_find_stmt(n->try_stmt.finally_body, f);
        break;
    case AST_CATCH_CLAUSE:
        gm_find_stmt(n->catch_clause.body, f);
        break;
    case AST_SWITCH_STMT:
        gm_find_expr(n->switch_stmt.expr, f);
        for (int i = 0; i < n->switch_stmt.cases.count; i++)
            gm_find_stmt(n->switch_stmt.cases.items[i], f);
        break;
    case AST_SWITCH_CASE:
        gm_find_expr(n->switch_case.pattern, f);
        gm_find_stmt(n->switch_case.body, f);
        break;
    case AST_LOCK_STMT:
        gm_find_expr(n->lock_stmt.expr, f);
        gm_find_stmt(n->lock_stmt.body, f);
        break;
    case AST_YIELD_STMT:
        gm_find_expr(n->yield_stmt.value, f);
        break;
    case AST_LABEL_STMT:
    case AST_GOTO_STMT:
        break;
    case AST_QUERY_EXPR:
        gm_find_expr(n->query.source, f);
        for (int i = 0; i < n->query.clauses.count; i++) {
            zan_ast_node_t *cl = n->query.clauses.items[i];
            gm_find_expr(cl->query_clause.expr, f);
            if (cl->kind == AST_QUERY_JOIN) {
                gm_find_expr(cl->query_clause.source, f);
                gm_find_expr(cl->query_clause.left_key, f);
                gm_find_expr(cl->query_clause.right_key, f);
            }
        }
        gm_find_expr(n->query.group_expr, f);
        gm_find_expr(n->query.group_key, f);
        gm_find_expr(n->query.select, f);
        break;
    default:
        break;
    }
}

zan_ast_node_t *zan_genmeta_find_call(zan_ast_node_t *unit, int id) {
    gm_find_ctx_t f;
    f.want = id;
    f.seen = 0;
    f.found = NULL;
    f.nodes = NULL;
    f.cap = 0;
    for (int i = 0; i < unit->comp_unit.decls.count && !f.found; i++) {
        zan_ast_node_t *decl = unit->comp_unit.decls.items[i];
        if (decl->kind != AST_CLASS_DECL && decl->kind != AST_STRUCT_DECL)
            continue;
        for (int j = 0; j < decl->type_decl.members.count && !f.found; j++) {
            zan_ast_node_t *m = decl->type_decl.members.items[j];
            if (m->kind == AST_METHOD_DECL || m->kind == AST_CONSTRUCTOR_DECL)
                gm_find_stmt(m->method_decl.body, &f);
            else if (m->kind == AST_FIELD_DECL || m->kind == AST_PROPERTY_DECL)
                gm_find_expr(m->field_decl.initializer, &f);
        }
    }
    return f.found;
}

/* One pass over the unit, storing the call-site node of id k (1-based, in the
 * export's traversal order) at nodes[k-1], for k <= cap. Returns the total
 * number of call sites, so the caller can size the array with a first call
 * (nodes = NULL). Unlike zan_genmeta_find_call this is unaffected by earlier
 * rewrites: apply_rewrites takes its snapshot before touching anything, and
 * rewrites only mutate node contents, never replace the nodes themselves. */
int zan_genmeta_index_calls(zan_ast_node_t *unit, zan_ast_node_t **nodes,
                            int cap) {
    gm_find_ctx_t f;
    f.want = 0;
    f.seen = 0;
    f.found = NULL;
    f.nodes = nodes;
    f.cap = cap;
    for (int i = 0; i < unit->comp_unit.decls.count; i++) {
        zan_ast_node_t *decl = unit->comp_unit.decls.items[i];
        if (decl->kind != AST_CLASS_DECL && decl->kind != AST_STRUCT_DECL)
            continue;
        for (int j = 0; j < decl->type_decl.members.count; j++) {
            zan_ast_node_t *m = decl->type_decl.members.items[j];
            if (m->kind == AST_METHOD_DECL || m->kind == AST_CONSTRUCTOR_DECL)
                gm_find_stmt(m->method_decl.body, &f);
            else if (m->kind == AST_FIELD_DECL || m->kind == AST_PROPERTY_DECL)
                gm_find_expr(m->field_decl.initializer, &f);
        }
    }
    return f.seen;
}

static char *gm_export(zan_ast_node_t *unit, zan_diag_t *diag) {
    if (!unit) return NULL;
    json_value *root = json_new_obj();
    json_obj_set(root, "version", json_new_num(1));

    if (diag) {
        json_value *files = json_new_arr();
        for (int i = 0; i < diag->file_count; i++) {
            const char *name = diag->file_names ? diag->file_names[i] : NULL;
            json_arr_add(files, json_new_str(name ? name : ""));
        }
        json_obj_set(root, "files", files);
    }

    json_value *classes = json_new_arr();
    for (int i = 0; i < unit->comp_unit.decls.count; i++)
        gm_export_type(unit->comp_unit.decls.items[i], classes);
    json_obj_set(root, "classes", classes);

    gm_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.calls = json_new_arr();
    ctx.call_id = 0;
    ctx.pend_nodes = NULL;
    ctx.pend_ids = NULL;
    ctx.pend_count = 0;
    ctx.pend_cap = 0;
    /* walk every method body and field initializer for call sites */
    for (int i = 0; i < unit->comp_unit.decls.count; i++) {
        zan_ast_node_t *decl = unit->comp_unit.decls.items[i];
        if (decl->kind != AST_CLASS_DECL && decl->kind != AST_STRUCT_DECL)
            continue;
        gm_istr_to(ctx.cur_cls, sizeof(ctx.cur_cls), decl->type_decl.name);
        for (int j = 0; j < decl->type_decl.members.count; j++) {
            zan_ast_node_t *m = decl->type_decl.members.items[j];
            if (m->kind == AST_METHOD_DECL || m->kind == AST_CONSTRUCTOR_DECL) {
                gm_istr_to(ctx.cur_fn, sizeof(ctx.cur_fn), m->method_decl.name);
                gm_walk_stmt(m->method_decl.body, &ctx);
            } else if (m->kind == AST_FIELD_DECL ||
                       m->kind == AST_PROPERTY_DECL) {
                ctx.cur_fn[0] = 0;
                gm_walk_expr(m->field_decl.initializer, &ctx);
            }
        }
    }
    gm_prune_calls(ctx.calls, classes);
    json_obj_set(root, "calls", ctx.calls);

    free(ctx.pend_nodes);
    free(ctx.pend_ids);
    free(ctx.rec_nodes);
    free(ctx.rec_ids);

    char *out = json_serialize(root);
    {
        const char *gm_dump = getenv("ZAN_GENMETA_DUMP");
        if (gm_dump && gm_dump[0]) {
            FILE *df = fopen(gm_dump, "wb");
            if (df) {
                fwrite(out, 1, strlen(out), df);
                fclose(df);
            }
        }
    }
    fflush(stderr);
    json_free(root);
    return out;
}

char *zan_genmeta_export(zan_ast_node_t *unit) {
    return gm_export(unit, NULL);
}

char *zan_genmeta_export_files(zan_ast_node_t *unit, zan_diag_t *diag) {
    return gm_export(unit, diag);
}

/* ---- expression-tree deserializer (gm_expr_tree's inverse) ----
 *
 * Rebuilds AST expression nodes from the JSON trees the generators embed in
 * rewrite directives (db_chain args, db_acc_root/db_expr_arg whole trees).
 * No binder metadata is needed: generation runs pre-binding and every node is
 * identified by shape alone. An `id` node may carry a "targs" array to build
 * a generic identifier (`Expr<User>.From`) the same way the parser does
 * (`Box<int>.Create(x)` -> inst_type_ref). */

static zan_token_kind_t gm_token_kind(const char *name) {
    if (!name) return TK_INVALID;
    for (int k = 0; k < TK__COUNT; k++)
        if (strcmp(zan_token_kind_name((zan_token_kind_t)k), name) == 0)
            return (zan_token_kind_t)k;
    return TK_INVALID;
}

static zan_ast_node_t *gm_type_ref_from(const char *name, zan_arena_t *arena,
                                        zan_loc_t loc) {
    zan_ast_node_t *t = zan_ast_new(arena, AST_TYPE_REF, loc);
    t->type_ref.name.str = name ? zan_arena_strdup(arena, name, strlen(name))
                                : NULL;
    t->type_ref.name.len = name ? (uint32_t)strlen(name) : 0;
    zan_ast_list_init(&t->type_ref.type_args);
    t->type_ref.is_array = false;
    t->type_ref.is_nullable = false;
    return t;
}

zan_ast_node_t *zan_genmeta_expr_from_json(json_value *j, zan_arena_t *arena) {
    if (!j || j->type != JSON_OBJ) return NULL;
    const char *k = json_get_str(json_obj_get(j, "k"));
    if (!k) return NULL;
    zan_loc_t loc = zan_loc(0, 0, 0, 0);
    json_value *f = NULL;
    zan_ast_node_t *n = NULL;

    if (strcmp(k, "null") == 0)
        return zan_ast_new(arena, AST_NULL_LITERAL, loc);

    if (strcmp(k, "lit") == 0) {
        const char *t = json_get_str(json_obj_get(j, "t"));
        json_value *v = json_obj_get(j, "v");
        if (t && strcmp(t, "int") == 0) {
            n = zan_ast_new(arena, AST_INT_LITERAL, loc);
            n->int_val = (int64_t)json_get_num(v, 0);
        } else if (t && strcmp(t, "double") == 0) {
            n = zan_ast_new(arena, AST_FLOAT_LITERAL, loc);
            n->float_val = json_get_num(v, 0);
        } else if (t && strcmp(t, "bool") == 0) {
            n = zan_ast_new(arena, AST_BOOL_LITERAL, loc);
            n->bool_val = json_get_bool(v, false);
        } else if (t && strcmp(t, "char") == 0) {
            n = zan_ast_new(arena, AST_CHAR_LITERAL, loc);
            n->int_val = (int64_t)json_get_num(v, 0);
        } else if (t && strcmp(t, "str") == 0) {
            const char *s = json_get_str(v);
            n = zan_ast_new(arena, AST_STRING_LITERAL, loc);
            n->str_val.str = s ? zan_arena_strdup(arena, s, strlen(s)) : NULL;
            n->str_val.len = s ? (uint32_t)strlen(s) : 0;
        }
        return n;
    }

    if (strcmp(k, "id") == 0) {
        const char *nm = json_get_str(json_obj_get(j, "n"));
        n = zan_ast_new(arena, AST_IDENTIFIER, loc);
        n->ident.name.str = nm ? zan_arena_strdup(arena, nm, strlen(nm)) : NULL;
        n->ident.name.len = nm ? (uint32_t)strlen(nm) : 0;
        json_value *ta = json_obj_get(j, "targs");
        if (ta && ta->type == JSON_ARR && ta->as.arr.count > 0) {
            zan_ast_node_t *tr = gm_type_ref_from(nm, arena, loc);
            for (int i = 0; i < ta->as.arr.count; i++) {
                const char *targ = json_get_str(ta->as.arr.items[i]);
                if (!targ) continue;
                zan_ast_list_push(&tr->type_ref.type_args,
                                  gm_type_ref_from(targ, arena, loc), arena);
            }
            n->inst_type_ref = tr;
        }
        return n;
    }

    if (strcmp(k, "this") == 0) return zan_ast_new(arena, AST_THIS_EXPR, loc);
    if (strcmp(k, "base") == 0) return zan_ast_new(arena, AST_BASE_EXPR, loc);

    if (strcmp(k, "bin") == 0) {
        n = zan_ast_new(arena, AST_BINARY, loc);
        n->binary.op = gm_token_kind(json_get_str(json_obj_get(j, "op")));
        n->binary.left =
            zan_genmeta_expr_from_json(json_obj_get(j, "l"), arena);
        n->binary.right =
            zan_genmeta_expr_from_json(json_obj_get(j, "r"), arena);
        return n;
    }
    if (strcmp(k, "uni") == 0) {
        n = zan_ast_new(arena, AST_UNARY, loc);
        n->unary.op = gm_token_kind(json_get_str(json_obj_get(j, "op")));
        n->unary.operand =
            zan_genmeta_expr_from_json(json_obj_get(j, "o"), arena);
        return n;
    }
    if (strcmp(k, "mem") == 0) {
        const char *nm = json_get_str(json_obj_get(j, "n"));
        n = zan_ast_new(arena, AST_MEMBER_ACCESS, loc);
        n->member.object =
            zan_genmeta_expr_from_json(json_obj_get(j, "o"), arena);
        n->member.name.str = nm ? zan_arena_strdup(arena, nm, strlen(nm)) : NULL;
        n->member.name.len = nm ? (uint32_t)strlen(nm) : 0;
        n->member.null_cond = 0;
        return n;
    }
    if (strcmp(k, "call") == 0) {
        n = zan_ast_new(arena, AST_CALL, loc);
        n->call.callee = zan_genmeta_expr_from_json(json_obj_get(j, "f"), arena);
        zan_ast_list_init(&n->call.args);
        zan_ast_list_init(&n->call.type_args);
        f = json_obj_get(j, "a");
        if (f && f->type == JSON_ARR)
            for (int i = 0; i < f->as.arr.count; i++)
                zan_ast_list_push(&n->call.args,
                                  zan_genmeta_expr_from_json(f->as.arr.items[i],
                                                             arena),
                                  arena);
        return n;
    }
    if (strcmp(k, "cond") == 0) {
        n = zan_ast_new(arena, AST_CONDITIONAL, loc);
        n->conditional.cond =
            zan_genmeta_expr_from_json(json_obj_get(j, "c"), arena);
        n->conditional.then_expr =
            zan_genmeta_expr_from_json(json_obj_get(j, "t"), arena);
        n->conditional.else_expr =
            zan_genmeta_expr_from_json(json_obj_get(j, "e"), arena);
        return n;
    }
    if (strcmp(k, "cast") == 0) {
        n = zan_ast_new(arena, AST_CAST_EXPR, loc);
        n->cast.type =
            gm_type_ref_from(json_get_str(json_obj_get(j, "t")), arena, loc);
        n->cast.expr =
            zan_genmeta_expr_from_json(json_obj_get(j, "e"), arena);
        return n;
    }
    if (strcmp(k, "is") == 0) {
        n = zan_ast_new(arena, AST_IS_EXPR, loc);
        n->type_test.expr =
            zan_genmeta_expr_from_json(json_obj_get(j, "e"), arena);
        n->type_test.type =
            gm_type_ref_from(json_get_str(json_obj_get(j, "t")), arena, loc);
        return n;
    }
    if (strcmp(k, "idx") == 0) {
        n = zan_ast_new(arena, AST_INDEX, loc);
        n->index.object =
            zan_genmeta_expr_from_json(json_obj_get(j, "o"), arena);
        n->index.index =
            zan_genmeta_expr_from_json(json_obj_get(j, "i"), arena);
        return n;
    }
    if (strcmp(k, "assign") == 0) {
        n = zan_ast_new(arena, AST_ASSIGNMENT, loc);
        n->binary.op = gm_token_kind(json_get_str(json_obj_get(j, "op")));
        n->binary.left =
            zan_genmeta_expr_from_json(json_obj_get(j, "l"), arena);
        n->binary.right =
            zan_genmeta_expr_from_json(json_obj_get(j, "r"), arena);
        return n;
    }
    if (strcmp(k, "new") == 0) {
        n = zan_ast_new(arena, AST_NEW_EXPR, loc);
        n->new_expr.type =
            gm_type_ref_from(json_get_str(json_obj_get(j, "t")), arena, loc);
        zan_ast_list_init(&n->new_expr.args);
        f = json_obj_get(j, "a");
        if (f && f->type == JSON_ARR)
            for (int i = 0; i < f->as.arr.count; i++)
                zan_ast_list_push(&n->new_expr.args,
                                  zan_genmeta_expr_from_json(f->as.arr.items[i],
                                                             arena),
                                  arena);
        n->new_expr.is_array = false;
        n->new_expr.array_init = false;
        return n;
    }
    if (strcmp(k, "block") == 0) {
        /* a lambda with a block body: only the shape matters */
        n = zan_ast_new(arena, AST_BLOCK, loc);
        zan_ast_list_init(&n->block.stmts);
        return n;
    }
    if (strcmp(k, "lam") == 0) {
        n = zan_ast_new(arena, AST_LAMBDA, loc);
        zan_ast_list_init(&n->lambda.params);
        f = json_obj_get(j, "p");
        if (f && f->type == JSON_ARR) {
            for (int i = 0; i < f->as.arr.count; i++) {
                const char *pn = json_get_str(f->as.arr.items[i]);
                if (!pn) continue;
                zan_ast_node_t *p = zan_ast_new(arena, AST_PARAM, loc);
                p->param.name.str = zan_arena_strdup(arena, pn, strlen(pn));
                p->param.name.len = (uint32_t)strlen(pn);
                p->param.type = NULL;
                p->param.default_val = NULL;
                p->param.is_params = 0;
                p->param.by_ref = 0;
                p->param.is_this = 0;
                zan_ast_list_push(&n->lambda.params, p, arena);
            }
        }
        n->lambda.body =
            zan_genmeta_expr_from_json(json_obj_get(j, "b"), arena);
        return n;
    }
    if (strcmp(k, "await") == 0) {
        n = zan_ast_new(arena, AST_AWAIT_EXPR, loc);
        n->await_expr.expr =
            zan_genmeta_expr_from_json(json_obj_get(j, "o"), arena);
        return n;
    }
    if (strcmp(k, "ref") == 0) {
        n = zan_ast_new(arena, AST_REF_ARG, loc);
        n->ref_arg.expr =
            zan_genmeta_expr_from_json(json_obj_get(j, "o"), arena);
        n->ref_arg.decl_type = NULL;
        n->ref_arg.is_out = 0;
        return n;
    }
    return NULL; /* "other": unknown shape, caller skips the directive */
}
