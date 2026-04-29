#include "ChromeWidgets.hpp"

#include <QEasingCurve>
#include <QEnterEvent>
#include <QPainter>
#include <QPainterPath>
#include <QVariantAnimation>

namespace ui {

namespace {
QColor lerp(const QColor &a, const QColor &b, qreal t) {
    if (t <= 0) return a;
    if (t >= 1) return b;
    return QColor::fromRgbF(a.redF()   + (b.redF()   - a.redF())   * t,
                            a.greenF() + (b.greenF() - a.greenF()) * t,
                            a.blueF()  + (b.blueF()  - a.blueF())  * t,
                            a.alphaF() + (b.alphaF() - a.alphaF()) * t);
}
}  // namespace

// ---- ChromeBar ----------------------------------------------------------

ChromeBar::ChromeBar(QWidget *parent)
    : QWidget(parent), m_bg(28, 28, 30, 235) {
    setAttribute(Qt::WA_StyledBackground, false);
    setAutoFillBackground(false);
    m_anim = new QVariantAnimation(this);
    m_anim->setDuration(220);
    m_anim->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_anim, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
        m_bg = v.value<QColor>();
        update();
    });
}

void ChromeBar::setBackgroundColor(const QColor &c, bool animate) {
    if (!c.isValid()) return;
    if (!animate || !isVisible()) {
        m_anim->stop();
        m_bg = c;
        update();
        return;
    }
    m_anim->stop();
    m_anim->setStartValue(m_bg);
    m_anim->setEndValue(c);
    m_anim->start();
}

void ChromeBar::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    if (m_topCornerRadius <= 0) {
        p.fillRect(rect(), m_bg);
        return;
    }

    QPainterPath path;
    path.setFillRule(Qt::WindingFill);
    const QRectF r(rect());
    path.addRoundedRect(r, m_topCornerRadius, m_topCornerRadius);
    path.addRect(QRectF(r.left(), r.top() + m_topCornerRadius, r.width(), r.height() - m_topCornerRadius));
    p.fillPath(path, m_bg);
}

// ---- AddrPill -----------------------------------------------------------

AddrPill::AddrPill(QWidget *parent) : QWidget(parent) {
    setAttribute(Qt::WA_Hover, true);
    setAttribute(Qt::WA_StyledBackground, false);
    setAutoFillBackground(false);
    m_anim = new QVariantAnimation(this);
    m_anim->setDuration(140);
    m_anim->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_anim, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
        m_progress = v.toDouble();
        update();
    });
    m_loadAnim = new QVariantAnimation(this);
    m_loadAnim->setDuration(180);
    m_loadAnim->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_loadAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
        m_loadCurrent = v.toDouble();
        update();
    });
}

void AddrPill::setLoadProgress(int percent) {
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    if (percent == m_loadTarget) return;
    m_loadTarget = percent;
    m_loadAnim->stop();
    m_loadAnim->setStartValue(m_loadCurrent);
    // 100% snaps shut quickly: slide to full, then hide via a follow-up to 0.
    if (percent <= 0 || percent >= 100) {
        m_loadAnim->setEndValue(percent <= 0 ? 0.0 : 100.0);
        m_loadAnim->setDuration(percent >= 100 ? 120 : 0);
    } else {
        m_loadAnim->setEndValue((qreal)percent);
        m_loadAnim->setDuration(180);
    }
    m_loadAnim->start();
}

void AddrPill::setLoadColor(const QColor &c) {
    if (c.isValid()) m_loadColor = c;
    update();
}

void AddrPill::setHoverColor(const QColor &c) {
    m_hoverColor = c;
    update();
}

void AddrPill::setIdleColor(const QColor &c) {
    m_idleColor = c;
    update();
}

void AddrPill::setPopped(bool popped) {
    if (m_popped == popped) return;
    m_popped = popped;
    update();
}

void AddrPill::animateTo(qreal target) {
    m_anim->stop();
    m_anim->setStartValue(m_progress);
    m_anim->setEndValue(target);
    m_anim->start();
}

void AddrPill::enterEvent(QEnterEvent *) { animateTo(1.0); }
void AddrPill::leaveEvent(QEvent *)      { animateTo(0.0); }

void AddrPill::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    QPainterPath path;
    path.addRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5),
                        m_radius, m_radius);

    // Idle base — used to give the pill a slightly lighter tone than the
    // surrounding chrome, even when not hovered.
    if (m_idleColor.alpha() > 0) {
        p.fillPath(path, m_idleColor);
    }

    if (m_progress > 0.0) {
        QColor c = m_hoverColor;
        c.setAlphaF(c.alphaF() * m_progress);
        p.fillPath(path, c);
    }

    // Focused/"popped" state: brighten the fill and draw a subtle 1 px
    // border so the pill reads as elevated above the rest of the chrome.
    if (m_popped) {
        p.fillPath(path, QColor(255, 255, 255, 22));
        QPen pen(QColor(255, 255, 255, 60));
        pen.setWidthF(1.0);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        p.drawPath(path);
    }

    // Bottom-edge load strip — clipped to the rounded pill so it follows
    // the curve at the corners. Hidden when fully loaded.
    if (m_loadCurrent > 0.0 && m_loadCurrent < 100.0) {
        p.save();
        p.setClipPath(path);
        const qreal h = 2.0;
        const qreal w = width() * (m_loadCurrent / 100.0);
        const QRectF strip(0, height() - h, w, h);
        p.fillRect(strip, m_loadColor);
        p.restore();
    }
}

}  // namespace ui
