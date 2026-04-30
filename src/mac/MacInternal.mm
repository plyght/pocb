#include "MacInternal.hpp"

#ifdef __APPLE__
#include <QPoint>
#include <QStringList>
#include <QVector>
#include <QWidget>

namespace mac::internal {

NSWindow *nsWindowOf(QWidget *w) {
    if (!w) return nil;
    w->winId();
    NSView *view = (__bridge NSView *)reinterpret_cast<void *>(w->winId());
    return view.window;
}

}  // namespace mac::internal

#include "MacIntegration.hpp"
#import <AppKit/AppKit.h>
#import <AppKit/NSGlassEffectView.h>
#import <objc/message.h>
#import <objc/runtime.h>

#include <vector>

@interface PocbMenuCallbackTarget : NSObject
@property(nonatomic, assign) std::vector<std::function<void()>> *callbacks;
@property(nonatomic, assign) BOOL ownsCallbacks;
@property(nonatomic, strong) NSPopover *popover;
@property(nonatomic, strong) NSWindow *window;
@property(nonatomic, strong) NSView *overlayView;
@property(nonatomic, strong) id eventMonitor;
- (void)invoke:(id)sender;
- (void)closePopup;
@end

@implementation PocbMenuCallbackTarget
- (void)dealloc {
    if (self.eventMonitor) [NSEvent removeMonitor:self.eventMonitor];
    if (self.ownsCallbacks) delete self.callbacks;
    self.callbacks = nullptr;
}

- (void)closePopup {
    [self.popover close];
    [self.overlayView removeFromSuperview];
    [self.window orderOut:nil];
    if (self.eventMonitor) {
        [NSEvent removeMonitor:self.eventMonitor];
        self.eventMonitor = nil;
    }
}

- (void)invoke:(id)sender {
    NSInteger idx = [sender tag];
    if (self.callbacks && idx >= 0 && static_cast<size_t>(idx) < self.callbacks->size()) {
        (*self.callbacks)[static_cast<size_t>(idx)]();
    }
    [self closePopup];
}
@end

static void pocbPinSubview(NSView *subview, NSView *container);

static NSView *pocbLiquidGlassContainer(NSView *content, NSSize size) {
    if (@available(macOS 26.0, *)) {
        NSGlassEffectContainerView *container = [[NSGlassEffectContainerView alloc] initWithFrame:NSMakeRect(0, 0, size.width, size.height)];
        container.spacing = 0.0;
        container.translatesAutoresizingMaskIntoConstraints = NO;

        NSGlassEffectView *glass = [[NSGlassEffectView alloc] initWithFrame:container.bounds];
        glass.cornerRadius = 16.0;
        glass.style = NSGlassEffectViewStyleRegular;
        glass.tintColor = nil;
        glass.translatesAutoresizingMaskIntoConstraints = NO;
        glass.contentView = content;

        NSView *glassHost = [[NSView alloc] initWithFrame:container.bounds];
        glassHost.translatesAutoresizingMaskIntoConstraints = NO;
        [glassHost addSubview:glass];
        pocbPinSubview(glass, glassHost);
        container.contentView = glassHost;
        return container;
    }
    NSVisualEffectView *visual = [[NSVisualEffectView alloc] initWithFrame:NSMakeRect(0, 0, size.width, size.height)];
    visual.material = NSVisualEffectMaterialPopover;
    visual.blendingMode = NSVisualEffectBlendingModeBehindWindow;
    visual.state = NSVisualEffectStateActive;
    visual.translatesAutoresizingMaskIntoConstraints = NO;
    visual.wantsLayer = YES;
    visual.layer.cornerRadius = 16.0;
    visual.layer.masksToBounds = YES;
    [visual addSubview:content];
    return visual;
}

static void pocbPinSubview(NSView *subview, NSView *container) {
    subview.translatesAutoresizingMaskIntoConstraints = NO;
    [NSLayoutConstraint activateConstraints:@[
        [subview.leadingAnchor constraintEqualToAnchor:container.leadingAnchor],
        [subview.trailingAnchor constraintEqualToAnchor:container.trailingAnchor],
        [subview.topAnchor constraintEqualToAnchor:container.topAnchor],
        [subview.bottomAnchor constraintEqualToAnchor:container.bottomAnchor]
    ]];
}

static NSView *pocbAdaptiveGlassMenuBackground(NSRect frame) {
    if (@available(macOS 26.0, *)) {
        NSGlassEffectView *glass = [[NSGlassEffectView alloc] initWithFrame:frame];
        glass.cornerRadius = 13.0;
        glass.style = NSGlassEffectViewStyleRegular;
        glass.tintColor = nil;
        glass.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
        return glass;
    }
    return nil;
}

static void pocbApplyClearGlassToOpenMenuWindows(void) {
    if (@available(macOS 26.0, *)) {
        for (NSWindow *window in NSApp.windows) {
            NSString *className = NSStringFromClass(window.class);
            if (![className containsString:@"Menu"] && window.level != NSPopUpMenuWindowLevel) continue;
            NSView *content = window.contentView;
            if (!content || objc_getAssociatedObject(window, @selector(pocbInstallClearMenuGlassObserver))) continue;
            window.opaque = NO;
            window.backgroundColor = NSColor.clearColor;
            window.hasShadow = YES;
            NSView *glass = pocbAdaptiveGlassMenuBackground(content.bounds);
            if (!glass) continue;
            [content addSubview:glass positioned:NSWindowBelow relativeTo:nil];
            objc_setAssociatedObject(window, @selector(pocbInstallClearMenuGlassObserver), glass, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
        }
    }
}

static id pocbInstallClearMenuGlassObserver(void) {
    if (@available(macOS 26.0, *)) {
        return [NSNotificationCenter.defaultCenter addObserverForName:NSNotificationName(@"_NSMenuWillOpenNotification")
                                                               object:nil
                                                                queue:NSOperationQueue.mainQueue
                                                           usingBlock:^(__unused NSNotification *note) {
            dispatch_async(dispatch_get_main_queue(), ^{ pocbApplyClearGlassToOpenMenuWindows(); });
            dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(0.01 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{ pocbApplyClearGlassToOpenMenuWindows(); });
            dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(0.04 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{ pocbApplyClearGlassToOpenMenuWindows(); });
        }];
    }
    return nil;
}

static bool pocbShowGlassMenu(NSWindow *owner,
                              NSPoint screenPoint,
                              NSArray<NSDictionary *> *items,
                              std::vector<std::function<void()>> callbacks) {
    if (!owner || items.count == 0 || callbacks.empty()) return false;
    if (@available(macOS 26.0, *)) {
        const CGFloat width = 216.0;
        const CGFloat itemHeight = 30.0;
        const CGFloat separatorHeight = 9.0;
        CGFloat height = 16.0;
        for (NSDictionary *item in items) height += item[@"separator"] ? separatorHeight : itemHeight;

        NSStackView *stack = [[NSStackView alloc] init];
        stack.orientation = NSUserInterfaceLayoutOrientationVertical;
        stack.spacing = 0.0;
        stack.edgeInsets = NSEdgeInsetsMake(8, 8, 8, 8);
        stack.translatesAutoresizingMaskIntoConstraints = NO;

        PocbMenuCallbackTarget *target = [PocbMenuCallbackTarget new];
        target.callbacks = new std::vector<std::function<void()>>(std::move(callbacks));
        target.ownsCallbacks = YES;

        for (NSDictionary *item in items) {
            if (item[@"separator"]) {
                NSBox *separator = [[NSBox alloc] init];
                separator.boxType = NSBoxSeparator;
                separator.translatesAutoresizingMaskIntoConstraints = NO;
                [separator.heightAnchor constraintEqualToConstant:separatorHeight].active = YES;
                [stack addArrangedSubview:separator];
                continue;
            }
            NSButton *button = [NSButton buttonWithTitle:item[@"title"] target:target action:@selector(invoke:)];
            button.tag = [item[@"tag"] integerValue];
            button.bordered = NO;
            button.bezelStyle = NSBezelStyleInline;
            button.alignment = NSTextAlignmentLeft;
            button.imagePosition = NSImageLeft;
            button.controlSize = NSControlSizeRegular;
            button.enabled = [item[@"enabled"] boolValue];
            if (item[@"symbol"]) {
                if (@available(macOS 11.0, *)) button.image = [NSImage imageWithSystemSymbolName:item[@"symbol"] accessibilityDescription:nil];
            }
            button.translatesAutoresizingMaskIntoConstraints = NO;
            button.wantsLayer = YES;
            button.layer.cornerRadius = 8.0;
            [button.heightAnchor constraintEqualToConstant:itemHeight].active = YES;
            [button.widthAnchor constraintEqualToConstant:width - 16.0].active = YES;
            [stack addArrangedSubview:button];
        }

        NSView *contentView = owner.contentView;
        if (!contentView) return false;
        NSView *root = pocbLiquidGlassContainer(stack, NSMakeSize(width, height));
        pocbPinSubview(stack, root);
        NSPoint windowPoint = [owner convertPointFromScreen:screenPoint];
        NSPoint contentPoint = [contentView convertPoint:windowPoint fromView:nil];
        CGFloat x = contentPoint.x;
        CGFloat y = contentPoint.y - height;
        if (contentView.isFlipped) y = contentPoint.y;
        x = MAX(8.0, MIN(x, NSWidth(contentView.bounds) - width - 8.0));
        y = MAX(8.0, MIN(y, NSHeight(contentView.bounds) - height - 8.0));
        root.frame = NSMakeRect(x, y, width, height);
        root.autoresizingMask = NSViewMaxXMargin | NSViewMinYMargin;
        root.wantsLayer = YES;
        root.layer.shadowColor = NSColor.blackColor.CGColor;
        root.layer.shadowOpacity = 0.26;
        root.layer.shadowRadius = 22.0;
        root.layer.shadowOffset = NSMakeSize(0, -8);
        [contentView addSubview:root positioned:NSWindowAbove relativeTo:nil];
        target.overlayView = root;
        target.eventMonitor = [NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskLeftMouseDown | NSEventMaskRightMouseDown | NSEventMaskKeyDown handler:^NSEvent *(NSEvent *event) {
            if (event.type == NSEventTypeKeyDown) {
                [target closePopup];
                return event;
            }
            if (event.window != owner) {
                [target closePopup];
                return event;
            }
            NSPoint p = [root convertPoint:event.locationInWindow fromView:nil];
            if (!NSPointInRect(p, root.bounds)) [target closePopup];
            return event;
        }];
        objc_setAssociatedObject(root, @selector(invoke:), target, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
        return true;
    }
    return false;
}

namespace mac {

void performHapticFeedback() {
    [[NSHapticFeedbackManager defaultPerformer] performFeedbackPattern:NSHapticFeedbackPatternGeneric performanceTime:NSHapticFeedbackPerformanceTimeDefault];
}

void sendStandardEditAction(const char *selector) {
    if (!selector || !*selector) return;
    SEL sel = sel_registerName(selector);
    [NSApp sendAction:sel to:nil from:nil];
}

bool showNativeContextMenu(QWidget *anchor,
                           const QPoint &globalPos,
                           const QStringList &titles,
                           const QVector<bool> &enabled,
                           std::vector<std::function<void()>> callbacks) {
    if (!anchor || titles.isEmpty() || callbacks.empty()) return false;
    anchor->winId();
    NSView *view = (__bridge NSView *)reinterpret_cast<void *>(anchor->winId());
    if (!view) return false;

    NSPoint screenPoint = [NSEvent mouseLocation];
    if (globalPos != QPoint()) {
        NSScreen *screen = view.window.screen ?: NSScreen.mainScreen;
        const CGFloat screenHeight = screen ? screen.frame.size.height : 0;
        screenPoint = NSMakePoint(globalPos.x(), screenHeight - globalPos.y());
    }

    NSMutableArray<NSDictionary *> *items = [NSMutableArray array];
    NSInteger callbackIndex = 0;
    for (int i = 0; i < titles.size(); ++i) {
        const QString title = titles.at(i);
        if (title == QStringLiteral("-")) {
            [items addObject:@{ @"separator": @YES }];
            continue;
        }
        [items addObject:@{
            @"title": title.toNSString(),
            @"tag": @(callbackIndex++),
            @"enabled": @(i >= enabled.size() || enabled.at(i))
        }];
    }
    PocbMenuCallbackTarget *target = [PocbMenuCallbackTarget new];
    target.callbacks = &callbacks;

    NSMenu *menu = [[NSMenu alloc] initWithTitle:@""];
    id glassObserver = pocbInstallClearMenuGlassObserver();
    callbackIndex = 0;
    for (int i = 0; i < titles.size(); ++i) {
        const QString title = titles.at(i);
        if (title == QStringLiteral("-")) {
            [menu addItem:[NSMenuItem separatorItem]];
            continue;
        }
        NSMenuItem *item = [[NSMenuItem alloc] initWithTitle:title.toNSString() action:@selector(invoke:) keyEquivalent:@""];
        [item setTarget:target];
        [item setTag:callbackIndex++];
        [item setEnabled:i >= enabled.size() || enabled.at(i)];
        [menu addItem:item];
    }

    const NSPoint windowPoint = [view.window convertPointFromScreen:screenPoint];
    const NSPoint point = [view convertPoint:windowPoint fromView:nil];
    [menu popUpMenuPositioningItem:nil atLocation:point inView:view];
    if (glassObserver) [NSNotificationCenter.defaultCenter removeObserver:glassObserver];
    target.callbacks = nullptr;
    return true;
}

bool showNativePageActionsMenu(QWidget *anchor,
                               std::function<void()> copyUrl,
                               std::function<void()> reload,
                               std::function<void()> newTab,
                               std::function<void()> settings) {
    if (!anchor) return false;
    anchor->winId();
    NSView *view = (__bridge NSView *)reinterpret_cast<void *>(anchor->winId());
    if (!view) return false;

    std::vector<std::function<void()>> callbacks;
    callbacks.reserve(4);
    callbacks.push_back(std::move(copyUrl));
    callbacks.push_back(std::move(reload));
    callbacks.push_back(std::move(newTab));
    callbacks.push_back(std::move(settings));

    const NSPoint origin = [view.window convertPointToScreen:[view convertPoint:NSMakePoint(NSMidX(view.bounds), NSMinY(view.bounds)) toView:nil]];
    NSMutableArray<NSDictionary *> *items = [NSMutableArray arrayWithArray:@[
        @{ @"title": @"Copy URL", @"symbol": @"doc.on.doc", @"tag": @0, @"enabled": @YES },
        @{ @"title": @"Reload", @"symbol": @"arrow.clockwise", @"tag": @1, @"enabled": @YES },
        @{ @"separator": @YES },
        @{ @"title": @"New Tab", @"symbol": @"plus", @"tag": @2, @"enabled": @YES },
        @{ @"title": @"Settings…", @"symbol": @"gearshape", @"tag": @3, @"enabled": @YES }
    ]];
    PocbMenuCallbackTarget *target = [PocbMenuCallbackTarget new];
    target.callbacks = &callbacks;

    NSMenu *menu = [[NSMenu alloc] initWithTitle:@""];
    id glassObserver = pocbInstallClearMenuGlassObserver();
    auto addItem = ^(NSString *title, NSString *symbolName, NSInteger tag) {
        NSMenuItem *item = [[NSMenuItem alloc] initWithTitle:title action:@selector(invoke:) keyEquivalent:@""];
        if (@available(macOS 11.0, *)) item.image = [NSImage imageWithSystemSymbolName:symbolName accessibilityDescription:nil];
        [item setTarget:target];
        [item setTag:tag];
        [menu addItem:item];
    };

    addItem(@"Copy URL", @"doc.on.doc", 0);
    addItem(@"Reload", @"arrow.clockwise", 1);
    [menu addItem:[NSMenuItem separatorItem]];
    addItem(@"New Tab", @"plus", 2);
    addItem(@"Settings…", @"gearshape", 3);

    const NSPoint point = NSMakePoint(NSMidX(view.bounds), NSMinY(view.bounds));
    [menu popUpMenuPositioningItem:nil atLocation:point inView:view];
    if (glassObserver) [NSNotificationCenter.defaultCenter removeObserver:glassObserver];
    target.callbacks = nullptr;
    return true;
}

}  // namespace mac
#else
#include "MacIntegration.hpp"

namespace mac {

bool showNativePageActionsMenu(QWidget *,
                               std::function<void()>,
                               std::function<void()>,
                               std::function<void()>,
                               std::function<void()>) {
    return false;
}

bool showNativeContextMenu(QWidget *,
                           const QPoint &,
                           const QStringList &,
                           const QVector<bool> &,
                           std::vector<std::function<void()>>) {
    return false;
}

}  // namespace mac
#endif
