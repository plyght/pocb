#include "PasswordImport.hpp"

#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHash>
#include <QMessageBox>
#include <QPushButton>
#include <QStandardPaths>
#include <QTextStream>
#include <QUrl>

namespace PasswordImport {
namespace {

QString normaliseHeader(const QString &h) {
    QString s = h.trimmed().toLower();
    if (!s.isEmpty() && s.at(0) == QChar(0xFEFF)) s.remove(0, 1);
    s.remove(QLatin1Char(' '));
    s.remove(QLatin1Char('_'));
    return s;
}

int indexOfAny(const QStringList &headers, const QStringList &names) {
    for (const QString &n : names) {
        const int i = headers.indexOf(n);
        if (i >= 0) return i;
    }
    return -1;
}

QString hostFor(const QString &urlText) {
    QString t = urlText.trimmed();
    if (t.isEmpty()) return {};
    QUrl u = t.contains(QStringLiteral("://")) ? QUrl(t) : QUrl(QStringLiteral("https://") + t);
    QString host = u.host().toLower();
    if (host.startsWith(QStringLiteral("www."))) host.remove(0, 4);
    if (host.isEmpty()) {
        // Chrome sometimes exports android:// or bare labels; keep the text
        // so the user sees what was skipped, but only if it looks like a host.
        if (t.contains(QLatin1Char('.')) && !t.contains(QLatin1Char('/'))) host = t.toLower();
    }
    return host;
}

}  // namespace

QList<QStringList> parseCsv(const QString &text) {
    QList<QStringList> rows;
    QStringList row;
    QString field;
    bool quoted = false;
    const int n = text.size();
    for (int i = 0; i < n; ++i) {
        const QChar c = text.at(i);
        if (quoted) {
            if (c == QLatin1Char('"')) {
                if (i + 1 < n && text.at(i + 1) == QLatin1Char('"')) { field += QLatin1Char('"'); ++i; }
                else quoted = false;
            } else {
                field += c;
            }
            continue;
        }
        if (c == QLatin1Char('"')) { quoted = true; continue; }
        if (c == QLatin1Char(',')) { row << field; field.clear(); continue; }
        if (c == QLatin1Char('\r')) continue;
        if (c == QLatin1Char('\n')) {
            row << field;
            field.clear();
            if (!(row.size() == 1 && row.first().isEmpty())) rows << row;
            row.clear();
            continue;
        }
        field += c;
    }
    if (!field.isEmpty() || !row.isEmpty()) {
        row << field;
        if (!(row.size() == 1 && row.first().isEmpty())) rows << row;
    }
    return rows;
}

QList<mac::PasswordCredential> deduplicate(const QList<mac::PasswordCredential> &in, int *duplicates) {
    QHash<QString, int> index;
    QList<mac::PasswordCredential> out;
    int dups = 0;
    for (const mac::PasswordCredential &c : in) {
        const QString key = c.site.toLower() + QLatin1Char('\n') + c.user.toLower();
        auto it = index.find(key);
        if (it == index.end()) {
            index.insert(key, out.size());
            out.push_back(c);
        } else {
            ++dups;
            if (!c.password.isEmpty()) out[it.value()].password = c.password;
        }
    }
    if (duplicates) *duplicates = dups;
    return out;
}

ParseResult parse(const QString &csvText) {
    ParseResult r;
    const QList<QStringList> rows = parseCsv(csvText);
    if (rows.isEmpty()) { r.error = QStringLiteral("The file is empty."); return r; }

    QStringList headers;
    for (const QString &h : rows.first()) headers << normaliseHeader(h);
    const int urlCol = indexOfAny(headers, {QStringLiteral("url"), QStringLiteral("loginuri"), QStringLiteral("website"), QStringLiteral("site")});
    const int userCol = indexOfAny(headers, {QStringLiteral("username"), QStringLiteral("user"), QStringLiteral("login"), QStringLiteral("email")});
    const int passCol = indexOfAny(headers, {QStringLiteral("password"), QStringLiteral("pass")});
    const int nameCol = indexOfAny(headers, {QStringLiteral("name"), QStringLiteral("title")});
    if (urlCol < 0 || userCol < 0 || passCol < 0) {
        r.error = QStringLiteral("Could not find url, username and password columns. Expected a Chrome, Arc, Brave, Edge, Firefox or Safari export.");
        return r;
    }
    if (headers.contains(QStringLiteral("httprealm")) || headers.contains(QStringLiteral("formactionorigin")) || headers.contains(QStringLiteral("guid"))) r.source = Source::Firefox;
    else if (headers.contains(QStringLiteral("otpauth")) || headers.contains(QStringLiteral("notes"))) r.source = Source::Safari;
    else if (headers.contains(QStringLiteral("note")) || nameCol >= 0) r.source = Source::Chromium;

    QList<mac::PasswordCredential> all;
    for (int i = 1; i < rows.size(); ++i) {
        const QStringList &row = rows.at(i);
        ++r.rows;
        auto at = [&row](int col) { return col >= 0 && col < row.size() ? row.at(col) : QString(); };
        QString host = hostFor(at(urlCol));
        if (host.isEmpty() && nameCol >= 0) host = hostFor(at(nameCol));
        const QString user = at(userCol).trimmed();
        const QString pass = at(passCol);
        if (host.isEmpty() || user.isEmpty() || pass.isEmpty()) { ++r.skipped; continue; }
        all.push_back({host, user, pass});
    }
    r.credentials = deduplicate(all, &r.duplicates);
    return r;
}

ParseResult parseFile(const QString &path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        ParseResult r;
        r.error = f.errorString();
        return r;
    }
    const QByteArray bytes = f.readAll();
    return parse(QString::fromUtf8(bytes));
}

void importFromCsvInteractive(QWidget *parent) {
    const QString start = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    const QString path = QFileDialog::getOpenFileName(parent, QStringLiteral("Import Passwords"), start,
                                                      QStringLiteral("Password CSV exports (*.csv);;All files (*)"));
    if (path.isEmpty()) return;

    ParseResult result = parseFile(path);
    if (!result.error.isEmpty()) {
        QMessageBox::warning(parent, QStringLiteral("Import Passwords"), result.error);
        return;
    }
    if (result.credentials.isEmpty()) {
        QMessageBox::information(parent, QStringLiteral("Import Passwords"),
                                 QStringLiteral("No usable logins found (%1 rows, %2 skipped).").arg(result.rows).arg(result.skipped));
        return;
    }

    const QString sourceName = result.source == Source::Firefox ? QStringLiteral("Firefox")
                             : result.source == Source::Safari ? QStringLiteral("Safari")
                             : result.source == Source::Chromium ? QStringLiteral("Chrome / Arc / Brave / Edge")
                             : QStringLiteral("CSV");
    const QString summary = QStringLiteral("%1 logins from %2 (%3 duplicates merged, %4 rows skipped).")
                                .arg(result.credentials.size()).arg(sourceName).arg(result.duplicates).arg(result.skipped);

    auto &ap = mac::ApplePasswords::instance();
    const QList<mac::PasswordCredential> creds = result.credentials;
    ap.exportToPasswordsApp(creds, parent, [parent, creds, summary](bool stored, const QString &message) {
        if (mac::ApplePasswords::credentialExchangeAvailable()) {
            QMessageBox::information(parent, QStringLiteral("Import Passwords"), summary + QLatin1Char('\n') + message);
            return;
        }
        // Passwords app has no public import API for non-extension apps, so
        // offer the CSV route it does support (File > Import Passwords…).
        QMessageBox box(parent);
        box.setWindowTitle(QStringLiteral("Import Passwords"));
        box.setIcon(QMessageBox::Question);
        box.setText(summary);
        box.setInformativeText(QStringLiteral("%1\n\nTo also add them to the Passwords app, export a Chrome-format CSV and use "
                                              "Passwords > File > Import Passwords…. Delete the CSV afterwards.")
                                   .arg(stored ? message : QStringLiteral("They could not be stored in the keychain: %1").arg(message)));
        QPushButton *exportBtn = box.addButton(QStringLiteral("Export CSV for Passwords…"), QMessageBox::AcceptRole);
        QPushButton *openBtn = box.addButton(QStringLiteral("Open Passwords"), QMessageBox::ActionRole);
        box.addButton(QMessageBox::Close);
        box.exec();
        if (box.clickedButton() == openBtn) {
            mac::ApplePasswords::openPasswordsApp();
            return;
        }
        if (box.clickedButton() != exportBtn) return;
        const QString dest = QFileDialog::getSaveFileName(parent, QStringLiteral("Export for Passwords app"),
                                                          QStandardPaths::writableLocation(QStandardPaths::DesktopLocation) + QStringLiteral("/pocb-passwords-import.csv"),
                                                          QStringLiteral("CSV (*.csv)"));
        if (dest.isEmpty()) return;
        QString err;
        if (!mac::ApplePasswords::writeChromeCsv(dest, creds, &err)) {
            QMessageBox::warning(parent, QStringLiteral("Import Passwords"), QStringLiteral("Could not write %1: %2").arg(dest, err));
            return;
        }
        mac::ApplePasswords::openPasswordsApp();
        QMessageBox::information(parent, QStringLiteral("Import Passwords"),
                                 QStringLiteral("Wrote %1.\nIn Passwords choose File > Import Passwords… and pick that file, then delete it.").arg(QFileInfo(dest).fileName()));
    });
}

}  // namespace PasswordImport
