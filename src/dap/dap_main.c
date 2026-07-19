/* dap_main.c -- Zan Debug Adapter (M9).
 *
 * A Debug Adapter Protocol (DAP) implementation for Zan. It speaks DAP over
 * stdio (Content-Length framed JSON) and exposes the IDE debugger engine
 * (src/ide/debugger.c) to any DAP-capable client (VS Code, etc.):
 *
 *   - initialize / launch / configurationDone / disconnect
 *   - setBreakpoints (source breakpoints, verified, with conditions)
 *   - threads / stackTrace / scopes / variables
 *   - continue / next / stepIn / stepOut / pause
 *   - evaluate (watch expressions and hover evaluation)
 *   - setVariable (modify variables at runtime)
 *   - stopped / continued / terminated / exited / output events
 *
 * Enhanced features:
 *   - Conditional breakpoints with expression evaluation
 *   - Hit-count breakpoints
 *   - Logpoints (tracepoints)
 *   - Watch expression evaluation
 *   - Variable modification (setVariable)
 *   - Multiple scopes (Locals, Watch)
 *
 * Runtime process control is delegated to the debugger engine, which drives a
 * real gdb in machine-interface mode (the compiler emits DWARF via `zanc -g`),
 * so breakpoints, stepping, stack frames and variables are genuine.
 *
 * Usage:
 *   zan-dap                (communicates over stdin/stdout)
 *   zan-dap --port <N>     (listens on 127.0.0.1:<N> for a single DAP client)
 *
 * The TCP server mode exists for clients that cannot easily drive a child
 * process over bidirectional pipes (e.g. the self-hosted Zan IDE, which speaks
 * DAP through the standard-library TCP socket layer).
 */
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
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
typedef int dap_sock_t;
#define DAP_INVALID_SOCK (-1)
#endif

typedef struct {
    debugger_t dbg;
    FILE      *out;
    int        seq;              /* outgoing message sequence */
    char       program[1024];    /* launched executable */
    char       prog_args[1024];  /* inferior arguments */
    char       source_file[1024];/* main source file (for stack frames) */
    bool       terminated;
    bool       launched;
    bool       use_sock;         /* true when framing over a TCP socket */
    dap_sock_t sock;             /* connected client socket (server mode) */
} dap_t;

/* ---- TCP transport (server mode) -------------------------------------- */

static bool sock_send_all(dap_sock_t s, const char *buf, int n) {
    int sent = 0;
    while (sent < n) {
        int r = (int)send(s, buf + sent, n - sent, 0);
        if (r <= 0) return false;
        sent += r;
    }
    return true;
}

/* Byte-level transport callbacks over the client socket; the Content-Length
 * framing itself is shared with the stdio path (see common/rpc.c). */
static int sock_reader(void *ctx, char *buf, int n) {
    return (int)recv(*(dap_sock_t *)ctx, buf, n, 0);
}

static bool sock_writer(void *ctx, const char *buf, int n) {
    return sock_send_all(*(dap_sock_t *)ctx, buf, n);
}

/* Read one Content-Length framed message from the socket. Returns a malloc'd
 * NUL-terminated body, or NULL on disconnect. */
static char *rpc_read_message_sock(dap_sock_t s) {
    return rpc_read_message_cb(sock_reader, &s, 64 * 1024 * 1024);
}

static void rpc_write_message_sock(dap_sock_t s, const char *payload) {
    rpc_write_message_cb(sock_writer, &s, payload);
}

/* Reference ids used by scopes/variables. */
#define VARREF_LOCALS  1000
#define VARREF_WATCHES 2000

/* ============================ message I/O ============================ */

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

/* Emit a "stopped" event for the single thread. */
static void dap_send_stopped(dap_t *d, const char *reason) {
    json_value *body = json_new_obj();
    json_obj_set(body, "reason", json_new_str(reason));
    json_obj_set(body, "threadId", json_new_num(1));
    json_obj_set(body, "allThreadsStopped", json_new_bool(true));
    dap_send_event(d, "stopped", body);
}

/* Flush any buffered engine output (inferior stdout + debug notes) to the
 * client's debug console, then reset the buffer. */
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

/* Map a gdb/MI stop reason to a DAP `stopped` reason. */
static const char *dap_stop_reason(const char *mi) {
    if (!mi || !mi[0]) return "breakpoint";
    if (strncmp(mi, "breakpoint", 10) == 0) return "breakpoint";
    if (strstr(mi, "stepping-range") || strstr(mi, "finished")) return "step";
    if (strstr(mi, "watchpoint")) return "data breakpoint";
    if (strstr(mi, "signal")) return "exception";
    return "breakpoint";
}

/* After an execution command: report the resulting stop or termination. */
static void dap_report_stop(dap_t *d) {
    dap_flush_output(d);
    if (d->dbg.state == DBG_PAUSED)
        dap_send_stopped(d, dap_stop_reason(d->dbg.stop_reason));
    else
        dap_terminate_with_code(d, d->dbg.last_exit_code);
}

/* ============================== handlers ============================= */

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
    dap_send_response(d, request, true, caps);
    /* signal readiness for configuration (breakpoints, etc.) */
    dap_send_event(d, "initialized", NULL);
}

static void handle_set_breakpoints(dap_t *d, json_value *request) {
    json_value *args = json_obj_get(request, "arguments");
    json_value *source = json_obj_get(args, "source");
    const char *path = json_get_str(json_obj_get(source, "path"));
    json_value *bps = json_obj_get(args, "breakpoints");

    if (path) {
        /* drop existing breakpoints for this file */
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

        int id = -1;
        if (path) {
            if (log_msg && log_msg[0]) {
                /* Logpoint */
                id = dbg_add_logpoint(&d->dbg, path, line, log_msg);
            } else if (hit_cond && hit_cond[0]) {
                /* Hit-count breakpoint */
                int count = atoi(hit_cond);
                if (count > 0)
                    id = dbg_add_hitcount_bp(&d->dbg, path, line, count);
                else
                    id = dbg_add_breakpoint(&d->dbg, path, line);
            } else if (cond && cond[0]) {
                /* Conditional breakpoint */
                id = dbg_add_conditional_bp(&d->dbg, path, line, cond);
            } else {
                /* Normal breakpoint */
                id = dbg_add_breakpoint(&d->dbg, path, line);
            }
        }

        json_value *out = json_new_obj();
        json_obj_set(out, "id", json_new_num(id >= 0 ? id : i));
        json_obj_set(out, "verified", json_new_bool(id >= 0));
        json_obj_set(out, "line", json_new_num(line));
        if (cond && cond[0])
            json_obj_set(out, "message", json_new_str(cond));
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

    /* The program is actually started at configurationDone, once the client
     * has delivered its breakpoints. */
    dap_send_response(d, request, true, NULL);
}

static void handle_configuration_done(dap_t *d, json_value *request) {
    dap_send_response(d, request, true, NULL);
    /* Breakpoints have now been delivered; launch the inferior under gdb. */
    dbg_start(&d->dbg, d->program, d->prog_args[0] ? d->prog_args : NULL);
    dap_report_stop(d);
}

static void handle_threads(dap_t *d, json_value *request) {
    json_value *thread = json_new_obj();
    json_obj_set(thread, "id", json_new_num(1));
    json_obj_set(thread, "name", json_new_str("main"));
    json_value *threads = json_new_arr();
    json_arr_add(threads, thread);
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

    /* Locals scope */
    json_value *locals_scope = json_new_obj();
    json_obj_set(locals_scope, "name", json_new_str("Locals"));
    json_obj_set(locals_scope, "variablesReference", json_new_num(VARREF_LOCALS));
    json_obj_set(locals_scope, "expensive", json_new_bool(false));
    json_obj_set(locals_scope, "presentationHint", json_new_str("locals"));
    json_arr_add(scopes, locals_scope);

    /* Watch scope (if there are watches) */
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
    } else if (ref == VARREF_WATCHES) {
        /* Return watch expression values */
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

/* NEW: Evaluate expression (watch, hover, repl) */
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

    /* If this is a "watch" context, add the watch expression */
    if (context_str && strcmp(context_str, "watch") == 0) {
        /* ensure it's in the watch list */
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

/* NEW: Set variable value */
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

/* NEW: Exception info request */
static void handle_exception_info(dap_t *d, json_value *request) {
    char info[256] = {0};
    bool has_info = dbg_get_exception_info(&d->dbg, info, sizeof(info));

    json_value *body = json_new_obj();
    json_obj_set(body, "exceptionId", json_new_str(has_info ? info : "unknown"));
    json_obj_set(body, "breakMode", json_new_str("always"));
    dap_send_response(d, request, true, body);
}

/* continue / next / stepIn / stepOut share a shape */
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

static void handle_pause(dap_t *d, json_value *request) {
    dap_send_response(d, request, true, NULL);
    /* Synchronous MI backend has no async-interrupt path yet; acknowledge. */
    dap_send_stopped(d, "pause");
}

static void handle_disconnect(dap_t *d, json_value *request) {
    dbg_stop(&d->dbg);
    dap_send_response(d, request, true, NULL);
    dap_terminate_with_code(d, d->dbg.last_exit_code);
}

/* ============================== dispatch ============================= */

static void dispatch(dap_t *d, json_value *request) {
    const char *cmd = json_get_str(json_obj_get(request, "command"));
    if (!cmd) return;

    if (strcmp(cmd, "initialize") == 0)             handle_initialize(d, request);
    else if (strcmp(cmd, "setBreakpoints") == 0)    handle_set_breakpoints(d, request);
    else if (strcmp(cmd, "setExceptionBreakpoints") == 0) dap_send_response(d, request, true, NULL);
    else if (strcmp(cmd, "launch") == 0)            handle_launch(d, request);
    else if (strcmp(cmd, "attach") == 0)            handle_launch(d, request);
    else if (strcmp(cmd, "configurationDone") == 0) handle_configuration_done(d, request);
    else if (strcmp(cmd, "threads") == 0)           handle_threads(d, request);
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

/* Listen on 127.0.0.1:port and accept a single client. Returns the connected
 * socket, or DAP_INVALID_SOCK on failure. */
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
        char *body = d.use_sock ? rpc_read_message_sock(d.sock)
                                : rpc_read_message(stdin);
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
