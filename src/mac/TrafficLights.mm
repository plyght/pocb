#include "MacIntegration.hpp"
#include "MacInternal.hpp"

#ifdef __APPLE__
#import <AppKit/AppKit.h>
#include <QWidget>
#endif

namespace mac {

void setTrafficLightsHidden(QWidget *window, bool hidden) {
#ifdef __APPLE__
    if (!window) return;
    NSWindow *nsw = internal::nsWindowOf(window);
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

}  // namespace mac
