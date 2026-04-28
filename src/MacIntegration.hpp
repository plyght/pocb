#pragma once

class QMainWindow;
class QWidget;

namespace mac {

// Configures the QMainWindow's NSWindow for a unified, Safari-style titlebar
// (transparent titlebar, hidden title, full-size content view, attached
// NSToolbar with UnifiedCompact style). Applies the dynamic traffic-lights
// leading inset to `toolbarRow`'s contents margins. No-op off macOS.
void integrateUnifiedToolbar(QMainWindow *window, QWidget *toolbarRow, bool compact = true);

// Sets a corner radius on the underlying NSView's CALayer for `widget` (and
// its descendant NSViews). Useful for rounding QWebEngineView's surface.
void roundWidgetCorners(QWidget *widget, double radius);

// Material identifiers (mirrors NSVisualEffectMaterial). 0 == window
// background (under-window), 1 == sidebar, 2 == header (titlebar-style),
// 3 == HUD window, 4 == fullscreen UI, 5 == popover.
enum class VibrancyMaterial { WindowBackground = 0, Sidebar = 1, HeaderView = 2, HUDWindow = 3, FullScreenUI = 4, Popover = 5 };

// Makes the QMainWindow's NSWindow translucent and installs a behind-window
// NSVisualEffectView as the bottom sibling of the content view. No-op off macOS.
void enableWindowVibrancy(QWidget *window, VibrancyMaterial material = VibrancyMaterial::WindowBackground);

// Inserts an NSVisualEffectView as the bottom subview of `widget`'s NSView
// (sized to fill, autoresizing). `widget` should paint transparently
// (Qt::WA_TranslucentBackground + transparent stylesheet) so the blur is visible.
void applyVibrancyBehind(QWidget *widget, VibrancyMaterial material = VibrancyMaterial::Sidebar);

// Asks AppKit / CoreAnimation to drive `window`'s NSWindow at the display's
// maximum refresh rate (e.g. 120Hz on ProMotion panels) instead of the
// default 60Hz. No-op off macOS / on non-ProMotion displays.
void enableHighRefreshRate(QWidget *window);

}  // namespace mac
