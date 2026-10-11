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
#define DBG_MAX_THREADS      64
#define DBG_MAX_OUTPUT       8192

typedef enum {
    DBG_IDLE,
    DBG_RUNNING,
    DBG_PAUSED,
    DBG_STEPPING,
    DBG_TERMINATED
} dbg_state_t;

typedef enum {
    BP_NORMAL,
    BP_CONDITIONAL,
    BP_HITCOUNT,
    BP_LOGPOINT     /* doesn't break, logs a message */
} bp_type_t;

/* A breakpoint */
typedef struct {
    char      file[512];
    int       line;             /* 0-based */
    bool      enabled;
    bool      verified;
    int       id;

    bp_type_t type;
    char      condition[256];
    int       hit_count_target;
    int       hit_count;
    char      log_message[256];
} dbg_breakpoint_t;

typedef struct {
    char      expression[256];
    char      value[256];
    char      type[64];
    bool      valid;
    bool      has_children;
} dbg_watch_t;

typedef struct {
    char      name[128];
    char      value[256];
    char      type[64];
    int       scope;
    bool      has_children;
} dbg_local_t;

/* An expanded child variable (五期: DWARF-backed field expansion). */
typedef struct {
    char      name[128];
    char      value[256];
    char      type[128];
    int       expand_ref;
} dbg_var_t;

#define DBG_VARREF_DYN      4000
#define DBG_MAX_VAR_REFS    128
#define DBG_VAR_NAME_CAP    64

typedef struct {
    int       id;
    char      name[128];
    bool      running;
} dbg_thread_t;

typedef struct {
    char      function_name[128];
    char      file[512];
    int       line;
    int       col;
    int       frame_id;
} dbg_frame_t;

typedef struct {
    char      text[512];
    int       category;     /* 0=stdout, 1=stderr, 2=debug, 3=info */
} dbg_output_entry_t;

typedef struct {
    dbg_state_t     state;

    /* breakpoints */
    dbg_breakpoint_t breakpoints[DBG_MAX_BREAKPOINTS];
    int              bp_count;
    int              bp_next_id;

    dbg_watch_t     watches[DBG_MAX_WATCHES];
    int             watch_count;

    dbg_local_t     locals[DBG_MAX_LOCALS];
    int             local_count;

    bool            var_created[DBG_MAX_LOCALS];
    long            var_gen;
    char            var_refs[DBG_MAX_VAR_REFS][DBG_VAR_NAME_CAP];
    int             var_ref_count;

    dbg_thread_t    threads[DBG_MAX_THREADS];
    int             thread_count;
    int             current_thread;

    dbg_frame_t     callstack[DBG_MAX_CALLSTACK];
    int             callstack_depth;
    int             active_frame;

    char            output[DBG_MAX_OUTPUT];
    int             output_len;

    char            current_file[512];
    int             current_line;
    int             current_col;
    char            stop_reason[128];

#ifdef _WIN32
    void           *process_handle;
    void           *thread_handle;
    unsigned long   process_id;
    unsigned long   thread_id;
#else
    int             child_pid;
#endif

#ifdef _WIN32
    void           *gdb_in_w;
    void           *gdb_out_r;
    void           *gdb_proc;
#else
    int             gdb_in_fd;
    int             gdb_out_fd;
    int             gdb_pid;
#endif
    char            gdb_path[512];
    char            program_path[1024];
    int             mi_token;
    char            mi_buf[8192];
    int             mi_buf_len;
    int             last_exit_code;

    long            inferior_pid;
    bool            interrupt_requested;

    void          (*wait_hook)(void *user);
    void           *wait_hook_user;

    /* settings */
    bool            break_on_entry;
    bool            break_on_exception;
    bool            break_on_throw;
    int             exc_bp_throw;
    int             exc_bp_unhandled;
    bool            attached;
    bool            skip_stdlib;
} debugger_t;

void dbg_set_gdb_path(debugger_t *dbg, const char *path);

void dbg_init(debugger_t *dbg);

bool dbg_start(debugger_t *dbg, const char *program, const char *args);

bool dbg_attach(debugger_t *dbg, const char *program, int pid);

int dbg_set_exception_breakpoints(debugger_t *dbg, bool on_throw, bool on_unhandled);

void dbg_refresh_threads(debugger_t *dbg);

bool dbg_select_thread(debugger_t *dbg, int thread_id);

void dbg_stop(debugger_t *dbg);

void dbg_continue(debugger_t *dbg);

void dbg_set_wait_hook(debugger_t *dbg, void (*fn)(void *user), void *user);

bool dbg_interrupt(debugger_t *dbg);

void dbg_wait_stop(debugger_t *dbg);

/* Stepping */
void dbg_step_over(debugger_t *dbg);
void dbg_step_into(debugger_t *dbg);
void dbg_step_out(debugger_t *dbg);

/* Run to cursor */
void dbg_run_to_cursor(debugger_t *dbg, const char *file, int line);


int dbg_add_breakpoint(debugger_t *dbg, const char *file, int line);

int dbg_add_conditional_bp(debugger_t *dbg, const char *file, int line,
                           const char *condition);

int dbg_add_hitcount_bp(debugger_t *dbg, const char *file, int line, int count);

int dbg_add_logpoint(debugger_t *dbg, const char *file, int line,
                     const char *message);

/* Remove a breakpoint by ID */
bool dbg_remove_breakpoint(debugger_t *dbg, int bp_id);

/* Remove a breakpoint by location */
bool dbg_remove_breakpoint_at(debugger_t *dbg, const char *file, int line);

int dbg_toggle_breakpoint(debugger_t *dbg, const char *file, int line);

/* Enable/disable a breakpoint */
void dbg_enable_breakpoint(debugger_t *dbg, int bp_id, bool enabled);

/* Edit a breakpoint's condition */
void dbg_set_bp_condition(debugger_t *dbg, int bp_id, const char *condition);

bool dbg_has_breakpoint(debugger_t *dbg, const char *file, int line);

dbg_breakpoint_t *dbg_get_breakpoint_at(debugger_t *dbg, const char *file, int line);


int dbg_add_watch(debugger_t *dbg, const char *expression);

void dbg_remove_watch(debugger_t *dbg, int index);

void dbg_edit_watch(debugger_t *dbg, int index, const char *new_expression);

void dbg_evaluate_watches(debugger_t *dbg);

bool dbg_evaluate(debugger_t *dbg, const char *expression, char *result, int result_size);


void dbg_refresh_locals(debugger_t *dbg);

bool dbg_type_expandable(const char *ty);

int dbg_expand_variables(debugger_t *dbg, int ref, dbg_var_t *out, int cap);

void dbg_refresh_callstack(debugger_t *dbg);

void dbg_select_frame(debugger_t *dbg, int frame_index);

/* --- Output --- */

void dbg_append_output(debugger_t *dbg, const char *text);

void dbg_clear_output(debugger_t *dbg);

/* --- Utility --- */

bool dbg_is_current_line(debugger_t *dbg, const char *file, int line);

bool dbg_set_variable(debugger_t *dbg, const char *name, const char *value);

bool dbg_get_exception_info(debugger_t *dbg, char *info, int info_size);

#ifdef __cplusplus
}
#endif

#endif /* ZAN_DEBUGGER_H */
