#include "WebView.hpp"

#include "WebKitProfile.hpp"

#import <AppKit/AppKit.h>
#import <WebKit/WKWebView.h>
#import <WebKit/WKWebViewConfiguration.h>

#include <QRegularExpression>
#import <WebKit/WKWebsiteDataStore.h>
#import <WebKit/WKNavigationDelegate.h>
#import <WebKit/WKUIDelegate.h>
#import <WebKit/WKNavigationAction.h>
#import <WebKit/WKWindowFeatures.h>
#import <WebKit/WKPreferences.h>

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
};

// Objective-C bridge: forwards WKNavigationDelegate / WKUIDelegate /
// KVO callbacks into the C++ WebView so they become Qt signals.
@interface PocbWKBridge : NSObject <WKNavigationDelegate, WKUIDelegate>
@property(nonatomic, assign) WebView *owner;
- (void)attachKVO:(WKWebView *)wk;
- (void)detachKVO:(WKWebView *)wk;
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
    (void)nav;
    WebView *o = self.owner;
    if (!o) return;
    emit o->loadFinished(true);

    // Sniff the page's preferred chrome colour (theme-color meta first,
    // then the computed body background). One short async JS call per
    // navigation — cheap.
    // Try, in order:
    //   1. <meta name="theme-color"> (with media-query awareness for dark mode)
    //   2. computed background of <html>
    //   3. computed background of <body>
    //   4. background of any large fixed-position banner near the top
    // Skip transparent / fully-clear values so we always emit something
    // useful when the page actually has a colour.
    static NSString *const kSniff =
        @"(function(){"
        @"  function ok(c){ if(!c) return false; c=c.trim(); if(!c) return false; "
        @"    if(c==='transparent') return false; "
        @"    var m=c.match(/rgba?\\(([^)]+)\\)/); if(m){ var p=m[1].split(','); if(p.length>=4 && parseFloat(p[3])<0.05) return false; } "
        @"    return true; }"
        @"  try {"
        @"    var dark = matchMedia && matchMedia('(prefers-color-scheme: dark)').matches;"
        @"    var metas = document.querySelectorAll('meta[name=\"theme-color\"]');"
        @"    var fallbackMeta = null;"
        @"    for (var i=0; i<metas.length; ++i) {"
        @"      var mm = metas[i]; var media = mm.getAttribute('media') || '';"
        @"      if (!media) { fallbackMeta = mm; continue; }"
        @"      if (dark && /dark/i.test(media)) return mm.content;"
        @"      if (!dark && /light/i.test(media)) return mm.content;"
        @"    }"
        @"    if (fallbackMeta && ok(fallbackMeta.content)) return fallbackMeta.content;"
        @"    var html = document.documentElement;"
        @"    if (html) { var hc = getComputedStyle(html).backgroundColor; if (ok(hc)) return hc; }"
        @"    var body = document.body;"
        @"    if (body) { var bc = getComputedStyle(body).backgroundColor; if (ok(bc)) return bc; }"
        @"  } catch(e) {}"
        @"  return '';"
        @"})()";
    [wk evaluateJavaScript:kSniff completionHandler:^(id result, NSError *error) {
        if (!o) return;
        if (error && !result) return;
        NSString *s = [result isKindOfClass:[NSString class]] ? (NSString *)result : nil;
        QColor color;
        if (s && s.length > 0) {
            QString qs = QString::fromNSString(s).trimmed();
            // QColor parses CSS named colours, "#rrggbb", "#rgb", and most
            // "rgb(r,g,b)"/"rgba(r,g,b,a)" forms via setNamedColor.
            color.setNamedColor(qs);
            if (!color.isValid()) {
                // Fallback: parse "rgb(r, g, b[, a])" by hand.
                static const QRegularExpression re(
                    "rgba?\\(\\s*(\\d+)\\s*,\\s*(\\d+)\\s*,\\s*(\\d+)\\s*(?:,\\s*([0-9.]+)\\s*)?\\)");
                QRegularExpressionMatch m = re.match(qs);
                if (m.hasMatch()) {
                    color.setRgb(m.captured(1).toInt(), m.captured(2).toInt(), m.captured(3).toInt());
                    if (!m.captured(4).isEmpty()) color.setAlphaF(m.captured(4).toDouble());
                }
            }
        }
        emit o->themeColorChanged(color);
    }];
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

@end

WebView::WebView(WebKitProfile *profile, QWidget *parent)
    : QWidget(parent), m_impl(new Impl) {
    setAttribute(Qt::WA_NativeWindow);
    setAttribute(Qt::WA_DontCreateNativeAncestors);
    winId();

    if (profile) {
        WKWebViewConfiguration *cfg = [[WKWebViewConfiguration alloc] init];
        if (profile->dataStore()) {
            cfg.websiteDataStore = (__bridge WKWebsiteDataStore *)profile->dataStore();
        }
        WKWebView *wk = [[WKWebView alloc] initWithFrame:NSZeroRect configuration:cfg];
        if (@available(macOS 13.3, *)) {
            wk.inspectable = YES;
        }
        adoptNativeWebView((__bridge_retained void *)wk);
    }
}

WebView::~WebView() {
    if (m_impl->wk) {
        [m_impl->bridge detachKVO:m_impl->wk];
        m_impl->wk.navigationDelegate = nil;
        m_impl->wk.UIDelegate = nil;
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
        [m_impl->wk removeFromSuperview];
    }
    m_impl->wk = wk;
    if (!m_impl->bridge) {
        m_impl->bridge = [[PocbWKBridge alloc] init];
        m_impl->bridge.owner = this;
    }
    wk.navigationDelegate = m_impl->bridge;
    wk.UIDelegate = m_impl->bridge;
    [m_impl->bridge attachKVO:wk];

    NSView *host = qtNSView(this);
    if (host) {
        wk.frame = host.bounds;
        wk.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
        [host addSubview:wk];
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

void WebView::resizeEvent(QResizeEvent *e) {
    QWidget::resizeEvent(e);
    if (m_impl->wk) {
        NSView *host = qtNSView(this);
        if (host) m_impl->wk.frame = host.bounds;
    }
}

void WebView::showEvent(QShowEvent *e) {
    QWidget::showEvent(e);
    if (m_impl->wk) {
        NSView *host = qtNSView(this);
        if (host && m_impl->wk.superview != host) {
            m_impl->wk.frame = host.bounds;
            m_impl->wk.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
            [host addSubview:m_impl->wk];
        }
    }
}
