#include "ToolbarCollapse.hpp"

#include <QEasingCurve>
#include <QEnterEvent>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QResizeEvent>
#include <QVariantAnimation>
#include <QtMath>

namespace ui {

// ---- CollapsingToolbarHost ------------------------------------------------

CollapsingToolbarHost::CollapsingToolbarHost(QWidget *parent) : QWidget(parent) {
    setObjectName("ToolbarHost");
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_StyledBackground, false);
    setAutoFillBackground(false);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void CollapsingToolbarHost::setRow(QWidget *row, QWidget *hairline) {
    m_row = row;
    m_hairline = hairline;
    if (m_row) m_row->setParent(this);
    if (m_hairline) m_hairline->setParent(this);
    syncHeight();
}

int CollapsingToolbarHost::expandedHeight() const {
    int h = 0;
    if (m_row) h += m_row->height() > 0 ? m_row->height() : m_row->sizeHint().height();
    if (m_hairline) h += m_hairline->height() > 0 ? m_hairline->height() : 1;
    return h;
}

void CollapsingToolbarHost::setProgress(qreal t) {
    const qreal clamped = qBound(0.0, t, 1.0);
    if (qFuzzyCompare(clamped + 1.0, m_progress + 1.0)) return;
    m_progress = clamped;
    syncHeight();
}

void CollapsingToolbarHost::syncHeight() {
    const int full = expandedHeight();
    const int h = qRound(full * m_progress);
    if (h != height() || minimumHeight() != h || maximumHeight() != h) setFixedHeight(h);
    layoutChildren();
}

void CollapsingToolbarHost::paintEvent(QPaintEvent *e) {
    QPainter p(this);
    p.setCompositionMode(QPainter::CompositionMode_Source);
    p.fillRect(e->rect(), Qt::transparent);
}

void CollapsingToolbarHost::resizeEvent(QResizeEvent *e) {
    QWidget::resizeEvent(e);
    layoutChildren();
}

void CollapsingToolbarHost::layoutChildren() {
    const int full = expandedHeight();
    // Children are pinned to the host's bottom edge so they translate
    // upward as the host shrinks instead of being resized.
    const int top = height() - full;
    int y = top;
    if (m_row) {
        const int rowH = m_row->height() > 0 ? m_row->height() : m_row->sizeHint().height();
        m_row->setGeometry(0, y, width(), rowH);
        y += rowH;
    }
    if (m_hairline) {
        m_hairline->setGeometry(0, y, width(), 1);
    }
}

// ---- ToolbarGrabber -------------------------------------------------------

ToolbarGrabber::ToolbarGrabber(QWidget *parent)
    : QWidget(parent, Qt::Tool | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint | Qt::WindowDoesNotAcceptFocus) {
    setObjectName("ToolbarGrabber");
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_Hover, true);
    setFocusPolicy(Qt::NoFocus);
    setMouseTracking(true);
    m_anim = new QVariantAnimation(this);
    m_anim->setDuration(140);
    m_anim->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_anim, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
        m_hover = v.toDouble();
        update();
    });
}

void ToolbarGrabber::setPillColor(const QColor &c) {
    m_pill = c;
    update();
}

void ToolbarGrabber::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const qreal w = 36.0 + 18.0 * m_hover;
    const qreal h = 3.0 + 2.0 * m_hover;
    const QRectF pill(width() / 2.0 - w / 2.0, 2.0, w, h);
    QPainterPath path;
    path.addRoundedRect(pill, h / 2.0, h / 2.0);
    // Glass body: translucent tint + specular top gradient + thin edge.
    QColor body = m_pill;
    body.setAlpha(qRound(body.alpha() * (0.55 + 0.45 * m_hover)));
    p.fillPath(path, body);
    QLinearGradient sheen(pill.topLeft(), pill.bottomLeft());
    sheen.setColorAt(0.0, QColor(255, 255, 255, 110));
    sheen.setColorAt(1.0, QColor(255, 255, 255, 0));
    p.fillPath(path, sheen);
    QPen edge(QColor(0, 0, 0, 40));
    edge.setWidthF(0.8);
    p.setPen(edge);
    p.setBrush(Qt::NoBrush);
    p.drawPath(path);
}

void ToolbarGrabber::enterEvent(QEnterEvent *) {
    m_anim->stop();
    m_anim->setStartValue(m_hover);
    m_anim->setEndValue(1.0);
    m_anim->start();
    emit hovered();
}

void ToolbarGrabber::leaveEvent(QEvent *) {
    m_anim->stop();
    m_anim->setStartValue(m_hover);
    m_anim->setEndValue(0.0);
    m_anim->start();
}

void ToolbarGrabber::mousePressEvent(QMouseEvent *e) {
    e->accept();
    emit hovered();
}

}  // namespace ui
