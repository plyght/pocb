#pragma once

#include "Theme.hpp"

#include <QIcon>
#include <QPoint>
#include <QString>
#include <QWidget>

class QLabel;
class QTimer;
class QVariantAnimation;

namespace ui {

// Liquid Glass notification toast. A frameless tool window (so it floats
// above the native WKWebView) that slides in from the right, auto-dismisses
// after 3.5 s, and emits clicked() on press.
class ToastWidget final : public QWidget {
    Q_OBJECT
public:
    explicit ToastWidget(const Theme &theme, QWidget *parent = nullptr);

    void setContent(const QString &title, const QString &subtitle, const QIcon &icon);
    // Anchor = global top-right corner the toast should hug (inset applied).
    void presentAt(const QPoint &anchorTopRight);
    void reposition(const QPoint &anchorTopRight);
    void dismiss();
    bool presenting() const { return isVisible() && !m_dismissing; }

    static constexpr int Width = 312;
    static constexpr int Height = 60;
    static constexpr int Radius = 14;
    static constexpr int AutoDismissMs = 3500;

signals:
    void clicked();
    void dismissed();

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void showEvent(QShowEvent *) override;
    void enterEvent(QEnterEvent *) override;
    void leaveEvent(QEvent *) override;

private:
    void applyProgress(qreal t);

    Theme m_theme;
    QWidget *m_inner = nullptr;
    QLabel *m_iconLabel = nullptr;
    QLabel *m_titleLabel = nullptr;
    QLabel *m_subtitleLabel = nullptr;
    QVariantAnimation *m_anim = nullptr;
    QTimer *m_dismissTimer = nullptr;
    QPoint m_target;
    qreal m_progress = 0.0;
    bool m_dismissing = false;
    bool m_pressed = false;
    bool m_chromeApplied = false;
};

}  // namespace ui
