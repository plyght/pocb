#pragma once

#include <QIcon>
#include <QString>

class QColor;
class QMainWindow;
class QWidget;

namespace mac {

// Renders a system SF Symbol (macOS 11+) as a QIcon. Returns null QIcon on
// older macOS or non-Apple targets.
QIcon sfSymbolIcon(const QString &name, double pointSize, const QColor &color);

// Configures the QMainWindow's NSWindow for a unified, Safari-style titlebar
// (transparent titlebar, hidden title, full-size content view, attached
// NSToolbar with UnifiedCompact style). Applies the dynamic traffic-lights
// leading inset to `toolbarRow`'s contents margins. No-op off macOS.
void integrateUnifiedToolbar(QMainWindow *window, QWidget *toolbarRow, bool compact = true);

// Sets a corner radius on the underlying NSView's CALayer for `widget`. When
// `recurseDescendants` is true (default) the same radius is applied to every
// descendant NSView — necessary for QWebEngineView/WKWebView's nested layer
// stack. Pass false when you want the parent layer's masksToBounds to be the
// only thing clipping the descendants (e.g. a toolbar+webview wrapper).
void roundWidgetCorners(QWidget *widget, double radius, bool recurseDescendants = true);

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

// Hide or show the three standard window buttons (close/min/zoom) on the
// QMainWindow's NSWindow. Useful for collapsing them with a sidebar.
void setTrafficLightsHidden(QWidget *window, bool hidden);

// Asks AppKit / CoreAnimation to drive `window`'s NSWindow at the display's
// maximum refresh rate (e.g. 120Hz on ProMotion panels) instead of the
// default 60Hz. No-op off macOS / on non-ProMotion displays.
void enableHighRefreshRate(QWidget *window);

}  // namespace mac
