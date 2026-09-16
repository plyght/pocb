#include "ToastWidget.hpp"

#include "MacIntegration.hpp"

#include <QEasingCurve>
#include <QEnterEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QSettings>
#include <QTimer>
#include <QVBoxLayout>
#include <QVariantAnimation>

namespace ui {

namespace {
constexpr int kSlideTravel = 26;
constexpr int kSlideInMs = 220;
constexpr int kSlideOutMs = 160;
}  // namespace

ToastWidget::ToastWidget(const Theme &theme, QWidget *parent)
    : QWidget(parent, Qt::Tool | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint | Qt::WindowDoesNotAcceptFocus),
      m_theme(theme) {
    setObjectName("Toast");
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_Hover, true);
    setFocusPolicy(Qt::NoFocus);
    setFixedSize(Width, Height);
    setCursor(Qt::PointingHandCursor);

    // Native inner view so Qt paints the labels above the glass backdrop
    // (same trick as the floating sidebar).
    m_inner = new QWidget(this);
    m_inner->setObjectName("ToastInner");
    m_inner->setAttribute(Qt::WA_TranslucentBackground);
    m_inner->setAttribute(Qt::WA_NativeWindow);
    m_inner->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_inner->setStyleSheet("QWidget#ToastInner { background: transparent; }");
    m_inner->setGeometry(rect());

    auto *row = new QHBoxLayout(m_inner);
    row->setContentsMargins(14, 0, 16, 0);
    row->setSpacing(12);
    m_iconLabel = new QLabel(m_inner);
    m_iconLabel->setFixedSize(24, 24);
    m_iconLabel->setAttribute(Qt::WA_TranslucentBackground);
    row->addWidget(m_iconLabel, 0, Qt::AlignVCenter);
    auto *column = new QVBoxLayout();
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(1);
    m_titleLabel = new QLabel(m_inner);
    m_titleLabel->setStyleSheet(QString("QLabel { color: %1; font-family: '%2'; font-size: %3px; font-weight: 600; background: transparent; }")
                                    .arg(m_theme.foreground.name(), m_theme.fontFamily, QString::number(m_theme.regularSize)));
    m_subtitleLabel = new QLabel(m_inner);
    m_subtitleLabel->setStyleSheet(QString("QLabel { color: %1; font-family: '%2'; font-size: %3px; background: transparent; }")
                                       .arg(m_theme.muted.name(), m_theme.fontFamily, QString::number(m_theme.smallSize + 1)));
    column->addStretch(1);
    column->addWidget(m_titleLabel);
    column->addWidget(m_subtitleLabel);
    column->addStretch(1);
    row->addLayout(column, 1);

    m_anim = new QVariantAnimation(this);
    m_anim->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_anim, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) { applyProgress(v.toDouble()); });
    connect(m_anim, &QVariantAnimation::finished, this, [this] {
        if (!m_dismissing) return;
        m_dismissing = false;
        hide();
        emit dismissed();
    });

    m_dismissTimer = new QTimer(this);
    m_dismissTimer->setSingleShot(true);
    m_dismissTimer->setInterval(AutoDismissMs);
    connect(m_dismissTimer, &QTimer::timeout, this, &ToastWidget::dismiss);
}

void ToastWidget::setContent(const QString &title, const QString &subtitle, const QIcon &icon) {
    const QIcon shown = icon.isNull() ? mac::sfSymbolIcon("bell.badge", 15.0, m_theme.foreground) : icon;
    m_iconLabel->setPixmap(shown.pixmap(24, 24));
    m_titleLabel->setText(title);
    m_subtitleLabel->setText(subtitle);
    m_subtitleLabel->setVisible(!subtitle.isEmpty());
}

void ToastWidget::applyProgress(qreal t) {
    m_progress = qBound(0.0, t, 1.0);
    move(m_target.x() + qRound(kSlideTravel * (1.0 - m_progress)), m_target.y());
    setWindowOpacity(m_progress);
}

void ToastWidget::presentAt(const QPoint &anchorTopRight) {
    m_target = QPoint(anchorTopRight.x() - Width, anchorTopRight.y());
    m_dismissTimer->stop();
    if (isVisible() && !m_dismissing) {
        // Replace in place: just restart the timer and keep the position.
        m_anim->stop();
        applyProgress(1.0);
        m_dismissTimer->start();
        return;
    }
    m_dismissing = false;
    m_anim->stop();
    applyProgress(0.0);
    show();
    raise();
    m_anim->setDuration(kSlideInMs);
    m_anim->setStartValue(0.0);
    m_anim->setEndValue(1.0);
    m_anim->start();
    m_dismissTimer->start();
}

void ToastWidget::reposition(const QPoint &anchorTopRight) {
    m_target = QPoint(anchorTopRight.x() - Width, anchorTopRight.y());
    if (isVisible()) applyProgress(m_progress);
}

void ToastWidget::dismiss() {
    if (!isVisible() || m_dismissing) return;
    m_dismissTimer->stop();
    m_dismissing = true;
    m_anim->stop();
    m_anim->setDuration(kSlideOutMs);
    m_anim->setStartValue(m_progress);
    m_anim->setEndValue(0.0);
    m_anim->start();
}

void ToastWidget::showEvent(QShowEvent *e) {
    QWidget::showEvent(e);
    m_inner->setGeometry(rect());
    if (!m_chromeApplied) {
        winId();
        const bool glass = QSettings().value("ui/useLiquidGlass", true).toBool();
        if (glass) mac::makeFloatingGlassPanel(this, Radius);
        else mac::makeFloatingVibrantPanel(this, mac::VibrancyMaterial::HUDWindow, Radius);
        m_chromeApplied = true;
    }
}

void ToastWidget::paintEvent(QPaintEvent *) {
    // Fallback body (visible when no native backdrop could be installed) +
    // a specular 1 px inner top highlight that reads through the glass.
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    QPainterPath path;
    path.addRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), Radius, Radius);
    QColor body = m_theme.background;
    body.setAlpha(m_chromeApplied ? 40 : 225);
    p.fillPath(path, body);
    p.save();
    p.setClipPath(path);
    QColor hi = m_theme.foreground;
    hi.setAlpha(36);
    p.setPen(QPen(hi, 1.0));
    p.drawLine(QPointF(Radius, 1.0), QPointF(width() - Radius, 1.0));
    p.restore();
    QColor edge = m_theme.foreground;
    edge.setAlpha(m_pressed ? 60 : 30);
    p.setPen(QPen(edge, 1.0));
    p.setBrush(Qt::NoBrush);
    p.drawPath(path);
}

void ToastWidget::mousePressEvent(QMouseEvent *e) {
    m_pressed = true;
    update();
    e->accept();
}

void ToastWidget::mouseReleaseEvent(QMouseEvent *e) {
    const bool inside = m_pressed && rect().contains(e->pos());
    m_pressed = false;
    update();
    e->accept();
    if (inside) {
        emit clicked();
        dismiss();
    }
}

void ToastWidget::enterEvent(QEnterEvent *) {
    // Hovering pauses auto-dismiss.
    if (!m_dismissing) m_dismissTimer->stop();
}

void ToastWidget::leaveEvent(QEvent *) {
    if (isVisible() && !m_dismissing) m_dismissTimer->start();
}

}  // namespace ui
