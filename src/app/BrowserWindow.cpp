#include "BrowserWindow.hpp"


#include "AddressBarController.hpp"
#include "FloatingOmnibox.hpp"
#include "ChromeWidgets.hpp"
#include "MacIntegration.hpp"
#include "SettingsDialog.hpp"
#include "SidebarController.hpp"
#include "TabTree.hpp"
#include "Topbar.hpp"
#include "WebView.hpp"

#include <QAction>
#include <QApplication>
#include <QDesktopServices>
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
            resize(1280, 820);
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
            mac::roundWidgetCorners(m_webContainer, 10.0, /*recurseDescendants=*/false);
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
    m_addrWrap = w.addrWrap;

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
    m_splitter->setHandleWidth(8);
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
    sideLayout->setContentsMargins(10, 52, 0, 10);
    sideLayout->setSpacing(0);

    const QDir cacheDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/favicons");
    cacheDir.mkpath(".");
    m_favicons = new FaviconService(cacheDir, this);

    sidebar->setMinimumWidth(160);
    sidebar->setMaximumWidth(520);

    auto *stackHost = new QWidget(m_splitter);
    stackHost->setObjectName("StackHost");
    stackHost->setStyleSheet("QWidget#StackHost { background: transparent; }");
    auto *hostLayout = new QVBoxLayout(stackHost);
    hostLayout->setContentsMargins(0, 6, 6, 6);
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
    const int savedSidebarWidth = settings.value("ui/sidebarWidth", 240).toInt();
    const int initialSidebar = qBound(sidebar->minimumWidth(), savedSidebarWidth, sidebar->maximumWidth());
    m_splitter->setSizes({initialSidebar, qMax(400, width() - initialSidebar)});
    // Helper: when the sidebar is hidden the web content reclaims its
    // normal 6 px breathing room on the left; when visible the seam is
    // flush against the splitter handle.
    auto applyStackHostInset = [hostLayout](bool sidebarVisible) {
        hostLayout->setContentsMargins(sidebarVisible ? 0 : 6, 6, 6, 6);
    };
    m_sidebar = new SidebarController(this, m_splitter, applyStackHostInset, this);
    m_sidebar->setSidebarContent(m_tabTree->widget(), sideLayout);

    connect(m_splitter, &QSplitter::splitterMoved, this, [this, sidebar](int pos, int) {
        if (!m_splitter) return;
        const int collapseThreshold = sidebar->minimumWidth() * 7 / 10;
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
    connect(&m_profiles, &ProfileStore::currentProfileChanged, this, [this] {
        m_tabTree->rebuildForProfile();
    });
}

void BrowserWindow::setupActions() {
    auto *back        = new QAction(QStringLiteral(u"\u2039"), this);   // ‹
    auto *forward     = new QAction(QStringLiteral(u"\u203A"), this);   // ›
    auto *reload      = new QAction(QStringLiteral(u"\u21BB"), this);   // ↻
    auto *newTabAction= new QAction(QStringLiteral(u"+"), this);
    auto *closeTab    = new QAction("Close Tab", this);
    auto *settings    = new QAction(QStringLiteral(u"\u2699"), this);   // ⚙

    back->setToolTip("Back");
    forward->setToolTip("Forward");
    reload->setToolTip("Reload  (⌘R)");
    newTabAction->setToolTip("New Tab  (⌘T)");
    settings->setToolTip("Settings");

    back->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_BracketLeft));
    forward->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_BracketRight));

    auto *mb = new QMenuBar(nullptr);
    mb->addMenu("File")->addAction(newTabAction);
    mb->addMenu("Settings")->addAction(settings);

    connect(back, &QAction::triggered, this, [this] { if (auto *view = currentView()) view->back(); });
    connect(forward, &QAction::triggered, this, [this] { if (auto *view = currentView()) view->forward(); });
    connect(reload, &QAction::triggered, this, [this] { if (auto *view = currentView()) view->reload(); });
    auto openBlankTabWithOmnibox = [this] {
        m_tabTree->newTab(QUrl("about:blank"));  // skip the homepage path; we override below.
        if (auto *view = currentView()) {
            const QString bg = m_theme.background.name();
            view->loadHtml(QStringLiteral(
                "<!doctype html><html><head><meta charset=\"utf-8\">"
                "<style>html,body{margin:0;height:100%%;background:%1;}</style>"
                "</head><body></body></html>").arg(bg));
        }
        m_floatingOmnibox->showFor(m_stack, QString());
    };
    connect(newTabAction, &QAction::triggered, this, openBlankTabWithOmnibox);
    connect(closeTab, &QAction::triggered, this, [this] { m_tabTree->closeCurrent(); });
    connect(settings, &QAction::triggered, this, &BrowserWindow::showSettings);

    new QShortcut(QKeySequence::AddTab, this, openBlankTabWithOmnibox);
    new QShortcut(QKeySequence::Close, this, [this] { m_tabTree->closeCurrent(); });
    new QShortcut(QKeySequence::Refresh, this, [this] { if (auto *view = currentView()) view->reload(); });
    new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_L), this, [this] {
        QString current;
        if (auto *view = currentView()) {
            const QUrl u = view->url();
            const QString s = u.toString();
            if (!s.isEmpty() && s != "about:blank" && !s.startsWith("data:")) current = s;
        }
        m_floatingOmnibox->showFor(m_stack, current);
    });
    auto toggleSidebar = [this] {
        auto *side = m_splitter->widget(0);
        if (!side) return;
        const bool nowVisible = !side->isVisible();
        m_sidebar->setHidden(!nowVisible);
        if (nowVisible) {
            // Drag-collapse leaves the sidebar with size 0 in the splitter;
            // restore the persisted (or default) width on re-open. Defer to
            // the next event loop turn so the splitter has re-included the
            // widget after setVisible(true) before we resize it.
            QTimer::singleShot(0, this, [this, side] {
                const int saved = QSettings().value("ui/sidebarWidth", 240).toInt();
                const int target = qBound(side->minimumWidth(), saved, side->maximumWidth());
                const int total = m_splitter->size().width();
                m_splitter->setSizes({target, qMax(0, total - target - m_splitter->handleWidth())});
            });
        }
    };
    auto *toggleSidebarAction = new QAction("Toggle Sidebar", this);
    toggleSidebarAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_B));
    toggleSidebarAction->setShortcutContext(Qt::ApplicationShortcut);
    toggleSidebarAction->setMenuRole(QAction::NoRole);
    connect(toggleSidebarAction, &QAction::triggered, this, toggleSidebar);
    addAction(toggleSidebarAction);
    mb->addMenu("View")->addAction(toggleSidebarAction);
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

