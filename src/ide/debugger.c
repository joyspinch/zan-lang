/* debugger.c -- Debugger implementation.
 *
 * Provides breakpoint management, process control, and variable inspection.
 * On Windows uses CreateProcess + DebugActiveProcess for basic debugging.
 */
#include "debugger.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#ifdef _WIN32
#include <windows.h>
#endif

void dbg_init(debugger_t *dbg) {
    memset(dbg, 0, sizeof(debugger_t));
    dbg->state = DBG_IDLE;
    dbg->current_frame = -1;
}

int dbg_add_breakpoint(debugger_t *dbg, const char *file, int line) {
    if (dbg->bp_count >= DBG_MAX_BREAKPOINTS) return -1;

    /* check for duplicate */
    for (int i = 0; i < dbg->bp_count; i++) {
        if (dbg->breakpoints[i].line == line &&
            strcmp(dbg->breakpoints[i].file, file) == 0)
            return i;
    }

    int idx = dbg->bp_count++;
    dbg_breakpoint_t *bp = &dbg->breakpoints[idx];
    memset(bp, 0, sizeof(dbg_breakpoint_t));
    strncpy(bp->file, file, sizeof(bp->file) - 1);
    bp->line = line;
    bp->enabled = true;
    bp->hit = false;
    bp->hit_count = 0;
    return idx;
}

void dbg_remove_breakpoint(debugger_t *dbg, int index) {
    if (index < 0 || index >= dbg->bp_count) return;
    for (int i = index; i < dbg->bp_count - 1; i++) {
        dbg->breakpoints[i] = dbg->breakpoints[i + 1];
    }
    dbg->bp_count--;
}

void dbg_toggle_breakpoint(debugger_t *dbg, const char *file, int line) {
    dbg_breakpoint_t *bp = dbg_find_breakpoint(dbg, file, line);
    if (bp) {
        bp->enabled = !bp->enabled;
    } else {
        dbg_add_breakpoint(dbg, file, line);
    }
}

dbg_breakpoint_t *dbg_find_breakpoint(debugger_t *dbg, const char *file, int line) {
    for (int i = 0; i < dbg->bp_count; i++) {
        if (dbg->breakpoints[i].line == line &&
            strcmp(dbg->breakpoints[i].file, file) == 0)
            return &dbg->breakpoints[i];
    }
    return NULL;
}

bool dbg_has_breakpoint(const debugger_t *dbg, const char *file, int line) {
    for (int i = 0; i < dbg->bp_count; i++) {
        if (dbg->breakpoints[i].line == line &&
            dbg->breakpoints[i].enabled &&
            strcmp(dbg->breakpoints[i].file, file) == 0)
            return true;
    }
    return false;
}

bool dbg_is_current_line(const debugger_t *dbg, const char *file, int line) {
    if (dbg->state != DBG_PAUSED) return false;
    return dbg->current_line == line &&
           strcmp(dbg->current_file, file) == 0;
}

#ifdef _WIN32

bool dbg_start(debugger_t *dbg, const char *exe_path, const char *args) {
    if (dbg->state != DBG_IDLE && dbg->state != DBG_STOPPED) return false;

    char cmd_line[2048];
    if (args && args[0])
        snprintf(cmd_line, sizeof(cmd_line), "\"%s\" %s", exe_path, args);
    else
        snprintf(cmd_line, sizeof(cmd_line), "\"%s\"", exe_path);

    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    memset(&pi, 0, sizeof(pi));

    BOOL ok = CreateProcessA(
        NULL, cmd_line, NULL, NULL, FALSE,
        DEBUG_PROCESS | DEBUG_ONLY_THIS_PROCESS | CREATE_NEW_CONSOLE,
        NULL, NULL, &si, &pi);

    if (!ok) return false;

    dbg->process_handle = pi.hProcess;
    dbg->thread_handle = pi.hThread;
    dbg->process_id = pi.dwProcessId;
    dbg->state = DBG_RUNNING;

    /* populate initial frame */
    dbg->frame_count = 1;
    dbg->current_frame = 0;
    strncpy(dbg->frames[0].function, "Main", sizeof(dbg->frames[0].function) - 1);
    dbg->frames[0].line = 0;

    return true;
}

void dbg_stop(debugger_t *dbg) {
    if (dbg->state == DBG_IDLE) return;

    if (dbg->process_handle) {
        TerminateProcess((HANDLE)dbg->process_handle, 1);
        CloseHandle((HANDLE)dbg->process_handle);
        CloseHandle((HANDLE)dbg->thread_handle);
        dbg->process_handle = NULL;
        dbg->thread_handle = NULL;
    }

    dbg->state = DBG_STOPPED;
    dbg->frame_count = 0;
    dbg->local_count = 0;
}

void dbg_continue(debugger_t *dbg) {
    if (dbg->state != DBG_PAUSED) return;

    if (dbg->thread_handle) {
        ContinueDebugEvent(dbg->process_id, 0, DBG_CONTINUE);
    }
    dbg->state = DBG_RUNNING;

    /* clear hit flags */
    for (int i = 0; i < dbg->bp_count; i++) {
        dbg->breakpoints[i].hit = false;
    }
}

void dbg_step_over(debugger_t *dbg) {
    if (dbg->state != DBG_PAUSED) return;
    /* In a real debugger, set temp breakpoint at next line and continue.
     * For now, simulate stepping by advancing the line. */
    dbg->current_line++;
    /* re-pause at next line */
    dbg->state = DBG_PAUSED;
}

void dbg_step_into(debugger_t *dbg) {
    if (dbg->state != DBG_PAUSED) return;
    /* Simulate stepping into a function call */
    if (dbg->frame_count < DBG_MAX_FRAMES) {
        dbg_frame_t *f = &dbg->frames[dbg->frame_count++];
        snprintf(f->function, sizeof(f->function), "<called function>");
        strncpy(f->file, dbg->current_file, sizeof(f->file) - 1);
        f->line = 0;
        dbg->current_frame = dbg->frame_count - 1;
    }
    dbg->state = DBG_PAUSED;
}

void dbg_step_out(debugger_t *dbg) {
    if (dbg->state != DBG_PAUSED) return;
    /* Simulate stepping out */
    if (dbg->frame_count > 1) {
        dbg->frame_count--;
        dbg->current_frame = dbg->frame_count - 1;
        dbg->current_line = dbg->frames[dbg->current_frame].line;
    }
    dbg->state = DBG_PAUSED;
}

#else /* non-Windows stubs */

bool dbg_start(debugger_t *dbg, const char *exe_path, const char *args) {
    (void)dbg; (void)exe_path; (void)args;
    return false;
}
void dbg_stop(debugger_t *dbg) { (void)dbg; }
void dbg_continue(debugger_t *dbg) { (void)dbg; }
void dbg_step_over(debugger_t *dbg) { (void)dbg; }
void dbg_step_into(debugger_t *dbg) { (void)dbg; }
void dbg_step_out(debugger_t *dbg) { (void)dbg; }

#endif

dbg_state_t dbg_get_state(const debugger_t *dbg) {
    return dbg->state;
}
