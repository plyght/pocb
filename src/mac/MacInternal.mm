#include "MacInternal.hpp"

#ifdef __APPLE__
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

#include <vector>

@interface PocbMenuCallbackTarget : NSObject
@property(nonatomic, assign) std::vector<std::function<void()>> *callbacks;
- (void)invoke:(id)sender;
@end

@implementation PocbMenuCallbackTarget
- (void)invoke:(id)sender {
    NSInteger idx = [sender tag];
    if (self.callbacks && idx >= 0 && static_cast<size_t>(idx) < self.callbacks->size()) {
        (*self.callbacks)[static_cast<size_t>(idx)]();
    }
}
@end

namespace mac {

void sendStandardEditAction(const char *selector) {
    if (!selector || !*selector) return;
    SEL sel = sel_registerName(selector);
    [NSApp sendAction:sel to:nil from:nil];
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

    PocbMenuCallbackTarget *target = [PocbMenuCallbackTarget new];
    target.callbacks = &callbacks;

    NSMenu *menu = [[NSMenu alloc] initWithTitle:@""];
    auto addItem = ^(NSString *title, NSInteger tag) {
        NSMenuItem *item = [[NSMenuItem alloc] initWithTitle:title action:@selector(invoke:) keyEquivalent:@""];
        [item setTarget:target];
        [item setTag:tag];
        [menu addItem:item];
    };

    addItem(@"Copy URL", 0);
    addItem(@"Reload", 1);
    [menu addItem:[NSMenuItem separatorItem]];
    addItem(@"New Tab", 2);
    addItem(@"Settings…", 3);

    const NSPoint point = NSMakePoint(NSMidX(view.bounds), NSMinY(view.bounds));
    [menu popUpMenuPositioningItem:nil atLocation:point inView:view];
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

}  // namespace mac
#endif
