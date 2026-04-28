#pragma once

#include "FaviconService.hpp"
#include "ProfileStore.hpp"
#include "Theme.hpp"

#include <QHash>
#include <QMainWindow>
#include <QUrl>
#include <functional>

class FloatingOmnibox;
class QHBoxLayout;
class QLineEdit;
class QProgressBar;
class QSplitter;
class QToolBar;
class QToolButton;
class QAction;
class QTreeWidget;
class QTreeWidgetItem;
class WebView;

class BrowserWindow final : public QMainWindow {
    Q_OBJECT
public:
    explicit BrowserWindow(QWidget *parent = nullptr);

protected:
    void showEvent(QShowEvent *e) override;

private slots:
    void newTab(const QUrl &url = QUrl(), bool background = false, QTreeWidgetItem *parentItem = nullptr);
    void closeCurrentTab();
    void loadFromOmnibox();
    void showSettings();
    void updateForCurrentTab();

private:
    QUrl urlFromInput(const QString &input) const;
    WebView *currentView() const;
    QTreeWidgetItem *currentItem() const;
    void selectItem(QTreeWidgetItem *item);
    void wireView(WebView *view, QTreeWidgetItem *item);
    void adoptChildView(WebView *child, QTreeWidgetItem *parentItem, bool background);
    void rebuildProfilePages();
    void setupUi();
    void setupActions();

    Theme m_theme;
    ProfileStore m_profiles;
    QString m_homePage = "https://search.brave.com";
    QString m_searchEngine = "https://search.brave.com/search?q=%1";

    QLineEdit *m_omnibox = nullptr;
    FloatingOmnibox *m_floatingOmnibox = nullptr;
    QProgressBar *m_progress = nullptr;
    QSplitter *m_splitter = nullptr;
    QTreeWidget *m_tabs = nullptr;
    QWidget *m_stack = nullptr;
    QHash<QTreeWidgetItem *, WebView *> m_views;
    QAction *m_omniAction = nullptr;
    FaviconService *m_favicons = nullptr;
    QHBoxLayout *m_toolbarLayout = nullptr;
    std::function<void(bool)> m_setStackHostInset;
};
