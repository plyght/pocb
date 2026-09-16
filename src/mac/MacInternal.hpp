#pragma once

#ifdef __APPLE__
#import <AppKit/AppKit.h>

class QWidget;

namespace mac::internal {
NSWindow *nsWindowOf(QWidget *w);
}

namespace mac {
NSView *makeGlurBackdropView(NSRect frame, double cornerRadius, double blurRadius, double offset, double interpolation);
}

#endif
