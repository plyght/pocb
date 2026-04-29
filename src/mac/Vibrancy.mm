#include "MacIntegration.hpp"
#include "MacInternal.hpp"

#ifdef __APPLE__
#import <AppKit/AppKit.h>
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
    nsw.level = NSFloatingWindowLevel;
    nsw.collectionBehavior |= NSWindowCollectionBehaviorFullScreenAuxiliary;
    nsw.ignoresMouseEvents = NO;
#else
    (void)window;
#endif
}

void keepMouseCursorVisible() {
#ifdef __APPLE__
    [NSCursor setHiddenUntilMouseMoves:NO];
#else
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
            [view sortSubviewsUsingFunction:[](NSView *a, NSView *b, void *) -> NSComparisonResult {
                const BOOL av = [a isKindOfClass:[NSVisualEffectView class]] && [a.identifier isEqualToString:@"PocbBehindVibrancy"];
                const BOOL bv = [b isKindOfClass:[NSVisualEffectView class]] && [b.identifier isEqualToString:@"PocbBehindVibrancy"];
                if (av == bv) return NSOrderedSame;
                return av ? NSOrderedAscending : NSOrderedDescending;
            } context:nullptr];
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
