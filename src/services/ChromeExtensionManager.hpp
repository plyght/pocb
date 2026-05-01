#pragma once

#include <QJsonArray>
#include <QObject>
#include <QString>
#include <QStringList>

class BrowserWindow;
class WebView;

class ChromeExtensionManager final : public QObject {
    Q_OBJECT
public:
    struct ContentScript {
        QString id;
        QStringList matches;
        QStringList excludeMatches;
        QStringList js;
        QStringList css;
        bool allFrames = false;
        QString runAt;
    };

    struct ExtensionInfo {
        QString path;
        QString name;
        QString iconPath;
    };

    explicit ChromeExtensionManager(QObject *parent = nullptr);

    static QString extensionDirectory();
    static QStringList configuredPaths();
    static QList<ExtensionInfo> configuredExtensions();
    static void setConfiguredPaths(const QStringList &paths);
    static QJsonArray contentScriptPayload();
    static QString bootstrapScript();
    static void installContentRuleLists(void *userContentController);
    static void setBrowserWindow(BrowserWindow *window);
    static void notifyTabOpened(WebView *view);
    static void notifyTabActivated(WebView *view, WebView *previousView = nullptr);
    static void notifyTabChanged(WebView *view);
    static void notifyTabClosed(WebView *view);
    // Returns a `WKWebExtensionController *` on macOS 15.4+ after starting
    // asynchronous loading for configured unpacked extensions.
    static void *nativeController();

private:
    static QList<ContentScript> loadContentScripts();
    static QString patternToRegex(const QString &pattern);
};
