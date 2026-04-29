#include "BookmarkStore.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>

#include <algorithm>

BookmarkStore::BookmarkStore(QObject *parent) : QObject(parent) {
}

QVector<Bookmark> BookmarkStore::bookmarks(const QString &profileName) const {
    return read(profileName);
}

bool BookmarkStore::contains(const QString &profileName, const QUrl &url) const {
    const QString target = url.adjusted(QUrl::NormalizePathSegments | QUrl::StripTrailingSlash).toString();
    for (const Bookmark &bookmark : read(profileName)) {
        if (bookmark.url.adjusted(QUrl::NormalizePathSegments | QUrl::StripTrailingSlash).toString() == target) return true;
    }
    return false;
}

void BookmarkStore::addBookmark(const QString &profileName, const QString &title, const QUrl &url) {
    if (!url.isValid() || url.isEmpty() || url.scheme() == "about" || url.scheme() == "data") return;
    QVector<Bookmark> items = read(profileName);
    const QString target = url.adjusted(QUrl::NormalizePathSegments | QUrl::StripTrailingSlash).toString();
    for (Bookmark &bookmark : items) {
        if (bookmark.url.adjusted(QUrl::NormalizePathSegments | QUrl::StripTrailingSlash).toString() == target) {
            bookmark.title = title.trimmed().isEmpty() ? url.toString() : title.trimmed();
            bookmark.url = url;
            writeBookmarks(profileName, items);
            emit bookmarksChanged(profileName);
            return;
        }
    }
    items.append({title.trimmed().isEmpty() ? url.toString() : title.trimmed(), url});
    writeBookmarks(profileName, items);
    emit bookmarksChanged(profileName);
}

void BookmarkStore::removeBookmark(const QString &profileName, const QUrl &url) {
    QVector<Bookmark> items = read(profileName);
    const QString target = url.adjusted(QUrl::NormalizePathSegments | QUrl::StripTrailingSlash).toString();
    const qsizetype before = items.size();
    items.erase(std::remove_if(items.begin(), items.end(), [&](const Bookmark &bookmark) {
        return bookmark.url.adjusted(QUrl::NormalizePathSegments | QUrl::StripTrailingSlash).toString() == target;
    }), items.end());
    if (items.size() == before) return;
    writeBookmarks(profileName, items);
    emit bookmarksChanged(profileName);
}

QString BookmarkStore::sanitize(const QString &name) const {
    QString out = name.trimmed();
    out.replace('/', '-');
    out.replace(':', '-');
    return out.isEmpty() ? QStringLiteral("Default") : out;
}

QString BookmarkStore::filePath(const QString &profileName) const {
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    const QDir dir(base + "/Profiles/" + sanitize(profileName));
    return dir.filePath("bookmarks.json");
}

QVector<Bookmark> BookmarkStore::read(const QString &profileName) const {
    QFile file(filePath(profileName));
    if (!file.open(QIODevice::ReadOnly)) return {};
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isArray()) return {};
    QVector<Bookmark> items;
    for (const QJsonValue &value : doc.array()) {
        const QJsonObject object = value.toObject();
        const QUrl url(object.value("url").toString());
        if (!url.isValid() || url.isEmpty()) continue;
        const QString title = object.value("title").toString(url.toString()).trimmed();
        items.append({title.isEmpty() ? url.toString() : title, url});
    }
    return items;
}

void BookmarkStore::writeBookmarks(const QString &profileName, const QVector<Bookmark> &bookmarks) const {
    const QString path = filePath(profileName);
    QDir().mkpath(QFileInfo(path).absolutePath());
    QJsonArray array;
    for (const Bookmark &bookmark : bookmarks) {
        QJsonObject object;
        object.insert("title", bookmark.title);
        object.insert("url", bookmark.url.toString());
        array.append(object);
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return;
    file.write(QJsonDocument(array).toJson(QJsonDocument::Indented));
    file.commit();
}
