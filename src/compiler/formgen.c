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
 *         static Form __CreateWindow() {...}  // window + OnLoad + Wire
 *         static void Show() {...}            // open it, run until closed
 *         static void Main() {...}            // entry, primary design only
 *     }
 *
 * Several designs compile together (a multi-window project): only the FIRST
 * compiler input gets a Main(), every design gets a Show(), so one window
 * opens another with `Settings.Show();`. Show() runs the opened window's own
 * event loop and returns when that window closes, so from the caller's side
 * it behaves like a modal dialog (the calling window waits); use
 * __CreateWindow() when the caller wants to adjust the Form before running
 * it.
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

/* A .zform field names the concrete Zan Control class directly.  There is no
 * numeric type table here: the generated source is intentionally generic so
 * a project can add any Control without changing the compiler. */
static const char *fg_kind_of(zan_json_value_t *o) {
    zan_json_value_t *k = zan_json_get(o, "kind");
    if (k && k->type == ZAN_JSON_STRING && k->string_val.str)
        return k->string_val.str;
    return "";
}

static bool fg_is_ident(const char *s);

static void fg_validate_fields(zan_json_value_t *arr, zan_diag_t *diag,
                               const char *file_name) {
    if (!arr || arr->type != ZAN_JSON_ARRAY) return;
    for (int i = 0; i < arr->array_val.count; i++) {
        zan_json_value_t *o = arr->array_val.items[i];
        if (!o || o->type != ZAN_JSON_OBJECT) {
            if (diag) {
                zan_diag_emit(diag, DIAG_ERROR, zan_loc(0, 0, 0, 0),
                              "%s: .zform field %d must be an object",
                              file_name ? file_name : "<design>", i);
            }
            continue;
        }
        const char *kind = fg_kind_of(o);
        if (!fg_is_ident(kind) && diag) {
            zan_diag_emit(diag, DIAG_ERROR, zan_loc(0, 0, 0, 0),
                          "%s: .zform field %d requires a valid Zan Control kind; use the class name in \"kind\"",
                          file_name ? file_name : "<design>", i);
        }
        zan_json_value_t *kids = zan_json_get(o, "kids");
        fg_validate_fields(kids, diag, file_name);
    }
}

/* A field with children.  Container semantics belong to the
 * Control implementation; formgen only preserves the declared tree. */
static bool fg_is_container(zan_json_value_t *o) {
    zan_json_value_t *kids = zan_json_get(o, "kids");
    return kids && kids->type == ZAN_JSON_ARRAY;
}

static int fg_obj_num(zan_json_value_t *o, const char *key, int def);

static int fg_default_dock(zan_json_value_t *o) {
    (void)o;
    return 0;
}

static int fg_pref_h(zan_json_value_t *o) {
    int h = fg_obj_num(o, "fh", 32);
    return h > 0 ? h : 32;
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

/* ---- per-field code emission (mirrors EmitBuildField / CtorExpr /
 *      EmitFieldSetup, plus the missing event wiring) ---- */

/* Emit the component named by `kind` directly. The compiler deliberately does
 * not know the component's implementation or maintain a type mapping. A
 * missing class/constructor is reported by the normal Zan compiler. */
static void fg_ctor(fg_buf_t *b, zan_json_value_t *o, const char *kind) {
    (void)o;
    fg_putf(b, "new %s()", kind);
}

/* Apply schema-level properties without knowing what the component is. Each
 * Control may override SetProp/SetExtra for its own options. */
static void fg_field_setup(fg_buf_t *b, zan_json_value_t *o, const char *vn) {
    char *opts = fg_join_opts(o);
    if (opts && opts[0]) {
        fg_putf(b, "        %s.SetProp(\"options\", \"", vn);
        fg_esc_cstr(b, opts);
        fg_putf(b, "\");\n");
    }
    free(opts);
    const char *placeholder = fg_obj_str(o, "placeholder");
    if (placeholder[0]) {
        fg_putf(b, "        %s.SetProp(\"placeholder\", \"", vn);
        fg_esc_cstr(b, placeholder);
        fg_putf(b, "\");\n");
    }
    if (fg_obj_bool(o, "required")) {
        fg_putf(b, "        %s.SetProp(\"required\", \"true\");\n", vn);
    }
    if (fg_obj_bool(o, "defOn")) {
        fg_putf(b, "        %s.SetProp(\"defOn\", \"true\");\n", vn);
    }
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
                         fg_used_t *used, bool freeMode,
                         bool parentIsFlow) {
    const char *kind = fg_kind_of(o);
    const char *fname = fg_obj_str(o, "name");
    char fallback[24];
    int next = id + 1;
    bool req = fg_obj_bool(o, "required");

    bool flowCell = !freeMode && parentIsFlow;

    const char *vn;
    bool typed = fg_is_ident(fname) && fname[0] != '\0'
        && !fg_used_has(used, fname);
    if (typed) {
        vn = fname;
        if (used->count < 128) used->names[used->count++] = fname;
        fg_putf(decls, "    static %s %s;\n", kind, vn);
    } else {
        snprintf(fallback, sizeof(fallback), "c%d", id);
        vn = fallback;
        fg_putf(body, "        %s %s;\n", kind, vn);
    }

    fg_putf(body, "        %s = ", vn);
    fg_ctor(body, o, kind);
    fg_putf(body, ";\n");
    fg_putf(body, "        %s.name = \"", vn);
    fg_esc_cstr(body, fname);
    fg_putf(body, "\";\n");
    fg_field_setup(body, o, vn);
    if (req) {
        /* Required validation is component-owned. A custom Control may expose
         * its own required/error properties; formgen must not cast it to an
         * Input or assume a GetText API. */
        fg_putf(body, "        %s.SetProp(\"required\", \"true\");\n", vn);
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
        int dk = fg_obj_num(o, "dock", fg_default_dock(o));
        if (dk == 1 || dk == 2) {
            fg_putf(body, "        %s.Dock(Dock.%s());\n",
                    vn, dk == 1 ? "Top" : "Bottom");
            fg_putf(body, "        %s.Prefer(0, %d * __dp / 100);\n", vn, fh);
        } else if (dk == 3 || dk == 4) {
            fg_putf(body, "        %s.Dock(Dock.%s());\n",
                    vn, dk == 3 ? "Left" : "Right");
            fg_putf(body, "        %s.Prefer(%d * __dp / 100, 0);\n", vn, fw);
        } else if (dk == 5) {
            fg_putf(body, "        %s.Dock(Dock.Fill());\n", vn);
        } else {
            fg_putf(body, "        %s.Dock(Dock.Manual());\n", vn);
            fg_putf(body, "        %s.Place(%d * __dp / 100, %d * __dp / 100);\n",
                    vn, fx, fy);
            fg_putf(body, "        %s.Prefer(%d * __dp / 100, %d * __dp / 100);\n",
                    vn, fw, fh);
        }
        if (fg_is_container(o)) {
            fg_putf(body, "        %s.Pad(0);\n", vn);
        }
        fg_putf(body, "        %s.Add(%s);\n", parent, vn);
    } else if (!flowCell) {
        /* Legacy stacking path: used only for children of tab pages, split
         * panes and dock panels, whose parent is a plain Panel, not an
         * FbFlow. Top-level fields and card contents go through AddCell. */
        fg_putf(body, "        %s.Dock(Dock.Top());\n", vn);
        if (fg_is_container(o)) {
            fg_putf(body, "        %s.Pad(10);\n", vn);
            int ph = 44 + 44 * (int)fg_children_count(o);
            fg_putf(body, "        %s.Prefer(0, %d);\n", vn, ph);
        } else {
            fg_putf(body, "        %s.Prefer(0, %d);\n", vn, fg_pref_h(o));
        }
        fg_putf(body, "        %s.Add(%s);\n", parent, vn);
    } else {
        /* Flow (auto-snap) mode: add the field as a 24-column cell to the
         * FbFlow `parent`, exactly like FormBuilder.Build - one layout
         * authority shared by designer preview and runtime. */
        int sp = fg_obj_num(o, "span", 24);
        if (sp < 1) sp = 1;
        if (sp > 24) sp = 24;
        if (fg_is_container(o)) {
            fg_putf(body, "        %s.Pad(10);\n", vn);
            int ph = 44 + 44 * (int)fg_children_count(o);
            fg_putf(body, "        %s.Prefer(0, %d);\n", vn, ph);
            fg_putf(body, "        %s.AddCell(%s, 24);\n", parent, vn);
        } else {
            fg_putf(body, "        %s.Prefer(0, %d);\n", vn, fg_pref_h(o));
            fg_putf(body, "        %s.AddCell(%s, %d);\n", parent, vn, sp);
        }
    }
    fg_emit_handlers(wire, o, vn);

    zan_json_value_t *kids = zan_json_get(o, "kids");
    if (fg_is_container(o) && kids && kids->type == ZAN_JSON_ARRAY) {
        /* The component owns its child layout. Formgen only appends each
         * generated child to the declared parent Control. */
        for (int i = 0; i < kids->array_val.count; i++) {
            zan_json_value_t *kid = kids->array_val.items[i];
            next = fg_emit_field(decls, body, wire, valid, kid,
                                 vn, next, used, freeMode, false);
        }
    }
    return next;
}

/* ---- entry point ---- */

char *zan_formgen_translate(const char *json, size_t json_len,
                            const char *file_name, struct zan_diag *diag,
                            int emit_main) {
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
    fg_validate_fields(fields, diag, file_name);
    if (diag && zan_diag_has_errors(diag)) {
        zan_json_free(root);
        return NULL;
    }
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
        const char *rootParent = "root";
        if (!free_mode) {
            /* Flow mode packs fields into 24-column rows via one shared FbFlow
             * (the same primitive FormBuilder.Build and the designer preview
             * use), so `span` is honoured identically at design and run time. */
            fg_putf(&body, "        FbFlow __flow = new FbFlow();\n");
            fg_putf(&body, "        __flow.Dock(Dock.Top());\n");
            fg_putf(&body, "        root.Add(__flow);\n");
            rootParent = "__flow";
        }
        for (int i = 0; i < fields->array_val.count; i++)
            id = fg_emit_field(&decls, &body, &wire, &valid,
                               fields->array_val.items[i],
                               rootParent, id, &used, free_mode, !free_mode);
    }

    /* ---- assemble the synthetic partial class ---- */
    fg_putf(&out, "using System;\n");
    fg_putf(&out, "using Gui;\n");
    fg_putf(&out, "using Gui.Widget;\n");
    fg_putf(&out, "using Gui.Hmi;\n\n");
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
    fg_putf(&out, "    /// Builds this designed window: control tree, window\n");
    fg_putf(&out, "    /// attributes, OnLoad and the event wiring - everything except\n");
    fg_putf(&out, "    /// the event loop, so a caller may still adjust it before Run().\n");
    fg_putf(&out, "    static Form __CreateWindow() {\n");
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
    fg_putf(&out, "        return form;\n");
    fg_putf(&out, "    }\n\n");
    fg_putf(&out, "    /// Opens this designed window and runs it until it closes,\n");
    fg_putf(&out, "    /// so the calling window waits like it would on a modal dialog.\n");
    fg_putf(&out, "    /// This is how one window calls another:\n");
    fg_putf(&out, "    ///     %s.Show();\n", name);
    fg_putf(&out, "    static void Show() {\n");
    fg_putf(&out, "        Form form = %s.__CreateWindow();\n", name);
    fg_putf(&out, "        form.Run();\n");
    fg_putf(&out, "    }\n");
    if (emit_main) {
        /* Only the primary input opens as the program's entry point: the other
         * designs of a multi-window project compile alongside it and are
         * reached through Show(), so there is exactly one Main(). */
        fg_putf(&out, "\n    static void Main() {\n");
        fg_putf(&out, "        %s.Show();\n", name);
        fg_putf(&out, "    }\n");
    }
    fg_putf(&out, "}\n");

    zan_json_free(root);
    free(decls.buf);
    free(body.buf);
    free(wire.buf);
    if (!out.buf) { out.buf = (char *)malloc(1); if (out.buf) out.buf[0] = '\0'; }
    return out.buf;
}
