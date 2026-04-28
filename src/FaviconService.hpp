#pragma once

#include <QDir>
#include <QHash>
#include <QObject>
#include <QPixmap>
#include <QPointer>
#include <QString>
#include <QUrl>

class QNetworkAccessManager;
class QNetworkReply;

// Slim port of vicinae's FaviconService: in-memory + on-disk cache,
// async fetch via Google s2 with size fallback. No SQLite.
class FaviconService final : public QObject {
    Q_OBJECT
public:
    enum Provider { Google, None };

    explicit FaviconService(const QDir &cacheDir, QObject *parent = nullptr);

    void setProvider(Provider p) { m_provider = p; }
    Provider provider() const { return m_provider; }

    // Returns cached pixmap immediately if available (possibly null).
    QPixmap cached(const QUrl &url) const;

    // Kicks off a fetch if not cached. Emits faviconReady when done.
    void request(const QUrl &url);

signals:
    void faviconReady(const QString &domain, const QPixmap &pixmap);

private:
    static QString domainOf(const QUrl &url);
    void tryNextSize(const QString &domain);
    void store(const QString &domain, const QPixmap &pm);
    QString diskPath(const QString &domain) const;

    QNetworkAccessManager *m_nam;
    QDir m_dir;
    Provider m_provider = Google;
    QHash<QString, QPixmap> m_memCache;
    QHash<QString, int> m_attempt;             // domain -> size index
    QHash<QString, QPointer<QNetworkReply>> m_inflight;
};
