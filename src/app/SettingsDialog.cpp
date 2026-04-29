#include "SettingsDialog.hpp"

#include "MacIntegration.hpp"
#include "ChromeExtensionManager.hpp"
#include "ProfileStore.hpp"
#include "Theme.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QShowEvent>
#include <QToolButton>
#include <QVBoxLayout>

namespace {

constexpr int kPanelRadius = 14;

// Small-caps muted section heading, matches the editorial tone of the
// chrome (sidebar group headers, omnibox labels).
QLabel *makeSectionHeader(const QString &text, const Theme &theme, QWidget *parent) {
    auto *l = new QLabel(text.toUpper(), parent);
    l->setStyleSheet(QString(
        "QLabel {"
        "  color: %1;"
        "  font-family: '%2';"
        "  font-size: %3px;"
        "  font-weight: 600;"
        "  letter-spacing: 1.2px;"
        "  padding: 0px 2px;"
        "}")
        .arg(theme.muted.name(),
             theme.fontFamily,
             QString::number(theme.smallSize)));
    return l;
}

// Rounded panel "card" that groups a section's controls. Background +
// hairline border match the topbar/sidebar treatment used elsewhere.
QFrame *makeCard(const Theme &theme, QWidget *parent) {
    auto *card = new QFrame(parent);
    card->setObjectName("SettingsCard");
    QColor bg = theme.panel;
    bg.setAlphaF(0.45f);
    QColor border = theme.borderSoft;
    border.setAlphaF(0.65f);
    card->setStyleSheet(QString(
        "QFrame#SettingsCard {"
        "  background: rgba(%1,%2,%3,%4);"
        "  border: 1px solid rgba(%5,%6,%7,%8);"
        "  border-radius: 12px;"
        "}")
        .arg(bg.red()).arg(bg.green()).arg(bg.blue()).arg(bg.alpha())
        .arg(border.red()).arg(border.green()).arg(border.blue()).arg(border.alpha()));
    return card;
}

QLabel *makeHelp(const QString &text, const Theme &theme, QWidget *parent) {
    auto *l = new QLabel(text, parent);
    l->setWordWrap(true);
    l->setStyleSheet(QString(
        "QLabel { color: %1; font-size: %2px; }")
        .arg(theme.muted.name(),
             QString::number(theme.smallSize)));
    return l;
}

}  // namespace

SettingsDialog::SettingsDialog(ProfileStore &profiles, QWidget *parent) : QDialog(parent), m_profiles(profiles) {
    setWindowTitle("Settings");
    setModal(true);
    setMinimumWidth(560);
    resize(620, 460);

    // Let the behind-window NSVisualEffectView blur show through instead of
    // the global stylesheet's opaque QDialog background.
    setAttribute(Qt::WA_TranslucentBackground);
    setStyleSheet("QDialog { background: transparent; }");

    const Theme theme;
    m_theme = theme;

    auto *root = new QVBoxLayout(this);
    // Top inset reserves space for the (now transparent) macOS titlebar so
    // our content doesn't slide under the traffic lights.
    root->setContentsMargins(24, 36, 24, 20);
    root->setSpacing(16);

    // ---- Profile card -----------------------------------------------------
    root->addWidget(makeSectionHeader("Profile", theme, this));
    auto *profileCard = makeCard(theme, this);
    auto *profileCol = new QVBoxLayout(profileCard);
    profileCol->setContentsMargins(16, 14, 16, 14);
    profileCol->setSpacing(10);

    auto *profileForm = new QFormLayout();
    profileForm->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    profileForm->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
    profileForm->setHorizontalSpacing(14);
    profileForm->setVerticalSpacing(10);

    m_profileBox = new QComboBox(profileCard);
    profileForm->addRow("Active", m_profileBox);

    auto *createRow = new QWidget(profileCard);
    auto *createLayout = new QHBoxLayout(createRow);
    createLayout->setContentsMargins(0, 0, 0, 0);
    createLayout->setSpacing(8);
    m_newProfile = new QLineEdit(createRow);
    m_newProfile->setPlaceholderText("New profile name");
    auto *addProfile = new QPushButton("Create", createRow);
    addProfile->setIcon(mac::sfSymbolIcon("plus", 12.0, theme.foreground));
    addProfile->setIconSize(QSize(13, 13));
    addProfile->setCursor(Qt::PointingHandCursor);
    createLayout->addWidget(m_newProfile, 1);
    createLayout->addWidget(addProfile);
    profileForm->addRow("Add", createRow);

    profileCol->addLayout(profileForm);
    root->addWidget(profileCard);

    // ---- Browsing card ----------------------------------------------------
    root->addWidget(makeSectionHeader("Browsing", theme, this));
    auto *browseCard = makeCard(theme, this);
    auto *browseCol = new QVBoxLayout(browseCard);
    browseCol->setContentsMargins(16, 14, 16, 14);
    browseCol->setSpacing(10);

    auto *browseForm = new QFormLayout();
    browseForm->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    browseForm->setHorizontalSpacing(14);
    browseForm->setVerticalSpacing(10);

    m_homePage = new QLineEdit("https://search.brave.com", browseCard);
    m_searchEngine = new QLineEdit("https://search.brave.com/search?q=%1", browseCard);

    browseForm->addRow("Home page", m_homePage);
    browseForm->addRow("Search URL", m_searchEngine);

    m_showFullUrl = new QCheckBox("Always show the full URL", browseCard);
    m_showFullUrl->setChecked(QSettings().value("ui/showFullUrl", false).toBool());
    browseForm->addRow("Address bar", m_showFullUrl);

    m_addrInSidebar = new QCheckBox("Move address bar into the sidebar (restart required)", browseCard);
    m_addrInSidebar->setChecked(QSettings().value("ui/addressBarInSidebar", false).toBool());
    browseForm->addRow("", m_addrInSidebar);

    browseCol->addLayout(browseForm);
    browseCol->addWidget(makeHelp(
        "The search URL must contain %1 where the query goes.",
        theme, browseCard));
    root->addWidget(browseCard);

    root->addWidget(makeSectionHeader("Extensions", theme, this));
    auto *extensionsCard = makeCard(theme, this);
    auto *extensionsCol = new QVBoxLayout(extensionsCard);
    extensionsCol->setContentsMargins(16, 14, 16, 14);
    extensionsCol->setSpacing(10);

    auto *extensionRow = new QWidget(extensionsCard);
    auto *extensionLayout = new QHBoxLayout(extensionRow);
    extensionLayout->setContentsMargins(0, 0, 0, 0);
    extensionLayout->setSpacing(8);
    m_extensionPaths = new QLineEdit(ChromeExtensionManager::configuredPaths().join(";"), extensionRow);
    m_extensionPaths->setPlaceholderText("/path/to/unpacked-extension;/path/to/another-extension");
    auto *addExtension = new QPushButton("Add folder", extensionRow);
    addExtension->setIcon(mac::sfSymbolIcon("folder", 12.0, theme.foreground));
    addExtension->setIconSize(QSize(13, 13));
    addExtension->setCursor(Qt::PointingHandCursor);
    extensionLayout->addWidget(m_extensionPaths, 1);
    extensionLayout->addWidget(addExtension);
    extensionsCol->addWidget(extensionRow);
    extensionsCol->addWidget(makeHelp(
        "Loads local WebExtensions through WebKit on macOS 15.4+ and injects a compatibility runtime for older systems. Unpacked folders work best; background workers, native messaging, request blocking, and store installs depend on WebKit support and extension permissions.",
        theme, extensionsCard));
    root->addWidget(extensionsCard);

    root->addStretch(1);

    // ---- Footer buttons ---------------------------------------------------
    auto *footer = new QHBoxLayout();
    footer->setContentsMargins(0, 0, 0, 0);
    footer->setSpacing(8);
    footer->addStretch(1);
    auto *cancel = new QPushButton("Cancel", this);
    cancel->setCursor(Qt::PointingHandCursor);
    auto *save = new QPushButton("Save", this);
    save->setDefault(true);
    save->setAutoDefault(true);
    save->setCursor(Qt::PointingHandCursor);
    save->setIcon(mac::sfSymbolIcon("checkmark", 12.0, QColor("#1a1306")));
    save->setIconSize(QSize(13, 13));
    cancel->setMinimumWidth(96);
    save->setMinimumWidth(110);
    footer->addWidget(cancel);
    footer->addWidget(save);
    root->addLayout(footer);

    refreshProfiles();

    connect(addProfile, &QPushButton::clicked, this, [this] {
        const QString name = m_newProfile->text().trimmed();
        if (name.isEmpty()) return;
        m_profiles.createProfile(name);
        m_profiles.setCurrentProfile(name);
        m_newProfile->clear();
        refreshProfiles();
    });
    connect(m_profileBox, &QComboBox::currentTextChanged, &m_profiles, &ProfileStore::setCurrentProfile);
    connect(addExtension, &QPushButton::clicked, this, [this] {
        const QString dir = QFileDialog::getExistingDirectory(this, "Choose unpacked Chrome extension");
        if (dir.isEmpty()) return;
        QStringList paths = m_extensionPaths->text().split(';', Qt::SkipEmptyParts);
        if (!paths.contains(dir)) paths << dir;
        m_extensionPaths->setText(paths.join(';'));
    });
    connect(save, &QPushButton::clicked, this, [this] {
        emit homePageChanged(m_homePage->text());
        emit searchEngineChanged(m_searchEngine->text());
        const bool full = m_showFullUrl->isChecked();
        QSettings().setValue("ui/showFullUrl", full);
        emit showFullUrlChanged(full);
        QSettings().setValue("ui/addressBarInSidebar", m_addrInSidebar->isChecked());
        ChromeExtensionManager::setConfiguredPaths(m_extensionPaths->text().split(';', Qt::SkipEmptyParts));
        accept();
    });
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
}

void SettingsDialog::showEvent(QShowEvent *e) {
    QDialog::showEvent(e);
    mac::enableWindowVibrancy(this, mac::VibrancyMaterial::HeaderView);
    mac::makeTitlebarTransparent(this);
}

void SettingsDialog::keyPressEvent(QKeyEvent *e) {
    if (e->key() == Qt::Key_Escape) { reject(); return; }
    QDialog::keyPressEvent(e);
}

void SettingsDialog::refreshProfiles() {
    m_profileBox->blockSignals(true);
    m_profileBox->clear();
    m_profileBox->addItems(m_profiles.profiles());
    m_profileBox->setCurrentText(m_profiles.currentName());
    m_profileBox->blockSignals(false);
}
