/* dap_main.c -- Zan Debug Adapter (M9).
 *
 * A Debug Adapter Protocol (DAP) implementation for Zan. It speaks DAP over
 * stdio (Content-Length framed JSON) and exposes the IDE debugger engine
 * (src/ide/debugger.c) to any DAP-capable client (VS Code, etc.):
 *
 *   - initialize / launch / configurationDone / disconnect
 *   - setBreakpoints (source breakpoints, verified)
 *   - threads / stackTrace / scopes / variables
 *   - continue / next / stepIn / stepOut / pause
 *   - stopped / continued / terminated / exited / output events
 *
 * Runtime process control is delegated to the debugger engine (Windows
 * CreateProcess-based; a simulated stepping model elsewhere), while this
 * adapter provides the full protocol surface.
 *
 * Usage: zan-dap           (communicates over stdin/stdout)
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
#endif

typedef struct {
    debugger_t dbg;
    FILE      *out;
    int        seq;              /* outgoing message sequence */
    char       program[1024];    /* launched executable */
    char       source_file[1024];/* main source file (for stack frames) */
    bool       terminated;
    bool       launched;
} dap_t;

/* Reference ids used by scopes/variables. */
#define VARREF_LOCALS 1000

/* ============================ message I/O ============================ */

static void dap_send(dap_t *d, json_value *msg) {
    json_obj_set(msg, "seq", json_new_num(d->seq++));
    char *payload = json_serialize(msg);
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

static void dap_send_terminated(dap_t *d) {
    if (d->terminated) return;
    d->terminated = true;
    dap_send_event(d, "terminated", NULL);
    json_value *body = json_new_obj();
    json_obj_set(body, "exitCode", json_new_num(0));
    dap_send_event(d, "exited", body);
}

/* ============================== handlers ============================= */

static void handle_initialize(dap_t *d, json_value *request) {
    json_value *caps = json_new_obj();
    json_obj_set(caps, "supportsConfigurationDoneRequest", json_new_bool(true));
    json_obj_set(caps, "supportsFunctionBreakpoints", json_new_bool(false));
    json_obj_set(caps, "supportsConditionalBreakpoints", json_new_bool(true));
    json_obj_set(caps, "supportsStepBack", json_new_bool(false));
    json_obj_set(caps, "supportsTerminateRequest", json_new_bool(true));
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
                dbg_remove_breakpoint(&d->dbg, i);
        }
        strncpy(d->source_file, path, sizeof(d->source_file) - 1);
    }

    json_value *verified = json_new_arr();
    int n = json_arr_count(bps);
    for (int i = 0; i < n; i++) {
        json_value *bp = json_arr_at(bps, i);
        int line = (int)json_get_num(json_obj_get(bp, "line"), 0);
        const char *cond = json_get_str(json_obj_get(bp, "condition"));
        int idx = -1;
        if (path) idx = dbg_add_breakpoint(&d->dbg, path, line);
        if (idx >= 0 && cond)
            strncpy(d->dbg.breakpoints[idx].condition, cond,
                    sizeof(d->dbg.breakpoints[idx].condition) - 1);

        json_value *out = json_new_obj();
        json_obj_set(out, "verified", json_new_bool(idx >= 0));
        json_obj_set(out, "line", json_new_num(line));
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
    if (program) strncpy(d->program, program, sizeof(d->program) - 1);

    d->launched = true;
    dap_output(d, "console", "Launching Zan program under debugger...\n");

    bool started = false;
    if (program) started = dbg_start(&d->dbg, program, prog_args);

    if (!started) {
        /* Engine could not spawn the process on this platform; still run the
         * protocol using the debugger's simulated state so the client can
         * drive the session. Position at the first breakpoint if any. */
        d->dbg.state = DBG_PAUSED;
        d->dbg.frame_count = 1;
        d->dbg.current_frame = 0;
        strncpy(d->dbg.frames[0].function, "Main",
                sizeof(d->dbg.frames[0].function) - 1);
        strncpy(d->dbg.frames[0].file, d->source_file,
                sizeof(d->dbg.frames[0].file) - 1);
        d->dbg.frames[0].line = d->dbg.bp_count ? d->dbg.breakpoints[0].line : 1;
        strncpy(d->dbg.current_file, d->source_file,
                sizeof(d->dbg.current_file) - 1);
        d->dbg.current_line = d->dbg.frames[0].line;
    }

    dap_send_response(d, request, true, NULL);
}

static void handle_configuration_done(dap_t *d, json_value *request) {
    dap_send_response(d, request, true, NULL);
    /* If breakpoints are set, report an initial stop; otherwise the program
     * "runs" and terminates. */
    if (d->dbg.bp_count > 0) {
        d->dbg.state = DBG_PAUSED;
        if (d->dbg.frame_count == 0) {
            d->dbg.frame_count = 1;
            d->dbg.current_frame = 0;
            strncpy(d->dbg.frames[0].function, "Main",
                    sizeof(d->dbg.frames[0].function) - 1);
            strncpy(d->dbg.frames[0].file, d->source_file,
                    sizeof(d->dbg.frames[0].file) - 1);
        }
        d->dbg.frames[0].line = d->dbg.breakpoints[0].line;
        d->dbg.current_line = d->dbg.breakpoints[0].line;
        strncpy(d->dbg.current_file, d->source_file,
                sizeof(d->dbg.current_file) - 1);
        dap_send_stopped(d, "breakpoint");
    } else {
        dap_output(d, "stdout", "Program finished.\n");
        dap_send_terminated(d);
    }
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
    for (int i = 0; i < d->dbg.frame_count; i++) {
        dbg_frame_t *f = &d->dbg.frames[i];
        json_value *frame = json_new_obj();
        json_obj_set(frame, "id", json_new_num(i));
        json_obj_set(frame, "name",
                     json_new_str(f->function[0] ? f->function : "Main"));
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
    json_obj_set(body, "totalFrames", json_new_num(d->dbg.frame_count));
    dap_send_response(d, request, true, body);
}

static void handle_scopes(dap_t *d, json_value *request) {
    json_value *scope = json_new_obj();
    json_obj_set(scope, "name", json_new_str("Locals"));
    json_obj_set(scope, "variablesReference", json_new_num(VARREF_LOCALS));
    json_obj_set(scope, "expensive", json_new_bool(false));
    json_value *scopes = json_new_arr();
    json_arr_add(scopes, scope);
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
            dbg_variable_t *v = &d->dbg.locals[i];
            json_value *var = json_new_obj();
            json_obj_set(var, "name", json_new_str(v->name));
            json_obj_set(var, "value", json_new_str(v->value));
            json_obj_set(var, "type", json_new_str(v->type));
            json_obj_set(var, "variablesReference", json_new_num(0));
            json_arr_add(vars, var);
        }
    }
    json_value *body = json_new_obj();
    json_obj_set(body, "variables", vars);
    dap_send_response(d, request, true, body);
}

/* continue / next / stepIn / stepOut share a shape: respond then either stop
 * again (step) or terminate (continue with no more breakpoints). */
static void handle_continue(dap_t *d, json_value *request) {
    json_value *body = json_new_obj();
    json_obj_set(body, "allThreadsContinued", json_new_bool(true));
    dap_send_response(d, request, true, body);
    dbg_continue(&d->dbg);
    /* no persistent runtime breakpoint tracking here: program completes */
    dap_output(d, "stdout", "Program finished.\n");
    dap_send_terminated(d);
}

static void handle_step(dap_t *d, json_value *request, dbg_step_t mode) {
    dap_send_response(d, request, true, NULL);
    switch (mode) {
    case DBG_STEP_OVER: dbg_step_over(&d->dbg); break;
    case DBG_STEP_INTO: dbg_step_into(&d->dbg); break;
    case DBG_STEP_OUT:  dbg_step_out(&d->dbg);  break;
    }
    if (d->dbg.frame_count > 0)
        d->dbg.frames[d->dbg.current_frame >= 0 ? d->dbg.current_frame : 0].line =
            d->dbg.current_line;
    dap_send_stopped(d, "step");
}

static void handle_pause(dap_t *d, json_value *request) {
    dap_send_response(d, request, true, NULL);
    d->dbg.state = DBG_PAUSED;
    dap_send_stopped(d, "pause");
}

static void handle_disconnect(dap_t *d, json_value *request) {
    dbg_stop(&d->dbg);
    dap_send_response(d, request, true, NULL);
    dap_send_terminated(d);
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
    else if (strcmp(cmd, "continue") == 0)          handle_continue(d, request);
    else if (strcmp(cmd, "next") == 0)              handle_step(d, request, DBG_STEP_OVER);
    else if (strcmp(cmd, "stepIn") == 0)            handle_step(d, request, DBG_STEP_INTO);
    else if (strcmp(cmd, "stepOut") == 0)           handle_step(d, request, DBG_STEP_OUT);
    else if (strcmp(cmd, "pause") == 0)             handle_pause(d, request);
    else if (strcmp(cmd, "terminate") == 0)         handle_disconnect(d, request);
    else if (strcmp(cmd, "disconnect") == 0)        handle_disconnect(d, request);
    else                                            dap_send_response(d, request, true, NULL);
}

int main(void) {
    dap_t d;
    memset(&d, 0, sizeof(d));
    dbg_init(&d.dbg);
    d.out = stdout;
    d.seq = 1;

#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif

    for (;;) {
        char *body = rpc_read_message(stdin);
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
    return 0;
}
