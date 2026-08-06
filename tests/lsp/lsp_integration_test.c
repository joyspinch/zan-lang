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
    child_close(&child);
    if (!def) {
        fprintf(stderr, "FAIL: no definition response\n");
        return 1;
    }

    int rc = 1;
    json_value *root = json_parse(def);
    json_free(def);
    if (!root) {
        fprintf(stderr, "FAIL: unparseable definition response\n");
        return 1;
    }

    json_value *result = json_obj_get(root, "result");
    json_value *range = result ? json_obj_get(result, "range") : NULL;
    json_value *start = range ? json_obj_get(range, "start") : NULL;
    if (!result || !range || !start) {
        fprintf(stderr, "FAIL: definition came back null (wrong position "
                        "encoding? expected a Location at line 2 col 31)\n");
        json_free(root);
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
    return rc;
}
