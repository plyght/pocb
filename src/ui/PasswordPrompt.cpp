#include "PasswordPrompt.hpp"

#include "LayoutMetrics.hpp"
#include "MacIntegration.hpp"

#include <QApplication>
#include <QEvent>
#include <QFont>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScreen>
#include <QTimer>
#include <QVBoxLayout>

#include <functional>

namespace {

constexpr int kWidth = 372;
constexpr int kRadius = ui::metrics::FloatingPanelRadius;
constexpr int kPadX = 14;
constexpr int kPadY = 12;
constexpr int kRowHeight = 44;
constexpr int kIconSize = 28;
constexpr double kFillAlpha = 0.78;
constexpr int kSaveAutoHideMs = 15000;

QPointer<PasswordPrompt> &currentPrompt() {
    static QPointer<PasswordPrompt> p;
    return p;
}

// One saved account line: person icon, "user" / "site", Touch ID glyph.
class AccountRow final : public QWidget {
public:
    AccountRow(const QString &user, const QString &site, const Theme &theme, std::function<void(const QString &)> onActivate, QWidget *parent)
        : QWidget(parent), m_user(user), m_site(site), m_theme(theme), m_onActivate(std::move(onActivate)) {
        setFixedHeight(kRowHeight);
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_Hover, true);
        setAttribute(Qt::WA_TranslucentBackground);
        m_person = mac::sfSymbolIcon(QStringLiteral("person.crop.circle.fill"), 20, theme.foreground).pixmap(kIconSize, kIconSize);
        m_touch = mac::sfSymbolIcon(QStringLiteral("touchid"), 15, theme.accent).pixmap(18, 18);
    }
    QString user() const { return m_user; }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        if (m_hover) {
            QColor bg = m_theme.hover;
            bg.setAlphaF(0.85);
            QPainterPath path;
            path.addRoundedRect(QRectF(rect()).adjusted(6, 2, -6, -2), 8, 8);
            p.fillPath(path, bg);
        }
        const int iconX = kPadX;
        p.drawPixmap(iconX, (height() - kIconSize) / 2, m_person);
        const int textX = iconX + kIconSize + 10;
        const int textW = width() - textX - kPadX - 26;
        QFont f = font();
        f.setPointSize(m_theme.regularSize);
        f.setWeight(QFont::Medium);
        p.setFont(f);
        p.setPen(m_theme.foreground);
        QFontMetrics fm(f);
        p.drawText(QRect(textX, 6, textW, fm.height()), Qt::AlignLeft | Qt::AlignVCenter, fm.elidedText(m_user, Qt::ElideMiddle, textW));
        QFont sf = font();
        sf.setPointSize(m_theme.smallSize);
        p.setFont(sf);
        p.setPen(m_theme.muted);
        QFontMetrics sfm(sf);
        p.drawText(QRect(textX, 6 + fm.height(), textW, sfm.height()), Qt::AlignLeft | Qt::AlignVCenter, sfm.elidedText(m_site, Qt::ElideMiddle, textW));
        p.drawPixmap(width() - kPadX - 18, (height() - 18) / 2, m_touch);
    }
    void enterEvent(QEnterEvent *) override { m_hover = true; update(); }
    void leaveEvent(QEvent *) override { m_hover = false; update(); }
    void mouseReleaseEvent(QMouseEvent *e) override {
        if (e->button() == Qt::LeftButton && rect().contains(e->pos()) && m_onActivate) m_onActivate(m_user);
    }

private:
    QString m_user;
    QString m_site;
    Theme m_theme;
    std::function<void(const QString &)> m_onActivate;
    QPixmap m_person;
    QPixmap m_touch;
    bool m_hover = false;
};

}  // namespace

// ---------------------------------------------------------------------------

PasswordPrompt *PasswordPrompt::current() { return currentPrompt(); }

void PasswordPrompt::dismissCurrent() {
    if (PasswordPrompt *p = currentPrompt()) p->dismiss();
}

PasswordPrompt *PasswordPrompt::showSaved(QWidget *anchor, const QString &site, const QStringList &accounts, const Theme &theme) {
    if (accounts.isEmpty()) return nullptr;
    auto *p = new PasswordPrompt(Kind::Saved, anchor, site, theme);
    p->buildSaved(accounts);
    p->reposition();
    p->show();
    return p;
}

PasswordPrompt *PasswordPrompt::showGenerate(QWidget *anchor, const QString &site, const QString &password, const Theme &theme) {
    if (password.isEmpty()) return nullptr;
    auto *p = new PasswordPrompt(Kind::Generate, anchor, site, theme);
    p->buildGenerate(password);
    p->reposition();
    p->show();
    return p;
}

PasswordPrompt *PasswordPrompt::showSave(QWidget *anchor, const QString &site, const QString &user, const Theme &theme) {
    auto *p = new PasswordPrompt(Kind::Save, anchor, site, theme);
    p->buildSave(user);
    p->reposition();
    p->show();
    p->m_autoHide = new QTimer(p);
    p->m_autoHide->setSingleShot(true);
    connect(p->m_autoHide, &QTimer::timeout, p, &PasswordPrompt::dismiss);
    p->m_autoHide->start(kSaveAutoHideMs);
    return p;
}

PasswordPrompt::PasswordPrompt(Kind kind, QWidget *anchor, const QString &site, const Theme &theme)
    : QWidget(nullptr, Qt::Tool | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint | Qt::WindowStaysOnTopHint | Qt::WindowDoesNotAcceptFocus),
      m_kind(kind), m_theme(theme), m_site(site), m_anchor(anchor) {
    dismissCurrent();
    currentPrompt() = this;
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_DeleteOnClose);
    setFixedWidth(kWidth);
    setFont(QFont(theme.fontFamily, theme.regularSize));

    m_col = new QVBoxLayout(this);
    m_col->setContentsMargins(0, kPadY, 0, kPadY);
    m_col->setSpacing(6);

    qApp->installEventFilter(this);
    if (anchor && anchor->window()) anchor->window()->installEventFilter(this);
}

QWidget *PasswordPrompt::makeHeader(const QString &symbol, const QString &title, const QString &subtitle) {
    auto *row = new QWidget(this);
    row->setAttribute(Qt::WA_TranslucentBackground);
    auto *hl = new QHBoxLayout(row);
    hl->setContentsMargins(kPadX, 0, kPadX, 0);
    hl->setSpacing(10);
    auto *icon = new QLabel(row);
    icon->setFixedSize(kIconSize, kIconSize);
    icon->setPixmap(mac::sfSymbolIcon(symbol, 20, m_theme.accent).pixmap(kIconSize, kIconSize));
    icon->setStyleSheet(QStringLiteral("background: transparent;"));
    hl->addWidget(icon, 0, Qt::AlignTop);
    auto *text = new QVBoxLayout;
    text->setContentsMargins(0, 0, 0, 0);
    text->setSpacing(2);
    auto *t = new QLabel(title, row);
    QFont tf = font();
    tf.setPointSize(m_theme.regularSize);
    tf.setWeight(QFont::DemiBold);
    t->setFont(tf);
    t->setWordWrap(true);
    t->setStyleSheet(QStringLiteral("color: %1; background: transparent;").arg(m_theme.foreground.name()));
    text->addWidget(t);
    if (!subtitle.isEmpty()) {
        auto *s = new QLabel(subtitle, row);
        QFont sf = font();
        sf.setPointSize(m_theme.smallSize);
        s->setFont(sf);
        s->setWordWrap(true);
        s->setStyleSheet(QStringLiteral("color: %1; background: transparent;").arg(m_theme.muted.name()));
        text->addWidget(s);
    }
    hl->addLayout(text, 1);
    return row;
}

QWidget *PasswordPrompt::makeButton(const QString &text, bool primary) {
    auto *b = new QPushButton(text, this);
    b->setCursor(Qt::PointingHandCursor);
    b->setFocusPolicy(Qt::NoFocus);
    b->setFixedHeight(26);
    QColor bg = primary ? m_theme.accent : m_theme.raised;
    QColor fg = primary ? QColor(Qt::white) : m_theme.foreground;
    QColor hover = primary ? m_theme.accent.lighter(112) : m_theme.hover;
    b->setStyleSheet(QStringLiteral(
        "QPushButton { color: %1; background: %2; border: 1px solid %4; border-radius: 7px; padding: 0 12px; font-size: %5px; font-weight: 600; }"
        "QPushButton:hover { background: %3; }"
        "QPushButton:pressed { background: %2; }")
        .arg(fg.name(), bg.name(QColor::HexArgb), hover.name(QColor::HexArgb), primary ? bg.name(QColor::HexArgb) : m_theme.border.name(QColor::HexArgb))
        .arg(m_theme.smallSize + 1));
    return b;
}

void PasswordPrompt::buildSaved(const QStringList &accounts) {
    const QString title = accounts.size() == 1
        ? QStringLiteral("Use saved password: %1").arg(accounts.first())
        : QStringLiteral("Use saved password");
    m_col->addWidget(makeHeader(QStringLiteral("key.fill"), title, QStringLiteral("Touch ID confirms before filling on %1").arg(m_site)));
    auto *list = new QWidget(this);
    list->setAttribute(Qt::WA_TranslucentBackground);
    auto *vl = new QVBoxLayout(list);
    vl->setContentsMargins(0, 4, 0, 0);
    vl->setSpacing(0);
    int shown = 0;
    for (const QString &user : accounts) {
        if (shown++ >= 5) break;
        auto *row = new AccountRow(user, m_site, m_theme, [this](const QString &u) {
            mac::performHapticFeedback();
            finish([this, u] { emit useSavedChosen(u); });
        }, list);
        vl->addWidget(row);
    }
    m_col->addWidget(list);
    auto *foot = new QHBoxLayout;
    foot->setContentsMargins(kPadX, 2, kPadX, 0);
    foot->addStretch(1);
    QWidget *notNow = makeButton(QStringLiteral("Not now"), false);
    connect(static_cast<QPushButton *>(notNow), &QPushButton::clicked, this, &PasswordPrompt::dismiss);
    foot->addWidget(notNow);
    m_col->addLayout(foot);
    adjustSize();
}

void PasswordPrompt::buildGenerate(const QString &password) {
    m_password = password;
    m_col->addWidget(makeHeader(QStringLiteral("key.fill"), QStringLiteral("Use Strong Password"),
                                QStringLiteral("pocb will save it in Apple Passwords for %1.").arg(m_site)));
    auto *pw = new QLabel(password, this);
    QFont mono(QStringLiteral("SF Mono"));
    mono.setStyleHint(QFont::Monospace);
    mono.setPointSize(m_theme.regularSize + 1);
    mono.setWeight(QFont::DemiBold);
    pw->setFont(mono);
    pw->setTextInteractionFlags(Qt::TextSelectableByMouse);
    pw->setAlignment(Qt::AlignCenter);
    pw->setFixedHeight(32);
    QColor pill = m_theme.raised;
    pill.setAlphaF(0.9);
    pw->setStyleSheet(QStringLiteral("color: %1; background: %2; border: 1px solid %3; border-radius: 8px; margin: 0 %4px;")
                          .arg(m_theme.accent.name(), pill.name(QColor::HexArgb), m_theme.border.name(QColor::HexArgb)).arg(kPadX));
    m_col->addWidget(pw);
    auto *foot = new QHBoxLayout;
    foot->setContentsMargins(kPadX, 4, kPadX, 0);
    foot->addStretch(1);
    QWidget *notNow = makeButton(QStringLiteral("Not now"), false);
    QWidget *use = makeButton(QStringLiteral("Use"), true);
    connect(static_cast<QPushButton *>(notNow), &QPushButton::clicked, this, &PasswordPrompt::dismiss);
    connect(static_cast<QPushButton *>(use), &QPushButton::clicked, this, [this] {
        mac::performHapticFeedback();
        finish([this] { emit useGeneratedChosen(m_password); });
    });
    foot->addWidget(notNow);
    foot->addWidget(use);
    m_col->addLayout(foot);
    adjustSize();
}

void PasswordPrompt::buildSave(const QString &user) {
    m_col->addWidget(makeHeader(QStringLiteral("lock.badge.clock"), QStringLiteral("Save password in Apple Passwords?"),
                                user.isEmpty() ? m_site : QStringLiteral("%1 — %2").arg(user, m_site)));
    auto *foot = new QHBoxLayout;
    foot->setContentsMargins(kPadX, 6, kPadX, 0);
    foot->setSpacing(8);
    QWidget *never = makeButton(QStringLiteral("Never for this Site"), false);
    QWidget *notNow = makeButton(QStringLiteral("Not now"), false);
    QWidget *save = makeButton(QStringLiteral("Save"), true);
    connect(static_cast<QPushButton *>(never), &QPushButton::clicked, this, [this] { finish([this] { emit neverForSiteChosen(); }); });
    connect(static_cast<QPushButton *>(notNow), &QPushButton::clicked, this, &PasswordPrompt::dismiss);
    connect(static_cast<QPushButton *>(save), &QPushButton::clicked, this, [this] {
        mac::performHapticFeedback();
        finish([this] { emit saveChosen(); });
    });
    foot->addWidget(never);
    foot->addStretch(1);
    foot->addWidget(notNow);
    foot->addWidget(save);
    m_col->addLayout(foot);
    adjustSize();
}

void PasswordPrompt::finish(std::function<void()> emitChoice) {
    if (m_done) return;
    m_done = true;
    if (emitChoice) emitChoice();
    dismiss();
}

void PasswordPrompt::dismiss() {
    if (currentPrompt() == this) currentPrompt() = nullptr;
    qApp->removeEventFilter(this);
    if (!m_done) { m_done = true; emit dismissed(); }
    hide();
    deleteLater();
}

void PasswordPrompt::showEvent(QShowEvent *e) {
    QWidget::showEvent(e);
    mac::makeFloatingVibrantPanel(this, mac::VibrancyMaterial::Popover, kRadius);
    mac::roundWidgetCorners(this, kRadius, false);
    mac::showWindowWithoutAppActivation(this);
    reposition();
}

void PasswordPrompt::paintEvent(QPaintEvent *) {
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
}

void PasswordPrompt::keyPressEvent(QKeyEvent *e) {
    if (e->key() == Qt::Key_Escape) { dismiss(); return; }
    QWidget::keyPressEvent(e);
}

bool PasswordPrompt::eventFilter(QObject *watched, QEvent *event) {
    switch (event->type()) {
    case QEvent::MouseButtonPress: {
        // Clicks anywhere outside the prompt dismiss it (Qt::Tool windows do
        // not get popup semantics for free).
        auto *me = static_cast<QMouseEvent *>(event);
        QWidget *w = qobject_cast<QWidget *>(watched);
        if (w && (w == this || isAncestorOf(w))) break;
        if (!frameGeometry().contains(me->globalPosition().toPoint())) dismiss();
        break;
    }
    case QEvent::Move:
    case QEvent::Resize:
        if (m_anchor && watched == m_anchor->window()) reposition();
        break;
    case QEvent::Hide:
    case QEvent::WindowStateChange:
        if (m_anchor && watched == m_anchor->window() && (!m_anchor->window()->isVisible() || m_anchor->window()->isMinimized())) dismiss();
        break;
    default:
        break;
    }
    return false;
}

void PasswordPrompt::reposition() {
    if (!m_anchor) return;
    const QRect a(m_anchor->mapToGlobal(QPoint(0, 0)), m_anchor->size());
    QPoint pos(a.center().x() - width() / 2, a.bottom() + 6);
    if (QScreen *screen = m_anchor->screen()) {
        const QRect avail = screen->availableGeometry();
        pos.setX(qBound(avail.left() + 8, pos.x(), avail.right() - width() - 8));
        if (pos.y() + height() > avail.bottom() - 8) pos.setY(qMax(avail.top() + 8, a.top() - height() - 6));
    }
    move(pos);
}
