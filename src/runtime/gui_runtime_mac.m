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
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

typedef int64_t i64;
typedef uint32_t u32;

/* Implemented in gui_runtime.c; not part of the [DllImport] ABI. */
extern const u32 *zan_gui_internal_surface_data(i64 id, int *w, int *h, int *stride);

#define EXPORT __attribute__((visibility("default")))

/* ---- global state (single window, matching the other backends) ---- */
static NSWindow *g_window = nil;
static NSView   *g_view   = nil;
static CGImageRef g_frame = NULL;
/* Flat pending-event slots: [kind, x, y, button, keycode, mods]. */
static long g_evt[6];

/* Custom content view that blits the last presented CGImage. */
@interface ZanView : NSView
@end

@implementation ZanView
- (BOOL)isFlipped { return YES; }          /* top-left origin, matches surface */
- (BOOL)acceptsFirstResponder { return YES; }
- (void)drawRect:(NSRect)dirtyRect {
    (void)dirtyRect;
    if (!g_frame) return;
    CGContextRef ctx = (CGContextRef)[[NSGraphicsContext currentContext] CGContext];
    if (!ctx) return;
    CGContextDrawImage(ctx, self.bounds, g_frame);
}
@end

/* ---- window lifecycle ---- */

EXPORT i64 zan_gui_create_window(const char *title, i64 width, i64 height) {
    @autoreleasepool {
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];

        NSRect rect = NSMakeRect(0, 0, (CGFloat)width, (CGFloat)height);
        NSUInteger style = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                           NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable;
        g_window = [[NSWindow alloc] initWithContentRect:rect
                                               styleMask:style
                                                 backing:NSBackingStoreBuffered
                                                   defer:NO];
        g_view = [[ZanView alloc] initWithFrame:rect];
        [g_window setContentView:g_view];
        if (title) {
            [g_window setTitle:[NSString stringWithUTF8String:title]];
        }
        [g_window center];
        return (i64)(intptr_t)g_window;
    }
}

EXPORT i64 zan_gui_show_window(i64 hwnd_val) {
    (void)hwnd_val;
    if (!g_window) return 1;
    @autoreleasepool {
        [NSApp activateIgnoringOtherApps:YES];
        [g_window makeKeyAndOrderFront:nil];
    }
    return 0;
}

EXPORT i64 zan_gui_close_window(i64 hwnd_val) {
    (void)hwnd_val;
    if (g_window) {
        @autoreleasepool { [g_window close]; }
        g_window = nil;
        g_view = nil;
    }
    return 0;
}

/* ---- event pump ---- */

static void translate_event(NSEvent *ev) {
    memset(g_evt, 0, sizeof(g_evt));
    NSPoint p = [ev locationInWindow];
    NSPoint local = g_view ? [g_view convertPoint:p fromView:nil] : p;
    long mods = 0;
    NSUInteger f = [ev modifierFlags];
    if (f & NSEventModifierFlagShift)   mods |= 1;
    if (f & NSEventModifierFlagControl) mods |= 2;
    if (f & NSEventModifierFlagOption)  mods |= 4;
    if (f & NSEventModifierFlagCommand) mods |= 8;
    g_evt[5] = mods;

    switch ([ev type]) {
    case NSEventTypeMouseMoved:
    case NSEventTypeLeftMouseDragged:
    case NSEventTypeRightMouseDragged:
    case NSEventTypeOtherMouseDragged:
        g_evt[0] = 1; g_evt[1] = (long)local.x; g_evt[2] = (long)local.y;
        break;
    case NSEventTypeLeftMouseDown:
        g_evt[0] = 2; g_evt[1] = (long)local.x; g_evt[2] = (long)local.y; g_evt[3] = 0;
        break;
    case NSEventTypeRightMouseDown:
        g_evt[0] = 2; g_evt[1] = (long)local.x; g_evt[2] = (long)local.y; g_evt[3] = 1;
        break;
    case NSEventTypeOtherMouseDown:
        g_evt[0] = 2; g_evt[1] = (long)local.x; g_evt[2] = (long)local.y; g_evt[3] = 2;
        break;
    case NSEventTypeLeftMouseUp:
        g_evt[0] = 3; g_evt[1] = (long)local.x; g_evt[2] = (long)local.y; g_evt[3] = 0;
        break;
    case NSEventTypeRightMouseUp:
        g_evt[0] = 3; g_evt[1] = (long)local.x; g_evt[2] = (long)local.y; g_evt[3] = 1;
        break;
    case NSEventTypeOtherMouseUp:
        g_evt[0] = 3; g_evt[1] = (long)local.x; g_evt[2] = (long)local.y; g_evt[3] = 2;
        break;
    case NSEventTypeKeyDown:
        g_evt[0] = 4; g_evt[4] = (long)[ev keyCode];
        break;
    case NSEventTypeKeyUp:
        g_evt[0] = 5; g_evt[4] = (long)[ev keyCode];
        break;
    default:
        break;
    }
}

static i64 pump(bool wait) {
    if (!g_window) return -1;
    memset(g_evt, 0, sizeof(g_evt));
    @autoreleasepool {
        NSDate *until = wait ? [NSDate distantFuture] : [NSDate distantPast];
        NSEvent *ev = [NSApp nextEventMatchingMask:NSEventMaskAny
                                         untilDate:until
                                            inMode:NSDefaultRunLoopMode
                                           dequeue:YES];
        if (!ev) return 1;                 /* no event pending (poll) */
        translate_event(ev);
        [NSApp sendEvent:ev];              /* keep the window responsive */
    }
    return 0;
}

EXPORT i64 zan_gui_wait_event(void) { return pump(true); }
EXPORT i64 zan_gui_poll_event(void) { return pump(false); }

EXPORT i64 zan_gui_event_kind(void)    { return g_evt[0]; }
EXPORT i64 zan_gui_event_x(void)       { return g_evt[1]; }
EXPORT i64 zan_gui_event_y(void)       { return g_evt[2]; }
EXPORT i64 zan_gui_event_button(void)  { return g_evt[3]; }
EXPORT i64 zan_gui_event_keycode(void) { return g_evt[4]; }
EXPORT i64 zan_gui_event_mods(void)    { return g_evt[5]; }
EXPORT i64 zan_gui_event_hwnd(void)    { return (i64)(intptr_t)g_window; }

/* ---- geometry ---- */

EXPORT i64 zan_gui_window_width(void) {
    if (!g_view) return 0;
    return (i64)[g_view bounds].size.width;
}
EXPORT i64 zan_gui_window_height(void) {
    if (!g_view) return 0;
    return (i64)[g_view bounds].size.height;
}
EXPORT i64 zan_gui_client_width(i64 hwnd_val)  { (void)hwnd_val; return zan_gui_window_width(); }
EXPORT i64 zan_gui_client_height(i64 hwnd_val) { (void)hwnd_val; return zan_gui_window_height(); }

/* ---- present: blit the software surface to the window ---- */

EXPORT i64 zan_gui_present(i64 hwnd_val, i64 surface_id) {
    (void)hwnd_val;
    if (!g_view) return 1;
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
            if (g_frame) CGImageRelease(g_frame);
            g_frame = img;                 /* retained; released on next present */
            [g_view setNeedsDisplay:YES];
            [g_view displayIfNeeded];
        }
        if (bmp) CGContextRelease(bmp);
        CGColorSpaceRelease(cs);
    }
    return 0;
}

/* ---- misc ---- */

EXPORT i64 zan_gui_set_title(i64 hwnd_val, const char *title) {
    (void)hwnd_val;
    if (g_window && title) {
        @autoreleasepool { [g_window setTitle:[NSString stringWithUTF8String:title]]; }
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
    (void)hwnd_val;
    if (g_window) { @autoreleasepool { [g_window miniaturize:nil]; } }
    return 0;
}

EXPORT i64 zan_gui_toggle_maximize(i64 hwnd_val) {
    (void)hwnd_val;
    if (g_window) { @autoreleasepool { [g_window zoom:nil]; } }
    return 0;
}

EXPORT i64 zan_gui_is_maximized(i64 hwnd_val) {
    (void)hwnd_val;
    if (!g_window) return 0;
    return [g_window isZoomed] ? 1 : 0;
}

EXPORT i64 zan_gui_set_topmost(i64 hwnd_val, i64 on) {
    (void)hwnd_val;
    if (g_window) {
        [g_window setLevel:(on ? NSFloatingWindowLevel : NSNormalWindowLevel)];
    }
    return 0;
}

EXPORT i64 zan_gui_get_dpi_scale(void) {
    @autoreleasepool {
        NSWindow *w = g_window;
        CGFloat scale = w ? [w backingScaleFactor]
                          : [[NSScreen mainScreen] backingScaleFactor];
        if (scale <= 0) scale = 1.0;
        return (i64)(scale * 100.0 + 0.5);
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
