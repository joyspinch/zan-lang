/* ====================================================================
 * LSP integration test for zan-lsp (A5: UTF-16 position encoding).
 *
 * Spawns the real zan-lsp server, speaks Content-Length-framed LSP over its
 * stdio, opens a source file whose lines contain multi-byte UTF-8 (CJK and a
 * 4-byte emoji, i.e. surrogate-pair -> 2 UTF-16 units), and asks for the
 * definition of `Helper` at a position given in UTF-16 code units.
 *
 * The LSP protocol defines positions in UTF-16 code units, but the compiler's
 * lexer counts columns in bytes.  The old server passed the raw client
 * character through as a byte offset, so a query at UTF-16 unit 39 landed at
 * byte 39 -- inside the word `int` on the call line -- and the definition
 * came back null.  With the conversion fixed, unit 39 maps to the byte offset
 * of `Helper`, and the server must answer with the real definition location
 * (line 2, UTF-16 column 31, measured to the `H` of `Helper`).
 *
 * Usage: lsp_integration_test <zan-lsp>
 * ==================================================================== */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#ifdef _WIN32
#  include <windows.h>
#else
#  include <unistd.h>
#  include <sys/types.h>
#  include <sys/stat.h>
#  include <sys/wait.h>
#  include <fcntl.h>
#  include <errno.h>
#endif

#include "src/common/json.h"

/* --------- child process with bidirectional pipes --------- */
typedef struct {
#ifdef _WIN32
    HANDLE in_w;   /* write to child's stdin */
    HANDLE out_r;  /* read from child's stdout */
    HANDLE proc;
#else
    int in_w;
    int out_r;
    int pid;
#endif
} child_t;

static bool child_spawn(child_t *c, const char *exe) {
#ifdef _WIN32
    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
    HANDLE in_r = NULL, in_w = NULL, out_r = NULL, out_w = NULL;
    if (!CreatePipe(&in_r, &in_w, &sa, 0)) return false;
    if (!CreatePipe(&out_r, &out_w, &sa, 0)) return false;
    SetHandleInformation(in_w, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(out_r, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si = {0};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = in_r;
    si.hStdOutput = out_w;
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    PROCESS_INFORMATION pi = {0};
    char cmd[1200];
    snprintf(cmd, sizeof(cmd), "\"%s\"", exe);
    if (!CreateProcessA(NULL, cmd, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi))
        return false;
    CloseHandle(in_r);
    CloseHandle(out_w);
    CloseHandle(pi.hThread);
    c->in_w = in_w;
    c->out_r = out_r;
    c->proc = pi.hProcess;
    return true;
#else
    int inpipe[2], outpipe[2];
    if (pipe(inpipe) != 0 || pipe(outpipe) != 0) return false;
    int pid = fork();
    if (pid < 0) return false;
    if (pid == 0) {
        dup2(inpipe[0], 0);
        dup2(outpipe[1], 1);
        close(inpipe[0]); close(inpipe[1]);
        close(outpipe[0]); close(outpipe[1]);
        execl(exe, exe, (char *)NULL);
        _exit(127);
    }
    close(inpipe[0]);
    close(outpipe[1]);
    c->in_w = inpipe[1];
    c->out_r = outpipe[0];
    c->pid = pid;
    return true;
#endif
}

static void child_write(child_t *c, const char *buf, int len) {
#ifdef _WIN32
    DWORD w = 0;
    WriteFile(c->in_w, buf, (DWORD)len, &w, NULL);
#else
    ssize_t off = 0;
    while (off < len) {
        ssize_t n = write(c->in_w, buf + off, (size_t)(len - off));
        if (n <= 0) break;
        off += n;
    }
#endif
}

static int child_read_byte(child_t *c) {
    char b;
#ifdef _WIN32
    DWORD r = 0;
    if (!ReadFile(c->out_r, &b, 1, &r, NULL) || r == 0) return -1;
#else
    ssize_t r = read(c->out_r, &b, 1);
    if (r <= 0) return -1;
#endif
    return (unsigned char)b;
}

static void child_close(child_t *c) {
#ifdef _WIN32
    if (c->in_w) CloseHandle(c->in_w);
    if (c->out_r) CloseHandle(c->out_r);
    if (c->proc) {
        WaitForSingleObject(c->proc, 3000);
        CloseHandle(c->proc);
    }
#else
    if (c->in_w >= 0) close(c->in_w);
    if (c->out_r >= 0) close(c->out_r);
    if (c->pid > 0) {
        int st;
        waitpid(c->pid, &st, 0);
    }
#endif
}

/* --------- LSP framing --------- */
/* Hard ceiling on how much we will read; a server that spirals into sending
 * junk (or a framing bug) fails the test instead of hanging the suite. */
static int g_read_budget = 4 * 1024 * 1024;

static int child_read_byte_budgeted(child_t *c) {
    if (g_read_budget <= 0) return -1;
    int b = child_read_byte(c);
    if (b >= 0) g_read_budget--;
    return b;
}

static void lsp_send(child_t *c, const char *body) {
    char header[64];
    int hlen = snprintf(header, sizeof(header),
                        "Content-Length: %d\r\n\r\n", (int)strlen(body));
    child_write(c, header, hlen);
    child_write(c, body, (int)strlen(body));
}

/* Read one framed LSP message into buf (NUL-terminated). False on EOF. */
static bool lsp_recv(child_t *c, char *buf, int cap) {
    char header[256];
    int hp = 0;
    int content_len = -1;
    while (1) {
        int line_start = hp;
        while (1) {
            int ch = child_read_byte_budgeted(c);
            if (ch < 0) return false;
            if (hp < (int)sizeof(header) - 1) header[hp++] = (char)ch;
            if (ch == '\n') break;
        }
        header[hp] = '\0';
        if (hp - line_start <= 2) break; /* blank line */
        if (content_len < 0) {
            const char *cl = header + line_start;
            if (strncmp(cl, "Content-Length:", 15) == 0)
                content_len = atoi(cl + 15);
        }
    }
    if (content_len < 0 || content_len >= cap) return false;
    int got = 0;
    while (got < content_len) {
        int ch = child_read_byte_budgeted(c);
        if (ch < 0) return false;
        buf[got++] = (char)ch;
    }
    buf[got] = '\0';
    return true;
}

/* Serialize + frame one JSON message to the server. */
static void send_message(child_t *c, json_value *root) {
    char *body = json_serialize(root);
    lsp_send(c, body);
    free(body);
    json_free(root);
}

/* Drain messages until one whose top-level "id" equals want_id arrives.
 * Returns a malloc'd copy of that message body, or NULL if the server never
 * answers (EOF, framing error, or too many unrelated messages). */
static char *recv_until_id(child_t *c, double want_id) {
    char buf[65536];
    for (int tries = 0; tries < 64; tries++) {
        if (!lsp_recv(c, buf, sizeof(buf))) return NULL;
        json_value *root = json_parse(buf);
        if (!root) continue;
        double got = json_get_num(json_obj_get(root, "id"), -1);
        json_free(root);
        if (got == want_id) return strdup(buf);
    }
    return NULL;
}

/* --------- the test --------- */
/* Test document.  Line 2 (0-based) is the definition of Helper; the comment
 * in front of it holds CJK + a surrogate-pair emoji, so the H of `Helper`
 * sits at byte 45 but only UTF-16 column 31.  Line 4 calls it; there the H
 * sits at byte 45 too but at UTF-16 column 39.  Any byte-oriented server
 * misplaces at least one of the two. */
#define TEST_URI "file:///lsp_utf16_test.zan"
static const char *DOC =
    "using System;\n"
    "class Program {\n"
    "    /* \xE4\xBD\xA0\xE5\xA5\xBD\xF0\x9F\x98\x80 \xE8\xBF\x99\xE6\x98\xAF\xE6\xB3\xA8\xE9\x87\x8A */ static int Helper() { return 42; }\n"
    "    static void Main() {\n"
    "        string label = \"\xE4\xBD\xA0\xE5\xA5\xBD\xF0\x9F\x98\x80\"; int v = Helper();\n"
    "        Console.WriteLine(v);\n"
    "    }\n"
    "}\n";

static json_value *mk_text_document(void) {
    json_value *td = json_new_obj();
    json_obj_set(td, "uri", json_new_str(TEST_URI));
    json_obj_set(td, "languageId", json_new_str("zan"));
    json_obj_set(td, "version", json_new_num(1));
    json_obj_set(td, "text", json_new_str(DOC));
    return td;
}

/* --------- extended capability checks (formatting/highlight/fold) ------- */

/* Second document with an intentional syntax error on line 1 (the compiler
 * accepts unknown symbols in field initializers, so the error must be a
 * parser one: "int q = ;" fails at 1-based 2:13, i.e. 0-based line 1 char 12
 * with the ';' as a one-character token range). */
#define BAD_URI "file:///lsp_diag_range_test.zan"
static const char *BAD_DOC =
    "class P {\n"
    "    int q = ;\n"
    "}\n";

static int ext_fails = 0;

static void ext_check(bool cond, const char *msg) {
    printf("%s: %s\n", cond ? "PASS" : "FAIL", msg);
    if (!cond) ext_fails++;
}

/* Build one request message with the given id/method/params. */
static json_value *mk_request(int id, const char *method, json_value *params) {
    json_value *msg = json_new_obj();
    json_obj_set(msg, "jsonrpc", json_new_str("2.0"));
    json_obj_set(msg, "id", json_new_num(id));
    json_obj_set(msg, "method", json_new_str(method));
    json_obj_set(msg, "params", params);
    return msg;
}

/* Does an array-valued JSON fragment (raw text) contain a folding range
 * start->end? Parsed with the shared json parser on a synthesized object. */
static bool fold_contains(const char *body, int s0, int e0) {
    char pat[96];
    snprintf(pat, sizeof(pat), "\"startLine\":%d,\"endLine\":%d", s0, e0);
    return strstr(body, pat) != NULL;
}

/* 同一声明在文档索引和项目索引中有不同副本；两种查询都应返回这两个范围。 */
static bool helper_highlights_exact(const char *body) {
    json_value *root = body ? json_parse(body) : NULL;
    json_value *result = json_obj_get(root, "result");
    bool declaration = false, call = false;
    bool valid = json_is(result, JSON_ARR) && json_arr_count(result) == 2;
    for (int i = 0; valid && i < json_arr_count(result); i++) {
        json_value *highlight = json_arr_at(result, i);
        json_value *range = json_obj_get(highlight, "range");
        json_value *start = json_obj_get(range, "start");
        json_value *end = json_obj_get(range, "end");
        int line = (int)json_get_num(json_obj_get(start, "line"), -1);
        int col = (int)json_get_num(json_obj_get(start, "character"), -1);
        valid = json_get_num(json_obj_get(highlight, "kind"), -1) == 1 &&
                json_get_num(json_obj_get(end, "line"), -1) == line &&
                json_get_num(json_obj_get(end, "character"), -1) == col + 6;
        if (line == 2 && col == 31) declaration = true;
        else if (line == 4 && col == 39) call = true;
        else valid = false;
    }
    json_free(root);
    return valid && declaration && call;
}

static int run_extended_checks(child_t *child) {
    /* open the intentionally-broken second document: didOpen publishes its
     * diagnostics synchronously */
    {
        json_value *td = json_new_obj();
        json_obj_set(td, "uri", json_new_str(BAD_URI));
        json_obj_set(td, "languageId", json_new_str("zan"));
        json_obj_set(td, "version", json_new_num(1));
        json_obj_set(td, "text", json_new_str(BAD_DOC));
        json_value *params = json_new_obj();
        json_obj_set(params, "textDocument", td);
        send_message(child, mk_request(-1, "textDocument/didOpen", params));
    }

    /* foldingRange (id 10) */
    {
        json_value *td = json_new_obj();
        json_obj_set(td, "uri", json_new_str(TEST_URI));
        json_value *params = json_new_obj();
        json_obj_set(params, "textDocument", td);
        send_message(child, mk_request(10, "textDocument/foldingRange", params));
    }
    /* documentHighlight on Helper (id 11) */
    {
        json_value *td = json_new_obj();
        json_obj_set(td, "uri", json_new_str(TEST_URI));
        json_value *pos = json_new_obj();
        json_obj_set(pos, "line", json_new_num(4));
        json_obj_set(pos, "character", json_new_num(39));
        json_value *params = json_new_obj();
        json_obj_set(params, "textDocument", td);
        json_obj_set(params, "position", pos);
        send_message(child, mk_request(11, "textDocument/documentHighlight", params));
    }
    /* prepareRename on Helper (id 12) */
    {
        json_value *td = json_new_obj();
        json_obj_set(td, "uri", json_new_str(TEST_URI));
        json_value *pos = json_new_obj();
        json_obj_set(pos, "line", json_new_num(4));
        json_obj_set(pos, "character", json_new_num(39));
        json_value *params = json_new_obj();
        json_obj_set(params, "textDocument", td);
        json_obj_set(params, "position", pos);
        send_message(child, mk_request(12, "textDocument/prepareRename", params));
    }
    /* formatting (id 13) and rangeFormatting over already-clean lines (id 14) */
    {
        json_value *td = json_new_obj();
        json_obj_set(td, "uri", json_new_str(TEST_URI));
        json_value *params = json_new_obj();
        json_obj_set(params, "textDocument", td);
        json_value *opts = json_new_obj();
        json_obj_set(params, "options", opts);
        send_message(child, mk_request(13, "textDocument/formatting", params));
    }
    {
        json_value *td = json_new_obj();
        json_obj_set(td, "uri", json_new_str(TEST_URI));
        json_value *rg = json_new_obj();
        json_value *s = json_new_obj();
        json_obj_set(s, "line", json_new_num(4));
        json_obj_set(s, "character", json_new_num(0));
        json_value *e = json_new_obj();
        json_obj_set(e, "line", json_new_num(6));
        json_obj_set(e, "character", json_new_num(0));
        json_obj_set(rg, "start", s);
        json_obj_set(rg, "end", e);
        json_value *params = json_new_obj();
        json_obj_set(params, "textDocument", td);
        json_obj_set(params, "range", rg);
        json_obj_set(params, "options", json_new_obj());
        send_message(child, mk_request(14, "textDocument/rangeFormatting", params));
    }

    /* drain: collect the five responses and the second doc's diagnostics */
    char *r[6] = {0};       /* by id: 10..14 */
    char *diag_bad = NULL;  /* publishDiagnostics for BAD_URI */
    for (int i = 0; i < 300 && ext_fails == 0; i++) {
        char buf[131072];
        if (!lsp_recv(child, buf, sizeof(buf))) break;
        if (strstr(buf, "\"textDocument/publishDiagnostics\"") && strstr(buf, BAD_URI)) {
            free(diag_bad);
            diag_bad = strdup(buf);
        }
        json_value *root = json_parse(buf);
        if (!root) continue;
        long id = (long)json_get_num(json_obj_get(root, "id"), -1);
        json_free(root);
        if (id >= 10 && id <= 14) {
            free(r[id - 10]);
            r[id - 10] = strdup(buf);
        }
        bool done = r[0] && r[1] && r[2] && r[3] && r[4];
        if (done && diag_bad) break;
    }

    /* foldingRange: class block and Main's block, same-line Helper braces excluded */
    if (r[0]) {
        ext_check(fold_contains(r[0], 1, 6), "foldingRange: class block 1..6");
        ext_check(fold_contains(r[0], 3, 5), "foldingRange: Main block 3..5");
        ext_check(strstr(r[0], "\"startLine\":2,") == NULL,
                  "foldingRange: same-line braces produce no range");
    } else {
        ext_check(false, "foldingRange: no response");
    }

    /* BAD_URI 打开后缓存已失效，调用和声明必须仍映射到同一声明身份。 */
    ext_check(helper_highlights_exact(r[1]),
              "documentHighlight: 调用处查询返回两个精确 UTF-16 范围");
    {
        json_value *td = json_new_obj();
        json_obj_set(td, "uri", json_new_str(TEST_URI));
        json_value *pos = json_new_obj();
        json_obj_set(pos, "line", json_new_num(2));
        json_obj_set(pos, "character", json_new_num(31));
        json_value *params = json_new_obj();
        json_obj_set(params, "textDocument", td);
        json_obj_set(params, "position", pos);
        send_message(child, mk_request(15, "textDocument/documentHighlight", params));
        char *response = recv_until_id(child, 15);
        ext_check(helper_highlights_exact(response),
                  "documentHighlight: 声明处查询返回两个精确 UTF-16 范围");
        free(response);
    }

    /* prepareRename: exact word range + placeholder */
    if (r[2]) {
        ext_check(strstr(r[2], "\"placeholder\":\"Helper\"") != NULL,
                  "prepareRename: placeholder is Helper");
        ext_check(strstr(r[2], "\"character\":39") != NULL,
                  "prepareRename: range starts at UTF-16 col 39");
    } else {
        ext_check(false, "prepareRename: no response");
    }

    /* formatting: already-clean document comes back unchanged in one edit */
    if (r[3]) {
        ext_check(strstr(r[3], "\"newText\":\"using System;\\nclass Program {\\n") != NULL,
                  "formatting: stable on formatted input");
    } else {
        ext_check(false, "formatting: no response");
    }

    /* rangeFormatting over clean lines: zero edits (idempotence) */
    if (r[4]) {
        ext_check(strstr(r[4], "\"result\":[]") != NULL,
                  "rangeFormatting: no spurious edits on clean lines");
    } else {
        ext_check(false, "rangeFormatting: no response");
    }

    /* diagnostics: the broken expression is flagged with its real extent
     * (';' at 0-based char 12, one character wide) */
    if (diag_bad) {
        ext_check(strstr(diag_bad, "\"start\":{\"line\":1,\"character\":12}") != NULL,
                  "diagnostics: error reported at line 1 char 12");
        ext_check(strstr(diag_bad, "\"end\":{\"line\":1,\"character\":13}") != NULL,
                  "diagnostics: token range covers ';' (12..13)");
    } else {
        ext_check(false, "diagnostics: none published for the broken doc");
    }

    for (int i = 0; i < 5; i++) free(r[i]);
    free(diag_bad);
    printf("\n%d extended failure(s)\n", ext_fails);
    return ext_fails ? 1 : 0;
}

static void completion_document(child_t *child, const char *uri,
                                const char *text, int version, bool open) {
    json_value *td = json_new_obj();
    json_obj_set(td, "uri", json_new_str(uri));
    json_obj_set(td, "version", json_new_num(version));
    json_value *params = json_new_obj();
    json_obj_set(params, "textDocument", td);
    if (open) {
        json_obj_set(td, "languageId", json_new_str("zan"));
        json_obj_set(td, "text", json_new_str(text));
    } else {
        json_value *changes = json_new_arr();
        json_value *change = json_new_obj();
        json_obj_set(change, "text", json_new_str(text));
        json_arr_add(changes, change);
        json_obj_set(params, "contentChanges", changes);
    }
    send_message(child, mk_request(-1, open ? "textDocument/didOpen"
                                          : "textDocument/didChange", params));
}

static char *request_at_marker_ex(child_t *child, const char *uri, const char *text,
                                  const char *receiver, const char *method,
                                  const char *new_name, bool include_decl) {
    static int request_id = 60;
    const char *at = strstr(text, receiver);
    if (!at) return NULL;
    at += strlen(receiver);
    int line = 0, column = 0;
    for (const char *p = text; p < at; p++) {
        if (*p == '\n') { line++; column = 0; }
        else {
            unsigned char ch = (unsigned char)*p;
            if ((ch & 0xc0) != 0x80) column += ch >= 0xf0 ? 2 : 1;
        }
    }
    json_value *td = json_new_obj();
    json_obj_set(td, "uri", json_new_str(uri));
    json_value *pos = json_new_obj();
    json_obj_set(pos, "line", json_new_num(line));
    json_obj_set(pos, "character", json_new_num(column));
    json_value *params = json_new_obj();
    json_obj_set(params, "textDocument", td);
    json_obj_set(params, "position", pos);
    if (new_name) json_obj_set(params, "newName", json_new_str(new_name));
    if (strcmp(method, "textDocument/references") == 0) {
        json_value *context = json_new_obj();
        json_obj_set(context, "includeDeclaration", json_new_bool(include_decl));
        json_obj_set(params, "context", context);
    }
    int id = request_id++;
    send_message(child, mk_request(id, method, params));
    return recv_until_id(child, id);
}

static char *request_at_marker(child_t *child, const char *uri, const char *text,
                               const char *receiver, const char *method, const char *new_name) {
    return request_at_marker_ex(child, uri, text, receiver, method, new_name, true);
}

static bool completion_has(child_t *child, const char *uri, const char *text,
                           const char *receiver, const char *label) {
    char *response = request_at_marker(child, uri, text, receiver,
                                       "textDocument/completion", NULL);
    if (!response) { ext_check(false, "completion response received"); return false; }
    json_value *root = json_parse(response);
    free(response);
    json_value *items = json_obj_get(root, "result");
    if (!items || items->type != JSON_ARR) {
        ext_check(false, "completion result is an array");
        json_free(root);
        return false;
    }
    bool found = false;
    for (int i = 0; i < json_arr_count(items); i++) {
        const char *name = json_get_str(json_obj_get(json_arr_at(items, i), "label"));
        if (name && strcmp(name, label) == 0) found = true;
    }
    json_free(root);
    return found;
}

/* Check every returned occurrence, so correct counts cannot hide a literal
 * fragment being edited in place of a real (possibly nested) hole. */
static bool interpolation_ranges_exact(json_value *items, const char *text,
                                        const char *uri, bool rename) {
    const char *markers[] = {
        "int interpolated", " {interpolated", "{$\"{interpolated", "\"} {interpolated"
    };
    int first = rename ? 0 : 1;
    if (!json_is(items, JSON_ARR) || json_arr_count(items) != 4 - first) return false;
    unsigned seen = 0;
    for (int i = 0; i < json_arr_count(items); i++) {
        json_value *item = json_arr_at(items, i);
        const char *value = json_get_str(json_obj_get(item, rename ? "newText" : "uri"));
        if (!value || strcmp(value, rename ? "formattedValue" : uri) != 0) return false;
        json_value *range = json_obj_get(item, "range");
        json_value *start = json_obj_get(range, "start");
        json_value *end = json_obj_get(range, "end");
        if (json_get_num(json_obj_get(start, "line"), -1) != 0 ||
            json_get_num(json_obj_get(end, "line"), -1) != 0) return false;
        int col = (int)json_get_num(json_obj_get(start, "character"), -1);
        if (json_get_num(json_obj_get(end, "character"), -1) != col + 12) return false;
        bool matched = false;
        for (int m = first; m < 4; m++) {
            const char *at = strstr(text, markers[m]);
            if (at && col == (int)(at + strlen(markers[m]) - 12 - text) && !(seen & (1u << m))) {
                seen |= 1u << m;
                matched = true;
                break;
            }
        }
        if (!matched) return false;
    }
    return true;
}

static int run_completion_checks(child_t *child) {
    const char *uri = "file:///lsp_ast_completion.zan";
    const char *text =
        "class AstBox {\n"
        "    public string Text;\n"
        "    public string Multi\n"
        "        (int value)\n"
        "        { return \"ok\"; }\n"
        "}\n"
        "class AstProgram {\n"
        "    static void Other(int item) { item.ToString(); }\n"
        "    static void Main() {\n"
        "        var item = new AstBox();\n"
        "        item.Text = \"x\";\n"
        "        item.Multi(1).Length;\n"
        "        { string innerOnly = \"x\"; innerOnly.Length; }\n"
        "        innerOnly.Length;\n"
        "    }\n"
        "}\n";
    completion_document(child, uri, text, 1, true);
    ext_check(completion_has(child, uri, text, "        item.", "Text"),
              "completion: var new uses its AST type across same-named parameter");
    ext_check(completion_has(child, uri, text, "        item.", "Multi"),
              "completion: multi-line method declaration is indexed");
    ext_check(completion_has(child, uri, text, "item.Multi(1).", "Length"),
              "completion: multi-line method return resolves member chain");
    ext_check(completion_has(child, uri, text, "{ string innerOnly = \"x\"; innerOnly.", "Length"),
              "completion: single-line nested block local is indexed");
    ext_check(!completion_has(child, uri, text, "        innerOnly.", "Length"),
              "completion: local cannot escape its nested block");

    const char *scope_uri = "file:///lsp_column_scope.zan";
    const char *scope_text =
        "class ColumnP { static void Main() {\n"
        "    string same = \"outer\"; { int same = 1; same.ToString(); } same.ToUpper();\n"
        "    { string scoped = \"x\"; scoped.Length; } scoped.Length;\n"
        "    { string prefixOnly = \"x\"; pref; } pref;\n"
        "    /* \xE4\xB8\xAD\xF0\x9F\x98\x80 */ { string wide = \"x\"; wide.Length; } wide.Length;\n"
        "    for (string loopOnly = \"x\"; false;) { loopOnly.Length; } loopOnly.Length;\n"
        "} }\n";
    completion_document(child, scope_uri, scope_text, 1, true);
    ext_check(!completion_has(child, scope_uri, scope_text, "same = 1; same.", "ToUpper"),
              "completion: inner same-line declaration shadows outer string");
    ext_check(completion_has(child, scope_uri, scope_text, "} same.", "ToUpper"),
              "completion: outer declaration returns after same-line block");
    ext_check(completion_has(child, scope_uri, scope_text, "scoped = \"x\"; scoped.", "Length"),
              "completion: same-line block local is visible inside");
    ext_check(!completion_has(child, scope_uri, scope_text, "} scoped.", "Length"),
              "completion: same-line block local is invisible outside");
    ext_check(completion_has(child, scope_uri, scope_text, "prefixOnly = \"x\"; pref", "prefixOnly"),
              "completion: ordinary prefix includes visible block local");
    ext_check(!completion_has(child, scope_uri, scope_text, "} pref", "prefixOnly"),
              "completion: ordinary prefix excludes expired block local");
    ext_check(completion_has(child, scope_uri, scope_text, "wide = \"x\"; wide.", "Length"),
              "completion: UTF-16 columns preserve block-local visibility");
    ext_check(!completion_has(child, scope_uri, scope_text, "} wide.", "Length"),
              "completion: UTF-16 columns preserve block end");
    ext_check(completion_has(child, scope_uri, scope_text, "{ loopOnly.", "Length"),
              "completion: for initializer is visible inside loop");
    ext_check(!completion_has(child, scope_uri, scope_text, "} loopOnly.", "Length"),
              "completion: for initializer cannot escape loop");

    const char *identity_uri = "file:///lsp_local_identity.zan";
    const char *identity_text =
        "class IdentityP { int same; static void Main() {\n"
        "    string same = \"x\"; same.Length; { int same = 1; same.ToString(); } same.Length;\n"
        "    IdentityP obj = new IdentityP(); obj.same = 1;\n"
        "} }\n";
    completion_document(child, identity_uri, identity_text, 1, true);
    char *response = request_at_marker(child, identity_uri, identity_text, "\"x\"; same",
                                       "textDocument/references", NULL);
    json_value *root = response ? json_parse(response) : NULL;
    if (root && json_arr_count(json_obj_get(root, "result")) != 3) {
        fprintf(stderr, "DEBUG outer local references response (count=%d): %s\n",
                json_arr_count(json_obj_get(root, "result")), response ? response : "(null)");
    }
    ext_check(root && json_arr_count(json_obj_get(root, "result")) == 3,
              "references: outer local excludes shadow and member access");
    json_free(root); free(response);
    response = request_at_marker(child, identity_uri, identity_text, "1; same",
                                  "textDocument/rename", "innerRenamed");
    root = response ? json_parse(response) : NULL;
    json_value *changes = json_obj_get(json_obj_get(root, "result"), "changes");
    ext_check(changes && json_arr_count(json_obj_get(changes, identity_uri)) == 2,
              "rename: inner local edits only its declaration and use");
    json_free(root); free(response);
    response = request_at_marker(child, identity_uri, identity_text, "} same",
                                  "textDocument/definition", NULL);
    root = response ? json_parse(response) : NULL;
    json_value *start = json_obj_get(json_obj_get(json_obj_get(root, "result"), "range"), "start");
    ext_check(start && json_get_num(json_obj_get(start, "line"), -1) == 1 &&
              json_get_num(json_obj_get(start, "character"), -1) == 11,
              "definition: same-line outer use selects outer declaration");
    json_free(root); free(response);

    const char *owners_uri = "file:///lsp_completion_owners.zan";
    const char *owners_text =
        "class OwnerA { private int Secret; public string value; void Tiny() {} void Pick(int correct) {} } "
        "class OwnerB { void Pick(string wrong) {} void Run(OwnerA a, OwnerA[] items) { "
        "int value = 0; a.value.Length; items[0].value.Length; this.Pick(\"x\"); a.Pick(1); a.Secret; hiddenOnly; } } "
        "class Foreign { private int hiddenOnly; }\n";
    completion_document(child, owners_uri, owners_text, 1, true);
    ext_check(!completion_has(child, owners_uri, owners_text, "a.", "Secret"),
              "completion: same-line unrelated type cannot access private");
    ext_check(completion_has(child, owners_uri, owners_text, "items[0].", "value"),
              "completion: array subscript reaches element members");
    ext_check(completion_has(child, owners_uri, owners_text, "this.", "Pick"),
              "completion: this resolves enclosing type");
    ext_check(!completion_has(child, owners_uri, owners_text, "hidden", "hiddenOnly"),
              "completion: ordinary prefix cannot expose foreign private");
    response = request_at_marker(child, owners_uri, owners_text, "a.value",
                                  "textDocument/definition", NULL);
    root = response ? json_parse(response) : NULL;
    start = json_obj_get(json_obj_get(json_obj_get(root, "result"), "range"), "start");
    ext_check(start && json_get_num(json_obj_get(start, "character"), -1) ==
              (double)(strstr(owners_text, "value;") - owners_text),
              "definition: receiver member cannot select same-named local");
    json_free(root); free(response);
    response = request_at_marker(child, owners_uri, owners_text, "a.Pick(",
                                  "textDocument/signatureHelp", NULL);
    root = response ? json_parse(response) : NULL;
    json_value *signatures = json_obj_get(json_obj_get(root, "result"), "signatures");
    const char *signature = json_get_str(json_obj_get(json_arr_at(signatures, 0), "label"));
    ext_check(signature && strstr(signature, "int correct") && !strstr(signature, "string wrong"),
              "signature: local receiver selects exact method owner");
    json_free(root); free(response);

    const char *generics_uri = "file:///lsp_generic_chain.zan";
    const char *generics_text =
        "class GenericBox<T> { public T Fetch() { return default(T); } }\n"
        "class GenericProgram { static void Main() { var later = Later(); later.Length; "
        "GenericBox<string> box = new GenericBox<string>(); box.Fetch().Length; "
        "List<int> numbers = new List<int>(); numbers.ToArray().Length; "
        "Dictionary<string,List<string>> map = new Dictionary<string,List<string>>(); map[\"a\"][0].Length; "
        "StringBuilder builder = new StringBuilder(); builder.Append(\"x\").Length; "
        "box.Missing().Fetch(); } static string Later() { return \"x\"; } }\n";
    completion_document(child, generics_uri, generics_text, 1, true);
    ext_check(completion_has(child, generics_uri, generics_text, "later.", "Length"),
              "completion: var initializer resolves later-declared method");
    ext_check(completion_has(child, generics_uri, generics_text, "box.Fetch().", "Length"),
              "completion: generic method return substitutes receiver arguments");
    ext_check(completion_has(child, generics_uri, generics_text, "numbers.ToArray().", "Length"),
              "completion: builtin collection return exposes array members");
    ext_check(completion_has(child, generics_uri, generics_text, "map[\"a\"][0].", "Length"),
              "completion: nested dictionary and list subscript resolves element");
    ext_check(!completion_has(child, generics_uri, generics_text, "builder.Append(\"x\").", "Length"),
              "completion: void builtin result cannot create fluent chain");
    ext_check(!completion_has(child, generics_uri, generics_text, "box.Missing().", "Fetch"),
              "completion: unknown method cannot retain receiver type");

    const char *suffix_uri = "file:///lsp_receiver_suffix.zan";
    const char *suffix_text =
        "class StaticBox<T> { public static T Make() { return default(T); } public T Fetch() { return default(T); } }\n"
        "class StringBox : StaticBox<string> { void Check() { base.Fetch().Length; } }\n"
        "class SuffixHolder { public string[] Values; }\n"
        "class SuffixP { static int Index() { return 0; } static void Main() { "
        "string[] items = new string[1]; items[Index()].Length; "
        "SuffixHolder holder = new SuffixHolder(); holder.Values[Index()].Length; "
        "Dictionary<string,string> map = new Dictionary<string,string>(); map[\"(\"].Length; "
        "StaticBox<string>.Make().Length; } }\n";
    completion_document(child, suffix_uri, suffix_text, 1, true);
    ext_check(completion_has(child, suffix_uri, suffix_text, "items[Index()].", "Length"),
              "completion: index argument call does not turn array root into call");
    ext_check(completion_has(child, suffix_uri, suffix_text, "holder.Values[Index()].", "Length"),
              "completion: member index argument call does not alter member identity");
    ext_check(completion_has(child, suffix_uri, suffix_text, "map[\"(\"].", "Length"),
              "completion: parenthesis in dictionary key is literal text");
    ext_check(completion_has(child, suffix_uri, suffix_text, "StaticBox<string>.", "Make"),
              "completion: generic static receiver resolves its bare declaration");
    ext_check(completion_has(child, suffix_uri, suffix_text, "StaticBox<string>.Make().", "Length"),
              "completion: generic static call retains concrete type argument");
    ext_check(completion_has(child, suffix_uri, suffix_text, "base.Fetch().", "Length"),
              "completion: base receiver retains generic argument");

    const char *factory_uri = "file:///lsp_project_factory.zan";
    const char *factory_text = "class ProjectFactory { public static string Fetch() { return \"x\"; } }\n";
    const char *consumer_uri = "file:///lsp_factory_consumer.zan";
    const char *consumer_text =
        "class FactoryConsumer { static void Main() { var product = ProjectFactory.Fetch(); product.ToString(); } }\n";
    completion_document(child, factory_uri, factory_text, 1, true);
    completion_document(child, consumer_uri, consumer_text, 1, true);
    ext_check(completion_has(child, consumer_uri, consumer_text, "product.", "ToUpper"),
              "completion: cross-file static factory infers var initializer");
    const char *factory_v2 = "class ProjectFactory { public static int Fetch() { return 1; } }\n";
#ifdef _WIN32
    const char *factory_alias = "file:///LSP_PROJECT_FACTORY.zan";
#else
    const char *factory_alias = factory_uri;
#endif
    completion_document(child, factory_alias, factory_v2, 2, false);
    ext_check(!completion_has(child, consumer_uri, consumer_text, "product.", "ToUpper"),
              "completion: provider edit invalidates consumer initializer cache");
    ext_check(completion_has(child, consumer_uri, consumer_text, "product.", "ToString"),
              "completion: edited provider still infers a concrete scalar type");
    response = request_at_marker(child, consumer_uri, consumer_text, "ProjectFactory.Fetch",
                                  "textDocument/definition", NULL);
    root = response ? json_parse(response) : NULL;
    ext_check(root && json_is(json_obj_get(root, "result"), JSON_OBJ),
              "definition: provider case alias replaces its declaration identity");
    json_free(root); free(response);

    const char *implicit_uri = "file:///lsp_implicit_parameter.zan";
    const char *implicit_text = "class ImplicitP { int P { set { Console.WriteLine(value); } } }\n";
    completion_document(child, implicit_uri, implicit_text, 1, true);
    response = request_at_marker(child, implicit_uri, implicit_text, "(value",
                                  "textDocument/prepareRename", NULL);
    root = response ? json_parse(response) : NULL;
    ext_check(root && json_obj_get(root, "result") && json_obj_get(root, "result")->type == JSON_NULL,
              "rename: implicit setter value has no source declaration");
    json_free(root); free(response);

    const char *rename_uri = "file:///lsp_member_identity.zan";
    const char *rename_text =
        "class RenameA { int count; void F() { count = 1; } } "
        "class RenameB { int count; void F() { int count = 0; } }\n";
    completion_document(child, rename_uri, rename_text, 1, true);
    response = request_at_marker(child, rename_uri, rename_text, "RenameA { int count",
                                  "textDocument/rename", "total");
    root = response ? json_parse(response) : NULL;
    changes = json_obj_get(json_obj_get(root, "result"), "changes");
    bool member_rename_safe = root && (json_obj_get(root, "error") ||
                             json_is(json_obj_get(root, "result"), JSON_NULL));
    if (changes) {
        json_value *edits = json_obj_get(changes, rename_uri);
        member_rename_safe = json_arr_count(edits) == 2;
        for (int i = 0; i < json_arr_count(edits); i++) {
            json_value *edit_start = json_obj_get(json_obj_get(json_arr_at(edits, i), "range"), "start");
            if (json_get_num(json_obj_get(edit_start, "character"), -1) >=
                (double)(strstr(rename_text, "class RenameB") - rename_text)) member_rename_safe = false;
        }
    }
    ext_check(member_rename_safe, "rename: member cannot edit unrelated type or local");
    json_free(root); free(response);

    /* Unresolved word: the lexical fallback must stay in the origin document —
     * same-named text in other files has no proven identity to rename. */
    const char *fb_a_uri = "file:///lsp_rename_fallback_a.zan";
    const char *fb_a_text = "class FallbackA { void F() { mystery = 1; } }\n";
    const char *fb_b_uri = "file:///lsp_rename_fallback_b.zan";
    const char *fb_b_text = "class FallbackB { void F() { mystery = 2; } }\n";
    completion_document(child, fb_a_uri, fb_a_text, 1, true);
    completion_document(child, fb_b_uri, fb_b_text, 1, true);
    response = request_at_marker(child, fb_a_uri, fb_a_text, "mystery",
                                  "textDocument/rename", "solved");
    root = response ? json_parse(response) : NULL;
    changes = json_obj_get(json_obj_get(root, "result"), "changes");
    ext_check(changes && json_obj_get(changes, fb_a_uri) &&
              json_arr_count(json_obj_get(changes, fb_a_uri)) == 1 &&
              !json_obj_get(changes, fb_b_uri),
              "rename: unresolved word stays in the origin document");
    json_free(root); free(response);

    const char *refs_uri = "file:///lsp_reference_consumer.zan";
    const char *refs_text =
        "class ReferenceConsumer { void Use(RenameA a, RenameB b) { a.count = 2; b.count = 3; int count = 4; } }\n";
    completion_document(child, refs_uri, refs_text, 1, true);
    for (int include_decl = 0; include_decl < 2; include_decl++) {
        response = request_at_marker_ex(child, rename_uri, rename_text, "RenameA { int count",
                                         "textDocument/references", NULL, include_decl != 0);
        root = response ? json_parse(response) : NULL;
        json_value *locations = json_obj_get(root, "result");
        ext_check(locations && json_arr_count(locations) == 2 + include_decl,
                  include_decl ? "references: exact member identity includes declaration"
                               : "references: exact member identity excludes declaration");
        int consumer_hits = 0;
        for (int i = 0; i < json_arr_count(locations); i++) {
            json_value *location = json_arr_at(locations, i);
            const char *location_uri = json_get_str(json_obj_get(location, "uri"));
            if (location_uri && strcmp(location_uri, refs_uri) == 0) {
                consumer_hits++;
                start = json_obj_get(json_obj_get(location, "range"), "start");
                ext_check(json_get_num(json_obj_get(start, "character"), -1) ==
                          (double)(strstr(refs_text, "a.count") + 2 - refs_text),
                          "references: cross-file receiver excludes unrelated member and local");
            }
        }
        ext_check(consumer_hits == 1, "references: cross-file occurrence resolves exact declaration");
        json_free(root); free(response);
    }

    response = request_at_marker_ex(child, identity_uri, identity_text, "\"x\"; same",
                                     "textDocument/references", NULL, false);
    root = response ? json_parse(response) : NULL;
    ext_check(root && json_arr_count(json_obj_get(root, "result")) == 2,
              "references: local includeDeclaration false excludes exact declaration");
    json_free(root); free(response);

    const char *interp_uri = "file:///lsp_interpolation_identity.zan";
    const char *interp_text =
        "class InterpolationP { void Use() { int interpolated = 1; "
        "string literal = \"interpolated\"; "
        "string message = $\"interpolated {{interpolated}} {interpolated} {$\"{interpolated}\"} {interpolated:D4}\"; } }\n";
    completion_document(child, interp_uri, interp_text, 1, true);
    response = request_at_marker(child, interp_uri, interp_text, "int interpolated",
                                  "textDocument/rename", "formattedValue");
    root = response ? json_parse(response) : NULL;
    changes = json_obj_get(json_obj_get(root, "result"), "changes");
    bool interpolation_rename = changes && interpolation_ranges_exact(json_obj_get(changes, interp_uri),
                                                                       interp_text, interp_uri, true);
    ext_check(interpolation_rename,
              "rename: nested interpolation holes are code and literal fragments stay untouched");
    if (!interpolation_rename) fprintf(stderr, "插值重命名响应: %s\n", response ? response : "(null)");
    json_free(root); free(response);
    response = request_at_marker_ex(child, interp_uri, interp_text, " {interpolated",
                                     "textDocument/references", NULL, false);
    root = response ? json_parse(response) : NULL;
    bool interpolation_refs = root && interpolation_ranges_exact(json_obj_get(root, "result"),
                                                                   interp_text, interp_uri, false);
    ext_check(interpolation_refs,
              "references: interpolation target excludes declaration and literal names");
    if (!interpolation_refs) fprintf(stderr, "插值引用响应: %s\n", response ? response : "(null)");
    json_free(root); free(response);
    response = request_at_marker(child, interp_uri, interp_text, "int interpolated",
                                  "textDocument/rename", "class");
    root = response ? json_parse(response) : NULL;
    ext_check(root && json_obj_get(root, "error") && !json_obj_get(root, "result"),
              "rename: lexer keyword cannot be a replacement identifier");
    json_free(root); free(response);

    const char *capture_uri = "file:///lsp_rename_capture.zan";
    const char *capture_text =
        "class CaptureP { void Use() { int outerValue = 1; "
        "{ int nestedValue = 2; outerValue += nestedValue; } outerValue++; "
        "Func<int,int> mapper = (int lambdaValue) => outerValue + lambdaValue; } }\n";
    completion_document(child, capture_uri, capture_text, 1, true);
    response = request_at_marker(child, capture_uri, capture_text, "int outerValue",
                                  "textDocument/rename", "nestedValue");
    root = response ? json_parse(response) : NULL;
    ext_check(root && json_obj_get(root, "error") && !json_obj_get(root, "result"),
              "rename: nested declaration cannot capture renamed outer uses");
    json_free(root); free(response);
    response = request_at_marker(child, capture_uri, capture_text, "int nestedValue",
                                  "textDocument/rename", "outerValue");
    root = response ? json_parse(response) : NULL;
    ext_check(root && json_obj_get(root, "error") && !json_obj_get(root, "result"),
              "rename: nested local cannot capture pre-existing outer references");
    json_free(root); free(response);
    response = request_at_marker(child, capture_uri, capture_text, "int outerValue",
                                  "textDocument/rename", "lambdaValue");
    root = response ? json_parse(response) : NULL;
    ext_check(root && json_obj_get(root, "error") && !json_obj_get(root, "result"),
              "rename: lambda parameter cannot capture renamed outer references");
    json_free(root); free(response);

    const char *version_uri = "file:///lsp_version_completion.zan";
    const char *v1 = "class VersionP { static void Main() { string value = \"x\"; value.ToString(); } }\n";
    const char *v2 = "class VersionP { static void Main() { int    value = 1  ; value.ToString(); } }\n";
    completion_document(child, version_uri, v1, 10, true);
    ext_check(completion_has(child, version_uri, v1, "value.", "ToUpper"),
              "completion: single-line method local is available");
    completion_document(child, version_uri, v2, 12, false);
    ext_check(!completion_has(child, version_uri, v2, "value.", "ToUpper"),
              "completion: latest unsaved text replaces cached type");
    completion_document(child, version_uri, v1, 11, false);
    ext_check(!completion_has(child, version_uri, v2, "value.", "ToUpper"),
              "completion: stale document version cannot replace newer text");
    completion_document(child, version_uri, v1, 12, false);
    ext_check(!completion_has(child, version_uri, v2, "value.", "ToUpper"),
              "completion: duplicate document version cannot replace newer text");
    completion_document(child, version_uri, v1, 13, false);
    ext_check(completion_has(child, version_uri, v1, "value.", "ToUpper"),
              "completion: newer document version invalidates cached type");
    printf("\n%d completion failure(s)\n", ext_fails);
    return ext_fails ? 1 : 0;
}

/* --------- scope-aware rename/references + engine cache ---- */

#define SC_URI "file:///lsp_scope_test.zan"
/* Two methods each with a local `count`: scope-aware rename of the one in
 * Add() (line 4) must not touch the unrelated one in Other() (line 8). */
static const char *SC_DOC =
    "class S {\n"
    "    int total = 0;\n"
    "    static void Add() {\n"
    "        int count = 1;\n"       /* line 3 */
    "        count = count + 1;\n"   /* line 4: rename target */
    "        Console.WriteLine(count);\n" /* line 5 */
    "    }\n"
    "    static void Other() {\n"
    "        int count = 9;\n"       /* line 8: must stay untouched */
    "        Console.WriteLine(count);\n" /* line 9: must stay untouched */
    "    }\n"
    "}\n";

static void scope_open(child_t *child) {
    json_value *td = json_new_obj();
    json_obj_set(td, "uri", json_new_str(SC_URI));
    json_obj_set(td, "languageId", json_new_str("zan"));
    json_obj_set(td, "version", json_new_num(1));
    json_obj_set(td, "text", json_new_str(SC_DOC));
    json_value *params = json_new_obj();
    json_obj_set(params, "textDocument", td);
    send_message(child, mk_request(-1, "textDocument/didOpen", params));
}

static json_value *scope_position(int line, int character) {
    json_value *pos = json_new_obj();
    json_obj_set(pos, "line", json_new_num(line));
    json_obj_set(pos, "character", json_new_num(character));
    return pos;
}

static int run_scope_checks(child_t *child) {
    scope_open(child);

    /* rename the local `count` in Add() (line 4, char 8) -> "tally" */
    {
        json_value *td = json_new_obj();
        json_obj_set(td, "uri", json_new_str(SC_URI));
        json_value *params = json_new_obj();
        json_obj_set(params, "textDocument", td);
        json_obj_set(params, "position", scope_position(4, 8));
        json_obj_set(params, "newName", json_new_str("tally"));
        send_message(child, mk_request(20, "textDocument/rename", params));
        char *r = recv_until_id(child, 20);
        if (r) {
            int hits = 0;
            for (const char *p = r; (p = strstr(p, "\"newText\":\"tally\"")) != NULL; p++) hits++;
            ext_check(hits == 4, "rename: local renamed 4 times inside Add() only");
            ext_check(strstr(r, SC_URI) != NULL, "rename: edits target the scope doc");
            ext_check(strstr(r, "\"line\":8") == NULL && strstr(r, "\"line\":9") == NULL,
                      "rename: Other()'s same-named local untouched");
        } else {
            ext_check(false, "rename: no response");
        }
        free(r);
    }

    /* references on Other()'s own `count` (line 8; "int " occupies chars
     * 8..11, the identifier starts at char 12) */
    {
        json_value *td = json_new_obj();
        json_obj_set(td, "uri", json_new_str(SC_URI));
        json_value *params = json_new_obj();
        json_obj_set(params, "textDocument", td);
        json_obj_set(params, "position", scope_position(8, 12));
        json_value *context = json_new_obj();
        json_obj_set(context, "includeDeclaration", json_new_bool(true));
        json_obj_set(params, "context", context);
        send_message(child, mk_request(21, "textDocument/references", params));
        char *r = recv_until_id(child, 21);
        if (r) {
            int locs = 0;
            for (const char *p = r; (p = strstr(p, "\"uri\":\"file:///lsp_scope_test.zan\"")) != NULL; p++)
                locs++;
            ext_check(locs == 2, "references: local reports only its own method (2)");
        } else {
            ext_check(false, "references: no response");
        }
        free(r);
    }

    /* engine cache: symbols -> definition on another doc -> symbols again.
     * Each switch must rebuild on the uri change and still answer right. */
    {
        json_value *td = json_new_obj();
        json_obj_set(td, "uri", json_new_str(SC_URI));
        json_value *params = json_new_obj();
        json_obj_set(params, "textDocument", td);
        send_message(child, mk_request(22, "textDocument/documentSymbol", params));
        char *r = recv_until_id(child, 22);
        ext_check(r && strstr(r, "\"name\":\"Add\"") && strstr(r, "\"name\":\"Other\""),
                  "cache: documentSymbol on scope doc lists methods");
        ext_check(r && strstr(r, "\"name\":\"count\"") == NULL,
                  "cache: documentSymbol excludes locals");
        free(r);
    }
    {
        json_value *td = json_new_obj();
        json_obj_set(td, "uri", json_new_str(TEST_URI));
        json_value *params = json_new_obj();
        json_obj_set(params, "textDocument", td);
        json_obj_set(params, "position", scope_position(4, 39));
        send_message(child, mk_request(23, "textDocument/definition", params));
        char *r = recv_until_id(child, 23);
        ext_check(r && strstr(r, "\"character\":31") != NULL,
                  "cache: definition still exact after uri switch");
        free(r);
    }
    {
        json_value *td = json_new_obj();
        json_obj_set(td, "uri", json_new_str(SC_URI));
        json_value *params = json_new_obj();
        json_obj_set(params, "textDocument", td);
        send_message(child, mk_request(24, "textDocument/documentSymbol", params));
        char *r = recv_until_id(child, 24);
        ext_check(r && strstr(r, "\"name\":\"Add\"") != NULL,
                  "cache: switching back rebuilds the scope doc");
        free(r);
    }

    printf("\n%d scope failure(s)\n", ext_fails);
    return ext_fails ? 1 : 0;
}

/* --------- semanticTokens and inlay hints checks --------- */
#define HINT_URI "file:///lsp_hint_test.zan"
static const char *HINT_DOC =
    "class Greeter {\n"
    "    static void Hello(string name, int age) {\n"
    "    }\n"
    "    static void Run() {\n"
    "        var x = 100;\n"
    "        Hello(\"world\", 42);\n"
    "    }\n"
    "}\n";

static int run_semantic_and_hint_checks(child_t *child) {
    /* open HINT_DOC */
    {
        json_value *td = json_new_obj();
        json_obj_set(td, "uri", json_new_str(HINT_URI));
        json_obj_set(td, "languageId", json_new_str("zan"));
        json_obj_set(td, "version", json_new_num(1));
        json_obj_set(td, "text", json_new_str(HINT_DOC));
        json_value *params = json_new_obj();
        json_obj_set(params, "textDocument", td);
        send_message(child, mk_request(-1, "textDocument/didOpen", params));
    }

    /* textDocument/inlayHint (id 30) */
    {
        json_value *td = json_new_obj();
        json_obj_set(td, "uri", json_new_str(HINT_URI));
        json_value *params = json_new_obj();
        json_obj_set(params, "textDocument", td);
        send_message(child, mk_request(30, "textDocument/inlayHint", params));
        char *r = recv_until_id(child, 30);
        ext_check(r && strstr(r, ": int") != NULL,
                  "inlayHint: inferred type ': int' emitted for var x");
        ext_check(r && strstr(r, "name:") != NULL && strstr(r, "age:") != NULL,
                  "inlayHint: parameter names 'name:' and 'age:' emitted for call");
        free(r);
    }

    /* textDocument/semanticTokens/full (id 31) */
    {
        json_value *td = json_new_obj();
        json_obj_set(td, "uri", json_new_str(HINT_URI));
        json_value *params = json_new_obj();
        json_obj_set(params, "textDocument", td);
        send_message(child, mk_request(31, "textDocument/semanticTokens/full", params));
        char *r = recv_until_id(child, 31);
        ext_check(r && strstr(r, "\"data\":[") != NULL,
                  "semanticTokens: returned data array");
        /* ensure data array is non-empty */
        ext_check(r && strstr(r, "\"data\":[]") == NULL,
                  "semanticTokens: non-empty token data returned");
        free(r);
    }

    printf("\n%d semantic/hint failure(s)\n", ext_fails);
    return ext_fails ? 1 : 0;
}

/* --------- $/cancelRequest checks (reader/worker split) ---------
 *
 * A second server rooted at a generated workspace of ~300 files, so project
 * indexing and the references file walk take seconds. The reader thread must
 * stay unblocked while the worker executes, so a cancel lands while a
 * request is queued or mid-walk: the cancelled request must answer -32800
 * and the request behind it must still complete. */
#define CANCEL_FILES 300
#define CANCEL_FILLER_LINES 1200

/* references at line 1 char 8 = the `cancelMe` field (a field, not a
 * local, so the scope fast path does not shorten the walk). */
static json_value *mk_refs_params(const char *doc_uri) {
    json_value *td = json_new_obj();
    json_obj_set(td, "uri", json_new_str(doc_uri));
    json_value *pos = json_new_obj();
    json_obj_set(pos, "line", json_new_num(1));
    json_obj_set(pos, "character", json_new_num(8));
    json_value *params = json_new_obj();
    json_obj_set(params, "textDocument", td);
    json_obj_set(params, "position", pos);
    json_value *context = json_new_obj();
    json_obj_set(context, "includeDeclaration", json_new_bool(false));
    json_obj_set(params, "context", context);
    return params;
}

static int run_cancel_checks(const char *exe) {
    int fails = 0;
    char root[600];
#ifdef _WIN32
    char tmp[MAX_PATH];
    GetTempPathA(sizeof(tmp), tmp);
    snprintf(root, sizeof(root), "%szan_lsp_cancel_%lu", tmp,
             (unsigned long)GetCurrentProcessId());
#else
    snprintf(root, sizeof(root), "/tmp/zan_lsp_cancel_%d", (int)getpid());
#endif

#ifdef _WIN32
    CreateDirectoryA(root, NULL);
#else
    mkdir(root, 0755);
#endif

    /* Each generated file references the same Doc.cancelMe declaration, so
     * the semantic references walk must still report one location per file. */
    for (int i = 0; i < CANCEL_FILES; i++) {
        char path[700];
        snprintf(path, sizeof(path), "%s%cGen%d.zan", root,
#ifdef _WIN32
            '\\',
#else
            '/',
#endif
            i);
        FILE *f = fopen(path, "wb");
        if (!f) { fprintf(stderr, "FAIL: cannot create %s\n", path); return 1; }
        fprintf(f, "class Gen%d {\n", i);
        for (int l = 0; l < CANCEL_FILLER_LINES; l++)
            fprintf(f, "    int pad%d = %d;\n", l, l);
        fprintf(f, "    void Use(Doc doc) { doc.cancelMe = 1; }\n}\n");
        fclose(f);
    }

    child_t child;
    if (!child_spawn(&child, exe)) {
        fprintf(stderr, "FAIL: cannot spawn zan-lsp (cancel group)\n");
        return 1;
    }

    char uri[700];
    snprintf(uri, sizeof(uri), "file:///Gen0.zan");

    /* initialize with the generated workspace as root */
    {
        json_value *params = json_new_obj();
        char ruri[700];
#ifdef _WIN32
        /* the server only accepts file:/// URIs; backslashes become '/' */
        snprintf(ruri, sizeof(ruri), "file:///");
        char *wp = ruri + strlen(ruri);
        for (const char *q = root; *q && wp < ruri + sizeof(ruri) - 1; q++)
            *wp++ = (*q == '\\') ? '/' : *q;
        *wp = '\0';
#else
        snprintf(ruri, sizeof(ruri), "file://%s", root);
#endif
        json_obj_set(params, "rootUri", json_new_str(ruri));
        send_message(&child, mk_request(1, "initialize", params));
        char *r = recv_until_id(&child, 1);
        if (!r) {
            fprintf(stderr, "FAIL: no initialize response (cancel group)\n");
            child_close(&child);
            return 1;
        }
        free(r);
    }

    /* didOpen: the worker starts the (slow) project index for this root. */
    {
        json_value *td = json_new_obj();
        json_obj_set(td, "uri", json_new_str(uri));
        json_obj_set(td, "languageId", json_new_str("zan"));
        json_obj_set(td, "version", json_new_num(1));
        json_obj_set(td, "text", json_new_str(
            "class Doc {\n    int cancelMe;\n}\n"));
        json_value *params = json_new_obj();
        json_obj_set(params, "textDocument", td);
        send_message(&child, mk_request(-1, "textDocument/didOpen", params));
    }

    /* All three rounds rely on the same slow path: indexing the generated
     * workspace takes seconds, while the reader thread accepts a cancel in
     * microseconds, so every cancel lands well inside the window.
     *
     * Round 1: didOpen started the cold index build inside the worker;
     * refs(100) queues behind it, so the cancel slot is recorded long before
     * refs(100) is dequeued and the worker refuses it without executing. */
    send_message(&child, mk_request(100, "textDocument/references",
                                    mk_refs_params(uri)));
    {
        json_value *cancel = json_new_obj();
        json_obj_set(cancel, "jsonrpc", json_new_str("2.0"));
        json_obj_set(cancel, "method", json_new_str("$/cancelRequest"));
        json_value *cp = json_new_obj();
        json_obj_set(cp, "id", json_new_num(100));
        json_obj_set(cancel, "params", cp);
        send_message(&child, cancel);
    }
    char *r100 = recv_until_id(&child, 100);
    ext_check(r100 && strstr(r100, "-32800") != NULL
              && strstr(r100, "\"error\"") != NULL,
              "cancel: queued references request answers -32800");
    free(r100);

    /* Round 2: the cancelled build left the index cold, so refs(101) restarts
     * the multi-second scan itself and the cancel hits its checkpoints
     * mid-walk; the answer must be -32800, never a partial success. */
    send_message(&child, mk_request(101, "textDocument/references",
                                    mk_refs_params(uri)));
    {
        json_value *cancel = json_new_obj();
        json_obj_set(cancel, "jsonrpc", json_new_str("2.0"));
        json_obj_set(cancel, "method", json_new_str("$/cancelRequest"));
        json_value *cp = json_new_obj();
        json_obj_set(cp, "id", json_new_num(101));
        json_obj_set(cancel, "params", cp);
        send_message(&child, cancel);
    }
    char *r101 = recv_until_id(&child, 101);
    ext_check(r101 && strstr(r101, "-32800") != NULL,
              "cancel: in-flight references walk aborts with -32800");
    free(r101);

    /* Round 3: after two aborts the index is rebuilt from scratch; the
     * pipeline must not be wedged and the walk must cover the project. */
    send_message(&child, mk_request(102, "textDocument/references",
                                    mk_refs_params(uri)));
    char *r102 = recv_until_id(&child, 102);
    ext_check(r102 && strstr(r102, "\"result\"") != NULL,
              "cancel: pipeline still serves requests after cancellations");
    ext_check(r102 && strstr(r102, "Gen1") != NULL,
              "cancel: post-cancel walk still scans project files");
    json_value *refs_response = r102 ? json_parse(r102) : NULL;
    int got_count = refs_response ? json_arr_count(json_obj_get(refs_response, "result")) : -1;
    ext_check(refs_response && got_count == CANCEL_FILES,
              "cancel: reopened pipeline resolves one exact declaration across all unopened files");
    json_free(refs_response);
    free(r102);

    /* shutdown + exit */
    send_message(&child, mk_request(199, "shutdown", json_new_obj()));
    {
        json_value *msg = json_new_obj();
        json_obj_set(msg, "jsonrpc", json_new_str("2.0"));
        json_obj_set(msg, "method", json_new_str("exit"));
        send_message(&child, msg);
    }
    child_close(&child);

    fails = ext_fails;
    printf("\n%d cancel failure(s)\n", fails);

    for (int i = 0; i < CANCEL_FILES; i++) {
        char path[700];
        snprintf(path, sizeof(path), "%s%cGen%d.zan", root,
#ifdef _WIN32
            '\\',
#else
            '/',
#endif
            i);
#ifdef _WIN32
        DeleteFileA(path);
#else
        unlink(path);
#endif
    }
#ifdef _WIN32
    RemoveDirectoryA(root);
#else
    rmdir(root);
#endif
    return fails ? 1 : 0;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: lsp_integration_test <zan-lsp>\n");
        return 2;
    }

    child_t child;
    if (!child_spawn(&child, argv[1])) {
        fprintf(stderr, "FAIL: cannot spawn zan-lsp\n");
        return 1;
    }

    /* initialize (id 1) -- no positionEncoding cap: the server must do UTF-16 */
    {
        json_value *msg = json_new_obj();
        json_obj_set(msg, "jsonrpc", json_new_str("2.0"));
        json_obj_set(msg, "id", json_new_num(1));
        json_obj_set(msg, "method", json_new_str("initialize"));
        json_obj_set(msg, "params", json_new_obj());
        send_message(&child, msg);
    }
    char *resp = recv_until_id(&child, 1);
    if (!resp) {
        fprintf(stderr, "FAIL: no initialize response\n");
        child_close(&child);
        return 1;
    }
    free(resp);

    /* textDocument/didOpen -- registers the doc and triggers diagnostics */
    {
        json_value *params = json_new_obj();
        json_obj_set(params, "textDocument", mk_text_document());
        json_value *msg = json_new_obj();
        json_obj_set(msg, "jsonrpc", json_new_str("2.0"));
        json_obj_set(msg, "method", json_new_str("textDocument/didOpen"));
        json_obj_set(msg, "params", params);
        send_message(&child, msg);
    }

    /* textDocument/definition (id 3) at line 4, UTF-16 col 39 -> `Helper` */
    {
        json_value *td = json_new_obj();
        json_obj_set(td, "uri", json_new_str(TEST_URI));
        json_value *pos = json_new_obj();
        json_obj_set(pos, "line", json_new_num(4));
        json_obj_set(pos, "character", json_new_num(39));
        json_value *params = json_new_obj();
        json_obj_set(params, "textDocument", td);
        json_obj_set(params, "position", pos);
        json_value *msg = json_new_obj();
        json_obj_set(msg, "jsonrpc", json_new_str("2.0"));
        json_obj_set(msg, "id", json_new_num(3));
        json_obj_set(msg, "method", json_new_str("textDocument/definition"));
        json_obj_set(msg, "params", params);
        send_message(&child, msg);
    }

    char *def = recv_until_id(&child, 3);
    if (!def) {
        fprintf(stderr, "FAIL: no definition response\n");
        child_close(&child);
        return 1;
    }

    int rc = 1;
    json_value *root = json_parse(def);
    free(def);
    if (!root) {
        fprintf(stderr, "FAIL: unparseable definition response\n");
        child_close(&child);
        return 1;
    }

    json_value *result = json_obj_get(root, "result");
    json_value *range = result ? json_obj_get(result, "range") : NULL;
    json_value *start = range ? json_obj_get(range, "start") : NULL;
    if (!result || !range || !start) {
        fprintf(stderr, "FAIL: definition came back null (wrong position "
                        "encoding? expected a Location at line 2 col 31)\n");
        json_free(root);
        child_close(&child);
        return 1;
    }

    double sl = json_get_num(json_obj_get(start, "line"), -1);
    double sc = json_get_num(json_obj_get(start, "character"), -1);
    double ec = json_get_num(json_obj_get(json_obj_get(range, "end"), "character"), -1);

    if (sl == 2 && sc == 31 && ec == 37) {
        printf("PASS: definition of Helper at line %d, col %d (UTF-16)\n",
               (int)sl, (int)sc);
        rc = 0;
    } else {
        fprintf(stderr, "FAIL: got definition at line %d col %d (end %d); "
                        "expected line 2 col 31 (end 37)\n",
                        (int)sl, (int)sc, (int)ec);
    }

    json_free(root);

    if (rc == 0)
        rc = run_extended_checks(&child);

    if (rc == 0)
        rc = run_scope_checks(&child);

    if (rc == 0)
        rc = run_completion_checks(&child);

    if (rc == 0)
        rc = run_semantic_and_hint_checks(&child);

    /* spawns its own second server rooted at a generated workspace */
    if (rc == 0)
        rc = run_cancel_checks(argv[1]);

    child_close(&child);
    return rc;
}
