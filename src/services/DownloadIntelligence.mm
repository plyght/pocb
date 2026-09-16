#include "DownloadIntelligence.hpp"

#include "DownloadManager.hpp"

#include <QCoreApplication>
#include <QDebug>
#include <QFileInfo>
#include <QMetaObject>
#include <QRegularExpression>

#include <memory>

#ifdef POCB_HAS_SWIFT_INTELLIGENCE
extern "C" {
bool pocb_download_intelligence_available(void);
void pocb_suggest_download_name(const char *url, const char *originalName, const char *pageTitle,
                                const char *mimeType, void *context,
                                void (*callback)(void *context, const char *suggestion));
}
#endif

namespace DownloadIntelligence {

namespace {
struct Pending {
    std::function<void(QString)> done;
    QString originalName;
};

void completeOnMainThread(Pending *pending, QString suggestion) {
    std::shared_ptr<Pending> owned(pending);
    QMetaObject::invokeMethod(QCoreApplication::instance(), [owned, suggestion = std::move(suggestion)] {
        if (owned->done) owned->done(sanitizeSuggestion(suggestion, owned->originalName));
    }, Qt::QueuedConnection);
}
}  // namespace

bool isAvailable() {
#ifdef POCB_HAS_SWIFT_INTELLIGENCE
    if (@available(macOS 26.0, *)) return pocb_download_intelligence_available();
#endif
    return false;
}

QString sanitizeSuggestion(const QString &suggestion, const QString &originalName) {
    QString s = suggestion;
    s.remove(QRegularExpression(QStringLiteral("[\\x00-\\x1f\\x7f]")));
    s.replace(QRegularExpression(QStringLiteral("[/\\\\:]")), QStringLiteral("-"));
    s = s.simplified();
    while (s.startsWith(QLatin1Char('.'))) s.remove(0, 1);
    if (s.isEmpty()) return QString();
    const QString ext = QFileInfo(originalName).suffix();
    if (!ext.isEmpty()) {
        if (s.endsWith(QLatin1Char('.') + ext, Qt::CaseInsensitive)) s.chop(ext.size() + 1);
        else {
            const QString sExt = QFileInfo(s).suffix();
            static const QRegularExpression looksLikeExtension(QStringLiteral("^[A-Za-z][A-Za-z0-9]{0,4}$"));
            if (looksLikeExtension.match(sExt).hasMatch()) s.chop(sExt.size() + 1);
        }
        s = s.trimmed();
        while (s.endsWith(QLatin1Char('.'))) s.chop(1);
        if (s.isEmpty()) return QString();
        const int maxBase = 120 - ext.size() - 1;
        if (s.size() > maxBase) s = s.left(maxBase).trimmed();
        return s + QLatin1Char('.') + ext;
    }
    if (s.size() > 120) s = s.left(120).trimmed();
    return s;
}

void suggestName(const DownloadItem &item, std::function<void(QString)> done) {
    if (!done) return;
    if (!isAvailable()) {
        QMetaObject::invokeMethod(QCoreApplication::instance(), [done] { done(QString()); }, Qt::QueuedConnection);
        return;
    }
#ifdef POCB_HAS_SWIFT_INTELLIGENCE
    auto *pending = new Pending{std::move(done), item.fileName};
    const QByteArray url = item.url.toString().toUtf8();
    const QByteArray name = item.fileName.toUtf8();
    const QByteArray title = item.pageTitle.toUtf8();
    const QByteArray mime = item.mimeType.toUtf8();
    pocb_suggest_download_name(url.constData(), name.constData(), title.constData(), mime.constData(), pending,
                               [](void *ctx, const char *suggestion) {
                                   completeOnMainThread(static_cast<Pending *>(ctx),
                                                        suggestion ? QString::fromUtf8(suggestion) : QString());
                               });
#else
    (void)item;
#endif
}

}  // namespace DownloadIntelligence
