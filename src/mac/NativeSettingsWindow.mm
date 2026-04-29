#include "NativeSettingsWindow.hpp"

#include "ProfileStore.hpp"

#include <QWidget>

#import <AppKit/AppKit.h>

namespace {

NSWindow *parentWindow(QWidget *parent) {
    if (!parent) return NSApp.keyWindow;
    NSView *view = (__bridge NSView *)(reinterpret_cast<void *>(parent->winId()));
    return view.window ?: NSApp.keyWindow;
}

void positionWindow(NSWindow *window, NSWindow *owner) {
    if (!owner) {
        [window center];
        return;
    }
    NSRect ownerFrame = owner.frame;
    NSRect frame = window.frame;
    frame.origin.x = NSMidX(ownerFrame) - frame.size.width / 2.0;
    frame.origin.y = NSMidY(ownerFrame) - frame.size.height / 2.0;
    [window setFrameOrigin:frame.origin];
}

}  // namespace

namespace mac {

bool showNativeSettingsWindow(QWidget *parent,
                              ProfileStore &profiles,
                              QString &homePage,
                              QString &searchEngine,
                              bool &showFullUrl) {
    Q_UNUSED(profiles);
    Q_UNUSED(homePage);
    Q_UNUSED(searchEngine);
    Q_UNUSED(showFullUrl);

    @autoreleasepool {
        NSWindow *window = [[NSWindow alloc]
            initWithContentRect:NSMakeRect(0, 0, 640, 520)
                      styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable | NSWindowStyleMaskFullSizeContentView
                        backing:NSBackingStoreBuffered
                          defer:NO];
        window.title = @"Settings";
        window.titlebarAppearsTransparent = YES;
        window.movableByWindowBackground = YES;
        window.releasedWhenClosed = NO;
        window.minSize = NSMakeSize(480, 320);

        NSVisualEffectView *root = [[NSVisualEffectView alloc] init];
        root.material = NSVisualEffectMaterialUnderWindowBackground;
        root.blendingMode = NSVisualEffectBlendingModeBehindWindow;
        root.state = NSVisualEffectStateActive;
        window.contentView = root;

        __block bool closed = false;
        id observer = [[NSNotificationCenter defaultCenter]
            addObserverForName:NSWindowWillCloseNotification
                        object:window
                         queue:nil
                    usingBlock:^(NSNotification *) {
                        closed = true;
                        [NSApp stopModal];
                    }];

        positionWindow(window, parentWindow(parent));
        [window makeKeyAndOrderFront:nil];
        [NSApp runModalForWindow:window];

        [[NSNotificationCenter defaultCenter] removeObserver:observer];
        if (!closed) [window close];
        return false;
    }
}

}  // namespace mac
