#include "ChromeExtensionManager.hpp"

#include "BrowserWindow.hpp"
#include "WebView.hpp"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSettings>

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

static BrowserWindow *g_browserWindow = nullptr;

@interface PocbExtensionTab : NSObject <WKWebExtensionTab>
@property(nonatomic, assign) WebView *view;
@end

@interface PocbExtensionWindow : NSObject <WKWebExtensionWindow>
@end

@interface PocbExtensionDelegate : NSObject <WKWebExtensionControllerDelegate>
@end

@implementation PocbExtensionTab
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
- (void)closeForWebExtensionContext:(WKWebExtensionContext *)context completionHandler:(void (^)(NSError *))completionHandler { (void)context; if (g_browserWindow) g_browserWindow->extensionCloseView(self.view); completionHandler(nil); }
@end

@implementation PocbExtensionWindow
- (NSArray<id<WKWebExtensionTab>> *)tabsForWebExtensionContext:(WKWebExtensionContext *)context { (void)context; NSMutableArray *tabs = [NSMutableArray array]; if (!g_browserWindow) return tabs; for (WebView *view : g_browserWindow->extensionViews()) { PocbExtensionTab *tab = [PocbExtensionTab new]; tab.view = view; [tabs addObject:tab]; } return tabs; }
- (id<WKWebExtensionTab>)activeTabForWebExtensionContext:(WKWebExtensionContext *)context { (void)context; if (!g_browserWindow || !g_browserWindow->extensionCurrentView()) return nil; PocbExtensionTab *tab = [PocbExtensionTab new]; tab.view = g_browserWindow->extensionCurrentView(); return tab; }
- (WKWebExtensionWindowType)windowTypeForWebExtensionContext:(WKWebExtensionContext *)context { (void)context; return WKWebExtensionWindowTypeNormal; }
- (WKWebExtensionWindowState)windowStateForWebExtensionContext:(WKWebExtensionContext *)context { (void)context; return g_browserWindow && g_browserWindow->isFullScreen() ? WKWebExtensionWindowStateFullscreen : WKWebExtensionWindowStateNormal; }
- (CGRect)frameForWebExtensionContext:(WKWebExtensionContext *)context { (void)context; return g_browserWindow ? CGRectMake(g_browserWindow->x(), g_browserWindow->y(), g_browserWindow->width(), g_browserWindow->height()) : CGRectZero; }
- (BOOL)isPrivateForWebExtensionContext:(WKWebExtensionContext *)context { (void)context; return NO; }
- (void)focusForWebExtensionContext:(WKWebExtensionContext *)context completionHandler:(void (^)(NSError *))completionHandler { (void)context; if (g_browserWindow) { g_browserWindow->show(); g_browserWindow->raise(); g_browserWindow->activateWindow(); } completionHandler(nil); }
@end

@implementation PocbExtensionDelegate
- (NSArray<id<WKWebExtensionWindow>> *)webExtensionController:(WKWebExtensionController *)controller openWindowsForExtensionContext:(WKWebExtensionContext *)extensionContext { (void)controller; (void)extensionContext; return g_browserWindow ? @[ [PocbExtensionWindow new] ] : @[]; }
- (id<WKWebExtensionWindow>)webExtensionController:(WKWebExtensionController *)controller focusedWindowForExtensionContext:(WKWebExtensionContext *)extensionContext { (void)controller; (void)extensionContext; return g_browserWindow ? [PocbExtensionWindow new] : nil; }
- (void)webExtensionController:(WKWebExtensionController *)controller openNewTabUsingConfiguration:(WKWebExtensionTabConfiguration *)configuration forExtensionContext:(WKWebExtensionContext *)extensionContext completionHandler:(void (^)(id<WKWebExtensionTab>, NSError *))completionHandler { (void)controller; (void)extensionContext; NSURL *url = [configuration respondsToSelector:@selector(URL)] ? [configuration valueForKey:@"URL"] : nil; BOOL shouldActivate = YES; @try { id active = [configuration valueForKey:@"shouldActivate"]; if ([active respondsToSelector:@selector(boolValue)]) shouldActivate = [active boolValue]; } @catch (...) {} WebView *view = g_browserWindow ? g_browserWindow->extensionCreateTab(url ? QUrl(QString::fromNSString(url.absoluteString)) : QUrl(), !shouldActivate) : nullptr; if (!view) { completionHandler(nil, [NSError errorWithDomain:@"pocb.extensions" code:1 userInfo:@{NSLocalizedDescriptionKey:@"No browser window is available"}]); return; } PocbExtensionTab *tab = [PocbExtensionTab new]; tab.view = view; completionHandler(tab, nil); }
- (void)webExtensionController:(WKWebExtensionController *)controller openNewWindowUsingConfiguration:(WKWebExtensionWindowConfiguration *)configuration forExtensionContext:(WKWebExtensionContext *)extensionContext completionHandler:(void (^)(id<WKWebExtensionWindow>, NSError *))completionHandler { (void)controller; (void)configuration; (void)extensionContext; if (!g_browserWindow) { completionHandler(nil, [NSError errorWithDomain:@"pocb.extensions" code:2 userInfo:@{NSLocalizedDescriptionKey:@"No browser window is available"}]); return; } g_browserWindow->extensionCreateTab(QUrl(), false); completionHandler([PocbExtensionWindow new], nil); }
- (void)webExtensionController:(WKWebExtensionController *)controller openOptionsPageForExtensionContext:(WKWebExtensionContext *)extensionContext completionHandler:(void (^)(NSError *))completionHandler { (void)controller; if (!g_browserWindow || !extensionContext.optionsPageURL) { completionHandler([NSError errorWithDomain:@"pocb.extensions" code:3 userInfo:@{NSLocalizedDescriptionKey:@"No options page is available"}]); return; } g_browserWindow->extensionCreateTab(QUrl(QString::fromNSString(extensionContext.optionsPageURL.absoluteString)), false); completionHandler(nil); }
- (void)webExtensionController:(WKWebExtensionController *)controller didUpdateAction:(WKWebExtensionAction *)action forExtensionContext:(WKWebExtensionContext *)context { (void)controller; if (!g_browserWindow) return; QString key = QString::fromNSString(context.webExtension.displayName ?: context.webExtension.version ?: @"extension"); QString label = QString::fromNSString(action.label.length ? action.label : (context.webExtension.displayName ?: @"Extension")); g_browserWindow->extensionSetAction(key, label, [action] { if (action.presentsPopup && action.popupPopover) { NSWindow *window = g_browserWindow ? (__bridge NSWindow *)reinterpret_cast<void *>(g_browserWindow->winId()) : nil; NSView *view = window.contentView; [action.popupPopover showRelativeToRect:NSMakeRect(NSMidX(view.bounds), NSMaxY(view.bounds) - 44, 1, 1) ofView:view preferredEdge:NSMinYEdge]; } }); }
- (void)webExtensionController:(WKWebExtensionController *)controller presentPopupForAction:(WKWebExtensionAction *)action forExtensionContext:(WKWebExtensionContext *)context completionHandler:(void (^)(NSError *))completionHandler { (void)controller; (void)context; if (!g_browserWindow || !action.presentsPopup || !action.popupPopover) { completionHandler([NSError errorWithDomain:@"pocb.extensions" code:4 userInfo:@{NSLocalizedDescriptionKey:@"No popup is available"}]); return; } NSWindow *window = (__bridge NSWindow *)reinterpret_cast<void *>(g_browserWindow->winId()); NSView *view = window.contentView; [action.popupPopover showRelativeToRect:NSMakeRect(NSMidX(view.bounds), NSMaxY(view.bounds) - 44, 1, 1) ofView:view preferredEdge:NSMinYEdge]; completionHandler(nil); }
- (void)webExtensionController:(WKWebExtensionController *)controller promptForPermissions:(NSSet<WKWebExtensionPermission> *)permissions inTab:(id<WKWebExtensionTab>)tab forExtensionContext:(WKWebExtensionContext *)extensionContext completionHandler:(void (^)(NSSet<WKWebExtensionPermission> *, NSDate *))completionHandler { (void)controller; (void)tab; (void)extensionContext; completionHandler(permissions, [NSDate distantFuture]); }
- (void)webExtensionController:(WKWebExtensionController *)controller promptForPermissionToAccessURLs:(NSSet<NSURL *> *)urls inTab:(id<WKWebExtensionTab>)tab forExtensionContext:(WKWebExtensionContext *)extensionContext completionHandler:(void (^)(NSSet<NSURL *> *, NSDate *))completionHandler { (void)controller; (void)tab; (void)extensionContext; completionHandler(urls, [NSDate distantFuture]); }
- (void)webExtensionController:(WKWebExtensionController *)controller promptForPermissionMatchPatterns:(NSSet<WKWebExtensionMatchPattern *> *)matchPatterns inTab:(id<WKWebExtensionTab>)tab forExtensionContext:(WKWebExtensionContext *)extensionContext completionHandler:(void (^)(NSSet<WKWebExtensionMatchPattern *> *, NSDate *))completionHandler { (void)controller; (void)tab; (void)extensionContext; completionHandler(matchPatterns, [NSDate distantFuture]); }
@end

ChromeExtensionManager::ChromeExtensionManager(QObject *parent) : QObject(parent) {}

void ChromeExtensionManager::setBrowserWindow(BrowserWindow *window) {
    g_browserWindow = window;
}

QStringList ChromeExtensionManager::configuredPaths() {
    return QSettings().value("extensions/unpackedPaths").toStringList();
}

void ChromeExtensionManager::setConfiguredPaths(const QStringList &paths) {
    QStringList cleaned;
    for (const QString &path : paths) {
        const QString p = QDir::cleanPath(path.trimmed());
        if (!p.isEmpty() && QDir(p).exists() && !cleaned.contains(p)) cleaned << p;
    }
    QSettings().setValue("extensions/unpackedPaths", cleaned);
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
        for (WKWebExtensionContext *context in [contexts copy]) {
            NSError *unloadError = nil;
            if (context.loaded && ![controller unloadExtensionContext:context error:&unloadError]) {
                NSLog(@"pocb failed to unload web extension %@: %@", context.webExtension.displayName, unloadError.localizedDescription);
            }
        }
        [contexts removeAllObjects];

        WKWebExtensionControllerConfiguration *configuration = [WKWebExtensionControllerConfiguration defaultConfiguration];
        controller = [[WKWebExtensionController alloc] initWithConfiguration:configuration];
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
                    NSDate *expirationDate = [NSDate distantFuture];
                    NSMutableDictionary *permissions = [NSMutableDictionary dictionary];
                    for (WKWebExtensionPermission permission in extension.requestedPermissions) permissions[permission] = expirationDate;
                    context.grantedPermissions = permissions;
                    NSMutableDictionary *patterns = [NSMutableDictionary dictionary];
                    for (WKWebExtensionMatchPattern *pattern in extension.allRequestedMatchPatterns) patterns[pattern] = expirationDate;
                    context.grantedPermissionMatchPatterns = patterns;
                    NSError *loadError = nil;
                    if (![controller loadExtensionContext:context error:&loadError]) {
                        NSLog(@"pocb failed to activate web extension %@: %@", extension.displayName ?: url.path, loadError.localizedDescription);
                        return;
                    }
                    [contexts addObject:context];
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
