#include "NativeProfilePopover.hpp"

#include "ProfileStore.hpp"

#include <QStringList>
#include <QWidget>

#import <AppKit/AppKit.h>
#import <AppKit/NSGlassEffectView.h>
#import <objc/runtime.h>

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

constexpr CGFloat kIconButtonSize = 30.0;

NSButton *symbolButton(NSString *name, BOOL selected) {
    NSButton *button = [NSButton buttonWithImage:symbolImage(name) target:nil action:nil];
    button.translatesAutoresizingMaskIntoConstraints = NO;
    button.bezelStyle = NSBezelStyleInline;
    button.bordered = NO;
    button.imagePosition = NSImageOnly;
    button.imageScaling = NSImageScaleProportionallyDown;
    button.controlSize = NSControlSizeRegular;
    button.contentTintColor = selected ? [NSColor controlAccentColor] : [NSColor labelColor];
    button.toolTip = name;
    button.wantsLayer = YES;
    button.layer.cornerRadius = 8.0;
    button.layer.backgroundColor = selected ? [[NSColor controlAccentColor] colorWithAlphaComponent:0.16].CGColor : NSColor.clearColor.CGColor;
    [button.widthAnchor constraintEqualToConstant:kIconButtonSize].active = YES;
    [button.heightAnchor constraintEqualToConstant:kIconButtonSize].active = YES;
    return button;
}

NSButton *footerButton(NSString *title, id target, SEL action) {
    NSButton *button = [NSButton buttonWithTitle:title target:target action:action];
    button.translatesAutoresizingMaskIntoConstraints = NO;
    button.bezelStyle = NSBezelStyleRegularSquare;
    button.controlSize = NSControlSizeSmall;
    [button.heightAnchor constraintEqualToConstant:26.0].active = YES;
    [button.widthAnchor constraintGreaterThanOrEqualToConstant:58.0].active = YES;
    return button;
}

NSButton *footerIconButton(NSString *name, id target, SEL action) {
    NSButton *button = [NSButton buttonWithImage:symbolImage(name) target:target action:action];
    button.translatesAutoresizingMaskIntoConstraints = NO;
    button.bezelStyle = NSBezelStyleRegularSquare;
    button.imagePosition = NSImageOnly;
    button.imageScaling = NSImageScaleProportionallyDown;
    button.controlSize = NSControlSizeSmall;
    [button.widthAnchor constraintEqualToConstant:26.0].active = YES;
    [button.heightAnchor constraintEqualToConstant:26.0].active = YES;
    return button;
}

NSView *nativeRegularGlassView(NSRect frame, CGFloat cornerRadius) {
    if (@available(macOS 26.0, *)) {
        NSGlassEffectView *glass = [[NSGlassEffectView alloc] initWithFrame:frame];
        glass.cornerRadius = cornerRadius;
        glass.style = NSGlassEffectViewStyleRegular;
        glass.tintColor = nil;
        glass.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
        return glass;
    }
    return nil;
}

void clearPopoverBacking(NSView *view) {
    if (!view) return;
    view.wantsLayer = YES;
    view.layer.opaque = NO;
    view.layer.backgroundColor = NSColor.clearColor.CGColor;
    if ([view isKindOfClass:NSVisualEffectView.class]) {
        NSVisualEffectView *effect = (NSVisualEffectView *)view;
        effect.material = NSVisualEffectMaterialPopover;
        effect.blendingMode = NSVisualEffectBlendingModeBehindWindow;
        effect.state = NSVisualEffectStateActive;
    }
    if ([view isKindOfClass:NSScrollView.class]) {
        NSScrollView *scroll = (NSScrollView *)view;
        scroll.drawsBackground = NO;
        scroll.backgroundColor = NSColor.clearColor;
        scroll.contentView.drawsBackground = NO;
        scroll.contentView.backgroundColor = NSColor.clearColor;
    }
    if ([view isKindOfClass:NSClipView.class]) {
        NSClipView *clip = (NSClipView *)view;
        clip.drawsBackground = NO;
        clip.backgroundColor = NSColor.clearColor;
    }
    if ([view isKindOfClass:NSBox.class]) {
        NSBox *box = (NSBox *)view;
        if (box.boxType != NSBoxSeparator) {
            box.fillColor = NSColor.clearColor;
            box.transparent = YES;
        }
    }
    for (NSView *subview in view.subviews) clearPopoverBacking(subview);
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
    grid.columnSpacing = 9.0;
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
    const NSInteger columns = 7;
    const NSInteger rows = ((NSInteger)icons.count + columns - 1) / columns;
    for (NSInteger row = 0; row < rows; ++row) {
        NSMutableArray<NSView *> *views = [NSMutableArray array];
        for (NSInteger col = 0; col < columns; ++col) {
            NSInteger index = row * columns + col;
            if (index >= (NSInteger)icons.count) {
                NSView *spacer = [[NSView alloc] init];
                [spacer.widthAnchor constraintEqualToConstant:kIconButtonSize].active = YES;
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
    [grid.widthAnchor constraintEqualToConstant:264.0].active = YES;
    NSStackView *buttons = [[NSStackView alloc] init];
    buttons.orientation = NSUserInterfaceLayoutOrientationHorizontal;
    buttons.spacing = 8.0;
    buttons.alignment = NSLayoutAttributeCenterY;
    buttons.translatesAutoresizingMaskIntoConstraints = NO;
    NSButton *add = footerIconButton(@"plus", self, @selector(addProfile:));
    add.toolTip = @"New Profile";
    NSButton *cancel = footerButton(@"Cancel", self, @selector(cancel:));
    NSButton *save = footerButton(@"Save", self, @selector(save:));
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
        BOOL selected = [button.identifier isEqualToString:self.selectedIcon];
        button.contentTintColor = selected ? [NSColor controlAccentColor] : [NSColor labelColor];
        button.layer.backgroundColor = selected ? [[NSColor controlAccentColor] colorWithAlphaComponent:0.16].CGColor : NSColor.clearColor.CGColor;
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
        BOOL selected = [button.identifier isEqualToString:self.selectedIcon];
        button.contentTintColor = selected ? [NSColor controlAccentColor] : [NSColor labelColor];
        button.layer.backgroundColor = selected ? [[NSColor controlAccentColor] colorWithAlphaComponent:0.16].CGColor : NSColor.clearColor.CGColor;
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
    popover.contentSize = NSMakeSize(292, 330);
    popover.contentViewController = controller;
    [popover showRelativeToRect:view.bounds ofView:view preferredEdge:NSRectEdgeMaxY];
    if (@available(macOS 26.0, *)) {
        dispatch_async(dispatch_get_main_queue(), ^{
            NSWindow *window = popover.contentViewController.view.window;
            window.opaque = NO;
            window.backgroundColor = NSColor.clearColor;
            clearPopoverBacking(window.contentView);
            clearPopoverBacking(popover.contentViewController.view);
            if (!objc_getAssociatedObject(window, @selector(showNativeProfilePopover:))) {
                NSView *glass = nativeRegularGlassView(window.contentView.bounds, 16.0);
                if (glass) {
                    [window.contentView addSubview:glass positioned:NSWindowBelow relativeTo:nil];
                    objc_setAssociatedObject(window, @selector(showNativeProfilePopover:), glass, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
                }
            }
        });
    }
    return true;
}

}  // namespace mac
