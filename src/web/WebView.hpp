#pragma once

#include <QColor>
#include <QPixmap>
#include <QUrl>
#include <QWidget>

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

    // Internal: install an externally-created WKWebView (used by the
    // WKUIDelegate's createWebViewWithConfiguration: path so popups stay
    // wired to their opener's session). Takes ownership of the NSView.
    void adoptNativeWebView(void *wkWebView);

    // Last colour reported by sniffTopColor() (or invalid if never sniffed).
    QColor cachedThemeColor() const;
    // Re-runs the sniff JS and (asynchronously) emits themeColorChanged.
    void sniffTopColor();

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

protected:
    void resizeEvent(QResizeEvent *e) override;
    void showEvent(QShowEvent *e) override;

private:
    struct Impl;
    Impl *m_impl;
};
