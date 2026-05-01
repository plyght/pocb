#pragma once

#include <QObject>
#include <QPointer>
#include <QRect>
#include <functional>

class QLabel;
class QMainWindow;
class QSplitter;
class QVBoxLayout;
class QVariantAnimation;
class QTimer;
class QWidget;

class SidebarController final : public QObject {
    Q_OBJECT
public:
    SidebarController(QMainWindow *window, QSplitter *splitter,
                      std::function<void(bool)> setStackHostInset,
                      QObject *parent);

    // Hand the controller the sidebar content widget (e.g. the tab tree)
    // and the docked layout it lives in. The controller will reparent it
    // into the floating overlay on hover-while-collapsed and back when the
    // sidebar is re-docked.
    void setSidebarContent(QWidget *content, QVBoxLayout *dockedLayout);

    void setHidden(bool hidden);
    void expandAnimated();
    void positionHoverZone();
    void positionFloating();
    bool hoverZoneVisible() const;
    bool floatingVisible() const;

protected:
    bool eventFilter(QObject *obj, QEvent *ev) override;

private:
    void showFloating();
    void hideFloatingAnimated();
    void hideFloatingImmediate();
    void dockContent();
    void prepareFloatingSnapshot();
    void clearFloatingSnapshot();

    QMainWindow *m_window = nullptr;
    QSplitter *m_splitter = nullptr;
    QWidget *m_hoverZone = nullptr;
    QWidget *m_floating = nullptr;
    QWidget *m_floatingInner = nullptr;
    QVBoxLayout *m_floatingLayout = nullptr;
    QLabel *m_floatingSnapshot = nullptr;
    QPointer<QWidget> m_content;
    QPointer<QVBoxLayout> m_dockedLayout;
    QVariantAnimation *m_anim = nullptr;
    QVariantAnimation *m_slideAnim = nullptr;
    QTimer *m_dismissTimer = nullptr;
    QTimer *m_hoverPoll = nullptr;
    std::function<void(bool)> m_setStackHostInset;
    bool m_hoverArmed = true;
    bool m_slidingOut = false;
    bool m_floatingChromeApplied = false;
    double m_slideProgress = 0.0;
    bool m_nativeFloatingAnimation = false;
    QRect m_floatingBaseGeometry;
};
