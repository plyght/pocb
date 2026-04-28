#include "SettingsDialog.hpp"

#include "ProfileStore.hpp"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

SettingsDialog::SettingsDialog(ProfileStore &profiles, QWidget *parent) : QDialog(parent), m_profiles(profiles) {
    setWindowTitle("pocb settings");
    setModal(true);
    resize(520, 260);

    auto *layout = new QVBoxLayout(this);
    auto *form = new QFormLayout();

    m_profileBox = new QComboBox(this);
    m_newProfile = new QLineEdit(this);
    m_newProfile->setPlaceholderText("new profile name");
    auto *profileRow = new QWidget(this);
    auto *profileLayout = new QHBoxLayout(profileRow);
    profileLayout->setContentsMargins(0, 0, 0, 0);
    auto *addProfile = new QPushButton("Create", profileRow);
    profileLayout->addWidget(m_profileBox, 1);
    profileLayout->addWidget(m_newProfile, 1);
    profileLayout->addWidget(addProfile);

    m_homePage = new QLineEdit("https://search.brave.com", this);
    m_searchEngine = new QLineEdit("https://search.brave.com/search?q=%1", this);

    form->addRow("Profile", profileRow);
    form->addRow("Home page", m_homePage);
    form->addRow("Search URL", m_searchEngine);
    layout->addWidget(new QLabel("Search URL must contain %1 where the query goes.", this));
    layout->addLayout(form);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    layout->addWidget(buttons);

    refreshProfiles();

    connect(addProfile, &QPushButton::clicked, this, [this] {
        m_profiles.createProfile(m_newProfile->text());
        m_profiles.setCurrentProfile(m_newProfile->text());
        m_newProfile->clear();
        refreshProfiles();
    });
    connect(m_profileBox, &QComboBox::currentTextChanged, &m_profiles, &ProfileStore::setCurrentProfile);
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        emit homePageChanged(m_homePage->text());
        emit searchEngineChanged(m_searchEngine->text());
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void SettingsDialog::refreshProfiles() {
    m_profileBox->blockSignals(true);
    m_profileBox->clear();
    m_profileBox->addItems(m_profiles.profiles());
    m_profileBox->setCurrentText(m_profiles.currentName());
    m_profileBox->blockSignals(false);
}
