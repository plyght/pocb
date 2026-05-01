#include "BrowserWindow.hpp"


#include "AddressBarController.hpp"
#include "FloatingOmnibox.hpp"
#include "ChromeWidgets.hpp"
#include "ChromeExtensionManager.hpp"
#include "LayoutMetrics.hpp"
#include "MacIntegration.hpp"
#include "NativeProfilePopover.hpp"
#include "NativeSettingsWindow.hpp"
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

namespace {

QEasingCurve responsiveEaseOut() {
    QEasingCurve curve(QEasingCurve::BezierSpline);
    curve.addCubicBezierSegment(QPointF(0.23, 1.0), QPointF(0.32, 1.0), QPointF(1.0, 1.0));
    return curve;
}

void setButtonSymbolSmooth(QToolButton *button, const QString &symbol, double pointSize, const QColor &color) {
    if (!button) return;
    const QString previousSymbol = button->property("sfSymbolName").toString();
    const QColor previousColor = button->property("sfSymbolColor").value<QColor>();
    if (previousSymbol == symbol && previousColor == color) return;
    button->setProperty("sfSymbolName", symbol);
    button->setProperty("sfSymbolColor", color);
    auto applyIcon = [button, symbol, pointSize, color] {
        button->setIcon(mac::sfSymbolIcon(symbol, pointSize, color));
    };
    if (previousSymbol.isEmpty() || previousSymbol == symbol) {
        applyIcon();
        return;
    }
    auto *effect = qobject_cast<QGraphicsOpacityEffect *>(button->graphicsEffect());
    if (!effect) {
        effect = new QGraphicsOpacityEffect(button);
        effect->setOpacity(1.0);
        button->setGraphicsEffect(effect);
    }
    auto *out = new QPropertyAnimation(effect, "opacity", button);
    out->setDuration(55);
    out->setStartValue(effect->opacity());
    out->setEndValue(0.35);
    out->setEasingCurve(responsiveEaseOut());
    QObject::connect(out, &QPropertyAnimation::finished, button, [button, effect, applyIcon] {
        applyIcon();
        auto *in = new QPropertyAnimation(effect, "opacity", button);
        in->setDuration(70);
        in->setStartValue(effect->opacity());
        in->setEndValue(1.0);
        in->setEasingCurve(responsiveEaseOut());
        QObject::connect(in, &QPropertyAnimation::finished, in, &QObject::deleteLater);
        in->start();
    });
    QObject::connect(out, &QPropertyAnimation::finished, out, &QObject::deleteLater);
    out->start();
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
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, false);
        QColor line = m_theme.foreground;
        line.setAlpha(42);
        painter.setPen(QPen(line, 1));
        if (orientation() == Qt::Horizontal) {
            const int x = width() / 2;
            painter.drawLine(QPoint(x, 0), QPoint(x, height()));
        } else {
            const int y = height() / 2;
            painter.drawLine(QPoint(0, y), QPoint(width(), y));
        }
    }

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
    setupUi();
    setupActions();
    ChromeExtensionManager::setBrowserWindow(this);
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
    if (m_tabTree) m_tabTree->restoreTabs(restoredSessionForProfile(m_profiles.currentName()));
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
    QSettings settings;
    settings.setValue("ui/windowGeometry", saveGeometry());
    saveSessionForProfile(m_profiles.currentName());
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
    });
}

WebView *BrowserWindow::extensionCurrentView() const {
    return currentView();
}

QList<WebView *> BrowserWindow::extensionViews() const {
    return m_tabTree ? m_tabTree->views() : QList<WebView *>();
}

WebView *BrowserWindow::extensionCreateTab(const QUrl &url, bool background) {
    return m_tabTree ? m_tabTree->newTabForExtension(url, background) : nullptr;
}

void BrowserWindow::extensionSelectView(WebView *view) {
    if (m_tabTree) m_tabTree->selectView(view);
}

void BrowserWindow::extensionCloseView(WebView *view) {
    if (!m_tabTree || !view) return;
    m_tabTree->selectView(view);
    if (m_tabTree->currentView() == view) m_tabTree->closeCurrent();
}

void BrowserWindow::extensionSetAction(const QString &key, const QString &label, std::function<void()> handler) {
    if (!m_topbar) return;
    QToolButton *button = m_extensionActionButtons.value(key, nullptr);
    if (!button) {
        button = new QToolButton(m_topbar);
        button->setAutoRaise(true);
        button->setFocusPolicy(Qt::NoFocus);
        button->setCursor(Qt::PointingHandCursor);
        button->setIconSize(QSize(16, 16));
        button->setFixedSize(28, 28);
        button->setStyleSheet(QString(
            "QToolButton { background: transparent; border: none; border-radius: 6px; padding: 0px; color: %1; }"
            "QToolButton:hover { background: %2; }"
            "QToolButton:pressed { background: %3; }")
            .arg(m_theme.foreground.name(), m_theme.hover.name(), m_theme.raised.name()));
        if (auto *layout = qobject_cast<QHBoxLayout *>(m_topbar->layout())) {
            const int index = m_settingsBtn ? layout->indexOf(m_settingsBtn) : layout->count();
            layout->insertWidget(qMax(0, index), button);
        }
        m_extensionActionButtons.insert(key, button);
    }
    button->setText(label.left(1).toUpper());
    button->setToolTip(label);
    button->disconnect();
    connect(button, &QToolButton::clicked, this, [handler = std::move(handler)] { handler(); });
    button->show();
}

void BrowserWindow::loadFromOmnibox() {
    const QUrl url = urlFromInput(m_omnibox->text());
    if (handleInternalUrl(url)) return;
    if (auto *view = currentView()) view->load(url);
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
        pane->setStyleSheet(QString("QWidget#SplitPane { background: %1; border: 1px solid transparent; border-radius: 10px; }").arg(m_theme.background.name()));
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
            if (!handleInternalUrl(url)) viewGuard->load(url);
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
            reSymbol(toolbar.sidebar, "sidebar.left", 14.0);
            reSymbol(toolbar.back, "chevron.backward", 14.0);
            reSymbol(toolbar.forward, "chevron.forward", 14.0);
            setButtonSymbolSmooth(toolbar.reload, viewGuard && viewGuard->isLoading() ? "xmark" : "arrow.clockwise", 14.0, fg);
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
            toolbar.back->setEnabled(viewGuard->canGoBack());
            toolbar.forward->setEnabled(viewGuard->canGoForward());
            QColor fg = toolbar.bar->property("chromeFg").value<QColor>();
            if (!fg.isValid()) fg = QColor(245, 245, 247);
            setButtonSymbolSmooth(toolbar.reload, viewGuard->isLoading() ? "xmark" : "arrow.clockwise", 14.0, fg);
        };
        syncPaneNav();
        connect(view, &WebView::navigationStateChanged, toolbar.bar, syncPaneNav);
        connect(toolbar.sidebar, &QToolButton::clicked, this, [this] {
            if (!m_sidebar || !m_sidebarWidget) return;
            if (m_sidebarWidget->isVisible()) m_sidebar->setHidden(true);
            else m_sidebar->expandAnimated();
        });
        connect(toolbar.back, &QToolButton::clicked, view, [viewGuard] { if (viewGuard) viewGuard->back(); });
        connect(toolbar.forward, &QToolButton::clicked, view, [viewGuard] { if (viewGuard) viewGuard->forward(); });
        connect(toolbar.reload, &QToolButton::clicked, view, [viewGuard] { if (viewGuard) { if (viewGuard->isLoading()) viewGuard->stop(); else viewGuard->reload(); } });
        connect(toolbar.newTab, &QToolButton::clicked, this, [this] {
            m_tabTree->newTab(QUrl("about:blank"));
            refreshFloatingOmniboxItems();
            m_floatingOmnibox->showFor(m_stack, QString());
        });
        connect(toolbar.settings, &QToolButton::clicked, this, &BrowserWindow::showSettings);
        connect(toolbar.pillMenuBtn, &QToolButton::clicked, this, [this, viewGuard, button = toolbar.pillMenuBtn] {
            auto copyUrl = [viewGuard] { if (viewGuard) QApplication::clipboard()->setText(viewGuard->url().toString()); };
            auto reload = [viewGuard] { if (viewGuard) viewGuard->reload(); };
            auto newTab = [this] {
                m_tabTree->newTab(QUrl("about:blank"));
                refreshFloatingOmniboxItems();
                m_floatingOmnibox->showFor(m_stack, QString());
            };
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
        pane->setStyleSheet(QString("QWidget#SplitPane { background: %1; border: 1px solid transparent; border-top-left-radius: %2px; border-top-right-radius: %3px; border-bottom-left-radius: 10px; border-bottom-right-radius: 10px; }")
            .arg(m_theme.background.name(), QString::number(left ? 10 : 0), QString::number(left ? 0 : 10)));
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
    if (!mac::showNativeSettingsWindow(this, m_profiles, homePage, searchEngine, showFullUrl)) return;

    m_homePage = homePage;
    if (m_tabTree) m_tabTree->setHomePage(homePage);

    if (searchEngine.contains("%1")) {
        m_searchEngine = searchEngine;
        if (m_floatingOmnibox) m_floatingOmnibox->setSearchEngineUrl(searchEngine);
    }

    if (m_addressBarCtl) m_addressBarCtl->setShowFullUrl(showFullUrl);
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
    } else {
        static_cast<QStackedLayout *>(m_stack->layout())->setCurrentWidget(view);
        if (!m_addrInSidebar && m_topbar) m_topbar->show();
        if (!m_addrInSidebar && m_topSeparator) m_topSeparator->show();
    }
    m_omnibox->setText(view->url().toString());
    if (m_backBtn) m_backBtn->setEnabled(view->canGoBack());
    if (m_fwdBtn) m_fwdBtn->setEnabled(view->canGoForward());
    if (m_reloadBtn) setButtonSymbolSmooth(m_reloadBtn, view->isLoading() ? "xmark" : "arrow.clockwise", 14.0, m_theme.foreground);
    connect(view, &WebView::navigationStateChanged, this, [this, view] {
        if (currentView() != view) return;
        if (m_backBtn) m_backBtn->setEnabled(view->canGoBack());
        if (m_fwdBtn) m_fwdBtn->setEnabled(view->canGoForward());
        applyChromeForPageColor(view->cachedThemeColor());
    }, Qt::UniqueConnection);
    if (m_addressBarCtl) {
        m_addressBarCtl->setDisplayUrl(view->url().toString(), view->url().scheme() == "https");
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
        pill->setIdleColor(QColor(28, 28, 30, 235));
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
            auto newTab = [this] {
                m_tabTree->newTab(QUrl("about:blank"));
                refreshFloatingOmniboxItems();
                m_floatingOmnibox->showFor(m_stack, QString());
            };
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
            if (auto *view = currentView()) view->load(url);
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
    connect(m_reloadBtn, &QToolButton::clicked, this, [this] { if (auto *v = currentView()) { if (v->isLoading()) v->stop(); else v->reload(); } });
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
        refreshFloatingOmniboxItems();
        m_floatingOmnibox->showFor(m_stack, QString());
    });

    return w.bar;
}

QWidget *BrowserWindow::buildProfileSwitcher(QWidget *parent) {
    auto *wrap = new QWidget(parent);
    wrap->setObjectName("ProfileSwitcher");
    wrap->setAttribute(Qt::WA_TranslucentBackground);
    auto *layout = new QHBoxLayout(wrap);
    layout->setContentsMargins(0, 8, 0, 0);
    layout->setSpacing(0);

    m_profileBtn = new QToolButton(wrap);
    m_profileBtn->setObjectName("ProfileButton");
    m_profileBtn->setToolButtonStyle(Qt::ToolButtonIconOnly);
    m_profileBtn->setCursor(Qt::PointingHandCursor);
    m_profileBtn->setFocusPolicy(Qt::NoFocus);
    m_profileBtn->setIconSize(QSize(19, 19));
    m_profileBtn->setMinimumSize(32, 32);
    m_profileBtn->setMaximumSize(32, 32);
    m_profileBtn->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    m_profileBtn->setStyleSheet(QString(
        "QToolButton#ProfileButton { background: transparent; border: none; border-radius: 8px; padding: 0px; color: %1; text-align: center; font-family: '%2'; font-size: %3px; }"
        "QToolButton#ProfileButton:hover { background: rgba(255,255,255,0.08); }"
        "QToolButton#ProfileButton:pressed { background: rgba(255,255,255,0.12); }")
        .arg(m_theme.foreground.name(), m_theme.fontFamily, QString::number(m_theme.regularSize)));
    layout->addWidget(m_profileBtn, 1, Qt::AlignLeft | Qt::AlignBottom);
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
    m_profileBtn->setIcon(mac::sfSymbolIcon(m_profiles.iconName(name), 15.0, m_theme.foreground));
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

void BrowserWindow::updateSidebarPreview(int direction) {
    if (!m_sidebarPreviewTabs || !m_sidebarPreviewIcon) return;
    const QStringList list = orderedProfiles();
    const int current = qMax(0, list.indexOf(m_profiles.currentName()));
    const int next = qBound(0, current + direction, list.size() - 1);
    if (next == current || next < 0 || next >= list.size()) return;
    const QString profile = list.at(next);
    if (m_sidebarPreviewProfile == profile) return;
    m_sidebarPreviewProfile = profile;
    m_sidebarPreviewIcon->setIcon(mac::sfSymbolIcon(m_profiles.iconName(profile), 18.0, m_theme.foreground));
    m_sidebarPreviewTabs->setUpdatesEnabled(false);
    m_sidebarPreviewTabs->clear();
    const QStringList titles = m_profileTabSnapshots.value(profile, QStringList{QStringLiteral("New tab")});
    for (const QString &title : titles) {
        auto *item = new QTreeWidgetItem(QStringList() << title);
        item->setIcon(0, mac::sfSymbolIcon("globe", 12.0, m_theme.muted));
        m_sidebarPreviewTabs->addTopLevelItem(item);
    }
    m_sidebarPreviewTabs->setUpdatesEnabled(true);
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
        return;
    }
    const int direction = m_sidebarSwipeDirection != 0 ? m_sidebarSwipeDirection : (m_sidebarSwipeOffset < 0 ? 1 : -1);
    updateSidebarPreview(direction);
    m_sidebarStrip->setGeometry(direction > 0 ? m_sidebarSwipeOffset : m_sidebarSwipeOffset - width,
                                0, width * 2, bounds.height());
    if (direction > 0) {
        m_sidebarPage->setGeometry(0, 0, width, bounds.height());
        if (m_sidebarPreviewPage) m_sidebarPreviewPage->setGeometry(width, 0, width, bounds.height());
    } else {
        if (m_sidebarPreviewPage) m_sidebarPreviewPage->setGeometry(0, 0, width, bounds.height());
        m_sidebarPage->setGeometry(width, 0, width, bounds.height());
    }
    if (m_sidebarPreviewPage) m_sidebarPreviewPage->show();
}

void BrowserWindow::settleSidebarSwipe(bool commit) {
    if (m_sidebarSwipeSettleTimer) m_sidebarSwipeSettleTimer->stop();
    if (m_sidebarSwipeSettling) return;
    if (m_sidebarSwipeAnim) {
        m_sidebarSwipeAnim->stop();
        m_sidebarSwipeAnim->deleteLater();
        m_sidebarSwipeAnim = nullptr;
    }
    const int width = m_sidebarWidget ? qMax(160, m_sidebarWidget->width()) : 240;
    const int startOffset = m_sidebarSwipeOffset;
    const int direction = startOffset < 0 ? 1 : -1;
    const QStringList list = orderedProfiles();
    const int current = qMax(0, list.indexOf(m_profiles.currentName()));
    const int next = qBound(0, current + direction, list.size() - 1);
    if (commit && (next == current || next < 0 || next >= list.size())) commit = false;
    auto *driver = new QVariantAnimation(this);
    m_sidebarSwipeAnim = driver;
    driver->setStartValue(startOffset);
    driver->setEndValue(commit ? (direction > 0 ? -width : width) : 0);
    driver->setDuration(commit ? 150 : 105);
    driver->setEasingCurve(responsiveEaseOut());
    m_sidebarSwipeSettling = true;
    connect(driver, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
        setSidebarSwipeOffset(value.toInt());
    });
    connect(driver, &QVariantAnimation::finished, this, [this, driver, commit, list, next] {
        if (m_sidebarSwipeAnim != driver) return;
        if (commit && next >= 0 && next < list.size()) {
            m_profiles.setCurrentProfile(list.at(next));
        }
        setSidebarSwipeOffset(0);
        m_sidebarSwipeActive = false;
        m_sidebarSwipeSettling = false;
        m_profileSwipeRemainder = 0;
        m_sidebarSwipeDirection = 0;
        m_sidebarPreviewProfile.clear();
        m_sidebarSwipeAnim = nullptr;
        driver->deleteLater();
    });
    driver->start();
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
    } else if (command == QStringLiteral("close-sidebar")) {
        if (m_sidebar) m_sidebar->setHidden(true);
    } else if (command == QStringLiteral("toggle-sidebar")) {
        if (m_sidebar && m_sidebarWidget) m_sidebar->setHidden(m_sidebarWidget->isVisible());
    } else if (command == QStringLiteral("new-tab")) {
        if (m_tabTree) m_tabTree->newTab(QUrl("about:blank"));
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

QList<QUrl> BrowserWindow::restoredSessionForProfile(const QString &profileName) const {
    QSettings settings;
    const QString key = QStringLiteral("sessions/%1/urls").arg(profileName);
    QList<QUrl> urls;
    const QStringList stored = settings.value(key).toStringList();
    for (const QString &value : stored) {
        const QUrl url(value);
        if (url.isValid() && !url.isEmpty()) urls.append(url);
    }
    return urls;
}

void BrowserWindow::saveSessionForProfile(const QString &profileName) const {
    if (!m_tabTree || profileName.trimmed().isEmpty()) return;
    QStringList values;
    for (const QUrl &url : m_tabTree->tabUrls()) values.append(url.toString());
    QSettings().setValue(QStringLiteral("sessions/%1/urls").arg(profileName), values);
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
}

void BrowserWindow::refreshFloatingOmniboxItems() {
    if (!m_floatingOmnibox) return;
    QList<FloatingOmnibox::LocalItem> items;
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
    auto addUrlItem = [&items, &iconForUrl](const QString &title, const QUrl &url) {
        if (!url.isValid() || url.isEmpty() || url.scheme() == QStringLiteral("about") || url.scheme() == QStringLiteral("data")) return;
        items.append({QStringLiteral("Page · ") + (title.isEmpty() ? url.toString() : title), url.toString(), iconForUrl(url), false});
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
    }
    for (const RecentPage &page : m_recentPages) {
        addUrlItem(page.title, page.url);
    }
    m_floatingOmnibox->setLocalItems(items);
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
        if (auto *view = currentView()) view->load(url);
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
    auto *hostLayout = new QVBoxLayout(stackHost);
    hostLayout->setContentsMargins(ui::metrics::stackHostMargins(/*sidebarVisible=*/true));
    hostLayout->setSpacing(0);

    // The web container holds the top toolbar AND the web view stack inside
    // a single rounded shape, so the toolbar lives "inside" the rounded card
    // pushing the page content down.
    m_webContainer = new QWidget(stackHost);
    m_webContainer->setObjectName("WebContainer");
    m_webContainer->setStyleSheet(QString(
        "QWidget#WebContainer { background: rgba(26, 26, 26, 180); border-radius: %1px; }")
        .arg(ui::metrics::WebContainerRadius));
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

    m_sidebarViewport = new QWidget(sidebar);
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
    connect(m_tabTree, &TabTree::tabDetachRequested, this, &BrowserWindow::detachTabToWindow);
    connect(m_tabTree, &TabTree::tabSplitRequested, this, &BrowserWindow::splitTabs);
    connect(m_tabTree, &TabTree::tabSplitPreviewRequested, this, &BrowserWindow::showSplitPreview);
    connect(m_tabTree, &TabTree::tabSplitPreviewEnded, this, &BrowserWindow::hideSplitPreview);
    pageLayout->addWidget(m_tabTree->widget(), 1);
    m_profileSwitcher = buildProfileSwitcher(m_sidebarPage);
    pageLayout->addWidget(m_profileSwitcher, 0, Qt::AlignLeft | Qt::AlignBottom);
    sideLayout->addWidget(m_sidebarViewport, 1);
    m_sidebarPreviewPage = new QWidget(m_sidebarStrip);
    m_sidebarPreviewPage->setObjectName("SidebarPreviewPage");
    m_sidebarPreviewPage->setAttribute(Qt::WA_TranslucentBackground);
    m_sidebarPreviewPage->setStyleSheet("QWidget#SidebarPreviewPage { background: transparent; }");
    auto *previewLayout = new QVBoxLayout(m_sidebarPreviewPage);
    previewLayout->setContentsMargins(0, 0, 0, 0);
    m_sidebarPreviewTabs = new QTreeWidget(m_sidebarPreviewPage);
    m_sidebarPreviewTabs->setHeaderHidden(true);
    m_sidebarPreviewTabs->setRootIsDecorated(false);
    m_sidebarPreviewTabs->setFrameShape(QFrame::NoFrame);
    m_sidebarPreviewTabs->setFocusPolicy(Qt::NoFocus);
    m_sidebarPreviewTabs->setSelectionMode(QAbstractItemView::NoSelection);
    m_sidebarPreviewTabs->setIconSize(QSize(16, 16));
    m_sidebarPreviewTabs->setUniformRowHeights(true);
    m_sidebarPreviewTabs->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_sidebarPreviewTabs->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_sidebarPreviewTabs->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_sidebarPreviewTabs->setAttribute(Qt::WA_TranslucentBackground);
    m_sidebarPreviewTabs->viewport()->setAttribute(Qt::WA_TranslucentBackground);
    m_sidebarPreviewTabs->setStyleSheet(QString(
        "QTreeWidget { background: transparent; border: none; color: %1; outline: 0; }"
        "QTreeWidget::item { padding: 4px 28px 4px 6px; border: none; background: transparent; color: %1; selection-background-color: transparent; }")
        .arg(m_theme.foreground.name()));
    previewLayout->addWidget(m_sidebarPreviewTabs, 1);
    m_sidebarPreviewIcon = new QToolButton(m_sidebarPreviewPage);
    m_sidebarPreviewIcon->setToolButtonStyle(Qt::ToolButtonIconOnly);
    m_sidebarPreviewIcon->setIconSize(QSize(19, 19));
    m_sidebarPreviewIcon->setMinimumSize(32, 32);
    m_sidebarPreviewIcon->setMaximumSize(32, 32);
    m_sidebarPreviewIcon->setEnabled(false);
    m_sidebarPreviewIcon->setStyleSheet("QToolButton { background: transparent; border: none; }");
    previewLayout->addWidget(m_sidebarPreviewIcon, 0, Qt::AlignLeft | Qt::AlignBottom);
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
    m_sidebar->setSidebarContent(m_sidebarViewport, sideLayout);
    m_sidebarSwipeSettleTimer = new QTimer(this);
    m_sidebarSwipeSettleTimer->setSingleShot(true);
    m_sidebarSwipeSettleTimer->setInterval(180);
    connect(m_sidebarSwipeSettleTimer, &QTimer::timeout, this, [this] {
        if (!m_sidebarSwipeActive) return;
        settleSidebarSwipe(qAbs(m_profileSwipeRemainder) >= qMax(160, m_sidebarWidget ? m_sidebarWidget->width() : 240) / 3);
    });

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
    });
    connect(&m_profiles, &ProfileStore::profilesChanged, this, [this] {
        updateProfileSwitcher();
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
        refreshFloatingOmniboxItems();
        m_floatingOmnibox->showFor(m_stack, QString());
    };
    auto focusOmnibox = [this] {
        QString current;
        if (auto *view = currentView()) {
            const QUrl u = view->url();
            const QString s = u.toString();
            if (!s.isEmpty() && s != "about:blank" && !s.startsWith("data:")) current = s;
        }
        refreshFloatingOmniboxItems();
        m_floatingOmnibox->showFor(m_stack, current);
    };
    auto toggleSidebar = [this] {
        auto *side = m_splitter->widget(0);
        if (!side) return;
        const bool nowVisible = !side->isVisible();
        if (nowVisible) {
            m_sidebar->expandAnimated();
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
    if (obj == m_sidebarViewport && ev->type() == QEvent::Resize) {
        setSidebarSwipeOffset(0);
        if (m_sidebarPreviewTabs) m_sidebarPreviewTabs->setColumnWidth(0, m_sidebarViewport->width());
    }
    if (ev->type() == QEvent::Wheel && m_sidebarWidget && m_sidebarWidget->isVisible() && !m_sidebarSwipeSettling) {
        const QPoint global = QCursor::pos();
        const QRect sidebarRect(m_sidebarWidget->mapToGlobal(QPoint(0, 0)), m_sidebarWidget->size());
        if (sidebarRect.contains(global)) {
            auto *wheel = static_cast<QWheelEvent *>(ev);
            const QPoint pixel = wheel->pixelDelta();
            const QPoint angle = wheel->angleDelta();
            const bool highResolutionTrackpad = !pixel.isNull();
            if (wheel->phase() == Qt::ScrollBegin && !m_sidebarSwipeSettling) {
                if (m_sidebarSwipeSettleTimer) m_sidebarSwipeSettleTimer->stop();
                m_profileSwipeRemainder = m_sidebarSwipeOffset;
            }
            if (wheel->phase() == Qt::ScrollEnd && m_sidebarSwipeActive) {
                settleSidebarSwipe(qAbs(m_sidebarSwipeOffset) >= qMax(160, m_sidebarWidget->width()) / 4);
                return true;
            }
            const int horizontal = pixel.x() != 0 ? pixel.x() : angle.x() / 2;
            const int vertical = pixel.y() != 0 ? pixel.y() : angle.y() / 2;
            if (qAbs(horizontal) > qAbs(vertical) && horizontal != 0) {
                const int width = qMax(160, m_sidebarWidget->width());
                if (m_sidebarSwipeSettleTimer) m_sidebarSwipeSettleTimer->stop();
                const int intended = m_profileSwipeRemainder + horizontal;
                if (m_sidebarSwipeDirection != 0 && ((m_sidebarSwipeDirection > 0 && intended > 0) || (m_sidebarSwipeDirection < 0 && intended < 0))) {
                    m_profileSwipeRemainder = 0;
                    setSidebarSwipeOffset(0);
                    m_sidebarSwipeActive = true;
                    return true;
                }
                const int intendedDirection = m_sidebarSwipeDirection != 0 ? m_sidebarSwipeDirection : (intended < 0 ? 1 : -1);
                const QStringList list = orderedProfiles();
                const int profileIndex = qMax(0, list.indexOf(m_profiles.currentName()));
                const int targetIndex = profileIndex + intendedDirection;
                if (targetIndex < 0 || targetIndex >= list.size()) {
                    m_profileSwipeRemainder = 0;
                    m_sidebarSwipeDirection = 0;
                    setSidebarSwipeOffset(0);
                    m_sidebarSwipeActive = false;
                    m_sidebarPreviewProfile.clear();
                    return true;
                }
                m_sidebarSwipeDirection = intendedDirection;
                m_profileSwipeRemainder = qBound(-width, intended, width);
                const int sign = m_profileSwipeRemainder < 0 ? -1 : 1;
                const int magnitude = qAbs(m_profileSwipeRemainder);
                const int displayed = magnitude <= width * 2 / 3
                    ? magnitude
                    : width * 2 / 3 + (magnitude - width * 2 / 3) / 4;
                setSidebarSwipeOffset(sign * displayed);
                m_sidebarSwipeActive = true;
                if (magnitude >= width * 3 / 4) {
                    settleSidebarSwipe(true);
                } else if (wheel->phase() == Qt::ScrollEnd) {
                    settleSidebarSwipe(magnitude >= width / 4);
                } else if (!highResolutionTrackpad && m_sidebarSwipeSettleTimer) {
                    m_sidebarSwipeSettleTimer->start(180);
                }
                return true;
            }
            if (m_sidebarSwipeActive && wheel->phase() == Qt::ScrollEnd) {
                settleSidebarSwipe(qAbs(m_profileSwipeRemainder) >= qMax(160, m_sidebarWidget->width()) / 3);
                return true;
            }
        }
    }
    return QMainWindow::eventFilter(obj, ev);
}

void BrowserWindow::applyChromeForPageColor(const QColor &pageColor) {
    if (!m_topbar || m_addrInSidebar) return;

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

    if (auto *cb = qobject_cast<ui::ChromeBar *>(m_topbar)) {
        cb->setBackgroundColor(bg, /*animate=*/true);
    }

    // Re-render every SF Symbol in the new foreground tone.
    const double symPt = 14.0;
    auto reSymbol = [&](QToolButton *btn, const QString &name, double pointSize) {
        if (!btn) return;
        btn->setIcon(mac::sfSymbolIcon(name, pointSize, fg));
    };
    reSymbol(m_sidebarBtn, "sidebar.left", symPt);
    reSymbol(m_backBtn,    "chevron.backward", symPt);
    reSymbol(m_fwdBtn,     "chevron.forward", symPt);
    setButtonSymbolSmooth(m_reloadBtn, currentView() && currentView()->isLoading() ? "xmark" : "arrow.clockwise", symPt, fg);
    reSymbol(m_newTabBtn,  "plus", symPt);
    reSymbol(m_settingsBtn,"gearshape", symPt);
    reSymbol(m_pillMenuBtn,"ellipsis.circle", m_addrInSidebar ? 14.0 : 12.0);

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
    for (QToolButton *btn : {m_sidebarBtn, m_backBtn, m_fwdBtn, m_reloadBtn, m_newTabBtn, m_settingsBtn}) {
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
        pill->setIdleColor(bg);
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

