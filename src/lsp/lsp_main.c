/* lsp_main.c -- Zan Language Server (M8).
 *
 * A Language Server Protocol implementation for Zan. It speaks LSP over
 * stdio (JSON-RPC with Content-Length framing) and provides:
 *
 *   - Real-time diagnostics (lexer + parser + binder + checker)
 *   - Autocomplete (textDocument/completion) with snippets and member access
 *   - Hover type info (textDocument/hover) with documentation
 *   - Go to definition (textDocument/definition)
 *   - Find references (textDocument/references)
 *   - Document symbols (textDocument/documentSymbol)
 *   - Signature help (textDocument/signatureHelp) for method parameters
 *
 * Front-end analysis reuses the compiler front-end directly (no LLVM
 * dependency); symbol intelligence reuses the IDE intellisense engine.
 *
 * Usage: zan-lsp            (communicates over stdin/stdout)
 */
#include "json.h"
#include "rpc.h"

#include "zan.h"
#include "arena.h"
#include "diag.h"
#include "lexer.h"
#include "parser.h"
#include "ast.h"
#include "binder.h"
#include "checker.h"

#include "intellisense.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#endif

#include "../common/host_oom.h"
/* ============================ document store ========================== */

#define LSP_MAX_DOCS 256

typedef struct {
    char *uri;
    char *text;
} lsp_doc_t;

typedef struct {
    lsp_doc_t docs[LSP_MAX_DOCS];
    int       doc_count;
    bool      shutdown_requested;
    bool      project_indexed;
    char      workspace_root[1024];
    FILE     *out;
} lsp_server_t;

static char *dup_str(const char *s) {
    if (!s) s = "";
    size_t n = strlen(s);
    char *d = (char *)malloc(n + 1);
    if (d) memcpy(d, s, n + 1);
    return d;
}

static lsp_doc_t *lsp_find_doc(lsp_server_t *s, const char *uri) {
    for (int i = 0; i < s->doc_count; i++)
        if (strcmp(s->docs[i].uri, uri) == 0) return &s->docs[i];
    return NULL;
}

static void lsp_set_doc(lsp_server_t *s, const char *uri, const char *text) {
    lsp_doc_t *d = lsp_find_doc(s, uri);
    if (d) {
        free(d->text);
        d->text = dup_str(text);
        return;
    }
    if (s->doc_count >= LSP_MAX_DOCS) return;
    d = &s->docs[s->doc_count++];
    d->uri = dup_str(uri);
    d->text = dup_str(text);
}

static void lsp_remove_doc(lsp_server_t *s, const char *uri) {
    for (int i = 0; i < s->doc_count; i++) {
        if (strcmp(s->docs[i].uri, uri) == 0) {
            free(s->docs[i].uri);
            free(s->docs[i].text);
            s->docs[i] = s->docs[--s->doc_count];
            return;
        }
    }
}

/* ============================ text helpers =========================== */

/* Convert an LSP (line, character) position (both 0-based, UTF-16 units
 * approximated as bytes) to a byte offset into `text`. */
static size_t pos_to_offset(const char *text, int line, int character) {
    size_t off = 0;
    int cur_line = 0;
    while (text[off] && cur_line < line) {
        if (text[off] == '\n') cur_line++;
        off++;
    }
    for (int i = 0; i < character && text[off] && text[off] != '\n'; i++)
        off++;
    return off;
}

static bool is_ident_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_';
}

/* Extract the identifier that spans byte `offset` (cursor may sit anywhere
 * within or right after it). */
static void word_at(const char *text, size_t offset, char *out, size_t cap) {
    size_t len = strlen(text);
    if (offset > len) offset = len;
    size_t start = offset;
    while (start > 0 && is_ident_char(text[start - 1])) start--;
    size_t endp = offset;
    while (text[endp] && is_ident_char(text[endp])) endp++;
    size_t n = endp - start;
    if (n >= cap) n = cap - 1;
    memcpy(out, text + start, n);
    out[n] = '\0';
}

/* Extract the typed prefix immediately before the cursor. */
static void prefix_before(const char *text, size_t offset, char *out, size_t cap) {
    size_t start = offset;
    while (start > 0 && is_ident_char(text[start - 1])) start--;
    size_t n = offset - start;
    if (n >= cap) n = cap - 1;
    memcpy(out, text + start, n);
    out[n] = '\0';
}

/* Convert a global byte offset into a (line, character) pair (both 0-based).
 * The intellisense engine reports symbol locations as a byte offset in its
 * `col` field, so we derive accurate LSP positions from the document text. */
static void offset_to_linecol(const char *text, int offset, int *line, int *character) {
    int ln = 0, col = 0;
    for (int i = 0; i < offset && text[i]; i++) {
        if (text[i] == '\n') { ln++; col = 0; }
        else col++;
    }
    *line = ln;
    *character = col;
}

/* Extract chain expression text before cursor.
 * For "a.Method1().Method2().Pre|" extracts the full chain expression
 * starting from the beginning of the expression. */
static void extract_chain_expr(const char *text, size_t offset,
                               char *chain_out, size_t chain_cap) {
    chain_out[0] = '\0';
    /* Walk backward from cursor, collecting the entire expression including
     * identifiers, dots, parenthesized arguments, and angle brackets */
    size_t end = offset;
    size_t start = end;
    int paren_depth = 0;
    int angle_depth = 0;

    while (start > 0) {
        char ch = text[start - 1];
        if (ch == ')') {
            paren_depth++;
            start--;
        } else if (ch == '(') {
            if (paren_depth > 0) { paren_depth--; start--; }
            else break;
        } else if (ch == '>') {
            angle_depth++;
            start--;
        } else if (ch == '<') {
            if (angle_depth > 0) { angle_depth--; start--; }
            else break;
        } else if (ch == '.' && paren_depth == 0 && angle_depth == 0) {
            start--;
        } else if ((is_ident_char(ch) || ch == '_') && paren_depth == 0 && angle_depth == 0) {
            start--;
        } else if (paren_depth > 0 || angle_depth > 0) {
            /* Inside parens/angles, accept anything */
            start--;
        } else {
            break;
        }
    }

    size_t n = end - start;
    if (n >= chain_cap) n = chain_cap - 1;
    if (n > 0) {
        memcpy(chain_out, text + start, n);
        chain_out[n] = '\0';
    }
}

/* If the cursor is in `Something.<prefix>`, return "Something" in `out`.
 * For chain calls like `a.B().C().pre`, uses intel_resolve_chain to find
 * the resolved type after the chain. Falls back to simple one-dot lookup. */
static void member_context(const char *text, size_t offset, char *out, size_t cap) {
    out[0] = '\0';
    size_t start = offset;
    while (start > 0 && is_ident_char(text[start - 1])) start--;
    if (start == 0 || text[start - 1] != '.') return;

    /* Extract full chain expression */
    char chain_expr[1024];
    extract_chain_expr(text, offset, chain_expr, sizeof(chain_expr));

    /* Check if it's a multi-dot chain */
    int dot_count = 0;
    int pd = 0;
    for (int i = 0; chain_expr[i]; i++) {
        if (chain_expr[i] == '(') pd++;
        else if (chain_expr[i] == ')') { if (pd > 0) pd--; }
        else if (chain_expr[i] == '.' && pd == 0) dot_count++;
    }

    if (dot_count > 1) {
        /* Multi-dot chain: resolve through intel_resolve_chain.
         * We store chain_expr for later resolution by the LSP handler.
         * For now, extract as much as we can with the simple approach first. */
        /* Store the full chain in out with a special prefix "CHAIN:" */
        if (strlen(chain_expr) + 7 < cap) {
            snprintf(out, cap, "CHAIN:%s", chain_expr);
            return;
        }
    }

    /* Simple single-dot: extract identifier before the dot */
    size_t dot = start - 1;
    size_t obj_end = dot;
    size_t obj_start = obj_end;
    /* Walk backward past parenthesized call if present: handle "Method()." */
    if (obj_start > 0 && text[obj_start - 1] == ')') {
        int pdepth = 1;
        obj_start--;
        while (obj_start > 0 && pdepth > 0) {
            obj_start--;
            if (text[obj_start] == ')') pdepth++;
            else if (text[obj_start] == '(') pdepth--;
        }
        /* Now obj_start is at '(', go back to get method name */
        obj_end = obj_start;
        obj_start = obj_end;
    }
    while (obj_start > 0 && is_ident_char(text[obj_start - 1])) obj_start--;
    size_t n = obj_end - obj_start;
    if (n >= cap) n = cap - 1;
    memcpy(out, text + obj_start, n);
    out[n] = '\0';
}

/* Find the method name for signature help: scan backward from offset to find
 * the method call context (the word before the nearest unclosed parenthesis). */
static void method_call_context(const char *text, size_t offset,
                                char *method_out, size_t method_cap,
                                char *class_out, size_t class_cap,
                                int *active_param) {
    method_out[0] = '\0';
    class_out[0] = '\0';
    *active_param = 0;

    int paren_depth = 0;
    size_t i = offset;
    bool found = false;

    /* scan backward to find the opening ( of the enclosing call */
    while (i > 0) {
        i--;
        if (text[i] == ')') paren_depth++;
        else if (text[i] == '(') {
            if (paren_depth == 0) { found = true; break; }
            paren_depth--;
        }
    }
    if (!found) return;

    /* Count the argument index at the cursor by scanning forward from the '('
     * and counting only top-level commas: commas nested in (), [], {}, generic
     * <...>, or inside string/char literals do not separate arguments. */
    {
        int depth = 0;
        int commas = 0;
        for (size_t j = i + 1; j < offset; j++) {
            char ch = text[j];
            if (ch == '"' || ch == '\'') {
                char q = ch;
                j++;
                while (j < offset && text[j] != q) {
                    if (text[j] == '\\') j++;
                    j++;
                }
                continue;
            }
            if (ch == '(' || ch == '[' || ch == '{') depth++;
            else if (ch == ')' || ch == ']' || ch == '}') { if (depth > 0) depth--; }
            else if (ch == '<' && is_ident_char(text[j - 1])) depth++;
            else if (ch == '>' && depth > 0) depth--;
            else if (ch == ',' && depth == 0) commas++;
        }
        *active_param = commas;
    }

    /* extract method name before the '(' */
    size_t end = i;
    while (end > 0 && (text[end - 1] == ' ' || text[end - 1] == '\t')) end--;
    size_t name_end = end;
    while (end > 0 && is_ident_char(text[end - 1])) end--;
    size_t n = name_end - end;
    if (n >= method_cap) n = method_cap - 1;
    memcpy(method_out, text + end, n);
    method_out[n] = '\0';

    /* check for class.method pattern */
    if (end > 0 && text[end - 1] == '.') {
        size_t dot_pos = end - 1;
        size_t cls_end = dot_pos;
        size_t cls_start = cls_end;
        while (cls_start > 0 && is_ident_char(text[cls_start - 1])) cls_start--;
        size_t cn = cls_end - cls_start;
        if (cn >= class_cap) cn = class_cap - 1;
        memcpy(class_out, text + cls_start, cn);
        class_out[cn] = '\0';
    }
}

/* ============================ diagnostics ============================ */

/* Run the front-end over `text` and publish diagnostics for `uri`. */
static void publish_diagnostics(lsp_server_t *s, const char *uri, const char *text) {
    zan_arena_t *arena = zan_arena_new();
    zan_diag_t *diag = zan_diag_new(arena);
    zan_diag_set_capture(diag, true);
    zan_diag_add_file(diag, uri, text);

    zan_lexer_t lex;
    zan_lexer_init(&lex, text, strlen(text), 0, arena, diag);

    zan_parser_t parser;
    zan_parser_init(&parser, &lex, arena, diag);
    zan_ast_node_t *ast = zan_parser_parse(&parser);

    /* Only run later phases if parsing produced no errors, to avoid
     * cascading failures / crashes on a partial AST. */
    if (ast && !zan_diag_has_errors(diag)) {
        zan_binder_t binder;
        zan_binder_init(&binder, arena, diag);
        zan_binder_bind(&binder, ast);

        if (!zan_diag_has_errors(diag)) {
            zan_checker_t checker;
            zan_checker_init(&checker, &binder, arena, diag);
            zan_checker_check(&checker, ast);
        }
    }

    json_value *arr = json_new_arr();
    int count = zan_diag_entry_count(diag);
    for (int i = 0; i < count; i++) {
        const zan_diag_entry_t *e = zan_diag_entry_at(diag, i);
        int line = (int)e->loc.line > 0 ? (int)e->loc.line - 1 : 0;
        int col  = (int)e->loc.col  > 0 ? (int)e->loc.col  - 1 : 0;

        json_value *d = json_new_obj();
        json_value *range = json_new_obj();
        json_value *start = json_new_obj();
        json_value *endp  = json_new_obj();
        json_obj_set(start, "line", json_new_num(line));
        json_obj_set(start, "character", json_new_num(col));
        json_obj_set(endp, "line", json_new_num(line));
        json_obj_set(endp, "character", json_new_num(col + 1));
        json_obj_set(range, "start", start);
        json_obj_set(range, "end", endp);
        json_obj_set(d, "range", range);
        /* LSP severity: 1=Error 2=Warning 3=Info 4=Hint */
        int sev = e->level == DIAG_ERROR ? 1 : (e->level == DIAG_WARNING ? 2 : 3);
        json_obj_set(d, "severity", json_new_num(sev));
        json_obj_set(d, "source", json_new_str("zanc"));
        json_obj_set(d, "message", json_new_str(e->message));
        json_arr_add(arr, d);
    }

    json_value *params = json_new_obj();
    json_obj_set(params, "uri", json_new_str(uri));
    json_obj_set(params, "diagnostics", arr);

    json_value *note = json_new_obj();
    json_obj_set(note, "jsonrpc", json_new_str("2.0"));
    json_obj_set(note, "method", json_new_str("textDocument/publishDiagnostics"));
    json_obj_set(note, "params", params);

    char *payload = json_serialize(note);
    rpc_write_message(s->out, payload);
    free(payload);
    json_free(note);

    zan_diag_free_buffers(diag);
    zan_arena_free(arena);
}

/* ============================ responses ============================== */

static void send_response(lsp_server_t *s, json_value *id, json_value *result) {
    json_value *resp = json_new_obj();
    json_obj_set(resp, "jsonrpc", json_new_str("2.0"));
    /* clone id */
    if (id && id->type == JSON_NUM)
        json_obj_set(resp, "id", json_new_num(id->as.num));
    else if (id && id->type == JSON_STR)
        json_obj_set(resp, "id", json_new_str(id->as.str));
    else
        json_obj_set(resp, "id", json_new_null());
    json_obj_set(resp, "result", result ? result : json_new_null());

    char *payload = json_serialize(resp);
    rpc_write_message(s->out, payload);
    free(payload);
    json_free(resp);
}

/* map intellisense kind to LSP CompletionItemKind */
static int lsp_completion_kind(isym_kind_t k) {
    switch (k) {
    case ISYM_CLASS:       return 7;   /* Class */
    case ISYM_STRUCT:      return 22;  /* Struct */
    case ISYM_ENUM:        return 13;  /* Enum */
    case ISYM_INTERFACE:   return 8;   /* Interface */
    case ISYM_METHOD:      return 2;   /* Method */
    case ISYM_FIELD:       return 5;   /* Field */
    case ISYM_PROPERTY:    return 10;  /* Property */
    case ISYM_VARIABLE:    return 6;   /* Variable */
    case ISYM_PARAMETER:   return 6;   /* Variable */
    case ISYM_KEYWORD:     return 14;  /* Keyword */
    case ISYM_TYPE:        return 25;  /* TypeParameter */
    case ISYM_NAMESPACE:   return 9;   /* Module */
    case ISYM_ENUM_MEMBER: return 20;  /* EnumMember */
    case ISYM_EVENT:       return 23;  /* Event */
    case ISYM_SNIPPET:     return 15;  /* Snippet */
    case ISYM_CONSTRUCTOR: return 4;   /* Constructor */
    default:               return 1;   /* Text */
    }
}

/* map intellisense kind to LSP SymbolKind (documentSymbol) */
static int lsp_symbol_kind(isym_kind_t k) {
    switch (k) {
    case ISYM_CLASS:       return 5;   /* Class */
    case ISYM_STRUCT:      return 23;  /* Struct */
    case ISYM_ENUM:        return 10;  /* Enum */
    case ISYM_INTERFACE:   return 11;  /* Interface */
    case ISYM_METHOD:      return 6;   /* Method */
    case ISYM_FIELD:       return 8;   /* Field */
    case ISYM_PROPERTY:    return 7;   /* Property */
    case ISYM_NAMESPACE:   return 3;   /* Namespace */
    case ISYM_ENUM_MEMBER: return 22;  /* EnumMember */
    case ISYM_CONSTRUCTOR: return 9;   /* Constructor */
    default:               return 13;  /* Variable */
    }
}

/* ============================ handlers =============================== */

static void handle_initialize(lsp_server_t *s, json_value *id, json_value *params) {
    /* Extract workspace root for project indexing */
    if (params) {
        const char *root_uri = json_get_str(json_obj_get(params, "rootUri"));
        const char *root_path = json_get_str(json_obj_get(params, "rootPath"));
        if (root_uri && strncmp(root_uri, "file:///", 8) == 0) {
            strncpy(s->workspace_root, root_uri + 8, sizeof(s->workspace_root) - 1);
            /* Convert URI encoding: %20 -> space, forward slash -> backslash on Windows */
#ifdef _WIN32
            for (char *p = s->workspace_root; *p; p++) {
                if (*p == '/') *p = '\\';
            }
#endif
        } else if (root_path) {
            strncpy(s->workspace_root, root_path, sizeof(s->workspace_root) - 1);
        }
    }

    json_value *caps = json_new_obj();
    json_obj_set(caps, "textDocumentSync", json_new_num(1)); /* full sync */

    /* Completion with trigger characters */
    json_value *completion = json_new_obj();
    json_value *triggers = json_new_arr();
    json_arr_add(triggers, json_new_str("."));
    json_arr_add(triggers, json_new_str("<"));
    json_obj_set(completion, "triggerCharacters", triggers);
    json_obj_set(completion, "resolveProvider", json_new_bool(false));
    json_obj_set(caps, "completionProvider", completion);

    /* Signature help with trigger characters */
    json_value *sig_help = json_new_obj();
    json_value *sig_triggers = json_new_arr();
    json_arr_add(sig_triggers, json_new_str("("));
    json_arr_add(sig_triggers, json_new_str(","));
    json_obj_set(sig_help, "triggerCharacters", sig_triggers);
    json_obj_set(caps, "signatureHelpProvider", sig_help);

    json_obj_set(caps, "hoverProvider", json_new_bool(true));
    json_obj_set(caps, "definitionProvider", json_new_bool(true));
    json_obj_set(caps, "referencesProvider", json_new_bool(true));
    json_obj_set(caps, "documentSymbolProvider", json_new_bool(true));

    /* Code actions (organize usings, etc.) */
    json_value *code_action = json_new_obj();
    json_value *ca_kinds = json_new_arr();
    json_arr_add(ca_kinds, json_new_str("source.organizeImports"));
    json_obj_set(code_action, "codeActionKinds", ca_kinds);
    json_obj_set(caps, "codeActionProvider", code_action);

    json_value *result = json_new_obj();
    json_obj_set(result, "capabilities", caps);

    json_value *info = json_new_obj();
    json_obj_set(info, "name", json_new_str("zan-lsp"));
    json_obj_set(info, "version", json_new_str("0.3.0"));
    json_obj_set(result, "serverInfo", info);

    send_response(s, id, result);
}

/* Project-wide intellisense instance for cross-file completion */
static intellisense_t *g_project_intel = NULL;

static void ensure_project_indexed(lsp_server_t *s) {
    if (s->project_indexed) return;
    s->project_indexed = true;
    if (!s->workspace_root[0]) return;

    if (!g_project_intel) {
        g_project_intel = (intellisense_t *)malloc(sizeof(intellisense_t));
        if (!g_project_intel) return;
        intel_init(g_project_intel);
    }
    intel_index_project(g_project_intel, s->workspace_root);
}

static void handle_did_open(lsp_server_t *s, json_value *params) {
    json_value *td = json_obj_get(params, "textDocument");
    const char *uri = json_get_str(json_obj_get(td, "uri"));
    const char *text = json_get_str(json_obj_get(td, "text"));
    if (!uri || !text) return;
    lsp_set_doc(s, uri, text);

    /* Index the project on first file open */
    ensure_project_indexed(s);

    publish_diagnostics(s, uri, text);
}

static void handle_did_change(lsp_server_t *s, json_value *params) {
    json_value *td = json_obj_get(params, "textDocument");
    const char *uri = json_get_str(json_obj_get(td, "uri"));
    json_value *changes = json_obj_get(params, "contentChanges");
    if (!uri || !changes) return;
    /* full sync: last change carries the whole document */
    int n = json_arr_count(changes);
    if (n <= 0) return;
    json_value *last = json_arr_at(changes, n - 1);
    const char *text = json_get_str(json_obj_get(last, "text"));
    if (!text) return;
    lsp_set_doc(s, uri, text);
    publish_diagnostics(s, uri, text);
}

static void handle_did_close(lsp_server_t *s, json_value *params) {
    json_value *td = json_obj_get(params, "textDocument");
    const char *uri = json_get_str(json_obj_get(td, "uri"));
    if (!uri) return;
    lsp_remove_doc(s, uri);
    /* clear diagnostics */
    publish_diagnostics(s, uri, "");
}

/* Extract (uri, line, character) common to positional requests. */
static bool get_position(json_value *params, const char **uri,
                         int *line, int *character) {
    json_value *td = json_obj_get(params, "textDocument");
    json_value *pos = json_obj_get(params, "position");
    *uri = json_get_str(json_obj_get(td, "uri"));
    *line = (int)json_get_num(json_obj_get(pos, "line"), 0);
    *character = (int)json_get_num(json_obj_get(pos, "character"), 0);
    return *uri != NULL;
}

static void handle_completion(lsp_server_t *s, json_value *id, json_value *params) {
    const char *uri; int line, character;
    if (!get_position(params, &uri, &line, &character)) {
        send_response(s, id, json_new_null());
        return;
    }
    lsp_doc_t *doc = lsp_find_doc(s, uri);
    if (!doc) { send_response(s, id, json_new_arr()); return; }

    size_t off = pos_to_offset(doc->text, line, character);
    char prefix[128], context[128];
    prefix_before(doc->text, off, prefix, sizeof(prefix));
    member_context(doc->text, off, context, sizeof(context));

    /* intellisense_t is large (~2 MB); keep it off the stack. */
    intellisense_t *is = (intellisense_t *)malloc(sizeof(*is));
    if (!is) { send_response(s, id, json_new_arr()); return; }
    intel_init(is);
    intel_parse_file(is, uri, doc->text, strlen(doc->text));

    const char *effective = prefix[0] ? prefix : "";
    int count = 0;

    /* If we have a context (member access), try member completion */
    if (context[0]) {
        const char *resolve_type = context;
        char chain_prefix[128] = "";

        /* Handle chain expressions (CHAIN:prefix) */
        if (strncmp(context, "CHAIN:", 6) == 0) {
            const char *chain_expr = context + 6;
            /* Use intel_resolve_chain to get the type at end of chain */
            const char *chain_type = intel_resolve_chain(is, chain_expr,
                                                         chain_prefix, sizeof(chain_prefix));
            if (!chain_type && g_project_intel) {
                chain_type = intel_resolve_chain(g_project_intel, chain_expr,
                                                 chain_prefix, sizeof(chain_prefix));
            }
            if (chain_type) {
                resolve_type = chain_type;
                if (chain_prefix[0]) {
                    strncpy(prefix, chain_prefix, sizeof(prefix) - 1);
                    effective = prefix[0] ? prefix : "";
                }
            } else {
                /* Fallback: can't resolve chain, show nothing */
                resolve_type = "";
            }
        }

        if (resolve_type[0]) {
            count = intel_complete_members(is, resolve_type, effective);
            /* Also check project-wide types for member completion */
            if (count == 0 && g_project_intel) {
                count = intel_complete_members(g_project_intel, resolve_type, effective);
                if (count > 0) {
                    memcpy(is->completions, g_project_intel->completions,
                           sizeof(completion_t) * (size_t)count);
                    is->completion_count = count;
                }
            }
        }
    } else if (effective[0]) {
        count = intel_complete(is, effective, NULL);
        /* Supplement with project-wide symbols if we have few results */
        if (g_project_intel && count < 20) {
            int proj_count = intel_complete(g_project_intel, effective, NULL);
            /* Merge project completions into local list, avoiding duplicates */
            for (int pi = 0; pi < proj_count && count < INTEL_MAX_COMPLETIONS; pi++) {
                bool dup = false;
                for (int li = 0; li < count; li++) {
                    if (strcmp(is->completions[li].label,
                              g_project_intel->completions[pi].label) == 0) {
                        dup = true;
                        break;
                    }
                }
                if (!dup) {
                    is->completions[count] = g_project_intel->completions[pi];
                    /* Add auto-import info: if the symbol is from another file,
                     * include the namespace in the detail */
                    count++;
                }
            }
            is->completion_count = count;
        }
    }

    json_value *items = json_new_arr();
    for (int i = 0; i < count; i++) {
        completion_t *c = &is->completions[i];
        json_value *item = json_new_obj();
        json_obj_set(item, "label", json_new_str(c->label));
        json_obj_set(item, "insertText", json_new_str(c->insert_text));
        json_obj_set(item, "detail", json_new_str(c->detail));
        json_obj_set(item, "kind", json_new_num(lsp_completion_kind(c->kind)));

        /* Add documentation if available */
        if (c->doc[0]) {
            json_value *doc_obj = json_new_obj();
            json_obj_set(doc_obj, "kind", json_new_str("markdown"));
            json_obj_set(doc_obj, "value", json_new_str(c->doc));
            json_obj_set(item, "documentation", doc_obj);
        }

        /* For snippets, set insertTextFormat to Snippet(2) */
        if (c->kind == ISYM_SNIPPET) {
            json_obj_set(item, "insertTextFormat", json_new_num(2));
        }

        /* Sort text for ordering */
        char sort_key[140];
        snprintf(sort_key, sizeof(sort_key), "%d_%s", c->sort_priority + 5, c->label);
        json_obj_set(item, "sortText", json_new_str(sort_key));

        json_arr_add(items, item);
    }
    free(is);
    send_response(s, id, items);
}

static void handle_hover(lsp_server_t *s, json_value *id, json_value *params) {
    const char *uri; int line, character;
    if (!get_position(params, &uri, &line, &character)) {
        send_response(s, id, json_new_null());
        return;
    }
    lsp_doc_t *doc = lsp_find_doc(s, uri);
    if (!doc) { send_response(s, id, json_new_null()); return; }

    size_t off = pos_to_offset(doc->text, line, character);
    char word[128];
    word_at(doc->text, off, word, sizeof(word));
    if (!word[0]) { send_response(s, id, json_new_null()); return; }

    intellisense_t *is = (intellisense_t *)malloc(sizeof(*is));
    if (!is) { send_response(s, id, json_new_null()); return; }
    intel_init(is);
    intel_parse_file(is, uri, doc->text, strlen(doc->text));
    hover_info_t h = intel_hover(is, word);
    free(is);
    if (!h.valid) { send_response(s, id, json_new_null()); return; }

    char md[1024];
    if (h.doc[0])
        snprintf(md, sizeof(md), "```zan\n%s\n```\n\n%s", h.text, h.doc);
    else
        snprintf(md, sizeof(md), "```zan\n%s\n```", h.text);

    json_value *contents = json_new_obj();
    json_obj_set(contents, "kind", json_new_str("markdown"));
    json_obj_set(contents, "value", json_new_str(md));
    json_value *result = json_new_obj();
    json_obj_set(result, "contents", contents);
    send_response(s, id, result);
}

static void handle_definition(lsp_server_t *s, json_value *id, json_value *params) {
    const char *uri; int line, character;
    if (!get_position(params, &uri, &line, &character)) {
        send_response(s, id, json_new_null());
        return;
    }
    lsp_doc_t *doc = lsp_find_doc(s, uri);
    if (!doc) { send_response(s, id, json_new_null()); return; }

    size_t off = pos_to_offset(doc->text, line, character);
    char word[128];
    word_at(doc->text, off, word, sizeof(word));
    if (!word[0]) { send_response(s, id, json_new_null()); return; }

    intellisense_t *is = (intellisense_t *)malloc(sizeof(*is));
    if (!is) { send_response(s, id, json_new_null()); return; }
    intel_init(is);
    intel_parse_file(is, uri, doc->text, strlen(doc->text));
    goto_def_t g = intel_goto_def(is, word);
    free(is);
    if (!g.found) { send_response(s, id, json_new_null()); return; }

    int dl, dc;
    offset_to_linecol(doc->text, g.col, &dl, &dc);

    json_value *loc = json_new_obj();
    json_obj_set(loc, "uri", json_new_str(g.file[0] ? g.file : uri));
    json_value *range = json_new_obj();
    json_value *start = json_new_obj();
    json_value *endp  = json_new_obj();
    json_obj_set(start, "line", json_new_num(dl));
    json_obj_set(start, "character", json_new_num(dc));
    json_obj_set(endp, "line", json_new_num(dl));
    json_obj_set(endp, "character", json_new_num(dc + (int)strlen(word)));
    json_obj_set(range, "start", start);
    json_obj_set(range, "end", endp);
    json_obj_set(loc, "range", range);
    send_response(s, id, loc);
}

/* NEW: Find all references handler */
static void handle_references(lsp_server_t *s, json_value *id, json_value *params) {
    const char *uri; int line, character;
    if (!get_position(params, &uri, &line, &character)) {
        send_response(s, id, json_new_arr());
        return;
    }
    lsp_doc_t *doc = lsp_find_doc(s, uri);
    if (!doc) { send_response(s, id, json_new_arr()); return; }

    size_t off = pos_to_offset(doc->text, line, character);
    char word[128];
    word_at(doc->text, off, word, sizeof(word));
    if (!word[0]) { send_response(s, id, json_new_arr()); return; }

    intellisense_t *is = (intellisense_t *)malloc(sizeof(*is));
    if (!is) { send_response(s, id, json_new_arr()); return; }
    intel_init(is);
    intel_parse_file(is, uri, doc->text, strlen(doc->text));

    goto_def_t refs[64];
    int ref_count = intel_find_references(is, word, refs, 64);
    free(is);

    json_value *arr = json_new_arr();
    for (int i = 0; i < ref_count; i++) {
        int rl, rc;
        offset_to_linecol(doc->text, refs[i].col, &rl, &rc);

        json_value *loc = json_new_obj();
        json_obj_set(loc, "uri", json_new_str(refs[i].file[0] ? refs[i].file : uri));
        json_value *range = json_new_obj();
        json_value *start = json_new_obj();
        json_value *endp  = json_new_obj();
        json_obj_set(start, "line", json_new_num(rl));
        json_obj_set(start, "character", json_new_num(rc));
        json_obj_set(endp, "line", json_new_num(rl));
        json_obj_set(endp, "character", json_new_num(rc + (int)strlen(word)));
        json_obj_set(range, "start", start);
        json_obj_set(range, "end", endp);
        json_obj_set(loc, "range", range);
        json_arr_add(arr, loc);
    }
    send_response(s, id, arr);
}

/* NEW: Signature help handler */
static void handle_signature_help(lsp_server_t *s, json_value *id, json_value *params) {
    const char *uri; int line, character;
    if (!get_position(params, &uri, &line, &character)) {
        send_response(s, id, json_new_null());
        return;
    }
    lsp_doc_t *doc = lsp_find_doc(s, uri);
    if (!doc) { send_response(s, id, json_new_null()); return; }

    size_t off = pos_to_offset(doc->text, line, character);

    char method_name[128], class_context[128];
    int active_param = 0;
    method_call_context(doc->text, off, method_name, sizeof(method_name),
                        class_context, sizeof(class_context), &active_param);

    if (!method_name[0]) { send_response(s, id, json_new_null()); return; }

    intellisense_t *is = (intellisense_t *)malloc(sizeof(*is));
    if (!is) { send_response(s, id, json_new_null()); return; }
    intel_init(is);
    intel_parse_file(is, uri, doc->text, strlen(doc->text));

    signature_info_t sig = intel_signature_help(is, method_name,
                                                 class_context[0] ? class_context : NULL);
    free(is);

    if (!sig.valid) { send_response(s, id, json_new_null()); return; }

    /* Build the SignatureHelp response */
    json_value *result = json_new_obj();

    json_value *sigs = json_new_arr();
    json_value *sig_obj = json_new_obj();
    json_obj_set(sig_obj, "label", json_new_str(sig.label));
    if (sig.doc[0]) {
        json_value *doc_obj = json_new_obj();
        json_obj_set(doc_obj, "kind", json_new_str("markdown"));
        json_obj_set(doc_obj, "value", json_new_str(sig.doc));
        json_obj_set(sig_obj, "documentation", doc_obj);
    }

    /* parameters */
    json_value *param_arr = json_new_arr();
    for (int i = 0; i < sig.param_count; i++) {
        json_value *p = json_new_obj();
        char param_label[128];
        if (sig.params[i].type[0])
            snprintf(param_label, sizeof(param_label), "%s %s",
                    sig.params[i].type, sig.params[i].label);
        else
            strncpy(param_label, sig.params[i].label, sizeof(param_label) - 1);
        json_obj_set(p, "label", json_new_str(param_label));
        if (sig.params[i].doc[0])
            json_obj_set(p, "documentation", json_new_str(sig.params[i].doc));
        json_arr_add(param_arr, p);
    }
    json_obj_set(sig_obj, "parameters", param_arr);
    json_arr_add(sigs, sig_obj);

    json_obj_set(result, "signatures", sigs);
    json_obj_set(result, "activeSignature", json_new_num(0));
    json_obj_set(result, "activeParameter", json_new_num(active_param));

    send_response(s, id, result);
}

static void handle_document_symbol(lsp_server_t *s, json_value *id, json_value *params) {
    json_value *td = json_obj_get(params, "textDocument");
    const char *uri = json_get_str(json_obj_get(td, "uri"));
    if (!uri) { send_response(s, id, json_new_arr()); return; }
    lsp_doc_t *doc = lsp_find_doc(s, uri);
    if (!doc) { send_response(s, id, json_new_arr()); return; }

    intellisense_t *is = (intellisense_t *)malloc(sizeof(*is));
    if (!is) { send_response(s, id, json_new_arr()); return; }
    intel_init(is);
    intel_parse_file(is, uri, doc->text, strlen(doc->text));

    json_value *arr = json_new_arr();
    for (int i = 0; i < is->symbol_count; i++) {
        isym_t *sym = &is->symbols[i];
        /* Skip local variables and parameters from document symbols */
        if (sym->kind == ISYM_VARIABLE || sym->kind == ISYM_PARAMETER) continue;

        json_value *sinfo = json_new_obj();
        json_obj_set(sinfo, "name", json_new_str(sym->name));
        json_obj_set(sinfo, "kind", json_new_num(lsp_symbol_kind(sym->kind)));
        if (sym->parent[0])
            json_obj_set(sinfo, "containerName", json_new_str(sym->parent));

        int sl, sc;
        offset_to_linecol(doc->text, sym->col, &sl, &sc);

        json_value *loc = json_new_obj();
        json_obj_set(loc, "uri", json_new_str(uri));
        json_value *range = json_new_obj();
        json_value *start = json_new_obj();
        json_value *endp  = json_new_obj();
        json_obj_set(start, "line", json_new_num(sl));
        json_obj_set(start, "character", json_new_num(sc));
        json_obj_set(endp, "line", json_new_num(sl));
        json_obj_set(endp, "character", json_new_num(sc + (int)strlen(sym->name)));
        json_obj_set(range, "start", start);
        json_obj_set(range, "end", endp);
        json_obj_set(loc, "range", range);
        json_obj_set(sinfo, "location", loc);
        json_arr_add(arr, sinfo);
    }
    free(is);
    send_response(s, id, arr);
}

/* Code action handler: organize usings (auto-import / remove unused) */
static void handle_code_action(lsp_server_t *s, json_value *id, json_value *params) {
    json_value *td = json_obj_get(params, "textDocument");
    const char *uri = json_get_str(json_obj_get(td, "uri"));
    if (!uri) { send_response(s, id, json_new_arr()); return; }

    lsp_doc_t *doc = lsp_find_doc(s, uri);
    if (!doc || !doc->text) { send_response(s, id, json_new_arr()); return; }

    /* Use the project-wide intellisense for analyzing usings */
    intellisense_t *is = g_project_intel;
    if (!is) {
        is = (intellisense_t *)malloc(sizeof(intellisense_t));
        if (!is) { send_response(s, id, json_new_arr()); return; }
        intel_init(is);
    }

    size_t doc_len = strlen(doc->text);
    using_analysis_t analysis = intel_analyze_usings(is, doc->text, doc_len);

    json_value *actions = json_new_arr();

    /* If there are missing or unused usings, offer "Organize Usings" */
    if (analysis.missing_count > 0 || analysis.unused_count > 0) {
        json_value *action = json_new_obj();
        json_obj_set(action, "title", json_new_str("Organize Usings"));
        json_obj_set(action, "kind", json_new_str("source.organizeImports"));

        /* Build the text edit to replace the using block */
        size_t new_len;
        char *new_text = intel_organize_usings(is, doc->text, doc_len, &new_len);
        if (new_text) {
            /* Full document replacement edit */
            json_value *edit_obj = json_new_obj();
            json_value *changes = json_new_obj();
            json_value *edits = json_new_arr();

            json_value *text_edit = json_new_obj();
            json_value *range = json_new_obj();
            json_value *start_pos = json_new_obj();
            json_value *end_pos = json_new_obj();

            json_obj_set(start_pos, "line", json_new_num(0));
            json_obj_set(start_pos, "character", json_new_num(0));

            /* Count lines in original document for end position */
            int line_count = 0;
            for (size_t i = 0; i < doc_len; i++)
                if (doc->text[i] == '\n') line_count++;
            json_obj_set(end_pos, "line", json_new_num(line_count));
            json_obj_set(end_pos, "character", json_new_num(0));

            json_obj_set(range, "start", start_pos);
            json_obj_set(range, "end", end_pos);
            json_obj_set(text_edit, "range", range);
            json_obj_set(text_edit, "newText", json_new_str(new_text));
            json_arr_add(edits, text_edit);
            json_obj_set(changes, uri, edits);
            json_obj_set(edit_obj, "changes", changes);
            json_obj_set(action, "edit", edit_obj);
            free(new_text);
        }

        json_arr_add(actions, action);
    }

    /* Also offer individual "Add using X" actions for each missing using */
    for (int i = 0; i < analysis.missing_count; i++) {
        json_value *action = json_new_obj();
        char title[256];
        snprintf(title, sizeof(title), "Add using %s", analysis.missing_usings[i]);
        json_obj_set(action, "title", json_new_str(title));
        json_obj_set(action, "kind", json_new_str("quickfix"));

        /* Insert at line 0 (before first using or at top of file) */
        json_value *edit_obj = json_new_obj();
        json_value *changes = json_new_obj();
        json_value *edits = json_new_arr();
        json_value *text_edit = json_new_obj();
        json_value *range = json_new_obj();
        json_value *pos = json_new_obj();

        int insert_line = (analysis.using_count > 0) ? analysis.usings[0].line : 0;
        json_obj_set(pos, "line", json_new_num(insert_line));
        json_obj_set(pos, "character", json_new_num(0));
        json_obj_set(range, "start", pos);
        json_value *pos2 = json_new_obj();
        json_obj_set(pos2, "line", json_new_num(insert_line));
        json_obj_set(pos2, "character", json_new_num(0));
        json_obj_set(range, "end", pos2);
        json_obj_set(text_edit, "range", range);

        char using_text[256];
        snprintf(using_text, sizeof(using_text), "using %s;\n", analysis.missing_usings[i]);
        json_obj_set(text_edit, "newText", json_new_str(using_text));
        json_arr_add(edits, text_edit);
        json_obj_set(changes, uri, edits);
        json_obj_set(edit_obj, "changes", changes);
        json_obj_set(action, "edit", edit_obj);

        json_arr_add(actions, action);
    }

    /* Offer "Remove unused using X" for each unused */
    for (int i = 0; i < analysis.unused_count; i++) {
        int idx = analysis.unused_indices[i];
        json_value *action = json_new_obj();
        char title[256];
        snprintf(title, sizeof(title), "Remove using %s", analysis.usings[idx].namespace_name);
        json_obj_set(action, "title", json_new_str(title));
        json_obj_set(action, "kind", json_new_str("quickfix"));

        json_value *edit_obj = json_new_obj();
        json_value *changes = json_new_obj();
        json_value *edits = json_new_arr();
        json_value *text_edit = json_new_obj();
        json_value *range = json_new_obj();
        json_value *start_pos = json_new_obj();
        json_value *end_pos = json_new_obj();

        json_obj_set(start_pos, "line", json_new_num(analysis.usings[idx].line));
        json_obj_set(start_pos, "character", json_new_num(0));
        json_obj_set(end_pos, "line", json_new_num(analysis.usings[idx].line + 1));
        json_obj_set(end_pos, "character", json_new_num(0));
        json_obj_set(range, "start", start_pos);
        json_obj_set(range, "end", end_pos);
        json_obj_set(text_edit, "range", range);
        json_obj_set(text_edit, "newText", json_new_str(""));
        json_arr_add(edits, text_edit);
        json_obj_set(changes, uri, edits);
        json_obj_set(edit_obj, "changes", changes);
        json_obj_set(action, "edit", edit_obj);

        json_arr_add(actions, action);
    }

    if (is != g_project_intel) free(is);
    send_response(s, id, actions);
}

/* ============================ dispatch =============================== */

static void dispatch(lsp_server_t *s, json_value *msg) {
    const char *method = json_get_str(json_obj_get(msg, "method"));
    json_value *id = json_obj_get(msg, "id");
    json_value *params = json_obj_get(msg, "params");
    if (!method) return;

    if (strcmp(method, "initialize") == 0) {
        handle_initialize(s, id, params);
    } else if (strcmp(method, "initialized") == 0) {
        /* notification, no reply */
    } else if (strcmp(method, "textDocument/didOpen") == 0) {
        handle_did_open(s, params);
    } else if (strcmp(method, "textDocument/didChange") == 0) {
        handle_did_change(s, params);
    } else if (strcmp(method, "textDocument/didClose") == 0) {
        handle_did_close(s, params);
    } else if (strcmp(method, "textDocument/didSave") == 0) {
        /* nothing extra: diagnostics already fresh */
    } else if (strcmp(method, "textDocument/completion") == 0) {
        handle_completion(s, id, params);
    } else if (strcmp(method, "textDocument/hover") == 0) {
        handle_hover(s, id, params);
    } else if (strcmp(method, "textDocument/definition") == 0) {
        handle_definition(s, id, params);
    } else if (strcmp(method, "textDocument/references") == 0) {
        handle_references(s, id, params);
    } else if (strcmp(method, "textDocument/signatureHelp") == 0) {
        handle_signature_help(s, id, params);
    } else if (strcmp(method, "textDocument/documentSymbol") == 0) {
        handle_document_symbol(s, id, params);
    } else if (strcmp(method, "textDocument/codeAction") == 0) {
        handle_code_action(s, id, params);
    } else if (strcmp(method, "shutdown") == 0) {
        s->shutdown_requested = true;
        send_response(s, id, json_new_null());
    } else if (strcmp(method, "exit") == 0) {
        /* handled by caller loop */
    } else if (id) {
        /* unknown request: reply null so the client isn't left hanging */
        send_response(s, id, json_new_null());
    }
}

int main(void) {
    lsp_server_t server;
    memset(&server, 0, sizeof(server));
    server.out = stdout;

#ifdef _WIN32
    /* avoid CRLF translation mangling the framed protocol */
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif

    for (;;) {
        char *body = rpc_read_message(stdin);
        if (!body) break; /* EOF */

        json_value *msg = json_parse(body);
        free(body);
        if (!msg) continue;

        const char *method = json_get_str(json_obj_get(msg, "method"));
        bool is_exit = method && strcmp(method, "exit") == 0;

        dispatch(&server, msg);
        json_free(msg);

        if (is_exit) break;
    }

    for (int i = 0; i < server.doc_count; i++) {
        free(server.docs[i].uri);
        free(server.docs[i].text);
    }
    return server.shutdown_requested ? 0 : 0;
}
