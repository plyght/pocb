#include "AddressBarController.hpp"

#include "MacIntegration.hpp"

#include <QApplication>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QVBoxLayout>
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
#include <QStyle>

namespace {

// Top-level container that paints a rounded translucent background plus a
// 1 px hairline border. Hosts the suggestion QListWidget. Required because
// frameless Qt::Tool windows on macOS otherwise have no background of
// their own — the QListWidget stylesheet alone can't draw past the
// rounded corners since AppKit clips to the square frame.
class AddrPopupFrame : public QWidget {
public:
    explicit AddrPopupFrame(QWidget *parent, const QColor &fill, const QColor &border)
        : QWidget(parent), m_fill(fill), m_border(border) {
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_NoSystemBackground);
        setAutoFillBackground(false);
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        QPainterPath path;
        path.addRoundedRect(r, 12.0, 12.0);
        p.fillPath(path, m_fill);
        QPen pen(m_border);
        pen.setWidthF(1.0);
        p.setPen(pen);
        p.drawPath(path);
    }

private:
    QColor m_fill;
    QColor m_border;
};

struct AddrEngine {
    QString host;
    QString path;
    QList<QPair<QString, QString>> extraParams;
    QString queryParam = "q";
};
QString colorWithAlpha(QColor color, float alpha) {
    color.setAlphaF(alpha);
    return QString("rgba(%1, %2, %3, %4)")
        .arg(color.red())
        .arg(color.green())
        .arg(color.blue())
        .arg(QString::number(color.alphaF(), 'f', 3));
}

AddrEngine engineForHost(const QString &host) {
    const QString h = host.toLower();
    if (h.contains("duckduckgo")) return {"duckduckgo.com", "/ac/", {{"type","list"}}, "q"};
    if (h.contains("google"))     return {"suggestqueries.google.com", "/complete/search", {{"client","firefox"}}, "q"};
    if (h.contains("brave"))      return {"search.brave.com", "/api/suggest", {}, "q"};
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
    m_debounce->setInterval(80);
    connect(m_debounce, &QTimer::timeout, this, &AddressBarController::fetchSuggestions);
    connect(m_bar, &QLineEdit::textEdited, this, [this](const QString &t) {
        mac::hideCursorUntilMouseMoves();
        m_pendingQuery = t.trimmed();
        m_statusText.clear();
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

void AddressBarController::cancelEditing() {
    endEditing(/*restoreUrl=*/true, m_savedUrl);
    if (m_bar) m_bar->clearFocus();
}

void AddressBarController::endEditing(bool restoreUrl, const QString &currentUrl) {
    hidePopup();
    if (m_appFilterInstalled) {
        qApp->removeEventFilter(this);
        m_appFilterInstalled = false;
    }
    m_editing = false;
    if (!currentUrl.isEmpty()) m_currentUrl = currentUrl;
    if (restoreUrl && m_bar) applyDisplay();
}

bool AddressBarController::eventFilter(QObject *obj, QEvent *ev) {
    if (obj != m_bar) {
        if (m_editing && ev->type() == QEvent::MouseButtonPress) {
            auto *w = qobject_cast<QWidget *>(obj);
            if (w && w != m_bar && w != m_popup && !m_bar->isAncestorOf(w) && (!m_popup || !m_popup->isAncestorOf(w))) {
                endEditing(/*restoreUrl=*/true, m_savedUrl);
            }
        }
        return QObject::eventFilter(obj, ev);
    }
    if (ev->type() == QEvent::FocusIn) {
        beginEditing();
    } else if (ev->type() == QEvent::FocusOut) {
        return QObject::eventFilter(obj, ev);
    } else if (ev->type() == QEvent::KeyPress) {
        mac::hideCursorUntilMouseMoves();
        auto *ke = static_cast<QKeyEvent *>(ev);
        if (ke->key() == Qt::Key_Escape) {
            endEditing(/*restoreUrl=*/true, m_savedUrl);
            emit escapePressed();
            return true;
        }
        if ((ke->key() == Qt::Key_Down || ke->key() == Qt::Key_Up) &&
            m_popupList && m_popup && m_popup->isVisible() && m_popupList->count() > 0) {
            int row = m_popupList->currentRow();
            if (ke->key() == Qt::Key_Down)
                row = row + 1 >= m_popupList->count() ? 0 : row + 1;
            else
                row = row <= 0 ? m_popupList->count() - 1 : row - 1;
            m_popupList->setCurrentRow(row);
            m_bar->setText(m_popupList->item(row)->data(Qt::UserRole).toString().isEmpty()
                           ? m_popupList->item(row)->text()
                           : m_popupList->item(row)->data(Qt::UserRole).toString());
            return true;
        }
    }
    return QObject::eventFilter(obj, ev);
}

void AddressBarController::beginEditing() {
    if (m_editing) return;
    m_editing = true;
    if (!m_appFilterInstalled) {
        qApp->installEventFilter(this);
        m_appFilterInstalled = true;
    }
    m_savedUrl = m_currentUrl;
    // Swap to the full URL for editing — never the prettified form, since
    // editing a domain-only string would mangle paths and queries on commit.
    if (m_bar && !m_currentUrl.isEmpty() && m_currentUrl != QStringLiteral("about:blank")) {
        m_bar->setText(m_currentUrl);
    }
    QTimer::singleShot(0, this, [this] {
        if (m_bar->hasFocus()) m_bar->selectAll();
    });
    // Open the floating popout immediately on focus, even before typing —
    // the user expects to see the "box with URL on top" the moment the bar
    // becomes active.
    populatePopup({});
}

void AddressBarController::commit() {
    QString text;
    if (m_popupList && m_popup && m_popup->isVisible() && m_popupList->currentItem()
        && (m_popupList->currentItem()->flags() & Qt::ItemIsSelectable)) {
        text = m_popupList->currentItem()->data(Qt::UserRole).toString().isEmpty()
                   ? m_popupList->currentItem()->text()
                   : m_popupList->currentItem()->data(Qt::UserRole).toString();
    } else {
        text = m_bar->text();
    }
    hidePopup();
    if (text.trimmed().isEmpty()) return;
    endEditing(/*restoreUrl=*/false, QString());
    emit submitted(text);
}

void AddressBarController::fetchSuggestions() {
    if (m_pendingQuery.isEmpty()) return;
    if (m_inflight) {
        QNetworkReply *oldReply = m_inflight.data();
        m_inflight.clear();
        oldReply->abort();
        oldReply->deleteLater();
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
    reply->setProperty("fallbackTried", false);
    m_inflight = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        onSuggestionReplyFinished(reply);
    });
}

void AddressBarController::onSuggestionReplyFinished(QNetworkReply *reply) {
    reply->deleteLater();
    if (reply != m_inflight.data()) return;
    m_inflight.clear();
        const QString replyQuery = reply->property("query").toString();
        const bool fallbackTried = reply->property("fallbackTried").toBool();
        auto tryFallback = [this, replyQuery, fallbackTried] {
            if (fallbackTried || replyQuery.isEmpty() || replyQuery != m_pendingQuery) {
                if (replyQuery == m_pendingQuery) {
                    m_statusText = QStringLiteral("No suggestions available");
                    populatePopup({});
                }
                return;
            }
            QUrl fallback;
            fallback.setScheme("https");
            fallback.setHost("suggestqueries.google.com");
            fallback.setPath("/complete/search");
            QUrlQuery fq;
            fq.addQueryItem("client", "firefox");
            fq.addQueryItem("q", replyQuery);
            fallback.setQuery(fq);
            QNetworkRequest fallbackReq(fallback);
            fallbackReq.setHeader(QNetworkRequest::UserAgentHeader,
                                  "Mozilla/5.0 (Macintosh; Intel Mac OS X 14_0) AppleWebKit/605 (KHTML, like Gecko) pocb");
            fallbackReq.setRawHeader("Accept", "application/json,text/javascript,*/*;q=0.1");
            fallbackReq.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                                     QNetworkRequest::NoLessSafeRedirectPolicy);
            QNetworkReply *fallbackReply = m_net->get(fallbackReq);
            fallbackReply->setProperty("query", replyQuery);
            fallbackReply->setProperty("fallbackTried", true);
            m_inflight = fallbackReply;
            connect(fallbackReply, &QNetworkReply::finished, this, [this, fallbackReply] {
                onSuggestionReplyFinished(fallbackReply);
            });
        };
        if (reply->error() != QNetworkReply::NoError) {
            if (fallbackTried) m_statusText = QStringLiteral("Suggestions failed: %1").arg(reply->errorString());
            tryFallback();
            return;
        }
        if (!m_bar || replyQuery != m_pendingQuery) return;

        const QByteArray body = reply->readAll();
        QJsonParseError parseError;
        const auto doc = QJsonDocument::fromJson(body, &parseError);
        if (parseError.error != QJsonParseError::NoError) {
            if (fallbackTried) m_statusText = QStringLiteral("Suggestions returned non-JSON");
            tryFallback();
            return;
        }
        QStringList items;
        auto addSuggestion = [&items](const QString &s) {
            const QString trimmed = s.trimmed();
            if (!trimmed.isEmpty() && !items.contains(trimmed, Qt::CaseInsensitive)) items << trimmed;
        };
        auto addFromObject = [&addSuggestion](const QJsonObject &o) {
            addSuggestion(o.value("phrase").toString());
            addSuggestion(o.value("suggestion").toString());
            addSuggestion(o.value("value").toString());
            addSuggestion(o.value("text").toString());
            addSuggestion(o.value("query").toString());
            addSuggestion(o.value("term").toString());
        };
        if (doc.isArray() && doc.array().size() >= 2 && doc.array().at(1).isArray()) {
            for (const auto &v : doc.array().at(1).toArray()) {
                if (v.isString()) addSuggestion(v.toString());
                else if (v.isObject()) addFromObject(v.toObject());
            }
        } else if (doc.isArray()) {
            for (const auto &v : doc.array()) {
                if (v.isString()) addSuggestion(v.toString());
                else if (v.isObject()) addFromObject(v.toObject());
            }
        } else if (doc.isObject()) {
            const auto root = doc.object();
            for (const QString &key : {QStringLiteral("suggestions"), QStringLiteral("results"), QStringLiteral("items"), QStringLiteral("data")}) {
                const auto arr = root.value(key).toArray();
                for (const auto &v : arr) {
                    if (v.isString()) addSuggestion(v.toString());
                    else if (v.isObject()) addFromObject(v.toObject());
                }
            }
        }
        if (items.isEmpty()) {
            if (fallbackTried) m_statusText = QStringLiteral("No suggestions in response");
            tryFallback();
            return;
        }
        m_statusText.clear();
        if (items.size() > 8) items = items.mid(0, 8);
        populatePopup(items);
}

QIcon AddressBarController::engineIcon() const {
    if (!m_engineIcon.isNull()) return m_engineIcon;
    return mac::sfSymbolIcon("magnifyingglass", 13.0, m_iconColor);
}

void AddressBarController::fetchEngineIcon(const QString &host) {
    if (host.isEmpty() || m_engineIconLoading || m_engineIconHost == host) return;
    m_engineIconLoading = true;
    QUrl url;
    url.setScheme("https");
    url.setHost("www.google.com");
    url.setPath("/s2/favicons");
    QUrlQuery q;
    q.addQueryItem("domain", host);
    q.addQueryItem("sz", "32");
    url.setQuery(q);
    QNetworkReply *reply = m_net->get(QNetworkRequest(url));
    connect(reply, &QNetworkReply::finished, this, [this, reply, host] {
        reply->deleteLater();
        m_engineIconLoading = false;
        if (reply->error() != QNetworkReply::NoError) return;
        QPixmap pix;
        if (!pix.loadFromData(reply->readAll())) return;
        m_engineIcon = QIcon(pix);
        m_engineIconHost = host;
        if (!m_popupList) return;
        for (int i = 0; i < m_popupList->count(); ++i) {
            m_popupList->item(i)->setIcon(m_engineIcon);
        }
    });
}

void AddressBarController::populatePopup(const QStringList &items) {
    if (!m_popup) {
        // Container window: paints the rounded fill + border. The list
        // widget itself sits inside as a transparent child, so its rows
        // can be painted/styled independently of the panel chrome.
        QColor fill = m_theme.panel;
        fill.setAlphaF(0.0);
        QColor border = m_theme.border;
        border.setAlpha(105);
        m_popup = new AddrPopupFrame(m_bar ? m_bar->window() : nullptr, fill, border);
        m_popup->setAttribute(Qt::WA_TranslucentBackground);
        m_popup->setAttribute(Qt::WA_NoSystemBackground);
        m_popup->setAutoFillBackground(false);
        m_popup->setFocusPolicy(Qt::NoFocus);

        m_popupGlass = new QWidget(m_popup);
        m_popupGlass->setObjectName("AddrPopupGlass");
        m_popupGlass->setAttribute(Qt::WA_TranslucentBackground);
        m_popupGlass->setAttribute(Qt::WA_NoSystemBackground);
        m_popupGlass->setAutoFillBackground(false);
        m_popupGlass->lower();

        auto *vbox = new QVBoxLayout(m_popup);
        vbox->setContentsMargins(0, 0, 0, 0);
        vbox->setSpacing(0);

        m_popupList = new QListWidget(m_popup);
        vbox->addWidget(m_popupList);
        m_popupList->raise();

        m_popupList->setObjectName("AddrPopup");
        m_popupList->setFrameShape(QFrame::NoFrame);
        m_popupList->setFocusPolicy(Qt::NoFocus);
        m_popupList->setUniformItemSizes(false);
        m_popupList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_popupList->setAttribute(Qt::WA_TranslucentBackground);
        m_popupList->setAttribute(Qt::WA_NoSystemBackground);
        m_popupList->setAutoFillBackground(false);
        m_popupList->viewport()->setAttribute(Qt::WA_TranslucentBackground);
        m_popupList->viewport()->setAttribute(Qt::WA_NoSystemBackground);
        m_popupList->viewport()->setAutoFillBackground(false);
        QFont f = m_popupList->font();
        f.setFamily(m_theme.fontFamily);
        f.setPointSize(m_theme.regularSize);
        m_popupList->setFont(f);
        m_popupList->setStyleSheet(QString(
            "QListWidget#AddrPopup, QListWidget#AddrPopup::viewport { background: transparent; border: none; padding: 0px; color: %1; }"
            "QListWidget#AddrPopup::item {"
            "  padding: 8px 14px;"
            "  margin: 0px;"
            "  border-radius: 6px;"
            "  color: %1;"
            "}"
            "QListWidget#AddrPopup::item:selected { background: %2; }"
            "QListWidget#AddrPopup::item:hover:!selected { background: %3; }"
            "QListWidget#AddrPopup QScrollBar:vertical { background: transparent; width: 6px; margin: 4px 2px; }"
            "QListWidget#AddrPopup QScrollBar::handle:vertical { background: %4; border-radius: 3px; min-height: 24px; }"
            "QListWidget#AddrPopup QScrollBar::add-line, QListWidget#AddrPopup QScrollBar::sub-line { height:0; width:0; }")
            .arg(m_theme.foreground.name(),
                 colorWithAlpha(m_theme.raised, 0.70f),
                 colorWithAlpha(m_theme.hover, 0.54f),
                 colorWithAlpha(m_theme.border, 0.62f)));
        connect(m_popupList, &QListWidget::itemClicked, this, [this](QListWidgetItem *it) {
            if (!it) return;
            // Skip non-selectable header row.
            if (!(it->flags() & Qt::ItemIsSelectable)) return;
            m_bar->setText(it->data(Qt::UserRole).toString().isEmpty() ? it->text() : it->data(Qt::UserRole).toString());
            commit();
        });
    }
    m_popupList->clear();
    if (items.isEmpty()) {
        hidePopup();
        return;
    }
    const QString engineHost = QUrl(m_searchEngine).host().isEmpty()
                               ? QStringLiteral("search")
                               : QUrl(m_searchEngine).host();
    fetchEngineIcon(engineHost);
    for (const auto &s : items) {
        auto *it = new QListWidgetItem(s, m_popupList);
        it->setData(Qt::UserRole, s);
        it->setIcon(engineIcon());
        it->setSizeHint(QSize(0, 34));
    }
    m_popupList->setCurrentRow(-1);
    showPopup();
}

void AddressBarController::positionPopup() {
    if (!m_popup || !m_bar) return;
    QWidget *anchor = m_bar->parentWidget() ? m_bar->parentWidget() : m_bar;
    const bool inTopbar = anchor->window() && anchor->window()->findChild<QWidget *>("WebTopbar")
                          && anchor->window()->findChild<QWidget *>("WebTopbar")->isAncestorOf(anchor);

    const QPoint anchorBottom = anchor->mapToGlobal(QPoint(0, anchor->height()));
    const QPoint parentBottom = m_popup->isWindow()
                                ? anchorBottom
                                : (m_popup->parentWidget()
                                      ? m_popup->parentWidget()->mapFromGlobal(anchorBottom)
                                      : anchorBottom);
    const QRect available = m_popup->isWindow()
                            ? (anchor->screen() ? anchor->screen()->availableGeometry() : QRect())
                            : (m_popup->parentWidget()
                                  ? m_popup->parentWidget()->rect()
                                  : QRect());

    const int rows = qMin(m_popupList ? m_popupList->count() : 0, 9);
    const int height = qMax(1, rows) * 34;
    int width = inTopbar ? qBound(420, anchor->width(), 720)
                         : qMax(anchor->width(), 320);
    int x = parentBottom.x() + (anchor->width() - width) / 2;
    int y = parentBottom.y() + (inTopbar ? 8 : 6);

    if (available.isValid()) {
        width = qMin(width, available.width() - 24);
        x = qBound(available.left() + 12, x, available.right() - width - 12);
        const int maxHeight = qMax(96, available.bottom() - y - 12);
        m_popup->setGeometry(x, y, width, qMin(height, maxHeight));
    } else {
        m_popup->setGeometry(x, y, width, height);
    }
}

void AddressBarController::showPopup() {
    if (!m_popup) return;
    positionPopup();
    if (m_popupGlass) {
        m_popupGlass->setGeometry(m_popup->rect());
        m_popupGlass->lower();
    }
    if (!m_popup->isVisible()) m_popup->show();
    mac::applyLiquidGlassSiblingBehind(m_popup, 12.0);
    mac::roundWidgetCorners(m_popup, 12.0, false);
    if (m_popupList) {
        m_popupList->raise();
        m_popupList->viewport()->raise();
    }
    m_popup->raise();
    if (m_bar && !m_bar->hasFocus()) m_bar->setFocus(Qt::OtherFocusReason);
}

void AddressBarController::hidePopup() {
    if (m_popup) mac::hideLiquidGlassSibling(m_popup);
    if (m_popup && m_popup->isVisible()) m_popup->hide();
}
