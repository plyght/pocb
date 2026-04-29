#pragma once

#include "FaviconService.hpp"
#include "ProfileStore.hpp"
#include "Theme.hpp"

#include <QHash>
#include <QMainWindow>
#include <QUrl>
#include <functional>

class AddressBarController;
class SidebarController;
class TabTree;
class FloatingOmnibox;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QListWidget;
class QNetworkAccessManager;
class QNetworkReply;
class QProgressBar;
class QTimer;
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
    void moveEvent(QMoveEvent *e) override;
    void resizeEvent(QResizeEvent *e) override;
    bool eventFilter(QObject *obj, QEvent *ev) override;

private slots:
    void loadFromOmnibox();
    void showSettings();
    void updateForCurrentTab();

private:
    QUrl urlFromInput(const QString &input) const;
    WebView *currentView() const;
    void setupUi();
    void setupActions();
    QWidget *buildTopbar(QWidget *parent);
    Theme m_theme;
    ProfileStore m_profiles;
    QString m_homePage = "https://search.brave.com";
    QString m_searchEngine = "https://search.brave.com/search?q=%1";

    QLineEdit *m_omnibox = nullptr;
    FloatingOmnibox *m_floatingOmnibox = nullptr;
    QProgressBar *m_progress = nullptr;
    QWidget *m_topSeparator = nullptr;
    QSplitter *m_splitter = nullptr;
    TabTree *m_tabTree = nullptr;
    QWidget *m_stack = nullptr;
    QWidget *m_webContainer = nullptr;
    QWidget *m_topbar = nullptr;
    QToolButton *m_backBtn = nullptr;
    QToolButton *m_fwdBtn = nullptr;
    QToolButton *m_reloadBtn = nullptr;
    QToolButton *m_settingsBtn = nullptr;
    QToolButton *m_newTabBtn = nullptr;
    QLineEdit *m_addressBar = nullptr;
    QLabel *m_lockIcon = nullptr;
    AddressBarController *m_addressBarCtl = nullptr;
    QAction *m_omniAction = nullptr;
    FaviconService *m_favicons = nullptr;
    QHBoxLayout *m_toolbarLayout = nullptr;
    SidebarController *m_sidebar = nullptr;
};
