#include "TabTree.hpp"

#include "FaviconService.hpp"
#include "MacIntegration.hpp"
#include "ProfileStore.hpp"
#include "WebView.hpp"

#include <QCursor>
#include <QEvent>
#include <QHeaderView>
#include <QIcon>
#include <QMouseEvent>
#include <QFontMetrics>
#include <QPainter>
#include <QPixmap>
#include <QStackedLayout>
#include <QStyledItemDelegate>
#include <QStringList>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVariant>

namespace {

QRect closeButtonRect(const QRect &rowRect, int viewportWidth) {
    const int side = 18;
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
            if (viewportWidth > 0) rowRect.setRight(viewportWidth - 4);
            painter->setPen(Qt::NoPen);
            painter->setBrush(fill);
            painter->drawRoundedRect(rowRect.adjusted(0, 2, 0, -2), 7, 7);
        }

        const QVariant decoration = index.data(Qt::DecorationRole);
        QRect textRect = option.rect.adjusted(14 + depth * 18, 0, -30, 0);
        if (decoration.canConvert<QIcon>()) {
            const QIcon icon = qvariant_cast<QIcon>(decoration);
            const QRect iconRect(textRect.left(), option.rect.top() + (option.rect.height() - 16) / 2, 16, 16);
            icon.paint(painter, iconRect, Qt::AlignCenter);
            textRect.setLeft(iconRect.right() + 7);
        }

        painter->setPen(m_theme.foreground);
        painter->setFont(option.font);
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
    m_tabs = new TabTreeWidget(sidebarParent);
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
    m_tabs->setIconSize(QSize(16, 16));
    m_tabs->setExpandsOnDoubleClick(false);
    m_tabs->setUniformRowHeights(true);
    m_tabs->setMouseTracking(true);
    m_tabs->viewport()->setMouseTracking(true);
    m_tabs->viewport()->installEventFilter(this);
    m_tabs->setItemDelegate(new TabItemDelegate(m_theme, m_tabs));
    m_tabs->setAttribute(Qt::WA_MacShowFocusRect, false);
    m_tabs->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_tabs->setAttribute(Qt::WA_TranslucentBackground);
    m_tabs->setAttribute(Qt::WA_NoSystemBackground);
    m_tabs->viewport()->setAttribute(Qt::WA_TranslucentBackground);
    m_tabs->viewport()->setAttribute(Qt::WA_NoSystemBackground);
    m_tabs->viewport()->setAutoFillBackground(false);
    m_tabs->setStyleSheet(QString(
        "QTreeWidget#TabTree { background: transparent; border: none; color: %1; outline: 0; }"
        "QTreeWidget#TabTree::item { padding: 4px 28px 4px 6px; border: none; background: transparent; color: %1; selection-background-color: transparent; }"
        "QTreeWidget#TabTree::item:selected { background: transparent; color: %1; selection-background-color: transparent; }"
        "QTreeWidget#TabTree::item:selected:active { background: transparent; color: %1; selection-background-color: transparent; }"
        "QTreeWidget#TabTree::item:selected:!active { background: transparent; color: %1; selection-background-color: transparent; }"
        "QTreeWidget#TabTree::item:hover:!selected { background: transparent; }"
        "QTreeWidget#TabTree::branch { background: transparent; border: none; image: none; }"
        "QTreeWidget#TabTree::branch:selected { background: transparent; border: none; image: none; }"
        "QTreeWidget#TabTree::branch:hover { background: transparent; border: none; image: none; }")
        .arg(m_theme.foreground.name()));

    connect(m_tabs, &QTreeWidget::currentItemChanged, this, [this] {
        emit currentTabChanged();
        // Replay the active tab's last known theme colour immediately for a
        // snappy chrome update, then kick off a fresh sniff in case the
        // page has changed since (scrolled, dynamically restyled, etc.).
        if (auto *v = currentView()) {
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
    return m_tabs->currentItem();
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
    m_tabs->setCurrentItem(item);
    m_tabs->clearSelection();
    m_tabs->viewport()->update();
    emit currentTabChanged();
}

bool TabTree::eventFilter(QObject *watched, QEvent *event) {
    if (watched == m_tabs->viewport() && event->type() == QEvent::Resize) {
        m_tabs->setColumnWidth(0, m_tabs->viewport()->width());
    }
    if (watched == m_tabs->viewport() && event->type() == QEvent::MouseMove) {
        auto *mouse = static_cast<QMouseEvent *>(event);
        const auto *item = m_tabs->itemAt(mouse->pos());
        const bool overClose = item && closeButtonRect(m_tabs->visualItemRect(const_cast<QTreeWidgetItem *>(item)), m_tabs->viewport()->width()).contains(mouse->pos());
        m_tabs->viewport()->setCursor(overClose ? Qt::PointingHandCursor : Qt::ArrowCursor);
        m_tabs->viewport()->update();
    }
    if (watched == m_tabs->viewport() && event->type() == QEvent::Leave) {
        m_tabs->viewport()->unsetCursor();
        m_tabs->viewport()->update();
    }
    if (watched == m_tabs->viewport() && event->type() == QEvent::MouseButtonPress) {
        auto *mouse = static_cast<QMouseEvent *>(event);
        if (mouse->button() == Qt::LeftButton) {
            if (auto *item = m_tabs->itemAt(mouse->pos())) {
                if (closeButtonRect(m_tabs->visualItemRect(item), m_tabs->viewport()->width()).contains(mouse->pos())) {
                    m_tabs->viewport()->unsetCursor();
                    closeItem(item);
                    return true;
                }
            }
        }
    }
    return QObject::eventFilter(watched, event);
}

WebView *TabTree::newTabForExtension(const QUrl &url, bool background, QTreeWidgetItem *parentItem) {
    auto *view = new WebView(m_profiles->currentProfile(), m_stack);

    auto *item = new QTreeWidgetItem(QStringList() << "New tab");
    item->setData(0, Qt::UserRole, QVariant::fromValue<quintptr>(reinterpret_cast<quintptr>(view)));
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
    auto *view = m_views.take(item);
    if (view) view->deleteLater();
    delete item;
    if (m_tabs->topLevelItemCount() == 0) newTab(QUrl(m_homePage));
    emit currentTabChanged();
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
    connect(view, &WebView::titleChanged, this, [this, item](const QString &title) {
        item->setText(0, title.isEmpty() ? "New tab" : title);
        emit currentTabChanged();
    });
    connect(view, &WebView::urlChanged, this, [this, view, item](const QUrl &url) {
        if (view == currentView()) emit currentTabChanged();
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
        emit loadProgress(progress);
    });
    connect(view, &WebView::themeColorChanged, this, [this, view](const QColor &c) {
        if (view != currentView()) return;
        emit themeColorChanged(c);
    });
    connect(view, &WebView::loadFinished, this, [this](bool) { emit currentTabChanged(); });
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
