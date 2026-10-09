/* intellisense */
#include "intellisense.h"
#include "../common/json.h"
/* 内部辅助逻辑 */
#include "../compiler/builtin_api.h"
#include "../compiler/parser.h"
#include "../compiler/arena.h"
#include "../compiler/diag.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

#ifndef _WIN32
#include <strings.h>
#define _strnicmp strncasecmp
#define _strdup strdup
#endif

#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#endif
#include "../common/host_oom.h"

/* 核心系统底层抽象与内存语义契约 */
static const char *builtin_types[] = {
    "int", "float", "double", "bool", "string", "char", "byte", "long",
    "short", "void", "var", "object", "decimal", "uint", "ulong",
    "ushort", "sbyte", NULL
};

/* 核心系统底层抽象与内存语义契约 */
static const char *builtin_keywords[] = {
    "abstract", "as", "async", "await", "base", "break",
    "case", "catch", "class", "const", "continue",
    "default", "delegate", "do", "else", "enum", "event",
    "extern", "false", "finally", "for", "foreach",
    "if", "in", "interface", "internal", "is",
    "namespace", "new", "null", "operator", "out", "override",
    "params", "private", "protected", "public", "readonly",
    "ref", "return", "sealed", "static", "struct", "switch",
    "this", "throw", "true", "try", "typeof",
    "using", "virtual", "volatile", "while", "yield",
    NULL
};

/* 内部辅助逻辑 */

/* 模块核心语义抽象与接口调用契约 */
static const zan_builtin_type_t *builtin_receiver(const char *type_name) {
    if (!type_name || !type_name[0]) return NULL;
    char bare[64];
    snprintf(bare, sizeof(bare), "%s", type_name);
    char *lt = strchr(bare, '<');
    if (lt) *lt = '\0';
    char *br = strstr(bare, "[]");
    if (br) *br = '\0';

    const zan_builtin_type_t *bt = zan_builtin_find(bare);
    if (bt) return bt;

    int count = 0;
    const zan_builtin_type_t *all = zan_builtin_types(&count);
    for (int i = 0; i < count; i++) {
        if (strcmp(all[i].name_public, bare) == 0) return &all[i];
    }
    return NULL;
}

/* 模块核心语义抽象与接口调用契约 */
static bool already_offered(const intellisense_t *is, const char *label) {
    for (int i = 0; i < is->completion_count; i++) {
        if (strcmp(is->completions[i].label, label) == 0) return true;
    }
    return false;
}

/* 底层系统交互与数据协议契约 */

/* 模块核心语义抽象与接口调用契约 */
static bool reserve_symbols(intellisense_t *is, int need) {
    if (need <= is->symbol_cap) return true;
    int cap = is->symbol_cap ? is->symbol_cap * 2 : INTEL_INIT_SYMBOLS;
    while (cap < need) cap *= 2;
    isym_t *p = (isym_t *)realloc(is->symbols, (size_t)cap * sizeof(isym_t));
    if (!p) return false;
    is->symbols = p;
    is->symbol_cap = cap;
    return true;
}

/* 模块核心语义抽象与接口调用契约 */
static bool reserve_methods(intellisense_t *is, int need) {
    if (need <= is->method_cap) return true;
    int cap = is->method_cap ? is->method_cap * 2 : 64;
    while (cap < need) cap *= 2;
    imethod_t *p = (imethod_t *)realloc(is->methods, (size_t)cap * sizeof(imethod_t));
    if (!p) return false;
    is->methods = p;
    is->method_cap = cap;
    return true;
}

/* 模块核心语义抽象与接口调用契约 */
static bool reserve_files(intellisense_t *is, int need) {
    if (need <= is->indexed_file_cap) return true;
    int cap = is->indexed_file_cap ? is->indexed_file_cap * 2 : INTEL_INIT_FILES;
    while (cap < need) cap *= 2;
    char (*p)[512] = (char (*)[512])realloc(is->indexed_files, (size_t)cap * 512);
    if (!p) return false;
    is->indexed_files = p;
    is->indexed_file_cap = cap;
    return true;
}

void intel_init(intellisense_t *is) {
    memset(is, 0, sizeof(intellisense_t));
    is->completion_selected = -1;
    intel_register_snippets(is);
}

void intel_clear(intellisense_t *is) {
    is->symbol_count = 0;
    is->method_count = 0;
    is->indexed_file_count = 0;
}

void intel_free(intellisense_t *is) {
    if (!is) return;
    free(is->symbols);
    free(is->indexed_files);
    free(is->methods);
    is->symbols = NULL;
    is->indexed_files = NULL;
    is->methods = NULL;
    is->symbol_count = 0;
    is->symbol_cap = 0;
    is->method_count = 0;
    is->method_cap = 0;
    is->indexed_file_count = 0;
    is->indexed_file_cap = 0;
}

static void add_symbol(intellisense_t *is, const char *name,
                       const char *type_name, const char *parent,
                       const char *signature, const char *file,
                       isym_kind_t kind, int line, int col) {
    if (!reserve_symbols(is, is->symbol_count + 1)) return;
    isym_t *sym = &is->symbols[is->symbol_count++];
    memset(sym, 0, sizeof(isym_t));
    sym->offset = sym->method_offset = -1;
    strncpy(sym->name, name, sizeof(sym->name) - 1);
    strncpy(sym->type_name, type_name ? type_name : "", sizeof(sym->type_name) - 1);
    strncpy(sym->parent, parent ? parent : "", sizeof(sym->parent) - 1);
    strncpy(sym->signature, signature ? signature : "", sizeof(sym->signature) - 1);
    strncpy(sym->file, file ? file : "", sizeof(sym->file) - 1);
    sym->kind = kind;
    sym->line = line;
    sym->col = col;
}

/* 核心系统底层抽象与内存语义契约 */
static void add_symbol_ex(intellisense_t *is, const char *name,
                          const char *type_name, const char *parent,
                          const char *signature, const char *file,
                          const char *doc,
                          isym_kind_t kind, int line, int col,
                          bool is_static, int param_count) {
    if (!reserve_symbols(is, is->symbol_count + 1)) return;
    isym_t *sym = &is->symbols[is->symbol_count++];
    memset(sym, 0, sizeof(isym_t));
    sym->offset = sym->method_offset = -1;
    strncpy(sym->name, name, sizeof(sym->name) - 1);
    strncpy(sym->type_name, type_name ? type_name : "", sizeof(sym->type_name) - 1);
    strncpy(sym->parent, parent ? parent : "", sizeof(sym->parent) - 1);
    strncpy(sym->signature, signature ? signature : "", sizeof(sym->signature) - 1);
    strncpy(sym->file, file ? file : "", sizeof(sym->file) - 1);
    strncpy(sym->doc, doc ? doc : "", sizeof(sym->doc) - 1);
    sym->kind = kind;
    sym->line = line;
    sym->col = col;
    sym->is_static = is_static;
    sym->param_count = param_count;
}

/* 核心系统底层抽象与内存语义契约 */
void intel_register_snippets(intellisense_t *is) {
    is->snippet_count = 0;

    struct { const char *trigger; const char *label; const char *body; const char *desc; } snips[] = {
        {"if",       "if statement",       "if ($1) {\n    $2\n}", "if conditional"},
        {"ifelse",   "if-else statement",  "if ($1) {\n    $2\n} else {\n    $3\n}", "if-else conditional"},
        {"for",      "for loop",           "for (int $1 = 0; $1 < $2; $1++) {\n    $3\n}", "for loop with counter"},
        {"foreach",  "foreach loop",       "foreach (var $1 in $2) {\n    $3\n}", "foreach iteration"},
        {"while",    "while loop",         "while ($1) {\n    $2\n}", "while loop"},
        {"do",       "do-while loop",      "do {\n    $1\n} while ($2);", "do-while loop"},
        {"switch",   "switch statement",   "switch ($1) {\n    case $2:\n        $3\n        break;\n    default:\n        break;\n}", "switch-case"},
        {"try",      "try-catch",          "try {\n    $1\n} catch (Exception $2) {\n    $3\n}", "try-catch block"},
        {"tryf",     "try-catch-finally",  "try {\n    $1\n} catch (Exception $2) {\n    $3\n} finally {\n    $4\n}", "try-catch-finally"},
        {"class",    "class definition",   "class $1 {\n    $2\n}", "class declaration"},
        {"struct",   "struct definition",  "struct $1 {\n    $2\n}", "struct declaration"},
        {"prop",     "property",           "public $1 $2 { get; set; }", "auto property"},
        {"propf",    "full property",      "private $1 _$2;\npublic $1 $2 {\n    get => _$2;\n    set => _$2 = value;\n}", "full property"},
        {"ctor",     "constructor",        "public $1($2) {\n    $3\n}", "constructor"},
        {"method",   "method",             "public $1 $2($3) {\n    $4\n}", "method declaration"},
        {"async",    "async method",       "public async Task $1($2) {\n    $3\n}", "async method"},
        {"main",     "Main method",        "static void Main(string[] args) {\n    $1\n}", "program entry point"},
        {"cw",       "Console.WriteLine",  "Console.WriteLine($1);", "print to console"},
        {"cr",       "Console.ReadLine",   "Console.ReadLine()", "read from console"},
        {NULL, NULL, NULL, NULL}
    };

    for (int i = 0; snips[i].trigger && is->snippet_count < INTEL_MAX_SNIPPETS; i++) {
        snippet_t *s = &is->snippets[is->snippet_count++];
        strncpy(s->trigger, snips[i].trigger, sizeof(s->trigger) - 1);
        strncpy(s->label, snips[i].label, sizeof(s->label) - 1);
        strncpy(s->body, snips[i].body, sizeof(s->body) - 1);
        strncpy(s->description, snips[i].desc, sizeof(s->description) - 1);
    }
}

/* 底层系统交互与数据协议契约 */

/* 内部辅助逻辑 */
static const char *intel_design_widget(int ft) {
    if (ft == 0 || ft == 2 || ft == 3) return "Input";
    if (ft == 1 || ft == 14) return "TextArea";
    if (ft == 4) return "Radio";
    if (ft == 5) return "Checkbox";
    if (ft == 6 || ft == 48) return "SelectBox";
    if (ft == 7) return "Switch";
    if (ft == 8) return "Rate";
    if (ft == 9) return "Slider";
    if (ft == 29) return "Progress";
    if (ft == 49) return "Button";
    if (ft == 18 || ft == 45 || ft == 36) return "Panel";
    return "Label";
}

/* 内部辅助逻辑 */
static int intel_doc_name_lines(const char *text, size_t len,
                                  const char *lines[64], const char *vals[64],
                                  int max) {
    int n = 0, line = 0;
    const char *p = text, *end = text + len;
    while (p < end && n < max) {
        const char *nl = memchr(p, '\n', (size_t)(end - p));
        const char *el = nl ? nl : end;
        const char *q = p;
        while (q + 6 < el &&
               !(q[0] == '"' && q[1] == 'n' && q[2] == 'a' && q[3] == 'm' &&
                 q[4] == 'e' && q[5] == '"')) {
            q++;
        }
        if (q + 6 < el) {
            const char *v = q + 6;
            while (v < el && (*v == ' ' || *v == '\t' || *v == ':')) v++;
            if (v < el && *v == '"') {
                v++;
                const char *vs = v;
                while (v < el && *v != '"') v++;
                char val[128];
                size_t vl = (size_t)(v - vs);
                if (vl < sizeof(val)) {
                    memcpy(val, vs, vl);
                    val[vl] = '\0';
                    char *ln = (char *)malloc(16);
                    if (ln) {
                        snprintf(ln, 16, "%d", line);
                        lines[n] = ln;
                        vals[n] = _strdup(val);
                        n++;
                    }
                }
            }
        }
        if (!nl) break;
        p = nl + 1;
        line++;
    }
    return n;
}

/* 内部辅助实现 */

/* 底层系统交互与数据协议契约 */
static size_t intel_html_decode(const char *v, size_t n, char *out,
                                size_t cap);
static bool intel_html_attr(const char *line, size_t len, const char *attr,
                            char *out, size_t cap) {
    size_t al = strlen(attr);
    for (size_t i = 0; i + al + 2 < len; i++) {
        if (line[i] != attr[0]) continue;
        if (memcmp(line + i, attr, al) != 0 || line[i + al] != '=') continue;
        if (i > 0 && line[i - 1] != ' ' && line[i - 1] != '\t' &&
            line[i - 1] != '<') continue;
        if (line[i + al + 1] != '"') continue;
        const char *v = line + i + al + 2;
        size_t n = 0;
        while (v + n < line + len && v[n] != '"') n++;
        if (intel_html_decode(v, n, out, cap) == 0 && n > 0) return false;
        return true;
    }
    return false;
}

/* 内部辅助实现 */
static size_t intel_html_decode(const char *v, size_t n, char *out,
                                size_t cap) {
    size_t r = 0, w = 0;
    while (r < n) {
        if (v[r] == '&') {
            const char *semi = memchr(v + r, ';', n - r);
            size_t el = semi ? (size_t)(semi - (v + r)) + 1 : 0;
            if (el >= 4 && el <= 8) {
                char c = 0;
                if (el == 5 && memcmp(v + r, "&amp;", 5) == 0) c = '&';
                else if (el == 4 && memcmp(v + r, "&lt;", 4) == 0) c = '<';
                else if (el == 4 && memcmp(v + r, "&gt;", 4) == 0) c = '>';
                else if (el == 6 && memcmp(v + r, "&quot;", 6) == 0) c = '"';
                else if (el == 5 && memcmp(v + r, "&apos;", 5) == 0) c = '\'';
                else if (v[r + 1] == '#') {
                    int code = 0, ok = 1;
                    for (size_t k = r + 2; k + 1 < r + el; k++) {
                        if (v[k] < '0' || v[k] > '9') { ok = 0; break; }
                        code = code * 10 + (v[k] - '0');
                    }
                    if (ok && code > 0 && code < 128) c = (char)code;
                }
                if (c != 0) {
                    if (w + 1 >= cap) return 0;
                    out[w++] = c;
                    r += el;
                    continue;
                }
            }
        }
        if (w + 1 >= cap) return 0;
        out[w++] = v[r++];
    }
    out[w] = '\0';
    return w;
}

static bool intel_html_has(const char *line, size_t len, const char *needle) {
    size_t nl = strlen(needle);
    if (len < nl) return false;
    for (size_t i = 0; i + nl <= len; i++) {
        if (line[i] == needle[0] && memcmp(line + i, needle, nl) == 0)
            return true;
    }
    return false;
}

/* 模块核心语义抽象与接口调用契约 */
static int intel_html_handlers(const char *line, size_t len,
                               char outs[][128], int max) {
    static const char kOn[] = "data-on-";
    const size_t kl = sizeof(kOn) - 1;
    int n = 0;
    size_t i = 0;
    while (i + kl <= len && n < max) {
        size_t j = i;
        while (j + kl <= len && memcmp(line + j, kOn, kl) != 0) j++;
        if (j + kl > len) break;
        size_t k = j + kl;
        while (k < len && line[k] != '=') k++;
        if (k + 1 >= len || line[k + 1] != '"') { i = j + kl; continue; }
        const char *v = line + k + 2;
        size_t m = 0;
        while (v + m < line + len && v[m] != '"') m++;
        if (m > 0 && m < 128) {
            memcpy(outs[n], v, m);
            outs[n][m] = '\0';
            n++;
        }
        i = (size_t)(v - line) + m + 1;
    }
    return n;
}

static void intel_parse_design_html(intellisense_t *is, const char *filepath,
                                   const char *content, size_t len) {
    /* 语言服务与调试协议交互规范 */
    bool has_marker = false;
    char class_name[128] = {0};
    char submit[128] = {0};
    char form_handlers[8][128];
    int form_handler_count = 0;
    const char *p = content, *end = content + len;
    while (p < end && !has_marker) {
        const char *nl = memchr(p, '\n', (size_t)(end - p));
        size_t ll = (size_t)((nl ? nl : end) - p);
        if (intel_html_has(p, ll, "data-zan-design")) {
            has_marker = true;
            intel_html_attr(p, ll, "id", class_name, sizeof(class_name));
            intel_html_attr(p, ll, "data-submit", submit, sizeof(submit));
            form_handler_count = intel_html_handlers(p, ll, form_handlers, 8);
        }
        if (!nl) break;
        p = nl + 1;
    }
    if (!has_marker || !class_name[0] ||
        !isalpha((unsigned char)class_name[0])) {
        return;
    }

    /* 模块核心语义抽象与接口调用契约 */
    add_symbol_ex(is, class_name, "Form", NULL, NULL, filepath, NULL,
                  ISYM_CLASS, 0, 0, false, 0);
    if (submit[0])
        add_symbol(is, submit, "void", class_name, "void handler()",
                   filepath, ISYM_METHOD, 0, 0);
    for (int h = 0; h < form_handler_count; h++) {
        add_symbol(is, form_handlers[h], "void", class_name, "void handler()",
                   filepath, ISYM_METHOD, 0, 0);
    }

    /* 底层系统交互与数据协议契约 */
    p = content;
    int line = 0;
    while (p < end) {
        const char *nl = memchr(p, '\n', (size_t)(end - p));
        size_t ll = (size_t)((nl ? nl : end) - p);
        char kind[128];
        if (intel_html_attr(p, ll, "data-kind", kind, sizeof(kind)) &&
            kind[0]) {
            char fname[128];
            if (intel_html_attr(p, ll, "id", fname, sizeof(fname)) &&
                fname[0] && isalpha((unsigned char)fname[0])) {
                /* 内部辅助逻辑 */
                const char *wtype = "Panel";
                if (isalpha((unsigned char)kind[0])) {
                    wtype = kind;
                } else if (kind[0] >= '0' && kind[0] <= '9') {
                    wtype = intel_design_widget(atoi(kind));
                }
                add_symbol(is, fname, wtype, class_name, NULL,
                           filepath, ISYM_FIELD, line, 0);
            }
            char hs[8][128];
            int hn = intel_html_handlers(p, ll, hs, 8);
            for (int h = 0; h < hn; h++) {
                add_symbol(is, hs[h], "void", class_name, "void handler()",
                           filepath, ISYM_METHOD, line, 0);
            }
        }
        if (!nl) break;
        p = nl + 1;
        line++;
    }
}

/* 内部辅助实现 */
static void intel_parse_zscene(intellisense_t *is, const char *filepath,
                               const char *content, size_t len) {
    char *text = (char *)malloc(len + 1);
    if (!text) return;
    memcpy(text, content, len);
    text[len] = '\0';

    json_value *root = json_parse(text);
    free(text);
    if (!root || root->type != JSON_OBJ) { json_free(root); return; }

    const char *class_name = json_get_str(json_obj_get(root, "name"));
    if (!class_name || !class_name[0]) { json_free(root); return; }

    add_symbol_ex(is, class_name, "object", NULL, NULL, filepath, NULL,
                  ISYM_CLASS, 0, 0, false, 0);

    /* 底层系统交互与数据协议契约 */
    const char *name_lines[64], *name_vals[64];
    int name_count = intel_doc_name_lines(content, len, name_lines,
                                          name_vals, 64);

    json_value *els = json_obj_get(root, "elements");
    if (els && els->type == JSON_ARR) {
        for (int i = 0; i < els->as.arr.count; i++) {
            json_value *e = els->as.arr.items[i];
            if (!e || e->type != JSON_OBJ) continue;
            const char *ename = json_get_str(json_obj_get(e, "name"));
            if (!ename || !ename[0] || !isalpha((unsigned char)ename[0]))
                continue;
            int eline = 0;
            for (int k = 0; k < name_count; k++) {
                if (name_vals[k] && strcmp(name_vals[k], ename) == 0) {
                    eline = atoi(name_lines[k]);
                    break;
                }
            }
            add_symbol(is, ename, "SceneElement", class_name, NULL,
                       filepath, ISYM_FIELD, eline, 0);
        }
    }

    for (int k = 0; k < name_count; k++) {
        free((void *)name_lines[k]);
        free((void *)name_vals[k]);
    }
    json_free(root);
}

/* 模块核心语义抽象与接口调用契约 */
typedef struct {
    zan_token_t token;
    int mate;
} intel_token_t;

typedef struct {
    intellisense_t *is;
    intellisense_t *project;
    const char *file;
    const char *source;
    size_t len;
    size_t *lines;
    int line_count;
    intel_token_t *tokens;
    int token_count;
    bool declarations_only;
} intel_ast_ctx_t;

typedef struct {
    int start;
    int end;
    int method;
} intel_scope_t;

static int intel_utf16_col(const char *s, size_t len) {
    int col = 0;
    for (size_t i = 0; i < len;) {
        unsigned char c = (unsigned char)s[i];
        size_t n = c < 0x80 ? 1 : (c & 0xe0) == 0xc0 ? 2
                   : (c & 0xf0) == 0xe0 ? 3 : (c & 0xf8) == 0xf0 ? 4 : 1;
        if (i + n > len) n = 1;
        col += n == 4 ? 2 : 1;
        i += n;
    }
    return col;
}

static void intel_ast_position(const intel_ast_ctx_t *c, int offset,
                               int *line, int *col) {
    size_t off = offset < 0 ? 0 : (size_t)offset;
    if (off > c->len) off = c->len;
    int lo = 0, hi = c->line_count;
    while (lo + 1 < hi) {
        int mid = lo + (hi - lo) / 2;
        if (c->lines[mid] <= off) lo = mid; else hi = mid;
    }
    *line = lo;
    *col = intel_utf16_col(c->source + c->lines[lo], off - c->lines[lo]);
    if (offset > 0 && (size_t)offset > c->len) (*col)++; /* 核心系统底层抽象与内存语义契约 */
}

static int intel_ast_token_at(const intel_ast_ctx_t *c, int offset) {
    int lo = 0, hi = c->token_count;
    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;
        if (c->tokens[mid].token.loc.offset < (uint32_t)offset) lo = mid + 1;
        else hi = mid;
    }
    return lo;
}

static void intel_ast_append(char *out, size_t cap, const char *text) {
    size_t used = strlen(out);
    if (used < cap) snprintf(out + used, cap - used, "%s", text);
}

static void intel_ast_text(char *out, size_t cap, zan_istr_t text) {
    snprintf(out, cap, "%.*s", (int)text.len, text.str ? text.str : "");
}

static void intel_ast_type(const zan_ast_node_t *n, char *out, size_t cap) {
    out[0] = '\0';
    if (!n) { snprintf(out, cap, "var"); return; }
    if (n->kind == AST_TYPE_REF) {
        if (n->type_ref.array_element) {
            const zan_ast_node_t *element = n;
            while (element->kind == AST_TYPE_REF && element->type_ref.array_element)
                element = element->type_ref.array_element;
            intel_ast_type(element, out, cap);
            /* 内部辅助逻辑 */
            for (element = n; element->kind == AST_TYPE_REF && element->type_ref.array_element;
                 element = element->type_ref.array_element) {
                intel_ast_append(out, cap, "[");
                for (int i = 1; i < element->type_ref.array_rank; i++) intel_ast_append(out, cap, ",");
                intel_ast_append(out, cap, "]");
            }
            return;
        }
        intel_ast_text(out, cap, n->type_ref.name);
        if (n->type_ref.type_args.count) {
            intel_ast_append(out, cap, "<");
            for (int i = 0; i < n->type_ref.type_args.count; i++) {
                char arg[128];
                intel_ast_type(n->type_ref.type_args.items[i], arg, sizeof(arg));
                if (i) intel_ast_append(out, cap, ",");
                intel_ast_append(out, cap, arg);
            }
            intel_ast_append(out, cap, ">");
        }
        if (n->type_ref.is_nullable) intel_ast_append(out, cap, "?");
        if (n->type_ref.is_array) {
            intel_ast_append(out, cap, "[");
            for (int i = 1; i < n->type_ref.array_rank; i++) intel_ast_append(out, cap, ",");
            intel_ast_append(out, cap, "]");
        }
    } else if (n->kind == AST_QUALIFIED_NAME) {
        for (int i = 0; i < n->qualified_name.parts.count; i++) {
            char part[128];
            intel_ast_type(n->qualified_name.parts.items[i], part, sizeof(part));
            if (i) intel_ast_append(out, cap, ".");
            intel_ast_append(out, cap, part);
        }
    } else if (n->kind == AST_IDENTIFIER) {
        intel_ast_text(out, cap, n->ident.name);
    } else if (n->kind == AST_TUPLE_TYPE) {
        intel_ast_append(out, cap, "(");
        for (int i = 0; i < n->tuple_type.elems.count; i++) {
            char part[128];
            intel_ast_type(n->tuple_type.elems.items[i], part, sizeof(part));
            if (i) intel_ast_append(out, cap, ",");
            intel_ast_append(out, cap, part);
        }
        intel_ast_append(out, cap, ")");
    }
}

/* 内部辅助逻辑 */
static int intel_ast_type_end(const intel_ast_ctx_t *c, const zan_ast_node_t *n) {
    if (!n) return 0;
    int i = intel_ast_token_at(c, (int)n->loc.offset);
    if (i >= c->token_count) return i;
    if (n->kind == AST_TUPLE_TYPE && c->tokens[i].mate >= 0)
        return c->tokens[i].mate + 1;
    if (n->kind != AST_TYPE_REF) return i + 1;
    if (n->type_ref.array_element) i = intel_ast_type_end(c, n->type_ref.array_element);
    else {
        i++;
        while (i + 1 < c->token_count && c->tokens[i].token.kind == TK_DOT) i += 2;
    }
    if (n->type_ref.type_args.count) {
        int last = intel_ast_type_end(c, n->type_ref.type_args.items[n->type_ref.type_args.count - 1]);
        if (last > i) i = last;
        while (i < c->token_count &&
               (c->tokens[i].token.kind == TK_GREATER || c->tokens[i].token.kind == TK_GREATER_GREATER ||
                c->tokens[i].token.kind == TK_GREATER_GREATER_GREATER)) i++;
    }
    while (i < c->token_count) {
        if (c->tokens[i].token.kind == TK_QUESTION) i++;
        else if (c->tokens[i].token.kind == TK_LBRACKET && c->tokens[i].mate >= i)
            i = c->tokens[i].mate + 1;
        else break;
    }
    return i;
}

static int intel_ast_name_token(const intel_ast_ctx_t *c, const zan_ast_node_t *n,
                                 zan_istr_t name, const zan_ast_node_t *type) {
    if (!name.str || !name.len) return -1;
    int i = intel_ast_token_at(c, (int)n->loc.offset);
    int floor = intel_ast_type_end(c, type);
    if (floor > i) i = floor;
    for (; i < c->token_count; i++) {
        const zan_token_t *t = &c->tokens[i].token;
        if (t->kind == TK_IDENT && t->str_val.len == name.len &&
            memcmp(t->str_val.str, name.str, name.len) == 0) return i;
        /* 模块核心语义抽象与接口调用契约 */
        if (t->kind == TK_EQ && (n->kind == AST_VAR_DECL || n->kind == AST_FIELD_DECL)) {
            for (i++; i < c->token_count; i++) {
                zan_token_kind_t k = c->tokens[i].token.kind;
                if (k == TK_COMMA) break;
                if (k == TK_SEMICOLON || k == TK_RBRACE || k == TK_EOF) return -1;
                if (c->tokens[i].mate > i) i = c->tokens[i].mate;
            }
            continue;
        }
        if (t->kind == TK_SEMICOLON || t->kind == TK_LBRACE ||
            t->kind == TK_RBRACE || t->kind == TK_EQ) break;
    }
    return -1; /* 底层系统交互与数据协议契约 */
}

static int intel_ast_end(const intel_ast_ctx_t *c, const zan_ast_node_t *n, int limit) {
    if (!n) return limit;
    switch (n->kind) {
    case AST_BLOCK: {
        int token = intel_ast_token_at(c, (int)n->loc.offset);
        if (token < c->token_count && c->tokens[token].token.kind != TK_LBRACE && n->block.stmts.count)
            return intel_ast_end(c, n->block.stmts.items[n->block.stmts.count - 1], limit);
        break;
    }
    case AST_IF_STMT:
        return intel_ast_end(c, n->if_stmt.else_body ? n->if_stmt.else_body : n->if_stmt.then_body, limit);
    case AST_WHILE_STMT: case AST_DO_WHILE_STMT:
        return intel_ast_end(c, n->while_stmt.body, limit);
    case AST_FOR_STMT: return intel_ast_end(c, n->for_stmt.body, limit);
    case AST_FOREACH_STMT: return intel_ast_end(c, n->foreach_stmt.body, limit);
    case AST_LOCK_STMT: return intel_ast_end(c, n->lock_stmt.body, limit);
    case AST_CHECKED_STMT: return intel_ast_end(c, n->checked_stmt.body, limit);
    default: break;
    }
    int i = intel_ast_token_at(c, (int)n->loc.offset);
    if (i < c->token_count && c->tokens[i].token.kind == TK_LBRACE)
        return c->tokens[i].mate >= i ? (int)c->tokens[c->tokens[i].mate].token.loc.offset + 1 : limit;
    /* 模块核心语义抽象与接口调用契约 */
    for (; i < c->token_count && (int)c->tokens[i].token.loc.offset < limit; i++) {
        zan_token_kind_t kind = c->tokens[i].token.kind;
        if (kind == TK_SEMICOLON) return (int)c->tokens[i].token.loc.offset + 1;
        if (kind == TK_RBRACE) return (int)c->tokens[i].token.loc.offset;
        if (c->tokens[i].mate > i) i = c->tokens[i].mate;
    }
    return limit;
}

static void intel_ast_scope(const intel_ast_ctx_t *c, isym_t *s, intel_scope_t scope) {
    s->method_offset = scope.method;
    s->scope_start_offset = scope.start;
    s->scope_end_offset = scope.end;
    intel_ast_position(c, scope.start, &s->scope_start_line, &s->scope_start_col);
    intel_ast_position(c, scope.end, &s->scope_end_line, &s->scope_end_col);
}

/* 内部辅助逻辑 */
static void utf8_trim_end(char *s) {
    size_t n = strlen(s);
    size_t i = n;
    while (i > 0 && ((unsigned char)s[i - 1] & 0xC0) == 0x80) i--;
    if (i > 0 && (unsigned char)s[i - 1] >= 0x80) {
        unsigned char b = (unsigned char)s[i - 1];
        size_t need = b >= 0xF0 ? 4 : (b >= 0xE0 ? 3 : 2);
        if (n - (i - 1) < need) i--; /* 底层系统交互与数据协议契约 */
        else i = n;                  /* 核心系统底层抽象与内存语义契约 */
    }
    if (i < n) s[i] = '\0';
}

static void intel_ast_doc(const intel_ast_ctx_t *c, const zan_ast_node_t *n, char *out, size_t cap) {
    out[0] = '\0';
    int line = (int)n->loc.line - 1;
    while (line > 0) {
        size_t a = c->lines[line - 1], b = c->lines[line];
        while (a < b && (c->source[a] == ' ' || c->source[a] == '\t')) a++;
        if (b - a < 3 || memcmp(c->source + a, "///", 3) != 0) break;
        a += 3;
        while (a < b && c->source[a] == ' ') a++;
        while (b > a && (c->source[b - 1] == '\n' || c->source[b - 1] == '\r')) b--;
        char previous[256];
        snprintf(previous, sizeof(previous), "%s", out);
        snprintf(out, cap, "%.*s%s%s", (int)(b - a), c->source + a,
                 previous[0] ? "\n" : "", previous);
        utf8_trim_end(out);
        line--;
    }
}

static isym_t *intel_ast_symbol(intel_ast_ctx_t *c, const zan_ast_node_t *n,
                                zan_istr_t name, const zan_ast_node_t *decl_type,
                                const char *type, const char *parent,
                                const char *signature, isym_kind_t kind,
                                uint32_t mods, int params, intel_scope_t scope) {
    int token = intel_ast_name_token(c, n, name, decl_type);
    if (token < 0) return NULL;
    char text[128], doc[256];
    intel_ast_text(text, sizeof(text), name);
    intel_ast_doc(c, n, doc, sizeof(doc));
    int offset = (int)c->tokens[token].token.loc.offset, line, col;
    intel_ast_position(c, offset, &line, &col);
    int before = c->is->symbol_count;
    add_symbol_ex(c->is, text, type, parent, signature, c->file, doc,
                  kind, line, col, (mods & MOD_STATIC) != 0, params);
    if (c->is->symbol_count == before) return NULL;
    isym_t *s = &c->is->symbols[before];
    s->offset = offset;
    s->visibility = mods & MOD_PRIVATE ? IVIS_PRIVATE : mods & MOD_PROTECTED ? IVIS_PROTECTED : IVIS_PUBLIC;
    intel_ast_scope(c, s, scope);
    return s;
}

typedef struct {
    const char *text;
    size_t len;
} intel_type_arg_t;

static intel_type_arg_t intel_generic_arg(const char *type, int index);

static const char *intel_array_suffix(const char *type) {
    int depth = 0;
    for (const char *p = type; *p; p++) {
        if (*p == '<') depth++;
        else if (*p == '>') depth--;
        else if (*p == '[' && !depth) return p;
    }
    return NULL;
}

static void intel_ast_element_type(const char *collection, bool indexing, char *out, size_t cap) {
    snprintf(out, cap, "var");
    const char *rank = intel_array_suffix(collection);
    if (rank) {
        const char *end = strchr(rank, ']');
        if (end) snprintf(out, cap, "%.*s%s", (int)(rank - collection), collection, end + 1);
        return;
    }
    const zan_builtin_type_t *bt = builtin_receiver(collection);
    if (bt && (strcmp(bt->type, "List") == 0 || (indexing && strcmp(bt->type, "Dict") == 0))) {
        intel_type_arg_t element = intel_generic_arg(collection, strcmp(bt->type, "Dict") == 0 ? 1 : 0);
        if (element.text) snprintf(out, cap, "%.*s", (int)element.len, element.text);
    } else if (strcmp(collection, "string") == 0) snprintf(out, cap, "char");
}

static void intel_ast_infer(intel_ast_ctx_t *c, const zan_ast_node_t *n,
                             const char *parent, char *out, size_t cap) {
    snprintf(out, cap, "var");
    if (!n) return;
    switch (n->kind) {
    case AST_NEW_EXPR:
        intel_ast_type(n->new_expr.type, out, cap);
        if (n->new_expr.is_array && n->new_expr.type &&
            !(n->new_expr.type->kind == AST_TYPE_REF && n->new_expr.type->type_ref.is_array))
            intel_ast_append(out, cap, "[]");
        break;
    case AST_CAST_EXPR: intel_ast_type(n->cast.type, out, cap); break;
    case AST_AS_EXPR: intel_ast_type(n->type_test.type, out, cap); break;
    case AST_STRING_LITERAL: case AST_STRING_INTERP: snprintf(out, cap, "string"); break;
    case AST_CHAR_LITERAL: snprintf(out, cap, "char"); break;
    case AST_BOOL_LITERAL: snprintf(out, cap, "bool"); break;
    case AST_INT_LITERAL:
        snprintf(out, cap, "%s", n->lit_suffix == 3 ? "ulong" : n->lit_suffix == 2 ? "uint"
                 : n->lit_suffix == 1 || (n->int_val > INT32_MAX &&
                   (n->lit_radix == 10 || n->int_val > UINT32_MAX)) ? "long" : "int"); break;
    case AST_FLOAT_LITERAL: snprintf(out, cap, "double"); break;
    case AST_UNARY: case AST_POSTFIX_UNARY:
        if (n->unary.op == TK_BANG) snprintf(out, cap, "bool");
        else intel_ast_infer(c, n->unary.operand, parent, out, cap);
        break;
    case AST_IDENTIFIER: {
        char name[128]; int line, col;
        intel_ast_text(name, sizeof(name), n->ident.name);
        intel_ast_position(c, (int)n->loc.offset, &line, &col);
        const char *type = intel_resolve_type_pos(c->is, name, line, col);
        if (type) snprintf(out, cap, "%s", type);
        else if (n->ident.inst_type_ref) intel_ast_type(n->ident.inst_type_ref, out, cap);
        else {
            intellisense_t *indexes[] = {c->is, c->project};
            if (builtin_receiver(name)) snprintf(out, cap, "%s", name);
            for (int ix = 0; ix < 2; ix++) {
                if (!indexes[ix]) continue;
                for (int i = 0; i < indexes[ix]->symbol_count; i++) {
                    const isym_t *s = &indexes[ix]->symbols[i];
                    if ((s->kind == ISYM_CLASS || s->kind == ISYM_STRUCT || s->kind == ISYM_ENUM ||
                         s->kind == ISYM_INTERFACE) && strcmp(s->name, name) == 0) {
                        snprintf(out, cap, "%s", name);
                        break;
                    }
                }
            }
        }
        break;
    }
    case AST_THIS_EXPR: snprintf(out, cap, "%s", parent); break;
    case AST_MEMBER_ACCESS: {
        char receiver[128], member[128];
        intel_ast_infer(c, n->member.object, parent, receiver, sizeof(receiver));
        intel_ast_text(member, sizeof(member), n->member.name);
        const char *type = intel_resolve_method_return_ex(c->is, c->project, receiver, member);
        if (type) snprintf(out, cap, "%s", type);
        break;
    }
    case AST_CALL:
        if (n->call.callee && n->call.callee->kind == AST_IDENTIFIER) {
            char name[128];
            intel_ast_text(name, sizeof(name), n->call.callee->ident.name);
            const char *type = intel_resolve_method_return_ex(c->is, c->project, parent, name);
            if (type) snprintf(out, cap, "%s", type);
        } else intel_ast_infer(c, n->call.callee, parent, out, cap);
        break;
    case AST_INDEX: {
        char receiver[128];
        intel_ast_infer(c, n->index.object, parent, receiver, sizeof(receiver));
        intel_ast_element_type(receiver, true, out, cap);
        break;
    }
    case AST_CONDITIONAL: {
        char other[128];
        intel_ast_infer(c, n->conditional.then_expr, parent, out, cap);
        intel_ast_infer(c, n->conditional.else_expr, parent, other, sizeof(other));
        if (strcmp(out, other) != 0) snprintf(out, cap, "var");
        break;
    }
    case AST_ASSIGNMENT: intel_ast_infer(c, n->binary.left, parent, out, cap); break;
    case AST_BINARY: {
        zan_token_kind_t op = n->binary.op;
        if (op == TK_EQ_EQ || op == TK_BANG_EQ || op == TK_LESS || op == TK_LESS_EQ ||
            op == TK_GREATER || op == TK_GREATER_EQ || op == TK_AMP_AMP || op == TK_PIPE_PIPE) {
            snprintf(out, cap, "bool");
        } else {
            char other[128];
            intel_ast_infer(c, n->binary.left, parent, out, cap);
            intel_ast_infer(c, n->binary.right, parent, other, sizeof(other));
            if (strcmp(out, other) != 0) snprintf(out, cap, "var");
        }
        break;
    }
    case AST_AWAIT_EXPR: {
        char task[128];
        intel_ast_infer(c, n->await_expr.expr, parent, task, sizeof(task));
        if (strncmp(task, "Task<", 5) == 0 || strncmp(task, "ValueTask<", 10) == 0) {
            intel_type_arg_t result = intel_generic_arg(task, 0);
            if (result.text) snprintf(out, cap, "%.*s", (int)result.len, result.text);
        }
        break;
    }
    case AST_NAMED_ARG: intel_ast_infer(c, n->named_arg.expr, parent, out, cap); break;
    default: break;
    }
}

static void intel_ast_walk(intel_ast_ctx_t *c, const zan_ast_node_t *n,
                           const char *parent, intel_scope_t scope);

static void intel_ast_walk_list(intel_ast_ctx_t *c, const zan_ast_list_t *list,
                                const char *parent, intel_scope_t scope) {
    for (int i = 0; i < list->count; i++) intel_ast_walk(c, list->items[i], parent, scope);
}

static void intel_ast_embedded(intel_ast_ctx_t *c, const zan_ast_node_t *body,
                                const char *parent, intel_scope_t scope) {
    if (!body) return;
    scope.start = (int)body->loc.offset;
    scope.end = intel_ast_end(c, body, scope.end);
    intel_ast_walk(c, body, parent, scope);
}

static intel_scope_t intel_ast_body(intel_ast_ctx_t *c, const zan_ast_node_t *body,
                                     const char *parent, const char *name,
                                     int decl_offset, intel_scope_t scope) {
    intel_scope_t extent = {(int)body->loc.offset, intel_ast_end(c, body, scope.end), decl_offset};
    if (reserve_methods(c->is, c->is->method_count + 1)) {
        imethod_t *m = &c->is->methods[c->is->method_count++];
        memset(m, 0, sizeof(*m));
        snprintf(m->name, sizeof(m->name), "%s", name);
        snprintf(m->parent, sizeof(m->parent), "%s", parent);
        snprintf(m->file, sizeof(m->file), "%s", c->file);
        m->decl_offset = extent.method;
        m->start_offset = extent.start; m->end_offset = extent.end;
        intel_ast_position(c, extent.start, &m->start_line, &m->start_col);
        intel_ast_position(c, extent.end, &m->end_line, &m->end_col);
    }
    return extent;
}

static void intel_ast_callable(intel_ast_ctx_t *c, const zan_ast_node_t *n,
                               const char *parent, intel_scope_t scope) {
    char name[128], type[128], sig[256], ptype[128], pname[128];
    intel_ast_text(name, sizeof(name), n->method_decl.name);
    intel_ast_type(n->method_decl.return_type, type, sizeof(type));
    if (!n->method_decl.return_type) snprintf(type, sizeof(type), "void");
    bool ctor = n->kind == AST_CONSTRUCTOR_DECL;
    if (ctor) snprintf(type, sizeof(type), "%s", parent);
    uint32_t mods = n->method_decl.modifiers;
    snprintf(sig, sizeof(sig), "%s%s%s%s%s%s(", mods & MOD_STATIC ? "static " : "",
             mods & MOD_ASYNC ? "async " : "", ctor ? "" : type, ctor ? "" : " ",
             parent && parent[0] ? parent : "", parent && parent[0] ? "." : "");
    /* 模块核心语义抽象与接口调用契约 */
    size_t sl = strlen(sig);
    if (sl) sig[sl - 1] = '\0';
    intel_ast_append(sig, sizeof(sig), name);
    intel_ast_append(sig, sizeof(sig), "(");
    for (int i = 0; i < n->method_decl.params.count; i++) {
        const zan_ast_node_t *p = n->method_decl.params.items[i];
        intel_ast_type(p->param.type, ptype, sizeof(ptype));
        intel_ast_text(pname, sizeof(pname), p->param.name);
        if (i) intel_ast_append(sig, sizeof(sig), ", ");
        intel_ast_append(sig, sizeof(sig), p->param.by_ref == 1 ? "ref " : p->param.by_ref == 2 ? "out " : p->param.is_params ? "params " : "");
        intel_ast_append(sig, sizeof(sig), ptype);
        intel_ast_append(sig, sizeof(sig), " ");
        intel_ast_append(sig, sizeof(sig), pname);
    }
    intel_ast_append(sig, sizeof(sig), ")");
    const zan_ast_node_t *floor = ctor ? NULL : n->method_decl.return_type;
    if (c->declarations_only) {
        intel_ast_symbol(c, n, n->method_decl.name, floor, type, parent, sig,
                         ctor ? ISYM_CONSTRUCTOR : n->kind == AST_DELEGATE_DECL ? ISYM_TYPE : ISYM_METHOD,
                         mods, n->method_decl.params.count, scope);
        return;
    }
    /* 内部辅助逻辑 */
    if (intel_ast_name_token(c, n, n->method_decl.name, floor) < 0) return;
    intel_scope_t body = intel_ast_body(c, n->method_decl.body ? n->method_decl.body : n,
                                        parent, name, (int)n->loc.offset, scope);
    for (int i = 0; i < n->method_decl.params.count; i++) {
        const zan_ast_node_t *p = n->method_decl.params.items[i];
        intel_ast_type(p->param.type, ptype, sizeof(ptype));
        intel_ast_symbol(c, p, p->param.name, p->param.type, ptype, parent, NULL,
                         ISYM_PARAMETER, 0, 0, body);
    }
    intel_ast_walk(c, n->method_decl.body, parent, body);
}

static void intel_ast_accessor(intel_ast_ctx_t *c, const zan_ast_node_t *property,
                               const zan_ast_node_t *body, const char *parent,
                               intel_scope_t scope, bool setter) {
    if (!body || c->declarations_only) return;
    char name[128];
    intel_ast_text(name, sizeof(name), property->field_decl.name);
    intel_scope_t accessor = intel_ast_body(c, body, parent, name, (int)body->loc.offset, scope);
    if (property->field_decl.indexer_params) {
        for (int i = 0; i < property->field_decl.indexer_params->count; i++) {
            const zan_ast_node_t *p = property->field_decl.indexer_params->items[i];
            char type[128];
            intel_ast_type(p->param.type, type, sizeof(type));
            intel_ast_symbol(c, p, p->param.name, p->param.type, type, parent, NULL,
                             ISYM_PARAMETER, 0, 0, accessor);
        }
    }
    if (setter) {
        char type[128];
        intel_ast_type(property->field_decl.type, type, sizeof(type));
        int before = c->is->symbol_count;
        add_symbol(c->is, "value", type, parent, NULL, c->file, ISYM_PARAMETER,
                   (int)body->loc.line - 1, 0);
        if (before < c->is->symbol_count) intel_ast_scope(c, &c->is->symbols[before], accessor);
    }
    intel_ast_walk(c, body, parent, accessor);
}

static void intel_ast_walk(intel_ast_ctx_t *c, const zan_ast_node_t *n,
                           const char *parent, intel_scope_t scope) {
    if (!n) return;
    char name[128], type[128];
    switch (n->kind) {
    case AST_COMPILATION_UNIT: {
        char ns[128] = {0};
        if (n->comp_unit.ns) {
            const zan_ast_node_t *decl = n->comp_unit.ns;
            intel_ast_type(decl->namespace_decl.name, ns, sizeof(ns));
            if (c->declarations_only) {
                int line, col;
                intel_ast_position(c, (int)decl->namespace_decl.name->loc.offset, &line, &col);
                add_symbol(c->is, ns, NULL, NULL, NULL, c->file, ISYM_NAMESPACE, line, col);
            }
        }
        intel_ast_walk_list(c, &n->comp_unit.decls, ns, scope);
        break;
    }
    case AST_CLASS_DECL: case AST_STRUCT_DECL: case AST_INTERFACE_DECL: case AST_ENUM_DECL: {
        intel_ast_text(name, sizeof(name), n->type_decl.name);
        type[0] = '\0';
        if (n->type_decl.bases.count) intel_ast_type(n->type_decl.bases.items[0], type, sizeof(type));
        int begin = intel_ast_token_at(c, (int)n->loc.offset), end = scope.end;
        for (int i = begin; i < c->token_count; i++) {
            if (c->tokens[i].token.kind == TK_LBRACE) {
                if (c->tokens[i].mate >= i) end = (int)c->tokens[c->tokens[i].mate].token.loc.offset + 1;
                break;
            }
        }
        intel_scope_t type_scope = {(int)n->loc.offset, end, -1};
        if (c->declarations_only) {
            char declaration[256];
            snprintf(declaration, sizeof(declaration), "%s", name);
            if (n->type_decl.type_params.count) intel_ast_append(declaration, sizeof(declaration), "<");
            for (int i = 0; i < n->type_decl.type_params.count; i++) {
                char param[128];
                intel_ast_type(n->type_decl.type_params.items[i], param, sizeof(param));
                if (i) intel_ast_append(declaration, sizeof(declaration), ",");
                intel_ast_append(declaration, sizeof(declaration), param);
            }
            if (n->type_decl.type_params.count) intel_ast_append(declaration, sizeof(declaration), ">");
            intel_ast_symbol(c, n, n->type_decl.name, NULL, type, parent, declaration,
                             n->kind == AST_CLASS_DECL ? ISYM_CLASS : n->kind == AST_STRUCT_DECL ? ISYM_STRUCT
                             : n->kind == AST_ENUM_DECL ? ISYM_ENUM : ISYM_INTERFACE,
                             n->type_decl.modifiers, 0, type_scope);
        }
        intel_ast_walk_list(c, &n->type_decl.members, name, type_scope);
        break;
    }
    case AST_METHOD_DECL: case AST_CONSTRUCTOR_DECL: case AST_DESTRUCTOR_DECL: case AST_DELEGATE_DECL:
        intel_ast_callable(c, n, parent, scope); break;
    case AST_FIELD_DECL: case AST_PROPERTY_DECL:
        intel_ast_type(n->field_decl.type, type, sizeof(type));
        if (c->declarations_only) {
            intel_ast_symbol(c, n, n->field_decl.name, n->field_decl.type, type, parent, NULL,
                             n->kind == AST_PROPERTY_DECL ? ISYM_PROPERTY : n->field_decl.modifiers & MOD_EVENT ? ISYM_EVENT : ISYM_FIELD,
                             n->field_decl.modifiers, 0, scope);
        } else {
            intel_ast_walk(c, n->field_decl.initializer, parent, scope);
            intel_ast_accessor(c, n, n->field_decl.getter_body, parent, scope, false);
            intel_ast_accessor(c, n, n->field_decl.setter_body, parent, scope, true);
        }
        break;
    case AST_ENUM_MEMBER:
        if (c->declarations_only)
            intel_ast_symbol(c, n, n->enum_member.name, NULL, parent, parent, NULL, ISYM_ENUM_MEMBER, MOD_STATIC, 0, scope);
        break;
    case AST_BLOCK: {
        intel_scope_t block = scope;
        int token = intel_ast_token_at(c, (int)n->loc.offset);
        if (token < c->token_count && c->tokens[token].token.kind == TK_LBRACE) {
            block.start = (int)n->loc.offset;
            block.end = intel_ast_end(c, n, scope.end);
        }
        intel_ast_walk_list(c, &n->block.stmts, parent, block);
        break;
    }
    case AST_VAR_DECL:
        intel_ast_type(n->var_decl.type, type, sizeof(type));
        if (!n->var_decl.type || strcmp(type, "var") == 0) intel_ast_infer(c, n->var_decl.initializer, parent, type, sizeof(type));
        intel_ast_symbol(c, n, n->var_decl.name, n->var_decl.type, type, parent, NULL, ISYM_VARIABLE, 0, 0, scope);
        intel_ast_walk(c, n->var_decl.initializer, parent, scope);
        break;
    case AST_IF_STMT:
        intel_ast_walk(c, n->if_stmt.cond, parent, scope);
        intel_ast_embedded(c, n->if_stmt.then_body, parent, scope);
        intel_ast_embedded(c, n->if_stmt.else_body, parent, scope); break;
    case AST_WHILE_STMT: case AST_DO_WHILE_STMT:
        intel_ast_walk(c, n->while_stmt.cond, parent, scope);
        intel_ast_embedded(c, n->while_stmt.body, parent, scope); break;
    case AST_FOR_STMT: {
        intel_scope_t loop = {(int)n->loc.offset, intel_ast_end(c, n, scope.end), scope.method};
        intel_ast_walk(c, n->for_stmt.init, parent, loop);
        intel_ast_walk(c, n->for_stmt.cond, parent, loop);
        intel_ast_walk(c, n->for_stmt.step, parent, loop);
        intel_ast_embedded(c, n->for_stmt.body, parent, loop); break;
    }
    case AST_FOREACH_STMT: {
        if (!n->foreach_stmt.body) break;
        intel_ast_walk(c, n->foreach_stmt.collection, parent, scope);
        intel_scope_t loop = {(int)n->foreach_stmt.body->loc.offset, intel_ast_end(c, n, scope.end), scope.method};
        intel_ast_type(n->foreach_stmt.var_type, type, sizeof(type));
        if (!n->foreach_stmt.var_type || strcmp(type, "var") == 0) {
            char collection[128];
            intel_ast_infer(c, n->foreach_stmt.collection, parent, collection, sizeof(collection));
            intel_ast_element_type(collection, false, type, sizeof(type));
        }
        intel_ast_symbol(c, n, n->foreach_stmt.var_name, n->foreach_stmt.var_type, type, parent, NULL, ISYM_VARIABLE, 0, 0, loop);
        intel_ast_walk(c, n->foreach_stmt.body, parent, loop); break;
    }
    case AST_TRY_STMT:
        intel_ast_walk(c, n->try_stmt.try_body, parent, scope);
        intel_ast_walk_list(c, &n->try_stmt.catches, parent, scope);
        intel_ast_walk(c, n->try_stmt.finally_body, parent, scope); break;
    case AST_CATCH_CLAUSE: {
        if (!n->catch_clause.body) break;
        intel_scope_t caught = {(int)n->catch_clause.body->loc.offset, intel_ast_end(c, n->catch_clause.body, scope.end), scope.method};
        intel_ast_type(n->catch_clause.type, type, sizeof(type));
        intel_ast_symbol(c, n, n->catch_clause.var_name, n->catch_clause.type, type, parent, NULL, ISYM_VARIABLE, 0, 0, caught);
        intel_ast_walk(c, n->catch_clause.body, parent, caught); break;
    }
    case AST_SWITCH_STMT:
        intel_ast_walk(c, n->switch_stmt.expr, parent, scope);
        intel_ast_walk_list(c, &n->switch_stmt.cases, parent, scope); break;
    case AST_SWITCH_CASE: {
        intel_scope_t branch = {(int)n->loc.offset, intel_ast_end(c, n->switch_case.body, scope.end), scope.method};
        if (n->switch_case.type_pattern) {
            intel_ast_type(n->switch_case.type_pattern, type, sizeof(type));
            intel_ast_symbol(c, n, n->switch_case.var_name, n->switch_case.type_pattern,
                             type, parent, NULL, ISYM_VARIABLE, 0, 0, branch);
        }
        intel_ast_walk(c, n->switch_case.pattern, parent, branch);
        intel_ast_walk(c, n->switch_case.when_cond, parent, branch);
        intel_ast_walk(c, n->switch_case.body, parent, branch); break;
    }
    case AST_LOCK_STMT:
        intel_ast_walk(c, n->lock_stmt.expr, parent, scope);
        intel_ast_embedded(c, n->lock_stmt.body, parent, scope); break;
    case AST_CHECKED_STMT: intel_ast_walk(c, n->checked_stmt.body, parent, scope); break;
    case AST_EXPR_STMT: intel_ast_walk(c, n->expr_stmt.expr, parent, scope); break;
    case AST_RETURN_STMT: intel_ast_walk(c, n->ret.value, parent, scope); break;
    case AST_CALL:
        intel_ast_walk(c, n->call.callee, parent, scope);
        intel_ast_walk_list(c, &n->call.args, parent, scope); break;
    case AST_ASSIGNMENT: case AST_BINARY:
        intel_ast_walk(c, n->binary.left, parent, scope);
        intel_ast_walk(c, n->binary.right, parent, scope); break;
    case AST_NEW_EXPR:
        intel_ast_walk(c, n->new_expr.call_init, parent, scope);
        intel_ast_walk_list(c, &n->new_expr.args, parent, scope);
        intel_ast_walk_list(c, &n->new_expr.arg_inits, parent, scope); break;
    case AST_MEMBER_ACCESS: intel_ast_walk(c, n->member.object, parent, scope); break;
    case AST_INDEX:
        intel_ast_walk(c, n->index.object, parent, scope);
        intel_ast_walk(c, n->index.index, parent, scope);
        intel_ast_walk_list(c, &n->index.extra, parent, scope); break;
    case AST_UNARY: case AST_POSTFIX_UNARY: intel_ast_walk(c, n->unary.operand, parent, scope); break;
    case AST_CAST_EXPR: intel_ast_walk(c, n->cast.expr, parent, scope); break;
    case AST_IS_EXPR: case AST_AS_EXPR: intel_ast_walk(c, n->type_test.expr, parent, scope); break;
    case AST_CONDITIONAL:
        intel_ast_walk(c, n->conditional.cond, parent, scope);
        intel_ast_walk(c, n->conditional.then_expr, parent, scope);
        intel_ast_walk(c, n->conditional.else_expr, parent, scope); break;
    case AST_AWAIT_EXPR: intel_ast_walk(c, n->await_expr.expr, parent, scope); break;
    case AST_THROW_STMT: intel_ast_walk(c, n->throw_stmt.value, parent, scope); break;
    case AST_YIELD_STMT: intel_ast_walk(c, n->yield_stmt.value, parent, scope); break;
    case AST_NAMED_ARG: intel_ast_walk(c, n->named_arg.expr, parent, scope); break;
    case AST_REF_ARG:
        if (n->ref_arg.decl_type && n->ref_arg.expr && n->ref_arg.expr->kind == AST_IDENTIFIER) {
            intel_ast_type(n->ref_arg.decl_type, type, sizeof(type));
            intel_ast_symbol(c, n->ref_arg.expr, n->ref_arg.expr->ident.name, NULL,
                             type, parent, NULL, ISYM_VARIABLE, 0, 0, scope);
        }
        intel_ast_walk(c, n->ref_arg.expr, parent, scope); break;
    case AST_COLL_INIT: intel_ast_walk_list(c, &n->coll_init.items, parent, scope); break;
    case AST_STRING_INTERP: intel_ast_walk_list(c, &n->string_interp.parts, parent, scope); break;
    case AST_TUPLE_EXPR: intel_ast_walk_list(c, &n->tuple_expr.items, parent, scope); break;
    case AST_TUPLE_DECON:
        for (int i = 0; i < n->tuple_decon.names.count; i++) {
            const zan_ast_node_t *variable = n->tuple_decon.names.items[i];
            if (!variable || variable->kind != AST_IDENTIFIER ||
                (variable->ident.name.len == 1 && variable->ident.name.str[0] == '_')) continue;
            const zan_ast_node_t *decl_type = i < n->tuple_decon.types.count ? n->tuple_decon.types.items[i] : NULL;
            intel_ast_type(decl_type, type, sizeof(type));
            const zan_ast_node_t *init = n->tuple_decon.initializer;
            if ((!decl_type || strcmp(type, "var") == 0) && init && init->kind == AST_TUPLE_EXPR && i < init->tuple_expr.items.count)
                intel_ast_infer(c, (const zan_ast_node_t *)init->tuple_expr.items.items[i], parent, type, sizeof(type));
            intel_ast_symbol(c, variable, variable->ident.name, NULL, type, parent, NULL, ISYM_VARIABLE, 0, 0, scope);
        }
        intel_ast_walk(c, n->tuple_decon.initializer, parent, scope); break;
    case AST_SWITCH_EXPR:
        intel_ast_walk(c, n->switch_expr.expr, parent, scope);
        intel_ast_walk_list(c, &n->switch_expr.arms, parent, scope); break;
    case AST_SWITCH_ARM:
        intel_ast_walk(c, n->switch_arm.pattern, parent, scope);
        intel_ast_walk(c, n->switch_arm.when_cond, parent, scope);
        intel_ast_walk(c, n->switch_arm.result, parent, scope); break;
    case AST_WITH_EXPR:
        intel_ast_walk(c, n->with_expr.expr, parent, scope);
        intel_ast_walk_list(c, &n->with_expr.assigns, parent, scope); break;
    case AST_QUERY_EXPR:
        intel_ast_walk(c, n->query.source, parent, scope);
        intel_ast_walk_list(c, &n->query.clauses, parent, scope);
        intel_ast_walk(c, n->query.group_expr, parent, scope);
        intel_ast_walk(c, n->query.group_key, parent, scope);
        intel_ast_walk(c, n->query.select, parent, scope); break;
    case AST_QUERY_WHERE: case AST_QUERY_LET: case AST_QUERY_ORDERBY: case AST_QUERY_JOIN:
        intel_ast_walk(c, n->query_clause.expr, parent, scope);
        intel_ast_walk(c, n->query_clause.source, parent, scope);
        intel_ast_walk(c, n->query_clause.left_key, parent, scope);
        intel_ast_walk(c, n->query_clause.right_key, parent, scope); break;
    case AST_LAMBDA: {
        if (!n->lambda.body) break;
        int body_start = (int)n->lambda.body->loc.offset;
        int arrow_tok = intel_ast_token_at(c, (int)n->loc.offset);
        for (; arrow_tok < c->token_count; arrow_tok++) {
            if (c->tokens[arrow_tok].token.kind == TK_ARROW) {
                if (arrow_tok + 1 < c->token_count) {
                    body_start = (int)c->tokens[arrow_tok + 1].token.loc.offset;
                }
                break;
            }
            if (c->tokens[arrow_tok].mate > arrow_tok) arrow_tok = c->tokens[arrow_tok].mate;
        }
        intel_scope_t lambda = {body_start, scope.end, scope.method};
        if (n->lambda.body->kind == AST_BLOCK) lambda.end = intel_ast_end(c, n->lambda.body, scope.end);
        else {
            int token = intel_ast_token_at(c, lambda.start);
            for (; token < c->token_count; token++) {
                zan_token_kind_t k = c->tokens[token].token.kind;
                if (k == TK_COMMA || k == TK_SEMICOLON || k == TK_RPAREN || k == TK_RBRACKET || k == TK_RBRACE || k == TK_EOF) {
                    lambda.end = (int)c->tokens[token].token.loc.offset;
                    break;
                }
                if (c->tokens[token].mate > token) token = c->tokens[token].mate;
            }
        }
        if (lambda.method < 0) {
            intel_scope_t callable = intel_ast_body(c, n->lambda.body, parent, "", (int)n->loc.offset, lambda);
            lambda.method = callable.method;
        }
        for (int i = 0; i < n->lambda.params.count; i++) {
            const zan_ast_node_t *p = n->lambda.params.items[i];
            intel_ast_type(p->param.type, type, sizeof(type));
            intel_ast_symbol(c, p, p->param.name, p->param.type, type, parent, NULL, ISYM_PARAMETER, 0, 0, lambda);
        }
        intel_ast_walk(c, n->lambda.body, parent, lambda); break;
    }
    default: break;
    }
}

/* 内部辅助逻辑 */
int intel_find_text_references(const char *text, const char *word, size_t *offsets, int max) {
    int count = 0;
    size_t wlen = strlen(word);
    if (!wlen) return 0;
    zan_arena_t *arena = zan_arena_new();
    zan_diag_t *diag = zan_diag_new(arena);
    zan_diag_set_capture(diag, true);
    zan_lexer_t lex;
    zan_lexer_init(&lex, text, strlen(text), 0, arena, diag);
    while (!offsets || count < max) {
        zan_token_t token = zan_lexer_next(&lex);
        if (token.kind == TK_EOF) break;
        if (token.kind == TK_IDENT && token.str_val.len == wlen &&
            memcmp(token.str_val.str, word, wlen) == 0) {
            if (offsets) offsets[count] = token.loc.offset;
            count++;
        }
    }
    zan_diag_free_buffers(diag);
    zan_arena_free(arena);
    return count;
}

static void intel_parse_zan_ast(intellisense_t *is, intellisense_t *project, const char *file,
                                 const char *content, size_t len) {
    zan_arena_t *arena = zan_arena_new();
    zan_diag_t *diag = zan_diag_new(arena);
    zan_diag_set_capture(diag, true);
    zan_lexer_t lex;
    zan_parser_t parser;
    intel_ast_ctx_t c = {0};
    char *recovery = NULL;
    c.is = is; c.project = project; c.file = file; c.source = content; c.len = len;
    c.lines = (size_t *)malloc((len + 1) * sizeof(size_t));
    if (!c.lines) goto done;
    c.lines[c.line_count++] = 0;
    for (size_t i = 0; i < len; i++) if (content[i] == '\n') c.lines[c.line_count++] = i + 1;
    int cap = 0;
    zan_lexer_init(&lex, content, len, 0, arena, diag);
    for (;;) {
        zan_token_t t = zan_lexer_next(&lex);
        if (c.token_count == cap) {
            int next = cap ? cap * 2 : 256;
            intel_token_t *tokens = (intel_token_t *)realloc(c.tokens, (size_t)next * sizeof(*tokens));
            if (!tokens) goto done;
            c.tokens = tokens; cap = next;
        }
        c.tokens[c.token_count++] = (intel_token_t){t, -1};
        if (t.kind == TK_EOF) break;
    }
    int *stack = (int *)malloc((size_t)c.token_count * sizeof(int));
    if (!stack) goto done;
    int depth = 0;
    for (int i = 0; i < c.token_count; i++) {
        zan_token_kind_t k = c.tokens[i].token.kind;
        if (k == TK_LBRACE || k == TK_LPAREN || k == TK_LBRACKET) stack[depth++] = i;
        else if (depth && (k == TK_RBRACE || k == TK_RPAREN || k == TK_RBRACKET)) {
            zan_token_kind_t expected = k == TK_RBRACE ? TK_LBRACE : k == TK_RPAREN ? TK_LPAREN : TK_LBRACKET;
            int match = depth - 1;
            /* 内部辅助逻辑 */
            while (match >= 0 && c.tokens[stack[match]].token.kind != expected) {
                if (c.tokens[stack[match]].token.kind == TK_LBRACE) break;
                match--;
            }
            if (match >= 0 && c.tokens[stack[match]].token.kind == expected) {
                int open = stack[match];
                depth = match; c.tokens[open].mate = i; c.tokens[i].mate = open;
            }
        }
    }
    free(stack);
    /* 核心系统底层抽象与内存语义契约 */
    for (int i = 0; i + 1 < c.token_count; i++) {
        zan_token_kind_t k = c.tokens[i].token.kind, next = c.tokens[i + 1].token.kind;
        if ((k == TK_DOT || k == TK_QUESTION_DOT || k == TK_EQ) &&
            (next == TK_EOF || next == TK_SEMICOLON || next == TK_RBRACE ||
             next == TK_RPAREN || next == TK_RBRACKET || next == TK_COMMA)) {
            if (!recovery) {
                recovery = (char *)malloc(len + 1);
                if (!recovery) goto done;
                memcpy(recovery, content, len); recovery[len] = '\0';
            }
            size_t offset = c.tokens[i].token.loc.offset;
            recovery[offset] = ' ';
            if (k == TK_QUESTION_DOT && offset + 1 < len) recovery[offset + 1] = ' ';
        }
    }
    zan_lexer_init(&lex, recovery ? recovery : content, len, 0, arena, diag);
    zan_parser_init(&parser, &lex, arena, diag);
    zan_ast_node_t *unit = zan_parser_parse(&parser);
    char saved_file[512];
    snprintf(saved_file, sizeof(saved_file), "%s", is->current_file);
    snprintf(is->current_file, sizeof(is->current_file), "%s", file);
    intel_scope_t scope = {0, (int)len + 1, -1};
    /* 内部辅助逻辑 */
    c.declarations_only = true;
    intel_ast_walk(&c, unit, "", scope);
    c.declarations_only = false;
    intel_ast_walk(&c, unit, "", scope);
    snprintf(is->current_file, sizeof(is->current_file), "%s", saved_file);
done:
    free(c.lines); free(c.tokens); free(recovery);
    zan_diag_free_buffers(diag);
    zan_arena_free(arena);
}

/* 模块核心语义抽象与接口调用契约 */
bool intel_same_file(const char *a, const char *b) {
#ifdef _WIN32
    while (*a && *b) {
        unsigned char ca = (unsigned char)*a++, cb = (unsigned char)*b++;
        if (ca == '\\') ca = '/';
        if (cb == '\\') cb = '/';
        if (tolower(ca) != tolower(cb)) return false;
    }
    return !*a && !*b;
#else
    return strcmp(a, b) == 0;
#endif
}

/* 底层系统交互与数据协议契约 */
void intel_parse_file_ex(intellisense_t *is, intellisense_t *project, const char *filepath,
                         const char *content, size_t len) {
    /* 底层系统交互与数据协议契约 */
    int dst = 0;
    for (int i = 0; i < is->symbol_count; i++) {
        if (!intel_same_file(is->symbols[i].file, filepath)) {
            if (dst != i) is->symbols[dst] = is->symbols[i];
            dst++;
        }
    }
    is->symbol_count = dst;

    /* 底层系统交互与数据协议契约 */
    int mdst = 0;
    for (int i = 0; i < is->method_count; i++) {
        if (!intel_same_file(is->methods[i].file, filepath)) {
            if (mdst != i) is->methods[mdst] = is->methods[i];
            mdst++;
        }
    }
    is->method_count = mdst;

    /* 核心系统底层抽象与内存语义契约 */
    int fdst = 0;
    for (int i = 0; i < is->indexed_file_count; i++) {
        if (intel_same_file(is->indexed_files[i], filepath)) continue;
        if (fdst != i) memcpy(is->indexed_files[fdst], is->indexed_files[i], sizeof(is->indexed_files[fdst]));
        fdst++;
    }
    is->indexed_file_count = fdst;
    if (reserve_files(is, fdst + 1)) {
        snprintf(is->indexed_files[is->indexed_file_count++], 512, "%s", filepath);
    }

    if (!content || len == 0) return;

    /* 底层系统交互与数据协议契约 */
    {
        size_t fl = strlen(filepath);
        if (fl > 7 && strcmp(filepath + fl - 7, ".zscene") == 0) {
            intel_parse_zscene(is, filepath, content, len);
            return;
        }
        if (fl > 5 && strcmp(filepath + fl - 5, ".html") == 0) {
            intel_parse_design_html(is, filepath, content, len);
            return;
        }
        if (fl > 4 && strcmp(filepath + fl - 4, ".htm") == 0) {
            intel_parse_design_html(is, filepath, content, len);
            return;
        }
    }

    intel_parse_zan_ast(is, project, filepath, content, len);
}

void intel_parse_file(intellisense_t *is, const char *filepath,
                      const char *content, size_t len) {
    intel_parse_file_ex(is, NULL, filepath, content, len);
}

bool intel_position_in(int line, int col, int start_line, int start_col,
                       int end_line, int end_col) {
    if (line < start_line || line > end_line) return false;
    if (col < 0) return true; /* 核心系统底层抽象与内存语义契约 */
    return (line != start_line || col >= start_col) &&
           (line != end_line || col < end_col);
}

const imethod_t *intel_method_at(const intellisense_t *is, int line, int col) {
    if (!is || line < 0) return NULL;
    const imethod_t *best = NULL;
    for (int i = 0; i < is->method_count; i++) {
        const imethod_t *m = &is->methods[i];
        if (is->current_file[0] && !intel_same_file(m->file, is->current_file)) continue;
        if (intel_position_in(line, col, m->start_line, m->start_col, m->end_line, m->end_col) &&
            (!best || m->end_offset - m->start_offset < best->end_offset - best->start_offset)) best = m;
    }
    return best;
}

bool intel_symbol_visible_at(const intellisense_t *is, const isym_t *sym, int line, int col) {
    if (!is || !sym) return false;
    if (sym->kind != ISYM_VARIABLE && sym->kind != ISYM_PARAMETER) return true;
    if (line < 0 || sym->method_offset < 0) return false;
    if (is->current_file[0] && !intel_same_file(sym->file, is->current_file)) return false;
    /* 模块核心语义抽象与接口调用契约 */
    if (sym->offset >= 0 && col >= 0 && line == sym->line && col >= sym->col &&
        col < sym->col + intel_utf16_col(sym->name, strlen(sym->name))) return true;
    const imethod_t *m = intel_method_at(is, line, col);
    if (!m || !intel_same_file(sym->file, m->file) || sym->method_offset != m->decl_offset) return false;
    if (!intel_position_in(line, col, sym->scope_start_line, sym->scope_start_col,
                          sym->scope_end_line, sym->scope_end_col)) return false;
    if (sym->kind == ISYM_VARIABLE &&
        (line < sym->line || (line == sym->line && col >= 0 && col < sym->col))) return false;
    return true;
}

const char *intel_enclosing_type_at(const intellisense_t *is, int line, int col) {
    const imethod_t *method = intel_method_at(is, line, col);
    if (method) return method->parent;
    const isym_t *best = NULL;
    if (!is || line < 0) return "";
    for (int i = 0; i < is->symbol_count; i++) {
        const isym_t *s = &is->symbols[i];
        if (s->kind != ISYM_CLASS && s->kind != ISYM_STRUCT && s->kind != ISYM_INTERFACE) continue;
        if (is->current_file[0] && !intel_same_file(s->file, is->current_file)) continue;
        if (s->offset >= 0 && intel_position_in(line, col, s->scope_start_line, s->scope_start_col,
                                               s->scope_end_line, s->scope_end_col) &&
            (!best || s->scope_start_offset > best->scope_start_offset)) best = s;
    }
    return best ? best->name : "";
}

const isym_t *intel_lookup_symbol_at(intellisense_t *is, const char *word, int line, int col) {
    if (!is || !word || !word[0]) return NULL;
    const isym_t *best = NULL;
    for (int i = is->symbol_count - 1; i >= 0; i--) {
        const isym_t *s = &is->symbols[i];
        if (strcmp(s->name, word) != 0 || !intel_symbol_visible_at(is, s, line, col)) continue;
        if (s->kind == ISYM_VARIABLE || s->kind == ISYM_PARAMETER) {
            if (!best || s->scope_start_offset > best->scope_start_offset ||
                (s->scope_start_offset == best->scope_start_offset && s->offset > best->offset)) best = s;
        }
    }
    if (best) return best;
    const char *parent = intel_enclosing_type_at(is, line, col);
    for (int i = is->symbol_count - 1; i >= 0; i--) {
        const isym_t *s = &is->symbols[i];
        if (strcmp(s->name, word) != 0 || s->kind == ISYM_VARIABLE || s->kind == ISYM_PARAMETER) continue;
        bool member = s->kind == ISYM_FIELD || s->kind == ISYM_PROPERTY || s->kind == ISYM_EVENT || s->kind == ISYM_METHOD;
        if (member && parent[0] && strcmp(s->parent, parent) == 0) return s;
        if (member && line >= 0) continue;
        if (is->current_file[0] && intel_same_file(s->file, is->current_file) && !best) best = s;
        else if (!is->current_file[0] && !best) best = s;
    }
    return best;
}

/* 模块核心语义抽象与接口调用契约 */
const isym_t *intel_lookup_symbol_any(intellisense_t *is, const char *word) {
    if (!is || !word || !word[0]) return NULL;
    const isym_t *best = NULL;
    for (int i = is->symbol_count - 1; i >= 0; i--) {
        const isym_t *s = &is->symbols[i];
        if (strcmp(s->name, word) != 0) continue;
        if (s->kind == ISYM_VARIABLE || s->kind == ISYM_PARAMETER) continue;
        bool member = s->kind == ISYM_FIELD || s->kind == ISYM_PROPERTY ||
                      s->kind == ISYM_EVENT || s->kind == ISYM_METHOD;
        if (member) return s;
        if (!best) best = s;
    }
    return best;
}

const char *intel_resolve_type_pos(intellisense_t *is, const char *var_name, int line, int col) {
    const isym_t *s = intel_lookup_symbol_at(is, var_name, line, col);
    if (!s) return NULL;
    if (s->kind == ISYM_VARIABLE || s->kind == ISYM_PARAMETER || s->kind == ISYM_FIELD ||
        s->kind == ISYM_PROPERTY || s->kind == ISYM_EVENT) return s->type_name;
    return NULL;
}

const char *intel_resolve_type_at(intellisense_t *is, const char *var_name, int line) {
    return intel_resolve_type_pos(is, var_name, line, -1);
}

const char *intel_resolve_type(intellisense_t *is, const char *var_name) {
    return intel_resolve_type_at(is, var_name, -1);
}

/* 返回the base type of a user-defined class/struct, or NULL */
static const char *class_base(intellisense_t *is, const char *cls) {
    char bare[128];
    snprintf(bare, sizeof(bare), "%s", cls);
    bare[strcspn(bare, "<?")] = '\0';
    const char *simple = strrchr(bare, '.');
    cls = simple ? simple + 1 : bare;
    for (int i = 0; i < is->symbol_count; i++) {
        isym_t *sym = &is->symbols[i];
        if ((sym->kind == ISYM_CLASS || sym->kind == ISYM_STRUCT ||
             sym->kind == ISYM_INTERFACE) &&
            strcmp(sym->name, cls) == 0) {
            return sym->type_name[0] ? sym->type_name : NULL;
        }
    }
    return NULL;
}

static bool intel_type_owner_is(const char *type, const char *owner) {
    char bare[128];
    snprintf(bare, sizeof(bare), "%s", type);
    bare[strcspn(bare, "<?")] = '\0';
    const char *simple = strrchr(bare, '.');
    return strcmp(simple ? simple + 1 : bare, owner) == 0;
}

/* 内部辅助逻辑 */
static int intel_owner_distance(intellisense_t *is, const char *type, const char *owner) {
    for (int depth = 0; type && type[0] && depth < 16; depth++) {
        if (intel_type_owner_is(type, owner)) return depth;
        type = class_base(is, type);
    }
    return -1;
}

/* 内部辅助逻辑 */
static bool member_visible(const intellisense_t *is, ivis_t vis,
                           const char *parent, const char *from_class) {
    if (vis == IVIS_PUBLIC) return true;
    if (!parent[0] || !from_class || !from_class[0]) return false;
    if (intel_type_owner_is(from_class, parent)) return true;
    return vis == IVIS_PROTECTED &&
           intel_owner_distance((intellisense_t *)is, from_class, parent) >= 0;
}

/* 核心系统底层抽象与内存语义契约 */
int intel_complete_members_pos(intellisense_t *is, const char *type_name,
                               const char *prefix, int line, int col) {
    is->completion_count = 0;
    is->completion_selected = 0;

    size_t plen = prefix ? strlen(prefix) : 0;

    /* 底层系统交互与数据协议契约 */
    if (strcmp(type_name, "this") == 0 || strcmp(type_name, "self") == 0) {
        const char *encl = intel_enclosing_type_at(is, line, col);
        if (encl && encl[0]) type_name = encl;
    }
    bool is_known_type = false;
    for (int i = 0; i < is->symbol_count; i++) {
        isym_kind_t k = is->symbols[i].kind;
        if ((k == ISYM_CLASS || k == ISYM_STRUCT || k == ISYM_INTERFACE ||
             k == ISYM_ENUM) && strcmp(is->symbols[i].name, type_name) == 0) {
            is_known_type = true;
            break;
        }
    }
    if (!is_known_type && builtin_receiver(type_name)) is_known_type = true;
    if (!is_known_type) {
        const char *rv = (col >= 0) ? intel_resolve_type_pos(is, type_name, line, col)
                                    : intel_resolve_type_at(is, type_name, line);
        if (rv && rv[0]) type_name = rv;
    }

    const char *rank = intel_array_suffix(type_name);
    if (rank) {
        /* 内部辅助逻辑 */
        const char *members[] = {"Length", "Count", "GetLength"};
        int count = rank[1] == ',' ? 3 : 2;
        for (int i = 0; i < count; i++) {
            if (plen && _strnicmp(members[i], prefix, plen) != 0) continue;
            completion_t *c = &is->completions[is->completion_count++];
            memset(c, 0, sizeof(*c));
            snprintf(c->label, sizeof(c->label), "%s", members[i]);
            snprintf(c->insert_text, sizeof(c->insert_text), "%s%s", members[i], i == 2 ? "(" : "");
            snprintf(c->detail, sizeof(c->detail), "int %s%s", members[i], i == 2 ? "(int dimension)" : "");
            c->kind = i == 2 ? ISYM_METHOD : ISYM_PROPERTY;
        }
        is->completion_active = is->completion_count > 0;
        return is->completion_count;
    }

    /* 内部辅助逻辑 */
    char bare_type[64];
    snprintf(bare_type, sizeof(bare_type), "%s", type_name);
    { char *lt = strchr(bare_type, '<'); if (lt) *lt = '\0';
      char *br = strstr(bare_type, "[]"); if (br) *br = '\0'; }

    /* 内部辅助逻辑 */
    const char *from_class = intel_enclosing_type_at(is, line, col);

    /* 内部辅助逻辑 */
    const char *cls = bare_type;
    int guard = 0;
    while (cls && cls[0] && guard < 16 &&
           is->completion_count < INTEL_MAX_COMPLETIONS) {
        char member_parent[128];
        snprintf(member_parent, sizeof(member_parent), "%s", cls);
        member_parent[strcspn(member_parent, "<?")] = '\0';
        const char *simple = strrchr(member_parent, '.');
        simple = simple ? simple + 1 : member_parent;
        for (int i = 0; i < is->symbol_count && is->completion_count < INTEL_MAX_COMPLETIONS; i++) {
            isym_t *sym = &is->symbols[i];
            if (sym->parent[0] == '\0') continue;
            if (strcmp(sym->parent, simple) != 0) continue;
            if (sym->kind != ISYM_METHOD && sym->kind != ISYM_FIELD &&
                sym->kind != ISYM_PROPERTY && sym->kind != ISYM_ENUM_MEMBER)
                continue;
            if (!member_visible(is, sym->visibility, sym->parent, from_class))
                continue;

            if (plen > 0 && _strnicmp(sym->name, prefix, plen) != 0) continue;

            /* 核心系统底层抽象与内存语义契约 */
            bool dup = false;
            for (int k = 0; k < is->completion_count; k++) {
                if (strcmp(is->completions[k].label, sym->name) == 0) { dup = true; break; }
            }
            if (dup) continue;

            completion_t *c = &is->completions[is->completion_count++];
            strncpy(c->label, sym->name, sizeof(c->label) - 1);
            if (sym->kind == ISYM_METHOD)
                snprintf(c->insert_text, sizeof(c->insert_text), "%s(", sym->name);
            else
                strncpy(c->insert_text, sym->name, sizeof(c->insert_text) - 1);
            if (sym->signature[0])
                strncpy(c->detail, sym->signature, sizeof(c->detail) - 1);
            else
                snprintf(c->detail, sizeof(c->detail), "%s.%s : %s", cls, sym->name, sym->type_name);
            strncpy(c->doc, sym->doc, sizeof(c->doc) - 1);
            c->kind = sym->kind;
            c->sort_priority = (guard == 0) ? 0 : 1;
        }
        cls = class_base(is, cls);
        guard++;
    }

    /* 内部辅助逻辑 */
    const zan_builtin_type_t *bt = builtin_receiver(bare_type);
    if (bt) {
        for (int i = 0; i < bt->member_count && is->completion_count < INTEL_MAX_COMPLETIONS; i++) {
            const zan_builtin_member_t *m = &bt->members[i];
            if (plen > 0 && _strnicmp(m->name, prefix, plen) != 0) continue;
            if (already_offered(is, m->name)) continue;
            completion_t *c = &is->completions[is->completion_count++];
            snprintf(c->label, sizeof(c->label), "%s", m->name);
            if (m->kind == 'M')
                snprintf(c->insert_text, sizeof(c->insert_text), "%s(", m->name);
            else
                snprintf(c->insert_text, sizeof(c->insert_text), "%s", m->name);
            /* 模块核心语义抽象与接口调用契约 */
            snprintf(c->detail, sizeof(c->detail), "%s", m->sig);
            c->kind = (m->kind == 'M') ? ISYM_METHOD : ISYM_FIELD;
            c->sort_priority = 1;
        }
    }

    /* 核心系统底层抽象与内存语义契约 */
    for (int i = 0; i < is->symbol_count && is->completion_count < INTEL_MAX_COMPLETIONS; i++) {
        if (is->symbols[i].kind == ISYM_ENUM && strcmp(is->symbols[i].name, bare_type) == 0) {
            /* 底层系统交互与数据协议契约 */
            for (int j = 0; j < is->symbol_count && is->completion_count < INTEL_MAX_COMPLETIONS; j++) {
                if (is->symbols[j].kind == ISYM_ENUM_MEMBER &&
                    strcmp(is->symbols[j].parent, bare_type) == 0) {
                    if (plen > 0 && _strnicmp(is->symbols[j].name, prefix, plen) != 0) continue;
                    completion_t *c = &is->completions[is->completion_count++];
                    strncpy(c->label, is->symbols[j].name, sizeof(c->label) - 1);
                    strncpy(c->insert_text, is->symbols[j].name, sizeof(c->insert_text) - 1);
                    snprintf(c->detail, sizeof(c->detail), "%s.%s", type_name, is->symbols[j].name);
                    c->kind = ISYM_ENUM_MEMBER;
                    c->sort_priority = 0;
                }
            }
            break;
        }
    }

    is->completion_active = is->completion_count > 0;
    return is->completion_count;
}

int intel_complete_members_at(intellisense_t *is, const char *type_name,
                              const char *prefix, int line) {
    return intel_complete_members_pos(is, type_name, prefix, line, -1);
}

int intel_complete_members(intellisense_t *is, const char *type_name,
                           const char *prefix) {
    return intel_complete_members_pos(is, type_name, prefix, -1, -1);
}

static int intel_bare_symbol_rank(intellisense_t *is, const isym_t *sym,
                                  const char *from_class, int line, int col) {
    switch (sym->kind) {
    case ISYM_VARIABLE: case ISYM_PARAMETER:
        if (line < 0 || !intel_symbol_visible_at(is, sym, line, col) ||
            intel_lookup_symbol_at(is, sym->name, line, col) != sym) return -1;
        return 0;
    case ISYM_METHOD: case ISYM_FIELD: case ISYM_PROPERTY: case ISYM_EVENT: {
        /* 内部辅助逻辑 */
        if (!from_class[0]) return -1;
        int distance = intel_owner_distance(is, from_class, sym->parent);
        if (distance < 0 || !member_visible(is, sym->visibility, sym->parent, from_class)) return -1;
        return distance + 1;
    }
    case ISYM_CLASS: case ISYM_STRUCT: case ISYM_INTERFACE: case ISYM_ENUM:
    case ISYM_TYPE: case ISYM_NAMESPACE:
        return 32;
    default:
        return -1;
    }
}

bool intel_is_keyword(const char *word) {
    if (!word || !word[0]) return false;
    for (int i = 0; builtin_keywords[i]; i++)
        if (strcmp(builtin_keywords[i], word) == 0) return true;
    return false;
}

/* 核心系统底层抽象与内存语义契约 */
static int intel_complete_pos_ex(intellisense_t *is, const char *prefix,
                                 const char *context_class,
                                 const char *from_class_override,
                                 int line, int col) {
    is->completion_count = 0;
    is->completion_selected = 0;

    size_t plen = strlen(prefix);
    if (plen == 0) {
        is->completion_active = false;
        return 0;
    }

    /* 底层系统交互与数据协议契约 */
    if (context_class && context_class[0]) {
        return intel_complete_members_pos(is, context_class, prefix, line, col);
    }

    /* 核心系统底层抽象与内存语义契约 */
    for (int i = 0; i < is->snippet_count && is->completion_count < INTEL_MAX_COMPLETIONS; i++) {
        if (_strnicmp(is->snippets[i].trigger, prefix, plen) != 0) continue;
        completion_t *c = &is->completions[is->completion_count++];
        snprintf(c->label, sizeof(c->label), "%s (snippet)", is->snippets[i].trigger);
        strncpy(c->insert_text, is->snippets[i].body, sizeof(c->insert_text) - 1);
        strncpy(c->detail, is->snippets[i].description, sizeof(c->detail) - 1);
        c->kind = ISYM_SNIPPET;
        c->sort_priority = -1;
    }

    const char *from_class = (from_class_override && from_class_override[0])
                                 ? from_class_override
                                 : intel_enclosing_type_at(is, line, col);
    for (int i = 0; i < is->symbol_count && is->completion_count < INTEL_MAX_COMPLETIONS; i++) {
        isym_t *sym = &is->symbols[i];
        if (_strnicmp(sym->name, prefix, plen) != 0) continue;
        int rank = intel_bare_symbol_rank(is, sym, from_class, line, col);
        if (rank < 0 || already_offered(is, sym->name)) continue;
        /* 内部辅助逻辑 */
        bool hidden = false;
        for (int j = 0; j < is->symbol_count; j++) {
            if (j == i || strcmp(is->symbols[j].name, sym->name) != 0) continue;
            int other = intel_bare_symbol_rank(is, &is->symbols[j], from_class, line, col);
            if (other >= 0 && other < rank) { hidden = true; break; }
        }
        if (hidden) continue;

        completion_t *c = &is->completions[is->completion_count++];
        strncpy(c->label, sym->name, sizeof(c->label) - 1);
        if (sym->kind == ISYM_METHOD)
            snprintf(c->insert_text, sizeof(c->insert_text), "%s(", sym->name);
        else
            strncpy(c->insert_text, sym->name, sizeof(c->insert_text) - 1);

        if (sym->signature[0])
            strncpy(c->detail, sym->signature, sizeof(c->detail) - 1);
        else if (sym->type_name[0])
            snprintf(c->detail, sizeof(c->detail), "%s : %s", sym->name, sym->type_name);
        else
            strncpy(c->detail, sym->name, sizeof(c->detail) - 1);

        strncpy(c->doc, sym->doc, sizeof(c->doc) - 1);
        c->kind = sym->kind;
        c->sort_priority = 0;
    }

    /* 核心系统底层抽象与内存语义契约 */
    for (int i = 0; builtin_keywords[i] && is->completion_count < INTEL_MAX_COMPLETIONS; i++) {
        if (_strnicmp(builtin_keywords[i], prefix, plen) != 0) continue;
        /* 核心系统底层抽象与内存语义契约 */
        bool dup = false;
        for (int j = 0; j < is->completion_count; j++) {
            if (strcmp(is->completions[j].insert_text, builtin_keywords[i]) == 0 ||
                strncmp(is->completions[j].label, builtin_keywords[i], strlen(builtin_keywords[i])) == 0) {
                dup = true; break;
            }
        }
        if (dup) continue;
        completion_t *c = &is->completions[is->completion_count++];
        strncpy(c->label, builtin_keywords[i], sizeof(c->label) - 1);
        strncpy(c->insert_text, builtin_keywords[i], sizeof(c->insert_text) - 1);
        snprintf(c->detail, sizeof(c->detail), "keyword");
        c->kind = ISYM_KEYWORD;
        c->sort_priority = 2;
    }

    /* 核心系统底层抽象与内存语义契约 */
    for (int i = 0; builtin_types[i] && is->completion_count < INTEL_MAX_COMPLETIONS; i++) {
        if (_strnicmp(builtin_types[i], prefix, plen) != 0) continue;
        bool dup = false;
        for (int j = 0; j < is->completion_count; j++) {
            if (strcmp(is->completions[j].label, builtin_types[i]) == 0) { dup = true; break; }
        }
        if (dup) continue;
        completion_t *c = &is->completions[is->completion_count++];
        strncpy(c->label, builtin_types[i], sizeof(c->label) - 1);
        strncpy(c->insert_text, builtin_types[i], sizeof(c->insert_text) - 1);
        snprintf(c->detail, sizeof(c->detail), "type");
        c->kind = ISYM_TYPE;
        c->sort_priority = 1;
    }

    /* 内部辅助逻辑 */
    {
        int btcount = 0;
        const zan_builtin_type_t *all = zan_builtin_types(&btcount);
        for (int i = 0; i < btcount && is->completion_count < INTEL_MAX_COMPLETIONS; i++) {
            const char *tname = all[i].name_public;
            if (_strnicmp(tname, prefix, plen) != 0) continue;
            if (already_offered(is, tname)) continue;
            completion_t *c = &is->completions[is->completion_count++];
            snprintf(c->label, sizeof(c->label), "%s", tname);
            snprintf(c->insert_text, sizeof(c->insert_text), "%s", tname);
            snprintf(c->detail, sizeof(c->detail), "%s", all[i].display);
            c->kind = ISYM_TYPE;
            c->sort_priority = 1;
        }
    }

    is->completion_active = is->completion_count > 0;
    return is->completion_count;
}

int intel_complete_pos(intellisense_t *is, const char *prefix,
                       const char *context_class, int line, int col) {
    return intel_complete_pos_ex(is, prefix, context_class, NULL, line, col);
}

/* 内部辅助实现 */
int intel_complete_bare(intellisense_t *is, const char *prefix,
                        const char *from_class) {
    return intel_complete_pos_ex(is, prefix, NULL, from_class, -1, -1);
}

int intel_complete_at(intellisense_t *is, const char *prefix,
                      const char *context_class, int line) {
    return intel_complete_pos(is, prefix, context_class, line, -1);
}

int intel_complete(intellisense_t *is, const char *prefix,
                   const char *context_class) {
    return intel_complete_pos(is, prefix, context_class, -1, -1);
}

hover_info_t intel_hover_pos(intellisense_t *is, const char *word, int line, int col) {
    hover_info_t info = {0};
    if (!is || !word || !word[0]) return info;

    const isym_t *selected = intel_lookup_symbol_at(is, word, line, col);
    /* 内部辅助逻辑 */
    if (!selected) selected = intel_lookup_symbol_any(is, word);
    /* 内部辅助逻辑 */
    for (int i = 0; i < is->symbol_count; i++) {
        if (&is->symbols[i] == selected) {
            isym_kind_t hk = is->symbols[i].kind;
            if (is->symbols[i].signature[0]) {
                strncpy(info.text, is->symbols[i].signature, sizeof(info.text) - 1);
            } else if (hk == ISYM_CLASS || hk == ISYM_STRUCT ||
                       hk == ISYM_INTERFACE || hk == ISYM_ENUM) {
                const char *kw = "class";
                if (hk == ISYM_STRUCT) kw = "struct";
                else if (hk == ISYM_INTERFACE) kw = "interface";
                else if (hk == ISYM_ENUM) kw = "enum";
                if (is->symbols[i].type_name[0])
                    snprintf(info.text, sizeof(info.text), "%s %s : %s",
                            kw, is->symbols[i].name, is->symbols[i].type_name);
                else
                    snprintf(info.text, sizeof(info.text), "%s %s",
                            kw, is->symbols[i].name);
            } else {
                snprintf(info.text, sizeof(info.text), "%s : %s",
                        is->symbols[i].name, is->symbols[i].type_name);
            }
            strncpy(info.doc, is->symbols[i].doc, sizeof(info.doc) - 1);
            info.valid = true;
            return info;
        }
    }

    /* 核心系统底层抽象与内存语义契约 */
    for (int i = 0; builtin_types[i]; i++) {
        if (strcmp(builtin_types[i], word) == 0) {
            snprintf(info.text, sizeof(info.text), "type %s (built-in)", word);
            info.valid = true;
            return info;
        }
    }

    /* 检查the compiler's built-in type members (`File */
    {
        int btcount = 0;
        const zan_builtin_type_t *all = zan_builtin_types(&btcount);
        for (int i = 0; i < btcount; i++) {
            const zan_builtin_type_t *bt = &all[i];
            if (strcmp(bt->name_public, word) == 0 || strcmp(bt->type, word) == 0) {
                snprintf(info.text, sizeof(info.text), "class %s (built-in)", bt->display);
                info.valid = true;
                return info;
            }
            for (int mi = 0; mi < bt->member_count; mi++) {
                if (strcmp(bt->members[mi].name, word) != 0) continue;
                snprintf(info.text, sizeof(info.text), "%s", bt->members[mi].sig);
                snprintf(info.doc, sizeof(info.doc), "%s.%s", bt->name_public, word);
                info.valid = true;
                return info;
            }
        }
    }

    /* 核心系统底层抽象与内存语义契约 */
    for (int i = 0; builtin_keywords[i]; i++) {
        if (strcmp(builtin_keywords[i], word) == 0) {
            snprintf(info.text, sizeof(info.text), "keyword %s", word);
            info.valid = true;
            return info;
        }
    }

    return info;
}

hover_info_t intel_hover_at(intellisense_t *is, const char *word, int line) {
    return intel_hover_pos(is, word, line, -1);
}

/* 内部辅助逻辑 */
static const isym_t *intel_member_sym(intellisense_t *is, const char *type_name,
                                      const char *member) {
    if (!is || !type_name || !type_name[0] || !member || !member[0]) return NULL;

    char bare[64];
    snprintf(bare, sizeof(bare), "%s", type_name);
    { char *lt = strchr(bare, '<'); if (lt) *lt = '\0';
      char *br = strstr(bare, "[]"); if (br) *br = '\0'; }

    const char *cls = bare;
    for (int guard = 0; cls && cls[0] && guard < 16; guard++) {
        char simple[128];
        snprintf(simple, sizeof(simple), "%s", cls);
        simple[strcspn(simple, "<?")] = '\0';
        const char *sp = strrchr(simple, '.');
        sp = sp ? sp + 1 : simple;
        for (int i = 0; i < is->symbol_count; i++) {
            const isym_t *sym = &is->symbols[i];
            if (!sym->parent[0] || strcmp(sym->parent, sp) != 0) continue;
            if (strcmp(sym->name, member) != 0) continue;
            if (sym->kind != ISYM_METHOD && sym->kind != ISYM_FIELD &&
                sym->kind != ISYM_PROPERTY && sym->kind != ISYM_ENUM_MEMBER)
                continue;
            return sym;
        }
        cls = class_base(is, cls);
    }
    return NULL;
}

/* 核心系统底层抽象与内存语义契约 */
hover_info_t intel_hover_member(intellisense_t *is, const char *type_name,
                                const char *member) {
    hover_info_t info = {0};
    if (!is) return info;

    const isym_t *sym = intel_member_sym(is, type_name, member);
    if (sym) {
        if (sym->signature[0])
            strncpy(info.text, sym->signature, sizeof(info.text) - 1);
        else
            snprintf(info.text, sizeof(info.text), "%s : %s",
                     sym->name, sym->type_name);
        strncpy(info.doc, sym->doc, sizeof(info.doc) - 1);
        info.valid = true;
        return info;
    }

    /* 内部辅助逻辑 */
    char bare[64];
    snprintf(bare, sizeof(bare), "%s", type_name);
    { char *lt = strchr(bare, '<'); if (lt) *lt = '\0';
      char *br = strstr(bare, "[]"); if (br) *br = '\0'; }
    const zan_builtin_type_t *bt = builtin_receiver(bare);
    if (bt) {
        for (int mi = 0; mi < bt->member_count; mi++) {
            if (strcmp(bt->members[mi].name, member) != 0) continue;
            snprintf(info.text, sizeof(info.text), "%s", bt->members[mi].sig);
            snprintf(info.doc, sizeof(info.doc), "%s.%s", bt->name_public, member);
            info.valid = true;
            return info;
        }
    }
    return info;
}

/* 核心系统底层抽象与内存语义契约 */
bool intel_goto_member(intellisense_t *is, const char *type_name,
                       const char *member, goto_def_t *out) {
    if (!out) return false;
    const isym_t *sym = intel_member_sym(is, type_name, member);
    if (!sym) return false;
    strncpy(out->file, sym->file, sizeof(out->file) - 1);
    out->line = sym->line;
    out->col = sym->col;
    out->found = true;
    return true;
}

hover_info_t intel_hover(intellisense_t *is, const char *word) {
    return intel_hover_at(is, word, -1);
}

goto_def_t intel_goto_def(intellisense_t *is, const char *word) {
    goto_def_t result = {0};

    for (int i = 0; i < is->symbol_count; i++) {
        if (strcmp(is->symbols[i].name, word) == 0) {
            /* 底层系统交互与数据协议契约 */
            if (is->symbols[i].kind == ISYM_CLASS || is->symbols[i].kind == ISYM_STRUCT ||
                is->symbols[i].kind == ISYM_ENUM || is->symbols[i].kind == ISYM_INTERFACE ||
                !result.found) {
                strncpy(result.file, is->symbols[i].file, sizeof(result.file) - 1);
                result.line = is->symbols[i].line;
                result.col = is->symbols[i].col;
                result.found = true;
                if (is->symbols[i].kind == ISYM_CLASS || is->symbols[i].kind == ISYM_STRUCT)
                    return result;
            }
        }
    }

    return result;
}

/* 内部辅助逻辑 */
static void sig_param_list(param_info_t *params, int *count, int max,
                           const char *signature) {
    const char *pstart = strchr(signature, '(');
    const char *pend = pstart ? strchr(pstart, ')') : NULL;
    if (!pstart || !pend) return;
    pstart++;
    char params_copy[512];
    int plen2 = (int)(pend - pstart);
    if (plen2 > 510) plen2 = 510;
    memcpy(params_copy, pstart, (size_t)plen2);
    params_copy[plen2] = '\0';

    /* 内部辅助逻辑 */
    char *tok = params_copy;
    while (*tok && *count < max) {
        while (*tok == ' ') tok++;
        char *comma = NULL;
        int nest = 0;
        for (char *q = tok; *q; q++) {
            if (*q == '(' || *q == '[' || *q == '{') nest++;
            else if (*q == ')' || *q == ']' || *q == '}') { if (nest > 0) nest--; }
            else if (*q == '<' && q > tok &&
                     (isalnum((unsigned char)q[-1]) || q[-1] == '_')) nest++;
            else if (*q == '>' && nest > 0) nest--;
            else if (*q == ',' && nest == 0) { comma = q; break; }
        }
        int tlen = comma ? (int)(comma - tok) : (int)strlen(tok);
        if (tlen > 0) {
            char param_str[128];
            if (tlen > 127) tlen = 127;
            memcpy(param_str, tok, (size_t)tlen);
            param_str[tlen] = '\0';

            /* 底层系统交互与数据协议契约 */
            char *eq = strchr(param_str, '=');
            if (eq) {
                while (eq > param_str && eq[-1] == ' ') eq--;
                *eq = '\0';
            }

            /* 核心系统底层抽象与内存语义契约 */
            char *space = strrchr(param_str, ' ');
            if (space) {
                *space = '\0';
                strncpy(params[*count].type, param_str, sizeof(params[*count].type) - 1);
                strncpy(params[*count].label, space + 1, sizeof(params[*count].label) - 1);
            } else {
                strncpy(params[*count].label, param_str, sizeof(params[*count].label) - 1);
            }
            (*count)++;
        }
        if (comma) tok = comma + 1;
        else break;
    }
}

static void substitute_generics(const char *parent_type, const char *declaration,
                                 const char *raw_ret, char *out, size_t cap);

/* 模块核心语义抽象与接口调用契约 */
signature_info_t intel_signature_help_pos_ex(intellisense_t *is, intellisense_t *project,
                                             const char *method_name, const char *class_context,
                                             int line, int col) {
    signature_info_t sig = {0};
    if (!is || !method_name || !method_name[0]) return sig;
    const char *from_class = intel_enclosing_type_at(is, line, col);
    char receiver[128] = "";
    if (!class_context || !class_context[0] || strcmp(class_context, "this") == 0 ||
        strcmp(class_context, "self") == 0) {
        snprintf(receiver, sizeof(receiver), "%s", from_class);
    } else if (strcmp(class_context, "base") == 0) {
        const char *base = from_class[0] ? class_base(is, from_class) : NULL;
        if (!base && project && from_class[0]) base = class_base(project, from_class);
        if (base) snprintf(receiver, sizeof(receiver), "%s", base);
    } else {
        const char *type = intel_resolve_type_pos(is, class_context, line, col);
        if (!type && project) type = intel_resolve_type_pos(project, class_context, line, col);
        if (type) snprintf(receiver, sizeof(receiver), "%s", type);
        else {
            bool known = builtin_receiver(class_context) != NULL;
            intellisense_t *indices[] = {is, project};
            for (int k = 0; !known && k < 2; k++) {
                if (!indices[k]) continue;
                for (int i = 0; !known && i < indices[k]->symbol_count; i++) {
                    const isym_t *s = &indices[k]->symbols[i];
                    if ((s->kind == ISYM_CLASS || s->kind == ISYM_STRUCT || s->kind == ISYM_INTERFACE) &&
                        intel_type_owner_is(class_context, s->name)) known = true;
                }
            }
            if (known) snprintf(receiver, sizeof(receiver), "%s", class_context);
        }
    }
    if (!receiver[0] || intel_array_suffix(receiver)) return sig;

    for (int depth = 0; receiver[0] && depth < 16; depth++) {
        /* 内部辅助逻辑 */
        const zan_builtin_type_t *bt = builtin_receiver(receiver);
        if (bt) {
            for (int i = 0; i < bt->member_count; i++) {
                const zan_builtin_member_t *m = &bt->members[i];
                if (m->kind != 'M' || strcmp(m->name, method_name) != 0) continue;
                substitute_generics(receiver, bt->display, m->sig, sig.label, sizeof(sig.label));
                snprintf(sig.doc, sizeof(sig.doc), "%s.%s", bt->name_public, m->name);
                sig_param_list(sig.params, &sig.param_count, INTEL_MAX_PARAMS, sig.label);
                sig.valid = true;
                return sig;
            }
        }
        const isym_t *declaration = NULL;
        intellisense_t *indices[] = {is, project};
        for (int k = 0; !declaration && k < 2; k++) {
            if (!indices[k]) continue;
            for (int i = 0; i < indices[k]->symbol_count; i++) {
                const isym_t *s = &indices[k]->symbols[i];
                if ((s->kind == ISYM_CLASS || s->kind == ISYM_STRUCT || s->kind == ISYM_INTERFACE) &&
                    intel_type_owner_is(receiver, s->name)) { declaration = s; break; }
            }
        }
        for (int k = 0; k < 2; k++) {
            if (!indices[k]) continue;
            for (int i = 0; i < indices[k]->symbol_count; i++) {
                const isym_t *s = &indices[k]->symbols[i];
                if (s->kind != ISYM_METHOD || strcmp(s->name, method_name) != 0 ||
                    !intel_type_owner_is(receiver, s->parent) ||
                    !member_visible(indices[k], s->visibility, s->parent, from_class)) continue;
                substitute_generics(receiver, declaration ? declaration->signature : NULL,
                                    s->signature, sig.label, sizeof(sig.label));
                snprintf(sig.doc, sizeof(sig.doc), "%s", s->doc);
                sig_param_list(sig.params, &sig.param_count, INTEL_MAX_PARAMS, sig.label);
                sig.valid = true;
                return sig;
            }
        }
        if (!declaration || !declaration->type_name[0]) break;
        char base[128];
        substitute_generics(receiver, declaration->signature, declaration->type_name, base, sizeof(base));
        snprintf(receiver, sizeof(receiver), "%s", base);
    }
    return sig;
}

signature_info_t intel_signature_help_pos(intellisense_t *is, const char *method_name,
                                          const char *class_context, int line, int col) {
    return intel_signature_help_pos_ex(is, NULL, method_name, class_context, line, col);
}

signature_info_t intel_signature_help(intellisense_t *is, const char *method_name,
                                      const char *class_context) {
    return intel_signature_help_pos(is, method_name, class_context, -1, -1);
}

/* 核心系统底层抽象与内存语义契约 */
int intel_find_references(intellisense_t *is, const char *word,
                          goto_def_t *results, int max_results) {
    int count = 0;
    for (int i = 0; i < is->symbol_count && count < max_results; i++) {
        if (strcmp(is->symbols[i].name, word) == 0) {
            results[count].found = true;
            strncpy(results[count].file, is->symbols[i].file, sizeof(results[count].file) - 1);
            results[count].line = is->symbols[i].line;
            results[count].col = is->symbols[i].col;
            count++;
        }
    }
    return count;
}

bool intel_local_extent(intellisense_t *is, const char *word, int line,
                        int *out_start_line, int *out_end_line,
                        int *out_decl_line) {
    const isym_t *sym = intel_lookup_symbol_at(is, word, line, -1);
    if (!sym || (sym->kind != ISYM_VARIABLE && sym->kind != ISYM_PARAMETER)) return false;
    if (out_start_line) *out_start_line = sym->scope_start_line;
    if (out_end_line) *out_end_line = sym->scope_end_line;
    if (out_decl_line) *out_decl_line = sym->line;
    return true;
}

const char *intel_accept(intellisense_t *is) {
    if (!is->completion_active || is->completion_selected < 0 ||
        is->completion_selected >= is->completion_count)
        return NULL;

    const char *text = is->completions[is->completion_selected].insert_text;
    is->completion_active = false;
    return text;
}

void intel_select_up(intellisense_t *is) {
    if (is->completion_selected > 0)
        is->completion_selected--;
}

void intel_select_down(intellisense_t *is) {
    if (is->completion_selected < is->completion_count - 1)
        is->completion_selected++;
}

void intel_dismiss(intellisense_t *is) {
    is->completion_active = false;
    is->completion_count = 0;
    is->completion_selected = -1;
    is->signature_visible = false;
}

/* 核心系统底层抽象与内存语义契约 */

/* 模块核心语义抽象与接口调用契约 */
static bool extract_return_type_from_sig(const char *sig, char *out, size_t cap) {
    if (!sig || !sig[0] || !out || cap == 0) return false;
    out[0] = '\0';
    const char *p = sig;
    while (*p == ' ' || *p == '\t') p++;
    if (strncmp(p, "static ", 7) == 0) p += 7;
    while (*p == ' ' || *p == '\t') p++;
    if (strncmp(p, "async ", 6) == 0) p += 6;
    while (*p == ' ' || *p == '\t') p++;
    if (strncmp(p, "await ", 6) == 0) p += 6;
    while (*p == ' ' || *p == '\t') p++;

    const char *type_start = p;
    int angle_depth = 0;
    while (*p) {
        if (*p == '<') {
            angle_depth++;
            p++;
        } else if (*p == '>') {
            if (angle_depth > 0) angle_depth--;
            p++;
        } else if (angle_depth == 0 && (*p == ' ' || *p == '\t' || *p == '(')) {
            break;
        } else if (angle_depth == 0 && *p == '[' && p[1] == ']') {
            p += 2;
        } else {
            p++;
        }
    }
    int len = (int)(p - type_start);
    if (len <= 0 || (size_t)len >= cap) return false;
    memcpy(out, type_start, (size_t)len);
    out[len] = '\0';
    if (strcmp(out, "void") == 0) {
        out[0] = '\0';
        return false;
    }
    return true;
}

static intel_type_arg_t intel_generic_arg(const char *type, int index) {
    intel_type_arg_t empty = {NULL, 0};
    const char *p = type ? strchr(type, '<') : NULL;
    if (!p) return empty;
    const char *start = ++p;
    int angle = 1, bracket = 0, paren = 0, current = 0;
    for (; *p; p++) {
        if (*p == '<') angle++;
        else if (*p == '>') angle--;
        else if (*p == '[') bracket++;
        else if (*p == ']') bracket--;
        else if (*p == '(') paren++;
        else if (*p == ')') paren--;
        if (!angle || (*p == ',' && angle == 1 && !bracket && !paren)) {
            if (current == index) {
                const char *end = p;
                while (start < end && isspace((unsigned char)*start)) start++;
                while (end > start && isspace((unsigned char)end[-1])) end--;
                return (intel_type_arg_t){start, (size_t)(end - start)};
            }
            if (!angle) break;
            current++; start = p + 1;
        }
    }
    return empty;
}

/* 模块核心语义抽象与接口调用契约 */
static void substitute_generics(const char *parent_type, const char *declaration,
                                 const char *raw_ret, char *out, size_t cap) {
    if (!out || !cap) return;
    out[0] = '\0';
    if (!raw_ret) return;
    for (const char *p = raw_ret; *p;) {
        const char *start = p;
        if (isalpha((unsigned char)*p) || *p == '_') {
            while (isalnum((unsigned char)*p) || *p == '_') p++;
            intel_type_arg_t actual = {NULL, 0};
            for (int i = 0; ; i++) {
                intel_type_arg_t formal = intel_generic_arg(declaration, i);
                if (!formal.text) break;
                if (formal.len == (size_t)(p - start) && memcmp(formal.text, start, formal.len) == 0) {
                    actual = intel_generic_arg(parent_type, i);
                    break;
                }
            }
            size_t used = strlen(out);
            if (used < cap) snprintf(out + used, cap - used, "%.*s",
                                      (int)(actual.text ? actual.len : (size_t)(p - start)),
                                      actual.text ? actual.text : start);
        } else {
            char punctuation[2] = {*p++, '\0'};
            intel_ast_append(out, cap, punctuation);
        }
    }
}

const char *intel_resolve_method_return_ex(intellisense_t *is, intellisense_t *project,
                                           const char *type_name, const char *method_name) {
    static char buf[128];
    if (!type_name || !type_name[0] || !method_name || !method_name[0]) return NULL;
    char current[128];
    snprintf(current, sizeof(current), "%s", type_name);
    buf[0] = '\0';

    /* 模块核心语义抽象与接口调用契约 */
    const char *rank = intel_array_suffix(current);
    if (rank) {
        if (strcmp(method_name, "Length") == 0 || strcmp(method_name, "Count") == 0 ||
            (rank[1] == ',' && strcmp(method_name, "GetLength") == 0)) return "int";
        return NULL;
    }

    intellisense_t *indexes[] = {is, project};
    for (int guard = 0; current[0] && guard < 16; guard++) {
        char bare[128];
        snprintf(bare, sizeof(bare), "%s", current);
        bare[strcspn(bare, "<?")] = '\0';
        const char *simple = strrchr(bare, '.');
        simple = simple ? simple + 1 : bare;
        const isym_t *declaration = NULL;
        for (int ix = 0; ix < 2 && !declaration; ix++) {
            if (!indexes[ix]) continue;
            for (int i = 0; i < indexes[ix]->symbol_count; i++) {
                const isym_t *s = &indexes[ix]->symbols[i];
                if ((s->kind == ISYM_CLASS || s->kind == ISYM_STRUCT || s->kind == ISYM_INTERFACE) &&
                    strcmp(s->name, simple) == 0) { declaration = s; break; }
            }
        }
        for (int ix = 0; ix < 2; ix++) {
            if (!indexes[ix]) continue;
            for (int i = 0; i < indexes[ix]->symbol_count; i++) {
                const isym_t *s = &indexes[ix]->symbols[i];
                if ((s->kind == ISYM_METHOD || s->kind == ISYM_PROPERTY || s->kind == ISYM_FIELD || s->kind == ISYM_EVENT) &&
                    strcmp(s->parent, simple) == 0 && strcmp(s->name, method_name) == 0) {
                    if (!s->type_name[0] || strcmp(s->type_name, "void") == 0 || strcmp(s->type_name, "var") == 0) return NULL;
                    substitute_generics(current, declaration ? declaration->signature : NULL,
                                        s->type_name, buf, sizeof(buf));
                    return buf[0] ? buf : NULL;
                }
            }
        }
        const zan_builtin_type_t *bt = builtin_receiver(simple);
        if (bt) {
            for (int i = 0; i < bt->member_count; i++) {
                const zan_builtin_member_t *m = &bt->members[i];
                if (strcmp(m->name, method_name) != 0) continue;
                char raw_ret[128];
                if (!extract_return_type_from_sig(m->sig, raw_ret, sizeof(raw_ret)) || strcmp(raw_ret, "void") == 0) return NULL;
                const char *formal = strcmp(bt->type, "Dict") == 0 ? "Dict<K,V>" : "Collection<T>";
                substitute_generics(current, formal, raw_ret, buf, sizeof(buf));
                return buf[0] ? buf : NULL;
            }
        }
        if (!declaration || !declaration->type_name[0]) break;
        char base[128];
        substitute_generics(current, declaration->signature, declaration->type_name, base, sizeof(base));
        snprintf(current, sizeof(current), "%s", base);
    }
    return NULL;
}

const char *intel_resolve_method_return(intellisense_t *is, const char *type_name,
                                        const char *method_name) {
    return intel_resolve_method_return_ex(is, NULL, type_name, method_name);
}

static int intel_chain_indexes(const char *part) {
    int paren = 0, angle = 0, bracket = 0, count = 0;
    char quote = 0;
    for (const char *p = part; *p; p++) {
        if (quote) {
            if (*p == '\\' && p[1]) p++;
            else if (*p == quote) quote = 0;
            continue;
        }
        if (*p == '"' || *p == '\'') { quote = *p; continue; }
        if (*p == '(') paren++;
        else if (*p == ')') paren--;
        else if (*p == '<' && !paren && !bracket) angle++;
        else if (*p == '>' && angle && !paren && !bracket) angle--;
        else if (*p == '[') { if (!paren && !angle && !bracket) count++; bracket++; }
        else if (*p == ']') bracket--;
    }
    return count;
}

/* 模块核心语义抽象与接口调用契约 */
static bool intel_chain_name(const char *part, char *out, size_t cap) {
    const char *start = part;
    while (isspace((unsigned char)*start)) start++;
    const char *p = start;
    int angle = 0;
    for (; *p; p++) {
        if (*p == '<') angle++;
        else if (*p == '>' && angle) angle--;
        else if (!angle && (*p == '(' || *p == '[')) break;
    }
    const char *end = p;
    while (end > start && isspace((unsigned char)end[-1])) end--;
    snprintf(out, cap, "%.*s", (int)(end - start), start);
    return *p == '(';
}

const char *intel_resolve_chain_pos(intellisense_t *is, intellisense_t *project,
                                    const char *chain, char *final_member, size_t final_cap,
                                    int line, int col) {
    static char resolved_type[128];
    if (final_member && final_cap > 0) final_member[0] = '\0';
    resolved_type[0] = '\0';

    if (!chain || !chain[0]) return NULL;

    /* 底层系统交互与数据协议契约 */
    char parts[16][128];
    int part_count = 0;
    int paren_depth = 0;
    int angle_depth = 0;
    int bracket_depth = 0;
    char quote = 0;
    int part_start = 0;
    int ci = 0;

    while (chain[ci] && part_count < 16) {
        char ch = chain[ci];
        if (quote) {
            if (ch == '\\' && chain[ci + 1]) ci++;
            else if (ch == quote) quote = 0;
            ci++; continue;
        }
        if (ch == '"' || ch == '\'') quote = ch;
        else if (ch == '(') paren_depth++;
        else if (ch == ')') { if (paren_depth > 0) paren_depth--; }
        else if (ch == '[') bracket_depth++;
        else if (ch == ']') { if (bracket_depth > 0) bracket_depth--; }
        else if (ch == '<' && !paren_depth && !bracket_depth) angle_depth++;
        else if (ch == '>' && !paren_depth && !bracket_depth) { if (angle_depth > 0) angle_depth--; }
        else if (ch == '.' && paren_depth == 0 && angle_depth == 0 && bracket_depth == 0) {
            int len = ci - part_start;
            if (len > 0 && len < 127) {
                memcpy(parts[part_count], chain + part_start, (size_t)len);
                parts[part_count][len] = '\0';
                part_count++;
            }
            part_start = ci + 1;
        }
        ci++;
    }

    /* 核心系统底层抽象与内存语义契约 */
    int rem_len = ci - part_start;
    /* 内部辅助实现 */
    if (part_count == 0 && rem_len > 0 && rem_len < 127) {
        memcpy(parts[0], chain + part_start, (size_t)rem_len);
        parts[0][rem_len] = '\0';
        part_count = 1;
    }
    if (final_member && final_cap > 0 && rem_len >= 0) {
        int copy_len = rem_len < (int)final_cap - 1 ? rem_len : (int)final_cap - 1;
        memcpy(final_member, chain + part_start, (size_t)copy_len);
        final_member[copy_len] = '\0';
    }

    if (part_count == 0) return NULL;

    /* 核心系统底层抽象与内存语义契约 */
    char first_name[128];
    bool is_root_call = intel_chain_name(parts[0], first_name, sizeof(first_name));
    char *fn = first_name;
    int root_indexes = intel_chain_indexes(parts[0]);
    if (is_root_call) first_name[strcspn(first_name, "<")] = '\0';
    char root_owner[128];
    snprintf(root_owner, sizeof(root_owner), "%s", fn);
    root_owner[strcspn(root_owner, "<?")] = '\0';

    const char *current_type = NULL;
    const char *encl_cls = intel_enclosing_type_at(is, line, col);

    /* 模块核心语义抽象与接口调用契约 */
    if (parts[0][0] == '"' || parts[0][0] == '\'') {
        current_type = "string";
    } else if (parts[0][0] >= '0' && parts[0][0] <= '9') {
        current_type = "int";
    } else if (strcmp(fn, "this") == 0 || strcmp(fn, "self") == 0) {
        current_type = encl_cls;
    } else if (strcmp(fn, "base") == 0) {
        const char *base = encl_cls[0] ? class_base(is, encl_cls) : NULL;
        if (!base && project && encl_cls[0]) base = class_base(project, encl_cls);
        current_type = base;
    } else if (is_root_call) {
        /* 底层系统交互与数据协议契约 */
        if (encl_cls[0]) {
            current_type = intel_resolve_method_return_ex(is, project, encl_cls, fn);
        }
    } else {
        /* 底层系统交互与数据协议契约 */
        if (is && line >= 0) {
            current_type = intel_resolve_type_pos(is, fn, line, col);
        }
        if (line < 0 && (!current_type || !current_type[0])) {
            if (is) current_type = intel_resolve_type(is, fn);
        }
        if (line < 0 && (!current_type || !current_type[0])) {
            if (project) current_type = intel_resolve_type(project, fn);
        }
        if (!current_type || !current_type[0]) {
            /* 核心系统底层抽象与内存语义契约 */
            if (encl_cls[0]) {
                current_type = intel_resolve_method_return_ex(is, project, encl_cls, fn);
            }
        }
        if (!current_type || !current_type[0]) {
            /* 底层系统交互与数据协议契约 */
            if (builtin_receiver(root_owner)) {
                current_type = fn;
            } else if (is) {
                for (int i = 0; i < is->symbol_count; i++) {
                    if ((is->symbols[i].kind == ISYM_CLASS || is->symbols[i].kind == ISYM_STRUCT ||
                         is->symbols[i].kind == ISYM_ENUM || is->symbols[i].kind == ISYM_INTERFACE) &&
                        strcmp(is->symbols[i].name, root_owner) == 0) {
                        current_type = fn;
                        break;
                    }
                }
            }
            if ((!current_type || !current_type[0]) && project) {
                for (int i = 0; i < project->symbol_count; i++) {
                    if ((project->symbols[i].kind == ISYM_CLASS || project->symbols[i].kind == ISYM_STRUCT ||
                         project->symbols[i].kind == ISYM_ENUM || project->symbols[i].kind == ISYM_INTERFACE) &&
                        strcmp(project->symbols[i].name, root_owner) == 0) {
                        current_type = fn;
                        break;
                    }
                }
            }
        }
    }

    if (!current_type || !current_type[0]) {
        return NULL;
    }

    strncpy(resolved_type, current_type, sizeof(resolved_type) - 1);
    resolved_type[sizeof(resolved_type) - 1] = '\0';
    for (int i = 0; i < root_indexes; i++) {
        char collection[128];
        snprintf(collection, sizeof(collection), "%s", resolved_type);
        intel_ast_element_type(collection, true, resolved_type, sizeof(resolved_type));
        if (strcmp(resolved_type, "var") == 0) return NULL;
    }

    /* 底层系统交互与数据协议契约 */
    for (int pi = 1; pi < part_count; pi++) {
        char member_name[128];
        intel_chain_name(parts[pi], member_name, sizeof(member_name));
        member_name[strcspn(member_name, "<")] = '\0';
        int indexes = intel_chain_indexes(parts[pi]);
        char *mstart = member_name;

        if (!mstart[0]) continue;

        const char *next_type = intel_resolve_method_return_ex(is, project, resolved_type, mstart);
        if (next_type && next_type[0]) {
            strncpy(resolved_type, next_type, sizeof(resolved_type) - 1);
            resolved_type[sizeof(resolved_type) - 1] = '\0';
        } else return NULL; /* 底层系统交互与数据协议契约 */
        for (int i = 0; i < indexes; i++) {
            char collection[128];
            snprintf(collection, sizeof(collection), "%s", resolved_type);
            intel_ast_element_type(collection, true, resolved_type, sizeof(resolved_type));
            if (strcmp(resolved_type, "var") == 0) return NULL;
        }
    }

    return resolved_type[0] ? resolved_type : NULL;
}

const char *intel_resolve_chain_ex(intellisense_t *is, intellisense_t *project,
                                   const char *chain, char *final_member, size_t final_cap,
                                   int line) {
    return intel_resolve_chain_pos(is, project, chain, final_member, final_cap, line, -1);
}

const char *intel_resolve_chain(intellisense_t *is, const char *chain,
                                char *final_member, size_t final_cap) {
    return intel_resolve_chain_pos(is, NULL, chain, final_member, final_cap, -1, -1);
}

/* 核心系统底层抽象与内存语义契约 */

/* 内部辅助实现 */
static bool index_skip_dir(const char *name) {
    return strcmp(name, "bin") == 0 ||
           strcmp(name, "obj") == 0 ||
           strcmp(name, "build") == 0 ||
           strcmp(name, "dist") == 0 ||
           strcmp(name, "node_modules") == 0 ||
           strcmp(name, "target") == 0 ||
           strcmp(name, "_scratch") == 0 ||
           strcmp(name, ".git") == 0;
}

/* 内部辅助逻辑 */
volatile int intel_cancel_flag = 0;

#ifdef _WIN32

static void index_directory_recursive(intellisense_t *is, const char *dir_path) {
    if (intel_cancel_flag) return;
    char search_path[1024];
    snprintf(search_path, sizeof(search_path), "%s\\*", dir_path);

    wchar_t wsearch[1024];
    if (MultiByteToWideChar(CP_UTF8, 0, search_path, -1, wsearch, 1024) <= 0) return;

    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW(wsearch, &fd);
    if (hFind == INVALID_HANDLE_VALUE) return;

    do {
        if (intel_cancel_flag) break;
        if (fd.cFileName[0] == L'.') continue;

        char filename_utf8[512];
        if (WideCharToMultiByte(CP_UTF8, 0, fd.cFileName, -1, filename_utf8, sizeof(filename_utf8), NULL, NULL) <= 0)
            continue;

        char full_path[1024];
        snprintf(full_path, sizeof(full_path), "%s\\%s", dir_path, filename_utf8);

        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            /* 模块核心语义抽象与接口调用契约 */
            if (!index_skip_dir(filename_utf8)) {
                index_directory_recursive(is, full_path);
            }
        } else {
            /* 核心系统底层抽象与内存语义契约 */
            size_t name_len = strlen(filename_utf8);
            bool is_zan = name_len > 4 &&
                          strcmp(filename_utf8 + name_len - 4, ".zan") == 0;
            bool is_zscene = name_len > 7 &&
                             strcmp(filename_utf8 + name_len - 7, ".zscene") == 0;
            bool is_html = name_len > 5 &&
                           strcmp(filename_utf8 + name_len - 5, ".html") == 0;
            bool is_htm = name_len > 4 &&
                          strcmp(filename_utf8 + name_len - 4, ".htm") == 0;
            if ((is_zan || is_zscene || is_html || is_htm) && !intel_cancel_flag) {
                wchar_t wfull[1024];
                if (MultiByteToWideChar(CP_UTF8, 0, full_path, -1, wfull, 1024) > 0) {
                    HANDLE hFile = CreateFileW(wfull, GENERIC_READ, FILE_SHARE_READ,
                                               NULL, OPEN_EXISTING, 0, NULL);
                    if (hFile != INVALID_HANDLE_VALUE) {
                        DWORD file_size = GetFileSize(hFile, NULL);
                        if (file_size > 0 && file_size < 2 * 1024 * 1024) {
                            char *content = (char *)malloc(file_size + 1);
                            if (content) {
                                DWORD bytes_read;
                                ReadFile(hFile, content, file_size, &bytes_read, NULL);
                                content[bytes_read] = '\0';
                                intel_parse_file(is, full_path, content, bytes_read);
                                free(content);
                            }
                        }
                        CloseHandle(hFile);
                    }
                }
            }
        }
    } while (FindNextFileW(hFind, &fd));
    FindClose(hFind);
}

void intel_index_project(intellisense_t *is, const char *project_root) {
    if (!project_root || !project_root[0]) return;
    index_directory_recursive(is, project_root);
}

#else /* 核心系统底层抽象与内存语义契约 */
static void index_directory_recursive(intellisense_t *is, const char *dir_path) {
    if (intel_cancel_flag) return;
    DIR *dir = opendir(dir_path);
    if (!dir) return;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (intel_cancel_flag) break;
        if (entry->d_name[0] == '.') continue;

        char full_path[1024];
        snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entry->d_name);

        struct stat st;
        if (stat(full_path, &st) != 0) continue;

        if (S_ISDIR(st.st_mode)) {
            if (!index_skip_dir(entry->d_name)) {
                index_directory_recursive(is, full_path);
            }
        } else if (S_ISREG(st.st_mode)) {
            size_t name_len = strlen(entry->d_name);
            bool is_zan = name_len > 4 &&
                          strcmp(entry->d_name + name_len - 4, ".zan") == 0;
            bool is_zscene = name_len > 7 &&
                             strcmp(entry->d_name + name_len - 7, ".zscene") == 0;
            bool is_html = name_len > 5 &&
                           strcmp(entry->d_name + name_len - 5, ".html") == 0;
            bool is_htm = name_len > 4 &&
                          strcmp(entry->d_name + name_len - 4, ".htm") == 0;
            if ((is_zan || is_zscene || is_html || is_htm) && !intel_cancel_flag) {
                FILE *f = fopen(full_path, "rb");
                if (f) {
                    fseek(f, 0, SEEK_END);
                    long file_size = ftell(f);
                    fseek(f, 0, SEEK_SET);
                    if (file_size > 0 && file_size < 2 * 1024 * 1024) {
                        char *content = (char *)malloc((size_t)file_size + 1);
                        if (content) {
                            size_t nread = fread(content, 1, (size_t)file_size, f);
                            content[nread] = '\0';
                            intel_parse_file(is, full_path, content, nread);
                            free(content);
                        }
                    }
                    fclose(f);
                }
            }
        }
    }
    closedir(dir);
}

void intel_index_project(intellisense_t *is, const char *project_root) {
    if (!project_root || !project_root[0]) return;
    index_directory_recursive(is, project_root);
}
#endif

void intel_index_files(intellisense_t *is, const char **filepaths, int count) {
    for (int i = 0; i < count; i++) {
        if (intel_cancel_flag) break;
        if (!filepaths[i]) continue;

        /* 核心系统底层抽象与内存语义契约 */
#ifdef _WIN32
        wchar_t wfp[1024];
        FILE *f = NULL;
        if (MultiByteToWideChar(CP_UTF8, 0, filepaths[i], -1, wfp, 1024) > 0) {
            f = _wfopen(wfp, L"rb");
        } else {
            f = fopen(filepaths[i], "rb");
        }
#else
        FILE *f = fopen(filepaths[i], "rb");
#endif
        if (!f) continue;
        fseek(f, 0, SEEK_END);
        long file_size = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (file_size <= 0 || file_size > 2 * 1024 * 1024) { fclose(f); continue; }

        char *content = (char *)malloc((size_t)file_size + 1);
        if (!content) { fclose(f); continue; }
        size_t nread = fread(content, 1, (size_t)file_size, f);
        content[nread] = '\0';
        fclose(f);

        intel_parse_file(is, filepaths[i], content, nread);
        free(content);
    }
}

/* 核心系统底层抽象与内存语义契约 */

/* 核心系统底层抽象与内存语义契约 */
typedef struct {
    const char *ns;
    const char *types[32];
} ns_types_t;

static const ns_types_t stdlib_namespace_map[] = {
    {"System", {"Console", "Environment", "Math", "Convert", "String",
                "Object", "Exception", "Array", "Type", "GC",
                "Nullable", "Tuple", "Func", "Action", "Stopwatch", NULL}},
    {"System.IO", {"File", "Path", "Directory", "StreamReader", "StreamWriter",
                   "FileStream", "MemoryStream", NULL}},
    {"System.Collections", {"List", "Dict", "Queue", "Stack", "HashSet",
                            "LinkedList", NULL}},
    {"System.Text", {"StringBuilder", "Encoding", "Regex", NULL}},
    {"System.Threading", {"Thread", "Mutex", "Semaphore", "Task", "Timer",
                           "AtomicInt", "SharedTable", NULL}},
    {"System.Json", {"JsonValue", NULL}},
    {"System.Net", {"HttpClient", "HttpRequest", "HttpResponse", "Socket", "TcpClient", NULL}},
    {"System.Diagnostics", {"Process", "ProcessControl", "ProcessHost", "Log", "ServerMetrics", NULL}},
    {"System.Linq", {"Enumerable", NULL}},
    {NULL, {NULL}}
};

/* 核心系统底层抽象与内存语义契约 */
static const char *find_namespace_for_type(const char *type_name) {
    for (int i = 0; stdlib_namespace_map[i].ns; i++) {
        for (int j = 0; stdlib_namespace_map[i].types[j]; j++) {
            if (strcmp(type_name, stdlib_namespace_map[i].types[j]) == 0)
                return stdlib_namespace_map[i].ns;
        }
    }
    return NULL;
}

static void intel_add_ns_completion(intellisense_t *is, const char *ns,
                                    const char *ns_prefix) {
    size_t plen = strlen(ns_prefix);
    if (plen && _strnicmp(ns, ns_prefix, plen) != 0) return;
    for (int j = 0; j < is->completion_count; j++) {
        if (strcmp(is->completions[j].label, ns) == 0) return;
    }
    if (is->completion_count >= INTEL_MAX_COMPLETIONS) return;
    completion_t *c = &is->completions[is->completion_count++];
    strncpy(c->label, ns, sizeof(c->label) - 1);
    strncpy(c->insert_text, ns, sizeof(c->insert_text) - 1);
    snprintf(c->detail, sizeof(c->detail), "namespace");
    c->doc[0] = '\0';
    c->kind = ISYM_NAMESPACE;
    c->sort_priority = 1;
}

int intel_complete_usings(intellisense_t *is, intellisense_t *project,
                          const char *ns_prefix) {
    is->completion_count = 0;
    is->completion_selected = 0;
    is->completion_active = true;
    if (!ns_prefix) ns_prefix = "";

    /* 模块核心语义抽象与接口调用契约 */
    for (int pass = 0; pass < 2; pass++) {
        intellisense_t *src = pass == 0 ? is : project;
        if (!src) continue;
        for (int i = 0; i < src->symbol_count; i++) {
            if (src->symbols[i].kind != ISYM_NAMESPACE) continue;
            intel_add_ns_completion(is, src->symbols[i].name, ns_prefix);
        }
    }
    for (int i = 0; stdlib_namespace_map[i].ns; i++) {
        intel_add_ns_completion(is, stdlib_namespace_map[i].ns, ns_prefix);
    }
    return is->completion_count;
}

using_analysis_t intel_analyze_usings(intellisense_t *is, const char *content, size_t len) {
    using_analysis_t result;
    memset(&result, 0, sizeof(result));

    if (!content || len == 0) return result;

    const char *p = content;
    const char *end = content + len;
    int line_num = 0;
    bool in_using_block = true;

    /* 模块核心语义抽象与接口调用契约 */
    while (p < end && in_using_block) {
        while (p < end && (*p == ' ' || *p == '\t' || *p == '\r')) p++;
        if (p >= end) break;
        if (*p == '\n') { line_num++; p++; continue; }

        /* 核心系统底层抽象与内存语义契约 */
        if (p + 1 < end && p[0] == '/' && p[1] == '/') {
            while (p < end && *p != '\n') p++;
            continue;
        }

        /* 核心系统底层抽象与内存语义契约 */
        if (p + 5 < end && memcmp(p, "using", 5) == 0 &&
            !isalnum((unsigned char)p[5]) && p[5] != '_') {
            p += 5;
            while (p < end && (*p == ' ' || *p == '\t')) p++;

            /* 核心系统底层抽象与内存语义契约 */
            const char *ns_start = p;
            while (p < end && *p != ';' && *p != '\n') p++;
            int ns_len = (int)(p - ns_start);
            while (ns_len > 0 && (ns_start[ns_len-1] == ' ' || ns_start[ns_len-1] == '\t' || ns_start[ns_len-1] == ';'))
                ns_len--;
            if (ns_len > 0 && ns_len < 127 && result.using_count < INTEL_MAX_USINGS) {
                using_entry_t *u = &result.usings[result.using_count++];
                memcpy(u->namespace_name, ns_start, (size_t)ns_len);
                u->namespace_name[ns_len] = '\0';
                u->line = line_num;
                u->is_used = false;
            }
            if (p < end && *p == ';') p++;
        } else if (p + 9 < end && memcmp(p, "namespace", 9) == 0) {
            in_using_block = false;
        } else if (isalpha((unsigned char)*p)) {
            in_using_block = false;
        } else {
            p++;
        }
    }

    /* 内部辅助逻辑 */
    p = content;
    line_num = 0;
    while (p < end) {
        if (*p == '\n') { line_num++; p++; continue; }
        if (!isalpha((unsigned char)*p) && *p != '_') { p++; continue; }

        /* 核心系统底层抽象与内存语义契约 */
        const char *word_start = p;
        while (p < end && (isalnum((unsigned char)*p) || *p == '_')) p++;
        int wlen = (int)(p - word_start);
        if (wlen >= 128) continue;

        char word[128];
        memcpy(word, word_start, (size_t)wlen);
        word[wlen] = '\0';

        /* 核心系统底层抽象与内存语义契约 */
        if (strcmp(word, "using") == 0 || strcmp(word, "namespace") == 0 ||
            strcmp(word, "class") == 0 || strcmp(word, "struct") == 0 ||
            strcmp(word, "if") == 0 || strcmp(word, "else") == 0 ||
            strcmp(word, "for") == 0 || strcmp(word, "while") == 0 ||
            strcmp(word, "return") == 0 || strcmp(word, "void") == 0 ||
            strcmp(word, "int") == 0 || strcmp(word, "bool") == 0 ||
            strcmp(word, "string") == 0 || strcmp(word, "var") == 0)
            continue;

        /* 核心系统底层抽象与内存语义契约 */
        const char *ns = find_namespace_for_type(word);
        if (!ns) continue;

        /* 底层系统交互与数据协议契约 */
        bool found_using = false;
        for (int i = 0; i < result.using_count; i++) {
            if (strcmp(result.usings[i].namespace_name, ns) == 0) {
                result.usings[i].is_used = true;
                found_using = true;
                break;
            }
        }

        /* 底层系统交互与数据协议契约 */
        if (!found_using) {
            bool already_missing = false;
            for (int i = 0; i < result.missing_count; i++) {
                if (strcmp(result.missing_usings[i], ns) == 0) {
                    already_missing = true;
                    break;
                }
            }
            if (!already_missing && result.missing_count < INTEL_MAX_USINGS) {
                strncpy(result.missing_usings[result.missing_count++], ns, 127);
            }
        }
    }

    /* 核心系统底层抽象与内存语义契约 */
    for (int i = 0; i < result.using_count; i++) {
        if (!result.usings[i].is_used) {
            /* 模块核心语义抽象与接口调用契约 */
            bool used_by_symbol = false;
            for (int s = 0; s < is->symbol_count; s++) {
                if (is->symbols[s].kind == ISYM_NAMESPACE &&
                    strcmp(is->symbols[s].name, result.usings[i].namespace_name) == 0) {
                    used_by_symbol = true;
                    break;
                }
            }
            if (!used_by_symbol && result.unused_count < INTEL_MAX_USINGS) {
                result.unused_indices[result.unused_count++] = i;
            }
        }
    }

    return result;
}

void intel_format_using(const char *namespace_name, char *out, size_t out_cap) {
    snprintf(out, out_cap, "using %s;", namespace_name);
}

/* 底层系统交互与数据协议契约 */
static int using_sort_cmp(const void *a, const void *b) {
    const using_entry_t *ua = (const using_entry_t *)a;
    const using_entry_t *ub = (const using_entry_t *)b;
    /* 核心系统底层抽象与内存语义契约 */
    bool a_sys = (strncmp(ua->namespace_name, "System", 6) == 0);
    bool b_sys = (strncmp(ub->namespace_name, "System", 6) == 0);
    if (a_sys && !b_sys) return -1;
    if (!a_sys && b_sys) return 1;
    return strcmp(ua->namespace_name, ub->namespace_name);
}

char *intel_organize_usings(intellisense_t *is, const char *content, size_t len,
                            size_t *out_len) {
    if (!content || len == 0) { *out_len = 0; return NULL; }

    using_analysis_t analysis = intel_analyze_usings(is, content, len);

    /* 核心系统底层抽象与内存语义契约 */
    using_entry_t new_usings[INTEL_MAX_USINGS * 2];
    int new_count = 0;

    /* 核心系统底层抽象与内存语义契约 */
    for (int i = 0; i < analysis.using_count; i++) {
        /* 核心系统底层抽象与内存语义契约 */
        bool is_unused = false;
        for (int j = 0; j < analysis.unused_count; j++) {
            if (analysis.unused_indices[j] == i) { is_unused = true; break; }
        }
        if (!is_unused) {
            new_usings[new_count] = analysis.usings[i];
            new_count++;
        }
    }

    /* 核心系统底层抽象与内存语义契约 */
    for (int i = 0; i < analysis.missing_count; i++) {
        using_entry_t *u = &new_usings[new_count++];
        memset(u, 0, sizeof(*u));
        strncpy(u->namespace_name, analysis.missing_usings[i], 127);
        u->is_used = true;
    }

    /* 核心系统底层抽象与内存语义契约 */
    qsort(new_usings, (size_t)new_count, sizeof(using_entry_t), using_sort_cmp);

    /* 内部辅助逻辑 */

    /* 模块核心语义抽象与接口调用契约 */
    int first_using_line = -1, last_using_line = -1;
    for (int i = 0; i < analysis.using_count; i++) {
        if (first_using_line < 0 || analysis.usings[i].line < first_using_line)
            first_using_line = analysis.usings[i].line;
        if (analysis.usings[i].line > last_using_line)
            last_using_line = analysis.usings[i].line;
    }

    if (first_using_line < 0) {
        /* 模块核心语义抽象与接口调用契约 */
        first_using_line = 0;
        last_using_line = -1;
    }

    /* 底层系统交互与数据协议契约 */
    size_t using_start_off = 0;
    size_t using_end_off = 0;
    int cur_line = 0;
    size_t off = 0;

    while (off < len && cur_line < first_using_line) {
        if (content[off] == '\n') cur_line++;
        off++;
    }
    using_start_off = off;

    if (last_using_line >= 0) {
        while (off < len && cur_line <= last_using_line) {
            if (content[off] == '\n') cur_line++;
            off++;
        }
        using_end_off = off;
    } else {
        using_end_off = using_start_off;
    }

    /* 核心系统底层抽象与内存语义契约 */
    size_t buf_cap = len + (size_t)new_count * 140 + 64;
    char *buf = (char *)malloc(buf_cap);
    if (!buf) { *out_len = 0; return NULL; }
    size_t buf_len = 0;

    /* 核心系统底层抽象与内存语义契约 */
    memcpy(buf, content, using_start_off);
    buf_len = using_start_off;

    /* 核心系统底层抽象与内存语义契约 */
    for (int i = 0; i < new_count; i++) {
        int n = snprintf(buf + buf_len, buf_cap - buf_len, "using %s;\n",
                        new_usings[i].namespace_name);
        if (n > 0) buf_len += (size_t)n;
    }

    /* 模块核心语义抽象与接口调用契约 */
    if (buf_len > 0 && buf[buf_len - 1] == '\n' && using_end_off < len &&
        content[using_end_off] != '\n') {
        buf[buf_len++] = '\n';
    }

    /* 核心系统底层抽象与内存语义契约 */
    size_t remaining = len - using_end_off;
    if (remaining > 0) {
        memcpy(buf + buf_len, content + using_end_off, remaining);
        buf_len += remaining;
    }
    buf[buf_len] = '\0';

    *out_len = buf_len;
    return buf;
}

/* 核心系统底层抽象与内存语义契约 */
int intel_collect_inlay_hints(intellisense_t *is, const char *content, size_t len,
                             intel_inlay_hint_t *hints, int max_hints) {
    if (!content || len == 0 || !hints || max_hints <= 0) return 0;
    int count = 0;

    /* 模块核心语义抽象与接口调用契约 */
    if (is) {
        for (int i = 0; i < is->symbol_count && count < max_hints; i++) {
            isym_t *sym = &is->symbols[i];
            if (sym->kind == ISYM_VARIABLE && sym->type_name[0] &&
                strcmp(sym->type_name, "var") != 0 && strcmp(sym->type_name, "void") != 0) {
                /* 核心系统底层抽象与内存语义契约 */
                int cur_line = 0;
                const char *p = content;
                const char *end = content + len;
                while (p < end && cur_line < sym->line) {
                    if (*p == '\n') cur_line++;
                    p++;
                }
                if (p < end && cur_line == sym->line) {
                    const char *le = p;
                    while (le < end && *le != '\n') le++;
                    /* 核心系统底层抽象与内存语义契约 */
                    const char *var_pos = strstr(p, "var ");
                    if (var_pos && var_pos < le) {
                        /* 底层系统交互与数据协议契约 */
                        const char *name_pos = strstr(var_pos + 4, sym->name);
                        if (name_pos && name_pos < le) {
                            /* 核心系统底层抽象与内存语义契约 */
                            int col = (int)(name_pos - p) + (int)strlen(sym->name);
                            intel_inlay_hint_t *h = &hints[count++];
                            h->line = sym->line;
                            h->col = col;
                            h->kind = 1; /* 核心系统底层抽象与内存语义契约 */
                            snprintf(h->label, sizeof(h->label), ": %s", sym->type_name);
                        }
                    }
                }
            }
        }
    }

    /* 底层系统交互与数据协议契约 */
    if (is && count < max_hints) {
        const char *p = content;
        const char *end = content + len;
        int line_num = 0;
        const char *line_start = p;

        while (p < end && count < max_hints) {
            if (*p == '\n') {
                line_num++;
                p++;
                line_start = p;
                continue;
            }
            if (*p == '"') {
                p++;
                while (p < end && *p != '"') {
                    if (*p == '\\' && p + 1 < end) p++;
                    if (*p == '\n') { line_num++; line_start = p + 1; }
                    p++;
                }
                if (p < end) p++;
                continue;
            }
            if (*p == '/' && p + 1 < end && p[1] == '/') {
                while (p < end && *p != '\n') p++;
                continue;
            }

            /* 底层系统交互与数据协议契约 */
            if (isalpha((unsigned char)*p) || *p == '_') {
                const char *w_start = p;
                while (p < end && (isalnum((unsigned char)*p) || *p == '_')) p++;
                int w_len = (int)(p - w_start);
                char fn_name[128];
                if (w_len < 127) {
                    memcpy(fn_name, w_start, (size_t)w_len);
                    fn_name[w_len] = '\0';
                } else {
                    continue;
                }

                const char *peek = p;
                while (peek < end && (*peek == ' ' || *peek == '\t')) peek++;
                if (peek < end && *peek == '(') {
                    /* 核心系统底层抽象与内存语义契约 */
                    if (strcmp(fn_name, "if") == 0 || strcmp(fn_name, "while") == 0 ||
                        strcmp(fn_name, "for") == 0 || strcmp(fn_name, "foreach") == 0 ||
                        strcmp(fn_name, "switch") == 0 || strcmp(fn_name, "catch") == 0) {
                        p = peek + 1;
                        continue;
                    }

                    /* 核心系统底层抽象与内存语义契约 */
                    isym_t *method_sym = NULL;
                    for (int s = 0; s < is->symbol_count; s++) {
                        if (is->symbols[s].kind == ISYM_METHOD &&
                            strcmp(is->symbols[s].name, fn_name) == 0 &&
                            is->symbols[s].param_count > 0) {
                            method_sym = &is->symbols[s];
                            break;
                        }
                    }

                    if (method_sym && method_sym->signature[0]) {
                        /* 核心系统底层抽象与内存语义契约 */
                        const char *sig_paren = strchr(method_sym->signature, '(');
                        if (sig_paren) {
                            sig_paren++;
                            char pnames[8][64];
                            int pcount = 0;
                            const char *sp = sig_paren;
                            while (*sp && *sp != ')' && pcount < 8) {
                                while (*sp == ' ' || *sp == '\t') sp++;
                                if (!*sp || *sp == ')') break;
                                /* 核心系统底层抽象与内存语义契约 */
                                while (*sp && *sp != ' ' && *sp != ')' && *sp != ',') sp++;
                                while (*sp == ' ' || *sp == '\t') sp++;
                                const char *pn_s = sp;
                                while (*sp && *sp != ',' && *sp != ')' && *sp != ' ') sp++;
                                int pnl = (int)(sp - pn_s);
                                if (pnl > 0 && pnl < 63) {
                                    memcpy(pnames[pcount], pn_s, (size_t)pnl);
                                    pnames[pcount][pnl] = '\0';
                                    pcount++;
                                }
                                while (*sp && *sp != ',' && *sp != ')') sp++;
                                if (*sp == ',') sp++;
                            }

                            /* 核心系统底层抽象与内存语义契约 */
                            const char *arg_p = peek + 1;
                            int arg_idx = 0;
                            int paren_lvl = 1;
                            while (arg_p < end && paren_lvl > 0 && arg_idx < pcount && count < max_hints) {
                                while (arg_p < end && (*arg_p == ' ' || *arg_p == '\t')) arg_p++;
                                if (arg_p >= end || *arg_p == ')') break;

                                /* 底层系统交互与数据协议契约 */
                                int arg_col = (int)(arg_p - line_start);
                                intel_inlay_hint_t *h = &hints[count++];
                                h->line = line_num;
                                h->col = arg_col;
                                h->kind = 2; /* 核心系统底层抽象与内存语义契约 */
                                snprintf(h->label, sizeof(h->label), "%s:", pnames[arg_idx]);
                                arg_idx++;

                                /* 底层系统交互与数据协议契约 */
                                while (arg_p < end && paren_lvl > 0) {
                                    if (*arg_p == '(') paren_lvl++;
                                    else if (*arg_p == ')') {
                                        paren_lvl--;
                                        if (paren_lvl == 0) break;
                                    } else if (*arg_p == ',' && paren_lvl == 1) {
                                        arg_p++;
                                        break;
                                    } else if (*arg_p == '\n') {
                                        line_num++;
                                        line_start = arg_p + 1;
                                    }
                                    arg_p++;
                                }
                            }
                            p = arg_p;
                            continue;
                        }
                    }
                }
            }
            p++;
        }
    }

    return count;
}

