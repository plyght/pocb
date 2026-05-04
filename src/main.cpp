#include "BrowserWindow.hpp"
#include "GlobalHotkeys.hpp"
#include "LittleWindow.hpp"
#include "MacIntegration.hpp"

#include <QApplication>
#include <QEvent>
#include <QFileOpenEvent>
#include <QSettings>
#include <QUrl>

class PocbApplication final : public QApplication {
public:
    PocbApplication(int &argc, char **argv) : QApplication(argc, argv) {}

    bool event(QEvent *event) override {
        if (event->type() == QEvent::FileOpen) {
            auto *openEvent = static_cast<QFileOpenEvent *>(event);
            if (openEvent->url().isValid()) {
                openUrl(openEvent->url());
                return true;
            }
        }
        return QApplication::event(event);
    }

    void openUrl(const QUrl &url) {
        if (QSettings().value("browser/externalLinksInLittleWindow", true).toBool()) {
            mac::setAccessoryActivationPolicy();
            auto *window = new LittleWindow(url, nullptr, true);
            window->setAttribute(Qt::WA_DeleteOnClose);
            window->show();
            mac::showWindowWithoutAppActivation(window);
        } else {
            mac::setRegularActivationPolicy();
            auto *window = new BrowserWindow();
            window->setAttribute(Qt::WA_DeleteOnClose);
            window->show();
            window->extensionCreateTab(url, false);
            window->raise();
            window->activateWindow();
        }
    }
};

int main(int argc, char *argv[]) {
    PocbApplication app(argc, argv);
    QApplication::setApplicationName("pocb");
    QApplication::setOrganizationName("plyght");
    QApplication::setApplicationDisplayName("pocb");
    mac::installForegroundApplicationTracker();
    app.setQuitOnLastWindowClosed(false);

    mac::installNewLittleWindowHotkey([] {
        mac::setRegularActivationPolicy();
        auto *window = new LittleWindow(QUrl("about:blank"));
        window->setAttribute(Qt::WA_DeleteOnClose);
        window->show();
        window->raise();
        window->activateWindow();
    });

    const QStringList args = app.arguments();
    QUrl startupUrl;
    for (int i = 1; i < args.size(); ++i) {
        const QUrl url = QUrl::fromUserInput(args.at(i));
        if (url.isValid() && !url.scheme().isEmpty()) {
            startupUrl = url;
            break;
        }
    }

    if (startupUrl.isValid()) {
        if (QSettings().value("browser/externalLinksInLittleWindow", true).toBool()) mac::setAccessoryActivationPolicy();
        app.openUrl(startupUrl);
    } else {
        mac::setRegularActivationPolicy();
        BrowserWindow window;
        window.show();
        return app.exec();
    }

    return app.exec();
}
