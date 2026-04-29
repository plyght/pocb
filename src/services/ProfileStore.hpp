#pragma once

#include <QDir>
#include <QHash>
#include <QObject>
#include <QStringList>

class WebKitProfile;

class ProfileStore final : public QObject {
    Q_OBJECT
public:
    explicit ProfileStore(QObject *parent = nullptr);

    QStringList profiles() const;
    QString currentName() const;
    QString iconName(const QString &name) const;
    WebKitProfile *currentProfile() const;

public slots:
    void setCurrentProfile(const QString &name);
    void createProfile(const QString &name);
    void renameProfile(const QString &oldName, const QString &newName);
    void setIconName(const QString &name, const QString &iconName);

signals:
    void currentProfileChanged(WebKitProfile *profile);
    void profilesChanged();

private:
    QString sanitize(const QString &name) const;
    QDir root() const;
    WebKitProfile *loadProfile(const QString &name);

    QString m_currentName;
    WebKitProfile *m_currentProfile = nullptr;
    QHash<QString, WebKitProfile *> m_cache;
};
