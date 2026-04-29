#include "TabTree.hpp"

#include "FaviconService.hpp"
#include "ProfileStore.hpp"
#include "WebView.hpp"

#include <QIcon>
#include <QPixmap>
#include <QStackedLayout>
#include <QStringList>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVariant>

TabTree::TabTree(ProfileStore &profiles, FaviconService *favicons, QWidget *stack,
                 const Theme &theme, QWidget *sidebarParent, QObject *parent)
    : QObject(parent), m_profiles(&profiles), m_favicons(favicons),
      m_stack(stack), m_theme(theme) {
    m_tabs = new QTreeWidget(sidebarParent);
    m_tabs->setObjectName("TabTree");
    m_tabs->setHeaderHidden(true);
    m_tabs->setIndentation(14);
    m_tabs->setRootIsDecorated(false);
    m_tabs->setAnimated(true);
    m_tabs->setFrameShape(QFrame::NoFrame);
    m_tabs->setIconSize(QSize(16, 16));
    m_tabs->setExpandsOnDoubleClick(false);
    m_tabs->setUniformRowHeights(true);
    m_tabs->setAttribute(Qt::WA_MacShowFocusRect, false);
    m_tabs->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_tabs->setAttribute(Qt::WA_TranslucentBackground);
    m_tabs->viewport()->setAutoFillBackground(false);
    m_tabs->setStyleSheet(QString(
        "QTreeWidget#TabTree { background: transparent; border: none; color: %1; }"
        "QTreeWidget#TabTree::item { padding: 4px 6px; border-radius: 6px; color: %1; }"
        "QTreeWidget#TabTree::item:selected { background: %2; color: %1; }"
        "QTreeWidget#TabTree::item:hover:!selected { background: %3; }")
        .arg(m_theme.foreground.name(),
             m_theme.raised.name(),
             m_theme.hover.name()));

    connect(m_tabs, &QTreeWidget::currentItemChanged, this, [this] {
        emit currentTabChanged();
    });

    if (m_favicons) {
        connect(m_favicons, &FaviconService::faviconReady, this,
                [this](const QString &domain, const QPixmap &pm) {
                    if (pm.isNull()) return;
                    const QIcon icon(pm);
                    for (auto it = m_views.constBegin(); it != m_views.constEnd(); ++it) {
                        const QString itemDomain = it.value()->url().host();
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

void TabTree::selectItem(QTreeWidgetItem *item) {
    if (!item) return;
    m_tabs->setCurrentItem(item);
    emit currentTabChanged();
}

void TabTree::newTab(const QUrl &url, bool background, QTreeWidgetItem *parentItem) {
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
    auto *item = currentItem();
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
