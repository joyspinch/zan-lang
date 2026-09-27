/* debugger.c -- Integrated debugger implementation.
 *
 * Enhanced with:
 *   - Conditional breakpoints with expression evaluation
 *   - Hit-count breakpoints
 *   - Watch expression evaluation
 *   - Local variable inspection
 *   - Call stack management
 *   - Logpoint support
 *   - Debug output panel
 */
#include "debugger.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#ifdef _WIN32
#include <windows.h>
#include <tlhelp32.h>
#else
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <errno.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#endif

/* ======================================================================
 * GDB/MI backend
 *
 * The real debugger is driven by spawning gdb in machine-interface mode
 * (`gdb --interpreter=mi2`) and exchanging MI records over redirected
 * pipes. The compiler emits DWARF (`zanc -g`), so gdb maps native stops
 * back to Zan source lines, frames and variables. All process control,
 * stepping, stack/locals/watch inspection below is real gdb, not a model.
 * ==================================================================== */

static bool mi_active(debugger_t *dbg) {
#ifdef _WIN32
    return dbg->gdb_proc != NULL;
#else
    return dbg->gdb_pid > 0;
#endif
}

/* Basename of a path (handles both slash flavours). */
static const char *mi_basename(const char *path) {
    const char *b = path;
    for (const char *p = path; *p; p++)
        if (*p == '/' || *p == '\\') b = p + 1;
    return b;
}

/* Extract a quoted MI field: finds `key="` and copies the (un-escaped) string
 * up to the next unescaped quote into `out`. Returns false if absent. */
static bool mi_field(const char *rec, const char *key, char *out, int size) {
    char pat[80];
    snprintf(pat, sizeof(pat), "%s=\"", key);
    const char *s = strstr(rec, pat);
    if (!s) return false;
    s += strlen(pat);
    int n = 0;
    while (*s && *s != '"') {
        char c = *s;
        if (c == '\\' && s[1]) { s++; c = *s; }
        if (n < size - 1) out[n++] = c;
        s++;
    }
    out[n] = '\0';
    return true;
}

static void mi_raw_write(debugger_t *dbg, const char *s) {
    size_t len = strlen(s);
#ifdef _WIN32
    DWORD wr = 0;
    WriteFile((HANDLE)dbg->gdb_in_w, s, (DWORD)len, &wr, NULL);
#else
    ssize_t r = write(dbg->gdb_in_fd, s, len); (void)r;
#endif
}

/* Read a single '\n'-terminated line from gdb's stdout into `out` (newline
 * stripped, surplus bytes stay buffered in mi_buf). With `poll` the call does
 * not block: it waits up to ~15ms for gdb output, gives the registered wait
 * hook a chance to run (so a single-threaded adapter can service client
 * requests — `pause` — while the target runs), and returns 0 when no complete
 * line is available yet.
 * Returns 1 = line delivered, 0 = nothing yet (only in poll mode), -1 = EOF.
 * A partial line still pending at EOF is delivered as a final line. */
static int mi_read_line_ex(debugger_t *dbg, char *out, int out_size, bool poll) {
    for (;;) {
        /* flush a complete line already buffered */
        for (int i = 0; i < dbg->mi_buf_len; i++) {
            if (dbg->mi_buf[i] != '\n') continue;
            int len = i;
            if (len > 0 && dbg->mi_buf[len - 1] == '\r') len--;
            if (len > out_size - 1) len = out_size - 1;
            memcpy(out, dbg->mi_buf, (size_t)len);
            out[len] = '\0';
            int rest = dbg->mi_buf_len - (i + 1);
            memmove(dbg->mi_buf, dbg->mi_buf + i + 1, (size_t)rest);
            dbg->mi_buf_len = rest;
            return 1;
        }
        if (dbg->mi_buf_len >= (int)sizeof(dbg->mi_buf)) {
            /* buffer full with no newline: drop and resync — the record was
             * longer than any caller's line buffer anyway */
            dbg->mi_buf_len = 0;
        }
        char chunk[4096];
        int got = 0;
#ifdef _WIN32
        if (poll) {
            DWORD avail = 0;
            if (!PeekNamedPipe((HANDLE)dbg->gdb_out_r, NULL, 0, NULL, &avail, NULL))
                got = -1;                  /* broken pipe */
            else if (avail == 0) {
                if (dbg->wait_hook) dbg->wait_hook(dbg->wait_hook_user);
                Sleep(10);
                continue;
            }
        }
        if (got == 0) {
            DWORD gotw = 0;
            if (!ReadFile((HANDLE)dbg->gdb_out_r, chunk, sizeof(chunk), &gotw, NULL))
                got = -1;
            else
                got = (int)gotw;
        }
#else
        if (poll) {
            fd_set rf;
            FD_ZERO(&rf);
            FD_SET(dbg->gdb_out_fd, &rf);
            struct timeval tv = {0, 15000};
            if (select(dbg->gdb_out_fd + 1, &rf, NULL, NULL, &tv) <= 0) {
                if (dbg->wait_hook) dbg->wait_hook(dbg->wait_hook_user);
                continue;
            }
        }
        ssize_t r = read(dbg->gdb_out_fd, chunk, sizeof(chunk));
        got = r > 0 ? (int)r : -1;
#endif
        if (got <= 0) {
            /* EOF: a partial line still pending is delivered as the last one */
            if (dbg->mi_buf_len > 0) {
                int len = dbg->mi_buf_len;
                if (len > out_size - 1) len = out_size - 1;
                memcpy(out, dbg->mi_buf, (size_t)len);
                out[len] = '\0';
                dbg->mi_buf_len = 0;
                return 1;
            }
            return -1;
        }
        int n = got;
        int cap = (int)sizeof(dbg->mi_buf) - dbg->mi_buf_len;
        if (n > cap) n = cap;
        if (n <= 0) continue;
        memcpy(dbg->mi_buf + dbg->mi_buf_len, chunk, (size_t)n);
        dbg->mi_buf_len += n;
    }
}

/* Blocking line read (command round-trips). */
static bool mi_read_line(debugger_t *dbg, char *out, int out_size) {
    return mi_read_line_ex(dbg, out, out_size, false) == 1;
}

/* Forward an MI stream record body (a c-string like  "text\n" ) to output. */
static void mi_forward_stream(debugger_t *dbg, const char *body) {
    char buf[2048];
    int n = 0;
    const char *s = body;
    if (*s == '"') s++;
    while (*s && *s != '"' && n < (int)sizeof(buf) - 1) {
        char c = *s;
        if (c == '\\' && s[1]) {
            s++;
            switch (*s) { case 'n': c = '\n'; break; case 't': c = '\t'; break;
                          case 'r': c = '\r'; break; default: c = *s; }
        }
        buf[n++] = c;
        s++;
    }
    buf[n] = '\0';
    if (n) dbg_append_output(dbg, buf);
}

/* Parse a `*stopped,...` async record: update state + current location. */
static void mi_handle_stopped(debugger_t *dbg, const char *rec) {
    char reason[64] = "";
    mi_field(rec, "reason", reason, sizeof(reason));
    if (strncmp(reason, "exited", 6) == 0) {
        char ec[16] = "";
        dbg->last_exit_code = mi_field(rec, "exit-code", ec, sizeof(ec))
                              ? (int)strtol(ec, NULL, 0) : 0;
        dbg->interrupt_requested = false;
        dbg->state = DBG_TERMINATED;
        return;
    }
    dbg->state = DBG_PAUSED;
    if (reason[0])
        snprintf(dbg->stop_reason, sizeof(dbg->stop_reason), "%s", reason);
    else
        /* an interrupt stop often carries no reason at all */
        snprintf(dbg->stop_reason, sizeof(dbg->stop_reason), "signal-received");
    if (dbg->interrupt_requested) {
        /* we asked for this one: it is a pause, not a signal hit */
        snprintf(dbg->stop_reason, sizeof(dbg->stop_reason), "pause");
        dbg->interrupt_requested = false;
    }
    char file[512] = "", line[16] = "";
    if (mi_field(rec, "fullname", file, sizeof(file)) ||
        mi_field(rec, "file", file, sizeof(file)))
        snprintf(dbg->current_file, sizeof(dbg->current_file), "%s", file);
    if (mi_field(rec, "line", line, sizeof(line)))
        dbg->current_line = atoi(line);
}

static void mi_dispatch_line(debugger_t *dbg, const char *line) {
    if (line[0] == '*') {
        if (strncmp(line + 1, "stopped", 7) == 0) mi_handle_stopped(dbg, line);
    } else if (line[0] == '@') {
        mi_forward_stream(dbg, line + 1);
    } else if (line[0] == '=') {
        /* =thread-group-started,id="i1",pid="4242": remember the inferior so
         * dbg_interrupt can reach the debuggee, not gdb itself */
        if (strncmp(line + 1, "thread-group-started", 20) == 0) {
            char pid[32] = "";
            if (mi_field(line, "pid", pid, sizeof(pid)))
                dbg->inferior_pid = atoi(pid);
        }
    }
    /* '~' console, '&' log, '+' status: ignored as gdb chatter */
}

/* Pump gdb output until the result record for `token` arrives, copying it into
 * `result`. Dispatches async records + inferior output meanwhile. */
static bool mi_pump(debugger_t *dbg, int token, char *result, int result_size) {
    char line[8192];
    while (mi_read_line(dbg, line, sizeof(line))) {
        if (line[0] == '\0') continue;
        const char *p = line;
        int t = 0; bool has_tok = false;
        while (*p >= '0' && *p <= '9') { t = t * 10 + (*p - '0'); p++; has_tok = true; }
        if (*p == '^') {
            if (has_tok && t != token) continue;
            if (result) { strncpy(result, line, (size_t)result_size - 1); result[result_size - 1] = '\0'; }
            return true;
        }
        if (line[0] == '*' || line[0] == '@' || line[0] == '=') {
            mi_dispatch_line(dbg, line);
            continue;
        }
        if (line[0] == '~' || line[0] == '&' || line[0] == '+' || line[0] == '^') continue;
        if (strncmp(line, "(gdb)", 5) == 0) continue;
        /* Unrecognised line: inferior stdout (local gdb does not wrap it). */
        { char buf[8200]; snprintf(buf, sizeof(buf), "%s\n", line); dbg_append_output(dbg, buf); }
    }
    return false;
}

/* Send an MI command and wait for its result record. */
static bool mi_command(debugger_t *dbg, const char *cmd, char *result, int result_size) {
    if (!mi_active(dbg)) return false;
    int tok = ++dbg->mi_token;
    char buf[1400];
    snprintf(buf, sizeof(buf), "%d%s\n", tok, cmd);
    mi_raw_write(dbg, buf);
    return mi_pump(dbg, tok, result, result_size);
}

/* Read until the next `*stopped` (or EOF/termination). Polls gdb output so
 * the adapter's wait hook runs while the target runs. */
static bool mi_wait_stopped(debugger_t *dbg) {
    char line[8192];
    for (;;) {
        int r = mi_read_line_ex(dbg, line, sizeof(line), true);
        if (r < 0) {
            dbg->state = DBG_TERMINATED;
            return false;
        }
        if (r == 0) continue;   /* hook already had its chance */
        if (line[0] == '*' || line[0] == '@') {
            mi_dispatch_line(dbg, line);
            if (line[0] == '*' && strncmp(line + 1, "stopped", 7) == 0) return true;
        } else if (line[0] == '~' || line[0] == '&' || line[0] == '=' ||
                   line[0] == '+' || line[0] == '^' || strncmp(line, "(gdb)", 5) == 0) {
            /* MI chatter / result record: skip */
        } else {
            char buf[8200]; snprintf(buf, sizeof(buf), "%s\n", line);
            dbg_append_output(dbg, buf);
        }
        if (dbg->state == DBG_TERMINATED) return true;
    }
}

/* True when at least one stack frame carries DWARF source info from a Zan
 * source, i.e. the selected thread is stopped in program code rather than in
 * CRT/system code (the mingw CRT contributes source-bearing frames too, but
 * they are all .c files). */
static bool mi_zan_frame(debugger_t *dbg) {
    for (int i = 0; i < dbg->callstack_depth; i++) {
        const char *f = dbg->callstack[i].file;
        size_t n = strlen(f);
        if (n < 4) continue;
        const char *s = f + n - 4;
        if (s[0] == '.' && (s[1] == 'z' || s[1] == 'Z') &&
            (s[2] == 'a' || s[2] == 'A') && (s[3] == 'n' || s[3] == 'N'))
            return true;
    }
    return false;
}

/* A pause (DebugBreakProcess / SIGINT) can stop on an injected or foreign
 * thread whose stack has no Zan frames. Walk the thread list and settle on
 * the first thread stopped in Zan code so the pause shows the program's own
 * location. */
static void mi_pick_zan_thread(debugger_t *dbg) {
    if (mi_zan_frame(dbg)) return; /* already stopped in program code */
    int saved = dbg->current_thread;
    for (int i = 0; i < dbg->thread_count; i++) {
        int tid = dbg->threads[i].id;
        if (tid == dbg->current_thread || tid <= 0) continue;
        char cmd[48], res[512] = "";
        snprintf(cmd, sizeof(cmd), "-thread-select %d", tid);
        if (!mi_command(dbg, cmd, res, sizeof(res)) || strstr(res, "^error"))
            continue;
        dbg->current_thread = tid;
        dbg_refresh_callstack(dbg);
        if (mi_zan_frame(dbg)) return;
    }
    dbg->current_thread = saved;
}

/* Refresh the paused view after a stop: threads, stack, locals, watches —
 * and untangle compiler-emitted exception-hook frames (a stop inside
 * __zan_eh_* is an exception, not an ordinary breakpoint). */
static void mi_after_stop(debugger_t *dbg) {
    dbg_refresh_threads(dbg);
    dbg_refresh_callstack(dbg);
    if (dbg->thread_count > 1) mi_pick_zan_thread(dbg);
    dbg->active_frame = 0;
    if (dbg->callstack_depth > 0 &&
        strncmp(dbg->callstack[0].function_name, "__zan_eh_", 9) == 0) {
        bool unhandled = strstr(dbg->callstack[0].function_name,
                                "unhandled") != NULL;
        snprintf(dbg->stop_reason, sizeof(dbg->stop_reason), "%s",
                 unhandled ? "unhandled exception" : "exception thrown");
        for (int i = 1; i < dbg->callstack_depth; i++)
            dbg->callstack[i - 1] = dbg->callstack[i];
        dbg->callstack_depth--;
        if (dbg->callstack_depth > 0) {
            snprintf(dbg->current_file, sizeof(dbg->current_file), "%s",
                     dbg->callstack[0].file);
            dbg->current_line = dbg->callstack[0].line;
        }
        char sel[64], tmp[512];
        snprintf(sel, sizeof(sel), "-stack-select-frame 1");
        mi_command(dbg, sel, tmp, sizeof(tmp));
    }
    dbg_refresh_locals(dbg);
    dbg_evaluate_watches(dbg);
}

/* Run an execution command (-exec-continue/next/step/finish), wait for the
 * resulting stop, then refresh the paused view. */
static void mi_exec(debugger_t *dbg, const char *cmd) {
    if (!mi_active(dbg) || dbg->state == DBG_TERMINATED) return;
    char res[512] = "";
    dbg->state = DBG_RUNNING;
    if (!mi_command(dbg, cmd, res, sizeof(res))) { dbg->state = DBG_TERMINATED; return; }
    if (strstr(res, "^error")) {
        char msg[256] = "";
        if (mi_field(res, "msg", msg, sizeof(msg))) {
            char out[300]; snprintf(out, sizeof(out), "[DBG] %s\n", msg);
            dbg_append_output(dbg, out);
        }
        dbg->state = DBG_PAUSED;
        return;
    }
    mi_wait_stopped(dbg);
    if (dbg->state == DBG_PAUSED)
        mi_after_stop(dbg);
}

/* Directory holding the running executable (zan-dap), with no trailing sep. */
static void mi_exe_dir(char *out, size_t outsz) {
    out[0] = '\0';
#ifdef _WIN32
    GetModuleFileNameA(NULL, out, (DWORD)outsz);
    { char *s = strrchr(out, '\\'); if (s) *s = '\0'; }
#elif defined(__APPLE__)
    { uint32_t sz = (uint32_t)outsz;
      if (_NSGetExecutablePath(out, &sz) != 0) out[0] = '\0';
      char *s = strrchr(out, '/'); if (s) *s = '\0'; }
#else
    { ssize_t n = readlink("/proc/self/exe", out, outsz - 1);
      if (n > 0) { out[n] = '\0'; char *s = strrchr(out, '/'); if (s) *s = '\0'; } }
#endif
}

static bool mi_file_exists(const char *path) {
    FILE *f = fopen(path, "rb");
    if (f) { fclose(f); return true; }
    return false;
}

/* Resolve the gdb executable to use. Preference order: explicit path set by
 * the adapter, the ZAN_GDB env override, a gdb bundled inside the toolchain
 * next to zan-dap (so a published IDE debugs without any system install),
 * then platform-known locations, then bare "gdb" on PATH. */
static void mi_resolve_gdb(debugger_t *dbg, char *out, int size) {
    if (dbg->gdb_path[0]) { snprintf(out, (size_t)size, "%s", dbg->gdb_path); return; }
    const char *env = getenv("ZAN_GDB");
    if (env && env[0]) { snprintf(out, (size_t)size, "%s", env); return; }

    char dir[1024];
    mi_exe_dir(dir, sizeof(dir));
    if (dir[0]) {
#ifdef _WIN32
        const char *rel[] = { "\\gdb.exe", "\\debugger\\bin\\gdb.exe", "\\debugger\\gdb.exe" };
#else
        const char *rel[] = { "/gdb", "/debugger/bin/gdb", "/debugger/gdb" };
#endif
        for (size_t i = 0; i < sizeof(rel) / sizeof(rel[0]); i++) {
            char cand[1200];
            snprintf(cand, sizeof(cand), "%s%s", dir, rel[i]);
            if (mi_file_exists(cand)) { snprintf(out, (size_t)size, "%s", cand); return; }
        }
    }
#ifdef _WIN32
    const char *known = "C:\\TDM-GCC-64\\bin\\gdb.exe";
    if (mi_file_exists(known)) { snprintf(out, (size_t)size, "%s", known); return; }
    snprintf(out, (size_t)size, "gdb.exe");
#else
    snprintf(out, (size_t)size, "gdb");
#endif
}

/* Spawn gdb with redirected stdin/stdout. Returns false on failure. */
static bool mi_spawn(debugger_t *dbg, const char *program) {
    char gdb[512];
    mi_resolve_gdb(dbg, gdb, sizeof(gdb));
    dbg->mi_buf_len = 0;
    dbg->mi_token = 0;
#ifdef _WIN32
    SECURITY_ATTRIBUTES sa = {0};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    HANDLE in_r = NULL, in_w = NULL, out_r = NULL, out_w = NULL;
    if (!CreatePipe(&in_r, &in_w, &sa, 0)) return false;
    if (!CreatePipe(&out_r, &out_w, &sa, 0)) { CloseHandle(in_r); CloseHandle(in_w); return false; }
    SetHandleInformation(in_w, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(out_r, HANDLE_FLAG_INHERIT, 0);

    char cmd[2048];
    snprintf(cmd, sizeof(cmd),
             "\"%s\" --interpreter=mi2 -q -nx \"%s\"", gdb, program);

    STARTUPINFOA si = {0};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = in_r;
    si.hStdOutput = out_w;
    si.hStdError = out_w;
    PROCESS_INFORMATION pi = {0};
    BOOL ok = CreateProcessA(NULL, cmd, NULL, NULL, TRUE,
                             CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
    CloseHandle(in_r);
    CloseHandle(out_w);
    if (!ok) { CloseHandle(in_w); CloseHandle(out_r); return false; }
    CloseHandle(pi.hThread);
    dbg->gdb_in_w = in_w;
    dbg->gdb_out_r = out_r;
    dbg->gdb_proc = pi.hProcess;
    dbg->process_id = pi.dwProcessId;
    return true;
#else
    int in_pipe[2], out_pipe[2];
    if (pipe(in_pipe) != 0) return false;
    if (pipe(out_pipe) != 0) { close(in_pipe[0]); close(in_pipe[1]); return false; }
    pid_t pid = fork();
    if (pid < 0) { close(in_pipe[0]); close(in_pipe[1]); close(out_pipe[0]); close(out_pipe[1]); return false; }
    if (pid == 0) {
        dup2(in_pipe[0], 0);
        dup2(out_pipe[1], 1);
        dup2(out_pipe[1], 2);
        close(in_pipe[0]); close(in_pipe[1]);
        close(out_pipe[0]); close(out_pipe[1]);
        execlp(gdb, gdb, "--interpreter=mi2", "-q", "-nx", program, (char *)NULL);
        _exit(127);
    }
    close(in_pipe[0]);
    close(out_pipe[1]);
    dbg->gdb_in_fd = in_pipe[1];
    dbg->gdb_out_fd = out_pipe[0];
    dbg->gdb_pid = pid;
    return true;
#endif
}

static void mi_close(debugger_t *dbg) {
    if (!mi_active(dbg)) return;
    mi_raw_write(dbg, "-gdb-exit\n");
#ifdef _WIN32
    WaitForSingleObject((HANDLE)dbg->gdb_proc, 2000);
    TerminateProcess((HANDLE)dbg->gdb_proc, 0);
    CloseHandle((HANDLE)dbg->gdb_in_w);
    CloseHandle((HANDLE)dbg->gdb_out_r);
    CloseHandle((HANDLE)dbg->gdb_proc);
    dbg->gdb_in_w = dbg->gdb_out_r = dbg->gdb_proc = NULL;
#else
    close(dbg->gdb_in_fd);
    close(dbg->gdb_out_fd);
    int st; waitpid(dbg->gdb_pid, &st, 0);
    dbg->gdb_in_fd = dbg->gdb_out_fd = -1;
    dbg->gdb_pid = 0;
#endif
}

void dbg_set_gdb_path(debugger_t *dbg, const char *path) {
    if (path) snprintf(dbg->gdb_path, sizeof(dbg->gdb_path), "%s", path);
}

void dbg_init(debugger_t *dbg) {
    memset(dbg, 0, sizeof(debugger_t));
    dbg->state = DBG_IDLE;
    dbg->bp_next_id = 1;
    dbg->break_on_entry = false;
    dbg->break_on_exception = true;
    dbg->skip_stdlib = true;
    dbg->active_frame = 0;
    dbg->current_thread = 1;
    dbg->exc_bp_throw = -1;
    dbg->exc_bp_unhandled = -1;
#ifndef _WIN32
    dbg->gdb_in_fd = -1;
    dbg->gdb_out_fd = -1;
#endif
}

/* --- Breakpoint management --- */

int dbg_add_breakpoint(debugger_t *dbg, const char *file, int line) {
    if (dbg->bp_count >= DBG_MAX_BREAKPOINTS) return -1;

    /* check for duplicate */
    for (int i = 0; i < dbg->bp_count; i++) {
        if (dbg->breakpoints[i].line == line &&
            strcmp(dbg->breakpoints[i].file, file) == 0) {
            return dbg->breakpoints[i].id; /* already exists */
        }
    }

    dbg_breakpoint_t *bp = &dbg->breakpoints[dbg->bp_count++];
    memset(bp, 0, sizeof(dbg_breakpoint_t));
    strncpy(bp->file, file, sizeof(bp->file) - 1);
    bp->line = line;
    bp->enabled = true;
    bp->verified = false;
    bp->id = dbg->bp_next_id++;
    bp->type = BP_NORMAL;

    dbg_append_output(dbg, "[DBG] Breakpoint set");
    char msg[128];
    snprintf(msg, sizeof(msg), " at line %d\n", line + 1);
    dbg_append_output(dbg, msg);

    return bp->id;
}

int dbg_add_conditional_bp(debugger_t *dbg, const char *file, int line,
                           const char *condition) {
    int id = dbg_add_breakpoint(dbg, file, line);
    if (id < 0) return -1;

    /* Find the breakpoint and set condition */
    for (int i = 0; i < dbg->bp_count; i++) {
        if (dbg->breakpoints[i].id == id) {
            dbg->breakpoints[i].type = BP_CONDITIONAL;
            strncpy(dbg->breakpoints[i].condition, condition,
                    sizeof(dbg->breakpoints[i].condition) - 1);
            char msg[384];
            snprintf(msg, sizeof(msg), "[DBG] Conditional breakpoint at line %d: %s\n",
                    line + 1, condition);
            dbg_append_output(dbg, msg);
            break;
        }
    }
    return id;
}

int dbg_add_hitcount_bp(debugger_t *dbg, const char *file, int line, int count) {
    int id = dbg_add_breakpoint(dbg, file, line);
    if (id < 0) return -1;

    for (int i = 0; i < dbg->bp_count; i++) {
        if (dbg->breakpoints[i].id == id) {
            dbg->breakpoints[i].type = BP_HITCOUNT;
            dbg->breakpoints[i].hit_count_target = count;
            dbg->breakpoints[i].hit_count = 0;
            char msg[128];
            snprintf(msg, sizeof(msg), "[DBG] Hit-count breakpoint at line %d (count: %d)\n",
                    line + 1, count);
            dbg_append_output(dbg, msg);
            break;
        }
    }
    return id;
}

int dbg_add_logpoint(debugger_t *dbg, const char *file, int line,
                     const char *message) {
    int id = dbg_add_breakpoint(dbg, file, line);
    if (id < 0) return -1;

    for (int i = 0; i < dbg->bp_count; i++) {
        if (dbg->breakpoints[i].id == id) {
            dbg->breakpoints[i].type = BP_LOGPOINT;
            strncpy(dbg->breakpoints[i].log_message, message,
                    sizeof(dbg->breakpoints[i].log_message) - 1);
            char msg[384];
            snprintf(msg, sizeof(msg), "[DBG] Logpoint at line %d: \"%s\"\n",
                    line + 1, message);
            dbg_append_output(dbg, msg);
            break;
        }
    }
    return id;
}

bool dbg_remove_breakpoint(debugger_t *dbg, int bp_id) {
    for (int i = 0; i < dbg->bp_count; i++) {
        if (dbg->breakpoints[i].id == bp_id) {
            char msg[128];
            snprintf(msg, sizeof(msg), "[DBG] Breakpoint %d removed (line %d)\n",
                    bp_id, dbg->breakpoints[i].line + 1);
            dbg_append_output(dbg, msg);

            /* shift remaining */
            for (int j = i; j < dbg->bp_count - 1; j++)
                dbg->breakpoints[j] = dbg->breakpoints[j + 1];
            dbg->bp_count--;
            return true;
        }
    }
    return false;
}

bool dbg_remove_breakpoint_at(debugger_t *dbg, const char *file, int line) {
    for (int i = 0; i < dbg->bp_count; i++) {
        if (dbg->breakpoints[i].line == line &&
            strcmp(dbg->breakpoints[i].file, file) == 0) {
            return dbg_remove_breakpoint(dbg, dbg->breakpoints[i].id);
        }
    }
    return false;
}

int dbg_toggle_breakpoint(debugger_t *dbg, const char *file, int line) {
    for (int i = 0; i < dbg->bp_count; i++) {
        if (dbg->breakpoints[i].line == line &&
            strcmp(dbg->breakpoints[i].file, file) == 0) {
            dbg_remove_breakpoint(dbg, dbg->breakpoints[i].id);
            return -1;
        }
    }
    return dbg_add_breakpoint(dbg, file, line);
}

void dbg_enable_breakpoint(debugger_t *dbg, int bp_id, bool enabled) {
    for (int i = 0; i < dbg->bp_count; i++) {
        if (dbg->breakpoints[i].id == bp_id) {
            dbg->breakpoints[i].enabled = enabled;
            char msg[128];
            snprintf(msg, sizeof(msg), "[DBG] Breakpoint %d %s\n",
                    bp_id, enabled ? "enabled" : "disabled");
            dbg_append_output(dbg, msg);
            break;
        }
    }
}

void dbg_set_bp_condition(debugger_t *dbg, int bp_id, const char *condition) {
    for (int i = 0; i < dbg->bp_count; i++) {
        if (dbg->breakpoints[i].id == bp_id) {
            dbg->breakpoints[i].type = condition[0] ? BP_CONDITIONAL : BP_NORMAL;
            strncpy(dbg->breakpoints[i].condition, condition,
                    sizeof(dbg->breakpoints[i].condition) - 1);
            break;
        }
    }
}

bool dbg_has_breakpoint(debugger_t *dbg, const char *file, int line) {
    for (int i = 0; i < dbg->bp_count; i++) {
        if (dbg->breakpoints[i].line == line &&
            strcmp(dbg->breakpoints[i].file, file) == 0)
            return true;
    }
    return false;
}

dbg_breakpoint_t *dbg_get_breakpoint_at(debugger_t *dbg, const char *file, int line) {
    for (int i = 0; i < dbg->bp_count; i++) {
        if (dbg->breakpoints[i].line == line &&
            strcmp(dbg->breakpoints[i].file, file) == 0)
            return &dbg->breakpoints[i];
    }
    return NULL;
}

/* --- Watch expressions --- */

int dbg_add_watch(debugger_t *dbg, const char *expression) {
    if (dbg->watch_count >= DBG_MAX_WATCHES) return -1;

    dbg_watch_t *w = &dbg->watches[dbg->watch_count];
    memset(w, 0, sizeof(dbg_watch_t));
    strncpy(w->expression, expression, sizeof(w->expression) - 1);
    w->valid = false;
    snprintf(w->value, sizeof(w->value), "%s", "<not evaluated>");

    char msg[256];
    snprintf(msg, sizeof(msg), "[DBG] Watch added: %s\n", expression);
    dbg_append_output(dbg, msg);

    return dbg->watch_count++;
}

void dbg_remove_watch(debugger_t *dbg, int index) {
    if (index < 0 || index >= dbg->watch_count) return;

    for (int i = index; i < dbg->watch_count - 1; i++)
        dbg->watches[i] = dbg->watches[i + 1];
    dbg->watch_count--;
}

void dbg_edit_watch(debugger_t *dbg, int index, const char *new_expression) {
    if (index < 0 || index >= dbg->watch_count) return;
    strncpy(dbg->watches[index].expression, new_expression,
            sizeof(dbg->watches[index].expression) - 1);
    dbg->watches[index].valid = false;
    snprintf(dbg->watches[index].value, sizeof(dbg->watches[index].value), "%s",
             "<not evaluated>");
}

/* Evaluate an expression in the current frame via gdb. */
bool dbg_evaluate(debugger_t *dbg, const char *expression, char *result, int result_size) {
    if (dbg->state != DBG_PAUSED) {
        snprintf(result, (size_t)result_size, "<not paused>");
        return false;
    }
    if (mi_active(dbg)) {
        char cmd[600];
        snprintf(cmd, sizeof(cmd), "-data-evaluate-expression \"%s\"", expression);
        char res[4096] = "";
        if (mi_command(dbg, cmd, res, sizeof(res)) && strstr(res, "^done")) {
            char v[256] = "";
            if (mi_field(res, "value", v, sizeof(v))) {
                snprintf(result, (size_t)result_size, "%s", v);
                return true;
            }
        }
        char msg[200] = "";
        if (res[0] && mi_field(res, "msg", msg, sizeof(msg)))
            snprintf(result, (size_t)result_size, "%s", msg);
        else
            snprintf(result, (size_t)result_size, "<cannot evaluate '%s'>", expression);
        return false;
    }

    /* No live process: fall back to a name lookup against the last locals. */
    for (int i = 0; i < dbg->local_count; i++) {
        if (strcmp(dbg->locals[i].name, expression) == 0) {
            snprintf(result, (size_t)result_size, "%s", dbg->locals[i].value);
            return true;
        }
    }
    snprintf(result, (size_t)result_size, "<cannot evaluate '%s'>", expression);
    return false;
}

void dbg_evaluate_watches(debugger_t *dbg) {
    for (int i = 0; i < dbg->watch_count; i++) {
        dbg->watches[i].valid = dbg_evaluate(dbg, dbg->watches[i].expression,
                                              dbg->watches[i].value,
                                              sizeof(dbg->watches[i].value));
    }
}

/* --- Process management (gdb/MI backend) --- */

bool dbg_start(debugger_t *dbg, const char *program, const char *args) {
    if (mi_active(dbg)) dbg_stop(dbg);
    if (!program || !program[0]) return false;
    snprintf(dbg->program_path, sizeof(dbg->program_path), "%s", program);

    if (!mi_spawn(dbg, program)) {
        char err[1200];
        snprintf(err, sizeof(err), "[DBG] Failed to launch gdb for %s\n", program);
        dbg_append_output(dbg, err);
        dbg->state = DBG_TERMINATED;
        return false;
    }
    dbg->inferior_pid = 0;
    dbg->interrupt_requested = false;

    char res[1024];
    /* Synchronous stepping; suppress pagination/confirmation chatter. */
    mi_command(dbg, "-gdb-set mi-async off", res, sizeof(res));
    mi_command(dbg, "-gdb-set confirm off", res, sizeof(res));
    mi_command(dbg, "-gdb-set print pretty off", res, sizeof(res));
    /* Launch the inferior directly rather than via a shell, so a bundled gdb
     * works without sh/cmd on PATH (avoids "CreateProcess failed"). */
    mi_command(dbg, "-gdb-set startup-with-shell off", res, sizeof(res));

    if (args && args[0]) {
        char cmd[1200];
        snprintf(cmd, sizeof(cmd), "-exec-arguments %s", args);
        mi_command(dbg, cmd, res, sizeof(res));
    }

    /* Push every enabled breakpoint into gdb. */
    for (int i = 0; i < dbg->bp_count; i++) {
        dbg_breakpoint_t *bp = &dbg->breakpoints[i];
        if (!bp->enabled) continue;
        char cmd[900], r[1024] = "";
        const char *base = mi_basename(bp->file);
        if (bp->type == BP_CONDITIONAL && bp->condition[0])
            snprintf(cmd, sizeof(cmd), "-break-insert -c \"%s\" \"%s:%d\"",
                     bp->condition, base, bp->line);
        else
            snprintf(cmd, sizeof(cmd), "-break-insert \"%s:%d\"", base, bp->line);
        bp->verified = mi_command(dbg, cmd, r, sizeof(r)) && strstr(r, "^done") != NULL;
        /* Hit-count: gdb's ignore-count makes the breakpoint skip its first
         * N hits, so "== K" / ">= K" ignore K-1 and take effect on the Kth
         * hit. The DAP layer already rejected unsupported forms. */
        if (bp->verified && bp->type == BP_HITCOUNT && bp->hit_count_target > 1) {
            char num[16] = "";
            if (mi_field(r, "number", num, sizeof(num))) {
                char acmd[64], ar[512] = "";
                snprintf(acmd, sizeof(acmd), "-break-after %s %d",
                         num, bp->hit_count_target - 1);
                mi_command(dbg, acmd, ar, sizeof(ar));
            }
        }
    }

    dbg->state = DBG_RUNNING;
    dbg->last_exit_code = 0;
    char msg[1200];
    snprintf(msg, sizeof(msg), "[DBG] Launched under gdb: %s\n", program);
    dbg_append_output(dbg, msg);

    /* Exception breakpoints requested before the session existed. */
    dbg->exc_bp_throw = -1;
    dbg->exc_bp_unhandled = -1;
    if (dbg->break_on_throw || dbg->break_on_exception)
        dbg_set_exception_breakpoints(dbg, dbg->break_on_throw,
                                      dbg->break_on_exception);

    /* Start the inferior; --start stops at entry when requested. */
    mi_exec(dbg, dbg->break_on_entry ? "-exec-run --start" : "-exec-run");
    return true;
}

bool dbg_attach(debugger_t *dbg, const char *program, int pid) {
    if (pid <= 0) return false;
    if (mi_active(dbg)) dbg_stop(dbg);
    /* gdb is happy to start with no file and read symbols from the process,
     * but naming the executable gives it the Zan DWARF right away. */
    if (program && program[0])
        snprintf(dbg->program_path, sizeof(dbg->program_path), "%s", program);
    else
        dbg->program_path[0] = '\0';
    if (!mi_spawn(dbg, dbg->program_path)) {
        dbg_append_output(dbg, "[DBG] Failed to launch gdb for attach\n");
        dbg->state = DBG_TERMINATED;
        return false;
    }
    dbg->inferior_pid = 0;
    dbg->interrupt_requested = false;
    char res[2048] = "";
    mi_command(dbg, "-gdb-set mi-async off", res, sizeof(res));
    mi_command(dbg, "-gdb-set confirm off", res, sizeof(res));
    mi_command(dbg, "-gdb-set print pretty off", res, sizeof(res));

    char cmd[64];
    snprintf(cmd, sizeof(cmd), "-target-attach %d", pid);
    if (!mi_command(dbg, cmd, res, sizeof(res)) || strstr(res, "^error")) {
        char msg[256] = "";
        char out[400];
        mi_field(res, "msg", msg, sizeof(msg));
        snprintf(out, sizeof(out), "[DBG] Attach to pid %d failed: %s\n",
                 pid, msg[0] ? msg : "unknown error");
        dbg_append_output(dbg, out);
        mi_close(dbg);
        dbg->state = DBG_TERMINATED;
        return false;
    }
    dbg->attached = true;
    dbg->state = DBG_PAUSED;   /* -target-attach stops the process */
    dbg->last_exit_code = 0;

    /* Breakpoints the client delivered before attaching. */
    for (int i = 0; i < dbg->bp_count; i++) {
        dbg_breakpoint_t *bp = &dbg->breakpoints[i];
        if (!bp->enabled) continue;
        char bcmd[900], r[1024] = "";
        const char *base = mi_basename(bp->file);
        if (bp->type == BP_CONDITIONAL && bp->condition[0])
            snprintf(bcmd, sizeof(bcmd), "-break-insert -f -c \"%s\" \"%s:%d\"",
                     bp->condition, base, bp->line);
        else
            snprintf(bcmd, sizeof(bcmd), "-break-insert -f \"%s:%d\"", base, bp->line);
        bp->verified = mi_command(dbg, bcmd, r, sizeof(r)) && strstr(r, "^done") != NULL;
        if (bp->verified && bp->type == BP_HITCOUNT && bp->hit_count_target > 1) {
            char num[16] = "";
            if (mi_field(r, "number", num, sizeof(num))) {
                char acmd[64], ar[512] = "";
                snprintf(acmd, sizeof(acmd), "-break-after %s %d",
                         num, bp->hit_count_target - 1);
                mi_command(dbg, acmd, ar, sizeof(ar));
            }
        }
    }
    dbg->exc_bp_throw = -1;
    dbg->exc_bp_unhandled = -1;
    if (dbg->break_on_throw || dbg->break_on_exception)
        dbg_set_exception_breakpoints(dbg, dbg->break_on_throw,
                                      dbg->break_on_exception);

    char msg[1200];
    snprintf(msg, sizeof(msg), "[DBG] Attached to pid %d under gdb.\n", pid);
    dbg_append_output(dbg, msg);
    dbg_refresh_threads(dbg);
    dbg_refresh_callstack(dbg);
    dbg->active_frame = 0;
    dbg_refresh_locals(dbg);
    return true;
}

void dbg_stop(debugger_t *dbg) {
    if (mi_active(dbg)) {
        mi_close(dbg);
        dbg_append_output(dbg, "[DBG] Debugging session terminated.\n");
    }
    dbg->state = DBG_TERMINATED;
    dbg->local_count = 0;
    dbg->callstack_depth = 0;
}

void dbg_continue(debugger_t *dbg) {
    if (dbg->state != DBG_PAUSED) return;
    dbg_append_output(dbg, "[DBG] Continuing...\n");
    mi_exec(dbg, "-exec-continue");
}

/* --- pause support (real interruption of the running target) --- */

void dbg_set_wait_hook(debugger_t *dbg, void (*fn)(void *user), void *user) {
    dbg->wait_hook = fn;
    dbg->wait_hook_user = user;
}

bool dbg_interrupt(debugger_t *dbg) {
    if (!mi_active(dbg) || dbg->state != DBG_RUNNING || dbg->inferior_pid <= 0)
        return false;
    dbg->interrupt_requested = true;
    dbg_append_output(dbg, "[DBG] Interrupt requested\n");
#ifdef _WIN32
    HANDLE h = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
                               PROCESS_VM_OPERATION | PROCESS_VM_READ |
                               PROCESS_VM_WRITE,
                           FALSE, (DWORD)dbg->inferior_pid);
    if (!h) {
        dbg->interrupt_requested = false;
        dbg_append_output(dbg, "[DBG] Interrupt failed: cannot open process\n");
        return false;
    }
    bool ok = DebugBreakProcess(h) != 0;
    CloseHandle(h);
    if (!ok) {
        dbg->interrupt_requested = false;
        dbg_append_output(dbg, "[DBG] Interrupt failed: DebugBreakProcess\n");
    }
    return ok;
#else
    if (kill((pid_t)dbg->inferior_pid, SIGINT) != 0) {
        dbg->interrupt_requested = false;
        dbg_append_output(dbg, "[DBG] Interrupt failed: kill\n");
        return false;
    }
    return true;
#endif
}

void dbg_wait_stop(debugger_t *dbg) {
    if (dbg->state != DBG_RUNNING) return;
    mi_wait_stopped(dbg);
    if (dbg->state == DBG_PAUSED)
        mi_after_stop(dbg);
}

void dbg_step_over(debugger_t *dbg) {
    if (dbg->state != DBG_PAUSED) return;
    mi_exec(dbg, "-exec-next");
}

void dbg_step_into(debugger_t *dbg) {
    if (dbg->state != DBG_PAUSED) return;
    mi_exec(dbg, "-exec-step");
}

void dbg_step_out(debugger_t *dbg) {
    if (dbg->state != DBG_PAUSED) return;
    mi_exec(dbg, "-exec-finish");
}

void dbg_run_to_cursor(debugger_t *dbg, const char *file, int line) {
    if (dbg->state != DBG_PAUSED) return;
    char cmd[900], res[1024];
    snprintf(cmd, sizeof(cmd), "-break-insert -t \"%s:%d\"", mi_basename(file), line);
    mi_command(dbg, cmd, res, sizeof(res));
    mi_exec(dbg, "-exec-continue");
}

/* --- Locals and Call Stack (gdb/MI) --- */

/* --- Threads (gdb/MI) --- */

void dbg_refresh_threads(debugger_t *dbg) {
    dbg->thread_count = 0;
    if (!mi_active(dbg) || dbg->state != DBG_PAUSED) return;
    char res[8192] = "";
    if (!mi_command(dbg, "-thread-info", res, sizeof(res))) return;
    char cur[16] = "";
    if (mi_field(res, "current-thread-id", cur, sizeof(cur)))
        dbg->current_thread = atoi(cur);
    const char *p = strstr(res, "threads=[");
    if (!p) return;
    while ((p = strstr(p, "{id=")) != NULL) {
        p += 1;
        const char *end = strchr(p, '}');
        if (!end) break;
        char block[1024];
        int bl = (int)(end - p);
        if (bl > (int)sizeof(block) - 1) bl = (int)sizeof(block) - 1;
        memcpy(block, p, (size_t)bl);
        block[bl] = '\0';

        dbg_thread_t *t = &dbg->threads[dbg->thread_count];
        memset(t, 0, sizeof(*t));
        char id[16] = "", name[128] = "", target[128] = "", st[32] = "";
        mi_field(block, "id", id, sizeof(id));
        mi_field(block, "name", name, sizeof(name));
        mi_field(block, "target-id", target, sizeof(target));
        mi_field(block, "state", st, sizeof(st));
        t->id = atoi(id);
        if (name[0]) snprintf(t->name, sizeof(t->name), "%s", name);
        else if (target[0]) snprintf(t->name, sizeof(t->name), "%s", target);
        else snprintf(t->name, sizeof(t->name), "Thread %d", t->id);
        t->running = strcmp(st, "running") == 0;
        if (t->id > 0) dbg->thread_count++;
        if (dbg->thread_count >= DBG_MAX_THREADS) break;
        p = end;
    }
}

bool dbg_select_thread(debugger_t *dbg, int thread_id) {
    if (!mi_active(dbg) || thread_id <= 0) return false;
    char cmd[64], res[1024] = "";
    snprintf(cmd, sizeof(cmd), "-thread-select %d", thread_id);
    if (!mi_command(dbg, cmd, res, sizeof(res)) || strstr(res, "^error"))
        return false;
    dbg->current_thread = thread_id;
    dbg->active_frame = 0;
    dbg_refresh_callstack(dbg);
    dbg_refresh_locals(dbg);
    return true;
}

/* --- Exception breakpoints --- */

/* Inserts a pending breakpoint on `func`, returning its gdb number or -1. */
static int mi_break_function(debugger_t *dbg, const char *func) {
    char cmd[256], res[2048] = "";
    snprintf(cmd, sizeof(cmd), "-break-insert -f %s", func);
    if (!mi_command(dbg, cmd, res, sizeof(res)) || strstr(res, "^error"))
        return -1;
    char num[16] = "";
    if (!mi_field(res, "number", num, sizeof(num))) return -1;
    return atoi(num);
}

static void mi_delete_bp(debugger_t *dbg, int *num) {
    if (*num < 0) return;
    char cmd[64], res[512] = "";
    snprintf(cmd, sizeof(cmd), "-break-delete %d", *num);
    mi_command(dbg, cmd, res, sizeof(res));
    *num = -1;
}

int dbg_set_exception_breakpoints(debugger_t *dbg, bool on_throw, bool on_unhandled) {
    dbg->break_on_throw = on_throw;
    dbg->break_on_exception = on_unhandled;
    if (!mi_active(dbg)) return 0;   /* applied by dbg_start instead */
    int placed = 0;
    if (on_throw) {
        if (dbg->exc_bp_throw < 0)
            dbg->exc_bp_throw = mi_break_function(dbg, "__zan_eh_throw");
        if (dbg->exc_bp_throw >= 0) placed++;
    } else {
        mi_delete_bp(dbg, &dbg->exc_bp_throw);
    }
    if (on_unhandled) {
        if (dbg->exc_bp_unhandled < 0)
            dbg->exc_bp_unhandled = mi_break_function(dbg, "__zan_eh_unhandled");
        if (dbg->exc_bp_unhandled >= 0) placed++;
    } else {
        mi_delete_bp(dbg, &dbg->exc_bp_unhandled);
    }
    return placed;
}

void dbg_refresh_callstack(debugger_t *dbg) {
    dbg->callstack_depth = 0;
    if (!mi_active(dbg) || dbg->state != DBG_PAUSED) return;
    char res[8192] = "";
    if (!mi_command(dbg, "-stack-list-frames", res, sizeof(res))) return;

    const char *p = res;
    while ((p = strstr(p, "frame={")) != NULL) {
        p += 6;
        const char *end = strchr(p, '}');
        if (!end) break;
        char block[1024];
        int bl = (int)(end - p);
        if (bl > (int)sizeof(block) - 1) bl = (int)sizeof(block) - 1;
        memcpy(block, p, (size_t)bl);
        block[bl] = '\0';

        dbg_frame_t *f = &dbg->callstack[dbg->callstack_depth];
        memset(f, 0, sizeof(*f));
        char lvl[16] = "", func[128] = "", file[512] = "", ln[16] = "";
        mi_field(block, "level", lvl, sizeof(lvl));
        mi_field(block, "func", func, sizeof(func));
        if (!mi_field(block, "fullname", file, sizeof(file)))
            mi_field(block, "file", file, sizeof(file));
        mi_field(block, "line", ln, sizeof(ln));
        f->frame_id = atoi(lvl);
        snprintf(f->function_name, sizeof(f->function_name), "%s", func[0] ? func : "??");
        snprintf(f->file, sizeof(f->file), "%s", file);
        f->line = atoi(ln);
        f->col = 1;
        dbg->callstack_depth++;
        if (dbg->callstack_depth >= DBG_MAX_CALLSTACK) break;
        p = end;
    }
}

void dbg_refresh_locals(debugger_t *dbg) {
    dbg->local_count = 0;
    /* varobjs from the previous stop point at a dead frame: recreate */
    dbg->var_gen++;
    memset(dbg->var_created, 0, sizeof(dbg->var_created));
    dbg->var_ref_count = 0;
    if (!mi_active(dbg) || dbg->state != DBG_PAUSED) return;

    char sel[64], tmp[512];
    snprintf(sel, sizeof(sel), "-stack-select-frame %d", dbg->active_frame);
    mi_command(dbg, sel, tmp, sizeof(tmp));

    char res[8192] = "";
    if (!mi_command(dbg, "-stack-list-variables --simple-values", res, sizeof(res)))
        return;
    const char *p = strstr(res, "variables=[");
    if (!p) return;
    p += 11;
    /* --simple-values omits the value (and thus any nested braces) for
     * aggregates, so a flat '{'..'}' scan is safe. */
    while ((p = strchr(p, '{')) != NULL) {
        const char *end = strchr(p, '}');
        if (!end) break;
        char block[512];
        int bl = (int)(end - p);
        if (bl > (int)sizeof(block) - 1) bl = (int)sizeof(block) - 1;
        memcpy(block, p, (size_t)bl);
        block[bl] = '\0';

        dbg_local_t *v = &dbg->locals[dbg->local_count];
        memset(v, 0, sizeof(*v));
        char nm[128] = "", ty[64] = "", val[256] = "";
        mi_field(block, "name", nm, sizeof(nm));
        mi_field(block, "type", ty, sizeof(ty));
        if (!mi_field(block, "value", val, sizeof(val)))
            snprintf(val, sizeof(val), "%s", "{...}");
        snprintf(v->name, sizeof(v->name), "%s", nm);
        snprintf(v->type, sizeof(v->type), "%s", ty);
        snprintf(v->value, sizeof(v->value), "%s", val);
        v->scope = 0;
        v->has_children = dbg_type_expandable(ty);
        dbg->local_count++;
        if (dbg->local_count >= DBG_MAX_LOCALS) break;
        p = end;
    }
}

/* ---- variable expansion (五期: DWARF-backed field expansion) ----
 *
 * gdb only knows Zan object shapes since irgen started emitting DWARF
 * structure types for class payloads, the intrinsic collections and
 * arrays; locals of those types are expandable through gdb varobjs now. */

/* A type is worth expanding when gdb reports a struct (or a pointer to one);
 * plain byte/string pointers print their text already and have no fields. */
bool dbg_type_expandable(const char *ty) {
    if (!ty || !ty[0]) return false;
    bool strct = strncmp(ty, "struct ", 7) == 0;
    size_t n = strlen(ty);
    bool ptr = n > 0 && ty[n - 1] == '*';
    if (!strct && !ptr) return false;
    if (strcmp(ty, "byte *") == 0 || strcmp(ty, "char *") == 0 ||
        strcmp(ty, "unsigned char *") == 0)
        return false;
    if (strstr(ty, "char *") != NULL) return false;
    return true;
}

/* Extract the next balanced `child={...}` record from an MI
 * -var-list-children reply, honoring quotes so string values with braces
 * survive. Returns the cursor for the next call, NULL at the end. */
static const char *mi_next_child(const char *p, char *out, int cap) {
    const char *c = p;
    while ((c = strstr(c, "child={")) != NULL) {
        const char *body = c + 6; /* at '{' */
        int depth = 0;
        bool in_str = false;
        const char *q = body;
        while (*q) {
            if (in_str) {
                if (*q == '\\') q++;
                else if (*q == '"') in_str = false;
            } else if (*q == '"') {
                in_str = true;
            } else if (*q == '{') {
                depth++;
            } else if (*q == '}') {
                depth--;
                if (depth == 0) break;
            }
            q++;
        }
        if (depth != 0) return NULL; /* truncated record */
        int bl = (int)(q - body - 1);
        if (bl > cap - 1) bl = cap - 1;
        memcpy(out, body + 1, (size_t)bl);
        out[bl] = '\0';
        return q + 1;
    }
    return NULL;
}

static int dbg_fill_children(char *res, dbg_var_t *out, int cap) {
    int count = 0;
    char block[1024];
    const char *p = res;
    while (count < cap && (p = mi_next_child(p, block, sizeof(block))) != NULL) {
        char nm[128] = "", exp[128] = "", val[256] = "", ty[128] = "";
        mi_field(block, "name", nm, sizeof(nm));
        mi_field(block, "exp", exp, sizeof(exp));
        mi_field(block, "type", ty, sizeof(ty));
        mi_field(block, "value", val, sizeof(val));
        dbg_var_t *o = &out[count];
        memset(o, 0, sizeof(*o));
        snprintf(o->name, sizeof(o->name), "%s", exp[0] ? exp : nm);
        snprintf(o->value, sizeof(o->value), "%s", val);
        snprintf(o->type, sizeof(o->type), "%s", ty);
        count++;
    }
    return count;
}

/* List gdb varobj children of `varobj_name`; descends through the pointer
 * pseudo-child (a `struct X *` varobj lists exactly one `*expr` child) so a
 * class-typed field expands straight to its fields. `first_name_out` yields
 * the MI varobj name of the first listed record (for ref bookkeeping). */
static int dbg_var_list_children(debugger_t *dbg, const char *varobj_name,
                                 dbg_var_t *out, int cap, int depth,
                                 char *first_name_out, int name_cap);

static int dbg_var_list_children(debugger_t *dbg, const char *varobj_name,
                                 dbg_var_t *out, int cap, int depth,
                                 char *first_name_out, int name_cap) {
    if (depth > 3 || !varobj_name[0]) return 0;
    char cmd[256];
    static char res[262144];
    /* --all-values: the default reply omits value=..., which is the one
     * thing the debugger UI is here for */
    snprintf(cmd, sizeof(cmd), "-var-list-children --all-values \"%s\"",
             varobj_name);
    if (!mi_command(dbg, cmd, res, (int)sizeof(res))) return 0;
    if (!strstr(res, "^done")) return 0;

    char block[1024], cni[128] = "";
    if (mi_next_child(res, block, sizeof(block)))
        mi_field(block, "name", cni, sizeof(cni));
    int n = dbg_fill_children(res, out, cap);

    /* Pointer pseudo-child hop: `data` (long *) or `inner` (struct P *)
     * lists as one `*expr` child; the real fields sit one level deeper. */
    if (n == 1 && out[0].name[0] == '*' && cni[0] && depth < 3) {
        dbg_var_t inner[64];
        int inner_n = dbg_var_list_children(dbg, cni, inner, 64, depth + 1,
                                            first_name_out, name_cap);
        if (inner_n > 0) {
            int m = inner_n < cap ? inner_n : cap;
            for (int i = 0; i < m; i++) out[i] = inner[i];
            return m;
        }
    }
    if (first_name_out && cni[0])
        snprintf(first_name_out, (size_t)name_cap, "%s", cni);
    return n;
}

static bool dbg_local_varobj(debugger_t *dbg, int i, char *name_out, int cap) {
    if (i < 0 || i >= dbg->local_count) return false;
    if (!dbg->var_created[i]) {
        char expr[160];
        bool is_ptr = strchr(dbg->locals[i].type, '*') != NULL &&
                      strcmp(dbg->locals[i].type, "string") != 0;
        if (is_ptr)
            snprintf(expr, sizeof(expr), "*%s", dbg->locals[i].name);
        else
            snprintf(expr, sizeof(expr), "%s", dbg->locals[i].name);
        char cmd[256], res[1024];
        snprintf(cmd, sizeof(cmd), "-var-create z%ld_%d * \"%s\"",
                 dbg->var_gen, i, expr);
        if (!mi_command(dbg, cmd, res, sizeof(res)) ||
            !strstr(res, "^done")) {
            return false;
        }
        dbg->var_created[i] = true;
    }
    snprintf(name_out, (size_t)cap, "z%ld_%d", dbg->var_gen, i);
    return true;
}

int dbg_expand_variables(debugger_t *dbg, int ref, dbg_var_t *out, int cap) {
    if (!mi_active(dbg) || dbg->state != DBG_PAUSED || cap <= 0) return 0;
    char node[DBG_VAR_NAME_CAP] = "";

    if (ref >= 3000 && ref < 3000 + DBG_MAX_LOCALS) {
        if (!dbg_local_varobj(dbg, ref - 3000, node, (int)sizeof(node)))
            return 0;
    } else if (ref >= DBG_VARREF_DYN &&
               ref < DBG_VARREF_DYN + dbg->var_ref_count) {
        snprintf(node, sizeof(node), "%s",
                 dbg->var_refs[ref - DBG_VARREF_DYN]);
    } else {
        return 0;
    }

    char first_name[128] = "";
    int n = dbg_var_list_children(dbg, node, out, cap, 0,
                                  first_name, (int)sizeof(first_name));

    /* Hand out references for expandable children, remembering each child's
     * MI varobj name (gdb names them `<parent>.<exp>`, in listed order). */
    for (int i = 0; i < n; i++) {
        out[i].expand_ref = 0;
        if (!dbg_type_expandable(out[i].type)) continue;
        if (dbg->var_ref_count >= DBG_MAX_VAR_REFS) continue;
        char cni[160];
        snprintf(cni, sizeof(cni), "%s.%s", node, out[i].name);
        int idx = dbg->var_ref_count++;
        snprintf(dbg->var_refs[idx], DBG_VAR_NAME_CAP, "%s", cni);
        out[i].expand_ref = DBG_VARREF_DYN + idx;
    }
    (void)first_name;
    return n;
}

void dbg_select_frame(debugger_t *dbg, int frame_index) {
    if (frame_index < 0 || frame_index >= dbg->callstack_depth) return;
    dbg->active_frame = frame_index;

    /* Update current location to match selected frame */
    strncpy(dbg->current_file, dbg->callstack[frame_index].file,
            sizeof(dbg->current_file) - 1);
    dbg->current_line = dbg->callstack[frame_index].line;
    dbg->current_col = dbg->callstack[frame_index].col;

    /* Refresh locals for the new frame */
    dbg_refresh_locals(dbg);
    dbg_evaluate_watches(dbg);
}

/* --- Output --- */

void dbg_append_output(debugger_t *dbg, const char *text) {
    int tlen = (int)strlen(text);
    int space = DBG_MAX_OUTPUT - dbg->output_len - 1;
    if (tlen > space) {
        /* scroll: discard first half */
        int keep = dbg->output_len / 2;
        memmove(dbg->output, dbg->output + (dbg->output_len - keep), (size_t)keep);
        dbg->output_len = keep;
        space = DBG_MAX_OUTPUT - dbg->output_len - 1;
        if (tlen > space) tlen = space;
    }
    memcpy(dbg->output + dbg->output_len, text, (size_t)tlen);
    dbg->output_len += tlen;
    dbg->output[dbg->output_len] = '\0';
}

void dbg_clear_output(debugger_t *dbg) {
    dbg->output[0] = '\0';
    dbg->output_len = 0;
}

/* --- Variable assignment --- */

bool dbg_set_variable(debugger_t *dbg, const char *name, const char *value) {
    if (dbg->state != DBG_PAUSED) return false;

    if (mi_active(dbg)) {
        char cmd[600], res[1024] = "";
        snprintf(cmd, sizeof(cmd), "-gdb-set var %s=%s", name, value);
        bool ok = mi_command(dbg, cmd, res, sizeof(res)) && strstr(res, "^done") != NULL;
        if (ok) {
            dbg_refresh_locals(dbg);
            char msg[256];
            snprintf(msg, sizeof(msg), "[DBG] Set %s = %s\n", name, value);
            dbg_append_output(dbg, msg);
        }
        return ok;
    }

    char msg[256];
    snprintf(msg, sizeof(msg), "[DBG] Variable '%s' not found in current scope\n", name);
    dbg_append_output(dbg, msg);
    return false;
}

bool dbg_get_exception_info(debugger_t *dbg, char *info, int info_size) {
    if (dbg->state != DBG_PAUSED) return false;
    if (strstr(dbg->stop_reason, "exception") == NULL) return false;

    snprintf(info, (size_t)info_size, "Exception at %s:%d",
            dbg->current_file, dbg->current_line + 1);
    return true;
}

bool dbg_is_current_line(debugger_t *dbg, const char *file, int line) {
    if (dbg->state != DBG_PAUSED) return false;
    if (dbg->current_line != line) return false;
    if (!file || !dbg->current_file[0]) return false;
    /* Compare filenames (case-insensitive on Windows) */
#ifdef _WIN32
    return _stricmp(dbg->current_file, file) == 0;
#else
    return strcmp(dbg->current_file, file) == 0;
#endif
}
