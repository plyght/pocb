#pragma once

#include <QMargins>
#include <QPoint>
#include <QRect>
#include <QSize>
#include <QWidget>

namespace ui::metrics {

inline constexpr int WindowDefaultWidth = 1280;
inline constexpr int WindowDefaultHeight = 820;
inline constexpr int WebContainerRadius = 10;
inline constexpr int FloatingPanelRadius = 12;
inline constexpr int UnifiedToolbarHeight = 52;
inline constexpr int TrafficLightInteractionInset = 28;
inline constexpr int SidebarMinimumWidth = 190;
inline constexpr int SidebarMaximumWidth = 520;
inline constexpr int SidebarDefaultWidth = 240;
inline constexpr int SplitterHandleWidth = 8;
inline constexpr int SidebarHoverZoneWidth = 48;
inline constexpr int SidebarCollapseNumerator = 7;
inline constexpr int SidebarCollapseDenominator = 10;
inline constexpr int StackHostVisibleLeftInset = 0;
inline constexpr int StackHostHiddenLeftInset = 6;
inline constexpr int StackHostTopInset = 6;
inline constexpr int StackHostRightInset = 6;
inline constexpr int StackHostBottomInset = 6;
inline constexpr int FloatingSidebarSideGap = 8;
inline constexpr int FloatingSidebarBottomGap = 8;
inline constexpr int FloatingDismissDelayMs = 180;
inline constexpr int FloatingSlideDurationMs = 160;
inline constexpr int DockedSidebarTopInset = UnifiedToolbarHeight;
inline constexpr int DockedSidebarLeftInset = 10;
inline constexpr int DockedSidebarRightInset = 0;
inline constexpr int DockedSidebarBottomInset = 10;
inline constexpr int SidebarHeaderNavHeight = UnifiedToolbarHeight;
inline constexpr int SidebarHeaderNavTopInset = 11;
inline constexpr int SidebarHeaderNavRightInset = 6;
inline constexpr int SidebarHeaderTrafficLightClearance = 96;
inline constexpr int CopiedLinkPopupWidth = 118;
inline constexpr int CopiedLinkPopupHeight = 54;
inline constexpr int CopiedLinkPopupInset = 14;

inline QMargins stackHostMargins(bool sidebarVisible) {
    return QMargins(sidebarVisible ? StackHostVisibleLeftInset : StackHostHiddenLeftInset,
                    StackHostTopInset,
                    StackHostRightInset,
                    StackHostBottomInset);
}

inline QRect windowContentRect(QWidget *window) {
    if (!window) return {};
    return QRect(window->mapToGlobal(QPoint(0, 0)), window->size());
}

inline QRect sidebarHoverZoneRect(QWidget *window) {
    const QRect frame = windowContentRect(window);
    return QRect(frame.left(), frame.top() + TrafficLightInteractionInset,
                 SidebarHoverZoneWidth,
                 qMax(0, frame.height() - TrafficLightInteractionInset));
}

inline QRect floatingSidebarRect(QWidget *window, int width) {
    const QRect frame = windowContentRect(window);
    return QRect(frame.left() + FloatingSidebarSideGap,
                 frame.top() + UnifiedToolbarHeight,
                 width,
                 qMax(0, frame.height() - UnifiedToolbarHeight - FloatingSidebarBottomGap));
}

inline int sidebarCollapseThreshold(int minimumWidth) {
    return minimumWidth * SidebarCollapseNumerator / SidebarCollapseDenominator;
}

}  // namespace ui::metrics
