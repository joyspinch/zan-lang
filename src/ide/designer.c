/* designer.c -- Visual Form/Window Designer implementation.
 *
 * Provides a WYSIWYG designer for creating window forms in Zan.
 * Generates .designer.zan files with InitializeComponent() code.
 */

#include "designer.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

#ifdef _WIN32
#include <windows.h>
#endif

/* ---- Control type metadata ---- */

typedef struct {
    const char *name;
    const char *prefix;     /* default name prefix: "btn", "lbl", etc. */
    int         def_w;
    int         def_h;
    const char *events[8];  /* common events */
} ctrl_meta_t;

static const ctrl_meta_t ctrl_meta[DCTL_COUNT] = {
    [DCTL_BUTTON]         = {"Button",         "btn",   100,  30, {"Click", "MouseEnter", "MouseLeave", NULL}},
    [DCTL_LABEL]          = {"Label",          "lbl",   100,  20, {"Click", NULL}},
    [DCTL_TEXTBOX]        = {"TextBox",        "txt",   120,  24, {"TextChanged", "KeyDown", "KeyUp", "Leave", NULL}},
    [DCTL_CHECKBOX]       = {"CheckBox",       "chk",   120,  22, {"CheckedChanged", "Click", NULL}},
    [DCTL_RADIOBUTTON]    = {"RadioButton",    "rdo",   120,  22, {"CheckedChanged", NULL}},
    [DCTL_COMBOBOX]       = {"ComboBox",       "cmb",   120,  24, {"SelectedIndexChanged", "DropDown", "TextChanged", NULL}},
    [DCTL_LISTBOX]        = {"ListBox",        "lst",   120, 100, {"SelectedIndexChanged", "DoubleClick", NULL}},
    [DCTL_LISTVIEW]       = {"ListView",       "lvw",   200, 150, {"SelectedIndexChanged", "DoubleClick", "ColumnClick", NULL}},
    [DCTL_TREEVIEW]       = {"TreeView",       "tvw",   180, 150, {"AfterSelect", "BeforeExpand", "DoubleClick", NULL}},
    [DCTL_PANEL]          = {"Panel",          "pnl",   200, 150, {"Paint", "Resize", NULL}},
    [DCTL_GROUPBOX]       = {"GroupBox",       "grp",   200, 120, {NULL}},
    [DCTL_TABCONTROL]     = {"TabControl",     "tab",   250, 180, {"SelectedIndexChanged", NULL}},
    [DCTL_PICTUREBOX]     = {"PictureBox",     "pic",   100, 100, {"Click", "Paint", NULL}},
    [DCTL_PROGRESSBAR]    = {"ProgressBar",    "prg",   200,  24, {NULL}},
    [DCTL_TRACKBAR]       = {"TrackBar",       "trk",   200,  45, {"ValueChanged", NULL}},
    [DCTL_MENUSTRIP]      = {"MenuStrip",      "mnu",   200,  24, {NULL}},
    [DCTL_TOOLBAR]        = {"ToolBar",        "tlb",   200,  28, {NULL}},
    [DCTL_STATUSBAR]      = {"StatusBar",      "stb",   200,  22, {NULL}},
    [DCTL_DATAGRIDVIEW]   = {"DataGridView",   "dgv",   300, 200, {"CellClick", "SelectionChanged", "CellValueChanged", NULL}},
    [DCTL_SPLITTER]       = {"Splitter",       "spl",     4, 100, {NULL}},
    [DCTL_TIMER]          = {"Timer",          "tmr",    32,  32, {"Tick", NULL}},
    [DCTL_NUMERICUPDOWN]  = {"NumericUpDown",  "nud",    80,  24, {"ValueChanged", NULL}},
    [DCTL_DATETIMEPICKER] = {"DateTimePicker", "dtp",   150,  24, {"ValueChanged", NULL}},
    [DCTL_RICHTEXTBOX]    = {"RichTextBox",    "rtb",   200, 100, {"TextChanged", NULL}},
    [DCTL_WEBBROWSER]     = {"WebBrowser",     "web",   300, 200, {"Navigated", "DocumentCompleted", NULL}},
    [DCTL_CUSTOM]         = {"CustomControl",  "ctl",   100, 100, {"Paint", NULL}},
};

/* ---- Implementation ---- */

void designer_init(designer_t *d) {
    memset(d, 0, sizeof(*d));
    d->grid_size = 8;
    d->show_grid = true;
    d->zoom = 1.0f;
    d->selected_id = -1;
    d->next_id = 1;
    d->tool_selected = DCTL_BUTTON;
    d->form.width = 640;
    d->form.height = 480;
    d->form.maximize_box = true;
    d->form.minimize_box = true;
    d->form.resizable = true;
    d->form.show_in_taskbar = true;
    d->form.background_color = 0xF0F0F0;
    strncpy(d->form.title, "Form1", sizeof(d->form.title) - 1);
    strncpy(d->form.class_name, "Form1", sizeof(d->form.class_name) - 1);
    strncpy(d->form.start_position, "CenterScreen", sizeof(d->form.start_position) - 1);
}

void designer_new_form(designer_t *d, const char *class_name, const char *ns) {
    designer_init(d);
    d->active = true;
    if (class_name) strncpy(d->form.class_name, class_name, sizeof(d->form.class_name) - 1);
    if (ns) strncpy(d->form.namespace_name, ns, sizeof(d->form.namespace_name) - 1);
}

const char *designer_ctrl_type_name(designer_ctrl_type_t type) {
    if (type >= 0 && type < DCTL_COUNT) return ctrl_meta[type].name;
    return "Unknown";
}

void designer_default_size(designer_ctrl_type_t type, int *w, int *h) {
    if (type >= 0 && type < DCTL_COUNT) {
        *w = ctrl_meta[type].def_w;
        *h = ctrl_meta[type].def_h;
    } else {
        *w = 100; *h = 30;
    }
}

void designer_snap_to_grid(designer_t *d, int *x, int *y) {
    if (d->grid_size > 1) {
        *x = ((*x + d->grid_size / 2) / d->grid_size) * d->grid_size;
        *y = ((*y + d->grid_size / 2) / d->grid_size) * d->grid_size;
    }
}

static designer_control_t *find_ctrl(designer_t *d, int id) {
    for (int i = 0; i < d->control_count; i++) {
        if (d->controls[i].id == id) return &d->controls[i];
    }
    return NULL;
}

static void push_undo(designer_t *d, undo_type_t type, int ctrl_id,
                      int ox, int oy, int ow, int oh,
                      const char *prop, const char *old_val) {
    if (d->undo_count >= DESIGNER_MAX_UNDO) {
        /* Shift stack */
        memmove(&d->undo_stack[0], &d->undo_stack[1],
                sizeof(undo_entry_t) * (DESIGNER_MAX_UNDO - 1));
        d->undo_count--;
    }
    /* Truncate redo history */
    d->undo_count = d->undo_pos;
    undo_entry_t *u = &d->undo_stack[d->undo_count];
    memset(u, 0, sizeof(*u));
    u->type = type;
    u->ctrl_id = ctrl_id;
    u->old_x = ox; u->old_y = oy;
    u->old_w = ow; u->old_h = oh;
    if (prop) strncpy(u->prop_name, prop, sizeof(u->prop_name) - 1);
    if (old_val) strncpy(u->old_value, old_val, sizeof(u->old_value) - 1);
    d->undo_count++;
    d->undo_pos = d->undo_count;
}

int designer_add_control(designer_t *d, designer_ctrl_type_t type,
                         const char *name, int x, int y, int w, int h) {
    if (d->control_count >= DESIGNER_MAX_CONTROLS) return -1;

    designer_snap_to_grid(d, &x, &y);

    designer_control_t *c = &d->controls[d->control_count];
    memset(c, 0, sizeof(*c));
    c->id = d->next_id++;
    c->type = type;
    c->x = x; c->y = y;
    c->w = w; c->h = h;
    c->visible = true;
    c->enabled = true;
    c->parent_id = -1;
    c->anchor = ANCHOR_TOP | ANCHOR_LEFT;
    c->dock = DOCK_NONE;
    c->tab_index = d->control_count;

    if (name && name[0]) {
        strncpy(c->name, name, sizeof(c->name) - 1);
    } else {
        /* Auto-generate name: prefix + id */
        snprintf(c->name, sizeof(c->name), "%s%d",
                 ctrl_meta[type].prefix, c->id);
    }
    /* Default text */
    strncpy(c->text, c->name, sizeof(c->text) - 1);

    d->control_count++;
    d->modified = true;

    push_undo(d, UNDO_ADD_CONTROL, c->id, 0, 0, 0, 0, NULL, NULL);
    return c->id;
}

bool designer_delete_selected(designer_t *d) {
    if (d->selected_id < 0) return false;
    for (int i = 0; i < d->control_count; i++) {
        if (d->controls[i].id == d->selected_id) {
            /* Save for undo */
            undo_entry_t *u = &d->undo_stack[d->undo_pos < DESIGNER_MAX_UNDO ? d->undo_pos : DESIGNER_MAX_UNDO - 1];
            u->saved_ctrl = d->controls[i];
            push_undo(d, UNDO_DELETE_CONTROL, d->controls[i].id, 0, 0, 0, 0, NULL, NULL);

            /* Remove from array */
            memmove(&d->controls[i], &d->controls[i + 1],
                    sizeof(designer_control_t) * (size_t)(d->control_count - i - 1));
            d->control_count--;
            d->selected_id = -1;
            d->modified = true;
            return true;
        }
    }
    return false;
}

int designer_hit_test(designer_t *d, int x, int y) {
    /* Check in reverse order (top-most first) */
    for (int i = d->control_count - 1; i >= 0; i--) {
        designer_control_t *c = &d->controls[i];
        if (x >= c->x && x <= c->x + c->w &&
            y >= c->y && y <= c->y + c->h) {
            return c->id;
        }
    }
    return -1;
}

void designer_move_control(designer_t *d, int ctrl_id, int new_x, int new_y) {
    designer_control_t *c = find_ctrl(d, ctrl_id);
    if (!c) return;
    designer_snap_to_grid(d, &new_x, &new_y);
    if (c->x != new_x || c->y != new_y) {
        push_undo(d, UNDO_MOVE_CONTROL, ctrl_id, c->x, c->y, c->w, c->h, NULL, NULL);
        c->x = new_x;
        c->y = new_y;
        d->modified = true;
    }
}

void designer_resize_control(designer_t *d, int ctrl_id, int new_w, int new_h) {
    designer_control_t *c = find_ctrl(d, ctrl_id);
    if (!c) return;
    if (new_w < 8) new_w = 8;
    if (new_h < 8) new_h = 8;
    if (c->w != new_w || c->h != new_h) {
        push_undo(d, UNDO_RESIZE_CONTROL, ctrl_id, c->x, c->y, c->w, c->h, NULL, NULL);
        c->w = new_w;
        c->h = new_h;
        d->modified = true;
    }
}

const char *designer_get_prop(designer_t *d, int ctrl_id, const char *prop_name) {
    designer_control_t *c = find_ctrl(d, ctrl_id);
    if (!c) return NULL;
    /* Built-in properties */
    static char buf[256];
    if (strcmp(prop_name, "Name") == 0) return c->name;
    if (strcmp(prop_name, "Text") == 0) return c->text;
    if (strcmp(prop_name, "Location.X") == 0) { snprintf(buf, 256, "%d", c->x); return buf; }
    if (strcmp(prop_name, "Location.Y") == 0) { snprintf(buf, 256, "%d", c->y); return buf; }
    if (strcmp(prop_name, "Size.Width") == 0) { snprintf(buf, 256, "%d", c->w); return buf; }
    if (strcmp(prop_name, "Size.Height") == 0) { snprintf(buf, 256, "%d", c->h); return buf; }
    if (strcmp(prop_name, "Visible") == 0) return c->visible ? "true" : "false";
    if (strcmp(prop_name, "Enabled") == 0) return c->enabled ? "true" : "false";
    if (strcmp(prop_name, "TabIndex") == 0) { snprintf(buf, 256, "%d", c->tab_index); return buf; }
    /* Custom properties */
    for (int i = 0; i < c->prop_count; i++) {
        if (strcmp(c->props[i].name, prop_name) == 0) return c->props[i].value;
    }
    return NULL;
}

void designer_set_prop(designer_t *d, int ctrl_id, const char *prop_name,
                       const char *value) {
    designer_control_t *c = find_ctrl(d, ctrl_id);
    if (!c) return;

    const char *old_val = designer_get_prop(d, ctrl_id, prop_name);
    push_undo(d, UNDO_CHANGE_PROPERTY, ctrl_id, 0, 0, 0, 0,
              prop_name, old_val ? old_val : "");

    /* Built-in properties */
    if (strcmp(prop_name, "Name") == 0) { strncpy(c->name, value, sizeof(c->name) - 1); }
    else if (strcmp(prop_name, "Text") == 0) { strncpy(c->text, value, sizeof(c->text) - 1); }
    else if (strcmp(prop_name, "Location.X") == 0) { c->x = atoi(value); }
    else if (strcmp(prop_name, "Location.Y") == 0) { c->y = atoi(value); }
    else if (strcmp(prop_name, "Size.Width") == 0) { c->w = atoi(value); }
    else if (strcmp(prop_name, "Size.Height") == 0) { c->h = atoi(value); }
    else if (strcmp(prop_name, "Visible") == 0) { c->visible = strcmp(value, "true") == 0; }
    else if (strcmp(prop_name, "Enabled") == 0) { c->enabled = strcmp(value, "true") == 0; }
    else if (strcmp(prop_name, "TabIndex") == 0) { c->tab_index = atoi(value); }
    else {
        /* Custom property */
        for (int i = 0; i < c->prop_count; i++) {
            if (strcmp(c->props[i].name, prop_name) == 0) {
                strncpy(c->props[i].value, value, PROP_MAX_VALUE - 1);
                d->modified = true;
                return;
            }
        }
        if (c->prop_count < DESIGNER_MAX_PROPS) {
            designer_prop_t *p = &c->props[c->prop_count++];
            strncpy(p->name, prop_name, sizeof(p->name) - 1);
            strncpy(p->value, value, sizeof(p->value) - 1);
            strncpy(p->category, "Misc", sizeof(p->category) - 1);
            p->type = PROP_STRING;
        }
    }
    d->modified = true;
}

void designer_add_event(designer_t *d, int ctrl_id,
                        const char *event_name, const char *handler_name) {
    designer_control_t *c = find_ctrl(d, ctrl_id);
    if (!c || c->event_count >= DESIGNER_MAX_EVENTS) return;
    event_binding_t *e = &c->events[c->event_count++];
    strncpy(e->event_name, event_name, sizeof(e->event_name) - 1);
    if (handler_name && handler_name[0]) {
        strncpy(e->handler_name, handler_name, sizeof(e->handler_name) - 1);
    } else {
        snprintf(e->handler_name, sizeof(e->handler_name), "%s_%s", c->name, event_name);
    }
    d->modified = true;
}

void designer_undo(designer_t *d) {
    if (d->undo_pos <= 0) return;
    d->undo_pos--;
    undo_entry_t *u = &d->undo_stack[d->undo_pos];
    designer_control_t *c;

    switch (u->type) {
    case UNDO_ADD_CONTROL:
        /* Remove the added control */
        for (int i = 0; i < d->control_count; i++) {
            if (d->controls[i].id == u->ctrl_id) {
                u->saved_ctrl = d->controls[i];
                memmove(&d->controls[i], &d->controls[i + 1],
                        sizeof(designer_control_t) * (size_t)(d->control_count - i - 1));
                d->control_count--;
                break;
            }
        }
        break;
    case UNDO_DELETE_CONTROL:
        /* Re-add the deleted control */
        if (d->control_count < DESIGNER_MAX_CONTROLS) {
            d->controls[d->control_count++] = u->saved_ctrl;
        }
        break;
    case UNDO_MOVE_CONTROL:
    case UNDO_RESIZE_CONTROL:
        c = find_ctrl(d, u->ctrl_id);
        if (c) {
            int nx = c->x, ny = c->y, nw = c->w, nh = c->h;
            c->x = u->old_x; c->y = u->old_y;
            c->w = u->old_w; c->h = u->old_h;
            u->old_x = nx; u->old_y = ny;
            u->old_w = nw; u->old_h = nh;
        }
        break;
    case UNDO_CHANGE_PROPERTY:
        c = find_ctrl(d, u->ctrl_id);
        if (c) {
            char cur_val[PROP_MAX_VALUE] = "";
            const char *v = designer_get_prop(d, u->ctrl_id, u->prop_name);
            if (v) strncpy(cur_val, v, PROP_MAX_VALUE - 1);
            designer_set_prop(d, u->ctrl_id, u->prop_name, u->old_value);
            strncpy(u->old_value, cur_val, PROP_MAX_VALUE - 1);
            d->undo_pos--; /* set_prop pushed an undo, compensate */
        }
        break;
    default:
        break;
    }
    d->modified = true;
}

void designer_redo(designer_t *d) {
    if (d->undo_pos >= d->undo_count) return;
    undo_entry_t *u = &d->undo_stack[d->undo_pos];
    d->undo_pos++;

    switch (u->type) {
    case UNDO_ADD_CONTROL:
        if (d->control_count < DESIGNER_MAX_CONTROLS) {
            d->controls[d->control_count++] = u->saved_ctrl;
        }
        break;
    case UNDO_DELETE_CONTROL:
        for (int i = 0; i < d->control_count; i++) {
            if (d->controls[i].id == u->ctrl_id) {
                memmove(&d->controls[i], &d->controls[i + 1],
                        sizeof(designer_control_t) * (size_t)(d->control_count - i - 1));
                d->control_count--;
                break;
            }
        }
        break;
    case UNDO_MOVE_CONTROL:
    case UNDO_RESIZE_CONTROL: {
        designer_control_t *c = find_ctrl(d, u->ctrl_id);
        if (c) {
            int nx = c->x, ny = c->y, nw = c->w, nh = c->h;
            c->x = u->old_x; c->y = u->old_y;
            c->w = u->old_w; c->h = u->old_h;
            u->old_x = nx; u->old_y = ny;
            u->old_w = nw; u->old_h = nh;
        }
        break;
    }
    default:
        break;
    }
    d->modified = true;
}

/* ---- Code Generation ---- */

static void emit_control_code(designer_control_t *c, char *buf, size_t cap, size_t *pos) {
    const char *type_name = designer_ctrl_type_name(c->type);
    size_t p = *pos;

#define EMIT(...) do { p += (size_t)snprintf(buf + p, cap - p, __VA_ARGS__); } while(0)

    EMIT("        // %s\n", c->name);
    EMIT("        this.%s = new %s();\n", c->name, type_name);
    EMIT("        this.%s.Name = \"%s\";\n", c->name, c->name);
    if (c->text[0] && strcmp(c->text, c->name) != 0)
        EMIT("        this.%s.Text = \"%s\";\n", c->name, c->text);
    EMIT("        this.%s.Location = new Point(%d, %d);\n", c->name, c->x, c->y);
    EMIT("        this.%s.Size = new Size(%d, %d);\n", c->name, c->w, c->h);
    if (c->tab_index > 0)
        EMIT("        this.%s.TabIndex = %d;\n", c->name, c->tab_index);
    if (!c->visible)
        EMIT("        this.%s.Visible = false;\n", c->name);
    if (!c->enabled)
        EMIT("        this.%s.Enabled = false;\n", c->name);
    if (c->anchor != (ANCHOR_TOP | ANCHOR_LEFT)) {
        EMIT("        this.%s.Anchor = ", c->name);
        bool first = true;
        if (c->anchor & ANCHOR_TOP)    { EMIT("%sAnchor.Top", first ? "" : " | "); first = false; }
        if (c->anchor & ANCHOR_BOTTOM) { EMIT("%sAnchor.Bottom", first ? "" : " | "); first = false; }
        if (c->anchor & ANCHOR_LEFT)   { EMIT("%sAnchor.Left", first ? "" : " | "); first = false; }
        if (c->anchor & ANCHOR_RIGHT)  { EMIT("%sAnchor.Right", first ? "" : " | "); first = false; }
        EMIT(";\n");
    }
    if (c->dock != DOCK_NONE) {
        const char *dock_names[] = {"None","Top","Bottom","Left","Right","Fill"};
        EMIT("        this.%s.Dock = DockStyle.%s;\n", c->name, dock_names[c->dock]);
    }
    /* Custom properties */
    for (int i = 0; i < c->prop_count; i++) {
        if (!c->props[i].is_default) {
            EMIT("        this.%s.%s = %s;\n", c->name, c->props[i].name, c->props[i].value);
        }
    }
    /* Event bindings */
    for (int i = 0; i < c->event_count; i++) {
        EMIT("        this.%s.%s += this.%s;\n",
             c->name, c->events[i].event_name, c->events[i].handler_name);
    }
    EMIT("\n");

#undef EMIT
    *pos = p;
}

char *designer_generate_code(designer_t *d, size_t *out_len) {
    /* Estimate buffer size */
    size_t cap = 8192 + (size_t)d->control_count * 512;
    char *buf = (char *)malloc(cap);
    if (!buf) return NULL;
    size_t pos = 0;

#define EMIT(...) do { pos += (size_t)snprintf(buf + pos, cap - pos, __VA_ARGS__); } while(0)

    /* File header */
    if (d->form.namespace_name[0])
        EMIT("namespace %s;\n\n", d->form.namespace_name);

    EMIT("using System;\n");
    EMIT("using System.Drawing;\n");
    EMIT("using System.Windows;\n");
    EMIT("using System.Windows.Controls;\n\n");

    /* Class declaration */
    EMIT("partial class %s : Form {\n", d->form.class_name);

    /* Control field declarations */
    for (int i = 0; i < d->control_count; i++) {
        EMIT("    private %s %s;\n",
             designer_ctrl_type_name(d->controls[i].type),
             d->controls[i].name);
    }
    EMIT("\n");

    /* InitializeComponent method */
    EMIT("    private void InitializeComponent() {\n");

    /* Emit control setup code */
    for (int i = 0; i < d->control_count; i++) {
        emit_control_code(&d->controls[i], buf, cap, &pos);
    }

    /* Form properties */
    EMIT("        // Form\n");
    EMIT("        this.Text = \"%s\";\n", d->form.title);
    EMIT("        this.ClientSize = new Size(%d, %d);\n", d->form.width, d->form.height);
    if (d->form.start_position[0])
        EMIT("        this.StartPosition = FormStartPosition.%s;\n", d->form.start_position);
    if (!d->form.maximize_box)
        EMIT("        this.MaximizeBox = false;\n");
    if (!d->form.minimize_box)
        EMIT("        this.MinimizeBox = false;\n");
    if (!d->form.resizable)
        EMIT("        this.FormBorderStyle = FormBorderStyle.FixedSingle;\n");

    /* Add controls to form */
    EMIT("\n        // Add controls\n");
    for (int i = 0; i < d->control_count; i++) {
        if (d->controls[i].parent_id < 0) {
            EMIT("        this.Controls.Add(this.%s);\n", d->controls[i].name);
        }
    }
    /* Add controls to containers */
    for (int i = 0; i < d->control_count; i++) {
        if (d->controls[i].parent_id >= 0) {
            designer_control_t *parent = find_ctrl(d, d->controls[i].parent_id);
            if (parent) {
                EMIT("        this.%s.Controls.Add(this.%s);\n",
                     parent->name, d->controls[i].name);
            }
        }
    }

    EMIT("    }\n");
    EMIT("}\n");

#undef EMIT

    if (out_len) *out_len = pos;
    return buf;
}

char *designer_generate_event_stubs(designer_t *d, size_t *out_len) {
    size_t cap = 4096 + (size_t)d->control_count * 256;
    char *buf = (char *)malloc(cap);
    if (!buf) return NULL;
    size_t pos = 0;

#define EMIT(...) do { pos += (size_t)snprintf(buf + pos, cap - pos, __VA_ARGS__); } while(0)

    EMIT("    // Event handlers\n\n");
    for (int i = 0; i < d->control_count; i++) {
        designer_control_t *c = &d->controls[i];
        for (int j = 0; j < c->event_count; j++) {
            EMIT("    private void %s(object sender, EventArgs e) {\n",
                 c->events[j].handler_name);
            EMIT("        // TODO: implement %s.%s handler\n",
                 c->name, c->events[j].event_name);
            EMIT("    }\n\n");
        }
    }

#undef EMIT

    if (out_len) *out_len = pos;
    return buf;
}

/* ---- Save / Load ---- */

bool designer_save(designer_t *d, const char *filepath) {
    FILE *f = fopen(filepath, "w");
    if (!f) return false;

    fprintf(f, "# Zan Form Designer File\n");
    fprintf(f, "# DO NOT EDIT - auto-generated by Zan IDE Designer\n\n");
    fprintf(f, "[Form]\n");
    fprintf(f, "ClassName=%s\n", d->form.class_name);
    fprintf(f, "Namespace=%s\n", d->form.namespace_name);
    fprintf(f, "Title=%s\n", d->form.title);
    fprintf(f, "Width=%d\n", d->form.width);
    fprintf(f, "Height=%d\n", d->form.height);
    fprintf(f, "MaximizeBox=%s\n", d->form.maximize_box ? "true" : "false");
    fprintf(f, "MinimizeBox=%s\n", d->form.minimize_box ? "true" : "false");
    fprintf(f, "Resizable=%s\n", d->form.resizable ? "true" : "false");
    fprintf(f, "StartPosition=%s\n", d->form.start_position);
    fprintf(f, "BackColor=%06X\n\n", d->form.background_color);

    for (int i = 0; i < d->control_count; i++) {
        designer_control_t *c = &d->controls[i];
        fprintf(f, "[Control]\n");
        fprintf(f, "Id=%d\n", c->id);
        fprintf(f, "Type=%d\n", (int)c->type);
        fprintf(f, "Name=%s\n", c->name);
        fprintf(f, "Text=%s\n", c->text);
        fprintf(f, "X=%d\n", c->x);
        fprintf(f, "Y=%d\n", c->y);
        fprintf(f, "W=%d\n", c->w);
        fprintf(f, "H=%d\n", c->h);
        fprintf(f, "Anchor=%d\n", c->anchor);
        fprintf(f, "Dock=%d\n", (int)c->dock);
        fprintf(f, "TabIndex=%d\n", c->tab_index);
        fprintf(f, "Visible=%s\n", c->visible ? "true" : "false");
        fprintf(f, "Enabled=%s\n", c->enabled ? "true" : "false");
        fprintf(f, "ParentId=%d\n", c->parent_id);
        /* Custom properties */
        for (int j = 0; j < c->prop_count; j++) {
            fprintf(f, "Prop.%s=%s\n", c->props[j].name, c->props[j].value);
        }
        /* Events */
        for (int j = 0; j < c->event_count; j++) {
            fprintf(f, "Event.%s=%s\n", c->events[j].event_name, c->events[j].handler_name);
        }
        fprintf(f, "\n");
    }

    fclose(f);
    d->modified = false;
    return true;
}

bool designer_load(designer_t *d, const char *filepath) {
    FILE *f = fopen(filepath, "r");
    if (!f) return false;

    designer_init(d);
    d->active = true;

    char line[512];
    designer_control_t *cur_ctrl = NULL;
    bool in_form = false;

    while (fgets(line, sizeof(line), f)) {
        /* Trim newline */
        int len = (int)strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) line[--len] = '\0';
        if (len == 0 || line[0] == '#') continue;

        if (strcmp(line, "[Form]") == 0) { in_form = true; cur_ctrl = NULL; continue; }
        if (strcmp(line, "[Control]") == 0) {
            in_form = false;
            if (d->control_count < DESIGNER_MAX_CONTROLS) {
                cur_ctrl = &d->controls[d->control_count];
                memset(cur_ctrl, 0, sizeof(*cur_ctrl));
                cur_ctrl->visible = true;
                cur_ctrl->enabled = true;
                cur_ctrl->parent_id = -1;
                cur_ctrl->anchor = ANCHOR_TOP | ANCHOR_LEFT;
                d->control_count++;
            }
            continue;
        }

        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        char *key = line;
        char *val = eq + 1;

        if (in_form) {
            if (strcmp(key, "ClassName") == 0) strncpy(d->form.class_name, val, 63);
            else if (strcmp(key, "Namespace") == 0) strncpy(d->form.namespace_name, val, 127);
            else if (strcmp(key, "Title") == 0) strncpy(d->form.title, val, 127);
            else if (strcmp(key, "Width") == 0) d->form.width = atoi(val);
            else if (strcmp(key, "Height") == 0) d->form.height = atoi(val);
            else if (strcmp(key, "MaximizeBox") == 0) d->form.maximize_box = strcmp(val, "true") == 0;
            else if (strcmp(key, "MinimizeBox") == 0) d->form.minimize_box = strcmp(val, "true") == 0;
            else if (strcmp(key, "Resizable") == 0) d->form.resizable = strcmp(val, "true") == 0;
            else if (strcmp(key, "StartPosition") == 0) strncpy(d->form.start_position, val, 31);
            else if (strcmp(key, "BackColor") == 0) d->form.background_color = (int)strtol(val, NULL, 16);
        } else if (cur_ctrl) {
            if (strcmp(key, "Id") == 0) { cur_ctrl->id = atoi(val); if (cur_ctrl->id >= d->next_id) d->next_id = cur_ctrl->id + 1; }
            else if (strcmp(key, "Type") == 0) cur_ctrl->type = (designer_ctrl_type_t)atoi(val);
            else if (strcmp(key, "Name") == 0) strncpy(cur_ctrl->name, val, 63);
            else if (strcmp(key, "Text") == 0) strncpy(cur_ctrl->text, val, 127);
            else if (strcmp(key, "X") == 0) cur_ctrl->x = atoi(val);
            else if (strcmp(key, "Y") == 0) cur_ctrl->y = atoi(val);
            else if (strcmp(key, "W") == 0) cur_ctrl->w = atoi(val);
            else if (strcmp(key, "H") == 0) cur_ctrl->h = atoi(val);
            else if (strcmp(key, "Anchor") == 0) cur_ctrl->anchor = atoi(val);
            else if (strcmp(key, "Dock") == 0) cur_ctrl->dock = (dock_mode_t)atoi(val);
            else if (strcmp(key, "TabIndex") == 0) cur_ctrl->tab_index = atoi(val);
            else if (strcmp(key, "Visible") == 0) cur_ctrl->visible = strcmp(val, "true") == 0;
            else if (strcmp(key, "Enabled") == 0) cur_ctrl->enabled = strcmp(val, "true") == 0;
            else if (strcmp(key, "ParentId") == 0) cur_ctrl->parent_id = atoi(val);
            else if (strncmp(key, "Prop.", 5) == 0 && cur_ctrl->prop_count < DESIGNER_MAX_PROPS) {
                designer_prop_t *p = &cur_ctrl->props[cur_ctrl->prop_count++];
                strncpy(p->name, key + 5, sizeof(p->name) - 1);
                strncpy(p->value, val, sizeof(p->value) - 1);
                p->type = PROP_STRING;
            }
            else if (strncmp(key, "Event.", 6) == 0 && cur_ctrl->event_count < DESIGNER_MAX_EVENTS) {
                event_binding_t *e = &cur_ctrl->events[cur_ctrl->event_count++];
                strncpy(e->event_name, key + 6, sizeof(e->event_name) - 1);
                strncpy(e->handler_name, val, sizeof(e->handler_name) - 1);
            }
        }
    }

    fclose(f);
    d->modified = false;
    return true;
}

/* ---- Alignment helpers ---- */

void designer_align_left(designer_t *d) {
    if (d->selected_id < 0) return;
    designer_control_t *ref = find_ctrl(d, d->selected_id);
    if (!ref) return;
    for (int i = 0; i < d->control_count; i++) {
        if (d->controls[i].selected && d->controls[i].id != d->selected_id) {
            designer_move_control(d, d->controls[i].id, ref->x, d->controls[i].y);
        }
    }
}

void designer_align_top(designer_t *d) {
    if (d->selected_id < 0) return;
    designer_control_t *ref = find_ctrl(d, d->selected_id);
    if (!ref) return;
    for (int i = 0; i < d->control_count; i++) {
        if (d->controls[i].selected && d->controls[i].id != d->selected_id) {
            designer_move_control(d, d->controls[i].id, d->controls[i].x, ref->y);
        }
    }
}

void designer_align_right(designer_t *d) {
    if (d->selected_id < 0) return;
    designer_control_t *ref = find_ctrl(d, d->selected_id);
    if (!ref) return;
    int ref_right = ref->x + ref->w;
    for (int i = 0; i < d->control_count; i++) {
        if (d->controls[i].selected && d->controls[i].id != d->selected_id) {
            designer_move_control(d, d->controls[i].id,
                                 ref_right - d->controls[i].w, d->controls[i].y);
        }
    }
}

void designer_align_bottom(designer_t *d) {
    if (d->selected_id < 0) return;
    designer_control_t *ref = find_ctrl(d, d->selected_id);
    if (!ref) return;
    int ref_bottom = ref->y + ref->h;
    for (int i = 0; i < d->control_count; i++) {
        if (d->controls[i].selected && d->controls[i].id != d->selected_id) {
            designer_move_control(d, d->controls[i].id,
                                 d->controls[i].x, ref_bottom - d->controls[i].h);
        }
    }
}

void designer_center_horizontal(designer_t *d) {
    for (int i = 0; i < d->control_count; i++) {
        if (d->controls[i].selected) {
            int center = (d->form.width - d->controls[i].w) / 2;
            designer_move_control(d, d->controls[i].id, center, d->controls[i].y);
        }
    }
}

void designer_center_vertical(designer_t *d) {
    for (int i = 0; i < d->control_count; i++) {
        if (d->controls[i].selected) {
            int center = (d->form.height - d->controls[i].h) / 2;
            designer_move_control(d, d->controls[i].id, d->controls[i].x, center);
        }
    }
}

void designer_same_width(designer_t *d) {
    if (d->selected_id < 0) return;
    designer_control_t *ref = find_ctrl(d, d->selected_id);
    if (!ref) return;
    for (int i = 0; i < d->control_count; i++) {
        if (d->controls[i].selected && d->controls[i].id != d->selected_id) {
            designer_resize_control(d, d->controls[i].id, ref->w, d->controls[i].h);
        }
    }
}

void designer_same_height(designer_t *d) {
    if (d->selected_id < 0) return;
    designer_control_t *ref = find_ctrl(d, d->selected_id);
    if (!ref) return;
    for (int i = 0; i < d->control_count; i++) {
        if (d->controls[i].selected && d->controls[i].id != d->selected_id) {
            designer_resize_control(d, d->controls[i].id, d->controls[i].w, ref->h);
        }
    }
}

void designer_auto_tab_order(designer_t *d) {
    /* Assign tab order top-to-bottom, left-to-right */
    /* Simple bubble sort by (y, x) */
    int indices[DESIGNER_MAX_CONTROLS];
    for (int i = 0; i < d->control_count; i++) indices[i] = i;

    for (int i = 0; i < d->control_count - 1; i++) {
        for (int j = i + 1; j < d->control_count; j++) {
            designer_control_t *a = &d->controls[indices[i]];
            designer_control_t *b = &d->controls[indices[j]];
            if (a->y > b->y || (a->y == b->y && a->x > b->x)) {
                int tmp = indices[i]; indices[i] = indices[j]; indices[j] = tmp;
            }
        }
    }
    for (int i = 0; i < d->control_count; i++) {
        d->controls[indices[i]].tab_index = i;
    }
    d->modified = true;
}

/* ---- Win32 Rendering ---- */

#ifdef _WIN32

void designer_paint(designer_t *d, void *hdc_void, int offset_x, int offset_y,
                    int view_w, int view_h) {
    HDC hdc = (HDC)hdc_void;

    /* Draw form background */
    RECT form_rect = { offset_x, offset_y,
                       offset_x + (int)(d->form.width * d->zoom),
                       offset_y + (int)(d->form.height * d->zoom) };
    HBRUSH bg_brush = CreateSolidBrush(d->form.background_color);
    FillRect(hdc, &form_rect, bg_brush);
    DeleteObject(bg_brush);

    /* Draw grid */
    if (d->show_grid) {
        int gs = (int)(d->grid_size * d->zoom);
        if (gs >= 4) {
            for (int gx = offset_x + gs; gx < form_rect.right; gx += gs) {
                for (int gy = offset_y + gs; gy < form_rect.bottom; gy += gs) {
                    SetPixel(hdc, gx, gy, RGB(192, 192, 192));
                }
            }
        }
    }

    /* Draw form border */
    HPEN border_pen = CreatePen(PS_SOLID, 2, RGB(100, 100, 100));
    HPEN old_pen = (HPEN)SelectObject(hdc, border_pen);
    HBRUSH null_brush = (HBRUSH)GetStockObject(NULL_BRUSH);
    HBRUSH old_brush = (HBRUSH)SelectObject(hdc, null_brush);
    Rectangle(hdc, form_rect.left, form_rect.top, form_rect.right, form_rect.bottom);
    SelectObject(hdc, old_pen);
    SelectObject(hdc, old_brush);
    DeleteObject(border_pen);

    /* Draw controls */
    for (int i = 0; i < d->control_count; i++) {
        designer_control_t *c = &d->controls[i];
        int cx = offset_x + (int)(c->x * d->zoom);
        int cy = offset_y + (int)(c->y * d->zoom);
        int cw = (int)(c->w * d->zoom);
        int ch = (int)(c->h * d->zoom);

        RECT cr = { cx, cy, cx + cw, cy + ch };

        /* Control appearance based on type */
        COLORREF fill_color = RGB(255, 255, 255);
        COLORREF border_color = RGB(128, 128, 128);

        switch (c->type) {
        case DCTL_BUTTON:
            fill_color = RGB(225, 225, 225);
            border_color = RGB(100, 100, 100);
            break;
        case DCTL_PANEL:
        case DCTL_GROUPBOX:
            fill_color = RGB(240, 240, 240);
            border_color = RGB(160, 160, 160);
            break;
        case DCTL_LABEL:
            fill_color = d->form.background_color;
            border_color = RGB(200, 200, 200);
            break;
        case DCTL_PROGRESSBAR:
            fill_color = RGB(200, 230, 200);
            break;
        default:
            break;
        }

        HBRUSH ctrl_brush = CreateSolidBrush(fill_color);
        HPEN ctrl_pen = CreatePen(PS_SOLID, 1, border_color);
        SelectObject(hdc, ctrl_brush);
        SelectObject(hdc, ctrl_pen);
        Rectangle(hdc, cr.left, cr.top, cr.right, cr.bottom);
        DeleteObject(ctrl_brush);
        DeleteObject(ctrl_pen);

        /* Draw control text */
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(0, 0, 0));
        wchar_t wtext[128];
        MultiByteToWideChar(CP_UTF8, 0, c->text, -1, wtext, 128);
        DrawTextW(hdc, wtext, -1, &cr,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        /* Draw type indicator (small text top-left) */
        SetTextColor(hdc, RGB(100, 100, 200));
        wchar_t wtype[64];
        MultiByteToWideChar(CP_UTF8, 0, designer_ctrl_type_name(c->type), -1, wtype, 64);
        RECT type_rect = { cx + 2, cy + 1, cx + cw - 2, cy + 12 };
        DrawTextW(hdc, wtype, -1, &type_rect, DT_LEFT | DT_TOP | DT_NOPREFIX);

        /* Selection handles */
        if (c->id == d->selected_id) {
            HPEN sel_pen = CreatePen(PS_DASH, 1, RGB(0, 120, 215));
            SelectObject(hdc, sel_pen);
            SelectObject(hdc, (HBRUSH)GetStockObject(NULL_BRUSH));
            Rectangle(hdc, cr.left - 1, cr.top - 1, cr.right + 1, cr.bottom + 1);
            DeleteObject(sel_pen);

            /* Draw 8 resize handles */
            HBRUSH handle_brush = CreateSolidBrush(RGB(0, 120, 215));
            int hs = 6;
            int hx[] = { cr.left, (cr.left+cr.right)/2, cr.right,
                        cr.left, cr.right,
                        cr.left, (cr.left+cr.right)/2, cr.right };
            int hy[] = { cr.top, cr.top, cr.top,
                        (cr.top+cr.bottom)/2, (cr.top+cr.bottom)/2,
                        cr.bottom, cr.bottom, cr.bottom };
            for (int h = 0; h < 8; h++) {
                RECT hr = { hx[h] - hs/2, hy[h] - hs/2, hx[h] + hs/2, hy[h] + hs/2 };
                FillRect(hdc, &hr, handle_brush);
            }
            DeleteObject(handle_brush);
        }
    }
}

void designer_paint_toolbox(designer_t *d, void *hdc_void, int x, int y, int w, int h) {
    HDC hdc = (HDC)hdc_void;
    int item_h = 22;
    int ty = y + 4;

    /* Title */
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(0, 0, 0));
    HFONT bold_font = CreateFontW(14, 0, 0, 0, FW_BOLD, 0, 0, 0,
                                   DEFAULT_CHARSET, 0, 0, 0, 0, L"Segoe UI");
    HFONT old_font = (HFONT)SelectObject(hdc, bold_font);
    RECT title_rect = { x + 4, ty, x + w - 4, ty + 18 };
    DrawTextW(hdc, L"Toolbox", -1, &title_rect, DT_LEFT | DT_VCENTER);
    SelectObject(hdc, old_font);
    DeleteObject(bold_font);
    ty += 22;

    /* Draw separator */
    HPEN sep_pen = CreatePen(PS_SOLID, 1, RGB(200, 200, 200));
    HPEN old_pen2 = (HPEN)SelectObject(hdc, sep_pen);
    MoveToEx(hdc, x + 4, ty, NULL);
    LineTo(hdc, x + w - 4, ty);
    SelectObject(hdc, old_pen2);
    DeleteObject(sep_pen);
    ty += 4;

    /* List control types */
    for (int i = 0; i < DCTL_COUNT && ty + item_h < y + h; i++) {
        RECT item_rect = { x + 2, ty, x + w - 2, ty + item_h };

        if ((int)d->tool_selected == i) {
            HBRUSH sel_brush = CreateSolidBrush(RGB(205, 232, 255));
            FillRect(hdc, &item_rect, sel_brush);
            DeleteObject(sel_brush);
        }

        SetTextColor(hdc, RGB(30, 30, 30));
        item_rect.left += 8;
        wchar_t wname[64];
        MultiByteToWideChar(CP_UTF8, 0, ctrl_meta[i].name, -1, wname, 64);
        DrawTextW(hdc, wname, -1, &item_rect,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        ty += item_h;
    }
}

void designer_paint_properties(designer_t *d, void *hdc_void, int x, int y, int w, int h) {
    HDC hdc = (HDC)hdc_void;
    int row_h = 20;
    int py = y + 4;

    SetBkMode(hdc, TRANSPARENT);

    /* Title */
    HFONT bold_font = CreateFontW(14, 0, 0, 0, FW_BOLD, 0, 0, 0,
                                   DEFAULT_CHARSET, 0, 0, 0, 0, L"Segoe UI");
    HFONT old_font = (HFONT)SelectObject(hdc, bold_font);
    SetTextColor(hdc, RGB(0, 0, 0));
    RECT title_rect = { x + 4, py, x + w - 4, py + 18 };
    DrawTextW(hdc, L"Properties", -1, &title_rect, DT_LEFT | DT_VCENTER);
    SelectObject(hdc, old_font);
    DeleteObject(bold_font);
    py += 24;

    if (d->selected_id < 0) {
        SetTextColor(hdc, RGB(128, 128, 128));
        RECT msg_rect = { x + 8, py, x + w - 8, py + 20 };
        DrawTextW(hdc, L"No control selected", -1, &msg_rect, DT_LEFT);
        return;
    }

    designer_control_t *c = find_ctrl(d, d->selected_id);
    if (!c) return;

    /* Built-in properties */
    struct { const char *name; const char *val; } props[] = {
        {"(Name)", c->name},
        {"Type", designer_ctrl_type_name(c->type)},
        {"Text", c->text},
    };
    char loc_buf[32], size_buf[32];
    snprintf(loc_buf, 32, "%d, %d", c->x, c->y);
    snprintf(size_buf, 32, "%d, %d", c->w, c->h);

    int name_w = w / 2;

    /* Property rows */
    const char *builtin_names[] = {"(Name)", "Type", "Text", "Location", "Size",
                                    "Visible", "Enabled", "TabIndex", "Anchor", "Dock"};
    const char *builtin_vals[10];
    builtin_vals[0] = c->name;
    builtin_vals[1] = designer_ctrl_type_name(c->type);
    builtin_vals[2] = c->text;
    builtin_vals[3] = loc_buf;
    builtin_vals[4] = size_buf;
    builtin_vals[5] = c->visible ? "true" : "false";
    builtin_vals[6] = c->enabled ? "true" : "false";
    static char tab_buf[16];
    snprintf(tab_buf, 16, "%d", c->tab_index);
    builtin_vals[7] = tab_buf;
    static char anch_buf[32];
    snprintf(anch_buf, 32, "%d", c->anchor);
    builtin_vals[8] = anch_buf;
    static const char *dock_names[] = {"None","Top","Bottom","Left","Right","Fill"};
    builtin_vals[9] = dock_names[c->dock];

    for (int i = 0; i < 10 && py + row_h < y + h; i++) {
        /* Alternating row color */
        if (i % 2 == 0) {
            RECT row_rect = { x, py, x + w, py + row_h };
            HBRUSH alt_brush = CreateSolidBrush(RGB(248, 248, 248));
            FillRect(hdc, &row_rect, alt_brush);
            DeleteObject(alt_brush);
        }

        SetTextColor(hdc, RGB(60, 60, 60));
        RECT name_rect = { x + 4, py, x + name_w, py + row_h };
        wchar_t wn[64];
        MultiByteToWideChar(CP_UTF8, 0, builtin_names[i], -1, wn, 64);
        DrawTextW(hdc, wn, -1, &name_rect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        SetTextColor(hdc, RGB(0, 0, 0));
        RECT val_rect = { x + name_w + 4, py, x + w - 4, py + row_h };
        wchar_t wv[128];
        MultiByteToWideChar(CP_UTF8, 0, builtin_vals[i], -1, wv, 128);
        DrawTextW(hdc, wv, -1, &val_rect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        py += row_h;
    }

    /* Custom properties */
    for (int i = 0; i < c->prop_count && py + row_h < y + h; i++) {
        SetTextColor(hdc, RGB(60, 60, 60));
        RECT name_rect = { x + 4, py, x + name_w, py + row_h };
        wchar_t wn[64];
        MultiByteToWideChar(CP_UTF8, 0, c->props[i].name, -1, wn, 64);
        DrawTextW(hdc, wn, -1, &name_rect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        SetTextColor(hdc, RGB(0, 0, 0));
        RECT val_rect = { x + name_w + 4, py, x + w - 4, py + row_h };
        wchar_t wv[128];
        MultiByteToWideChar(CP_UTF8, 0, c->props[i].value, -1, wv, 128);
        DrawTextW(hdc, wv, -1, &val_rect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        py += row_h;
    }

    /* Events section */
    if (c->event_count > 0 && py + row_h < y + h) {
        py += 8;
        HFONT bold2 = CreateFontW(12, 0, 0, 0, FW_BOLD, 0, 0, 0,
                                    DEFAULT_CHARSET, 0, 0, 0, 0, L"Segoe UI");
        SelectObject(hdc, bold2);
        SetTextColor(hdc, RGB(0, 0, 0));
        RECT ev_title = { x + 4, py, x + w, py + row_h };
        DrawTextW(hdc, L"Events", -1, &ev_title, DT_LEFT | DT_VCENTER);
        SelectObject(hdc, old_font);
        DeleteObject(bold2);
        py += row_h;

        for (int i = 0; i < c->event_count && py + row_h < y + h; i++) {
            SetTextColor(hdc, RGB(60, 60, 60));
            RECT en_rect = { x + 4, py, x + name_w, py + row_h };
            wchar_t wen[64];
            MultiByteToWideChar(CP_UTF8, 0, c->events[i].event_name, -1, wen, 64);
            DrawTextW(hdc, wen, -1, &en_rect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

            SetTextColor(hdc, RGB(0, 0, 180));
            RECT eh_rect = { x + name_w + 4, py, x + w - 4, py + row_h };
            wchar_t weh[128];
            MultiByteToWideChar(CP_UTF8, 0, c->events[i].handler_name, -1, weh, 128);
            DrawTextW(hdc, weh, -1, &eh_rect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

            py += row_h;
        }
    }
}

#endif /* _WIN32 */
