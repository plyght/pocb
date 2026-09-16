#include "BrowserWindow.hpp"


#include "AddressBarController.hpp"
#include "FloatingOmnibox.hpp"
#include "ChromeWidgets.hpp"
#include "ChromeExtensionManager.hpp"
#include "LayoutMetrics.hpp"
#include "LittleWindow.hpp"
#include "MacIntegration.hpp"
#include "DefaultBrowser.hpp"
#include "DownloadManager.hpp"
#include "DownloadsPopover.hpp"
#include "NativeProfilePopover.hpp"
#include "PasswordImport.hpp"
#include "PasswordManager.hpp"
#include "PasswordPrompt.hpp"
#include "NativeSettingsWindow.hpp"
#include "SidebarController.hpp"
#include "SidebarWidgets.hpp"
#include "TabTree.hpp"
#include "ToastWidget.hpp"
#include "ToolbarCollapse.hpp"
#include "Topbar.hpp"
#include "WebView.hpp"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QDebug>
#include <QClipboard>
#include <QDesktopServices>
#include <QMenu>
#include <QEvent>
#include <QFocusEvent>
#include <QHBoxLayout>
#include <QIcon>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPalette>
#include <QPainter>
#include <QPainterPath>
#include <QFrame>
#include <QFileDialog>
#include <QGraphicsOpacityEffect>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMenuBar>
#include <QMouseEvent>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProgressBar>
#include <QPropertyAnimation>
#include <QPointer>
#include <QPixmap>
#include <QDir>
#include <QElapsedTimer>
#include <QShortcut>
#include <QShortcutEvent>
#include <QTimer>
#include <QUrlQuery>
#include <QSettings>
#include <QSplitter>
#include <QSplitterHandle>
#include <QStandardPaths>
#include <QStackedLayout>
#include <QStatusBar>
#include <QStyle>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QVariantAnimation>
#include <QWheelEvent>
#include <QEasingCurve>
#include <QGuiApplication>
#include <QScreen>
#include <QWindow>
#include <QEvent>
#include <QVBoxLayout>

#include <vector>

namespace {

QEasingCurve responsiveEaseOut() {
    QEasingCurve curve(QEasingCurve::BezierSpline);
    curve.addCubicBezierSegment(QPointF(0.23, 1.0), QPointF(0.32, 1.0), QPointF(1.0, 1.0));
    return curve;
}

bool isBlankTabUrl(const QUrl &url) {
    return url.isEmpty() || url.toString() == QStringLiteral("about:blank") || url.toString().startsWith(QStringLiteral("data:text/html"));
}

QUrl alternateNavUrlFor(const QString &text) {
    const QString trimmed = text.trimmed().toLower();
    if (trimmed.size() < 2 || trimmed.contains(QChar::Space) || trimmed.contains('/') || trimmed.contains(':') || trimmed.contains('.')) return QUrl();
    for (const QChar ch : trimmed) {
        if (!(ch.isLetterOrNumber() || ch == '-')) return QUrl();
    }
    if (trimmed.startsWith('-') || trimmed.endsWith('-')) return QUrl();
    return QUrl(QStringLiteral("http://www.%1.com/").arg(trimmed));
}

QColor disabledToolbarColor(const QColor &foreground) {
    QColor color = foreground;
    const int luma = (foreground.red() * 299 + foreground.green() * 587 + foreground.blue() * 114) / 1000;
    color.setAlpha(95);
    return color;
}

// Main-frame-only scroll observer. rAF-throttled so at most one message per
// frame reaches the native side, and it never posts when nothing moved.
const char kScrollObserverScript[] = R"JS(
(function () {
  if (window.__pocbScrollObserverInstalled) return;
  window.__pocbScrollObserverInstalled = true;
  var lastY = window.scrollY || 0;
  var pending = false;
  function flush() {
    pending = false;
    var y = window.scrollY || document.documentElement.scrollTop || 0;
    var dy = y - lastY;
    if (dy === 0) return;
    lastY = y;
    try {
      window.webkit.messageHandlers.pocb.postMessage({
        name: "scroll",
        body: { y: y, dy: dy, atTop: y <= 0, direction: dy > 0 ? "down" : "up" }
      });
    } catch (e) {}
  }
  window.addEventListener("scroll", function () {
    if (pending) return;
    pending = true;
    window.requestAnimationFrame(flush);
  }, { passive: true });
})();
)JS";

constexpr int kToolbarCollapseThresholdPx = 24;
constexpr int kToolbarCollapseMs = 180;
// A single-frame jump this large is scroll restoration / an anchor jump,
// not a user scroll; it must not collapse the toolbar.
constexpr double kToolbarProgrammaticJumpPx = 400.0;
constexpr int kProfileSwipeAxisLockPx = 6;
constexpr double kProfileSwipeCommitFraction = 0.30;
constexpr double kProfileSwipeFlickVelocity = 650.0;  // px/s
constexpr int kProfileSwipeMinFlickTravel = 18;
constexpr int kProfileSwipeSettleMinMs = 140;
constexpr int kProfileSwipeSettleMaxMs = 260;
constexpr int kProfileSwipeVelocityWindowMs = 110;

// iOS-style rubber band: asymptotically approaches `limit`.
int rubberBand(int raw, int limit) {
    if (raw == 0 || limit <= 0) return 0;
    const double x = qAbs(raw);
    const double d = limit;
    const double out = (1.0 - 1.0 / (0.55 * x / d + 1.0)) * d;
    return raw < 0 ? -qRound(out) : qRound(out);
}

qint64 nowMs() {
    static QElapsedTimer timer;
    if (!timer.isValid()) timer.start();
    return timer.elapsed();
}

void setButtonSymbolSmooth(QToolButton *button, const QString &symbol, double pointSize, const QColor &color) {
    if (!button) return;
    const QString previousSymbol = button->property("sfSymbolName").toString();
    const QColor previousColor = button->property("sfSymbolColor").value<QColor>();
    if (previousSymbol == symbol && previousColor == color) return;
    button->setProperty("sfSymbolName", symbol);
    button->setProperty("sfSymbolColor", color);
    button->setIcon(mac::sfSymbolIcon(symbol, pointSize, color));
    if (auto *effect = qobject_cast<QGraphicsOpacityEffect *>(button->graphicsEffect())) {
        effect->setOpacity(1.0);
    }
}

class PillMenuHoverFilter final : public QObject {
public:
    PillMenuHoverFilter(QWidget *pill, QToolButton *button, QObject *parent)
        : QObject(parent), m_pill(pill), m_button(button) {}

    bool eventFilter(QObject *obj, QEvent *event) override {
        if ((obj != m_pill && obj != m_button) || !m_pill || !m_button) return QObject::eventFilter(obj, event);
        if (event->type() == QEvent::Enter) {
            m_button->show();
        } else if (event->type() == QEvent::Leave) {
            QTimer::singleShot(0, this, [this] {
                if (!m_pill || !m_button) return;
                const QPoint global = QCursor::pos();
                const bool overPill = m_pill->rect().contains(m_pill->mapFromGlobal(global));
                const bool overButton = m_button->rect().contains(m_button->mapFromGlobal(global));
                if (!overPill && !overButton) m_button->hide();
            });
        }
        return QObject::eventFilter(obj, event);
    }

private:
    QWidget *m_pill = nullptr;
    QToolButton *m_button = nullptr;
};

class SplitPaneHandle final : public QSplitterHandle {
public:
    SplitPaneHandle(Qt::Orientation orientation, QSplitter *parent, const Theme &theme)
        : QSplitterHandle(orientation, parent), m_theme(theme) {
        setAttribute(Qt::WA_TranslucentBackground);
        setAutoFillBackground(false);
    }

protected:
    void paintEvent(QPaintEvent *) override {}

private:
    Theme m_theme;
};

class SplitPaneSplitter final : public QSplitter {
public:
    SplitPaneSplitter(Qt::Orientation orientation, const Theme &theme, QWidget *parent)
        : QSplitter(orientation, parent), m_theme(theme) {}

protected:
    QSplitterHandle *createHandle() override {
        return new SplitPaneHandle(orientation(), this, m_theme);
    }

private:
    Theme m_theme;
};

class TabSwitcherPopup final : public QWidget {
public:
    TabSwitcherPopup(const Theme &theme, QWidget *parent)
        : QWidget(parent, Qt::Tool | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint), m_theme(theme) {
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_ShowWithoutActivating);
        setFocusPolicy(Qt::NoFocus);
    }

    void setTabs(const QList<WebView *> &tabs) {
        m_tabs = tabs;
        m_thumbnails.clear();
        const QSize thumbSize(184, 104);
        for (auto *tab : m_tabs) m_thumbnails.append(tab ? tab->snapshot(thumbSize) : QPixmap());
        resize(sizeHint());
        update();
    }

    void setCurrentIndex(int index) {
        m_index = qBound(0, index, qMax(0, m_tabs.size() - 1));
        update();
    }

    QSize sizeHint() const override {
        const int count = qMin(7, qMax(1, m_tabs.size()));
        return QSize(count * 210 + 28, 178);
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        QColor fill = m_theme.background;
        fill.setAlpha(218);
        QColor stroke = m_theme.border;
        stroke.setAlpha(110);
        painter.setPen(QPen(stroke, 1));
        painter.setBrush(fill);
        painter.drawRoundedRect(rect().adjusted(0, 0, -1, -1), 20, 20);

        const int count = qMin(7, m_tabs.size());
        const int cardW = 194;
        const int cardH = 142;
        const int gap = 16;
        int x = (width() - count * cardW - (count - 1) * gap) / 2;
        const int y = 18;
        for (int i = 0; i < count; ++i) {
            const bool active = i == m_index;
            QRect card(x, y, cardW, cardH);
            QColor cardFill = m_theme.foreground;
            cardFill.setAlpha(active ? 34 : 16);
            QColor cardStroke = m_theme.foreground;
            cardStroke.setAlpha(active ? 105 : 32);
            painter.setPen(QPen(cardStroke, active ? 1.4 : 1.0));
            painter.setBrush(cardFill);
            painter.drawRoundedRect(card, 14, 14);

            QRect shotRect = card.adjusted(8, 8, -8, -42);
            painter.setClipPath([shotRect] {
                QPainterPath path;
                path.addRoundedRect(shotRect, 10, 10);
                return path;
            }());
            if (i < m_thumbnails.size() && !m_thumbnails.at(i).isNull()) {
                painter.drawPixmap(shotRect, m_thumbnails.at(i));
            } else {
                QColor empty = m_theme.foreground;
                empty.setAlpha(18);
                painter.fillRect(shotRect, empty);
            }
            painter.setClipping(false);

            if (auto *tab = m_tabs.at(i)) {
                QRect iconRect(card.left() + 12, card.bottom() - 29, 16, 16);
                const QIcon icon = tab->windowIcon();
                if (!icon.isNull()) icon.paint(&painter, iconRect);
                QFont titleFont = font();
                titleFont.setPointSizeF(11.5);
                titleFont.setWeight(active ? QFont::DemiBold : QFont::Normal);
                painter.setFont(titleFont);
                QColor text = m_theme.foreground;
                text.setAlpha(active ? 235 : 176);
                painter.setPen(text);
                QString title = tab->title().trimmed();
                if (title.isEmpty()) title = QStringLiteral("New tab");
                painter.drawText(QRect(card.left() + 34, card.bottom() - 32, card.width() - 46, 22), Qt::AlignVCenter | Qt::AlignLeft, painter.fontMetrics().elidedText(title, Qt::ElideRight, card.width() - 48));
            }
            x += cardW + gap;
        }
    }

private:
    Theme m_theme;
    QList<WebView *> m_tabs;
    QList<QPixmap> m_thumbnails;
    int m_index = 0;
};

}  // namespace

BrowserWindow::BrowserWindow(QWidget *parent) : QMainWindow(parent) {
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_NoSystemBackground);
    setAutoFillBackground(false);
#if defined(Q_OS_MACOS) && QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
    // Qt 6.9+ flags: extend the client area under the titlebar and stop Qt
    // from reserving safe-area margins for the titlebar in the central
    // widget. Without these, QMainWindow leaves a band of empty space at the
    // top of the window even with NSWindowStyleMaskFullSizeContentView set
    // (regression since Qt 6.4 — see QTBUG-134797).
    setAttribute(Qt::WA_ContentsMarginsRespectsSafeArea, false);
    setWindowFlags(windowFlags() | Qt::ExpandedClientAreaHint | Qt::NoTitleBarBackgroundHint);
#endif
    ChromeExtensionManager::setBrowserWindow(this);
    // Registered before any WKWebView exists so every tab gets the observer.
    static bool scrollObserverRegistered = false;
    if (!scrollObserverRegistered) {
        scrollObserverRegistered = true;
        WebView::registerUserScript(QString::fromLatin1(kScrollObserverScript), /*mainFrameOnly=*/true);
    }
    m_collapseToolbarOnScroll = QSettings().value("ui/collapseToolbarOnScroll", true).toBool();
    setupUi();
    setupActions();
    setupIntegrations();
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
    if (m_tabTree) m_tabTree->restoreSession(restoredSessionForProfile(m_profiles.currentName()));
}

void BrowserWindow::setupIntegrations() {
    m_downloadsPopover = new DownloadsPopover(m_theme, this);
    auto *downloads = DownloadManager::instance();
    connect(this, &BrowserWindow::downloadsRequested, this, [this] {
        if (m_downloadsPopover->isVisible()) m_downloadsPopover->hidePopover();
        else m_downloadsPopover->showAnchoredTo(m_downloadsBtn);
    });
    connect(downloads, &DownloadManager::activeCountChanged, this, [this](int active, double progress) {
        showDownloadsBadge(active, progress);
    });
    connect(downloads, &DownloadManager::itemAdded, this, [this](const QString &) {
        if (!isActiveWindow() || !m_downloadsBtn) return;
        if (!m_downloadsPopover->isVisible()) m_downloadsPopover->showAnchoredTo(m_downloadsBtn);
    });
    connect(downloads, &DownloadManager::itemFinished, this, [this, downloads](const QString &id, bool ok) {
        if (m_downloadsPopover && m_downloadsPopover->isVisible()) return;
        DownloadItem item;
        if (!downloads->item(id, &item)) return;
        if (ok) {
            m_lastFinishedDownload = id;
            showToast(QStringLiteral("%1 downloaded").arg(item.fileName), humanSize(item.total > 0 ? item.total : item.received),
                      DownloadManager::fileIcon(item.path, 32));
        } else if (item.state == DownloadItem::Failed) {
            showToast(QStringLiteral("Download failed"), item.fileName, mac::sfSymbolIcon("exclamationmark.triangle.fill", 16.0, m_theme.foreground));
        }
    });
    connect(this, &BrowserWindow::toastClicked, this, [this, downloads] {
        if (!m_lastFinishedDownload.isEmpty()) downloads->revealInFinder(m_lastFinishedDownload);
    });

    auto *passwords = PasswordManager::instance();
    connect(passwords, &PasswordManager::autofillAvailable, this, [this, passwords](WebView *view, const QStringList &accounts) {
        if (view != currentView() || !m_addrWrap) return;
        if (PasswordPrompt *cur = PasswordPrompt::current(); cur && cur->kind() == PasswordPrompt::Kind::Save) return;
        if (auto *p = PasswordPrompt::showSaved(m_addrWrap, view->url().host(), accounts, m_theme))
            connect(p, &PasswordPrompt::useSavedChosen, passwords, [passwords, view](const QString &user) { passwords->fill(view, user); });
    });
    connect(passwords, &PasswordManager::generateAvailable, this, [this, passwords](WebView *view, const QString &password) {
        if (view != currentView() || !m_addrWrap) return;
        if (PasswordPrompt *cur = PasswordPrompt::current(); cur && cur->kind() == PasswordPrompt::Kind::Generate && cur->site() == view->url().host()) return;
        if (auto *p = PasswordPrompt::showGenerate(m_addrWrap, view->url().host(), password, m_theme))
            connect(p, &PasswordPrompt::useGeneratedChosen, passwords, [passwords, view](const QString &) { passwords->generateAndFill(view); });
    });
    connect(passwords, &PasswordManager::saveOffered, this, [this, passwords](WebView *view, const QString &user, const QString &password) {
        if (view != currentView() || !m_addrWrap) return;
        const QUrl site = view->url();
        if (auto *p = PasswordPrompt::showSave(m_addrWrap, site.host(), user, m_theme)) {
            connect(p, &PasswordPrompt::saveChosen, passwords, [passwords, site, user, password] { passwords->save(site, user, password); });
            connect(p, &PasswordPrompt::neverForSiteChosen, passwords, [passwords, site] { passwords->setNeverSave(site, true); });
        }
    });
    connect(passwords, &PasswordManager::saveFinished, this, [this](const QUrl &site, const QString &user, bool ok, const QString &error) {
        if (!isActiveWindow()) return;
        if (ok) showToast(QStringLiteral("Saved to Apple Passwords"), QStringLiteral("%1 · %2").arg(user, site.host()), mac::sfSymbolIcon("key.fill", 16.0, m_theme.foreground));
        else showToast(QStringLiteral("Couldn't save password"), error, mac::sfSymbolIcon("exclamationmark.triangle.fill", 16.0, m_theme.foreground));
    });
}

void BrowserWindow::moveEvent(QMoveEvent *e) {
    QMainWindow::moveEvent(e);
    if (m_sidebar) {
        if (m_sidebar->hoverZoneVisible()) m_sidebar->positionHoverZone();
        if (m_sidebar->floatingVisible()) m_sidebar->positionFloating();
    }
    positionToolbarGrabber();
    positionToast();
}

void BrowserWindow::resizeEvent(QResizeEvent *e) {
    QMainWindow::resizeEvent(e);
    if (m_sidebar) {
        if (m_sidebar->hoverZoneVisible()) m_sidebar->positionHoverZone();
        if (m_sidebar->floatingVisible()) m_sidebar->positionFloating();
    }
    syncAddressPillGlass();
    positionToolbarGrabber();
    positionToast();
}

void BrowserWindow::syncAddressPillGlass() {
    auto *pill = qobject_cast<ui::AddrPill *>(m_addrWrap);
    if (!pill || !pill->glassMode() || !isVisible()) return;
    // The glass NSViews are not clipped by the collapsing host, so they only
    // show while the toolbar row is fully expanded.
    const bool expanded = !m_toolbarHost || m_toolbarHost->progress() > 0.0;
    if (expanded && pill->isVisible() && m_toolbarRowAvailable) {
        if (m_toolbarHost) {
            const bool glur = QSettings().value("ui/toolbarBackdrop", "glass").toString() == QLatin1String("glur");
            mac::applyBackdropBehind(m_toolbarHost, 0.0, glur ? mac::BackdropStyle::Glur : mac::BackdropStyle::LiquidGlass);
        }
        for (ui::ToolbarCluster *cluster : m_toolbarClusters) {
            if (cluster && cluster->glassBacked() && cluster->isVisible()) mac::applyLiquidGlassBehind(cluster, cluster->radius());
        }
        mac::applyLiquidGlassBehind(pill, pill->radius());
    } else {
        if (m_toolbarHost) mac::hideLiquidGlassBehind(m_toolbarHost);
        for (ui::ToolbarCluster *cluster : m_toolbarClusters) {
            if (cluster && cluster->glassBacked()) mac::hideLiquidGlassBehind(cluster);
        }
        mac::hideLiquidGlassBehind(pill);
    }
}

void BrowserWindow::syncToolbarOverlay() {
    if (!m_toolbarHost || !m_webContainer) return;
    m_toolbarHost->setOverlayWidth(m_webContainer->width());
    m_toolbarHost->raise();
    const double inset = m_toolbarRowAvailable && m_toolbarHost->isVisible() ? m_toolbarHost->height() : 0.0;
    for (WebView *view : extensionViews()) {
        if (!view) continue;
        view->setObscuredTopInset(inset);
        view->setCornerRadius(ui::metrics::WebContainerRadius);
    }
}

// ---- Collapsing toolbar -----------------------------------------------------

void BrowserWindow::setToolbarRowVisible(bool visible) {
    m_toolbarRowAvailable = visible;
    if (m_toolbarHost) m_toolbarHost->setVisible(visible);
    if (!visible && m_toolbarGrabber) m_toolbarGrabber->hide();
    if (visible) positionToolbarGrabber();
    syncToolbarOverlay();
    syncAddressPillGlass();
}

void BrowserWindow::setToolbarCollapsed(bool collapsed, bool animated) {
    if (!m_toolbarHost || m_addrInSidebar) return;
    if (collapsed && !m_toolbarRowAvailable) return;
    if (m_toolbarCollapsed == collapsed && (!m_toolbarAnim || m_toolbarAnim->state() != QAbstractAnimation::Running)) {
        positionToolbarGrabber();
        return;
    }
    m_toolbarCollapsed = collapsed;
    m_scrollDownAccum = 0.0;
    if (!m_toolbarAnim) {
        m_toolbarAnim = new QVariantAnimation(this);
        m_toolbarAnim->setDuration(kToolbarCollapseMs);
        m_toolbarAnim->setEasingCurve(QEasingCurve::OutCubic);
        connect(m_toolbarAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
            if (m_toolbarHost) m_toolbarHost->setProgress(v.toDouble());
            syncToolbarOverlay();
            syncAddressPillGlass();
        });
        connect(m_toolbarAnim, &QVariantAnimation::finished, this, [this] {
            syncAddressPillGlass();
            positionToolbarGrabber();
            positionToast();
            mac::refreshUnifiedToolbar(this);
        });
    }
    m_toolbarAnim->stop();
    const qreal target = collapsed ? 0.0 : 1.0;
    if (!animated || !isVisible()) {
        m_toolbarHost->setProgress(target);
        syncToolbarOverlay();
        syncAddressPillGlass();
        positionToolbarGrabber();
        positionToast();
        return;
    }
    // Hide the grabber immediately when expanding so it never overlaps the
    // returning toolbar; when collapsing it appears once the row is gone.
    if (!collapsed && m_toolbarGrabber) m_toolbarGrabber->hide();
    m_toolbarAnim->setStartValue(m_toolbarHost->progress());
    m_toolbarAnim->setEndValue(target);
    m_toolbarAnim->start();
}

void BrowserWindow::expandToolbar() {
    m_scrollDownAccum = 0.0;
    if (m_toolbarCollapsed) setToolbarCollapsed(false);
}

void BrowserWindow::handlePageScroll(const QVariant &body) {
    if (!m_collapseToolbarOnScroll || m_addrInSidebar || !m_toolbarRowAvailable) return;
    const QVariantMap map = body.toMap();
    const double y = map.value("y").toDouble();
    const double dy = map.value("dy").toDouble();
    const bool atTop = map.value("atTop").toBool() || y <= 0.0;
    if (atTop || dy < 0.0) {
        expandToolbar();
        return;
    }
    if (dy <= 0.0) return;
    if (dy > kToolbarProgrammaticJumpPx) {
        m_scrollDownAccum = 0.0;
        return;
    }
    // Never collapse while the user is typing in the omnibox.
    if (m_addressBar && m_addressBar->hasFocus()) return;
    m_scrollDownAccum += dy;
    if (!m_toolbarCollapsed && m_scrollDownAccum > kToolbarCollapseThresholdPx && y > kToolbarCollapseThresholdPx) {
        setToolbarCollapsed(true);
    }
}

void BrowserWindow::observeScrollFor(WebView *view) {
    if (m_scrollObservedView == view) return;
    if (m_scrollObserverConn) QObject::disconnect(m_scrollObserverConn);
    if (m_navigationExpandConn) QObject::disconnect(m_navigationExpandConn);
    m_scrollObserverConn = QMetaObject::Connection();
    m_navigationExpandConn = QMetaObject::Connection();
    m_scrollObservedView = view;
    if (!view) return;
    syncToolbarOverlay();
    m_scrollObserverConn = connect(view, &WebView::scriptMessage, this, [this, view](const QString &name, const QVariant &body) {
        if (name != QLatin1String("scroll") || currentView() != view) return;
        handlePageScroll(body);
    });
    // Navigation start (URL change) brings the toolbar back.
    m_navigationExpandConn = connect(view, &WebView::urlChanged, this, [this, view](const QUrl &) {
        if (currentView() == view) expandToolbar();
    });
}

void BrowserWindow::positionToolbarGrabber() {
    if (!m_toolbarGrabber || !m_webContainer) return;
    const bool show = m_toolbarCollapsed && m_toolbarRowAvailable && !m_addrInSidebar && isVisible()
        && !isMinimized() && (!m_toolbarAnim || m_toolbarAnim->state() != QAbstractAnimation::Running);
    if (!show) {
        if (m_toolbarGrabber->isVisible()) m_toolbarGrabber->hide();
        return;
    }
    const QPoint origin = m_webContainer->mapToGlobal(QPoint(0, 0));
    m_toolbarGrabber->setGeometry(origin.x(), origin.y(), m_webContainer->width(), ui::ToolbarGrabber::HotZoneHeight);
    if (!m_toolbarGrabber->isVisible()) {
        m_toolbarGrabber->show();
        mac::showWindowWithoutAppActivation(m_toolbarGrabber);
    }
    m_toolbarGrabber->raise();
}

// ---- Downloads badge + toast ----------------------------------------------

void BrowserWindow::showDownloadsBadge(int activeCount, double progress) {
    if (m_downloadsBtn) m_downloadsBtn->setActivity(activeCount, progress);
}

void BrowserWindow::positionToast() {
    if (!m_toast || !m_toast->isVisible() || !m_webContainer) return;
    const QPoint origin = m_webContainer->mapToGlobal(QPoint(0, 0));
    const int toolbarBottom = m_toolbarHost && m_toolbarHost->isVisible() ? m_toolbarHost->height() : 0;
    m_toast->reposition(QPoint(origin.x() + m_webContainer->width() - ui::metrics::ToastInset,
                               origin.y() + toolbarBottom + ui::metrics::ToastInset));
}

void BrowserWindow::showToast(const QString &title, const QString &subtitle, const QIcon &icon) {
    if (!m_toast) {
        m_toast = new ui::ToastWidget(m_theme, this);
        connect(m_toast, &ui::ToastWidget::clicked, this, &BrowserWindow::toastClicked);
    }
    m_toast->setContent(title, subtitle, icon);
    const QPoint origin = m_webContainer ? m_webContainer->mapToGlobal(QPoint(0, 0)) : mapToGlobal(QPoint(0, 0));
    const int width = m_webContainer ? m_webContainer->width() : this->width();
    const int toolbarBottom = m_toolbarHost && m_toolbarHost->isVisible() ? m_toolbarHost->height() : 0;
    m_toast->presentAt(QPoint(origin.x() + width - ui::metrics::ToastInset,
                              origin.y() + toolbarBottom + ui::metrics::ToastInset));
}

void BrowserWindow::closeEvent(QCloseEvent *e) {
    QSettings settings;
    settings.setValue("ui/windowGeometry", saveGeometry());
    saveSessionForProfile(m_profiles.currentName());
    QMainWindow::closeEvent(e);
}

void BrowserWindow::showEvent(QShowEvent *e) {
    QMainWindow::showEvent(e);
    mac::integrateUnifiedToolbar(this, nullptr, /*compact=*/true);
    mac::setWindowAppearanceDark(this, m_theme.background.lightness() < 128);
    mac::enableWindowVibrancy(this, mac::VibrancyMaterial::Sidebar);
    mac::enableHighRefreshRate(this);
    // Round the web-content stack on the next event loop turn (after the
    // first QWebEngineView NSView exists).
    QTimer::singleShot(0, this, [this] {
        if (m_webContainer) {
            mac::roundWidgetCorners(m_webContainer, ui::metrics::WebContainerRadius, /*recurseDescendants=*/false);
        }
        if (m_stack) mac::roundWidgetCorners(m_stack, ui::metrics::WebContainerRadius);
        syncToolbarOverlay();
        // Liquid Glass backing for the address pill.
        syncAddressPillGlass();
        positionToolbarGrabber();
    });
}

WebView *BrowserWindow::extensionCurrentView() const {
    return currentView();
}

QList<WebView *> BrowserWindow::extensionViews() const {
    return m_tabTree ? m_tabTree->views() : QList<WebView *>();
}

void BrowserWindow::applyPageColorScheme(const QString &scheme) {
    QSettings().setValue("ui/pageColorScheme", scheme);
    m_lastAppliedChrome = QColor();
    const QList<WebView *> liveTabs = extensionViews();
    for (auto *view : liveTabs) {
        if (view) view->setPageColorScheme(scheme);
    }
    if (auto *view = currentView()) applyChromeForPageColor(view->cachedThemeColor());
    else applyChromeForPageColor(QColor());
}

WebView *BrowserWindow::extensionCreateTab(const QUrl &url, bool background) {
    return m_tabTree ? m_tabTree->newTabForExtension(url, background) : nullptr;
}

WebView *BrowserWindow::extensionAdoptNativeTab(void *nativeWebView, bool background) {
    if (!m_tabTree || !nativeWebView) return nullptr;
    auto *view = new WebView(nullptr, nullptr);
    view->adoptNativeWebView(nativeWebView);
    m_tabTree->adoptExtensionView(view, background);
    return view;
}

void BrowserWindow::extensionSelectView(WebView *view) {
    if (m_tabTree) m_tabTree->selectView(view);
}

void BrowserWindow::extensionCloseView(WebView *view) {
    if (!m_tabTree || !view) return;
    m_tabTree->selectView(view);
    if (m_tabTree->currentView() == view) m_tabTree->closeCurrent();
}

void BrowserWindow::extensionSetAction(const QString &key, const QString &label, const QIcon &icon, std::function<void(QWidget *)> handler) {
    if (!m_topbar) return;
    QToolButton *button = m_extensionActionButtons.value(key, nullptr);
    if (!button) {
        button = new QToolButton(m_toolbarActions ? static_cast<QWidget *>(m_toolbarActions) : m_topbar);
        button->setAutoRaise(true);
        button->setFocusPolicy(Qt::NoFocus);
        button->setCursor(Qt::PointingHandCursor);
        button->setIconSize(QSize(16, 16));
        button->setFixedSize(32, 32);
        button->setStyleSheet(QString(
            "QToolButton { background: transparent; border: none; border-radius: 16px; padding: 0px; color: %1; }"
            "QToolButton:hover { background: %2; }"
            "QToolButton:pressed { background: %3; }")
            .arg(m_theme.foreground.name(), m_theme.hover.name(), m_theme.raised.name()));
        if (auto *layout = qobject_cast<QHBoxLayout *>(m_toolbarActions ? m_toolbarActions->layout() : m_topbar->layout())) {
            const int index = m_extensionsBtn ? layout->indexOf(m_extensionsBtn) : (m_settingsBtn ? layout->indexOf(m_settingsBtn) : layout->count());
            layout->insertWidget(qMax(0, index), button);
        }
        m_extensionActionButtons.insert(key, button);
    }
    button->setText(QString());
    if (!icon.isNull()) {
        button->setIcon(icon);
    } else {
        button->setIcon(mac::sfSymbolIcon("puzzlepiece.extension", 14.0, m_theme.foreground));
        for (const auto &extension : ChromeExtensionManager::configuredExtensions()) {
            if (extension.name == label || extension.name == key || QFileInfo(extension.path).fileName() == key || label.contains(extension.name, Qt::CaseInsensitive)) {
                if (!extension.iconPath.isEmpty()) button->setIcon(QIcon(extension.iconPath));
                break;
            }
        }
    }
    button->setToolTip(label);
    m_extensionActionHandlers.insert(key, std::move(handler));
    button->disconnect();
    connect(button, &QToolButton::clicked, this, [this, key, button] {
        const auto handler = m_extensionActionHandlers.value(key);
        if (handler) handler(button);
    });
    button->show();
}

void BrowserWindow::loadFromOmnibox() {
    const QUrl url = urlFromInput(m_omnibox->text());
    if (handleInternalUrl(url)) return;
    if (auto *view = currentView()) {
        view->load(url);
        if (m_reloadBtn && !isBlankTabUrl(url)) {
            QColor fg = m_topbar ? m_topbar->property("chromeFg").value<QColor>() : QColor();
            if (!fg.isValid()) fg = m_theme.foreground;
            m_reloadBtn->setEnabled(true);
            setButtonSymbolSmooth(m_reloadBtn, "xmark", 14.0, fg);
        }
    }
}

void BrowserWindow::openBlankTabForLocationEntry() {
    m_tabTree->newTab(QUrl("about:blank"));
    if (m_reloadBtn) {
        QColor fg = m_topbar ? m_topbar->property("chromeFg").value<QColor>() : QColor();
        if (!fg.isValid()) fg = m_theme.foreground;
        const QColor disabledFg = disabledToolbarColor(fg);
        m_reloadBtn->setEnabled(false);
        setButtonSymbolSmooth(m_reloadBtn, "arrow.clockwise", 14.0, disabledFg);
    }
    refreshFloatingOmniboxItems();
    if (m_addrInSidebar) {
        if (m_floatingOmnibox) m_floatingOmnibox->showFor(m_stack, QString());
        return;
    }
    if (!m_addressBar) return;
    m_addressBar->setText(QString());
    for (int delay : {0, 25, 75}) {
        QTimer::singleShot(delay, this, [this] {
            if (!m_addressBar || m_addrInSidebar) return;
            raise();
            activateWindow();
            m_addressBar->setFocus(Qt::ShortcutFocusReason);
            m_addressBar->selectAll();
        });
    }
}

void BrowserWindow::detachTabToWindow(WebView *view, const QUrl &url, const QPoint &globalPos) {
    if (!view || !m_tabTree) return;
    auto *window = new BrowserWindow;
    const QSize windowSize = size().isValid() ? size() : QSize(ui::metrics::WindowDefaultWidth, ui::metrics::WindowDefaultHeight);
    window->resize(windowSize);
    window->move(globalPos - QPoint(80, 48));
    window->show();
    if (auto *newView = window->currentView()) newView->load(url);
    m_tabTree->selectView(view);
    if (m_tabTree->currentView() == view) m_tabTree->closeCurrent();
}

void BrowserWindow::hideSplitPreview() {
    if (!m_splitPreviewActive) {
        if (m_splitPreview) m_splitPreview->hide();
        return;
    }
    m_splitPreviewActive = false;
    m_splitPreviewTarget = nullptr;
    m_splitPreviewLeft = false;
    if (auto *stackLayout = m_stack ? qobject_cast<QStackedLayout *>(m_stack->layout()) : nullptr) {
        stackLayout->setContentsMargins(0, 0, 0, 0);
        stackLayout->invalidate();
        m_stack->updateGeometry();
        if (auto *current = stackLayout->currentWidget()) current->setGeometry(m_stack->rect());
    }
    if (m_splitPreview) m_splitPreview->hide();
}

void BrowserWindow::showSplitPreview(WebView *dragged, WebView *target, const QPoint &globalPos) {
    if (!m_stack || !dragged || !target || dragged == target) {
        hideSplitPreview();
        return;
    }
    const QPoint local = m_stack->mapFromGlobal(globalPos);
    if (!m_stack->rect().contains(local)) {
        hideSplitPreview();
        return;
    }
    if (!m_splitPreview) {
        m_splitPreview = new QWidget(m_stack);
        m_splitPreview->setAttribute(Qt::WA_TransparentForMouseEvents);
    } else if (m_splitPreview->parentWidget() != m_stack) {
        m_splitPreview->setParent(m_stack);
    }
    const QRect bounds = m_stack->rect();
    const bool left = local.x() < bounds.center().x();
    const int previewWidth = qMax(160, bounds.width() / 2);
    QRect previewRect = bounds;
    if (left) previewRect.setWidth(previewWidth);
    else previewRect.setLeft(bounds.right() - previewWidth + 1);
    if (m_splitPreviewActive && m_splitPreviewTarget == target && m_splitPreviewLeft == left && m_splitPreview->geometry() == previewRect) return;
    m_splitPreviewActive = true;
    m_splitPreviewTarget = target;
    m_splitPreviewLeft = left;
    if (auto *stackLayout = qobject_cast<QStackedLayout *>(m_stack->layout())) {
        if (left) stackLayout->setContentsMargins(previewWidth, 0, 0, 0);
        else stackLayout->setContentsMargins(0, 0, previewWidth, 0);
        stackLayout->invalidate();
        m_stack->updateGeometry();
        stackLayout->activate();
    }
    m_splitPreview->setGeometry(previewRect);
    const QString material = QColor(m_theme.background.red(), m_theme.background.green(), m_theme.background.blue(), 132).name(QColor::HexArgb);
    const QString line = QColor(m_theme.foreground.red(), m_theme.foreground.green(), m_theme.foreground.blue(), 42).name(QColor::HexArgb);
    m_splitPreview->setStyleSheet(left
        ? QString("background: %1; border-right: 1px solid %2;").arg(material, line)
        : QString("background: %1; border-left: 1px solid %2;").arg(material, line));
    m_splitPreview->show();
    m_splitPreview->raise();
}

void BrowserWindow::splitTabs(WebView *first, WebView *second, const QPoint &globalPos) {
    hideSplitPreview();
    if (!first || !second || first == second || !m_stack) return;
    const QPoint stackPos = m_stack->mapFromGlobal(globalPos);
    const bool firstOnLeft = stackPos.x() < m_stack->rect().center().x();
    QWidget *host = m_splitHosts.value(first, nullptr);
    if (!host) host = m_splitHosts.value(second, nullptr);
    QSplitter *paneSplitter = nullptr;
    if (!host) {
        host = new QWidget(m_stack);
        auto *layout = new QHBoxLayout(host);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);
        paneSplitter = new SplitPaneSplitter(Qt::Horizontal, m_theme, host);
        paneSplitter->setObjectName("SplitPaneSplitter");
        paneSplitter->setHandleWidth(9);
        paneSplitter->setChildrenCollapsible(false);
        layout->addWidget(paneSplitter);
        static_cast<QStackedLayout *>(m_stack->layout())->addWidget(host);
    } else {
        paneSplitter = host->findChild<QSplitter *>("SplitPaneSplitter");
    }
    if (!paneSplitter) return;

    auto removeExistingPane = [paneSplitter](WebView *view) {
        for (auto *pane : paneSplitter->findChildren<QWidget *>("SplitPane", Qt::FindDirectChildrenOnly)) {
            if (pane->findChild<WebView *>(QString(), Qt::FindDirectChildrenOnly) == view) {
                view->setParent(nullptr);
                pane->setParent(nullptr);
                pane->deleteLater();
                return;
            }
        }
    };

    auto makePane = [this, host](WebView *view) {
        auto *pane = new QWidget(host);
        pane->setObjectName("SplitPane");
        pane->setStyleSheet(QString("QWidget#SplitPane { background: %1; border: none; border-radius: 0px; }").arg(m_theme.background.name()));
        auto *layout = new QVBoxLayout(pane);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);
        ui::TopbarWidgets toolbar = ui::buildTopbar(pane, m_theme);
        if (auto *bar = qobject_cast<ui::ChromeBar *>(toolbar.bar)) {
            bar->setTopCornerRadius(10);
            bar->setBackgroundColor(m_theme.raised, /*animate=*/false);
        }
        auto *addressCtl = new AddressBarController(toolbar.addressBar, toolbar.lockIcon, m_theme, pane);
        addressCtl->setSearchEngineUrl(m_searchEngine);
        addressCtl->setDisplayUrl(view->url().toString(), view->url().scheme() == "https");
        toolbar.searchIcon->setVisible(toolbar.addressBar->text().isEmpty());
        toolbar.addrWrap->setMouseTracking(true);
        toolbar.pillMenuBtn->setMouseTracking(true);
        auto *pillHover = new PillMenuHoverFilter(toolbar.addrWrap, toolbar.pillMenuBtn, pane);
        toolbar.addrWrap->installEventFilter(pillHover);
        toolbar.pillMenuBtn->installEventFilter(pillHover);
        QPointer<WebView> viewGuard(view);
        connect(addressCtl, &AddressBarController::submitted, this, [this, viewGuard](const QString &text) {
            if (!viewGuard) return;
            const QUrl url = urlFromInput(text);
            if (!handleInternalUrl(url)) {
                const QUrl alternate = alternateNavUrlFor(text);
                viewGuard->setProperty("alternateNavUrl", alternate.isValid() && url != alternate ? alternate.toString() : QString());
                viewGuard->load(url);
            }
            viewGuard->setFocus();
        });
        connect(addressCtl, &AddressBarController::escapePressed, view, [viewGuard] {
            if (viewGuard) viewGuard->setFocus();
        });
        connect(toolbar.addressBar, &QLineEdit::textChanged, toolbar.searchIcon, [searchIcon = toolbar.searchIcon](const QString &text) {
            searchIcon->setVisible(text.isEmpty());
        });
        connect(view, &WebView::urlChanged, toolbar.addressBar, [addressCtl, addressBar = toolbar.addressBar, searchIcon = toolbar.searchIcon](const QUrl &url) {
            addressCtl->setDisplayUrl(url.toString(), url.scheme() == "https");
            searchIcon->setVisible(addressBar->text().isEmpty());
        });
        connect(view, &WebView::loadProgress, toolbar.addrWrap, [addrWrap = toolbar.addrWrap](int progress) {
            if (auto *pill = qobject_cast<ui::AddrPill *>(addrWrap)) pill->setLoadProgress(progress);
        });
        auto applyPaneChrome = [this, toolbar, addressCtl, viewGuard](const QColor &pageColor) {
            const bool hasColor = pageColor.isValid() && pageColor.alpha() >= 16;
            const QColor bg = hasColor ? pageColor : QColor(28, 28, 30, 235);
            const int luma = (bg.red() * 299 + bg.green() * 587 + bg.blue() * 114) / 1000;
            const bool dark = luma < 140;
            const QColor fg = dark ? QColor(245, 245, 247) : QColor(28, 28, 30);
            QColor disabledFg = disabledToolbarColor(fg);
            toolbar.bar->setProperty("chromeFg", fg);
            auto mixRgb = [](const QColor &from, const QColor &to, double t) {
                return QColor(qRound(from.red() + (to.red() - from.red()) * t),
                              qRound(from.green() + (to.green() - from.green()) * t),
                              qRound(from.blue() + (to.blue() - from.blue()) * t),
                              from.alpha());
            };
            QColor hover = dark ? mixRgb(bg, QColor(255, 255, 255, bg.alpha()), 0.16)
                                : mixRgb(bg, QColor(0, 0, 0, bg.alpha()), 0.10);
            hover.setAlpha(qMax(220, bg.alpha()));
            QColor pressed = dark ? mixRgb(bg, QColor(255, 255, 255, bg.alpha()), 0.24)
                                  : mixRgb(bg, QColor(0, 0, 0, bg.alpha()), 0.16);
            pressed.setAlpha(qMax(230, bg.alpha()));
            QColor menuHover = dark ? mixRgb(bg, QColor(0, 0, 0, bg.alpha()), 0.22)
                                    : mixRgb(bg, QColor(255, 255, 255, bg.alpha()), 0.28);
            menuHover.setAlpha(qMax(220, bg.alpha()));
            QColor menuPressed = dark ? mixRgb(bg, QColor(0, 0, 0, bg.alpha()), 0.32)
                                      : mixRgb(bg, QColor(255, 255, 255, bg.alpha()), 0.40);
            menuPressed.setAlpha(qMax(230, bg.alpha()));
            if (auto *bar = qobject_cast<ui::ChromeBar *>(toolbar.bar)) bar->setBackgroundColor(bg, /*animate=*/true);
            auto reSymbol = [&](QToolButton *btn, const QString &name, double pointSize) {
                if (btn) btn->setIcon(mac::sfSymbolIcon(name, pointSize, fg));
            };
            reSymbol(toolbar.sidebar, "sidebar.left", 16.0);
            reSymbol(toolbar.back, "chevron.backward", 14.0);
            reSymbol(toolbar.forward, "chevron.forward", 14.0);
            const bool blankTab = !viewGuard || isBlankTabUrl(viewGuard->url());
            const bool canReload = !blankTab;
            if (toolbar.reload) toolbar.reload->setEnabled(canReload);
            setButtonSymbolSmooth(toolbar.reload, viewGuard && viewGuard->isLoading() && !blankTab ? "xmark" : "arrow.clockwise", 14.0, canReload ? fg : disabledFg);
            reSymbol(toolbar.newTab, "plus", 14.0);
            reSymbol(toolbar.settings, "gearshape", 14.0);
            reSymbol(toolbar.pillMenuBtn, "ellipsis.circle", 12.0);
            addressCtl->setIconColor(fg);
            const QString btnQss = QString(
                "QToolButton { background: transparent; border: none; border-radius: 6px; padding: 0px; }"
                "QToolButton:hover { background: rgba(%1,%2,%3,%4); }"
                "QToolButton:pressed { background: rgba(%5,%6,%7,%8); }")
                .arg(hover.red()).arg(hover.green()).arg(hover.blue()).arg(hover.alphaF(), 0, 'f', 3)
                .arg(pressed.red()).arg(pressed.green()).arg(pressed.blue()).arg(pressed.alphaF(), 0, 'f', 3);
            for (auto *btn : {toolbar.sidebar, toolbar.back, toolbar.forward, toolbar.reload, toolbar.newTab, toolbar.settings}) {
                if (btn) btn->setStyleSheet(btnQss);
            }
            if (toolbar.pillMenuBtn) {
                toolbar.pillMenuBtn->setStyleSheet(QString(
                    "QToolButton { background: transparent; border: none; border-radius: 6px; padding: 0px; }"
                    "QToolButton:hover { background: rgba(%1,%2,%3,%4); }"
                    "QToolButton:pressed { background: rgba(%5,%6,%7,%8); }")
                    .arg(menuHover.red()).arg(menuHover.green()).arg(menuHover.blue()).arg(menuHover.alphaF(), 0, 'f', 3)
                    .arg(menuPressed.red()).arg(menuPressed.green()).arg(menuPressed.blue()).arg(menuPressed.alphaF(), 0, 'f', 3));
            }
            if (auto *pill = qobject_cast<ui::AddrPill *>(toolbar.addrWrap)) {
                pill->setIdleColor(bg);
                pill->setHoverColor(hover);
            }
            if (toolbar.addressBar) {
                toolbar.addressBar->setStyleSheet(QString(
                    "QLineEdit { background: transparent; border: none; color: %1; font-family: '%2'; font-size: %3px; padding: 0px; }")
                    .arg(fg.name(), m_theme.fontFamily, QString::number(m_theme.regularSize)));
            }
        };
        applyPaneChrome(view->cachedThemeColor());
        connect(view, &WebView::themeColorChanged, toolbar.bar, applyPaneChrome);
        auto syncPaneNav = [toolbar, viewGuard] {
            if (!viewGuard) return;
            QColor fg = toolbar.bar->property("chromeFg").value<QColor>();
            if (!fg.isValid()) fg = QColor(245, 245, 247);
            QColor disabledFg = disabledToolbarColor(fg);
            const bool canGoBack = viewGuard->canGoBack();
            const bool canGoForward = viewGuard->canGoForward();
            const bool blankTab = isBlankTabUrl(viewGuard->url());
            const bool canReload = !blankTab;
            toolbar.back->setEnabled(canGoBack);
            toolbar.back->setIcon(mac::sfSymbolIcon("chevron.backward", 14.0, canGoBack ? fg : disabledFg));
            toolbar.forward->setEnabled(canGoForward);
            toolbar.forward->setIcon(mac::sfSymbolIcon("chevron.forward", 14.0, canGoForward ? fg : disabledFg));
            toolbar.reload->setEnabled(canReload);
            setButtonSymbolSmooth(toolbar.reload, viewGuard->isLoading() && !blankTab ? "xmark" : "arrow.clockwise", 14.0, canReload ? fg : disabledFg);
        };
        syncPaneNav();
        connect(view, &WebView::navigationStateChanged, toolbar.bar, syncPaneNav);
        connect(toolbar.sidebar, &QToolButton::clicked, this, [this] {
            if (!m_sidebar || !m_sidebarWidget) return;
            if (m_sidebarWidget->isVisible()) m_sidebar->setHidden(true);
            else m_sidebar->setHidden(false);
        });
        connect(toolbar.back, &QToolButton::clicked, view, [viewGuard] { if (viewGuard) viewGuard->back(); });
        connect(toolbar.forward, &QToolButton::clicked, view, [viewGuard] { if (viewGuard) viewGuard->forward(); });
        connect(toolbar.reload, &QToolButton::clicked, view, [toolbar, viewGuard] {
            if (!viewGuard || isBlankTabUrl(viewGuard->url())) return;
            QColor fg = toolbar.bar->property("chromeFg").value<QColor>();
            if (!fg.isValid()) fg = QColor(245, 245, 247);
            if (viewGuard->isLoading()) {
                viewGuard->stop();
                setButtonSymbolSmooth(toolbar.reload, "arrow.clockwise", 14.0, fg);
            } else {
                viewGuard->reload();
                setButtonSymbolSmooth(toolbar.reload, "xmark", 14.0, fg);
            }
        });
        connect(toolbar.newTab, &QToolButton::clicked, this, [this] { openBlankTabForLocationEntry(); });
        connect(toolbar.settings, &QToolButton::clicked, this, &BrowserWindow::showSettings);
        connect(toolbar.pillMenuBtn, &QToolButton::clicked, this, [this, viewGuard, button = toolbar.pillMenuBtn] {
            auto copyUrl = [viewGuard] { if (viewGuard) QApplication::clipboard()->setText(viewGuard->url().toString()); };
            auto reload = [viewGuard] { if (viewGuard) viewGuard->reload(); };
            auto newTab = [this] { openBlankTabForLocationEntry(); };
            auto settings = [this] { showSettings(); };
            auto bookmark = [this, viewGuard] {
                if (!viewGuard) return;
                if (m_bookmarks.contains(m_profiles.currentName(), viewGuard->url())) m_bookmarks.removeBookmark(m_profiles.currentName(), viewGuard->url());
                else m_bookmarks.addBookmark(m_profiles.currentName(), viewGuard->title(), viewGuard->url());
                refreshFloatingOmniboxItems();
            };
            const QString bookmarkTitle = viewGuard && m_bookmarks.contains(m_profiles.currentName(), viewGuard->url()) ? QStringLiteral("Remove Bookmark") : QStringLiteral("Bookmark This Page");
            if (mac::showNativePageActionsMenu(button, copyUrl, reload, bookmark, bookmarkTitle, newTab, settings)) return;

            QMenu menu(this);
            menu.addAction("Copy URL", this, copyUrl);
            menu.addAction("Reload", this, reload);
            menu.addAction(bookmarkTitle, this, bookmark);
            menu.addSeparator();
            menu.addAction("New Tab", this, newTab);
            menu.addAction("Settings…", this, settings);
            menu.exec(button->mapToGlobal(QPoint(0, button->height())));
        });
        auto *unsplit = new QToolButton(toolbar.bar);
        unsplit->setAutoRaise(true);
        unsplit->setFocusPolicy(Qt::NoFocus);
        unsplit->setCursor(Qt::PointingHandCursor);
        unsplit->setIcon(mac::sfSymbolIcon("rectangle.split.1x2", 12.0, m_theme.foreground));
        unsplit->setFixedSize(28, 28);
        unsplit->setToolTip("Split out tab");
        auto updateUnsplitChrome = [this, unsplit](const QColor &pageColor) {
            const bool hasColor = pageColor.isValid() && pageColor.alpha() >= 16;
            const QColor bg = hasColor ? pageColor : QColor(28, 28, 30, 235);
            const int luma = (bg.red() * 299 + bg.green() * 587 + bg.blue() * 114) / 1000;
            const bool dark = luma < 140;
            const QColor fg = dark ? QColor(245, 245, 247) : QColor(28, 28, 30);
            auto mixRgb = [](const QColor &from, const QColor &to, double t) {
                return QColor(qRound(from.red() + (to.red() - from.red()) * t),
                              qRound(from.green() + (to.green() - from.green()) * t),
                              qRound(from.blue() + (to.blue() - from.blue()) * t),
                              from.alpha());
            };
            QColor hover = dark ? mixRgb(bg, QColor(255, 255, 255, bg.alpha()), 0.16)
                                : mixRgb(bg, QColor(0, 0, 0, bg.alpha()), 0.10);
            hover.setAlpha(qMax(220, bg.alpha()));
            QColor pressed = dark ? mixRgb(bg, QColor(255, 255, 255, bg.alpha()), 0.24)
                                  : mixRgb(bg, QColor(0, 0, 0, bg.alpha()), 0.16);
            pressed.setAlpha(qMax(230, bg.alpha()));
            unsplit->setIcon(mac::sfSymbolIcon("rectangle.split.1x2", 12.0, fg));
            unsplit->setStyleSheet(QString(
                "QToolButton { background: transparent; border: none; border-radius: 6px; padding: 0px; }"
                "QToolButton:hover { background: rgba(%1,%2,%3,%4); }"
                "QToolButton:pressed { background: rgba(%5,%6,%7,%8); }")
                .arg(hover.red()).arg(hover.green()).arg(hover.blue()).arg(hover.alphaF(), 0, 'f', 3)
                .arg(pressed.red()).arg(pressed.green()).arg(pressed.blue()).arg(pressed.alphaF(), 0, 'f', 3));
        };
        updateUnsplitChrome(view->cachedThemeColor());
        connect(view, &WebView::themeColorChanged, unsplit, updateUnsplitChrome);
        if (auto *row = qobject_cast<QHBoxLayout *>(toolbar.bar->layout())) row->addWidget(unsplit);
        layout->addWidget(toolbar.bar);
        view->setParent(pane);
        layout->addWidget(view, 1);
        auto collapseHostIfNeeded = [this, host] {
            const auto remaining = host->findChildren<WebView *>();
            if (remaining.size() == 1) {
                auto *lastView = remaining.first();
                lastView->setParent(m_stack);
                static_cast<QStackedLayout *>(m_stack->layout())->addWidget(lastView);
                m_splitHosts.remove(lastView);
                host->deleteLater();
            }
        };
        connect(unsplit, &QToolButton::clicked, this, [this, host, pane, view, collapseHostIfNeeded] {
            view->setParent(m_stack);
            static_cast<QStackedLayout *>(m_stack->layout())->addWidget(view);
            m_splitHosts.remove(view);
            pane->deleteLater();
            collapseHostIfNeeded();
            static_cast<QStackedLayout *>(m_stack->layout())->setCurrentWidget(view);
            if (!m_addrInSidebar && m_topbar) m_topbar->show();
            if (!m_addrInSidebar && m_topSeparator) m_topSeparator->show();
            view->show();
        });
        QPointer<QWidget> paneGuard = pane;
        QPointer<QWidget> hostGuard = host;
        connect(view, &QObject::destroyed, this, [this, paneGuard, hostGuard] {
            if (paneGuard) paneGuard->deleteLater();
            if (!hostGuard) return;
            QTimer::singleShot(0, this, [this, hostGuard] {
                if (!hostGuard) return;
                const auto remaining = hostGuard->findChildren<WebView *>();
                if (remaining.size() == 1) {
                    auto *lastView = remaining.first();
                    lastView->setParent(m_stack);
                    static_cast<QStackedLayout *>(m_stack->layout())->addWidget(lastView);
                    m_splitHosts.remove(lastView);
                    hostGuard->deleteLater();
                    static_cast<QStackedLayout *>(m_stack->layout())->setCurrentWidget(lastView);
                    if (!m_addrInSidebar && m_topbar) m_topbar->show();
                    if (!m_addrInSidebar && m_topSeparator) m_topSeparator->show();
                    lastView->show();
                } else if (remaining.isEmpty()) {
                    hostGuard->deleteLater();
                }
            });
        }, Qt::UniqueConnection);
        return pane;
    };

    removeExistingPane(first);
    removeExistingPane(second);
    auto *firstPane = makePane(first);
    auto *secondPane = makePane(second);
    auto setPaneSide = [this](QWidget *pane, bool left) {
        pane->setStyleSheet(QString("QWidget#SplitPane { background: %1; border: none; border-radius: 0px; }")
            .arg(m_theme.background.name()));
        if (auto *bar = pane->findChild<ui::ChromeBar *>(QString(), Qt::FindDirectChildrenOnly)) {
            bar->setTopCornerMask(left, !left);
        }
    };
    if (firstOnLeft) {
        setPaneSide(firstPane, true);
        setPaneSide(secondPane, false);
        paneSplitter->insertWidget(0, firstPane);
        paneSplitter->insertWidget(1, secondPane);
    } else {
        setPaneSide(secondPane, true);
        setPaneSide(firstPane, false);
        paneSplitter->insertWidget(0, secondPane);
        paneSplitter->insertWidget(1, firstPane);
    }
    m_splitHosts.insert(first, host);
    m_splitHosts.insert(second, host);
    if (m_tabTree) m_tabTree->markViewsSplit(first, second);
    connect(first, &QObject::destroyed, this, [this, first] { m_splitHosts.remove(first); }, Qt::UniqueConnection);
    connect(second, &QObject::destroyed, this, [this, second] { m_splitHosts.remove(second); }, Qt::UniqueConnection);
    static_cast<QStackedLayout *>(m_stack->layout())->setCurrentWidget(host);
    if (m_topbar) m_topbar->hide();
    if (m_topSeparator) m_topSeparator->hide();
    first->show();
    second->show();
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
    saveSessionForProfile(m_profiles.currentName());
    QString homePage = m_homePage;
    QString searchEngine = m_searchEngine;
    bool showFullUrl = QSettings().value("ui/showFullUrl", false).toBool();
    bool closeWindowWithLastTab = QSettings().value("browser/closeWindowWithLastTab", false).toBool();
    const mac::SettingsOutcome outcome = mac::showNativeSettingsWindow(this, m_profiles, homePage, searchEngine, showFullUrl, closeWindowWithLastTab);
    if (!outcome.saved) return;

    m_homePage = homePage;
    if (m_tabTree) m_tabTree->setHomePage(homePage);

    if (searchEngine.contains("%1")) {
        m_searchEngine = searchEngine;
        if (m_floatingOmnibox) m_floatingOmnibox->setSearchEngineUrl(searchEngine);
    }
    if (m_tabTree) m_tabTree->setCloseWindowWithLastTab(closeWindowWithLastTab);

    if (m_addressBarCtl) m_addressBarCtl->setShowFullUrl(showFullUrl);
    m_collapseToolbarOnScroll = QSettings().value("ui/collapseToolbarOnScroll", true).toBool();
    if (!m_collapseToolbarOnScroll) expandToolbar();
    syncAddressPillGlass();
    if (outcome.pageColorSchemeChanged) applyPageColorScheme(QSettings().value("ui/pageColorScheme", QStringLiteral("system")).toString());
    if (outcome.importPasswords) PasswordImport::importFromCsvInteractive(this);
}

void BrowserWindow::updateForCurrentTab() {
    mac::refreshUnifiedToolbar(this);
    auto *view = currentView();
    if (!view) return;
    m_tabRecency.removeAll(view);
    m_tabRecency.prepend(view);
    if (auto *splitHost = m_splitHosts.value(view, nullptr)) {
        static_cast<QStackedLayout *>(m_stack->layout())->setCurrentWidget(splitHost);
        if (m_topbar) m_topbar->hide();
        if (m_topSeparator) m_topSeparator->hide();
        setToolbarRowVisible(false);
    } else {
        static_cast<QStackedLayout *>(m_stack->layout())->setCurrentWidget(view);
        if (!m_addrInSidebar && m_topbar) m_topbar->show();
        if (!m_addrInSidebar && m_topSeparator) m_topSeparator->show();
        setToolbarRowVisible(!m_addrInSidebar);
    }
    // Tab change always brings the toolbar back and re-targets the scroll
    // observer at the newly current tab only.
    observeScrollFor(view);
    expandToolbar();
    m_omnibox->setText(view->url().toString());
    auto syncNavButtons = [this, view] {
        QColor fg = m_topbar ? m_topbar->property("chromeFg").value<QColor>() : QColor();
        if (!fg.isValid()) fg = m_theme.foreground;
        QColor disabledFg = disabledToolbarColor(fg);
        const bool canGoBack = view->canGoBack();
        const bool canGoForward = view->canGoForward();
        if (m_backBtn) {
            m_backBtn->setEnabled(canGoBack);
            m_backBtn->setIcon(mac::sfSymbolIcon("chevron.backward", 14.0, canGoBack ? fg : disabledFg));
        }
        if (m_fwdBtn) {
            m_fwdBtn->setEnabled(canGoForward);
            m_fwdBtn->setIcon(mac::sfSymbolIcon("chevron.forward", 14.0, canGoForward ? fg : disabledFg));
        }
    };
    syncNavButtons();
    if (m_reloadBtn) {
        QColor fg = m_topbar ? m_topbar->property("chromeFg").value<QColor>() : QColor();
        if (!fg.isValid()) fg = m_theme.foreground;
        QColor disabledFg = disabledToolbarColor(fg);
        const bool blankTab = isBlankTabUrl(view->url());
        const bool canReload = !blankTab;
        m_reloadBtn->setEnabled(canReload);
        setButtonSymbolSmooth(m_reloadBtn, view->isLoading() && !blankTab ? "xmark" : "arrow.clockwise", 14.0, canReload ? fg : disabledFg);
    }
    connect(view, &WebView::loadFinished, this, [this, view](bool ok) {
        if (currentView() != view || !ok) return;
        const QString alternate = view->property("alternateNavUrl").toString();
        if (alternate.isEmpty()) return;
        view->setProperty("alternateNavUrl", QString());
        if (statusBar()) statusBar()->showMessage(QStringLiteral("Did you mean to go to %1? Press Ctrl+Enter next time to open the .com directly.").arg(QUrl(alternate).host()), 8000);
    }, Qt::UniqueConnection);
    connect(view, &WebView::navigationStateChanged, this, [this, view] {
        if (currentView() != view) return;
        QColor fg = m_topbar ? m_topbar->property("chromeFg").value<QColor>() : QColor();
        if (!fg.isValid()) fg = m_theme.foreground;
        QColor disabledFg = disabledToolbarColor(fg);
        const bool canGoBack = view->canGoBack();
        const bool canGoForward = view->canGoForward();
        if (m_backBtn) {
            m_backBtn->setEnabled(canGoBack);
            m_backBtn->setIcon(mac::sfSymbolIcon("chevron.backward", 14.0, canGoBack ? fg : disabledFg));
        }
        if (m_fwdBtn) {
            m_fwdBtn->setEnabled(canGoForward);
            m_fwdBtn->setIcon(mac::sfSymbolIcon("chevron.forward", 14.0, canGoForward ? fg : disabledFg));
        }
        if (m_reloadBtn) {
            const bool blankTab = isBlankTabUrl(view->url());
            const bool canReload = !blankTab;
            m_reloadBtn->setEnabled(canReload);
            setButtonSymbolSmooth(m_reloadBtn, view->isLoading() && !blankTab ? "xmark" : "arrow.clockwise", 14.0, canReload ? fg : disabledFg);
        }
        applyChromeForPageColor(view->cachedThemeColor());
    }, Qt::UniqueConnection);
    if (m_addressBarCtl) {
        m_addressBarCtl->showDisplayUrl(view->url().toString(), view->url().scheme() == "https");
    }
    setWindowTitle((view->title().isEmpty() ? "pocb" : view->title()) + " — pocb");
    rememberCurrentPage();
}

QWidget *BrowserWindow::buildTopbar(QWidget *parent) {
    ui::TopbarWidgets w = ui::buildTopbar(parent, m_theme);
    m_sidebarBtn = w.sidebar;
    m_backBtn = w.back;
    m_fwdBtn = w.forward;
    m_reloadBtn = w.reload;
    m_newTabBtn = w.newTab;
    m_settingsBtn = w.settings;
    m_toolbarActions = w.actionsCluster;
    m_toolbarClusters = {w.navCluster, w.actionsCluster};
    m_extensionsBtn = new QToolButton(w.actionsCluster);
    m_extensionsBtn->setAutoRaise(true);
    m_extensionsBtn->setFocusPolicy(Qt::NoFocus);
    m_extensionsBtn->setCursor(Qt::PointingHandCursor);
    m_extensionsBtn->setIconSize(QSize(18, 18));
    m_extensionsBtn->setFixedSize(32, 32);
    m_extensionsBtn->setToolTip("Extensions");
    m_extensionsBtn->setIcon(mac::sfSymbolIcon("puzzlepiece.extension", 16.0, m_theme.foreground));
    m_extensionsBtn->setStyleSheet(QString(
        "QToolButton { background: transparent; border: none; border-radius: 16px; padding: 0px; }"
        "QToolButton:hover { background: %1; }"
        "QToolButton:pressed { background: %2; }")
        .arg(m_theme.hover.name(), m_theme.raised.name()));
    m_downloadsBtn = new ui::DownloadsButton(w.actionsCluster);
    m_downloadsBtn->setAutoRaise(true);
    m_downloadsBtn->setFocusPolicy(Qt::NoFocus);
    m_downloadsBtn->setCursor(Qt::PointingHandCursor);
    m_downloadsBtn->setIconSize(QSize(18, 18));
    m_downloadsBtn->setFixedSize(32, 32);
    m_downloadsBtn->setToolTip("Downloads");
    m_downloadsBtn->setIcon(mac::sfSymbolIcon("arrow.down.circle", 16.0, m_theme.foreground));
    m_downloadsBtn->setStyleSheet(m_extensionsBtn->styleSheet());
    m_downloadsBtn->setRingColor(m_theme.accent);
    if (auto *layout = qobject_cast<QHBoxLayout *>(w.actionsCluster->layout())) {
        const int index = m_settingsBtn ? layout->indexOf(m_settingsBtn) : layout->count();
        layout->insertWidget(qMax(0, index), m_extensionsBtn);
        layout->insertWidget(qMax(0, index) + 1, m_downloadsBtn);
    }
    connect(m_downloadsBtn, &QToolButton::clicked, this, &BrowserWindow::downloadsRequested);
    m_settingsBtn = w.settings;
    m_addressBar = w.addressBar;
    auto syncDisabledDragThrough = [](QToolButton *button) {
        if (button) button->setAttribute(Qt::WA_TransparentForMouseEvents, !button->isEnabled());
    };
    for (QToolButton *button : {m_backBtn, m_fwdBtn, m_reloadBtn}) {
        if (!button) continue;
        button->installEventFilter(this);
        syncDisabledDragThrough(button);
    }
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
        pill->setIdleColor(QColor(28, 28, 30, 235));
        pill->setFocusColor(m_theme.accent);
        if (QSettings().value("ui/useLiquidGlass", true).toBool() && !QSettings().value("ui/addressBarInSidebar", false).toBool()) {
            // The toolbar leaves a hole under the pill and an
            // NSGlassEffectView is stacked beneath the native container
            // (applied once the NSViews exist, see syncAddressPillGlass).
            pill->setGlassMode(true);
            if (auto *bar = qobject_cast<ui::ChromeBar *>(pill->parentWidget())) {
                bar->setGlassCutout(pill, pill->radius());
                bar->setGlassBacked(true);
            }
            for (ui::ToolbarCluster *cluster : m_toolbarClusters) {
                if (cluster) cluster->setGlassBacked(true);
            }
            pill->installEventFilter(this);
            if (m_webContainer) m_webContainer->installEventFilter(this);
        }
    }
    // Pop the pill when the user begins editing it.
    class FocusPopFilter : public QObject {
    public:
        FocusPopFilter(QWidget *pill, BrowserWindow *owner, std::function<void()> onFocusIn)
            : QObject(owner), m_pill(pill), m_onFocusIn(std::move(onFocusIn)) {}
        bool eventFilter(QObject *o, QEvent *e) override {
            if (auto *ap = qobject_cast<ui::AddrPill *>(m_pill)) {
                if (e->type() == QEvent::FocusIn) {
                    ap->setPopped(true);
                    if (m_onFocusIn) m_onFocusIn();
                } else if (e->type() == QEvent::FocusOut) {
                    ap->setPopped(false);
                }
            }
            return QObject::eventFilter(o, e);
        }
        QWidget *m_pill;
        std::function<void()> m_onFocusIn;
    };
    if (m_addressBar && m_addrWrap) {
        m_addressBar->installEventFilter(new FocusPopFilter(m_addrWrap, this, [this] { expandToolbar(); }));
    }
    w.bar->installEventFilter(this);
    for (auto *child : w.bar->findChildren<QWidget *>()) {
        if (qobject_cast<QToolButton *>(child) || qobject_cast<QLineEdit *>(child)) continue;
        if (m_addrWrap && (child == m_addrWrap || m_addrWrap->isAncestorOf(child))) continue;
        child->installEventFilter(this);
    }

    if (m_addrWrap && m_pillMenuBtn) {
        m_addrWrap->setMouseTracking(true);
        m_pillMenuBtn->setMouseTracking(true);
        auto *pillHover = new PillMenuHoverFilter(m_addrWrap, m_pillMenuBtn, this);
        m_addrWrap->installEventFilter(pillHover);
        m_pillMenuBtn->installEventFilter(pillHover);
    }

    if (m_pillMenuBtn) {
        connect(m_pillMenuBtn, &QToolButton::clicked, this, [this] {
            auto copyUrl = [this] {
                if (auto *v = currentView()) QApplication::clipboard()->setText(v->url().toString());
            };
            auto reload = [this] { if (auto *v = currentView()) v->reload(); };
            auto newTab = [this] { openBlankTabForLocationEntry(); };
            auto settings = [this] { showSettings(); };
            auto bookmark = [this] {
                if (auto *v = currentView()) {
                    if (m_bookmarks.contains(m_profiles.currentName(), v->url())) m_bookmarks.removeBookmark(m_profiles.currentName(), v->url());
                    else m_bookmarks.addBookmark(m_profiles.currentName(), v->title(), v->url());
                    refreshFloatingOmniboxItems();
                }
            };
            const QString bookmarkTitle = currentView() && m_bookmarks.contains(m_profiles.currentName(), currentView()->url()) ? QStringLiteral("Remove Bookmark") : QStringLiteral("Bookmark This Page");
            if (mac::showNativePageActionsMenu(m_pillMenuBtn, copyUrl, reload, bookmark, bookmarkTitle, newTab, settings)) return;

            QMenu menu(this);
            menu.addAction("Copy URL", this, copyUrl);
            menu.addAction("Reload", this, reload);
            menu.addAction(bookmarkTitle, this, bookmark);
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
        const QUrl url = urlFromInput(text);
        if (!handleInternalUrl(url)) {
            if (auto *view = currentView()) {
                const QUrl alternate = alternateNavUrlFor(text);
                view->setProperty("alternateNavUrl", alternate.isValid() && url != alternate ? alternate.toString() : QString());
                view->load(url);
            }
        }
        if (auto *v = currentView()) v->setFocus();
    });
    connect(m_addressBarCtl, &AddressBarController::escapePressed, this, [this] {
        if (auto *v = currentView()) v->setFocus();
    });

    connect(m_sidebarBtn, &QToolButton::clicked, this, [this] {
        if (!m_sidebar || !m_sidebarWidget) return;
        if (m_sidebarWidget->isVisible()) m_sidebar->setHidden(true);
        else m_sidebar->expandAnimated();
    });
    connect(m_backBtn,   &QToolButton::clicked, this, [this] { if (auto *v = currentView()) v->back(); });
    connect(m_fwdBtn,    &QToolButton::clicked, this, [this] { if (auto *v = currentView()) v->forward(); });
    connect(m_reloadBtn, &QToolButton::clicked, this, [this] {
        if (auto *v = currentView()) {
            if (isBlankTabUrl(v->url())) return;
            QColor fg = m_topbar ? m_topbar->property("chromeFg").value<QColor>() : QColor();
            if (!fg.isValid()) fg = m_theme.foreground;
            if (v->isLoading()) {
                v->stop();
                setButtonSymbolSmooth(m_reloadBtn, "arrow.clockwise", 14.0, fg);
            } else {
                v->reload();
                setButtonSymbolSmooth(m_reloadBtn, "xmark", 14.0, fg);
            }
        }
    });
    connect(m_settingsBtn, &QToolButton::clicked, this, &BrowserWindow::showSettings);
    connect(m_extensionsBtn, &QToolButton::clicked, this, &BrowserWindow::showExtensionsMenu);
    connect(m_newTabBtn, &QToolButton::clicked, this, [this] { openBlankTabForLocationEntry(); });

    return w.bar;
}

QWidget *BrowserWindow::buildProfileSwitcher(QWidget *parent) {
    auto *wrap = new QWidget(parent);
    wrap->setObjectName("ProfileSwitcher");
    wrap->setAttribute(Qt::WA_TranslucentBackground);
    auto *layout = new QHBoxLayout(wrap);
    layout->setContentsMargins(2, 6, 2, 0);
    layout->setSpacing(6);

    m_profileAvatar = new ui::ProfileAvatarButton(m_theme, wrap);
    m_profileAvatar->setObjectName("ProfileButton");
    m_profileAvatar->setDiameter(ui::metrics::ProfileAvatarDiameter);
    m_profileBtn = m_profileAvatar;
    layout->addWidget(m_profileAvatar, 0, Qt::AlignLeft | Qt::AlignVCenter);
    m_pagerDots = new ui::PagerDots(m_theme, wrap);
    layout->addWidget(m_pagerDots, 1, Qt::AlignCenter);
    auto *archiveBtn = new QToolButton(wrap);
    archiveBtn->setAutoRaise(true);
    archiveBtn->setFocusPolicy(Qt::NoFocus);
    archiveBtn->setCursor(Qt::PointingHandCursor);
    archiveBtn->setFixedSize(m_profileAvatar->size());
    archiveBtn->setToolTip(QStringLiteral("Recently closed tabs"));
    archiveBtn->setIcon(mac::sfSymbolIcon("archivebox", 14.0, m_theme.foreground));
    const bool darkSidebar = m_theme.background.lightness() < 128;
    archiveBtn->setStyleSheet(QString(
        "QToolButton { background: rgba(255,255,255,%2); border: none; border-radius: %1px; padding: 0px; }"
        "QToolButton:hover { background: rgba(255,255,255,%3); }"
        "QToolButton:pressed { background: rgba(255,255,255,%4); }")
        .arg(m_profileAvatar->width() / 2)
        .arg(darkSidebar ? 14 : 90).arg(darkSidebar ? 30 : 140).arg(darkSidebar ? 44 : 180));
    connect(archiveBtn, &QToolButton::clicked, this, [this, archiveBtn] { showArchiveMenu(archiveBtn); });
    layout->addWidget(archiveBtn, 0, Qt::AlignRight | Qt::AlignVCenter);
    wrap->installEventFilter(this);
    m_profileBtn->installEventFilter(this);
    connect(m_profileBtn, &QToolButton::clicked, this, &BrowserWindow::showProfileMenu);
    updateProfileSwitcher();
    return wrap;
}

void BrowserWindow::updateProfileSwitcher() {
    if (!m_profileBtn) return;
    const QString name = m_profiles.currentName().isEmpty() ? QStringLiteral("Default") : m_profiles.currentName();
    m_profileBtn->setText(QString());
    m_profileBtn->setToolTip(QStringLiteral("Profile: %1").arg(name));
    if (m_profileAvatar) {
        m_profileAvatar->setProfileName(name);
        m_profileAvatar->setActive(true);
    }
    updatePagerDots();
}

void BrowserWindow::updatePagerDots() {
    if (!m_pagerDots) return;
    const QStringList list = orderedProfiles();
    m_pagerDots->setCount(list.size());
    const int current = qMax(0, list.indexOf(m_profiles.currentName()));
    const int width = m_sidebarViewport ? qMax(1, m_sidebarViewport->width()) : 1;
    // Offset < 0 means the next profile is sliding in from the right.
    const qreal shift = -qreal(m_sidebarSwipeOffset) / qreal(width);
    m_pagerDots->setPosition(current + shift);
    m_pagerDots->setVisible(list.size() > 1);
}

void BrowserWindow::switchProfileRelative(int direction) {
    QStringList list = m_profiles.profiles();
    list.removeDuplicates();
    const int defaultIndex = list.indexOf("Default");
    if (defaultIndex > 0) list.move(defaultIndex, 0);
    const int current = qMax(0, list.indexOf(m_profiles.currentName()));
    const int next = qBound(0, current + direction, list.size() - 1);
    if (next == current || next < 0 || next >= list.size()) return;
    saveSessionForProfile(m_profiles.currentName());
    animateProfileSwitcher(direction);
    m_profiles.setCurrentProfile(list.at(next));
}

QStringList BrowserWindow::orderedProfiles() const {
    QStringList list = m_profiles.profiles();
    list.removeDuplicates();
    const int defaultIndex = list.indexOf("Default");
    if (defaultIndex > 0) list.move(defaultIndex, 0);
    return list;
}

void BrowserWindow::updateCurrentProfileSnapshot() {
    if (!m_tabTree || !m_tabTree->widget()) return;
    QStringList titles;
    auto *tree = m_tabTree->treeWidget();
    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        if (auto *item = tree->topLevelItem(i)) titles.append(item->text(0).isEmpty() ? QStringLiteral("New tab") : item->text(0));
    }
    if (titles.isEmpty()) titles.append(QStringLiteral("New tab"));
    m_profileTabSnapshots.insert(m_profiles.currentName(), titles);
}

QPixmap BrowserWindow::renderProfilePreview(const QString &profile, const QSize &size) const {
    // Painted once per gesture into an offscreen pixmap (see
    // updateSidebarPreview); the preview pane then just blits it.
    const qreal dpr = devicePixelRatioF();
    QPixmap pixmap(size * dpr);
    pixmap.setDevicePixelRatio(dpr);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);
    const QStringList titles = m_profileTabSnapshots.value(profile, QStringList{QStringLiteral("New tab")});
    QFont f(m_theme.fontFamily, m_theme.regularSize);
    p.setFont(f);
    const int rowH = ui::metrics::SidebarRowHeight + 2;
    const int iconSize = 14;
    const QPixmap globe = mac::sfSymbolIcon("globe", 12.0, m_theme.muted).pixmap(iconSize, iconSize);
    int y = 2;
    const int rows = qMax(0, (size.height() - 4) / rowH);
    for (int i = 0; i < titles.size() && i < rows; ++i) {
        const QRect row(2, y, size.width() - 4, rowH - 2);
        if (i == 0) {
            QColor sel = m_theme.raised;
            sel.setAlpha(140);
            p.setPen(Qt::NoPen);
            p.setBrush(sel);
            p.drawRoundedRect(row, 7, 7);
        }
        p.drawPixmap(row.left() + 8, row.center().y() - iconSize / 2 + 1, globe);
        p.setPen(i == 0 ? m_theme.foreground : m_theme.muted);
        const QRect textRect(row.left() + 8 + iconSize + 8, row.top(), row.width() - iconSize - 24, row.height());
        p.drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, p.fontMetrics().elidedText(titles.at(i), Qt::ElideRight, textRect.width()));
        y += rowH;
    }
    return pixmap;
}

void BrowserWindow::updateSidebarPreview(int direction) {
    if (!m_sidebarPreviewPane || !m_sidebarViewport) return;
    const QStringList list = orderedProfiles();
    const int current = qMax(0, list.indexOf(m_profiles.currentName()));
    const int next = qBound(0, current + direction, list.size() - 1);
    if (next == current || next < 0 || next >= list.size()) return;
    const QString profile = list.at(next);
    if (m_sidebarPreviewProfile == profile && m_sidebarPreviewPane->hasSnapshot()) return;
    m_sidebarPreviewProfile = profile;
    m_sidebarPreviewPane->setSnapshot(renderProfilePreview(profile, m_sidebarViewport->size()));
}

void BrowserWindow::setSidebarSwipeOffset(int offset) {
    if (!m_sidebarPage || !m_sidebarViewport) return;
    const int width = qMax(1, m_sidebarViewport->width());
    const QRect bounds(QPoint(0, 0), m_sidebarViewport->size());
    m_sidebarViewport->setMask(QRegion(bounds));
    m_sidebarSwipeOffset = qBound(-width, offset, width);
    if (!m_sidebarStrip) return;
    if (m_sidebarSwipeOffset == 0) {
        m_sidebarStrip->setGeometry(bounds);
        m_sidebarPage->setGeometry(bounds);
        if (m_sidebarPreviewPage) m_sidebarPreviewPage->hide();
        updatePagerDots();
        return;
    }
    const int direction = m_sidebarSwipeDirection != 0 ? m_sidebarSwipeDirection : (m_sidebarSwipeOffset < 0 ? 1 : -1);
    const bool previewAvailable = m_sidebarPreviewPane && m_sidebarPreviewPane->hasSnapshot();
    m_sidebarStrip->setGeometry(direction > 0 ? m_sidebarSwipeOffset : m_sidebarSwipeOffset - width,
                                0, width * 2, bounds.height());
    if (direction > 0) {
        m_sidebarPage->setGeometry(0, 0, width, bounds.height());
        if (m_sidebarPreviewPage) m_sidebarPreviewPage->setGeometry(width, 0, width, bounds.height());
    } else {
        if (m_sidebarPreviewPage) m_sidebarPreviewPage->setGeometry(0, 0, width, bounds.height());
        m_sidebarPage->setGeometry(width, 0, width, bounds.height());
    }
    if (m_sidebarPreviewPage) {
        if (previewAvailable) {
            // Parallax: the incoming page lags ~25% behind the live one and
            // fades in over the first half of the travel.
            const qreal t = qBound(0.0, qreal(qAbs(m_sidebarSwipeOffset)) / qreal(width), 1.0);
            const int lag = qRound((1.0 - t) * width * 0.25) * (direction > 0 ? 1 : -1);
            m_sidebarPreviewPane->setReveal(qMin(1.0, 0.35 + t * 1.3), lag);
            if (!m_sidebarPreviewPage->isVisible()) m_sidebarPreviewPage->show();
        } else {
            // Rubber-banding past the last profile: nothing to show.
            m_sidebarPreviewPage->hide();
        }
    }
    updatePagerDots();
}

void BrowserWindow::resetProfileSwipeState() {
    m_sidebarSwipeActive = false;
    m_sidebarSwipeSettling = false;
    m_sidebarSwipeGestureOpen = false;
    m_sidebarSwipeHapticFired = false;
    m_sidebarSwipeAxis = 0;
    m_sidebarSwipeAxisDx = 0;
    m_sidebarSwipeAxisDy = 0;
    m_profileSwipeRemainder = 0;
    m_sidebarSwipeDirection = 0;
    m_sidebarSwipeSamples.clear();
    m_sidebarPreviewProfile.clear();
    if (m_sidebarPreviewPane) m_sidebarPreviewPane->clearSnapshot();
}

void BrowserWindow::settleSidebarSwipe(bool commit) {
    if (m_sidebarSwipeSettling) return;
    if (m_sidebarSwipeAnim) {
        m_sidebarSwipeAnim->stop();
        m_sidebarSwipeAnim->deleteLater();
        m_sidebarSwipeAnim = nullptr;
    }
    const int width = m_sidebarViewport ? qMax(1, m_sidebarViewport->width()) : 240;
    const int startOffset = m_sidebarSwipeOffset;
    const int direction = m_sidebarSwipeDirection != 0 ? m_sidebarSwipeDirection : (startOffset < 0 ? 1 : -1);
    const QStringList list = orderedProfiles();
    const int current = qMax(0, list.indexOf(m_profiles.currentName()));
    const int next = qBound(0, current + direction, list.size() - 1);
    if (commit && (next == current || next < 0 || next >= list.size())) commit = false;
    if (commit && !(m_sidebarPreviewPane && m_sidebarPreviewPane->hasSnapshot())) updateSidebarPreview(direction);
    const int endOffset = commit ? (direction > 0 ? -width : width) : 0;
    const int remaining = qAbs(endOffset - startOffset);
    if (remaining == 0) {
        if (commit) {
            saveSessionForProfile(m_profiles.currentName());
            m_profiles.setCurrentProfile(list.at(next));
        }
        resetProfileSwipeState();
        setSidebarSwipeOffset(0);
        return;
    }
    // Duration scales with what is left to travel, clamped to 140..260 ms.
    const int duration = qBound(kProfileSwipeSettleMinMs,
                                qRound(qreal(remaining) / qreal(width) * kProfileSwipeSettleMaxMs),
                                kProfileSwipeSettleMaxMs);
    auto *driver = new QVariantAnimation(this);
    m_sidebarSwipeAnim = driver;
    driver->setStartValue(startOffset);
    driver->setEndValue(endOffset);
    driver->setDuration(duration);
    driver->setEasingCurve(QEasingCurve::OutCubic);
    m_sidebarSwipeSettling = true;
    m_sidebarSwipeGestureOpen = false;
    connect(driver, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
        setSidebarSwipeOffset(value.toInt());
    });
    connect(driver, &QVariantAnimation::finished, this, [this, driver, commit, list, next] {
        if (m_sidebarSwipeAnim != driver) return;
        m_sidebarSwipeAnim = nullptr;
        driver->deleteLater();
        // The profile only changes once the settle animation has landed.
        if (commit && next >= 0 && next < list.size()) {
            saveSessionForProfile(m_profiles.currentName());
            m_profiles.setCurrentProfile(list.at(next));
        }
        resetProfileSwipeState();
        setSidebarSwipeOffset(0);
    });
    driver->start();
}

bool BrowserWindow::handleProfileSwipeWheel(QWheelEvent *wheel) {
    // Phase-driven trackpad gesture:
    //   ScrollBegin  -> open a gesture (nothing moves yet)
    //   ScrollUpdate -> 1:1 horizontal pixelDelta().x() once axis-locked
    //   ScrollEnd    -> commit (>=30% travel or flick) or cancel
    // ScrollMomentum and NoScrollPhase (legacy mouse wheels) are ignored and
    // can never open or move a gesture.
    const Qt::ScrollPhase phase = wheel->phase();
    if (phase == Qt::ScrollMomentum || phase == Qt::NoScrollPhase) {
        // Let vertical momentum keep scrolling the tab tree; swallow only
        // horizontal momentum so it can't leak into a neighbouring scroller.
        return m_sidebarSwipeActive && qAbs(wheel->pixelDelta().x()) > qAbs(wheel->pixelDelta().y());
    }
    if (m_sidebarSwipeSettling) return m_sidebarSwipeAxis > 0;

    if (phase == Qt::ScrollBegin) {
        m_sidebarSwipeGestureOpen = true;
        m_sidebarSwipeAxis = 0;
        m_sidebarSwipeAxisDx = 0;
        m_sidebarSwipeAxisDy = 0;
        m_sidebarSwipeHapticFired = false;
        m_sidebarSwipeSamples.clear();
        m_profileSwipeRemainder = 0;
        m_sidebarSwipeDirection = 0;
        return false;
    }
    if (!m_sidebarSwipeGestureOpen) return false;

    if (phase == Qt::ScrollUpdate) {
        const QPoint pixel = wheel->pixelDelta();
        if (m_sidebarSwipeAxis == 0) {
            m_sidebarSwipeAxisDx += pixel.x();
            m_sidebarSwipeAxisDy += pixel.y();
            if (qAbs(m_sidebarSwipeAxisDx) < kProfileSwipeAxisLockPx && qAbs(m_sidebarSwipeAxisDy) < kProfileSwipeAxisLockPx) return false;
            if (qAbs(m_sidebarSwipeAxisDy) >= qAbs(m_sidebarSwipeAxisDx)) {
                // Vertical: this gesture belongs to the tab tree.
                m_sidebarSwipeAxis = -1;
                return false;
            }
            m_sidebarSwipeAxis = 1;
            m_sidebarSwipeActive = true;
            m_profileSwipeRemainder = m_sidebarSwipeAxisDx;
        } else if (m_sidebarSwipeAxis < 0) {
            return false;
        } else {
            m_profileSwipeRemainder += pixel.x();
        }
        const qint64 now = nowMs();
        m_sidebarSwipeSamples.append({now, pixel.x()});
        while (!m_sidebarSwipeSamples.isEmpty() && now - m_sidebarSwipeSamples.first().ms > kProfileSwipeVelocityWindowMs) {
            m_sidebarSwipeSamples.removeFirst();
        }

        const int width = m_sidebarViewport ? qMax(1, m_sidebarViewport->width()) : 240;
        const int intendedDirection = m_profileSwipeRemainder < 0 ? 1 : (m_profileSwipeRemainder > 0 ? -1 : 0);
        if (intendedDirection != 0 && intendedDirection != m_sidebarSwipeDirection) {
            m_sidebarSwipeDirection = intendedDirection;
            m_sidebarPreviewProfile.clear();
            if (m_sidebarPreviewPane) m_sidebarPreviewPane->clearSnapshot();
            m_sidebarSwipeHapticFired = false;
            updateSidebarPreview(intendedDirection);
        }
        const QStringList list = orderedProfiles();
        const int profileIndex = qMax(0, list.indexOf(m_profiles.currentName()));
        const int targetIndex = profileIndex + m_sidebarSwipeDirection;
        const bool hasTarget = m_sidebarSwipeDirection != 0 && targetIndex >= 0 && targetIndex < list.size();
        int displayed = 0;
        if (hasTarget) {
            const int magnitude = qAbs(m_profileSwipeRemainder);
            // Past a full page the extra travel is rubber-banded so the
            // preview never overshoots into empty space.
            displayed = magnitude <= width ? magnitude : width + rubberBand(magnitude - width, width / 8);
            displayed = qMin(displayed, width);
            displayed *= (m_profileSwipeRemainder < 0 ? -1 : 1);
            const int threshold = qRound(width * kProfileSwipeCommitFraction);
            if (!m_sidebarSwipeHapticFired && magnitude >= threshold) {
                m_sidebarSwipeHapticFired = true;
                mac::performHapticFeedback();
            }
        } else {
            // First/last profile: resist, never wrap.
            displayed = rubberBand(m_profileSwipeRemainder, width / 4);
        }
        setSidebarSwipeOffset(displayed);
        return true;
    }

    if (phase == Qt::ScrollEnd) {
        m_sidebarSwipeGestureOpen = false;
        if (m_sidebarSwipeAxis <= 0 || !m_sidebarSwipeActive) {
            resetProfileSwipeState();
            return false;
        }
        const int width = m_sidebarViewport ? qMax(1, m_sidebarViewport->width()) : 240;
        const int magnitude = qAbs(m_profileSwipeRemainder);
        // Velocity from the recent, timestamped samples (px/s).
        double velocity = 0.0;
        if (m_sidebarSwipeSamples.size() >= 2) {
            const qint64 span = m_sidebarSwipeSamples.last().ms - m_sidebarSwipeSamples.first().ms;
            int sum = 0;
            for (const SwipeSample &s : m_sidebarSwipeSamples) sum += s.dx;
            if (span > 0) velocity = sum * 1000.0 / double(span);
        }
        const bool sameDirection = (velocity < 0 && m_profileSwipeRemainder < 0) || (velocity > 0 && m_profileSwipeRemainder > 0);
        const bool flick = sameDirection && qAbs(velocity) >= kProfileSwipeFlickVelocity && magnitude >= kProfileSwipeMinFlickTravel;
        const bool commit = magnitude >= qRound(width * kProfileSwipeCommitFraction) || flick;
        settleSidebarSwipe(commit);
        return true;
    }
    return false;
}

void BrowserWindow::animateProfileSwitcher(int direction) {
    if (!m_profileBtn) return;
    if (m_profileAnim) {
        m_profileAnim->stop();
        m_profileAnim->deleteLater();
        m_profileAnim = nullptr;
    }
    const QRect end = m_profileBtn->geometry();
    QRect start = end.translated(direction > 0 ? 18 : -18, 0);
    m_profileBtn->setGeometry(start);
    m_profileAnim = new QPropertyAnimation(m_profileBtn, "geometry", this);
    m_profileAnim->setDuration(155);
    m_profileAnim->setStartValue(start);
    m_profileAnim->setEndValue(end);
    m_profileAnim->setEasingCurve(responsiveEaseOut());
    QPropertyAnimation *anim = m_profileAnim;
    connect(anim, &QPropertyAnimation::finished, anim, &QObject::deleteLater);
    connect(anim, &QObject::destroyed, this, [this, anim] { if (m_profileAnim == anim) m_profileAnim = nullptr; });
    anim->start();
}

void BrowserWindow::showProfileMenu() {
    if (!m_profileBtn) return;
    if (mac::showNativeProfilePopover(m_profileBtn, m_profiles)) return;
    QMenu menu(this);
    QStringList list = m_profiles.profiles();
    list.removeDuplicates();
    const int defaultIndex = list.indexOf("Default");
    if (defaultIndex > 0) list.move(defaultIndex, 0);
    for (const QString &name : list) {
        auto *action = menu.addAction(mac::sfSymbolIcon(m_profiles.iconName(name), 13.0, m_theme.foreground), name);
        action->setCheckable(true);
        action->setChecked(name == m_profiles.currentName());
        connect(action, &QAction::triggered, this, [this, name] {
            saveSessionForProfile(m_profiles.currentName());
            m_profiles.setCurrentProfile(name);
        });
    }
    menu.addSeparator();
    menu.addAction("Manage Profiles…", this, &BrowserWindow::showSettings);
    menu.exec(m_profileBtn->mapToGlobal(QPoint(0, m_profileBtn->height() + 2)));
}

void BrowserWindow::showExtensionsMenu() {
    if (!m_extensionsBtn) return;
    const QString extensionDir = ChromeExtensionManager::extensionDirectory();
    const QStringList paths = ChromeExtensionManager::configuredPaths();
    auto openExtensionFolder = [extensionDir] {
        QDesktopServices::openUrl(QUrl::fromLocalFile(extensionDir));
    };
    auto addUnpackedExtension = [this] {
        const QString dir = QFileDialog::getExistingDirectory(this, "Choose unpacked Chrome extension", ChromeExtensionManager::extensionDirectory());
        if (dir.isEmpty()) return;
        QStringList paths = QSettings().value("extensions/unpackedPaths").toStringList();
        const QString path = QDir::cleanPath(dir);
        if (!paths.contains(path)) paths << path;
        ChromeExtensionManager::setConfiguredPaths(paths);
        ChromeExtensionManager::nativeController();
    };

    QStringList nativeTitles;
    QVector<bool> nativeEnabled;
    std::vector<std::function<void()>> nativeCallbacks;
    if (paths.isEmpty()) {
        nativeTitles << QStringLiteral("No unpacked extensions loaded");
        nativeEnabled << false;
        nativeCallbacks.push_back([] {});
    } else {
        for (const QString &path : paths) {
            const QString label = QFileInfo(path).fileName().isEmpty() ? path : QFileInfo(path).fileName();
            nativeTitles << label;
            nativeEnabled << true;
            nativeCallbacks.push_back([path] {
                QDesktopServices::openUrl(QUrl::fromLocalFile(path));
            });
        }
    }
    nativeTitles << QStringLiteral("-") << QStringLiteral("Open Extensions Folder") << QStringLiteral("Add Unpacked Extension…") << QStringLiteral("Manage Extensions…");
    nativeEnabled << true << true << true << true;
    nativeCallbacks.push_back(openExtensionFolder);
    nativeCallbacks.push_back(addUnpackedExtension);
    nativeCallbacks.push_back([this] { showSettings(); });
    if (mac::showNativeContextMenu(m_extensionsBtn, m_extensionsBtn->mapToGlobal(QPoint(m_extensionsBtn->width() / 2, m_extensionsBtn->height())), nativeTitles, nativeEnabled, std::move(nativeCallbacks))) return;

    QMenu menu(this);
    if (paths.isEmpty()) {
        QAction *empty = menu.addAction("No unpacked extensions loaded");
        empty->setEnabled(false);
    } else {
        for (const QString &path : paths) {
            const QString label = QFileInfo(path).fileName().isEmpty() ? path : QFileInfo(path).fileName();
            QAction *action = menu.addAction(mac::sfSymbolIcon("puzzlepiece.extension", 13.0, m_theme.foreground), label);
            action->setToolTip(path);
            connect(action, &QAction::triggered, this, [path] {
                QDesktopServices::openUrl(QUrl::fromLocalFile(path));
            });
        }
    }
    menu.addSeparator();
    menu.addAction("Open Extensions Folder", this, openExtensionFolder);
    menu.addAction("Add Unpacked Extension…", this, addUnpackedExtension);
    menu.addAction("Manage Extensions…", this, &BrowserWindow::showSettings);
    menu.exec(m_extensionsBtn->mapToGlobal(QPoint(0, m_extensionsBtn->height() + 2)));
}

QUrl BrowserWindow::urlFromInput(const QString &input) const {
    const QString trimmed = input.trimmed();
    if (trimmed.isEmpty()) return QUrl(m_homePage);
    if (trimmed.startsWith(QStringLiteral("pocb://"), Qt::CaseInsensitive)) return QUrl(trimmed);
    const bool hasScheme = trimmed.contains("://");
    const bool isLocalhost = trimmed.startsWith("localhost") || trimmed.startsWith("127.") || trimmed.startsWith("[::1]");
    const bool looksLikeHost = trimmed.contains('.') || isLocalhost || trimmed.startsWith("http://") || trimmed.startsWith("https://");
    if (looksLikeHost) {
        const QString navigable = (!hasScheme && !isLocalhost) ? QStringLiteral("https://") + trimmed : trimmed;
        QUrl url = QUrl::fromUserInput(navigable);
        if (url.isValid()) return url;
    }
    return QUrl(m_searchEngine.arg(QString::fromUtf8(QUrl::toPercentEncoding(trimmed))));
}

WebView *BrowserWindow::currentView() const {
    return m_tabTree ? m_tabTree->currentView() : nullptr;
}

bool BrowserWindow::handleInternalUrl(const QUrl &url) {
    if (url.scheme() != QStringLiteral("pocb")) return false;
    const QString command = url.host().toLower();
    if (command == QStringLiteral("settings")) {
        showSettings();
    } else if (command == QStringLiteral("passkeys")) {
        if (auto *v = currentView()) v->loadHtml(passkeyDiagnosticsHtml());
    } else if (command == QStringLiteral("close-sidebar")) {
        if (m_sidebar) m_sidebar->setHidden(true);
    } else if (command == QStringLiteral("toggle-sidebar")) {
        if (m_sidebar && m_sidebarWidget) m_sidebar->setHidden(m_sidebarWidget->isVisible());
    } else if (command == QStringLiteral("new-tab")) {
        openBlankTabForLocationEntry();
    } else if (command == QStringLiteral("close-tab")) {
        if (m_tabTree) m_tabTree->closeCurrent();
    } else if (command == QStringLiteral("copy-url")) {
        if (auto *v = currentView()) {
            QApplication::clipboard()->setText(v->url().toString());
            showCopiedLinkPopup();
        }
    } else if (command == QStringLiteral("reopen-closed-tab")) {
        reopenLastClosedTab();
    } else if (command == QStringLiteral("toggle-bookmark")) {
        if (auto *v = currentView()) {
            if (m_bookmarks.contains(m_profiles.currentName(), v->url())) m_bookmarks.removeBookmark(m_profiles.currentName(), v->url());
            else m_bookmarks.addBookmark(m_profiles.currentName(), v->title(), v->url());
            refreshFloatingOmniboxItems();
        }
    } else if (command == QStringLiteral("switch-tab")) {
        bool ok = false;
        const quintptr ptr = url.query().toULongLong(&ok, 16);
        if (ok && m_tabTree) m_tabTree->selectView(reinterpret_cast<WebView *>(ptr));
    }
    return true;
}

QString BrowserWindow::passkeyDiagnosticsHtml() const {
    return QStringLiteral(R"HTML(<!doctype html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>pocb passkeys</title>
<style>
:root { color-scheme: dark; font-family: -apple-system, BlinkMacSystemFont, "SF Pro Text", sans-serif; background: #101010; color: #f4f0ea; }
body { margin: 0; padding: 48px; }
main { max-width: 860px; margin: 0 auto; }
h1 { font-size: 42px; letter-spacing: -0.04em; margin: 0 0 12px; }
p { color: #bdb6aa; line-height: 1.55; }
.card { border: 1px solid rgba(255,255,255,.12); border-radius: 18px; padding: 22px; margin: 18px 0; background: rgba(255,255,255,.04); }
.row { display: flex; justify-content: space-between; gap: 16px; padding: 12px 0; border-bottom: 1px solid rgba(255,255,255,.08); }
.row:last-child { border-bottom: 0; }
.value { font-family: ui-monospace, SFMono-Regular, Menlo, monospace; color: #fff; text-align: right; }
.good { color: #94f0ba; }
.bad { color: #ff9d8f; }
.warn { color: #ffd479; }
button { appearance: none; border: 0; border-radius: 999px; padding: 12px 18px; background: #f4f0ea; color: #101010; font-weight: 700; }
code { color: #fff; }
</style>
</head>
<body>
<main>
<h1>Passkey diagnostics</h1>
<p>This page checks what websites can see inside pocb. Proper browser passkey support requires a signed app with Apple's browser public-key credential entitlement.</p>
<div class="card" id="results"></div>
<div class="card">
<p>For local web tests, use <code>http://localhost:PORT</code>, expected origin <code>http://localhost:PORT</code>, and RP ID <code>localhost</code>. A credential created for localhost will not work on 127.0.0.1, LAN IPs, tunnel domains, or production domains.</p>
<button id="copy">Copy required entitlement</button>
</div>
</main>
<script>
const rows = [];
const add = (name, value, cls) => rows.push(`<div class="row"><span>${name}</span><span class="value ${cls || ''}">${value}</span></div>`);
(async () => {
  add('Origin', location.origin);
  add('Secure context', String(window.isSecureContext), window.isSecureContext ? 'good' : 'warn');
  add('navigator.credentials', String(!!navigator.credentials), navigator.credentials ? 'good' : 'bad');
  add('PublicKeyCredential', String(!!window.PublicKeyCredential), window.PublicKeyCredential ? 'good' : 'bad');
  if (window.PublicKeyCredential?.isUserVerifyingPlatformAuthenticatorAvailable) {
    try {
      const available = await PublicKeyCredential.isUserVerifyingPlatformAuthenticatorAvailable();
      add('Platform authenticator', String(available), available ? 'good' : 'warn');
    } catch (error) {
      add('Platform authenticator', error.name || String(error), 'bad');
    }
  } else {
    add('Platform authenticator', 'probe unavailable', 'bad');
  }
  if (window.PublicKeyCredential?.isConditionalMediationAvailable) {
    try {
      const available = await PublicKeyCredential.isConditionalMediationAvailable();
      add('Conditional mediation', String(available), available ? 'good' : 'warn');
    } catch (error) {
      add('Conditional mediation', error.name || String(error), 'bad');
    }
  } else {
    add('Conditional mediation', 'probe unavailable', 'warn');
  }
  add('Required app entitlement', 'com.apple.developer.web-browser.public-key-credential', 'warn');
  document.getElementById('results').innerHTML = rows.join('');
})();
document.getElementById('copy').addEventListener('click', async () => {
  await navigator.clipboard.writeText('com.apple.developer.web-browser.public-key-credential');
});
</script>
</body>
</html>)HTML");
}

QStringList BrowserWindow::restoredSessionForProfile(const QString &profileName) const {
    return QSettings().value(QStringLiteral("sessions/%1/urls").arg(profileName)).toStringList();
}

void BrowserWindow::saveSessionForProfile(const QString &profileName) const {
    if (!m_tabTree || profileName.trimmed().isEmpty()) return;
    QSettings().setValue(QStringLiteral("sessions/%1/urls").arg(profileName), m_tabTree->sessionEntries());
}

void BrowserWindow::showArchiveMenu(QWidget *anchor) {
    QMenu menu(this);
    bool any = false;
    for (const ClosedTab &tab : m_closedTabs) {
        if (!tab.url.isValid() || tab.url.isEmpty()) continue;
        any = true;
        const QString title = tab.title.isEmpty() ? tab.url.toString() : tab.title;
        const QUrl url = tab.url;
        const QPixmap favicon = m_favicons ? m_favicons->cached(url) : QPixmap();
        const QIcon icon = favicon.isNull() ? mac::sfSymbolIcon("globe", 13.0, m_theme.muted) : QIcon(favicon);
        menu.addAction(icon, QFontMetrics(menu.font()).elidedText(title, Qt::ElideRight, 320), this, [this, url] {
            if (m_tabTree) m_tabTree->reopenUrl(url);
            refreshFloatingOmniboxItems();
        });
    }
    if (!any) menu.addAction(QStringLiteral("No archived tabs"))->setEnabled(false);
    else {
        menu.addSeparator();
        menu.addAction(QStringLiteral("Clear Archive"), this, [this] {
            m_closedTabs.clear();
            refreshFloatingOmniboxItems();
        });
    }
    const QPoint pos = anchor ? anchor->mapToGlobal(QPoint(0, anchor->height() + 4)) : QCursor::pos();
    menu.exec(pos);
}

void BrowserWindow::reopenLastClosedTab() {
    while (!m_closedTabs.isEmpty()) {
        const ClosedTab tab = m_closedTabs.takeFirst();
        if (!tab.url.isValid() || tab.url.isEmpty()) continue;
        if (m_tabTree) m_tabTree->reopenUrl(tab.url);
        refreshFloatingOmniboxItems();
        return;
    }
}

void BrowserWindow::rememberCurrentPage() {
    auto *view = currentView();
    if (!view) return;
    const QUrl url = view->url();
    if (!url.isValid() || url.isEmpty() || url.scheme() == QStringLiteral("about") || url.scheme() == QStringLiteral("data")) return;
    const QString title = view->title().isEmpty() ? url.toString() : view->title();
    for (int i = m_recentPages.size() - 1; i >= 0; --i) {
        if (m_recentPages.at(i).url == url) m_recentPages.removeAt(i);
    }
    m_recentPages.prepend({title, url});
    while (m_recentPages.size() > 25) m_recentPages.removeLast();
    refreshFloatingOmniboxItems();
}

void BrowserWindow::refreshFloatingOmniboxItems() {
    QList<FloatingOmnibox::LocalItem> items;
    QList<AddressBarController::LocalItem> addressItems;
    auto addCommand = [this, &items](const QString &title, const QString &url, const QString &symbol) {
        items.append({title, url, mac::sfSymbolIcon(symbol, 13.0, m_theme.foreground), false});
    };
    addCommand("Command · Settings", "pocb://settings", "gearshape");
    addCommand("Command · Close Sidebar", "pocb://close-sidebar", "sidebar.left");
    addCommand("Command · Toggle Sidebar", "pocb://toggle-sidebar", "sidebar.left");
    addCommand("Command · New Tab", "pocb://new-tab", "plus");
    addCommand("Command · Close Tab", "pocb://close-tab", "xmark");
    addCommand("Command · Reopen Closed Tab", "pocb://reopen-closed-tab", "arrow.uturn.backward");
    addCommand("Command · Toggle Bookmark", "pocb://toggle-bookmark", "star");
    addCommand("Command · Copy Current URL", "pocb://copy-url", "link");
    auto iconForUrl = [this](const QUrl &url) {
        if (m_favicons) {
            if (const QPixmap pm = m_favicons->cached(url); !pm.isNull()) return QIcon(pm);
            m_favicons->request(url);
        }
        return mac::sfSymbolIcon("globe", 13.0, m_theme.muted);
    };
    auto addUrlItem = [&items, &addressItems, &iconForUrl](const QString &title, const QUrl &url) {
        if (!url.isValid() || url.isEmpty() || url.scheme() == QStringLiteral("about") || url.scheme() == QStringLiteral("data")) return;
        items.append({QStringLiteral("Page · ") + (title.isEmpty() ? url.toString() : title), url.toString(), iconForUrl(url), false});
        addressItems.append({title.isEmpty() ? url.toString() : title, url.toString()});
    };
    const QList<WebView *> liveTabs = m_tabTree ? m_tabTree->views() : QList<WebView *>();
    for (int i = m_tabRecency.size() - 1; i >= 0; --i) {
        if (!liveTabs.contains(m_tabRecency.at(i))) m_tabRecency.removeAt(i);
    }
    QList<WebView *> orderedTabs = m_tabRecency;
    for (auto *view : liveTabs) {
        if (view && !orderedTabs.contains(view)) orderedTabs.append(view);
    }
    int defaultTabCount = 0;
    for (auto *view : orderedTabs) {
        if (!view || view == currentView()) continue;
        const QUrl url = view->url();
        if (!url.isValid() || url.isEmpty() || url.scheme() == QStringLiteral("about") || url.scheme() == QStringLiteral("data")) continue;
        const QString title = view->title().isEmpty() ? url.toString() : view->title();
        items.append({QStringLiteral("Tab · ") + title, QStringLiteral("pocb://switch-tab?") + QString::number(reinterpret_cast<quintptr>(view), 16), iconForUrl(url), defaultTabCount < 3});
        ++defaultTabCount;
    }
    for (const ClosedTab &tab : m_closedTabs) {
        if (tab.url.isValid() && !tab.url.isEmpty()) items.append({QStringLiteral("Closed · ") + (tab.title.isEmpty() ? tab.url.toString() : tab.title), tab.url.toString(), mac::sfSymbolIcon("arrow.uturn.backward", 13.0, m_theme.muted), false});
    }
    for (const Bookmark &bookmark : m_bookmarks.bookmarks(m_profiles.currentName())) {
        items.append({QStringLiteral("Bookmark · ") + (bookmark.title.isEmpty() ? bookmark.url.toString() : bookmark.title), bookmark.url.toString(), iconForUrl(bookmark.url), false});
        addressItems.append({bookmark.title.isEmpty() ? bookmark.url.toString() : bookmark.title, bookmark.url.toString()});
    }
    for (const RecentPage &page : m_recentPages) {
        addUrlItem(page.title, page.url);
    }
    if (m_floatingOmnibox) m_floatingOmnibox->setLocalItems(items);
    if (m_addressBarCtl) m_addressBarCtl->setLocalItems(addressItems);
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
        const QUrl url = urlFromInput(text);
        if (handleInternalUrl(url)) return;
        m_omnibox->setText(text);
        if (auto *view = currentView()) {
            const QUrl alternate = alternateNavUrlFor(text);
            view->setProperty("alternateNavUrl", alternate.isValid() && url != alternate ? alternate.toString() : QString());
            view->load(url);
        }
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
    m_sidebarWidget = sidebar;
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

    m_addrInSidebar = QSettings().value("ui/addressBarInSidebar", false).toBool();
    sidebar->setMinimumWidth(m_addrInSidebar ? ui::metrics::SidebarHeaderMinimumWidth
                                             : ui::metrics::SidebarMinimumWidth);
    sidebar->setMaximumWidth(ui::metrics::SidebarMaximumWidth);

    auto *stackHost = new QWidget(m_splitter);
    stackHost->setObjectName("StackHost");
    stackHost->setStyleSheet("QWidget#StackHost { background: transparent; }");
    stackHost->setAttribute(Qt::WA_TranslucentBackground);
    auto *hostLayout = new QVBoxLayout(stackHost);
    hostLayout->setContentsMargins(ui::metrics::stackHostMargins(/*sidebarVisible=*/true));
    hostLayout->setSpacing(0);

    // The web container holds the top toolbar AND the web view stack inside
    // a single rounded shape, so the toolbar lives "inside" the rounded card
    // pushing the page content down.
    m_webContainer = new QWidget(stackHost);
    m_webContainer->setObjectName("WebContainer");
    const bool useLiquidGlass = QSettings().value("ui/useLiquidGlass", true).toBool();
    m_webContainer->setStyleSheet(QString(
        "QWidget#WebContainer { background: %1; border: none; border-radius: %2px; }")
        .arg(useLiquidGlass ? QStringLiteral("transparent") : QStringLiteral("rgba(26, 26, 26, 180)"))
        .arg(ui::metrics::WebContainerRadius));
    if (useLiquidGlass) m_webContainer->setAttribute(Qt::WA_TranslucentBackground);
    auto *containerLayout = new QVBoxLayout(m_webContainer);
    containerLayout->setContentsMargins(0, 0, 0, 0);
    containerLayout->setSpacing(0);

    // Toolbar row + hairline live inside one host whose fixed height is the
    // only thing that animates when the toolbar collapses (see
    // ui::CollapsingToolbarHost).
    m_toolbarHost = new ui::CollapsingToolbarHost(m_webContainer);
    m_topbar = buildTopbar(m_toolbarHost);

    // Fixed thin hairline between toolbar and the page.
    m_topSeparator = new QWidget(m_toolbarHost);
    m_topSeparator->setObjectName("WebTopSeparator");
    m_topSeparator->setFixedHeight(1);
    m_topSeparator->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_topSeparator->setStyleSheet(QSettings().value("ui/useLiquidGlass", true).toBool()
                                      ? QStringLiteral("QWidget#WebTopSeparator { background: transparent; }")
                                      : QStringLiteral("QWidget#WebTopSeparator { background: rgba(255,255,255,0.08); }"));
    m_toolbarHost->setRow(m_topbar, m_topSeparator);
    m_toolbarHost->setAttribute(Qt::WA_TranslucentBackground);
    m_toolbarHost->setAttribute(Qt::WA_NativeWindow);

    m_toolbarGrabber = new ui::ToolbarGrabber(this);
    m_toolbarGrabber->setPillColor(m_theme.foreground);
    connect(m_toolbarGrabber, &ui::ToolbarGrabber::hovered, this, &BrowserWindow::expandToolbar);

    m_stack = new QWidget(m_webContainer);
    auto *stackLayout = new QStackedLayout(m_stack);
    stackLayout->setContentsMargins(0, 0, 0, 0);
    m_stack->setAttribute(Qt::WA_NativeWindow);
    containerLayout->addWidget(m_stack, 1);
    m_toolbarHost->raise();
    m_webContainer->installEventFilter(this);

    // Translucent rounded panel that hosts the tab tree and, pinned below
    // it, the profile avatar + pager dots.
    auto *sidebarPanel = new ui::SidebarPanel(m_theme, sidebar);
    sidebarPanel->setObjectName("SidebarPanel");
    sidebarPanel->setRadius(ui::metrics::SidebarPanelRadius);
    m_sidebarContent = sidebarPanel;
    auto *panelLayout = new QVBoxLayout(sidebarPanel);
    panelLayout->setContentsMargins(ui::metrics::SidebarPanelPadding, ui::metrics::SidebarPanelPadding,
                                    ui::metrics::SidebarPanelPadding, ui::metrics::SidebarPanelPadding);
    panelLayout->setSpacing(0);

    m_sidebarViewport = new QWidget(sidebarPanel);
    m_sidebarViewport->setObjectName("SidebarViewport");
    m_sidebarViewport->setAttribute(Qt::WA_TranslucentBackground);
    m_sidebarViewport->setStyleSheet("QWidget#SidebarViewport { background: transparent; }");
    m_sidebarViewport->installEventFilter(this);
    m_sidebarStrip = new QWidget(m_sidebarViewport);
    m_sidebarStrip->setObjectName("SidebarStrip");
    m_sidebarStrip->setAttribute(Qt::WA_TranslucentBackground);
    m_sidebarStrip->setStyleSheet("QWidget#SidebarStrip { background: transparent; }");

    m_sidebarPage = new QWidget(m_sidebarStrip);
    m_sidebarPage->setObjectName("SidebarPage");
    m_sidebarPage->setAttribute(Qt::WA_TranslucentBackground);
    m_sidebarPage->setStyleSheet("QWidget#SidebarPage { background: transparent; }");
    auto *pageLayout = new QVBoxLayout(m_sidebarPage);
    pageLayout->setContentsMargins(0, 0, 0, 0);
    pageLayout->setSpacing(0);

    // TabTree owns the sidebar list + WebView lifetime, but the QStackedLayout
    // host (`m_stack`) and the surrounding sidebar chrome stay here.
    m_tabTree = new TabTree(m_profiles, m_favicons, m_stack, m_theme, m_sidebarPage, this);
    m_tabTree->setHomePage(m_homePage);
    m_tabTree->setCloseWindowWithLastTab(QSettings().value("browser/closeWindowWithLastTab", false).toBool());
    connect(m_tabTree, &TabTree::tabDetachRequested, this, &BrowserWindow::detachTabToWindow);
    connect(m_tabTree, &TabTree::tabSplitRequested, this, &BrowserWindow::splitTabs);
    connect(m_tabTree, &TabTree::tabSplitPreviewRequested, this, &BrowserWindow::showSplitPreview);
    connect(m_tabTree, &TabTree::tabSplitPreviewEnded, this, &BrowserWindow::hideSplitPreview);
    connect(m_tabTree, &TabTree::lastTabCloseRequested, this, &BrowserWindow::close);
    {
        auto *addTabRow = new ui::SidebarActionRow(m_theme, mac::sfSymbolIcon("plus", 14.0, m_theme.foreground),
                                                    QStringLiteral("Add Tab"), m_sidebarPage);
        connect(addTabRow, &QToolButton::clicked, this, [this] { openBlankTabForLocationEntry(); });
        pageLayout->addWidget(addTabRow, 0);
    }
    pageLayout->addWidget(m_tabTree->widget(), 1);
    panelLayout->addWidget(m_sidebarViewport, 1);
    m_profileSwitcher = buildProfileSwitcher(sidebarPanel);
    panelLayout->addWidget(m_profileSwitcher, 0);
    sideLayout->addWidget(sidebarPanel, 1);
    m_sidebarPreviewPage = new QWidget(m_sidebarStrip);
    m_sidebarPreviewPage->setObjectName("SidebarPreviewPage");
    m_sidebarPreviewPage->setAttribute(Qt::WA_TranslucentBackground);
    m_sidebarPreviewPage->setStyleSheet("QWidget#SidebarPreviewPage { background: transparent; }");
    auto *previewLayout = new QVBoxLayout(m_sidebarPreviewPage);
    previewLayout->setContentsMargins(0, 0, 0, 0);
    m_sidebarPreviewPane = new ui::SidebarPreviewPane(m_sidebarPreviewPage);
    previewLayout->addWidget(m_sidebarPreviewPane, 1);
    m_sidebarPreviewPage->hide();
    m_sidebarPage->setGeometry(QRect(QPoint(0, 0), m_sidebarViewport->size()));
    m_sidebarPreviewPage->setGeometry(QRect(QPoint(m_sidebarViewport->width(), 0), m_sidebarViewport->size()));

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
    m_sidebar->setSidebarContent(m_sidebarContent, sideLayout);

    if (m_addrInSidebar) {
        // Drop the toolbar entirely; the address pill + nav buttons live in
        // the sidebar instead.
        if (m_topbar) m_topbar->hide();
        if (m_topSeparator) m_topSeparator->hide();
        setToolbarRowVisible(false);

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

        m_sidebarHeader = new QWidget(m_sidebarPage);
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
            if (auto *oldLayout = m_addrWrap->parentWidget() ? m_addrWrap->parentWidget()->layout() : nullptr) {
                oldLayout->removeWidget(m_addrWrap);
            }
            m_addrWrap->setParent(m_sidebarHeader);
            m_addrWrap->setFixedHeight(36);
            if (auto *pill = qobject_cast<ui::AddrPill *>(m_addrWrap)) {
                pill->setRadius(8);
            }
            if (auto *row = qobject_cast<QHBoxLayout *>(m_addrWrap->layout())) {
                row->setContentsMargins(10, 0, 10, 0);
                row->setSpacing(8);
                row->setSizeConstraint(QLayout::SetNoConstraint);
            }
            m_addrWrap->setMinimumHeight(36);
            m_addrWrap->setMaximumHeight(36);
            if (m_searchIcon) {
                m_searchIcon->setFixedSize(18, 18);
                m_searchIcon->setPixmap(mac::sfSymbolIcon("magnifyingglass", 13.5, m_theme.muted).pixmap(18, 18));
            }
            if (m_lockIcon) m_lockIcon->setFixedSize(18, 18);
            if (m_pillMenuBtn) {
                m_pillMenuBtn->setFixedSize(24, 24);
                m_pillMenuBtn->setIconSize(QSize(16, 16));
                m_pillMenuBtn->setIcon(mac::sfSymbolIcon("ellipsis.circle", 14.0, m_theme.foreground));
            }
            if (m_addressBar) {
                m_addressBar->setFixedHeight(28);
                m_addressBar->setStyleSheet(QString(
                    "QLineEdit {"
                    "  background: transparent;"
                    "  border: none;"
                    "  color: %1;"
                    "  font-family: '%2';"
                    "  font-size: 14px;"
                    "  padding: 0px;"
                    "}" )
                    .arg(m_theme.foreground.name(), m_theme.fontFamily));
                QPalette addressPalette = m_addressBar->palette();
                addressPalette.setColor(QPalette::Text, m_theme.foreground);
                addressPalette.setColor(QPalette::Base, Qt::transparent);
                addressPalette.setColor(QPalette::Highlight, m_theme.accent);
                addressPalette.setColor(QPalette::HighlightedText, m_theme.background);
                m_addressBar->setPalette(addressPalette);
                m_addressBar->setContentsMargins(0, 0, 0, 0);
                m_addressBar->setTextMargins(0, 0, 0, 0);
            }
            auto *addrHost = new QWidget(m_sidebarHeader);
            addrHost->setObjectName("SidebarAddressHost");
            addrHost->setStyleSheet("QWidget#SidebarAddressHost { background: transparent; }");
            auto *addrHostLayout = new QHBoxLayout(addrHost);
            addrHostLayout->setContentsMargins(6, 0, 6, 0);
            addrHostLayout->setSpacing(0);
            addrHostLayout->addWidget(m_addrWrap);
            headerCol->addWidget(addrHost);
        }

        pageLayout->insertWidget(0, m_sidebarHeader);
    }

    connect(m_splitter, &QSplitter::splitterMoved, this, [this, sidebar](int pos, int) {
        if (!m_splitter) return;
        if (m_splitter->property("sidebarAnimating").toBool()) return;
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
    qApp->installEventFilter(this);

    if (auto *sb = statusBar()) sb->hide();
    setStatusBar(nullptr);

    connect(m_omnibox, &QLineEdit::returnPressed, this, &BrowserWindow::loadFromOmnibox);
    connect(m_tabTree, &TabTree::currentTabChanged, this, [this] {
        updateForCurrentTab();
        updateCurrentProfileSnapshot();
        refreshFloatingOmniboxItems();
        saveSessionForProfile(m_profiles.currentName());
    });
    connect(m_tabTree, &TabTree::tabClosed, this, [this](const QUrl &url, const QString &title) {
        m_closedTabs.prepend({title, url});
        while (m_closedTabs.size() > 20) m_closedTabs.removeLast();
        refreshFloatingOmniboxItems();
    });
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
        m_tabTree->rebuildForProfile(restoredSessionForProfile(m_profiles.currentName()));
        updateProfileSwitcher();
        updateCurrentProfileSnapshot();
    });
    connect(&m_profiles, &ProfileStore::profilesChanged, this, [this] {
        updateProfileSwitcher();
    });
    updateCurrentProfileSnapshot();
}

void BrowserWindow::setupActions() {
    auto openBlankTabWithOmnibox = [this] { openBlankTabForLocationEntry(); };
    auto focusOmnibox = [this] {
        QString current;
        if (auto *view = currentView()) {
            const QUrl u = view->url();
            const QString s = u.toString();
            if (!s.isEmpty() && s != "about:blank" && !s.startsWith("data:")) current = s;
        }
        refreshFloatingOmniboxItems();
        expandToolbar();
        m_floatingOmnibox->showFor(m_stack, current);
    };
    auto toggleSidebar = [this] {
        auto *side = m_splitter->widget(0);
        if (!side) return;
        const bool nowVisible = !side->isVisible();
        if (nowVisible) {
            m_sidebar->setHidden(false);
        } else {
            m_sidebar->setHidden(true);
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
    auto *passkeysAction = makeAction("Passkey Diagnostics", QKeySequence(),
                                      [this] { if (auto *v = currentView()) v->loadHtml(passkeyDiagnosticsHtml()); }, QAction::ApplicationSpecificRole);
    auto *quitAction  = makeAction("Quit pocb", QKeySequence(Qt::CTRL | Qt::Key_Q),
                                   [] { QApplication::quit(); }, QAction::QuitRole);

    auto *defaultBrowserAction = makeAction("Set as Default Browser", QKeySequence(), [] { mac::setAsDefaultBrowser(); }, QAction::ApplicationSpecificRole);
    defaultBrowserAction->setIcon(mac::sfSymbolIcon("heart.fill", 13.0, m_theme.foreground));
    auto *littleExternalAction = makeAction("Open External Links in Little Window", QKeySequence(), nullptr, QAction::ApplicationSpecificRole);
    littleExternalAction->setIcon(mac::sfSymbolIcon("heart", 13.0, m_theme.foreground));
    littleExternalAction->setCheckable(true);
    littleExternalAction->setChecked(QSettings().value("browser/externalLinksInLittleWindow", true).toBool());
    connect(littleExternalAction, &QAction::toggled, this, [](bool checked) { QSettings().setValue("browser/externalLinksInLittleWindow", checked); });

    // ── File ────────────────────────────────────────────────────────────
    auto *fileMenu = mb->addMenu("File");
    fileMenu->addAction(aboutAction);
    fileMenu->addAction(prefsAction);
    fileMenu->addAction(passkeysAction);
    fileMenu->addAction(defaultBrowserAction);
    fileMenu->addAction(littleExternalAction);
    fileMenu->addAction(quitAction);
    fileMenu->addAction(makeAction("New Tab", QKeySequence(Qt::CTRL | Qt::Key_T), openBlankTabWithOmnibox));
    fileMenu->addAction(makeAction("New Window", QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_N),
                                   [] { (new BrowserWindow())->show(); }));
    fileMenu->addAction(makeAction("New Little Window", QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_N),
                                   [] { (new LittleWindow(QUrl("about:blank")))->show(); }));
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
    auto reloadCurrentView = [this] {
        if (auto *v = currentView()) {
            if (isBlankTabUrl(v->url())) return;
            QColor fg = m_topbar ? m_topbar->property("chromeFg").value<QColor>() : QColor();
            if (!fg.isValid()) fg = m_theme.foreground;
            if (m_reloadBtn) {
                m_reloadBtn->setEnabled(true);
                setButtonSymbolSmooth(m_reloadBtn, "xmark", 14.0, fg);
            }
            v->reload();
        }
    };
    viewMenu->addAction(makeAction("Reload", QKeySequence(Qt::CTRL | Qt::Key_R), reloadCurrentView));
    viewMenu->addAction(makeAction("Force Reload", QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_R), reloadCurrentView));
    viewMenu->addSeparator();
    auto *pageAppearanceMenu = viewMenu->addMenu("Page Appearance");
    auto *pageAppearanceGroup = new QActionGroup(this);
    pageAppearanceGroup->setExclusive(true);
    const QString pageScheme = QSettings().value("ui/pageColorScheme", QStringLiteral("system")).toString();
    auto addPageAppearanceAction = [this, pageAppearanceMenu, pageAppearanceGroup, pageScheme](const QString &title, const QString &scheme) {
        auto *action = pageAppearanceMenu->addAction(title);
        action->setCheckable(true);
        action->setChecked(pageScheme == scheme);
        pageAppearanceGroup->addAction(action);
        connect(action, &QAction::triggered, this, [this, scheme] { applyPageColorScheme(scheme); });
    };
    addPageAppearanceAction("System", QStringLiteral("system"));
    addPageAppearanceAction("Light", QStringLiteral("light"));
    addPageAppearanceAction("Dark", QStringLiteral("dark"));
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
    tabsMenu->addAction(makeAction("New Tab", QKeySequence(),
                                   openBlankTabWithOmnibox));
    tabsMenu->addAction(makeAction("Close Tab", QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_K),
                                   [this] { m_tabTree->closeCurrent(); }));
    tabsMenu->addAction(makeAction("Reopen Closed Tab", QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_T),
                                   [this] { reopenLastClosedTab(); }));
    tabsMenu->addSeparator();
    auto showNextTabSwitcher = [this] { showTabSwitcher(+1); };
    auto showPreviousTabSwitcher = [this] { showTabSwitcher(-1); };
    auto *nextTabAction = makeAction("Select Next Tab",
                                     QKeySequence(Qt::CTRL | Qt::Key_Tab),
                                     showNextTabSwitcher);
    nextTabAction->setShortcutContext(Qt::ApplicationShortcut);
    addAction(nextTabAction);
    tabsMenu->addAction(nextTabAction);
    auto *previousTabAction = makeAction("Select Previous Tab",
                                         QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Tab),
                                         showPreviousTabSwitcher);
    previousTabAction->setShortcutContext(Qt::ApplicationShortcut);
    addAction(previousTabAction);
    tabsMenu->addAction(previousTabAction);
    auto *nextTabShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Tab), this);
    nextTabShortcut->setContext(Qt::ApplicationShortcut);
    connect(nextTabShortcut, &QShortcut::activated, this, showNextTabSwitcher);
    auto *previousTabShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Tab), this);
    previousTabShortcut->setContext(Qt::ApplicationShortcut);
    connect(previousTabShortcut, &QShortcut::activated, this, showPreviousTabSwitcher);
    auto *previousBacktabShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Backtab), this);
    previousBacktabShortcut->setContext(Qt::ApplicationShortcut);
    connect(previousBacktabShortcut, &QShortcut::activated, this, showPreviousTabSwitcher);
    mac::installTabSwitcherKeyMonitor(this,
                                      showNextTabSwitcher,
                                      showPreviousTabSwitcher,
                                      [this] {
                                          const bool active = m_tabSwitcherPending || (m_tabSwitcher && m_tabSwitcher->isVisible());
                                          if (active) acceptTabSwitcher();
                                          return active;
                                      },
                                      [this] {
                                          const bool active = m_tabSwitcherPending || (m_tabSwitcher && m_tabSwitcher->isVisible());
                                          if (active) hideTabSwitcher();
                                          return active;
                                      });

    // ── Bookmarks ─────────────────────────────────────────────────────
    auto *bookmarksMenu = mb->addMenu("Bookmarks");
    auto addCurrentBookmark = [this] {
        if (auto *v = currentView()) {
            if (m_bookmarks.contains(m_profiles.currentName(), v->url())) m_bookmarks.removeBookmark(m_profiles.currentName(), v->url());
            else m_bookmarks.addBookmark(m_profiles.currentName(), v->title(), v->url());
            refreshFloatingOmniboxItems();
        }
    };
    auto rebuildBookmarksMenu = [this, bookmarksMenu, addCurrentBookmark] {
        bookmarksMenu->clear();
        bool canBookmark = false;
        bool isBookmarked = false;
        if (auto *v = currentView()) {
            canBookmark = v->url().isValid() && !v->url().isEmpty() && v->url().scheme() != "about" && v->url().scheme() != "data";
            isBookmarked = canBookmark && m_bookmarks.contains(m_profiles.currentName(), v->url());
        }
        auto *bookmarkPage = bookmarksMenu->addAction(isBookmarked ? "Remove Bookmark" : "Bookmark This Page");
        bookmarkPage->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_D));
        bookmarkPage->setEnabled(canBookmark);
        connect(bookmarkPage, &QAction::triggered, this, addCurrentBookmark);
        bookmarksMenu->addSeparator();
        const QVector<Bookmark> items = m_bookmarks.bookmarks(m_profiles.currentName());
        if (items.isEmpty()) {
            auto *empty = bookmarksMenu->addAction("No Bookmarks");
            empty->setEnabled(false);
        } else {
            for (const Bookmark &bookmark : items) {
                auto *open = bookmarksMenu->addAction(bookmark.title);
                open->setToolTip(bookmark.url.toString());
                connect(open, &QAction::triggered, this, [this, url = bookmark.url] {
                    if (auto *v = currentView()) v->load(url);
                });
            }
        }
    };
    connect(bookmarksMenu, &QMenu::aboutToShow, this, rebuildBookmarksMenu);
    connect(&m_bookmarks, &BookmarkStore::bookmarksChanged, this, [this, rebuildBookmarksMenu](const QString &profileName) {
        if (profileName == m_profiles.currentName()) rebuildBookmarksMenu();
    });
    connect(&m_profiles, &ProfileStore::currentProfileChanged, this, rebuildBookmarksMenu);
    rebuildBookmarksMenu();
    auto *bookmarkPageShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_D), this);
    bookmarkPageShortcut->setContext(Qt::ApplicationShortcut);
    connect(bookmarkPageShortcut, &QShortcut::activated, this, addCurrentBookmark);

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
            a->setChecked(name == m_profiles.currentName());
            connect(a, &QAction::triggered, this, [this, name] {
                saveSessionForProfile(m_profiles.currentName());
                m_profiles.setCurrentProfile(name);
            });
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


QList<WebView *> BrowserWindow::orderedSwitchableTabs() const {
    const QList<WebView *> liveTabs = m_tabTree ? m_tabTree->views() : QList<WebView *>();
    QList<WebView *> ordered;
    for (auto *view : m_tabRecency) {
        if (view && liveTabs.contains(view) && !ordered.contains(view)) ordered.append(view);
    }
    for (auto *view : liveTabs) {
        if (view && !ordered.contains(view)) ordered.append(view);
    }
    return ordered;
}

void BrowserWindow::showTabSwitcher(int direction) {
    if (!m_tabTree) return;
    const QList<WebView *> tabs = orderedSwitchableTabs();
    if (tabs.size() < 2) return;
    if (m_tabSwitcher && m_tabSwitcher->isVisible()) {
        advanceTabSwitcher(direction);
        return;
    }
    m_tabSwitcherTabs = tabs;
    m_tabSwitcherIndex = qBound(0, direction > 0 ? 1 : tabs.size() - 1, tabs.size() - 1);
    m_tabSwitcherPending = true;
    if (!m_tabSwitcherOpenTimer) {
        m_tabSwitcherOpenTimer = new QTimer(this);
        m_tabSwitcherOpenTimer->setSingleShot(true);
        connect(m_tabSwitcherOpenTimer, &QTimer::timeout, this, [this] {
            if (!m_tabSwitcherPending || m_tabSwitcherTabs.isEmpty()) return;
            if (!m_tabSwitcher) m_tabSwitcher = new TabSwitcherPopup(m_theme, this);
            auto *popup = static_cast<TabSwitcherPopup *>(m_tabSwitcher);
            popup->setTabs(m_tabSwitcherTabs);
            popup->setCurrentIndex(m_tabSwitcherIndex);
            const QRect screen = (windowHandle() && windowHandle()->screen()) ? windowHandle()->screen()->availableGeometry() : QGuiApplication::primaryScreen()->availableGeometry();
            popup->move(screen.center() - QPoint(popup->width() / 2, popup->height() / 2));
            popup->show();
            mac::makeFloatingVibrantPanel(popup, mac::VibrancyMaterial::HUDWindow, 20.0);
            popup->raise();
        });
    }
    m_tabSwitcherOpenTimer->start(0);
}

void BrowserWindow::advanceTabSwitcher(int direction) {
    if (m_tabSwitcherTabs.isEmpty()) return;
    m_tabSwitcherIndex = (m_tabSwitcherIndex + (direction > 0 ? 1 : -1) + m_tabSwitcherTabs.size()) % m_tabSwitcherTabs.size();
    if (m_tabSwitcher) static_cast<TabSwitcherPopup *>(m_tabSwitcher)->setCurrentIndex(m_tabSwitcherIndex);
}

void BrowserWindow::acceptTabSwitcher() {
    if (m_tabSwitcherOpenTimer) m_tabSwitcherOpenTimer->stop();
    if (!m_tabSwitcherTabs.isEmpty()) {
        WebView *target = m_tabSwitcherTabs.value(m_tabSwitcherIndex);
        if (target && m_tabTree) m_tabTree->selectView(target);
    }
    hideTabSwitcher();
}

void BrowserWindow::hideTabSwitcher() {
    m_tabSwitcherPending = false;
    if (m_tabSwitcherOpenTimer) m_tabSwitcherOpenTimer->stop();
    if (m_tabSwitcher) m_tabSwitcher->hide();
    m_tabSwitcherTabs.clear();
    m_tabSwitcherIndex = 0;
}

bool BrowserWindow::eventFilter(QObject *obj, QEvent *ev) {
    if (ev->type() == QEvent::EnabledChange) {
        if (obj == m_backBtn || obj == m_fwdBtn || obj == m_reloadBtn) {
            if (auto *button = qobject_cast<QToolButton *>(obj)) {
                button->setAttribute(Qt::WA_TransparentForMouseEvents, !button->isEnabled());
            }
        }
    }
    QWidget *eventWidget = qobject_cast<QWidget *>(obj);
    const bool toolbarDragTarget = eventWidget && m_topbar
        && (eventWidget == m_topbar || m_topbar->isAncestorOf(eventWidget))
        && !qobject_cast<QToolButton *>(eventWidget)
        && !qobject_cast<QLineEdit *>(eventWidget)
        && !(m_addrWrap && (eventWidget == m_addrWrap || m_addrWrap->isAncestorOf(eventWidget)));
    if (toolbarDragTarget) {
        if (ev->type() == QEvent::MouseButtonPress) {
            auto *mouse = static_cast<QMouseEvent *>(ev);
            if (mouse->button() == Qt::LeftButton) {
                if (windowHandle() && windowHandle()->startSystemMove()) {
                    m_toolbarDragging = false;
                    return true;
                }
                m_toolbarDragging = true;
                m_toolbarDragOffset = mouse->globalPosition().toPoint() - frameGeometry().topLeft();
                return true;
            }
        } else if (ev->type() == QEvent::MouseMove && m_toolbarDragging) {
            auto *mouse = static_cast<QMouseEvent *>(ev);
            if (mouse->buttons() & Qt::LeftButton) {
                move(mouse->globalPosition().toPoint() - m_toolbarDragOffset);
                return true;
            }
        } else if (ev->type() == QEvent::MouseButtonRelease) {
            m_toolbarDragging = false;
        }
    }
    if (ev->type() == QEvent::ShortcutOverride || ev->type() == QEvent::KeyPress) {
        auto *key = static_cast<QKeyEvent *>(ev);
        if ((key->key() == Qt::Key_Tab || key->key() == Qt::Key_Backtab) && (key->modifiers() & Qt::ControlModifier)) {
            key->accept();
            if (ev->type() == QEvent::KeyPress) showTabSwitcher((key->modifiers() & Qt::ShiftModifier) || key->key() == Qt::Key_Backtab ? -1 : +1);
            return true;
        }
    }
    if (ev->type() == QEvent::KeyRelease && (m_tabSwitcherPending || (m_tabSwitcher && m_tabSwitcher->isVisible()))) {
        auto *key = static_cast<QKeyEvent *>(ev);
        if (key->key() == Qt::Key_Control) {
            acceptTabSwitcher();
            return true;
        }
        if (key->key() == Qt::Key_Escape) {
            hideTabSwitcher();
            return true;
        }
    }
    if ((obj == m_addrWrap || obj == m_webContainer)
        && (ev->type() == QEvent::Move || ev->type() == QEvent::Resize || ev->type() == QEvent::Show || ev->type() == QEvent::Hide)) {
        // Deferred: the layout pass that moved us may still be running.
        QTimer::singleShot(0, this, [this] { syncAddressPillGlass(); });
    }
    if (obj == m_webContainer && ev->type() == QEvent::Resize) syncToolbarOverlay();
    if (obj == m_sidebarViewport && ev->type() == QEvent::Resize) {
        if (m_sidebarSwipeAnim) {
            m_sidebarSwipeAnim->stop();
            m_sidebarSwipeAnim->deleteLater();
            m_sidebarSwipeAnim = nullptr;
        }
        resetProfileSwipeState();
        setSidebarSwipeOffset(0);
    }
    if (ev->type() == QEvent::Wheel && m_sidebarWidget && m_sidebarWidget->isVisible()) {
        const QPoint global = QCursor::pos();
        const QRect sidebarRect(m_sidebarWidget->mapToGlobal(QPoint(0, 0)), m_sidebarWidget->size());
        // Only wheel events delivered to widgets inside the sidebar take
        // part; the cursor check guards against stale positions.
        const bool inSidebar = eventWidget && (eventWidget == m_sidebarWidget || m_sidebarWidget->isAncestorOf(eventWidget));
        if (inSidebar && sidebarRect.contains(global)) {
            if (handleProfileSwipeWheel(static_cast<QWheelEvent *>(ev))) return true;
        }
    }
    return QMainWindow::eventFilter(obj, ev);
}

void BrowserWindow::applyChromeForPageColor(const QColor &pageColor) {
    if (!m_topbar || m_addrInSidebar) return;

    const bool hasColor = pageColor.isValid() && pageColor.alpha() >= 16;
    const QColor base = m_theme.background.lightness() < 128 ? QColor(24, 24, 27, 235) : QColor(245, 245, 247, 235);
    auto blend = [](const QColor &from, const QColor &to, double t) {
        return QColor(qRound(from.red() + (to.red() - from.red()) * t),
                      qRound(from.green() + (to.green() - from.green()) * t),
                      qRound(from.blue() + (to.blue() - from.blue()) * t),
                      from.alpha());
    };
    const QColor bg = hasColor ? blend(base, pageColor, 0.22) : base;

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
    QColor disabledFg = disabledToolbarColor(fg);

    // Hover shade: lighten dark pages, darken light ones — same logic for
    // toolbar buttons and the address bar wrap.
    auto mixRgb = [](const QColor &from, const QColor &to, double t) {
        return QColor(qRound(from.red() + (to.red() - from.red()) * t),
                      qRound(from.green() + (to.green() - from.green()) * t),
                      qRound(from.blue() + (to.blue() - from.blue()) * t),
                      from.alpha());
    };
    QColor hover = dark ? mixRgb(bg, QColor(255, 255, 255, bg.alpha()), 0.16)
                        : mixRgb(bg, QColor(0, 0, 0, bg.alpha()), 0.10);
    hover.setAlpha(qMax(220, bg.alpha()));

    QColor pressed = dark ? mixRgb(bg, QColor(255, 255, 255, bg.alpha()), 0.24)
                          : mixRgb(bg, QColor(0, 0, 0, bg.alpha()), 0.16);
    pressed.setAlpha(qMax(230, bg.alpha()));
    QColor menuHover = dark ? mixRgb(bg, QColor(0, 0, 0, bg.alpha()), 0.22)
                            : mixRgb(bg, QColor(255, 255, 255, bg.alpha()), 0.28);
    menuHover.setAlpha(qMax(220, bg.alpha()));
    QColor menuPressed = dark ? mixRgb(bg, QColor(0, 0, 0, bg.alpha()), 0.32)
                              : mixRgb(bg, QColor(255, 255, 255, bg.alpha()), 0.40);
    menuPressed.setAlpha(qMax(230, bg.alpha()));

    auto rgba = [](const QColor &c) {
        return QString("rgba(%1,%2,%3,%4)")
            .arg(c.red()).arg(c.green()).arg(c.blue()).arg(c.alpha());
    };

    m_topbar->setProperty("chromeFg", fg);
    if (auto *cb = qobject_cast<ui::ChromeBar *>(m_topbar)) {
        cb->setBackgroundColor(bg, /*animate=*/true);
    }

    // Re-render every SF Symbol in the new foreground tone.
    const double symPt = 14.0;
    auto reSymbol = [&](QToolButton *btn, const QString &name, double pointSize) {
        if (!btn) return;
        btn->setIcon(mac::sfSymbolIcon(name, pointSize, fg));
    };
    reSymbol(m_sidebarBtn, "sidebar.left", 16.0);
    if (m_backBtn) m_backBtn->setIcon(mac::sfSymbolIcon("chevron.backward", symPt, m_backBtn->isEnabled() ? fg : disabledFg));
    if (m_fwdBtn) m_fwdBtn->setIcon(mac::sfSymbolIcon("chevron.forward", symPt, m_fwdBtn->isEnabled() ? fg : disabledFg));
    const bool blankTab = !currentView() || isBlankTabUrl(currentView()->url());
    const bool canReload = !blankTab;
    if (m_reloadBtn) m_reloadBtn->setEnabled(canReload);
    setButtonSymbolSmooth(m_reloadBtn, currentView() && currentView()->isLoading() && !blankTab ? "xmark" : "arrow.clockwise", symPt, canReload ? fg : disabledFg);
    reSymbol(m_newTabBtn,  "plus", symPt);
    reSymbol(m_extensionsBtn, "puzzlepiece.extension", 16.0);
    reSymbol(m_downloadsBtn, "arrow.down.circle", 16.0);
    if (m_downloadsBtn) {
        QColor track = fg;
        track.setAlpha(50);
        m_downloadsBtn->setTrackColor(track);
        m_downloadsBtn->setBadgeTextColor(dark ? QColor(20, 20, 22) : QColor(250, 250, 252));
    }
    if (m_toolbarGrabber) m_toolbarGrabber->setPillColor(fg);
    reSymbol(m_settingsBtn,"gearshape", symPt);
    reSymbol(m_pillMenuBtn,"ellipsis.circle", m_addrInSidebar ? 14.0 : 12.0);

    if (m_addressBarCtl) m_addressBarCtl->setIconColor(fg);

    const QString btnQss = QString(
        "QToolButton {"
        "  background: transparent;"
        "  border: none;"
        "  border-radius: 16px;"
        "  padding: 0px;"
        "}"
        "QToolButton:hover { background: %1; }"
        "QToolButton:pressed { background: %2; }")
        .arg(rgba(hover), rgba(pressed));
    for (QToolButton *btn : {m_sidebarBtn, m_backBtn, m_fwdBtn, m_reloadBtn, m_newTabBtn, m_extensionsBtn, static_cast<QToolButton *>(m_downloadsBtn), m_settingsBtn}) {
        if (btn) btn->setStyleSheet(btnQss);
    }
    if (m_pillMenuBtn) {
        m_pillMenuBtn->setStyleSheet(QString(
            "QToolButton { background: transparent; border: none; border-radius: 4px; padding: 0px; }"
            "QToolButton:hover { background: %1; }"
            "QToolButton:pressed { background: %2; }")
            .arg(rgba(menuHover), rgba(menuPressed)));
    }

    if (auto *pill = qobject_cast<ui::AddrPill *>(m_addrWrap)) {
        pill->setIdleColor(dark ? QColor(255, 255, 255, 30) : QColor(0, 0, 0, 18));
        pill->setHoverColor(hover);
        QColor loadTint = fg;
        loadTint.setAlpha(220);
        pill->setLoadColor(loadTint);
        QColor focus = fg;
        focus.setAlpha(dark ? 90 : 120);
        pill->setFocusColor(focus);
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

