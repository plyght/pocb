#include "DownloadsPopover.hpp"

#include "LayoutMetrics.hpp"
#include "MacIntegration.hpp"

#include <QApplication>
#include <QClipboard>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <functional>

namespace {
constexpr int kWidth = 320;
constexpr int kRadius = ui::metrics::FloatingPanelRadius;
constexpr int kHeaderHeight = 40;
constexpr int kRowHeight = 56;
constexpr int kSuggestionHeight = 30;
constexpr int kMaxListHeight = kRowHeight * 6;
constexpr int kIconSize = 32;
constexpr int kStatusSize = 20;
constexpr int kHoverButton = 24;
constexpr int kPadX = 12;
constexpr double kFillAlpha = 0.72;

QString subtitleFor(const DownloadItem &it) {
    switch (it.state) {
        case DownloadItem::Running: {
            QString s;
            if (it.total > 0) s = QStringLiteral("%1 of %2").arg(humanSize(it.received), humanSize(it.total));
            else s = it.received > 0 ? humanSize(it.received) : QStringLiteral("Starting…");
            if (it.bytesPerSecond > 1.0) s += QStringLiteral(" – %1/s").arg(humanSize(static_cast<qint64>(it.bytesPerSecond)));
            return s;
        }
        case DownloadItem::Completed:
            return it.received > 0 ? QStringLiteral("%1 – Completed").arg(humanSize(it.received)) : QStringLiteral("Completed");
        case DownloadItem::Failed:
            return it.error.isEmpty() ? QStringLiteral("Failed") : QStringLiteral("Failed – %1").arg(it.error);
        case DownloadItem::Cancelled:
            return QStringLiteral("Cancelled");
    }
    return QString();
}
}  // namespace

// One download. Painted by hand so the ring, elided text and hover buttons
// stay pixel-aligned with the rest of the chrome.
class DownloadRowWidget final : public QWidget {
public:
    DownloadRowWidget(const Theme &theme, const DownloadItem &item, QWidget *parent)
        : QWidget(parent), m_theme(theme), m_item(item) {
        setMouseTracking(true);
        setAttribute(Qt::WA_Hover);
        setCursor(Qt::ArrowCursor);
        m_magnifier = mac::sfSymbolIcon(QStringLiteral("magnifyingglass"), 12, theme.foreground);
        m_close = mac::sfSymbolIcon(QStringLiteral("xmark"), 11, theme.foreground);
        // Multi-layer "*.circle.fill" symbols collapse to a solid disc with a
        // single palette colour, so the glyphs are composited by hand.
        m_check = mac::sfSymbolIcon(QStringLiteral("checkmark"), 10, QColor("#0e0e10"));
        m_warn = mac::sfSymbolIcon(QStringLiteral("exclamationmark"), 10, QColor("#0e0e10"));
        m_stop = mac::sfSymbolIcon(QStringLiteral("xmark"), 9, QColor("#0e0e10"));
        setItem(item);
    }

    void setItem(const DownloadItem &item) {
        const bool iconStale = m_icon.isNull() || item.path != m_item.path || item.state != m_item.state;
        m_item = item;
        if (iconStale) m_icon = DownloadManager::fileIcon(item.path.isEmpty() ? item.fileName : item.path, kIconSize);
        setFixedHeight(kRowHeight + (hasSuggestion() ? kSuggestionHeight : 0));
        update();
    }

    const DownloadItem &item() const { return m_item; }
    bool hasSuggestion() const {
        return m_item.state == DownloadItem::Completed && !m_item.suggestedName.isEmpty() && m_item.suggestedName != m_item.fileName;
    }

    std::function<void()> onOpen, onReveal, onCloseOrCancel, onUseSuggestion, onKeepName;
    std::function<void(const QPoint &)> onContextMenu;

protected:
    void enterEvent(QEnterEvent *) override { m_hover = true; update(); }
    void leaveEvent(QEvent *) override { m_hover = false; m_hot = None; update(); }

    void mouseMoveEvent(QMouseEvent *e) override {
        const Hit h = hitTest(e->pos());
        if (h != m_hot) {
            m_hot = h;
            update();
        }
        QWidget::mouseMoveEvent(e);
    }

    void mousePressEvent(QMouseEvent *e) override {
        if (e->button() == Qt::RightButton) {
            if (onContextMenu) onContextMenu(e->globalPosition().toPoint());
            return;
        }
        if (e->button() != Qt::LeftButton) return;
        switch (hitTest(e->pos())) {
            case Reveal: if (onReveal) onReveal(); break;
            case Close: if (onCloseOrCancel) onCloseOrCancel(); break;
            case Use: if (onUseSuggestion) onUseSuggestion(); break;
            case Keep: if (onKeepName) onKeepName(); break;
            case None: break;
        }
    }

    void mouseDoubleClickEvent(QMouseEvent *e) override {
        if (e->button() == Qt::LeftButton && hitTest(e->pos()) == None && onOpen) onOpen();
    }

    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const QRect r = rect();
        const QRect main(0, 0, r.width(), kRowHeight);

        if (m_hover) {
            QColor hover = m_theme.hover;
            hover.setAlphaF(0.55);
            QPainterPath path;
            path.addRoundedRect(QRectF(main).adjusted(6, 2, -6, -2), 8, 8);
            p.fillPath(path, hover);
        }

        // Icon
        const QRect iconRect(kPadX, main.center().y() - kIconSize / 2, kIconSize, kIconSize);
        if (!m_icon.isNull()) m_icon.paint(&p, iconRect, Qt::AlignCenter, m_item.state == DownloadItem::Running ? QIcon::Disabled : QIcon::Normal);
        if (m_item.state == DownloadItem::Running) {
            p.setOpacity(0.35);
            if (!m_icon.isNull()) m_icon.paint(&p, iconRect);
            p.setOpacity(1.0);
        }

        // Text block
        const int rightZone = m_hover ? (kHoverButton * 2 + 6) : kStatusSize;
        const int textLeft = iconRect.right() + 12;
        const int textRight = r.width() - kPadX - rightZone - 8;
        QFont nameFont = font();
        nameFont.setPointSize(m_theme.regularSize);
        nameFont.setWeight(QFont::Medium);
        QFont subFont = font();
        subFont.setPointSize(m_theme.smallSize);

        p.setFont(nameFont);
        p.setPen(m_item.state == DownloadItem::Cancelled || m_item.state == DownloadItem::Failed ? m_theme.muted : m_theme.foreground);
        const QString name = QFontMetrics(nameFont).elidedText(m_item.fileName, Qt::ElideMiddle, textRight - textLeft);
        p.drawText(QRect(textLeft, main.top() + 11, textRight - textLeft, 18), Qt::AlignLeft | Qt::AlignVCenter, name);

        p.setFont(subFont);
        p.setPen(m_item.state == DownloadItem::Failed ? m_theme.danger : m_theme.muted);
        const QString sub = QFontMetrics(subFont).elidedText(subtitleFor(m_item), Qt::ElideRight, textRight - textLeft);
        p.drawText(QRect(textLeft, main.top() + 30, textRight - textLeft, 16), Qt::AlignLeft | Qt::AlignVCenter, sub);

        // Right side: hover buttons or status glyph
        if (m_hover) {
            if (m_item.state == DownloadItem::Completed) paintButton(p, revealRect(), m_magnifier, m_hot == Reveal);
            paintButton(p, closeRect(), m_close, m_hot == Close);
        } else {
            const QRect s = statusRect();
            switch (m_item.state) {
                case DownloadItem::Running: paintRing(p, s); break;
                case DownloadItem::Completed: paintBadge(p, s, QColor("#4cd964"), m_check); break;
                case DownloadItem::Failed: paintBadge(p, s, m_theme.danger, m_warn); break;
                case DownloadItem::Cancelled: paintBadge(p, s, m_theme.subtle, m_stop); break;
            }
        }

        if (hasSuggestion()) {
            const QRect strip(0, kRowHeight, r.width(), kSuggestionHeight);
            p.setFont(subFont);
            p.setPen(m_theme.muted);
            const QRect keep = keepRect();
            const QRect use = useRect();
            const int textW = use.left() - 8 - (kPadX + 6);
            const QString label = QFontMetrics(subFont).elidedText(
                QStringLiteral("Rename to “%1”?").arg(m_item.suggestedName), Qt::ElideMiddle, textW);
            p.drawText(QRect(kPadX + 6, strip.top(), textW, strip.height() - 6), Qt::AlignLeft | Qt::AlignVCenter, label);
            paintPill(p, use, QStringLiteral("Use"), true, m_hot == Use);
            paintPill(p, keep, QStringLiteral("Keep"), false, m_hot == Keep);
        }
    }

private:
    enum Hit { None, Reveal, Close, Use, Keep };

    QRect statusRect() const {
        return QRect(width() - kPadX - kStatusSize, kRowHeight / 2 - kStatusSize / 2, kStatusSize, kStatusSize);
    }
    QRect closeRect() const {
        return QRect(width() - kPadX - kHoverButton + 2, kRowHeight / 2 - kHoverButton / 2, kHoverButton, kHoverButton);
    }
    QRect revealRect() const { return closeRect().translated(-(kHoverButton + 4), 0); }
    QRect useRect() const { return QRect(width() - kPadX - 6 - 44 - 4 - 44, kRowHeight + 2, 44, 20); }
    QRect keepRect() const { return QRect(width() - kPadX - 6 - 44, kRowHeight + 2, 44, 20); }

    Hit hitTest(const QPoint &pos) const {
        if (hasSuggestion()) {
            if (useRect().contains(pos)) return Use;
            if (keepRect().contains(pos)) return Keep;
        }
        if (pos.y() < kRowHeight) {
            if (m_item.state == DownloadItem::Completed && revealRect().contains(pos)) return Reveal;
            if (closeRect().contains(pos)) return Close;
        }
        return None;
    }

    void paintButton(QPainter &p, const QRect &r, const QIcon &icon, bool hot) const {
        QColor bg = m_theme.foreground;
        bg.setAlphaF(hot ? 0.22 : 0.12);
        p.setPen(Qt::NoPen);
        p.setBrush(bg);
        p.drawEllipse(r);
        icon.paint(&p, r.adjusted(6, 6, -6, -6));
    }

    void paintBadge(QPainter &p, const QRect &r, const QColor &fill, const QIcon &glyph) const {
        p.setPen(Qt::NoPen);
        p.setBrush(fill);
        p.drawEllipse(QRectF(r).adjusted(1, 1, -1, -1));
        glyph.paint(&p, r.adjusted(4, 4, -4, -4));
    }

    void paintPill(QPainter &p, const QRect &r, const QString &text, bool primary, bool hot) const {
        QColor bg = primary ? m_theme.accent : m_theme.raised;
        if (hot) bg = bg.lighter(115);
        QPainterPath path;
        path.addRoundedRect(QRectF(r), 6, 6);
        p.fillPath(path, bg);
        QFont f = font();
        f.setPointSize(m_theme.smallSize);
        f.setWeight(QFont::DemiBold);
        p.setFont(f);
        p.setPen(primary ? QColor("#1a1a1a") : m_theme.foreground);
        p.drawText(r, Qt::AlignCenter, text);
    }

    void paintRing(QPainter &p, const QRect &r) const {
        QRectF ring = QRectF(r).adjusted(1.5, 1.5, -1.5, -1.5);
        QPen track(m_theme.border);
        track.setWidthF(2.5);
        p.setPen(track);
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(ring);
        QPen bar(m_theme.accent);
        bar.setWidthF(2.5);
        bar.setCapStyle(Qt::RoundCap);
        p.setPen(bar);
        if (m_item.total > 0) {
            const double frac = qBound(0.0, static_cast<double>(m_item.received) / static_cast<double>(m_item.total), 1.0);
            p.drawArc(ring, 90 * 16, static_cast<int>(-frac * 360 * 16));
        } else {
            // Indeterminate: short spinning arc driven by the row's timer.
            p.drawArc(ring, m_spin * 16, -100 * 16);
        }
        // Inner stop square
        QColor sq = m_theme.foreground;
        sq.setAlphaF(0.85);
        p.setPen(Qt::NoPen);
        p.setBrush(sq);
        p.drawRoundedRect(QRectF(r.center().x() - 3, r.center().y() - 3, 7, 7), 1.5, 1.5);
    }

    void timerEvent(QTimerEvent *) override {
        if (m_item.state != DownloadItem::Running || m_item.total > 0) return;
        m_spin = (m_spin - 12 + 360) % 360;
        update();
    }

    void showEvent(QShowEvent *) override { if (!m_timerId) m_timerId = startTimer(66); }
    void hideEvent(QHideEvent *) override { if (m_timerId) { killTimer(m_timerId); m_timerId = 0; } }

    Theme m_theme;
    DownloadItem m_item;
    QIcon m_icon, m_magnifier, m_close, m_check, m_warn, m_stop;
    bool m_hover = false;
    Hit m_hot = None;
    int m_spin = 90;
    int m_timerId = 0;
};

DownloadsPopover::DownloadsPopover(const Theme &theme, QWidget *parent)
    : QWidget(parent, Qt::Popup | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint),
      m_theme(theme) {
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_NoSystemBackground);
    setFixedWidth(kWidth);
    setFocusPolicy(Qt::StrongFocus);

    auto *col = new QVBoxLayout(this);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(0);

    auto *header = new QWidget(this);
    header->setAttribute(Qt::WA_TranslucentBackground);
    header->setFixedHeight(kHeaderHeight);
    auto *hl = new QHBoxLayout(header);
    hl->setContentsMargins(kPadX + 4, 0, kPadX, 0);
    hl->setSpacing(6);
    m_title = new QLabel(QStringLiteral("Downloads"), header);
    QFont tf = m_title->font();
    tf.setPointSize(theme.regularSize);
    tf.setWeight(QFont::DemiBold);
    m_title->setFont(tf);
    m_title->setStyleSheet(QStringLiteral("color: %1; background: transparent;").arg(theme.foreground.name()));
    hl->addWidget(m_title);
    hl->addStretch(1);
    m_clearBtn = new QToolButton(header);
    m_clearBtn->setText(QStringLiteral("Clear"));
    m_clearBtn->setCursor(Qt::PointingHandCursor);
    m_clearBtn->setAutoRaise(true);
    m_clearBtn->setStyleSheet(QStringLiteral(
        "QToolButton { color: %1; background: transparent; border: none; padding: 2px 6px; font-size: %3px; }"
        "QToolButton:hover { color: %2; }").arg(theme.muted.name(), theme.foreground.name()).arg(theme.smallSize + 1));
    connect(m_clearBtn, &QToolButton::clicked, this, [] { DownloadManager::instance()->clearFinished(); });
    hl->addWidget(m_clearBtn);
    col->addWidget(header);

    m_scroll = new QScrollArea(this);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setWidgetResizable(true);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_scroll->setAttribute(Qt::WA_TranslucentBackground);
    m_scroll->viewport()->setAttribute(Qt::WA_TranslucentBackground);
    m_scroll->setStyleSheet(QStringLiteral(
        "QScrollArea, QScrollArea > QWidget > QWidget { background: transparent; }"
        "QScrollBar:vertical { background: transparent; width: 6px; margin: 2px; }"
        "QScrollBar::handle:vertical { background: %1; border-radius: 3px; min-height: 20px; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }").arg(theme.subtle.name()));
    m_list = new QWidget(m_scroll);
    m_list->setAttribute(Qt::WA_TranslucentBackground);
    m_listLayout = new QVBoxLayout(m_list);
    m_listLayout->setContentsMargins(0, 0, 0, 6);
    m_listLayout->setSpacing(0);
    m_listLayout->addStretch(1);
    m_scroll->setWidget(m_list);
    col->addWidget(m_scroll);

    m_empty = new QLabel(QStringLiteral("No downloads"), this);
    m_empty->setAlignment(Qt::AlignCenter);
    m_empty->setFixedHeight(64);
    m_empty->setStyleSheet(QStringLiteral("color: %1; background: transparent; font-size: %2px;")
                               .arg(theme.muted.name()).arg(theme.regularSize));
    col->addWidget(m_empty);

    auto *dm = DownloadManager::instance();
    connect(dm, &DownloadManager::itemAdded, this, [this](const QString &) { rebuild(); });
    connect(dm, &DownloadManager::itemUpdated, this, &DownloadsPopover::updateRow);
    connect(dm, &DownloadManager::renameSuggested, this, &DownloadsPopover::renameSuggested);

    rebuild();
}

void DownloadsPopover::showAnchoredTo(QWidget *anchor) {
    m_anchor = anchor;
    rebuild();
    reposition();
    show();
    raise();
    activateWindow();
}

void DownloadsPopover::hidePopover() { hide(); }

void DownloadsPopover::showEvent(QShowEvent *e) {
    QWidget::showEvent(e);
    mac::makeFloatingVibrantPanel(this, mac::VibrancyMaterial::Popover, kRadius);
    mac::roundWidgetCorners(this, kRadius, false);
    setFocus(Qt::PopupFocusReason);
}

void DownloadsPopover::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    QPainterPath path;
    path.addRoundedRect(r, kRadius, kRadius);
    QColor fill = m_theme.background;
    fill.setAlphaF(kFillAlpha);
    p.fillPath(path, fill);
    QPen pen(m_theme.border);
    pen.setWidthF(1.0);
    p.setPen(pen);
    p.drawPath(path);
    // Header separator
    if (!m_rows.isEmpty()) {
        QColor sep = m_theme.borderSoft;
        p.setPen(sep);
        p.drawLine(QPointF(kPadX, kHeaderHeight - 0.5), QPointF(width() - kPadX, kHeaderHeight - 0.5));
    }
}

void DownloadsPopover::keyPressEvent(QKeyEvent *e) {
    if (e->key() == Qt::Key_Escape) { hidePopover(); return; }
    QWidget::keyPressEvent(e);
}

void DownloadsPopover::rebuild() {
    auto *dm = DownloadManager::instance();
    QVector<DownloadItem> items = dm->items();
    // Newest first.
    std::reverse(items.begin(), items.end());

    QHash<QString, DownloadRowWidget *> keep;
    for (const DownloadItem &it : items) {
        DownloadRowWidget *row = m_rows.take(it.id);
        if (!row) {
            row = new DownloadRowWidget(m_theme, it, m_list);
            const QString id = it.id;
            row->onOpen = [this, id] { DownloadManager::instance()->open(id); emit openRequested(id); };
            row->onReveal = [id] { DownloadManager::instance()->revealInFinder(id); };
            row->onCloseOrCancel = [id] {
                DownloadItem d;
                auto *m = DownloadManager::instance();
                if (m->item(id, &d) && d.state == DownloadItem::Running) m->cancel(id); else m->remove(id);
            };
            row->onUseSuggestion = [id] { DownloadManager::instance()->applySuggestion(id); };
            row->onKeepName = [id] { DownloadManager::instance()->dismissSuggestion(id); };
            row->onContextMenu = [this, id](const QPoint &pos) { showContextMenu(id, pos); };
        } else {
            row->setItem(it);
        }
        keep.insert(it.id, row);
    }
    for (DownloadRowWidget *stale : m_rows) stale->deleteLater();
    m_rows = keep;

    // Re-order widgets in the layout to match `items`.
    while (m_listLayout->count() > 1) {
        QLayoutItem *li = m_listLayout->takeAt(0);
        delete li;
    }
    int idx = 0;
    for (const DownloadItem &it : items) m_listLayout->insertWidget(idx++, m_rows.value(it.id));
    for (DownloadRowWidget *row : m_rows) row->show();
    relayout();
}

void DownloadsPopover::updateRow(const QString &id) {
    DownloadItem it;
    DownloadRowWidget *row = m_rows.value(id);
    if (!DownloadManager::instance()->item(id, &it)) {
        if (row) rebuild();
        return;
    }
    if (!row) { rebuild(); return; }
    row->setItem(it);
    relayout();
}

void DownloadsPopover::relayout() {
    const bool empty = m_rows.isEmpty();
    m_empty->setVisible(empty);
    m_scroll->setVisible(!empty);
    bool anyFinished = false;
    for (DownloadRowWidget *row : m_rows) anyFinished = anyFinished || row->item().state != DownloadItem::Running;
    m_clearBtn->setVisible(anyFinished);
    int listHeight = 0;
    for (DownloadRowWidget *row : m_rows) listHeight += row->height();
    listHeight += 6;
    const int contentHeight = empty ? m_empty->height() : qMin(listHeight, kMaxListHeight);
    m_scroll->setFixedHeight(empty ? 0 : contentHeight);
    setFixedHeight(kHeaderHeight + contentHeight);
    if (isVisible()) reposition();
    update();
}

void DownloadsPopover::reposition() {
    if (!m_anchor) return;
    const QRect a(m_anchor->mapToGlobal(QPoint(0, 0)), m_anchor->size());
    QPoint pos(a.right() - width() + 1, a.bottom() + 6);
    if (QScreen *screen = m_anchor->screen()) {
        const QRect avail = screen->availableGeometry();
        pos.setX(qBound(avail.left() + 8, pos.x(), avail.right() - width() - 8));
        if (pos.y() + height() > avail.bottom() - 8) pos.setY(qMax(avail.top() + 8, a.top() - height() - 6));
    }
    move(pos);
}

void DownloadsPopover::showContextMenu(const QString &id, const QPoint &globalPos) {
    DownloadItem it;
    auto *dm = DownloadManager::instance();
    if (!dm->item(id, &it)) return;
    const bool done = it.state == DownloadItem::Completed;
    const QStringList titles{QStringLiteral("Open"), QStringLiteral("Show in Finder"), QStringLiteral("Copy Link"),
                             QStringLiteral("Rename…"), QStringLiteral("-"),
                             it.state == DownloadItem::Running ? QStringLiteral("Cancel") : QStringLiteral("Remove")};
    const QVector<bool> enabled{done, done, it.url.isValid(), done, true, true};
    std::vector<std::function<void()>> callbacks{
        [dm, id] { dm->open(id); },
        [dm, id] { dm->revealInFinder(id); },
        [it] { QGuiApplication::clipboard()->setText(it.url.toString()); },
        [this, id] { promptRename(id); },
        [dm, id, running = it.state == DownloadItem::Running] { if (running) dm->cancel(id); else dm->remove(id); },
    };
    if (mac::showNativeContextMenu(this, globalPos, titles, enabled, callbacks)) return;
    QMenu menu(this);
    int cb = 0;
    for (int i = 0; i < titles.size(); ++i) {
        if (titles[i] == QStringLiteral("-")) { menu.addSeparator(); continue; }
        QAction *act = menu.addAction(titles[i]);
        act->setEnabled(enabled[i]);
        const int index = cb++;
        connect(act, &QAction::triggered, this, [callbacks, index] { callbacks[index](); });
    }
    menu.exec(globalPos);
}

void DownloadsPopover::promptRename(const QString &id) {
    DownloadItem it;
    if (!DownloadManager::instance()->item(id, &it)) return;
    hidePopover();
    QTimer::singleShot(0, this, [id, it] {
        bool ok = false;
        const QString name = QInputDialog::getText(nullptr, QStringLiteral("Rename Download"),
                                                   QStringLiteral("New file name:"), QLineEdit::Normal, it.fileName, &ok);
        if (ok && !name.trimmed().isEmpty()) DownloadManager::instance()->rename(id, name.trimmed());
    });
}
