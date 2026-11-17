/* Native Cocoa contract test; link against zan_gui and Cocoa, without ARC.
 * Real pasteboards and file URLs exercise the destination callbacks. Finder's
 * system drag routing still needs an interactive macOS session to verify. */
#import <Cocoa/Cocoa.h>
#import <objc/runtime.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

typedef intptr_t iptr;
typedef int32_t i32;

extern iptr zan_gui_create_window(const char *title, i32 width, i32 height);
extern i32 zan_gui_show_window(iptr hwnd);
extern i32 zan_gui_close_window(iptr hwnd);
extern int zan_gui_resize_client(iptr hwnd, i32 width, i32 height);
extern i32 zan_gui_set_window_pos(iptr hwnd, i32 x, i32 y);
extern i32 zan_gui_client_width(iptr hwnd);
extern i32 zan_gui_client_height(iptr hwnd);
extern i32 zan_gui_enable_glass(iptr hwnd, i32 tint);
extern i32 zan_gui_disable_glass(iptr hwnd);
extern i32 zan_gui_destroy_window(iptr hwnd);
extern i32 zan_gui_create_surface(i32 width, i32 height);
extern i32 zan_gui_destroy_surface(i32 surface);
extern i32 zan_gui_present(iptr hwnd, i32 surface);
extern i32 zan_gui_poll_event(void);
extern i32 zan_gui_event_kind(void);
extern i32 zan_gui_event_x(void);
extern i32 zan_gui_event_y(void);
extern iptr zan_gui_event_hwnd(void);
extern i32 zan_gui_inject_event(iptr hwnd, i32 kind, i32 x, i32 y,
                                i32 button, i32 keycode, i32 mods);
extern i32 zan_gui_inject_pending(void);
extern int zan_gui_drop_pending(void);
extern const char *zan_gui_drop_take(void);

static void check(BOOL condition, const char *message) {
    if (!condition) {
        @throw [NSException exceptionWithName:@"ZanGuiTestFailure"
                                      reason:[NSString stringWithUTF8String:message]
                                    userInfo:nil];
    }
}

/* Only the source metadata is substituted; URL decoding and destination
 * dispatch use the actual pasteboard and the runtime's registered ZanView. */
@interface ZanGuiTestDraggingInfo : NSObject {
    NSPasteboard *pasteboard;
    NSWindow *window;
    NSPoint point;
    NSDragOperation operation;
}
- (instancetype)initWithPasteboard:(NSPasteboard *)board window:(NSWindow *)target
                            point:(NSPoint)location operation:(NSDragOperation)mask;
@end

@implementation ZanGuiTestDraggingInfo
- (instancetype)initWithPasteboard:(NSPasteboard *)board window:(NSWindow *)target
                            point:(NSPoint)location operation:(NSDragOperation)mask {
    self = [super init];
    if (self) {
        pasteboard = [board retain];
        window = [target retain];
        point = location;
        operation = mask;
    }
    return self;
}
- (NSPasteboard *)draggingPasteboard { return pasteboard; }
- (NSWindow *)draggingDestinationWindow { return window; }
- (NSPoint)draggingLocation { return point; }
- (NSDragOperation)draggingSourceOperationMask { return operation; }
- (void)dealloc {
    [pasteboard release];
    [window release];
    [super dealloc];
}
@end

/* Associated sentinels observe actual deallocation without retainCount or
 * retaining the window/view under test. Keep flags global for failure cleanup. */
@interface ZanGuiTestLifetime : NSObject {
    BOOL *destroyed;
}
- (instancetype)initWithFlag:(BOOL *)flag;
@end

@implementation ZanGuiTestLifetime
- (instancetype)initWithFlag:(BOOL *)flag {
    self = [super init];
    if (self) destroyed = flag;
    return self;
}
- (void)dealloc {
    *destroyed = YES;
    [super dealloc];
}
@end

static char lifetime_key;
static BOOL lifetime_window, lifetime_view, lifetime_glass[2];

static void watch_lifetime(id object, BOOL *flag) {
    *flag = NO;
    ZanGuiTestLifetime *watch = [[ZanGuiTestLifetime alloc] initWithFlag:flag];
    objc_setAssociatedObject(object, &lifetime_key, watch, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    [watch release];
}

static NSMutableArray *pasteboards;

static NSPasteboard *make_pasteboard(NSArray *objects) {
    NSPasteboard *board = [NSPasteboard pasteboardWithUniqueName];
    [pasteboards addObject:board];
    check([board writeObjects:objects], "write native pasteboard");
    return board;
}

static id<NSDraggingInfo> drag_info(NSView *view, NSPasteboard *board,
                                  NSPoint local, NSDragOperation operation) {
    ZanGuiTestDraggingInfo *info = [[ZanGuiTestDraggingInfo alloc]
        initWithPasteboard:board window:[view window]
        point:[view convertPoint:local toView:nil] operation:operation];
    return (id<NSDraggingInfo>)[info autorelease];
}

static id<NSDraggingDestination> destination(NSView *view) {
    check([view conformsToProtocol:@protocol(NSDraggingDestination)],
          "drawing view adopts NSDraggingDestination");
    check([[view registeredDraggedTypes] containsObject:NSPasteboardTypeFileURL],
          "drawing view registers file URLs");
    return (id<NSDraggingDestination>)view;
}

static int pixels(CGFloat points, NSWindow *window) {
    return (int)floor(points * [window backingScaleFactor] + 0.5);
}

static void drain(void) {
    for (int i = 0; i < 1024; i++) {
        i32 result = zan_gui_poll_event();
        if (result == 1) {
            check(!zan_gui_drop_pending(), "idle has no drop payload");
            return;
        }
        check(result == 0, "event pump remains usable");
    }
    check(NO, "event queue drains within a bounded number of polls");
}

static void check_resize_events(iptr hwnd, i32 width, i32 height) {
    BOOL found = NO;
    for (int i = 0; i < 1024; i++) {
        i32 result = zan_gui_poll_event();
        if (result == 1) {
            check(found, "resize publishes a kind-7 event");
            return;
        }
        check(result == 0, "pump resize notifications");
        if (zan_gui_event_kind() == 7) {
            check(zan_gui_event_hwnd() == hwnd, "resize event routes to target window");
            check(zan_gui_event_x() == width && zan_gui_event_y() == height,
                  "resize event reports device-pixel client dimensions");
            found = YES;
        }
    }
    check(NO, "resize notifications drain within a bounded number of polls");
}

static void next_event(i32 kind, iptr hwnd) {
    check(zan_gui_poll_event() == 0, "queued event is delivered");
    check(zan_gui_event_kind() == kind, "event kind matches queue order");
    check(zan_gui_event_hwnd() == hwnd, "event routes to its own window");
}

static void no_paths(void) {
    check(!zan_gui_drop_pending(), "event has no pending drop paths");
    const char *path = zan_gui_drop_take();
    check(path && !*path, "empty batch returns an empty string");
}

static void take_paths(NSArray *paths) {
    NSUInteger count = 0;
    while (1) {
        const char *path = zan_gui_drop_take();
        check(path != NULL, "drop_take never returns NULL");
        if (!*path) break;
        check(count < [paths count], "drop contains no extra paths");
        check(strcmp(path, [[paths objectAtIndex:count] UTF8String]) == 0,
              "decoded UTF-8 path preserves content and order");
        count++;
    }
    check(count == [paths count], "drop_take drains the whole batch");
    no_paths();
}

static void test_geometry(iptr primary, iptr peer) {
    NSWindow *first = (NSWindow *)primary;
    NSWindow *second = (NSWindow *)peer;
    i32 primary_w = zan_gui_client_width(primary);
    i32 primary_h = zan_gui_client_height(primary);
    check(zan_gui_resize_client(peer, 481, 361) == 0, "resize peer in device pixels");
    check(zan_gui_client_width(peer) == 481 && zan_gui_client_height(peer) == 361,
          "odd device-pixel dimensions round trip");
    check(zan_gui_client_width(primary) == primary_w &&
          zan_gui_client_height(primary) == primary_h, "peer resize isolates primary");
    check(pixels([[second contentView] bounds].size.width, second) == 481,
          "native points agree with the target window backing scale");
    check_resize_events(peer, 481, 361);
    check(zan_gui_resize_client(0, 333, 247) == 0, "zero handle resizes primary");
    check(zan_gui_client_width(primary) == 333 && zan_gui_client_height(primary) == 247,
          "zero handle resolves primary geometry");
    check(zan_gui_client_width(peer) == 481 && zan_gui_client_height(peer) == 361,
          "primary resize isolates peer");
    check_resize_events(primary, 333, 247);
    check(zan_gui_resize_client((iptr)1, 100, 100) == -1,
          "unknown resize handle fails without primary fallback");
    check(zan_gui_resize_client(peer, 0, 100) == -1 &&
          zan_gui_resize_client(peer, 100, -1) == -1, "nonpositive sizes fail");
    check(zan_gui_client_width(peer) == 481 && zan_gui_client_height(peer) == 361,
          "invalid resize leaves target unchanged");

    NSRect first_frame = [first frame];
    NSScreen *screen = [second screen];
    check(screen != nil, "peer has a target screen");
    NSRect work = [screen visibleFrame];
    CGFloat scale = [second backingScaleFactor];
    NSSize size = [second frame].size;
    check(zan_gui_set_window_pos(peer, 74, 86) == 0, "position peer from work-area top left");
    NSRect moved = [second frame];
    check(fabs(moved.origin.x - (NSMinX(work) + 74 / scale)) < 0.01 &&
          fabs(NSMaxY(moved) - (NSMaxY(work) - 86 / scale)) < 0.01,
          "position converts device pixels and flips AppKit screen Y");
    check(NSEqualSizes(moved.size, size), "position preserves size");
    check(NSEqualRects([first frame], first_frame), "peer positioning isolates primary");
    check(zan_gui_set_window_pos((iptr)1, 1, 1) != 0,
          "unknown position handle fails without primary fallback");
    check(NSEqualRects([first frame], first_frame), "invalid position preserves primary");
    check(zan_gui_set_window_pos(0, 38, 50) == 0, "zero position handle resolves primary");
    check(NSEqualRects([second frame], moved), "primary positioning isolates peer");

    BOOL retina = NO;
    BOOL mixed = NO;
    for (NSScreen *candidate in [NSScreen screens]) {
        NSRect frame = [candidate visibleFrame];
        [second setFrameOrigin:NSMakePoint(NSMinX(frame) + 24, NSMinY(frame) + 24)];
        drain();
        CGFloat target_scale = [second backingScaleFactor];
        check(fabs(target_scale - [candidate backingScaleFactor]) < 0.01,
              "peer adopts the destination screen backing scale");
        if (target_scale > 1) retina = YES;
        if (fabs(target_scale - [first backingScaleFactor]) > 0.01) mixed = YES;
        check(zan_gui_resize_client(peer, 483, 363) == 0,
              "resize after screen change uses peer scale");
        check(zan_gui_client_width(peer) == 483 && zan_gui_client_height(peer) == 363,
              "per-screen device pixels round trip");
        check(zan_gui_client_width(primary) == 333 && zan_gui_client_height(primary) == 247,
              "screen changes do not resize primary");
    }
    printf("Retina display coverage: %s; mixed-scale display coverage: %s\n",
           retina ? "covered" : "unavailable", mixed ? "covered" : "unavailable");
    [second setFrame:moved display:NO];
    check(zan_gui_resize_client(peer, 481, 361) == 0, "restore peer device size");
    drain();
}

/* Keep the peer key so mainScreen need not follow the primary's destination.
 * Inspect the new child's initial geometry before any resize can hide the bug. */
static void test_child_creation_after_screen_change(iptr primary, iptr peer) {
    NSWindow *parent = (NSWindow *)primary;
    NSWindow *other = (NSWindow *)peer;
    NSRect original = [parent frame];
    BOOL mixed = NO;
    @try {
        for (NSScreen *screen in [NSScreen screens]) {
            NSRect work = [screen visibleFrame];
            [parent setFrameOrigin:NSMakePoint(NSMinX(work) + 24, NSMinY(work) + 24)];
            [other makeKeyAndOrderFront:nil];
            drain();
            check([parent screen] == screen, "primary moves to the child destination screen");
            CGFloat scale = [parent backingScaleFactor];
            if (fabs(scale - [[NSScreen mainScreen] backingScaleFactor]) > 0.01) mixed = YES;
            NSRect parent_frame = [parent frame];
            NSRect peer_frame = [other frame];
            iptr child = zan_gui_create_window("mac-child-after-screen-change", 485, 365);
            check(child != 0, "create child after moving primary between screens");
            @try {
                NSWindow *window = (NSWindow *)child;
                check([window screen] == screen, "new child follows the primary's screen");
                check(fabs([window backingScaleFactor] - scale) < 0.01,
                      "new child uses the destination backing scale");
                check(zan_gui_client_width(child) == 485 && zan_gui_client_height(child) == 365,
                      "initial child size preserves odd device-pixel dimensions");
                NSSize bounds = [[window contentView] bounds].size;
                check(pixels(bounds.width, window) == 485 && pixels(bounds.height, window) == 365,
                      "native child bounds agree before the first resize");
                check(NSEqualRects([parent frame], parent_frame) &&
                      NSEqualRects([other frame], peer_frame), "child creation leaves both peers unchanged");
                check(zan_gui_show_window(child) == 0, "show child without resizing it");
                drain();
                check(zan_gui_client_width(child) == 485 && zan_gui_client_height(child) == 365,
                      "showing child preserves its initial device-pixel dimensions");
            } @finally {
                zan_gui_close_window(child);
            }
            drain();
        }
    } @finally {
        [parent setFrame:original display:NO];
    }
    printf("Child creation with parent/mainScreen scale mismatch: %s\n",
           mixed ? "covered" : "unavailable");
    drain();
}

static void test_batches(iptr primary, iptr peer, NSArray *paths,
                         NSPasteboard *both, NSPasteboard *single) {
    NSView *first = [(NSWindow *)primary contentView];
    NSView *second = [(NSWindow *)peer contentView];
    id<NSDraggingDestination> first_drop = destination(first);
    id<NSDraggingDestination> second_drop = destination(second);
    NSPoint local = NSMakePoint(23, 31);
    id<NSDraggingInfo> peer_info = drag_info(second, both, local, NSDragOperationCopy);
    id<NSDraggingInfo> first_info = drag_info(first, single, local, NSDragOperationCopy);
    drain();
    check([second_drop draggingEntered:peer_info] == NSDragOperationCopy &&
          [second_drop draggingUpdated:peer_info] == NSDragOperationCopy &&
          [second_drop prepareForDragOperation:peer_info], "native destination accepts file URL copy");
    zan_gui_inject_event(primary, 9, 7, 8, 0, 0, 0);
    check([second_drop performDragOperation:peer_info], "queue peer file drop");
    zan_gui_inject_event(primary, 9, 9, 10, 0, 0, 0);
    check([first_drop performDragOperation:first_info], "queue primary file drop");
    no_paths();
    next_event(9, primary);
    no_paths();
    next_event(9, peer);
    check(zan_gui_event_x() == pixels(local.x, (NSWindow *)peer) &&
          zan_gui_event_y() == pixels(local.y, (NSWindow *)peer),
          "drop location uses flipped client device pixels");
    check(zan_gui_drop_pending() && zan_gui_drop_pending(), "pending is nondestructive");
    check(strcmp(zan_gui_drop_take(), [[paths objectAtIndex:0] UTF8String]) == 0,
          "first UTF-8 path survives the callback autorelease pool");
    check(zan_gui_drop_pending(), "second path stays pending");
    const char *last = zan_gui_drop_take();
    check(strcmp(last, [[paths objectAtIndex:1] UTF8String]) == 0, "second UTF-8 path decodes once");
    check(!zan_gui_drop_pending(), "taking last path clears pending");
    next_event(9, primary);
    check(strcmp(last, [[paths objectAtIndex:1] UTF8String]) == 0,
          "returned path lives until next take across event changes");
    no_paths();
    next_event(9, primary);
    take_paths(@[[paths objectAtIndex:1]]);
    drain();

    check([second_drop performDragOperation:peer_info], "queue partially consumed batch");
    zan_gui_inject_event(primary, 9, 0, 0, 0, 0, 0);
    next_event(9, peer);
    check(*zan_gui_drop_take() != 0, "partially consume peer batch");
    next_event(9, primary);
    no_paths();
    drain();

    NSPasteboard *web = make_pasteboard(@[[NSURL URLWithString:@"https://example.invalid/file.txt"]]);
    id<NSDraggingInfo> web_info = drag_info(first, web, local, NSDragOperationCopy);
    check([first_drop draggingEntered:web_info] == NSDragOperationNone &&
          ![first_drop performDragOperation:web_info], "non-file URL is rejected");
    id<NSDraggingInfo> move = drag_info(first, both, local, NSDragOperationMove);
    check(![first_drop prepareForDragOperation:move] && ![first_drop performDragOperation:move],
          "move-only source is rejected");
    id<NSDraggingInfo> outside = drag_info(first, both,
        NSMakePoint(NSMaxX([first bounds]) + 1, 10), NSDragOperationCopy);
    check(![first_drop performDragOperation:outside], "out-of-view drop is rejected");
    check(![first_drop performDragOperation:peer_info], "destination window mismatch is rejected");
    check(zan_gui_inject_pending() == 0, "rejected drops enqueue no event");
    no_paths();
}

static void test_capacity(iptr primary, iptr peer, NSArray *paths,
                          NSPasteboard *both, NSPasteboard *single) {
    NSView *first = [(NSWindow *)primary contentView];
    NSView *second = [(NSWindow *)peer contentView];
    id<NSDraggingDestination> first_drop = destination(first);
    id<NSDraggingDestination> second_drop = destination(second);
    id<NSDraggingInfo> rejected = drag_info(first, both, NSMakePoint(11, 13), NSDragOperationCopy);
    id<NSDraggingInfo> accepted = drag_info(second, single, NSMakePoint(17, 19), NSDragOperationCopy);
    drain();
    check([first_drop prepareForDragOperation:rejected], "prepare drop while ring has room");
    for (int i = 0; i < 63; i++) zan_gui_inject_event(primary, 9, i, 0, 0, 0, 0);
    check(zan_gui_inject_pending() == 63, "ring has 63 usable event slots");
    check(![first_drop performDragOperation:rejected], "full ring rejects entire drop batch");
    check(zan_gui_inject_pending() == 63, "full-ring rejection preserves queued events");
    for (int i = 0; i < 63; i++) {
        next_event(9, primary);
        check(zan_gui_event_x() == i, "full ring preserves original event order");
        no_paths();
    }
    check([second_drop performDragOperation:accepted], "drop succeeds after full ring drains");
    next_event(9, peer);
    take_paths(@[[paths objectAtIndex:1]]);
    drain();

    for (int i = 0; i < 62; i++) zan_gui_inject_event(primary, 1, i, 0, 0, 0, 0);
    check([second_drop performDragOperation:accepted], "drop uses final free ring slot");
    check(![first_drop performDragOperation:rejected], "following drop is rejected without replacing batch");
    for (int i = 0; i < 62; i++) {
        next_event(1, primary);
        no_paths();
    }
    next_event(9, peer);
    take_paths(@[[paths objectAtIndex:1]]);
    drain();
}

static void test_glass(iptr primary, iptr peer, NSArray *paths, NSPasteboard *both) {
    NSWindow *window = (NSWindow *)primary;
    NSView *view = [[window contentView] retain];
    check(zan_gui_enable_glass(primary, 0) == 0, "enable native glass");
    check([window contentView] != view && [view superview] == [window contentView],
          "glass reparents drawing view under native effect view");
    check(zan_gui_resize_client(primary, 401, 301) == 0,
          "resize follows glass content and drawing subview");
    check(zan_gui_client_width(primary) == 401 && zan_gui_client_height(primary) == 301,
          "glass resize retains exact device dimensions");
    i32 peer_w = zan_gui_client_width(peer);
    i32 peer_h = zan_gui_client_height(peer);
    NSRect inset = NSInsetRect([[window contentView] bounds], 11, 17);
    [view setFrame:inset];
    drain();
    NSPoint local = NSMakePoint(23, 31);
    id<NSDraggingInfo> info = drag_info(view, both, local, NSDragOperationCopy);
    check([destination(view) performDragOperation:info], "drop reaches nested glass drawing view");
    next_event(9, primary);
    check(zan_gui_event_x() == pixels(local.x, window) &&
          zan_gui_event_y() == pixels(local.y, window), "nested drop uses view-local coordinates");
    take_paths(paths);
    check(zan_gui_client_width(peer) == peer_w && zan_gui_client_height(peer) == peer_h,
          "glass geometry leaves peer isolated");
    [view setFrame:[[window contentView] bounds]];
    check(zan_gui_disable_glass(primary) == 0 && [window contentView] == view,
          "disable glass restores drawing content view");
    check(zan_gui_resize_client(primary, 403, 303) == 0,
          "resize remains usable after removing glass");
    [view release];
    drain();
}

static void test_window_ownership(void) {
    /* Exercise close/destroy, destroy alone, and repeated destroy, each with
     * both a plain drawing view and an active glass wrapper. */
    for (int scenario = 0; scenario < 6; ++scenario) {
        int glass = scenario % 2;
        int teardown = scenario / 2;
        iptr hwnd = zan_gui_create_window("mac-window-ownership", 251, 181);
        check(hwnd != 0, "create ownership test window");
        i32 surface = -1;
        @try {
            @autoreleasepool {
                NSWindow *window = (NSWindow *)hwnd;
                NSView *view = [window contentView];
                watch_lifetime(window, &lifetime_window);
                watch_lifetime(view, &lifetime_view);
                surface = zan_gui_create_surface(251, 181);
                check(surface >= 0, "create surface for native backing ownership");
                check(zan_gui_present(hwnd, surface) == 0 && zan_gui_present(hwnd, surface) == 0,
                      "present both IOSurface buffers before closing");
                if (glass) {
                    check(zan_gui_enable_glass(hwnd, 0) == 0, "enable glass for ownership test");
                    watch_lifetime([window contentView], &lifetime_glass[0]);
                    check(zan_gui_disable_glass(hwnd) == 0, "disable glass releases wrapper");
                    check([window contentView] == view, "drawing view survives glass removal");
                    check(zan_gui_enable_glass(hwnd, 0) == 0, "enable another glass wrapper");
                    watch_lifetime([window contentView], &lifetime_glass[1]);
                    check(zan_gui_present(hwnd, surface) == 0, "drawing view survives glass replacement");
                }
                if (teardown == 0) {
                    check(zan_gui_close_window(hwnd) == 0, "close ownership test window");
                    check(zan_gui_destroy_window(hwnd) == 0, "destroy after close does not release twice");
                    check(zan_gui_close_window(hwnd) == 0, "repeated close is harmless");
                } else {
                    check(zan_gui_destroy_window(hwnd) == 0, "destroy without prior close tears down window");
                    if (teardown == 2) {
                        check(zan_gui_destroy_window(hwnd) == 0, "repeated destroy is harmless");
                    }
                }
                check(zan_gui_client_width(hwnd) == 0 && zan_gui_client_height(hwnd) == 0,
                      "teardown removes the window handle from the registry");
                hwnd = 0;
            }
            drain(); /* Drain autoreleased native objects before checking final ownership. */
            check(lifetime_window && lifetime_view, "close or destroy releases window and drawing view");
            if (glass) {
                check(lifetime_glass[0] && lifetime_glass[1],
                      "disable and teardown each release their glass wrapper");
            }
        } @finally {
            if (hwnd) zan_gui_close_window(hwnd);
            if (surface >= 0) zan_gui_destroy_surface(surface);
        }
    }
}

static void test_closed_windows(iptr primary, iptr peer, NSArray *paths,
                                NSPasteboard *both, NSPasteboard *single) {
    iptr doomed = zan_gui_create_window("mac-drop-closed", 250, 180);
    check(doomed != 0, "create window for queued close");
    NSView *view = [[(NSWindow *)doomed contentView] retain];
    id<NSDraggingDestination> target = destination(view);
    id<NSDraggingInfo> stale = drag_info(view, both, NSMakePoint(11, 13), NSDragOperationCopy);
    NSView *peer_view = [(NSWindow *)peer contentView];
    id<NSDraggingInfo> survivor = drag_info(peer_view, single, NSMakePoint(17, 19), NSDragOperationCopy);
    drain();
    check([target performDragOperation:stale], "queue drop before closing target");
    check([destination(peer_view) performDragOperation:survivor], "queue surviving peer batch");
    zan_gui_close_window(doomed);
    check(zan_gui_resize_client(doomed, 100, 100) == -1 &&
          zan_gui_set_window_pos(doomed, 1, 1) != 0, "closed geometry handle is rejected");
    check(![target performDragOperation:stale], "closed view cannot enqueue another drop");
    next_event(0, 0);
    no_paths();
    next_event(9, peer);
    take_paths(@[[paths objectAtIndex:1]]);
    [view release];
    drain();

    doomed = zan_gui_create_window("mac-drop-active-close", 250, 180);
    check(doomed != 0, "create window for active close");
    view = [(NSWindow *)doomed contentView];
    drain();
    check([destination(view) performDragOperation:drag_info(view, both,
        NSMakePoint(11, 13), NSDragOperationCopy)], "queue active-close batch");
    next_event(9, doomed);
    check(zan_gui_drop_pending(), "closing target has active paths");
    zan_gui_close_window(doomed);
    no_paths();
    check(zan_gui_resize_client(primary, 405, 305) == 0,
          "primary survives closing another drop target");
    drain();
}

int main(void) {
    @autoreleasepool {
        if ([[NSScreen screens] count] == 0) {
            fprintf(stderr, "SKIP: Cocoa window tests require a logged-in macOS display session\n");
            return 77;
        }
        int result = 1;
        iptr primary = 0;
        iptr peer = 0;
        NSString *directory = nil;
        pasteboards = [[NSMutableArray alloc] init];
        @try {
            check(zan_gui_resize_client(0, 100, 100) == -1,
                  "zero resize handle without windows fails");
            check(zan_gui_set_window_pos(0, 1, 1) != 0,
                  "zero position handle without windows fails");
            primary = zan_gui_create_window("mac-drop-primary", 320, 240);
            peer = zan_gui_create_window("mac-drop-peer", 280, 180);
            check(primary && peer && primary != peer, "create independent native windows");
            check(zan_gui_show_window(primary) == 0 && zan_gui_show_window(peer) == 0,
                  "show native test windows");
            drain();
            directory = [NSTemporaryDirectory() stringByAppendingPathComponent:
                [[NSProcessInfo processInfo] globallyUniqueString]];
            check([[NSFileManager defaultManager] createDirectoryAtPath:directory
                withIntermediateDirectories:NO attributes:nil error:NULL], "create native file fixtures");
            NSArray *paths = @[
                [directory stringByAppendingPathComponent:@"中文 空格 #%.txt"],
                [directory stringByAppendingPathComponent:@"second-😀.txt"]
            ];
            NSMutableArray *urls = [NSMutableArray array];
            for (NSString *path in paths) {
                check([[NSData dataWithBytes:"x" length:1] writeToFile:path atomically:NO],
                      "write actual UTF-8 file fixture");
                [urls addObject:[NSURL fileURLWithPath:path]];
            }
            NSPasteboard *both = make_pasteboard(urls);
            NSPasteboard *single = make_pasteboard(@[[urls objectAtIndex:1]]);
            test_geometry(primary, peer);
            test_child_creation_after_screen_change(primary, peer);
            test_batches(primary, peer, paths, both, single);
            test_capacity(primary, peer, paths, both, single);
            test_glass(primary, peer, paths, both);
            test_window_ownership();
            test_closed_windows(primary, peer, paths, both, single);
            puts("MAC_WINDOW_DROP_OK");
            result = 0;
        } @catch (NSException *ex) {
            fprintf(stderr, "FAIL: %s: %s\n", [[ex name] UTF8String], [[ex reason] UTF8String]);
        } @finally {
            if (peer) zan_gui_close_window(peer);
            if (primary) zan_gui_close_window(primary);
            for (NSPasteboard *board in pasteboards) [board releaseGlobally];
            [pasteboards release];
            if (directory) [[NSFileManager defaultManager] removeItemAtPath:directory error:NULL];
        }
        return result;
    }
}
