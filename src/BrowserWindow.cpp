#include "BrowserWindow.hpp"


#include "FloatingOmnibox.hpp"
#include "MacIntegration.hpp"
#include "SettingsDialog.hpp"
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
    resize(1280, 820);
    // Force NSWindow creation so we can position the traffic lights before
    // the window is visible (no one-frame flash at the default position).
    winId();
    mac::integrateUnifiedToolbar(this, nullptr, /*compact=*/true);
    newTab(QUrl(m_homePage));
}

void BrowserWindow::moveEvent(QMoveEvent *e) {
    QMainWindow::moveEvent(e);
    if (m_sidebarHoverZone && m_sidebarHoverZone->isVisible()) positionSidebarHoverZone();
}

void BrowserWindow::resizeEvent(QResizeEvent *e) {
    QMainWindow::resizeEvent(e);
    if (m_sidebarHoverZone && m_sidebarHoverZone->isVisible()) positionSidebarHoverZone();
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

void BrowserWindow::newTab(const QUrl &url, bool background, QTreeWidgetItem *parentItem) {
    auto *view = new WebView(m_profiles.currentProfile(), m_stack);

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

void BrowserWindow::adoptChildView(WebView *child, QTreeWidgetItem *parentItem, bool background) {
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

void BrowserWindow::closeCurrentTab() {
    auto *item = currentItem();
    if (!item) return;
    auto *view = m_views.take(item);
    if (view) view->deleteLater();
    delete item;
    if (m_tabs->topLevelItemCount() == 0) newTab(QUrl(m_homePage));
    updateForCurrentTab();
}

void BrowserWindow::loadFromOmnibox() {
    if (auto *view = currentView()) view->load(urlFromInput(m_omnibox->text()));
}

void BrowserWindow::showSettings() {
    SettingsDialog dialog(m_profiles, this);
    connect(&dialog, &SettingsDialog::homePageChanged, this, [this](const QString &url) { m_homePage = url; });
    connect(&dialog, &SettingsDialog::searchEngineChanged, this, [this](const QString &url) {
        if (!url.contains("%1")) return;
        m_searchEngine = url;
        if (m_floatingOmnibox) m_floatingOmnibox->setSearchEngineUrl(url);
    });
    dialog.exec();
}

void BrowserWindow::updateForCurrentTab() {
    auto *view = currentView();
    if (!view) return;
    static_cast<QStackedLayout *>(m_stack->layout())->setCurrentWidget(view);
    m_omnibox->setText(view->url().toString());
    if (m_addressBar && !m_addrEditing) {
        const QString s = view->url().toString();
        m_addressBar->setText(s == "about:blank" ? QString() : s);
        if (m_lockIcon) m_lockIcon->setVisible(view->url().scheme() == "https");
    }
    setWindowTitle((view->title().isEmpty() ? "pocb" : view->title()) + " — pocb");
}

QWidget *BrowserWindow::buildTopbar(QWidget *parent) {
    auto *bar = new QWidget(parent);
    bar->setObjectName("WebTopbar");
    bar->setFixedHeight(40);
    bar->setStyleSheet(QString(
        "QWidget#WebTopbar {"
        "  background: rgba(28,28,30,0.92);"
        "  border-bottom: 1px solid %1;"
        "}")
        .arg(m_theme.borderSoft.name()));

    auto *row = new QHBoxLayout(bar);
    row->setContentsMargins(8, 4, 8, 4);
    row->setSpacing(2);

    const QColor iconColor = m_theme.foreground;
    const double symPt = 14.0;

    auto makeBtn = [&](const QString &symbol, const QString &tip) {
        auto *btn = new QToolButton(bar);
        btn->setAutoRaise(true);
        btn->setFocusPolicy(Qt::NoFocus);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setIconSize(QSize(16, 16));
        btn->setFixedSize(28, 28);
        btn->setToolTip(tip);
        btn->setIcon(mac::sfSymbolIcon(symbol, symPt, iconColor));
        btn->setStyleSheet(QString(
            "QToolButton {"
            "  background: transparent;"
            "  border: none;"
            "  border-radius: 6px;"
            "  padding: 0px;"
            "}"
            "QToolButton:hover { background: %1; }"
            "QToolButton:pressed { background: %2; }"
            "QToolButton:disabled { color: %3; }")
            .arg(m_theme.hover.name(),
                 m_theme.raised.name(),
                 m_theme.muted.name()));
        return btn;
    };

    m_backBtn    = makeBtn("chevron.backward", "Back");
    m_fwdBtn     = makeBtn("chevron.forward",  "Forward");
    m_reloadBtn  = makeBtn("arrow.clockwise",  "Reload  (⌘R)");
    m_newTabBtn  = makeBtn("plus",             "New Tab  (⌘T)");
    m_settingsBtn= makeBtn("gearshape",        "Settings");

    row->addWidget(m_backBtn);
    row->addWidget(m_fwdBtn);
    row->addWidget(m_reloadBtn);
    row->addSpacing(6);

    // Address bar — clickable read-only display that opens the floating
    // omnibox for editing.
    auto *addrWrap = new QWidget(bar);
    addrWrap->setObjectName("AddressWrap");
    addrWrap->setFixedHeight(28);
    addrWrap->setStyleSheet(QString(
        "QWidget#AddressWrap {"
        "  background: %1;"
        "  border: 1px solid %2;"
        "  border-radius: 7px;"
        "}"
        "QWidget#AddressWrap:hover {"
        "  background: %3;"
        "}")
        .arg(m_theme.raised.name(),
             m_theme.borderSoft.name(),
             m_theme.hover.name()));
    auto *addrRow = new QHBoxLayout(addrWrap);
    addrRow->setContentsMargins(8, 0, 8, 0);
    addrRow->setSpacing(6);

    m_lockIcon = new QLabel(addrWrap);
    m_lockIcon->setFixedSize(14, 14);
    {
        QIcon lock = mac::sfSymbolIcon("lock.fill", 11.0, m_theme.muted);
        m_lockIcon->setPixmap(lock.pixmap(14, 14));
    }
    addrRow->addWidget(m_lockIcon);

    m_addressBar = new QLineEdit(addrWrap);
    m_addressBar->setFrame(false);
    m_addressBar->setPlaceholderText("Search or enter address");
    m_addressBar->setClearButtonEnabled(false);
    m_addressBar->setStyleSheet(QString(
        "QLineEdit {"
        "  background: transparent;"
        "  border: none;"
        "  color: %1;"
        "  font-family: '%2';"
        "  font-size: %3px;"
        "  padding: 0px;"
        "}")
        .arg(m_theme.foreground.name(),
             m_theme.fontFamily,
             QString::number(m_theme.regularSize)));
    addrRow->addWidget(m_addressBar, 1);

    // Wire address bar focus + edits + key handling.
    m_addressBar->installEventFilter(this);

    m_addrNet = new QNetworkAccessManager(this);
    m_addrDebounce = new QTimer(this);
    m_addrDebounce->setSingleShot(true);
    m_addrDebounce->setInterval(120);
    connect(m_addrDebounce, &QTimer::timeout, this, &BrowserWindow::addrFetchSuggestions);
    connect(m_addressBar, &QLineEdit::textEdited, this, [this](const QString &t) {
        m_addrPendingQuery = t.trimmed();
        if (m_addrPendingQuery.isEmpty()) { addrHidePopup(); return; }
        m_addrDebounce->start();
    });
    connect(m_addressBar, &QLineEdit::returnPressed, this, &BrowserWindow::addrCommit);

    row->addWidget(addrWrap, 1);
    row->addSpacing(6);
    row->addWidget(m_newTabBtn);
    row->addWidget(m_settingsBtn);

    connect(m_backBtn,   &QToolButton::clicked, this, [this] { if (auto *v = currentView()) v->back(); });
    connect(m_fwdBtn,    &QToolButton::clicked, this, [this] { if (auto *v = currentView()) v->forward(); });
    connect(m_reloadBtn, &QToolButton::clicked, this, [this] { if (auto *v = currentView()) v->reload(); });
    connect(m_settingsBtn, &QToolButton::clicked, this, &BrowserWindow::showSettings);
    connect(m_newTabBtn, &QToolButton::clicked, this, [this] {
        newTab(QUrl("about:blank"));
        if (auto *view = currentView()) {
            const QString bg = m_theme.background.name();
            view->loadHtml(QStringLiteral(
                "<!doctype html><html><head><meta charset=\"utf-8\">"
                "<style>html,body{margin:0;height:100%%;background:%1;}</style>"
                "</head><body></body></html>").arg(bg));
        }
        m_floatingOmnibox->showFor(m_stack, QString());
    });

    return bar;
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
    return m_views.value(currentItem(), nullptr);
}

QTreeWidgetItem *BrowserWindow::currentItem() const {
    return m_tabs->currentItem();
}

void BrowserWindow::selectItem(QTreeWidgetItem *item) {
    if (!item) return;
    m_tabs->setCurrentItem(item);
    updateForCurrentTab();
}

void BrowserWindow::wireView(WebView *view, QTreeWidgetItem *item) {
    connect(view, &WebView::titleChanged, this, [item](const QString &title) {
        item->setText(0, title.isEmpty() ? "New tab" : title);
    });
    connect(view, &WebView::urlChanged, this, [this, view, item](const QUrl &url) {
        if (view == currentView()) m_omnibox->setText(url.toString());
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
        m_progress->setValue(progress);
        m_progress->setVisible(progress > 0 && progress < 100);
    });
    connect(view, &WebView::loadFinished, this, [this](bool) { updateForCurrentTab(); });
    connect(view, &WebView::newTabRequested, this, [this, item](WebView *child, bool background) {
        adoptChildView(child, item, background);
    });
    connect(view, &WebView::closeRequested, this, [this, view] {
        for (auto it = m_views.begin(); it != m_views.end(); ++it) {
            if (it.value() == view) {
                m_tabs->setCurrentItem(it.key());
                closeCurrentTab();
                return;
            }
        }
    });
}

void BrowserWindow::rebuildProfilePages() {
    const QUrl activeUrl = currentView() ? currentView()->url() : QUrl(m_homePage);
    m_views.clear();
    m_tabs->clear();
    const auto children = m_stack->findChildren<WebView *>();
    for (auto *child : children) child->deleteLater();
    newTab(activeUrl);
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

    m_progress = new QProgressBar(this);
    m_progress->setMaximumHeight(2);
    m_progress->setTextVisible(false);
    m_progress->setVisible(false);
    root->addWidget(m_progress);

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

    m_tabs = new QTreeWidget(sidebar);
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
    sideLayout->addWidget(m_tabs, 1);

    const QDir cacheDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/favicons");
    cacheDir.mkpath(".");
    m_favicons = new FaviconService(cacheDir, this);
    connect(m_favicons, &FaviconService::faviconReady, this,
            [this](const QString &domain, const QPixmap &pm) {
                if (pm.isNull()) return;
                const QIcon icon(pm);
                for (auto it = m_views.constBegin(); it != m_views.constEnd(); ++it) {
                    const QString itemDomain = it.value()->url().host();
                    if (itemDomain == domain) it.key()->setIcon(0, icon);
                }
            });

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
    // Hover-enter on the toolbar (a pure-Qt widget the cursor must cross to
    // reach the browser content) is what triggers auto-collapse of a
    // hover-opened sidebar. The QWebEngineView below it is a native NSView
    // that doesn't pump Qt mouse events, so listening for hover events there
    // wouldn't fire reliably.
    m_topbar->setAttribute(Qt::WA_Hover, true);
    m_topbar->installEventFilter(this);
    m_webContainer->setAttribute(Qt::WA_Hover, true);
    m_webContainer->installEventFilter(this);

    m_stack = new QWidget(m_webContainer);
    auto *stackLayout = new QStackedLayout(m_stack);
    stackLayout->setContentsMargins(0, 0, 0, 0);
    containerLayout->addWidget(m_stack, 1);

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
    m_setStackHostInset = applyStackHostInset;

    connect(m_splitter, &QSplitter::splitterMoved, this, [this, sidebar](int pos, int) {
        if (!m_splitter) return;
        // Hysteresis: collapse below 70% of minWidth, but require a full
        // minWidth drag to re-open. Without this gap the splitter flaps
        // between hidden/shown when the user drags the seam shut, which then
        // re-opens spuriously as the cursor moves.
        const int collapseThreshold = sidebar->minimumWidth() * 7 / 10;
        const int reopenThreshold = sidebar->minimumWidth();
        if (pos < collapseThreshold) {
            if (sidebar->isVisible()) setSidebarHidden(true);
            return;
        }
        if (!sidebar->isVisible()) {
            if (pos < reopenThreshold) return;
            setSidebarHidden(false);
        }
        const auto sizes = m_splitter->sizes();
        if (!sizes.isEmpty() && sizes.first() >= sidebar->minimumWidth()) {
            QSettings().setValue("ui/sidebarWidth", sizes.first());
        }
    });

    root->addWidget(m_splitter, 1);
    setCentralWidget(central);

    // Edge hover zone — when the sidebar is collapsed, a fully invisible
    // top-level overlay along the window's left edge expands the sidebar on
    // hover. Made a top-level Qt::Tool window (parented to this) so it sits
    // above the QWebEngineView's native NSView; if it were a Qt child of the
    // central widget, the web view's metal layer would steal the mouse.
    m_sidebarHoverZone = new QWidget(this, Qt::FramelessWindowHint | Qt::Tool | Qt::NoDropShadowWindowHint);
    m_sidebarHoverZone->setObjectName("SidebarHoverZone");
    m_sidebarHoverZone->setAttribute(Qt::WA_TranslucentBackground);
    m_sidebarHoverZone->setAttribute(Qt::WA_ShowWithoutActivating);
    m_sidebarHoverZone->setAttribute(Qt::WA_AlwaysStackOnTop);
    m_sidebarHoverZone->setFocusPolicy(Qt::NoFocus);
    m_sidebarHoverZone->setStyleSheet("background: transparent;");
    m_sidebarHoverZone->hide();
    m_sidebarHoverZone->installEventFilter(this);
    central->setObjectName("CentralRoot");
    central->setStyleSheet("QWidget#CentralRoot { background: transparent; }");
    central->setAttribute(Qt::WA_TranslucentBackground);
    setContentsMargins(0, 0, 0, 0);

    if (auto *sb = statusBar()) sb->hide();
    setStatusBar(nullptr);

    connect(m_omnibox, &QLineEdit::returnPressed, this, &BrowserWindow::loadFromOmnibox);
    connect(m_tabs, &QTreeWidget::currentItemChanged, this, &BrowserWindow::updateForCurrentTab);
    connect(&m_profiles, &ProfileStore::currentProfileChanged, this, [this] {
        rebuildProfilePages();
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
        newTab(QUrl("about:blank"));  // skip the homepage path; we override below.
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
    connect(closeTab, &QAction::triggered, this, &BrowserWindow::closeCurrentTab);
    connect(settings, &QAction::triggered, this, &BrowserWindow::showSettings);

    new QShortcut(QKeySequence::AddTab, this, openBlankTabWithOmnibox);
    new QShortcut(QKeySequence::Close, this, [this] { closeCurrentTab(); });
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
        setSidebarHidden(!nowVisible);
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

void BrowserWindow::setSidebarHidden(bool hidden) {
    if (!m_splitter) return;
    auto *side = m_splitter->widget(0);
    if (!side) return;
    side->setVisible(!hidden);
    mac::setTrafficLightsHidden(this, hidden);
    if (m_setStackHostInset) m_setStackHostInset(!hidden);
    if (m_sidebarHoverZone) {
        if (hidden) {
            positionSidebarHoverZone();
            m_sidebarHoverZone->show();
            m_sidebarHoverZone->raise();
            // If the cursor is already over the strip at the moment the
            // sidebar collapses (typical when dragging it shut), require
            // the user to move out and back in before re-expanding —
            // otherwise the zone would instantly re-fire and ping-pong.
            const QPoint local = m_sidebarHoverZone->mapFromGlobal(QCursor::pos());
            m_sidebarHoverArmed = !m_sidebarHoverZone->rect().contains(local);
        } else {
            m_sidebarHoverZone->hide();
            m_sidebarHoverArmed = true;
        }
    }
    m_sidebarOpenedByHover = false;
}

void BrowserWindow::positionSidebarHoverZone() {
    if (!m_sidebarHoverZone) return;
    constexpr int kZoneWidth = 16;
    // Anchor against the window's content area, skipping the toolbar so we
    // don't hijack the traffic-light region or topbar interactions.
    const QPoint topLeft = mapToGlobal(QPoint(0, 0));
    const int top = topLeft.y() + 28;
    const int height = qMax(0, this->height() - 28);
    m_sidebarHoverZone->setGeometry(topLeft.x(), top, kZoneWidth, height);
}

static void ensureSidebarAnim(BrowserWindow *self, QVariantAnimation *&anim, QSplitter *splitter) {
    if (anim) return;
    anim = new QVariantAnimation(self);
    anim->setDuration(180);
    anim->setEasingCurve(QEasingCurve::OutCubic);
    QObject::connect(anim, &QVariantAnimation::valueChanged, self,
                     [splitter](const QVariant &v) {
                         if (!splitter) return;
                         const int w = v.toInt();
                         const int total = splitter->size().width();
                         const int handle = splitter->handleWidth();
                         splitter->setSizes({w, qMax(0, total - w - handle)});
                     });
}

void BrowserWindow::expandSidebarAnimated() {
    if (!m_splitter) return;
    auto *side = m_splitter->widget(0);
    if (!side) return;
    if (m_sidebarAnim && m_sidebarAnim->state() == QAbstractAnimation::Running && !m_sidebarCollapsing) return;

    m_sidebarCollapsing = false;
    setSidebarHidden(false);
    if (m_sidebarHoverZone) m_sidebarHoverZone->hide();

    const int saved = QSettings().value("ui/sidebarWidth", 240).toInt();
    const int target = qBound(side->minimumWidth(), saved, side->maximumWidth());
    const int total = m_splitter->size().width();
    const int handle = m_splitter->handleWidth();
    const int currentW = m_splitter->sizes().value(0, 0);
    m_splitter->setSizes({currentW, qMax(0, total - currentW - handle)});

    ensureSidebarAnim(this, m_sidebarAnim, m_splitter);
    m_sidebarAnim->stop();
    m_sidebarAnim->setStartValue(currentW);
    m_sidebarAnim->setEndValue(target);
    m_sidebarAnim->start();

    // Auto-collapse is driven by a HoverLeave event filter installed on the
    // sidebar widget itself in setupUi — fully event-driven, no polling.
    m_sidebarOpenedByHover = true;
}

void BrowserWindow::collapseSidebarAnimated() {
    if (!m_splitter) return;
    auto *side = m_splitter->widget(0);
    if (!side || !side->isVisible()) return;
    if (m_sidebarCollapsing) return;

    m_sidebarOpenedByHover = false;

    const int currentW = m_splitter->sizes().value(0, side->width());
    if (currentW > 0 && side->minimumWidth() > 0) {
        QSettings().setValue("ui/sidebarWidth", qMax(currentW, side->minimumWidth()));
    }

    ensureSidebarAnim(this, m_sidebarAnim, m_splitter);
    m_sidebarCollapsing = true;
    m_sidebarAnim->stop();
    // Reconnect finished once — guarded so we only attach a single handler.
    static bool finishedHooked = false;
    if (!finishedHooked) {
        finishedHooked = true;
        connect(m_sidebarAnim, &QVariantAnimation::finished, this, [this] {
            if (!m_sidebarCollapsing) return;
            m_sidebarCollapsing = false;
            setSidebarHidden(true);
        });
    }
    m_sidebarAnim->setStartValue(currentW);
    m_sidebarAnim->setEndValue(0);
    m_sidebarAnim->start();
}

bool BrowserWindow::eventFilter(QObject *obj, QEvent *ev) {
    if (obj == m_addressBar) {
        if (ev->type() == QEvent::FocusIn) {
            addrBeginEditing();
        } else if (ev->type() == QEvent::FocusOut) {
            // Only restore the URL if focus left the line edit and didn't go
            // to the suggestion popup itself.
            QWidget *now = QApplication::focusWidget();
            if (!m_addrPopup || (now != m_addrPopup && now != m_addrPopup->viewport())) {
                addrEndEditing(/*restoreUrl=*/true);
            }
        } else if (ev->type() == QEvent::KeyPress) {
            auto *ke = static_cast<QKeyEvent *>(ev);
            if (ke->key() == Qt::Key_Escape) {
                addrEndEditing(/*restoreUrl=*/true);
                if (auto *v = currentView()) v->setFocus();
                return true;
            }
            if ((ke->key() == Qt::Key_Down || ke->key() == Qt::Key_Up) &&
                m_addrPopup && m_addrPopup->isVisible() && m_addrPopup->count() > 0) {
                int row = m_addrPopup->currentRow();
                if (ke->key() == Qt::Key_Down)
                    row = (row + 1) % m_addrPopup->count();
                else
                    row = (row <= 0) ? m_addrPopup->count() - 1 : row - 1;
                m_addrPopup->setCurrentRow(row);
                m_addressBar->setText(m_addrPopup->item(row)->text());
                return true;
            }
        }
    }
    if (m_sidebarHoverZone && obj == m_sidebarHoverZone) {
        if (ev->type() == QEvent::Leave) {
            m_sidebarHoverArmed = true;
        } else if (ev->type() == QEvent::Enter) {
            if (m_sidebarHoverArmed) expandSidebarAnimated();
        }
    }
    if (m_sidebarOpenedByHover &&
        (obj == m_topbar || obj == m_webContainer) &&
        (ev->type() == QEvent::HoverEnter || ev->type() == QEvent::Enter)) {
        collapseSidebarAnimated();
    }
    return QMainWindow::eventFilter(obj, ev);
}

namespace {
struct AddrEngine {
    QString host;
    QString path;
    QList<QPair<QString, QString>> extraParams;
    QString queryParam = "q";
};
AddrEngine engineForHost(const QString &host) {
    const QString h = host.toLower();
    if (h.contains("duckduckgo")) return {"duckduckgo.com", "/ac/", {{"type","list"}}, "q"};
    if (h.contains("google"))     return {"suggestqueries.google.com", "/complete/search", {{"client","firefox"}}, "q"};
    if (h.contains("brave"))      return {"search.brave.com", "/api/suggest", {{"source","web"}}, "q"};
    if (h.contains("bing"))       return {"www.bing.com", "/osjson.aspx", {}, "query"};
    if (h.contains("ecosia"))     return {"ac.ecosia.org", "/", {{"type","list"}}, "q"};
    if (h.contains("startpage"))  return {"www.startpage.com", "/suggestions",
                                          {{"format","opensearch"}, {"segment","startpage.macos"}}, "q"};
    return {"duckduckgo.com", "/ac/", {{"type","list"}}, "q"};
}
}  // namespace

void BrowserWindow::addrBeginEditing() {
    if (m_addrEditing) return;
    m_addrEditing = true;
    m_addrSavedUrl = m_addressBar->text();
    QTimer::singleShot(0, this, [this] {
        if (m_addressBar->hasFocus()) m_addressBar->selectAll();
    });
}

void BrowserWindow::addrEndEditing(bool restoreUrl) {
    addrHidePopup();
    m_addrEditing = false;
    if (restoreUrl && m_addressBar) {
        if (auto *view = currentView()) {
            const QString s = view->url().toString();
            m_addressBar->setText(s == "about:blank" ? QString() : s);
        }
    }
}

void BrowserWindow::addrCommit() {
    QString text;
    if (m_addrPopup && m_addrPopup->isVisible() && m_addrPopup->currentItem()) {
        text = m_addrPopup->currentItem()->text();
    } else {
        text = m_addressBar->text();
    }
    addrHidePopup();
    if (text.trimmed().isEmpty()) return;
    if (auto *view = currentView()) view->load(urlFromInput(text));
    if (auto *v = currentView()) v->setFocus();
    m_addrEditing = false;
}

void BrowserWindow::addrFetchSuggestions() {
    if (m_addrPendingQuery.isEmpty()) return;
    if (m_addrInflight) {
        m_addrInflight->abort();
        m_addrInflight->deleteLater();
        m_addrInflight = nullptr;
    }
    const QString engineHost = QUrl(m_searchEngine).host();
    const auto eng = engineForHost(engineHost);
    QUrl url;
    url.setScheme("https");
    url.setHost(eng.host);
    url.setPath(eng.path);
    QUrlQuery q;
    for (const auto &p : eng.extraParams) q.addQueryItem(p.first, p.second);
    q.addQueryItem(eng.queryParam, m_addrPendingQuery);
    url.setQuery(q);
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  "Mozilla/5.0 (Macintosh; Intel Mac OS X 14_0) AppleWebKit/605 (KHTML, like Gecko) pocb");
    req.setRawHeader("Accept", "application/json,text/javascript,*/*;q=0.1");
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply *reply = m_addrNet->get(req);
    reply->setProperty("query", m_addrPendingQuery);
    m_addrInflight = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (reply != m_addrInflight) return;
        m_addrInflight = nullptr;
        if (reply->error() != QNetworkReply::NoError) return;
        if (!m_addressBar || reply->property("query").toString() != m_addressBar->text().trimmed()) return;

        const QByteArray body = reply->readAll();
        const auto doc = QJsonDocument::fromJson(body);
        QStringList items;
        if (doc.isArray() && doc.array().size() >= 2 && doc.array().at(1).isArray()) {
            for (const auto &v : doc.array().at(1).toArray()) {
                const QString s = v.toString();
                if (!s.isEmpty()) items << s;
            }
        } else if (doc.isArray()) {
            for (const auto &v : doc.array()) {
                if (v.isString()) items << v.toString();
                else if (v.isObject()) {
                    const auto o = v.toObject();
                    const QString s = o.value("phrase").toString(o.value("suggestion").toString());
                    if (!s.isEmpty()) items << s;
                }
            }
        }
        if (items.size() > 8) items = items.mid(0, 8);
        addrPopulatePopup(items);
    });
}

void BrowserWindow::addrPopulatePopup(const QStringList &items) {
    if (!m_addrPopup) {
        m_addrPopup = new QListWidget(nullptr);
        m_addrPopup->setWindowFlags(Qt::Popup | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
        m_addrPopup->setAttribute(Qt::WA_TranslucentBackground);
        m_addrPopup->setObjectName("AddrPopup");
        m_addrPopup->setFrameShape(QFrame::NoFrame);
        m_addrPopup->setFocusPolicy(Qt::NoFocus);
        m_addrPopup->setUniformItemSizes(true);
        m_addrPopup->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_addrPopup->setAttribute(Qt::WA_ShowWithoutActivating, true);
        QFont f = m_addrPopup->font();
        f.setFamily(m_theme.fontFamily);
        f.setPointSize(m_theme.regularSize);
        m_addrPopup->setFont(f);
        m_addrPopup->setStyleSheet(QString(
            "QListWidget#AddrPopup {"
            "  background: %1;"
            "  border: 1px solid %2;"
            "  border-radius: 10px;"
            "  padding: 6px 0px;"
            "  color: %3;"
            "}"
            "QListWidget#AddrPopup::item {"
            "  padding: 8px 12px;"
            "  margin: 1px 6px;"
            "  border-radius: 7px;"
            "  color: %3;"
            "}"
            "QListWidget#AddrPopup::item:selected { background: %4; }"
            "QListWidget#AddrPopup::item:hover:!selected { background: %5; }"
            "QListWidget#AddrPopup QScrollBar:vertical { background: transparent; width: 6px; margin: 4px 2px; }"
            "QListWidget#AddrPopup QScrollBar::handle:vertical { background: %2; border-radius: 3px; min-height: 24px; }"
            "QListWidget#AddrPopup QScrollBar::add-line, QListWidget#AddrPopup QScrollBar::sub-line { height:0; width:0; }")
            .arg(m_theme.panel.name(),
                 m_theme.border.name(),
                 m_theme.foreground.name(),
                 m_theme.raised.name(),
                 m_theme.hover.name()));
        connect(m_addrPopup, &QListWidget::itemClicked, this, [this](QListWidgetItem *it) {
            if (!it) return;
            m_addressBar->setText(it->text());
            addrCommit();
        });
    }
    m_addrPopup->clear();
    if (items.isEmpty()) { addrHidePopup(); return; }
    for (const auto &s : items) {
        auto *it = new QListWidgetItem(s, m_addrPopup);
        it->setSizeHint(QSize(0, 32));
    }
    m_addrPopup->setCurrentRow(-1);
    addrShowPopup();
}

void BrowserWindow::addrPosition() {
    if (!m_addrPopup || !m_addressBar) return;
    QWidget *anchor = m_addressBar->parentWidget() ? m_addressBar->parentWidget() : m_addressBar;
    const QPoint topLeft = anchor->mapToGlobal(QPoint(0, anchor->height() + 6));
    const int width = anchor->width();
    int rows = qMin(m_addrPopup->count(), 8);
    const int height = rows * 32 + 12;
    m_addrPopup->setGeometry(topLeft.x(), topLeft.y(), width, height);
}

void BrowserWindow::addrShowPopup() {
    if (!m_addrPopup) return;
    addrPosition();
    if (!m_addrPopup->isVisible()) m_addrPopup->show();
    m_addrPopup->raise();
}

void BrowserWindow::addrHidePopup() {
    if (m_addrPopup && m_addrPopup->isVisible()) m_addrPopup->hide();
}
