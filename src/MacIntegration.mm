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

// Empty NSToolbar delegate. We attach an NSToolbar purely so AppKit will
// switch into "unified" titlebar+toolbar mode and centre the traffic lights
// in that band — Safari's approach. The toolbar itself contributes no
// items; our actual chrome is drawn by Qt over the content area (which,
// thanks to NSWindowStyleMaskFullSizeContentView, extends under the band).
@interface PocbToolbarDelegate : NSObject <NSToolbarDelegate>
@end
@implementation PocbToolbarDelegate
- (NSArray<NSToolbarItemIdentifier> *)toolbarDefaultItemIdentifiers:(NSToolbar *)toolbar {
    (void)toolbar; return @[];
}
- (NSArray<NSToolbarItemIdentifier> *)toolbarAllowedItemIdentifiers:(NSToolbar *)toolbar {
    (void)toolbar; return @[];
}
- (NSToolbarItem *)toolbar:(NSToolbar *)toolbar
        itemForItemIdentifier:(NSToolbarItemIdentifier)itemIdentifier
    willBeInsertedIntoToolbar:(BOOL)flag {
    (void)toolbar; (void)itemIdentifier; (void)flag; return nil;
}
@end

#endif

namespace mac {

#ifdef __APPLE__
static NSWindow *nsWindowOf(QWidget *w) {
    if (!w) return nil;
    w->winId();
    NSView *view = (__bridge NSView *)reinterpret_cast<void *>(w->winId());
    return view.window;
}

// Reposition the three traffic-light buttons so they sit vertically centred
// within our Qt toolbar row instead of in AppKit's default ~y=6 offset.
static void centerTrafficLights(NSWindow *nsw, int rowHeight) {
    NSButton *close = [nsw standardWindowButton:NSWindowCloseButton];
    NSButton *mini  = [nsw standardWindowButton:NSWindowMiniaturizeButton];
    NSButton *zoom  = [nsw standardWindowButton:NSWindowZoomButton];
    if (!close || !mini || !zoom) return;

    // Default AppKit layout: close.x ≈ 7, spaced 20pt apart, button height ≈ 14.
    NSView *titlebarView = close.superview;
    if (!titlebarView) return;
    const CGFloat h = close.frame.size.height;
    // Titlebar view is anchored at the TOP of the window in flipped-from-top
    // sense, but its own coords are bottom-up. The titlebar view's height
    // equals the band that AppKit reserves; we want the buttons centred in
    // our `rowHeight` band (which is at the very top of the window).
    const CGFloat tbH = titlebarView.frame.size.height;
    // Top-of-window y in the titlebar's coordinate space is `tbH`. We want
    // the button centre at `rowHeight/2` below the top → y = tbH - rowHeight/2 - h/2.
    const CGFloat y = tbH - (rowHeight / 2.0) - (h / 2.0);
    auto move = [&](NSButton *b) {
        NSRect f = b.frame;
        f.origin.y = y;
        b.frame = f;
    };
    move(close);
    move(mini);
    move(zoom);
}

static void apply(QMainWindow *win, QWidget *row, bool compact) {
    NSWindow *nsw = nsWindowOf(win);
    if (!nsw || !row) return;

    nsw.titleVisibility = NSWindowTitleHidden;
    nsw.titlebarAppearsTransparent = YES;
    nsw.movableByWindowBackground = YES;
    if (nsw.toolbar) nsw.toolbar = nil;
    nsw.styleMask &= ~NSWindowStyleMaskFullSizeContentView;

    const int bandHeight = compact ? 38 : 52;
    row->setFixedHeight(bandHeight);
    // Traffic lights occupy ~x=[7,67] from the leading edge of the titlebar
    // accessory view. Leave room for them.
    row->setContentsMargins(74, 0, 14, 0);

    // Force the toolbar widget to have its own native NSView, then hand it
    // to AppKit as a titlebar accessory. AppKit will draw it INSIDE the
    // titlebar band (next to the traffic lights, sharing the vibrancy).
    row->setAttribute(Qt::WA_NativeWindow);
    row->winId();
    NSView *rowView = (__bridge NSView *)reinterpret_cast<void *>(row->winId());
    if (!rowView) return;

    // If we already installed an accessory for this window, just resize it.
    for (NSTitlebarAccessoryViewController *vc in nsw.titlebarAccessoryViewControllers) {
        if ([vc.identifier isEqualToString:@"PocbTitlebarRow"]) {
            NSRect f = vc.view.frame;
            f.size.height = bandHeight;
            vc.view.frame = f;
            return;
        }
    }

    NSView *host = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 800, bandHeight)];
    host.autoresizingMask = NSViewWidthSizable;
    rowView.frame = host.bounds;
    rowView.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    [host addSubview:rowView];

    NSTitlebarAccessoryViewController *vc = [[NSTitlebarAccessoryViewController alloc] init];
    vc.identifier = @"PocbTitlebarRow";
    vc.view = host;
    vc.layoutAttribute = NSLayoutAttributeRight;  // span the full titlebar
    if (@available(macOS 11.0, *)) {
        vc.automaticallyAdjustsSize = NO;
    }
    [nsw addTitlebarAccessoryViewController:vc];

    // Re-show the window-level Qt widget in case Qt hid it after reparenting.
    row->show();
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
    if (!win || !toolbarRow) return;
    auto run = [win, toolbarRow, compact] { apply(win, toolbarRow, compact); };
    QTimer::singleShot(0, win, run);
    QTimer::singleShot(50, win, run);
#else
    (void)win; (void)toolbarRow; (void)compact;
#endif
}

}  // namespace mac
