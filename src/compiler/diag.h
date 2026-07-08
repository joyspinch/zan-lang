/* diag.h -- Diagnostic reporting (errors, warnings, notes). */

#ifndef ZAN_DIAG_H
#define ZAN_DIAG_H

#include "zan.h"

typedef enum {
    DIAG_ERROR,
    DIAG_WARNING,
    DIAG_NOTE,
} zan_diag_level_t;

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
    zan_diag_entry_t *entries;
    int               entry_count;
    int               entry_cap;
};

zan_diag_t *zan_diag_new(zan_arena_t *arena);
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

#endif /* ZAN_DIAG_H */
