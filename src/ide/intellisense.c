/* intellisense.c -- Code intelligence implementation.
 *
 * Enhanced with:
 *   - Dot-triggered member completion for user-defined types
 *   - Snippet completions (if, for, foreach, class, etc.)
 *   - Signature help for method calls
 *   - Multi-file symbol indexing
 *   - Variable type resolution for member access
 *   - Better doc-comment extraction
 */
#include "intellisense.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

#ifndef _WIN32
#include <strings.h>
#define _strnicmp strncasecmp
#endif

#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#endif
#include "../common/host_oom.h"

/* Built-in type completions */
static const char *builtin_types[] = {
    "int", "float", "double", "bool", "string", "char", "byte", "long",
    "short", "void", "var", "object", "decimal", "uint", "ulong",
    "ushort", "sbyte", NULL
};

/* Built-in keyword completions */
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

/* Console built-in methods */
static const char *console_methods[] = {
    "WriteLine", "Write", "ReadLine", "Read", "Clear", NULL
};

/* Stdlib static class members for semantic autocomplete */
typedef struct {
    const char *class_name;
    const char *methods[24];
} stdlib_class_t;

static const stdlib_class_t stdlib_classes[] = {
    {"File", {"ReadAllText", "WriteAllText", "AppendAllText", "Exists", "Delete",
              "Move", "Copy", "GetSize", NULL}},
    {"Path", {"GetFileName", "GetExtension", "Combine", "GetDirectoryName",
              "HasExtension", "ChangeExtension", "GetFileNameWithoutExtension",
              "GetTempPath", NULL}},
    {"Directory", {"Exists", "CreateDirectory", "Delete", "GetCurrentDirectory",
                   "SetCurrentDirectory", "GetFiles", "GetDirectories", NULL}},
    {"JsonParser", {"GetString", "GetInt", "GetDouble", "GetBool", "HasKey",
                    "GetValueRaw", NULL}},
    {"JsonBuilder", {"ObjectStart", "ObjectEnd", "AddString", "AddInt",
                     "AddBool", "AddNull", NULL}},
    {"Thread", {"Sleep", "CurrentId", NULL}},
    {"Stopwatch", {"GetMilliseconds", NULL}},
    {"Mutex", {"Create", "Lock", "Unlock", "Destroy", NULL}},
    {"AtomicInt", {"Load", "Store", "Exchange", "CompareExchange", "Add",
                   "Increment", "Decrement", "IsValid", NULL}},
    {"SharedTable", {"Open", "KeySize", "ColumnInt", "ColumnFloat",
                     "ColumnString", "Create", "Close", "Destroy", "SetInt",
                     "GetInt", "SetFloat", "GetFloat", "SetString", "GetString",
                     "Increment", "Decrement", "Delete", "Exists", "Count",
                     "Clear", "IsOpen", NULL}},
    {"Encoding", {"IntToString", "ParseInt", "ParseDouble", "GetByteCount", NULL}},
    {"Convert", {"ToInt32", "ToString", NULL}},
    {"Math", {"Sqrt", "Abs", "Max", "Min", "Pow", "Floor", "Ceiling", "Round",
              "Sin", "Cos", "Tan", "Log", "Exp", "PI", "E", NULL}},
    {"Environment", {"ArgCount", "ArgAt", "Exit", "GetEnvironmentVariable", NULL}},
    {"String", {"IsNullOrEmpty", "Format", "Join", "Split", "Concat", NULL}},
    {NULL, {NULL}}
};

/* List<T> instance methods */
static const char *list_methods[] = {
    "Add", "Clear", "RemoveAt", "IndexOf", "Contains", "Insert", "Reverse",
    "Count", "Sort", "ToArray", "ForEach", "Find", "FindAll", "Remove", NULL
};

/* Dict<K,V> instance methods */
static const char *dict_methods[] = {
    "Add", "ContainsKey", "Remove", "Clear", "Count", "TryGetValue",
    "Keys", "Values", NULL
};

/* StringBuilder instance methods */
static const char *sb_methods[] = {
    "Append", "AppendLine", "ToString", "Clear", "Length", "Insert",
    "Remove", "Replace", NULL
};

/* string instance methods */
static const char *string_methods[] = {
    "Length", "Substring", "Contains", "StartsWith", "EndsWith",
    "IndexOf", "LastIndexOf", "Replace", "Trim", "TrimStart", "TrimEnd",
    "Split", "ToLower", "ToUpper", "PadLeft", "PadRight", "Insert",
    "Remove", "Equals", "CompareTo", NULL
};

void intel_init(intellisense_t *is) {
    memset(is, 0, sizeof(intellisense_t));
    is->completion_selected = -1;
    intel_register_snippets(is);
}

void intel_clear(intellisense_t *is) {
    is->symbol_count = 0;
    is->indexed_file_count = 0;
}

static void add_symbol(intellisense_t *is, const char *name,
                       const char *type_name, const char *parent,
                       const char *signature, const char *file,
                       isym_kind_t kind, int line, int col) {
    if (is->symbol_count >= INTEL_MAX_SYMBOLS) return;
    isym_t *sym = &is->symbols[is->symbol_count++];
    memset(sym, 0, sizeof(isym_t));
    strncpy(sym->name, name, sizeof(sym->name) - 1);
    strncpy(sym->type_name, type_name ? type_name : "", sizeof(sym->type_name) - 1);
    strncpy(sym->parent, parent ? parent : "", sizeof(sym->parent) - 1);
    strncpy(sym->signature, signature ? signature : "", sizeof(sym->signature) - 1);
    strncpy(sym->file, file ? file : "", sizeof(sym->file) - 1);
    sym->kind = kind;
    sym->line = line;
    sym->col = col;
}

/* Add symbol with extra metadata */
static void add_symbol_ex(intellisense_t *is, const char *name,
                          const char *type_name, const char *parent,
                          const char *signature, const char *file,
                          const char *doc,
                          isym_kind_t kind, int line, int col,
                          bool is_static, int param_count) {
    if (is->symbol_count >= INTEL_MAX_SYMBOLS) return;
    isym_t *sym = &is->symbols[is->symbol_count++];
    memset(sym, 0, sizeof(isym_t));
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

/* Register built-in code snippets */
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

/* Simple lexical scanner to extract symbols from Zan source */
void intel_parse_file(intellisense_t *is, const char *filepath,
                      const char *content, size_t len) {
    /* clear previous symbols from this file */
    int dst = 0;
    for (int i = 0; i < is->symbol_count; i++) {
        if (strcmp(is->symbols[i].file, filepath) != 0) {
            if (dst != i) is->symbols[dst] = is->symbols[i];
            dst++;
        }
    }
    is->symbol_count = dst;

    /* track this file as indexed */
    bool already_indexed = false;
    for (int i = 0; i < is->indexed_file_count; i++) {
        if (strcmp(is->indexed_files[i], filepath) == 0) { already_indexed = true; break; }
    }
    if (!already_indexed && is->indexed_file_count < 64) {
        strncpy(is->indexed_files[is->indexed_file_count++], filepath, 511);
    }

    if (!content || len == 0) return;

    char current_class[128] = {0};
    char current_ns[128] = {0};
    char last_doc_comment[256] = {0};
    int brace_depth = 0;
    int class_brace = -1;
    int line_num = 0;

    const char *p = content;
    const char *end = content + len;

    while (p < end) {
        /* skip whitespace */
        while (p < end && (*p == ' ' || *p == '\t' || *p == '\r')) p++;
        if (p >= end) break;

        if (*p == '\n') { line_num++; p++; continue; }

        /* capture doc comments (/// style) */
        if (p + 2 < end && p[0] == '/' && p[1] == '/' && p[2] == '/') {
            p += 3;
            while (p < end && *p == ' ') p++;
            const char *doc_start = p;
            while (p < end && *p != '\n') p++;
            int doc_len = (int)(p - doc_start);
            if (doc_len > 254) doc_len = 254;
            memcpy(last_doc_comment, doc_start, (size_t)doc_len);
            last_doc_comment[doc_len] = '\0';
            continue;
        }

        /* skip comments */
        if (p + 1 < end && p[0] == '/' && p[1] == '/') {
            while (p < end && *p != '\n') p++;
            last_doc_comment[0] = '\0';
            continue;
        }
        if (p + 1 < end && p[0] == '/' && p[1] == '*') {
            p += 2;
            while (p + 1 < end && !(p[0] == '*' && p[1] == '/')) {
                if (*p == '\n') line_num++;
                p++;
            }
            if (p + 1 < end) p += 2;
            continue;
        }

        /* track braces */
        if (*p == '{') { brace_depth++; p++; continue; }
        if (*p == '}') {
            brace_depth--;
            if (brace_depth == class_brace) {
                current_class[0] = '\0';
                class_brace = -1;
            }
            p++;
            continue;
        }

        /* skip string literals */
        if (*p == '"') {
            p++;
            while (p < end && *p != '"') {
                if (*p == '\\' && p + 1 < end) p++;
                if (*p == '\n') line_num++;
                p++;
            }
            if (p < end) p++;
            continue;
        }
        if (*p == '$' && p + 1 < end && p[1] == '"') {
            p += 2;
            while (p < end && *p != '"') {
                if (*p == '\\' && p + 1 < end) p++;
                if (*p == '{') { while (p < end && *p != '}') p++; }
                if (*p == '\n') line_num++;
                p++;
            }
            if (p < end) p++;
            continue;
        }

        /* extract identifier */
        if (isalpha((unsigned char)*p) || *p == '_') {
            const char *word_start = p;
            while (p < end && (isalnum((unsigned char)*p) || *p == '_')) p++;
            int wlen = (int)(p - word_start);

            char word[128];
            int copy_len = wlen < 127 ? wlen : 127;
            memcpy(word, word_start, (size_t)copy_len);
            word[copy_len] = '\0';

            /* namespace */
            if (strcmp(word, "namespace") == 0) {
                while (p < end && (*p == ' ' || *p == '\t')) p++;
                const char *ns = p;
                while (p < end && (isalnum((unsigned char)*p) || *p == '.' || *p == '_')) p++;
                int nslen = (int)(p - ns) < 127 ? (int)(p - ns) : 127;
                memcpy(current_ns, ns, (size_t)nslen);
                current_ns[nslen] = '\0';
                add_symbol(is, current_ns, NULL, NULL, NULL, filepath,
                          ISYM_NAMESPACE, line_num, (int)(word_start - content));
                last_doc_comment[0] = '\0';
                continue;
            }

            /* class / struct / enum / interface */
            if (strcmp(word, "class") == 0 || strcmp(word, "struct") == 0 ||
                strcmp(word, "enum") == 0 || strcmp(word, "interface") == 0) {
                isym_kind_t kind = ISYM_CLASS;
                if (strcmp(word, "struct") == 0) kind = ISYM_STRUCT;
                else if (strcmp(word, "enum") == 0) kind = ISYM_ENUM;
                else if (strcmp(word, "interface") == 0) kind = ISYM_INTERFACE;

                while (p < end && (*p == ' ' || *p == '\t')) p++;
                const char *name = p;
                while (p < end && (isalnum((unsigned char)*p) || *p == '_')) p++;
                int nlen = (int)(p - name) < 127 ? (int)(p - name) : 127;
                memcpy(current_class, name, (size_t)nlen);
                current_class[nlen] = '\0';
                class_brace = brace_depth;

                add_symbol_ex(is, current_class, NULL, current_ns, NULL, filepath,
                             last_doc_comment[0] ? last_doc_comment : NULL,
                             kind, line_num, (int)(name - content), false, 0);
                last_doc_comment[0] = '\0';

                /* If enum, parse enum members */
                if (kind == ISYM_ENUM) {
                    while (p < end && *p != '{') {
                        if (*p == '\n') line_num++;
                        p++;
                    }
                    if (p < end) { p++; brace_depth++; }
                    while (p < end && *p != '}') {
                        while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) {
                            if (*p == '\n') line_num++;
                            p++;
                        }
                        if (p < end && *p == '}') break;
                        if (p < end && (isalpha((unsigned char)*p) || *p == '_')) {
                            const char *ename = p;
                            while (p < end && (isalnum((unsigned char)*p) || *p == '_')) p++;
                            int elen = (int)(p - ename) < 127 ? (int)(p - ename) : 127;
                            char enum_name[128];
                            memcpy(enum_name, ename, (size_t)elen);
                            enum_name[elen] = '\0';
                            add_symbol(is, enum_name, current_class, current_class,
                                      NULL, filepath, ISYM_ENUM_MEMBER, line_num,
                                      (int)(ename - content));
                        }
                        while (p < end && *p != ',' && *p != '}') {
                            if (*p == '\n') line_num++;
                            p++;
                        }
                        if (p < end && *p == ',') p++;
                    }
                }
                continue;
            }

            /* method detection: type name(...) pattern */
            if (current_class[0] && brace_depth == class_brace + 1) {
                /* skip modifiers */
                bool is_static_m = false;
                bool is_async_m = false;
                bool is_override = false;
                const char *saved_p = p;
                char type[64] = {0};

                if (strcmp(word, "static") == 0 || strcmp(word, "public") == 0 ||
                    strcmp(word, "private") == 0 || strcmp(word, "protected") == 0 ||
                    strcmp(word, "virtual") == 0 || strcmp(word, "override") == 0 ||
                    strcmp(word, "abstract") == 0 || strcmp(word, "async") == 0 ||
                    strcmp(word, "internal") == 0 || strcmp(word, "sealed") == 0 ||
                    strcmp(word, "readonly") == 0 || strcmp(word, "new") == 0) {
                    if (strcmp(word, "static") == 0) is_static_m = true;
                    if (strcmp(word, "async") == 0) is_async_m = true;
                    if (strcmp(word, "override") == 0) is_override = true;
                    last_doc_comment[0] = '\0'; /* don't clear yet */
                    continue;
                }

                /* word might be a type name; look for identifier after it */
                strncpy(type, word, sizeof(type) - 1);

                /* Handle generic type: Type<T> */
                if (p < end && *p == '<') {
                    int angle = 1;
                    p++;
                    while (p < end && angle > 0) {
                        if (*p == '<') angle++;
                        else if (*p == '>') angle--;
                        p++;
                    }
                }

                /* Handle array type: Type[] */
                if (p < end && *p == '[' && p + 1 < end && p[1] == ']') p += 2;

                while (p < end && (*p == ' ' || *p == '\t')) p++;

                if (p < end && (isalpha((unsigned char)*p) || *p == '_')) {
                    const char *name_start = p;
                    while (p < end && (isalnum((unsigned char)*p) || *p == '_')) p++;
                    int name_len = (int)(p - name_start) < 127 ? (int)(p - name_start) : 127;
                    char method_name[128];
                    memcpy(method_name, name_start, (size_t)name_len);
                    method_name[name_len] = '\0';

                    while (p < end && (*p == ' ' || *p == '\t')) p++;

                    if (p < end && *p == '(') {
                        /* it's a method - count parameters */
                        int param_count = 0;
                        char params_buf[256] = {0};
                        int pb_len = 0;
                        int paren = 1;
                        p++;
                        const char *params_start = p;
                        while (p < end && paren > 0) {
                            if (*p == '(') paren++;
                            else if (*p == ')') paren--;
                            else if (*p == ',' && paren == 1) param_count++;
                            if (*p == '\n') line_num++;
                            p++;
                        }
                        int params_len = (int)(p - params_start - 1);
                        if (params_len > 0) {
                            param_count++; /* at least one param */
                            if (params_len > 254) params_len = 254;
                            memcpy(params_buf, params_start, (size_t)params_len);
                            params_buf[params_len] = '\0';
                        }

                        /* build signature */
                        char sig[256];
                        snprintf(sig, sizeof(sig), "%s%s%s %s.%s(%s)",
                                is_static_m ? "static " : "",
                                is_async_m ? "async " : "",
                                type, current_class, method_name, params_buf);
                        add_symbol_ex(is, method_name, type, current_class, sig,
                                     filepath, last_doc_comment[0] ? last_doc_comment : NULL,
                                     ISYM_METHOD, line_num,
                                     (int)(name_start - content),
                                     is_static_m, param_count);

                        /* Also extract parameters as symbols */
                        /* Parse params_buf: "Type1 name1, Type2 name2" */
                        if (params_buf[0]) {
                            char *pp = params_buf;
                            while (*pp) {
                                while (*pp == ' ' || *pp == '\t') pp++;
                                /* skip modifiers (ref, out, params) */
                                if (strncmp(pp, "ref ", 4) == 0) pp += 4;
                                else if (strncmp(pp, "out ", 4) == 0) pp += 4;
                                else if (strncmp(pp, "params ", 7) == 0) pp += 7;
                                /* type */
                                while (*pp == ' ') pp++;
                                const char *pt = pp;
                                while (*pp && *pp != ' ' && *pp != ',' && *pp != '<') pp++;
                                if (*pp == '<') { while (*pp && *pp != '>') pp++; if (*pp) pp++; }
                                while (*pp == ' ') pp++;
                                /* name */
                                const char *pn = pp;
                                while (*pp && *pp != ',' && *pp != '=' && *pp != ' ') pp++;
                                int pn_len = (int)(pp - pn);
                                if (pn_len > 0 && pn_len < 127) {
                                    char pname[128];
                                    memcpy(pname, pn, (size_t)pn_len);
                                    pname[pn_len] = '\0';
                                    /* add as variable symbol */
                                    char ptype[64] = {0};
                                    int ptlen = (int)(pn - pt);
                                    while (ptlen > 0 && pt[ptlen-1] == ' ') ptlen--;
                                    if (ptlen > 0 && ptlen < 63) {
                                        memcpy(ptype, pt, (size_t)ptlen);
                                        ptype[ptlen] = '\0';
                                    }
                                    add_symbol(is, pname, ptype, current_class, NULL,
                                              filepath, ISYM_PARAMETER, line_num,
                                              (int)(name_start - content));
                                }
                                while (*pp && *pp != ',') pp++;
                                if (*pp == ',') pp++;
                            }
                        }
                    } else if (p < end && (*p == ';' || *p == '=' || *p == '{')) {
                        /* it's a field or property */
                        isym_kind_t fkind = ISYM_FIELD;
                        if (*p == '{') {
                            const char *peek = p + 1;
                            while (peek < end && (*peek == ' ' || *peek == '\t' || *peek == '\n')) peek++;
                            if (peek + 3 < end && memcmp(peek, "get", 3) == 0)
                                fkind = ISYM_PROPERTY;
                        }
                        char sig[256];
                        snprintf(sig, sizeof(sig), "%s%s %s.%s",
                                is_static_m ? "static " : "",
                                type, current_class, method_name);
                        add_symbol_ex(is, method_name, type, current_class, sig,
                                     filepath, last_doc_comment[0] ? last_doc_comment : NULL,
                                     fkind, line_num,
                                     (int)(name_start - content),
                                     is_static_m, 0);
                    } else {
                        p = saved_p;
                    }
                }
                (void)is_static_m;
                (void)is_async_m;
                (void)is_override;
            }

            /* Track local variable declarations: var x = ..., Type x = ... */
            if (brace_depth > class_brace + 1 && current_class[0]) {
                /* Inside a method body */
                if (strcmp(word, "var") == 0 || isupper((unsigned char)word[0])) {
                    const char *peek = p;
                    while (peek < end && (*peek == ' ' || *peek == '\t')) peek++;
                    if (peek < end && (isalpha((unsigned char)*peek) || *peek == '_')) {
                        const char *vn = peek;
                        while (peek < end && (isalnum((unsigned char)*peek) || *peek == '_')) peek++;
                        while (peek < end && (*peek == ' ' || *peek == '\t')) peek++;
                        if (peek < end && (*peek == '=' || *peek == ';')) {
                            int vn_len = (int)(peek - vn);
                            /* back up to remove trailing spaces */
                            while (vn_len > 0 && (vn[vn_len-1] == ' ' || vn[vn_len-1] == '\t')) vn_len--;
                            if (vn_len > 0 && vn_len < 127) {
                                char vname[128];
                                memcpy(vname, vn, (size_t)vn_len);
                                vname[vn_len] = '\0';
                                add_symbol(is, vname, word, current_class, NULL,
                                          filepath, ISYM_VARIABLE, line_num,
                                          (int)(vn - content));
                            }
                        }
                    }
                }
            }

            last_doc_comment[0] = '\0';
            continue;
        }

        last_doc_comment[0] = '\0';
        p++;
    }
}

/* Resolve the type of a variable from the symbol table */
const char *intel_resolve_type(intellisense_t *is, const char *var_name) {
    for (int i = is->symbol_count - 1; i >= 0; i--) {
        if ((is->symbols[i].kind == ISYM_VARIABLE ||
             is->symbols[i].kind == ISYM_PARAMETER ||
             is->symbols[i].kind == ISYM_FIELD ||
             is->symbols[i].kind == ISYM_PROPERTY) &&
            strcmp(is->symbols[i].name, var_name) == 0) {
            return is->symbols[i].type_name;
        }
    }
    return NULL;
}

/* Complete members of a given type */
int intel_complete_members(intellisense_t *is, const char *type_name,
                           const char *prefix) {
    is->completion_count = 0;
    is->completion_selected = 0;

    size_t plen = prefix ? strlen(prefix) : 0;

    /* Check user-defined type members first */
    for (int i = 0; i < is->symbol_count && is->completion_count < INTEL_MAX_COMPLETIONS; i++) {
        isym_t *sym = &is->symbols[i];
        if (sym->parent[0] == '\0') continue;
        if (strcmp(sym->parent, type_name) != 0) continue;
        if (sym->kind != ISYM_METHOD && sym->kind != ISYM_FIELD &&
            sym->kind != ISYM_PROPERTY && sym->kind != ISYM_ENUM_MEMBER)
            continue;

        if (plen > 0 && _strnicmp(sym->name, prefix, plen) != 0) continue;

        completion_t *c = &is->completions[is->completion_count++];
        strncpy(c->label, sym->name, sizeof(c->label) - 1);
        if (sym->kind == ISYM_METHOD)
            snprintf(c->insert_text, sizeof(c->insert_text), "%s(", sym->name);
        else
            strncpy(c->insert_text, sym->name, sizeof(c->insert_text) - 1);
        if (sym->signature[0])
            strncpy(c->detail, sym->signature, sizeof(c->detail) - 1);
        else
            snprintf(c->detail, sizeof(c->detail), "%s.%s : %s", type_name, sym->name, sym->type_name);
        strncpy(c->doc, sym->doc, sizeof(c->doc) - 1);
        c->kind = sym->kind;
        c->sort_priority = 0;
    }

    /* Check stdlib classes */
    for (int ci = 0; stdlib_classes[ci].class_name; ci++) {
        if (strcmp(type_name, stdlib_classes[ci].class_name) != 0) continue;
        for (int mi = 0; stdlib_classes[ci].methods[mi] && is->completion_count < INTEL_MAX_COMPLETIONS; mi++) {
            if (plen > 0 && _strnicmp(stdlib_classes[ci].methods[mi], prefix, plen) != 0) continue;
            completion_t *c = &is->completions[is->completion_count++];
            strncpy(c->label, stdlib_classes[ci].methods[mi], sizeof(c->label) - 1);
            snprintf(c->insert_text, sizeof(c->insert_text), "%s(", stdlib_classes[ci].methods[mi]);
            snprintf(c->detail, sizeof(c->detail), "%s.%s()", type_name, stdlib_classes[ci].methods[mi]);
            c->kind = ISYM_METHOD;
            c->sort_priority = 1;
        }
        break;
    }

    /* Console methods */
    if (strcmp(type_name, "Console") == 0) {
        for (int i = 0; console_methods[i] && is->completion_count < INTEL_MAX_COMPLETIONS; i++) {
            if (plen > 0 && _strnicmp(console_methods[i], prefix, plen) != 0) continue;
            completion_t *c = &is->completions[is->completion_count++];
            strncpy(c->label, console_methods[i], sizeof(c->label) - 1);
            snprintf(c->insert_text, sizeof(c->insert_text), "%s(", console_methods[i]);
            snprintf(c->detail, sizeof(c->detail), "Console.%s()", console_methods[i]);
            c->kind = ISYM_METHOD;
            c->sort_priority = 1;
        }
    }

    /* string instance methods */
    if (strcmp(type_name, "string") == 0 || strcmp(type_name, "String") == 0) {
        for (int i = 0; string_methods[i] && is->completion_count < INTEL_MAX_COMPLETIONS; i++) {
            if (plen > 0 && _strnicmp(string_methods[i], prefix, plen) != 0) continue;
            completion_t *c = &is->completions[is->completion_count++];
            strncpy(c->label, string_methods[i], sizeof(c->label) - 1);
            strncpy(c->insert_text, string_methods[i], sizeof(c->insert_text) - 1);
            snprintf(c->detail, sizeof(c->detail), "string.%s", string_methods[i]);
            c->kind = ISYM_METHOD;
            c->sort_priority = 1;
        }
    }

    /* List/Dict/StringBuilder instance methods (resolve via symbol type) */
    const char *resolved = intel_resolve_type(is, type_name);
    if (resolved) {
        if (strstr(resolved, "List") != NULL) {
            for (int i = 0; list_methods[i] && is->completion_count < INTEL_MAX_COMPLETIONS; i++) {
                if (plen > 0 && _strnicmp(list_methods[i], prefix, plen) != 0) continue;
                completion_t *c = &is->completions[is->completion_count++];
                strncpy(c->label, list_methods[i], sizeof(c->label) - 1);
                strncpy(c->insert_text, list_methods[i], sizeof(c->insert_text) - 1);
                snprintf(c->detail, sizeof(c->detail), "List.%s", list_methods[i]);
                c->kind = ISYM_METHOD;
                c->sort_priority = 1;
            }
        }
        if (strstr(resolved, "Dict") != NULL) {
            for (int i = 0; dict_methods[i] && is->completion_count < INTEL_MAX_COMPLETIONS; i++) {
                if (plen > 0 && _strnicmp(dict_methods[i], prefix, plen) != 0) continue;
                completion_t *c = &is->completions[is->completion_count++];
                strncpy(c->label, dict_methods[i], sizeof(c->label) - 1);
                strncpy(c->insert_text, dict_methods[i], sizeof(c->insert_text) - 1);
                snprintf(c->detail, sizeof(c->detail), "Dict.%s", dict_methods[i]);
                c->kind = ISYM_METHOD;
                c->sort_priority = 1;
            }
        }
        if (strstr(resolved, "StringBuilder") != NULL) {
            for (int i = 0; sb_methods[i] && is->completion_count < INTEL_MAX_COMPLETIONS; i++) {
                if (plen > 0 && _strnicmp(sb_methods[i], prefix, plen) != 0) continue;
                completion_t *c = &is->completions[is->completion_count++];
                strncpy(c->label, sb_methods[i], sizeof(c->label) - 1);
                strncpy(c->insert_text, sb_methods[i], sizeof(c->insert_text) - 1);
                snprintf(c->detail, sizeof(c->detail), "StringBuilder.%s", sb_methods[i]);
                c->kind = ISYM_METHOD;
                c->sort_priority = 1;
            }
        }
    }

    /* Enum member access */
    for (int i = 0; i < is->symbol_count && is->completion_count < INTEL_MAX_COMPLETIONS; i++) {
        if (is->symbols[i].kind == ISYM_ENUM && strcmp(is->symbols[i].name, type_name) == 0) {
            /* found enum type, list its members */
            for (int j = 0; j < is->symbol_count && is->completion_count < INTEL_MAX_COMPLETIONS; j++) {
                if (is->symbols[j].kind == ISYM_ENUM_MEMBER &&
                    strcmp(is->symbols[j].parent, type_name) == 0) {
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

    /* LINQ extension methods for List/IEnumerable/Array types */
    if (strcmp(type_name, "List") == 0 || strcmp(type_name, "IEnumerable") == 0 ||
        strcmp(type_name, "Array") == 0 || strstr(type_name, "List<") != NULL ||
        strstr(type_name, "IEnumerable<") != NULL) {
        static const char *linq_ext[] = {
            "Where", "Select", "SelectMany", "OrderBy", "OrderByDescending",
            "ThenBy", "GroupBy", "Join",
            "Take", "TakeWhile", "Skip", "SkipWhile",
            "First", "FirstOrDefault", "Last", "LastOrDefault",
            "Single", "SingleOrDefault", "ElementAt",
            "Any", "All", "Count", "Sum", "Min", "Max", "Average",
            "Distinct", "Union", "Intersect", "Except",
            "Concat", "Zip", "Aggregate", "Contains",
            "ToList", "ToArray", "ToDictionary", "ToHashSet",
            "Reverse", "OfType", "Cast", "AsEnumerable",
            NULL
        };
        for (int i = 0; linq_ext[i] && is->completion_count < INTEL_MAX_COMPLETIONS; i++) {
            if (plen > 0 && _strnicmp(linq_ext[i], prefix, plen) != 0) continue;
            /* Avoid duplicates from stdlib_classes */
            bool dup = false;
            for (int d = 0; d < is->completion_count; d++) {
                if (strcmp(is->completions[d].label, linq_ext[i]) == 0) { dup = true; break; }
            }
            if (dup) continue;
            completion_t *c = &is->completions[is->completion_count++];
            strncpy(c->label, linq_ext[i], sizeof(c->label) - 1);
            snprintf(c->insert_text, sizeof(c->insert_text), "%s(", linq_ext[i]);
            snprintf(c->detail, sizeof(c->detail), "%s.%s() (LINQ)", type_name, linq_ext[i]);
            c->kind = ISYM_METHOD;
            c->sort_priority = 2;
        }
    }

    is->completion_active = is->completion_count > 0;
    return is->completion_count;
}

/* Generate completions matching prefix */
int intel_complete(intellisense_t *is, const char *prefix,
                   const char *context_class) {
    is->completion_count = 0;
    is->completion_selected = 0;

    size_t plen = strlen(prefix);
    if (plen == 0) {
        is->completion_active = false;
        return 0;
    }

    /* If we have a context class (dot completion), delegate */
    if (context_class && context_class[0]) {
        return intel_complete_members(is, context_class, prefix);
    }

    /* Match snippets first (highest priority) */
    for (int i = 0; i < is->snippet_count && is->completion_count < INTEL_MAX_COMPLETIONS; i++) {
        if (_strnicmp(is->snippets[i].trigger, prefix, plen) != 0) continue;
        completion_t *c = &is->completions[is->completion_count++];
        snprintf(c->label, sizeof(c->label), "%s (snippet)", is->snippets[i].trigger);
        strncpy(c->insert_text, is->snippets[i].body, sizeof(c->insert_text) - 1);
        strncpy(c->detail, is->snippets[i].description, sizeof(c->detail) - 1);
        c->kind = ISYM_SNIPPET;
        c->sort_priority = -1;
    }

    /* match symbols */
    for (int i = 0; i < is->symbol_count && is->completion_count < INTEL_MAX_COMPLETIONS; i++) {
        isym_t *sym = &is->symbols[i];
        if (_strnicmp(sym->name, prefix, plen) != 0) continue;

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

    /* add matching keywords */
    for (int i = 0; builtin_keywords[i] && is->completion_count < INTEL_MAX_COMPLETIONS; i++) {
        if (_strnicmp(builtin_keywords[i], prefix, plen) != 0) continue;
        /* avoid duplicates with snippets */
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

    /* add matching types */
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

    is->completion_active = is->completion_count > 0;
    return is->completion_count;
}

hover_info_t intel_hover(intellisense_t *is, const char *word) {
    hover_info_t info = {0};

    for (int i = 0; i < is->symbol_count; i++) {
        if (strcmp(is->symbols[i].name, word) == 0) {
            if (is->symbols[i].signature[0])
                strncpy(info.text, is->symbols[i].signature, sizeof(info.text) - 1);
            else
                snprintf(info.text, sizeof(info.text), "%s : %s",
                        is->symbols[i].name, is->symbols[i].type_name);
            strncpy(info.doc, is->symbols[i].doc, sizeof(info.doc) - 1);
            info.valid = true;
            return info;
        }
    }

    /* check built-in types */
    for (int i = 0; builtin_types[i]; i++) {
        if (strcmp(builtin_types[i], word) == 0) {
            snprintf(info.text, sizeof(info.text), "type %s (built-in)", word);
            info.valid = true;
            return info;
        }
    }

    /* check keywords */
    for (int i = 0; builtin_keywords[i]; i++) {
        if (strcmp(builtin_keywords[i], word) == 0) {
            snprintf(info.text, sizeof(info.text), "keyword %s", word);
            info.valid = true;
            return info;
        }
    }

    return info;
}

goto_def_t intel_goto_def(intellisense_t *is, const char *word) {
    goto_def_t result = {0};

    for (int i = 0; i < is->symbol_count; i++) {
        if (strcmp(is->symbols[i].name, word) == 0) {
            /* prefer type/class definitions over usages */
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

/* Signature help: provide info about method parameters */
signature_info_t intel_signature_help(intellisense_t *is, const char *method_name,
                                      const char *class_context) {
    signature_info_t sig = {0};

    for (int i = 0; i < is->symbol_count; i++) {
        isym_t *sym = &is->symbols[i];
        if (sym->kind != ISYM_METHOD) continue;
        if (strcmp(sym->name, method_name) != 0) continue;
        if (class_context && class_context[0] && strcmp(sym->parent, class_context) != 0) continue;

        strncpy(sig.label, sym->signature, sizeof(sig.label) - 1);
        strncpy(sig.doc, sym->doc, sizeof(sig.doc) - 1);

        /* Parse parameters from signature: extract between ( and ) */
        const char *pstart = strchr(sym->signature, '(');
        const char *pend = pstart ? strchr(pstart, ')') : NULL;
        if (pstart && pend) {
            pstart++;
            char params_copy[256];
            int plen2 = (int)(pend - pstart);
            if (plen2 > 254) plen2 = 254;
            memcpy(params_copy, pstart, (size_t)plen2);
            params_copy[plen2] = '\0';

            /* Split by commas */
            char *tok = params_copy;
            while (*tok && sig.param_count < INTEL_MAX_PARAMS) {
                while (*tok == ' ') tok++;
                char *comma = strchr(tok, ',');
                int tlen = comma ? (int)(comma - tok) : (int)strlen(tok);
                if (tlen > 0) {
                    char param_str[128];
                    if (tlen > 127) tlen = 127;
                    memcpy(param_str, tok, (size_t)tlen);
                    param_str[tlen] = '\0';

                    /* split "Type name" */
                    char *space = strrchr(param_str, ' ');
                    if (space) {
                        *space = '\0';
                        strncpy(sig.params[sig.param_count].type, param_str,
                                sizeof(sig.params[sig.param_count].type) - 1);
                        strncpy(sig.params[sig.param_count].label, space + 1,
                                sizeof(sig.params[sig.param_count].label) - 1);
                    } else {
                        strncpy(sig.params[sig.param_count].label, param_str,
                                sizeof(sig.params[sig.param_count].label) - 1);
                    }
                    sig.param_count++;
                }
                if (comma) tok = comma + 1;
                else break;
            }
        }

        sig.valid = true;
        return sig;
    }

    return sig;
}

/* Find all references to a symbol */
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

/* --- Chain-call type resolution --- */

/* Method return type mapping for fluent APIs */
typedef struct {
    const char *class_name;
    const char *method_name;
    const char *return_type;  /* NULL means returns same type (fluent) */
} method_return_t;

static const method_return_t method_returns[] = {
    /* StringBuilder - all methods return StringBuilder (fluent) */
    {"StringBuilder", "Append",     "StringBuilder"},
    {"StringBuilder", "AppendLine", "StringBuilder"},
    {"StringBuilder", "Insert",     "StringBuilder"},
    {"StringBuilder", "Remove",     "StringBuilder"},
    {"StringBuilder", "Replace",    "StringBuilder"},
    {"StringBuilder", "Clear",      "StringBuilder"},
    {"StringBuilder", "ToString",   "string"},

    /* string methods */
    {"string", "Substring",   "string"},
    {"string", "Replace",     "string"},
    {"string", "Trim",        "string"},
    {"string", "TrimStart",   "string"},
    {"string", "TrimEnd",     "string"},
    {"string", "ToLower",     "string"},
    {"string", "ToUpper",     "string"},
    {"string", "PadLeft",     "string"},
    {"string", "PadRight",    "string"},
    {"string", "Insert",      "string"},
    {"string", "Remove",      "string"},
    {"string", "Split",       "string[]"},
    {"string", "Contains",    "bool"},
    {"string", "StartsWith",  "bool"},
    {"string", "EndsWith",    "bool"},
    {"string", "IndexOf",     "int"},
    {"string", "LastIndexOf", "int"},
    {"string", "Length",      "int"},

    /* List<T> - LINQ-style methods */
    {"List", "Where",         "List"},
    {"List", "Select",        "List"},
    {"List", "OrderBy",       "List"},
    {"List", "OrderByDescending", "List"},
    {"List", "ThenBy",        "List"},
    {"List", "Take",          "List"},
    {"List", "Skip",          "List"},
    {"List", "Distinct",      "List"},
    {"List", "Reverse",       "List"},
    {"List", "ToList",        "List"},
    {"List", "ToArray",       "Array"},
    {"List", "First",         "object"},
    {"List", "Last",          "object"},
    {"List", "FirstOrDefault","object"},
    {"List", "LastOrDefault", "object"},
    {"List", "Count",         "int"},
    {"List", "Any",           "bool"},
    {"List", "All",           "bool"},
    {"List", "Sum",           "int"},
    {"List", "Max",           "int"},
    {"List", "Min",           "int"},
    {"List", "Average",       "double"},

    /* IEnumerable / Queryable (same as List for LINQ) */
    {"IEnumerable", "Where",    "IEnumerable"},
    {"IEnumerable", "Select",   "IEnumerable"},
    {"IEnumerable", "OrderBy",  "IEnumerable"},
    {"IEnumerable", "Take",     "IEnumerable"},
    {"IEnumerable", "Skip",     "IEnumerable"},
    {"IEnumerable", "Distinct", "IEnumerable"},
    {"IEnumerable", "ToList",   "List"},
    {"IEnumerable", "ToArray",  "Array"},
    {"IEnumerable", "Count",    "int"},
    {"IEnumerable", "Any",      "bool"},
    {"IEnumerable", "First",    "object"},

    /* Dict */
    {"Dict", "Keys",    "List"},
    {"Dict", "Values",  "List"},
    {"Dict", "Count",   "int"},
    {"Dict", "ContainsKey", "bool"},

    /* HttpClient (fluent) */
    {"HttpClient", "SetHeader",  "HttpClient"},
    {"HttpClient", "SetTimeout", "HttpClient"},
    {"HttpClient", "Get",        "HttpResponse"},
    {"HttpClient", "Post",       "HttpResponse"},

    /* HttpResponse */
    {"HttpResponse", "GetBody",   "string"},
    {"HttpResponse", "GetStatus", "int"},
    {"HttpResponse", "GetHeader", "string"},

    /* Task (async chaining) */
    {"Task", "ContinueWith", "Task"},
    {"Task", "Then",         "Task"},
    {"Task", "Result",       "object"},

    {NULL, NULL, NULL}
};

/* LINQ extension methods available on any List/IEnumerable */
static const char *linq_methods[] = {
    "Where", "Select", "SelectMany", "OrderBy", "OrderByDescending",
    "ThenBy", "ThenByDescending", "GroupBy", "Join",
    "Take", "TakeWhile", "Skip", "SkipWhile",
    "First", "FirstOrDefault", "Last", "LastOrDefault",
    "Single", "SingleOrDefault", "ElementAt",
    "Any", "All", "Count", "Sum", "Min", "Max", "Average",
    "Distinct", "Union", "Intersect", "Except",
    "Concat", "Zip", "Aggregate", "Contains",
    "ToList", "ToArray", "ToDictionary", "ToHashSet",
    "Reverse", "OfType", "Cast",
    NULL
};

const char *intel_resolve_method_return(intellisense_t *is, const char *type_name,
                                        const char *method_name) {
    /* Check static return type table */
    for (int i = 0; method_returns[i].class_name; i++) {
        if (strcmp(method_returns[i].class_name, type_name) == 0 &&
            strcmp(method_returns[i].method_name, method_name) == 0) {
            return method_returns[i].return_type;
        }
    }

    /* Check user-defined symbols for method return type */
    for (int i = 0; i < is->symbol_count; i++) {
        isym_t *sym = &is->symbols[i];
        if (sym->kind == ISYM_METHOD &&
            strcmp(sym->parent, type_name) == 0 &&
            strcmp(sym->name, method_name) == 0) {
            if (sym->type_name[0]) return sym->type_name;
            /* If return type is same as parent class, it's fluent */
            return type_name;
        }
    }

    /* LINQ methods on List/IEnumerable return List */
    if (strcmp(type_name, "List") == 0 || strcmp(type_name, "IEnumerable") == 0 ||
        strstr(type_name, "List<") != NULL) {
        for (int i = 0; linq_methods[i]; i++) {
            if (strcmp(method_name, linq_methods[i]) == 0) {
                if (strcmp(method_name, "Count") == 0 || strcmp(method_name, "Sum") == 0 ||
                    strcmp(method_name, "Min") == 0 || strcmp(method_name, "Max") == 0)
                    return "int";
                if (strcmp(method_name, "Any") == 0 || strcmp(method_name, "All") == 0 ||
                    strcmp(method_name, "Contains") == 0)
                    return "bool";
                if (strcmp(method_name, "Average") == 0)
                    return "double";
                if (strcmp(method_name, "First") == 0 || strcmp(method_name, "Last") == 0 ||
                    strcmp(method_name, "Single") == 0)
                    return "object";
                if (strcmp(method_name, "ToArray") == 0)
                    return "Array";
                if (strcmp(method_name, "ToDictionary") == 0)
                    return "Dict";
                return "List";
            }
        }
    }

    /* Properties return their type */
    for (int i = 0; i < is->symbol_count; i++) {
        isym_t *sym = &is->symbols[i];
        if ((sym->kind == ISYM_PROPERTY || sym->kind == ISYM_FIELD) &&
            strcmp(sym->parent, type_name) == 0 &&
            strcmp(sym->name, method_name) == 0) {
            return sym->type_name;
        }
    }

    return NULL;
}

const char *intel_resolve_chain(intellisense_t *is, const char *chain,
                                char *final_member, size_t final_cap) {
    static char resolved_type[128];
    final_member[0] = '\0';
    resolved_type[0] = '\0';

    if (!chain || !chain[0]) return NULL;

    /* Parse chain: split by '.' but skip content inside parentheses */
    char parts[16][128];
    int part_count = 0;
    const char *p = chain;
    int paren_depth = 0;
    int angle_depth = 0;
    int part_start = 0;
    int ci = 0;

    while (chain[ci] && part_count < 16) {
        char ch = chain[ci];
        if (ch == '(') paren_depth++;
        else if (ch == ')') { if (paren_depth > 0) paren_depth--; }
        else if (ch == '<') angle_depth++;
        else if (ch == '>') { if (angle_depth > 0) angle_depth--; }
        else if (ch == '.' && paren_depth == 0 && angle_depth == 0) {
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
    /* Last part (after final dot) is the prefix being typed */
    int remaining_len = ci - part_start;
    if (remaining_len >= 0 && remaining_len < (int)final_cap) {
        memcpy(final_member, chain + part_start, (size_t)remaining_len);
        final_member[remaining_len] = '\0';
    }

    if (part_count == 0) return NULL;

    /* Resolve the first part: it's a variable name or type name */
    /* Strip any method call parens from first part: "myVar" or "GetList()" */
    char first_name[128];
    strncpy(first_name, parts[0], 127);
    first_name[127] = '\0';
    char *paren = strchr(first_name, '(');
    if (paren) *paren = '\0';

    /* Resolve type of first element */
    const char *current_type = intel_resolve_type(is, first_name);
    if (!current_type || !current_type[0]) {
        /* Maybe it's a type name directly (static call) */
        for (int i = 0; i < is->symbol_count; i++) {
            if ((is->symbols[i].kind == ISYM_CLASS || is->symbols[i].kind == ISYM_STRUCT) &&
                strcmp(is->symbols[i].name, first_name) == 0) {
                current_type = first_name;
                break;
            }
        }
        /* Check stdlib classes */
        if (!current_type) {
            for (int ci2 = 0; stdlib_classes[ci2].class_name; ci2++) {
                if (strcmp(first_name, stdlib_classes[ci2].class_name) == 0) {
                    current_type = stdlib_classes[ci2].class_name;
                    break;
                }
            }
        }
        if (!current_type) return NULL;
    }

    /* Strip generic notation: "List<int>" -> "List" */
    strncpy(resolved_type, current_type, 127);
    resolved_type[127] = '\0';
    char *angle = strchr(resolved_type, '<');
    if (angle) *angle = '\0';

    /* Walk the chain: resolve each subsequent member/method call */
    for (int pi = 1; pi < part_count; pi++) {
        char member_name[128];
        strncpy(member_name, parts[pi], 127);
        member_name[127] = '\0';
        /* Strip parens: "Where(...)" -> "Where" */
        char *mp = strchr(member_name, '(');
        if (mp) *mp = '\0';
        /* Trim trailing spaces */
        int mlen = (int)strlen(member_name);
        while (mlen > 0 && member_name[mlen-1] == ' ') member_name[--mlen] = '\0';

        if (!member_name[0]) continue;

        const char *next_type = intel_resolve_method_return(is, resolved_type, member_name);
        if (next_type) {
            strncpy(resolved_type, next_type, 127);
            resolved_type[127] = '\0';
            angle = strchr(resolved_type, '<');
            if (angle) *angle = '\0';
        } else {
            /* Unknown method - try treating as same type (fluent assumption) */
            /* Keep current type */
        }
    }

    return resolved_type[0] ? resolved_type : NULL;
}

/* --- Project-wide indexing --- */

#ifdef _WIN32

static void index_directory_recursive(intellisense_t *is, const char *dir_path) {
    char search_path[1024];
    snprintf(search_path, sizeof(search_path), "%s\\*", dir_path);

    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA(search_path, &fd);
    if (hFind == INVALID_HANDLE_VALUE) return;

    do {
        if (fd.cFileName[0] == '.') continue;

        char full_path[1024];
        snprintf(full_path, sizeof(full_path), "%s\\%s", dir_path, fd.cFileName);

        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            /* recurse into subdirectories (skip bin/obj/build/.git) */
            if (strcmp(fd.cFileName, "bin") != 0 &&
                strcmp(fd.cFileName, "obj") != 0 &&
                strcmp(fd.cFileName, "build") != 0 &&
                strcmp(fd.cFileName, ".git") != 0) {
                index_directory_recursive(is, full_path);
            }
        } else {
            /* check if it's a .zan file */
            size_t name_len = strlen(fd.cFileName);
            if (name_len > 4 && strcmp(fd.cFileName + name_len - 4, ".zan") == 0) {
                /* read file and parse it */
                HANDLE hFile = CreateFileA(full_path, GENERIC_READ, FILE_SHARE_READ,
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
    } while (FindNextFileA(hFind, &fd));
    FindClose(hFind);
}

void intel_index_project(intellisense_t *is, const char *project_root) {
    if (!project_root || !project_root[0]) return;
    index_directory_recursive(is, project_root);
}

#else /* Non-Windows: use dirent.h */
static void index_directory_recursive(intellisense_t *is, const char *dir_path) {
    DIR *dir = opendir(dir_path);
    if (!dir) return;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue;

        char full_path[1024];
        snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entry->d_name);

        struct stat st;
        if (stat(full_path, &st) != 0) continue;

        if (S_ISDIR(st.st_mode)) {
            if (strcmp(entry->d_name, "bin") != 0 &&
                strcmp(entry->d_name, "obj") != 0 &&
                strcmp(entry->d_name, "build") != 0 &&
                strcmp(entry->d_name, ".git") != 0) {
                index_directory_recursive(is, full_path);
            }
        } else if (S_ISREG(st.st_mode)) {
            size_t name_len = strlen(entry->d_name);
            if (name_len > 4 && strcmp(entry->d_name + name_len - 4, ".zan") == 0) {
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
        if (!filepaths[i]) continue;

        /* Read the file */
        FILE *f = fopen(filepaths[i], "rb");
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

/* --- Auto-using management --- */

/* Standard library namespace -> types mapping */
typedef struct {
    const char *ns;
    const char *types[32];
} ns_types_t;

static const ns_types_t stdlib_namespace_map[] = {
    {"System", {"Console", "Environment", "Math", "Convert", "String",
                "Object", "Exception", "Array", "Type", "GC",
                "Nullable", "Tuple", "Func", "Action", NULL}},
    {"System.IO", {"File", "Path", "Directory", "StreamReader", "StreamWriter",
                   "FileStream", "MemoryStream", "BinaryReader", "BinaryWriter", NULL}},
    {"System.Collections", {"List", "Dict", "Queue", "Stack", "HashSet",
                            "LinkedList", "SortedList", NULL}},
    {"System.Text", {"StringBuilder", "Encoding", "Regex", NULL}},
    {"System.Threading", {"Thread", "Mutex", "Semaphore", "Task", "Timer",
                           "AtomicInt", "SharedTable", NULL}},
    {"System.Json", {"JsonParser", "JsonBuilder", "JsonValue", NULL}},
    {"System.Net", {"HttpClient", "HttpRequest", "HttpResponse", "Socket", "TcpClient", NULL}},
    {"System.Diagnostics", {"Stopwatch", "Process", "Debug", "Trace", NULL}},
    {"System.Linq", {"Enumerable", "Queryable", NULL}},
    {NULL, {NULL}}
};

/* Find which namespace a type belongs to */
static const char *find_namespace_for_type(const char *type_name) {
    for (int i = 0; stdlib_namespace_map[i].ns; i++) {
        for (int j = 0; stdlib_namespace_map[i].types[j]; j++) {
            if (strcmp(type_name, stdlib_namespace_map[i].types[j]) == 0)
                return stdlib_namespace_map[i].ns;
        }
    }
    return NULL;
}

using_analysis_t intel_analyze_usings(intellisense_t *is, const char *content, size_t len) {
    using_analysis_t result;
    memset(&result, 0, sizeof(result));

    if (!content || len == 0) return result;

    const char *p = content;
    const char *end = content + len;
    int line_num = 0;
    bool in_using_block = true;

    /* Phase 1: Extract all "using" statements from the top of the file */
    while (p < end && in_using_block) {
        while (p < end && (*p == ' ' || *p == '\t' || *p == '\r')) p++;
        if (p >= end) break;
        if (*p == '\n') { line_num++; p++; continue; }

        /* skip comments */
        if (p + 1 < end && p[0] == '/' && p[1] == '/') {
            while (p < end && *p != '\n') p++;
            continue;
        }

        /* look for "using" keyword */
        if (p + 5 < end && memcmp(p, "using", 5) == 0 &&
            !isalnum((unsigned char)p[5]) && p[5] != '_') {
            p += 5;
            while (p < end && (*p == ' ' || *p == '\t')) p++;

            /* extract namespace name */
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

    /* Phase 2: Scan the file body for type references and check which
     * namespaces are actually used */
    p = content;
    line_num = 0;
    while (p < end) {
        if (*p == '\n') { line_num++; p++; continue; }
        if (!isalpha((unsigned char)*p) && *p != '_') { p++; continue; }

        /* extract identifier */
        const char *word_start = p;
        while (p < end && (isalnum((unsigned char)*p) || *p == '_')) p++;
        int wlen = (int)(p - word_start);
        if (wlen >= 128) continue;

        char word[128];
        memcpy(word, word_start, (size_t)wlen);
        word[wlen] = '\0';

        /* skip common keywords */
        if (strcmp(word, "using") == 0 || strcmp(word, "namespace") == 0 ||
            strcmp(word, "class") == 0 || strcmp(word, "struct") == 0 ||
            strcmp(word, "if") == 0 || strcmp(word, "else") == 0 ||
            strcmp(word, "for") == 0 || strcmp(word, "while") == 0 ||
            strcmp(word, "return") == 0 || strcmp(word, "void") == 0 ||
            strcmp(word, "int") == 0 || strcmp(word, "bool") == 0 ||
            strcmp(word, "string") == 0 || strcmp(word, "var") == 0)
            continue;

        /* Check if this type belongs to a namespace */
        const char *ns = find_namespace_for_type(word);
        if (!ns) continue;

        /* Mark the namespace as used if it's in the using list */
        bool found_using = false;
        for (int i = 0; i < result.using_count; i++) {
            if (strcmp(result.usings[i].namespace_name, ns) == 0) {
                result.usings[i].is_used = true;
                found_using = true;
                break;
            }
        }

        /* If not in the using list, it's a missing using */
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

    /* Phase 3: Identify unused usings */
    for (int i = 0; i < result.using_count; i++) {
        if (!result.usings[i].is_used) {
            /* Also check if any symbol in the indexed symbols uses this namespace */
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

/* Sort comparison for using statements (alphabetical) */
static int using_sort_cmp(const void *a, const void *b) {
    const using_entry_t *ua = (const using_entry_t *)a;
    const using_entry_t *ub = (const using_entry_t *)b;
    /* System.* namespaces come first */
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

    /* Build the new using block:
     * 1. Keep used usings
     * 2. Add missing usings
     * 3. Sort alphabetically (System.* first) */
    using_entry_t new_usings[INTEL_MAX_USINGS * 2];
    int new_count = 0;

    /* Add kept usings */
    for (int i = 0; i < analysis.using_count; i++) {
        /* skip if it's in the unused list */
        bool is_unused = false;
        for (int j = 0; j < analysis.unused_count; j++) {
            if (analysis.unused_indices[j] == i) { is_unused = true; break; }
        }
        if (!is_unused) {
            new_usings[new_count] = analysis.usings[i];
            new_count++;
        }
    }

    /* Add missing usings */
    for (int i = 0; i < analysis.missing_count; i++) {
        using_entry_t *u = &new_usings[new_count++];
        memset(u, 0, sizeof(*u));
        strncpy(u->namespace_name, analysis.missing_usings[i], 127);
        u->is_used = true;
    }

    /* Sort the using list */
    qsort(new_usings, (size_t)new_count, sizeof(using_entry_t), using_sort_cmp);

    /* Reconstruct the file:
     * - Replace all lines from the first using to the last using with new usings
     * - Keep everything else as-is */

    /* Find the range of using statements in the original content */
    int first_using_line = -1, last_using_line = -1;
    for (int i = 0; i < analysis.using_count; i++) {
        if (first_using_line < 0 || analysis.usings[i].line < first_using_line)
            first_using_line = analysis.usings[i].line;
        if (analysis.usings[i].line > last_using_line)
            last_using_line = analysis.usings[i].line;
    }

    if (first_using_line < 0) {
        /* No existing usings - insert at top after any initial comments */
        first_using_line = 0;
        last_using_line = -1;
    }

    /* Find byte offsets for the using range */
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

    /* Build new file content */
    size_t buf_cap = len + (size_t)new_count * 140 + 64;
    char *buf = (char *)malloc(buf_cap);
    if (!buf) { *out_len = 0; return NULL; }
    size_t buf_len = 0;

    /* Copy content before usings */
    memcpy(buf, content, using_start_off);
    buf_len = using_start_off;

    /* Write new using block */
    for (int i = 0; i < new_count; i++) {
        int n = snprintf(buf + buf_len, buf_cap - buf_len, "using %s;\n",
                        new_usings[i].namespace_name);
        if (n > 0) buf_len += (size_t)n;
    }

    /* Add blank line after usings if there isn't one */
    if (buf_len > 0 && buf[buf_len - 1] == '\n' && using_end_off < len &&
        content[using_end_off] != '\n') {
        buf[buf_len++] = '\n';
    }

    /* Copy content after usings */
    size_t remaining = len - using_end_off;
    if (remaining > 0) {
        memcpy(buf + buf_len, content + using_end_off, remaining);
        buf_len += remaining;
    }
    buf[buf_len] = '\0';

    *out_len = buf_len;
    return buf;
}
