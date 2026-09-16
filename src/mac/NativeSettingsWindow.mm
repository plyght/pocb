#include "NativeSettingsWindow.hpp"

#include "ChromeExtensionManager.hpp"
#include "DownloadIntelligence.hpp"
#include "PasswordManager.hpp"
#include "ProfileStore.hpp"

#include <QSettings>
#include <QStringList>
#include <QWidget>

#import <AppKit/AppKit.h>

@interface PocbFlippedView : NSView
@end

@implementation PocbFlippedView
- (BOOL)isFlipped {
    return YES;
}
@end

namespace {

constexpr CGFloat kWindowWidth = 560.0;
constexpr CGFloat kWindowHeight = 600.0;
constexpr CGFloat kContentInset = 20.0;
constexpr CGFloat kRowInset = 16.0;
constexpr CGFloat kRowHeight = 44.0;
constexpr CGFloat kCardRadius = 12.0;

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

NSTextField *rowTitle(NSString *text) {
    NSTextField *field = [NSTextField labelWithString:text];
    field.font = [NSFont systemFontOfSize:14.0];
    field.textColor = NSColor.labelColor;
    field.lineBreakMode = NSLineBreakByTruncatingTail;
    field.translatesAutoresizingMaskIntoConstraints = NO;
    [field setContentCompressionResistancePriority:NSLayoutPriorityDefaultLow forOrientation:NSLayoutConstraintOrientationHorizontal];
    return field;
}

NSTextField *sectionHeader(NSString *text) {
    NSTextField *field = [NSTextField labelWithString:text];
    field.font = [NSFont systemFontOfSize:15.0 weight:NSFontWeightSemibold];
    field.textColor = NSColor.labelColor;
    field.translatesAutoresizingMaskIntoConstraints = NO;
    return field;
}

NSTextField *footnote(NSString *text) {
    NSTextField *field = [NSTextField wrappingLabelWithString:text];
    field.font = [NSFont systemFontOfSize:12.0];
    field.textColor = NSColor.secondaryLabelColor;
    field.translatesAutoresizingMaskIntoConstraints = NO;
    return field;
}

NSTextField *textField(NSString *value, NSString *placeholder, CGFloat width) {
    NSTextField *field = [NSTextField textFieldWithString:value ?: @""];
    field.placeholderString = placeholder;
    field.font = [NSFont systemFontOfSize:13.0];
    field.bezelStyle = NSTextFieldRoundedBezel;
    field.translatesAutoresizingMaskIntoConstraints = NO;
    [field.widthAnchor constraintEqualToConstant:width].active = YES;
    return field;
}

NSSwitch *toggle(bool on) {
    NSSwitch *control = [[NSSwitch alloc] init];
    control.state = on ? NSControlStateValueOn : NSControlStateValueOff;
    control.controlSize = NSControlSizeSmall;
    control.translatesAutoresizingMaskIntoConstraints = NO;
    return control;
}

NSSegmentedControl *segmented(NSArray<NSString *> *titles, NSInteger selected) {
    NSSegmentedControl *control = [NSSegmentedControl segmentedControlWithLabels:titles
                                                                    trackingMode:NSSegmentSwitchTrackingSelectOne
                                                                          target:nil
                                                                          action:nil];
    control.selectedSegment = selected;
    control.translatesAutoresizingMaskIntoConstraints = NO;
    return control;
}

NSButton *pushButton(NSString *title) {
    NSButton *control = [NSButton buttonWithTitle:title target:nil action:nil];
    control.bezelStyle = NSBezelStyleRounded;
    control.controlSize = NSControlSizeRegular;
    control.translatesAutoresizingMaskIntoConstraints = NO;
    return control;
}

NSView *hairline() {
    NSView *line = [[NSView alloc] init];
    line.wantsLayer = YES;
    line.layer.backgroundColor = NSColor.separatorColor.CGColor;
    line.translatesAutoresizingMaskIntoConstraints = NO;
    [line.heightAnchor constraintEqualToConstant:1.0].active = YES;
    return line;
}

NSView *row(NSString *title, NSView *control) {
    NSView *container = [[NSView alloc] init];
    container.translatesAutoresizingMaskIntoConstraints = NO;
    NSTextField *label = rowTitle(title);
    [container addSubview:label];
    [container addSubview:control];
    [NSLayoutConstraint activateConstraints:@[
        [container.heightAnchor constraintGreaterThanOrEqualToConstant:kRowHeight],
        [label.leadingAnchor constraintEqualToAnchor:container.leadingAnchor constant:kRowInset],
        [label.centerYAnchor constraintEqualToAnchor:container.centerYAnchor],
        [control.trailingAnchor constraintEqualToAnchor:container.trailingAnchor constant:-kRowInset],
        [control.centerYAnchor constraintEqualToAnchor:container.centerYAnchor],
        [control.topAnchor constraintGreaterThanOrEqualToAnchor:container.topAnchor constant:6.0],
        [control.bottomAnchor constraintLessThanOrEqualToAnchor:container.bottomAnchor constant:-6.0],
        [control.leadingAnchor constraintGreaterThanOrEqualToAnchor:label.trailingAnchor constant:12.0]
    ]];
    return container;
}

NSView *card(NSArray<NSView *> *rows) {
    NSView *box = [[NSView alloc] init];
    box.wantsLayer = YES;
    box.layer.backgroundColor = NSColor.quaternarySystemFillColor.CGColor;
    box.layer.cornerRadius = kCardRadius;
    box.layer.cornerCurve = kCACornerCurveContinuous;
    box.layer.masksToBounds = YES;
    box.translatesAutoresizingMaskIntoConstraints = NO;

    NSStackView *stack = [[NSStackView alloc] init];
    stack.orientation = NSUserInterfaceLayoutOrientationVertical;
    stack.alignment = NSLayoutAttributeLeading;
    stack.spacing = 0.0;
    stack.translatesAutoresizingMaskIntoConstraints = NO;
    [box addSubview:stack];
    [NSLayoutConstraint activateConstraints:@[
        [stack.topAnchor constraintEqualToAnchor:box.topAnchor],
        [stack.bottomAnchor constraintEqualToAnchor:box.bottomAnchor],
        [stack.leadingAnchor constraintEqualToAnchor:box.leadingAnchor],
        [stack.trailingAnchor constraintEqualToAnchor:box.trailingAnchor]
    ]];
    for (NSUInteger i = 0; i < rows.count; ++i) {
        if (i > 0) {
            NSView *line = hairline();
            [stack addArrangedSubview:line];
            [line.leadingAnchor constraintEqualToAnchor:stack.leadingAnchor constant:kRowInset].active = YES;
            [line.trailingAnchor constraintEqualToAnchor:stack.trailingAnchor].active = YES;
        }
        [stack addArrangedSubview:rows[i]];
        [rows[i].widthAnchor constraintEqualToAnchor:stack.widthAnchor].active = YES;
    }
    return box;
}

NSStackView *section(NSString *header, NSArray<NSView *> *rows, NSString *footer) {
    NSStackView *stack = [[NSStackView alloc] init];
    stack.orientation = NSUserInterfaceLayoutOrientationVertical;
    stack.alignment = NSLayoutAttributeLeading;
    stack.spacing = 8.0;
    stack.translatesAutoresizingMaskIntoConstraints = NO;
    if (header.length) {
        NSTextField *title = sectionHeader(header);
        [stack addArrangedSubview:title];
        [stack setCustomSpacing:10.0 afterView:title];
    }
    NSView *box = card(rows);
    [stack addArrangedSubview:box];
    [box.widthAnchor constraintEqualToAnchor:stack.widthAnchor].active = YES;
    if (footer.length) {
        NSTextField *note = footnote(footer);
        [stack addArrangedSubview:note];
        [note.widthAnchor constraintEqualToAnchor:stack.widthAnchor].active = YES;
    }
    return stack;
}

NSScrollView *page(NSArray<NSView *> *sections) {
    NSStackView *stack = [[NSStackView alloc] init];
    stack.orientation = NSUserInterfaceLayoutOrientationVertical;
    stack.alignment = NSLayoutAttributeLeading;
    stack.spacing = 26.0;
    stack.edgeInsets = NSEdgeInsetsMake(kContentInset, kContentInset, kContentInset + 8.0, kContentInset);
    stack.translatesAutoresizingMaskIntoConstraints = NO;
    for (NSView *view in sections) {
        [stack addArrangedSubview:view];
        [view.widthAnchor constraintEqualToAnchor:stack.widthAnchor constant:-2.0 * kContentInset].active = YES;
    }

    NSView *document = [[PocbFlippedView alloc] init];
    document.translatesAutoresizingMaskIntoConstraints = NO;
    [document addSubview:stack];

    NSScrollView *scroll = [[NSScrollView alloc] init];
    scroll.drawsBackground = NO;
    scroll.hasVerticalScroller = YES;
    scroll.autohidesScrollers = YES;
    scroll.automaticallyAdjustsContentInsets = YES;
    scroll.translatesAutoresizingMaskIntoConstraints = NO;
    scroll.documentView = document;
    [NSLayoutConstraint activateConstraints:@[
        [stack.topAnchor constraintEqualToAnchor:document.topAnchor],
        [stack.leadingAnchor constraintEqualToAnchor:document.leadingAnchor],
        [stack.trailingAnchor constraintEqualToAnchor:document.trailingAnchor],
        [stack.bottomAnchor constraintEqualToAnchor:document.bottomAnchor],
        [document.widthAnchor constraintEqualToAnchor:scroll.contentView.widthAnchor],
        [document.topAnchor constraintEqualToAnchor:scroll.contentView.topAnchor],
        [document.leadingAnchor constraintEqualToAnchor:scroll.contentView.leadingAnchor]
    ]];
    return scroll;
}

NSStackView *hstack(NSArray<NSView *> *views, CGFloat spacing) {
    NSStackView *stack = [NSStackView stackViewWithViews:views];
    stack.orientation = NSUserInterfaceLayoutOrientationHorizontal;
    stack.alignment = NSLayoutAttributeCenterY;
    stack.spacing = spacing;
    stack.translatesAutoresizingMaskIntoConstraints = NO;
    return stack;
}

}  // namespace

static NSToolbarItemIdentifier const kPocbSettingsTabsItem = @"pocb.settings.tabs";

@interface PocbSettingsController : NSObject <NSToolbarDelegate, NSWindowDelegate>
@property(nonatomic, strong) NSWindow *window;
@property(nonatomic, strong) NSView *pageHost;
@property(nonatomic, copy) NSArray<NSString *> *tabTitles;
@property(nonatomic, copy) NSArray<NSView *> *pages;
@property(nonatomic, assign) NSInteger selectedTab;
@property(nonatomic, copy) void (^onCreateProfile)(void);
@property(nonatomic, copy) void (^onAddExtensionFolder)(void);
@property(nonatomic, copy) void (^onImportPasswords)(void);
@property(nonatomic, copy) void (^onOpenRepository)(void);
@property(nonatomic, copy) void (^onClose)(void);
@end

@implementation PocbSettingsController

- (NSArray<NSToolbarItemIdentifier> *)toolbarDefaultItemIdentifiers:(NSToolbar *)toolbar {
    return @[NSToolbarFlexibleSpaceItemIdentifier, kPocbSettingsTabsItem, NSToolbarFlexibleSpaceItemIdentifier];
}

- (NSArray<NSToolbarItemIdentifier> *)toolbarAllowedItemIdentifiers:(NSToolbar *)toolbar {
    return [self toolbarDefaultItemIdentifiers:toolbar];
}

- (NSToolbarItem *)toolbar:(NSToolbar *)toolbar
        itemForItemIdentifier:(NSToolbarItemIdentifier)identifier
    willBeInsertedIntoToolbar:(BOOL)flag {
    if (![identifier isEqualToString:kPocbSettingsTabsItem]) return nil;
    NSToolbarItemGroup *group = [NSToolbarItemGroup groupWithItemIdentifier:identifier
                                                                     titles:self.tabTitles
                                                              selectionMode:NSToolbarItemGroupSelectionModeSelectOne
                                                                     labels:self.tabTitles
                                                                     target:self
                                                                     action:@selector(tabChanged:)];
    group.controlRepresentation = NSToolbarItemGroupControlRepresentationExpanded;
    group.selectedIndex = self.selectedTab;
    return group;
}

- (void)tabChanged:(NSToolbarItemGroup *)group {
    [self showTab:group.selectedIndex];
}

- (void)showTab:(NSInteger)index {
    if (index < 0 || index >= (NSInteger)self.pages.count) return;
    NSView *incoming = self.pages[index];
    for (NSView *view in self.pages) {
        if (view == incoming) continue;
        view.hidden = YES;
    }
    self.selectedTab = index;
    incoming.alphaValue = 0.0;
    incoming.hidden = NO;
    [NSAnimationContext runAnimationGroup:^(NSAnimationContext *context) {
        context.duration = 0.18;
        incoming.animator.alphaValue = 1.0;
    }];
}

- (void)createProfile:(id)sender {
    if (self.onCreateProfile) self.onCreateProfile();
}

- (void)addExtensionFolder:(id)sender {
    if (self.onAddExtensionFolder) self.onAddExtensionFolder();
}

- (void)importPasswords:(id)sender {
    if (self.onImportPasswords) self.onImportPasswords();
}

- (void)openRepository:(id)sender {
    if (self.onOpenRepository) self.onOpenRepository();
}

- (void)windowWillClose:(NSNotification *)notification {
    if (self.onClose) self.onClose();
}

- (void)cancelOperation:(id)sender {
    [self.window performClose:sender];
}

@end

namespace mac {

SettingsOutcome showNativeSettingsWindow(QWidget *parent,
                                         ProfileStore &profiles,
                                         QString &homePage,
                                         QString &searchEngine,
                                         bool &showFullUrl,
                                         bool &closeWindowWithLastTab) {
    @autoreleasepool {
        QSettings settings;
        SettingsOutcome outcome;
        __block bool importRequested = false;
        __block bool closed = false;

        PocbSettingsController *controller = [[PocbSettingsController alloc] init];
        controller.tabTitles = @[@"General", @"Appearance", @"Privacy", @"About"];

        NSWindow *window = [[NSWindow alloc]
            initWithContentRect:NSMakeRect(0, 0, kWindowWidth, kWindowHeight)
                      styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable
                        backing:NSBackingStoreBuffered
                          defer:NO];
        window.title = @"Settings";
        window.titleVisibility = NSWindowTitleHidden;
        window.toolbarStyle = NSWindowToolbarStyleUnified;
        window.movableByWindowBackground = YES;
        window.releasedWhenClosed = NO;
        window.delegate = controller;
        controller.window = window;

        NSToolbar *toolbar = [[NSToolbar alloc] initWithIdentifier:@"pocb.settings.toolbar"];
        toolbar.delegate = controller;
        toolbar.displayMode = NSToolbarDisplayModeIconOnly;
        toolbar.allowsUserCustomization = NO;
        toolbar.centeredItemIdentifiers = [NSSet setWithObject:kPocbSettingsTabsItem];
        window.toolbar = toolbar;

        NSView *root = [[NSView alloc] init];
        root.translatesAutoresizingMaskIntoConstraints = NO;
        window.contentView = root;

        NSPopUpButton *profilePopup = [[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];
        profilePopup.translatesAutoresizingMaskIntoConstraints = NO;
        for (const QString &name : profiles.profiles()) [profilePopup addItemWithTitle:toNSString(name)];
        [profilePopup selectItemWithTitle:toNSString(profiles.currentName())];

        NSTextField *newProfile = textField(@"", @"New profile name", 200.0);
        NSButton *createProfile = pushButton(@"Create");
        createProfile.target = controller;
        createProfile.action = @selector(createProfile:);

        NSTextField *homeField = textField(toNSString(homePage), @"about:blank", 280.0);
        NSTextField *searchField = textField(toNSString(searchEngine), @"https://search.brave.com/search?q=%1", 280.0);
        NSSwitch *littleWindow = toggle(settings.value("browser/externalLinksInLittleWindow", true).toBool());
        NSSwitch *closeLastTab = toggle(closeWindowWithLastTab);

        NSTextField *extensionPaths = textField(toNSString(ChromeExtensionManager::configuredPaths().join(";")), @"/path/to/unpacked-extension", 220.0);
        NSButton *chooseExtension = pushButton(@"Add Folder…");
        chooseExtension.target = controller;
        chooseExtension.action = @selector(addExtensionFolder:);

        NSScrollView *general = page(@[
            section(@"Profiles", @[
                row(@"Active profile", profilePopup),
                row(@"New profile", hstack(@[newProfile, createProfile], 8.0))
            ], nil),
            section(@"Browsing", @[
                row(@"Home page", homeField),
                row(@"Search URL", searchField),
                row(@"Open external links in Little Window", littleWindow)
            ], @"Search URLs must contain %1 where the query goes."),
            section(@"Tabs", @[
                row(@"Close window with last tab", closeLastTab)
            ], nil),
            section(@"Extensions", @[
                row(@"Unpacked extension folders", hstack(@[extensionPaths, chooseExtension], 8.0))
            ], @"Folders are loaded as local WebExtensions when WebKit supports them. Separate multiple paths with ;")
        ]);

        const bool liquidGlassOn = settings.value("ui/useLiquidGlass", true).toBool();
        NSSwitch *liquidGlass = toggle(liquidGlassOn);
        const bool glur = settings.value("ui/toolbarBackdrop", "glass").toString() == QLatin1String("glur");
        NSSegmentedControl *backdrop = segmented(@[@"Liquid Glass", @"Glur"], glur ? 1 : 0);
        NSSwitch *collapseToolbar = toggle(settings.value("ui/collapseToolbarOnScroll", true).toBool());
        NSSwitch *sidebarAddress = toggle(settings.value("ui/addressBarInSidebar", false).toBool());
        NSSwitch *fullUrl = toggle(showFullUrl);
        const QString scheme = settings.value("ui/pageColorScheme", QStringLiteral("system")).toString();
        NSSegmentedControl *pageScheme = segmented(@[@"System", @"Light", @"Dark"], scheme == "light" ? 1 : scheme == "dark" ? 2 : 0);

        NSScrollView *appearance = page(@[
            section(@"Shell", @[
                row(@"Liquid Glass sidebar and window", liquidGlass),
                row(@"Toolbar backdrop", backdrop),
                row(@"Hide toolbar while scrolling", collapseToolbar)
            ], @"Liquid Glass is Apple's frosted material. Glur is a progressive blur that fades the page out under the toolbar instead. Changing the sidebar material takes effect after a restart."),
            section(@"Address Bar", @[
                row(@"Always show the full URL", fullUrl),
                row(@"Address bar in sidebar", sidebarAddress)
            ], @"Moving the address bar takes effect after a restart."),
            section(@"Web Content", @[
                row(@"Page appearance", pageScheme)
            ], @"Pages that support dark mode follow this instead of the system setting.")
        ]);

        PasswordManager *passwords = PasswordManager::instance();
        NSSwitch *applePasswords = toggle(passwords->isEnabled());
        NSSwitch *touchID = toggle(passwords->autofillRequiresTouchID());
        NSButton *importPasswords = pushButton(@"Import from Browser…");
        importPasswords.target = controller;
        importPasswords.action = @selector(importPasswords:);

        const bool renameAvailable = DownloadIntelligence::isAvailable();
        NSSwitch *smartRename = toggle(settings.value("downloads/smartRename", false).toBool());
        NSSwitch *smartRenameAsk = toggle(settings.value("downloads/smartRenameAsk", true).toBool());
        smartRename.enabled = renameAvailable;
        smartRenameAsk.enabled = renameAvailable;

        NSScrollView *privacy = page(@[
            section(@"Apple Passwords", @[
                row(@"Use Apple Passwords", applePasswords),
                row(@"Require Touch ID to autofill", touchID),
                row(@"Move passwords from another browser", importPasswords)
            ], @"Passwords are saved to and filled from the Passwords app. Import takes a CSV exported from Chrome, Arc, Brave, Edge or Firefox."),
            section(@"Downloads", @[
                row(@"Rename downloads with Apple Intelligence", smartRename),
                row(@"Ask before renaming", smartRenameAsk)
            ], renameAvailable
                   ? @"Suggests a clearer file name on-device once a download finishes. Nothing leaves your Mac."
                   : @"Apple Intelligence is not available on this Mac.")
        ]);

        NSImageView *icon = [NSImageView imageViewWithImage:NSApp.applicationIconImage];
        icon.translatesAutoresizingMaskIntoConstraints = NO;
        [icon.widthAnchor constraintEqualToConstant:96.0].active = YES;
        [icon.heightAnchor constraintEqualToConstant:96.0].active = YES;
        NSTextField *appName = [NSTextField labelWithString:@"pocb"];
        appName.font = [NSFont systemFontOfSize:24.0 weight:NSFontWeightSemibold];
        appName.translatesAutoresizingMaskIntoConstraints = NO;
        NSString *shortVersion = NSBundle.mainBundle.infoDictionary[@"CFBundleShortVersionString"] ?: @"";
        NSTextField *version = [NSTextField labelWithString:[NSString stringWithFormat:@"Version %@", shortVersion]];
        version.font = [NSFont systemFontOfSize:13.0];
        version.textColor = NSColor.secondaryLabelColor;
        version.translatesAutoresizingMaskIntoConstraints = NO;
        NSTextField *blurb = footnote(@"A small, native WebKit browser for macOS.");
        blurb.alignment = NSTextAlignmentCenter;
        NSButton *repo = pushButton(@"View on GitHub");
        repo.target = controller;
        repo.action = @selector(openRepository:);

        NSStackView *aboutStack = [NSStackView stackViewWithViews:@[icon, appName, version, blurb, repo]];
        aboutStack.orientation = NSUserInterfaceLayoutOrientationVertical;
        aboutStack.alignment = NSLayoutAttributeCenterX;
        aboutStack.spacing = 6.0;
        [aboutStack setCustomSpacing:14.0 afterView:icon];
        [aboutStack setCustomSpacing:18.0 afterView:blurb];
        aboutStack.translatesAutoresizingMaskIntoConstraints = NO;
        NSView *about = [[NSView alloc] init];
        about.translatesAutoresizingMaskIntoConstraints = NO;
        [about addSubview:aboutStack];
        [NSLayoutConstraint activateConstraints:@[
            [aboutStack.centerXAnchor constraintEqualToAnchor:about.centerXAnchor],
            [aboutStack.centerYAnchor constraintEqualToAnchor:about.centerYAnchor constant:-24.0]
        ]];

        NSView *pageHost = [[NSView alloc] init];
        pageHost.translatesAutoresizingMaskIntoConstraints = NO;
        [root addSubview:pageHost];
        controller.pageHost = pageHost;
        controller.pages = @[general, appearance, privacy, about];
        for (NSView *view in controller.pages) {
            [pageHost addSubview:view];
            view.hidden = YES;
            [NSLayoutConstraint activateConstraints:@[
                [view.topAnchor constraintEqualToAnchor:pageHost.topAnchor],
                [view.bottomAnchor constraintEqualToAnchor:pageHost.bottomAnchor],
                [view.leadingAnchor constraintEqualToAnchor:pageHost.leadingAnchor],
                [view.trailingAnchor constraintEqualToAnchor:pageHost.trailingAnchor]
            ]];
        }
        [NSLayoutConstraint activateConstraints:@[
            [pageHost.topAnchor constraintEqualToAnchor:root.topAnchor],
            [pageHost.bottomAnchor constraintEqualToAnchor:root.bottomAnchor],
            [pageHost.leadingAnchor constraintEqualToAnchor:root.leadingAnchor],
            [pageHost.trailingAnchor constraintEqualToAnchor:root.trailingAnchor]
        ]];
        controller.selectedTab = 0;
        general.hidden = NO;

        controller.onCreateProfile = ^{
            const QString name = toQString(newProfile.stringValue).trimmed();
            if (name.isEmpty()) return;
            profiles.createProfile(name);
            profiles.setCurrentProfile(name);
            [profilePopup removeAllItems];
            for (const QString &profileName : profiles.profiles()) [profilePopup addItemWithTitle:toNSString(profileName)];
            [profilePopup selectItemWithTitle:toNSString(name)];
            newProfile.stringValue = @"";
        };
        controller.onAddExtensionFolder = ^{
            NSOpenPanel *panel = [NSOpenPanel openPanel];
            panel.canChooseFiles = NO;
            panel.canChooseDirectories = YES;
            panel.allowsMultipleSelection = NO;
            if ([panel runModal] != NSModalResponseOK) return;
            QStringList paths = toQString(extensionPaths.stringValue).split(';', Qt::SkipEmptyParts);
            const QString path = toQString(panel.URL.path);
            if (!paths.contains(path)) paths << path;
            extensionPaths.stringValue = toNSString(paths.join(';'));
        };
        controller.onImportPasswords = ^{
            importRequested = true;
            [window performClose:nil];
        };
        controller.onOpenRepository = ^{
            [NSWorkspace.sharedWorkspace openURL:[NSURL URLWithString:@"https://github.com/plyght/pocb"]];
        };
        controller.onClose = ^{
            if (closed) return;
            closed = true;
            [NSApp stopModal];
        };

        id monitor = [NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskKeyDown
                                                           handler:^NSEvent *(NSEvent *event) {
            const bool command = (event.modifierFlags & NSEventModifierFlagDeviceIndependentFlagsMask) == NSEventModifierFlagCommand;
            if (command && [event.charactersIgnoringModifiers isEqualToString:@"w"]) {
                [window performClose:nil];
                return nil;
            }
            return event;
        }];
        positionWindow(window, parentWindow(parent));
        [window makeKeyAndOrderFront:nil];
        [NSApp runModalForWindow:window];
        [NSEvent removeMonitor:monitor];
        [window orderOut:nil];
        window.delegate = nil;
        toolbar.delegate = nil;

        const QString selectedProfile = toQString(profilePopup.titleOfSelectedItem).trimmed();
        if (!selectedProfile.isEmpty()) profiles.setCurrentProfile(selectedProfile);
        homePage = toQString(homeField.stringValue).trimmed();
        searchEngine = toQString(searchField.stringValue).trimmed();
        showFullUrl = fullUrl.state == NSControlStateValueOn;
        closeWindowWithLastTab = closeLastTab.state == NSControlStateValueOn;
        settings.setValue("ui/showFullUrl", showFullUrl);
        settings.setValue("browser/closeWindowWithLastTab", closeWindowWithLastTab);
        settings.setValue("browser/externalLinksInLittleWindow", littleWindow.state == NSControlStateValueOn);
        settings.setValue("ui/addressBarInSidebar", sidebarAddress.state == NSControlStateValueOn);
        settings.setValue("ui/useLiquidGlass", liquidGlass.state == NSControlStateValueOn);
        settings.setValue("ui/toolbarBackdrop", backdrop.selectedSegment == 1 ? QStringLiteral("glur") : QStringLiteral("glass"));
        settings.setValue("ui/collapseToolbarOnScroll", collapseToolbar.state == NSControlStateValueOn);
        const QString newScheme = pageScheme.selectedSegment == 1 ? QStringLiteral("light")
                                : pageScheme.selectedSegment == 2 ? QStringLiteral("dark")
                                                                  : QStringLiteral("system");
        outcome.pageColorSchemeChanged = newScheme != scheme;
        settings.setValue("ui/pageColorScheme", newScheme);
        settings.setValue("downloads/smartRename", smartRename.state == NSControlStateValueOn);
        settings.setValue("downloads/smartRenameAsk", smartRenameAsk.state == NSControlStateValueOn);
        passwords->setEnabled(applePasswords.state == NSControlStateValueOn);
        passwords->setAutofillRequiresTouchID(touchID.state == NSControlStateValueOn);
        ChromeExtensionManager::setConfiguredPaths(toQString(extensionPaths.stringValue).split(';', Qt::SkipEmptyParts));
        outcome.saved = true;
        outcome.importPasswords = importRequested;
        return outcome;
    }
}

}  // namespace mac
