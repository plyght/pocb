#pragma once

#include "DownloadManager.hpp"
#include "Theme.hpp"

#include <QHash>
#include <QPointer>
#include <QWidget>

class QLabel;
class QScrollArea;
class QToolButton;
class QVBoxLayout;
class DownloadRowWidget;

// Frameless, Liquid Glass popover listing DownloadManager's items. Shown
// under a toolbar button via showAnchoredTo().
class DownloadsPopover final : public QWidget {
    Q_OBJECT
public:
    explicit DownloadsPopover(const Theme &theme, QWidget *parent = nullptr);

    void showAnchoredTo(QWidget *anchor);
    void hidePopover();

signals:
    void openRequested(const QString &id);
    void renameSuggested(const QString &id, const QString &suggestion);

protected:
    void showEvent(QShowEvent *e) override;
    void paintEvent(QPaintEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;

private:
    void rebuild();
    void updateRow(const QString &id);
    void relayout();
    void reposition();
    void showContextMenu(const QString &id, const QPoint &globalPos);
    void promptRename(const QString &id);

    Theme m_theme;
    QLabel *m_title = nullptr;
    QToolButton *m_clearBtn = nullptr;
    QScrollArea *m_scroll = nullptr;
    QWidget *m_list = nullptr;
    QVBoxLayout *m_listLayout = nullptr;
    QLabel *m_empty = nullptr;
    QHash<QString, DownloadRowWidget *> m_rows;
    QPointer<QWidget> m_anchor;
};
