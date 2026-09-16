#pragma once

#include "ApplePasswords.hpp"

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVariant>

class WebView;

// Login-form detection + Apple Passwords glue. Construct (via instance()) once
// before the first WebView exists so the content script is registered for
// every WKWebView configuration. The manager never shows UI itself; it emits
// the signals below and the browser window decides how to present them
// (PasswordPrompt is the intended presenter).
//
// Settings (QSettings):
//   passwords/applePasswordsEnabled   bool, default true
//   passwords/autofillRequiresTouchID bool, default true
//   passwords/neverSave               QStringList of hosts (managed here)
class PasswordManager final : public QObject {
    Q_OBJECT
public:
    static PasswordManager *instance();

    bool isEnabled() const;
    bool autofillRequiresTouchID() const;

    // Hosts the user chose "Never for this site" on.
    bool isNeverSave(const QUrl &site) const;
    void setNeverSave(const QUrl &site, bool never);

public slots:
    void setEnabled(bool enabled);
    void setAutofillRequiresTouchID(bool require);

    // Runs the Touch ID gate, loads the credential for `user` (or the only
    // saved one when `user` is empty) and fills the page.
    void fill(WebView *view, const QString &user);
    // Generates a strong password, fills every new-password field, and keeps
    // it so a later pw.submitted for the same origin is offered for saving.
    void generateAndFill(WebView *view);
    // Persists a credential offered via saveOffered().
    void save(const QUrl &site, const QString &user, const QString &password);
    // Re-evaluates the last form report for `view` (e.g. after a save).
    void refresh(WebView *view);

signals:
    // A page with a login form has saved accounts (usernames only; secrets
    // are loaded on fill()).
    void autofillAvailable(WebView *view, const QStringList &accounts);
    // A page shows a sign-up / change-password form.
    void generateAvailable(WebView *view, const QString &suggestedPassword);
    // A login was submitted whose credential is new or changed.
    void saveOffered(WebView *view, const QString &user, const QString &password);
    // Outcome of save() for toast/UI feedback.
    void saveFinished(const QUrl &site, const QString &user, bool ok, const QString &error);
    void enabledChanged(bool enabled);

private:
    explicit PasswordManager(QObject *parent = nullptr);
    void loadSettings() const;
    void attach(WebView *view);
    void handleMessage(WebView *view, const QString &name, const QVariant &body);
    void handleFormsFound(WebView *view, const QVariantMap &body);
    void handleSubmitted(WebView *view, const QVariantMap &body);
    void fillInto(WebView *view, const QString &user, const QString &password);
    static QString contentScript();
    static QString jsString(const QString &s);

    struct PageState {
        QUrl origin;
        bool hasNewPassword = false;
        bool hasCurrentPassword = false;
        QString pendingGenerated;   // password we generated for this origin
        QString lastOfferedUser;    // dedupe repeated saveOffered for same submit
        QString lastOfferedPassword;
        QString lastFilledUser;     // credential we autofilled; never re-offered
        QString lastFilledPassword;
    };

    mutable bool m_settingsLoaded = false;
    mutable bool m_enabled = true;
    mutable bool m_requireTouchID = true;
    mutable QSet<QString> m_neverSave;
    QSet<WebView *> m_attached;
    QHash<WebView *, PageState> m_pages;
};
