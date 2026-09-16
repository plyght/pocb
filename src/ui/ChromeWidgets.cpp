#include "ChromeWidgets.hpp"

#include <QEasingCurve>
#include <QEnterEvent>
#include <QEvent>
#include <QFont>
#include <QFontMetrics>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
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

QEasingCurve responsiveEaseOut() {
    QEasingCurve curve(QEasingCurve::BezierSpline);
    curve.addCubicBezierSegment(QPointF(0.23, 1.0), QPointF(0.32, 1.0), QPointF(1.0, 1.0));
    return curve;
}
}  // namespace

// ---- ChromeBar ----------------------------------------------------------

ChromeBar::ChromeBar(QWidget *parent)
    : QWidget(parent), m_bg(28, 28, 30, 235) {
    setAttribute(Qt::WA_StyledBackground, false);
    setAutoFillBackground(false);
    m_anim = new QVariantAnimation(this);
    m_anim->setDuration(180);
    m_anim->setEasingCurve(responsiveEaseOut());
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

void ChromeBar::setGlassCutout(QWidget *child, qreal radius) {
    if (m_cutout) m_cutout->removeEventFilter(this);
    m_cutout = child;
    m_cutoutRadius = radius;
    if (m_cutout) m_cutout->installEventFilter(this);
    update();
}

bool ChromeBar::eventFilter(QObject *watched, QEvent *event) {
    if (watched == m_cutout) {
        switch (event->type()) {
        case QEvent::Move:
        case QEvent::Resize:
        case QEvent::Show:
        case QEvent::Hide:
            update();
            break;
        default:
            break;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void ChromeBar::paintEvent(QPaintEvent *) {
    if (m_glassBacked) return;
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    QPainterPath path;
    const QRectF r(rect());
    if (m_topCornerRadius <= 0) {
        path.addRect(r);
    } else {
        path.setFillRule(Qt::WindingFill);
        path.addRoundedRect(r, m_topCornerRadius, m_topCornerRadius);
        path.addRect(QRectF(r.left(), r.top() + m_topCornerRadius, r.width(), r.height() - m_topCornerRadius));
        if (!m_roundTopLeft) path.addRect(QRectF(r.left(), r.top(), m_topCornerRadius, m_topCornerRadius));
        if (!m_roundTopRight) path.addRect(QRectF(r.right() - m_topCornerRadius, r.top(), m_topCornerRadius, m_topCornerRadius));
        path = path.simplified();
    }
    if (m_cutout && m_cutout->isVisible()) {
        QPainterPath hole;
        hole.addRoundedRect(QRectF(m_cutout->geometry()), m_cutoutRadius, m_cutoutRadius);
        path = path.subtracted(hole);
    }
    p.fillPath(path, m_bg);
}

// ---- ToolbarCluster -------------------------------------------------------

ToolbarCluster::ToolbarCluster(QWidget *parent) : QWidget(parent) {
    setAttribute(Qt::WA_StyledBackground, false);
    setAutoFillBackground(false);
}

void ToolbarCluster::paintEvent(QPaintEvent *) {
    if (m_glass || !m_fill.isValid()) return;
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(Qt::NoPen);
    p.setBrush(m_fill);
    p.drawRoundedRect(QRectF(rect()), radius(), radius());
}

// ---- AddrPill -----------------------------------------------------------

AddrPill::AddrPill(QWidget *parent) : QWidget(parent) {
    setAttribute(Qt::WA_Hover, true);
    setAttribute(Qt::WA_StyledBackground, false);
    setAutoFillBackground(false);
    m_anim = new QVariantAnimation(this);
    m_anim->setDuration(125);
    m_anim->setEasingCurve(responsiveEaseOut());
    connect(m_anim, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
        m_progress = v.toDouble();
        update();
    });
    m_loadAnim = new QVariantAnimation(this);
    m_loadAnim->setDuration(115);
    m_loadAnim->setEasingCurve(responsiveEaseOut());
    connect(m_loadAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
        m_loadCurrent = v.toDouble();
        update();
    });
    connect(m_loadAnim, &QVariantAnimation::finished, this, [this] {
        if (m_loadTarget >= 100) {
            m_loadCurrent = 0.0;
            m_loadPulseAnim->stop();
            update();
        }
    });
    m_loadPulseAnim = new QVariantAnimation(this);
    m_loadPulseAnim->setStartValue(0.0);
    m_loadPulseAnim->setEndValue(1.0);
    m_loadPulseAnim->setDuration(1100);
    m_loadPulseAnim->setLoopCount(-1);
    m_loadPulseAnim->setEasingCurve(QEasingCurve::Linear);
    connect(m_loadPulseAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
        m_loadPulse = v.toDouble();
        update();
    });
}

void AddrPill::setLoadProgress(int percent) {
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    if (percent == m_loadTarget) return;
    if (percent > 0 && percent < 100 && m_loadCurrent >= 100.0) {
        m_loadCurrent = 0.0;
    }
    m_loadTarget = percent;
    m_loadAnim->stop();
    m_loadAnim->setStartValue(m_loadCurrent);
    if (percent <= 0) {
        m_loadCurrent = 0.0;
        m_loadPulseAnim->stop();
        update();
        return;
    }
    if (percent >= 100) {
        m_loadAnim->setEndValue(100.0);
        m_loadAnim->setDuration(90);
    } else {
        if (m_loadPulseAnim->state() != QAbstractAnimation::Running) m_loadPulseAnim->start();
        const qreal visualTarget = qMax((qreal)percent, 96.0);
        m_loadAnim->setEndValue(visualTarget);
        const int delta = qAbs(qRound(visualTarget - m_loadCurrent));
        m_loadAnim->setDuration(qBound(60, 24 + delta * 2, 120));
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
        QColor idle = m_idleColor;
        // In glass mode the NSGlassEffectView behind us supplies the body;
        // keep only a faint tint so the refraction stays visible.
        if (!m_glass) p.fillPath(path, idle);
    }

    if (m_progress > 0.0) {
        QColor c = m_hoverColor;
        c.setAlphaF(c.alphaF() * m_progress * (m_glass ? 0.45 : 1.0));
        p.fillPath(path, c);
    }

    if (m_glass) {
        // Specular 1 px inner top highlight + soft edge stroke.
        p.save();
        p.setClipPath(path);
        QLinearGradient sheen(0, 0, 0, height());
        sheen.setColorAt(0.0, QColor(255, 255, 255, 34));
        sheen.setColorAt(0.5, QColor(255, 255, 255, 6));
        sheen.setColorAt(1.0, QColor(255, 255, 255, 0));
        p.fillPath(path, sheen);
        p.setPen(QPen(QColor(255, 255, 255, 46), 1.0));
        p.drawLine(QPointF(m_radius, 1.0), QPointF(width() - m_radius, 1.0));
        p.restore();
        const bool lightIdle = m_idleColor.alpha() > 0 && m_idleColor.lightness() < 128;
        QPen edge(lightIdle ? QColor(0, 0, 0, 30) : QColor(255, 255, 255, 26));
        edge.setWidthF(1.0);
        p.setPen(edge);
        p.setBrush(Qt::NoBrush);
        p.drawPath(path);
    }

    // Focused/"popped" state: brighten the fill and draw a subtle 1 px
    // border so the pill reads as elevated above the rest of the chrome.
    if (m_popped) {
        p.fillPath(path, QColor(255, 255, 255, 22));
        QPen pen(m_focusColor);
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
        QColor load = m_loadColor;
        load.setAlphaF(qMin(1.0, load.alphaF() * 0.86));
        p.fillRect(strip, load);
        const qreal pulseWidth = qMax<qreal>(26.0, qMin<qreal>(72.0, width() * 0.18));
        const qreal edgePulse = 0.5 - 0.5 * qCos(m_loadPulse * 6.283185307179586);
        const qreal center = w - pulseWidth * (0.38 + edgePulse * 0.18);
        const qreal left = qMax<qreal>(0.0, center - pulseWidth * 0.62);
        const qreal right = qMin<qreal>(w, center + pulseWidth * 0.38);
        if (right > left) {
            QLinearGradient shine(left, 0.0, right, 0.0);
            QColor edge = m_loadColor.lighter(110);
            edge.setAlphaF(0.0);
            QColor mid = m_loadColor.lighter(155);
            mid.setAlphaF(qMin(1.0, mid.alphaF() * (0.52 + edgePulse * 0.36)));
            QColor tip = m_loadColor.lighter(170);
            tip.setAlphaF(qMin(1.0, tip.alphaF() * (0.70 + edgePulse * 0.25)));
            shine.setColorAt(0.0, edge);
            shine.setColorAt(0.58, mid);
            shine.setColorAt(1.0, tip);
            p.fillRect(QRectF(left, height() - h, right - left, h), shine);
        }
        p.restore();
    }
}

// ---- DownloadsButton ----------------------------------------------------

DownloadsButton::DownloadsButton(QWidget *parent) : QToolButton(parent) {
    m_spinAnim = new QVariantAnimation(this);
    m_spinAnim->setStartValue(0.0);
    m_spinAnim->setEndValue(1.0);
    m_spinAnim->setDuration(900);
    m_spinAnim->setLoopCount(-1);
    m_spinAnim->setEasingCurve(QEasingCurve::Linear);
    connect(m_spinAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
        m_spin = v.toDouble();
        update();
    });
}

void DownloadsButton::setActivity(int activeCount, double progress) {
    m_active = qMax(0, activeCount);
    m_progress = progress < 0.0 ? -1.0 : qBound(0.0, progress, 1.0);
    const bool spin = m_active > 0 && m_progress < 0.0;
    if (spin && m_spinAnim->state() != QAbstractAnimation::Running) m_spinAnim->start();
    if (!spin && m_spinAnim->state() == QAbstractAnimation::Running) m_spinAnim->stop();
    setToolTip(m_active > 0
                   ? QStringLiteral("Downloads — %1 active").arg(m_active)
                   : QStringLiteral("Downloads"));
    update();
}

void DownloadsButton::paintEvent(QPaintEvent *e) {
    QToolButton::paintEvent(e);
    if (m_active <= 0) return;
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QRectF r(rect());
    const qreal ringSide = qMin(r.width(), r.height()) - 8.0;
    QRectF ring(r.center().x() - ringSide / 2.0, r.center().y() - ringSide / 2.0, ringSide, ringSide);
    ring.adjust(1.0, 1.0, -1.0, -1.0);
    QPen track(m_track, 1.6);
    track.setCapStyle(Qt::RoundCap);
    p.setPen(track);
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(ring);
    QPen arc(m_ring, 1.8);
    arc.setCapStyle(Qt::RoundCap);
    p.setPen(arc);
    if (m_progress < 0.0) {
        const int start = qRound(90.0 * 16 - m_spin * 360.0 * 16);
        p.drawArc(ring, start, -110 * 16);
    } else {
        p.drawArc(ring, 90 * 16, -qRound(m_progress * 360.0 * 16));
    }
    // Count badge, top-right.
    const qreal badge = 12.0;
    const QRectF badgeRect(r.right() - badge - 2.0, r.top() + 2.0, badge, badge);
    p.setPen(Qt::NoPen);
    p.setBrush(m_ring);
    p.drawEllipse(badgeRect);
    QFont f = font();
    f.setPixelSize(8);
    f.setWeight(QFont::Bold);
    p.setFont(f);
    p.setPen(m_badgeText);
    p.drawText(badgeRect, Qt::AlignCenter, m_active > 9 ? QStringLiteral("9+") : QString::number(m_active));
}

}  // namespace ui
