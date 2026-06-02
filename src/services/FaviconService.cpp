#include "FaviconService.hpp"

#include <QFile>
#include <QFileInfo>
#include <QBuffer>
#include <QImage>
#include <QImageReader>
#include <QNetworkAccessManager>
#include <QRegularExpression>
#include <QNetworkReply>
#include <QNetworkRequest>

namespace {
constexpr int kDirectAttemptCount = 4;
constexpr int kHtmlAttemptCount = 2;
constexpr int kSizes[] = {128, 64, 32};
constexpr int kSizeCount = sizeof(kSizes) / sizeof(kSizes[0]);

QString registrableDomain(QString domain) {
    if (domain.startsWith("www.")) domain.remove(0, 4);
    const QStringList parts = domain.split('.', Qt::SkipEmptyParts);
    if (parts.size() < 2) return domain;
    return parts.mid(parts.size() - 2).join('.');
}

QPixmap loadBestPixmap(const QByteArray &data) {
    QBuffer buffer;
    buffer.setData(data);
    buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer);
    reader.setAutoTransform(true);

    QImage best;
    const int count = reader.imageCount();
    if (count > 1) {
        for (int i = 0; i < count; ++i) {
            if (!reader.jumpToImage(i)) continue;
            const QImage image = reader.read();
            if (image.isNull()) continue;
            if (best.isNull() || image.width() * image.height() > best.width() * best.height()) best = image;
        }
    } else {
        best = reader.read();
    }

    if (best.isNull()) {
        QPixmap fallback;
        fallback.loadFromData(data);
        return fallback;
    }

    return QPixmap::fromImage(best.convertToFormat(QImage::Format_ARGB32_Premultiplied));
}

QList<QUrl> iconCandidatesFromHtml(const QByteArray &data, const QUrl &base) {
    QList<QUrl> urls;
    const QString html = QString::fromUtf8(data.left(262144));
    QRegularExpression linkRe(QStringLiteral("<link\\b[^>]*>"), QRegularExpression::CaseInsensitiveOption);
    QRegularExpression relRe(QStringLiteral("\\brel\\s*=\\s*([\'\"])([^\'\"]*)\\1"), QRegularExpression::CaseInsensitiveOption);
    QRegularExpression hrefRe(QStringLiteral("\\bhref\\s*=\\s*([\'\"])([^\'\"]+)\\1"), QRegularExpression::CaseInsensitiveOption);
    auto it = linkRe.globalMatch(html);
    while (it.hasNext()) {
        const QString tag = it.next().captured(0);
        const QString rel = relRe.match(tag).captured(2).toLower();
        if (!rel.contains(QStringLiteral("icon")) || rel.contains(QStringLiteral("apple-touch")) || rel.contains(QStringLiteral("mask-icon"))) continue;
        const QString href = hrefRe.match(tag).captured(2).trimmed();
        if (href.isEmpty()) continue;
        const QUrl url = base.resolved(QUrl(href));
        if (url.isValid() && !urls.contains(url)) urls.append(url);
    }
    return urls;
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
    return m_dir.filePath(domain + ".v4.png");
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
    if (idx >= kHtmlAttemptCount + m_candidates.value(domain).size() + kDirectAttemptCount + kSizeCount) {
        m_attempt.remove(domain);
        m_candidates.remove(domain);
        m_inflight.remove(domain);
        return;
    }

    QUrl url;
    const QString rootDomain = registrableDomain(domain);
    const int candidateCount = m_candidates.value(domain).size();
    if (idx == 0) {
        url = QUrl(QString("https://%1/").arg(domain));
    } else if (idx == 1) {
        url = QUrl(QString("https://www.%1/").arg(domain));
    } else if (idx < kHtmlAttemptCount + candidateCount) {
        url = m_candidates.value(domain).at(idx - kHtmlAttemptCount);
    } else {
        const int directIdx = idx - kHtmlAttemptCount - candidateCount;
        if (directIdx == 0) {
            url = QUrl(QString("https://%1/favicon.ico").arg(domain));
        } else if (directIdx == 1) {
            url = QUrl(QString("https://www.%1/favicon.ico").arg(domain));
        } else if (directIdx == 2) {
            url = QUrl(QString("https://%1/favicon.ico").arg(rootDomain));
        } else if (directIdx == 3) {
            url = QUrl(QString("https://www.%1/favicon.ico").arg(rootDomain));
        } else {
            const int size = kSizes[directIdx - kDirectAttemptCount];
            url = QUrl(QString("https://www.google.com/s2/favicons?domain=%1&sz=%2").arg(rootDomain).arg(size));
        }
    }
    QNetworkRequest req(url);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    auto *reply = m_nam->get(req);
    m_inflight.insert(domain, reply);

    connect(reply, &QNetworkReply::finished, this, [this, domain, reply, url]() {
        const QByteArray data = reply->readAll();
        const int idx = m_attempt.value(domain, 0);
        if (reply->error() == QNetworkReply::NoError && idx < kHtmlAttemptCount) {
            QList<QUrl> candidates = m_candidates.value(domain);
            for (const QUrl &candidate : iconCandidatesFromHtml(data, url)) {
                if (!candidates.contains(candidate)) candidates.append(candidate);
            }
            m_candidates.insert(domain, candidates);
        }
        const QPixmap pm = idx < kHtmlAttemptCount ? QPixmap() : loadBestPixmap(data);
        const bool ok = reply->error() == QNetworkReply::NoError && !pm.isNull();
        reply->deleteLater();

        if (ok) {
            m_attempt.remove(domain);
            m_candidates.remove(domain);
            m_inflight.remove(domain);
            store(domain, pm);
            return;
        }

        m_attempt[domain] = m_attempt.value(domain, 0) + 1;
        m_inflight.remove(domain);
        tryNextSize(domain);
    });
}
