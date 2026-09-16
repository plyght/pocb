#include "SidebarWidgets.hpp"

#include <QFont>
#include <QHash>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QStyleOption>
#include <QVariantAnimation>
#include <QtMath>

namespace ui {

QColor profileAvatarColor(const QString &profileName) {
    static const QColor palette[] = {
        QColor(0x5E, 0x8B, 0xFF),  // blue
        QColor(0xC5, 0x6B, 0xFF),  // violet
        QColor(0xFF, 0x7A, 0x59),  // coral
        QColor(0x3E, 0xC6, 0x9E),  // teal
        QColor(0xF2, 0xB1, 0x34),  // amber
        QColor(0xFF, 0x5F, 0x8A),  // pink
        QColor(0x4F, 0xC3, 0xF7),  // sky
        QColor(0x9C, 0xCC, 0x65),  // lime
    };
    if (profileName.isEmpty() || profileName == QLatin1String("Default")) return palette[0];
    const uint h = qHash(profileName.toLower(), 0x9e3779b9u);
    return palette[h % (sizeof(palette) / sizeof(palette[0]))];
}

// ---- ProfileAvatarButton --------------------------------------------------

ProfileAvatarButton::ProfileAvatarButton(const Theme &theme, QWidget *parent)
    : QToolButton(parent), m_theme(theme) {
    setToolButtonStyle(Qt::ToolButtonIconOnly);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::NoFocus);
    setAttribute(Qt::WA_Hover, true);
    setAutoRaise(true);
    setDiameter(m_diameter);
    setStyleSheet("QToolButton { background: transparent; border: none; padding: 0px; }");
}

void ProfileAvatarButton::setProfileName(const QString &name) {
    m_name = name;
    setToolTip(QStringLiteral("Profile: %1").arg(name.isEmpty() ? QStringLiteral("Default") : name));
    update();
}

void ProfileAvatarButton::setActive(bool active) {
    if (m_active == active) return;
    m_active = active;
    update();
}

void ProfileAvatarButton::setDiameter(int px) {
    m_diameter = qMax(16, px);
    setFixedSize(m_diameter + 6, m_diameter + 6);
    update();
}

void ProfileAvatarButton::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QRectF bounds(rect());
    const bool hovered = underMouse();
    if (hovered || isDown()) {
        QColor halo = m_theme.foreground;
        halo.setAlpha(isDown() ? 34 : 20);
        p.setPen(Qt::NoPen);
        p.setBrush(halo);
        p.drawRoundedRect(bounds, 8, 8);
    }
    const QString shown = m_name.isEmpty() ? QStringLiteral("Default") : m_name;
    const QColor base = profileAvatarColor(shown);
    const qreal d = m_diameter;
    const QRectF disc(bounds.center().x() - d / 2.0, bounds.center().y() - d / 2.0, d, d);
    QColor shadow(0, 0, 0, m_theme.background.lightness() < 128 ? 70 : 34);
    p.setPen(Qt::NoPen);
    p.setBrush(shadow);
    p.drawEllipse(disc.translated(0, 1.0).adjusted(-0.5, -0.5, 0.5, 0.5));
    QLinearGradient fill(disc.topLeft(), disc.bottomLeft());
    fill.setColorAt(0.0, base.lighter(m_active ? 124 : 112));
    fill.setColorAt(1.0, base.darker(m_active ? 106 : 118));
    p.setBrush(fill);
    p.drawEllipse(disc);
    QPen rim(QColor(255, 255, 255, m_active ? 96 : 56), 1.0);
    p.setPen(rim);
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(disc.adjusted(0.5, 0.5, -0.5, -0.5));
    QFont f = font();
    f.setPixelSize(qRound(d * 0.46));
    f.setWeight(QFont::Bold);
    p.setFont(f);
    p.setPen(QColor(255, 255, 255, 240));
    p.drawText(disc, Qt::AlignCenter, shown.left(1).toUpper());
}

// ---- PagerDots ------------------------------------------------------------

PagerDots::PagerDots(const Theme &theme, QWidget *parent) : QWidget(parent), m_theme(theme) {
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_TranslucentBackground);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    setFixedHeight(12);
}

void PagerDots::setCount(int count) {
    m_count = qMax(1, count);
    updateGeometry();
    update();
}

void PagerDots::setPosition(qreal position) {
    m_position = qBound(-0.5, position, m_count - 0.5);
    update();
}

QSize PagerDots::sizeHint() const {
    return QSize(m_count * 12 + 4, 12);
}

void PagerDots::paintEvent(QPaintEvent *) {
    if (m_count <= 1) return;
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const qreal step = 12.0;
    const qreal total = m_count * step;
    const qreal x0 = (width() - total) / 2.0 + step / 2.0;
    const qreal cy = height() / 2.0;
    QColor inactive = m_theme.foreground;
    inactive.setAlpha(70);
    p.setPen(Qt::NoPen);
    for (int i = 0; i < m_count; ++i) {
        p.setBrush(inactive);
        p.drawEllipse(QPointF(x0 + i * step, cy), 2.5, 2.5);
    }
    // Sliding active pill: stretches while the position sits between dots.
    const qreal pos = qBound(0.0, m_position, m_count - 1.0);
    const int lo = int(qFloor(pos));
    const int hi = qMin(m_count - 1, lo + 1);
    const qreal frac = pos - lo;
    const qreal left = x0 + lo * step + (hi != lo ? qMax(0.0, frac - 0.5) * 2.0 * step : 0.0);
    const qreal right = x0 + lo * step + (hi != lo ? qMin(frac, 0.5) * 2.0 * step : 0.0);
    QColor active = m_theme.foreground;
    active.setAlpha(230);
    p.setBrush(active);
    p.drawRoundedRect(QRectF(left - 3.0, cy - 3.0, (right - left) + 6.0, 6.0), 3.0, 3.0);
}

// ---- SidebarActionRow -----------------------------------------------------

SidebarActionRow::SidebarActionRow(const Theme &theme, const QIcon &icon, const QString &label, QWidget *parent)
    : QToolButton(parent), m_theme(theme), m_icon(icon), m_label(label) {
    setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::NoFocus);
    setAttribute(Qt::WA_Hover, true);
    setAutoRaise(true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setStyleSheet("QToolButton { background: transparent; border: none; padding: 0px; }");
}

void SidebarActionRow::setIcon(const QIcon &icon) {
    m_icon = icon;
    update();
}

QSize SidebarActionRow::sizeHint() const {
    return QSize(120, 34);
}

void SidebarActionRow::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const bool hovered = underMouse();
    const bool pressed = isDown();
    if (hovered || pressed) {
        QColor fill(255, 255, 255);
        fill.setAlpha(m_theme.background.lightness() < 128 ? (pressed ? 30 : 18) : (pressed ? 150 : 95));
        p.setPen(Qt::NoPen);
        p.setBrush(fill);
        p.drawRoundedRect(QRectF(rect()).adjusted(0, 2, 0, -2), 9, 9);
    }
    const QRect iconRect(12, (height() - 18) / 2, 18, 18);
    m_icon.paint(&p, iconRect, Qt::AlignCenter);
    QFont f = font();
    f.setWeight(QFont::Medium);
    p.setFont(f);
    QColor text = m_theme.foreground;
    text.setAlpha(hovered ? 255 : 225);
    p.setPen(text);
    const QRect textRect(iconRect.right() + 11, 0, width() - iconRect.right() - 19, height());
    p.drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft,
               QFontMetrics(f).elidedText(m_label, Qt::ElideRight, textRect.width()));
}

// ---- SidebarPanel ---------------------------------------------------------

SidebarPanel::SidebarPanel(const Theme &theme, QWidget *parent) : QWidget(parent), m_theme(theme) {
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_StyledBackground, false);
    setAutoFillBackground(false);
}

void SidebarPanel::paintEvent(QPaintEvent *) {}

// ---- SidebarPreviewPane ---------------------------------------------------

SidebarPreviewPane::SidebarPreviewPane(QWidget *parent) : QWidget(parent) {
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAutoFillBackground(false);
}

void SidebarPreviewPane::setSnapshot(const QPixmap &pixmap) {
    m_pixmap = pixmap;
    update();
}

void SidebarPreviewPane::setReveal(qreal opacity, int parallaxDx) {
    const qreal clamped = qBound(0.0, opacity, 1.0);
    if (qFuzzyCompare(clamped, m_opacity) && parallaxDx == m_parallaxDx) return;
    m_opacity = clamped;
    m_parallaxDx = parallaxDx;
    update();
}

void SidebarPreviewPane::clearSnapshot() {
    m_pixmap = QPixmap();
    m_opacity = 1.0;
    m_parallaxDx = 0;
    update();
}

void SidebarPreviewPane::paintEvent(QPaintEvent *) {
    if (m_pixmap.isNull()) return;
    QPainter p(this);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    p.setOpacity(m_opacity);
    const QSizeF logical = m_pixmap.deviceIndependentSize();
    p.drawPixmap(QRectF(QPointF(m_parallaxDx, 0), logical), m_pixmap, QRectF(QPointF(0, 0), m_pixmap.size()));
}

}  // namespace ui
