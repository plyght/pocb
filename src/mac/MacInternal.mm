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
#endif
