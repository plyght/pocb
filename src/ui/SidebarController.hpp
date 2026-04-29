#pragma once

#include <QObject>
#include <functional>

class QMainWindow;
class QSplitter;
class QVariantAnimation;
class QWidget;

class SidebarController final : public QObject {
    Q_OBJECT
public:
    SidebarController(QMainWindow *window, QSplitter *splitter,
                      std::function<void(bool)> setStackHostInset,
                      QObject *parent);

    void setHidden(bool hidden);
    void expandAnimated();
    void positionHoverZone();
    bool hoverZoneVisible() const;

protected:
    bool eventFilter(QObject *obj, QEvent *ev) override;

private:
    QMainWindow *m_window = nullptr;
    QSplitter *m_splitter = nullptr;
    QWidget *m_hoverZone = nullptr;
    QVariantAnimation *m_anim = nullptr;
    std::function<void(bool)> m_setStackHostInset;
    bool m_hoverArmed = true;
};
