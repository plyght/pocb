#include "ChromeExtensionManager.hpp"

#include "BrowserWindow.hpp"
#include "WebView.hpp"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFileInfo>
#include <QJsonArray>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QPixmap>

#import <Foundation/Foundation.h>
#import <WebKit/WKWebExtension.h>
#import <WebKit/WKWebExtensionContext.h>
#import <WebKit/WKWebExtensionController.h>
#import <WebKit/WKWebExtensionControllerConfiguration.h>
#import <WebKit/WKWebExtensionAction.h>
#import <WebKit/WKWebExtensionMatchPattern.h>
#import <WebKit/WKWebExtensionTab.h>
#import <WebKit/WKWebExtensionTabConfiguration.h>
#import <WebKit/WKWebExtensionWindow.h>
#import <WebKit/WKWebExtensionWindowConfiguration.h>
#import <WebKit/WKWebView.h>
#import <WebKit/WKWebViewConfiguration.h>
#import <WebKit/WKNavigationAction.h>
#import <WebKit/WKUIDelegate.h>
#import <WebKit/WKUserContentController.h>
#import <WebKit/WKContentRuleList.h>
#import <WebKit/WKContentRuleListStore.h>

static BrowserWindow *g_browserWindow = nullptr;
static NSView *g_popupAnchor = nil;
static WKWebExtensionController *g_extensionController = nil;
static NSMutableArray<WKWebExtensionContext *> *g_extensionContexts = nil;
static NSMutableDictionary<NSValue *, id> *g_extensionTabs = nil;
static id g_extensionWindow = nil;

@interface PocbExtensionTab : NSObject <WKWebExtensionTab>
@property(nonatomic, assign) WebView *view;
@end

@interface PocbExtensionWindow : NSObject <WKWebExtensionWindow>
@end

@interface PocbExtensionPopupDelegate : NSObject <WKUIDelegate>
@end

@interface PocbExtensionDelegate : NSObject <WKWebExtensionControllerDelegate>
@end

static PocbExtensionWindow *pocbExtensionWindow() {
    if (!g_extensionWindow) g_extensionWindow = [PocbExtensionWindow new];
    return g_extensionWindow;
}

static PocbExtensionTab *pocbTabForView(WebView *view) {
    if (!view) return nil;
    if (!g_extensionTabs) g_extensionTabs = [NSMutableDictionary dictionary];
    NSValue *key = [NSValue valueWithPointer:view];
    PocbExtensionTab *tab = g_extensionTabs[key];
    if (!tab) {
        tab = [PocbExtensionTab new];
        tab.view = view;
        g_extensionTabs[key] = tab;
    }
    return tab;
}

@implementation PocbExtensionTab
- (id<WKWebExtensionWindow>)windowForWebExtensionContext:(WKWebExtensionContext *)context { (void)context; return g_browserWindow ? pocbExtensionWindow() : nil; }
- (WKWebView *)webViewForWebExtensionContext:(WKWebExtensionContext *)context { (void)context; return (__bridge WKWebView *)self.view->nativeWebView(); }
- (NSString *)titleForWebExtensionContext:(WKWebExtensionContext *)context { (void)context; return self.view->title().toNSString(); }
- (NSUInteger)indexInWindowForWebExtensionContext:(WKWebExtensionContext *)context { (void)context; return g_browserWindow ? (NSUInteger)g_browserWindow->extensionViews().indexOf(self.view) : NSNotFound; }
- (NSURL *)urlForWebExtensionContext:(WKWebExtensionContext *)context { (void)context; QUrl url = self.view ? self.view->url() : QUrl(); return url.isValid() ? [NSURL URLWithString:url.toString().toNSString()] : nil; }
- (NSURL *)pendingURLForWebExtensionContext:(WKWebExtensionContext *)context { (void)context; return nil; }
- (BOOL)isLoadingCompleteForWebExtensionContext:(WKWebExtensionContext *)context { (void)context; WKWebView *wk = self.view ? (__bridge WKWebView *)self.view->nativeWebView() : nil; return !wk || wk.estimatedProgress >= 1.0; }
- (BOOL)isPinnedForWebExtensionContext:(WKWebExtensionContext *)context { (void)context; return NO; }
- (BOOL)isMutedForWebExtensionContext:(WKWebExtensionContext *)context { (void)context; return NO; }
- (BOOL)isReaderModeAvailableForWebExtensionContext:(WKWebExtensionContext *)context { (void)context; return NO; }
- (void)loadURL:(NSURL *)url forWebExtensionContext:(WKWebExtensionContext *)context completionHandler:(void (^)(NSError *))completionHandler { (void)context; self.view->load(QUrl(QString::fromNSString(url.absoluteString))); completionHandler(nil); }
- (void)reloadFromOrigin:(BOOL)fromOrigin forWebExtensionContext:(WKWebExtensionContext *)context completionHandler:(void (^)(NSError *))completionHandler { (void)context; (void)fromOrigin; self.view->reload(); completionHandler(nil); }
- (void)goBackForWebExtensionContext:(WKWebExtensionContext *)context completionHandler:(void (^)(NSError *))completionHandler { (void)context; self.view->back(); completionHandler(nil); }
- (void)goForwardForWebExtensionContext:(WKWebExtensionContext *)context completionHandler:(void (^)(NSError *))completionHandler { (void)context; self.view->forward(); completionHandler(nil); }
- (void)activateForWebExtensionContext:(WKWebExtensionContext *)context completionHandler:(void (^)(NSError *))completionHandler { (void)context; if (g_browserWindow) g_browserWindow->extensionSelectView(self.view); completionHandler(nil); }
- (BOOL)isSelectedForWebExtensionContext:(WKWebExtensionContext *)context { (void)context; return g_browserWindow && g_browserWindow->extensionCurrentView() == self.view; }
- (void)setSelected:(BOOL)selected forWebExtensionContext:(WKWebExtensionContext *)context completionHandler:(void (^)(NSError *))completionHandler { (void)context; if (selected && g_browserWindow) g_browserWindow->extensionSelectView(self.view); completionHandler(nil); }
- (BOOL)shouldGrantPermissionsOnUserGestureForWebExtensionContext:(WKWebExtensionContext *)context { (void)context; return YES; }
- (BOOL)shouldBypassPermissionsForWebExtensionContext:(WKWebExtensionContext *)context { (void)context; return YES; }
- (void)closeForWebExtensionContext:(WKWebExtensionContext *)context completionHandler:(void (^)(NSError *))completionHandler { (void)context; if (g_browserWindow) g_browserWindow->extensionCloseView(self.view); completionHandler(nil); }
@end

@implementation PocbExtensionWindow
- (NSArray<id<WKWebExtensionTab>> *)tabsForWebExtensionContext:(WKWebExtensionContext *)context { (void)context; NSMutableArray *tabs = [NSMutableArray array]; if (!g_browserWindow) return tabs; for (WebView *view : g_browserWindow->extensionViews()) [tabs addObject:pocbTabForView(view)]; return tabs; }
- (id<WKWebExtensionTab>)activeTabForWebExtensionContext:(WKWebExtensionContext *)context { (void)context; return g_browserWindow ? pocbTabForView(g_browserWindow->extensionCurrentView()) : nil; }
- (WKWebExtensionWindowType)windowTypeForWebExtensionContext:(WKWebExtensionContext *)context { (void)context; return WKWebExtensionWindowTypeNormal; }
- (WKWebExtensionWindowState)windowStateForWebExtensionContext:(WKWebExtensionContext *)context { (void)context; return g_browserWindow && g_browserWindow->isFullScreen() ? WKWebExtensionWindowStateFullscreen : WKWebExtensionWindowStateNormal; }
- (CGRect)frameForWebExtensionContext:(WKWebExtensionContext *)context { (void)context; return g_browserWindow ? CGRectMake(g_browserWindow->x(), g_browserWindow->y(), g_browserWindow->width(), g_browserWindow->height()) : CGRectZero; }
- (BOOL)isPrivateForWebExtensionContext:(WKWebExtensionContext *)context { (void)context; return NO; }
- (void)focusForWebExtensionContext:(WKWebExtensionContext *)context completionHandler:(void (^)(NSError *))completionHandler { (void)context; if (g_browserWindow) { g_browserWindow->show(); g_browserWindow->raise(); g_browserWindow->activateWindow(); } completionHandler(nil); }
@end

@implementation PocbExtensionPopupDelegate
- (WKWebView *)webView:(WKWebView *)webView createWebViewWithConfiguration:(WKWebViewConfiguration *)configuration forNavigationAction:(WKNavigationAction *)navigationAction windowFeatures:(WKWindowFeatures *)windowFeatures { (void)configuration; (void)windowFeatures; if (navigationAction.request.URL) [webView loadRequest:navigationAction.request]; return nil; }
@end

@implementation PocbExtensionDelegate
- (NSArray<id<WKWebExtensionWindow>> *)webExtensionController:(WKWebExtensionController *)controller openWindowsForExtensionContext:(WKWebExtensionContext *)extensionContext { (void)controller; (void)extensionContext; return g_browserWindow ? @[ pocbExtensionWindow() ] : @[]; }
- (id<WKWebExtensionWindow>)webExtensionController:(WKWebExtensionController *)controller focusedWindowForExtensionContext:(WKWebExtensionContext *)extensionContext { (void)controller; (void)extensionContext; return g_browserWindow ? pocbExtensionWindow() : nil; }
- (void)webExtensionController:(WKWebExtensionController *)controller openNewTabUsingConfiguration:(WKWebExtensionTabConfiguration *)configuration forExtensionContext:(WKWebExtensionContext *)extensionContext completionHandler:(void (^)(id<WKWebExtensionTab>, NSError *))completionHandler { (void)controller; NSURL *url = configuration.url; BOOL shouldActivate = configuration.shouldBeActive; NSLog(@"pocb extension requested tab url=%@ active=%@", url.absoluteString, shouldActivate ? @"YES" : @"NO"); WebView *view = nullptr; BOOL extensionURL = [url.scheme containsString:@"extension"]; if (g_browserWindow && url && extensionURL && extensionContext.webViewConfiguration) { WKWebView *wk = [[WKWebView alloc] initWithFrame:NSZeroRect configuration:extensionContext.webViewConfiguration]; view = g_browserWindow->extensionAdoptNativeTab((__bridge_retained void *)wk, !shouldActivate); [wk loadRequest:[NSURLRequest requestWithURL:url]]; } else { view = g_browserWindow ? g_browserWindow->extensionCreateTab(url ? QUrl(QString::fromNSString(url.absoluteString)) : QUrl(), !shouldActivate) : nullptr; } if (!view) { completionHandler(nil, [NSError errorWithDomain:@"pocb.extensions" code:1 userInfo:@{NSLocalizedDescriptionKey:@"No browser window is available"}]); return; } PocbExtensionTab *tab = [PocbExtensionTab new]; tab.view = view; completionHandler(tab, nil); }
- (void)webExtensionController:(WKWebExtensionController *)controller openNewWindowUsingConfiguration:(WKWebExtensionWindowConfiguration *)configuration forExtensionContext:(WKWebExtensionContext *)extensionContext completionHandler:(void (^)(id<WKWebExtensionWindow>, NSError *))completionHandler { (void)controller; (void)configuration; (void)extensionContext; if (!g_browserWindow) { completionHandler(nil, [NSError errorWithDomain:@"pocb.extensions" code:2 userInfo:@{NSLocalizedDescriptionKey:@"No browser window is available"}]); return; } g_browserWindow->extensionCreateTab(QUrl(), false); completionHandler([PocbExtensionWindow new], nil); }
- (void)webExtensionController:(WKWebExtensionController *)controller openOptionsPageForExtensionContext:(WKWebExtensionContext *)extensionContext completionHandler:(void (^)(NSError *))completionHandler { (void)controller; if (!g_browserWindow || !extensionContext.optionsPageURL) { completionHandler([NSError errorWithDomain:@"pocb.extensions" code:3 userInfo:@{NSLocalizedDescriptionKey:@"No options page is available"}]); return; } g_browserWindow->extensionCreateTab(QUrl(QString::fromNSString(extensionContext.optionsPageURL.absoluteString)), false); completionHandler(nil); }
- (void)webExtensionController:(WKWebExtensionController *)controller didUpdateAction:(WKWebExtensionAction *)action forExtensionContext:(WKWebExtensionContext *)context { (void)controller; if (!g_browserWindow) return; QString key = QString::fromNSString(context.webExtension.displayName ?: context.webExtension.version ?: @"extension"); QString label = QString::fromNSString(action.label.length ? action.label : (context.webExtension.displayName ?: @"Extension")); QIcon icon; NSImage *image = [action iconForSize:CGSizeMake(32, 32)] ?: [context.webExtension iconForSize:CGSizeMake(32, 32)]; if (image) { CGImageRef cg = [image CGImageForProposedRect:nil context:nil hints:nil]; if (cg) { NSBitmapImageRep *rep = [[NSBitmapImageRep alloc] initWithCGImage:cg]; NSData *data = [rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}]; QPixmap pixmap; if (data.length && pixmap.loadFromData(reinterpret_cast<const uchar *>(data.bytes), static_cast<uint>(data.length), "PNG")) icon = QIcon(pixmap); } } if (icon.isNull()) { for (const auto &extension : ChromeExtensionManager::configuredExtensions()) { if (label.contains(extension.name, Qt::CaseInsensitive) || key.contains(extension.name, Qt::CaseInsensitive) || extension.name.contains(QStringLiteral("Ghostery"), Qt::CaseInsensitive)) { if (!extension.iconPath.isEmpty()) icon = QIcon(extension.iconPath); break; } } } g_browserWindow->extensionSetAction(key, label, icon, [context](QWidget *button) { if (!button) return; button->winId(); g_popupAnchor = (__bridge NSView *)reinterpret_cast<void *>(button->winId()); PocbExtensionTab *tab = g_browserWindow ? pocbTabForView(g_browserWindow->extensionCurrentView()) : nil; if (g_extensionController && g_browserWindow) { PocbExtensionWindow *window = pocbExtensionWindow(); [g_extensionController didFocusWindow:window]; if (tab) [g_extensionController didActivateTab:tab previousActiveTab:nil]; } if (tab) [context userGesturePerformedInTab:tab]; [context performActionForTab:tab]; }); }
- (void)webExtensionController:(WKWebExtensionController *)controller presentPopupForAction:(WKWebExtensionAction *)action forExtensionContext:(WKWebExtensionContext *)context completionHandler:(void (^)(NSError *))completionHandler { (void)controller; (void)context; if (!action.presentsPopup || !action.popupPopover) { completionHandler([NSError errorWithDomain:@"pocb.extensions" code:4 userInfo:@{NSLocalizedDescriptionKey:@"No popup is available"}]); return; } static PocbExtensionPopupDelegate *popupDelegate = nil; if (!popupDelegate) popupDelegate = [PocbExtensionPopupDelegate new]; action.popupWebView.UIDelegate = popupDelegate; NSView *anchor = g_popupAnchor; if (!anchor && g_browserWindow) { g_browserWindow->winId(); anchor = (__bridge NSView *)reinterpret_cast<void *>(g_browserWindow->winId()); } if (!anchor) { completionHandler([NSError errorWithDomain:@"pocb.extensions" code:5 userInfo:@{NSLocalizedDescriptionKey:@"No browser view is available"}]); return; } [action.popupPopover showRelativeToRect:anchor.bounds ofView:anchor preferredEdge:NSMinYEdge]; NSURL *activeURL = nil; if (g_browserWindow && g_browserWindow->extensionCurrentView()) { QUrl qurl = g_browserWindow->extensionCurrentView()->url(); if (qurl.isValid()) activeURL = [NSURL URLWithString:qurl.toString().toNSString()]; } if (activeURL && ([activeURL.scheme isEqualToString:@"http"] || [activeURL.scheme isEqualToString:@"https"])) { WKWebView *activeWebView = g_browserWindow && g_browserWindow->extensionCurrentView() ? (__bridge WKWebView *)g_browserWindow->extensionCurrentView()->nativeWebView() : nil; NSString *collector = @"(function(){try{var urls=[];function add(u){try{u=String(u||'');if(/^https?:/.test(u)&&urls.indexOf(u)<0)urls.push(u);}catch(e){}}try{performance.getEntriesByType('resource').forEach(function(e){add(e.name);});}catch(e){}try{document.querySelectorAll('[src],[href]').forEach(function(el){add(el.currentSrc||el.src||el.href);});}catch(e){}return urls.slice(0,500);}catch(e){return[];}})()"; [activeWebView evaluateJavaScript:collector completionHandler:^(id result, NSError *pageError) { (void)pageError; NSMutableArray<NSString *> *urls = [NSMutableArray array]; if ([result isKindOfClass:[NSArray class]]) { for (id value in (NSArray *)result) if ([value isKindOfClass:[NSString class]]) [urls addObject:value]; } NSData *urlsData = [NSJSONSerialization dataWithJSONObject:urls options:0 error:nil]; NSString *urlsJSON = urlsData ? [[NSString alloc] initWithData:urlsData encoding:NSUTF8StringEncoding] : @"[]"; NSString *pageURL = [activeURL.absoluteString stringByReplacingOccurrencesOfString:@"\\" withString:@"\\\\"]; pageURL = [pageURL stringByReplacingOccurrencesOfString:@"'" withString:@"\\'"]; NSString *seed = [NSString stringWithFormat:@"(function(){var page='%@',observed=%@;async function run(){try{if(!globalThis.chrome||!chrome.storage||!chrome.runtime)return;var trackerdb=await import(chrome.runtime.getURL('utils/trackerdb.js'));var reqmod=await import(chrome.runtime.getURL('utils/request.js'));if(trackerdb.setup&&trackerdb.setup.pending)await trackerdb.setup.pending;var ExtendedRequest=reqmod.default;var u=new URL(page);var host=u.hostname;var parts=host.split('.');var stats={domain:parts.slice(Math.max(0,parts.length-2)).join('.'),hostname:host,url:page,trackers:[],timestamp:Date.now()};var byId={};observed.forEach(function(url,i){try{var request=ExtendedRequest.fromRequestDetails({url:url,originUrl:page,tabId:1,requestId:'pocb-'+i,timeStamp:Date.now()});request.blocked=true;var metadata=trackerdb.getMetadata(request);if(!metadata)return;var tracker=byId[metadata.id];if(!tracker){tracker=byId[metadata.id]=Object.assign({},metadata,{blocked:true,modified:false,requestsCount:0,requests:[]});stats.trackers.push(tracker);}tracker.requestsCount=(tracker.requestsCount||0)+1;if(tracker.requests.length<100)tracker.requests.unshift({id:request.requestId,url:url,blocked:true,modified:false});}catch(e){}});var order=['advertising','site_analytics','customer_interaction','essential','audio_video_player','comments','pornvertising','social_media','cdn','hosting','extensions','unknown','unidentified'];stats.trackers.sort(function(a,b){return order.indexOf(a.category)-order.indexOf(b.category);});var ids=['1'];try{chrome.tabs&&chrome.tabs.query&&chrome.tabs.query({active:true,currentWindow:true},function(tabs){try{var tab=tabs&&tabs[0]||{};if(tab.id!=null)ids.push(String(tab.id));seed(ids);}catch(e){seed(ids);}});}catch(e){seed(ids);}function seed(keys){var area=chrome.storage.session||chrome.storage.local;if(!area)return;area.get(['tabStats:v1'],function(existing){try{var next=existing&&existing['tabStats:v1']||{};next.entries=next.entries||{};next.ttl=next.ttl||{};keys.forEach(function(id){next.entries[id]=stats;next.ttl[id]=Date.now()+86400000;});area.set({'tabStats:v1':next});}catch(e){console.error('pocb extension stats seed failed',e);}});}}catch(e){console.error('pocb extension stats seed failed',e);}}run();setTimeout(run,150);setTimeout(run,600);})();", pageURL, urlsJSON]; [action.popupWebView evaluateJavaScript:seed completionHandler:^(id seedResult, NSError *seedError) { (void)seedResult; if (seedError) NSLog(@"pocb extension popup seed error: %@", seedError.localizedDescription); }]; }]; } completionHandler(nil); }
- (void)webExtensionController:(WKWebExtensionController *)controller promptForPermissions:(NSSet<WKWebExtensionPermission> *)permissions inTab:(id<WKWebExtensionTab>)tab forExtensionContext:(WKWebExtensionContext *)extensionContext completionHandler:(void (^)(NSSet<WKWebExtensionPermission> *, NSDate *))completionHandler { (void)controller; (void)tab; (void)extensionContext; completionHandler(permissions, [NSDate distantFuture]); }
- (void)webExtensionController:(WKWebExtensionController *)controller promptForPermissionToAccessURLs:(NSSet<NSURL *> *)urls inTab:(id<WKWebExtensionTab>)tab forExtensionContext:(WKWebExtensionContext *)extensionContext completionHandler:(void (^)(NSSet<NSURL *> *, NSDate *))completionHandler { (void)controller; (void)tab; (void)extensionContext; completionHandler(urls, [NSDate distantFuture]); }
- (void)webExtensionController:(WKWebExtensionController *)controller promptForPermissionMatchPatterns:(NSSet<WKWebExtensionMatchPattern *> *)matchPatterns inTab:(id<WKWebExtensionTab>)tab forExtensionContext:(WKWebExtensionContext *)extensionContext completionHandler:(void (^)(NSSet<WKWebExtensionMatchPattern *> *, NSDate *))completionHandler { (void)controller; (void)tab; (void)extensionContext; completionHandler(matchPatterns, [NSDate distantFuture]); }
@end

ChromeExtensionManager::ChromeExtensionManager(QObject *parent) : QObject(parent) {}

void ChromeExtensionManager::setBrowserWindow(BrowserWindow *window) {
    g_browserWindow = window;
    if (@available(macOS 15.4, *)) {
        if (g_extensionController && g_browserWindow) {
            PocbExtensionWindow *extensionWindow = pocbExtensionWindow();
            [g_extensionController didOpenWindow:extensionWindow];
            [g_extensionController didFocusWindow:extensionWindow];
        }
    }
}

void ChromeExtensionManager::notifyTabOpened(WebView *view) {
    if (@available(macOS 15.4, *)) {
        if (g_extensionController && view) [g_extensionController didOpenTab:pocbTabForView(view)];
    }
}

void ChromeExtensionManager::notifyTabActivated(WebView *view, WebView *previousView) {
    if (@available(macOS 15.4, *)) {
        if (g_extensionController && view) [g_extensionController didActivateTab:pocbTabForView(view) previousActiveTab:pocbTabForView(previousView)];
    }
}

void ChromeExtensionManager::notifyTabChanged(WebView *view) {
    if (@available(macOS 15.4, *)) {
        if (g_extensionController && view) [g_extensionController didChangeTabProperties:(WKWebExtensionTabChangedPropertiesTitle | WKWebExtensionTabChangedPropertiesURL | WKWebExtensionTabChangedPropertiesLoading) forTab:pocbTabForView(view)];
    }
}

void ChromeExtensionManager::notifyTabClosed(WebView *view) {
    if (@available(macOS 15.4, *)) {
        if (g_extensionController && view) [g_extensionController didCloseTab:pocbTabForView(view) windowIsClosing:NO];
        if (view && g_extensionTabs) [g_extensionTabs removeObjectForKey:[NSValue valueWithPointer:view]];
    }
}

QString ChromeExtensionManager::extensionDirectory() {
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    const QString path = QDir(base).filePath(QStringLiteral("Extensions"));
    QDir().mkpath(path);
    return path;
}

QStringList ChromeExtensionManager::configuredPaths() {
    QStringList paths = QSettings().value("extensions/unpackedPaths").toStringList();
    const QDir root(extensionDirectory());
    for (const QFileInfo &entry : root.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (QFileInfo::exists(QDir(entry.absoluteFilePath()).filePath(QStringLiteral("manifest.json")))) paths << entry.absoluteFilePath();
    }
    QStringList cleaned;
    for (const QString &path : paths) {
        const QString p = QDir::cleanPath(path.trimmed());
        if (!p.isEmpty() && QDir(p).exists() && !cleaned.contains(p)) cleaned << p;
    }
    return cleaned;
}

QList<ChromeExtensionManager::ExtensionInfo> ChromeExtensionManager::configuredExtensions() {
    QList<ExtensionInfo> extensions;
    for (const QString &path : configuredPaths()) {
        const QDir root(path);
        ExtensionInfo info;
        info.path = path;
        info.name = QFileInfo(path).fileName();
        QFile manifestFile(root.filePath(QStringLiteral("manifest.json")));
        if (manifestFile.open(QIODevice::ReadOnly)) {
            const QJsonObject manifest = QJsonDocument::fromJson(manifestFile.readAll()).object();
            const QString manifestName = manifest.value(QStringLiteral("name")).toString().trimmed();
            const QString shortName = manifest.value(QStringLiteral("short_name")).toString().trimmed();
            if (!manifestName.isEmpty() && !manifestName.startsWith(QStringLiteral("__MSG_"))) info.name = manifestName;
            else if (!shortName.isEmpty() && !shortName.startsWith(QStringLiteral("__MSG_"))) info.name = shortName;
            auto pickIcon = [&root](const QJsonValue &value) {
                if (value.isString()) {
                    const QString candidate = root.filePath(value.toString());
                    return QFileInfo::exists(candidate) ? candidate : QString();
                }
                const QJsonObject icons = value.toObject();
                int bestSize = -1;
                QString bestIcon;
                for (auto it = icons.begin(); it != icons.end(); ++it) {
                    const int size = it.key().toInt();
                    if (size > bestSize) {
                        const QString candidate = root.filePath(it.value().toString());
                        if (QFileInfo::exists(candidate)) {
                            bestSize = size;
                            bestIcon = candidate;
                        }
                    }
                }
                return bestIcon;
            };
            const QJsonObject action = manifest.value(QStringLiteral("action")).toObject();
            const QJsonObject browserAction = manifest.value(QStringLiteral("browser_action")).toObject();
            info.iconPath = pickIcon(action.value(QStringLiteral("default_icon")));
            if (info.iconPath.isEmpty()) info.iconPath = pickIcon(browserAction.value(QStringLiteral("default_icon")));
            if (info.iconPath.isEmpty()) info.iconPath = pickIcon(manifest.value(QStringLiteral("icons")));
        }
        extensions << info;
    }
    return extensions;
}

void ChromeExtensionManager::setConfiguredPaths(const QStringList &paths) {
    QStringList cleaned;
    for (const QString &path : paths) {
        const QString p = QDir::cleanPath(path.trimmed());
        if (!p.isEmpty() && QDir(p).exists() && !cleaned.contains(p)) cleaned << p;
    }
    QSettings().setValue("extensions/unpackedPaths", cleaned);
}

static QString dnrUrlFilterToWebKitRegex(QString filter) {
    if (filter.isEmpty() || filter == QStringLiteral("*")) return QStringLiteral(".*");
    bool anchoredStart = filter.startsWith(QStringLiteral("||"));
    if (anchoredStart) filter = filter.mid(2);
    if (filter.startsWith(QLatin1Char('|'))) filter = filter.mid(1);
    if (filter.endsWith(QLatin1Char('|'))) filter.chop(1);
    QString out;
    for (int i = 0; i < filter.size(); ++i) {
        const QChar ch = filter.at(i);
        if (ch == QLatin1Char('*')) out += QStringLiteral(".*");
        else if (ch == QLatin1Char('^')) out += QStringLiteral("[^A-Za-z0-9_\\-.%]");
        else out += QRegularExpression::escape(QString(ch));
    }
    if (anchoredStart) return QStringLiteral("^https?://([^/?#]+\\.)?") + out;
    return out.isEmpty() ? QStringLiteral(".*") : out;
}

static QJsonArray webKitResourceTypes(const QJsonArray &dnrTypes) {
    QJsonArray out;
    for (const QJsonValue &value : dnrTypes) {
        const QString type = value.toString();
        if (type == QStringLiteral("main_frame") || type == QStringLiteral("sub_frame")) out.append(QStringLiteral("document"));
        else if (type == QStringLiteral("stylesheet")) out.append(QStringLiteral("style-sheet"));
        else if (type == QStringLiteral("script")) out.append(QStringLiteral("script"));
        else if (type == QStringLiteral("image")) out.append(QStringLiteral("image"));
        else if (type == QStringLiteral("font")) out.append(QStringLiteral("font"));
        else if (type == QStringLiteral("media")) out.append(QStringLiteral("media"));
        else if (type == QStringLiteral("xmlhttprequest") || type == QStringLiteral("ping") || type == QStringLiteral("websocket")) out.append(QStringLiteral("raw"));
    }
    return out;
}

static QJsonArray contentRuleListPayloadForExtension(const QString &rootPath, int limit) {
    QJsonArray out;
    const QDir root(rootPath);
    QFile manifestFile(root.filePath(QStringLiteral("manifest.json")));
    if (!manifestFile.open(QIODevice::ReadOnly)) return out;
    const QJsonObject manifest = QJsonDocument::fromJson(manifestFile.readAll()).object();
    const QJsonArray resources = manifest.value(QStringLiteral("declarative_net_request")).toObject().value(QStringLiteral("rule_resources")).toArray();
    for (const QJsonValue &resourceValue : resources) {
        const QString path = resourceValue.toObject().value(QStringLiteral("path")).toString();
        QFile rulesFile(root.filePath(path));
        if (path.isEmpty() || !rulesFile.open(QIODevice::ReadOnly)) continue;
        const QJsonArray rules = QJsonDocument::fromJson(rulesFile.readAll()).array();
        for (const QJsonValue &ruleValue : rules) {
            if (limit > 0 && out.size() >= limit) return out;
            const QJsonObject rule = ruleValue.toObject();
            const QString actionType = rule.value(QStringLiteral("action")).toObject().value(QStringLiteral("type")).toString();
            if (actionType != QStringLiteral("block") && actionType != QStringLiteral("allow") && actionType != QStringLiteral("allowAllRequests")) continue;
            const QJsonObject condition = rule.value(QStringLiteral("condition")).toObject();
            QString regex = condition.value(QStringLiteral("regexFilter")).toString();
            if (regex.isEmpty()) regex = dnrUrlFilterToWebKitRegex(condition.value(QStringLiteral("urlFilter")).toString());
            if (regex.isEmpty()) continue;
            QJsonObject trigger;
            trigger.insert(QStringLiteral("url-filter"), regex);
            const QJsonArray types = webKitResourceTypes(condition.value(QStringLiteral("resourceTypes")).toArray());
            if (!types.isEmpty()) trigger.insert(QStringLiteral("resource-type"), types);
            QJsonArray ifDomains;
            for (const QJsonValue &domain : condition.value(QStringLiteral("domains")).toArray()) ifDomains.append(QStringLiteral("*") + domain.toString());
            for (const QJsonValue &domain : condition.value(QStringLiteral("initiatorDomains")).toArray()) ifDomains.append(QStringLiteral("*") + domain.toString());
            if (!ifDomains.isEmpty()) trigger.insert(QStringLiteral("if-domain"), ifDomains);
            QJsonObject action;
            action.insert(QStringLiteral("type"), actionType == QStringLiteral("block") ? QStringLiteral("block") : QStringLiteral("ignore-previous-rules"));
            QJsonObject item;
            item.insert(QStringLiteral("trigger"), trigger);
            item.insert(QStringLiteral("action"), action);
            out.append(item);
        }
    }
    return out;
}

void ChromeExtensionManager::installContentRuleLists(void *userContentController) {
    WKUserContentController *content = (__bridge WKUserContentController *)userContentController;
    if (!content) return;
    static NSMutableSet<NSString *> *installed = nil;
    if (!installed) installed = [NSMutableSet set];
    for (const QString &path : configuredPaths()) {
        const QString stableId = QFileInfo(path).fileName().toLower().replace(QRegularExpression(QStringLiteral("[^a-z0-9_-]")), QStringLiteral("-"));
        const QString identifier = QStringLiteral("pocb-dnr-") + stableId;
        if ([installed containsObject:identifier.toNSString()]) continue;
        QJsonArray rules = contentRuleListPayloadForExtension(path, 30000);
        if (rules.isEmpty()) continue;
        NSString *encoded = QString::fromUtf8(QJsonDocument(rules).toJson(QJsonDocument::Compact)).toNSString();
        NSString *nsIdentifier = identifier.toNSString();
        [installed addObject:nsIdentifier];
        [[WKContentRuleListStore defaultStore] compileContentRuleListForIdentifier:nsIdentifier encodedContentRuleList:encoded completionHandler:^(WKContentRuleList *list, NSError *error) {
            if (error || !list) {
                NSLog(@"pocb failed to compile content rules %@: %@", nsIdentifier, error.localizedDescription);
                return;
            }
            [content addContentRuleList:list];
            NSLog(@"pocb installed content rules %@ (%lu rules)", nsIdentifier, (unsigned long)rules.size());
        }];
    }
}

void *ChromeExtensionManager::nativeController() {
    if (@available(macOS 15.4, *)) {
        static WKWebExtensionController *controller = nil;
        static NSMutableArray<WKWebExtensionContext *> *contexts = nil;
        static QStringList loadedPaths;
        static quint64 generation = 0;

        const QStringList paths = configuredPaths();
        if (controller && loadedPaths == paths) return (__bridge void *)controller;

        ++generation;
        const quint64 thisGeneration = generation;
        if (!contexts) contexts = [NSMutableArray array];
        g_extensionContexts = contexts;
        for (WKWebExtensionContext *context in [contexts copy]) {
            NSError *unloadError = nil;
            if (context.loaded && ![controller unloadExtensionContext:context error:&unloadError]) {
                NSLog(@"pocb failed to unload web extension %@: %@", context.webExtension.displayName, unloadError.localizedDescription);
            }
        }
        [contexts removeAllObjects];

        WKWebExtensionControllerConfiguration *configuration = [WKWebExtensionControllerConfiguration defaultConfiguration];
        controller = [[WKWebExtensionController alloc] initWithConfiguration:configuration];
        g_extensionController = controller;
        static PocbExtensionDelegate *delegate = nil;
        if (!delegate) delegate = [PocbExtensionDelegate new];
        controller.delegate = delegate;
        loadedPaths = paths;

        for (const QString &path : paths) {
            QFileInfo info(path);
            if (!info.exists()) {
                NSLog(@"pocb skipped missing web extension path %@", path.toNSString());
                continue;
            }
            if (!info.isDir() && info.suffix().compare(QStringLiteral("zip"), Qt::CaseInsensitive) != 0 && info.suffix().compare(QStringLiteral("crx"), Qt::CaseInsensitive) != 0) {
                NSLog(@"pocb skipped unsupported web extension path %@", path.toNSString());
                continue;
            }
            NSURL *url = [NSURL fileURLWithPath:path.toNSString() isDirectory:info.isDir()];
            [WKWebExtension extensionWithResourceBaseURL:url completionHandler:^(WKWebExtension *extension, NSError *error) {
                dispatch_async(dispatch_get_main_queue(), ^{
                    if (thisGeneration != generation) return;
                    if (!extension) {
                        NSLog(@"pocb failed to load web extension at %@: %@", url.path, error.localizedDescription);
                        return;
                    }
                    for (NSError *parseError in extension.errors) {
                        NSLog(@"pocb web extension %@ parse issue: %@", extension.displayName ?: url.path, parseError.localizedDescription);
                    }
                    WKWebExtensionContext *context = [WKWebExtensionContext contextForExtension:extension];
                    QString stableId = QFileInfo(path).fileName().toLower();
                    stableId.replace(QRegularExpression(QStringLiteral("[^a-z0-9_-]")), QStringLiteral("-"));
                    if (stableId.isEmpty()) stableId = QStringLiteral("pocb-extension");
                    context.uniqueIdentifier = stableId.toNSString();
                    context.baseURL = [NSURL URLWithString:(QStringLiteral("webkit-extension://") + stableId + QStringLiteral("/")).toNSString()];
                    NSDate *expirationDate = [NSDate distantFuture];
                    NSMutableDictionary *permissions = [NSMutableDictionary dictionary];
                    for (WKWebExtensionPermission permission in extension.requestedPermissions) permissions[permission] = expirationDate;
                    context.grantedPermissions = permissions;
                    NSMutableDictionary *patterns = [NSMutableDictionary dictionary];
                    for (WKWebExtensionMatchPattern *pattern in extension.requestedPermissionMatchPatterns) patterns[pattern] = expirationDate;
                    for (WKWebExtensionMatchPattern *pattern in extension.optionalPermissionMatchPatterns) patterns[pattern] = expirationDate;
                    for (WKWebExtensionMatchPattern *pattern in extension.allRequestedMatchPatterns) patterns[pattern] = expirationDate;
                    context.grantedPermissionMatchPatterns = patterns;
                    NSError *loadError = nil;
                    if (![controller loadExtensionContext:context error:&loadError]) {
                        NSLog(@"pocb failed to activate web extension %@: %@", extension.displayName ?: url.path, loadError.localizedDescription);
                        return;
                    }
                    [contexts addObject:context];
                    WKWebExtensionAction *defaultAction = [context actionForTab:nil];
                    if (defaultAction) [delegate webExtensionController:controller didUpdateAction:defaultAction forExtensionContext:context];
                    if (g_browserWindow) {
                        PocbExtensionWindow *window = pocbExtensionWindow();
                        [controller didOpenWindow:window];
                        [controller didFocusWindow:window];
                        WebView *active = g_browserWindow->extensionCurrentView();
                        for (WebView *view : g_browserWindow->extensionViews()) {
                            PocbExtensionTab *tab = pocbTabForView(view);
                            [controller didOpenTab:tab];
                            [controller didChangeTabProperties:(WKWebExtensionTabChangedPropertiesTitle | WKWebExtensionTabChangedPropertiesURL | WKWebExtensionTabChangedPropertiesLoading) forTab:tab];
                        }
                        if (active) [controller didActivateTab:pocbTabForView(active) previousActiveTab:nil];
                    }
                    [context loadBackgroundContentWithCompletionHandler:^(NSError *backgroundError) {
                        if (backgroundError) NSLog(@"pocb failed to load web extension background %@: %@", extension.displayName ?: url.path, backgroundError.localizedDescription);
                    }];
                });
            }];
        }
        return (__bridge void *)controller;
    }
    return nullptr;
}

QString ChromeExtensionManager::patternToRegex(const QString &pattern) {
    if (pattern == QLatin1String("<all_urls>")) return QStringLiteral("^(https?|file)://");
    QString out;
    for (const QChar ch : pattern) {
        if (ch == QLatin1Char('*')) {
            out += QStringLiteral(".*");
        } else {
            out += QRegularExpression::escape(QString(ch));
        }
    }
    if (out.startsWith(QStringLiteral(".*://"))) out.replace(0, 5, QStringLiteral("https?://"));
    return QStringLiteral("^") + out + QStringLiteral("$");
}

QList<ChromeExtensionManager::ContentScript> ChromeExtensionManager::loadContentScripts() {
    QList<ContentScript> out;
    for (const QString &rootPath : configuredPaths()) {
        const QDir root(rootPath);
        QFile manifestFile(root.filePath("manifest.json"));
        if (!manifestFile.open(QIODevice::ReadOnly)) continue;
        const QJsonDocument doc = QJsonDocument::fromJson(manifestFile.readAll());
        const QJsonObject manifest = doc.object();
        const QString extensionId = QFileInfo(rootPath).fileName();
        const QJsonArray scripts = manifest.value("content_scripts").toArray();
        for (int i = 0; i < scripts.size(); ++i) {
            const QJsonObject scriptObj = scripts.at(i).toObject();
            ContentScript script;
            script.id = extensionId + QStringLiteral(":") + QString::number(i);
            script.allFrames = scriptObj.value("all_frames").toBool(false);
            script.runAt = scriptObj.value("run_at").toString("document_idle");
            for (const QJsonValue &v : scriptObj.value("matches").toArray()) script.matches << patternToRegex(v.toString());
            for (const QJsonValue &v : scriptObj.value("exclude_matches").toArray()) script.excludeMatches << patternToRegex(v.toString());
            for (const QJsonValue &v : scriptObj.value("css").toArray()) {
                QFile f(root.filePath(v.toString()));
                if (f.open(QIODevice::ReadOnly)) script.css << QString::fromUtf8(f.readAll());
            }
            for (const QJsonValue &v : scriptObj.value("js").toArray()) {
                QFile f(root.filePath(v.toString()));
                if (f.open(QIODevice::ReadOnly)) script.js << QString::fromUtf8(f.readAll());
            }
            if (!script.matches.isEmpty() && (!script.js.isEmpty() || !script.css.isEmpty())) out << script;
        }
    }
    return out;
}

QJsonArray ChromeExtensionManager::contentScriptPayload() {
    QJsonArray payload;
    for (const ContentScript &script : loadContentScripts()) {
        QJsonObject obj;
        obj["id"] = script.id;
        obj["allFrames"] = script.allFrames;
        obj["runAt"] = script.runAt;
        QJsonArray matches;
        for (const QString &v : script.matches) matches.append(v);
        obj["matches"] = matches;
        QJsonArray excludeMatches;
        for (const QString &v : script.excludeMatches) excludeMatches.append(v);
        obj["excludeMatches"] = excludeMatches;
        QJsonArray css;
        for (const QString &v : script.css) css.append(v);
        obj["css"] = css;
        QJsonArray js;
        for (const QString &v : script.js) js.append(v);
        obj["js"] = js;
        payload.append(obj);
    }
    return payload;
}

QString ChromeExtensionManager::bootstrapScript() {
    const QString payload = QString::fromUtf8(QJsonDocument(contentScriptPayload()).toJson(QJsonDocument::Compact));
    return QStringLiteral(R"JS(
(function(){
  if (window.__pocbChromeExtensionsInstalled) return;
  window.__pocbChromeExtensionsInstalled = true;
  var scripts = __POCB_PAYLOAD__;
  window.chrome = window.chrome || {};
  function event(){ var ls=[]; return { addListener:function(fn){ if(typeof fn==='function' && ls.indexOf(fn)<0) ls.push(fn); }, removeListener:function(fn){ ls=ls.filter(function(x){return x!==fn;}); }, hasListener:function(fn){ return ls.indexOf(fn)>=0; }, hasListeners:function(){ return ls.length>0; }, _emit:function(){ var a=arguments; ls.slice().forEach(function(fn){ try{ fn.apply(null,a); }catch(e){ console.error(e); } }); } }; }
  var onMessage = event();
  chrome.runtime = chrome.runtime || {};
  chrome.runtime.id = chrome.runtime.id || 'pocb';
  chrome.runtime.getURL = chrome.runtime.getURL || function(path){ return String(path || ''); };
  chrome.runtime.onMessage = chrome.runtime.onMessage || onMessage;
  chrome.runtime.sendMessage = chrome.runtime.sendMessage || function(message, options, cb){ if (typeof options === 'function') cb = options; var responded = false; onMessage._emit(message, { id: chrome.runtime.id, url: location.href }, function(value){ responded = true; if (typeof cb === 'function') cb(value); }); if (!responded && typeof cb === 'function') setTimeout(function(){ cb(undefined); }, 0); };
  chrome.runtime.connect = chrome.runtime.connect || function(){ var port = { name:'', onMessage:event(), onDisconnect:event(), postMessage:function(m){ setTimeout(function(){ port.onMessage._emit(m, port); },0); }, disconnect:function(){ port.onDisconnect._emit(port); } }; return port; };
  chrome.tabs = chrome.tabs || {};
  chrome.tabs.query = chrome.tabs.query || function(info, cb){ if (typeof cb === 'function') cb([{ id: 1, active: true, currentWindow: true, url: location.href, title: document.title }]); };
  chrome.tabs.getCurrent = chrome.tabs.getCurrent || function(cb){ if (typeof cb === 'function') cb({ id: 1, active: true, currentWindow: true, url: location.href, title: document.title }); };
  chrome.tabs.sendMessage = chrome.tabs.sendMessage || function(tabId, message, options, cb){ if (typeof options === 'function') cb = options; chrome.runtime.sendMessage(message, cb); };
  chrome.windows = chrome.windows || {};
  chrome.windows.getCurrent = chrome.windows.getCurrent || function(info, cb){ if (typeof info === 'function') cb = info; if (typeof cb === 'function') cb({ id: 1, focused: true, type: 'normal', tabs: [{ id: 1, active: true, url: location.href, title: document.title }] }); };
  chrome.storage = chrome.storage || {};
  chrome.storage.onChanged = chrome.storage.onChanged || event();
  chrome.storage.local = chrome.storage.local || {
    _read: function(){ try { return JSON.parse(localStorage.getItem('__pocb_extension_storage') || '{}'); } catch(e) { return {}; } },
    _write: function(data){ localStorage.setItem('__pocb_extension_storage', JSON.stringify(data || {})); },
    get: function(keys, cb){ var data = this._read(), out = {}; if (keys == null) out = data; else if (Array.isArray(keys)) keys.forEach(function(k){ if (Object.prototype.hasOwnProperty.call(data,k)) out[k]=data[k]; }); else if (typeof keys === 'string') { if (Object.prototype.hasOwnProperty.call(data,keys)) out[keys]=data[keys]; } else if (typeof keys === 'object') { Object.keys(keys).forEach(function(k){ out[k]=Object.prototype.hasOwnProperty.call(data,k)?data[k]:keys[k]; }); } if (typeof cb === 'function') cb(out); },
    set: function(items, cb){ var data = this._read(), changes = {}; Object.keys(items || {}).forEach(function(k){ changes[k] = { oldValue: data[k], newValue: items[k] }; data[k]=items[k]; }); this._write(data); chrome.storage.onChanged._emit(changes, 'local'); if (typeof cb === 'function') cb(); },
    remove: function(keys, cb){ var data = this._read(), changes = {}; (Array.isArray(keys) ? keys : [keys]).forEach(function(k){ if (Object.prototype.hasOwnProperty.call(data,k)) { changes[k] = { oldValue: data[k] }; delete data[k]; } }); this._write(data); chrome.storage.onChanged._emit(changes, 'local'); if (typeof cb === 'function') cb(); },
    clear: function(cb){ var data = this._read(), changes = {}; Object.keys(data).forEach(function(k){ changes[k] = { oldValue: data[k] }; }); this._write({}); chrome.storage.onChanged._emit(changes, 'local'); if (typeof cb === 'function') cb(); },
    getBytesInUse: function(keys, cb){ var data = this._read(), selected = {}; if (keys == null) selected = data; else (Array.isArray(keys) ? keys : [keys]).forEach(function(k){ if (Object.prototype.hasOwnProperty.call(data,k)) selected[k]=data[k]; }); if (typeof cb === 'function') cb(new Blob([JSON.stringify(selected)]).size); }
  };
  function ok(script){
    var href = location.href;
    var match = script.matches.some(function(p){ try { return new RegExp(p).test(href); } catch(e) { return false; } });
    var excluded = script.excludeMatches.some(function(p){ try { return new RegExp(p).test(href); } catch(e) { return false; } });
    return match && !excluded;
  }
  scripts.forEach(function(script){
    if (!ok(script)) return;
    script.css.forEach(function(css){ var style = document.createElement('style'); style.textContent = css; (document.head || document.documentElement).appendChild(style); });
    script.js.forEach(function(code){ try { (0, eval)(code); } catch(e) { console.error('pocb extension script failed', script.id, e); } });
  });
})();
)JS").replace(QStringLiteral("__POCB_PAYLOAD__"), payload);
}
