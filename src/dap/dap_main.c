/* dap_main */
#include "json.h"
#include "rpc.h"
#include "debugger.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET dap_sock_t;
#define DAP_INVALID_SOCK INVALID_SOCKET
#else
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <time.h>
typedef int dap_sock_t;
#define DAP_INVALID_SOCK (-1)
#endif

typedef struct {
    debugger_t dbg;
    FILE      *out;
    int        seq;              /* 核心系统底层抽象与内存语义契约 */
    char       program[1024];    /* 核心系统底层抽象与内存语义契约 */
    char       prog_args[1024];  /* 核心系统底层抽象与内存语义契约 */
    char       source_file[1024];/* 底层系统交互与数据协议契约 */
    bool       terminated;
    bool       launched;
    int        attach_pid;       /* 核心系统底层抽象与内存语义契约 */
    bool       use_sock;         /* 底层系统交互与数据协议契约 */
    dap_sock_t sock;             /* 核心系统底层抽象与内存语义契约 */
} dap_t;

/* 核心系统底层抽象与内存语义契约 */

static bool sock_send_all(dap_sock_t s, const char *buf, int n) {
    int sent = 0;
    while (sent < n) {
        int r = (int)send(s, buf + sent, n - sent, 0);
        if (r <= 0) return false;
        sent += r;
    }
    return true;
}

/* 内部辅助逻辑 */
#ifndef DAP_FRAME_DEADLINE_MS
#define DAP_FRAME_DEADLINE_MS 60000
#endif

typedef struct {
    dap_sock_t s;
    bool started;        /* 核心系统底层抽象与内存语义契约 */
    long long deadline;  /* 底层系统交互与数据协议契约 */
} dap_frame_reader_t;

static long long dap_now_ms(void) {
#ifdef _WIN32
    return (long long)GetTickCount64();
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
#endif
}

static int sock_reader(void *ctx, char *buf, int n) {
    dap_frame_reader_t *r = (dap_frame_reader_t *)ctx;
    if (r->started) {
        /* 内部辅助逻辑 */
        long long wait = r->deadline - dap_now_ms();
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
        r->deadline = dap_now_ms() + DAP_FRAME_DEADLINE_MS;
    }
    return got;
}

static bool sock_writer(void *ctx, const char *buf, int n) {
    return sock_send_all(*(dap_sock_t *)ctx, buf, n);
}

/* 底层系统交互与数据协议契约 */
static char *rpc_read_message_sock(dap_sock_t s) {
    static dap_frame_reader_t r;   /* 核心系统底层抽象与内存语义契约 */
    r.s = s;
    r.started = false;             /* 底层系统交互与数据协议契约 */
    return rpc_read_message_cb(sock_reader, &r, 64 * 1024 * 1024);
}

static void rpc_write_message_sock(dap_sock_t s, const char *payload) {
    rpc_write_message_cb(sock_writer, &s, payload);
}

/* 核心系统底层抽象与内存语义契约 */
#define VARREF_LOCALS  1000
#define VARREF_WATCHES 2000

/* message I/O */

static void dap_send(dap_t *d, json_value *msg) {
    json_obj_set(msg, "seq", json_new_num(d->seq++));
    char *payload = json_serialize(msg);
    if (d->use_sock)
        rpc_write_message_sock(d->sock, payload);
    else
        rpc_write_message(d->out, payload);
    free(payload);
    json_free(msg);
}

static void dap_send_response(dap_t *d, json_value *request, bool success,
                              json_value *body) {
    json_value *resp = json_new_obj();
    json_obj_set(resp, "type", json_new_str("response"));
    json_obj_set(resp, "request_seq",
                 json_new_num(json_get_num(json_obj_get(request, "seq"), 0)));
    json_obj_set(resp, "success", json_new_bool(success));
    const char *cmd = json_get_str(json_obj_get(request, "command"));
    json_obj_set(resp, "command", json_new_str(cmd ? cmd : ""));
    if (body) json_obj_set(resp, "body", body);
    dap_send(d, resp);
}

static void dap_send_event(dap_t *d, const char *event, json_value *body) {
    json_value *ev = json_new_obj();
    json_obj_set(ev, "type", json_new_str("event"));
    json_obj_set(ev, "event", json_new_str(event));
    if (body) json_obj_set(ev, "body", body);
    dap_send(d, ev);
}

static void dap_output(dap_t *d, const char *category, const char *text) {
    json_value *body = json_new_obj();
    json_obj_set(body, "category", json_new_str(category));
    json_obj_set(body, "output", json_new_str(text));
    dap_send_event(d, "output", body);
}

/* 底层系统交互与数据协议契约 */
static void dap_send_stopped(dap_t *d, const char *reason) {
    json_value *body = json_new_obj();
    json_obj_set(body, "reason", json_new_str(reason));
    json_obj_set(body, "threadId", json_new_num(1));
    json_obj_set(body, "allThreadsStopped", json_new_bool(true));
    dap_send_event(d, "stopped", body);
}

/* 内部辅助逻辑 */
static void dap_flush_output(dap_t *d) {
    if (d->dbg.output_len > 0) {
        dap_output(d, "stdout", d->dbg.output);
        dbg_clear_output(&d->dbg);
    }
}

static void dap_terminate_with_code(dap_t *d, int exit_code) {
    if (d->terminated) return;
    d->terminated = true;
    dap_send_event(d, "terminated", NULL);
    json_value *body = json_new_obj();
    json_obj_set(body, "exitCode", json_new_num(exit_code));
    dap_send_event(d, "exited", body);
}

/* 底层系统交互与数据协议契约 */
static const char *dap_stop_reason(const char *mi) {
    if (!mi || !mi[0]) return "breakpoint";
    if (strcmp(mi, "pause") == 0) return "pause";
    if (strcmp(mi, "signal-received") == 0) return "pause"; /* 核心系统底层抽象与内存语义契约 */
    if (strncmp(mi, "breakpoint", 10) == 0) return "breakpoint";
    if (strstr(mi, "stepping-range") || strstr(mi, "finished")) return "step";
    if (strstr(mi, "watchpoint")) return "data breakpoint";
    if (strstr(mi, "exception")) return "exception";
    if (strstr(mi, "signal")) return "exception";
    return "breakpoint";
}

/* 内部辅助逻辑 */
static const char *dap_basename(const char *path) {
    const char *b = path;
    for (const char *p = path; *p; p++)
        if (*p == '/' || *p == '\\') b = p + 1;
    return b;
}

/* 底层系统交互与数据协议契约 */
static dbg_breakpoint_t *dap_logpoint_here(dap_t *d) {
    if (d->dbg.state != DBG_PAUSED) return NULL;
    dbg_breakpoint_t *lp = NULL;
    for (int i = 0; i < d->dbg.bp_count; i++) {
        dbg_breakpoint_t *bp = &d->dbg.breakpoints[i];
        if (!bp->enabled) continue;
        if (bp->line != d->dbg.current_line) continue;
        if (strcmp(dap_basename(bp->file), dap_basename(d->dbg.current_file)) != 0)
            continue;
        if (bp->type == BP_LOGPOINT) lp = bp;
        else return NULL; /* 底层系统交互与数据协议契约 */
    }
    return lp;
}

/* 内部辅助逻辑 */
static void dap_format_log(dap_t *d, const char *msg, char *out, int cap) {
    int o = 0;
    for (int i = 0; msg[i]; i++) {
        if (msg[i] != '{') {
            if (o < cap - 1) out[o++] = msg[i];
            continue;
        }
        const char *close = strchr(msg + i + 1, '}');
        if (!close) {
            if (o < cap - 1) out[o++] = msg[i];
            continue;
        }
        char expr[192];
        int el = (int)(close - (msg + i + 1));
        if (el > (int)sizeof(expr) - 1) el = (int)sizeof(expr) - 1;
        memcpy(expr, msg + i + 1, (size_t)el);
        expr[el] = '\0';
        i = (int)(close - msg);
        char val[256];
        if (el > 0 && dbg_evaluate(&d->dbg, expr, val, sizeof(val)))
            o += snprintf(out + o, (size_t)(cap - o > 0 ? cap - o : 0), "%s", val);
        else
            o += snprintf(out + o, (size_t)(cap - o > 0 ? cap - o : 0), "<%s>",
                          expr[0] ? expr : "?");
        if (o >= cap - 1) break;
    }
    out[o < cap - 1 ? o : cap - 1] = '\0';
    size_t l = strlen(out);
    if (l == 0 || out[l - 1] != '\n') {
        out[l < cap - 1 ? l : cap - 1] = '\n';
        out[l < cap - 1 ? l + 1 : cap - 1] = '\0';
    }
}

/* 底层系统交互与数据协议契约 */
static void dap_report_stop(dap_t *d) {
    dap_flush_output(d);
    int guard = 0;
    while (d->dbg.state == DBG_PAUSED && guard++ < 64) {
        dbg_breakpoint_t *lp = dap_logpoint_here(d);
        if (!lp) break;
        char text[1024];
        dap_format_log(d, lp->log_message, text, sizeof(text));
        dap_output(d, "stdout", text);
        dbg_continue(&d->dbg);
        dap_flush_output(d);
    }
    if (d->dbg.state == DBG_PAUSED)
        dap_send_stopped(d, dap_stop_reason(d->dbg.stop_reason));
    else
        dap_terminate_with_code(d, d->dbg.last_exit_code);
}

/* handlers */

static void handle_initialize(dap_t *d, json_value *request) {
    json_value *caps = json_new_obj();
    json_obj_set(caps, "supportsConfigurationDoneRequest", json_new_bool(true));
    json_obj_set(caps, "supportsFunctionBreakpoints", json_new_bool(false));
    json_obj_set(caps, "supportsConditionalBreakpoints", json_new_bool(true));
    json_obj_set(caps, "supportsHitConditionalBreakpoints", json_new_bool(true));
    json_obj_set(caps, "supportsLogPoints", json_new_bool(true));
    json_obj_set(caps, "supportsEvaluateForHovers", json_new_bool(true));
    json_obj_set(caps, "supportsSetVariable", json_new_bool(true));
    json_obj_set(caps, "supportsStepBack", json_new_bool(false));
    json_obj_set(caps, "supportsTerminateRequest", json_new_bool(true));
    json_obj_set(caps, "supportsRunInTerminalRequest", json_new_bool(false));
    json_obj_set(caps, "supportsExceptionInfoRequest", json_new_bool(true));
    json_obj_set(caps, "supportsExceptionFilterOptions", json_new_bool(true));
    {
        /* 底层系统交互与数据协议契约 */
        json_value *filters = json_new_arr();
        const char *ids[2]   = { "throw", "unhandled" };
        const char *labels[2] = { "Thrown exceptions", "Unhandled exceptions" };
        for (int i = 0; i < 2; i++) {
            json_value *f = json_new_obj();
            json_obj_set(f, "filter", json_new_str(ids[i]));
            json_obj_set(f, "label", json_new_str(labels[i]));
            json_obj_set(f, "default", json_new_bool(i == 1));
            json_arr_add(filters, f);
        }
        json_obj_set(caps, "exceptionBreakpointFilters", filters);
    }
    dap_send_response(d, request, true, caps);
    /* 底层系统交互与数据协议契约 */
    dap_send_event(d, "initialized", NULL);
}

static void handle_set_breakpoints(dap_t *d, json_value *request) {
    json_value *args = json_obj_get(request, "arguments");
    json_value *source = json_obj_get(args, "source");
    const char *path = json_get_str(json_obj_get(source, "path"));
    json_value *bps = json_obj_get(args, "breakpoints");

    if (path) {
        /* 底层系统交互与数据协议契约 */
        for (int i = d->dbg.bp_count - 1; i >= 0; i--) {
            if (strcmp(d->dbg.breakpoints[i].file, path) == 0)
                dbg_remove_breakpoint(&d->dbg, d->dbg.breakpoints[i].id);
        }
        strncpy(d->source_file, path, sizeof(d->source_file) - 1);
    }

    json_value *verified = json_new_arr();
    int n = json_arr_count(bps);
    for (int i = 0; i < n; i++) {
        json_value *bp = json_arr_at(bps, i);
        int line = (int)json_get_num(json_obj_get(bp, "line"), 0);
        const char *cond = json_get_str(json_obj_get(bp, "condition"));
        const char *hit_cond = json_get_str(json_obj_get(bp, "hitCondition"));
        const char *log_msg = json_get_str(json_obj_get(bp, "logMessage"));
        const char *reject = NULL; /* 底层系统交互与数据协议契约 */

        int id = -1;
        if (path) {
            if (log_msg && log_msg[0]) {
                /* Logpoint */
                id = dbg_add_logpoint(&d->dbg, path, line, log_msg);
            } else if (hit_cond && hit_cond[0]) {
                /* 核心系统底层抽象与内存语义契约 */
                const char *h = hit_cond;
                while (*h == ' ') h++;
                bool modulo = (h[0] == '%');
                if (strncmp(h, "==", 2) == 0 || strncmp(h, ">=", 2) == 0) h += 2;
                int count = atoi(h);
                if (count > 0 && !modulo) {
                    id = dbg_add_hitcount_bp(&d->dbg, path, line, count);
                } else {
                    char why[192];
                    snprintf(why, sizeof(why),
                             "unsupported hitCondition '%s' (use K, ==K or >=K)",
                             hit_cond);
                    reject = why;
                }
            } else if (cond && cond[0]) {
                /* 核心系统底层抽象与内存语义契约 */
                id = dbg_add_conditional_bp(&d->dbg, path, line, cond);
            } else {
                /* 核心系统底层抽象与内存语义契约 */
                id = dbg_add_breakpoint(&d->dbg, path, line);
            }
        }

        json_value *out = json_new_obj();
        json_obj_set(out, "id", json_new_num(id >= 0 ? id : i));
        json_obj_set(out, "verified", json_new_bool(id >= 0));
        json_obj_set(out, "line", json_new_num(line));
        if (cond && cond[0])
            json_obj_set(out, "message", json_new_str(cond));
        else if (reject)
            json_obj_set(out, "message", json_new_str(reject));
        json_arr_add(verified, out);
    }

    json_value *body = json_new_obj();
    json_obj_set(body, "breakpoints", verified);
    dap_send_response(d, request, true, body);
}

static void handle_launch(dap_t *d, json_value *request) {
    json_value *args = json_obj_get(request, "arguments");
    const char *program = json_get_str(json_obj_get(args, "program"));
    const char *prog_args = json_get_str(json_obj_get(args, "args"));
    bool stop_on_entry = (bool)json_get_num(json_obj_get(args, "stopOnEntry"), 0);

    const char *gdb_path = json_get_str(json_obj_get(args, "gdbPath"));

    if (program) strncpy(d->program, program, sizeof(d->program) - 1);
    if (prog_args) strncpy(d->prog_args, prog_args, sizeof(d->prog_args) - 1);
    if (gdb_path && gdb_path[0]) dbg_set_gdb_path(&d->dbg, gdb_path);
    d->dbg.break_on_entry = stop_on_entry;

    d->launched = true;
    dap_output(d, "console", "Launching Zan program under gdb...\n");

    /* 内部辅助逻辑 */
    dap_send_response(d, request, true, NULL);
}

static void handle_configuration_done(dap_t *d, json_value *request) {
    dap_send_response(d, request, true, NULL);
    /* 底层系统交互与数据协议契约 */
    if (d->attach_pid > 0)
        dbg_attach(&d->dbg, d->program, d->attach_pid);
    else
        dbg_start(&d->dbg, d->program, d->prog_args[0] ? d->prog_args : NULL);
    dap_report_stop(d);
}

static void handle_threads(dap_t *d, json_value *request) {
    json_value *threads = json_new_arr();
    dbg_refresh_threads(&d->dbg);
    for (int i = 0; i < d->dbg.thread_count; i++) {
        json_value *thread = json_new_obj();
        json_obj_set(thread, "id", json_new_num(d->dbg.threads[i].id));
        json_obj_set(thread, "name", json_new_str(d->dbg.threads[i].name));
        json_arr_add(threads, thread);
    }
    if (d->dbg.thread_count == 0) {
        /* 内部辅助逻辑 */
        json_value *thread = json_new_obj();
        json_obj_set(thread, "id", json_new_num(1));
        json_obj_set(thread, "name", json_new_str("main"));
        json_arr_add(threads, thread);
    }
    json_value *body = json_new_obj();
    json_obj_set(body, "threads", threads);
    dap_send_response(d, request, true, body);
}

static void handle_stack_trace(dap_t *d, json_value *request) {
    json_value *frames = json_new_arr();
    for (int i = 0; i < d->dbg.callstack_depth; i++) {
        dbg_frame_t *f = &d->dbg.callstack[i];
        json_value *frame = json_new_obj();
        json_obj_set(frame, "id", json_new_num(f->frame_id));
        json_obj_set(frame, "name",
                     json_new_str(f->function_name[0] ? f->function_name : "Main"));
        json_obj_set(frame, "line", json_new_num(f->line));
        json_obj_set(frame, "column", json_new_num(f->col > 0 ? f->col : 1));
        const char *file = f->file[0] ? f->file : d->source_file;
        if (file[0]) {
            json_value *src = json_new_obj();
            json_obj_set(src, "path", json_new_str(file));
            json_obj_set(frame, "source", src);
        }
        json_arr_add(frames, frame);
    }
    json_value *body = json_new_obj();
    json_obj_set(body, "stackFrames", frames);
    json_obj_set(body, "totalFrames", json_new_num(d->dbg.callstack_depth));
    dap_send_response(d, request, true, body);
}

static void handle_scopes(dap_t *d, json_value *request) {
    json_value *scopes = json_new_arr();

    /* 核心系统底层抽象与内存语义契约 */
    json_value *locals_scope = json_new_obj();
    json_obj_set(locals_scope, "name", json_new_str("Locals"));
    json_obj_set(locals_scope, "variablesReference", json_new_num(VARREF_LOCALS));
    json_obj_set(locals_scope, "expensive", json_new_bool(false));
    json_obj_set(locals_scope, "presentationHint", json_new_str("locals"));
    json_arr_add(scopes, locals_scope);

    /* 核心系统底层抽象与内存语义契约 */
    if (d->dbg.watch_count > 0) {
        json_value *watch_scope = json_new_obj();
        json_obj_set(watch_scope, "name", json_new_str("Watch"));
        json_obj_set(watch_scope, "variablesReference", json_new_num(VARREF_WATCHES));
        json_obj_set(watch_scope, "expensive", json_new_bool(false));
        json_arr_add(scopes, watch_scope);
    }

    json_value *body = json_new_obj();
    json_obj_set(body, "scopes", scopes);
    dap_send_response(d, request, true, body);
}

static void handle_variables(dap_t *d, json_value *request) {
    json_value *args = json_obj_get(request, "arguments");
    int ref = (int)json_get_num(json_obj_get(args, "variablesReference"), 0);
    json_value *vars = json_new_arr();

    if (ref == VARREF_LOCALS) {
        for (int i = 0; i < d->dbg.local_count; i++) {
            dbg_local_t *v = &d->dbg.locals[i];
            json_value *var = json_new_obj();
            json_obj_set(var, "name", json_new_str(v->name));
            json_obj_set(var, "value", json_new_str(v->value));
            json_obj_set(var, "type", json_new_str(v->type));
            json_obj_set(var, "variablesReference",
                        json_new_num(v->has_children ? (i + 3000) : 0));
            json_arr_add(vars, var);
        }
    } else if (ref >= 3000 && ref < 3000 + DBG_MAX_LOCALS) {
        /* 内部辅助逻辑 */
        dbg_var_t kids[64];
        int n = dbg_expand_variables(&d->dbg, ref, kids, 64);
        for (int i = 0; i < n; i++) {
            json_value *var = json_new_obj();
            json_obj_set(var, "name", json_new_str(kids[i].name));
            json_obj_set(var, "value", json_new_str(kids[i].value));
            json_obj_set(var, "type", json_new_str(kids[i].type));
            json_obj_set(var, "variablesReference",
                        json_new_num(kids[i].expand_ref));
            json_arr_add(vars, var);
        }
    } else if (ref >= DBG_VARREF_DYN) {
        dbg_var_t kids[64];
        int n = dbg_expand_variables(&d->dbg, ref, kids, 64);
        for (int i = 0; i < n; i++) {
            json_value *var = json_new_obj();
            json_obj_set(var, "name", json_new_str(kids[i].name));
            json_obj_set(var, "value", json_new_str(kids[i].value));
            json_obj_set(var, "type", json_new_str(kids[i].type));
            json_obj_set(var, "variablesReference",
                        json_new_num(kids[i].expand_ref));
            json_arr_add(vars, var);
        }
    } else if (ref == VARREF_WATCHES) {
        /* 核心系统底层抽象与内存语义契约 */
        dbg_evaluate_watches(&d->dbg);
        for (int i = 0; i < d->dbg.watch_count; i++) {
            dbg_watch_t *w = &d->dbg.watches[i];
            json_value *var = json_new_obj();
            json_obj_set(var, "name", json_new_str(w->expression));
            json_obj_set(var, "value", json_new_str(w->value));
            json_obj_set(var, "type", json_new_str(w->type[0] ? w->type : "unknown"));
            json_obj_set(var, "variablesReference", json_new_num(0));
            json_obj_set(var, "evaluateName", json_new_str(w->expression));
            json_arr_add(vars, var);
        }
    }

    json_value *body = json_new_obj();
    json_obj_set(body, "variables", vars);
    dap_send_response(d, request, true, body);
}

/* 底层系统交互与数据协议契约 */
static void handle_evaluate(dap_t *d, json_value *request) {
    json_value *args = json_obj_get(request, "arguments");
    const char *expression = json_get_str(json_obj_get(args, "expression"));
    const char *context_str = json_get_str(json_obj_get(args, "context"));

    if (!expression || !expression[0]) {
        dap_send_response(d, request, false, NULL);
        return;
    }

    char result[256];
    bool success = dbg_evaluate(&d->dbg, expression, result, sizeof(result));

    json_value *body = json_new_obj();
    json_obj_set(body, "result", json_new_str(result));
    json_obj_set(body, "variablesReference", json_new_num(0));

    /* 底层系统交互与数据协议契约 */
    if (context_str && strcmp(context_str, "watch") == 0) {
        /* 核心系统底层抽象与内存语义契约 */
        bool found = false;
        for (int i = 0; i < d->dbg.watch_count; i++) {
            if (strcmp(d->dbg.watches[i].expression, expression) == 0) {
                found = true;
                break;
            }
        }
        if (!found) dbg_add_watch(&d->dbg, expression);
    }

    dap_send_response(d, request, success, body);
}

/* 核心系统底层抽象与内存语义契约 */
static void handle_set_variable(dap_t *d, json_value *request) {
    json_value *args = json_obj_get(request, "arguments");
    const char *name = json_get_str(json_obj_get(args, "name"));
    const char *value = json_get_str(json_obj_get(args, "value"));

    if (!name || !value) {
        dap_send_response(d, request, false, NULL);
        return;
    }

    bool success = dbg_set_variable(&d->dbg, name, value);
    json_value *body = json_new_obj();
    json_obj_set(body, "value", json_new_str(value));
    json_obj_set(body, "variablesReference", json_new_num(0));
    dap_send_response(d, request, success, body);
}

/* 核心系统底层抽象与内存语义契约 */
static void handle_exception_info(dap_t *d, json_value *request) {
    char info[256] = {0};
    bool has_info = dbg_get_exception_info(&d->dbg, info, sizeof(info));

    json_value *body = json_new_obj();
    json_obj_set(body, "exceptionId", json_new_str(has_info ? info : "unknown"));
    json_obj_set(body, "breakMode", json_new_str("always"));
    dap_send_response(d, request, true, body);
}

/* 底层系统交互与数据协议契约 */
static void handle_continue(dap_t *d, json_value *request) {
    json_value *body = json_new_obj();
    json_obj_set(body, "allThreadsContinued", json_new_bool(true));
    dap_send_response(d, request, true, body);
    dbg_continue(&d->dbg);
    dap_report_stop(d);
}

static void handle_next(dap_t *d, json_value *request) {
    dap_send_response(d, request, true, NULL);
    dbg_step_over(&d->dbg);
    dap_report_stop(d);
}

static void handle_step_in(dap_t *d, json_value *request) {
    dap_send_response(d, request, true, NULL);
    dbg_step_into(&d->dbg);
    dap_report_stop(d);
}

static void handle_step_out(dap_t *d, json_value *request) {
    dap_send_response(d, request, true, NULL);
    dbg_step_out(&d->dbg);
    dap_report_stop(d);
}

/* 核心系统底层抽象与内存语义契约 */
static void handle_pause(dap_t *d, json_value *request) {
    if (d->dbg.state != DBG_RUNNING) {
        json_value *body = json_new_obj();
        json_obj_set(body, "error", json_new_str("target is not running"));
        dap_send_response(d, request, false, body);
        return;
    }
    dap_send_response(d, request, true, NULL);
    if (dbg_interrupt(&d->dbg))
        dbg_wait_stop(&d->dbg);
    dap_report_stop(d);
}

static void handle_disconnect(dap_t *d, json_value *request) {
    dbg_stop(&d->dbg);
    dap_send_response(d, request, true, NULL);
    dap_terminate_with_code(d, d->dbg.last_exit_code);
}

static void handle_set_exception_breakpoints(dap_t *d, json_value *request) {
    json_value *args = json_obj_get(request, "arguments");
    json_value *filters = json_obj_get(args, "filters");
    bool on_throw = false, on_unhandled = false;
    int n = json_arr_count(filters);
    for (int i = 0; i < n; i++) {
        const char *f = json_get_str(json_arr_at(filters, i));
        if (!f) continue;
        if (strcmp(f, "throw") == 0 || strcmp(f, "all") == 0) on_throw = true;
        else if (strcmp(f, "unhandled") == 0 || strcmp(f, "uncaught") == 0)
            on_unhandled = true;
    }
    /* 内部辅助逻辑 */
    dbg_set_exception_breakpoints(&d->dbg, on_throw, on_unhandled);
    dap_send_response(d, request, true, NULL);
}

static void handle_attach(dap_t *d, json_value *request) {
    json_value *args = json_obj_get(request, "arguments");
    const char *program = json_get_str(json_obj_get(args, "program"));
    const char *gdb_path = json_get_str(json_obj_get(args, "gdbPath"));
    int pid = (int)json_get_num(json_obj_get(args, "processId"), 0);
    if (program) strncpy(d->program, program, sizeof(d->program) - 1);
    if (gdb_path && gdb_path[0]) dbg_set_gdb_path(&d->dbg, gdb_path);
    if (pid <= 0) {
        dap_send_response(d, request, false, NULL);
        dap_output(d, "console", "attach needs a processId.\n");
        return;
    }
    d->attach_pid = pid;
    d->launched = true;
    dap_send_response(d, request, true, NULL);
}

static void handle_select_thread(dap_t *d, json_value *request) {
    json_value *args = json_obj_get(request, "arguments");
    int tid = (int)json_get_num(json_obj_get(args, "threadId"), 0);
    if (tid > 0) dbg_select_thread(&d->dbg, tid);
    dap_send_response(d, request, true, NULL);
}

/* 内部辅助逻辑 */

static dap_t *g_dap;

#define DAP_PARK_MAX 64
static char *g_parked[DAP_PARK_MAX];
static int   g_parked_head;
static int   g_parked_tail;

static char *take_parked(void) {
    if (g_parked_head == g_parked_tail) return NULL;
    char *body = g_parked[g_parked_head++];
    if (g_parked_head == g_parked_tail) g_parked_head = g_parked_tail = 0;
    return body;
}

#ifdef _WIN32
static bool rd_input_pending(dap_t *d) {
    if (d->use_sock) {
        u_long n = 0;
        if (ioctlsocket(d->sock, FIONREAD, &n) != 0) return false;
        return n > 0;
    }
    HANDLE h = (HANDLE)_get_osfhandle(0);
    DWORD avail = 0;
    /* 内部辅助逻辑 */
    return h != INVALID_HANDLE_VALUE && h != NULL &&
           PeekNamedPipe(h, NULL, 0, NULL, &avail, NULL) && avail > 0;
}

/* 底层系统交互与数据协议契约 */
static int rd_reader(void *ctx, char *buf, int n) {
    (void)ctx;
    DWORD r = 0;
    if (!ReadFile((HANDLE)_get_osfhandle(0), buf, (DWORD)n, &r, NULL)) return 0;
    return (int)r;
}
#else
static bool rd_input_pending(dap_t *d) {
    if (d->use_sock) {
        char b;
        return recv(d->sock, &b, 1, MSG_PEEK | MSG_DONTWAIT) > 0;
    }
    struct timeval tv = {0, 0};
    fd_set rf;
    FD_ZERO(&rf);
    FD_SET(0, &rf);
    return select(1, &rf, NULL, NULL, &tv) > 0;
}

static int rd_reader(void *ctx, char *buf, int n) {
    (void)ctx;
    return (int)read(0, buf, (size_t)n);
}
#endif

/* 语言服务与调试协议交互规范 */
static void dap_wait_hook(void *user) {
    dap_t *d = user;
    if (!rd_input_pending(d)) return;
    char *body = d->use_sock
        ? rpc_read_message_sock(d->sock)
        : rpc_read_message_cb(rd_reader, NULL, RPC_MAX_MESSAGE);
    if (!body) return; /* 核心系统底层抽象与内存语义契约 */
    json_value *msg = json_parse(body);
    if (!msg) {
        free(body);
        return;
    }
    const char *cmd = json_get_str(json_obj_get(msg, "command"));
    if (cmd && strcmp(cmd, "pause") == 0) {
        /* 内部辅助逻辑 */
        dap_send_response(d, msg, true, NULL);
        json_free(msg);
        free(body);
        dbg_interrupt(&d->dbg);
    } else if (g_parked_tail < DAP_PARK_MAX) {
        g_parked[g_parked_tail++] = body; /* 核心系统底层抽象与内存语义契约 */
        json_free(msg);
    } else {
        json_free(msg);
        free(body); /* 核心系统底层抽象与内存语义契约 */
    }
}

/* dispatch */

static void dispatch(dap_t *d, json_value *request) {
    const char *cmd = json_get_str(json_obj_get(request, "command"));
    if (!cmd) return;

    if (strcmp(cmd, "initialize") == 0)             handle_initialize(d, request);
    else if (strcmp(cmd, "setBreakpoints") == 0)    handle_set_breakpoints(d, request);
    else if (strcmp(cmd, "setExceptionBreakpoints") == 0) handle_set_exception_breakpoints(d, request);
    else if (strcmp(cmd, "launch") == 0)            handle_launch(d, request);
    else if (strcmp(cmd, "attach") == 0)            handle_attach(d, request);
    else if (strcmp(cmd, "configurationDone") == 0) handle_configuration_done(d, request);
    else if (strcmp(cmd, "threads") == 0)           handle_threads(d, request);
    else if (strcmp(cmd, "selectThread") == 0)      handle_select_thread(d, request);
    else if (strcmp(cmd, "stackTrace") == 0)        handle_stack_trace(d, request);
    else if (strcmp(cmd, "scopes") == 0)            handle_scopes(d, request);
    else if (strcmp(cmd, "variables") == 0)         handle_variables(d, request);
    else if (strcmp(cmd, "evaluate") == 0)          handle_evaluate(d, request);
    else if (strcmp(cmd, "setVariable") == 0)       handle_set_variable(d, request);
    else if (strcmp(cmd, "exceptionInfo") == 0)     handle_exception_info(d, request);
    else if (strcmp(cmd, "continue") == 0)          handle_continue(d, request);
    else if (strcmp(cmd, "next") == 0)              handle_next(d, request);
    else if (strcmp(cmd, "stepIn") == 0)            handle_step_in(d, request);
    else if (strcmp(cmd, "stepOut") == 0)           handle_step_out(d, request);
    else if (strcmp(cmd, "pause") == 0)             handle_pause(d, request);
    else if (strcmp(cmd, "terminate") == 0)         handle_disconnect(d, request);
    else if (strcmp(cmd, "disconnect") == 0)        handle_disconnect(d, request);
    else                                            dap_send_response(d, request, true, NULL);
}

/* Listen on 127 */
static dap_sock_t dap_listen_accept(int port) {
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return DAP_INVALID_SOCK;
#endif
    dap_sock_t ls = socket(AF_INET, SOCK_STREAM, 0);
    if (ls == DAP_INVALID_SOCK) return DAP_INVALID_SOCK;
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
        return DAP_INVALID_SOCK;
    }
    dap_sock_t cs = accept(ls, NULL, NULL);
#ifdef _WIN32
    closesocket(ls);
#else
    close(ls);
#endif
    return cs;
}

int main(int argc, char **argv) {
    int port = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--port") == 0 && i + 1 < argc)
            port = atoi(argv[++i]);
    }

    dap_t d;
    memset(&d, 0, sizeof(d));
    dbg_init(&d.dbg);
    d.out = stdout;
    d.seq = 1;
    g_dap = &d;
    dbg_set_wait_hook(&d.dbg, dap_wait_hook, &d);

    if (port > 0) {
        d.sock = dap_listen_accept(port);
        if (d.sock == DAP_INVALID_SOCK) {
            fprintf(stderr, "zan-dap: failed to listen on port %d\n", port);
            return 1;
        }
        d.use_sock = true;
    } else {
#ifdef _WIN32
        _setmode(_fileno(stdin), _O_BINARY);
        _setmode(_fileno(stdout), _O_BINARY);
#endif
    }

    for (;;) {
        char *body = take_parked();
        if (!body)
            body = d.use_sock ? rpc_read_message_sock(d.sock)
                              : rpc_read_message_cb(rd_reader, NULL, RPC_MAX_MESSAGE);
        if (!body) break;

        json_value *msg = json_parse(body);
        free(body);
        if (!msg) continue;

        const char *cmd = json_get_str(json_obj_get(msg, "command"));
        bool is_disconnect = cmd && (strcmp(cmd, "disconnect") == 0);

        dispatch(&d, msg);
        json_free(msg);

        if (is_disconnect) break;
    }

    dbg_stop(&d.dbg);
#ifdef _WIN32
    if (d.use_sock) { closesocket(d.sock); WSACleanup(); }
#else
    if (d.use_sock) close(d.sock);
#endif
    return 0;
}
