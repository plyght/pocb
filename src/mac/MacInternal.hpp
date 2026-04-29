#pragma once

#ifdef __APPLE__
#import <AppKit/AppKit.h>

class QWidget;

namespace mac::internal {
NSWindow *nsWindowOf(QWidget *w);
}

#endif
