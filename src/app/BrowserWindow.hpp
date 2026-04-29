#pragma once

#include "BookmarkStore.hpp"
#include "FaviconService.hpp"
#include "ProfileStore.hpp"
#include "Theme.hpp"

#include <QHash>
#include <QList>
#include <QStringList>
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
class QPropertyAnimation;
class QTimer;
class QVariantAnimation;
class QSplitter;
class QToolBar;
class QToolButton;
class QAction;
class QTreeWidget;
class QTreeWidget;
class QTreeWidgetItem;
class WebView;

class BrowserWindow final : public QMainWindow {
    Q_OBJECT
public:
    explicit BrowserWindow(QWidget *parent = nullptr);
    WebView *extensionCurrentView() const;
    QList<WebView *> extensionViews() const;
    WebView *extensionCreateTab(const QUrl &url, bool background);
    void extensionSelectView(WebView *view);
    void extensionCloseView(WebView *view);
    void extensionSetAction(const QString &key, const QString &label, std::function<void()> handler);

protected:
    void showEvent(QShowEvent *e) override;
    void moveEvent(QMoveEvent *e) override;
    void resizeEvent(QResizeEvent *e) override;
    void closeEvent(QCloseEvent *e) override;
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
    QWidget *buildProfileSwitcher(QWidget *parent);
    void updateProfileSwitcher();
    void switchProfileRelative(int direction);
    void animateProfileSwitcher(int direction);
    void setSidebarSwipeOffset(int offset);
    void settleSidebarSwipe(bool commit);
    QStringList orderedProfiles() const;
    void updateCurrentProfileSnapshot();
    void updateSidebarPreview(int direction);
    void showProfileMenu();
    void showCopiedLinkPopup();
    Theme m_theme;
    ProfileStore m_profiles;
    BookmarkStore m_bookmarks;
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
    QToolButton *m_profileBtn = nullptr;
    QWidget *m_profileSwitcher = nullptr;
    QPropertyAnimation *m_profileAnim = nullptr;
    QVariantAnimation *m_sidebarSwipeAnim = nullptr;
    QTimer *m_sidebarSwipeSettleTimer = nullptr;
    int m_profileSwipeRemainder = 0;
    int m_sidebarSwipeOffset = 0;
    bool m_sidebarSwipeActive = false;
    bool m_sidebarSwipeSettling = false;
    QHash<QString, QStringList> m_profileTabSnapshots;
    QHash<QString, QToolButton *> m_extensionActionButtons;
    QLineEdit *m_addressBar = nullptr;
    QLabel *m_lockIcon = nullptr;
    QLabel *m_searchIcon = nullptr;
    QToolButton *m_pillMenuBtn = nullptr;
    QWidget *m_addrWrap = nullptr;
    QWidget *m_sidebarWidget = nullptr;
    QWidget *m_sidebarViewport = nullptr;
    QWidget *m_sidebarStrip = nullptr;
    QWidget *m_sidebarPage = nullptr;
    QWidget *m_sidebarPreviewPage = nullptr;
    QTreeWidget *m_sidebarPreviewTabs = nullptr;
    QToolButton *m_sidebarPreviewIcon = nullptr;
    QWidget *m_sidebarHeader = nullptr;
    bool m_addrInSidebar = false;
    QColor m_lastAppliedChrome;
    void applyChromeForPageColor(const QColor &pageColor);
    AddressBarController *m_addressBarCtl = nullptr;
    QAction *m_omniAction = nullptr;
    FaviconService *m_favicons = nullptr;
    QHBoxLayout *m_toolbarLayout = nullptr;
    SidebarController *m_sidebar = nullptr;
};
