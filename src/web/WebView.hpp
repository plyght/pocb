#pragma once

#include <QColor>
#include <QPixmap>
#include <QUrl>
#include <QVariant>
#include <QWidget>

#include <functional>

class WebKitProfile;

// Thin QWidget wrapper around a native WKWebView. Mirrors the bits of
// QWebEngineView's API that BrowserWindow uses.
class WebView final : public QWidget {
    Q_OBJECT
public:
    explicit WebView(WebKitProfile *profile, QWidget *parent = nullptr);
    ~WebView() override;

    void load(const QUrl &url);
    void loadHtml(const QString &html);
    void back();
    void forward();
    void reload();
    void stop();
    void closePage();
    void setPageColorScheme(const QString &scheme);
    bool canGoBack() const;
    bool canGoForward() const;
    bool isLoading() const;
    QUrl url() const;
    QString title() const;
    QPixmap snapshot(const QSize &size) const;
    void *nativeWebView() const;
    void setObscuredTopInset(double inset);
    void setCornerRadius(double radius);

    // Internal: install an externally-created WKWebView (used by the
    // WKUIDelegate's createWebViewWithConfiguration: path so popups stay
    // wired to their opener's session). Takes ownership of the NSView.
    void adoptNativeWebView(void *wkWebView);

    // Last colour reported by sniffTopColor() (or invalid if never sniffed).
    QColor cachedThemeColor() const;
    // Re-runs the sniff JS and (asynchronously) emits themeColorChanged.
    void sniffTopColor();

    // Runs JS in the page's main frame. Fire-and-forget when `done` is empty.
    void runJavaScript(const QString &script, std::function<void(const QVariant &)> done = {});

    // Registers a WKUserScript (injected at document start, all frames) that
    // is added to every WKWebView configuration created from now on. Pages
    // may post back via window.webkit.messageHandlers.pocb.postMessage({name, body})
    // and the owning WebView emits scriptMessage(name, body).
    static void registerUserScript(const QString &source, bool mainFrameOnly = false);

    // Called with the raw WKWebView* every time one is adopted by a WebView.
    // Lets native integrations (downloads, passwords, gestures) attach
    // themselves without editing this file.
    static void addNativeWebViewHook(std::function<void(void *wkWebView, WebView *owner)> hook);

signals:
    void urlChanged(const QUrl &url);
    void titleChanged(const QString &title);
    void loadProgress(int progress);
    void loadFinished(bool ok);
    void navigationStateChanged();
    void newTabRequested(WebView *child, bool background);
    void closeRequested();
    // Emitted after a navigation finishes, with the page's preferred chrome
    // colour. Invalid QColor when the page exposes nothing useful.
    void themeColorChanged(const QColor &color);
    void contentMouseDown();
    // Message posted from an injected user script via the `pocb` handler.
    void scriptMessage(const QString &name, const QVariant &body);
    // A navigation turned into a WKDownload*. Receivers own the delegate
    // wiring; the pointer is the raw (unretained) WKDownload.
    void downloadStarted(void *wkDownload);

protected:
    void resizeEvent(QResizeEvent *e) override;
    void showEvent(QShowEvent *e) override;

private:
    struct Impl;
    Impl *m_impl;
};
