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
NSVisualEffectView *makeVev(NSRect frame, mac::VibrancyMaterial m) {
    NSVisualEffectView *v = [[NSVisualEffectView alloc] initWithFrame:frame];
    v.material = materialFor(m);
    v.blendingMode = NSVisualEffectBlendingModeBehindWindow;
    v.state = NSVisualEffectStateFollowsWindowActiveState;
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
    for (NSView *sub in content.subviews) {
        if ([sub isKindOfClass:[NSVisualEffectView class]] &&
            [sub.identifier isEqualToString:@"PocbFloatingVibrancy"]) return;
    }
    NSVisualEffectView *vev = makeVev(content.bounds, material);
    vev.identifier = @"PocbFloatingVibrancy";
    [content addSubview:vev positioned:NSWindowBelow relativeTo:nil];
#else
    (void)window; (void)material; (void)cornerRadius;
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

}  // namespace mac
