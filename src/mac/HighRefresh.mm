#include "MacIntegration.hpp"
#include "MacInternal.hpp"

#ifdef __APPLE__
#import <AppKit/AppKit.h>
#import <QuartzCore/QuartzCore.h>
#import <QuartzCore/CADisplayLink.h>
#include <QWidget>
#endif

namespace mac {

void enableHighRefreshRate(QWidget *window) {
#ifdef __APPLE__
    if (!window) return;
    NSWindow *nsw = internal::nsWindowOf(window);
    if (!nsw) return;
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

}  // namespace mac
