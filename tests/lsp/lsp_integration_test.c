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
    json_free(body);
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

    /* documentHighlight: every whole-word occurrence of Helper */
    if (r[1]) {
        int hits = 0;
        for (const char *p = r[1]; (p = strstr(p, "\"kind\":1")) != NULL; p++) hits++;
        ext_check(hits >= 2, "documentHighlight: >= 2 occurrences highlighted");
    } else {
        ext_check(false, "documentHighlight: no response");
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

    /* Generated project: each file mentions `cancelMe` exactly once, so a
     * full references walk reports one location per file. */
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
        fprintf(f, "    int cancelMe;\n}\n");
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
    json_free(resp);

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
    json_free(def);
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
        rc = run_semantic_and_hint_checks(&child);

    /* spawns its own second server rooted at a generated workspace */
    if (rc == 0)
        rc = run_cancel_checks(argv[1]);

    child_close(&child);
    return rc;
}
