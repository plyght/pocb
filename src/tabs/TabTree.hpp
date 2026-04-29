#pragma once

#include "Theme.hpp"

#include <QColor>
#include <QHash>
#include <QObject>
#include <QString>
#include <QUrl>

class FaviconService;
class ProfileStore;
class QTreeWidget;
class QTreeWidgetItem;
class QWidget;
class WebView;

class TabTree final : public QObject {
    Q_OBJECT
public:
    TabTree(ProfileStore &profiles, FaviconService *favicons, QWidget *stack,
            const Theme &theme, QWidget *sidebarParent, QObject *parent);

    QTreeWidget *widget() const { return m_tabs; }
    WebView *currentView() const;
    QTreeWidgetItem *currentItem() const;

    void newTab(const QUrl &url = QUrl(), bool background = false,
                QTreeWidgetItem *parentItem = nullptr);
    void closeCurrent();
    // Re-create with a single home tab (used when the active profile changes).
    void rebuildForProfile();

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

private:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void wireView(WebView *view, QTreeWidgetItem *item);
    void adoptChildView(WebView *child, QTreeWidgetItem *parentItem, bool background);
    void selectItem(QTreeWidgetItem *item);
    void closeItem(QTreeWidgetItem *item);

    ProfileStore *m_profiles = nullptr;
    FaviconService *m_favicons = nullptr;
    QWidget *m_stack = nullptr;
    Theme m_theme;
    QTreeWidget *m_tabs = nullptr;
    QHash<QTreeWidgetItem *, WebView *> m_views;
    QString m_homePage = "https://search.brave.com";
};
