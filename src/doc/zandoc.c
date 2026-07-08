/* zandoc.c -- Zan Documentation Generator.
 *
 * Extracts /// XML doc comments from .zan source files and generates
 * HTML or Markdown documentation.
 *
 * Usage:
 *   zandoc <file.zan> [--html|--md] [-o output]
 *   zandoc <dir/> [--html|--md] [-o output_dir]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>

#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#define mkdir_p(d) _mkdir(d)
#else
#include <sys/stat.h>
#define mkdir_p(d) mkdir(d, 0755)
#endif

#define MAX_MEMBERS 512
#define MAX_DOC 4096

typedef enum {
    DOC_CLASS,
    DOC_STRUCT,
    DOC_ENUM,
    DOC_INTERFACE,
    DOC_METHOD,
    DOC_FIELD,
    DOC_PROPERTY
} doc_kind_t;

typedef struct {
    char name[128];
    char parent[128];
    char signature[512];
    char summary[1024];
    char returns[256];
    char params[8][256];
    int param_count;
    doc_kind_t kind;
    int line;
    char access[32];
} doc_member_t;

typedef struct {
    doc_member_t members[MAX_MEMBERS];
    int count;
    char namespace_name[256];
    char file_name[512];
} doc_file_t;

static char *read_file(const char *path, long *out_size) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = (char *)malloc((size_t)sz + 1);
    fread(buf, 1, (size_t)sz, f);
    buf[sz] = 0;
    fclose(f);
    if (out_size) *out_size = sz;
    return buf;
}

/* Extract text between <tag> and </tag> */
static bool extract_tag(const char *line, const char *tag, char *out, int max) {
    char open[64], close[64];
    snprintf(open, sizeof(open), "<%s>", tag);
    snprintf(close, sizeof(close), "</%s>", tag);
    const char *start = strstr(line, open);
    if (!start) return false;
    start += strlen(open);
    const char *end = strstr(start, close);
    if (!end) {
        int len = (int)strlen(start);
        if (len >= max) len = max - 1;
        memcpy(out, start, (size_t)len);
        out[len] = 0;
    } else {
        int len = (int)(end - start);
        if (len >= max) len = max - 1;
        memcpy(out, start, (size_t)len);
        out[len] = 0;
    }
    return true;
}

/* Parse a .zan file and extract doc comments + declarations */
static void parse_docs(const char *src, doc_file_t *doc) {
    doc->count = 0;
    doc->namespace_name[0] = 0;

    char summary_buf[1024] = {0};
    char returns_buf[256] = {0};
    bool has_doc = false;

    const char *p = src;
    int line_num = 0;

    while (*p) {
        line_num++;
        const char *line_start = p;
        while (*p && *p != '\n') p++;
        int line_len = (int)(p - line_start);
        if (*p == '\n') p++;

        char line[MAX_DOC];
        if (line_len >= MAX_DOC) line_len = MAX_DOC - 1;
        memcpy(line, line_start, (size_t)line_len);
        line[line_len] = 0;

        /* trim leading whitespace */
        char *trimmed = line;
        while (*trimmed == ' ' || *trimmed == '\t') trimmed++;

        /* namespace directive */
        if (strncmp(trimmed, "namespace ", 10) == 0) {
            char *ns = trimmed + 10;
            while (*ns == ' ') ns++;
            char *end2 = ns;
            while (*end2 && *end2 != ';' && *end2 != '\n' && *end2 != '\r') end2++;
            int len = (int)(end2 - ns);
            if (len >= (int)sizeof(doc->namespace_name)) len = (int)sizeof(doc->namespace_name) - 1;
            memcpy(doc->namespace_name, ns, (size_t)len);
            doc->namespace_name[len] = 0;
            continue;
        }

        /* XML doc comment: /// <summary>...</summary> */
        if (strncmp(trimmed, "///", 3) == 0) {
            const char *comment = trimmed + 3;
            while (*comment == ' ') comment++;
            if (extract_tag(comment, "summary", summary_buf, sizeof(summary_buf))) {
                has_doc = true;
            }
            if (extract_tag(comment, "returns", returns_buf, sizeof(returns_buf))) {
                /* collected */
            }
            continue;
        }

        /* declaration lines */
        if (doc->count >= MAX_MEMBERS) continue;

        doc_member_t *m = &doc->members[doc->count];
        memset(m, 0, sizeof(*m));

        /* detect access modifier */
        char *decl = trimmed;
        if (strncmp(decl, "public ", 7) == 0) { strcpy(m->access, "public"); decl += 7; }
        else if (strncmp(decl, "private ", 8) == 0) { strcpy(m->access, "private"); decl += 8; }
        else if (strncmp(decl, "protected ", 10) == 0) { strcpy(m->access, "protected"); decl += 10; }
        else if (strncmp(decl, "internal ", 9) == 0) { strcpy(m->access, "internal"); decl += 9; }
        else strcpy(m->access, "public");

        if (strncmp(decl, "static ", 7) == 0) decl += 7;

        /* class/struct/enum/interface */
        doc_kind_t kind = DOC_CLASS;
        bool is_type_decl = false;
        if (strncmp(decl, "class ", 6) == 0) { kind = DOC_CLASS; decl += 6; is_type_decl = true; }
        else if (strncmp(decl, "struct ", 7) == 0) { kind = DOC_STRUCT; decl += 7; is_type_decl = true; }
        else if (strncmp(decl, "enum ", 5) == 0) { kind = DOC_ENUM; decl += 5; is_type_decl = true; }
        else if (strncmp(decl, "interface ", 10) == 0) { kind = DOC_INTERFACE; decl += 10; is_type_decl = true; }

        if (is_type_decl) {
            m->kind = kind;
            m->line = line_num;
            /* extract name */
            char *name_end = decl;
            while (*name_end && *name_end != ' ' && *name_end != '{' && *name_end != ':' &&
                   *name_end != '<') name_end++;
            int nlen = (int)(name_end - decl);
            if (nlen >= (int)sizeof(m->name)) nlen = (int)sizeof(m->name) - 1;
            memcpy(m->name, decl, (size_t)nlen);
            m->name[nlen] = 0;
            if (has_doc) strncpy(m->summary, summary_buf, sizeof(m->summary) - 1);
            has_doc = false;
            summary_buf[0] = 0;
            returns_buf[0] = 0;
            doc->count++;
            continue;
        }

        /* method: has () in the line and a type before name */
        if (strchr(decl, '(') != NULL) {
            m->kind = DOC_METHOD;
            m->line = line_num;
            /* capture full signature */
            int slen = (int)strlen(decl);
            /* trim trailing { */
            while (slen > 0 && (decl[slen-1] == '{' || decl[slen-1] == ' ')) slen--;
            if (slen >= (int)sizeof(m->signature)) slen = (int)sizeof(m->signature) - 1;
            memcpy(m->signature, decl, (size_t)slen);
            m->signature[slen] = 0;
            /* extract method name: word before ( */
            char *paren = strchr(decl, '(');
            char *name_start = paren - 1;
            while (name_start > decl && *name_start == ' ') name_start--;
            char *name_end2 = name_start + 1;
            while (name_start > decl && isalnum((unsigned char)*(name_start-1))) name_start--;
            int mnlen = (int)(name_end2 - name_start);
            if (mnlen >= (int)sizeof(m->name)) mnlen = (int)sizeof(m->name) - 1;
            memcpy(m->name, name_start, (size_t)mnlen);
            m->name[mnlen] = 0;
            if (has_doc) strncpy(m->summary, summary_buf, sizeof(m->summary) - 1);
            strncpy(m->returns, returns_buf, sizeof(m->returns) - 1);
            has_doc = false;
            summary_buf[0] = 0;
            returns_buf[0] = 0;
            doc->count++;
        }
    }
}

/* Generate Markdown documentation */
static void gen_markdown(doc_file_t *doc, FILE *out) {
    if (doc->namespace_name[0]) {
        fprintf(out, "# Namespace: %s\n\n", doc->namespace_name);
    }

    /* group by type: classes first, then their methods */
    for (int i = 0; i < doc->count; i++) {
        doc_member_t *m = &doc->members[i];
        if (m->kind == DOC_CLASS || m->kind == DOC_STRUCT ||
            m->kind == DOC_ENUM || m->kind == DOC_INTERFACE) {
            const char *kind_str = "class";
            if (m->kind == DOC_STRUCT) kind_str = "struct";
            if (m->kind == DOC_ENUM) kind_str = "enum";
            if (m->kind == DOC_INTERFACE) kind_str = "interface";

            fprintf(out, "## %s %s\n\n", kind_str, m->name);
            if (m->summary[0]) fprintf(out, "%s\n\n", m->summary);

            /* find methods belonging to this class (those after it until next type) */
            fprintf(out, "### Methods\n\n");
            for (int j = i + 1; j < doc->count; j++) {
                doc_member_t *mm = &doc->members[j];
                if (mm->kind == DOC_CLASS || mm->kind == DOC_STRUCT ||
                    mm->kind == DOC_ENUM || mm->kind == DOC_INTERFACE) break;
                if (mm->kind == DOC_METHOD) {
                    fprintf(out, "#### `%s`\n\n", mm->signature);
                    if (mm->summary[0]) fprintf(out, "%s\n\n", mm->summary);
                    if (mm->returns[0]) fprintf(out, "**Returns:** %s\n\n", mm->returns);
                }
            }
            fprintf(out, "---\n\n");
        }
    }
}

/* Generate HTML documentation */
static void gen_html(doc_file_t *doc, FILE *out) {
    fprintf(out, "<!DOCTYPE html>\n<html>\n<head>\n<meta charset=\"utf-8\">\n");
    fprintf(out, "<title>%s - Zan API Documentation</title>\n", doc->namespace_name[0] ? doc->namespace_name : "Zan");
    fprintf(out, "<style>\n");
    fprintf(out, "body { font-family: 'Segoe UI', sans-serif; max-width: 900px; margin: 0 auto; padding: 20px; background: #1e1e2e; color: #cdd6f4; }\n");
    fprintf(out, "h1 { color: #89b4fa; border-bottom: 2px solid #313244; padding-bottom: 10px; }\n");
    fprintf(out, "h2 { color: #a6e3a1; margin-top: 30px; }\n");
    fprintf(out, "h3 { color: #f9e2af; }\n");
    fprintf(out, "code { background: #313244; padding: 2px 6px; border-radius: 4px; font-size: 0.9em; }\n");
    fprintf(out, "pre { background: #313244; padding: 12px; border-radius: 8px; overflow-x: auto; }\n");
    fprintf(out, ".method { margin: 15px 0; padding: 10px; background: #181825; border-left: 3px solid #89b4fa; border-radius: 4px; }\n");
    fprintf(out, ".summary { color: #a6adc8; margin: 5px 0; }\n");
    fprintf(out, "</style>\n</head>\n<body>\n");

    if (doc->namespace_name[0]) {
        fprintf(out, "<h1>%s</h1>\n", doc->namespace_name);
    }

    for (int i = 0; i < doc->count; i++) {
        doc_member_t *m = &doc->members[i];
        if (m->kind == DOC_CLASS || m->kind == DOC_STRUCT ||
            m->kind == DOC_ENUM || m->kind == DOC_INTERFACE) {
            const char *kind_str = "class";
            if (m->kind == DOC_STRUCT) kind_str = "struct";
            if (m->kind == DOC_ENUM) kind_str = "enum";
            if (m->kind == DOC_INTERFACE) kind_str = "interface";

            fprintf(out, "<h2>%s %s</h2>\n", kind_str, m->name);
            if (m->summary[0]) fprintf(out, "<p class=\"summary\">%s</p>\n", m->summary);
            fprintf(out, "<h3>Methods</h3>\n");

            for (int j = i + 1; j < doc->count; j++) {
                doc_member_t *mm = &doc->members[j];
                if (mm->kind == DOC_CLASS || mm->kind == DOC_STRUCT ||
                    mm->kind == DOC_ENUM || mm->kind == DOC_INTERFACE) break;
                if (mm->kind == DOC_METHOD) {
                    fprintf(out, "<div class=\"method\">\n");
                    fprintf(out, "  <code>%s</code>\n", mm->signature);
                    if (mm->summary[0]) fprintf(out, "  <p class=\"summary\">%s</p>\n", mm->summary);
                    if (mm->returns[0]) fprintf(out, "  <p><strong>Returns:</strong> %s</p>\n", mm->returns);
                    fprintf(out, "</div>\n");
                }
            }
            fprintf(out, "<hr>\n");
        }
    }

    fprintf(out, "</body>\n</html>\n");
}

static void print_usage(void) {
    printf("Zan Documentation Generator (zandoc)\n\n");
    printf("Usage:\n");
    printf("  zandoc <file.zan>                      Generate markdown docs\n");
    printf("  zandoc <file.zan> --html [-o out.html]  Generate HTML docs\n");
    printf("  zandoc <file.zan> --md [-o out.md]      Generate markdown docs\n");
    printf("  zandoc --help                           Show this help\n");
}

int main(int argc, char **argv) {
    if (argc < 2) { print_usage(); return 0; }

    bool html_mode = false;
    const char *file_path = NULL;
    const char *output_path = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--html") == 0) html_mode = true;
        else if (strcmp(argv[i], "--md") == 0) html_mode = false;
        else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) output_path = argv[++i];
        else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_usage(); return 0;
        }
        else file_path = argv[i];
    }

    if (!file_path) { fprintf(stderr, "error: no file specified\n"); return 1; }

    long src_len = 0;
    char *src = read_file(file_path, &src_len);
    if (!src) { fprintf(stderr, "error: cannot read '%s'\n", file_path); return 1; }

    doc_file_t *doc = (doc_file_t *)calloc(1, sizeof(doc_file_t));
    if (!doc) { fprintf(stderr, "error: out of memory\n"); return 1; }
    strncpy(doc->file_name, file_path, sizeof(doc->file_name) - 1);
    parse_docs(src, doc);

    FILE *out = stdout;
    if (output_path) {
        out = fopen(output_path, "w");
        if (!out) { fprintf(stderr, "error: cannot write '%s'\n", output_path); return 1; }
    }

    if (html_mode)
        gen_html(doc, out);
    else
        gen_markdown(doc, out);

    if (output_path) fclose(out);
    free(doc);
    free(src);

    if (output_path) printf("Generated: %s\n", output_path);
    return 0;
}
