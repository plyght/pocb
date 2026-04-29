#include "MacIntegration.hpp"
#include "MacInternal.hpp"

#ifdef __APPLE__
#import <AppKit/AppKit.h>
#import <objc/runtime.h>
#import <objc/message.h>

#include <QMainWindow>
#include <QTimer>
#include <QWidget>

// =============================================================================
// Traffic-light positioning + sizing.
//
// Strategy: override private button-origin getters on _NSThemeFrame so AppKit
// places the buttons where we want during its own layout pass — no flicker,
// no fight loop, no after-the-fact correction. Hover (_mouseInGroup:) and
// per-cell sizing are handled the same way.
//
// Key safety constraint discovered the hard way: do NOT use object_setClass
// to per-instance subclass _NSThemeFrame. AppKit's NSDerivedProperty
// machinery (used heavily by WKWebView's corner-radii KVO and by titlebar
// internals) caches values keyed by Class identity and asserts when it sees
// a class it doesn't recognise. Per-instance subclassing the theme frame
// crashes inside _NSDP_getComputedPropertyValue → __assert_rtn → SIGABRT.
//
// Instead: swizzle methods directly on the real class via
// method_setImplementation. Class identity is preserved, NSDP is happy.
// Each override gates on an associated-object marker so we only affect our
// windows; other windows in the process see the original behaviour.
// =============================================================================

static char kPocbWindowEnabledKey;
static char kPocbCellEnabledKey;
static char kPocbTrackerKey;

@interface PocbHoverTracker : NSObject {
@public
    __weak NSWindow *window;
    NSTrackingArea  *area;
}
@end
@implementation PocbHoverTracker
// macOS 26 (Tahoe) requires poking these private _NSThemeWidget methods on
// each traffic-light button when the mouse enters/exits the group rect.
// Otherwise glyphs get stuck on / refuse to appear. (iTerm2 commit 9af0242,
// Nov 2025.)
- (void)pokeButtons:(BOOL)entered {
    NSWindow *w = window;
    if (!w) return;
    NSWindowButton kinds[3] = { NSWindowCloseButton, NSWindowMiniaturizeButton, NSWindowZoomButton };
    SEL sMouse = sel_registerName("mouseEnteredOrExited");
    SEL sStart = sel_registerName("startMonitoringFlagsChanged");
    SEL sStop  = sel_registerName("stopMonitoringFlagsChanged");
    for (int i = 0; i < 3; ++i) {
        NSButton *b = [w standardWindowButton:kinds[i]];
        if (!b) continue;
        if ([b respondsToSelector:sMouse]) {
            ((void (*)(id, SEL))objc_msgSend)(b, sMouse);
        }
        SEL flag = entered ? sStart : sStop;
        if ([b respondsToSelector:flag]) {
            ((void (*)(id, SEL))objc_msgSend)(b, flag);
        }
        [b setNeedsDisplay:YES];
    }
}
- (void)mouseEntered:(NSEvent *)e { (void)e; [self pokeButtons:YES]; }
- (void)mouseExited:(NSEvent *)e  { (void)e; [self pokeButtons:NO];  }

// iTerm2 pattern: force a redraw of the button cells when app/window focus
// state changes, so the buttons properly grey-out on resign and re-colour on
// become-key. Without this, AppKit's stock path can miss the redraw when
// the cells live behind our swizzles.
- (void)focusChanged {
    NSWindow *w = window;
    if (!w) return;
    NSWindowButton kinds[3] = { NSWindowCloseButton, NSWindowMiniaturizeButton, NSWindowZoomButton };
    for (int i = 0; i < 3; ++i) {
        [[w standardWindowButton:kinds[i]] setNeedsDisplay:YES];
    }
}
@end

// Tunables — read by the swizzled methods.
static CGFloat g_originX        = 20.0;
static CGFloat g_originY_topPad = 18.0;
static CGFloat g_spacing        = 26.0;
static CGFloat g_buttonW        = 16.0;
static CGFloat g_buttonH        = 16.0;

// -----------------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------------

static BOOL pocb_themeframe_enabled(NSView *tf) {
    return tf && tf.window &&
           objc_getAssociatedObject(tf.window, &kPocbWindowEnabledKey) != nil;
}

static NSPoint pocb_button_origin(NSView *tf, NSInteger idx) {
    const CGFloat fh = NSHeight(tf.frame);
    const CGFloat x  = g_originX + (CGFloat)idx * g_spacing;
    const CGFloat y  = tf.isFlipped
                           ? g_originY_topPad
                           : (fh - g_originY_topPad - g_buttonH);
    return NSMakePoint(x, y);
}

static NSRect pocb_button_group_rect(NSView *tf) {
    const CGFloat fh = NSHeight(tf.frame);
    // Group rect = exactly the buttons' total span, no padding (matches
    // iTerm2's iTermStandardWindowButtonsView.bounds — native feel).
    const CGFloat groupW = 2.0 * g_spacing + g_buttonW;
    const CGFloat x = g_originX;
    const CGFloat y = tf.isFlipped
                          ? g_originY_topPad
                          : (fh - g_originY_topPad - g_buttonH);
    return NSMakeRect(x, y, groupW, g_buttonH);
}

// -----------------------------------------------------------------------------
// Theme-frame method swizzles (class-level, gated per-window)
// -----------------------------------------------------------------------------

typedef NSPoint (*PocbPointIMP)(id, SEL);
typedef BOOL    (*PocbMouseInGroupIMP)(id, SEL, NSButton *);

struct OriginHook {
    SEL          sel;
    NSInteger    btnIdx;
    PocbPointIMP orig;
    IMP          replacement;
};

static OriginHook g_origin_hooks[12];   // up to 12 candidate selectors
static int        g_origin_hook_count = 0;
static PocbMouseInGroupIMP g_orig_mouseInGroup = NULL;
static BOOL g_themeframe_swizzled = NO;

// Generic dispatcher: looks up which slot we are by SEL, runs ours if
// enabled, otherwise calls cached original.
static NSPoint pocb_origin_dispatch(id self, SEL _cmd, NSInteger idx) {
    NSView *tf = (NSView *)self;
    if (pocb_themeframe_enabled(tf)) {
        return pocb_button_origin(tf, idx);
    }
    for (int i = 0; i < g_origin_hook_count; ++i) {
        if (g_origin_hooks[i].sel == _cmd && g_origin_hooks[i].orig) {
            return g_origin_hooks[i].orig(self, _cmd);
        }
    }
    return NSZeroPoint;
}

static NSPoint pocb_origin_close(id self, SEL _cmd) { return pocb_origin_dispatch(self, _cmd, 0); }
static NSPoint pocb_origin_min  (id self, SEL _cmd) { return pocb_origin_dispatch(self, _cmd, 1); }
static NSPoint pocb_origin_zoom (id self, SEL _cmd) { return pocb_origin_dispatch(self, _cmd, 2); }

static BOOL pocb_themeframe_mouseInGroup(id self, SEL _cmd, NSButton *button) {
    NSView *tf = (NSView *)self;
    if (pocb_themeframe_enabled(tf)) {
        NSPoint mouseInWindow = [tf.window mouseLocationOutsideOfEventStream];
        NSPoint local = [tf convertPoint:mouseInWindow fromView:nil];
        if (NSPointInRect(local, pocb_button_group_rect(tf))) return YES;
    }
    if (g_orig_mouseInGroup) return g_orig_mouseInGroup(self, _cmd, button);
    return NO;
}

static void swizzleThemeFrameOnce(NSView *themeFrame) {
    if (g_themeframe_swizzled) return;
    Class cls = object_getClass(themeFrame);
    NSLog(@"[pocb-tl] themeFrame class: %@", NSStringFromClass(cls));

    struct { const char *name; NSInteger idx; IMP repl; } cands[] = {
        { "_closeButtonOrigin",          0, (IMP)pocb_origin_close },
        { "_minimizeButtonOrigin",       1, (IMP)pocb_origin_min   },
        { "_zoomButtonOrigin",           2, (IMP)pocb_origin_zoom  },
        { "_closeButtonOriginInRect:",   0, (IMP)pocb_origin_close },
        { "_minimizeButtonOriginInRect:",1, (IMP)pocb_origin_min   },
        { "_zoomButtonOriginInRect:",    2, (IMP)pocb_origin_zoom  },
    };
    for (size_t i = 0; i < sizeof(cands)/sizeof(cands[0]); ++i) {
        SEL s = sel_registerName(cands[i].name);
        Method m = class_getInstanceMethod(cls, s);
        if (!m) continue;
        IMP orig = method_setImplementation(m, cands[i].repl);
        g_origin_hooks[g_origin_hook_count++] = (OriginHook){
            s, cands[i].idx, (PocbPointIMP)orig, cands[i].repl
        };
        NSLog(@"[pocb-tl] swizzled %s on %@", cands[i].name, NSStringFromClass(cls));
    }

    SEL mig = @selector(_mouseInGroup:);
    Method mm = class_getInstanceMethod(cls, mig);
    if (mm) {
        g_orig_mouseInGroup = (PocbMouseInGroupIMP)method_setImplementation(mm, (IMP)pocb_themeframe_mouseInGroup);
        NSLog(@"[pocb-tl] swizzled _mouseInGroup: on %@", NSStringFromClass(cls));
    }

    NSLog(@"[pocb-tl] themeFrame swizzle: %d origin getters hooked", g_origin_hook_count);
    g_themeframe_swizzled = YES;
}

// -----------------------------------------------------------------------------
// Cell size swizzle (class-level, gated per-cell)
// -----------------------------------------------------------------------------

typedef NSSize (*PocbCellSizeIMP)(id, SEL);
struct CellHook {
    Class           cls;
    PocbCellSizeIMP orig;
};
static CellHook g_cell_hooks[8];
static int      g_cell_hook_count = 0;

static PocbCellSizeIMP pocb_find_cell_orig(Class c) {
    for (int i = 0; i < g_cell_hook_count; ++i) {
        if (g_cell_hooks[i].cls == c) return g_cell_hooks[i].orig;
    }
    return NULL;
}

static NSSize pocb_cell_size(id self, SEL _cmd) {
    NSCell *cell = (NSCell *)self;
    if (objc_getAssociatedObject(cell, &kPocbCellEnabledKey) != nil) {
        return NSMakeSize(g_buttonW, g_buttonH);
    }
    PocbCellSizeIMP orig = pocb_find_cell_orig(object_getClass(cell));
    if (orig) return orig(self, _cmd);
    return NSMakeSize(14, 14);
}

static void swizzleCellClassOnce(Class cls) {
    if (!cls) return;
    for (int i = 0; i < g_cell_hook_count; ++i) {
        if (g_cell_hooks[i].cls == cls) return;
    }
    Method m = class_getInstanceMethod(cls, @selector(cellSize));
    if (!m) return;
    IMP orig = method_setImplementation(m, (IMP)pocb_cell_size);
    g_cell_hooks[g_cell_hook_count++] = (CellHook){ cls, (PocbCellSizeIMP)orig };
    NSLog(@"[pocb-tl] swizzled -cellSize on %@", NSStringFromClass(cls));
}

static void enableCell(NSButton *btn) {
    if (!btn) return;
    NSCell *cell = btn.cell;
    if (!cell) return;
    swizzleCellClassOnce(object_getClass(cell));
    objc_setAssociatedObject(cell, &kPocbCellEnabledKey, @YES, OBJC_ASSOCIATION_RETAIN);
}

// -----------------------------------------------------------------------------
// Apply
// -----------------------------------------------------------------------------

static void apply(QMainWindow *win) {
    NSWindow *nsw = mac::internal::nsWindowOf(win);
    if (!nsw) return;

    nsw.titleVisibility = NSWindowTitleHidden;
    nsw.titlebarAppearsTransparent = YES;
    if (nsw.toolbar) nsw.toolbar = nil;
    nsw.styleMask |= NSWindowStyleMaskFullSizeContentView;

    NSView *themeFrame = nsw.contentView.superview;
    if (themeFrame) swizzleThemeFrameOnce(themeFrame);

    objc_setAssociatedObject(nsw, &kPocbWindowEnabledKey, @YES, OBJC_ASSOCIATION_RETAIN);

    // Install / refresh a tracking area on the theme frame for the button
    // group rect. On enter/exit we invalidate the buttons so AppKit re-runs
    // the cell draw path (which calls our _mouseInGroup:), which fixes the
    // "glyphs stuck on after mouse leaves" bug.
    if (themeFrame) {
        PocbHoverTracker *t = objc_getAssociatedObject(themeFrame, &kPocbTrackerKey);
        if (!t) {
            t = [[PocbHoverTracker alloc] init];
            t->window = nsw;
            objc_setAssociatedObject(themeFrame, &kPocbTrackerKey, t, OBJC_ASSOCIATION_RETAIN);
            NSNotificationCenter *nc = NSNotificationCenter.defaultCenter;
            for (NSNotificationName n in @[ NSApplicationDidBecomeActiveNotification,
                                            NSApplicationDidResignActiveNotification,
                                            NSWindowDidBecomeKeyNotification,
                                            NSWindowDidResignKeyNotification ]) {
                [nc addObserver:t selector:@selector(focusChanged) name:n object:nil];
            }
        }
        if (t->area) {
            [themeFrame removeTrackingArea:t->area];
            t->area = nil;
        }
        NSRect r = pocb_button_group_rect(themeFrame);
        t->area = [[NSTrackingArea alloc]
                       initWithRect:r
                            options:(NSTrackingMouseEnteredAndExited |
                                     NSTrackingActiveAlways)
                              owner:t
                           userInfo:nil];
        [themeFrame addTrackingArea:t->area];
    }

    NSButton *close = [nsw standardWindowButton:NSWindowCloseButton];
    NSButton *mini  = [nsw standardWindowButton:NSWindowMiniaturizeButton];
    NSButton *zoom  = [nsw standardWindowButton:NSWindowZoomButton];
    for (NSButton *b in @[ close ?: NSNull.null, mini ?: NSNull.null, zoom ?: NSNull.null ]) {
        if (![b isKindOfClass:[NSButton class]]) continue;
        enableCell(b);
        NSRect f = b.frame;
        f.size.width = g_buttonW; f.size.height = g_buttonH;
        [b setFrame:f];
    }

    [themeFrame setNeedsLayout:YES];
    [themeFrame setNeedsDisplay:YES];

    NSLog(@"[pocb-tl] apply: close=%@ mini=%@ zoom=%@",
          NSStringFromRect(close.frame),
          NSStringFromRect(mini.frame),
          NSStringFromRect(zoom.frame));
}

#endif

namespace mac {

void integrateUnifiedToolbar(QMainWindow *win, QWidget *toolbarRow, bool compact) {
#ifdef __APPLE__
    (void)toolbarRow; (void)compact;
    if (!win) return;

    g_originX        = 20.0;
    g_originY_topPad = 14.0;
    g_spacing        = 23.0;
    g_buttonW        = 17.0;
    g_buttonH        = 17.0;

    auto run = [win] { apply(win); };
    run();
    QTimer::singleShot(0,   win, run);
    QTimer::singleShot(50,  win, run);
#else
    (void)win; (void)toolbarRow; (void)compact;
#endif
}

void refreshUnifiedToolbar(QWidget *window) {
#ifdef __APPLE__
    if (!window) return;
    NSWindow *nsw = mac::internal::nsWindowOf(window);
    if (!nsw) return;
    NSView *themeFrame = nsw.contentView.superview;
    [themeFrame setNeedsLayout:YES];
    [themeFrame setNeedsDisplay:YES];
#else
    (void)window;
#endif
}

}  // namespace mac
