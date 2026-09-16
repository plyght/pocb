#pragma once

#include "Theme.hpp"

#include <QColor>
#include <QIcon>
#include <QPixmap>
#include <QString>
#include <QToolButton>
#include <QWidget>

class QVariantAnimation;

namespace ui {

// Deterministic avatar tint for a profile name (stable across launches).
QColor profileAvatarColor(const QString &profileName);

// Circular profile avatar: coloured disc with the profile's initial letter.
// The active profile gets a bright ring.
class ProfileAvatarButton final : public QToolButton {
    Q_OBJECT
public:
    explicit ProfileAvatarButton(const Theme &theme, QWidget *parent = nullptr);

    void setProfileName(const QString &name);
    QString profileName() const { return m_name; }
    void setActive(bool active);
    void setDiameter(int px);

protected:
    void paintEvent(QPaintEvent *) override;

private:
    Theme m_theme;
    QString m_name;
    bool m_active = true;
    int m_diameter = 26;
};

// Horizontal pager dots. `position` may be fractional while a swipe is in
// flight so the highlighted dot slides between neighbours.
class PagerDots final : public QWidget {
    Q_OBJECT
public:
    explicit PagerDots(const Theme &theme, QWidget *parent = nullptr);

    void setCount(int count);
    void setPosition(qreal position);
    int count() const { return m_count; }
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *) override;

private:
    Theme m_theme;
    int m_count = 1;
    qreal m_position = 0.0;
};

class SidebarActionRow final : public QToolButton {
    Q_OBJECT
public:
    SidebarActionRow(const Theme &theme, const QIcon &icon, const QString &label, QWidget *parent = nullptr);
    void setIcon(const QIcon &icon);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *) override;

private:
    Theme m_theme;
    QIcon m_icon;
    QString m_label;
};

// Translucent rounded backdrop for the docked sidebar content: dark tinted
// fill with a 1 px inner top highlight, painted over the window vibrancy.
class SidebarPanel final : public QWidget {
    Q_OBJECT
public:
    explicit SidebarPanel(const Theme &theme, QWidget *parent = nullptr);
    void setRadius(int px) { m_radius = px; update(); }

protected:
    void paintEvent(QPaintEvent *) override;

private:
    Theme m_theme;
    int m_radius = 12;
};

// Displays a cached pixmap of the neighbouring profile's sidebar during a
// swipe. Opacity + horizontal parallax follow the gesture progress.
class SidebarPreviewPane final : public QWidget {
    Q_OBJECT
public:
    explicit SidebarPreviewPane(QWidget *parent = nullptr);

    void setSnapshot(const QPixmap &pixmap);
    void setReveal(qreal opacity, int parallaxDx);
    bool hasSnapshot() const { return !m_pixmap.isNull(); }
    void clearSnapshot();

protected:
    void paintEvent(QPaintEvent *) override;

private:
    QPixmap m_pixmap;
    qreal m_opacity = 1.0;
    int m_parallaxDx = 0;
};

}  // namespace ui
