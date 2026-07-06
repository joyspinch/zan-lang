/* intellisense.c -- Code intelligence implementation.
 *
 * Lightweight symbol extraction using simple lexical scanning.
 * Parses Zan source files to extract classes, methods, fields, etc.
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
    {"Encoding", {"IntToString", "ParseInt", "ParseDouble", "GetByteCount", NULL}},
    {"Convert", {"ToInt32", "ToString", NULL}},
    {"Math", {"Sqrt", "Abs", "Max", "Min", "Pow", "Floor", "Ceiling", NULL}},
    {"Environment", {"ArgCount", "ArgAt", NULL}},
    {NULL, {NULL}}
};

/* List<T> instance methods */
static const char *list_methods[] = {
    "Add", "Clear", "RemoveAt", "IndexOf", "Contains", "Insert", "Reverse",
    "Count", NULL
};

/* Dict<K,V> instance methods */
static const char *dict_methods[] = {
    "Add", "ContainsKey", "Remove", "Clear", "Count", NULL
};

/* StringBuilder instance methods */
static const char *sb_methods[] = {
    "Append", "ToString", "Clear", "Length", NULL
};

void intel_init(intellisense_t *is) {
    memset(is, 0, sizeof(intellisense_t));
    is->completion_selected = -1;
}

void intel_clear(intellisense_t *is) {
    is->symbol_count = 0;
}

static void add_symbol(intellisense_t *is, const char *name,
                       const char *type_name, const char *parent,
                       const char *signature, const char *file,
                       isym_kind_t kind, int line, int col) {
    if (is->symbol_count >= INTEL_MAX_SYMBOLS) return;
    isym_t *sym = &is->symbols[is->symbol_count++];
    strncpy(sym->name, name, sizeof(sym->name) - 1);
    strncpy(sym->type_name, type_name ? type_name : "", sizeof(sym->type_name) - 1);
    strncpy(sym->parent, parent ? parent : "", sizeof(sym->parent) - 1);
    strncpy(sym->signature, signature ? signature : "", sizeof(sym->signature) - 1);
    strncpy(sym->file, file ? file : "", sizeof(sym->file) - 1);
    sym->kind = kind;
    sym->line = line;
    sym->col = col;
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

    if (!content || len == 0) return;

    char current_class[128] = {0};
    char current_ns[128] = {0};
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

        /* skip comments */
        if (p + 1 < end && p[0] == '/' && p[1] == '/') {
            while (p < end && *p != '\n') p++;
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

                add_symbol(is, current_class, NULL, current_ns, NULL, filepath,
                          kind, line_num, (int)(name - content));
                continue;
            }

            /* method detection: type name(...) pattern */
            /* look ahead for ( to detect method or field */
            if (current_class[0] && brace_depth == class_brace + 1) {
                /* skip modifiers */
                bool is_static = false, is_async = false;
                const char *saved_p = p;
                char type[64] = {0};

                if (strcmp(word, "static") == 0 || strcmp(word, "public") == 0 ||
                    strcmp(word, "private") == 0 || strcmp(word, "protected") == 0 ||
                    strcmp(word, "virtual") == 0 || strcmp(word, "override") == 0 ||
                    strcmp(word, "abstract") == 0 || strcmp(word, "async") == 0) {
                    if (strcmp(word, "static") == 0) is_static = true;
                    if (strcmp(word, "async") == 0) is_async = true;
                    /* continue scanning modifiers and type+name */
                    continue;
                }

                /* word might be a type name; look for identifier after it */
                strncpy(type, word, sizeof(type) - 1);
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
                        /* it's a method */
                        char sig[256];
                        snprintf(sig, sizeof(sig), "%s%s%s %s.%s(...)",
                                is_static ? "static " : "",
                                is_async ? "async " : "",
                                type, current_class, method_name);
                        add_symbol(is, method_name, type, current_class, sig,
                                  filepath, ISYM_METHOD, line_num,
                                  (int)(name_start - content));
                        /* skip to end of parameter list */
                        int paren = 1;
                        p++;
                        while (p < end && paren > 0) {
                            if (*p == '(') paren++;
                            else if (*p == ')') paren--;
                            if (*p == '\n') line_num++;
                            p++;
                        }
                    } else if (p < end && (*p == ';' || *p == '=' || *p == '{')) {
                        /* it's a field or property */
                        isym_kind_t fkind = ISYM_FIELD;
                        /* check for { get; set; } pattern */
                        if (*p == '{') {
                            const char *peek = p + 1;
                            while (peek < end && (*peek == ' ' || *peek == '\t' || *peek == '\n')) peek++;
                            if (peek + 3 < end && memcmp(peek, "get", 3) == 0)
                                fkind = ISYM_PROPERTY;
                        }
                        char sig[256];
                        snprintf(sig, sizeof(sig), "%s %s.%s",
                                type, current_class, method_name);
                        add_symbol(is, method_name, type, current_class, sig,
                                  filepath, fkind, line_num,
                                  (int)(name_start - content));
                    } else {
                        p = saved_p;
                    }
                }
                (void)is_static;
                (void)is_async;
            }
            continue;
        }

        p++;
    }
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

    /* match symbols */
    for (int i = 0; i < is->symbol_count && is->completion_count < INTEL_MAX_COMPLETIONS; i++) {
        isym_t *sym = &is->symbols[i];

        /* prefix match (case-insensitive) */
        if (_strnicmp(sym->name, prefix, plen) != 0) continue;

        /* if in a class context, prefer same-class symbols */
        completion_t *c = &is->completions[is->completion_count++];
        strncpy(c->label, sym->name, sizeof(c->label) - 1);
        strncpy(c->insert_text, sym->name, sizeof(c->insert_text) - 1);

        if (sym->signature[0])
            strncpy(c->detail, sym->signature, sizeof(c->detail) - 1);
        else if (sym->type_name[0])
            snprintf(c->detail, sizeof(c->detail), "%s : %s", sym->name, sym->type_name);
        else
            strncpy(c->detail, sym->name, sizeof(c->detail) - 1);

        c->kind = sym->kind;
    }

    /* add matching keywords */
    for (int i = 0; builtin_keywords[i] && is->completion_count < INTEL_MAX_COMPLETIONS; i++) {
        if (_strnicmp(builtin_keywords[i], prefix, plen) != 0) continue;
        completion_t *c = &is->completions[is->completion_count++];
        strncpy(c->label, builtin_keywords[i], sizeof(c->label) - 1);
        strncpy(c->insert_text, builtin_keywords[i], sizeof(c->insert_text) - 1);
        snprintf(c->detail, sizeof(c->detail), "keyword");
        c->kind = ISYM_KEYWORD;
    }

    /* add matching types */
    for (int i = 0; builtin_types[i] && is->completion_count < INTEL_MAX_COMPLETIONS; i++) {
        if (_strnicmp(builtin_types[i], prefix, plen) != 0) continue;
        /* skip if already added as keyword */
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
    }

    /* Console methods if prefix matches after "Console." context */
    if (context_class && strcmp(context_class, "Console") == 0) {
        for (int i = 0; console_methods[i] && is->completion_count < INTEL_MAX_COMPLETIONS; i++) {
            if (_strnicmp(console_methods[i], prefix, plen) != 0) continue;
            completion_t *c = &is->completions[is->completion_count++];
            strncpy(c->label, console_methods[i], sizeof(c->label) - 1);
            strncpy(c->insert_text, console_methods[i], sizeof(c->insert_text) - 1);
            snprintf(c->detail, sizeof(c->detail), "Console.%s()", console_methods[i]);
            c->kind = ISYM_METHOD;
        }
    }

    /* Stdlib static class methods (File.xxx, Path.xxx, JsonParser.xxx, etc.) */
    if (context_class) {
        for (int ci = 0; stdlib_classes[ci].class_name; ci++) {
            if (strcmp(context_class, stdlib_classes[ci].class_name) != 0) continue;
            for (int mi = 0; stdlib_classes[ci].methods[mi] && is->completion_count < INTEL_MAX_COMPLETIONS; mi++) {
                if (plen > 0 && _strnicmp(stdlib_classes[ci].methods[mi], prefix, plen) != 0) continue;
                completion_t *c = &is->completions[is->completion_count++];
                strncpy(c->label, stdlib_classes[ci].methods[mi], sizeof(c->label) - 1);
                strncpy(c->insert_text, stdlib_classes[ci].methods[mi], sizeof(c->insert_text) - 1);
                snprintf(c->detail, sizeof(c->detail), "%s.%s()", context_class, stdlib_classes[ci].methods[mi]);
                c->kind = ISYM_METHOD;
            }
            break;
        }
    }

    /* List<T> instance method completions */
    if (context_class) {
        /* check if context looks like a List variable — heuristic: look up in symbols */
        for (int si = 0; si < is->symbol_count; si++) {
            if (is->symbols[si].kind == ISYM_VARIABLE &&
                strcmp(is->symbols[si].name, context_class) == 0 &&
                strstr(is->symbols[si].type_name, "List") != NULL) {
                for (int mi = 0; list_methods[mi] && is->completion_count < INTEL_MAX_COMPLETIONS; mi++) {
                    if (plen > 0 && _strnicmp(list_methods[mi], prefix, plen) != 0) continue;
                    completion_t *c = &is->completions[is->completion_count++];
                    strncpy(c->label, list_methods[mi], sizeof(c->label) - 1);
                    strncpy(c->insert_text, list_methods[mi], sizeof(c->insert_text) - 1);
                    snprintf(c->detail, sizeof(c->detail), "List.%s", list_methods[mi]);
                    c->kind = ISYM_METHOD;
                }
                break;
            }
            /* Dict<K,V> instance methods */
            if (is->symbols[si].kind == ISYM_VARIABLE &&
                strcmp(is->symbols[si].name, context_class) == 0 &&
                strstr(is->symbols[si].type_name, "Dict") != NULL) {
                for (int mi = 0; dict_methods[mi] && is->completion_count < INTEL_MAX_COMPLETIONS; mi++) {
                    if (plen > 0 && _strnicmp(dict_methods[mi], prefix, plen) != 0) continue;
                    completion_t *c = &is->completions[is->completion_count++];
                    strncpy(c->label, dict_methods[mi], sizeof(c->label) - 1);
                    strncpy(c->insert_text, dict_methods[mi], sizeof(c->insert_text) - 1);
                    snprintf(c->detail, sizeof(c->detail), "Dict.%s", dict_methods[mi]);
                    c->kind = ISYM_METHOD;
                }
                break;
            }
            /* StringBuilder instance methods */
            if (is->symbols[si].kind == ISYM_VARIABLE &&
                strcmp(is->symbols[si].name, context_class) == 0 &&
                strstr(is->symbols[si].type_name, "StringBuilder") != NULL) {
                for (int mi = 0; sb_methods[mi] && is->completion_count < INTEL_MAX_COMPLETIONS; mi++) {
                    if (plen > 0 && _strnicmp(sb_methods[mi], prefix, plen) != 0) continue;
                    completion_t *c = &is->completions[is->completion_count++];
                    strncpy(c->label, sb_methods[mi], sizeof(c->label) - 1);
                    strncpy(c->insert_text, sb_methods[mi], sizeof(c->insert_text) - 1);
                    snprintf(c->detail, sizeof(c->detail), "StringBuilder.%s", sb_methods[mi]);
                    c->kind = ISYM_METHOD;
                }
                break;
            }
        }
    }

    is->completion_active = is->completion_count > 0;
    return is->completion_count;

    (void)context_class;
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
            strncpy(result.file, is->symbols[i].file, sizeof(result.file) - 1);
            result.line = is->symbols[i].line;
            result.col = is->symbols[i].col;
            result.found = true;
            return result;
        }
    }

    return result;
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
}
