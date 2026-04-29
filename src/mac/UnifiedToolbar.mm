#include "MacIntegration.hpp"
#include "MacInternal.hpp"

#ifdef __APPLE__
#import <AppKit/AppKit.h>
#import <QuartzCore/QuartzCore.h>
#import <objc/runtime.h>
#import <objc/message.h>

#include <QMainWindow>
#include <QTimer>
#include <QWidget>

// Per-instance isa-swizzle: replace the titlebar container view's class
// with a dynamically-created subclass whose -setFrame: clamps height to
// our desired value. AppKit can no longer reset it between our pins.
static char kPocbDesiredHeightKey;
static char kPocbWindowKey;

static void pocb_container_setFrame(id self, SEL _cmd, NSRect frame) {
    (void)_cmd;
    NSNumber *desired = objc_getAssociatedObject(self, &kPocbDesiredHeightKey);
    NSWindow *nsw = objc_getAssociatedObject(self, &kPocbWindowKey);
    if (desired && nsw) {
        const CGFloat h = desired.doubleValue;
        frame.size.height = h;
        frame.origin.y = nsw.frame.size.height - h;
    }
    struct objc_super sup = { self, class_getSuperclass(object_getClass(self)) };
    ((void (*)(struct objc_super *, SEL, NSRect))objc_msgSendSuper)(&sup, @selector(setFrame:), frame);
}

static void installContainerHeightLock(NSView *container, NSWindow *nsw, CGFloat height) {
    if (!container || !nsw) return;
    objc_setAssociatedObject(container, &kPocbDesiredHeightKey, @(height), OBJC_ASSOCIATION_RETAIN);
    objc_setAssociatedObject(container, &kPocbWindowKey, nsw, OBJC_ASSOCIATION_ASSIGN);
    Class baseClass = object_getClass(container);
    NSString *subclassName = [NSString stringWithFormat:@"PocbLockedTitlebarContainer_%p", (void *)container];
    Class subclass = NSClassFromString(subclassName);
    if (!subclass) {
        subclass = objc_allocateClassPair(baseClass, subclassName.UTF8String, 0);
        if (subclass) {
            class_addMethod(subclass, @selector(setFrame:), (IMP)pocb_container_setFrame, "v@:{CGRect={CGPoint=dd}{CGSize=dd}}");
            objc_registerClassPair(subclass);
        }
    }
    if (subclass && object_getClass(container) != subclass) {
        object_setClass(container, subclass);
    }
}

@interface PocbTrafficLightsPinner : NSObject
@property (nonatomic, weak) NSWindow *window;
@property (nonatomic, weak) NSView *observedContainer;
@property (nonatomic, assign) CGFloat padX;
@property (nonatomic, assign) CGFloat padY;
@property (nonatomic, assign) CGFloat spacing;
@property (nonatomic, assign) CGFloat buttonScale;
@property (nonatomic, assign) BOOL applying;
- (void)pin;
@end

@implementation PocbTrafficLightsPinner
- (void)pin {
    if (self.applying) return;
    self.applying = YES;
    [self pinImpl];
    self.applying = NO;
}

- (void)pinImpl {
    NSWindow *nsw = self.window;
    if (!nsw) return;
    NSButton *close = [nsw standardWindowButton:NSWindowCloseButton];
    NSButton *mini  = [nsw standardWindowButton:NSWindowMiniaturizeButton];
    NSButton *zoom  = [nsw standardWindowButton:NSWindowZoomButton];
    if (!close || !mini || !zoom) return;
    NSView *containerView = close.superview.superview;
    if (!containerView) return;
    if (self.observedContainer != containerView) {
        if (self.observedContainer) {
            [NSNotificationCenter.defaultCenter removeObserver:self
                                                          name:NSViewFrameDidChangeNotification
                                                        object:self.observedContainer];
        }
        containerView.postsFrameChangedNotifications = YES;
        [NSNotificationCenter.defaultCenter addObserver:self
                                               selector:@selector(containerFrameDidChange:)
                                                   name:NSViewFrameDidChangeNotification
                                                 object:containerView];
        self.observedContainer = containerView;
    }

    const CGFloat buttonHeight = close.frame.size.height;
    const CGFloat x = self.padX;
    const CGFloat y = self.padY;

    const CGFloat newH = buttonHeight + y;
    NSRect cf = containerView.frame;
    installContainerHeightLock(containerView, nsw, newH);
    if (fabs(cf.size.height - newH) > 0.5 ||
        fabs(cf.origin.y - (nsw.frame.size.height - newH)) > 0.5) {
        cf.size.height = newH;
        cf.origin.y = nsw.frame.size.height - newH;
        containerView.frame = cf;
    }

    NSArray<NSButton *> *btns = @[close, mini, zoom];
    CGFloat spacing = self.spacing;
    if (spacing <= 0.0) spacing = mini.frame.origin.x - close.frame.origin.x;
    const CGFloat scale = self.buttonScale > 0.0 ? self.buttonScale : 1.0;
    for (NSUInteger i = 0; i < btns.count; ++i) {
        NSButton *b = btns[i];
        if (scale != 1.0) {
            b.wantsLayer = YES;
            b.layer.anchorPoint = CGPointMake(0.5, 0.5);
            b.layer.affineTransform = CGAffineTransformMakeScale(scale, scale);
        }
        NSPoint o = b.frame.origin;
        o.x = x + (CGFloat)i * spacing;
        [b setFrameOrigin:o];
    }
}
- (void)containerFrameDidChange:(NSNotification *)n { (void)n; [self pin]; }
- (void)windowDidUpdate:(NSNotification *)n { (void)n; [self pin]; }
- (void)windowDidResize:(NSNotification *)n { (void)n; [self pin]; }
- (void)windowDidBecomeKey:(NSNotification *)n { (void)n; [self pin]; }
- (void)windowDidResignKey:(NSNotification *)n { (void)n; [self pin]; }
- (void)windowDidEnterFullScreen:(NSNotification *)n { (void)n; [self pin]; }
- (void)windowDidExitFullScreen:(NSNotification *)n { (void)n; [self pin]; }
@end

static char kPocbPinnerKey;

static void apply(QMainWindow *win, CGFloat padX, CGFloat padY, CGFloat spacing, CGFloat scale) {
    NSWindow *nsw = mac::internal::nsWindowOf(win);
    if (!nsw) return;

    nsw.titleVisibility = NSWindowTitleHidden;
    nsw.titlebarAppearsTransparent = YES;
    nsw.movableByWindowBackground = YES;
    if (nsw.toolbar) nsw.toolbar = nil;
    NSArray *accs = [nsw.titlebarAccessoryViewControllers copy];
    NSInteger idx = 0;
    for (NSTitlebarAccessoryViewController *vc in accs) {
        if ([vc.identifier isEqualToString:@"PocbTitlebarSpacer"]) {
            [nsw removeTitlebarAccessoryViewControllerAtIndex:idx];
        } else {
            idx++;
        }
    }
    [nsw setStyleMask:nsw.styleMask | NSWindowStyleMaskFullSizeContentView];

    PocbTrafficLightsPinner *pinner = objc_getAssociatedObject(nsw, &kPocbPinnerKey);
    if (!pinner) {
        pinner = [[PocbTrafficLightsPinner alloc] init];
        pinner.window = nsw;
        objc_setAssociatedObject(nsw, &kPocbPinnerKey, pinner, OBJC_ASSOCIATION_RETAIN);
        NSNotificationCenter *nc = NSNotificationCenter.defaultCenter;
        [nc addObserver:pinner selector:@selector(windowDidResize:)
                   name:NSWindowDidResizeNotification object:nsw];
        [nc addObserver:pinner selector:@selector(windowDidUpdate:)
                   name:NSWindowDidUpdateNotification object:nsw];
        [nc addObserver:pinner selector:@selector(windowDidBecomeKey:)
                   name:NSWindowDidBecomeKeyNotification object:nsw];
        [nc addObserver:pinner selector:@selector(windowDidResignKey:)
                   name:NSWindowDidResignKeyNotification object:nsw];
        [nc addObserver:pinner selector:@selector(windowDidEnterFullScreen:)
                   name:NSWindowDidEnterFullScreenNotification object:nsw];
        [nc addObserver:pinner selector:@selector(windowDidExitFullScreen:)
                   name:NSWindowDidExitFullScreenNotification object:nsw];
    }
    pinner.padX = padX;
    pinner.padY = padY;
    pinner.spacing = spacing;
    pinner.buttonScale = scale;
    [pinner pin];

    NSView *cv = nsw.contentView;
    if (cv) {
        NSRect content = [nsw contentRectForFrameRect:NSMakeRect(0, 0, nsw.frame.size.width, nsw.frame.size.height)];
        cv.frame = NSMakeRect(0, 0, content.size.width, content.size.height);
        [cv setNeedsLayout:YES];
        [cv layoutSubtreeIfNeeded];
    }
}
#endif

namespace mac {

void integrateUnifiedToolbar(QMainWindow *win, QWidget *toolbarRow, bool compact) {
#ifdef __APPLE__
    (void)toolbarRow;
    if (!win) return;
    const CGFloat padX = 22.0;
    const CGFloat padY = compact ? 30.0 : 36.0;
    const CGFloat spacing = 26.0;
    const CGFloat scale = 1.18;
    auto run = [win, padX, padY, spacing, scale] { apply(win, padX, padY, spacing, scale); };
    run();
    QTimer::singleShot(0, win, run);
    QTimer::singleShot(50, win, run);
#else
    (void)win; (void)toolbarRow; (void)compact;
#endif
}

}  // namespace mac
