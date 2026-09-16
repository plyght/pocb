#pragma once

#include <QColor>
#include <QPointer>
#include <QToolButton>
#include <QWidget>

class QVariantAnimation;

namespace ui {

// Toolbar backdrop. Paints a flat colour itself (no stylesheet recompute on
// page-colour changes) and smoothly animates between previous and new
// page-colour. Children remain stylesheet-driven.
class ChromeBar final : public QWidget {
    Q_OBJECT
public:
    explicit ChromeBar(QWidget *parent = nullptr);

    void setBackgroundColor(const QColor &c, bool animate = true);
    QColor backgroundColor() const { return m_bg; }
    void setTopCornerRadius(int px) { m_topCornerRadius = px; update(); }
    void setTopCornerMask(bool left, bool right) { m_roundTopLeft = left; m_roundTopRight = right; update(); }
    // Leave a rounded hole under `child` (a direct child) so a Liquid Glass
    // view stacked beneath the Qt content shows through there. nullptr clears.
    void setGlassCutout(QWidget *child, qreal radius);
    // Paint nothing: a Liquid Glass view stacked beneath provides the surface.
    void setGlassBacked(bool on) { m_glassBacked = on; update(); }
    bool glassBacked() const { return m_glassBacked; }

protected:
    void paintEvent(QPaintEvent *) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QColor m_bg;
    bool m_glassBacked = false;
    int m_topCornerRadius = 0;
    bool m_roundTopLeft = true;
    bool m_roundTopRight = true;
    QVariantAnimation *m_anim = nullptr;
    QPointer<QWidget> m_cutout;
    qreal m_cutoutRadius = 0.0;
};

// Address-bar pill. Paints a rounded background that transparently inherits
// Capsule that groups toolbar buttons. Paints a translucent fill unless a
// Liquid Glass view is stacked beneath it.
class ToolbarCluster final : public QWidget {
    Q_OBJECT
public:
    explicit ToolbarCluster(QWidget *parent = nullptr);
    void setFillColor(const QColor &c) { m_fill = c; update(); }
    void setGlassBacked(bool on) { m_glass = on; update(); }
    bool glassBacked() const { return m_glass; }
    qreal radius() const { return height() / 2.0; }

protected:
    void paintEvent(QPaintEvent *) override;

private:
    QColor m_fill;
    bool m_glass = false;
};

// the parent toolbar colour at rest, fades to a per-page hover tone on
// mouse enter, and fades back on leave.
class AddrPill final : public QWidget {
    Q_OBJECT
public:
    explicit AddrPill(QWidget *parent = nullptr);

    // Idle = transparent (toolbar shows through); hover blends towards `c`.
    void setHoverColor(const QColor &c);
    void setIdleColor(const QColor &c);
    void setPopped(bool popped);
    void setRadius(int px) { m_radius = px; update(); }
    int radius() const { return m_radius; }
    // Focus ring tint used for the popped state (defaults to a soft white).
    void setFocusColor(const QColor &c) { m_focusColor = c; update(); }
    // Glass mode: a Liquid Glass NSView sits behind the pill, so the idle
    // fill is thinned out and a specular top highlight is painted instead.
    void setGlassMode(bool glass) { m_glass = glass; update(); }
    bool glassMode() const { return m_glass; }

    // 0..100, 0/100 hides the strip. Animates between intermediate values
    // for a smooth fill rather than discrete snaps from WebKit's progress
    // notifications.
    void setLoadProgress(int percent);
    void setLoadColor(const QColor &c);

protected:
    void paintEvent(QPaintEvent *) override;
    void enterEvent(QEnterEvent *) override;
    void leaveEvent(QEvent *) override;

private:
    void animateTo(qreal target);

    QColor m_hoverColor = QColor(255, 255, 255, 24);
    QColor m_idleColor = QColor(0, 0, 0, 0);
    QColor m_focusColor = QColor(255, 255, 255, 60);
    bool m_glass = false;
    bool m_popped = false;
    qreal m_progress = 0.0;
    int m_radius = 7;
    QVariantAnimation *m_anim = nullptr;

    int m_loadTarget = 0;
    qreal m_loadCurrent = 0.0;
    qreal m_loadPulse = 0.0;
    QColor m_loadColor = QColor(120, 180, 255, 235);
    QVariantAnimation *m_loadAnim = nullptr;
    QVariantAnimation *m_loadPulseAnim = nullptr;
};

// Toolbar downloads button. Plain SF Symbol when idle; while downloads are
// active it overlays a thin progress ring around the icon plus a count badge.
class DownloadsButton final : public QToolButton {
    Q_OBJECT
public:
    explicit DownloadsButton(QWidget *parent = nullptr);

    // activeCount <= 0 hides the ring/badge. progress in 0..1; a negative
    // value shows an indeterminate spinning arc.
    void setActivity(int activeCount, double progress);
    void setRingColor(const QColor &c) { m_ring = c; update(); }
    void setTrackColor(const QColor &c) { m_track = c; update(); }
    void setBadgeTextColor(const QColor &c) { m_badgeText = c; update(); }
    int activeCount() const { return m_active; }

protected:
    void paintEvent(QPaintEvent *) override;

private:
    int m_active = 0;
    double m_progress = 0.0;
    qreal m_spin = 0.0;
    QColor m_ring = QColor(120, 180, 255);
    QColor m_track = QColor(255, 255, 255, 50);
    QColor m_badgeText = QColor(20, 20, 22);
    QVariantAnimation *m_spinAnim = nullptr;
};

}  // namespace ui
