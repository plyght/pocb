#pragma once

#include "Theme.hpp"

#include <QPointer>
#include <QWidget>

class QLineEdit;
class QListWidget;
class QNetworkAccessManager;
class QNetworkReply;
class QTimer;

// Vicinae-launcher-inspired floating omnibox: a frameless, translucent,
// rounded panel that overlays the browser content. Includes live search
// suggestions (Google's public suggest endpoint, works for any engine).
class FloatingOmnibox final : public QWidget {
    Q_OBJECT
public:
    explicit FloatingOmnibox(const Theme &theme, QWidget *parent = nullptr);

    void showFor(QWidget *anchor, const QString &initialText = QString());

    // Hint where to fetch suggestions from. Pass the same template URL
    // used for searches (e.g. "https://search.brave.com/search?q=%1") and
    // the omnibox will derive the engine's public suggest endpoint.
    void setSearchEngineUrl(const QString &templateUrl);

signals:
    void submitted(const QString &text);

protected:
    void paintEvent(QPaintEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;
    void showEvent(QShowEvent *e) override;
    bool eventFilter(QObject *obj, QEvent *ev) override;

private:
    void onTextEdited(const QString &text);
    void scheduleSuggestionRequest();
    void fetchSuggestions();
    void onSuggestionsReceived(QNetworkReply *reply);
    void setSuggestions(const QStringList &items);
    void acceptCurrent();
    void relayout();

    Theme m_theme;
    QLineEdit *m_input = nullptr;
    QListWidget *m_list = nullptr;
    QWidget *m_divider = nullptr;
    QNetworkAccessManager *m_net = nullptr;
    QPointer<QNetworkReply> m_inflight;
    QTimer *m_debounce = nullptr;
    QString m_pendingQuery;
    QString m_engineHost;  // e.g. "duckduckgo.com"
};
