/* debugger.h -- Debugging support for the Zan IDE.
 *
 * Provides breakpoint management, step control, variable inspection,
 * and call stack display. Communicates with the compiled program via
 * debug info embedded in the LLVM IR.
 */
#ifndef ZAN_DEBUGGER_H
#define ZAN_DEBUGGER_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DBG_MAX_BREAKPOINTS 256
#define DBG_MAX_VARIABLES   512
#define DBG_MAX_FRAMES      64

/* Debugger state */
typedef enum {
    DBG_IDLE,       /* not debugging */
    DBG_RUNNING,    /* program running */
    DBG_PAUSED,     /* hit breakpoint or step */
    DBG_STOPPED     /* program exited */
} dbg_state_t;

/* Step mode */
typedef enum {
    DBG_STEP_OVER,
    DBG_STEP_INTO,
    DBG_STEP_OUT
} dbg_step_t;

/* A breakpoint */
typedef struct {
    char        file[512];
    int         line;       /* 0-based */
    bool        enabled;
    bool        hit;        /* currently stopped at this BP */
    int         hit_count;
    char        condition[256]; /* conditional expression (future) */
} dbg_breakpoint_t;

/* A variable in the watch/locals panel */
typedef struct {
    char        name[128];
    char        type[64];
    char        value[256];
    int         scope_depth;
    bool        is_local;
} dbg_variable_t;

/* A stack frame */
typedef struct {
    char        function[256];
    char        file[512];
    int         line;
    int         col;
} dbg_frame_t;

/* The debugger engine */
typedef struct {
    dbg_state_t     state;

    /* breakpoints */
    dbg_breakpoint_t breakpoints[DBG_MAX_BREAKPOINTS];
    int              bp_count;

    /* variables */
    dbg_variable_t   locals[DBG_MAX_VARIABLES];
    int              local_count;

    /* call stack */
    dbg_frame_t      frames[DBG_MAX_FRAMES];
    int              frame_count;
    int              current_frame;

    /* current position */
    char             current_file[512];
    int              current_line;

    /* process handle */
#ifdef _WIN32
    void            *process_handle;   /* HANDLE */
    void            *thread_handle;    /* HANDLE */
    unsigned long    process_id;
#else
    int              child_pid;
#endif
} debugger_t;

/* Initialize debugger */
void dbg_init(debugger_t *dbg);

/* Add/remove breakpoints */
int  dbg_add_breakpoint(debugger_t *dbg, const char *file, int line);
void dbg_remove_breakpoint(debugger_t *dbg, int index);
void dbg_toggle_breakpoint(debugger_t *dbg, const char *file, int line);
dbg_breakpoint_t *dbg_find_breakpoint(debugger_t *dbg, const char *file, int line);

/* Start debugging a program */
bool dbg_start(debugger_t *dbg, const char *exe_path, const char *args);

/* Stop debugging */
void dbg_stop(debugger_t *dbg);

/* Continue execution */
void dbg_continue(debugger_t *dbg);

/* Step operations */
void dbg_step_over(debugger_t *dbg);
void dbg_step_into(debugger_t *dbg);
void dbg_step_out(debugger_t *dbg);

/* Get current state */
dbg_state_t dbg_get_state(const debugger_t *dbg);

/* Check if a line has a breakpoint */
bool dbg_has_breakpoint(const debugger_t *dbg, const char *file, int line);

/* Is the debugger currently stopped at this line? */
bool dbg_is_current_line(const debugger_t *dbg, const char *file, int line);

#ifdef __cplusplus
}
#endif

#endif /* ZAN_DEBUGGER_H */
