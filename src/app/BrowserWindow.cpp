#include "BrowserWindow.hpp"


#include "AddressBarController.hpp"
#include "FloatingOmnibox.hpp"
#include "ChromeWidgets.hpp"
#include "LayoutMetrics.hpp"
#include "MacIntegration.hpp"
#include "SettingsDialog.hpp"
#include "SidebarController.hpp"
#include "TabTree.hpp"
#include "Topbar.hpp"
#include "WebView.hpp"

#include <QAction>
#include <QApplication>
#include <QDebug>
#include <QClipboard>
#include <QDesktopServices>
#include <QMenu>
#include <QEvent>
#include <QFocusEvent>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QFrame>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMenuBar>
#include <QMouseEvent>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProgressBar>
#include <QDir>
#include <QShortcut>
#include <QShortcutEvent>
#include <QTimer>
#include <QUrlQuery>
#include <QSettings>
#include <QSplitter>
#include <QStandardPaths>
#include <QStackedLayout>
#include <QStatusBar>
#include <QStyle>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QVariantAnimation>
#include <QEasingCurve>
#include <QEvent>
#include <QVBoxLayout>

BrowserWindow::BrowserWindow(QWidget *parent) : QMainWindow(parent) {
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_NoSystemBackground);
    setAutoFillBackground(false);
#ifdef Q_OS_MACOS
    // Qt 6.9+ flags: extend the client area under the titlebar and stop Qt
    // from reserving safe-area margins for the titlebar in the central
    // widget. Without these, QMainWindow leaves a band of empty space at the
    // top of the window even with NSWindowStyleMaskFullSizeContentView set
    // (regression since Qt 6.4 — see QTBUG-134797).
    setAttribute(Qt::WA_ContentsMarginsRespectsSafeArea, false);
    setWindowFlags(windowFlags() | Qt::ExpandedClientAreaHint | Qt::NoTitleBarBackgroundHint);
#endif
    setupUi();
    setupActions();
    setWindowTitle("pocb");
    {
        QSettings settings;
        const QByteArray geom = settings.value("ui/windowGeometry").toByteArray();
        if (!geom.isEmpty()) {
            restoreGeometry(geom);
        } else {
            resize(ui::metrics::WindowDefaultWidth, ui::metrics::WindowDefaultHeight);
        }
    }
    // Force NSWindow creation so we can position the traffic lights before
    // the window is visible (no one-frame flash at the default position).
    winId();
    mac::integrateUnifiedToolbar(this, nullptr, /*compact=*/true);
    m_tabTree->newTab(QUrl(m_homePage));
}

void BrowserWindow::moveEvent(QMoveEvent *e) {
    QMainWindow::moveEvent(e);
    if (m_sidebar) {
        if (m_sidebar->hoverZoneVisible()) m_sidebar->positionHoverZone();
        if (m_sidebar->floatingVisible()) m_sidebar->positionFloating();
    }
}

void BrowserWindow::resizeEvent(QResizeEvent *e) {
    QMainWindow::resizeEvent(e);
    if (m_sidebar) {
        if (m_sidebar->hoverZoneVisible()) m_sidebar->positionHoverZone();
        if (m_sidebar->floatingVisible()) m_sidebar->positionFloating();
    }
}

void BrowserWindow::closeEvent(QCloseEvent *e) {
    QSettings().setValue("ui/windowGeometry", saveGeometry());
    QMainWindow::closeEvent(e);
}

void BrowserWindow::showEvent(QShowEvent *e) {
    QMainWindow::showEvent(e);
    mac::integrateUnifiedToolbar(this, nullptr, /*compact=*/true);
    mac::enableWindowVibrancy(this, mac::VibrancyMaterial::Sidebar);
    mac::enableHighRefreshRate(this);
    // Round the web-content stack on the next event loop turn (after the
    // first QWebEngineView NSView exists).
    QTimer::singleShot(0, this, [this] {
        if (m_webContainer) {
            mac::roundWidgetCorners(m_webContainer, ui::metrics::WebContainerRadius, /*recurseDescendants=*/false);
        }
        if (m_stack) mac::roundWidgetCorners(m_stack, 0.0);
        if (auto *host = findChild<QWidget *>("StackHost")) {
            mac::applyVibrancyBehind(host, mac::VibrancyMaterial::Sidebar);
        }
    });
}

void BrowserWindow::loadFromOmnibox() {
    if (auto *view = currentView()) view->load(urlFromInput(m_omnibox->text()));
}

void BrowserWindow::showCopiedLinkPopup() {
    QWidget *host = m_webContainer ? m_webContainer : m_stack;
    if (!host) host = this;

    auto *popup = new QFrame(host);
    popup->setObjectName("CopiedLinkPopup");
    popup->setAttribute(Qt::WA_DeleteOnClose);
    popup->setAttribute(Qt::WA_TranslucentBackground);
    popup->setAutoFillBackground(false);
    popup->setFixedSize(ui::metrics::CopiedLinkPopupWidth, ui::metrics::CopiedLinkPopupHeight);
    mac::applyVibrancyBehind(popup, mac::VibrancyMaterial::HUDWindow);
    popup->setStyleSheet(QString(
        "QFrame#CopiedLinkPopup {"
        "  background: %1;"
        "  border: 1px solid %2;"
        "  border-radius: 8px;"
        "}"
        "QLabel {"
        "  color: %3;"
        "  font-family: '%4';"
        "  font-size: %5px;"
        "}")
        .arg(QColor(36, 36, 38, 150).name(QColor::HexArgb),
             m_theme.border.name(),
             m_theme.foreground.name(),
             m_theme.fontFamily,
             QString::number(m_theme.regularSize)));

    auto *row = new QHBoxLayout(popup);
    row->setContentsMargins(12, 0, 12, 0);
    row->setSpacing(8);

    auto *icon = new QLabel(popup);
    icon->setFixedSize(16, 16);
    icon->setPixmap(mac::sfSymbolIcon("link", 12.5, m_theme.foreground).pixmap(16, 16));
    row->addWidget(icon);

    auto *label = new QLabel("Copied link", popup);
    row->addWidget(label);

    popup->move(ui::metrics::CopiedLinkPopupInset,
                qMax(ui::metrics::CopiedLinkPopupInset,
                     host->height() - popup->height() - ui::metrics::CopiedLinkPopupInset));
    popup->show();
    mac::roundWidgetCorners(popup, 8.0, false);
    popup->raise();
    QTimer::singleShot(1300, popup, &QWidget::close);
}

void BrowserWindow::showSettings() {
    SettingsDialog dialog(m_profiles, this);
    connect(&dialog, &SettingsDialog::homePageChanged, this, [this](const QString &url) {
        m_homePage = url;
        if (m_tabTree) m_tabTree->setHomePage(url);
    });
    connect(&dialog, &SettingsDialog::searchEngineChanged, this, [this](const QString &url) {
        if (!url.contains("%1")) return;
        m_searchEngine = url;
        if (m_floatingOmnibox) m_floatingOmnibox->setSearchEngineUrl(url);
    });
    connect(&dialog, &SettingsDialog::showFullUrlChanged, this, [this](bool full) {
        if (m_addressBarCtl) m_addressBarCtl->setShowFullUrl(full);
    });
    dialog.exec();
}

void BrowserWindow::updateForCurrentTab() {
    mac::refreshUnifiedToolbar(this);
    auto *view = currentView();
    if (!view) return;
    static_cast<QStackedLayout *>(m_stack->layout())->setCurrentWidget(view);
    m_omnibox->setText(view->url().toString());
    if (m_addressBarCtl) {
        m_addressBarCtl->setDisplayUrl(view->url().toString(), view->url().scheme() == "https");
    }
    setWindowTitle((view->title().isEmpty() ? "pocb" : view->title()) + " — pocb");
}

QWidget *BrowserWindow::buildTopbar(QWidget *parent) {
    ui::TopbarWidgets w = ui::buildTopbar(parent, m_theme);
    m_backBtn = w.back;
    m_fwdBtn = w.forward;
    m_reloadBtn = w.reload;
    m_newTabBtn = w.newTab;
    m_settingsBtn = w.settings;
    m_addressBar = w.addressBar;
    m_lockIcon = w.lockIcon;
    m_searchIcon = w.searchIcon;
    m_pillMenuBtn = w.pillMenuBtn;
    m_addrWrap = w.addrWrap;

    // Magnifier icon visible only when the field is empty.
    auto syncSearchIcon = [this] {
        if (!m_searchIcon || !m_addressBar) return;
        m_searchIcon->setVisible(m_addressBar->text().isEmpty());
    };
    syncSearchIcon();
    connect(m_addressBar, &QLineEdit::textChanged, this, [syncSearchIcon](const QString &) { syncSearchIcon(); });

    if (auto *pill = qobject_cast<ui::AddrPill *>(m_addrWrap)) {
        pill->setIdleColor(QColor(255, 255, 255, 14));
    }
    // Pop the pill when the user begins editing it.
    class FocusPopFilter : public QObject {
    public:
        FocusPopFilter(QWidget *pill, QObject *parent) : QObject(parent), m_pill(pill) {}
        bool eventFilter(QObject *o, QEvent *e) override {
            if (auto *ap = qobject_cast<ui::AddrPill *>(m_pill)) {
                if (e->type() == QEvent::FocusIn) ap->setPopped(true);
                else if (e->type() == QEvent::FocusOut) ap->setPopped(false);
            }
            return QObject::eventFilter(o, e);
        }
        QWidget *m_pill;
    };
    if (m_addressBar && m_addrWrap) {
        m_addressBar->installEventFilter(new FocusPopFilter(m_addrWrap, this));
    }

    if (m_pillMenuBtn) {
        connect(m_pillMenuBtn, &QToolButton::clicked, this, [this] {
            auto copyUrl = [this] {
                if (auto *v = currentView()) QApplication::clipboard()->setText(v->url().toString());
            };
            auto reload = [this] { if (auto *v = currentView()) v->reload(); };
            auto newTab = [this] {
                m_tabTree->newTab(QUrl("about:blank"));
                m_floatingOmnibox->showFor(m_stack, QString());
            };
            auto settings = [this] { showSettings(); };
            if (mac::showNativePageActionsMenu(m_pillMenuBtn, copyUrl, reload, newTab, settings)) return;

            QMenu menu(this);
            menu.addAction("Copy URL", this, copyUrl);
            menu.addAction("Reload", this, reload);
            menu.addSeparator();
            menu.addAction("New Tab", this, newTab);
            menu.addAction("Settings…", this, settings);
            const QPoint pos = m_pillMenuBtn->mapToGlobal(QPoint(0, m_pillMenuBtn->height()));
            menu.exec(pos);
        });
    }

    m_addressBarCtl = new AddressBarController(m_addressBar, m_lockIcon, m_theme, this);
    m_addressBarCtl->setSearchEngineUrl(m_searchEngine);
    connect(m_addressBarCtl, &AddressBarController::submitted, this, [this](const QString &text) {
        if (auto *view = currentView()) view->load(urlFromInput(text));
        if (auto *v = currentView()) v->setFocus();
    });
    connect(m_addressBarCtl, &AddressBarController::escapePressed, this, [this] {
        if (auto *v = currentView()) v->setFocus();
    });

    connect(m_backBtn,   &QToolButton::clicked, this, [this] { if (auto *v = currentView()) v->back(); });
    connect(m_fwdBtn,    &QToolButton::clicked, this, [this] { if (auto *v = currentView()) v->forward(); });
    connect(m_reloadBtn, &QToolButton::clicked, this, [this] { if (auto *v = currentView()) v->reload(); });
    connect(m_settingsBtn, &QToolButton::clicked, this, &BrowserWindow::showSettings);
    connect(m_newTabBtn, &QToolButton::clicked, this, [this] {
        m_tabTree->newTab(QUrl("about:blank"));
        if (auto *view = currentView()) {
            const QString bg = m_theme.background.name();
            view->loadHtml(QStringLiteral(
                "<!doctype html><html><head><meta charset=\"utf-8\">"
                "<style>html,body{margin:0;height:100%%;background:%1;}</style>"
                "</head><body></body></html>").arg(bg));
        }
        m_floatingOmnibox->showFor(m_stack, QString());
    });

    return w.bar;
}


QUrl BrowserWindow::urlFromInput(const QString &input) const {
    const QString trimmed = input.trimmed();
    if (trimmed.isEmpty()) return QUrl(m_homePage);
    QUrl url = QUrl::fromUserInput(trimmed);
    const bool looksLikeHost = trimmed.contains('.') || trimmed.startsWith("localhost") || trimmed.startsWith("http://") || trimmed.startsWith("https://");
    if (looksLikeHost && url.isValid()) return url;
    return QUrl(m_searchEngine.arg(QString::fromUtf8(QUrl::toPercentEncoding(trimmed))));
}

WebView *BrowserWindow::currentView() const {
    return m_tabTree ? m_tabTree->currentView() : nullptr;
}

void BrowserWindow::setupUi() {
    qApp->setStyleSheet(appStyleSheet(m_theme));
    auto *central = new QWidget(this);
    auto *root = new QVBoxLayout(central);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // No toolbar row — the web content extends all the way up under the
    // titlebar (NSWindowStyleMaskFullSizeContentView), and the traffic
    // lights float on top, repositioned by MacIntegration.

    m_omnibox = new QLineEdit(this);
    m_omnibox->hide();

    m_floatingOmnibox = new FloatingOmnibox(m_theme);
    m_floatingOmnibox->setSearchEngineUrl(m_searchEngine);
    connect(m_floatingOmnibox, &FloatingOmnibox::submitted, this, [this](const QString &text) {
        if (text.trimmed().isEmpty()) return;
        m_omnibox->setText(text);
        if (auto *view = currentView()) view->load(urlFromInput(text));
    });

    // The load progress is now painted inside the address pill (see
    // ui::AddrPill::setLoadProgress). m_progress remains as an off-screen
    // sink for any code that still pokes it.
    m_progress = new QProgressBar(this);
    m_progress->hide();

    m_splitter = new QSplitter(this);
    m_splitter->setOrientation(Qt::Horizontal);
    // The splitter handle IS the seam between sidebar and web content.
    // Both adjacent panels have zero inner padding on this edge so the
    // handle is the only thing between them — drag anywhere along it to
    // move the boundary.
    m_splitter->setHandleWidth(ui::metrics::SplitterHandleWidth);
    m_splitter->setChildrenCollapsible(true);
    m_splitter->setOpaqueResize(true);
    m_splitter->setAttribute(Qt::WA_TranslucentBackground);
    m_splitter->setStyleSheet(
        "QSplitter { background: transparent; }"
        "QSplitter::handle:horizontal { background: transparent; }");

    auto *sidebar = new QWidget(m_splitter);
    sidebar->setObjectName("Sidebar");
    sidebar->setStyleSheet("QWidget#Sidebar { background: transparent; }");
    sidebar->setAttribute(Qt::WA_TranslucentBackground);
    auto *sideLayout = new QVBoxLayout(sidebar);
    // Top inset clears the traffic-light band (unified-toolbar height ~52px
    // on Big Sur+; first sidebar row sits just below the buttons). Left/right
    // insets pad the selection highlight inward from the window edge so it
    // visually aligns with the traffic-light leading edge.
    sideLayout->setContentsMargins(ui::metrics::DockedSidebarLeftInset,
                                   ui::metrics::DockedSidebarTopInset,
                                   ui::metrics::DockedSidebarRightInset,
                                   ui::metrics::DockedSidebarBottomInset);
    sideLayout->setSpacing(0);

    const QDir cacheDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/favicons");
    cacheDir.mkpath(".");
    m_favicons = new FaviconService(cacheDir, this);

    sidebar->setMinimumWidth(ui::metrics::SidebarMinimumWidth);
    sidebar->setMaximumWidth(ui::metrics::SidebarMaximumWidth);

    auto *stackHost = new QWidget(m_splitter);
    stackHost->setObjectName("StackHost");
    stackHost->setStyleSheet("QWidget#StackHost { background: transparent; }");
    auto *hostLayout = new QVBoxLayout(stackHost);
    hostLayout->setContentsMargins(ui::metrics::stackHostMargins(/*sidebarVisible=*/true));
    hostLayout->setSpacing(0);

    // The web container holds the top toolbar AND the web view stack inside
    // a single rounded shape, so the toolbar lives "inside" the rounded card
    // pushing the page content down.
    m_webContainer = new QWidget(stackHost);
    m_webContainer->setObjectName("WebContainer");
    m_webContainer->setStyleSheet(
        "QWidget#WebContainer { background: #1a1a1a; }");
    auto *containerLayout = new QVBoxLayout(m_webContainer);
    containerLayout->setContentsMargins(0, 0, 0, 0);
    containerLayout->setSpacing(0);

    m_topbar = buildTopbar(m_webContainer);
    containerLayout->addWidget(m_topbar);

    // Fixed thin hairline between toolbar and the page.
    m_topSeparator = new QWidget(m_webContainer);
    m_topSeparator->setObjectName("WebTopSeparator");
    m_topSeparator->setFixedHeight(1);
    m_topSeparator->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_topSeparator->setStyleSheet(
        "QWidget#WebTopSeparator { background: rgba(255,255,255,0.08); }");
    containerLayout->addWidget(m_topSeparator);

    m_stack = new QWidget(m_webContainer);
    auto *stackLayout = new QStackedLayout(m_stack);
    stackLayout->setContentsMargins(0, 0, 0, 0);
    containerLayout->addWidget(m_stack, 1);

    // TabTree owns the sidebar list + WebView lifetime, but the QStackedLayout
    // host (`m_stack`) and the surrounding sidebar chrome stay here.
    m_tabTree = new TabTree(m_profiles, m_favicons, m_stack, m_theme, sidebar, this);
    m_tabTree->setHomePage(m_homePage);
    sideLayout->addWidget(m_tabTree->widget(), 1);

    hostLayout->addWidget(m_webContainer, 1);
    m_splitter->addWidget(sidebar);
    m_splitter->addWidget(stackHost);
    m_splitter->setStretchFactor(0, 0);
    m_splitter->setStretchFactor(1, 1);
    if (auto *handle = m_splitter->handle(1)) handle->setCursor(Qt::SplitHCursor);

    QSettings settings;
    const int savedSidebarWidth = settings.value("ui/sidebarWidth", ui::metrics::SidebarDefaultWidth).toInt();
    const int initialSidebar = qBound(sidebar->minimumWidth(), savedSidebarWidth, sidebar->maximumWidth());
    m_splitter->setSizes({initialSidebar, qMax(400, width() - initialSidebar)});
    // Helper: when the sidebar is hidden the web content reclaims its
    // normal 6 px breathing room on the left; when visible the seam is
    // flush against the splitter handle.
    auto applyStackHostInset = [hostLayout](bool sidebarVisible) {
        hostLayout->setContentsMargins(ui::metrics::stackHostMargins(sidebarVisible));
    };
    m_sidebar = new SidebarController(this, m_splitter, applyStackHostInset, this);
    m_sidebar->setSidebarContent(m_tabTree->widget(), sideLayout);

    m_addrInSidebar = QSettings().value("ui/addressBarInSidebar", false).toBool();
    if (m_addrInSidebar) {
        // Drop the toolbar entirely; the address pill + nav buttons live in
        // the sidebar instead.
        if (m_topbar) m_topbar->hide();
        if (m_topSeparator) m_topSeparator->hide();

        // Sidebar already has 52px top inset for the traffic-light band; we
        // re-use that band by placing nav buttons inside it (right-aligned),
        // so reset the top margin and own the spacing manually.
        // Right margin matches the web container's left inset (0 — the
        // splitter handle itself provides the visual gap), so the sidebar
        // doesn't end with extra dead space relative to the page edge.
        sideLayout->setContentsMargins(ui::metrics::DockedSidebarLeftInset,
                                       0,
                                       ui::metrics::DockedSidebarRightInset,
                                       ui::metrics::DockedSidebarBottomInset);

        m_sidebarHeader = new QWidget(sidebar);
        m_sidebarHeader->setObjectName("SidebarHeader");
        m_sidebarHeader->setStyleSheet("QWidget#SidebarHeader { background: transparent; }");
        auto *headerCol = new QVBoxLayout(m_sidebarHeader);
        headerCol->setContentsMargins(0, 0, 0, 6);
        headerCol->setSpacing(6);

        // Traffic-light row: 52px tall (matches the unified-toolbar band).
        // Traffic lights are painted by AppKit on the leading edge; nav
        // buttons sit on the trailing edge at the same vertical center.
        auto *navRow = new QWidget(m_sidebarHeader);
        navRow->setFixedHeight(ui::metrics::SidebarHeaderNavHeight);
        auto *navLayout = new QHBoxLayout(navRow);
        // Top inset positions buttons at the traffic-light vertical center
        // (~y=14 from window top); the leading spacer keeps a comfortable
        // gap between the lights and the back button on narrow sidebars.
        navLayout->setContentsMargins(0, ui::metrics::SidebarHeaderNavTopInset,
                                      ui::metrics::SidebarHeaderNavRightInset, 0);
        navLayout->setSpacing(4);
        navLayout->addSpacing(ui::metrics::SidebarHeaderTrafficLightClearance);
        navLayout->addStretch(1);
        for (QToolButton *btn : {m_backBtn, m_fwdBtn, m_reloadBtn}) {
            if (!btn) continue;
            btn->setParent(navRow);
            btn->setFixedSize(24, 24);
            btn->setIconSize(QSize(14, 14));
            navLayout->addWidget(btn, 0, Qt::AlignTop);
        }
        headerCol->addWidget(navRow);

        // Move the address pill itself into the sidebar header. It keeps
        // every property (controller, lock icon, search icon, menu btn,
        // load-progress fill) — only its parent changes. Match a tab row's
        // visual dimensions: ~26 px tall, 6 px horizontal inner padding,
        // same 6 px corner radius.
        if (m_addrWrap) {
            m_addrWrap->setParent(m_sidebarHeader);
            m_addrWrap->setFixedHeight(30);
            if (auto *pill = qobject_cast<ui::AddrPill *>(m_addrWrap)) {
                pill->setRadius(6);
            }
            if (auto *row = qobject_cast<QHBoxLayout *>(m_addrWrap->layout())) {
                row->setContentsMargins(6, 0, 6, 0);
                row->setSpacing(6);
                row->setSizeConstraint(QLayout::SetNoConstraint);
            }
            m_addrWrap->setMinimumHeight(30);
            m_addrWrap->setMaximumHeight(30);
            if (m_searchIcon) {
                m_searchIcon->setFixedSize(16, 16);
                m_searchIcon->setPixmap(mac::sfSymbolIcon("magnifyingglass", 13.0, m_theme.muted).pixmap(16, 16));
            }
            if (m_lockIcon) m_lockIcon->setFixedSize(16, 16);
            if (m_addressBar) {
                m_addressBar->setFixedHeight(22);
                m_addressBar->setContentsMargins(0, 0, 0, 0);
                m_addressBar->setTextMargins(0, 0, 0, 0);
            }
            headerCol->addWidget(m_addrWrap);
        }

        sideLayout->insertWidget(0, m_sidebarHeader);
    }

    connect(m_splitter, &QSplitter::splitterMoved, this, [this, sidebar](int pos, int) {
        if (!m_splitter) return;
        const int collapseThreshold = ui::metrics::sidebarCollapseThreshold(sidebar->minimumWidth());
        if (pos < collapseThreshold) {
            if (sidebar->isVisible()) m_sidebar->setHidden(true);
            return;
        }
        if (!sidebar->isVisible()) m_sidebar->setHidden(false);
        const auto sizes = m_splitter->sizes();
        if (!sizes.isEmpty() && sizes.first() >= sidebar->minimumWidth()) {
            QSettings().setValue("ui/sidebarWidth", sizes.first());
        }
    });

    root->addWidget(m_splitter, 1);
    setCentralWidget(central);

    central->setObjectName("CentralRoot");
    central->setStyleSheet("QWidget#CentralRoot { background: transparent; }");
    central->setAttribute(Qt::WA_TranslucentBackground);
    setContentsMargins(0, 0, 0, 0);

    if (auto *sb = statusBar()) sb->hide();
    setStatusBar(nullptr);

    connect(m_omnibox, &QLineEdit::returnPressed, this, &BrowserWindow::loadFromOmnibox);
    connect(m_tabTree, &TabTree::currentTabChanged, this, &BrowserWindow::updateForCurrentTab);
    connect(m_tabTree, &TabTree::loadProgress, this, [this](int progress) {
        if (auto *pill = qobject_cast<ui::AddrPill *>(m_addrWrap)) {
            pill->setLoadProgress(progress);
        }
    });
    connect(m_tabTree, &TabTree::themeColorChanged, this,
            &BrowserWindow::applyChromeForPageColor);
    connect(m_tabTree, &TabTree::contentMouseDown, this, [this] {
        if (m_addressBarCtl && m_addressBarCtl->isEditing()) m_addressBarCtl->cancelEditing();
    });
    connect(&m_profiles, &ProfileStore::currentProfileChanged, this, [this] {
        m_tabTree->rebuildForProfile();
    });
}

void BrowserWindow::setupActions() {
    auto openBlankTabWithOmnibox = [this] {
        m_tabTree->newTab(QUrl("about:blank"));
        if (auto *view = currentView()) {
            const QString bg = m_theme.background.name();
            view->loadHtml(QStringLiteral(
                "<!doctype html><html><head><meta charset=\"utf-8\">"
                "<style>html,body{margin:0;height:100%%;background:%1;}</style>"
                "</head><body></body></html>").arg(bg));
        }
        m_floatingOmnibox->showFor(m_stack, QString());
    };
    auto focusOmnibox = [this] {
        QString current;
        if (auto *view = currentView()) {
            const QUrl u = view->url();
            const QString s = u.toString();
            if (!s.isEmpty() && s != "about:blank" && !s.startsWith("data:")) current = s;
        }
        m_floatingOmnibox->showFor(m_stack, current);
    };
    auto toggleSidebar = [this] {
        auto *side = m_splitter->widget(0);
        if (!side) return;
        const bool nowVisible = !side->isVisible();
        m_sidebar->setHidden(!nowVisible);
        if (nowVisible) {
            QTimer::singleShot(0, this, [this, side] {
                const int saved = QSettings().value("ui/sidebarWidth", ui::metrics::SidebarDefaultWidth).toInt();
                const int target = qBound(side->minimumWidth(), saved, side->maximumWidth());
                const int total = m_splitter->size().width();
                m_splitter->setSizes({target, qMax(0, total - target - m_splitter->handleWidth())});
            });
        }
    };

    auto makeAction = [this](const QString &text, const QKeySequence &shortcut,
                             std::function<void()> slot,
                             QAction::MenuRole role = QAction::NoRole) {
        auto *a = new QAction(text, this);
        if (!shortcut.isEmpty()) a->setShortcut(shortcut);
        a->setMenuRole(role);
        if (slot) connect(a, &QAction::triggered, this, std::move(slot));
        return a;
    };

    // Edit menu items forward to the AppKit first responder so native
    // NSText fields (and WKWebView) handle them.
    auto fwd = [](const char *sel) {
        return [sel] { mac::sendStandardEditAction(sel); };
    };

    auto *mb = new QMenuBar(this);
    setMenuBar(mb);

    // ── App menu (auto: "pocb") ─────────────────────────────────────────
    auto *aboutAction = makeAction("About pocb", {}, [] { QApplication::aboutQt(); }, QAction::AboutRole);
    auto *prefsAction = makeAction("Settings…", QKeySequence(Qt::CTRL | Qt::Key_Comma),
                                   [this] { showSettings(); }, QAction::PreferencesRole);
    auto *quitAction  = makeAction("Quit pocb", QKeySequence(Qt::CTRL | Qt::Key_Q),
                                   [] { QApplication::quit(); }, QAction::QuitRole);

    // ── File ────────────────────────────────────────────────────────────
    auto *fileMenu = mb->addMenu("File");
    fileMenu->addAction(makeAction("New Tab", QKeySequence(Qt::CTRL | Qt::Key_T), openBlankTabWithOmnibox));
    fileMenu->addAction(makeAction("New Window", QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_N),
                                   [] { (new BrowserWindow())->show(); }));
    fileMenu->addSeparator();
    fileMenu->addAction(makeAction("Close Tab", QKeySequence(Qt::CTRL | Qt::Key_W),
                                   [this] { m_tabTree->closeCurrent(); }));
    fileMenu->addAction(makeAction("Close Window", QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_W),
                                   [this] { close(); }));
    fileMenu->addSeparator();
    fileMenu->addAction(makeAction("Open Location…", QKeySequence(Qt::CTRL | Qt::Key_L), focusOmnibox));

    // ── Edit ────────────────────────────────────────────────────────────
    auto *editMenu = mb->addMenu("Edit");
    editMenu->addAction(makeAction("Undo", QKeySequence(Qt::CTRL | Qt::Key_Z), fwd("undo:")));
    editMenu->addAction(makeAction("Redo", QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Z), fwd("redo:")));
    editMenu->addSeparator();
    editMenu->addAction(makeAction("Cut",   QKeySequence(Qt::CTRL | Qt::Key_X), fwd("cut:"),       QAction::TextHeuristicRole));
    editMenu->addAction(makeAction("Copy",  QKeySequence(Qt::CTRL | Qt::Key_C), fwd("copy:"),      QAction::TextHeuristicRole));
    editMenu->addAction(makeAction("Paste", QKeySequence(Qt::CTRL | Qt::Key_V), fwd("paste:"),     QAction::TextHeuristicRole));
    editMenu->addAction(makeAction("Select All", QKeySequence(Qt::CTRL | Qt::Key_A), fwd("selectAll:"), QAction::TextHeuristicRole));
    editMenu->addSeparator();
    auto copyCurrentUrl = [this] {
        if (auto *v = currentView()) {
            QApplication::clipboard()->setText(v->url().toString());
            showCopiedLinkPopup();
        }
    };
    auto *copyCurrentUrlAction = makeAction("Copy Current URL", QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_C),
                                            copyCurrentUrl);
    copyCurrentUrlAction->setShortcutContext(Qt::ApplicationShortcut);
    addAction(copyCurrentUrlAction);
    editMenu->addAction(copyCurrentUrlAction);
    auto *copyCurrentUrlShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_C), this);
    copyCurrentUrlShortcut->setContext(Qt::ApplicationShortcut);
    connect(copyCurrentUrlShortcut, &QShortcut::activated, this, copyCurrentUrl);

    // ── View ────────────────────────────────────────────────────────────
    auto *viewMenu = mb->addMenu("View");
    viewMenu->addAction(makeAction("Reload", QKeySequence(Qt::CTRL | Qt::Key_R),
                                   [this] { if (auto *v = currentView()) v->reload(); }));
    viewMenu->addAction(makeAction("Force Reload", QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_R),
                                   [this] { if (auto *v = currentView()) v->reload(); }));
    viewMenu->addSeparator();
    auto *toggleSidebarAction = makeAction("Toggle Sidebar", QKeySequence(Qt::CTRL | Qt::Key_B), toggleSidebar);
    toggleSidebarAction->setShortcutContext(Qt::ApplicationShortcut);
    addAction(toggleSidebarAction);
    viewMenu->addAction(toggleSidebarAction);
    viewMenu->addSeparator();
    viewMenu->addAction(makeAction("Enter Full Screen",
                                   QKeySequence(Qt::CTRL | Qt::META | Qt::Key_F),
                                   [this] {
                                       if (isFullScreen()) showNormal();
                                       else showFullScreen();
                                   }));

    // ── Tabs ────────────────────────────────────────────────────────────
    auto *tabsMenu = mb->addMenu("Tabs");
    tabsMenu->addAction(makeAction("New Tab", QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_T),
                                   openBlankTabWithOmnibox));
    tabsMenu->addAction(makeAction("Close Tab", QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_K),
                                   [this] { m_tabTree->closeCurrent(); }));
    tabsMenu->addSeparator();
    auto navTab = [this](int dir) {
        auto *tree = m_tabTree->widget();
        if (!tree) return;
        auto *cur = m_tabTree->currentItem();
        if (!cur) return;
        QTreeWidgetItem *next = (dir > 0) ? tree->itemBelow(cur) : tree->itemAbove(cur);
        if (!next) {
            const int n = tree->topLevelItemCount();
            if (n == 0) return;
            next = tree->topLevelItem(dir > 0 ? 0 : n - 1);
        }
        if (next) tree->setCurrentItem(next);
    };
    tabsMenu->addAction(makeAction("Select Next Tab",
                                   QKeySequence(Qt::CTRL | Qt::Key_Tab),
                                   [navTab] { navTab(+1); }));
    tabsMenu->addAction(makeAction("Select Previous Tab",
                                   QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Tab),
                                   [navTab] { navTab(-1); }));

    // ── Bookmarks (placeholders — bookmark store not yet implemented) ──
    auto *bookmarksMenu = mb->addMenu("Bookmarks");
    auto *bookmarkPage = makeAction("Bookmark This Page…", QKeySequence(Qt::CTRL | Qt::Key_D), nullptr);
    bookmarkPage->setEnabled(false);
    bookmarksMenu->addAction(bookmarkPage);
    auto *showBookmarks = makeAction("Show All Bookmarks",
                                     QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_B), nullptr);
    showBookmarks->setEnabled(false);
    bookmarksMenu->addAction(showBookmarks);

    // ── History ────────────────────────────────────────────────────────
    auto *historyMenu = mb->addMenu("History");
    historyMenu->addAction(makeAction("Back", QKeySequence(Qt::CTRL | Qt::Key_BracketLeft),
                                      [this] { if (auto *v = currentView()) v->back(); }));
    historyMenu->addAction(makeAction("Forward", QKeySequence(Qt::CTRL | Qt::Key_BracketRight),
                                      [this] { if (auto *v = currentView()) v->forward(); }));
    historyMenu->addSeparator();
    historyMenu->addAction(makeAction("Home", QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_H),
                                      [this] { if (auto *v = currentView()) v->load(QUrl(m_homePage)); }));
    historyMenu->addSeparator();
    auto *showHistory = makeAction("Show All History",
                                   QKeySequence(Qt::CTRL | Qt::Key_Y), nullptr);
    showHistory->setEnabled(false);
    historyMenu->addAction(showHistory);

    // ── Profiles ───────────────────────────────────────────────────────
    auto *profilesMenu = mb->addMenu("Profiles");
    auto rebuildProfilesMenu = [this, profilesMenu] {
        profilesMenu->clear();
        const QStringList list = m_profiles.profiles();
        for (const QString &name : list) {
            auto *a = profilesMenu->addAction(name);
            a->setCheckable(true);
            connect(a, &QAction::triggered, this, [this, name] { m_profiles.setCurrentProfile(name); });
        }
        if (!list.isEmpty()) profilesMenu->addSeparator();
        auto *manage = profilesMenu->addAction("Manage Profiles…");
        manage->setMenuRole(QAction::NoRole);
        connect(manage, &QAction::triggered, this, &BrowserWindow::showSettings);
    };
    rebuildProfilesMenu();
    connect(&m_profiles, &ProfileStore::currentProfileChanged, this, [rebuildProfilesMenu] {
        rebuildProfilesMenu();
    });

    // ── Window ─────────────────────────────────────────────────────────
    auto *windowMenu = mb->addMenu("Window");
    windowMenu->addAction(makeAction("Minimize", QKeySequence(Qt::CTRL | Qt::Key_M),
                                     [this] { showMinimized(); }));
    windowMenu->addAction(makeAction("Zoom", {}, [this] {
        if (isMaximized()) showNormal(); else showMaximized();
    }));
    windowMenu->addSeparator();
    windowMenu->addAction(makeAction("Bring All to Front", {}, [] {
        const auto windows = QApplication::topLevelWidgets();
        for (auto *w : windows) if (w->isWindow() && w->isVisible()) w->raise();
    }));

    // ── Help ───────────────────────────────────────────────────────────
    auto *helpMenu = mb->addMenu("Help");
    helpMenu->addAction(makeAction("pocb Help", {}, [] {
        QDesktopServices::openUrl(QUrl("https://github.com/plyght/pocb"));
    }));

    // App-menu items (Qt re-homes these via menuRole on macOS).
    helpMenu->addAction(aboutAction);
    fileMenu->addAction(prefsAction);
    fileMenu->addAction(quitAction);
}


bool BrowserWindow::eventFilter(QObject *obj, QEvent *ev) {
    return QMainWindow::eventFilter(obj, ev);
}

void BrowserWindow::applyChromeForPageColor(const QColor &pageColor) {
    if (!m_topbar) return;

    const bool hasColor = pageColor.isValid() && pageColor.alpha() >= 16;
    const QColor bg = hasColor ? pageColor : QColor(28, 28, 30, 235);

    // Skip the (relatively expensive) re-rasterise of 6 SF Symbols + 5
    // button stylesheet resets when nothing actually changed — common on
    // tab switch where the cached colour gets replayed first and then the
    // fresh sniff returns the identical value.
    if (bg == m_lastAppliedChrome) return;
    m_lastAppliedChrome = bg;

    // Decide a foreground tone that contrasts with `bg`. We use perceived
    // luminance (Rec. 601) to classify dark vs light pages.
    const int luma = (bg.red() * 299 + bg.green() * 587 + bg.blue() * 114) / 1000;
    const bool dark = luma < 140;
    const QColor fg = dark ? QColor(245, 245, 247) : QColor(28, 28, 30);

    // Hover shade: lighten dark pages, darken light ones — same logic for
    // toolbar buttons and the address bar wrap.
    QColor hover = bg;
    hover = dark ? hover.lighter(125) : hover.darker(108);
    hover.setAlpha(qMax(220, bg.alpha()));

    QColor pressed = bg;
    pressed = dark ? pressed.lighter(140) : pressed.darker(115);
    pressed.setAlpha(qMax(230, bg.alpha()));

    auto rgba = [](const QColor &c) {
        return QString("rgba(%1,%2,%3,%4)")
            .arg(c.red()).arg(c.green()).arg(c.blue()).arg(c.alpha());
    };

    if (auto *cb = qobject_cast<ui::ChromeBar *>(m_topbar)) {
        cb->setBackgroundColor(bg, /*animate=*/true);
    }

    // Re-render every SF Symbol in the new foreground tone.
    const double symPt = 14.0;
    auto reSymbol = [&](QToolButton *btn, const QString &name) {
        if (!btn) return;
        btn->setIcon(mac::sfSymbolIcon(name, symPt, fg));
    };
    reSymbol(m_backBtn,    "chevron.backward");
    reSymbol(m_fwdBtn,     "chevron.forward");
    reSymbol(m_reloadBtn,  "arrow.clockwise");
    reSymbol(m_newTabBtn,  "plus");
    reSymbol(m_settingsBtn,"gearshape");

    if (m_addressBarCtl) m_addressBarCtl->setIconColor(fg);

    const QString btnQss = QString(
        "QToolButton {"
        "  background: transparent;"
        "  border: none;"
        "  border-radius: 6px;"
        "  padding: 0px;"
        "}"
        "QToolButton:hover { background: %1; }"
        "QToolButton:pressed { background: %2; }")
        .arg(rgba(hover), rgba(pressed));
    for (QToolButton *btn : {m_backBtn, m_fwdBtn, m_reloadBtn, m_newTabBtn, m_settingsBtn}) {
        if (btn) btn->setStyleSheet(btnQss);
    }

    if (auto *pill = qobject_cast<ui::AddrPill *>(m_addrWrap)) {
        pill->setHoverColor(hover);
        QColor loadTint = fg;
        loadTint.setAlpha(220);
        pill->setLoadColor(loadTint);
    }

    if (m_addressBar) {
        m_addressBar->setStyleSheet(QString(
            "QLineEdit {"
            "  background: transparent;"
            "  border: none;"
            "  color: %1;"
            "  font-family: '%2';"
            "  font-size: %3px;"
            "  padding: 0px;"
            "}")
            .arg(fg.name(), m_theme.fontFamily, QString::number(m_theme.regularSize)));
    }
}

