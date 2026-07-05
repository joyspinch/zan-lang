/* lsp_main.c -- Zan Language Server (M8).
 *
 * A Language Server Protocol implementation for Zan. It speaks LSP over
 * stdio (JSON-RPC with Content-Length framing) and provides:
 *
 *   - Real-time diagnostics (lexer + parser + binder + checker)
 *   - Autocomplete (textDocument/completion)
 *   - Hover type info (textDocument/hover)
 *   - Go to definition (textDocument/definition)
 *   - Document symbols (textDocument/documentSymbol)
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

/* If the cursor is in `Something.<prefix>`, return "Something" in `out`. */
static void member_context(const char *text, size_t offset, char *out, size_t cap) {
    out[0] = '\0';
    size_t start = offset;
    while (start > 0 && is_ident_char(text[start - 1])) start--;
    if (start == 0 || text[start - 1] != '.') return;
    size_t dot = start - 1;
    size_t obj_end = dot;
    size_t obj_start = obj_end;
    while (obj_start > 0 && is_ident_char(text[obj_start - 1])) obj_start--;
    size_t n = obj_end - obj_start;
    if (n >= cap) n = cap - 1;
    memcpy(out, text + obj_start, n);
    out[n] = '\0';
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
    case ISYM_CLASS:     return 7;   /* Class */
    case ISYM_STRUCT:    return 22;  /* Struct */
    case ISYM_ENUM:      return 13;  /* Enum */
    case ISYM_INTERFACE: return 8;   /* Interface */
    case ISYM_METHOD:    return 2;   /* Method */
    case ISYM_FIELD:     return 5;   /* Field */
    case ISYM_PROPERTY:  return 10;  /* Property */
    case ISYM_VARIABLE:  return 6;   /* Variable */
    case ISYM_PARAMETER: return 6;   /* Variable */
    case ISYM_KEYWORD:   return 14;  /* Keyword */
    case ISYM_TYPE:      return 25;  /* TypeParameter */
    case ISYM_NAMESPACE: return 9;   /* Module */
    default:             return 1;   /* Text */
    }
}

/* map intellisense kind to LSP SymbolKind (documentSymbol) */
static int lsp_symbol_kind(isym_kind_t k) {
    switch (k) {
    case ISYM_CLASS:     return 5;   /* Class */
    case ISYM_STRUCT:    return 23;  /* Struct */
    case ISYM_ENUM:      return 10;  /* Enum */
    case ISYM_INTERFACE: return 11;  /* Interface */
    case ISYM_METHOD:    return 6;   /* Method */
    case ISYM_FIELD:     return 8;   /* Field */
    case ISYM_PROPERTY:  return 7;   /* Property */
    case ISYM_NAMESPACE: return 3;   /* Namespace */
    default:             return 13;  /* Variable */
    }
}

/* ============================ handlers =============================== */

static void handle_initialize(lsp_server_t *s, json_value *id) {
    json_value *caps = json_new_obj();
    json_obj_set(caps, "textDocumentSync", json_new_num(1)); /* full sync */

    json_value *completion = json_new_obj();
    json_value *triggers = json_new_arr();
    json_arr_add(triggers, json_new_str("."));
    json_obj_set(completion, "triggerCharacters", triggers);
    json_obj_set(completion, "resolveProvider", json_new_bool(false));
    json_obj_set(caps, "completionProvider", completion);

    json_obj_set(caps, "hoverProvider", json_new_bool(true));
    json_obj_set(caps, "definitionProvider", json_new_bool(true));
    json_obj_set(caps, "documentSymbolProvider", json_new_bool(true));

    json_value *result = json_new_obj();
    json_obj_set(result, "capabilities", caps);

    json_value *info = json_new_obj();
    json_obj_set(info, "name", json_new_str("zan-lsp"));
    json_obj_set(info, "version", json_new_str("0.1.0"));
    json_obj_set(result, "serverInfo", info);

    send_response(s, id, result);
}

static void handle_did_open(lsp_server_t *s, json_value *params) {
    json_value *td = json_obj_get(params, "textDocument");
    const char *uri = json_get_str(json_obj_get(td, "uri"));
    const char *text = json_get_str(json_obj_get(td, "text"));
    if (!uri || !text) return;
    lsp_set_doc(s, uri, text);
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
    if (effective[0])
        count = intel_complete(is, effective, context[0] ? context : NULL);

    json_value *items = json_new_arr();
    for (int i = 0; i < count; i++) {
        completion_t *c = &is->completions[i];
        json_value *item = json_new_obj();
        json_obj_set(item, "label", json_new_str(c->label));
        json_obj_set(item, "insertText", json_new_str(c->insert_text));
        json_obj_set(item, "detail", json_new_str(c->detail));
        json_obj_set(item, "kind", json_new_num(lsp_completion_kind(c->kind)));
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

    char md[640];
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

/* ============================ dispatch =============================== */

static void dispatch(lsp_server_t *s, json_value *msg) {
    const char *method = json_get_str(json_obj_get(msg, "method"));
    json_value *id = json_obj_get(msg, "id");
    json_value *params = json_obj_get(msg, "params");
    if (!method) return;

    if (strcmp(method, "initialize") == 0) {
        handle_initialize(s, id);
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
    } else if (strcmp(method, "textDocument/documentSymbol") == 0) {
        handle_document_symbol(s, id, params);
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
