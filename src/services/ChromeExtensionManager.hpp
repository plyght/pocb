#pragma once

#include <QJsonArray>
#include <QObject>
#include <QString>
#include <QStringList>

class BrowserWindow;

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

    explicit ChromeExtensionManager(QObject *parent = nullptr);

    static QStringList configuredPaths();
    static void setConfiguredPaths(const QStringList &paths);
    static QJsonArray contentScriptPayload();
    static QString bootstrapScript();
    static void setBrowserWindow(BrowserWindow *window);
    // Returns a `WKWebExtensionController *` on macOS 15.4+ after starting
    // asynchronous loading for configured unpacked extensions.
    static void *nativeController();

private:
    static QList<ContentScript> loadContentScripts();
    static QString patternToRegex(const QString &pattern);
};
