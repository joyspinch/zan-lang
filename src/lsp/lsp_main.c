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
 *        zan-lsp --port <N> (listens on 127.0.0.1:<N> for a single client)
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
#include "zan_version.h"
#include "package.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <ctype.h>

#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#include <winsock2.h>
#include <windows.h>
typedef SOCKET lsp_sock_t;
#define LSP_INVALID_SOCK INVALID_SOCKET
#else
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <sys/stat.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
typedef int lsp_sock_t;
#define LSP_INVALID_SOCK (-1)
#endif

#include "../common/host_oom.h"
/* ============================ document store ========================== */

#define LSP_MAX_DOCS 256

/* Diagnostics are published by a worker thread once edits go quiet for
 * this long; keystrokes never wait on the front-end run. */
#define LSP_DIAG_QUIET_MS 200

/* One queued incoming message (request or notification), parsed by the
 * reader thread. msg == NULL marks the shutdown sentinel. */
typedef struct lsp_msg {
    struct lsp_msg *next;
    json_value *msg;
} lsp_msg_t;

typedef struct {
    char *uri;
    char *text;
    long    version;         /* bumped on every accepted didChange */
    bool    diag_pending;    /* front-end run queued for this doc */
    uint64_t last_change_ms; /* when the latest edit landed */
} lsp_doc_t;

typedef struct {
    lsp_doc_t docs[LSP_MAX_DOCS];
    int       doc_count;
    bool      shutdown_requested;
    bool      project_indexed;
    char      workspace_root[1024];
    FILE     *out;
    bool       use_sock;  /* true when framing over a TCP socket */
    lsp_sock_t sock;      /* connected client socket (server mode) */
    /* doc_lock guards the doc store and is held for the duration of one
     * dispatched message (serial dispatch, same ordering guarantees as the
     * pre-worker design). write_lock keeps protocol frames from
     * interleaving between the main thread and the diagnostics worker;
     * the worker never holds doc_lock while running the front-end. */
#ifdef _WIN32
    CRITICAL_SECTION doc_lock;
    CRITICAL_SECTION write_lock;
    HANDLE diag_thread;
#else
    pthread_mutex_t doc_lock;
    pthread_mutex_t write_lock;
    pthread_t diag_thread;
    bool diag_thread_valid;
#endif
    volatile bool diag_stop;
    /* ---- request worker: the reader thread only reads frames and
     * enqueues; one worker thread executes them FIFO. Handlers keep their
     * single-threaded assumptions (they run one at a time under doc_lock).
     * The reader stays unblocked while a request executes, which is what
     * makes $/cancelRequest usable: it is intercepted by the reader, since
     * a cancel notice must land while the worker is mid-request. ---- */
#ifdef _WIN32
    CRITICAL_SECTION q_lock;
    HANDLE worker_thread;
#else
    pthread_mutex_t q_lock;
    pthread_t worker_thread;
    bool worker_thread_valid;
#endif
    lsp_msg_t *q_head;          /* FIFO of parsed incoming messages */
    lsp_msg_t *q_tail;
    int q_len;
    /* cancel slot: the most recent $/cancelRequest id (q_lock-guarded) */
    bool cancel_valid;
    bool cancel_is_str;
    double cancel_num;
    char cancel_str[80];
    /* id of the request the worker is executing right now (q_lock) */
    bool exec_valid;
    bool exec_is_str;
    double exec_num;
    char exec_str[80];
} lsp_server_t;

/* ---- TCP transport (--port): same single-client server as zan-dap ---- */

static bool sock_send_all(lsp_sock_t s, const char *buf, int n) {
    int off = 0;
    while (off < n) {
        int r = (int)send(s, buf + off, n - off, 0);
        if (r <= 0) return false;
        off += r;
    }
    return true;
}

/* An LSP session may idle freely BETWEEN frames (the editor can sit on a
 * request for minutes), so the wait for a frame's first byte stays
 * unbounded -- exactly like the stdio transport. But once a frame has
 * started, the peer must finish it: a client that dribbles one byte per
 * recv, or dies mid-frame with a half-open connection, would otherwise
 * park this server forever, and it serves exactly one client -- whoever
 * connects first owns it. The deadline is per frame: armed on the first
 * byte, never extended per chunk, disarmed by rpc_read_message_sock when
 * the frame ends. Even the 64MB cap moves over loopback in seconds, so a
 * minute is far past any legitimate peer. */
#ifndef LSP_FRAME_DEADLINE_MS
#define LSP_FRAME_DEADLINE_MS 60000
#endif

typedef struct {
    lsp_sock_t s;
    bool started;        /* a frame is in flight: the deadline is armed */
    uint64_t deadline;   /* monotonic ms when the frame must be complete */
} lsp_frame_reader_t;

static uint64_t lsp_now_ms(void);   /* defined with the diagnostics code */

static int sock_reader(void *ctx, char *buf, int n) {
    lsp_frame_reader_t *r = (lsp_frame_reader_t *)ctx;
    if (r->started) {
        /* Bounded wait for the next chunk; a blown deadline or a select
         * error reads as EOF, which ends the frame and the session. */
        long long wait = (long long)(r->deadline - lsp_now_ms());
        if (wait <= 0) return 0;
        fd_set fds;
        struct timeval tv;
        FD_ZERO(&fds);
        FD_SET(r->s, &fds);
        tv.tv_sec = (long)(wait / 1000);
        tv.tv_usec = (long)((wait % 1000) * 1000);
        if (select((int)r->s + 1, &fds, NULL, NULL, &tv) <= 0) return 0;
    }
    int got = (int)recv(r->s, buf, n, 0);
    if (got > 0 && !r->started) {
        r->started = true;
        r->deadline = lsp_now_ms() + (uint64_t)LSP_FRAME_DEADLINE_MS;
    }
    return got;
}

static bool sock_writer(void *ctx, const char *buf, int n) {
    return sock_send_all(*(lsp_sock_t *)ctx, buf, n);
}

static char *rpc_read_message_sock(lsp_sock_t s) {
    static lsp_frame_reader_t r;   /* the reader loop is single-threaded */
    r.s = s;
    r.started = false;             /* each frame arms fresh on first byte */
    return rpc_read_message_cb(sock_reader, &r, 64 * 1024 * 1024);
}

static void rpc_write_message_sock(lsp_sock_t s, const char *payload) {
    rpc_write_message_cb(sock_writer, &s, payload);
}

/* All protocol output funnels through here so the socket transport is a
 * drop-in replacement for stdio, and so frames from the main thread and
 * the diagnostics worker can never interleave mid-frame. */
static void lsp_write(lsp_server_t *s, const char *payload) {
#ifdef _WIN32
    EnterCriticalSection(&s->write_lock);
#else
    pthread_mutex_lock(&s->write_lock);
#endif
    if (s->use_sock)
        rpc_write_message_sock(s->sock, payload);
    else
        rpc_write_message(s->out, payload);
#ifdef _WIN32
    LeaveCriticalSection(&s->write_lock);
#else
    pthread_mutex_unlock(&s->write_lock);
#endif
}

/* Listen on 127.0.0.1:port and accept a single client. Returns the connected
 * socket, or LSP_INVALID_SOCK on failure. */
static lsp_sock_t lsp_listen_accept(int port) {
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return LSP_INVALID_SOCK;
#endif
    lsp_sock_t ls = socket(AF_INET, SOCK_STREAM, 0);
    if (ls == LSP_INVALID_SOCK) return LSP_INVALID_SOCK;
    int one = 1;
    setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, (const char *)&one, sizeof(one));
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons((unsigned short)port);
    if (bind(ls, (struct sockaddr *)&addr, sizeof(addr)) != 0 ||
        listen(ls, 1) != 0) {
#ifdef _WIN32
        closesocket(ls);
#else
        close(ls);
#endif
        return LSP_INVALID_SOCK;
    }
    lsp_sock_t cs = accept(ls, NULL, NULL);
#ifdef _WIN32
    closesocket(ls);
#else
    close(ls);
#endif
    return cs;
}

static char *dup_str(const char *s) {
    if (!s) s = "";
    size_t n = strlen(s);
    char *d = (char *)malloc(n + 1);
    if (d) memcpy(d, s, n + 1);
    return d;
}

static uint64_t lsp_now_ms(void) {
#ifdef _WIN32
    return (uint64_t)GetTickCount64();
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ull + (uint64_t)ts.tv_nsec / 1000000ull;
#endif
}

static void lsp_locks_init(lsp_server_t *s) {
#ifdef _WIN32
    InitializeCriticalSection(&s->doc_lock);
    InitializeCriticalSection(&s->write_lock);
    InitializeCriticalSection(&s->q_lock);
#else
    pthread_mutex_init(&s->doc_lock, NULL);
    pthread_mutex_init(&s->write_lock, NULL);
    pthread_mutex_init(&s->q_lock, NULL);
#endif
}

static void lsp_locks_free(lsp_server_t *s) {
#ifdef _WIN32
    DeleteCriticalSection(&s->doc_lock);
    DeleteCriticalSection(&s->write_lock);
    DeleteCriticalSection(&s->q_lock);
#else
    pthread_mutex_destroy(&s->doc_lock);
    pthread_mutex_destroy(&s->write_lock);
    pthread_mutex_destroy(&s->q_lock);
#endif
}

static void lsp_doc_lock(lsp_server_t *s) {
#ifdef _WIN32
    EnterCriticalSection(&s->doc_lock);
#else
    pthread_mutex_lock(&s->doc_lock);
#endif
}

static void lsp_doc_unlock(lsp_server_t *s) {
#ifdef _WIN32
    LeaveCriticalSection(&s->doc_lock);
#else
    pthread_mutex_unlock(&s->doc_lock);
#endif
}

static void lsp_sleep_ms(unsigned ms) {
#ifdef _WIN32
    Sleep(ms);
#else
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
#endif
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

/* UTF-8 sequence starting at byte c: its byte length and how many UTF-16 code
 * units the LSP spec assigns it (1 for BMP code points, 2 for a surrogate
 * pair, i.e. 4-byte UTF-8). Invalid / standalone bytes are treated as 1 unit. */
static void utf8_seq_info(unsigned char c, int *bytes, int *utf16_units) {
    if (c < 0x80)                 { *bytes = 1; *utf16_units = 1; }
    else if ((c & 0xE0) == 0xC0)  { *bytes = 2; *utf16_units = 1; }
    else if ((c & 0xF0) == 0xE0)  { *bytes = 3; *utf16_units = 1; }
    else if ((c & 0xF8) == 0xF0)  { *bytes = 4; *utf16_units = 2; }
    else                          { *bytes = 1; *utf16_units = 1; }
}

/* UTF-16 code units in the half-open byte range [start, end). */
static int utf16_units_range(const char *start, const char *end) {
    int units = 0;
    while (start < end) {
        int bytes, cu;
        utf8_seq_info((unsigned char)*start, &bytes, &cu);
        if (start + bytes > end) break; /* range cuts a sequence: stop */
        units += cu;
        start += bytes;
    }
    return units;
}

/* Convert the compiler's 1-based byte column on the given 0-based line into a
 * 0-based UTF-16 character position (the lexer counts bytes, so a diagnostic
 * after non-ASCII text would otherwise be emitted with a wrong column). */
static int byte_col_to_utf16_char(const char *text, int line0, int byte_col1) {
    const char *ls = text;
    int ln = 0;
    while (ln < line0 && *ls) {
        if (*ls == '\n') ln++;
        ls++;
    }
    if (byte_col1 <= 1) return 0;
    /* walk the line for byte_col1-1 bytes (clamped to the line end / a split
     * multi-byte sequence), then count the UTF-16 units of that prefix */
    const char *line_start = ls;
    int want = byte_col1 - 1;
    int have = 0;
    while (*ls && *ls != '\n' && have < want) {
        int bytes, cu;
        utf8_seq_info((unsigned char)*ls, &bytes, &cu);
        if (have + bytes > want) break;
        have += bytes;
        ls += bytes;
    }
    return utf16_units_range(line_start, ls);
}

/* Convert an LSP (line, character) position (both 0-based; character in UTF-16
 * code units per the LSP spec) to a byte offset into `text`. */
static size_t pos_to_offset(const char *text, int line, int character) {
    size_t off = 0;
    int cur_line = 0;
    while (text[off] && cur_line < line) {
        if (text[off] == '\n') cur_line++;
        off++;
    }
    /* advance `character` UTF-16 units along this line; a character that falls
     * inside a surrogate pair clamps to the code point's start */
    int units = 0;
    while (text[off] && text[off] != '\n' && units < character) {
        int bytes, cu;
        utf8_seq_info((unsigned char)text[off], &bytes, &cu);
        if (units + cu > character) break;
        units += cu;
        off += (size_t)bytes;
    }
    return off;
}

/* Start of the given 0-based line (walks to the line's first byte). */
static const char *line_start_at(const char *text, int line0) {
    const char *ls = text;
    int ln = 0;
    while (ln < line0 && *ls) {
        if (*ls == '\n') ln++;
        ls++;
    }
    return ls;
}

static bool is_ident_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_';
}

/* Length in bytes of the identifier starting at `s` (0 when none). */
static int ident_len_at(const char *s) {
    int n = 0;
    while (is_ident_char(s[n])) n++;
    return n;
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

/* Convert a global byte offset into a (line, character) pair (both 0-based,
 * character in UTF-16 code units).
 * The intellisense engine reports symbol locations as a byte offset in its
 * `col` field, so we derive accurate LSP positions from the document text. */
static void offset_to_linecol(const char *text, int offset, int *line, int *character) {
    int ln = 0, col = 0, i = 0;
    while (i < offset && text[i]) {
        unsigned char c = (unsigned char)text[i];
        if (c == '\n') { ln++; col = 0; i++; continue; }
        int bytes, cu;
        utf8_seq_info(c, &bytes, &cu);
        if (i + bytes > offset) {
            /* offset cuts inside a multi-byte sequence: report the character
             * containing the offset (its full unit weight). */
            col += cu;
            break;
        }
        col += cu;
        i += bytes;
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
    int bracket_depth = 0;

    while (start > 0) {
        char ch = text[start - 1];
        if (ch == ')') {
            paren_depth++;
            start--;
        } else if (ch == '(') {
            if (paren_depth > 0) { paren_depth--; start--; }
            else break;
        } else if (ch == ']') {
            /* `items[Index()]` / `map["a"][0]`: subscripts belong to the
             * receiver chain like calls do */
            bracket_depth++;
            start--;
        } else if (ch == '[') {
            if (bracket_depth > 0) { bracket_depth--; start--; }
            else break;
        } else if (ch == '>') {
            angle_depth++;
            start--;
        } else if (ch == '<') {
            if (angle_depth > 0) { angle_depth--; start--; }
            else break;
        } else if (ch == '.' && paren_depth == 0 && angle_depth == 0 &&
                   bracket_depth == 0) {
            start--;
        } else if (ch == '"' || ch == '\'') {
            /* A string/char literal chain root: consume back to the opening
             * quote so `"abc".ToUpper()` arrives whole at the resolver. */
            char q = ch;
            start--;
            while (start > 0 && text[start - 1] != q) start--;
            if (start > 0) start--;
        } else if ((is_ident_char(ch) || ch == '_') && paren_depth == 0 && angle_depth == 0) {
            start--;
        } else if (paren_depth > 0 || angle_depth > 0 || bracket_depth > 0) {
            /* Inside parens/angles/brackets, accept anything */
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
/* Detect a `using` directive context at the cursor: the line holding
 * `off` either opens with the keyword (cursor before or inside it) or
 * the cursor sits after "using" within the namespace word being typed.
 * Outputs the namespace prefix typed so far ("" = none yet). */
static bool using_ns_context(const char *text, size_t off,
                             char *out, size_t cap) {
    size_t ls = off;
    while (ls > 0 && text[ls - 1] != '\n') ls--;
    size_t le = off;
    while (text[le] != '\0' && text[le] != '\n') le++;

    size_t hlen = off - ls, tlen = le - off;
    const char *head = text + ls, *tail = text + off;

    size_t i = 0;
    while (i < hlen && (head[i] == ' ' || head[i] == '\t' || head[i] == '\r')) i++;

    if (i == hlen) {
        /* Cursor before the keyword: the line must open with "using". */
        size_t j = 0;
        while (j < tlen && (tail[j] == ' ' || tail[j] == '\t')) j++;
        if (tlen - j >= 5 && memcmp(tail + j, "using", 5) == 0 &&
            (tlen - j == 5 || tail[j + 5] == ' ' || tail[j + 5] == '\t')) {
            out[0] = '\0';
            return true;
        }
        return false;
    }

    size_t rlen = hlen - i;
    const char *rest = head + i;
    size_t m = 0;
    while (m < rlen && m < 5 &&
           tolower((unsigned char)rest[m]) == "using"[m]) m++;
    if (rlen < 5 || m < 5) return false;   /* partial word or not "using" */

    size_t p = 5;
    while (p < rlen && (rest[p] == ' ' || rest[p] == '\t')) p++;
    if (p == rlen) { out[0] = '\0'; return true; }

    size_t w0 = p, w1 = p;
    while (w1 < rlen && (isalnum((unsigned char)rest[w1]) ||
                         rest[w1] == '.' || rest[w1] == '_')) w1++;
    if (w1 != rlen) return false;          /* trailing junk on the line */
    size_t n = w1 - w0 < cap - 1 ? w1 - w0 : cap - 1;
    memcpy(out, rest + w0, n);
    out[n] = '\0';
    return true;
}

/* The dotted receiver expression immediately before a member word: for
 * `MainStatusBar.SetText` with the cursor inside SetText this yields
 * "MainStatusBar". `offset` is the cursor offset; the word start is found
 * with the same backward scan word_at uses (the cursor may sit mid-word).
 * The scan stops at any character a simple receiver chain cannot contain
 * (whitespace, parens, operators); returns false when the character before
 * the word is not a dot. */
static bool receiver_chain_before(const char *text, size_t offset,
                                  char *out, size_t cap) {
    size_t start = offset;
    while (start > 0 && is_ident_char(text[start - 1])) start--;
    if (start == 0 || text[start - 1] != '.') return false;
    size_t end = start - 1; /* at the dot */
    size_t from = end;
    for (;;) {
        while (from > 0) {
            char c = text[from - 1];
            if (isalnum((unsigned char)c) || c == '_' || c == '.' ||
                c == '<' || c == '>' || c == '?') from--;
            else break;
        }
        if (from > 0 && (text[from - 1] == '"' || text[from - 1] == '\'')) {
            /* string/char literal in the chain: consume back to the opening
             * quote and keep collecting (`"abc".ToUpper` -> `"abc".ToUpper`) */
            char q = text[from - 1];
            from--;
            while (from > 0 && text[from - 1] != q) from--;
            if (from > 0) from--;
            continue;
        }
        break;
    }
    size_t len = end - from;
    if (len == 0 || len >= cap) return false;
    memcpy(out, text + from, len);
    out[len] = '\0';
    return true;
}

static void member_context(const char *text, size_t offset, char *out, size_t cap) {
    out[0] = '\0';
    size_t start = offset;
    while (start > 0 && is_ident_char(text[start - 1])) start--;
    if (start == 0 || text[start - 1] != '.') return;

    /* Extract full chain expression */
    char chain_expr[1024];
    extract_chain_expr(text, offset, chain_expr, sizeof(chain_expr));

    /* Literal roots ("text", 'c', 123) cannot be expressed as a bare
     * identifier: hand the whole chain to the resolver, whose literal-root
     * branches type them (string/int). */
    if (chain_expr[0] == '"' || chain_expr[0] == '\'' ||
        (chain_expr[0] >= '0' && chain_expr[0] <= '9')) {
        if (strlen(chain_expr) + 7 < cap) {
            snprintf(out, cap, "CHAIN:%s", chain_expr);
            return;
        }
    }

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
    if (n == 0 && chain_expr[0]) {
        /* Nothing identifier-shaped before the dot (a call chain or other
         * expression): the resolver handles the whole chain. */
        if (strlen(chain_expr) + 7 < cap) {
            snprintf(out, cap, "CHAIN:%s", chain_expr);
            return;
        }
    }
    if (n >= cap) n = cap - 1;
    memcpy(out, text + obj_start, n);
    out[n] = '\0';
}

/* Find the method name for signature help: scan backward from offset to find
 * the method call context (the word before the nearest unclosed parenthesis). */
static void method_call_context(const char *text, size_t offset,
                                char *method_out, size_t method_cap,
                                char *class_out, size_t class_cap,
                                int *active_param,
                                char *recv_chain_out, size_t recv_chain_cap) {
    method_out[0] = '\0';
    class_out[0] = '\0';
    if (recv_chain_out && recv_chain_cap > 0) recv_chain_out[0] = '\0';
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
        /* the full receiver chain behind the dot (literals and multi-hop
         * chains included) for receiver-typed signature resolution */
        if (recv_chain_out && recv_chain_cap > 0) {
            recv_chain_out[0] = '\0';
            receiver_chain_before(text, end, recv_chain_out, recv_chain_cap);
        }
        size_t dot_pos = end - 1;
        size_t cls_end = dot_pos;
        size_t cls_start = cls_end;
        while (cls_start > 0 && is_ident_char(text[cls_start - 1])) cls_start--;
        size_t cn = cls_end - cls_start;
        if (cn >= class_cap) cn = class_cap - 1;
        memcpy(class_out, text + cls_start, cn);
        class_out[cn] = '\0';
    } else if (recv_chain_out && recv_chain_cap > 0) {
        recv_chain_out[0] = '\0';
    }
}

/* ============================ diagnostics ============================ */

/* Designer documents (.zscene JSON, .html/.htm P7d) are never Zan
 * source: GenForm/GenScene project them at compile time and the designers
 * validate them live. Publish an empty list so any previously shown errors
 * clear on close, and so the zanc front-end never runs over them. */
static void publish_empty_diagnostics(lsp_server_t *s, const char *uri) {
    json_value *params = json_new_obj();
    json_obj_set(params, "uri", json_new_str(uri));
    json_obj_set(params, "diagnostics", json_new_arr());
    json_value *note = json_new_obj();
    json_obj_set(note, "jsonrpc", json_new_str("2.0"));
    json_obj_set(note, "method", json_new_str("textDocument/publishDiagnostics"));
    json_obj_set(note, "params", params);
    char *payload = json_serialize(note);
    lsp_write(s, payload);
    free(payload);
    json_free(note);
}

/* Run the front-end over `text` and publish diagnostics for `uri`. */
static void publish_diagnostics(lsp_server_t *s, const char *uri, const char *text) {
    size_t ul = strlen(uri);
    if ((ul > 7 && strcmp(uri + ul - 7, ".zscene") == 0) ||
        (ul > 5 && strcmp(uri + ul - 5, ".html") == 0) ||
        (ul > 4 && strcmp(uri + ul - 4, ".htm") == 0)) {
        publish_empty_diagnostics(s, uri);
        return;
    }

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
        /* e->loc.col is the lexer's 1-based byte column; LSP wants UTF-16
         * units, so convert (a diagnostic on a line with non-ASCII text before
         * it would otherwise land on the wrong character). */
        int col  = byte_col_to_utf16_char(text, line, (int)e->loc.col);
        /* Underline the whole token when the diagnostic lands on an
         * identifier, not just its first character. */
        int end_char = col + 1;
        {
            const char *ls = line_start_at(text, line);
            const char *le = strchr(ls, '\n');
            if (!le) le = ls + strlen(ls);
            int skip = (int)e->loc.col - 1;
            if (skip < 0) skip = 0;
            if (ls + skip > le) skip = (int)(le - ls);
            const char *tok = ls + skip;
            int tlen = ident_len_at(tok);
            if (tok + tlen > le) tlen = (int)(le - tok);
            if (tlen > 0)
                end_char = utf16_units_range(ls, tok + tlen);
        }

        json_value *d = json_new_obj();
        json_value *range = json_new_obj();
        json_value *start = json_new_obj();
        json_value *endp  = json_new_obj();
        json_obj_set(start, "line", json_new_num(line));
        json_obj_set(start, "character", json_new_num(col));
        json_obj_set(endp, "line", json_new_num(line));
        json_obj_set(endp, "character", json_new_num(end_char));
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
    lsp_write(s, payload);
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
    lsp_write(s, payload);
    free(payload);
    json_free(resp);
}

/* JSON-RPC error response (used for $/cancelRequest: -32800 RequestCancelled,
 * and for requests dropped when the queue is saturated). */
static void send_response_error(lsp_server_t *s, json_value *id, int code,
                                const char *message) {
    json_value *resp = json_new_obj();
    json_obj_set(resp, "jsonrpc", json_new_str("2.0"));
    if (id && id->type == JSON_NUM)
        json_obj_set(resp, "id", json_new_num(id->as.num));
    else if (id && id->type == JSON_STR)
        json_obj_set(resp, "id", json_new_str(id->as.str));
    else
        json_obj_set(resp, "id", json_new_null());
    json_value *err = json_new_obj();
    json_obj_set(err, "code", json_new_num(code));
    json_obj_set(err, "message", json_new_str(message));
    json_obj_set(resp, "error", err);

    char *payload = json_serialize(resp);
    lsp_write(s, payload);
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

static void ensure_project_indexed(lsp_server_t *s);

/* Percent-decodes `in` into `out` (NUL-terminated, `cap`-bounded). Each %XX
 * hex pair becomes its byte; a '%' not followed by two hex digits is kept
 * literally, so paths that arrive already decoded pass through unchanged. */
static void uri_percent_decode(const char *in, char *out, size_t cap) {
    size_t o = 0;
    for (size_t i = 0; in[i] && o + 1 < cap; i++) {
        if (in[i] == '%' && isxdigit((unsigned char)in[i + 1])
            && isxdigit((unsigned char)in[i + 2])) {
            char hex[3] = { in[i + 1], in[i + 2], '\0' };
            out[o++] = (char)strtol(hex, NULL, 16);
            i += 2;
        } else {
            out[o++] = in[i];
        }
    }
    out[o] = '\0';
}

/* Percent-encodes a native path into URI path form: unreserved bytes plus
 * '/' and the drive-letter ':' stay raw, everything else (space, '%',
 * '"#&<>[\]^`{|}', control and every non-ASCII byte) becomes %XX with
 * uppercase hex, matching what standard clients emit for these paths. */
static void uri_percent_encode_path(const char *in, char *out, size_t cap) {
    static const char hex[] = "0123456789ABCDEF";
    size_t o = 0;
    for (size_t i = 0; in[i] && o + 3 < cap; i++) {
        unsigned char c = (unsigned char)in[i];
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')
            || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.'
            || c == '~' || c == '/' || c == ':') {
            out[o++] = (char)c;
        } else {
            out[o++] = '%';
            out[o++] = hex[c >> 4];
            out[o++] = hex[c & 15];
        }
    }
    out[o] = '\0';
}

/* Native filesystem path for a file:  // URI (percent-decoded, separators
 * native), so a document update can replace the project index's entry for
 * the same file instead of adding a duplicate keyed by URI. */
static void uri_to_native_path(const char *uri, char *out, size_t cap) {
    const char *rest = NULL;
    if (strncmp(uri, "file:///", 8) == 0) rest = uri + 8;   /* file:///C:/... */
    else if (strncmp(uri, "file://", 7) == 0) rest = uri + 7; /* file://host/... */
    if (rest) {
        char decoded[1200];
        uri_percent_decode(rest, decoded, sizeof(decoded));
        strncpy(out, decoded, cap - 1);
        out[cap - 1] = '\0';
#ifdef _WIN32
        for (char *p = out; *p; p++) {
            if (*p == '/') *p = '\\';
        }
#endif
    } else {
        strncpy(out, uri, cap - 1);
        out[cap - 1] = '\0';
    }
}

static void handle_initialize(lsp_server_t *s, json_value *id, json_value *params) {
    /* Extract workspace root for project indexing */
    if (params) {
        const char *root_uri = json_get_str(json_obj_get(params, "rootUri"));
        const char *root_path = json_get_str(json_obj_get(params, "rootPath"));
        if (root_uri && strncmp(root_uri, "file://", 7) == 0) {
            uri_to_native_path(root_uri, s->workspace_root, sizeof(s->workspace_root));
        } else if (root_path) {
            strncpy(s->workspace_root, root_path, sizeof(s->workspace_root) - 1);
        }
    }

    ensure_project_indexed(s);

    json_value *caps = json_new_obj();
    /* Incremental document sync: clients may send range-based edits.
     * Full-text changes (range absent) are still accepted, so full-sync
     * clients keep working unchanged. */
    json_value *sync = json_new_obj();
    json_obj_set(sync, "openClose", json_new_bool(true));
    json_obj_set(sync, "change", json_new_num(2)); /* incremental */
    json_obj_set(caps, "textDocumentSync", sync);

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
    json_obj_set(caps, "workspaceSymbolProvider", json_new_bool(true));
    /* rename goes through prepareRename first, so clients can validate the
     * target word before sending the edit */
    json_value *rename = json_new_obj();
    json_obj_set(rename, "prepareProvider", json_new_bool(true));
    json_obj_set(caps, "renameProvider", rename);
    json_obj_set(caps, "documentHighlightProvider", json_new_bool(true));
    json_obj_set(caps, "foldingRangeProvider", json_new_bool(true));
    json_obj_set(caps, "documentFormattingProvider", json_new_bool(true));
    json_obj_set(caps, "documentRangeFormattingProvider", json_new_bool(true));

    /* Code actions (organize usings, etc.) */
    json_value *code_action = json_new_obj();
    json_value *ca_kinds = json_new_arr();
    json_arr_add(ca_kinds, json_new_str("source.organizeImports"));
    json_obj_set(code_action, "codeActionKinds", ca_kinds);
    json_obj_set(caps, "codeActionProvider", code_action);

    /* Inlay hints (inferred types for var, parameter names at call sites) */
    json_value *inlay_hint = json_new_obj();
    json_obj_set(inlay_hint, "resolveProvider", json_new_bool(false));
    json_obj_set(caps, "inlayHintProvider", inlay_hint);

    /* Semantic tokens provider */
    {
        json_value *sem_tokens = json_new_obj();
        json_value *legend = json_new_obj();
        json_value *token_types = json_new_arr();
        /* Legend matches semantic token types:
         * 0: namespace, 1: type, 2: class, 3: enum, 4: interface, 5: struct,
         * 6: parameter, 7: variable, 8: property, 9: function, 10: method,
         * 11: keyword, 12: string, 13: number, 14: operator */
        json_arr_add(token_types, json_new_str("namespace"));
        json_arr_add(token_types, json_new_str("type"));
        json_arr_add(token_types, json_new_str("class"));
        json_arr_add(token_types, json_new_str("enum"));
        json_arr_add(token_types, json_new_str("interface"));
        json_arr_add(token_types, json_new_str("struct"));
        json_arr_add(token_types, json_new_str("parameter"));
        json_arr_add(token_types, json_new_str("variable"));
        json_arr_add(token_types, json_new_str("property"));
        json_arr_add(token_types, json_new_str("function"));
        json_arr_add(token_types, json_new_str("method"));
        json_arr_add(token_types, json_new_str("keyword"));
        json_arr_add(token_types, json_new_str("string"));
        json_arr_add(token_types, json_new_str("number"));
        json_arr_add(token_types, json_new_str("operator"));
        json_obj_set(legend, "tokenTypes", token_types);
        json_obj_set(legend, "tokenModifiers", json_new_arr());
        json_obj_set(sem_tokens, "legend", legend);
        json_obj_set(sem_tokens, "full", json_new_bool(true));
        json_obj_set(caps, "semanticTokensProvider", sem_tokens);
    }

    /* Execute-command: memory leak check */
    json_value *exec_cmd = json_new_obj();
    json_value *cmd_list = json_new_arr();
    json_arr_add(cmd_list, json_new_str("zan.checkLeaks"));
    json_obj_set(exec_cmd, "commands", cmd_list);
    json_obj_set(caps, "executeCommandProvider", exec_cmd);

    json_value *result = json_new_obj();
    json_obj_set(result, "capabilities", caps);

    json_value *info = json_new_obj();
    json_obj_set(info, "name", json_new_str("zan-lsp"));
    json_obj_set(info, "version", json_new_str(ZAN_VERSION));
    json_obj_set(result, "serverInfo", info);

    send_response(s, id, result);
}

/* Project-wide intellisense instance for cross-file completion */
static intellisense_t *g_project_intel = NULL;

/* Toolchain stdlib root: user project roots rarely contain the stdlib, so
 * goto-definition and member completion on stdlib members (List.Add,
 * app.RequestRedraw, File.WriteAllText, ...) used to miss entirely. Locate
 * it the same exe-relative way zanc's --auto-stdlib does; ZAN_STDLIB
 * overrides for non-standard layouts. */
static bool lsp_stdlib_root(char *out, size_t cap) {
    const char *env = getenv("ZAN_STDLIB");
    if (env && env[0]) {
        snprintf(out, cap, "%s", env);
        return true;
    }
#ifdef _WIN32
    char exe_path[1024];
    if (GetModuleFileNameA(NULL, exe_path, (DWORD)sizeof(exe_path)) == 0) {
        return false;
    }
    char *last_sep = strrchr(exe_path, '\\');
    if (!last_sep) { return false; }
    *last_sep = '\0';
    snprintf(out, cap, "%s\\..\\stdlib", exe_path);
    DWORD attr = GetFileAttributesA(out);
    if (attr == INVALID_FILE_ATTRIBUTES || !(attr & FILE_ATTRIBUTE_DIRECTORY)) {
        snprintf(out, cap, "%s\\stdlib", exe_path);
        attr = GetFileAttributesA(out);
        if (attr == INVALID_FILE_ATTRIBUTES || !(attr & FILE_ATTRIBUTE_DIRECTORY)) {
            return false;
        }
    }
    return true;
#elif defined(__APPLE__)
    char exe_path[1024];
    uint32_t size = sizeof(exe_path);
    if (_NSGetExecutablePath(exe_path, &size) != 0) { return false; }
    char *last_sep = strrchr(exe_path, '/');
    if (!last_sep) { return false; }
    *last_sep = '\0';
    snprintf(out, cap, "%s/../stdlib", exe_path);
    struct stat st;
    if (stat(out, &st) == 0 && S_ISDIR(st.st_mode)) { return true; }
    snprintf(out, cap, "%s/stdlib", exe_path);
    return stat(out, &st) == 0 && S_ISDIR(st.st_mode);
#else
    char exe_path[1024];
    ssize_t elen = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (elen <= 0) { return false; }
    exe_path[elen] = '\0';
    char *last_sep = strrchr(exe_path, '/');
    if (!last_sep) { return false; }
    *last_sep = '\0';
    snprintf(out, cap, "%s/../stdlib", exe_path);
    struct stat st;
    if (stat(out, &st) == 0 && S_ISDIR(st.st_mode)) { return true; }
    snprintf(out, cap, "%s/stdlib", exe_path);
    return stat(out, &st) == 0 && S_ISDIR(st.st_mode);
#endif
}

/* Parse the toolchain stdlib into the shared project index, once per
 * session, right after the workspace scan. Reuses the same recursive
 * walker, so skip-lists and file-size caps apply unchanged.
 * Packages (Zan.Gui, Zan.Data, ...) are indexed here too, because they
 * ship Zan source just like the stdlib and their types must complete in
 * user code. The workspace root is the anchor: `zan_pkg_all_source_roots`
 * uses the same search order as zanc (project packages/ first, then the
 * toolchain-relative packages/, then the global store). */
static void ensure_stdlib_indexed(lsp_server_t *s) {
    static bool stdlib_indexed = false;
    if (stdlib_indexed) { return; }
    stdlib_indexed = true;
    if (!g_project_intel) { return; }
    char root[1024];
    if (lsp_stdlib_root(root, sizeof(root))) {
        intel_index_project(g_project_intel, root);
    }

    /* Index every package source root the compiler would see. */
    const char *project_dir = s->workspace_root[0] ? s->workspace_root : ".";
    char pkg_roots[32][1024];
    int pkg_n = zan_pkg_all_source_roots(project_dir, pkg_roots, 32);
    for (int i = 0; i < pkg_n; i++) {
        if (!intel_cancel_flag)
            intel_index_project(g_project_intel, pkg_roots[i]);
    }
}

static void ensure_project_indexed(lsp_server_t *s) {
    if (s->project_indexed) return;
    s->project_indexed = true;

    if (!g_project_intel) {
        g_project_intel = (intellisense_t *)malloc(sizeof(intellisense_t));
        if (!g_project_intel) return;
        intel_init(g_project_intel);
    }
    /* No workspace root: still keep the aggregate index alive — every
     * open/change feeds it (update_project_index), so cross-file member
     * chains and statics between open documents keep working (tests, and
     * clients that open files without a root). Only the disk scan is
     * workspace-bound. */
    if (s->workspace_root[0]) {
        intel_index_project(g_project_intel, s->workspace_root);
        if (intel_cancel_flag) {
            /* Aborted mid-scan by $/cancelRequest: retry from scratch on the
             * next request rather than trusting a partial index. */
            s->project_indexed = false;
            return;
        }
    }
    ensure_stdlib_indexed(s);
}

/* Re-index one changed document into the shared project index so completion
 * from OTHER files (e.g. a form's code-behind after the designer edited the
 * design doc) sees the change immediately, without a full project rescan. */
static void update_project_index(lsp_server_t *s, const char *uri,
                                 const char *text) {
    if (!g_project_intel || !uri || !text) return;
    char path[1024];
    uri_to_native_path(uri, path, sizeof(path));
    intel_parse_file(g_project_intel, path, text, strlen(text));
    (void)s;
}

static void handle_did_open(lsp_server_t *s, json_value *params) {
    json_value *td = json_obj_get(params, "textDocument");
    const char *uri = json_get_str(json_obj_get(td, "uri"));
    const char *text = json_get_str(json_obj_get(td, "text"));
    if (!uri || !text) return;
    lsp_set_doc(s, uri, text);

    /* the open version anchors didChange staleness checks */
    json_value *ver_j = json_obj_get(td, "version");
    int over = ver_j ? (int)json_get_num(ver_j, 0) : 0;
    lsp_doc_t *d = lsp_find_doc(s, uri);
    if (d && over > 0) d->version = over;

    /* Index the project on first file open */
    ensure_project_indexed(s);
    update_project_index(s, uri, text);

    publish_diagnostics(s, uri, text);
}

/* Apply one contentChanges entry to a document: range-spliced for
 * incremental sync, whole-document replace when no range is present
 * (full-sync clients). */
static void lsp_doc_apply_change(lsp_doc_t *d, json_value *change) {
    const char *text = json_get_str(json_obj_get(change, "text"));
    if (!text) return;
    json_value *range = json_obj_get(change, "range");
    json_value *start = range ? json_obj_get(range, "start") : NULL;
    json_value *end   = range ? json_obj_get(range, "end") : NULL;
    if (!start || !end) {
        free(d->text);
        d->text = dup_str(text);
        return;
    }
    size_t soff = pos_to_offset(d->text,
                                (int)json_get_num(json_obj_get(start, "line"), 0),
                                (int)json_get_num(json_obj_get(start, "character"), 0));
    size_t eoff = pos_to_offset(d->text,
                                (int)json_get_num(json_obj_get(end, "line"), 0),
                                (int)json_get_num(json_obj_get(end, "character"), 0));
    size_t dlen = strlen(d->text);
    if (eoff > dlen) eoff = dlen;
    if (eoff < soff) eoff = soff;
    size_t tlen = strlen(text);
    char *nt = (char *)malloc(dlen - (eoff - soff) + tlen + 1);
    if (!nt) {
        free(d->text);
        d->text = dup_str(text);
        return;
    }
    memcpy(nt, d->text, soff);
    memcpy(nt + soff, text, tlen);
    memcpy(nt + soff + tlen, d->text + eoff, dlen - eoff);
    nt[dlen - (eoff - soff) + tlen] = '\0';
    free(d->text);
    d->text = nt;
}

static void handle_did_change(lsp_server_t *s, json_value *params) {
    json_value *td = json_obj_get(params, "textDocument");
    const char *uri = json_get_str(json_obj_get(td, "uri"));
    json_value *changes = json_obj_get(params, "contentChanges");
    if (!uri || !changes) return;
    int n = json_arr_count(changes);
    if (n <= 0) return;

    /* Versions are monotonic per document: a change whose version is not
     * newer than the last applied one is the client racing itself (a retried
     * change, or an undo that re-sent an older edit). Applying it would roll
     * the document text back, so drop it. */
    json_value *ver_j = json_obj_get(td, "version");
    int incoming = ver_j ? (int)json_get_num(ver_j, 0) : 0;

    lsp_doc_t *d = lsp_find_doc(s, uri);
    if (!d) {
        /* Edit for a document we never saw opened: recover from a
         * full-text change if the client sent one. */
        for (int i = 0; i < n && !d; i++) {
            json_value *ch = json_arr_at(changes, i);
            if (!json_obj_get(ch, "range")) {
                const char *text = json_get_str(json_obj_get(ch, "text"));
                if (text) {
                    lsp_set_doc(s, uri, text);
                    d = lsp_find_doc(s, uri);
                }
            }
        }
        if (!d) return;
    } else {
        if (incoming > 0 && incoming <= d->version) return;
        /* LSP: changes apply in order, each to the result of the previous */
        for (int i = 0; i < n; i++)
            lsp_doc_apply_change(d, json_arr_at(changes, i));
    }

    d->version = incoming > 0 ? incoming : d->version + 1;
    d->last_change_ms = lsp_now_ms();
    d->diag_pending = true;

    /* Cheap heuristic re-index stays on the request thread so cross-file
     * completions see the edit immediately; the expensive front-end run
     * (lex/parse/bind/check) is the diagnostics worker's job. */
    update_project_index(s, uri, d->text);
}

/* Per-document engine cache. Handlers used to malloc a ~2 MB intellisense_t
 * and re-scan the whole document on EVERY completion/hover/definition/
 * signature/symbols request. One slot serves the active document; the key is
 * (uri, doc version) — didChange bumps the version, didClose invalidates.
 * intel_clear keeps the grown symbol arrays, so successive rebuilds of the
 * same document reuse their capacity instead of re-fattening the heap. */
static intellisense_t *g_doc_intel = NULL;
static char g_doc_intel_uri[512] = "";
static long g_doc_intel_version = -1;

/* Returns the cached engine for `uri`'s current text, rebuilding it when the
 * uri or version differs from what is cached. The returned pointer is owned
 * by the cache — handlers must not free it. */
static intellisense_t *doc_intel_for(lsp_server_t *s, const char *uri) {
    lsp_doc_t *doc = lsp_find_doc(s, uri);
    if (!doc) return NULL;
    if (g_doc_intel && strcmp(g_doc_intel_uri, uri) == 0 &&
        g_doc_intel_version == doc->version)
        return g_doc_intel;
    if (!g_doc_intel) {
        g_doc_intel = (intellisense_t *)malloc(sizeof(*g_doc_intel));
        if (!g_doc_intel) return NULL;
        intel_init(g_doc_intel);
    }
    intel_clear(g_doc_intel);
    intel_parse_file(g_doc_intel, uri, doc->text, strlen(doc->text));
    snprintf(g_doc_intel_uri, sizeof(g_doc_intel_uri), "%s", uri);
    g_doc_intel_version = doc->version;
    return g_doc_intel;
}

static void doc_intel_invalidate(const char *uri) {
    if (uri && strcmp(g_doc_intel_uri, uri) == 0) {
        g_doc_intel_uri[0] = '\0';
        g_doc_intel_version = -1;
    }
}

static void handle_did_close(lsp_server_t *s, json_value *params) {
    json_value *td = json_obj_get(params, "textDocument");
    const char *uri = json_get_str(json_obj_get(td, "uri"));
    if (!uri) return;
    lsp_remove_doc(s, uri);
    doc_intel_invalidate(uri);
    /* clear diagnostics */
    publish_diagnostics(s, uri, "");
}

/* ===================== diagnostics worker thread ===================== */

/* Runs the compiler front-end (lex/parse/bind/check) for documents whose
 * edits have gone quiet, so a keystroke on a large file costs the request
 * thread only a splice + heuristic re-index — never the 30-60ms front-end
 * pass. The worker snapshots text under doc_lock and runs the front-end
 * with no lock held; publishes serialize through lsp_write's write_lock. */
static int diag_worker_loop(lsp_server_t *s) {
    while (!s->diag_stop) {
        lsp_sleep_ms(20);
        char *uri = NULL, *text = NULL;
        lsp_doc_lock(s);
        uint64_t now = lsp_now_ms();
        for (int i = 0; i < s->doc_count; i++) {
            lsp_doc_t *d = &s->docs[i];
            if (d->diag_pending && now - d->last_change_ms >= LSP_DIAG_QUIET_MS) {
                uri = dup_str(d->uri);
                text = dup_str(d->text);
                d->diag_pending = false;
                break;
            }
        }
        lsp_doc_unlock(s);
        if (!uri) continue;
        publish_diagnostics(s, uri, text);
        free(uri);
        free(text);
    }
    return 0;
}

#ifdef _WIN32
static DWORD WINAPI diag_worker_main(LPVOID arg) {
    return (DWORD)diag_worker_loop((lsp_server_t *)arg);
}
#else
static void *diag_worker_main(void *arg) {
    return (void *)(intptr_t)diag_worker_loop((lsp_server_t *)arg);
}
#endif

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

/* Member completion for `x.` where x is a `var` local whose initializer the
 * doc engine could not type at parse time because the provider class lives
 * in another file (`var product = ProjectFactory.Fetch();`). The initializer
 * is re-resolved against the aggregate index at request time, so a provider
 * edit under any URI alias retypes the receiver immediately. */
static int complete_var_init_members(const lsp_doc_t *doc, intellisense_t *is,
                                     const char *name, int line, int character,
                                     const char *prefix) {
    if (!g_project_intel || !doc || !is || !name[0]) return 0;
    const isym_t *sym = intel_lookup_symbol_at(is, name, line, character);
    if (!sym || sym->kind != ISYM_VARIABLE) return 0;
    if (sym->type_name[0] && strcmp(sym->type_name, "var") != 0) return 0;

    size_t i = pos_to_offset(doc->text, sym->line, sym->col);
    while (doc->text[i] && doc->text[i] != '=') i++;
    if (!doc->text[i]) return 0;
    i++;
    while (doc->text[i] == ' ' || doc->text[i] == '\t') i++;
    size_t e = i, pd = 0, bd = 0, ad = 0;
    char q = 0;
    while (doc->text[e]) {
        char ch = doc->text[e];
        if (q) {
            if (ch == '\\' && doc->text[e + 1]) e++;
            else if (ch == q) q = 0;
            e++;
            continue;
        }
        if (ch == '"' || ch == '\'') { q = ch; e++; continue; }
        if (ch == '(') pd++;
        else if (ch == ')') { if (pd) pd--; }
        else if (ch == '[') bd++;
        else if (ch == ']') { if (bd) bd--; }
        else if (ch == '<' && !pd && !bd) ad++;
        else if (ch == '>' && !pd && !bd) { if (ad) ad--; }
        else if ((ch == ';' || ch == ',') && !pd && !bd && !ad) break;
        e++;
    }
    while (e > i && (doc->text[e - 1] == ' ' || doc->text[e - 1] == '\t')) e--;
    if (e <= i || e - i >= 256) return 0;
    char init[256];
    memcpy(init, doc->text + i, e - i);
    init[e - i] = '\0';

    char fm[64];
    /* the resolver treats everything after the last dot as the completion
     * prefix, not a chain segment — append a dot so the final call is
     * resolved for its return type */
    char dotted[300];
    snprintf(dotted, sizeof(dotted), "%s.", init);
    const char *ct = intel_resolve_chain_pos(is, g_project_intel, dotted, fm,
                                             sizeof(fm), sym->line, sym->col);
    if ((!ct || !ct[0]) && g_project_intel)
        ct = intel_resolve_chain_pos(g_project_intel, NULL, dotted, fm,
                                     sizeof(fm), sym->line, sym->col);
    if (!ct || !ct[0] || strcmp(ct, "var") == 0) return 0;
    return intel_complete_members_pos(is, ct, prefix, line, character);
}

static void handle_completion(lsp_server_t *s, json_value *id, json_value *params) {    const char *uri; int line, character;
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

    /* intellisense_t is large (~2 MB); keep it off the stack. The engine is
     * cached per (uri, doc version) and reused across requests instead of
     * being malloc'd and the document re-scanned on every keystroke. */
    intellisense_t *is = doc_intel_for(s, uri);
    if (!is) { send_response(s, id, json_new_arr()); return; }

    const char *effective = prefix[0] ? prefix : "";
    int count = 0;

    /* `using` directive: offer namespaces instead of the symbol walk. */
    char ns_prefix[128];
    bool ns_mode = using_ns_context(doc->text, off, ns_prefix, sizeof(ns_prefix));
    if (ns_mode) {
        count = intel_complete_usings(is, g_project_intel, ns_prefix);
    }

    /* If we have a context (member access), try member completion */
    if (!ns_mode && context[0]) {
        const char *resolve_type = context;
        char chain_prefix[128] = "";

        /* Handle chain expressions (CHAIN:prefix) */
        if (strncmp(context, "CHAIN:", 6) == 0) {
            const char *chain_expr = context + 6;
            /* Use intel_resolve_chain_pos to get the type at end of chain, with cursor line */
            const char *chain_type = intel_resolve_chain_pos(is, g_project_intel, chain_expr,
                                                             chain_prefix, sizeof(chain_prefix),
                                                             line, character);
            if (!chain_type && g_project_intel) {
                chain_type = intel_resolve_chain_pos(g_project_intel, NULL, chain_expr,
                                                     chain_prefix, sizeof(chain_prefix),
                                                     line, character);
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
            /* column-aware: mid-line a same-named inner local may shadow the
             * receiver, or an expired block local must not resolve at all —
             * the col-less resolution would answer for the whole line */
            count = intel_complete_members_pos(is, resolve_type, effective,
                                               line, character);
            if (count == 0 && strncmp(context, "CHAIN:", 6) != 0) {
                /* cross-file `var` initializer: see complete_var_init_members */
                count = complete_var_init_members(doc, is, context, line,
                                                  character, effective);
            }
            /* Augment with project-wide members of the same type. The
             * receiver name must be resolved against the OPEN DOCUMENT
             * first — handing the raw name to the project index resolves
             * it against identically-named variables from unrelated files
             * and floods the list with a stranger type's members. */
            if (g_project_intel) {
                /* Resolve the receiver name at the real cursor column (see
                 * the hover call site: column-less lookup hides a method's
                 * parameters behind any smaller earlier method body). */
                const char *local_t = intel_resolve_type_pos(is, resolve_type,
                                                             line, character);
                const char *query_t = (local_t && local_t[0]) ? local_t
                                                              : resolve_type;
                int before = count;
                intel_complete_members(g_project_intel, query_t, effective);
                for (int pi = 0;
                     pi < g_project_intel->completion_count &&
                     count < INTEL_MAX_COMPLETIONS;
                     pi++) {
                    bool dup = false;
                    for (int li = 0; li < before; li++) {
                        if (strcmp(is->completions[li].label,
                                  g_project_intel->completions[pi].label) == 0) {
                            dup = true;
                            break;
                        }
                    }
                    if (!dup) {
                        is->completions[count] =
                            g_project_intel->completions[pi];
                        count++;
                    }
                }
                is->completion_count = count;
            }
        }
    } else if (!ns_mode && effective[0]) {
        /* column-aware bare-prefix completion: a block local must show up
         * inside its block and vanish the moment the block closes, even
         * when everything shares one physical line */
        count = intel_complete_pos(is, effective, NULL, line, character);
        /* Supplement with project-wide symbols if we have few results.
         * The enclosing class comes from the live buffer: designer-projected
         * widget fields live only in the index as members of the partial
         * class, and member symbols need their owner on record to pass the
         * bare-symbol rank guard. */
        if (g_project_intel && count < 20) {
            const char *encl = intel_enclosing_type_at(is, line, character);
            int proj_count = intel_complete_bare(g_project_intel, effective, encl);
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

    intellisense_t *is = doc_intel_for(s, uri);
    if (!is) { send_response(s, id, json_new_null()); return; }

    if (getenv("ZAN_LSP_DUMP_SYMS")) {
        fprintf(stderr, "DUMP %s symbols=%d methods=%d\n", uri, is->symbol_count, is->method_count);
        for (int i = 0; i < is->symbol_count; i++) {
            isym_t *y = &is->symbols[i];
            fprintf(stderr, "  [%d] '%s' kind=%d type='%s' parent='%s' line=%d col=%d mo=%d scope=%d,%d-%d,%d\n",
                    i, y->name, (int)y->kind, y->type_name, y->parent, y->line, y->col,
                    y->method_offset, y->scope_start_line, y->scope_start_col,
                    y->scope_end_line, y->scope_end_col);
        }
        for (int i = 0; i < is->method_count; i++) {
            imethod_t *m = &is->methods[i];
            fprintf(stderr, "  M[%d] %s.%s decl=%d span=%d,%d-%d,%d\n", i, m->parent, m->name,
                    m->decl_offset, m->start_line, m->start_col, m->end_line, m->end_col);
        }
    }
    /* `receiver.word` resolves first: the member of the receiver's type is
     * the correct description even when a same-named local shadows the word
     * in the document (cursor on `Visible` of `a.Visible` with a local
     * `string Visible` in scope). The plain name lookup below is the
     * fallback for words that are not member accesses. */
    hover_info_t h = {0};
    char chain[256];
    if (receiver_chain_before(doc->text, off, chain, sizeof(chain))) {
        char fm[64];
        const char *rt = intel_resolve_chain_pos(is, g_project_intel, chain,
                                                 fm, sizeof(fm), line, character);
        if (!rt || !rt[0]) {
            /* retry with the engines swapped: designer-projected fields
             * exist only in the project index, so the root may resolve
             * there while the enclosing class comes from the document */
            rt = intel_resolve_chain_pos(g_project_intel, is, chain,
                                         fm, sizeof(fm), line, character);
        }
        if (rt && rt[0] && g_project_intel)
            h = intel_hover_member(g_project_intel, rt, word);
    }

    /* Resolve the plain word against the real cursor column. Column-less
     * lookups make intel_method_at match every method on the line (the
     * smallest span wins), which hides Run's parameters behind an earlier
     * small method and sends the name to the project-index sewer. */
    if (!h.valid)
        h = intel_hover_pos(is, word, line, character);

    /* cross-file symbols (e.g. a design-doc-projected widget field referenced
     * from the business file) live in the project index */
    if (!h.valid && g_project_intel)
        h = intel_hover_at(g_project_intel, word, -1);
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

/* Native path <-> file:  // URI conversion; defined further below alongside
 * the reference-walking helpers that share them. */
static void fspath_to_uri(const char *path, char *out, size_t cap);
static bool same_uri_ci(const char *a, const char *b);

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

    intellisense_t *is = doc_intel_for(s, uri);
    if (!is) { send_response(s, id, json_new_null()); return; }
    goto_def_t g = {0};
    char chain[256];
    bool member_ctx = receiver_chain_before(doc->text, off, chain, sizeof(chain));

    /* `receiver.word` resolves first: the member of the receiver's type is
     * the correct target even when a same-named local shadows the word in
     * the document (cursor on `Visible` of `a.Visible` with a local
     * `string Visible` in scope). The name-only walk is the fallback for
     * words that are not member accesses. */
    if (member_ctx) {
        char fm[64];
        const char *rt = intel_resolve_chain_pos(is, g_project_intel, chain,
                                                 fm, sizeof(fm), line, character);
        if (!rt || !rt[0]) {
            /* retry with the engines swapped: designer-projected fields
             * exist only in the project index, so the root may resolve
             * there while the enclosing class comes from the document */
            rt = intel_resolve_chain_pos(g_project_intel, is, chain,
                                         fm, sizeof(fm), line, character);
        }
        if (rt && rt[0] && g_project_intel) {
            intel_goto_member(g_project_intel, rt, word, &g);
        }
    }
    if (!g.found) {
        /* scope-aware local: shadowing and block expiry are column-sensitive
         * on a same-line block (`} same` is the outer local again), so the
         * lexical lookup with the real column wins before any name-only
         * walk — which would answer with the first same-named symbol in the
         * file (often a field) regardless of scope */
        const isym_t *loc = intel_lookup_symbol_at(is, word, line, character);
        if (loc && (loc->kind == ISYM_VARIABLE || loc->kind == ISYM_PARAMETER)) {
            g.found = true;
            g.line = loc->line;
            g.col = loc->col;
            g.file[0] = '\0'; /* same document */
        }
    }
    if (!g.found)
        g = intel_goto_def(is, word);
    if (!g.found && g_project_intel)
        g = intel_goto_def(g_project_intel, word);
    if (!g.found) { send_response(s, id, json_new_null()); return; }

    /* The target file may be recorded as a native path (project index) or as
     * the document URI (doc engine); always answer with a proper file:  // URI
     * so clients can open it, and compare through same_uri_ci, which decodes
     * and case-folds both sides. */
    char target_uri[1800];
    if (g.file[0] && strncmp(g.file, "file://", 7) == 0) {
        snprintf(target_uri, sizeof(target_uri), "%s", g.file);
    } else if (g.file[0]) {
        fspath_to_uri(g.file, target_uri, sizeof(target_uri));
    } else {
        snprintf(target_uri, sizeof(target_uri), "%s", uri);
    }
    bool same_doc = same_uri_ci(target_uri, uri);
    int dl, dc;
    if (same_doc || (g.file[0] && lsp_find_doc(s, target_uri))) {
        dl = g.line;
        dc = g.col;
    } else {
        /* cross-file symbol (e.g. a design-doc-projected field): the target
         * file is not open here, so use the recorded line directly; design-doc
         * symbols carry their definition line. */
        dl = g.line;
        dc = 0;
    }

    json_value *loc = json_new_obj();
    json_obj_set(loc, "uri", json_new_str(target_uri));
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

/* Scan document text for whole-word occurrences of `word`, skipping plain
 * string/char literals and line/block comments so matches are real code
 * references. Interpolated strings ($"...", with nested holes) contribute
 * the identifiers inside their {holes}: those are code — {{ and }} are
 * escaped braces, and everything after `:` or `,` inside a hole is format
 * text, not expression. */
static void scan_string_refs(const char *text, size_t *ip, const char *word,
                             size_t wlen, size_t *offsets, int *count,
                             int max, int depth) {
    size_t i = *ip;
    char q = text[i];
    bool interp = false;
    if (q == '"' && depth < 4) {
        size_t b = i;
        if (b > 0 && text[b - 1] == '$') interp = true;
        else if (b > 1 && ((text[b - 1] == '@' && text[b - 2] == '$') ||
                           (text[b - 1] == '$' && text[b - 2] == '@')))
            interp = true;
    }
    i++;
    if (!interp) {
        while (text[i] && text[i] != q) {
            if (text[i] == '\\' && text[i + 1]) i++;
            i++;
        }
        if (text[i]) i++;
        *ip = i;
        return;
    }
    /* interpolated: {hole} contents are code. The loop must not stop at a
     * bare `"` — inside a hole that quote opens a nested string (recurse);
     * only a `"` at hole==0 closes this string. */
    int hole = 0;
    bool fmt = false;
    while (text[i]) {
        char d = text[i];
        if (hole == 0) {
            if (d == '"') break; /* real closing quote */
            if (d == '{') {
                if (text[i + 1] == '{') { i += 2; continue; } /* escaped */
                hole = 1;
                fmt = false;
                i++;
                continue;
            }
            i++;
            continue;
        }
        if (d == '}') { hole = 0; fmt = false; i++; continue; }
        if (d == '"' || d == '\'') {
            scan_string_refs(text, &i, word, wlen, offsets, count, max, depth + 1);
            continue;
        }
        if (!fmt && (d == ':' || d == ',')) { fmt = true; i++; continue; }
        if (!fmt && is_ident_char(d) && (i == 0 || !is_ident_char(text[i - 1]))) {
            size_t j = i;
            while (text[j] && is_ident_char(text[j])) j++;
            if ((j - i) == wlen && strncmp(text + i, word, wlen) == 0 &&
                *count < max)
                offsets[(*count)++] = i;
            i = j;
            continue;
        }
        i++;
    }
    if (text[i]) i++;
    *ip = i;
}

static int find_text_references(const char *text, const char *word,
                                size_t *offsets, int max) {
    int count = 0;
    size_t wlen = strlen(word);
    if (wlen == 0) return 0;
    size_t i = 0;
    while (text[i] && count < max) {
        char c = text[i];
        if (c == '/' && text[i + 1] == '/') {
            i += 2;
            while (text[i] && text[i] != '\n') i++;
            continue;
        }
        if (c == '/' && text[i + 1] == '*') {
            i += 2;
            while (text[i] && !(text[i] == '*' && text[i + 1] == '/')) i++;
            if (text[i]) i += 2;
            continue;
        }
        if (c == '"' || c == '\'') {
            scan_string_refs(text, &i, word, wlen, offsets, &count, max, 0);
            continue;
        }
        if (is_ident_char(c) && (i == 0 || !is_ident_char(text[i - 1]))) {
            size_t j = i;
            while (text[j] && is_ident_char(text[j])) j++;
            if ((j - i) == wlen && strncmp(text + i, word, wlen) == 0)
                offsets[count++] = i;
            i = j;
            continue;
        }
        i++;
    }
    return count;
}

/* Reads a whole file into a malloc'd NUL-terminated buffer (NULL on failure). */
static char *read_file_all(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long n = ftell(f);
    if (n < 0) { fclose(f); return NULL; }
    rewind(f);
    char *buf = (char *)malloc((size_t)n + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t rd = fread(buf, 1, (size_t)n, f);
    buf[rd] = '\0';
    fclose(f);
    return buf;
}

/* file:  // URI for a native path, separators normalized to '/' and percent-
 * encoded, so echoed URIs match the encoded forms clients send for the
 * same files (spaces, '%', non-ASCII). */
static void fspath_to_uri(const char *path, char *out, size_t cap) {
    if (strncmp(path, "file://", 7) == 0) {
        snprintf(out, cap, "%s", path);
        return;
    }
    char raw[1800];
    if (path[0] == '/') {
        snprintf(raw, sizeof(raw), "file://%s", path);
    } else {
        snprintf(raw, sizeof(raw), "file:///%s", path);
    }
    for (char *p = raw; *p; p++) if (*p == '\\') *p = '/';
    uri_percent_encode_path(raw, out, cap);
}

/* Two URIs naming the same file: separators and case are irrelevant on the
 * platforms zan targets (Windows paths differ in drive-letter case, and the
 * client may send either separator). Both sides are percent-decoded first,
 * so a client-encoded URI matches the same file however we spell it. */
static bool same_uri_ci(const char *a, const char *b) {
    char da[1800], db[1800];
    uri_percent_decode(a, da, sizeof(da));
    uri_percent_decode(b, db, sizeof(db));
    a = da;
    b = db;
    while (*a && *b) {
        char ca = *a == '\\' ? '/' : (char)tolower((unsigned char)*a);
        char cb = *b == '\\' ? '/' : (char)tolower((unsigned char)*b);
        if (ca != cb) return false;
        a++; b++;
    }
    return *a == '\0' && *b == '\0';
}

/* True when `uri` is one of the currently open documents. */
static bool uri_is_open(lsp_server_t *s, const char *uri) {
    for (int i = 0; i < s->doc_count; i++)
        if (same_uri_ci(s->docs[i].uri, uri)) return true;
    return false;
}

/* One Location per whole-word occurrence of `word` in `text`. `lo`/`hi` bound
 * the reported lines inclusively (-1/-1 = unbounded). Returns the number
 * appended to `arr`. */
static int add_locations(json_value *arr, const char *uri,
                         const char *text, const char *word,
                         int lo, int hi) {
    size_t offsets[512];
    int n = find_text_references(text, word, offsets, 512);
    for (int i = 0; i < n; i++) {
        int rl, rc;
        offset_to_linecol(text, (int)offsets[i], &rl, &rc);
        if (lo >= 0 && (rl < lo || rl > hi)) continue;
        json_value *loc = json_new_obj();
        json_obj_set(loc, "uri", json_new_str(uri));
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
    return n;
}

/* Two resolved type names name the same type when their simple names
 * (namespace, generics and arrays stripped) match — the same pairing
 * intel_member_sym uses to walk a receiver type to its declaring class. */
static bool type_simple_same(const char *a, const char *b) {
    char sa[128], sb[128];
    snprintf(sa, sizeof(sa), "%s", a ? a : "");
    snprintf(sb, sizeof(sb), "%s", b ? b : "");
    sa[strcspn(sa, "<?")] = '\0';
    sb[strcspn(sb, "<?")] = '\0';
    const char *pa = strrchr(sa, '.'); pa = pa ? pa + 1 : sa;
    const char *pb = strrchr(sb, '.'); pb = pb ? pb + 1 : sb;
    return strcmp(pa, pb) == 0;
}

/* The symbol a references/rename request targets, resolved at the cursor:
 * a member access resolves through the receiver's type, otherwise the word
 * is looked up scope-aware (a local beats a same-named field only when the
 * local is actually visible there). Occurrences are then accepted only when
 * they resolve back to this same target — the textual whole-word walks
 * would conflate shadowing locals, same-named fields of other classes and
 * interpolation/literal text. */
typedef struct {
    bool ok;
    bool is_local;
    char rt_simple[128]; /* member owner, simple name */
    bool have_decl;
    char decl_ref[512];  /* declaration file (native path or file:  // ) */
    int decl_line, decl_col; /* member declaration position */
    int ldecl_line, ldecl_col; /* local declaration position */
    const char *origin_uri;
    char word[128];
} sym_target_t;

static bool sym_target_is_decl(const sym_target_t *t, const char *doc_uri,
                               int rl, int rc) {
    if (t->is_local)
        return strcmp(doc_uri, t->origin_uri) == 0 &&
               rl == t->ldecl_line && rc == t->ldecl_col;
    return t->have_decl && same_uri_ci(doc_uri, t->decl_ref) &&
           rl == t->decl_line && rc == t->decl_col;
}

static bool occurrence_matches(const sym_target_t *t, const char *text,
                               intellisense_t *de, intellisense_t *project,
                               const char *doc_uri, size_t off, int rl, int rc) {
    if (t->is_local) {
        /* a local never escapes its file; identity is its declaration
         * position, NOT an engine pointer — asking another document for its
         * engine may evict and rebuild the origin engine mid-request, so
         * anything captured earlier by address is stale by the time the
         * per-occurrence checks run */
        if (!de || strcmp(doc_uri, t->origin_uri) != 0) return false;
        /* `recv.word` is a member access, never a use of the local — the
         * lookup here would still return the same-named visible local */
        if (off > 0 && text[off - 1] == '.') return false;
        const isym_t *s2 = intel_lookup_symbol_at(de, t->word, rl, rc);
        return s2 && (s2->kind == ISYM_VARIABLE || s2->kind == ISYM_PARAMETER) &&
               s2->line == t->ldecl_line && s2->col == t->ldecl_col;
    }
    if (sym_target_is_decl(t, doc_uri, rl, rc)) return true;
    char chain[256];
    if (receiver_chain_before(text, off, chain, sizeof(chain))) {
        char fm[64];
        const char *rt2 = de ? intel_resolve_chain_pos(de, project, chain, fm,
                                                       sizeof(fm), rl, rc)
                             : NULL;
        if ((!rt2 || !rt2[0]) && project)
            rt2 = intel_resolve_chain_pos(project, de, chain, fm, sizeof(fm),
                                          rl, rc);
        if (rt2 && rt2[0] && type_simple_same(rt2, t->rt_simple)) return true;
    }
    /* a bare use of a field inside its own class resolves lexically to the
     * member (the enclosing-type branch of intel_lookup_symbol_at) */
    if (de) {
        const isym_t *s2 = intel_lookup_symbol_at(de, t->word, rl, rc);
        if (s2 && s2->parent[0] && type_simple_same(s2->parent, t->rt_simple))
            return true;
    }
    return false;
}

static bool resolve_sym_target(intellisense_t *is, intellisense_t *project,
                               const char *text, size_t off, const char *uri,
                               const char *word, int line, int character,
                               sym_target_t *t) {
    memset(t, 0, sizeof(*t));
    snprintf(t->word, sizeof(t->word), "%s", word);
    t->origin_uri = uri;

    /* `receiver.word`: the member of the receiver's type */
    char chain[256];
    if (receiver_chain_before(text, off, chain, sizeof(chain))) {
        char fm[64];
        const char *rt = is ? intel_resolve_chain_pos(is, project, chain, fm,
                                                      sizeof(fm), line, character)
                            : NULL;
        if ((!rt || !rt[0]) && project)
            rt = intel_resolve_chain_pos(project, is, chain, fm, sizeof(fm),
                                         line, character);
        if (rt && rt[0] && project) {
            goto_def_t decl;
            memset(&decl, 0, sizeof(decl));
            if (intel_goto_member(project, rt, word, &decl) && decl.file[0]) {
                char duri[512];
                fspath_to_uri(decl.file, duri, sizeof(duri));
                t->ok = true;
                t->is_local = false;
                snprintf(t->rt_simple, sizeof(t->rt_simple), "%s", rt);
                t->have_decl = true;
                snprintf(t->decl_ref, sizeof(t->decl_ref), "%s", duri);
                t->decl_line = decl.line;
                t->decl_col = decl.col;
                return true;
            }
        }
    }

    if (!is) return false;
    const isym_t *sym = intel_lookup_symbol_at(is, word, line, character);
    if (!sym) return false;
    if (sym->kind == ISYM_VARIABLE || sym->kind == ISYM_PARAMETER) {
        if (sym->line < 0) return false; /* synthesized (implicit setter value) */
        /* the recorded declaration must really spell the word: a synthesized
         * symbol points at the property line, not at any declaration of this
         * name in the text */
        char dcheck[128];
        word_at(text, pos_to_offset(text, sym->line, sym->col), dcheck,
                sizeof(dcheck));
        if (strcmp(dcheck, word) != 0) return false;
        t->ok = true;
        t->is_local = true;
        t->ldecl_line = sym->line;
        t->ldecl_col = sym->col;
        return true;
    }
    if ((sym->kind == ISYM_FIELD || sym->kind == ISYM_PROPERTY ||
         sym->kind == ISYM_EVENT || sym->kind == ISYM_METHOD) &&
        sym->parent[0]) {
        /* a member reached without a receiver: its declaration carries the
         * owner (cursor on the field's own declaration, or a bare use) */
        t->ok = true;
        t->is_local = false;
        snprintf(t->rt_simple, sizeof(t->rt_simple), "%s", sym->parent);
        t->have_decl = true;
        {
            char duri[512];
            fspath_to_uri(sym->file[0] ? sym->file : uri, duri, sizeof(duri));
            snprintf(t->decl_ref, sizeof(t->decl_ref), "%s", duri);
        }
        t->decl_line = sym->line;
        t->decl_col = sym->col;
        return true;
    }
    return false;
}

/* Locations for a resolved target across one document: only occurrences that
 * resolve back to the target, honoring includeDeclaration. */
static int add_scoped_locations(json_value *arr, const char *doc_uri,
                                const char *text, intellisense_t *de,
                                intellisense_t *project, const sym_target_t *t,
                                bool include_decl) {
    size_t offsets[512];
    int n = find_text_references(text, t->word, offsets, 512);
    int added = 0;
    for (int i = 0; i < n; i++) {
        int rl, rc;
        offset_to_linecol(text, (int)offsets[i], &rl, &rc);
        if (!occurrence_matches(t, text, de, project, doc_uri, offsets[i],
                                rl, rc))
            continue;
        if (!include_decl && sym_target_is_decl(t, doc_uri, rl, rc)) continue;
        json_value *loc = json_new_obj();
        json_obj_set(loc, "uri", json_new_str(doc_uri));
        json_value *range = json_new_obj();
        json_value *start = json_new_obj();
        json_value *endp  = json_new_obj();
        json_obj_set(start, "line", json_new_num(rl));
        json_obj_set(start, "character", json_new_num(rc));
        json_obj_set(endp, "line", json_new_num(rl));
        json_obj_set(endp, "character", json_new_num(rc + (int)strlen(t->word)));
        json_obj_set(range, "start", start);
        json_obj_set(range, "end", endp);
        json_obj_set(loc, "range", range);
        json_arr_add(arr, loc);
        added++;
    }
    return added;
}

/* Scoped walk over unopened project files. The name-only flood used to
 * answer these textually; the scoped walk keeps the same per-occurrence
 * identity by building one process-lifetime scratch engine per file. */
static intellisense_t *g_unopened_intel = NULL;

typedef struct {
    json_value *out;      /* locations array (references) or changes object (rename) */
    const sym_target_t *t;
    const char *new_name; /* rename mode when non-NULL */
    bool include_decl;
    bool covered_decl;
    intellisense_t *scratch;
} scoped_unopened_ctx_t;

static json_value *scoped_rename_edits(const char *doc_uri, const char *text,
                                       intellisense_t *de, intellisense_t *project,
                                       const sym_target_t *t,
                                       const char *new_name);

static void scoped_unopened_visit(void *ctx, const char *uri, const char *text) {
    scoped_unopened_ctx_t *uc = (scoped_unopened_ctx_t *)ctx;
    if (!uc->scratch) return;
    intel_clear(uc->scratch);
    intel_parse_file(uc->scratch, uri, text, strlen(text));
    if (!uc->t->is_local && uc->t->have_decl &&
        same_uri_ci(uri, uc->t->decl_ref))
        uc->covered_decl = true;
    if (uc->new_name) {
        json_value *edits = scoped_rename_edits(uri, text, uc->scratch,
                                                g_project_intel, uc->t,
                                                uc->new_name);
        if (edits) json_obj_set(uc->out, uri, edits);
    } else {
        add_scoped_locations(uc->out, uri, text, uc->scratch,
                             g_project_intel, uc->t, uc->include_decl);
    }
}

static intellisense_t *unopened_scratch(void) {
    if (!g_unopened_intel) {
        g_unopened_intel = (intellisense_t *)malloc(sizeof(*g_unopened_intel));
        if (g_unopened_intel) intel_init(g_unopened_intel);
    }
    return g_unopened_intel;
}

/* Runs `visit` over every project file that is not currently open, reading it
 * from disk. Open documents are skipped because their in-memory text (which
 * includes unsaved edits) is authoritative. */
typedef void (*project_file_fn)(void *ctx, const char *uri, const char *text);

static bool lsp_cancel_hit(lsp_server_t *s);

static void for_each_unopened_project_file(lsp_server_t *s, void *ctx,
                                           project_file_fn visit) {
    ensure_project_indexed(s);
    if (!g_project_intel) return;
    for (int f = 0; f < g_project_intel->indexed_file_count; f++) {
        if (lsp_cancel_hit(s)) return;
        const char *fp = g_project_intel->indexed_files[f];
        if (!fp[0]) continue;
        char furi[1800];
        fspath_to_uri(fp, furi, sizeof(furi));
        if (uri_is_open(s, furi)) continue;
        char *text = read_file_all(fp);
        if (!text) continue;
        visit(ctx, furi, text);
        free(text);
    }
}

typedef struct {
    json_value *arr;
    const char *word;
} refs_ctx_t;

static void refs_visit(void *ctx, const char *uri, const char *text) {
    refs_ctx_t *rc = (refs_ctx_t *)ctx;
    add_locations(rc->arr, uri, text, rc->word, -1, -1);
}

/* Find all references, project-wide: every open document plus every indexed
 * project file on disk, so references in files the editor never opened are
 * reported too. */
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

    /* Symbol-scoped path: locals, fields, properties and methods resolve to
     * a concrete target at the cursor, and only occurrences that resolve back
     * to the same target are reported — a textual walk would conflate a
     * shadowing local, same-named members of other classes and literal text. */
    intellisense_t *is = doc_intel_for(s, uri);
    sym_target_t t;
    bool resolved = resolve_sym_target(is, g_project_intel, doc->text, off, uri, word,
                                       line, character, &t);
    if (resolved) {
        json_value *arr = json_new_arr();
        bool include_decl = true;
        json_value *ctxj = json_obj_get(params, "context");
        if (ctxj) {
            json_value *idj = json_obj_get(ctxj, "includeDeclaration");
            /* clients send a real false; json_get_num would default a bool
             * to the fallback, so read booleans as booleans */
            if (idj) include_decl = json_get_bool(idj, json_get_num(idj, 1) != 0.0);
        }
        /* origin document first: doc_intel_for for another document can
         * evict and rebuild this request's own engine from the small cache */
        int od = -1;
        for (int d = 0; d < s->doc_count; d++)
            if (strcmp(s->docs[d].uri, uri) == 0) { od = d; break; }
        for (int pass = 0; pass < s->doc_count; pass++) {
            if (lsp_cancel_hit(s)) break;
            int d = pass;
            if (od >= 0) d = (pass == 0) ? od : (pass <= od ? pass - 1 : pass);
            lsp_doc_t *dd = &s->docs[d];
            intellisense_t *de = (d == od)
                                 ? is : doc_intel_for(s, dd->uri);
            add_scoped_locations(arr, dd->uri, dd->text, de, g_project_intel,
                                 &t, include_decl);
        }
        /* unopened project files: same per-occurrence identity, one scratch
         * engine reused across files */
        bool covered_decl = false;
        if (!lsp_cancel_hit(s) && g_project_intel) {
            intellisense_t *scratch = unopened_scratch();
            if (scratch) {
                scoped_unopened_ctx_t uc;
                uc.out = arr;
                uc.t = &t;
                uc.new_name = NULL;
                uc.include_decl = include_decl;
                uc.covered_decl = false;
                uc.scratch = scratch;
                for_each_unopened_project_file(s, &uc, scoped_unopened_visit);
                covered_decl = uc.covered_decl;
            }
        }
        /* the member may be declared in a file never opened here */
        if (!t.is_local && t.have_decl && include_decl && !covered_decl) {
            bool covered = false;
            for (int d = 0; d < s->doc_count; d++)
                if (same_uri_ci(s->docs[d].uri, t.decl_ref)) covered = true;
            if (!covered && !lsp_cancel_hit(s)) {
                char duri[1800];
                if (strncmp(t.decl_ref, "file://", 7) == 0)
                    snprintf(duri, sizeof(duri), "%s", t.decl_ref);
                else
                    fspath_to_uri(t.decl_ref, duri, sizeof(duri));
                json_value *loc = json_new_obj();
                json_obj_set(loc, "uri", json_new_str(duri));
                json_value *range = json_new_obj();
                json_value *start = json_new_obj();
                json_value *endp  = json_new_obj();
                json_obj_set(start, "line", json_new_num(t.decl_line));
                json_obj_set(start, "character", json_new_num(t.decl_col));
                json_obj_set(endp, "line", json_new_num(t.decl_line));
                json_obj_set(endp,
                             "character",
                             json_new_num(t.decl_col + (int)strlen(t.word)));
                json_obj_set(range, "start", start);
                json_obj_set(range, "end", endp);
                json_obj_set(loc, "range", range);
                json_arr_add(arr, loc);
            }
        }
        if (lsp_cancel_hit(s)) {
            json_free(arr);
            send_response_error(s, id, -32800, "Request cancelled");
            return;
        }
        send_response(s, id, arr);
        return;
    }

    /* Name-only fallback (type names and words no index knows): every open
     * document plus every indexed project file on disk. */
    json_value *arr = json_new_arr();
    for (int d = 0; d < s->doc_count; d++) {
        if (lsp_cancel_hit(s)) break;
        add_locations(arr, s->docs[d].uri, s->docs[d].text, word, -1, -1);
    }
    if (!lsp_cancel_hit(s)) {
        refs_ctx_t rc; rc.arr = arr; rc.word = word;
        for_each_unopened_project_file(s, &rc, refs_visit);
    }
    /* A cancelled request answers -32800, never a partial success. */
    if (lsp_cancel_hit(s)) {
        json_free(arr);
        send_response_error(s, id, -32800, "Request cancelled");
        return;
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

    char method_name[128], class_context[128], recv_chain[256];
    int active_param = 0;
    method_call_context(doc->text, off, method_name, sizeof(method_name),
                        class_context, sizeof(class_context), &active_param,
                        recv_chain, sizeof(recv_chain));

    if (!method_name[0]) { send_response(s, id, json_new_null()); return; }

    intellisense_t *is = doc_intel_for(s, uri);
    if (!is) { send_response(s, id, json_new_null()); return; }

    signature_info_t sig = intel_signature_help(is, method_name,
                                                 class_context[0] ? class_context : NULL);

    /* Receiver-typed fallback: `MainStatusBar.SetText(` — the bare receiver
     * name is a field, not a type, so the context lookup above comes back
     * empty and the signature would be null. Resolve the receiver's type
     * (designer fields live in the project index) and ask again with the
     * concrete type. */
    if (!sig.valid && recv_chain[0]) {
        char fm[64];
        const char *rt = intel_resolve_chain_pos(is, g_project_intel, recv_chain,
                                                 fm, sizeof(fm), line, character);
        if (!rt || !rt[0]) {
            rt = intel_resolve_chain_pos(g_project_intel, is, recv_chain,
                                         fm, sizeof(fm), line, character);
        }
        if (rt && rt[0])
            sig = intel_signature_help_pos_ex(is, g_project_intel, method_name,
                                              rt, line, character);
    }

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

    intellisense_t *is = doc_intel_for(s, uri);
    if (!is) { send_response(s, id, json_new_arr()); return; }

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
    send_response(s, id, arr);
}

/* One TextEdit per whole-word occurrence of `word`, or NULL when there is
 * none in this text. `lo`/`hi` bound the edited lines inclusively
 * (-1/-1 = unbounded). */
static json_value *rename_edits_for(const char *text, const char *word,
                                    const char *new_name, int lo, int hi) {
    size_t offsets[512];
    int n = find_text_references(text, word, offsets, 512);
    if (n == 0) return NULL;
    json_value *edits = json_new_arr();
    for (int i = 0; i < n; i++) {
        int rl, rc;
        offset_to_linecol(text, (int)offsets[i], &rl, &rc);
        if (lo >= 0 && (rl < lo || rl > hi)) continue;
        json_value *edit = json_new_obj();
        json_value *range = json_new_obj();
        json_value *start = json_new_obj();
        json_value *endp  = json_new_obj();
        json_obj_set(start, "line", json_new_num(rl));
        json_obj_set(start, "character", json_new_num(rc));
        json_obj_set(endp, "line", json_new_num(rl));
        json_obj_set(endp, "character", json_new_num(rc + (int)strlen(word)));
        json_obj_set(range, "start", start);
        json_obj_set(range, "end", endp);
        json_obj_set(edit, "range", range);
        json_obj_set(edit, "newText", json_new_str(new_name));
        json_arr_add(edits, edit);
    }
    return edits;
}

/* Two resolved type names name the same type when their simple names
 * (namespace, generics and arrays stripped) match — the same pairing
 * intel_member_sym uses to walk a receiver type to its declaring class. */
/* TextEdit list for the occurrences of a resolved target in one document,
 * each rewritten to `new_name`. NULL when nothing in this document matches. */
static json_value *scoped_rename_edits(const char *doc_uri, const char *text,
                                       intellisense_t *de, intellisense_t *project,
                                       const sym_target_t *t,
                                       const char *new_name) {
    size_t offsets[512];
    int n = find_text_references(text, t->word, offsets, 512);
    json_value *edits = NULL;
    for (int i = 0; i < n; i++) {
        int rl, rc;
        offset_to_linecol(text, (int)offsets[i], &rl, &rc);
        bool keep = occurrence_matches(t, text, de, project, doc_uri,
                                       offsets[i], rl, rc);
        if (!keep)
            continue;
        if (!edits) edits = json_new_arr();
        json_value *edit = json_new_obj();
        json_value *range = json_new_obj();
        json_value *start = json_new_obj();
        json_value *endp  = json_new_obj();
        json_obj_set(start, "line", json_new_num(rl));
        json_obj_set(start, "character", json_new_num(rc));
        json_obj_set(endp, "line", json_new_num(rl));
        json_obj_set(endp, "character", json_new_num(rc + (int)strlen(t->word)));
        json_obj_set(range, "start", start);
        json_obj_set(range, "end", endp);
        json_obj_set(edit, "range", range);
        json_obj_set(edit, "newText", json_new_str(new_name));
        json_arr_add(edits, edit);
    }
    return edits;
}

/* Rename handler: symbol-scoped WorkspaceEdit when the word under the cursor
 * resolves to a declaration; otherwise a lexical whole-word rename confined to
 * the origin document. */
static void handle_rename(lsp_server_t *s, json_value *id, json_value *params) {
    const char *uri; int line, character;
    const char *new_name = json_get_str(json_obj_get(params, "newName"));
    if (!get_position(params, &uri, &line, &character) || !new_name || !new_name[0]) {
        send_response(s, id, json_new_null());
        return;
    }
    /* the replacement must be a valid identifier */
    if (!(isalpha((unsigned char)new_name[0]) || new_name[0] == '_')) {
        send_response(s, id, json_new_null());
        return;
    }
    for (const char *p = new_name + 1; *p; p++) {
        if (!is_ident_char(*p)) { send_response(s, id, json_new_null()); return; }
    }
    /* a keyword parses as an identifier-shaped token but is not one — the
     * renamed code would not compile */
    if (intel_is_keyword(new_name)) {
        send_response_error(s, id, -32602, "newName is a keyword");
        return;
    }
    lsp_doc_t *doc = lsp_find_doc(s, uri);
    if (!doc) { send_response(s, id, json_new_null()); return; }

    size_t off = pos_to_offset(doc->text, line, character);
    char word[128];
    word_at(doc->text, off, word, sizeof(word));
    if (!word[0]) { send_response(s, id, json_new_null()); return; }

    intellisense_t *is = doc_intel_for(s, uri);
    sym_target_t t;
    bool resolved = resolve_sym_target(is, g_project_intel, doc->text, off, uri,
                                       word, line, character, &t);
    if (resolved) {
        /* renaming a local onto the name of another local/parameter in the
         * same file would capture or be captured — refuse instead of
         * producing code whose meaning silently changes */
        if (t.is_local && strcmp(word, new_name) != 0 && is) {
            for (int i = 0; i < is->symbol_count; i++) {
                const isym_t *sym = &is->symbols[i];
                if ((sym->kind == ISYM_VARIABLE || sym->kind == ISYM_PARAMETER) &&
                    strcmp(sym->name, new_name) == 0) {
                    send_response_error(s, id, -32602,
                                        "newName collides with an existing local");
                    return;
                }
            }
        }
        json_value *changes = json_new_obj();
        /* an open declaration file is covered by the scan: its declaration
         * occurrence resolves back to the target like any other use */
        bool decl_open = false;
        /* origin document first: asking another document for its engine can
         * evict and rebuild this request's own engine from the small cache,
         * so the origin pass must run while `is` is still live */
        int od = -1;
        for (int d = 0; d < s->doc_count; d++)
            if (strcmp(s->docs[d].uri, uri) == 0) { od = d; break; }
        for (int pass = 0; pass < s->doc_count; pass++) {
            if (lsp_cancel_hit(s)) break;
            int d = pass;
            if (od >= 0) d = (pass == 0) ? od : (pass <= od ? pass - 1 : pass);
            lsp_doc_t *dd = &s->docs[d];
            intellisense_t *de = (d == od)
                                 ? is : doc_intel_for(s, dd->uri);
            json_value *edits = de ? scoped_rename_edits(dd->uri, dd->text, de,
                                                         g_project_intel, &t,
                                                         new_name)
                                   : NULL;
            if (edits) json_obj_set(changes, dd->uri, edits);
            if (t.have_decl && same_uri_ci(dd->uri, t.decl_ref)) decl_open = true;
        }
        /* unopened project files: same per-occurrence identity for the edits */
        bool covered_decl = decl_open;
        if (!lsp_cancel_hit(s) && g_project_intel) {
            intellisense_t *scratch = unopened_scratch();
            if (scratch) {
                scoped_unopened_ctx_t uc;
                uc.out = changes;
                uc.t = &t;
                uc.new_name = new_name;
                uc.include_decl = true;
                uc.covered_decl = false;
                uc.scratch = scratch;
                for_each_unopened_project_file(s, &uc, scoped_unopened_visit);
                covered_decl = covered_decl || uc.covered_decl;
            }
        }
        /* the member may be declared in a file never opened here */
        if (!t.is_local && t.have_decl && !covered_decl) {
            json_value *edits = json_new_arr();
            json_value *edit = json_new_obj();
            json_value *range = json_new_obj();
            json_value *start = json_new_obj();
            json_value *endp  = json_new_obj();
            json_obj_set(start, "line", json_new_num(t.decl_line));
            json_obj_set(start, "character", json_new_num(t.decl_col));
            json_obj_set(endp, "line", json_new_num(t.decl_line));
            json_obj_set(endp,
                         "character",
                         json_new_num(t.decl_col + (int)strlen(t.word)));
            json_obj_set(range, "start", start);
            json_obj_set(range, "end", endp);
            json_obj_set(edit, "range", range);
            json_obj_set(edit, "newText", json_new_str(new_name));
            json_arr_add(edits, edit);
            char duri[1800];
            if (strncmp(t.decl_ref, "file://", 7) == 0)
                snprintf(duri, sizeof(duri), "%s", t.decl_ref);
            else
                fspath_to_uri(t.decl_ref, duri, sizeof(duri));
            json_obj_set(changes, duri, edits);
        }
        json_value *we = json_new_obj();
        json_obj_set(we, "changes", changes);
        send_response(s, id, we);
        return;
    }

    /* Unresolved word: fall back to a lexical rename, but only in the
     * document the request came from. Without a resolved identity there is
     * no way to tell same-named occurrences in other files apart, so a
     * cross-file textual flood would rewrite provably different symbols —
     * prepareRename already refuses unresolved words for the same reason. */
    json_value *changes = json_new_obj();
    json_value *edits = rename_edits_for(doc->text, word, new_name, -1, -1);
    if (edits) json_obj_set(changes, doc->uri, edits);

    json_value *we = json_new_obj();
    json_obj_set(we, "changes", changes);
    send_response(s, id, we);
}

/* ============================ formatting ============================= */

/* The same line-based formatter semantics as the zanfmt tool: 4 spaces per
 * brace level, a line whose first character is '}' drops one level before
 * emitting, a line whose last character is '{' bumps the level after,
 * trailing whitespace stripped, blank runs collapsed to one, final newline.
 * Unlike zanfmt the brace detection here is string/comment aware — a '{'
 * inside a literal or a trailing  // comment no longer shifts the indent.
 * Both stay line-based: nothing inside a line is ever re-spaced. */

typedef struct {
    bool in_str;    /* inside a "..." literal (carries over lines) */
    bool in_block;  /* inside a block comment (carries over lines) */
} fmt_scan_t;

typedef struct {
    int  level;            /* indent level for this line */
    int  lead;             /* original leading whitespace, in bytes */
    const char *content;   /* leading/trailing whitespace stripped */
    int  content_len;
    bool blank;
} fmt_line_t;

typedef struct { char *buf; size_t len, cap; } txtbuf_t;

static bool txtbuf_put(txtbuf_t *b, const char *s, size_t n) {
    if (b->len + n + 1 > b->cap) {
        size_t nc = b->cap ? b->cap * 2 : 8192;
        while (nc < b->len + n + 1) nc *= 2;
        char *nb = (char *)realloc(b->buf, nc);
        if (!nb) return false;
        b->buf = nb;
        b->cap = nc;
    }
    memcpy(b->buf + b->len, s, n);
    b->len += n;
    b->buf[b->len] = '\0';
    return true;
}

static bool txtbuf_spaces(txtbuf_t *b, int n) {
    static const char pad[32] = "                                ";
    while (n > 0) {
        int k = n < 32 ? n : 32;
        if (!txtbuf_put(b, pad, (size_t)k)) return false;
        n -= k;
    }
    return true;
}

/* Scan the document into per-line records, tracking the brace depth. The
 * string/block-comment state lives in `st` and carries across lines, so a
 * caller can resume. Returns a malloc'd array of *out_count records, NULL on
 * OOM. */
static fmt_line_t *fmt_scan(const char *text, int *out_count, fmt_scan_t *st) {
    int nlines = 1;
    for (const char *p = text; *p; p++)
        if (*p == '\n') nlines++;
    fmt_line_t *lines = (fmt_line_t *)malloc(sizeof(fmt_line_t) * (size_t)nlines);
    if (!lines) return NULL;

    int depth = 0;
    const char *p = text;
    int count = 0;
    while (*p) {
        fmt_line_t *L = &lines[count++];
        L->lead = 0;
        while (*p == ' ' || *p == '\t') { L->lead++; p++; }
        const char *cs = p;
        const char *le = strchr(p, '\n');
        if (!le) le = p + strlen(p);
        const char *ce = le;
        while (ce > cs && (ce[-1] == ' ' || ce[-1] == '\t' || ce[-1] == '\r')) ce--;
        L->content = cs;
        L->content_len = (int)(ce - cs);
        L->blank = L->content_len == 0;

        /* does the trimmed line end with a '{' that is outside strings and
         * comments? (a '}' at the line start never needs this check) */
        bool ends_open = false;
        for (const char *q = cs; q < ce; ) {
            char c = *q;
            if (st->in_block) {
                if (c == '*' && q + 1 < ce && q[1] == '/') { st->in_block = false; q += 2; }
                else q++;
                continue;
            }
            if (st->in_str) {
                if (c == '\\') { q += 2; continue; }
                if (c == '"') st->in_str = false;
                q++;
                continue;
            }
            if (c == '"') { st->in_str = true; q++; continue; }
            if (c == '/' && q + 1 < ce && q[1] == '/') break; /* line comment */
            if (c == '/' && q + 1 < ce && q[1] == '*') { st->in_block = true; q += 2; continue; }
            ends_open = (c == '{');
            q++;
        }

        if (!L->blank && L->content[0] == '}') {
            depth--;
            if (depth < 0) depth = 0;
        }
        L->level = depth;
        if (ends_open) depth++;

        p = (*le == '\n') ? le + 1 : le;
    }
    *out_count = count;
    return lines;
}

/* Whole-document formatting: one TextEdit replacing everything. */
static void handle_formatting(lsp_server_t *s, json_value *id, json_value *params) {
    json_value *td = json_obj_get(params, "textDocument");
    const char *uri = json_get_str(json_obj_get(td, "uri"));
    lsp_doc_t *doc = uri ? lsp_find_doc(s, uri) : NULL;
    json_value *result = json_new_arr();
    if (doc && doc->text && doc->text[0]) {
        fmt_scan_t st = {false, false};
        int count = 0;
        fmt_line_t *lines = fmt_scan(doc->text, &count, &st);
        if (lines) {
            txtbuf_t out = {0};
            bool ok = true;
            int blanks = 0;
            for (int i = 0; i < count && ok; i++) {
                fmt_line_t *L = &lines[i];
                if (L->blank) {
                    blanks++;
                    if (blanks <= 1) ok = txtbuf_put(&out, "\n", 1);
                    continue;
                }
                blanks = 0;
                ok = txtbuf_spaces(&out, L->level * 4) &&
                     txtbuf_put(&out, L->content, (size_t)L->content_len) &&
                     txtbuf_put(&out, "\n", 1);
            }
            if (ok && out.len > 0) {
                int el, ec;
                offset_to_linecol(doc->text, (int)strlen(doc->text), &el, &ec);
                json_value *edit = json_new_obj();
                json_value *range = json_new_obj();
                json_value *start = json_new_obj();
                json_value *endp = json_new_obj();
                json_obj_set(start, "line", json_new_num(0));
                json_obj_set(start, "character", json_new_num(0));
                json_obj_set(endp, "line", json_new_num(el));
                json_obj_set(endp, "character", json_new_num(ec));
                json_obj_set(range, "start", start);
                json_obj_set(range, "end", endp);
                json_obj_set(edit, "range", range);
                json_obj_set(edit, "newText", json_new_str(out.buf));
                json_arr_add(result, edit);
            }
            free(out.buf);
            free(lines);
        }
    }
    send_response(s, id, result);
}

/* Range formatting: re-indent only the touched lines (blank-run collapsing is
 * deliberately left out — it would reach outside the requested range). */
static void handle_range_formatting(lsp_server_t *s, json_value *id, json_value *params) {
    json_value *td = json_obj_get(params, "textDocument");
    const char *uri = json_get_str(json_obj_get(td, "uri"));
    lsp_doc_t *doc = uri ? lsp_find_doc(s, uri) : NULL;
    json_value *result = json_new_arr();
    if (doc && doc->text && doc->text[0]) {
        json_value *rg = json_obj_get(params, "range");
        int l0 = (int)json_get_num(json_obj_get(json_obj_get(rg, "start"), "line"), 0);
        int l1 = (int)json_get_num(json_obj_get(json_obj_get(rg, "end"), "line"), l0);
        if (l1 < l0) { int t = l0; l0 = l1; l1 = t; }

        fmt_scan_t st = {false, false};
        int count = 0;
        fmt_line_t *lines = fmt_scan(doc->text, &count, &st);
        if (lines) {
            if (l1 > count - 1) l1 = count - 1;
            for (int i = l0; i <= l1; i++) {
                fmt_line_t *L = &lines[i];
                if (L->blank) continue;
                int want = L->level * 4;
                if (want == L->lead) continue; /* already indented right */
                json_value *edit = json_new_obj();
                json_value *range = json_new_obj();
                json_value *start = json_new_obj();
                json_value *endp = json_new_obj();
                json_obj_set(start, "line", json_new_num(i));
                json_obj_set(start, "character", json_new_num(0));
                json_obj_set(endp, "line", json_new_num(i));
                /* leading whitespace is ASCII: bytes == UTF-16 units */
                json_obj_set(endp, "character", json_new_num(L->lead));
                json_obj_set(range, "start", start);
                json_obj_set(range, "end", endp);
                json_obj_set(edit, "range", range);
                char pad[128];
                int nsp = want < 120 ? want : 120;
                memset(pad, ' ', (size_t)nsp);
                pad[nsp] = '\0';
                json_obj_set(edit, "newText", json_new_str(pad));
                json_arr_add(result, edit);
            }
            free(lines);
        }
    }
    send_response(s, id, result);
}

/* =================== documentHighlight / folding ===================== */

static void handle_document_highlight(lsp_server_t *s, json_value *id, json_value *params) {
    const char *uri;
    int line, character;
    json_value *result = json_new_arr();
    if (get_position(params, &uri, &line, &character)) {
        lsp_doc_t *doc = lsp_find_doc(s, uri);
        if (doc) {
            size_t off = pos_to_offset(doc->text, line, character);
            char word[128];
            word_at(doc->text, off, word, sizeof(word));
            if (word[0]) {
                /* scope-aware bound: a local/parameter only highlights inside
                 * its own method body */
                int lo = -1, hi = -1;
                intellisense_t *is = doc_intel_for(s, uri);
                int ms = -1, me = -1, dl = -1;
                if (is && intel_local_extent(is, word, line, &ms, &me, &dl))
                    { lo = ms < dl ? ms : dl; hi = me; }
                size_t offs[512];
                int n = find_text_references(doc->text, word, offs, 512);
                for (int i = 0; i < n; i++) {
                    int rl, rc;
                    offset_to_linecol(doc->text, (int)offs[i], &rl, &rc);
                    if (lo >= 0 && (rl < lo || rl > hi)) continue;
                    json_value *hl = json_new_obj();
                    json_value *range = json_new_obj();
                    json_value *start = json_new_obj();
                    json_value *endp = json_new_obj();
                    json_obj_set(start, "line", json_new_num(rl));
                    json_obj_set(start, "character", json_new_num(rc));
                    json_obj_set(endp, "line", json_new_num(rl));
                    json_obj_set(endp, "character", json_new_num(rc + (int)strlen(word)));
                    json_obj_set(range, "start", start);
                    json_obj_set(range, "end", endp);
                    json_obj_set(hl, "range", range);
                    json_obj_set(hl, "kind", json_new_num(1)); /* Text */
                    json_arr_add(result, hl);
                }
            }
        }
    }
    send_response(s, id, result);
}

/* Brace pairs outside strings/comments become folding ranges; the closing
 * line stays visible (endLine = line of '}' minus one). */
static void handle_folding_range(lsp_server_t *s, json_value *id, json_value *params) {
    json_value *td = json_obj_get(params, "textDocument");
    const char *uri = json_get_str(json_obj_get(td, "uri"));
    lsp_doc_t *doc = uri ? lsp_find_doc(s, uri) : NULL;
    json_value *result = json_new_arr();
    if (doc && doc->text) {
        fmt_scan_t st = {false, false};
        int starts[1024];
        int sp = 0;
        const char *p = doc->text;
        int line = 0;
        while (*p) {
            const char *le = strchr(p, '\n');
            if (!le) le = p + strlen(p);
            for (const char *q = p; q < le; ) {
                char c = *q;
                if (st.in_block) {
                    if (c == '*' && q + 1 < le && q[1] == '/') { st.in_block = false; q += 2; }
                    else q++;
                    continue;
                }
                if (st.in_str) {
                    if (c == '\\') { q += 2; continue; }
                    if (c == '"') st.in_str = false;
                    q++;
                    continue;
                }
                if (c == '"') { st.in_str = true; q++; continue; }
                if (c == '/' && q + 1 < le && q[1] == '/') break;
                if (c == '/' && q + 1 < le && q[1] == '*') { st.in_block = true; q += 2; continue; }
                if (c == '{') {
                    if (sp < (int)(sizeof(starts) / sizeof(starts[0]))) starts[sp] = line;
                    if (sp < (int)(sizeof(starts) / sizeof(starts[0]))) sp++;
                } else if (c == '}') {
                    if (sp > 0) {
                        sp--;
                        int s0 = starts[sp];
                        if (line - 1 > s0) {
                            json_value *fr = json_new_obj();
                            json_obj_set(fr, "startLine", json_new_num(s0));
                            json_obj_set(fr, "endLine", json_new_num(line - 1));
                            json_arr_add(result, fr);
                        }
                    }
                }
                q++;
            }
            if (*le == '\n') { line++; p = le + 1; }
            else break;
        }
    }
    send_response(s, id, result);
}

/* ========================== prepareRename ============================ */

/* Prepare gives clients the exact range to rename plus a placeholder, and a
 * null reply for positions that hold no word (the actual rename stays a
 * textual whole-word edit until the binder's scopes back it). */
static void handle_prepare_rename(lsp_server_t *s, json_value *id, json_value *params) {
    const char *uri;
    int line, character;
    if (!get_position(params, &uri, &line, &character)) {
        send_response(s, id, json_new_null());
        return;
    }
    lsp_doc_t *doc = lsp_find_doc(s, uri);
    if (!doc) { send_response(s, id, json_new_null()); return; }

    size_t off = pos_to_offset(doc->text, line, character);
    size_t start = off;
    while (start > 0 && is_ident_char(doc->text[start - 1])) start--;
    size_t end = off;
    while (doc->text[end] && is_ident_char(doc->text[end])) end++;
    if (end == start) { send_response(s, id, json_new_null()); return; }

    int rl, rc;
    offset_to_linecol(doc->text, (int)start, &rl, &rc);
    char word[128];
    size_t n = end - start;
    if (n >= sizeof(word)) n = sizeof(word) - 1;
    memcpy(word, doc->text + start, n);
    word[n] = '\0';

    /* Only offer a range when the word resolves to something with a real
     * declaration: an implicit setter `value` (or any word no index knows)
     * has nothing to rename, and renaming it would be a lie. The decl-text
     * verification lives inside resolve_sym_target, so both paths share it. */
    intellisense_t *is = doc_intel_for(s, uri);
    sym_target_t t;
    bool renamable = resolve_sym_target(is, g_project_intel, doc->text, off,
                                        uri, word, line, character, &t);
    if (!renamable) { send_response(s, id, json_new_null()); return; }

    json_value *result = json_new_obj();
    json_value *range = json_new_obj();
    json_value *startp = json_new_obj();
    json_value *endp = json_new_obj();
    json_obj_set(startp, "line", json_new_num(rl));
    json_obj_set(startp, "character", json_new_num(rc));
    json_obj_set(endp, "line", json_new_num(rl));
    json_obj_set(endp, "character", json_new_num(rc + (int)n));
    json_obj_set(range, "start", startp);
    json_obj_set(range, "end", endp);
    json_obj_set(result, "range", range);
    json_obj_set(result, "placeholder", json_new_str(word));
    send_response(s, id, result);
}

/* Case-insensitive substring match (workspace/symbol query filter). */
static bool name_matches_query(const char *name, const char *query) {
    if (!query || !query[0]) return true;
    size_t qlen = strlen(query);
    for (const char *p = name; *p; p++) {
        size_t i = 0;
        while (i < qlen && p[i] &&
               tolower((unsigned char)p[i]) == tolower((unsigned char)query[i]))
            i++;
        if (i == qlen) return true;
    }
    return false;
}

/* workspace/symbol handler: query the project-wide index (built from the
 * workspace root on the first didOpen), filtered case-insensitively. */
static void handle_workspace_symbol(lsp_server_t *s, json_value *id, json_value *params) {
    const char *query = json_get_str(json_obj_get(params, "query"));
    ensure_project_indexed(s);

    json_value *arr = json_new_arr();
    intellisense_t *is = g_project_intel;
    int emitted = 0;
    if (is) {
        for (int i = 0; i < is->symbol_count && emitted < 256; i++) {
            isym_t *sym = &is->symbols[i];
            if (sym->kind == ISYM_VARIABLE || sym->kind == ISYM_PARAMETER) continue;
            if (!name_matches_query(sym->name, query)) continue;

            json_value *sinfo = json_new_obj();
            json_obj_set(sinfo, "name", json_new_str(sym->name));
            json_obj_set(sinfo, "kind", json_new_num(lsp_symbol_kind(sym->kind)));
            if (sym->parent[0])
                json_obj_set(sinfo, "containerName", json_new_str(sym->parent));

            char furi[600];
            if (strncmp(sym->file, "file://", 7) == 0)
                snprintf(furi, sizeof(furi), "%s", sym->file);
            else if (sym->file[0] == '/')
                snprintf(furi, sizeof(furi), "file://%s", sym->file);
            else
                snprintf(furi, sizeof(furi), "file:///%s", sym->file);

            json_value *loc = json_new_obj();
            json_obj_set(loc, "uri", json_new_str(furi));
            json_value *range = json_new_obj();
            json_value *start = json_new_obj();
            json_value *endp  = json_new_obj();
            json_obj_set(start, "line", json_new_num(sym->line));
            json_obj_set(start, "character", json_new_num(0));
            json_obj_set(endp, "line", json_new_num(sym->line));
            json_obj_set(endp, "character", json_new_num((int)strlen(sym->name)));
            json_obj_set(range, "start", start);
            json_obj_set(range, "end", endp);
            json_obj_set(loc, "range", range);
            json_obj_set(sinfo, "location", loc);
            json_arr_add(arr, sinfo);
            emitted++;
        }
    }
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

    if (is != g_project_intel) { intel_free(is); free(is); }
    send_response(s, id, actions);
}

/* Inlay hint handler */
static void handle_inlay_hint(lsp_server_t *s, json_value *id, json_value *params) {
    json_value *td = json_obj_get(params, "textDocument");
    const char *uri = json_get_str(json_obj_get(td, "uri"));
    if (!uri) { send_response(s, id, json_new_arr()); return; }

    lsp_doc_t *doc = lsp_find_doc(s, uri);
    if (!doc || !doc->text) { send_response(s, id, json_new_arr()); return; }

    intellisense_t *is = doc_intel_for(s, uri);
    intel_inlay_hint_t hints[256];
    int count = intel_collect_inlay_hints(is, doc->text, strlen(doc->text), hints, 256);

    json_value *arr = json_new_arr();
    for (int i = 0; i < count; i++) {
        json_value *item = json_new_obj();
        json_value *pos = json_new_obj();
        json_obj_set(pos, "line", json_new_num(hints[i].line));
        json_obj_set(pos, "character", json_new_num(hints[i].col));
        json_obj_set(item, "position", pos);
        json_obj_set(item, "label", json_new_str(hints[i].label));
        json_obj_set(item, "kind", json_new_num(hints[i].kind));
        if (hints[i].kind == 1) {
            json_obj_set(item, "paddingLeft", json_new_bool(true));
        } else if (hints[i].kind == 2) {
            json_obj_set(item, "paddingRight", json_new_bool(true));
        }
        json_arr_add(arr, item);
    }
    send_response(s, id, arr);
}

/* Semantic tokens full handler */
/* Legend index mappings:
 * 0: namespace, 1: type, 2: class, 3: enum, 4: interface, 5: struct,
 * 6: parameter, 7: variable, 8: property, 9: function, 10: method,
 * 11: keyword, 12: string, 13: number, 14: operator */
static int map_token_type(zan_token_kind_t tk) {
    switch (tk) {
    case TK_CLASS:
    case TK_STRUCT:
    case TK_INTERFACE:
    case TK_ENUM:
    case TK_NAMESPACE:
    case TK_USING:
    case TK_PUBLIC:
    case TK_PRIVATE:
    case TK_PROTECTED:
    case TK_INTERNAL:
    case TK_STATIC:
    case TK_READONLY:
    case TK_CONST:
    case TK_VIRTUAL:
    case TK_OVERRIDE:
    case TK_ABSTRACT:
    case TK_SEALED:
    case TK_ASYNC:
    case TK_AWAIT:
    case TK_IF:
    case TK_ELSE:
    case TK_WHILE:
    case TK_DO:
    case TK_FOR:
    case TK_FOREACH:
    case TK_IN:
    case TK_SWITCH:
    case TK_CASE:
    case TK_DEFAULT:
    case TK_BREAK:
    case TK_CONTINUE:
    case TK_RETURN:
    case TK_TRY:
    case TK_CATCH:
    case TK_FINALLY:
    case TK_THROW:
    case TK_NEW:
    case TK_THIS:
    case TK_BASE:
    case TK_TYPEOF:
    case TK_SIZEOF:
    case TK_IS:
    case TK_AS:
    case TK_NULL:
    case TK_TRUE:
    case TK_FALSE:
    case TK_OPERATOR:
    case TK_DELEGATE:
    case TK_LOCK:
    case TK_FIXED:
    case TK_UNSAFE:
    case TK_GOTO:
    case TK_WHEN:
    case TK_WHERE:
    case TK_DEFER:
    case TK_LET:
        return 11; /* keyword */

    case TK_INT:
    case TK_LONG:
    case TK_SHORT:
    case TK_BYTE:
    case TK_SBYTE:
    case TK_UINT:
    case TK_ULONG:
    case TK_USHORT:
    case TK_FLOAT:
    case TK_DOUBLE:
    case TK_DECIMAL:
    case TK_BOOL:
    case TK_CHAR:
    case TK_STRING:
    case TK_OBJECT:
    case TK_VOID:
    case TK_VAR:
    case TK_NINT:
        return 1; /* type */

    case TK_STRING_LIT:
    case TK_CHAR_LIT:
    case TK_INTERP_START:
    case TK_INTERP_MID:
    case TK_INTERP_END:
        return 12; /* string */

    case TK_INT_LIT:
    case TK_FLOAT_LIT:
        return 13; /* number */

    default:
        return -1;
    }
}

static void handle_semantic_tokens_full(lsp_server_t *s, json_value *id, json_value *params) {
    json_value *td = json_obj_get(params, "textDocument");
    const char *uri = json_get_str(json_obj_get(td, "uri"));
    if (!uri) {
        json_value *empty_res = json_new_obj();
        json_obj_set(empty_res, "data", json_new_arr());
        send_response(s, id, empty_res);
        return;
    }

    lsp_doc_t *doc = lsp_find_doc(s, uri);
    if (!doc || !doc->text) {
        json_value *empty_res = json_new_obj();
        json_obj_set(empty_res, "data", json_new_arr());
        send_response(s, id, empty_res);
        return;
    }

    intellisense_t *is = doc_intel_for(s, uri);

    /* Tokenize document with zan_lexer */
    zan_arena_t *arena = zan_arena_new();
    zan_diag_t *diag = zan_diag_new(arena);
    zan_diag_set_capture(diag, true);

    zan_lexer_t lex;
    zan_lexer_init(&lex, doc->text, strlen(doc->text), 0, arena, diag);

    /* Collect semantic tokens: array of integers (5 ints per token) */
    json_value *data = json_new_arr();
    int prev_line = 0;
    int prev_char = 0;

    zan_token_t tok;
    while ((tok = zan_lexer_next(&lex)).kind != TK_EOF && tok.kind != TK_INVALID) {
        int token_type = -1;
        int tlen = 0;

        if (tok.kind == TK_IDENT) {
            const char *ident_str = tok.str_val.str;
            tlen = (int)tok.str_val.len;

            /* Check against intellisense symbols */
            if (is) {
                for (int si = 0; si < is->symbol_count; si++) {
                    if ((int)strlen(is->symbols[si].name) == tlen &&
                        strncmp(is->symbols[si].name, ident_str, (size_t)tlen) == 0) {
                        isym_kind_t sk = is->symbols[si].kind;
                        if (sk == ISYM_CLASS) token_type = 2; /* class */
                        else if (sk == ISYM_STRUCT) token_type = 5; /* struct */
                        else if (sk == ISYM_ENUM) token_type = 3; /* enum */
                        else if (sk == ISYM_INTERFACE) token_type = 4; /* interface */
                        else if (sk == ISYM_METHOD) token_type = 10; /* method */
                        else if (sk == ISYM_PROPERTY) token_type = 8; /* property */
                        else if (sk == ISYM_PARAMETER) token_type = 6; /* parameter */
                        else if (sk == ISYM_VARIABLE) token_type = 7; /* variable */
                        else if (sk == ISYM_NAMESPACE) token_type = 0; /* namespace */
                        break;
                    }
                }
            }
            if (token_type == -1) {
                /* If not matched in symbols, check naming convention or default to variable */
                if (tlen > 0 && isupper((unsigned char)ident_str[0])) {
                    token_type = 1; /* type */
                } else {
                    token_type = 7; /* variable */
                }
            }
        } else {
            token_type = map_token_type(tok.kind);
            if (tok.kind == TK_STRING_LIT || tok.kind == TK_CHAR_LIT) {
                tlen = (int)tok.str_val.len;
            } else {
                /* For keywords/numbers, compute length from line text */
                const char *ls = line_start_at(doc->text, (int)tok.loc.line - 1);
                if (ls) {
                    const char *p = ls + (tok.loc.col - 1);
                    const char *pe = p;
                    while (*pe && !isspace((unsigned char)*pe) && *pe != ';' &&
                           *pe != '(' && *pe != ')' && *pe != '{' && *pe != '}' &&
                           *pe != '[' && *pe != ']' && *pe != ',' && *pe != '.') pe++;
                    tlen = (int)(pe - p);
                    if (tlen <= 0) tlen = 1;
                } else {
                    tlen = 1;
                }
            }
        }

        if (token_type >= 0 && tlen > 0) {
            int line = (int)tok.loc.line - 1;
            if (line < 0) line = 0;
            int col = byte_col_to_utf16_char(doc->text, line, (int)tok.loc.col);

            int delta_line = line - prev_line;
            int delta_start = (delta_line == 0) ? (col - prev_char) : col;

            json_arr_add(data, json_new_num(delta_line));
            json_arr_add(data, json_new_num(delta_start));
            json_arr_add(data, json_new_num(tlen));
            json_arr_add(data, json_new_num(token_type));
            json_arr_add(data, json_new_num(0)); /* tokenModifiers: none */

            prev_line = line;
            prev_char = col;
        }
    }

    zan_diag_free_buffers(diag);
    zan_arena_free(arena);

    json_value *result = json_new_obj();
    json_obj_set(result, "data", data);
    send_response(s, id, result);
}

/* ========================= leak checking ============================ */

/* Convert a file:  // URI to a native filesystem path. Delegates to
 * uri_to_native_path so the decode rules live in exactly one place. */
static void uri_to_fspath(const char *uri, char *out, size_t cap) {
    out[0] = '\0';
    if (!uri) return;
    uri_to_native_path(uri, out, cap);
}

/* Compile the given source with --check-leaks, run it, and publish any
 * reported leak sites as diagnostics. This surfaces the compiler's runtime
 * ARC leak report (objects still reachable at exit) inside the editor. */
static void run_leak_check(lsp_server_t *s, const char *uri) {
    char src[1024];
    uri_to_fspath(uri, src, sizeof(src));
    if (!src[0]) return;

    char zanc[1024];
    if (s->workspace_root[0]) {
#ifdef _WIN32
        snprintf(zanc, sizeof(zanc), "%s\\build\\zanc.exe", s->workspace_root);
#else
        snprintf(zanc, sizeof(zanc), "%s/build/zanc", s->workspace_root);
#endif
    } else {
        snprintf(zanc, sizeof(zanc), "zanc");
    }

    char out_exe[1100];
#ifdef _WIN32
    snprintf(out_exe, sizeof(out_exe), "%s.leakcheck.exe", src);
#else
    snprintf(out_exe, sizeof(out_exe), "%s.leakcheck", src);
#endif

    /* Redirect the compiler's own stdout/stderr to null: this server's stdout
     * carries the framed LSP protocol and must not be polluted. */
    /* On Windows, cmd /c strips the first and last quote of the whole command
     * line, so the entire command must be wrapped in an extra pair of quotes
     * to keep the individually-quoted paths intact. */
    char build_cmd[4096];
    snprintf(build_cmd, sizeof(build_cmd),
#ifdef _WIN32
             "\"\"%s\" \"%s\" -o \"%s\" --auto-stdlib --check-leaks >nul 2>&1\"",
#else
             "\"%s\" \"%s\" -o \"%s\" --auto-stdlib --check-leaks >/dev/null 2>&1",
#endif
             zanc, src, out_exe);

    json_value *arr = json_new_arr();
    char summary[256] = {0};

    if (system(build_cmd) == 0) {
        char run_cmd[1200];
        snprintf(run_cmd, sizeof(run_cmd),
#ifdef _WIN32
                 "\"\"%s\" 2>&1\"",
#else
                 "\"%s\" 2>&1",
#endif
                 out_exe);
#ifdef _WIN32
        FILE *fp = _popen(run_cmd, "r");
#else
        FILE *fp = popen(run_cmd, "r");
#endif
        if (fp) {
            char line[1024];
            while (fgets(line, sizeof(line), fp)) {
                if (strstr(line, "memory leak detected")) {
                    strncpy(summary, line, sizeof(summary) - 1);
                    char *nl = strchr(summary, '\n');
                    if (nl) *nl = '\0';
                    continue;
                }
                const char *at = strstr(line, "allocated at ");
                if (!at) continue;
                at += strlen("allocated at ");
                /* strip trailing newline */
                char loc[1024];
                strncpy(loc, at, sizeof(loc) - 1);
                loc[sizeof(loc) - 1] = '\0';
                char *nl = strpbrk(loc, "\r\n");
                if (nl) *nl = '\0';
                /* parse trailing :line:col from the right (path may contain ':') */
                char *c2 = strrchr(loc, ':');
                if (!c2) continue;
                *c2 = '\0';
                char *c1 = strrchr(loc, ':');
                if (!c1) continue;
                *c1 = '\0';
                int dline = atoi(c1 + 1);
                int dcol  = atoi(c2 + 1);
                if (dline < 1) dline = 1;
                if (dcol  < 1) dcol  = 1;

                json_value *d = json_new_obj();
                json_value *range = json_new_obj();
                json_value *start = json_new_obj();
                json_value *endp  = json_new_obj();
                json_obj_set(start, "line", json_new_num(dline - 1));
                json_obj_set(start, "character", json_new_num(dcol - 1));
                json_obj_set(endp, "line", json_new_num(dline - 1));
                json_obj_set(endp, "character", json_new_num(dcol));
                json_obj_set(range, "start", start);
                json_obj_set(range, "end", endp);
                json_obj_set(d, "range", range);
                json_obj_set(d, "severity", json_new_num(2)); /* Warning */
                json_obj_set(d, "source", json_new_str("zan-leakcheck"));
                json_obj_set(d, "message",
                    json_new_str("memory leak: object allocated here is still "
                                 "reachable at program exit (possible ARC cycle)"));
                json_arr_add(arr, d);
            }
#ifdef _WIN32
            _pclose(fp);
#else
            pclose(fp);
#endif
        }
        remove(out_exe);
    }

    /* publish (an empty array clears previous leak diagnostics) */
    json_value *params = json_new_obj();
    json_obj_set(params, "uri", json_new_str(uri));
    json_obj_set(params, "diagnostics", arr);
    json_value *note = json_new_obj();
    json_obj_set(note, "jsonrpc", json_new_str("2.0"));
    json_obj_set(note, "method", json_new_str("textDocument/publishDiagnostics"));
    json_obj_set(note, "params", params);
    char *payload = json_serialize(note);
    lsp_write(s, payload);
    free(payload);
    json_free(note);

    if (summary[0]) {
        json_value *mparams = json_new_obj();
        json_obj_set(mparams, "type", json_new_num(2)); /* Warning */
        json_obj_set(mparams, "message", json_new_str(summary));
        json_value *mnote = json_new_obj();
        json_obj_set(mnote, "jsonrpc", json_new_str("2.0"));
        json_obj_set(mnote, "method", json_new_str("window/showMessage"));
        json_obj_set(mnote, "params", mparams);
        char *mp = json_serialize(mnote);
        lsp_write(s, mp);
        free(mp);
        json_free(mnote);
    }
}

static void handle_execute_command(lsp_server_t *s, json_value *id, json_value *params) {
    const char *command = json_get_str(json_obj_get(params, "command"));
    if (command && strcmp(command, "zan.checkLeaks") == 0) {
        json_value *args = json_obj_get(params, "arguments");
        const char *uri = NULL;
        if (args && json_arr_count(args) > 0)
            uri = json_get_str(json_arr_at(args, 0));
        if (uri) run_leak_check(s, uri);
    }
    send_response(s, id, json_new_null());
}

/* ============================ dispatch =============================== */

static void dispatch(lsp_server_t *s, json_value *msg) {
    const char *method = json_get_str(json_obj_get(msg, "method"));
    json_value *id = json_obj_get(msg, "id");
    json_value *params = json_obj_get(msg, "params");
    if (!method) return;

    /* Handlers read/mutate the doc store and g_project_intel; the
     * diagnostics worker only touches the store to snapshot text. */
    lsp_doc_lock(s);
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
    } else if (strcmp(method, "textDocument/rename") == 0) {
        handle_rename(s, id, params);
    } else if (strcmp(method, "textDocument/prepareRename") == 0) {
        handle_prepare_rename(s, id, params);
    } else if (strcmp(method, "textDocument/documentHighlight") == 0) {
        handle_document_highlight(s, id, params);
    } else if (strcmp(method, "textDocument/foldingRange") == 0) {
        handle_folding_range(s, id, params);
    } else if (strcmp(method, "textDocument/formatting") == 0) {
        handle_formatting(s, id, params);
    } else if (strcmp(method, "textDocument/rangeFormatting") == 0) {
        handle_range_formatting(s, id, params);
    } else if (strcmp(method, "workspace/symbol") == 0) {
        handle_workspace_symbol(s, id, params);
    } else if (strcmp(method, "textDocument/codeAction") == 0) {
        handle_code_action(s, id, params);
    } else if (strcmp(method, "textDocument/inlayHint") == 0) {
        handle_inlay_hint(s, id, params);
    } else if (strcmp(method, "textDocument/semanticTokens/full") == 0) {
        handle_semantic_tokens_full(s, id, params);
    } else if (strcmp(method, "workspace/executeCommand") == 0) {
        handle_execute_command(s, id, params);
    } else if (strcmp(method, "shutdown") == 0) {
        s->shutdown_requested = true;
        send_response(s, id, json_new_null());
    } else if (strcmp(method, "exit") == 0) {
        /* handled by caller loop */
    } else if (id) {
        /* unknown request: reply null so the client isn't left hanging */
        send_response(s, id, json_new_null());
    }
    lsp_doc_unlock(s);
}

/* ---- request queue + $/cancelRequest (reader/worker split) ---- */

#define LSP_Q_MAX 1024

/* q_lock never nests inside a dispatched handler's doc_lock except from the
 * worker's cancellation checkpoints (doc_lock -> q_lock, one direction only);
 * the reader touches nothing but q_lock, so it never blocks on a request. */
static void lsp_q_lock(lsp_server_t *s) {
#ifdef _WIN32
    EnterCriticalSection(&s->q_lock);
#else
    pthread_mutex_lock(&s->q_lock);
#endif
}

static void lsp_q_unlock(lsp_server_t *s) {
#ifdef _WIN32
    LeaveCriticalSection(&s->q_lock);
#else
    pthread_mutex_unlock(&s->q_lock);
#endif
}

/* Reader side: enqueue one parsed message. On overflow a request is refused
 * (caller replies error and frees); the msg==NULL shutdown sentinel is always
 * enqueued or the worker would never stop. Takes ownership only on success. */
static bool q_push(lsp_server_t *s, json_value *msg) {
    lsp_msg_t *n = (lsp_msg_t *)malloc(sizeof(*n));
    if (!n) return false;
    n->next = NULL;
    n->msg = msg;
    bool ok = true;
    lsp_q_lock(s);
    if (msg != NULL && s->q_len >= LSP_Q_MAX) {
        ok = false;
    } else {
        if (s->q_tail) s->q_tail->next = n;
        else s->q_head = n;
        s->q_tail = n;
        s->q_len++;
    }
    lsp_q_unlock(s);
    if (!ok) free(n);
    return ok;
}

/* Worker side: dequeue in FIFO order; blocks (poll) while empty. Returns
 * nodes only; the shutdown sentinel is a node with msg == NULL. */
static lsp_msg_t *q_pop(lsp_server_t *s) {
    for (;;) {
        lsp_msg_t *n = NULL;
        lsp_q_lock(s);
        if (s->q_head) {
            n = s->q_head;
            s->q_head = n->next;
            if (!s->q_head) s->q_tail = NULL;
            s->q_len--;
        }
        lsp_q_unlock(s);
        if (n) return n;
        lsp_sleep_ms(2);
    }
}

static bool cancel_matches_locked(lsp_server_t *s);

/* Reader side: record a $/cancelRequest id; if the cancelled request is the
 * one executing right now, also raise the deep-scan abort flag so an
 * in-flight project index walk bails at its next checkpoint. A queued target
 * must NOT raise the flag -- that would abort whatever request is executing,
 * so it is simply refused when dequeued. */
static void q_set_cancel(lsp_server_t *s, json_value *id) {
    if (!id) return;
    lsp_q_lock(s);
    if (id->type == JSON_NUM) {
        s->cancel_valid = true;
        s->cancel_is_str = false;
        s->cancel_num = id->as.num;
    } else if (id->type == JSON_STR && id->as.str &&
               strlen(id->as.str) < sizeof(s->cancel_str)) {
        s->cancel_valid = true;
        s->cancel_is_str = true;
        snprintf(s->cancel_str, sizeof(s->cancel_str), "%s", id->as.str);
    }
    if (cancel_matches_locked(s)) intel_cancel_flag = 1;
    lsp_q_unlock(s);
}

static bool cancel_matches_locked(lsp_server_t *s) {
    if (!s->cancel_valid || !s->exec_valid) return false;
    if (s->cancel_is_str != s->exec_is_str) return false;
    if (s->cancel_is_str)
        return strcmp(s->cancel_str, s->exec_str) == 0;
    return s->cancel_num == s->exec_num;
}

/* Checkpoint for long-running handlers (project file walks): true when the
 * request currently being executed has been cancelled. */
static bool lsp_cancel_hit(lsp_server_t *s) {
    lsp_q_lock(s);
    bool hit = cancel_matches_locked(s);
    lsp_q_unlock(s);
    if (hit) intel_cancel_flag = 1;
    return hit;
}

/* Worker side, before dispatch: if the queued request is already cancelled,
 * consume the cancel slot and report true (caller replies -32800). */
static bool lsp_take_cancel(lsp_server_t *s, json_value *id) {
    bool hit = false;
    lsp_q_lock(s);
    s->exec_valid = false;
    if (id && id->type == JSON_NUM) {
        s->exec_valid = true;
        s->exec_is_str = false;
        s->exec_num = id->as.num;
    } else if (id && id->type == JSON_STR && id->as.str &&
               strlen(id->as.str) < sizeof(s->exec_str)) {
        s->exec_valid = true;
        s->exec_is_str = true;
        snprintf(s->exec_str, sizeof(s->exec_str), "%s", id->as.str);
    }
    if (cancel_matches_locked(s)) {
        hit = true;
        s->cancel_valid = false;
    }
    /* A non-matching slot stays recorded: its target may still be queued
     * behind this request (the reader runs ahead of the worker), so it must
     * be refused when dequeued. Ids are unique per session, so a lingering
     * slot can never hit an unrelated request. */
    lsp_q_unlock(s);
    intel_cancel_flag = 0; /* flags are per-request; stale aborts must not
                              kill the next request's project scan */
    return hit;
}

/* Worker side, after dispatch: drop the exec record and the abort flag, but
 * keep a pending cancel slot -- its target may still be queued behind this
 * request and must be refused when dequeued. */
static void lsp_clear_exec(lsp_server_t *s) {
    lsp_q_lock(s);
    s->exec_valid = false;
    lsp_q_unlock(s);
    intel_cancel_flag = 0;
}

static void lsp_worker_loop(lsp_server_t *s);

#ifdef _WIN32
static DWORD WINAPI lsp_worker_main(LPVOID arg) {
    lsp_worker_loop((lsp_server_t *)arg);
    return 0;
}
#else
static void *lsp_worker_main(void *arg) {
    lsp_worker_loop((lsp_server_t *)arg);
    return NULL;
}
#endif

/* Executes queued messages one at a time (FIFO, same ordering as the old
 * serial read loop). The `exit` sentinel (msg == NULL) ends the loop. */
static void lsp_worker_loop(lsp_server_t *s) {
    for (;;) {
        lsp_msg_t *m = q_pop(s);
        if (!m) break; /* unreachable: q_pop only returns nodes; sentinel has
                          msg == NULL and is handled below */
        json_value *msg = m->msg;
        free(m);
        if (!msg) break; /* sentinel */
        const char *method = json_get_str(json_obj_get(msg, "method"));
        json_value *id = json_obj_get(msg, "id");
        if (method && strcmp(method, "exit") == 0) {
            json_free(msg);
            break;
        }
        if (id && lsp_take_cancel(s, id)) {
            send_response_error(s, id, -32800, "Request cancelled");
            lsp_clear_exec(s);
            json_free(msg);
            continue;
        }
        dispatch(s, msg);
        if (id) lsp_clear_exec(s);
        json_free(msg);
    }
}

int main(int argc, char **argv) {
    int port = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--port") == 0 && i + 1 < argc)
            port = atoi(argv[++i]);
    }

    lsp_server_t server;
    memset(&server, 0, sizeof(server));
    server.out = stdout;
    lsp_locks_init(&server);

#ifdef _WIN32
    server.diag_thread = CreateThread(NULL, 0, diag_worker_main, &server, 0, NULL);
#else
    server.diag_thread_valid =
        pthread_create(&server.diag_thread, NULL, diag_worker_main, &server) == 0;
#endif
#ifdef _WIN32
    server.worker_thread = CreateThread(NULL, 0, lsp_worker_main, &server, 0, NULL);
#else
    server.worker_thread_valid =
        pthread_create(&server.worker_thread, NULL, lsp_worker_main, &server) == 0;
#endif

    if (port > 0) {
        server.sock = lsp_listen_accept(port);
        if (server.sock == LSP_INVALID_SOCK) {
            fprintf(stderr, "zan-lsp: failed to listen on port %d\n", port);
            return 1;
        }
        server.use_sock = true;
    } else {
#ifdef _WIN32
        /* avoid CRLF translation mangling the framed protocol */
        _setmode(_fileno(stdin), _O_BINARY);
        _setmode(_fileno(stdout), _O_BINARY);
#endif
    }

    /* Reader loop: parse frames and enqueue; never executes handlers, so a
     * slow request cannot keep $/cancelRequest (or exit) from being read. */
    for (;;) {
        char *body = server.use_sock ? rpc_read_message_sock(server.sock)
                                     : rpc_read_message(stdin);
        if (!body) break; /* EOF */

        json_value *msg = json_parse(body);
        free(body);
        if (!msg) continue;

        const char *method = json_get_str(json_obj_get(msg, "method"));
        if (method && strcmp(method, "$/cancelRequest") == 0) {
            q_set_cancel(&server, json_obj_get(json_obj_get(msg, "params"), "id"));
            json_free(msg);
            continue;
        }
        bool is_exit = method && strcmp(method, "exit") == 0;
        if (!q_push(&server, msg)) {
            json_value *id = json_obj_get(msg, "id");
            if (id)
                send_response_error(&server, id, -32000,
                                    "server queue full");
            json_free(msg);
        }
        if (is_exit) break;
    }

    /* Tell the worker to drain what is queued and stop (the sentinel keeps
     * FIFO order: requests read before exit/EOF still run). The sentinel
     * bypasses the queue cap in q_push; only OOM can refuse it, so retry. */
    while (!q_push(&server, NULL)) lsp_sleep_ms(10);

#ifdef _WIN32
    if (server.worker_thread) {
        WaitForSingleObject(server.worker_thread, INFINITE);
        CloseHandle(server.worker_thread);
    }
#else
    if (server.worker_thread_valid)
        pthread_join(server.worker_thread, NULL);
#endif

    /* Stop the diagnostics worker before tearing the doc store down, and
     * flush any diagnostics still pending for open documents. */
    server.diag_stop = true;
#ifdef _WIN32
    if (server.diag_thread) {
        WaitForSingleObject(server.diag_thread, INFINITE);
        CloseHandle(server.diag_thread);
    }
#else
    if (server.diag_thread_valid)
        pthread_join(server.diag_thread, NULL);
#endif
    lsp_locks_free(&server);

    for (int i = 0; i < server.doc_count; i++) {
        free(server.docs[i].uri);
        free(server.docs[i].text);
    }
#ifdef _WIN32
    if (server.use_sock) { closesocket(server.sock); WSACleanup(); }
#else
    if (server.use_sock) close(server.sock);
#endif
    return server.shutdown_requested ? 0 : 0;
}
