/* project.h -- Project tree and file browser for the Zan IDE. */
#ifndef ZAN_PROJECT_H
#define ZAN_PROJECT_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PROJECT_MAX_ENTRIES 4096
#define PROJECT_MAX_PATH    512

/* File entry type */
typedef enum {
    ENTRY_FILE,
    ENTRY_DIR
} entry_type_t;

/* A single entry in the project tree */
typedef struct {
    char           name[256];
    char           full_path[PROJECT_MAX_PATH];
    entry_type_t   type;
    int            depth;       /* nesting level (0 = root) */
    bool           expanded;    /* for directories */
    bool           visible;     /* should be displayed (parent expanded) */
} project_entry_t;

/* Project state */
typedef struct {
    char              root_path[PROJECT_MAX_PATH];
    char              root_name[256];
    project_entry_t   entries[PROJECT_MAX_ENTRIES];
    int               entry_count;
    int               selected;     /* selected entry index (-1 = none) */
    int               scroll;       /* scroll offset in entries */
    int               visible_rows; /* how many rows fit in the panel */
    bool              is_open;      /* is a project open? */
} project_tree_t;

/* Initialize project tree */
void project_init(project_tree_t *pt);

/* Open a project directory. Scans files recursively. Returns true on success. */
bool project_open(project_tree_t *pt, const char *dir_path);

/* Close the project */
void project_close(project_tree_t *pt);

/* Refresh the project tree (re-scan files) */
void project_refresh(project_tree_t *pt);

/* Toggle expand/collapse of a directory entry */
void project_toggle(project_tree_t *pt, int index);

/* Get the selected entry (NULL if none) */
project_entry_t *project_selected(project_tree_t *pt);

/* Move selection up */
void project_select_up(project_tree_t *pt);

/* Move selection down */
void project_select_down(project_tree_t *pt);

/* Count visible entries */
int project_visible_count(const project_tree_t *pt);

/* Get the nth visible entry */
project_entry_t *project_visible_entry(project_tree_t *pt, int visible_idx);

/* Ensure selected entry is visible in scroll view */
void project_ensure_visible(project_tree_t *pt);

#ifdef __cplusplus
}
#endif

#endif /* ZAN_PROJECT_H */
