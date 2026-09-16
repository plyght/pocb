#pragma once

#include <QList>
#include <QString>
#include <QStringList>
#include <QUrl>

#include <functional>

class QWidget;

namespace mac {

// One saved login. `site` is the host the credential was stored for.
struct PasswordCredential {
    QString site;
    QString user;
    QString password;
};

// Bridge to Apple Passwords / iCloud Keychain. Every private call is resolved
// at runtime (NSClassFromString / respondsToSelector / dlsym) and every route
// has a public fallback, so a moved selector on a future macOS degrades to the
// next backend instead of crashing. See docs/apple-passwords.md for which
// backends were verified on macOS 26.
//
// Backend order:
//   fetch    : AuthenticationServicesCore (ASCAgentProxy, entitled builds) ->
//              SafariShared WBSSavedAccountStore (Apple-only keychain group) ->
//              pocb's own keychain items (data-protection keychain, then the
//              legacy login keychain).
//   save     : WBSSavedAccountStore -> SecAddSharedWebCredential -> own items.
//   generate : WBSPasswordGenerationManager -> SecCreateSharedWebCredentialPassword
//              -> local xxxxxx-xxxxxx-xxxxxx.
//   export   : Credential Exchange (macOS 26, ASCredentialExportManager) when
//              callable -> Chrome-format CSV for Passwords > File > Import.
class ApplePasswords {
public:
    using FetchCallback = std::function<void(const QList<PasswordCredential> &credentials, const QString &error)>;
    using BoolCallback = std::function<void(bool ok, const QString &error)>;

    static ApplePasswords &instance();

    // True when at least one storage backend can be used in this process.
    bool isAvailable() const;
    // Human readable name of the backend that will answer saveCredential().
    QString backendDescription() const;

    // Gate fetches behind LAContext biometrics (default on). When the Mac has
    // no Touch ID the gate falls back to the device-owner password policy.
    void setRequireBiometrics(bool require);
    bool requireBiometrics() const;

    // Non-secret lookup used to decide whether an autofill offer exists
    // without triggering Touch ID. Returns usernames saved for `site`.
    QStringList savedUsernames(const QUrl &site) const;

    // Loads credentials (with secrets) for `site`. Runs the Touch ID gate
    // first; `done` is invoked on the Qt main thread exactly once.
    void fetchCredentials(const QUrl &site, FetchCallback done);

    // Stores or updates `user`/`password` for `site`. `done` runs on the main
    // thread exactly once.
    void saveCredential(const QUrl &site, const QString &user, const QString &password, BoolCallback done);

    // Removes a credential pocb stored itself (never touches Safari's items).
    bool removeCredential(const QUrl &site, const QString &user);

    // Strong password in Apple's "xxxxxx-xxxxxx-xxxxxx" shape. `site` lets the
    // Safari generator honour per-domain password rules when it is present.
    QString generatePassword(const QUrl &site = QUrl());

    // Hands credentials to the Passwords app. `parent` anchors any UI the
    // fallback path needs (a save dialog for the CSV route). `done` reports
    // success and a user-facing message.
    void exportToPasswordsApp(const QList<PasswordCredential> &credentials, QWidget *parent, BoolCallback done);

    // True when the macOS 26 Credential Exchange API can actually be driven
    // from this process (currently requires a Swift shim + credential provider
    // extension, so this is false in the shipped configuration).
    static bool credentialExchangeAvailable();

    // Launches the system Passwords app (macOS 15+).
    static bool openPasswordsApp();

    // Writes Chrome/Arc/Brave/Edge-compatible CSV that Passwords can import.
    static bool writeChromeCsv(const QString &path, const QList<PasswordCredential> &credentials, QString *error = nullptr);

    // Canonical host used as the storage key for `url`.
    static QString siteKey(const QUrl &url);

private:
    ApplePasswords();
    bool m_requireBiometrics = true;
};

}  // namespace mac
