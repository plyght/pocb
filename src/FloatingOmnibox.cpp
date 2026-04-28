#include "FloatingOmnibox.hpp"

#include "MacIntegration.hpp"

#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QPainterPath>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <QVBoxLayout>

namespace {
// Vicinae LauncherWindow.qml: 60px search row, 14px corner rounding,
// 1px mainWindowBorder, no shadow, full-window translucent fill.
constexpr int kPanelWidth   = 720;
constexpr int kInputHeight  = 60;
constexpr int kRowHeight    = 40;
constexpr int kMaxRows      = 7;
constexpr int kPanelRadius  = 14;
constexpr int kInputPadX    = 16;   // SearchBar.qml leftMargin/rightMargin
constexpr int kListPadV     = 4;    // GenericListView topMargin/bottomMargin
constexpr int kItemMarginX  = 6;    // SelectableDelegate left/rightMargin
constexpr float kFillAlpha  = 0.62f;  // Config.windowOpacity-ish, tuned for vibrancy

struct EngineSuggest {
    QString host;
    QString path;
    QList<QPair<QString, QString>> extraParams;
    QString queryParam = "q";
};

EngineSuggest engineFor(const QString &host) {
    const QString h = host.toLower();
    if (h.contains("duckduckgo")) return {"duckduckgo.com", "/ac/", {{"type","list"}}, "q"};
    if (h.contains("google"))     return {"suggestqueries.google.com", "/complete/search", {{"client","firefox"}}, "q"};
    if (h.contains("brave"))      return {"search.brave.com", "/api/suggest", {{"source","web"}}, "q"};
    if (h.contains("bing"))       return {"www.bing.com", "/osjson.aspx", {}, "query"};
    if (h.contains("ecosia"))     return {"ac.ecosia.org", "/", {{"type","list"}}, "q"};
    if (h.contains("startpage"))  return {"www.startpage.com", "/suggestions",
                                          {{"format","opensearch"}, {"segment","startpage.macos"}}, "q"};
    return {"duckduckgo.com", "/ac/", {{"type","list"}}, "q"};
}
}  // namespace

FloatingOmnibox::FloatingOmnibox(const Theme &theme, QWidget *parent)
    : QWidget(parent, Qt::Popup | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint),
      m_theme(theme) {
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_NoSystemBackground);
    setFocusPolicy(Qt::StrongFocus);

    // The widget IS the panel. No outer shadow margin, no inner sub-panel.
    auto *col = new QVBoxLayout(this);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(0);

    // Search row — single input, no icon (matches SearchBar.qml).
    auto *searchRow = new QWidget(this);
    searchRow->setAttribute(Qt::WA_TranslucentBackground);
    searchRow->setFixedHeight(kInputHeight);
    auto *searchLayout = new QHBoxLayout(searchRow);
    searchLayout->setContentsMargins(kInputPadX, 0, kInputPadX, 0);
    searchLayout->setSpacing(0);

    m_input = new QLineEdit(searchRow);
    m_input->setObjectName("OmniboxInput");
    m_input->setPlaceholderText("Search for anything...");
    m_input->setFrame(false);
    m_input->setClearButtonEnabled(false);
    {
        QFont f = m_input->font();
        f.setFamily(theme.fontFamily);
        // Vicinae: regularFontSize * 1.1.
        f.setPointSizeF(theme.regularSize * 1.1);
        m_input->setFont(f);
    }
    m_input->setStyleSheet(QString(
        "QLineEdit#OmniboxInput {"
        "  background: transparent;"
        "  border: none;"
        "  color: %1;"
        "  selection-background-color: %2;"
        "  selection-color: %3;"
        "  padding: 0px;"
        "}")
        .arg(theme.foreground.name(),
             theme.accent.name(),
             theme.background.name()));
    m_input->installEventFilter(this);
    searchLayout->addWidget(m_input, 1);
    col->addWidget(searchRow);

    // 1px hairline divider (only when suggestions are present).
    m_divider = new QWidget(this);
    m_divider->setFixedHeight(1);
    m_divider->setStyleSheet(QString("background: %1;").arg(theme.borderSoft.name()));
    m_divider->hide();
    col->addWidget(m_divider);

    // Suggestions list — SelectableDelegate metrics: 6px lateral margins,
    // 10px radius, blended hover/selection backgrounds.
    m_list = new QListWidget(this);
    m_list->setObjectName("OmniboxList");
    m_list->setFrameShape(QFrame::NoFrame);
    m_list->setUniformItemSizes(true);
    m_list->setAttribute(Qt::WA_TranslucentBackground);
    m_list->viewport()->setAutoFillBackground(false);
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_list->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_list->setFocusPolicy(Qt::NoFocus);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    {
        QFont rf = m_list->font();
        rf.setFamily(theme.fontFamily);
        rf.setPointSize(theme.regularSize);
        m_list->setFont(rf);
    }
    m_list->setStyleSheet(QString(
        "QListWidget#OmniboxList {"
        "  background: transparent;"
        "  border: none;"
        "  outline: none;"
        "  padding: %1px 0px;"
        "}"
        "QListWidget#OmniboxList::item {"
        "  color: %2;"
        "  padding: 8px 12px;"
        "  border-radius: 10px;"
        "  margin: 1px %3px;"
        "}"
        "QListWidget#OmniboxList::item:selected {"
        "  background: %4;"
        "  color: %2;"
        "}"
        "QListWidget#OmniboxList::item:hover:!selected {"
        "  background: %5;"
        "}"
        "QListWidget#OmniboxList QScrollBar:vertical {"
        "  background: transparent; width: 6px; margin: 4px 2px;"
        "}"
        "QListWidget#OmniboxList QScrollBar::handle:vertical {"
        "  background: %6; border-radius: 3px; min-height: 24px;"
        "}"
        "QListWidget#OmniboxList QScrollBar::add-line, QListWidget#OmniboxList QScrollBar::sub-line {"
        "  height: 0; width: 0;"
        "}")
        .arg(QString::number(kListPadV),
             theme.foreground.name(),
             QString::number(kItemMarginX),
             theme.raised.name(),
             theme.hover.name(),
             theme.border.name()));
    m_list->hide();
    col->addWidget(m_list);

    m_net = new QNetworkAccessManager(this);
    m_debounce = new QTimer(this);
    m_debounce->setSingleShot(true);
    m_debounce->setInterval(120);
    connect(m_debounce, &QTimer::timeout, this, &FloatingOmnibox::fetchSuggestions);
    connect(m_net, &QNetworkAccessManager::finished, this, &FloatingOmnibox::onSuggestionsReceived);

    connect(m_input, &QLineEdit::textEdited, this, &FloatingOmnibox::onTextEdited);
    connect(m_input, &QLineEdit::returnPressed, this, &FloatingOmnibox::acceptCurrent);
    connect(m_list, &QListWidget::itemActivated, this, [this](QListWidgetItem *item) {
        if (item) emit submitted(item->text());
        close();
    });
    connect(m_list, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        if (item) emit submitted(item->text());
        close();
    });

    m_engineHost = "duckduckgo.com";
    relayout();
}

void FloatingOmnibox::setSearchEngineUrl(const QString &templateUrl) {
    const QUrl u(templateUrl);
    if (u.isValid() && !u.host().isEmpty()) m_engineHost = u.host();
}

void FloatingOmnibox::showFor(QWidget *anchor, const QString &initialText) {
    if (anchor) {
        const QPoint topLeft = anchor->mapToGlobal(QPoint(0, 0));
        const int x = topLeft.x() + (anchor->width() - width()) / 2;
        // Vicinae sits at Screen.height/3; over the web viewport, ~38% reads similarly.
        const int y = topLeft.y() + (anchor->height() - height()) * 38 / 100;
        move(x, y);
    }
    setSuggestions({});
    m_input->setText(initialText);
    m_input->selectAll();
    show();
    raise();
    activateWindow();
    m_input->setFocus(Qt::ShortcutFocusReason);
}

void FloatingOmnibox::showEvent(QShowEvent *e) {
    QWidget::showEvent(e);
    // Real translucency: NSVisualEffectView (Popover material) behind our
    // painted fill, with the NSView layer corner-radius'd so the blur
    // clips to the rounded shape. No-op off macOS.
    mac::applyVibrancyBehind(this, mac::VibrancyMaterial::Popover);
    mac::roundWidgetCorners(this, kPanelRadius);
    m_input->setFocus(Qt::ShortcutFocusReason);
}

void FloatingOmnibox::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    QPainterPath path;
    path.addRoundedRect(r, kPanelRadius, kPanelRadius);

    QColor fill = m_theme.background;
    fill.setAlphaF(kFillAlpha);
    p.fillPath(path, fill);

    QPen pen(m_theme.border);
    pen.setWidthF(1.0);
    p.setPen(pen);
    p.drawPath(path);
}

void FloatingOmnibox::keyPressEvent(QKeyEvent *e) {
    if (e->key() == Qt::Key_Escape) { close(); return; }
    QWidget::keyPressEvent(e);
}

bool FloatingOmnibox::eventFilter(QObject *obj, QEvent *ev) {
    if (obj == m_input && ev->type() == QEvent::KeyPress) {
        auto *ke = static_cast<QKeyEvent *>(ev);
        if (ke->key() == Qt::Key_Down && m_list->count() > 0) {
            int row = m_list->currentRow();
            row = (row + 1) % m_list->count();
            m_list->setCurrentRow(row);
            return true;
        }
        if (ke->key() == Qt::Key_Up && m_list->count() > 0) {
            int row = m_list->currentRow();
            row = (row <= 0) ? m_list->count() - 1 : row - 1;
            m_list->setCurrentRow(row);
            return true;
        }
        if (ke->key() == Qt::Key_Escape) { close(); return true; }
    }
    return QWidget::eventFilter(obj, ev);
}

void FloatingOmnibox::onTextEdited(const QString &text) {
    m_pendingQuery = text.trimmed();
    if (m_pendingQuery.isEmpty()) { setSuggestions({}); return; }
    m_debounce->start();
}

void FloatingOmnibox::fetchSuggestions() {
    if (m_pendingQuery.isEmpty()) return;
    if (m_inflight) {
        m_inflight->abort();
        m_inflight->deleteLater();
        m_inflight = nullptr;
    }
    const auto eng = engineFor(m_engineHost);
    QUrl url;
    url.setScheme("https");
    url.setHost(eng.host);
    url.setPath(eng.path);
    QUrlQuery q;
    for (const auto &p : eng.extraParams) q.addQueryItem(p.first, p.second);
    q.addQueryItem(eng.queryParam, m_pendingQuery);
    url.setQuery(q);
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  "Mozilla/5.0 (Macintosh; Intel Mac OS X 14_0) AppleWebKit/605 (KHTML, like Gecko) pocb");
    req.setRawHeader("Accept", "application/json,text/javascript,*/*;q=0.1");
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    m_inflight = m_net->get(req);
    m_inflight->setProperty("query", m_pendingQuery);
}

void FloatingOmnibox::onSuggestionsReceived(QNetworkReply *reply) {
    reply->deleteLater();
    if (reply != m_inflight) return;
    m_inflight = nullptr;
    if (reply->error() != QNetworkReply::NoError) return;
    if (reply->property("query").toString() != m_input->text().trimmed()) return;

    const QByteArray body = reply->readAll();
    const auto doc = QJsonDocument::fromJson(body);
    QStringList items;
    if (doc.isArray() && doc.array().size() >= 2 && doc.array().at(1).isArray()) {
        for (const auto &v : doc.array().at(1).toArray()) {
            const QString s = v.toString();
            if (!s.isEmpty()) items << s;
        }
    } else if (doc.isArray()) {
        for (const auto &v : doc.array()) {
            if (v.isString()) items << v.toString();
            else if (v.isObject()) {
                const auto o = v.toObject();
                const QString s = o.value("phrase").toString(o.value("suggestion").toString());
                if (!s.isEmpty()) items << s;
            }
        }
    }
    if (items.size() > kMaxRows) items = items.mid(0, kMaxRows);
    setSuggestions(items);
}

void FloatingOmnibox::setSuggestions(const QStringList &items) {
    m_list->clear();
    if (items.isEmpty()) {
        m_list->hide();
        m_divider->hide();
        relayout();
        return;
    }
    for (const auto &s : items) {
        auto *it = new QListWidgetItem(s, m_list);
        it->setSizeHint(QSize(0, kRowHeight));
    }
    m_divider->show();
    m_list->show();
    m_list->setCurrentRow(-1);
    relayout();
}

void FloatingOmnibox::relayout() {
    int visibleRows = qMin(m_list->count(), kMaxRows);
    int listHeight = visibleRows == 0 ? 0 : visibleRows * kRowHeight + kListPadV * 2;
    int divH = visibleRows == 0 ? 0 : 1;
    setFixedSize(kPanelWidth, kInputHeight + divH + listHeight);
    m_list->setFixedHeight(listHeight);
}

void FloatingOmnibox::acceptCurrent() {
    QString text = m_list->currentItem()
                       ? m_list->currentItem()->text()
                       : m_input->text();
    emit submitted(text);
    close();
}
