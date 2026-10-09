/* 语言服务与调试协议交互规范 */
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

/* 核心系统底层抽象与内存语义契约 */
typedef enum {
    DBG_IDLE,       /* 核心系统底层抽象与内存语义契约 */
    DBG_RUNNING,    /* 核心系统底层抽象与内存语义契约 */
    DBG_PAUSED,     /* 核心系统底层抽象与内存语义契约 */
    DBG_STEPPING,   /* 核心系统底层抽象与内存语义契约 */
    DBG_TERMINATED  /* 核心系统底层抽象与内存语义契约 */
} dbg_state_t;

/* 核心系统底层抽象与内存语义契约 */
typedef enum {
    BP_NORMAL,      /* 核心系统底层抽象与内存语义契约 */
    BP_CONDITIONAL, /* 核心系统底层抽象与内存语义契约 */
    BP_HITCOUNT,    /* 核心系统底层抽象与内存语义契约 */
    BP_LOGPOINT     /* doesn't break, logs a message */
} bp_type_t;

/* A breakpoint */
typedef struct {
    char      file[512];
    int       line;             /* 0-based */
    bool      enabled;
    bool      verified;         /* 底层系统交互与数据协议契约 */
    int       id;               /* 核心系统底层抽象与内存语义契约 */

    bp_type_t type;
    char      condition[256];   /* 核心系统底层抽象与内存语义契约 */
    int       hit_count_target; /* 核心系统底层抽象与内存语义契约 */
    int       hit_count;        /* 核心系统底层抽象与内存语义契约 */
    char      log_message[256]; /* 核心系统底层抽象与内存语义契约 */
} dbg_breakpoint_t;

/* 核心系统底层抽象与内存语义契约 */
typedef struct {
    char      expression[256];
    char      value[256];       /* 核心系统底层抽象与内存语义契约 */
    char      type[64];         /* 核心系统底层抽象与内存语义契约 */
    bool      valid;            /* 核心系统底层抽象与内存语义契约 */
    bool      has_children;     /* 核心系统底层抽象与内存语义契约 */
} dbg_watch_t;

/* 核心系统底层抽象与内存语义契约 */
typedef struct {
    char      name[128];
    char      value[256];
    char      type[64];
    int       scope;            /* 核心系统底层抽象与内存语义契约 */
    bool      has_children;
} dbg_local_t;

/* An expanded child variable (五期: DWARF-backed field expansion). */
typedef struct {
    char      name[128];
    char      value[256];
    char      type[128];
    int       expand_ref;       /* 核心系统底层抽象与内存语义契约 */
} dbg_var_t;

/* 模块核心语义抽象与接口调用契约 */
#define DBG_VARREF_DYN      4000
#define DBG_MAX_VAR_REFS    128
#define DBG_VAR_NAME_CAP    64

/* 底层系统交互与数据协议契约 */
typedef struct {
    int       id;               /* 底层系统交互与数据协议契约 */
    char      name[128];        /* 核心系统底层抽象与内存语义契约 */
    bool      running;
} dbg_thread_t;

/* 核心系统底层抽象与内存语义契约 */
typedef struct {
    char      function_name[128];
    char      file[512];
    int       line;
    int       col;
    int       frame_id;
} dbg_frame_t;

/* 核心系统底层抽象与内存语义契约 */
typedef struct {
    char      text[512];
    int       category;     /* 0=stdout, 1=stderr, 2=debug, 3=info */
} dbg_output_entry_t;

/* 核心系统底层抽象与内存语义契约 */
typedef struct {
    dbg_state_t     state;

    /* breakpoints */
    dbg_breakpoint_t breakpoints[DBG_MAX_BREAKPOINTS];
    int              bp_count;
    int              bp_next_id;

    /* 核心系统底层抽象与内存语义契约 */
    dbg_watch_t     watches[DBG_MAX_WATCHES];
    int             watch_count;

    /* 核心系统底层抽象与内存语义契约 */
    dbg_local_t     locals[DBG_MAX_LOCALS];
    int             local_count;

    /* 语言服务与调试协议交互规范 */
    bool            var_created[DBG_MAX_LOCALS];
    long            var_gen;            /* 底层系统交互与数据协议契约 */
    char            var_refs[DBG_MAX_VAR_REFS][DBG_VAR_NAME_CAP];
    int             var_ref_count;

    /* 核心系统底层抽象与内存语义契约 */
    dbg_thread_t    threads[DBG_MAX_THREADS];
    int             thread_count;
    int             current_thread;

    /* 核心系统底层抽象与内存语义契约 */
    dbg_frame_t     callstack[DBG_MAX_CALLSTACK];
    int             callstack_depth;
    int             active_frame;   /* 核心系统底层抽象与内存语义契约 */

    /* 核心系统底层抽象与内存语义契约 */
    char            output[DBG_MAX_OUTPUT];
    int             output_len;

    /* 核心系统底层抽象与内存语义契约 */
    char            current_file[512];
    int             current_line;
    int             current_col;
    char            stop_reason[128];   /* 核心系统底层抽象与内存语义契约 */

    /* 核心系统底层抽象与内存语义契约 */
#ifdef _WIN32
    void           *process_handle;
    void           *thread_handle;
    unsigned long   process_id;
    unsigned long   thread_id;
#else
    int             child_pid;
#endif

    /* 语言服务与调试协议交互规范 */
#ifdef _WIN32
    void           *gdb_in_w;   /* 核心系统底层抽象与内存语义契约 */
    void           *gdb_out_r;  /* 核心系统底层抽象与内存语义契约 */
    void           *gdb_proc;   /* 核心系统底层抽象与内存语义契约 */
#else
    int             gdb_in_fd;  /* 核心系统底层抽象与内存语义契约 */
    int             gdb_out_fd; /* 核心系统底层抽象与内存语义契约 */
    int             gdb_pid;
#endif
    char            gdb_path[512];  /* 核心系统底层抽象与内存语义契约 */
    char            program_path[1024];
    int             mi_token;       /* 核心系统底层抽象与内存语义契约 */
    char            mi_buf[8192];   /* 核心系统底层抽象与内存语义契约 */
    int             mi_buf_len;
    int             last_exit_code;

    /* 语言服务与调试协议交互规范 */
    long            inferior_pid;
    bool            interrupt_requested;

    /* 语言服务与调试协议交互规范 */
    void          (*wait_hook)(void *user);
    void           *wait_hook_user;

    /* settings */
    bool            break_on_entry;     /* 核心系统底层抽象与内存语义契约 */
    bool            break_on_exception; /* 核心系统底层抽象与内存语义契约 */
    bool            break_on_throw;     /* 核心系统底层抽象与内存语义契约 */
    int             exc_bp_throw;       /* 核心系统底层抽象与内存语义契约 */
    int             exc_bp_unhandled;
    bool            attached;           /* 核心系统底层抽象与内存语义契约 */
    bool            skip_stdlib;        /* 核心系统底层抽象与内存语义契约 */
} debugger_t;

/* 语言服务与调试协议交互规范 */
void dbg_set_gdb_path(debugger_t *dbg, const char *path);

/* 核心系统底层抽象与内存语义契约 */
void dbg_init(debugger_t *dbg);

/* 核心系统底层抽象与内存语义契约 */
bool dbg_start(debugger_t *dbg, const char *program, const char *args);

/* 语言服务与调试协议交互规范 */
bool dbg_attach(debugger_t *dbg, const char *program, int pid);

/* 语言服务与调试协议交互规范 */
int dbg_set_exception_breakpoints(debugger_t *dbg, bool on_throw, bool on_unhandled);

/* 底层系统交互与数据协议契约 */
void dbg_refresh_threads(debugger_t *dbg);

/* 模块核心语义抽象与接口调用契约 */
bool dbg_select_thread(debugger_t *dbg, int thread_id);

/* 核心系统底层抽象与内存语义契约 */
void dbg_stop(debugger_t *dbg);

/* 核心系统底层抽象与内存语义契约 */
void dbg_continue(debugger_t *dbg);

/* 语言服务与调试协议交互规范 */
void dbg_set_wait_hook(debugger_t *dbg, void (*fn)(void *user), void *user);

/* 语言服务与调试协议交互规范 */
bool dbg_interrupt(debugger_t *dbg);

/* 语言服务与调试协议交互规范 */
void dbg_wait_stop(debugger_t *dbg);

/* Stepping */
void dbg_step_over(debugger_t *dbg);
void dbg_step_into(debugger_t *dbg);
void dbg_step_out(debugger_t *dbg);

/* Run to cursor */
void dbg_run_to_cursor(debugger_t *dbg, const char *file, int line);

/* 核心系统底层抽象与内存语义契约 */

/* 核心系统底层抽象与内存语义契约 */
int dbg_add_breakpoint(debugger_t *dbg, const char *file, int line);

/* 核心系统底层抽象与内存语义契约 */
int dbg_add_conditional_bp(debugger_t *dbg, const char *file, int line,
                           const char *condition);

/* 核心系统底层抽象与内存语义契约 */
int dbg_add_hitcount_bp(debugger_t *dbg, const char *file, int line, int count);

/* 模块核心语义抽象与接口调用契约 */
int dbg_add_logpoint(debugger_t *dbg, const char *file, int line,
                     const char *message);

/* Remove a breakpoint by ID */
bool dbg_remove_breakpoint(debugger_t *dbg, int bp_id);

/* Remove a breakpoint by location */
bool dbg_remove_breakpoint_at(debugger_t *dbg, const char *file, int line);

/* 核心系统底层抽象与内存语义契约 */
int dbg_toggle_breakpoint(debugger_t *dbg, const char *file, int line);

/* Enable/disable a breakpoint */
void dbg_enable_breakpoint(debugger_t *dbg, int bp_id, bool enabled);

/* Edit a breakpoint's condition */
void dbg_set_bp_condition(debugger_t *dbg, int bp_id, const char *condition);

/* 核心系统底层抽象与内存语义契约 */
bool dbg_has_breakpoint(debugger_t *dbg, const char *file, int line);

/* 核心系统底层抽象与内存语义契约 */
dbg_breakpoint_t *dbg_get_breakpoint_at(debugger_t *dbg, const char *file, int line);

/* 核心系统底层抽象与内存语义契约 */

/* 核心系统底层抽象与内存语义契约 */
int dbg_add_watch(debugger_t *dbg, const char *expression);

/* 核心系统底层抽象与内存语义契约 */
void dbg_remove_watch(debugger_t *dbg, int index);

/* 核心系统底层抽象与内存语义契约 */
void dbg_edit_watch(debugger_t *dbg, int index, const char *new_expression);

/* 底层系统交互与数据协议契约 */
void dbg_evaluate_watches(debugger_t *dbg);

/* 底层系统交互与数据协议契约 */
bool dbg_evaluate(debugger_t *dbg, const char *expression, char *result, int result_size);

/* 核心系统底层抽象与内存语义契约 */

/* 底层系统交互与数据协议契约 */
void dbg_refresh_locals(debugger_t *dbg);

/* 模块核心语义抽象与接口调用契约 */
bool dbg_type_expandable(const char *ty);

/* 语言服务与调试协议交互规范 */
int dbg_expand_variables(debugger_t *dbg, int ref, dbg_var_t *out, int cap);

/* 核心系统底层抽象与内存语义契约 */
void dbg_refresh_callstack(debugger_t *dbg);

/* 底层系统交互与数据协议契约 */
void dbg_select_frame(debugger_t *dbg, int frame_index);

/* --- Output --- */

/* 核心系统底层抽象与内存语义契约 */
void dbg_append_output(debugger_t *dbg, const char *text);

/* 核心系统底层抽象与内存语义契约 */
void dbg_clear_output(debugger_t *dbg);

/* --- Utility --- */

/* 底层系统交互与数据协议契约 */
bool dbg_is_current_line(debugger_t *dbg, const char *file, int line);

/* 核心系统底层抽象与内存语义契约 */
bool dbg_set_variable(debugger_t *dbg, const char *name, const char *value);

/* 核心系统底层抽象与内存语义契约 */
bool dbg_get_exception_info(debugger_t *dbg, char *info, int info_size);

#ifdef __cplusplus
}
#endif

#endif /* ZAN_DEBUGGER_H */
