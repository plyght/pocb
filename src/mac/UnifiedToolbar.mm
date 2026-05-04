#include "MacIntegration.hpp"
#include "MacInternal.hpp"

#ifdef __APPLE__
#import <AppKit/AppKit.h>
#import <QuartzCore/QuartzCore.h>
#import <objc/runtime.h>
#import <objc/message.h>

#include <QMainWindow>
#include <QTimer>
#include <QWidget>

// =============================================================================
// Traffic-light positioning + sizing — iTerm2-style reparent approach.
//
// Why this path: on macOS 26 (Tahoe) the live traffic-light buttons in the
// theme frame are governed by AppKit private layout that hardcodes
// mini.x = close.x + 23 and resists external setFrame calls. The only way
// to get arbitrary spacing AND arbitrary size with a non-flickery, native-
// looking result is the iTerm2 / Chrome / Brave path:
//
//   1. Use +[NSWindow standardWindowButton:forStyleMask:] (the CLASS method)
//      to create FRESH NSButton instances with the same cell classes the
//      system uses (_NSThemeCloseWidgetCell, _NSThemeWidgetCell,
//      _NSThemeZoomWidgetCell). These are not the live theme-frame buttons,
//      so AppKit's titlebar layout doesn't touch them.
//   2. Add the fresh buttons to a custom container view we own, positioned
//      where we want.
//   3. Hide the originals via -[NSWindow standardWindowButton:].hidden = YES.
//   4. Wire close/min/zoom targets back to the window. Zoom needs special
//      handling because option-click-zoom checks _lastLeftHit against the
//      *original* zoom button — so option+click triggers toggleFullScreen
//      explicitly to avoid the broken path. (Same as iTerm2.)
//   5. Tracking area + _mouseInGroup: + the Tahoe mouseEnteredOrExited /
//      startMonitoringFlagsChanged pokes — so glyphs appear/disappear and
//      Option-key swap (zoom→maximize) works.
//
// Visual size: _NSThemeWidget paints at a fixed ~14pt logical size. We keep
// cellSize / frames at that nominal size (centered in each g_buttonWH slot)
// and apply CALayer scale so the drawn dots actually grow — scaling only the
// outer g_buttonWH×g_buttonWH frame left empty padding (user-visible bug).
// =============================================================================

static char kPocbContainerKey;
static char kPocbQMainWindowKey;
static char kPocbTrafficOffsetKey;

// Cell-size swizzle: _NSThemeCloseWidgetCell inherits -cellSize from
// _NSThemeWidgetCell, so class_getInstanceMethod(closeCell, cellSize) and
// class_getInstanceMethod(widgetCell, cellSize) return the *same* Method*.
// Swizzling twice with method_setImplementation stores our IMP as "orig" the
// second time → infinite recursion (seen when miniaturize: allocates a new
// widget and calls -sizeToFit → -cellSize). Fix: swizzle each unique Method*
// exactly once and always forward to the IMP we captured on first install.
typedef NSSize (*PocbCellSizeIMP)(id, SEL);
static char kPocbCellOnKey;

// Key: Method* as opaque pointer. Value: NSNumber wrapping uint64_t of orig IMP.
static NSMutableDictionary<NSValue *, NSValue *> *g_cellSizeOrigIMPByMethod;

extern CGFloat g_buttonWH;
// Theme widgets paint ~14×14; we center that in each slot and scale the layer.
static const CGFloat kPocbTrafficNativeDrawWH = 14.0;

static NSSize pocb_cell_size(id self, SEL _cmd) {
    NSCell *cell = (NSCell *)self;
    if (objc_getAssociatedObject(cell, &kPocbCellOnKey)) {
        return NSMakeSize(kPocbTrafficNativeDrawWH, kPocbTrafficNativeDrawWH);
    }
    Method m = class_getInstanceMethod(object_getClass((id)cell), _cmd);
    if (!m) return NSMakeSize(14, 14);
    NSValue *mKey = [NSValue valueWithPointer:m];
    NSValue *origBox = g_cellSizeOrigIMPByMethod[mKey];
    if (!origBox) {
        PocbCellSizeIMP imp = (PocbCellSizeIMP)method_getImplementation(m);
        return imp(cell, _cmd);
    }
    PocbCellSizeIMP orig = (PocbCellSizeIMP)origBox.pointerValue;
    return orig(cell, _cmd);
}

static void pocb_swizzle_cell_size_if_needed(Class cls) {
    if (!cls) return;
    Method m = class_getInstanceMethod(cls, @selector(cellSize));
    if (!m) return;
    NSValue *mKey = [NSValue valueWithPointer:m];
    if (g_cellSizeOrigIMPByMethod[mKey]) return;

    if (!g_cellSizeOrigIMPByMethod)
        g_cellSizeOrigIMPByMethod = [NSMutableDictionary dictionary];
    IMP realOrig = method_getImplementation(m);
    g_cellSizeOrigIMPByMethod[mKey] = [NSValue valueWithPointer:(void *)realOrig];
    method_setImplementation(m, (IMP)pocb_cell_size);
    NSLog(@"[pocb-tl] swizzled cellSize once for Method %p (class query %@)",
          m, NSStringFromClass(cls));
}

static void pocb_enable_cell(NSButton *btn) {
    if (!btn) return;
    NSCell *c = btn.cell;
    if (!c) return;
    Class cellClass = object_getClass((id)c);
    pocb_swizzle_cell_size_if_needed(cellClass);
    objc_setAssociatedObject(c, &kPocbCellOnKey, @YES, OBJC_ASSOCIATION_RETAIN);
}

// AppKit restores the live titlebar buttons after fullscreen transitions;
// keep them visually hidden (alpha only — see comment in apply()).
static void pocb_hide_native_traffic_lights(NSWindow *nsw) {
    if (!nsw) return;
    [[nsw standardWindowButton:NSWindowCloseButton]       setAlphaValue:0.0];
    [[nsw standardWindowButton:NSWindowMiniaturizeButton] setAlphaValue:0.0];
    [[nsw standardWindowButton:NSWindowZoomButton]        setAlphaValue:0.0];
}

@interface PocbWindowButtonsView : NSView
- (instancetype)initWithButtons:(NSArray<NSButton *> *)buttons
                         window:(NSWindow *)window
                       buttonWH:(CGFloat)wh
                        spacing:(CGFloat)spacing;
/// Updates layout metrics from globals; call after changing g_buttonWH / g_spacing.
- (void)setButtonMetricsWH:(CGFloat)wh spacing:(CGFloat)spacing;
- (void)layoutButtons;
@end

@implementation PocbWindowButtonsView {
    NSArray<NSButton *> *_buttons;
    __weak NSWindow     *_window;
    NSTrackingArea      *_trackingArea;
    BOOL                 _mouseInGroup;
    CGFloat              _buttonWH;
    CGFloat              _spacing;
}

- (instancetype)initWithButtons:(NSArray<NSButton *> *)buttons
                         window:(NSWindow *)window
                       buttonWH:(CGFloat)wh
                        spacing:(CGFloat)spacing {
    const CGFloat w = (CGFloat)(buttons.count > 0 ? buttons.count - 1 : 0) * spacing + wh;
    self = [super initWithFrame:NSMakeRect(0, 0, w, wh)];
    if (!self) return nil;
    _buttons = buttons;
    _window  = window;
    _buttonWH = wh;
    _spacing = spacing;
    for (NSButton *b in buttons) {
        [self addSubview:b];
    }
    [self layoutButtons];

    // Factory actions are private (_close:, _setNeedsZoom:) and on Tahoe they
    // often verify sender == [window standardWindowButton:…]. Our fresh
    // buttons fail that check, so clicks are swallowed. iTerm2 routes zoom
    // through the container; we route all three through public NSWindow APIs.
    if (buttons.count >= 3) {
        NSButton *close = buttons[0];
        NSButton *mini  = buttons[1];
        NSButton *zoom  = buttons[2];
        close.target = self; close.action = @selector(pocbClose:);
        mini.target  = self; mini.action  = @selector(pocbMini:);
        zoom.target  = self; zoom.action  = @selector(pocbZoom:);
    }

    NSNotificationCenter *nc = NSNotificationCenter.defaultCenter;
    for (NSNotificationName n in @[ NSApplicationDidBecomeActiveNotification,
                                    NSApplicationDidResignActiveNotification,
                                    NSWindowDidBecomeKeyNotification,
                                    NSWindowDidResignKeyNotification ]) {
        [nc addObserver:self selector:@selector(redraw) name:n object:nil];
    }
    [nc addObserver:self selector:@selector(windowResized:)
               name:NSWindowDidResizeNotification object:window];
    for (NSNotificationName n in @[ NSWindowDidExitFullScreenNotification,
                                    NSWindowDidEnterFullScreenNotification ]) {
        [nc addObserver:self selector:@selector(windowFullScreenTransition:)
                   name:n object:window];
    }
    return self;
}

- (void)dealloc {
    [NSNotificationCenter.defaultCenter removeObserver:self];
}

- (void)setButtonMetricsWH:(CGFloat)wh spacing:(CGFloat)spacing {
    _buttonWH = wh;
    _spacing = spacing;
    [self layoutButtons];
    for (NSButton *b in _buttons)
        [b setNeedsDisplay:YES];
    [self setNeedsDisplay:YES];
}

- (void)layoutButtons {
    // Native paint requires 14×14 frame; the visible disc grows to fill
    // _buttonWH via CALayer scale. Hit-testing handled at container level
    // (see hitTest:/mouseDown:) so the full slot is clickable/hoverable.
    const CGFloat n = kPocbTrafficNativeDrawWH;
    const CGFloat scale = MAX(0.35, MIN(5.0, _buttonWH / n));
    NSInteger i = 0;
    for (NSButton *b in _buttons) {
        const CGFloat cx = (CGFloat)i * _spacing + _buttonWH * 0.5;
        const CGFloat cy = _buttonWH * 0.5;
        b.frame = NSMakeRect(cx - n * 0.5, cy - n * 0.5, n, n);
        b.wantsLayer = YES;
        // Don't touch layer.anchorPoint: NSView keeps it at (0,0) and
        // synchronises layer.position with frame.origin. Changing it to
        // (0.5,0.5) shifts the rendered layer by -(n/2, n/2), so the visible
        // disc no longer overlaps the click/hover slot. Scale around the
        // layer's geometric centre with a translate-scale-translate transform
        // instead, which leaves position/anchorPoint untouched.
        const CGFloat tx = n * 0.5;
        const CGFloat ty = n * 0.5;
        CATransform3D t = CATransform3DIdentity;
        t = CATransform3DTranslate(t, tx, ty, 0);
        t = CATransform3DScale(t, scale, scale, 1.0);
        t = CATransform3DTranslate(t, -tx, -ty, 0);
        b.layer.transform = t;
        i++;
    }
}

- (void)redraw {
    for (NSView *sub in self.subviews) [sub setNeedsDisplay:YES];
}

- (void)windowResized:(NSNotification *)n {
    (void)n;
    NSWindow *w = _window;
    if (!w) return;
    NSView *cv = w.contentView;
    extern CGFloat g_originY_topPad;
    NSRect f = self.frame;
    f.origin.y = cv.isFlipped ? g_originY_topPad
                              : (NSHeight(cv.frame) - g_originY_topPad - _buttonWH);
    self.frame = f;
}

- (void)windowFullScreenTransition:(NSNotification *)n {
    (void)n;
    NSWindow *w = _window;
    if (!w) return;
    pocb_hide_native_traffic_lights(w);
    NSView *cv = w.contentView;
    if (!cv) return;
    [cv addSubview:self positioned:NSWindowAbove relativeTo:nil];
    extern CGFloat g_originY_topPad;
    NSRect f = self.frame;
    f.origin.y = cv.isFlipped ? g_originY_topPad
                              : (NSHeight(cv.frame) - g_originY_topPad - _buttonWH);
    self.frame = f;
}

// AppKit asks the buttons' superview whether the mouse is in the button
// group, to decide whether to draw the colored glyphs.
- (BOOL)_mouseInGroup:(NSButton *)button {
    (void)button;
    return _mouseInGroup;
}

- (NSButton *)buttonAtSuperviewPoint:(NSPoint)point {
    const NSPoint local = [self convertPoint:point fromView:self.superview];
    NSInteger i = 0;
    for (NSButton *b in _buttons) {
        const NSRect slot = NSMakeRect((CGFloat)i * _spacing, 0,
                                       _buttonWH, _buttonWH);
        if (NSPointInRect(local, slot)) return b;
        i++;
    }
    return nil;
}

// Buttons are 14×14 under a layer scale; redirect every event in the slot
// rect to us so click + drag tracking matches the visible disc, not the
// underlying frame.
- (NSView *)hitTest:(NSPoint)point {
    if (self.alphaValue == 0 || self.hidden) return nil;
    return [self buttonAtSuperviewPoint:point] ? self : nil;
}

- (BOOL)acceptsFirstMouse:(NSEvent *)event { (void)event; return YES; }

- (void)mouseDown:(NSEvent *)event {
    const NSPoint inSuper =
        [self.superview convertPoint:event.locationInWindow fromView:nil];
    NSButton *target = [self buttonAtSuperviewPoint:inSuper];
    if (!target) return;

    target.highlighted = YES;
    [target setNeedsDisplay:YES];

    const NSEventMask mask =
        NSEventMaskLeftMouseDragged | NSEventMaskLeftMouseUp;
    while (1) {
        // -[NSWindow nextEventMatchingMask:] doesn't exist; modal dispatch is
        // on NSApplication. Without this, the loop returned nil and the click
        // action never fired.
        NSEvent *e = [NSApp nextEventMatchingMask:mask
                                        untilDate:[NSDate distantFuture]
                                           inMode:NSEventTrackingRunLoopMode
                                          dequeue:YES];
        if (!e) break;
        const NSPoint p =
            [self.superview convertPoint:e.locationInWindow fromView:nil];
        const BOOL inside = ([self buttonAtSuperviewPoint:p] == target);
        if (target.highlighted != inside) {
            target.highlighted = inside;
            [target setNeedsDisplay:YES];
        }
        if (e.type == NSEventTypeLeftMouseUp) {
            target.highlighted = NO;
            [target setNeedsDisplay:YES];
            if (inside && target.action) {
                [NSApp sendAction:target.action to:target.target from:target];
            }
            break;
        }
    }
}

- (void)updateTrackingAreas {
    [super updateTrackingAreas];
    if (_trackingArea) {
        [self removeTrackingArea:_trackingArea];
        _trackingArea = nil;
    }
    // Match iTerm2 iTermStandardWindowButtonsView — no InVisibleRect.
    _trackingArea = [[NSTrackingArea alloc]
                         initWithRect:self.bounds
                              options:(NSTrackingMouseEnteredAndExited |
                                       NSTrackingActiveAlways)
                                owner:self
                             userInfo:nil];
    [self addTrackingArea:_trackingArea];
}

- (void)setShowIcons:(BOOL)v {
    if (_mouseInGroup == v) return;
    _mouseInGroup = v;
    [self redraw];
}

- (void)pokeTahoe:(BOOL)entered {
    SEL sMouse = sel_registerName("mouseEnteredOrExited");
    SEL sStart = sel_registerName("startMonitoringFlagsChanged");
    SEL sStop  = sel_registerName("stopMonitoringFlagsChanged");
    for (NSButton *b in _buttons) {
        if ([b respondsToSelector:sMouse]) ((void(*)(id,SEL))objc_msgSend)(b, sMouse);
        SEL flag = entered ? sStart : sStop;
        if ([b respondsToSelector:flag]) ((void(*)(id,SEL))objc_msgSend)(b, flag);
    }
}

- (void)mouseEntered:(NSEvent *)e {
    [super mouseEntered:e];
    [self setShowIcons:YES];
    [self pokeTahoe:YES];
}

- (void)mouseExited:(NSEvent *)e {
    [super mouseExited:e];
    [self setShowIcons:NO];
    [self pokeTahoe:NO];
}

- (void)pocbClose:(id)sender {
    NSWindow *w = self.window;
    if (!w) return;
    // QNSWindow's performClose: often never reaches Qt — route through the
    // QMainWindow so closeEvent / geometry save run.
    NSValue *qv = objc_getAssociatedObject(w, &kPocbQMainWindowKey);
    if (qv) {
        QMainWindow *qm = static_cast<QMainWindow *>(qv.pointerValue);
        if (qm) {
            qm->close();
            return;
        }
    }
    [w performClose:sender];
}

- (void)pocbMini:(id)sender {
    [self.window miniaturize:sender];
}

- (void)pocbZoom:(id)sender {
    NSWindow *w = self.window;
    if (!w) return;
    // Match iTerm2: green = native fullscreen; Option+green = classic zoom
    // (maximize / zoom rect), not the other way around.
    if (([NSEvent modifierFlags] & NSEventModifierFlagOption) != 0) {
        [w zoom:sender];
    } else {
        [w toggleFullScreen:sender];
    }
}

@end

// =============================================================================
// Apply
// =============================================================================

CGFloat g_originX        = 20.0;
CGFloat g_originY_topPad = 16.0;
CGFloat g_spacing        = 22.0;
CGFloat g_buttonWH       = 16.0;

static void pocb_sync_traffic_container(NSWindow *nsw, PocbWindowButtonsView *container) {
    if (!nsw || !container) return;
    [container setButtonMetricsWH:g_buttonWH spacing:g_spacing];
    NSView *cv = nsw.contentView;
    const CGFloat cvH = NSHeight(cv.frame);
    const CGFloat rowW = (CGFloat)container.subviews.count > 0
                         ? (((CGFloat)container.subviews.count - 1.0) * g_spacing + g_buttonWH)
                         : g_buttonWH;
    NSValue *offsetValue = objc_getAssociatedObject(nsw, &kPocbTrafficOffsetKey);
    NSPoint offset = offsetValue ? offsetValue.pointValue : NSMakePoint(0.0, 0.0);
    NSRect cf;
    cf.origin.x = g_originX + offset.x;
    cf.origin.y = cv.isFlipped ? (g_originY_topPad + offset.y)
                              : (cvH - g_originY_topPad - offset.y - g_buttonWH);
    cf.size.width = rowW;
    cf.size.height = g_buttonWH;
    container.frame = cf;
    [container updateTrackingAreas];
}

static void apply(QMainWindow *win) {
    NSWindow *nsw = mac::internal::nsWindowOf(win);
    if (!nsw) return;

    objc_setAssociatedObject(nsw, &kPocbQMainWindowKey,
                             [NSValue valueWithPointer:(void *)win],
                             OBJC_ASSOCIATION_RETAIN_NONATOMIC);

    nsw.titleVisibility = NSWindowTitleHidden;
    nsw.titlebarAppearsTransparent = YES;
    if (nsw.toolbar) nsw.toolbar = nil;
    nsw.styleMask |= NSWindowStyleMaskFullSizeContentView;

    // Hide the live traffic-light buttons in the theme frame. Their public
    // accessors continue to function; only the visuals are suppressed.
    // setHidden:YES on the live close/mini buttons propagates to our fresh
    // copies on Tahoe (some shared state we can't see). Use alphaValue=0
    // instead — purely visual, doesn't propagate. Buttons remain in their
    // theme-frame hierarchy so AppKit's internal layout is undisturbed.
    pocb_hide_native_traffic_lights(nsw);

    PocbWindowButtonsView *container = objc_getAssociatedObject(nsw, &kPocbContainerKey);
    if (!container) {
        const NSUInteger sm = nsw.styleMask;
        NSButton *close = [NSWindow standardWindowButton:NSWindowCloseButton       forStyleMask:sm];
        NSButton *mini  = [NSWindow standardWindowButton:NSWindowMiniaturizeButton forStyleMask:sm];
        NSButton *zoom  = [NSWindow standardWindowButton:NSWindowZoomButton        forStyleMask:sm];
        NSLog(@"[pocb-tl] fresh: close=%@ mini=%@ zoom=%@", close, mini, zoom);
        if (!close || !mini || !zoom) {
            NSLog(@"[pocb-tl] standardWindowButton:forStyleMask: returned nil");
            return;
        }
        // If the factory returned the LIVE (already-onscreen) buttons rather
        // than fresh copies, hiding the natives later would also hide ours.
        // Detect that and bail out so the user sees the originals instead of
        // ghost-hidden ones.
        NSButton *liveClose = [nsw standardWindowButton:NSWindowCloseButton];
        NSLog(@"[pocb-tl] live close=%p, fresh close=%p (same? %d)",
              (__bridge void *)liveClose, (__bridge void *)close, liveClose == close);

        pocb_enable_cell(close);
        pocb_enable_cell(mini);
        pocb_enable_cell(zoom);

        container = [[PocbWindowButtonsView alloc]
                         initWithButtons:@[ close, mini, zoom ]
                                  window:nsw
                                buttonWH:g_buttonWH
                                 spacing:g_spacing];
        // Anchor to top-left of the window's content view.
        container.autoresizingMask = NSViewMaxXMargin | NSViewMinYMargin;
        objc_setAssociatedObject(nsw, &kPocbContainerKey, container, OBJC_ASSOCIATION_RETAIN);

        // Add as the topmost subview of the contentView. Because we use
        // NSWindowStyleMaskFullSizeContentView, the contentView spans the
        // entire window including the titlebar region.
        [nsw.contentView addSubview:container
                          positioned:NSWindowAbove
                          relativeTo:nil];
    }

    NSView *cv = nsw.contentView;
    pocb_sync_traffic_container(nsw, container);
    // Qt may reorder contentView subviews during layout; stay above WebKit/Qt.
    [cv addSubview:container positioned:NSWindowAbove relativeTo:nil];

    NSLog(@"[pocb-tl] apply: container=%@ subviews=%lu",
          NSStringFromRect(container.frame), (unsigned long)container.subviews.count);
    for (NSView *s in container.subviews) {
        NSLog(@"[pocb-tl]   sub: %@ frame=%@ hidden=%d alpha=%f",
              NSStringFromClass([s class]), NSStringFromRect(s.frame), s.isHidden, s.alphaValue);
    }
}

#endif

namespace mac {

void integrateUnifiedToolbar(QMainWindow *win, QWidget *toolbarRow, bool compact) {
#ifdef __APPLE__
    (void)toolbarRow; (void)compact;
    if (!win) return;
    // g_originX, g_originY_topPad, g_spacing, g_buttonWH are file-level
    // tunables; do not reset them here or edits never stick.

    auto run = [win] { apply(win); };
    run();
    QTimer::singleShot(0,  win, run);
    QTimer::singleShot(50, win, run);
#else
    (void)win; (void)toolbarRow; (void)compact;
#endif
}

void refreshUnifiedToolbar(QWidget *window) {
#ifdef __APPLE__
    if (!window) return;
    NSWindow *nsw = mac::internal::nsWindowOf(window);
    if (!nsw) return;
    PocbWindowButtonsView *c = objc_getAssociatedObject(nsw, &kPocbContainerKey);
    if (c)
        pocb_sync_traffic_container(nsw, c);
#else
    (void)window;
#endif
}

void setTrafficLightOffset(QWidget *window, double x, double y) {
#ifdef __APPLE__
    if (!window) return;
    NSWindow *nsw = mac::internal::nsWindowOf(window);
    if (!nsw) return;
    objc_setAssociatedObject(nsw, &kPocbTrafficOffsetKey, [NSValue valueWithPoint:NSMakePoint(x, y)], OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    PocbWindowButtonsView *c = objc_getAssociatedObject(nsw, &kPocbContainerKey);
    if (c) pocb_sync_traffic_container(nsw, c);
#else
    (void)window;
    (void)x;
    (void)y;
#endif
}

void setTrafficLightsHidden(QWidget *window, bool hidden) {
#ifdef __APPLE__
    if (!window) return;
    NSWindow *nsw = mac::internal::nsWindowOf(window);
    if (!nsw) return;
    // The visible controls live in PocbWindowButtonsView, not
    // standardWindowButton: — toggling only the latter missed sidebar
    // collapse. Never setHidden:YES on the live buttons (Tahoe propagates
    // that to our copies); keep them alpha-hidden only.
    pocb_hide_native_traffic_lights(nsw);
    PocbWindowButtonsView *container =
        objc_getAssociatedObject(nsw, &kPocbContainerKey);
    if (container)
        container.hidden = hidden;
#else
    (void)window;
    (void)hidden;
#endif
}

}  // namespace mac
