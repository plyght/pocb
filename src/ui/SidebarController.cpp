#include "SidebarController.hpp"

#include "MacIntegration.hpp"
#include "LayoutMetrics.hpp"

#include <QCursor>
#include <QEasingCurve>
#include <QEvent>
#include <QLabel>
#include <QMainWindow>
#include <QSettings>
#include <QSplitter>
#include <QTimer>
#include <QVariantAnimation>
#include <QVBoxLayout>
#include <QWidget>

namespace {
constexpr int kDismissDelayMs = ui::metrics::FloatingDismissDelayMs;
constexpr int kSlideInDurationMs = ui::metrics::FloatingSlideInDurationMs;
constexpr int kSlideOutDurationMs = ui::metrics::FloatingSlideOutDurationMs;
constexpr int kSlideTravelPx = ui::metrics::FloatingSlideTravelPx;

QEasingCurve responsiveEaseOut() {
    return QEasingCurve(QEasingCurve::OutCubic);
}
}

SidebarController::SidebarController(QMainWindow *window, QSplitter *splitter,
                                     std::function<void(bool)> setStackHostInset,
                                     QObject *parent)
    : QObject(parent), m_window(window), m_splitter(splitter),
      m_setStackHostInset(std::move(setStackHostInset)) {
    m_hoverZone = new QWidget(m_window, Qt::FramelessWindowHint | Qt::Tool | Qt::NoDropShadowWindowHint);
    m_hoverZone->setObjectName("SidebarHoverZone");
    m_hoverZone->setAttribute(Qt::WA_TranslucentBackground);
    m_hoverZone->setAttribute(Qt::WA_ShowWithoutActivating);
    m_hoverZone->setAttribute(Qt::WA_AlwaysStackOnTop);
    m_hoverZone->setFocusPolicy(Qt::NoFocus);
    m_hoverZone->setStyleSheet("background: transparent;");
    m_hoverZone->hide();
    m_hoverZone->installEventFilter(this);

    // Top-level Qt::Tool window — its own NSWindow so it can hover above the
    // WKWebView and animate independently of the splitter. Vibrancy + rounded
    // corners are applied to the underlying NSWindow on first show via
    // mac::enableWindowVibrancy (which flips opaque=NO + clearColor and adds
    // a behind-window NSVisualEffectView).
    m_floating = new QWidget(nullptr, Qt::FramelessWindowHint | Qt::Tool);
    m_floating->setObjectName("FloatingSidebar");
    m_floating->setAttribute(Qt::WA_TranslucentBackground);
    m_floating->setAttribute(Qt::WA_ShowWithoutActivating);
    m_floating->setFocusPolicy(Qt::NoFocus);
    m_floating->setMouseTracking(true);

    m_floatingInner = new QWidget(m_floating);
    m_floatingInner->setObjectName("FloatingSidebarInner");
    m_floatingInner->setAttribute(Qt::WA_TranslucentBackground);
    // Force a real NSView for the inner widget. Without this, Qt renders
    // child widgets into the contentView's shared layer, which sits BELOW
    // any subviews (including our vibrancy NSVisualEffectView) — so the tab
    // tree gets painted under the blur and disappears. Promoting the inner
    // to a native window makes it a sibling NSView drawn on top of the
    // vibrancy.
    m_floatingInner->setAttribute(Qt::WA_NativeWindow);
    m_floatingInner->setStyleSheet("QWidget#FloatingSidebarInner { background: transparent; }");
    m_floatingLayout = new QVBoxLayout(m_floatingInner);
    m_floatingLayout->setContentsMargins(10, 12, 10, 10);
    m_floatingLayout->setSpacing(0);

    m_floatingSnapshot = new QLabel(m_floatingInner);
    m_floatingSnapshot->setObjectName("FloatingSidebarSnapshot");
    m_floatingSnapshot->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_floatingSnapshot->setScaledContents(false);
    m_floatingSnapshot->setStyleSheet("QLabel#FloatingSidebarSnapshot { background: transparent; }");
    m_floatingSnapshot->hide();

    m_floating->hide();
    m_floating->installEventFilter(this);

    m_dismissTimer = new QTimer(this);
    m_dismissTimer->setSingleShot(true);
    m_dismissTimer->setInterval(kDismissDelayMs);
    connect(m_dismissTimer, &QTimer::timeout, this, [this] { hideFloatingAnimated(); });

    // While the floating panel is open we poll the cursor every ~60ms to
    // decide whether to dismiss. The "keep-alive" rect spans from the host
    // window's left edge all the way to the panel's right edge — so the
    // gap between window edge and panel edge (kFloatingSideGap) doesn't
    // count as "left the panel" and we don't get the dismiss/re-summon
    // ping-pong that Enter/Leave events caused.
    m_hoverPoll = new QTimer(this);
    m_hoverPoll->setInterval(60);
    connect(m_hoverPoll, &QTimer::timeout, this, [this] {
        if (!m_floating || !m_window || !m_floating->isVisible()) return;
        const QPoint cursor = QCursor::pos();
        const QRect panel = m_floating->frameGeometry();
        const QRect windowFrame = ui::metrics::windowContentRect(m_window);
        const QRect keepAlive(windowFrame.left(), panel.top() - 10,
                              panel.right() - windowFrame.left() + 18, panel.height() + 20);
        if (keepAlive.contains(cursor)) {
            if (m_dismissTimer) m_dismissTimer->stop();
            if (m_slidingOut) showFloating();
        } else if (!m_slidingOut) {
            if (m_dismissTimer && !m_dismissTimer->isActive())
                m_dismissTimer->start();
        }
    });

    // The slide animation drives both window x position (small inward
    // travel) and opacity. Value runs 0..1, where 0 = hidden / out-of-place,
    // 1 = fully shown / final position.
    m_slideAnim = new QVariantAnimation(this);
    m_slideAnim->setDuration(kSlideInDurationMs);
    m_slideAnim->setEasingCurve(responsiveEaseOut());
    m_slideAnim->setStartValue(0.0);
    m_slideAnim->setEndValue(1.0);
    connect(m_slideAnim, &QVariantAnimation::valueChanged, this,
            [this](const QVariant &v) {
                if (!m_floating || !m_floatingInner) return;
                const double t = qBound(0.0, v.toDouble(), 1.0);
                m_slideProgress = t;
                const int w = m_floating->width();
                const int h = m_floating->height();
                const int travel = qMin(kSlideTravelPx, w);
                const int x = -travel + qRound(travel * t);
                m_floatingInner->setGeometry(x, 0, w, h);
                m_floating->setWindowOpacity(0.9 + 0.1 * t);
            });
    connect(m_slideAnim, &QVariantAnimation::finished, this, [this] {
            if (!m_slidingOut) {
            clearFloatingSnapshot();
            return;
        }
        m_slidingOut = false;
        if (m_hoverPoll) m_hoverPoll->stop();
        if (m_floating) m_floating->hide();
        dockContent();
        if (m_splitter) {
            QWidget *side = m_splitter->widget(0);
            if (side && !side->isVisible() && m_hoverZone) {
                positionHoverZone();
                m_hoverZone->show();
                m_hoverZone->raise();
                // Mouse is by definition outside both the floating panel
                // and the hover strip when slide-out completes (Leave was
                // what kicked off the dismiss timer in the first place),
                // so we can safely arm the strip immediately.
                m_hoverArmed = true;
            }
        }
    });
}

void SidebarController::setSidebarContent(QWidget *content, QVBoxLayout *dockedLayout) {
    m_content = content;
    m_dockedLayout = dockedLayout;
}

bool SidebarController::hoverZoneVisible() const {
    return m_hoverZone && m_hoverZone->isVisible();
}

bool SidebarController::floatingVisible() const {
    return m_floating && m_floating->isVisible();
}

void SidebarController::setHidden(bool hidden) {
    if (!m_splitter) return;
    auto *side = m_splitter->widget(0);
    if (!side) return;

    if (!hidden) {
        if (m_floating && m_floating->isVisible()) {
            m_slideAnim->stop();
            m_slidingOut = false;
            m_floating->hide();
        }
        if (m_dismissTimer) m_dismissTimer->stop();
        dockContent();
    }

    side->setVisible(!hidden);
    mac::setTrafficLightsHidden(m_window, hidden);
    mac::refreshUnifiedToolbar(m_window);
    if (m_setStackHostInset) m_setStackHostInset(!hidden);
    if (!m_hoverZone) return;
    if (hidden) {
        positionHoverZone();
        m_hoverZone->show();
        m_hoverZone->raise();
        // Always require a fresh Leave→Enter cycle before the strip can
        // re-fire — otherwise drag-collapsing the sidebar (cursor still
        // hovering near the strip) instantly summons it again.
        m_hoverArmed = false;
    } else {
        m_hoverZone->hide();
        m_hoverArmed = true;
    }
}

void SidebarController::positionHoverZone() {
    if (!m_hoverZone || !m_window) return;
    m_hoverZone->setGeometry(ui::metrics::sidebarHoverZoneRect(m_window));
}

void SidebarController::positionFloating() {
    if (!m_floating || !m_window) return;
    const int saved = QSettings().value("ui/sidebarWidth", ui::metrics::SidebarDefaultWidth).toInt();
    const QWidget *side = m_splitter ? m_splitter->widget(0) : nullptr;
    const int minimum = side ? side->minimumWidth() : ui::metrics::SidebarMinimumWidth;
    const int width = qBound(minimum, saved, ui::metrics::SidebarMaximumWidth);
    const QRect geometry = ui::metrics::floatingSidebarRect(m_window, width);
    m_floating->setGeometry(geometry);
    if (m_floatingInner) {
        // Preserve current x offset (mid-animation) but match new size.
        m_floatingInner->setGeometry(m_floatingInner->x(), 0, width, geometry.height());
    }
}

void SidebarController::expandAnimated() {
    if (!m_splitter) return;
    auto *side = m_splitter->widget(0);
    if (!side) return;
    if (m_anim && m_anim->state() == QAbstractAnimation::Running) return;

    if (m_floating && m_floating->isVisible()) hideFloatingImmediate();
    setHidden(false);
    if (m_hoverZone) m_hoverZone->hide();

    const int saved = QSettings().value("ui/sidebarWidth", ui::metrics::SidebarDefaultWidth).toInt();
    const int target = qBound(side->minimumWidth(), saved, side->maximumWidth());
    const int total = m_splitter->size().width();
    const int handle = m_splitter->handleWidth();
    m_splitter->setSizes({0, qMax(0, total - handle)});

    if (!m_anim) {
        m_anim = new QVariantAnimation(this);
        m_anim->setDuration(kSlideInDurationMs);
        m_anim->setEasingCurve(responsiveEaseOut());
        connect(m_anim, &QVariantAnimation::valueChanged, this,
                [this](const QVariant &v) {
                    if (!m_splitter) return;
                    const int w = v.toInt();
                    const int total = m_splitter->size().width();
                    const int handle = m_splitter->handleWidth();
                    m_splitter->setSizes({w, qMax(0, total - w - handle)});
                });
    }
    m_anim->stop();
    m_anim->setStartValue(0);
    m_anim->setEndValue(target);
    m_anim->start();
}

void SidebarController::showFloating() {
    if (!m_floating || !m_content || !m_floatingLayout || !m_window) return;
    if (m_dismissTimer) m_dismissTimer->stop();

    if (m_floating->isVisible() && m_slidingOut) {
        // Reverse direction mid-flight.
        m_slidingOut = false;
        const double startT = m_slideProgress;
        m_slideAnim->stop();
        m_slideAnim->setDuration(kSlideInDurationMs);
        m_slideAnim->setEasingCurve(responsiveEaseOut());
        m_slideAnim->setStartValue(startT);
        m_slideAnim->setEndValue(1.0);
        m_slideAnim->start();
        return;
    }
    if (m_floating->isVisible()) return;

    m_content->setUpdatesEnabled(false);
    m_floatingLayout->addWidget(m_content, 1);
    m_content->show();

    positionFloating();
    if (m_floatingInner) {
        m_floatingInner->setGeometry(-qMin(kSlideTravelPx, m_floating->width()), 0,
                                      m_floating->width(), m_floating->height());
    }
    m_slideProgress = 0.0;
    m_floating->setWindowOpacity(1.0);
    m_floating->show();
    m_floating->raise();

    if (!m_floatingChromeApplied) {
        // NSWindow exists now; configure vibrancy + rounded corners on it.
        // enableWindowVibrancy flips opaque=NO + adds a behind-window
        // NSVisualEffectView so the panel shows real desktop blur.
        mac::makeFloatingVibrantPanel(m_floating, mac::VibrancyMaterial::Sidebar, 12.0);
        m_floatingChromeApplied = true;
    }

    if (m_hoverZone) m_hoverZone->hide();
    prepareFloatingSnapshot();
    m_content->setUpdatesEnabled(true);
    m_slidingOut = false;
    m_slideAnim->stop();
    m_slideAnim->setDuration(kSlideInDurationMs);
    m_slideAnim->setEasingCurve(responsiveEaseOut());
    m_slideAnim->setStartValue(0.0);
    m_slideAnim->setEndValue(1.0);
    m_slideAnim->start();
    if (m_hoverPoll) m_hoverPoll->start();
}

void SidebarController::hideFloatingAnimated() {
    if (!m_floating || !m_floating->isVisible() || !m_window) {
        hideFloatingImmediate();
        return;
    }
    prepareFloatingSnapshot();
    m_slidingOut = true;
    const double startT = m_slideProgress;
    m_slideAnim->stop();
    m_slideAnim->setDuration(kSlideOutDurationMs);
    m_slideAnim->setEasingCurve(responsiveEaseOut());
    m_slideAnim->setStartValue(startT);
    m_slideAnim->setEndValue(0.0);
    m_slideAnim->start();
}

void SidebarController::hideFloatingImmediate() {
    if (m_dismissTimer) m_dismissTimer->stop();
    if (m_hoverPoll) m_hoverPoll->stop();
    if (m_slideAnim) m_slideAnim->stop();
    m_slidingOut = false;
    if (m_floating && m_floating->isVisible()) m_floating->hide();
    dockContent();
}

void SidebarController::prepareFloatingSnapshot() {
    if (!m_content || !m_floatingSnapshot || !m_floatingInner) return;
    const QPixmap pixmap = m_content->grab();
    if (pixmap.isNull()) return;
    m_floatingSnapshot->setPixmap(pixmap);
    m_floatingSnapshot->setGeometry(m_content->geometry());
    m_floatingSnapshot->show();
    m_floatingSnapshot->raise();
    m_content->hide();
}

void SidebarController::clearFloatingSnapshot() {
    if (m_content) m_content->show();
    if (!m_floatingSnapshot) return;
    m_floatingSnapshot->clear();
    m_floatingSnapshot->hide();
}

void SidebarController::dockContent() {
    clearFloatingSnapshot();
    if (m_content && m_dockedLayout) {
        const int insertAt = qMax(0, m_dockedLayout->count() - 1);
        m_dockedLayout->insertWidget(insertAt, m_content, 1);
    }
}

bool SidebarController::eventFilter(QObject *obj, QEvent *ev) {
    if (obj == m_hoverZone) {
        if (ev->type() == QEvent::Leave) {
            m_hoverArmed = true;
        } else if (ev->type() == QEvent::Enter) {
            if (m_hoverArmed) showFloating();
        }
    }
    // Note: dismiss/keep-alive on the floating panel itself is handled by
    // the cursor-position poll (m_hoverPoll), not by Enter/Leave events,
    // because the gap between the window edge and the panel edge would
    // otherwise cause an immediate dismiss-then-resummon cycle.
    return QObject::eventFilter(obj, ev);
}
