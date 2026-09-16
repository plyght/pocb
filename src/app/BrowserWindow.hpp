#pragma once

#include "BookmarkStore.hpp"
#include "FaviconService.hpp"
#include "ProfileStore.hpp"
#include "Theme.hpp"

#include <QHash>
#include <QIcon>
#include <QList>
#include <QMetaObject>
#include <QPixmap>
#include <QPoint>
#include <QPointer>
#include <QStringList>
#include <QMainWindow>
#include <QUrl>
#include <QVariant>
#include <functional>

class AddressBarController;
class DownloadsPopover;
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
namespace ui {
class CollapsingToolbarHost;
class DownloadsButton;
class PagerDots;
class ProfileAvatarButton;
class SidebarPreviewPane;
class ToastWidget;
class ToolbarCluster;
class ToolbarGrabber;
}

class BrowserWindow final : public QMainWindow {
    Q_OBJECT
public:
    explicit BrowserWindow(QWidget *parent = nullptr);
    WebView *extensionCurrentView() const;
    QList<WebView *> extensionViews() const;
    WebView *extensionCreateTab(const QUrl &url, bool background);
    WebView *extensionAdoptNativeTab(void *nativeWebView, bool background);
    void extensionSelectView(WebView *view);
    void extensionCloseView(WebView *view);
    void extensionSetAction(const QString &key, const QString &label, const QIcon &icon, std::function<void(QWidget *)> handler);

    // Downloads button badge: activeCount <= 0 clears it; progress is the
    // aggregate 0..1 (negative = indeterminate).
    void showDownloadsBadge(int activeCount, double progress);
    // Liquid Glass toast, top-right under the toolbar, auto-dismisses after
    // 3.5 s. Clicking it emits toastClicked().
    void showToast(const QString &title, const QString &subtitle, const QIcon &icon);

signals:
    void downloadsRequested();
    void toastClicked();

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
    void setupIntegrations();
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
    QPixmap renderProfilePreview(const QString &profile, const QSize &size) const;
    bool handleProfileSwipeWheel(QWheelEvent *wheel);
    void resetProfileSwipeState();
    void updatePagerDots();
    // Collapsing toolbar.
    void setToolbarCollapsed(bool collapsed, bool animated = true);
    void expandToolbar();
    void handlePageScroll(const QVariant &body);
    void observeScrollFor(WebView *view);
    void positionToolbarGrabber();
    void syncAddressPillGlass();
    void positionToast();
    void setToolbarRowVisible(bool visible);
    void showProfileMenu();
    void showExtensionsMenu();
    void showCopiedLinkPopup();
    void refreshFloatingOmniboxItems();
    void openBlankTabForLocationEntry();
    void rememberCurrentPage();
    QStringList restoredSessionForProfile(const QString &profileName) const;
    void saveSessionForProfile(const QString &profileName) const;
    void reopenLastClosedTab();
    void showArchiveMenu(QWidget *anchor);
    void detachTabToWindow(WebView *view, const QUrl &url, const QPoint &globalPos);
    void splitTabs(WebView *first, WebView *second, const QPoint &globalPos);
    void showSplitPreview(WebView *dragged, WebView *target, const QPoint &globalPos);
    void hideSplitPreview();
    void showTabSwitcher(int direction);
    void advanceTabSwitcher(int direction);
    void acceptTabSwitcher();
    void hideTabSwitcher();
    void applyPageColorScheme(const QString &scheme);
    QList<WebView *> orderedSwitchableTabs() const;
    bool handleInternalUrl(const QUrl &url);
    QString passkeyDiagnosticsHtml() const;
    struct RecentPage {
        QString title;
        QUrl url;
    };
    struct ClosedTab {
        QString title;
        QUrl url;
    };
    Theme m_theme;
    ProfileStore m_profiles;
    BookmarkStore m_bookmarks;
    QString m_homePage = "about:blank";
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
    QToolButton *m_sidebarBtn = nullptr;
    QToolButton *m_backBtn = nullptr;
    QToolButton *m_fwdBtn = nullptr;
    QToolButton *m_reloadBtn = nullptr;
    QToolButton *m_settingsBtn = nullptr;
    QToolButton *m_newTabBtn = nullptr;
    ui::ToolbarCluster *m_toolbarActions = nullptr;
    QList<ui::ToolbarCluster *> m_toolbarClusters;
    QToolButton *m_extensionsBtn = nullptr;
    ui::DownloadsButton *m_downloadsBtn = nullptr;
    DownloadsPopover *m_downloadsPopover = nullptr;
    QString m_lastFinishedDownload;
    QToolButton *m_profileBtn = nullptr;
    ui::ProfileAvatarButton *m_profileAvatar = nullptr;
    ui::PagerDots *m_pagerDots = nullptr;
    QWidget *m_profileSwitcher = nullptr;
    QPropertyAnimation *m_profileAnim = nullptr;
    QVariantAnimation *m_sidebarSwipeAnim = nullptr;
    // Profile swipe gesture state (strictly phase driven, see
    // handleProfileSwipeWheel). m_profileSwipeRemainder is the raw,
    // unclamped horizontal travel; m_sidebarSwipeOffset is what is drawn.
    int m_profileSwipeRemainder = 0;
    int m_sidebarSwipeOffset = 0;
    int m_sidebarSwipeDirection = 0;
    bool m_sidebarSwipeActive = false;
    bool m_sidebarSwipeSettling = false;
    bool m_sidebarSwipeGestureOpen = false;
    bool m_sidebarSwipeHapticFired = false;
    int m_sidebarSwipeAxis = 0;  // 0 undecided, +1 horizontal, -1 vertical
    int m_sidebarSwipeAxisDx = 0;
    int m_sidebarSwipeAxisDy = 0;
    struct SwipeSample {
        qint64 ms;
        int dx;
    };
    QList<SwipeSample> m_sidebarSwipeSamples;
    QString m_sidebarPreviewProfile;
    ui::SidebarPreviewPane *m_sidebarPreviewPane = nullptr;
    // Collapsing toolbar state.
    ui::CollapsingToolbarHost *m_toolbarHost = nullptr;
    ui::ToolbarGrabber *m_toolbarGrabber = nullptr;
    QVariantAnimation *m_toolbarAnim = nullptr;
    QMetaObject::Connection m_scrollObserverConn;
    QMetaObject::Connection m_navigationExpandConn;
    QPointer<WebView> m_scrollObservedView;
    bool m_toolbarCollapsed = false;
    bool m_toolbarRowAvailable = true;
    bool m_collapseToolbarOnScroll = true;
    double m_scrollDownAccum = 0.0;
    ui::ToastWidget *m_toast = nullptr;
    QHash<QString, QStringList> m_profileTabSnapshots;
    QHash<QString, QToolButton *> m_extensionActionButtons;
    QHash<QString, std::function<void(QWidget *)>> m_extensionActionHandlers;
    QLineEdit *m_addressBar = nullptr;
    QLabel *m_lockIcon = nullptr;
    QLabel *m_searchIcon = nullptr;
    QToolButton *m_pillMenuBtn = nullptr;
    QWidget *m_addrWrap = nullptr;
    QPoint m_toolbarDragOffset;
    bool m_toolbarDragging = false;
    QWidget *m_sidebarWidget = nullptr;
    QWidget *m_sidebarViewport = nullptr;
    QWidget *m_sidebarStrip = nullptr;
    QWidget *m_sidebarPage = nullptr;
    QWidget *m_sidebarPreviewPage = nullptr;
    QWidget *m_sidebarContent = nullptr;
    QWidget *m_sidebarHeader = nullptr;
    bool m_addrInSidebar = false;
    QColor m_lastAppliedChrome;
    void applyChromeForPageColor(const QColor &pageColor);
    AddressBarController *m_addressBarCtl = nullptr;
    QAction *m_omniAction = nullptr;
    FaviconService *m_favicons = nullptr;
    QHBoxLayout *m_toolbarLayout = nullptr;
    SidebarController *m_sidebar = nullptr;
    QList<WebView *> m_tabRecency;
    QWidget *m_tabSwitcher = nullptr;
    QList<WebView *> m_tabSwitcherTabs;
    int m_tabSwitcherIndex = 0;
    QTimer *m_tabSwitcherOpenTimer = nullptr;
    bool m_tabSwitcherPending = false;
    QHash<WebView *, QWidget *> m_splitHosts;
    QWidget *m_splitPreview = nullptr;
    WebView *m_splitPreviewTarget = nullptr;
    bool m_splitPreviewLeft = false;
    bool m_splitPreviewActive = false;
    QList<RecentPage> m_recentPages;
    QList<ClosedTab> m_closedTabs;
};
