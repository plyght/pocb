#include "MacIntegration.hpp"

#ifdef __APPLE__
#import <AppKit/AppKit.h>
#import <QuartzCore/QuartzCore.h>
#include <QWidget>
#endif

namespace mac {

void roundWidgetCorners(QWidget *widget, double radius, bool recurseDescendants) {
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
    if (recurseDescendants) {
        for (NSView *sub in view.subviews) roundView(sub);
    }
#else
    (void)widget; (void)radius; (void)recurseDescendants;
#endif
}

}  // namespace mac
