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
#endif

void dbg_init(debugger_t *dbg) {
    memset(dbg, 0, sizeof(debugger_t));
    dbg->state = DBG_IDLE;
    dbg->bp_next_id = 1;
    dbg->break_on_entry = false;
    dbg->break_on_exception = true;
    dbg->skip_stdlib = true;
    dbg->active_frame = 0;
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

/* Evaluate a simple expression against locals */
bool dbg_evaluate(debugger_t *dbg, const char *expression, char *result, int result_size) {
    if (dbg->state != DBG_PAUSED) {
        snprintf(result, (size_t)result_size, "<not paused>");
        return false;
    }

    /* Simple evaluation: check if expression matches a local variable name */
    for (int i = 0; i < dbg->local_count; i++) {
        if (strcmp(dbg->locals[i].name, expression) == 0) {
            snprintf(result, (size_t)result_size, "%s", dbg->locals[i].value);
            return true;
        }
    }

    /* Check member access: expr.member */
    const char *dot = strchr(expression, '.');
    if (dot) {
        char obj_name[128];
        int obj_len = (int)(dot - expression);
        if (obj_len > 0 && obj_len < 127) {
            memcpy(obj_name, expression, (size_t)obj_len);
            obj_name[obj_len] = '\0';
            const char *member = dot + 1;

            /* look up the object in locals */
            for (int i = 0; i < dbg->local_count; i++) {
                if (strcmp(dbg->locals[i].name, obj_name) == 0) {
                    /* For now, indicate the member access */
                    snprintf(result, (size_t)result_size, "%s.%s = <complex>",
                            obj_name, member);
                    return true;
                }
            }
        }
    }

    /* Simple arithmetic: literal numbers */
    char *end_ptr;
    long val = strtol(expression, &end_ptr, 0);
    if (end_ptr != expression && *end_ptr == '\0') {
        snprintf(result, (size_t)result_size, "%ld", val);
        return true;
    }

    /* Comparison expressions: "a == b", "a > b", etc. */
    /* For now, indicate the expression can't be evaluated */
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

/* --- Process management --- */

#ifdef _WIN32

bool dbg_start(debugger_t *dbg, const char *program, const char *args) {
    if (dbg->state != DBG_IDLE && dbg->state != DBG_TERMINATED) {
        dbg_stop(dbg);
    }

    STARTUPINFOA si = {0};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {0};

    char cmd[2048];
    if (args && args[0])
        snprintf(cmd, sizeof(cmd), "\"%s\" %s", program, args);
    else
        snprintf(cmd, sizeof(cmd), "\"%s\"", program);

    DWORD flags = DEBUG_PROCESS | CREATE_NEW_CONSOLE;
    if (!CreateProcessA(NULL, cmd, NULL, NULL, FALSE, flags,
                       NULL, NULL, &si, &pi)) {
        char err[256];
        snprintf(err, sizeof(err), "[DBG] Failed to start: %s (error %lu)\n",
                program, GetLastError());
        dbg_append_output(dbg, err);
        return false;
    }

    dbg->process_handle = pi.hProcess;
    dbg->thread_handle = pi.hThread;
    dbg->process_id = pi.dwProcessId;
    dbg->thread_id = pi.dwThreadId;

    if (dbg->break_on_entry) {
        dbg->state = DBG_PAUSED;
        strncpy(dbg->stop_reason, "entry", sizeof(dbg->stop_reason) - 1);
    } else {
        dbg->state = DBG_RUNNING;
    }

    char msg[512];
    snprintf(msg, sizeof(msg), "[DBG] Started debugging: %s (PID: %lu)\n",
            program, dbg->process_id);
    dbg_append_output(dbg, msg);
    return true;
}

void dbg_stop(debugger_t *dbg) {
    if (dbg->state == DBG_IDLE) return;

    if (dbg->process_handle) {
        TerminateProcess(dbg->process_handle, 1);
        CloseHandle(dbg->process_handle);
        CloseHandle(dbg->thread_handle);
        dbg->process_handle = NULL;
        dbg->thread_handle = NULL;
    }
    dbg->state = DBG_TERMINATED;
    dbg->local_count = 0;
    dbg->callstack_depth = 0;
    dbg_append_output(dbg, "[DBG] Debugging session terminated.\n");
}

/* Helper: check if a breakpoint should fire (condition, hit count, logpoint) */
static bool bp_should_stop(debugger_t *dbg, dbg_breakpoint_t *bp) {
    if (!bp->enabled) return false;

    switch (bp->type) {
    case BP_NORMAL:
        return true;

    case BP_CONDITIONAL:
        /* Evaluate the condition expression */
        if (bp->condition[0]) {
            char result[64];
            bool ok = dbg_evaluate(dbg, bp->condition, result, sizeof(result));
            if (!ok) return false;
            /* treat "true", "1", non-zero as true */
            if (strcmp(result, "true") == 0 || strcmp(result, "1") == 0)
                return true;
            if (result[0] >= '1' && result[0] <= '9')
                return true;
            return false;
        }
        return true;

    case BP_HITCOUNT:
        bp->hit_count++;
        return (bp->hit_count >= bp->hit_count_target);

    case BP_LOGPOINT:
        /* Logpoints don't stop; they emit output */
        if (bp->log_message[0]) {
            char msg[512];
            snprintf(msg, sizeof(msg), "[LOG] %s:%d: %s\n",
                    bp->file, bp->line + 1, bp->log_message);
            dbg_append_output(dbg, msg);
        }
        return false;

    default:
        return true;
    }
}

void dbg_continue(debugger_t *dbg) {
    if (dbg->state != DBG_PAUSED) return;
    dbg->state = DBG_RUNNING;
    dbg_append_output(dbg, "[DBG] Continuing...\n");

    /* Resume thread for real process debugging */
    if (dbg->process_handle) {
        ContinueDebugEvent(dbg->process_id, dbg->thread_id, DBG_CONTINUE);

        /* Wait for next debug event (breakpoint, exception, or exit) */
        DEBUG_EVENT de;
        while (WaitForDebugEvent(&de, 5000)) {
            if (de.dwDebugEventCode == EXIT_PROCESS_DEBUG_EVENT) {
                dbg->state = DBG_TERMINATED;
                dbg_append_output(dbg, "[DBG] Process exited.\n");
                return;
            }
            if (de.dwDebugEventCode == EXCEPTION_DEBUG_EVENT) {
                /* Check if it's a breakpoint exception */
                if (de.u.Exception.ExceptionRecord.ExceptionCode == EXCEPTION_BREAKPOINT ||
                    de.u.Exception.ExceptionRecord.ExceptionCode == EXCEPTION_SINGLE_STEP) {
                    dbg->state = DBG_PAUSED;
                    strncpy(dbg->stop_reason, "breakpoint", sizeof(dbg->stop_reason) - 1);
                    dbg_refresh_locals(dbg);
                    dbg_evaluate_watches(dbg);
                    return;
                }
                /* Other exception: stop */
                dbg->state = DBG_PAUSED;
                strncpy(dbg->stop_reason, "exception", sizeof(dbg->stop_reason) - 1);
                dbg_refresh_locals(dbg);
                return;
            }
            ContinueDebugEvent(de.dwProcessId, de.dwThreadId, DBG_CONTINUE);
        }
        /* Timeout or no event: process likely ended */
        dbg->state = DBG_TERMINATED;
        return;
    }

    /* Simulated mode: find the next breakpoint to stop at */
    int next_bp_line = -1;
    dbg_breakpoint_t *hit_bp = NULL;

    /* Look for breakpoints after current line in the same file */
    for (int i = 0; i < dbg->bp_count; i++) {
        dbg_breakpoint_t *bp = &dbg->breakpoints[i];
        if (!bp->enabled) continue;
        if (strcmp(bp->file, dbg->current_file) != 0) continue;
        if (bp->line <= dbg->current_line) continue;

        if (next_bp_line < 0 || bp->line < next_bp_line) {
            next_bp_line = bp->line;
            hit_bp = bp;
        }
    }

    if (hit_bp && bp_should_stop(dbg, hit_bp)) {
        /* Stop at next breakpoint */
        dbg->current_line = next_bp_line;
        dbg->state = DBG_PAUSED;
        strncpy(dbg->stop_reason, "breakpoint", sizeof(dbg->stop_reason) - 1);

        /* Update stack frame */
        if (dbg->callstack_depth > 0) {
            dbg->callstack[dbg->active_frame].line = dbg->current_line;
        }
        dbg_refresh_locals(dbg);
        dbg_evaluate_watches(dbg);

        char msg[128];
        snprintf(msg, sizeof(msg), "[DBG] Hit breakpoint at line %d\n", next_bp_line + 1);
        dbg_append_output(dbg, msg);
    } else if (hit_bp && hit_bp->type == BP_LOGPOINT) {
        /* Logpoint didn't stop, look further or terminate */
        dbg->current_line = next_bp_line;
        /* Recursively continue past logpoints */
        dbg->state = DBG_PAUSED; /* temporarily pause to allow recursive continue */
        dbg_continue(dbg);
    } else {
        /* No more breakpoints: program finishes */
        dbg->state = DBG_TERMINATED;
        dbg_append_output(dbg, "[DBG] Program finished (no more breakpoints to hit).\n");
    }
}

void dbg_step_over(debugger_t *dbg) {
    if (dbg->state != DBG_PAUSED) return;
    dbg->state = DBG_STEPPING;
    strncpy(dbg->stop_reason, "step", sizeof(dbg->stop_reason) - 1);

    /* Simulate step over: advance to next line */
    dbg->current_line++;
    dbg->state = DBG_PAUSED;
    dbg_refresh_locals(dbg);
    dbg_evaluate_watches(dbg);

    char msg[128];
    snprintf(msg, sizeof(msg), "[DBG] Step over -> line %d\n", dbg->current_line + 1);
    dbg_append_output(dbg, msg);
}

void dbg_step_into(debugger_t *dbg) {
    if (dbg->state != DBG_PAUSED) return;
    dbg->state = DBG_STEPPING;
    strncpy(dbg->stop_reason, "step", sizeof(dbg->stop_reason) - 1);

    /* Simulate step into */
    dbg->current_line++;
    dbg->state = DBG_PAUSED;
    dbg_refresh_locals(dbg);
    dbg_evaluate_watches(dbg);

    char msg[128];
    snprintf(msg, sizeof(msg), "[DBG] Step into -> line %d\n", dbg->current_line + 1);
    dbg_append_output(dbg, msg);
}

void dbg_step_out(debugger_t *dbg) {
    if (dbg->state != DBG_PAUSED) return;
    dbg->state = DBG_STEPPING;
    strncpy(dbg->stop_reason, "step", sizeof(dbg->stop_reason) - 1);

    /* Simulate step out: return to caller frame */
    if (dbg->callstack_depth > 1) {
        dbg->callstack_depth--;
        dbg->current_line = dbg->callstack[dbg->callstack_depth - 1].line;
        strncpy(dbg->current_file, dbg->callstack[dbg->callstack_depth - 1].file,
                sizeof(dbg->current_file) - 1);
    } else {
        dbg->current_line++;
    }
    dbg->state = DBG_PAUSED;
    dbg_refresh_locals(dbg);
    dbg_evaluate_watches(dbg);

    char msg[128];
    snprintf(msg, sizeof(msg), "[DBG] Step out -> line %d\n", dbg->current_line + 1);
    dbg_append_output(dbg, msg);
}

void dbg_run_to_cursor(debugger_t *dbg, const char *file, int line) {
    if (dbg->state != DBG_PAUSED) return;

    /* Set a temporary breakpoint and continue */
    strncpy(dbg->current_file, file, sizeof(dbg->current_file) - 1);
    dbg->current_line = line;
    dbg->state = DBG_PAUSED;
    strncpy(dbg->stop_reason, "run to cursor", sizeof(dbg->stop_reason) - 1);
    dbg_refresh_locals(dbg);
    dbg_evaluate_watches(dbg);

    char msg[128];
    snprintf(msg, sizeof(msg), "[DBG] Run to cursor -> line %d\n", line + 1);
    dbg_append_output(dbg, msg);
}

#else /* Non-Windows */

bool dbg_start(debugger_t *dbg, const char *program, const char *args) {
    (void)program; (void)args;
    dbg_append_output(dbg, "[DBG] Debugging not supported on this platform.\n");
    return false;
}

void dbg_stop(debugger_t *dbg) {
    dbg->state = DBG_IDLE;
    dbg_append_output(dbg, "[DBG] Stopped.\n");
}

void dbg_continue(debugger_t *dbg) { (void)dbg; }
void dbg_step_over(debugger_t *dbg) { (void)dbg; }
void dbg_step_into(debugger_t *dbg) { (void)dbg; }
void dbg_step_out(debugger_t *dbg) { (void)dbg; }
void dbg_run_to_cursor(debugger_t *dbg, const char *file, int line) { (void)dbg; (void)file; (void)line; }

#endif

/* --- Locals and Call Stack --- */

void dbg_refresh_locals(debugger_t *dbg) {
    /* In a real implementation, this would read debug info from the process.
     * For now, maintain the current set of locals. The DAP adapter will
     * populate these from the debug information. */
    (void)dbg;
}

void dbg_refresh_callstack(debugger_t *dbg) {
    /* In a real implementation, walk the stack frames.
     * The DAP adapter populates this. */
    if (dbg->callstack_depth == 0 && dbg->state == DBG_PAUSED) {
        /* Add at least the current frame */
        dbg->callstack[0].frame_id = 0;
        strncpy(dbg->callstack[0].file, dbg->current_file,
                sizeof(dbg->callstack[0].file) - 1);
        dbg->callstack[0].line = dbg->current_line;
        dbg->callstack[0].col = dbg->current_col;
        snprintf(dbg->callstack[0].function_name,
                 sizeof(dbg->callstack[0].function_name), "%s", "<unknown>");
        dbg->callstack_depth = 1;
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

    /* Update local variable if it exists */
    for (int i = 0; i < dbg->local_count; i++) {
        if (strcmp(dbg->locals[i].name, name) == 0) {
            strncpy(dbg->locals[i].value, value, sizeof(dbg->locals[i].value) - 1);
            char msg[256];
            snprintf(msg, sizeof(msg), "[DBG] Set %s = %s\n", name, value);
            dbg_append_output(dbg, msg);
            return true;
        }
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
