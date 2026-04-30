#pragma once

#include "Theme.hpp"

#include <QHash>
#include <QTreeWidgetItem>
#include <QWidget>

class QTreeWidget;
class WebView;

class MiniWindowDragPreview final : public QWidget {
public:
    MiniWindowDragPreview(const Theme &theme, QTreeWidget *tabs,
                          const QHash<QTreeWidgetItem *, WebView *> &views,
                          QTreeWidgetItem *item);

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *) override;

private:
    static QSize previewSize();

    Theme m_theme;
    QTreeWidget *m_tabs = nullptr;
    QHash<QTreeWidgetItem *, WebView *> m_views;
    QTreeWidgetItem *m_item = nullptr;
};
