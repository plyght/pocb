#include "AddressBarController.hpp"

#include "MacIntegration.hpp"

#include <QApplication>
#include <QSettings>
#include <QEvent>
#include <QFocusEvent>
#include <QFont>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

namespace {
struct AddrEngine {
    QString host;
    QString path;
    QList<QPair<QString, QString>> extraParams;
    QString queryParam = "q";
};
AddrEngine engineForHost(const QString &host) {
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

AddressBarController::AddressBarController(QLineEdit *bar, QLabel *lockIcon, const Theme &theme, QObject *parent)
    : QObject(parent), m_bar(bar), m_lockIcon(lockIcon), m_theme(theme) {
    m_bar->installEventFilter(this);
    // Click-only focus: don't let the address bar grab initial / tab focus on
    // launch (which would put the controller into editing mode before any
    // URL was set, suppressing all subsequent setDisplayUrl updates).
    m_bar->setFocusPolicy(Qt::ClickFocus);
    m_iconColor = m_theme.muted;
    m_showFull = QSettings().value("ui/showFullUrl", false).toBool();
    if (m_lockIcon) m_lockIcon->setVisible(false);

    m_net = new QNetworkAccessManager(this);
    m_debounce = new QTimer(this);
    m_debounce->setSingleShot(true);
    m_debounce->setInterval(120);
    connect(m_debounce, &QTimer::timeout, this, &AddressBarController::fetchSuggestions);
    connect(m_bar, &QLineEdit::textEdited, this, [this](const QString &t) {
        m_pendingQuery = t.trimmed();
        if (m_pendingQuery.isEmpty()) { hidePopup(); return; }
        m_debounce->start();
    });
    connect(m_bar, &QLineEdit::returnPressed, this, &AddressBarController::commit);
}

void AddressBarController::setDisplayUrl(const QString &urlString, bool isHttps) {
    m_currentUrl = urlString;
    m_isHttps = isHttps;
    renderLock();
    if (!m_editing) applyDisplay();
}

void AddressBarController::setIconColor(const QColor &color) {
    if (color.isValid()) m_iconColor = color;
    renderLock();
}

void AddressBarController::setShowFullUrl(bool full) {
    if (m_showFull == full) return;
    m_showFull = full;
    if (!m_editing) applyDisplay();
}

void AddressBarController::renderLock() {
    if (!m_lockIcon) return;
    // Hidden for blank/data URLs; shown for any real navigation.
    const bool real = !m_currentUrl.isEmpty()
                       && m_currentUrl != QStringLiteral("about:blank")
                       && !m_currentUrl.startsWith("data:");
    m_lockIcon->setVisible(real);
    if (!real) return;

    if (m_isHttps) {
        QColor c = m_iconColor;
        c.setAlpha(170);
        m_lockIcon->setPixmap(mac::sfSymbolIcon("lock.fill", 11.0, c).pixmap(14, 14));
    } else {
        // System red for the unlocked / insecure state.
        const QColor red(255, 69, 58);
        m_lockIcon->setPixmap(mac::sfSymbolIcon("lock.slash.fill", 11.0, red).pixmap(14, 14));
    }
}

QString AddressBarController::prettify(const QString &fullUrl) const {
    if (fullUrl.isEmpty() || fullUrl == QStringLiteral("about:blank")) return QString();
    QUrl u = QUrl::fromUserInput(fullUrl);
    if (!u.isValid() || u.host().isEmpty()) return fullUrl;
    QString host = u.host();
    if (host.startsWith("www.")) host = host.mid(4);
    QString path = u.path();
    if (path == "/" || path.isEmpty()) return host;
    // Drop trailing slash for visual cleanliness; keep query/fragment off.
    while (path.endsWith('/')) path.chop(1);
    return host + path;
}

void AddressBarController::applyDisplay() {
    if (!m_bar) return;
    if (m_currentUrl.isEmpty() || m_currentUrl == QStringLiteral("about:blank")) {
        m_bar->setText(QString());
        return;
    }
    m_bar->setText(m_showFull ? m_currentUrl : prettify(m_currentUrl));
    m_bar->setCursorPosition(0);
}

void AddressBarController::endEditing(bool restoreUrl, const QString &currentUrl) {
    hidePopup();
    m_editing = false;
    if (!currentUrl.isEmpty()) m_currentUrl = currentUrl;
    if (restoreUrl && m_bar) applyDisplay();
}

bool AddressBarController::eventFilter(QObject *obj, QEvent *ev) {
    if (obj != m_bar) return QObject::eventFilter(obj, ev);
    if (ev->type() == QEvent::FocusIn) {
        beginEditing();
    } else if (ev->type() == QEvent::FocusOut) {
        QWidget *now = QApplication::focusWidget();
        if (!m_popup || (now != m_popup && now != m_popup->viewport())) {
            endEditing(/*restoreUrl=*/true, m_savedUrl);
        }
    } else if (ev->type() == QEvent::KeyPress) {
        auto *ke = static_cast<QKeyEvent *>(ev);
        if (ke->key() == Qt::Key_Escape) {
            endEditing(/*restoreUrl=*/true, m_savedUrl);
            emit escapePressed();
            return true;
        }
        if ((ke->key() == Qt::Key_Down || ke->key() == Qt::Key_Up) &&
            m_popup && m_popup->isVisible() && m_popup->count() > 0) {
            int row = m_popup->currentRow();
            if (ke->key() == Qt::Key_Down)
                row = (row + 1) % m_popup->count();
            else
                row = (row <= 0) ? m_popup->count() - 1 : row - 1;
            m_popup->setCurrentRow(row);
            m_bar->setText(m_popup->item(row)->text());
            return true;
        }
    }
    return QObject::eventFilter(obj, ev);
}

void AddressBarController::beginEditing() {
    if (m_editing) return;
    m_editing = true;
    m_savedUrl = m_currentUrl;
    // Swap to the full URL for editing — never the prettified form, since
    // editing a domain-only string would mangle paths and queries on commit.
    if (m_bar && !m_currentUrl.isEmpty() && m_currentUrl != QStringLiteral("about:blank")) {
        m_bar->setText(m_currentUrl);
    }
    QTimer::singleShot(0, this, [this] {
        if (m_bar->hasFocus()) m_bar->selectAll();
    });
}

void AddressBarController::commit() {
    QString text;
    if (m_popup && m_popup->isVisible() && m_popup->currentItem()) {
        text = m_popup->currentItem()->text();
    } else {
        text = m_bar->text();
    }
    hidePopup();
    if (text.trimmed().isEmpty()) return;
    m_editing = false;
    emit submitted(text);
}

void AddressBarController::fetchSuggestions() {
    if (m_pendingQuery.isEmpty()) return;
    if (m_inflight) {
        m_inflight->abort();
        m_inflight->deleteLater();
        m_inflight = nullptr;
    }
    const QString engineHost = QUrl(m_searchEngine).host();
    const auto eng = engineForHost(engineHost);
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
    QNetworkReply *reply = m_net->get(req);
    reply->setProperty("query", m_pendingQuery);
    m_inflight = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (reply != m_inflight) return;
        m_inflight = nullptr;
        if (reply->error() != QNetworkReply::NoError) return;
        if (!m_bar || reply->property("query").toString() != m_bar->text().trimmed()) return;

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
        if (items.size() > 8) items = items.mid(0, 8);
        populatePopup(items);
    });
}

void AddressBarController::populatePopup(const QStringList &items) {
    if (!m_popup) {
        m_popup = new QListWidget(nullptr);
        m_popup->setWindowFlags(Qt::Popup | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
        m_popup->setAttribute(Qt::WA_TranslucentBackground);
        m_popup->setObjectName("AddrPopup");
        m_popup->setFrameShape(QFrame::NoFrame);
        m_popup->setFocusPolicy(Qt::NoFocus);
        m_popup->setUniformItemSizes(true);
        m_popup->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_popup->setAttribute(Qt::WA_ShowWithoutActivating, true);
        QFont f = m_popup->font();
        f.setFamily(m_theme.fontFamily);
        f.setPointSize(m_theme.regularSize);
        m_popup->setFont(f);
        m_popup->setStyleSheet(QString(
            "QListWidget#AddrPopup {"
            "  background: %1;"
            "  border: 1px solid %2;"
            "  border-radius: 10px;"
            "  padding: 6px 0px;"
            "  color: %3;"
            "}"
            "QListWidget#AddrPopup::item {"
            "  padding: 8px 12px;"
            "  margin: 1px 6px;"
            "  border-radius: 7px;"
            "  color: %3;"
            "}"
            "QListWidget#AddrPopup::item:selected { background: %4; }"
            "QListWidget#AddrPopup::item:hover:!selected { background: %5; }"
            "QListWidget#AddrPopup QScrollBar:vertical { background: transparent; width: 6px; margin: 4px 2px; }"
            "QListWidget#AddrPopup QScrollBar::handle:vertical { background: %2; border-radius: 3px; min-height: 24px; }"
            "QListWidget#AddrPopup QScrollBar::add-line, QListWidget#AddrPopup QScrollBar::sub-line { height:0; width:0; }")
            .arg(m_theme.panel.name(),
                 m_theme.border.name(),
                 m_theme.foreground.name(),
                 m_theme.raised.name(),
                 m_theme.hover.name()));
        connect(m_popup, &QListWidget::itemClicked, this, [this](QListWidgetItem *it) {
            if (!it) return;
            m_bar->setText(it->text());
            commit();
        });
    }
    m_popup->clear();
    if (items.isEmpty()) { hidePopup(); return; }
    for (const auto &s : items) {
        auto *it = new QListWidgetItem(s, m_popup);
        it->setSizeHint(QSize(0, 32));
    }
    m_popup->setCurrentRow(-1);
    showPopup();
}

void AddressBarController::positionPopup() {
    if (!m_popup || !m_bar) return;
    QWidget *anchor = m_bar->parentWidget() ? m_bar->parentWidget() : m_bar;
    const QPoint topLeft = anchor->mapToGlobal(QPoint(0, anchor->height() + 6));
    const int width = anchor->width();
    int rows = qMin(m_popup->count(), 8);
    const int height = rows * 32 + 12;
    m_popup->setGeometry(topLeft.x(), topLeft.y(), width, height);
}

void AddressBarController::showPopup() {
    if (!m_popup) return;
    positionPopup();
    if (!m_popup->isVisible()) m_popup->show();
    m_popup->raise();
}

void AddressBarController::hidePopup() {
    if (m_popup && m_popup->isVisible()) m_popup->hide();
}
