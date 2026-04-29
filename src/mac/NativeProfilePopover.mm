#include "NativeProfilePopover.hpp"

#include "ProfileStore.hpp"

#include <QStringList>
#include <QWidget>

#import <AppKit/AppKit.h>

namespace {

NSString *toNSString(const QString &value) {
    return [NSString stringWithUTF8String:value.toUtf8().constData()];
}

QString toQString(NSString *value) {
    if (!value) return QString();
    return QString::fromUtf8([value UTF8String]);
}

NSImage *symbolImage(NSString *name) {
    if (@available(macOS 11.0, *)) {
        NSImage *image = [NSImage imageWithSystemSymbolName:name accessibilityDescription:nil];
        if (image) return image;
        return [NSImage imageWithSystemSymbolName:@"questionmark.circle" accessibilityDescription:nil];
    }
    return nil;
}

NSButton *symbolButton(NSString *name, BOOL selected) {
    NSButton *button = [NSButton buttonWithImage:symbolImage(name) target:nil action:nil];
    button.translatesAutoresizingMaskIntoConstraints = NO;
    button.bezelStyle = selected ? NSBezelStyleRegularSquare : NSBezelStyleInline;
    button.imagePosition = NSImageOnly;
    button.controlSize = NSControlSizeLarge;
    button.toolTip = name;
    button.wantsLayer = YES;
    button.layer.cornerRadius = 9.0;
    [button.widthAnchor constraintEqualToConstant:34.0].active = YES;
    [button.heightAnchor constraintEqualToConstant:34.0].active = YES;
    return button;
}

}  // namespace

@interface ProfilePopoverController : NSViewController
@property(nonatomic, assign) ProfileStore *profiles;
@property(nonatomic, strong) NSPopover *popover;
@property(nonatomic, strong) NSTextField *nameField;
@property(nonatomic, strong) NSMutableArray<NSButton *> *iconButtons;
@property(nonatomic, copy) NSString *selectedIcon;
@property(nonatomic, assign) BOOL creatingNew;
@end

@implementation ProfilePopoverController

- (void)loadView {
    NSView *root = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 292, 330)];
    root.translatesAutoresizingMaskIntoConstraints = NO;

    NSStackView *stack = [[NSStackView alloc] init];
    stack.orientation = NSUserInterfaceLayoutOrientationVertical;
    stack.spacing = 10.0;
    stack.edgeInsets = NSEdgeInsetsMake(14, 14, 14, 14);
    stack.translatesAutoresizingMaskIntoConstraints = NO;
    [root addSubview:stack];
    [NSLayoutConstraint activateConstraints:@[
        [stack.leadingAnchor constraintEqualToAnchor:root.leadingAnchor],
        [stack.trailingAnchor constraintEqualToAnchor:root.trailingAnchor],
        [stack.topAnchor constraintEqualToAnchor:root.topAnchor],
        [stack.bottomAnchor constraintEqualToAnchor:root.bottomAnchor]
    ]];

    self.nameField = [NSTextField textFieldWithString:toNSString(self.profiles->currentName())];
    self.nameField.placeholderString = @"Profile name";
    self.nameField.controlSize = NSControlSizeRegular;
    self.nameField.font = [NSFont systemFontOfSize:13.0];
    self.nameField.translatesAutoresizingMaskIntoConstraints = NO;
    [self.nameField.heightAnchor constraintEqualToConstant:24.0].active = YES;
    [stack addArrangedSubview:self.nameField];

    NSScrollView *scroll = [[NSScrollView alloc] init];
    scroll.translatesAutoresizingMaskIntoConstraints = NO;
    scroll.hasVerticalScroller = NO;
    scroll.hasHorizontalScroller = NO;
    scroll.autohidesScrollers = YES;
    scroll.borderType = NSNoBorder;
    scroll.drawsBackground = NO;
    [scroll.heightAnchor constraintEqualToConstant:226.0].active = YES;
    [stack addArrangedSubview:scroll];

    NSGridView *grid = [[NSGridView alloc] init];
    grid.translatesAutoresizingMaskIntoConstraints = NO;
    grid.rowSpacing = 8.0;
    grid.columnSpacing = 8.0;
    scroll.documentView = grid;

    NSArray<NSString *> *icons = @[
        @"person.crop.circle.fill", @"person.fill", @"person.2.fill", @"globe", @"briefcase.fill", @"house.fill",
        @"sparkles", @"bolt.fill", @"moon.fill", @"sun.max.fill", @"cloud.fill", @"flame.fill",
        @"heart.fill", @"star.fill", @"flag.fill", @"bookmark.fill", @"tag.fill", @"bell.fill",
        @"gamecontroller.fill", @"paintbrush.fill", @"pencil", @"book.fill", @"graduationcap.fill", @"terminal",
        @"lock.fill", @"key.fill", @"shield.fill", @"eye.fill", @"camera.fill", @"photo.fill",
        @"music.note", @"headphones", @"mic.fill", @"film.fill", @"tv.fill", @"display",
        @"airplane", @"car.fill", @"bicycle", @"leaf.fill", @"hare.fill", @"tortoise.fill",
        @"cup.and.saucer.fill", @"fork.knife", @"gift.fill", @"cart.fill", @"creditcard.fill", @"banknote.fill",
        @"hammer.fill", @"wrench.and.screwdriver.fill", @"gearshape.fill", @"cpu.fill", @"memorychip.fill", @"network",
        @"paperplane.fill", @"message.fill", @"bubble.left.fill", @"envelope.fill", @"calendar", @"clock.fill"
    ];
    self.selectedIcon = toNSString(self.profiles->iconName(self.profiles->currentName()));
    self.iconButtons = [NSMutableArray array];
    const NSInteger columns = 6;
    const NSInteger rows = ((NSInteger)icons.count + columns - 1) / columns;
    for (NSInteger row = 0; row < rows; ++row) {
        NSMutableArray<NSView *> *views = [NSMutableArray array];
        for (NSInteger col = 0; col < columns; ++col) {
            NSInteger index = row * columns + col;
            if (index >= (NSInteger)icons.count) {
                NSView *spacer = [[NSView alloc] init];
                [spacer.widthAnchor constraintEqualToConstant:34.0].active = YES;
                [views addObject:spacer];
                continue;
            }
            NSString *icon = icons[index];
            NSButton *button = symbolButton(icon, [icon isEqualToString:self.selectedIcon]);
            button.target = self;
            button.action = @selector(iconPicked:);
            button.identifier = icon;
            [self.iconButtons addObject:button];
            [views addObject:button];
        }
        [grid addRowWithViews:views];
    }
    [grid.widthAnchor constraintGreaterThanOrEqualToConstant:244.0].active = YES;

    NSStackView *buttons = [[NSStackView alloc] init];
    buttons.orientation = NSUserInterfaceLayoutOrientationHorizontal;
    buttons.spacing = 8.0;
    buttons.alignment = NSLayoutAttributeCenterY;
    buttons.translatesAutoresizingMaskIntoConstraints = NO;
    NSButton *add = [NSButton buttonWithImage:symbolImage(@"plus") target:self action:@selector(addProfile:)];
    add.bezelStyle = NSBezelStyleRounded;
    add.controlSize = NSControlSizeLarge;
    add.toolTip = @"New Profile";
    [add.widthAnchor constraintEqualToConstant:32.0].active = YES;
    NSButton *cancel = [NSButton buttonWithTitle:@"Cancel" target:self action:@selector(cancel:)];
    cancel.bezelStyle = NSBezelStyleRounded;
    cancel.controlSize = NSControlSizeLarge;
    NSButton *save = [NSButton buttonWithTitle:@"Save" target:self action:@selector(save:)];
    save.bezelStyle = NSBezelStyleRounded;
    save.controlSize = NSControlSizeLarge;
    save.keyEquivalent = @"\r";
    [buttons addArrangedSubview:add];
    [buttons addArrangedSubview:[[NSView alloc] init]];
    [buttons addArrangedSubview:cancel];
    [buttons addArrangedSubview:save];
    [buttons setHuggingPriority:NSLayoutPriorityDefaultLow forOrientation:NSLayoutConstraintOrientationHorizontal];
    [stack addArrangedSubview:buttons];

    self.view = root;
}

- (void)iconPicked:(NSButton *)sender {
    self.selectedIcon = sender.identifier;
    for (NSButton *button in self.iconButtons) {
        button.bezelStyle = [button.identifier isEqualToString:self.selectedIcon] ? NSBezelStyleRegularSquare : NSBezelStyleInline;
    }
}

- (void)cancel:(id)sender {
    [self.popover close];
}

- (void)addProfile:(id)sender {
    self.creatingNew = YES;
    self.nameField.stringValue = @"";
    self.nameField.placeholderString = @"New profile name";
    self.selectedIcon = @"person.crop.circle.fill";
    for (NSButton *button in self.iconButtons) {
        button.bezelStyle = [button.identifier isEqualToString:self.selectedIcon] ? NSBezelStyleRegularSquare : NSBezelStyleInline;
    }
    [self.view.window makeFirstResponder:self.nameField];
}

- (void)save:(id)sender {
    QString name = toQString(self.nameField.stringValue).trimmed();
    if (self.creatingNew) {
        if (name.isEmpty()) return;
        self.profiles->createProfile(name);
        self.profiles->setCurrentProfile(name);
        self.profiles->setIconName(name, toQString(self.selectedIcon));
        [self.popover close];
        return;
    }
    QString oldName = self.profiles->currentName();
    if (!name.isEmpty() && name != oldName) self.profiles->renameProfile(oldName, name);
    self.profiles->setIconName(self.profiles->currentName(), toQString(self.selectedIcon));
    [self.popover close];
}

@end

namespace mac {

bool showNativeProfilePopover(QWidget *anchor, ProfileStore &profiles) {
    if (!anchor) return false;
    NSView *view = (__bridge NSView *)(reinterpret_cast<void *>(anchor->winId()));
    if (!view) return false;

    NSPopover *popover = [[NSPopover alloc] init];
    popover.behavior = NSPopoverBehaviorTransient;
    popover.animates = YES;
    if (@available(macOS 10.10, *)) popover.appearance = NSAppearance.currentDrawingAppearance;

    ProfilePopoverController *controller = [[ProfilePopoverController alloc] init];
    controller.profiles = &profiles;
    controller.popover = popover;
    popover.contentViewController = controller;
    popover.contentSize = NSMakeSize(292, 330);
    [popover showRelativeToRect:view.bounds ofView:view preferredEdge:NSRectEdgeMaxY];
    return true;
}

}  // namespace mac
