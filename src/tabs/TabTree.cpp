#include "TabTree.hpp"

#include "FaviconService.hpp"
#include "LayoutMetrics.hpp"
#include "MacIntegration.hpp"
#include "ProfileStore.hpp"
#include "WebView.hpp"

#include <QApplication>
#include <QCursor>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QEvent>
#include <QHeaderView>
#include <QIcon>
#include <QLabel>
#include <QListWidget>
#include <QMouseEvent>
#include <QMenu>
#include <QMimeData>
#include <QPainterPath>
#include <QContextMenuEvent>
#include <QDrag>
#include <QFontMetrics>
#include <QPainter>
#include <QPixmap>
#include <QImage>
#include <QStackedLayout>
#include <QStyledItemDelegate>
#include <QStringList>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVariant>
#include <QWindow>
#include <QVBoxLayout>
#include <cmath>
#include <vector>

namespace {

QRect closeButtonRect(const QRect &rowRect, int viewportWidth) {
    const int side = 16;
    const int right = viewportWidth > 0 ? viewportWidth - 6 : rowRect.right() - 6;
    return QRect(right - side,
                 rowRect.top() + (rowRect.height() - side) / 2,
                 side,
                 side);
}

int itemDepth(const QTreeWidget *tree, const QModelIndex &index) {
    if (!tree) return 0;
    int depth = 0;
    QTreeWidgetItem *item = tree->itemFromIndex(index);
    while (item && item->parent()) {
        ++depth;
        item = item->parent();
    }
    return depth;
}

bool isNewTabUrl(const QUrl &url) {
    return url.isEmpty() || url.toString() == QStringLiteral("about:blank");
}

enum TabRoles {
    PinStateRole = Qt::UserRole + 1,
    UnreadRole = Qt::UserRole + 2,
    OriginalUrlRole = Qt::UserRole + 3,
    EssentialBorderRole = Qt::UserRole + 4,
};

enum TabPinState {
    NormalTab = 0,
    PinnedTab = 1,
    EssentialTab = 2,
};

class TabItemDelegate final : public QStyledItemDelegate {
public:
    TabItemDelegate(const Theme &theme, QObject *parent)
        : QStyledItemDelegate(parent), m_theme(theme), m_closeIcon(mac::sfSymbolIcon("xmark", 10.5, theme.foreground)) {}

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override {
        const auto *tree = option.widget ? qobject_cast<const QTreeWidget *>(option.widget) : nullptr;
        const bool selected = tree && tree->currentIndex() == index;
        const bool hovered = option.state.testFlag(QStyle::State_MouseOver);
        const int depth = itemDepth(tree, index);
        const int viewportWidth = option.widget ? option.widget->width() : 0;
        const QRect closeRect = closeButtonRect(option.rect, viewportWidth);

        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        if (selected || hovered) {
            QColor fill = m_theme.background.lightness() < 128 ? QColor(255, 255, 255) : QColor(0, 0, 0);
            fill.setAlpha(selected ? 22 : 12);
            QRect rowRect = option.rect;
            rowRect.setLeft(6 + depth * 18);
            if (viewportWidth > 0) rowRect.setRight(viewportWidth - 6);
            painter->setPen(Qt::NoPen);
            painter->setBrush(fill);
            painter->drawRoundedRect(rowRect.adjusted(0, 3, 0, -3), 6, 6);
        }

        const int pinState = index.data(PinStateRole).toInt();
        const bool unread = index.data(UnreadRole).toBool();
        if (pinState != NormalTab || unread) {
            QColor dot = pinState == EssentialTab ? m_theme.accent : m_theme.foreground;
            dot.setAlpha(unread ? 230 : 150);
            painter->setPen(Qt::NoPen);
            painter->setBrush(dot);
            const int dotSize = pinState == EssentialTab ? 6 : 5;
            painter->drawEllipse(QRect(option.rect.left() + 7 + depth * 18,
                                       option.rect.top() + (option.rect.height() - dotSize) / 2,
                                       dotSize,
                                       dotSize));
        }

        const QVariant decoration = index.data(Qt::DecorationRole);
        QRect textRect = option.rect.adjusted(16 + depth * 18, 0, -28, 0);
        if (decoration.canConvert<QIcon>()) {
            const QIcon icon = qvariant_cast<QIcon>(decoration);
            const QRect iconRect(textRect.left(), option.rect.top() + (option.rect.height() - 14) / 2, 14, 14);
            icon.paint(painter, iconRect, Qt::AlignCenter);
            textRect.setLeft(iconRect.right() + 7);
        }

        QColor textColor = m_theme.foreground;
        if (unread) textColor = textColor.lighter(m_theme.background.lightness() < 128 ? 135 : 85);
        painter->setPen(textColor);
        QFont textFont = option.font;
        if (unread) textFont.setWeight(QFont::DemiBold);
        painter->setFont(textFont);
        const QString text = option.fontMetrics.elidedText(index.data(Qt::DisplayRole).toString(), Qt::ElideRight, textRect.width());
        painter->drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, text);
        painter->restore();

        if (!selected && !hovered) return;
        if (!m_closeIcon.isNull()) {
            m_closeIcon.paint(painter, closeRect.adjusted(4, 4, -4, -4), Qt::AlignCenter);
        } else {
            painter->save();
            painter->setRenderHint(QPainter::Antialiasing, true);
            painter->setPen(QPen(option.palette.color(QPalette::Text), 1.6));
            const QRectF r = closeRect.adjusted(5, 5, -5, -5);
            painter->drawLine(r.topLeft(), r.bottomRight());
            painter->drawLine(r.topRight(), r.bottomLeft());
            painter->restore();
        }
    }

private:
    Theme m_theme;
    QIcon m_closeIcon;
};

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
    if (!best.isValid()) return QColor();
    if (best.lightnessF() < 0.38) best = best.lighter(145);
    if (best.lightnessF() > 0.72) best = best.darker(130);
    return best;
}

class EssentialItemDelegate final : public QStyledItemDelegate {
public:
    EssentialItemDelegate(const Theme &theme, QObject *parent)
        : QStyledItemDelegate(parent), m_theme(theme) {}

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        QRect tile = option.rect.adjusted(2, 2, -2, -2);
        QColor fill = m_theme.foreground;
        fill.setAlpha(option.state.testFlag(QStyle::State_MouseOver) ? 30 : 18);
        QColor border = index.data(EssentialBorderRole).value<QColor>();
        if (!border.isValid()) border = m_theme.border;
        border.setAlpha(option.state.testFlag(QStyle::State_MouseOver) ? 190 : 125);
        painter->setBrush(fill);
        painter->setPen(QPen(border, 1));
        painter->drawRoundedRect(tile, 7, 7);

        const QIcon icon = qvariant_cast<QIcon>(index.data(Qt::DecorationRole));
        const QRect iconRect(tile.center().x() - 9, tile.center().y() - 9, 18, 18);
        icon.paint(painter, iconRect, Qt::AlignCenter);
        painter->restore();
    }

private:
    Theme m_theme;
};

class TabTreeWidget final : public QTreeWidget {
public:
    using QTreeWidget::QTreeWidget;

protected:
    void drawBranches(QPainter *, const QRect &, const QModelIndex &) const override {}
};

}  // namespace

TabTree::TabTree(ProfileStore &profiles, FaviconService *favicons, QWidget *stack,
                 const Theme &theme, QWidget *sidebarParent, QObject *parent)
    : QObject(parent), m_profiles(&profiles), m_favicons(favicons),
      m_stack(stack), m_theme(theme) {
    m_container = new QWidget(sidebarParent);
    m_container->setAttribute(Qt::WA_TranslucentBackground);
    m_container->setStyleSheet("QWidget { background: transparent; }");
    auto *layout = new QVBoxLayout(m_container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);

    m_essentials = new QListWidget(m_container);
    m_essentials->setObjectName("EssentialTabsGrid");
    m_essentials->setViewMode(QListView::IconMode);
    m_essentials->setFlow(QListView::LeftToRight);
    m_essentials->setWrapping(true);
    m_essentials->setResizeMode(QListView::Adjust);
    m_essentials->setMovement(QListView::Static);
    m_essentials->setSpacing(6);
    m_essentials->setGridSize(QSize(44, 44));
    m_essentials->setIconSize(QSize(18, 18));
    m_essentials->setFixedHeight(0);
    m_essentials->setFrameShape(QFrame::NoFrame);
    m_essentials->setFocusPolicy(Qt::NoFocus);
    m_essentials->setSelectionMode(QAbstractItemView::NoSelection);
    m_essentials->setAcceptDrops(true);
    m_essentials->setContextMenuPolicy(Qt::DefaultContextMenu);
    m_essentials->setAttribute(Qt::WA_TranslucentBackground);
    m_essentialsViewport = m_essentials->viewport();
    m_essentialsViewport->setAttribute(Qt::WA_TranslucentBackground);
    m_essentialsViewport->setAutoFillBackground(false);
    m_essentialsViewport->installEventFilter(this);
    m_essentials->setItemDelegate(new EssentialItemDelegate(m_theme, m_essentials));
    m_essentials->setStyleSheet("QListWidget#EssentialTabsGrid { background: transparent; border: none; outline: 0; }");
    layout->addWidget(m_essentials, 0);

    m_tabs = new TabTreeWidget(m_container);
    m_tabs->setObjectName("TabTree");
    m_tabs->setHeaderHidden(true);
    m_tabs->header()->setStretchLastSection(true);
    m_tabs->setIndentation(14);
    m_tabs->setRootIsDecorated(false);
    m_tabs->setAllColumnsShowFocus(false);
    m_tabs->setSelectionMode(QAbstractItemView::NoSelection);
    m_tabs->setFocusPolicy(Qt::NoFocus);
    m_tabs->setAnimated(true);
    m_tabs->setFrameShape(QFrame::NoFrame);
    m_tabs->setIconSize(QSize(14, 14));
    m_tabs->setExpandsOnDoubleClick(false);
    m_tabs->setContextMenuPolicy(Qt::DefaultContextMenu);
    m_tabs->setUniformRowHeights(true);
    m_tabs->setMouseTracking(true);
    m_tabs->setAcceptDrops(true);
    m_tabsViewport = m_tabs->viewport();
    m_tabsViewport->setMouseTracking(true);
    m_tabsViewport->installEventFilter(this);
    m_tabs->setItemDelegate(new TabItemDelegate(m_theme, m_tabs));
    m_tabs->setAttribute(Qt::WA_MacShowFocusRect, false);
    m_tabs->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_tabs->setAttribute(Qt::WA_TranslucentBackground);
    m_tabs->setAttribute(Qt::WA_NoSystemBackground);
    m_tabs->viewport()->setAttribute(Qt::WA_TranslucentBackground);
    m_tabs->viewport()->setAttribute(Qt::WA_NoSystemBackground);
    m_tabs->viewport()->setAutoFillBackground(false);
    layout->addWidget(m_tabs, 1);

    m_tabs->setStyleSheet(QString(
        "QTreeWidget#TabTree { background: transparent; border: none; color: %1; outline: 0; }"
        "QTreeWidget#TabTree::item { padding: 2px 26px 2px 6px; border: none; background: transparent; color: %1; selection-background-color: transparent; }"
        "QTreeWidget#TabTree::item:selected { background: transparent; color: %1; selection-background-color: transparent; }"
        "QTreeWidget#TabTree::item:selected:active { background: transparent; color: %1; selection-background-color: transparent; }"
        "QTreeWidget#TabTree::item:selected:!active { background: transparent; color: %1; selection-background-color: transparent; }"
        "QTreeWidget#TabTree::item:hover:!selected { background: transparent; }"
        "QTreeWidget#TabTree::branch { background: transparent; border: none; image: none; }"
        "QTreeWidget#TabTree::branch:selected { background: transparent; border: none; image: none; }"
        "QTreeWidget#TabTree::branch:hover { background: transparent; border: none; image: none; }")
        .arg(m_theme.foreground.name()));

    connect(m_tabs, &QTreeWidget::currentItemChanged, this, [this] {
        markItemUnread(currentItem(), false);
        emit currentTabChanged();
        // Replay the active tab's last known theme colour immediately for a
        // snappy chrome update, then kick off a fresh sniff in case the
        // page has changed since (scrolled, dynamically restyled, etc.).
        if (auto *v = currentView()) {
            if (isNewTabUrl(v->url())) {
                emit loadProgress(0);
                emit themeColorChanged(QColor());
                return;
            }
            const QColor cached = v->cachedThemeColor();
            if (cached.isValid()) emit themeColorChanged(cached);
            v->sniffTopColor();
        } else {
            emit themeColorChanged(QColor());
        }
    });

    if (m_favicons) {
        connect(m_favicons, &FaviconService::faviconReady, this,
                [this](const QString &domain, const QPixmap &pm) {
                    if (pm.isNull()) return;
                    const QIcon icon(pm);
                    for (auto it = m_views.constBegin(); it != m_views.constEnd(); ++it) {
                        QString itemDomain = it.value()->url().host();
                        if (itemDomain.startsWith("www.")) itemDomain.remove(0, 4);
                        if (itemDomain == domain) it.key()->setIcon(0, icon);
                    }
                });
    }
}

WebView *TabTree::currentView() const {
    return m_views.value(currentItem(), nullptr);
}

QTreeWidgetItem *TabTree::currentItem() const {
    return m_currentEssentialItem ? m_currentEssentialItem : m_tabs->currentItem();
}

QList<WebView *> TabTree::views() const {
    return m_views.values();
}

void TabTree::selectView(WebView *view) {
    for (auto it = m_views.constBegin(); it != m_views.constEnd(); ++it) {
        if (it.value() == view) {
            selectItem(it.key());
            return;
        }
    }
}

void TabTree::selectItem(QTreeWidgetItem *item) {
    if (!item) return;
    markItemUnread(item, false);
    if (item->data(0, PinStateRole).toInt() == EssentialTab) {
        m_currentEssentialItem = item;
        m_tabs->clearSelection();
        m_tabs->setCurrentItem(nullptr);
    } else {
        m_currentEssentialItem = nullptr;
        m_tabs->setCurrentItem(item);
        m_tabs->clearSelection();
    }
    m_tabs->viewport()->update();
    if (m_essentials) m_essentials->viewport()->update();
    emit currentTabChanged();
}

bool TabTree::eventFilter(QObject *watched, QEvent *event) {
    if (watched->property("pocbDetachOverlay").toBool()) {
        if (event->type() == QEvent::DragEnter || event->type() == QEvent::DragMove) {
            auto *drag = static_cast<QDragMoveEvent *>(event);
            if (drag->mimeData()->hasFormat("application/x-pocb-tabptr")) {
                drag->setDropAction(Qt::MoveAction);
                drag->accept();
                return true;
            }
        }
        if (event->type() == QEvent::Drop) {
            auto *drop = static_cast<QDropEvent *>(event);
            auto *item = reinterpret_cast<QTreeWidgetItem *>(drop->mimeData()->data("application/x-pocb-tabptr").toULongLong());
            if (item && m_views.contains(item)) emit tabDetachRequested(m_views.value(item), m_views.value(item)->url(), drop->position().toPoint() + qobject_cast<QWidget *>(watched)->pos());
            drop->setDropAction(Qt::MoveAction);
            drop->accept();
            return true;
        }
    }
    if (!m_essentialsViewport || !m_tabsViewport) return QObject::eventFilter(watched, event);
    if (watched == m_essentialsViewport && event->type() == QEvent::ContextMenu) {
        auto *context = static_cast<QContextMenuEvent *>(event);
        if (auto *gridItem = m_essentials->itemAt(context->pos())) {
            if (auto *item = treeItemForEssential(gridItem)) {
                selectItem(item);
                showContextMenu(item, context->globalPos());
                return true;
            }
        }
    }
    if (watched == m_essentialsViewport && event->type() == QEvent::MouseButtonPress) {
        auto *mouse = static_cast<QMouseEvent *>(event);
        if (mouse->button() == Qt::LeftButton) {
            if (auto *gridItem = m_essentials->itemAt(mouse->pos())) {
                if (auto *item = treeItemForEssential(gridItem)) {
                    m_pressedItem = item;
                    m_pressPos = mouse->pos();
                    selectItem(item);
                    return true;
                }
            }
        }
    }
    if (watched == m_essentialsViewport && event->type() == QEvent::MouseMove) {
        auto *mouse = static_cast<QMouseEvent *>(event);
        if (m_draggingItem && m_dragOverlay) {
            const QPoint global = mouse->globalPosition().toPoint();
            const bool outsideWindow = !m_container->window()->frameGeometry().contains(global);
            const QPixmap preview = outsideWindow ? miniWindowDragPixmapForItem(m_draggingItem) : dragPixmapForItem(m_draggingItem, true);
            qobject_cast<QLabel *>(m_dragOverlay)->setPixmap(preview);
            m_dragOverlay->resize(preview.size() / preview.devicePixelRatio());
            m_dragOverlay->move(global - (outsideWindow ? QPoint(34, 46) : QPoint(22, 22)));
            if (outsideWindow) clearDropIndicator();
            else {
                const int index = essentialDropIndex(mouse->pos());
                showDropIndicator(QRect(m_essentials->mapTo(m_container, QPoint((index % 4) * 50 + 2, (index / 4) * 50 + 2)), QSize(44, 44)));
            }
            return true;
        }
        if (m_pressedItem && (mouse->pos() - m_pressPos).manhattanLength() >= QApplication::startDragDistance()) {
            m_draggingItem = m_pressedItem;
            m_draggingFromEssential = true;
            const QPixmap preview = dragPixmapForItem(m_draggingItem, true);
            auto *label = new QLabel(nullptr, Qt::Tool | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
            label->setAttribute(Qt::WA_TranslucentBackground);
            label->setAttribute(Qt::WA_ShowWithoutActivating);
            label->setPixmap(preview);
            label->resize(preview.size() / preview.devicePixelRatio());
            m_dragOverlay = label;
            if (m_draggingItem) {
                m_draggingItem->setHidden(true);
                syncEssentialGrid();
            }
            m_essentialsViewport->grabMouse();
            m_pressedItem = nullptr;
            m_dragOverlay->move(mouse->globalPosition().toPoint() - QPoint(22, 22));
            m_dragOverlay->show();
            return true;
        }
    }
    if (watched == m_essentialsViewport && (event->type() == QEvent::DragEnter || event->type() == QEvent::DragMove)) {
        auto *drag = static_cast<QDragMoveEvent *>(event);
        if (drag->mimeData()->hasFormat("application/x-pocb-tabptr")) {
            const int index = essentialDropIndex(drag->position().toPoint());
            const int row = index / 4;
            const int col = index % 4;
            showDropIndicator(QRect(m_essentials->mapTo(m_container, QPoint(col * 50 + 2, row * 50 + 2)), QSize(44, 44)));
            drag->acceptProposedAction();
            return true;
        }
    }
    if (watched == m_essentialsViewport && event->type() == QEvent::Drop) {
        auto *drop = static_cast<QDropEvent *>(event);
        auto *item = reinterpret_cast<QTreeWidgetItem *>(drop->mimeData()->data("application/x-pocb-tabptr").toULongLong());
        if (item) setItemEssentialAt(item, essentialDropIndex(drop->position().toPoint()));
        clearDropIndicator();
        drop->acceptProposedAction();
        return true;
    }
    if (watched == m_tabsViewport && event->type() == QEvent::Resize) {
        if (m_tabs) m_tabs->setColumnWidth(0, m_tabsViewport->width());
    }
    if (watched == m_tabsViewport && event->type() == QEvent::MouseMove) {
        auto *mouse = static_cast<QMouseEvent *>(event);
        if (m_draggingItem && m_dragOverlay) {
            const QPoint global = mouse->globalPosition().toPoint();
            const bool outsideWindow = !m_container->window()->frameGeometry().contains(global);
            const QPixmap preview = outsideWindow ? miniWindowDragPixmapForItem(m_draggingItem) : dragPixmapForItem(m_draggingItem, false);
            qobject_cast<QLabel *>(m_dragOverlay)->setPixmap(preview);
            m_dragOverlay->resize(preview.size() / preview.devicePixelRatio());
            m_dragOverlay->move(global - (outsideWindow ? QPoint(34, 46) : QPoint(qMin(80, preview.width() / 2), preview.height() / 2)));
            if (outsideWindow) clearDropIndicator();
            else if (mouse->pos().y() < 36) showDropIndicator(QRect(m_tabs->mapTo(m_container, QPoint(8, 4)), QSize(44, 44)));
            else if (auto *target = m_tabs->itemAt(mouse->pos())) {
                const QRect row = m_tabs->visualItemRect(target);
                const bool before = mouse->pos().y() < row.center().y();
                showDropIndicator(QRect(m_tabs->mapTo(m_container, QPoint(row.left() + 8, before ? row.top() : row.bottom())), QSize(qMax(32, row.width() - 16), 2)));
            }
            return true;
        }
        const auto *item = m_tabs->itemAt(mouse->pos());
        const bool overClose = item && closeButtonRect(m_tabs->visualItemRect(const_cast<QTreeWidgetItem *>(item)), m_tabs->viewport()->width()).contains(mouse->pos());
        m_tabs->viewport()->setCursor(overClose ? Qt::PointingHandCursor : Qt::ArrowCursor);
        m_tabs->viewport()->update();
    }
    if (watched == m_tabsViewport && event->type() == QEvent::Leave) {
        m_tabs->viewport()->unsetCursor();
        m_tabs->viewport()->update();
    }
    if (watched == m_tabsViewport && event->type() == QEvent::ContextMenu) {
        auto *context = static_cast<QContextMenuEvent *>(event);
        if (auto *item = m_tabs->itemAt(context->pos())) {
            selectItem(item);
            showContextMenu(item, context->globalPos());
            return true;
        }
    }
    if (watched == m_tabsViewport && event->type() == QEvent::MouseButtonPress) {
        auto *mouse = static_cast<QMouseEvent *>(event);
        if (mouse->button() == Qt::LeftButton) {
            if (auto *item = m_tabs->itemAt(mouse->pos())) {
                if (closeButtonRect(m_tabs->visualItemRect(item), m_tabs->viewport()->width()).contains(mouse->pos())) {
                    m_tabs->viewport()->unsetCursor();
                    closeItem(item);
                    return true;
                }
                m_pressedItem = item;
                m_pressPos = mouse->pos();
            }
        }
    }
    if (watched == m_tabsViewport && event->type() == QEvent::MouseMove) {
        auto *mouse = static_cast<QMouseEvent *>(event);
        if (m_pressedItem && (mouse->pos() - m_pressPos).manhattanLength() >= QApplication::startDragDistance()) {
            m_draggingItem = m_pressedItem;
            m_draggingFromEssential = false;
            const QPixmap preview = dragPixmapForItem(m_draggingItem, false);
            auto *label = new QLabel(nullptr, Qt::Tool | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
            label->setAttribute(Qt::WA_TranslucentBackground);
            label->setAttribute(Qt::WA_ShowWithoutActivating);
            label->setPixmap(preview);
            label->resize(preview.size() / preview.devicePixelRatio());
            m_dragOverlay = label;
            if (m_draggingItem) m_draggingItem->setHidden(true);
            m_tabsViewport->grabMouse();
            m_pressedItem = nullptr;
            m_dragOverlay->move(mouse->globalPosition().toPoint() - QPoint(qMin(80, preview.width() / 2), preview.height() / 2));
            m_dragOverlay->show();
            return true;
        }
    }
    if (watched == m_tabsViewport && (event->type() == QEvent::DragEnter || event->type() == QEvent::DragMove)) {
        auto *drag = static_cast<QDragMoveEvent *>(event);
        if (drag->mimeData()->hasFormat("application/x-pocb-tabptr")) {
            if (drag->position().y() < 36) {
                showDropIndicator(QRect(m_tabs->mapTo(m_container, QPoint(8, 4)), QSize(44, 44)));
            } else if (auto *target = m_tabs->itemAt(drag->position().toPoint())) {
                const QRect row = m_tabs->visualItemRect(target);
                const bool before = drag->position().y() < row.center().y();
                showDropIndicator(QRect(m_tabs->mapTo(m_container, QPoint(row.left() + 8, before ? row.top() : row.bottom())), QSize(qMax(32, row.width() - 16), 2)));
            }
            drag->acceptProposedAction();
            return true;
        }
    }
    if ((watched == m_tabsViewport || watched == m_essentialsViewport) && event->type() == QEvent::MouseButtonRelease && m_draggingItem) {
        auto *mouse = static_cast<QMouseEvent *>(event);
        QWidget *grabbed = qobject_cast<QWidget *>(watched);
        if (grabbed) grabbed->releaseMouse();
        const QPoint global = mouse->globalPosition().toPoint();
        const bool outsideWindow = !m_container->window()->frameGeometry().contains(global);
        if (outsideWindow) {
            if (m_views.contains(m_draggingItem)) emit tabDetachRequested(m_views.value(m_draggingItem), m_views.value(m_draggingItem)->url(), global);
        } else if (m_essentials->viewport()->rect().contains(m_essentials->viewport()->mapFromGlobal(global))) {
            setItemEssentialAt(m_draggingItem, essentialDropIndex(m_essentials->viewport()->mapFromGlobal(global)));
        } else {
            finishTabDrop(m_draggingItem, global, watched, m_tabs->viewport()->mapFromGlobal(global));
            if (m_draggingItem) m_draggingItem->setHidden(m_draggingItem->data(0, PinStateRole).toInt() == EssentialTab);
        }
        if (m_dragOverlay) {
            m_dragOverlay->deleteLater();
            m_dragOverlay = nullptr;
        }
        m_draggingItem = nullptr;
        clearDropIndicator();
        syncEssentialGrid();
        return true;
    }
    if (watched == m_tabsViewport && event->type() == QEvent::Drop) {
        auto *drop = static_cast<QDropEvent *>(event);
        auto *item = reinterpret_cast<QTreeWidgetItem *>(drop->mimeData()->data("application/x-pocb-tabptr").toULongLong());
        if (item) finishTabDrop(item, m_tabs->viewport()->mapToGlobal(drop->position().toPoint()), watched, drop->position().toPoint());
        clearDropIndicator();
        drop->acceptProposedAction();
        return true;
    }
    return QObject::eventFilter(watched, event);
}

WebView *TabTree::newTabForExtension(const QUrl &url, bool background, QTreeWidgetItem *parentItem) {
    auto *view = new WebView(m_profiles->currentProfile(), m_stack);

    auto *item = new QTreeWidgetItem(QStringList() << "New tab");
    item->setData(0, Qt::UserRole, QVariant::fromValue<quintptr>(reinterpret_cast<quintptr>(view)));
    item->setData(0, PinStateRole, NormalTab);
    item->setData(0, UnreadRole, false);
    item->setData(0, OriginalUrlRole, QString());
    if (!parentItem) parentItem = currentItem();
    if (parentItem) parentItem->addChild(item);
    else m_tabs->addTopLevelItem(item);
    item->setExpanded(true);
    m_views.insert(item, view);

    static_cast<QStackedLayout *>(m_stack->layout())->addWidget(view);
    wireView(view, item);

    view->load(url.isEmpty() ? QUrl(m_homePage) : url);
    if (!background) selectItem(item);
    return view;
}

void TabTree::newTab(const QUrl &url, bool background, QTreeWidgetItem *parentItem) {
    newTabForExtension(url, background, parentItem);
}

void TabTree::adoptChildView(WebView *child, QTreeWidgetItem *parentItem, bool background) {
    if (!child) return;
    child->setParent(m_stack);
    auto *item = new QTreeWidgetItem(QStringList() << "New tab");
    item->setData(0, Qt::UserRole, QVariant::fromValue<quintptr>(reinterpret_cast<quintptr>(child)));
    item->setData(0, PinStateRole, NormalTab);
    item->setData(0, UnreadRole, false);
    item->setData(0, OriginalUrlRole, QString());
    if (parentItem) parentItem->addChild(item);
    else m_tabs->addTopLevelItem(item);
    item->setExpanded(true);
    m_views.insert(item, child);
    static_cast<QStackedLayout *>(m_stack->layout())->addWidget(child);
    wireView(child, item);
    if (!background) selectItem(item);
}

void TabTree::closeCurrent() {
    closeItem(currentItem());
}

void TabTree::closeItem(QTreeWidgetItem *item) {
    if (!item) return;
    deleteItemRecursive(item);
    if (m_tabs->topLevelItemCount() == 0) newTab(QUrl(m_homePage));
    emit currentTabChanged();
}

void TabTree::closeChildren(QTreeWidgetItem *item) {
    if (!item) return;
    while (item->childCount() > 0) deleteItemRecursive(item->child(0));
    item->setExpanded(false);
    emit currentTabChanged();
}

void TabTree::duplicateItem(QTreeWidgetItem *item) {
    if (!item) return;
    auto *view = m_views.value(item, nullptr);
    newTab(view ? view->url() : QUrl(m_homePage), false, item->parent());
}

void TabTree::showContextMenu(QTreeWidgetItem *item, const QPoint &globalPos) {
    if (!item) return;
    selectItem(item);
    const int pinState = item->data(0, PinStateRole).toInt();
    const bool isEssential = pinState == EssentialTab;
    const bool isPinned = pinState == PinnedTab;
    const int essentials = essentialCount();
    QStringList titles;
    QVector<bool> enabled;
    std::vector<std::function<void()>> callbacks;
    auto add = [&](const QString &title, bool isEnabled, std::function<void()> callback) {
        titles.append(title);
        enabled.append(isEnabled);
        callbacks.push_back(std::move(callback));
    };
    auto separator = [&] {
        titles.append(QStringLiteral("-"));
        enabled.append(false);
    };

    add("New Child Tab", true, [this, item] { newTab(QUrl(m_homePage), false, item); });
    add("Duplicate Tab", true, [this, item] { duplicateItem(item); });
    add("Reload Tab", true, [this, item] {
        if (auto *view = m_views.value(item, nullptr)) view->reload();
    });
    separator();
    if (!isEssential) add(isPinned ? "Unpin Tab" : "Pin Tab", true, [this, item, isPinned] { setItemPinState(item, isPinned ? NormalTab : PinnedTab); });
    add(isEssential ? "Remove from Essentials" : QString("Add to Essentials (%1/12)").arg(essentials), isEssential || essentials < 12, [this, item, isEssential] { setItemPinState(item, isEssential ? NormalTab : EssentialTab); });
    if (isPinned || isEssential) {
        add("Reset to Pinned URL", !item->data(0, OriginalUrlRole).toString().isEmpty(), [this, item] { resetPinnedItem(item); });
        add("Replace Pinned URL", true, [this, item] { replacePinnedUrl(item); });
    }
    separator();
    add("Close Child Tabs", item->childCount() > 0, [this, item] { closeChildren(item); });
    if (!isEssential) add("Close Tab", true, [this, item] { closeItem(item); });

    mac::showNativeContextMenu(m_tabs->viewport(), globalPos, titles, enabled, std::move(callbacks));
}

void TabTree::setItemPinState(QTreeWidgetItem *item, int state) {
    if (!item) return;
    item->setData(0, PinStateRole, state);
    item->setHidden(state == EssentialTab);
    if (state == NormalTab) {
        item->setData(0, OriginalUrlRole, QString());
    } else if (auto *view = m_views.value(item, nullptr); item->data(0, OriginalUrlRole).toString().isEmpty()) {
        item->setData(0, OriginalUrlRole, view->url().toString());
    }
    if (state != NormalTab) insertTopLevelForState(item, state);
    if (state == NormalTab && m_currentEssentialItem == item) m_currentEssentialItem = nullptr;
    syncEssentialGrid();
    m_tabs->viewport()->update();
    mac::performHapticFeedback();
}

void TabTree::markItemUnread(QTreeWidgetItem *item, bool unread) {
    if (!item) return;
    item->setData(0, UnreadRole, unread);
    m_tabs->viewport()->update();
    if (item->data(0, PinStateRole).toInt() == EssentialTab) syncEssentialGrid();
}

void TabTree::deleteItemRecursive(QTreeWidgetItem *item) {
    if (!item) return;
    while (item->childCount() > 0) deleteItemRecursive(item->child(0));
    auto *view = m_views.take(item);
    if (view) view->deleteLater();
    if (m_currentEssentialItem == item) m_currentEssentialItem = nullptr;
    delete item;
    syncEssentialGrid();
    if (m_views.isEmpty()) newTab(QUrl(m_homePage));
}

int TabTree::essentialCount() const {
    int count = 0;
    for (int i = 0; i < m_tabs->topLevelItemCount(); ++i) {
        if (m_tabs->topLevelItem(i)->data(0, PinStateRole).toInt() == EssentialTab) ++count;
    }
    return count;
}

void TabTree::resetPinnedItem(QTreeWidgetItem *item) {
    if (!item) return;
    const QUrl url(item->data(0, OriginalUrlRole).toString());
    if (!url.isValid()) return;
    if (auto *view = m_views.value(item, nullptr)) view->load(url);
}

void TabTree::replacePinnedUrl(QTreeWidgetItem *item) {
    if (!item) return;
    if (auto *view = m_views.value(item, nullptr)) item->setData(0, OriginalUrlRole, view->url().toString());
}

void TabTree::insertTopLevelForState(QTreeWidgetItem *item, int state) {
    if (!item) return;
    if (auto *parent = item->parent()) parent->removeChild(item);
    else {
        const int existing = m_tabs->indexOfTopLevelItem(item);
        if (existing >= 0) m_tabs->takeTopLevelItem(existing);
    }
    int index = 0;
    if (state == PinnedTab) {
        while (index < m_tabs->topLevelItemCount() &&
               m_tabs->topLevelItem(index)->data(0, PinStateRole).toInt() == EssentialTab) ++index;
        while (index < m_tabs->topLevelItemCount() &&
               m_tabs->topLevelItem(index)->data(0, PinStateRole).toInt() == PinnedTab) ++index;
    }
    m_tabs->insertTopLevelItem(index, item);
}

void TabTree::syncEssentialGrid() {
    if (!m_essentials) return;
    m_essentialItems.clear();
    m_essentials->clear();
    for (int i = 0; i < m_tabs->topLevelItemCount(); ++i) {
        auto *item = m_tabs->topLevelItem(i);
        if (!item || item->data(0, PinStateRole).toInt() != EssentialTab) continue;
        auto *gridItem = new QListWidgetItem(item->icon(0), QString(), m_essentials);
        gridItem->setToolTip(item->text(0));
        gridItem->setSizeHint(QSize(44, 44));
        gridItem->setData(EssentialBorderRole, vibrantColorFromIcon(item->icon(0)));
        m_essentialItems.insert(gridItem, item);
    }
    const int count = m_essentials->count();
    const int rows = count == 0 ? 0 : ((count - 1) / 4) + 1;
    m_essentials->setFixedHeight(rows == 0 ? 0 : rows * 44 + qMax(0, rows - 1) * 6);
}

QTreeWidgetItem *TabTree::treeItemForEssential(QListWidgetItem *item) const {
    return m_essentialItems.value(item, nullptr);
}

QTreeWidgetItem *TabTree::itemForView(WebView *view) const {
    for (auto it = m_views.constBegin(); it != m_views.constEnd(); ++it) {
        if (it.value() == view) return it.key();
    }
    return nullptr;
}

int TabTree::essentialDropIndex(const QPoint &pos) const {
    if (!m_essentials) return essentialCount();
    if (auto *gridItem = m_essentials->itemAt(pos)) return qMax(0, m_essentials->row(gridItem));
    const int col = qBound(0, pos.x() / 50, 3);
    const int row = qMax(0, pos.y() / 50);
    return qBound(0, row * 4 + col, essentialCount());
}

void TabTree::setItemEssentialAt(QTreeWidgetItem *item, int index) {
    if (!item) return;
    if (auto *parent = item->parent()) parent->removeChild(item);
    else {
        const int existing = m_tabs->indexOfTopLevelItem(item);
        if (existing >= 0) m_tabs->takeTopLevelItem(existing);
    }
    item->setData(0, PinStateRole, EssentialTab);
    item->setHidden(true);
    if (auto *view = m_views.value(item, nullptr); item->data(0, OriginalUrlRole).toString().isEmpty()) {
        item->setData(0, OriginalUrlRole, view->url().toString());
    }
    int insertAt = 0;
    int seenEssentials = 0;
    while (insertAt < m_tabs->topLevelItemCount()) {
        if (m_tabs->topLevelItem(insertAt)->data(0, PinStateRole).toInt() != EssentialTab) break;
        if (seenEssentials >= index) break;
        ++seenEssentials;
        ++insertAt;
    }
    m_tabs->insertTopLevelItem(insertAt, item);
    syncEssentialGrid();
    m_tabs->viewport()->update();
    if (m_essentials) m_essentials->viewport()->update();
    mac::performHapticFeedback();
}

QPixmap TabTree::dragPixmapForItem(QTreeWidgetItem *item, bool essential) const {
    if (essential) {
        for (auto it = m_essentialItems.constBegin(); it != m_essentialItems.constEnd(); ++it) {
            if (it.value() != item) continue;
            const QRect rect = m_essentials->visualItemRect(it.key()).adjusted(0, 0, 1, 1);
            if (rect.isValid()) return m_essentials->viewport()->grab(rect);
        }
        const QSize size(44, 44);
        QPixmap pixmap(size * qApp->devicePixelRatio());
        pixmap.setDevicePixelRatio(qApp->devicePixelRatio());
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing, true);
        QColor fill = m_theme.foreground;
        fill.setAlpha(34);
        QColor stroke = vibrantColorFromIcon(item ? item->icon(0) : QIcon());
        if (!stroke.isValid()) stroke = m_theme.border;
        stroke.setAlpha(180);
        painter.setPen(QPen(stroke, 1));
        painter.setBrush(fill);
        painter.drawRoundedRect(QRect(QPoint(), size).adjusted(1, 1, -1, -1), 9, 9);
        if (item) item->icon(0).paint(&painter, QRect(13, 13, 18, 18), Qt::AlignCenter);
        return pixmap;
    }

    if (item && !item->isHidden()) {
        const QRect rect = m_tabs->visualItemRect(item).adjusted(0, 0, 1, 1);
        if (rect.isValid()) return m_tabs->viewport()->grab(rect);
    }

    const QSize size(qMin(220, qMax(132, m_tabs->viewport()->width() - 12)), 34);
    QPixmap pixmap(size * qApp->devicePixelRatio());
    pixmap.setDevicePixelRatio(qApp->devicePixelRatio());
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QColor fill = m_theme.foreground;
    fill.setAlpha(38);
    QColor stroke = vibrantColorFromIcon(item ? item->icon(0) : QIcon());
    if (!stroke.isValid()) stroke = m_theme.border;
    stroke.setAlpha(170);
    painter.setPen(QPen(stroke, 1));
    painter.setBrush(fill);
    painter.drawRoundedRect(QRect(QPoint(), size).adjusted(1, 1, -1, -1), 8, 8);
    if (item) {
        item->icon(0).paint(&painter, QRect(12, 10, 14, 14), Qt::AlignCenter);
        painter.setPen(m_theme.foreground);
        painter.setFont(m_tabs->font());
        painter.drawText(QRect(34, 0, size.width() - 44, size.height()), Qt::AlignVCenter | Qt::AlignLeft, item->text(0));
    }
    return pixmap;
}

QPixmap TabTree::miniWindowDragPixmapForItem(QTreeWidgetItem *item) const {
    const QSize size(190, 122);
    QPixmap pixmap(size * qApp->devicePixelRatio());
    pixmap.setDevicePixelRatio(qApp->devicePixelRatio());
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QRect outer = QRect(QPoint(), size).adjusted(1, 1, -2, -2);
    QPainterPath clip;
    clip.addRoundedRect(QRectF(outer), 10, 10);
    QColor shell = m_theme.foreground;
    shell.setAlpha(22);
    painter.setPen(QPen(m_theme.border, 1));
    painter.setBrush(shell);
    painter.drawPath(clip);
    painter.setClipPath(clip);
    const QRect sidebarRect(1, 1, 47, size.height() - 2);
    QColor sidebar = m_theme.foreground;
    sidebar.setAlpha(15);
    painter.fillRect(sidebarRect, sidebar);
    const QRect webCard(49, 6, size.width() - 55, size.height() - 12);
    QPainterPath webPath;
    webPath.addRoundedRect(QRectF(webCard), ui::metrics::WebContainerRadius * 0.55, ui::metrics::WebContainerRadius * 0.55);
    QColor webChrome = QColor(26, 26, 26, 185);
    painter.fillPath(webPath, webChrome);
    QColor separator = m_theme.border;
    separator.setAlpha(90);
    painter.fillRect(QRect(webCard.left(), webCard.top() + 18, webCard.width(), 1), separator);
    if (item) {
        QRect tabRect(8, 32, 28, 16);
        QColor tabFill = m_theme.foreground;
        tabFill.setAlpha(36);
        QColor tabStroke = vibrantColorFromIcon(item->icon(0));
        if (!tabStroke.isValid()) tabStroke = m_theme.border;
        tabStroke.setAlpha(150);
        painter.setClipping(false);
        painter.setPen(QPen(tabStroke, 1));
        painter.setBrush(tabFill);
        painter.drawRoundedRect(tabRect, 4, 4);
        item->icon(0).paint(&painter, QRect(tabRect.left() + 5, tabRect.top() + 4, 9, 9), Qt::AlignCenter);
        painter.setPen(m_theme.foreground);
        QFont miniFont = m_tabs->font();
        miniFont.setPointSizeF(qMax(6.0, miniFont.pointSizeF() - 3.0));
        painter.setFont(miniFont);
        painter.drawText(QRect(tabRect.left() + 17, tabRect.top(), tabRect.width() - 18, tabRect.height()), Qt::AlignVCenter | Qt::AlignLeft, QStringLiteral("·"));
        painter.setClipPath(clip);
        WebView *view = m_views.value(item, nullptr);
        QPixmap shot = view ? view->snapshot(QSize(webCard.width(), webCard.height() - 19)) : QPixmap();
        const QRect contentRect(webCard.left(), webCard.top() + 19, webCard.width(), webCard.height() - 19);
        if (!shot.isNull()) {
            painter.drawPixmap(contentRect, shot, QRect(QPoint(), QSize(qRound(shot.width() / shot.devicePixelRatio()), qRound(shot.height() / shot.devicePixelRatio()))));
        } else {
            QColor page = m_theme.foreground;
            page.setAlpha(245);
            painter.fillRect(contentRect, page);
        }
    }
    painter.setClipping(false);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(255, 95, 87));
    painter.drawEllipse(QRect(10, 9, 6, 6));
    painter.setBrush(QColor(255, 189, 46));
    painter.drawEllipse(QRect(20, 9, 6, 6));
    painter.setBrush(QColor(40, 200, 64));
    painter.drawEllipse(QRect(30, 9, 6, 6));
    painter.setPen(QPen(m_theme.border, 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(outer, 10, 10);
    return pixmap;
}

void TabTree::finishTabDrop(QTreeWidgetItem *draggedItem, const QPoint &, QObject *, const QPoint &localPos) {
    if (!draggedItem || !m_tabs) return;
    if (localPos.y() < 36) {
        setItemEssentialAt(draggedItem, essentialCount());
        mac::performHapticFeedback();
        return;
    }
    if (draggedItem->data(0, PinStateRole).toInt() == EssentialTab) {
        setItemPinState(draggedItem, NormalTab);
        mac::performHapticFeedback();
        return;
    }
    auto *target = m_tabs->itemAt(localPos);
    if (!target || target == draggedItem) return;
    const QRect row = m_tabs->visualItemRect(target);
    auto *draggedView = m_views.value(draggedItem, nullptr);
    auto *targetView = m_views.value(target, nullptr);
    if (draggedView && targetView && localPos.x() > row.left() + row.width() * 0.62) {
        emit tabSplitRequested(draggedView, targetView, false);
        return;
    }
    if (draggedView && targetView && localPos.x() < row.left() + row.width() * 0.38) {
        emit tabSplitRequested(draggedView, targetView, true);
        return;
    }
    const int state = draggedItem->data(0, PinStateRole).toInt();
    if (auto *parent = draggedItem->parent()) parent->removeChild(draggedItem);
    else {
        const int idx = m_tabs->indexOfTopLevelItem(draggedItem);
        if (idx >= 0) m_tabs->takeTopLevelItem(idx);
    }
    const int targetIndex = m_tabs->indexOfTopLevelItem(target);
    if (targetIndex >= 0) {
        const bool before = localPos.y() < row.center().y();
        m_tabs->insertTopLevelItem(targetIndex + (before ? 0 : 1), draggedItem);
    } else if (target->parent()) {
        target->parent()->addChild(draggedItem);
    } else {
        m_tabs->addTopLevelItem(draggedItem);
    }
    draggedItem->setData(0, PinStateRole, state);
    syncEssentialGrid();
    m_tabs->viewport()->update();
}

void TabTree::showDropIndicator(const QRect &rect) {
    if (!m_dropIndicator) {
        m_dropIndicator = new QWidget(m_container);
        m_dropIndicator->setAttribute(Qt::WA_TransparentForMouseEvents);
        m_dropIndicator->setStyleSheet("background: transparent;");
    }
    m_dropIndicator->setStyleSheet(rect.height() > 10
        ? QString("background: transparent; border: 1px solid %1; border-radius: 9px;").arg(m_theme.accent.name())
        : QString("background: %1; border: none; border-radius: 1px;").arg(m_theme.accent.name()));
    const bool moved = m_dropIndicator->geometry() != rect;
    m_dropIndicator->setGeometry(rect);
    if (moved) mac::performHapticFeedback();
    m_dropIndicator->show();
    m_dropIndicator->raise();
}

void TabTree::clearDropIndicator() {
    if (m_dropIndicator) m_dropIndicator->hide();
}

void TabTree::rebuildForProfile() {
    const QUrl activeUrl = currentView() ? currentView()->url() : QUrl(m_homePage);
    m_views.clear();
    m_tabs->clear();
    const auto children = m_stack->findChildren<WebView *>();
    for (auto *child : children) child->deleteLater();
    newTab(activeUrl);
}

void TabTree::wireView(WebView *view, QTreeWidgetItem *item) {
    connect(view, &WebView::titleChanged, this, [this, view, item](const QString &title) {
        item->setText(0, title.isEmpty() ? "New tab" : title);
        if (view != currentView()) markItemUnread(item, true);
        emit currentTabChanged();
    });
    connect(view, &WebView::urlChanged, this, [this, view, item](const QUrl &url) {
        if (view == currentView()) emit currentTabChanged();
        else markItemUnread(item, true);
        if (m_favicons) {
            if (auto cached = m_favicons->cached(url); !cached.isNull()) {
                item->setIcon(0, QIcon(cached));
            } else {
                m_favicons->request(url);
            }
        }
    });
    connect(view, &WebView::loadProgress, this, [this, view](int progress) {
        if (view != currentView()) return;
        if (isNewTabUrl(view->url())) {
            emit loadProgress(0);
            return;
        }
        emit loadProgress(progress);
    });
    connect(view, &WebView::themeColorChanged, this, [this, view](const QColor &c) {
        if (view != currentView()) return;
        emit themeColorChanged(c);
    });
    connect(view, &WebView::loadFinished, this, [this, view, item](bool) {
        if (view != currentView()) markItemUnread(item, true);
        emit currentTabChanged();
    });
    connect(view, &WebView::contentMouseDown, this, [this, view] {
        if (view == currentView()) emit contentMouseDown();
    });
    connect(view, &WebView::newTabRequested, this, [this, item](WebView *child, bool background) {
        adoptChildView(child, item, background);
    });
    connect(view, &WebView::closeRequested, this, [this, view] {
        for (auto it = m_views.begin(); it != m_views.end(); ++it) {
            if (it.value() == view) {
                m_tabs->setCurrentItem(it.key());
                closeCurrent();
                return;
            }
        }
    });
}
