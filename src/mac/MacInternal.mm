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

namespace mac {

void sendStandardEditAction(const char *selector) {
    if (!selector || !*selector) return;
    SEL sel = sel_registerName(selector);
    [NSApp sendAction:sel to:nil from:nil];
}

}  // namespace mac
#endif
