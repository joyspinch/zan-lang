/* gui_backend.h -- render backend seam for the GUI runtime.
 *
 * Every drawing primitive Zan can reach arrives through the ~45 zan_gui_*
 * exports in gui_runtime.c (stdlib/Gui/Render.zan holds their [DllImport]
 * declarations, and no widget ever touches pixels directly), so this is the one
 * place a second rasterizer has to be plugged in: a surface carries a backend
 * pointer, each export validates its arguments and then hands the call to that
 * backend. The exported ABI does not change, and neither does any Zan code.
 *
 * The built-in software rasterizer is itself a backend (`zan_cpu_backend`), and
 * it is what an entry a backend leaves NULL falls back to -- so a GPU backend
 * can implement the primitives it accelerates and inherit the rest. It stays
 * the permanent fallback for machines with no usable GPU context (remote
 * desktops, VMs, CI); a NULL backend pointer on a surface means the CPU path.
 *
 * Entries are the primitives, not the exports: the mask-less entry points
 * (fill_rounded_rect, fill_circle without a corner mask, ...) fold into their
 * masked form, so a backend implements ~20 entries rather than 45. Where two
 * exports differ in more than an argument -- an opaque vertical gradient vs. a
 * blended multi-stop one, a single anti-aliased line vs. a whole polyline in
 * one coverage pass -- they keep separate entries: collapsing them would make
 * the seam lossy, i.e. no backend could reproduce both.
 *
 * Which backend a surface gets is the *application's* choice (Gui.App exposes
 * it; ZanIDE persists it in its own profile). It is deliberately not an
 * environment variable: one machine-wide switch shared by every Zan program on
 * the box is not a setting.
 */

#ifndef ZAN_GUI_BACKEND_H
#define ZAN_GUI_BACKEND_H

#include <stdint.h>

struct zan_surface_s;

/* Corner-mask bits shared with Zan (Canvas.FillRoundRectIn): 1=TL 2=TR 4=BR
 * 8=BL, 15 = all four (what the non-mask entry points pass). */
#define ZAN_CORNERS_ALL 15

/* Gradient direction, as zan_gui_fill_grad_mask defines it. */
#define ZAN_GRAD_VERTICAL      0  /* top -> bottom */
#define ZAN_GRAD_HORIZONTAL    1  /* left -> right */
#define ZAN_GRAD_TO_BOTTOM_RIGHT 2
#define ZAN_GRAD_TO_BOTTOM_LEFT  3

/* --- text: cached coverage tiles and the run that positions them -----------
 *
 * Measuring and rasterizing glyphs stays platform code (GDI / FreeType /
 * CoreText); what it produces is cached as coverage tiles and handed to the
 * backend as a positioned run, so a backend never talks to a font engine.
 *
 * A tile is one glyph (FreeType, single-channel coverage) or one whole
 * laid-out run (GDI, whose ClearType coverage is per-channel and only valid
 * for the run it laid out -- splitting it into glyphs would change how text
 * looks). `id`/`rev` identify the tile's content while it stays cached, which
 * is what lets a GPU backend upload a tile once and reuse it for every later
 * frame that draws the same text. */
typedef struct zan_glyph_tile_s {
    uint32_t id;        /* 0 is never a valid tile */
    uint32_t rev;       /* bumped when the same id's pixels are re-rasterized */
    int w, h;
    /* Where the tile sits relative to the pen: `left` is the x offset from it,
     * `top` the rows the tile rises above the baseline (FreeType's bitmap_top).
     * A whole-run tile carries 0/0 and is placed at the draw position. */
    int left, top;
    int advance;        /* pen movement this tile consumes */
    int bpp;            /* 1 = coverage byte (R8), 4 = per-channel 0x00RRGGBB */
    const void *cov;    /* w*h samples, rows tightly packed */
} zan_glyph_tile;

/* Destination of one tile, in surface pixels (top-left, offsets applied). */
typedef struct {
    const zan_glyph_tile *tile;
    int x, y;
} zan_glyph_item;

typedef struct {
    uint32_t color;
    int count;
    const zan_glyph_item *items;
} zan_glyph_run;

typedef struct zan_gui_backend_s {
    const char *name;   /* "cpu", "gl", ... */

    /* --- solid geometry ------------------------------------------------- */
    void (*clear_rect)(struct zan_surface_s *s, int x, int y, int w, int h,
                       uint32_t color);
    void (*fill_rect)(struct zan_surface_s *s, int x, int y, int w, int h,
                      uint32_t color);
    /* radius <= 0 (or an empty corner mask) is a plain rectangle. */
    void (*fill_round)(struct zan_surface_s *s, int x, int y, int w, int h,
                       int radius, int corners, uint32_t color);
    /* Stroked inward by `thickness` device pixels, exactly as passed: a
     * thickness of 0 draws nothing (what zan_gui_draw_rect always did). */
    void (*draw_round)(struct zan_surface_s *s, int x, int y, int w, int h,
                       int radius, int corners, uint32_t color, int thickness);
    /* Opaque two-stop vertical gradient: rows are overwritten (alpha forced
     * opaque), which is what a wallpaper/backdrop fill needs. */
    void (*fill_vgrad)(struct zan_surface_s *s, int x, int y, int w, int h,
                       int radius, int corners, uint32_t top, uint32_t bottom);
    /* Blended linear gradient in direction `dir`, `via` an optional middle
     * stop (0 for none, grad_sample's convention); translucent stops
     * composite over the backdrop. */
    void (*fill_grad)(struct zan_surface_s *s, int x, int y, int w, int h,
                      int radius, int corners, int dir,
                      uint32_t from, uint32_t via, uint32_t to);
    void (*shadow_round)(struct zan_surface_s *s, int x, int y, int w, int h,
                         int radius, int blur, uint32_t color);
    void (*fill_circle)(struct zan_surface_s *s, int cx, int cy, int radius,
                        uint32_t color);
    void (*draw_circle)(struct zan_surface_s *s, int cx, int cy, int radius,
                        uint32_t color, int thickness);
    /* Soft radial glow: alpha fades from inner_alpha (0..255) at the centre to
     * 0 at `radius`; only the RGB of `color` is used. */
    void (*fill_radial)(struct zan_surface_s *s, int cx, int cy, int radius,
                        uint32_t color, int inner_alpha);
    /* Ring sector (pie / donut slice), degrees clockwise from 12 o'clock;
     * r_inner 0 gives a solid pie slice. */
    void (*fill_sector)(struct zan_surface_s *s, int cx, int cy,
                        int r_inner, int r_outer, int a0_deg, int a1_deg,
                        uint32_t color);
    /* One anti-aliased segment (Wu for hairlines, a round-capped capsule when
     * thick) -- not the polyline path below, whose single-pass max-coverage
     * rasterization produces different pixels. */
    void (*draw_line)(struct zan_surface_s *s, int x0, int y0, int x1, int y1,
                      uint32_t color, int thickness);
    /* Whole path in one coverage pass (round joins, each pixel blended once).
     * `fx` is the fixed-point shift of the vertex coordinates: 0 for integer
     * pixels, 8 for 16.8 sub-pixel vertices. */
    void (*polyline)(struct zan_surface_s *s, const int32_t *pts, int n,
                     uint32_t color, int thickness, int fx);

    /* --- surface-sampling primitives ------------------------------------ */
    /* Frosted glass. slot >= 0 reuses a cached blur (dirty == 0 means the
     * source is unchanged); corner_mask == 0 blurs the plain rectangle. */
    void (*blur)(struct zan_surface_s *s, int x, int y, int w, int h,
                 int radius, int slot, int dirty,
                 int corner_radius, int corner_mask);
    void (*snapshot)(struct zan_surface_s *s, int x, int y, int w, int h,
                     int slot);
    /* Returns 0 when the slot does not match this surface/geometry, in which
     * case the caller must repaint the region instead of trusting the slot. */
    int  (*restore)(struct zan_surface_s *s, int x, int y, int w, int h,
                    int slot, int sub_rect);

    /* --- text and images ------------------------------------------------ */
    /* Whole text string, font engine included: for a backend that does its own
     * shaping/rasterization. Left NULL, the export runs the platform font code
     * and hands the result to glyph_run below, which is the seam a GPU backend
     * normally wants. */
    void (*draw_text)(struct zan_surface_s *s, int x, int y, const char *text,
                      uint32_t color, int font_size);
    /* One text run as positioned coverage tiles: the CPU backend composites
     * them; a GPU backend uploads the tiles whose (id, rev) it has not seen
     * into its atlas texture and draws the run in one call. */
    void (*glyph_run)(struct zan_surface_s *s, const zan_glyph_run *run);
    /* Source w/h <= 0 means the whole image. */
    void (*blit_image)(struct zan_surface_s *s, const char *path,
                       int dx, int dy, int dw, int dh,
                       int sx, int sy, int sw, int sh);

    /* --- state and frame boundaries ------------------------------------- */
    void (*set_clip)(struct zan_surface_s *s, int x0, int y0, int x1, int y1);
    /* End of frame: a GPU backend submits its batches here. The CPU path has
     * nothing to do, since it wrote straight into the surface. */
    void (*flush)(struct zan_surface_s *s);
    /* Pull the composed frame back into `s->pixels` (stride in pixels) for
     * presents that need a CPU bitmap: UpdateLayeredWindow, screenshots,
     * pixel-compare tests. */
    void (*read_pixels)(struct zan_surface_s *s);
    /* Put the composed frame on screen without going through `s->pixels`:
     * `native_window` is the shell's window handle (HWND / X11 Window). Returns
     * 1 when the frame is on screen and the shell must not blit its bitmap, 0
     * when this backend cannot present to that window -- the shell then blits
     * as it always has, which is also what the CPU backend (present == NULL)
     * makes every shell do. */
    int  (*present)(struct zan_surface_s *s, void *native_window);
    /* The shell's window is going away: drop whatever the backend attached to
     * it (the GL child window), so nothing outlives the handle. */
    void (*drop_window)(void *native_window);
    /* The surface is being freed: drop whatever the backend holds for it (the
     * GPU texture and framebuffer). Surface ids are recycled, so a backend that
     * keyed anything on the id must not carry it into the next surface. */
    void (*drop_surface)(struct zan_surface_s *s);

    /* --- keeping the two sides in step ---------------------------------- */
    /* A backend that keeps the frame somewhere other than s->pixels (a GPU
     * framebuffer) has to move it across whenever the other side is about to
     * touch it: the seam calls sync_to_cpu before running an entry this backend
     * left NULL (the CPU code draws straight into s->pixels), and sync_from_cpu
     * before an entry it does implement. Both NULL for the CPU backend, which
     * is the only side there is. That is what lets a GPU backend ship one
     * primitive at a time instead of all ~20 at once. */
    void (*sync_to_cpu)(struct zan_surface_s *s);
    void (*sync_from_cpu)(struct zan_surface_s *s);
} zan_gui_backend;

/* The built-in software rasterizer: default backend of every surface and the
 * per-entry fallback for backends that implement only part of the vtable. */
extern const zan_gui_backend zan_cpu_backend;

#endif /* ZAN_GUI_BACKEND_H */
