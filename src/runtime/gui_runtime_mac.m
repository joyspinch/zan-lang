/*
 * zan_gui runtime — macOS Cocoa backend.
 *
 * Provides the native windowing / event / present / window-management ABI for
 * macOS. The shared software renderer and bitmap-font text rendering live in
 * gui_runtime.c (compiled with -DZAN_GUI_COCOA so its macOS stub branch is
 * disabled). present() reads the current frame's ARGB surface through the
 * internal accessor zan_gui_internal_surface_data() and blits it via CoreGraphics.
 *
 * Built only on Apple targets; requires the Cocoa framework.
 */
#import <Cocoa/Cocoa.h>
#import <CoreText/CoreText.h>
#import <WebKit/WebKit.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <unistd.h>

typedef int64_t i64;
typedef uint32_t u32;

/* Implemented in gui_runtime.c; not part of the [DllImport] ABI. */
extern u32 *zan_gui_internal_surface_data(i64 id, int *w, int *h, int *stride);

/* Defined below; used by the client-side title-bar metrics helpers. */
i64 zan_gui_get_dpi_scale(void);

#define EXPORT __attribute__((visibility("default")))

/* ---- per-window registry ----
 * One process can drive several top-level windows. Each keeps its own view
 * (which owns its last-presented image) and size. g_mwins[0] is the primary
 * window used for the handle-less size queries. */
#define ZAN_MAX_WINDOWS 16
typedef struct {
    NSWindow *window;
    NSView   *view;
    int w, h;
} zan_mwin_t;
static zan_mwin_t g_mwins[ZAN_MAX_WINDOWS];
static int g_mwin_count = 0;

/* Pending-event slots: [kind, x, y, button, keycode, mods, hwnd]. */
static long g_evt[7];
/* Handle of the window feeding the events currently being decoded. */
static long g_evt_win = 0;

static zan_mwin_t *mwin_find_win(NSWindow *w) {
    for (int i = 0; i < g_mwin_count; i++)
        if (g_mwins[i].window == w) return &g_mwins[i];
    return NULL;
}
static zan_mwin_t *mwin_find(long hwnd) {
    return mwin_find_win((NSWindow *)(intptr_t)hwnd);
}

/* Decoded events are queued so a single NSEvent can yield several ABI events
 * (e.g. a key press emits a keyDown followed by a textInput, and a window
 * resize/close arrives from the delegate), matching the Win32 backend where
 * WM_KEYDOWN and WM_CHAR are distinct messages. Each entry also carries the
 * source window handle so the app can route it. */
#define ZAN_EVQ_CAP 64
static long g_evq[ZAN_EVQ_CAP][7];
static int g_evq_head = 0;
static int g_evq_tail = 0;

static void evq_push(long kind, long x, long y, long button, long keycode, long mods) {
    int next = (g_evq_tail + 1) % ZAN_EVQ_CAP;
    if (next == g_evq_head) return; /* queue full: drop oldest-behavior avoided */
    g_evq[g_evq_tail][0] = kind;
    g_evq[g_evq_tail][1] = x;
    g_evq[g_evq_tail][2] = y;
    g_evq[g_evq_tail][3] = button;
    g_evq[g_evq_tail][4] = keycode;
    g_evq[g_evq_tail][5] = mods;
    g_evq[g_evq_tail][6] = g_evt_win;
    g_evq_tail = next;
}

static int evq_pop(void) {
    if (g_evq_head == g_evq_tail) return 0;
    memcpy(g_evt, g_evq[g_evq_head], sizeof(g_evt));
    g_evq_head = (g_evq_head + 1) % ZAN_EVQ_CAP;
    return 1;
}

/* Custom content view that blits its own last-presented CGImage, so each
 * window renders independently. */
@interface ZanView : NSView {
@public
    CGImageRef frame;
}
@end

@implementation ZanView
- (BOOL)isFlipped { return YES; }          /* top-left origin, matches surface */
- (BOOL)acceptsFirstResponder { return YES; }
/* The window is borderless and draws its own title bar, so no part of the
 * content view should trigger the system "drag anywhere" behavior; window
 * dragging is initiated explicitly from the caption region (see decode_event). */
- (BOOL)mouseDownCanMoveWindow { return NO; }
- (void)drawRect:(NSRect)dirtyRect {
    (void)dirtyRect;
    if (!frame) return;
    CGContextRef ctx = (CGContextRef)[[NSGraphicsContext currentContext] CGContext];
    if (!ctx) return;
    /* The surface uses a top-left origin (row 0 = top), but Core Graphics blits
     * images bottom-up; flip the CTM so row 0 lands at the top of this flipped
     * view instead of the bottom. */
    CGContextSaveGState(ctx);
    CGContextTranslateCTM(ctx, 0, self.bounds.size.height);
    CGContextScaleCTM(ctx, 1.0, -1.0);
    CGContextDrawImage(ctx, self.bounds, frame);
    CGContextRestoreGState(ctx);
}
@end

/* Borderless windows are not key/main by default; allow both so the app still
 * receives keyboard focus and looks active without a native title bar. */
@interface ZanWindow : NSWindow
@end
@implementation ZanWindow
- (BOOL)canBecomeKeyWindow { return YES; }
- (BOOL)canBecomeMainWindow { return YES; }
@end

/* Client-side title-bar metrics (borderless window). The app draws its own
 * caption; these mirror the X11 backend so the draggable caption region and
 * the (non-draggable) caption-button cluster line up with what Zan paints. */
static int g_caption_btn_count = 5;
static int zan_titlebar_h(void) { return (int)(32 * zan_gui_get_dpi_scale() / 100); }
static int zan_caption_btn_w(void) { return (int)(46 * zan_gui_get_dpi_scale() / 100); }

/* Window delegate: surfaces resize (kind 7) and close (kind 8) events, which
 * do not flow through -nextEventMatchingMask:, so the app can relayout and
 * quit exactly as it does on the Win32/X11 backends. */
@interface ZanDelegate : NSObject <NSWindowDelegate>
@end

@implementation ZanDelegate
- (void)windowDidResize:(NSNotification *)note {
    NSWindow *win = [note object];
    zan_mwin_t *mw = mwin_find_win(win);
    if (mw && mw->view) {
        NSSize s = [mw->view bounds].size;
        mw->w = (int)s.width;
        mw->h = (int)s.height;
        g_evt_win = (long)(intptr_t)win;
        evq_push(7, (long)s.width, (long)s.height, 0, 0, 0);
    }
}
- (BOOL)windowShouldClose:(NSWindow *)sender {
    g_evt_win = (long)(intptr_t)sender;
    evq_push(8, 0, 0, 0, 0, 0);
    return NO; /* let the app decide when to quit rather than tearing down */
}
@end

static ZanDelegate *g_delegate = nil;

/* ---- window lifecycle ---- */

EXPORT i64 zan_gui_create_window(const char *title, i64 width, i64 height) {
    if (g_mwin_count >= ZAN_MAX_WINDOWS) return 0;
    @autoreleasepool {
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
        if (!g_delegate) g_delegate = [[ZanDelegate alloc] init];

        NSRect rect = NSMakeRect(0, 0, (CGFloat)width, (CGFloat)height);
        /* Borderless: the app paints its own title bar/caption buttons, matching
         * the Win32 (WS_POPUP) and X11 (override caption) backends. Keep the
         * resizable/miniaturizable capabilities so edge-resize still works. */
        NSUInteger style = NSWindowStyleMaskBorderless |
                           NSWindowStyleMaskMiniaturizable |
                           NSWindowStyleMaskResizable;
        NSWindow *window = [[ZanWindow alloc] initWithContentRect:rect
                                               styleMask:style
                                                 backing:NSBackingStoreBuffered
                                                   defer:NO];
        ZanView *view = [[ZanView alloc] initWithFrame:rect];
        view->frame = NULL;
        [window setContentView:view];
        [window setAcceptsMouseMovedEvents:YES]; /* deliver mouseMoved for hover */
        [window setDelegate:g_delegate];          /* one delegate serves all windows */
        [window setMovableByWindowBackground:NO]; /* dragging is caption-driven */
        if (title) {
            [window setTitle:[NSString stringWithUTF8String:title]];
        }
        if (g_mwin_count > 0) {
            NSRect parent = [g_mwins[0].window frame];
            NSRect child = [window frame];
            NSPoint origin = NSMakePoint(
                NSMidX(parent) - child.size.width / 2.0,
                NSMidY(parent) - child.size.height / 2.0);
            NSScreen *screen = [g_mwins[0].window screen];
            if (screen) {
                NSRect work = [screen visibleFrame];
                if (origin.x < NSMinX(work)) origin.x = NSMinX(work);
                if (origin.y < NSMinY(work)) origin.y = NSMinY(work);
                if (origin.x + child.size.width > NSMaxX(work))
                    origin.x = NSMaxX(work) - child.size.width;
                if (origin.y + child.size.height > NSMaxY(work))
                    origin.y = NSMaxY(work) - child.size.height;
            }
            [window setFrameOrigin:origin];
        } else {
            [window center];
        }

        zan_mwin_t *mw = &g_mwins[g_mwin_count++];
        mw->window = window;
        mw->view = view;
        mw->w = (int)width;
        mw->h = (int)height;
        return (i64)(intptr_t)window;
    }
}

EXPORT i64 zan_gui_show_window(i64 hwnd_val) {
    zan_mwin_t *mw = mwin_find(hwnd_val);
    if (!mw && g_mwin_count > 0) mw = &g_mwins[0];
    if (!mw) return 1;
    @autoreleasepool {
        [NSApp activateIgnoringOtherApps:YES];
        [mw->window makeKeyAndOrderFront:nil];
    }
    return 0;
}

EXPORT i64 zan_gui_close_window(i64 hwnd_val) {
    zan_mwin_t *mw = mwin_find(hwnd_val);
    if (!mw && g_mwin_count == 1) mw = &g_mwins[0];
    if (!mw) return 0;
    @autoreleasepool {
        ZanView *view = (ZanView *)mw->view;
        if (view && view->frame) { CGImageRelease(view->frame); view->frame = NULL; }
        [mw->window setDelegate:nil];
        [mw->window close];
    }
    int idx = (int)(mw - g_mwins);
    g_mwins[idx] = g_mwins[--g_mwin_count];
    return 0;
}

/* ---- event pump ---- */

/* Map Cocoa modifier flags to the Win32 encoding the cross-platform widgets
 * expect: bit0=Ctrl, bit1=Shift, bit2=Alt. macOS's Command is folded into the
 * Ctrl bit as well so Cmd-based shortcuts (Cmd+C/V/X/Z/A/S) exercise the same
 * logic the widgets were written against for Windows' Ctrl shortcuts. */
static long zan_mods(NSUInteger f) {
    long mods = 0;
    if (f & (NSEventModifierFlagControl | NSEventModifierFlagCommand)) mods |= 1;
    if (f & NSEventModifierFlagShift)  mods |= 2;
    if (f & NSEventModifierFlagOption) mods |= 4;
    return mods;
}

/* Translate a Cocoa virtual key code to the Windows VK code the Zan `Keys`
 * constants use, so keyboard handling is identical across backends. Returns 0
 * for keys resolved from their character below (letters/digits). */
static long zan_vk_from_keycode(unsigned short kc) {
    switch (kc) {
    case 53:  return 27;  /* Escape */
    case 36:  return 13;  /* Return */
    case 76:  return 13;  /* keypad Enter */
    case 48:  return 9;   /* Tab */
    case 51:  return 8;   /* Backspace (Delete key) */
    case 117: return 46;  /* Forward Delete */
    case 123: return 37;  /* Left */
    case 126: return 38;  /* Up */
    case 124: return 39;  /* Right */
    case 125: return 40;  /* Down */
    case 115: return 36;  /* Home */
    case 119: return 35;  /* End */
    case 116: return 33;  /* PageUp */
    case 121: return 34;  /* PageDown */
    case 49:  return 32;  /* Space */
    case 122: return 112; /* F1 */
    case 120: return 113; /* F2 */
    case 96:  return 116; /* F5 */
    case 103: return 122; /* F11 */
    default:  return 0;
    }
}

/* Resolve an alphanumeric VK code from a key event's layout-independent
 * character (VK_A..VK_Z == 'A'..'Z', VK_0..VK_9 == '0'..'9'). */
static long zan_vk_from_chars(NSString *bare) {
    if ([bare length] == 0) return 0;
    unichar u = [bare characterAtIndex:0];
    if (u >= 'a' && u <= 'z') return (long)(u - 'a' + 'A');
    if ((u >= 'A' && u <= 'Z') || (u >= '0' && u <= '9')) return (long)u;
    return 0;
}

static void decode_event(NSEvent *ev) {
    NSEventType type = [ev type];
    long mods = zan_mods([ev modifierFlags]);
    NSWindow *win = [ev window];
    zan_mwin_t *mw = win ? mwin_find_win(win) : NULL;
    NSView *view = mw ? mw->view : NULL;
    g_evt_win = (long)(intptr_t)win;
    NSPoint p = [ev locationInWindow];
    NSPoint local = view ? [view convertPoint:p fromView:nil] : p;
    long lx = (long)local.x, ly = (long)local.y;

    switch (type) {
    case NSEventTypeApplicationDefined:
        /* A wake posted from a background thread or a WebKit navigation
         * callback: deliver an empty (kind 0) event so pump() returns and the
         * app runs DrainPosts()/repaints (matches Win32/X11 Wake semantics). */
        evq_push(0, 0, 0, 0, 0, 0);
        break;
    case NSEventTypeMouseMoved:
    case NSEventTypeLeftMouseDragged:
    case NSEventTypeRightMouseDragged:
    case NSEventTypeOtherMouseDragged:
        evq_push(1, lx, ly, 0, 0, mods);
        break;
    case NSEventTypeLeftMouseDown: {
        /* Caption drag: mirror the Win32/X11 WM_NCHITTEST logic. A press in the
         * title-bar strip that is left of the caption-button cluster drags the
         * window natively and is not delivered as a client click; presses over
         * the buttons or below the caption fall through as ordinary clicks. */
        int capW = g_caption_btn_count * zan_caption_btn_w();
        if (mw && ly >= 0 && ly < zan_titlebar_h() && lx < mw->w - capW) {
            [win performWindowDragWithEvent:ev];
            break;
        }
        evq_push(2, lx, ly, 0, 0, mods);
        break;
    }
    case NSEventTypeRightMouseDown: evq_push(2, lx, ly, 1, 0, mods); break;
    case NSEventTypeOtherMouseDown: evq_push(2, lx, ly, 2, 0, mods); break;
    case NSEventTypeLeftMouseUp:    evq_push(3, lx, ly, 0, 0, mods); break;
    case NSEventTypeRightMouseUp:   evq_push(3, lx, ly, 1, 0, mods); break;
    case NSEventTypeOtherMouseUp:   evq_push(3, lx, ly, 2, 0, mods); break;
    case NSEventTypeScrollWheel: {
        double dy = [ev scrollingDeltaY];
        if ([ev hasPreciseScrollingDeltas]) dy /= 10.0;
        long delta = (long)(dy * 120.0); /* one wheel notch == 120, like Win32 */
        if (delta == 0 && dy != 0.0) delta = dy > 0 ? 120 : -120;
        if (delta != 0) evq_push(13, lx, ly, 0, delta, mods);
        break;
    }
    case NSEventTypeKeyDown: {
        long vk = zan_vk_from_keycode([ev keyCode]);
        if (vk == 0) vk = zan_vk_from_chars([ev charactersIgnoringModifiers]);
        evq_push(4, 0, 0, 0, vk, mods);
        /* Emit a WM_CHAR-style text event so widgets receive the same integer
         * codes as on Win32 (typing, Backspace/Tab/Enter, and Ctrl/Cmd
         * shortcuts as ASCII control codes). */
        if (mods & 1) {
            /* Ctrl or Cmd held: deliver a control code (Ctrl+A..Z => 1..26,
             * Ctrl+/ => 31) since Cocoa's -characters does not fold these. */
            NSString *ci = [ev charactersIgnoringModifiers];
            if ([ci length] == 1) {
                unichar u = [ci characterAtIndex:0];
                long code = 0;
                if (u >= 'a' && u <= 'z') code = u - 'a' + 1;
                else if (u >= 'A' && u <= 'Z') code = u - 'A' + 1;
                else if (u == '/') code = 31;
                if (code) evq_push(6, 0, 0, 0, code, mods);
            }
        } else if (vk == 8) {
            /* Backspace: Cocoa reports 0x7F in -characters; deliver 8 like Win32. */
            evq_push(6, 0, 0, 0, 8, mods);
        } else {
            NSString *chars = [ev characters];
            for (NSUInteger i = 0; i < [chars length]; i++) {
                unichar u = [chars characterAtIndex:i];
                /* printable text plus Tab(9)/Enter(13); skip DEL and the
                 * 0xF700-0xF8FF private range Cocoa uses for function keys. */
                if (u < 0xF700 && ((u >= 32 && u != 127) || u == 9 || u == 13))
                    evq_push(6, 0, 0, 0, (long)u, mods);
            }
        }
        break;
    }
    case NSEventTypeKeyUp: {
        long vk = zan_vk_from_keycode([ev keyCode]);
        if (vk == 0) vk = zan_vk_from_chars([ev charactersIgnoringModifiers]);
        evq_push(5, 0, 0, 0, vk, mods);
        break;
    }
    default:
        break;
    }
}

static i64 pump(bool wait) {
    if (g_mwin_count == 0) return -1;
    if (evq_pop()) return 0;               /* drain already-decoded events first */
    memset(g_evt, 0, sizeof(g_evt));
    @autoreleasepool {
        for (;;) {
            NSDate *until = wait ? [NSDate distantFuture] : [NSDate distantPast];
            NSEvent *ev = [NSApp nextEventMatchingMask:NSEventMaskAny
                                             untilDate:until
                                                inMode:NSDefaultRunLoopMode
                                               dequeue:YES];
            if (!ev) return 1;             /* nothing pending (poll) */
            decode_event(ev);
            [NSApp sendEvent:ev];          /* keep the window responsive */
            if (evq_pop()) return 0;
            if (!wait) return 1;           /* polled event produced nothing decodable */
            /* wait: keep pumping until an event decodes into something */
        }
    }
}

EXPORT i64 zan_gui_wait_event(void) { return pump(true); }
EXPORT i64 zan_gui_poll_event(void) { return pump(false); }

/* Wake a UI thread blocked in pump() from another thread so it can drain the
 * dispatch queue. Posting an application-defined NSEvent is thread-safe. */
EXPORT i64 zan_gui_wake(void) {
    @autoreleasepool {
        NSEvent *ev = [NSEvent otherEventWithType:NSEventTypeApplicationDefined
                                         location:NSMakePoint(0, 0)
                                    modifierFlags:0
                                        timestamp:0
                                     windowNumber:0
                                          context:nil
                                          subtype:0
                                            data1:0
                                            data2:0];
        [NSApp postEvent:ev atStart:NO];
    }
    return 0;
}

EXPORT i64 zan_gui_event_kind(void)    { return g_evt[0]; }
EXPORT i64 zan_gui_event_x(void)       { return g_evt[1]; }
EXPORT i64 zan_gui_event_y(void)       { return g_evt[2]; }
EXPORT i64 zan_gui_event_button(void)  { return g_evt[3]; }
EXPORT i64 zan_gui_event_keycode(void) { return g_evt[4]; }
EXPORT i64 zan_gui_event_mods(void)    { return g_evt[5]; }
EXPORT i64 zan_gui_event_hwnd(void)    { return (i64)g_evt[6]; }

/* ---- native text rendering ---- */

static CTFontRef zan_font(i64 font_size) {
    CGFloat size = (CGFloat)font_size;
    if (size < 8.0) size = 8.0;
    return CTFontCreateWithName(CFSTR(".AppleSystemUIFont"), size, NULL);
}

static CTLineRef zan_text_line(const char *utf8, CTFontRef font) {
    if (!utf8 || !*utf8 || !font) return NULL;
    CFStringRef text = CFStringCreateWithCString(
        kCFAllocatorDefault, utf8, kCFStringEncodingUTF8);
    if (!text) return NULL;
    CGColorRef white = CGColorCreateGenericGray(1.0, 1.0);
    const void *keys[] = {
        kCTFontAttributeName,
        kCTForegroundColorAttributeName
    };
    const void *values[] = { font, white };
    CFDictionaryRef attrs = CFDictionaryCreate(
        kCFAllocatorDefault, keys, values, 2,
        &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFAttributedStringRef attributed = CFAttributedStringCreate(
        kCFAllocatorDefault, text, attrs);
    CTLineRef line = CTLineCreateWithAttributedString(attributed);
    CFRelease(attributed);
    CFRelease(attrs);
    CGColorRelease(white);
    CFRelease(text);
    return line;
}

static u32 zan_blend(u32 dst, u32 color, unsigned int coverage) {
    u32 ca = (color >> 24) & 0xFF;
    if (ca == 0) ca = 255;
    u32 alpha = ca * coverage / 255;
    u32 inv = 255 - alpha;
    u32 sr = (color >> 16) & 0xFF;
    u32 sg = (color >> 8) & 0xFF;
    u32 sb = color & 0xFF;
    u32 dr = (dst >> 16) & 0xFF;
    u32 dg = (dst >> 8) & 0xFF;
    u32 db = dst & 0xFF;
    u32 r = (sr * alpha + dr * inv) / 255;
    u32 g = (sg * alpha + dg * inv) / 255;
    u32 b = (sb * alpha + db * inv) / 255;
    return 0xFF000000u | (r << 16) | (g << 8) | b;
}

EXPORT void zan_gui_draw_text(i64 surface_id, i64 x, i64 y,
                              const char *utf8, i64 color, i64 font_size) {
    int sw = 0, sh = 0, stride = 0;
    u32 *pixels = zan_gui_internal_surface_data(surface_id, &sw, &sh, &stride);
    if (!pixels || !utf8 || !*utf8) return;

    @autoreleasepool {
        CTFontRef font = zan_font(font_size);
        CTLineRef line = zan_text_line(utf8, font);
        if (!line) {
            if (font) CFRelease(font);
            return;
        }
        CGFloat ascent = 0, descent = 0, leading = 0;
        double measured =
            CTLineGetTypographicBounds(line, &ascent, &descent, &leading);
        int tw = (int)ceil(measured) + 2;
        int th = (int)ceil(ascent + descent + leading) + 2;
        if (tw > 0 && th > 0) {
            unsigned char *mask =
                (unsigned char *)calloc((size_t)tw * (size_t)th, 1);
            CGColorSpaceRef cs = CGColorSpaceCreateDeviceGray();
            CGContextRef ctx = CGBitmapContextCreate(
                mask, (size_t)tw, (size_t)th, 8, (size_t)tw,
                cs, (CGBitmapInfo)kCGImageAlphaNone);
            if (ctx) {
                CGContextSetGrayFillColor(ctx, 1.0, 1.0);
                CGContextSetTextPosition(ctx, 1.0, descent + 1.0);
                CTLineDraw(line, ctx);
                CGContextFlush(ctx);
                for (int py = 0; py < th; py++) {
                    int dy = (int)y + py;
                    if (dy < 0 || dy >= sh) continue;
                    int sy = py;
                    for (int px = 0; px < tw; px++) {
                        int dx = (int)x + px;
                        if (dx < 0 || dx >= sw) continue;
                        unsigned int coverage = mask[sy * tw + px];
                        if (!coverage) continue;
                        int index = dy * stride + dx;
                        pixels[index] =
                            zan_blend(pixels[index], (u32)color, coverage);
                    }
                }
                CGContextRelease(ctx);
            }
            CGColorSpaceRelease(cs);
            free(mask);
        }
        CFRelease(line);
        CFRelease(font);
    }
}

EXPORT i64 zan_gui_measure_text(const char *utf8, i64 font_size) {
    if (!utf8 || !*utf8) return 0;
    @autoreleasepool {
        CTFontRef font = zan_font(font_size);
        CTLineRef line = zan_text_line(utf8, font);
        if (!line) {
            if (font) CFRelease(font);
            return 0;
        }
        double width = CTLineGetTypographicBounds(line, NULL, NULL, NULL);
        CFRelease(line);
        CFRelease(font);
        return (i64)ceil(width);
    }
}

EXPORT i64 zan_gui_font_height(i64 font_size) {
    CTFontRef font = zan_font(font_size);
    if (!font) return 0;
    CGFloat height = CTFontGetAscent(font) + CTFontGetDescent(font) +
                     CTFontGetLeading(font);
    CFRelease(font);
    return (i64)ceil(height);
}

/* ---- geometry ---- */

EXPORT i64 zan_gui_window_width(void) {
    if (g_mwin_count == 0 || !g_mwins[0].view) return 0;
    return (i64)[g_mwins[0].view bounds].size.width;
}
EXPORT i64 zan_gui_window_height(void) {
    if (g_mwin_count == 0 || !g_mwins[0].view) return 0;
    return (i64)[g_mwins[0].view bounds].size.height;
}
EXPORT i64 zan_gui_client_width(i64 hwnd_val) {
    zan_mwin_t *mw = mwin_find(hwnd_val);
    if (!mw || !mw->view) return zan_gui_window_width();
    return (i64)[mw->view bounds].size.width;
}
EXPORT i64 zan_gui_client_height(i64 hwnd_val) {
    zan_mwin_t *mw = mwin_find(hwnd_val);
    if (!mw || !mw->view) return zan_gui_window_height();
    return (i64)[mw->view bounds].size.height;
}

/* ---- client-side title-bar metrics (borderless window) ---- */

EXPORT i64 zan_gui_titlebar_height(void) { return (i64)zan_titlebar_h(); }
EXPORT i64 zan_gui_caption_button_width(void) { return (i64)zan_caption_btn_w(); }
EXPORT i64 zan_gui_set_caption_buttons(i64 count) {
    if (count >= 0 && count <= 8) g_caption_btn_count = (int)count;
    return 0;
}

/* ---- present: blit the software surface to the window ---- */

EXPORT i64 zan_gui_present(i64 hwnd_val, i64 surface_id) {
    zan_mwin_t *mw = mwin_find(hwnd_val);
    if (!mw && g_mwin_count > 0) mw = &g_mwins[0];
    if (!mw || !mw->view) return 1;
    ZanView *view = (ZanView *)mw->view;
    int w = 0, h = 0, stride = 0;
    const u32 *pixels = zan_gui_internal_surface_data(surface_id, &w, &h, &stride);
    if (!pixels || w <= 0 || h <= 0) return 1;

    @autoreleasepool {
        CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
        /* Surface pixels are 0xAARRGGBB in memory (little-endian bytes B,G,R,A);
         * interpret as premultiplied ARGB via 32-little byte order. */
        CGBitmapInfo info = kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little;
        CGContextRef bmp = CGBitmapContextCreate((void *)pixels, (size_t)w, (size_t)h,
                                                 8, (size_t)stride * 4, cs, info);
        CGImageRef img = bmp ? CGBitmapContextCreateImage(bmp) : NULL;
        if (img) {
            if (view->frame) CGImageRelease(view->frame);
            view->frame = img;             /* retained; released on next present */
            [view setNeedsDisplay:YES];
            [view displayIfNeeded];
        }
        if (bmp) CGContextRelease(bmp);
        CGColorSpaceRelease(cs);
    }
    return 0;
}

/* ---- misc ---- */

EXPORT i64 zan_gui_set_title(i64 hwnd_val, const char *title) {
    zan_mwin_t *mw = mwin_find(hwnd_val);
    if (!mw && g_mwin_count > 0) mw = &g_mwins[0];
    if (mw && title) {
        @autoreleasepool { [mw->window setTitle:[NSString stringWithUTF8String:title]]; }
    }
    return 0;
}

EXPORT i64 zan_gui_set_cursor(i64 cursor) {
    @autoreleasepool {
        switch (cursor) {
        case 1:  [[NSCursor pointingHandCursor] set]; break;
        case 2:  [[NSCursor IBeamCursor] set]; break;
        case 3:  [[NSCursor resizeLeftRightCursor] set]; break;
        case 4:  [[NSCursor resizeUpDownCursor] set]; break;
        default: [[NSCursor arrowCursor] set]; break;
        }
    }
    return 0;
}

EXPORT i64 zan_gui_get_tick_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (i64)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

EXPORT void zan_gui_sleep_ms(i64 ms) {
    if (ms > 0) usleep((useconds_t)(ms * 1000));
}

/* ---- window management ---- */

EXPORT i64 zan_gui_minimize(i64 hwnd_val) {
    zan_mwin_t *mw = mwin_find(hwnd_val);
    if (!mw && g_mwin_count > 0) mw = &g_mwins[0];
    if (mw) { @autoreleasepool { [mw->window miniaturize:nil]; } }
    return 0;
}

EXPORT i64 zan_gui_toggle_maximize(i64 hwnd_val) {
    zan_mwin_t *mw = mwin_find(hwnd_val);
    if (!mw && g_mwin_count > 0) mw = &g_mwins[0];
    if (mw) { @autoreleasepool { [mw->window zoom:nil]; } }
    return 0;
}

EXPORT i64 zan_gui_is_maximized(i64 hwnd_val) {
    zan_mwin_t *mw = mwin_find(hwnd_val);
    if (!mw && g_mwin_count > 0) mw = &g_mwins[0];
    if (!mw) return 0;
    return [mw->window isZoomed] ? 1 : 0;
}

EXPORT i64 zan_gui_set_topmost(i64 hwnd_val, i64 on) {
    zan_mwin_t *mw = mwin_find(hwnd_val);
    if (!mw && g_mwin_count > 0) mw = &g_mwins[0];
    if (mw) {
        [mw->window setLevel:(on ? NSFloatingWindowLevel : NSNormalWindowLevel)];
    }
    return 0;
}

EXPORT i64 zan_gui_get_dpi_scale(void) {
    @autoreleasepool {
        NSWindow *w = g_mwin_count > 0 ? g_mwins[0].window : nil;
        CGFloat scale = w ? [w backingScaleFactor]
                          : [[NSScreen mainScreen] backingScaleFactor];
        if (scale <= 0) scale = 1.0;
        return (i64)(scale * 100.0 + 0.5);
    }
}

/* Report the caret position (client-area px) so an IME's composition/candidate
 * windows can follow the cursor. Stored for parity with the Win32/X11 backends;
 * macOS text entry currently flows through the shared software path, so this is
 * advisory and does not reposition a live composition session. */
static int g_ime_x = 0;
static int g_ime_y = 0;
EXPORT void zan_gui_set_ime_pos(i64 x, i64 y) {
    g_ime_x = (int)x;
    g_ime_y = (int)y;
}

/* ========================================================================
 * Embedded WebView (WKWebView).
 *
 * Each web view is a WKWebView added as a subview of a window's (flipped)
 * content view, so its frame uses the same top-left client coordinates the
 * software renderer draws in. A per-view navigation delegate tracks the last
 * request URL, the last HTTP response status, and bumps a navigation counter
 * on each finished/failed load so the Zan widget can react by polling. Cookie
 * access and JavaScript evaluation use WebKit's asynchronous APIs; because the
 * Zan event loop calls these from the main thread we drive them to completion
 * by spinning the run loop (with a timeout) rather than blocking on a
 * semaphore, which would deadlock the main queue.
 * ======================================================================== */
#define ZAN_MAX_WEBVIEWS 32

@class ZanWebViewDelegate;

typedef struct {
    int used;
    WKWebView *wv;
    ZanWebViewDelegate *delegate;
    NSWindow *ownerWindow;
    long navSeq;      /* incremented on each finished/failed navigation */
    int lastStatus;   /* last HTTP response status code (0 if none) */
    char *urlBuf;
    char *titleBuf;
    char *lastReqBuf;
    char *evalBuf;
    char *cookieBuf;
} zan_webview_t;

static zan_webview_t g_webviews[ZAN_MAX_WEBVIEWS];

@interface ZanWebViewDelegate : NSObject <WKNavigationDelegate>
@property (nonatomic) int slot;
@end

@implementation ZanWebViewDelegate
- (void)webView:(WKWebView *)wv
    decidePolicyForNavigationAction:(WKNavigationAction *)action
                    decisionHandler:(void (^)(WKNavigationActionPolicy))decisionHandler {
    (void)wv;
    NSURL *u = action.request.URL;
    if (u && self.slot >= 0 && self.slot < ZAN_MAX_WEBVIEWS) {
        zan_webview_t *w = &g_webviews[self.slot];
        const char *s = [[u absoluteString] UTF8String];
        char *nb = strdup(s ? s : "");
        if (nb) { free(w->lastReqBuf); w->lastReqBuf = nb; }
    }
    decisionHandler(WKNavigationActionPolicyAllow);
}
- (void)webView:(WKWebView *)wv
    decidePolicyForNavigationResponse:(WKNavigationResponse *)response
                      decisionHandler:(void (^)(WKNavigationResponsePolicy))decisionHandler {
    (void)wv;
    if (self.slot >= 0 && self.slot < ZAN_MAX_WEBVIEWS &&
        [response.response isKindOfClass:[NSHTTPURLResponse class]]) {
        NSHTTPURLResponse *http = (NSHTTPURLResponse *)response.response;
        g_webviews[self.slot].lastStatus = (int)http.statusCode;
    }
    decisionHandler(WKNavigationResponsePolicyAllow);
}
- (void)webView:(WKWebView *)wv
    didStartProvisionalNavigation:(WKNavigation *)navigation {
    (void)wv; (void)navigation;
    /* Loading began: bump so the app repaints (URL/loading state changed). */
    if (self.slot >= 0 && self.slot < ZAN_MAX_WEBVIEWS) g_webviews[self.slot].navSeq++;
    zan_gui_wake();
}
- (void)webView:(WKWebView *)wv didCommitNavigation:(WKNavigation *)navigation {
    (void)wv; (void)navigation;
    if (self.slot >= 0 && self.slot < ZAN_MAX_WEBVIEWS) g_webviews[self.slot].navSeq++;
    zan_gui_wake();
}
- (void)webView:(WKWebView *)wv didFinishNavigation:(WKNavigation *)navigation {
    (void)wv; (void)navigation;
    if (self.slot >= 0 && self.slot < ZAN_MAX_WEBVIEWS) g_webviews[self.slot].navSeq++;
    zan_gui_wake();
}
- (void)webView:(WKWebView *)wv
    didFailNavigation:(WKNavigation *)navigation withError:(NSError *)error {
    (void)wv; (void)navigation; (void)error;
    if (self.slot >= 0 && self.slot < ZAN_MAX_WEBVIEWS) g_webviews[self.slot].navSeq++;
    zan_gui_wake();
}
- (void)webView:(WKWebView *)wv
    didFailProvisionalNavigation:(WKNavigation *)navigation withError:(NSError *)error {
    (void)wv; (void)navigation; (void)error;
    if (self.slot >= 0 && self.slot < ZAN_MAX_WEBVIEWS) g_webviews[self.slot].navSeq++;
    zan_gui_wake();
}
- (void)webView:(WKWebView *)wv didReceiveServerRedirectForProvisionalNavigation:(WKNavigation *)navigation {
    (void)wv; (void)navigation;
    if (self.slot >= 0 && self.slot < ZAN_MAX_WEBVIEWS) g_webviews[self.slot].navSeq++;
    zan_gui_wake();
}
@end

static WKWebView *wv_get(i64 h) {
    if (h < 1 || h > ZAN_MAX_WEBVIEWS) return nil;
    zan_webview_t *w = &g_webviews[(int)h - 1];
    return w->used ? w->wv : nil;
}

/* Copy s into the given per-view buffer slot and return it (valid until the
 * next call for that slot's field). */
static const char *wv_store(char **slot, const char *s) {
    char *nb = strdup(s ? s : "");
    if (!nb) return "";
    free(*slot);
    *slot = nb;
    return *slot;
}

/* Spin the main run loop until *done or the timeout elapses. Used to bridge
 * WebKit's async cookie/JS APIs to a synchronous ABI without deadlocking. */
static void wv_spin(volatile BOOL *done, double timeout) {
    NSDate *deadline = [NSDate dateWithTimeIntervalSinceNow:timeout];
    while (!*done && [deadline timeIntervalSinceNow] > 0) {
        [[NSRunLoop currentRunLoop]
            runMode:NSDefaultRunLoopMode
            beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.01]];
    }
}

EXPORT i64 zan_gui_webview_create(i64 hwnd_val) {
    zan_mwin_t *mw = mwin_find(hwnd_val);
    if (!mw && g_mwin_count > 0) mw = &g_mwins[0];
    if (!mw || !mw->view) return 0;
    int slot = -1;
    for (int i = 0; i < ZAN_MAX_WEBVIEWS; i++) {
        if (!g_webviews[i].used) { slot = i; break; }
    }
    if (slot < 0) return 0;
    @autoreleasepool {
        WKWebViewConfiguration *cfg = [[WKWebViewConfiguration alloc] init];
        WKWebView *wv = [[WKWebView alloc] initWithFrame:NSMakeRect(0, 0, 100, 100)
                                           configuration:cfg];
        [cfg release];
        ZanWebViewDelegate *d = [[ZanWebViewDelegate alloc] init];
        d.slot = slot;
        wv.navigationDelegate = d;   /* weak; the +1 from alloc keeps it alive */
        [wv setHidden:YES];
        [mw->view addSubview:wv];    /* view retains wv; keep our +1 too */

        zan_webview_t *w = &g_webviews[slot];
        w->used = 1;
        w->wv = wv;
        w->delegate = d;
        w->ownerWindow = mw->window;
        w->navSeq = 0;
        w->lastStatus = 0;
        w->urlBuf = NULL;
        w->titleBuf = NULL;
        w->lastReqBuf = NULL;
        w->evalBuf = NULL;
        w->cookieBuf = NULL;
    }
    return (i64)(slot + 1);
}

EXPORT void zan_gui_webview_destroy(i64 h) {
    if (h < 1 || h > ZAN_MAX_WEBVIEWS) return;
    zan_webview_t *w = &g_webviews[(int)h - 1];
    if (!w->used) return;
    @autoreleasepool {
        if (w->wv) {
            w->wv.navigationDelegate = nil;
            [w->wv removeFromSuperview]; /* drop the superview's retain */
            [w->wv release];             /* drop our create-time retain */
        }
        if (w->delegate) [w->delegate release];
    }
    free(w->urlBuf);
    free(w->titleBuf);
    free(w->lastReqBuf);
    free(w->evalBuf);
    free(w->cookieBuf);
    memset(w, 0, sizeof(*w));
}

EXPORT void zan_gui_webview_set_frame(i64 h, i64 x, i64 y, i64 w, i64 hh) {
    WKWebView *wv = wv_get(h);
    if (!wv) return;
    [wv setFrame:NSMakeRect((CGFloat)x, (CGFloat)y, (CGFloat)w, (CGFloat)hh)];
}

EXPORT void zan_gui_webview_set_visible(i64 h, i64 visible) {
    WKWebView *wv = wv_get(h);
    if (!wv) return;
    [wv setHidden:(visible ? NO : YES)];
}

EXPORT void zan_gui_webview_navigate(i64 h, const char *url) {
    WKWebView *wv = wv_get(h);
    if (!wv || !url || !*url) return;
    @autoreleasepool {
        NSString *s = [NSString stringWithUTF8String:url];
        NSURL *u = [NSURL URLWithString:s];
        if (!u || ![u scheme]) {
            u = [NSURL URLWithString:[@"https://" stringByAppendingString:s]];
        }
        if (u) [wv loadRequest:[NSURLRequest requestWithURL:u]];
    }
}

EXPORT void zan_gui_webview_load_html(i64 h, const char *html, const char *base_url) {
    WKWebView *wv = wv_get(h);
    if (!wv || !html) return;
    @autoreleasepool {
        NSString *body = [NSString stringWithUTF8String:html];
        NSURL *base = (base_url && *base_url)
                          ? [NSURL URLWithString:[NSString stringWithUTF8String:base_url]]
                          : nil;
        [wv loadHTMLString:(body ? body : @"") baseURL:base];
    }
}

EXPORT void zan_gui_webview_back(i64 h) {
    WKWebView *wv = wv_get(h);
    if (wv && [wv canGoBack]) [wv goBack];
}

EXPORT void zan_gui_webview_forward(i64 h) {
    WKWebView *wv = wv_get(h);
    if (wv && [wv canGoForward]) [wv goForward];
}

EXPORT void zan_gui_webview_reload(i64 h) {
    WKWebView *wv = wv_get(h);
    if (wv) [wv reload];
}

EXPORT void zan_gui_webview_stop(i64 h) {
    WKWebView *wv = wv_get(h);
    if (wv) [wv stopLoading];
}

EXPORT i64 zan_gui_webview_can_go_back(i64 h) {
    WKWebView *wv = wv_get(h);
    return (wv && [wv canGoBack]) ? 1 : 0;
}

EXPORT i64 zan_gui_webview_can_go_forward(i64 h) {
    WKWebView *wv = wv_get(h);
    return (wv && [wv canGoForward]) ? 1 : 0;
}

EXPORT i64 zan_gui_webview_is_loading(i64 h) {
    WKWebView *wv = wv_get(h);
    return (wv && [wv isLoading]) ? 1 : 0;
}

EXPORT i64 zan_gui_webview_nav_seq(i64 h) {
    if (h < 1 || h > ZAN_MAX_WEBVIEWS) return 0;
    zan_webview_t *w = &g_webviews[(int)h - 1];
    return w->used ? (i64)w->navSeq : 0;
}

EXPORT i64 zan_gui_webview_last_status(i64 h) {
    if (h < 1 || h > ZAN_MAX_WEBVIEWS) return 0;
    zan_webview_t *w = &g_webviews[(int)h - 1];
    return w->used ? (i64)w->lastStatus : 0;
}

EXPORT const char *zan_gui_webview_get_url(i64 h) {
    if (h < 1 || h > ZAN_MAX_WEBVIEWS) return "";
    zan_webview_t *w = &g_webviews[(int)h - 1];
    if (!w->used || !w->wv) return "";
    @autoreleasepool {
        NSURL *u = w->wv.URL;
        const char *s = u ? [[u absoluteString] UTF8String] : "";
        return wv_store(&w->urlBuf, s);
    }
}

EXPORT const char *zan_gui_webview_get_title(i64 h) {
    if (h < 1 || h > ZAN_MAX_WEBVIEWS) return "";
    zan_webview_t *w = &g_webviews[(int)h - 1];
    if (!w->used || !w->wv) return "";
    @autoreleasepool {
        NSString *t = w->wv.title;
        const char *s = t ? [t UTF8String] : "";
        return wv_store(&w->titleBuf, s);
    }
}

EXPORT const char *zan_gui_webview_last_request(i64 h) {
    if (h < 1 || h > ZAN_MAX_WEBVIEWS) return "";
    zan_webview_t *w = &g_webviews[(int)h - 1];
    if (!w->used) return "";
    return w->lastReqBuf ? w->lastReqBuf : "";
}

EXPORT const char *zan_gui_webview_eval(i64 h, const char *js) {
    if (h < 1 || h > ZAN_MAX_WEBVIEWS) return "";
    zan_webview_t *w = &g_webviews[(int)h - 1];
    if (!w->used || !w->wv || !js) return "";
    @autoreleasepool {
        NSString *script = [NSString stringWithUTF8String:js];
        __block BOOL done = NO;
        __block NSString *out = nil;
        [w->wv evaluateJavaScript:(script ? script : @"")
                completionHandler:^(id result, NSError *error) {
            if (!error && result && result != [NSNull null]) {
                out = [[NSString stringWithFormat:@"%@", result] retain];
            }
            done = YES;
        }];
        wv_spin(&done, 3.0);
        const char *s = out ? [out UTF8String] : "";
        const char *ret = wv_store(&w->evalBuf, s);
        if (out) [out release];
        return ret;
    }
}

EXPORT const char *zan_gui_webview_get_cookies(i64 h, const char *url) {
    if (h < 1 || h > ZAN_MAX_WEBVIEWS) return "";
    zan_webview_t *w = &g_webviews[(int)h - 1];
    if (!w->used || !w->wv) return "";
    @autoreleasepool {
        NSString *filterHost = nil;
        if (url && *url) {
            NSURL *u = [NSURL URLWithString:[NSString stringWithUTF8String:url]];
            filterHost = u ? [u host] : nil;
        }
        WKHTTPCookieStore *store =
            w->wv.configuration.websiteDataStore.httpCookieStore;
        __block BOOL done = NO;
        __block NSArray<NSHTTPCookie *> *cookies = nil;
        [store getAllCookies:^(NSArray<NSHTTPCookie *> *result) {
            cookies = [result retain];
            done = YES;
        }];
        wv_spin(&done, 3.0);
        NSMutableString *acc = [NSMutableString string];
        for (NSHTTPCookie *c in cookies) {
            if (filterHost) {
                NSString *dom = [c domain];
                NSString *bare = [dom hasPrefix:@"."] ? [dom substringFromIndex:1] : dom;
                if (!([filterHost isEqualToString:dom] ||
                      [filterHost isEqualToString:bare] ||
                      [filterHost hasSuffix:bare])) {
                    continue;
                }
            }
            if ([acc length] > 0) [acc appendString:@"; "];
            [acc appendFormat:@"%@=%@", [c name], [c value]];
        }
        if (cookies) [cookies release];
        return wv_store(&w->cookieBuf, [acc UTF8String]);
    }
}

EXPORT void zan_gui_webview_set_cookie(i64 h, const char *url,
                                       const char *name, const char *value) {
    WKWebView *wv = wv_get(h);
    if (!wv || !url || !name) return;
    @autoreleasepool {
        NSURL *u = [NSURL URLWithString:[NSString stringWithUTF8String:url]];
        NSString *host = u ? [u host] : nil;
        if (!host) return;
        NSString *path = ([u path] && [[u path] length] > 0) ? [u path] : @"/";
        NSDictionary *props = @{
            NSHTTPCookieName: [NSString stringWithUTF8String:name],
            NSHTTPCookieValue: [NSString stringWithUTF8String:(value ? value : "")],
            NSHTTPCookieDomain: host,
            NSHTTPCookiePath: path
        };
        NSHTTPCookie *cookie = [NSHTTPCookie cookieWithProperties:props];
        if (!cookie) return;
        WKHTTPCookieStore *store =
            wv.configuration.websiteDataStore.httpCookieStore;
        __block BOOL done = NO;
        [store setCookie:cookie completionHandler:^{ done = YES; }];
        wv_spin(&done, 3.0);
    }
}

EXPORT void zan_gui_webview_clear_cookies(i64 h) {
    WKWebView *wv = wv_get(h);
    if (!wv) return;
    @autoreleasepool {
        WKWebsiteDataStore *ds = wv.configuration.websiteDataStore;
        NSSet *types = [NSSet setWithObject:WKWebsiteDataTypeCookies];
        __block BOOL done = NO;
        [ds removeDataOfTypes:types
                modifiedSince:[NSDate dateWithTimeIntervalSince1970:0]
            completionHandler:^{ done = YES; }];
        wv_spin(&done, 3.0);
    }
}

EXPORT i64 zan_gui_set_clipboard(const char *utf8) {
    @autoreleasepool {
        NSPasteboard *pb = [NSPasteboard generalPasteboard];
        [pb clearContents];
        NSString *s = [NSString stringWithUTF8String:(utf8 ? utf8 : "")];
        [pb setString:(s ? s : @"") forType:NSPasteboardTypeString];
    }
    return 0;
}

/* Read the general pasteboard's text as UTF-8. Mirrors the Windows/X11 ABI:
 * returns a NUL-terminated i8* valid until the next call (the previous buffer
 * is freed each time), or "" when the pasteboard holds no text. */
EXPORT const char *zan_gui_get_clipboard(void) {
    static char *g_clip_buf = NULL;
    @autoreleasepool {
        NSPasteboard *pb = [NSPasteboard generalPasteboard];
        NSString *s = [pb stringForType:NSPasteboardTypeString];
        const char *utf8 = s ? [s UTF8String] : NULL;
        if (!utf8) return "";
        char *nb = strdup(utf8);
        if (!nb) return "";
        free(g_clip_buf);
        g_clip_buf = nb;
        return g_clip_buf;
    }
}
