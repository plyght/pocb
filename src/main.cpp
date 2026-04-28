#include "BrowserWindow.hpp"

#include <QApplication>

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName("pocb");
    QApplication::setOrganizationName("plyght");
    QApplication::setApplicationDisplayName("pocb");

    BrowserWindow window;
    window.show();

    return app.exec();
}
