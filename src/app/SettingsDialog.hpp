#pragma once

#include <QDialog>

#include "Theme.hpp"

class QCheckBox;
class QComboBox;
class QKeyEvent;
class QLineEdit;
class QPaintEvent;
class QShowEvent;
class ProfileStore;

class SettingsDialog final : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(ProfileStore &profiles, QWidget *parent = nullptr);

signals:
    void homePageChanged(const QString &url);
    void searchEngineChanged(const QString &url);
    // True = always show full URL in the address bar; false = show a tidy
    // domain-first form when not editing.
    void showFullUrlChanged(bool enabled);

protected:
    void keyPressEvent(QKeyEvent *e) override;
    void showEvent(QShowEvent *e) override;

private:
    void refreshProfiles();

    Theme m_theme;

    ProfileStore &m_profiles;
    QComboBox *m_profileBox = nullptr;
    QLineEdit *m_newProfile = nullptr;
    QLineEdit *m_homePage = nullptr;
    QLineEdit *m_searchEngine = nullptr;
    QLineEdit *m_extensionPaths = nullptr;
    QCheckBox *m_showFullUrl = nullptr;
    QCheckBox *m_addrInSidebar = nullptr;
};
