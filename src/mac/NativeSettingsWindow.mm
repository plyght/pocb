#include "NativeSettingsWindow.hpp"

#include "ChromeExtensionManager.hpp"
#include "ProfileStore.hpp"

#include <QSettings>
#include <QStringList>
#include <QWidget>

#import <AppKit/AppKit.h>

namespace {

NSString *toNSString(const QString &value) {
    return [NSString stringWithUTF8String:value.toUtf8().constData()];
}

QString toQString(NSString *value) {
    if (!value) return QString();
    return QString::fromUtf8(value.UTF8String);
}

NSWindow *parentWindow(QWidget *parent) {
    if (!parent) return NSApp.keyWindow;
    NSView *view = (__bridge NSView *)(reinterpret_cast<void *>(parent->winId()));
    return view.window ?: NSApp.keyWindow;
}

NSTextField *label(NSString *text) {
    NSTextField *field = [NSTextField labelWithString:text];
    field.font = [NSFont systemFontOfSize:12.0 weight:NSFontWeightMedium];
    field.textColor = NSColor.secondaryLabelColor;
    field.alignment = NSTextAlignmentRight;
    field.translatesAutoresizingMaskIntoConstraints = NO;
    return field;
}

NSTextField *textField(NSString *value, NSString *placeholder) {
    NSTextField *field = [NSTextField textFieldWithString:value ?: @""];
    field.placeholderString = placeholder;
    field.font = [NSFont systemFontOfSize:13.0];
    field.translatesAutoresizingMaskIntoConstraints = NO;
    return field;
}

NSButton *button(NSString *title, NSBezelStyle style) {
    NSButton *control = [NSButton buttonWithTitle:title target:nil action:nil];
    control.bezelStyle = style;
    control.translatesAutoresizingMaskIntoConstraints = NO;
    return control;
}

NSButton *checkbox(NSString *title, bool checked) {
    NSButton *control = [NSButton checkboxWithTitle:title target:nil action:nil];
    control.state = checked ? NSControlStateValueOn : NSControlStateValueOff;
    control.translatesAutoresizingMaskIntoConstraints = NO;
    return control;
}

NSView *separator() {
    NSBox *box = [[NSBox alloc] init];
    box.boxType = NSBoxSeparator;
    box.translatesAutoresizingMaskIntoConstraints = NO;
    return box;
}

void addRow(NSGridView *grid, NSString *title, NSView *control) {
    NSGridRow *row = [grid addRowWithViews:@[label(title), control]];
    row.yPlacement = NSGridCellPlacementCenter;
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
    @autoreleasepool {
        QSettings settings;
        __block NSInteger result = NSModalResponseCancel;

        NSWindow *window = [[NSWindow alloc]
            initWithContentRect:NSMakeRect(0, 0, 680, 520)
                      styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable | NSWindowStyleMaskFullSizeContentView
                        backing:NSBackingStoreBuffered
                          defer:NO];
        window.title = @"Settings";
        window.titlebarAppearsTransparent = YES;
        window.movableByWindowBackground = YES;
        window.releasedWhenClosed = NO;
        window.minSize = NSMakeSize(560, 440);

        NSVisualEffectView *root = [[NSVisualEffectView alloc] init];
        root.material = NSVisualEffectMaterialUnderWindowBackground;
        root.blendingMode = NSVisualEffectBlendingModeBehindWindow;
        root.state = NSVisualEffectStateActive;
        root.translatesAutoresizingMaskIntoConstraints = NO;
        window.contentView = root;

        NSStackView *stack = [[NSStackView alloc] init];
        stack.orientation = NSUserInterfaceLayoutOrientationVertical;
        stack.alignment = NSLayoutAttributeLeading;
        stack.spacing = 18.0;
        stack.translatesAutoresizingMaskIntoConstraints = NO;
        [root addSubview:stack];

        NSTextField *title = [NSTextField labelWithString:@"Settings"];
        title.font = [NSFont systemFontOfSize:26.0 weight:NSFontWeightSemibold];
        title.textColor = NSColor.labelColor;
        title.translatesAutoresizingMaskIntoConstraints = NO;
        [stack addArrangedSubview:title];

        NSGridView *grid = [[NSGridView alloc] init];
        grid.translatesAutoresizingMaskIntoConstraints = NO;
        grid.rowSpacing = 12.0;
        grid.columnSpacing = 14.0;
        [stack addArrangedSubview:grid];

        NSPopUpButton *profilePopup = [[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];
        profilePopup.translatesAutoresizingMaskIntoConstraints = NO;
        for (const QString &name : profiles.profiles()) [profilePopup addItemWithTitle:toNSString(name)];
        [profilePopup selectItemWithTitle:toNSString(profiles.currentName())];
        addRow(grid, @"Active profile", profilePopup);

        NSStackView *newProfileRow = [[NSStackView alloc] init];
        newProfileRow.orientation = NSUserInterfaceLayoutOrientationHorizontal;
        newProfileRow.spacing = 8.0;
        newProfileRow.translatesAutoresizingMaskIntoConstraints = NO;
        NSTextField *newProfile = textField(@"", @"New profile name");
        NSButton *createProfile = button(@"Create", NSBezelStyleRounded);
        [newProfileRow addArrangedSubview:newProfile];
        [newProfileRow addArrangedSubview:createProfile];
        [newProfile.widthAnchor constraintGreaterThanOrEqualToConstant:260.0].active = YES;
        addRow(grid, @"Add profile", newProfileRow);

        [grid addRowWithViews:@[label(@""), separator()]];

        NSTextField *homeField = textField(toNSString(homePage), @"https://search.brave.com");
        addRow(grid, @"Home page", homeField);

        NSTextField *searchField = textField(toNSString(searchEngine), @"https://search.brave.com/search?q=%1");
        addRow(grid, @"Search URL", searchField);

        NSButton *fullUrl = checkbox(@"Always show the full URL", showFullUrl);
        addRow(grid, @"Address bar", fullUrl);

        NSButton *sidebarAddress = checkbox(@"Move address bar into the sidebar (restart required)", settings.value("ui/addressBarInSidebar", false).toBool());
        addRow(grid, @"", sidebarAddress);

        [grid addRowWithViews:@[label(@""), separator()]];

        NSStackView *extensionRow = [[NSStackView alloc] init];
        extensionRow.orientation = NSUserInterfaceLayoutOrientationHorizontal;
        extensionRow.spacing = 8.0;
        extensionRow.translatesAutoresizingMaskIntoConstraints = NO;
        NSTextField *extensionPaths = textField(toNSString(ChromeExtensionManager::configuredPaths().join(";")), @"/path/to/unpacked-extension;/path/to/another-extension");
        NSButton *chooseExtension = button(@"Add Folder", NSBezelStyleRounded);
        [extensionRow addArrangedSubview:extensionPaths];
        [extensionRow addArrangedSubview:chooseExtension];
        [extensionPaths.widthAnchor constraintGreaterThanOrEqualToConstant:360.0].active = YES;
        addRow(grid, @"Extensions", extensionRow);

        NSTextField *help = [NSTextField wrappingLabelWithString:@"Search URLs must contain %1. Extension folders are loaded as local WebExtensions when supported by WebKit."];
        help.font = [NSFont systemFontOfSize:12.0];
        help.textColor = NSColor.secondaryLabelColor;
        help.translatesAutoresizingMaskIntoConstraints = NO;
        addRow(grid, @"", help);

        [grid columnAtIndex:0].width = 116.0;
        [grid columnAtIndex:0].xPlacement = NSGridCellPlacementTrailing;
        [grid columnAtIndex:1].xPlacement = NSGridCellPlacementFill;

        NSStackView *footer = [[NSStackView alloc] init];
        footer.orientation = NSUserInterfaceLayoutOrientationHorizontal;
        footer.alignment = NSLayoutAttributeCenterY;
        footer.spacing = 8.0;
        footer.translatesAutoresizingMaskIntoConstraints = NO;
        NSView *spacer = [[NSView alloc] init];
        spacer.translatesAutoresizingMaskIntoConstraints = NO;
        NSButton *cancel = button(@"Cancel", NSBezelStyleRounded);
        NSButton *save = button(@"Save", NSBezelStyleRounded);
        save.keyEquivalent = @"\r";
        save.bezelColor = NSColor.controlAccentColor;
        [footer addArrangedSubview:spacer];
        [footer addArrangedSubview:cancel];
        [footer addArrangedSubview:save];
        [stack addArrangedSubview:footer];

        [NSLayoutConstraint activateConstraints:@[
            [stack.topAnchor constraintEqualToAnchor:root.topAnchor constant:54.0],
            [stack.leadingAnchor constraintEqualToAnchor:root.leadingAnchor constant:32.0],
            [stack.trailingAnchor constraintEqualToAnchor:root.trailingAnchor constant:-32.0],
            [stack.bottomAnchor constraintLessThanOrEqualToAnchor:root.bottomAnchor constant:-24.0],
            [grid.widthAnchor constraintEqualToAnchor:stack.widthAnchor],
            [footer.widthAnchor constraintEqualToAnchor:stack.widthAnchor],
            [spacer.widthAnchor constraintGreaterThanOrEqualToConstant:1.0]
        ]];

        __block id createMonitor = [NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskLeftMouseDown handler:^NSEvent *(NSEvent *event) {
            if (event.window == window && NSPointInRect([createProfile convertPoint:event.locationInWindow fromView:nil], createProfile.bounds)) {
                const QString name = toQString(newProfile.stringValue).trimmed();
                if (!name.isEmpty()) {
                    profiles.createProfile(name);
                    profiles.setCurrentProfile(name);
                    [profilePopup removeAllItems];
                    for (const QString &profileName : profiles.profiles()) [profilePopup addItemWithTitle:toNSString(profileName)];
                    [profilePopup selectItemWithTitle:toNSString(name)];
                    newProfile.stringValue = @"";
                }
            }
            if (event.window == window && NSPointInRect([chooseExtension convertPoint:event.locationInWindow fromView:nil], chooseExtension.bounds)) {
                NSOpenPanel *panel = [NSOpenPanel openPanel];
                panel.canChooseFiles = NO;
                panel.canChooseDirectories = YES;
                panel.allowsMultipleSelection = NO;
                if ([panel runModal] == NSModalResponseOK) {
                    QStringList paths = toQString(extensionPaths.stringValue).split(';', Qt::SkipEmptyParts);
                    const QString path = toQString(panel.URL.path);
                    if (!paths.contains(path)) paths << path;
                    extensionPaths.stringValue = toNSString(paths.join(';'));
                }
            }
            return event;
        }];

        __block id keyMonitor = [NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskLeftMouseDown | NSEventMaskKeyDown handler:^NSEvent *(NSEvent *event) {
            if (event.window != window) return event;
            const bool cancelClick = event.type == NSEventTypeLeftMouseDown && NSPointInRect([cancel convertPoint:event.locationInWindow fromView:nil], cancel.bounds);
            const bool saveClick = event.type == NSEventTypeLeftMouseDown && NSPointInRect([save convertPoint:event.locationInWindow fromView:nil], save.bounds);
            const bool escapeKey = event.type == NSEventTypeKeyDown && event.keyCode == 53;
            const bool returnKey = event.type == NSEventTypeKeyDown && (event.keyCode == 36 || event.keyCode == 76);
            if (cancelClick || escapeKey) {
                result = NSModalResponseCancel;
                [NSApp stopModal];
                return nil;
            }
            if (saveClick || returnKey) {
                result = NSModalResponseOK;
                [NSApp stopModal];
                return nil;
            }
            return event;
        }];

        id closeObserver = [[NSNotificationCenter defaultCenter]
            addObserverForName:NSWindowWillCloseNotification
                        object:window
                         queue:nil
                    usingBlock:^(NSNotification *) {
                        result = NSModalResponseCancel;
                        [NSApp stopModal];
                    }];

        positionWindow(window, parentWindow(parent));
        [window makeKeyAndOrderFront:nil];
        [NSApp runModalForWindow:window];

        [[NSNotificationCenter defaultCenter] removeObserver:closeObserver];
        [NSEvent removeMonitor:createMonitor];
        [NSEvent removeMonitor:keyMonitor];
        [window orderOut:nil];

        if (result != NSModalResponseOK) return false;

        const QString selectedProfile = toQString(profilePopup.titleOfSelectedItem).trimmed();
        if (!selectedProfile.isEmpty()) profiles.setCurrentProfile(selectedProfile);
        homePage = toQString(homeField.stringValue).trimmed();
        searchEngine = toQString(searchField.stringValue).trimmed();
        showFullUrl = fullUrl.state == NSControlStateValueOn;
        settings.setValue("ui/showFullUrl", showFullUrl);
        settings.setValue("ui/addressBarInSidebar", sidebarAddress.state == NSControlStateValueOn);
        ChromeExtensionManager::setConfiguredPaths(toQString(extensionPaths.stringValue).split(';', Qt::SkipEmptyParts));
        return true;
    }
}

}  // namespace mac
