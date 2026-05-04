#include "LittleWindow.hpp"

#include "AddressBarController.hpp"
#include "LayoutMetrics.hpp"
#include "MacIntegration.hpp"
#include "Topbar.hpp"
#include "WebView.hpp"

#include <QApplication>
#include <QCloseEvent>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QMouseEvent>
#include <QLineEdit>
#include <QProgressBar>
#include <QShortcut>
#include <QStackedLayout>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QUrlQuery>
#include <QVBoxLayout>
#include <QWindow>

LittleWindow::LittleWindow(const QUrl &url, QWidget *parent, bool restorePreviousAppOnClose) : QMainWindow(parent), m_restorePreviousAppOnClose(restorePreviousAppOnClose) {
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_NoSystemBackground);
    setAutoFillBackground(false);
#if defined(Q_OS_MACOS) && QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
    setAttribute(Qt::WA_ContentsMarginsRespectsSafeArea, false);
    setWindowFlags(windowFlags() | Qt::ExpandedClientAreaHint | Qt::NoTitleBarBackgroundHint);
#endif
    setupUi(url);
    resize(900, 620);
    winId();
    mac::integrateUnifiedToolbar(this, nullptr, true);
    mac::setTrafficLightOffset(this, -11.0, -3.5);
}

void LittleWindow::showEvent(QShowEvent *e) {
    QMainWindow::showEvent(e);
    mac::integrateUnifiedToolbar(this, nullptr, true);
    mac::setTrafficLightOffset(this, -11.0, -3.5);
    mac::enableWindowVibrancy(this, mac::VibrancyMaterial::Sidebar);
    mac::enableHighRefreshRate(this);
    QTimer::singleShot(0, this, [this] {
        if (centralWidget()) mac::roundWidgetCorners(centralWidget(), ui::metrics::WebContainerRadius, false);
    });
}

void LittleWindow::closeEvent(QCloseEvent *e) {
    QMainWindow::closeEvent(e);
}

bool LittleWindow::eventFilter(QObject *obj, QEvent *event) {
    if (obj == m_toolbarDragHandle) {
        if (event->type() == QEvent::MouseButtonPress) {
            auto *mouse = static_cast<QMouseEvent *>(event);
            if (mouse->button() == Qt::LeftButton) {
                if (windowHandle() && windowHandle()->startSystemMove()) {
                    m_toolbarDragging = false;
                    return true;
                }
                m_toolbarDragging = true;
                m_toolbarDragOffset = mouse->globalPosition().toPoint() - frameGeometry().topLeft();
                return true;
            }
        } else if (event->type() == QEvent::MouseMove && m_toolbarDragging) {
            auto *mouse = static_cast<QMouseEvent *>(event);
            if (mouse->buttons() & Qt::LeftButton) {
                move(mouse->globalPosition().toPoint() - m_toolbarDragOffset);
                return true;
            }
        } else if (event->type() == QEvent::MouseButtonRelease) {
            m_toolbarDragging = false;
        }
    }
    return QMainWindow::eventFilter(obj, event);
}

QUrl LittleWindow::urlFromInput(const QString &input) const {
    const QString trimmed = input.trimmed();
    QUrl url = QUrl::fromUserInput(trimmed);
    if (url.isValid() && !url.scheme().isEmpty() && (trimmed.contains('.') || trimmed.contains(':'))) return url;
    return QUrl(m_searchEngine.arg(QUrl::toPercentEncoding(trimmed)));
}

void LittleWindow::setupUi(const QUrl &url) {
    auto *container = new QWidget(this);
    container->setObjectName("LittleContainer");
    container->setStyleSheet(QString("QWidget#LittleContainer { background: rgba(26, 26, 26, 180); border: 1px solid rgba(255,255,255,0.10); border-radius: %1px; }").arg(ui::metrics::WebContainerRadius));
    auto *layout = new QVBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    ui::TopbarWidgets topbar = ui::buildTopbar(container, m_theme);
    m_backBtn = topbar.back;
    m_fwdBtn = topbar.forward;
    m_reloadBtn = topbar.reload;
    m_addressBar = topbar.addressBar;
    m_lockIcon = topbar.lockIcon;
    m_searchIcon = topbar.searchIcon;
    if (topbar.sidebar) topbar.sidebar->hide();
    if (topbar.newTab) topbar.newTab->hide();
    if (topbar.settings) topbar.settings->hide();
    m_toolbarDragHandle = topbar.bar;
    m_toolbarDragHandle->installEventFilter(this);
    if (auto *row = qobject_cast<QHBoxLayout *>(topbar.bar->layout())) row->insertSpacing(0, 64);
    layout->addWidget(topbar.bar);

    auto *separator = new QWidget(container);
    separator->setFixedHeight(1);
    separator->setStyleSheet("background: rgba(255,255,255,0.08);");
    layout->addWidget(separator);

    m_view = new WebView(m_profiles.currentProfile(), container);
    layout->addWidget(m_view, 1);
    setCentralWidget(container);

    m_addressBarCtl = new AddressBarController(m_addressBar, m_lockIcon, m_theme, this);
    connect(m_addressBar, &QLineEdit::returnPressed, this, [this] { m_view->load(urlFromInput(m_addressBar->text())); });
    connect(m_addressBar, &QLineEdit::textChanged, this, [this] { if (m_searchIcon) m_searchIcon->setVisible(m_addressBar->text().isEmpty()); });
    connect(m_backBtn, &QToolButton::clicked, m_view, &WebView::back);
    connect(m_fwdBtn, &QToolButton::clicked, m_view, &WebView::forward);
    connect(m_reloadBtn, &QToolButton::clicked, this, [this] { m_view->isLoading() ? m_view->stop() : m_view->reload(); });
    connect(m_view, &WebView::urlChanged, this, [this] { updateChrome(); });
    connect(m_view, &WebView::titleChanged, this, [this] { updateChrome(); });
    connect(m_view, &WebView::navigationStateChanged, this, [this] { updateChrome(); });
    connect(m_view, &WebView::loadProgress, this, [this](int) { updateChrome(); });
    auto *locationShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_L), this);
    connect(locationShortcut, &QShortcut::activated, this, &LittleWindow::focusLocation);
    auto *closeShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_W), this);
    connect(closeShortcut, &QShortcut::activated, this, &LittleWindow::close);
    m_view->load(url.isValid() ? url : QUrl("about:blank"));
    updateChrome();
}

void LittleWindow::updateChrome() {
    if (!m_view) return;
    const QUrl url = m_view->url();
    if (m_addressBarCtl) m_addressBarCtl->setDisplayUrl(url.toString(), url.scheme() == "https");
    if (m_backBtn) m_backBtn->setEnabled(m_view->canGoBack());
    if (m_fwdBtn) m_fwdBtn->setEnabled(m_view->canGoForward());
    if (m_reloadBtn) m_reloadBtn->setIcon(mac::sfSymbolIcon(m_view->isLoading() ? "xmark" : "arrow.clockwise", 14.0, m_theme.foreground));
    setWindowTitle((m_view->title().isEmpty() ? QStringLiteral("Little pocb") : m_view->title()) + QStringLiteral(" — pocb"));
}

void LittleWindow::focusLocation() {
    if (!m_addressBar) return;
    m_addressBar->setFocus(Qt::ShortcutFocusReason);
    m_addressBar->selectAll();
}
