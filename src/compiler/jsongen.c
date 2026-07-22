/* jsongen.c -- compile-time entity mapper generation for System.Json.
 *
 * `Json.Deserialize<T>(text)` and `Json.Serialize<T>(obj)` are lowered at
 * compile time: for every class T reachable from a call site (including
 * classes referenced by fields, transitively), a binder (`B_T`), tree
 * serializer (`T_T`) and the string-facing entry points (`D_T` / `S_T`) are
 * generated as Zan source into a synthetic `__JsonBind` class, parsed, and
 * merged into the compilation unit. Call sites are then rewritten to the
 * generated methods, so no reflection or runtime type info is needed.
 *
 * Mapping is deliberately lenient (JSON from the wild rarely matches an
 * entity exactly): missing keys keep field defaults, scalar mismatches are
 * coerced through the JsonValue As* helpers, a single object where a
 * List<T> is expected binds as a one-element list, extra JSON keys are
 * ignored, and a field declared as JsonValue passes its subtree through
 * untouched for fully dynamic access.
 */

#include "jsongen.h"
#include "arena.h"
#include "diag.h"
#include "lexer.h"
#include "parser.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- growable source buffer ---- */

typedef struct {
    char *buf;
    size_t len;
    size_t cap;
} jg_buf_t;

static void jg_putf(jg_buf_t *b, const char *fmt, ...) {
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

/* ---- helpers ---- */

static bool istr_is(zan_istr_t s, const char *lit) {
    size_t n = strlen(lit);
    return s.len == n && memcmp(s.str, lit, n) == 0;
}

/* ---- declaration registry ---- */

typedef struct {
    zan_ast_node_t *unit;
} jg_decls_t;

static zan_ast_node_t *jg_find_decl(zan_ast_node_t *unit, zan_istr_t name,
                                    zan_ast_kind_t kind) {
    for (int i = 0; i < unit->comp_unit.decls.count; i++) {
        zan_ast_node_t *d = unit->comp_unit.decls.items[i];
        if (d->kind != kind) continue;
        zan_istr_t dn = (kind == AST_ENUM_DECL || kind == AST_CLASS_DECL ||
                         kind == AST_STRUCT_DECL)
                            ? d->type_decl.name
                            : d->type_decl.name;
        if (dn.len == name.len &&
            memcmp(dn.str, name.str, (size_t)name.len) == 0)
            return d;
    }
    return NULL;
}

/* ---- field kind classification ---- */

typedef enum {
    FK_INT,       /* int / long / short / byte / sbyte / uint / ushort / ulong / char */
    FK_FLOAT,     /* double / float / decimal */
    FK_BOOL,
    FK_STRING,
    FK_ENUM,
    FK_CLASS,
    FK_JSONVALUE,
    FK_LIST,
    FK_SKIP,
} jg_fk_t;

static bool jg_is_int_name(zan_istr_t n) {
    return istr_is(n, "int") || istr_is(n, "long") || istr_is(n, "short") ||
           istr_is(n, "byte") || istr_is(n, "sbyte") || istr_is(n, "uint") ||
           istr_is(n, "ushort") || istr_is(n, "ulong") || istr_is(n, "char");
}

static bool jg_is_float_name(zan_istr_t n) {
    return istr_is(n, "double") || istr_is(n, "float") || istr_is(n, "decimal");
}

/* Classify a scalar/class element type (no List nesting). */
static jg_fk_t jg_classify_base(zan_ast_node_t *unit, zan_istr_t name) {
    if (jg_is_int_name(name)) return FK_INT;
    if (jg_is_float_name(name)) return FK_FLOAT;
    if (istr_is(name, "bool")) return FK_BOOL;
    if (istr_is(name, "string")) return FK_STRING;
    if (istr_is(name, "JsonValue")) return FK_JSONVALUE;
    if (jg_find_decl(unit, name, AST_ENUM_DECL)) return FK_ENUM;
    if (jg_find_decl(unit, name, AST_CLASS_DECL)) return FK_CLASS;
    return FK_SKIP;
}

static jg_fk_t jg_classify(zan_ast_node_t *unit, zan_ast_node_t *tref,
                           zan_istr_t *elem_name, jg_fk_t *elem_kind) {
    if (!tref || tref->kind != AST_TYPE_REF) return FK_SKIP;
    if (tref->type_ref.is_array) return FK_SKIP;
    zan_istr_t name = tref->type_ref.name;
    if (istr_is(name, "List") && tref->type_ref.type_args.count == 1) {
        zan_ast_node_t *ea = tref->type_ref.type_args.items[0];
        if (!ea || ea->kind != AST_TYPE_REF || ea->type_ref.is_array ||
            ea->type_ref.type_args.count > 0)
            return FK_SKIP;
        jg_fk_t ek = jg_classify_base(unit, ea->type_ref.name);
        if (ek == FK_SKIP) return FK_SKIP;
        *elem_name = ea->type_ref.name;
        *elem_kind = ek;
        return FK_LIST;
    }
    if (tref->type_ref.type_args.count > 0) return FK_SKIP;
    return jg_classify_base(unit, name);
}

/* ---- needed-class worklist ---- */

typedef struct {
    zan_istr_t *names;
    bool *root_list; /* Deserialize<List<T>> / Serialize<List<T>> was requested */
    int count;
    int cap;
} jg_need_t;

static int jg_need_find(jg_need_t *need, zan_istr_t name) {
    for (int i = 0; i < need->count; i++) {
        if (need->names[i].len == name.len &&
            memcmp(need->names[i].str, name.str, (size_t)name.len) == 0)
            return i;
    }
    return -1;
}

static int jg_need_add(jg_need_t *need, zan_istr_t name) {
    int at = jg_need_find(need, name);
    if (at >= 0) return at;
    if (need->count >= need->cap) {
        int ncap = need->cap ? need->cap * 2 : 16;
        need->names = (zan_istr_t *)realloc(need->names,
                                            sizeof(zan_istr_t) * (size_t)ncap);
        need->root_list = (bool *)realloc(need->root_list,
                                          sizeof(bool) * (size_t)ncap);
        need->cap = ncap;
    }
    need->names[need->count] = name;
    need->root_list[need->count] = false;
    return need->count++;
}

/* ---- call-site scan + rewrite ---- */

typedef struct {
    zan_ast_node_t *unit;
    zan_arena_t *arena;
    zan_diag_t *diag;
    jg_need_t need;
    bool any;
} jg_ctx_t;

/* True when `call` is Json.Deserialize<...> / Json.Serialize<...>. */
static bool jg_is_json_call(zan_ast_node_t *call, bool *is_deser) {
    if (call->kind != AST_CALL) return false;
    zan_ast_node_t *callee = call->call.callee;
    if (!callee || callee->kind != AST_MEMBER_ACCESS) return false;
    zan_ast_node_t *obj = callee->member.object;
    if (!obj || obj->kind != AST_IDENTIFIER || !istr_is(obj->ident.name, "Json"))
        return false;
    if (istr_is(callee->member.name, "Deserialize")) { *is_deser = true; return true; }
    if (istr_is(callee->member.name, "Serialize")) { *is_deser = false; return true; }
    return false;
}

static zan_istr_t jg_arena_name(zan_arena_t *arena, const char *prefix,
                                zan_istr_t cls) {
    size_t plen = strlen(prefix);
    char *s = (char *)zan_arena_alloc(arena, plen + (size_t)cls.len + 1);
    memcpy(s, prefix, plen);
    memcpy(s + plen, cls.str, (size_t)cls.len);
    s[plen + cls.len] = '\0';
    zan_istr_t r = { s, (uint32_t)(plen + cls.len) };
    return r;
}

static void jg_visit_expr(jg_ctx_t *c, zan_ast_node_t *e);

static void jg_visit_list(jg_ctx_t *c, zan_ast_list_t *l) {
    for (int i = 0; i < l->count; i++) jg_visit_expr(c, l->items[i]);
}

static void jg_rewrite_call(jg_ctx_t *c, zan_ast_node_t *call, bool is_deser) {
    zan_ast_node_t *targ = call->call.type_args.items[0];
    if (!targ || targ->kind != AST_TYPE_REF || targ->type_ref.is_array ||
        targ->type_ref.is_nullable) {
        zan_diag_emit(c->diag, DIAG_ERROR, call->loc,
                      "Json.%s<T>: unsupported type argument",
                      is_deser ? "Deserialize" : "Serialize");
        return;
    }
    zan_istr_t cls;
    bool as_list = false;
    if (istr_is(targ->type_ref.name, "List") &&
        targ->type_ref.type_args.count == 1 &&
        targ->type_ref.type_args.items[0]->kind == AST_TYPE_REF) {
        cls = targ->type_ref.type_args.items[0]->type_ref.name;
        as_list = true;
    } else if (targ->type_ref.type_args.count == 0) {
        cls = targ->type_ref.name;
    } else {
        zan_diag_emit(c->diag, DIAG_ERROR, call->loc,
                      "Json.%s<T>: T must be a class or List<Class>",
                      is_deser ? "Deserialize" : "Serialize");
        return;
    }
    if (!jg_find_decl(c->unit, cls, AST_CLASS_DECL)) {
        zan_diag_emit(c->diag, DIAG_ERROR, call->loc,
                      "Json.%s<T>: '%.*s' is not a known class",
                      is_deser ? "Deserialize" : "Serialize",
                      (int)cls.len, cls.str);
        return;
    }
    if (call->call.args.count != 1) {
        zan_diag_emit(c->diag, DIAG_ERROR, call->loc,
                      "Json.%s<T> takes exactly one argument",
                      is_deser ? "Deserialize" : "Serialize");
        return;
    }
    int at = jg_need_add(&c->need, cls);
    if (as_list) c->need.root_list[at] = true;
    c->any = true;

    const char *prefix = is_deser ? (as_list ? "DL_" : "D_")
                                  : (as_list ? "SL_" : "S_");
    zan_ast_node_t *callee = call->call.callee;
    callee->member.object->ident.name =
        (zan_istr_t){ "__JsonBind", 10 };
    callee->member.name = jg_arena_name(c->arena, prefix, cls);
    call->call.type_args.count = 0;
}

static void jg_visit_expr(jg_ctx_t *c, zan_ast_node_t *e) {
    if (!e) return;
    switch (e->kind) {
    case AST_BLOCK: jg_visit_list(c, &e->block.stmts); break;
    case AST_VAR_DECL: jg_visit_expr(c, e->var_decl.initializer); break;
    case AST_EXPR_STMT: jg_visit_expr(c, e->expr_stmt.expr); break;
    case AST_RETURN_STMT: jg_visit_expr(c, e->ret.value); break;
    case AST_IF_STMT:
        jg_visit_expr(c, e->if_stmt.cond);
        jg_visit_expr(c, e->if_stmt.then_body);
        jg_visit_expr(c, e->if_stmt.else_body);
        break;
    case AST_WHILE_STMT:
    case AST_DO_WHILE_STMT:
        jg_visit_expr(c, e->while_stmt.cond);
        jg_visit_expr(c, e->while_stmt.body);
        break;
    case AST_FOR_STMT:
        jg_visit_expr(c, e->for_stmt.init);
        jg_visit_expr(c, e->for_stmt.cond);
        jg_visit_expr(c, e->for_stmt.step);
        jg_visit_expr(c, e->for_stmt.body);
        break;
    case AST_FOREACH_STMT:
        jg_visit_expr(c, e->foreach_stmt.collection);
        jg_visit_expr(c, e->foreach_stmt.body);
        break;
    case AST_THROW_STMT: jg_visit_expr(c, e->throw_stmt.value); break;
    case AST_TRY_STMT:
        jg_visit_expr(c, e->try_stmt.try_body);
        for (int i = 0; i < e->try_stmt.catches.count; i++)
            jg_visit_expr(c, e->try_stmt.catches.items[i]->catch_clause.body);
        jg_visit_expr(c, e->try_stmt.finally_body);
        break;
    case AST_SWITCH_STMT:
        jg_visit_expr(c, e->switch_stmt.expr);
        for (int i = 0; i < e->switch_stmt.cases.count; i++) {
            jg_visit_expr(c, e->switch_stmt.cases.items[i]->switch_case.pattern);
            jg_visit_expr(c, e->switch_stmt.cases.items[i]->switch_case.body);
        }
        break;
    case AST_YIELD_STMT: jg_visit_expr(c, e->yield_stmt.value); break;
    case AST_LOCK_STMT:
        jg_visit_expr(c, e->lock_stmt.expr);
        jg_visit_expr(c, e->lock_stmt.body);
        break;
    case AST_BINARY:
    case AST_ASSIGNMENT:
        jg_visit_expr(c, e->binary.left);
        jg_visit_expr(c, e->binary.right);
        break;
    case AST_UNARY:
    case AST_POSTFIX_UNARY:
        jg_visit_expr(c, e->unary.operand);
        break;
    case AST_CALL: {
        bool is_deser = false;
        if (jg_is_json_call(e, &is_deser)) {
            if (e->call.type_args.count == 1) {
                jg_rewrite_call(c, e, is_deser);
            } else {
                zan_diag_emit(c->diag, DIAG_ERROR, e->loc,
                              "Json.%s requires an explicit type argument: "
                              "Json.%s<T>(...)",
                              is_deser ? "Deserialize" : "Serialize",
                              is_deser ? "Deserialize" : "Serialize");
            }
        }
        jg_visit_expr(c, e->call.callee);
        jg_visit_list(c, &e->call.args);
        break;
    }
    case AST_MEMBER_ACCESS: jg_visit_expr(c, e->member.object); break;
    case AST_INDEX:
        jg_visit_expr(c, e->index.object);
        jg_visit_expr(c, e->index.index);
        break;
    case AST_NEW_EXPR: jg_visit_list(c, &e->new_expr.args); break;
    case AST_CAST_EXPR: jg_visit_expr(c, e->cast.expr); break;
    case AST_IS_EXPR:
    case AST_AS_EXPR:
        jg_visit_expr(c, e->type_test.expr);
        break;
    case AST_CONDITIONAL:
        jg_visit_expr(c, e->conditional.cond);
        jg_visit_expr(c, e->conditional.then_expr);
        jg_visit_expr(c, e->conditional.else_expr);
        break;
    case AST_LAMBDA: jg_visit_expr(c, e->lambda.body); break;
    case AST_AWAIT_EXPR: jg_visit_expr(c, e->await_expr.expr); break;
    case AST_STRING_INTERP: jg_visit_list(c, &e->string_interp.parts); break;
    case AST_REF_ARG: jg_visit_expr(c, e->ref_arg.expr); break;
    case AST_QUERY_EXPR:
        jg_visit_expr(c, e->query.source);
        jg_visit_list(c, &e->query.wheres);
        jg_visit_expr(c, e->query.select);
        break;
    default: break;
    }
}

/* ---- per-class code generation ---- */

/* Collect a class's serializable fields including inherited base-class
 * fields (base first, C#-style). Static fields and unsupported types are
 * skipped. */
static void jg_gen_fields(jg_ctx_t *c, jg_buf_t *out, zan_istr_t cls_name,
                          zan_ast_node_t *cls, bool bind, int depth) {
    if (depth > 16 || !cls) return;
    /* base class fields first */
    for (int bx = 0; bx < cls->type_decl.bases.count; bx++) {
        zan_ast_node_t *bref = cls->type_decl.bases.items[bx];
        if (bref->kind != AST_TYPE_REF) continue;
        zan_ast_node_t *bdecl =
            jg_find_decl(c->unit, bref->type_ref.name, AST_CLASS_DECL);
        if (bdecl)
            jg_gen_fields(c, out, bref->type_ref.name, bdecl, bind, depth + 1);
    }

    for (int i = 0; i < cls->type_decl.members.count; i++) {
        zan_ast_node_t *m = cls->type_decl.members.items[i];
        if (m->kind != AST_FIELD_DECL) continue;
        if (m->field_decl.modifiers & MOD_STATIC) continue;
        zan_istr_t fname = m->field_decl.name;
        int FL = (int)fname.len;
        const char *FS = fname.str;

        zan_istr_t elem_name = { NULL, 0 };
        jg_fk_t ek = FK_SKIP;
        jg_fk_t fk = jg_classify(c->unit, m->field_decl.type, &elem_name, &ek);
        zan_ast_node_t *tref = m->field_decl.type;
        zan_istr_t tname = (tref && tref->kind == AST_TYPE_REF)
                               ? tref->type_ref.name
                               : (zan_istr_t){ "?", 1 };

        if (fk == FK_SKIP) {
            zan_diag_emit(c->diag, DIAG_WARNING, m->loc,
                          "Json mapper for '%.*s': field '%.*s' has an "
                          "unsupported type and is skipped",
                          (int)cls_name.len, cls_name.str, FL, FS);
            continue;
        }
        if (fk == FK_CLASS) jg_need_add(&c->need, tname);
        if (fk == FK_LIST && ek == FK_CLASS) jg_need_add(&c->need, elem_name);

        if (bind) {
            switch (fk) {
            case FK_INT:
            case FK_ENUM:
                jg_putf(out, "        o.%.*s = (%.*s)v.Int(\"%.*s\", 0);\n",
                        FL, FS, (int)tname.len, tname.str, FL, FS);
                break;
            case FK_FLOAT:
                jg_putf(out, "        o.%.*s = (%.*s)v.Double(\"%.*s\", 0.0);\n",
                        FL, FS, (int)tname.len, tname.str, FL, FS);
                break;
            case FK_BOOL:
                jg_putf(out, "        o.%.*s = v.Bool(\"%.*s\", false);\n",
                        FL, FS, FL, FS);
                break;
            case FK_STRING:
                jg_putf(out, "        o.%.*s = v.Str(\"%.*s\", \"\");\n",
                        FL, FS, FL, FS);
                break;
            case FK_JSONVALUE:
                jg_putf(out, "        o.%.*s = v.Get(\"%.*s\");\n",
                        FL, FS, FL, FS);
                break;
            case FK_CLASS:
                jg_putf(out,
                    "        JsonValue j_%.*s = v.Get(\"%.*s\");\n"
                    "        if (j_%.*s != null && j_%.*s.IsObject()) { "
                    "o.%.*s = __JsonBind.B_%.*s(j_%.*s); }\n",
                    FL, FS, FL, FS, FL, FS, FL, FS, FL, FS,
                    (int)tname.len, tname.str, FL, FS);
                break;
            case FK_LIST: {
                int EL = (int)elem_name.len;
                const char *ES = elem_name.str;
                jg_putf(out,
                    "        JsonValue j_%.*s = v.Get(\"%.*s\");\n"
                    "        o.%.*s = new List<%.*s>();\n",
                    FL, FS, FL, FS, FL, FS, EL, ES);
                char elem_from[256];
                const char *src_one = "j_%.*s";
                (void)src_one;
                switch (ek) {
                case FK_INT:
                case FK_ENUM:
                    snprintf(elem_from, sizeof(elem_from),
                             "(%.*s)%%s.AsInt(0)", EL, ES);
                    break;
                case FK_FLOAT:
                    snprintf(elem_from, sizeof(elem_from),
                             "(%.*s)%%s.AsDouble(0.0)", EL, ES);
                    break;
                case FK_BOOL:
                    snprintf(elem_from, sizeof(elem_from), "%%s.AsBool(false)");
                    break;
                case FK_STRING:
                    snprintf(elem_from, sizeof(elem_from), "%%s.AsString(\"\")");
                    break;
                case FK_JSONVALUE:
                    snprintf(elem_from, sizeof(elem_from), "%%s");
                    break;
                default: /* FK_CLASS */
                    snprintf(elem_from, sizeof(elem_from),
                             "__JsonBind.B_%.*s(%%s)", EL, ES);
                    break;
                }
                char at_expr[128], one_expr[128];
                snprintf(at_expr, sizeof(at_expr), "j_%.*s.At(i_%.*s)",
                         FL, FS, FL, FS);
                snprintf(one_expr, sizeof(one_expr), "j_%.*s", FL, FS);
                char conv_at[512], conv_one[512];
                snprintf(conv_at, sizeof(conv_at), elem_from, at_expr);
                snprintf(conv_one, sizeof(conv_one), elem_from, one_expr);
                jg_putf(out,
                    "        if (j_%.*s != null && j_%.*s.IsArray()) {\n"
                    "            int i_%.*s = 0;\n"
                    "            while (i_%.*s < j_%.*s.Count()) {\n"
                    "                o.%.*s.Add(%s);\n"
                    "                i_%.*s = i_%.*s + 1;\n"
                    "            }\n"
                    "        } else if (j_%.*s != null && !j_%.*s.IsNull()) {\n"
                    "            o.%.*s.Add(%s);\n"
                    "        }\n",
                    FL, FS, FL, FS, FL, FS, FL, FS, FL, FS, FL, FS, conv_at,
                    FL, FS, FL, FS, FL, FS, FL, FS, FL, FS, conv_one);
                break;
            }
            default: break;
            }
        } else {
            /* tree (serialize) direction */
            switch (fk) {
            case FK_INT:
                jg_putf(out,
                    "        v.Put(\"%.*s\", JsonValue.NewNum("
                    "Convert.ToString(o.%.*s)));\n",
                    FL, FS, FL, FS);
                break;
            case FK_ENUM:
                jg_putf(out,
                    "        v.Put(\"%.*s\", JsonValue.NewNum("
                    "Convert.ToString((int)o.%.*s)));\n",
                    FL, FS, FL, FS);
                break;
            case FK_FLOAT:
                jg_putf(out,
                    "        v.Put(\"%.*s\", JsonValue.NewNum("
                    "Convert.ToString(o.%.*s)));\n",
                    FL, FS, FL, FS);
                break;
            case FK_BOOL:
                jg_putf(out,
                    "        v.Put(\"%.*s\", JsonValue.NewBool(o.%.*s));\n",
                    FL, FS, FL, FS);
                break;
            case FK_STRING:
                jg_putf(out,
                    "        v.Put(\"%.*s\", __JsonBind.T_Str(o.%.*s));\n",
                    FL, FS, FL, FS);
                break;
            case FK_JSONVALUE:
                jg_putf(out,
                    "        v.Put(\"%.*s\", __JsonBind.T_Jv(o.%.*s));\n",
                    FL, FS, FL, FS);
                break;
            case FK_CLASS:
                jg_putf(out,
                    "        v.Put(\"%.*s\", __JsonBind.T_%.*s(o.%.*s));\n",
                    FL, FS, (int)tname.len, tname.str, FL, FS);
                break;
            case FK_LIST: {
                int EL = (int)elem_name.len;
                const char *ES = elem_name.str;
                char elem_to[256];
                switch (ek) {
                case FK_INT:
                case FK_FLOAT:
                    snprintf(elem_to, sizeof(elem_to),
                             "JsonValue.NewNum(Convert.ToString(%%s))");
                    break;
                case FK_ENUM:
                    snprintf(elem_to, sizeof(elem_to),
                             "JsonValue.NewNum(Convert.ToString((int)%%s))");
                    break;
                case FK_BOOL:
                    snprintf(elem_to, sizeof(elem_to), "JsonValue.NewBool(%%s)");
                    break;
                case FK_STRING:
                    snprintf(elem_to, sizeof(elem_to), "__JsonBind.T_Str(%%s)");
                    break;
                case FK_JSONVALUE:
                    snprintf(elem_to, sizeof(elem_to), "__JsonBind.T_Jv(%%s)");
                    break;
                default: /* FK_CLASS */
                    snprintf(elem_to, sizeof(elem_to), "__JsonBind.T_%.*s(%%s)",
                             EL, ES);
                    break;
                }
                char item[128];
                snprintf(item, sizeof(item), "o.%.*s[i_%.*s]", FL, FS, FL, FS);
                char conv[512];
                snprintf(conv, sizeof(conv), elem_to, item);
                jg_putf(out,
                    "        JsonValue a_%.*s = JsonValue.NewArray();\n"
                    "        if (o.%.*s != null) {\n"
                    "            int i_%.*s = 0;\n"
                    "            while (i_%.*s < o.%.*s.Count) {\n"
                    "                a_%.*s.Append(%s);\n"
                    "                i_%.*s = i_%.*s + 1;\n"
                    "            }\n"
                    "        }\n"
                    "        v.Put(\"%.*s\", a_%.*s);\n",
                    FL, FS, FL, FS, FL, FS, FL, FS, FL, FS, FL, FS, conv,
                    FL, FS, FL, FS, FL, FS, FL, FS);
                break;
            }
            default: break;
            }
        }
    }
}

static void jg_gen_class(jg_ctx_t *c, jg_buf_t *out, int idx) {
    zan_istr_t cls = c->need.names[idx];
    int CL = (int)cls.len;
    const char *CS = cls.str;
    zan_ast_node_t *decl = jg_find_decl(c->unit, cls, AST_CLASS_DECL);
    if (!decl) return;

    /* binder: JsonValue -> entity (lenient) */
    jg_putf(out, "    static %.*s B_%.*s(JsonValue v) {\n", CL, CS, CL, CS);
    jg_putf(out, "        %.*s o = new %.*s();\n", CL, CS, CL, CS);
    jg_putf(out, "        if (v == null || !v.IsObject()) { return o; }\n");
    jg_gen_fields(c, out, cls, decl, true, 0);
    jg_putf(out, "        return o;\n    }\n");

    /* tree: entity -> JsonValue */
    jg_putf(out, "    static JsonValue T_%.*s(%.*s o) {\n", CL, CS, CL, CS);
    jg_putf(out, "        if (o == null) { return JsonValue.NewNull(); }\n");
    jg_putf(out, "        JsonValue v = JsonValue.NewObject();\n");
    jg_gen_fields(c, out, cls, decl, false, 0);
    jg_putf(out, "        return v;\n    }\n");

    /* string-facing entry points */
    jg_putf(out,
        "    static %.*s D_%.*s(string s) { "
        "return __JsonBind.B_%.*s(JsonValue.Parse(s)); }\n",
        CL, CS, CL, CS, CL, CS);
    jg_putf(out,
        "    static string S_%.*s(%.*s o) { "
        "return __JsonBind.T_%.*s(o).ToJson(); }\n",
        CL, CS, CL, CS, CL, CS);

    if (c->need.root_list[idx]) {
        jg_putf(out,
            "    static List<%.*s> DL_%.*s(string s) {\n"
            "        JsonValue v = JsonValue.Parse(s);\n"
            "        List<%.*s> r = new List<%.*s>();\n"
            "        if (v != null && v.IsArray()) {\n"
            "            int i = 0;\n"
            "            while (i < v.Count()) { "
            "r.Add(__JsonBind.B_%.*s(v.At(i))); i = i + 1; }\n"
            "        } else if (v != null && v.IsObject()) {\n"
            "            r.Add(__JsonBind.B_%.*s(v));\n"
            "        }\n"
            "        return r;\n"
            "    }\n",
            CL, CS, CL, CS, CL, CS, CL, CS, CL, CS, CL, CS);
        jg_putf(out,
            "    static string SL_%.*s(List<%.*s> l) {\n"
            "        JsonValue a = JsonValue.NewArray();\n"
            "        if (l != null) {\n"
            "            int i = 0;\n"
            "            while (i < l.Count) { "
            "a.Append(__JsonBind.T_%.*s(l[i])); i = i + 1; }\n"
            "        }\n"
            "        return a.ToJson();\n"
            "    }\n",
            CL, CS, CL, CS, CL, CS);
    }
}

/* ---- entry point ---- */

void zan_jsongen_run(zan_ast_node_t *unit, zan_arena_t *arena,
                     zan_diag_t *diag) {
    jg_ctx_t c;
    memset(&c, 0, sizeof(c));
    c.unit = unit;
    c.arena = arena;
    c.diag = diag;

    /* scan every method/constructor body for Json.Deserialize / Json.Serialize */
    for (int i = 0; i < unit->comp_unit.decls.count; i++) {
        zan_ast_node_t *d = unit->comp_unit.decls.items[i];
        if (d->kind != AST_CLASS_DECL && d->kind != AST_STRUCT_DECL) continue;
        for (int m = 0; m < d->type_decl.members.count; m++) {
            zan_ast_node_t *mem = d->type_decl.members.items[m];
            if (mem->kind == AST_METHOD_DECL ||
                mem->kind == AST_CONSTRUCTOR_DECL)
                jg_visit_expr(&c, mem->method_decl.body);
            else if (mem->kind == AST_FIELD_DECL)
                jg_visit_expr(&c, mem->field_decl.initializer);
        }
    }

    if (!c.any || zan_diag_has_errors(diag)) {
        free(c.need.names);
        free(c.need.root_list);
        return;
    }

    jg_buf_t out;
    memset(&out, 0, sizeof(out));
    jg_putf(&out, "class __JsonBind {\n");
    jg_putf(&out,
        "    static JsonValue T_Str(string s) { "
        "if (s == null) { return JsonValue.NewNull(); } "
        "return JsonValue.NewStr(s); }\n");
    jg_putf(&out,
        "    static JsonValue T_Jv(JsonValue x) { "
        "if (x == null) { return JsonValue.NewNull(); } "
        "return x; }\n");
    /* jg_gen_class may append to c.need while generating (nested classes) */
    for (int i = 0; i < c.need.count; i++)
        jg_gen_class(&c, &out, i);
    jg_putf(&out, "}\n");

    char *gsrc = zan_arena_strdup(arena, out.buf, out.len);
    size_t glen = out.len;
    free(out.buf);
    free(c.need.names);
    free(c.need.root_list);

    zan_lexer_t lex;
    zan_lexer_init(&lex, gsrc, glen, 0, arena, diag);
    zan_parser_t gp;
    zan_parser_init(&gp, &lex, arena, diag);
    zan_ast_node_t *gu = zan_parser_parse(&gp);
    if (!gu) return;
    for (int i = 0; i < gu->comp_unit.decls.count; i++)
        zan_ast_list_push(&unit->comp_unit.decls, gu->comp_unit.decls.items[i],
                          arena);
}
