#include "ProfileStore.hpp"

#include "WebKitProfile.hpp"

#include <QFileInfo>
#include <QStandardPaths>

ProfileStore::ProfileStore(QObject *parent) : QObject(parent) {
    root().mkpath(".");
    createProfile("Default");
    setCurrentProfile("Default");
}

QStringList ProfileStore::profiles() const {
    QStringList names;
    const auto entries = root().entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo &entry : entries) names.append(entry.fileName());
    if (names.isEmpty()) names.append("Default");
    return names;
}

QString ProfileStore::currentName() const {
    return m_currentName;
}

WebKitProfile *ProfileStore::currentProfile() const {
    return m_currentProfile;
}

void ProfileStore::setCurrentProfile(const QString &name) {
    const QString clean = sanitize(name);
    if (clean.isEmpty()) return;
    if (m_currentName == clean && m_currentProfile) return;
    m_currentName = clean;
    m_currentProfile = loadProfile(clean);
    emit currentProfileChanged(m_currentProfile);
}

void ProfileStore::createProfile(const QString &name) {
    const QString clean = sanitize(name);
    if (clean.isEmpty()) return;
    root().mkpath(clean);
    emit profilesChanged();
}

QString ProfileStore::sanitize(const QString &name) const {
    QString out = name.trimmed();
    out.replace('/', '-');
    out.replace(':', '-');
    return out;
}

QDir ProfileStore::root() const {
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return QDir(base + "/Profiles");
}

WebKitProfile *ProfileStore::loadProfile(const QString &name) {
    if (auto *cached = m_cache.value(name, nullptr)) return cached;
    createProfile(name);
    auto *profile = new WebKitProfile(name, this);
    m_cache.insert(name, profile);
    return profile;
}
