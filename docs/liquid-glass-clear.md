# Liquid Glass Clear Style Notes

Use this when a component should get the lighter/clear Liquid Glass treatment without forcing tint or app appearance.

Local Xcode beta SDK findings:

- AppKit exposes `NSGlassEffectViewStyleRegular` and `NSGlassEffectViewStyleClear` in `NSGlassEffectView.h`.
- There is no public AppKit `light` or `dark` glass style enum.
- Keep `tintColor = nil` unless a component explicitly needs a brand/user tint.
- Avoid forcing `appearance` for glass unless testing a specific bug; it fights system adaptivity.
- Prefer `NSGlassEffectView.cornerRadius` over layer masks for rounded glass edges.

Clear-style setup:

```objc
if (@available(macOS 26.0, *)) {
    NSGlassEffectView *glass = [[NSGlassEffectView alloc] initWithFrame:frame];
    glass.cornerRadius = cornerRadius;
    glass.style = NSGlassEffectViewStyleClear;
    glass.tintColor = nil;
}
```

For an existing glass view:

```objc
if ([glass isKindOfClass:NSGlassEffectView.class]) {
    glass.appearance = nil;
    glass.tintColor = nil;
    glass.style = NSGlassEffectViewStyleClear;
}
```

Layering pattern that worked for the address results dropdown:

1. Keep Qt content as a normal in-window child widget so text input keeps focus/caret/Cmd shortcuts.
2. Add `NSGlassEffectView` as a native sibling behind the Qt content, attached to the main `NSWindow.contentView`.
3. Position it with `convertRect:fromView:` from the Qt widget’s native view.
4. Add it with `positioned:NSWindowBelow relativeTo:widgetView`.
5. Hide that sibling when the Qt popup hides.

This avoids separate tool-window focus bugs while still letting glass sample the webpage/content behind it.
