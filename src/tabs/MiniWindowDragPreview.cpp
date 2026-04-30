#include "MiniWindowDragPreview.hpp"

#include "WebView.hpp"

#include <QIcon>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QPixmap>
#include <QTreeWidget>
#include <cmath>

namespace {

constexpr QSize kPreviewSize(190, 122);
constexpr int kRadius = 10;
constexpr int kSidebarWidth = 48;
constexpr int kToolbarHeight = 27;
constexpr int kOuterInset = 1;

QColor vibrantColorFromIcon(const QIcon &icon) {
    const QPixmap pixmap = icon.pixmap(64, 64);
    if (pixmap.isNull()) return QColor();
    const QImage image = pixmap.toImage().convertToFormat(QImage::Format_RGBA8888);
    struct Bucket { double r = 0; double g = 0; double b = 0; double weight = 0; };
    QHash<int, Bucket> buckets;
    const QPointF center((image.width() - 1) / 2.0, (image.height() - 1) / 2.0);
    const double maxDistance = qMax(1.0, std::hypot(center.x(), center.y()));
    for (int y = 0; y < image.height(); ++y) {
        const QRgb *line = reinterpret_cast<const QRgb *>(image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            const QColor color = QColor::fromRgba(line[x]);
            if (color.alpha() < 110) continue;
            const double saturation = color.hslSaturationF();
            const double lightness = color.lightnessF();
            if (saturation < 0.18 || lightness < 0.16 || lightness > 0.88) continue;
            const int hue = color.hslHue();
            if (hue < 0) continue;
            const int bucketKey = (hue / 18) * 18;
            const double centerWeight = 1.0 - (std::hypot(x - center.x(), y - center.y()) / maxDistance) * 0.35;
            const double alphaWeight = color.alphaF();
            const double chromaWeight = 0.35 + saturation * 1.45;
            const double lightnessWeight = 0.45 + (1.0 - qAbs(lightness - 0.52));
            const double weight = qMax(0.0, centerWeight) * alphaWeight * chromaWeight * lightnessWeight;
            auto &bucket = buckets[bucketKey];
            bucket.r += color.red() * weight;
            bucket.g += color.green() * weight;
            bucket.b += color.blue() * weight;
            bucket.weight += weight;
        }
    }
    double bestWeight = 0;
    QColor best;
    for (auto it = buckets.constBegin(); it != buckets.constEnd(); ++it) {
        if (it.value().weight <= bestWeight) continue;
        bestWeight = it.value().weight;
        best = QColor(qRound(it.value().r / it.value().weight),
                      qRound(it.value().g / it.value().weight),
                      qRound(it.value().b / it.value().weight));
    }
    return best;
}

QString shortUrl(WebView *view) {
    if (!view) return QString();
    const QUrl currentUrl = view->url();
    QString value = currentUrl.host().isEmpty()
        ? currentUrl.toDisplayString(QUrl::RemoveScheme | QUrl::RemovePassword | QUrl::StripTrailingSlash)
        : currentUrl.host();
    if (value.startsWith(QStringLiteral("www."))) value.remove(0, 4);
    return value;
}

}

MiniWindowDragPreview::MiniWindowDragPreview(const Theme &theme, QTreeWidget *tabs,
                                             const QHash<QTreeWidgetItem *, WebView *> &views,
                                             QTreeWidgetItem *item)
    : QWidget(nullptr, Qt::Tool | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint),
      m_theme(theme), m_tabs(tabs), m_views(views), m_item(item) {
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_NativeWindow);
    setFocusPolicy(Qt::NoFocus);
    resize(previewSize());
}

QSize MiniWindowDragPreview::sizeHint() const { return previewSize(); }

void MiniWindowDragPreview::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QRect outer = rect().adjusted(kOuterInset, kOuterInset, -kOuterInset - 1, -kOuterInset - 1);
    QPainterPath shellPath;
    shellPath.addRoundedRect(QRectF(outer), kRadius, kRadius);
    painter.setClipPath(shellPath);

    QColor baseTint = m_theme.foreground;
    baseTint.setAlpha(16);
    painter.fillPath(shellPath, baseTint);

    const QRect sidebarRect(outer.left(), outer.top(), kSidebarWidth, outer.height());
    QColor sidebarTint = m_theme.foreground;
    sidebarTint.setAlpha(14);
    painter.fillRect(sidebarRect, sidebarTint);

    const QRect contentRect(sidebarRect.right() + 1, outer.top(), outer.right() - sidebarRect.right(), outer.height());
    QColor contentTint = m_theme.foreground;
    contentTint.setAlpha(7);
    painter.fillRect(contentRect, contentTint);

    QColor divider = m_theme.foreground;
    divider.setAlpha(28);
    painter.fillRect(QRect(contentRect.left(), contentRect.top(), 1, contentRect.height()), divider);

    painter.fillRect(QRect(contentRect.left(), contentRect.top() + kToolbarHeight, contentRect.width(), 1), divider);

    WebView *view = m_item ? m_views.value(m_item, nullptr) : nullptr;
    const QString url = shortUrl(view);
    QColor urlColor = m_theme.foreground;
    urlColor.setAlpha(178);
    painter.setPen(urlColor);
    QFont urlFont = m_tabs ? m_tabs->font() : font();
    urlFont.setPointSizeF(qMax(7.0, urlFont.pointSizeF() - 2.0));
    painter.setFont(urlFont);
    painter.drawText(QRect(contentRect.left() + 9, contentRect.top() + 2, contentRect.width() - 18, kToolbarHeight - 3),
                     Qt::AlignVCenter | Qt::AlignLeft,
                     url.isEmpty() ? QStringLiteral("New tab") : url);

    const QRect pageRect(contentRect.left(), contentRect.top() + kToolbarHeight + 1,
                         contentRect.width(), contentRect.height() - kToolbarHeight - 1);
    QPixmap shot = view ? view->snapshot(pageRect.size()) : QPixmap();
    if (!shot.isNull()) {
        painter.drawPixmap(pageRect, shot, QRect(QPoint(), QSize(qRound(shot.width() / shot.devicePixelRatio()), qRound(shot.height() / shot.devicePixelRatio()))));
    } else {
        QColor page = m_theme.foreground;
        page.setAlpha(216);
        painter.fillRect(pageRect, page);
    }

    if (m_item) {
        const QRect tabRect(sidebarRect.left() + 7, sidebarRect.top() + 31, 29, 17);
        QColor tabFill = m_theme.foreground;
        tabFill.setAlpha(38);
        QColor tabStroke = vibrantColorFromIcon(m_item->icon(0));
        if (!tabStroke.isValid()) tabStroke = m_theme.border;
        tabStroke.setAlpha(150);
        painter.setPen(QPen(tabStroke, 1));
        painter.setBrush(tabFill);
        painter.drawRoundedRect(tabRect, 4, 4);
        m_item->icon(0).paint(&painter, QRect(tabRect.left() + 5, tabRect.top() + 4, 9, 9), Qt::AlignCenter);
        QColor dotColor = m_theme.foreground;
        dotColor.setAlpha(210);
        painter.setPen(dotColor);
        QFont miniFont = m_tabs ? m_tabs->font() : font();
        miniFont.setPointSizeF(qMax(6.0, miniFont.pointSizeF() - 3.0));
        painter.setFont(miniFont);
        painter.drawText(QRect(tabRect.left() + 17, tabRect.top(), tabRect.width() - 18, tabRect.height()), Qt::AlignVCenter | Qt::AlignLeft, QStringLiteral("·"));
    }

    painter.setClipping(false);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(255, 95, 87));
    painter.drawEllipse(QRect(10, 9, 6, 6));
    painter.setBrush(QColor(255, 189, 46));
    painter.drawEllipse(QRect(20, 9, 6, 6));
    painter.setBrush(QColor(40, 200, 64));
    painter.drawEllipse(QRect(30, 9, 6, 6));

    QColor outerStroke = m_theme.foreground;
    outerStroke.setAlpha(38);
    painter.setPen(QPen(outerStroke, 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(outer, kRadius, kRadius);
}

QSize MiniWindowDragPreview::previewSize() { return kPreviewSize; }
