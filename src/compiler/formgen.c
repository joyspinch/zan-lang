/* formgen.c -- .zform design-document translation.
 *
 * Translates the visual designer's .zform JSON (see stdlib/Gui/Designer and
 * src/ide_zan/ZanIDE.CodeNav.zan GenFormFile for the format and the widget
 * mapping it mirrors) into synthetic Zan source:
 *
 *     partial class <Name> {
 *         static <Widget> <field>;            // typed fields (designer names)
 *         static Control __BuildForm() {...}  // real control tree
 *         static void __WireForm(Form form){...} // on<Event> -> form.Handle()
 *         static void Main() {...}            // window + OnLoad + Wire + Run
 *     }
 *
 * The synthetic class is a compile-time projection of the design document:
 * nothing is written to disk, so the .zform stays the single source of truth.
 * Event bindings are attached by name through Form.Handle() (the same
 * name -> Action contract as UiDoc/HandlerRegistry): the JSON stores handler
 * names, the business file registers Actions with form.On("name", fn), and
 * __WireForm subscribes each control's event to the matching Action. Handlers
 * that are never registered simply never fire -- unregistered names are safe.
 *
 * The widget mapping mirrors the IDE's generator so a design produced by the
 * designer and one compiled directly produce identical trees.
 */
#include "formgen.h"
#include "stdlib_ext.h"
#include "diag.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ---- growable buffer ---- */

typedef struct {
    char *buf;
    size_t len;
    size_t cap;
} fg_buf_t;

static void fg_putf(fg_buf_t *b, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int need = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (need < 0) return;
    if (b->len + (size_t)need + 1 > b->cap) {
        size_t nc = b->cap ? b->cap * 2 : 512;
        while (nc < b->len + (size_t)need + 1) nc *= 2;
        char *nb = (char *)realloc(b->buf, nc);
        if (!nb) return;
        b->buf = nb;
        b->cap = nc;
    }
    va_start(ap, fmt);
    vsnprintf(b->buf + b->len, b->cap - b->len, fmt, ap);
    va_end(ap);
    b->len += (size_t)need;
    b->buf[b->len] = '\0';
}

/* ---- design type table (mirrors FormField.TypeKey / TypeName) ---- */

static const char *fg_kind_of(int ft) {
    switch (ft) {
    case 1:  return "textarea";
    case 2:  return "password";
    case 3:  return "number";
    case 4:  return "radio";
    case 5:  return "checkbox";
    case 6:  return "select";
    case 7:  return "switch";
    case 8:  return "rate";
    case 9:  return "slider";
    case 10: return "date";
    case 11: return "time";
    case 12: return "upload";
    case 13: return "color";
    case 14: return "richtext";
    case 15: return "title";
    case 16: return "text";
    case 17: return "divider";
    case 18: return "card";
    case 19: return "tag";
    case 20: return "badge";
    case 21: return "avatar";
    case 22: return "statistic";
    case 23: return "table";
    case 24: return "timeline";
    case 25: return "tree";
    case 26: return "collapse";
    case 27: return "empty";
    case 28: return "alert";
    case 29: return "progress";
    case 30: return "result";
    case 31: return "skeleton";
    case 32: return "spin";
    case 33: return "tooltip";
    case 34: return "steps";
    case 35: return "breadcrumb";
    case 36: return "tabs";
    case 37: return "pagination";
    case 38: return "menu";
    case 39: return "pageheader";
    case 40: return "listview";
    case 41: return "datatable";
    case 42: return "carousel";
    case 43: return "chart";
    case 44: return "ellipsis";
    case 45: return "panel";
    case 46: return "scrollbar";
    case 47: return "loading";
    case 48: return "dropdown";
    case 49: return "floatbutton";
    case 50: return "popover";
    case 51: return "popconfirm";
    case 52: return "notification";
    case 53: return "ribbon";
    case 54: return "icon";
    case 55: return "contextmenu";
    case 56: return "layer";
    case 57: return "timer";
    default:  return "input";
    }
}

/* Field type of a .zform field object: "kind" string wins, else "type" int. */
static int fg_ftype_of(zan_json_value_t *o) {
    zan_json_value_t *k = zan_json_get(o, "kind");
    if (k && k->type == ZAN_JSON_STRING && k->string_val.str) {
        const char *s = k->string_val.str;
        size_t n = k->string_val.len;
        for (int ft = 0; ft <= 57; ft++) {
            const char *kk = fg_kind_of(ft);
            if (strlen(kk) == n && memcmp(kk, s, n) == 0) return ft;
        }
        if (n == 6 && memcmp(s, "custom", 6) == 0) return 100;
        return 0;
    }
    zan_json_value_t *t = zan_json_get(o, "type");
    if (t && t->type == ZAN_JSON_NUMBER) return (int)t->number_val;
    return 0;
}

/* Concrete Gui widget type for a field type (mirrors
 * ZanIDE.CodeNav.WidgetTypeOf). Anything unmapped renders as Label so the
 * form always compiles and shows its caption. */
static const char *fg_widget_type(int ft) {
    if (ft == 0 || ft == 2 || ft == 3) return "Input";
    if (ft == 1 || ft == 14) return "TextArea";
    if (ft == 4) return "Radio";
    if (ft == 5) return "Checkbox";
    if (ft == 6 || ft == 48) return "SelectBox";
    if (ft == 7) return "Switch";
    if (ft == 8) return "Rate";
    if (ft == 9) return "Slider";
    if (ft == 29) return "Progress";
    if (ft == 49) return "Button";
    if (ft == 18 || ft == 45 || ft == 36) return "Panel";
    return "Label";
}

/* Whether a field needs a caption Label above it (mirrors FieldNeedsCaption). */
static bool fg_needs_caption(int ft) {
    return ft == 0 || ft == 1 || ft == 2 || ft == 3 || ft == 4
        || ft == 6 || ft == 8 || ft == 9 || ft == 29 || ft == 14 || ft == 48;
}

/* Preferred height of a field (mirrors PrefHOf). */
static int fg_pref_h(int ft) {
    if (ft == 1 || ft == 14) return 90;
    if (ft == 29) return 16;
    if (ft == 15) return 30;
    if (ft == 5 || ft == 7 || ft == 4 || ft == 8) return 26;
    if (ft == 49) return 36;
    return 32;
}

/* Container types nest children (mirrors FormField.IsContainerOf). */
static bool fg_is_container(int ft) {
    return ft == 18 || ft == 36;
}

/* ---- string helpers ---- */

/* Escape for embedding inside a Zan double-quoted literal (mirrors EscZ). */
static void fg_esc(fg_buf_t *b, const char *s, size_t n) {
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        if (c == '\\') fg_putf(b, "\\\\");
        else if (c == '"') fg_putf(b, "\\\"");
        else if (c == '\n') fg_putf(b, "\\n");
        else if (c == '\r') { /* drop */ }
        else if (c == '\t') fg_putf(b, "\\t");
        else fg_putf(b, "%c", c);
    }
}

static void fg_esc_cstr(fg_buf_t *b, const char *s) {
    fg_esc(b, s, s ? strlen(s) : 0);
}

/* Identifier-safe check for typed field declarations. */
static bool fg_is_ident(const char *s) {
    if (!s || !s[0]) return false;
    if (!(isalpha((unsigned char)s[0]) || s[0] == '_')) return false;
    for (int i = 1; s[i]; i++) {
        if (!(isalnum((unsigned char)s[i]) || s[i] == '_')) return false;
    }
    return true;
}

/* Join options with '|' (mirrors JoinOpts). Returns malloc'd string or NULL. */
static char *fg_join_opts(zan_json_value_t *o) {
    zan_json_value_t *opts = zan_json_get(o, "options");
    if (!opts || opts->type != ZAN_JSON_ARRAY || opts->array_val.count == 0)
        return NULL;
    size_t need = 1;
    for (int i = 0; i < opts->array_val.count; i++) {
        zan_json_value_t *it = opts->array_val.items[i];
        if (it->type == ZAN_JSON_STRING && it->string_val.str)
            need += it->string_val.len + 1;
    }
    char *out = (char *)malloc(need);
    if (!out) return NULL;
    size_t w = 0;
    for (int i = 0; i < opts->array_val.count; i++) {
        zan_json_value_t *it = opts->array_val.items[i];
        if (i > 0) out[w++] = '|';
        if (it->type == ZAN_JSON_STRING && it->string_val.str) {
            memcpy(out + w, it->string_val.str, it->string_val.len);
            w += it->string_val.len;
        }
    }
    out[w] = '\0';
    return out;
}

static bool fg_obj_bool(zan_json_value_t *o, const char *key) {
    zan_json_value_t *v = zan_json_get(o, key);
    return v && v->type == ZAN_JSON_BOOL && v->bool_val;
}

static int fg_obj_num(zan_json_value_t *o, const char *key, int def) {
    zan_json_value_t *v = zan_json_get(o, key);
    if (v && v->type == ZAN_JSON_NUMBER) return (int)v->number_val;
    return def;
}

static const char *fg_obj_str(zan_json_value_t *o, const char *key) {
    zan_json_value_t *v = zan_json_get(o, key);
    if (v && v->type == ZAN_JSON_STRING && v->string_val.str)
        return v->string_val.str;
    return "";
}

/* The field's display caption: label, else field name, else "". */
static const char *fg_caption(zan_json_value_t *o) {
    const char *label = fg_obj_str(o, "label");
    if (label && label[0]) return label;
    const char *fname = fg_obj_str(o, "name");
    if (fname && fname[0]) return fname;
    return "";
}

/* ---- per-field code emission (mirrors EmitBuildField / CtorExpr /
 *      EmitFieldSetup, plus the missing event wiring) ---- */

static void fg_ctor(fg_buf_t *b, zan_json_value_t *o, int ft, const char *wt) {
    const char *cap = fg_caption(o);
    if (strcmp(wt, "Input") == 0) {
        fg_putf(b, "new Input(\"");
        fg_esc_cstr(b, fg_obj_str(o, "placeholder"));
        fg_putf(b, "\")");
    } else if (strcmp(wt, "TextArea") == 0) {
        fg_putf(b, "TextArea.Create(\"");
        fg_esc_cstr(b, fg_obj_str(o, "placeholder"));
        fg_putf(b, "\")");
    } else if (strcmp(wt, "Button") == 0) {
        fg_putf(b, "new Button { Text = \"");
        fg_esc_cstr(b, cap);
        fg_putf(b, "\" }");
    } else if (strcmp(wt, "Checkbox") == 0) {
        fg_putf(b, "new Checkbox(\"");
        fg_esc_cstr(b, cap);
        fg_putf(b, "\")");
    } else if (strcmp(wt, "Switch") == 0) {
        fg_putf(b, "Switch.Create()");
    } else if (strcmp(wt, "Radio") == 0) {
        fg_putf(b, "new Radio(SignalInt.Create(0), 0, \"");
        fg_esc_cstr(b, cap);
        fg_putf(b, "\")");
    } else if (strcmp(wt, "Slider") == 0) {
        fg_putf(b, "new Slider(0, 100, SignalInt.Create(0))");
    } else if (strcmp(wt, "SelectBox") == 0) {
        fg_putf(b, "SelectBox.FromOptions(new List<string>(), \"\")");
    } else if (strcmp(wt, "Progress") == 0) {
        fg_putf(b, "new Progress(50, 1)");
    } else if (strcmp(wt, "Rate") == 0) {
        fg_putf(b, "new Rate(SignalInt.Create(0), 5)");
    } else if (strcmp(wt, "Panel") == 0) {
        fg_putf(b, "new Panel(\"");
        fg_esc_cstr(b, fg_obj_str(o, "name"));
        fg_putf(b, "\")");
    } else {
        fg_putf(b, "new Label(\"");
        fg_esc_cstr(b, cap);
        fg_putf(b, "\")");
    }
}

/* Extra per-widget initialisation (checked/on state, select options). */
static void fg_field_setup(fg_buf_t *b, zan_json_value_t *o, int ft,
                           const char *wt, const char *vn) {
    if (strcmp(wt, "Checkbox") == 0 && fg_obj_bool(o, "defOn")) {
        fg_putf(b, "        %s.SetChecked(true);\n", vn);
    }
    if (strcmp(wt, "Switch") == 0 && fg_obj_bool(o, "defOn")) {
        fg_putf(b, "        %s.SetOn(true);\n", vn);
    }
    if (strcmp(wt, "SelectBox") == 0) {
        char *opts = fg_join_opts(o);
        if (opts && opts[0]) {
            fg_putf(b, "        %s.SetOptionsText(\"", vn);
            fg_esc_cstr(b, opts);
            fg_putf(b, "\");\n");
        }
        free(opts);
    }
    (void)ft;
}

/* Emit `on<Event>` handler bindings for the field. The event name is the key
 * with the leading "on" stripped; the handler name is the value. __WireForm
 * subscribes each event to form.Handle(<handler name>) so an unregistered
 * handler simply never fires (same contract as UiDoc).
 *
 * Lambdas are non-capturing in Zan (a delegate is a plain function pointer),
 * so the form is reached through the class's own `__form` static instead of
 * the `__WireForm` parameter: static members are visible inside lambdas. */
static void fg_emit_handlers(fg_buf_t *wire, zan_json_value_t *o,
                             const char *vn) {
    zan_json_value_t *obj = o;
    if (!obj || obj->type != ZAN_JSON_OBJECT) return;
    for (int i = 0; i < obj->object_val.count; i++) {
        const char *key = obj->object_val.keys[i] ? obj->object_val.keys[i] : "";
        if (key[0] != 'o' || key[1] != 'n') continue;
        zan_json_value_t *v = obj->object_val.values[i];
        if (!v || v->type != ZAN_JSON_STRING || !v->string_val.str
            || !v->string_val.str[0]) continue;
        fg_putf(wire, "        %s.BindEvent(\"", vn);
        fg_esc_cstr(wire, key + 2);
        fg_putf(wire, "\", () => __form.Handle(\"");
        fg_esc(wire, v->string_val.str, v->string_val.len);
        fg_putf(wire, "\"));\n");
    }
}

/* Track used typed field names so duplicate identifiers fall back to locals. */
typedef struct { const char *names[128]; int count; } fg_used_t;

static bool fg_used_has(fg_used_t *u, const char *n) {
    for (int i = 0; i < u->count; i++)
        if (strcmp(u->names[i], n) == 0) return true;
    return false;
}

static int fg_children_count(zan_json_value_t *o) {
    zan_json_value_t *kids = zan_json_get(o, "kids");
    if (kids && kids->type == ZAN_JSON_ARRAY) return kids->array_val.count;
    return 0;
}

/* Emit one field's declarations/construction into `decls`/`body`, its event
 * wiring into `wire`, its required-field validation lines into `valid`,
 * recursing into container kids. `freeMode` selects the free-canvas layout:
 * absolute DockManual+Place placement with no caption row (the designer's
 * layoutMode == 1), vs. the flow 24-column docking. Returns next local id. */
static int fg_emit_field(fg_buf_t *decls, fg_buf_t *body, fg_buf_t *wire,
                         fg_buf_t *valid, zan_json_value_t *o,
                         const char *parent, int id,
                         fg_used_t *used, bool freeMode) {
    int ft = fg_ftype_of(o);
    const char *wt = fg_widget_type(ft);
    const char *fname = fg_obj_str(o, "name");
    char fallback[24];
    int next = id + 1;
    bool req = fg_obj_bool(o, "required");
    bool isInput = strcmp(wt, "Input") == 0;

    if (!freeMode && fg_needs_caption(ft)) {
        fg_putf(body, "        Label cap%d = new Label(\"", id);
        fg_esc_cstr(body, fg_caption(o));
        fg_putf(body, "\");\n");
        fg_putf(body, "        cap%d.Dock(Dock.Top());\n", id);
        fg_putf(body, "        cap%d.Prefer(0, 18);\n", id);
        fg_putf(body, "        %s.Add(cap%d);\n", parent, id);
    }

    const char *vn;
    bool typed = fg_is_ident(fname) && fname[0] != '\0'
        && !fg_used_has(used, fname);
    if (typed) {
        vn = fname;
        if (used->count < 128) used->names[used->count++] = fname;
        fg_putf(decls, "    static %s %s;\n", wt, vn);
    } else if (req && isInput) {
        // A required field must stay reachable from __ValidateForm, so it is
        // hoisted to a static field even when its name is not an identifier.
        snprintf(fallback, sizeof(fallback), "c%d", id);
        vn = fallback;
        fg_putf(decls, "    static %s %s;\n", wt, vn);
    } else {
        snprintf(fallback, sizeof(fallback), "c%d", id);
        vn = fallback;
        fg_putf(body, "        %s %s;\n", wt, vn);
    }

    fg_putf(body, "        %s = ", vn);
    fg_ctor(body, o, ft, wt);
    fg_putf(body, ";\n");
    fg_putf(body, "        %s.name = \"", vn);
    fg_esc_cstr(body, fname);
    fg_putf(body, "\";\n");
    fg_field_setup(body, o, ft, wt, vn);
    if (strcmp(wt, "Label") == 0 && fg_obj_bool(o, "wrap")) {
        fg_putf(body, "        %s.wrap = true;\n", vn);
    }
    if (req && isInput) {
        fg_putf(valid, "        if (TextWrap.Trim(%s.GetText()) == \"\") { %s.SetError(\"Required\"); ok = false; }\n", vn, vn);
    }
    if (freeMode) {
        int fx = fg_obj_num(o, "fx", 0);
        int fy = fg_obj_num(o, "fy", 0);
        int fw = fg_obj_num(o, "fw", 200);
        int fh = fg_obj_num(o, "fh", 32);
        /* Free-canvas coordinates are authored at 100%: scale them to the
         * display like the theme metrics and the stylesheet lengths already
         * are, so a design keeps its proportions (and stays inside its
         * winShape silhouette, which the runtime scales too) at 125/150/200%. */
        fg_putf(body, "        %s.Dock(Dock.Manual());\n", vn);
        fg_putf(body, "        %s.Place(%d * __dp / 100, %d * __dp / 100);\n",
                vn, fx, fy);
        fg_putf(body, "        %s.Prefer(%d * __dp / 100, %d * __dp / 100);\n",
                vn, fw, fh);
        if (fg_is_container(ft)) {
            fg_putf(body, "        %s.Pad(0);\n", vn);
        }
    } else {
        fg_putf(body, "        %s.Dock(Dock.Top());\n", vn);
        if (fg_is_container(ft)) {
            fg_putf(body, "        %s.Pad(10);\n", vn);
            int ph = 44 + 44 * (int)fg_children_count(o);
            fg_putf(body, "        %s.Prefer(0, %d);\n", vn, ph);
        } else {
            fg_putf(body, "        %s.Prefer(0, %d);\n", vn, fg_pref_h(ft));
        }
    }
    fg_putf(body, "        %s.Add(%s);\n", parent, vn);
    fg_emit_handlers(wire, o, vn);

    zan_json_value_t *kids = zan_json_get(o, "kids");
    if (fg_is_container(ft) && kids && kids->type == ZAN_JSON_ARRAY) {
        for (int i = 0; i < kids->array_val.count; i++)
            next = fg_emit_field(decls, body, wire, valid,
                                 kids->array_val.items[i],
                                 vn, next, used, freeMode);
    }
    return next;
}

/* ---- entry point ---- */

char *zan_formgen_translate(const char *json, size_t json_len,
                            const char *file_name, struct zan_diag *diag) {
    (void)diag;
    zan_json_value_t *root = zan_json_parse(json, json_len);
    if (!root || root->type != ZAN_JSON_OBJECT) {
        if (root) zan_json_free(root);
        return NULL;
    }

    const char *name = fg_obj_str(root, "name");
    char namebuf[128];
    if (!name[0] || !fg_is_ident(name)) {
        /* fall back to the file's base name */
        const char *base = file_name ? file_name : "";
        const char *slash = strrchr(base, '/');
        const char *bslash = strrchr(base, '\\');
        const char *p = (slash && (!bslash || slash > bslash))
                        ? slash + 1 : (bslash ? bslash + 1 : base);
        size_t n = strlen(p);
        if (n > 6 && memcmp(p + n - 6, ".zform", 6) == 0) n -= 6;
        if (n > 127) n = 127;
        memcpy(namebuf, p, n);
        namebuf[n] = '\0';
        name = namebuf;
        if (!name[0] || !fg_is_ident(name)) { name = "Form"; }
    }

    zan_json_value_t *fields = zan_json_get(root, "fields");
    int win_w = 480, win_h = 520;
    zan_json_value_t *w = zan_json_get(root, "winW");
    if (w && w->type == ZAN_JSON_NUMBER && w->number_val > 0) win_w = (int)w->number_val;
    zan_json_value_t *h = zan_json_get(root, "winH");
    if (h && h->type == ZAN_JSON_NUMBER && h->number_val > 0) win_h = (int)h->number_val;
    bool free_mode = fg_obj_num(root, "layoutMode", 0) == 1;
    const char *title = fg_obj_str(root, "winTitle");
    if (!title[0]) title = name;
    bool center = fg_obj_bool(root, "winCenter");
    int pos_x = 0, pos_y = 0;
    zan_json_value_t *px = zan_json_get(root, "winPosX");
    if (px && px->type == ZAN_JSON_NUMBER) pos_x = (int)px->number_val;
    zan_json_value_t *py = zan_json_get(root, "winPosY");
    if (py && py->type == ZAN_JSON_NUMBER) pos_y = (int)py->number_val;
    /* Window appearance options: winShape (0 rect, 1 rounded rect with
     * winShapeRadius, 2 ellipse, or a JSON array of shape regions
     * [{t,x,y,w,h,r},...] whose union is the silhouette), winChrome (custom
     * title bar, default true), winGlass (OS-native acrylic), winOpacity
     * (10..100, 0 = keep opaque). Projected onto the App API in Main. */
    int win_shape = fg_obj_num(root, "winShape", 0);
    int win_shape_radius = fg_obj_num(root, "winShapeRadius", 0);
    int win_shadow = fg_obj_num(root, "winShadow", 0);
    int win_opacity = fg_obj_num(root, "winOpacity", 0);
    bool win_glass = fg_obj_bool(root, "winGlass");
    bool win_chrome = true;
    zan_json_value_t *wch = zan_json_get(root, "winChrome");
    if (wch && wch->type == ZAN_JSON_BOOL) win_chrome = wch->bool_val;
    /* Compose the silhouette spec string: "t,x,y,w,h,r;..." in logical px. */
    char shape_spec[512];
    shape_spec[0] = '\0';
    zan_json_value_t *wshape = zan_json_get(root, "winShape");
    if (wshape && wshape->type == ZAN_JSON_ARRAY) {
        int n = 0;
        for (int i = 0; i < wshape->array_val.count; i++) {
            zan_json_value_t *reg = wshape->array_val.items[i];
            int t = fg_obj_num(reg, "t", 1);
            int rx = fg_obj_num(reg, "x", 0);
            int ry = fg_obj_num(reg, "y", 0);
            int rw = fg_obj_num(reg, "w", 0);
            int rh = fg_obj_num(reg, "h", 0);
            int rr = fg_obj_num(reg, "r", 0);
            if (rw <= 0 || rh <= 0) { continue; }
            if (n > 0) {
                size_t L = strlen(shape_spec);
                if (L + 2 < sizeof(shape_spec)) { strcat(shape_spec, ";"); }
            }
            snprintf(shape_spec + strlen(shape_spec),
                     sizeof(shape_spec) - strlen(shape_spec),
                     "%d,%d,%d,%d,%d,%d", t, rx, ry, rw, rh, rr);
            n = n + 1;
        }
    } else if (win_shape == 1) {
        snprintf(shape_spec, sizeof(shape_spec), "1,0,0,%d,%d,%d",
                 win_w, win_h, win_shape_radius);
    } else if (win_shape == 2) {
        int d = win_w;
        if (win_h < d) { d = win_h; }
        snprintf(shape_spec, sizeof(shape_spec), "2,0,0,%d,%d,0", d, d);
    }

    /* winShadow: prepend a type-3 "shadow band" region — a full-window rounded
     * rect whose radius tracks the shape's — so the OS surface covers the
     * drop-shadow pixels App paints around the silhouette (type 3 is treated
     * as a rounded rect by the backend and casts no shadow itself). */
    if (win_shadow > 0 && shape_spec[0] != '\0') {
        int max_r = win_shape_radius;
        if (wshape && wshape->type == ZAN_JSON_ARRAY) {
            for (int i = 0; i < wshape->array_val.count; i++) {
                int rr = fg_obj_num(wshape->array_val.items[i], "r", 0);
                if (rr > max_r) { max_r = rr; }
            }
        }
        int band_r = max_r + win_shadow;
        if (band_r < 8) { band_r = 8; }
        if (band_r > 64) { band_r = 64; }
        char band[64];
        snprintf(band, sizeof(band), "3,0,0,%d,%d,%d;", win_w, win_h, band_r);
        char tmp[576];
        snprintf(tmp, sizeof(tmp), "%s%s", band, shape_spec);
        snprintf(shape_spec, sizeof(shape_spec), "%s", tmp);
    }

    fg_buf_t decls = {0}, body = {0}, wire = {0}, valid = {0}, out = {0};
    fg_used_t used = {0};

    if (fields && fields->type == ZAN_JSON_ARRAY) {
        int id = 0;
        for (int i = 0; i < fields->array_val.count; i++)
            id = fg_emit_field(&decls, &body, &wire, &valid,
                               fields->array_val.items[i],
                               "root", id, &used, free_mode);
    }

    /* ---- assemble the synthetic partial class ---- */
    fg_putf(&out, "using System;\n");
    fg_putf(&out, "using Gui;\n");
    fg_putf(&out, "using Gui.Widget;\n\n");
    fg_putf(&out, "// <auto-generated> Window layout compiled from %s.\n",
            file_name ? file_name : "<design>");
    fg_putf(&out, "// Regenerated from the visual design on every build - do not edit.\n");
    fg_putf(&out, "// Write your initialization and event logic in %s.zan.\n", name);
    fg_putf(&out, "partial class %s {\n", name);
    fg_putf(&out, "    /// The live form, kept for the non-capturing handler\n");
    fg_putf(&out, "    /// lambdas (delegates are plain function pointers).\n");
    fg_putf(&out, "    static Form __form;\n");
    if (decls.buf) {
        fg_putf(&out, "%s", decls.buf);
        fg_putf(&out, "\n");
    }
    fg_putf(&out, "    /// Builds the designed control tree (real, interactive widgets).\n");
    fg_putf(&out, "    static Control __BuildForm() {\n");
    fg_putf(&out, "        Panel root = new Panel(\"root\");\n");
    fg_putf(&out, "        root.Dock(Dock.Fill());\n");
    if (free_mode) {
        // Free-canvas design: coordinates are absolute window pixels, so the
        // root must not add its own padding/gap.
        fg_putf(&out, "        root.Pad(0);\n");
        fg_putf(&out, "        root.gap = 0;\n");
        fg_putf(&out, "        int __dp = Canvas.GetDpiScale();\n");
    } else {
        fg_putf(&out, "        root.Pad(16);\n");
        fg_putf(&out, "        root.gap = 10;\n");
    }
    if (body.buf) fg_putf(&out, "%s", body.buf);
    fg_putf(&out, "        return root;\n");
    fg_putf(&out, "    }\n\n");
    fg_putf(&out, "    /// Validates the required fields: flags every empty required\n");
    fg_putf(&out, "    /// input (red border + message) and reports whether the form is\n");
    fg_putf(&out, "    /// OK to submit. Call it from the submit handler:\n");
    fg_putf(&out, "    ///     if (!__ValidateForm()) { return; }\n");
    fg_putf(&out, "    static bool __ValidateForm() {\n");
    fg_putf(&out, "        bool ok = true;\n");
    if (valid.buf) fg_putf(&out, "%s", valid.buf);
    fg_putf(&out, "        return ok;\n");
    fg_putf(&out, "    }\n\n");
    fg_putf(&out, "    /// Subscribes every designed `on<Event>` binding to the handler\n");
    fg_putf(&out, "    /// registry on the form (see Form.On / Form.Handle). Unregistered\n");
    fg_putf(&out, "    /// handler names simply never fire.\n");
    fg_putf(&out, "    static void __WireForm(Form form) {\n");
    fg_putf(&out, "        __form = form;\n");
    if (wire.buf) fg_putf(&out, "%s", wire.buf);
    fg_putf(&out, "    }\n\n");
    fg_putf(&out, "    static void Main() {\n");
    fg_putf(&out, "        Form form = Form.CreateDark(\"");
    fg_esc_cstr(&out, title);
    fg_putf(&out, "\", %d, %d);\n", win_w, win_h);
    fg_putf(&out, "        form.Add(__BuildForm());\n");
    if (center) {
        fg_putf(&out, "        form.GetApp().CenterWindow();\n");
    } else {
        fg_putf(&out, "        form.GetApp().SetWindowPos(%d, %d);\n", pos_x, pos_y);
    }
    if (shape_spec[0] != '\0') {
        fg_putf(&out, "        form.GetApp().SetWindowShape(\"");
        fg_esc_cstr(&out, shape_spec);
        fg_putf(&out, "\");\n");
    }
    if (win_shadow > 0) {
        fg_putf(&out, "        form.GetApp().SetWindowShadow(%d);\n", win_shadow);
    }
    if (!win_chrome) {
        fg_putf(&out, "        form.GetApp().SetChromeVisible(false);\n");
    }
    if (win_glass) {
        fg_putf(&out, "        form.GetApp().SetNativeGlass(true);\n");
    }
    if (win_opacity > 0 && win_opacity < 100) {
        fg_putf(&out, "        form.GetApp().SetWindowOpacity(%d);\n", win_opacity);
    }
    fg_putf(&out, "        %s.OnLoad(form);\n", name);
    fg_putf(&out, "        %s.__WireForm(form);\n", name);
    fg_putf(&out, "        form.Run();\n");
    fg_putf(&out, "    }\n");
    fg_putf(&out, "}\n");

    zan_json_free(root);
    free(decls.buf);
    free(body.buf);
    free(wire.buf);
    if (!out.buf) { out.buf = (char *)malloc(1); if (out.buf) out.buf[0] = '\0'; }
    return out.buf;
}
