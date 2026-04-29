#pragma once

#include <QColor>
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

protected:
    void paintEvent(QPaintEvent *) override;

private:
    QColor m_bg;
    int m_topCornerRadius = 0;
    QVariantAnimation *m_anim = nullptr;
};

// Address-bar pill. Paints a rounded background that transparently inherits
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

}  // namespace ui
