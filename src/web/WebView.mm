#include "WebView.hpp"

#include "WebKitProfile.hpp"
#include "ChromeExtensionManager.hpp"

#import <AppKit/AppKit.h>
#import <WebKit/WKWebView.h>
#import <WebKit/WKWebViewConfiguration.h>

#include <QPointer>
#include <QRegularExpression>
#import <WebKit/WKWebsiteDataStore.h>
#import <WebKit/WKNavigationDelegate.h>
#import <WebKit/WKUIDelegate.h>
#import <WebKit/WKNavigationAction.h>
#import <WebKit/WKWindowFeatures.h>
#import <WebKit/WKPreferences.h>
#import <WebKit/WKWebExtensionController.h>
#import <WebKit/WKUserContentController.h>
#import <WebKit/WKUserScript.h>

static void disableWebKit60FpsCap(WKPreferences *preferences) {
    if (!preferences) return;

    Class prefsClass = NSClassFromString(@"WKPreferences");
    SEL featuresSelector = NSSelectorFromString(@"_features");
    if (!prefsClass || ![prefsClass respondsToSelector:featuresSelector]) return;

    NSArray *features = nil;
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Warc-performSelector-leaks"
    features = [prefsClass performSelector:featuresSelector];
#pragma clang diagnostic pop
    if (![features isKindOfClass:[NSArray class]]) return;

    SEL keySelector = NSSelectorFromString(@"key");
    SEL setEnabledSelector = NSSelectorFromString(@"_setEnabled:forFeature:");
    if (![preferences respondsToSelector:setEnabledSelector]) return;

    for (id feature in features) {
        if (![feature respondsToSelector:keySelector]) continue;
        NSString *key = nil;
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Warc-performSelector-leaks"
        key = [feature performSelector:keySelector];
#pragma clang diagnostic pop
        if (![key isKindOfClass:[NSString class]]) continue;
        if (![key isEqualToString:@"PreferPageRenderingUpdatesNear60FPSEnabled"]) continue;

        NSMethodSignature *signature = [preferences methodSignatureForSelector:setEnabledSelector];
        if (!signature) return;
        NSInvocation *invocation = [NSInvocation invocationWithMethodSignature:signature];
        BOOL enabled = NO;
        invocation.target = preferences;
        invocation.selector = setEnabledSelector;
        id featureArgument = feature;
        [invocation setArgument:&enabled atIndex:2];
        [invocation setArgument:&featureArgument atIndex:3];
        [invocation invoke];
        return;
    }
}

// Forward decl of helper that does the QWidget -> NSView bridge.
static NSView *qtNSView(QWidget *w) {
    if (!w) return nil;
    w->winId();
    return (__bridge NSView *)reinterpret_cast<void *>(w->winId());
}

@class PocbWKBridge;

struct WebView::Impl {
    WKWebView *wk = nil;
    PocbWKBridge *bridge = nil;
    NSView *observedHost = nil;
    NSArray<NSLayoutConstraint *> *edgeConstraints = nil;
    QColor cachedTopColor;
};

// Objective-C bridge: forwards WKNavigationDelegate / WKUIDelegate /
// KVO callbacks into the C++ WebView so they become Qt signals.
@interface PocbWKBridge : NSObject <WKNavigationDelegate, WKUIDelegate>
@property(nonatomic, assign) WebView *owner;
- (void)attachKVO:(WKWebView *)wk;
- (void)detachKVO:(WKWebView *)wk;
- (void)hostFrameDidChange:(NSNotification *)note;
- (void)contentMouseDown:(NSGestureRecognizer *)sender;
@end

@implementation PocbWKBridge

- (void)attachKVO:(WKWebView *)wk {
    [wk addObserver:self forKeyPath:@"URL" options:NSKeyValueObservingOptionNew context:nullptr];
    [wk addObserver:self forKeyPath:@"title" options:NSKeyValueObservingOptionNew context:nullptr];
    [wk addObserver:self forKeyPath:@"estimatedProgress" options:NSKeyValueObservingOptionNew context:nullptr];
}

- (void)detachKVO:(WKWebView *)wk {
    @try { [wk removeObserver:self forKeyPath:@"URL"]; } @catch (...) {}
    @try { [wk removeObserver:self forKeyPath:@"title"]; } @catch (...) {}
    @try { [wk removeObserver:self forKeyPath:@"estimatedProgress"]; } @catch (...) {}
}

- (void)contentMouseDown:(NSGestureRecognizer *)sender {
    (void)sender;
    if (self.owner) emit self.owner->contentMouseDown();
}

- (void)observeValueForKeyPath:(NSString *)keyPath
                      ofObject:(id)object
                        change:(NSDictionary<NSKeyValueChangeKey, id> *)change
                       context:(void *)context {
    (void)object; (void)change; (void)context;
    if (!self.owner) return;
    WebView *o = self.owner;
    if ([keyPath isEqualToString:@"URL"]) {
        emit o->urlChanged(o->url());
    } else if ([keyPath isEqualToString:@"title"]) {
        emit o->titleChanged(o->title());
    } else if ([keyPath isEqualToString:@"estimatedProgress"]) {
        WKWebView *wk = (WKWebView *)object;
        emit o->loadProgress((int)(wk.estimatedProgress * 100.0));
    }
}

#pragma mark - WKNavigationDelegate

- (void)webView:(WKWebView *)wk didFinishNavigation:(WKNavigation *)nav {
    (void)wk; (void)nav;
    WebView *o = self.owner;
    if (!o) return;
    emit o->loadFinished(true);
    o->sniffTopColor();
}

- (void)webView:(WKWebView *)wk didFailNavigation:(WKNavigation *)nav withError:(NSError *)err {
    (void)wk; (void)nav; (void)err;
    if (self.owner) emit self.owner->loadFinished(false);
}

- (void)webView:(WKWebView *)wk didFailProvisionalNavigation:(WKNavigation *)nav withError:(NSError *)err {
    (void)wk; (void)nav; (void)err;
    if (self.owner) emit self.owner->loadFinished(false);
}

#pragma mark - WKUIDelegate

- (WKWebView *)webView:(WKWebView *)wk
   createWebViewWithConfiguration:(WKWebViewConfiguration *)configuration
              forNavigationAction:(WKNavigationAction *)navigationAction
                   windowFeatures:(WKWindowFeatures *)windowFeatures {
    (void)wk; (void)windowFeatures;
    if (!self.owner) return nil;

    disableWebKit60FpsCap(configuration.preferences);
    WKWebView *child = [[WKWebView alloc] initWithFrame:NSZeroRect configuration:configuration];

    // Wrap into a new Qt-side WebView and hand it to the parent so it can
    // place it in the tab tree.
    WebView *childView = new WebView(nullptr, nullptr);
    childView->adoptNativeWebView((__bridge_retained void *)child);

    const bool background =
        (navigationAction.modifierFlags & NSEventModifierFlagCommand) != 0;
    emit self.owner->newTabRequested(childView, background);

    if (navigationAction.targetFrame == nil && navigationAction.request.URL) {
        [child loadRequest:navigationAction.request];
    }
    return child;
}

- (void)webViewDidClose:(WKWebView *)wk {
    (void)wk;
    if (self.owner) emit self.owner->closeRequested();
}

- (void)hostFrameDidChange:(NSNotification *)note {
    NSView *host = (NSView *)note.object;
    if (!host || !self.owner) return;
    // Resync the WKWebView frame whenever the Qt host NSView's frame
    // changes. NSView autoresize only handles size deltas from addSubview
    // time, so on the very first show of a tab — when the host has been
    // resized while hidden — the WK content can be drawn at a stale (smaller)
    // size, leaving a gray L-shape along the top/left until the next manual
    // resize. Subscribing to NSViewFrameDidChangeNotification on the host
    // closes that gap.
    if (host.inLiveResize) return;
    for (NSView *sub in host.subviews) {
        if ([sub isKindOfClass:[WKWebView class]]) {
            [host setNeedsLayout:YES];
            [host layoutSubtreeIfNeeded];
            if (!NSEqualRects(sub.frame, host.bounds)) sub.frame = host.bounds;
        }
    }
}

@end

WebView::WebView(WebKitProfile *profile, QWidget *parent)
    : QWidget(parent), m_impl(new Impl) {
    setAttribute(Qt::WA_NativeWindow);
    // Intentionally DO promote ancestors to native widgets. With
    // WA_DontCreateNativeAncestors set, this widget's NSView gets reparented
    // to the top-level NSWindow content view and Qt manually translates its
    // frame into window coordinates on every layout pass. On first show
    // that translation can lag the Qt layout for a frame, leaving the
    // WKWebView inset relative to its logical parent — which exposes the
    // WebContainer's #1A1A1A background as a gray L along the top/left
    // until the user resizes the window. Letting the ancestors be native
    // makes the WKWebView a true subview of the stack's NSView, so the
    // macOS autoresize chain (not Qt's translation) drives geometry.
    winId();

    if (profile) {
        WKWebViewConfiguration *cfg = [[WKWebViewConfiguration alloc] init];
        disableWebKit60FpsCap(cfg.preferences);
        if (profile->dataStore()) {
            cfg.websiteDataStore = (__bridge WKWebsiteDataStore *)profile->dataStore();
        }
        if (@available(macOS 15.4, *)) {
            if (void *controller = ChromeExtensionManager::nativeController()) {
                cfg.webExtensionController = (__bridge WKWebExtensionController *)controller;
            }
        } else {
            const QString extensionBootstrap = ChromeExtensionManager::bootstrapScript();
            if (!extensionBootstrap.trimmed().isEmpty()) {
                WKUserContentController *content = [[WKUserContentController alloc] init];
                WKUserScript *script = [[WKUserScript alloc] initWithSource:extensionBootstrap.toNSString()
                                                              injectionTime:WKUserScriptInjectionTimeAtDocumentStart
                                                           forMainFrameOnly:NO];
                [content addUserScript:script];
                cfg.userContentController = content;
            }
        }
        WKWebView *wk = [[WKWebView alloc] initWithFrame:NSZeroRect configuration:cfg];
        if (@available(macOS 13.3, *)) {
            wk.inspectable = YES;
        }
        adoptNativeWebView((__bridge_retained void *)wk);
    }
}

WebView::~WebView() {
    if (m_impl->bridge) m_impl->bridge.owner = nullptr;
    if (m_impl->observedHost) {
        [[NSNotificationCenter defaultCenter] removeObserver:m_impl->bridge
                                                        name:NSViewFrameDidChangeNotification
                                                      object:m_impl->observedHost];
        m_impl->observedHost = nil;
    }
    if (m_impl->wk) {
        [m_impl->bridge detachKVO:m_impl->wk];
        m_impl->wk.navigationDelegate = nil;
        m_impl->wk.UIDelegate = nil;
        if (m_impl->edgeConstraints) {
            [NSLayoutConstraint deactivateConstraints:m_impl->edgeConstraints];
            m_impl->edgeConstraints = nil;
        }
        [m_impl->wk removeFromSuperview];
        m_impl->wk = nil;
    }
    m_impl->bridge = nil;
    delete m_impl;
}

void WebView::adoptNativeWebView(void *wkWebViewPtr) {
    WKWebView *wk = (__bridge_transfer WKWebView *)wkWebViewPtr;
    if (m_impl->wk) {
        [m_impl->bridge detachKVO:m_impl->wk];
        if (m_impl->edgeConstraints) {
            [NSLayoutConstraint deactivateConstraints:m_impl->edgeConstraints];
            m_impl->edgeConstraints = nil;
        }
        [m_impl->wk removeFromSuperview];
    }
    disableWebKit60FpsCap(wk.configuration.preferences);
    m_impl->wk = wk;
    if (!m_impl->bridge) {
        m_impl->bridge = [[PocbWKBridge alloc] init];
        m_impl->bridge.owner = this;
    }
    wk.navigationDelegate = m_impl->bridge;
    wk.UIDelegate = m_impl->bridge;
    NSClickGestureRecognizer *click = [[NSClickGestureRecognizer alloc] initWithTarget:m_impl->bridge action:@selector(contentMouseDown:)];
    click.delaysPrimaryMouseButtonEvents = NO;
    [wk addGestureRecognizer:click];
    [m_impl->bridge attachKVO:wk];

    NSView *host = qtNSView(this);
    if (host) {
        host.autoresizesSubviews = YES;
        wk.frame = host.bounds;
        wk.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
        wk.translatesAutoresizingMaskIntoConstraints = NO;
        [host addSubview:wk];
        m_impl->edgeConstraints = @[
            [wk.leadingAnchor constraintEqualToAnchor:host.leadingAnchor],
            [wk.trailingAnchor constraintEqualToAnchor:host.trailingAnchor],
            [wk.topAnchor constraintEqualToAnchor:host.topAnchor],
            [wk.bottomAnchor constraintEqualToAnchor:host.bottomAnchor]
        ];
        [NSLayoutConstraint activateConstraints:m_impl->edgeConstraints];
        [host layoutSubtreeIfNeeded];

        if (m_impl->observedHost && m_impl->observedHost != host) {
            [[NSNotificationCenter defaultCenter] removeObserver:m_impl->bridge
                                                            name:NSViewFrameDidChangeNotification
                                                          object:m_impl->observedHost];
            m_impl->observedHost = nil;
        }
        if (!m_impl->observedHost) {
            host.postsFrameChangedNotifications = YES;
            [[NSNotificationCenter defaultCenter] addObserver:m_impl->bridge
                                                     selector:@selector(hostFrameDidChange:)
                                                         name:NSViewFrameDidChangeNotification
                                                       object:host];
            m_impl->observedHost = host;
        }
    }
}

void WebView::load(const QUrl &url) {
    if (!m_impl->wk) return;
    NSString *str = url.toString().toNSString();
    NSURL *nsurl = [NSURL URLWithString:str];
    if (!nsurl) return;
    [m_impl->wk loadRequest:[NSURLRequest requestWithURL:nsurl]];
}

void WebView::loadHtml(const QString &html) {
    if (!m_impl->wk) return;
    [m_impl->wk loadHTMLString:html.toNSString() baseURL:nil];
}

void WebView::back()    { if (m_impl->wk) [m_impl->wk goBack]; }
void WebView::forward() { if (m_impl->wk) [m_impl->wk goForward]; }
void WebView::reload()  { if (m_impl->wk) [m_impl->wk reload]; }

QUrl WebView::url() const {
    if (!m_impl->wk || !m_impl->wk.URL) return QUrl();
    return QUrl(QString::fromNSString(m_impl->wk.URL.absoluteString));
}

QString WebView::title() const {
    if (!m_impl->wk || !m_impl->wk.title) return QString();
    return QString::fromNSString(m_impl->wk.title);
}

void *WebView::nativeWebView() const {
    return (__bridge void *)m_impl->wk;
}

QColor WebView::cachedThemeColor() const {
    return m_impl->cachedTopColor;
}

void WebView::sniffTopColor() {
    if (!m_impl->wk) return;
    // Walk DOM ancestors starting at the element under the very top of the
    // viewport (top-center pixel). The first opaque background we hit is
    // the chrome colour to use — this matches what the user sees at the
    // page's top edge, even when <body> itself is transparent or the page
    // uses a fixed header.
    static NSString *const kSniff =
        @"(function(){"
        @"  function ok(c){ if(!c) return false; c=c.trim(); if(!c||c==='transparent') return false;"
        @"    var m=c.match(/rgba?\\(([^)]+)\\)/); if(m){ var p=m[1].split(','); if(p.length>=4 && parseFloat(p[3])<0.05) return false; }"
        @"    return true; }"
        @"  try {"
        @"    var w = window.innerWidth || document.documentElement.clientWidth;"
        @"    var x = Math.max(1, Math.floor(w/2));"
        @"    var el = document.elementFromPoint(x, 1) || document.elementFromPoint(x, 0);"
        @"    var cur = el;"
        @"    while (cur) {"
        @"      try { var bg = getComputedStyle(cur).backgroundColor; if (ok(bg)) return bg; } catch(e){}"
        @"      cur = cur.parentElement;"
        @"    }"
        @"    var html = document.documentElement;"
        @"    if (html) { var hc = getComputedStyle(html).backgroundColor; if (ok(hc)) return hc; }"
        @"    var body = document.body;"
        @"    if (body) { var bc = getComputedStyle(body).backgroundColor; if (ok(bc)) return bc; }"
        @"    var dark = matchMedia && matchMedia('(prefers-color-scheme: dark)').matches;"
        @"    var metas = document.querySelectorAll('meta[name=\"theme-color\"]');"
        @"    for (var i=0; i<metas.length; ++i) {"
        @"      var mm = metas[i]; var media = mm.getAttribute('media') || '';"
        @"      if (!media) continue;"
        @"      if (dark && /dark/i.test(media)) return mm.content;"
        @"      if (!dark && /light/i.test(media)) return mm.content;"
        @"    }"
        @"    if (metas.length) return metas[0].content;"
        @"  } catch(e) {}"
        @"  return '';"
        @"})()";
    QPointer<WebView> guard(this);
    [m_impl->wk evaluateJavaScript:kSniff completionHandler:^(id result, NSError *error) {
        (void)error;
        WebView *self = guard.data();
        if (!self || !self->m_impl || !self->m_impl->wk) return;
        NSString *s = [result isKindOfClass:[NSString class]] ? (NSString *)result : nil;
        QColor color;
        if (s && s.length > 0) {
            QString qs = QString::fromNSString(s).trimmed();
            color = QColor::fromString(qs);
            if (!color.isValid()) {
                static const QRegularExpression re(
                    "rgba?\\(\\s*(\\d+)\\s*,\\s*(\\d+)\\s*,\\s*(\\d+)\\s*(?:,\\s*([0-9.]+)\\s*)?\\)");
                QRegularExpressionMatch m = re.match(qs);
                if (m.hasMatch()) {
                    color.setRgb(m.captured(1).toInt(), m.captured(2).toInt(), m.captured(3).toInt());
                    if (!m.captured(4).isEmpty()) color.setAlphaF(m.captured(4).toDouble());
                }
            }
        }
        self->m_impl->cachedTopColor = color;
        emit self->themeColorChanged(color);
    }];
}

void WebView::resizeEvent(QResizeEvent *e) {
    QWidget::resizeEvent(e);
    if (m_impl->wk) {
        NSView *host = qtNSView(this);
        if (host && !host.inLiveResize) {
            [host setNeedsLayout:YES];
            [host layoutSubtreeIfNeeded];
        }
    }
}

void WebView::showEvent(QShowEvent *e) {
    QWidget::showEvent(e);
    if (m_impl->wk) {
        NSView *host = qtNSView(this);
        if (host) {
            host.autoresizesSubviews = YES;
            if (m_impl->wk.superview != host) {
                if (m_impl->edgeConstraints) {
                    [NSLayoutConstraint deactivateConstraints:m_impl->edgeConstraints];
                    m_impl->edgeConstraints = nil;
                }
                m_impl->wk.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
                m_impl->wk.translatesAutoresizingMaskIntoConstraints = NO;
                [host addSubview:m_impl->wk];
                m_impl->edgeConstraints = @[
                    [m_impl->wk.leadingAnchor constraintEqualToAnchor:host.leadingAnchor],
                    [m_impl->wk.trailingAnchor constraintEqualToAnchor:host.trailingAnchor],
                    [m_impl->wk.topAnchor constraintEqualToAnchor:host.topAnchor],
                    [m_impl->wk.bottomAnchor constraintEqualToAnchor:host.bottomAnchor]
                ];
                [NSLayoutConstraint activateConstraints:m_impl->edgeConstraints];
            }
            // Always resync the frame on show: when a tab is first activated
            // the host QWidget may have been resized while hidden, and the
            // NSView autoresize chain doesn't always pick that up — leaving
            // a gray strip along the top/left until the next resize.
            [host setNeedsLayout:YES];
            [host layoutSubtreeIfNeeded];
            if (!NSEqualRects(m_impl->wk.frame, host.bounds)) m_impl->wk.frame = host.bounds;
            [m_impl->wk setNeedsDisplay:YES];

            if (m_impl->observedHost != host) {
                if (m_impl->observedHost) {
                    [[NSNotificationCenter defaultCenter] removeObserver:m_impl->bridge
                                                                    name:NSViewFrameDidChangeNotification
                                                                  object:m_impl->observedHost];
                }
                host.postsFrameChangedNotifications = YES;
                [[NSNotificationCenter defaultCenter] addObserver:m_impl->bridge
                                                         selector:@selector(hostFrameDidChange:)
                                                             name:NSViewFrameDidChangeNotification
                                                           object:host];
                m_impl->observedHost = host;
            }
        }
    }
}
