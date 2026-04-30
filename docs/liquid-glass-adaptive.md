# Liquid Glass Adaptive Dropdown

The address suggestions dropdown uses native AppKit Liquid Glass while staying inside the main window so the address field keeps focus, caret visibility, and keyboard shortcuts.

Implementation shape:

1. The Qt results popup remains a normal child widget of the main browser window.
2. A native glass sibling is added directly to the `NSWindow.contentView`, positioned below the Qt popup with `positioned:NSWindowBelow relativeTo:widgetView`.
3. The sibling contains an `NSGlassEffectView` using:

```objc
glass.cornerRadius = cornerRadius;
glass.style = NSGlassEffectViewStyleRegular;
glass.tintColor = nil;
```

4. On macOS 26, the glass is wrapped in private `NSAdaptiveAppearanceView` when available:

```objc
Class adaptiveClass = NSClassFromString(@"NSAdaptiveAppearanceView");
NSView *adaptive = [[adaptiveClass alloc] initWithFrame:frame];
setWindowServerAware:YES
setAnimatesAppearanceTransitions:YES
[adaptive addSubview:glass];
```

This lets AppKit sample the rendered backdrop/window-server luma itself. The browser does not pass page background colors or manually force light/dark glass.

Notes:

- `NSGlassEffectView` is public.
- `NSAdaptiveAppearanceView` is private AppKit API.
- This is acceptable for local/prototype use, but not App Store-safe.
- Keep `tintColor = nil`; tinting fights native adaptation.
- Prefer `NSGlassEffectView.cornerRadius` instead of layer masks for clean rounded edges.

Related clear style notes: `docs/liquid-glass-clear.md`.
