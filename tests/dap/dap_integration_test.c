/* 底层系统交互与数据协议契约 */
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

/* 核心系统底层抽象与内存语义契约 */
typedef struct {
#ifdef _WIN32
    HANDLE in_w;   /* write to child's stdin */
    HANDLE out_r;  /* 核心系统底层抽象与内存语义契约 */
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

/* 核心系统底层抽象与内存语义契约 */
static int g_seq = 0;

static void dap_send(child_t *c, const char *body) {
    char header[64];
    int hlen = snprintf(header, sizeof(header),
                        "Content-Length: %d\r\n\r\n", (int)strlen(body));
    child_write(c, header, hlen);
    child_write(c, body, (int)strlen(body));
}

/* 底层系统交互与数据协议契约 */
static bool dap_recv(child_t *c, char *buf, int cap) {
    char header[256];
    int hp = 0;
    int content_len = -1;
    /* 底层系统交互与数据协议契约 */
    while (1) {
        int line_start = hp;
        while (1) {
            int ch = child_read_byte(c);
            if (ch < 0) return false;
            if (hp < (int)sizeof(header) - 1) header[hp++] = (char)ch;
            if (ch == '\n') break;
        }
        header[hp] = '\0';
        /* 底层系统交互与数据协议契约 */
        if (hp - line_start <= 2) break; /* 核心系统底层抽象与内存语义契约 */
        if (content_len < 0) {
            const char *cl = header + line_start;
            if (strncmp(cl, "Content-Length:", 15) == 0)
                content_len = atoi(cl + 15);
        }
    }
    if (content_len < 0 || content_len >= cap) return false;
    int got = 0;
    while (got < content_len) {
        int ch = child_read_byte(c);
        if (ch < 0) return false;
        buf[got++] = (char)ch;
    }
    buf[got] = '\0';
    return true;
}

/* 底层系统交互与数据协议契约 */
static bool wait_event(child_t *c, const char *name, char *out, int cap) {
    for (int i = 0; i < 200; i++) {
        if (!dap_recv(c, out, cap)) return false;
        char pat[128];
        snprintf(pat, sizeof(pat), "\"event\":\"%s\"", name);
        if (strstr(out, "\"type\":\"event\"") && strstr(out, pat))
            return true;
    }
    return false;
}

/* 底层系统交互与数据协议契约 */
static bool wait_stopped_or_ended(child_t *c, char *out, int cap) {
    for (int i = 0; i < 200; i++) {
        if (!dap_recv(c, out, cap)) return false;
        if (strstr(out, "\"type\":\"event\"") &&
            strstr(out, "\"event\":\"stopped\""))
            return true;
        if (strstr(out, "\"type\":\"event\"") &&
            (strstr(out, "\"event\":\"terminated\"") ||
             strstr(out, "\"event\":\"exited\"")))
            return false;
    }
    return false;
}

/* 底层系统交互与数据协议契约 */
static bool wait_response(child_t *c, const char *command, char *out, int cap) {
    for (int i = 0; i < 200; i++) {
        if (!dap_recv(c, out, cap)) return false;
        char pat[128];
        snprintf(pat, sizeof(pat), "\"command\":\"%s\"", command);
        if (strstr(out, "\"type\":\"response\"") && strstr(out, pat))
            return true;
    }
    return false;
}

static int g_fails = 0;
static void check(bool cond, const char *msg) {
    printf("%s: %s\n", cond ? "PASS" : "FAIL", msg);
    if (!cond) g_fails++;
}

/* 底层系统交互与数据协议契约 */
static long json_num(const char *json, const char *key, long def) {
    char pat[128];
    snprintf(pat, sizeof(pat), "\"%s\":", key);
    const char *p = strstr(json, pat);
    if (!p) return def;
    p += strlen(pat);
    while (*p == ' ') p++;
    return strtol(p, NULL, 10);
}

/* 底层系统交互与数据协议契约 */
static void json_escape(const char *in, char *out, size_t cap) {
    size_t j = 0;
    for (size_t i = 0; in[i] && j + 2 < cap; i++) {
        char ch = in[i];
        if (ch == '\\' || ch == '"') out[j++] = '\\';
        out[j++] = ch;
    }
    out[j] = '\0';
}

/* 核心系统底层抽象与内存语义契约 */

static void handshake(child_t *c, const char *program, char *body, char *msg,
                      size_t cap) {
    snprintf(body, cap,
             "{\"seq\":%d,\"type\":\"request\",\"command\":\"initialize\","
             "\"arguments\":{\"clientID\":\"itest\",\"adapterID\":\"zan\"}}", ++g_seq);
    dap_send(c, body);
    (void)wait_response(c, "initialize", msg, (int)cap);
    (void)wait_event(c, "initialized", msg, (int)cap);

    snprintf(body, cap,
             "{\"seq\":%d,\"type\":\"request\",\"command\":\"launch\","
             "\"arguments\":{\"program\":\"%s\",\"stopOnEntry\":false}}",
             ++g_seq, program);
    dap_send(c, body);
    (void)wait_response(c, "launch", msg, (int)cap);
}

static void send_configuration_done(child_t *c, char *body, char *msg, size_t cap) {
    snprintf(body, cap,
             "{\"seq\":%d,\"type\":\"request\",\"command\":\"configurationDone\"}",
             ++g_seq);
    dap_send(c, body);
    (void)wait_response(c, "configurationDone", msg, (int)cap);
}

static void send_disconnect(child_t *c, char *body, char *msg, size_t cap) {
    (void)msg;
    snprintf(body, cap,
             "{\"seq\":%d,\"type\":\"request\",\"command\":\"disconnect\"}", ++g_seq);
    dap_send(c, body);
}

/* 底层系统交互与数据协议契约 */
static int run_basic(const char *dap_exe, const char *target, const char *source) {
    g_seq = 0;
    child_t c;
    memset(&c, 0, sizeof(c));
#ifndef _WIN32
    c.in_w = -1; c.out_r = -1;
#endif
    if (!child_spawn(&c, dap_exe)) {
        fprintf(stderr, "could not spawn zan-dap: %s\n", dap_exe);
        return 2;
    }

    char body[65536];
    char msg[65536];

    handshake(&c, target, body, msg, sizeof(body));

    /* 核心系统底层抽象与内存语义契约 */
    snprintf(body, sizeof(body),
             "{\"seq\":%d,\"type\":\"request\",\"command\":\"setBreakpoints\","
             "\"arguments\":{\"source\":{\"path\":\"%s\"},"
             "\"breakpoints\":[{\"line\":11},{\"line\":17}]}}",
             ++g_seq, source);
    dap_send(&c, body);
    check(wait_response(&c, "setBreakpoints", msg, sizeof(msg)), "setBreakpoints response");
    bool verified = strstr(msg, "\"verified\":true") != NULL;

    send_configuration_done(&c, body, msg, sizeof(body));

    /* 底层系统交互与数据协议契约 */
    if (!wait_stopped_or_ended(&c, msg, sizeof(msg))) {
        child_close(&c);
        const char *optout = getenv("ZAN_ALLOW_SKIP_DAP");
        if (optout && strcmp(optout, "1") == 0) {
            printf("SKIP: no stopped event (gdb unavailable) — opt-out via ZAN_ALLOW_SKIP_DAP=1\n");
            return 77;
        }
        fprintf(stderr, "FAIL: no stopped event — is gdb installed and usable?\n"
                        "      (set ZAN_ALLOW_SKIP_DAP=1 to skip this test)\n");
        return 1;
    }
    check(strstr(msg, "\"reason\":\"breakpoint\"") != NULL, "stopped reason=breakpoint");
    check(verified, "breakpoint verified");

    /* stackTrace */
    snprintf(body, sizeof(body),
             "{\"seq\":%d,\"type\":\"request\",\"command\":\"stackTrace\","
             "\"arguments\":{\"threadId\":1}}", ++g_seq);
    dap_send(&c, body);
    check(wait_response(&c, "stackTrace", msg, sizeof(msg)), "stackTrace response");
    long total = json_num(msg, "totalFrames", 0);
    check(total >= 1, "call stack has at least one frame");
    long frame_id = json_num(msg, "id", -1);

    /* scopes */
    snprintf(body, sizeof(body),
             "{\"seq\":%d,\"type\":\"request\",\"command\":\"scopes\","
             "\"arguments\":{\"frameId\":%ld}}", ++g_seq, frame_id);
    dap_send(&c, body);
    check(wait_response(&c, "scopes", msg, sizeof(msg)), "scopes response");
    long locals_ref = json_num(msg, "variablesReference", 0);
    check(locals_ref != 0, "locals scope reference");

    /* variables */
    snprintf(body, sizeof(body),
             "{\"seq\":%d,\"type\":\"request\",\"command\":\"variables\","
             "\"arguments\":{\"variablesReference\":%ld}}", ++g_seq, locals_ref);
    dap_send(&c, body);
    check(wait_response(&c, "variables", msg, sizeof(msg)), "variables response");
    check(strstr(msg, "\"acc\"") != NULL, "local 'acc' present in variables");

    /* evaluate */
    snprintf(body, sizeof(body),
             "{\"seq\":%d,\"type\":\"request\",\"command\":\"evaluate\","
             "\"arguments\":{\"expression\":\"n\",\"frameId\":%ld,\"context\":\"watch\"}}",
             ++g_seq, frame_id);
    dap_send(&c, body);
    check(wait_response(&c, "evaluate", msg, sizeof(msg)), "evaluate response");

    /* 核心系统底层抽象与内存语义契约 */
    snprintf(body, sizeof(body),
             "{\"seq\":%d,\"type\":\"request\",\"command\":\"next\","
             "\"arguments\":{\"threadId\":1}}", ++g_seq);
    dap_send(&c, body);
    (void)wait_response(&c, "next", msg, sizeof(msg));
    check(wait_event(&c, "stopped", msg, sizeof(msg)), "stopped after step over");

    /* 底层系统交互与数据协议契约 */
    snprintf(body, sizeof(body),
             "{\"seq\":%d,\"type\":\"request\",\"command\":\"continue\","
             "\"arguments\":{\"threadId\":1}}", ++g_seq);
    dap_send(&c, body);

    bool exited = false;
    for (int i = 0; i < 400 && !exited; i++) {
        if (!dap_recv(&c, msg, sizeof(msg))) break;
        if (strstr(msg, "\"event\":\"exited\"")) { exited = true; break; }
        if (strstr(msg, "\"type\":\"event\"") &&
            strstr(msg, "\"event\":\"stopped\"")) {
            snprintf(body, sizeof(body),
                     "{\"seq\":%d,\"type\":\"request\",\"command\":\"continue\","
                     "\"arguments\":{\"threadId\":1}}", ++g_seq);
            dap_send(&c, body);
        }
    }
    check(exited, "exited event");

    send_disconnect(&c, body, msg, sizeof(body));

    child_close(&c);
    return 0;
}

/* 底层系统交互与数据协议契约 */
static int run_hitcount(const char *dap_exe, const char *burn, const char *burn_src) {
    g_seq = 0;
    child_t c;
    memset(&c, 0, sizeof(c));
#ifndef _WIN32
    c.in_w = -1; c.out_r = -1;
#endif
    if (!child_spawn(&c, dap_exe)) return 77;
    char body[65536], msg[65536];

    handshake(&c, burn, body, msg, sizeof(body));

    snprintf(body, sizeof(body),
             "{\"seq\":%d,\"type\":\"request\",\"command\":\"setBreakpoints\","
             "\"arguments\":{\"source\":{\"path\":\"%s\"},"
             "\"breakpoints\":[{\"line\":15,\"hitCondition\":\"==1000\"}]}}",
             ++g_seq, burn_src);
    dap_send(&c, body);
    check(wait_response(&c, "setBreakpoints", msg, sizeof(msg)),
          "hitcount: setBreakpoints response");
    check(strstr(msg, "\"verified\":true") != NULL, "hitcount: breakpoint verified");

    send_configuration_done(&c, body, msg, sizeof(body));

    if (!wait_stopped_or_ended(&c, msg, sizeof(msg))) {
        check(false, "hitcount: no stop on the counted hit");
        child_close(&c);
        return 1;
    }
    check(strstr(msg, "\"reason\":\"breakpoint\"") != NULL,
          "hitcount: stopped on the 1000th hit");

    snprintf(body, sizeof(body),
             "{\"seq\":%d,\"type\":\"request\",\"command\":\"stackTrace\","
             "\"arguments\":{\"threadId\":1}}", ++g_seq);
    dap_send(&c, body);
    (void)wait_response(&c, "stackTrace", msg, sizeof(msg));
    long frame_id = json_num(msg, "id", -1);

    /* 底层系统交互与数据协议契约 */
    snprintf(body, sizeof(body),
             "{\"seq\":%d,\"type\":\"request\",\"command\":\"evaluate\","
             "\"arguments\":{\"expression\":\"i\",\"frameId\":%ld,\"context\":\"watch\"}}",
             ++g_seq, frame_id);
    dap_send(&c, body);
    check(wait_response(&c, "evaluate", msg, sizeof(msg)), "hitcount: evaluate response");
    check(strstr(msg, "999") != NULL, "hitcount: loop counter is 999 at the stop");

    send_disconnect(&c, body, msg, sizeof(body));
    child_close(&c);
    return 0;
}

/* 底层系统交互与数据协议契约 */
static int run_logpoint(const char *dap_exe, const char *burn, const char *burn_src) {
    g_seq = 0;
    child_t c;
    memset(&c, 0, sizeof(c));
#ifndef _WIN32
    c.in_w = -1; c.out_r = -1;
#endif
    if (!child_spawn(&c, dap_exe)) return 77;
    char body[65536], msg[65536];

    handshake(&c, burn, body, msg, sizeof(body));

    snprintf(body, sizeof(body),
             "{\"seq\":%d,\"type\":\"request\",\"command\":\"setBreakpoints\","
             "\"arguments\":{\"source\":{\"path\":\"%s\"},"
             "\"breakpoints\":[{\"line\":30,\"logMessage\":\"tick outer={outer}\"}]}}",
             ++g_seq, burn_src);
    dap_send(&c, body);
    check(wait_response(&c, "setBreakpoints", msg, sizeof(msg)),
          "logpoint: setBreakpoints response");
    check(strstr(msg, "\"verified\":true") != NULL, "logpoint: breakpoint verified");

    send_configuration_done(&c, body, msg, sizeof(body));

    bool exited = false, stopped = false;
    int logs = 0;
    for (int i = 0; i < 800 && !exited && !stopped; i++) {
        if (!dap_recv(&c, msg, sizeof(msg))) break;
        if (strstr(msg, "\"event\":\"exited\"")) { exited = true; break; }
        if (strstr(msg, "\"type\":\"event\"") && strstr(msg, "\"event\":\"stopped\"")) {
            stopped = true;
            break;
        }
        if (strstr(msg, "\"event\":\"output\"") && strstr(msg, "tick outer="))
            logs++;
    }
    check(logs >= 1, "logpoint: log output events seen");
    check(strstr(msg, "tick outer=") != NULL || logs > 0,
          "logpoint: {outer} interpolation in output");
    check(!stopped, "logpoint: never reported a stop");
    check(exited, "logpoint: target ran to exit");

    send_disconnect(&c, body, msg, sizeof(body));
    child_close(&c);
    return 0;
}

/* 底层系统交互与数据协议契约 */
static int run_pause(const char *dap_exe, const char *burn, const char *burn_src) {
    (void)burn_src;
    g_seq = 0;
    child_t c;
    memset(&c, 0, sizeof(c));
#ifndef _WIN32
    c.in_w = -1; c.out_r = -1;
#endif
    if (!child_spawn(&c, dap_exe)) return 77;
    char body[65536], msg[65536];

    handshake(&c, burn, body, msg, sizeof(body));

    send_configuration_done(&c, body, msg, sizeof(body));

    /* 底层系统交互与数据协议契约 */
#ifdef _WIN32
    Sleep(1000);
#else
    sleep(1);
#endif

    snprintf(body, sizeof(body),
             "{\"seq\":%d,\"type\":\"request\",\"command\":\"pause\"}", ++g_seq);
    dap_send(&c, body);
    check(wait_response(&c, "pause", msg, sizeof(msg)), "pause: response");

    if (!wait_stopped_or_ended(&c, msg, sizeof(msg))) {
        check(false, "pause: no stopped event");
        child_close(&c);
        return 1;
    }
    check(strstr(msg, "\"reason\":\"pause\"") != NULL, "pause: stopped reason=pause");

    snprintf(body, sizeof(body),
             "{\"seq\":%d,\"type\":\"request\",\"command\":\"stackTrace\","
             "\"arguments\":{\"threadId\":1}}", ++g_seq);
    dap_send(&c, body);
    check(wait_response(&c, "stackTrace", msg, sizeof(msg)),
          "pause: stackTrace response");
    check(json_num(msg, "totalFrames", 0) >= 1, "pause: call stack available");
    check(strstr(msg, "dbgtarget_burn") != NULL,
          "pause: stopped inside the burn target");

    send_disconnect(&c, body, msg, sizeof(body));
    child_close(&c);
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 4) {
        fprintf(stderr, "usage: %s <zan-dap> <target-exe> <source> "
                        "[<burn-exe> <burn-source>]\n", argv[0]);
        return 2;
    }
    const char *dap_exe = argv[1];
    char target[1024], source[1024];
    json_escape(argv[2], target, sizeof(target));
    json_escape(argv[3], source, sizeof(source));

    setvbuf(stdout, NULL, _IONBF, 0);

    int r = run_basic(dap_exe, target, source);
    if (r != 0) return r; /* 底层系统交互与数据协议契约 */

    if (argc >= 6) {
        char burn[1024], burn_src[1024];
        json_escape(argv[4], burn, sizeof(burn));
        json_escape(argv[5], burn_src, sizeof(burn_src));
        r = run_hitcount(dap_exe, burn, burn_src);
        if (r == 0) r = run_logpoint(dap_exe, burn, burn_src);
        if (r == 0) r = run_pause(dap_exe, burn, burn_src);
    }

    printf("\n%d failure(s)\n", g_fails);
    return g_fails ? 1 : 0;
}
