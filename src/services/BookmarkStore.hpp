#pragma once

#include <QObject>
#include <QUrl>
#include <QVector>

struct Bookmark {
    QString title;
    QUrl url;
};

class BookmarkStore final : public QObject {
    Q_OBJECT
public:
    explicit BookmarkStore(QObject *parent = nullptr);

    QVector<Bookmark> bookmarks(const QString &profileName) const;
    bool contains(const QString &profileName, const QUrl &url) const;

public slots:
    void addBookmark(const QString &profileName, const QString &title, const QUrl &url);
    void removeBookmark(const QString &profileName, const QUrl &url);

signals:
    void bookmarksChanged(const QString &profileName);

private:
    QString sanitize(const QString &name) const;
    QString filePath(const QString &profileName) const;
    QVector<Bookmark> read(const QString &profileName) const;
    void writeBookmarks(const QString &profileName, const QVector<Bookmark> &bookmarks) const;
};
