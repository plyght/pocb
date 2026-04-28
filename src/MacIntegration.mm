#include "MacIntegration.hpp"

#include <QMainWindow>
#include <QTimer>
#include <QWidget>

#ifdef __APPLE__
#import <AppKit/AppKit.h>
#import <QuartzCore/QuartzCore.h>
#import <QuartzCore/CADisplayLink.h>
#import <QuartzCore/CAMetalLayer.h>
#import <objc/runtime.h>
#import <objc/message.h>

// Per-instance isa-swizzle: replace the titlebar container view's class
// with a dynamically-created subclass whose -setFrame: clamps height to
// our desired value. AppKit can no longer reset it between our pins.
static char kPocbDesiredHeightKey;
static char kPocbWindowKey;

static void pocb_container_setFrame(id self, SEL _cmd, NSRect frame) {
    (void)_cmd;
    NSNumber *desired = objc_getAssociatedObject(self, &kPocbDesiredHeightKey);
    NSWindow *nsw = objc_getAssociatedObject(self, &kPocbWindowKey);
    if (desired && nsw) {
        const CGFloat h = desired.doubleValue;
        frame.size.height = h;
        frame.origin.y = nsw.frame.size.height - h;
    }
    struct objc_super sup = { self, class_getSuperclass(object_getClass(self)) };
    ((void (*)(struct objc_super *, SEL, NSRect))objc_msgSendSuper)(&sup, @selector(setFrame:), frame);
}

static void installContainerHeightLock(NSView *container, NSWindow *nsw, CGFloat height) {
    if (!container || !nsw) return;
    objc_setAssociatedObject(container, &kPocbDesiredHeightKey, @(height), OBJC_ASSOCIATION_RETAIN);
    objc_setAssociatedObject(container, &kPocbWindowKey, nsw, OBJC_ASSOCIATION_ASSIGN);
    Class baseClass = object_getClass(container);
    NSString *subclassName = [NSString stringWithFormat:@"PocbLockedTitlebarContainer_%p", (void *)container];
    Class subclass = NSClassFromString(subclassName);
    if (!subclass) {
        subclass = objc_allocateClassPair(baseClass, subclassName.UTF8String, 0);
        if (subclass) {
            class_addMethod(subclass, @selector(setFrame:), (IMP)pocb_container_setFrame, "v@:{CGRect={CGPoint=dd}{CGSize=dd}}");
            objc_registerClassPair(subclass);
        }
    }
    if (subclass && object_getClass(container) != subclass) {
        object_setClass(container, subclass);
    }
}

@interface PocbTrafficLightsPinner : NSObject
@property (nonatomic, weak) NSWindow *window;
@property (nonatomic, weak) NSView *observedContainer;
@property (nonatomic, assign) CGFloat padX;
@property (nonatomic, assign) CGFloat padY;
@property (nonatomic, assign) CGFloat spacing;
@property (nonatomic, assign) CGFloat buttonScale;
@property (nonatomic, assign) BOOL applying;
- (void)pin;
@end

@implementation PocbTrafficLightsPinner
- (void)pin {
    if (self.applying) return;
    self.applying = YES;
    [self pinImpl];
    self.applying = NO;
}

- (void)pinImpl {
    NSWindow *nsw = self.window;
    if (!nsw) return;
    NSButton *close = [nsw standardWindowButton:NSWindowCloseButton];
    NSButton *mini  = [nsw standardWindowButton:NSWindowMiniaturizeButton];
    NSButton *zoom  = [nsw standardWindowButton:NSWindowZoomButton];
    if (!close || !mini || !zoom) return;
    // The buttons sit inside _NSThemeCloseWidget → _NSTitlebarContainerView.
    // Resizing the *container* (two levels up) lets the buttons sit lower
    // than the default ~28pt titlebar without being clipped — and without
    // attaching an NSToolbar / titlebar accessory (both of which trigger
    // macOS 26's larger window-corner treatment).
    NSView *containerView = close.superview.superview;
    if (!containerView) return;
    // Subscribe to frame-changed notifications on the container so we re-pin
    // whenever AppKit re-lays it out (which it does on title-bar updates,
    // theme changes, fullscreen transitions, …).
    if (self.observedContainer != containerView) {
        if (self.observedContainer) {
            [NSNotificationCenter.defaultCenter removeObserver:self
                                                          name:NSViewFrameDidChangeNotification
                                                        object:self.observedContainer];
        }
        containerView.postsFrameChangedNotifications = YES;
        [NSNotificationCenter.defaultCenter addObserver:self
                                               selector:@selector(containerFrameDidChange:)
                                                   name:NSViewFrameDidChangeNotification
                                                 object:containerView];
        self.observedContainer = containerView;
    }

    const CGFloat buttonHeight = close.frame.size.height;
    // Tauri's convention: x = leading inset, y = top inset (where x is the
    // distance from the left window edge to the close button, and the
    // titlebar container is grown to (buttonHeight + y) so the button
    // centre ends up at y + buttonHeight/2 from the top of the window).
    const CGFloat x = self.padX;
    const CGFloat y = self.padY;

    const CGFloat newH = buttonHeight + y;
    NSRect cf = containerView.frame;
    // Lock the container's height so AppKit can't shrink it between pins
    // (per-instance isa-swizzle of -setFrame:).
    installContainerHeightLock(containerView, nsw, newH);
    if (fabs(cf.size.height - newH) > 0.5 ||
        fabs(cf.origin.y - (nsw.frame.size.height - newH)) > 0.5) {
        cf.size.height = newH;
        cf.origin.y = nsw.frame.size.height - newH;
        containerView.frame = cf;
    }

    NSArray<NSButton *> *btns = @[close, mini, zoom];
    CGFloat spacing = self.spacing;
    if (spacing <= 0.0) spacing = mini.frame.origin.x - close.frame.origin.x;
    const CGFloat scale = self.buttonScale > 0.0 ? self.buttonScale : 1.0;
    for (NSUInteger i = 0; i < btns.count; ++i) {
        NSButton *b = btns[i];
        if (scale != 1.0) {
            b.wantsLayer = YES;
            b.layer.anchorPoint = CGPointMake(0.5, 0.5);
            b.layer.affineTransform = CGAffineTransformMakeScale(scale, scale);
        }
        // Don't touch origin.y — Tauri's trick is to leave buttons at
        // their original (bottom-of-container) y; resizing the container
        // upward shifts them visually downward in the window.
        NSPoint o = b.frame.origin;
        o.x = x + (CGFloat)i * spacing;
        [b setFrameOrigin:o];
    }
}
- (void)containerFrameDidChange:(NSNotification *)n { (void)n; [self pin]; }
- (void)windowDidUpdate:(NSNotification *)n { (void)n; [self pin]; }
- (void)windowDidResize:(NSNotification *)n { (void)n; [self pin]; }
- (void)windowDidBecomeKey:(NSNotification *)n { (void)n; [self pin]; }
- (void)windowDidResignKey:(NSNotification *)n { (void)n; [self pin]; }
- (void)windowDidEnterFullScreen:(NSNotification *)n { (void)n; [self pin]; }
- (void)windowDidExitFullScreen:(NSNotification *)n { (void)n; [self pin]; }
@end

static char kPocbPinnerKey;

#endif

namespace mac {

#ifdef __APPLE__
static NSWindow *nsWindowOf(QWidget *w) {
    if (!w) return nil;
    w->winId();
    NSView *view = (__bridge NSView *)reinterpret_cast<void *>(w->winId());
    return view.window;
}

static void apply(QMainWindow *win, CGFloat padX, CGFloat padY, CGFloat spacing, CGFloat scale) {
    NSWindow *nsw = nsWindowOf(win);
    if (!nsw) return;

    nsw.titleVisibility = NSWindowTitleHidden;
    nsw.titlebarAppearsTransparent = YES;
    nsw.movableByWindowBackground = YES;
    if (nsw.toolbar) nsw.toolbar = nil;
    // Drop any titlebar accessory we previously installed.
    NSArray *accs = [nsw.titlebarAccessoryViewControllers copy];
    NSInteger idx = 0;
    for (NSTitlebarAccessoryViewController *vc in accs) {
        if ([vc.identifier isEqualToString:@"PocbTitlebarSpacer"]) {
            [nsw removeTitlebarAccessoryViewControllerAtIndex:idx];
        } else {
            idx++;
        }
    }
    [nsw setStyleMask:nsw.styleMask | NSWindowStyleMaskFullSizeContentView];

    // Manually reposition traffic lights, with a tracking-area refresh
    // (setFrame:display:) so hover states track the new origin. Avoids
    // attaching an NSToolbar / titlebar accessory, both of which round
    // the window's corners further on macOS 26+.
    PocbTrafficLightsPinner *pinner = objc_getAssociatedObject(nsw, &kPocbPinnerKey);
    if (!pinner) {
        pinner = [[PocbTrafficLightsPinner alloc] init];
        pinner.window = nsw;
        objc_setAssociatedObject(nsw, &kPocbPinnerKey, pinner, OBJC_ASSOCIATION_RETAIN);
        NSNotificationCenter *nc = NSNotificationCenter.defaultCenter;
        [nc addObserver:pinner selector:@selector(windowDidResize:)
                   name:NSWindowDidResizeNotification object:nsw];
        [nc addObserver:pinner selector:@selector(windowDidUpdate:)
                   name:NSWindowDidUpdateNotification object:nsw];
        [nc addObserver:pinner selector:@selector(windowDidBecomeKey:)
                   name:NSWindowDidBecomeKeyNotification object:nsw];
        [nc addObserver:pinner selector:@selector(windowDidResignKey:)
                   name:NSWindowDidResignKeyNotification object:nsw];
        [nc addObserver:pinner selector:@selector(windowDidEnterFullScreen:)
                   name:NSWindowDidEnterFullScreenNotification object:nsw];
        [nc addObserver:pinner selector:@selector(windowDidExitFullScreen:)
                   name:NSWindowDidExitFullScreenNotification object:nsw];
    }
    pinner.padX = padX;
    pinner.padY = padY;
    pinner.spacing = spacing;
    pinner.buttonScale = scale;
    [pinner pin];

    NSView *cv = nsw.contentView;
    if (cv) {
        NSRect content = [nsw contentRectForFrameRect:NSMakeRect(0, 0, nsw.frame.size.width, nsw.frame.size.height)];
        cv.frame = NSMakeRect(0, 0, content.size.width, content.size.height);
        [cv setNeedsLayout:YES];
        [cv layoutSubtreeIfNeeded];
    }
}
#endif

void roundWidgetCorners(QWidget *widget, double radius) {
#ifdef __APPLE__
    if (!widget) return;
    widget->winId();
    NSView *view = (__bridge NSView *)reinterpret_cast<void *>(widget->winId());
    if (!view) return;
    auto roundView = ^(NSView *v) {
        v.wantsLayer = YES;
        v.layer.cornerRadius = radius;
        v.layer.masksToBounds = YES;
        v.layer.backgroundColor = NSColor.clearColor.CGColor;
        if (@available(macOS 10.15, *)) {
            [v.layer setValue:@"continuous" forKey:@"cornerCurve"];
        }
    };
    roundView(view);
    for (NSView *sub in view.subviews) roundView(sub);
#else
    (void)widget; (void)radius;
#endif
}

#ifdef __APPLE__
static NSVisualEffectMaterial materialFor(VibrancyMaterial m) {
    switch (m) {
        case VibrancyMaterial::Sidebar:      return NSVisualEffectMaterialSidebar;
        case VibrancyMaterial::HeaderView:   return NSVisualEffectMaterialHeaderView;
        case VibrancyMaterial::HUDWindow:    return NSVisualEffectMaterialHUDWindow;
        case VibrancyMaterial::FullScreenUI: return NSVisualEffectMaterialFullScreenUI;
        case VibrancyMaterial::Popover:      return NSVisualEffectMaterialPopover;
        case VibrancyMaterial::WindowBackground:
        default:                             return NSVisualEffectMaterialWindowBackground;
    }
}
static NSVisualEffectView *makeVev(NSRect frame, VibrancyMaterial m) {
    NSVisualEffectView *v = [[NSVisualEffectView alloc] initWithFrame:frame];
    v.material = materialFor(m);
    v.blendingMode = NSVisualEffectBlendingModeBehindWindow;
    v.state = NSVisualEffectStateFollowsWindowActiveState;
    v.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    return v;
}
#endif

void enableWindowVibrancy(QWidget *window, VibrancyMaterial material) {
#ifdef __APPLE__
    if (!window) return;
    NSWindow *nsw = nsWindowOf(window);
    if (!nsw) return;
    nsw.opaque = NO;
    nsw.backgroundColor = NSColor.clearColor;
    NSView *content = nsw.contentView;
    if (!content) return;
    NSView *frameView = content.superview ?: content;
    // Avoid duplicate insertion.
    for (NSView *sub in frameView.subviews) {
        if ([sub isKindOfClass:[NSVisualEffectView class]] &&
            [sub.identifier isEqualToString:@"PocbWindowVibrancy"]) return;
    }
    NSVisualEffectView *vev = makeVev(frameView.bounds, material);
    vev.identifier = @"PocbWindowVibrancy";
    [frameView addSubview:vev positioned:NSWindowBelow relativeTo:nil];
#else
    (void)window; (void)material;
#endif
}

void applyVibrancyBehind(QWidget *widget, VibrancyMaterial material) {
#ifdef __APPLE__
    if (!widget) return;
    widget->winId();
    NSView *view = (__bridge NSView *)reinterpret_cast<void *>(widget->winId());
    if (!view) return;
    for (NSView *sub in view.subviews) {
        if ([sub isKindOfClass:[NSVisualEffectView class]] &&
            [sub.identifier isEqualToString:@"PocbBehindVibrancy"]) return;
    }
    NSVisualEffectView *vev = makeVev(view.bounds, material);
    vev.identifier = @"PocbBehindVibrancy";
    [view addSubview:vev positioned:NSWindowBelow relativeTo:nil];
#else
    (void)widget; (void)material;
#endif
}

void setTrafficLightsHidden(QWidget *window, bool hidden) {
#ifdef __APPLE__
    if (!window) return;
    NSWindow *nsw = nsWindowOf(window);
    if (!nsw) return;
    NSButton *close = [nsw standardWindowButton:NSWindowCloseButton];
    NSButton *mini  = [nsw standardWindowButton:NSWindowMiniaturizeButton];
    NSButton *zoom  = [nsw standardWindowButton:NSWindowZoomButton];
    auto apply = ^(NSButton *b) {
        if (!b) return;
        b.hidden = hidden;
        b.alphaValue = hidden ? 0.0 : 1.0;
        b.enabled = !hidden;
    };
    apply(close); apply(mini); apply(zoom);
#else
    (void)window; (void)hidden;
#endif
}

void enableHighRefreshRate(QWidget *window) {
#ifdef __APPLE__
    if (!window) return;
    NSWindow *nsw = nsWindowOf(window);
    if (!nsw) return;
    // Walk the view tree and bump every CAMetalLayer / CALayer's preferred
    // frame rate range to the screen's maximum refresh rate.
    NSScreen *screen = nsw.screen ?: NSScreen.mainScreen;
    if (!screen) return;
    float maxHz = 60.0f;
    if (@available(macOS 12.0, *)) {
        maxHz = (float)screen.maximumFramesPerSecond;
    }
    if (maxHz <= 60.0f) return;

    if (@available(macOS 14.0, *)) {
        CADisplayLink *link = [nsw displayLinkWithTarget:[NSNull null] selector:@selector(self)];
        link.preferredFrameRateRange = (CAFrameRateRange){60.0f, maxHz, maxHz};
        [link addToRunLoop:NSRunLoop.mainRunLoop forMode:NSRunLoopCommonModes];
    }

#else
    (void)window;
#endif
}

void integrateUnifiedToolbar(QMainWindow *win, QWidget *toolbarRow, bool compact) {
#ifdef __APPLE__
    (void)toolbarRow; (void)compact;
    if (!win) return;
    const CGFloat padX = 22.0;
    const CGFloat padY = compact ? 30.0 : 36.0;
    const CGFloat spacing = 26.0;
    const CGFloat scale = 1.18;
    auto run = [win, padX, padY, spacing, scale] { apply(win, padX, padY, spacing, scale); };
    // Apply synchronously to avoid a one-frame flash at the default position
    // before the timer fires, then re-apply via timers in case Qt re-runs
    // its window setup after first show.
    run();
    QTimer::singleShot(0, win, run);
    QTimer::singleShot(50, win, run);
#else
    (void)win; (void)toolbarRow; (void)compact;
#endif
}

}  // namespace mac
