#ifndef ZAN_GUI_ENTRY_H
#define ZAN_GUI_ENTRY_H

#include "ast.h"

/* Check for existing entries before the driver loads project settings. */
bool zan_gui_needs_entry(const zan_ast_node_t *unit);

/* Call when zan_gui_needs_entry succeeds, before final namespace resolution. */
void zan_gui_synthesize_entry(zan_ast_node_t *unit, zan_arena_t *arena,
                              zan_diag_t *diag, const char *project_type,
                              const char *project_entry, const char *input_file);

#endif
