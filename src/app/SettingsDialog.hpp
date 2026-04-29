#pragma once

#include <QDialog>

class QCheckBox;
class QComboBox;
class QLineEdit;
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

private:
    void refreshProfiles();

    ProfileStore &m_profiles;
    QComboBox *m_profileBox = nullptr;
    QLineEdit *m_newProfile = nullptr;
    QLineEdit *m_homePage = nullptr;
    QLineEdit *m_searchEngine = nullptr;
    QCheckBox *m_showFullUrl = nullptr;
};
