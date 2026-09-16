#pragma once

#include <QColor>
#include <QWidget>

class QVariantAnimation;

namespace ui {

// Hosts the unified toolbar row plus its hairline. Only the host's fixed
// height animates (expanded height -> 0); the children keep their size and
// slide up with the host's bottom edge, so no toolbar child ever relayouts
// and the web stack below simply grows into the freed space.
class CollapsingToolbarHost final : public QWidget {
    Q_OBJECT
public:
    explicit CollapsingToolbarHost(QWidget *parent = nullptr);

    void setRow(QWidget *row, QWidget *hairline);
    int expandedHeight() const;
    // 1.0 = fully expanded, 0.0 = collapsed to nothing.
    void setProgress(qreal t);
    qreal progress() const { return m_progress; }
    void syncHeight();

protected:
    void resizeEvent(QResizeEvent *) override;
    void paintEvent(QPaintEvent *) override;

private:
    void layoutChildren();

    QWidget *m_row = nullptr;
    QWidget *m_hairline = nullptr;
    qreal m_progress = 1.0;
};

// Frameless tool window parked over the top edge of the web container while
// the toolbar is collapsed. It is the 8 px hover hot zone and paints the
// centred 3 px glass grabber pill, which swells on hover.
class ToolbarGrabber final : public QWidget {
    Q_OBJECT
public:
    explicit ToolbarGrabber(QWidget *parent = nullptr);

    void setPillColor(const QColor &c);
    static constexpr int HotZoneHeight = 8;

signals:
    void hovered();

protected:
    void paintEvent(QPaintEvent *) override;
    void enterEvent(QEnterEvent *) override;
    void leaveEvent(QEvent *) override;
    void mousePressEvent(QMouseEvent *) override;

private:
    QVariantAnimation *m_anim = nullptr;
    qreal m_hover = 0.0;
    QColor m_pill = QColor(255, 255, 255, 170);
};

}  // namespace ui
