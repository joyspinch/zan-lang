/* diag.h -- Diagnostic reporting (errors, warnings, notes). */

#ifndef ZAN_DIAG_H
#define ZAN_DIAG_H

#include "zan.h"

typedef enum {
    DIAG_ERROR,
    DIAG_WARNING,
    DIAG_NOTE,
} zan_diag_level_t;

struct zan_diag {
    int error_count;
    int warning_count;
    int max_errors;
    const char *const *file_names;  /* indexed by file_id */
    const char *const *file_sources; /* indexed by file_id */
    int file_count;
};

zan_diag_t *zan_diag_new(zan_arena_t *arena);
void zan_diag_add_file(zan_diag_t *diag, const char *name, const char *source);
void zan_diag_emit(zan_diag_t *diag, zan_diag_level_t level, zan_loc_t loc,
                   const char *fmt, ...);
bool zan_diag_has_errors(zan_diag_t *diag);

#endif /* ZAN_DIAG_H */
