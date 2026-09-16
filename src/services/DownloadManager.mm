#include "DownloadManager.hpp"

#include "DownloadIntelligence.hpp"
#include "WebView.hpp"

#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#import <UserNotifications/UserNotifications.h>
#import <WebKit/WKDownload.h>
#import <WebKit/WKDownloadDelegate.h>

#include <sys/xattr.h>

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHash>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPixmap>
#include <QPointer>
#include <QSettings>
#include <QStandardPaths>
#include <QUuid>

#include <algorithm>

namespace {
constexpr int kMaxPersistedItems = 100;
constexpr qint64 kUnknownTotal = -1;

QString sanitizeFileName(QString name) {
    name.replace(QLatin1Char('/'), QLatin1Char('-'));
    name.replace(QLatin1Char(':'), QLatin1Char('-'));
    name.replace(QLatin1Char('\\'), QLatin1Char('-'));
    name.replace(QChar(0), QString());
    name = name.trimmed();
    while (name.startsWith(QLatin1Char('.'))) name.remove(0, 1);
    if (name.size() > 200) {
        const QString ext = QFileInfo(name).suffix();
        name = name.left(200 - ext.size() - 1);
        if (!ext.isEmpty()) name += QLatin1Char('.') + ext;
    }
    if (name.isEmpty()) name = QStringLiteral("download");
    return name;
}

// "name.ext" -> "name (2).ext" until the path is free.
QString uniquePath(const QString &dir, const QString &fileName) {
    const QFileInfo info(fileName);
    const QString base = info.completeBaseName();
    const QString ext = info.suffix();
    QString candidate = QDir(dir).filePath(fileName);
    for (int n = 2; QFileInfo::exists(candidate) && n < 10000; ++n) {
        const QString numbered = ext.isEmpty()
            ? QStringLiteral("%1 (%2)").arg(base).arg(n)
            : QStringLiteral("%1 (%2).%3").arg(base).arg(n).arg(ext);
        candidate = QDir(dir).filePath(numbered);
    }
    return candidate;
}

void setWhereFroms(const QString &path, const QUrl &url, const QUrl &referrer) {
    NSMutableArray<NSString *> *froms = [NSMutableArray array];
    if (url.isValid()) [froms addObject:url.toString().toNSString()];
    if (referrer.isValid() && referrer != url) [froms addObject:referrer.toString().toNSString()];
    if (froms.count == 0) return;
    NSError *err = nil;
    NSData *plist = [NSPropertyListSerialization dataWithPropertyList:froms
                                                               format:NSPropertyListBinaryFormat_v1_0
                                                              options:0
                                                                error:&err];
    if (!plist || err) return;
    const QByteArray p = QFile::encodeName(path);
    setxattr(p.constData(), "com.apple.metadata:kMDItemWhereFroms", plist.bytes, plist.length, 0, 0);
}

QIcon iconFromNSImage(NSImage *img, int pointSize) {
    if (!img) return QIcon();
    QIcon icon;
    for (double scale : {1.0, 2.0}) {
        const int px = static_cast<int>(pointSize * scale);
        NSBitmapImageRep *rep = [[NSBitmapImageRep alloc]
            initWithBitmapDataPlanes:NULL pixelsWide:px pixelsHigh:px bitsPerSample:8 samplesPerPixel:4
                            hasAlpha:YES isPlanar:NO colorSpaceName:NSDeviceRGBColorSpace
                        bitmapFormat:(NSBitmapFormat)0 bytesPerRow:0 bitsPerPixel:32];
        if (!rep) continue;
        rep.size = NSMakeSize(pointSize, pointSize);
        [NSGraphicsContext saveGraphicsState];
        NSGraphicsContext *ctx = [NSGraphicsContext graphicsContextWithBitmapImageRep:rep];
        [NSGraphicsContext setCurrentContext:ctx];
        [img drawInRect:NSMakeRect(0, 0, pointSize, pointSize) fromRect:NSZeroRect
              operation:NSCompositingOperationSourceOver fraction:1.0 respectFlipped:YES hints:nil];
        [ctx flushGraphics];
        [NSGraphicsContext restoreGraphicsState];
        QImage out(px, px, QImage::Format_ARGB32_Premultiplied);
        out.fill(Qt::transparent);
        const unsigned char *src = rep.bitmapData;
        const NSInteger stride = rep.bytesPerRow;
        for (int y = 0; y < px; ++y) {
            uchar *dst = out.scanLine(y);
            const unsigned char *row = src + y * stride;
            for (int x = 0; x < px; ++x) {
                dst[x * 4 + 0] = row[x * 4 + 2];
                dst[x * 4 + 1] = row[x * 4 + 1];
                dst[x * 4 + 2] = row[x * 4 + 0];
                dst[x * 4 + 3] = row[x * 4 + 3];
            }
        }
        out.setDevicePixelRatio(scale);
        icon.addPixmap(QPixmap::fromImage(out));
    }
    return icon;
}
}  // namespace

QString humanSize(qint64 bytes) {
    if (bytes < 0) return QString();
    const char *units[] = {"B", "KB", "MB", "GB", "TB"};
    double v = static_cast<double>(bytes);
    int u = 0;
    while (v >= 1000.0 && u < 4) { v /= 1000.0; ++u; }
    if (u == 0) return QStringLiteral("%1 B").arg(bytes);
    QString num = QString::number(v, 'f', v < 100.0 ? 1 : 0);
    if (num.endsWith(QLatin1String(".0"))) num.chop(2);
    return QStringLiteral("%1 %2").arg(num, QLatin1String(units[u]));
}

struct DownloadManagerBridge;

@interface PocbDownloadDelegate : NSObject <WKDownloadDelegate>
@property(nonatomic, assign) DownloadManagerBridge *bridge;
@property(nonatomic, copy) NSString *itemId;
@property(nonatomic, strong) WKDownload *download;
@property(nonatomic, strong) NSProgress *observedProgress;
- (void)stopObserving;
@end

struct DownloadManager::Impl {
    DownloadManager *q = nullptr;
    QVector<DownloadItem> items;
    QHash<QString, PocbDownloadDelegate *> delegates;
    struct Speed { QElapsedTimer timer; qint64 lastBytes = 0; double ema = 0.0; };
    QHash<QString, Speed> speeds;
    int lastActive = -1;
    double lastAggregate = -1.0;
    bool notificationsAuthorized = false;
    bool notificationsRequested = false;

    int indexOf(const QString &id) const {
        for (int i = 0; i < items.size(); ++i) if (items[i].id == id) return i;
        return -1;
    }

    QString storePath() const {
        const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        QDir().mkpath(dir);
        return QDir(dir).filePath(QStringLiteral("downloads.json"));
    }

    void load() {
        QFile f(storePath());
        if (!f.open(QIODevice::ReadOnly)) return;
        const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
        for (const QJsonValue &v : doc.array()) {
            const QJsonObject o = v.toObject();
            DownloadItem it;
            it.id = o.value("id").toString();
            it.url = QUrl(o.value("url").toString());
            it.fileName = o.value("fileName").toString();
            it.path = o.value("path").toString();
            it.received = static_cast<qint64>(o.value("received").toDouble());
            it.total = static_cast<qint64>(o.value("total").toDouble(kUnknownTotal));
            it.state = static_cast<DownloadItem::State>(o.value("state").toInt(DownloadItem::Completed));
            if (it.state == DownloadItem::Running) it.state = DownloadItem::Cancelled;
            it.started = QDateTime::fromString(o.value("started").toString(), Qt::ISODate);
            it.finished = QDateTime::fromString(o.value("finished").toString(), Qt::ISODate);
            it.suggestedName = o.value("suggestedName").toString();
            it.mimeType = o.value("mimeType").toString();
            it.pageTitle = o.value("pageTitle").toString();
            it.error = o.value("error").toString();
            if (!it.id.isEmpty()) items.push_back(it);
        }
    }

    void save() const {
        QJsonArray arr;
        int count = 0;
        for (auto it = items.crbegin(); it != items.crend() && count < kMaxPersistedItems; ++it) {
            if (it->state == DownloadItem::Running) continue;
            QJsonObject o;
            o["id"] = it->id;
            o["url"] = it->url.toString();
            o["fileName"] = it->fileName;
            o["path"] = it->path;
            o["received"] = static_cast<double>(it->received);
            o["total"] = static_cast<double>(it->total);
            o["state"] = static_cast<int>(it->state);
            o["started"] = it->started.toString(Qt::ISODate);
            o["finished"] = it->finished.toString(Qt::ISODate);
            o["suggestedName"] = it->suggestedName;
            o["mimeType"] = it->mimeType;
            o["pageTitle"] = it->pageTitle;
            o["error"] = it->error;
            arr.prepend(o);
            ++count;
        }
        QFile f(storePath());
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return;
        f.write(QJsonDocument(arr).toJson(QJsonDocument::Compact));
    }

    void emitActiveState() {
        const int active = q->activeCount();
        const double agg = q->aggregateProgress();
        if (active != lastActive || std::abs(agg - lastAggregate) > 0.002 || active == 0) {
            lastActive = active;
            lastAggregate = agg;
            emit q->activeCountChanged(active, agg);
        }
    }

    void updateSpeed(DownloadItem &it) {
        Speed &s = speeds[it.id];
        if (!s.timer.isValid()) { s.timer.start(); s.lastBytes = it.received; return; }
        const qint64 ms = s.timer.elapsed();
        if (ms < 400) return;
        const double inst = (it.received - s.lastBytes) * 1000.0 / static_cast<double>(ms);
        s.ema = s.ema <= 0.0 ? inst : (s.ema * 0.6 + inst * 0.4);
        s.lastBytes = it.received;
        s.timer.restart();
        it.bytesPerSecond = s.ema;
    }

    void finishItem(const QString &id, DownloadItem::State state, const QString &error) {
        const int i = indexOf(id);
        if (i < 0) return;
        DownloadItem &it = items[i];
        it.state = state;
        it.finished = QDateTime::currentDateTime();
        it.error = error;
        it.bytesPerSecond = 0.0;
        if (state == DownloadItem::Completed) {
            const qint64 sz = QFileInfo(it.path).size();
            if (sz > 0) { it.received = sz; if (it.total <= 0) it.total = sz; }
        } else if (state == DownloadItem::Cancelled && !it.path.isEmpty()) {
            QFile::remove(it.path);
        }
        speeds.remove(id);
        if (PocbDownloadDelegate *d = delegates.take(id)) {
            [d stopObserving];
            d.download = nil;
        }
        const DownloadItem copy = it;
        save();
        emit q->itemUpdated(id);
        emit q->itemFinished(id, state == DownloadItem::Completed);
        emitActiveState();
        if (state == DownloadItem::Completed) {
            notifyCompleted(copy);
            maybeSuggestName(copy);
        }
    }

    void notifyCompleted(const DownloadItem &it) {
        if ([NSApp isActive]) return;
        if (![NSBundle mainBundle].bundleIdentifier) return;
        @try {
            UNUserNotificationCenter *center = [UNUserNotificationCenter currentNotificationCenter];
            if (!center) return;
            const QString title = it.fileName;
            const QString body = QStringLiteral("Download complete · %1").arg(humanSize(it.received));
            auto post = ^{
                UNMutableNotificationContent *content = [UNMutableNotificationContent new];
                content.title = title.toNSString();
                content.body = body.toNSString();
                content.sound = nil;
                UNNotificationRequest *req = [UNNotificationRequest requestWithIdentifier:it.id.toNSString()
                                                                                  content:content
                                                                                  trigger:nil];
                [center addNotificationRequest:req withCompletionHandler:^(NSError *e) {
                    if (e) qDebug() << "[downloads] notification failed:" << QString::fromNSString(e.localizedDescription);
                }];
            };
            if (notificationsAuthorized) { post(); return; }
            if (notificationsRequested) return;
            notificationsRequested = true;
            [center requestAuthorizationWithOptions:(UNAuthorizationOptionAlert | UNAuthorizationOptionSound)
                                  completionHandler:^(BOOL granted, NSError *) {
                dispatch_async(dispatch_get_main_queue(), ^{
                    notificationsAuthorized = granted;
                    if (granted) post();
                });
            }];
        } @catch (NSException *e) {
            qDebug() << "[downloads] notifications unavailable:" << QString::fromNSString(e.reason);
        }
    }

    void maybeSuggestName(const DownloadItem &it) {
        QSettings settings;
        if (!settings.value(QStringLiteral("downloads/smartRename"), false).toBool()) return;
        if (!DownloadIntelligence::isAvailable()) return;
        const QString id = it.id;
        QPointer<DownloadManager> guard(q);
        DownloadIntelligence::suggestName(it, [guard, id](const QString &suggestion) {
            if (!guard || suggestion.isEmpty()) return;
            DownloadManager::Impl *impl = guard->m_impl;
            const int i = impl->indexOf(id);
            if (i < 0 || impl->items[i].state != DownloadItem::Completed) return;
            if (suggestion == impl->items[i].fileName) return;
            if (QSettings().value(QStringLiteral("downloads/smartRenameAsk"), true).toBool()) {
                impl->items[i].suggestedName = suggestion;
                impl->save();
                emit guard->itemUpdated(id);
                emit guard->renameSuggested(id, suggestion);
            } else {
                guard->rename(id, suggestion);
            }
        });
    }
};

// Static trampolines the ObjC delegate can call without seeing Impl's layout.
struct DownloadManagerBridge {
    DownloadManager::Impl *impl;

    NSURL *destination(NSString *itemId, NSURLResponse *response, NSString *suggested) {
        const QString id = QString::fromNSString(itemId);
        const int i = impl->indexOf(id);
        if (i < 0) return nil;
        DownloadItem &it = impl->items[i];
        QString name = sanitizeFileName(QString::fromNSString(suggested ?: response.suggestedFilename ?: @"download"));
        if (response.MIMEType) it.mimeType = QString::fromNSString(response.MIMEType);
        if (response.expectedContentLength > 0) it.total = response.expectedContentLength;
        if (response.URL) it.url = QUrl::fromNSURL(response.URL);
        it.path = uniquePath(impl->q->downloadsDirectory(), name);
        it.fileName = QFileInfo(it.path).fileName();
        qDebug() << "[downloads] destination" << it.path << "total" << it.total;
        emit impl->q->itemUpdated(id);
        return [NSURL fileURLWithPath:it.path.toNSString()];
    }

    void progress(NSString *itemId, int64_t completed, int64_t total) {
        const QString id = QString::fromNSString(itemId);
        const int i = impl->indexOf(id);
        if (i < 0) return;
        DownloadItem &it = impl->items[i];
        if (it.state != DownloadItem::Running) return;
        it.received = std::max<qint64>(0, completed);
        if (total > 0) it.total = total;
        impl->updateSpeed(it);
        emit impl->q->itemUpdated(id);
        impl->emitActiveState();
    }

    void finished(NSString *itemId) {
        const QString id = QString::fromNSString(itemId);
        const int i = impl->indexOf(id);
        if (i < 0) return;
        const DownloadItem it = impl->items[i];
        setWhereFroms(it.path, it.url, QUrl());
        qDebug() << "[downloads] finished" << it.path;
        impl->finishItem(id, DownloadItem::Completed, QString());
    }

    void failed(NSString *itemId, NSError *error) {
        const QString id = QString::fromNSString(itemId);
        const bool cancelled = error && [error.domain isEqualToString:NSURLErrorDomain] && error.code == NSURLErrorCancelled;
        qDebug() << "[downloads] failed" << id << (error ? QString::fromNSString(error.localizedDescription) : QString());
        impl->finishItem(id, cancelled ? DownloadItem::Cancelled : DownloadItem::Failed,
                         error ? QString::fromNSString(error.localizedDescription) : QString());
    }

    void redirected(NSString *itemId, NSURLRequest *request) {
        const QString id = QString::fromNSString(itemId);
        const int i = impl->indexOf(id);
        if (i < 0 || !request.URL) return;
        impl->items[i].url = QUrl::fromNSURL(request.URL);
        emit impl->q->itemUpdated(id);
    }
};

static DownloadManagerBridge gBridge{nullptr};

@implementation PocbDownloadDelegate

- (void)observeProgress:(NSProgress *)progress {
    [self stopObserving];
    self.observedProgress = progress;
    [progress addObserver:self forKeyPath:@"completedUnitCount" options:NSKeyValueObservingOptionNew context:nullptr];
    [progress addObserver:self forKeyPath:@"totalUnitCount" options:NSKeyValueObservingOptionNew context:nullptr];
    [progress addObserver:self forKeyPath:@"fractionCompleted" options:NSKeyValueObservingOptionNew context:nullptr];
}

- (void)stopObserving {
    NSProgress *p = self.observedProgress;
    if (!p) return;
    @try {
        [p removeObserver:self forKeyPath:@"completedUnitCount"];
        [p removeObserver:self forKeyPath:@"totalUnitCount"];
        [p removeObserver:self forKeyPath:@"fractionCompleted"];
    } @catch (NSException *) {
    }
    self.observedProgress = nil;
}

- (void)dealloc {
    [self stopObserving];
}

- (void)observeValueForKeyPath:(NSString *)keyPath ofObject:(id)object change:(NSDictionary *)change context:(void *)context {
    (void)keyPath; (void)change; (void)context;
    NSProgress *p = (NSProgress *)object;
    const int64_t completed = p.completedUnitCount;
    const int64_t total = p.totalUnitCount;
    NSString *itemId = self.itemId;
    void (^apply)(void) = ^{
        if (self.bridge && self.bridge->impl) self.bridge->progress(itemId, completed, total);
    };
    if ([NSThread isMainThread]) apply(); else dispatch_async(dispatch_get_main_queue(), apply);
}

- (void)download:(WKDownload *)download
    decideDestinationUsingResponse:(NSURLResponse *)response
                 suggestedFilename:(NSString *)suggestedFilename
                 completionHandler:(void (^)(NSURL *))completionHandler {
    (void)download;
    NSURL *dest = self.bridge && self.bridge->impl ? self.bridge->destination(self.itemId, response, suggestedFilename) : nil;
    completionHandler(dest);
    if (download.progress) [self observeProgress:download.progress];
}

- (void)download:(WKDownload *)download
    willPerformHTTPRedirection:(NSHTTPURLResponse *)response
                    newRequest:(NSURLRequest *)request
               decisionHandler:(void (^)(WKDownloadRedirectPolicy))decisionHandler {
    (void)download; (void)response;
    if (self.bridge && self.bridge->impl) self.bridge->redirected(self.itemId, request);
    decisionHandler(WKDownloadRedirectPolicyAllow);
}

- (void)downloadDidFinish:(WKDownload *)download {
    (void)download;
    [self stopObserving];
    if (self.bridge && self.bridge->impl) self.bridge->finished(self.itemId);
}

- (void)download:(WKDownload *)download didFailWithError:(NSError *)error resumeData:(NSData *)resumeData {
    (void)download; (void)resumeData;
    [self stopObserving];
    if (self.bridge && self.bridge->impl) self.bridge->failed(self.itemId, error);
}

@end

DownloadManager *DownloadManager::instance() {
    static DownloadManager *inst = new DownloadManager();
    return inst;
}

DownloadManager::DownloadManager() : m_impl(new Impl) {
    m_impl->q = this;
    gBridge.impl = m_impl;
    m_impl->load();

    WebView::addNativeWebViewHook([this](void *, WebView *owner) {
        if (!owner) return;
        connect(owner, &WebView::downloadStarted, this, [this, owner](void *wkDownload) {
            if (!wkDownload) return;
            WKDownload *download = (__bridge WKDownload *)wkDownload;
            DownloadItem it;
            it.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
            it.started = QDateTime::currentDateTime();
            it.url = download.originalRequest.URL ? QUrl::fromNSURL(download.originalRequest.URL) : owner->url();
            it.fileName = it.url.fileName().isEmpty() ? QStringLiteral("download") : it.url.fileName();
            it.pageTitle = owner->title();
            m_impl->items.push_back(it);

            PocbDownloadDelegate *delegate = [PocbDownloadDelegate new];
            delegate.bridge = &gBridge;
            delegate.itemId = it.id.toNSString();
            delegate.download = download;
            download.delegate = delegate;
            m_impl->delegates.insert(it.id, delegate);
            qDebug() << "[downloads] started" << it.url;
            emit itemAdded(it.id);
            m_impl->emitActiveState();
        });
    });
}

DownloadManager::~DownloadManager() {
    gBridge.impl = nullptr;
    delete m_impl;
}

QVector<DownloadItem> DownloadManager::items() const { return m_impl->items; }

bool DownloadManager::item(const QString &id, DownloadItem *out) const {
    const int i = m_impl->indexOf(id);
    if (i < 0) return false;
    if (out) *out = m_impl->items[i];
    return true;
}

int DownloadManager::activeCount() const {
    return static_cast<int>(std::count_if(m_impl->items.cbegin(), m_impl->items.cend(),
                                          [](const DownloadItem &d) { return d.state == DownloadItem::Running; }));
}

double DownloadManager::aggregateProgress() const {
    qint64 received = 0, total = 0;
    int unknown = 0, running = 0;
    for (const DownloadItem &d : m_impl->items) {
        if (d.state != DownloadItem::Running) continue;
        ++running;
        if (d.total > 0) { received += d.received; total += d.total; } else ++unknown;
    }
    if (running == 0) return 0.0;
    if (total <= 0) return unknown ? 0.0 : 1.0;
    return std::clamp(static_cast<double>(received) / static_cast<double>(total), 0.0, 1.0);
}

void DownloadManager::cancel(const QString &id) {
    PocbDownloadDelegate *d = m_impl->delegates.value(id);
    if (d && d.download) {
        [d.download cancel:^(NSData *) {}];
    }
    m_impl->finishItem(id, DownloadItem::Cancelled, QString());
}

void DownloadManager::remove(const QString &id) {
    const int i = m_impl->indexOf(id);
    if (i < 0) return;
    if (m_impl->items[i].state == DownloadItem::Running) { cancel(id); }
    const int j = m_impl->indexOf(id);
    if (j < 0) return;
    m_impl->items.remove(j);
    m_impl->save();
    emit itemUpdated(id);
    m_impl->emitActiveState();
}

void DownloadManager::clearFinished() {
    QStringList removed;
    for (int i = m_impl->items.size() - 1; i >= 0; --i) {
        if (m_impl->items[i].state != DownloadItem::Running) {
            removed << m_impl->items[i].id;
            m_impl->items.remove(i);
        }
    }
    m_impl->save();
    for (const QString &id : removed) emit itemUpdated(id);
}

void DownloadManager::revealInFinder(const QString &id) {
    DownloadItem it;
    if (!item(id, &it) || it.path.isEmpty()) return;
    [[NSWorkspace sharedWorkspace] selectFile:it.path.toNSString() inFileViewerRootedAtPath:@""];
}

void DownloadManager::open(const QString &id) {
    DownloadItem it;
    if (!item(id, &it) || it.path.isEmpty() || !QFileInfo::exists(it.path)) return;
    [[NSWorkspace sharedWorkspace] openURL:[NSURL fileURLWithPath:it.path.toNSString()]];
}

bool DownloadManager::rename(const QString &id, const QString &newName) {
    const int i = m_impl->indexOf(id);
    if (i < 0) return false;
    DownloadItem &it = m_impl->items[i];
    if (it.state != DownloadItem::Completed || it.path.isEmpty()) return false;
    const QString clean = sanitizeFileName(newName);
    if (clean.isEmpty() || clean == it.fileName) return false;
    const QString dir = QFileInfo(it.path).absolutePath();
    const QString target = uniquePath(dir, clean);
    if (!QFile::rename(it.path, target)) {
        qDebug() << "[downloads] rename failed" << it.path << "->" << target;
        return false;
    }
    it.path = target;
    it.fileName = QFileInfo(target).fileName();
    it.suggestedName.clear();
    m_impl->save();
    emit itemUpdated(id);
    return true;
}

void DownloadManager::applySuggestion(const QString &id) {
    const int i = m_impl->indexOf(id);
    if (i < 0 || m_impl->items[i].suggestedName.isEmpty()) return;
    const QString name = m_impl->items[i].suggestedName;
    if (!rename(id, name)) dismissSuggestion(id);
}

void DownloadManager::dismissSuggestion(const QString &id) {
    const int i = m_impl->indexOf(id);
    if (i < 0) return;
    m_impl->items[i].suggestedName.clear();
    m_impl->save();
    emit itemUpdated(id);
}

QString DownloadManager::downloadsDirectory() const {
    QString dir = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    if (dir.isEmpty()) dir = QDir::homePath() + QStringLiteral("/Downloads");
    QDir().mkpath(dir);
    return dir;
}

QIcon DownloadManager::fileIcon(const QString &path, int pointSize) {
    NSImage *img = nil;
    if (!path.isEmpty() && QFileInfo::exists(path)) {
        img = [[NSWorkspace sharedWorkspace] iconForFile:path.toNSString()];
    } else {
        const QString ext = QFileInfo(path).suffix();
        UTType *type = ext.isEmpty() ? nil : [UTType typeWithFilenameExtension:ext.toNSString()];
        img = [[NSWorkspace sharedWorkspace] iconForContentType:type ?: UTTypeData];
    }
    return iconFromNSImage(img, pointSize);
}
