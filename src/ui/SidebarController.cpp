#include "SidebarController.hpp"

#include "MacIntegration.hpp"

#include <QCursor>
#include <QEasingCurve>
#include <QEvent>
#include <QMainWindow>
#include <QSettings>
#include <QSplitter>
#include <QTimer>
#include <QVariantAnimation>
#include <QWidget>

SidebarController::SidebarController(QMainWindow *window, QSplitter *splitter,
                                     std::function<void(bool)> setStackHostInset,
                                     QObject *parent)
    : QObject(parent), m_window(window), m_splitter(splitter),
      m_setStackHostInset(std::move(setStackHostInset)) {
    // Edge hover zone — a top-level Qt::Tool overlay along the window's left
    // edge that re-expands the sidebar on hover when collapsed. Top-level so
    // it sits above the QWebEngineView's native NSView.
    m_hoverZone = new QWidget(m_window, Qt::FramelessWindowHint | Qt::Tool | Qt::NoDropShadowWindowHint);
    m_hoverZone->setObjectName("SidebarHoverZone");
    m_hoverZone->setAttribute(Qt::WA_TranslucentBackground);
    m_hoverZone->setAttribute(Qt::WA_ShowWithoutActivating);
    m_hoverZone->setAttribute(Qt::WA_AlwaysStackOnTop);
    m_hoverZone->setFocusPolicy(Qt::NoFocus);
    m_hoverZone->setStyleSheet("background: transparent;");
    m_hoverZone->hide();
    m_hoverZone->installEventFilter(this);
}

bool SidebarController::hoverZoneVisible() const {
    return m_hoverZone && m_hoverZone->isVisible();
}

void SidebarController::setHidden(bool hidden) {
    if (!m_splitter) return;
    auto *side = m_splitter->widget(0);
    if (!side) return;
    side->setVisible(!hidden);
    mac::setTrafficLightsHidden(m_window, hidden);
    if (m_setStackHostInset) m_setStackHostInset(!hidden);
    if (!m_hoverZone) return;
    if (hidden) {
        positionHoverZone();
        m_hoverZone->show();
        m_hoverZone->raise();
        // If the cursor is already over the strip when the sidebar collapses
        // (typical when dragging it shut), require the user to move out and
        // back in before re-expanding — otherwise the zone instantly re-fires
        // and ping-pongs.
        const QPoint local = m_hoverZone->mapFromGlobal(QCursor::pos());
        m_hoverArmed = !m_hoverZone->rect().contains(local);
    } else {
        m_hoverZone->hide();
        m_hoverArmed = true;
    }
}

void SidebarController::positionHoverZone() {
    if (!m_hoverZone || !m_window) return;
    constexpr int kZoneWidth = 16;
    const QPoint topLeft = m_window->mapToGlobal(QPoint(0, 0));
    const int top = topLeft.y() + 28;
    const int height = qMax(0, m_window->height() - 28);
    m_hoverZone->setGeometry(topLeft.x(), top, kZoneWidth, height);
}

void SidebarController::expandAnimated() {
    if (!m_splitter) return;
    auto *side = m_splitter->widget(0);
    if (!side) return;
    if (m_anim && m_anim->state() == QAbstractAnimation::Running) return;

    setHidden(false);
    if (m_hoverZone) m_hoverZone->hide();

    const int saved = QSettings().value("ui/sidebarWidth", 240).toInt();
    const int target = qBound(side->minimumWidth(), saved, side->maximumWidth());
    const int total = m_splitter->size().width();
    const int handle = m_splitter->handleWidth();
    m_splitter->setSizes({0, qMax(0, total - handle)});

    if (!m_anim) {
        m_anim = new QVariantAnimation(this);
        m_anim->setDuration(180);
        m_anim->setEasingCurve(QEasingCurve::OutCubic);
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

bool SidebarController::eventFilter(QObject *obj, QEvent *ev) {
    if (obj == m_hoverZone) {
        if (ev->type() == QEvent::Leave) {
            m_hoverArmed = true;
        } else if (ev->type() == QEvent::Enter) {
            if (m_hoverArmed) expandAnimated();
        }
    }
    return QObject::eventFilter(obj, ev);
}
