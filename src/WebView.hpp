#pragma once

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
    QUrl url() const;
    QString title() const;

    // Internal: install an externally-created WKWebView (used by the
    // WKUIDelegate's createWebViewWithConfiguration: path so popups stay
    // wired to their opener's session). Takes ownership of the NSView.
    void adoptNativeWebView(void *wkWebView);

signals:
    void urlChanged(const QUrl &url);
    void titleChanged(const QString &title);
    void loadProgress(int progress);
    void loadFinished(bool ok);
    void newTabRequested(WebView *child, bool background);
    void closeRequested();

protected:
    void resizeEvent(QResizeEvent *e) override;
    void showEvent(QShowEvent *e) override;

private:
    struct Impl;
    Impl *m_impl;
};
