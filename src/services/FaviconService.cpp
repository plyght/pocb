#include "FaviconService.hpp"

#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

namespace {
constexpr int kSizes[] = {64, 32, 16};
constexpr int kSizeCount = sizeof(kSizes) / sizeof(kSizes[0]);
}

FaviconService::FaviconService(const QDir &cacheDir, QObject *parent)
    : QObject(parent), m_nam(new QNetworkAccessManager(this)), m_dir(cacheDir) {
    m_dir.mkpath(".");
}

QString FaviconService::domainOf(const QUrl &url) {
    QString host = url.host();
    if (host.startsWith("www.")) host.remove(0, 4);
    return host;
}

QString FaviconService::diskPath(const QString &domain) const {
    return m_dir.filePath(domain + ".png");
}

QPixmap FaviconService::cached(const QUrl &url) const {
    const QString domain = domainOf(url);
    if (domain.isEmpty()) return {};
    if (auto it = m_memCache.constFind(domain); it != m_memCache.constEnd()) return it.value();

    QPixmap pm;
    if (QFileInfo::exists(diskPath(domain)) && pm.load(diskPath(domain), "PNG")) {
        const_cast<FaviconService *>(this)->m_memCache.insert(domain, pm);
        return pm;
    }
    return {};
}

void FaviconService::store(const QString &domain, const QPixmap &pm) {
    m_memCache.insert(domain, pm);
    pm.save(diskPath(domain), "PNG");
    emit faviconReady(domain, pm);
}

void FaviconService::request(const QUrl &url) {
    const QString domain = domainOf(url);
    if (domain.isEmpty() || m_provider == None) return;

    if (auto pm = cached(url); !pm.isNull()) {
        emit faviconReady(domain, pm);
        return;
    }

    if (m_inflight.contains(domain)) return;

    m_attempt.insert(domain, 0);
    tryNextSize(domain);
}

void FaviconService::tryNextSize(const QString &domain) {
    const int idx = m_attempt.value(domain, 0);
    if (idx >= kSizeCount) {
        m_attempt.remove(domain);
        m_inflight.remove(domain);
        return;
    }

    const int size = kSizes[idx];
    const QUrl url(QString("https://www.google.com/s2/favicons?domain=%1&sz=%2").arg(domain).arg(size));
    QNetworkRequest req(url);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    auto *reply = m_nam->get(req);
    m_inflight.insert(domain, reply);

    connect(reply, &QNetworkReply::finished, this, [this, domain, reply]() {
        QPixmap pm;
        const QByteArray data = reply->readAll();
        const bool ok = reply->error() == QNetworkReply::NoError && pm.loadFromData(data) && !pm.isNull();
        reply->deleteLater();

        if (ok) {
            m_attempt.remove(domain);
            m_inflight.remove(domain);
            store(domain, pm);
            return;
        }

        m_attempt[domain] = m_attempt.value(domain, 0) + 1;
        m_inflight.remove(domain);
        tryNextSize(domain);
    });
}
