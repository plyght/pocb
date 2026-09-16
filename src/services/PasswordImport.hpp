#pragma once

#include "ApplePasswords.hpp"

#include <QList>
#include <QString>

class QWidget;

// Imports password CSV exports from other browsers into Apple Passwords.
//
// Recognised layouts (header row is matched case-insensitively, column order
// is free):
//   Chrome / Arc / Brave / Edge : name,url,username,password,note
//   Firefox                     : url,username,password,httpRealm,formActionOrigin,guid,timeCreated,...
//   Safari / Passwords app      : Title,URL,Username,Password,Notes,OTPAuth
namespace PasswordImport {

enum class Source { Unknown, Chromium, Firefox, Safari };

struct ParseResult {
    Source source = Source::Unknown;
    QList<mac::PasswordCredential> credentials;  // deduplicated
    int rows = 0;                                // data rows seen
    int skipped = 0;                             // rows without url/user/password
    int duplicates = 0;
    QString error;
};

// RFC 4180 style parser (quoted fields, doubled quotes, embedded newlines).
QList<QStringList> parseCsv(const QString &text);

ParseResult parse(const QString &csvText);
ParseResult parseFile(const QString &path);

// Same credential = same host + same user (case-insensitive); the last
// occurrence with a non-empty password wins.
QList<mac::PasswordCredential> deduplicate(const QList<mac::PasswordCredential> &in, int *duplicates = nullptr);

// Opens a file picker, parses, then hands the logins to
// ApplePasswords::exportToPasswordsApp. When the Credential Exchange API is
// not available it offers to write a Chrome-format CSV the Passwords app can
// import via File > Import Passwords… and opens the Passwords app.
void importFromCsvInteractive(QWidget *parent);

}  // namespace PasswordImport
