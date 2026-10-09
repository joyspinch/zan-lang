/* 内部辅助实现 */
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
/* 核心系统底层抽象与内存语义契约 */

#define LSP_MAX_DOCS 256

/* 内部辅助逻辑 */
#define LSP_DIAG_QUIET_MS 200

/* 内部辅助逻辑 */
typedef struct lsp_msg {
    struct lsp_msg *next;
    json_value *msg;
} lsp_msg_t;

typedef struct {
    char *uri;
    char *text;
    long    version;         /* 核心系统底层抽象与内存语义契约 */
    bool    diag_pending;    /* 底层系统交互与数据协议契约 */
    uint64_t last_change_ms; /* 核心系统底层抽象与内存语义契约 */
} lsp_doc_t;

typedef struct {
    lsp_doc_t docs[LSP_MAX_DOCS];
    int       doc_count;
    bool      shutdown_requested;
    bool      project_indexed;
    char      workspace_root[1024];
    FILE     *out;
    bool       use_sock;  /* 底层系统交互与数据协议契约 */
    lsp_sock_t sock;      /* 核心系统底层抽象与内存语义契约 */
    /* 内部辅助实现 */
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
    /* 内部辅助逻辑 */
#ifdef _WIN32
    CRITICAL_SECTION q_lock;
    HANDLE worker_thread;
#else
    pthread_mutex_t q_lock;
    pthread_t worker_thread;
    bool worker_thread_valid;
#endif
    lsp_msg_t *q_head;          /* 核心系统底层抽象与内存语义契约 */
    lsp_msg_t *q_tail;
    int q_len;
    /* 模块核心语义抽象与接口调用契约 */
    bool cancel_valid;
    bool cancel_is_str;
    double cancel_num;
    char cancel_str[80];
    /* 模块核心语义抽象与接口调用契约 */
    bool exec_valid;
    bool exec_is_str;
    double exec_num;
    char exec_str[80];
} lsp_server_t;

/* 模块核心语义抽象与接口调用契约 */

static bool sock_send_all(lsp_sock_t s, const char *buf, int n) {
    int off = 0;
    while (off < n) {
        int r = (int)send(s, buf + off, n - off, 0);
        if (r <= 0) return false;
        off += r;
    }
    return true;
}

/* 内部辅助实现 */
#ifndef LSP_FRAME_DEADLINE_MS
#define LSP_FRAME_DEADLINE_MS 60000
#endif

typedef struct {
    lsp_sock_t s;
    bool started;        /* 核心系统底层抽象与内存语义契约 */
    uint64_t deadline;   /* 底层系统交互与数据协议契约 */
} lsp_frame_reader_t;

static uint64_t lsp_now_ms(void);   /* 核心系统底层抽象与内存语义契约 */

static int sock_reader(void *ctx, char *buf, int n) {
    lsp_frame_reader_t *r = (lsp_frame_reader_t *)ctx;
    if (r->started) {
        /* 内部辅助逻辑 */
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
    static lsp_frame_reader_t r;   /* 核心系统底层抽象与内存语义契约 */
    r.s = s;
    r.started = false;             /* 底层系统交互与数据协议契约 */
    return rpc_read_message_cb(sock_reader, &r, 64 * 1024 * 1024);
}

static void rpc_write_message_sock(lsp_sock_t s, const char *payload) {
    rpc_write_message_cb(sock_writer, &s, payload);
}

/* 内部辅助逻辑 */
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

/* Listen on 127 */
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

/* 核心系统底层抽象与内存语义契约 */

/* 内部辅助逻辑 */
static void utf8_seq_info(unsigned char c, int *bytes, int *utf16_units) {
    if (c < 0x80)                 { *bytes = 1; *utf16_units = 1; }
    else if ((c & 0xE0) == 0xC0)  { *bytes = 2; *utf16_units = 1; }
    else if ((c & 0xF0) == 0xE0)  { *bytes = 3; *utf16_units = 1; }
    else if ((c & 0xF8) == 0xF0)  { *bytes = 4; *utf16_units = 2; }
    else                          { *bytes = 1; *utf16_units = 1; }
}

/* 模块核心语义抽象与接口调用契约 */
static int utf16_units_range(const char *start, const char *end) {
    int units = 0;
    while (start < end) {
        int bytes, cu;
        utf8_seq_info((unsigned char)*start, &bytes, &cu);
        if (start + bytes > end) break; /* 核心系统底层抽象与内存语义契约 */
        units += cu;
        start += bytes;
    }
    return units;
}

/* 内部辅助逻辑 */
static int byte_col_to_utf16_char(const char *text, int line0, int byte_col1) {
    const char *ls = text;
    int ln = 0;
    while (ln < line0 && *ls) {
        if (*ls == '\n') ln++;
        ls++;
    }
    if (byte_col1 <= 1) return 0;
    /* 内部辅助逻辑 */
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

/* 内部辅助逻辑 */
static size_t pos_to_offset(const char *text, int line, int character) {
    size_t off = 0;
    int cur_line = 0;
    while (text[off] && cur_line < line) {
        if (text[off] == '\n') cur_line++;
        off++;
    }
    /* 内部辅助逻辑 */
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

/* 模块核心语义抽象与接口调用契约 */
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

/* 底层系统交互与数据协议契约 */
static int ident_len_at(const char *s) {
    int n = 0;
    while (is_ident_char(s[n])) n++;
    return n;
}

/* 内部辅助逻辑 */
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

/* 模块核心语义抽象与接口调用契约 */
static void prefix_before(const char *text, size_t offset, char *out, size_t cap) {
    size_t start = offset;
    while (start > 0 && is_ident_char(text[start - 1])) start--;
    size_t n = offset - start;
    if (n >= cap) n = cap - 1;
    memcpy(out, text + start, n);
    out[n] = '\0';
}

/* 内部辅助逻辑 */
static void offset_to_linecol(const char *text, int offset, int *line, int *character) {
    int ln = 0, col = 0, i = 0;
    while (i < offset && text[i]) {
        unsigned char c = (unsigned char)text[i];
        if (c == '\n') { ln++; col = 0; i++; continue; }
        int bytes, cu;
        utf8_seq_info(c, &bytes, &cu);
        if (i + bytes > offset) {
            /* 内部辅助逻辑 */
            col += cu;
            break;
        }
        col += cu;
        i += bytes;
    }
    *line = ln;
    *character = col;
}

/* 底层系统交互与数据协议契约 */
static void extract_chain_expr(const char *text, size_t offset,
                               char *chain_out, size_t chain_cap) {
    chain_out[0] = '\0';
    /* 内部辅助逻辑 */
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
            /* 内部辅助逻辑 */
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
            /* 模块核心语义抽象与接口调用契约 */
            char q = ch;
            start--;
            while (start > 0 && text[start - 1] != q) start--;
            if (start > 0) start--;
        } else if ((is_ident_char(ch) || ch == '_') && paren_depth == 0 && angle_depth == 0) {
            start--;
        } else if (paren_depth > 0 || angle_depth > 0 || bracket_depth > 0) {
            /* 底层系统交互与数据协议契约 */
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

/* 核心系统底层抽象与内存语义契约 */
/* 内部辅助实现 */
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
        /* 模块核心语义抽象与接口调用契约 */
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
    if (rlen < 5 || m < 5) return false;   /* 核心系统底层抽象与内存语义契约 */

    size_t p = 5;
    while (p < rlen && (rest[p] == ' ' || rest[p] == '\t')) p++;
    if (p == rlen) { out[0] = '\0'; return true; }

    size_t w0 = p, w1 = p;
    while (w1 < rlen && (isalnum((unsigned char)rest[w1]) ||
                         rest[w1] == '.' || rest[w1] == '_')) w1++;
    if (w1 != rlen) return false;          /* 核心系统底层抽象与内存语义契约 */
    size_t n = w1 - w0 < cap - 1 ? w1 - w0 : cap - 1;
    memcpy(out, rest + w0, n);
    out[n] = '\0';
    return true;
}

/* 内部辅助逻辑 */
static bool receiver_chain_before(const char *text, size_t offset,
                                  char *out, size_t cap) {
    size_t start = offset;
    while (start > 0 && is_ident_char(text[start - 1])) start--;
    if (start == 0 || text[start - 1] != '.') return false;
    size_t end = start - 1; /* 核心系统底层抽象与内存语义契约 */
    size_t from = end;
    for (;;) {
        while (from > 0) {
            char c = text[from - 1];
            if (isalnum((unsigned char)c) || c == '_' || c == '.' ||
                c == '<' || c == '>' || c == '?') from--;
            else break;
        }
        if (from > 0 && (text[from - 1] == '"' || text[from - 1] == '\'')) {
            /* 内部辅助逻辑 */
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

    /* 核心系统底层抽象与内存语义契约 */
    char chain_expr[1024];
    extract_chain_expr(text, offset, chain_expr, sizeof(chain_expr));

    /* 内部辅助逻辑 */
    if (chain_expr[0] == '"' || chain_expr[0] == '\'' ||
        (chain_expr[0] >= '0' && chain_expr[0] <= '9')) {
        if (strlen(chain_expr) + 7 < cap) {
            snprintf(out, cap, "CHAIN:%s", chain_expr);
            return;
        }
    }

    /* 核心系统底层抽象与内存语义契约 */
    int dot_count = 0;
    int pd = 0;
    for (int i = 0; chain_expr[i]; i++) {
        if (chain_expr[i] == '(') pd++;
        else if (chain_expr[i] == ')') { if (pd > 0) pd--; }
        else if (chain_expr[i] == '.' && pd == 0) dot_count++;
    }

    if (dot_count > 1) {
        /* 模块核心语义抽象与接口调用契约 */
        /* 模块核心语义抽象与接口调用契约 */
        if (strlen(chain_expr) + 7 < cap) {
            snprintf(out, cap, "CHAIN:%s", chain_expr);
            return;
        }
    }

    /* 模块核心语义抽象与接口调用契约 */
    size_t dot = start - 1;
    size_t obj_end = dot;
    size_t obj_start = obj_end;
    /* 模块核心语义抽象与接口调用契约 */
    if (obj_start > 0 && text[obj_start - 1] == ')') {
        int pdepth = 1;
        obj_start--;
        while (obj_start > 0 && pdepth > 0) {
            obj_start--;
            if (text[obj_start] == ')') pdepth++;
            else if (text[obj_start] == '(') pdepth--;
        }
        /* 底层系统交互与数据协议契约 */
        obj_end = obj_start;
        obj_start = obj_end;
    }
    while (obj_start > 0 && is_ident_char(text[obj_start - 1])) obj_start--;
    size_t n = obj_end - obj_start;
    if (n == 0 && chain_expr[0]) {
        /* 内部辅助逻辑 */
        if (strlen(chain_expr) + 7 < cap) {
            snprintf(out, cap, "CHAIN:%s", chain_expr);
            return;
        }
    }
    if (n >= cap) n = cap - 1;
    memcpy(out, text + obj_start, n);
    out[n] = '\0';
}

/* 内部辅助逻辑 */
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

    /* 模块核心语义抽象与接口调用契约 */
    while (i > 0) {
        i--;
        if (text[i] == ')') paren_depth++;
        else if (text[i] == '(') {
            if (paren_depth == 0) { found = true; break; }
            paren_depth--;
        }
    }
    if (!found) return;

    /* 内部辅助逻辑 */
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

    /* 核心系统底层抽象与内存语义契约 */
    size_t end = i;
    while (end > 0 && (text[end - 1] == ' ' || text[end - 1] == '\t')) end--;
    size_t name_end = end;
    while (end > 0 && is_ident_char(text[end - 1])) end--;
    size_t n = name_end - end;
    if (n >= method_cap) n = method_cap - 1;
    memcpy(method_out, text + end, n);
    method_out[n] = '\0';

    /* 核心系统底层抽象与内存语义契约 */
    if (end > 0 && text[end - 1] == '.') {
        /* 内部辅助逻辑 */
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

/* diagnostics */

/* 核心系统底层抽象与内存语义契约 */
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

/* 模块核心语义抽象与接口调用契约 */
static void publish_diagnostics(lsp_server_t *s, const char *uri, const char *text) {
    size_t ul = strlen(uri);
    if ((ul > 5 && strcmp(uri + ul - 5, ".html") == 0) ||
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

    /* 内部辅助逻辑 */
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
        /* e->loc */
        int col  = byte_col_to_utf16_char(text, line, (int)e->loc.col);
        /* 内部辅助逻辑 */
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
        /* 底层系统交互与数据协议契约 */
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

/* responses */

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

/* 内部辅助逻辑 */
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

/* 核心系统底层抽象与内存语义契约 */
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

/* 底层系统交互与数据协议契约 */
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

/* handlers */

static void ensure_project_indexed(lsp_server_t *s);

/* 模块核心语义抽象与接口调用契约 */
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

/* 内部辅助实现 */
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

/* 内部辅助逻辑 */
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
    /* 底层系统交互与数据协议契约 */
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
    /* 模块核心语义抽象与接口调用契约 */
    json_value *sync = json_new_obj();
    json_obj_set(sync, "openClose", json_new_bool(true));
    json_obj_set(sync, "change", json_new_num(2)); /* incremental */
    json_obj_set(caps, "textDocumentSync", sync);

    /* 核心系统底层抽象与内存语义契约 */
    json_value *completion = json_new_obj();
    json_value *triggers = json_new_arr();
    json_arr_add(triggers, json_new_str("."));
    json_arr_add(triggers, json_new_str("<"));
    json_obj_set(completion, "triggerCharacters", triggers);
    json_obj_set(completion, "resolveProvider", json_new_bool(false));
    json_obj_set(caps, "completionProvider", completion);

    /* 核心系统底层抽象与内存语义契约 */
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
    /* 内部辅助逻辑 */
    json_value *rename = json_new_obj();
    json_obj_set(rename, "prepareProvider", json_new_bool(true));
    json_obj_set(caps, "renameProvider", rename);
    json_obj_set(caps, "documentHighlightProvider", json_new_bool(true));
    json_obj_set(caps, "foldingRangeProvider", json_new_bool(true));
    json_obj_set(caps, "documentFormattingProvider", json_new_bool(true));
    json_obj_set(caps, "documentRangeFormattingProvider", json_new_bool(true));

    /* 核心系统底层抽象与内存语义契约 */
    json_value *code_action = json_new_obj();
    json_value *ca_kinds = json_new_arr();
    json_arr_add(ca_kinds, json_new_str("source.organizeImports"));
    json_obj_set(code_action, "codeActionKinds", ca_kinds);
    json_obj_set(caps, "codeActionProvider", code_action);

    /* 模块核心语义抽象与接口调用契约 */
    json_value *inlay_hint = json_new_obj();
    json_obj_set(inlay_hint, "resolveProvider", json_new_bool(false));
    json_obj_set(caps, "inlayHintProvider", inlay_hint);

    /* 核心系统底层抽象与内存语义契约 */
    {
        json_value *sem_tokens = json_new_obj();
        json_value *legend = json_new_obj();
        json_value *token_types = json_new_arr();
        /* 内部辅助实现 */
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

    /* 核心系统底层抽象与内存语义契约 */
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

/* 模块核心语义抽象与接口调用契约 */
static intellisense_t *g_project_intel = NULL;

/* 内部辅助实现 */
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

/* 内部辅助逻辑 */
static void ensure_stdlib_indexed(lsp_server_t *s) {
    static bool stdlib_indexed = false;
    if (stdlib_indexed) { return; }
    stdlib_indexed = true;
    if (!g_project_intel) { return; }
    char root[1024];
    if (lsp_stdlib_root(root, sizeof(root))) {
        intel_index_project(g_project_intel, root);
    }

    /* 模块核心语义抽象与接口调用契约 */
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
    /* 内部辅助实现 */
    if (s->workspace_root[0]) {
        intel_index_project(g_project_intel, s->workspace_root);
        if (intel_cancel_flag) {
            /* 内部辅助逻辑 */
            s->project_indexed = false;
            return;
        }
    }
    ensure_stdlib_indexed(s);
}

/* 内部辅助逻辑 */
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

    /* 底层系统交互与数据协议契约 */
    json_value *ver_j = json_obj_get(td, "version");
    int over = ver_j ? (int)json_get_num(ver_j, 0) : 0;
    lsp_doc_t *d = lsp_find_doc(s, uri);
    if (d && over > 0) d->version = over;

    /* 底层系统交互与数据协议契约 */
    ensure_project_indexed(s);
    update_project_index(s, uri, text);

    publish_diagnostics(s, uri, text);
}

/* 内部辅助逻辑 */
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

    /* 内部辅助实现 */
    json_value *ver_j = json_obj_get(td, "version");
    int incoming = ver_j ? (int)json_get_num(ver_j, 0) : 0;

    lsp_doc_t *d = lsp_find_doc(s, uri);
    if (!d) {
        /* 内部辅助逻辑 */
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
        /* 模块核心语义抽象与接口调用契约 */
        for (int i = 0; i < n; i++)
            lsp_doc_apply_change(d, json_arr_at(changes, i));
    }

    d->version = incoming > 0 ? incoming : d->version + 1;
    d->last_change_ms = lsp_now_ms();
    d->diag_pending = true;

    /* 内部辅助逻辑 */
    update_project_index(s, uri, d->text);
}

/* 核心系统底层抽象与内存语义契约 */
static intellisense_t *g_doc_intel = NULL;
static char g_doc_intel_uri[512] = "";
static long g_doc_intel_version = -1;

/* 内部辅助逻辑 */
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
    /* 核心系统底层抽象与内存语义契约 */
    publish_diagnostics(s, uri, "");
}

/* 核心系统底层抽象与内存语义契约 */

/* 内部辅助实现 */
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

/* 底层系统交互与数据协议契约 */
static bool get_position(json_value *params, const char **uri,
                         int *line, int *character) {
    json_value *td = json_obj_get(params, "textDocument");
    json_value *pos = json_obj_get(params, "position");
    *uri = json_get_str(json_obj_get(td, "uri"));
    *line = (int)json_get_num(json_obj_get(pos, "line"), 0);
    *character = (int)json_get_num(json_obj_get(pos, "character"), 0);
    return *uri != NULL;
}

/* 核心系统底层抽象与内存语义契约 */
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
    /* 内部辅助逻辑 */
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

    /* 底层系统交互与数据协议契约 */
    intellisense_t *is = doc_intel_for(s, uri);
    if (!is) { send_response(s, id, json_new_arr()); return; }

    const char *effective = prefix[0] ? prefix : "";
    int count = 0;

    /* 模块核心语义抽象与接口调用契约 */
    char ns_prefix[128];
    bool ns_mode = using_ns_context(doc->text, off, ns_prefix, sizeof(ns_prefix));
    if (ns_mode) {
        count = intel_complete_usings(is, g_project_intel, ns_prefix);
    }

    /* 底层系统交互与数据协议契约 */
    if (!ns_mode && context[0]) {
        const char *resolve_type = context;
        char chain_prefix[128] = "";

        /* 核心系统底层抽象与内存语义契约 */
        if (strncmp(context, "CHAIN:", 6) == 0) {
            const char *chain_expr = context + 6;
            /* 语言服务与调试协议交互规范 */
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
                /* 底层系统交互与数据协议契约 */
                resolve_type = "";
            }
        }

        if (resolve_type[0]) {
            /* 内部辅助逻辑 */
            count = intel_complete_members_pos(is, resolve_type, effective,
                                               line, character);
            if (count == 0 && strncmp(context, "CHAIN:", 6) != 0) {
                /* 模块核心语义抽象与接口调用契约 */
                count = complete_var_init_members(doc, is, context, line,
                                                  character, effective);
            }
            /* 模块核心语义抽象与接口调用契约 */
            if (g_project_intel) {
                /* 内部辅助逻辑 */
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
        /* 内部辅助逻辑 */
        count = intel_complete_pos(is, effective, NULL, line, character);
        /* 模块核心语义抽象与接口调用契约 */
        if (g_project_intel && count < 20) {
            const char *encl = intel_enclosing_type_at(is, line, character);
            int proj_count = intel_complete_bare(g_project_intel, effective, encl);
            /* 模块核心语义抽象与接口调用契约 */
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
                    /* 内部辅助逻辑 */
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

        /* 核心系统底层抽象与内存语义契约 */
        if (c->doc[0]) {
            json_value *doc_obj = json_new_obj();
            json_obj_set(doc_obj, "kind", json_new_str("markdown"));
            json_obj_set(doc_obj, "value", json_new_str(c->doc));
            json_obj_set(item, "documentation", doc_obj);
        }

        /* 核心系统底层抽象与内存语义契约 */
        if (c->kind == ISYM_SNIPPET) {
            json_obj_set(item, "insertTextFormat", json_new_num(2));
        }

        /* 核心系统底层抽象与内存语义契约 */
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
    /* 内部辅助实现 */
    hover_info_t h = {0};
    char chain[256];
    if (receiver_chain_before(doc->text, off, chain, sizeof(chain))) {
        char fm[64];
        const char *rt = intel_resolve_chain_pos(is, g_project_intel, chain,
                                                 fm, sizeof(fm), line, character);
        if (!rt || !rt[0]) {
            /* 内部辅助逻辑 */
            rt = intel_resolve_chain_pos(g_project_intel, is, chain,
                                         fm, sizeof(fm), line, character);
        }
        if (rt && rt[0] && g_project_intel)
            h = intel_hover_member(g_project_intel, rt, word);
    }

    /* 模块核心语义抽象与接口调用契约 */
    if (!h.valid)
        h = intel_hover_pos(is, word, line, character);

    /* 核心系统底层抽象与内存语义契约 */
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

/* 内部辅助逻辑 */
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

    /* 内部辅助实现 */
    if (member_ctx) {
        char fm[64];
        const char *rt = intel_resolve_chain_pos(is, g_project_intel, chain,
                                                 fm, sizeof(fm), line, character);
        if (!rt || !rt[0]) {
            /* 内部辅助逻辑 */
            rt = intel_resolve_chain_pos(g_project_intel, is, chain,
                                         fm, sizeof(fm), line, character);
        }
        if (rt && rt[0] && g_project_intel) {
            intel_goto_member(g_project_intel, rt, word, &g);
        }
    }
    if (!g.found) {
        /* 内部辅助实现 */
        const isym_t *loc = intel_lookup_symbol_at(is, word, line, character);
        if (loc && (loc->kind == ISYM_VARIABLE || loc->kind == ISYM_PARAMETER)) {
            g.found = true;
            g.line = loc->line;
            g.col = loc->col;
            g.file[0] = '\0'; /* 核心系统底层抽象与内存语义契约 */
        }
    }
    if (!g.found)
        g = intel_goto_def(is, word);
    if (!g.found && g_project_intel)
        g = intel_goto_def(g_project_intel, word);
    if (!g.found) { send_response(s, id, json_new_null()); return; }

    /* 内部辅助实现 */
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
        /* 核心系统底层抽象与内存语义契约 */
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

/* 内部辅助实现 */
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
    /* 核心系统底层抽象与内存语义契约 */
    int hole = 0;
    bool fmt = false;
    while (text[i]) {
        char d = text[i];
        if (hole == 0) {
            if (d == '"') break; /* 核心系统底层抽象与内存语义契约 */
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

/* 模块核心语义抽象与接口调用契约 */
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

/* 内部辅助逻辑 */
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

/* 内部辅助实现 */
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

/* 模块核心语义抽象与接口调用契约 */
static bool uri_is_open(lsp_server_t *s, const char *uri) {
    for (int i = 0; i < s->doc_count; i++)
        if (same_uri_ci(s->docs[i].uri, uri)) return true;
    return false;
}

/* 模块核心语义抽象与接口调用契约 */
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

/* 内部辅助逻辑 */
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

/* 内部辅助实现 */
typedef struct {
    bool ok;
    bool is_local;
    char rt_simple[128]; /* 核心系统底层抽象与内存语义契约 */
    bool have_decl;
    char decl_ref[512];  /* 核心系统底层抽象与内存语义契约 */
    int decl_line, decl_col; /* 核心系统底层抽象与内存语义契约 */
    int ldecl_line, ldecl_col; /* 核心系统底层抽象与内存语义契约 */
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
        /* 内部辅助实现 */
        if (!de || strcmp(doc_uri, t->origin_uri) != 0) return false;
        /* `recv */
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
    /* 内部辅助逻辑 */
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

    /* 底层系统交互与数据协议契约 */
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
        if (sym->line < 0) return false; /* 核心系统底层抽象与内存语义契约 */
        /* 内部辅助逻辑 */
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
        /* 内部辅助逻辑 */
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

/* 内部辅助逻辑 */
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

/* 底层系统交互与数据协议契约 */
static intellisense_t *g_unopened_intel = NULL;

typedef struct {
    json_value *out;      /* 底层系统交互与数据协议契约 */
    const sym_target_t *t;
    const char *new_name; /* 核心系统底层抽象与内存语义契约 */
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

/* 内部辅助逻辑 */
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

/* 内部辅助逻辑 */
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

    /* 内部辅助实现 */
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
            /* 内部辅助逻辑 */
            if (idj) include_decl = json_get_bool(idj, json_get_num(idj, 1) != 0.0);
        }
        /* 内部辅助逻辑 */
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
        /* 内部辅助逻辑 */
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
        /* 模块核心语义抽象与接口调用契约 */
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

    /* 内部辅助逻辑 */
    json_value *arr = json_new_arr();
    for (int d = 0; d < s->doc_count; d++) {
        if (lsp_cancel_hit(s)) break;
        add_locations(arr, s->docs[d].uri, s->docs[d].text, word, -1, -1);
    }
    if (!lsp_cancel_hit(s)) {
        refs_ctx_t rc; rc.arr = arr; rc.word = word;
        for_each_unopened_project_file(s, &rc, refs_visit);
    }
    /* 底层系统交互与数据协议契约 */
    if (lsp_cancel_hit(s)) {
        json_free(arr);
        send_response_error(s, id, -32800, "Request cancelled");
        return;
    }
    send_response(s, id, arr);
}

/* 核心系统底层抽象与内存语义契约 */
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

    /* 核心系统底层抽象与内存语义契约 */
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

    /* 核心系统底层抽象与内存语义契约 */
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
        /* 模块核心语义抽象与接口调用契约 */
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

/* 内部辅助逻辑 */
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

/* 内部辅助逻辑 */
/* 内部辅助逻辑 */
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

/* 内部辅助逻辑 */
static void handle_rename(lsp_server_t *s, json_value *id, json_value *params) {
    const char *uri; int line, character;
    const char *new_name = json_get_str(json_obj_get(params, "newName"));
    if (!get_position(params, &uri, &line, &character) || !new_name || !new_name[0]) {
        send_response(s, id, json_new_null());
        return;
    }
    /* 核心系统底层抽象与内存语义契约 */
    if (!(isalpha((unsigned char)new_name[0]) || new_name[0] == '_')) {
        send_response(s, id, json_new_null());
        return;
    }
    for (const char *p = new_name + 1; *p; p++) {
        if (!is_ident_char(*p)) { send_response(s, id, json_new_null()); return; }
    }
    /* 内部辅助逻辑 */
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
        /* 内部辅助逻辑 */
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
        /* 内部辅助逻辑 */
        bool decl_open = false;
        /* 内部辅助逻辑 */
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
        /* 模块核心语义抽象与接口调用契约 */
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
        /* 模块核心语义抽象与接口调用契约 */
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

    /* 内部辅助逻辑 */
    json_value *changes = json_new_obj();
    json_value *edits = rename_edits_for(doc->text, word, new_name, -1, -1);
    if (edits) json_obj_set(changes, doc->uri, edits);

    json_value *we = json_new_obj();
    json_obj_set(we, "changes", changes);
    send_response(s, id, we);
}

/* formatting */

/* 内部辅助实现 */

typedef struct {
    bool in_str;    /* 核心系统底层抽象与内存语义契约 */
    bool in_block;  /* 底层系统交互与数据协议契约 */
} fmt_scan_t;

typedef struct {
    int  level;            /* 核心系统底层抽象与内存语义契约 */
    int  lead;             /* 核心系统底层抽象与内存语义契约 */
    const char *content;   /* 核心系统底层抽象与内存语义契约 */
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

/* 模块核心语义抽象与接口调用契约 */
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

        /* 内部辅助逻辑 */
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
            if (c == '/' && q + 1 < ce && q[1] == '/') break; /* 核心系统底层抽象与内存语义契约 */
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

/* 底层系统交互与数据协议契约 */
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

/* 内部辅助逻辑 */
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
                if (want == L->lead) continue; /* 核心系统底层抽象与内存语义契约 */
                json_value *edit = json_new_obj();
                json_value *range = json_new_obj();
                json_value *start = json_new_obj();
                json_value *endp = json_new_obj();
                json_obj_set(start, "line", json_new_num(i));
                json_obj_set(start, "character", json_new_num(0));
                json_obj_set(endp, "line", json_new_num(i));
                /* 底层系统交互与数据协议契约 */
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

/* documentHighlight / folding */

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
                /* 语言服务与调试协议交互规范 */
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

/* 内部辅助逻辑 */
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

/* prepareRename */

/* 内部辅助逻辑 */
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

    /* 内部辅助实现 */
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

/* 模块核心语义抽象与接口调用契约 */
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

/* 内部辅助逻辑 */
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

/* 模块核心语义抽象与接口调用契约 */
static void handle_code_action(lsp_server_t *s, json_value *id, json_value *params) {
    json_value *td = json_obj_get(params, "textDocument");
    const char *uri = json_get_str(json_obj_get(td, "uri"));
    if (!uri) { send_response(s, id, json_new_arr()); return; }

    lsp_doc_t *doc = lsp_find_doc(s, uri);
    if (!doc || !doc->text) { send_response(s, id, json_new_arr()); return; }

    /* 模块核心语义抽象与接口调用契约 */
    intellisense_t *is = g_project_intel;
    if (!is) {
        is = (intellisense_t *)malloc(sizeof(intellisense_t));
        if (!is) { send_response(s, id, json_new_arr()); return; }
        intel_init(is);
    }

    size_t doc_len = strlen(doc->text);
    using_analysis_t analysis = intel_analyze_usings(is, doc->text, doc_len);

    json_value *actions = json_new_arr();

    /* 模块核心语义抽象与接口调用契约 */
    if (analysis.missing_count > 0 || analysis.unused_count > 0) {
        json_value *action = json_new_obj();
        json_obj_set(action, "title", json_new_str("Organize Usings"));
        json_obj_set(action, "kind", json_new_str("source.organizeImports"));

        /* 模块核心语义抽象与接口调用契约 */
        size_t new_len;
        char *new_text = intel_organize_usings(is, doc->text, doc_len, &new_len);
        if (new_text) {
            /* 核心系统底层抽象与内存语义契约 */
            json_value *edit_obj = json_new_obj();
            json_value *changes = json_new_obj();
            json_value *edits = json_new_arr();

            json_value *text_edit = json_new_obj();
            json_value *range = json_new_obj();
            json_value *start_pos = json_new_obj();
            json_value *end_pos = json_new_obj();

            json_obj_set(start_pos, "line", json_new_num(0));
            json_obj_set(start_pos, "character", json_new_num(0));

            /* 底层系统交互与数据协议契约 */
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

    /* 模块核心语义抽象与接口调用契约 */
    for (int i = 0; i < analysis.missing_count; i++) {
        json_value *action = json_new_obj();
        char title[256];
        snprintf(title, sizeof(title), "Add using %s", analysis.missing_usings[i]);
        json_obj_set(action, "title", json_new_str(title));
        json_obj_set(action, "kind", json_new_str("quickfix"));

        /* 底层系统交互与数据协议契约 */
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

    /* 底层系统交互与数据协议契约 */
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

/* 核心系统底层抽象与内存语义契约 */
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

/* 核心系统底层抽象与内存语义契约 */
/* 内部辅助实现 */
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

    /* 核心系统底层抽象与内存语义契约 */
    zan_arena_t *arena = zan_arena_new();
    zan_diag_t *diag = zan_diag_new(arena);
    zan_diag_set_capture(diag, true);

    zan_lexer_t lex;
    zan_lexer_init(&lex, doc->text, strlen(doc->text), 0, arena, diag);

    /* 模块核心语义抽象与接口调用契约 */
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

            /* 核心系统底层抽象与内存语义契约 */
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
                /* 模块核心语义抽象与接口调用契约 */
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
                /* 模块核心语义抽象与接口调用契约 */
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

/* 核心系统底层抽象与内存语义契约 */

/* 底层系统交互与数据协议契约 */
static void uri_to_fspath(const char *uri, char *out, size_t cap) {
    out[0] = '\0';
    if (!uri) return;
    uri_to_native_path(uri, out, cap);
}

/* 内部辅助逻辑 */
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

    /* 内部辅助逻辑 */
    /* 内部辅助逻辑 */
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
                /* 核心系统底层抽象与内存语义契约 */
                char loc[1024];
                strncpy(loc, at, sizeof(loc) - 1);
                loc[sizeof(loc) - 1] = '\0';
                char *nl = strpbrk(loc, "\r\n");
                if (nl) *nl = '\0';
                /* 模块核心语义抽象与接口调用契约 */
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

    /* 底层系统交互与数据协议契约 */
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

/* dispatch */

static void dispatch(lsp_server_t *s, json_value *msg) {
    const char *method = json_get_str(json_obj_get(msg, "method"));
    json_value *id = json_obj_get(msg, "id");
    json_value *params = json_obj_get(msg, "params");
    if (!method) return;

    /* 内部辅助逻辑 */
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
        /* 核心系统底层抽象与内存语义契约 */
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
        /* 核心系统底层抽象与内存语义契约 */
    } else if (id) {
        /* 模块核心语义抽象与接口调用契约 */
        send_response(s, id, json_new_null());
    }
    lsp_doc_unlock(s);
}

/* 底层系统交互与数据协议契约 */

#define LSP_Q_MAX 1024

/* 内部辅助逻辑 */
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

/* 底层系统交互与数据协议契约 */
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

/* 模块核心语义抽象与接口调用契约 */
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

/* 内部辅助实现 */
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

/* 内部辅助逻辑 */
static bool lsp_cancel_hit(lsp_server_t *s) {
    lsp_q_lock(s);
    bool hit = cancel_matches_locked(s);
    lsp_q_unlock(s);
    if (hit) intel_cancel_flag = 1;
    return hit;
}

/* 内部辅助逻辑 */
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
    /* 内部辅助实现 */
    lsp_q_unlock(s);
    intel_cancel_flag = 0; /* 内部辅助逻辑 */
    return hit;
}

/* 内部辅助逻辑 */
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

/* 内部辅助逻辑 */
static void lsp_worker_loop(lsp_server_t *s) {
    for (;;) {
        lsp_msg_t *m = q_pop(s);
        if (!m) break; /* 内部辅助逻辑 */
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
        /* 底层系统交互与数据协议契约 */
        _setmode(_fileno(stdin), _O_BINARY);
        _setmode(_fileno(stdout), _O_BINARY);
#endif
    }

    /* 内部辅助逻辑 */
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

    /* 内部辅助逻辑 */
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

    /* 内部辅助逻辑 */
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
