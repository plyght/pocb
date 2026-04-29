#include "FaviconService.hpp"

#include <QColor>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

namespace {
constexpr int kDirectAttemptCount = 4;
constexpr int kSizes[] = {64, 32, 16};
constexpr int kSizeCount = sizeof(kSizes) / sizeof(kSizes[0]);

bool nearWhite(const QColor &c) {
    return c.alpha() > 220 && c.red() > 232 && c.green() > 232 && c.blue() > 232;
}

QString registrableDomain(QString domain) {
    if (domain.startsWith("www.")) domain.remove(0, 4);
    const QStringList parts = domain.split('.', Qt::SkipEmptyParts);
    if (parts.size() < 2) return domain;
    return parts.mid(parts.size() - 2).join('.');
}

QPixmap removeWhiteMatte(const QPixmap &pm) {
    QImage img = pm.toImage().convertToFormat(QImage::Format_ARGB32);
    if (img.isNull()) return pm;

    QVector<QPoint> stack;
    auto push = [&](int x, int y) {
        if (x < 0 || y < 0 || x >= img.width() || y >= img.height()) return;
        QColor c = img.pixelColor(x, y);
        if (!nearWhite(c)) return;
        c.setAlpha(0);
        img.setPixelColor(x, y, c);
        stack.append(QPoint(x, y));
    };

    for (int x = 0; x < img.width(); ++x) {
        push(x, 0);
        push(x, img.height() - 1);
    }
    for (int y = 0; y < img.height(); ++y) {
        push(0, y);
        push(img.width() - 1, y);
    }

    while (!stack.isEmpty()) {
        const QPoint p = stack.takeLast();
        push(p.x() + 1, p.y());
        push(p.x() - 1, p.y());
        push(p.x(), p.y() + 1);
        push(p.x(), p.y() - 1);
    }

    for (int y = 0; y < img.height(); ++y) {
        for (int x = 0; x < img.width(); ++x) {
            QColor c = img.pixelColor(x, y);
            if (!nearWhite(c)) continue;
            int transparentNeighbors = 0;
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    if (dx == 0 && dy == 0) continue;
                    const int nx = x + dx;
                    const int ny = y + dy;
                    if (nx < 0 || ny < 0 || nx >= img.width() || ny >= img.height()) continue;
                    if (img.pixelColor(nx, ny).alpha() < 16) ++transparentNeighbors;
                }
            }
            if (transparentNeighbors > 0) {
                c.setAlpha(0);
                img.setPixelColor(x, y, c);
            }
        }
    }

    return QPixmap::fromImage(img);
}
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
    const QPixmap cleaned = removeWhiteMatte(pm);
    m_memCache.insert(domain, cleaned);
    cleaned.save(diskPath(domain), "PNG");
    emit faviconReady(domain, cleaned);
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
    if (idx >= kDirectAttemptCount + kSizeCount) {
        m_attempt.remove(domain);
        m_inflight.remove(domain);
        return;
    }

    QUrl url;
    const QString rootDomain = registrableDomain(domain);
    if (idx == 0) {
        url = QUrl(QString("https://%1/favicon.ico").arg(domain));
    } else if (idx == 1) {
        url = QUrl(QString("https://www.%1/favicon.ico").arg(domain));
    } else if (idx == 2) {
        url = QUrl(QString("https://%1/favicon.ico").arg(rootDomain));
    } else if (idx == 3) {
        url = QUrl(QString("https://www.%1/favicon.ico").arg(rootDomain));
    } else {
        const int size = kSizes[idx - kDirectAttemptCount];
        url = QUrl(QString("https://www.google.com/s2/favicons?domain=%1&sz=%2").arg(rootDomain).arg(size));
    }
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
