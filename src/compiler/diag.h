/* diag.h -- Diagnostic reporting (errors, warnings, notes). */

#ifndef ZAN_DIAG_H
#define ZAN_DIAG_H

#include "zan.h"

typedef enum {
    DIAG_ERROR,
    DIAG_WARNING,
    DIAG_NOTE,
} zan_diag_level_t;

/* Errors printed for one source line before the rest are suppressed as
 * cascade noise (see zan_diag_t.dup_line_errors). */
#define ZAN_DIAG_MAX_ERRORS_PER_LINE 8

/* Longest source-line excerpt echoed with a diagnostic; longer lines are shown
 * as a window around the error column. */
#define ZAN_DIAG_MAX_SOURCE_ECHO 200

/* A single captured diagnostic (used by the language server). */
typedef struct {
    zan_diag_level_t level;
    zan_loc_t        loc;
    char             message[512];
} zan_diag_entry_t;

struct zan_diag {
    int error_count;
    int warning_count;
    int max_errors;
    const char *const *file_names;  /* indexed by file_id */
    const char *const *file_sources; /* indexed by file_id */
    int file_count;

    /* structured capture (opt-in, used by tooling like the LSP server).
     * When capture is enabled, diagnostics are stored in `entries` and the
     * usual stderr rendering is suppressed. */
    bool              capture;
    bool              treat_warnings_as_errors;
    zan_diag_entry_t *entries;
    int               entry_count;
    int               entry_cap;

    /* Cascade suppression: error recovery re-reports failures at each
     * successive column while it walks an over-deep or otherwise malformed
     * expression, which turned a single over-deep parenthesis nest into
     * thousands of copies of a handful of messages. At most
     * ZAN_DIAG_MAX_ERRORS_PER_LINE errors are printed for one source line;
     * error_count still counts them all, so the build still fails. */
    uint32_t dup_file_id;
    uint32_t dup_line;
    int      dup_line_errors;
};

zan_diag_t *zan_diag_new(zan_arena_t *arena);
void zan_diag_set_deny_warnings(zan_diag_t *diag, bool enabled);
void zan_diag_add_file(zan_diag_t *diag, const char *name, const char *source);
void zan_diag_emit(zan_diag_t *diag, zan_diag_level_t level, zan_loc_t loc,
                   const char *fmt, ...);
bool zan_diag_has_errors(zan_diag_t *diag);

/* ---- structured capture API (for tooling) ---- */

/* Enable/disable structured capture. While enabled, zan_diag_emit stores
 * entries instead of printing them to stderr. */
void zan_diag_set_capture(zan_diag_t *diag, bool enabled);

/* Access captured diagnostics. */
int  zan_diag_entry_count(const zan_diag_t *diag);
const zan_diag_entry_t *zan_diag_entry_at(const zan_diag_t *diag, int index);

/* Release the heap-allocated capture buffer (and file arrays). Safe to call
 * on any diag; leaves the struct itself intact. */
void zan_diag_free_buffers(zan_diag_t *diag);

/* Progress trace, printed to stderr only when ZANC_TRACE is set in the
 * environment. Declared here rather than in irgen.h because the front end is
 * also compiled into zan-lsp without LLVM, and the long phases need a trace
 * too: the last line printed is the phase that never came back. */
void zan_compile_trace(const char *fmt, ...);

#endif /* ZAN_DIAG_H */
