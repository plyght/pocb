#include "BrowserWindow.hpp"


#include "FloatingOmnibox.hpp"
#include "MacIntegration.hpp"
#include "SettingsDialog.hpp"
#include "WebView.hpp"

#include <QAction>
#include <QApplication>
#include <QDesktopServices>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QProgressBar>
#include <QDir>
#include <QShortcut>
#include <QTimer>
#include <QSettings>
#include <QSplitter>
#include <QStandardPaths>
#include <QStackedLayout>
#include <QStatusBar>
#include <QStyle>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
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

void BrowserWindow::showEvent(QShowEvent *e) {
    QMainWindow::showEvent(e);
    mac::integrateUnifiedToolbar(this, nullptr, /*compact=*/true);
    mac::enableWindowVibrancy(this, mac::VibrancyMaterial::Sidebar);
    mac::enableHighRefreshRate(this);
    // Round the web-content stack on the next event loop turn (after the
    // first QWebEngineView NSView exists).
    QTimer::singleShot(0, this, [this] {
        if (m_stack) mac::roundWidgetCorners(m_stack, 10.0);
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
    setWindowTitle((view->title().isEmpty() ? "pocb" : view->title()) + " — pocb");
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
    m_stack = new QWidget(stackHost);
    auto *stackLayout = new QStackedLayout(m_stack);
    stackLayout->setContentsMargins(0, 0, 0, 0);
    hostLayout->addWidget(m_stack, 1);
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

    connect(m_splitter, &QSplitter::splitterMoved, this, [this, sidebar, applyStackHostInset](int pos, int) {
        if (!m_splitter) return;
        const int collapseThreshold = sidebar->minimumWidth() * 7 / 10;
        if (pos < collapseThreshold) {
            if (sidebar->isVisible()) {
                sidebar->setVisible(false);
                mac::setTrafficLightsHidden(this, true);
                applyStackHostInset(false);
            }
            return;
        }
        if (!sidebar->isVisible()) {
            sidebar->setVisible(true);
            mac::setTrafficLightsHidden(this, false);
            applyStackHostInset(true);
        }
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
        side->setVisible(nowVisible);
        mac::setTrafficLightsHidden(this, !nowVisible);
        if (m_setStackHostInset) m_setStackHostInset(nowVisible);
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
