/* scenegen.c -- .zscene design-document translation.
 *
 * Translates the scene designer's .zscene JSON (see stdlib/Game/Scene/
 * SceneDoc.zan ToJson for the format) into synthetic Zan source:
 *
 *     partial class <Name> {
 *         static SceneElement <element>;      // typed fields (designer names)
 *         static SceneDoc scene;
 *         static SceneDoc BuildScene() {...}  // the designed document
 *         static void Run() {...}             // render loop via SceneView
 *         static void Main() {...}            // entry, primary design only
 *     }
 *
 * The synthetic class is a compile-time projection of the design document:
 * nothing is written to disk, so the .zscene stays the single source of truth
 * (the same two-file model as .zform/formgen -- no Name.g.zan). Element
 * fields keep the designer names, so the business file (<Name>.zan, which
 * supplies OnLoad) manipulates the designed elements directly.
 *
 * Several designs can compile together: only the first compiler input gets a
 * Main(), every design gets a Run().
 */
#include "scenegen.h"
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
} sg_buf_t;

static void sg_putf(sg_buf_t *b, const char *fmt, ...) {
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

static void sg_esc(sg_buf_t *b, const char *s) {
    if (!s) return;
    for (size_t i = 0; s[i]; i++) {
        char c = s[i];
        if (c == '\\' || c == '"') sg_putf(b, "\\%c", c);
        else if (c == '\n') sg_putf(b, "\\n");
        else if (c == '\r') sg_putf(b, "\\r");
        else if (c == '\t') sg_putf(b, "\\t");
        else sg_putf(b, "%c", c);
    }
}

static int sg_is_ident(const char *s) {
    if (!s || !s[0]) return 0;
    if (!(isalpha((unsigned char)s[0]) || s[0] == '_')) return 0;
    for (int i = 1; s[i]; i++) {
        if (!(isalnum((unsigned char)s[i]) || s[i] == '_')) return 0;
    }
    return 1;
}

static int sg_obj_num(zan_json_value_t *o, const char *key, int def) {
    zan_json_value_t *v = zan_json_get(o, key);
    if (v && v->type == ZAN_JSON_NUMBER) return (int)v->number_val;
    return def;
}

static const char *sg_obj_str(zan_json_value_t *o, const char *key) {
    zan_json_value_t *v = zan_json_get(o, key);
    if (v && v->type == ZAN_JSON_STRING && v->string_val.str)
        return v->string_val.str;
    return "";
}

static int sg_obj_bool(zan_json_value_t *o, const char *key, int def) {
    zan_json_value_t *v = zan_json_get(o, key);
    if (v && v->type == ZAN_JSON_BOOL) return v->bool_val ? 1 : 0;
    return def;
}

/* Component `i` of a numeric array property ("color", "clearColor"). */
static int sg_arr_num(zan_json_value_t *o, const char *key, int i, int def) {
    zan_json_value_t *a = zan_json_get(o, key);
    if (!a || a->type != ZAN_JSON_ARRAY || i >= a->array_val.count) return def;
    zan_json_value_t *v = a->array_val.items[i];
    if (v && v->type == ZAN_JSON_NUMBER) return (int)v->number_val;
    return def;
}

/* Names already claimed by a typed field, so duplicates fall back to locals. */
typedef struct {
    char **names;
    int count;
    int cap;
} sg_used_t;

static int sg_used_has(sg_used_t *u, const char *n) {
    for (int i = 0; i < u->count; i++)
        if (strcmp(u->names[i], n) == 0) return 1;
    return 0;
}

static void sg_used_add(sg_used_t *u, const char *n) {
    if (u->count == u->cap) {
        int nc = u->cap ? u->cap * 2 : 16;
        char **nn = (char **)realloc(u->names, (size_t)nc * sizeof(char *));
        if (!nn) return;
        u->names = nn;
        u->cap = nc;
    }
    size_t ln = strlen(n) + 1;
    char *copy = (char *)malloc(ln);
    if (!copy) return;
    memcpy(copy, n, ln);
    u->names[u->count++] = copy;
}

char *zan_scenegen_translate(const char *json, size_t json_len,
                             const char *file_name, struct zan_diag *diag,
                             int emit_main) {
    (void)diag;
    zan_json_value_t *root = zan_json_parse(json, json_len);
    if (!root || root->type != ZAN_JSON_OBJECT) {
        if (root) zan_json_free(root);
        return NULL;
    }

    const char *name = sg_obj_str(root, "name");
    char namebuf[128];
    if (!sg_is_ident(name)) {
        const char *base = file_name ? file_name : "";
        const char *slash = strrchr(base, '/');
        const char *bslash = strrchr(base, '\\');
        const char *p = (slash && (!bslash || slash > bslash))
                        ? slash + 1 : (bslash ? bslash + 1 : base);
        size_t n = strlen(p);
        if (n > 7 && memcmp(p + n - 7, ".zscene", 7) == 0) n -= 7;
        if (n > 127) n = 127;
        memcpy(namebuf, p, n);
        namebuf[n] = '\0';
        name = namebuf;
        if (!sg_is_ident(name)) name = "Scene";
    }

    int dw = sg_obj_num(root, "designWidth", 1280);
    if (dw <= 0) dw = 1280;
    int dh = sg_obj_num(root, "designHeight", 720);
    if (dh <= 0) dh = 720;

    sg_buf_t decls = {0}, body = {0}, out = {0};
    sg_used_t used = {0};

    zan_json_value_t *elems = zan_json_get(root, "elements");
    int ecount = (elems && elems->type == ZAN_JSON_ARRAY)
                 ? elems->array_val.count : 0;
    for (int i = 0; i < ecount; i++) {
        zan_json_value_t *e = elems->array_val.items[i];
        if (!e || e->type != ZAN_JSON_OBJECT) continue;
        const char *ename = sg_obj_str(e, "name");
        char vn[160];
        if (sg_is_ident(ename) && !sg_used_has(&used, ename)) {
            snprintf(vn, sizeof(vn), "%s", ename);
            sg_used_add(&used, vn);
            sg_putf(&decls, "    static SceneElement %s;\n", vn);
        } else {
            snprintf(vn, sizeof(vn), "e%d", i);
            sg_putf(&body, "        SceneElement %s;\n", vn);
        }
        sg_putf(&body, "        %s = new SceneElement(\"", vn);
        sg_esc(&body, sg_obj_str(e, "kind"));
        sg_putf(&body, "\", \"");
        sg_esc(&body, ename);
        sg_putf(&body, "\");\n");
        sg_putf(&body, "        %s.At(%d, %d);\n", vn,
                sg_obj_num(e, "x", 0), sg_obj_num(e, "y", 0));
        sg_putf(&body, "        %s.Size(%d, %d);\n", vn,
                sg_obj_num(e, "w", 0), sg_obj_num(e, "h", 0));
        sg_putf(&body, "        %s.AnchorTo(%d, %d);\n", vn,
                sg_obj_num(e, "anchorX", 0), sg_obj_num(e, "anchorY", 0));
        sg_putf(&body, "        %s.Order(%d);\n", vn, sg_obj_num(e, "z", 0));
        sg_putf(&body, "        %s.Tint(%d, %d, %d, %d);\n", vn,
                sg_arr_num(e, "color", 0, 255), sg_arr_num(e, "color", 1, 255),
                sg_arr_num(e, "color", 2, 255), sg_arr_num(e, "color", 3, 255));
        const char *img = sg_obj_str(e, "image");
        if (img[0]) {
            sg_putf(&body, "        %s.Image(\"", vn);
            sg_esc(&body, img);
            sg_putf(&body, "\");\n");
        }
        const char *text = sg_obj_str(e, "text");
        if (text[0]) {
            sg_putf(&body, "        %s.Caption(\"", vn);
            sg_esc(&body, text);
            sg_putf(&body, "\");\n");
        }
        int rows = sg_obj_num(e, "rows", 0), cols = sg_obj_num(e, "cols", 0);
        int gapx = sg_obj_num(e, "gapX", 0), gapy = sg_obj_num(e, "gapY", 0);
        if (rows > 0 || cols > 0 || gapx != 0 || gapy != 0) {
            sg_putf(&body, "        %s.Grid(%d, %d, %d, %d);\n", vn,
                    rows, cols, gapx, gapy);
        }
        int value = sg_obj_num(e, "value", 0);
        if (value != 0) {
            sg_putf(&body, "        %s.SetValue(%d);\n", vn, value);
        }
        const char *items = sg_obj_str(e, "items");
        if (items[0]) {
            sg_putf(&body, "        %s.SetItems(\"", vn);
            sg_esc(&body, items);
            sg_putf(&body, "\");\n");
        }
        if (sg_obj_bool(e, "locked", 0)) {
            sg_putf(&body, "        %s.SetLocked(true);\n", vn);
        }
        if (!sg_obj_bool(e, "visible", 1)) {
            sg_putf(&body, "        %s.SetVisible(false);\n", vn);
        }
        sg_putf(&body, "        d.Add(%s);\n", vn);
    }

    sg_putf(&out, "// <auto-generated> compile-time projection of %s\n",
            file_name ? file_name : "(scene)");
    sg_putf(&out, "// The .zscene design document is the source of truth; put\n");
    sg_putf(&out, "// your initialization and game logic in %s.zan.\n", name);
    sg_putf(&out, "using System;\n");
    sg_putf(&out, "using Gui;\n");
    sg_putf(&out, "using Game.Scene;\n\n");
    sg_putf(&out, "partial class %s {\n", name);
    if (decls.buf) sg_putf(&out, "%s", decls.buf);
    sg_putf(&out, "    static SceneDoc scene;\n\n");
    sg_putf(&out, "    /// Builds the designed scene document (design-resolution tree).\n");
    sg_putf(&out, "    static SceneDoc BuildScene() {\n");
    sg_putf(&out, "        SceneDoc d = new SceneDoc(\"");
    sg_esc(&out, name);
    sg_putf(&out, "\", %d, %d);\n", dw, dh);
    sg_putf(&out, "        d.SetMode(%d);\n", sg_obj_num(root, "scaleMode", 0));
    sg_putf(&out, "        d.SetClearColor(%d, %d, %d);\n",
            sg_arr_num(root, "clearColor", 0, 0),
            sg_arr_num(root, "clearColor", 1, 0),
            sg_arr_num(root, "clearColor", 2, 0));
    const char *bg = sg_obj_str(root, "background");
    if (bg[0]) {
        sg_putf(&out, "        d.SetBackground(\"");
        sg_esc(&out, bg);
        sg_putf(&out, "\");\n");
    }
    if (body.buf) sg_putf(&out, "%s", body.buf);
    sg_putf(&out, "        return d;\n");
    sg_putf(&out, "    }\n\n");
    sg_putf(&out, "    /// Opens this designed scene and renders it until the window\n");
    sg_putf(&out, "    /// closes, so one design opens another with `%s.Run();`.\n", name);
    sg_putf(&out, "    static void Run() {\n");
    sg_putf(&out, "        scene = BuildScene();\n");
    sg_putf(&out, "        App app = App.CreateDark(\"");
    sg_esc(&out, name);
    sg_putf(&out, "\", %d, %d);\n", dw, dh);
    if (sg_obj_num(root, "winCenter", 0) != 0) {
        sg_putf(&out, "        app.CenterWindow();\n");
    } else {
        sg_putf(&out, "        app.SetWindowPos(%d, %d);\n",
                sg_obj_num(root, "winPosX", 0), sg_obj_num(root, "winPosY", 0));
    }
    sg_putf(&out, "        app.Show();\n");
    sg_putf(&out, "        %s.OnLoad(app);\n", name);
    sg_putf(&out, "        while (app.isRunning) {\n");
    sg_putf(&out, "            if (!app.ProcessEvent()) { break; }\n");
    sg_putf(&out, "            if (!app.needsRedraw) { continue; }\n");
    sg_putf(&out, "            app.BeginFrame();\n");
    sg_putf(&out, "            SceneView.Render(app, scene);\n");
    sg_putf(&out, "            app.RenderChrome(\"");
    sg_esc(&out, name);
    sg_putf(&out, "\");\n");
    sg_putf(&out, "            app.PresentFrame();\n");
    sg_putf(&out, "        }\n");
    sg_putf(&out, "    }\n");
    if (emit_main) {
        sg_putf(&out, "\n    static void Main() {\n");
        sg_putf(&out, "        %s.Run();\n", name);
        sg_putf(&out, "    }\n");
    }
    sg_putf(&out, "}\n");

    for (int i = 0; i < used.count; i++) free(used.names[i]);
    free(used.names);
    free(decls.buf);
    free(body.buf);
    zan_json_free(root);
    return out.buf;
}
