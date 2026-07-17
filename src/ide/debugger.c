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

/* Read a single '\n'-terminated line (newline stripped) from gdb's stdout.
 * Buffers surplus bytes in dbg->mi_buf. Returns false at EOF. */
static bool mi_read_line(debugger_t *dbg, char *out, int out_size) {
    int len = 0;
    for (;;) {
        for (int i = 0; i < dbg->mi_buf_len; i++) {
            char c = dbg->mi_buf[i];
            if (c == '\n') {
                int rest = dbg->mi_buf_len - (i + 1);
                memmove(dbg->mi_buf, dbg->mi_buf + i + 1, (size_t)rest);
                dbg->mi_buf_len = rest;
                if (len > 0 && out[len - 1] == '\r') len--;
                out[len < out_size ? len : out_size - 1] = '\0';
                return true;
            }
            if (len < out_size - 1) out[len++] = c;
        }
        dbg->mi_buf_len = 0;
        char chunk[4096];
#ifdef _WIN32
        DWORD got = 0;
        if (!ReadFile((HANDLE)dbg->gdb_out_r, chunk, sizeof(chunk), &got, NULL) || got == 0) {
            if (len > 0) { out[len < out_size ? len : out_size - 1] = '\0'; return true; }
            return false;
        }
#else
        ssize_t got = read(dbg->gdb_out_fd, chunk, sizeof(chunk));
        if (got <= 0) {
            if (len > 0) { out[len < out_size ? len : out_size - 1] = '\0'; return true; }
            return false;
        }
#endif
        int n = (int)got;
        if (n > (int)sizeof(dbg->mi_buf)) n = (int)sizeof(dbg->mi_buf);
        memcpy(dbg->mi_buf, chunk, (size_t)n);
        dbg->mi_buf_len = n;
    }
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
        dbg->state = DBG_TERMINATED;
        return;
    }
    dbg->state = DBG_PAUSED;
    if (reason[0])
        snprintf(dbg->stop_reason, sizeof(dbg->stop_reason), "%s", reason);
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
    }
    /* '~' console, '&' log, '=' notify, '+' status: ignored as gdb chatter */
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
        if (line[0] == '*' || line[0] == '@') { mi_dispatch_line(dbg, line); continue; }
        if (line[0] == '~' || line[0] == '&' || line[0] == '=' || line[0] == '+') continue;
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

/* Read until the next `*stopped` (or EOF/termination). */
static bool mi_wait_stopped(debugger_t *dbg) {
    char line[8192];
    while (mi_read_line(dbg, line, sizeof(line))) {
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
    dbg->state = DBG_TERMINATED;
    return false;
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
    if (dbg->state == DBG_PAUSED) {
        dbg_refresh_callstack(dbg);
        dbg->active_frame = 0;
        dbg_refresh_locals(dbg);
        dbg_evaluate_watches(dbg);
    }
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

    char res[1024];
    /* Synchronous stepping; suppress pagination/confirmation chatter. */
    mi_command(dbg, "-gdb-set mi-async off", res, sizeof(res));
    mi_command(dbg, "-gdb-set confirm off", res, sizeof(res));
    mi_command(dbg, "-gdb-set print pretty off", res, sizeof(res));

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
    }

    dbg->state = DBG_RUNNING;
    dbg->last_exit_code = 0;
    char msg[1200];
    snprintf(msg, sizeof(msg), "[DBG] Launched under gdb: %s\n", program);
    dbg_append_output(dbg, msg);

    /* Start the inferior; --start stops at entry when requested. */
    mi_exec(dbg, dbg->break_on_entry ? "-exec-run --start" : "-exec-run");
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
        v->has_children = false;
        dbg->local_count++;
        if (dbg->local_count >= DBG_MAX_LOCALS) break;
        p = end;
    }
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
    if (strcmp(dbg->stop_reason, "exception") != 0) return false;

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
