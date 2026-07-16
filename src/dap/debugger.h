/* debugger.h -- Integrated debugger for the Zan IDE.
 *
 * Enhanced with:
 *   - Conditional breakpoints (expression-based)
 *   - Hit count breakpoints
 *   - Watch expressions
 *   - Variable inspection with type info
 *   - Call stack with parameter values
 *   - Debug output panel integration
 *   - Logpoint support (breakpoint that only logs)
 */
#ifndef ZAN_DEBUGGER_H
#define ZAN_DEBUGGER_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DBG_MAX_BREAKPOINTS  128
#define DBG_MAX_WATCHES      32
#define DBG_MAX_LOCALS       256
#define DBG_MAX_CALLSTACK    64
#define DBG_MAX_OUTPUT       8192

/* Debugger state machine */
typedef enum {
    DBG_IDLE,       /* not debugging */
    DBG_RUNNING,    /* program running */
    DBG_PAUSED,     /* hit a breakpoint or step complete */
    DBG_STEPPING,   /* in the middle of a step operation */
    DBG_TERMINATED  /* program ended */
} dbg_state_t;

/* Breakpoint type */
typedef enum {
    BP_NORMAL,      /* always breaks */
    BP_CONDITIONAL, /* breaks when expression is true */
    BP_HITCOUNT,    /* breaks after N hits */
    BP_LOGPOINT     /* doesn't break, logs a message */
} bp_type_t;

/* A breakpoint */
typedef struct {
    char      file[512];
    int       line;             /* 0-based */
    bool      enabled;
    bool      verified;         /* set once we confirm the BP was placed */
    int       id;               /* unique breakpoint ID */

    bp_type_t type;
    char      condition[256];   /* expression for conditional BP */
    int       hit_count_target; /* for hit-count BP */
    int       hit_count;        /* current hit count */
    char      log_message[256]; /* for logpoint */
} dbg_breakpoint_t;

/* A watch expression */
typedef struct {
    char      expression[256];
    char      value[256];       /* last evaluated value */
    char      type[64];         /* resolved type */
    bool      valid;            /* was evaluation successful? */
    bool      has_children;     /* is it a complex object? */
} dbg_watch_t;

/* A local variable */
typedef struct {
    char      name[128];
    char      value[256];
    char      type[64];
    int       scope;            /* 0 = current scope, 1 = parent, etc. */
    bool      has_children;
} dbg_local_t;

/* A call stack frame */
typedef struct {
    char      function_name[128];
    char      file[512];
    int       line;
    int       col;
    int       frame_id;
} dbg_frame_t;

/* Debug output entry */
typedef struct {
    char      text[512];
    int       category;     /* 0=stdout, 1=stderr, 2=debug, 3=info */
} dbg_output_entry_t;

/* Main debugger state */
typedef struct {
    dbg_state_t     state;

    /* breakpoints */
    dbg_breakpoint_t breakpoints[DBG_MAX_BREAKPOINTS];
    int              bp_count;
    int              bp_next_id;

    /* watch expressions */
    dbg_watch_t     watches[DBG_MAX_WATCHES];
    int             watch_count;

    /* current locals */
    dbg_local_t     locals[DBG_MAX_LOCALS];
    int             local_count;

    /* call stack */
    dbg_frame_t     callstack[DBG_MAX_CALLSTACK];
    int             callstack_depth;
    int             active_frame;   /* selected frame index */

    /* debug output buffer */
    char            output[DBG_MAX_OUTPUT];
    int             output_len;

    /* current stopped location */
    char            current_file[512];
    int             current_line;
    int             current_col;
    char            stop_reason[128];   /* reason for pause */

    /* process information (platform-specific) */
#ifdef _WIN32
    void           *process_handle;
    void           *thread_handle;
    unsigned long   process_id;
    unsigned long   thread_id;
#else
    int             child_pid;
#endif

    /* GDB/MI backend: the real debugger is driven by spawning gdb in machine
     * interface mode and exchanging MI commands over redirected pipes. */
#ifdef _WIN32
    void           *gdb_in_w;   /* HANDLE: write end of gdb's stdin */
    void           *gdb_out_r;  /* HANDLE: read end of gdb's stdout */
    void           *gdb_proc;   /* HANDLE: the gdb process */
#else
    int             gdb_in_fd;  /* write end of gdb's stdin */
    int             gdb_out_fd; /* read end of gdb's stdout */
    int             gdb_pid;
#endif
    char            gdb_path[512];  /* resolved gdb executable */
    char            program_path[1024];
    int             mi_token;       /* monotonically increasing MI command token */
    char            mi_buf[8192];   /* leftover bytes between line reads */
    int             mi_buf_len;
    int             last_exit_code;

    /* settings */
    bool            break_on_entry;     /* pause at program start */
    bool            break_on_exception; /* pause on unhandled exceptions */
    bool            skip_stdlib;        /* don't step into stdlib */
} debugger_t;

/* Override the gdb executable used by the backend (else it is auto-detected
 * from PATH / bundled toolchain / known install locations). */
void dbg_set_gdb_path(debugger_t *dbg, const char *path);

/* Initialize debugger */
void dbg_init(debugger_t *dbg);

/* Start debugging a program */
bool dbg_start(debugger_t *dbg, const char *program, const char *args);

/* Stop debugging (terminate the program) */
void dbg_stop(debugger_t *dbg);

/* Continue execution */
void dbg_continue(debugger_t *dbg);

/* Stepping */
void dbg_step_over(debugger_t *dbg);
void dbg_step_into(debugger_t *dbg);
void dbg_step_out(debugger_t *dbg);

/* Run to cursor */
void dbg_run_to_cursor(debugger_t *dbg, const char *file, int line);

/* --- Breakpoint management --- */

/* Add a simple breakpoint. Returns breakpoint ID or -1. */
int dbg_add_breakpoint(debugger_t *dbg, const char *file, int line);

/* Add a conditional breakpoint */
int dbg_add_conditional_bp(debugger_t *dbg, const char *file, int line,
                           const char *condition);

/* Add a hit-count breakpoint */
int dbg_add_hitcount_bp(debugger_t *dbg, const char *file, int line, int count);

/* Add a logpoint (breakpoint that logs but doesn't stop) */
int dbg_add_logpoint(debugger_t *dbg, const char *file, int line,
                     const char *message);

/* Remove a breakpoint by ID */
bool dbg_remove_breakpoint(debugger_t *dbg, int bp_id);

/* Remove a breakpoint by location */
bool dbg_remove_breakpoint_at(debugger_t *dbg, const char *file, int line);

/* Toggle breakpoint at location */
int dbg_toggle_breakpoint(debugger_t *dbg, const char *file, int line);

/* Enable/disable a breakpoint */
void dbg_enable_breakpoint(debugger_t *dbg, int bp_id, bool enabled);

/* Edit a breakpoint's condition */
void dbg_set_bp_condition(debugger_t *dbg, int bp_id, const char *condition);

/* Check if line has a breakpoint */
bool dbg_has_breakpoint(debugger_t *dbg, const char *file, int line);

/* Get breakpoint at location (NULL if none) */
dbg_breakpoint_t *dbg_get_breakpoint_at(debugger_t *dbg, const char *file, int line);

/* --- Watch expressions --- */

/* Add a watch expression */
int dbg_add_watch(debugger_t *dbg, const char *expression);

/* Remove a watch expression by index */
void dbg_remove_watch(debugger_t *dbg, int index);

/* Edit a watch expression */
void dbg_edit_watch(debugger_t *dbg, int index, const char *new_expression);

/* Evaluate all watch expressions (called when paused) */
void dbg_evaluate_watches(debugger_t *dbg);

/* Evaluate a single expression and return value as string */
bool dbg_evaluate(debugger_t *dbg, const char *expression, char *result, int result_size);

/* --- Locals and Call Stack --- */

/* Refresh local variables for current frame */
void dbg_refresh_locals(debugger_t *dbg);

/* Refresh call stack */
void dbg_refresh_callstack(debugger_t *dbg);

/* Select a different stack frame (updates locals) */
void dbg_select_frame(debugger_t *dbg, int frame_index);

/* --- Output --- */

/* Append text to debug output */
void dbg_append_output(debugger_t *dbg, const char *text);

/* Clear debug output */
void dbg_clear_output(debugger_t *dbg);

/* --- Utility --- */

/* Check if debugger is paused at specific file:line */
bool dbg_is_current_line(debugger_t *dbg, const char *file, int line);

/* Set value of a variable (in current scope) */
bool dbg_set_variable(debugger_t *dbg, const char *name, const char *value);

/* Get exception info if stopped on exception */
bool dbg_get_exception_info(debugger_t *dbg, char *info, int info_size);

#ifdef __cplusplus
}
#endif

#endif /* ZAN_DEBUGGER_H */
