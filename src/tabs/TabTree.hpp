#pragma once

#include "Theme.hpp"

#include <QColor>
#include <QHash>
#include <QList>
#include <QPointer>
#include <QObject>
#include <QString>
#include <QPoint>
#include <QUrl>

class FaviconService;
class ProfileStore;
class QPoint;
class QListWidget;
class QListWidgetItem;
class QTreeWidget;
class QTreeWidgetItem;
class QWidget;
class WebView;

class TabTree final : public QObject {
    Q_OBJECT
public:
    TabTree(ProfileStore &profiles, FaviconService *favicons, QWidget *stack,
            const Theme &theme, QWidget *sidebarParent, QObject *parent);

    QWidget *widget() const { return m_container; }
    QTreeWidget *treeWidget() const { return m_tabs; }
    WebView *currentView() const;
    QTreeWidgetItem *currentItem() const;
    QList<WebView *> views() const;
    void selectView(WebView *view);
    void markViewsSplit(WebView *first, WebView *second);

    WebView *newTabForExtension(const QUrl &url = QUrl(), bool background = false,
                                QTreeWidgetItem *parentItem = nullptr);
    void adoptExtensionView(WebView *child, bool background = false);
    void newTab(const QUrl &url = QUrl(), bool background = false,
                QTreeWidgetItem *parentItem = nullptr);
    void closeCurrent();
    QList<QUrl> tabUrls() const;
    void restoreTabs(const QList<QUrl> &urls);
    void reopenUrl(const QUrl &url);
    // Re-create with restored tabs for the active profile.
    void rebuildForProfile(const QList<QUrl> &urls = {});

    void setHomePage(const QString &url) { m_homePage = url; }
    QString homePage() const { return m_homePage; }

signals:
    // Emitted whenever the current tab changes or its URL/title changes —
    // BrowserWindow uses this to refresh the address bar / window title.
    void currentTabChanged();
    // Current tab's load progress (0..100). 0 / 100 means hide.
    void loadProgress(int progress);
    // Forwarded from the current tab's WebView; emitted after each
    // navigation finishes with the page's preferred chrome colour. Invalid
    // QColor when the page exposes nothing useful.
    void themeColorChanged(const QColor &color);
    void contentMouseDown();
    void tabDetachRequested(WebView *view, const QUrl &url, const QPoint &globalPos);
    void tabSplitRequested(WebView *first, WebView *second, const QPoint &globalPos);
    void tabSplitPreviewRequested(WebView *dragged, WebView *target, const QPoint &globalPos);
    void tabSplitPreviewEnded();
    void tabClosed(const QUrl &url, const QString &title);

private:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void wireView(WebView *view, QTreeWidgetItem *item);
    void adoptChildView(WebView *child, QTreeWidgetItem *parentItem, bool background);
    void selectItem(QTreeWidgetItem *item);
    void closeItem(QTreeWidgetItem *item);
    void closeChildren(QTreeWidgetItem *item);
    void duplicateItem(QTreeWidgetItem *item);
    void showContextMenu(QTreeWidgetItem *item, const QPoint &globalPos);
    void setItemPinState(QTreeWidgetItem *item, int state);
    void markItemUnread(QTreeWidgetItem *item, bool unread);
    void deleteItemRecursive(QTreeWidgetItem *item);
    int essentialCount() const;
    void resetPinnedItem(QTreeWidgetItem *item);
    void replacePinnedUrl(QTreeWidgetItem *item);
    void insertTopLevelForState(QTreeWidgetItem *item, int state);
    void syncEssentialGrid();
    QTreeWidgetItem *treeItemForEssential(QListWidgetItem *item) const;
    QTreeWidgetItem *itemForView(WebView *view) const;
    QPixmap dragPixmapForItem(QTreeWidgetItem *item, bool essential) const;
    QWidget *createTabDragOverlay(QTreeWidgetItem *item, bool essential, bool outsideWindow) const;
    void updateTabDragOverlay(bool essential, const QPoint &global);
    void finishTabDrop(QTreeWidgetItem *draggedItem, const QPoint &globalPos, QObject *target, const QPoint &localPos);
    void selectFallbackForDraggedItem(QTreeWidgetItem *item);
    int essentialDropIndex(const QPoint &pos) const;
    void setItemEssentialAt(QTreeWidgetItem *item, int index);
    void showDropIndicator(const QRect &rect);
    void clearDropIndicator();

    ProfileStore *m_profiles = nullptr;
    FaviconService *m_favicons = nullptr;
    QWidget *m_stack = nullptr;
    QWidget *m_container = nullptr;
    Theme m_theme;
    QListWidget *m_essentials = nullptr;
    QTreeWidget *m_tabs = nullptr;
    QPointer<QWidget> m_essentialsViewport;
    QPointer<QWidget> m_tabsViewport;
    QHash<QTreeWidgetItem *, WebView *> m_views;
    QHash<QListWidgetItem *, QTreeWidgetItem *> m_essentialItems;
    QTreeWidgetItem *m_currentEssentialItem = nullptr;
    QList<QTreeWidgetItem *> m_tabHistory;
    QTreeWidgetItem *m_pressedItem = nullptr;
    QPoint m_pressPos;
    QPoint m_pressGlobalPos;
    QWidget *m_dropIndicator = nullptr;
    QWidget *m_dragOverlay = nullptr;
    QTreeWidgetItem *m_draggingItem = nullptr;
    bool m_draggingFromEssential = false;
    QString m_homePage = "https://search.brave.com";
};
