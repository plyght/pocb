#pragma once

#include "Theme.hpp"

#include <QObject>
#include <QString>

class QLabel;
class QLineEdit;
class QListWidget;
class QNetworkAccessManager;
class QNetworkReply;
class QTimer;
class QWidget;

class AddressBarController final : public QObject {
    Q_OBJECT
public:
    AddressBarController(QLineEdit *bar, QLabel *lockIcon, const Theme &theme, QObject *parent);

    void setSearchEngineUrl(const QString &url) { m_searchEngine = url; }
    bool isEditing() const { return m_editing; }

    // Update address bar text when not editing (called on tab url changes).
    void setDisplayUrl(const QString &urlString, bool isHttps);

    // Cancel editing and hide popup; if restoreUrl is true, the bar's text is
    // overwritten by the supplied current url string.
    void endEditing(bool restoreUrl, const QString &currentUrl);

protected:
    bool eventFilter(QObject *obj, QEvent *ev) override;

signals:
    // Emitted when the user commits a query (Enter or popup click). The
    // BrowserWindow turns this into a navigation via its urlFromInput.
    void submitted(const QString &text);
    // Emitted on Escape so the parent can refocus the web view.
    void escapePressed();

private:
    void beginEditing();
    void commit();
    void fetchSuggestions();
    void populatePopup(const QStringList &items);
    void positionPopup();
    void showPopup();
    void hidePopup();

    QLineEdit *m_bar = nullptr;
    QLabel *m_lockIcon = nullptr;
    Theme m_theme;
    QString m_searchEngine;

    QNetworkAccessManager *m_net = nullptr;
    QNetworkReply *m_inflight = nullptr;
    QTimer *m_debounce = nullptr;
    QListWidget *m_popup = nullptr;
    QString m_pendingQuery;
    QString m_savedUrl;
    bool m_editing = false;
};
