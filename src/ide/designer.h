/* designer.h -- Visual Form/Window Designer for Zan IDE.
 *
 * Provides a drag-and-drop WYSIWYG form designer that generates
 * Zan source code for window layouts. Supports:
 *   - Control palette (Button, Label, TextBox, ComboBox, ListView, etc.)
 *   - Drag & drop placement on design surface
 *   - Property grid for editing control properties
 *   - Snap-to-grid alignment
 *   - Undo/redo for design operations
 *   - Code generation (generates .zan source for the form)
 *   - Anchoring and docking layout
 *   - Event handler stub generation
 */

#ifndef DESIGNER_H
#define DESIGNER_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Control types available in the designer ---- */
typedef enum {
    DCTL_BUTTON,
    DCTL_LABEL,
    DCTL_TEXTBOX,
    DCTL_CHECKBOX,
    DCTL_RADIOBUTTON,
    DCTL_COMBOBOX,
    DCTL_LISTBOX,
    DCTL_LISTVIEW,
    DCTL_TREEVIEW,
    DCTL_PANEL,
    DCTL_GROUPBOX,
    DCTL_TABCONTROL,
    DCTL_PICTUREBOX,
    DCTL_PROGRESSBAR,
    DCTL_TRACKBAR,
    DCTL_MENUSTRIP,
    DCTL_TOOLBAR,
    DCTL_STATUSBAR,
    DCTL_DATAGRIDVIEW,
    DCTL_SPLITTER,
    DCTL_TIMER,
    DCTL_NUMERICUPDOWN,
    DCTL_DATETIMEPICKER,
    DCTL_RICHTEXTBOX,
    DCTL_WEBBROWSER,
    DCTL_CUSTOM,
    DCTL_COUNT
} designer_ctrl_type_t;

/* Anchor flags for layout */
#define ANCHOR_NONE    0x00
#define ANCHOR_TOP     0x01
#define ANCHOR_BOTTOM  0x02
#define ANCHOR_LEFT    0x04
#define ANCHOR_RIGHT   0x08
#define ANCHOR_ALL     (ANCHOR_TOP | ANCHOR_BOTTOM | ANCHOR_LEFT | ANCHOR_RIGHT)

/* Dock modes */
typedef enum {
    DOCK_NONE,
    DOCK_TOP,
    DOCK_BOTTOM,
    DOCK_LEFT,
    DOCK_RIGHT,
    DOCK_FILL
} dock_mode_t;

/* Property types */
typedef enum {
    PROP_STRING,
    PROP_INT,
    PROP_BOOL,
    PROP_COLOR,
    PROP_FONT,
    PROP_ENUM,
    PROP_EVENT
} prop_type_t;

/* A single property value */
#define PROP_MAX_VALUE 256
typedef struct {
    char        name[64];
    char        value[PROP_MAX_VALUE];
    char        category[32];      /* "Layout", "Appearance", "Behavior", "Events" */
    prop_type_t type;
    bool        is_default;
} designer_prop_t;

#define DESIGNER_MAX_PROPS  64
#define DESIGNER_MAX_EVENTS 32

/* Event handler binding */
typedef struct {
    char event_name[64];       /* e.g. "Click", "TextChanged" */
    char handler_name[128];    /* e.g. "btnOK_Click" */
} event_binding_t;

/* A control instance on the design surface */
typedef struct {
    int                   id;           /* unique control ID */
    designer_ctrl_type_t  type;
    char                  name[64];     /* e.g. "btnSubmit" */
    char                  text[128];    /* display text */
    int                   x, y;         /* position on form */
    int                   w, h;         /* size */
    int                   anchor;       /* ANCHOR_* flags */
    dock_mode_t           dock;
    int                   tab_index;
    bool                  visible;
    bool                  enabled;
    int                   parent_id;    /* -1 = form, else container ctrl id */

    /* Custom properties */
    designer_prop_t       props[DESIGNER_MAX_PROPS];
    int                   prop_count;

    /* Event bindings */
    event_binding_t       events[DESIGNER_MAX_EVENTS];
    int                   event_count;

    /* Visual state (runtime only) */
    bool                  selected;
    bool                  hovered;
    int                   resize_handle;  /* which handle is being dragged */
} designer_control_t;

/* Undo operation */
typedef enum {
    UNDO_ADD_CONTROL,
    UNDO_DELETE_CONTROL,
    UNDO_MOVE_CONTROL,
    UNDO_RESIZE_CONTROL,
    UNDO_CHANGE_PROPERTY,
    UNDO_REORDER
} undo_type_t;

typedef struct {
    undo_type_t type;
    int         ctrl_id;
    int         old_x, old_y, old_w, old_h;
    char        old_value[PROP_MAX_VALUE];
    char        prop_name[64];
    designer_control_t saved_ctrl;  /* for delete undo */
} undo_entry_t;

#define DESIGNER_MAX_CONTROLS 256
#define DESIGNER_MAX_UNDO     100

/* Form properties */
typedef struct {
    char   title[128];
    int    width;
    int    height;
    char   class_name[64];       /* generated class name */
    char   namespace_name[128];
    bool   maximize_box;
    bool   minimize_box;
    bool   resizable;
    bool   show_in_taskbar;
    char   icon_path[512];
    char   start_position[32];   /* "CenterScreen", "Manual", etc. */
    int    background_color;     /* RGB */
} form_props_t;

/* The designer state */
typedef struct {
    bool                active;         /* is designer tab open */
    form_props_t        form;
    designer_control_t  controls[DESIGNER_MAX_CONTROLS];
    int                 control_count;
    int                 next_id;
    int                 selected_id;    /* -1 = none, 0+ = control id */
    int                 grid_size;      /* snap grid (default 8) */
    bool                show_grid;
    float               zoom;           /* 1.0 = 100% */

    /* Drag state */
    bool                dragging;
    bool                resizing;
    int                 drag_start_x, drag_start_y;
    int                 drag_ctrl_orig_x, drag_ctrl_orig_y;
    int                 drag_ctrl_orig_w, drag_ctrl_orig_h;
    int                 resize_handle;  /* 0-7: corners and edges */

    /* Toolbox selection */
    designer_ctrl_type_t tool_selected; /* which control type to place next */
    bool                 placing;       /* in placement mode */

    /* Undo/redo */
    undo_entry_t        undo_stack[DESIGNER_MAX_UNDO];
    int                 undo_count;
    int                 undo_pos;

    /* Code generation output */
    char               *generated_code;
    size_t              generated_len;

    /* File association */
    char                form_file[512];    /* .zan file path */
    char                designer_file[512]; /* .designer.zan file path */
    bool                modified;
} designer_t;

/* ---- API ---- */

/* Initialize designer state */
void designer_init(designer_t *d);

/* Reset designer to empty form */
void designer_new_form(designer_t *d, const char *class_name, const char *ns);

/* Load a form from a .designer.zan file */
bool designer_load(designer_t *d, const char *filepath);

/* Save the form design to a .designer.zan file */
bool designer_save(designer_t *d, const char *filepath);

/* Add a control to the design surface */
int designer_add_control(designer_t *d, designer_ctrl_type_t type,
                         const char *name, int x, int y, int w, int h);

/* Delete selected control */
bool designer_delete_selected(designer_t *d);

/* Select control at given position */
int designer_hit_test(designer_t *d, int x, int y);

/* Move selected control */
void designer_move_control(designer_t *d, int ctrl_id, int new_x, int new_y);

/* Resize selected control */
void designer_resize_control(designer_t *d, int ctrl_id, int new_w, int new_h);

/* Get/Set control property */
const char *designer_get_prop(designer_t *d, int ctrl_id, const char *prop_name);
void designer_set_prop(designer_t *d, int ctrl_id, const char *prop_name,
                       const char *value);

/* Undo / Redo */
void designer_undo(designer_t *d);
void designer_redo(designer_t *d);

/* Snap position to grid */
void designer_snap_to_grid(designer_t *d, int *x, int *y);

/* Generate Zan source code from the form design */
char *designer_generate_code(designer_t *d, size_t *out_len);

/* Generate event handler stubs for unimplemented events */
char *designer_generate_event_stubs(designer_t *d, size_t *out_len);

/* Get default size for a control type */
void designer_default_size(designer_ctrl_type_t type, int *w, int *h);

/* Get the display name of a control type */
const char *designer_ctrl_type_name(designer_ctrl_type_t type);

/* Add event binding to a control */
void designer_add_event(designer_t *d, int ctrl_id,
                        const char *event_name, const char *handler_name);

/* Align selected controls */
void designer_align_left(designer_t *d);
void designer_align_top(designer_t *d);
void designer_align_right(designer_t *d);
void designer_align_bottom(designer_t *d);
void designer_center_horizontal(designer_t *d);
void designer_center_vertical(designer_t *d);
void designer_same_width(designer_t *d);
void designer_same_height(designer_t *d);

/* Tab order management */
void designer_auto_tab_order(designer_t *d);

#ifdef _WIN32
/* Paint the design surface onto a Win32 DC */
void designer_paint(designer_t *d, void *hdc, int offset_x, int offset_y,
                    int view_w, int view_h);

/* Paint the toolbox palette */
void designer_paint_toolbox(designer_t *d, void *hdc, int x, int y, int w, int h);

/* Paint the property grid for selected control */
void designer_paint_properties(designer_t *d, void *hdc, int x, int y, int w, int h);
#endif

#ifdef __cplusplus
}
#endif

#endif /* DESIGNER_H */
