#include "MacIntegration.hpp"
#include "MacInternal.hpp"

#ifdef __APPLE__
#import <AppKit/AppKit.h>
#import <AppKit/NSGlassEffectView.h>
#import <objc/message.h>
#import <objc/runtime.h>
#include <QWidget>

namespace {
NSVisualEffectMaterial materialFor(mac::VibrancyMaterial m) {
    using M = mac::VibrancyMaterial;
    switch (m) {
        case M::Sidebar:      return NSVisualEffectMaterialSidebar;
        case M::HeaderView:   return NSVisualEffectMaterialHeaderView;
        case M::HUDWindow:    return NSVisualEffectMaterialHUDWindow;
        case M::FullScreenUI: return NSVisualEffectMaterialFullScreenUI;
        case M::Popover:      return NSVisualEffectMaterialPopover;
        case M::WindowBackground:
        default:              return NSVisualEffectMaterialWindowBackground;
    }
}
NSVisualEffectView *makeVev(NSRect frame, mac::VibrancyMaterial m,
                            NSVisualEffectBlendingMode blendingMode = NSVisualEffectBlendingModeBehindWindow) {
    NSVisualEffectView *v = [[NSVisualEffectView alloc] initWithFrame:frame];
    v.material = materialFor(m);
    v.blendingMode = blendingMode;
    v.state = NSVisualEffectStateActive;
    v.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    return v;
}
NSComparisonResult compareBehindVibrancySubviews(__kindof NSView *a, __kindof NSView *b, void *) {
    const BOOL av = [a isKindOfClass:[NSVisualEffectView class]] && [a.identifier isEqualToString:@"PocbBehindVibrancy"];
    const BOOL bv = [b isKindOfClass:[NSVisualEffectView class]] && [b.identifier isEqualToString:@"PocbBehindVibrancy"];
    if (av == bv) return NSOrderedSame;
    return av ? NSOrderedAscending : NSOrderedDescending;
}
static char kPocbLiquidGlassSiblingKey;
static __strong NSRunningApplication *pocbPreviousForegroundApplication;
static __strong id pocbForegroundApplicationObserver;
NSGlassEffectView *findGlassEffectView(NSView *view) {
    if ([view isKindOfClass:NSGlassEffectView.class]) return (NSGlassEffectView *)view;
    for (NSView *subview in view.subviews) {
        if (NSGlassEffectView *glass = findGlassEffectView(subview)) return glass;
    }
    return nil;
}

NSView *makeGlassView(NSRect frame, double cornerRadius) {
    NSView *backdrop = nil;
    if (@available(macOS 26.0, *)) {
        NSGlassEffectView *glass = [[NSGlassEffectView alloc] initWithFrame:NSMakeRect(0, 0, frame.size.width, frame.size.height)];
        glass.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
        glass.cornerRadius = cornerRadius;
        glass.style = NSGlassEffectViewStyleRegular;
        glass.tintColor = nil;

        Class adaptiveClass = NSClassFromString(@"NSAdaptiveAppearanceView");
        if (adaptiveClass) {
            NSView *adaptive = [[adaptiveClass alloc] initWithFrame:frame];
            adaptive.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
            SEL setWindowServerAware = sel_registerName("setWindowServerAware:");
            if ([adaptive respondsToSelector:setWindowServerAware]) {
                ((void(*)(id,SEL,BOOL))objc_msgSend)(adaptive, setWindowServerAware, YES);
            }
            SEL setAnimates = sel_registerName("setAnimatesAppearanceTransitions:");
            if ([adaptive respondsToSelector:setAnimates]) {
                ((void(*)(id,SEL,BOOL))objc_msgSend)(adaptive, setAnimates, YES);
            }
            [adaptive addSubview:glass];
            backdrop = adaptive;
        } else {
            glass.frame = frame;
            backdrop = glass;
        }
    } else {
        backdrop = makeVev(frame, mac::VibrancyMaterial::Popover, NSVisualEffectBlendingModeWithinWindow);
    }
    backdrop.identifier = @"PocbLiquidGlassSibling";
    if (!findGlassEffectView(backdrop)) {
        backdrop.wantsLayer = YES;
        backdrop.layer.cornerRadius = cornerRadius;
        backdrop.layer.masksToBounds = YES;
        if (@available(macOS 10.15, *)) {
            [backdrop.layer setValue:@"continuous" forKey:@"cornerCurve"];
        }
    }
    return backdrop;
}
}  // namespace
#endif

namespace mac {

void enableWindowVibrancy(QWidget *window, VibrancyMaterial material) {
#ifdef __APPLE__
    if (!window) return;
    NSWindow *nsw = internal::nsWindowOf(window);
    if (!nsw) return;
    nsw.opaque = NO;
    nsw.backgroundColor = NSColor.clearColor;
    NSView *content = nsw.contentView;
    if (!content) return;
    NSView *frameView = content.superview ?: content;
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

void makeFloatingVibrantPanel(QWidget *window, VibrancyMaterial material, double cornerRadius) {
#ifdef __APPLE__
    if (!window) return;
    NSWindow *nsw = internal::nsWindowOf(window);
    if (!nsw) return;
    nsw.opaque = NO;
    nsw.backgroundColor = NSColor.clearColor;
    nsw.hasShadow = YES;
    NSView *content = nsw.contentView;
    if (!content) return;
    content.wantsLayer = YES;
    content.layer.cornerRadius = cornerRadius;
    content.layer.masksToBounds = YES;
    content.layer.backgroundColor = NSColor.clearColor.CGColor;
    if (@available(macOS 10.15, *)) {
        [content.layer setValue:@"continuous" forKey:@"cornerCurve"];
    }
    NSView *target = content.superview ?: content;
    target.wantsLayer = YES;
    target.layer.cornerRadius = cornerRadius;
    target.layer.masksToBounds = YES;
    target.layer.backgroundColor = NSColor.clearColor.CGColor;
    if (@available(macOS 10.15, *)) {
        [target.layer setValue:@"continuous" forKey:@"cornerCurve"];
    }
    for (NSView *sub in target.subviews) {
        if ([sub isKindOfClass:[NSVisualEffectView class]] &&
            [sub.identifier isEqualToString:@"PocbFloatingVibrancy"]) {
            sub.frame = target.bounds;
            return;
        }
    }
    NSVisualEffectView *vev = makeVev(target.bounds, material);
    vev.identifier = @"PocbFloatingVibrancy";
    [target addSubview:vev positioned:NSWindowBelow relativeTo:nil];
#else
    (void)window; (void)material; (void)cornerRadius;
#endif
}

void preventWindowActivation(QWidget *window) {
#ifdef __APPLE__
    if (!window) return;
    window->winId();
    NSWindow *nsw = internal::nsWindowOf(window);
    if (!nsw) return;
    nsw.hidesOnDeactivate = NO;
    nsw.level = NSNormalWindowLevel;
    nsw.collectionBehavior |= NSWindowCollectionBehaviorFullScreenAuxiliary;
    nsw.ignoresMouseEvents = NO;
    [nsw setCanHide:NO];
#else
    (void)window;
#endif
}

void orderOtherApplicationWindowsBack(QWidget *frontWindow) {
#ifdef __APPLE__
    NSWindow *front = frontWindow ? internal::nsWindowOf(frontWindow) : nil;
    for (NSWindow *window in NSApp.windows) {
        if (window == front) continue;
        [window orderBack:nil];
    }
#else
    (void)frontWindow;
#endif
}

void setAccessoryActivationPolicy() {
#ifdef __APPLE__
    [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
#endif
}

void setRegularActivationPolicy() {
#ifdef __APPLE__
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
#endif
}

void showWindowWithoutAppActivation(QWidget *window) {
#ifdef __APPLE__
    if (!window) return;
    window->winId();
    NSWindow *nsw = internal::nsWindowOf(window);
    if (!nsw) return;
    [nsw orderFrontRegardless];
#else
    (void)window;
#endif
}

void installForegroundApplicationTracker() {
#ifdef __APPLE__
    if (pocbForegroundApplicationObserver) return;
    NSRunningApplication *frontmost = [[NSWorkspace sharedWorkspace] frontmostApplication];
    if (frontmost && frontmost.processIdentifier != [[NSRunningApplication currentApplication] processIdentifier]) pocbPreviousForegroundApplication = frontmost;
    NSNotificationCenter *center = [[NSWorkspace sharedWorkspace] notificationCenter];
    pocbForegroundApplicationObserver = [center addObserverForName:NSWorkspaceDidActivateApplicationNotification
                                                            object:nil
                                                             queue:nil
                                                        usingBlock:^(NSNotification *note) {
        NSRunningApplication *app = note.userInfo[NSWorkspaceApplicationKey];
        if (!app || app.processIdentifier == [[NSRunningApplication currentApplication] processIdentifier]) return;
        pocbPreviousForegroundApplication = app;
    }];
#else
#endif
}

void restorePreviousForegroundApplication() {
#ifdef __APPLE__
    NSRunningApplication *app = pocbPreviousForegroundApplication;
    if (app && !app.terminated) [app activateWithOptions:0];
    [NSApp deactivate];
#else
#endif
}

void hideCursorUntilMouseMoves() {
#ifdef __APPLE__
    [NSCursor setHiddenUntilMouseMoves:YES];
#endif
}

void applyLiquidGlassBehind(QWidget *widget, double cornerRadius) {
#ifdef __APPLE__
    if (!widget) return;
    widget->winId();
    NSView *view = (__bridge NSView *)reinterpret_cast<void *>(widget->winId());
    if (!view) return;
    view.wantsLayer = YES;
    view.layer.backgroundColor = NSColor.clearColor.CGColor;
    view.layer.cornerRadius = cornerRadius;
    view.layer.masksToBounds = YES;
    if (@available(macOS 10.15, *)) {
        [view.layer setValue:@"continuous" forKey:@"cornerCurve"];
    }
    for (NSView *sub in view.subviews) {
        if ([sub.identifier isEqualToString:@"PocbLiquidGlassBehind"]) {
            sub.frame = view.bounds;
            return;
        }
    }
    NSView *backdrop = nil;
    if (@available(macOS 26.0, *)) {
        NSGlassEffectView *glass = [[NSGlassEffectView alloc] initWithFrame:view.bounds];
        glass.identifier = @"PocbLiquidGlassBehind";
        glass.cornerRadius = cornerRadius;
        glass.style = NSGlassEffectViewStyleRegular;
        glass.tintColor = nil;
        backdrop = glass;
    } else {
        NSVisualEffectView *visual = makeVev(view.bounds, VibrancyMaterial::Popover, NSVisualEffectBlendingModeWithinWindow);
        visual.identifier = @"PocbLiquidGlassBehind";
        backdrop = visual;
    }
    backdrop.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    backdrop.wantsLayer = YES;
    backdrop.layer.cornerRadius = cornerRadius;
    backdrop.layer.masksToBounds = YES;
    if (@available(macOS 10.15, *)) {
        [backdrop.layer setValue:@"continuous" forKey:@"cornerCurve"];
    }
    [view addSubview:backdrop positioned:NSWindowBelow relativeTo:nil];
#else
    (void)widget; (void)cornerRadius;
#endif
}

void applyLiquidGlassSiblingBehind(QWidget *widget, double cornerRadius) {
#ifdef __APPLE__
    if (!widget) return;
    widget->winId();
    NSView *widgetView = (__bridge NSView *)reinterpret_cast<void *>(widget->winId());
    if (!widgetView || !widgetView.window.contentView) return;
    NSView *content = widgetView.window.contentView;
    NSView *glass = objc_getAssociatedObject(widgetView, &kPocbLiquidGlassSiblingKey);
    const NSRect frame = [content convertRect:widgetView.bounds fromView:widgetView];
    if (!glass) {
        glass = makeGlassView(frame, cornerRadius);
        objc_setAssociatedObject(widgetView, &kPocbLiquidGlassSiblingKey, glass, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
        [content addSubview:glass positioned:NSWindowBelow relativeTo:widgetView];
    }
    glass.frame = frame;
    glass.hidden = NO;
    if (NSGlassEffectView *glassEffect = findGlassEffectView(glass)) {
        glassEffect.appearance = nil;
        ((void(*)(id,SEL,id))objc_msgSend)(glassEffect, @selector(setTintColor:), nil);
        ((void(*)(id,SEL,NSInteger))objc_msgSend)(glassEffect, @selector(setStyle:), NSGlassEffectViewStyleRegular);
    }
    [content addSubview:glass positioned:NSWindowBelow relativeTo:widgetView];
#else
    (void)widget; (void)cornerRadius;
#endif
}

void hideLiquidGlassSibling(QWidget *widget) {
#ifdef __APPLE__
    if (!widget) return;
    widget->winId();
    NSView *widgetView = (__bridge NSView *)reinterpret_cast<void *>(widget->winId());
    if (!widgetView) return;
    NSView *glass = objc_getAssociatedObject(widgetView, &kPocbLiquidGlassSiblingKey);
    if (glass) glass.hidden = YES;
#else
    (void)widget;
#endif
}

void makeTransparentFloatingPanel(QWidget *window, double cornerRadius) {
#ifdef __APPLE__
    if (!window) return;
    window->winId();
    NSWindow *nsw = internal::nsWindowOf(window);
    if (!nsw) return;
    nsw.opaque = NO;
    nsw.backgroundColor = NSColor.clearColor;
    nsw.hasShadow = YES;
    NSView *content = nsw.contentView;
    if (!content) return;
    content.wantsLayer = YES;
    content.layer.cornerRadius = cornerRadius;
    content.layer.masksToBounds = YES;
    content.layer.backgroundColor = NSColor.clearColor.CGColor;
    if (@available(macOS 10.15, *)) {
        [content.layer setValue:@"continuous" forKey:@"cornerCurve"];
    }
#else
    (void)window; (void)cornerRadius;
#endif
}

void makeTitlebarTransparent(QWidget *window) {
#ifdef __APPLE__
    if (!window) return;
    NSWindow *nsw = internal::nsWindowOf(window);
    if (!nsw) return;
    nsw.titlebarAppearsTransparent = YES;
    nsw.titleVisibility = NSWindowTitleHidden;
    nsw.styleMask |= NSWindowStyleMaskFullSizeContentView;
#else
    (void)window;
#endif
}

void applyVibrancyBehind(QWidget *widget, VibrancyMaterial material) {
#ifdef __APPLE__
    if (!widget) return;
    widget->winId();
    NSView *view = (__bridge NSView *)reinterpret_cast<void *>(widget->winId());
    if (!view) return;
    view.wantsLayer = YES;
    view.layer.backgroundColor = NSColor.clearColor.CGColor;
    view.layer.cornerRadius = 12.0;
    view.layer.masksToBounds = YES;
    if (@available(macOS 10.15, *)) {
        [view.layer setValue:@"continuous" forKey:@"cornerCurve"];
    }
    for (NSView *sub in view.subviews) {
        if ([sub isKindOfClass:[NSVisualEffectView class]] &&
            [sub.identifier isEqualToString:@"PocbBehindVibrancy"]) {
            [view sortSubviewsUsingFunction:compareBehindVibrancySubviews context:nullptr];
            return;
        }
    }
    NSVisualEffectView *vev = makeVev(view.bounds, material, NSVisualEffectBlendingModeWithinWindow);
    vev.identifier = @"PocbBehindVibrancy";
    [view addSubview:vev positioned:NSWindowBelow relativeTo:nil];
#else
    (void)widget; (void)material;
#endif
}

}  // namespace mac
