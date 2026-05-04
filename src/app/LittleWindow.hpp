#pragma once

#include "ProfileStore.hpp"
#include "Theme.hpp"

#include <QMainWindow>
#include <QPoint>
#include <QUrl>

class QCloseEvent;
class QEvent;
class QMouseEvent;

class AddressBarController;
class QLabel;
class QLineEdit;
class QProgressBar;
class QToolButton;
class WebView;

class LittleWindow final : public QMainWindow {
    Q_OBJECT
public:
    explicit LittleWindow(const QUrl &url, QWidget *parent = nullptr, bool restorePreviousAppOnClose = false);

protected:
    void showEvent(QShowEvent *e) override;
    void closeEvent(QCloseEvent *e) override;
    bool eventFilter(QObject *obj, QEvent *event) override;

private:
    QUrl urlFromInput(const QString &input) const;
    void setupUi(const QUrl &url);
    void updateChrome();
    void focusLocation();

    Theme m_theme;
    ProfileStore m_profiles;
    QString m_searchEngine = "https://search.brave.com/search?q=%1";
    WebView *m_view = nullptr;
    QLineEdit *m_addressBar = nullptr;
    QLabel *m_lockIcon = nullptr;
    QLabel *m_searchIcon = nullptr;
    QToolButton *m_backBtn = nullptr;
    QToolButton *m_fwdBtn = nullptr;
    QToolButton *m_reloadBtn = nullptr;
    QProgressBar *m_progress = nullptr;
    AddressBarController *m_addressBarCtl = nullptr;
    QWidget *m_toolbarDragHandle = nullptr;
    bool m_toolbarDragging = false;
    QPoint m_toolbarDragOffset;
    bool m_restorePreviousAppOnClose = false;
};
